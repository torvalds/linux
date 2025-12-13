// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains platform specific structure definitions
 * and init function used by Wildcat Lake PCH.
 *
 * Copyright (c) 2025, Intel Corporation.
 */

#include <linux/bits.h>
#include <linux/pci.h>

#include "core.h"

static const struct pmc_bit_map wcl_pcdn_pfear_map[] = {
	{"PMC_0",               BIT(0)},
	{"FUSE_OSSE",           BIT(1)},
	{"ESPISPI",             BIT(2)},
	{"XHCI",                BIT(3)},
	{"SPA",                 BIT(4)},
	{"RSVD",                BIT(5)},
	{"MPFPW2",              BIT(6)},
	{"GBE",                 BIT(7)},

	{"SBR16B21",            BIT(0)},
	{"SBR16B5",             BIT(1)},
	{"SBR8B1",              BIT(2)},
	{"SBR8B0",              BIT(3)},
	{"P2SB0",               BIT(4)},
	{"D2D_DISP_1",          BIT(5)},
	{"LPSS",                BIT(6)},
	{"LPC",                 BIT(7)},

	{"SMB",                 BIT(0)},
	{"ISH",                 BIT(1)},
	{"DBG_SBR16B",          BIT(2)},
	{"NPK_0",               BIT(3)},
	{"D2D_NOC_1",           BIT(4)},
	{"FIA_P",               BIT(5)},
	{"FUSE",                BIT(6)},
	{"DBG_PSF",             BIT(7)},

	{"DISP_PGA1",           BIT(0)},
	{"XDCI",                BIT(1)},
	{"EXI",                 BIT(2)},
	{"CSE",                 BIT(3)},
	{"KVMCC",               BIT(4)},
	{"PMT",                 BIT(5)},
	{"CLINK",               BIT(6)},
	{"PTIO",                BIT(7)},

	{"USBR0",               BIT(0)},
	{"SBR16B22",            BIT(1)},
	{"SMT1",                BIT(2)},
	{"MPFPW1",              BIT(3)},
	{"SMS2",                BIT(4)},
	{"SMS1",                BIT(5)},
	{"CSMERTC",             BIT(6)},
	{"CSMEPSF",             BIT(7)},

	{"D2D_NOC_0",           BIT(0)},
	{"ESE",                 BIT(1)},
	{"FIACPCB_P",           BIT(2)},
	{"RSVD",                BIT(3)},
	{"SBR8B2",              BIT(4)},
	{"OSSE_SMT1",           BIT(5)},
	{"D2D_DISP",            BIT(6)},
	{"P2SB1",               BIT(7)},

	{"U3FPW1",              BIT(0)},
	{"SBR16B3",             BIT(1)},
	{"PSF4",                BIT(2)},
	{"CNVI",                BIT(3)},
	{"UFSX2",               BIT(4)},
	{"ENDBG",               BIT(5)},
	{"DBC",                 BIT(6)},
	{"SBRG",                BIT(7)},

	{"RSVD",                BIT(0)},
	{"NPK1",                BIT(1)},
	{"SBR16B7",             BIT(2)},
	{"SBR16B4",             BIT(3)},
	{"FIA_XG",              BIT(4)},
	{"PSF6",                BIT(5)},
	{"UFSPW1",              BIT(6)},
	{"FIA_U",               BIT(7)},

	{"PSF8",                BIT(0)},
	{"PSF0",                BIT(1)},
	{"RSVD",                BIT(2)},
	{"FIACPCB_U",           BIT(3)},
	{"TAM",                 BIT(4)},
	{"SBR16B0",             BIT(5)},
	{"TBTLSX",              BIT(6)},
	{"THC0",                BIT(7)},

	{"THC1",                BIT(0)},
	{"PMC_1",               BIT(1)},
	{"FIACPCB_XG",          BIT(2)},
	{"TCSS",                BIT(3)},
	{"DISP_PGA",            BIT(4)},
	{"SBR16B20",            BIT(5)},
	{"SBR8B20",             BIT(6)},
	{"DBG_SBR",             BIT(7)},

	{"SPC",                 BIT(0)},
	{"ACE_0",               BIT(1)},
	{"ACE_1",               BIT(2)},
	{"ACE_2",               BIT(3)},
	{"ACE_3",               BIT(4)},
	{"ACE_4",               BIT(5)},
	{"ACE_5",               BIT(6)},
	{"ACE_6",               BIT(7)},

	{"ACE_7",               BIT(0)},
	{"ACE_8",               BIT(1)},
	{"ACE_9",               BIT(2)},
	{"ACE_10",              BIT(3)},
	{"SBR16B2",             BIT(4)},
	{"SBR8B4",              BIT(5)},
	{"OSSE",                BIT(6)},
	{"SBR16B1",             BIT(7)},
	{}
};

static const struct pmc_bit_map *ext_wcl_pcdn_pfear_map[] = {
	wcl_pcdn_pfear_map,
	NULL
};

static const struct pmc_bit_map wcl_pcdn_ltr_show_map[] = {
	{"SOUTHPORT_A",		CNP_PMC_LTR_SPA},
	{"RSVD",		WCL_PMC_LTR_RESERVED},
	{"SATA",		CNP_PMC_LTR_SATA},
	{"GIGABIT_ETHERNET",	CNP_PMC_LTR_GBE},
	{"XHCI",		CNP_PMC_LTR_XHCI},
	{"SOUTHPORT_F",		ADL_PMC_LTR_SPF},
	{"ME",			CNP_PMC_LTR_ME},
	{"SATA1",		CNP_PMC_LTR_EVA},
	{"SOUTHPORT_C",		CNP_PMC_LTR_SPC},
	{"HD_AUDIO",		CNP_PMC_LTR_AZ},
	{"CNV",			CNP_PMC_LTR_CNV},
	{"LPSS",		CNP_PMC_LTR_LPSS},
	{"SOUTHPORT_D",		CNP_PMC_LTR_SPD},
	{"SOUTHPORT_E",		CNP_PMC_LTR_SPE},
	{"SATA2",		PTL_PMC_LTR_SATA2},
	{"ESPI",		CNP_PMC_LTR_ESPI},
	{"SCC",			CNP_PMC_LTR_SCC},
	{"ISH",			CNP_PMC_LTR_ISH},
	{"UFSX2",		CNP_PMC_LTR_UFSX2},
	{"EMMC",		CNP_PMC_LTR_EMMC},
	{"WIGIG",		ICL_PMC_LTR_WIGIG},
	{"THC0",		TGL_PMC_LTR_THC0},
	{"THC1",		TGL_PMC_LTR_THC1},
	{"SOUTHPORT_G",		MTL_PMC_LTR_SPG},
	{"ESE",			MTL_PMC_LTR_ESE},
	{"IOE_PMC",		MTL_PMC_LTR_IOE_PMC},
	{"DMI3",		ARL_PMC_LTR_DMI3},
	{"OSSE",		LNL_PMC_LTR_OSSE},

	/* Below two cannot be used for LTR_IGNORE */
	{"CURRENT_PLATFORM",	PTL_PMC_LTR_CUR_PLT},
	{"AGGREGATED_SYSTEM",	PTL_PMC_LTR_CUR_ASLT},
	{}
};

static const struct pmc_bit_map wcl_pcdn_power_gating_status_0_map[] = {
	{"PMC_PGD0_PG_STS",              BIT(0),	0},
	{"FUSE_OSSE_PGD0_PG_STS",        BIT(1),	0},
	{"ESPISPI_PGD0_PG_STS",          BIT(2),	0},
	{"XHCI_PGD0_PG_STS",             BIT(3),	1},
	{"SPA_PGD0_PG_STS",              BIT(4),	1},
	{"RSVD_5",                       BIT(5),	0},
	{"MPFPW2_PGD0_PG_STS",           BIT(6),	0},
	{"GBE_PGD0_PG_STS",              BIT(7),	1},
	{"SBR16B21_PGD0_PG_STS",         BIT(8),	0},
	{"SBR16B5_PGD0_PG_STS",          BIT(9),	0},
	{"SBR8B1_PGD0_PG_STS",           BIT(10),	0},
	{"SBR8B0_PGD0_PG_STS",           BIT(11),	0},
	{"P2SB0_PG_STS",                 BIT(12),	1},
	{"D2D_DISP_PGD1_PG_STS",         BIT(13),	0},
	{"LPSS_PGD0_PG_STS",             BIT(14),	1},
	{"LPC_PGD0_PG_STS",              BIT(15),	0},
	{"SMB_PGD0_PG_STS",              BIT(16),	0},
	{"ISH_PGD0_PG_STS",              BIT(17),	0},
	{"DBG_SBR16B_PGD0_PG_STS",       BIT(18),	0},
	{"NPK_PGD0_PG_STS",              BIT(19),	0},
	{"D2D_NOC_PGD1_PG_STS",          BIT(20),	0},
	{"FIA_P_PGD0_PG_STS",            BIT(21),	0},
	{"FUSE_PGD0_PG_STS",             BIT(22),	0},
	{"DBG_PSF_PGD0_PG_STS",          BIT(23),	0},
	{"DISP_PGA1_PGD0_PG_STS",        BIT(24),	0},
	{"XDCI_PGD0_PG_STS",             BIT(25),	1},
	{"EXI_PGD0_PG_STS",              BIT(26),	0},
	{"CSE_PGD0_PG_STS",              BIT(27),	1},
	{"KVMCC_PGD0_PG_STS",            BIT(28),	1},
	{"PMT_PGD0_PG_STS",              BIT(29),	1},
	{"CLINK_PGD0_PG_STS",            BIT(30),	1},
	{"PTIO_PGD0_PG_STS",             BIT(31),	1},
	{}
};

static const struct pmc_bit_map wcl_pcdn_power_gating_status_1_map[] = {
	{"USBR0_PGD0_PG_STS",            BIT(0),	1},
	{"SBR16B22_PGD0_PG_STS",         BIT(1),	0},
	{"SMT1_PGD0_PG_STS",             BIT(2),	1},
	{"MPFPW1_PGD0_PG_STS",           BIT(3),	0},
	{"SMS2_PGD0_PG_STS",             BIT(4),	1},
	{"SMS1_PGD0_PG_STS",             BIT(5),	1},
	{"CSMERTC_PGD0_PG_STS",          BIT(6),	0},
	{"CSMEPSF_PGD0_PG_STS",          BIT(7),	0},
	{"D2D_NOC_PGD0_PG_STS",          BIT(8),	0},
	{"ESE_PGD0_PG_STS",              BIT(9),	1},
	{"FIACPCB_P_PGD0_PG_STS",        BIT(10),	0},
	{"SBR8B2_PGD0_PG_STS",           BIT(12),	0},
	{"OSSE_SMT1_PGD0_PG_STS",        BIT(13),	1},
	{"D2D_DISP_PGD0_PG_STS",         BIT(14),	0},
	{"P2SB1_PGD0_PG_STS",            BIT(15),	1},
	{"U3FPW1_PGD0_PG_STS",           BIT(16),	0},
	{"SBR16B3_PGD0_PG_STS",          BIT(17),	0},
	{"PSF4_PGD0_PG_STS",             BIT(18),	0},
	{"CNVI_PGD0_PG_STS",             BIT(19),	0},
	{"UFSX2_PGD0_PG_STS",            BIT(20),	1},
	{"ENDBG_PGD0_PG_STS",            BIT(21),	0},
	{"DBC_PGD0_PG_STS",              BIT(22),	0},
	{"SBRG_PGD0_PG_STS",             BIT(23),	0},
	{"NPK_PGD1_PG_STS",              BIT(25),	0},
	{"SBR16B7_PGD0_PG_STS",          BIT(26),	0},
	{"SBR16B4_PGD0_PG_STS",          BIT(27),	0},
	{"FIA_XG_PSF_PGD0_PG_STS",       BIT(28),	0},
	{"PSF6_PGD0_PG_STS",             BIT(29),	0},
	{"UFSPW1_PGD0_PG_STS",           BIT(30),	0},
	{"FIA_U_PGD0_PG_STS",            BIT(31),	0},
	{}
};

static const struct pmc_bit_map wcl_pcdn_power_gating_status_2_map[] = {
	{"PSF8_PGD0_PG_STS",             BIT(0),	0},
	{"PSF0_PGD0_PG_STS",             BIT(1),	0},
	{"FIACPCB_U_PGD0_PG_STS",        BIT(3),	0},
	{"TAM_PGD0_PG_STS",              BIT(4),	1},
	{"SBR16B0_PGD0_PG_STS",          BIT(5),	0},
	{"TBTLSX_PGD0_PG_STS",           BIT(6),	1},
	{"THC0_PGD0_PG_STS",             BIT(7),	1},
	{"THC1_PGD0_PG_STS",             BIT(8),	1},
	{"PMC_PGD1_PG_STS",              BIT(9),	0},
	{"FIACPCB_XG_PGD0_PG_STS",       BIT(10),	0},
	{"TCSS_PGD0_PG_STS",             BIT(11),	0},
	{"DISP_PGA_PGD0_PG_STS",         BIT(12),	0},
	{"SBR8B4_PGD0_PG_STS",           BIT(13),	0},
	{"SBR8B20_PGD0_PG_STS",          BIT(14),	0},
	{"DBG_PGD0_PG_STS",              BIT(15),	0},
	{"SPC_PGD0_PG_STS",              BIT(16),	1},
	{"ACE_PGD0_PG_STS",              BIT(17),	0},
	{"ACE_PGD1_PG_STS",              BIT(18),	0},
	{"ACE_PGD2_PG_STS",              BIT(19),	0},
	{"ACE_PGD3_PG_STS",              BIT(20),	0},
	{"ACE_PGD4_PG_STS",              BIT(21),	0},
	{"ACE_PGD5_PG_STS",              BIT(22),	0},
	{"ACE_PGD6_PG_STS",              BIT(23),	0},
	{"ACE_PGD7_PG_STS",              BIT(24),	0},
	{"ACE_PGD8_PG_STS",              BIT(25),	0},
	{"ACE_PGD9_PG_STS",              BIT(26),	0},
	{"ACE_PGD10_PG_STS",             BIT(27),	0},
	{"SBR16B2_PG_PGD0_PG_STS",       BIT(28),	0},
	{"SBR16B20_PGD0_PG_STS",         BIT(29),	0},
	{"OSSE_PGD0_PG_STS",             BIT(30),	1},
	{"SBR16B1_PGD0_PG_STS",          BIT(31),	0},
	{}
};

static const struct pmc_bit_map wcl_pcdn_d3_status_0_map[] = {
	{"LPSS_D3_STS",                  BIT(3),	1},
	{"XDCI_D3_STS",                  BIT(4),	1},
	{"XHCI_D3_STS",                  BIT(5),	1},
	{"SPA_D3_STS",                   BIT(12),	0},
	{"SPC_D3_STS",                   BIT(14),	0},
	{"OSSE_D3_STS",                  BIT(15),	0},
	{"ESPISPI_D3_STS",               BIT(18),	0},
	{"PSTH_D3_STS",                  BIT(21),	0},
	{}
};

static const struct pmc_bit_map wcl_pcdn_d3_status_1_map[] = {
	{"OSSE_SMT1_D3_STS",             BIT(16),	0},
	{"GBE_D3_STS",                   BIT(19),	0},
	{"ITSS_D3_STS",                  BIT(23),	0},
	{"CNVI_D3_STS",                  BIT(27),	0},
	{"UFSX2_D3_STS",                 BIT(28),	0},
	{}
};

static const struct pmc_bit_map wcl_pcdn_d3_status_2_map[] = {
	{"CSMERTC_D3_STS",               BIT(1),	0},
	{"ESE_D3_STS",                   BIT(2),	0},
	{"CSE_D3_STS",                   BIT(4),	0},
	{"KVMCC_D3_STS",                 BIT(5),	0},
	{"USBR0_D3_STS",                 BIT(6),	0},
	{"ISH_D3_STS",                   BIT(7),	0},
	{"SMT1_D3_STS",                  BIT(8),	0},
	{"SMT2_D3_STS",                  BIT(9),	0},
	{"SMT3_D3_STS",                  BIT(10),	0},
	{"CLINK_D3_STS",                 BIT(14),	0},
	{"PTIO_D3_STS",                  BIT(16),	0},
	{"PMT_D3_STS",                   BIT(17),	0},
	{"SMS1_D3_STS",                  BIT(18),	0},
	{"SMS2_D3_STS",                  BIT(19),	0},
	{"OSSE_SMT2_D3_STS",             BIT(22),	0},
	{}
};

static const struct pmc_bit_map wcl_pcdn_d3_status_3_map[] = {
	{"THC0_D3_STS",                  BIT(14),	1},
	{"THC1_D3_STS",                  BIT(15),	1},
	{"OSSE_SMT3_D3_STS",             BIT(16),	0},
	{"ACE_D3_STS",                   BIT(23),	0},
	{}
};

static const struct pmc_bit_map wcl_pcdn_vnn_req_status_0_map[] = {
	{"LPSS_VNN_REQ_STS",             BIT(3),	1},
	{"OSSE_VNN_REQ_STS",             BIT(15),	1},
	{"ESPISPI_VNN_REQ_STS",          BIT(18),	1},
	{}
};

static const struct pmc_bit_map wcl_pcdn_vnn_req_status_1_map[] = {
	{"NPK_VNN_REQ_STS",              BIT(4),	1},
	{"DFXAGG_VNN_REQ_STS",           BIT(8),	0},
	{"EXI_VNN_REQ_STS",              BIT(9),	1},
	{"OSSE_SMT1_VNN_REQ_STS",        BIT(16),	1},
	{"P2D_VNN_REQ_STS",              BIT(18),	1},
	{"GBE_VNN_REQ_STS",              BIT(19),	1},
	{"SMB_VNN_REQ_STS",              BIT(25),	1},
	{"LPC_VNN_REQ_STS",              BIT(26),	0},
	{}
};

static const struct pmc_bit_map wcl_pcdn_vnn_req_status_2_map[] = {
	{"CSMERTC_VNN_REQ_STS",          BIT(1),	1},
	{"ESE_VNN_REQ_STS",              BIT(2),	1},
	{"CSE_VNN_REQ_STS",              BIT(4),	1},
	{"ISH_VNN_REQ_STS",              BIT(7),	1},
	{"SMT1_VNN_REQ_STS",             BIT(8),	1},
	{"CLINK_VNN_REQ_STS",            BIT(14),	1},
	{"SMS1_VNN_REQ_STS",             BIT(18),	1},
	{"SMS2_VNN_REQ_STS",             BIT(19),	1},
	{"GPIOCOM4_VNN_REQ_STS",         BIT(20),	1},
	{"GPIOCOM3_VNN_REQ_STS",         BIT(21),	1},
	{"GPIOCOM1_VNN_REQ_STS",         BIT(23),	1},
	{"GPIOCOM0_VNN_REQ_STS",         BIT(24),	1},
	{"DISP_SHIM_VNN_REQ_STS",        BIT(31),	1},
	{}
};

static const struct pmc_bit_map wcl_pcdn_vnn_misc_status_map[] = {
	{"CPU_C10_REQ_STS",              BIT(0),	0},
	{"TS_OFF_REQ_STS",               BIT(1),	0},
	{"PNDE_MET_REQ_STS",             BIT(2),	1},
	{"FW_THROTTLE_ALLOWED_REQ_STS",  BIT(4),	0},
	{"VNN_SOC_REQ_STS",              BIT(6),	1},
	{"ISH_VNNAON_REQ_STS",           BIT(7),	0},
	{"D2D_NOC_CFI_QACTIVE_REQ_STS",	 BIT(8),	1},
	{"D2D_NOC_GPSB_QACTIVE_REQ_STS", BIT(9),	1},
	{"PLT_GREATER_REQ_STS",          BIT(11),	1},
	{"ALL_SBR_IDLE_REQ_STS",         BIT(12),	0},
	{"PMC_IDLE_FB_OCP_REQ_STS",      BIT(13),	0},
	{"PM_SYNC_STATES_REQ_STS",       BIT(14),	0},
	{"EA_REQ_STS",                   BIT(15),	0},
	{"MPHY_CORE_OFF_REQ_STS",        BIT(16),	0},
	{"BRK_EV_EN_REQ_STS",            BIT(17),	0},
	{"AUTO_DEMO_EN_REQ_STS",         BIT(18),	0},
	{"ITSS_CLK_SRC_REQ_STS",         BIT(19),	1},
	{"ARC_IDLE_REQ_STS",             BIT(21),	0},
	{"FIA_DEEP_PM_REQ_STS",          BIT(23),	0},
	{"XDCI_ATTACHED_REQ_STS",        BIT(24),	1},
	{"ARC_INTERRUPT_WAKE_REQ_STS",   BIT(25),	0},
	{"D2D_DISP_DDI_QACTIVE_REQ_STS", BIT(26),	1},
	{"PRE_WAKE0_REQ_STS",            BIT(27),	1},
	{"PRE_WAKE1_REQ_STS",            BIT(28),	1},
	{"PRE_WAKE2_REQ_STS",            BIT(29),	1},
	{}
};

static const struct pmc_bit_map wcl_pcdn_rsc_status_map[] = {
	{"Memory",		0,		1},
	{"PSF0",		0,		1},
	{"PSF6",		0,		1},
	{"PSF8",		0,		1},
	{"SAF_CFI_LINK",	0,		1},
	{"SB",			0,		1},
	{}
};

static const struct pmc_bit_map *wcl_pcdn_lpm_maps[] = {
	ptl_pcdp_clocksource_status_map,
	wcl_pcdn_power_gating_status_0_map,
	wcl_pcdn_power_gating_status_1_map,
	wcl_pcdn_power_gating_status_2_map,
	wcl_pcdn_d3_status_0_map,
	wcl_pcdn_d3_status_1_map,
	wcl_pcdn_d3_status_2_map,
	wcl_pcdn_d3_status_3_map,
	wcl_pcdn_vnn_req_status_0_map,
	wcl_pcdn_vnn_req_status_1_map,
	wcl_pcdn_vnn_req_status_2_map,
	ptl_pcdp_vnn_req_status_3_map,
	wcl_pcdn_vnn_misc_status_map,
	ptl_pcdp_signal_status_map,
	NULL
};

static const struct pmc_bit_map *wcl_pcdn_blk_maps[] = {
	wcl_pcdn_power_gating_status_0_map,
	wcl_pcdn_power_gating_status_1_map,
	wcl_pcdn_power_gating_status_2_map,
	wcl_pcdn_rsc_status_map,
	wcl_pcdn_vnn_req_status_0_map,
	wcl_pcdn_vnn_req_status_1_map,
	wcl_pcdn_vnn_req_status_2_map,
	ptl_pcdp_vnn_req_status_3_map,
	wcl_pcdn_d3_status_0_map,
	wcl_pcdn_d3_status_1_map,
	wcl_pcdn_d3_status_2_map,
	wcl_pcdn_d3_status_3_map,
	ptl_pcdp_clocksource_status_map,
	wcl_pcdn_vnn_misc_status_map,
	ptl_pcdp_signal_status_map,
	NULL
};

static const struct pmc_reg_map wcl_pcdn_reg_map = {
	.pfear_sts = ext_wcl_pcdn_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slp_s0_res_counter_step = TGL_PMC_SLP_S0_RES_COUNTER_STEP,
	.ltr_show_sts = wcl_pcdn_ltr_show_map,
	.msr_sts = msr_map,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = WCL_PCD_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = LNL_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
	.lpm_num_maps = PTL_LPM_NUM_MAPS,
	.ltr_ignore_max = LNL_NUM_IP_IGN_ALLOWED,
	.lpm_res_counter_step_x2 = TGL_PMC_LPM_RES_COUNTER_STEP_X2,
	.etr3_offset = ETR3_OFFSET,
	.lpm_sts_latch_en_offset = MTL_LPM_STATUS_LATCH_EN_OFFSET,
	.lpm_priority_offset = MTL_LPM_PRI_OFFSET,
	.lpm_en_offset = MTL_LPM_EN_OFFSET,
	.lpm_residency_offset = MTL_LPM_RESIDENCY_OFFSET,
	.lpm_sts = wcl_pcdn_lpm_maps,
	.lpm_status_offset = MTL_LPM_STATUS_OFFSET,
	.lpm_live_status_offset = MTL_LPM_LIVE_STATUS_OFFSET,
	.s0ix_blocker_maps = wcl_pcdn_blk_maps,
	.s0ix_blocker_offset = LNL_S0IX_BLOCKER_OFFSET,
};

#define WCL_NPU_PCI_DEV                0xfd3e

/*
 * Set power state of select devices that do not have drivers to D3
 * so that they do not block Package C entry.
 */
static void wcl_d3_fixup(void)
{
	pmc_core_set_device_d3(WCL_NPU_PCI_DEV);
}

static int wcl_resume(struct pmc_dev *pmcdev)
{
	wcl_d3_fixup();
	return cnl_resume(pmcdev);
}

static int wcl_core_init(struct pmc_dev *pmcdev, struct pmc_dev_info *pmc_dev_info)
{
	wcl_d3_fixup();
	return generic_core_init(pmcdev, pmc_dev_info);
}

struct pmc_dev_info wcl_pmc_dev = {
	.map = &wcl_pcdn_reg_map,
	.suspend = cnl_suspend,
	.resume = wcl_resume,
	.init = wcl_core_init,
};
