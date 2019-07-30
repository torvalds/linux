/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Intel Corporation */

#ifndef __IPU3_UTIL_H
#define __IPU3_UTIL_H

struct device;
struct imgu_device;

#define IPU3_CSS_POOL_SIZE		4

/**
 * imgu_css_map - store DMA mapping info for buffer
 *
 * @size:		size of the buffer in bytes.
 * @vaddr:		kernel virtual address.
 * @daddr:		iova dma address to access IPU3.
 * @vma:		private, a pointer to &struct vm_struct,
 *			used for imgu_dmamap_free.
 */
struct imgu_css_map {
	size_t size;
	void *vaddr;
	dma_addr_t daddr;
	struct vm_struct *vma;
};

/**
 * imgu_css_pool - circular buffer pool definition
 *
 * @entry:		array with IPU3_CSS_POOL_SIZE elements.
 * @entry.param:	a &struct imgu_css_map for storing the mem mapping.
 * @entry.valid:	used to mark if the entry has valid data.
 * @last:		write pointer, initialized to IPU3_CSS_POOL_SIZE.
 */
struct imgu_css_pool {
	struct {
		struct imgu_css_map param;
		bool valid;
	} entry[IPU3_CSS_POOL_SIZE];
	u32 last;
};

int imgu_css_dma_buffer_resize(struct imgu_device *imgu,
			       struct imgu_css_map *map, size_t size);
void imgu_css_pool_cleanup(struct imgu_device *imgu,
			   struct imgu_css_pool *pool);
int imgu_css_pool_init(struct imgu_device *imgu, struct imgu_css_pool *pool,
		       size_t size);
void imgu_css_pool_get(struct imgu_css_pool *pool);
void imgu_css_pool_put(struct imgu_css_pool *pool);
const struct imgu_css_map *imgu_css_pool_last(struct imgu_css_pool *pool,
					      u32 last);

#endif
