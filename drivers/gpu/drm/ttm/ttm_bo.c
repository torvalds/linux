/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
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
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */

#define pr_fmt(fmt) "[TTM] " fmt

#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/atomic.h>
#include <linux/dma-resv.h>

#include "ttm_module.h"

/* default destructor */
static void ttm_bo_default_destroy(struct ttm_buffer_object *bo)
{
	kfree(bo);
}

static void ttm_bo_mem_space_debug(struct ttm_buffer_object *bo,
					struct ttm_placement *placement)
{
	struct drm_printer p = drm_debug_printer(TTM_PFX);
	struct ttm_resource_manager *man;
	int i, mem_type;

	drm_printf(&p, "No space for %p (%lu pages, %zuK, %zuM)\n",
		   bo, bo->resource->num_pages, bo->base.size >> 10,
		   bo->base.size >> 20);
	for (i = 0; i < placement->num_placement; i++) {
		mem_type = placement->placement[i].mem_type;
		drm_printf(&p, "  placement[%d]=0x%08X (%d)\n",
			   i, placement->placement[i].flags, mem_type);
		man = ttm_manager_type(bo->bdev, mem_type);
		ttm_resource_manager_debug(man, &p);
	}
}

static void ttm_bo_del_from_lru(struct ttm_buffer_object *bo)
{
	struct ttm_device *bdev = bo->bdev;

	list_del_init(&bo->lru);

	if (bdev->funcs->del_from_lru_notify)
		bdev->funcs->del_from_lru_notify(bo);
}

static void ttm_bo_bulk_move_set_pos(struct ttm_lru_bulk_move_pos *pos,
				     struct ttm_buffer_object *bo)
{
	if (!pos->first)
		pos->first = bo;
	pos->last = bo;
}

void ttm_bo_move_to_lru_tail(struct ttm_buffer_object *bo,
			     struct ttm_resource *mem,
			     struct ttm_lru_bulk_move *bulk)
{
	struct ttm_device *bdev = bo->bdev;
	struct ttm_resource_manager *man;

	if (!bo->deleted)
		dma_resv_assert_held(bo->base.resv);

	if (bo->pin_count) {
		ttm_bo_del_from_lru(bo);
		return;
	}

	if (!mem)
		return;

	man = ttm_manager_type(bdev, mem->mem_type);
	list_move_tail(&bo->lru, &man->lru[bo->priority]);

	if (bdev->funcs->del_from_lru_notify)
		bdev->funcs->del_from_lru_notify(bo);

	if (bulk && !bo->pin_count) {
		switch (bo->resource->mem_type) {
		case TTM_PL_TT:
			ttm_bo_bulk_move_set_pos(&bulk->tt[bo->priority], bo);
			break;

		case TTM_PL_VRAM:
			ttm_bo_bulk_move_set_pos(&bulk->vram[bo->priority], bo);
			break;
		}
	}
}
EXPORT_SYMBOL(ttm_bo_move_to_lru_tail);

void ttm_bo_bulk_move_lru_tail(struct ttm_lru_bulk_move *bulk)
{
	unsigned i;

	for (i = 0; i < TTM_MAX_BO_PRIORITY; ++i) {
		struct ttm_lru_bulk_move_pos *pos = &bulk->tt[i];
		struct ttm_resource_manager *man;

		if (!pos->first)
			continue;

		dma_resv_assert_held(pos->first->base.resv);
		dma_resv_assert_held(pos->last->base.resv);

		man = ttm_manager_type(pos->first->bdev, TTM_PL_TT);
		list_bulk_move_tail(&man->lru[i], &pos->first->lru,
				    &pos->last->lru);
	}

	for (i = 0; i < TTM_MAX_BO_PRIORITY; ++i) {
		struct ttm_lru_bulk_move_pos *pos = &bulk->vram[i];
		struct ttm_resource_manager *man;

		if (!pos->first)
			continue;

		dma_resv_assert_held(pos->first->base.resv);
		dma_resv_assert_held(pos->last->base.resv);

		man = ttm_manager_type(pos->first->bdev, TTM_PL_VRAM);
		list_bulk_move_tail(&man->lru[i], &pos->first->lru,
				    &pos->last->lru);
	}
}
EXPORT_SYMBOL(ttm_bo_bulk_move_lru_tail);

static int ttm_bo_handle_move_mem(struct ttm_buffer_object *bo,
				  struct ttm_resource *mem, bool evict,
				  struct ttm_operation_ctx *ctx,
				  struct ttm_place *hop)
{
	struct ttm_resource_manager *old_man, *new_man;
	struct ttm_device *bdev = bo->bdev;
	int ret;

	old_man = ttm_manager_type(bdev, bo->resource->mem_type);
	new_man = ttm_manager_type(bdev, mem->mem_type);

	ttm_bo_unmap_virtual(bo);

	/*
	 * Create and bind a ttm if required.
	 */

	if (new_man->use_tt) {
		/* Zero init the new TTM structure if the old location should
		 * have used one as well.
		 */
		ret = ttm_tt_create(bo, old_man->use_tt);
		if (ret)
			goto out_err;

		if (mem->mem_type != TTM_PL_SYSTEM) {
			ret = ttm_tt_populate(bo->bdev, bo->ttm, ctx);
			if (ret)
				goto out_err;
		}
	}

	ret = bdev->funcs->move(bo, evict, ctx, mem, hop);
	if (ret) {
		if (ret == -EMULTIHOP)
			return ret;
		goto out_err;
	}

	ctx->bytes_moved += bo->base.size;
	return 0;

out_err:
	new_man = ttm_manager_type(bdev, bo->resource->mem_type);
	if (!new_man->use_tt)
		ttm_bo_tt_destroy(bo);

	return ret;
}

/*
 * Call bo::reserved.
 * Will release GPU memory type usage on destruction.
 * This is the place to put in driver specific hooks to release
 * driver private resources.
 * Will release the bo::reserved lock.
 */

static void ttm_bo_cleanup_memtype_use(struct ttm_buffer_object *bo)
{
	if (bo->bdev->funcs->delete_mem_notify)
		bo->bdev->funcs->delete_mem_notify(bo);

	ttm_bo_tt_destroy(bo);
	ttm_resource_free(bo, &bo->resource);
}

static int ttm_bo_individualize_resv(struct ttm_buffer_object *bo)
{
	int r;

	if (bo->base.resv == &bo->base._resv)
		return 0;

	BUG_ON(!dma_resv_trylock(&bo->base._resv));

	r = dma_resv_copy_fences(&bo->base._resv, bo->base.resv);
	dma_resv_unlock(&bo->base._resv);
	if (r)
		return r;

	if (bo->type != ttm_bo_type_sg) {
		/* This works because the BO is about to be destroyed and nobody
		 * reference it any more. The only tricky case is the trylock on
		 * the resv object while holding the lru_lock.
		 */
		spin_lock(&bo->bdev->lru_lock);
		bo->base.resv = &bo->base._resv;
		spin_unlock(&bo->bdev->lru_lock);
	}

	return r;
}

static void ttm_bo_flush_all_fences(struct ttm_buffer_object *bo)
{
	struct dma_resv *resv = &bo->base._resv;
	struct dma_resv_list *fobj;
	struct dma_fence *fence;
	int i;

	rcu_read_lock();
	fobj = dma_resv_shared_list(resv);
	fence = dma_resv_excl_fence(resv);
	if (fence && !fence->ops->signaled)
		dma_fence_enable_sw_signaling(fence);

	for (i = 0; fobj && i < fobj->shared_count; ++i) {
		fence = rcu_dereference(fobj->shared[i]);

		if (!fence->ops->signaled)
			dma_fence_enable_sw_signaling(fence);
	}
	rcu_read_unlock();
}

/**
 * ttm_bo_cleanup_refs
 * If bo idle, remove from lru lists, and unref.
 * If not idle, block if possible.
 *
 * Must be called with lru_lock and reservation held, this function
 * will drop the lru lock and optionally the reservation lock before returning.
 *
 * @bo:                    The buffer object to clean-up
 * @interruptible:         Any sleeps should occur interruptibly.
 * @no_wait_gpu:           Never wait for gpu. Return -EBUSY instead.
 * @unlock_resv:           Unlock the reservation lock as well.
 */

static int ttm_bo_cleanup_refs(struct ttm_buffer_object *bo,
			       bool interruptible, bool no_wait_gpu,
			       bool unlock_resv)
{
	struct dma_resv *resv = &bo->base._resv;
	int ret;

	if (dma_resv_test_signaled(resv, true))
		ret = 0;
	else
		ret = -EBUSY;

	if (ret && !no_wait_gpu) {
		long lret;

		if (unlock_resv)
			dma_resv_unlock(bo->base.resv);
		spin_unlock(&bo->bdev->lru_lock);

		lret = dma_resv_wait_timeout(resv, true, interruptible,
					     30 * HZ);

		if (lret < 0)
			return lret;
		else if (lret == 0)
			return -EBUSY;

		spin_lock(&bo->bdev->lru_lock);
		if (unlock_resv && !dma_resv_trylock(bo->base.resv)) {
			/*
			 * We raced, and lost, someone else holds the reservation now,
			 * and is probably busy in ttm_bo_cleanup_memtype_use.
			 *
			 * Even if it's not the case, because we finished waiting any
			 * delayed destruction would succeed, so just return success
			 * here.
			 */
			spin_unlock(&bo->bdev->lru_lock);
			return 0;
		}
		ret = 0;
	}

	if (ret || unlikely(list_empty(&bo->ddestroy))) {
		if (unlock_resv)
			dma_resv_unlock(bo->base.resv);
		spin_unlock(&bo->bdev->lru_lock);
		return ret;
	}

	ttm_bo_del_from_lru(bo);
	list_del_init(&bo->ddestroy);
	spin_unlock(&bo->bdev->lru_lock);
	ttm_bo_cleanup_memtype_use(bo);

	if (unlock_resv)
		dma_resv_unlock(bo->base.resv);

	ttm_bo_put(bo);

	return 0;
}

/*
 * Traverse the delayed list, and call ttm_bo_cleanup_refs on all
 * encountered buffers.
 */
bool ttm_bo_delayed_delete(struct ttm_device *bdev, bool remove_all)
{
	struct list_head removed;
	bool empty;

	INIT_LIST_HEAD(&removed);

	spin_lock(&bdev->lru_lock);
	while (!list_empty(&bdev->ddestroy)) {
		struct ttm_buffer_object *bo;

		bo = list_first_entry(&bdev->ddestroy, struct ttm_buffer_object,
				      ddestroy);
		list_move_tail(&bo->ddestroy, &removed);
		if (!ttm_bo_get_unless_zero(bo))
			continue;

		if (remove_all || bo->base.resv != &bo->base._resv) {
			spin_unlock(&bdev->lru_lock);
			dma_resv_lock(bo->base.resv, NULL);

			spin_lock(&bdev->lru_lock);
			ttm_bo_cleanup_refs(bo, false, !remove_all, true);

		} else if (dma_resv_trylock(bo->base.resv)) {
			ttm_bo_cleanup_refs(bo, false, !remove_all, true);
		} else {
			spin_unlock(&bdev->lru_lock);
		}

		ttm_bo_put(bo);
		spin_lock(&bdev->lru_lock);
	}
	list_splice_tail(&removed, &bdev->ddestroy);
	empty = list_empty(&bdev->ddestroy);
	spin_unlock(&bdev->lru_lock);

	return empty;
}

static void ttm_bo_release(struct kref *kref)
{
	struct ttm_buffer_object *bo =
	    container_of(kref, struct ttm_buffer_object, kref);
	struct ttm_device *bdev = bo->bdev;
	int ret;

	WARN_ON_ONCE(bo->pin_count);

	if (!bo->deleted) {
		ret = ttm_bo_individualize_resv(bo);
		if (ret) {
			/* Last resort, if we fail to allocate memory for the
			 * fences block for the BO to become idle
			 */
			dma_resv_wait_timeout(bo->base.resv, true, false,
					      30 * HZ);
		}

		if (bo->bdev->funcs->release_notify)
			bo->bdev->funcs->release_notify(bo);

		drm_vma_offset_remove(bdev->vma_manager, &bo->base.vma_node);
		ttm_mem_io_free(bdev, bo->resource);
	}

	if (!dma_resv_test_signaled(bo->base.resv, true) ||
	    !dma_resv_trylock(bo->base.resv)) {
		/* The BO is not idle, resurrect it for delayed destroy */
		ttm_bo_flush_all_fences(bo);
		bo->deleted = true;

		spin_lock(&bo->bdev->lru_lock);

		/*
		 * Make pinned bos immediately available to
		 * shrinkers, now that they are queued for
		 * destruction.
		 *
		 * FIXME: QXL is triggering this. Can be removed when the
		 * driver is fixed.
		 */
		if (bo->pin_count) {
			bo->pin_count = 0;
			ttm_bo_move_to_lru_tail(bo, bo->resource, NULL);
		}

		kref_init(&bo->kref);
		list_add_tail(&bo->ddestroy, &bdev->ddestroy);
		spin_unlock(&bo->bdev->lru_lock);

		schedule_delayed_work(&bdev->wq,
				      ((HZ / 100) < 1) ? 1 : HZ / 100);
		return;
	}

	spin_lock(&bo->bdev->lru_lock);
	ttm_bo_del_from_lru(bo);
	list_del(&bo->ddestroy);
	spin_unlock(&bo->bdev->lru_lock);

	ttm_bo_cleanup_memtype_use(bo);
	dma_resv_unlock(bo->base.resv);

	atomic_dec(&ttm_glob.bo_count);
	dma_fence_put(bo->moving);
	bo->destroy(bo);
}

void ttm_bo_put(struct ttm_buffer_object *bo)
{
	kref_put(&bo->kref, ttm_bo_release);
}
EXPORT_SYMBOL(ttm_bo_put);

int ttm_bo_lock_delayed_workqueue(struct ttm_device *bdev)
{
	return cancel_delayed_work_sync(&bdev->wq);
}
EXPORT_SYMBOL(ttm_bo_lock_delayed_workqueue);

void ttm_bo_unlock_delayed_workqueue(struct ttm_device *bdev, int resched)
{
	if (resched)
		schedule_delayed_work(&bdev->wq,
				      ((HZ / 100) < 1) ? 1 : HZ / 100);
}
EXPORT_SYMBOL(ttm_bo_unlock_delayed_workqueue);

static int ttm_bo_evict(struct ttm_buffer_object *bo,
			struct ttm_operation_ctx *ctx)
{
	struct ttm_device *bdev = bo->bdev;
	struct ttm_resource *evict_mem;
	struct ttm_placement placement;
	struct ttm_place hop;
	int ret = 0;

	memset(&hop, 0, sizeof(hop));

	dma_resv_assert_held(bo->base.resv);

	placement.num_placement = 0;
	placement.num_busy_placement = 0;
	bdev->funcs->evict_flags(bo, &placement);

	if (!placement.num_placement && !placement.num_busy_placement) {
		ret = ttm_bo_wait(bo, true, false);
		if (ret)
			return ret;

		/*
		 * Since we've already synced, this frees backing store
		 * immediately.
		 */
		return ttm_bo_pipeline_gutting(bo);
	}

	ret = ttm_bo_mem_space(bo, &placement, &evict_mem, ctx);
	if (ret) {
		if (ret != -ERESTARTSYS) {
			pr_err("Failed to find memory space for buffer 0x%p eviction\n",
			       bo);
			ttm_bo_mem_space_debug(bo, &placement);
		}
		goto out;
	}

	ret = ttm_bo_handle_move_mem(bo, evict_mem, true, ctx, &hop);
	if (unlikely(ret)) {
		WARN(ret == -EMULTIHOP, "Unexpected multihop in eviction - likely driver bug\n");
		if (ret != -ERESTARTSYS)
			pr_err("Buffer eviction failed\n");
		ttm_resource_free(bo, &evict_mem);
	}
out:
	return ret;
}

bool ttm_bo_eviction_valuable(struct ttm_buffer_object *bo,
			      const struct ttm_place *place)
{
	dma_resv_assert_held(bo->base.resv);
	if (bo->resource->mem_type == TTM_PL_SYSTEM)
		return true;

	/* Don't evict this BO if it's outside of the
	 * requested placement range
	 */
	if (place->fpfn >= (bo->resource->start + bo->resource->num_pages) ||
	    (place->lpfn && place->lpfn <= bo->resource->start))
		return false;

	return true;
}
EXPORT_SYMBOL(ttm_bo_eviction_valuable);

/*
 * Check the target bo is allowable to be evicted or swapout, including cases:
 *
 * a. if share same reservation object with ctx->resv, have assumption
 * reservation objects should already be locked, so not lock again and
 * return true directly when either the opreation allow_reserved_eviction
 * or the target bo already is in delayed free list;
 *
 * b. Otherwise, trylock it.
 */
static bool ttm_bo_evict_swapout_allowable(struct ttm_buffer_object *bo,
					   struct ttm_operation_ctx *ctx,
					   const struct ttm_place *place,
					   bool *locked, bool *busy)
{
	bool ret = false;

	if (bo->base.resv == ctx->resv) {
		dma_resv_assert_held(bo->base.resv);
		if (ctx->allow_res_evict)
			ret = true;
		*locked = false;
		if (busy)
			*busy = false;
	} else {
		ret = dma_resv_trylock(bo->base.resv);
		*locked = ret;
		if (busy)
			*busy = !ret;
	}

	if (ret && place && !bo->bdev->funcs->eviction_valuable(bo, place)) {
		ret = false;
		if (*locked) {
			dma_resv_unlock(bo->base.resv);
			*locked = false;
		}
	}

	return ret;
}

/**
 * ttm_mem_evict_wait_busy - wait for a busy BO to become available
 *
 * @busy_bo: BO which couldn't be locked with trylock
 * @ctx: operation context
 * @ticket: acquire ticket
 *
 * Try to lock a busy buffer object to avoid failing eviction.
 */
static int ttm_mem_evict_wait_busy(struct ttm_buffer_object *busy_bo,
				   struct ttm_operation_ctx *ctx,
				   struct ww_acquire_ctx *ticket)
{
	int r;

	if (!busy_bo || !ticket)
		return -EBUSY;

	if (ctx->interruptible)
		r = dma_resv_lock_interruptible(busy_bo->base.resv,
							  ticket);
	else
		r = dma_resv_lock(busy_bo->base.resv, ticket);

	/*
	 * TODO: It would be better to keep the BO locked until allocation is at
	 * least tried one more time, but that would mean a much larger rework
	 * of TTM.
	 */
	if (!r)
		dma_resv_unlock(busy_bo->base.resv);

	return r == -EDEADLK ? -EBUSY : r;
}

int ttm_mem_evict_first(struct ttm_device *bdev,
			struct ttm_resource_manager *man,
			const struct ttm_place *place,
			struct ttm_operation_ctx *ctx,
			struct ww_acquire_ctx *ticket)
{
	struct ttm_buffer_object *bo = NULL, *busy_bo = NULL;
	bool locked = false;
	unsigned i;
	int ret;

	spin_lock(&bdev->lru_lock);
	for (i = 0; i < TTM_MAX_BO_PRIORITY; ++i) {
		list_for_each_entry(bo, &man->lru[i], lru) {
			bool busy;

			if (!ttm_bo_evict_swapout_allowable(bo, ctx, place,
							    &locked, &busy)) {
				if (busy && !busy_bo && ticket !=
				    dma_resv_locking_ctx(bo->base.resv))
					busy_bo = bo;
				continue;
			}

			if (!ttm_bo_get_unless_zero(bo)) {
				if (locked)
					dma_resv_unlock(bo->base.resv);
				continue;
			}
			break;
		}

		/* If the inner loop terminated early, we have our candidate */
		if (&bo->lru != &man->lru[i])
			break;

		bo = NULL;
	}

	if (!bo) {
		if (busy_bo && !ttm_bo_get_unless_zero(busy_bo))
			busy_bo = NULL;
		spin_unlock(&bdev->lru_lock);
		ret = ttm_mem_evict_wait_busy(busy_bo, ctx, ticket);
		if (busy_bo)
			ttm_bo_put(busy_bo);
		return ret;
	}

	if (bo->deleted) {
		ret = ttm_bo_cleanup_refs(bo, ctx->interruptible,
					  ctx->no_wait_gpu, locked);
		ttm_bo_put(bo);
		return ret;
	}

	spin_unlock(&bdev->lru_lock);

	ret = ttm_bo_evict(bo, ctx);
	if (locked)
		ttm_bo_unreserve(bo);

	ttm_bo_put(bo);
	return ret;
}

/*
 * Add the last move fence to the BO and reserve a new shared slot. We only use
 * a shared slot to avoid unecessary sync and rely on the subsequent bo move to
 * either stall or use an exclusive fence respectively set bo->moving.
 */
static int ttm_bo_add_move_fence(struct ttm_buffer_object *bo,
				 struct ttm_resource_manager *man,
				 struct ttm_resource *mem,
				 bool no_wait_gpu)
{
	struct dma_fence *fence;
	int ret;

	spin_lock(&man->move_lock);
	fence = dma_fence_get(man->move);
	spin_unlock(&man->move_lock);

	if (!fence)
		return 0;

	if (no_wait_gpu) {
		ret = dma_fence_is_signaled(fence) ? 0 : -EBUSY;
		dma_fence_put(fence);
		return ret;
	}

	dma_resv_add_shared_fence(bo->base.resv, fence);

	ret = dma_resv_reserve_shared(bo->base.resv, 1);
	if (unlikely(ret)) {
		dma_fence_put(fence);
		return ret;
	}

	dma_fence_put(bo->moving);
	bo->moving = fence;
	return 0;
}

/*
 * Repeatedly evict memory from the LRU for @mem_type until we create enough
 * space, or we've evicted everything and there isn't enough space.
 */
static int ttm_bo_mem_force_space(struct ttm_buffer_object *bo,
				  const struct ttm_place *place,
				  struct ttm_resource **mem,
				  struct ttm_operation_ctx *ctx)
{
	struct ttm_device *bdev = bo->bdev;
	struct ttm_resource_manager *man;
	struct ww_acquire_ctx *ticket;
	int ret;

	man = ttm_manager_type(bdev, place->mem_type);
	ticket = dma_resv_locking_ctx(bo->base.resv);
	do {
		ret = ttm_resource_alloc(bo, place, mem);
		if (likely(!ret))
			break;
		if (unlikely(ret != -ENOSPC))
			return ret;
		ret = ttm_mem_evict_first(bdev, man, place, ctx,
					  ticket);
		if (unlikely(ret != 0))
			return ret;
	} while (1);

	return ttm_bo_add_move_fence(bo, man, *mem, ctx->no_wait_gpu);
}

/*
 * Creates space for memory region @mem according to its type.
 *
 * This function first searches for free space in compatible memory types in
 * the priority order defined by the driver.  If free space isn't found, then
 * ttm_bo_mem_force_space is attempted in priority order to evict and find
 * space.
 */
int ttm_bo_mem_space(struct ttm_buffer_object *bo,
			struct ttm_placement *placement,
			struct ttm_resource **mem,
			struct ttm_operation_ctx *ctx)
{
	struct ttm_device *bdev = bo->bdev;
	bool type_found = false;
	int i, ret;

	ret = dma_resv_reserve_shared(bo->base.resv, 1);
	if (unlikely(ret))
		return ret;

	for (i = 0; i < placement->num_placement; ++i) {
		const struct ttm_place *place = &placement->placement[i];
		struct ttm_resource_manager *man;

		man = ttm_manager_type(bdev, place->mem_type);
		if (!man || !ttm_resource_manager_used(man))
			continue;

		type_found = true;
		ret = ttm_resource_alloc(bo, place, mem);
		if (ret == -ENOSPC)
			continue;
		if (unlikely(ret))
			goto error;

		ret = ttm_bo_add_move_fence(bo, man, *mem, ctx->no_wait_gpu);
		if (unlikely(ret)) {
			ttm_resource_free(bo, mem);
			if (ret == -EBUSY)
				continue;

			goto error;
		}
		return 0;
	}

	for (i = 0; i < placement->num_busy_placement; ++i) {
		const struct ttm_place *place = &placement->busy_placement[i];
		struct ttm_resource_manager *man;

		man = ttm_manager_type(bdev, place->mem_type);
		if (!man || !ttm_resource_manager_used(man))
			continue;

		type_found = true;
		ret = ttm_bo_mem_force_space(bo, place, mem, ctx);
		if (likely(!ret))
			return 0;

		if (ret && ret != -EBUSY)
			goto error;
	}

	ret = -ENOMEM;
	if (!type_found) {
		pr_err(TTM_PFX "No compatible memory type found\n");
		ret = -EINVAL;
	}

error:
	if (bo->resource->mem_type == TTM_PL_SYSTEM && !bo->pin_count)
		ttm_bo_move_to_lru_tail_unlocked(bo);

	return ret;
}
EXPORT_SYMBOL(ttm_bo_mem_space);

static int ttm_bo_bounce_temp_buffer(struct ttm_buffer_object *bo,
				     struct ttm_resource **mem,
				     struct ttm_operation_ctx *ctx,
				     struct ttm_place *hop)
{
	struct ttm_placement hop_placement;
	struct ttm_resource *hop_mem;
	int ret;

	hop_placement.num_placement = hop_placement.num_busy_placement = 1;
	hop_placement.placement = hop_placement.busy_placement = hop;

	/* find space in the bounce domain */
	ret = ttm_bo_mem_space(bo, &hop_placement, &hop_mem, ctx);
	if (ret)
		return ret;
	/* move to the bounce domain */
	ret = ttm_bo_handle_move_mem(bo, hop_mem, false, ctx, NULL);
	if (ret) {
		ttm_resource_free(bo, &hop_mem);
		return ret;
	}
	return 0;
}

static int ttm_bo_move_buffer(struct ttm_buffer_object *bo,
			      struct ttm_placement *placement,
			      struct ttm_operation_ctx *ctx)
{
	struct ttm_resource *mem;
	struct ttm_place hop;
	int ret;

	dma_resv_assert_held(bo->base.resv);

	/*
	 * Determine where to move the buffer.
	 *
	 * If driver determines move is going to need
	 * an extra step then it will return -EMULTIHOP
	 * and the buffer will be moved to the temporary
	 * stop and the driver will be called to make
	 * the second hop.
	 */
	ret = ttm_bo_mem_space(bo, placement, &mem, ctx);
	if (ret)
		return ret;
bounce:
	ret = ttm_bo_handle_move_mem(bo, mem, false, ctx, &hop);
	if (ret == -EMULTIHOP) {
		ret = ttm_bo_bounce_temp_buffer(bo, &mem, ctx, &hop);
		if (ret)
			goto out;
		/* try and move to final place now. */
		goto bounce;
	}
out:
	if (ret)
		ttm_resource_free(bo, &mem);
	return ret;
}

static bool ttm_bo_places_compat(const struct ttm_place *places,
				 unsigned num_placement,
				 struct ttm_resource *mem,
				 uint32_t *new_flags)
{
	unsigned i;

	for (i = 0; i < num_placement; i++) {
		const struct ttm_place *heap = &places[i];

		if ((mem->start < heap->fpfn ||
		     (heap->lpfn != 0 && (mem->start + mem->num_pages) > heap->lpfn)))
			continue;

		*new_flags = heap->flags;
		if ((mem->mem_type == heap->mem_type) &&
		    (!(*new_flags & TTM_PL_FLAG_CONTIGUOUS) ||
		     (mem->placement & TTM_PL_FLAG_CONTIGUOUS)))
			return true;
	}
	return false;
}

bool ttm_bo_mem_compat(struct ttm_placement *placement,
		       struct ttm_resource *mem,
		       uint32_t *new_flags)
{
	if (ttm_bo_places_compat(placement->placement, placement->num_placement,
				 mem, new_flags))
		return true;

	if ((placement->busy_placement != placement->placement ||
	     placement->num_busy_placement > placement->num_placement) &&
	    ttm_bo_places_compat(placement->busy_placement,
				 placement->num_busy_placement,
				 mem, new_flags))
		return true;

	return false;
}
EXPORT_SYMBOL(ttm_bo_mem_compat);

int ttm_bo_validate(struct ttm_buffer_object *bo,
		    struct ttm_placement *placement,
		    struct ttm_operation_ctx *ctx)
{
	int ret;
	uint32_t new_flags;

	dma_resv_assert_held(bo->base.resv);

	/*
	 * Remove the backing store if no placement is given.
	 */
	if (!placement->num_placement && !placement->num_busy_placement)
		return ttm_bo_pipeline_gutting(bo);

	/*
	 * Check whether we need to move buffer.
	 */
	if (!ttm_bo_mem_compat(placement, bo->resource, &new_flags)) {
		ret = ttm_bo_move_buffer(bo, placement, ctx);
		if (ret)
			return ret;
	}
	/*
	 * We might need to add a TTM.
	 */
	if (bo->resource->mem_type == TTM_PL_SYSTEM) {
		ret = ttm_tt_create(bo, true);
		if (ret)
			return ret;
	}
	return 0;
}
EXPORT_SYMBOL(ttm_bo_validate);

int ttm_bo_init_reserved(struct ttm_device *bdev,
			 struct ttm_buffer_object *bo,
			 size_t size,
			 enum ttm_bo_type type,
			 struct ttm_placement *placement,
			 uint32_t page_alignment,
			 struct ttm_operation_ctx *ctx,
			 struct sg_table *sg,
			 struct dma_resv *resv,
			 void (*destroy) (struct ttm_buffer_object *))
{
	static const struct ttm_place sys_mem = { .mem_type = TTM_PL_SYSTEM };
	bool locked;
	int ret;

	bo->destroy = destroy ? destroy : ttm_bo_default_destroy;

	kref_init(&bo->kref);
	INIT_LIST_HEAD(&bo->lru);
	INIT_LIST_HEAD(&bo->ddestroy);
	bo->bdev = bdev;
	bo->type = type;
	bo->page_alignment = page_alignment;
	bo->moving = NULL;
	bo->pin_count = 0;
	bo->sg = sg;
	if (resv) {
		bo->base.resv = resv;
		dma_resv_assert_held(bo->base.resv);
	} else {
		bo->base.resv = &bo->base._resv;
	}
	atomic_inc(&ttm_glob.bo_count);

	ret = ttm_resource_alloc(bo, &sys_mem, &bo->resource);
	if (unlikely(ret)) {
		ttm_bo_put(bo);
		return ret;
	}

	/*
	 * For ttm_bo_type_device buffers, allocate
	 * address space from the device.
	 */
	if (bo->type == ttm_bo_type_device ||
	    bo->type == ttm_bo_type_sg)
		ret = drm_vma_offset_add(bdev->vma_manager, &bo->base.vma_node,
					 bo->resource->num_pages);

	/* passed reservation objects should already be locked,
	 * since otherwise lockdep will be angered in radeon.
	 */
	if (!resv) {
		locked = dma_resv_trylock(bo->base.resv);
		WARN_ON(!locked);
	}

	if (likely(!ret))
		ret = ttm_bo_validate(bo, placement, ctx);

	if (unlikely(ret)) {
		if (!resv)
			ttm_bo_unreserve(bo);

		ttm_bo_put(bo);
		return ret;
	}

	ttm_bo_move_to_lru_tail_unlocked(bo);

	return ret;
}
EXPORT_SYMBOL(ttm_bo_init_reserved);

int ttm_bo_init(struct ttm_device *bdev,
		struct ttm_buffer_object *bo,
		size_t size,
		enum ttm_bo_type type,
		struct ttm_placement *placement,
		uint32_t page_alignment,
		bool interruptible,
		struct sg_table *sg,
		struct dma_resv *resv,
		void (*destroy) (struct ttm_buffer_object *))
{
	struct ttm_operation_ctx ctx = { interruptible, false };
	int ret;

	ret = ttm_bo_init_reserved(bdev, bo, size, type, placement,
				   page_alignment, &ctx, sg, resv, destroy);
	if (ret)
		return ret;

	if (!resv)
		ttm_bo_unreserve(bo);

	return 0;
}
EXPORT_SYMBOL(ttm_bo_init);

/*
 * buffer object vm functions.
 */

void ttm_bo_unmap_virtual(struct ttm_buffer_object *bo)
{
	struct ttm_device *bdev = bo->bdev;

	drm_vma_node_unmap(&bo->base.vma_node, bdev->dev_mapping);
	ttm_mem_io_free(bdev, bo->resource);
}
EXPORT_SYMBOL(ttm_bo_unmap_virtual);

int ttm_bo_wait(struct ttm_buffer_object *bo,
		bool interruptible, bool no_wait)
{
	long timeout = 15 * HZ;

	if (no_wait) {
		if (dma_resv_test_signaled(bo->base.resv, true))
			return 0;
		else
			return -EBUSY;
	}

	timeout = dma_resv_wait_timeout(bo->base.resv, true, interruptible,
					timeout);
	if (timeout < 0)
		return timeout;

	if (timeout == 0)
		return -EBUSY;

	dma_resv_add_excl_fence(bo->base.resv, NULL);
	return 0;
}
EXPORT_SYMBOL(ttm_bo_wait);

int ttm_bo_swapout(struct ttm_buffer_object *bo, struct ttm_operation_ctx *ctx,
		   gfp_t gfp_flags)
{
	struct ttm_place place;
	bool locked;
	int ret;

	/*
	 * While the bo may already reside in SYSTEM placement, set
	 * SYSTEM as new placement to cover also the move further below.
	 * The driver may use the fact that we're moving from SYSTEM
	 * as an indication that we're about to swap out.
	 */
	memset(&place, 0, sizeof(place));
	place.mem_type = TTM_PL_SYSTEM;
	if (!ttm_bo_evict_swapout_allowable(bo, ctx, &place, &locked, NULL))
		return -EBUSY;

	if (!bo->ttm || !ttm_tt_is_populated(bo->ttm) ||
	    bo->ttm->page_flags & TTM_PAGE_FLAG_SG ||
	    bo->ttm->page_flags & TTM_PAGE_FLAG_SWAPPED ||
	    !ttm_bo_get_unless_zero(bo)) {
		if (locked)
			dma_resv_unlock(bo->base.resv);
		return -EBUSY;
	}

	if (bo->deleted) {
		ttm_bo_cleanup_refs(bo, false, false, locked);
		ttm_bo_put(bo);
		return 0;
	}

	ttm_bo_del_from_lru(bo);
	/* TODO: Cleanup the locking */
	spin_unlock(&bo->bdev->lru_lock);

	/*
	 * Move to system cached
	 */
	if (bo->resource->mem_type != TTM_PL_SYSTEM) {
		struct ttm_operation_ctx ctx = { false, false };
		struct ttm_resource *evict_mem;
		struct ttm_place hop;

		memset(&hop, 0, sizeof(hop));
		ret = ttm_resource_alloc(bo, &place, &evict_mem);
		if (unlikely(ret))
			goto out;

		ret = ttm_bo_handle_move_mem(bo, evict_mem, true, &ctx, &hop);
		if (unlikely(ret != 0)) {
			WARN(ret == -EMULTIHOP, "Unexpected multihop in swaput - likely driver bug.\n");
			goto out;
		}
	}

	/*
	 * Make sure BO is idle.
	 */
	ret = ttm_bo_wait(bo, false, false);
	if (unlikely(ret != 0))
		goto out;

	ttm_bo_unmap_virtual(bo);

	/*
	 * Swap out. Buffer will be swapped in again as soon as
	 * anyone tries to access a ttm page.
	 */
	if (bo->bdev->funcs->swap_notify)
		bo->bdev->funcs->swap_notify(bo);

	if (ttm_tt_is_populated(bo->ttm))
		ret = ttm_tt_swapout(bo->bdev, bo->ttm, gfp_flags);
out:

	/*
	 * Unreserve without putting on LRU to avoid swapping out an
	 * already swapped buffer.
	 */
	if (locked)
		dma_resv_unlock(bo->base.resv);
	ttm_bo_put(bo);
	return ret;
}

void ttm_bo_tt_destroy(struct ttm_buffer_object *bo)
{
	if (bo->ttm == NULL)
		return;

	ttm_tt_destroy(bo->bdev, bo->ttm);
	bo->ttm = NULL;
}
