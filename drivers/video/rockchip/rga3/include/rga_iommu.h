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

struct rga_mmu_base {
	unsigned int *buf_virtual;
	struct page **pages;
	u8 buf_order;
	u8 pages_order;

	int32_t front;
	int32_t back;
	int32_t size;
	int32_t curr;
};

int rga_user_memory_check(struct page **pages, u32 w, u32 h, u32 format, int flag);
int rga_set_mmu_base(struct rga_job *job, struct rga2_req *req);
unsigned int *rga_mmu_buf_get(struct rga_mmu_base *mmu_base, uint32_t size);

struct rga_mmu_base *rga_mmu_base_init(size_t size);
void rga_mmu_base_free(struct rga_mmu_base **mmu_base);

int rga_iommu_detach(struct rga_iommu_info *info);
int rga_iommu_attach(struct rga_iommu_info *info);
struct rga_iommu_info *rga_iommu_probe(struct device *dev);
int rga_iommu_remove(struct rga_iommu_info *info);

int rga_iommu_bind(void);
void rga_iommu_unbind(void);

#endif

