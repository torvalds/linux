// SPDX-License-Identifier: GPL-2.0-or-later

#include <drm/drm_gem_vram_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_mode.h>
#include <drm/drm_prime.h>
#include <drm/drm_vram_mm_helper.h>
#include <drm/ttm/ttm_page_alloc.h>

static const struct drm_gem_object_funcs drm_gem_vram_object_funcs;

/**
 * DOC: overview
 *
 * This library provides a GEM buffer object that is backed by video RAM
 * (VRAM). It can be used for framebuffer devices with dedicated memory.
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
	unsigned int i;
	unsigned int c = 0;

	gbo->placement.placement = gbo->placements;
	gbo->placement.busy_placement = gbo->placements;

	if (pl_flag & TTM_PL_FLAG_VRAM)
		gbo->placements[c++].flags = TTM_PL_FLAG_WC |
					     TTM_PL_FLAG_UNCACHED |
					     TTM_PL_FLAG_VRAM;

	if (pl_flag & TTM_PL_FLAG_SYSTEM)
		gbo->placements[c++].flags = TTM_PL_MASK_CACHING |
					     TTM_PL_FLAG_SYSTEM;

	if (!c)
		gbo->placements[c++].flags = TTM_PL_MASK_CACHING |
					     TTM_PL_FLAG_SYSTEM;

	gbo->placement.num_placement = c;
	gbo->placement.num_busy_placement = c;

	for (i = 0; i < c; ++i) {
		gbo->placements[i].fpfn = 0;
		gbo->placements[i].lpfn = 0;
	}
}

static int drm_gem_vram_init(struct drm_device *dev,
			     struct ttm_bo_device *bdev,
			     struct drm_gem_vram_object *gbo,
			     size_t size, unsigned long pg_align,
			     bool interruptible)
{
	int ret;
	size_t acc_size;

	if (!gbo->bo.base.funcs)
		gbo->bo.base.funcs = &drm_gem_vram_object_funcs;

	ret = drm_gem_object_init(dev, &gbo->bo.base, size);
	if (ret)
		return ret;

	acc_size = ttm_bo_dma_acc_size(bdev, size, sizeof(*gbo));

	gbo->bo.bdev = bdev;
	drm_gem_vram_placement(gbo, TTM_PL_FLAG_VRAM | TTM_PL_FLAG_SYSTEM);

	ret = ttm_bo_init(bdev, &gbo->bo, size, ttm_bo_type_device,
			  &gbo->placement, pg_align, interruptible, acc_size,
			  NULL, NULL, ttm_buffer_object_destroy);
	if (ret)
		goto err_drm_gem_object_release;

	return 0;

err_drm_gem_object_release:
	drm_gem_object_release(&gbo->bo.base);
	return ret;
}

/**
 * drm_gem_vram_create() - Creates a VRAM-backed GEM object
 * @dev:		the DRM device
 * @bdev:		the TTM BO device backing the object
 * @size:		the buffer size in bytes
 * @pg_align:		the buffer's alignment in multiples of the page size
 * @interruptible:	sleep interruptible if waiting for memory
 *
 * Returns:
 * A new instance of &struct drm_gem_vram_object on success, or
 * an ERR_PTR()-encoded error code otherwise.
 */
struct drm_gem_vram_object *drm_gem_vram_create(struct drm_device *dev,
						struct ttm_bo_device *bdev,
						size_t size,
						unsigned long pg_align,
						bool interruptible)
{
	struct drm_gem_vram_object *gbo;
	int ret;

	gbo = kzalloc(sizeof(*gbo), GFP_KERNEL);
	if (!gbo)
		return ERR_PTR(-ENOMEM);

	ret = drm_gem_vram_init(dev, bdev, gbo, size, pg_align, interruptible);
	if (ret < 0)
		goto err_kfree;

	return gbo;

err_kfree:
	kfree(gbo);
	return ERR_PTR(ret);
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
	if (WARN_ON_ONCE(!gbo->pin_count))
		return (s64)-ENODEV;
	return gbo->bo.offset;
}
EXPORT_SYMBOL(drm_gem_vram_offset);

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
 * Returns:
 * 0 on success, or
 * a negative error code otherwise.
 */
int drm_gem_vram_pin(struct drm_gem_vram_object *gbo, unsigned long pl_flag)
{
	int i, ret;
	struct ttm_operation_ctx ctx = { false, false };

	ret = ttm_bo_reserve(&gbo->bo, true, false, NULL);
	if (ret < 0)
		return ret;

	if (gbo->pin_count)
		goto out;

	if (pl_flag)
		drm_gem_vram_placement(gbo, pl_flag);

	for (i = 0; i < gbo->placement.num_placement; ++i)
		gbo->placements[i].flags |= TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(&gbo->bo, &gbo->placement, &ctx);
	if (ret < 0)
		goto err_ttm_bo_unreserve;

out:
	++gbo->pin_count;
	ttm_bo_unreserve(&gbo->bo);

	return 0;

err_ttm_bo_unreserve:
	ttm_bo_unreserve(&gbo->bo);
	return ret;
}
EXPORT_SYMBOL(drm_gem_vram_pin);

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
	int i, ret;
	struct ttm_operation_ctx ctx = { false, false };

	ret = ttm_bo_reserve(&gbo->bo, true, false, NULL);
	if (ret < 0)
		return ret;

	if (WARN_ON_ONCE(!gbo->pin_count))
		goto out;

	--gbo->pin_count;
	if (gbo->pin_count)
		goto out;

	for (i = 0; i < gbo->placement.num_placement ; ++i)
		gbo->placements[i].flags &= ~TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(&gbo->bo, &gbo->placement, &ctx);
	if (ret < 0)
		goto err_ttm_bo_unreserve;

out:
	ttm_bo_unreserve(&gbo->bo);

	return 0;

err_ttm_bo_unreserve:
	ttm_bo_unreserve(&gbo->bo);
	return ret;
}
EXPORT_SYMBOL(drm_gem_vram_unpin);

/**
 * drm_gem_vram_kmap() - Maps a GEM VRAM object into kernel address space
 * @gbo:	the GEM VRAM object
 * @map:	establish a mapping if necessary
 * @is_iomem:	returns true if the mapped memory is I/O memory, or false \
	otherwise; can be NULL
 *
 * This function maps the buffer object into the kernel's address space
 * or returns the current mapping. If the parameter map is false, the
 * function only queries the current mapping, but does not establish a
 * new one.
 *
 * Returns:
 * The buffers virtual address if mapped, or
 * NULL if not mapped, or
 * an ERR_PTR()-encoded error code otherwise.
 */
void *drm_gem_vram_kmap(struct drm_gem_vram_object *gbo, bool map,
			bool *is_iomem)
{
	int ret;
	struct ttm_bo_kmap_obj *kmap = &gbo->kmap;

	if (kmap->virtual || !map)
		goto out;

	ret = ttm_bo_kmap(&gbo->bo, 0, gbo->bo.num_pages, kmap);
	if (ret)
		return ERR_PTR(ret);

out:
	if (!is_iomem)
		return kmap->virtual;
	if (!kmap->virtual) {
		*is_iomem = false;
		return NULL;
	}
	return ttm_kmap_obj_virtual(kmap, is_iomem);
}
EXPORT_SYMBOL(drm_gem_vram_kmap);

/**
 * drm_gem_vram_kunmap() - Unmaps a GEM VRAM object
 * @gbo:	the GEM VRAM object
 */
void drm_gem_vram_kunmap(struct drm_gem_vram_object *gbo)
{
	struct ttm_bo_kmap_obj *kmap = &gbo->kmap;

	if (!kmap->virtual)
		return;

	ttm_bo_kunmap(kmap);
	kmap->virtual = NULL;
}
EXPORT_SYMBOL(drm_gem_vram_kunmap);

/**
 * drm_gem_vram_fill_create_dumb() - \
	Helper for implementing &struct drm_driver.dumb_create
 * @file:		the DRM file
 * @dev:		the DRM device
 * @bdev:		the TTM BO device managing the buffer object
 * @pg_align:		the buffer's alignment in multiples of the page size
 * @interruptible:	sleep interruptible if waiting for memory
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
				  struct ttm_bo_device *bdev,
				  unsigned long pg_align,
				  bool interruptible,
				  struct drm_mode_create_dumb *args)
{
	size_t pitch, size;
	struct drm_gem_vram_object *gbo;
	int ret;
	u32 handle;

	pitch = args->width * ((args->bpp + 7) / 8);
	size = pitch * args->height;

	size = roundup(size, PAGE_SIZE);
	if (!size)
		return -EINVAL;

	gbo = drm_gem_vram_create(dev, bdev, size, pg_align, interruptible);
	if (IS_ERR(gbo))
		return PTR_ERR(gbo);

	ret = drm_gem_handle_create(file, &gbo->bo.base, &handle);
	if (ret)
		goto err_drm_gem_object_put_unlocked;

	drm_gem_object_put_unlocked(&gbo->bo.base);

	args->pitch = pitch;
	args->size = size;
	args->handle = handle;

	return 0;

err_drm_gem_object_put_unlocked:
	drm_gem_object_put_unlocked(&gbo->bo.base);
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

/**
 * drm_gem_vram_bo_driver_evict_flags() - \
	Implements &struct ttm_bo_driver.evict_flags
 * @bo:	TTM buffer object. Refers to &struct drm_gem_vram_object.bo
 * @pl:	TTM placement information.
 */
void drm_gem_vram_bo_driver_evict_flags(struct ttm_buffer_object *bo,
					struct ttm_placement *pl)
{
	struct drm_gem_vram_object *gbo;

	/* TTM may pass BOs that are not GEM VRAM BOs. */
	if (!drm_is_gem_vram(bo))
		return;

	gbo = drm_gem_vram_of_bo(bo);
	drm_gem_vram_placement(gbo, TTM_PL_FLAG_SYSTEM);
	*pl = gbo->placement;
}
EXPORT_SYMBOL(drm_gem_vram_bo_driver_evict_flags);

/**
 * drm_gem_vram_bo_driver_verify_access() - \
	Implements &struct ttm_bo_driver.verify_access
 * @bo:		TTM buffer object. Refers to &struct drm_gem_vram_object.bo
 * @filp:	File pointer.
 *
 * Returns:
 * 0 on success, or
 * a negative errno code otherwise.
 */
int drm_gem_vram_bo_driver_verify_access(struct ttm_buffer_object *bo,
					 struct file *filp)
{
	struct drm_gem_vram_object *gbo = drm_gem_vram_of_bo(bo);

	return drm_vma_node_verify_access(&gbo->bo.base.vma_node,
					  filp->private_data);
}
EXPORT_SYMBOL(drm_gem_vram_bo_driver_verify_access);

/*
 * drm_gem_vram_mm_funcs - Functions for &struct drm_vram_mm
 *
 * Most users of @struct drm_gem_vram_object will also use
 * @struct drm_vram_mm. This instance of &struct drm_vram_mm_funcs
 * can be used to connect both.
 */
const struct drm_vram_mm_funcs drm_gem_vram_mm_funcs = {
	.evict_flags = drm_gem_vram_bo_driver_evict_flags,
	.verify_access = drm_gem_vram_bo_driver_verify_access
};
EXPORT_SYMBOL(drm_gem_vram_mm_funcs);

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

	return drm_gem_vram_fill_create_dumb(file, dev, &dev->vram_mm->bdev, 0,
					     false, args);
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

	drm_gem_object_put_unlocked(gem);

	return 0;
}
EXPORT_SYMBOL(drm_gem_vram_driver_dumb_mmap_offset);

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
	int ret;
	void *base;

	ret = drm_gem_vram_pin(gbo, 0);
	if (ret)
		return NULL;
	base = drm_gem_vram_kmap(gbo, true, NULL);
	if (IS_ERR(base)) {
		drm_gem_vram_unpin(gbo);
		return NULL;
	}
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

	drm_gem_vram_kunmap(gbo);
	drm_gem_vram_unpin(gbo);
}

/*
 * GEM object funcs
 */

static const struct drm_gem_object_funcs drm_gem_vram_object_funcs = {
	.free	= drm_gem_vram_object_free,
	.pin	= drm_gem_vram_object_pin,
	.unpin	= drm_gem_vram_object_unpin,
	.vmap	= drm_gem_vram_object_vmap,
	.vunmap	= drm_gem_vram_object_vunmap
};
