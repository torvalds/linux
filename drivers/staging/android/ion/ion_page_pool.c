// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/staging/android/ion/ion_mem_pool.c
 *
 * Copyright (C) 2011 Google, Inc.
 */

#include <linux/dma-buf.h>
#include <linux/list.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/sched/signal.h>

#include "ion.h"

/*
 * We avoid atomic_long_t to minimize cache flushes at the cost of possible
 * race which would result in a small accounting inaccuracy that we can
 * tolerate.
 */
static long nr_total_pages;

static inline struct page *ion_page_pool_alloc_pages(struct ion_page_pool *pool)
{
	if (fatal_signal_pending(current))
		return NULL;
	return alloc_pages(pool->gfp_mask, pool->order);
}

static void ion_page_pool_free_pages(struct ion_page_pool *pool,
				     struct page *page)
{
	__free_pages(page, pool->order);
}

static void ion_page_pool_add(struct ion_page_pool *pool, struct page *page)
{
	mutex_lock(&pool->mutex);
	if (PageHighMem(page)) {
		list_add_tail(&page->lru, &pool->high_items);
		pool->high_count++;
	} else {
		list_add_tail(&page->lru, &pool->low_items);
		pool->low_count++;
	}

	nr_total_pages += 1 << pool->order;
	mod_node_page_state(page_pgdat(page), NR_KERNEL_MISC_RECLAIMABLE,
							1 << pool->order);
	mutex_unlock(&pool->mutex);
}

static struct page *ion_page_pool_remove(struct ion_page_pool *pool, bool high)
{
	struct page *page;

	if (high) {
		BUG_ON(!pool->high_count);
		page = list_first_entry(&pool->high_items, struct page, lru);
		pool->high_count--;
	} else {
		BUG_ON(!pool->low_count);
		page = list_first_entry(&pool->low_items, struct page, lru);
		pool->low_count--;
	}

	list_del(&page->lru);
	nr_total_pages -= 1 << pool->order;
	mod_node_page_state(page_pgdat(page), NR_KERNEL_MISC_RECLAIMABLE,
							-(1 << pool->order));
	return page;
}

static void page_sync_for_device(struct device *dev, struct page *page,
				 size_t size, enum dma_data_direction dir)
{
	struct scatterlist sg;

	if (!dev || !page)
		return;

	sg_init_table(&sg, 1);
	sg_set_page(&sg, page, size, 0);
	sg_dma_address(&sg) = page_to_phys(page);
	dma_sync_sg_for_device(dev, &sg, 1, dir);
}

struct page *ion_page_pool_alloc(struct ion_page_pool *pool)
{
	struct page *page = NULL;

	BUG_ON(!pool);

	mutex_lock(&pool->mutex);
	if (pool->high_count)
		page = ion_page_pool_remove(pool, true);
	else if (pool->low_count)
		page = ion_page_pool_remove(pool, false);
	mutex_unlock(&pool->mutex);

	if (!page) {
		page = ion_page_pool_alloc_pages(pool);
		page_sync_for_device(pool->dev, page,
				     PAGE_SIZE << pool->order,
				     DMA_BIDIRECTIONAL);
	}

	return page;
}

void ion_page_pool_free(struct ion_page_pool *pool, struct page *page)
{
	BUG_ON(pool->order != compound_order(page));

	ion_page_pool_add(pool, page);
}

static int ion_page_pool_total(struct ion_page_pool *pool, bool high)
{
	int count = pool->low_count;

	if (high)
		count += pool->high_count;

	return count << pool->order;
}

long ion_page_pool_nr_pages(void)
{
	/* Correct possible overflow caused by racing writes */
	if (nr_total_pages < 0)
		nr_total_pages = 0;
	return nr_total_pages;
}

int ion_page_pool_shrink(struct ion_page_pool *pool, gfp_t gfp_mask,
			 int nr_to_scan)
{
	int freed = 0;
	bool high;

	if (current_is_kswapd())
		high = true;
	else
		high = !!(gfp_mask & __GFP_HIGHMEM);

	if (nr_to_scan == 0)
		return ion_page_pool_total(pool, high);

	while (freed < nr_to_scan) {
		struct page *page;

		mutex_lock(&pool->mutex);
		if (pool->low_count) {
			page = ion_page_pool_remove(pool, false);
		} else if (high && pool->high_count) {
			page = ion_page_pool_remove(pool, true);
		} else {
			mutex_unlock(&pool->mutex);
			break;
		}
		mutex_unlock(&pool->mutex);
		ion_page_pool_free_pages(pool, page);
		freed += (1 << pool->order);
	}

	return freed;
}

static const struct platform_device_info ion_dev_info = {
	.name		= "ion-pool",
	.id		= PLATFORM_DEVID_AUTO,
	.dma_mask	= DMA_BIT_MASK(32),
};

static struct platform_device *platform_device_register_dma(const char *name)
{
	struct platform_device *pdev;
	int ret;

	pdev = platform_device_register_full(&ion_dev_info);
	if (pdev) {
		ret = of_dma_configure(&pdev->dev, NULL, true);
		if (ret) {
			platform_device_unregister(pdev);
			pdev = NULL;
		}
	}

	return pdev;
}

struct ion_page_pool *ion_page_pool_create(gfp_t gfp_mask, unsigned int order)
{
	struct platform_device *pdev;
	struct ion_page_pool *pool = kzalloc(sizeof(*pool), GFP_KERNEL);

	if (!pool)
		return NULL;
	pool->high_count = 0;
	pool->low_count = 0;
	INIT_LIST_HEAD(&pool->low_items);
	INIT_LIST_HEAD(&pool->high_items);
	pool->gfp_mask = gfp_mask | __GFP_COMP;
	pool->order = order;
	mutex_init(&pool->mutex);
	plist_node_init(&pool->list, order);

	pdev = platform_device_register_dma("ion_pool");
	if (!IS_ERR(pdev))
		pool->dev = &pdev->dev;

	return pool;
}

void ion_page_pool_destroy(struct ion_page_pool *pool)
{
	if (pool->dev) {
		struct platform_device *pdev = to_platform_device(pool->dev);

		of_dma_deconfigure(&pdev->dev);
		platform_device_unregister(pdev);
	}
	kfree(pool);
}
