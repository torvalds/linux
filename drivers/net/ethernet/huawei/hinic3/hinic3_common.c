// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/delay.h>
#include <linux/dma-mapping.h>

#include "hinic3_common.h"

int hinic3_dma_zalloc_coherent_align(struct device *dev, u32 size, u32 align,
				     gfp_t flag,
				     struct hinic3_dma_addr_align *mem_align)
{
	dma_addr_t paddr, align_paddr;
	void *vaddr, *align_vaddr;
	u32 real_size = size;

	vaddr = dma_alloc_coherent(dev, real_size, &paddr, flag);
	if (!vaddr)
		return -ENOMEM;

	align_paddr = ALIGN(paddr, align);
	if (align_paddr == paddr) {
		align_vaddr = vaddr;
		goto out;
	}

	dma_free_coherent(dev, real_size, vaddr, paddr);

	/* realloc memory for align */
	real_size = size + align;
	vaddr = dma_alloc_coherent(dev, real_size, &paddr, flag);
	if (!vaddr)
		return -ENOMEM;

	align_paddr = ALIGN(paddr, align);
	align_vaddr = vaddr + (align_paddr - paddr);

out:
	mem_align->real_size = real_size;
	mem_align->ori_vaddr = vaddr;
	mem_align->ori_paddr = paddr;
	mem_align->align_vaddr = align_vaddr;
	mem_align->align_paddr = align_paddr;

	return 0;
}

void hinic3_dma_free_coherent_align(struct device *dev,
				    struct hinic3_dma_addr_align *mem_align)
{
	dma_free_coherent(dev, mem_align->real_size,
			  mem_align->ori_vaddr, mem_align->ori_paddr);
}
