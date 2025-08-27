// SPDX-License-Identifier: GPL-2.0 or Linux-OpenIB
/* Copyright (c) 2018 - 2024 Intel Corporation */
#include "osdep.h"
#include "type.h"
#include "protos.h"
#include "ig3rdma_hw.h"

/**
 * ig3rdma_ena_irq - Enable interrupt
 * @dev: pointer to the device structure
 * @idx: vector index
 */
static void ig3rdma_ena_irq(struct irdma_sc_dev *dev, u32 idx)
{
	u32 val;
	u32 int_stride = 1; /* one u32 per register */

	if (dev->is_pf)
		int_stride = 0x400;
	else
		idx--; /* VFs use DYN_CTL_N */

	val = FIELD_PREP(IRDMA_GLINT_DYN_CTL_INTENA, 1) |
	      FIELD_PREP(IRDMA_GLINT_DYN_CTL_CLEARPBA, 1);

	writel(val, dev->hw_regs[IRDMA_GLINT_DYN_CTL] + (idx * int_stride));
}

/**
 * ig3rdma_disable_irq - Disable interrupt
 * @dev: pointer to the device structure
 * @idx: vector index
 */
static void ig3rdma_disable_irq(struct irdma_sc_dev *dev, u32 idx)
{
	u32 int_stride = 1; /* one u32 per register */

	if (dev->is_pf)
		int_stride = 0x400;
	else
		idx--; /* VFs use DYN_CTL_N */

	writel(0, dev->hw_regs[IRDMA_GLINT_DYN_CTL] + (idx * int_stride));
}

static const struct irdma_irq_ops ig3rdma_irq_ops = {
	.irdma_dis_irq = ig3rdma_disable_irq,
	.irdma_en_irq = ig3rdma_ena_irq,
};

void ig3rdma_init_hw(struct irdma_sc_dev *dev)
{
	dev->irq_ops = &ig3rdma_irq_ops;

	dev->hw_attrs.uk_attrs.hw_rev = IRDMA_GEN_3;
	dev->hw_attrs.uk_attrs.max_hw_wq_frags = IG3RDMA_MAX_WQ_FRAGMENT_COUNT;
	dev->hw_attrs.uk_attrs.max_hw_read_sges = IG3RDMA_MAX_SGE_RD;
	dev->hw_attrs.uk_attrs.max_hw_sq_chunk = IRDMA_MAX_QUANTA_PER_WR;
	dev->hw_attrs.first_hw_vf_fpm_id = 0;
	dev->hw_attrs.max_hw_vf_fpm_id = IG3_MAX_APFS + IG3_MAX_AVFS;
	dev->hw_attrs.uk_attrs.feature_flags |= IRDMA_FEATURE_64_BYTE_CQE;
	if (dev->feature_info[IRDMA_FTN_FLAGS] & IRDMA_ATOMICS_ALLOWED_BIT)
		dev->hw_attrs.uk_attrs.feature_flags |=
			IRDMA_FEATURE_ATOMIC_OPS;
	dev->hw_attrs.uk_attrs.feature_flags |= IRDMA_FEATURE_CQE_TIMESTAMPING;

	dev->hw_attrs.uk_attrs.feature_flags |= IRDMA_FEATURE_SRQ;
	dev->hw_attrs.uk_attrs.feature_flags |= IRDMA_FEATURE_RTS_AE |
						IRDMA_FEATURE_CQ_RESIZE;
	dev->hw_attrs.page_size_cap = SZ_4K | SZ_2M | SZ_1G;
	dev->hw_attrs.max_hw_ird = IG3RDMA_MAX_IRD_SIZE;
	dev->hw_attrs.max_hw_ord = IG3RDMA_MAX_ORD_SIZE;
	dev->hw_attrs.uk_attrs.min_hw_wq_size = IG3RDMA_MIN_WQ_SIZE;
	dev->hw_attrs.uk_attrs.max_hw_srq_quanta = IRDMA_SRQ_MAX_QUANTA;
	dev->hw_attrs.uk_attrs.max_hw_inline = IG3RDMA_MAX_INLINE_DATA_SIZE;
	dev->hw_attrs.max_hw_device_pages =
		dev->is_pf ? IG3RDMA_MAX_PF_PUSH_PAGE_COUNT : IG3RDMA_MAX_VF_PUSH_PAGE_COUNT;
}

static void __iomem *__ig3rdma_get_reg_addr(struct irdma_mmio_region *region, u64 reg_offset)
{
	if (reg_offset >= region->offset &&
	    reg_offset < (region->offset + region->len)) {
		reg_offset -= region->offset;

		return region->addr + reg_offset;
	}

	return NULL;
}

void __iomem *ig3rdma_get_reg_addr(struct irdma_hw *hw, u64 reg_offset)
{
	u8 __iomem *reg_addr;
	int i;

	reg_addr = __ig3rdma_get_reg_addr(&hw->rdma_reg, reg_offset);
	if (reg_addr)
		return reg_addr;

	for (i = 0; i < hw->num_io_regions; i++) {
		reg_addr = __ig3rdma_get_reg_addr(&hw->io_regs[i], reg_offset);
		if (reg_addr)
			return reg_addr;
	}

	WARN_ON_ONCE(1);

	return NULL;
}
