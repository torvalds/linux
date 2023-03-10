// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/ttm/ttm_tt.h>

#include "i915_deps.h"
#include "i915_drv.h"
#include "intel_memory_region.h"
#include "intel_region_ttm.h"

#include "gem/i915_gem_object.h"
#include "gem/i915_gem_region.h"
#include "gem/i915_gem_ttm.h"
#include "gem/i915_gem_ttm_move.h"

#include "gt/intel_engine_pm.h"
#include "gt/intel_gt.h"
#include "gt/intel_migrate.h"

/**
 * DOC: Selftest failure modes for failsafe migration:
 *
 * For fail_gpu_migration, the gpu blit scheduled is always a clear blit
 * rather than a copy blit, and then we force the failure paths as if
 * the blit fence returned an error.
 *
 * For fail_work_allocation we fail the kmalloc of the async worker, we
 * sync the gpu blit. If it then fails, or fail_gpu_migration is set to
 * true, then a memcpy operation is performed sync.
 */
#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
static bool fail_gpu_migration;
static bool fail_work_allocation;
static bool ban_memcpy;

void i915_ttm_migrate_set_failure_modes(bool gpu_migration,
					bool work_allocation)
{
	fail_gpu_migration = gpu_migration;
	fail_work_allocation = work_allocation;
}

void i915_ttm_migrate_set_ban_memcpy(bool ban)
{
	ban_memcpy = ban;
}
#endif

static enum i915_cache_level
i915_ttm_cache_level(struct drm_i915_private *i915, struct ttm_resource *res,
		     struct ttm_tt *ttm)
{
	return ((HAS_LLC(i915) || HAS_SNOOP(i915)) &&
		!i915_ttm_gtt_binds_lmem(res) &&
		ttm->caching == ttm_cached) ? I915_CACHE_LLC :
		I915_CACHE_NONE;
}

static struct intel_memory_region *
i915_ttm_region(struct ttm_device *bdev, int ttm_mem_type)
{
	struct drm_i915_private *i915 = container_of(bdev, typeof(*i915), bdev);

	/* There's some room for optimization here... */
	GEM_BUG_ON(ttm_mem_type != I915_PL_SYSTEM &&
		   ttm_mem_type < I915_PL_LMEM0);
	if (ttm_mem_type == I915_PL_SYSTEM)
		return intel_memory_region_lookup(i915, INTEL_MEMORY_SYSTEM,
						  0);

	return intel_memory_region_lookup(i915, INTEL_MEMORY_LOCAL,
					  ttm_mem_type - I915_PL_LMEM0);
}

/**
 * i915_ttm_adjust_domains_after_move - Adjust the GEM domains after a
 * TTM move
 * @obj: The gem object
 */
void i915_ttm_adjust_domains_after_move(struct drm_i915_gem_object *obj)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);

	if (i915_ttm_cpu_maps_iomem(bo->resource) || bo->ttm->caching != ttm_cached) {
		obj->write_domain = I915_GEM_DOMAIN_WC;
		obj->read_domains = I915_GEM_DOMAIN_WC;
	} else {
		obj->write_domain = I915_GEM_DOMAIN_CPU;
		obj->read_domains = I915_GEM_DOMAIN_CPU;
	}
}

/**
 * i915_ttm_adjust_gem_after_move - Adjust the GEM state after a TTM move
 * @obj: The gem object
 *
 * Adjusts the GEM object's region, mem_flags and cache coherency after a
 * TTM move.
 */
void i915_ttm_adjust_gem_after_move(struct drm_i915_gem_object *obj)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	unsigned int cache_level;
	unsigned int mem_flags;
	unsigned int i;
	int mem_type;

	/*
	 * We might have been purged (or swapped out) if the resource is NULL,
	 * in which case the SYSTEM placement is the closest match to describe
	 * the current domain. If the object is ever used in this state then we
	 * will require moving it again.
	 */
	if (!bo->resource) {
		mem_flags = I915_BO_FLAG_STRUCT_PAGE;
		mem_type = I915_PL_SYSTEM;
		cache_level = I915_CACHE_NONE;
	} else {
		mem_flags = i915_ttm_cpu_maps_iomem(bo->resource) ? I915_BO_FLAG_IOMEM :
			I915_BO_FLAG_STRUCT_PAGE;
		mem_type = bo->resource->mem_type;
		cache_level = i915_ttm_cache_level(to_i915(bo->base.dev), bo->resource,
						   bo->ttm);
	}

	/*
	 * If object was moved to an allowable region, update the object
	 * region to consider it migrated. Note that if it's currently not
	 * in an allowable region, it's evicted and we don't update the
	 * object region.
	 */
	if (intel_region_to_ttm_type(obj->mm.region) != mem_type) {
		for (i = 0; i < obj->mm.n_placements; ++i) {
			struct intel_memory_region *mr = obj->mm.placements[i];

			if (intel_region_to_ttm_type(mr) == mem_type &&
			    mr != obj->mm.region) {
				i915_gem_object_release_memory_region(obj);
				i915_gem_object_init_memory_region(obj, mr);
				break;
			}
		}
	}

	obj->mem_flags &= ~(I915_BO_FLAG_STRUCT_PAGE | I915_BO_FLAG_IOMEM);
	obj->mem_flags |= mem_flags;

	i915_gem_object_set_cache_coherency(obj, cache_level);
}

/**
 * i915_ttm_move_notify - Prepare an object for move
 * @bo: The ttm buffer object.
 *
 * This function prepares an object for move by removing all GPU bindings,
 * removing all CPU mapings and finally releasing the pages sg-table.
 *
 * Return: 0 if successful, negative error code on error.
 */
int i915_ttm_move_notify(struct ttm_buffer_object *bo)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	int ret;

	/*
	 * Note: The async unbinding here will actually transform the
	 * blocking wait for unbind into a wait before finally submitting
	 * evict / migration blit and thus stall the migration timeline
	 * which may not be good for overall throughput. We should make
	 * sure we await the unbind fences *after* the migration blit
	 * instead of *before* as we currently do.
	 */
	ret = i915_gem_object_unbind(obj, I915_GEM_OBJECT_UNBIND_ACTIVE |
				     I915_GEM_OBJECT_UNBIND_ASYNC);
	if (ret)
		return ret;

	ret = __i915_gem_object_put_pages(obj);
	if (ret)
		return ret;

	return 0;
}

static struct dma_fence *i915_ttm_accel_move(struct ttm_buffer_object *bo,
					     bool clear,
					     struct ttm_resource *dst_mem,
					     struct ttm_tt *dst_ttm,
					     struct sg_table *dst_st,
					     const struct i915_deps *deps)
{
	struct drm_i915_private *i915 = container_of(bo->bdev, typeof(*i915),
						     bdev);
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	struct i915_request *rq;
	struct ttm_tt *src_ttm = bo->ttm;
	enum i915_cache_level src_level, dst_level;
	int ret;

	if (!to_gt(i915)->migrate.context || intel_gt_is_wedged(to_gt(i915)))
		return ERR_PTR(-EINVAL);

	/* With fail_gpu_migration, we always perform a GPU clear. */
	if (I915_SELFTEST_ONLY(fail_gpu_migration))
		clear = true;

	dst_level = i915_ttm_cache_level(i915, dst_mem, dst_ttm);
	if (clear) {
		if (bo->type == ttm_bo_type_kernel &&
		    !I915_SELFTEST_ONLY(fail_gpu_migration))
			return ERR_PTR(-EINVAL);

		intel_engine_pm_get(to_gt(i915)->migrate.context->engine);
		ret = intel_context_migrate_clear(to_gt(i915)->migrate.context, deps,
						  dst_st->sgl, dst_level,
						  i915_ttm_gtt_binds_lmem(dst_mem),
						  0, &rq);
	} else {
		struct i915_refct_sgt *src_rsgt =
			i915_ttm_resource_get_st(obj, bo->resource);

		if (IS_ERR(src_rsgt))
			return ERR_CAST(src_rsgt);

		src_level = i915_ttm_cache_level(i915, bo->resource, src_ttm);
		intel_engine_pm_get(to_gt(i915)->migrate.context->engine);
		ret = intel_context_migrate_copy(to_gt(i915)->migrate.context,
						 deps, src_rsgt->table.sgl,
						 src_level,
						 i915_ttm_gtt_binds_lmem(bo->resource),
						 dst_st->sgl, dst_level,
						 i915_ttm_gtt_binds_lmem(dst_mem),
						 &rq);

		i915_refct_sgt_put(src_rsgt);
	}

	intel_engine_pm_put(to_gt(i915)->migrate.context->engine);

	if (ret && rq) {
		i915_request_wait(rq, 0, MAX_SCHEDULE_TIMEOUT);
		i915_request_put(rq);
	}

	return ret ? ERR_PTR(ret) : &rq->fence;
}

/**
 * struct i915_ttm_memcpy_arg - argument for the bo memcpy functionality.
 * @_dst_iter: Storage space for the destination kmap iterator.
 * @_src_iter: Storage space for the source kmap iterator.
 * @dst_iter: Pointer to the destination kmap iterator.
 * @src_iter: Pointer to the source kmap iterator.
 * @clear: Whether to clear instead of copy.
 * @src_rsgt: Refcounted scatter-gather list of source memory.
 * @dst_rsgt: Refcounted scatter-gather list of destination memory.
 */
struct i915_ttm_memcpy_arg {
	union {
		struct ttm_kmap_iter_tt tt;
		struct ttm_kmap_iter_iomap io;
	} _dst_iter,
	_src_iter;
	struct ttm_kmap_iter *dst_iter;
	struct ttm_kmap_iter *src_iter;
	unsigned long num_pages;
	bool clear;
	struct i915_refct_sgt *src_rsgt;
	struct i915_refct_sgt *dst_rsgt;
};

/**
 * struct i915_ttm_memcpy_work - Async memcpy worker under a dma-fence.
 * @fence: The dma-fence.
 * @work: The work struct use for the memcpy work.
 * @lock: The fence lock. Not used to protect anything else ATM.
 * @irq_work: Low latency worker to signal the fence since it can't be done
 * from the callback for lockdep reasons.
 * @cb: Callback for the accelerated migration fence.
 * @arg: The argument for the memcpy functionality.
 * @i915: The i915 pointer.
 * @obj: The GEM object.
 * @memcpy_allowed: Instead of processing the @arg, and falling back to memcpy
 * or memset, we wedge the device and set the @obj unknown_state, to prevent
 * further access to the object with the CPU or GPU.  On some devices we might
 * only be permitted to use the blitter engine for such operations.
 */
struct i915_ttm_memcpy_work {
	struct dma_fence fence;
	struct work_struct work;
	spinlock_t lock;
	struct irq_work irq_work;
	struct dma_fence_cb cb;
	struct i915_ttm_memcpy_arg arg;
	struct drm_i915_private *i915;
	struct drm_i915_gem_object *obj;
	bool memcpy_allowed;
};

static void i915_ttm_move_memcpy(struct i915_ttm_memcpy_arg *arg)
{
	ttm_move_memcpy(arg->clear, arg->num_pages,
			arg->dst_iter, arg->src_iter);
}

static void i915_ttm_memcpy_init(struct i915_ttm_memcpy_arg *arg,
				 struct ttm_buffer_object *bo, bool clear,
				 struct ttm_resource *dst_mem,
				 struct ttm_tt *dst_ttm,
				 struct i915_refct_sgt *dst_rsgt)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	struct intel_memory_region *dst_reg, *src_reg;

	dst_reg = i915_ttm_region(bo->bdev, dst_mem->mem_type);
	src_reg = i915_ttm_region(bo->bdev, bo->resource->mem_type);
	GEM_BUG_ON(!dst_reg || !src_reg);

	arg->dst_iter = !i915_ttm_cpu_maps_iomem(dst_mem) ?
		ttm_kmap_iter_tt_init(&arg->_dst_iter.tt, dst_ttm) :
		ttm_kmap_iter_iomap_init(&arg->_dst_iter.io, &dst_reg->iomap,
					 &dst_rsgt->table, dst_reg->region.start);

	arg->src_iter = !i915_ttm_cpu_maps_iomem(bo->resource) ?
		ttm_kmap_iter_tt_init(&arg->_src_iter.tt, bo->ttm) :
		ttm_kmap_iter_iomap_init(&arg->_src_iter.io, &src_reg->iomap,
					 &obj->ttm.cached_io_rsgt->table,
					 src_reg->region.start);
	arg->clear = clear;
	arg->num_pages = bo->base.size >> PAGE_SHIFT;

	arg->dst_rsgt = i915_refct_sgt_get(dst_rsgt);
	arg->src_rsgt = clear ? NULL :
		i915_ttm_resource_get_st(obj, bo->resource);
}

static void i915_ttm_memcpy_release(struct i915_ttm_memcpy_arg *arg)
{
	i915_refct_sgt_put(arg->src_rsgt);
	i915_refct_sgt_put(arg->dst_rsgt);
}

static void __memcpy_work(struct work_struct *work)
{
	struct i915_ttm_memcpy_work *copy_work =
		container_of(work, typeof(*copy_work), work);
	struct i915_ttm_memcpy_arg *arg = &copy_work->arg;
	bool cookie;

	/*
	 * FIXME: We need to take a closer look here. We should be able to plonk
	 * this into the fence critical section.
	 */
	if (!copy_work->memcpy_allowed) {
		struct intel_gt *gt;
		unsigned int id;

		for_each_gt(gt, copy_work->i915, id)
			intel_gt_set_wedged(gt);
	}

	cookie = dma_fence_begin_signalling();

	if (copy_work->memcpy_allowed) {
		i915_ttm_move_memcpy(arg);
	} else {
		/*
		 * Prevent further use of the object. Any future GTT binding or
		 * CPU access is not allowed once we signal the fence. Outside
		 * of the fence critical section, we then also then wedge the gpu
		 * to indicate the device is not functional.
		 *
		 * The below dma_fence_signal() is our write-memory-barrier.
		 */
		copy_work->obj->mm.unknown_state = true;
	}

	dma_fence_end_signalling(cookie);

	dma_fence_signal(&copy_work->fence);

	i915_ttm_memcpy_release(arg);
	i915_gem_object_put(copy_work->obj);
	dma_fence_put(&copy_work->fence);
}

static void __memcpy_irq_work(struct irq_work *irq_work)
{
	struct i915_ttm_memcpy_work *copy_work =
		container_of(irq_work, typeof(*copy_work), irq_work);
	struct i915_ttm_memcpy_arg *arg = &copy_work->arg;

	dma_fence_signal(&copy_work->fence);
	i915_ttm_memcpy_release(arg);
	i915_gem_object_put(copy_work->obj);
	dma_fence_put(&copy_work->fence);
}

static void __memcpy_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct i915_ttm_memcpy_work *copy_work =
		container_of(cb, typeof(*copy_work), cb);

	if (unlikely(fence->error || I915_SELFTEST_ONLY(fail_gpu_migration))) {
		INIT_WORK(&copy_work->work, __memcpy_work);
		queue_work(system_unbound_wq, &copy_work->work);
	} else {
		init_irq_work(&copy_work->irq_work, __memcpy_irq_work);
		irq_work_queue(&copy_work->irq_work);
	}
}

static const char *get_driver_name(struct dma_fence *fence)
{
	return "i915_ttm_memcpy_work";
}

static const char *get_timeline_name(struct dma_fence *fence)
{
	return "unbound";
}

static const struct dma_fence_ops dma_fence_memcpy_ops = {
	.get_driver_name = get_driver_name,
	.get_timeline_name = get_timeline_name,
};

static struct dma_fence *
i915_ttm_memcpy_work_arm(struct i915_ttm_memcpy_work *work,
			 struct dma_fence *dep)
{
	int ret;

	spin_lock_init(&work->lock);
	dma_fence_init(&work->fence, &dma_fence_memcpy_ops, &work->lock, 0, 0);
	dma_fence_get(&work->fence);
	ret = dma_fence_add_callback(dep, &work->cb, __memcpy_cb);
	if (ret) {
		if (ret != -ENOENT)
			dma_fence_wait(dep, false);

		return ERR_PTR(I915_SELFTEST_ONLY(fail_gpu_migration) ? -EINVAL :
			       dep->error);
	}

	return &work->fence;
}

static bool i915_ttm_memcpy_allowed(struct ttm_buffer_object *bo,
				    struct ttm_resource *dst_mem)
{
	if (i915_gem_object_needs_ccs_pages(i915_ttm_to_gem(bo)))
		return false;

	if (!(i915_ttm_resource_mappable(bo->resource) &&
	      i915_ttm_resource_mappable(dst_mem)))
		return false;

	return I915_SELFTEST_ONLY(ban_memcpy) ? false : true;
}

static struct dma_fence *
__i915_ttm_move(struct ttm_buffer_object *bo,
		const struct ttm_operation_ctx *ctx, bool clear,
		struct ttm_resource *dst_mem, struct ttm_tt *dst_ttm,
		struct i915_refct_sgt *dst_rsgt, bool allow_accel,
		const struct i915_deps *move_deps)
{
	const bool memcpy_allowed = i915_ttm_memcpy_allowed(bo, dst_mem);
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	struct drm_i915_private *i915 = to_i915(bo->base.dev);
	struct i915_ttm_memcpy_work *copy_work = NULL;
	struct i915_ttm_memcpy_arg _arg, *arg = &_arg;
	struct dma_fence *fence = ERR_PTR(-EINVAL);

	if (allow_accel) {
		fence = i915_ttm_accel_move(bo, clear, dst_mem, dst_ttm,
					    &dst_rsgt->table, move_deps);

		/*
		 * We only need to intercept the error when moving to lmem.
		 * When moving to system, TTM or shmem will provide us with
		 * cleared pages.
		 */
		if (!IS_ERR(fence) && !i915_ttm_gtt_binds_lmem(dst_mem) &&
		    !I915_SELFTEST_ONLY(fail_gpu_migration ||
					fail_work_allocation))
			goto out;
	}

	/* If we've scheduled gpu migration. Try to arm error intercept. */
	if (!IS_ERR(fence)) {
		struct dma_fence *dep = fence;

		if (!I915_SELFTEST_ONLY(fail_work_allocation))
			copy_work = kzalloc(sizeof(*copy_work), GFP_KERNEL);

		if (copy_work) {
			copy_work->i915 = i915;
			copy_work->memcpy_allowed = memcpy_allowed;
			copy_work->obj = i915_gem_object_get(obj);
			arg = &copy_work->arg;
			if (memcpy_allowed)
				i915_ttm_memcpy_init(arg, bo, clear, dst_mem,
						     dst_ttm, dst_rsgt);

			fence = i915_ttm_memcpy_work_arm(copy_work, dep);
		} else {
			dma_fence_wait(dep, false);
			fence = ERR_PTR(I915_SELFTEST_ONLY(fail_gpu_migration) ?
					-EINVAL : fence->error);
		}
		dma_fence_put(dep);

		if (!IS_ERR(fence))
			goto out;
	} else {
		int err = PTR_ERR(fence);

		if (err == -EINTR || err == -ERESTARTSYS || err == -EAGAIN)
			return fence;

		if (move_deps) {
			err = i915_deps_sync(move_deps, ctx);
			if (err)
				return ERR_PTR(err);
		}
	}

	/* Error intercept failed or no accelerated migration to start with */

	if (memcpy_allowed) {
		if (!copy_work)
			i915_ttm_memcpy_init(arg, bo, clear, dst_mem, dst_ttm,
					     dst_rsgt);
		i915_ttm_move_memcpy(arg);
		i915_ttm_memcpy_release(arg);
	}
	if (copy_work)
		i915_gem_object_put(copy_work->obj);
	kfree(copy_work);

	return memcpy_allowed ? NULL : ERR_PTR(-EIO);
out:
	if (!fence && copy_work) {
		i915_ttm_memcpy_release(arg);
		i915_gem_object_put(copy_work->obj);
		kfree(copy_work);
	}

	return fence;
}

/**
 * i915_ttm_move - The TTM move callback used by i915.
 * @bo: The buffer object.
 * @evict: Whether this is an eviction.
 * @dst_mem: The destination ttm resource.
 * @hop: If we need multihop, what temporary memory type to move to.
 *
 * Return: 0 if successful, negative error code otherwise.
 */
int i915_ttm_move(struct ttm_buffer_object *bo, bool evict,
		  struct ttm_operation_ctx *ctx,
		  struct ttm_resource *dst_mem,
		  struct ttm_place *hop)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	struct ttm_resource_manager *dst_man =
		ttm_manager_type(bo->bdev, dst_mem->mem_type);
	struct dma_fence *migration_fence = NULL;
	struct ttm_tt *ttm = bo->ttm;
	struct i915_refct_sgt *dst_rsgt;
	bool clear;
	int ret;

	if (GEM_WARN_ON(i915_ttm_is_ghost_object(bo))) {
		ttm_bo_move_null(bo, dst_mem);
		return 0;
	}

	if (!bo->resource) {
		if (dst_mem->mem_type != TTM_PL_SYSTEM) {
			hop->mem_type = TTM_PL_SYSTEM;
			hop->flags = TTM_PL_FLAG_TEMPORARY;
			return -EMULTIHOP;
		}

		/*
		 * This is only reached when first creating the object, or if
		 * the object was purged or swapped out (pipeline-gutting). For
		 * the former we can safely skip all of the below since we are
		 * only using a dummy SYSTEM placement here. And with the latter
		 * we will always re-enter here with bo->resource set correctly
		 * (as per the above), since this is part of a multi-hop
		 * sequence, where at the end we can do the move for real.
		 *
		 * The special case here is when the dst_mem is TTM_PL_SYSTEM,
		 * which doens't require any kind of move, so it should be safe
		 * to skip all the below and call ttm_bo_move_null() here, where
		 * the caller in __i915_ttm_get_pages() will take care of the
		 * rest, since we should have a valid ttm_tt.
		 */
		ttm_bo_move_null(bo, dst_mem);
		return 0;
	}

	ret = i915_ttm_move_notify(bo);
	if (ret)
		return ret;

	if (obj->mm.madv != I915_MADV_WILLNEED) {
		i915_ttm_purge(obj);
		ttm_resource_free(bo, &dst_mem);
		return 0;
	}

	/* Populate ttm with pages if needed. Typically system memory. */
	if (ttm && (dst_man->use_tt || (ttm->page_flags & TTM_TT_FLAG_SWAPPED))) {
		ret = ttm_tt_populate(bo->bdev, ttm, ctx);
		if (ret)
			return ret;
	}

	dst_rsgt = i915_ttm_resource_get_st(obj, dst_mem);
	if (IS_ERR(dst_rsgt))
		return PTR_ERR(dst_rsgt);

	clear = !i915_ttm_cpu_maps_iomem(bo->resource) && (!ttm || !ttm_tt_is_populated(ttm));
	if (!(clear && ttm && !(ttm->page_flags & TTM_TT_FLAG_ZERO_ALLOC))) {
		struct i915_deps deps;

		i915_deps_init(&deps, GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN);
		ret = i915_deps_add_resv(&deps, bo->base.resv, ctx);
		if (ret) {
			i915_refct_sgt_put(dst_rsgt);
			return ret;
		}

		migration_fence = __i915_ttm_move(bo, ctx, clear, dst_mem, ttm,
						  dst_rsgt, true, &deps);
		i915_deps_fini(&deps);
	}

	/* We can possibly get an -ERESTARTSYS here */
	if (IS_ERR(migration_fence)) {
		i915_refct_sgt_put(dst_rsgt);
		return PTR_ERR(migration_fence);
	}

	if (migration_fence) {
		if (I915_SELFTEST_ONLY(evict && fail_gpu_migration))
			ret = -EIO; /* never feed non-migrate fences into ttm */
		else
			ret = ttm_bo_move_accel_cleanup(bo, migration_fence, evict,
							true, dst_mem);
		if (ret) {
			dma_fence_wait(migration_fence, false);
			ttm_bo_move_sync_cleanup(bo, dst_mem);
		}
		dma_fence_put(migration_fence);
	} else {
		ttm_bo_move_sync_cleanup(bo, dst_mem);
	}

	i915_ttm_adjust_domains_after_move(obj);
	i915_ttm_free_cached_io_rsgt(obj);

	if (i915_ttm_gtt_binds_lmem(dst_mem) || i915_ttm_cpu_maps_iomem(dst_mem)) {
		obj->ttm.cached_io_rsgt = dst_rsgt;
		obj->ttm.get_io_page.sg_pos = dst_rsgt->table.sgl;
		obj->ttm.get_io_page.sg_idx = 0;
	} else {
		i915_refct_sgt_put(dst_rsgt);
	}

	i915_ttm_adjust_lru(obj);
	i915_ttm_adjust_gem_after_move(obj);
	return 0;
}

/**
 * i915_gem_obj_copy_ttm - Copy the contents of one ttm-based gem object to
 * another
 * @dst: The destination object
 * @src: The source object
 * @allow_accel: Allow using the blitter. Otherwise TTM memcpy is used.
 * @intr: Whether to perform waits interruptible:
 *
 * Note: The caller is responsible for assuring that the underlying
 * TTM objects are populated if needed and locked.
 *
 * Return: Zero on success. Negative error code on error. If @intr == true,
 * then it may return -ERESTARTSYS or -EINTR.
 */
int i915_gem_obj_copy_ttm(struct drm_i915_gem_object *dst,
			  struct drm_i915_gem_object *src,
			  bool allow_accel, bool intr)
{
	struct ttm_buffer_object *dst_bo = i915_gem_to_ttm(dst);
	struct ttm_buffer_object *src_bo = i915_gem_to_ttm(src);
	struct ttm_operation_ctx ctx = {
		.interruptible = intr,
	};
	struct i915_refct_sgt *dst_rsgt;
	struct dma_fence *copy_fence;
	struct i915_deps deps;
	int ret;

	assert_object_held(dst);
	assert_object_held(src);
	i915_deps_init(&deps, GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN);

	ret = dma_resv_reserve_fences(src_bo->base.resv, 1);
	if (ret)
		return ret;

	ret = dma_resv_reserve_fences(dst_bo->base.resv, 1);
	if (ret)
		return ret;

	ret = i915_deps_add_resv(&deps, dst_bo->base.resv, &ctx);
	if (ret)
		return ret;

	ret = i915_deps_add_resv(&deps, src_bo->base.resv, &ctx);
	if (ret)
		return ret;

	dst_rsgt = i915_ttm_resource_get_st(dst, dst_bo->resource);
	copy_fence = __i915_ttm_move(src_bo, &ctx, false, dst_bo->resource,
				     dst_bo->ttm, dst_rsgt, allow_accel,
				     &deps);

	i915_deps_fini(&deps);
	i915_refct_sgt_put(dst_rsgt);
	if (IS_ERR_OR_NULL(copy_fence))
		return PTR_ERR_OR_ZERO(copy_fence);

	dma_resv_add_fence(dst_bo->base.resv, copy_fence, DMA_RESV_USAGE_WRITE);
	dma_resv_add_fence(src_bo->base.resv, copy_fence, DMA_RESV_USAGE_READ);
	dma_fence_put(copy_fence);

	return 0;
}
