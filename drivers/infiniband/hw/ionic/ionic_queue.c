// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#include <linux/dma-mapping.h>

#include "ionic_queue.h"

int ionic_queue_init(struct ionic_queue *q, struct device *dma_dev,
		     int depth, size_t stride)
{
	if (depth < 0 || depth > 0xffff)
		return -EINVAL;

	if (stride == 0 || stride > 0x10000)
		return -EINVAL;

	if (depth == 0)
		depth = 1;

	q->depth_log2 = order_base_2(depth + 1);
	q->stride_log2 = order_base_2(stride);

	if (q->depth_log2 + q->stride_log2 < PAGE_SHIFT)
		q->depth_log2 = PAGE_SHIFT - q->stride_log2;

	if (q->depth_log2 > 16 || q->stride_log2 > 16)
		return -EINVAL;

	q->size = BIT_ULL(q->depth_log2 + q->stride_log2);
	q->mask = BIT(q->depth_log2) - 1;

	q->ptr = dma_alloc_coherent(dma_dev, q->size, &q->dma, GFP_KERNEL);
	if (!q->ptr)
		return -ENOMEM;

	/* it will always be page aligned, but just to be sure... */
	if (!PAGE_ALIGNED(q->ptr)) {
		dma_free_coherent(dma_dev, q->size, q->ptr, q->dma);
		return -ENOMEM;
	}

	q->prod = 0;
	q->cons = 0;
	q->dbell = 0;

	return 0;
}

void ionic_queue_destroy(struct ionic_queue *q, struct device *dma_dev)
{
	dma_free_coherent(dma_dev, q->size, q->ptr, q->dma);
}
