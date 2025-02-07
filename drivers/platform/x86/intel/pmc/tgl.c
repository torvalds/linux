// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains platform specific structure definitions
 * and init function used by Tiger Lake PCH.
 *
 * Copyright (c) 2022, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include "core.h"

#define ACPI_S0IX_DSM_UUID		"57a6512e-3979-4e9d-9708-ff13b2508972"
#define ACPI_GET_LOW_MODE_REGISTERS	1

enum pch_type {
	PCH_H,
	PCH_LP
};

const struct pmc_bit_map tgl_pfear_map[] = {
	{"PSF9",		BIT(0)},
	{"RES_66",		BIT(1)},
	{"RES_67",		BIT(2)},
	{"RES_68",		BIT(3)},
	{"RES_69",		BIT(4)},
	{"RES_70",		BIT(5)},
	{"TBTLSX",		BIT(6)},
	{}
};

const struct pmc_bit_map *ext_tgl_pfear_map[] = {
	/*
	 * Check intel_pmc_core_ids[] users of tgl_reg_map for
	 * a list of core SoCs using this.
	 */
	cnp_pfear_map,
	tgl_pfear_map,
	NULL
};

const struct pmc_bit_map tgl_clocksource_status_map[] = {
	{"USB2PLL_OFF_STS",			BIT(18)},
	{"PCIe/USB3.1_Gen2PLL_OFF_STS",		BIT(19)},
	{"PCIe_Gen3PLL_OFF_STS",		BIT(20)},
	{"OPIOPLL_OFF_STS",			BIT(21)},
	{"OCPLL_OFF_STS",			BIT(22)},
	{"MainPLL_OFF_STS",			BIT(23)},
	{"MIPIPLL_OFF_STS",			BIT(24)},
	{"Fast_XTAL_Osc_OFF_STS",		BIT(25)},
	{"AC_Ring_Osc_OFF_STS",			BIT(26)},
	{"MC_Ring_Osc_OFF_STS",			BIT(27)},
	{"SATAPLL_OFF_STS",			BIT(29)},
	{"XTAL_USB2PLL_OFF_STS",		BIT(31)},
	{}
};

const struct pmc_bit_map tgl_power_gating_status_map[] = {
	{"CSME_PG_STS",				BIT(0)},
	{"SATA_PG_STS",				BIT(1)},
	{"xHCI_PG_STS",				BIT(2)},
	{"UFSX2_PG_STS",			BIT(3)},
	{"OTG_PG_STS",				BIT(5)},
	{"SPA_PG_STS",				BIT(6)},
	{"SPB_PG_STS",				BIT(7)},
	{"SPC_PG_STS",				BIT(8)},
	{"SPD_PG_STS",				BIT(9)},
	{"SPE_PG_STS",				BIT(10)},
	{"SPF_PG_STS",				BIT(11)},
	{"LSX_PG_STS",				BIT(13)},
	{"P2SB_PG_STS",				BIT(14)},
	{"PSF_PG_STS",				BIT(15)},
	{"SBR_PG_STS",				BIT(16)},
	{"OPIDMI_PG_STS",			BIT(17)},
	{"THC0_PG_STS",				BIT(18)},
	{"THC1_PG_STS",				BIT(19)},
	{"GBETSN_PG_STS",			BIT(20)},
	{"GBE_PG_STS",				BIT(21)},
	{"LPSS_PG_STS",				BIT(22)},
	{"MMP_UFSX2_PG_STS",			BIT(23)},
	{"MMP_UFSX2B_PG_STS",			BIT(24)},
	{"FIA_PG_STS",				BIT(25)},
	{}
};

const struct pmc_bit_map tgl_d3_status_map[] = {
	{"ADSP_D3_STS",				BIT(0)},
	{"SATA_D3_STS",				BIT(1)},
	{"xHCI0_D3_STS",			BIT(2)},
	{"xDCI1_D3_STS",			BIT(5)},
	{"SDX_D3_STS",				BIT(6)},
	{"EMMC_D3_STS",				BIT(7)},
	{"IS_D3_STS",				BIT(8)},
	{"THC0_D3_STS",				BIT(9)},
	{"THC1_D3_STS",				BIT(10)},
	{"GBE_D3_STS",				BIT(11)},
	{"GBE_TSN_D3_STS",			BIT(12)},
	{}
};

const struct pmc_bit_map tgl_vnn_req_status_map[] = {
	{"GPIO_COM0_VNN_REQ_STS",		BIT(1)},
	{"GPIO_COM1_VNN_REQ_STS",		BIT(2)},
	{"GPIO_COM2_VNN_REQ_STS",		BIT(3)},
	{"GPIO_COM3_VNN_REQ_STS",		BIT(4)},
	{"GPIO_COM4_VNN_REQ_STS",		BIT(5)},
	{"GPIO_COM5_VNN_REQ_STS",		BIT(6)},
	{"Audio_VNN_REQ_STS",			BIT(7)},
	{"ISH_VNN_REQ_STS",			BIT(8)},
	{"CNVI_VNN_REQ_STS",			BIT(9)},
	{"eSPI_VNN_REQ_STS",			BIT(10)},
	{"Display_VNN_REQ_STS",			BIT(11)},
	{"DTS_VNN_REQ_STS",			BIT(12)},
	{"SMBUS_VNN_REQ_STS",			BIT(14)},
	{"CSME_VNN_REQ_STS",			BIT(15)},
	{"SMLINK0_VNN_REQ_STS",			BIT(16)},
	{"SMLINK1_VNN_REQ_STS",			BIT(17)},
	{"CLINK_VNN_REQ_STS",			BIT(20)},
	{"DCI_VNN_REQ_STS",			BIT(21)},
	{"ITH_VNN_REQ_STS",			BIT(22)},
	{"CSME_VNN_REQ_STS",			BIT(24)},
	{"GBE_VNN_REQ_STS",			BIT(25)},
	{}
};

const struct pmc_bit_map tgl_vnn_misc_status_map[] = {
	{"CPU_C10_REQ_STS_0",			BIT(0)},
	{"PCIe_LPM_En_REQ_STS_3",		BIT(3)},
	{"ITH_REQ_STS_5",			BIT(5)},
	{"CNVI_REQ_STS_6",			BIT(6)},
	{"ISH_REQ_STS_7",			BIT(7)},
	{"USB2_SUS_PG_Sys_REQ_STS_10",		BIT(10)},
	{"PCIe_Clk_REQ_STS_12",			BIT(12)},
	{"MPHY_Core_DL_REQ_STS_16",		BIT(16)},
	{"Break-even_En_REQ_STS_17",		BIT(17)},
	{"Auto-demo_En_REQ_STS_18",		BIT(18)},
	{"MPHY_SUS_REQ_STS_22",			BIT(22)},
	{"xDCI_attached_REQ_STS_24",		BIT(24)},
	{}
};

const struct pmc_bit_map tgl_signal_status_map[] = {
	{"LSX_Wake0_En_STS",			BIT(0)},
	{"LSX_Wake0_Pol_STS",			BIT(1)},
	{"LSX_Wake1_En_STS",			BIT(2)},
	{"LSX_Wake1_Pol_STS",			BIT(3)},
	{"LSX_Wake2_En_STS",			BIT(4)},
	{"LSX_Wake2_Pol_STS",			BIT(5)},
	{"LSX_Wake3_En_STS",			BIT(6)},
	{"LSX_Wake3_Pol_STS",			BIT(7)},
	{"LSX_Wake4_En_STS",			BIT(8)},
	{"LSX_Wake4_Pol_STS",			BIT(9)},
	{"LSX_Wake5_En_STS",			BIT(10)},
	{"LSX_Wake5_Pol_STS",			BIT(11)},
	{"LSX_Wake6_En_STS",			BIT(12)},
	{"LSX_Wake6_Pol_STS",			BIT(13)},
	{"LSX_Wake7_En_STS",			BIT(14)},
	{"LSX_Wake7_Pol_STS",			BIT(15)},
	{"Intel_Se_IO_Wake0_En_STS",		BIT(16)},
	{"Intel_Se_IO_Wake0_Pol_STS",		BIT(17)},
	{"Intel_Se_IO_Wake1_En_STS",		BIT(18)},
	{"Intel_Se_IO_Wake1_Pol_STS",		BIT(19)},
	{"Int_Timer_SS_Wake0_En_STS",		BIT(20)},
	{"Int_Timer_SS_Wake0_Pol_STS",		BIT(21)},
	{"Int_Timer_SS_Wake1_En_STS",		BIT(22)},
	{"Int_Timer_SS_Wake1_Pol_STS",		BIT(23)},
	{"Int_Timer_SS_Wake2_En_STS",		BIT(24)},
	{"Int_Timer_SS_Wake2_Pol_STS",		BIT(25)},
	{"Int_Timer_SS_Wake3_En_STS",		BIT(26)},
	{"Int_Timer_SS_Wake3_Pol_STS",		BIT(27)},
	{"Int_Timer_SS_Wake4_En_STS",		BIT(28)},
	{"Int_Timer_SS_Wake4_Pol_STS",		BIT(29)},
	{"Int_Timer_SS_Wake5_En_STS",		BIT(30)},
	{"Int_Timer_SS_Wake5_Pol_STS",		BIT(31)},
	{}
};

const struct pmc_bit_map *tgl_lpm_maps[] = {
	tgl_clocksource_status_map,
	tgl_power_gating_status_map,
	tgl_d3_status_map,
	tgl_vnn_req_status_map,
	tgl_vnn_misc_status_map,
	tgl_signal_status_map,
	NULL
};

const struct pmc_reg_map tgl_reg_map = {
	.pfear_sts = ext_tgl_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slp_s0_res_counter_step = TGL_PMC_SLP_S0_RES_COUNTER_STEP,
	.ltr_show_sts = cnp_ltr_show_map,
	.msr_sts = msr_map,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = CNP_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = ICL_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
	.ltr_ignore_max = TGL_NUM_IP_IGN_ALLOWED,
	.lpm_num_maps = TGL_LPM_NUM_MAPS,
	.lpm_res_counter_step_x2 = TGL_PMC_LPM_RES_COUNTER_STEP_X2,
	.lpm_sts_latch_en_offset = TGL_LPM_STS_LATCH_EN_OFFSET,
	.lpm_en_offset = TGL_LPM_EN_OFFSET,
	.lpm_priority_offset = TGL_LPM_PRI_OFFSET,
	.lpm_residency_offset = TGL_LPM_RESIDENCY_OFFSET,
	.lpm_sts = tgl_lpm_maps,
	.lpm_status_offset = TGL_LPM_STATUS_OFFSET,
	.lpm_live_status_offset = TGL_LPM_LIVE_STATUS_OFFSET,
	.etr3_offset = ETR3_OFFSET,
};

const struct pmc_reg_map tgl_h_reg_map = {
	.pfear_sts = ext_tgl_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slp_s0_res_counter_step = TGL_PMC_SLP_S0_RES_COUNTER_STEP,
	.ltr_show_sts = cnp_ltr_show_map,
	.msr_sts = msr_map,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = CNP_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = ICL_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
	.ltr_ignore_max = TGL_NUM_IP_IGN_ALLOWED,
	.lpm_num_maps = TGL_LPM_NUM_MAPS,
	.lpm_res_counter_step_x2 = TGL_PMC_LPM_RES_COUNTER_STEP_X2,
	.lpm_sts_latch_en_offset = TGL_LPM_STS_LATCH_EN_OFFSET,
	.lpm_en_offset = TGL_LPM_EN_OFFSET,
	.lpm_priority_offset = TGL_LPM_PRI_OFFSET,
	.lpm_residency_offset = TGL_LPM_RESIDENCY_OFFSET,
	.lpm_sts = tgl_lpm_maps,
	.lpm_status_offset = TGL_LPM_STATUS_OFFSET,
	.lpm_live_status_offset = TGL_LPM_LIVE_STATUS_OFFSET,
	.etr3_offset = ETR3_OFFSET,
	.pson_residency_offset = TGL_PSON_RESIDENCY_OFFSET,
	.pson_residency_counter_step = TGL_PSON_RES_COUNTER_STEP,
};

void pmc_core_get_tgl_lpm_reqs(struct platform_device *pdev)
{
	struct pmc_dev *pmcdev = platform_get_drvdata(pdev);
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	const int num_maps = pmc->map->lpm_num_maps;
	u32 lpm_size = LPM_MAX_NUM_MODES * num_maps * 4;
	union acpi_object *out_obj;
	struct acpi_device *adev;
	guid_t s0ix_dsm_guid;
	u32 *lpm_req_regs, *addr;

	adev = ACPI_COMPANION(&pdev->dev);
	if (!adev)
		return;

	guid_parse(ACPI_S0IX_DSM_UUID, &s0ix_dsm_guid);

	out_obj = acpi_evaluate_dsm_typed(adev->handle, &s0ix_dsm_guid, 0,
					  ACPI_GET_LOW_MODE_REGISTERS, NULL, ACPI_TYPE_BUFFER);
	if (out_obj) {
		u32 size = out_obj->buffer.length;

		if (size != lpm_size) {
			acpi_handle_debug(adev->handle,
				"_DSM returned unexpected buffer size, have %u, expect %u\n",
				size, lpm_size);
			goto free_acpi_obj;
		}
	} else {
		acpi_handle_debug(adev->handle,
				  "_DSM function 0 evaluation failed\n");
		goto free_acpi_obj;
	}

	addr = (u32 *)out_obj->buffer.pointer;

	lpm_req_regs = devm_kzalloc(&pdev->dev, lpm_size * sizeof(u32),
				     GFP_KERNEL);
	if (!lpm_req_regs)
		goto free_acpi_obj;

	memcpy(lpm_req_regs, addr, lpm_size);
	pmc->lpm_req_regs = lpm_req_regs;

free_acpi_obj:
	ACPI_FREE(out_obj);
}

static int tgl_core_init(struct pmc_dev *pmcdev, struct pmc_dev_info *pmc_dev_info)
{
	int ret;

	ret = generic_core_init(pmcdev, pmc_dev_info);
	if (ret)
		return ret;

	pmc_core_get_tgl_lpm_reqs(pmcdev->pdev);
	return 0;
}

struct pmc_dev_info tgl_l_pmc_dev = {
	.map = &tgl_reg_map,
	.suspend = cnl_suspend,
	.resume = cnl_resume,
	.init = tgl_core_init,
};

struct pmc_dev_info tgl_pmc_dev = {
	.map = &tgl_h_reg_map,
	.suspend = cnl_suspend,
	.resume = cnl_resume,
	.init = tgl_core_init,
};
