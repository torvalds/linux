/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2016 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_gem_object.h"
#include "i915_scatterlist.h"
#include "i915_gem_lmem.h"
#include "i915_gem_mman.h"

void __i915_gem_object_set_pages(struct drm_i915_gem_object *obj,
				 struct sg_table *pages,
				 unsigned int sg_page_sizes)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	unsigned long supported = INTEL_INFO(i915)->page_sizes;
	int i;

	lockdep_assert_held(&obj->mm.lock);

	if (i915_gem_object_is_volatile(obj))
		obj->mm.madv = I915_MADV_DONTNEED;

	/* Make the pages coherent with the GPU (flushing any swapin). */
	if (obj->cache_dirty) {
		obj->write_domain = 0;
		if (i915_gem_object_has_struct_page(obj))
			drm_clflush_sg(pages);
		obj->cache_dirty = false;
	}

	obj->mm.get_page.sg_pos = pages->sgl;
	obj->mm.get_page.sg_idx = 0;

	obj->mm.pages = pages;

	if (i915_gem_object_is_tiled(obj) &&
	    i915->quirks & QUIRK_PIN_SWIZZLED_PAGES) {
		GEM_BUG_ON(obj->mm.quirked);
		__i915_gem_object_pin_pages(obj);
		obj->mm.quirked = true;
	}

	GEM_BUG_ON(!sg_page_sizes);
	obj->mm.page_sizes.phys = sg_page_sizes;

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

	if (i915_gem_object_is_shrinkable(obj)) {
		struct list_head *list;
		unsigned long flags;

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
	int err;

	if (unlikely(obj->mm.madv != I915_MADV_WILLNEED)) {
		DRM_DEBUG("Attempting to obtain a purgeable object\n");
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

	err = mutex_lock_interruptible_nested(&obj->mm.lock, I915_MM_GET_PAGES);
	if (err)
		return err;

	if (unlikely(!i915_gem_object_has_pages(obj))) {
		GEM_BUG_ON(i915_gem_object_has_pinned_pages(obj));

		err = ____i915_gem_object_get_pages(obj);
		if (err)
			goto unlock;

		smp_mb__before_atomic();
	}
	atomic_inc(&obj->mm.pages_pin_count);

unlock:
	mutex_unlock(&obj->mm.lock);
	return err;
}

/* Immediately discard the backing storage */
void i915_gem_object_truncate(struct drm_i915_gem_object *obj)
{
	drm_gem_free_mmap_offset(&obj->base);
	if (obj->ops->truncate)
		obj->ops->truncate(obj);
}

/* Try to discard unwanted pages */
void i915_gem_object_writeback(struct drm_i915_gem_object *obj)
{
	lockdep_assert_held(&obj->mm.lock);
	GEM_BUG_ON(i915_gem_object_has_pages(obj));

	if (obj->ops->writeback)
		obj->ops->writeback(obj);
}

static void __i915_gem_object_reset_page_iter(struct drm_i915_gem_object *obj)
{
	struct radix_tree_iter iter;
	void __rcu **slot;

	rcu_read_lock();
	radix_tree_for_each_slot(slot, &obj->mm.get_page.radix, &iter, 0)
		radix_tree_delete(&obj->mm.get_page.radix, iter.index);
	rcu_read_unlock();
}

static void unmap_object(struct drm_i915_gem_object *obj, void *ptr)
{
	if (is_vmalloc_addr(ptr))
		vunmap(ptr);
	else
		kunmap(kmap_to_page(ptr));
}

struct sg_table *
__i915_gem_object_unset_pages(struct drm_i915_gem_object *obj)
{
	struct sg_table *pages;

	pages = fetch_and_zero(&obj->mm.pages);
	if (IS_ERR_OR_NULL(pages))
		return pages;

	if (i915_gem_object_is_volatile(obj))
		obj->mm.madv = I915_MADV_WILLNEED;

	i915_gem_object_make_unshrinkable(obj);

	if (obj->mm.mapping) {
		unmap_object(obj, page_mask_bits(obj->mm.mapping));
		obj->mm.mapping = NULL;
	}

	__i915_gem_object_reset_page_iter(obj);
	obj->mm.page_sizes.phys = obj->mm.page_sizes.sg = 0;

	return pages;
}

int __i915_gem_object_put_pages(struct drm_i915_gem_object *obj)
{
	struct sg_table *pages;
	int err;

	if (i915_gem_object_has_pinned_pages(obj))
		return -EBUSY;

	GEM_BUG_ON(atomic_read(&obj->bind_count));

	/* May be called by shrinker from within get_pages() (on another bo) */
	mutex_lock(&obj->mm.lock);
	if (unlikely(atomic_read(&obj->mm.pages_pin_count))) {
		err = -EBUSY;
		goto unlock;
	}

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
	if (!pages && !i915_gem_object_needs_async_cancel(obj))
		pages = ERR_PTR(-EINVAL);

	if (!IS_ERR(pages))
		obj->ops->put_pages(obj, pages);

	err = 0;
unlock:
	mutex_unlock(&obj->mm.lock);

	return err;
}

static inline pte_t iomap_pte(resource_size_t base,
			      dma_addr_t offset,
			      pgprot_t prot)
{
	return pte_mkspecial(pfn_pte((base + offset) >> PAGE_SHIFT, prot));
}

/* The 'mapping' part of i915_gem_object_pin_map() below */
static void *i915_gem_object_map(struct drm_i915_gem_object *obj,
				 enum i915_map_type type)
{
	unsigned long n_pte = obj->base.size >> PAGE_SHIFT;
	struct sg_table *sgt = obj->mm.pages;
	pte_t *stack[32], **mem;
	struct vm_struct *area;
	pgprot_t pgprot;

	if (!i915_gem_object_has_struct_page(obj) && type != I915_MAP_WC)
		return NULL;

	/* A single page can always be kmapped */
	if (n_pte == 1 && type == I915_MAP_WB)
		return kmap(sg_page(sgt->sgl));

	mem = stack;
	if (n_pte > ARRAY_SIZE(stack)) {
		/* Too big for stack -- allocate temporary array instead */
		mem = kvmalloc_array(n_pte, sizeof(*mem), GFP_KERNEL);
		if (!mem)
			return NULL;
	}

	area = alloc_vm_area(obj->base.size, mem);
	if (!area) {
		if (mem != stack)
			kvfree(mem);
		return NULL;
	}

	switch (type) {
	default:
		MISSING_CASE(type);
		/* fallthrough - to use PAGE_KERNEL anyway */
	case I915_MAP_WB:
		pgprot = PAGE_KERNEL;
		break;
	case I915_MAP_WC:
		pgprot = pgprot_writecombine(PAGE_KERNEL_IO);
		break;
	}

	if (i915_gem_object_has_struct_page(obj)) {
		struct sgt_iter iter;
		struct page *page;
		pte_t **ptes = mem;

		for_each_sgt_page(page, iter, sgt)
			**ptes++ = mk_pte(page, pgprot);
	} else {
		resource_size_t iomap;
		struct sgt_iter iter;
		pte_t **ptes = mem;
		dma_addr_t addr;

		iomap = obj->mm.region->iomap.base;
		iomap -= obj->mm.region->region.start;

		for_each_sgt_daddr(addr, iter, sgt)
			**ptes++ = iomap_pte(iomap, addr, pgprot);
	}

	if (mem != stack)
		kvfree(mem);

	return area->addr;
}

/* get, pin, and map the pages of the object into kernel space */
void *i915_gem_object_pin_map(struct drm_i915_gem_object *obj,
			      enum i915_map_type type)
{
	enum i915_map_type has_type;
	unsigned int flags;
	bool pinned;
	void *ptr;
	int err;

	flags = I915_GEM_OBJECT_HAS_STRUCT_PAGE | I915_GEM_OBJECT_HAS_IOMEM;
	if (!i915_gem_object_type_has(obj, flags))
		return ERR_PTR(-ENXIO);

	err = mutex_lock_interruptible_nested(&obj->mm.lock, I915_MM_GET_PAGES);
	if (err)
		return ERR_PTR(err);

	pinned = !(type & I915_MAP_OVERRIDE);
	type &= ~I915_MAP_OVERRIDE;

	if (!atomic_inc_not_zero(&obj->mm.pages_pin_count)) {
		if (unlikely(!i915_gem_object_has_pages(obj))) {
			GEM_BUG_ON(i915_gem_object_has_pinned_pages(obj));

			err = ____i915_gem_object_get_pages(obj);
			if (err)
				goto err_unlock;

			smp_mb__before_atomic();
		}
		atomic_inc(&obj->mm.pages_pin_count);
		pinned = false;
	}
	GEM_BUG_ON(!i915_gem_object_has_pages(obj));

	ptr = page_unpack_bits(obj->mm.mapping, &has_type);
	if (ptr && has_type != type) {
		if (pinned) {
			err = -EBUSY;
			goto err_unpin;
		}

		unmap_object(obj, ptr);

		ptr = obj->mm.mapping = NULL;
	}

	if (!ptr) {
		ptr = i915_gem_object_map(obj, type);
		if (!ptr) {
			err = -ENOMEM;
			goto err_unpin;
		}

		obj->mm.mapping = page_pack_bits(ptr, type);
	}

out_unlock:
	mutex_unlock(&obj->mm.lock);
	return ptr;

err_unpin:
	atomic_dec(&obj->mm.pages_pin_count);
err_unlock:
	ptr = ERR_PTR(err);
	goto out_unlock;
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

struct scatterlist *
i915_gem_object_get_sg(struct drm_i915_gem_object *obj,
		       unsigned int n,
		       unsigned int *offset)
{
	struct i915_gem_object_page_iter *iter = &obj->mm.get_page;
	struct scatterlist *sg;
	unsigned int idx, count;

	might_sleep();
	GEM_BUG_ON(n >= obj->base.size >> PAGE_SHIFT);
	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));

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
	count = __sg_page_count(sg);

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
		count = __sg_page_count(sg);
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
		count = __sg_page_count(sg);
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
i915_gem_object_get_page(struct drm_i915_gem_object *obj, unsigned int n)
{
	struct scatterlist *sg;
	unsigned int offset;

	GEM_BUG_ON(!i915_gem_object_has_struct_page(obj));

	sg = i915_gem_object_get_sg(obj, n, &offset);
	return nth_page(sg_page(sg), offset);
}

/* Like i915_gem_object_get_page(), but mark the returned page dirty */
struct page *
i915_gem_object_get_dirty_page(struct drm_i915_gem_object *obj,
			       unsigned int n)
{
	struct page *page;

	page = i915_gem_object_get_page(obj, n);
	if (!obj->mm.dirty)
		set_page_dirty(page);

	return page;
}

dma_addr_t
i915_gem_object_get_dma_address_len(struct drm_i915_gem_object *obj,
				    unsigned long n,
				    unsigned int *len)
{
	struct scatterlist *sg;
	unsigned int offset;

	sg = i915_gem_object_get_sg(obj, n, &offset);

	if (len)
		*len = sg_dma_len(sg) - (offset << PAGE_SHIFT);

	return sg_dma_address(sg) + (offset << PAGE_SHIFT);
}

dma_addr_t
i915_gem_object_get_dma_address(struct drm_i915_gem_object *obj,
				unsigned long n)
{
	return i915_gem_object_get_dma_address_len(obj, n, NULL);
}
