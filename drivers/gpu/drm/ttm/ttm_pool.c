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
#include <linux/debugfs.h>
#include <linux/highmem.h>
#include <linux/sched/mm.h>

#ifdef CONFIG_X86
#include <asm/set_memory.h>
#endif

#include <drm/ttm/ttm_pool.h>
#include <drm/ttm/ttm_tt.h>
#include <drm/ttm/ttm_bo.h>

#include "ttm_module.h"

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

static struct ttm_pool_type global_write_combined[NR_PAGE_ORDERS];
static struct ttm_pool_type global_uncached[NR_PAGE_ORDERS];

static struct ttm_pool_type global_dma32_write_combined[NR_PAGE_ORDERS];
static struct ttm_pool_type global_dma32_uncached[NR_PAGE_ORDERS];

static spinlock_t shrinker_lock;
static struct list_head shrinker_list;
static struct shrinker *mm_shrinker;
static DECLARE_RWSEM(pool_shrink_rwsem);

/* Allocate pages of size 1 << order with the given gfp_flags */
static struct page *ttm_pool_alloc_page(struct ttm_pool *pool, gfp_t gfp_flags,
					unsigned int order)
{
	unsigned long attr = DMA_ATTR_FORCE_CONTIGUOUS;
	struct ttm_pool_dma *dma;
	struct page *p;
	void *vaddr;

	/* Don't set the __GFP_COMP flag for higher order allocations.
	 * Mapping pages directly into an userspace process and calling
	 * put_page() on a TTM allocated page is illegal.
	 */
	if (order)
		gfp_flags |= __GFP_NOMEMALLOC | __GFP_NORETRY | __GFP_NOWARN |
			__GFP_KSWAPD_RECLAIM;

	if (!pool->use_dma_alloc) {
		p = alloc_pages_node(pool->nid, gfp_flags, order);
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
	unsigned int i, num_pages = 1 << pt->order;

	for (i = 0; i < num_pages; ++i) {
		if (PageHighMem(p))
			clear_highpage(p + i);
		else
			clear_page(page_address(p + i));
	}

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

	spin_lock(&shrinker_lock);
	list_add_tail(&pt->shrinker_list, &shrinker_list);
	spin_unlock(&shrinker_lock);
}

/* Remove a pool_type from the global shrinker list and free all pages */
static void ttm_pool_type_fini(struct ttm_pool_type *pt)
{
	struct page *p;

	spin_lock(&shrinker_lock);
	list_del(&pt->shrinker_list);
	spin_unlock(&shrinker_lock);

	while ((p = ttm_pool_type_take(pt)))
		ttm_pool_free_page(pt->pool, pt->caching, pt->order, p);
}

/* Return the pool_type to use for the given caching and order */
static struct ttm_pool_type *ttm_pool_select_type(struct ttm_pool *pool,
						  enum ttm_caching caching,
						  unsigned int order)
{
	if (pool->use_dma_alloc || pool->nid != NUMA_NO_NODE)
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
	unsigned int num_pages;
	struct page *p;

	down_read(&pool_shrink_rwsem);
	spin_lock(&shrinker_lock);
	pt = list_first_entry(&shrinker_list, typeof(*pt), shrinker_list);
	list_move_tail(&pt->shrinker_list, &shrinker_list);
	spin_unlock(&shrinker_lock);

	p = ttm_pool_type_take(pt);
	if (p) {
		ttm_pool_free_page(pt->pool, pt->caching, pt->order, p);
		num_pages = 1 << pt->order;
	} else {
		num_pages = 0;
	}
	up_read(&pool_shrink_rwsem);

	return num_pages;
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

/* Called when we got a page, either from a pool or newly allocated */
static int ttm_pool_page_allocated(struct ttm_pool *pool, unsigned int order,
				   struct page *p, dma_addr_t **dma_addr,
				   unsigned long *num_pages,
				   struct page ***pages)
{
	unsigned int i;
	int r;

	if (*dma_addr) {
		r = ttm_pool_map(pool, order, p, dma_addr);
		if (r)
			return r;
	}

	*num_pages -= 1 << order;
	for (i = 1 << order; i; --i, ++(*pages), ++p)
		**pages = p;

	return 0;
}

/**
 * ttm_pool_free_range() - Free a range of TTM pages
 * @pool: The pool used for allocating.
 * @tt: The struct ttm_tt holding the page pointers.
 * @caching: The page caching mode used by the range.
 * @start_page: index for first page to free.
 * @end_page: index for last page to free + 1.
 *
 * During allocation the ttm_tt page-vector may be populated with ranges of
 * pages with different attributes if allocation hit an error without being
 * able to completely fulfill the allocation. This function can be used
 * to free these individual ranges.
 */
static void ttm_pool_free_range(struct ttm_pool *pool, struct ttm_tt *tt,
				enum ttm_caching caching,
				pgoff_t start_page, pgoff_t end_page)
{
	struct page **pages = &tt->pages[start_page];
	unsigned int order;
	pgoff_t i, nr;

	for (i = start_page; i < end_page; i += nr, pages += nr) {
		struct ttm_pool_type *pt = NULL;

		order = ttm_pool_page_order(pool, *pages);
		nr = (1UL << order);
		if (tt->dma_address)
			ttm_pool_unmap(pool, tt->dma_address[i], nr);

		pt = ttm_pool_select_type(pool, caching, order);
		if (pt)
			ttm_pool_type_give(pt, *pages);
		else
			ttm_pool_free_page(pool, caching, order, *pages);
	}
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
	pgoff_t num_pages = tt->num_pages;
	dma_addr_t *dma_addr = tt->dma_address;
	struct page **caching = tt->pages;
	struct page **pages = tt->pages;
	enum ttm_caching page_caching;
	gfp_t gfp_flags = GFP_USER;
	pgoff_t caching_divide;
	unsigned int order;
	struct page *p;
	int r;

	WARN_ON(!num_pages || ttm_tt_is_populated(tt));
	WARN_ON(dma_addr && !pool->dev);

	if (tt->page_flags & TTM_TT_FLAG_ZERO_ALLOC)
		gfp_flags |= __GFP_ZERO;

	if (ctx->gfp_retry_mayfail)
		gfp_flags |= __GFP_RETRY_MAYFAIL;

	if (pool->use_dma32)
		gfp_flags |= GFP_DMA32;
	else
		gfp_flags |= GFP_HIGHUSER;

	for (order = min_t(unsigned int, MAX_PAGE_ORDER, __fls(num_pages));
	     num_pages;
	     order = min_t(unsigned int, order, __fls(num_pages))) {
		struct ttm_pool_type *pt;

		page_caching = tt->caching;
		pt = ttm_pool_select_type(pool, tt->caching, order);
		p = pt ? ttm_pool_type_take(pt) : NULL;
		if (p) {
			r = ttm_pool_apply_caching(caching, pages,
						   tt->caching);
			if (r)
				goto error_free_page;

			caching = pages;
			do {
				r = ttm_pool_page_allocated(pool, order, p,
							    &dma_addr,
							    &num_pages,
							    &pages);
				if (r)
					goto error_free_page;

				caching = pages;
				if (num_pages < (1 << order))
					break;

				p = ttm_pool_type_take(pt);
			} while (p);
		}

		page_caching = ttm_cached;
		while (num_pages >= (1 << order) &&
		       (p = ttm_pool_alloc_page(pool, gfp_flags, order))) {

			if (PageHighMem(p)) {
				r = ttm_pool_apply_caching(caching, pages,
							   tt->caching);
				if (r)
					goto error_free_page;
				caching = pages;
			}
			r = ttm_pool_page_allocated(pool, order, p, &dma_addr,
						    &num_pages, &pages);
			if (r)
				goto error_free_page;
			if (PageHighMem(p))
				caching = pages;
		}

		if (!p) {
			if (order) {
				--order;
				continue;
			}
			r = -ENOMEM;
			goto error_free_all;
		}
	}

	r = ttm_pool_apply_caching(caching, pages, tt->caching);
	if (r)
		goto error_free_all;

	return 0;

error_free_page:
	ttm_pool_free_page(pool, page_caching, order, p);

error_free_all:
	num_pages = tt->num_pages - num_pages;
	caching_divide = caching - tt->pages;
	ttm_pool_free_range(pool, tt, tt->caching, 0, caching_divide);
	ttm_pool_free_range(pool, tt, ttm_cached, caching_divide, num_pages);

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
	ttm_pool_free_range(pool, tt, tt->caching, 0, tt->num_pages);

	while (atomic_long_read(&allocated_pages) > page_pool_size)
		ttm_pool_shrink();
}
EXPORT_SYMBOL(ttm_pool_free);

/**
 * ttm_pool_init - Initialize a pool
 *
 * @pool: the pool to initialize
 * @dev: device for DMA allocations and mappings
 * @nid: NUMA node to use for allocations
 * @use_dma_alloc: true if coherent DMA alloc should be used
 * @use_dma32: true if GFP_DMA32 should be used
 *
 * Initialize the pool and its pool types.
 */
void ttm_pool_init(struct ttm_pool *pool, struct device *dev,
		   int nid, bool use_dma_alloc, bool use_dma32)
{
	unsigned int i, j;

	WARN_ON(!dev && use_dma_alloc);

	pool->dev = dev;
	pool->nid = nid;
	pool->use_dma_alloc = use_dma_alloc;
	pool->use_dma32 = use_dma32;

	if (use_dma_alloc || nid != NUMA_NO_NODE) {
		for (i = 0; i < TTM_NUM_CACHING_TYPES; ++i)
			for (j = 0; j < NR_PAGE_ORDERS; ++j)
				ttm_pool_type_init(&pool->caching[i].orders[j],
						   pool, i, j);
	}
}
EXPORT_SYMBOL(ttm_pool_init);

/**
 * ttm_pool_synchronize_shrinkers - Wait for all running shrinkers to complete.
 *
 * This is useful to guarantee that all shrinker invocations have seen an
 * update, before freeing memory, similar to rcu.
 */
static void ttm_pool_synchronize_shrinkers(void)
{
	down_write(&pool_shrink_rwsem);
	up_write(&pool_shrink_rwsem);
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

	if (pool->use_dma_alloc || pool->nid != NUMA_NO_NODE) {
		for (i = 0; i < TTM_NUM_CACHING_TYPES; ++i)
			for (j = 0; j < NR_PAGE_ORDERS; ++j)
				ttm_pool_type_fini(&pool->caching[i].orders[j]);
	}

	/* We removed the pool types from the LRU, but we need to also make sure
	 * that no shrinker is concurrently freeing pages from the pool.
	 */
	ttm_pool_synchronize_shrinkers();
}
EXPORT_SYMBOL(ttm_pool_fini);

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

/* Print a nice header for the order */
static void ttm_pool_debugfs_header(struct seq_file *m)
{
	unsigned int i;

	seq_puts(m, "\t ");
	for (i = 0; i < NR_PAGE_ORDERS; ++i)
		seq_printf(m, " ---%2u---", i);
	seq_puts(m, "\n");
}

/* Dump information about the different pool types */
static void ttm_pool_debugfs_orders(struct ttm_pool_type *pt,
				    struct seq_file *m)
{
	unsigned int i;

	for (i = 0; i < NR_PAGE_ORDERS; ++i)
		seq_printf(m, " %8u", ttm_pool_type_count(&pt[i]));
	seq_puts(m, "\n");
}

/* Dump the total amount of allocated pages */
static void ttm_pool_debugfs_footer(struct seq_file *m)
{
	seq_printf(m, "\ntotal\t: %8lu of %8lu\n",
		   atomic_long_read(&allocated_pages), page_pool_size);
}

/* Dump the information for the global pools */
static int ttm_pool_debugfs_globals_show(struct seq_file *m, void *data)
{
	ttm_pool_debugfs_header(m);

	spin_lock(&shrinker_lock);
	seq_puts(m, "wc\t:");
	ttm_pool_debugfs_orders(global_write_combined, m);
	seq_puts(m, "uc\t:");
	ttm_pool_debugfs_orders(global_uncached, m);
	seq_puts(m, "wc 32\t:");
	ttm_pool_debugfs_orders(global_dma32_write_combined, m);
	seq_puts(m, "uc 32\t:");
	ttm_pool_debugfs_orders(global_dma32_uncached, m);
	spin_unlock(&shrinker_lock);

	ttm_pool_debugfs_footer(m);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ttm_pool_debugfs_globals);

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

	if (!pool->use_dma_alloc) {
		seq_puts(m, "unused\n");
		return 0;
	}

	ttm_pool_debugfs_header(m);

	spin_lock(&shrinker_lock);
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
	spin_unlock(&shrinker_lock);

	ttm_pool_debugfs_footer(m);
	return 0;
}
EXPORT_SYMBOL(ttm_pool_debugfs);

/* Test the shrinker functions and dump the result */
static int ttm_pool_debugfs_shrink_show(struct seq_file *m, void *data)
{
	struct shrink_control sc = { .gfp_mask = GFP_NOFS };

	fs_reclaim_acquire(GFP_KERNEL);
	seq_printf(m, "%lu/%lu\n", ttm_pool_shrinker_count(mm_shrinker, &sc),
		   ttm_pool_shrinker_scan(mm_shrinker, &sc));
	fs_reclaim_release(GFP_KERNEL);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ttm_pool_debugfs_shrink);

#endif

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

	spin_lock_init(&shrinker_lock);
	INIT_LIST_HEAD(&shrinker_list);

	for (i = 0; i < NR_PAGE_ORDERS; ++i) {
		ttm_pool_type_init(&global_write_combined[i], NULL,
				   ttm_write_combined, i);
		ttm_pool_type_init(&global_uncached[i], NULL, ttm_uncached, i);

		ttm_pool_type_init(&global_dma32_write_combined[i], NULL,
				   ttm_write_combined, i);
		ttm_pool_type_init(&global_dma32_uncached[i], NULL,
				   ttm_uncached, i);
	}

#ifdef CONFIG_DEBUG_FS
	debugfs_create_file("page_pool", 0444, ttm_debugfs_root, NULL,
			    &ttm_pool_debugfs_globals_fops);
	debugfs_create_file("page_pool_shrink", 0400, ttm_debugfs_root, NULL,
			    &ttm_pool_debugfs_shrink_fops);
#endif

	mm_shrinker = shrinker_alloc(0, "drm-ttm_pool");
	if (!mm_shrinker)
		return -ENOMEM;

	mm_shrinker->count_objects = ttm_pool_shrinker_count;
	mm_shrinker->scan_objects = ttm_pool_shrinker_scan;
	mm_shrinker->seeks = 1;

	shrinker_register(mm_shrinker);

	return 0;
}

/**
 * ttm_pool_mgr_fini - Finalize globals
 *
 * Cleanup the global pools and unregister the MM shrinker.
 */
void ttm_pool_mgr_fini(void)
{
	unsigned int i;

	for (i = 0; i < NR_PAGE_ORDERS; ++i) {
		ttm_pool_type_fini(&global_write_combined[i]);
		ttm_pool_type_fini(&global_uncached[i]);

		ttm_pool_type_fini(&global_dma32_write_combined[i]);
		ttm_pool_type_fini(&global_dma32_uncached[i]);
	}

	shrinker_free(mm_shrinker);
	WARN_ON(!list_empty(&shrinker_list));
}
