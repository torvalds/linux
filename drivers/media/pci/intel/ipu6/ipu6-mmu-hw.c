// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Intel Corporation
 */
#include <asm/barrier.h>

#include <linux/bits.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "ipu6.h"
#include "ipu6-dma.h"
#include "ipu6-mmu.h"
#include "ipu6-platform-regs.h"

#define ISP_PAGE_SHIFT		12
#define ISP_PAGE_SIZE		BIT(ISP_PAGE_SHIFT)
#define ISP_PAGE_MASK		(~(ISP_PAGE_SIZE - 1))

#define ISP_L1PT_SHIFT		22
#define ISP_L1PT_MASK		(~((1U << ISP_L1PT_SHIFT) - 1))

#define ISP_L2PT_SHIFT		12
#define ISP_L2PT_MASK		(~(ISP_L1PT_MASK | (~(ISP_PAGE_MASK))))

#define ISP_L1PT_PTES           1024
#define ISP_L2PT_PTES           1024

#define ISP_PADDR_SHIFT		12

#define REG_TLB_INVALIDATE	0x0000

#define REG_L1_PHYS		0x0004	/* 27-bit pfn */
#define REG_INFO		0x0008

#define TBL_PHYS_ADDR(a)	((phys_addr_t)(a) << ISP_PADDR_SHIFT)

static struct ipu6_mmu_hw ipu6_isys_mmu_hwdata[] = {
	{
		.offset = IPU6_ISYS_IOMMU0_OFFSET,
		.info_bits = IPU6_INFO_REQUEST_DESTINATION_IOSF,
		.nr_l1streams = 16,
		.l1_block_sz = {
			3, 8, 2, 2, 2, 2, 2, 2, 1, 1,
			1, 1, 1, 1, 1, 1
		},
		.nr_l2streams = 16,
		.l2_block_sz = {
			2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
			2, 2, 2, 2, 2, 2
		},
		.insert_read_before_invalidate = false,
		.l1_stream_id_reg_offset =
		IPU6_MMU_L1_STREAM_ID_REG_OFFSET,
		.l2_stream_id_reg_offset =
		IPU6_MMU_L2_STREAM_ID_REG_OFFSET,
	},
	{
		.offset = IPU6_ISYS_IOMMU1_OFFSET,
		.info_bits = 0,
		.nr_l1streams = 16,
		.l1_block_sz = {
			2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
			2, 2, 2, 1, 1, 4
		},
		.nr_l2streams = 16,
		.l2_block_sz = {
			2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
			2, 2, 2, 2, 2, 2
		},
		.insert_read_before_invalidate = false,
		.l1_stream_id_reg_offset =
		IPU6_MMU_L1_STREAM_ID_REG_OFFSET,
		.l2_stream_id_reg_offset =
		IPU6_MMU_L2_STREAM_ID_REG_OFFSET,
	},
	{
		.offset = IPU6_ISYS_IOMMUI_OFFSET,
		.info_bits = 0,
		.nr_l1streams = 0,
		.nr_l2streams = 0,
		.insert_read_before_invalidate = false,
	},
};

static struct ipu6_mmu_hw ipu6_psys_mmu_hwdata[] = {
	{
		.offset = IPU6_PSYS_IOMMU0_OFFSET,
		.info_bits =
		IPU6_INFO_REQUEST_DESTINATION_IOSF,
		.nr_l1streams = 16,
		.l1_block_sz = {
			2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
			2, 2, 2, 2, 2, 2
		},
		.nr_l2streams = 16,
		.l2_block_sz = {
			2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
			2, 2, 2, 2, 2, 2
		},
		.insert_read_before_invalidate = false,
		.l1_stream_id_reg_offset =
		IPU6_MMU_L1_STREAM_ID_REG_OFFSET,
		.l2_stream_id_reg_offset =
		IPU6_MMU_L2_STREAM_ID_REG_OFFSET,
	},
	{
		.offset = IPU6_PSYS_IOMMU1_OFFSET,
		.info_bits = 0,
		.nr_l1streams = 32,
		.l1_block_sz = {
			1, 2, 2, 2, 2, 2, 2, 2, 2, 2,
			2, 2, 2, 2, 2, 10,
			5, 4, 14, 6, 4, 14, 6, 4, 8,
			4, 2, 1, 1, 1, 1, 14
		},
		.nr_l2streams = 32,
		.l2_block_sz = {
			2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
			2, 2, 2, 2, 2, 2,
			2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
			2, 2, 2, 2, 2, 2
		},
		.insert_read_before_invalidate = false,
		.l1_stream_id_reg_offset =
		IPU6_MMU_L1_STREAM_ID_REG_OFFSET,
		.l2_stream_id_reg_offset =
		IPU6_PSYS_MMU1W_L2_STREAM_ID_REG_OFFSET,
	},
	{
		.offset = IPU6_PSYS_IOMMU1R_OFFSET,
		.info_bits = 0,
		.nr_l1streams = 16,
		.l1_block_sz = {
			1, 4, 4, 4, 4, 16, 8, 4, 32,
			16, 16, 2, 2, 2, 1, 12
		},
		.nr_l2streams = 16,
		.l2_block_sz = {
			2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
			2, 2, 2, 2, 2, 2
		},
		.insert_read_before_invalidate = false,
		.l1_stream_id_reg_offset =
		IPU6_MMU_L1_STREAM_ID_REG_OFFSET,
		.l2_stream_id_reg_offset =
		IPU6_MMU_L2_STREAM_ID_REG_OFFSET,
	},
	{
		.offset = IPU6_PSYS_IOMMUI_OFFSET,
		.info_bits = 0,
		.nr_l1streams = 0,
		.nr_l2streams = 0,
		.insert_read_before_invalidate = false,
	},
};

struct ipu6_mmu_hwdata {
	struct ipu6_mmu_hw *hwdata;
	unsigned int nr_mmus;
};

static const struct ipu6_mmu_hwdata ipu6_mmu_hwdata_lookup[IPU_SUBSYS_NUM] = {
	[IPU_PSYS] = {
		.hwdata = ipu6_psys_mmu_hwdata,
		.nr_mmus = ARRAY_SIZE(ipu6_psys_mmu_hwdata),
	},
	[IPU_ISYS] = {
		.hwdata = ipu6_isys_mmu_hwdata,
		.nr_mmus = ARRAY_SIZE(ipu6_isys_mmu_hwdata),
	},
};

static void __ipu6_tlb_invalidate(struct ipu6_mmu *mmu)
{
	struct ipu6_mmu_hw *mmu_hw = mmu->ipu6_mmu_hw;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&mmu->ready_lock, flags);
	if (!mmu->ready) {
		spin_unlock_irqrestore(&mmu->ready_lock, flags);
		return;
	}

	for (i = 0; i < mmu->nr_mmus; i++) {
		/*
		 * To avoid the HW bug induced dead lock in some of the IPU6
		 * MMUs on successive invalidate calls, we need to first do a
		 * read to the page table base before writing the invalidate
		 * register. MMUs which need to implement this WA, will have
		 * the insert_read_before_invalidate flags set as true.
		 * Disregard the return value of the read.
		 */
		if (mmu_hw[i].insert_read_before_invalidate)
			readl(mmu_hw[i].base + REG_L1_PHYS);

		writel(0xffffffff, mmu_hw[i].base + REG_TLB_INVALIDATE);
		/*
		 * The TLB invalidation is a "single cycle" (IOMMU clock cycles)
		 * When the actual MMIO write reaches the IPU6 TLB Invalidate
		 * register, wmb() will force the TLB invalidate out if the CPU
		 * attempts to update the IOMMU page table (or sooner).
		 */
		wmb();
	}
	spin_unlock_irqrestore(&mmu->ready_lock, flags);
}

static int __ipu6_mmu_hw_init(struct ipu6_mmu *mmu)
{
	struct ipu6_mmu_info *mmu_info = mmu->dmap->mmu_info;
	struct ipu6_mmu_hw *mmu_hw = mmu->ipu6_mmu_hw;

	/* Initialise the each MMU HW block */
	for (unsigned int i = 0; i < mmu->nr_mmus; i++) {
		unsigned int j;
		u16 block_addr;

		/* Write page table address per MMU */
		writel((phys_addr_t)mmu_info->l1_pt_dma,
		       mmu_hw[i].base + REG_L1_PHYS);

		/* Set info bits per MMU */
		writel(mmu_hw[i].info_bits, mmu_hw[i].base + REG_INFO);

		/* Configure MMU TLB stream configuration for L1 */
		for (j = 0, block_addr = 0; j < mmu_hw[i].nr_l1streams;
		     block_addr += mmu_hw[i].l1_block_sz[j], j++) {
			if (block_addr > IPU6_MAX_LI_BLOCK_ADDR) {
				dev_err(mmu->dev, "invalid L1 configuration\n");
				return -EINVAL;
			}

			/* Write block start address for each streams */
			writel(block_addr, mmu_hw[i].base +
			       mmu_hw[i].l1_stream_id_reg_offset + 4 * j);
		}

		/* Configure MMU TLB stream configuration for L2 */
		for (j = 0, block_addr = 0; j < mmu_hw[i].nr_l2streams;
		     block_addr += mmu_hw[i].l2_block_sz[j], j++) {
			if (block_addr > IPU6_MAX_L2_BLOCK_ADDR) {
				dev_err(mmu->dev, "invalid L2 configuration\n");
				return -EINVAL;
			}

			writel(block_addr, mmu_hw[i].base +
			       mmu_hw[i].l2_stream_id_reg_offset + 4 * j);
		}
	}

	return 0;
}

static int __ipu6_mmu_init_hw_data(struct ipu6_mmu *mmu, struct device *dev,
				   void __iomem *base)
{
	const struct ipu6_mmu_hwdata *lookup;
	struct ipu6_mmu_hw *mmu_hw, *src;
	unsigned int i, nr_mmus;

	if (mmu->mmid >= IPU_SUBSYS_NUM)
		return -EINVAL;

	lookup = &ipu6_mmu_hwdata_lookup[mmu->mmid];
	src = lookup->hwdata;
	nr_mmus = lookup->nr_mmus;

	mmu_hw = devm_kcalloc(dev, nr_mmus, sizeof(*mmu_hw), GFP_KERNEL);
	if (!mmu_hw)
		return -ENOMEM;

	for (i = 0; i < nr_mmus; i++) {
		if (src[i].nr_l1streams > IPU6_MMU_MAX_TLB_L1_STREAMS ||
		    src[i].nr_l2streams > IPU6_MMU_MAX_TLB_L2_STREAMS)
			return -EINVAL;

		mmu_hw[i] = src[i];
		mmu_hw[i].base = base + src[i].offset;
	}

	mmu->nr_mmus = nr_mmus;
	mmu->ipu6_mmu_hw = mmu_hw;

	return 0;
}

const struct ipu6_mmu_hw_ops ipu6_mmu_ops = {
	.init_hw_data = __ipu6_mmu_init_hw_data,
	.hw_init = __ipu6_mmu_hw_init,
	.tlb_invalidate = __ipu6_tlb_invalidate,
};
