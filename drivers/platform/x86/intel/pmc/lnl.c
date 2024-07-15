// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains platform specific structure definitions
 * and init function used by Meteor Lake PCH.
 *
 * Copyright (c) 2022, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include <linux/cpu.h>
#include <linux/pci.h>

#include "core.h"

const struct pmc_bit_map lnl_ltr_show_map[] = {
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

	{"ESE",			MTL_PMC_LTR_ESE},
	{"IOE_PMC",		MTL_PMC_LTR_IOE_PMC},
	{"DMI3",		ARL_PMC_LTR_DMI3},
	{"OSSE",		LNL_PMC_LTR_OSSE},

	/* Below two cannot be used for LTR_IGNORE */
	{"CURRENT_PLATFORM",	CNP_PMC_LTR_CUR_PLT},
	{"AGGREGATED_SYSTEM",	CNP_PMC_LTR_CUR_ASLT},
	{}
};

const struct pmc_bit_map lnl_power_gating_status_0_map[] = {
	{"PMC_PGD0_PG_STS",			BIT(0)},
	{"FUSE_OSSE_PGD0_PG_STS",		BIT(1)},
	{"ESPISPI_PGD0_PG_STS",			BIT(2)},
	{"XHCI_PGD0_PG_STS",			BIT(3)},
	{"SPA_PGD0_PG_STS",			BIT(4)},
	{"SPB_PGD0_PG_STS",			BIT(5)},
	{"SPR16B0_PGD0_PG_STS",			BIT(6)},
	{"GBE_PGD0_PG_STS",			BIT(7)},
	{"SBR8B7_PGD0_PG_STS",			BIT(8)},
	{"SBR8B6_PGD0_PG_STS",			BIT(9)},
	{"SBR16B1_PGD0_PG_STS",			BIT(10)},
	{"SBR8B8_PGD0_PG_STS",			BIT(11)},
	{"ESE_PGD3_PG_STS",			BIT(12)},
	{"D2D_DISP_PGD0_PG_STS",		BIT(13)},
	{"LPSS_PGD0_PG_STS",			BIT(14)},
	{"LPC_PGD0_PG_STS",			BIT(15)},
	{"SMB_PGD0_PG_STS",			BIT(16)},
	{"ISH_PGD0_PG_STS",			BIT(17)},
	{"SBR8B2_PGD0_PG_STS",			BIT(18)},
	{"NPK_PGD0_PG_STS",			BIT(19)},
	{"D2D_NOC_PGD0_PG_STS",			BIT(20)},
	{"SAFSS_PGD0_PG_STS",			BIT(21)},
	{"FUSE_PGD0_PG_STS",			BIT(22)},
	{"D2D_DISP_PGD1_PG_STS",		BIT(23)},
	{"MPFPW1_PGD0_PG_STS",			BIT(24)},
	{"XDCI_PGD0_PG_STS",			BIT(25)},
	{"EXI_PGD0_PG_STS",			BIT(26)},
	{"CSE_PGD0_PG_STS",			BIT(27)},
	{"KVMCC_PGD0_PG_STS",			BIT(28)},
	{"PMT_PGD0_PG_STS",			BIT(29)},
	{"CLINK_PGD0_PG_STS",			BIT(30)},
	{"PTIO_PGD0_PG_STS",			BIT(31)},
	{}
};

const struct pmc_bit_map lnl_power_gating_status_1_map[] = {
	{"USBR0_PGD0_PG_STS",			BIT(0)},
	{"SUSRAM_PGD0_PG_STS",			BIT(1)},
	{"SMT1_PGD0_PG_STS",			BIT(2)},
	{"U3FPW1_PGD0_PG_STS",			BIT(3)},
	{"SMS2_PGD0_PG_STS",			BIT(4)},
	{"SMS1_PGD0_PG_STS",			BIT(5)},
	{"CSMERTC_PGD0_PG_STS",			BIT(6)},
	{"CSMEPSF_PGD0_PG_STS",			BIT(7)},
	{"FIA_PG_PGD0_PG_STS",			BIT(8)},
	{"SBR16B4_PGD0_PG_STS",			BIT(9)},
	{"P2SB8B_PGD0_PG_STS",			BIT(10)},
	{"DBG_SBR_PGD0_PG_STS",			BIT(11)},
	{"SBR8B9_PGD0_PG_STS",			BIT(12)},
	{"OSSE_SMT1_PGD0_PG_STS",		BIT(13)},
	{"SBR8B10_PGD0_PG_STS",			BIT(14)},
	{"SBR16B3_PGD0_PG_STS",			BIT(15)},
	{"G5FPW1_PGD0_PG_STS",			BIT(16)},
	{"SBRG_PGD0_PG_STS",			BIT(17)},
	{"PSF4_PGD0_PG_STS",			BIT(18)},
	{"CNVI_PGD0_PG_STS",			BIT(19)},
	{"USFX2_PGD0_PG_STS",			BIT(20)},
	{"ENDBG_PGD0_PG_STS",			BIT(21)},
	{"FIACPCB_P5X4_PGD0_PG_STS",		BIT(22)},
	{"SBR8B3_PGD0_PG_STS",			BIT(23)},
	{"SBR8B0_PGD0_PG_STS",			BIT(24)},
	{"NPK_PGD1_PG_STS",			BIT(25)},
	{"OSSE_HOTHAM_PGD0_PG_STS",		BIT(26)},
	{"D2D_NOC_PGD2_PG_STS",			BIT(27)},
	{"SBR8B1_PGD0_PG_STS",			BIT(28)},
	{"PSF6_PGD0_PG_STS",			BIT(29)},
	{"PSF7_PGD0_PG_STS",			BIT(30)},
	{"FIA_U_PGD0_PG_STS",			BIT(31)},
	{}
};

const struct pmc_bit_map lnl_power_gating_status_2_map[] = {
	{"PSF8_PGD0_PG_STS",			BIT(0)},
	{"SBR16B2_PGD0_PG_STS",			BIT(1)},
	{"D2D_IPU_PGD0_PG_STS",			BIT(2)},
	{"FIACPCB_U_PGD0_PG_STS",		BIT(3)},
	{"TAM_PGD0_PG_STS",			BIT(4)},
	{"D2D_NOC_PGD1_PG_STS",			BIT(5)},
	{"TBTLSX_PGD0_PG_STS",			BIT(6)},
	{"THC0_PGD0_PG_STS",			BIT(7)},
	{"THC1_PGD0_PG_STS",			BIT(8)},
	{"PMC_PGD0_PG_STS",			BIT(9)},
	{"SBR8B5_PGD0_PG_STS",			BIT(10)},
	{"UFSPW1_PGD0_PG_STS",			BIT(11)},
	{"DBC_PGD0_PG_STS",			BIT(12)},
	{"TCSS_PGD0_PG_STS",			BIT(13)},
	{"FIA_P5X4_PGD0_PG_STS",		BIT(14)},
	{"DISP_PGA_PGD0_PG_STS",		BIT(15)},
	{"DISP_PSF_PGD0_PG_STS",		BIT(16)},
	{"PSF0_PGD0_PG_STS",			BIT(17)},
	{"P2SB16B_PGD0_PG_STS",			BIT(18)},
	{"ACE_PGD0_PG_STS",			BIT(19)},
	{"ACE_PGD1_PG_STS",			BIT(20)},
	{"ACE_PGD2_PG_STS",			BIT(21)},
	{"ACE_PGD3_PG_STS",			BIT(22)},
	{"ACE_PGD4_PG_STS",			BIT(23)},
	{"ACE_PGD5_PG_STS",			BIT(24)},
	{"ACE_PGD6_PG_STS",			BIT(25)},
	{"ACE_PGD7_PG_STS",			BIT(26)},
	{"ACE_PGD8_PG_STS",			BIT(27)},
	{"ACE_PGD9_PG_STS",			BIT(28)},
	{"ACE_PGD10_PG_STS",			BIT(29)},
	{"FIACPCB_PG_PGD0_PG_STS",		BIT(30)},
	{"OSSE_PGD0_PG_STS",			BIT(31)},
	{}
};

const struct pmc_bit_map lnl_d3_status_0_map[] = {
	{"LPSS_D3_STS",				BIT(3)},
	{"XDCI_D3_STS",				BIT(4)},
	{"XHCI_D3_STS",				BIT(5)},
	{"SPA_D3_STS",				BIT(12)},
	{"SPB_D3_STS",				BIT(13)},
	{"OSSE_D3_STS",				BIT(15)},
	{"ESPISPI_D3_STS",			BIT(18)},
	{"PSTH_D3_STS",				BIT(21)},
	{}
};

const struct pmc_bit_map lnl_d3_status_1_map[] = {
	{"OSSE_SMT1_D3_STS",			BIT(7)},
	{"GBE_D3_STS",				BIT(19)},
	{"ITSS_D3_STS",				BIT(23)},
	{"CNVI_D3_STS",				BIT(27)},
	{"UFSX2_D3_STS",			BIT(28)},
	{"OSSE_HOTHAM_D3_STS",			BIT(31)},
	{}
};

const struct pmc_bit_map lnl_d3_status_2_map[] = {
	{"ESE_D3_STS",				BIT(0)},
	{"CSMERTC_D3_STS",			BIT(1)},
	{"SUSRAM_D3_STS",			BIT(2)},
	{"CSE_D3_STS",				BIT(4)},
	{"KVMCC_D3_STS",			BIT(5)},
	{"USBR0_D3_STS",			BIT(6)},
	{"ISH_D3_STS",				BIT(7)},
	{"SMT1_D3_STS",				BIT(8)},
	{"SMT2_D3_STS",				BIT(9)},
	{"SMT3_D3_STS",				BIT(10)},
	{"OSSE_SMT2_D3_STS",			BIT(13)},
	{"CLINK_D3_STS",			BIT(14)},
	{"PTIO_D3_STS",				BIT(16)},
	{"PMT_D3_STS",				BIT(17)},
	{"SMS1_D3_STS",				BIT(18)},
	{"SMS2_D3_STS",				BIT(19)},
	{}
};

const struct pmc_bit_map lnl_d3_status_3_map[] = {
	{"THC0_D3_STS",				BIT(14)},
	{"THC1_D3_STS",				BIT(15)},
	{"OSSE_SMT3_D3_STS",			BIT(21)},
	{"ACE_D3_STS",				BIT(23)},
	{}
};

const struct pmc_bit_map lnl_vnn_req_status_0_map[] = {
	{"LPSS_VNN_REQ_STS",			BIT(3)},
	{"OSSE_VNN_REQ_STS",			BIT(15)},
	{"ESPISPI_VNN_REQ_STS",			BIT(18)},
	{}
};

const struct pmc_bit_map lnl_vnn_req_status_1_map[] = {
	{"NPK_VNN_REQ_STS",			BIT(4)},
	{"OSSE_SMT1_VNN_REQ_STS",		BIT(7)},
	{"DFXAGG_VNN_REQ_STS",			BIT(8)},
	{"EXI_VNN_REQ_STS",			BIT(9)},
	{"P2D_VNN_REQ_STS",			BIT(18)},
	{"GBE_VNN_REQ_STS",			BIT(19)},
	{"SMB_VNN_REQ_STS",			BIT(25)},
	{"LPC_VNN_REQ_STS",			BIT(26)},
	{}
};

const struct pmc_bit_map lnl_vnn_req_status_2_map[] = {
	{"eSE_VNN_REQ_STS",			BIT(0)},
	{"CSMERTC_VNN_REQ_STS",			BIT(1)},
	{"CSE_VNN_REQ_STS",			BIT(4)},
	{"ISH_VNN_REQ_STS",			BIT(7)},
	{"SMT1_VNN_REQ_STS",			BIT(8)},
	{"CLINK_VNN_REQ_STS",			BIT(14)},
	{"SMS1_VNN_REQ_STS",			BIT(18)},
	{"SMS2_VNN_REQ_STS",			BIT(19)},
	{"GPIOCOM4_VNN_REQ_STS",		BIT(20)},
	{"GPIOCOM3_VNN_REQ_STS",		BIT(21)},
	{"GPIOCOM2_VNN_REQ_STS",		BIT(22)},
	{"GPIOCOM1_VNN_REQ_STS",		BIT(23)},
	{"GPIOCOM0_VNN_REQ_STS",		BIT(24)},
	{}
};

const struct pmc_bit_map lnl_vnn_req_status_3_map[] = {
	{"DISP_SHIM_VNN_REQ_STS",		BIT(2)},
	{"DTS0_VNN_REQ_STS",			BIT(7)},
	{"GPIOCOM5_VNN_REQ_STS",		BIT(11)},
	{}
};

const struct pmc_bit_map lnl_vnn_misc_status_map[] = {
	{"CPU_C10_REQ_STS",			BIT(0)},
	{"TS_OFF_REQ_STS",			BIT(1)},
	{"PNDE_MET_REQ_STS",			BIT(2)},
	{"PCIE_DEEP_PM_REQ_STS",		BIT(3)},
	{"PMC_CLK_THROTTLE_EN_REQ_STS",		BIT(4)},
	{"NPK_VNNAON_REQ_STS",			BIT(5)},
	{"VNN_SOC_REQ_STS",			BIT(6)},
	{"ISH_VNNAON_REQ_STS",			BIT(7)},
	{"D2D_NOC_CFI_QACTIVE_REQ_STS",		BIT(8)},
	{"D2D_NOC_GPSB_QACTIVE_REQ_STS",	BIT(9)},
	{"D2D_NOC_IPU_QACTIVE_REQ_STS",		BIT(10)},
	{"PLT_GREATER_REQ_STS",			BIT(11)},
	{"PCIE_CLKREQ_REQ_STS",			BIT(12)},
	{"PMC_IDLE_FB_OCP_REQ_STS",		BIT(13)},
	{"PM_SYNC_STATES_REQ_STS",		BIT(14)},
	{"EA_REQ_STS",				BIT(15)},
	{"MPHY_CORE_OFF_REQ_STS",		BIT(16)},
	{"BRK_EV_EN_REQ_STS",			BIT(17)},
	{"AUTO_DEMO_EN_REQ_STS",		BIT(18)},
	{"ITSS_CLK_SRC_REQ_STS",		BIT(19)},
	{"LPC_CLK_SRC_REQ_STS",			BIT(20)},
	{"ARC_IDLE_REQ_STS",			BIT(21)},
	{"MPHY_SUS_REQ_STS",			BIT(22)},
	{"FIA_DEEP_PM_REQ_STS",			BIT(23)},
	{"UXD_CONNECTED_REQ_STS",		BIT(24)},
	{"ARC_INTERRUPT_WAKE_REQ_STS",	BIT(25)},
	{"D2D_NOC_DISP_DDI_QACTIVE_REQ_STS",	BIT(26)},
	{"PRE_WAKE0_REQ_STS",			BIT(27)},
	{"PRE_WAKE1_REQ_STS",			BIT(28)},
	{"PRE_WAKE2_EN_REQ_STS",		BIT(29)},
	{"WOV_REQ_STS",				BIT(30)},
	{"D2D_NOC_DISP_EDP_QACTIVE_REQ_STS_31",	BIT(31)},
	{}
};

const struct pmc_bit_map lnl_clocksource_status_map[] = {
	{"AON2_OFF_STS",			BIT(0)},
	{"AON3_OFF_STS",			BIT(1)},
	{"AON4_OFF_STS",			BIT(2)},
	{"AON5_OFF_STS",			BIT(3)},
	{"AON1_OFF_STS",			BIT(4)},
	{"MPFPW1_0_PLL_OFF_STS",		BIT(6)},
	{"USB3_PLL_OFF_STS",			BIT(8)},
	{"AON3_SPL_OFF_STS",			BIT(9)},
	{"G5FPW1_PLL_OFF_STS",			BIT(15)},
	{"XTAL_AGGR_OFF_STS",			BIT(17)},
	{"USB2_PLL_OFF_STS",			BIT(18)},
	{"SAF_PLL_OFF_STS",			BIT(19)},
	{"SE_TCSS_PLL_OFF_STS",			BIT(20)},
	{"DDI_PLL_OFF_STS",			BIT(21)},
	{"FILTER_PLL_OFF_STS",			BIT(22)},
	{"ACE_PLL_OFF_STS",			BIT(24)},
	{"FABRIC_PLL_OFF_STS",			BIT(25)},
	{"SOC_PLL_OFF_STS",			BIT(26)},
	{"REF_OFF_STS",				BIT(28)},
	{"IMG_OFF_STS",				BIT(29)},
	{"RTC_PLL_OFF_STS",			BIT(31)},
	{}
};

const struct pmc_bit_map *lnl_lpm_maps[] = {
	lnl_clocksource_status_map,
	lnl_power_gating_status_0_map,
	lnl_power_gating_status_1_map,
	lnl_power_gating_status_2_map,
	lnl_d3_status_0_map,
	lnl_d3_status_1_map,
	lnl_d3_status_2_map,
	lnl_d3_status_3_map,
	lnl_vnn_req_status_0_map,
	lnl_vnn_req_status_1_map,
	lnl_vnn_req_status_2_map,
	lnl_vnn_req_status_3_map,
	lnl_vnn_misc_status_map,
	mtl_socm_signal_status_map,
	NULL
};

const struct pmc_bit_map lnl_pfear_map[] = {
	{"PMC_0",			BIT(0)},
	{"FUSE_OSSE",			BIT(1)},
	{"ESPISPI",			BIT(2)},
	{"XHCI",			BIT(3)},
	{"SPA",				BIT(4)},
	{"SPB",				BIT(5)},
	{"SBR16B0",			BIT(6)},
	{"GBE",				BIT(7)},

	{"SBR8B7",			BIT(0)},
	{"SBR8B6",			BIT(1)},
	{"SBR16B1",			BIT(1)},
	{"SBR8B8",			BIT(2)},
	{"ESE",				BIT(3)},
	{"SBR8B10",			BIT(4)},
	{"D2D_DISP_0",			BIT(5)},
	{"LPSS",			BIT(6)},
	{"LPC",				BIT(7)},

	{"SMB",				BIT(0)},
	{"ISH",				BIT(1)},
	{"SBR8B2",			BIT(2)},
	{"NPK_0",			BIT(3)},
	{"D2D_NOC_0",			BIT(4)},
	{"SAFSS",			BIT(5)},
	{"FUSE",			BIT(6)},
	{"D2D_DISP_1",			BIT(7)},

	{"MPFPW1",			BIT(0)},
	{"XDCI",			BIT(1)},
	{"EXI",				BIT(2)},
	{"CSE",				BIT(3)},
	{"KVMCC",			BIT(4)},
	{"PMT",				BIT(5)},
	{"CLINK",			BIT(6)},
	{"PTIO",			BIT(7)},

	{"USBR",			BIT(0)},
	{"SUSRAM",			BIT(1)},
	{"SMT1",			BIT(2)},
	{"U3FPW1",			BIT(3)},
	{"SMS2",			BIT(4)},
	{"SMS1",			BIT(5)},
	{"CSMERTC",			BIT(6)},
	{"CSMEPSF",			BIT(7)},

	{"FIA_PG",			BIT(0)},
	{"SBR16B4",			BIT(1)},
	{"P2SB8B",			BIT(2)},
	{"DBG_SBR",			BIT(3)},
	{"SBR8B9",			BIT(4)},
	{"OSSE_SMT1",			BIT(5)},
	{"SBR8B10",			BIT(6)},
	{"SBR16B3",			BIT(7)},

	{"G5FPW1",			BIT(0)},
	{"SBRG",			BIT(1)},
	{"PSF4",			BIT(2)},
	{"CNVI",			BIT(3)},
	{"UFSX2",			BIT(4)},
	{"ENDBG",			BIT(5)},
	{"FIACPCB_P5X4",		BIT(6)},
	{"SBR8B3",			BIT(7)},

	{"SBR8B0",			BIT(0)},
	{"NPK_1",			BIT(1)},
	{"OSSE_HOTHAM",			BIT(2)},
	{"D2D_NOC_2",			BIT(3)},
	{"SBR8B1",			BIT(4)},
	{"PSF6",			BIT(5)},
	{"PSF7",			BIT(6)},
	{"FIA_U",			BIT(7)},

	{"PSF8",			BIT(0)},
	{"SBR16B2",			BIT(1)},
	{"D2D_IPU",			BIT(2)},
	{"FIACPCB_U",			BIT(3)},
	{"TAM",				BIT(4)},
	{"D2D_NOC_1",			BIT(5)},
	{"TBTLSX",			BIT(6)},
	{"THC0",			BIT(7)},

	{"THC1",			BIT(0)},
	{"PMC_1",			BIT(1)},
	{"SBR8B5",			BIT(2)},
	{"UFSPW1",			BIT(3)},
	{"DBC",				BIT(4)},
	{"TCSS",			BIT(5)},
	{"FIA_P5X4",			BIT(6)},
	{"DISP_PGA",			BIT(7)},

	{"DBG_PSF",			BIT(0)},
	{"PSF0",			BIT(1)},
	{"P2SB16B",			BIT(2)},
	{"ACE0",			BIT(3)},
	{"ACE1",			BIT(4)},
	{"ACE2",			BIT(5)},
	{"ACE3",			BIT(6)},
	{"ACE4",			BIT(7)},

	{"ACE5",			BIT(0)},
	{"ACE6",			BIT(1)},
	{"ACE7",			BIT(2)},
	{"ACE8",			BIT(3)},
	{"ACE9",			BIT(4)},
	{"ACE10",			BIT(5)},
	{"FIACPCB",			BIT(6)},
	{"OSSE",			BIT(7)},
	{}
};

const struct pmc_bit_map *ext_lnl_pfear_map[] = {
	lnl_pfear_map,
	NULL
};

const struct pmc_reg_map lnl_socm_reg_map = {
	.pfear_sts = ext_lnl_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slp_s0_res_counter_step = TGL_PMC_SLP_S0_RES_COUNTER_STEP,
	.ltr_show_sts = lnl_ltr_show_map,
	.msr_sts = msr_map,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = LNL_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = LNL_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
	.ltr_ignore_max = LNL_NUM_IP_IGN_ALLOWED,
	.lpm_num_maps = ADL_LPM_NUM_MAPS,
	.lpm_res_counter_step_x2 = TGL_PMC_LPM_RES_COUNTER_STEP_X2,
	.etr3_offset = ETR3_OFFSET,
	.lpm_sts_latch_en_offset = MTL_LPM_STATUS_LATCH_EN_OFFSET,
	.lpm_priority_offset = MTL_LPM_PRI_OFFSET,
	.lpm_en_offset = MTL_LPM_EN_OFFSET,
	.lpm_residency_offset = MTL_LPM_RESIDENCY_OFFSET,
	.lpm_sts = lnl_lpm_maps,
	.lpm_status_offset = MTL_LPM_STATUS_OFFSET,
	.lpm_live_status_offset = MTL_LPM_LIVE_STATUS_OFFSET,
};

#define LNL_NPU_PCI_DEV		0x643e
#define LNL_IPU_PCI_DEV		0x645d

/*
 * Set power state of select devices that do not have drivers to D3
 * so that they do not block Package C entry.
 */
static void lnl_d3_fixup(void)
{
	pmc_core_set_device_d3(LNL_IPU_PCI_DEV);
	pmc_core_set_device_d3(LNL_NPU_PCI_DEV);
}

static int lnl_resume(struct pmc_dev *pmcdev)
{
	lnl_d3_fixup();
	pmc_core_send_ltr_ignore(pmcdev, 3, 0);

	return pmc_core_resume_common(pmcdev);
}

int lnl_core_init(struct pmc_dev *pmcdev)
{
	int ret;
	struct pmc *pmc = pmcdev->pmcs[PMC_IDX_SOC];

	lnl_d3_fixup();

	pmcdev->suspend = cnl_suspend;
	pmcdev->resume = lnl_resume;

	pmc->map = &lnl_socm_reg_map;
	ret = get_primary_reg_base(pmc);
	if (ret)
		return ret;

	pmc_core_get_low_power_modes(pmcdev);

	return 0;
}
