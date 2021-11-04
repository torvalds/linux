// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/ttm/ttm_bo_driver.h>

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
	unsigned int i;

	/*
	 * If object was moved to an allowable region, update the object
	 * region to consider it migrated. Note that if it's currently not
	 * in an allowable region, it's evicted and we don't update the
	 * object region.
	 */
	if (intel_region_to_ttm_type(obj->mm.region) != bo->resource->mem_type) {
		for (i = 0; i < obj->mm.n_placements; ++i) {
			struct intel_memory_region *mr = obj->mm.placements[i];

			if (intel_region_to_ttm_type(mr) == bo->resource->mem_type &&
			    mr != obj->mm.region) {
				i915_gem_object_release_memory_region(obj);
				i915_gem_object_init_memory_region(obj, mr);
				break;
			}
		}
	}

	obj->mem_flags &= ~(I915_BO_FLAG_STRUCT_PAGE | I915_BO_FLAG_IOMEM);

	obj->mem_flags |= i915_ttm_cpu_maps_iomem(bo->resource) ? I915_BO_FLAG_IOMEM :
		I915_BO_FLAG_STRUCT_PAGE;

	cache_level = i915_ttm_cache_level(to_i915(bo->base.dev), bo->resource,
					   bo->ttm);
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

	ret = i915_gem_object_unbind(obj, I915_GEM_OBJECT_UNBIND_ACTIVE);
	if (ret)
		return ret;

	ret = __i915_gem_object_put_pages(obj);
	if (ret)
		return ret;

	return 0;
}

static int i915_ttm_accel_move(struct ttm_buffer_object *bo,
			       bool clear,
			       struct ttm_resource *dst_mem,
			       struct ttm_tt *dst_ttm,
			       struct sg_table *dst_st)
{
	struct drm_i915_private *i915 = container_of(bo->bdev, typeof(*i915),
						     bdev);
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	struct i915_request *rq;
	struct ttm_tt *src_ttm = bo->ttm;
	enum i915_cache_level src_level, dst_level;
	int ret;

	if (!i915->gt.migrate.context || intel_gt_is_wedged(&i915->gt))
		return -EINVAL;

	dst_level = i915_ttm_cache_level(i915, dst_mem, dst_ttm);
	if (clear) {
		if (bo->type == ttm_bo_type_kernel)
			return -EINVAL;

		intel_engine_pm_get(i915->gt.migrate.context->engine);
		ret = intel_context_migrate_clear(i915->gt.migrate.context, NULL,
						  dst_st->sgl, dst_level,
						  i915_ttm_gtt_binds_lmem(dst_mem),
						  0, &rq);

		if (!ret && rq) {
			i915_request_wait(rq, 0, MAX_SCHEDULE_TIMEOUT);
			i915_request_put(rq);
		}
		intel_engine_pm_put(i915->gt.migrate.context->engine);
	} else {
		struct i915_refct_sgt *src_rsgt =
			i915_ttm_resource_get_st(obj, bo->resource);

		if (IS_ERR(src_rsgt))
			return PTR_ERR(src_rsgt);

		src_level = i915_ttm_cache_level(i915, bo->resource, src_ttm);
		intel_engine_pm_get(i915->gt.migrate.context->engine);
		ret = intel_context_migrate_copy(i915->gt.migrate.context,
						 NULL, src_rsgt->table.sgl,
						 src_level,
						 i915_ttm_gtt_binds_lmem(bo->resource),
						 dst_st->sgl, dst_level,
						 i915_ttm_gtt_binds_lmem(dst_mem),
						 &rq);
		i915_refct_sgt_put(src_rsgt);
		if (!ret && rq) {
			i915_request_wait(rq, 0, MAX_SCHEDULE_TIMEOUT);
			i915_request_put(rq);
		}
		intel_engine_pm_put(i915->gt.migrate.context->engine);
	}

	return ret;
}

/**
 * __i915_ttm_move - helper to perform TTM moves or clears.
 * @bo: The source buffer object.
 * @clear: Whether this is a clear operation.
 * @dst_mem: The destination ttm resource.
 * @dst_ttm: The destination ttm page vector.
 * @dst_rsgt: The destination refcounted sg-list.
 * @allow_accel: Whether to allow acceleration.
 */
void __i915_ttm_move(struct ttm_buffer_object *bo, bool clear,
		     struct ttm_resource *dst_mem,
		     struct ttm_tt *dst_ttm,
		     struct i915_refct_sgt *dst_rsgt,
		     bool allow_accel)
{
	int ret = -EINVAL;

	if (allow_accel)
		ret = i915_ttm_accel_move(bo, clear, dst_mem, dst_ttm,
					  &dst_rsgt->table);
	if (ret) {
		struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
		struct intel_memory_region *dst_reg, *src_reg;
		union {
			struct ttm_kmap_iter_tt tt;
			struct ttm_kmap_iter_iomap io;
		} _dst_iter, _src_iter;
		struct ttm_kmap_iter *dst_iter, *src_iter;

		dst_reg = i915_ttm_region(bo->bdev, dst_mem->mem_type);
		src_reg = i915_ttm_region(bo->bdev, bo->resource->mem_type);
		GEM_BUG_ON(!dst_reg || !src_reg);

		dst_iter = !i915_ttm_cpu_maps_iomem(dst_mem) ?
			ttm_kmap_iter_tt_init(&_dst_iter.tt, dst_ttm) :
			ttm_kmap_iter_iomap_init(&_dst_iter.io, &dst_reg->iomap,
						 &dst_rsgt->table,
						 dst_reg->region.start);

		src_iter = !i915_ttm_cpu_maps_iomem(bo->resource) ?
			ttm_kmap_iter_tt_init(&_src_iter.tt, bo->ttm) :
			ttm_kmap_iter_iomap_init(&_src_iter.io, &src_reg->iomap,
						 &obj->ttm.cached_io_rsgt->table,
						 src_reg->region.start);

		ttm_move_memcpy(clear, dst_mem->num_pages, dst_iter, src_iter);
	}
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
	struct ttm_tt *ttm = bo->ttm;
	struct i915_refct_sgt *dst_rsgt;
	bool clear;
	int ret;

	/* Sync for now. We could do the actual copy async. */
	ret = ttm_bo_wait_ctx(bo, ctx);
	if (ret)
		return ret;

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
	if (!(clear && ttm && !(ttm->page_flags & TTM_TT_FLAG_ZERO_ALLOC)))
		__i915_ttm_move(bo, clear, dst_mem, bo->ttm, dst_rsgt, true);

	ttm_bo_move_sync_cleanup(bo, dst_mem);
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
