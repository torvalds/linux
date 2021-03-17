/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2016 Intel Corporation
 */

#include <linux/pagevec.h>
#include <linux/swap.h>

#include "gem/i915_gem_region.h"
#include "i915_drv.h"
#include "i915_gemfs.h"
#include "i915_gem_object.h"
#include "i915_scatterlist.h"
#include "i915_trace.h"

/*
 * Move pages to appropriate lru and release the pagevec, decrementing the
 * ref count of those pages.
 */
static void check_release_pagevec(struct pagevec *pvec)
{
	check_move_unevictable_pages(pvec);
	__pagevec_release(pvec);
	cond_resched();
}

static int shmem_get_pages(struct drm_i915_gem_object *obj)
{
	struct drm_i915_private *i915 = to_i915(obj->base.dev);
	struct intel_memory_region *mem = obj->mm.region;
	const unsigned long page_count = obj->base.size / PAGE_SIZE;
	unsigned long i;
	struct address_space *mapping;
	struct sg_table *st;
	struct scatterlist *sg;
	struct sgt_iter sgt_iter;
	struct page *page;
	unsigned long last_pfn = 0;	/* suppress gcc warning */
	unsigned int max_segment = i915_sg_segment_size();
	unsigned int sg_page_sizes;
	gfp_t noreclaim;
	int ret;

	/*
	 * Assert that the object is not currently in any GPU domain. As it
	 * wasn't in the GTT, there shouldn't be any way it could have been in
	 * a GPU cache
	 */
	GEM_BUG_ON(obj->read_domains & I915_GEM_GPU_DOMAINS);
	GEM_BUG_ON(obj->write_domain & I915_GEM_GPU_DOMAINS);

	/*
	 * If there's no chance of allocating enough pages for the whole
	 * object, bail early.
	 */
	if (obj->base.size > resource_size(&mem->region))
		return -ENOMEM;

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

rebuild_st:
	if (sg_alloc_table(st, page_count, GFP_KERNEL)) {
		kfree(st);
		return -ENOMEM;
	}

	/*
	 * Get the list of pages out of our struct file.  They'll be pinned
	 * at this point until we release them.
	 *
	 * Fail silently without starting the shrinker
	 */
	mapping = obj->base.filp->f_mapping;
	mapping_set_unevictable(mapping);
	noreclaim = mapping_gfp_constraint(mapping, ~__GFP_RECLAIM);
	noreclaim |= __GFP_NORETRY | __GFP_NOWARN;

	sg = st->sgl;
	st->nents = 0;
	sg_page_sizes = 0;
	for (i = 0; i < page_count; i++) {
		const unsigned int shrink[] = {
			I915_SHRINK_BOUND | I915_SHRINK_UNBOUND,
			0,
		}, *s = shrink;
		gfp_t gfp = noreclaim;

		do {
			cond_resched();
			page = shmem_read_mapping_page_gfp(mapping, i, gfp);
			if (!IS_ERR(page))
				break;

			if (!*s) {
				ret = PTR_ERR(page);
				goto err_sg;
			}

			i915_gem_shrink(i915, 2 * page_count, NULL, *s++);

			/*
			 * We've tried hard to allocate the memory by reaping
			 * our own buffer, now let the real VM do its job and
			 * go down in flames if truly OOM.
			 *
			 * However, since graphics tend to be disposable,
			 * defer the oom here by reporting the ENOMEM back
			 * to userspace.
			 */
			if (!*s) {
				/* reclaim and warn, but no oom */
				gfp = mapping_gfp_mask(mapping);

				/*
				 * Our bo are always dirty and so we require
				 * kswapd to reclaim our pages (direct reclaim
				 * does not effectively begin pageout of our
				 * buffers on its own). However, direct reclaim
				 * only waits for kswapd when under allocation
				 * congestion. So as a result __GFP_RECLAIM is
				 * unreliable and fails to actually reclaim our
				 * dirty pages -- unless you try over and over
				 * again with !__GFP_NORETRY. However, we still
				 * want to fail this allocation rather than
				 * trigger the out-of-memory killer and for
				 * this we want __GFP_RETRY_MAYFAIL.
				 */
				gfp |= __GFP_RETRY_MAYFAIL;
			}
		} while (1);

		if (!i ||
		    sg->length >= max_segment ||
		    page_to_pfn(page) != last_pfn + 1) {
			if (i) {
				sg_page_sizes |= sg->length;
				sg = sg_next(sg);
			}
			st->nents++;
			sg_set_page(sg, page, PAGE_SIZE, 0);
		} else {
			sg->length += PAGE_SIZE;
		}
		last_pfn = page_to_pfn(page);

		/* Check that the i965g/gm workaround works. */
		GEM_BUG_ON(gfp & __GFP_DMA32 && last_pfn >= 0x00100000UL);
	}
	if (sg) { /* loop terminated early; short sg table */
		sg_page_sizes |= sg->length;
		sg_mark_end(sg);
	}

	/* Trim unused sg entries to avoid wasting memory. */
	i915_sg_trim(st);

	ret = i915_gem_gtt_prepare_pages(obj, st);
	if (ret) {
		/*
		 * DMA remapping failed? One possible cause is that
		 * it could not reserve enough large entries, asking
		 * for PAGE_SIZE chunks instead may be helpful.
		 */
		if (max_segment > PAGE_SIZE) {
			for_each_sgt_page(page, sgt_iter, st)
				put_page(page);
			sg_free_table(st);

			max_segment = PAGE_SIZE;
			goto rebuild_st;
		} else {
			dev_warn(i915->drm.dev,
				 "Failed to DMA remap %lu pages\n",
				 page_count);
			goto err_pages;
		}
	}

	if (i915_gem_object_needs_bit17_swizzle(obj))
		i915_gem_object_do_bit_17_swizzle(obj, st);

	__i915_gem_object_set_pages(obj, st, sg_page_sizes);

	return 0;

err_sg:
	sg_mark_end(sg);
err_pages:
	mapping_clear_unevictable(mapping);
	if (sg != st->sgl) {
		struct pagevec pvec;

		pagevec_init(&pvec);
		for_each_sgt_page(page, sgt_iter, st) {
			if (!pagevec_add(&pvec, page))
				check_release_pagevec(&pvec);
		}
		if (pagevec_count(&pvec))
			check_release_pagevec(&pvec);
	}
	sg_free_table(st);
	kfree(st);

	/*
	 * shmemfs first checks if there is enough memory to allocate the page
	 * and reports ENOSPC should there be insufficient, along with the usual
	 * ENOMEM for a genuine allocation failure.
	 *
	 * We use ENOSPC in our driver to mean that we have run out of aperture
	 * space and so want to translate the error from shmemfs back to our
	 * usual understanding of ENOMEM.
	 */
	if (ret == -ENOSPC)
		ret = -ENOMEM;

	return ret;
}

static void
shmem_truncate(struct drm_i915_gem_object *obj)
{
	/*
	 * Our goal here is to return as much of the memory as
	 * is possible back to the system as we are called from OOM.
	 * To do this we must instruct the shmfs to drop all of its
	 * backing pages, *now*.
	 */
	shmem_truncate_range(file_inode(obj->base.filp), 0, (loff_t)-1);
	obj->mm.madv = __I915_MADV_PURGED;
	obj->mm.pages = ERR_PTR(-EFAULT);
}

static void
shmem_writeback(struct drm_i915_gem_object *obj)
{
	struct address_space *mapping;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_NONE,
		.nr_to_write = SWAP_CLUSTER_MAX,
		.range_start = 0,
		.range_end = LLONG_MAX,
		.for_reclaim = 1,
	};
	unsigned long i;

	/*
	 * Leave mmapings intact (GTT will have been revoked on unbinding,
	 * leaving only CPU mmapings around) and add those pages to the LRU
	 * instead of invoking writeback so they are aged and paged out
	 * as normal.
	 */
	mapping = obj->base.filp->f_mapping;

	/* Begin writeback on each dirty page */
	for (i = 0; i < obj->base.size >> PAGE_SHIFT; i++) {
		struct page *page;

		page = find_lock_page(mapping, i);
		if (!page)
			continue;

		if (!page_mapped(page) && clear_page_dirty_for_io(page)) {
			int ret;

			SetPageReclaim(page);
			ret = mapping->a_ops->writepage(page, &wbc);
			if (!PageWriteback(page))
				ClearPageReclaim(page);
			if (!ret)
				goto put;
		}
		unlock_page(page);
put:
		put_page(page);
	}
}

void
__i915_gem_object_release_shmem(struct drm_i915_gem_object *obj,
				struct sg_table *pages,
				bool needs_clflush)
{
	GEM_BUG_ON(obj->mm.madv == __I915_MADV_PURGED);

	if (obj->mm.madv == I915_MADV_DONTNEED)
		obj->mm.dirty = false;

	if (needs_clflush &&
	    (obj->read_domains & I915_GEM_DOMAIN_CPU) == 0 &&
	    !(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_READ))
		drm_clflush_sg(pages);

	__start_cpu_write(obj);
}

static void
shmem_put_pages(struct drm_i915_gem_object *obj, struct sg_table *pages)
{
	struct sgt_iter sgt_iter;
	struct pagevec pvec;
	struct page *page;

	__i915_gem_object_release_shmem(obj, pages, true);

	i915_gem_gtt_finish_pages(obj, pages);

	if (i915_gem_object_needs_bit17_swizzle(obj))
		i915_gem_object_save_bit_17_swizzle(obj, pages);

	mapping_clear_unevictable(file_inode(obj->base.filp)->i_mapping);

	pagevec_init(&pvec);
	for_each_sgt_page(page, sgt_iter, pages) {
		if (obj->mm.dirty)
			set_page_dirty(page);

		if (obj->mm.madv == I915_MADV_WILLNEED)
			mark_page_accessed(page);

		if (!pagevec_add(&pvec, page))
			check_release_pagevec(&pvec);
	}
	if (pagevec_count(&pvec))
		check_release_pagevec(&pvec);
	obj->mm.dirty = false;

	sg_free_table(pages);
	kfree(pages);
}

static int
shmem_pwrite(struct drm_i915_gem_object *obj,
	     const struct drm_i915_gem_pwrite *arg)
{
	struct address_space *mapping = obj->base.filp->f_mapping;
	char __user *user_data = u64_to_user_ptr(arg->data_ptr);
	u64 remain, offset;
	unsigned int pg;

	/* Caller already validated user args */
	GEM_BUG_ON(!access_ok(user_data, arg->size));

	/*
	 * Before we instantiate/pin the backing store for our use, we
	 * can prepopulate the shmemfs filp efficiently using a write into
	 * the pagecache. We avoid the penalty of instantiating all the
	 * pages, important if the user is just writing to a few and never
	 * uses the object on the GPU, and using a direct write into shmemfs
	 * allows it to avoid the cost of retrieving a page (either swapin
	 * or clearing-before-use) before it is overwritten.
	 */
	if (i915_gem_object_has_pages(obj))
		return -ENODEV;

	if (obj->mm.madv != I915_MADV_WILLNEED)
		return -EFAULT;

	/*
	 * Before the pages are instantiated the object is treated as being
	 * in the CPU domain. The pages will be clflushed as required before
	 * use, and we can freely write into the pages directly. If userspace
	 * races pwrite with any other operation; corruption will ensue -
	 * that is userspace's prerogative!
	 */

	remain = arg->size;
	offset = arg->offset;
	pg = offset_in_page(offset);

	do {
		unsigned int len, unwritten;
		struct page *page;
		void *data, *vaddr;
		int err;
		char c;

		len = PAGE_SIZE - pg;
		if (len > remain)
			len = remain;

		/* Prefault the user page to reduce potential recursion */
		err = __get_user(c, user_data);
		if (err)
			return err;

		err = __get_user(c, user_data + len - 1);
		if (err)
			return err;

		err = pagecache_write_begin(obj->base.filp, mapping,
					    offset, len, 0,
					    &page, &data);
		if (err < 0)
			return err;

		vaddr = kmap_atomic(page);
		unwritten = __copy_from_user_inatomic(vaddr + pg,
						      user_data,
						      len);
		kunmap_atomic(vaddr);

		err = pagecache_write_end(obj->base.filp, mapping,
					  offset, len, len - unwritten,
					  page, data);
		if (err < 0)
			return err;

		/* We don't handle -EFAULT, leave it to the caller to check */
		if (unwritten)
			return -ENODEV;

		remain -= len;
		user_data += len;
		offset += len;
		pg = 0;
	} while (remain);

	return 0;
}

static void shmem_release(struct drm_i915_gem_object *obj)
{
	i915_gem_object_release_memory_region(obj);

	fput(obj->base.filp);
}

const struct drm_i915_gem_object_ops i915_gem_shmem_ops = {
	.name = "i915_gem_object_shmem",
	.flags = I915_GEM_OBJECT_HAS_STRUCT_PAGE |
		 I915_GEM_OBJECT_IS_SHRINKABLE,

	.get_pages = shmem_get_pages,
	.put_pages = shmem_put_pages,
	.truncate = shmem_truncate,
	.writeback = shmem_writeback,

	.pwrite = shmem_pwrite,

	.release = shmem_release,
};

static int __create_shmem(struct drm_i915_private *i915,
			  struct drm_gem_object *obj,
			  resource_size_t size)
{
	unsigned long flags = VM_NORESERVE;
	struct file *filp;

	drm_gem_private_object_init(&i915->drm, obj, size);

	if (i915->mm.gemfs)
		filp = shmem_file_setup_with_mnt(i915->mm.gemfs, "i915", size,
						 flags);
	else
		filp = shmem_file_setup("i915", size, flags);
	if (IS_ERR(filp))
		return PTR_ERR(filp);

	obj->filp = filp;
	return 0;
}

static int shmem_object_init(struct intel_memory_region *mem,
			     struct drm_i915_gem_object *obj,
			     resource_size_t size,
			     unsigned int flags)
{
	static struct lock_class_key lock_class;
	struct drm_i915_private *i915 = mem->i915;
	struct address_space *mapping;
	unsigned int cache_level;
	gfp_t mask;
	int ret;

	ret = __create_shmem(i915, &obj->base, size);
	if (ret)
		return ret;

	mask = GFP_HIGHUSER | __GFP_RECLAIMABLE;
	if (IS_I965GM(i915) || IS_I965G(i915)) {
		/* 965gm cannot relocate objects above 4GiB. */
		mask &= ~__GFP_HIGHMEM;
		mask |= __GFP_DMA32;
	}

	mapping = obj->base.filp->f_mapping;
	mapping_set_gfp_mask(mapping, mask);
	GEM_BUG_ON(!(mapping_gfp_mask(mapping) & __GFP_RECLAIM));

	i915_gem_object_init(obj, &i915_gem_shmem_ops, &lock_class);

	obj->write_domain = I915_GEM_DOMAIN_CPU;
	obj->read_domains = I915_GEM_DOMAIN_CPU;

	if (HAS_LLC(i915))
		/* On some devices, we can have the GPU use the LLC (the CPU
		 * cache) for about a 10% performance improvement
		 * compared to uncached.  Graphics requests other than
		 * display scanout are coherent with the CPU in
		 * accessing this cache.  This means in this mode we
		 * don't need to clflush on the CPU side, and on the
		 * GPU side we only need to flush internal caches to
		 * get data visible to the CPU.
		 *
		 * However, we maintain the display planes as UC, and so
		 * need to rebind when first used as such.
		 */
		cache_level = I915_CACHE_LLC;
	else
		cache_level = I915_CACHE_NONE;

	i915_gem_object_set_cache_coherency(obj, cache_level);

	i915_gem_object_init_memory_region(obj, mem, 0);

	return 0;
}

struct drm_i915_gem_object *
i915_gem_object_create_shmem(struct drm_i915_private *i915,
			     resource_size_t size)
{
	return i915_gem_object_create_region(i915->mm.regions[INTEL_REGION_SMEM],
					     size, 0);
}

/* Allocate a new GEM object and fill it with the supplied data */
struct drm_i915_gem_object *
i915_gem_object_create_shmem_from_data(struct drm_i915_private *dev_priv,
				       const void *data, resource_size_t size)
{
	struct drm_i915_gem_object *obj;
	struct file *file;
	resource_size_t offset;
	int err;

	obj = i915_gem_object_create_shmem(dev_priv, round_up(size, PAGE_SIZE));
	if (IS_ERR(obj))
		return obj;

	GEM_BUG_ON(obj->write_domain != I915_GEM_DOMAIN_CPU);

	file = obj->base.filp;
	offset = 0;
	do {
		unsigned int len = min_t(typeof(size), size, PAGE_SIZE);
		struct page *page;
		void *pgdata, *vaddr;

		err = pagecache_write_begin(file, file->f_mapping,
					    offset, len, 0,
					    &page, &pgdata);
		if (err < 0)
			goto fail;

		vaddr = kmap(page);
		memcpy(vaddr, data, len);
		kunmap(page);

		err = pagecache_write_end(file, file->f_mapping,
					  offset, len, len,
					  page, pgdata);
		if (err < 0)
			goto fail;

		size -= len;
		data += len;
		offset += len;
	} while (size);

	return obj;

fail:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}

static int init_shmem(struct intel_memory_region *mem)
{
	int err;

	err = i915_gemfs_init(mem->i915);
	if (err) {
		DRM_NOTE("Unable to create a private tmpfs mount, hugepage support will be disabled(%d).\n",
			 err);
	}

	intel_memory_region_set_name(mem, "system");

	return 0; /* Don't error, we can simply fallback to the kernel mnt */
}

static void release_shmem(struct intel_memory_region *mem)
{
	i915_gemfs_fini(mem->i915);
}

static const struct intel_memory_region_ops shmem_region_ops = {
	.init = init_shmem,
	.release = release_shmem,
	.init_object = shmem_object_init,
};

struct intel_memory_region *i915_gem_shmem_setup(struct drm_i915_private *i915)
{
	return intel_memory_region_create(i915, 0,
					  totalram_pages() << PAGE_SHIFT,
					  PAGE_SIZE, 0,
					  &shmem_region_ops);
}

bool i915_gem_object_is_shmem(const struct drm_i915_gem_object *obj)
{
	return obj->ops == &i915_gem_shmem_ops;
}
