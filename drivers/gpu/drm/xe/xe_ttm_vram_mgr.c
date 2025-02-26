// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2022 Intel Corporation
 * Copyright (C) 2021-2002 Red Hat
 */

#include <drm/drm_managed.h>
#include <drm/drm_drv.h>

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
	struct xe_ttm_vram_mgr *mgr = to_xe_ttm_vram_mgr(man);
	struct xe_ttm_vram_mgr_resource *vres;
	struct drm_buddy *mm = &mgr->mm;
	u64 size, min_page_size;
	unsigned long lpfn;
	int err;

	lpfn = place->lpfn;
	if (!lpfn || lpfn > man->size >> PAGE_SHIFT)
		lpfn = man->size >> PAGE_SHIFT;

	if (tbo->base.size >> PAGE_SHIFT > (lpfn - place->fpfn))
		return -E2BIG; /* don't trigger eviction for the impossible */

	vres = kzalloc(sizeof(*vres), GFP_KERNEL);
	if (!vres)
		return -ENOMEM;

	ttm_resource_init(tbo, place, &vres->base);

	/* bail out quickly if there's likely not enough VRAM for this BO */
	if (ttm_resource_manager_usage(man) > man->size) {
		err = -ENOSPC;
		goto error_fini;
	}

	INIT_LIST_HEAD(&vres->blocks);

	if (place->flags & TTM_PL_FLAG_TOPDOWN)
		vres->flags |= DRM_BUDDY_TOPDOWN_ALLOCATION;

	if (place->fpfn || lpfn != man->size >> PAGE_SHIFT)
		vres->flags |= DRM_BUDDY_RANGE_ALLOCATION;

	if (WARN_ON(!vres->base.size)) {
		err = -EINVAL;
		goto error_fini;
	}
	size = vres->base.size;

	min_page_size = mgr->default_page_size;
	if (tbo->page_alignment)
		min_page_size = (u64)tbo->page_alignment << PAGE_SHIFT;

	if (WARN_ON(min_page_size < mm->chunk_size)) {
		err = -EINVAL;
		goto error_fini;
	}

	if (WARN_ON(!IS_ALIGNED(size, min_page_size))) {
		err = -EINVAL;
		goto error_fini;
	}

	mutex_lock(&mgr->lock);
	if (lpfn <= mgr->visible_size >> PAGE_SHIFT && size > mgr->visible_avail) {
		err = -ENOSPC;
		goto error_unlock;
	}

	if (place->fpfn + (size >> PAGE_SHIFT) != lpfn &&
	    place->flags & TTM_PL_FLAG_CONTIGUOUS) {
		size = roundup_pow_of_two(size);
		min_page_size = size;

		lpfn = max_t(unsigned long, place->fpfn + (size >> PAGE_SHIFT), lpfn);
	}

	err = drm_buddy_alloc_blocks(mm, (u64)place->fpfn << PAGE_SHIFT,
				     (u64)lpfn << PAGE_SHIFT, size,
				     min_page_size, &vres->blocks, vres->flags);
	if (err)
		goto error_unlock;

	if (place->flags & TTM_PL_FLAG_CONTIGUOUS) {
		if (!drm_buddy_block_trim(mm, NULL, vres->base.size, &vres->blocks))
			size = vres->base.size;
	}

	if (lpfn <= mgr->visible_size >> PAGE_SHIFT) {
		vres->used_visible_size = size;
	} else {
		struct drm_buddy_block *block;

		list_for_each_entry(block, &vres->blocks, link) {
			u64 start = drm_buddy_block_offset(block);

			if (start < mgr->visible_size) {
				u64 end = start + drm_buddy_block_size(mm, block);

				vres->used_visible_size +=
					min(end, mgr->visible_size) - start;
			}
		}
	}

	mgr->visible_avail -= vres->used_visible_size;
	mutex_unlock(&mgr->lock);

	if (!(vres->base.placement & TTM_PL_FLAG_CONTIGUOUS) &&
	    xe_is_vram_mgr_blocks_contiguous(mm, &vres->blocks))
		vres->base.placement |= TTM_PL_FLAG_CONTIGUOUS;

	/*
	 * For some kernel objects we still rely on the start when io mapping
	 * the object.
	 */
	if (vres->base.placement & TTM_PL_FLAG_CONTIGUOUS) {
		struct drm_buddy_block *block = list_first_entry(&vres->blocks,
								 typeof(*block),
								 link);

		vres->base.start = drm_buddy_block_offset(block) >> PAGE_SHIFT;
	} else {
		vres->base.start = XE_BO_INVALID_OFFSET;
	}

	*res = &vres->base;
	return 0;
error_unlock:
	mutex_unlock(&mgr->lock);
error_fini:
	ttm_resource_fini(man, &vres->base);
	kfree(vres);

	return err;
}

static void xe_ttm_vram_mgr_del(struct ttm_resource_manager *man,
				struct ttm_resource *res)
{
	struct xe_ttm_vram_mgr_resource *vres =
		to_xe_ttm_vram_mgr_resource(res);
	struct xe_ttm_vram_mgr *mgr = to_xe_ttm_vram_mgr(man);
	struct drm_buddy *mm = &mgr->mm;

	mutex_lock(&mgr->lock);
	drm_buddy_free_list(mm, &vres->blocks, 0);
	mgr->visible_avail += vres->used_visible_size;
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
	drm_printf(printer, "default_page_size: %lluKiB\n",
		   mgr->default_page_size >> 10);
	drm_printf(printer, "visible_avail: %lluMiB\n",
		   (u64)mgr->visible_avail >> 20);
	drm_printf(printer, "visible_size: %lluMiB\n",
		   (u64)mgr->visible_size >> 20);

	drm_buddy_print(mm, printer);
	mutex_unlock(&mgr->lock);
	drm_printf(printer, "man size:%llu\n", man->size);
}

static bool xe_ttm_vram_mgr_intersects(struct ttm_resource_manager *man,
				       struct ttm_resource *res,
				       const struct ttm_place *place,
				       size_t size)
{
	struct xe_ttm_vram_mgr *mgr = to_xe_ttm_vram_mgr(man);
	struct xe_ttm_vram_mgr_resource *vres =
		to_xe_ttm_vram_mgr_resource(res);
	struct drm_buddy *mm = &mgr->mm;
	struct drm_buddy_block *block;

	if (!place->fpfn && !place->lpfn)
		return true;

	if (!place->fpfn && place->lpfn == mgr->visible_size >> PAGE_SHIFT)
		return vres->used_visible_size > 0;

	list_for_each_entry(block, &vres->blocks, link) {
		unsigned long fpfn =
			drm_buddy_block_offset(block) >> PAGE_SHIFT;
		unsigned long lpfn = fpfn +
			(drm_buddy_block_size(mm, block) >> PAGE_SHIFT);

		if (place->fpfn < lpfn && place->lpfn > fpfn)
			return true;
	}

	return false;
}

static bool xe_ttm_vram_mgr_compatible(struct ttm_resource_manager *man,
				       struct ttm_resource *res,
				       const struct ttm_place *place,
				       size_t size)
{
	struct xe_ttm_vram_mgr *mgr = to_xe_ttm_vram_mgr(man);
	struct xe_ttm_vram_mgr_resource *vres =
		to_xe_ttm_vram_mgr_resource(res);
	struct drm_buddy *mm = &mgr->mm;
	struct drm_buddy_block *block;

	if (!place->fpfn && !place->lpfn)
		return true;

	if (!place->fpfn && place->lpfn == mgr->visible_size >> PAGE_SHIFT)
		return vres->used_visible_size == size;

	list_for_each_entry(block, &vres->blocks, link) {
		unsigned long fpfn =
			drm_buddy_block_offset(block) >> PAGE_SHIFT;
		unsigned long lpfn = fpfn +
			(drm_buddy_block_size(mm, block) >> PAGE_SHIFT);

		if (fpfn < place->fpfn || lpfn > place->lpfn)
			return false;
	}

	return true;
}

static const struct ttm_resource_manager_func xe_ttm_vram_mgr_func = {
	.alloc	= xe_ttm_vram_mgr_new,
	.free	= xe_ttm_vram_mgr_del,
	.intersects = xe_ttm_vram_mgr_intersects,
	.compatible = xe_ttm_vram_mgr_compatible,
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

	WARN_ON_ONCE(mgr->visible_avail != mgr->visible_size);

	drm_buddy_fini(&mgr->mm);

	ttm_resource_manager_cleanup(&mgr->manager);

	ttm_set_driver_manager(&xe->ttm, mgr->mem_type, NULL);

	mutex_destroy(&mgr->lock);
}

int __xe_ttm_vram_mgr_init(struct xe_device *xe, struct xe_ttm_vram_mgr *mgr,
			   u32 mem_type, u64 size, u64 io_size,
			   u64 default_page_size)
{
	struct ttm_resource_manager *man = &mgr->manager;
	int err;

	if (mem_type != XE_PL_STOLEN) {
		const char *name = mem_type == XE_PL_VRAM0 ? "vram0" : "vram1";
		man->cg = drmm_cgroup_register_region(&xe->drm, name, size);
		if (IS_ERR(man->cg))
			return PTR_ERR(man->cg);
	}

	man->func = &xe_ttm_vram_mgr_func;
	mgr->mem_type = mem_type;
	mutex_init(&mgr->lock);
	mgr->default_page_size = default_page_size;
	mgr->visible_size = io_size;
	mgr->visible_avail = io_size;

	ttm_resource_manager_init(man, &xe->ttm, size);
	err = drm_buddy_init(&mgr->mm, man->size, default_page_size);
	if (err)
		return err;

	ttm_set_driver_manager(&xe->ttm, mem_type, &mgr->manager);
	ttm_resource_manager_set_used(&mgr->manager, true);

	return drmm_add_action_or_reset(&xe->drm, ttm_vram_mgr_fini, mgr);
}

int xe_ttm_vram_mgr_init(struct xe_tile *tile, struct xe_ttm_vram_mgr *mgr)
{
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_mem_region *vram = &tile->mem.vram;

	mgr->vram = vram;
	return __xe_ttm_vram_mgr_init(xe, mgr, XE_PL_VRAM0 + tile->id,
				      vram->usable_size, vram->io_size,
				      PAGE_SIZE);
}

int xe_ttm_vram_mgr_alloc_sgt(struct xe_device *xe,
			      struct ttm_resource *res,
			      u64 offset, u64 length,
			      struct device *dev,
			      enum dma_data_direction dir,
			      struct sg_table **sgt)
{
	struct xe_tile *tile = &xe->tiles[res->mem_type - XE_PL_VRAM0];
	struct xe_ttm_vram_mgr_resource *vres = to_xe_ttm_vram_mgr_resource(res);
	struct xe_res_cursor cursor;
	struct scatterlist *sg;
	int num_entries = 0;
	int i, r;

	if (vres->used_visible_size < res->size)
		return -EOPNOTSUPP;

	*sgt = kmalloc(sizeof(**sgt), GFP_KERNEL);
	if (!*sgt)
		return -ENOMEM;

	/* Determine the number of DRM_BUDDY blocks to export */
	xe_res_first(res, offset, length, &cursor);
	while (cursor.remaining) {
		num_entries++;
		/* Limit maximum size to 2GiB due to SG table limitations. */
		xe_res_next(&cursor, min_t(u64, cursor.size, SZ_2G));
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
		phys_addr_t phys = cursor.start + tile->mem.vram.io_start;
		size_t size = min_t(u64, cursor.size, SZ_2G);
		dma_addr_t addr;

		addr = dma_map_resource(dev, phys, size, dir,
					DMA_ATTR_SKIP_CPU_SYNC);
		r = dma_mapping_error(dev, addr);
		if (r)
			goto error_unmap;

		sg_set_page(sg, NULL, size, 0);
		sg_dma_address(sg) = addr;
		sg_dma_len(sg) = size;

		xe_res_next(&cursor, size);
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

u64 xe_ttm_vram_get_cpu_visible_size(struct ttm_resource_manager *man)
{
	struct xe_ttm_vram_mgr *mgr = to_xe_ttm_vram_mgr(man);

	return mgr->visible_size;
}

void xe_ttm_vram_get_used(struct ttm_resource_manager *man,
			  u64 *used, u64 *used_visible)
{
	struct xe_ttm_vram_mgr *mgr = to_xe_ttm_vram_mgr(man);

	mutex_lock(&mgr->lock);
	*used = mgr->mm.size - mgr->mm.avail;
	*used_visible = mgr->visible_size - mgr->visible_avail;
	mutex_unlock(&mgr->lock);
}

u64 xe_ttm_vram_get_avail(struct ttm_resource_manager *man)
{
	struct xe_ttm_vram_mgr *mgr = to_xe_ttm_vram_mgr(man);
	u64 avail;

	mutex_lock(&mgr->lock);
	avail =  mgr->mm.avail;
	mutex_unlock(&mgr->lock);

	return avail;
}
