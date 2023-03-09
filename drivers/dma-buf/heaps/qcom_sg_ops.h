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
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_SG_OPS_H
#define _QCOM_SG_OPS_H

#include <linux/scatterlist.h>
#include <linux/dma-heap.h>
#include <linux/device.h>
#include "deferred-free-helper.h"
#include "qcom_dma_heap_priv.h"

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
	struct deferred_freelist_item deferred_free;
	void (*free)(struct qcom_sg_buffer *buffer);
};

struct dma_heap_attachment {
	struct device *dev;
	struct sg_table *table;
	struct list_head list;
	bool mapped;
};

int qcom_sg_attach(struct dma_buf *dmabuf,
		   struct dma_buf_attachment *attachment);

void qcom_sg_detach(struct dma_buf *dmabuf,
		    struct dma_buf_attachment *attachment);

struct sg_table *qcom_sg_map_dma_buf(struct dma_buf_attachment *attachment,
				     enum dma_data_direction direction);

void qcom_sg_unmap_dma_buf(struct dma_buf_attachment *attachment,
			   struct sg_table *table,
			   enum dma_data_direction direction);

int qcom_sg_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
				     enum dma_data_direction direction);

int qcom_sg_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
				   enum dma_data_direction direction);

int qcom_sg_dma_buf_begin_cpu_access_partial(struct dma_buf *dmabuf,
					     enum dma_data_direction dir,
					     unsigned int offset,
					     unsigned int len);

int qcom_sg_dma_buf_end_cpu_access_partial(struct dma_buf *dmabuf,
					   enum dma_data_direction direction,
					   unsigned int offset,
					   unsigned int len);

int qcom_sg_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma);

void *qcom_sg_do_vmap(struct qcom_sg_buffer *buffer);

int qcom_sg_vmap(struct dma_buf *dmabuf, struct iosys_map *map);

void qcom_sg_vunmap(struct dma_buf *dmabuf, struct iosys_map *map);

void qcom_sg_release(struct dma_buf *dmabuf);

struct mem_buf_vmperm *qcom_sg_lookup_vmperm(struct dma_buf *dmabuf);

extern struct mem_buf_dma_buf_ops qcom_sg_buf_ops;

#endif /* _QCOM_SG_OPS_H */
