/*
 * Copyright 2011 (c) Oracle Corp.

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
 * Author: Konrad Rzeszutek Wilk <konrad.wilk@oracle.com>
 */

/*
 * A simple DMA pool losely based on dmapool.c. It has certain advantages
 * over the DMA pools:
 * - Pool collects resently freed pages for reuse (and hooks up to
 *   the shrinker).
 * - Tracks currently in use pages
 * - Tracks whether the page is UC, WB or cached (and reverts to WB
 *   when freed).
 */

#if defined(CONFIG_SWIOTLB) || defined(CONFIG_INTEL_IOMMU)
#define pr_fmt(fmt) "[TTM] " fmt

#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/seq_file.h> /* for seq_printf */
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/highmem.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_page_alloc.h>
#ifdef TTM_HAS_AGP
#include <asm/agp.h>
#endif

#define NUM_PAGES_TO_ALLOC		(PAGE_SIZE/sizeof(struct page *))
#define SMALL_ALLOCATION		4
#define FREE_ALL_PAGES			(~0U)
/* times are in msecs */
#define IS_UNDEFINED			(0)
#define IS_WC				(1<<1)
#define IS_UC				(1<<2)
#define IS_CACHED			(1<<3)
#define IS_DMA32			(1<<4)

enum pool_type {
	POOL_IS_UNDEFINED,
	POOL_IS_WC = IS_WC,
	POOL_IS_UC = IS_UC,
	POOL_IS_CACHED = IS_CACHED,
	POOL_IS_WC_DMA32 = IS_WC | IS_DMA32,
	POOL_IS_UC_DMA32 = IS_UC | IS_DMA32,
	POOL_IS_CACHED_DMA32 = IS_CACHED | IS_DMA32,
};
/*
 * The pool structure. There are usually six pools:
 *  - generic (not restricted to DMA32):
 *      - write combined, uncached, cached.
 *  - dma32 (up to 2^32 - so up 4GB):
 *      - write combined, uncached, cached.
 * for each 'struct device'. The 'cached' is for pages that are actively used.
 * The other ones can be shrunk by the shrinker API if neccessary.
 * @pools: The 'struct device->dma_pools' link.
 * @type: Type of the pool
 * @lock: Protects the inuse_list and free_list from concurrnet access. Must be
 * used with irqsave/irqrestore variants because pool allocator maybe called
 * from delayed work.
 * @inuse_list: Pool of pages that are in use. The order is very important and
 *   it is in the order that the TTM pages that are put back are in.
 * @free_list: Pool of pages that are free to be used. No order requirements.
 * @dev: The device that is associated with these pools.
 * @size: Size used during DMA allocation.
 * @npages_free: Count of available pages for re-use.
 * @npages_in_use: Count of pages that are in use.
 * @nfrees: Stats when pool is shrinking.
 * @nrefills: Stats when the pool is grown.
 * @gfp_flags: Flags to pass for alloc_page.
 * @name: Name of the pool.
 * @dev_name: Name derieved from dev - similar to how dev_info works.
 *   Used during shutdown as the dev_info during release is unavailable.
 */
struct dma_pool {
	struct list_head pools; /* The 'struct device->dma_pools link */
	enum pool_type type;
	spinlock_t lock;
	struct list_head inuse_list;
	struct list_head free_list;
	struct device *dev;
	unsigned size;
	unsigned npages_free;
	unsigned npages_in_use;
	unsigned long nfrees; /* Stats when shrunk. */
	unsigned long nrefills; /* Stats when grown. */
	gfp_t gfp_flags;
	char name[13]; /* "cached dma32" */
	char dev_name[64]; /* Constructed from dev */
};

/*
 * The accounting page keeping track of the allocated page along with
 * the DMA address.
 * @page_list: The link to the 'page_list' in 'struct dma_pool'.
 * @vaddr: The virtual address of the page
 * @dma: The bus address of the page. If the page is not allocated
 *   via the DMA API, it will be -1.
 */
struct dma_page {
	struct list_head page_list;
	void *vaddr;
	struct page *p;
	dma_addr_t dma;
};

/*
 * Limits for the pool. They are handled without locks because only place where
 * they may change is in sysfs store. They won't have immediate effect anyway
 * so forcing serialization to access them is pointless.
 */

struct ttm_pool_opts {
	unsigned	alloc_size;
	unsigned	max_size;
	unsigned	small;
};

/*
 * Contains the list of all of the 'struct device' and their corresponding
 * DMA pools. Guarded by _mutex->lock.
 * @pools: The link to 'struct ttm_pool_manager->pools'
 * @dev: The 'struct device' associated with the 'pool'
 * @pool: The 'struct dma_pool' associated with the 'dev'
 */
struct device_pools {
	struct list_head pools;
	struct device *dev;
	struct dma_pool *pool;
};

/*
 * struct ttm_pool_manager - Holds memory pools for fast allocation
 *
 * @lock: Lock used when adding/removing from pools
 * @pools: List of 'struct device' and 'struct dma_pool' tuples.
 * @options: Limits for the pool.
 * @npools: Total amount of pools in existence.
 * @shrinker: The structure used by [un|]register_shrinker
 */
struct ttm_pool_manager {
	struct mutex		lock;
	struct list_head	pools;
	struct ttm_pool_opts	options;
	unsigned		npools;
	struct shrinker		mm_shrink;
	struct kobject		kobj;
};

static struct ttm_pool_manager *_manager;

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

static ssize_t ttm_pool_store(struct kobject *kobj, struct attribute *attr,
			      const char *buffer, size_t size)
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

static ssize_t ttm_pool_show(struct kobject *kobj, struct attribute *attr,
			     char *buffer)
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

#ifndef CONFIG_X86
static int set_pages_array_wb(struct page **pages, int addrinarray)
{
#ifdef TTM_HAS_AGP
	int i;

	for (i = 0; i < addrinarray; i++)
		unmap_page_from_agp(pages[i]);
#endif
	return 0;
}

static int set_pages_array_wc(struct page **pages, int addrinarray)
{
#ifdef TTM_HAS_AGP
	int i;

	for (i = 0; i < addrinarray; i++)
		map_page_into_agp(pages[i]);
#endif
	return 0;
}

static int set_pages_array_uc(struct page **pages, int addrinarray)
{
#ifdef TTM_HAS_AGP
	int i;

	for (i = 0; i < addrinarray; i++)
		map_page_into_agp(pages[i]);
#endif
	return 0;
}
#endif /* for !CONFIG_X86 */

static int ttm_set_pages_caching(struct dma_pool *pool,
				 struct page **pages, unsigned cpages)
{
	int r = 0;
	/* Set page caching */
	if (pool->type & IS_UC) {
		r = set_pages_array_uc(pages, cpages);
		if (r)
			pr_err("%s: Failed to set %d pages to uc!\n",
			       pool->dev_name, cpages);
	}
	if (pool->type & IS_WC) {
		r = set_pages_array_wc(pages, cpages);
		if (r)
			pr_err("%s: Failed to set %d pages to wc!\n",
			       pool->dev_name, cpages);
	}
	return r;
}

static void __ttm_dma_free_page(struct dma_pool *pool, struct dma_page *d_page)
{
	dma_addr_t dma = d_page->dma;
	dma_free_coherent(pool->dev, pool->size, d_page->vaddr, dma);

	kfree(d_page);
	d_page = NULL;
}
static struct dma_page *__ttm_dma_alloc_page(struct dma_pool *pool)
{
	struct dma_page *d_page;

	d_page = kmalloc(sizeof(struct dma_page), GFP_KERNEL);
	if (!d_page)
		return NULL;

	d_page->vaddr = dma_alloc_coherent(pool->dev, pool->size,
					   &d_page->dma,
					   pool->gfp_flags);
	if (d_page->vaddr) {
		if (is_vmalloc_addr(d_page->vaddr))
			d_page->p = vmalloc_to_page(d_page->vaddr);
		else
			d_page->p = virt_to_page(d_page->vaddr);
	} else {
		kfree(d_page);
		d_page = NULL;
	}
	return d_page;
}
static enum pool_type ttm_to_type(int flags, enum ttm_caching_state cstate)
{
	enum pool_type type = IS_UNDEFINED;

	if (flags & TTM_PAGE_FLAG_DMA32)
		type |= IS_DMA32;
	if (cstate == tt_cached)
		type |= IS_CACHED;
	else if (cstate == tt_uncached)
		type |= IS_UC;
	else
		type |= IS_WC;

	return type;
}

static void ttm_pool_update_free_locked(struct dma_pool *pool,
					unsigned freed_pages)
{
	pool->npages_free -= freed_pages;
	pool->nfrees += freed_pages;

}

/* set memory back to wb and free the pages. */
static void ttm_dma_pages_put(struct dma_pool *pool, struct list_head *d_pages,
			      struct page *pages[], unsigned npages)
{
	struct dma_page *d_page, *tmp;

	/* Don't set WB on WB page pool. */
	if (npages && !(pool->type & IS_CACHED) &&
	    set_pages_array_wb(pages, npages))
		pr_err("%s: Failed to set %d pages to wb!\n",
		       pool->dev_name, npages);

	list_for_each_entry_safe(d_page, tmp, d_pages, page_list) {
		list_del(&d_page->page_list);
		__ttm_dma_free_page(pool, d_page);
	}
}

static void ttm_dma_page_put(struct dma_pool *pool, struct dma_page *d_page)
{
	/* Don't set WB on WB page pool. */
	if (!(pool->type & IS_CACHED) && set_pages_array_wb(&d_page->p, 1))
		pr_err("%s: Failed to set %d pages to wb!\n",
		       pool->dev_name, 1);

	list_del(&d_page->page_list);
	__ttm_dma_free_page(pool, d_page);
}

/*
 * Free pages from pool.
 *
 * To prevent hogging the ttm_swap process we only free NUM_PAGES_TO_ALLOC
 * number of pages in one go.
 *
 * @pool: to free the pages from
 * @nr_free: If set to true will free all pages in pool
 * @use_static: Safe to use static buffer
 **/
static unsigned ttm_dma_page_pool_free(struct dma_pool *pool, unsigned nr_free,
				       bool use_static)
{
	static struct page *static_buf[NUM_PAGES_TO_ALLOC];
	unsigned long irq_flags;
	struct dma_page *dma_p, *tmp;
	struct page **pages_to_free;
	struct list_head d_pages;
	unsigned freed_pages = 0,
		 npages_to_free = nr_free;

	if (NUM_PAGES_TO_ALLOC < nr_free)
		npages_to_free = NUM_PAGES_TO_ALLOC;
#if 0
	if (nr_free > 1) {
		pr_debug("%s: (%s:%d) Attempting to free %d (%d) pages\n",
			 pool->dev_name, pool->name, current->pid,
			 npages_to_free, nr_free);
	}
#endif
	if (use_static)
		pages_to_free = static_buf;
	else
		pages_to_free = kmalloc(npages_to_free * sizeof(struct page *),
					GFP_KERNEL);

	if (!pages_to_free) {
		pr_err("%s: Failed to allocate memory for pool free operation\n",
		       pool->dev_name);
		return 0;
	}
	INIT_LIST_HEAD(&d_pages);
restart:
	spin_lock_irqsave(&pool->lock, irq_flags);

	/* We picking the oldest ones off the list */
	list_for_each_entry_safe_reverse(dma_p, tmp, &pool->free_list,
					 page_list) {
		if (freed_pages >= npages_to_free)
			break;

		/* Move the dma_page from one list to another. */
		list_move(&dma_p->page_list, &d_pages);

		pages_to_free[freed_pages++] = dma_p->p;
		/* We can only remove NUM_PAGES_TO_ALLOC at a time. */
		if (freed_pages >= NUM_PAGES_TO_ALLOC) {

			ttm_pool_update_free_locked(pool, freed_pages);
			/**
			 * Because changing page caching is costly
			 * we unlock the pool to prevent stalling.
			 */
			spin_unlock_irqrestore(&pool->lock, irq_flags);

			ttm_dma_pages_put(pool, &d_pages, pages_to_free,
					  freed_pages);

			INIT_LIST_HEAD(&d_pages);

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
		ttm_pool_update_free_locked(pool, freed_pages);
		nr_free -= freed_pages;
	}

	spin_unlock_irqrestore(&pool->lock, irq_flags);

	if (freed_pages)
		ttm_dma_pages_put(pool, &d_pages, pages_to_free, freed_pages);
out:
	if (pages_to_free != static_buf)
		kfree(pages_to_free);
	return nr_free;
}

static void ttm_dma_free_pool(struct device *dev, enum pool_type type)
{
	struct device_pools *p;
	struct dma_pool *pool;

	if (!dev)
		return;

	mutex_lock(&_manager->lock);
	list_for_each_entry_reverse(p, &_manager->pools, pools) {
		if (p->dev != dev)
			continue;
		pool = p->pool;
		if (pool->type != type)
			continue;

		list_del(&p->pools);
		kfree(p);
		_manager->npools--;
		break;
	}
	list_for_each_entry_reverse(pool, &dev->dma_pools, pools) {
		if (pool->type != type)
			continue;
		/* Takes a spinlock.. */
		/* OK to use static buffer since global mutex is held. */
		ttm_dma_page_pool_free(pool, FREE_ALL_PAGES, true);
		WARN_ON(((pool->npages_in_use + pool->npages_free) != 0));
		/* This code path is called after _all_ references to the
		 * struct device has been dropped - so nobody should be
		 * touching it. In case somebody is trying to _add_ we are
		 * guarded by the mutex. */
		list_del(&pool->pools);
		kfree(pool);
		break;
	}
	mutex_unlock(&_manager->lock);
}

/*
 * On free-ing of the 'struct device' this deconstructor is run.
 * Albeit the pool might have already been freed earlier.
 */
static void ttm_dma_pool_release(struct device *dev, void *res)
{
	struct dma_pool *pool = *(struct dma_pool **)res;

	if (pool)
		ttm_dma_free_pool(dev, pool->type);
}

static int ttm_dma_pool_match(struct device *dev, void *res, void *match_data)
{
	return *(struct dma_pool **)res == match_data;
}

static struct dma_pool *ttm_dma_pool_init(struct device *dev, gfp_t flags,
					  enum pool_type type)
{
	char *n[] = {"wc", "uc", "cached", " dma32", "unknown",};
	enum pool_type t[] = {IS_WC, IS_UC, IS_CACHED, IS_DMA32, IS_UNDEFINED};
	struct device_pools *sec_pool = NULL;
	struct dma_pool *pool = NULL, **ptr;
	unsigned i;
	int ret = -ENODEV;
	char *p;

	if (!dev)
		return NULL;

	ptr = devres_alloc(ttm_dma_pool_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	ret = -ENOMEM;

	pool = kmalloc_node(sizeof(struct dma_pool), GFP_KERNEL,
			    dev_to_node(dev));
	if (!pool)
		goto err_mem;

	sec_pool = kmalloc_node(sizeof(struct device_pools), GFP_KERNEL,
				dev_to_node(dev));
	if (!sec_pool)
		goto err_mem;

	INIT_LIST_HEAD(&sec_pool->pools);
	sec_pool->dev = dev;
	sec_pool->pool =  pool;

	INIT_LIST_HEAD(&pool->free_list);
	INIT_LIST_HEAD(&pool->inuse_list);
	INIT_LIST_HEAD(&pool->pools);
	spin_lock_init(&pool->lock);
	pool->dev = dev;
	pool->npages_free = pool->npages_in_use = 0;
	pool->nfrees = 0;
	pool->gfp_flags = flags;
	pool->size = PAGE_SIZE;
	pool->type = type;
	pool->nrefills = 0;
	p = pool->name;
	for (i = 0; i < 5; i++) {
		if (type & t[i]) {
			p += snprintf(p, sizeof(pool->name) - (p - pool->name),
				      "%s", n[i]);
		}
	}
	*p = 0;
	/* We copy the name for pr_ calls b/c when dma_pool_destroy is called
	 * - the kobj->name has already been deallocated.*/
	snprintf(pool->dev_name, sizeof(pool->dev_name), "%s %s",
		 dev_driver_string(dev), dev_name(dev));
	mutex_lock(&_manager->lock);
	/* You can get the dma_pool from either the global: */
	list_add(&sec_pool->pools, &_manager->pools);
	_manager->npools++;
	/* or from 'struct device': */
	list_add(&pool->pools, &dev->dma_pools);
	mutex_unlock(&_manager->lock);

	*ptr = pool;
	devres_add(dev, ptr);

	return pool;
err_mem:
	devres_free(ptr);
	kfree(sec_pool);
	kfree(pool);
	return ERR_PTR(ret);
}

static struct dma_pool *ttm_dma_find_pool(struct device *dev,
					  enum pool_type type)
{
	struct dma_pool *pool, *tmp, *found = NULL;

	if (type == IS_UNDEFINED)
		return found;

	/* NB: We iterate on the 'struct dev' which has no spinlock, but
	 * it does have a kref which we have taken. The kref is taken during
	 * graphic driver loading - in the drm_pci_init it calls either
	 * pci_dev_get or pci_register_driver which both end up taking a kref
	 * on 'struct device'.
	 *
	 * On teardown, the graphic drivers end up quiescing the TTM (put_pages)
	 * and calls the dev_res deconstructors: ttm_dma_pool_release. The nice
	 * thing is at that point of time there are no pages associated with the
	 * driver so this function will not be called.
	 */
	list_for_each_entry_safe(pool, tmp, &dev->dma_pools, pools) {
		if (pool->type != type)
			continue;
		found = pool;
		break;
	}
	return found;
}

/*
 * Free pages the pages that failed to change the caching state. If there
 * are pages that have changed their caching state already put them to the
 * pool.
 */
static void ttm_dma_handle_caching_state_failure(struct dma_pool *pool,
						 struct list_head *d_pages,
						 struct page **failed_pages,
						 unsigned cpages)
{
	struct dma_page *d_page, *tmp;
	struct page *p;
	unsigned i = 0;

	p = failed_pages[0];
	if (!p)
		return;
	/* Find the failed page. */
	list_for_each_entry_safe(d_page, tmp, d_pages, page_list) {
		if (d_page->p != p)
			continue;
		/* .. and then progress over the full list. */
		list_del(&d_page->page_list);
		__ttm_dma_free_page(pool, d_page);
		if (++i < cpages)
			p = failed_pages[i];
		else
			break;
	}

}

/*
 * Allocate 'count' pages, and put 'need' number of them on the
 * 'pages' and as well on the 'dma_address' starting at 'dma_offset' offset.
 * The full list of pages should also be on 'd_pages'.
 * We return zero for success, and negative numbers as errors.
 */
static int ttm_dma_pool_alloc_new_pages(struct dma_pool *pool,
					struct list_head *d_pages,
					unsigned count)
{
	struct page **caching_array;
	struct dma_page *dma_p;
	struct page *p;
	int r = 0;
	unsigned i, cpages;
	unsigned max_cpages = min(count,
			(unsigned)(PAGE_SIZE/sizeof(struct page *)));

	/* allocate array for page caching change */
	caching_array = kmalloc(max_cpages*sizeof(struct page *), GFP_KERNEL);

	if (!caching_array) {
		pr_err("%s: Unable to allocate table for new pages\n",
		       pool->dev_name);
		return -ENOMEM;
	}

	if (count > 1) {
		pr_debug("%s: (%s:%d) Getting %d pages\n",
			 pool->dev_name, pool->name, current->pid, count);
	}

	for (i = 0, cpages = 0; i < count; ++i) {
		dma_p = __ttm_dma_alloc_page(pool);
		if (!dma_p) {
			pr_err("%s: Unable to get page %u\n",
			       pool->dev_name, i);

			/* store already allocated pages in the pool after
			 * setting the caching state */
			if (cpages) {
				r = ttm_set_pages_caching(pool, caching_array,
							  cpages);
				if (r)
					ttm_dma_handle_caching_state_failure(
						pool, d_pages, caching_array,
						cpages);
			}
			r = -ENOMEM;
			goto out;
		}
		p = dma_p->p;
#ifdef CONFIG_HIGHMEM
		/* gfp flags of highmem page should never be dma32 so we
		 * we should be fine in such case
		 */
		if (!PageHighMem(p))
#endif
		{
			caching_array[cpages++] = p;
			if (cpages == max_cpages) {
				/* Note: Cannot hold the spinlock */
				r = ttm_set_pages_caching(pool, caching_array,
						 cpages);
				if (r) {
					ttm_dma_handle_caching_state_failure(
						pool, d_pages, caching_array,
						cpages);
					goto out;
				}
				cpages = 0;
			}
		}
		list_add(&dma_p->page_list, d_pages);
	}

	if (cpages) {
		r = ttm_set_pages_caching(pool, caching_array, cpages);
		if (r)
			ttm_dma_handle_caching_state_failure(pool, d_pages,
					caching_array, cpages);
	}
out:
	kfree(caching_array);
	return r;
}

/*
 * @return count of pages still required to fulfill the request.
 */
static int ttm_dma_page_pool_fill_locked(struct dma_pool *pool,
					 unsigned long *irq_flags)
{
	unsigned count = _manager->options.small;
	int r = pool->npages_free;

	if (count > pool->npages_free) {
		struct list_head d_pages;

		INIT_LIST_HEAD(&d_pages);

		spin_unlock_irqrestore(&pool->lock, *irq_flags);

		/* Returns how many more are neccessary to fulfill the
		 * request. */
		r = ttm_dma_pool_alloc_new_pages(pool, &d_pages, count);

		spin_lock_irqsave(&pool->lock, *irq_flags);
		if (!r) {
			/* Add the fresh to the end.. */
			list_splice(&d_pages, &pool->free_list);
			++pool->nrefills;
			pool->npages_free += count;
			r = count;
		} else {
			struct dma_page *d_page;
			unsigned cpages = 0;

			pr_err("%s: Failed to fill %s pool (r:%d)!\n",
			       pool->dev_name, pool->name, r);

			list_for_each_entry(d_page, &d_pages, page_list) {
				cpages++;
			}
			list_splice_tail(&d_pages, &pool->free_list);
			pool->npages_free += cpages;
			r = cpages;
		}
	}
	return r;
}

/*
 * @return count of pages still required to fulfill the request.
 * The populate list is actually a stack (not that is matters as TTM
 * allocates one page at a time.
 */
static int ttm_dma_pool_get_pages(struct dma_pool *pool,
				  struct ttm_dma_tt *ttm_dma,
				  unsigned index)
{
	struct dma_page *d_page;
	struct ttm_tt *ttm = &ttm_dma->ttm;
	unsigned long irq_flags;
	int count, r = -ENOMEM;

	spin_lock_irqsave(&pool->lock, irq_flags);
	count = ttm_dma_page_pool_fill_locked(pool, &irq_flags);
	if (count) {
		d_page = list_first_entry(&pool->free_list, struct dma_page, page_list);
		ttm->pages[index] = d_page->p;
		ttm_dma->cpu_address[index] = d_page->vaddr;
		ttm_dma->dma_address[index] = d_page->dma;
		list_move_tail(&d_page->page_list, &ttm_dma->pages_list);
		r = 0;
		pool->npages_in_use += 1;
		pool->npages_free -= 1;
	}
	spin_unlock_irqrestore(&pool->lock, irq_flags);
	return r;
}

/*
 * On success pages list will hold count number of correctly
 * cached pages. On failure will hold the negative return value (-ENOMEM, etc).
 */
int ttm_dma_populate(struct ttm_dma_tt *ttm_dma, struct device *dev)
{
	struct ttm_tt *ttm = &ttm_dma->ttm;
	struct ttm_mem_global *mem_glob = ttm->glob->mem_glob;
	struct dma_pool *pool;
	enum pool_type type;
	unsigned i;
	gfp_t gfp_flags;
	int ret;

	if (ttm->state != tt_unpopulated)
		return 0;

	type = ttm_to_type(ttm->page_flags, ttm->caching_state);
	if (ttm->page_flags & TTM_PAGE_FLAG_DMA32)
		gfp_flags = GFP_USER | GFP_DMA32;
	else
		gfp_flags = GFP_HIGHUSER;
	if (ttm->page_flags & TTM_PAGE_FLAG_ZERO_ALLOC)
		gfp_flags |= __GFP_ZERO;

	pool = ttm_dma_find_pool(dev, type);
	if (!pool) {
		pool = ttm_dma_pool_init(dev, gfp_flags, type);
		if (IS_ERR_OR_NULL(pool)) {
			return -ENOMEM;
		}
	}

	INIT_LIST_HEAD(&ttm_dma->pages_list);
	for (i = 0; i < ttm->num_pages; ++i) {
		ret = ttm_dma_pool_get_pages(pool, ttm_dma, i);
		if (ret != 0) {
			ttm_dma_unpopulate(ttm_dma, dev);
			return -ENOMEM;
		}

		ret = ttm_mem_global_alloc_page(mem_glob, ttm->pages[i],
						false, false);
		if (unlikely(ret != 0)) {
			ttm_dma_unpopulate(ttm_dma, dev);
			return -ENOMEM;
		}
	}

	if (unlikely(ttm->page_flags & TTM_PAGE_FLAG_SWAPPED)) {
		ret = ttm_tt_swapin(ttm);
		if (unlikely(ret != 0)) {
			ttm_dma_unpopulate(ttm_dma, dev);
			return ret;
		}
	}

	ttm->state = tt_unbound;
	return 0;
}
EXPORT_SYMBOL_GPL(ttm_dma_populate);

/* Put all pages in pages list to correct pool to wait for reuse */
void ttm_dma_unpopulate(struct ttm_dma_tt *ttm_dma, struct device *dev)
{
	struct ttm_tt *ttm = &ttm_dma->ttm;
	struct dma_pool *pool;
	struct dma_page *d_page, *next;
	enum pool_type type;
	bool is_cached = false;
	unsigned count = 0, i, npages = 0;
	unsigned long irq_flags;

	type = ttm_to_type(ttm->page_flags, ttm->caching_state);
	pool = ttm_dma_find_pool(dev, type);
	if (!pool)
		return;

	is_cached = (ttm_dma_find_pool(pool->dev,
		     ttm_to_type(ttm->page_flags, tt_cached)) == pool);

	/* make sure pages array match list and count number of pages */
	list_for_each_entry(d_page, &ttm_dma->pages_list, page_list) {
		ttm->pages[count] = d_page->p;
		count++;
	}

	spin_lock_irqsave(&pool->lock, irq_flags);
	pool->npages_in_use -= count;
	if (is_cached) {
		pool->nfrees += count;
	} else {
		pool->npages_free += count;
		list_splice(&ttm_dma->pages_list, &pool->free_list);
		/*
		 * Wait to have at at least NUM_PAGES_TO_ALLOC number of pages
		 * to free in order to minimize calls to set_memory_wb().
		 */
		if (pool->npages_free >= (_manager->options.max_size +
					  NUM_PAGES_TO_ALLOC))
			npages = pool->npages_free - _manager->options.max_size;
	}
	spin_unlock_irqrestore(&pool->lock, irq_flags);

	if (is_cached) {
		list_for_each_entry_safe(d_page, next, &ttm_dma->pages_list, page_list) {
			ttm_mem_global_free_page(ttm->glob->mem_glob,
						 d_page->p);
			ttm_dma_page_put(pool, d_page);
		}
	} else {
		for (i = 0; i < count; i++) {
			ttm_mem_global_free_page(ttm->glob->mem_glob,
						 ttm->pages[i]);
		}
	}

	INIT_LIST_HEAD(&ttm_dma->pages_list);
	for (i = 0; i < ttm->num_pages; i++) {
		ttm->pages[i] = NULL;
		ttm_dma->cpu_address[i] = 0;
		ttm_dma->dma_address[i] = 0;
	}

	/* shrink pool if necessary (only on !is_cached pools)*/
	if (npages)
		ttm_dma_page_pool_free(pool, npages, false);
	ttm->state = tt_unpopulated;
}
EXPORT_SYMBOL_GPL(ttm_dma_unpopulate);

/**
 * Callback for mm to request pool to reduce number of page held.
 *
 * XXX: (dchinner) Deadlock warning!
 *
 * I'm getting sadder as I hear more pathetical whimpers about needing per-pool
 * shrinkers
 */
static unsigned long
ttm_dma_pool_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	static unsigned start_pool;
	unsigned idx = 0;
	unsigned pool_offset;
	unsigned shrink_pages = sc->nr_to_scan;
	struct device_pools *p;
	unsigned long freed = 0;

	if (list_empty(&_manager->pools))
		return SHRINK_STOP;

	if (!mutex_trylock(&_manager->lock))
		return SHRINK_STOP;
	if (!_manager->npools)
		goto out;
	pool_offset = ++start_pool % _manager->npools;
	list_for_each_entry(p, &_manager->pools, pools) {
		unsigned nr_free;

		if (!p->dev)
			continue;
		if (shrink_pages == 0)
			break;
		/* Do it in round-robin fashion. */
		if (++idx < pool_offset)
			continue;
		nr_free = shrink_pages;
		/* OK to use static buffer since global mutex is held. */
		shrink_pages = ttm_dma_page_pool_free(p->pool, nr_free, true);
		freed += nr_free - shrink_pages;

		pr_debug("%s: (%s:%d) Asked to shrink %d, have %d more to go\n",
			 p->pool->dev_name, p->pool->name, current->pid,
			 nr_free, shrink_pages);
	}
out:
	mutex_unlock(&_manager->lock);
	return freed;
}

static unsigned long
ttm_dma_pool_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	struct device_pools *p;
	unsigned long count = 0;

	if (!mutex_trylock(&_manager->lock))
		return 0;
	list_for_each_entry(p, &_manager->pools, pools)
		count += p->pool->npages_free;
	mutex_unlock(&_manager->lock);
	return count;
}

static void ttm_dma_pool_mm_shrink_init(struct ttm_pool_manager *manager)
{
	manager->mm_shrink.count_objects = ttm_dma_pool_shrink_count;
	manager->mm_shrink.scan_objects = &ttm_dma_pool_shrink_scan;
	manager->mm_shrink.seeks = 1;
	register_shrinker(&manager->mm_shrink);
}

static void ttm_dma_pool_mm_shrink_fini(struct ttm_pool_manager *manager)
{
	unregister_shrinker(&manager->mm_shrink);
}

int ttm_dma_page_alloc_init(struct ttm_mem_global *glob, unsigned max_pages)
{
	int ret = -ENOMEM;

	WARN_ON(_manager);

	pr_info("Initializing DMA pool allocator\n");

	_manager = kzalloc(sizeof(*_manager), GFP_KERNEL);
	if (!_manager)
		goto err;

	mutex_init(&_manager->lock);
	INIT_LIST_HEAD(&_manager->pools);

	_manager->options.max_size = max_pages;
	_manager->options.small = SMALL_ALLOCATION;
	_manager->options.alloc_size = NUM_PAGES_TO_ALLOC;

	/* This takes care of auto-freeing the _manager */
	ret = kobject_init_and_add(&_manager->kobj, &ttm_pool_kobj_type,
				   &glob->kobj, "dma_pool");
	if (unlikely(ret != 0)) {
		kobject_put(&_manager->kobj);
		goto err;
	}
	ttm_dma_pool_mm_shrink_init(_manager);
	return 0;
err:
	return ret;
}

void ttm_dma_page_alloc_fini(void)
{
	struct device_pools *p, *t;

	pr_info("Finalizing DMA pool allocator\n");
	ttm_dma_pool_mm_shrink_fini(_manager);

	list_for_each_entry_safe_reverse(p, t, &_manager->pools, pools) {
		dev_dbg(p->dev, "(%s:%d) Freeing.\n", p->pool->name,
			current->pid);
		WARN_ON(devres_destroy(p->dev, ttm_dma_pool_release,
			ttm_dma_pool_match, p->pool));
		ttm_dma_free_pool(p->dev, p->pool->type);
	}
	kobject_put(&_manager->kobj);
	_manager = NULL;
}

int ttm_dma_page_alloc_debugfs(struct seq_file *m, void *data)
{
	struct device_pools *p;
	struct dma_pool *pool = NULL;
	char *h[] = {"pool", "refills", "pages freed", "inuse", "available",
		     "name", "virt", "busaddr"};

	if (!_manager) {
		seq_printf(m, "No pool allocator running.\n");
		return 0;
	}
	seq_printf(m, "%13s %12s %13s %8s %8s %8s\n",
		   h[0], h[1], h[2], h[3], h[4], h[5]);
	mutex_lock(&_manager->lock);
	list_for_each_entry(p, &_manager->pools, pools) {
		struct device *dev = p->dev;
		if (!dev)
			continue;
		pool = p->pool;
		seq_printf(m, "%13s %12ld %13ld %8d %8d %8s\n",
				pool->name, pool->nrefills,
				pool->nfrees, pool->npages_in_use,
				pool->npages_free,
				pool->dev_name);
	}
	mutex_unlock(&_manager->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(ttm_dma_page_alloc_debugfs);

#endif
