// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>

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

int hinic3_wait_for_timeout(void *priv_data, wait_cpl_handler handler,
			    u32 wait_total_ms, u32 wait_once_us)
{
	enum hinic3_wait_return ret;
	int err;

	err = read_poll_timeout(handler, ret, ret == HINIC3_WAIT_PROCESS_CPL,
				wait_once_us, wait_total_ms * USEC_PER_MSEC,
				false, priv_data);

	return err;
}

/* Data provided to/by cmdq is arranged in structs with little endian fields but
 * every dword (32bits) should be swapped since HW swaps it again when it
 * copies it from/to host memory.
 */
void hinic3_cmdq_buf_swab32(void *data, int len)
{
	swab32_array(data, len / sizeof(u32));
}
