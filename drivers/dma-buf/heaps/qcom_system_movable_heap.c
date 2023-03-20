// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * DMABUF System heap exporter
 */

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__
#include <linux/slab.h>
#include "qcom_system_heap.h"
#include "qcom_system_movable_heap.h"

static struct dma_heap *sys_heap;

struct page *qcom_movable_heap_alloc_pages(struct dynamic_page_pool *pool)
{
	/*
	 * This function allocates memory from the Normal zone if the number of free pages in the
	 * normal zone is above the min watermark, or if the allocation request is for a page with
	 * order zero.
	 * Else, the function will allocate memory from the movable zone.
	 */
	int mark;
	enum zone_type highest_zoneidx;
	struct zone *zone;
	struct page *page = NULL;

	highest_zoneidx = dynamic_pool_gfp_zone(pool->gfp_mask);
	zone = &NODE_DATA(numa_node_id())->node_zones[highest_zoneidx];
	mark = min_wmark_pages(zone);

	if (dynamic_pool_zone_watermark_ok_safe(zone, pool->order, mark, highest_zoneidx)) {
		page = alloc_pages(pool->gfp_mask, pool->order);
	} else {
		page = alloc_pages((pool->gfp_mask | __GFP_MOVABLE), pool->order);
		if (page) {
			if (is_zone_movable_page(page)) {
				/*
				 * Pin down the page so that it cannot be migrated while it is
				 * still being utilized by the use-case.
				 */
				get_page(page);
			} else {
				/*
				 * Due to the Fallback scheme, it is posisble to get a page from
				 * the Normal zone after appending __GFP_MOVABLE to the allocation
				 * flags.
				 * Return all pages that we get from the Normal zone while using
				 * __GFP_MOVABLE to avoid allocating a page from the CMA region.
				 */
				__free_pages(page, pool->order);
				return NULL;
			}
		}
	}
	return page;
}


static struct dma_buf *movable_heap_allocate(struct dma_heap *heap,
						unsigned long len,
						unsigned long fd_flags,
						unsigned long heap_flags)
{
	struct qcom_sg_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	int ret = -ENOMEM;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	ret = system_qcom_sg_buffer_alloc(sys_heap, buffer, len, true);
	if (ret)
		goto free_buffer;

	buffer->vmperm = mem_buf_vmperm_alloc(&buffer->sg_table);

	if (IS_ERR(buffer->vmperm)) {
		ret = PTR_ERR(buffer->vmperm);
		goto free_sys_heap_mem;
	}

	/* Create the dmabuf */
	exp_info.exp_name = dma_heap_get_name(heap);
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = mem_buf_dma_buf_export(&exp_info, &qcom_sg_buf_ops);

	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto vmperm_release;
	}
	return dmabuf;

vmperm_release:
	mem_buf_vmperm_release(buffer->vmperm);
free_sys_heap_mem:
	qcom_system_heap_free(buffer);
	return ERR_PTR(ret);
free_buffer:
	kfree(buffer);

	return ERR_PTR(ret);
}

static const struct dma_heap_ops movable_heap_ops = {
	.allocate = movable_heap_allocate,
};

void qcom_sys_movable_heap_create(void)
{
	struct dma_heap_export_info exp_info;
	struct dma_heap *heap;
	int ret;
	const char *name = "qcom,system-movable";

	sys_heap = dma_heap_find("qcom,system");
	if (!sys_heap) {
		pr_err("Unable to find qcom,system heap.\n");
		return;
	}

	exp_info.name = name;
	exp_info.ops = &movable_heap_ops;
	exp_info.priv = NULL;

	heap = dma_heap_add(&exp_info);
	if (IS_ERR(heap)) {
		ret = PTR_ERR(heap);
		pr_err("Failed to create '%s', error is %d\n", name, ret);
		return;
	}

	pr_info("DMA-BUF Heap: Created '%s'\n", name);
}
