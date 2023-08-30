// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <linux/slab.h>

#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_bo.h>

#include <drm/drm_buddy.h>

#include "i915_ttm_buddy_manager.h"

#include "i915_gem.h"

struct i915_ttm_buddy_manager {
	struct ttm_resource_manager manager;
	struct drm_buddy mm;
	struct list_head reserved;
	struct mutex lock;
	unsigned long visible_size;
	unsigned long visible_avail;
	unsigned long visible_reserved;
	u64 default_page_size;
};

static struct i915_ttm_buddy_manager *
to_buddy_manager(struct ttm_resource_manager *man)
{
	return container_of(man, struct i915_ttm_buddy_manager, manager);
}

static int i915_ttm_buddy_man_alloc(struct ttm_resource_manager *man,
				    struct ttm_buffer_object *bo,
				    const struct ttm_place *place,
				    struct ttm_resource **res)
{
	struct i915_ttm_buddy_manager *bman = to_buddy_manager(man);
	struct i915_ttm_buddy_resource *bman_res;
	struct drm_buddy *mm = &bman->mm;
	unsigned long n_pages, lpfn;
	u64 min_page_size;
	u64 size;
	int err;

	lpfn = place->lpfn;
	if (!lpfn)
		lpfn = man->size;

	bman_res = kzalloc(sizeof(*bman_res), GFP_KERNEL);
	if (!bman_res)
		return -ENOMEM;

	ttm_resource_init(bo, place, &bman_res->base);
	INIT_LIST_HEAD(&bman_res->blocks);
	bman_res->mm = mm;

	if (place->flags & TTM_PL_FLAG_TOPDOWN)
		bman_res->flags |= DRM_BUDDY_TOPDOWN_ALLOCATION;

	if (place->fpfn || lpfn != man->size)
		bman_res->flags |= DRM_BUDDY_RANGE_ALLOCATION;

	GEM_BUG_ON(!bman_res->base.size);
	size = bman_res->base.size;

	min_page_size = bman->default_page_size;
	if (bo->page_alignment)
		min_page_size = bo->page_alignment << PAGE_SHIFT;

	GEM_BUG_ON(min_page_size < mm->chunk_size);
	GEM_BUG_ON(!IS_ALIGNED(size, min_page_size));

	if (place->fpfn + PFN_UP(bman_res->base.size) != place->lpfn &&
	    place->flags & TTM_PL_FLAG_CONTIGUOUS) {
		unsigned long pages;

		size = roundup_pow_of_two(size);
		min_page_size = size;

		pages = size >> ilog2(mm->chunk_size);
		if (pages > lpfn)
			lpfn = pages;
	}

	if (size > lpfn << PAGE_SHIFT) {
		err = -E2BIG;
		goto err_free_res;
	}

	n_pages = size >> ilog2(mm->chunk_size);

	mutex_lock(&bman->lock);
	if (lpfn <= bman->visible_size && n_pages > bman->visible_avail) {
		mutex_unlock(&bman->lock);
		err = -ENOSPC;
		goto err_free_res;
	}

	err = drm_buddy_alloc_blocks(mm, (u64)place->fpfn << PAGE_SHIFT,
				     (u64)lpfn << PAGE_SHIFT,
				     (u64)n_pages << PAGE_SHIFT,
				     min_page_size,
				     &bman_res->blocks,
				     bman_res->flags);
	if (unlikely(err))
		goto err_free_blocks;

	if (place->flags & TTM_PL_FLAG_CONTIGUOUS) {
		u64 original_size = (u64)bman_res->base.size;

		drm_buddy_block_trim(mm,
				     original_size,
				     &bman_res->blocks);
	}

	if (lpfn <= bman->visible_size) {
		bman_res->used_visible_size = PFN_UP(bman_res->base.size);
	} else {
		struct drm_buddy_block *block;

		list_for_each_entry(block, &bman_res->blocks, link) {
			unsigned long start =
				drm_buddy_block_offset(block) >> PAGE_SHIFT;

			if (start < bman->visible_size) {
				unsigned long end = start +
					(drm_buddy_block_size(mm, block) >> PAGE_SHIFT);

				bman_res->used_visible_size +=
					min(end, bman->visible_size) - start;
			}
		}
	}

	if (bman_res->used_visible_size)
		bman->visible_avail -= bman_res->used_visible_size;

	mutex_unlock(&bman->lock);

	*res = &bman_res->base;
	return 0;

err_free_blocks:
	drm_buddy_free_list(mm, &bman_res->blocks);
	mutex_unlock(&bman->lock);
err_free_res:
	ttm_resource_fini(man, &bman_res->base);
	kfree(bman_res);
	return err;
}

static void i915_ttm_buddy_man_free(struct ttm_resource_manager *man,
				    struct ttm_resource *res)
{
	struct i915_ttm_buddy_resource *bman_res = to_ttm_buddy_resource(res);
	struct i915_ttm_buddy_manager *bman = to_buddy_manager(man);

	mutex_lock(&bman->lock);
	drm_buddy_free_list(&bman->mm, &bman_res->blocks);
	bman->visible_avail += bman_res->used_visible_size;
	mutex_unlock(&bman->lock);

	ttm_resource_fini(man, res);
	kfree(bman_res);
}

static bool i915_ttm_buddy_man_intersects(struct ttm_resource_manager *man,
					  struct ttm_resource *res,
					  const struct ttm_place *place,
					  size_t size)
{
	struct i915_ttm_buddy_resource *bman_res = to_ttm_buddy_resource(res);
	struct i915_ttm_buddy_manager *bman = to_buddy_manager(man);
	struct drm_buddy *mm = &bman->mm;
	struct drm_buddy_block *block;

	if (!place->fpfn && !place->lpfn)
		return true;

	GEM_BUG_ON(!place->lpfn);

	/*
	 * If we just want something mappable then we can quickly check
	 * if the current victim resource is using any of the CPU
	 * visible portion.
	 */
	if (!place->fpfn &&
	    place->lpfn == i915_ttm_buddy_man_visible_size(man))
		return bman_res->used_visible_size > 0;

	/* Check each drm buddy block individually */
	list_for_each_entry(block, &bman_res->blocks, link) {
		unsigned long fpfn =
			drm_buddy_block_offset(block) >> PAGE_SHIFT;
		unsigned long lpfn = fpfn +
			(drm_buddy_block_size(mm, block) >> PAGE_SHIFT);

		if (place->fpfn < lpfn && place->lpfn > fpfn)
			return true;
	}

	return false;
}

static bool i915_ttm_buddy_man_compatible(struct ttm_resource_manager *man,
					  struct ttm_resource *res,
					  const struct ttm_place *place,
					  size_t size)
{
	struct i915_ttm_buddy_resource *bman_res = to_ttm_buddy_resource(res);
	struct i915_ttm_buddy_manager *bman = to_buddy_manager(man);
	struct drm_buddy *mm = &bman->mm;
	struct drm_buddy_block *block;

	if (!place->fpfn && !place->lpfn)
		return true;

	GEM_BUG_ON(!place->lpfn);

	if (!place->fpfn &&
	    place->lpfn == i915_ttm_buddy_man_visible_size(man))
		return bman_res->used_visible_size == PFN_UP(res->size);

	/* Check each drm buddy block individually */
	list_for_each_entry(block, &bman_res->blocks, link) {
		unsigned long fpfn =
			drm_buddy_block_offset(block) >> PAGE_SHIFT;
		unsigned long lpfn = fpfn +
			(drm_buddy_block_size(mm, block) >> PAGE_SHIFT);

		if (fpfn < place->fpfn || lpfn > place->lpfn)
			return false;
	}

	return true;
}

static void i915_ttm_buddy_man_debug(struct ttm_resource_manager *man,
				     struct drm_printer *printer)
{
	struct i915_ttm_buddy_manager *bman = to_buddy_manager(man);
	struct drm_buddy_block *block;

	mutex_lock(&bman->lock);
	drm_printf(printer, "default_page_size: %lluKiB\n",
		   bman->default_page_size >> 10);
	drm_printf(printer, "visible_avail: %lluMiB\n",
		   (u64)bman->visible_avail << PAGE_SHIFT >> 20);
	drm_printf(printer, "visible_size: %lluMiB\n",
		   (u64)bman->visible_size << PAGE_SHIFT >> 20);
	drm_printf(printer, "visible_reserved: %lluMiB\n",
		   (u64)bman->visible_reserved << PAGE_SHIFT >> 20);

	drm_buddy_print(&bman->mm, printer);

	drm_printf(printer, "reserved:\n");
	list_for_each_entry(block, &bman->reserved, link)
		drm_buddy_block_print(&bman->mm, block, printer);
	mutex_unlock(&bman->lock);
}

static const struct ttm_resource_manager_func i915_ttm_buddy_manager_func = {
	.alloc = i915_ttm_buddy_man_alloc,
	.free = i915_ttm_buddy_man_free,
	.intersects = i915_ttm_buddy_man_intersects,
	.compatible = i915_ttm_buddy_man_compatible,
	.debug = i915_ttm_buddy_man_debug,
};

/**
 * i915_ttm_buddy_man_init - Setup buddy allocator based ttm manager
 * @bdev: The ttm device
 * @type: Memory type we want to manage
 * @use_tt: Set use_tt for the manager
 * @size: The size in bytes to manage
 * @visible_size: The CPU visible size in bytes to manage
 * @default_page_size: The default minimum page size in bytes for allocations,
 * this must be at least as large as @chunk_size, and can be overridden by
 * setting the BO page_alignment, to be larger or smaller as needed.
 * @chunk_size: The minimum page size in bytes for our allocations i.e
 * order-zero
 *
 * Note that the starting address is assumed to be zero here, since this
 * simplifies keeping the property where allocated blocks having natural
 * power-of-two alignment. So long as the real starting address is some large
 * power-of-two, or naturally start from zero, then this should be fine.  Also
 * the &i915_ttm_buddy_man_reserve interface can be used to preserve alignment
 * if say there is some unusable range from the start of the region. We can
 * revisit this in the future and make the interface accept an actual starting
 * offset and let it take care of the rest.
 *
 * Note that if the @size is not aligned to the @chunk_size then we perform the
 * required rounding to get the usable size. The final size in pages can be
 * taken from &ttm_resource_manager.size.
 *
 * Return: 0 on success, negative error code on failure.
 */
int i915_ttm_buddy_man_init(struct ttm_device *bdev,
			    unsigned int type, bool use_tt,
			    u64 size, u64 visible_size, u64 default_page_size,
			    u64 chunk_size)
{
	struct ttm_resource_manager *man;
	struct i915_ttm_buddy_manager *bman;
	int err;

	bman = kzalloc(sizeof(*bman), GFP_KERNEL);
	if (!bman)
		return -ENOMEM;

	err = drm_buddy_init(&bman->mm, size, chunk_size);
	if (err)
		goto err_free_bman;

	mutex_init(&bman->lock);
	INIT_LIST_HEAD(&bman->reserved);
	GEM_BUG_ON(default_page_size < chunk_size);
	bman->default_page_size = default_page_size;
	bman->visible_size = visible_size >> PAGE_SHIFT;
	bman->visible_avail = bman->visible_size;

	man = &bman->manager;
	man->use_tt = use_tt;
	man->func = &i915_ttm_buddy_manager_func;
	ttm_resource_manager_init(man, bdev, bman->mm.size >> PAGE_SHIFT);

	ttm_resource_manager_set_used(man, true);
	ttm_set_driver_manager(bdev, type, man);

	return 0;

err_free_bman:
	kfree(bman);
	return err;
}

/**
 * i915_ttm_buddy_man_fini - Destroy the buddy allocator ttm manager
 * @bdev: The ttm device
 * @type: Memory type we want to manage
 *
 * Note that if we reserved anything with &i915_ttm_buddy_man_reserve, this will
 * also be freed for us here.
 *
 * Return: 0 on success, negative error code on failure.
 */
int i915_ttm_buddy_man_fini(struct ttm_device *bdev, unsigned int type)
{
	struct ttm_resource_manager *man = ttm_manager_type(bdev, type);
	struct i915_ttm_buddy_manager *bman = to_buddy_manager(man);
	struct drm_buddy *mm = &bman->mm;
	int ret;

	ttm_resource_manager_set_used(man, false);

	ret = ttm_resource_manager_evict_all(bdev, man);
	if (ret)
		return ret;

	ttm_set_driver_manager(bdev, type, NULL);

	mutex_lock(&bman->lock);
	drm_buddy_free_list(mm, &bman->reserved);
	drm_buddy_fini(mm);
	bman->visible_avail += bman->visible_reserved;
	WARN_ON_ONCE(bman->visible_avail != bman->visible_size);
	mutex_unlock(&bman->lock);

	ttm_resource_manager_cleanup(man);
	kfree(bman);

	return 0;
}

/**
 * i915_ttm_buddy_man_reserve - Reserve address range
 * @man: The buddy allocator ttm manager
 * @start: The offset in bytes, where the region start is assumed to be zero
 * @size: The size in bytes
 *
 * Note that the starting address for the region is always assumed to be zero.
 *
 * Return: 0 on success, negative error code on failure.
 */
int i915_ttm_buddy_man_reserve(struct ttm_resource_manager *man,
			       u64 start, u64 size)
{
	struct i915_ttm_buddy_manager *bman = to_buddy_manager(man);
	struct drm_buddy *mm = &bman->mm;
	unsigned long fpfn = start >> PAGE_SHIFT;
	unsigned long flags = 0;
	int ret;

	flags |= DRM_BUDDY_RANGE_ALLOCATION;

	mutex_lock(&bman->lock);
	ret = drm_buddy_alloc_blocks(mm, start,
				     start + size,
				     size, mm->chunk_size,
				     &bman->reserved,
				     flags);

	if (fpfn < bman->visible_size) {
		unsigned long lpfn = fpfn + (size >> PAGE_SHIFT);
		unsigned long visible = min(lpfn, bman->visible_size) - fpfn;

		bman->visible_reserved += visible;
		bman->visible_avail -= visible;
	}
	mutex_unlock(&bman->lock);

	return ret;
}

/**
 * i915_ttm_buddy_man_visible_size - Return the size of the CPU visible portion
 * in pages.
 * @man: The buddy allocator ttm manager
 */
u64 i915_ttm_buddy_man_visible_size(struct ttm_resource_manager *man)
{
	struct i915_ttm_buddy_manager *bman = to_buddy_manager(man);

	return bman->visible_size;
}

/**
 * i915_ttm_buddy_man_avail - Query the avail tracking for the manager.
 *
 * @man: The buddy allocator ttm manager
 * @avail: The total available memory in pages for the entire manager.
 * @visible_avail: The total available memory in pages for the CPU visible
 * portion. Note that this will always give the same value as @avail on
 * configurations that don't have a small BAR.
 */
void i915_ttm_buddy_man_avail(struct ttm_resource_manager *man,
			      u64 *avail, u64 *visible_avail)
{
	struct i915_ttm_buddy_manager *bman = to_buddy_manager(man);

	mutex_lock(&bman->lock);
	*avail = bman->mm.avail >> PAGE_SHIFT;
	*visible_avail = bman->visible_avail;
	mutex_unlock(&bman->lock);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
void i915_ttm_buddy_man_force_visible_size(struct ttm_resource_manager *man,
					   u64 size)
{
	struct i915_ttm_buddy_manager *bman = to_buddy_manager(man);

	bman->visible_size = size;
}
#endif
