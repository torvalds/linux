/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_CARVEOUT_HEAP_H
#define _QCOM_CARVEOUT_HEAP_H

#include "qcom_dt_parser.h"

#ifdef CONFIG_QCOM_DMABUF_HEAPS_CARVEOUT
int qcom_secure_carveout_heap_create(struct platform_heap *heap_data);
int qcom_carveout_heap_create(struct platform_heap *heap_data);
#else
static inline int qcom_secure_carveout_heap_create(struct platform_heap *heap_data)
{
	return -EINVAL;
}
static inline int qcom_carveout_heap_create(struct platform_heap *heap_data)
{
	return -EINVAL;
}
#endif

#endif /* _QCOM_CARVEOUT_HEAP_H */
