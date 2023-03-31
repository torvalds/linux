/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_UBWCP_HEAP_H
#define _QCOM_UBWCP_HEAP_H

#ifdef CONFIG_QCOM_DMABUF_HEAPS_UBWCP
int qcom_ubwcp_heap_create(char *name, bool movable);
#else
int qcom_ubwcp_heap_create(char *name, bool movable)
{
	return 0;
}
#endif

#endif /* _QCOM_UBWCP_HEAP_H */
