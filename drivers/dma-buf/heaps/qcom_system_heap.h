/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_SYSTEM_HEAP_H
#define _QCOM_SYSTEM_HEAP_H

#include <linux/dma-heap.h>
#include <linux/err.h>
#include "qcom_sg_ops.h"
#include "qcom_dynamic_page_pool.h"
#include "qcom_sg_ops.h"

struct qcom_system_heap {
	int uncached;
	struct dynamic_page_pool **pool_list;
};

#ifdef CONFIG_QCOM_DMABUF_HEAPS_SYSTEM
void qcom_system_heap_create(const char *name, const char *system_alias, bool uncached);
void qcom_system_heap_free(struct qcom_sg_buffer *buffer);
struct page *qcom_sys_heap_alloc_largest_available(struct dynamic_page_pool **pools,
						   unsigned long size,
						   unsigned int max_order,
						   bool movable);
int system_qcom_sg_buffer_alloc(struct dma_heap *heap,
				struct qcom_sg_buffer *buffer,
				unsigned long len,
				bool movable);
enum zone_type dynamic_pool_gfp_zone(gfp_t flags);

bool dynamic_pool_zone_watermark_ok_safe(struct zone *z, unsigned int order,
					unsigned long mark, int highest_zoneidx);
#else
static inline void qcom_system_heap_create(const char *name, const char *system_alias,
					   bool uncached)
{

}
static inline void qcom_system_heap_free(struct qcom_sg_buffer *buffer)
{

}
static inline struct page *qcom_sys_heap_alloc_largest_available(struct dynamic_page_pool **pools,
								 unsigned long size,
								 unsigned int max_order,
								 bool movable)
{
	return ERR_PTR(-EOPNOTSUPP);
}
static inline int system_qcom_sg_buffer_alloc(struct dma_heap *heap,
					      struct qcom_sg_buffer *buffer,
					      unsigned long len,
					      bool movable)
{
	return -EOPNOTSUPP;
}
enum zone_type dynamic_pool_gfp_zone(gfp_t flags)
{
	return -EOPNOTSUPP;
}

bool dynamic_pool_zone_watermark_ok_safe(struct zone *z, unsigned int order,
					unsigned long mark, int highest_zoneidx)
{
	return -EOPNOTSUPP;
}
#endif

#endif /* _QCOM_SYSTEM_HEAP_H */
