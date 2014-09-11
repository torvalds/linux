/**************************************************************************
 *
 * Copyright © 2009 VMware, Inc., Palo Alto, CA., USA
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

#include <drm/drmP.h>
#include "vmwgfx_drv.h"
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_object.h>
#include <drm/ttm/ttm_module.h>
#include <linux/dma_remapping.h>

#define VMWGFX_DRIVER_NAME "vmwgfx"
#define VMWGFX_DRIVER_DESC "Linux drm driver for VMware graphics devices"
#define VMWGFX_CHIP_SVGAII 0
#define VMW_FB_RESERVATION 0

#define VMW_MIN_INITIAL_WIDTH 800
#define VMW_MIN_INITIAL_HEIGHT 600


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

/**
 * The core DRM version of this macro doesn't account for
 * DRM_COMMAND_BASE.
 */

#define VMW_IOCTL_DEF(ioctl, func, flags) \
  [DRM_IOCTL_NR(DRM_IOCTL_##ioctl) - DRM_COMMAND_BASE] = {DRM_##ioctl, flags, func, DRM_IOCTL_##ioctl}

/**
 * Ioctl definitions.
 */

static const struct drm_ioctl_desc vmw_ioctls[] = {
	VMW_IOCTL_DEF(VMW_GET_PARAM, vmw_getparam_ioctl,
		      DRM_AUTH | DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_ALLOC_DMABUF, vmw_dmabuf_alloc_ioctl,
		      DRM_AUTH | DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_UNREF_DMABUF, vmw_dmabuf_unref_ioctl,
		      DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_CURSOR_BYPASS,
		      vmw_kms_cursor_bypass_ioctl,
		      DRM_MASTER | DRM_CONTROL_ALLOW | DRM_UNLOCKED),

	VMW_IOCTL_DEF(VMW_CONTROL_STREAM, vmw_overlay_ioctl,
		      DRM_MASTER | DRM_CONTROL_ALLOW | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_CLAIM_STREAM, vmw_stream_claim_ioctl,
		      DRM_MASTER | DRM_CONTROL_ALLOW | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_UNREF_STREAM, vmw_stream_unref_ioctl,
		      DRM_MASTER | DRM_CONTROL_ALLOW | DRM_UNLOCKED),

	VMW_IOCTL_DEF(VMW_CREATE_CONTEXT, vmw_context_define_ioctl,
		      DRM_AUTH | DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_UNREF_CONTEXT, vmw_context_destroy_ioctl,
		      DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_CREATE_SURFACE, vmw_surface_define_ioctl,
		      DRM_AUTH | DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_UNREF_SURFACE, vmw_surface_destroy_ioctl,
		      DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_REF_SURFACE, vmw_surface_reference_ioctl,
		      DRM_AUTH | DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_EXECBUF, vmw_execbuf_ioctl,
		      DRM_AUTH | DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_FENCE_WAIT, vmw_fence_obj_wait_ioctl,
		      DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_FENCE_SIGNALED,
		      vmw_fence_obj_signaled_ioctl,
		      DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_FENCE_UNREF, vmw_fence_obj_unref_ioctl,
		      DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_FENCE_EVENT, vmw_fence_event_ioctl,
		      DRM_AUTH | DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_GET_3D_CAP, vmw_get_cap_3d_ioctl,
		      DRM_AUTH | DRM_UNLOCKED | DRM_RENDER_ALLOW),

	/* these allow direct access to the framebuffers mark as master only */
	VMW_IOCTL_DEF(VMW_PRESENT, vmw_present_ioctl,
		      DRM_MASTER | DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_PRESENT_READBACK,
		      vmw_present_readback_ioctl,
		      DRM_MASTER | DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_UPDATE_LAYOUT,
		      vmw_kms_update_layout_ioctl,
		      DRM_MASTER | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_CREATE_SHADER,
		      vmw_shader_define_ioctl,
		      DRM_AUTH | DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_UNREF_SHADER,
		      vmw_shader_destroy_ioctl,
		      DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_GB_SURFACE_CREATE,
		      vmw_gb_surface_define_ioctl,
		      DRM_AUTH | DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_GB_SURFACE_REF,
		      vmw_gb_surface_reference_ioctl,
		      DRM_AUTH | DRM_UNLOCKED | DRM_RENDER_ALLOW),
	VMW_IOCTL_DEF(VMW_SYNCCPU,
		      vmw_user_dmabuf_synccpu_ioctl,
		      DRM_UNLOCKED | DRM_RENDER_ALLOW),
};

static struct pci_device_id vmw_pci_id_list[] = {
	{0x15ad, 0x0405, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VMWGFX_CHIP_SVGAII},
	{0, 0, 0}
};
MODULE_DEVICE_TABLE(pci, vmw_pci_id_list);

static int enable_fbdev = IS_ENABLED(CONFIG_DRM_VMWGFX_FBCON);
static int vmw_force_iommu;
static int vmw_restrict_iommu;
static int vmw_force_coherent;
static int vmw_restrict_dma_mask;

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
	struct ttm_buffer_object *bo;
	struct ttm_bo_kmap_obj map;
	volatile SVGA3dQueryResult *result;
	bool dummy;

	/*
	 * Create the bo as pinned, so that a tryreserve will
	 * immediately succeed. This is because we're the only
	 * user of the bo currently.
	 */
	ret = ttm_bo_create(&dev_priv->bdev,
			    PAGE_SIZE,
			    ttm_bo_type_device,
			    &vmw_sys_ne_placement,
			    0, false, NULL,
			    &bo);

	if (unlikely(ret != 0))
		return ret;

	ret = ttm_bo_reserve(bo, false, true, false, NULL);
	BUG_ON(ret != 0);

	ret = ttm_bo_kmap(bo, 0, 1, &map);
	if (likely(ret == 0)) {
		result = ttm_kmap_obj_virtual(&map, &dummy);
		result->totalSize = sizeof(*result);
		result->state = SVGA3D_QUERYSTATE_PENDING;
		result->result32 = 0xff;
		ttm_bo_kunmap(&map);
	}
	vmw_bo_pin(bo, false);
	ttm_bo_unreserve(bo);

	if (unlikely(ret != 0)) {
		DRM_ERROR("Dummy query buffer map failed.\n");
		ttm_bo_unref(&bo);
	} else
		dev_priv->dummy_query_bo = bo;

	return ret;
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
	if (dev_priv->has_mob) {
		ret = vmw_otables_setup(dev_priv);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Unable to initialize "
				  "guest Memory OBjects.\n");
			goto out_no_mob;
		}
	}
	ret = vmw_dummy_query_bo_create(dev_priv);
	if (unlikely(ret != 0))
		goto out_no_query_bo;

	return 0;

out_no_query_bo:
	if (dev_priv->has_mob)
		vmw_otables_takedown(dev_priv);
out_no_mob:
	vmw_fence_fifo_down(dev_priv->fman);
	vmw_fifo_release(dev_priv, &dev_priv->fifo);
	return ret;
}

static void vmw_release_device(struct vmw_private *dev_priv)
{
	/*
	 * Previous destructions should've released
	 * the pinned bo.
	 */

	BUG_ON(dev_priv->pinned_bo != NULL);

	ttm_bo_unref(&dev_priv->dummy_query_bo);
	if (dev_priv->has_mob)
		vmw_otables_takedown(dev_priv);
	vmw_fence_fifo_down(dev_priv->fman);
	vmw_fifo_release(dev_priv, &dev_priv->fifo);
}


/**
 * Increase the 3d resource refcount.
 * If the count was prevously zero, initialize the fifo, switching to svga
 * mode. Note that the master holds a ref as well, and may request an
 * explicit switch to svga mode if fb is not running, using @unhide_svga.
 */
int vmw_3d_resource_inc(struct vmw_private *dev_priv,
			bool unhide_svga)
{
	int ret = 0;

	mutex_lock(&dev_priv->release_mutex);
	if (unlikely(dev_priv->num_3d_resources++ == 0)) {
		ret = vmw_request_device(dev_priv);
		if (unlikely(ret != 0))
			--dev_priv->num_3d_resources;
	} else if (unhide_svga) {
		mutex_lock(&dev_priv->hw_mutex);
		vmw_write(dev_priv, SVGA_REG_ENABLE,
			  vmw_read(dev_priv, SVGA_REG_ENABLE) &
			  ~SVGA_REG_ENABLE_HIDE);
		mutex_unlock(&dev_priv->hw_mutex);
	}

	mutex_unlock(&dev_priv->release_mutex);
	return ret;
}

/**
 * Decrease the 3d resource refcount.
 * If the count reaches zero, disable the fifo, switching to vga mode.
 * Note that the master holds a refcount as well, and may request an
 * explicit switch to vga mode when it releases its refcount to account
 * for the situation of an X server vt switch to VGA with 3d resources
 * active.
 */
void vmw_3d_resource_dec(struct vmw_private *dev_priv,
			 bool hide_svga)
{
	int32_t n3d;

	mutex_lock(&dev_priv->release_mutex);
	if (unlikely(--dev_priv->num_3d_resources == 0))
		vmw_release_device(dev_priv);
	else if (hide_svga) {
		mutex_lock(&dev_priv->hw_mutex);
		vmw_write(dev_priv, SVGA_REG_ENABLE,
			  vmw_read(dev_priv, SVGA_REG_ENABLE) |
			  SVGA_REG_ENABLE_HIDE);
		mutex_unlock(&dev_priv->hw_mutex);
	}

	n3d = (int32_t) dev_priv->num_3d_resources;
	mutex_unlock(&dev_priv->release_mutex);

	BUG_ON(n3d < 0);
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

	dev_priv = kzalloc(sizeof(*dev_priv), GFP_KERNEL);
	if (unlikely(dev_priv == NULL)) {
		DRM_ERROR("Failed allocating a device private struct.\n");
		return -ENOMEM;
	}

	pci_set_master(dev->pdev);

	dev_priv->dev = dev;
	dev_priv->vmw_chipset = chipset;
	dev_priv->last_read_seqno = (uint32_t) -100;
	mutex_init(&dev_priv->hw_mutex);
	mutex_init(&dev_priv->cmdbuf_mutex);
	mutex_init(&dev_priv->release_mutex);
	mutex_init(&dev_priv->binding_mutex);
	rwlock_init(&dev_priv->resource_lock);
	ttm_lock_init(&dev_priv->reservation_sem);

	for (i = vmw_res_context; i < vmw_res_max; ++i) {
		idr_init(&dev_priv->res_idr[i]);
		INIT_LIST_HEAD(&dev_priv->res_lru[i]);
	}

	mutex_init(&dev_priv->init_mutex);
	init_waitqueue_head(&dev_priv->fence_queue);
	init_waitqueue_head(&dev_priv->fifo_queue);
	dev_priv->fence_queue_waiters = 0;
	atomic_set(&dev_priv->fifo_queue_waiters, 0);

	dev_priv->used_memory_size = 0;

	dev_priv->io_start = pci_resource_start(dev->pdev, 0);
	dev_priv->vram_start = pci_resource_start(dev->pdev, 1);
	dev_priv->mmio_start = pci_resource_start(dev->pdev, 2);

	dev_priv->enable_fb = enable_fbdev;

	mutex_lock(&dev_priv->hw_mutex);

	vmw_write(dev_priv, SVGA_REG_ID, SVGA_ID_2);
	svga_id = vmw_read(dev_priv, SVGA_REG_ID);
	if (svga_id != SVGA_ID_2) {
		ret = -ENOSYS;
		DRM_ERROR("Unsupported SVGA ID 0x%x\n", svga_id);
		mutex_unlock(&dev_priv->hw_mutex);
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

		dev_priv->max_mob_pages = mem_size * 1024 / PAGE_SIZE;
		dev_priv->prim_bb_mem =
			vmw_read(dev_priv,
				 SVGA_REG_MAX_PRIMARY_BOUNDING_BOX_MEM);
		dev_priv->max_mob_size =
			vmw_read(dev_priv, SVGA_REG_MOB_MAX_SIZE);
	} else
		dev_priv->prim_bb_mem = dev_priv->vram_size;

	ret = vmw_dma_masks(dev_priv);
	if (unlikely(ret != 0)) {
		mutex_unlock(&dev_priv->hw_mutex);
		goto out_err0;
	}

	if (unlikely(dev_priv->prim_bb_mem < dev_priv->vram_size))
		dev_priv->prim_bb_mem = dev_priv->vram_size;

	mutex_unlock(&dev_priv->hw_mutex);

	vmw_print_capabilities(dev_priv->capabilities);

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


	ret = ttm_bo_device_init(&dev_priv->bdev,
				 dev_priv->bo_global_ref.ref.object,
				 &vmw_bo_driver,
				 dev->anon_inode->i_mapping,
				 VMWGFX_FILE_PAGE_OFFSET,
				 false);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed initializing TTM buffer object driver.\n");
		goto out_err1;
	}

	ret = ttm_bo_init_mm(&dev_priv->bdev, TTM_PL_VRAM,
			     (dev_priv->vram_size >> PAGE_SHIFT));
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed initializing memory manager for VRAM.\n");
		goto out_err2;
	}

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

	dev_priv->mmio_mtrr = arch_phys_wc_add(dev_priv->mmio_start,
					       dev_priv->mmio_size);

	dev_priv->mmio_virt = ioremap_wc(dev_priv->mmio_start,
					 dev_priv->mmio_size);

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
		ret = drm_irq_install(dev, dev->pdev->irq);
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

	vmw_kms_save_vga(dev_priv);

	/* Start kms and overlay systems, needs fifo. */
	ret = vmw_kms_init(dev_priv);
	if (unlikely(ret != 0))
		goto out_no_kms;
	vmw_overlay_init(dev_priv);

	if (dev_priv->enable_fb) {
		ret = vmw_3d_resource_inc(dev_priv, true);
		if (unlikely(ret != 0))
			goto out_no_fifo;
		vmw_fb_init(dev_priv);
	}

	dev_priv->pm_nb.notifier_call = vmwgfx_pm_notifier;
	register_pm_notifier(&dev_priv->pm_nb);

	return 0;

out_no_fifo:
	vmw_overlay_close(dev_priv);
	vmw_kms_close(dev_priv);
out_no_kms:
	vmw_kms_restore_vga(dev_priv);
	vmw_fence_manager_takedown(dev_priv->fman);
out_no_fman:
	if (dev_priv->capabilities & SVGA_CAP_IRQMASK)
		drm_irq_uninstall(dev_priv->dev);
out_no_irq:
	if (dev_priv->stealth)
		pci_release_region(dev->pdev, 2);
	else
		pci_release_regions(dev->pdev);
out_no_device:
	ttm_object_device_release(&dev_priv->tdev);
out_err4:
	iounmap(dev_priv->mmio_virt);
out_err3:
	arch_phys_wc_del(dev_priv->mmio_mtrr);
	if (dev_priv->has_mob)
		(void) ttm_bo_clean_mm(&dev_priv->bdev, VMW_PL_MOB);
	if (dev_priv->has_gmr)
		(void) ttm_bo_clean_mm(&dev_priv->bdev, VMW_PL_GMR);
	(void)ttm_bo_clean_mm(&dev_priv->bdev, TTM_PL_VRAM);
out_err2:
	(void)ttm_bo_device_release(&dev_priv->bdev);
out_err1:
	vmw_ttm_global_release(dev_priv);
out_err0:
	for (i = vmw_res_context; i < vmw_res_max; ++i)
		idr_destroy(&dev_priv->res_idr[i]);

	kfree(dev_priv);
	return ret;
}

static int vmw_driver_unload(struct drm_device *dev)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	enum vmw_res_type i;

	unregister_pm_notifier(&dev_priv->pm_nb);

	if (dev_priv->ctx.res_ht_initialized)
		drm_ht_remove(&dev_priv->ctx.res_ht);
	if (dev_priv->ctx.cmd_bounce)
		vfree(dev_priv->ctx.cmd_bounce);
	if (dev_priv->enable_fb) {
		vmw_fb_close(dev_priv);
		vmw_kms_restore_vga(dev_priv);
		vmw_3d_resource_dec(dev_priv, false);
	}
	vmw_kms_close(dev_priv);
	vmw_overlay_close(dev_priv);
	vmw_fence_manager_takedown(dev_priv->fman);
	if (dev_priv->capabilities & SVGA_CAP_IRQMASK)
		drm_irq_uninstall(dev_priv->dev);
	if (dev_priv->stealth)
		pci_release_region(dev->pdev, 2);
	else
		pci_release_regions(dev->pdev);

	ttm_object_device_release(&dev_priv->tdev);
	iounmap(dev_priv->mmio_virt);
	arch_phys_wc_del(dev_priv->mmio_mtrr);
	if (dev_priv->has_mob)
		(void) ttm_bo_clean_mm(&dev_priv->bdev, VMW_PL_MOB);
	if (dev_priv->has_gmr)
		(void)ttm_bo_clean_mm(&dev_priv->bdev, VMW_PL_GMR);
	(void)ttm_bo_clean_mm(&dev_priv->bdev, TTM_PL_VRAM);
	(void)ttm_bo_device_release(&dev_priv->bdev);
	vmw_ttm_global_release(dev_priv);

	for (i = vmw_res_context; i < vmw_res_max; ++i)
		idr_destroy(&dev_priv->res_idr[i]);

	kfree(dev_priv);

	return 0;
}

static void vmw_preclose(struct drm_device *dev,
			 struct drm_file *file_priv)
{
	struct vmw_fpriv *vmw_fp = vmw_fpriv(file_priv);
	struct vmw_private *dev_priv = vmw_priv(dev);

	vmw_event_fence_fpriv_gone(dev_priv->fman, &vmw_fp->fence_events);
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
	if (unlikely(vmw_fp == NULL))
		return ret;

	INIT_LIST_HEAD(&vmw_fp->fence_events);
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

	if (file_priv->minor->type != DRM_MINOR_LEGACY ||
	    !(flags & DRM_AUTH))
		return NULL;

	ret = mutex_lock_interruptible(&dev->master_mutex);
	if (unlikely(ret != 0))
		return ERR_PTR(-ERESTARTSYS);

	if (file_priv->is_master) {
		mutex_unlock(&dev->master_mutex);
		return NULL;
	}

	/*
	 * Check if we were previously master, but now dropped.
	 */
	if (vmw_fp->locked_master) {
		mutex_unlock(&dev->master_mutex);
		DRM_ERROR("Dropped master trying to access ioctl that "
			  "requires authentication.\n");
		return ERR_PTR(-EACCES);
	}
	mutex_unlock(&dev->master_mutex);

	/*
	 * Taking the drm_global_mutex after the TTM lock might deadlock
	 */
	if (!(flags & DRM_UNLOCKED)) {
		DRM_ERROR("Refusing locked ioctl access.\n");
		return ERR_PTR(-EDEADLK);
	}

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

		if (unlikely(ioctl->cmd_drv != cmd)) {
			DRM_ERROR("Invalid command format, ioctl %d\n",
				  nr - DRM_COMMAND_BASE);
			return -EINVAL;
		}
		flags = ioctl->flags;
	} else if (!drm_ioctl_flags(nr, &flags))
		return -EINVAL;

	vmaster = vmw_master_check(dev, file_priv, flags);
	if (unlikely(IS_ERR(vmaster))) {
		DRM_INFO("IOCTL ERROR %d\n", nr);
		return PTR_ERR(vmaster);
	}

	ret = ioctl_func(filp, cmd, arg);
	if (vmaster)
		ttm_read_unlock(&vmaster->lock);

	return ret;
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
	struct drm_crtc *crtc;
	struct drm_mode_set set;
	int ret;

	set.x = 0;
	set.y = 0;
	set.fb = NULL;
	set.mode = NULL;
	set.connectors = NULL;
	set.num_connectors = 0;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		set.crtc = crtc;
		ret = drm_mode_set_config_internal(&set);
		WARN_ON(ret != 0);
	}

}

static void vmw_master_init(struct vmw_master *vmaster)
{
	ttm_lock_init(&vmaster->lock);
	INIT_LIST_HEAD(&vmaster->fb_surf);
	mutex_init(&vmaster->fb_surf_mutex);
}

static int vmw_master_create(struct drm_device *dev,
			     struct drm_master *master)
{
	struct vmw_master *vmaster;

	vmaster = kzalloc(sizeof(*vmaster), GFP_KERNEL);
	if (unlikely(vmaster == NULL))
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

	if (!dev_priv->enable_fb) {
		ret = vmw_3d_resource_inc(dev_priv, true);
		if (unlikely(ret != 0))
			return ret;
		vmw_kms_save_vga(dev_priv);
		mutex_lock(&dev_priv->hw_mutex);
		vmw_write(dev_priv, SVGA_REG_TRACES, 0);
		mutex_unlock(&dev_priv->hw_mutex);
	}

	if (active) {
		BUG_ON(active != &dev_priv->fbdev_master);
		ret = ttm_vt_lock(&active->lock, false, vmw_fp->tfile);
		if (unlikely(ret != 0))
			goto out_no_active_lock;

		ttm_lock_set_kill(&active->lock, true, SIGTERM);
		ret = ttm_bo_evict_mm(&dev_priv->bdev, TTM_PL_VRAM);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Unable to clean VRAM on "
				  "master drop.\n");
		}

		dev_priv->active_master = NULL;
	}

	ttm_lock_set_kill(&vmaster->lock, false, SIGTERM);
	if (!from_open) {
		ttm_vt_unlock(&vmaster->lock);
		BUG_ON(vmw_fp->locked_master != file_priv->master);
		drm_master_put(&vmw_fp->locked_master);
	}

	dev_priv->active_master = vmaster;

	return 0;

out_no_active_lock:
	if (!dev_priv->enable_fb) {
		vmw_kms_restore_vga(dev_priv);
		vmw_3d_resource_dec(dev_priv, true);
		mutex_lock(&dev_priv->hw_mutex);
		vmw_write(dev_priv, SVGA_REG_TRACES, 1);
		mutex_unlock(&dev_priv->hw_mutex);
	}
	return ret;
}

static void vmw_master_drop(struct drm_device *dev,
			    struct drm_file *file_priv,
			    bool from_release)
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
	if (unlikely((ret != 0))) {
		DRM_ERROR("Unable to lock TTM at VT switch.\n");
		drm_master_put(&vmw_fp->locked_master);
	}

	ttm_lock_set_kill(&vmaster->lock, false, SIGTERM);
	vmw_execbuf_release_pinned_bo(dev_priv);

	if (!dev_priv->enable_fb) {
		ret = ttm_bo_evict_mm(&dev_priv->bdev, TTM_PL_VRAM);
		if (unlikely(ret != 0))
			DRM_ERROR("Unable to clean VRAM on master drop.\n");
		vmw_kms_restore_vga(dev_priv);
		vmw_3d_resource_dec(dev_priv, true);
		mutex_lock(&dev_priv->hw_mutex);
		vmw_write(dev_priv, SVGA_REG_TRACES, 1);
		mutex_unlock(&dev_priv->hw_mutex);
	}

	dev_priv->active_master = &dev_priv->fbdev_master;
	ttm_lock_set_kill(&dev_priv->fbdev_master.lock, false, SIGTERM);
	ttm_vt_unlock(&dev_priv->fbdev_master.lock);

	if (dev_priv->enable_fb)
		vmw_fb_on(dev_priv);
}


static void vmw_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static int vmwgfx_pm_notifier(struct notifier_block *nb, unsigned long val,
			      void *ptr)
{
	struct vmw_private *dev_priv =
		container_of(nb, struct vmw_private, pm_nb);

	switch (val) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		ttm_suspend_lock(&dev_priv->reservation_sem);

		/**
		 * This empties VRAM and unbinds all GMR bindings.
		 * Buffer contents is moved to swappable memory.
		 */
		vmw_execbuf_release_pinned_bo(dev_priv);
		vmw_resource_evict_all(dev_priv);
		ttm_bo_swapout_all(&dev_priv->bdev);

		break;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
	case PM_POST_RESTORE:
		ttm_suspend_unlock(&dev_priv->reservation_sem);

		break;
	case PM_RESTORE_PREPARE:
		break;
	default:
		break;
	}
	return 0;
}

/**
 * These might not be needed with the virtual SVGA device.
 */

static int vmw_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct vmw_private *dev_priv = vmw_priv(dev);

	if (dev_priv->num_3d_resources != 0) {
		DRM_INFO("Can't suspend or hibernate "
			 "while 3D resources are active.\n");
		return -EBUSY;
	}

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

static int vmw_pm_prepare(struct device *kdev)
{
	struct pci_dev *pdev = to_pci_dev(kdev);
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct vmw_private *dev_priv = vmw_priv(dev);

	/**
	 * Release 3d reference held by fbdev and potentially
	 * stop fifo.
	 */
	dev_priv->suspended = true;
	if (dev_priv->enable_fb)
			vmw_3d_resource_dec(dev_priv, true);

	if (dev_priv->num_3d_resources != 0) {

		DRM_INFO("Can't suspend or hibernate "
			 "while 3D resources are active.\n");

		if (dev_priv->enable_fb)
			vmw_3d_resource_inc(dev_priv, true);
		dev_priv->suspended = false;
		return -EBUSY;
	}

	return 0;
}

static void vmw_pm_complete(struct device *kdev)
{
	struct pci_dev *pdev = to_pci_dev(kdev);
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct vmw_private *dev_priv = vmw_priv(dev);

	mutex_lock(&dev_priv->hw_mutex);
	vmw_write(dev_priv, SVGA_REG_ID, SVGA_ID_2);
	(void) vmw_read(dev_priv, SVGA_REG_ID);
	mutex_unlock(&dev_priv->hw_mutex);

	/**
	 * Reclaim 3d reference held by fbdev and potentially
	 * start fifo.
	 */
	if (dev_priv->enable_fb)
			vmw_3d_resource_inc(dev_priv, false);

	dev_priv->suspended = false;
}

static const struct dev_pm_ops vmw_pm_ops = {
	.prepare = vmw_pm_prepare,
	.complete = vmw_pm_complete,
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
	DRIVER_MODESET | DRIVER_PRIME | DRIVER_RENDER,
	.load = vmw_driver_load,
	.unload = vmw_driver_unload,
	.lastclose = vmw_lastclose,
	.irq_preinstall = vmw_irq_preinstall,
	.irq_postinstall = vmw_irq_postinstall,
	.irq_uninstall = vmw_irq_uninstall,
	.irq_handler = vmw_irq_handler,
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
	.preclose = vmw_preclose,
	.postclose = vmw_postclose,
	.set_busid = drm_pci_set_busid,

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
	ret = drm_pci_init(&driver, &vmw_pci_driver);
	if (ret)
		DRM_ERROR("Failed initializing DRM.\n");
	return ret;
}

static void __exit vmwgfx_exit(void)
{
	drm_pci_exit(&driver, &vmw_pci_driver);
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
