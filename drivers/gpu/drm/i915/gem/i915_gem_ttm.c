// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <linux/shmem_fs.h>

#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/drm_buddy.h>

#include "i915_drv.h"
#include "i915_ttm_buddy_manager.h"
#include "intel_memory_region.h"
#include "intel_region_ttm.h"

#include "gem/i915_gem_mman.h"
#include "gem/i915_gem_object.h"
#include "gem/i915_gem_region.h"
#include "gem/i915_gem_ttm.h"
#include "gem/i915_gem_ttm_move.h"
#include "gem/i915_gem_ttm_pm.h"
#include "gt/intel_gpu_commands.h"

#define I915_TTM_PRIO_PURGE     0
#define I915_TTM_PRIO_NO_PAGES  1
#define I915_TTM_PRIO_HAS_PAGES 2
#define I915_TTM_PRIO_NEEDS_CPU_ACCESS 3

/*
 * Size of struct ttm_place vector in on-stack struct ttm_placement allocs
 */
#define I915_TTM_MAX_PLACEMENTS INTEL_REGION_UNKNOWN

/**
 * struct i915_ttm_tt - TTM page vector with additional private information
 * @ttm: The base TTM page vector.
 * @dev: The struct device used for dma mapping and unmapping.
 * @cached_rsgt: The cached scatter-gather table.
 * @is_shmem: Set if using shmem.
 * @filp: The shmem file, if using shmem backend.
 *
 * Note that DMA may be going on right up to the point where the page-
 * vector is unpopulated in delayed destroy. Hence keep the
 * scatter-gather table mapped and cached up to that point. This is
 * different from the cached gem object io scatter-gather table which
 * doesn't have an associated dma mapping.
 */
struct i915_ttm_tt {
	struct ttm_tt ttm;
	struct device *dev;
	struct i915_refct_sgt cached_rsgt;

	bool is_shmem;
	struct file *filp;
};

static const struct ttm_place sys_placement_flags = {
	.fpfn = 0,
	.lpfn = 0,
	.mem_type = I915_PL_SYSTEM,
	.flags = 0,
};

static struct ttm_placement i915_sys_placement = {
	.num_placement = 1,
	.placement = &sys_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &sys_placement_flags,
};

/**
 * i915_ttm_sys_placement - Return the struct ttm_placement to be
 * used for an object in system memory.
 *
 * Rather than making the struct extern, use this
 * function.
 *
 * Return: A pointer to a static variable for sys placement.
 */
struct ttm_placement *i915_ttm_sys_placement(void)
{
	return &i915_sys_placement;
}

static int i915_ttm_err_to_gem(int err)
{
	/* Fastpath */
	if (likely(!err))
		return 0;

	switch (err) {
	case -EBUSY:
		/*
		 * TTM likes to convert -EDEADLK to -EBUSY, and wants us to
		 * restart the operation, since we don't record the contending
		 * lock. We use -EAGAIN to restart.
		 */
		return -EAGAIN;
	case -ENOSPC:
		/*
		 * Memory type / region is full, and we can't evict.
		 * Except possibly system, that returns -ENOMEM;
		 */
		return -ENXIO;
	default:
		break;
	}

	return err;
}

static enum ttm_caching
i915_ttm_select_tt_caching(const struct drm_i915_gem_object *obj)
{
	/*
	 * Objects only allowed in system get cached cpu-mappings, or when
	 * evicting lmem-only buffers to system for swapping. Other objects get
	 * WC mapping for now. Even if in system.
	 */
	if (obj->mm.n_placements <= 1)
		return ttm_cached;

	return ttm_write_combined;
}

static void
i915_ttm_place_from_region(const struct intel_memory_region *mr,
			   struct ttm_place *place,
			   resource_size_t offset,
			   resource_size_t size,
			   unsigned int flags)
{
	memset(place, 0, sizeof(*place));
	place->mem_type = intel_region_to_ttm_type(mr);

	if (mr->type == INTEL_MEMORY_SYSTEM)
		return;

	if (flags & I915_BO_ALLOC_CONTIGUOUS)
		place->flags |= TTM_PL_FLAG_CONTIGUOUS;
	if (offset != I915_BO_INVALID_OFFSET) {
		place->fpfn = offset >> PAGE_SHIFT;
		place->lpfn = place->fpfn + (size >> PAGE_SHIFT);
	} else if (mr->io_size && mr->io_size < mr->total) {
		if (flags & I915_BO_ALLOC_GPU_ONLY) {
			place->flags |= TTM_PL_FLAG_TOPDOWN;
		} else {
			place->fpfn = 0;
			place->lpfn = mr->io_size >> PAGE_SHIFT;
		}
	}
}

static void
i915_ttm_placement_from_obj(const struct drm_i915_gem_object *obj,
			    struct ttm_place *requested,
			    struct ttm_place *busy,
			    struct ttm_placement *placement)
{
	unsigned int num_allowed = obj->mm.n_placements;
	unsigned int flags = obj->flags;
	unsigned int i;

	placement->num_placement = 1;
	i915_ttm_place_from_region(num_allowed ? obj->mm.placements[0] :
				   obj->mm.region, requested, obj->bo_offset,
				   obj->base.size, flags);

	/* Cache this on object? */
	placement->num_busy_placement = num_allowed;
	for (i = 0; i < placement->num_busy_placement; ++i)
		i915_ttm_place_from_region(obj->mm.placements[i], busy + i,
					   obj->bo_offset, obj->base.size, flags);

	if (num_allowed == 0) {
		*busy = *requested;
		placement->num_busy_placement = 1;
	}

	placement->placement = requested;
	placement->busy_placement = busy;
}

static int i915_ttm_tt_shmem_populate(struct ttm_device *bdev,
				      struct ttm_tt *ttm,
				      struct ttm_operation_ctx *ctx)
{
	struct drm_i915_private *i915 = container_of(bdev, typeof(*i915), bdev);
	struct intel_memory_region *mr = i915->mm.regions[INTEL_MEMORY_SYSTEM];
	struct i915_ttm_tt *i915_tt = container_of(ttm, typeof(*i915_tt), ttm);
	const unsigned int max_segment = i915_sg_segment_size();
	const size_t size = (size_t)ttm->num_pages << PAGE_SHIFT;
	struct file *filp = i915_tt->filp;
	struct sgt_iter sgt_iter;
	struct sg_table *st;
	struct page *page;
	unsigned long i;
	int err;

	if (!filp) {
		struct address_space *mapping;
		gfp_t mask;

		filp = shmem_file_setup("i915-shmem-tt", size, VM_NORESERVE);
		if (IS_ERR(filp))
			return PTR_ERR(filp);

		mask = GFP_HIGHUSER | __GFP_RECLAIMABLE;

		mapping = filp->f_mapping;
		mapping_set_gfp_mask(mapping, mask);
		GEM_BUG_ON(!(mapping_gfp_mask(mapping) & __GFP_RECLAIM));

		i915_tt->filp = filp;
	}

	st = &i915_tt->cached_rsgt.table;
	err = shmem_sg_alloc_table(i915, st, size, mr, filp->f_mapping,
				   max_segment);
	if (err)
		return err;

	err = dma_map_sgtable(i915_tt->dev, st, DMA_BIDIRECTIONAL,
			      DMA_ATTR_SKIP_CPU_SYNC);
	if (err)
		goto err_free_st;

	i = 0;
	for_each_sgt_page(page, sgt_iter, st)
		ttm->pages[i++] = page;

	if (ttm->page_flags & TTM_TT_FLAG_SWAPPED)
		ttm->page_flags &= ~TTM_TT_FLAG_SWAPPED;

	return 0;

err_free_st:
	shmem_sg_free_table(st, filp->f_mapping, false, false);

	return err;
}

static void i915_ttm_tt_shmem_unpopulate(struct ttm_tt *ttm)
{
	struct i915_ttm_tt *i915_tt = container_of(ttm, typeof(*i915_tt), ttm);
	bool backup = ttm->page_flags & TTM_TT_FLAG_SWAPPED;
	struct sg_table *st = &i915_tt->cached_rsgt.table;

	shmem_sg_free_table(st, file_inode(i915_tt->filp)->i_mapping,
			    backup, backup);
}

static void i915_ttm_tt_release(struct kref *ref)
{
	struct i915_ttm_tt *i915_tt =
		container_of(ref, typeof(*i915_tt), cached_rsgt.kref);
	struct sg_table *st = &i915_tt->cached_rsgt.table;

	GEM_WARN_ON(st->sgl);

	kfree(i915_tt);
}

static const struct i915_refct_sgt_ops tt_rsgt_ops = {
	.release = i915_ttm_tt_release
};

static struct ttm_tt *i915_ttm_tt_create(struct ttm_buffer_object *bo,
					 uint32_t page_flags)
{
	struct drm_i915_private *i915 = container_of(bo->bdev, typeof(*i915),
						     bdev);
	struct ttm_resource_manager *man =
		ttm_manager_type(bo->bdev, bo->resource->mem_type);
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	unsigned long ccs_pages = 0;
	enum ttm_caching caching;
	struct i915_ttm_tt *i915_tt;
	int ret;

	if (!obj)
		return NULL;

	i915_tt = kzalloc(sizeof(*i915_tt), GFP_KERNEL);
	if (!i915_tt)
		return NULL;

	if (obj->flags & I915_BO_ALLOC_CPU_CLEAR &&
	    man->use_tt)
		page_flags |= TTM_TT_FLAG_ZERO_ALLOC;

	caching = i915_ttm_select_tt_caching(obj);
	if (i915_gem_object_is_shrinkable(obj) && caching == ttm_cached) {
		page_flags |= TTM_TT_FLAG_EXTERNAL |
			      TTM_TT_FLAG_EXTERNAL_MAPPABLE;
		i915_tt->is_shmem = true;
	}

	if (i915_gem_object_needs_ccs_pages(obj))
		ccs_pages = DIV_ROUND_UP(DIV_ROUND_UP(bo->base.size,
						      NUM_BYTES_PER_CCS_BYTE),
					 PAGE_SIZE);

	ret = ttm_tt_init(&i915_tt->ttm, bo, page_flags, caching, ccs_pages);
	if (ret)
		goto err_free;

	__i915_refct_sgt_init(&i915_tt->cached_rsgt, bo->base.size,
			      &tt_rsgt_ops);

	i915_tt->dev = obj->base.dev->dev;

	return &i915_tt->ttm;

err_free:
	kfree(i915_tt);
	return NULL;
}

static int i915_ttm_tt_populate(struct ttm_device *bdev,
				struct ttm_tt *ttm,
				struct ttm_operation_ctx *ctx)
{
	struct i915_ttm_tt *i915_tt = container_of(ttm, typeof(*i915_tt), ttm);

	if (i915_tt->is_shmem)
		return i915_ttm_tt_shmem_populate(bdev, ttm, ctx);

	return ttm_pool_alloc(&bdev->pool, ttm, ctx);
}

static void i915_ttm_tt_unpopulate(struct ttm_device *bdev, struct ttm_tt *ttm)
{
	struct i915_ttm_tt *i915_tt = container_of(ttm, typeof(*i915_tt), ttm);
	struct sg_table *st = &i915_tt->cached_rsgt.table;

	if (st->sgl)
		dma_unmap_sgtable(i915_tt->dev, st, DMA_BIDIRECTIONAL, 0);

	if (i915_tt->is_shmem) {
		i915_ttm_tt_shmem_unpopulate(ttm);
	} else {
		sg_free_table(st);
		ttm_pool_free(&bdev->pool, ttm);
	}
}

static void i915_ttm_tt_destroy(struct ttm_device *bdev, struct ttm_tt *ttm)
{
	struct i915_ttm_tt *i915_tt = container_of(ttm, typeof(*i915_tt), ttm);

	if (i915_tt->filp)
		fput(i915_tt->filp);

	ttm_tt_fini(ttm);
	i915_refct_sgt_put(&i915_tt->cached_rsgt);
}

static bool i915_ttm_eviction_valuable(struct ttm_buffer_object *bo,
				       const struct ttm_place *place)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	struct ttm_resource *res = bo->resource;

	if (!obj)
		return false;

	/*
	 * EXTERNAL objects should never be swapped out by TTM, instead we need
	 * to handle that ourselves. TTM will already skip such objects for us,
	 * but we would like to avoid grabbing locks for no good reason.
	 */
	if (bo->ttm && bo->ttm->page_flags & TTM_TT_FLAG_EXTERNAL)
		return false;

	/* Will do for now. Our pinned objects are still on TTM's LRU lists */
	if (!i915_gem_object_evictable(obj))
		return false;

	switch (res->mem_type) {
	case I915_PL_LMEM0: {
		struct ttm_resource_manager *man =
			ttm_manager_type(bo->bdev, res->mem_type);
		struct i915_ttm_buddy_resource *bman_res =
			to_ttm_buddy_resource(res);
		struct drm_buddy *mm = bman_res->mm;
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

		/* Real range allocation */
		list_for_each_entry(block, &bman_res->blocks, link) {
			unsigned long fpfn =
				drm_buddy_block_offset(block) >> PAGE_SHIFT;
			unsigned long lpfn = fpfn +
				(drm_buddy_block_size(mm, block) >> PAGE_SHIFT);

			if (place->fpfn < lpfn && place->lpfn > fpfn)
				return true;
		}
		return false;
	} default:
		break;
	}

	return true;
}

static void i915_ttm_evict_flags(struct ttm_buffer_object *bo,
				 struct ttm_placement *placement)
{
	*placement = i915_sys_placement;
}

/**
 * i915_ttm_free_cached_io_rsgt - Free object cached LMEM information
 * @obj: The GEM object
 * This function frees any LMEM-related information that is cached on
 * the object. For example the radix tree for fast page lookup and the
 * cached refcounted sg-table
 */
void i915_ttm_free_cached_io_rsgt(struct drm_i915_gem_object *obj)
{
	struct radix_tree_iter iter;
	void __rcu **slot;

	if (!obj->ttm.cached_io_rsgt)
		return;

	rcu_read_lock();
	radix_tree_for_each_slot(slot, &obj->ttm.get_io_page.radix, &iter, 0)
		radix_tree_delete(&obj->ttm.get_io_page.radix, iter.index);
	rcu_read_unlock();

	i915_refct_sgt_put(obj->ttm.cached_io_rsgt);
	obj->ttm.cached_io_rsgt = NULL;
}

/**
 * i915_ttm_purge - Clear an object of its memory
 * @obj: The object
 *
 * This function is called to clear an object of it's memory when it is
 * marked as not needed anymore.
 *
 * Return: 0 on success, negative error code on failure.
 */
int i915_ttm_purge(struct drm_i915_gem_object *obj)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	struct i915_ttm_tt *i915_tt =
		container_of(bo->ttm, typeof(*i915_tt), ttm);
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
	};
	struct ttm_placement place = {};
	int ret;

	if (obj->mm.madv == __I915_MADV_PURGED)
		return 0;

	ret = ttm_bo_validate(bo, &place, &ctx);
	if (ret)
		return ret;

	if (bo->ttm && i915_tt->filp) {
		/*
		 * The below fput(which eventually calls shmem_truncate) might
		 * be delayed by worker, so when directly called to purge the
		 * pages(like by the shrinker) we should try to be more
		 * aggressive and release the pages immediately.
		 */
		shmem_truncate_range(file_inode(i915_tt->filp),
				     0, (loff_t)-1);
		fput(fetch_and_zero(&i915_tt->filp));
	}

	obj->write_domain = 0;
	obj->read_domains = 0;
	i915_ttm_adjust_gem_after_move(obj);
	i915_ttm_free_cached_io_rsgt(obj);
	obj->mm.madv = __I915_MADV_PURGED;

	return 0;
}

static int i915_ttm_shrink(struct drm_i915_gem_object *obj, unsigned int flags)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	struct i915_ttm_tt *i915_tt =
		container_of(bo->ttm, typeof(*i915_tt), ttm);
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = flags & I915_GEM_OBJECT_SHRINK_NO_GPU_WAIT,
	};
	struct ttm_placement place = {};
	int ret;

	if (!bo->ttm || bo->resource->mem_type != TTM_PL_SYSTEM)
		return 0;

	GEM_BUG_ON(!i915_tt->is_shmem);

	if (!i915_tt->filp)
		return 0;

	ret = ttm_bo_wait_ctx(bo, &ctx);
	if (ret)
		return ret;

	switch (obj->mm.madv) {
	case I915_MADV_DONTNEED:
		return i915_ttm_purge(obj);
	case __I915_MADV_PURGED:
		return 0;
	}

	if (bo->ttm->page_flags & TTM_TT_FLAG_SWAPPED)
		return 0;

	bo->ttm->page_flags |= TTM_TT_FLAG_SWAPPED;
	ret = ttm_bo_validate(bo, &place, &ctx);
	if (ret) {
		bo->ttm->page_flags &= ~TTM_TT_FLAG_SWAPPED;
		return ret;
	}

	if (flags & I915_GEM_OBJECT_SHRINK_WRITEBACK)
		__shmem_writeback(obj->base.size, i915_tt->filp->f_mapping);

	return 0;
}

static void i915_ttm_delete_mem_notify(struct ttm_buffer_object *bo)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);

	if (likely(obj)) {
		__i915_gem_object_pages_fini(obj);
		i915_ttm_free_cached_io_rsgt(obj);
	}
}

static struct i915_refct_sgt *i915_ttm_tt_get_st(struct ttm_tt *ttm)
{
	struct i915_ttm_tt *i915_tt = container_of(ttm, typeof(*i915_tt), ttm);
	struct sg_table *st;
	int ret;

	if (i915_tt->cached_rsgt.table.sgl)
		return i915_refct_sgt_get(&i915_tt->cached_rsgt);

	st = &i915_tt->cached_rsgt.table;
	ret = sg_alloc_table_from_pages_segment(st,
			ttm->pages, ttm->num_pages,
			0, (unsigned long)ttm->num_pages << PAGE_SHIFT,
			i915_sg_segment_size(), GFP_KERNEL);
	if (ret) {
		st->sgl = NULL;
		return ERR_PTR(ret);
	}

	ret = dma_map_sgtable(i915_tt->dev, st, DMA_BIDIRECTIONAL, 0);
	if (ret) {
		sg_free_table(st);
		return ERR_PTR(ret);
	}

	return i915_refct_sgt_get(&i915_tt->cached_rsgt);
}

/**
 * i915_ttm_resource_get_st - Get a refcounted sg-table pointing to the
 * resource memory
 * @obj: The GEM object used for sg-table caching
 * @res: The struct ttm_resource for which an sg-table is requested.
 *
 * This function returns a refcounted sg-table representing the memory
 * pointed to by @res. If @res is the object's current resource it may also
 * cache the sg_table on the object or attempt to access an already cached
 * sg-table. The refcounted sg-table needs to be put when no-longer in use.
 *
 * Return: A valid pointer to a struct i915_refct_sgt or error pointer on
 * failure.
 */
struct i915_refct_sgt *
i915_ttm_resource_get_st(struct drm_i915_gem_object *obj,
			 struct ttm_resource *res)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	u32 page_alignment;

	if (!i915_ttm_gtt_binds_lmem(res))
		return i915_ttm_tt_get_st(bo->ttm);

	page_alignment = bo->page_alignment << PAGE_SHIFT;
	if (!page_alignment)
		page_alignment = obj->mm.region->min_page_size;

	/*
	 * If CPU mapping differs, we need to add the ttm_tt pages to
	 * the resulting st. Might make sense for GGTT.
	 */
	GEM_WARN_ON(!i915_ttm_cpu_maps_iomem(res));
	if (bo->resource == res) {
		if (!obj->ttm.cached_io_rsgt) {
			struct i915_refct_sgt *rsgt;

			rsgt = intel_region_ttm_resource_to_rsgt(obj->mm.region,
								 res,
								 page_alignment);
			if (IS_ERR(rsgt))
				return rsgt;

			obj->ttm.cached_io_rsgt = rsgt;
		}
		return i915_refct_sgt_get(obj->ttm.cached_io_rsgt);
	}

	return intel_region_ttm_resource_to_rsgt(obj->mm.region, res,
						 page_alignment);
}

static int i915_ttm_truncate(struct drm_i915_gem_object *obj)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	int err;

	WARN_ON_ONCE(obj->mm.madv == I915_MADV_WILLNEED);

	err = i915_ttm_move_notify(bo);
	if (err)
		return err;

	return i915_ttm_purge(obj);
}

static void i915_ttm_swap_notify(struct ttm_buffer_object *bo)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	int ret;

	if (!obj)
		return;

	ret = i915_ttm_move_notify(bo);
	GEM_WARN_ON(ret);
	GEM_WARN_ON(obj->ttm.cached_io_rsgt);
	if (!ret && obj->mm.madv != I915_MADV_WILLNEED)
		i915_ttm_purge(obj);
}

/**
 * i915_ttm_resource_mappable - Return true if the ttm resource is CPU
 * accessible.
 * @res: The TTM resource to check.
 *
 * This is interesting on small-BAR systems where we may encounter lmem objects
 * that can't be accessed via the CPU.
 */
bool i915_ttm_resource_mappable(struct ttm_resource *res)
{
	struct i915_ttm_buddy_resource *bman_res = to_ttm_buddy_resource(res);

	if (!i915_ttm_cpu_maps_iomem(res))
		return true;

	return bman_res->used_visible_size == bman_res->base.num_pages;
}

static int i915_ttm_io_mem_reserve(struct ttm_device *bdev, struct ttm_resource *mem)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(mem->bo);
	bool unknown_state;

	if (!obj)
		return -EINVAL;

	if (!kref_get_unless_zero(&obj->base.refcount))
		return -EINVAL;

	assert_object_held(obj);

	unknown_state = i915_gem_object_has_unknown_state(obj);
	i915_gem_object_put(obj);
	if (unknown_state)
		return -EINVAL;

	if (!i915_ttm_cpu_maps_iomem(mem))
		return 0;

	if (!i915_ttm_resource_mappable(mem))
		return -EINVAL;

	mem->bus.caching = ttm_write_combined;
	mem->bus.is_iomem = true;

	return 0;
}

static unsigned long i915_ttm_io_mem_pfn(struct ttm_buffer_object *bo,
					 unsigned long page_offset)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	struct scatterlist *sg;
	unsigned long base;
	unsigned int ofs;

	GEM_BUG_ON(!obj);
	GEM_WARN_ON(bo->ttm);

	base = obj->mm.region->iomap.base - obj->mm.region->region.start;
	sg = __i915_gem_object_get_sg(obj, &obj->ttm.get_io_page, page_offset, &ofs, true);

	return ((base + sg_dma_address(sg)) >> PAGE_SHIFT) + ofs;
}

/*
 * All callbacks need to take care not to downcast a struct ttm_buffer_object
 * without checking its subclass, since it might be a TTM ghost object.
 */
static struct ttm_device_funcs i915_ttm_bo_driver = {
	.ttm_tt_create = i915_ttm_tt_create,
	.ttm_tt_populate = i915_ttm_tt_populate,
	.ttm_tt_unpopulate = i915_ttm_tt_unpopulate,
	.ttm_tt_destroy = i915_ttm_tt_destroy,
	.eviction_valuable = i915_ttm_eviction_valuable,
	.evict_flags = i915_ttm_evict_flags,
	.move = i915_ttm_move,
	.swap_notify = i915_ttm_swap_notify,
	.delete_mem_notify = i915_ttm_delete_mem_notify,
	.io_mem_reserve = i915_ttm_io_mem_reserve,
	.io_mem_pfn = i915_ttm_io_mem_pfn,
};

/**
 * i915_ttm_driver - Return a pointer to the TTM device funcs
 *
 * Return: Pointer to statically allocated TTM device funcs.
 */
struct ttm_device_funcs *i915_ttm_driver(void)
{
	return &i915_ttm_bo_driver;
}

static int __i915_ttm_get_pages(struct drm_i915_gem_object *obj,
				struct ttm_placement *placement)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
	};
	int real_num_busy;
	int ret;

	/* First try only the requested placement. No eviction. */
	real_num_busy = fetch_and_zero(&placement->num_busy_placement);
	ret = ttm_bo_validate(bo, placement, &ctx);
	if (ret) {
		ret = i915_ttm_err_to_gem(ret);
		/*
		 * Anything that wants to restart the operation gets to
		 * do that.
		 */
		if (ret == -EDEADLK || ret == -EINTR || ret == -ERESTARTSYS ||
		    ret == -EAGAIN)
			return ret;

		/*
		 * If the initial attempt fails, allow all accepted placements,
		 * evicting if necessary.
		 */
		placement->num_busy_placement = real_num_busy;
		ret = ttm_bo_validate(bo, placement, &ctx);
		if (ret)
			return i915_ttm_err_to_gem(ret);
	}

	if (bo->ttm && !ttm_tt_is_populated(bo->ttm)) {
		ret = ttm_tt_populate(bo->bdev, bo->ttm, &ctx);
		if (ret)
			return ret;

		i915_ttm_adjust_domains_after_move(obj);
		i915_ttm_adjust_gem_after_move(obj);
	}

	if (!i915_gem_object_has_pages(obj)) {
		struct i915_refct_sgt *rsgt =
			i915_ttm_resource_get_st(obj, bo->resource);

		if (IS_ERR(rsgt))
			return PTR_ERR(rsgt);

		GEM_BUG_ON(obj->mm.rsgt);
		obj->mm.rsgt = rsgt;
		__i915_gem_object_set_pages(obj, &rsgt->table,
					    i915_sg_dma_sizes(rsgt->table.sgl));
	}

	GEM_BUG_ON(bo->ttm && ((obj->base.size >> PAGE_SHIFT) < bo->ttm->num_pages));
	i915_ttm_adjust_lru(obj);
	return ret;
}

static int i915_ttm_get_pages(struct drm_i915_gem_object *obj)
{
	struct ttm_place requested, busy[I915_TTM_MAX_PLACEMENTS];
	struct ttm_placement placement;

	GEM_BUG_ON(obj->mm.n_placements > I915_TTM_MAX_PLACEMENTS);

	/* Move to the requested placement. */
	i915_ttm_placement_from_obj(obj, &requested, busy, &placement);

	return __i915_ttm_get_pages(obj, &placement);
}

/**
 * DOC: Migration vs eviction
 *
 * GEM migration may not be the same as TTM migration / eviction. If
 * the TTM core decides to evict an object it may be evicted to a
 * TTM memory type that is not in the object's allowable GEM regions, or
 * in fact theoretically to a TTM memory type that doesn't correspond to
 * a GEM memory region. In that case the object's GEM region is not
 * updated, and the data is migrated back to the GEM region at
 * get_pages time. TTM may however set up CPU ptes to the object even
 * when it is evicted.
 * Gem forced migration using the i915_ttm_migrate() op, is allowed even
 * to regions that are not in the object's list of allowable placements.
 */
static int __i915_ttm_migrate(struct drm_i915_gem_object *obj,
			      struct intel_memory_region *mr,
			      unsigned int flags)
{
	struct ttm_place requested;
	struct ttm_placement placement;
	int ret;

	i915_ttm_place_from_region(mr, &requested, obj->bo_offset,
				   obj->base.size, flags);
	placement.num_placement = 1;
	placement.num_busy_placement = 1;
	placement.placement = &requested;
	placement.busy_placement = &requested;

	ret = __i915_ttm_get_pages(obj, &placement);
	if (ret)
		return ret;

	/*
	 * Reinitialize the region bindings. This is primarily
	 * required for objects where the new region is not in
	 * its allowable placements.
	 */
	if (obj->mm.region != mr) {
		i915_gem_object_release_memory_region(obj);
		i915_gem_object_init_memory_region(obj, mr);
	}

	return 0;
}

static int i915_ttm_migrate(struct drm_i915_gem_object *obj,
			    struct intel_memory_region *mr)
{
	return __i915_ttm_migrate(obj, mr, obj->flags);
}

static void i915_ttm_put_pages(struct drm_i915_gem_object *obj,
			       struct sg_table *st)
{
	/*
	 * We're currently not called from a shrinker, so put_pages()
	 * typically means the object is about to destroyed, or called
	 * from move_notify(). So just avoid doing much for now.
	 * If the object is not destroyed next, The TTM eviction logic
	 * and shrinkers will move it out if needed.
	 */

	if (obj->mm.rsgt)
		i915_refct_sgt_put(fetch_and_zero(&obj->mm.rsgt));
}

/**
 * i915_ttm_adjust_lru - Adjust an object's position on relevant LRU lists.
 * @obj: The object
 */
void i915_ttm_adjust_lru(struct drm_i915_gem_object *obj)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	struct i915_ttm_tt *i915_tt =
		container_of(bo->ttm, typeof(*i915_tt), ttm);
	bool shrinkable =
		bo->ttm && i915_tt->filp && ttm_tt_is_populated(bo->ttm);

	/*
	 * Don't manipulate the TTM LRUs while in TTM bo destruction.
	 * We're called through i915_ttm_delete_mem_notify().
	 */
	if (!kref_read(&bo->kref))
		return;

	/*
	 * We skip managing the shrinker LRU in set_pages() and just manage
	 * everything here. This does at least solve the issue with having
	 * temporary shmem mappings(like with evicted lmem) not being visible to
	 * the shrinker. Only our shmem objects are shrinkable, everything else
	 * we keep as unshrinkable.
	 *
	 * To make sure everything plays nice we keep an extra shrink pin in TTM
	 * if the underlying pages are not currently shrinkable. Once we release
	 * our pin, like when the pages are moved to shmem, the pages will then
	 * be added to the shrinker LRU, assuming the caller isn't also holding
	 * a pin.
	 *
	 * TODO: consider maybe also bumping the shrinker list here when we have
	 * already unpinned it, which should give us something more like an LRU.
	 *
	 * TODO: There is a small window of opportunity for this function to
	 * get called from eviction after we've dropped the last GEM refcount,
	 * but before the TTM deleted flag is set on the object. Avoid
	 * adjusting the shrinker list in such cases, since the object is
	 * not available to the shrinker anyway due to its zero refcount.
	 * To fix this properly we should move to a TTM shrinker LRU list for
	 * these objects.
	 */
	if (kref_get_unless_zero(&obj->base.refcount)) {
		if (shrinkable != obj->mm.ttm_shrinkable) {
			if (shrinkable) {
				if (obj->mm.madv == I915_MADV_WILLNEED)
					__i915_gem_object_make_shrinkable(obj);
				else
					__i915_gem_object_make_purgeable(obj);
			} else {
				i915_gem_object_make_unshrinkable(obj);
			}

			obj->mm.ttm_shrinkable = shrinkable;
		}
		i915_gem_object_put(obj);
	}

	/*
	 * Put on the correct LRU list depending on the MADV status
	 */
	spin_lock(&bo->bdev->lru_lock);
	if (shrinkable) {
		/* Try to keep shmem_tt from being considered for shrinking. */
		bo->priority = TTM_MAX_BO_PRIORITY - 1;
	} else if (obj->mm.madv != I915_MADV_WILLNEED) {
		bo->priority = I915_TTM_PRIO_PURGE;
	} else if (!i915_gem_object_has_pages(obj)) {
		bo->priority = I915_TTM_PRIO_NO_PAGES;
	} else {
		struct ttm_resource_manager *man =
			ttm_manager_type(bo->bdev, bo->resource->mem_type);

		/*
		 * If we need to place an LMEM resource which doesn't need CPU
		 * access then we should try not to victimize mappable objects
		 * first, since we likely end up stealing more of the mappable
		 * portion. And likewise when we try to find space for a mappble
		 * object, we know not to ever victimize objects that don't
		 * occupy any mappable pages.
		 */
		if (i915_ttm_cpu_maps_iomem(bo->resource) &&
		    i915_ttm_buddy_man_visible_size(man) < man->size &&
		    !(obj->flags & I915_BO_ALLOC_GPU_ONLY))
			bo->priority = I915_TTM_PRIO_NEEDS_CPU_ACCESS;
		else
			bo->priority = I915_TTM_PRIO_HAS_PAGES;
	}

	ttm_bo_move_to_lru_tail(bo);
	spin_unlock(&bo->bdev->lru_lock);
}

/*
 * TTM-backed gem object destruction requires some clarification.
 * Basically we have two possibilities here. We can either rely on the
 * i915 delayed destruction and put the TTM object when the object
 * is idle. This would be detected by TTM which would bypass the
 * TTM delayed destroy handling. The other approach is to put the TTM
 * object early and rely on the TTM destroyed handling, and then free
 * the leftover parts of the GEM object once TTM's destroyed list handling is
 * complete. For now, we rely on the latter for two reasons:
 * a) TTM can evict an object even when it's on the delayed destroy list,
 * which in theory allows for complete eviction.
 * b) There is work going on in TTM to allow freeing an object even when
 * it's not idle, and using the TTM destroyed list handling could help us
 * benefit from that.
 */
static void i915_ttm_delayed_free(struct drm_i915_gem_object *obj)
{
	GEM_BUG_ON(!obj->ttm.created);

	ttm_bo_put(i915_gem_to_ttm(obj));
}

static vm_fault_t vm_fault_ttm(struct vm_fault *vmf)
{
	struct vm_area_struct *area = vmf->vma;
	struct ttm_buffer_object *bo = area->vm_private_data;
	struct drm_device *dev = bo->base.dev;
	struct drm_i915_gem_object *obj;
	vm_fault_t ret;
	int idx;

	obj = i915_ttm_to_gem(bo);
	if (!obj)
		return VM_FAULT_SIGBUS;

	/* Sanity check that we allow writing into this object */
	if (unlikely(i915_gem_object_is_readonly(obj) &&
		     area->vm_flags & VM_WRITE))
		return VM_FAULT_SIGBUS;

	ret = ttm_bo_vm_reserve(bo, vmf);
	if (ret)
		return ret;

	if (obj->mm.madv != I915_MADV_WILLNEED) {
		dma_resv_unlock(bo->base.resv);
		return VM_FAULT_SIGBUS;
	}

	if (!i915_ttm_resource_mappable(bo->resource)) {
		int err = -ENODEV;
		int i;

		for (i = 0; i < obj->mm.n_placements; i++) {
			struct intel_memory_region *mr = obj->mm.placements[i];
			unsigned int flags;

			if (!mr->io_size && mr->type != INTEL_MEMORY_SYSTEM)
				continue;

			flags = obj->flags;
			flags &= ~I915_BO_ALLOC_GPU_ONLY;
			err = __i915_ttm_migrate(obj, mr, flags);
			if (!err)
				break;
		}

		if (err) {
			drm_dbg(dev, "Unable to make resource CPU accessible\n");
			dma_resv_unlock(bo->base.resv);
			return VM_FAULT_SIGBUS;
		}
	}

	if (drm_dev_enter(dev, &idx)) {
		ret = ttm_bo_vm_fault_reserved(vmf, vmf->vma->vm_page_prot,
					       TTM_BO_VM_NUM_PREFAULT);
		drm_dev_exit(idx);
	} else {
		ret = ttm_bo_vm_dummy_page(vmf, vmf->vma->vm_page_prot);
	}
	if (ret == VM_FAULT_RETRY && !(vmf->flags & FAULT_FLAG_RETRY_NOWAIT))
		return ret;

	i915_ttm_adjust_lru(obj);

	dma_resv_unlock(bo->base.resv);
	return ret;
}

static int
vm_access_ttm(struct vm_area_struct *area, unsigned long addr,
	      void *buf, int len, int write)
{
	struct drm_i915_gem_object *obj =
		i915_ttm_to_gem(area->vm_private_data);

	if (i915_gem_object_is_readonly(obj) && write)
		return -EACCES;

	return ttm_bo_vm_access(area, addr, buf, len, write);
}

static void ttm_vm_open(struct vm_area_struct *vma)
{
	struct drm_i915_gem_object *obj =
		i915_ttm_to_gem(vma->vm_private_data);

	GEM_BUG_ON(!obj);
	i915_gem_object_get(obj);
}

static void ttm_vm_close(struct vm_area_struct *vma)
{
	struct drm_i915_gem_object *obj =
		i915_ttm_to_gem(vma->vm_private_data);

	GEM_BUG_ON(!obj);
	i915_gem_object_put(obj);
}

static const struct vm_operations_struct vm_ops_ttm = {
	.fault = vm_fault_ttm,
	.access = vm_access_ttm,
	.open = ttm_vm_open,
	.close = ttm_vm_close,
};

static u64 i915_ttm_mmap_offset(struct drm_i915_gem_object *obj)
{
	/* The ttm_bo must be allocated with I915_BO_ALLOC_USER */
	GEM_BUG_ON(!drm_mm_node_allocated(&obj->base.vma_node.vm_node));

	return drm_vma_node_offset_addr(&obj->base.vma_node);
}

static void i915_ttm_unmap_virtual(struct drm_i915_gem_object *obj)
{
	ttm_bo_unmap_virtual(i915_gem_to_ttm(obj));
}

static const struct drm_i915_gem_object_ops i915_gem_ttm_obj_ops = {
	.name = "i915_gem_object_ttm",
	.flags = I915_GEM_OBJECT_IS_SHRINKABLE |
		 I915_GEM_OBJECT_SELF_MANAGED_SHRINK_LIST,

	.get_pages = i915_ttm_get_pages,
	.put_pages = i915_ttm_put_pages,
	.truncate = i915_ttm_truncate,
	.shrink = i915_ttm_shrink,

	.adjust_lru = i915_ttm_adjust_lru,
	.delayed_free = i915_ttm_delayed_free,
	.migrate = i915_ttm_migrate,

	.mmap_offset = i915_ttm_mmap_offset,
	.unmap_virtual = i915_ttm_unmap_virtual,
	.mmap_ops = &vm_ops_ttm,
};

void i915_ttm_bo_destroy(struct ttm_buffer_object *bo)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);

	i915_gem_object_release_memory_region(obj);
	mutex_destroy(&obj->ttm.get_io_page.lock);

	if (obj->ttm.created) {
		/*
		 * We freely manage the shrinker LRU outide of the mm.pages life
		 * cycle. As a result when destroying the object we should be
		 * extra paranoid and ensure we remove it from the LRU, before
		 * we free the object.
		 *
		 * Touching the ttm_shrinkable outside of the object lock here
		 * should be safe now that the last GEM object ref was dropped.
		 */
		if (obj->mm.ttm_shrinkable)
			i915_gem_object_make_unshrinkable(obj);

		i915_ttm_backup_free(obj);

		/* This releases all gem object bindings to the backend. */
		__i915_gem_free_object(obj);

		call_rcu(&obj->rcu, __i915_gem_free_object_rcu);
	} else {
		__i915_gem_object_fini(obj);
	}
}

/**
 * __i915_gem_ttm_object_init - Initialize a ttm-backed i915 gem object
 * @mem: The initial memory region for the object.
 * @obj: The gem object.
 * @size: Object size in bytes.
 * @flags: gem object flags.
 *
 * Return: 0 on success, negative error code on failure.
 */
int __i915_gem_ttm_object_init(struct intel_memory_region *mem,
			       struct drm_i915_gem_object *obj,
			       resource_size_t offset,
			       resource_size_t size,
			       resource_size_t page_size,
			       unsigned int flags)
{
	static struct lock_class_key lock_class;
	struct drm_i915_private *i915 = mem->i915;
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
	};
	enum ttm_bo_type bo_type;
	int ret;

	drm_gem_private_object_init(&i915->drm, &obj->base, size);
	i915_gem_object_init(obj, &i915_gem_ttm_obj_ops, &lock_class, flags);

	obj->bo_offset = offset;

	/* Don't put on a region list until we're either locked or fully initialized. */
	obj->mm.region = mem;
	INIT_LIST_HEAD(&obj->mm.region_link);

	INIT_RADIX_TREE(&obj->ttm.get_io_page.radix, GFP_KERNEL | __GFP_NOWARN);
	mutex_init(&obj->ttm.get_io_page.lock);
	bo_type = (obj->flags & I915_BO_ALLOC_USER) ? ttm_bo_type_device :
		ttm_bo_type_kernel;

	obj->base.vma_node.driver_private = i915_gem_to_ttm(obj);

	/* Forcing the page size is kernel internal only */
	GEM_BUG_ON(page_size && obj->mm.n_placements);

	/*
	 * Keep an extra shrink pin to prevent the object from being made
	 * shrinkable too early. If the ttm_tt is ever allocated in shmem, we
	 * drop the pin. The TTM backend manages the shrinker LRU itself,
	 * outside of the normal mm.pages life cycle.
	 */
	i915_gem_object_make_unshrinkable(obj);

	/*
	 * If this function fails, it will call the destructor, but
	 * our caller still owns the object. So no freeing in the
	 * destructor until obj->ttm.created is true.
	 * Similarly, in delayed_destroy, we can't call ttm_bo_put()
	 * until successful initialization.
	 */
	ret = ttm_bo_init_reserved(&i915->bdev, i915_gem_to_ttm(obj), size,
				   bo_type, &i915_sys_placement,
				   page_size >> PAGE_SHIFT,
				   &ctx, NULL, NULL, i915_ttm_bo_destroy);
	if (ret)
		return i915_ttm_err_to_gem(ret);

	obj->ttm.created = true;
	i915_gem_object_release_memory_region(obj);
	i915_gem_object_init_memory_region(obj, mem);
	i915_ttm_adjust_domains_after_move(obj);
	i915_ttm_adjust_gem_after_move(obj);
	i915_gem_object_unlock(obj);

	return 0;
}

static const struct intel_memory_region_ops ttm_system_region_ops = {
	.init_object = __i915_gem_ttm_object_init,
	.release = intel_region_ttm_fini,
};

struct intel_memory_region *
i915_gem_ttm_system_setup(struct drm_i915_private *i915,
			  u16 type, u16 instance)
{
	struct intel_memory_region *mr;

	mr = intel_memory_region_create(i915, 0,
					totalram_pages() << PAGE_SHIFT,
					PAGE_SIZE, 0, 0,
					type, instance,
					&ttm_system_region_ops);
	if (IS_ERR(mr))
		return mr;

	intel_memory_region_set_name(mr, "system-ttm");
	return mr;
}
