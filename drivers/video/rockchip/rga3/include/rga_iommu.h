/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RGA_MMU_INFO_H__
#define __RGA_MMU_INFO_H__

#include "rga_drv.h"

/* RGA_IOMMU register offsets */
#define RGA_IOMMU_BASE				0xf00
#define RGA_IOMMU_DTE_ADDR			(RGA_IOMMU_BASE + 0x00) /* Directory table address */
#define RGA_IOMMU_STATUS			(RGA_IOMMU_BASE + 0x04)
#define RGA_IOMMU_COMMAND			(RGA_IOMMU_BASE + 0x08)
#define RGA_IOMMU_PAGE_FAULT_ADDR		(RGA_IOMMU_BASE + 0x0C) /* IOVA of last page fault */
#define RGA_IOMMU_ZAP_ONE_LINE			(RGA_IOMMU_BASE + 0x10) /* Shootdown one IOTLB entry */
#define RGA_IOMMU_INT_RAWSTAT			(RGA_IOMMU_BASE + 0x14) /* IRQ status ignoring mask */
#define RGA_IOMMU_INT_CLEAR			(RGA_IOMMU_BASE + 0x18) /* Acknowledge and re-arm irq */
#define RGA_IOMMU_INT_MASK			(RGA_IOMMU_BASE + 0x1C) /* IRQ enable */
#define RGA_IOMMU_INT_STATUS			(RGA_IOMMU_BASE + 0x20) /* IRQ status after masking */
#define RGA_IOMMU_AUTO_GATING			(RGA_IOMMU_BASE + 0x24)

/* RGA_IOMMU_STATUS fields */
#define RGA_IOMMU_STATUS_PAGING_ENABLED		BIT(0)
#define RGA_IOMMU_STATUS_PAGE_FAULT_ACTIVE	BIT(1)
#define RGA_IOMMU_STATUS_STALL_ACTIVE		BIT(2)
#define RGA_IOMMU_STATUS_IDLE			BIT(3)
#define RGA_IOMMU_STATUS_REPLAY_BUFFER_EMPTY	BIT(4)
#define RGA_IOMMU_STATUS_PAGE_FAULT_IS_WRITE	BIT(5)
#define RGA_IOMMU_STATUS_STALL_NOT_ACTIVE	BIT(31)

/* RGA_IOMMU_COMMAND command values */
#define RGA_IOMMU_CMD_ENABLE_PAGING		0 /* Enable memory translation */
#define RGA_IOMMU_CMD_DISABLE_PAGING		1 /* Disable memory translation */
#define RGA_IOMMU_CMD_ENABLE_STALL		2 /* Stall paging to allow other cmds */
#define RGA_IOMMU_CMD_DISABLE_STALL		3 /* Stop stall re-enables paging */
#define RGA_IOMMU_CMD_ZAP_CACHE			4 /* Shoot down entire IOTLB */
#define RGA_IOMMU_CMD_PAGE_FAULT_DONE		5 /* Clear page fault */
#define RGA_IOMMU_CMD_FORCE_RESET		6 /* Reset all registers */

/* RGA_IOMMU_INT_* register fields */
#define RGA_IOMMU_IRQ_PAGE_FAULT		0x01 /* page fault */
#define RGA_IOMMU_IRQ_BUS_ERROR			0x02 /* bus read error */
#define RGA_IOMMU_IRQ_MASK			(RGA_IOMMU_IRQ_PAGE_FAULT | RGA_IOMMU_IRQ_BUS_ERROR)

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

