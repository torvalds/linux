/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_SYSTEM_MOVABLE_HEAP_H
#define _QCOM_SYSTEM_MOVABLE_HEAP_H

#include "qcom_dynamic_page_pool.h"

#ifdef CONFIG_QCOM_DMABUF_HEAPS_SYSTEM_MOVABLE
void qcom_sys_movable_heap_create(void);

struct page *qcom_movable_heap_alloc_pages(struct dynamic_page_pool *pool);
#else
static inline void qcom_sys_movable_heap_create(void)
{

}

static inline struct page *qcom_movable_heap_alloc_pages(struct dynamic_page_pool *pool)
{
	return  ERR_PTR(-EOPNOTSUPP);
}
#endif

#endif /* _QCOM_SYSTEM_MOVABLE_HEAP_H */


