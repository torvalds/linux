// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains platform specific structure definitions
 * and init function used by Sunrise Point PCH.
 *
 * Copyright (c) 2022, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include "core.h"

const struct pmc_bit_map spt_pll_map[] = {
	{"MIPI PLL",			SPT_PMC_BIT_MPHY_CMN_LANE0},
	{"GEN2 USB2PCIE2 PLL",		SPT_PMC_BIT_MPHY_CMN_LANE1},
	{"DMIPCIE3 PLL",		SPT_PMC_BIT_MPHY_CMN_LANE2},
	{"SATA PLL",			SPT_PMC_BIT_MPHY_CMN_LANE3},
	{}
};

const struct pmc_bit_map spt_mphy_map[] = {
	{"MPHY CORE LANE 0",           SPT_PMC_BIT_MPHY_LANE0},
	{"MPHY CORE LANE 1",           SPT_PMC_BIT_MPHY_LANE1},
	{"MPHY CORE LANE 2",           SPT_PMC_BIT_MPHY_LANE2},
	{"MPHY CORE LANE 3",           SPT_PMC_BIT_MPHY_LANE3},
	{"MPHY CORE LANE 4",           SPT_PMC_BIT_MPHY_LANE4},
	{"MPHY CORE LANE 5",           SPT_PMC_BIT_MPHY_LANE5},
	{"MPHY CORE LANE 6",           SPT_PMC_BIT_MPHY_LANE6},
	{"MPHY CORE LANE 7",           SPT_PMC_BIT_MPHY_LANE7},
	{"MPHY CORE LANE 8",           SPT_PMC_BIT_MPHY_LANE8},
	{"MPHY CORE LANE 9",           SPT_PMC_BIT_MPHY_LANE9},
	{"MPHY CORE LANE 10",          SPT_PMC_BIT_MPHY_LANE10},
	{"MPHY CORE LANE 11",          SPT_PMC_BIT_MPHY_LANE11},
	{"MPHY CORE LANE 12",          SPT_PMC_BIT_MPHY_LANE12},
	{"MPHY CORE LANE 13",          SPT_PMC_BIT_MPHY_LANE13},
	{"MPHY CORE LANE 14",          SPT_PMC_BIT_MPHY_LANE14},
	{"MPHY CORE LANE 15",          SPT_PMC_BIT_MPHY_LANE15},
	{}
};

const struct pmc_bit_map spt_pfear_map[] = {
	{"PMC",				SPT_PMC_BIT_PMC},
	{"OPI-DMI",			SPT_PMC_BIT_OPI},
	{"SPI / eSPI",			SPT_PMC_BIT_SPI},
	{"XHCI",			SPT_PMC_BIT_XHCI},
	{"SPA",				SPT_PMC_BIT_SPA},
	{"SPB",				SPT_PMC_BIT_SPB},
	{"SPC",				SPT_PMC_BIT_SPC},
	{"GBE",				SPT_PMC_BIT_GBE},
	{"SATA",			SPT_PMC_BIT_SATA},
	{"HDA-PGD0",			SPT_PMC_BIT_HDA_PGD0},
	{"HDA-PGD1",			SPT_PMC_BIT_HDA_PGD1},
	{"HDA-PGD2",			SPT_PMC_BIT_HDA_PGD2},
	{"HDA-PGD3",			SPT_PMC_BIT_HDA_PGD3},
	{"RSVD",			SPT_PMC_BIT_RSVD_0B},
	{"LPSS",			SPT_PMC_BIT_LPSS},
	{"LPC",				SPT_PMC_BIT_LPC},
	{"SMB",				SPT_PMC_BIT_SMB},
	{"ISH",				SPT_PMC_BIT_ISH},
	{"P2SB",			SPT_PMC_BIT_P2SB},
	{"DFX",				SPT_PMC_BIT_DFX},
	{"SCC",				SPT_PMC_BIT_SCC},
	{"RSVD",			SPT_PMC_BIT_RSVD_0C},
	{"FUSE",			SPT_PMC_BIT_FUSE},
	{"CAMERA",			SPT_PMC_BIT_CAMREA},
	{"RSVD",			SPT_PMC_BIT_RSVD_0D},
	{"USB3-OTG",			SPT_PMC_BIT_USB3_OTG},
	{"EXI",				SPT_PMC_BIT_EXI},
	{"CSE",				SPT_PMC_BIT_CSE},
	{"CSME_KVM",			SPT_PMC_BIT_CSME_KVM},
	{"CSME_PMT",			SPT_PMC_BIT_CSME_PMT},
	{"CSME_CLINK",			SPT_PMC_BIT_CSME_CLINK},
	{"CSME_PTIO",			SPT_PMC_BIT_CSME_PTIO},
	{"CSME_USBR",			SPT_PMC_BIT_CSME_USBR},
	{"CSME_SUSRAM",			SPT_PMC_BIT_CSME_SUSRAM},
	{"CSME_SMT",			SPT_PMC_BIT_CSME_SMT},
	{"RSVD",			SPT_PMC_BIT_RSVD_1A},
	{"CSME_SMS2",			SPT_PMC_BIT_CSME_SMS2},
	{"CSME_SMS1",			SPT_PMC_BIT_CSME_SMS1},
	{"CSME_RTC",			SPT_PMC_BIT_CSME_RTC},
	{"CSME_PSF",			SPT_PMC_BIT_CSME_PSF},
	{}
};

const struct pmc_bit_map *ext_spt_pfear_map[] = {
	/*
	 * Check intel_pmc_core_ids[] users of spt_reg_map for
	 * a list of core SoCs using this.
	 */
	spt_pfear_map,
	NULL
};

const struct pmc_bit_map spt_ltr_show_map[] = {
	{"SOUTHPORT_A",		SPT_PMC_LTR_SPA},
	{"SOUTHPORT_B",		SPT_PMC_LTR_SPB},
	{"SATA",		SPT_PMC_LTR_SATA},
	{"GIGABIT_ETHERNET",	SPT_PMC_LTR_GBE},
	{"XHCI",		SPT_PMC_LTR_XHCI},
	{"Reserved",		SPT_PMC_LTR_RESERVED},
	{"ME",			SPT_PMC_LTR_ME},
	/* EVA is Enterprise Value Add, doesn't really exist on PCH */
	{"EVA",			SPT_PMC_LTR_EVA},
	{"SOUTHPORT_C",		SPT_PMC_LTR_SPC},
	{"HD_AUDIO",		SPT_PMC_LTR_AZ},
	{"LPSS",		SPT_PMC_LTR_LPSS},
	{"SOUTHPORT_D",		SPT_PMC_LTR_SPD},
	{"SOUTHPORT_E",		SPT_PMC_LTR_SPE},
	{"CAMERA",		SPT_PMC_LTR_CAM},
	{"ESPI",		SPT_PMC_LTR_ESPI},
	{"SCC",			SPT_PMC_LTR_SCC},
	{"ISH",			SPT_PMC_LTR_ISH},
	/* Below two cannot be used for LTR_IGNORE */
	{"CURRENT_PLATFORM",	SPT_PMC_LTR_CUR_PLT},
	{"AGGREGATED_SYSTEM",	SPT_PMC_LTR_CUR_ASLT},
	{}
};

const struct pmc_reg_map spt_reg_map = {
	.pfear_sts = ext_spt_pfear_map,
	.mphy_sts = spt_mphy_map,
	.pll_sts = spt_pll_map,
	.ltr_show_sts = spt_ltr_show_map,
	.msr_sts = msr_map,
	.slp_s0_offset = SPT_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slp_s0_res_counter_step = SPT_PMC_SLP_S0_RES_COUNTER_STEP,
	.ltr_ignore_offset = SPT_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = SPT_PMC_MMIO_REG_LEN,
	.ppfear0_offset = SPT_PMC_XRAM_PPFEAR0A,
	.ppfear_buckets = SPT_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = SPT_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = SPT_PMC_READ_DISABLE_BIT,
	.ltr_ignore_max = SPT_NUM_IP_IGN_ALLOWED,
	.pm_vric1_offset = SPT_PMC_VRIC1_OFFSET,
};

void spt_core_init(struct pmc_dev *pmcdev)
{
	pmcdev->map = &spt_reg_map;
}
