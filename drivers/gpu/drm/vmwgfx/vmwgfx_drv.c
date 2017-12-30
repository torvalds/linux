/**************************************************************************
 *
 * Copyright © 2009-2016 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
#include <linux/module.h>
#include <linux/console.h>

#include <drm/drmP.h>
#include "vmwgfx_drv.h"
#include "vmwgfx_binding.h"
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_object.h>
#include <drm/ttm/ttm_module.h>
#include <linux/dma_remapping.h>

#define VMWGFX_DRIVER_DESC "Linux drm driver for VMware graphics devices"
#define VMWGFX_CHIP_SVGAII 0
#define VMW_FB_RESERVATION 0

#define VMW_MIN_INITIAL_WIDTH 800
#define VMW_MIN_INITIAL_HEIGHT 600

#ifndef VMWGFX_GIT_VERSION
#define VMWGFX_GIT_VERSION "Unknown"
#endif

#define VMWGFX_REPO "In Tree"


/**
 * Fully encoded drm commands. Might move to vmw_drm.h
 */

#define DRM_IOCTL_VMW_GET_PARAM					\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VMW_GET_PARAM,		\
		 struct drm_vmw_getparam_arg)
#define DRM_IOCTL_VMW_ALLOC_DMABUF				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VMW_ALLOC_DMABUF,	\
		union drm_vmw_alloc_dmabuf_arg)
#define DRM_IOCTL_VMW_UNREF_DMABUF				\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_UNREF_DMABUF,	\
		struct drm_vmw_unref_dmabuf_arg)
#define DRM_IOCTL_VMW_CURSOR_BYPASS				\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_CURSOR_BYPASS,	\
		 struct drm_vmw_cursor_bypass_arg)

#define DRM_IOCTL_VMW_CONTROL_STREAM				\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_CONTROL_STREAM,	\
		 struct drm_vmw_control_stream_arg)
#define DRM_IOCTL_VMW_CLAIM_STREAM				\
	DRM_IOR(DRM_COMMAND_BASE + DRM_VMW_CLAIM_STREAM,	\
		 struct drm_vmw_stream_arg)
#define DRM_IOCTL_VMW_UNREF_STREAM				\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_UNREF_STREAM,	\
		 struct drm_vmw_stream_arg)

#define DRM_IOCTL_VMW_CREATE_CONTEXT				\
	DRM_IOR(DRM_COMMAND_BASE + DRM_VMW_CREATE_CONTEXT,	\
		struct drm_vmw_context_arg)
#define DRM_IOCTL_VMW_UNREF_CONTEXT				\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_UNREF_CONTEXT,	\
		struct drm_vmw_context_arg)
#define DRM_IOCTL_VMW_CREATE_SURFACE				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VMW_CREATE_SURFACE,	\
		 union drm_vmw_surface_create_arg)
#define DRM_IOCTL_VMW_UNREF_SURFACE				\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_UNREF_SURFACE,	\
		 struct drm_vmw_surface_arg)
#define DRM_IOCTL_VMW_REF_SURFACE				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VMW_REF_SURFACE,	\
		 union drm_vmw_surface_reference_arg)
#define DRM_IOCTL_VMW_EXECBUF					\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_EXECBUF,		\
		struct drm_vmw_execbuf_arg)
#define DRM_IOCTL_VMW_GET_3D_CAP				\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_GET_3D_CAP,		\
		 struct drm_vmw_get_3d_cap_arg)
#define DRM_IOCTL_VMW_FENCE_WAIT				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VMW_FENCE_WAIT,		\
		 struct drm_vmw_fence_wait_arg)
#define DRM_IOCTL_VMW_FENCE_SIGNALED				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VMW_FENCE_SIGNALED,	\
		 struct drm_vmw_fence_signaled_arg)
#define DRM_IOCTL_VMW_FENCE_UNREF				\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_FENCE_UNREF,		\
		 struct drm_vmw_fence_arg)
#define DRM_IOCTL_VMW_FENCE_EVENT				\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_FENCE_EVENT,		\
		 struct drm_vmw_fence_event_arg)
#define DRM_IOCTL_VMW_PRESENT					\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_PRESENT,		\
		 struct drm_vmw_present_arg)
#define DRM_IOCTL_VMW_PRESENT_READBACK				\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_PRESENT_READBACK,	\
		 struct drm_vmw_present_readback_arg)
#define DRM_IOCTL_VMW_UPDATE_LAYOUT				\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_UPDATE_LAYOUT,	\
		 struct drm_vmw_update_layout_arg)
#define DRM_IOCTL_VMW_CREATE_SHADER				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VMW_CREATE_SHADER,	\
		 struct drm_vmw_shader_create_arg)
#define DRM_IOCTL_VMW_UNREF_SHADER				\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_UNREF_SHADER,	\
		 struct drm_vmw_shader_arg)
#define DRM_IOCTL_VMW_GB_SURFACE_CREATE				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VMW_GB_SURFACE_CREATE,	\
		 union drm_vmw_gb_surface_create_arg)
#define DRM_IOCTL_VMW_GB_SURFACE_REF				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VMW_GB_SURFACE_REF,	\
		 union drm_vmw_gb_surface_reference_arg)
#define DRM_IOCTL_VMW_SYNCCPU					\
	DRM_IOW(DRM_COMMAND_BASE + DRM_VMW_SYNCCPU,		\
		 struct drm_vmw_synccpu_arg)
#define DRM_IOCTL_VMW_CREATE_EXTENDED_CONTEXT			\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VMW_CREATE_EXTENDED_CONTEXT,	\
		struct drm_vmw_context_arg)

/**
 * The core DRM version of this macro doesn't account for
 * DRM_COMMAND_BASE.
 */

#define VMW_IOCTL_DEF(ioctl, func, flags) \
  [DRM_IOCTL_NR(DRM_IOCTL_##ioctl) - DRM_COMMAND_BASE] = {DRM_IOCTL_##ioctl, flags, func}

/**
 * Ioctl definitions.
 */

static const struct drm_ioctl_desc vmw_ioctls[] = {
	VMW_IOCTL_DEF(VMW_GET_PARAM, vmw_getparam_ioctl,
		      DRM_AUTH | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_ALLOC_DMABUF, vmw_dmabuf_alloc_ioctl,
		      DRM_AUTH | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_UNREF_DMABUF, vmw_dmabuf_unref_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_CURSOR_BYPASS,
		      vmw_kms_cursor_bypass_ioctl,
		      DRM_MASTER | DRM_CONTROL_ALLOW),

	VMW_IOCTL_DEF(VMW_CONTROL_STREAM, vmw_overlay_ioctl,
		      DRM_MASTER | DRM_CONTROL_ALLOW),
	VMW_IOCTL_DEF(VMW_CLAIM_STREAM, vmw_stream_claim_ioctl,
		      DRM_MASTER | DRM_CONTROL_ALLOW),
	VMW_IOCTL_DEF(VMW_UNREF_STREAM, vmw_stream_unref_ioctl,
		      DRM_MASTER | DRM_CONTROL_ALLOW),

	VMW_IOCTL_DEF(VMW_CREATE_CONTEXT, vmw_context_define_ioctl,
		      DRM_AUTH | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_UNREF_CONTEXT, vmw_context_destroy_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_CREATE_SURFACE, vmw_surface_define_ioctl,
		      DRM_AUTH | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_UNREF_SURFACE, vmw_surface_destroy_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_REF_SURFACE, vmw_surface_reference_ioctl,
		      DRM_AUTH | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_EXECBUF, NULL, DRM_AUTH |
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_FENCE_WAIT, vmw_fence_obj_wait_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_FENCE_SIGNALED,
		      vmw_fence_obj_signaled_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_FENCE_UNREF, vmw_fence_obj_unref_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_FENCE_EVENT, vmw_fence_event_ioctl,
		      DRM_AUTH | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_GET_3D_CAP, vmw_get_cap_3d_ioctl,
		      DRM_AUTH | DRM_RENDER_ALLOW),

	/* these allow direct access to the framebuffers mark as master only */
	VMW_IOCTL_DEF(VMW_PRESENT, vmw_present_ioctl,
		      DRM_MASTER | DRM_AUTH),
	VMW_IOCTL_DEF(VMW_PRESENT_READBACK,
		      vmw_present_readback_ioctl,
		      DRM_MASTER | DRM_AUTH),
	/*
	 * The permissions of the below ioctl are overridden in
	 * vmw_generic_ioctl(). We require either
	 * DRM_MASTER or capable(CAP_SYS_ADMIN).
	 */
	VMW_IOCTL_DEF(VMW_UPDATE_LAYOUT,
		      vmw_kms_update_layout_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_CREATE_SHADER,
		      vmw_shader_define_ioctl,
		      DRM_AUTH | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_UNREF_SHADER,
		      vmw_shader_destroy_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_GB_SURFACE_CREATE,
		      vmw_gb_surface_define_ioctl,
		      DRM_AUTH | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_GB_SURFACE_REF,
		      vmw_gb_surface_reference_ioctl,
		      DRM_AUTH | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_SYNCCPU,
		      vmw_user_dmabuf_synccpu_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_CREATE_EXTENDED_CONTEXT,
		      vmw_extended_context_define_ioctl,
		      DRM_AUTH | DRM_RENDER_ALLOW),
};

static const struct pci_device_id vmw_pci_id_list[] = {
	{0x15ad, 0x0405, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VMWGFX_CHIP_SVGAII},
	{0, 0, 0}
};
MODULE_DEVICE_TABLE(pci, vmw_pci_id_list);

static int enable_fbdev = IS_ENABLED(CONFIG_DRM_VMWGFX_FBCON);
static int vmw_force_iommu;
static int vmw_restrict_iommu;
static int vmw_force_coherent;
static int vmw_restrict_dma_mask;
static int vmw_assume_16bpp;

static int vmw_probe(struct pci_dev *, const struct pci_device_id *);
static void vmw_master_init(struct vmw_master *);
static int vmwgfx_pm_notifier(struct notifier_block *nb, unsigned long val,
			      void *ptr);

MODULE_PARM_DESC(enable_fbdev, "Enable vmwgfx fbdev");
module_param_named(enable_fbdev, enable_fbdev, int, 0600);
MODULE_PARM_DESC(force_dma_api, "Force using the DMA API for TTM pages");
module_param_named(force_dma_api, vmw_force_iommu, int, 0600);
MODULE_PARM_DESC(restrict_iommu, "Try to limit IOMMU usage for TTM pages");
module_param_named(restrict_iommu, vmw_restrict_iommu, int, 0600);
MODULE_PARM_DESC(force_coherent, "Force coherent TTM pages");
module_param_named(force_coherent, vmw_force_coherent, int, 0600);
MODULE_PARM_DESC(restrict_dma_mask, "Restrict DMA mask to 44 bits with IOMMU");
module_param_named(restrict_dma_mask, vmw_restrict_dma_mask, int, 0600);
MODULE_PARM_DESC(assume_16bpp, "Assume 16-bpp when filtering modes");
module_param_named(assume_16bpp, vmw_assume_16bpp, int, 0600);


static void vmw_print_capabilities(uint32_t capabilities)
{
	DRM_INFO("Capabilities:\n");
	if (capabilities & SVGA_CAP_RECT_COPY)
		DRM_INFO("  Rect copy.\n");
	if (capabilities & SVGA_CAP_CURSOR)
		DRM_INFO("  Cursor.\n");
	if (capabilities & SVGA_CAP_CURSOR_BYPASS)
		DRM_INFO("  Cursor bypass.\n");
	if (capabilities & SVGA_CAP_CURSOR_BYPASS_2)
		DRM_INFO("  Cursor bypass 2.\n");
	if (capabilities & SVGA_CAP_8BIT_EMULATION)
		DRM_INFO("  8bit emulation.\n");
	if (capabilities & SVGA_CAP_ALPHA_CURSOR)
		DRM_INFO("  Alpha cursor.\n");
	if (capabilities & SVGA_CAP_3D)
		DRM_INFO("  3D.\n");
	if (capabilities & SVGA_CAP_EXTENDED_FIFO)
		DRM_INFO("  Extended Fifo.\n");
	if (capabilities & SVGA_CAP_MULTIMON)
		DRM_INFO("  Multimon.\n");
	if (capabilities & SVGA_CAP_PITCHLOCK)
		DRM_INFO("  Pitchlock.\n");
	if (capabilities & SVGA_CAP_IRQMASK)
		DRM_INFO("  Irq mask.\n");
	if (capabilities & SVGA_CAP_DISPLAY_TOPOLOGY)
		DRM_INFO("  Display Topology.\n");
	if (capabilities & SVGA_CAP_GMR)
		DRM_INFO("  GMR.\n");
	if (capabilities & SVGA_CAP_TRACES)
		DRM_INFO("  Traces.\n");
	if (capabilities & SVGA_CAP_GMR2)
		DRM_INFO("  GMR2.\n");
	if (capabilities & SVGA_CAP_SCREEN_OBJECT_2)
		DRM_INFO("  Screen Object 2.\n");
	if (capabilities & SVGA_CAP_COMMAND_BUFFERS)
		DRM_INFO("  Command Buffers.\n");
	if (capabilities & SVGA_CAP_CMD_BUFFERS_2)
		DRM_INFO("  Command Buffers 2.\n");
	if (capabilities & SVGA_CAP_GBOBJECTS)
		DRM_INFO("  Guest Backed Resources.\n");
	if (capabilities & SVGA_CAP_DX)
		DRM_INFO("  DX Features.\n");
}

/**
 * vmw_dummy_query_bo_create - create a bo to hold a dummy query result
 *
 * @dev_priv: A device private structure.
 *
 * This function creates a small buffer object that holds the query
 * result for dummy queries emitted as query barriers.
 * The function will then map the first page and initialize a pending
 * occlusion query result structure, Finally it will unmap the buffer.
 * No interruptible waits are done within this function.
 *
 * Returns an error if bo creation or initialization fails.
 */
static int vmw_dummy_query_bo_create(struct vmw_private *dev_priv)
{
	int ret;
	struct vmw_dma_buffer *vbo;
	struct ttm_bo_kmap_obj map;
	volatile SVGA3dQueryResult *result;
	bool dummy;

	/*
	 * Create the vbo as pinned, so that a tryreserve will
	 * immediately succeed. This is because we're the only
	 * user of the bo currently.
	 */
	vbo = kzalloc(sizeof(*vbo), GFP_KERNEL);
	if (!vbo)
		return -ENOMEM;

	ret = vmw_dmabuf_init(dev_priv, vbo, PAGE_SIZE,
			      &vmw_sys_ne_placement, false,
			      &vmw_dmabuf_bo_free);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_bo_reserve(&vbo->base, false, true, NULL);
	BUG_ON(ret != 0);
	vmw_bo_pin_reserved(vbo, true);

	ret = ttm_bo_kmap(&vbo->base, 0, 1, &map);
	if (likely(ret == 0)) {
		result = ttm_kmap_obj_virtual(&map, &dummy);
		result->totalSize = sizeof(*result);
		result->state = SVGA3D_QUERYSTATE_PENDING;
		result->result32 = 0xff;
		ttm_bo_kunmap(&map);
	}
	vmw_bo_pin_reserved(vbo, false);
	ttm_bo_unreserve(&vbo->base);

	if (unlikely(ret != 0)) {
		DRM_ERROR("Dummy query buffer map failed.\n");
		vmw_dmabuf_unreference(&vbo);
	} else
		dev_priv->dummy_query_bo = vbo;

	return ret;
}

/**
 * vmw_request_device_late - Perform late device setup
 *
 * @dev_priv: Pointer to device private.
 *
 * This function performs setup of otables and enables large command
 * buffer submission. These tasks are split out to a separate function
 * because it reverts vmw_release_device_early and is intended to be used
 * by an error path in the hibernation code.
 */
static int vmw_request_device_late(struct vmw_private *dev_priv)
{
	int ret;

	if (dev_priv->has_mob) {
		ret = vmw_otables_setup(dev_priv);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Unable to initialize "
				  "guest Memory OBjects.\n");
			return ret;
		}
	}

	if (dev_priv->cman) {
		ret = vmw_cmdbuf_set_pool_size(dev_priv->cman,
					       256*4096, 2*4096);
		if (ret) {
			struct vmw_cmdbuf_man *man = dev_priv->cman;

			dev_priv->cman = NULL;
			vmw_cmdbuf_man_destroy(man);
		}
	}

	return 0;
}

static int vmw_request_device(struct vmw_private *dev_priv)
{
	int ret;

	ret = vmw_fifo_init(dev_priv, &dev_priv->fifo);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Unable to initialize FIFO.\n");
		return ret;
	}
	vmw_fence_fifo_up(dev_priv->fman);
	dev_priv->cman = vmw_cmdbuf_man_create(dev_priv);
	if (IS_ERR(dev_priv->cman)) {
		dev_priv->cman = NULL;
		dev_priv->has_dx = false;
	}

	ret = vmw_request_device_late(dev_priv);
	if (ret)
		goto out_no_mob;

	ret = vmw_dummy_query_bo_create(dev_priv);
	if (unlikely(ret != 0))
		goto out_no_query_bo;

	return 0;

out_no_query_bo:
	if (dev_priv->cman)
		vmw_cmdbuf_remove_pool(dev_priv->cman);
	if (dev_priv->has_mob) {
		(void) ttm_bo_evict_mm(&dev_priv->bdev, VMW_PL_MOB);
		vmw_otables_takedown(dev_priv);
	}
	if (dev_priv->cman)
		vmw_cmdbuf_man_destroy(dev_priv->cman);
out_no_mob:
	vmw_fence_fifo_down(dev_priv->fman);
	vmw_fifo_release(dev_priv, &dev_priv->fifo);
	return ret;
}

/**
 * vmw_release_device_early - Early part of fifo takedown.
 *
 * @dev_priv: Pointer to device private struct.
 *
 * This is the first part of command submission takedown, to be called before
 * buffer management is taken down.
 */
static void vmw_release_device_early(struct vmw_private *dev_priv)
{
	/*
	 * Previous destructions should've released
	 * the pinned bo.
	 */

	BUG_ON(dev_priv->pinned_bo != NULL);

	vmw_dmabuf_unreference(&dev_priv->dummy_query_bo);
	if (dev_priv->cman)
		vmw_cmdbuf_remove_pool(dev_priv->cman);

	if (dev_priv->has_mob) {
		ttm_bo_evict_mm(&dev_priv->bdev, VMW_PL_MOB);
		vmw_otables_takedown(dev_priv);
	}
}

/**
 * vmw_release_device_late - Late part of fifo takedown.
 *
 * @dev_priv: Pointer to device private struct.
 *
 * This is the last part of the command submission takedown, to be called when
 * command submission is no longer needed. It may wait on pending fences.
 */
static void vmw_release_device_late(struct vmw_private *dev_priv)
{
	vmw_fence_fifo_down(dev_priv->fman);
	if (dev_priv->cman)
		vmw_cmdbuf_man_destroy(dev_priv->cman);

	vmw_fifo_release(dev_priv, &dev_priv->fifo);
}

/**
 * Sets the initial_[width|height] fields on the given vmw_private.
 *
 * It does so by reading SVGA_REG_[WIDTH|HEIGHT] regs and then
 * clamping the value to fb_max_[width|height] fields and the
 * VMW_MIN_INITIAL_[WIDTH|HEIGHT].
 * If the values appear to be invalid, set them to
 * VMW_MIN_INITIAL_[WIDTH|HEIGHT].
 */
static void vmw_get_initial_size(struct vmw_private *dev_priv)
{
	uint32_t width;
	uint32_t height;

	width = vmw_read(dev_priv, SVGA_REG_WIDTH);
	height = vmw_read(dev_priv, SVGA_REG_HEIGHT);

	width = max_t(uint32_t, width, VMW_MIN_INITIAL_WIDTH);
	height = max_t(uint32_t, height, VMW_MIN_INITIAL_HEIGHT);

	if (width > dev_priv->fb_max_width ||
	    height > dev_priv->fb_max_height) {

		/*
		 * This is a host error and shouldn't occur.
		 */

		width = VMW_MIN_INITIAL_WIDTH;
		height = VMW_MIN_INITIAL_HEIGHT;
	}

	dev_priv->initial_width = width;
	dev_priv->initial_height = height;
}

/**
 * vmw_dma_select_mode - Determine how DMA mappings should be set up for this
 * system.
 *
 * @dev_priv: Pointer to a struct vmw_private
 *
 * This functions tries to determine the IOMMU setup and what actions
 * need to be taken by the driver to make system pages visible to the
 * device.
 * If this function decides that DMA is not possible, it returns -EINVAL.
 * The driver may then try to disable features of the device that require
 * DMA.
 */
static int vmw_dma_select_mode(struct vmw_private *dev_priv)
{
	static const char *names[vmw_dma_map_max] = {
		[vmw_dma_phys] = "Using physical TTM page addresses.",
		[vmw_dma_alloc_coherent] = "Using coherent TTM pages.",
		[vmw_dma_map_populate] = "Keeping DMA mappings.",
		[vmw_dma_map_bind] = "Giving up DMA mappings early."};
#ifdef CONFIG_X86
	const struct dma_map_ops *dma_ops = get_dma_ops(dev_priv->dev->dev);

#ifdef CONFIG_INTEL_IOMMU
	if (intel_iommu_enabled) {
		dev_priv->map_mode = vmw_dma_map_populate;
		goto out_fixup;
	}
#endif

	if (!(vmw_force_iommu || vmw_force_coherent)) {
		dev_priv->map_mode = vmw_dma_phys;
		DRM_INFO("DMA map mode: %s\n", names[dev_priv->map_mode]);
		return 0;
	}

	dev_priv->map_mode = vmw_dma_map_populate;

	if (dma_ops->sync_single_for_cpu)
		dev_priv->map_mode = vmw_dma_alloc_coherent;
#ifdef CONFIG_SWIOTLB
	if (swiotlb_nr_tbl() == 0)
		dev_priv->map_mode = vmw_dma_map_populate;
#endif

#ifdef CONFIG_INTEL_IOMMU
out_fixup:
#endif
	if (dev_priv->map_mode == vmw_dma_map_populate &&
	    vmw_restrict_iommu)
		dev_priv->map_mode = vmw_dma_map_bind;

	if (vmw_force_coherent)
		dev_priv->map_mode = vmw_dma_alloc_coherent;

#if !defined(CONFIG_SWIOTLB) && !defined(CONFIG_INTEL_IOMMU)
	/*
	 * No coherent page pool
	 */
	if (dev_priv->map_mode == vmw_dma_alloc_coherent)
		return -EINVAL;
#endif

#else /* CONFIG_X86 */
	dev_priv->map_mode = vmw_dma_map_populate;
#endif /* CONFIG_X86 */

	DRM_INFO("DMA map mode: %s\n", names[dev_priv->map_mode]);

	return 0;
}

/**
 * vmw_dma_masks - set required page- and dma masks
 *
 * @dev: Pointer to struct drm-device
 *
 * With 32-bit we can only handle 32 bit PFNs. Optionally set that
 * restriction also for 64-bit systems.
 */
#ifdef CONFIG_INTEL_IOMMU
static int vmw_dma_masks(struct vmw_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;

	if (intel_iommu_enabled &&
	    (sizeof(unsigned long) == 4 || vmw_restrict_dma_mask)) {
		DRM_INFO("Restricting DMA addresses to 44 bits.\n");
		return dma_set_mask(dev->dev, DMA_BIT_MASK(44));
	}
	return 0;
}
#else
static int vmw_dma_masks(struct vmw_private *dev_priv)
{
	return 0;
}
#endif

static int vmw_driver_load(struct drm_device *dev, unsigned long chipset)
{
	struct vmw_private *dev_priv;
	int ret;
	uint32_t svga_id;
	enum vmw_res_type i;
	bool refuse_dma = false;
	char host_log[100] = {0};

	dev_priv = kzalloc(sizeof(*dev_priv), GFP_KERNEL);
	if (unlikely(!dev_priv)) {
		DRM_ERROR("Failed allocating a device private struct.\n");
		return -ENOMEM;
	}

	pci_set_master(dev->pdev);

	dev_priv->dev = dev;
	dev_priv->vmw_chipset = chipset;
	dev_priv->last_read_seqno = (uint32_t) -100;
	mutex_init(&dev_priv->cmdbuf_mutex);
	mutex_init(&dev_priv->release_mutex);
	mutex_init(&dev_priv->binding_mutex);
	mutex_init(&dev_priv->global_kms_state_mutex);
	rwlock_init(&dev_priv->resource_lock);
	ttm_lock_init(&dev_priv->reservation_sem);
	spin_lock_init(&dev_priv->hw_lock);
	spin_lock_init(&dev_priv->waiter_lock);
	spin_lock_init(&dev_priv->cap_lock);
	spin_lock_init(&dev_priv->svga_lock);
	spin_lock_init(&dev_priv->cursor_lock);

	for (i = vmw_res_context; i < vmw_res_max; ++i) {
		idr_init(&dev_priv->res_idr[i]);
		INIT_LIST_HEAD(&dev_priv->res_lru[i]);
	}

	mutex_init(&dev_priv->init_mutex);
	init_waitqueue_head(&dev_priv->fence_queue);
	init_waitqueue_head(&dev_priv->fifo_queue);
	dev_priv->fence_queue_waiters = 0;
	dev_priv->fifo_queue_waiters = 0;

	dev_priv->used_memory_size = 0;

	dev_priv->io_start = pci_resource_start(dev->pdev, 0);
	dev_priv->vram_start = pci_resource_start(dev->pdev, 1);
	dev_priv->mmio_start = pci_resource_start(dev->pdev, 2);

	dev_priv->assume_16bpp = !!vmw_assume_16bpp;

	dev_priv->enable_fb = enable_fbdev;

	vmw_write(dev_priv, SVGA_REG_ID, SVGA_ID_2);
	svga_id = vmw_read(dev_priv, SVGA_REG_ID);
	if (svga_id != SVGA_ID_2) {
		ret = -ENOSYS;
		DRM_ERROR("Unsupported SVGA ID 0x%x\n", svga_id);
		goto out_err0;
	}

	dev_priv->capabilities = vmw_read(dev_priv, SVGA_REG_CAPABILITIES);
	ret = vmw_dma_select_mode(dev_priv);
	if (unlikely(ret != 0)) {
		DRM_INFO("Restricting capabilities due to IOMMU setup.\n");
		refuse_dma = true;
	}

	dev_priv->vram_size = vmw_read(dev_priv, SVGA_REG_VRAM_SIZE);
	dev_priv->mmio_size = vmw_read(dev_priv, SVGA_REG_MEM_SIZE);
	dev_priv->fb_max_width = vmw_read(dev_priv, SVGA_REG_MAX_WIDTH);
	dev_priv->fb_max_height = vmw_read(dev_priv, SVGA_REG_MAX_HEIGHT);

	vmw_get_initial_size(dev_priv);

	if (dev_priv->capabilities & SVGA_CAP_GMR2) {
		dev_priv->max_gmr_ids =
			vmw_read(dev_priv, SVGA_REG_GMR_MAX_IDS);
		dev_priv->max_gmr_pages =
			vmw_read(dev_priv, SVGA_REG_GMRS_MAX_PAGES);
		dev_priv->memory_size =
			vmw_read(dev_priv, SVGA_REG_MEMORY_SIZE);
		dev_priv->memory_size -= dev_priv->vram_size;
	} else {
		/*
		 * An arbitrary limit of 512MiB on surface
		 * memory. But all HWV8 hardware supports GMR2.
		 */
		dev_priv->memory_size = 512*1024*1024;
	}
	dev_priv->max_mob_pages = 0;
	dev_priv->max_mob_size = 0;
	if (dev_priv->capabilities & SVGA_CAP_GBOBJECTS) {
		uint64_t mem_size =
			vmw_read(dev_priv,
				 SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB);

		/*
		 * Workaround for low memory 2D VMs to compensate for the
		 * allocation taken by fbdev
		 */
		if (!(dev_priv->capabilities & SVGA_CAP_3D))
			mem_size *= 3;

		dev_priv->max_mob_pages = mem_size * 1024 / PAGE_SIZE;
		dev_priv->prim_bb_mem =
			vmw_read(dev_priv,
				 SVGA_REG_MAX_PRIMARY_BOUNDING_BOX_MEM);
		dev_priv->max_mob_size =
			vmw_read(dev_priv, SVGA_REG_MOB_MAX_SIZE);
		dev_priv->stdu_max_width =
			vmw_read(dev_priv, SVGA_REG_SCREENTARGET_MAX_WIDTH);
		dev_priv->stdu_max_height =
			vmw_read(dev_priv, SVGA_REG_SCREENTARGET_MAX_HEIGHT);

		vmw_write(dev_priv, SVGA_REG_DEV_CAP,
			  SVGA3D_DEVCAP_MAX_TEXTURE_WIDTH);
		dev_priv->texture_max_width = vmw_read(dev_priv,
						       SVGA_REG_DEV_CAP);
		vmw_write(dev_priv, SVGA_REG_DEV_CAP,
			  SVGA3D_DEVCAP_MAX_TEXTURE_HEIGHT);
		dev_priv->texture_max_height = vmw_read(dev_priv,
							SVGA_REG_DEV_CAP);
	} else {
		dev_priv->texture_max_width = 8192;
		dev_priv->texture_max_height = 8192;
		dev_priv->prim_bb_mem = dev_priv->vram_size;
	}

	vmw_print_capabilities(dev_priv->capabilities);

	ret = vmw_dma_masks(dev_priv);
	if (unlikely(ret != 0))
		goto out_err0;

	if (dev_priv->capabilities & SVGA_CAP_GMR2) {
		DRM_INFO("Max GMR ids is %u\n",
			 (unsigned)dev_priv->max_gmr_ids);
		DRM_INFO("Max number of GMR pages is %u\n",
			 (unsigned)dev_priv->max_gmr_pages);
		DRM_INFO("Max dedicated hypervisor surface memory is %u kiB\n",
			 (unsigned)dev_priv->memory_size / 1024);
	}
	DRM_INFO("Maximum display memory size is %u kiB\n",
		 dev_priv->prim_bb_mem / 1024);
	DRM_INFO("VRAM at 0x%08x size is %u kiB\n",
		 dev_priv->vram_start, dev_priv->vram_size / 1024);
	DRM_INFO("MMIO at 0x%08x size is %u kiB\n",
		 dev_priv->mmio_start, dev_priv->mmio_size / 1024);

	ret = vmw_ttm_global_init(dev_priv);
	if (unlikely(ret != 0))
		goto out_err0;


	vmw_master_init(&dev_priv->fbdev_master);
	ttm_lock_set_kill(&dev_priv->fbdev_master.lock, false, SIGTERM);
	dev_priv->active_master = &dev_priv->fbdev_master;

	dev_priv->mmio_virt = memremap(dev_priv->mmio_start,
				       dev_priv->mmio_size, MEMREMAP_WB);

	if (unlikely(dev_priv->mmio_virt == NULL)) {
		ret = -ENOMEM;
		DRM_ERROR("Failed mapping MMIO.\n");
		goto out_err3;
	}

	/* Need mmio memory to check for fifo pitchlock cap. */
	if (!(dev_priv->capabilities & SVGA_CAP_DISPLAY_TOPOLOGY) &&
	    !(dev_priv->capabilities & SVGA_CAP_PITCHLOCK) &&
	    !vmw_fifo_have_pitchlock(dev_priv)) {
		ret = -ENOSYS;
		DRM_ERROR("Hardware has no pitchlock\n");
		goto out_err4;
	}

	dev_priv->tdev = ttm_object_device_init
		(dev_priv->mem_global_ref.object, 12, &vmw_prime_dmabuf_ops);

	if (unlikely(dev_priv->tdev == NULL)) {
		DRM_ERROR("Unable to initialize TTM object management.\n");
		ret = -ENOMEM;
		goto out_err4;
	}

	dev->dev_private = dev_priv;

	ret = pci_request_regions(dev->pdev, "vmwgfx probe");
	dev_priv->stealth = (ret != 0);
	if (dev_priv->stealth) {
		/**
		 * Request at least the mmio PCI resource.
		 */

		DRM_INFO("It appears like vesafb is loaded. "
			 "Ignore above error if any.\n");
		ret = pci_request_region(dev->pdev, 2, "vmwgfx stealth probe");
		if (unlikely(ret != 0)) {
			DRM_ERROR("Failed reserving the SVGA MMIO resource.\n");
			goto out_no_device;
		}
	}

	if (dev_priv->capabilities & SVGA_CAP_IRQMASK) {
		ret = vmw_irq_install(dev, dev->pdev->irq);
		if (ret != 0) {
			DRM_ERROR("Failed installing irq: %d\n", ret);
			goto out_no_irq;
		}
	}

	dev_priv->fman = vmw_fence_manager_init(dev_priv);
	if (unlikely(dev_priv->fman == NULL)) {
		ret = -ENOMEM;
		goto out_no_fman;
	}

	ret = ttm_bo_device_init(&dev_priv->bdev,
				 dev_priv->bo_global_ref.ref.object,
				 &vmw_bo_driver,
				 dev->anon_inode->i_mapping,
				 VMWGFX_FILE_PAGE_OFFSET,
				 false);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed initializing TTM buffer object driver.\n");
		goto out_no_bdev;
	}

	/*
	 * Enable VRAM, but initially don't use it until SVGA is enabled and
	 * unhidden.
	 */
	ret = ttm_bo_init_mm(&dev_priv->bdev, TTM_PL_VRAM,
			     (dev_priv->vram_size >> PAGE_SHIFT));
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed initializing memory manager for VRAM.\n");
		goto out_no_vram;
	}
	dev_priv->bdev.man[TTM_PL_VRAM].use_type = false;

	dev_priv->has_gmr = true;
	if (((dev_priv->capabilities & (SVGA_CAP_GMR | SVGA_CAP_GMR2)) == 0) ||
	    refuse_dma || ttm_bo_init_mm(&dev_priv->bdev, VMW_PL_GMR,
					 VMW_PL_GMR) != 0) {
		DRM_INFO("No GMR memory available. "
			 "Graphics memory resources are very limited.\n");
		dev_priv->has_gmr = false;
	}

	if (dev_priv->capabilities & SVGA_CAP_GBOBJECTS) {
		dev_priv->has_mob = true;
		if (ttm_bo_init_mm(&dev_priv->bdev, VMW_PL_MOB,
				   VMW_PL_MOB) != 0) {
			DRM_INFO("No MOB memory available. "
				 "3D will be disabled.\n");
			dev_priv->has_mob = false;
		}
	}

	if (dev_priv->has_mob) {
		spin_lock(&dev_priv->cap_lock);
		vmw_write(dev_priv, SVGA_REG_DEV_CAP, SVGA3D_DEVCAP_DX);
		dev_priv->has_dx = !!vmw_read(dev_priv, SVGA_REG_DEV_CAP);
		spin_unlock(&dev_priv->cap_lock);
	}


	ret = vmw_kms_init(dev_priv);
	if (unlikely(ret != 0))
		goto out_no_kms;
	vmw_overlay_init(dev_priv);

	ret = vmw_request_device(dev_priv);
	if (ret)
		goto out_no_fifo;

	DRM_INFO("DX: %s\n", dev_priv->has_dx ? "yes." : "no.");
	DRM_INFO("Atomic: %s\n",
		 (dev->driver->driver_features & DRIVER_ATOMIC) ? "yes" : "no");

	snprintf(host_log, sizeof(host_log), "vmwgfx: %s-%s",
		VMWGFX_REPO, VMWGFX_GIT_VERSION);
	vmw_host_log(host_log);

	memset(host_log, 0, sizeof(host_log));
	snprintf(host_log, sizeof(host_log), "vmwgfx: Module Version: %d.%d.%d",
		VMWGFX_DRIVER_MAJOR, VMWGFX_DRIVER_MINOR,
		VMWGFX_DRIVER_PATCHLEVEL);
	vmw_host_log(host_log);

	if (dev_priv->enable_fb) {
		vmw_fifo_resource_inc(dev_priv);
		vmw_svga_enable(dev_priv);
		vmw_fb_init(dev_priv);
	}

	dev_priv->pm_nb.notifier_call = vmwgfx_pm_notifier;
	register_pm_notifier(&dev_priv->pm_nb);

	return 0;

out_no_fifo:
	vmw_overlay_close(dev_priv);
	vmw_kms_close(dev_priv);
out_no_kms:
	if (dev_priv->has_mob)
		(void) ttm_bo_clean_mm(&dev_priv->bdev, VMW_PL_MOB);
	if (dev_priv->has_gmr)
		(void) ttm_bo_clean_mm(&dev_priv->bdev, VMW_PL_GMR);
	(void)ttm_bo_clean_mm(&dev_priv->bdev, TTM_PL_VRAM);
out_no_vram:
	(void)ttm_bo_device_release(&dev_priv->bdev);
out_no_bdev:
	vmw_fence_manager_takedown(dev_priv->fman);
out_no_fman:
	if (dev_priv->capabilities & SVGA_CAP_IRQMASK)
		vmw_irq_uninstall(dev_priv->dev);
out_no_irq:
	if (dev_priv->stealth)
		pci_release_region(dev->pdev, 2);
	else
		pci_release_regions(dev->pdev);
out_no_device:
	ttm_object_device_release(&dev_priv->tdev);
out_err4:
	memunmap(dev_priv->mmio_virt);
out_err3:
	vmw_ttm_global_release(dev_priv);
out_err0:
	for (i = vmw_res_context; i < vmw_res_max; ++i)
		idr_destroy(&dev_priv->res_idr[i]);

	if (dev_priv->ctx.staged_bindings)
		vmw_binding_state_free(dev_priv->ctx.staged_bindings);
	kfree(dev_priv);
	return ret;
}

static void vmw_driver_unload(struct drm_device *dev)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	enum vmw_res_type i;

	unregister_pm_notifier(&dev_priv->pm_nb);

	if (dev_priv->ctx.res_ht_initialized)
		drm_ht_remove(&dev_priv->ctx.res_ht);
	vfree(dev_priv->ctx.cmd_bounce);
	if (dev_priv->enable_fb) {
		vmw_fb_off(dev_priv);
		vmw_fb_close(dev_priv);
		vmw_fifo_resource_dec(dev_priv);
		vmw_svga_disable(dev_priv);
	}

	vmw_kms_close(dev_priv);
	vmw_overlay_close(dev_priv);

	if (dev_priv->has_gmr)
		(void)ttm_bo_clean_mm(&dev_priv->bdev, VMW_PL_GMR);
	(void)ttm_bo_clean_mm(&dev_priv->bdev, TTM_PL_VRAM);

	vmw_release_device_early(dev_priv);
	if (dev_priv->has_mob)
		(void) ttm_bo_clean_mm(&dev_priv->bdev, VMW_PL_MOB);
	(void) ttm_bo_device_release(&dev_priv->bdev);
	vmw_release_device_late(dev_priv);
	vmw_fence_manager_takedown(dev_priv->fman);
	if (dev_priv->capabilities & SVGA_CAP_IRQMASK)
		vmw_irq_uninstall(dev_priv->dev);
	if (dev_priv->stealth)
		pci_release_region(dev->pdev, 2);
	else
		pci_release_regions(dev->pdev);

	ttm_object_device_release(&dev_priv->tdev);
	memunmap(dev_priv->mmio_virt);
	if (dev_priv->ctx.staged_bindings)
		vmw_binding_state_free(dev_priv->ctx.staged_bindings);
	vmw_ttm_global_release(dev_priv);

	for (i = vmw_res_context; i < vmw_res_max; ++i)
		idr_destroy(&dev_priv->res_idr[i]);

	kfree(dev_priv);
}

static void vmw_postclose(struct drm_device *dev,
			 struct drm_file *file_priv)
{
	struct vmw_fpriv *vmw_fp;

	vmw_fp = vmw_fpriv(file_priv);

	if (vmw_fp->locked_master) {
		struct vmw_master *vmaster =
			vmw_master(vmw_fp->locked_master);

		ttm_lock_set_kill(&vmaster->lock, true, SIGTERM);
		ttm_vt_unlock(&vmaster->lock);
		drm_master_put(&vmw_fp->locked_master);
	}

	ttm_object_file_release(&vmw_fp->tfile);
	kfree(vmw_fp);
}

static int vmw_driver_open(struct drm_device *dev, struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_fpriv *vmw_fp;
	int ret = -ENOMEM;

	vmw_fp = kzalloc(sizeof(*vmw_fp), GFP_KERNEL);
	if (unlikely(!vmw_fp))
		return ret;

	vmw_fp->tfile = ttm_object_file_init(dev_priv->tdev, 10);
	if (unlikely(vmw_fp->tfile == NULL))
		goto out_no_tfile;

	file_priv->driver_priv = vmw_fp;

	return 0;

out_no_tfile:
	kfree(vmw_fp);
	return ret;
}

static struct vmw_master *vmw_master_check(struct drm_device *dev,
					   struct drm_file *file_priv,
					   unsigned int flags)
{
	int ret;
	struct vmw_fpriv *vmw_fp = vmw_fpriv(file_priv);
	struct vmw_master *vmaster;

	if (!drm_is_primary_client(file_priv) || !(flags & DRM_AUTH))
		return NULL;

	ret = mutex_lock_interruptible(&dev->master_mutex);
	if (unlikely(ret != 0))
		return ERR_PTR(-ERESTARTSYS);

	if (drm_is_current_master(file_priv)) {
		mutex_unlock(&dev->master_mutex);
		return NULL;
	}

	/*
	 * Check if we were previously master, but now dropped. In that
	 * case, allow at least render node functionality.
	 */
	if (vmw_fp->locked_master) {
		mutex_unlock(&dev->master_mutex);

		if (flags & DRM_RENDER_ALLOW)
			return NULL;

		DRM_ERROR("Dropped master trying to access ioctl that "
			  "requires authentication.\n");
		return ERR_PTR(-EACCES);
	}
	mutex_unlock(&dev->master_mutex);

	/*
	 * Take the TTM lock. Possibly sleep waiting for the authenticating
	 * master to become master again, or for a SIGTERM if the
	 * authenticating master exits.
	 */
	vmaster = vmw_master(file_priv->master);
	ret = ttm_read_lock(&vmaster->lock, true);
	if (unlikely(ret != 0))
		vmaster = ERR_PTR(ret);

	return vmaster;
}

static long vmw_generic_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg,
			      long (*ioctl_func)(struct file *, unsigned int,
						 unsigned long))
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	unsigned int nr = DRM_IOCTL_NR(cmd);
	struct vmw_master *vmaster;
	unsigned int flags;
	long ret;

	/*
	 * Do extra checking on driver private ioctls.
	 */

	if ((nr >= DRM_COMMAND_BASE) && (nr < DRM_COMMAND_END)
	    && (nr < DRM_COMMAND_BASE + dev->driver->num_ioctls)) {
		const struct drm_ioctl_desc *ioctl =
			&vmw_ioctls[nr - DRM_COMMAND_BASE];

		if (nr == DRM_COMMAND_BASE + DRM_VMW_EXECBUF) {
			ret = (long) drm_ioctl_permit(ioctl->flags, file_priv);
			if (unlikely(ret != 0))
				return ret;

			if (unlikely((cmd & (IOC_IN | IOC_OUT)) != IOC_IN))
				goto out_io_encoding;

			return (long) vmw_execbuf_ioctl(dev, arg, file_priv,
							_IOC_SIZE(cmd));
		} else if (nr == DRM_COMMAND_BASE + DRM_VMW_UPDATE_LAYOUT) {
			if (!drm_is_current_master(file_priv) &&
			    !capable(CAP_SYS_ADMIN))
				return -EACCES;
		}

		if (unlikely(ioctl->cmd != cmd))
			goto out_io_encoding;

		flags = ioctl->flags;
	} else if (!drm_ioctl_flags(nr, &flags))
		return -EINVAL;

	vmaster = vmw_master_check(dev, file_priv, flags);
	if (IS_ERR(vmaster)) {
		ret = PTR_ERR(vmaster);

		if (ret != -ERESTARTSYS)
			DRM_INFO("IOCTL ERROR Command %d, Error %ld.\n",
				 nr, ret);
		return ret;
	}

	ret = ioctl_func(filp, cmd, arg);
	if (vmaster)
		ttm_read_unlock(&vmaster->lock);

	return ret;

out_io_encoding:
	DRM_ERROR("Invalid command format, ioctl %d\n",
		  nr - DRM_COMMAND_BASE);

	return -EINVAL;
}

static long vmw_unlocked_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	return vmw_generic_ioctl(filp, cmd, arg, &drm_ioctl);
}

#ifdef CONFIG_COMPAT
static long vmw_compat_ioctl(struct file *filp, unsigned int cmd,
			     unsigned long arg)
{
	return vmw_generic_ioctl(filp, cmd, arg, &drm_compat_ioctl);
}
#endif

static void vmw_lastclose(struct drm_device *dev)
{
}

static void vmw_master_init(struct vmw_master *vmaster)
{
	ttm_lock_init(&vmaster->lock);
}

static int vmw_master_create(struct drm_device *dev,
			     struct drm_master *master)
{
	struct vmw_master *vmaster;

	vmaster = kzalloc(sizeof(*vmaster), GFP_KERNEL);
	if (unlikely(!vmaster))
		return -ENOMEM;

	vmw_master_init(vmaster);
	ttm_lock_set_kill(&vmaster->lock, true, SIGTERM);
	master->driver_priv = vmaster;

	return 0;
}

static void vmw_master_destroy(struct drm_device *dev,
			       struct drm_master *master)
{
	struct vmw_master *vmaster = vmw_master(master);

	master->driver_priv = NULL;
	kfree(vmaster);
}

static int vmw_master_set(struct drm_device *dev,
			  struct drm_file *file_priv,
			  bool from_open)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_fpriv *vmw_fp = vmw_fpriv(file_priv);
	struct vmw_master *active = dev_priv->active_master;
	struct vmw_master *vmaster = vmw_master(file_priv->master);
	int ret = 0;

	if (active) {
		BUG_ON(active != &dev_priv->fbdev_master);
		ret = ttm_vt_lock(&active->lock, false, vmw_fp->tfile);
		if (unlikely(ret != 0))
			return ret;

		ttm_lock_set_kill(&active->lock, true, SIGTERM);
		dev_priv->active_master = NULL;
	}

	ttm_lock_set_kill(&vmaster->lock, false, SIGTERM);
	if (!from_open) {
		ttm_vt_unlock(&vmaster->lock);
		BUG_ON(vmw_fp->locked_master != file_priv->master);
		drm_master_put(&vmw_fp->locked_master);
	}

	dev_priv->active_master = vmaster;
	drm_sysfs_hotplug_event(dev);

	return 0;
}

static void vmw_master_drop(struct drm_device *dev,
			    struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_fpriv *vmw_fp = vmw_fpriv(file_priv);
	struct vmw_master *vmaster = vmw_master(file_priv->master);
	int ret;

	/**
	 * Make sure the master doesn't disappear while we have
	 * it locked.
	 */

	vmw_fp->locked_master = drm_master_get(file_priv->master);
	ret = ttm_vt_lock(&vmaster->lock, false, vmw_fp->tfile);
	vmw_kms_legacy_hotspot_clear(dev_priv);
	if (unlikely((ret != 0))) {
		DRM_ERROR("Unable to lock TTM at VT switch.\n");
		drm_master_put(&vmw_fp->locked_master);
	}

	ttm_lock_set_kill(&vmaster->lock, false, SIGTERM);

	if (!dev_priv->enable_fb)
		vmw_svga_disable(dev_priv);

	dev_priv->active_master = &dev_priv->fbdev_master;
	ttm_lock_set_kill(&dev_priv->fbdev_master.lock, false, SIGTERM);
	ttm_vt_unlock(&dev_priv->fbdev_master.lock);

	if (dev_priv->enable_fb)
		vmw_fb_on(dev_priv);
}

/**
 * __vmw_svga_enable - Enable SVGA mode, FIFO and use of VRAM.
 *
 * @dev_priv: Pointer to device private struct.
 * Needs the reservation sem to be held in non-exclusive mode.
 */
static void __vmw_svga_enable(struct vmw_private *dev_priv)
{
	spin_lock(&dev_priv->svga_lock);
	if (!dev_priv->bdev.man[TTM_PL_VRAM].use_type) {
		vmw_write(dev_priv, SVGA_REG_ENABLE, SVGA_REG_ENABLE);
		dev_priv->bdev.man[TTM_PL_VRAM].use_type = true;
	}
	spin_unlock(&dev_priv->svga_lock);
}

/**
 * vmw_svga_enable - Enable SVGA mode, FIFO and use of VRAM.
 *
 * @dev_priv: Pointer to device private struct.
 */
void vmw_svga_enable(struct vmw_private *dev_priv)
{
	(void) ttm_read_lock(&dev_priv->reservation_sem, false);
	__vmw_svga_enable(dev_priv);
	ttm_read_unlock(&dev_priv->reservation_sem);
}

/**
 * __vmw_svga_disable - Disable SVGA mode and use of VRAM.
 *
 * @dev_priv: Pointer to device private struct.
 * Needs the reservation sem to be held in exclusive mode.
 * Will not empty VRAM. VRAM must be emptied by caller.
 */
static void __vmw_svga_disable(struct vmw_private *dev_priv)
{
	spin_lock(&dev_priv->svga_lock);
	if (dev_priv->bdev.man[TTM_PL_VRAM].use_type) {
		dev_priv->bdev.man[TTM_PL_VRAM].use_type = false;
		vmw_write(dev_priv, SVGA_REG_ENABLE,
			  SVGA_REG_ENABLE_HIDE |
			  SVGA_REG_ENABLE_ENABLE);
	}
	spin_unlock(&dev_priv->svga_lock);
}

/**
 * vmw_svga_disable - Disable SVGA_MODE, and use of VRAM. Keep the fifo
 * running.
 *
 * @dev_priv: Pointer to device private struct.
 * Will empty VRAM.
 */
void vmw_svga_disable(struct vmw_private *dev_priv)
{
	ttm_write_lock(&dev_priv->reservation_sem, false);
	spin_lock(&dev_priv->svga_lock);
	if (dev_priv->bdev.man[TTM_PL_VRAM].use_type) {
		dev_priv->bdev.man[TTM_PL_VRAM].use_type = false;
		spin_unlock(&dev_priv->svga_lock);
		if (ttm_bo_evict_mm(&dev_priv->bdev, TTM_PL_VRAM))
			DRM_ERROR("Failed evicting VRAM buffers.\n");
		vmw_write(dev_priv, SVGA_REG_ENABLE,
			  SVGA_REG_ENABLE_HIDE |
			  SVGA_REG_ENABLE_ENABLE);
	} else
		spin_unlock(&dev_priv->svga_lock);
	ttm_write_unlock(&dev_priv->reservation_sem);
}

static void vmw_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	pci_disable_device(pdev);
	drm_put_dev(dev);
}

static int vmwgfx_pm_notifier(struct notifier_block *nb, unsigned long val,
			      void *ptr)
{
	struct vmw_private *dev_priv =
		container_of(nb, struct vmw_private, pm_nb);

	switch (val) {
	case PM_HIBERNATION_PREPARE:
		if (dev_priv->enable_fb)
			vmw_fb_off(dev_priv);
		ttm_suspend_lock(&dev_priv->reservation_sem);

		/*
		 * This empties VRAM and unbinds all GMR bindings.
		 * Buffer contents is moved to swappable memory.
		 */
		vmw_execbuf_release_pinned_bo(dev_priv);
		vmw_resource_evict_all(dev_priv);
		vmw_release_device_early(dev_priv);
		ttm_bo_swapout_all(&dev_priv->bdev);
		vmw_fence_fifo_down(dev_priv->fman);
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
		vmw_fence_fifo_up(dev_priv->fman);
		ttm_suspend_unlock(&dev_priv->reservation_sem);
		if (dev_priv->enable_fb)
			vmw_fb_on(dev_priv);
		break;
	case PM_RESTORE_PREPARE:
		break;
	default:
		break;
	}
	return 0;
}

static int vmw_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct vmw_private *dev_priv = vmw_priv(dev);

	if (dev_priv->refuse_hibernation)
		return -EBUSY;

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);
	return 0;
}

static int vmw_pci_resume(struct pci_dev *pdev)
{
	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	return pci_enable_device(pdev);
}

static int vmw_pm_suspend(struct device *kdev)
{
	struct pci_dev *pdev = to_pci_dev(kdev);
	struct pm_message dummy;

	dummy.event = 0;

	return vmw_pci_suspend(pdev, dummy);
}

static int vmw_pm_resume(struct device *kdev)
{
	struct pci_dev *pdev = to_pci_dev(kdev);

	return vmw_pci_resume(pdev);
}

static int vmw_pm_freeze(struct device *kdev)
{
	struct pci_dev *pdev = to_pci_dev(kdev);
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct vmw_private *dev_priv = vmw_priv(dev);

	dev_priv->suspended = true;
	if (dev_priv->enable_fb)
		vmw_fifo_resource_dec(dev_priv);

	if (atomic_read(&dev_priv->num_fifo_resources) != 0) {
		DRM_ERROR("Can't hibernate while 3D resources are active.\n");
		if (dev_priv->enable_fb)
			vmw_fifo_resource_inc(dev_priv);
		WARN_ON(vmw_request_device_late(dev_priv));
		dev_priv->suspended = false;
		return -EBUSY;
	}

	if (dev_priv->enable_fb)
		__vmw_svga_disable(dev_priv);
	
	vmw_release_device_late(dev_priv);

	return 0;
}

static int vmw_pm_restore(struct device *kdev)
{
	struct pci_dev *pdev = to_pci_dev(kdev);
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct vmw_private *dev_priv = vmw_priv(dev);
	int ret;

	vmw_write(dev_priv, SVGA_REG_ID, SVGA_ID_2);
	(void) vmw_read(dev_priv, SVGA_REG_ID);

	if (dev_priv->enable_fb)
		vmw_fifo_resource_inc(dev_priv);

	ret = vmw_request_device(dev_priv);
	if (ret)
		return ret;

	if (dev_priv->enable_fb)
		__vmw_svga_enable(dev_priv);

	dev_priv->suspended = false;

	return 0;
}

static const struct dev_pm_ops vmw_pm_ops = {
	.freeze = vmw_pm_freeze,
	.thaw = vmw_pm_restore,
	.restore = vmw_pm_restore,
	.suspend = vmw_pm_suspend,
	.resume = vmw_pm_resume,
};

static const struct file_operations vmwgfx_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = vmw_unlocked_ioctl,
	.mmap = vmw_mmap,
	.poll = vmw_fops_poll,
	.read = vmw_fops_read,
#if defined(CONFIG_COMPAT)
	.compat_ioctl = vmw_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static struct drm_driver driver = {
	.driver_features = DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED |
	DRIVER_MODESET | DRIVER_PRIME | DRIVER_RENDER | DRIVER_ATOMIC,
	.load = vmw_driver_load,
	.unload = vmw_driver_unload,
	.lastclose = vmw_lastclose,
	.get_vblank_counter = vmw_get_vblank_counter,
	.enable_vblank = vmw_enable_vblank,
	.disable_vblank = vmw_disable_vblank,
	.ioctls = vmw_ioctls,
	.num_ioctls = ARRAY_SIZE(vmw_ioctls),
	.master_create = vmw_master_create,
	.master_destroy = vmw_master_destroy,
	.master_set = vmw_master_set,
	.master_drop = vmw_master_drop,
	.open = vmw_driver_open,
	.postclose = vmw_postclose,

	.dumb_create = vmw_dumb_create,
	.dumb_map_offset = vmw_dumb_map_offset,
	.dumb_destroy = vmw_dumb_destroy,

	.prime_fd_to_handle = vmw_prime_fd_to_handle,
	.prime_handle_to_fd = vmw_prime_handle_to_fd,

	.fops = &vmwgfx_driver_fops,
	.name = VMWGFX_DRIVER_NAME,
	.desc = VMWGFX_DRIVER_DESC,
	.date = VMWGFX_DRIVER_DATE,
	.major = VMWGFX_DRIVER_MAJOR,
	.minor = VMWGFX_DRIVER_MINOR,
	.patchlevel = VMWGFX_DRIVER_PATCHLEVEL
};

static struct pci_driver vmw_pci_driver = {
	.name = VMWGFX_DRIVER_NAME,
	.id_table = vmw_pci_id_list,
	.probe = vmw_probe,
	.remove = vmw_remove,
	.driver = {
		.pm = &vmw_pm_ops
	}
};

static int vmw_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	return drm_get_pci_dev(pdev, ent, &driver);
}

static int __init vmwgfx_init(void)
{
	int ret;

	if (vgacon_text_force())
		return -EINVAL;

	ret = pci_register_driver(&vmw_pci_driver);
	if (ret)
		DRM_ERROR("Failed initializing DRM.\n");
	return ret;
}

static void __exit vmwgfx_exit(void)
{
	pci_unregister_driver(&vmw_pci_driver);
}

module_init(vmwgfx_init);
module_exit(vmwgfx_exit);

MODULE_AUTHOR("VMware Inc. and others");
MODULE_DESCRIPTION("Standalone drm driver for the VMware SVGA device");
MODULE_LICENSE("GPL and additional rights");
MODULE_VERSION(__stringify(VMWGFX_DRIVER_MAJOR) "."
	       __stringify(VMWGFX_DRIVER_MINOR) "."
	       __stringify(VMWGFX_DRIVER_PATCHLEVEL) "."
	       "0");
