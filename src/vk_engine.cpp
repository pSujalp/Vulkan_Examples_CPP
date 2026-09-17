#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_types.h>
#include <vk_initializers.h>

#include "VkBootstrap.h"

#include <iostream>

constexpr bool bUseValidationLayers = true;
using namespace std;

void VulkanEngine::init()
{

	SDL_Init(SDL_INIT_VIDEO);

	SDL_DisplayMode dm;
	if (SDL_GetCurrentDisplayMode(0, &dm) == 0)
	{
		_windowExtent.height = dm.h;
		_windowExtent.width = dm.w;
	}

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

	_window = SDL_CreateWindow(
		"Vulkan Engine",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width,
		_windowExtent.height,
		window_flags);

	init_vulkan();

	init_swapchain();

	init_default_renderpass();

	init_framebuffers();

	init_commands();

	init_sync_structures();

	init_offscreen();

	init_descriptor();

	init_pipelines();

	load_meshes();

	load_Quadmesh();

	camera = Camera(glm::vec3(0.f, 0.f, 2.f));

	_isInitialized = true;
}
void VulkanEngine::cleanup()
{
	if (_isInitialized)
	{

		vkDeviceWaitIdle(_device);

		vkDestroyCommandPool(_device, _commandPool, nullptr);

		vmaDestroyAllocator(_allocator);

		vkDestroyFence(_device, _renderFence, nullptr);
		vkDestroySemaphore(_device, _renderSemaphore, nullptr);
		vkDestroySemaphore(_device, _presentSemaphore, nullptr);

		vkDestroySwapchainKHR(_device, _swapchain, nullptr);

		vkDestroyRenderPass(_device, _renderPass, nullptr);

		for (int i = 0; i < _framebuffers.size(); i++)
		{
			vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);

			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
		}

		vkDestroySurfaceKHR(_instance, _surface, nullptr);

		_mainDeletionQueue.flush();

		vkDestroyDevice(_device, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
	}
}

void VulkanEngine::draw()
{

	if (SDL_GetWindowFlags(_window) & SDL_WINDOW_MINIMIZED)
		return;

	VK_CHECK(vkWaitForFences(_device, 1, &_renderFence, true, 1000000000));
	VK_CHECK(vkResetFences(_device, 1, &_renderFence));

	VK_CHECK(vkResetCommandBuffer(_mainCommandBuffer, 0));

	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, 1000000000, _presentSemaphore, nullptr, &swapchainImageIndex));

	VkCommandBuffer cmd = _mainCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
	VkDeviceSize offset = 0;
	VkClearValue offscreenColorClear;
	offscreenColorClear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

	VkClearValue offscreenDepthClear;
	offscreenDepthClear.depthStencil.depth = 1.f;

	VkClearValue offscreenClears[] = {offscreenColorClear, offscreenDepthClear};

	VkRenderPassBeginInfo rpInfoOffscreen = vkinit::renderpass_begin_info(
		_OffScreen_renderPass, _windowExtent, _offscreenFramebuffer);

	rpInfoOffscreen.clearValueCount = 2;
	rpInfoOffscreen.pClearValues = offscreenClears;
	
	vkCmdBeginRenderPass(cmd, &rpInfoOffscreen, VK_SUBPASS_CONTENTS_INLINE);
	for (const auto &i : txs.TextureDescriptor_map){
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipelineLayout, i.second, 1, &i.first, 0, nullptr);
	}
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);



	for (const auto &i : _ModelMeshes)
	{

		vkCmdBindVertexBuffers(cmd, 0, 1, &i._vertexBuffer._buffer, &offset);
		vkCmdBindIndexBuffer(cmd, i._indexBuffer._buffer, 0, VK_INDEX_TYPE_UINT32);

		glm::mat4 view = camera.GetViewMatrix();
		glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom),
												(float)_windowExtent.width / (float)_windowExtent.height, 0.1f, 200.0f);
		projection[1][1] *= -1;

		glm::mat4 model = glm::rotate(glm::mat4{1.0f}, glm::radians(0.0f), glm::vec3(0, 1, 0));
		model = glm::translate(model, glm::vec3(0, 0, -40));
		model = glm::scale(model, glm::vec3(1.5f, 1.5f, 1.5f));

		glm::mat4 mesh_matrix = projection * view * model;

		MeshPushConstants constants;
		constants.render_matrix = mesh_matrix;

		vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);
		vkCmdDrawIndexed(cmd, static_cast<uint32_t>(i._indices.size()), 1, 0, 0, 0);
	}

	vkCmdEndRenderPass(cmd);

	VkClearValue clearValue;	
	clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.f;

	VkClearValue clearValues[] = {clearValue, depthClear};

	VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(_renderPass, _windowExtent, _framebuffers[swapchainImageIndex]);

	rpInfo.clearValueCount = 2;
	rpInfo.pClearValues = clearValues;

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	for (const auto &i : Offtxs.TextureDescriptor_map){
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _QuadPipelineLayout, i.second, 1, &i.first, 0, nullptr);
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _QuadPipeline);
	vkCmdBindVertexBuffers(cmd, 0, 1, &QuadMesh._vertexBuffer._buffer, &offset);
	vkCmdDraw(cmd, QuadMesh._vertices.size(), 1, 0, 0);
	vkCmdEndRenderPass(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = vkinit::submit_info(&cmd);
	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submit.pWaitDstStageMask = &waitStage;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &_presentSemaphore;

	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &_renderSemaphore;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _renderFence));

	VkPresentInfoKHR presentInfo = vkinit::present_info();

	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &_renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	_frameNumber++;
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;

	Uint64 NOW = SDL_GetPerformanceCounter();
	Uint64 LAST = 0;
	double deltaTime = 0;
	int mouseX, mouseY;
	Uint32 mouseState = SDL_GetMouseState(&mouseX, &mouseY);

	float lastX = mouseX;
	float lastY = mouseY;

	while (!bQuit)
	{

		LAST = NOW;
		NOW = SDL_GetPerformanceCounter();
		deltaTime = (double)((NOW - LAST) / (double)SDL_GetPerformanceFrequency());

		string str = "Vulkan Tutorial \t\t\t\t\t\t\t\t\t\t\t\t\t\t FPS:" + to_string((int)(1 / deltaTime));
		SDL_SetWindowTitle(_window, str.c_str());

		while (SDL_PollEvent(&e) != 0)
		{

			switch (e.type)
			{

			case SDL_QUIT:
				bQuit = true;
				break;

			case SDL_MOUSEBUTTONUP:
				if (e.button.button == SDL_BUTTON_LEFT)
				{

					printf("Left mouse button released at position: %d, %d\n", e.button.x, e.button.y);
					SDL_WarpMouseInWindow(_window, _windowExtent.width / 2, _windowExtent.height / 2);
					SDL_ShowCursor(1);
				}
				break;

			case SDL_KEYUP:
				switch (e.key.keysym.sym)
				{
				case SDLK_SPACE:
					printf("Spacebar released.\n");
					break;
				}
			}
		}
		const Uint8 *keystate = SDL_GetKeyboardState(nullptr);
		int mouseX, mouseY;
		mouseState = SDL_GetMouseState(&mouseX, &mouseY);
		bool isLeftInterfaceClick = (mouseState & SDL_BUTTON(SDL_BUTTON_LEFT));
		if (camera.firstMouse)
		{
			lastX = mouseX;
			lastY = mouseY;
			camera.firstMouse = false;
		}
		float xoffset = mouseX - lastX;
		float yoffset = lastY - mouseY;
		lastX = mouseX;
		lastY = mouseY;
		if (isLeftInterfaceClick)
		{
			camera.ProcessMouseMovement(xoffset, yoffset);
			SDL_ShowCursor(0);
			if (keystate[SDL_SCANCODE_W])
				camera.ProcessKeyboard(FORWARD, deltaTime);
			if (keystate[SDL_SCANCODE_S])
				camera.ProcessKeyboard(BACKWARD, deltaTime);
			if (keystate[SDL_SCANCODE_A])
				camera.ProcessKeyboard(LEFT, deltaTime);
			if (keystate[SDL_SCANCODE_D])
				camera.ProcessKeyboard(RIGHT, deltaTime);
		}

		draw();
	}
}

void VulkanEngine::init_vulkan()
{
	vkb::InstanceBuilder builder;

	auto inst_ret = builder.set_app_name("Example Vulkan Application")
						.request_validation_layers(bUseValidationLayers)
						.use_default_debug_messenger()
						.require_api_version(1, 1, 0)
						.build();

	vkb::Instance vkb_inst = inst_ret.value();

	_instance = vkb_inst.instance;
	_debug_messenger = vkb_inst.debug_messenger;

	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

	vkb::PhysicalDeviceSelector selector{vkb_inst};
	vkb::PhysicalDevice physicalDevice = selector
											 .set_minimum_version(1, 1)
											 .set_surface(_surface)
											 .select()
											 .value();

	vkb::DeviceBuilder deviceBuilder{physicalDevice};

	vkb::Device vkbDevice = deviceBuilder.build().value();

	_device = vkbDevice.device;
	_chosenGPU = physicalDevice.physical_device;

	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();

	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	vmaCreateAllocator(&allocatorInfo, &_allocator);
}

void VulkanEngine::init_swapchain()
{
	vkb::SwapchainBuilder swapchainBuilder{_chosenGPU, _device, _surface};

	vkb::Swapchain vkbSwapchain = swapchainBuilder
									  .use_default_format_selection()

									  .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
									  .set_desired_extent(_windowExtent.width, _windowExtent.height)
									  .build()
									  .value();

	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();
	_swachainImageFormat = vkbSwapchain.image_format;


	VkExtent3D depthImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
		1};

	_depthFormat = VK_FORMAT_D32_SFLOAT;

	VkImageCreateInfo dimg_info = vkinit::image_create_info(_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);
	VmaAllocationCreateInfo dimg_allocinfo = {};
	dimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	vmaCreateImage(_allocator, &dimg_info, &dimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr);
	VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(_depthFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
	VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_depthImageView));

	_mainDeletionQueue.push_function([=]()
									 {
		vkDestroyImageView(_device, _depthImageView, nullptr);
		vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation); });

	_mainDeletionQueue.push_function([=]()
									 { vkDestroySwapchainKHR(_device, _swapchain, nullptr); });
}
void VulkanEngine::init_default_renderpass()
{

	VkAttachmentDescription color_attachment = {};
	color_attachment.format = _swachainImageFormat;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription depth_attachment = {};

	depth_attachment.flags = 0;
	depth_attachment.format = _depthFormat;
	depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depth_attachment_ref = {};
	depth_attachment_ref.attachment = 1;
	depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;

	subpass.pDepthStencilAttachment = &depth_attachment_ref;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency depth_dependency = {};
	depth_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	depth_dependency.dstSubpass = 0;
	depth_dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depth_dependency.srcAccessMask = 0;
	depth_dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depth_dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency dependencies[2] = {dependency, depth_dependency};

	VkAttachmentDescription attachments[2] = {color_attachment, depth_attachment};

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

	render_pass_info.attachmentCount = 2;
	render_pass_info.pAttachments = &attachments[0];
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	render_pass_info.dependencyCount = 2;
	render_pass_info.pDependencies = &dependencies[0];

	VK_CHECK(vkCreateRenderPass(_device, &render_pass_info, nullptr, &_renderPass));
	_mainDeletionQueue.push_function([=]()
									 { vkDestroyRenderPass(_device, _renderPass, nullptr); });

	VkAttachmentDescription offscreen_color = {};
	offscreen_color.format = _offscreenFormat;
	offscreen_color.samples = VK_SAMPLE_COUNT_1_BIT;
	offscreen_color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	offscreen_color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	offscreen_color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	offscreen_color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	offscreen_color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	offscreen_color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentReference offscreen_color_ref = {};
	offscreen_color_ref.attachment = 0;
	offscreen_color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription offscreen_depth = {};
	offscreen_depth.format = _depthFormat;
	offscreen_depth.samples = VK_SAMPLE_COUNT_1_BIT;
	offscreen_depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	offscreen_depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	offscreen_depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	offscreen_depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	offscreen_depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	offscreen_depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference offscreen_depth_ref = {};
	offscreen_depth_ref.attachment = 1;
	offscreen_depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription offscreen_subpass = {};
	offscreen_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	offscreen_subpass.colorAttachmentCount = 1;
	offscreen_subpass.pColorAttachments = &offscreen_color_ref;
	offscreen_subpass.pDepthStencilAttachment = &offscreen_depth_ref;

	VkSubpassDependency offscreen_dep_in = {};
	offscreen_dep_in.srcSubpass = VK_SUBPASS_EXTERNAL;
	offscreen_dep_in.dstSubpass = 0;
	offscreen_dep_in.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	offscreen_dep_in.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	offscreen_dep_in.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	offscreen_dep_in.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency offscreen_dep_out = {};
	offscreen_dep_out.srcSubpass = 0;
	offscreen_dep_out.dstSubpass = VK_SUBPASS_EXTERNAL;
	offscreen_dep_out.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	offscreen_dep_out.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	offscreen_dep_out.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	offscreen_dep_out.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	VkSubpassDependency offscreen_deps[2] = {offscreen_dep_in, offscreen_dep_out};
	VkAttachmentDescription offscreen_attachments[2] = {offscreen_color, offscreen_depth};

	VkRenderPassCreateInfo offscreen_rp_info = {};
	offscreen_rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	offscreen_rp_info.attachmentCount = 2;
	offscreen_rp_info.pAttachments = offscreen_attachments;
	offscreen_rp_info.subpassCount = 1;
	offscreen_rp_info.pSubpasses = &offscreen_subpass;
	offscreen_rp_info.dependencyCount = 2;
	offscreen_rp_info.pDependencies = offscreen_deps;

	VK_CHECK(vkCreateRenderPass(_device, &offscreen_rp_info, nullptr, &_OffScreen_renderPass));
	_mainDeletionQueue.push_function([=]()
									 { vkDestroyRenderPass(_device, _OffScreen_renderPass, nullptr); });
}
void VulkanEngine::init_framebuffers()
{

	VkFramebufferCreateInfo fb_info = vkinit::framebuffer_create_info(_renderPass, _windowExtent);

	const uint32_t swapchain_imagecount = _swapchainImages.size();
	_framebuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

	for (int i = 0; i < swapchain_imagecount; i++)
	{

		VkImageView attachments[2];
		attachments[0] = _swapchainImageViews[i];
		attachments[1] = _depthImageView;
		fb_info.pAttachments = attachments;
		fb_info.attachmentCount = 2;
		VK_CHECK(vkCreateFramebuffer(_device, &fb_info, nullptr, &_framebuffers[i]));

		_mainDeletionQueue.push_function([=]()
										 {
			vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr); });
	}
}

void VulkanEngine::init_commands()
{

	VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_commandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_commandPool, 1);

	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_mainCommandBuffer));

	_mainDeletionQueue.push_function([=]()
									 { vkDestroyCommandPool(_device, _commandPool, nullptr); });
}

void VulkanEngine::init_sync_structures()
{
	VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);

	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_renderFence));

	_mainDeletionQueue.push_function([=]()
									 { vkDestroyFence(_device, _renderFence, nullptr); });

	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_presentSemaphore));
	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_renderSemaphore));

	_mainDeletionQueue.push_function([=]()
									 {
        vkDestroySemaphore(_device, _presentSemaphore, nullptr);
        vkDestroySemaphore(_device, _renderSemaphore, nullptr); });
}

bool VulkanEngine::load_shader_module(const char *filePath, VkShaderModule *outShaderModule)
{

	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		return false;
	}

	size_t fileSize = (size_t)file.tellg();

	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	file.seekg(0);

	file.read((char *)buffer.data(), fileSize);

	file.close();

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		return false;
	}
	*outShaderModule = shaderModule;
	return true;
}

void VulkanEngine::init_pipelines()
{
	VkShaderModule triangleFragShader;
	if (!load_shader_module("shaders/triangle_frag.frag.spv", &triangleFragShader))
	{
		std::cout << "Error when building the triangle fragment shader module" << std::endl;
	}
	else
	{
		std::cout << "Triangle fragment shader successfully loaded" << std::endl;
	}

	VkShaderModule triangleVertexShader;
	if (!load_shader_module("shaders/triangle_vert.vert.spv", &triangleVertexShader))
	{
		std::cout << "Error when building the triangle vertex shader module" << std::endl;
	}
	else
	{
		std::cout << "Triangle vertex shader successfully loaded" << std::endl;
	}

	VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();

	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_trianglePipelineLayout));

	PipelineBuilder pipelineBuilder;

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, triangleVertexShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader));

	pipelineBuilder._vertexInputInfo = vkinit::vertex_input_state_create_info();

	pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)_windowExtent.width;
	pipelineBuilder._viewport.height = (float)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = {0, 0};
	pipelineBuilder._scissor.extent = _windowExtent;

	pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);

	pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();

	pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state();

	pipelineBuilder._pipelineLayout = _trianglePipelineLayout;

	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	_trianglePipeline = pipelineBuilder.build_pipeline(_device, _renderPass);

	VertexInputDescription vertexDescription = Vertex::get_vertex_description();

	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();

	pipelineBuilder._shaderStages.clear();

	VkShaderModule meshVertShader;
	if (!load_shader_module("shaders/tri_mesh.vert.spv", &meshVertShader))
	{
		std::cout << "Error when building the triangle vertex shader module" << std::endl;
	}
	else
	{
		std::cout << "Red Triangle vertex shader successfully loaded" << std::endl;
	}

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader));

	pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

	VkPipelineLayoutCreateInfo mesh_pipeline_layout_info = {};
	mesh_pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	mesh_pipeline_layout_info.pNext = nullptr;
	mesh_pipeline_layout_info.setLayoutCount = txs.vkVkDescriptorSet_array.size();
	mesh_pipeline_layout_info.pSetLayouts = txs.vkVkDescriptorSet_array.data();
	mesh_pipeline_layout_info.pushConstantRangeCount = 1;
	VkPushConstantRange push_constant;
	push_constant.offset = 0;
	push_constant.size = sizeof(MeshPushConstants);
	push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	mesh_pipeline_layout_info.pPushConstantRanges = &push_constant;
	mesh_pipeline_layout_info.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(_device, &mesh_pipeline_layout_info, nullptr, &_meshPipelineLayout));

	pipelineBuilder._pipelineLayout = _meshPipelineLayout;

	_meshPipeline = pipelineBuilder.build_pipeline(_device, _OffScreen_renderPass);

	VkShaderModule QuadVertShader;
	VkShaderModule QuadFragShader;
	if (!load_shader_module("shaders/Quad_mesh.vert.spv", &QuadVertShader))
	{
		std::cout << "Error when building the triangle vertex shader module" << std::endl;
	}
	else
	{
		std::cout << "Red Triangle vertex shader successfully loaded" << std::endl;
	}
	if (!load_shader_module("shaders/Quad_mesh.frag.spv", &QuadFragShader))
	{
		std::cout << "Error when building the triangle vertex shader module" << std::endl;
	}
	else
	{
		std::cout << "Red Triangle vertex shader successfully loaded" << std::endl;
	}
	pipelineBuilder._shaderStages.clear();

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, QuadVertShader));

	pipelineBuilder._shaderStages.push_back(
		vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, QuadFragShader));

	VkPipelineLayoutCreateInfo quad_pipeline_layout_info = {};
	quad_pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	quad_pipeline_layout_info.pNext = nullptr;
	quad_pipeline_layout_info.setLayoutCount = Offtxs.vkVkDescriptorSet_array.size();
	quad_pipeline_layout_info.pSetLayouts = Offtxs.vkVkDescriptorSet_array.data();

	push_constant.offset = 0;
	push_constant.size = 0;
	push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	quad_pipeline_layout_info.pPushConstantRanges = &push_constant;
	quad_pipeline_layout_info.pushConstantRangeCount = 0;

	VK_CHECK(vkCreatePipelineLayout(_device, &quad_pipeline_layout_info, nullptr, &_QuadPipelineLayout));
	pipelineBuilder._pipelineLayout = _QuadPipelineLayout;

	_QuadPipeline = pipelineBuilder.build_pipeline(_device, _renderPass);

	vkDestroyShaderModule(_device, triangleFragShader, nullptr);
	vkDestroyShaderModule(_device, triangleVertexShader, nullptr);
	vkDestroyShaderModule(_device, meshVertShader, nullptr);
	vkDestroyShaderModule(_device, QuadVertShader, nullptr);
	vkDestroyShaderModule(_device, QuadFragShader, nullptr);

	_mainDeletionQueue.push_function([=]()
									 {
										 vkDestroyPipeline(_device, _trianglePipeline, nullptr);
										 vkDestroyPipeline(_device, _meshPipeline, nullptr);
										 vkDestroyPipeline(_device, _QuadPipeline, nullptr); });

	_mainDeletionQueue.push_function([=]()
									 { vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
									   vkDestroyPipelineLayout(_device, _QuadPipelineLayout, nullptr);
									   vkDestroyPipelineLayout(_device, _trianglePipelineLayout, nullptr); });
}

void VulkanEngine::load_meshes()
{

	FBX_Model_Loader fbxl = FBX_Model_Loader("assets/cube_animations.fbx");

	for (const auto &i : fbxl.Meshes)
	{
		_ModelMeshes.emplace_back(i.first);
	}
	upload_mesh(_ModelMeshes);
}

void VulkanEngine::upload_mesh(std::vector<Mesh> &model_meshes)
{

	for (size_t i{0}; i < model_meshes.size(); i++)
	{

		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = model_meshes[i]._vertices.size() * sizeof(Vertex);
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		VmaAllocationCreateInfo vmaallocInfo = {};
		vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo,
						&model_meshes[i]._vertexBuffer._buffer,
						&model_meshes[i]._vertexBuffer._allocation,
						nullptr);
		_mainDeletionQueue.push_function([=]()
										 { vmaDestroyBuffer(_allocator, model_meshes[i]._vertexBuffer._buffer, model_meshes[i]._vertexBuffer._allocation); });

		void *data;
		vmaMapMemory(_allocator, model_meshes[i]._vertexBuffer._allocation, &data);
		memcpy(data, model_meshes[i]._vertices.data(), model_meshes[i]._vertices.size() * sizeof(Vertex));
		vmaUnmapMemory(_allocator, model_meshes[i]._vertexBuffer._allocation);

		data = nullptr;

		VkBufferCreateInfo indexBufferInfo = {};
		indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		indexBufferInfo.size = model_meshes[i]._indices.size() * sizeof(uint32_t);
		indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

		VmaAllocationCreateInfo indexAllocInfo = {};
		indexAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

		VK_CHECK(vmaCreateBuffer(_allocator, &indexBufferInfo, &indexAllocInfo,
								 &model_meshes[i]._indexBuffer._buffer,
								 &model_meshes[i]._indexBuffer._allocation,
								 nullptr));
		vmaMapMemory(_allocator, model_meshes[i]._indexBuffer._allocation, &data);
		memcpy(data, model_meshes[i]._indices.data(), model_meshes[i]._indices.size() * sizeof(uint32_t));
		vmaUnmapMemory(_allocator, model_meshes[i]._indexBuffer._allocation);

		_mainDeletionQueue.push_function([=]()
										 { vmaDestroyBuffer(_allocator, model_meshes[i]._indexBuffer._buffer, model_meshes[i]._indexBuffer._allocation); });
	}
}

void VulkanEngine::load_Quadmesh()
{
	QuadMesh._vertices.resize(6);

	QuadMesh._vertices[0].position = glm::vec3{-1.0f, 1.0f, 0.0f};
	QuadMesh._vertices[1].position = glm::vec3{-1.0f, -1.0f, 0.0f};
	QuadMesh._vertices[2].position = glm::vec3{1.0f, -1.0f, 0.0f};
	QuadMesh._vertices[3].position = glm::vec3{-1.0f, 1.0f, 0.0f};
	QuadMesh._vertices[4].position = glm::vec3{1.0f, -1.0f, 0.0f};
	QuadMesh._vertices[5].position = glm::vec3{1.0f, 1.0f, 0.0f};

	QuadMesh._vertices[0].uv = glm::vec2{0.0f, 1.0f};
	QuadMesh._vertices[1].uv = glm::vec2{0.0f, 0.0f};
	QuadMesh._vertices[2].uv = glm::vec2{1.0f, 0.0f};
	QuadMesh._vertices[3].uv = glm::vec2{0.0f, 1.0f};
	QuadMesh._vertices[4].uv = glm::vec2{1.0f, 0.0f};
	QuadMesh._vertices[5].uv = glm::vec2{1.0f, 1.0f};

	upload_mesh(QuadMesh);
}

void VulkanEngine::upload_mesh(Mesh &mesh)
{

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;

	bufferInfo.size = mesh._vertices.size() * sizeof(Vertex);

	bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

	vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo,
					&mesh._vertexBuffer._buffer,
					&mesh._vertexBuffer._allocation,
					nullptr);

	_mainDeletionQueue.push_function([=]()
									 { vmaDestroyBuffer(_allocator, mesh._vertexBuffer._buffer, mesh._vertexBuffer._allocation); });

	void *data;
	vmaMapMemory(_allocator, mesh._vertexBuffer._allocation, &data);

	memcpy(data, mesh._vertices.data(), mesh._vertices.size() * sizeof(Vertex));
	vmaUnmapMemory(_allocator, mesh._vertexBuffer._allocation);
}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;

	AllocatedBuffer newBuffer;

	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo,
							 &newBuffer._buffer,
							 &newBuffer._allocation,
							 nullptr));

	return newBuffer;
}

void VulkanEngine::init_descriptor()
{
	std::vector<VkDescriptorPoolSize> sizes =
		{
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
			{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10}};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = 0;
	pool_info.maxSets = 10;
	pool_info.poolSizeCount = (uint32_t)sizes.size();
	pool_info.pPoolSizes = sizes.data();
	vkCreateDescriptorPool(_device, &pool_info, nullptr, &_descriptorPool);

	bool texLoaded = vkutil::load_image_from_file(*this, "assets/car_low.png", _texture);

	txs = Texture_Slots(_device, _mainDeletionQueue, _descriptorPool);
	txs.Add(_texture, 0, 0);

	Offtxs = Texture_Slots(_device, _mainDeletionQueue, _descriptorPool);
	Offtxs.Add(_offscreenImageView, _offscreenSampler, 0, 0);
}

void VulkanEngine::init_offscreen()
{
	VkExtent3D offscreenExtent = {_windowExtent.width, _windowExtent.height, 1};

	VkImageCreateInfo img_info = vkinit::image_create_info(
		_offscreenFormat,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		offscreenExtent);

	VmaAllocationCreateInfo img_alloc_info = {};
	img_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	img_alloc_info.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(_allocator, &img_info, &img_alloc_info,
				   &_offscreenImage.image, &_offscreenImage.allocation, nullptr);
	VkImageViewCreateInfo view_info = vkinit::imageview_create_info(
		_offscreenFormat, _offscreenImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &_offscreenImageView));
	VkImageCreateInfo dimg_info = vkinit::image_create_info(
		_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, offscreenExtent);
	VmaAllocationCreateInfo dimg_alloc_info = {};
	dimg_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimg_alloc_info.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(_allocator, &dimg_info, &dimg_alloc_info,
				   &_offscreenDepthImage.image, &_offscreenDepthImage.allocation, nullptr);

	VkImageViewCreateInfo dview_info = vkinit::imageview_create_info(
		_depthFormat, _offscreenDepthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(_device, &dview_info, nullptr, &_offscreenDepthImageView));

	VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR);
	VK_CHECK(vkCreateSampler(_device, &samplerInfo, nullptr, &_offscreenSampler));
	VkImageView attachments[2] = {_offscreenImageView, _offscreenDepthImageView};

	VkFramebufferCreateInfo fb_info = {};
	fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fb_info.renderPass = _OffScreen_renderPass;
	fb_info.attachmentCount = 2;
	fb_info.pAttachments = attachments;
	fb_info.width = _windowExtent.width;
	fb_info.height = _windowExtent.height;
	fb_info.layers = 1;
	VK_CHECK(vkCreateFramebuffer(_device, &fb_info, nullptr, &_offscreenFramebuffer));
	

	_mainDeletionQueue.push_function([=](){
		vkDestroySampler(_device, _offscreenSampler, nullptr);
		vkDestroyFramebuffer(_device, _offscreenFramebuffer, nullptr);
		vkDestroyImageView(_device, _offscreenImageView, nullptr);
		vmaDestroyImage(_allocator, _offscreenImage.image, _offscreenImage.allocation);
		vkDestroyImageView(_device, _offscreenDepthImageView, nullptr);
		vmaDestroyImage(_allocator, _offscreenDepthImage.image, _offscreenDepthImage.allocation); });
}