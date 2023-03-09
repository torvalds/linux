// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2022 Intel Corporation
 * Copyright (C) 2021-2002 Red Hat
 */

#include <drm/drm_managed.h>

#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_range_manager.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_res_cursor.h"
#include "xe_ttm_vram_mgr.h"

static inline struct drm_buddy_block *
xe_ttm_vram_mgr_first_block(struct list_head *list)
{
	return list_first_entry_or_null(list, struct drm_buddy_block, link);
}

static inline bool xe_is_vram_mgr_blocks_contiguous(struct drm_buddy *mm,
						    struct list_head *head)
{
	struct drm_buddy_block *block;
	u64 start, size;

	block = xe_ttm_vram_mgr_first_block(head);
	if (!block)
		return false;

	while (head != block->link.next) {
		start = drm_buddy_block_offset(block);
		size = drm_buddy_block_size(mm, block);

		block = list_entry(block->link.next, struct drm_buddy_block,
				   link);
		if (start + size != drm_buddy_block_offset(block))
			return false;
	}

	return true;
}

static int xe_ttm_vram_mgr_new(struct ttm_resource_manager *man,
			       struct ttm_buffer_object *tbo,
			       const struct ttm_place *place,
			       struct ttm_resource **res)
{
	u64 max_bytes, cur_size, min_block_size;
	struct xe_ttm_vram_mgr *mgr = to_xe_ttm_vram_mgr(man);
	struct xe_ttm_vram_mgr_resource *vres;
	u64 size, remaining_size, lpfn, fpfn;
	struct drm_buddy *mm = &mgr->mm;
	struct drm_buddy_block *block;
	unsigned long pages_per_block;
	int r;

	lpfn = (u64)place->lpfn << PAGE_SHIFT;
	if (!lpfn || lpfn > man->size)
		lpfn = man->size;

	fpfn = (u64)place->fpfn << PAGE_SHIFT;

	max_bytes = mgr->manager.size;
	if (place->flags & TTM_PL_FLAG_CONTIGUOUS) {
		pages_per_block = ~0ul;
	} else {
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		pages_per_block = HPAGE_PMD_NR;
#else
		/* default to 2MB */
		pages_per_block = 2UL << (20UL - PAGE_SHIFT);
#endif

		pages_per_block = max_t(uint32_t, pages_per_block,
					tbo->page_alignment);
	}

	vres = kzalloc(sizeof(*vres), GFP_KERNEL);
	if (!vres)
		return -ENOMEM;

	ttm_resource_init(tbo, place, &vres->base);
	remaining_size = vres->base.size;

	/* bail out quickly if there's likely not enough VRAM for this BO */
	if (ttm_resource_manager_usage(man) > max_bytes) {
		r = -ENOSPC;
		goto error_fini;
	}

	INIT_LIST_HEAD(&vres->blocks);

	if (place->flags & TTM_PL_FLAG_TOPDOWN)
		vres->flags |= DRM_BUDDY_TOPDOWN_ALLOCATION;

	if (fpfn || lpfn != man->size)
		/* Allocate blocks in desired range */
		vres->flags |= DRM_BUDDY_RANGE_ALLOCATION;

	mutex_lock(&mgr->lock);
	while (remaining_size) {
		if (tbo->page_alignment)
			min_block_size = tbo->page_alignment << PAGE_SHIFT;
		else
			min_block_size = mgr->default_page_size;

		XE_BUG_ON(min_block_size < mm->chunk_size);

		/* Limit maximum size to 2GiB due to SG table limitations */
		size = min(remaining_size, 2ULL << 30);

		if (size >= pages_per_block << PAGE_SHIFT)
			min_block_size = pages_per_block << PAGE_SHIFT;

		cur_size = size;

		if (fpfn + size != (u64)place->lpfn << PAGE_SHIFT) {
			/*
			 * Except for actual range allocation, modify the size and
			 * min_block_size conforming to continuous flag enablement
			 */
			if (place->flags & TTM_PL_FLAG_CONTIGUOUS) {
				size = roundup_pow_of_two(size);
				min_block_size = size;
			/*
			 * Modify the size value if size is not
			 * aligned with min_block_size
			 */
			} else if (!IS_ALIGNED(size, min_block_size)) {
				size = round_up(size, min_block_size);
			}
		}

		r = drm_buddy_alloc_blocks(mm, fpfn,
					   lpfn,
					   size,
					   min_block_size,
					   &vres->blocks,
					   vres->flags);
		if (unlikely(r))
			goto error_free_blocks;

		if (size > remaining_size)
			remaining_size = 0;
		else
			remaining_size -= size;
	}
	mutex_unlock(&mgr->lock);

	if (cur_size != size) {
		struct drm_buddy_block *block;
		struct list_head *trim_list;
		u64 original_size;
		LIST_HEAD(temp);

		trim_list = &vres->blocks;
		original_size = vres->base.size;

		/*
		 * If size value is rounded up to min_block_size, trim the last
		 * block to the required size
		 */
		if (!list_is_singular(&vres->blocks)) {
			block = list_last_entry(&vres->blocks, typeof(*block), link);
			list_move_tail(&block->link, &temp);
			trim_list = &temp;
			/*
			 * Compute the original_size value by subtracting the
			 * last block size with (aligned size - original size)
			 */
			original_size = drm_buddy_block_size(mm, block) -
				(size - cur_size);
		}

		mutex_lock(&mgr->lock);
		drm_buddy_block_trim(mm,
				     original_size,
				     trim_list);
		mutex_unlock(&mgr->lock);

		if (!list_empty(&temp))
			list_splice_tail(trim_list, &vres->blocks);
	}

	vres->base.start = 0;
	list_for_each_entry(block, &vres->blocks, link) {
		unsigned long start;

		start = drm_buddy_block_offset(block) +
			drm_buddy_block_size(mm, block);
		start >>= PAGE_SHIFT;

		if (start > PFN_UP(vres->base.size))
			start -= PFN_UP(vres->base.size);
		else
			start = 0;
		vres->base.start = max(vres->base.start, start);
	}

	if (xe_is_vram_mgr_blocks_contiguous(mm, &vres->blocks))
		vres->base.placement |= TTM_PL_FLAG_CONTIGUOUS;

	*res = &vres->base;
	return 0;

error_free_blocks:
	drm_buddy_free_list(mm, &vres->blocks);
	mutex_unlock(&mgr->lock);
error_fini:
	ttm_resource_fini(man, &vres->base);
	kfree(vres);

	return r;
}

static void xe_ttm_vram_mgr_del(struct ttm_resource_manager *man,
				struct ttm_resource *res)
{
	struct xe_ttm_vram_mgr_resource *vres =
		to_xe_ttm_vram_mgr_resource(res);
	struct xe_ttm_vram_mgr *mgr = to_xe_ttm_vram_mgr(man);
	struct drm_buddy *mm = &mgr->mm;

	mutex_lock(&mgr->lock);
	drm_buddy_free_list(mm, &vres->blocks);
	mutex_unlock(&mgr->lock);

	ttm_resource_fini(man, res);

	kfree(vres);
}

static void xe_ttm_vram_mgr_debug(struct ttm_resource_manager *man,
				  struct drm_printer *printer)
{
	struct xe_ttm_vram_mgr *mgr = to_xe_ttm_vram_mgr(man);
	struct drm_buddy *mm = &mgr->mm;

	mutex_lock(&mgr->lock);
	drm_buddy_print(mm, printer);
	mutex_unlock(&mgr->lock);
	drm_printf(printer, "man size:%llu\n", man->size);
}

static const struct ttm_resource_manager_func xe_ttm_vram_mgr_func = {
	.alloc	= xe_ttm_vram_mgr_new,
	.free	= xe_ttm_vram_mgr_del,
	.debug	= xe_ttm_vram_mgr_debug
};

static void ttm_vram_mgr_fini(struct drm_device *dev, void *arg)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_ttm_vram_mgr *mgr = arg;
	struct ttm_resource_manager *man = &mgr->manager;

	ttm_resource_manager_set_used(man, false);

	if (ttm_resource_manager_evict_all(&xe->ttm, man))
		return;

	drm_buddy_fini(&mgr->mm);

	ttm_resource_manager_cleanup(&mgr->manager);

	ttm_set_driver_manager(&xe->ttm, mgr->mem_type, NULL);
}

int __xe_ttm_vram_mgr_init(struct xe_device *xe, struct xe_ttm_vram_mgr *mgr,
			   u32 mem_type, u64 size, u64 default_page_size)
{
	struct ttm_resource_manager *man = &mgr->manager;
	int err;

	man->func = &xe_ttm_vram_mgr_func;
	mgr->mem_type = mem_type;
	mutex_init(&mgr->lock);
	mgr->default_page_size = default_page_size;

	ttm_resource_manager_init(man, &xe->ttm, size);
	err = drm_buddy_init(&mgr->mm, man->size, default_page_size);

	ttm_set_driver_manager(&xe->ttm, mem_type, &mgr->manager);
	ttm_resource_manager_set_used(&mgr->manager, true);

	return drmm_add_action_or_reset(&xe->drm, ttm_vram_mgr_fini, mgr);
}

int xe_ttm_vram_mgr_init(struct xe_gt *gt, struct xe_ttm_vram_mgr *mgr)
{
	struct xe_device *xe = gt_to_xe(gt);

	XE_BUG_ON(xe_gt_is_media_type(gt));

	mgr->gt = gt;

	return __xe_ttm_vram_mgr_init(xe, mgr, XE_PL_VRAM0 + gt->info.vram_id,
				      gt->mem.vram.size, PAGE_SIZE);
}

int xe_ttm_vram_mgr_alloc_sgt(struct xe_device *xe,
			      struct ttm_resource *res,
			      u64 offset, u64 length,
			      struct device *dev,
			      enum dma_data_direction dir,
			      struct sg_table **sgt)
{
	struct xe_gt *gt = xe_device_get_gt(xe, res->mem_type - XE_PL_VRAM0);
	struct xe_res_cursor cursor;
	struct scatterlist *sg;
	int num_entries = 0;
	int i, r;

	*sgt = kmalloc(sizeof(**sgt), GFP_KERNEL);
	if (!*sgt)
		return -ENOMEM;

	/* Determine the number of DRM_BUDDY blocks to export */
	xe_res_first(res, offset, length, &cursor);
	while (cursor.remaining) {
		num_entries++;
		xe_res_next(&cursor, cursor.size);
	}

	r = sg_alloc_table(*sgt, num_entries, GFP_KERNEL);
	if (r)
		goto error_free;

	/* Initialize scatterlist nodes of sg_table */
	for_each_sgtable_sg((*sgt), sg, i)
		sg->length = 0;

	/*
	 * Walk down DRM_BUDDY blocks to populate scatterlist nodes
	 * @note: Use iterator api to get first the DRM_BUDDY block
	 * and the number of bytes from it. Access the following
	 * DRM_BUDDY block(s) if more buffer needs to exported
	 */
	xe_res_first(res, offset, length, &cursor);
	for_each_sgtable_sg((*sgt), sg, i) {
		phys_addr_t phys = cursor.start + gt->mem.vram.io_start;
		size_t size = cursor.size;
		dma_addr_t addr;

		addr = dma_map_resource(dev, phys, size, dir,
					DMA_ATTR_SKIP_CPU_SYNC);
		r = dma_mapping_error(dev, addr);
		if (r)
			goto error_unmap;

		sg_set_page(sg, NULL, size, 0);
		sg_dma_address(sg) = addr;
		sg_dma_len(sg) = size;

		xe_res_next(&cursor, cursor.size);
	}

	return 0;

error_unmap:
	for_each_sgtable_sg((*sgt), sg, i) {
		if (!sg->length)
			continue;

		dma_unmap_resource(dev, sg->dma_address,
				   sg->length, dir,
				   DMA_ATTR_SKIP_CPU_SYNC);
	}
	sg_free_table(*sgt);

error_free:
	kfree(*sgt);
	return r;
}

void xe_ttm_vram_mgr_free_sgt(struct device *dev, enum dma_data_direction dir,
			      struct sg_table *sgt)
{
	struct scatterlist *sg;
	int i;

	for_each_sgtable_sg(sgt, sg, i)
		dma_unmap_resource(dev, sg->dma_address,
				   sg->length, dir,
				   DMA_ATTR_SKIP_CPU_SYNC);
	sg_free_table(sgt);
	kfree(sgt);
}
