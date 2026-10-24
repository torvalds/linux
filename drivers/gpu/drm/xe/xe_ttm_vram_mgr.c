// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021-2022 Intel Corporation
 * Copyright (C) 2021-2022 Red Hat
 */

#include <linux/cgroup_dmem.h>
#include <linux/debugfs.h>

#include <drm/drm_managed.h>
#include <drm/drm_drv.h>
#include <drm/drm_buddy.h>
#include <uapi/drm/xe_drm.h>

#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_range_manager.h>

#include "regs/xe_regs.h"
#include "xe_bo.h"
#include "xe_configfs.h"
#include "xe_device.h"
#include "xe_exec_queue.h"
#include "xe_lrc.h"
#include "xe_mmio.h"
#include "xe_pm.h"
#include "xe_printk.h"
#include "xe_res_cursor.h"
#include "xe_ttm_stolen_mgr.h"
#include "xe_ttm_vram_mgr.h"
#include "xe_vram_types.h"

static inline struct gpu_buddy_block *
xe_ttm_vram_mgr_first_block(struct list_head *list)
{
	return list_first_entry_or_null(list, struct gpu_buddy_block, link);
}

static inline bool xe_is_vram_mgr_blocks_contiguous(struct gpu_buddy *mm,
						    struct list_head *head)
{
	struct gpu_buddy_block *block;
	u64 start, size;

	block = xe_ttm_vram_mgr_first_block(head);
	if (!block)
		return false;

	while (head != block->link.next) {
		start = gpu_buddy_block_offset(block);
		size = gpu_buddy_block_size(mm, block);

		block = list_entry(block->link.next, struct gpu_buddy_block,
				   link);
		if (start + size != gpu_buddy_block_offset(block))
			return false;
	}

	return true;
}

static int xe_ttm_vram_buddy_alloc(struct xe_ttm_vram_mgr *mgr, u64 start,
				   u64 end, u64 size, u64 min_page_size,
				   struct list_head *blocks, unsigned long flags,
				   struct ttm_resource *res, u64 *used_visible)
{
	struct gpu_buddy *mm = &mgr->mm;
	struct gpu_buddy_block *block;
	int err;

	err = gpu_buddy_alloc_blocks(mm, start, end, size, min_page_size, blocks, flags);
	if (err)
		return err;

	/*
	 * Track the owning resource, never the owning BO. A BO backpointer
	 * cached here goes stale the moment TTM hands the resource to a ghost
	 * object (ttm_buffer_object_transfer()), which happens on every
	 * accelerated move and on pipelined gutting. The resource, in
	 * contrast, has exactly the same lifetime as these blocks and TTM
	 * keeps &ttm_resource.bo pointing at the current owner for us.
	 */
	list_for_each_entry(block, blocks, link)
		block->private = res;

	if (end <= mgr->visible_size) {
		*used_visible = size;
	} else {
		list_for_each_entry(block, blocks, link) {
			u64 blk_start = gpu_buddy_block_offset(block);

			if (blk_start < mgr->visible_size) {
				u64 blk_end = blk_start + gpu_buddy_block_size(mm, block);

				*used_visible += min(blk_end, mgr->visible_size) - blk_start;
			}
		}
	}

	mgr->visible_avail -= *used_visible;
	return 0;
}

static int xe_ttm_vram_mgr_new(struct ttm_resource_manager *man,
			       struct ttm_buffer_object *tbo,
			       const struct ttm_place *place,
			       struct ttm_resource **res)
{
	struct xe_ttm_vram_mgr *mgr = to_xe_ttm_vram_mgr(man);
	struct xe_ttm_vram_mgr_resource *vres;
	struct gpu_buddy *mm = &mgr->mm;
	u64 size, min_page_size;
	unsigned long lpfn;
	int err;

	lpfn = place->lpfn;
	if (!lpfn || lpfn > man->size >> PAGE_SHIFT)
		lpfn = man->size >> PAGE_SHIFT;

	if (tbo->base.size >> PAGE_SHIFT > (lpfn - place->fpfn))
		return -E2BIG; /* don't trigger eviction for the impossible */

	vres = kzalloc_obj(*vres);
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
		vres->flags |= GPU_BUDDY_TOPDOWN_ALLOCATION;

	if (place->flags & TTM_PL_FLAG_CONTIGUOUS)
		vres->flags |= GPU_BUDDY_CONTIGUOUS_ALLOCATION;

	if (place->fpfn || lpfn != man->size >> PAGE_SHIFT)
		vres->flags |= GPU_BUDDY_RANGE_ALLOCATION;

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

	err = xe_ttm_vram_buddy_alloc(mgr, (u64)place->fpfn << PAGE_SHIFT,
				      (u64)lpfn << PAGE_SHIFT, size,
				      min_page_size, &vres->blocks, vres->flags,
				      &vres->base, &vres->used_visible_size);
	if (err)
		goto error_unlock;
	mutex_unlock(&mgr->lock);

	if (!(vres->base.placement & TTM_PL_FLAG_CONTIGUOUS) &&
	    xe_is_vram_mgr_blocks_contiguous(mm, &vres->blocks))
		vres->base.placement |= TTM_PL_FLAG_CONTIGUOUS;

	/*
	 * For some kernel objects we still rely on the start when io mapping
	 * the object.
	 */
	if (vres->base.placement & TTM_PL_FLAG_CONTIGUOUS) {
		struct gpu_buddy_block *block = list_first_entry(&vres->blocks,
								 typeof(*block),
								 link);

		vres->base.start = gpu_buddy_block_offset(block) >> PAGE_SHIFT;
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

static void xe_ttm_vram_buddy_free(struct xe_ttm_vram_mgr *mgr,
				   struct list_head *blocks,
				   u64 used_visible)
{
	struct gpu_buddy_block *block;

	list_for_each_entry(block, blocks, link)
		block->private = NULL;
	gpu_buddy_free_list(&mgr->mm, blocks, 0);
	mgr->visible_avail += used_visible;
}

/*
 * Retry pending page-offline reservations.
 *
 * A reservation can fail because the blocks backing the bad page are still
 * allocated: either the owning BO could not be purged, or the purge was
 * pipelined and TTM handed the resource to a ghost object which frees it
 * only once the move fences signal. Rather than giving up, entries stay on
 * @queued_pages and are retried here every time VRAM blocks come back.
 *
 * Called with @mgr->lock held.
 */
static void xe_ttm_vram_retry_queued_pages(struct xe_ttm_vram_mgr *mgr)
{
	struct xe_ttm_vram_offline_resource *pos, *n;

	lockdep_assert_held(&mgr->lock);

	list_for_each_entry_safe(pos, n, &mgr->queued_pages, queued_link) {
		if (xe_ttm_vram_buddy_alloc(mgr, pos->addr, pos->addr + PAGE_SIZE,
					    PAGE_SIZE, PAGE_SIZE, &pos->blocks,
					    GPU_BUDDY_RANGE_ALLOCATION, NULL,
					    &pos->used_visible_size)) {
			pos->status = XE_PAGE_RESERVE_FAIL;
			continue;
		}
		--mgr->n_queued_pages;
		list_del_rcu(&pos->queued_link);
		++mgr->n_offlined_pages;
		list_add_rcu(&pos->offlined_link, &mgr->offlined_pages);
	}
}

static void xe_ttm_vram_mgr_del(struct ttm_resource_manager *man,
				struct ttm_resource *res)
{
	struct xe_ttm_vram_mgr_resource *vres =
		to_xe_ttm_vram_mgr_resource(res);
	struct xe_ttm_vram_mgr *mgr = to_xe_ttm_vram_mgr(man);

	mutex_lock(&mgr->lock);
	xe_ttm_vram_buddy_free(mgr, &vres->blocks, vres->used_visible_size);
	if (unlikely(!list_empty(&mgr->queued_pages)))
		xe_ttm_vram_retry_queued_pages(mgr);
	mutex_unlock(&mgr->lock);

	ttm_resource_fini(man, res);

	kfree(vres);
}

static void xe_ttm_vram_mgr_debug(struct ttm_resource_manager *man,
				  struct drm_printer *printer)
{
	struct xe_ttm_vram_mgr *mgr = to_xe_ttm_vram_mgr(man);
	struct gpu_buddy *mm = &mgr->mm;

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
	struct gpu_buddy *mm = &mgr->mm;
	struct gpu_buddy_block *block;

	if (!place->fpfn && !place->lpfn)
		return true;

	if (!place->fpfn && place->lpfn == mgr->visible_size >> PAGE_SHIFT)
		return vres->used_visible_size > 0;

	list_for_each_entry(block, &vres->blocks, link) {
		unsigned long fpfn =
			gpu_buddy_block_offset(block) >> PAGE_SHIFT;
		unsigned long lpfn = fpfn +
			(gpu_buddy_block_size(mm, block) >> PAGE_SHIFT);

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
	struct gpu_buddy *mm = &mgr->mm;
	struct gpu_buddy_block *block;

	if (!place->fpfn && !place->lpfn)
		return true;

	if (!place->fpfn && place->lpfn == mgr->visible_size >> PAGE_SHIFT)
		return vres->used_visible_size == size;

	list_for_each_entry(block, &vres->blocks, link) {
		unsigned long fpfn =
			gpu_buddy_block_offset(block) >> PAGE_SHIFT;
		unsigned long lpfn = fpfn +
			(gpu_buddy_block_size(mm, block) >> PAGE_SHIFT);

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

static const struct dmem_cgroup_ops xe_ttm_vram_mgr_dmem_ops;

static int xe_ttm_vram_mgr_dmem_reclaim(struct dmem_cgroup_pool_state *pool,
					u64 target_bytes, void *priv)
{
	struct ttm_resource_manager *man = priv;
	struct xe_device *xe = ttm_to_xe_device(man->bdev);
	int ret, idx;

	if (!drm_dev_enter(&xe->drm, &idx))
		return -ENODEV;

	{
		ACQUIRE(xe_pm_runtime_ioctl, pm)(xe);

		ret = ACQUIRE_ERR(xe_pm_runtime_ioctl, &pm);
		if (ret >= 0)
			ret = ttm_resource_manager_dmem_reclaim(pool, target_bytes, priv);
	}

	drm_dev_exit(idx);
	return ret;
}

static const struct dmem_cgroup_ops xe_ttm_vram_mgr_dmem_ops = {
	.reclaim = xe_ttm_vram_mgr_dmem_reclaim,
};

static void xe_ttm_vram_mgr_set_unused(struct drm_device *dev, void *arg)
{
	struct ttm_resource_manager *man = arg;

	ttm_resource_manager_set_used(man, false);
}

static void xe_ttm_vram_free_bad_pages(struct xe_ttm_vram_mgr *mgr)
{
	struct xe_ttm_vram_offline_resource *pos, *n;

	list_for_each_entry_safe(pos, n, &mgr->offlined_pages, offlined_link) {
		list_del_rcu(&pos->offlined_link);
		xe_ttm_vram_buddy_free(mgr, &pos->blocks, pos->used_visible_size);
		--mgr->n_offlined_pages;
		kfree_rcu(pos, rcu);
	}
	list_for_each_entry_safe(pos, n, &mgr->queued_pages, queued_link) {
		list_del_rcu(&pos->queued_link);
		/* queued entries have no buddy reservation yet */
		xe_ttm_vram_buddy_free(mgr, &pos->blocks, 0);
		--mgr->n_queued_pages;
		kfree_rcu(pos, rcu);
	}
}

static void xe_ttm_vram_mgr_fini(struct drm_device *dev, void *arg)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_ttm_vram_mgr *mgr = arg;
	struct ttm_resource_manager *man = &mgr->manager;

	mutex_lock(&mgr->lock);
	xe_ttm_vram_free_bad_pages(mgr);
	mutex_unlock(&mgr->lock);

	if (ttm_resource_manager_evict_all(&xe->ttm, man))
		return;

	WARN_ON_ONCE(mgr->visible_avail != mgr->visible_size);

	gpu_buddy_fini(&mgr->mm);

	ttm_resource_manager_cleanup(&mgr->manager);

	ttm_set_driver_manager(&xe->ttm, mgr->mem_type, NULL);
}

int __xe_ttm_vram_mgr_init(struct xe_device *xe, struct xe_ttm_vram_mgr *mgr,
			   u32 mem_type, u64 size, u64 io_size,
			   u64 default_page_size)
{
	struct ttm_resource_manager *man = &mgr->manager;
	struct dmem_cgroup_region *cg;
	const char *name;
	int err;

	man->func = &xe_ttm_vram_mgr_func;
	mgr->mem_type = mem_type;
	err = drmm_mutex_init(&xe->drm, &mgr->lock);
	if (err)
		return err;
	INIT_LIST_HEAD(&mgr->offlined_pages);
	INIT_LIST_HEAD(&mgr->queued_pages);
	mgr->default_page_size = default_page_size;
	mgr->visible_size = io_size;
	mgr->visible_avail = io_size;

	ttm_resource_manager_init(man, &xe->ttm, size);
	err = gpu_buddy_init(&mgr->mm, man->size, default_page_size);
	if (err)
		return err;

	gpu_buddy_driver_set_lock(&mgr->mm, &mgr->lock);
	ttm_set_driver_manager(&xe->ttm, mem_type, &mgr->manager);

	/*
	 * Register the fini action before the cgroup region so that devres
	 * LIFO teardown runs unregister_region before manager teardown
	 * (draining any in-flight reclaim callbacks) and the manager fini second.
	 */
	err = drmm_add_action_or_reset(&xe->drm, xe_ttm_vram_mgr_fini, mgr);
	if (err)
		return err;

	name = mem_type == XE_PL_VRAM0 ? "vram0" : "vram1";
	cg = drmm_cgroup_register_region(&xe->drm, name,
					 &(struct dmem_cgroup_init){
						.size = size,
						.ops = &xe_ttm_vram_mgr_dmem_ops,
						.reclaim_priv = man,
					 });
	if (IS_ERR(cg))
		return PTR_ERR(cg);

	ttm_resource_manager_set_dmem_region(man, cg);
	ttm_resource_manager_set_used(man, true);

	return drmm_add_action_or_reset(&xe->drm, xe_ttm_vram_mgr_set_unused, man);
}

/**
 * xe_ttm_vram_mgr_init - initialize TTM VRAM region
 * @xe: pointer to Xe device
 * @vram: pointer to xe_vram_region that contains the memory region attributes
 *
 * Initialize the Xe TTM for given @vram region using the given parameters.
 *
 * Returns 0 for success, negative error code otherwise.
 */
int xe_ttm_vram_mgr_init(struct xe_device *xe, struct xe_vram_region *vram)
{
	return __xe_ttm_vram_mgr_init(xe, &vram->ttm, vram->placement,
				      xe_vram_region_usable_size(vram),
				      xe_vram_region_io_size(vram),
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

	*sgt = kmalloc_obj(**sgt);
	if (!*sgt)
		return -ENOMEM;

	/* Determine the number of GPU_BUDDY blocks to export */
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
	 * Walk down GPU_BUDDY blocks to populate scatterlist nodes
	 * @note: Use iterator api to get first the GPU_BUDDY block
	 * and the number of bytes from it. Access the following
	 * GPU_BUDDY block(s) if more buffer needs to exported
	 */
	xe_res_first(res, offset, length, &cursor);
	for_each_sgtable_sg((*sgt), sg, i) {
		phys_addr_t phys = cursor.start + xe_vram_region_io_start(tile->mem.vram);
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

static int xe_ttm_vram_purge_page(struct xe_device *xe, struct xe_bo *bo)
{
	u32 q_flag = DRM_XE_EXEC_QUEUE_BAN_REASON_PAGE_OFFLINE;
	struct ttm_operation_ctx ctx = {};
	struct xe_exec_queue *q_to_put = NULL;
	struct xe_exec_queue *q = NULL;
	struct xe_vm *vm = NULL;
	u32	flags;
	int ret = 0;

	xe_bo_lock(bo, false);
	if (bo->vm)
		vm = xe_vm_get(bo->vm);
	flags = bo->flags;
	xe_bo_unlock(bo);
	/*  Ban VM if BO is PPGTT */
	if (vm && (flags & XE_BO_FLAG_PAGETABLE)) {
		struct xe_exec_queue *eq;
		int id;

		down_write(&vm->lock);
		if (xe->info.has_ctx_tlb_inval) {
			/*
			 * Must be the write lock: send_tlb_inval_ctx_ppgtt()
			 * mutates this list (list_move_tail() onto an on-stack
			 * head) while holding only the read lock, relying on
			 * tlb_inval->seqno_lock to keep itself the sole
			 * mutator. Traversing it under down_read() would let
			 * this walk follow entries onto that stack list.
			 */
			down_write(&vm->exec_queues.lock);
			for (id = 0; id < ARRAY_SIZE(vm->exec_queues.list); id++)
				list_for_each_entry(eq, &vm->exec_queues.list[id],
						    vm_exec_queue_link)
					atomic_or(q_flag, &eq->ban_reason);
			up_write(&vm->exec_queues.lock);
		} else {
			list_for_each_entry(eq, &vm->preempt.exec_queues, lr.link)
				atomic_or(q_flag, &eq->ban_reason);
		}
		smp_wmb(); /* Force all queue bits to be visible before killing the VM */
		xe_vm_kill(vm, true);
		up_write(&vm->lock);
	}
	if (vm)
		xe_vm_put(vm);

	xe_bo_lock(bo, false);
	q = READ_ONCE(bo->q);
	/*  Ban exec queue if BO is lrc */
	if (q && xe_exec_queue_get_unless_zero(q)) {
		/* ban queue */
		atomic_or(q_flag, &q->ban_reason);
		smp_wmb(); /* Force bit change to finish before state change triggers */
		q_to_put = q;
	}

	if (bo->purgeable.state == XE_MADV_PURGEABLE_PURGED) {
		/* Already purged by shrinker during unlocked window — nothing to do */
		xe_bo_unlock(bo);
		goto out;
	}

	xe_bo_set_purgeable_state(bo, XE_MADV_PURGEABLE_DONTNEED);
	ttm_bo_unmap_virtual(&bo->ttm);   /* nuke CPU mmap + VRAM IO mappings */
	if (xe_bo_is_pinned(bo))
		xe_bo_unpin(bo);
	ret = xe_ttm_bo_purge(&bo->ttm, &ctx);
	xe_bo_unlock(bo);

out:
	if (q_to_put) {
		xe_exec_queue_kill(q_to_put);
		xe_exec_queue_put(q_to_put);
	}

	return ret;
}

static bool xe_ttm_vram_page_already_processed(struct xe_ttm_vram_mgr *mgr,
					       u64 addr)
{
	struct xe_ttm_vram_offline_resource *pos;

	lockdep_assert_held(&mgr->lock);

	list_for_each_entry(pos, &mgr->offlined_pages, offlined_link) {
		if (pos->addr == addr)
			return true;
	}

	list_for_each_entry(pos, &mgr->queued_pages, queued_link) {
		if (pos->addr == addr)
			return true;
	}

	return false;
}

/*
 * Resolve the BO currently owning @block and take a reference on it.
 *
 * Called with @mgr->lock held, which serializes against
 * xe_ttm_vram_buddy_free() clearing block->private.
 *
 * Returns NULL when there is no xe_bo we can act on: either the block is
 * free, or the resource is temporarily owned by a TTM ghost object because
 * a move or a pipelined gutting is still in flight. In both cases the
 * blocks will hit xe_ttm_vram_mgr_del() on their own and the pending
 * reservation is retried from there.
 */
static struct xe_bo *xe_ttm_vram_block_owner_get(struct xe_device *xe,
						 struct gpu_buddy_block *block)
{
	struct ttm_resource *res = block->private;
	struct ttm_buffer_object *tbo;
	struct xe_bo *bo;

	if (!res)
		return NULL;

	guard(spinlock)(&xe->ttm.lru_lock);

	/*
	 * res->bo is updated under bdev->lru_lock by ttm_resource_set_bo().
	 * Racing with a ghost transfer here is benign: we either see the old
	 * owner (whose purge is a no-op and the retry path recovers) or the
	 * ghost (rejected below).
	 *
	 * A ghost is a bare ttm_transfer_obj, not an xe_bo, so ttm_to_xe_bo()
	 * on one would be out of bounds. xe_bo_is_xe_bo() rejects it since
	 * only our own BOs carry xe_ttm_bo_destroy().
	 */
	tbo = READ_ONCE(res->bo);
	if (!tbo || !xe_bo_is_xe_bo(tbo))
		return NULL;

	bo = ttm_to_xe_bo(tbo);

	/* The BO may already be in teardown with a zero refcount */
	return xe_bo_get_unless_zero(bo) ? bo : NULL;
}

static int xe_ttm_vram_reserve_page_at_addr(struct xe_device *xe, u64 addr,
					    struct xe_ttm_vram_mgr *vram_mgr, struct gpu_buddy *mm)
{
	struct xe_ttm_vram_offline_resource *nentry;
	struct xe_bo *pbo_to_put = NULL;
	struct xe_bo *pbo = NULL;
	struct gpu_buddy_block *block;
	u64 size = PAGE_SIZE;
	int ret = 0;

	scoped_guard(mutex, &vram_mgr->lock) {
		if (xe_ttm_vram_page_already_processed(vram_mgr, addr))
			return -EEXIST;
		block = gpu_buddy_allocated_addr_to_block(mm, addr);
		if (WARN_ON(IS_ERR(block)))
			return PTR_ERR(block);

		nentry = kzalloc_obj(*nentry);
		if (!nentry)
			return -ENOMEM;
		INIT_LIST_HEAD(&nentry->blocks);
		nentry->status = XE_PAGE_RESERVE_PENDING;
		nentry->addr = addr;

		if (block) {
			pbo = xe_ttm_vram_block_owner_get(xe, block);

			/*
			 * Critical kernel BO? Best-effort check without resv lock;
			 * worst case a concurrent pin causes reset path unnecessarily.
			 */
			if (pbo && ((pbo->ttm.type == ttm_bo_type_kernel &&
				     !(pbo->flags & XE_BO_FLAG_PINNED_LATE_RESTORE)) ||
				    (xe_bo_is_user(pbo) && xe_bo_is_pinned(pbo)))) {
				kfree(nentry);
				pbo_to_put = pbo;
				drm_err(&xe->drm,
					"%s: addr: 0x%llx is critical kernel bo, requesting SBR\n",
					__func__, addr);
				break;
			}
			/* Queue free(to-be-purged) pages */
			++vram_mgr->n_queued_pages;
			list_add_rcu(&nentry->queued_link, &vram_mgr->queued_pages);
		} else {
			/* Immediately offline unoccupied pages */
			/* Queue free(to-be-reserved) pages */
			ret = xe_ttm_vram_buddy_alloc(vram_mgr, addr, addr + size,
						      size, size, &nentry->blocks,
						      GPU_BUDDY_RANGE_ALLOCATION,
						      NULL, &nentry->used_visible_size);
			if (ret) {
				nentry->status = XE_PAGE_RESERVE_FAIL;
				drm_dbg(&xe->drm,
					"Page at addr:0x%llx still busy (%d), deferring reservation\n",
					addr, ret);
				++vram_mgr->n_queued_pages;
				list_add_rcu(&nentry->queued_link, &vram_mgr->queued_pages);
				return 0;
			}
			++vram_mgr->n_offlined_pages;
			list_add_rcu(&nentry->offlined_link, &vram_mgr->offlined_pages);
			return ret;
		}
	}

	/* Deferred put outside lock to avoid recursive deadlock */
	if (pbo_to_put) {
		xe_bo_put(pbo_to_put);
		/* Hint System controller driver for reset with -EIO  */
		return -EIO;
	}

	if (pbo) {
		/*
		 * Purge BO containing address - reference held from above.
		 * This does not necessarily free the blocks synchronously: if
		 * the BO is not idle, ttm_bo_pipeline_gutting() hands the
		 * resource to a ghost object and it is released only once the
		 * move fences signal. The reservation below then fails and is
		 * retried from xe_ttm_vram_mgr_del().
		 */
		ret = xe_ttm_vram_purge_page(xe, pbo);
		xe_bo_put(pbo);
		if (ret)
			drm_warn(&xe->drm, "Purge failed at addr:0x%llx, ret:%d\n", addr, ret);
	}

	return 0;
}

static struct xe_vram_region *xe_ttm_vram_addr_to_region(struct xe_device *xe, u64 addr)
{
	struct xe_tile *tile;
	u8 id;

	for_each_tile(tile, xe, id) {
		struct xe_vram_region *vr = tile->mem.vram;

		if (!vr)
			continue;

		if (addr >= vr->dpa_base && addr < (vr->dpa_base + vr->usable_size))
			return vr;

		/* CCS, GSM, or DSM — infrastructure zone, needs reset */
		if (addr >= (vr->dpa_base + vr->usable_size) &&
		    addr < (vr->dpa_base + vr->actual_physical_size))
			return NULL;
	}

	/*
	 * Return an explicit error pointer so the caller knows the addr
	 * is invalid and should be ignored, NOT SBR.
	 */
	return ERR_PTR(-EOPNOTSUPP);
}

/**
 * xe_ttm_vram_handle_addr_fault - Handle vram physical address error flaged
 * @xe: pointer to parent device
 * @addr: physical faulty address
 *
 * Handle the physcial faulty address error on specific tile.
 *
 * Returns 0 for success, negative error code otherwise as follow:
 * * %-EIO - critical BO or address outside any VRAM region; next action is reset.
 * * %-EOPNOTSUPP - log-only policy or unknown address; no further action.
 * * %-ENOMEM - allocation failure; next action is reset.
 * * %-ENXIO - address not found in buddy; no further action.
 * * %-EEXIST - address already processed; no further action.
 *
 * A return of 0 means the page is tracked. It may still be listed as
 * pending if the blocks backing it could not be freed immediately; the
 * reservation is then completed from xe_ttm_vram_mgr_del().
 */
int xe_ttm_vram_handle_addr_fault(struct xe_device *xe, u64 addr)
{
	struct xe_ttm_vram_mgr *vram_mgr;
	struct xe_vram_region *vr;
	struct gpu_buddy *mm;

	/* Assert that the address is PAGE_SIZE aligned */
	if (WARN_ON_ONCE(!IS_ALIGNED(addr, PAGE_SIZE))) {
		drm_err(&xe->drm, "Address %llx is not %lu aligned!\n", addr, PAGE_SIZE);
		return -EINVAL;
	}

	vr = xe_ttm_vram_addr_to_region(xe, addr);
	if (IS_ERR(vr)) {
		/*
		 * The addr is outside VRAM and GSM.
		 * Log a debug message if needed, and safely exit/ignore.
		 */
		drm_dbg(&xe->drm, "Address %llx is out of bounds, ignoring fault.\n", addr);
		return PTR_ERR(vr);
	}
	if (!vr) {
		drm_err(&xe->drm, "%s:%d GSM addr:%llx error requesting SBR\n",
			__func__, __LINE__, addr);
		/* Hint System controller driver for reset with -EIO  */
		return -EIO;
	}
	vram_mgr = &vr->ttm;
	mm = &vram_mgr->mm;

	if (xe->ras.disable_vram_page_offline) {
		xe_err(xe, "0x%llx is reported as corrupted address by HW\n",
		       addr);
		return -EOPNOTSUPP;
	}

	/* Reserve page at address */
	return xe_ttm_vram_reserve_page_at_addr(xe, addr - vr->dpa_base, vram_mgr, mm);
}
EXPORT_SYMBOL(xe_ttm_vram_handle_addr_fault);

/**
 * xe_ttm_vram_inject_fault - Inject a VRAM page fault for testing
 * @xe: xe device instance
 *
 * Picks the last unallocated VRAM page and reports it as faulted
 * via xe_ttm_vram_handle_addr_fault(). Used by the fault-inject
 * debugfs interface for testing page offlining.
 *
 * Note: Executing this test will permanently retire the allocated
 * memory tracking pages. The driver must be rebinded (unbind and bind)
 * post-test execution to reclaim the reserved space, as these pages
 * cannot be freed or reclaimed dynamically while the current instance
 * remains active.
 *
 * Return: 0 on success, negative error code on failure.
 */
int xe_ttm_vram_inject_fault(struct xe_device *xe)
{
	struct xe_tile *tile = xe_device_get_root_tile(xe);
	struct xe_vram_region *vr = tile->mem.vram;
	struct xe_ttm_vram_mgr *vram_mgr = &vr->ttm;
	struct gpu_buddy *mm = &vram_mgr->mm;
	u64 addr;

	if (vr->actual_physical_size < PAGE_SIZE)
		return -ENOSPC;

	addr = vr->actual_physical_size - PAGE_SIZE;
	while (addr < vr->actual_physical_size) {
		struct gpu_buddy_block *block;
		bool found = false;

		scoped_guard(mutex, &vram_mgr->lock) {
			block = gpu_buddy_allocated_addr_to_block(mm, addr);
			if (!block)
				found = true;
		}

		/*
		 * Intentional race window: xe_ttm_vram_handle_addr_fault()
		 * re-acquires vram_mgr->lock internally, so we cannot hold
		 * it here. A concurrent allocation claiming this page between
		 * the two calls is an acceptable false negative for this
		 * test-only path.
		 */
		if (found)
			return xe_ttm_vram_handle_addr_fault(xe, addr + vr->dpa_base);

		cond_resched();
		if (addr == 0)
			break;
		addr -= PAGE_SIZE;
	}

	return -ENOSPC;
}
EXPORT_SYMBOL(xe_ttm_vram_inject_fault);

static int vram_bad_pages_show(struct seq_file *m, void *unused)
{
	struct xe_device *xe = m->private;
	struct xe_ttm_vram_offline_resource *pos;
	struct ttm_resource_manager *man;
	struct xe_ttm_vram_mgr *mgr;
	struct xe_tile *tile;
	u8 id;

	man = ttm_manager_type(&xe->ttm, XE_PL_VRAM0);
	if (man)
		/* TODO Hook with RAS to show max_pages fetched from FW */
		seq_printf(m, "max_pages: %d\n",
			   to_xe_ttm_vram_mgr(man)->max_pages);

	for_each_tile(tile, xe, id) {
		struct xe_vram_region *vr = tile->mem.vram;

		man = ttm_manager_type(&xe->ttm, XE_PL_VRAM0 + id);
		if (!man || !vr)
			continue;
		mgr = to_xe_ttm_vram_mgr(man);

		rcu_read_lock();

		list_for_each_entry_rcu(pos, &mgr->offlined_pages, offlined_link) {
			u64 pfn;

			pfn = (pos->addr + vr->dpa_base) >> PAGE_SHIFT;
			seq_printf(m, "0x%016llx : 0x%016lx : R\n", pfn, PAGE_SIZE);
		}

		list_for_each_entry_rcu(pos, &mgr->queued_pages, queued_link) {
			u64 pfn;

			pfn = (pos->addr + vr->dpa_base) >> PAGE_SHIFT;
			seq_printf(m, "0x%016llx : 0x%016lx : %c\n",
				   pfn, PAGE_SIZE, pos->status ? 'F' : 'P');
		}

		rcu_read_unlock();
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(vram_bad_pages);

/**
 * xe_ttm_vram_debugfs_init - Initialize VRAM debugfs interfaces
 * @xe: The xe device structure pointer
 * @root: The root dentry of the debugfs directory
 *
 * This function registers platform-specific VRAM debugfs files used for
 * testing and debugging. Currently, it exposes the "vram_bad_pages" interface
 * to inspect marked faulty memory pages, restricted specifically to the
 * %XE_CRESCENTISLAND platform.
 *
 * Return: Void.
 */
void xe_ttm_vram_debugfs_init(struct xe_device *xe, struct dentry *root)
{
	/*
	 * TODO: Replace platform check with xe->info
	 * once the feature flag is plumbed through device info.
	 */
	if (xe->info.platform != XE_CRESCENTISLAND)
		return;
	debugfs_create_file("vram_bad_pages", 0444, root, xe, &vram_bad_pages_fops);
}
