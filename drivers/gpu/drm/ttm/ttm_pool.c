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

#include <drm/ttm/ttm_backup.h>
#include <drm/ttm/ttm_pool.h>
#include <drm/ttm/ttm_tt.h>
#include <drm/ttm/ttm_bo.h>

#include "ttm_module.h"

#ifdef CONFIG_FAULT_INJECTION
#include <linux/fault-inject.h>
static DECLARE_FAULT_ATTR(backup_fault_inject);
#else
#define should_fail(...) false
#endif

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

/**
 * struct ttm_pool_alloc_state - Current state of the tt page allocation process
 * @pages: Pointer to the next tt page pointer to populate.
 * @caching_divide: Pointer to the first page pointer whose page has a staged but
 * not committed caching transition from write-back to @tt_caching.
 * @dma_addr: Pointer to the next tt dma_address entry to populate if any.
 * @remaining_pages: Remaining pages to populate.
 * @tt_caching: The requested cpu-caching for the pages allocated.
 */
struct ttm_pool_alloc_state {
	struct page **pages;
	struct page **caching_divide;
	dma_addr_t *dma_addr;
	pgoff_t remaining_pages;
	enum ttm_caching tt_caching;
};

/**
 * struct ttm_pool_tt_restore - State representing restore from backup
 * @pool: The pool used for page allocation while restoring.
 * @snapshot_alloc: A snapshot of the most recent struct ttm_pool_alloc_state.
 * @alloced_page: Pointer to the page most recently allocated from a pool or system.
 * @first_dma: The dma address corresponding to @alloced_page if dma_mapping
 * is requested.
 * @alloced_pages: The number of allocated pages present in the struct ttm_tt
 * page vector from this restore session.
 * @restored_pages: The number of 4K pages restored for @alloced_page (which
 * is typically a multi-order page).
 * @page_caching: The struct ttm_tt requested caching
 * @order: The order of @alloced_page.
 *
 * Recovery from backup might fail when we've recovered less than the
 * full ttm_tt. In order not to loose any data (yet), keep information
 * around that allows us to restart a failed ttm backup recovery.
 */
struct ttm_pool_tt_restore {
	struct ttm_pool *pool;
	struct ttm_pool_alloc_state snapshot_alloc;
	struct page *alloced_page;
	dma_addr_t first_dma;
	pgoff_t alloced_pages;
	pgoff_t restored_pages;
	enum ttm_caching page_caching;
	unsigned int order;
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
			__GFP_THISNODE;

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

/* Apply any cpu-caching deferred during page allocation */
static int ttm_pool_apply_caching(struct ttm_pool_alloc_state *alloc)
{
#ifdef CONFIG_X86
	unsigned int num_pages = alloc->pages - alloc->caching_divide;

	if (!num_pages)
		return 0;

	switch (alloc->tt_caching) {
	case ttm_cached:
		break;
	case ttm_write_combined:
		return set_pages_array_wc(alloc->caching_divide, num_pages);
	case ttm_uncached:
		return set_pages_array_uc(alloc->caching_divide, num_pages);
	}
#endif
	alloc->caching_divide = alloc->pages;
	return 0;
}

/* DMA Map pages of 1 << order size and return the resulting dma_address. */
static int ttm_pool_map(struct ttm_pool *pool, unsigned int order,
			struct page *p, dma_addr_t *dma_addr)
{
	dma_addr_t addr;

	if (pool->use_dma_alloc) {
		struct ttm_pool_dma *dma = (void *)p->private;

		addr = dma->addr;
	} else {
		size_t size = (1ULL << order) * PAGE_SIZE;

		addr = dma_map_page(pool->dev, p, 0, size, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(pool->dev, addr))
			return -EFAULT;
	}

	*dma_addr = addr;

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
	if (pool->use_dma_alloc)
		return &pool->caching[caching].orders[order];

#ifdef CONFIG_X86
	switch (caching) {
	case ttm_write_combined:
		if (pool->nid != NUMA_NO_NODE)
			return &pool->caching[caching].orders[order];

		if (pool->use_dma32)
			return &global_dma32_write_combined[order];

		return &global_write_combined[order];
	case ttm_uncached:
		if (pool->nid != NUMA_NO_NODE)
			return &pool->caching[caching].orders[order];

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

/*
 * Split larger pages so that we can free each PAGE_SIZE page as soon
 * as it has been backed up, in order to avoid memory pressure during
 * reclaim.
 */
static void ttm_pool_split_for_swap(struct ttm_pool *pool, struct page *p)
{
	unsigned int order = ttm_pool_page_order(pool, p);
	pgoff_t nr;

	if (!order)
		return;

	split_page(p, order);
	nr = 1UL << order;
	while (nr--)
		(p++)->private = 0;
}

/**
 * DOC: Partial backup and restoration of a struct ttm_tt.
 *
 * Swapout using ttm_backup_backup_page() and swapin using
 * ttm_backup_copy_page() may fail.
 * The former most likely due to lack of swap-space or memory, the latter due
 * to lack of memory or because of signal interruption during waits.
 *
 * Backup failure is easily handled by using a ttm_tt pages vector that holds
 * both backup handles and page pointers. This has to be taken into account when
 * restoring such a ttm_tt from backup, and when freeing it while backed up.
 * When restoring, for simplicity, new pages are actually allocated from the
 * pool and the contents of any old pages are copied in and then the old pages
 * are released.
 *
 * For restoration failures, the struct ttm_pool_tt_restore holds sufficient state
 * to be able to resume an interrupted restore, and that structure is freed once
 * the restoration is complete. If the struct ttm_tt is destroyed while there
 * is a valid struct ttm_pool_tt_restore attached, that is also properly taken
 * care of.
 */

/* Is restore ongoing for the currently allocated page? */
static bool ttm_pool_restore_valid(const struct ttm_pool_tt_restore *restore)
{
	return restore && restore->restored_pages < (1 << restore->order);
}

/* DMA unmap and free a multi-order page, either to the relevant pool or to system. */
static pgoff_t ttm_pool_unmap_and_free(struct ttm_pool *pool, struct page *page,
				       const dma_addr_t *dma_addr, enum ttm_caching caching)
{
	struct ttm_pool_type *pt = NULL;
	unsigned int order;
	pgoff_t nr;

	if (pool) {
		order = ttm_pool_page_order(pool, page);
		nr = (1UL << order);
		if (dma_addr)
			ttm_pool_unmap(pool, *dma_addr, nr);

		pt = ttm_pool_select_type(pool, caching, order);
	} else {
		order = page->private;
		nr = (1UL << order);
	}

	if (pt)
		ttm_pool_type_give(pt, page);
	else
		ttm_pool_free_page(pool, caching, order, page);

	return nr;
}

/* Populate the page-array using the most recent allocated multi-order page. */
static void ttm_pool_allocated_page_commit(struct page *allocated,
					   dma_addr_t first_dma,
					   struct ttm_pool_alloc_state *alloc,
					   pgoff_t nr)
{
	pgoff_t i;

	for (i = 0; i < nr; ++i)
		*alloc->pages++ = allocated++;

	alloc->remaining_pages -= nr;

	if (!alloc->dma_addr)
		return;

	for (i = 0; i < nr; ++i) {
		*alloc->dma_addr++ = first_dma;
		first_dma += PAGE_SIZE;
	}
}

/*
 * When restoring, restore backed-up content to the newly allocated page and
 * if successful, populate the page-table and dma-address arrays.
 */
static int ttm_pool_restore_commit(struct ttm_pool_tt_restore *restore,
				   struct ttm_backup *backup,
				   const struct ttm_operation_ctx *ctx,
				   struct ttm_pool_alloc_state *alloc)

{
	pgoff_t i, nr = 1UL << restore->order;
	struct page **first_page = alloc->pages;
	struct page *p;
	int ret = 0;

	for (i = restore->restored_pages; i < nr; ++i) {
		p = first_page[i];
		if (ttm_backup_page_ptr_is_handle(p)) {
			unsigned long handle = ttm_backup_page_ptr_to_handle(p);

			if (IS_ENABLED(CONFIG_FAULT_INJECTION) && ctx->interruptible &&
			    should_fail(&backup_fault_inject, 1)) {
				ret = -EINTR;
				break;
			}

			if (handle == 0) {
				restore->restored_pages++;
				continue;
			}

			ret = ttm_backup_copy_page(backup, restore->alloced_page + i,
						   handle, ctx->interruptible);
			if (ret)
				break;

			ttm_backup_drop(backup, handle);
		} else if (p) {
			/*
			 * We could probably avoid splitting the old page
			 * using clever logic, but ATM we don't care, as
			 * we prioritize releasing memory ASAP. Note that
			 * here, the old retained page is always write-back
			 * cached.
			 */
			ttm_pool_split_for_swap(restore->pool, p);
			copy_highpage(restore->alloced_page + i, p);
			__free_pages(p, 0);
		}

		restore->restored_pages++;
		first_page[i] = ttm_backup_handle_to_page_ptr(0);
	}

	if (ret) {
		if (!restore->restored_pages) {
			dma_addr_t *dma_addr = alloc->dma_addr ? &restore->first_dma : NULL;

			ttm_pool_unmap_and_free(restore->pool, restore->alloced_page,
						dma_addr, restore->page_caching);
			restore->restored_pages = nr;
		}
		return ret;
	}

	ttm_pool_allocated_page_commit(restore->alloced_page, restore->first_dma,
				       alloc, nr);
	if (restore->page_caching == alloc->tt_caching || PageHighMem(restore->alloced_page))
		alloc->caching_divide = alloc->pages;
	restore->snapshot_alloc = *alloc;
	restore->alloced_pages += nr;

	return 0;
}

/* If restoring, save information needed for ttm_pool_restore_commit(). */
static void
ttm_pool_page_allocated_restore(struct ttm_pool *pool, unsigned int order,
				struct page *p,
				enum ttm_caching page_caching,
				dma_addr_t first_dma,
				struct ttm_pool_tt_restore *restore,
				const struct ttm_pool_alloc_state *alloc)
{
	restore->pool = pool;
	restore->order = order;
	restore->restored_pages = 0;
	restore->page_caching = page_caching;
	restore->first_dma = first_dma;
	restore->alloced_page = p;
	restore->snapshot_alloc = *alloc;
}

/*
 * Called when we got a page, either from a pool or newly allocated.
 * if needed, dma map the page and populate the dma address array.
 * Populate the page address array.
 * If the caching is consistent, update any deferred caching. Otherwise
 * stage this page for an upcoming deferred caching update.
 */
static int ttm_pool_page_allocated(struct ttm_pool *pool, unsigned int order,
				   struct page *p, enum ttm_caching page_caching,
				   struct ttm_pool_alloc_state *alloc,
				   struct ttm_pool_tt_restore *restore)
{
	bool caching_consistent;
	dma_addr_t first_dma;
	int r = 0;

	caching_consistent = (page_caching == alloc->tt_caching) || PageHighMem(p);

	if (caching_consistent) {
		r = ttm_pool_apply_caching(alloc);
		if (r)
			return r;
	}

	if (alloc->dma_addr) {
		r = ttm_pool_map(pool, order, p, &first_dma);
		if (r)
			return r;
	}

	if (restore) {
		ttm_pool_page_allocated_restore(pool, order, p, page_caching,
						first_dma, restore, alloc);
	} else {
		ttm_pool_allocated_page_commit(p, first_dma, alloc, 1UL << order);

		if (caching_consistent)
			alloc->caching_divide = alloc->pages;
	}

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
	struct ttm_backup *backup = tt->backup;
	pgoff_t i, nr;

	for (i = start_page; i < end_page; i += nr, pages += nr) {
		struct page *p = *pages;

		nr = 1;
		if (ttm_backup_page_ptr_is_handle(p)) {
			unsigned long handle = ttm_backup_page_ptr_to_handle(p);

			if (handle != 0)
				ttm_backup_drop(backup, handle);
		} else if (p) {
			dma_addr_t *dma_addr = tt->dma_address ?
				tt->dma_address + i : NULL;

			nr = ttm_pool_unmap_and_free(pool, p, dma_addr, caching);
		}
	}
}

static void ttm_pool_alloc_state_init(const struct ttm_tt *tt,
				      struct ttm_pool_alloc_state *alloc)
{
	alloc->pages = tt->pages;
	alloc->caching_divide = tt->pages;
	alloc->dma_addr = tt->dma_address;
	alloc->remaining_pages = tt->num_pages;
	alloc->tt_caching = tt->caching;
}

/*
 * Find a suitable allocation order based on highest desired order
 * and number of remaining pages
 */
static unsigned int ttm_pool_alloc_find_order(unsigned int highest,
					      const struct ttm_pool_alloc_state *alloc)
{
	return min_t(unsigned int, highest, __fls(alloc->remaining_pages));
}

static int __ttm_pool_alloc(struct ttm_pool *pool, struct ttm_tt *tt,
			    const struct ttm_operation_ctx *ctx,
			    struct ttm_pool_alloc_state *alloc,
			    struct ttm_pool_tt_restore *restore)
{
	enum ttm_caching page_caching;
	gfp_t gfp_flags = GFP_USER;
	pgoff_t caching_divide;
	unsigned int order;
	bool allow_pools;
	struct page *p;
	int r;

	WARN_ON(!alloc->remaining_pages || ttm_tt_is_populated(tt));
	WARN_ON(alloc->dma_addr && !pool->dev);

	if (tt->page_flags & TTM_TT_FLAG_ZERO_ALLOC)
		gfp_flags |= __GFP_ZERO;

	if (ctx->gfp_retry_mayfail)
		gfp_flags |= __GFP_RETRY_MAYFAIL;

	if (pool->use_dma32)
		gfp_flags |= GFP_DMA32;
	else
		gfp_flags |= GFP_HIGHUSER;

	page_caching = tt->caching;
	allow_pools = true;
	for (order = ttm_pool_alloc_find_order(MAX_PAGE_ORDER, alloc);
	     alloc->remaining_pages;
	     order = ttm_pool_alloc_find_order(order, alloc)) {
		struct ttm_pool_type *pt;

		/* First, try to allocate a page from a pool if one exists. */
		p = NULL;
		pt = ttm_pool_select_type(pool, page_caching, order);
		if (pt && allow_pools)
			p = ttm_pool_type_take(pt);
		/*
		 * If that fails or previously failed, allocate from system.
		 * Note that this also disallows additional pool allocations using
		 * write-back cached pools of the same order. Consider removing
		 * that behaviour.
		 */
		if (!p) {
			page_caching = ttm_cached;
			allow_pools = false;
			p = ttm_pool_alloc_page(pool, gfp_flags, order);
		}
		/* If that fails, lower the order if possible and retry. */
		if (!p) {
			if (order) {
				--order;
				page_caching = tt->caching;
				allow_pools = true;
				continue;
			}
			r = -ENOMEM;
			goto error_free_all;
		}
		r = ttm_pool_page_allocated(pool, order, p, page_caching, alloc,
					    restore);
		if (r)
			goto error_free_page;

		if (ttm_pool_restore_valid(restore)) {
			r = ttm_pool_restore_commit(restore, tt->backup, ctx, alloc);
			if (r)
				goto error_free_all;
		}
	}

	r = ttm_pool_apply_caching(alloc);
	if (r)
		goto error_free_all;

	kfree(tt->restore);
	tt->restore = NULL;

	return 0;

error_free_page:
	ttm_pool_free_page(pool, page_caching, order, p);

error_free_all:
	if (tt->restore)
		return r;

	caching_divide = alloc->caching_divide - tt->pages;
	ttm_pool_free_range(pool, tt, tt->caching, 0, caching_divide);
	ttm_pool_free_range(pool, tt, ttm_cached, caching_divide,
			    tt->num_pages - alloc->remaining_pages);

	return r;
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
	struct ttm_pool_alloc_state alloc;

	if (WARN_ON(ttm_tt_is_backed_up(tt)))
		return -EINVAL;

	ttm_pool_alloc_state_init(tt, &alloc);

	return __ttm_pool_alloc(pool, tt, ctx, &alloc, NULL);
}
EXPORT_SYMBOL(ttm_pool_alloc);

/**
 * ttm_pool_restore_and_alloc - Fill a ttm_tt, restoring previously backed-up
 * content.
 *
 * @pool: ttm_pool to use
 * @tt: ttm_tt object to fill
 * @ctx: operation context
 *
 * Fill the ttm_tt object with pages and also make sure to DMA map them when
 * necessary. Read in backed-up content.
 *
 * Returns: 0 on successe, negative error code otherwise.
 */
int ttm_pool_restore_and_alloc(struct ttm_pool *pool, struct ttm_tt *tt,
			       const struct ttm_operation_ctx *ctx)
{
	struct ttm_pool_alloc_state alloc;

	if (WARN_ON(!ttm_tt_is_backed_up(tt)))
		return -EINVAL;

	if (!tt->restore) {
		gfp_t gfp = GFP_KERNEL | __GFP_NOWARN;

		ttm_pool_alloc_state_init(tt, &alloc);
		if (ctx->gfp_retry_mayfail)
			gfp |= __GFP_RETRY_MAYFAIL;

		tt->restore = kzalloc(sizeof(*tt->restore), gfp);
		if (!tt->restore)
			return -ENOMEM;

		tt->restore->snapshot_alloc = alloc;
		tt->restore->pool = pool;
		tt->restore->restored_pages = 1;
	} else {
		struct ttm_pool_tt_restore *restore = tt->restore;
		int ret;

		alloc = restore->snapshot_alloc;
		if (ttm_pool_restore_valid(tt->restore)) {
			ret = ttm_pool_restore_commit(restore, tt->backup, ctx, &alloc);
			if (ret)
				return ret;
		}
		if (!alloc.remaining_pages)
			return 0;
	}

	return __ttm_pool_alloc(pool, tt, ctx, &alloc, tt->restore);
}

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
 * ttm_pool_drop_backed_up() - Release content of a swapped-out struct ttm_tt
 * @tt: The struct ttm_tt.
 *
 * Release handles with associated content or any remaining pages of
 * a backed-up struct ttm_tt.
 */
void ttm_pool_drop_backed_up(struct ttm_tt *tt)
{
	struct ttm_pool_tt_restore *restore;
	pgoff_t start_page = 0;

	WARN_ON(!ttm_tt_is_backed_up(tt));

	restore = tt->restore;

	/*
	 * Unmap and free any uncommitted restore page.
	 * any tt page-array backup entries already read back has
	 * been cleared already
	 */
	if (ttm_pool_restore_valid(restore)) {
		dma_addr_t *dma_addr = tt->dma_address ? &restore->first_dma : NULL;

		ttm_pool_unmap_and_free(restore->pool, restore->alloced_page,
					dma_addr, restore->page_caching);
		restore->restored_pages = 1UL << restore->order;
	}

	/*
	 * If a restore is ongoing, part of the tt pages may have a
	 * caching different than writeback.
	 */
	if (restore) {
		pgoff_t mid = restore->snapshot_alloc.caching_divide - tt->pages;

		start_page = restore->alloced_pages;
		WARN_ON(mid > start_page);
		/* Pages that might be dma-mapped and non-cached */
		ttm_pool_free_range(restore->pool, tt, tt->caching,
				    0, mid);
		/* Pages that might be dma-mapped but cached */
		ttm_pool_free_range(restore->pool, tt, ttm_cached,
				    mid, restore->alloced_pages);
		kfree(restore);
		tt->restore = NULL;
	}

	ttm_pool_free_range(NULL, tt, ttm_cached, start_page, tt->num_pages);
}

/**
 * ttm_pool_backup() - Back up or purge a struct ttm_tt
 * @pool: The pool used when allocating the struct ttm_tt.
 * @tt: The struct ttm_tt.
 * @flags: Flags to govern the backup behaviour.
 *
 * Back up or purge a struct ttm_tt. If @purge is true, then
 * all pages will be freed directly to the system rather than to the pool
 * they were allocated from, making the function behave similarly to
 * ttm_pool_free(). If @purge is false the pages will be backed up instead,
 * exchanged for handles.
 * A subsequent call to ttm_pool_restore_and_alloc() will then read back the content and
 * a subsequent call to ttm_pool_drop_backed_up() will drop it.
 * If backup of a page fails for whatever reason, @ttm will still be
 * partially backed up, retaining those pages for which backup fails.
 * In that case, this function can be retried, possibly after freeing up
 * memory resources.
 *
 * Return: Number of pages actually backed up or freed, or negative
 * error code on error.
 */
long ttm_pool_backup(struct ttm_pool *pool, struct ttm_tt *tt,
		     const struct ttm_backup_flags *flags)
{
	struct ttm_backup *backup = tt->backup;
	struct page *page;
	unsigned long handle;
	gfp_t alloc_gfp;
	gfp_t gfp;
	int ret = 0;
	pgoff_t shrunken = 0;
	pgoff_t i, num_pages;

	if (WARN_ON(ttm_tt_is_backed_up(tt)))
		return -EINVAL;

	if ((!ttm_backup_bytes_avail() && !flags->purge) ||
	    pool->use_dma_alloc || ttm_tt_is_backed_up(tt))
		return -EBUSY;

#ifdef CONFIG_X86
	/* Anything returned to the system needs to be cached. */
	if (tt->caching != ttm_cached)
		set_pages_array_wb(tt->pages, tt->num_pages);
#endif

	if (tt->dma_address || flags->purge) {
		for (i = 0; i < tt->num_pages; i += num_pages) {
			unsigned int order;

			page = tt->pages[i];
			if (unlikely(!page)) {
				num_pages = 1;
				continue;
			}

			order = ttm_pool_page_order(pool, page);
			num_pages = 1UL << order;
			if (tt->dma_address)
				ttm_pool_unmap(pool, tt->dma_address[i],
					       num_pages);
			if (flags->purge) {
				shrunken += num_pages;
				page->private = 0;
				__free_pages(page, order);
				memset(tt->pages + i, 0,
				       num_pages * sizeof(*tt->pages));
			}
		}
	}

	if (flags->purge)
		return shrunken;

	if (pool->use_dma32)
		gfp = GFP_DMA32;
	else
		gfp = GFP_HIGHUSER;

	alloc_gfp = GFP_KERNEL | __GFP_HIGH | __GFP_NOWARN | __GFP_RETRY_MAYFAIL;

	num_pages = tt->num_pages;

	/* Pretend doing fault injection by shrinking only half of the pages. */
	if (IS_ENABLED(CONFIG_FAULT_INJECTION) && should_fail(&backup_fault_inject, 1))
		num_pages = DIV_ROUND_UP(num_pages, 2);

	for (i = 0; i < num_pages; ++i) {
		s64 shandle;

		page = tt->pages[i];
		if (unlikely(!page))
			continue;

		ttm_pool_split_for_swap(pool, page);

		shandle = ttm_backup_backup_page(backup, page, flags->writeback, i,
						 gfp, alloc_gfp);
		if (shandle < 0) {
			/* We allow partially shrunken tts */
			ret = shandle;
			break;
		}
		handle = shandle;
		tt->pages[i] = ttm_backup_handle_to_page_ptr(handle);
		put_page(page);
		shrunken++;
	}

	return shrunken ? shrunken : ret;
}

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

	for (i = 0; i < TTM_NUM_CACHING_TYPES; ++i) {
		for (j = 0; j < NR_PAGE_ORDERS; ++j) {
			struct ttm_pool_type *pt;

			/* Initialize only pool types which are actually used */
			pt = ttm_pool_select_type(pool, i, j);
			if (pt != &pool->caching[i].orders[j])
				continue;

			ttm_pool_type_init(pt, pool, i, j);
		}
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

	for (i = 0; i < TTM_NUM_CACHING_TYPES; ++i) {
		for (j = 0; j < NR_PAGE_ORDERS; ++j) {
			struct ttm_pool_type *pt;

			pt = ttm_pool_select_type(pool, i, j);
			if (pt != &pool->caching[i].orders[j])
				continue;

			ttm_pool_type_fini(pt);
		}
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
#ifdef CONFIG_FAULT_INJECTION
	fault_create_debugfs_attr("backup_fault_inject", ttm_debugfs_root,
				  &backup_fault_inject);
#endif
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
