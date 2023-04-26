/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2016 Intel Corporation
 */

#include <drm/drm_cache.h>

#include "gt/intel_gt.h"
#include "gt/intel_gt_pm.h"

#include "i915_drv.h"
#include "i915_gem_object.h"
#include "i915_scatterlist.h"
#include "i915_gem_lmem.h"
#include "i915_gem_mman.h"

void __i915_gem_object_set_pages(struct drm_i915_gem_object *obj,
				 struct sg_table *pages)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	unsigned long supported = RUNTIME_INFO(i915)->page_sizes;
	bool shrinkable;
	int i;

	assert_object_held_shared(obj);

	if (i915_gem_object_is_volatile(obj))
		obj->mm.madv = I915_MADV_DONTNEED;

	/* Make the pages coherent with the GPU (flushing any swapin). */
	if (obj->cache_dirty) {
		WARN_ON_ONCE(IS_DGFX(i915));
		obj->write_domain = 0;
		if (i915_gem_object_has_struct_page(obj))
			drm_clflush_sg(pages);
		obj->cache_dirty = false;
	}

	obj->mm.get_page.sg_pos = pages->sgl;
	obj->mm.get_page.sg_idx = 0;
	obj->mm.get_dma_page.sg_pos = pages->sgl;
	obj->mm.get_dma_page.sg_idx = 0;

	obj->mm.pages = pages;

	obj->mm.page_sizes.phys = i915_sg_dma_sizes(pages->sgl);
	GEM_BUG_ON(!obj->mm.page_sizes.phys);

	/*
	 * Calculate the supported page-sizes which fit into the given
	 * sg_page_sizes. This will give us the page-sizes which we may be able
	 * to use opportunistically when later inserting into the GTT. For
	 * example if phys=2G, then in theory we should be able to use 1G, 2M,
	 * 64K or 4K pages, although in practice this will depend on a number of
	 * other factors.
	 */
	obj->mm.page_sizes.sg = 0;
	for_each_set_bit(i, &supported, ilog2(I915_GTT_MAX_PAGE_SIZE) + 1) {
		if (obj->mm.page_sizes.phys & ~0u << i)
			obj->mm.page_sizes.sg |= BIT(i);
	}
	GEM_BUG_ON(!HAS_PAGE_SIZES(i915, obj->mm.page_sizes.sg));

	shrinkable = i915_gem_object_is_shrinkable(obj);

	if (i915_gem_object_is_tiled(obj) &&
	    i915->gem_quirks & GEM_QUIRK_PIN_SWIZZLED_PAGES) {
		GEM_BUG_ON(i915_gem_object_has_tiling_quirk(obj));
		i915_gem_object_set_tiling_quirk(obj);
		GEM_BUG_ON(!list_empty(&obj->mm.link));
		atomic_inc(&obj->mm.shrink_pin);
		shrinkable = false;
	}

	if (shrinkable && !i915_gem_object_has_self_managed_shrink_list(obj)) {
		struct list_head *list;
		unsigned long flags;

		assert_object_held(obj);
		spin_lock_irqsave(&i915->mm.obj_lock, flags);

		i915->mm.shrink_count++;
		i915->mm.shrink_memory += obj->base.size;

		if (obj->mm.madv != I915_MADV_WILLNEED)
			list = &i915->mm.purge_list;
		else
			list = &i915->mm.shrink_list;
		list_add_tail(&obj->mm.link, list);

		atomic_set(&obj->mm.shrink_pin, 0);
		spin_unlock_irqrestore(&i915->mm.obj_lock, flags);
	}
}

int ____i915_gem_object_get_pages(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	int err;

	assert_object_held_shared(obj);

	if (unlikely(obj->mm.madv != I915_MADV_WILLNEED)) {
		drm_dbg(&i915->drm,
			"Attempting to obtain a purgeable object\n");
		return -EFAULT;
	}

	err = obj->ops->get_pages(obj);
	GEM_BUG_ON(!err && !i915_gem_object_has_pages(obj));

	return err;
}

/* Ensure that the associated pages are gathered from the backing storage
 * and pinned into our object. i915_gem_object_pin_pages() may be called
 * multiple times before they are released by a single call to
 * i915_gem_object_unpin_pages() - once the pages are no longer referenced
 * either as a result of memory pressure (reaping pages under the shrinker)
 * or as the object is itself released.
 */
int __i915_gem_object_get_pages(struct drm_i915_gem_object *obj)
{
	int err;

	assert_object_held(obj);

	assert_object_held_shared(obj);

	if (unlikely(!i915_gem_object_has_pages(obj))) {
		GEM_BUG_ON(i915_gem_object_has_pinned_pages(obj));

		err = ____i915_gem_object_get_pages(obj);
		if (err)
			return err;

		smp_mb__before_atomic();
	}
	atomic_inc(&obj->mm.pages_pin_count);

	return 0;
}

int i915_gem_object_pin_pages_unlocked(struct drm_i915_gem_object *obj)
{
	struct i915_gem_ww_ctx ww;
	int err;

	i915_gem_ww_ctx_init(&ww, true);
retry:
	err = i915_gem_object_lock(obj, &ww);
	if (!err)
		err = i915_gem_object_pin_pages(obj);

	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	return err;
}

/* Immediately discard the backing storage */
int i915_gem_object_truncate(struct drm_i915_gem_object *obj)
{
	if (obj->ops->truncate)
		return obj->ops->truncate(obj);

	return 0;
}

static void __i915_gem_object_reset_page_iter(struct drm_i915_gem_object *obj)
{
	struct radix_tree_iter iter;
	void __rcu **slot;

	rcu_read_lock();
	radix_tree_for_each_slot(slot, &obj->mm.get_page.radix, &iter, 0)
		radix_tree_delete(&obj->mm.get_page.radix, iter.index);
	radix_tree_for_each_slot(slot, &obj->mm.get_dma_page.radix, &iter, 0)
		radix_tree_delete(&obj->mm.get_dma_page.radix, iter.index);
	rcu_read_unlock();
}

static void unmap_object(struct drm_i915_gem_object *obj, void *ptr)
{
	if (is_vmalloc_addr(ptr))
		vunmap(ptr);
}

static void flush_tlb_invalidate(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct intel_gt *gt = to_gt(i915);

	if (!obj->mm.tlb)
		return;

	intel_gt_invalidate_tlb(gt, obj->mm.tlb);
	obj->mm.tlb = 0;
}

struct sg_table *
__i915_gem_object_unset_pages(struct drm_i915_gem_object *obj)
{
	struct sg_table *pages;

	assert_object_held_shared(obj);

	pages = fetch_and_zero(&obj->mm.pages);
	if (IS_ERR_OR_NULL(pages))
		return pages;

	if (i915_gem_object_is_volatile(obj))
		obj->mm.madv = I915_MADV_WILLNEED;

	if (!i915_gem_object_has_self_managed_shrink_list(obj))
		i915_gem_object_make_unshrinkable(obj);

	if (obj->mm.mapping) {
		unmap_object(obj, page_mask_bits(obj->mm.mapping));
		obj->mm.mapping = NULL;
	}

	__i915_gem_object_reset_page_iter(obj);
	obj->mm.page_sizes.phys = obj->mm.page_sizes.sg = 0;

	flush_tlb_invalidate(obj);

	return pages;
}

int __i915_gem_object_put_pages(struct drm_i915_gem_object *obj)
{
	struct sg_table *pages;

	if (i915_gem_object_has_pinned_pages(obj))
		return -EBUSY;

	/* May be called by shrinker from within get_pages() (on another bo) */
	assert_object_held_shared(obj);

	i915_gem_object_release_mmap_offset(obj);

	/*
	 * ->put_pages might need to allocate memory for the bit17 swizzle
	 * array, hence protect them from being reaped by removing them from gtt
	 * lists early.
	 */
	pages = __i915_gem_object_unset_pages(obj);

	/*
	 * XXX Temporary hijinx to avoid updating all backends to handle
	 * NULL pages. In the future, when we have more asynchronous
	 * get_pages backends we should be better able to handle the
	 * cancellation of the async task in a more uniform manner.
	 */
	if (!IS_ERR_OR_NULL(pages))
		obj->ops->put_pages(obj, pages);

	return 0;
}

/* The 'mapping' part of i915_gem_object_pin_map() below */
static void *i915_gem_object_map_page(struct drm_i915_gem_object *obj,
				      enum i915_map_type type)
{
	unsigned long n_pages = obj->base.size >> PAGE_SHIFT, i;
	struct page *stack[32], **pages = stack, *page;
	struct sgt_iter iter;
	pgprot_t pgprot;
	void *vaddr;

	switch (type) {
	default:
		MISSING_CASE(type);
		fallthrough;	/* to use PAGE_KERNEL anyway */
	case I915_MAP_WB:
		/*
		 * On 32b, highmem using a finite set of indirect PTE (i.e.
		 * vmap) to provide virtual mappings of the high pages.
		 * As these are finite, map_new_virtual() must wait for some
		 * other kmap() to finish when it runs out. If we map a large
		 * number of objects, there is no method for it to tell us
		 * to release the mappings, and we deadlock.
		 *
		 * However, if we make an explicit vmap of the page, that
		 * uses a larger vmalloc arena, and also has the ability
		 * to tell us to release unwanted mappings. Most importantly,
		 * it will fail and propagate an error instead of waiting
		 * forever.
		 *
		 * So if the page is beyond the 32b boundary, make an explicit
		 * vmap.
		 */
		if (n_pages == 1 && !PageHighMem(sg_page(obj->mm.pages->sgl)))
			return page_address(sg_page(obj->mm.pages->sgl));
		pgprot = PAGE_KERNEL;
		break;
	case I915_MAP_WC:
		pgprot = pgprot_writecombine(PAGE_KERNEL_IO);
		break;
	}

	if (n_pages > ARRAY_SIZE(stack)) {
		/* Too big for stack -- allocate temporary array instead */
		pages = kvmalloc_array(n_pages, sizeof(*pages), GFP_KERNEL);
		if (!pages)
			return ERR_PTR(-ENOMEM);
	}

	i = 0;
	for_each_sgt_page(page, iter, obj->mm.pages)
		pages[i++] = page;
	vaddr = vmap(pages, n_pages, 0, pgprot);
	if (pages != stack)
		kvfree(pages);

	return vaddr ?: ERR_PTR(-ENOMEM);
}

static void *i915_gem_object_map_pfn(struct drm_i915_gem_object *obj,
				     enum i915_map_type type)
{
	resource_size_t iomap = obj->mm.region->iomap.base -
		obj->mm.region->region.start;
	unsigned long n_pfn = obj->base.size >> PAGE_SHIFT;
	unsigned long stack[32], *pfns = stack, i;
	struct sgt_iter iter;
	dma_addr_t addr;
	void *vaddr;

	GEM_BUG_ON(type != I915_MAP_WC);

	if (n_pfn > ARRAY_SIZE(stack)) {
		/* Too big for stack -- allocate temporary array instead */
		pfns = kvmalloc_array(n_pfn, sizeof(*pfns), GFP_KERNEL);
		if (!pfns)
			return ERR_PTR(-ENOMEM);
	}

	i = 0;
	for_each_sgt_daddr(addr, iter, obj->mm.pages)
		pfns[i++] = (iomap + addr) >> PAGE_SHIFT;
	vaddr = vmap_pfn(pfns, n_pfn, pgprot_writecombine(PAGE_KERNEL_IO));
	if (pfns != stack)
		kvfree(pfns);

	return vaddr ?: ERR_PTR(-ENOMEM);
}

/* get, pin, and map the pages of the object into kernel space */
void *i915_gem_object_pin_map(struct drm_i915_gem_object *obj,
			      enum i915_map_type type)
{
	enum i915_map_type has_type;
	bool pinned;
	void *ptr;
	int err;

	if (!i915_gem_object_has_struct_page(obj) &&
	    !i915_gem_object_has_iomem(obj))
		return ERR_PTR(-ENXIO);

	if (WARN_ON_ONCE(obj->flags & I915_BO_ALLOC_GPU_ONLY))
		return ERR_PTR(-EINVAL);

	assert_object_held(obj);

	pinned = !(type & I915_MAP_OVERRIDE);
	type &= ~I915_MAP_OVERRIDE;

	if (!atomic_inc_not_zero(&obj->mm.pages_pin_count)) {
		if (unlikely(!i915_gem_object_has_pages(obj))) {
			GEM_BUG_ON(i915_gem_object_has_pinned_pages(obj));

			err = ____i915_gem_object_get_pages(obj);
			if (err)
				return ERR_PTR(err);

			smp_mb__before_atomic();
		}
		atomic_inc(&obj->mm.pages_pin_count);
		pinned = false;
	}
	GEM_BUG_ON(!i915_gem_object_has_pages(obj));

	/*
	 * For discrete our CPU mappings needs to be consistent in order to
	 * function correctly on !x86. When mapping things through TTM, we use
	 * the same rules to determine the caching type.
	 *
	 * The caching rules, starting from DG1:
	 *
	 *	- If the object can be placed in device local-memory, then the
	 *	  pages should be allocated and mapped as write-combined only.
	 *
	 *	- Everything else is always allocated and mapped as write-back,
	 *	  with the guarantee that everything is also coherent with the
	 *	  GPU.
	 *
	 * Internal users of lmem are already expected to get this right, so no
	 * fudging needed there.
	 */
	if (i915_gem_object_placement_possible(obj, INTEL_MEMORY_LOCAL)) {
		if (type != I915_MAP_WC && !obj->mm.n_placements) {
			ptr = ERR_PTR(-ENODEV);
			goto err_unpin;
		}

		type = I915_MAP_WC;
	} else if (IS_DGFX(to_i915(obj->base.dev))) {
		type = I915_MAP_WB;
	}

	ptr = page_unpack_bits(obj->mm.mapping, &has_type);
	if (ptr && has_type != type) {
		if (pinned) {
			ptr = ERR_PTR(-EBUSY);
			goto err_unpin;
		}

		unmap_object(obj, ptr);

		ptr = obj->mm.mapping = NULL;
	}

	if (!ptr) {
		err = i915_gem_object_wait_moving_fence(obj, true);
		if (err) {
			ptr = ERR_PTR(err);
			goto err_unpin;
		}

		if (GEM_WARN_ON(type == I915_MAP_WC && !pat_enabled()))
			ptr = ERR_PTR(-ENODEV);
		else if (i915_gem_object_has_struct_page(obj))
			ptr = i915_gem_object_map_page(obj, type);
		else
			ptr = i915_gem_object_map_pfn(obj, type);
		if (IS_ERR(ptr))
			goto err_unpin;

		obj->mm.mapping = page_pack_bits(ptr, type);
	}

	return ptr;

err_unpin:
	atomic_dec(&obj->mm.pages_pin_count);
	return ptr;
}

void *i915_gem_object_pin_map_unlocked(struct drm_i915_gem_object *obj,
				       enum i915_map_type type)
{
	void *ret;

	i915_gem_object_lock(obj, NULL);
	ret = i915_gem_object_pin_map(obj, type);
	i915_gem_object_unlock(obj);

	return ret;
}

enum i915_map_type i915_coherent_map_type(struct drm_i915_private *i915,
					  struct drm_i915_gem_object *obj,
					  bool always_coherent)
{
	if (i915_gem_object_is_lmem(obj))
		return I915_MAP_WC;
	if (HAS_LLC(i915) || always_coherent)
		return I915_MAP_WB;
	else
		return I915_MAP_WC;
}

void __i915_gem_object_flush_map(struct drm_i915_gem_object *obj,
				 unsigned long offset,
				 unsigned long size)
{
	enum i915_map_type has_type;
	void *ptr;

	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));
	GEM_BUG_ON(range_overflows_t(typeof(obj->base.size),
				     offset, size, obj->base.size));

	wmb(); /* let all previous writes be visible to coherent partners */
	obj->mm.dirty = true;

	if (obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_WRITE)
		return;

	ptr = page_unpack_bits(obj->mm.mapping, &has_type);
	if (has_type == I915_MAP_WC)
		return;

	drm_clflush_virt_range(ptr + offset, size);
	if (size == obj->base.size) {
		obj->write_domain &= ~I915_GEM_DOMAIN_CPU;
		obj->cache_dirty = false;
	}
}

void __i915_gem_object_release_map(struct drm_i915_gem_object *obj)
{
	GEM_BUG_ON(!obj->mm.mapping);

	/*
	 * We allow removing the mapping from underneath pinned pages!
	 *
	 * Furthermore, since this is an unsafe operation reserved only
	 * for construction time manipulation, we ignore locking prudence.
	 */
	unmap_object(obj, page_mask_bits(fetch_and_zero(&obj->mm.mapping)));

	i915_gem_object_unpin_map(obj);
}

struct scatterlist *
__i915_gem_object_page_iter_get_sg(struct drm_i915_gem_object *obj,
				   struct i915_gem_object_page_iter *iter,
				   pgoff_t n,
				   unsigned int *offset)

{
	const bool dma = iter == &obj->mm.get_dma_page ||
			 iter == &obj->ttm.get_io_page;
	unsigned int idx, count;
	struct scatterlist *sg;

	might_sleep();
	GEM_BUG_ON(n >= obj->base.size >> PAGE_SHIFT);
	if (!i915_gem_object_has_pinned_pages(obj))
		assert_object_held(obj);

	/* As we iterate forward through the sg, we record each entry in a
	 * radixtree for quick repeated (backwards) lookups. If we have seen
	 * this index previously, we will have an entry for it.
	 *
	 * Initial lookup is O(N), but this is amortized to O(1) for
	 * sequential page access (where each new request is consecutive
	 * to the previous one). Repeated lookups are O(lg(obj->base.size)),
	 * i.e. O(1) with a large constant!
	 */
	if (n < READ_ONCE(iter->sg_idx))
		goto lookup;

	mutex_lock(&iter->lock);

	/* We prefer to reuse the last sg so that repeated lookup of this
	 * (or the subsequent) sg are fast - comparing against the last
	 * sg is faster than going through the radixtree.
	 */

	sg = iter->sg_pos;
	idx = iter->sg_idx;
	count = dma ? __sg_dma_page_count(sg) : __sg_page_count(sg);

	while (idx + count <= n) {
		void *entry;
		unsigned long i;
		int ret;

		/* If we cannot allocate and insert this entry, or the
		 * individual pages from this range, cancel updating the
		 * sg_idx so that on this lookup we are forced to linearly
		 * scan onwards, but on future lookups we will try the
		 * insertion again (in which case we need to be careful of
		 * the error return reporting that we have already inserted
		 * this index).
		 */
		ret = radix_tree_insert(&iter->radix, idx, sg);
		if (ret && ret != -EEXIST)
			goto scan;

		entry = xa_mk_value(idx);
		for (i = 1; i < count; i++) {
			ret = radix_tree_insert(&iter->radix, idx + i, entry);
			if (ret && ret != -EEXIST)
				goto scan;
		}

		idx += count;
		sg = ____sg_next(sg);
		count = dma ? __sg_dma_page_count(sg) : __sg_page_count(sg);
	}

scan:
	iter->sg_pos = sg;
	iter->sg_idx = idx;

	mutex_unlock(&iter->lock);

	if (unlikely(n < idx)) /* insertion completed by another thread */
		goto lookup;

	/* In case we failed to insert the entry into the radixtree, we need
	 * to look beyond the current sg.
	 */
	while (idx + count <= n) {
		idx += count;
		sg = ____sg_next(sg);
		count = dma ? __sg_dma_page_count(sg) : __sg_page_count(sg);
	}

	*offset = n - idx;
	return sg;

lookup:
	rcu_read_lock();

	sg = radix_tree_lookup(&iter->radix, n);
	GEM_BUG_ON(!sg);

	/* If this index is in the middle of multi-page sg entry,
	 * the radix tree will contain a value entry that points
	 * to the start of that range. We will return the pointer to
	 * the base page and the offset of this page within the
	 * sg entry's range.
	 */
	*offset = 0;
	if (unlikely(xa_is_value(sg))) {
		unsigned long base = xa_to_value(sg);

		sg = radix_tree_lookup(&iter->radix, base);
		GEM_BUG_ON(!sg);

		*offset = n - base;
	}

	rcu_read_unlock();

	return sg;
}

struct page *
__i915_gem_object_get_page(struct drm_i915_gem_object *obj, pgoff_t n)
{
	struct scatterlist *sg;
	unsigned int offset;

	GEM_BUG_ON(!i915_gem_object_has_struct_page(obj));

	sg = i915_gem_object_get_sg(obj, n, &offset);
	return nth_page(sg_page(sg), offset);
}

/* Like i915_gem_object_get_page(), but mark the returned page dirty */
struct page *
__i915_gem_object_get_dirty_page(struct drm_i915_gem_object *obj, pgoff_t n)
{
	struct page *page;

	page = i915_gem_object_get_page(obj, n);
	if (!obj->mm.dirty)
		set_page_dirty(page);

	return page;
}

dma_addr_t
__i915_gem_object_get_dma_address_len(struct drm_i915_gem_object *obj,
				      pgoff_t n, unsigned int *len)
{
	struct scatterlist *sg;
	unsigned int offset;

	sg = i915_gem_object_get_sg_dma(obj, n, &offset);

	if (len)
		*len = sg_dma_len(sg) - (offset << PAGE_SHIFT);

	return sg_dma_address(sg) + (offset << PAGE_SHIFT);
}

dma_addr_t
__i915_gem_object_get_dma_address(struct drm_i915_gem_object *obj, pgoff_t n)
{
	return i915_gem_object_get_dma_address_len(obj, n, NULL);
}
