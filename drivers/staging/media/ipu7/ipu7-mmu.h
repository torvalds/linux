/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 - 2025 Intel Corporation
 */

#ifndef IPU7_MMU_H
#define IPU7_MMU_H

#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>

struct device;
struct page;
struct ipu7_hw_variants;
struct ipu7_mmu;
struct ipu7_mmu_info;

#define ISYS_MMID 0x1
#define PSYS_MMID 0x0

/* IPU7 for LNL */
/* IS MMU Cmd RD */
#define IPU7_IS_MMU_FW_RD_OFFSET		0x274000
#define IPU7_IS_MMU_FW_RD_STREAM_NUM		3
#define IPU7_IS_MMU_FW_RD_L1_BLOCKNR_REG	0x54
#define IPU7_IS_MMU_FW_RD_L2_BLOCKNR_REG	0x60

/* IS MMU Cmd WR */
#define IPU7_IS_MMU_FW_WR_OFFSET		0x275000
#define IPU7_IS_MMU_FW_WR_STREAM_NUM		3
#define IPU7_IS_MMU_FW_WR_L1_BLOCKNR_REG	0x54
#define IPU7_IS_MMU_FW_WR_L2_BLOCKNR_REG	0x60

/* IS MMU Data WR Snoop */
#define IPU7_IS_MMU_M0_OFFSET			0x276000
#define IPU7_IS_MMU_M0_STREAM_NUM		8
#define IPU7_IS_MMU_M0_L1_BLOCKNR_REG		0x54
#define IPU7_IS_MMU_M0_L2_BLOCKNR_REG		0x74

/* IS MMU Data WR ISOC */
#define IPU7_IS_MMU_M1_OFFSET			0x277000
#define IPU7_IS_MMU_M1_STREAM_NUM		16
#define IPU7_IS_MMU_M1_L1_BLOCKNR_REG		0x54
#define IPU7_IS_MMU_M1_L2_BLOCKNR_REG		0x94

/* PS MMU FW RD */
#define IPU7_PS_MMU_FW_RD_OFFSET		0x148000
#define IPU7_PS_MMU_FW_RD_STREAM_NUM		20
#define IPU7_PS_MMU_FW_RD_L1_BLOCKNR_REG	0x54
#define IPU7_PS_MMU_FW_RD_L2_BLOCKNR_REG	0xa4

/* PS MMU FW WR */
#define IPU7_PS_MMU_FW_WR_OFFSET		0x149000
#define IPU7_PS_MMU_FW_WR_STREAM_NUM		10
#define IPU7_PS_MMU_FW_WR_L1_BLOCKNR_REG	0x54
#define IPU7_PS_MMU_FW_WR_L2_BLOCKNR_REG	0x7c

/* PS MMU FW Data RD VC0 */
#define IPU7_PS_MMU_SRT_RD_OFFSET		0x14a000
#define IPU7_PS_MMU_SRT_RD_STREAM_NUM		40
#define IPU7_PS_MMU_SRT_RD_L1_BLOCKNR_REG	0x54
#define IPU7_PS_MMU_SRT_RD_L2_BLOCKNR_REG	0xf4

/* PS MMU FW Data WR VC0 */
#define IPU7_PS_MMU_SRT_WR_OFFSET		0x14b000
#define IPU7_PS_MMU_SRT_WR_STREAM_NUM		40
#define IPU7_PS_MMU_SRT_WR_L1_BLOCKNR_REG	0x54
#define IPU7_PS_MMU_SRT_WR_L2_BLOCKNR_REG	0xf4

/* IS UAO UC RD */
#define IPU7_IS_UAO_UC_RD_OFFSET		0x27c000
#define IPU7_IS_UAO_UC_RD_PLANENUM		4

/* IS UAO UC WR */
#define IPU7_IS_UAO_UC_WR_OFFSET		0x27d000
#define IPU7_IS_UAO_UC_WR_PLANENUM		4

/* IS UAO M0 WR */
#define IPU7_IS_UAO_M0_WR_OFFSET		0x27e000
#define IPU7_IS_UAO_M0_WR_PLANENUM		8

/* IS UAO M1 WR */
#define IPU7_IS_UAO_M1_WR_OFFSET		0x27f000
#define IPU7_IS_UAO_M1_WR_PLANENUM		16

/* PS UAO FW RD */
#define IPU7_PS_UAO_FW_RD_OFFSET		0x156000
#define IPU7_PS_UAO_FW_RD_PLANENUM		20

/* PS UAO FW WR */
#define IPU7_PS_UAO_FW_WR_OFFSET		0x157000
#define IPU7_PS_UAO_FW_WR_PLANENUM		16

/* PS UAO SRT RD */
#define IPU7_PS_UAO_SRT_RD_OFFSET		0x154000
#define IPU7_PS_UAO_SRT_RD_PLANENUM		40

/* PS UAO SRT WR */
#define IPU7_PS_UAO_SRT_WR_OFFSET		0x155000
#define IPU7_PS_UAO_SRT_WR_PLANENUM		40

#define IPU7_IS_ZLX_UC_RD_OFFSET		0x278000
#define IPU7_IS_ZLX_UC_WR_OFFSET		0x279000
#define IPU7_IS_ZLX_M0_OFFSET			0x27a000
#define IPU7_IS_ZLX_M1_OFFSET			0x27b000
#define IPU7_IS_ZLX_UC_RD_NUM			4
#define IPU7_IS_ZLX_UC_WR_NUM			4
#define IPU7_IS_ZLX_M0_NUM			8
#define IPU7_IS_ZLX_M1_NUM			16

#define IPU7_PS_ZLX_DATA_RD_OFFSET		0x14e000
#define IPU7_PS_ZLX_DATA_WR_OFFSET		0x14f000
#define IPU7_PS_ZLX_FW_RD_OFFSET		0x150000
#define IPU7_PS_ZLX_FW_WR_OFFSET		0x151000
#define IPU7_PS_ZLX_DATA_RD_NUM			32
#define IPU7_PS_ZLX_DATA_WR_NUM			32
#define IPU7_PS_ZLX_FW_RD_NUM			16
#define IPU7_PS_ZLX_FW_WR_NUM			10

/* IPU7P5 for PTL */
/* IS MMU Cmd RD */
#define IPU7P5_IS_MMU_FW_RD_OFFSET		0x274000
#define IPU7P5_IS_MMU_FW_RD_STREAM_NUM		3
#define IPU7P5_IS_MMU_FW_RD_L1_BLOCKNR_REG	0x54
#define IPU7P5_IS_MMU_FW_RD_L2_BLOCKNR_REG	0x60

/* IS MMU Cmd WR */
#define IPU7P5_IS_MMU_FW_WR_OFFSET		0x275000
#define IPU7P5_IS_MMU_FW_WR_STREAM_NUM		3
#define IPU7P5_IS_MMU_FW_WR_L1_BLOCKNR_REG	0x54
#define IPU7P5_IS_MMU_FW_WR_L2_BLOCKNR_REG	0x60

/* IS MMU Data WR Snoop */
#define IPU7P5_IS_MMU_M0_OFFSET			0x276000
#define IPU7P5_IS_MMU_M0_STREAM_NUM		16
#define IPU7P5_IS_MMU_M0_L1_BLOCKNR_REG		0x54
#define IPU7P5_IS_MMU_M0_L2_BLOCKNR_REG		0x94

/* IS MMU Data WR ISOC */
#define IPU7P5_IS_MMU_M1_OFFSET			0x277000
#define IPU7P5_IS_MMU_M1_STREAM_NUM		16
#define IPU7P5_IS_MMU_M1_L1_BLOCKNR_REG		0x54
#define IPU7P5_IS_MMU_M1_L2_BLOCKNR_REG		0x94

/* PS MMU FW RD */
#define IPU7P5_PS_MMU_FW_RD_OFFSET		0x148000
#define IPU7P5_PS_MMU_FW_RD_STREAM_NUM		16
#define IPU7P5_PS_MMU_FW_RD_L1_BLOCKNR_REG	0x54
#define IPU7P5_PS_MMU_FW_RD_L2_BLOCKNR_REG	0x94

/* PS MMU FW WR */
#define IPU7P5_PS_MMU_FW_WR_OFFSET		0x149000
#define IPU7P5_PS_MMU_FW_WR_STREAM_NUM		10
#define IPU7P5_PS_MMU_FW_WR_L1_BLOCKNR_REG	0x54
#define IPU7P5_PS_MMU_FW_WR_L2_BLOCKNR_REG	0x7c

/* PS MMU FW Data RD VC0 */
#define IPU7P5_PS_MMU_SRT_RD_OFFSET		0x14a000
#define IPU7P5_PS_MMU_SRT_RD_STREAM_NUM		22
#define IPU7P5_PS_MMU_SRT_RD_L1_BLOCKNR_REG	0x54
#define IPU7P5_PS_MMU_SRT_RD_L2_BLOCKNR_REG	0xac

/* PS MMU FW Data WR VC0 */
#define IPU7P5_PS_MMU_SRT_WR_OFFSET		0x14b000
#define IPU7P5_PS_MMU_SRT_WR_STREAM_NUM		32
#define IPU7P5_PS_MMU_SRT_WR_L1_BLOCKNR_REG	0x54
#define IPU7P5_PS_MMU_SRT_WR_L2_BLOCKNR_REG	0xd4

/* IS UAO UC RD */
#define IPU7P5_IS_UAO_UC_RD_OFFSET		0x27c000
#define IPU7P5_IS_UAO_UC_RD_PLANENUM		4

/* IS UAO UC WR */
#define IPU7P5_IS_UAO_UC_WR_OFFSET		0x27d000
#define IPU7P5_IS_UAO_UC_WR_PLANENUM		4

/* IS UAO M0 WR */
#define IPU7P5_IS_UAO_M0_WR_OFFSET		0x27e000
#define IPU7P5_IS_UAO_M0_WR_PLANENUM		16

/* IS UAO M1 WR */
#define IPU7P5_IS_UAO_M1_WR_OFFSET		0x27f000
#define IPU7P5_IS_UAO_M1_WR_PLANENUM		16

/* PS UAO FW RD */
#define IPU7P5_PS_UAO_FW_RD_OFFSET		0x156000
#define IPU7P5_PS_UAO_FW_RD_PLANENUM		16

/* PS UAO FW WR */
#define IPU7P5_PS_UAO_FW_WR_OFFSET		0x157000
#define IPU7P5_PS_UAO_FW_WR_PLANENUM		10

/* PS UAO SRT RD */
#define IPU7P5_PS_UAO_SRT_RD_OFFSET		0x154000
#define IPU7P5_PS_UAO_SRT_RD_PLANENUM		22

/* PS UAO SRT WR */
#define IPU7P5_PS_UAO_SRT_WR_OFFSET		0x155000
#define IPU7P5_PS_UAO_SRT_WR_PLANENUM		32

#define IPU7P5_IS_ZLX_UC_RD_OFFSET		0x278000
#define IPU7P5_IS_ZLX_UC_WR_OFFSET		0x279000
#define IPU7P5_IS_ZLX_M0_OFFSET			0x27a000
#define IPU7P5_IS_ZLX_M1_OFFSET			0x27b000
#define IPU7P5_IS_ZLX_UC_RD_NUM			4
#define IPU7P5_IS_ZLX_UC_WR_NUM			4
#define IPU7P5_IS_ZLX_M0_NUM			16
#define IPU7P5_IS_ZLX_M1_NUM			16

#define IPU7P5_PS_ZLX_DATA_RD_OFFSET		0x14e000
#define IPU7P5_PS_ZLX_DATA_WR_OFFSET		0x14f000
#define IPU7P5_PS_ZLX_FW_RD_OFFSET		0x150000
#define IPU7P5_PS_ZLX_FW_WR_OFFSET		0x151000
#define IPU7P5_PS_ZLX_DATA_RD_NUM		22
#define IPU7P5_PS_ZLX_DATA_WR_NUM		32
#define IPU7P5_PS_ZLX_FW_RD_NUM			16
#define IPU7P5_PS_ZLX_FW_WR_NUM			10

/* IS MMU Cmd RD */
#define IPU8_IS_MMU_FW_RD_OFFSET		0x270000
#define IPU8_IS_MMU_FW_RD_STREAM_NUM		3
#define IPU8_IS_MMU_FW_RD_L1_BLOCKNR_REG	0x54
#define IPU8_IS_MMU_FW_RD_L2_BLOCKNR_REG	0x60

/* IS MMU Cmd WR */
#define IPU8_IS_MMU_FW_WR_OFFSET		0x271000
#define IPU8_IS_MMU_FW_WR_STREAM_NUM		3
#define IPU8_IS_MMU_FW_WR_L1_BLOCKNR_REG	0x54
#define IPU8_IS_MMU_FW_WR_L2_BLOCKNR_REG	0x60

/* IS MMU Data WR Snoop */
#define IPU8_IS_MMU_M0_OFFSET			0x272000
#define IPU8_IS_MMU_M0_STREAM_NUM		16
#define IPU8_IS_MMU_M0_L1_BLOCKNR_REG		0x54
#define IPU8_IS_MMU_M0_L2_BLOCKNR_REG		0x94

/* IS MMU Data WR ISOC */
#define IPU8_IS_MMU_M1_OFFSET			0x273000
#define IPU8_IS_MMU_M1_STREAM_NUM		16
#define IPU8_IS_MMU_M1_L1_BLOCKNR_REG		0x54
#define IPU8_IS_MMU_M1_L2_BLOCKNR_REG		0x94

/* IS MMU UPIPE ISOC */
#define IPU8_IS_MMU_UPIPE_OFFSET		0x274000
#define IPU8_IS_MMU_UPIPE_STREAM_NUM		6
#define IPU8_IS_MMU_UPIPE_L1_BLOCKNR_REG	0x54
#define IPU8_IS_MMU_UPIPE_L2_BLOCKNR_REG	0x6c

/* PS MMU FW RD */
#define IPU8_PS_MMU_FW_RD_OFFSET		0x148000
#define IPU8_PS_MMU_FW_RD_STREAM_NUM		12
#define IPU8_PS_MMU_FW_RD_L1_BLOCKNR_REG	0x54
#define IPU8_PS_MMU_FW_RD_L2_BLOCKNR_REG	0x84

/* PS MMU FW WR */
#define IPU8_PS_MMU_FW_WR_OFFSET		0x149000
#define IPU8_PS_MMU_FW_WR_STREAM_NUM		8
#define IPU8_PS_MMU_FW_WR_L1_BLOCKNR_REG	0x54
#define IPU8_PS_MMU_FW_WR_L2_BLOCKNR_REG	0x74

/* PS MMU FW Data RD VC0 */
#define IPU8_PS_MMU_SRT_RD_OFFSET		0x14a000
#define IPU8_PS_MMU_SRT_RD_STREAM_NUM		26
#define IPU8_PS_MMU_SRT_RD_L1_BLOCKNR_REG	0x54
#define IPU8_PS_MMU_SRT_RD_L2_BLOCKNR_REG	0xbc

/* PS MMU FW Data WR VC0 */
#define IPU8_PS_MMU_SRT_WR_OFFSET		0x14b000
#define IPU8_PS_MMU_SRT_WR_STREAM_NUM		26
#define IPU8_PS_MMU_SRT_WR_L1_BLOCKNR_REG	0x54
#define IPU8_PS_MMU_SRT_WR_L2_BLOCKNR_REG	0xbc

/* IS UAO UC RD */
#define IPU8_IS_UAO_UC_RD_OFFSET		0x27a000
#define IPU8_IS_UAO_UC_RD_PLANENUM		4

/* IS UAO UC WR */
#define IPU8_IS_UAO_UC_WR_OFFSET		0x27b000
#define IPU8_IS_UAO_UC_WR_PLANENUM		4

/* IS UAO M0 WR */
#define IPU8_IS_UAO_M0_WR_OFFSET		0x27c000
#define IPU8_IS_UAO_M0_WR_PLANENUM		16

/* IS UAO M1 WR */
#define IPU8_IS_UAO_M1_WR_OFFSET		0x27d000
#define IPU8_IS_UAO_M1_WR_PLANENUM		16

/* IS UAO UPIPE */
#define IPU8_IS_UAO_UPIPE_OFFSET		0x27e000
#define IPU8_IS_UAO_UPIPE_PLANENUM		6

/* PS UAO FW RD */
#define IPU8_PS_UAO_FW_RD_OFFSET		0x156000
#define IPU8_PS_UAO_FW_RD_PLANENUM		12

/* PS UAO FW WR */
#define IPU8_PS_UAO_FW_WR_OFFSET		0x157000
#define IPU8_PS_UAO_FW_WR_PLANENUM		8

/* PS UAO SRT RD */
#define IPU8_PS_UAO_SRT_RD_OFFSET		0x154000
#define IPU8_PS_UAO_SRT_RD_PLANENUM		26

/* PS UAO SRT WR */
#define IPU8_PS_UAO_SRT_WR_OFFSET		0x155000
#define IPU8_PS_UAO_SRT_WR_PLANENUM		26

#define IPU8_IS_ZLX_UC_RD_OFFSET		0x275000
#define IPU8_IS_ZLX_UC_WR_OFFSET		0x276000
#define IPU8_IS_ZLX_M0_OFFSET			0x277000
#define IPU8_IS_ZLX_M1_OFFSET			0x278000
#define IPU8_IS_ZLX_UPIPE_OFFSET		0x279000
#define IPU8_IS_ZLX_UC_RD_NUM			4
#define IPU8_IS_ZLX_UC_WR_NUM			4
#define IPU8_IS_ZLX_M0_NUM			16
#define IPU8_IS_ZLX_M1_NUM			16
#define IPU8_IS_ZLX_UPIPE_NUM			6

#define IPU8_PS_ZLX_DATA_RD_OFFSET		0x14e000
#define IPU8_PS_ZLX_DATA_WR_OFFSET		0x14f000
#define IPU8_PS_ZLX_FW_RD_OFFSET		0x150000
#define IPU8_PS_ZLX_FW_WR_OFFSET		0x151000
#define IPU8_PS_ZLX_DATA_RD_NUM			26
#define IPU8_PS_ZLX_DATA_WR_NUM			26
#define IPU8_PS_ZLX_FW_RD_NUM			12
#define IPU8_PS_ZLX_FW_WR_NUM			8

#define MMU_REG_INVALIDATE_0			0x00
#define MMU_REG_INVALIDATE_1			0x04
#define MMU_REG_PAGE_TABLE_BASE_ADDR		0x08
#define MMU_REG_USER_INFO_BITS			0x0c
#define MMU_REG_AXI_REFILL_IF_ID		0x10
#define MMU_REG_PW_EN_BITMAP			0x14
#define MMU_REG_COLLAPSE_ENABLE_BITMAP		0x18
#define MMU_REG_GENERAL_REG			0x1c
#define MMU_REG_AT_SP_ARB_CFG			0x20
#define MMU_REG_INVALIDATION_STATUS		0x24
#define MMU_REG_IRQ_LEVEL_NO_PULSE		0x28
#define MMU_REG_IRQ_MASK			0x2c
#define MMU_REG_IRQ_ENABLE			0x30
#define MMU_REG_IRQ_EDGE			0x34
#define MMU_REG_IRQ_CLEAR			0x38
#define MMU_REG_IRQ_CAUSE			0x3c
#define MMU_REG_CG_CTRL_BITS			0x40
#define MMU_REG_RD_FIFOS_STATUS			0x44
#define MMU_REG_WR_FIFOS_STATUS			0x48
#define MMU_REG_COMMON_FIFOS_STATUS		0x4c
#define MMU_REG_FSM_STATUS			0x50

#define ZLX_REG_AXI_POOL			0x0
#define ZLX_REG_EN				0x20
#define ZLX_REG_CONF				0x24
#define ZLX_REG_CG_CTRL				0x900
#define ZLX_REG_FORCE_BYPASS			0x904

struct ipu7_mmu_info {
	struct device *dev;

	u32 *l1_pt;
	u32 l1_pt_dma;
	u32 **l2_pts;

	u32 *dummy_l2_pt;
	u32 dummy_l2_pteval;
	void *dummy_page;
	u32 dummy_page_pteval;

	dma_addr_t aperture_start;
	dma_addr_t aperture_end;
	unsigned long pgsize_bitmap;

	spinlock_t lock;	/* Serialize access to users */
	struct ipu7_dma_mapping *dmap;
};

struct ipu7_mmu {
	struct list_head node;

	struct ipu7_mmu_hw *mmu_hw;
	unsigned int nr_mmus;
	unsigned int mmid;

	phys_addr_t pgtbl;
	struct device *dev;

	struct ipu7_dma_mapping *dmap;
	struct list_head vma_list;

	struct page *trash_page;
	dma_addr_t pci_trash_page; /* IOVA from PCI DMA services (parent) */
	dma_addr_t iova_trash_page; /* IOVA for IPU child nodes to use */

	bool ready;
	spinlock_t ready_lock;	/* Serialize access to bool ready */

	void (*tlb_invalidate)(struct ipu7_mmu *mmu);
};

struct ipu7_mmu *ipu7_mmu_init(struct device *dev,
			       void __iomem *base, int mmid,
			       const struct ipu7_hw_variants *hw);
void ipu7_mmu_cleanup(struct ipu7_mmu *mmu);
int ipu7_mmu_hw_init(struct ipu7_mmu *mmu);
void ipu7_mmu_hw_cleanup(struct ipu7_mmu *mmu);
int ipu7_mmu_map(struct ipu7_mmu_info *mmu_info, unsigned long iova,
		 phys_addr_t paddr, size_t size);
void ipu7_mmu_unmap(struct ipu7_mmu_info *mmu_info, unsigned long iova,
		    size_t size);
phys_addr_t ipu7_mmu_iova_to_phys(struct ipu7_mmu_info *mmu_info,
				  dma_addr_t iova);
#endif
