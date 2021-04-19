// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "goyaP.h"
#include "../include/goya/asic_reg/goya_regs.h"

/*
 * goya_set_block_as_protected - set the given block as protected
 *
 * @hdev: pointer to hl_device structure
 * @block: block base address
 *
 */
static void goya_pb_set_block(struct hl_device *hdev, u64 base)
{
	u32 pb_addr = base - CFG_BASE + PROT_BITS_OFFS;

	while (pb_addr & 0xFFF) {
		WREG32(pb_addr, 0);
		pb_addr += 4;
	}
}

static void goya_init_mme_protection_bits(struct hl_device *hdev)
{
	u32 pb_addr, mask;
	u8 word_offset;

	/* TODO: change to real reg name when Soc Online is updated */
	u64 mmMME_SBB_POWER_ECO1 = 0xDFF60,
		mmMME_SBB_POWER_ECO2 = 0xDFF64;

	goya_pb_set_block(hdev, mmACC_MS_ECC_MEM_0_BASE);
	goya_pb_set_block(hdev, mmACC_MS_ECC_MEM_1_BASE);
	goya_pb_set_block(hdev, mmACC_MS_ECC_MEM_2_BASE);
	goya_pb_set_block(hdev, mmACC_MS_ECC_MEM_3_BASE);

	goya_pb_set_block(hdev, mmSBA_ECC_MEM_BASE);
	goya_pb_set_block(hdev, mmSBB_ECC_MEM_BASE);

	goya_pb_set_block(hdev, mmMME1_RTR_BASE);
	goya_pb_set_block(hdev, mmMME1_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmMME1_WR_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmMME2_RTR_BASE);
	goya_pb_set_block(hdev, mmMME2_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmMME2_WR_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmMME3_RTR_BASE);
	goya_pb_set_block(hdev, mmMME3_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmMME3_WR_REGULATOR_BASE);

	goya_pb_set_block(hdev, mmMME4_RTR_BASE);
	goya_pb_set_block(hdev, mmMME4_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmMME4_WR_REGULATOR_BASE);

	goya_pb_set_block(hdev, mmMME5_RTR_BASE);
	goya_pb_set_block(hdev, mmMME5_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmMME5_WR_REGULATOR_BASE);

	goya_pb_set_block(hdev, mmMME6_RTR_BASE);
	goya_pb_set_block(hdev, mmMME6_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmMME6_WR_REGULATOR_BASE);

	pb_addr = (mmMME_DUMMY & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmMME_DUMMY & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmMME_DUMMY & 0x7F) >> 2);
	mask |= 1 << ((mmMME_RESET & 0x7F) >> 2);
	mask |= 1 << ((mmMME_STALL & 0x7F) >> 2);
	mask |= 1 << ((mmMME_SM_BASE_ADDRESS_LOW & 0x7F) >> 2);
	mask |= 1 << ((mmMME_SM_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmMME_DBGMEM_ADD & 0x7F) >> 2);
	mask |= 1 << ((mmMME_DBGMEM_DATA_WR & 0x7F) >> 2);
	mask |= 1 << ((mmMME_DBGMEM_DATA_RD & 0x7F) >> 2);
	mask |= 1 << ((mmMME_DBGMEM_CTRL & 0x7F) >> 2);
	mask |= 1 << ((mmMME_DBGMEM_RC & 0x7F) >> 2);
	mask |= 1 << ((mmMME_LOG_SHADOW & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmMME_STORE_MAX_CREDIT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmMME_STORE_MAX_CREDIT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmMME_STORE_MAX_CREDIT & 0x7F) >> 2);
	mask |= 1 << ((mmMME_AGU & 0x7F) >> 2);
	mask |= 1 << ((mmMME_SBA & 0x7F) >> 2);
	mask |= 1 << ((mmMME_SBB & 0x7F) >> 2);
	mask |= 1 << ((mmMME_SBC & 0x7F) >> 2);
	mask |= 1 << ((mmMME_WBC & 0x7F) >> 2);
	mask |= 1 << ((mmMME_SBA_CONTROL_DATA & 0x7F) >> 2);
	mask |= 1 << ((mmMME_SBB_CONTROL_DATA & 0x7F) >> 2);
	mask |= 1 << ((mmMME_SBC_CONTROL_DATA & 0x7F) >> 2);
	mask |= 1 << ((mmMME_WBC_CONTROL_DATA & 0x7F) >> 2);
	mask |= 1 << ((mmMME_TE & 0x7F) >> 2);
	mask |= 1 << ((mmMME_TE2DEC & 0x7F) >> 2);
	mask |= 1 << ((mmMME_REI_STATUS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_REI_MASK & 0x7F) >> 2);
	mask |= 1 << ((mmMME_SEI_STATUS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_SEI_MASK & 0x7F) >> 2);
	mask |= 1 << ((mmMME_SPI_STATUS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_SPI_MASK & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmMME_QM_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmMME_QM_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmMME_QM_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_GLBL_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_BASE_LO & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_BASE_HI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_SIZE & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_PI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_CI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_ARUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmMME_QM_PQ_PUSH0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmMME_QM_PQ_PUSH0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmMME_QM_PQ_PUSH0 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_PUSH1 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_PUSH2 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_PUSH3 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_PTR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_PTR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_TSIZE & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_CTL & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmMME_QM_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmMME_QM_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmMME_QM_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmMME_QM_CP_STS & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmMME_QM_CP_STS & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmMME_QM_CP_STS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_CURRENT_INST_LO & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_CURRENT_INST_HI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_BARRIER_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CP_DBG_0 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_BUF_ADDR & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_PQ_BUF_RDATA & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_BUF_ADDR & 0x7F) >> 2);
	mask |= 1 << ((mmMME_QM_CQ_BUF_RDATA & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmMME_CMDQ_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmMME_CMDQ_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmMME_CMDQ_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_GLBL_STS1 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmMME_CMDQ_CQ_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmMME_CMDQ_CQ_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmMME_CMDQ_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmMME_CMDQ_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmMME_CMDQ_CQ_IFIFO_CNT &
			PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmMME_CMDQ_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_STS & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_CURRENT_INST_LO & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmMME_CMDQ_CP_CURRENT_INST_HI & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmMME_CMDQ_CP_CURRENT_INST_HI & PROT_BITS_OFFS) >> 7)
			<< 2;
	mask = 1 << ((mmMME_CMDQ_CP_CURRENT_INST_HI & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_BARRIER_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CP_DBG_0 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CQ_BUF_ADDR & 0x7F) >> 2);
	mask |= 1 << ((mmMME_CMDQ_CQ_BUF_RDATA & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmMME_SBB_POWER_ECO1 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmMME_SBB_POWER_ECO1 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmMME_SBB_POWER_ECO1 & 0x7F) >> 2);
	mask |= 1 << ((mmMME_SBB_POWER_ECO2 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);
}

static void goya_init_dma_protection_bits(struct hl_device *hdev)
{
	u32 pb_addr, mask;
	u8 word_offset;

	goya_pb_set_block(hdev, mmDMA_NRTR_BASE);
	goya_pb_set_block(hdev, mmDMA_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmDMA_WR_REGULATOR_BASE);

	pb_addr = (mmDMA_QM_0_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_0_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_0_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_GLBL_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_BASE_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_BASE_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_SIZE & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_PI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_CI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_ARUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmDMA_QM_0_PQ_PUSH0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_0_PQ_PUSH0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_0_PQ_PUSH0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_PUSH1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_PUSH2 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_PUSH3 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_PQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_PTR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_PTR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_TSIZE & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_CTL & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmDMA_QM_0_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_0_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_0_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_0_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	goya_pb_set_block(hdev, mmDMA_CH_0_BASE);

	pb_addr = (mmDMA_QM_1_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_1_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_1_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_GLBL_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_BASE_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_BASE_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_SIZE & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_PI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_CI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_ARUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmDMA_QM_1_PQ_PUSH0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_1_PQ_PUSH0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_1_PQ_PUSH0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_PUSH1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_PUSH2 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_PUSH3 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_PQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_PTR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_PTR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_TSIZE & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_CTL & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmDMA_QM_1_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_1_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_1_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_1_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	goya_pb_set_block(hdev, mmDMA_CH_1_BASE);

	pb_addr = (mmDMA_QM_2_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_2_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_2_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_GLBL_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_BASE_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_BASE_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_SIZE & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_PI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_CI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_ARUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmDMA_QM_2_PQ_PUSH0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_2_PQ_PUSH0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_2_PQ_PUSH0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_PUSH1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_PUSH2 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_PUSH3 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_PQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_PTR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_PTR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_TSIZE & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_CTL & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmDMA_QM_2_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_2_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_2_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_2_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	goya_pb_set_block(hdev, mmDMA_CH_2_BASE);

	pb_addr = (mmDMA_QM_3_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_3_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_3_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_GLBL_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_BASE_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_BASE_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_SIZE & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_PI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_CI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_ARUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmDMA_QM_3_PQ_PUSH0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_3_PQ_PUSH0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_3_PQ_PUSH0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_PUSH1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_PUSH2 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_PUSH3 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_PQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_PTR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_PTR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_TSIZE & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_CTL & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmDMA_QM_3_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_3_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_3_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_3_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	goya_pb_set_block(hdev, mmDMA_CH_3_BASE);

	pb_addr = (mmDMA_QM_4_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_4_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_4_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_GLBL_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_BASE_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_BASE_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_SIZE & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_PI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_CI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_ARUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmDMA_QM_4_PQ_PUSH0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_4_PQ_PUSH0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_4_PQ_PUSH0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_PUSH1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_PUSH2 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_PUSH3 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_PQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_PTR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_PTR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_TSIZE & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_CTL & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmDMA_QM_4_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmDMA_QM_4_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmDMA_QM_4_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmDMA_QM_4_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	goya_pb_set_block(hdev, mmDMA_CH_4_BASE);
}

static void goya_init_tpc_protection_bits(struct hl_device *hdev)
{
	u32 pb_addr, mask;
	u8 word_offset;

	goya_pb_set_block(hdev, mmTPC0_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmTPC0_WR_REGULATOR_BASE);

	pb_addr = (mmTPC0_CFG_SEMAPHORE & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC0_CFG_SEMAPHORE & PROT_BITS_OFFS) >> 7) << 2;

	mask = 1 << ((mmTPC0_CFG_SEMAPHORE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_VFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_SFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_STATUS & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC0_CFG_CFG_BASE_ADDRESS_HIGH & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC0_CFG_CFG_BASE_ADDRESS_HIGH &
			PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC0_CFG_CFG_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_CFG_SUBTRACT_VALUE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_SM_BASE_ADDRESS_LOW & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_SM_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_TPC_STALL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_MSS_CONFIG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_TPC_INTR_CAUSE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_TPC_INTR_MASK & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC0_CFG_ARUSER & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC0_CFG_ARUSER & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC0_CFG_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_AWUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC0_CFG_FUNC_MBIST_CNTRL & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC0_CFG_FUNC_MBIST_CNTRL &
			PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC0_CFG_FUNC_MBIST_CNTRL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_FUNC_MBIST_PAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_FUNC_MBIST_MEM_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_FUNC_MBIST_MEM_1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_FUNC_MBIST_MEM_2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_FUNC_MBIST_MEM_3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_FUNC_MBIST_MEM_4 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_FUNC_MBIST_MEM_5 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_FUNC_MBIST_MEM_6 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_FUNC_MBIST_MEM_7 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_FUNC_MBIST_MEM_8 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CFG_FUNC_MBIST_MEM_9 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC0_QM_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC0_QM_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC0_QM_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_GLBL_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_BASE_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_BASE_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_SIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_PI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_CI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_ARUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC0_QM_PQ_PUSH0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC0_QM_PQ_PUSH0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC0_QM_PQ_PUSH0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_PUSH1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_PUSH2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_PUSH3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_PQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_PTR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_PTR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_TSIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_CTL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC0_QM_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC0_QM_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC0_QM_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_QM_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC0_CMDQ_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC0_CMDQ_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC0_CMDQ_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_GLBL_STS1 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC0_CMDQ_CQ_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC0_CMDQ_CQ_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC0_CMDQ_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC0_CMDQ_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC0_CMDQ_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC0_CMDQ_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_CURRENT_INST_LO & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC0_CMDQ_CP_CURRENT_INST_HI & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC0_CMDQ_CP_CURRENT_INST_HI & PROT_BITS_OFFS) >> 7)
			<< 2;
	mask = 1 << ((mmTPC0_CMDQ_CP_CURRENT_INST_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_BARRIER_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CP_DBG_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CQ_BUF_ADDR & 0x7F) >> 2);
	mask |= 1 << ((mmTPC0_CMDQ_CQ_BUF_RDATA & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	goya_pb_set_block(hdev, mmTPC1_RTR_BASE);
	goya_pb_set_block(hdev, mmTPC1_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmTPC1_WR_REGULATOR_BASE);

	pb_addr = (mmTPC1_CFG_SEMAPHORE & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC1_CFG_SEMAPHORE & PROT_BITS_OFFS) >> 7) << 2;

	mask = 1 << ((mmTPC1_CFG_SEMAPHORE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_VFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_SFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_STATUS & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC1_CFG_CFG_BASE_ADDRESS_HIGH & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC1_CFG_CFG_BASE_ADDRESS_HIGH &
			PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC1_CFG_CFG_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_CFG_SUBTRACT_VALUE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_SM_BASE_ADDRESS_LOW & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_SM_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_TPC_STALL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_MSS_CONFIG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_TPC_INTR_CAUSE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_TPC_INTR_MASK & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC1_CFG_ARUSER & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC1_CFG_ARUSER & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC1_CFG_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_AWUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC1_CFG_FUNC_MBIST_CNTRL & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC1_CFG_FUNC_MBIST_CNTRL & PROT_BITS_OFFS) >> 7)
			<< 2;
	mask = 1 << ((mmTPC1_CFG_FUNC_MBIST_CNTRL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_FUNC_MBIST_PAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_FUNC_MBIST_MEM_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_FUNC_MBIST_MEM_1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_FUNC_MBIST_MEM_2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_FUNC_MBIST_MEM_3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_FUNC_MBIST_MEM_4 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_FUNC_MBIST_MEM_5 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_FUNC_MBIST_MEM_6 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_FUNC_MBIST_MEM_7 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_FUNC_MBIST_MEM_8 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CFG_FUNC_MBIST_MEM_9 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC1_QM_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC1_QM_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC1_QM_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_GLBL_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_BASE_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_BASE_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_SIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_PI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_CI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_ARUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC1_QM_PQ_PUSH0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC1_QM_PQ_PUSH0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC1_QM_PQ_PUSH0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_PUSH1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_PUSH2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_PUSH3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_PQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_PTR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_PTR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_TSIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_CTL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC1_QM_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC1_QM_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC1_QM_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_QM_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC1_CMDQ_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC1_CMDQ_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC1_CMDQ_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_GLBL_STS1 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC1_CMDQ_CQ_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC1_CMDQ_CQ_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC1_CMDQ_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC1_CMDQ_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC1_CMDQ_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC1_CMDQ_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_CURRENT_INST_LO & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC1_CMDQ_CP_CURRENT_INST_HI & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC1_CMDQ_CP_CURRENT_INST_HI & PROT_BITS_OFFS) >> 7)
			<< 2;
	mask = 1 << ((mmTPC1_CMDQ_CP_CURRENT_INST_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_BARRIER_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CP_DBG_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CQ_BUF_ADDR & 0x7F) >> 2);
	mask |= 1 << ((mmTPC1_CMDQ_CQ_BUF_RDATA & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	goya_pb_set_block(hdev, mmTPC2_RTR_BASE);
	goya_pb_set_block(hdev, mmTPC2_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmTPC2_WR_REGULATOR_BASE);

	pb_addr = (mmTPC2_CFG_SEMAPHORE & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC2_CFG_SEMAPHORE & PROT_BITS_OFFS) >> 7) << 2;

	mask = 1 << ((mmTPC2_CFG_SEMAPHORE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_VFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_SFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_STATUS & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC2_CFG_CFG_BASE_ADDRESS_HIGH & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC2_CFG_CFG_BASE_ADDRESS_HIGH &
			PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC2_CFG_CFG_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_CFG_SUBTRACT_VALUE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_SM_BASE_ADDRESS_LOW & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_SM_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_TPC_STALL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_MSS_CONFIG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_TPC_INTR_CAUSE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_TPC_INTR_MASK & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC2_CFG_ARUSER & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC2_CFG_ARUSER & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC2_CFG_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_AWUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC2_CFG_FUNC_MBIST_CNTRL & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC2_CFG_FUNC_MBIST_CNTRL & PROT_BITS_OFFS) >> 7)
			<< 2;
	mask = 1 << ((mmTPC2_CFG_FUNC_MBIST_CNTRL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_FUNC_MBIST_PAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_FUNC_MBIST_MEM_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_FUNC_MBIST_MEM_1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_FUNC_MBIST_MEM_2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_FUNC_MBIST_MEM_3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_FUNC_MBIST_MEM_4 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_FUNC_MBIST_MEM_5 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_FUNC_MBIST_MEM_6 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_FUNC_MBIST_MEM_7 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_FUNC_MBIST_MEM_8 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CFG_FUNC_MBIST_MEM_9 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC2_QM_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC2_QM_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC2_QM_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_GLBL_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_BASE_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_BASE_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_SIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_PI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_CI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_ARUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC2_QM_PQ_PUSH0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC2_QM_PQ_PUSH0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC2_QM_PQ_PUSH0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_PUSH1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_PUSH2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_PUSH3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_PQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_PTR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_PTR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_TSIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_CTL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC2_QM_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC2_QM_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC2_QM_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_QM_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC2_CMDQ_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC2_CMDQ_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC2_CMDQ_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_GLBL_STS1 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC2_CMDQ_CQ_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC2_CMDQ_CQ_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC2_CMDQ_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC2_CMDQ_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC2_CMDQ_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC2_CMDQ_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_CURRENT_INST_LO & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC2_CMDQ_CP_CURRENT_INST_HI & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC2_CMDQ_CP_CURRENT_INST_HI & PROT_BITS_OFFS) >> 7)
			<< 2;
	mask = 1 << ((mmTPC2_CMDQ_CP_CURRENT_INST_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_BARRIER_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CP_DBG_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CQ_BUF_ADDR & 0x7F) >> 2);
	mask |= 1 << ((mmTPC2_CMDQ_CQ_BUF_RDATA & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	goya_pb_set_block(hdev, mmTPC3_RTR_BASE);
	goya_pb_set_block(hdev, mmTPC3_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmTPC3_WR_REGULATOR_BASE);

	pb_addr = (mmTPC3_CFG_SEMAPHORE & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC3_CFG_SEMAPHORE & PROT_BITS_OFFS) >> 7) << 2;

	mask = 1 << ((mmTPC3_CFG_SEMAPHORE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_VFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_SFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_STATUS & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC3_CFG_CFG_BASE_ADDRESS_HIGH & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC3_CFG_CFG_BASE_ADDRESS_HIGH
			& PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC3_CFG_CFG_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_CFG_SUBTRACT_VALUE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_SM_BASE_ADDRESS_LOW & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_SM_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_TPC_STALL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_MSS_CONFIG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_TPC_INTR_CAUSE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_TPC_INTR_MASK & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC3_CFG_ARUSER & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC3_CFG_ARUSER & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC3_CFG_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_AWUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC3_CFG_FUNC_MBIST_CNTRL & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC3_CFG_FUNC_MBIST_CNTRL
			& PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC3_CFG_FUNC_MBIST_CNTRL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_FUNC_MBIST_PAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_FUNC_MBIST_MEM_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_FUNC_MBIST_MEM_1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_FUNC_MBIST_MEM_2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_FUNC_MBIST_MEM_3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_FUNC_MBIST_MEM_4 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_FUNC_MBIST_MEM_5 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_FUNC_MBIST_MEM_6 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_FUNC_MBIST_MEM_7 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_FUNC_MBIST_MEM_8 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CFG_FUNC_MBIST_MEM_9 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC3_QM_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC3_QM_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC3_QM_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_GLBL_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_BASE_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_BASE_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_SIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_PI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_CI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_ARUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC3_QM_PQ_PUSH0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC3_QM_PQ_PUSH0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC3_QM_PQ_PUSH0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_PUSH1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_PUSH2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_PUSH3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_PQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_PTR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_PTR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_TSIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_CTL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC3_QM_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC3_QM_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC3_QM_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_QM_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC3_CMDQ_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC3_CMDQ_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC3_CMDQ_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_GLBL_STS1 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC3_CMDQ_CQ_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC3_CMDQ_CQ_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC3_CMDQ_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC3_CMDQ_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC3_CMDQ_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC3_CMDQ_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_CURRENT_INST_LO & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC3_CMDQ_CP_CURRENT_INST_HI & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC3_CMDQ_CP_CURRENT_INST_HI & PROT_BITS_OFFS) >> 7)
			<< 2;
	mask = 1 << ((mmTPC3_CMDQ_CP_CURRENT_INST_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_BARRIER_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CP_DBG_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CQ_BUF_ADDR & 0x7F) >> 2);
	mask |= 1 << ((mmTPC3_CMDQ_CQ_BUF_RDATA & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	goya_pb_set_block(hdev, mmTPC4_RTR_BASE);
	goya_pb_set_block(hdev, mmTPC4_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmTPC4_WR_REGULATOR_BASE);

	pb_addr = (mmTPC4_CFG_SEMAPHORE & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC4_CFG_SEMAPHORE & PROT_BITS_OFFS) >> 7) << 2;

	mask = 1 << ((mmTPC4_CFG_SEMAPHORE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_VFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_SFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_STATUS & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC4_CFG_CFG_BASE_ADDRESS_HIGH & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC4_CFG_CFG_BASE_ADDRESS_HIGH &
			PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC4_CFG_CFG_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_CFG_SUBTRACT_VALUE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_SM_BASE_ADDRESS_LOW & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_SM_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_TPC_STALL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_MSS_CONFIG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_TPC_INTR_CAUSE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_TPC_INTR_MASK & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC4_CFG_ARUSER & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC4_CFG_ARUSER & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC4_CFG_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_AWUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC4_CFG_FUNC_MBIST_CNTRL & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC4_CFG_FUNC_MBIST_CNTRL &
			PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC4_CFG_FUNC_MBIST_CNTRL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_FUNC_MBIST_PAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_FUNC_MBIST_MEM_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_FUNC_MBIST_MEM_1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_FUNC_MBIST_MEM_2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_FUNC_MBIST_MEM_3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_FUNC_MBIST_MEM_4 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_FUNC_MBIST_MEM_5 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_FUNC_MBIST_MEM_6 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_FUNC_MBIST_MEM_7 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_FUNC_MBIST_MEM_8 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CFG_FUNC_MBIST_MEM_9 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC4_QM_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC4_QM_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC4_QM_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_GLBL_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_BASE_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_BASE_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_SIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_PI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_CI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_ARUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC4_QM_PQ_PUSH0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC4_QM_PQ_PUSH0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC4_QM_PQ_PUSH0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_PUSH1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_PUSH2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_PUSH3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_PQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_PTR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_PTR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_TSIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_CTL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC4_QM_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC4_QM_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC4_QM_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_QM_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC4_CMDQ_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC4_CMDQ_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC4_CMDQ_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_GLBL_STS1 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC4_CMDQ_CQ_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC4_CMDQ_CQ_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC4_CMDQ_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC4_CMDQ_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC4_CMDQ_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC4_CMDQ_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_CURRENT_INST_LO & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC4_CMDQ_CP_CURRENT_INST_HI & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC4_CMDQ_CP_CURRENT_INST_HI & PROT_BITS_OFFS) >> 7)
			<< 2;
	mask = 1 << ((mmTPC4_CMDQ_CP_CURRENT_INST_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_BARRIER_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CP_DBG_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CQ_BUF_ADDR & 0x7F) >> 2);
	mask |= 1 << ((mmTPC4_CMDQ_CQ_BUF_RDATA & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	goya_pb_set_block(hdev, mmTPC5_RTR_BASE);
	goya_pb_set_block(hdev, mmTPC5_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmTPC5_WR_REGULATOR_BASE);

	pb_addr = (mmTPC5_CFG_SEMAPHORE & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC5_CFG_SEMAPHORE & PROT_BITS_OFFS) >> 7) << 2;

	mask = 1 << ((mmTPC5_CFG_SEMAPHORE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_VFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_SFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_STATUS & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC5_CFG_CFG_BASE_ADDRESS_HIGH & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC5_CFG_CFG_BASE_ADDRESS_HIGH &
			PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC5_CFG_CFG_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_CFG_SUBTRACT_VALUE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_SM_BASE_ADDRESS_LOW & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_SM_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_TPC_STALL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_MSS_CONFIG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_TPC_INTR_CAUSE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_TPC_INTR_MASK & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC5_CFG_ARUSER & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC5_CFG_ARUSER & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC5_CFG_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_AWUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC5_CFG_FUNC_MBIST_CNTRL & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC5_CFG_FUNC_MBIST_CNTRL &
			PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC5_CFG_FUNC_MBIST_CNTRL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_FUNC_MBIST_PAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_FUNC_MBIST_MEM_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_FUNC_MBIST_MEM_1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_FUNC_MBIST_MEM_2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_FUNC_MBIST_MEM_3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_FUNC_MBIST_MEM_4 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_FUNC_MBIST_MEM_5 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_FUNC_MBIST_MEM_6 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_FUNC_MBIST_MEM_7 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_FUNC_MBIST_MEM_8 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CFG_FUNC_MBIST_MEM_9 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC5_QM_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC5_QM_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC5_QM_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_GLBL_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_BASE_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_BASE_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_SIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_PI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_CI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_ARUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC5_QM_PQ_PUSH0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC5_QM_PQ_PUSH0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC5_QM_PQ_PUSH0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_PUSH1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_PUSH2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_PUSH3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_PQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_PTR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_PTR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_TSIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_CTL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC5_QM_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC5_QM_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC5_QM_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_QM_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC5_CMDQ_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC5_CMDQ_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC5_CMDQ_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_GLBL_STS1 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC5_CMDQ_CQ_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC5_CMDQ_CQ_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC5_CMDQ_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC5_CMDQ_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC5_CMDQ_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC5_CMDQ_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_CURRENT_INST_LO & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC5_CMDQ_CP_CURRENT_INST_HI & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC5_CMDQ_CP_CURRENT_INST_HI & PROT_BITS_OFFS) >> 7)
			<< 2;
	mask = 1 << ((mmTPC5_CMDQ_CP_CURRENT_INST_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_BARRIER_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CP_DBG_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CQ_BUF_ADDR & 0x7F) >> 2);
	mask |= 1 << ((mmTPC5_CMDQ_CQ_BUF_RDATA & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	goya_pb_set_block(hdev, mmTPC6_RTR_BASE);
	goya_pb_set_block(hdev, mmTPC6_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmTPC6_WR_REGULATOR_BASE);

	pb_addr = (mmTPC6_CFG_SEMAPHORE & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC6_CFG_SEMAPHORE & PROT_BITS_OFFS) >> 7) << 2;

	mask = 1 << ((mmTPC6_CFG_SEMAPHORE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_VFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_SFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_STATUS & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC6_CFG_CFG_BASE_ADDRESS_HIGH & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC6_CFG_CFG_BASE_ADDRESS_HIGH &
			PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC6_CFG_CFG_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_CFG_SUBTRACT_VALUE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_SM_BASE_ADDRESS_LOW & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_SM_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_TPC_STALL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_MSS_CONFIG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_TPC_INTR_CAUSE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_TPC_INTR_MASK & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC6_CFG_ARUSER & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC6_CFG_ARUSER & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC6_CFG_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_AWUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC6_CFG_FUNC_MBIST_CNTRL & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC6_CFG_FUNC_MBIST_CNTRL &
			PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC6_CFG_FUNC_MBIST_CNTRL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_FUNC_MBIST_PAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_FUNC_MBIST_MEM_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_FUNC_MBIST_MEM_1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_FUNC_MBIST_MEM_2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_FUNC_MBIST_MEM_3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_FUNC_MBIST_MEM_4 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_FUNC_MBIST_MEM_5 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_FUNC_MBIST_MEM_6 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_FUNC_MBIST_MEM_7 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_FUNC_MBIST_MEM_8 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CFG_FUNC_MBIST_MEM_9 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC6_QM_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC6_QM_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC6_QM_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_GLBL_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_BASE_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_BASE_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_SIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_PI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_CI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_ARUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC6_QM_PQ_PUSH0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC6_QM_PQ_PUSH0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC6_QM_PQ_PUSH0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_PUSH1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_PUSH2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_PUSH3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_PQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_PTR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_PTR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_TSIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_CTL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC6_QM_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC6_QM_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC6_QM_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_QM_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC6_CMDQ_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC6_CMDQ_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC6_CMDQ_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_GLBL_STS1 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC6_CMDQ_CQ_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC6_CMDQ_CQ_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC6_CMDQ_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC6_CMDQ_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC6_CMDQ_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC6_CMDQ_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_CURRENT_INST_LO & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC6_CMDQ_CP_CURRENT_INST_HI & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC6_CMDQ_CP_CURRENT_INST_HI & PROT_BITS_OFFS) >> 7)
			<< 2;
	mask = 1 << ((mmTPC6_CMDQ_CP_CURRENT_INST_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_BARRIER_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CP_DBG_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CQ_BUF_ADDR & 0x7F) >> 2);
	mask |= 1 << ((mmTPC6_CMDQ_CQ_BUF_RDATA & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	goya_pb_set_block(hdev, mmTPC7_NRTR_BASE);
	goya_pb_set_block(hdev, mmTPC7_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmTPC7_WR_REGULATOR_BASE);

	pb_addr = (mmTPC7_CFG_SEMAPHORE & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC7_CFG_SEMAPHORE & PROT_BITS_OFFS) >> 7) << 2;

	mask = 1 << ((mmTPC7_CFG_SEMAPHORE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_VFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_SFLAGS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_STATUS & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC7_CFG_CFG_BASE_ADDRESS_HIGH & ~0xFFF) +	PROT_BITS_OFFS;
	word_offset = ((mmTPC7_CFG_CFG_BASE_ADDRESS_HIGH &
			PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC7_CFG_CFG_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_CFG_SUBTRACT_VALUE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_SM_BASE_ADDRESS_LOW & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_SM_BASE_ADDRESS_HIGH & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_TPC_STALL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_MSS_CONFIG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_TPC_INTR_CAUSE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_TPC_INTR_MASK & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC7_CFG_ARUSER & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC7_CFG_ARUSER & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC7_CFG_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_AWUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC7_CFG_FUNC_MBIST_CNTRL & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC7_CFG_FUNC_MBIST_CNTRL &
			PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC7_CFG_FUNC_MBIST_CNTRL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_FUNC_MBIST_PAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_FUNC_MBIST_MEM_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_FUNC_MBIST_MEM_1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_FUNC_MBIST_MEM_2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_FUNC_MBIST_MEM_3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_FUNC_MBIST_MEM_4 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_FUNC_MBIST_MEM_5 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_FUNC_MBIST_MEM_6 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_FUNC_MBIST_MEM_7 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_FUNC_MBIST_MEM_8 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CFG_FUNC_MBIST_MEM_9 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC7_QM_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC7_QM_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC7_QM_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_GLBL_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_BASE_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_BASE_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_SIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_PI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_CI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_ARUSER & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC7_QM_PQ_PUSH0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC7_QM_PQ_PUSH0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC7_QM_PQ_PUSH0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_PUSH1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_PUSH2 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_PUSH3 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_PQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_PTR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_PTR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_TSIZE & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_CTL & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC7_QM_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC7_QM_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC7_QM_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_QM_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC7_CMDQ_GLBL_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC7_CMDQ_GLBL_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC7_CMDQ_GLBL_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_GLBL_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_GLBL_PROT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_GLBL_ERR_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_GLBL_ERR_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_GLBL_ERR_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_GLBL_ERR_WDATA & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_GLBL_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_GLBL_NON_SECURE_PROPS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_GLBL_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_GLBL_STS1 & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC7_CMDQ_CQ_CFG0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC7_CMDQ_CQ_CFG0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC7_CMDQ_CQ_CFG0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CQ_CFG1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CQ_ARUSER & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CQ_PTR_LO_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CQ_PTR_HI_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CQ_TSIZE_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CQ_CTL_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CQ_STS0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CQ_STS1 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CQ_RD_RATE_LIM_EN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CQ_RD_RATE_LIM_RST_TOKEN & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CQ_RD_RATE_LIM_SAT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CQ_RD_RATE_LIM_TOUT & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC7_CMDQ_CQ_IFIFO_CNT & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC7_CMDQ_CQ_IFIFO_CNT & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC7_CMDQ_CQ_IFIFO_CNT & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_MSG_BASE0_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_MSG_BASE0_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_MSG_BASE1_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_MSG_BASE1_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_MSG_BASE2_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_MSG_BASE2_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_MSG_BASE3_ADDR_LO & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_MSG_BASE3_ADDR_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_LDMA_TSIZE_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_LDMA_SRC_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_LDMA_SRC_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_LDMA_DST_BASE_LO_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_LDMA_DST_BASE_HI_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_LDMA_COMMIT_OFFSET & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_STS & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_CURRENT_INST_LO & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);

	pb_addr = (mmTPC7_CMDQ_CP_CURRENT_INST_HI & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC7_CMDQ_CP_CURRENT_INST_HI & PROT_BITS_OFFS) >> 7)
			<< 2;
	mask = 1 << ((mmTPC7_CMDQ_CP_CURRENT_INST_HI & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_BARRIER_CFG & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CP_DBG_0 & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CQ_BUF_ADDR & 0x7F) >> 2);
	mask |= 1 << ((mmTPC7_CMDQ_CQ_BUF_RDATA & 0x7F) >> 2);

	WREG32(pb_addr + word_offset, ~mask);
}

/*
 * goya_init_protection_bits - Initialize protection bits for specific registers
 *
 * @hdev: pointer to hl_device structure
 *
 * All protection bits are 1 by default, means not protected. Need to set to 0
 * each bit that belongs to a protected register.
 *
 */
static void goya_init_protection_bits(struct hl_device *hdev)
{
	/*
	 * In each 4K block of registers, the last 128 bytes are protection
	 * bits - total of 1024 bits, one for each register. Each bit is related
	 * to a specific register, by the order of the registers.
	 * So in order to calculate the bit that is related to a given register,
	 * we need to calculate its word offset and then the exact bit inside
	 * the word (which is 4 bytes).
	 *
	 * Register address:
	 *
	 * 31                 12 11           7   6             2  1      0
	 * -----------------------------------------------------------------
	 * |      Don't         |    word       |  bit location  |    0    |
	 * |      care          |   offset      |  inside word   |         |
	 * -----------------------------------------------------------------
	 *
	 * Bits 7-11 represents the word offset inside the 128 bytes.
	 * Bits 2-6 represents the bit location inside the word.
	 */
	u32 pb_addr, mask;
	u8 word_offset;

	goya_pb_set_block(hdev, mmPCI_NRTR_BASE);
	goya_pb_set_block(hdev, mmPCI_RD_REGULATOR_BASE);
	goya_pb_set_block(hdev, mmPCI_WR_REGULATOR_BASE);

	goya_pb_set_block(hdev, mmSRAM_Y0_X0_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y0_X0_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y0_X1_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y0_X1_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y0_X2_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y0_X2_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y0_X3_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y0_X3_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y0_X4_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y0_X4_RTR_BASE);

	goya_pb_set_block(hdev, mmSRAM_Y1_X0_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y1_X0_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y1_X1_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y1_X1_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y1_X2_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y1_X2_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y1_X3_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y1_X3_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y1_X4_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y1_X4_RTR_BASE);

	goya_pb_set_block(hdev, mmSRAM_Y2_X0_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y2_X0_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y2_X1_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y2_X1_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y2_X2_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y2_X2_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y2_X3_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y2_X3_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y2_X4_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y2_X4_RTR_BASE);

	goya_pb_set_block(hdev, mmSRAM_Y3_X0_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y3_X0_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y3_X1_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y3_X1_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y3_X2_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y3_X2_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y3_X3_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y3_X3_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y3_X4_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y3_X4_RTR_BASE);

	goya_pb_set_block(hdev, mmSRAM_Y4_X0_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y4_X0_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y4_X1_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y4_X1_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y4_X2_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y4_X2_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y4_X3_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y4_X3_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y4_X4_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y4_X4_RTR_BASE);

	goya_pb_set_block(hdev, mmSRAM_Y5_X0_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y5_X0_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y5_X1_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y5_X1_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y5_X2_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y5_X2_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y5_X3_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y5_X3_RTR_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y5_X4_BANK_BASE);
	goya_pb_set_block(hdev, mmSRAM_Y5_X4_RTR_BASE);

	goya_pb_set_block(hdev, mmPCIE_WRAP_BASE);
	goya_pb_set_block(hdev, mmPCIE_CORE_BASE);
	goya_pb_set_block(hdev, mmPCIE_DB_CFG_BASE);
	goya_pb_set_block(hdev, mmPCIE_DB_CMD_BASE);
	goya_pb_set_block(hdev, mmPCIE_AUX_BASE);
	goya_pb_set_block(hdev, mmPCIE_DB_RSV_BASE);
	goya_pb_set_block(hdev, mmPCIE_PHY_BASE);
	goya_pb_set_block(hdev, mmTPC0_NRTR_BASE);
	goya_pb_set_block(hdev, mmTPC_PLL_BASE);

	pb_addr = (mmTPC_PLL_CLK_RLX_0 & ~0xFFF) + PROT_BITS_OFFS;
	word_offset = ((mmTPC_PLL_CLK_RLX_0 & PROT_BITS_OFFS) >> 7) << 2;
	mask = 1 << ((mmTPC_PLL_CLK_RLX_0 & 0x7C) >> 2);

	WREG32(pb_addr + word_offset, mask);

	goya_init_mme_protection_bits(hdev);

	goya_init_dma_protection_bits(hdev);

	goya_init_tpc_protection_bits(hdev);
}

/*
 * goya_init_security - Initialize security model
 *
 * @hdev: pointer to hl_device structure
 *
 * Initialize the security model of the device
 * That includes range registers and protection bit per register
 *
 */
void goya_init_security(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;

	u32 dram_addr_lo = lower_32_bits(DRAM_PHYS_BASE);
	u32 dram_addr_hi = upper_32_bits(DRAM_PHYS_BASE);

	u32 lbw_rng0_base = 0xFC440000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;
	u32 lbw_rng0_mask = 0xFFFF0000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;

	u32 lbw_rng1_base = 0xFC480000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;
	u32 lbw_rng1_mask = 0xFFF80000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;

	u32 lbw_rng2_base = 0xFC600000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;
	u32 lbw_rng2_mask = 0xFFE00000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;

	u32 lbw_rng3_base = 0xFC800000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;
	u32 lbw_rng3_mask = 0xFFF00000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;

	u32 lbw_rng4_base = 0xFCC02000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;
	u32 lbw_rng4_mask = 0xFFFFF000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;

	u32 lbw_rng5_base = 0xFCC40000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;
	u32 lbw_rng5_mask = 0xFFFF8000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;

	u32 lbw_rng6_base = 0xFCC48000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;
	u32 lbw_rng6_mask = 0xFFFFF000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;

	u32 lbw_rng7_base = 0xFCC4A000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;
	u32 lbw_rng7_mask = 0xFFFFE000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;

	u32 lbw_rng8_base = 0xFCC4C000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;
	u32 lbw_rng8_mask = 0xFFFFC000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;

	u32 lbw_rng9_base = 0xFCC50000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;
	u32 lbw_rng9_mask = 0xFFFF0000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;

	u32 lbw_rng10_base = 0xFCC60000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;
	u32 lbw_rng10_mask = 0xFFFE0000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;

	u32 lbw_rng11_base = 0xFCE02000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;
	u32 lbw_rng11_mask = 0xFFFFE000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;

	u32 lbw_rng12_base = 0xFE484000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;
	u32 lbw_rng12_mask = 0xFFFFF000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;

	u32 lbw_rng13_base = 0xFEC43000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;
	u32 lbw_rng13_mask = 0xFFFFF000 & DMA_MACRO_LBW_RANGE_BASE_R_MASK;

	WREG32(mmDMA_MACRO_LBW_RANGE_HIT_BLOCK, 0xFFFF);
	WREG32(mmDMA_MACRO_HBW_RANGE_HIT_BLOCK, 0xFF);

	if (!(goya->hw_cap_initialized & HW_CAP_MMU)) {
		WREG32(mmDMA_MACRO_HBW_RANGE_HIT_BLOCK, 0xFE);

		/* Protect HOST */
		WREG32(mmDMA_MACRO_HBW_RANGE_BASE_31_0_0, 0);
		WREG32(mmDMA_MACRO_HBW_RANGE_BASE_49_32_0, 0);
		WREG32(mmDMA_MACRO_HBW_RANGE_MASK_31_0_0, 0);
		WREG32(mmDMA_MACRO_HBW_RANGE_MASK_49_32_0, 0xFFF80);
	}

	/*
	 * Protect DDR @
	 * DRAM_VIRT_BASE : DRAM_VIRT_BASE + DRAM_VIRT_END
	 * The mask protects the first 512MB
	 */
	WREG32(mmDMA_MACRO_HBW_RANGE_BASE_31_0_1, dram_addr_lo);
	WREG32(mmDMA_MACRO_HBW_RANGE_BASE_49_32_1, dram_addr_hi);
	WREG32(mmDMA_MACRO_HBW_RANGE_MASK_31_0_1, 0xE0000000);
	WREG32(mmDMA_MACRO_HBW_RANGE_MASK_49_32_1, 0x3FFFF);

	/* Protect registers */

	WREG32(mmDMA_MACRO_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmDMA_MACRO_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmDMA_MACRO_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmDMA_MACRO_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmDMA_MACRO_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmDMA_MACRO_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmDMA_MACRO_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmDMA_MACRO_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmDMA_MACRO_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmDMA_MACRO_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmDMA_MACRO_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmDMA_MACRO_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmDMA_MACRO_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmDMA_MACRO_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmDMA_MACRO_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmDMA_MACRO_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmDMA_MACRO_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmDMA_MACRO_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmDMA_MACRO_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmDMA_MACRO_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmDMA_MACRO_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmDMA_MACRO_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmDMA_MACRO_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmDMA_MACRO_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmDMA_MACRO_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmDMA_MACRO_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmDMA_MACRO_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmDMA_MACRO_LBW_RANGE_MASK_13, lbw_rng13_mask);

	WREG32(mmMME1_RTR_LBW_RANGE_HIT, 0xFFFF);
	WREG32(mmMME2_RTR_LBW_RANGE_HIT, 0xFFFF);
	WREG32(mmMME3_RTR_LBW_RANGE_HIT, 0xFFFF);
	WREG32(mmMME4_RTR_LBW_RANGE_HIT, 0xFFFF);
	WREG32(mmMME5_RTR_LBW_RANGE_HIT, 0xFFFF);
	WREG32(mmMME6_RTR_LBW_RANGE_HIT, 0xFFFF);

	WREG32(mmMME1_RTR_HBW_RANGE_HIT, 0xFE);
	WREG32(mmMME2_RTR_HBW_RANGE_HIT, 0xFE);
	WREG32(mmMME3_RTR_HBW_RANGE_HIT, 0xFE);
	WREG32(mmMME4_RTR_HBW_RANGE_HIT, 0xFE);
	WREG32(mmMME5_RTR_HBW_RANGE_HIT, 0xFE);
	WREG32(mmMME6_RTR_HBW_RANGE_HIT, 0xFE);

	/* Protect HOST */
	WREG32(mmMME1_RTR_HBW_RANGE_BASE_L_0, 0);
	WREG32(mmMME1_RTR_HBW_RANGE_BASE_H_0, 0);
	WREG32(mmMME1_RTR_HBW_RANGE_MASK_L_0, 0);
	WREG32(mmMME1_RTR_HBW_RANGE_MASK_H_0, 0xFFF80);

	WREG32(mmMME2_RTR_HBW_RANGE_BASE_L_0, 0);
	WREG32(mmMME2_RTR_HBW_RANGE_BASE_H_0, 0);
	WREG32(mmMME2_RTR_HBW_RANGE_MASK_L_0, 0);
	WREG32(mmMME2_RTR_HBW_RANGE_MASK_H_0, 0xFFF80);

	WREG32(mmMME3_RTR_HBW_RANGE_BASE_L_0, 0);
	WREG32(mmMME3_RTR_HBW_RANGE_BASE_H_0, 0);
	WREG32(mmMME3_RTR_HBW_RANGE_MASK_L_0, 0);
	WREG32(mmMME3_RTR_HBW_RANGE_MASK_H_0, 0xFFF80);

	WREG32(mmMME4_RTR_HBW_RANGE_BASE_L_0, 0);
	WREG32(mmMME4_RTR_HBW_RANGE_BASE_H_0, 0);
	WREG32(mmMME4_RTR_HBW_RANGE_MASK_L_0, 0);
	WREG32(mmMME4_RTR_HBW_RANGE_MASK_H_0, 0xFFF80);

	WREG32(mmMME5_RTR_HBW_RANGE_BASE_L_0, 0);
	WREG32(mmMME5_RTR_HBW_RANGE_BASE_H_0, 0);
	WREG32(mmMME5_RTR_HBW_RANGE_MASK_L_0, 0);
	WREG32(mmMME5_RTR_HBW_RANGE_MASK_H_0, 0xFFF80);

	WREG32(mmMME6_RTR_HBW_RANGE_BASE_L_0, 0);
	WREG32(mmMME6_RTR_HBW_RANGE_BASE_H_0, 0);
	WREG32(mmMME6_RTR_HBW_RANGE_MASK_L_0, 0);
	WREG32(mmMME6_RTR_HBW_RANGE_MASK_H_0, 0xFFF80);

	/*
	 * Protect DDR @
	 * DRAM_VIRT_BASE : DRAM_VIRT_BASE + DRAM_VIRT_END
	 * The mask protects the first 512MB
	 */
	WREG32(mmMME1_RTR_HBW_RANGE_BASE_L_1, dram_addr_lo);
	WREG32(mmMME1_RTR_HBW_RANGE_BASE_H_1, dram_addr_hi);
	WREG32(mmMME1_RTR_HBW_RANGE_MASK_L_1, 0xE0000000);
	WREG32(mmMME1_RTR_HBW_RANGE_MASK_H_1, 0x3FFFF);

	WREG32(mmMME2_RTR_HBW_RANGE_BASE_L_1, dram_addr_lo);
	WREG32(mmMME2_RTR_HBW_RANGE_BASE_H_1, dram_addr_hi);
	WREG32(mmMME2_RTR_HBW_RANGE_MASK_L_1, 0xE0000000);
	WREG32(mmMME2_RTR_HBW_RANGE_MASK_H_1, 0x3FFFF);

	WREG32(mmMME3_RTR_HBW_RANGE_BASE_L_1, dram_addr_lo);
	WREG32(mmMME3_RTR_HBW_RANGE_BASE_H_1, dram_addr_hi);
	WREG32(mmMME3_RTR_HBW_RANGE_MASK_L_1, 0xE0000000);
	WREG32(mmMME3_RTR_HBW_RANGE_MASK_H_1, 0x3FFFF);

	WREG32(mmMME4_RTR_HBW_RANGE_BASE_L_1, dram_addr_lo);
	WREG32(mmMME4_RTR_HBW_RANGE_BASE_H_1, dram_addr_hi);
	WREG32(mmMME4_RTR_HBW_RANGE_MASK_L_1, 0xE0000000);
	WREG32(mmMME4_RTR_HBW_RANGE_MASK_H_1, 0x3FFFF);

	WREG32(mmMME5_RTR_HBW_RANGE_BASE_L_1, dram_addr_lo);
	WREG32(mmMME5_RTR_HBW_RANGE_BASE_H_1, dram_addr_hi);
	WREG32(mmMME5_RTR_HBW_RANGE_MASK_L_1, 0xE0000000);
	WREG32(mmMME5_RTR_HBW_RANGE_MASK_H_1, 0x3FFFF);

	WREG32(mmMME6_RTR_HBW_RANGE_BASE_L_1, dram_addr_lo);
	WREG32(mmMME6_RTR_HBW_RANGE_BASE_H_1, dram_addr_hi);
	WREG32(mmMME6_RTR_HBW_RANGE_MASK_L_1, 0xE0000000);
	WREG32(mmMME6_RTR_HBW_RANGE_MASK_H_1, 0x3FFFF);

	WREG32(mmMME1_RTR_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmMME1_RTR_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmMME1_RTR_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmMME1_RTR_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmMME1_RTR_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmMME1_RTR_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmMME1_RTR_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmMME1_RTR_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmMME1_RTR_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmMME1_RTR_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmMME1_RTR_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmMME1_RTR_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmMME1_RTR_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmMME1_RTR_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmMME1_RTR_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmMME1_RTR_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmMME1_RTR_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmMME1_RTR_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmMME1_RTR_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmMME1_RTR_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmMME1_RTR_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmMME1_RTR_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmMME1_RTR_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmMME1_RTR_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmMME1_RTR_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmMME1_RTR_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmMME1_RTR_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmMME1_RTR_LBW_RANGE_MASK_13, lbw_rng13_mask);

	WREG32(mmMME2_RTR_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmMME2_RTR_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmMME2_RTR_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmMME2_RTR_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmMME2_RTR_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmMME2_RTR_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmMME2_RTR_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmMME2_RTR_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmMME2_RTR_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmMME2_RTR_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmMME2_RTR_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmMME2_RTR_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmMME2_RTR_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmMME2_RTR_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmMME2_RTR_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmMME2_RTR_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmMME2_RTR_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmMME2_RTR_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmMME2_RTR_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmMME2_RTR_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmMME2_RTR_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmMME2_RTR_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmMME2_RTR_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmMME2_RTR_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmMME2_RTR_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmMME2_RTR_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmMME2_RTR_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmMME2_RTR_LBW_RANGE_MASK_13, lbw_rng13_mask);

	WREG32(mmMME3_RTR_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmMME3_RTR_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmMME3_RTR_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmMME3_RTR_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmMME3_RTR_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmMME3_RTR_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmMME3_RTR_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmMME3_RTR_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmMME3_RTR_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmMME3_RTR_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmMME3_RTR_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmMME3_RTR_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmMME3_RTR_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmMME3_RTR_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmMME3_RTR_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmMME3_RTR_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmMME3_RTR_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmMME3_RTR_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmMME3_RTR_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmMME3_RTR_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmMME3_RTR_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmMME3_RTR_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmMME3_RTR_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmMME3_RTR_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmMME3_RTR_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmMME3_RTR_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmMME3_RTR_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmMME3_RTR_LBW_RANGE_MASK_13, lbw_rng13_mask);

	WREG32(mmMME4_RTR_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmMME4_RTR_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmMME4_RTR_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmMME4_RTR_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmMME4_RTR_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmMME4_RTR_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmMME4_RTR_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmMME4_RTR_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmMME4_RTR_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmMME4_RTR_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmMME4_RTR_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmMME4_RTR_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmMME4_RTR_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmMME4_RTR_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmMME4_RTR_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmMME4_RTR_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmMME4_RTR_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmMME4_RTR_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmMME4_RTR_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmMME4_RTR_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmMME4_RTR_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmMME4_RTR_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmMME4_RTR_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmMME4_RTR_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmMME4_RTR_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmMME4_RTR_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmMME4_RTR_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmMME4_RTR_LBW_RANGE_MASK_13, lbw_rng13_mask);

	WREG32(mmMME5_RTR_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmMME5_RTR_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmMME5_RTR_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmMME5_RTR_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmMME5_RTR_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmMME5_RTR_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmMME5_RTR_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmMME5_RTR_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmMME5_RTR_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmMME5_RTR_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmMME5_RTR_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmMME5_RTR_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmMME5_RTR_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmMME5_RTR_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmMME5_RTR_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmMME5_RTR_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmMME5_RTR_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmMME5_RTR_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmMME5_RTR_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmMME5_RTR_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmMME5_RTR_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmMME5_RTR_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmMME5_RTR_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmMME5_RTR_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmMME5_RTR_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmMME5_RTR_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmMME5_RTR_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmMME5_RTR_LBW_RANGE_MASK_13, lbw_rng13_mask);

	WREG32(mmMME6_RTR_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmMME6_RTR_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmMME6_RTR_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmMME6_RTR_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmMME6_RTR_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmMME6_RTR_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmMME6_RTR_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmMME6_RTR_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmMME6_RTR_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmMME6_RTR_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmMME6_RTR_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmMME6_RTR_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmMME6_RTR_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmMME6_RTR_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmMME6_RTR_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmMME6_RTR_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmMME6_RTR_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmMME6_RTR_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmMME6_RTR_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmMME6_RTR_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmMME6_RTR_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmMME6_RTR_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmMME6_RTR_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmMME6_RTR_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmMME6_RTR_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmMME6_RTR_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmMME6_RTR_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmMME6_RTR_LBW_RANGE_MASK_13, lbw_rng13_mask);

	WREG32(mmTPC0_NRTR_LBW_RANGE_HIT, 0xFFFF);
	WREG32(mmTPC0_NRTR_HBW_RANGE_HIT, 0xFE);

	/* Protect HOST */
	WREG32(mmTPC0_NRTR_HBW_RANGE_BASE_L_0, 0);
	WREG32(mmTPC0_NRTR_HBW_RANGE_BASE_H_0, 0);
	WREG32(mmTPC0_NRTR_HBW_RANGE_MASK_L_0, 0);
	WREG32(mmTPC0_NRTR_HBW_RANGE_MASK_H_0, 0xFFF80);

	/*
	 * Protect DDR @
	 * DRAM_VIRT_BASE : DRAM_VIRT_BASE + DRAM_VIRT_END
	 * The mask protects the first 512MB
	 */
	WREG32(mmTPC0_NRTR_HBW_RANGE_BASE_L_1, dram_addr_lo);
	WREG32(mmTPC0_NRTR_HBW_RANGE_BASE_H_1, dram_addr_hi);
	WREG32(mmTPC0_NRTR_HBW_RANGE_MASK_L_1, 0xE0000000);
	WREG32(mmTPC0_NRTR_HBW_RANGE_MASK_H_1, 0x3FFFF);

	WREG32(mmTPC0_NRTR_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmTPC0_NRTR_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmTPC0_NRTR_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmTPC0_NRTR_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmTPC0_NRTR_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmTPC0_NRTR_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmTPC0_NRTR_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmTPC0_NRTR_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmTPC0_NRTR_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmTPC0_NRTR_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmTPC0_NRTR_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmTPC0_NRTR_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmTPC0_NRTR_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmTPC0_NRTR_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmTPC0_NRTR_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmTPC0_NRTR_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmTPC0_NRTR_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmTPC0_NRTR_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmTPC0_NRTR_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmTPC0_NRTR_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmTPC0_NRTR_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmTPC0_NRTR_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmTPC0_NRTR_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmTPC0_NRTR_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmTPC0_NRTR_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmTPC0_NRTR_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmTPC0_NRTR_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmTPC0_NRTR_LBW_RANGE_MASK_13, lbw_rng13_mask);

	WREG32(mmTPC1_RTR_LBW_RANGE_HIT, 0xFFFF);
	WREG32(mmTPC1_RTR_HBW_RANGE_HIT, 0xFE);

	/* Protect HOST */
	WREG32(mmTPC1_RTR_HBW_RANGE_BASE_L_0, 0);
	WREG32(mmTPC1_RTR_HBW_RANGE_BASE_H_0, 0);
	WREG32(mmTPC1_RTR_HBW_RANGE_MASK_L_0, 0);
	WREG32(mmTPC1_RTR_HBW_RANGE_MASK_H_0, 0xFFF80);

	/*
	 * Protect DDR @
	 * DRAM_VIRT_BASE : DRAM_VIRT_BASE + DRAM_VIRT_END
	 * The mask protects the first 512MB
	 */
	WREG32(mmTPC1_RTR_HBW_RANGE_BASE_L_1, dram_addr_lo);
	WREG32(mmTPC1_RTR_HBW_RANGE_BASE_H_1, dram_addr_hi);
	WREG32(mmTPC1_RTR_HBW_RANGE_MASK_L_1, 0xE0000000);
	WREG32(mmTPC1_RTR_HBW_RANGE_MASK_H_1, 0x3FFFF);

	WREG32(mmTPC1_RTR_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmTPC1_RTR_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmTPC1_RTR_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmTPC1_RTR_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmTPC1_RTR_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmTPC1_RTR_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmTPC1_RTR_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmTPC1_RTR_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmTPC1_RTR_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmTPC1_RTR_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmTPC1_RTR_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmTPC1_RTR_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmTPC1_RTR_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmTPC1_RTR_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmTPC1_RTR_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmTPC1_RTR_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmTPC1_RTR_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmTPC1_RTR_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmTPC1_RTR_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmTPC1_RTR_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmTPC1_RTR_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmTPC1_RTR_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmTPC1_RTR_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmTPC1_RTR_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmTPC1_RTR_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmTPC1_RTR_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmTPC1_RTR_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmTPC1_RTR_LBW_RANGE_MASK_13, lbw_rng13_mask);

	WREG32(mmTPC2_RTR_LBW_RANGE_HIT, 0xFFFF);
	WREG32(mmTPC2_RTR_HBW_RANGE_HIT, 0xFE);

	/* Protect HOST */
	WREG32(mmTPC2_RTR_HBW_RANGE_BASE_L_0, 0);
	WREG32(mmTPC2_RTR_HBW_RANGE_BASE_H_0, 0);
	WREG32(mmTPC2_RTR_HBW_RANGE_MASK_L_0, 0);
	WREG32(mmTPC2_RTR_HBW_RANGE_MASK_H_0, 0xFFF80);

	/*
	 * Protect DDR @
	 * DRAM_VIRT_BASE : DRAM_VIRT_BASE + DRAM_VIRT_END
	 * The mask protects the first 512MB
	 */
	WREG32(mmTPC2_RTR_HBW_RANGE_BASE_L_1, dram_addr_lo);
	WREG32(mmTPC2_RTR_HBW_RANGE_BASE_H_1, dram_addr_hi);
	WREG32(mmTPC2_RTR_HBW_RANGE_MASK_L_1, 0xE0000000);
	WREG32(mmTPC2_RTR_HBW_RANGE_MASK_H_1, 0x3FFFF);

	WREG32(mmTPC2_RTR_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmTPC2_RTR_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmTPC2_RTR_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmTPC2_RTR_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmTPC2_RTR_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmTPC2_RTR_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmTPC2_RTR_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmTPC2_RTR_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmTPC2_RTR_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmTPC2_RTR_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmTPC2_RTR_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmTPC2_RTR_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmTPC2_RTR_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmTPC2_RTR_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmTPC2_RTR_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmTPC2_RTR_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmTPC2_RTR_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmTPC2_RTR_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmTPC2_RTR_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmTPC2_RTR_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmTPC2_RTR_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmTPC2_RTR_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmTPC2_RTR_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmTPC2_RTR_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmTPC2_RTR_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmTPC2_RTR_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmTPC2_RTR_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmTPC2_RTR_LBW_RANGE_MASK_13, lbw_rng13_mask);

	WREG32(mmTPC3_RTR_LBW_RANGE_HIT, 0xFFFF);
	WREG32(mmTPC3_RTR_HBW_RANGE_HIT, 0xFE);

	/* Protect HOST */
	WREG32(mmTPC3_RTR_HBW_RANGE_BASE_L_0, 0);
	WREG32(mmTPC3_RTR_HBW_RANGE_BASE_H_0, 0);
	WREG32(mmTPC3_RTR_HBW_RANGE_MASK_L_0, 0);
	WREG32(mmTPC3_RTR_HBW_RANGE_MASK_H_0, 0xFFF80);

	/*
	 * Protect DDR @
	 * DRAM_VIRT_BASE : DRAM_VIRT_BASE + DRAM_VIRT_END
	 * The mask protects the first 512MB
	 */
	WREG32(mmTPC3_RTR_HBW_RANGE_BASE_L_1, dram_addr_lo);
	WREG32(mmTPC3_RTR_HBW_RANGE_BASE_H_1, dram_addr_hi);
	WREG32(mmTPC3_RTR_HBW_RANGE_MASK_L_1, 0xE0000000);
	WREG32(mmTPC3_RTR_HBW_RANGE_MASK_H_1, 0x3FFFF);

	WREG32(mmTPC3_RTR_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmTPC3_RTR_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmTPC3_RTR_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmTPC3_RTR_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmTPC3_RTR_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmTPC3_RTR_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmTPC3_RTR_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmTPC3_RTR_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmTPC3_RTR_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmTPC3_RTR_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmTPC3_RTR_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmTPC3_RTR_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmTPC3_RTR_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmTPC3_RTR_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmTPC3_RTR_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmTPC3_RTR_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmTPC3_RTR_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmTPC3_RTR_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmTPC3_RTR_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmTPC3_RTR_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmTPC3_RTR_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmTPC3_RTR_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmTPC3_RTR_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmTPC3_RTR_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmTPC3_RTR_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmTPC3_RTR_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmTPC3_RTR_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmTPC3_RTR_LBW_RANGE_MASK_13, lbw_rng13_mask);

	WREG32(mmTPC4_RTR_LBW_RANGE_HIT, 0xFFFF);
	WREG32(mmTPC4_RTR_HBW_RANGE_HIT, 0xFE);

	/* Protect HOST */
	WREG32(mmTPC4_RTR_HBW_RANGE_BASE_L_0, 0);
	WREG32(mmTPC4_RTR_HBW_RANGE_BASE_H_0, 0);
	WREG32(mmTPC4_RTR_HBW_RANGE_MASK_L_0, 0);
	WREG32(mmTPC4_RTR_HBW_RANGE_MASK_H_0, 0xFFF80);

	/*
	 * Protect DDR @
	 * DRAM_VIRT_BASE : DRAM_VIRT_BASE + DRAM_VIRT_END
	 * The mask protects the first 512MB
	 */
	WREG32(mmTPC4_RTR_HBW_RANGE_BASE_L_1, dram_addr_lo);
	WREG32(mmTPC4_RTR_HBW_RANGE_BASE_H_1, dram_addr_hi);
	WREG32(mmTPC4_RTR_HBW_RANGE_MASK_L_1, 0xE0000000);
	WREG32(mmTPC4_RTR_HBW_RANGE_MASK_H_1, 0x3FFFF);

	WREG32(mmTPC4_RTR_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmTPC4_RTR_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmTPC4_RTR_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmTPC4_RTR_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmTPC4_RTR_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmTPC4_RTR_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmTPC4_RTR_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmTPC4_RTR_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmTPC4_RTR_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmTPC4_RTR_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmTPC4_RTR_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmTPC4_RTR_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmTPC4_RTR_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmTPC4_RTR_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmTPC4_RTR_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmTPC4_RTR_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmTPC4_RTR_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmTPC4_RTR_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmTPC4_RTR_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmTPC4_RTR_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmTPC4_RTR_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmTPC4_RTR_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmTPC4_RTR_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmTPC4_RTR_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmTPC4_RTR_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmTPC4_RTR_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmTPC4_RTR_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmTPC4_RTR_LBW_RANGE_MASK_13, lbw_rng13_mask);

	WREG32(mmTPC5_RTR_LBW_RANGE_HIT, 0xFFFF);
	WREG32(mmTPC5_RTR_HBW_RANGE_HIT, 0xFE);

	/* Protect HOST */
	WREG32(mmTPC5_RTR_HBW_RANGE_BASE_L_0, 0);
	WREG32(mmTPC5_RTR_HBW_RANGE_BASE_H_0, 0);
	WREG32(mmTPC5_RTR_HBW_RANGE_MASK_L_0, 0);
	WREG32(mmTPC5_RTR_HBW_RANGE_MASK_H_0, 0xFFF80);

	/*
	 * Protect DDR @
	 * DRAM_VIRT_BASE : DRAM_VIRT_BASE + DRAM_VIRT_END
	 * The mask protects the first 512MB
	 */
	WREG32(mmTPC5_RTR_HBW_RANGE_BASE_L_1, dram_addr_lo);
	WREG32(mmTPC5_RTR_HBW_RANGE_BASE_H_1, dram_addr_hi);
	WREG32(mmTPC5_RTR_HBW_RANGE_MASK_L_1, 0xE0000000);
	WREG32(mmTPC5_RTR_HBW_RANGE_MASK_H_1, 0x3FFFF);

	WREG32(mmTPC5_RTR_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmTPC5_RTR_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmTPC5_RTR_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmTPC5_RTR_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmTPC5_RTR_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmTPC5_RTR_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmTPC5_RTR_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmTPC5_RTR_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmTPC5_RTR_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmTPC5_RTR_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmTPC5_RTR_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmTPC5_RTR_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmTPC5_RTR_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmTPC5_RTR_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmTPC5_RTR_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmTPC5_RTR_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmTPC5_RTR_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmTPC5_RTR_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmTPC5_RTR_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmTPC5_RTR_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmTPC5_RTR_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmTPC5_RTR_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmTPC5_RTR_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmTPC5_RTR_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmTPC5_RTR_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmTPC5_RTR_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmTPC5_RTR_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmTPC5_RTR_LBW_RANGE_MASK_13, lbw_rng13_mask);

	WREG32(mmTPC6_RTR_LBW_RANGE_HIT, 0xFFFF);
	WREG32(mmTPC6_RTR_HBW_RANGE_HIT, 0xFE);

	/* Protect HOST */
	WREG32(mmTPC6_RTR_HBW_RANGE_BASE_L_0, 0);
	WREG32(mmTPC6_RTR_HBW_RANGE_BASE_H_0, 0);
	WREG32(mmTPC6_RTR_HBW_RANGE_MASK_L_0, 0);
	WREG32(mmTPC6_RTR_HBW_RANGE_MASK_H_0, 0xFFF80);

	/*
	 * Protect DDR @
	 * DRAM_VIRT_BASE : DRAM_VIRT_BASE + DRAM_VIRT_END
	 * The mask protects the first 512MB
	 */
	WREG32(mmTPC6_RTR_HBW_RANGE_BASE_L_1, dram_addr_lo);
	WREG32(mmTPC6_RTR_HBW_RANGE_BASE_H_1, dram_addr_hi);
	WREG32(mmTPC6_RTR_HBW_RANGE_MASK_L_1, 0xE0000000);
	WREG32(mmTPC6_RTR_HBW_RANGE_MASK_H_1, 0x3FFFF);

	WREG32(mmTPC6_RTR_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmTPC6_RTR_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmTPC6_RTR_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmTPC6_RTR_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmTPC6_RTR_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmTPC6_RTR_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmTPC6_RTR_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmTPC6_RTR_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmTPC6_RTR_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmTPC6_RTR_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmTPC6_RTR_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmTPC6_RTR_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmTPC6_RTR_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmTPC6_RTR_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmTPC6_RTR_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmTPC6_RTR_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmTPC6_RTR_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmTPC6_RTR_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmTPC6_RTR_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmTPC6_RTR_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmTPC6_RTR_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmTPC6_RTR_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmTPC6_RTR_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmTPC6_RTR_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmTPC6_RTR_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmTPC6_RTR_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmTPC6_RTR_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmTPC6_RTR_LBW_RANGE_MASK_13, lbw_rng13_mask);

	WREG32(mmTPC7_NRTR_LBW_RANGE_HIT, 0xFFFF);
	WREG32(mmTPC7_NRTR_HBW_RANGE_HIT, 0xFE);

	/* Protect HOST */
	WREG32(mmTPC7_NRTR_HBW_RANGE_BASE_L_0, 0);
	WREG32(mmTPC7_NRTR_HBW_RANGE_BASE_H_0, 0);
	WREG32(mmTPC7_NRTR_HBW_RANGE_MASK_L_0, 0);
	WREG32(mmTPC7_NRTR_HBW_RANGE_MASK_H_0, 0xFFF80);

	/*
	 * Protect DDR @
	 * DRAM_VIRT_BASE : DRAM_VIRT_BASE + DRAM_VIRT_END
	 * The mask protects the first 512MB
	 */
	WREG32(mmTPC7_NRTR_HBW_RANGE_BASE_L_1, dram_addr_lo);
	WREG32(mmTPC7_NRTR_HBW_RANGE_BASE_H_1, dram_addr_hi);
	WREG32(mmTPC7_NRTR_HBW_RANGE_MASK_L_1, 0xE0000000);
	WREG32(mmTPC7_NRTR_HBW_RANGE_MASK_H_1, 0x3FFFF);

	WREG32(mmTPC7_NRTR_LBW_RANGE_BASE_0, lbw_rng0_base);
	WREG32(mmTPC7_NRTR_LBW_RANGE_MASK_0, lbw_rng0_mask);
	WREG32(mmTPC7_NRTR_LBW_RANGE_BASE_1, lbw_rng1_base);
	WREG32(mmTPC7_NRTR_LBW_RANGE_MASK_1, lbw_rng1_mask);
	WREG32(mmTPC7_NRTR_LBW_RANGE_BASE_2, lbw_rng2_base);
	WREG32(mmTPC7_NRTR_LBW_RANGE_MASK_2, lbw_rng2_mask);
	WREG32(mmTPC7_NRTR_LBW_RANGE_BASE_3, lbw_rng3_base);
	WREG32(mmTPC7_NRTR_LBW_RANGE_MASK_3, lbw_rng3_mask);
	WREG32(mmTPC7_NRTR_LBW_RANGE_BASE_4, lbw_rng4_base);
	WREG32(mmTPC7_NRTR_LBW_RANGE_MASK_4, lbw_rng4_mask);
	WREG32(mmTPC7_NRTR_LBW_RANGE_BASE_5, lbw_rng5_base);
	WREG32(mmTPC7_NRTR_LBW_RANGE_MASK_5, lbw_rng5_mask);
	WREG32(mmTPC7_NRTR_LBW_RANGE_BASE_6, lbw_rng6_base);
	WREG32(mmTPC7_NRTR_LBW_RANGE_MASK_6, lbw_rng6_mask);
	WREG32(mmTPC7_NRTR_LBW_RANGE_BASE_7, lbw_rng7_base);
	WREG32(mmTPC7_NRTR_LBW_RANGE_MASK_7, lbw_rng7_mask);
	WREG32(mmTPC7_NRTR_LBW_RANGE_BASE_8, lbw_rng8_base);
	WREG32(mmTPC7_NRTR_LBW_RANGE_MASK_8, lbw_rng8_mask);
	WREG32(mmTPC7_NRTR_LBW_RANGE_BASE_9, lbw_rng9_base);
	WREG32(mmTPC7_NRTR_LBW_RANGE_MASK_9, lbw_rng9_mask);
	WREG32(mmTPC7_NRTR_LBW_RANGE_BASE_10, lbw_rng10_base);
	WREG32(mmTPC7_NRTR_LBW_RANGE_MASK_10, lbw_rng10_mask);
	WREG32(mmTPC7_NRTR_LBW_RANGE_BASE_11, lbw_rng11_base);
	WREG32(mmTPC7_NRTR_LBW_RANGE_MASK_11, lbw_rng11_mask);
	WREG32(mmTPC7_NRTR_LBW_RANGE_BASE_12, lbw_rng12_base);
	WREG32(mmTPC7_NRTR_LBW_RANGE_MASK_12, lbw_rng12_mask);
	WREG32(mmTPC7_NRTR_LBW_RANGE_BASE_13, lbw_rng13_base);
	WREG32(mmTPC7_NRTR_LBW_RANGE_MASK_13, lbw_rng13_mask);

	goya_init_protection_bits(hdev);
}

void goya_ack_protection_bits_errors(struct hl_device *hdev)
{

}
