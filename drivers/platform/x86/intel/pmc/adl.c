// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains platform specific structure definitions
 * and init function used by Alder Lake PCH.
 *
 * Copyright (c) 2022, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include "core.h"

/* Alder Lake: PGD PFET Enable Ack Status Register(s) bitmap */
const struct pmc_bit_map adl_pfear_map[] = {
	{"SPI/eSPI",		BIT(2)},
	{"XHCI",		BIT(3)},
	{"SPA",			BIT(4)},
	{"SPB",			BIT(5)},
	{"SPC",			BIT(6)},
	{"GBE",			BIT(7)},

	{"SATA",		BIT(0)},
	{"HDA_PGD0",		BIT(1)},
	{"HDA_PGD1",		BIT(2)},
	{"HDA_PGD2",		BIT(3)},
	{"HDA_PGD3",		BIT(4)},
	{"SPD",			BIT(5)},
	{"LPSS",		BIT(6)},

	{"SMB",			BIT(0)},
	{"ISH",			BIT(1)},
	{"ITH",			BIT(3)},

	{"XDCI",		BIT(1)},
	{"DCI",			BIT(2)},
	{"CSE",			BIT(3)},
	{"CSME_KVM",		BIT(4)},
	{"CSME_PMT",		BIT(5)},
	{"CSME_CLINK",		BIT(6)},
	{"CSME_PTIO",		BIT(7)},

	{"CSME_USBR",		BIT(0)},
	{"CSME_SUSRAM",		BIT(1)},
	{"CSME_SMT1",		BIT(2)},
	{"CSME_SMS2",		BIT(4)},
	{"CSME_SMS1",		BIT(5)},
	{"CSME_RTC",		BIT(6)},
	{"CSME_PSF",		BIT(7)},

	{"CNVI",		BIT(3)},
	{"HDA_PGD4",		BIT(2)},
	{"HDA_PGD5",		BIT(3)},
	{"HDA_PGD6",		BIT(4)},
	{}
};

const struct pmc_bit_map *ext_adl_pfear_map[] = {
	/*
	 * Check intel_pmc_core_ids[] users of cnp_reg_map for
	 * a list of core SoCs using this.
	 */
	adl_pfear_map,
	NULL
};

const struct pmc_bit_map adl_ltr_show_map[] = {
	{"SOUTHPORT_A",		CNP_PMC_LTR_SPA},
	{"SOUTHPORT_B",		CNP_PMC_LTR_SPB},
	{"SATA",		CNP_PMC_LTR_SATA},
	{"GIGABIT_ETHERNET",	CNP_PMC_LTR_GBE},
	{"XHCI",		CNP_PMC_LTR_XHCI},
	{"SOUTHPORT_F",		ADL_PMC_LTR_SPF},
	{"ME",			CNP_PMC_LTR_ME},
	/* EVA is Enterprise Value Add, doesn't really exist on PCH */
	{"SATA1",		CNP_PMC_LTR_EVA},
	{"SOUTHPORT_C",		CNP_PMC_LTR_SPC},
	{"HD_AUDIO",		CNP_PMC_LTR_AZ},
	{"CNV",			CNP_PMC_LTR_CNV},
	{"LPSS",		CNP_PMC_LTR_LPSS},
	{"SOUTHPORT_D",		CNP_PMC_LTR_SPD},
	{"SOUTHPORT_E",		CNP_PMC_LTR_SPE},
	{"SATA2",		CNP_PMC_LTR_CAM},
	{"ESPI",		CNP_PMC_LTR_ESPI},
	{"SCC",			CNP_PMC_LTR_SCC},
	{"ISH",			CNP_PMC_LTR_ISH},
	{"UFSX2",		CNP_PMC_LTR_UFSX2},
	{"EMMC",		CNP_PMC_LTR_EMMC},
	/*
	 * Check intel_pmc_core_ids[] users of cnp_reg_map for
	 * a list of core SoCs using this.
	 */
	{"WIGIG",		ICL_PMC_LTR_WIGIG},
	{"THC0",		TGL_PMC_LTR_THC0},
	{"THC1",		TGL_PMC_LTR_THC1},
	{"SOUTHPORT_G",		CNP_PMC_LTR_RESERVED},

	/* Below two cannot be used for LTR_IGNORE */
	{"CURRENT_PLATFORM",	CNP_PMC_LTR_CUR_PLT},
	{"AGGREGATED_SYSTEM",	CNP_PMC_LTR_CUR_ASLT},
	{}
};

const struct pmc_bit_map adl_clocksource_status_map[] = {
	{"CLKPART1_OFF_STS",			BIT(0)},
	{"CLKPART2_OFF_STS",			BIT(1)},
	{"CLKPART3_OFF_STS",			BIT(2)},
	{"CLKPART4_OFF_STS",			BIT(3)},
	{"CLKPART5_OFF_STS",			BIT(4)},
	{"CLKPART6_OFF_STS",			BIT(5)},
	{"CLKPART7_OFF_STS",			BIT(6)},
	{"CLKPART8_OFF_STS",			BIT(7)},
	{"PCIE0PLL_OFF_STS",			BIT(10)},
	{"PCIE1PLL_OFF_STS",			BIT(11)},
	{"PCIE2PLL_OFF_STS",			BIT(12)},
	{"PCIE3PLL_OFF_STS",			BIT(13)},
	{"PCIE4PLL_OFF_STS",			BIT(14)},
	{"PCIE5PLL_OFF_STS",			BIT(15)},
	{"PCIE6PLL_OFF_STS",			BIT(16)},
	{"USB2PLL_OFF_STS",			BIT(18)},
	{"OCPLL_OFF_STS",			BIT(22)},
	{"AUDIOPLL_OFF_STS",			BIT(23)},
	{"GBEPLL_OFF_STS",			BIT(24)},
	{"Fast_XTAL_Osc_OFF_STS",		BIT(25)},
	{"AC_Ring_Osc_OFF_STS",			BIT(26)},
	{"MC_Ring_Osc_OFF_STS",			BIT(27)},
	{"SATAPLL_OFF_STS",			BIT(29)},
	{"USB3PLL_OFF_STS",			BIT(31)},
	{}
};

const struct pmc_bit_map adl_power_gating_status_0_map[] = {
	{"PMC_PGD0_PG_STS",			BIT(0)},
	{"DMI_PGD0_PG_STS",			BIT(1)},
	{"ESPISPI_PGD0_PG_STS",			BIT(2)},
	{"XHCI_PGD0_PG_STS",			BIT(3)},
	{"SPA_PGD0_PG_STS",			BIT(4)},
	{"SPB_PGD0_PG_STS",			BIT(5)},
	{"SPC_PGD0_PG_STS",			BIT(6)},
	{"GBE_PGD0_PG_STS",			BIT(7)},
	{"SATA_PGD0_PG_STS",			BIT(8)},
	{"DSP_PGD0_PG_STS",			BIT(9)},
	{"DSP_PGD1_PG_STS",			BIT(10)},
	{"DSP_PGD2_PG_STS",			BIT(11)},
	{"DSP_PGD3_PG_STS",			BIT(12)},
	{"SPD_PGD0_PG_STS",			BIT(13)},
	{"LPSS_PGD0_PG_STS",			BIT(14)},
	{"SMB_PGD0_PG_STS",			BIT(16)},
	{"ISH_PGD0_PG_STS",			BIT(17)},
	{"NPK_PGD0_PG_STS",			BIT(19)},
	{"PECI_PGD0_PG_STS",			BIT(21)},
	{"XDCI_PGD0_PG_STS",			BIT(25)},
	{"EXI_PGD0_PG_STS",			BIT(26)},
	{"CSE_PGD0_PG_STS",			BIT(27)},
	{"KVMCC_PGD0_PG_STS",			BIT(28)},
	{"PMT_PGD0_PG_STS",			BIT(29)},
	{"CLINK_PGD0_PG_STS",			BIT(30)},
	{"PTIO_PGD0_PG_STS",			BIT(31)},
	{}
};

const struct pmc_bit_map adl_power_gating_status_1_map[] = {
	{"USBR0_PGD0_PG_STS",			BIT(0)},
	{"SMT1_PGD0_PG_STS",			BIT(2)},
	{"CSMERTC_PGD0_PG_STS",			BIT(6)},
	{"CSMEPSF_PGD0_PG_STS",			BIT(7)},
	{"CNVI_PGD0_PG_STS",			BIT(19)},
	{"DSP_PGD4_PG_STS",			BIT(26)},
	{"SPG_PGD0_PG_STS",			BIT(27)},
	{"SPE_PGD0_PG_STS",			BIT(28)},
	{}
};

const struct pmc_bit_map adl_power_gating_status_2_map[] = {
	{"THC0_PGD0_PG_STS",			BIT(7)},
	{"THC1_PGD0_PG_STS",			BIT(8)},
	{"SPF_PGD0_PG_STS",			BIT(14)},
	{}
};

const struct pmc_bit_map adl_d3_status_0_map[] = {
	{"ISH_D3_STS",				BIT(2)},
	{"LPSS_D3_STS",				BIT(3)},
	{"XDCI_D3_STS",				BIT(4)},
	{"XHCI_D3_STS",				BIT(5)},
	{"SPA_D3_STS",				BIT(12)},
	{"SPB_D3_STS",				BIT(13)},
	{"SPC_D3_STS",				BIT(14)},
	{"SPD_D3_STS",				BIT(15)},
	{"SPE_D3_STS",				BIT(16)},
	{"DSP_D3_STS",				BIT(19)},
	{"SATA_D3_STS",				BIT(20)},
	{"DMI_D3_STS",				BIT(22)},
	{}
};

const struct pmc_bit_map adl_d3_status_1_map[] = {
	{"GBE_D3_STS",				BIT(19)},
	{"CNVI_D3_STS",				BIT(27)},
	{}
};

const struct pmc_bit_map adl_d3_status_2_map[] = {
	{"CSMERTC_D3_STS",			BIT(1)},
	{"CSE_D3_STS",				BIT(4)},
	{"KVMCC_D3_STS",			BIT(5)},
	{"USBR0_D3_STS",			BIT(6)},
	{"SMT1_D3_STS",				BIT(8)},
	{"PTIO_D3_STS",				BIT(16)},
	{"PMT_D3_STS",				BIT(17)},
	{}
};

const struct pmc_bit_map adl_d3_status_3_map[] = {
	{"THC0_D3_STS",				BIT(14)},
	{"THC1_D3_STS",				BIT(15)},
	{}
};

const struct pmc_bit_map adl_vnn_req_status_0_map[] = {
	{"ISH_VNN_REQ_STS",			BIT(2)},
	{"ESPISPI_VNN_REQ_STS",			BIT(18)},
	{"DSP_VNN_REQ_STS",			BIT(19)},
	{}
};

const struct pmc_bit_map adl_vnn_req_status_1_map[] = {
	{"NPK_VNN_REQ_STS",			BIT(4)},
	{"EXI_VNN_REQ_STS",			BIT(9)},
	{"GBE_VNN_REQ_STS",			BIT(19)},
	{"SMB_VNN_REQ_STS",			BIT(25)},
	{"CNVI_VNN_REQ_STS",			BIT(27)},
	{}
};

const struct pmc_bit_map adl_vnn_req_status_2_map[] = {
	{"CSMERTC_VNN_REQ_STS",			BIT(1)},
	{"CSE_VNN_REQ_STS",			BIT(4)},
	{"SMT1_VNN_REQ_STS",			BIT(8)},
	{"CLINK_VNN_REQ_STS",			BIT(14)},
	{"GPIOCOM4_VNN_REQ_STS",		BIT(20)},
	{"GPIOCOM3_VNN_REQ_STS",		BIT(21)},
	{"GPIOCOM2_VNN_REQ_STS",		BIT(22)},
	{"GPIOCOM1_VNN_REQ_STS",		BIT(23)},
	{"GPIOCOM0_VNN_REQ_STS",		BIT(24)},
	{}
};

const struct pmc_bit_map adl_vnn_req_status_3_map[] = {
	{"GPIOCOM5_VNN_REQ_STS",		BIT(11)},
	{}
};

const struct pmc_bit_map adl_vnn_misc_status_map[] = {
	{"CPU_C10_REQ_STS",			BIT(0)},
	{"PCIe_LPM_En_REQ_STS",			BIT(3)},
	{"ITH_REQ_STS",				BIT(5)},
	{"CNVI_REQ_STS",			BIT(6)},
	{"ISH_REQ_STS",				BIT(7)},
	{"USB2_SUS_PG_Sys_REQ_STS",		BIT(10)},
	{"PCIe_Clk_REQ_STS",			BIT(12)},
	{"MPHY_Core_DL_REQ_STS",		BIT(16)},
	{"Break-even_En_REQ_STS",		BIT(17)},
	{"MPHY_SUS_REQ_STS",			BIT(22)},
	{"xDCI_attached_REQ_STS",		BIT(24)},
	{}
};

const struct pmc_bit_map *adl_lpm_maps[] = {
	adl_clocksource_status_map,
	adl_power_gating_status_0_map,
	adl_power_gating_status_1_map,
	adl_power_gating_status_2_map,
	adl_d3_status_0_map,
	adl_d3_status_1_map,
	adl_d3_status_2_map,
	adl_d3_status_3_map,
	adl_vnn_req_status_0_map,
	adl_vnn_req_status_1_map,
	adl_vnn_req_status_2_map,
	adl_vnn_req_status_3_map,
	adl_vnn_misc_status_map,
	tgl_signal_status_map,
	NULL
};

const struct pmc_reg_map adl_reg_map = {
	.pfear_sts = ext_adl_pfear_map,
	.slp_s0_offset = ADL_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slp_s0_res_counter_step = TGL_PMC_SLP_S0_RES_COUNTER_STEP,
	.ltr_show_sts = adl_ltr_show_map,
	.msr_sts = msr_map,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = CNP_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = CNP_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
	.ltr_ignore_max = ADL_NUM_IP_IGN_ALLOWED,
	.lpm_num_modes = ADL_LPM_NUM_MODES,
	.lpm_num_maps = ADL_LPM_NUM_MAPS,
	.lpm_res_counter_step_x2 = TGL_PMC_LPM_RES_COUNTER_STEP_X2,
	.etr3_offset = ETR3_OFFSET,
	.lpm_sts_latch_en_offset = ADL_LPM_STATUS_LATCH_EN_OFFSET,
	.lpm_priority_offset = ADL_LPM_PRI_OFFSET,
	.lpm_en_offset = ADL_LPM_EN_OFFSET,
	.lpm_residency_offset = ADL_LPM_RESIDENCY_OFFSET,
	.lpm_sts = adl_lpm_maps,
	.lpm_status_offset = ADL_LPM_STATUS_OFFSET,
	.lpm_live_status_offset = ADL_LPM_LIVE_STATUS_OFFSET,
};

int adl_core_init(struct pmc_dev *pmcdev)
{
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_MAIN];
	int ret;

	pmc->map = &adl_reg_map;
	ret = get_primary_reg_base(pmc);
	if (ret)
		return ret;

	/* Due to a hardware limitation, the GBE LTR blocks PC10
	 * when a cable is attached. Tell the PMC to ignore it.
	 */
	dev_dbg(&pmcdev->pdev->dev, "ignoring GBE LTR\n");
	pmc_core_send_ltr_ignore(pmcdev, 3);

	return 0;
}
