/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2011 Google, Inc.
 * Copyright (C) 2019, 2020 Linaro Ltd.
 *
 * Portions based off of Andrew Davis' SRAM heap:
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 *
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _QCOM_SG_OPS_H
#define _QCOM_SG_OPS_H

#include <linux/scatterlist.h>
#include <linux/dma-heap.h>
#include <linux/device.h>
#include <linux/mem-buf-exporter.h>

struct qcom_sg_buffer {
	struct dma_heap *heap;
	struct list_head attachments;
	struct mutex lock;
	unsigned long len;
	struct sg_table sg_table;
	int vmap_cnt;
	void *vaddr;
	bool uncached;
	struct mem_buf_vmperm *vmperm;
	void (*free)(struct qcom_sg_buffer *buffer);
};

struct dma_heap_attachment {
	struct device *dev;
	struct sg_table *table;
	struct list_head list;
	bool mapped;
};

extern struct mem_buf_dma_buf_ops qcom_sg_buf_ops;

#endif /* _QCOM_SG_OPS_H */
