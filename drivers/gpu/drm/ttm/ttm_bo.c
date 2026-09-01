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

#include <drm/drm_print.h>
#include <drm/drm_util.h>
#include <drm/ttm/ttm_allocation.h>
#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_tt.h>

#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/atomic.h>
#include <linux/cgroup_dmem.h>
#include <linux/dma-resv.h>

#include "ttm_module.h"
#include "ttm_bo_internal.h"

static void ttm_bo_mem_space_debug(struct ttm_buffer_object *bo,
					struct ttm_placement *placement)
{
	struct drm_printer p = drm_dbg_printer(NULL, DRM_UT_CORE, TTM_PFX);
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
			ret = ttm_bo_populate(bo, ctx);
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
	dma_resv_for_each_fence_unlocked(&cursor, fence)
		dma_fence_enable_signaling(fence);
	dma_resv_iter_end(&cursor);
}

/*
 * Block for the dma_resv object to become idle, lock the buffer and clean up
 * the resource and tt object.
 */
static void ttm_bo_delayed_delete(struct work_struct *work)
{
	struct ttm_buffer_object *bo;

	bo = container_of(work, typeof(*bo), delayed_delete);

	dma_resv_wait_timeout(&bo->base._resv, DMA_RESV_USAGE_BOOKKEEP, false,
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

		if (bdev->funcs->release_notify)
			bdev->funcs->release_notify(bo);

		drm_vma_offset_remove(bdev->vma_manager, &bo->base.vma_node);
		ttm_mem_io_free(bdev, bo->resource);

		if (!dma_resv_test_signaled(&bo->base._resv,
					    DMA_RESV_USAGE_BOOKKEEP) ||
		    (want_init_on_free() && (bo->ttm != NULL)) ||
		    bo->type == ttm_bo_type_sg ||
		    !dma_resv_trylock(bo->base.resv)) {
			/* The BO is not idle, resurrect it for delayed destroy */
			ttm_bo_flush_all_fences(bo);
			bo->deleted = true;

			spin_lock(&bdev->lru_lock);

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
			spin_unlock(&bdev->lru_lock);

			INIT_WORK(&bo->delayed_delete, ttm_bo_delayed_delete);

			/* Schedule the worker on the closest NUMA node. This
			 * improves performance since system memory might be
			 * cleared on free and that is best done on a CPU core
			 * close to it.
			 */
			queue_work_node(bdev->pool.nid, bdev->wq, &bo->delayed_delete);
			return;
		}

		ttm_bo_cleanup_memtype_use(bo);
		dma_resv_unlock(bo->base.resv);
	}

	atomic_dec(&ttm_glob.bo_count);
	bo->destroy(bo);
}

/* TODO: remove! */
void ttm_bo_put(struct ttm_buffer_object *bo)
{
	kref_put(&bo->kref, ttm_bo_release);
}

void ttm_bo_fini(struct ttm_buffer_object *bo)
{
	ttm_bo_put(bo);
}
EXPORT_SYMBOL(ttm_bo_fini);

static int ttm_bo_bounce_temp_buffer(struct ttm_buffer_object *bo,
				     struct ttm_operation_ctx *ctx,
				     struct ttm_place *hop)
{
	struct ttm_placement hop_placement;
	struct ttm_resource *hop_mem;
	int ret;

	hop_placement.num_placement = 1;
	hop_placement.placement = hop;

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
	struct ttm_resource *evict_mem;
	struct ttm_placement placement;
	struct ttm_place hop;
	int ret = 0;

	memset(&hop, 0, sizeof(hop));

	dma_resv_assert_held(bo->base.resv);

	placement.num_placement = 0;
	bo->bdev->funcs->evict_flags(bo, &placement);

	if (!placement.num_placement) {
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

	do {
		ret = ttm_bo_handle_move_mem(bo, evict_mem, true, ctx, &hop);
		if (ret != -EMULTIHOP)
			break;

		ret = ttm_bo_bounce_temp_buffer(bo, ctx, &hop);
	} while (!ret);

	if (ret) {
		ttm_resource_free(bo, &evict_mem);
		if (ret != -ERESTARTSYS && ret != -EINTR)
			pr_err("Buffer eviction failed\n");
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

	dma_resv_assert_held(bo->base.resv);

	if (res->mem_type == TTM_PL_SYSTEM)
		return true;

	/* Don't evict this BO if it's outside of the
	 * requested placement range
	 */
	return ttm_resource_intersects(bo->bdev, res, place, bo->base.size);
}
EXPORT_SYMBOL(ttm_bo_eviction_valuable);

/**
 * ttm_bo_evict_first() - Evict the first bo on the manager's LRU list.
 * @bdev: The ttm device.
 * @man: The manager whose bo to evict.
 * @ctx: The TTM operation ctx governing the eviction.
 *
 * Return: 0 if successful or the resource disappeared. Negative error code on error.
 */
int ttm_bo_evict_first(struct ttm_device *bdev, struct ttm_resource_manager *man,
		       struct ttm_operation_ctx *ctx)
{
	struct ttm_resource_cursor cursor;
	struct ttm_buffer_object *bo;
	struct ttm_resource *res;
	unsigned int mem_type;
	int ret = 0;

	spin_lock(&bdev->lru_lock);
	ttm_resource_cursor_init(&cursor, man);
	res = ttm_resource_manager_first(&cursor);
	ttm_resource_cursor_fini(&cursor);
	if (!res) {
		ret = -ENOENT;
		goto out_no_ref;
	}
	bo = res->bo;
	if (!ttm_bo_get_unless_zero(bo))
		goto out_no_ref;
	mem_type = res->mem_type;
	spin_unlock(&bdev->lru_lock);
	ret = ttm_bo_reserve(bo, ctx->interruptible, ctx->no_wait_gpu, NULL);
	if (ret)
		goto out_no_lock;
	if (!bo->resource || bo->resource->mem_type != mem_type)
		goto out_bo_moved;

	if (bo->deleted) {
		ret = ttm_bo_wait_ctx(bo, ctx);
		if (!ret)
			ttm_bo_cleanup_memtype_use(bo);
	} else {
		ret = ttm_bo_evict(bo, ctx);
	}
out_bo_moved:
	dma_resv_unlock(bo->base.resv);
out_no_lock:
	ttm_bo_put(bo);
	return ret;

out_no_ref:
	spin_unlock(&bdev->lru_lock);
	return ret;
}

struct ttm_bo_alloc_state {
	/** @charge_pool: The memory pool the resource is charged to */
	struct dmem_cgroup_pool_state *charge_pool;
	/** @limit_pool: Which pool limit we should test against */
	struct dmem_cgroup_pool_state *limit_pool;
	/** @in_evict: Whether we are currently evicting buffers */
	bool in_evict;
	/** @may_try_low: If only unprotected BOs, i.e. BOs whose cgroup
	 *  is exceeding its dmem low/min protection, should be considered for eviction
	 */
	bool may_try_low;
};

/**
 * ttm_bo_alloc_at_place - Attempt allocating a BO's backing store in a place
 *
 * @bo: The buffer to allocate the backing store of
 * @place: The place to attempt allocation in
 * @ctx: ttm_operation_ctx associated with this allocation
 * @force_space: If we should evict buffers to force space
 * @res: On allocation success, the resulting struct ttm_resource.
 * @alloc_state: Object holding allocation state such as charged cgroups.
 *
 * Returns:
 * -EBUSY: No space available, but allocation should be retried with ttm_bo_evict_alloc.
 * -ENOSPC: No space available, allocation should not be retried.
 * -ERESTARTSYS: An interruptible sleep was interrupted by a signal.
 *
 */
static int ttm_bo_alloc_at_place(struct ttm_buffer_object *bo,
				 const struct ttm_place *place,
				 bool force_space,
				 struct ttm_resource **res,
				 struct ttm_bo_alloc_state *alloc_state)
{
	bool may_evict;
	int ret;

	may_evict = !alloc_state->in_evict && force_space &&
		    place->mem_type != TTM_PL_SYSTEM;
	if (!alloc_state->charge_pool) {
		ret = ttm_resource_try_charge(bo, place, &alloc_state->charge_pool,
					      force_space ? &alloc_state->limit_pool
							  : NULL);
		if (ret) {
			/*
			 * -EAGAIN means the charge failed, which we treat
			 * like an allocation failure. Therefore, return an
			 * error code indicating the allocation failed -
			 * either -EBUSY if the allocation should be
			 * retried with eviction, or -ENOSPC if there should
			 * be no second attempt.
			 */
			if (!alloc_state->in_evict)
				alloc_state->may_try_low = may_evict;
			if (ret == -EAGAIN)
				ret = may_evict ? -EBUSY : -ENOSPC;
			return ret;
		}
	}

	/*
	 * cgroup protection plays a special role in eviction.
	 * Conceptually, protection of memory via the dmem cgroup controller
	 * entitles the protected cgroup to use a certain amount of memory.
	 * There are two types of protection - the 'low' limit is a
	 * "best-effort" protection, whereas the 'min' limit provides a hard
	 * guarantee that memory within the cgroup's allowance will not be
	 * evicted under any circumstance.
	 *
	 * To faithfully model this concept in TTM, we also need to take cgroup
	 * protection into account when allocating. When allocation in one
	 * place fails, TTM will default to trying other places first before
	 * evicting.
	 * If the allocation is covered by dmem cgroup protection, however,
	 * this prevents the allocation from using the memory it is "entitled"
	 * to. To make sure unprotected allocations cannot push new protected
	 * allocations out of places they are "entitled" to use, we should
	 * evict buffers not covered by any cgroup protection, if this
	 * allocation is covered by cgroup protection.
	 *
	 * Buffers covered by 'min' protection are a special case - the 'min'
	 * limit is a stronger guarantee than 'low', and thus buffers protected
	 * by 'low' but not 'min' should also be considered for eviction.
	 * Buffers protected by 'min' will never be considered for eviction
	 * anyway, so the regular eviction path should be triggered here.
	 * Buffers protected by 'low' but not 'min' will take a special
	 * eviction path that only evicts buffers covered by neither 'low' or
	 * 'min' protections.
	 */
	if (!alloc_state->in_evict) {
		may_evict |= dmem_cgroup_below_min(NULL, alloc_state->charge_pool);
		alloc_state->may_try_low = may_evict;

		may_evict |= dmem_cgroup_below_low(NULL, alloc_state->charge_pool);
	}

	ret = ttm_resource_alloc(bo, place, res, alloc_state->charge_pool);
	if (ret) {
		if (ret == -ENOSPC && may_evict)
			ret = -EBUSY;
		return ret;
	}

	/*
	 * Ownership of charge_pool has been transferred to the TTM resource,
	 * don't make the caller think we still hold a reference to it.
	 */
	alloc_state->charge_pool = NULL;
	return 0;
}

/**
 * struct ttm_bo_evict_walk - Parameters for the evict walk.
 */
struct ttm_bo_evict_walk {
	/** @walk: The walk base parameters. */
	struct ttm_lru_walk walk;
	/** @place: The place passed to the resource allocation. */
	const struct ttm_place *place;
	/** @evictor: The buffer object we're trying to make room for. */
	struct ttm_buffer_object *evictor;
	/** @res: The allocated resource if any. */
	struct ttm_resource **res;
	/** @evicted: Number of successful evictions. */
	unsigned long evicted;

	/** @try_low: Whether we should attempt to evict BO's with low watermark threshold */
	bool try_low;
	/** @hit_low: If we cannot evict a bo when @try_low is false (first pass) */
	bool hit_low;

	/** @alloc_state: State associated with the allocation attempt. */
	struct ttm_bo_alloc_state *alloc_state;
};

static s64 ttm_bo_evict_cb(struct ttm_lru_walk *walk, struct ttm_buffer_object *bo)
{
	struct ttm_bo_evict_walk *evict_walk =
		container_of(walk, typeof(*evict_walk), walk);
	struct dmem_cgroup_pool_state *limit_pool, *ancestor = NULL;
	s64 bo_size = bo->base.size;
	bool evict_valuable;
	s64 lret;

	/*
	 * If may_try_low is not set, then we're trying to evict unprotected
	 * buffers in favor of a protected allocation for charge_pool. Explicitly skip
	 * buffers belonging to the same cgroup here - that cgroup is definitely protected,
	 * even though dmem_cgroup_state_evict_valuable would allow the eviction because a
	 * cgroup is always allowed to evict from itself even if it is protected.
	 */
	if (!evict_walk->alloc_state->may_try_low &&
			bo->resource->css == evict_walk->alloc_state->charge_pool)
		return 0;

	limit_pool = evict_walk->alloc_state->limit_pool;
	/*
	 * If there is no explicit limit pool, find the root of the shared subtree between
	 * evictor and evictee. This is important so that recursive protection rules can
	 * apply properly: Recursive protection distributes cgroup protection afforded
	 * to a parent cgroup but not used explicitly by a child cgroup between all child
	 * cgroups (see docs of effective_protection in mm/page_counter.c). However, when
	 * direct siblings compete for memory, siblings that were explicitly protected
	 * should get prioritized over siblings that weren't. This only happens correctly
	 * when the root of the shared subtree is passed to
	 * dmem_cgroup_state_evict_valuable. Otherwise, the effective-protection
	 * calculation cannot distinguish direct siblings from unrelated subtrees and the
	 * calculated protection ends up wrong.
	 */
	if (!limit_pool) {
		ancestor = dmem_cgroup_get_common_ancestor(bo->resource->css,
							   evict_walk->alloc_state->charge_pool);
		limit_pool = ancestor;
	}

	evict_valuable = dmem_cgroup_state_evict_valuable(limit_pool, bo->resource->css,
							  evict_walk->try_low,
							  &evict_walk->hit_low);
	if (ancestor)
		dmem_cgroup_pool_state_put(ancestor);

	if (!evict_valuable)
		return 0;

	/*
	 * evict_walk->place is NULL in cgroup drain mode.  Drivers'
	 * eviction_valuable() callbacks must handle a NULL place, treating it
	 * as "any placement": the TTM base implementation already does so via
	 * ttm_resource_intersects().
	 */
	if (bo->pin_count || !bo->bdev->funcs->eviction_valuable(bo, evict_walk->place))
		return 0;

	if (bo->deleted) {
		lret = ttm_bo_wait_ctx(bo, walk->arg.ctx);
		if (!lret)
			ttm_bo_cleanup_memtype_use(bo);
	} else {
		lret = ttm_bo_evict(bo, walk->arg.ctx);
	}

	if (lret)
		goto out;

	evict_walk->evicted++;
	if (evict_walk->res) {
		lret = ttm_bo_alloc_at_place(evict_walk->evictor,
					     evict_walk->place, false,
					     evict_walk->res,
					     evict_walk->alloc_state);
		if (lret == 0)
			return 1;
	} else {
		/* Cgroup drain: return bytes freed for byte-denominated progress. */
		return bo_size;
	}
out:
	/* Errors that should terminate the walk. */
	if (lret == -ENOSPC)
		return -EBUSY;

	return lret;
}

static const struct ttm_lru_walk_ops ttm_evict_walk_ops = {
	.process_bo = ttm_bo_evict_cb,
};

static int ttm_bo_evict_alloc(struct ttm_device *bdev,
			      struct ttm_resource_manager *man,
			      const struct ttm_place *place,
			      struct ttm_buffer_object *evictor,
			      struct ttm_operation_ctx *ctx,
			      struct ww_acquire_ctx *ticket,
			      struct ttm_resource **res,
			      struct ttm_bo_alloc_state *state)
{
	struct ttm_bo_evict_walk evict_walk = {
		.walk = {
			.ops = &ttm_evict_walk_ops,
			.arg = {
				.ctx = ctx,
				.ticket = ticket,
			}
		},
		.place = place,
		.evictor = evictor,
		.res = res,
		.alloc_state = state,
	};
	s64 lret;

	state->in_evict = true;

	evict_walk.walk.arg.trylock_only = true;
	lret = ttm_lru_walk_for_evict(&evict_walk.walk, bdev, man, 1);

	/* If we failed to find enough BOs to evict, but we skipped over
	 * some BOs because they were covered by dmem low protection, retry
	 * evicting these protected BOs too, except if we're told not to
	 * consider protected BOs at all.
	 */
	if (!lret && evict_walk.hit_low && state->may_try_low) {
		evict_walk.try_low = true;
		lret = ttm_lru_walk_for_evict(&evict_walk.walk, bdev, man, 1);
	}
	if (lret || !ticket)
		goto out;

	/* Reset low limit */
	evict_walk.try_low = evict_walk.hit_low = false;
	/* If ticket-locking, repeat while making progress. */
	evict_walk.walk.arg.trylock_only = false;

retry:
	do {
		/* The walk may clear the evict_walk.walk.ticket field */
		evict_walk.walk.arg.ticket = ticket;
		evict_walk.evicted = 0;
		lret = ttm_lru_walk_for_evict(&evict_walk.walk, bdev, man, 1);
	} while (!lret && evict_walk.evicted);

	/* We hit the low limit? Try once more */
	if (!lret && evict_walk.hit_low && !evict_walk.try_low &&
			state->may_try_low) {
		evict_walk.try_low = true;
		goto retry;
	}
out:
	state->in_evict = false;
	if (lret < 0)
		return lret;
	if (lret == 0)
		return -EBUSY;
	return 0;
}

/**
 * ttm_bo_evict_cgroup() - Evict buffer objects charged to a specific cgroup.
 * @bdev: The TTM device.
 * @man: The resource manager whose LRU to walk.
 * @limit_pool: The cgroup pool state whose members should be evicted.
 * @target_bytes: Number of bytes to free.
 * @ctx: The TTM operation context.
 *
 * Walk the LRU of @man and evict buffer objects that are charged to the
 * cgroup identified by @limit_pool, until at least @target_bytes have been
 * freed.  Mirrors the two-pass (trylock -> sleeping-lock, low-watermark)
 * strategy used by ttm_bo_evict_alloc().
 *
 * Return: >= @target_bytes on full success, 0..target_bytes-1 if partial,
 *         negative error code on fatal error.
 */
s64 ttm_bo_evict_cgroup(struct ttm_device *bdev,
			struct ttm_resource_manager *man,
			struct dmem_cgroup_pool_state *limit_pool,
			s64 target_bytes,
			struct ttm_operation_ctx *ctx)
{
	struct ttm_bo_evict_walk evict_walk = {
		.walk = {
			.ops = &ttm_evict_walk_ops,
			.arg = { .ctx = ctx },
		},
		.alloc_state = &(struct ttm_bo_alloc_state) {
			.limit_pool = limit_pool,
			.in_evict = true,
		},
		/* place, evictor, res left NULL: selects cgroup drain mode */
	};
	s64 lret, pass;

	evict_walk.walk.arg.trylock_only = true;
	lret = ttm_lru_walk_for_evict(&evict_walk.walk, bdev, man, target_bytes);
	if (lret < 0 || lret >= target_bytes)
		return lret;

	/* Second pass: also evict BOs at the low watermark. */
	if (evict_walk.hit_low) {
		evict_walk.try_low = true;
		pass = ttm_lru_walk_for_evict(&evict_walk.walk, bdev, man,
					      target_bytes - lret);
		if (pass < 0)
			return pass;
		lret += pass;
		if (lret >= target_bytes)
			return lret;
	}

	/* Full sleeping-lock pass for remaining target. */
	evict_walk.try_low = evict_walk.hit_low = false;
	evict_walk.walk.arg.trylock_only = false;

retry:
	evict_walk.walk.arg.sleeping_lock = true;
	do {
		evict_walk.evicted = 0;
		pass = ttm_lru_walk_for_evict(&evict_walk.walk, bdev, man,
					      target_bytes - lret);
		if (pass < 0) {
			lret = pass;
			goto out;
		}
		lret += pass;
	} while (lret < target_bytes && evict_walk.evicted);

	/* One more attempt if we hit the low limit during sleeping-lock pass. */
	if (lret < target_bytes && evict_walk.hit_low && !evict_walk.try_low) {
		evict_walk.try_low = true;
		goto retry;
	}

out:
	return lret;
}
EXPORT_SYMBOL(ttm_bo_evict_cgroup);

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
	if (!bo->pin_count++ && bo->resource)
		ttm_resource_move_to_lru_tail(bo->resource);
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
	if (!--bo->pin_count && bo->resource) {
		ttm_resource_add_bulk_move(bo->resource, bo);
		ttm_resource_move_to_lru_tail(bo->resource);
	}
	spin_unlock(&bo->bdev->lru_lock);
}
EXPORT_SYMBOL(ttm_bo_unpin);

/*
 * Add the pipelined eviction fencesto the BO as kernel dependency and reserve new
 * fence slots.
 */
static int ttm_bo_add_pipelined_eviction_fences(struct ttm_buffer_object *bo,
						struct ttm_resource_manager *man,
						bool no_wait_gpu)
{
	struct dma_fence *fence;
	int i;

	spin_lock(&man->eviction_lock);
	for (i = 0; i < TTM_NUM_MOVE_FENCES; i++) {
		fence = man->eviction_fences[i];
		if (!fence)
			continue;

		if (no_wait_gpu) {
			if (!dma_fence_is_signaled(fence)) {
				spin_unlock(&man->eviction_lock);
				return -EBUSY;
			}
		} else {
			dma_resv_add_fence(bo->base.resv, fence, DMA_RESV_USAGE_KERNEL);
		}
	}
	spin_unlock(&man->eviction_lock);

	/* TODO: this call should be removed. */
	return dma_resv_reserve_fences(bo->base.resv, 1);
}

/**
 * ttm_bo_alloc_resource - Allocate backing store for a BO
 *
 * @bo: Pointer to a struct ttm_buffer_object of which we want a resource for
 * @placement: Proposed new placement for the buffer object
 * @ctx: if and how to sleep, lock buffers and alloc memory
 * @force_space: If we should evict buffers to force space
 * @res: The resulting struct ttm_resource.
 *
 * Allocates a resource for the buffer object pointed to by @bo, using the
 * placement flags in @placement, potentially evicting other buffer objects when
 * @force_space is true.
 * This function may sleep while waiting for resources to become available.
 * Returns:
 * -EBUSY: No space available (only if no_wait == true).
 * -ENOSPC: Could not allocate space for the buffer object, either due to
 * fragmentation or concurrent allocators.
 * -ERESTARTSYS: An interruptible sleep was interrupted by a signal.
 */
static int ttm_bo_alloc_resource(struct ttm_buffer_object *bo,
				 struct ttm_placement *placement,
				 struct ttm_operation_ctx *ctx,
				 bool force_space,
				 struct ttm_resource **res)
{
	struct ttm_device *bdev = bo->bdev;
	struct ww_acquire_ctx *ticket;
	int i, ret;

	ticket = dma_resv_locking_ctx(bo->base.resv);
	ret = dma_resv_reserve_fences(bo->base.resv, TTM_NUM_MOVE_FENCES);
	if (unlikely(ret))
		return ret;

	for (i = 0; i < placement->num_placement; ++i) {
		const struct ttm_place *place = &placement->placement[i];
		struct ttm_bo_alloc_state alloc_state = {};
		struct ttm_resource_manager *man;

		man = ttm_manager_type(bdev, place->mem_type);
		if (!man || !ttm_resource_manager_used(man))
			continue;

		if (place->flags & (force_space ? TTM_PL_FLAG_DESIRED :
				    TTM_PL_FLAG_FALLBACK))
			continue;

		ret = ttm_bo_alloc_at_place(bo, place, force_space, res,
					    &alloc_state);

		if (ret == -ENOSPC) {
			dmem_cgroup_uncharge(alloc_state.charge_pool, bo->base.size);
			dmem_cgroup_pool_state_put(alloc_state.limit_pool);
			continue;
		} else if (ret == -EBUSY) {
			ret = ttm_bo_evict_alloc(bdev, man, place, bo, ctx,
						 ticket, res, &alloc_state);

			dmem_cgroup_pool_state_put(alloc_state.limit_pool);

			if (ret) {
				dmem_cgroup_uncharge(alloc_state.charge_pool,
						bo->base.size);
				if (ret == -EBUSY)
					continue;
				return ret;
			}
		} else if (ret) {
			dmem_cgroup_uncharge(alloc_state.charge_pool, bo->base.size);
			dmem_cgroup_pool_state_put(alloc_state.limit_pool);
			return ret;
		}

		ret = ttm_bo_add_pipelined_eviction_fences(bo, man, ctx->no_wait_gpu);
		if (unlikely(ret)) {
			ttm_resource_free(bo, res);
			if (ret == -EBUSY)
				continue;

			return ret;
		}
		return 0;
	}

	return -ENOSPC;
}

/*
 * ttm_bo_mem_space - Wrapper around ttm_bo_alloc_resource
 *
 * @bo: Pointer to a struct ttm_buffer_object of which we want a resource for
 * @placement: Proposed new placement for the buffer object
 * @res: The resulting struct ttm_resource.
 * @ctx: if and how to sleep, lock buffers and alloc memory
 *
 * Tries both idle allocation and forcefully eviction of buffers. See
 * ttm_bo_alloc_resource for details.
 */
int ttm_bo_mem_space(struct ttm_buffer_object *bo,
		     struct ttm_placement *placement,
		     struct ttm_resource **res,
		     struct ttm_operation_ctx *ctx)
{
	bool force_space = false;
	int ret;

	do {
		ret = ttm_bo_alloc_resource(bo, placement, ctx,
					    force_space, res);
		force_space = !force_space;
	} while (ret == -ENOSPC && force_space);

	return ret;
}
EXPORT_SYMBOL(ttm_bo_mem_space);

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
	struct ttm_resource *res;
	struct ttm_place hop;
	bool force_space;
	int ret;

	dma_resv_assert_held(bo->base.resv);

	/*
	 * Remove the backing store if no placement is given.
	 */
	if (!placement->num_placement)
		return ttm_bo_pipeline_gutting(bo);

	force_space = false;
	do {
		/* Check whether we need to move buffer. */
		if (bo->resource &&
		    ttm_resource_compatible(bo->resource, placement,
					    force_space))
			return 0;

		/* Moving of pinned BOs is forbidden */
		if (bo->pin_count)
			return -EINVAL;

		/*
		 * Determine where to move the buffer.
		 *
		 * If driver determines move is going to need
		 * an extra step then it will return -EMULTIHOP
		 * and the buffer will be moved to the temporary
		 * stop and the driver will be called to make
		 * the second hop.
		 */
		ret = ttm_bo_alloc_resource(bo, placement, ctx, force_space,
					    &res);
		force_space = !force_space;
		if (ret == -ENOSPC)
			continue;
		if (ret)
			return ret;

bounce:
		ret = ttm_bo_handle_move_mem(bo, res, false, ctx, &hop);
		if (ret == -EMULTIHOP) {
			ret = ttm_bo_bounce_temp_buffer(bo, ctx, &hop);
			/* try and move to final place now. */
			if (!ret)
				goto bounce;
		}
		if (ret) {
			ttm_resource_free(bo, &res);
			return ret;
		}

	} while (ret && force_space);

	/* For backward compatibility with userspace */
	if (ret == -ENOSPC)
		return bo->bdev->alloc_flags & TTM_ALLOCATION_PROPAGATE_ENOSPC ?
		       ret : -ENOMEM;

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
	struct ttm_operation_ctx ctx = { .interruptible = interruptible };
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

/**
 * struct ttm_bo_swapout_walk - Parameters for the swapout walk
 */
struct ttm_bo_swapout_walk {
	/** @walk: The walk base parameters. */
	struct ttm_lru_walk walk;
	/** @gfp_flags: The gfp flags to use for ttm_tt_swapout() */
	gfp_t gfp_flags;
	/** @hit_low: Whether we should attempt to swap BO's with low watermark threshold */
	/** @evict_low: If we cannot swap a bo when @try_low is false (first pass) */
	bool hit_low, evict_low;
};

static s64
ttm_bo_swapout_cb(struct ttm_lru_walk *walk, struct ttm_buffer_object *bo)
{
	struct ttm_place place = { .mem_type = bo->resource->mem_type };
	struct ttm_bo_swapout_walk *swapout_walk =
		container_of(walk, typeof(*swapout_walk), walk);
	struct ttm_operation_ctx *ctx = walk->arg.ctx;
	struct ttm_device *bdev = bo->bdev;
	struct ttm_tt *tt = bo->ttm;
	s64 ret;

	/*
	 * While the bo may already reside in SYSTEM placement, set
	 * SYSTEM as new placement to cover also the move further below.
	 * The driver may use the fact that we're moving from SYSTEM
	 * as an indication that we're about to swap out.
	 */
	if (bo->pin_count || !bdev->funcs->eviction_valuable(bo, &place)) {
		ret = -EBUSY;
		goto out;
	}

	if (!tt || !ttm_tt_is_populated(tt) ||
	    tt->page_flags & (TTM_TT_FLAG_EXTERNAL | TTM_TT_FLAG_SWAPPED)) {
		ret = -EBUSY;
		goto out;
	}

	if (bo->deleted) {
		pgoff_t num_pages = tt->num_pages;

		ret = ttm_bo_wait_ctx(bo, ctx);
		if (ret)
			goto out;

		ttm_bo_cleanup_memtype_use(bo);
		ret = num_pages;
		goto out;
	}

	/*
	 * Move to system cached
	 */
	if (bo->resource->mem_type != TTM_PL_SYSTEM) {
		struct ttm_resource *evict_mem;
		struct ttm_place hop;

		memset(&hop, 0, sizeof(hop));
		place.mem_type = TTM_PL_SYSTEM;
		ret = ttm_resource_alloc(bo, &place, &evict_mem, NULL);
		if (ret)
			goto out;

		ret = ttm_bo_handle_move_mem(bo, evict_mem, true, ctx, &hop);
		if (ret) {
			WARN(ret == -EMULTIHOP,
			     "Unexpected multihop in swapout - likely driver bug.\n");
			ttm_resource_free(bo, &evict_mem);
			goto out;
		}
	}

	/*
	 * Make sure BO is idle.
	 */
	ret = ttm_bo_wait_ctx(bo, ctx);
	if (ret)
		goto out;

	ttm_bo_unmap_virtual(bo);
	if (bdev->funcs->swap_notify)
		bdev->funcs->swap_notify(bo);

	if (ttm_tt_is_populated(tt)) {
		ret = ttm_tt_swapout(bdev, tt, swapout_walk->gfp_flags);
		if (!ret) {
			spin_lock(&bdev->lru_lock);
			ttm_resource_del_bulk_move_unevictable(bo->resource, bo);
			ttm_resource_move_to_lru_tail(bo->resource);
			spin_unlock(&bdev->lru_lock);
		}
	}

out:
	/* Consider -ENOMEM and -ENOSPC non-fatal. */
	if (ret == -ENOMEM || ret == -ENOSPC)
		ret = -EBUSY;

	return ret;
}

const struct ttm_lru_walk_ops ttm_swap_ops = {
	.process_bo = ttm_bo_swapout_cb,
};

/**
 * ttm_bo_swapout() - Swap out buffer objects on the LRU list to shmem.
 * @bdev: The ttm device.
 * @ctx: The ttm_operation_ctx governing the swapout operation.
 * @man: The resource manager whose resources / buffer objects are
 * goint to be swapped out.
 * @gfp_flags: The gfp flags used for shmem page allocations.
 * @target: The desired number of pages to swap out.
 *
 * Return: The number of pages actually swapped out, or negative error code
 * on error.
 */
s64 ttm_bo_swapout(struct ttm_device *bdev, struct ttm_operation_ctx *ctx,
		   struct ttm_resource_manager *man, gfp_t gfp_flags,
		   s64 target)
{
	struct ttm_bo_swapout_walk swapout_walk = {
		.walk = {
			.ops = &ttm_swap_ops,
			.arg = {
				.ctx = ctx,
				.trylock_only = true,
			},
		},
		.gfp_flags = gfp_flags,
	};

	return ttm_lru_walk_for_evict(&swapout_walk.walk, bdev, man, target);
}
EXPORT_SYMBOL_FOR_TESTS_ONLY(ttm_bo_swapout);

void ttm_bo_tt_destroy(struct ttm_buffer_object *bo)
{
	if (bo->ttm == NULL)
		return;

	ttm_tt_unpopulate(bo->bdev, bo->ttm);
	ttm_tt_destroy(bo->bdev, bo->ttm);
	bo->ttm = NULL;
}

/**
 * ttm_bo_populate() - Ensure that a buffer object has backing pages
 * @bo: The buffer object
 * @ctx: The ttm_operation_ctx governing the operation.
 *
 * For buffer objects in a memory type whose manager uses
 * struct ttm_tt for backing pages, ensure those backing pages
 * are present and with valid content. The bo's resource is also
 * placed on the correct LRU list if it was previously swapped
 * out.
 *
 * Return: 0 if successful, negative error code on failure.
 * Note: May return -EINTR or -ERESTARTSYS if @ctx::interruptible
 * is set to true.
 */
int ttm_bo_populate(struct ttm_buffer_object *bo,
		    struct ttm_operation_ctx *ctx)
{
	struct ttm_device *bdev = bo->bdev;
	struct ttm_tt *tt = bo->ttm;
	bool swapped;
	int ret;

	dma_resv_assert_held(bo->base.resv);

	if (!tt)
		return 0;

	swapped = ttm_tt_is_swapped(tt);
	ret = ttm_tt_populate(bdev, tt, ctx);
	if (ret)
		return ret;

	if (swapped && !ttm_tt_is_swapped(tt) && !bo->pin_count &&
	    bo->resource) {
		spin_lock(&bdev->lru_lock);
		ttm_resource_add_bulk_move(bo->resource, bo);
		ttm_resource_move_to_lru_tail(bo->resource);
		spin_unlock(&bdev->lru_lock);
	}

	return 0;
}
EXPORT_SYMBOL(ttm_bo_populate);

int ttm_bo_setup_export(struct ttm_buffer_object *bo,
			struct ttm_operation_ctx *ctx)
{
	int ret;

	ret = ttm_bo_reserve(bo, false, false, NULL);
	if (ret != 0)
		return ret;

	ret = ttm_bo_populate(bo, ctx);
	ttm_bo_unreserve(bo);
	return ret;
}
EXPORT_SYMBOL(ttm_bo_setup_export);
