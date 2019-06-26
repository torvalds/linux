// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "goyaP.h"
#include "include/goya/goya_coresight.h"
#include "include/goya/asic_reg/goya_regs.h"

#include <uapi/misc/habanalabs.h>

#include <linux/coresight.h>

#define GOYA_PLDM_CORESIGHT_TIMEOUT_USEC	(CORESIGHT_TIMEOUT_USEC * 100)

static u64 debug_stm_regs[GOYA_STM_LAST + 1] = {
	[GOYA_STM_CPU]		= mmCPU_STM_BASE,
	[GOYA_STM_DMA_CH_0_CS]	= mmDMA_CH_0_CS_STM_BASE,
	[GOYA_STM_DMA_CH_1_CS]	= mmDMA_CH_1_CS_STM_BASE,
	[GOYA_STM_DMA_CH_2_CS]	= mmDMA_CH_2_CS_STM_BASE,
	[GOYA_STM_DMA_CH_3_CS]	= mmDMA_CH_3_CS_STM_BASE,
	[GOYA_STM_DMA_CH_4_CS]	= mmDMA_CH_4_CS_STM_BASE,
	[GOYA_STM_DMA_MACRO_CS]	= mmDMA_MACRO_CS_STM_BASE,
	[GOYA_STM_MME1_SBA]	= mmMME1_SBA_STM_BASE,
	[GOYA_STM_MME3_SBB]	= mmMME3_SBB_STM_BASE,
	[GOYA_STM_MME4_WACS2]	= mmMME4_WACS2_STM_BASE,
	[GOYA_STM_MME4_WACS]	= mmMME4_WACS_STM_BASE,
	[GOYA_STM_MMU_CS]	= mmMMU_CS_STM_BASE,
	[GOYA_STM_PCIE]		= mmPCIE_STM_BASE,
	[GOYA_STM_PSOC]		= mmPSOC_STM_BASE,
	[GOYA_STM_TPC0_EML]	= mmTPC0_EML_STM_BASE,
	[GOYA_STM_TPC1_EML]	= mmTPC1_EML_STM_BASE,
	[GOYA_STM_TPC2_EML]	= mmTPC2_EML_STM_BASE,
	[GOYA_STM_TPC3_EML]	= mmTPC3_EML_STM_BASE,
	[GOYA_STM_TPC4_EML]	= mmTPC4_EML_STM_BASE,
	[GOYA_STM_TPC5_EML]	= mmTPC5_EML_STM_BASE,
	[GOYA_STM_TPC6_EML]	= mmTPC6_EML_STM_BASE,
	[GOYA_STM_TPC7_EML]	= mmTPC7_EML_STM_BASE
};

static u64 debug_etf_regs[GOYA_ETF_LAST + 1] = {
	[GOYA_ETF_CPU_0]	= mmCPU_ETF_0_BASE,
	[GOYA_ETF_CPU_1]	= mmCPU_ETF_1_BASE,
	[GOYA_ETF_CPU_TRACE]	= mmCPU_ETF_TRACE_BASE,
	[GOYA_ETF_DMA_CH_0_CS]	= mmDMA_CH_0_CS_ETF_BASE,
	[GOYA_ETF_DMA_CH_1_CS]	= mmDMA_CH_1_CS_ETF_BASE,
	[GOYA_ETF_DMA_CH_2_CS]	= mmDMA_CH_2_CS_ETF_BASE,
	[GOYA_ETF_DMA_CH_3_CS]	= mmDMA_CH_3_CS_ETF_BASE,
	[GOYA_ETF_DMA_CH_4_CS]	= mmDMA_CH_4_CS_ETF_BASE,
	[GOYA_ETF_DMA_MACRO_CS]	= mmDMA_MACRO_CS_ETF_BASE,
	[GOYA_ETF_MME1_SBA]	= mmMME1_SBA_ETF_BASE,
	[GOYA_ETF_MME3_SBB]	= mmMME3_SBB_ETF_BASE,
	[GOYA_ETF_MME4_WACS2]	= mmMME4_WACS2_ETF_BASE,
	[GOYA_ETF_MME4_WACS]	= mmMME4_WACS_ETF_BASE,
	[GOYA_ETF_MMU_CS]	= mmMMU_CS_ETF_BASE,
	[GOYA_ETF_PCIE]		= mmPCIE_ETF_BASE,
	[GOYA_ETF_PSOC]		= mmPSOC_ETF_BASE,
	[GOYA_ETF_TPC0_EML]	= mmTPC0_EML_ETF_BASE,
	[GOYA_ETF_TPC1_EML]	= mmTPC1_EML_ETF_BASE,
	[GOYA_ETF_TPC2_EML]	= mmTPC2_EML_ETF_BASE,
	[GOYA_ETF_TPC3_EML]	= mmTPC3_EML_ETF_BASE,
	[GOYA_ETF_TPC4_EML]	= mmTPC4_EML_ETF_BASE,
	[GOYA_ETF_TPC5_EML]	= mmTPC5_EML_ETF_BASE,
	[GOYA_ETF_TPC6_EML]	= mmTPC6_EML_ETF_BASE,
	[GOYA_ETF_TPC7_EML]	= mmTPC7_EML_ETF_BASE
};

static u64 debug_funnel_regs[GOYA_FUNNEL_LAST + 1] = {
	[GOYA_FUNNEL_CPU]		= mmCPU_FUNNEL_BASE,
	[GOYA_FUNNEL_DMA_CH_6_1]	= mmDMA_CH_FUNNEL_6_1_BASE,
	[GOYA_FUNNEL_DMA_MACRO_3_1]	= mmDMA_MACRO_FUNNEL_3_1_BASE,
	[GOYA_FUNNEL_MME0_RTR]		= mmMME0_RTR_FUNNEL_BASE,
	[GOYA_FUNNEL_MME1_RTR]		= mmMME1_RTR_FUNNEL_BASE,
	[GOYA_FUNNEL_MME2_RTR]		= mmMME2_RTR_FUNNEL_BASE,
	[GOYA_FUNNEL_MME3_RTR]		= mmMME3_RTR_FUNNEL_BASE,
	[GOYA_FUNNEL_MME4_RTR]		= mmMME4_RTR_FUNNEL_BASE,
	[GOYA_FUNNEL_MME5_RTR]		= mmMME5_RTR_FUNNEL_BASE,
	[GOYA_FUNNEL_PCIE]		= mmPCIE_FUNNEL_BASE,
	[GOYA_FUNNEL_PSOC]		= mmPSOC_FUNNEL_BASE,
	[GOYA_FUNNEL_TPC0_EML]		= mmTPC0_EML_FUNNEL_BASE,
	[GOYA_FUNNEL_TPC1_EML]		= mmTPC1_EML_FUNNEL_BASE,
	[GOYA_FUNNEL_TPC1_RTR]		= mmTPC1_RTR_FUNNEL_BASE,
	[GOYA_FUNNEL_TPC2_EML]		= mmTPC2_EML_FUNNEL_BASE,
	[GOYA_FUNNEL_TPC2_RTR]		= mmTPC2_RTR_FUNNEL_BASE,
	[GOYA_FUNNEL_TPC3_EML]		= mmTPC3_EML_FUNNEL_BASE,
	[GOYA_FUNNEL_TPC3_RTR]		= mmTPC3_RTR_FUNNEL_BASE,
	[GOYA_FUNNEL_TPC4_EML]		= mmTPC4_EML_FUNNEL_BASE,
	[GOYA_FUNNEL_TPC4_RTR]		= mmTPC4_RTR_FUNNEL_BASE,
	[GOYA_FUNNEL_TPC5_EML]		= mmTPC5_EML_FUNNEL_BASE,
	[GOYA_FUNNEL_TPC5_RTR]		= mmTPC5_RTR_FUNNEL_BASE,
	[GOYA_FUNNEL_TPC6_EML]		= mmTPC6_EML_FUNNEL_BASE,
	[GOYA_FUNNEL_TPC6_RTR]		= mmTPC6_RTR_FUNNEL_BASE,
	[GOYA_FUNNEL_TPC7_EML]		= mmTPC7_EML_FUNNEL_BASE
};

static u64 debug_bmon_regs[GOYA_BMON_LAST + 1] = {
	[GOYA_BMON_CPU_RD]		= mmCPU_RD_BMON_BASE,
	[GOYA_BMON_CPU_WR]		= mmCPU_WR_BMON_BASE,
	[GOYA_BMON_DMA_CH_0_0]		= mmDMA_CH_0_BMON_0_BASE,
	[GOYA_BMON_DMA_CH_0_1]		= mmDMA_CH_0_BMON_1_BASE,
	[GOYA_BMON_DMA_CH_1_0]		= mmDMA_CH_1_BMON_0_BASE,
	[GOYA_BMON_DMA_CH_1_1]		= mmDMA_CH_1_BMON_1_BASE,
	[GOYA_BMON_DMA_CH_2_0]		= mmDMA_CH_2_BMON_0_BASE,
	[GOYA_BMON_DMA_CH_2_1]		= mmDMA_CH_2_BMON_1_BASE,
	[GOYA_BMON_DMA_CH_3_0]		= mmDMA_CH_3_BMON_0_BASE,
	[GOYA_BMON_DMA_CH_3_1]		= mmDMA_CH_3_BMON_1_BASE,
	[GOYA_BMON_DMA_CH_4_0]		= mmDMA_CH_4_BMON_0_BASE,
	[GOYA_BMON_DMA_CH_4_1]		= mmDMA_CH_4_BMON_1_BASE,
	[GOYA_BMON_DMA_MACRO_0]		= mmDMA_MACRO_BMON_0_BASE,
	[GOYA_BMON_DMA_MACRO_1]		= mmDMA_MACRO_BMON_1_BASE,
	[GOYA_BMON_DMA_MACRO_2]		= mmDMA_MACRO_BMON_2_BASE,
	[GOYA_BMON_DMA_MACRO_3]		= mmDMA_MACRO_BMON_3_BASE,
	[GOYA_BMON_DMA_MACRO_4]		= mmDMA_MACRO_BMON_4_BASE,
	[GOYA_BMON_DMA_MACRO_5]		= mmDMA_MACRO_BMON_5_BASE,
	[GOYA_BMON_DMA_MACRO_6]		= mmDMA_MACRO_BMON_6_BASE,
	[GOYA_BMON_DMA_MACRO_7]		= mmDMA_MACRO_BMON_7_BASE,
	[GOYA_BMON_MME1_SBA_0]		= mmMME1_SBA_BMON0_BASE,
	[GOYA_BMON_MME1_SBA_1]		= mmMME1_SBA_BMON1_BASE,
	[GOYA_BMON_MME3_SBB_0]		= mmMME3_SBB_BMON0_BASE,
	[GOYA_BMON_MME3_SBB_1]		= mmMME3_SBB_BMON1_BASE,
	[GOYA_BMON_MME4_WACS2_0]	= mmMME4_WACS2_BMON0_BASE,
	[GOYA_BMON_MME4_WACS2_1]	= mmMME4_WACS2_BMON1_BASE,
	[GOYA_BMON_MME4_WACS2_2]	= mmMME4_WACS2_BMON2_BASE,
	[GOYA_BMON_MME4_WACS_0]		= mmMME4_WACS_BMON0_BASE,
	[GOYA_BMON_MME4_WACS_1]		= mmMME4_WACS_BMON1_BASE,
	[GOYA_BMON_MME4_WACS_2]		= mmMME4_WACS_BMON2_BASE,
	[GOYA_BMON_MME4_WACS_3]		= mmMME4_WACS_BMON3_BASE,
	[GOYA_BMON_MME4_WACS_4]		= mmMME4_WACS_BMON4_BASE,
	[GOYA_BMON_MME4_WACS_5]		= mmMME4_WACS_BMON5_BASE,
	[GOYA_BMON_MME4_WACS_6]		= mmMME4_WACS_BMON6_BASE,
	[GOYA_BMON_MMU_0]		= mmMMU_BMON_0_BASE,
	[GOYA_BMON_MMU_1]		= mmMMU_BMON_1_BASE,
	[GOYA_BMON_PCIE_MSTR_RD]	= mmPCIE_BMON_MSTR_RD_BASE,
	[GOYA_BMON_PCIE_MSTR_WR]	= mmPCIE_BMON_MSTR_WR_BASE,
	[GOYA_BMON_PCIE_SLV_RD]		= mmPCIE_BMON_SLV_RD_BASE,
	[GOYA_BMON_PCIE_SLV_WR]		= mmPCIE_BMON_SLV_WR_BASE,
	[GOYA_BMON_TPC0_EML_0]		= mmTPC0_EML_BUSMON_0_BASE,
	[GOYA_BMON_TPC0_EML_1]		= mmTPC0_EML_BUSMON_1_BASE,
	[GOYA_BMON_TPC0_EML_2]		= mmTPC0_EML_BUSMON_2_BASE,
	[GOYA_BMON_TPC0_EML_3]		= mmTPC0_EML_BUSMON_3_BASE,
	[GOYA_BMON_TPC1_EML_0]		= mmTPC1_EML_BUSMON_0_BASE,
	[GOYA_BMON_TPC1_EML_1]		= mmTPC1_EML_BUSMON_1_BASE,
	[GOYA_BMON_TPC1_EML_2]		= mmTPC1_EML_BUSMON_2_BASE,
	[GOYA_BMON_TPC1_EML_3]		= mmTPC1_EML_BUSMON_3_BASE,
	[GOYA_BMON_TPC2_EML_0]		= mmTPC2_EML_BUSMON_0_BASE,
	[GOYA_BMON_TPC2_EML_1]		= mmTPC2_EML_BUSMON_1_BASE,
	[GOYA_BMON_TPC2_EML_2]		= mmTPC2_EML_BUSMON_2_BASE,
	[GOYA_BMON_TPC2_EML_3]		= mmTPC2_EML_BUSMON_3_BASE,
	[GOYA_BMON_TPC3_EML_0]		= mmTPC3_EML_BUSMON_0_BASE,
	[GOYA_BMON_TPC3_EML_1]		= mmTPC3_EML_BUSMON_1_BASE,
	[GOYA_BMON_TPC3_EML_2]		= mmTPC3_EML_BUSMON_2_BASE,
	[GOYA_BMON_TPC3_EML_3]		= mmTPC3_EML_BUSMON_3_BASE,
	[GOYA_BMON_TPC4_EML_0]		= mmTPC4_EML_BUSMON_0_BASE,
	[GOYA_BMON_TPC4_EML_1]		= mmTPC4_EML_BUSMON_1_BASE,
	[GOYA_BMON_TPC4_EML_2]		= mmTPC4_EML_BUSMON_2_BASE,
	[GOYA_BMON_TPC4_EML_3]		= mmTPC4_EML_BUSMON_3_BASE,
	[GOYA_BMON_TPC5_EML_0]		= mmTPC5_EML_BUSMON_0_BASE,
	[GOYA_BMON_TPC5_EML_1]		= mmTPC5_EML_BUSMON_1_BASE,
	[GOYA_BMON_TPC5_EML_2]		= mmTPC5_EML_BUSMON_2_BASE,
	[GOYA_BMON_TPC5_EML_3]		= mmTPC5_EML_BUSMON_3_BASE,
	[GOYA_BMON_TPC6_EML_0]		= mmTPC6_EML_BUSMON_0_BASE,
	[GOYA_BMON_TPC6_EML_1]		= mmTPC6_EML_BUSMON_1_BASE,
	[GOYA_BMON_TPC6_EML_2]		= mmTPC6_EML_BUSMON_2_BASE,
	[GOYA_BMON_TPC6_EML_3]		= mmTPC6_EML_BUSMON_3_BASE,
	[GOYA_BMON_TPC7_EML_0]		= mmTPC7_EML_BUSMON_0_BASE,
	[GOYA_BMON_TPC7_EML_1]		= mmTPC7_EML_BUSMON_1_BASE,
	[GOYA_BMON_TPC7_EML_2]		= mmTPC7_EML_BUSMON_2_BASE,
	[GOYA_BMON_TPC7_EML_3]		= mmTPC7_EML_BUSMON_3_BASE
};

static u64 debug_spmu_regs[GOYA_SPMU_LAST + 1] = {
	[GOYA_SPMU_DMA_CH_0_CS]		= mmDMA_CH_0_CS_SPMU_BASE,
	[GOYA_SPMU_DMA_CH_1_CS]		= mmDMA_CH_1_CS_SPMU_BASE,
	[GOYA_SPMU_DMA_CH_2_CS]		= mmDMA_CH_2_CS_SPMU_BASE,
	[GOYA_SPMU_DMA_CH_3_CS]		= mmDMA_CH_3_CS_SPMU_BASE,
	[GOYA_SPMU_DMA_CH_4_CS]		= mmDMA_CH_4_CS_SPMU_BASE,
	[GOYA_SPMU_DMA_MACRO_CS]	= mmDMA_MACRO_CS_SPMU_BASE,
	[GOYA_SPMU_MME1_SBA]		= mmMME1_SBA_SPMU_BASE,
	[GOYA_SPMU_MME3_SBB]		= mmMME3_SBB_SPMU_BASE,
	[GOYA_SPMU_MME4_WACS2]		= mmMME4_WACS2_SPMU_BASE,
	[GOYA_SPMU_MME4_WACS]		= mmMME4_WACS_SPMU_BASE,
	[GOYA_SPMU_MMU_CS]		= mmMMU_CS_SPMU_BASE,
	[GOYA_SPMU_PCIE]		= mmPCIE_SPMU_BASE,
	[GOYA_SPMU_TPC0_EML]		= mmTPC0_EML_SPMU_BASE,
	[GOYA_SPMU_TPC1_EML]		= mmTPC1_EML_SPMU_BASE,
	[GOYA_SPMU_TPC2_EML]		= mmTPC2_EML_SPMU_BASE,
	[GOYA_SPMU_TPC3_EML]		= mmTPC3_EML_SPMU_BASE,
	[GOYA_SPMU_TPC4_EML]		= mmTPC4_EML_SPMU_BASE,
	[GOYA_SPMU_TPC5_EML]		= mmTPC5_EML_SPMU_BASE,
	[GOYA_SPMU_TPC6_EML]		= mmTPC6_EML_SPMU_BASE,
	[GOYA_SPMU_TPC7_EML]		= mmTPC7_EML_SPMU_BASE
};

static int goya_coresight_timeout(struct hl_device *hdev, u64 addr,
		int position, bool up)
{
	int rc;
	u32 val, timeout_usec;

	if (hdev->pldm)
		timeout_usec = GOYA_PLDM_CORESIGHT_TIMEOUT_USEC;
	else
		timeout_usec = CORESIGHT_TIMEOUT_USEC;

	rc = hl_poll_timeout(
		hdev,
		addr,
		val,
		up ? val & BIT(position) : !(val & BIT(position)),
		1000,
		timeout_usec);

	if (rc) {
		dev_err(hdev->dev,
			"Timeout while waiting for coresight, addr: 0x%llx, position: %d, up: %d\n",
				addr, position, up);
		return -EFAULT;
	}

	return 0;
}

static int goya_config_stm(struct hl_device *hdev,
		struct hl_debug_params *params)
{
	struct hl_debug_params_stm *input;
	u64 base_reg = debug_stm_regs[params->reg_idx] - CFG_BASE;
	int rc;

	WREG32(base_reg + 0xFB0, CORESIGHT_UNLOCK);

	if (params->enable) {
		input = params->input;

		if (!input)
			return -EINVAL;

		WREG32(base_reg + 0xE80, 0x80004);
		WREG32(base_reg + 0xD64, 7);
		WREG32(base_reg + 0xD60, 0);
		WREG32(base_reg + 0xD00, lower_32_bits(input->he_mask));
		WREG32(base_reg + 0xD20, lower_32_bits(input->sp_mask));
		WREG32(base_reg + 0xD60, 1);
		WREG32(base_reg + 0xD00, upper_32_bits(input->he_mask));
		WREG32(base_reg + 0xD20, upper_32_bits(input->sp_mask));
		WREG32(base_reg + 0xE70, 0x10);
		WREG32(base_reg + 0xE60, 0);
		WREG32(base_reg + 0xE64, 0x420000);
		WREG32(base_reg + 0xE00, 0xFFFFFFFF);
		WREG32(base_reg + 0xE20, 0xFFFFFFFF);
		WREG32(base_reg + 0xEF4, input->id);
		WREG32(base_reg + 0xDF4, 0x80);
		WREG32(base_reg + 0xE8C, input->frequency);
		WREG32(base_reg + 0xE90, 0x7FF);
		WREG32(base_reg + 0xE80, 0x7 | (input->id << 16));
	} else {
		WREG32(base_reg + 0xE80, 4);
		WREG32(base_reg + 0xD64, 0);
		WREG32(base_reg + 0xD60, 1);
		WREG32(base_reg + 0xD00, 0);
		WREG32(base_reg + 0xD20, 0);
		WREG32(base_reg + 0xD60, 0);
		WREG32(base_reg + 0xE20, 0);
		WREG32(base_reg + 0xE00, 0);
		WREG32(base_reg + 0xDF4, 0x80);
		WREG32(base_reg + 0xE70, 0);
		WREG32(base_reg + 0xE60, 0);
		WREG32(base_reg + 0xE64, 0);
		WREG32(base_reg + 0xE8C, 0);

		rc = goya_coresight_timeout(hdev, base_reg + 0xE80, 23, false);
		if (rc) {
			dev_err(hdev->dev,
				"Failed to disable STM on timeout, error %d\n",
				rc);
			return rc;
		}

		WREG32(base_reg + 0xE80, 4);
	}

	return 0;
}

static int goya_config_etf(struct hl_device *hdev,
		struct hl_debug_params *params)
{
	struct hl_debug_params_etf *input;
	u64 base_reg = debug_etf_regs[params->reg_idx] - CFG_BASE;
	u32 val;
	int rc;

	WREG32(base_reg + 0xFB0, CORESIGHT_UNLOCK);

	val = RREG32(base_reg + 0x304);
	val |= 0x1000;
	WREG32(base_reg + 0x304, val);
	val |= 0x40;
	WREG32(base_reg + 0x304, val);

	rc = goya_coresight_timeout(hdev, base_reg + 0x304, 6, false);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to %s ETF on timeout, error %d\n",
				params->enable ? "enable" : "disable", rc);
		return rc;
	}

	rc = goya_coresight_timeout(hdev, base_reg + 0xC, 2, true);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to %s ETF on timeout, error %d\n",
				params->enable ? "enable" : "disable", rc);
		return rc;
	}

	WREG32(base_reg + 0x20, 0);

	if (params->enable) {
		input = params->input;

		if (!input)
			return -EINVAL;

		WREG32(base_reg + 0x34, 0x3FFC);
		WREG32(base_reg + 0x28, input->sink_mode);
		WREG32(base_reg + 0x304, 0x4001);
		WREG32(base_reg + 0x308, 0xA);
		WREG32(base_reg + 0x20, 1);
	} else {
		WREG32(base_reg + 0x34, 0);
		WREG32(base_reg + 0x28, 0);
		WREG32(base_reg + 0x304, 0);
	}

	return 0;
}

static int goya_etr_validate_address(struct hl_device *hdev, u64 addr,
		u32 size)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 range_start, range_end;

	if (hdev->mmu_enable) {
		range_start = prop->va_space_dram_start_address;
		range_end = prop->va_space_dram_end_address;
	} else {
		range_start = prop->dram_user_base_address;
		range_end = prop->dram_end_address;
	}

	return hl_mem_area_inside_range(addr, size, range_start, range_end);
}

static int goya_config_etr(struct hl_device *hdev,
		struct hl_debug_params *params)
{
	struct hl_debug_params_etr *input;
	u64 base_reg = mmPSOC_ETR_BASE - CFG_BASE;
	u32 val;
	int rc;

	WREG32(base_reg + 0xFB0, CORESIGHT_UNLOCK);

	val = RREG32(base_reg + 0x304);
	val |= 0x1000;
	WREG32(base_reg + 0x304, val);
	val |= 0x40;
	WREG32(base_reg + 0x304, val);

	rc = goya_coresight_timeout(hdev, base_reg + 0x304, 6, false);
	if (rc) {
		dev_err(hdev->dev, "Failed to %s ETR on timeout, error %d\n",
				params->enable ? "enable" : "disable", rc);
		return rc;
	}

	rc = goya_coresight_timeout(hdev, base_reg + 0xC, 2, true);
	if (rc) {
		dev_err(hdev->dev, "Failed to %s ETR on timeout, error %d\n",
				params->enable ? "enable" : "disable", rc);
		return rc;
	}

	WREG32(base_reg + 0x20, 0);

	if (params->enable) {
		input = params->input;

		if (!input)
			return -EINVAL;

		if (input->buffer_size == 0) {
			dev_err(hdev->dev,
				"ETR buffer size should be bigger than 0\n");
			return -EINVAL;
		}

		if (!goya_etr_validate_address(hdev,
				input->buffer_address, input->buffer_size)) {
			dev_err(hdev->dev, "buffer address is not valid\n");
			return -EINVAL;
		}

		WREG32(base_reg + 0x34, 0x3FFC);
		WREG32(base_reg + 0x4, input->buffer_size);
		WREG32(base_reg + 0x28, input->sink_mode);
		WREG32(base_reg + 0x110, 0x700);
		WREG32(base_reg + 0x118,
				lower_32_bits(input->buffer_address));
		WREG32(base_reg + 0x11C,
				upper_32_bits(input->buffer_address));
		WREG32(base_reg + 0x304, 3);
		WREG32(base_reg + 0x308, 0xA);
		WREG32(base_reg + 0x20, 1);
	} else {
		WREG32(base_reg + 0x34, 0);
		WREG32(base_reg + 0x4, 0x400);
		WREG32(base_reg + 0x118, 0);
		WREG32(base_reg + 0x11C, 0);
		WREG32(base_reg + 0x308, 0);
		WREG32(base_reg + 0x28, 0);
		WREG32(base_reg + 0x304, 0);

		if (params->output_size >= sizeof(u64)) {
			u32 rwp, rwphi;

			/*
			 * The trace buffer address is 40 bits wide. The end of
			 * the buffer is set in the RWP register (lower 32
			 * bits), and in the RWPHI register (upper 8 bits).
			 */
			rwp = RREG32(base_reg + 0x18);
			rwphi = RREG32(base_reg + 0x3c) & 0xff;
			*(u64 *) params->output = ((u64) rwphi << 32) | rwp;
		}
	}

	return 0;
}

static int goya_config_funnel(struct hl_device *hdev,
		struct hl_debug_params *params)
{
	WREG32(debug_funnel_regs[params->reg_idx] - CFG_BASE + 0xFB0,
			CORESIGHT_UNLOCK);

	WREG32(debug_funnel_regs[params->reg_idx] - CFG_BASE,
			params->enable ? 0x33F : 0);

	return 0;
}

static int goya_config_bmon(struct hl_device *hdev,
		struct hl_debug_params *params)
{
	struct hl_debug_params_bmon *input;
	u64 base_reg = debug_bmon_regs[params->reg_idx] - CFG_BASE;
	u32 pcie_base = 0;

	WREG32(base_reg + 0x104, 1);

	if (params->enable) {
		input = params->input;

		if (!input)
			return -EINVAL;

		WREG32(base_reg + 0x200, lower_32_bits(input->start_addr0));
		WREG32(base_reg + 0x204, upper_32_bits(input->start_addr0));
		WREG32(base_reg + 0x208, lower_32_bits(input->addr_mask0));
		WREG32(base_reg + 0x20C, upper_32_bits(input->addr_mask0));
		WREG32(base_reg + 0x240, lower_32_bits(input->start_addr1));
		WREG32(base_reg + 0x244, upper_32_bits(input->start_addr1));
		WREG32(base_reg + 0x248, lower_32_bits(input->addr_mask1));
		WREG32(base_reg + 0x24C, upper_32_bits(input->addr_mask1));
		WREG32(base_reg + 0x224, 0);
		WREG32(base_reg + 0x234, 0);
		WREG32(base_reg + 0x30C, input->bw_win);
		WREG32(base_reg + 0x308, input->win_capture);

		/* PCIE IF BMON bug WA */
		if (params->reg_idx != GOYA_BMON_PCIE_MSTR_RD &&
				params->reg_idx != GOYA_BMON_PCIE_MSTR_WR &&
				params->reg_idx != GOYA_BMON_PCIE_SLV_RD &&
				params->reg_idx != GOYA_BMON_PCIE_SLV_WR)
			pcie_base = 0xA000000;

		WREG32(base_reg + 0x700, pcie_base | 0xB00 | (input->id << 12));
		WREG32(base_reg + 0x708, pcie_base | 0xA00 | (input->id << 12));
		WREG32(base_reg + 0x70C, pcie_base | 0xC00 | (input->id << 12));

		WREG32(base_reg + 0x100, 0x11);
		WREG32(base_reg + 0x304, 0x1);
	} else {
		WREG32(base_reg + 0x200, 0);
		WREG32(base_reg + 0x204, 0);
		WREG32(base_reg + 0x208, 0xFFFFFFFF);
		WREG32(base_reg + 0x20C, 0xFFFFFFFF);
		WREG32(base_reg + 0x240, 0);
		WREG32(base_reg + 0x244, 0);
		WREG32(base_reg + 0x248, 0xFFFFFFFF);
		WREG32(base_reg + 0x24C, 0xFFFFFFFF);
		WREG32(base_reg + 0x224, 0xFFFFFFFF);
		WREG32(base_reg + 0x234, 0x1070F);
		WREG32(base_reg + 0x30C, 0);
		WREG32(base_reg + 0x308, 0xFFFF);
		WREG32(base_reg + 0x700, 0xA000B00);
		WREG32(base_reg + 0x708, 0xA000A00);
		WREG32(base_reg + 0x70C, 0xA000C00);
		WREG32(base_reg + 0x100, 1);
		WREG32(base_reg + 0x304, 0);
		WREG32(base_reg + 0x104, 0);
	}

	return 0;
}

static int goya_config_spmu(struct hl_device *hdev,
		struct hl_debug_params *params)
{
	u64 base_reg = debug_spmu_regs[params->reg_idx] - CFG_BASE;
	struct hl_debug_params_spmu *input = params->input;
	u64 *output;
	u32 output_arr_len;
	u32 events_num;
	u32 overflow_idx;
	u32 cycle_cnt_idx;
	int i;

	if (params->enable) {
		input = params->input;

		if (!input)
			return -EINVAL;

		if (input->event_types_num < 3) {
			dev_err(hdev->dev,
				"not enough values for SPMU enable\n");
			return -EINVAL;
		}

		WREG32(base_reg + 0xE04, 0x41013046);
		WREG32(base_reg + 0xE04, 0x41013040);

		for (i = 0 ; i < input->event_types_num ; i++)
			WREG32(base_reg + 0x400 + i * 4, input->event_types[i]);

		WREG32(base_reg + 0xE04, 0x41013041);
		WREG32(base_reg + 0xC00, 0x8000003F);
	} else {
		output = params->output;
		output_arr_len = params->output_size / 8;
		events_num = output_arr_len - 2;
		overflow_idx = output_arr_len - 2;
		cycle_cnt_idx = output_arr_len - 1;

		if (!output)
			return -EINVAL;

		if (output_arr_len < 3) {
			dev_err(hdev->dev,
				"not enough values for SPMU disable\n");
			return -EINVAL;
		}

		WREG32(base_reg + 0xE04, 0x41013040);

		for (i = 0 ; i < events_num ; i++)
			output[i] = RREG32(base_reg + i * 8);

		output[overflow_idx] = RREG32(base_reg + 0xCC0);

		output[cycle_cnt_idx] = RREG32(base_reg + 0xFC);
		output[cycle_cnt_idx] <<= 32;
		output[cycle_cnt_idx] |= RREG32(base_reg + 0xF8);

		WREG32(base_reg + 0xCC0, 0);
	}

	return 0;
}

static int goya_config_timestamp(struct hl_device *hdev,
		struct hl_debug_params *params)
{
	WREG32(mmPSOC_TIMESTAMP_BASE - CFG_BASE, 0);
	if (params->enable) {
		WREG32(mmPSOC_TIMESTAMP_BASE - CFG_BASE + 0xC, 0);
		WREG32(mmPSOC_TIMESTAMP_BASE - CFG_BASE + 0x8, 0);
		WREG32(mmPSOC_TIMESTAMP_BASE - CFG_BASE, 1);
	}

	return 0;
}

int goya_debug_coresight(struct hl_device *hdev, void *data)
{
	struct hl_debug_params *params = data;
	u32 val;
	int rc;

	switch (params->op) {
	case HL_DEBUG_OP_STM:
		rc = goya_config_stm(hdev, params);
		break;
	case HL_DEBUG_OP_ETF:
		rc = goya_config_etf(hdev, params);
		break;
	case HL_DEBUG_OP_ETR:
		rc = goya_config_etr(hdev, params);
		break;
	case HL_DEBUG_OP_FUNNEL:
		rc = goya_config_funnel(hdev, params);
		break;
	case HL_DEBUG_OP_BMON:
		rc = goya_config_bmon(hdev, params);
		break;
	case HL_DEBUG_OP_SPMU:
		rc = goya_config_spmu(hdev, params);
		break;
	case HL_DEBUG_OP_TIMESTAMP:
		rc = goya_config_timestamp(hdev, params);
		break;

	default:
		dev_err(hdev->dev, "Unknown coresight id %d\n", params->op);
		return -EINVAL;
	}

	/* Perform read from the device to flush all configuration */
	val = RREG32(mmPCIE_DBI_DEVICE_ID_VENDOR_ID_REG);

	return rc;
}

void goya_halt_coresight(struct hl_device *hdev)
{
	struct hl_debug_params params = {};
	int i, rc;

	for (i = GOYA_ETF_FIRST ; i <= GOYA_ETF_LAST ; i++) {
		params.reg_idx = i;
		rc = goya_config_etf(hdev, &params);
		if (rc)
			dev_err(hdev->dev, "halt ETF failed, %d/%d\n", rc, i);
	}

	rc = goya_config_etr(hdev, &params);
	if (rc)
		dev_err(hdev->dev, "halt ETR failed, %d\n", rc);
}
