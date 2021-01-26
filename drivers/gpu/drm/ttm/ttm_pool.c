// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König
 */

/* Pooling of allocated pages is necessary because changing the caching
 * attributes on x86 of the linear mapping requires a costly cross CPU TLB
 * invalidate for those addresses.
 *
 * Additional to that allocations from the DMA coherent API are pooled as well
 * cause they are rather slow compared to alloc_pages+map.
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>

#ifdef CONFIG_X86
#include <asm/set_memory.h>
#endif

#include <drm/ttm/ttm_pool.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_tt.h>

/**
 * struct ttm_pool_dma - Helper object for coherent DMA mappings
 *
 * @addr: original DMA address returned for the mapping
 * @vaddr: original vaddr return for the mapping and order in the lower bits
 */
struct ttm_pool_dma {
	dma_addr_t addr;
	unsigned long vaddr;
};

static unsigned long page_pool_size;

MODULE_PARM_DESC(page_pool_size, "Number of pages in the WC/UC/DMA pool");
module_param(page_pool_size, ulong, 0644);

static atomic_long_t allocated_pages;

static struct ttm_pool_type global_write_combined[MAX_ORDER];
static struct ttm_pool_type global_uncached[MAX_ORDER];

static struct ttm_pool_type global_dma32_write_combined[MAX_ORDER];
static struct ttm_pool_type global_dma32_uncached[MAX_ORDER];

static struct mutex shrinker_lock;
static struct list_head shrinker_list;
static struct shrinker mm_shrinker;

/* Allocate pages of size 1 << order with the given gfp_flags */
static struct page *ttm_pool_alloc_page(struct ttm_pool *pool, gfp_t gfp_flags,
					unsigned int order)
{
	unsigned long attr = DMA_ATTR_FORCE_CONTIGUOUS;
	struct ttm_pool_dma *dma;
	struct page *p;
	void *vaddr;

	if (order) {
		gfp_flags |= GFP_TRANSHUGE_LIGHT | __GFP_NORETRY |
			__GFP_KSWAPD_RECLAIM;
		gfp_flags &= ~__GFP_MOVABLE;
		gfp_flags &= ~__GFP_COMP;
	}

	if (!pool->use_dma_alloc) {
		p = alloc_pages(gfp_flags, order);
		if (p)
			p->private = order;
		return p;
	}

	dma = kmalloc(sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return NULL;

	if (order)
		attr |= DMA_ATTR_NO_WARN;

	vaddr = dma_alloc_attrs(pool->dev, (1ULL << order) * PAGE_SIZE,
				&dma->addr, gfp_flags, attr);
	if (!vaddr)
		goto error_free;

	/* TODO: This is an illegal abuse of the DMA API, but we need to rework
	 * TTM page fault handling and extend the DMA API to clean this up.
	 */
	if (is_vmalloc_addr(vaddr))
		p = vmalloc_to_page(vaddr);
	else
		p = virt_to_page(vaddr);

	dma->vaddr = (unsigned long)vaddr | order;
	p->private = (unsigned long)dma;
	return p;

error_free:
	kfree(dma);
	return NULL;
}

/* Reset the caching and pages of size 1 << order */
static void ttm_pool_free_page(struct ttm_pool *pool, enum ttm_caching caching,
			       unsigned int order, struct page *p)
{
	unsigned long attr = DMA_ATTR_FORCE_CONTIGUOUS;
	struct ttm_pool_dma *dma;
	void *vaddr;

#ifdef CONFIG_X86
	/* We don't care that set_pages_wb is inefficient here. This is only
	 * used when we have to shrink and CPU overhead is irrelevant then.
	 */
	if (caching != ttm_cached && !PageHighMem(p))
		set_pages_wb(p, 1 << order);
#endif

	if (!pool || !pool->use_dma_alloc) {
		__free_pages(p, order);
		return;
	}

	if (order)
		attr |= DMA_ATTR_NO_WARN;

	dma = (void *)p->private;
	vaddr = (void *)(dma->vaddr & PAGE_MASK);
	dma_free_attrs(pool->dev, (1UL << order) * PAGE_SIZE, vaddr, dma->addr,
		       attr);
	kfree(dma);
}

/* Apply a new caching to an array of pages */
static int ttm_pool_apply_caching(struct page **first, struct page **last,
				  enum ttm_caching caching)
{
#ifdef CONFIG_X86
	unsigned int num_pages = last - first;

	if (!num_pages)
		return 0;

	switch (caching) {
	case ttm_cached:
		break;
	case ttm_write_combined:
		return set_pages_array_wc(first, num_pages);
	case ttm_uncached:
		return set_pages_array_uc(first, num_pages);
	}
#endif
	return 0;
}

/* Map pages of 1 << order size and fill the DMA address array  */
static int ttm_pool_map(struct ttm_pool *pool, unsigned int order,
			struct page *p, dma_addr_t **dma_addr)
{
	dma_addr_t addr;
	unsigned int i;

	if (pool->use_dma_alloc) {
		struct ttm_pool_dma *dma = (void *)p->private;

		addr = dma->addr;
	} else {
		size_t size = (1ULL << order) * PAGE_SIZE;

		addr = dma_map_page(pool->dev, p, 0, size, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(pool->dev, addr))
			return -EFAULT;
	}

	for (i = 1 << order; i ; --i) {
		*(*dma_addr)++ = addr;
		addr += PAGE_SIZE;
	}

	return 0;
}

/* Unmap pages of 1 << order size */
static void ttm_pool_unmap(struct ttm_pool *pool, dma_addr_t dma_addr,
			   unsigned int num_pages)
{
	/* Unmapped while freeing the page */
	if (pool->use_dma_alloc)
		return;

	dma_unmap_page(pool->dev, dma_addr, (long)num_pages << PAGE_SHIFT,
		       DMA_BIDIRECTIONAL);
}

/* Give pages into a specific pool_type */
static void ttm_pool_type_give(struct ttm_pool_type *pt, struct page *p)
{
	spin_lock(&pt->lock);
	list_add(&p->lru, &pt->pages);
	spin_unlock(&pt->lock);
	atomic_long_add(1 << pt->order, &allocated_pages);
}

/* Take pages from a specific pool_type, return NULL when nothing available */
static struct page *ttm_pool_type_take(struct ttm_pool_type *pt)
{
	struct page *p;

	spin_lock(&pt->lock);
	p = list_first_entry_or_null(&pt->pages, typeof(*p), lru);
	if (p) {
		atomic_long_sub(1 << pt->order, &allocated_pages);
		list_del(&p->lru);
	}
	spin_unlock(&pt->lock);

	return p;
}

/* Initialize and add a pool type to the global shrinker list */
static void ttm_pool_type_init(struct ttm_pool_type *pt, struct ttm_pool *pool,
			       enum ttm_caching caching, unsigned int order)
{
	pt->pool = pool;
	pt->caching = caching;
	pt->order = order;
	spin_lock_init(&pt->lock);
	INIT_LIST_HEAD(&pt->pages);

	mutex_lock(&shrinker_lock);
	list_add_tail(&pt->shrinker_list, &shrinker_list);
	mutex_unlock(&shrinker_lock);
}

/* Remove a pool_type from the global shrinker list and free all pages */
static void ttm_pool_type_fini(struct ttm_pool_type *pt)
{
	struct page *p, *tmp;

	mutex_lock(&shrinker_lock);
	list_del(&pt->shrinker_list);
	mutex_unlock(&shrinker_lock);

	list_for_each_entry_safe(p, tmp, &pt->pages, lru)
		ttm_pool_free_page(pt->pool, pt->caching, pt->order, p);
}

/* Return the pool_type to use for the given caching and order */
static struct ttm_pool_type *ttm_pool_select_type(struct ttm_pool *pool,
						  enum ttm_caching caching,
						  unsigned int order)
{
	if (pool->use_dma_alloc)
		return &pool->caching[caching].orders[order];

#ifdef CONFIG_X86
	switch (caching) {
	case ttm_write_combined:
		if (pool->use_dma32)
			return &global_dma32_write_combined[order];

		return &global_write_combined[order];
	case ttm_uncached:
		if (pool->use_dma32)
			return &global_dma32_uncached[order];

		return &global_uncached[order];
	default:
		break;
	}
#endif

	return NULL;
}

/* Free pages using the global shrinker list */
static unsigned int ttm_pool_shrink(void)
{
	struct ttm_pool_type *pt;
	unsigned int num_freed;
	struct page *p;

	mutex_lock(&shrinker_lock);
	pt = list_first_entry(&shrinker_list, typeof(*pt), shrinker_list);

	p = ttm_pool_type_take(pt);
	if (p) {
		ttm_pool_free_page(pt->pool, pt->caching, pt->order, p);
		num_freed = 1 << pt->order;
	} else {
		num_freed = 0;
	}

	list_move_tail(&pt->shrinker_list, &shrinker_list);
	mutex_unlock(&shrinker_lock);

	return num_freed;
}

/* Return the allocation order based for a page */
static unsigned int ttm_pool_page_order(struct ttm_pool *pool, struct page *p)
{
	if (pool->use_dma_alloc) {
		struct ttm_pool_dma *dma = (void *)p->private;

		return dma->vaddr & ~PAGE_MASK;
	}

	return p->private;
}

/**
 * ttm_pool_alloc - Fill a ttm_tt object
 *
 * @pool: ttm_pool to use
 * @tt: ttm_tt object to fill
 * @ctx: operation context
 *
 * Fill the ttm_tt object with pages and also make sure to DMA map them when
 * necessary.
 *
 * Returns: 0 on successe, negative error code otherwise.
 */
int ttm_pool_alloc(struct ttm_pool *pool, struct ttm_tt *tt,
		   struct ttm_operation_ctx *ctx)
{
	unsigned long num_pages = tt->num_pages;
	dma_addr_t *dma_addr = tt->dma_address;
	struct page **caching = tt->pages;
	struct page **pages = tt->pages;
	gfp_t gfp_flags = GFP_USER;
	unsigned int i, order;
	struct page *p;
	int r;

	WARN_ON(!num_pages || ttm_tt_is_populated(tt));
	WARN_ON(dma_addr && !pool->dev);

	if (tt->page_flags & TTM_PAGE_FLAG_ZERO_ALLOC)
		gfp_flags |= __GFP_ZERO;

	if (ctx->gfp_retry_mayfail)
		gfp_flags |= __GFP_RETRY_MAYFAIL;

	if (pool->use_dma32)
		gfp_flags |= GFP_DMA32;
	else
		gfp_flags |= GFP_HIGHUSER;

	for (order = min(MAX_ORDER - 1UL, __fls(num_pages)); num_pages;
	     order = min_t(unsigned int, order, __fls(num_pages))) {
		bool apply_caching = false;
		struct ttm_pool_type *pt;

		pt = ttm_pool_select_type(pool, tt->caching, order);
		p = pt ? ttm_pool_type_take(pt) : NULL;
		if (p) {
			apply_caching = true;
		} else {
			p = ttm_pool_alloc_page(pool, gfp_flags, order);
			if (p && PageHighMem(p))
				apply_caching = true;
		}

		if (!p) {
			if (order) {
				--order;
				continue;
			}
			r = -ENOMEM;
			goto error_free_all;
		}

		if (apply_caching) {
			r = ttm_pool_apply_caching(caching, pages,
						   tt->caching);
			if (r)
				goto error_free_page;
			caching = pages + (1 << order);
		}

		r = ttm_mem_global_alloc_page(&ttm_mem_glob, p,
					      (1 << order) * PAGE_SIZE,
					      ctx);
		if (r)
			goto error_free_page;

		if (dma_addr) {
			r = ttm_pool_map(pool, order, p, &dma_addr);
			if (r)
				goto error_global_free;
		}

		num_pages -= 1 << order;
		for (i = 1 << order; i; --i)
			*(pages++) = p++;
	}

	r = ttm_pool_apply_caching(caching, pages, tt->caching);
	if (r)
		goto error_free_all;

	return 0;

error_global_free:
	ttm_mem_global_free_page(&ttm_mem_glob, p, (1 << order) * PAGE_SIZE);

error_free_page:
	ttm_pool_free_page(pool, tt->caching, order, p);

error_free_all:
	num_pages = tt->num_pages - num_pages;
	for (i = 0; i < num_pages; ) {
		order = ttm_pool_page_order(pool, tt->pages[i]);
		ttm_pool_free_page(pool, tt->caching, order, tt->pages[i]);
		i += 1 << order;
	}

	return r;
}
EXPORT_SYMBOL(ttm_pool_alloc);

/**
 * ttm_pool_free - Free the backing pages from a ttm_tt object
 *
 * @pool: Pool to give pages back to.
 * @tt: ttm_tt object to unpopulate
 *
 * Give the packing pages back to a pool or free them
 */
void ttm_pool_free(struct ttm_pool *pool, struct ttm_tt *tt)
{
	unsigned int i;

	for (i = 0; i < tt->num_pages; ) {
		struct page *p = tt->pages[i];
		unsigned int order, num_pages;
		struct ttm_pool_type *pt;

		order = ttm_pool_page_order(pool, p);
		num_pages = 1ULL << order;
		ttm_mem_global_free_page(&ttm_mem_glob, p,
					 num_pages * PAGE_SIZE);
		if (tt->dma_address)
			ttm_pool_unmap(pool, tt->dma_address[i], num_pages);

		pt = ttm_pool_select_type(pool, tt->caching, order);
		if (pt)
			ttm_pool_type_give(pt, tt->pages[i]);
		else
			ttm_pool_free_page(pool, tt->caching, order,
					   tt->pages[i]);

		i += num_pages;
	}

	while (atomic_long_read(&allocated_pages) > page_pool_size)
		ttm_pool_shrink();
}
EXPORT_SYMBOL(ttm_pool_free);

/**
 * ttm_pool_init - Initialize a pool
 *
 * @pool: the pool to initialize
 * @dev: device for DMA allocations and mappings
 * @use_dma_alloc: true if coherent DMA alloc should be used
 * @use_dma32: true if GFP_DMA32 should be used
 *
 * Initialize the pool and its pool types.
 */
void ttm_pool_init(struct ttm_pool *pool, struct device *dev,
		   bool use_dma_alloc, bool use_dma32)
{
	unsigned int i, j;

	WARN_ON(!dev && use_dma_alloc);

	pool->dev = dev;
	pool->use_dma_alloc = use_dma_alloc;
	pool->use_dma32 = use_dma32;

	for (i = 0; i < TTM_NUM_CACHING_TYPES; ++i)
		for (j = 0; j < MAX_ORDER; ++j)
			ttm_pool_type_init(&pool->caching[i].orders[j],
					   pool, i, j);
}

/**
 * ttm_pool_fini - Cleanup a pool
 *
 * @pool: the pool to clean up
 *
 * Free all pages in the pool and unregister the types from the global
 * shrinker.
 */
void ttm_pool_fini(struct ttm_pool *pool)
{
	unsigned int i, j;

	for (i = 0; i < TTM_NUM_CACHING_TYPES; ++i)
		for (j = 0; j < MAX_ORDER; ++j)
			ttm_pool_type_fini(&pool->caching[i].orders[j]);
}

#ifdef CONFIG_DEBUG_FS
/* Count the number of pages available in a pool_type */
static unsigned int ttm_pool_type_count(struct ttm_pool_type *pt)
{
	unsigned int count = 0;
	struct page *p;

	spin_lock(&pt->lock);
	/* Only used for debugfs, the overhead doesn't matter */
	list_for_each_entry(p, &pt->pages, lru)
		++count;
	spin_unlock(&pt->lock);

	return count;
}

/* Dump information about the different pool types */
static void ttm_pool_debugfs_orders(struct ttm_pool_type *pt,
				    struct seq_file *m)
{
	unsigned int i;

	for (i = 0; i < MAX_ORDER; ++i)
		seq_printf(m, " %8u", ttm_pool_type_count(&pt[i]));
	seq_puts(m, "\n");
}

/**
 * ttm_pool_debugfs - Debugfs dump function for a pool
 *
 * @pool: the pool to dump the information for
 * @m: seq_file to dump to
 *
 * Make a debugfs dump with the per pool and global information.
 */
int ttm_pool_debugfs(struct ttm_pool *pool, struct seq_file *m)
{
	unsigned int i;

	mutex_lock(&shrinker_lock);

	seq_puts(m, "\t ");
	for (i = 0; i < MAX_ORDER; ++i)
		seq_printf(m, " ---%2u---", i);
	seq_puts(m, "\n");

	seq_puts(m, "wc\t:");
	ttm_pool_debugfs_orders(global_write_combined, m);
	seq_puts(m, "uc\t:");
	ttm_pool_debugfs_orders(global_uncached, m);

	seq_puts(m, "wc 32\t:");
	ttm_pool_debugfs_orders(global_dma32_write_combined, m);
	seq_puts(m, "uc 32\t:");
	ttm_pool_debugfs_orders(global_dma32_uncached, m);

	for (i = 0; i < TTM_NUM_CACHING_TYPES; ++i) {
		seq_puts(m, "DMA ");
		switch (i) {
		case ttm_cached:
			seq_puts(m, "\t:");
			break;
		case ttm_write_combined:
			seq_puts(m, "wc\t:");
			break;
		case ttm_uncached:
			seq_puts(m, "uc\t:");
			break;
		}
		ttm_pool_debugfs_orders(pool->caching[i].orders, m);
	}

	seq_printf(m, "\ntotal\t: %8lu of %8lu\n",
		   atomic_long_read(&allocated_pages), page_pool_size);

	mutex_unlock(&shrinker_lock);

	return 0;
}
EXPORT_SYMBOL(ttm_pool_debugfs);

#endif

/* As long as pages are available make sure to release at least one */
static unsigned long ttm_pool_shrinker_scan(struct shrinker *shrink,
					    struct shrink_control *sc)
{
	unsigned long num_freed = 0;

	do
		num_freed += ttm_pool_shrink();
	while (!num_freed && atomic_long_read(&allocated_pages));

	return num_freed;
}

/* Return the number of pages available or SHRINK_EMPTY if we have none */
static unsigned long ttm_pool_shrinker_count(struct shrinker *shrink,
					     struct shrink_control *sc)
{
	unsigned long num_pages = atomic_long_read(&allocated_pages);

	return num_pages ? num_pages : SHRINK_EMPTY;
}

/**
 * ttm_pool_mgr_init - Initialize globals
 *
 * @num_pages: default number of pages
 *
 * Initialize the global locks and lists for the MM shrinker.
 */
int ttm_pool_mgr_init(unsigned long num_pages)
{
	unsigned int i;

	if (!page_pool_size)
		page_pool_size = num_pages;

	mutex_init(&shrinker_lock);
	INIT_LIST_HEAD(&shrinker_list);

	for (i = 0; i < MAX_ORDER; ++i) {
		ttm_pool_type_init(&global_write_combined[i], NULL,
				   ttm_write_combined, i);
		ttm_pool_type_init(&global_uncached[i], NULL, ttm_uncached, i);

		ttm_pool_type_init(&global_dma32_write_combined[i], NULL,
				   ttm_write_combined, i);
		ttm_pool_type_init(&global_dma32_uncached[i], NULL,
				   ttm_uncached, i);
	}

	mm_shrinker.count_objects = ttm_pool_shrinker_count;
	mm_shrinker.scan_objects = ttm_pool_shrinker_scan;
	mm_shrinker.seeks = 1;
	return register_shrinker(&mm_shrinker);
}

/**
 * ttm_pool_mgr_fini - Finalize globals
 *
 * Cleanup the global pools and unregister the MM shrinker.
 */
void ttm_pool_mgr_fini(void)
{
	unsigned int i;

	for (i = 0; i < MAX_ORDER; ++i) {
		ttm_pool_type_fini(&global_write_combined[i]);
		ttm_pool_type_fini(&global_uncached[i]);

		ttm_pool_type_fini(&global_dma32_write_combined[i]);
		ttm_pool_type_fini(&global_dma32_uncached[i]);
	}

	unregister_shrinker(&mm_shrinker);
	WARN_ON(!list_empty(&shrinker_list));
}
