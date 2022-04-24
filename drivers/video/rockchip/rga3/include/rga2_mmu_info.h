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

int rga2_user_memory_check(struct page **pages, u32 w, u32 h, u32 format, int flag);

int rga2_set_mmu_base(struct rga_job *job, struct rga2_req *req);

unsigned int *rga2_mmu_buf_get(uint32_t size);

int rga2_mmu_base_init(void);
void rga2_mmu_base_free(void);

#endif

