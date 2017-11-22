/*
 * Copyright (c) Red Hat Inc.

 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Jerome Glisse <jglisse@redhat.com>
 *          Pauli Nieminen <suokkos@gmail.com>
 */

/* simple list based uncached page pool
 * - Pool collects resently freed pages for reuse
 * - Use page->lru to keep a free list
 * - doesn't track currently in use pages
 */

#define pr_fmt(fmt) "[TTM] " fmt

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/highmem.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/seq_file.h> /* for seq_printf */
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <linux/atomic.h>

#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_page_alloc.h>

#if IS_ENABLED(CONFIG_AGP)
#include <asm/agp.h>
#endif
#ifdef CONFIG_X86
#include <asm/set_memory.h>
#endif

#define NUM_PAGES_TO_ALLOC		(PAGE_SIZE/sizeof(struct page *))
#define SMALL_ALLOCATION		16
#define FREE_ALL_PAGES			(~0U)
/* times are in msecs */
#define PAGE_FREE_INTERVAL		1000

/**
 * struct ttm_page_pool - Pool to reuse recently allocated uc/wc pages.
 *
 * @lock: Protects the shared pool from concurrnet access. Must be used with
 * irqsave/irqrestore variants because pool allocator maybe called from
 * delayed work.
 * @fill_lock: Prevent concurrent calls to fill.
 * @list: Pool of free uc/wc pages for fast reuse.
 * @gfp_flags: Flags to pass for alloc_page.
 * @npages: Number of pages in pool.
 */
struct ttm_page_pool {
	spinlock_t		lock;
	bool			fill_lock;
	struct list_head	list;
	gfp_t			gfp_flags;
	unsigned		npages;
	char			*name;
	unsigned long		nfrees;
	unsigned long		nrefills;
	unsigned int		order;
};

/**
 * Limits for the pool. They are handled without locks because only place where
 * they may change is in sysfs store. They won't have immediate effect anyway
 * so forcing serialization to access them is pointless.
 */

struct ttm_pool_opts {
	unsigned	alloc_size;
	unsigned	max_size;
	unsigned	small;
};

#define NUM_POOLS 6

/**
 * struct ttm_pool_manager - Holds memory pools for fst allocation
 *
 * Manager is read only object for pool code so it doesn't need locking.
 *
 * @free_interval: minimum number of jiffies between freeing pages from pool.
 * @page_alloc_inited: reference counting for pool allocation.
 * @work: Work that is used to shrink the pool. Work is only run when there is
 * some pages to free.
 * @small_allocation: Limit in number of pages what is small allocation.
 *
 * @pools: All pool objects in use.
 **/
struct ttm_pool_manager {
	struct kobject		kobj;
	struct shrinker		mm_shrink;
	struct ttm_pool_opts	options;

	union {
		struct ttm_page_pool	pools[NUM_POOLS];
		struct {
			struct ttm_page_pool	wc_pool;
			struct ttm_page_pool	uc_pool;
			struct ttm_page_pool	wc_pool_dma32;
			struct ttm_page_pool	uc_pool_dma32;
			struct ttm_page_pool	wc_pool_huge;
			struct ttm_page_pool	uc_pool_huge;
		} ;
	};
};

static struct attribute ttm_page_pool_max = {
	.name = "pool_max_size",
	.mode = S_IRUGO | S_IWUSR
};
static struct attribute ttm_page_pool_small = {
	.name = "pool_small_allocation",
	.mode = S_IRUGO | S_IWUSR
};
static struct attribute ttm_page_pool_alloc_size = {
	.name = "pool_allocation_size",
	.mode = S_IRUGO | S_IWUSR
};

static struct attribute *ttm_pool_attrs[] = {
	&ttm_page_pool_max,
	&ttm_page_pool_small,
	&ttm_page_pool_alloc_size,
	NULL
};

static void ttm_pool_kobj_release(struct kobject *kobj)
{
	struct ttm_pool_manager *m =
		container_of(kobj, struct ttm_pool_manager, kobj);
	kfree(m);
}

static ssize_t ttm_pool_store(struct kobject *kobj,
		struct attribute *attr, const char *buffer, size_t size)
{
	struct ttm_pool_manager *m =
		container_of(kobj, struct ttm_pool_manager, kobj);
	int chars;
	unsigned val;
	chars = sscanf(buffer, "%u", &val);
	if (chars == 0)
		return size;

	/* Convert kb to number of pages */
	val = val / (PAGE_SIZE >> 10);

	if (attr == &ttm_page_pool_max)
		m->options.max_size = val;
	else if (attr == &ttm_page_pool_small)
		m->options.small = val;
	else if (attr == &ttm_page_pool_alloc_size) {
		if (val > NUM_PAGES_TO_ALLOC*8) {
			pr_err("Setting allocation size to %lu is not allowed. Recommended size is %lu\n",
			       NUM_PAGES_TO_ALLOC*(PAGE_SIZE >> 7),
			       NUM_PAGES_TO_ALLOC*(PAGE_SIZE >> 10));
			return size;
		} else if (val > NUM_PAGES_TO_ALLOC) {
			pr_warn("Setting allocation size to larger than %lu is not recommended\n",
				NUM_PAGES_TO_ALLOC*(PAGE_SIZE >> 10));
		}
		m->options.alloc_size = val;
	}

	return size;
}

static ssize_t ttm_pool_show(struct kobject *kobj,
		struct attribute *attr, char *buffer)
{
	struct ttm_pool_manager *m =
		container_of(kobj, struct ttm_pool_manager, kobj);
	unsigned val = 0;

	if (attr == &ttm_page_pool_max)
		val = m->options.max_size;
	else if (attr == &ttm_page_pool_small)
		val = m->options.small;
	else if (attr == &ttm_page_pool_alloc_size)
		val = m->options.alloc_size;

	val = val * (PAGE_SIZE >> 10);

	return snprintf(buffer, PAGE_SIZE, "%u\n", val);
}

static const struct sysfs_ops ttm_pool_sysfs_ops = {
	.show = &ttm_pool_show,
	.store = &ttm_pool_store,
};

static struct kobj_type ttm_pool_kobj_type = {
	.release = &ttm_pool_kobj_release,
	.sysfs_ops = &ttm_pool_sysfs_ops,
	.default_attrs = ttm_pool_attrs,
};

static struct ttm_pool_manager *_manager;

#ifndef CONFIG_X86
static int set_pages_wb(struct page *page, int numpages)
{
#if IS_ENABLED(CONFIG_AGP)
	int i;

	for (i = 0; i < numpages; i++)
		unmap_page_from_agp(page++);
#endif
	return 0;
}

static int set_pages_array_wb(struct page **pages, int addrinarray)
{
#if IS_ENABLED(CONFIG_AGP)
	int i;

	for (i = 0; i < addrinarray; i++)
		unmap_page_from_agp(pages[i]);
#endif
	return 0;
}

static int set_pages_array_wc(struct page **pages, int addrinarray)
{
#if IS_ENABLED(CONFIG_AGP)
	int i;

	for (i = 0; i < addrinarray; i++)
		map_page_into_agp(pages[i]);
#endif
	return 0;
}

static int set_pages_array_uc(struct page **pages, int addrinarray)
{
#if IS_ENABLED(CONFIG_AGP)
	int i;

	for (i = 0; i < addrinarray; i++)
		map_page_into_agp(pages[i]);
#endif
	return 0;
}
#endif

/**
 * Select the right pool or requested caching state and ttm flags. */
static struct ttm_page_pool *ttm_get_pool(int flags, bool huge,
					  enum ttm_caching_state cstate)
{
	int pool_index;

	if (cstate == tt_cached)
		return NULL;

	if (cstate == tt_wc)
		pool_index = 0x0;
	else
		pool_index = 0x1;

	if (flags & TTM_PAGE_FLAG_DMA32) {
		if (huge)
			return NULL;
		pool_index |= 0x2;

	} else if (huge) {
		pool_index |= 0x4;
	}

	return &_manager->pools[pool_index];
}

/* set memory back to wb and free the pages. */
static void ttm_pages_put(struct page *pages[], unsigned npages)
{
	unsigned i;
	if (set_pages_array_wb(pages, npages))
		pr_err("Failed to set %d pages to wb!\n", npages);
	for (i = 0; i < npages; ++i)
		__free_page(pages[i]);
}

static void ttm_pool_update_free_locked(struct ttm_page_pool *pool,
		unsigned freed_pages)
{
	pool->npages -= freed_pages;
	pool->nfrees += freed_pages;
}

/**
 * Free pages from pool.
 *
 * To prevent hogging the ttm_swap process we only free NUM_PAGES_TO_ALLOC
 * number of pages in one go.
 *
 * @pool: to free the pages from
 * @free_all: If set to true will free all pages in pool
 * @use_static: Safe to use static buffer
 **/
static int ttm_page_pool_free(struct ttm_page_pool *pool, unsigned nr_free,
			      bool use_static)
{
	static struct page *static_buf[NUM_PAGES_TO_ALLOC];
	unsigned long irq_flags;
	struct page *p;
	struct page **pages_to_free;
	unsigned freed_pages = 0,
		 npages_to_free = nr_free;

	if (NUM_PAGES_TO_ALLOC < nr_free)
		npages_to_free = NUM_PAGES_TO_ALLOC;

	if (use_static)
		pages_to_free = static_buf;
	else
		pages_to_free = kmalloc(npages_to_free * sizeof(struct page *),
					GFP_KERNEL);
	if (!pages_to_free) {
		pr_debug("Failed to allocate memory for pool free operation\n");
		return 0;
	}

restart:
	spin_lock_irqsave(&pool->lock, irq_flags);

	list_for_each_entry_reverse(p, &pool->list, lru) {
		if (freed_pages >= npages_to_free)
			break;

		pages_to_free[freed_pages++] = p;
		/* We can only remove NUM_PAGES_TO_ALLOC at a time. */
		if (freed_pages >= NUM_PAGES_TO_ALLOC) {
			/* remove range of pages from the pool */
			__list_del(p->lru.prev, &pool->list);

			ttm_pool_update_free_locked(pool, freed_pages);
			/**
			 * Because changing page caching is costly
			 * we unlock the pool to prevent stalling.
			 */
			spin_unlock_irqrestore(&pool->lock, irq_flags);

			ttm_pages_put(pages_to_free, freed_pages);
			if (likely(nr_free != FREE_ALL_PAGES))
				nr_free -= freed_pages;

			if (NUM_PAGES_TO_ALLOC >= nr_free)
				npages_to_free = nr_free;
			else
				npages_to_free = NUM_PAGES_TO_ALLOC;

			freed_pages = 0;

			/* free all so restart the processing */
			if (nr_free)
				goto restart;

			/* Not allowed to fall through or break because
			 * following context is inside spinlock while we are
			 * outside here.
			 */
			goto out;

		}
	}

	/* remove range of pages from the pool */
	if (freed_pages) {
		__list_del(&p->lru, &pool->list);

		ttm_pool_update_free_locked(pool, freed_pages);
		nr_free -= freed_pages;
	}

	spin_unlock_irqrestore(&pool->lock, irq_flags);

	if (freed_pages)
		ttm_pages_put(pages_to_free, freed_pages);
out:
	if (pages_to_free != static_buf)
		kfree(pages_to_free);
	return nr_free;
}

/**
 * Callback for mm to request pool to reduce number of page held.
 *
 * XXX: (dchinner) Deadlock warning!
 *
 * This code is crying out for a shrinker per pool....
 */
static unsigned long
ttm_pool_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	static DEFINE_MUTEX(lock);
	static unsigned start_pool;
	unsigned i;
	unsigned pool_offset;
	struct ttm_page_pool *pool;
	int shrink_pages = sc->nr_to_scan;
	unsigned long freed = 0;
	unsigned int nr_free_pool;

	if (!mutex_trylock(&lock))
		return SHRINK_STOP;
	pool_offset = ++start_pool % NUM_POOLS;
	/* select start pool in round robin fashion */
	for (i = 0; i < NUM_POOLS; ++i) {
		unsigned nr_free = shrink_pages;
		if (shrink_pages == 0)
			break;

		pool = &_manager->pools[(i + pool_offset)%NUM_POOLS];
		/* OK to use static buffer since global mutex is held. */
		nr_free_pool = (nr_free >> pool->order);
		if (nr_free_pool == 0)
			continue;

		shrink_pages = ttm_page_pool_free(pool, nr_free_pool, true);
		freed += ((nr_free_pool - shrink_pages) << pool->order);
	}
	mutex_unlock(&lock);
	return freed;
}


static unsigned long
ttm_pool_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	unsigned i;
	unsigned long count = 0;
	struct ttm_page_pool *pool;

	for (i = 0; i < NUM_POOLS; ++i) {
		pool = &_manager->pools[i];
		count += (pool->npages << pool->order);
	}

	return count;
}

static void ttm_pool_mm_shrink_init(struct ttm_pool_manager *manager)
{
	manager->mm_shrink.count_objects = ttm_pool_shrink_count;
	manager->mm_shrink.scan_objects = ttm_pool_shrink_scan;
	manager->mm_shrink.seeks = 1;
	register_shrinker(&manager->mm_shrink);
}

static void ttm_pool_mm_shrink_fini(struct ttm_pool_manager *manager)
{
	unregister_shrinker(&manager->mm_shrink);
}

static int ttm_set_pages_caching(struct page **pages,
		enum ttm_caching_state cstate, unsigned cpages)
{
	int r = 0;
	/* Set page caching */
	switch (cstate) {
	case tt_uncached:
		r = set_pages_array_uc(pages, cpages);
		if (r)
			pr_err("Failed to set %d pages to uc!\n", cpages);
		break;
	case tt_wc:
		r = set_pages_array_wc(pages, cpages);
		if (r)
			pr_err("Failed to set %d pages to wc!\n", cpages);
		break;
	default:
		break;
	}
	return r;
}

/**
 * Free pages the pages that failed to change the caching state. If there is
 * any pages that have changed their caching state already put them to the
 * pool.
 */
static void ttm_handle_caching_state_failure(struct list_head *pages,
		int ttm_flags, enum ttm_caching_state cstate,
		struct page **failed_pages, unsigned cpages)
{
	unsigned i;
	/* Failed pages have to be freed */
	for (i = 0; i < cpages; ++i) {
		list_del(&failed_pages[i]->lru);
		__free_page(failed_pages[i]);
	}
}

/**
 * Allocate new pages with correct caching.
 *
 * This function is reentrant if caller updates count depending on number of
 * pages returned in pages array.
 */
static int ttm_alloc_new_pages(struct list_head *pages, gfp_t gfp_flags,
			       int ttm_flags, enum ttm_caching_state cstate,
			       unsigned count, unsigned order)
{
	struct page **caching_array;
	struct page *p;
	int r = 0;
	unsigned i, j, cpages;
	unsigned npages = 1 << order;
	unsigned max_cpages = min(count, (unsigned)NUM_PAGES_TO_ALLOC);

	/* allocate array for page caching change */
	caching_array = kmalloc(max_cpages*sizeof(struct page *), GFP_KERNEL);

	if (!caching_array) {
		pr_debug("Unable to allocate table for new pages\n");
		return -ENOMEM;
	}

	for (i = 0, cpages = 0; i < count; ++i) {
		p = alloc_pages(gfp_flags, order);

		if (!p) {
			pr_debug("Unable to get page %u\n", i);

			/* store already allocated pages in the pool after
			 * setting the caching state */
			if (cpages) {
				r = ttm_set_pages_caching(caching_array,
							  cstate, cpages);
				if (r)
					ttm_handle_caching_state_failure(pages,
						ttm_flags, cstate,
						caching_array, cpages);
			}
			r = -ENOMEM;
			goto out;
		}

		list_add(&p->lru, pages);

#ifdef CONFIG_HIGHMEM
		/* gfp flags of highmem page should never be dma32 so we
		 * we should be fine in such case
		 */
		if (PageHighMem(p))
			continue;

#endif
		for (j = 0; j < npages; ++j) {
			caching_array[cpages++] = p++;
			if (cpages == max_cpages) {

				r = ttm_set_pages_caching(caching_array,
						cstate, cpages);
				if (r) {
					ttm_handle_caching_state_failure(pages,
						ttm_flags, cstate,
						caching_array, cpages);
					goto out;
				}
				cpages = 0;
			}
		}
	}

	if (cpages) {
		r = ttm_set_pages_caching(caching_array, cstate, cpages);
		if (r)
			ttm_handle_caching_state_failure(pages,
					ttm_flags, cstate,
					caching_array, cpages);
	}
out:
	kfree(caching_array);

	return r;
}

/**
 * Fill the given pool if there aren't enough pages and the requested number of
 * pages is small.
 */
static void ttm_page_pool_fill_locked(struct ttm_page_pool *pool, int ttm_flags,
				      enum ttm_caching_state cstate,
				      unsigned count, unsigned long *irq_flags)
{
	struct page *p;
	int r;
	unsigned cpages = 0;
	/**
	 * Only allow one pool fill operation at a time.
	 * If pool doesn't have enough pages for the allocation new pages are
	 * allocated from outside of pool.
	 */
	if (pool->fill_lock)
		return;

	pool->fill_lock = true;

	/* If allocation request is small and there are not enough
	 * pages in a pool we fill the pool up first. */
	if (count < _manager->options.small
		&& count > pool->npages) {
		struct list_head new_pages;
		unsigned alloc_size = _manager->options.alloc_size;

		/**
		 * Can't change page caching if in irqsave context. We have to
		 * drop the pool->lock.
		 */
		spin_unlock_irqrestore(&pool->lock, *irq_flags);

		INIT_LIST_HEAD(&new_pages);
		r = ttm_alloc_new_pages(&new_pages, pool->gfp_flags, ttm_flags,
					cstate, alloc_size, 0);
		spin_lock_irqsave(&pool->lock, *irq_flags);

		if (!r) {
			list_splice(&new_pages, &pool->list);
			++pool->nrefills;
			pool->npages += alloc_size;
		} else {
			pr_debug("Failed to fill pool (%p)\n", pool);
			/* If we have any pages left put them to the pool. */
			list_for_each_entry(p, &new_pages, lru) {
				++cpages;
			}
			list_splice(&new_pages, &pool->list);
			pool->npages += cpages;
		}

	}
	pool->fill_lock = false;
}

/**
 * Allocate pages from the pool and put them on the return list.
 *
 * @return zero for success or negative error code.
 */
static int ttm_page_pool_get_pages(struct ttm_page_pool *pool,
				   struct list_head *pages,
				   int ttm_flags,
				   enum ttm_caching_state cstate,
				   unsigned count, unsigned order)
{
	unsigned long irq_flags;
	struct list_head *p;
	unsigned i;
	int r = 0;

	spin_lock_irqsave(&pool->lock, irq_flags);
	if (!order)
		ttm_page_pool_fill_locked(pool, ttm_flags, cstate, count,
					  &irq_flags);

	if (count >= pool->npages) {
		/* take all pages from the pool */
		list_splice_init(&pool->list, pages);
		count -= pool->npages;
		pool->npages = 0;
		goto out;
	}
	/* find the last pages to include for requested number of pages. Split
	 * pool to begin and halve it to reduce search space. */
	if (count <= pool->npages/2) {
		i = 0;
		list_for_each(p, &pool->list) {
			if (++i == count)
				break;
		}
	} else {
		i = pool->npages + 1;
		list_for_each_prev(p, &pool->list) {
			if (--i == count)
				break;
		}
	}
	/* Cut 'count' number of pages from the pool */
	list_cut_position(pages, &pool->list, p);
	pool->npages -= count;
	count = 0;
out:
	spin_unlock_irqrestore(&pool->lock, irq_flags);

	/* clear the pages coming from the pool if requested */
	if (ttm_flags & TTM_PAGE_FLAG_ZERO_ALLOC) {
		struct page *page;

		list_for_each_entry(page, pages, lru) {
			if (PageHighMem(page))
				clear_highpage(page);
			else
				clear_page(page_address(page));
		}
	}

	/* If pool didn't have enough pages allocate new one. */
	if (count) {
		gfp_t gfp_flags = pool->gfp_flags;

		/* set zero flag for page allocation if required */
		if (ttm_flags & TTM_PAGE_FLAG_ZERO_ALLOC)
			gfp_flags |= __GFP_ZERO;

		/* ttm_alloc_new_pages doesn't reference pool so we can run
		 * multiple requests in parallel.
		 **/
		r = ttm_alloc_new_pages(pages, gfp_flags, ttm_flags, cstate,
					count, order);
	}

	return r;
}

/* Put all pages in pages list to correct pool to wait for reuse */
static void ttm_put_pages(struct page **pages, unsigned npages, int flags,
			  enum ttm_caching_state cstate)
{
	struct ttm_page_pool *pool = ttm_get_pool(flags, false, cstate);
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	struct ttm_page_pool *huge = ttm_get_pool(flags, true, cstate);
#endif
	unsigned long irq_flags;
	unsigned i;

	if (pool == NULL) {
		/* No pool for this memory type so free the pages */
		i = 0;
		while (i < npages) {
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			struct page *p = pages[i];
#endif
			unsigned order = 0, j;

			if (!pages[i]) {
				++i;
				continue;
			}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			if (!(flags & TTM_PAGE_FLAG_DMA32)) {
				for (j = 0; j < HPAGE_PMD_NR; ++j)
					if (p++ != pages[i + j])
					    break;

				if (j == HPAGE_PMD_NR)
					order = HPAGE_PMD_ORDER;
			}
#endif

			if (page_count(pages[i]) != 1)
				pr_err("Erroneous page count. Leaking pages.\n");
			__free_pages(pages[i], order);

			j = 1 << order;
			while (j) {
				pages[i++] = NULL;
				--j;
			}
		}
		return;
	}

	i = 0;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (huge) {
		unsigned max_size, n2free;

		spin_lock_irqsave(&huge->lock, irq_flags);
		while (i < npages) {
			struct page *p = pages[i];
			unsigned j;

			if (!p)
				break;

			for (j = 0; j < HPAGE_PMD_NR; ++j)
				if (p++ != pages[i + j])
				    break;

			if (j != HPAGE_PMD_NR)
				break;

			list_add_tail(&pages[i]->lru, &huge->list);

			for (j = 0; j < HPAGE_PMD_NR; ++j)
				pages[i++] = NULL;
			huge->npages++;
		}

		/* Check that we don't go over the pool limit */
		max_size = _manager->options.max_size;
		max_size /= HPAGE_PMD_NR;
		if (huge->npages > max_size)
			n2free = huge->npages - max_size;
		else
			n2free = 0;
		spin_unlock_irqrestore(&huge->lock, irq_flags);
		if (n2free)
			ttm_page_pool_free(huge, n2free, false);
	}
#endif

	spin_lock_irqsave(&pool->lock, irq_flags);
	while (i < npages) {
		if (pages[i]) {
			if (page_count(pages[i]) != 1)
				pr_err("Erroneous page count. Leaking pages.\n");
			list_add_tail(&pages[i]->lru, &pool->list);
			pages[i] = NULL;
			pool->npages++;
		}
		++i;
	}
	/* Check that we don't go over the pool limit */
	npages = 0;
	if (pool->npages > _manager->options.max_size) {
		npages = pool->npages - _manager->options.max_size;
		/* free at least NUM_PAGES_TO_ALLOC number of pages
		 * to reduce calls to set_memory_wb */
		if (npages < NUM_PAGES_TO_ALLOC)
			npages = NUM_PAGES_TO_ALLOC;
	}
	spin_unlock_irqrestore(&pool->lock, irq_flags);
	if (npages)
		ttm_page_pool_free(pool, npages, false);
}

/*
 * On success pages list will hold count number of correctly
 * cached pages.
 */
static int ttm_get_pages(struct page **pages, unsigned npages, int flags,
			 enum ttm_caching_state cstate)
{
	struct ttm_page_pool *pool = ttm_get_pool(flags, false, cstate);
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	struct ttm_page_pool *huge = ttm_get_pool(flags, true, cstate);
#endif
	struct list_head plist;
	struct page *p = NULL;
	unsigned count;
	int r;

	/* No pool for cached pages */
	if (pool == NULL) {
		gfp_t gfp_flags = GFP_USER;
		unsigned i;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		unsigned j;
#endif

		/* set zero flag for page allocation if required */
		if (flags & TTM_PAGE_FLAG_ZERO_ALLOC)
			gfp_flags |= __GFP_ZERO;

		if (flags & TTM_PAGE_FLAG_DMA32)
			gfp_flags |= GFP_DMA32;
		else
			gfp_flags |= GFP_HIGHUSER;

		i = 0;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		if (!(gfp_flags & GFP_DMA32)) {
			while (npages >= HPAGE_PMD_NR) {
				gfp_t huge_flags = gfp_flags;

				huge_flags |= GFP_TRANSHUGE;
				huge_flags &= ~__GFP_MOVABLE;
				huge_flags &= ~__GFP_COMP;
				p = alloc_pages(huge_flags, HPAGE_PMD_ORDER);
				if (!p)
					break;

				for (j = 0; j < HPAGE_PMD_NR; ++j)
					pages[i++] = p++;

				npages -= HPAGE_PMD_NR;
			}
		}
#endif

		while (npages) {
			p = alloc_page(gfp_flags);
			if (!p) {
				pr_debug("Unable to allocate page\n");
				return -ENOMEM;
			}

			pages[i++] = p;
			--npages;
		}
		return 0;
	}

	count = 0;

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (huge && npages >= HPAGE_PMD_NR) {
		INIT_LIST_HEAD(&plist);
		ttm_page_pool_get_pages(huge, &plist, flags, cstate,
					npages / HPAGE_PMD_NR,
					HPAGE_PMD_ORDER);

		list_for_each_entry(p, &plist, lru) {
			unsigned j;

			for (j = 0; j < HPAGE_PMD_NR; ++j)
				pages[count++] = &p[j];
		}
	}
#endif

	INIT_LIST_HEAD(&plist);
	r = ttm_page_pool_get_pages(pool, &plist, flags, cstate,
				    npages - count, 0);

	list_for_each_entry(p, &plist, lru)
		pages[count++] = p;

	if (r) {
		/* If there is any pages in the list put them back to
		 * the pool.
		 */
		pr_debug("Failed to allocate extra pages for large request\n");
		ttm_put_pages(pages, count, flags, cstate);
		return r;
	}

	return 0;
}

static void ttm_page_pool_init_locked(struct ttm_page_pool *pool, gfp_t flags,
		char *name, unsigned int order)
{
	spin_lock_init(&pool->lock);
	pool->fill_lock = false;
	INIT_LIST_HEAD(&pool->list);
	pool->npages = pool->nfrees = 0;
	pool->gfp_flags = flags;
	pool->name = name;
	pool->order = order;
}

int ttm_page_alloc_init(struct ttm_mem_global *glob, unsigned max_pages)
{
	int ret;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	unsigned order = HPAGE_PMD_ORDER;
#else
	unsigned order = 0;
#endif

	WARN_ON(_manager);

	pr_info("Initializing pool allocator\n");

	_manager = kzalloc(sizeof(*_manager), GFP_KERNEL);

	ttm_page_pool_init_locked(&_manager->wc_pool, GFP_HIGHUSER, "wc", 0);

	ttm_page_pool_init_locked(&_manager->uc_pool, GFP_HIGHUSER, "uc", 0);

	ttm_page_pool_init_locked(&_manager->wc_pool_dma32,
				  GFP_USER | GFP_DMA32, "wc dma", 0);

	ttm_page_pool_init_locked(&_manager->uc_pool_dma32,
				  GFP_USER | GFP_DMA32, "uc dma", 0);

	ttm_page_pool_init_locked(&_manager->wc_pool_huge,
				  GFP_TRANSHUGE	& ~(__GFP_MOVABLE | __GFP_COMP),
				  "wc huge", order);

	ttm_page_pool_init_locked(&_manager->uc_pool_huge,
				  GFP_TRANSHUGE	& ~(__GFP_MOVABLE | __GFP_COMP)
				  , "uc huge", order);

	_manager->options.max_size = max_pages;
	_manager->options.small = SMALL_ALLOCATION;
	_manager->options.alloc_size = NUM_PAGES_TO_ALLOC;

	ret = kobject_init_and_add(&_manager->kobj, &ttm_pool_kobj_type,
				   &glob->kobj, "pool");
	if (unlikely(ret != 0)) {
		kobject_put(&_manager->kobj);
		_manager = NULL;
		return ret;
	}

	ttm_pool_mm_shrink_init(_manager);

	return 0;
}

void ttm_page_alloc_fini(void)
{
	int i;

	pr_info("Finalizing pool allocator\n");
	ttm_pool_mm_shrink_fini(_manager);

	/* OK to use static buffer since global mutex is no longer used. */
	for (i = 0; i < NUM_POOLS; ++i)
		ttm_page_pool_free(&_manager->pools[i], FREE_ALL_PAGES, true);

	kobject_put(&_manager->kobj);
	_manager = NULL;
}

int ttm_pool_populate(struct ttm_tt *ttm)
{
	struct ttm_mem_global *mem_glob = ttm->glob->mem_glob;
	unsigned i;
	int ret;

	if (ttm->state != tt_unpopulated)
		return 0;

	ret = ttm_get_pages(ttm->pages, ttm->num_pages, ttm->page_flags,
			    ttm->caching_state);
	if (unlikely(ret != 0)) {
		ttm_pool_unpopulate(ttm);
		return ret;
	}

	for (i = 0; i < ttm->num_pages; ++i) {
		ret = ttm_mem_global_alloc_page(mem_glob, ttm->pages[i],
						PAGE_SIZE);
		if (unlikely(ret != 0)) {
			ttm_pool_unpopulate(ttm);
			return -ENOMEM;
		}
	}

	if (unlikely(ttm->page_flags & TTM_PAGE_FLAG_SWAPPED)) {
		ret = ttm_tt_swapin(ttm);
		if (unlikely(ret != 0)) {
			ttm_pool_unpopulate(ttm);
			return ret;
		}
	}

	ttm->state = tt_unbound;
	return 0;
}
EXPORT_SYMBOL(ttm_pool_populate);

void ttm_pool_unpopulate(struct ttm_tt *ttm)
{
	unsigned i;

	for (i = 0; i < ttm->num_pages; ++i) {
		if (!ttm->pages[i])
			continue;

		ttm_mem_global_free_page(ttm->glob->mem_glob, ttm->pages[i],
					 PAGE_SIZE);
	}
	ttm_put_pages(ttm->pages, ttm->num_pages, ttm->page_flags,
		      ttm->caching_state);
	ttm->state = tt_unpopulated;
}
EXPORT_SYMBOL(ttm_pool_unpopulate);

int ttm_populate_and_map_pages(struct device *dev, struct ttm_dma_tt *tt)
{
	unsigned i, j;
	int r;

	r = ttm_pool_populate(&tt->ttm);
	if (r)
		return r;

	for (i = 0; i < tt->ttm.num_pages; ++i) {
		struct page *p = tt->ttm.pages[i];
		size_t num_pages = 1;

		for (j = i + 1; j < tt->ttm.num_pages; ++j) {
			if (++p != tt->ttm.pages[j])
				break;

			++num_pages;
		}

		tt->dma_address[i] = dma_map_page(dev, tt->ttm.pages[i],
						  0, num_pages * PAGE_SIZE,
						  DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, tt->dma_address[i])) {
			while (i--) {
				dma_unmap_page(dev, tt->dma_address[i],
					       PAGE_SIZE, DMA_BIDIRECTIONAL);
				tt->dma_address[i] = 0;
			}
			ttm_pool_unpopulate(&tt->ttm);
			return -EFAULT;
		}

		for (j = 1; j < num_pages; ++j) {
			tt->dma_address[i + 1] = tt->dma_address[i] + PAGE_SIZE;
			++i;
		}
	}
	return 0;
}
EXPORT_SYMBOL(ttm_populate_and_map_pages);

void ttm_unmap_and_unpopulate_pages(struct device *dev, struct ttm_dma_tt *tt)
{
	unsigned i, j;

	for (i = 0; i < tt->ttm.num_pages;) {
		struct page *p = tt->ttm.pages[i];
		size_t num_pages = 1;

		if (!tt->dma_address[i] || !tt->ttm.pages[i]) {
			++i;
			continue;
		}

		for (j = i + 1; j < tt->ttm.num_pages; ++j) {
			if (++p != tt->ttm.pages[j])
				break;

			++num_pages;
		}

		dma_unmap_page(dev, tt->dma_address[i], num_pages * PAGE_SIZE,
			       DMA_BIDIRECTIONAL);

		i += num_pages;
	}
	ttm_pool_unpopulate(&tt->ttm);
}
EXPORT_SYMBOL(ttm_unmap_and_unpopulate_pages);

int ttm_page_alloc_debugfs(struct seq_file *m, void *data)
{
	struct ttm_page_pool *p;
	unsigned i;
	char *h[] = {"pool", "refills", "pages freed", "size"};
	if (!_manager) {
		seq_printf(m, "No pool allocator running.\n");
		return 0;
	}
	seq_printf(m, "%7s %12s %13s %8s\n",
			h[0], h[1], h[2], h[3]);
	for (i = 0; i < NUM_POOLS; ++i) {
		p = &_manager->pools[i];

		seq_printf(m, "%7s %12ld %13ld %8d\n",
				p->name, p->nrefills,
				p->nfrees, p->npages);
	}
	return 0;
}
EXPORT_SYMBOL(ttm_page_alloc_debugfs);
