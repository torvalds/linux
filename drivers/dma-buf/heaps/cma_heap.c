// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF CMA heap exporter
 *
 * Copyright (C) 2012, 2019 Linaro Ltd.
 * Author: <benjamin.gaignard@linaro.org> for ST-Ericsson.
 */

#include <linux/cma.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/dma-map-ops.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/sched/signal.h>

#include "heap-helpers.h"

struct cma_heap {
	struct dma_heap *heap;
	struct cma *cma;
};

static void cma_heap_free(struct heap_helper_buffer *buffer)
{
	struct cma_heap *cma_heap = dma_heap_get_drvdata(buffer->heap);
	unsigned long nr_pages = buffer->pagecount;
	struct page *cma_pages = buffer->priv_virt;

	/* free page list */
	kfree(buffer->pages);
	/* release memory */
	cma_release(cma_heap->cma, cma_pages, nr_pages);
	kfree(buffer);
}

/* dmabuf heap CMA operations functions */
static int cma_heap_allocate(struct dma_heap *heap,
			     unsigned long len,
			     unsigned long fd_flags,
			     unsigned long heap_flags)
{
	struct cma_heap *cma_heap = dma_heap_get_drvdata(heap);
	struct heap_helper_buffer *helper_buffer;
	struct page *cma_pages;
	size_t size = PAGE_ALIGN(len);
	unsigned long nr_pages = size >> PAGE_SHIFT;
	unsigned long align = get_order(size);
	struct dma_buf *dmabuf;
	int ret = -ENOMEM;
	pgoff_t pg;

	if (align > CONFIG_CMA_ALIGNMENT)
		align = CONFIG_CMA_ALIGNMENT;

	helper_buffer = kzalloc(sizeof(*helper_buffer), GFP_KERNEL);
	if (!helper_buffer)
		return -ENOMEM;

	init_heap_helper_buffer(helper_buffer, cma_heap_free);
	helper_buffer->heap = heap;
	helper_buffer->size = len;

	cma_pages = cma_alloc(cma_heap->cma, nr_pages, align, false);
	if (!cma_pages)
		goto free_buf;

	if (PageHighMem(cma_pages)) {
		unsigned long nr_clear_pages = nr_pages;
		struct page *page = cma_pages;

		while (nr_clear_pages > 0) {
			void *vaddr = kmap_atomic(page);

			memset(vaddr, 0, PAGE_SIZE);
			kunmap_atomic(vaddr);
			/*
			 * Avoid wasting time zeroing memory if the process
			 * has been killed by by SIGKILL
			 */
			if (fatal_signal_pending(current))
				goto free_cma;

			page++;
			nr_clear_pages--;
		}
	} else {
		memset(page_address(cma_pages), 0, size);
	}

	helper_buffer->pagecount = nr_pages;
	helper_buffer->pages = kmalloc_array(helper_buffer->pagecount,
					     sizeof(*helper_buffer->pages),
					     GFP_KERNEL);
	if (!helper_buffer->pages) {
		ret = -ENOMEM;
		goto free_cma;
	}

	for (pg = 0; pg < helper_buffer->pagecount; pg++)
		helper_buffer->pages[pg] = &cma_pages[pg];

	/* create the dmabuf */
	dmabuf = heap_helper_export_dmabuf(helper_buffer, fd_flags);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_pages;
	}

	helper_buffer->dmabuf = dmabuf;
	helper_buffer->priv_virt = cma_pages;

	ret = dma_buf_fd(dmabuf, fd_flags);
	if (ret < 0) {
		dma_buf_put(dmabuf);
		/* just return, as put will call release and that will free */
		return ret;
	}

	return ret;

free_pages:
	kfree(helper_buffer->pages);
free_cma:
	cma_release(cma_heap->cma, cma_pages, nr_pages);
free_buf:
	kfree(helper_buffer);
	return ret;
}

static const struct dma_heap_ops cma_heap_ops = {
	.allocate = cma_heap_allocate,
};

static int __add_cma_heap(struct cma *cma, void *data)
{
	struct cma_heap *cma_heap;
	struct dma_heap_export_info exp_info;

	cma_heap = kzalloc(sizeof(*cma_heap), GFP_KERNEL);
	if (!cma_heap)
		return -ENOMEM;
	cma_heap->cma = cma;

	exp_info.name = cma_get_name(cma);
	exp_info.ops = &cma_heap_ops;
	exp_info.priv = cma_heap;

	cma_heap->heap = dma_heap_add(&exp_info);
	if (IS_ERR(cma_heap->heap)) {
		int ret = PTR_ERR(cma_heap->heap);

		kfree(cma_heap);
		return ret;
	}

	return 0;
}

static int add_default_cma_heap(void)
{
	struct cma *default_cma = dev_get_cma_area(NULL);
	int ret = 0;

	if (default_cma)
		ret = __add_cma_heap(default_cma, NULL);

	return ret;
}
module_init(add_default_cma_heap);
MODULE_DESCRIPTION("DMA-BUF CMA Heap");
MODULE_LICENSE("GPL v2");
