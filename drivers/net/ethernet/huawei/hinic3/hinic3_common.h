/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_COMMON_H_
#define _HINIC3_COMMON_H_

#include <linux/device.h>

#define HINIC3_MIN_PAGE_SIZE  0x1000

struct hinic3_dma_addr_align {
	u32        real_size;

	void       *ori_vaddr;
	dma_addr_t ori_paddr;

	void       *align_vaddr;
	dma_addr_t align_paddr;
};

int hinic3_dma_zalloc_coherent_align(struct device *dev, u32 size, u32 align,
				     gfp_t flag,
				     struct hinic3_dma_addr_align *mem_align);
void hinic3_dma_free_coherent_align(struct device *dev,
				    struct hinic3_dma_addr_align *mem_align);

#endif
