/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _QCOM_SECURE_SYSTEM_HEAP_H
#define _QCOM_SECURE_SYSTEM_HEAP_H

#include <linux/dma-heap.h>
#include <linux/err.h>
#include "qcom_dynamic_page_pool.h"

struct qcom_secure_system_heap {
	struct dynamic_page_pool **pool_list;
	int vmid;
	struct list_head list;
};

#ifdef CONFIG_QCOM_DMABUF_HEAPS_SYSTEM_SECURE
void qcom_secure_system_heap_create(const char *name, const char *secure_system_alias,
				    int vmid);
#else
static void qcom_secure_system_heap_create(const char *name, const char *secure_system_alias,
					   int vmid)
{

}
#endif

#endif /* _QCOM_SECURE_SYSTEM_HEAP_H */
