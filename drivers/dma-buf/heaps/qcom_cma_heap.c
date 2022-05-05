// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF CMA heap exporter
 * Copied from drivers/dma-buf/heaps/cma_heap.c as of commit b61614ec318a
 * ("dma-buf: heaps: Add CMA heap to dmabuf heaps")
 *
 * Copyright (C) 2012, 2019 Linaro Ltd.
 * Author: <benjamin.gaignard@linaro.org> for ST-Ericsson.
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
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
#include <linux/list.h>

#include "qcom_cma_heap.h"
#include "qcom_sg_ops.h"

struct cma_heap {
	struct cma *cma;
	/* max_align is in units of page_order, similar to CONFIG_CMA_ALIGNMENT */
	u32 max_align;
	bool uncached;
};

static void cma_heap_free(struct qcom_sg_buffer *buffer)
{
	struct cma_heap *cma_heap;
	unsigned long nr_pages = buffer->len >> PAGE_SHIFT;
	struct page *cma_pages = sg_page(buffer->sg_table.sgl);

	cma_heap = dma_heap_get_drvdata(buffer->heap);

	/* free page list */
	sg_free_table(&buffer->sg_table);
	/* release memory */
	cma_release(cma_heap->cma, cma_pages, nr_pages);
	kfree(buffer);
}

/* dmabuf heap CMA operations functions */
struct dma_buf *cma_heap_allocate(struct dma_heap *heap,
				  unsigned long len,
				  unsigned long fd_flags,
				  unsigned long heap_flags)
{
	struct cma_heap *cma_heap;
	struct qcom_sg_buffer *helper_buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct page *cma_pages;
	size_t size = PAGE_ALIGN(len);
	unsigned long nr_pages = size >> PAGE_SHIFT;
	unsigned long align = get_order(size);
	struct dma_buf *dmabuf;
	int ret = -ENOMEM;

	cma_heap = dma_heap_get_drvdata(heap);

	if (align > cma_heap->max_align)
		align = cma_heap->max_align;

	helper_buffer = kzalloc(sizeof(*helper_buffer), GFP_KERNEL);
	if (!helper_buffer)
		return ERR_PTR(-ENOMEM);

	helper_buffer->heap = heap;
	INIT_LIST_HEAD(&helper_buffer->attachments);
	mutex_init(&helper_buffer->lock);
	helper_buffer->len = size;
	helper_buffer->uncached = cma_heap->uncached;
	helper_buffer->free = cma_heap_free;

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
			 * has been killed by SIGKILL
			 */
			if (fatal_signal_pending(current))
				goto free_cma;

			page++;
			nr_clear_pages--;
		}
	} else {
		memset(page_address(cma_pages), 0, size);
	}

	ret = sg_alloc_table(&helper_buffer->sg_table, 1, GFP_KERNEL);
	if (ret)
		goto free_cma;

	sg_set_page(helper_buffer->sg_table.sgl, cma_pages, size, 0);

	helper_buffer->vmperm = mem_buf_vmperm_alloc(&helper_buffer->sg_table);
	if (IS_ERR(helper_buffer->vmperm))
		goto free_sgtable;

	if (helper_buffer->uncached) {
		dma_map_sgtable(dma_heap_get_dev(heap), &helper_buffer->sg_table,
				DMA_BIDIRECTIONAL, 0);
		dma_unmap_sgtable(dma_heap_get_dev(heap), &helper_buffer->sg_table,
				  DMA_BIDIRECTIONAL, 0);
	}

	/* create the dmabuf */
	exp_info.exp_name = dma_heap_get_name(heap);
	exp_info.size = helper_buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = helper_buffer;
	dmabuf = mem_buf_dma_buf_export(&exp_info, &qcom_sg_buf_ops);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto vmperm_release;
	}

	return dmabuf;

vmperm_release:
	mem_buf_vmperm_release(helper_buffer->vmperm);
free_sgtable:
	sg_free_table(&helper_buffer->sg_table);
free_cma:
	cma_release(cma_heap->cma, cma_pages, nr_pages);
free_buf:
	kfree(helper_buffer);
	return ERR_PTR(ret);
}

static const struct dma_heap_ops cma_heap_ops = {
	.allocate = cma_heap_allocate,
};

static int __add_cma_heap(struct platform_heap *heap_data, void *data)
{
	struct cma_heap *cma_heap;
	struct dma_heap_export_info exp_info;
	struct dma_heap *heap;

	if (!heap_data->dev->cma_area) {
		pr_err("%s: CMA area for device uninitialized!\n", __func__);
		return -EINVAL;
	}

	cma_heap = kzalloc(sizeof(*cma_heap), GFP_KERNEL);
	if (!cma_heap)
		return -ENOMEM;

	cma_heap->cma = heap_data->dev->cma_area;
	cma_heap->max_align = CONFIG_CMA_ALIGNMENT;
	if (heap_data->max_align)
		cma_heap->max_align = heap_data->max_align;
	cma_heap->uncached = heap_data->is_uncached;

	exp_info.name = heap_data->name;
	exp_info.ops = &cma_heap_ops;
	exp_info.priv = cma_heap;

	heap = dma_heap_add(&exp_info);
	if (IS_ERR(heap)) {
		int ret = PTR_ERR(heap);

		kfree(cma_heap);
		return ret;
	}

	if (cma_heap->uncached)
		dma_coerce_mask_and_coherent(dma_heap_get_dev(heap),
					     DMA_BIT_MASK(64));

	return 0;
}

int qcom_add_cma_heap(struct platform_heap *heap_data)
{
	return __add_cma_heap(heap_data, NULL);
}
