// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2009-2016 VMware, Inc., Palo Alto, CA., USA
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

#include <linux/console.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/mem_encrypt.h>

#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_sysfs.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>

#include "ttm_object.h"
#include "vmwgfx_binding.h"
#include "vmwgfx_drv.h"

#define VMWGFX_DRIVER_DESC "Linux drm driver for VMware graphics devices"

#define VMW_MIN_INITIAL_WIDTH 800
#define VMW_MIN_INITIAL_HEIGHT 600

#define VMWGFX_VALIDATION_MEM_GRAN (16*PAGE_SIZE)


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
#define DRM_IOCTL_VMW_GB_SURFACE_CREATE_EXT				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VMW_GB_SURFACE_CREATE_EXT,	\
		union drm_vmw_gb_surface_create_ext_arg)
#define DRM_IOCTL_VMW_GB_SURFACE_REF_EXT				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VMW_GB_SURFACE_REF_EXT,		\
		union drm_vmw_gb_surface_reference_ext_arg)
#define DRM_IOCTL_VMW_MSG						\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VMW_MSG,			\
		struct drm_vmw_msg_arg)

/*
 * The core DRM version of this macro doesn't account for
 * DRM_COMMAND_BASE.
 */

#define VMW_IOCTL_DEF(ioctl, func, flags) \
  [DRM_IOCTL_NR(DRM_IOCTL_##ioctl) - DRM_COMMAND_BASE] = {DRM_IOCTL_##ioctl, flags, func}

/*
 * Ioctl definitions.
 */

static const struct drm_ioctl_desc vmw_ioctls[] = {
	VMW_IOCTL_DEF(VMW_GET_PARAM, vmw_getparam_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_ALLOC_DMABUF, vmw_bo_alloc_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_UNREF_DMABUF, vmw_bo_unref_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_CURSOR_BYPASS,
		      vmw_kms_cursor_bypass_ioctl,
		      DRM_MASTER),

	VMW_IOCTL_DEF(VMW_CONTROL_STREAM, vmw_overlay_ioctl,
		      DRM_MASTER),
	VMW_IOCTL_DEF(VMW_CLAIM_STREAM, vmw_stream_claim_ioctl,
		      DRM_MASTER),
	VMW_IOCTL_DEF(VMW_UNREF_STREAM, vmw_stream_unref_ioctl,
		      DRM_MASTER),

	VMW_IOCTL_DEF(VMW_CREATE_CONTEXT, vmw_context_define_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_UNREF_CONTEXT, vmw_context_destroy_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_CREATE_SURFACE, vmw_surface_define_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_UNREF_SURFACE, vmw_surface_destroy_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_REF_SURFACE, vmw_surface_reference_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_EXECBUF, vmw_execbuf_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_FENCE_WAIT, vmw_fence_obj_wait_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_FENCE_SIGNALED,
		      vmw_fence_obj_signaled_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_FENCE_UNREF, vmw_fence_obj_unref_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_FENCE_EVENT, vmw_fence_event_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_GET_3D_CAP, vmw_get_cap_3d_ioctl,
		      DRM_RENDER_ALLOW),

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
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_UNREF_SHADER,
		      vmw_shader_destroy_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_GB_SURFACE_CREATE,
		      vmw_gb_surface_define_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_GB_SURFACE_REF,
		      vmw_gb_surface_reference_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_SYNCCPU,
		      vmw_user_bo_synccpu_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_CREATE_EXTENDED_CONTEXT,
		      vmw_extended_context_define_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_GB_SURFACE_CREATE_EXT,
		      vmw_gb_surface_define_ext_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_GB_SURFACE_REF_EXT,
		      vmw_gb_surface_reference_ext_ioctl,
		      DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_MSG,
		      vmw_msg_ioctl,
		      DRM_RENDER_ALLOW),
};

static const struct pci_device_id vmw_pci_id_list[] = {
	{ PCI_DEVICE(0x15ad, VMWGFX_PCI_ID_SVGA2) },
	{ }
};
MODULE_DEVICE_TABLE(pci, vmw_pci_id_list);

static int enable_fbdev = IS_ENABLED(CONFIG_DRM_VMWGFX_FBCON);
static int vmw_force_iommu;
static int vmw_restrict_iommu;
static int vmw_force_coherent;
static int vmw_restrict_dma_mask;
static int vmw_assume_16bpp;

static int vmw_probe(struct pci_dev *, const struct pci_device_id *);
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


static void vmw_print_capabilities2(uint32_t capabilities2)
{
	DRM_INFO("Capabilities2:\n");
	if (capabilities2 & SVGA_CAP2_GROW_OTABLE)
		DRM_INFO("  Grow oTable.\n");
	if (capabilities2 & SVGA_CAP2_INTRA_SURFACE_COPY)
		DRM_INFO("  IntraSurface copy.\n");
	if (capabilities2 & SVGA_CAP2_DX3)
		DRM_INFO("  DX3.\n");
}

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
	if (capabilities & SVGA_CAP_HP_CMD_QUEUE)
		DRM_INFO("  HP Command Queue.\n");
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
	struct vmw_buffer_object *vbo;
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

	ret = vmw_bo_init(dev_priv, vbo, PAGE_SIZE,
			  &vmw_sys_placement, false, true,
			  &vmw_bo_bo_free);
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
		vmw_bo_unreference(&vbo);
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
		ret = vmw_cmdbuf_set_pool_size(dev_priv->cman, 256*4096);
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
		dev_priv->sm_type = VMW_SM_LEGACY;
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
		struct ttm_resource_manager *man;

		man = ttm_manager_type(&dev_priv->bdev, VMW_PL_MOB);
		ttm_resource_manager_evict_all(&dev_priv->bdev, man);
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

	vmw_bo_unreference(&dev_priv->dummy_query_bo);
	if (dev_priv->cman)
		vmw_cmdbuf_remove_pool(dev_priv->cman);

	if (dev_priv->has_mob) {
		struct ttm_resource_manager *man;

		man = ttm_manager_type(&dev_priv->bdev, VMW_PL_MOB);
		ttm_resource_manager_evict_all(&dev_priv->bdev, man);
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

/*
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
 * This functions tries to determine what actions need to be taken by the
 * driver to make system pages visible to the device.
 * If this function decides that DMA is not possible, it returns -EINVAL.
 * The driver may then try to disable features of the device that require
 * DMA.
 */
static int vmw_dma_select_mode(struct vmw_private *dev_priv)
{
	static const char *names[vmw_dma_map_max] = {
		[vmw_dma_phys] = "Using physical TTM page addresses.",
		[vmw_dma_alloc_coherent] = "Using coherent TTM pages.",
		[vmw_dma_map_populate] = "Caching DMA mappings.",
		[vmw_dma_map_bind] = "Giving up DMA mappings early."};

	/* TTM currently doesn't fully support SEV encryption. */
	if (mem_encrypt_active())
		return -EINVAL;

	if (vmw_force_coherent)
		dev_priv->map_mode = vmw_dma_alloc_coherent;
	else if (vmw_restrict_iommu)
		dev_priv->map_mode = vmw_dma_map_bind;
	else
		dev_priv->map_mode = vmw_dma_map_populate;

	DRM_INFO("DMA map mode: %s\n", names[dev_priv->map_mode]);
	return 0;
}

/**
 * vmw_dma_masks - set required page- and dma masks
 *
 * @dev_priv: Pointer to struct drm-device
 *
 * With 32-bit we can only handle 32 bit PFNs. Optionally set that
 * restriction also for 64-bit systems.
 */
static int vmw_dma_masks(struct vmw_private *dev_priv)
{
	struct drm_device *dev = &dev_priv->drm;
	int ret = 0;

	ret = dma_set_mask_and_coherent(dev->dev, DMA_BIT_MASK(64));
	if (dev_priv->map_mode != vmw_dma_phys &&
	    (sizeof(unsigned long) == 4 || vmw_restrict_dma_mask)) {
		DRM_INFO("Restricting DMA addresses to 44 bits.\n");
		return dma_set_mask_and_coherent(dev->dev, DMA_BIT_MASK(44));
	}

	return ret;
}

static int vmw_vram_manager_init(struct vmw_private *dev_priv)
{
	int ret;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	ret = vmw_thp_init(dev_priv);
#else
	ret = ttm_range_man_init(&dev_priv->bdev, TTM_PL_VRAM, false,
				 dev_priv->vram_size >> PAGE_SHIFT);
#endif
	ttm_resource_manager_set_used(ttm_manager_type(&dev_priv->bdev, TTM_PL_VRAM), false);
	return ret;
}

static void vmw_vram_manager_fini(struct vmw_private *dev_priv)
{
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	vmw_thp_fini(dev_priv);
#else
	ttm_range_man_fini(&dev_priv->bdev, TTM_PL_VRAM);
#endif
}

static int vmw_setup_pci_resources(struct vmw_private *dev,
				   unsigned long pci_id)
{
	resource_size_t fifo_start;
	resource_size_t fifo_size;
	int ret;
	struct pci_dev *pdev = to_pci_dev(dev->drm.dev);

	pci_set_master(pdev);

	ret = pci_request_regions(pdev, "vmwgfx probe");
	if (ret)
		return ret;

	dev->io_start = pci_resource_start(pdev, 0);
	dev->vram_start = pci_resource_start(pdev, 1);
	dev->vram_size = pci_resource_len(pdev, 1);
	fifo_start = pci_resource_start(pdev, 2);
	fifo_size = pci_resource_len(pdev, 2);

	DRM_INFO("FIFO at %pa size is %llu kiB\n",
		 &fifo_start, (uint64_t)fifo_size / 1024);
	dev->fifo_mem = devm_memremap(dev->drm.dev,
				      fifo_start,
				      fifo_size,
				      MEMREMAP_WB);

	if (IS_ERR(dev->fifo_mem)) {
		DRM_ERROR("Failed mapping FIFO memory.\n");
		pci_release_regions(pdev);
		return PTR_ERR(dev->fifo_mem);
	}

	/*
	 * This is approximate size of the vram, the exact size will only
	 * be known after we read SVGA_REG_VRAM_SIZE. The PCI resource
	 * size will be equal to or bigger than the size reported by
	 * SVGA_REG_VRAM_SIZE.
	 */
	DRM_INFO("VRAM at %pa size is %llu kiB\n",
		 &dev->vram_start, (uint64_t)dev->vram_size / 1024);

	return 0;
}

static int vmw_detect_version(struct vmw_private *dev)
{
	uint32_t svga_id;

	vmw_write(dev, SVGA_REG_ID, SVGA_ID_2);
	svga_id = vmw_read(dev, SVGA_REG_ID);
	if (svga_id != SVGA_ID_2) {
		DRM_ERROR("Unsupported SVGA ID 0x%x on chipset 0x%x\n",
			  svga_id, dev->vmw_chipset);
		return -ENOSYS;
	}
	return 0;
}

static int vmw_driver_load(struct vmw_private *dev_priv, u32 pci_id)
{
	int ret;
	enum vmw_res_type i;
	bool refuse_dma = false;
	char host_log[100] = {0};
	struct pci_dev *pdev = to_pci_dev(dev_priv->drm.dev);

	dev_priv->vmw_chipset = pci_id;
	dev_priv->last_read_seqno = (uint32_t) -100;
	dev_priv->drm.dev_private = dev_priv;

	ret = vmw_setup_pci_resources(dev_priv, pci_id);
	if (ret)
		return ret;
	ret = vmw_detect_version(dev_priv);
	if (ret)
		goto out_no_pci_or_version;

	mutex_init(&dev_priv->cmdbuf_mutex);
	mutex_init(&dev_priv->release_mutex);
	mutex_init(&dev_priv->binding_mutex);
	mutex_init(&dev_priv->global_kms_state_mutex);
	ttm_lock_init(&dev_priv->reservation_sem);
	spin_lock_init(&dev_priv->resource_lock);
	spin_lock_init(&dev_priv->hw_lock);
	spin_lock_init(&dev_priv->waiter_lock);
	spin_lock_init(&dev_priv->cap_lock);
	spin_lock_init(&dev_priv->cursor_lock);

	for (i = vmw_res_context; i < vmw_res_max; ++i) {
		idr_init(&dev_priv->res_idr[i]);
		INIT_LIST_HEAD(&dev_priv->res_lru[i]);
	}

	init_waitqueue_head(&dev_priv->fence_queue);
	init_waitqueue_head(&dev_priv->fifo_queue);
	dev_priv->fence_queue_waiters = 0;
	dev_priv->fifo_queue_waiters = 0;

	dev_priv->used_memory_size = 0;

	dev_priv->assume_16bpp = !!vmw_assume_16bpp;

	dev_priv->enable_fb = enable_fbdev;


	dev_priv->capabilities = vmw_read(dev_priv, SVGA_REG_CAPABILITIES);

	if (dev_priv->capabilities & SVGA_CAP_CAP2_REGISTER) {
		dev_priv->capabilities2 = vmw_read(dev_priv, SVGA_REG_CAP2);
	}


	ret = vmw_dma_select_mode(dev_priv);
	if (unlikely(ret != 0)) {
		DRM_INFO("Restricting capabilities since DMA not available.\n");
		refuse_dma = true;
		if (dev_priv->capabilities & SVGA_CAP_GBOBJECTS)
			DRM_INFO("Disabling 3D acceleration.\n");
	}

	dev_priv->vram_size = vmw_read(dev_priv, SVGA_REG_VRAM_SIZE);
	dev_priv->fifo_mem_size = vmw_read(dev_priv, SVGA_REG_MEM_SIZE);
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
		uint64_t mem_size;

		if (dev_priv->capabilities2 & SVGA_CAP2_GB_MEMSIZE_2)
			mem_size = vmw_read(dev_priv,
					    SVGA_REG_GBOBJECT_MEM_SIZE_KB);
		else
			mem_size =
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
	if (dev_priv->capabilities & SVGA_CAP_CAP2_REGISTER)
		vmw_print_capabilities2(dev_priv->capabilities2);

	ret = vmw_dma_masks(dev_priv);
	if (unlikely(ret != 0))
		goto out_err0;

	dma_set_max_seg_size(dev_priv->drm.dev, U32_MAX);

	if (dev_priv->capabilities & SVGA_CAP_GMR2) {
		DRM_INFO("Max GMR ids is %u\n",
			 (unsigned)dev_priv->max_gmr_ids);
		DRM_INFO("Max number of GMR pages is %u\n",
			 (unsigned)dev_priv->max_gmr_pages);
		DRM_INFO("Max dedicated hypervisor surface memory is %u kiB\n",
			 (unsigned)dev_priv->memory_size / 1024);
	}
	DRM_INFO("Maximum display memory size is %llu kiB\n",
		 (uint64_t)dev_priv->prim_bb_mem / 1024);

	/* Need mmio memory to check for fifo pitchlock cap. */
	if (!(dev_priv->capabilities & SVGA_CAP_DISPLAY_TOPOLOGY) &&
	    !(dev_priv->capabilities & SVGA_CAP_PITCHLOCK) &&
	    !vmw_fifo_have_pitchlock(dev_priv)) {
		ret = -ENOSYS;
		DRM_ERROR("Hardware has no pitchlock\n");
		goto out_err0;
	}

	dev_priv->tdev = ttm_object_device_init(&ttm_mem_glob, 12,
						&vmw_prime_dmabuf_ops);

	if (unlikely(dev_priv->tdev == NULL)) {
		DRM_ERROR("Unable to initialize TTM object management.\n");
		ret = -ENOMEM;
		goto out_err0;
	}

	if (dev_priv->capabilities & SVGA_CAP_IRQMASK) {
		ret = vmw_irq_install(&dev_priv->drm, pdev->irq);
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

	drm_vma_offset_manager_init(&dev_priv->vma_manager,
				    DRM_FILE_PAGE_OFFSET_START,
				    DRM_FILE_PAGE_OFFSET_SIZE);
	ret = ttm_device_init(&dev_priv->bdev, &vmw_bo_driver,
			      dev_priv->drm.dev,
			      dev_priv->drm.anon_inode->i_mapping,
			      &dev_priv->vma_manager,
			      dev_priv->map_mode == vmw_dma_alloc_coherent,
			      false);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed initializing TTM buffer object driver.\n");
		goto out_no_bdev;
	}

	/*
	 * Enable VRAM, but initially don't use it until SVGA is enabled and
	 * unhidden.
	 */

	ret = vmw_vram_manager_init(dev_priv);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed initializing memory manager for VRAM.\n");
		goto out_no_vram;
	}

	/*
	 * "Guest Memory Regions" is an aperture like feature with
	 *  one slot per bo. There is an upper limit of the number of
	 *  slots as well as the bo size.
	 */
	dev_priv->has_gmr = true;
	/* TODO: This is most likely not correct */
	if (((dev_priv->capabilities & (SVGA_CAP_GMR | SVGA_CAP_GMR2)) == 0) ||
	    refuse_dma ||
	    vmw_gmrid_man_init(dev_priv, VMW_PL_GMR) != 0) {
		DRM_INFO("No GMR memory available. "
			 "Graphics memory resources are very limited.\n");
		dev_priv->has_gmr = false;
	}

	if (dev_priv->capabilities & SVGA_CAP_GBOBJECTS && !refuse_dma) {
		dev_priv->has_mob = true;

		if (vmw_gmrid_man_init(dev_priv, VMW_PL_MOB) != 0) {
			DRM_INFO("No MOB memory available. "
				 "3D will be disabled.\n");
			dev_priv->has_mob = false;
		}
	}

	if (dev_priv->has_mob && (dev_priv->capabilities & SVGA_CAP_DX)) {
		spin_lock(&dev_priv->cap_lock);
		vmw_write(dev_priv, SVGA_REG_DEV_CAP, SVGA3D_DEVCAP_DXCONTEXT);
		if (vmw_read(dev_priv, SVGA_REG_DEV_CAP))
			dev_priv->sm_type = VMW_SM_4;
		spin_unlock(&dev_priv->cap_lock);
	}

	vmw_validation_mem_init_ttm(dev_priv, VMWGFX_VALIDATION_MEM_GRAN);

	/* SVGA_CAP2_DX2 (DefineGBSurface_v3) is needed for SM4_1 support */
	if (has_sm4_context(dev_priv) &&
	    (dev_priv->capabilities2 & SVGA_CAP2_DX2)) {
		vmw_write(dev_priv, SVGA_REG_DEV_CAP, SVGA3D_DEVCAP_SM41);

		if (vmw_read(dev_priv, SVGA_REG_DEV_CAP))
			dev_priv->sm_type = VMW_SM_4_1;

		if (has_sm4_1_context(dev_priv) &&
		    (dev_priv->capabilities2 & SVGA_CAP2_DX3)) {
			vmw_write(dev_priv, SVGA_REG_DEV_CAP, SVGA3D_DEVCAP_SM5);
			if (vmw_read(dev_priv, SVGA_REG_DEV_CAP))
				dev_priv->sm_type = VMW_SM_5;
		}
	}

	ret = vmw_kms_init(dev_priv);
	if (unlikely(ret != 0))
		goto out_no_kms;
	vmw_overlay_init(dev_priv);

	ret = vmw_request_device(dev_priv);
	if (ret)
		goto out_no_fifo;

	if (dev_priv->sm_type == VMW_SM_5)
		DRM_INFO("SM5 support available.\n");
	if (dev_priv->sm_type == VMW_SM_4_1)
		DRM_INFO("SM4_1 support available.\n");
	if (dev_priv->sm_type == VMW_SM_4)
		DRM_INFO("SM4 support available.\n");

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
		vmw_gmrid_man_fini(dev_priv, VMW_PL_MOB);
	if (dev_priv->has_gmr)
		vmw_gmrid_man_fini(dev_priv, VMW_PL_GMR);
	vmw_vram_manager_fini(dev_priv);
out_no_vram:
	ttm_device_fini(&dev_priv->bdev);
out_no_bdev:
	vmw_fence_manager_takedown(dev_priv->fman);
out_no_fman:
	if (dev_priv->capabilities & SVGA_CAP_IRQMASK)
		vmw_irq_uninstall(&dev_priv->drm);
out_no_irq:
	ttm_object_device_release(&dev_priv->tdev);
out_err0:
	for (i = vmw_res_context; i < vmw_res_max; ++i)
		idr_destroy(&dev_priv->res_idr[i]);

	if (dev_priv->ctx.staged_bindings)
		vmw_binding_state_free(dev_priv->ctx.staged_bindings);
out_no_pci_or_version:
	pci_release_regions(pdev);
	return ret;
}

static void vmw_driver_unload(struct drm_device *dev)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);
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
		vmw_gmrid_man_fini(dev_priv, VMW_PL_GMR);

	vmw_release_device_early(dev_priv);
	if (dev_priv->has_mob)
		vmw_gmrid_man_fini(dev_priv, VMW_PL_MOB);
	vmw_vram_manager_fini(dev_priv);
	ttm_device_fini(&dev_priv->bdev);
	drm_vma_offset_manager_destroy(&dev_priv->vma_manager);
	vmw_release_device_late(dev_priv);
	vmw_fence_manager_takedown(dev_priv->fman);
	if (dev_priv->capabilities & SVGA_CAP_IRQMASK)
		vmw_irq_uninstall(&dev_priv->drm);

	ttm_object_device_release(&dev_priv->tdev);
	if (dev_priv->ctx.staged_bindings)
		vmw_binding_state_free(dev_priv->ctx.staged_bindings);

	for (i = vmw_res_context; i < vmw_res_max; ++i)
		idr_destroy(&dev_priv->res_idr[i]);

	pci_release_regions(pdev);
}

static void vmw_postclose(struct drm_device *dev,
			 struct drm_file *file_priv)
{
	struct vmw_fpriv *vmw_fp = vmw_fpriv(file_priv);

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

static long vmw_generic_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg,
			      long (*ioctl_func)(struct file *, unsigned int,
						 unsigned long))
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	unsigned int nr = DRM_IOCTL_NR(cmd);
	unsigned int flags;

	/*
	 * Do extra checking on driver private ioctls.
	 */

	if ((nr >= DRM_COMMAND_BASE) && (nr < DRM_COMMAND_END)
	    && (nr < DRM_COMMAND_BASE + dev->driver->num_ioctls)) {
		const struct drm_ioctl_desc *ioctl =
			&vmw_ioctls[nr - DRM_COMMAND_BASE];

		if (nr == DRM_COMMAND_BASE + DRM_VMW_EXECBUF) {
			return ioctl_func(filp, cmd, arg);
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

	return ioctl_func(filp, cmd, arg);

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

static void vmw_master_set(struct drm_device *dev,
			   struct drm_file *file_priv,
			   bool from_open)
{
	/*
	 * Inform a new master that the layout may have changed while
	 * it was gone.
	 */
	if (!from_open)
		drm_sysfs_hotplug_event(dev);
}

static void vmw_master_drop(struct drm_device *dev,
			    struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);

	vmw_kms_legacy_hotspot_clear(dev_priv);
	if (!dev_priv->enable_fb)
		vmw_svga_disable(dev_priv);
}

/**
 * __vmw_svga_enable - Enable SVGA mode, FIFO and use of VRAM.
 *
 * @dev_priv: Pointer to device private struct.
 * Needs the reservation sem to be held in non-exclusive mode.
 */
static void __vmw_svga_enable(struct vmw_private *dev_priv)
{
	struct ttm_resource_manager *man = ttm_manager_type(&dev_priv->bdev, TTM_PL_VRAM);

	if (!ttm_resource_manager_used(man)) {
		vmw_write(dev_priv, SVGA_REG_ENABLE, SVGA_REG_ENABLE);
		ttm_resource_manager_set_used(man, true);
	}
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
	struct ttm_resource_manager *man = ttm_manager_type(&dev_priv->bdev, TTM_PL_VRAM);

	if (ttm_resource_manager_used(man)) {
		ttm_resource_manager_set_used(man, false);
		vmw_write(dev_priv, SVGA_REG_ENABLE,
			  SVGA_REG_ENABLE_HIDE |
			  SVGA_REG_ENABLE_ENABLE);
	}
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
	struct ttm_resource_manager *man = ttm_manager_type(&dev_priv->bdev, TTM_PL_VRAM);
	/*
	 * Disabling SVGA will turn off device modesetting capabilities, so
	 * notify KMS about that so that it doesn't cache atomic state that
	 * isn't valid anymore, for example crtcs turned on.
	 * Strictly we'd want to do this under the SVGA lock (or an SVGA mutex),
	 * but vmw_kms_lost_device() takes the reservation sem and thus we'll
	 * end up with lock order reversal. Thus, a master may actually perform
	 * a new modeset just after we call vmw_kms_lost_device() and race with
	 * vmw_svga_disable(), but that should at worst cause atomic KMS state
	 * to be inconsistent with the device, causing modesetting problems.
	 *
	 */
	vmw_kms_lost_device(&dev_priv->drm);
	ttm_write_lock(&dev_priv->reservation_sem, false);
	if (ttm_resource_manager_used(man)) {
		if (ttm_resource_manager_evict_all(&dev_priv->bdev, man))
			DRM_ERROR("Failed evicting VRAM buffers.\n");
		ttm_resource_manager_set_used(man, false);
		vmw_write(dev_priv, SVGA_REG_ENABLE,
			  SVGA_REG_ENABLE_HIDE |
			  SVGA_REG_ENABLE_ENABLE);
	}
	ttm_write_unlock(&dev_priv->reservation_sem);
}

static void vmw_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	ttm_mem_global_release(&ttm_mem_glob);
	drm_dev_unregister(dev);
	vmw_driver_unload(dev);
}

static unsigned long
vmw_get_unmapped_area(struct file *file, unsigned long uaddr,
		      unsigned long len, unsigned long pgoff,
		      unsigned long flags)
{
	struct drm_file *file_priv = file->private_data;
	struct vmw_private *dev_priv = vmw_priv(file_priv->minor->dev);

	return drm_get_unmapped_area(file, uaddr, len, pgoff, flags,
				     &dev_priv->vma_manager);
}

static int vmwgfx_pm_notifier(struct notifier_block *nb, unsigned long val,
			      void *ptr)
{
	struct vmw_private *dev_priv =
		container_of(nb, struct vmw_private, pm_nb);

	switch (val) {
	case PM_HIBERNATION_PREPARE:
		/*
		 * Take the reservation sem in write mode, which will make sure
		 * there are no other processes holding a buffer object
		 * reservation, meaning we should be able to evict all buffer
		 * objects if needed.
		 * Once user-space processes have been frozen, we can release
		 * the lock again.
		 */
		ttm_suspend_lock(&dev_priv->reservation_sem);
		dev_priv->suspend_locked = true;
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_RESTORE:
		if (READ_ONCE(dev_priv->suspend_locked)) {
			dev_priv->suspend_locked = false;
			ttm_suspend_unlock(&dev_priv->reservation_sem);
		}
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
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = false
	};
	int ret;

	/*
	 * Unlock for vmw_kms_suspend.
	 * No user-space processes should be running now.
	 */
	ttm_suspend_unlock(&dev_priv->reservation_sem);
	ret = vmw_kms_suspend(&dev_priv->drm);
	if (ret) {
		ttm_suspend_lock(&dev_priv->reservation_sem);
		DRM_ERROR("Failed to freeze modesetting.\n");
		return ret;
	}
	if (dev_priv->enable_fb)
		vmw_fb_off(dev_priv);

	ttm_suspend_lock(&dev_priv->reservation_sem);
	vmw_execbuf_release_pinned_bo(dev_priv);
	vmw_resource_evict_all(dev_priv);
	vmw_release_device_early(dev_priv);
	while (ttm_bo_swapout(&ctx, GFP_KERNEL) > 0);
	if (dev_priv->enable_fb)
		vmw_fifo_resource_dec(dev_priv);
	if (atomic_read(&dev_priv->num_fifo_resources) != 0) {
		DRM_ERROR("Can't hibernate while 3D resources are active.\n");
		if (dev_priv->enable_fb)
			vmw_fifo_resource_inc(dev_priv);
		WARN_ON(vmw_request_device_late(dev_priv));
		dev_priv->suspend_locked = false;
		ttm_suspend_unlock(&dev_priv->reservation_sem);
		if (dev_priv->suspend_state)
			vmw_kms_resume(dev);
		if (dev_priv->enable_fb)
			vmw_fb_on(dev_priv);
		return -EBUSY;
	}

	vmw_fence_fifo_down(dev_priv->fman);
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

	vmw_fence_fifo_up(dev_priv->fman);
	dev_priv->suspend_locked = false;
	ttm_suspend_unlock(&dev_priv->reservation_sem);
	if (dev_priv->suspend_state)
		vmw_kms_resume(&dev_priv->drm);

	if (dev_priv->enable_fb)
		vmw_fb_on(dev_priv);

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
	.get_unmapped_area = vmw_get_unmapped_area,
};

static const struct drm_driver driver = {
	.driver_features =
	DRIVER_MODESET | DRIVER_RENDER | DRIVER_ATOMIC,
	.ioctls = vmw_ioctls,
	.num_ioctls = ARRAY_SIZE(vmw_ioctls),
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
	struct vmw_private *vmw;
	int ret;

	ret = drm_fb_helper_remove_conflicting_pci_framebuffers(pdev, "svgadrmfb");
	if (ret)
		return ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	vmw = devm_drm_dev_alloc(&pdev->dev, &driver,
				 struct vmw_private, drm);
	if (IS_ERR(vmw))
		return PTR_ERR(vmw);

	pci_set_drvdata(pdev, &vmw->drm);

	ret = ttm_mem_global_init(&ttm_mem_glob, &pdev->dev);
	if (ret)
		return ret;

	ret = vmw_driver_load(vmw, ent->device);
	if (ret)
		return ret;

	ret = drm_dev_register(&vmw->drm, 0);
	if (ret) {
		vmw_driver_unload(&vmw->drm);
		return ret;
	}

	return 0;
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
