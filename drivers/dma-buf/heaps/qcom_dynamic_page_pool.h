/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ION Memory Allocator kernel interface header
 *
 * Taken from:
 * include/linux/dynamic_page_pool.h
 * Git-repo: https://git.linaro.org/people/john.stultz/android-dev.git
 * Branch: dma-buf-heap-perf
 * Git-commit: 458ea8030852755867bdc0384aa40f97aba7a572
 *
 * Copyright (C) 2011 Google, Inc.
 *
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _DYN_PAGE_POOL_H
#define _DYN_PAGE_POOL_H

#include <linux/device.h>
#include <linux/kref.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/shrinker.h>
#include <linux/types.h>

#define HIGH_ORDER_GFP  (((GFP_HIGHUSER | __GFP_ZERO | __GFP_NOWARN \
				| __GFP_NORETRY) & ~__GFP_RECLAIM) \
				| __GFP_COMP)
#define LOW_ORDER_GFP (GFP_HIGHUSER | __GFP_ZERO | __GFP_COMP)

/*
 * We add __GFP_NOWARN for order 4 allocations since the core mm/ framework
 * makes no guarantee of these allocations succeeding.
 */
static gfp_t order_flags[] = {HIGH_ORDER_GFP, HIGH_ORDER_GFP,
			      LOW_ORDER_GFP};
#if defined(CONFIG_IOMMU_IO_PGTABLE_ARMV7S) && !defined(CONFIG_64BIT) && !defined(CONFIG_ARM_LPAE)
static const unsigned int orders[] = {8, 4, 0};
#else
static const unsigned int orders[] = {9, 4, 0};
#endif
#define NUM_ORDERS ARRAY_SIZE(orders)

enum dynamic_pool_callback_ret {
	DYNAMIC_POOL_SUCCESS,
	DYNAMIC_POOL_FAILURE,
};

struct dynamic_page_pool;

typedef enum dynamic_pool_callback_ret (*prerelease_callback)(struct dynamic_page_pool *pool,
							      struct list_head *pages,
							      int num_pages);

/**
 * struct dynamic_page_pool - pagepool struct
 * @high_count:			number of highmem items in the pool
 * @low_count:			number of lowmem items in the pool
 * @count:			total number of pages/items in the pool
 * @high_items:			list of highmem items
 * @low_items:			list of lowmem items
 * @last_low_watermark_ktime:	most recent time at which the zone watermarks were
 *				low
 * @refill_worker:		kthread used to refill a pool, if applicable
 * @lock:			lock protecting this struct and especially the count
 *				item list
 * @gfp_mask:			gfp_mask to use from alloc
 * @order:			order of pages in the pool
 * @list:			list node for list of pools
 * @vmid:			the vmid used for this pool
 * @prerelease_callback:	preprocessing function called before pages are
 *				released to buddy
 *
 * Allows you to keep a pool of pre allocated pages to use
 * Keeping a pool of pages that is ready for dma, ie any cached mapping have
 * been invalidated from the cache, provides a significant performance benefit
 * on many systems
 */
struct dynamic_page_pool {
	int high_count;
	int low_count;
	atomic_t count;
	struct list_head high_items;
	struct list_head low_items;
	ktime_t last_low_watermark_ktime;
	struct task_struct *refill_worker;
	spinlock_t lock;
	gfp_t gfp_mask;
	unsigned int order;
	struct list_head list;
	int vmid;
	prerelease_callback prerelease_callback;
};

struct dynamic_page_pool **dynamic_page_pool_create_pools(int vmid,
							  prerelease_callback callback);
void dynamic_page_pool_release_pools(struct dynamic_page_pool **pool_list);

struct page *dynamic_page_pool_alloc(struct dynamic_page_pool *pool);
void dynamic_page_pool_free(struct dynamic_page_pool *pool, struct page *page);

int dynamic_page_pool_init_shrinker(void);
void dynamic_page_pool_shrink_high_and_low(struct dynamic_page_pool **pools_list,
					   int num_pools, int nr_to_scan);
int dynamic_page_pool_total(struct dynamic_page_pool *pool, bool high);

struct page *dynamic_page_pool_remove(struct dynamic_page_pool *pool, bool high);
void dynamic_page_pool_add(struct dynamic_page_pool *pool, struct page *page);

#endif /* _DYN_PAGE_POOL_H */
