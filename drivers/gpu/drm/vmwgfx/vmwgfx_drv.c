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

/**
 * The core DRM version of this macro doesn't account for
 * DRM_COMMAND_BASE.
 */

#define VMW_IOCTL_DEF(ioctl, func, flags) \
  [DRM_IOCTL_NR(DRM_IOCTL_##ioctl) - DRM_COMMAND_BASE] = {DRM_##ioctl, flags, func, DRM_IOCTL_##ioctl}

/**
 * Ioctl definitions.
 */

static struct drm_ioctl_desc vmw_ioctls[] = {
	VMW_IOCTL_DEF(VMW_GET_PARAM, vmw_getparam_ioctl,
		      DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_ALLOC_DMABUF, vmw_dmabuf_alloc_ioctl,
		      DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_UNREF_DMABUF, vmw_dmabuf_unref_ioctl,
		      DRM_AUTH | DRM_UNLOCKED),
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
		      DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_UNREF_CONTEXT, vmw_context_destroy_ioctl,
		      DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_CREATE_SURFACE, vmw_surface_define_ioctl,
		      DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_UNREF_SURFACE, vmw_surface_destroy_ioctl,
		      DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_REF_SURFACE, vmw_surface_reference_ioctl,
		      DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_EXECBUF, vmw_execbuf_ioctl,
		      DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_FENCE_WAIT, vmw_fence_obj_wait_ioctl,
		      DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_FENCE_SIGNALED,
		      vmw_fence_obj_signaled_ioctl,
		      DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_FENCE_UNREF, vmw_fence_obj_unref_ioctl,
		      DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_FENCE_EVENT,
		      vmw_fence_event_ioctl,
		      DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_GET_3D_CAP, vmw_get_cap_3d_ioctl,
		      DRM_AUTH | DRM_UNLOCKED),

	/* these allow direct access to the framebuffers mark as master only */
	VMW_IOCTL_DEF(VMW_PRESENT, vmw_present_ioctl,
		      DRM_MASTER | DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_PRESENT_READBACK,
		      vmw_present_readback_ioctl,
		      DRM_MASTER | DRM_AUTH | DRM_UNLOCKED),
	VMW_IOCTL_DEF(VMW_UPDATE_LAYOUT,
		      vmw_kms_update_layout_ioctl,
		      DRM_MASTER | DRM_UNLOCKED),
};

static struct pci_device_id vmw_pci_id_list[] = {
	{0x15ad, 0x0405, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VMWGFX_CHIP_SVGAII},
	{0, 0, 0}
};
MODULE_DEVICE_TABLE(pci, vmw_pci_id_list);

static int enable_fbdev = IS_ENABLED(CONFIG_DRM_VMWGFX_FBCON);

static int vmw_probe(struct pci_dev *, const struct pci_device_id *);
static void vmw_master_init(struct vmw_master *);
static int vmwgfx_pm_notifier(struct notifier_block *nb, unsigned long val,
			      void *ptr);

MODULE_PARM_DESC(enable_fbdev, "Enable vmwgfx fbdev");
module_param_named(enable_fbdev, enable_fbdev, int, 0600);

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
}


/**
 * vmw_execbuf_prepare_dummy_query - Initialize a query result structure at
 * the start of a buffer object.
 *
 * @dev_priv: The device private structure.
 *
 * This function will idle the buffer using an uninterruptible wait, then
 * map the first page and initialize a pending occlusion query result structure,
 * Finally it will unmap the buffer.
 *
 * TODO: Since we're only mapping a single page, we should optimize the map
 * to use kmap_atomic / iomap_atomic.
 */
static void vmw_dummy_query_bo_prepare(struct vmw_private *dev_priv)
{
	struct ttm_bo_kmap_obj map;
	volatile SVGA3dQueryResult *result;
	bool dummy;
	int ret;
	struct ttm_bo_device *bdev = &dev_priv->bdev;
	struct ttm_buffer_object *bo = dev_priv->dummy_query_bo;

	ttm_bo_reserve(bo, false, false, false, 0);
	spin_lock(&bdev->fence_lock);
	ret = ttm_bo_wait(bo, false, false, false);
	spin_unlock(&bdev->fence_lock);
	if (unlikely(ret != 0))
		(void) vmw_fallback_wait(dev_priv, false, true, 0, false,
					 10*HZ);

	ret = ttm_bo_kmap(bo, 0, 1, &map);
	if (likely(ret == 0)) {
		result = ttm_kmap_obj_virtual(&map, &dummy);
		result->totalSize = sizeof(*result);
		result->state = SVGA3D_QUERYSTATE_PENDING;
		result->result32 = 0xff;
		ttm_bo_kunmap(&map);
	} else
		DRM_ERROR("Dummy query buffer map failed.\n");
	ttm_bo_unreserve(bo);
}


/**
 * vmw_dummy_query_bo_create - create a bo to hold a dummy query result
 *
 * @dev_priv: A device private structure.
 *
 * This function creates a small buffer object that holds the query
 * result for dummy queries emitted as query barriers.
 * No interruptible waits are done within this function.
 *
 * Returns an error if bo creation fails.
 */
static int vmw_dummy_query_bo_create(struct vmw_private *dev_priv)
{
	return ttm_bo_create(&dev_priv->bdev,
			     PAGE_SIZE,
			     ttm_bo_type_device,
			     &vmw_vram_sys_placement,
			     0, 0, false, NULL,
			     &dev_priv->dummy_query_bo);
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
	ret = vmw_dummy_query_bo_create(dev_priv);
	if (unlikely(ret != 0))
		goto out_no_query_bo;
	vmw_dummy_query_bo_prepare(dev_priv);

	return 0;

out_no_query_bo:
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

static int vmw_driver_load(struct drm_device *dev, unsigned long chipset)
{
	struct vmw_private *dev_priv;
	int ret;
	uint32_t svga_id;

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
	rwlock_init(&dev_priv->resource_lock);
	idr_init(&dev_priv->context_idr);
	idr_init(&dev_priv->surface_idr);
	idr_init(&dev_priv->stream_idr);
	mutex_init(&dev_priv->init_mutex);
	init_waitqueue_head(&dev_priv->fence_queue);
	init_waitqueue_head(&dev_priv->fifo_queue);
	dev_priv->fence_queue_waiters = 0;
	atomic_set(&dev_priv->fifo_queue_waiters, 0);
	INIT_LIST_HEAD(&dev_priv->surface_lru);
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

	dev_priv->vram_size = vmw_read(dev_priv, SVGA_REG_VRAM_SIZE);
	dev_priv->mmio_size = vmw_read(dev_priv, SVGA_REG_MEM_SIZE);
	dev_priv->fb_max_width = vmw_read(dev_priv, SVGA_REG_MAX_WIDTH);
	dev_priv->fb_max_height = vmw_read(dev_priv, SVGA_REG_MAX_HEIGHT);

	vmw_get_initial_size(dev_priv);

	if (dev_priv->capabilities & SVGA_CAP_GMR) {
		dev_priv->max_gmr_descriptors =
			vmw_read(dev_priv,
				 SVGA_REG_GMR_MAX_DESCRIPTOR_LENGTH);
		dev_priv->max_gmr_ids =
			vmw_read(dev_priv, SVGA_REG_GMR_MAX_IDS);
	}
	if (dev_priv->capabilities & SVGA_CAP_GMR2) {
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

	mutex_unlock(&dev_priv->hw_mutex);

	vmw_print_capabilities(dev_priv->capabilities);

	if (dev_priv->capabilities & SVGA_CAP_GMR) {
		DRM_INFO("Max GMR ids is %u\n",
			 (unsigned)dev_priv->max_gmr_ids);
		DRM_INFO("Max GMR descriptors is %u\n",
			 (unsigned)dev_priv->max_gmr_descriptors);
	}
	if (dev_priv->capabilities & SVGA_CAP_GMR2) {
		DRM_INFO("Max number of GMR pages is %u\n",
			 (unsigned)dev_priv->max_gmr_pages);
		DRM_INFO("Max dedicated hypervisor surface memory is %u kiB\n",
			 (unsigned)dev_priv->memory_size / 1024);
	}
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
				 &vmw_bo_driver, VMWGFX_FILE_PAGE_OFFSET,
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
	if (ttm_bo_init_mm(&dev_priv->bdev, VMW_PL_GMR,
			   dev_priv->max_gmr_ids) != 0) {
		DRM_INFO("No GMR memory available. "
			 "Graphics memory resources are very limited.\n");
		dev_priv->has_gmr = false;
	}

	dev_priv->mmio_mtrr = drm_mtrr_add(dev_priv->mmio_start,
					   dev_priv->mmio_size, DRM_MTRR_WC);

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
	    (dev_priv->mem_global_ref.object, 12);

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

	dev_priv->fman = vmw_fence_manager_init(dev_priv);
	if (unlikely(dev_priv->fman == NULL))
		goto out_no_fman;

	/* Need to start the fifo to check if we can do screen objects */
	ret = vmw_3d_resource_inc(dev_priv, true);
	if (unlikely(ret != 0))
		goto out_no_fifo;
	vmw_kms_save_vga(dev_priv);

	/* Start kms and overlay systems, needs fifo. */
	ret = vmw_kms_init(dev_priv);
	if (unlikely(ret != 0))
		goto out_no_kms;
	vmw_overlay_init(dev_priv);

	/* 3D Depends on Screen Objects being used. */
	DRM_INFO("Detected %sdevice 3D availability.\n",
		 vmw_fifo_have_3d(dev_priv) ?
		 "" : "no ");

	/* We might be done with the fifo now */
	if (dev_priv->enable_fb) {
		vmw_fb_init(dev_priv);
	} else {
		vmw_kms_restore_vga(dev_priv);
		vmw_3d_resource_dec(dev_priv, true);
	}

	if (dev_priv->capabilities & SVGA_CAP_IRQMASK) {
		ret = drm_irq_install(dev);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Failed installing irq: %d\n", ret);
			goto out_no_irq;
		}
	}

	dev_priv->pm_nb.notifier_call = vmwgfx_pm_notifier;
	register_pm_notifier(&dev_priv->pm_nb);

	return 0;

out_no_irq:
	if (dev_priv->enable_fb)
		vmw_fb_close(dev_priv);
	vmw_overlay_close(dev_priv);
	vmw_kms_close(dev_priv);
out_no_kms:
	/* We still have a 3D resource reference held */
	if (dev_priv->enable_fb) {
		vmw_kms_restore_vga(dev_priv);
		vmw_3d_resource_dec(dev_priv, false);
	}
out_no_fifo:
	vmw_fence_manager_takedown(dev_priv->fman);
out_no_fman:
	if (dev_priv->stealth)
		pci_release_region(dev->pdev, 2);
	else
		pci_release_regions(dev->pdev);
out_no_device:
	ttm_object_device_release(&dev_priv->tdev);
out_err4:
	iounmap(dev_priv->mmio_virt);
out_err3:
	drm_mtrr_del(dev_priv->mmio_mtrr, dev_priv->mmio_start,
		     dev_priv->mmio_size, DRM_MTRR_WC);
	if (dev_priv->has_gmr)
		(void) ttm_bo_clean_mm(&dev_priv->bdev, VMW_PL_GMR);
	(void)ttm_bo_clean_mm(&dev_priv->bdev, TTM_PL_VRAM);
out_err2:
	(void)ttm_bo_device_release(&dev_priv->bdev);
out_err1:
	vmw_ttm_global_release(dev_priv);
out_err0:
	idr_destroy(&dev_priv->surface_idr);
	idr_destroy(&dev_priv->context_idr);
	idr_destroy(&dev_priv->stream_idr);
	kfree(dev_priv);
	return ret;
}

static int vmw_driver_unload(struct drm_device *dev)
{
	struct vmw_private *dev_priv = vmw_priv(dev);

	unregister_pm_notifier(&dev_priv->pm_nb);

	if (dev_priv->ctx.cmd_bounce)
		vfree(dev_priv->ctx.cmd_bounce);
	if (dev_priv->capabilities & SVGA_CAP_IRQMASK)
		drm_irq_uninstall(dev_priv->dev);
	if (dev_priv->enable_fb) {
		vmw_fb_close(dev_priv);
		vmw_kms_restore_vga(dev_priv);
		vmw_3d_resource_dec(dev_priv, false);
	}
	vmw_kms_close(dev_priv);
	vmw_overlay_close(dev_priv);
	vmw_fence_manager_takedown(dev_priv->fman);
	if (dev_priv->stealth)
		pci_release_region(dev->pdev, 2);
	else
		pci_release_regions(dev->pdev);

	ttm_object_device_release(&dev_priv->tdev);
	iounmap(dev_priv->mmio_virt);
	drm_mtrr_del(dev_priv->mmio_mtrr, dev_priv->mmio_start,
		     dev_priv->mmio_size, DRM_MTRR_WC);
	if (dev_priv->has_gmr)
		(void)ttm_bo_clean_mm(&dev_priv->bdev, VMW_PL_GMR);
	(void)ttm_bo_clean_mm(&dev_priv->bdev, TTM_PL_VRAM);
	(void)ttm_bo_device_release(&dev_priv->bdev);
	vmw_ttm_global_release(dev_priv);
	idr_destroy(&dev_priv->surface_idr);
	idr_destroy(&dev_priv->context_idr);
	idr_destroy(&dev_priv->stream_idr);

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
	ttm_object_file_release(&vmw_fp->tfile);
	if (vmw_fp->locked_master)
		drm_master_put(&vmw_fp->locked_master);
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
	dev_priv->bdev.dev_mapping = dev->dev_mapping;

	return 0;

out_no_tfile:
	kfree(vmw_fp);
	return ret;
}

static long vmw_unlocked_ioctl(struct file *filp, unsigned int cmd,
			       unsigned long arg)
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	unsigned int nr = DRM_IOCTL_NR(cmd);

	/*
	 * Do extra checking on driver private ioctls.
	 */

	if ((nr >= DRM_COMMAND_BASE) && (nr < DRM_COMMAND_END)
	    && (nr < DRM_COMMAND_BASE + dev->driver->num_ioctls)) {
		struct drm_ioctl_desc *ioctl =
		    &vmw_ioctls[nr - DRM_COMMAND_BASE];

		if (unlikely(ioctl->cmd_drv != cmd)) {
			DRM_ERROR("Invalid command format, ioctl %d\n",
				  nr - DRM_COMMAND_BASE);
			return -EINVAL;
		}
	}

	return drm_ioctl(filp, cmd, arg);
}

static int vmw_firstopen(struct drm_device *dev)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	dev_priv->is_opened = true;

	return 0;
}

static void vmw_lastclose(struct drm_device *dev)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct drm_crtc *crtc;
	struct drm_mode_set set;
	int ret;

	/**
	 * Do nothing on the lastclose call from drm_unload.
	 */

	if (!dev_priv->is_opened)
		return;

	dev_priv->is_opened = false;
	set.x = 0;
	set.y = 0;
	set.fb = NULL;
	set.mode = NULL;
	set.connectors = NULL;
	set.num_connectors = 0;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		set.crtc = crtc;
		ret = crtc->funcs->set_config(&set);
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
		mutex_lock(&dev_priv->hw_mutex);
		vmw_write(dev_priv, SVGA_REG_TRACES, 1);
		mutex_unlock(&dev_priv->hw_mutex);
		vmw_kms_restore_vga(dev_priv);
		vmw_3d_resource_dec(dev_priv, true);
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
	vmw_execbuf_release_pinned_bo(dev_priv, false, 0);

	if (unlikely((ret != 0))) {
		DRM_ERROR("Unable to lock TTM at VT switch.\n");
		drm_master_put(&vmw_fp->locked_master);
	}

	ttm_lock_set_kill(&vmaster->lock, true, SIGTERM);

	if (!dev_priv->enable_fb) {
		ret = ttm_bo_evict_mm(&dev_priv->bdev, TTM_PL_VRAM);
		if (unlikely(ret != 0))
			DRM_ERROR("Unable to clean VRAM on master drop.\n");
		mutex_lock(&dev_priv->hw_mutex);
		vmw_write(dev_priv, SVGA_REG_TRACES, 1);
		mutex_unlock(&dev_priv->hw_mutex);
		vmw_kms_restore_vga(dev_priv);
		vmw_3d_resource_dec(dev_priv, true);
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
	struct vmw_master *vmaster = dev_priv->active_master;

	switch (val) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		ttm_suspend_lock(&vmaster->lock);

		/**
		 * This empties VRAM and unbinds all GMR bindings.
		 * Buffer contents is moved to swappable memory.
		 */
		vmw_execbuf_release_pinned_bo(dev_priv, false, 0);
		ttm_bo_swapout_all(&dev_priv->bdev);

		break;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
	case PM_POST_RESTORE:
		ttm_suspend_unlock(&vmaster->lock);

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
	.fasync = drm_fasync,
#if defined(CONFIG_COMPAT)
	.compat_ioctl = drm_compat_ioctl,
#endif
	.llseek = noop_llseek,
};

static struct drm_driver driver = {
	.driver_features = DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED |
	DRIVER_MODESET,
	.load = vmw_driver_load,
	.unload = vmw_driver_unload,
	.firstopen = vmw_firstopen,
	.lastclose = vmw_lastclose,
	.irq_preinstall = vmw_irq_preinstall,
	.irq_postinstall = vmw_irq_postinstall,
	.irq_uninstall = vmw_irq_uninstall,
	.irq_handler = vmw_irq_handler,
	.get_vblank_counter = vmw_get_vblank_counter,
	.enable_vblank = vmw_enable_vblank,
	.disable_vblank = vmw_disable_vblank,
	.ioctls = vmw_ioctls,
	.num_ioctls = DRM_ARRAY_SIZE(vmw_ioctls),
	.dma_quiescent = NULL,	/*vmw_dma_quiescent, */
	.master_create = vmw_master_create,
	.master_destroy = vmw_master_destroy,
	.master_set = vmw_master_set,
	.master_drop = vmw_master_drop,
	.open = vmw_driver_open,
	.preclose = vmw_preclose,
	.postclose = vmw_postclose,

	.dumb_create = vmw_dumb_create,
	.dumb_map_offset = vmw_dumb_map_offset,
	.dumb_destroy = vmw_dumb_destroy,

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
