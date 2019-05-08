// SPDX-License-Identifier: GPL-2.0-or-later

#include <drm/drm_gem_vram_helper.h>
#include <drm/ttm/ttm_page_alloc.h>

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
	drm_gem_object_release(&gbo->gem);
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

	ret = drm_gem_object_init(dev, &gbo->gem, size);
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
	drm_gem_object_release(&gbo->gem);
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
 * drm_gem_vram_reserve() - Reserves a VRAM-backed GEM object
 * @gbo:	the GEM VRAM object
 * @no_wait:	don't wait for buffer object to become available
 *
 * See ttm_bo_reserve() for more information.
 *
 * Returns:
 * 0 on success, or
 * a negative error code otherwise
 */
int drm_gem_vram_reserve(struct drm_gem_vram_object *gbo, bool no_wait)
{
	return ttm_bo_reserve(&gbo->bo, true, no_wait, NULL);
}
EXPORT_SYMBOL(drm_gem_vram_reserve);

/**
 * drm_gem_vram_unreserve() - \
	Release a reservation acquired by drm_gem_vram_reserve()
 * @gbo:	the GEM VRAM object
 *
 * See ttm_bo_unreserve() for more information.
 */
void drm_gem_vram_unreserve(struct drm_gem_vram_object *gbo)
{
	ttm_bo_unreserve(&gbo->bo);
}
EXPORT_SYMBOL(drm_gem_vram_unreserve);

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
	return drm_vma_node_offset_addr(&gbo->bo.vma_node);
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
 * it can be pinned to another region.
 *
 * Returns:
 * 0 on success, or
 * a negative error code otherwise.
 */
int drm_gem_vram_pin(struct drm_gem_vram_object *gbo, unsigned long pl_flag)
{
	int i, ret;
	struct ttm_operation_ctx ctx = { false, false };

	if (gbo->pin_count) {
		++gbo->pin_count;
		return 0;
	}

	drm_gem_vram_placement(gbo, pl_flag);
	for (i = 0; i < gbo->placement.num_placement; ++i)
		gbo->placements[i].flags |= TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(&gbo->bo, &gbo->placement, &ctx);
	if (ret < 0)
		return ret;

	gbo->pin_count = 1;

	return 0;
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

	if (WARN_ON_ONCE(!gbo->pin_count))
		return 0;

	--gbo->pin_count;
	if (gbo->pin_count)
		return 0;

	for (i = 0; i < gbo->placement.num_placement ; ++i)
		gbo->placements[i].flags &= ~TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(&gbo->bo, &gbo->placement, &ctx);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(drm_gem_vram_unpin);

/**
 * drm_gem_vram_push_to_system() - \
	Unpins a GEM VRAM object and moves it to system memory
 * @gbo:	the GEM VRAM object
 *
 * This operation only works if the caller holds the final pin on the
 * buffer object.
 *
 * Returns:
 * 0 on success, or
 * a negative error code otherwise.
 */
int drm_gem_vram_push_to_system(struct drm_gem_vram_object *gbo)
{
	int i, ret;
	struct ttm_operation_ctx ctx = { false, false };

	if (WARN_ON_ONCE(!gbo->pin_count))
		return 0;

	--gbo->pin_count;
	if (gbo->pin_count)
		return 0;

	if (gbo->kmap.virtual)
		ttm_bo_kunmap(&gbo->kmap);

	drm_gem_vram_placement(gbo, TTM_PL_FLAG_SYSTEM);
	for (i = 0; i < gbo->placement.num_placement ; ++i)
		gbo->placements[i].flags |= TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(&gbo->bo, &gbo->placement, &ctx);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(drm_gem_vram_push_to_system);

/**
 * drm_gem_vram_kmap_at() - Maps a GEM VRAM object into kernel address space
 * @gbo:	the GEM VRAM object
 * @map:	establish a mapping if necessary
 * @is_iomem:	returns true if the mapped memory is I/O memory, or false \
	otherwise; can be NULL
 * @kmap:	the mapping's kmap object
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
void *drm_gem_vram_kmap_at(struct drm_gem_vram_object *gbo, bool map,
			   bool *is_iomem, struct ttm_bo_kmap_obj *kmap)
{
	int ret;

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
EXPORT_SYMBOL(drm_gem_vram_kmap_at);

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
	return drm_gem_vram_kmap_at(gbo, map, is_iomem, &gbo->kmap);
}
EXPORT_SYMBOL(drm_gem_vram_kmap);

/**
 * drm_gem_vram_kunmap_at() - Unmaps a GEM VRAM object
 * @gbo:	the GEM VRAM object
 * @kmap:	the mapping's kmap object
 */
void drm_gem_vram_kunmap_at(struct drm_gem_vram_object *gbo,
			    struct ttm_bo_kmap_obj *kmap)
{
	if (!kmap->virtual)
		return;

	ttm_bo_kunmap(kmap);
	kmap->virtual = NULL;
}
EXPORT_SYMBOL(drm_gem_vram_kunmap_at);

/**
 * drm_gem_vram_kunmap() - Unmaps a GEM VRAM object
 * @gbo:	the GEM VRAM object
 */
void drm_gem_vram_kunmap(struct drm_gem_vram_object *gbo)
{
	drm_gem_vram_kunmap_at(gbo, &gbo->kmap);
}
EXPORT_SYMBOL(drm_gem_vram_kunmap);
