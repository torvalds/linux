// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>

#include "i915_drv.h"
#include "intel_memory_region.h"
#include "intel_region_ttm.h"

#include "gem/i915_gem_object.h"
#include "gem/i915_gem_region.h"
#include "gem/i915_gem_ttm.h"
#include "gem/i915_gem_mman.h"

#define I915_PL_LMEM0 TTM_PL_PRIV
#define I915_PL_SYSTEM TTM_PL_SYSTEM
#define I915_PL_STOLEN TTM_PL_VRAM
#define I915_PL_GGTT TTM_PL_TT

#define I915_TTM_PRIO_PURGE     0
#define I915_TTM_PRIO_NO_PAGES  1
#define I915_TTM_PRIO_HAS_PAGES 2

/**
 * struct i915_ttm_tt - TTM page vector with additional private information
 * @ttm: The base TTM page vector.
 * @dev: The struct device used for dma mapping and unmapping.
 * @cached_st: The cached scatter-gather table.
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
	struct sg_table *cached_st;
};

static const struct ttm_place lmem0_sys_placement_flags[] = {
	{
		.fpfn = 0,
		.lpfn = 0,
		.mem_type = I915_PL_LMEM0,
		.flags = 0,
	}, {
		.fpfn = 0,
		.lpfn = 0,
		.mem_type = I915_PL_SYSTEM,
		.flags = 0,
	}
};

static struct ttm_placement i915_lmem0_placement = {
	.num_placement = 1,
	.placement = &lmem0_sys_placement_flags[0],
	.num_busy_placement = 1,
	.busy_placement = &lmem0_sys_placement_flags[0],
};

static struct ttm_placement i915_sys_placement = {
	.num_placement = 1,
	.placement = &lmem0_sys_placement_flags[1],
	.num_busy_placement = 1,
	.busy_placement = &lmem0_sys_placement_flags[1],
};

static void i915_ttm_adjust_lru(struct drm_i915_gem_object *obj);

static struct ttm_tt *i915_ttm_tt_create(struct ttm_buffer_object *bo,
					 uint32_t page_flags)
{
	struct ttm_resource_manager *man =
		ttm_manager_type(bo->bdev, bo->resource->mem_type);
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	struct i915_ttm_tt *i915_tt;
	int ret;

	i915_tt = kzalloc(sizeof(*i915_tt), GFP_KERNEL);
	if (!i915_tt)
		return NULL;

	if (obj->flags & I915_BO_ALLOC_CPU_CLEAR &&
	    man->use_tt)
		page_flags |= TTM_PAGE_FLAG_ZERO_ALLOC;

	ret = ttm_tt_init(&i915_tt->ttm, bo, page_flags, ttm_write_combined);
	if (ret) {
		kfree(i915_tt);
		return NULL;
	}

	i915_tt->dev = obj->base.dev->dev;

	return &i915_tt->ttm;
}

static void i915_ttm_tt_unpopulate(struct ttm_device *bdev, struct ttm_tt *ttm)
{
	struct i915_ttm_tt *i915_tt = container_of(ttm, typeof(*i915_tt), ttm);

	if (i915_tt->cached_st) {
		dma_unmap_sgtable(i915_tt->dev, i915_tt->cached_st,
				  DMA_BIDIRECTIONAL, 0);
		sg_free_table(i915_tt->cached_st);
		kfree(i915_tt->cached_st);
		i915_tt->cached_st = NULL;
	}
	ttm_pool_free(&bdev->pool, ttm);
}

static void i915_ttm_tt_destroy(struct ttm_device *bdev, struct ttm_tt *ttm)
{
	struct i915_ttm_tt *i915_tt = container_of(ttm, typeof(*i915_tt), ttm);

	kfree(i915_tt);
}

static bool i915_ttm_eviction_valuable(struct ttm_buffer_object *bo,
				       const struct ttm_place *place)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);

	/* Will do for now. Our pinned objects are still on TTM's LRU lists */
	if (!i915_gem_object_evictable(obj))
		return false;

	/* This isn't valid with a buddy allocator */
	return ttm_bo_eviction_valuable(bo, place);
}

static void i915_ttm_evict_flags(struct ttm_buffer_object *bo,
				 struct ttm_placement *placement)
{
	*placement = i915_sys_placement;
}

static int i915_ttm_move_notify(struct ttm_buffer_object *bo)
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

static void i915_ttm_free_cached_io_st(struct drm_i915_gem_object *obj)
{
	struct radix_tree_iter iter;
	void __rcu **slot;

	if (!obj->ttm.cached_io_st)
		return;

	rcu_read_lock();
	radix_tree_for_each_slot(slot, &obj->ttm.get_io_page.radix, &iter, 0)
		radix_tree_delete(&obj->ttm.get_io_page.radix, iter.index);
	rcu_read_unlock();

	sg_free_table(obj->ttm.cached_io_st);
	kfree(obj->ttm.cached_io_st);
	obj->ttm.cached_io_st = NULL;
}

static void i915_ttm_purge(struct drm_i915_gem_object *obj)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
	};
	struct ttm_placement place = {};
	int ret;

	if (obj->mm.madv == __I915_MADV_PURGED)
		return;

	/* TTM's purge interface. Note that we might be reentering. */
	ret = ttm_bo_validate(bo, &place, &ctx);

	if (!ret) {
		i915_ttm_free_cached_io_st(obj);
		obj->mm.madv = __I915_MADV_PURGED;
	}
}

static void i915_ttm_swap_notify(struct ttm_buffer_object *bo)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	int ret = i915_ttm_move_notify(bo);

	GEM_WARN_ON(ret);
	GEM_WARN_ON(obj->ttm.cached_io_st);
	if (!ret && obj->mm.madv != I915_MADV_WILLNEED)
		i915_ttm_purge(obj);
}

static void i915_ttm_delete_mem_notify(struct ttm_buffer_object *bo)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);

	if (likely(obj)) {
		/* This releases all gem object bindings to the backend. */
		__i915_gem_free_object(obj);
	}
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

static struct sg_table *i915_ttm_tt_get_st(struct ttm_tt *ttm)
{
	struct i915_ttm_tt *i915_tt = container_of(ttm, typeof(*i915_tt), ttm);
	struct scatterlist *sg;
	struct sg_table *st;
	int ret;

	if (i915_tt->cached_st)
		return i915_tt->cached_st;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return ERR_PTR(-ENOMEM);

	sg = __sg_alloc_table_from_pages
		(st, ttm->pages, ttm->num_pages, 0,
		 (unsigned long)ttm->num_pages << PAGE_SHIFT,
		 i915_sg_segment_size(), NULL, 0, GFP_KERNEL);
	if (IS_ERR(sg)) {
		kfree(st);
		return ERR_CAST(sg);
	}

	ret = dma_map_sgtable(i915_tt->dev, st, DMA_BIDIRECTIONAL, 0);
	if (ret) {
		sg_free_table(st);
		kfree(st);
		return ERR_PTR(ret);
	}

	i915_tt->cached_st = st;
	return st;
}

static struct sg_table *
i915_ttm_resource_get_st(struct drm_i915_gem_object *obj,
			 struct ttm_resource *res)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	struct ttm_resource_manager *man =
		ttm_manager_type(bo->bdev, res->mem_type);

	if (man->use_tt)
		return i915_ttm_tt_get_st(bo->ttm);

	return intel_region_ttm_node_to_st(obj->mm.region, res);
}

static int i915_ttm_move(struct ttm_buffer_object *bo, bool evict,
			 struct ttm_operation_ctx *ctx,
			 struct ttm_resource *dst_mem,
			 struct ttm_place *hop)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	struct ttm_resource_manager *dst_man =
		ttm_manager_type(bo->bdev, dst_mem->mem_type);
	struct ttm_resource_manager *src_man =
		ttm_manager_type(bo->bdev, bo->resource->mem_type);
	struct intel_memory_region *dst_reg, *src_reg;
	union {
		struct ttm_kmap_iter_tt tt;
		struct ttm_kmap_iter_iomap io;
	} _dst_iter, _src_iter;
	struct ttm_kmap_iter *dst_iter, *src_iter;
	struct sg_table *dst_st;
	int ret;

	dst_reg = i915_ttm_region(bo->bdev, dst_mem->mem_type);
	src_reg = i915_ttm_region(bo->bdev, bo->resource->mem_type);
	GEM_BUG_ON(!dst_reg || !src_reg);

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
	if (bo->ttm && (dst_man->use_tt ||
			(bo->ttm->page_flags & TTM_PAGE_FLAG_SWAPPED))) {
		ret = ttm_tt_populate(bo->bdev, bo->ttm, ctx);
		if (ret)
			return ret;
	}

	dst_st = i915_ttm_resource_get_st(obj, dst_mem);
	if (IS_ERR(dst_st))
		return PTR_ERR(dst_st);

	/* If we start mapping GGTT, we can no longer use man::use_tt here. */
	dst_iter = dst_man->use_tt ?
		ttm_kmap_iter_tt_init(&_dst_iter.tt, bo->ttm) :
		ttm_kmap_iter_iomap_init(&_dst_iter.io, &dst_reg->iomap,
					 dst_st, dst_reg->region.start);

	src_iter = src_man->use_tt ?
		ttm_kmap_iter_tt_init(&_src_iter.tt, bo->ttm) :
		ttm_kmap_iter_iomap_init(&_src_iter.io, &src_reg->iomap,
					 obj->ttm.cached_io_st,
					 src_reg->region.start);

	ttm_move_memcpy(bo, dst_mem->num_pages, dst_iter, src_iter);
	ttm_bo_move_sync_cleanup(bo, dst_mem);
	i915_ttm_free_cached_io_st(obj);

	if (!dst_man->use_tt) {
		obj->ttm.cached_io_st = dst_st;
		obj->ttm.get_io_page.sg_pos = dst_st->sgl;
		obj->ttm.get_io_page.sg_idx = 0;
	}

	return 0;
}

static int i915_ttm_io_mem_reserve(struct ttm_device *bdev, struct ttm_resource *mem)
{
	if (mem->mem_type < I915_PL_LMEM0)
		return 0;

	mem->bus.caching = ttm_write_combined;
	mem->bus.is_iomem = true;

	return 0;
}

static unsigned long i915_ttm_io_mem_pfn(struct ttm_buffer_object *bo,
					 unsigned long page_offset)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);
	unsigned long base = obj->mm.region->iomap.base - obj->mm.region->region.start;
	struct scatterlist *sg;
	unsigned int ofs;

	GEM_WARN_ON(bo->ttm);

	sg = __i915_gem_object_get_sg(obj, &obj->ttm.get_io_page, page_offset, &ofs, true, true);

	return ((base + sg_dma_address(sg)) >> PAGE_SHIFT) + ofs;
}

static struct ttm_device_funcs i915_ttm_bo_driver = {
	.ttm_tt_create = i915_ttm_tt_create,
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

static int i915_ttm_get_pages(struct drm_i915_gem_object *obj)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
	};
	struct sg_table *st;
	int ret;

	/* Move to the requested placement. */
	ret = ttm_bo_validate(bo, &i915_lmem0_placement, &ctx);
	if (ret)
		return ret == -ENOSPC ? -ENXIO : ret;

	/* Object either has a page vector or is an iomem object */
	st = bo->ttm ? i915_ttm_tt_get_st(bo->ttm) : obj->ttm.cached_io_st;
	if (IS_ERR(st))
		return PTR_ERR(st);

	__i915_gem_object_set_pages(obj, st, i915_sg_dma_sizes(st->sgl));

	i915_ttm_adjust_lru(obj);

	return ret;
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

	i915_ttm_adjust_lru(obj);
}

static void i915_ttm_adjust_lru(struct drm_i915_gem_object *obj)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);

	/*
	 * Don't manipulate the TTM LRUs while in TTM bo destruction.
	 * We're called through i915_ttm_delete_mem_notify().
	 */
	if (!kref_read(&bo->kref))
		return;

	/*
	 * Put on the correct LRU list depending on the MADV status
	 */
	spin_lock(&bo->bdev->lru_lock);
	if (obj->mm.madv != I915_MADV_WILLNEED) {
		bo->priority = I915_TTM_PRIO_PURGE;
	} else if (!i915_gem_object_has_pages(obj)) {
		if (bo->priority < I915_TTM_PRIO_HAS_PAGES)
			bo->priority = I915_TTM_PRIO_HAS_PAGES;
	} else {
		if (bo->priority > I915_TTM_PRIO_NO_PAGES)
			bo->priority = I915_TTM_PRIO_NO_PAGES;
	}

	ttm_bo_move_to_lru_tail(bo, bo->resource, NULL);
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
	if (obj->ttm.created) {
		ttm_bo_put(i915_gem_to_ttm(obj));
	} else {
		__i915_gem_free_object(obj);
		call_rcu(&obj->rcu, __i915_gem_free_object_rcu);
	}
}

static vm_fault_t vm_fault_ttm(struct vm_fault *vmf)
{
	struct vm_area_struct *area = vmf->vma;
	struct drm_i915_gem_object *obj =
		i915_ttm_to_gem(area->vm_private_data);

	/* Sanity check that we allow writing into this object */
	if (unlikely(i915_gem_object_is_readonly(obj) &&
		     area->vm_flags & VM_WRITE))
		return VM_FAULT_SIGBUS;

	return ttm_bo_vm_fault(vmf);
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

const struct drm_i915_gem_object_ops i915_gem_ttm_obj_ops = {
	.name = "i915_gem_object_ttm",
	.flags = I915_GEM_OBJECT_HAS_IOMEM,

	.get_pages = i915_ttm_get_pages,
	.put_pages = i915_ttm_put_pages,
	.truncate = i915_ttm_purge,
	.adjust_lru = i915_ttm_adjust_lru,
	.delayed_free = i915_ttm_delayed_free,
	.mmap_offset = i915_ttm_mmap_offset,
	.mmap_ops = &vm_ops_ttm,
};

void i915_ttm_bo_destroy(struct ttm_buffer_object *bo)
{
	struct drm_i915_gem_object *obj = i915_ttm_to_gem(bo);

	i915_gem_object_release_memory_region(obj);
	mutex_destroy(&obj->ttm.get_io_page.lock);
	if (obj->ttm.created)
		call_rcu(&obj->rcu, __i915_gem_free_object_rcu);
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
			       resource_size_t size,
			       unsigned int flags)
{
	static struct lock_class_key lock_class;
	struct drm_i915_private *i915 = mem->i915;
	enum ttm_bo_type bo_type;
	size_t alignment = 0;
	int ret;

	/* Adjust alignment to GPU- and CPU huge page sizes. */

	if (mem->is_range_manager) {
		if (size >= SZ_1G)
			alignment = SZ_1G >> PAGE_SHIFT;
		else if (size >= SZ_2M)
			alignment = SZ_2M >> PAGE_SHIFT;
		else if (size >= SZ_64K)
			alignment = SZ_64K >> PAGE_SHIFT;
	}

	drm_gem_private_object_init(&i915->drm, &obj->base, size);
	i915_gem_object_init(obj, &i915_gem_ttm_obj_ops, &lock_class, flags);
	i915_gem_object_init_memory_region(obj, mem);
	i915_gem_object_make_unshrinkable(obj);
	obj->read_domains = I915_GEM_DOMAIN_WC | I915_GEM_DOMAIN_GTT;
	i915_gem_object_set_cache_coherency(obj, I915_CACHE_NONE);
	INIT_RADIX_TREE(&obj->ttm.get_io_page.radix, GFP_KERNEL | __GFP_NOWARN);
	mutex_init(&obj->ttm.get_io_page.lock);

	bo_type = (obj->flags & I915_BO_ALLOC_USER) ? ttm_bo_type_device :
		ttm_bo_type_kernel;

	/*
	 * If this function fails, it will call the destructor, but
	 * our caller still owns the object. So no freeing in the
	 * destructor until obj->ttm.created is true.
	 * Similarly, in delayed_destroy, we can't call ttm_bo_put()
	 * until successful initialization.
	 */
	obj->base.vma_node.driver_private = i915_gem_to_ttm(obj);
	ret = ttm_bo_init(&i915->bdev, i915_gem_to_ttm(obj), size,
			  bo_type, &i915_sys_placement, alignment,
			  true, NULL, NULL, i915_ttm_bo_destroy);

	if (!ret)
		obj->ttm.created = true;

	/* i915 wants -ENXIO when out of memory region space. */
	return (ret == -ENOSPC) ? -ENXIO : ret;
}
