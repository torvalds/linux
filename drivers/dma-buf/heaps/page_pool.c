// SPDX-License-Identifier: GPL-2.0
/*
 * DMA BUF page pool system
 *
 * Copyright (C) 2020 Linaro Ltd.
 *
 * Based on the ION page pool code
 * Copyright (C) 2011 Google, Inc.
 */

#include <linux/freezer.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/sched/signal.h>
#include "page_pool.h"

static LIST_HEAD(pool_list);
static DEFINE_MUTEX(pool_list_lock);

static inline
struct page *dmabuf_page_pool_alloc_pages(struct dmabuf_page_pool *pool)
{
	if (fatal_signal_pending(current))
		return NULL;
	return alloc_pages(pool->gfp_mask, pool->order);
}

static inline void dmabuf_page_pool_free_pages(struct dmabuf_page_pool *pool,
					       struct page *page)
{
	__free_pages(page, pool->order);
}

static void dmabuf_page_pool_add(struct dmabuf_page_pool *pool, struct page *page)
{
	int index;

	if (PageHighMem(page))
		index = POOL_HIGHPAGE;
	else
		index = POOL_LOWPAGE;

	mutex_lock(&pool->mutex);
	list_add_tail(&page->lru, &pool->items[index]);
	pool->count[index]++;
	mod_node_page_state(page_pgdat(page), NR_KERNEL_MISC_RECLAIMABLE,
			    1 << pool->order);
	mutex_unlock(&pool->mutex);
}

static struct page *dmabuf_page_pool_remove(struct dmabuf_page_pool *pool, int index)
{
	struct page *page;

	mutex_lock(&pool->mutex);
	page = list_first_entry_or_null(&pool->items[index], struct page, lru);
	if (page) {
		pool->count[index]--;
		list_del(&page->lru);
		mod_node_page_state(page_pgdat(page), NR_KERNEL_MISC_RECLAIMABLE,
				    -(1 << pool->order));
	}
	mutex_unlock(&pool->mutex);

	return page;
}

static struct page *dmabuf_page_pool_fetch(struct dmabuf_page_pool *pool)
{
	struct page *page = NULL;

	page = dmabuf_page_pool_remove(pool, POOL_HIGHPAGE);
	if (!page)
		page = dmabuf_page_pool_remove(pool, POOL_LOWPAGE);

	return page;
}

struct page *dmabuf_page_pool_alloc(struct dmabuf_page_pool *pool)
{
	struct page *page = NULL;

	if (WARN_ON(!pool))
		return NULL;

	page = dmabuf_page_pool_fetch(pool);

	if (!page)
		page = dmabuf_page_pool_alloc_pages(pool);
	return page;
}
EXPORT_SYMBOL_GPL(dmabuf_page_pool_alloc);

void dmabuf_page_pool_free(struct dmabuf_page_pool *pool, struct page *page)
{
	if (WARN_ON(pool->order != compound_order(page)))
		return;

	dmabuf_page_pool_add(pool, page);
}
EXPORT_SYMBOL_GPL(dmabuf_page_pool_free);

static int dmabuf_page_pool_total(struct dmabuf_page_pool *pool, bool high)
{
	int count = pool->count[POOL_LOWPAGE];

	if (high)
		count += pool->count[POOL_HIGHPAGE];

	return count << pool->order;
}

struct dmabuf_page_pool *dmabuf_page_pool_create(gfp_t gfp_mask, unsigned int order)
{
	struct dmabuf_page_pool *pool = kmalloc(sizeof(*pool), GFP_KERNEL);
	int i;

	if (!pool)
		return NULL;

	for (i = 0; i < POOL_TYPE_SIZE; i++) {
		pool->count[i] = 0;
		INIT_LIST_HEAD(&pool->items[i]);
	}
	pool->gfp_mask = gfp_mask | __GFP_COMP;
	pool->order = order;
	mutex_init(&pool->mutex);

	mutex_lock(&pool_list_lock);
	list_add(&pool->list, &pool_list);
	mutex_unlock(&pool_list_lock);

	return pool;
}
EXPORT_SYMBOL_GPL(dmabuf_page_pool_create);

void dmabuf_page_pool_destroy(struct dmabuf_page_pool *pool)
{
	struct page *page;
	int i;

	/* Remove us from the pool list */
	mutex_lock(&pool_list_lock);
	list_del(&pool->list);
	mutex_unlock(&pool_list_lock);

	/* Free any remaining pages in the pool */
	for (i = 0; i < POOL_TYPE_SIZE; i++) {
		while ((page = dmabuf_page_pool_remove(pool, i)))
			dmabuf_page_pool_free_pages(pool, page);
	}

	kfree(pool);
}
EXPORT_SYMBOL_GPL(dmabuf_page_pool_destroy);

static int dmabuf_page_pool_do_shrink(struct dmabuf_page_pool *pool, gfp_t gfp_mask,
				      int nr_to_scan)
{
	int freed = 0;
	bool high;

	if (current_is_kswapd())
		high = true;
	else
		high = !!(gfp_mask & __GFP_HIGHMEM);

	if (nr_to_scan == 0)
		return dmabuf_page_pool_total(pool, high);

	while (freed < nr_to_scan) {
		struct page *page;

		/* Try to free low pages first */
		page = dmabuf_page_pool_remove(pool, POOL_LOWPAGE);
		if (!page)
			page = dmabuf_page_pool_remove(pool, POOL_HIGHPAGE);

		if (!page)
			break;

		dmabuf_page_pool_free_pages(pool, page);
		freed += (1 << pool->order);
	}

	return freed;
}

static int dmabuf_page_pool_shrink(gfp_t gfp_mask, int nr_to_scan)
{
	struct dmabuf_page_pool *pool;
	int nr_total = 0;
	int nr_freed;
	int only_scan = 0;

	if (!nr_to_scan)
		only_scan = 1;

	mutex_lock(&pool_list_lock);
	list_for_each_entry(pool, &pool_list, list) {
		if (only_scan) {
			nr_total += dmabuf_page_pool_do_shrink(pool,
							       gfp_mask,
							       nr_to_scan);
		} else {
			nr_freed = dmabuf_page_pool_do_shrink(pool,
							      gfp_mask,
							      nr_to_scan);
			nr_to_scan -= nr_freed;
			nr_total += nr_freed;
			if (nr_to_scan <= 0)
				break;
		}
	}
	mutex_unlock(&pool_list_lock);

	return nr_total;
}

static unsigned long dmabuf_page_pool_shrink_count(struct shrinker *shrinker,
						   struct shrink_control *sc)
{
	return dmabuf_page_pool_shrink(sc->gfp_mask, 0);
}

static unsigned long dmabuf_page_pool_shrink_scan(struct shrinker *shrinker,
						  struct shrink_control *sc)
{
	if (sc->nr_to_scan == 0)
		return 0;
	return dmabuf_page_pool_shrink(sc->gfp_mask, sc->nr_to_scan);
}

struct shrinker pool_shrinker = {
	.count_objects = dmabuf_page_pool_shrink_count,
	.scan_objects = dmabuf_page_pool_shrink_scan,
	.seeks = DEFAULT_SEEKS,
	.batch = 0,
};

static int dmabuf_page_pool_init_shrinker(void)
{
	return register_shrinker(&pool_shrinker);
}
module_init(dmabuf_page_pool_init_shrinker);
MODULE_LICENSE("GPL v2");
