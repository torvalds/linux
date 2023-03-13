/* SPDX-License-Identifier: GPL-2.0 */
/*
 * DMA BUF PagePool implementation
 * Based on earlier ION code by Google
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2020 Linaro Ltd.
 */

#ifndef _DMABUF_PAGE_POOL_H
#define _DMABUF_PAGE_POOL_H

#include <linux/device.h>
#include <linux/kref.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/shrinker.h>
#include <linux/types.h>

/* page types we track in the pool */
enum {
	POOL_LOWPAGE,      /* Clean lowmem pages */
	POOL_HIGHPAGE,     /* Clean highmem pages */

	POOL_TYPE_SIZE,
};

/**
 * struct dmabuf_page_pool - pagepool struct
 * @count[]:		array of number of pages of that type in the pool
 * @items[]:		array of list of pages of the specific type
 * @mutex:		lock protecting this struct and especially the count
 *			item list
 * @gfp_mask:		gfp_mask to use from alloc
 * @order:		order of pages in the pool
 * @list:		list node for list of pools
 *
 * Allows you to keep a pool of pre allocated pages to use
 */
struct dmabuf_page_pool {
	int count[POOL_TYPE_SIZE];
	struct list_head items[POOL_TYPE_SIZE];
	struct mutex mutex; /* No longer used! */
	gfp_t gfp_mask;
	unsigned int order;
	struct list_head list;
};

struct dmabuf_page_pool *dmabuf_page_pool_create(gfp_t gfp_mask,
						 unsigned int order);
void dmabuf_page_pool_destroy(struct dmabuf_page_pool *pool);
struct page *dmabuf_page_pool_alloc(struct dmabuf_page_pool *pool);
void dmabuf_page_pool_free(struct dmabuf_page_pool *pool, struct page *page);

#endif /* _DMABUF_PAGE_POOL_H */
