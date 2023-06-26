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

#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_tt.h>

#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/atomic.h>
#include <linux/dma-resv.h>

#include "ttm_module.h"

static void ttm_bo_mem_space_debug(struct ttm_buffer_object *bo,
					struct ttm_placement *placement)
{
	struct drm_printer p = drm_debug_printer(TTM_PFX);
	struct ttm_resource_manager *man;
	int i, mem_type;

	for (i = 0; i < placement->num_placement; i++) {
		mem_type = placement->placement[i].mem_type;
		drm_printf(&p, "  placement[%d]=0x%08X (%d)\n",
			   i, placement->placement[i].flags, mem_type);
		man = ttm_manager_type(bo->bdev, mem_type);
		ttm_resource_manager_debug(man, &p);
	}
}

/**
 * ttm_bo_move_to_lru_tail
 *
 * @bo: The buffer object.
 *
 * Move this BO to the tail of all lru lists used to lookup and reserve an
 * object. This function must be called with struct ttm_global::lru_lock
 * held, and is used to make a BO less likely to be considered for eviction.
 */
void ttm_bo_move_to_lru_tail(struct ttm_buffer_object *bo)
{
	dma_resv_assert_held(bo->base.resv);

	if (bo->resource)
		ttm_resource_move_to_lru_tail(bo->resource);
}
EXPORT_SYMBOL(ttm_bo_move_to_lru_tail);

/**
 * ttm_bo_set_bulk_move - update BOs bulk move object
 *
 * @bo: The buffer object.
 * @bulk: bulk move structure
 *
 * Update the BOs bulk move object, making sure that resources are added/removed
 * as well. A bulk move allows to move many resource on the LRU at once,
 * resulting in much less overhead of maintaining the LRU.
 * The only requirement is that the resources stay together on the LRU and are
 * never separated. This is enforces by setting the bulk_move structure on a BO.
 * ttm_lru_bulk_move_tail() should be used to move all resources to the tail of
 * their LRU list.
 */
void ttm_bo_set_bulk_move(struct ttm_buffer_object *bo,
			  struct ttm_lru_bulk_move *bulk)
{
	dma_resv_assert_held(bo->base.resv);

	if (bo->bulk_move == bulk)
		return;

	spin_lock(&bo->bdev->lru_lock);
	if (bo->resource)
		ttm_resource_del_bulk_move(bo->resource, bo);
	bo->bulk_move = bulk;
	if (bo->resource)
		ttm_resource_add_bulk_move(bo->resource, bo);
	spin_unlock(&bo->bdev->lru_lock);
}
EXPORT_SYMBOL(ttm_bo_set_bulk_move);

static int ttm_bo_handle_move_mem(struct ttm_buffer_object *bo,
				  struct ttm_resource *mem, bool evict,
				  struct ttm_operation_ctx *ctx,
				  struct ttm_place *hop)
{
	struct ttm_device *bdev = bo->bdev;
	bool old_use_tt, new_use_tt;
	int ret;

	old_use_tt = !bo->resource || ttm_manager_type(bdev, bo->resource->mem_type)->use_tt;
	new_use_tt = ttm_manager_type(bdev, mem->mem_type)->use_tt;

	ttm_bo_unmap_virtual(bo);

	/*
	 * Create and bind a ttm if required.
	 */

	if (new_use_tt) {
		/* Zero init the new TTM structure if the old location should
		 * have used one as well.
		 */
		ret = ttm_tt_create(bo, old_use_tt);
		if (ret)
			goto out_err;

		if (mem->mem_type != TTM_PL_SYSTEM) {
			ret = ttm_tt_populate(bo->bdev, bo->ttm, ctx);
			if (ret)
				goto out_err;
		}
	}

	ret = dma_resv_reserve_fences(bo->base.resv, 1);
	if (ret)
		goto out_err;

	ret = bdev->funcs->move(bo, evict, ctx, mem, hop);
	if (ret) {
		if (ret == -EMULTIHOP)
			return ret;
		goto out_err;
	}

	ctx->bytes_moved += bo->base.size;
	return 0;

out_err:
	if (!old_use_tt)
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
	struct dma_resv_iter cursor;
	struct dma_fence *fence;

	dma_resv_iter_begin(&cursor, resv, DMA_RESV_USAGE_BOOKKEEP);
	dma_resv_for_each_fence_unlocked(&cursor, fence) {
		if (!fence->ops->signaled)
			dma_fence_enable_sw_signaling(fence);
	}
	dma_resv_iter_end(&cursor);
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

	if (dma_resv_test_signaled(resv, DMA_RESV_USAGE_BOOKKEEP))
		ret = 0;
	else
		ret = -EBUSY;

	if (ret && !no_wait_gpu) {
		long lret;

		if (unlock_resv)
			dma_resv_unlock(bo->base.resv);
		spin_unlock(&bo->bdev->lru_lock);

		lret = dma_resv_wait_timeout(resv, DMA_RESV_USAGE_BOOKKEEP,
					     interruptible,
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

	if (ret) {
		if (unlock_resv)
			dma_resv_unlock(bo->base.resv);
		spin_unlock(&bo->bdev->lru_lock);
		return ret;
	}

	spin_unlock(&bo->bdev->lru_lock);
	ttm_bo_cleanup_memtype_use(bo);

	if (unlock_resv)
		dma_resv_unlock(bo->base.resv);

	return 0;
}

/*
 * Block for the dma_resv object to become idle, lock the buffer and clean up
 * the resource and tt object.
 */
static void ttm_bo_delayed_delete(struct work_struct *work)
{
	struct ttm_buffer_object *bo;

	bo = container_of(work, typeof(*bo), delayed_delete);

	dma_resv_wait_timeout(bo->base.resv, DMA_RESV_USAGE_BOOKKEEP, false,
			      MAX_SCHEDULE_TIMEOUT);
	dma_resv_lock(bo->base.resv, NULL);
	ttm_bo_cleanup_memtype_use(bo);
	dma_resv_unlock(bo->base.resv);
	ttm_bo_put(bo);
}

static void ttm_bo_release(struct kref *kref)
{
	struct ttm_buffer_object *bo =
	    container_of(kref, struct ttm_buffer_object, kref);
	struct ttm_device *bdev = bo->bdev;
	int ret;

	WARN_ON_ONCE(bo->pin_count);
	WARN_ON_ONCE(bo->bulk_move);

	if (!bo->deleted) {
		ret = ttm_bo_individualize_resv(bo);
		if (ret) {
			/* Last resort, if we fail to allocate memory for the
			 * fences block for the BO to become idle
			 */
			dma_resv_wait_timeout(bo->base.resv,
					      DMA_RESV_USAGE_BOOKKEEP, false,
					      30 * HZ);
		}

		if (bo->bdev->funcs->release_notify)
			bo->bdev->funcs->release_notify(bo);

		drm_vma_offset_remove(bdev->vma_manager, &bo->base.vma_node);
		ttm_mem_io_free(bdev, bo->resource);

		if (!dma_resv_test_signaled(bo->base.resv,
					    DMA_RESV_USAGE_BOOKKEEP) ||
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
				ttm_resource_move_to_lru_tail(bo->resource);
			}

			kref_init(&bo->kref);
			spin_unlock(&bo->bdev->lru_lock);

			INIT_WORK(&bo->delayed_delete, ttm_bo_delayed_delete);
			queue_work(bdev->wq, &bo->delayed_delete);
			return;
		}

		ttm_bo_cleanup_memtype_use(bo);
		dma_resv_unlock(bo->base.resv);
	}

	atomic_dec(&ttm_glob.bo_count);
	bo->destroy(bo);
}

/**
 * ttm_bo_put
 *
 * @bo: The buffer object.
 *
 * Unreference a buffer object.
 */
void ttm_bo_put(struct ttm_buffer_object *bo)
{
	kref_put(&bo->kref, ttm_bo_release);
}
EXPORT_SYMBOL(ttm_bo_put);

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
		ret = ttm_bo_wait_ctx(bo, ctx);
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

bounce:
	ret = ttm_bo_handle_move_mem(bo, evict_mem, true, ctx, &hop);
	if (ret == -EMULTIHOP) {
		ret = ttm_bo_bounce_temp_buffer(bo, &evict_mem, ctx, &hop);
		if (ret) {
			if (ret != -ERESTARTSYS && ret != -EINTR)
				pr_err("Buffer eviction failed\n");
			ttm_resource_free(bo, &evict_mem);
			goto out;
		}
		/* try and move to final place now. */
		goto bounce;
	}
out:
	return ret;
}

/**
 * ttm_bo_eviction_valuable
 *
 * @bo: The buffer object to evict
 * @place: the placement we need to make room for
 *
 * Check if it is valuable to evict the BO to make room for the given placement.
 */
bool ttm_bo_eviction_valuable(struct ttm_buffer_object *bo,
			      const struct ttm_place *place)
{
	struct ttm_resource *res = bo->resource;
	struct ttm_device *bdev = bo->bdev;

	dma_resv_assert_held(bo->base.resv);
	if (bo->resource->mem_type == TTM_PL_SYSTEM)
		return true;

	/* Don't evict this BO if it's outside of the
	 * requested placement range
	 */
	return ttm_resource_intersects(bdev, res, place, bo->base.size);
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

	if (ret && place && (bo->resource->mem_type != place->mem_type ||
		!bo->bdev->funcs->eviction_valuable(bo, place))) {
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
	struct ttm_resource_cursor cursor;
	struct ttm_resource *res;
	bool locked = false;
	int ret;

	spin_lock(&bdev->lru_lock);
	ttm_resource_manager_for_each_res(man, &cursor, res) {
		bool busy;

		if (!ttm_bo_evict_swapout_allowable(res->bo, ctx, place,
						    &locked, &busy)) {
			if (busy && !busy_bo && ticket !=
			    dma_resv_locking_ctx(res->bo->base.resv))
				busy_bo = res->bo;
			continue;
		}

		if (ttm_bo_get_unless_zero(res->bo)) {
			bo = res->bo;
			break;
		}
		if (locked)
			dma_resv_unlock(res->bo->base.resv);
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
	else
		ttm_bo_move_to_lru_tail_unlocked(bo);

	ttm_bo_put(bo);
	return ret;
}

/**
 * ttm_bo_pin - Pin the buffer object.
 * @bo: The buffer object to pin
 *
 * Make sure the buffer is not evicted any more during memory pressure.
 * @bo must be unpinned again by calling ttm_bo_unpin().
 */
void ttm_bo_pin(struct ttm_buffer_object *bo)
{
	dma_resv_assert_held(bo->base.resv);
	WARN_ON_ONCE(!kref_read(&bo->kref));
	spin_lock(&bo->bdev->lru_lock);
	if (bo->resource)
		ttm_resource_del_bulk_move(bo->resource, bo);
	++bo->pin_count;
	spin_unlock(&bo->bdev->lru_lock);
}
EXPORT_SYMBOL(ttm_bo_pin);

/**
 * ttm_bo_unpin - Unpin the buffer object.
 * @bo: The buffer object to unpin
 *
 * Allows the buffer object to be evicted again during memory pressure.
 */
void ttm_bo_unpin(struct ttm_buffer_object *bo)
{
	dma_resv_assert_held(bo->base.resv);
	WARN_ON_ONCE(!kref_read(&bo->kref));
	if (WARN_ON_ONCE(!bo->pin_count))
		return;

	spin_lock(&bo->bdev->lru_lock);
	--bo->pin_count;
	if (bo->resource)
		ttm_resource_add_bulk_move(bo->resource, bo);
	spin_unlock(&bo->bdev->lru_lock);
}
EXPORT_SYMBOL(ttm_bo_unpin);

/*
 * Add the last move fence to the BO as kernel dependency and reserve a new
 * fence slot.
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

	dma_resv_add_fence(bo->base.resv, fence, DMA_RESV_USAGE_KERNEL);

	ret = dma_resv_reserve_fences(bo->base.resv, 1);
	dma_fence_put(fence);
	return ret;
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

/**
 * ttm_bo_mem_space
 *
 * @bo: Pointer to a struct ttm_buffer_object. the data of which
 * we want to allocate space for.
 * @placement: Proposed new placement for the buffer object.
 * @mem: A struct ttm_resource.
 * @ctx: if and how to sleep, lock buffers and alloc memory
 *
 * Allocate memory space for the buffer object pointed to by @bo, using
 * the placement flags in @placement, potentially evicting other idle buffer objects.
 * This function may sleep while waiting for space to become available.
 * Returns:
 * -EBUSY: No space available (only if no_wait == 1).
 * -ENOMEM: Could not allocate memory for the buffer object, either due to
 * fragmentation or concurrent allocators.
 * -ERESTARTSYS: An interruptible sleep was interrupted by a signal.
 */
int ttm_bo_mem_space(struct ttm_buffer_object *bo,
			struct ttm_placement *placement,
			struct ttm_resource **mem,
			struct ttm_operation_ctx *ctx)
{
	struct ttm_device *bdev = bo->bdev;
	bool type_found = false;
	int i, ret;

	ret = dma_resv_reserve_fences(bo->base.resv, 1);
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
	return ret;
}
EXPORT_SYMBOL(ttm_bo_mem_space);

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

/**
 * ttm_bo_validate
 *
 * @bo: The buffer object.
 * @placement: Proposed placement for the buffer object.
 * @ctx: validation parameters.
 *
 * Changes placement and caching policy of the buffer object
 * according proposed placement.
 * Returns
 * -EINVAL on invalid proposed placement.
 * -ENOMEM on out-of-memory condition.
 * -EBUSY if no_wait is true and buffer busy.
 * -ERESTARTSYS if interrupted by a signal.
 */
int ttm_bo_validate(struct ttm_buffer_object *bo,
		    struct ttm_placement *placement,
		    struct ttm_operation_ctx *ctx)
{
	int ret;

	dma_resv_assert_held(bo->base.resv);

	/*
	 * Remove the backing store if no placement is given.
	 */
	if (!placement->num_placement && !placement->num_busy_placement)
		return ttm_bo_pipeline_gutting(bo);

	/* Check whether we need to move buffer. */
	if (bo->resource && ttm_resource_compat(bo->resource, placement))
		return 0;

	/* Moving of pinned BOs is forbidden */
	if (bo->pin_count)
		return -EINVAL;

	ret = ttm_bo_move_buffer(bo, placement, ctx);
	if (ret)
		return ret;

	/*
	 * We might need to add a TTM.
	 */
	if (!bo->resource || bo->resource->mem_type == TTM_PL_SYSTEM) {
		ret = ttm_tt_create(bo, true);
		if (ret)
			return ret;
	}
	return 0;
}
EXPORT_SYMBOL(ttm_bo_validate);

/**
 * ttm_bo_init_reserved
 *
 * @bdev: Pointer to a ttm_device struct.
 * @bo: Pointer to a ttm_buffer_object to be initialized.
 * @type: Requested type of buffer object.
 * @placement: Initial placement for buffer object.
 * @alignment: Data alignment in pages.
 * @ctx: TTM operation context for memory allocation.
 * @sg: Scatter-gather table.
 * @resv: Pointer to a dma_resv, or NULL to let ttm allocate one.
 * @destroy: Destroy function. Use NULL for kfree().
 *
 * This function initializes a pre-allocated struct ttm_buffer_object.
 * As this object may be part of a larger structure, this function,
 * together with the @destroy function, enables driver-specific objects
 * derived from a ttm_buffer_object.
 *
 * On successful return, the caller owns an object kref to @bo. The kref and
 * list_kref are usually set to 1, but note that in some situations, other
 * tasks may already be holding references to @bo as well.
 * Furthermore, if resv == NULL, the buffer's reservation lock will be held,
 * and it is the caller's responsibility to call ttm_bo_unreserve.
 *
 * If a failure occurs, the function will call the @destroy function. Thus,
 * after a failure, dereferencing @bo is illegal and will likely cause memory
 * corruption.
 *
 * Returns
 * -ENOMEM: Out of memory.
 * -EINVAL: Invalid placement flags.
 * -ERESTARTSYS: Interrupted by signal while sleeping waiting for resources.
 */
int ttm_bo_init_reserved(struct ttm_device *bdev, struct ttm_buffer_object *bo,
			 enum ttm_bo_type type, struct ttm_placement *placement,
			 uint32_t alignment, struct ttm_operation_ctx *ctx,
			 struct sg_table *sg, struct dma_resv *resv,
			 void (*destroy) (struct ttm_buffer_object *))
{
	int ret;

	kref_init(&bo->kref);
	bo->bdev = bdev;
	bo->type = type;
	bo->page_alignment = alignment;
	bo->destroy = destroy;
	bo->pin_count = 0;
	bo->sg = sg;
	bo->bulk_move = NULL;
	if (resv)
		bo->base.resv = resv;
	else
		bo->base.resv = &bo->base._resv;
	atomic_inc(&ttm_glob.bo_count);

	/*
	 * For ttm_bo_type_device buffers, allocate
	 * address space from the device.
	 */
	if (bo->type == ttm_bo_type_device || bo->type == ttm_bo_type_sg) {
		ret = drm_vma_offset_add(bdev->vma_manager, &bo->base.vma_node,
					 PFN_UP(bo->base.size));
		if (ret)
			goto err_put;
	}

	/* passed reservation objects should already be locked,
	 * since otherwise lockdep will be angered in radeon.
	 */
	if (!resv)
		WARN_ON(!dma_resv_trylock(bo->base.resv));
	else
		dma_resv_assert_held(resv);

	ret = ttm_bo_validate(bo, placement, ctx);
	if (unlikely(ret))
		goto err_unlock;

	return 0;

err_unlock:
	if (!resv)
		dma_resv_unlock(bo->base.resv);

err_put:
	ttm_bo_put(bo);
	return ret;
}
EXPORT_SYMBOL(ttm_bo_init_reserved);

/**
 * ttm_bo_init_validate
 *
 * @bdev: Pointer to a ttm_device struct.
 * @bo: Pointer to a ttm_buffer_object to be initialized.
 * @type: Requested type of buffer object.
 * @placement: Initial placement for buffer object.
 * @alignment: Data alignment in pages.
 * @interruptible: If needing to sleep to wait for GPU resources,
 * sleep interruptible.
 * pinned in physical memory. If this behaviour is not desired, this member
 * holds a pointer to a persistent shmem object. Typically, this would
 * point to the shmem object backing a GEM object if TTM is used to back a
 * GEM user interface.
 * @sg: Scatter-gather table.
 * @resv: Pointer to a dma_resv, or NULL to let ttm allocate one.
 * @destroy: Destroy function. Use NULL for kfree().
 *
 * This function initializes a pre-allocated struct ttm_buffer_object.
 * As this object may be part of a larger structure, this function,
 * together with the @destroy function,
 * enables driver-specific objects derived from a ttm_buffer_object.
 *
 * On successful return, the caller owns an object kref to @bo. The kref and
 * list_kref are usually set to 1, but note that in some situations, other
 * tasks may already be holding references to @bo as well.
 *
 * If a failure occurs, the function will call the @destroy function, Thus,
 * after a failure, dereferencing @bo is illegal and will likely cause memory
 * corruption.
 *
 * Returns
 * -ENOMEM: Out of memory.
 * -EINVAL: Invalid placement flags.
 * -ERESTARTSYS: Interrupted by signal while sleeping waiting for resources.
 */
int ttm_bo_init_validate(struct ttm_device *bdev, struct ttm_buffer_object *bo,
			 enum ttm_bo_type type, struct ttm_placement *placement,
			 uint32_t alignment, bool interruptible,
			 struct sg_table *sg, struct dma_resv *resv,
			 void (*destroy) (struct ttm_buffer_object *))
{
	struct ttm_operation_ctx ctx = { interruptible, false };
	int ret;

	ret = ttm_bo_init_reserved(bdev, bo, type, placement, alignment, &ctx,
				   sg, resv, destroy);
	if (ret)
		return ret;

	if (!resv)
		ttm_bo_unreserve(bo);

	return 0;
}
EXPORT_SYMBOL(ttm_bo_init_validate);

/*
 * buffer object vm functions.
 */

/**
 * ttm_bo_unmap_virtual
 *
 * @bo: tear down the virtual mappings for this BO
 */
void ttm_bo_unmap_virtual(struct ttm_buffer_object *bo)
{
	struct ttm_device *bdev = bo->bdev;

	drm_vma_node_unmap(&bo->base.vma_node, bdev->dev_mapping);
	ttm_mem_io_free(bdev, bo->resource);
}
EXPORT_SYMBOL(ttm_bo_unmap_virtual);

/**
 * ttm_bo_wait_ctx - wait for buffer idle.
 *
 * @bo:  The buffer object.
 * @ctx: defines how to wait
 *
 * Waits for the buffer to be idle. Used timeout depends on the context.
 * Returns -EBUSY if wait timed outt, -ERESTARTSYS if interrupted by a signal or
 * zero on success.
 */
int ttm_bo_wait_ctx(struct ttm_buffer_object *bo, struct ttm_operation_ctx *ctx)
{
	long ret;

	if (ctx->no_wait_gpu) {
		if (dma_resv_test_signaled(bo->base.resv,
					   DMA_RESV_USAGE_BOOKKEEP))
			return 0;
		else
			return -EBUSY;
	}

	ret = dma_resv_wait_timeout(bo->base.resv, DMA_RESV_USAGE_BOOKKEEP,
				    ctx->interruptible, 15 * HZ);
	if (unlikely(ret < 0))
		return ret;
	if (unlikely(ret == 0))
		return -EBUSY;
	return 0;
}
EXPORT_SYMBOL(ttm_bo_wait_ctx);

int ttm_bo_swapout(struct ttm_buffer_object *bo, struct ttm_operation_ctx *ctx,
		   gfp_t gfp_flags)
{
	struct ttm_place place;
	bool locked;
	long ret;

	/*
	 * While the bo may already reside in SYSTEM placement, set
	 * SYSTEM as new placement to cover also the move further below.
	 * The driver may use the fact that we're moving from SYSTEM
	 * as an indication that we're about to swap out.
	 */
	memset(&place, 0, sizeof(place));
	place.mem_type = bo->resource->mem_type;
	if (!ttm_bo_evict_swapout_allowable(bo, ctx, &place, &locked, NULL))
		return -EBUSY;

	if (!bo->ttm || !ttm_tt_is_populated(bo->ttm) ||
	    bo->ttm->page_flags & TTM_TT_FLAG_EXTERNAL ||
	    bo->ttm->page_flags & TTM_TT_FLAG_SWAPPED ||
	    !ttm_bo_get_unless_zero(bo)) {
		if (locked)
			dma_resv_unlock(bo->base.resv);
		return -EBUSY;
	}

	if (bo->deleted) {
		ret = ttm_bo_cleanup_refs(bo, false, false, locked);
		ttm_bo_put(bo);
		return ret == -EBUSY ? -ENOSPC : ret;
	}

	/* TODO: Cleanup the locking */
	spin_unlock(&bo->bdev->lru_lock);

	/*
	 * Move to system cached
	 */
	if (bo->resource->mem_type != TTM_PL_SYSTEM) {
		struct ttm_resource *evict_mem;
		struct ttm_place hop;

		memset(&hop, 0, sizeof(hop));
		place.mem_type = TTM_PL_SYSTEM;
		ret = ttm_resource_alloc(bo, &place, &evict_mem);
		if (unlikely(ret))
			goto out;

		ret = ttm_bo_handle_move_mem(bo, evict_mem, true, ctx, &hop);
		if (unlikely(ret != 0)) {
			WARN(ret == -EMULTIHOP, "Unexpected multihop in swaput - likely driver bug.\n");
			goto out;
		}
	}

	/*
	 * Make sure BO is idle.
	 */
	ret = ttm_bo_wait_ctx(bo, ctx);
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
	return ret == -EBUSY ? -ENOSPC : ret;
}

void ttm_bo_tt_destroy(struct ttm_buffer_object *bo)
{
	if (bo->ttm == NULL)
		return;

	ttm_tt_unpopulate(bo->bdev, bo->ttm);
	ttm_tt_destroy(bo->bdev, bo->ttm);
	bo->ttm = NULL;
}
