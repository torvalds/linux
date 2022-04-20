/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019 Linaro Ltd.
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _QCOM_DMA_HEAP_PRIV_H
#define _QCOM_DMA_HEAP_PRIV_H

#include <linux/dma-heap.h>
#include <linux/list.h>

/**
 * struct heap_helper_buffer - helper buffer metadata
 * @heap:		back pointer to the heap the buffer came from
 * @dmabuf:		backing dma-buf for this buffer
 * @size:		size of the buffer
 * @priv_virt		pointer to heap specific private value
 * @lock		mutext to protect the data in this structure
 * @vmap_cnt		count of vmap references on the buffer
 * @vaddr		vmap'ed virtual address
 * @pagecount		number of pages in the buffer
 * @pages		list of page pointers
 * @attachments		list of device attachments
 *
 * @free		heap callback to free the buffer
 */
struct heap_helper_buffer {
	struct dma_heap *heap;
	struct dma_buf *dmabuf;
	size_t size;

	void *priv_virt;
	struct mutex lock;
	int vmap_cnt;
	void *vaddr;
	pgoff_t pagecount;
	struct page **pages;
	struct list_head attachments;

	void (*free)(struct heap_helper_buffer *buffer);
};

#endif /* _QCOM_DMA_HEAP_PRIV_H */
