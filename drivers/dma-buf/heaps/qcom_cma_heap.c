// SPDX-License-Identifier: GPL-2.0
/*
 * DMABUF CMA heap exporter
 * Copied from drivers/dma-buf/heaps/cma_heap.c as of commit b61614ec318a
 * ("dma-buf: heaps: Add CMA heap to dmabuf heaps")
 *
 * Copyright (C) 2012, 2019 Linaro Ltd.
 * Author: <benjamin.gaignard@linaro.org> for ST-Ericsson.
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include <linux/of.h>

#include "qcom_cma_heap.h"
#include "qcom_sg_ops.h"

struct cma_heap {
	struct cma *cma;
	/* max_align is in units of page_order, similar to CONFIG_CMA_ALIGNMENT */
	u32 max_align;
	bool uncached;
	bool is_nomap;
};

struct dmabuf_cma_info {
	void *cpu_addr;
	dma_addr_t handle;
	struct qcom_sg_buffer buf;
};

static void cma_heap_free(struct qcom_sg_buffer *buffer)
{
	struct cma_heap *cma_heap;
	struct dmabuf_cma_info *info;

	info = container_of(buffer, struct dmabuf_cma_info, buf);
	cma_heap = dma_heap_get_drvdata(buffer->heap);
	if (info->cpu_addr) {
		struct device *dev = dma_heap_get_dev(buffer->heap);

		dma_free_attrs(dev, PAGE_ALIGN(buffer->len), info->cpu_addr,
				info->handle, 0);
	} else {
		struct page *cma_pages = sg_page(buffer->sg_table.sgl);
		unsigned long nr_pages = buffer->len >> PAGE_SHIFT;
		/* release memory */
		cma_release(cma_heap->cma, cma_pages, nr_pages);
	}

	/* free page list */
	sg_free_table(&buffer->sg_table);
	kfree(info);
}

static bool dmabuf_cma_is_nomap(struct device *dev)
{
	struct device_node *mem_region;
	bool val = false;

	mem_region = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!mem_region)
		goto err;

	val = of_property_read_bool(mem_region, "no-map");
err:
	of_node_put(mem_region);
	return val;
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
	struct dmabuf_cma_info *info;
	int ret = -ENOMEM;

	cma_heap = dma_heap_get_drvdata(heap);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	if (align > cma_heap->max_align)
		align = cma_heap->max_align;

	helper_buffer = &info->buf;
	helper_buffer->heap = heap;
	INIT_LIST_HEAD(&helper_buffer->attachments);
	mutex_init(&helper_buffer->lock);
	helper_buffer->len = size;
	helper_buffer->uncached = cma_heap->uncached;
	helper_buffer->free = cma_heap_free;

	if (cma_heap->is_nomap) {
		struct device *dev = dma_heap_get_dev(heap);

		dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
		info->cpu_addr = dma_alloc_wc(dev, size, &info->handle,
					GFP_KERNEL);

		if (!info->cpu_addr) {
			dev_err(dev, "failed to allocate buffer\n");
			goto free_info;
		}
		cma_pages = pfn_to_page(PFN_DOWN(info->handle));
	} else {
		cma_pages = cma_alloc(cma_heap->cma, nr_pages, align, false);
		if (!cma_pages)
			goto free_info;

		if (PageHighMem(cma_pages)) {
			unsigned long nr_clear_pages = nr_pages;
			struct page *page = cma_pages;

			while (nr_clear_pages > 0) {
				void *vaddr = kmap_local_page(page);

				memset(vaddr, 0, PAGE_SIZE);
				kunmap_local(vaddr);
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
	}

	ret = sg_alloc_table(&helper_buffer->sg_table, 1, GFP_KERNEL);
	if (ret)
		goto free_cma;

	sg_set_page(helper_buffer->sg_table.sgl, cma_pages, size, 0);

	helper_buffer->vmperm = mem_buf_vmperm_alloc(&helper_buffer->sg_table);
	if (IS_ERR(helper_buffer->vmperm))
		goto free_sgtable;

	if (helper_buffer->uncached && !cma_heap->is_nomap) {
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
	dmabuf = qcom_dma_buf_export(&exp_info, &qcom_sg_buf_ops);
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
	if (info->cpu_addr)
		dma_free_attrs(dma_heap_get_dev(heap), size, info->cpu_addr,
			info->handle, 0);
	else
		cma_release(cma_heap->cma, cma_pages, nr_pages);
free_info:
	kfree(info);
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

	cma_heap->is_nomap = dmabuf_cma_is_nomap(heap_data->dev);
#ifdef CONFIG_DMA_DECLARE_COHERENT
	if (cma_heap->is_nomap && !dma_heap_get_dev(heap)->dma_mem)
		dma_heap_get_dev(heap)->dma_mem = heap_data->dev->dma_mem;
#endif
	return 0;
}

int qcom_add_cma_heap(struct platform_heap *heap_data)
{
	return __add_cma_heap(heap_data, NULL);
}
