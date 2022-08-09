// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <linux/slab.h>

#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>

#include "i915_ttm_buddy_manager.h"

#include "i915_buddy.h"
#include "i915_gem.h"

struct i915_ttm_buddy_manager {
	struct ttm_resource_manager manager;
	struct i915_buddy_mm mm;
	struct list_head reserved;
	struct mutex lock;
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
	struct i915_buddy_mm *mm = &bman->mm;
	unsigned long n_pages;
	unsigned int min_order;
	u64 min_page_size;
	u64 size;
	int err;

	GEM_BUG_ON(place->fpfn || place->lpfn);

	bman_res = kzalloc(sizeof(*bman_res), GFP_KERNEL);
	if (!bman_res)
		return -ENOMEM;

	ttm_resource_init(bo, place, &bman_res->base);
	INIT_LIST_HEAD(&bman_res->blocks);
	bman_res->mm = mm;

	GEM_BUG_ON(!bman_res->base.num_pages);
	size = bman_res->base.num_pages << PAGE_SHIFT;

	min_page_size = bman->default_page_size;
	if (bo->page_alignment)
		min_page_size = bo->page_alignment << PAGE_SHIFT;

	GEM_BUG_ON(min_page_size < mm->chunk_size);
	min_order = ilog2(min_page_size) - ilog2(mm->chunk_size);
	if (place->flags & TTM_PL_FLAG_CONTIGUOUS) {
		size = roundup_pow_of_two(size);
		min_order = ilog2(size) - ilog2(mm->chunk_size);
	}

	if (size > mm->size) {
		err = -E2BIG;
		goto err_free_res;
	}

	n_pages = size >> ilog2(mm->chunk_size);

	do {
		struct i915_buddy_block *block;
		unsigned int order;

		order = fls(n_pages) - 1;
		GEM_BUG_ON(order > mm->max_order);
		GEM_BUG_ON(order < min_order);

		do {
			mutex_lock(&bman->lock);
			block = i915_buddy_alloc(mm, order);
			mutex_unlock(&bman->lock);
			if (!IS_ERR(block))
				break;

			if (order-- == min_order) {
				err = -ENOSPC;
				goto err_free_blocks;
			}
		} while (1);

		n_pages -= BIT(order);

		list_add_tail(&block->link, &bman_res->blocks);

		if (!n_pages)
			break;
	} while (1);

	*res = &bman_res->base;
	return 0;

err_free_blocks:
	mutex_lock(&bman->lock);
	i915_buddy_free_list(mm, &bman_res->blocks);
	mutex_unlock(&bman->lock);
err_free_res:
	kfree(bman_res);
	return err;
}

static void i915_ttm_buddy_man_free(struct ttm_resource_manager *man,
				    struct ttm_resource *res)
{
	struct i915_ttm_buddy_resource *bman_res = to_ttm_buddy_resource(res);
	struct i915_ttm_buddy_manager *bman = to_buddy_manager(man);

	mutex_lock(&bman->lock);
	i915_buddy_free_list(&bman->mm, &bman_res->blocks);
	mutex_unlock(&bman->lock);

	kfree(bman_res);
}

static const struct ttm_resource_manager_func i915_ttm_buddy_manager_func = {
	.alloc = i915_ttm_buddy_man_alloc,
	.free = i915_ttm_buddy_man_free,
};


/**
 * i915_ttm_buddy_man_init - Setup buddy allocator based ttm manager
 * @bdev: The ttm device
 * @type: Memory type we want to manage
 * @use_tt: Set use_tt for the manager
 * @size: The size in bytes to manage
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
			    u64 size, u64 default_page_size,
			    u64 chunk_size)
{
	struct ttm_resource_manager *man;
	struct i915_ttm_buddy_manager *bman;
	int err;

	bman = kzalloc(sizeof(*bman), GFP_KERNEL);
	if (!bman)
		return -ENOMEM;

	err = i915_buddy_init(&bman->mm, size, chunk_size);
	if (err)
		goto err_free_bman;

	mutex_init(&bman->lock);
	INIT_LIST_HEAD(&bman->reserved);
	GEM_BUG_ON(default_page_size < chunk_size);
	bman->default_page_size = default_page_size;

	man = &bman->manager;
	man->use_tt = use_tt;
	man->func = &i915_ttm_buddy_manager_func;
	ttm_resource_manager_init(man, bman->mm.size >> PAGE_SHIFT);

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
	struct i915_buddy_mm *mm = &bman->mm;
	int ret;

	ttm_resource_manager_set_used(man, false);

	ret = ttm_resource_manager_evict_all(bdev, man);
	if (ret)
		return ret;

	ttm_set_driver_manager(bdev, type, NULL);

	mutex_lock(&bman->lock);
	i915_buddy_free_list(mm, &bman->reserved);
	i915_buddy_fini(mm);
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
	struct i915_buddy_mm *mm = &bman->mm;
	int ret;

	mutex_lock(&bman->lock);
	ret = i915_buddy_alloc_range(mm, &bman->reserved, start, size);
	mutex_unlock(&bman->lock);

	return ret;
}

