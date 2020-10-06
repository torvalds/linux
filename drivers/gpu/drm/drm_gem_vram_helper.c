// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_ttm_helper.h>
#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mode.h>
#include <drm/drm_plane.h>
#include <drm/drm_prime.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/ttm/ttm_page_alloc.h>

static const struct drm_gem_object_funcs drm_gem_vram_object_funcs;

/**
 * DOC: overview
 *
 * This library provides &struct drm_gem_vram_object (GEM VRAM), a GEM
 * buffer object that is backed by video RAM (VRAM). It can be used for
 * framebuffer devices with dedicated memory.
 *
 * The data structure &struct drm_vram_mm and its helpers implement a memory
 * manager for simple framebuffer devices with dedicated video memory. GEM
 * VRAM buffer objects are either placed in the video memory or remain evicted
 * to system memory.
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
 * To initialize the VRAM helper library call drmm_vram_helper_alloc_mm().
 * The function allocates and initializes an instance of &struct drm_vram_mm
 * in &struct drm_device.vram_mm . Use &DRM_GEM_VRAM_DRIVER to initialize
 * &struct drm_driver and  &DRM_VRAM_MM_FILE_OPERATIONS to initialize
 * &struct file_operations; as illustrated below.
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
 *		ret = drmm_vram_helper_alloc_mm(dev, vram_base, vram_size);
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
 * You don't have to clean up the instance of VRAM MM.
 * drmm_vram_helper_alloc_mm() is a managed interface that installs a
 * clean-up handler to run during the DRM device's release.
 *
 * For drawing or scanout operations, rsp. buffer objects have to be pinned
 * in video RAM. Call drm_gem_vram_pin() with &DRM_GEM_VRAM_PL_FLAG_VRAM or
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
 * drm_gem_vram_vmap(). It maps the buffer into kernel address
 * space and returns the memory address. Use drm_gem_vram_vunmap() to
 * release the mapping.
 */

/*
 * Buffer-objects helpers
 */

static void drm_gem_vram_cleanup(struct drm_gem_vram_object *gbo)
{
	/* We got here via ttm_bo_put(), which means that the
	 * TTM buffer object in 'bo' has already been cleaned
	 * up; only release the GEM object.
	 */

	WARN_ON(gbo->kmap_use_count);
	WARN_ON(gbo->kmap.virtual);

	drm_gem_object_release(&gbo->bo.base);
}

static void drm_gem_vram_destroy(struct drm_gem_vram_object *gbo)
{
	drm_gem_vram_cleanup(gbo);
	kfree(gbo);
}

static void ttm_buffer_object_destroy(struct ttm_buffer_object *bo)
{
	struct drm_gem_vram_object *gbo = drm_gem_vram_of_bo(bo);

	drm_gem_vram_destroy(gbo);
}

static void drm_gem_vram_placement(struct drm_gem_vram_object *gbo,
				   unsigned long pl_flag)
{
	u32 invariant_flags = 0;
	unsigned int i;
	unsigned int c = 0;

	if (pl_flag & DRM_GEM_VRAM_PL_FLAG_TOPDOWN)
		invariant_flags = TTM_PL_FLAG_TOPDOWN;

	gbo->placement.placement = gbo->placements;
	gbo->placement.busy_placement = gbo->placements;

	if (pl_flag & DRM_GEM_VRAM_PL_FLAG_VRAM) {
		gbo->placements[c].mem_type = TTM_PL_VRAM;
		gbo->placements[c++].flags = TTM_PL_FLAG_WC |
					     TTM_PL_FLAG_UNCACHED |
					     invariant_flags;
	}

	if (pl_flag & DRM_GEM_VRAM_PL_FLAG_SYSTEM || !c) {
		gbo->placements[c].mem_type = TTM_PL_SYSTEM;
		gbo->placements[c++].flags = TTM_PL_MASK_CACHING |
					     invariant_flags;
	}

	gbo->placement.num_placement = c;
	gbo->placement.num_busy_placement = c;

	for (i = 0; i < c; ++i) {
		gbo->placements[i].fpfn = 0;
		gbo->placements[i].lpfn = 0;
	}
}

/**
 * drm_gem_vram_create() - Creates a VRAM-backed GEM object
 * @dev:		the DRM device
 * @size:		the buffer size in bytes
 * @pg_align:		the buffer's alignment in multiples of the page size
 *
 * GEM objects are allocated by calling struct drm_driver.gem_create_object,
 * if set. Otherwise kzalloc() will be used. Drivers can set their own GEM
 * object functions in struct drm_driver.gem_create_object. If no functions
 * are set, the new GEM object will use the default functions from GEM VRAM
 * helpers.
 *
 * Returns:
 * A new instance of &struct drm_gem_vram_object on success, or
 * an ERR_PTR()-encoded error code otherwise.
 */
struct drm_gem_vram_object *drm_gem_vram_create(struct drm_device *dev,
						size_t size,
						unsigned long pg_align)
{
	struct drm_gem_vram_object *gbo;
	struct drm_gem_object *gem;
	struct drm_vram_mm *vmm = dev->vram_mm;
	struct ttm_bo_device *bdev;
	int ret;
	size_t acc_size;

	if (WARN_ONCE(!vmm, "VRAM MM not initialized"))
		return ERR_PTR(-EINVAL);

	if (dev->driver->gem_create_object) {
		gem = dev->driver->gem_create_object(dev, size);
		if (!gem)
			return ERR_PTR(-ENOMEM);
		gbo = drm_gem_vram_of_gem(gem);
	} else {
		gbo = kzalloc(sizeof(*gbo), GFP_KERNEL);
		if (!gbo)
			return ERR_PTR(-ENOMEM);
		gem = &gbo->bo.base;
	}

	if (!gem->funcs)
		gem->funcs = &drm_gem_vram_object_funcs;

	ret = drm_gem_object_init(dev, gem, size);
	if (ret) {
		kfree(gbo);
		return ERR_PTR(ret);
	}

	bdev = &vmm->bdev;
	acc_size = ttm_bo_dma_acc_size(bdev, size, sizeof(*gbo));

	gbo->bo.bdev = bdev;
	drm_gem_vram_placement(gbo, DRM_GEM_VRAM_PL_FLAG_SYSTEM);

	/*
	 * A failing ttm_bo_init will call ttm_buffer_object_destroy
	 * to release gbo->bo.base and kfree gbo.
	 */
	ret = ttm_bo_init(bdev, &gbo->bo, size, ttm_bo_type_device,
			  &gbo->placement, pg_align, false, acc_size,
			  NULL, NULL, ttm_buffer_object_destroy);
	if (ret)
		return ERR_PTR(ret);

	return gbo;
}
EXPORT_SYMBOL(drm_gem_vram_create);

/**
 * drm_gem_vram_put() - Releases a reference to a VRAM-backed GEM object
 * @gbo:	the GEM VRAM object
 *
 * See ttm_bo_put() for more information.
 */
void drm_gem_vram_put(struct drm_gem_vram_object *gbo)
{
	ttm_bo_put(&gbo->bo);
}
EXPORT_SYMBOL(drm_gem_vram_put);

/**
 * drm_gem_vram_mmap_offset() - Returns a GEM VRAM object's mmap offset
 * @gbo:	the GEM VRAM object
 *
 * See drm_vma_node_offset_addr() for more information.
 *
 * Returns:
 * The buffer object's offset for userspace mappings on success, or
 * 0 if no offset is allocated.
 */
u64 drm_gem_vram_mmap_offset(struct drm_gem_vram_object *gbo)
{
	return drm_vma_node_offset_addr(&gbo->bo.base.vma_node);
}
EXPORT_SYMBOL(drm_gem_vram_mmap_offset);

static u64 drm_gem_vram_pg_offset(struct drm_gem_vram_object *gbo)
{
	/* Keep TTM behavior for now, remove when drivers are audited */
	if (WARN_ON_ONCE(!gbo->bo.mem.mm_node))
		return 0;

	return gbo->bo.mem.start;
}

/**
 * drm_gem_vram_offset() - \
	Returns a GEM VRAM object's offset in video memory
 * @gbo:	the GEM VRAM object
 *
 * This function returns the buffer object's offset in the device's video
 * memory. The buffer object has to be pinned to %TTM_PL_VRAM.
 *
 * Returns:
 * The buffer object's offset in video memory on success, or
 * a negative errno code otherwise.
 */
s64 drm_gem_vram_offset(struct drm_gem_vram_object *gbo)
{
	if (WARN_ON_ONCE(!gbo->bo.pin_count))
		return (s64)-ENODEV;
	return drm_gem_vram_pg_offset(gbo) << PAGE_SHIFT;
}
EXPORT_SYMBOL(drm_gem_vram_offset);

static int drm_gem_vram_pin_locked(struct drm_gem_vram_object *gbo,
				   unsigned long pl_flag)
{
	struct ttm_operation_ctx ctx = { false, false };
	int ret;

	if (gbo->bo.pin_count)
		goto out;

	if (pl_flag)
		drm_gem_vram_placement(gbo, pl_flag);

	ret = ttm_bo_validate(&gbo->bo, &gbo->placement, &ctx);
	if (ret < 0)
		return ret;

out:
	ttm_bo_pin(&gbo->bo);

	return 0;
}

/**
 * drm_gem_vram_pin() - Pins a GEM VRAM object in a region.
 * @gbo:	the GEM VRAM object
 * @pl_flag:	a bitmask of possible memory regions
 *
 * Pinning a buffer object ensures that it is not evicted from
 * a memory region. A pinned buffer object has to be unpinned before
 * it can be pinned to another region. If the pl_flag argument is 0,
 * the buffer is pinned at its current location (video RAM or system
 * memory).
 *
 * Small buffer objects, such as cursor images, can lead to memory
 * fragmentation if they are pinned in the middle of video RAM. This
 * is especially a problem on devices with only a small amount of
 * video RAM. Fragmentation can prevent the primary framebuffer from
 * fitting in, even though there's enough memory overall. The modifier
 * DRM_GEM_VRAM_PL_FLAG_TOPDOWN marks the buffer object to be pinned
 * at the high end of the memory region to avoid fragmentation.
 *
 * Returns:
 * 0 on success, or
 * a negative error code otherwise.
 */
int drm_gem_vram_pin(struct drm_gem_vram_object *gbo, unsigned long pl_flag)
{
	int ret;

	ret = ttm_bo_reserve(&gbo->bo, true, false, NULL);
	if (ret)
		return ret;
	ret = drm_gem_vram_pin_locked(gbo, pl_flag);
	ttm_bo_unreserve(&gbo->bo);

	return ret;
}
EXPORT_SYMBOL(drm_gem_vram_pin);

static void drm_gem_vram_unpin_locked(struct drm_gem_vram_object *gbo)
{
	ttm_bo_unpin(&gbo->bo);
}

/**
 * drm_gem_vram_unpin() - Unpins a GEM VRAM object
 * @gbo:	the GEM VRAM object
 *
 * Returns:
 * 0 on success, or
 * a negative error code otherwise.
 */
int drm_gem_vram_unpin(struct drm_gem_vram_object *gbo)
{
	int ret;

	ret = ttm_bo_reserve(&gbo->bo, true, false, NULL);
	if (ret)
		return ret;

	drm_gem_vram_unpin_locked(gbo);
	ttm_bo_unreserve(&gbo->bo);

	return 0;
}
EXPORT_SYMBOL(drm_gem_vram_unpin);

static void *drm_gem_vram_kmap_locked(struct drm_gem_vram_object *gbo,
				      bool map, bool *is_iomem)
{
	int ret;
	struct ttm_bo_kmap_obj *kmap = &gbo->kmap;

	if (gbo->kmap_use_count > 0)
		goto out;

	if (kmap->virtual || !map)
		goto out;

	ret = ttm_bo_kmap(&gbo->bo, 0, gbo->bo.num_pages, kmap);
	if (ret)
		return ERR_PTR(ret);

out:
	if (!kmap->virtual) {
		if (is_iomem)
			*is_iomem = false;
		return NULL; /* not mapped; don't increment ref */
	}
	++gbo->kmap_use_count;
	if (is_iomem)
		return ttm_kmap_obj_virtual(kmap, is_iomem);
	return kmap->virtual;
}

static void drm_gem_vram_kunmap_locked(struct drm_gem_vram_object *gbo)
{
	if (WARN_ON_ONCE(!gbo->kmap_use_count))
		return;
	if (--gbo->kmap_use_count > 0)
		return;

	/*
	 * Permanently mapping and unmapping buffers adds overhead from
	 * updating the page tables and creates debugging output. Therefore,
	 * we delay the actual unmap operation until the BO gets evicted
	 * from memory. See drm_gem_vram_bo_driver_move_notify().
	 */
}

/**
 * drm_gem_vram_vmap() - Pins and maps a GEM VRAM object into kernel address
 *                       space
 * @gbo:	The GEM VRAM object to map
 *
 * The vmap function pins a GEM VRAM object to its current location, either
 * system or video memory, and maps its buffer into kernel address space.
 * As pinned object cannot be relocated, you should avoid pinning objects
 * permanently. Call drm_gem_vram_vunmap() with the returned address to
 * unmap and unpin the GEM VRAM object.
 *
 * Returns:
 * The buffer's virtual address on success, or
 * an ERR_PTR()-encoded error code otherwise.
 */
void *drm_gem_vram_vmap(struct drm_gem_vram_object *gbo)
{
	int ret;
	void *base;

	ret = ttm_bo_reserve(&gbo->bo, true, false, NULL);
	if (ret)
		return ERR_PTR(ret);

	ret = drm_gem_vram_pin_locked(gbo, 0);
	if (ret)
		goto err_ttm_bo_unreserve;
	base = drm_gem_vram_kmap_locked(gbo, true, NULL);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto err_drm_gem_vram_unpin_locked;
	}

	ttm_bo_unreserve(&gbo->bo);

	return base;

err_drm_gem_vram_unpin_locked:
	drm_gem_vram_unpin_locked(gbo);
err_ttm_bo_unreserve:
	ttm_bo_unreserve(&gbo->bo);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(drm_gem_vram_vmap);

/**
 * drm_gem_vram_vunmap() - Unmaps and unpins a GEM VRAM object
 * @gbo:	The GEM VRAM object to unmap
 * @vaddr:	The mapping's base address as returned by drm_gem_vram_vmap()
 *
 * A call to drm_gem_vram_vunmap() unmaps and unpins a GEM VRAM buffer. See
 * the documentation for drm_gem_vram_vmap() for more information.
 */
void drm_gem_vram_vunmap(struct drm_gem_vram_object *gbo, void *vaddr)
{
	int ret;

	ret = ttm_bo_reserve(&gbo->bo, false, false, NULL);
	if (WARN_ONCE(ret, "ttm_bo_reserve_failed(): ret=%d\n", ret))
		return;

	drm_gem_vram_kunmap_locked(gbo);
	drm_gem_vram_unpin_locked(gbo);

	ttm_bo_unreserve(&gbo->bo);
}
EXPORT_SYMBOL(drm_gem_vram_vunmap);

/**
 * drm_gem_vram_fill_create_dumb() - \
	Helper for implementing &struct drm_driver.dumb_create
 * @file:		the DRM file
 * @dev:		the DRM device
 * @pg_align:		the buffer's alignment in multiples of the page size
 * @pitch_align:	the scanline's alignment in powers of 2
 * @args:		the arguments as provided to \
				&struct drm_driver.dumb_create
 *
 * This helper function fills &struct drm_mode_create_dumb, which is used
 * by &struct drm_driver.dumb_create. Implementations of this interface
 * should forwards their arguments to this helper, plus the driver-specific
 * parameters.
 *
 * Returns:
 * 0 on success, or
 * a negative error code otherwise.
 */
int drm_gem_vram_fill_create_dumb(struct drm_file *file,
				  struct drm_device *dev,
				  unsigned long pg_align,
				  unsigned long pitch_align,
				  struct drm_mode_create_dumb *args)
{
	size_t pitch, size;
	struct drm_gem_vram_object *gbo;
	int ret;
	u32 handle;

	pitch = args->width * DIV_ROUND_UP(args->bpp, 8);
	if (pitch_align) {
		if (WARN_ON_ONCE(!is_power_of_2(pitch_align)))
			return -EINVAL;
		pitch = ALIGN(pitch, pitch_align);
	}
	size = pitch * args->height;

	size = roundup(size, PAGE_SIZE);
	if (!size)
		return -EINVAL;

	gbo = drm_gem_vram_create(dev, size, pg_align);
	if (IS_ERR(gbo))
		return PTR_ERR(gbo);

	ret = drm_gem_handle_create(file, &gbo->bo.base, &handle);
	if (ret)
		goto err_drm_gem_object_put;

	drm_gem_object_put(&gbo->bo.base);

	args->pitch = pitch;
	args->size = size;
	args->handle = handle;

	return 0;

err_drm_gem_object_put:
	drm_gem_object_put(&gbo->bo.base);
	return ret;
}
EXPORT_SYMBOL(drm_gem_vram_fill_create_dumb);

/*
 * Helpers for struct ttm_bo_driver
 */

static bool drm_is_gem_vram(struct ttm_buffer_object *bo)
{
	return (bo->destroy == ttm_buffer_object_destroy);
}

static void drm_gem_vram_bo_driver_evict_flags(struct drm_gem_vram_object *gbo,
					       struct ttm_placement *pl)
{
	drm_gem_vram_placement(gbo, DRM_GEM_VRAM_PL_FLAG_SYSTEM);
	*pl = gbo->placement;
}

static void drm_gem_vram_bo_driver_move_notify(struct drm_gem_vram_object *gbo,
					       bool evict,
					       struct ttm_resource *new_mem)
{
	struct ttm_bo_kmap_obj *kmap = &gbo->kmap;

	if (WARN_ON_ONCE(gbo->kmap_use_count))
		return;

	if (!kmap->virtual)
		return;
	ttm_bo_kunmap(kmap);
	kmap->virtual = NULL;
}

static int drm_gem_vram_bo_driver_move(struct drm_gem_vram_object *gbo,
				       bool evict,
				       struct ttm_operation_ctx *ctx,
				       struct ttm_resource *new_mem)
{
	return ttm_bo_move_memcpy(&gbo->bo, ctx, new_mem);
}

/*
 * Helpers for struct drm_gem_object_funcs
 */

/**
 * drm_gem_vram_object_free() - \
	Implements &struct drm_gem_object_funcs.free
 * @gem:       GEM object. Refers to &struct drm_gem_vram_object.gem
 */
static void drm_gem_vram_object_free(struct drm_gem_object *gem)
{
	struct drm_gem_vram_object *gbo = drm_gem_vram_of_gem(gem);

	drm_gem_vram_put(gbo);
}

/*
 * Helpers for dump buffers
 */

/**
 * drm_gem_vram_driver_create_dumb() - \
	Implements &struct drm_driver.dumb_create
 * @file:		the DRM file
 * @dev:		the DRM device
 * @args:		the arguments as provided to \
				&struct drm_driver.dumb_create
 *
 * This function requires the driver to use @drm_device.vram_mm for its
 * instance of VRAM MM.
 *
 * Returns:
 * 0 on success, or
 * a negative error code otherwise.
 */
int drm_gem_vram_driver_dumb_create(struct drm_file *file,
				    struct drm_device *dev,
				    struct drm_mode_create_dumb *args)
{
	if (WARN_ONCE(!dev->vram_mm, "VRAM MM not initialized"))
		return -EINVAL;

	return drm_gem_vram_fill_create_dumb(file, dev, 0, 0, args);
}
EXPORT_SYMBOL(drm_gem_vram_driver_dumb_create);

/**
 * drm_gem_vram_driver_dumb_mmap_offset() - \
	Implements &struct drm_driver.dumb_mmap_offset
 * @file:	DRM file pointer.
 * @dev:	DRM device.
 * @handle:	GEM handle
 * @offset:	Returns the mapping's memory offset on success
 *
 * Returns:
 * 0 on success, or
 * a negative errno code otherwise.
 */
int drm_gem_vram_driver_dumb_mmap_offset(struct drm_file *file,
					 struct drm_device *dev,
					 uint32_t handle, uint64_t *offset)
{
	struct drm_gem_object *gem;
	struct drm_gem_vram_object *gbo;

	gem = drm_gem_object_lookup(file, handle);
	if (!gem)
		return -ENOENT;

	gbo = drm_gem_vram_of_gem(gem);
	*offset = drm_gem_vram_mmap_offset(gbo);

	drm_gem_object_put(gem);

	return 0;
}
EXPORT_SYMBOL(drm_gem_vram_driver_dumb_mmap_offset);

/*
 * Helpers for struct drm_plane_helper_funcs
 */

/**
 * drm_gem_vram_plane_helper_prepare_fb() - \
 *	Implements &struct drm_plane_helper_funcs.prepare_fb
 * @plane:	a DRM plane
 * @new_state:	the plane's new state
 *
 * During plane updates, this function sets the plane's fence and
 * pins the GEM VRAM objects of the plane's new framebuffer to VRAM.
 * Call drm_gem_vram_plane_helper_cleanup_fb() to unpin them.
 *
 * Returns:
 *	0 on success, or
 *	a negative errno code otherwise.
 */
int
drm_gem_vram_plane_helper_prepare_fb(struct drm_plane *plane,
				     struct drm_plane_state *new_state)
{
	size_t i;
	struct drm_gem_vram_object *gbo;
	int ret;

	if (!new_state->fb)
		return 0;

	for (i = 0; i < ARRAY_SIZE(new_state->fb->obj); ++i) {
		if (!new_state->fb->obj[i])
			continue;
		gbo = drm_gem_vram_of_gem(new_state->fb->obj[i]);
		ret = drm_gem_vram_pin(gbo, DRM_GEM_VRAM_PL_FLAG_VRAM);
		if (ret)
			goto err_drm_gem_vram_unpin;
	}

	ret = drm_gem_fb_prepare_fb(plane, new_state);
	if (ret)
		goto err_drm_gem_vram_unpin;

	return 0;

err_drm_gem_vram_unpin:
	while (i) {
		--i;
		gbo = drm_gem_vram_of_gem(new_state->fb->obj[i]);
		drm_gem_vram_unpin(gbo);
	}
	return ret;
}
EXPORT_SYMBOL(drm_gem_vram_plane_helper_prepare_fb);

/**
 * drm_gem_vram_plane_helper_cleanup_fb() - \
 *	Implements &struct drm_plane_helper_funcs.cleanup_fb
 * @plane:	a DRM plane
 * @old_state:	the plane's old state
 *
 * During plane updates, this function unpins the GEM VRAM
 * objects of the plane's old framebuffer from VRAM. Complements
 * drm_gem_vram_plane_helper_prepare_fb().
 */
void
drm_gem_vram_plane_helper_cleanup_fb(struct drm_plane *plane,
				     struct drm_plane_state *old_state)
{
	size_t i;
	struct drm_gem_vram_object *gbo;

	if (!old_state->fb)
		return;

	for (i = 0; i < ARRAY_SIZE(old_state->fb->obj); ++i) {
		if (!old_state->fb->obj[i])
			continue;
		gbo = drm_gem_vram_of_gem(old_state->fb->obj[i]);
		drm_gem_vram_unpin(gbo);
	}
}
EXPORT_SYMBOL(drm_gem_vram_plane_helper_cleanup_fb);

/*
 * Helpers for struct drm_simple_display_pipe_funcs
 */

/**
 * drm_gem_vram_simple_display_pipe_prepare_fb() - \
 *	Implements &struct drm_simple_display_pipe_funcs.prepare_fb
 * @pipe:	a simple display pipe
 * @new_state:	the plane's new state
 *
 * During plane updates, this function pins the GEM VRAM
 * objects of the plane's new framebuffer to VRAM. Call
 * drm_gem_vram_simple_display_pipe_cleanup_fb() to unpin them.
 *
 * Returns:
 *	0 on success, or
 *	a negative errno code otherwise.
 */
int drm_gem_vram_simple_display_pipe_prepare_fb(
	struct drm_simple_display_pipe *pipe,
	struct drm_plane_state *new_state)
{
	return drm_gem_vram_plane_helper_prepare_fb(&pipe->plane, new_state);
}
EXPORT_SYMBOL(drm_gem_vram_simple_display_pipe_prepare_fb);

/**
 * drm_gem_vram_simple_display_pipe_cleanup_fb() - \
 *	Implements &struct drm_simple_display_pipe_funcs.cleanup_fb
 * @pipe:	a simple display pipe
 * @old_state:	the plane's old state
 *
 * During plane updates, this function unpins the GEM VRAM
 * objects of the plane's old framebuffer from VRAM. Complements
 * drm_gem_vram_simple_display_pipe_prepare_fb().
 */
void drm_gem_vram_simple_display_pipe_cleanup_fb(
	struct drm_simple_display_pipe *pipe,
	struct drm_plane_state *old_state)
{
	drm_gem_vram_plane_helper_cleanup_fb(&pipe->plane, old_state);
}
EXPORT_SYMBOL(drm_gem_vram_simple_display_pipe_cleanup_fb);

/*
 * PRIME helpers
 */

/**
 * drm_gem_vram_object_pin() - \
	Implements &struct drm_gem_object_funcs.pin
 * @gem:	The GEM object to pin
 *
 * Returns:
 * 0 on success, or
 * a negative errno code otherwise.
 */
static int drm_gem_vram_object_pin(struct drm_gem_object *gem)
{
	struct drm_gem_vram_object *gbo = drm_gem_vram_of_gem(gem);

	/* Fbdev console emulation is the use case of these PRIME
	 * helpers. This may involve updating a hardware buffer from
	 * a shadow FB. We pin the buffer to it's current location
	 * (either video RAM or system memory) to prevent it from
	 * being relocated during the update operation. If you require
	 * the buffer to be pinned to VRAM, implement a callback that
	 * sets the flags accordingly.
	 */
	return drm_gem_vram_pin(gbo, 0);
}

/**
 * drm_gem_vram_object_unpin() - \
	Implements &struct drm_gem_object_funcs.unpin
 * @gem:	The GEM object to unpin
 */
static void drm_gem_vram_object_unpin(struct drm_gem_object *gem)
{
	struct drm_gem_vram_object *gbo = drm_gem_vram_of_gem(gem);

	drm_gem_vram_unpin(gbo);
}

/**
 * drm_gem_vram_object_vmap() - \
	Implements &struct drm_gem_object_funcs.vmap
 * @gem:	The GEM object to map
 *
 * Returns:
 * The buffers virtual address on success, or
 * NULL otherwise.
 */
static void *drm_gem_vram_object_vmap(struct drm_gem_object *gem)
{
	struct drm_gem_vram_object *gbo = drm_gem_vram_of_gem(gem);
	void *base;

	base = drm_gem_vram_vmap(gbo);
	if (IS_ERR(base))
		return NULL;
	return base;
}

/**
 * drm_gem_vram_object_vunmap() - \
	Implements &struct drm_gem_object_funcs.vunmap
 * @gem:	The GEM object to unmap
 * @vaddr:	The mapping's base address
 */
static void drm_gem_vram_object_vunmap(struct drm_gem_object *gem,
				       void *vaddr)
{
	struct drm_gem_vram_object *gbo = drm_gem_vram_of_gem(gem);

	drm_gem_vram_vunmap(gbo, vaddr);
}

/*
 * GEM object funcs
 */

static const struct drm_gem_object_funcs drm_gem_vram_object_funcs = {
	.free	= drm_gem_vram_object_free,
	.pin	= drm_gem_vram_object_pin,
	.unpin	= drm_gem_vram_object_unpin,
	.vmap	= drm_gem_vram_object_vmap,
	.vunmap	= drm_gem_vram_object_vunmap,
	.mmap   = drm_gem_ttm_mmap,
	.print_info = drm_gem_ttm_print_info,
};

/*
 * VRAM memory manager
 */

/*
 * TTM TT
 */

static void bo_driver_ttm_tt_destroy(struct ttm_bo_device *bdev, struct ttm_tt *tt)
{
	ttm_tt_destroy_common(bdev, tt);
	ttm_tt_fini(tt);
	kfree(tt);
}

/*
 * TTM BO device
 */

static struct ttm_tt *bo_driver_ttm_tt_create(struct ttm_buffer_object *bo,
					      uint32_t page_flags)
{
	struct ttm_tt *tt;
	int ret;

	tt = kzalloc(sizeof(*tt), GFP_KERNEL);
	if (!tt)
		return NULL;

	ret = ttm_tt_init(tt, bo, page_flags);
	if (ret < 0)
		goto err_ttm_tt_init;

	return tt;

err_ttm_tt_init:
	kfree(tt);
	return NULL;
}

static void bo_driver_evict_flags(struct ttm_buffer_object *bo,
				  struct ttm_placement *placement)
{
	struct drm_gem_vram_object *gbo;

	/* TTM may pass BOs that are not GEM VRAM BOs. */
	if (!drm_is_gem_vram(bo))
		return;

	gbo = drm_gem_vram_of_bo(bo);

	drm_gem_vram_bo_driver_evict_flags(gbo, placement);
}

static void bo_driver_move_notify(struct ttm_buffer_object *bo,
				  bool evict,
				  struct ttm_resource *new_mem)
{
	struct drm_gem_vram_object *gbo;

	/* TTM may pass BOs that are not GEM VRAM BOs. */
	if (!drm_is_gem_vram(bo))
		return;

	gbo = drm_gem_vram_of_bo(bo);

	drm_gem_vram_bo_driver_move_notify(gbo, evict, new_mem);
}

static int bo_driver_move(struct ttm_buffer_object *bo,
			  bool evict,
			  struct ttm_operation_ctx *ctx,
			  struct ttm_resource *new_mem)
{
	struct drm_gem_vram_object *gbo;

	gbo = drm_gem_vram_of_bo(bo);

	return drm_gem_vram_bo_driver_move(gbo, evict, ctx, new_mem);
}

static int bo_driver_io_mem_reserve(struct ttm_bo_device *bdev,
				    struct ttm_resource *mem)
{
	struct drm_vram_mm *vmm = drm_vram_mm_of_bdev(bdev);

	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:	/* nothing to do */
		break;
	case TTM_PL_VRAM:
		mem->bus.offset = (mem->start << PAGE_SHIFT) + vmm->vram_base;
		mem->bus.is_iomem = true;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct ttm_bo_driver bo_driver = {
	.ttm_tt_create = bo_driver_ttm_tt_create,
	.ttm_tt_destroy = bo_driver_ttm_tt_destroy,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = bo_driver_evict_flags,
	.move = bo_driver_move,
	.move_notify = bo_driver_move_notify,
	.io_mem_reserve = bo_driver_io_mem_reserve,
};

/*
 * struct drm_vram_mm
 */

static int drm_vram_mm_debugfs(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_vram_mm *vmm = node->minor->dev->vram_mm;
	struct ttm_resource_manager *man = ttm_manager_type(&vmm->bdev, TTM_PL_VRAM);
	struct drm_printer p = drm_seq_file_printer(m);

	ttm_resource_manager_debug(man, &p);
	return 0;
}

static const struct drm_info_list drm_vram_mm_debugfs_list[] = {
	{ "vram-mm", drm_vram_mm_debugfs, 0, NULL },
};

/**
 * drm_vram_mm_debugfs_init() - Register VRAM MM debugfs file.
 *
 * @minor: drm minor device.
 *
 */
void drm_vram_mm_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(drm_vram_mm_debugfs_list,
				 ARRAY_SIZE(drm_vram_mm_debugfs_list),
				 minor->debugfs_root, minor);
}
EXPORT_SYMBOL(drm_vram_mm_debugfs_init);

static int drm_vram_mm_init(struct drm_vram_mm *vmm, struct drm_device *dev,
			    uint64_t vram_base, size_t vram_size)
{
	int ret;

	vmm->vram_base = vram_base;
	vmm->vram_size = vram_size;

	ret = ttm_bo_device_init(&vmm->bdev, &bo_driver,
				 dev->anon_inode->i_mapping,
				 dev->vma_offset_manager,
				 true);
	if (ret)
		return ret;

	ret = ttm_range_man_init(&vmm->bdev, TTM_PL_VRAM,
				 false, vram_size >> PAGE_SHIFT);
	if (ret)
		return ret;

	return 0;
}

static void drm_vram_mm_cleanup(struct drm_vram_mm *vmm)
{
	ttm_range_man_fini(&vmm->bdev, TTM_PL_VRAM);
	ttm_bo_device_release(&vmm->bdev);
}

/*
 * Helpers for integration with struct drm_device
 */

/* deprecated; use drmm_vram_mm_init() */
struct drm_vram_mm *drm_vram_helper_alloc_mm(
	struct drm_device *dev, uint64_t vram_base, size_t vram_size)
{
	int ret;

	if (WARN_ON(dev->vram_mm))
		return dev->vram_mm;

	dev->vram_mm = kzalloc(sizeof(*dev->vram_mm), GFP_KERNEL);
	if (!dev->vram_mm)
		return ERR_PTR(-ENOMEM);

	ret = drm_vram_mm_init(dev->vram_mm, dev, vram_base, vram_size);
	if (ret)
		goto err_kfree;

	return dev->vram_mm;

err_kfree:
	kfree(dev->vram_mm);
	dev->vram_mm = NULL;
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(drm_vram_helper_alloc_mm);

void drm_vram_helper_release_mm(struct drm_device *dev)
{
	if (!dev->vram_mm)
		return;

	drm_vram_mm_cleanup(dev->vram_mm);
	kfree(dev->vram_mm);
	dev->vram_mm = NULL;
}
EXPORT_SYMBOL(drm_vram_helper_release_mm);

static void drm_vram_mm_release(struct drm_device *dev, void *ptr)
{
	drm_vram_helper_release_mm(dev);
}

/**
 * drmm_vram_helper_init - Initializes a device's instance of
 *                         &struct drm_vram_mm
 * @dev:	the DRM device
 * @vram_base:	the base address of the video memory
 * @vram_size:	the size of the video memory in bytes
 *
 * Creates a new instance of &struct drm_vram_mm and stores it in
 * struct &drm_device.vram_mm. The instance is auto-managed and cleaned
 * up as part of device cleanup. Calling this function multiple times
 * will generate an error message.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise.
 */
int drmm_vram_helper_init(struct drm_device *dev, uint64_t vram_base,
			  size_t vram_size)
{
	struct drm_vram_mm *vram_mm;

	if (drm_WARN_ON_ONCE(dev, dev->vram_mm))
		return 0;

	vram_mm = drm_vram_helper_alloc_mm(dev, vram_base, vram_size);
	if (IS_ERR(vram_mm))
		return PTR_ERR(vram_mm);
	return drmm_add_action_or_reset(dev, drm_vram_mm_release, NULL);
}
EXPORT_SYMBOL(drmm_vram_helper_init);

/*
 * Mode-config helpers
 */

static enum drm_mode_status
drm_vram_helper_mode_valid_internal(struct drm_device *dev,
				    const struct drm_display_mode *mode,
				    unsigned long max_bpp)
{
	struct drm_vram_mm *vmm = dev->vram_mm;
	unsigned long fbsize, fbpages, max_fbpages;

	if (WARN_ON(!dev->vram_mm))
		return MODE_BAD;

	max_fbpages = (vmm->vram_size / 2) >> PAGE_SHIFT;

	fbsize = mode->hdisplay * mode->vdisplay * max_bpp;
	fbpages = DIV_ROUND_UP(fbsize, PAGE_SIZE);

	if (fbpages > max_fbpages)
		return MODE_MEM;

	return MODE_OK;
}

/**
 * drm_vram_helper_mode_valid - Tests if a display mode's
 *	framebuffer fits into the available video memory.
 * @dev:	the DRM device
 * @mode:	the mode to test
 *
 * This function tests if enough video memory is available for using the
 * specified display mode. Atomic modesetting requires importing the
 * designated framebuffer into video memory before evicting the active
 * one. Hence, any framebuffer may consume at most half of the available
 * VRAM. Display modes that require a larger framebuffer can not be used,
 * even if the CRTC does support them. Each framebuffer is assumed to
 * have 32-bit color depth.
 *
 * Note:
 * The function can only test if the display mode is supported in
 * general. If there are too many framebuffers pinned to video memory,
 * a display mode may still not be usable in practice. The color depth of
 * 32-bit fits all current use case. A more flexible test can be added
 * when necessary.
 *
 * Returns:
 * MODE_OK if the display mode is supported, or an error code of type
 * enum drm_mode_status otherwise.
 */
enum drm_mode_status
drm_vram_helper_mode_valid(struct drm_device *dev,
			   const struct drm_display_mode *mode)
{
	static const unsigned long max_bpp = 4; /* DRM_FORMAT_XRGB8888 */

	return drm_vram_helper_mode_valid_internal(dev, mode, max_bpp);
}
EXPORT_SYMBOL(drm_vram_helper_mode_valid);

MODULE_DESCRIPTION("DRM VRAM memory-management helpers");
MODULE_LICENSE("GPL");
