/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RGA_MMU_INFO_H__
#define __RGA_MMU_INFO_H__

#include "rga_drv.h"

/*
 * The maximum input is 8192*8192, the maximum output is 4096*4096
 * The size of physical pages requested is:
 * (( maximum_input_value *
 *         maximum_input_value * format_bpp ) / 4K_page_size) + 1
 */
#define RGA2_PHY_PAGE_SIZE	 (((8192 * 8192 * 4) / 4096) + 1)

enum {
	MMU_MAP_CLEAN		= 1 << 0,
	MMU_MAP_INVALID		= 1 << 1,
	MMU_MAP_MASK		= 0x03,
	MMU_UNMAP_CLEAN		= 1 << 2,
	MMU_UNMAP_INVALID	= 1 << 3,
	MMU_UNMAP_MASK		= 0x0c,
};

struct rga2_mmu_info_t {
	int32_t front;
	int32_t back;
	int32_t size;
	int32_t curr;
	unsigned int *buf;
	unsigned int *buf_virtual;

	struct page **pages;

	u8 buf_order;
	u8 pages_order;
};

void rga2_dma_flush_cache_for_virtual_address(struct rga2_mmu_other_t *reg,
		struct rga_scheduler_t *scheduler);

int rga2_set_mmu_reg_info(struct rga2_mmu_other_t *reg,
		struct rga2_req *req, struct rga_job *job);

#endif

