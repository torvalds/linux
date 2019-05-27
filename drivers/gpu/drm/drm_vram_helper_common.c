// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>

/**
 * DOC: overview
 *
 * This library provides &struct drm_gem_vram_object (GEM VRAM), a GEM
 * buffer object that is backed by video RAM. It can be used for
 * framebuffer devices with dedicated memory. The video RAM can be
 * managed with &struct drm_vram_mm (VRAM MM). Both data structures are
 * supposed to be used together, but can also be used individually.
 *
 * With the GEM interface userspace applications create, manage and destroy
 * graphics buffers, such as an on-screen framebuffer. GEM does not provide
 * an implementation of these interfaces. It's up to the DRM driver to
 * provide an implementation that suits the hardware. If the hardware device
 * contains dedicated video memory, the DRM driver can use the VRAM helper
 * library. Each active buffer object is stored in video RAM. Active
 * buffer are used for drawing the current frame, typically something like
 * the frame's scanout buffer or the cursor image. If there's no more space
 * left in VRAM, inactive GEM objects can be moved to system memory.
 *
 * The easiest way to use the VRAM helper library is to call
 * drm_vram_helper_alloc_mm(). The function allocates and initializes an
 * instance of &struct drm_vram_mm in &struct drm_device.vram_mm . Use
 * &DRM_GEM_VRAM_DRIVER to initialize &struct drm_driver and
 * &DRM_VRAM_MM_FILE_OPERATIONS to initialize &struct file_operations;
 * as illustrated below.
 *
 * .. code-block:: c
 *
 *	struct file_operations fops ={
 *		.owner = THIS_MODULE,
 *		DRM_VRAM_MM_FILE_OPERATION
 *	};
 *	struct drm_driver drv = {
 *		.driver_feature = DRM_ ... ,
 *		.fops = &fops,
 *		DRM_GEM_VRAM_DRIVER
 *	};
 *
 *	int init_drm_driver()
 *	{
 *		struct drm_device *dev;
 *		uint64_t vram_base;
 *		unsigned long vram_size;
 *		int ret;
 *
 *		// setup device, vram base and size
 *		// ...
 *
 *		ret = drm_vram_helper_alloc_mm(dev, vram_base, vram_size,
 *					       &drm_gem_vram_mm_funcs);
 *		if (ret)
 *			return ret;
 *		return 0;
 *	}
 *
 * This creates an instance of &struct drm_vram_mm, exports DRM userspace
 * interfaces for GEM buffer management and initializes file operations to
 * allow for accessing created GEM buffers. With this setup, the DRM driver
 * manages an area of video RAM with VRAM MM and provides GEM VRAM objects
 * to userspace.
 *
 * To clean up the VRAM memory management, call drm_vram_helper_release_mm()
 * in the driver's clean-up code.
 *
 * .. code-block:: c
 *
 *	void fini_drm_driver()
 *	{
 *		struct drm_device *dev = ...;
 *
 *		drm_vram_helper_release_mm(dev);
 *	}
 *
 * For drawing or scanout operations, buffer object have to be pinned in video
 * RAM. Call drm_gem_vram_pin() with &DRM_GEM_VRAM_PL_FLAG_VRAM or
 * &DRM_GEM_VRAM_PL_FLAG_SYSTEM to pin a buffer object in video RAM or system
 * memory. Call drm_gem_vram_unpin() to release the pinned object afterwards.
 *
 * A buffer object that is pinned in video RAM has a fixed address within that
 * memory region. Call drm_gem_vram_offset() to retrieve this value. Typically
 * it's used to program the hardware's scanout engine for framebuffers, set
 * the cursor overlay's image for a mouse cursor, or use it as input to the
 * hardware's draing engine.
 *
 * To access a buffer object's memory from the DRM driver, call
 * drm_gem_vram_kmap(). It (optionally) maps the buffer into kernel address
 * space and returns the memory address. Use drm_gem_vram_kunmap() to
 * release the mapping.
 */

MODULE_DESCRIPTION("DRM VRAM memory-management helpers");
MODULE_LICENSE("GPL");
