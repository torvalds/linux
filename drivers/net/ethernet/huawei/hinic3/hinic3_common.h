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

enum hinic3_wait_return {
	HINIC3_WAIT_PROCESS_CPL     = 0,
	HINIC3_WAIT_PROCESS_WAITING = 1,
};

struct hinic3_sge {
	__le32 hi_addr;
	__le32 lo_addr;
	__le32 len;
	__le32 rsvd;
};

static inline void hinic3_set_sge(struct hinic3_sge *sge, dma_addr_t addr,
				  __le32 len)
{
	sge->hi_addr = cpu_to_le32(upper_32_bits(addr));
	sge->lo_addr = cpu_to_le32(lower_32_bits(addr));
	sge->len = len;
	sge->rsvd = 0;
}

int hinic3_dma_zalloc_coherent_align(struct device *dev, u32 size, u32 align,
				     gfp_t flag,
				     struct hinic3_dma_addr_align *mem_align);
void hinic3_dma_free_coherent_align(struct device *dev,
				    struct hinic3_dma_addr_align *mem_align);

typedef enum hinic3_wait_return (*wait_cpl_handler)(void *priv_data);
int hinic3_wait_for_timeout(void *priv_data, wait_cpl_handler handler,
			    u32 wait_total_ms, u32 wait_once_us);

void hinic3_cmdq_buf_swab32(void *data, int len);

#endif
