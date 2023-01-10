// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains platform specific structure definitions
 * and init function used by Cannon Lake Point PCH.
 *
 * Copyright (c) 2022, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include "core.h"

/* Cannon Lake: PGD PFET Enable Ack Status Register(s) bitmap */
const struct pmc_bit_map cnp_pfear_map[] = {
	{"PMC",                 BIT(0)},
	{"OPI-DMI",             BIT(1)},
	{"SPI/eSPI",            BIT(2)},
	{"XHCI",                BIT(3)},
	{"SPA",                 BIT(4)},
	{"SPB",                 BIT(5)},
	{"SPC",                 BIT(6)},
	{"GBE",                 BIT(7)},

	{"SATA",                BIT(0)},
	{"HDA_PGD0",            BIT(1)},
	{"HDA_PGD1",            BIT(2)},
	{"HDA_PGD2",            BIT(3)},
	{"HDA_PGD3",            BIT(4)},
	{"SPD",                 BIT(5)},
	{"LPSS",                BIT(6)},
	{"LPC",                 BIT(7)},

	{"SMB",                 BIT(0)},
	{"ISH",                 BIT(1)},
	{"P2SB",                BIT(2)},
	{"NPK_VNN",             BIT(3)},
	{"SDX",                 BIT(4)},
	{"SPE",                 BIT(5)},
	{"Fuse",                BIT(6)},
	{"SBR8",		BIT(7)},

	{"CSME_FSC",            BIT(0)},
	{"USB3_OTG",            BIT(1)},
	{"EXI",                 BIT(2)},
	{"CSE",                 BIT(3)},
	{"CSME_KVM",            BIT(4)},
	{"CSME_PMT",            BIT(5)},
	{"CSME_CLINK",          BIT(6)},
	{"CSME_PTIO",           BIT(7)},

	{"CSME_USBR",           BIT(0)},
	{"CSME_SUSRAM",         BIT(1)},
	{"CSME_SMT1",           BIT(2)},
	{"CSME_SMT4",           BIT(3)},
	{"CSME_SMS2",           BIT(4)},
	{"CSME_SMS1",           BIT(5)},
	{"CSME_RTC",            BIT(6)},
	{"CSME_PSF",            BIT(7)},

	{"SBR0",                BIT(0)},
	{"SBR1",                BIT(1)},
	{"SBR2",                BIT(2)},
	{"SBR3",                BIT(3)},
	{"SBR4",                BIT(4)},
	{"SBR5",                BIT(5)},
	{"CSME_PECI",           BIT(6)},
	{"PSF1",                BIT(7)},

	{"PSF2",                BIT(0)},
	{"PSF3",                BIT(1)},
	{"PSF4",                BIT(2)},
	{"CNVI",                BIT(3)},
	{"UFS0",                BIT(4)},
	{"EMMC",                BIT(5)},
	{"SPF",			BIT(6)},
	{"SBR6",                BIT(7)},

	{"SBR7",                BIT(0)},
	{"NPK_AON",             BIT(1)},
	{"HDA_PGD4",            BIT(2)},
	{"HDA_PGD5",            BIT(3)},
	{"HDA_PGD6",            BIT(4)},
	{"PSF6",		BIT(5)},
	{"PSF7",		BIT(6)},
	{"PSF8",		BIT(7)},
	{}
};

const struct pmc_bit_map *ext_cnp_pfear_map[] = {
	/*
	 * Check intel_pmc_core_ids[] users of cnp_reg_map for
	 * a list of core SoCs using this.
	 */
	cnp_pfear_map,
	NULL
};

const struct pmc_bit_map cnp_slps0_dbg0_map[] = {
	{"AUDIO_D3",		BIT(0)},
	{"OTG_D3",		BIT(1)},
	{"XHCI_D3",		BIT(2)},
	{"LPIO_D3",		BIT(3)},
	{"SDX_D3",		BIT(4)},
	{"SATA_D3",		BIT(5)},
	{"UFS0_D3",		BIT(6)},
	{"UFS1_D3",		BIT(7)},
	{"EMMC_D3",		BIT(8)},
	{}
};

const struct pmc_bit_map cnp_slps0_dbg1_map[] = {
	{"SDIO_PLL_OFF",	BIT(0)},
	{"USB2_PLL_OFF",	BIT(1)},
	{"AUDIO_PLL_OFF",	BIT(2)},
	{"OC_PLL_OFF",		BIT(3)},
	{"MAIN_PLL_OFF",	BIT(4)},
	{"XOSC_OFF",		BIT(5)},
	{"LPC_CLKS_GATED",	BIT(6)},
	{"PCIE_CLKREQS_IDLE",	BIT(7)},
	{"AUDIO_ROSC_OFF",	BIT(8)},
	{"HPET_XOSC_CLK_REQ",	BIT(9)},
	{"PMC_ROSC_SLOW_CLK",	BIT(10)},
	{"AON2_ROSC_GATED",	BIT(11)},
	{"CLKACKS_DEASSERTED",	BIT(12)},
	{}
};

const struct pmc_bit_map cnp_slps0_dbg2_map[] = {
	{"MPHY_CORE_GATED",	BIT(0)},
	{"CSME_GATED",		BIT(1)},
	{"USB2_SUS_GATED",	BIT(2)},
	{"DYN_FLEX_IO_IDLE",	BIT(3)},
	{"GBE_NO_LINK",		BIT(4)},
	{"THERM_SEN_DISABLED",	BIT(5)},
	{"PCIE_LOW_POWER",	BIT(6)},
	{"ISH_VNNAON_REQ_ACT",	BIT(7)},
	{"ISH_VNN_REQ_ACT",	BIT(8)},
	{"CNV_VNNAON_REQ_ACT",	BIT(9)},
	{"CNV_VNN_REQ_ACT",	BIT(10)},
	{"NPK_VNNON_REQ_ACT",	BIT(11)},
	{"PMSYNC_STATE_IDLE",	BIT(12)},
	{"ALST_GT_THRES",	BIT(13)},
	{"PMC_ARC_PG_READY",	BIT(14)},
	{}
};

const struct pmc_bit_map *cnp_slps0_dbg_maps[] = {
	cnp_slps0_dbg0_map,
	cnp_slps0_dbg1_map,
	cnp_slps0_dbg2_map,
	NULL
};

const struct pmc_bit_map cnp_ltr_show_map[] = {
	{"SOUTHPORT_A",		CNP_PMC_LTR_SPA},
	{"SOUTHPORT_B",		CNP_PMC_LTR_SPB},
	{"SATA",		CNP_PMC_LTR_SATA},
	{"GIGABIT_ETHERNET",	CNP_PMC_LTR_GBE},
	{"XHCI",		CNP_PMC_LTR_XHCI},
	{"Reserved",		CNP_PMC_LTR_RESERVED},
	{"ME",			CNP_PMC_LTR_ME},
	/* EVA is Enterprise Value Add, doesn't really exist on PCH */
	{"EVA",			CNP_PMC_LTR_EVA},
	{"SOUTHPORT_C",		CNP_PMC_LTR_SPC},
	{"HD_AUDIO",		CNP_PMC_LTR_AZ},
	{"CNV",			CNP_PMC_LTR_CNV},
	{"LPSS",		CNP_PMC_LTR_LPSS},
	{"SOUTHPORT_D",		CNP_PMC_LTR_SPD},
	{"SOUTHPORT_E",		CNP_PMC_LTR_SPE},
	{"CAMERA",		CNP_PMC_LTR_CAM},
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
	{"THC0",                TGL_PMC_LTR_THC0},
	{"THC1",                TGL_PMC_LTR_THC1},
	/* Below two cannot be used for LTR_IGNORE */
	{"CURRENT_PLATFORM",	CNP_PMC_LTR_CUR_PLT},
	{"AGGREGATED_SYSTEM",	CNP_PMC_LTR_CUR_ASLT},
	{}
};

const struct pmc_reg_map cnp_reg_map = {
	.pfear_sts = ext_cnp_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slp_s0_res_counter_step = SPT_PMC_SLP_S0_RES_COUNTER_STEP,
	.slps0_dbg_maps = cnp_slps0_dbg_maps,
	.ltr_show_sts = cnp_ltr_show_map,
	.msr_sts = msr_map,
	.slps0_dbg_offset = CNP_PMC_SLPS0_DBG_OFFSET,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = CNP_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = CNP_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
	.ltr_ignore_max = CNP_NUM_IP_IGN_ALLOWED,
	.etr3_offset = ETR3_OFFSET,
};

void cnp_core_init(struct pmc_dev *pmcdev)
{
	pmcdev->map = &cnp_reg_map;
}
