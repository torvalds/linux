/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _QCOM_CMA_HEAP_H
#define _QCOM_CMA_HEAP_H

#include "qcom_dt_parser.h"

#ifdef CONFIG_QCOM_DMABUF_HEAPS_CMA
int qcom_add_cma_heap(struct platform_heap *heap_data);
#else
static int qcom_add_cma_heap(struct platform_heap *heap_data)
{
	return 1;
}
#endif

#endif /* _QCOM_CMA_HEAP_H */
