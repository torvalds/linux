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

#include <linux/mm_types.h>
#include <linux/types.h>

struct dmabuf_page_pool;

struct dmabuf_page_pool *dmabuf_page_pool_create(gfp_t gfp_mask,
						 unsigned int order);
void dmabuf_page_pool_destroy(struct dmabuf_page_pool *pool);
struct page *dmabuf_page_pool_alloc(struct dmabuf_page_pool *pool);
void dmabuf_page_pool_free(struct dmabuf_page_pool *pool, struct page *page);

/* get pool size in bytes */
unsigned long dmabuf_page_pool_get_size(struct dmabuf_page_pool *pool);

#endif /* _DMABUF_PAGE_POOL_H */
