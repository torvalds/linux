// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Core SoC Power Management Controller Driver
 *
 * Copyright (c) 2016, Intel Corporation.
 * All Rights Reserved.
 *
 * Authors: Rajneesh Bhardwaj <rajneesh.bhardwaj@intel.com>
 *          Vishwanath Somayaji <vishwanath.somayaji@intel.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/uaccess.h>
#include <linux/uuid.h>

#include <acpi/acpi_bus.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/msr.h>
#include <asm/tsc.h>

#include "core.h"

#define ACPI_S0IX_DSM_UUID		"57a6512e-3979-4e9d-9708-ff13b2508972"
#define ACPI_GET_LOW_MODE_REGISTERS	1

/* PKGC MSRs are common across Intel Core SoCs */
static const struct pmc_bit_map msr_map[] = {
	{"Package C2",                  MSR_PKG_C2_RESIDENCY},
	{"Package C3",                  MSR_PKG_C3_RESIDENCY},
	{"Package C6",                  MSR_PKG_C6_RESIDENCY},
	{"Package C7",                  MSR_PKG_C7_RESIDENCY},
	{"Package C8",                  MSR_PKG_C8_RESIDENCY},
	{"Package C9",                  MSR_PKG_C9_RESIDENCY},
	{"Package C10",                 MSR_PKG_C10_RESIDENCY},
	{}
};

static const struct pmc_bit_map spt_pll_map[] = {
	{"MIPI PLL",			SPT_PMC_BIT_MPHY_CMN_LANE0},
	{"GEN2 USB2PCIE2 PLL",		SPT_PMC_BIT_MPHY_CMN_LANE1},
	{"DMIPCIE3 PLL",		SPT_PMC_BIT_MPHY_CMN_LANE2},
	{"SATA PLL",			SPT_PMC_BIT_MPHY_CMN_LANE3},
	{}
};

static const struct pmc_bit_map spt_mphy_map[] = {
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

static const struct pmc_bit_map spt_pfear_map[] = {
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

static const struct pmc_bit_map *ext_spt_pfear_map[] = {
	/*
	 * Check intel_pmc_core_ids[] users of spt_reg_map for
	 * a list of core SoCs using this.
	 */
	spt_pfear_map,
	NULL
};

static const struct pmc_bit_map spt_ltr_show_map[] = {
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

static const struct pmc_reg_map spt_reg_map = {
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

/* Cannon Lake: PGD PFET Enable Ack Status Register(s) bitmap */
static const struct pmc_bit_map cnp_pfear_map[] = {
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

static const struct pmc_bit_map *ext_cnp_pfear_map[] = {
	/*
	 * Check intel_pmc_core_ids[] users of cnp_reg_map for
	 * a list of core SoCs using this.
	 */
	cnp_pfear_map,
	NULL
};

static const struct pmc_bit_map icl_pfear_map[] = {
	{"RES_65",		BIT(0)},
	{"RES_66",		BIT(1)},
	{"RES_67",		BIT(2)},
	{"TAM",			BIT(3)},
	{"GBETSN",		BIT(4)},
	{"TBTLSX",		BIT(5)},
	{"RES_71",		BIT(6)},
	{"RES_72",		BIT(7)},
	{}
};

static const struct pmc_bit_map *ext_icl_pfear_map[] = {
	/*
	 * Check intel_pmc_core_ids[] users of icl_reg_map for
	 * a list of core SoCs using this.
	 */
	cnp_pfear_map,
	icl_pfear_map,
	NULL
};

static const struct pmc_bit_map tgl_pfear_map[] = {
	{"PSF9",		BIT(0)},
	{"RES_66",		BIT(1)},
	{"RES_67",		BIT(2)},
	{"RES_68",		BIT(3)},
	{"RES_69",		BIT(4)},
	{"RES_70",		BIT(5)},
	{"TBTLSX",		BIT(6)},
	{}
};

static const struct pmc_bit_map *ext_tgl_pfear_map[] = {
	/*
	 * Check intel_pmc_core_ids[] users of tgl_reg_map for
	 * a list of core SoCs using this.
	 */
	cnp_pfear_map,
	tgl_pfear_map,
	NULL
};

static const struct pmc_bit_map cnp_slps0_dbg0_map[] = {
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

static const struct pmc_bit_map cnp_slps0_dbg1_map[] = {
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

static const struct pmc_bit_map cnp_slps0_dbg2_map[] = {
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

static const struct pmc_bit_map *cnp_slps0_dbg_maps[] = {
	cnp_slps0_dbg0_map,
	cnp_slps0_dbg1_map,
	cnp_slps0_dbg2_map,
	NULL
};

static const struct pmc_bit_map cnp_ltr_show_map[] = {
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

static const struct pmc_reg_map cnp_reg_map = {
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

static const struct pmc_reg_map icl_reg_map = {
	.pfear_sts = ext_icl_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slp_s0_res_counter_step = ICL_PMC_SLP_S0_RES_COUNTER_STEP,
	.slps0_dbg_maps = cnp_slps0_dbg_maps,
	.ltr_show_sts = cnp_ltr_show_map,
	.msr_sts = msr_map,
	.slps0_dbg_offset = CNP_PMC_SLPS0_DBG_OFFSET,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = CNP_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = ICL_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
	.ltr_ignore_max = ICL_NUM_IP_IGN_ALLOWED,
	.etr3_offset = ETR3_OFFSET,
};

static const struct pmc_bit_map tgl_clocksource_status_map[] = {
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

static const struct pmc_bit_map tgl_power_gating_status_map[] = {
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

static const struct pmc_bit_map tgl_d3_status_map[] = {
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

static const struct pmc_bit_map tgl_vnn_req_status_map[] = {
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

static const struct pmc_bit_map tgl_vnn_misc_status_map[] = {
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

static const struct pmc_bit_map tgl_signal_status_map[] = {
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

static const struct pmc_bit_map *tgl_lpm_maps[] = {
	tgl_clocksource_status_map,
	tgl_power_gating_status_map,
	tgl_d3_status_map,
	tgl_vnn_req_status_map,
	tgl_vnn_misc_status_map,
	tgl_signal_status_map,
	NULL
};

static const struct pmc_reg_map tgl_reg_map = {
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

static void pmc_core_get_tgl_lpm_reqs(struct platform_device *pdev)
{
	struct pmc_dev *pmcdev = platform_get_drvdata(pdev);
	const int num_maps = pmcdev->map->lpm_num_maps;
	u32 lpm_size = LPM_MAX_NUM_MODES * num_maps * 4;
	union acpi_object *out_obj;
	struct acpi_device *adev;
	guid_t s0ix_dsm_guid;
	u32 *lpm_req_regs, *addr;

	adev = ACPI_COMPANION(&pdev->dev);
	if (!adev)
		return;

	guid_parse(ACPI_S0IX_DSM_UUID, &s0ix_dsm_guid);

	out_obj = acpi_evaluate_dsm(adev->handle, &s0ix_dsm_guid, 0,
				    ACPI_GET_LOW_MODE_REGISTERS, NULL);
	if (out_obj && out_obj->type == ACPI_TYPE_BUFFER) {
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
	pmcdev->lpm_req_regs = lpm_req_regs;

free_acpi_obj:
	ACPI_FREE(out_obj);
}

/* Alder Lake: PGD PFET Enable Ack Status Register(s) bitmap */
static const struct pmc_bit_map adl_pfear_map[] = {
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

static const struct pmc_bit_map *ext_adl_pfear_map[] = {
	/*
	 * Check intel_pmc_core_ids[] users of cnp_reg_map for
	 * a list of core SoCs using this.
	 */
	adl_pfear_map,
	NULL
};

static const struct pmc_bit_map adl_ltr_show_map[] = {
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

static const struct pmc_bit_map adl_clocksource_status_map[] = {
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

static const struct pmc_bit_map adl_power_gating_status_0_map[] = {
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

static const struct pmc_bit_map adl_power_gating_status_1_map[] = {
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

static const struct pmc_bit_map adl_power_gating_status_2_map[] = {
	{"THC0_PGD0_PG_STS",			BIT(7)},
	{"THC1_PGD0_PG_STS",			BIT(8)},
	{"SPF_PGD0_PG_STS",			BIT(14)},
	{}
};

static const struct pmc_bit_map adl_d3_status_0_map[] = {
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

static const struct pmc_bit_map adl_d3_status_1_map[] = {
	{"GBE_D3_STS",				BIT(19)},
	{"CNVI_D3_STS",				BIT(27)},
	{}
};

static const struct pmc_bit_map adl_d3_status_2_map[] = {
	{"CSMERTC_D3_STS",			BIT(1)},
	{"CSE_D3_STS",				BIT(4)},
	{"KVMCC_D3_STS",			BIT(5)},
	{"USBR0_D3_STS",			BIT(6)},
	{"SMT1_D3_STS",				BIT(8)},
	{"PTIO_D3_STS",				BIT(16)},
	{"PMT_D3_STS",				BIT(17)},
	{}
};

static const struct pmc_bit_map adl_d3_status_3_map[] = {
	{"THC0_D3_STS",				BIT(14)},
	{"THC1_D3_STS",				BIT(15)},
	{}
};

static const struct pmc_bit_map adl_vnn_req_status_0_map[] = {
	{"ISH_VNN_REQ_STS",			BIT(2)},
	{"ESPISPI_VNN_REQ_STS",			BIT(18)},
	{"DSP_VNN_REQ_STS",			BIT(19)},
	{}
};

static const struct pmc_bit_map adl_vnn_req_status_1_map[] = {
	{"NPK_VNN_REQ_STS",			BIT(4)},
	{"EXI_VNN_REQ_STS",			BIT(9)},
	{"GBE_VNN_REQ_STS",			BIT(19)},
	{"SMB_VNN_REQ_STS",			BIT(25)},
	{"CNVI_VNN_REQ_STS",			BIT(27)},
	{}
};

static const struct pmc_bit_map adl_vnn_req_status_2_map[] = {
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

static const struct pmc_bit_map adl_vnn_req_status_3_map[] = {
	{"GPIOCOM5_VNN_REQ_STS",		BIT(11)},
	{}
};

static const struct pmc_bit_map adl_vnn_misc_status_map[] = {
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

static const struct pmc_bit_map *adl_lpm_maps[] = {
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

static const struct pmc_reg_map adl_reg_map = {
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

static inline u32 pmc_core_reg_read(struct pmc_dev *pmcdev, int reg_offset)
{
	return readl(pmcdev->regbase + reg_offset);
}

static inline void pmc_core_reg_write(struct pmc_dev *pmcdev, int reg_offset,
				      u32 val)
{
	writel(val, pmcdev->regbase + reg_offset);
}

static inline u64 pmc_core_adjust_slp_s0_step(struct pmc_dev *pmcdev, u32 value)
{
	/*
	 * ADL PCH does not have the SLP_S0 counter and LPM Residency counters are
	 * used as a workaround which uses 30.5 usec tick. All other client
	 * programs have the legacy SLP_S0 residency counter that is using the 122
	 * usec tick.
	 */
	const int lpm_adj_x2 = pmcdev->map->lpm_res_counter_step_x2;

	if (pmcdev->map == &adl_reg_map)
		return (u64)value * GET_X2_COUNTER((u64)lpm_adj_x2);
	else
		return (u64)value * pmcdev->map->slp_s0_res_counter_step;
}

static int set_etr3(struct pmc_dev *pmcdev)
{
	const struct pmc_reg_map *map = pmcdev->map;
	u32 reg;
	int err;

	if (!map->etr3_offset)
		return -EOPNOTSUPP;

	mutex_lock(&pmcdev->lock);

	/* check if CF9 is locked */
	reg = pmc_core_reg_read(pmcdev, map->etr3_offset);
	if (reg & ETR3_CF9LOCK) {
		err = -EACCES;
		goto out_unlock;
	}

	/* write CF9 global reset bit */
	reg |= ETR3_CF9GR;
	pmc_core_reg_write(pmcdev, map->etr3_offset, reg);

	reg = pmc_core_reg_read(pmcdev, map->etr3_offset);
	if (!(reg & ETR3_CF9GR)) {
		err = -EIO;
		goto out_unlock;
	}

	err = 0;

out_unlock:
	mutex_unlock(&pmcdev->lock);
	return err;
}
static umode_t etr3_is_visible(struct kobject *kobj,
				struct attribute *attr,
				int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct pmc_dev *pmcdev = dev_get_drvdata(dev);
	const struct pmc_reg_map *map = pmcdev->map;
	u32 reg;

	mutex_lock(&pmcdev->lock);
	reg = pmc_core_reg_read(pmcdev, map->etr3_offset);
	mutex_unlock(&pmcdev->lock);

	return reg & ETR3_CF9LOCK ? attr->mode & (SYSFS_PREALLOC | 0444) : attr->mode;
}

static ssize_t etr3_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct pmc_dev *pmcdev = dev_get_drvdata(dev);
	const struct pmc_reg_map *map = pmcdev->map;
	u32 reg;

	if (!map->etr3_offset)
		return -EOPNOTSUPP;

	mutex_lock(&pmcdev->lock);

	reg = pmc_core_reg_read(pmcdev, map->etr3_offset);
	reg &= ETR3_CF9GR | ETR3_CF9LOCK;

	mutex_unlock(&pmcdev->lock);

	return sysfs_emit(buf, "0x%08x", reg);
}

static ssize_t etr3_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct pmc_dev *pmcdev = dev_get_drvdata(dev);
	int err;
	u32 reg;

	err = kstrtouint(buf, 16, &reg);
	if (err)
		return err;

	/* allow only CF9 writes */
	if (reg != ETR3_CF9GR)
		return -EINVAL;

	err = set_etr3(pmcdev);
	if (err)
		return err;

	return len;
}
static DEVICE_ATTR_RW(etr3);

static struct attribute *pmc_attrs[] = {
	&dev_attr_etr3.attr,
	NULL
};

static const struct attribute_group pmc_attr_group = {
	.attrs = pmc_attrs,
	.is_visible = etr3_is_visible,
};

static const struct attribute_group *pmc_dev_groups[] = {
	&pmc_attr_group,
	NULL
};

static int pmc_core_dev_state_get(void *data, u64 *val)
{
	struct pmc_dev *pmcdev = data;
	const struct pmc_reg_map *map = pmcdev->map;
	u32 value;

	value = pmc_core_reg_read(pmcdev, map->slp_s0_offset);
	*val = pmc_core_adjust_slp_s0_step(pmcdev, value);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(pmc_core_dev_state, pmc_core_dev_state_get, NULL, "%llu\n");

static int pmc_core_check_read_lock_bit(struct pmc_dev *pmcdev)
{
	u32 value;

	value = pmc_core_reg_read(pmcdev, pmcdev->map->pm_cfg_offset);
	return value & BIT(pmcdev->map->pm_read_disable_bit);
}

static void pmc_core_slps0_display(struct pmc_dev *pmcdev, struct device *dev,
				   struct seq_file *s)
{
	const struct pmc_bit_map **maps = pmcdev->map->slps0_dbg_maps;
	const struct pmc_bit_map *map;
	int offset = pmcdev->map->slps0_dbg_offset;
	u32 data;

	while (*maps) {
		map = *maps;
		data = pmc_core_reg_read(pmcdev, offset);
		offset += 4;
		while (map->name) {
			if (dev)
				dev_info(dev, "SLP_S0_DBG: %-32s\tState: %s\n",
					map->name,
					data & map->bit_mask ? "Yes" : "No");
			if (s)
				seq_printf(s, "SLP_S0_DBG: %-32s\tState: %s\n",
					   map->name,
					   data & map->bit_mask ? "Yes" : "No");
			++map;
		}
		++maps;
	}
}

static int pmc_core_lpm_get_arr_size(const struct pmc_bit_map **maps)
{
	int idx;

	for (idx = 0; maps[idx]; idx++)
		;/* Nothing */

	return idx;
}

static void pmc_core_lpm_display(struct pmc_dev *pmcdev, struct device *dev,
				 struct seq_file *s, u32 offset,
				 const char *str,
				 const struct pmc_bit_map **maps)
{
	int index, idx, len = 32, bit_mask, arr_size;
	u32 *lpm_regs;

	arr_size = pmc_core_lpm_get_arr_size(maps);
	lpm_regs = kmalloc_array(arr_size, sizeof(*lpm_regs), GFP_KERNEL);
	if (!lpm_regs)
		return;

	for (index = 0; index < arr_size; index++) {
		lpm_regs[index] = pmc_core_reg_read(pmcdev, offset);
		offset += 4;
	}

	for (idx = 0; idx < arr_size; idx++) {
		if (dev)
			dev_info(dev, "\nLPM_%s_%d:\t0x%x\n", str, idx,
				lpm_regs[idx]);
		if (s)
			seq_printf(s, "\nLPM_%s_%d:\t0x%x\n", str, idx,
				   lpm_regs[idx]);
		for (index = 0; maps[idx][index].name && index < len; index++) {
			bit_mask = maps[idx][index].bit_mask;
			if (dev)
				dev_info(dev, "%-30s %-30d\n",
					maps[idx][index].name,
					lpm_regs[idx] & bit_mask ? 1 : 0);
			if (s)
				seq_printf(s, "%-30s %-30d\n",
					   maps[idx][index].name,
					   lpm_regs[idx] & bit_mask ? 1 : 0);
		}
	}

	kfree(lpm_regs);
}

static bool slps0_dbg_latch;

static inline u8 pmc_core_reg_read_byte(struct pmc_dev *pmcdev, int offset)
{
	return readb(pmcdev->regbase + offset);
}

static void pmc_core_display_map(struct seq_file *s, int index, int idx, int ip,
				 u8 pf_reg, const struct pmc_bit_map **pf_map)
{
	seq_printf(s, "PCH IP: %-2d - %-32s\tState: %s\n",
		   ip, pf_map[idx][index].name,
		   pf_map[idx][index].bit_mask & pf_reg ? "Off" : "On");
}

static int pmc_core_ppfear_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map **maps = pmcdev->map->pfear_sts;
	u8 pf_regs[PPFEAR_MAX_NUM_ENTRIES];
	int index, iter, idx, ip = 0;

	iter = pmcdev->map->ppfear0_offset;

	for (index = 0; index < pmcdev->map->ppfear_buckets &&
	     index < PPFEAR_MAX_NUM_ENTRIES; index++, iter++)
		pf_regs[index] = pmc_core_reg_read_byte(pmcdev, iter);

	for (idx = 0; maps[idx]; idx++) {
		for (index = 0; maps[idx][index].name &&
		     index < pmcdev->map->ppfear_buckets * 8; ip++, index++)
			pmc_core_display_map(s, index, idx, ip,
					     pf_regs[index / 8], maps);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_ppfear);

/* This function should return link status, 0 means ready */
static int pmc_core_mtpmc_link_status(struct pmc_dev *pmcdev)
{
	u32 value;

	value = pmc_core_reg_read(pmcdev, SPT_PMC_PM_STS_OFFSET);
	return value & BIT(SPT_PMC_MSG_FULL_STS_BIT);
}

static int pmc_core_send_msg(struct pmc_dev *pmcdev, u32 *addr_xram)
{
	u32 dest;
	int timeout;

	for (timeout = NUM_RETRIES; timeout > 0; timeout--) {
		if (pmc_core_mtpmc_link_status(pmcdev) == 0)
			break;
		msleep(5);
	}

	if (timeout <= 0 && pmc_core_mtpmc_link_status(pmcdev))
		return -EBUSY;

	dest = (*addr_xram & MTPMC_MASK) | (1U << 1);
	pmc_core_reg_write(pmcdev, SPT_PMC_MTPMC_OFFSET, dest);
	return 0;
}

static int pmc_core_mphy_pg_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map *map = pmcdev->map->mphy_sts;
	u32 mphy_core_reg_low, mphy_core_reg_high;
	u32 val_low, val_high;
	int index, err = 0;

	if (pmcdev->pmc_xram_read_bit) {
		seq_puts(s, "Access denied: please disable PMC_READ_DISABLE setting in BIOS.");
		return 0;
	}

	mphy_core_reg_low  = (SPT_PMC_MPHY_CORE_STS_0 << 16);
	mphy_core_reg_high = (SPT_PMC_MPHY_CORE_STS_1 << 16);

	mutex_lock(&pmcdev->lock);

	if (pmc_core_send_msg(pmcdev, &mphy_core_reg_low) != 0) {
		err = -EBUSY;
		goto out_unlock;
	}

	msleep(10);
	val_low = pmc_core_reg_read(pmcdev, SPT_PMC_MFPMC_OFFSET);

	if (pmc_core_send_msg(pmcdev, &mphy_core_reg_high) != 0) {
		err = -EBUSY;
		goto out_unlock;
	}

	msleep(10);
	val_high = pmc_core_reg_read(pmcdev, SPT_PMC_MFPMC_OFFSET);

	for (index = 0; index < 8 && map[index].name; index++) {
		seq_printf(s, "%-32s\tState: %s\n",
			   map[index].name,
			   map[index].bit_mask & val_low ? "Not power gated" :
			   "Power gated");
	}

	for (index = 8; map[index].name; index++) {
		seq_printf(s, "%-32s\tState: %s\n",
			   map[index].name,
			   map[index].bit_mask & val_high ? "Not power gated" :
			   "Power gated");
	}

out_unlock:
	mutex_unlock(&pmcdev->lock);
	return err;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_mphy_pg);

static int pmc_core_pll_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map *map = pmcdev->map->pll_sts;
	u32 mphy_common_reg, val;
	int index, err = 0;

	if (pmcdev->pmc_xram_read_bit) {
		seq_puts(s, "Access denied: please disable PMC_READ_DISABLE setting in BIOS.");
		return 0;
	}

	mphy_common_reg  = (SPT_PMC_MPHY_COM_STS_0 << 16);
	mutex_lock(&pmcdev->lock);

	if (pmc_core_send_msg(pmcdev, &mphy_common_reg) != 0) {
		err = -EBUSY;
		goto out_unlock;
	}

	/* Observed PMC HW response latency for MTPMC-MFPMC is ~10 ms */
	msleep(10);
	val = pmc_core_reg_read(pmcdev, SPT_PMC_MFPMC_OFFSET);

	for (index = 0; map[index].name ; index++) {
		seq_printf(s, "%-32s\tState: %s\n",
			   map[index].name,
			   map[index].bit_mask & val ? "Active" : "Idle");
	}

out_unlock:
	mutex_unlock(&pmcdev->lock);
	return err;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_pll);

static int pmc_core_send_ltr_ignore(struct pmc_dev *pmcdev, u32 value)
{
	const struct pmc_reg_map *map = pmcdev->map;
	u32 reg;
	int err = 0;

	mutex_lock(&pmcdev->lock);

	if (value > map->ltr_ignore_max) {
		err = -EINVAL;
		goto out_unlock;
	}

	reg = pmc_core_reg_read(pmcdev, map->ltr_ignore_offset);
	reg |= BIT(value);
	pmc_core_reg_write(pmcdev, map->ltr_ignore_offset, reg);

out_unlock:
	mutex_unlock(&pmcdev->lock);

	return err;
}

static ssize_t pmc_core_ltr_ignore_write(struct file *file,
					 const char __user *userbuf,
					 size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct pmc_dev *pmcdev = s->private;
	u32 buf_size, value;
	int err;

	buf_size = min_t(u32, count, 64);

	err = kstrtou32_from_user(userbuf, buf_size, 10, &value);
	if (err)
		return err;

	err = pmc_core_send_ltr_ignore(pmcdev, value);

	return err == 0 ? count : err;
}

static int pmc_core_ltr_ignore_show(struct seq_file *s, void *unused)
{
	return 0;
}

static int pmc_core_ltr_ignore_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmc_core_ltr_ignore_show, inode->i_private);
}

static const struct file_operations pmc_core_ltr_ignore_ops = {
	.open           = pmc_core_ltr_ignore_open,
	.read           = seq_read,
	.write          = pmc_core_ltr_ignore_write,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static void pmc_core_slps0_dbg_latch(struct pmc_dev *pmcdev, bool reset)
{
	const struct pmc_reg_map *map = pmcdev->map;
	u32 fd;

	mutex_lock(&pmcdev->lock);

	if (!reset && !slps0_dbg_latch)
		goto out_unlock;

	fd = pmc_core_reg_read(pmcdev, map->slps0_dbg_offset);
	if (reset)
		fd &= ~CNP_PMC_LATCH_SLPS0_EVENTS;
	else
		fd |= CNP_PMC_LATCH_SLPS0_EVENTS;
	pmc_core_reg_write(pmcdev, map->slps0_dbg_offset, fd);

	slps0_dbg_latch = false;

out_unlock:
	mutex_unlock(&pmcdev->lock);
}

static int pmc_core_slps0_dbg_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;

	pmc_core_slps0_dbg_latch(pmcdev, false);
	pmc_core_slps0_display(pmcdev, NULL, s);
	pmc_core_slps0_dbg_latch(pmcdev, true);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_slps0_dbg);

static u32 convert_ltr_scale(u32 val)
{
	/*
	 * As per PCIE specification supporting document
	 * ECN_LatencyTolnReporting_14Aug08.pdf the Latency
	 * Tolerance Reporting data payload is encoded in a
	 * 3 bit scale and 10 bit value fields. Values are
	 * multiplied by the indicated scale to yield an absolute time
	 * value, expressible in a range from 1 nanosecond to
	 * 2^25*(2^10-1) = 34,326,183,936 nanoseconds.
	 *
	 * scale encoding is as follows:
	 *
	 * ----------------------------------------------
	 * |scale factor	|	Multiplier (ns)	|
	 * ----------------------------------------------
	 * |	0		|	1		|
	 * |	1		|	32		|
	 * |	2		|	1024		|
	 * |	3		|	32768		|
	 * |	4		|	1048576		|
	 * |	5		|	33554432	|
	 * |	6		|	Invalid		|
	 * |	7		|	Invalid		|
	 * ----------------------------------------------
	 */
	if (val > 5) {
		pr_warn("Invalid LTR scale factor.\n");
		return 0;
	}

	return 1U << (5 * val);
}

static int pmc_core_ltr_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map *map = pmcdev->map->ltr_show_sts;
	u64 decoded_snoop_ltr, decoded_non_snoop_ltr;
	u32 ltr_raw_data, scale, val;
	u16 snoop_ltr, nonsnoop_ltr;
	int index;

	for (index = 0; map[index].name ; index++) {
		decoded_snoop_ltr = decoded_non_snoop_ltr = 0;
		ltr_raw_data = pmc_core_reg_read(pmcdev,
						 map[index].bit_mask);
		snoop_ltr = ltr_raw_data & ~MTPMC_MASK;
		nonsnoop_ltr = (ltr_raw_data >> 0x10) & ~MTPMC_MASK;

		if (FIELD_GET(LTR_REQ_NONSNOOP, ltr_raw_data)) {
			scale = FIELD_GET(LTR_DECODED_SCALE, nonsnoop_ltr);
			val = FIELD_GET(LTR_DECODED_VAL, nonsnoop_ltr);
			decoded_non_snoop_ltr = val * convert_ltr_scale(scale);
		}

		if (FIELD_GET(LTR_REQ_SNOOP, ltr_raw_data)) {
			scale = FIELD_GET(LTR_DECODED_SCALE, snoop_ltr);
			val = FIELD_GET(LTR_DECODED_VAL, snoop_ltr);
			decoded_snoop_ltr = val * convert_ltr_scale(scale);
		}

		seq_printf(s, "%-32s\tLTR: RAW: 0x%-16x\tNon-Snoop(ns): %-16llu\tSnoop(ns): %-16llu\n",
			   map[index].name, ltr_raw_data,
			   decoded_non_snoop_ltr,
			   decoded_snoop_ltr);
	}
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_ltr);

static inline u64 adjust_lpm_residency(struct pmc_dev *pmcdev, u32 offset,
				       const int lpm_adj_x2)
{
	u64 lpm_res = pmc_core_reg_read(pmcdev, offset);

	return GET_X2_COUNTER((u64)lpm_adj_x2 * lpm_res);
}

static int pmc_core_substate_res_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const int lpm_adj_x2 = pmcdev->map->lpm_res_counter_step_x2;
	u32 offset = pmcdev->map->lpm_residency_offset;
	int i, mode;

	seq_printf(s, "%-10s %-15s\n", "Substate", "Residency");

	pmc_for_each_mode(i, mode, pmcdev) {
		seq_printf(s, "%-10s %-15llu\n", pmc_lpm_modes[mode],
			   adjust_lpm_residency(pmcdev, offset + (4 * mode), lpm_adj_x2));
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_substate_res);

static int pmc_core_substate_sts_regs_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map **maps = pmcdev->map->lpm_sts;
	u32 offset = pmcdev->map->lpm_status_offset;

	pmc_core_lpm_display(pmcdev, NULL, s, offset, "STATUS", maps);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_substate_sts_regs);

static int pmc_core_substate_l_sts_regs_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map **maps = pmcdev->map->lpm_sts;
	u32 offset = pmcdev->map->lpm_live_status_offset;

	pmc_core_lpm_display(pmcdev, NULL, s, offset, "LIVE_STATUS", maps);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_substate_l_sts_regs);

static void pmc_core_substate_req_header_show(struct seq_file *s)
{
	struct pmc_dev *pmcdev = s->private;
	int i, mode;

	seq_printf(s, "%30s |", "Element");
	pmc_for_each_mode(i, mode, pmcdev)
		seq_printf(s, " %9s |", pmc_lpm_modes[mode]);

	seq_printf(s, " %9s |\n", "Status");
}

static int pmc_core_substate_req_regs_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map **maps = pmcdev->map->lpm_sts;
	const struct pmc_bit_map *map;
	const int num_maps = pmcdev->map->lpm_num_maps;
	u32 sts_offset = pmcdev->map->lpm_status_offset;
	u32 *lpm_req_regs = pmcdev->lpm_req_regs;
	int mp;

	/* Display the header */
	pmc_core_substate_req_header_show(s);

	/* Loop over maps */
	for (mp = 0; mp < num_maps; mp++) {
		u32 req_mask = 0;
		u32 lpm_status;
		int mode, idx, i, len = 32;

		/*
		 * Capture the requirements and create a mask so that we only
		 * show an element if it's required for at least one of the
		 * enabled low power modes
		 */
		pmc_for_each_mode(idx, mode, pmcdev)
			req_mask |= lpm_req_regs[mp + (mode * num_maps)];

		/* Get the last latched status for this map */
		lpm_status = pmc_core_reg_read(pmcdev, sts_offset + (mp * 4));

		/*  Loop over elements in this map */
		map = maps[mp];
		for (i = 0; map[i].name && i < len; i++) {
			u32 bit_mask = map[i].bit_mask;

			if (!(bit_mask & req_mask))
				/*
				 * Not required for any enabled states
				 * so don't display
				 */
				continue;

			/* Display the element name in the first column */
			seq_printf(s, "%30s |", map[i].name);

			/* Loop over the enabled states and display if required */
			pmc_for_each_mode(idx, mode, pmcdev) {
				if (lpm_req_regs[mp + (mode * num_maps)] & bit_mask)
					seq_printf(s, " %9s |",
						   "Required");
				else
					seq_printf(s, " %9s |", " ");
			}

			/* In Status column, show the last captured state of this agent */
			if (lpm_status & bit_mask)
				seq_printf(s, " %9s |", "Yes");
			else
				seq_printf(s, " %9s |", " ");

			seq_puts(s, "\n");
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_substate_req_regs);

static int pmc_core_lpm_latch_mode_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	bool c10;
	u32 reg;
	int idx, mode;

	reg = pmc_core_reg_read(pmcdev, pmcdev->map->lpm_sts_latch_en_offset);
	if (reg & LPM_STS_LATCH_MODE) {
		seq_puts(s, "c10");
		c10 = false;
	} else {
		seq_puts(s, "[c10]");
		c10 = true;
	}

	pmc_for_each_mode(idx, mode, pmcdev) {
		if ((BIT(mode) & reg) && !c10)
			seq_printf(s, " [%s]", pmc_lpm_modes[mode]);
		else
			seq_printf(s, " %s", pmc_lpm_modes[mode]);
	}

	seq_puts(s, " clear\n");

	return 0;
}

static ssize_t pmc_core_lpm_latch_mode_write(struct file *file,
					     const char __user *userbuf,
					     size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct pmc_dev *pmcdev = s->private;
	bool clear = false, c10 = false;
	unsigned char buf[8];
	int idx, m, mode;
	u32 reg;

	if (count > sizeof(buf) - 1)
		return -EINVAL;
	if (copy_from_user(buf, userbuf, count))
		return -EFAULT;
	buf[count] = '\0';

	/*
	 * Allowed strings are:
	 *	Any enabled substate, e.g. 'S0i2.0'
	 *	'c10'
	 *	'clear'
	 */
	mode = sysfs_match_string(pmc_lpm_modes, buf);

	/* Check string matches enabled mode */
	pmc_for_each_mode(idx, m, pmcdev)
		if (mode == m)
			break;

	if (mode != m || mode < 0) {
		if (sysfs_streq(buf, "clear"))
			clear = true;
		else if (sysfs_streq(buf, "c10"))
			c10 = true;
		else
			return -EINVAL;
	}

	if (clear) {
		mutex_lock(&pmcdev->lock);

		reg = pmc_core_reg_read(pmcdev, pmcdev->map->etr3_offset);
		reg |= ETR3_CLEAR_LPM_EVENTS;
		pmc_core_reg_write(pmcdev, pmcdev->map->etr3_offset, reg);

		mutex_unlock(&pmcdev->lock);

		return count;
	}

	if (c10) {
		mutex_lock(&pmcdev->lock);

		reg = pmc_core_reg_read(pmcdev, pmcdev->map->lpm_sts_latch_en_offset);
		reg &= ~LPM_STS_LATCH_MODE;
		pmc_core_reg_write(pmcdev, pmcdev->map->lpm_sts_latch_en_offset, reg);

		mutex_unlock(&pmcdev->lock);

		return count;
	}

	/*
	 * For LPM mode latching we set the latch enable bit and selected mode
	 * and clear everything else.
	 */
	reg = LPM_STS_LATCH_MODE | BIT(mode);
	mutex_lock(&pmcdev->lock);
	pmc_core_reg_write(pmcdev, pmcdev->map->lpm_sts_latch_en_offset, reg);
	mutex_unlock(&pmcdev->lock);

	return count;
}
DEFINE_PMC_CORE_ATTR_WRITE(pmc_core_lpm_latch_mode);

static int pmc_core_pkgc_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmcdev = s->private;
	const struct pmc_bit_map *map = pmcdev->map->msr_sts;
	u64 pcstate_count;
	int index;

	for (index = 0; map[index].name ; index++) {
		if (rdmsrl_safe(map[index].bit_mask, &pcstate_count))
			continue;

		pcstate_count *= 1000;
		do_div(pcstate_count, tsc_khz);
		seq_printf(s, "%-8s : %llu\n", map[index].name,
			   pcstate_count);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmc_core_pkgc);

static bool pmc_core_pri_verify(u32 lpm_pri, u8 *mode_order)
{
	int i, j;

	if (!lpm_pri)
		return false;
	/*
	 * Each byte contains the priority level for 2 modes (7:4 and 3:0).
	 * In a 32 bit register this allows for describing 8 modes. Store the
	 * levels and look for values out of range.
	 */
	for (i = 0; i < 8; i++) {
		int level = lpm_pri & GENMASK(3, 0);

		if (level >= LPM_MAX_NUM_MODES)
			return false;

		mode_order[i] = level;
		lpm_pri >>= 4;
	}

	/* Check that we have unique values */
	for (i = 0; i < LPM_MAX_NUM_MODES - 1; i++)
		for (j = i + 1; j < LPM_MAX_NUM_MODES; j++)
			if (mode_order[i] == mode_order[j])
				return false;

	return true;
}

static void pmc_core_get_low_power_modes(struct platform_device *pdev)
{
	struct pmc_dev *pmcdev = platform_get_drvdata(pdev);
	u8 pri_order[LPM_MAX_NUM_MODES] = LPM_DEFAULT_PRI;
	u8 mode_order[LPM_MAX_NUM_MODES];
	u32 lpm_pri;
	u32 lpm_en;
	int mode, i, p;

	/* Use LPM Maps to indicate support for substates */
	if (!pmcdev->map->lpm_num_maps)
		return;

	lpm_en = pmc_core_reg_read(pmcdev, pmcdev->map->lpm_en_offset);
	pmcdev->num_lpm_modes = hweight32(lpm_en);

	/* Read 32 bit LPM_PRI register */
	lpm_pri = pmc_core_reg_read(pmcdev, pmcdev->map->lpm_priority_offset);


	/*
	 * If lpm_pri value passes verification, then override the default
	 * modes here. Otherwise stick with the default.
	 */
	if (pmc_core_pri_verify(lpm_pri, mode_order))
		/* Get list of modes in priority order */
		for (mode = 0; mode < LPM_MAX_NUM_MODES; mode++)
			pri_order[mode_order[mode]] = mode;
	else
		dev_warn(&pdev->dev, "Assuming a default substate order for this platform\n");

	/*
	 * Loop through all modes from lowest to highest priority,
	 * and capture all enabled modes in order
	 */
	i = 0;
	for (p = LPM_MAX_NUM_MODES - 1; p >= 0; p--) {
		int mode = pri_order[p];

		if (!(BIT(mode) & lpm_en))
			continue;

		pmcdev->lpm_en_modes[i++] = mode;
	}
}

static void pmc_core_dbgfs_unregister(struct pmc_dev *pmcdev)
{
	debugfs_remove_recursive(pmcdev->dbgfs_dir);
}

static void pmc_core_dbgfs_register(struct pmc_dev *pmcdev)
{
	struct dentry *dir;

	dir = debugfs_create_dir("pmc_core", NULL);
	pmcdev->dbgfs_dir = dir;

	debugfs_create_file("slp_s0_residency_usec", 0444, dir, pmcdev,
			    &pmc_core_dev_state);

	if (pmcdev->map->pfear_sts)
		debugfs_create_file("pch_ip_power_gating_status", 0444, dir,
				    pmcdev, &pmc_core_ppfear_fops);

	debugfs_create_file("ltr_ignore", 0644, dir, pmcdev,
			    &pmc_core_ltr_ignore_ops);

	debugfs_create_file("ltr_show", 0444, dir, pmcdev, &pmc_core_ltr_fops);

	debugfs_create_file("package_cstate_show", 0444, dir, pmcdev,
			    &pmc_core_pkgc_fops);

	if (pmcdev->map->pll_sts)
		debugfs_create_file("pll_status", 0444, dir, pmcdev,
				    &pmc_core_pll_fops);

	if (pmcdev->map->mphy_sts)
		debugfs_create_file("mphy_core_lanes_power_gating_status",
				    0444, dir, pmcdev,
				    &pmc_core_mphy_pg_fops);

	if (pmcdev->map->slps0_dbg_maps) {
		debugfs_create_file("slp_s0_debug_status", 0444,
				    dir, pmcdev,
				    &pmc_core_slps0_dbg_fops);

		debugfs_create_bool("slp_s0_dbg_latch", 0644,
				    dir, &slps0_dbg_latch);
	}

	if (pmcdev->map->lpm_en_offset) {
		debugfs_create_file("substate_residencies", 0444,
				    pmcdev->dbgfs_dir, pmcdev,
				    &pmc_core_substate_res_fops);
	}

	if (pmcdev->map->lpm_status_offset) {
		debugfs_create_file("substate_status_registers", 0444,
				    pmcdev->dbgfs_dir, pmcdev,
				    &pmc_core_substate_sts_regs_fops);
		debugfs_create_file("substate_live_status_registers", 0444,
				    pmcdev->dbgfs_dir, pmcdev,
				    &pmc_core_substate_l_sts_regs_fops);
		debugfs_create_file("lpm_latch_mode", 0644,
				    pmcdev->dbgfs_dir, pmcdev,
				    &pmc_core_lpm_latch_mode_fops);
	}

	if (pmcdev->lpm_req_regs) {
		debugfs_create_file("substate_requirements", 0444,
				    pmcdev->dbgfs_dir, pmcdev,
				    &pmc_core_substate_req_regs_fops);
	}
}

static const struct x86_cpu_id intel_pmc_core_ids[] = {
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE_L,		&spt_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE,		&spt_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(KABYLAKE_L,		&spt_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(KABYLAKE,		&spt_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(CANNONLAKE_L,	&cnp_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_L,		&icl_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_NNPI,	&icl_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(COMETLAKE,		&cnp_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(COMETLAKE_L,		&cnp_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(TIGERLAKE_L,		&tgl_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(TIGERLAKE,		&tgl_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_TREMONT,	&tgl_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_TREMONT_L,	&icl_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(ROCKETLAKE,		&tgl_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE_L,		&tgl_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE_N,		&tgl_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE,		&adl_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE_P,        &tgl_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE,		&adl_reg_map),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE_S,	&adl_reg_map),
	{}
};

MODULE_DEVICE_TABLE(x86cpu, intel_pmc_core_ids);

static const struct pci_device_id pmc_pci_ids[] = {
	{ PCI_VDEVICE(INTEL, SPT_PMC_PCI_DEVICE_ID) },
	{ }
};

/*
 * This quirk can be used on those platforms where
 * the platform BIOS enforces 24Mhz crystal to shutdown
 * before PMC can assert SLP_S0#.
 */
static bool xtal_ignore;
static int quirk_xtal_ignore(const struct dmi_system_id *id)
{
	xtal_ignore = true;
	return 0;
}

static void pmc_core_xtal_ignore(struct pmc_dev *pmcdev)
{
	u32 value;

	value = pmc_core_reg_read(pmcdev, pmcdev->map->pm_vric1_offset);
	/* 24MHz Crystal Shutdown Qualification Disable */
	value |= SPT_PMC_VRIC1_XTALSDQDIS;
	/* Low Voltage Mode Enable */
	value &= ~SPT_PMC_VRIC1_SLPS0LVEN;
	pmc_core_reg_write(pmcdev, pmcdev->map->pm_vric1_offset, value);
}

static const struct dmi_system_id pmc_core_dmi_table[]  = {
	{
	.callback = quirk_xtal_ignore,
	.ident = "HP Elite x2 1013 G3",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "HP"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP Elite x2 1013 G3"),
		},
	},
	{}
};

static void pmc_core_do_dmi_quirks(struct pmc_dev *pmcdev)
{
	dmi_check_system(pmc_core_dmi_table);

	if (xtal_ignore)
		pmc_core_xtal_ignore(pmcdev);
}

static int pmc_core_probe(struct platform_device *pdev)
{
	static bool device_initialized;
	struct pmc_dev *pmcdev;
	const struct x86_cpu_id *cpu_id;
	u64 slp_s0_addr;

	if (device_initialized)
		return -ENODEV;

	pmcdev = devm_kzalloc(&pdev->dev, sizeof(*pmcdev), GFP_KERNEL);
	if (!pmcdev)
		return -ENOMEM;

	platform_set_drvdata(pdev, pmcdev);

	cpu_id = x86_match_cpu(intel_pmc_core_ids);
	if (!cpu_id)
		return -ENODEV;

	pmcdev->map = (struct pmc_reg_map *)cpu_id->driver_data;

	/*
	 * Coffee Lake has CPU ID of Kaby Lake and Cannon Lake PCH. So here
	 * Sunrisepoint PCH regmap can't be used. Use Cannon Lake PCH regmap
	 * in this case.
	 */
	if (pmcdev->map == &spt_reg_map && !pci_dev_present(pmc_pci_ids))
		pmcdev->map = &cnp_reg_map;

	if (lpit_read_residency_count_address(&slp_s0_addr)) {
		pmcdev->base_addr = PMC_BASE_ADDR_DEFAULT;

		if (page_is_ram(PHYS_PFN(pmcdev->base_addr)))
			return -ENODEV;
	} else {
		pmcdev->base_addr = slp_s0_addr - pmcdev->map->slp_s0_offset;
	}

	pmcdev->regbase = ioremap(pmcdev->base_addr,
				  pmcdev->map->regmap_length);
	if (!pmcdev->regbase)
		return -ENOMEM;

	mutex_init(&pmcdev->lock);

	pmcdev->pmc_xram_read_bit = pmc_core_check_read_lock_bit(pmcdev);
	pmc_core_get_low_power_modes(pdev);
	pmc_core_do_dmi_quirks(pmcdev);

	if (pmcdev->map == &tgl_reg_map)
		pmc_core_get_tgl_lpm_reqs(pdev);

	/*
	 * On TGL and ADL, due to a hardware limitation, the GBE LTR blocks PC10
	 * when a cable is attached. Tell the PMC to ignore it.
	 */
	if (pmcdev->map == &tgl_reg_map || pmcdev->map == &adl_reg_map) {
		dev_dbg(&pdev->dev, "ignoring GBE LTR\n");
		pmc_core_send_ltr_ignore(pmcdev, 3);
	}

	pmc_core_dbgfs_register(pmcdev);

	device_initialized = true;
	dev_info(&pdev->dev, " initialized\n");

	return 0;
}

static int pmc_core_remove(struct platform_device *pdev)
{
	struct pmc_dev *pmcdev = platform_get_drvdata(pdev);

	pmc_core_dbgfs_unregister(pmcdev);
	platform_set_drvdata(pdev, NULL);
	mutex_destroy(&pmcdev->lock);
	iounmap(pmcdev->regbase);
	return 0;
}

static bool warn_on_s0ix_failures;
module_param(warn_on_s0ix_failures, bool, 0644);
MODULE_PARM_DESC(warn_on_s0ix_failures, "Check and warn for S0ix failures");

static __maybe_unused int pmc_core_suspend(struct device *dev)
{
	struct pmc_dev *pmcdev = dev_get_drvdata(dev);

	pmcdev->check_counters = false;

	/* No warnings on S0ix failures */
	if (!warn_on_s0ix_failures)
		return 0;

	/* Check if the syspend will actually use S0ix */
	if (pm_suspend_via_firmware())
		return 0;

	/* Save PC10 residency for checking later */
	if (rdmsrl_safe(MSR_PKG_C10_RESIDENCY, &pmcdev->pc10_counter))
		return -EIO;

	/* Save S0ix residency for checking later */
	if (pmc_core_dev_state_get(pmcdev, &pmcdev->s0ix_counter))
		return -EIO;

	pmcdev->check_counters = true;
	return 0;
}

static inline bool pmc_core_is_pc10_failed(struct pmc_dev *pmcdev)
{
	u64 pc10_counter;

	if (rdmsrl_safe(MSR_PKG_C10_RESIDENCY, &pc10_counter))
		return false;

	if (pc10_counter == pmcdev->pc10_counter)
		return true;

	return false;
}

static inline bool pmc_core_is_s0ix_failed(struct pmc_dev *pmcdev)
{
	u64 s0ix_counter;

	if (pmc_core_dev_state_get(pmcdev, &s0ix_counter))
		return false;

	if (s0ix_counter == pmcdev->s0ix_counter)
		return true;

	return false;
}

static __maybe_unused int pmc_core_resume(struct device *dev)
{
	struct pmc_dev *pmcdev = dev_get_drvdata(dev);
	const struct pmc_bit_map **maps = pmcdev->map->lpm_sts;
	int offset = pmcdev->map->lpm_status_offset;

	if (!pmcdev->check_counters)
		return 0;

	if (!pmc_core_is_s0ix_failed(pmcdev))
		return 0;

	if (pmc_core_is_pc10_failed(pmcdev)) {
		/* S0ix failed because of PC10 entry failure */
		dev_info(dev, "CPU did not enter PC10!!! (PC10 cnt=0x%llx)\n",
			 pmcdev->pc10_counter);
		return 0;
	}

	/* The real interesting case - S0ix failed - lets ask PMC why. */
	dev_warn(dev, "CPU did not enter SLP_S0!!! (S0ix cnt=%llu)\n",
		 pmcdev->s0ix_counter);
	if (pmcdev->map->slps0_dbg_maps)
		pmc_core_slps0_display(pmcdev, dev, NULL);
	if (pmcdev->map->lpm_sts)
		pmc_core_lpm_display(pmcdev, dev, NULL, offset, "STATUS", maps);

	return 0;
}

static const struct dev_pm_ops pmc_core_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pmc_core_suspend, pmc_core_resume)
};

static const struct acpi_device_id pmc_core_acpi_ids[] = {
	{"INT33A1", 0}, /* _HID for Intel Power Engine, _CID PNP0D80*/
	{ }
};
MODULE_DEVICE_TABLE(acpi, pmc_core_acpi_ids);

static struct platform_driver pmc_core_driver = {
	.driver = {
		.name = "intel_pmc_core",
		.acpi_match_table = ACPI_PTR(pmc_core_acpi_ids),
		.pm = &pmc_core_pm_ops,
		.dev_groups = pmc_dev_groups,
	},
	.probe = pmc_core_probe,
	.remove = pmc_core_remove,
};

module_platform_driver(pmc_core_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel PMC Core Driver");
