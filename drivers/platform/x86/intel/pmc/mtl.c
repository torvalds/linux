// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains platform specific structure definitions
 * and init function used by Meteor Lake PCH.
 *
 * Copyright (c) 2022, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include <linux/pci.h>
#include "core.h"

/*
 * Die Mapping to Product.
 * Product SOCDie IOEDie PCHDie
 * MTL-M   SOC-M  IOE-M  None
 * MTL-P   SOC-M  IOE-P  None
 * MTL-S   SOC-S  IOE-P  PCH-S
 */

const struct pmc_bit_map mtl_socm_pfear_map[] = {
	{"PMC",                 BIT(0)},
	{"OPI",                 BIT(1)},
	{"SPI",                 BIT(2)},
	{"XHCI",                BIT(3)},
	{"SPA",                 BIT(4)},
	{"SPB",                 BIT(5)},
	{"SPC",                 BIT(6)},
	{"GBE",                 BIT(7)},

	{"SATA",                BIT(0)},
	{"DSP0",                BIT(1)},
	{"DSP1",                BIT(2)},
	{"DSP2",                BIT(3)},
	{"DSP3",                BIT(4)},
	{"SPD",                 BIT(5)},
	{"LPSS",                BIT(6)},
	{"LPC",                 BIT(7)},

	{"SMB",                 BIT(0)},
	{"ISH",                 BIT(1)},
	{"P2SB",                BIT(2)},
	{"NPK_VNN",             BIT(3)},
	{"SDX",                 BIT(4)},
	{"SPE",                 BIT(5)},
	{"FUSE",                BIT(6)},
	{"SBR8",                BIT(7)},

	{"RSVD24",              BIT(0)},
	{"OTG",                 BIT(1)},
	{"EXI",                 BIT(2)},
	{"CSE",                 BIT(3)},
	{"CSME_KVM",            BIT(4)},
	{"CSME_PMT",            BIT(5)},
	{"CSME_CLINK",          BIT(6)},
	{"CSME_PTIO",           BIT(7)},

	{"CSME_USBR",           BIT(0)},
	{"CSME_SUSRAM",         BIT(1)},
	{"CSME_SMT1",           BIT(2)},
	{"RSVD35",              BIT(3)},
	{"CSME_SMS2",           BIT(4)},
	{"CSME_SMS",            BIT(5)},
	{"CSME_RTC",            BIT(6)},
	{"CSME_PSF",            BIT(7)},

	{"SBR0",                BIT(0)},
	{"SBR1",                BIT(1)},
	{"SBR2",                BIT(2)},
	{"SBR3",                BIT(3)},
	{"SBR4",                BIT(4)},
	{"SBR5",                BIT(5)},
	{"RSVD46",              BIT(6)},
	{"PSF1",                BIT(7)},

	{"PSF2",                BIT(0)},
	{"PSF3",                BIT(1)},
	{"PSF4",                BIT(2)},
	{"CNVI",                BIT(3)},
	{"UFSX2",               BIT(4)},
	{"EMMC",                BIT(5)},
	{"SPF",                 BIT(6)},
	{"SBR6",                BIT(7)},

	{"SBR7",                BIT(0)},
	{"NPK_AON",             BIT(1)},
	{"HDA4",                BIT(2)},
	{"HDA5",                BIT(3)},
	{"HDA6",                BIT(4)},
	{"PSF6",                BIT(5)},
	{"RSVD62",              BIT(6)},
	{"RSVD63",              BIT(7)},
	{}
};

const struct pmc_bit_map *ext_mtl_socm_pfear_map[] = {
	mtl_socm_pfear_map,
	NULL
};

const struct pmc_bit_map mtl_socm_ltr_show_map[] = {
	{"SOUTHPORT_A",		CNP_PMC_LTR_SPA},
	{"SOUTHPORT_B",		CNP_PMC_LTR_SPB},
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
	{"SATA2",		CNP_PMC_LTR_CAM},
	{"ESPI",		CNP_PMC_LTR_ESPI},
	{"SCC",			CNP_PMC_LTR_SCC},
	{"ISH",                 CNP_PMC_LTR_ISH},
	{"UFSX2",		CNP_PMC_LTR_UFSX2},
	{"EMMC",		CNP_PMC_LTR_EMMC},
	{"WIGIG",		ICL_PMC_LTR_WIGIG},
	{"THC0",		TGL_PMC_LTR_THC0},
	{"THC1",		TGL_PMC_LTR_THC1},
	{"SOUTHPORT_G",		MTL_PMC_LTR_SPG},
	{"ESE",                 MTL_PMC_LTR_ESE},
	{"IOE_PMC",		MTL_PMC_LTR_IOE_PMC},

	/* Below two cannot be used for LTR_IGNORE */
	{"CURRENT_PLATFORM",	CNP_PMC_LTR_CUR_PLT},
	{"AGGREGATED_SYSTEM",	CNP_PMC_LTR_CUR_ASLT},
	{}
};

const struct pmc_bit_map mtl_socm_clocksource_status_map[] = {
	{"AON2_OFF_STS",                 BIT(0)},
	{"AON3_OFF_STS",                 BIT(1)},
	{"AON4_OFF_STS",                 BIT(2)},
	{"AON5_OFF_STS",                 BIT(3)},
	{"AON1_OFF_STS",                 BIT(4)},
	{"XTAL_LVM_OFF_STS",             BIT(5)},
	{"MPFPW1_0_PLL_OFF_STS",         BIT(6)},
	{"MPFPW1_1_PLL_OFF_STS",         BIT(7)},
	{"USB3_PLL_OFF_STS",             BIT(8)},
	{"AON3_SPL_OFF_STS",             BIT(9)},
	{"MPFPW2_0_PLL_OFF_STS",         BIT(12)},
	{"MPFPW3_0_PLL_OFF_STS",         BIT(13)},
	{"XTAL_AGGR_OFF_STS",            BIT(17)},
	{"USB2_PLL_OFF_STS",             BIT(18)},
	{"FILTER_PLL_OFF_STS",           BIT(22)},
	{"ACE_PLL_OFF_STS",              BIT(24)},
	{"FABRIC_PLL_OFF_STS",           BIT(25)},
	{"SOC_PLL_OFF_STS",              BIT(26)},
	{"PCIFAB_PLL_OFF_STS",           BIT(27)},
	{"REF_PLL_OFF_STS",              BIT(28)},
	{"IMG_PLL_OFF_STS",              BIT(29)},
	{"RTC_PLL_OFF_STS",              BIT(31)},
	{}
};

const struct pmc_bit_map mtl_socm_power_gating_status_0_map[] = {
	{"PMC_PGD0_PG_STS",              BIT(0)},
	{"DMI_PGD0_PG_STS",              BIT(1)},
	{"ESPISPI_PGD0_PG_STS",          BIT(2)},
	{"XHCI_PGD0_PG_STS",             BIT(3)},
	{"SPA_PGD0_PG_STS",              BIT(4)},
	{"SPB_PGD0_PG_STS",              BIT(5)},
	{"SPC_PGD0_PG_STS",              BIT(6)},
	{"GBE_PGD0_PG_STS",              BIT(7)},
	{"SATA_PGD0_PG_STS",             BIT(8)},
	{"PSF13_PGD0_PG_STS",            BIT(9)},
	{"SOC_D2D_PGD3_PG_STS",          BIT(10)},
	{"MPFPW3_PGD0_PG_STS",           BIT(11)},
	{"ESE_PGD0_PG_STS",              BIT(12)},
	{"SPD_PGD0_PG_STS",              BIT(13)},
	{"LPSS_PGD0_PG_STS",             BIT(14)},
	{"LPC_PGD0_PG_STS",              BIT(15)},
	{"SMB_PGD0_PG_STS",              BIT(16)},
	{"ISH_PGD0_PG_STS",              BIT(17)},
	{"P2S_PGD0_PG_STS",              BIT(18)},
	{"NPK_PGD0_PG_STS",              BIT(19)},
	{"DBG_SBR_PGD0_PG_STS",          BIT(20)},
	{"SBRG_PGD0_PG_STS",             BIT(21)},
	{"FUSE_PGD0_PG_STS",             BIT(22)},
	{"SBR8_PGD0_PG_STS",             BIT(23)},
	{"SOC_D2D_PGD2_PG_STS",          BIT(24)},
	{"XDCI_PGD0_PG_STS",             BIT(25)},
	{"EXI_PGD0_PG_STS",              BIT(26)},
	{"CSE_PGD0_PG_STS",              BIT(27)},
	{"KVMCC_PGD0_PG_STS",            BIT(28)},
	{"PMT_PGD0_PG_STS",              BIT(29)},
	{"CLINK_PGD0_PG_STS",            BIT(30)},
	{"PTIO_PGD0_PG_STS",             BIT(31)},
	{}
};

const struct pmc_bit_map mtl_socm_power_gating_status_1_map[] = {
	{"USBR0_PGD0_PG_STS",            BIT(0)},
	{"SUSRAM_PGD0_PG_STS",           BIT(1)},
	{"SMT1_PGD0_PG_STS",             BIT(2)},
	{"FIACPCB_U_PGD0_PG_STS",        BIT(3)},
	{"SMS2_PGD0_PG_STS",             BIT(4)},
	{"SMS1_PGD0_PG_STS",             BIT(5)},
	{"CSMERTC_PGD0_PG_STS",          BIT(6)},
	{"CSMEPSF_PGD0_PG_STS",          BIT(7)},
	{"SBR0_PGD0_PG_STS",             BIT(8)},
	{"SBR1_PGD0_PG_STS",             BIT(9)},
	{"SBR2_PGD0_PG_STS",             BIT(10)},
	{"SBR3_PGD0_PG_STS",             BIT(11)},
	{"U3FPW1_PGD0_PG_STS",           BIT(12)},
	{"SBR5_PGD0_PG_STS",             BIT(13)},
	{"MPFPW1_PGD0_PG_STS",           BIT(14)},
	{"UFSPW1_PGD0_PG_STS",           BIT(15)},
	{"FIA_X_PGD0_PG_STS",            BIT(16)},
	{"SOC_D2D_PGD0_PG_STS",          BIT(17)},
	{"MPFPW2_PGD0_PG_STS",           BIT(18)},
	{"CNVI_PGD0_PG_STS",             BIT(19)},
	{"UFSX2_PGD0_PG_STS",            BIT(20)},
	{"ENDBG_PGD0_PG_STS",            BIT(21)},
	{"DBG_PSF_PGD0_PG_STS",          BIT(22)},
	{"SBR6_PGD0_PG_STS",             BIT(23)},
	{"SBR7_PGD0_PG_STS",             BIT(24)},
	{"NPK_PGD1_PG_STS",              BIT(25)},
	{"FIACPCB_X_PGD0_PG_STS",        BIT(26)},
	{"DBC_PGD0_PG_STS",              BIT(27)},
	{"FUSEGPSB_PGD0_PG_STS",         BIT(28)},
	{"PSF6_PGD0_PG_STS",             BIT(29)},
	{"PSF7_PGD0_PG_STS",             BIT(30)},
	{"GBETSN1_PGD0_PG_STS",          BIT(31)},
	{}
};

const struct pmc_bit_map mtl_socm_power_gating_status_2_map[] = {
	{"PSF8_PGD0_PG_STS",             BIT(0)},
	{"FIA_PGD0_PG_STS",              BIT(1)},
	{"SOC_D2D_PGD1_PG_STS",          BIT(2)},
	{"FIA_U_PGD0_PG_STS",            BIT(3)},
	{"TAM_PGD0_PG_STS",              BIT(4)},
	{"GBETSN_PGD0_PG_STS",           BIT(5)},
	{"TBTLSX_PGD0_PG_STS",           BIT(6)},
	{"THC0_PGD0_PG_STS",             BIT(7)},
	{"THC1_PGD0_PG_STS",             BIT(8)},
	{"PMC_PGD1_PG_STS",              BIT(9)},
	{"GNA_PGD0_PG_STS",              BIT(10)},
	{"ACE_PGD0_PG_STS",              BIT(11)},
	{"ACE_PGD1_PG_STS",              BIT(12)},
	{"ACE_PGD2_PG_STS",              BIT(13)},
	{"ACE_PGD3_PG_STS",              BIT(14)},
	{"ACE_PGD4_PG_STS",              BIT(15)},
	{"ACE_PGD5_PG_STS",              BIT(16)},
	{"ACE_PGD6_PG_STS",              BIT(17)},
	{"ACE_PGD7_PG_STS",              BIT(18)},
	{"ACE_PGD8_PG_STS",              BIT(19)},
	{"FIA_PGS_PGD0_PG_STS",          BIT(20)},
	{"FIACPCB_PGS_PGD0_PG_STS",      BIT(21)},
	{"FUSEPMSB_PGD0_PG_STS",         BIT(22)},
	{}
};

const struct pmc_bit_map mtl_socm_d3_status_0_map[] = {
	{"LPSS_D3_STS",                  BIT(3)},
	{"XDCI_D3_STS",                  BIT(4)},
	{"XHCI_D3_STS",                  BIT(5)},
	{"SPA_D3_STS",                   BIT(12)},
	{"SPB_D3_STS",                   BIT(13)},
	{"SPC_D3_STS",                   BIT(14)},
	{"SPD_D3_STS",                   BIT(15)},
	{"ESPISPI_D3_STS",               BIT(18)},
	{"SATA_D3_STS",                  BIT(20)},
	{"PSTH_D3_STS",                  BIT(21)},
	{"DMI_D3_STS",                   BIT(22)},
	{}
};

const struct pmc_bit_map mtl_socm_d3_status_1_map[] = {
	{"GBETSN1_D3_STS",               BIT(14)},
	{"GBE_D3_STS",                   BIT(19)},
	{"ITSS_D3_STS",                  BIT(23)},
	{"P2S_D3_STS",                   BIT(24)},
	{"CNVI_D3_STS",                  BIT(27)},
	{"UFSX2_D3_STS",                 BIT(28)},
	{}
};

const struct pmc_bit_map mtl_socm_d3_status_2_map[] = {
	{"GNA_D3_STS",                   BIT(0)},
	{"CSMERTC_D3_STS",               BIT(1)},
	{"SUSRAM_D3_STS",                BIT(2)},
	{"CSE_D3_STS",                   BIT(4)},
	{"KVMCC_D3_STS",                 BIT(5)},
	{"USBR0_D3_STS",                 BIT(6)},
	{"ISH_D3_STS",                   BIT(7)},
	{"SMT1_D3_STS",                  BIT(8)},
	{"SMT2_D3_STS",                  BIT(9)},
	{"SMT3_D3_STS",                  BIT(10)},
	{"CLINK_D3_STS",                 BIT(14)},
	{"PTIO_D3_STS",                  BIT(16)},
	{"PMT_D3_STS",                   BIT(17)},
	{"SMS1_D3_STS",                  BIT(18)},
	{"SMS2_D3_STS",                  BIT(19)},
	{}
};

const struct pmc_bit_map mtl_socm_d3_status_3_map[] = {
	{"ESE_D3_STS",                   BIT(2)},
	{"GBETSN_D3_STS",                BIT(13)},
	{"THC0_D3_STS",                  BIT(14)},
	{"THC1_D3_STS",                  BIT(15)},
	{"ACE_D3_STS",                   BIT(23)},
	{}
};

const struct pmc_bit_map mtl_socm_vnn_req_status_0_map[] = {
	{"LPSS_VNN_REQ_STS",             BIT(3)},
	{"FIA_VNN_REQ_STS",              BIT(17)},
	{"ESPISPI_VNN_REQ_STS",          BIT(18)},
	{}
};

const struct pmc_bit_map mtl_socm_vnn_req_status_1_map[] = {
	{"NPK_VNN_REQ_STS",              BIT(4)},
	{"DFXAGG_VNN_REQ_STS",           BIT(8)},
	{"EXI_VNN_REQ_STS",              BIT(9)},
	{"P2D_VNN_REQ_STS",              BIT(18)},
	{"GBE_VNN_REQ_STS",              BIT(19)},
	{"SMB_VNN_REQ_STS",              BIT(25)},
	{"LPC_VNN_REQ_STS",              BIT(26)},
	{}
};

const struct pmc_bit_map mtl_socm_vnn_req_status_2_map[] = {
	{"CSMERTC_VNN_REQ_STS",          BIT(1)},
	{"CSE_VNN_REQ_STS",              BIT(4)},
	{"ISH_VNN_REQ_STS",              BIT(7)},
	{"SMT1_VNN_REQ_STS",             BIT(8)},
	{"CLINK_VNN_REQ_STS",            BIT(14)},
	{"SMS1_VNN_REQ_STS",             BIT(18)},
	{"SMS2_VNN_REQ_STS",             BIT(19)},
	{"GPIOCOM4_VNN_REQ_STS",         BIT(20)},
	{"GPIOCOM3_VNN_REQ_STS",         BIT(21)},
	{"GPIOCOM2_VNN_REQ_STS",         BIT(22)},
	{"GPIOCOM1_VNN_REQ_STS",         BIT(23)},
	{"GPIOCOM0_VNN_REQ_STS",         BIT(24)},
	{}
};

const struct pmc_bit_map mtl_socm_vnn_req_status_3_map[] = {
	{"ESE_VNN_REQ_STS",              BIT(2)},
	{"DTS0_VNN_REQ_STS",             BIT(7)},
	{"GPIOCOM5_VNN_REQ_STS",         BIT(11)},
	{}
};

const struct pmc_bit_map mtl_socm_vnn_misc_status_map[] = {
	{"CPU_C10_REQ_STS",              BIT(0)},
	{"TS_OFF_REQ_STS",               BIT(1)},
	{"PNDE_MET_REQ_STS",             BIT(2)},
	{"PCIE_DEEP_PM_REQ_STS",         BIT(3)},
	{"PMC_CLK_THROTTLE_EN_REQ_STS",  BIT(4)},
	{"NPK_VNNAON_REQ_STS",           BIT(5)},
	{"VNN_SOC_REQ_STS",              BIT(6)},
	{"ISH_VNNAON_REQ_STS",           BIT(7)},
	{"IOE_COND_MET_S02I2_0_REQ_STS", BIT(8)},
	{"IOE_COND_MET_S02I2_1_REQ_STS", BIT(9)},
	{"IOE_COND_MET_S02I2_2_REQ_STS", BIT(10)},
	{"PLT_GREATER_REQ_STS",          BIT(11)},
	{"PCIE_CLKREQ_REQ_STS",          BIT(12)},
	{"PMC_IDLE_FB_OCP_REQ_STS",      BIT(13)},
	{"PM_SYNC_STATES_REQ_STS",       BIT(14)},
	{"EA_REQ_STS",                   BIT(15)},
	{"MPHY_CORE_OFF_REQ_STS",        BIT(16)},
	{"BRK_EV_EN_REQ_STS",            BIT(17)},
	{"AUTO_DEMO_EN_REQ_STS",         BIT(18)},
	{"ITSS_CLK_SRC_REQ_STS",         BIT(19)},
	{"LPC_CLK_SRC_REQ_STS",          BIT(20)},
	{"ARC_IDLE_REQ_STS",             BIT(21)},
	{"MPHY_SUS_REQ_STS",             BIT(22)},
	{"FIA_DEEP_PM_REQ_STS",          BIT(23)},
	{"UXD_CONNECTED_REQ_STS",        BIT(24)},
	{"ARC_INTERRUPT_WAKE_REQ_STS",   BIT(25)},
	{"USB2_VNNAON_ACT_REQ_STS",      BIT(26)},
	{"PRE_WAKE0_REQ_STS",            BIT(27)},
	{"PRE_WAKE1_REQ_STS",            BIT(28)},
	{"PRE_WAKE2_EN_REQ_STS",         BIT(29)},
	{"WOV_REQ_STS",                  BIT(30)},
	{"CNVI_V1P05_REQ_STS",           BIT(31)},
	{}
};

const struct pmc_bit_map mtl_socm_signal_status_map[] = {
	{"LSX_Wake0_En_STS",             BIT(0)},
	{"LSX_Wake0_Pol_STS",            BIT(1)},
	{"LSX_Wake1_En_STS",             BIT(2)},
	{"LSX_Wake1_Pol_STS",            BIT(3)},
	{"LSX_Wake2_En_STS",             BIT(4)},
	{"LSX_Wake2_Pol_STS",            BIT(5)},
	{"LSX_Wake3_En_STS",             BIT(6)},
	{"LSX_Wake3_Pol_STS",            BIT(7)},
	{"LSX_Wake4_En_STS",             BIT(8)},
	{"LSX_Wake4_Pol_STS",            BIT(9)},
	{"LSX_Wake5_En_STS",             BIT(10)},
	{"LSX_Wake5_Pol_STS",            BIT(11)},
	{"LSX_Wake6_En_STS",             BIT(12)},
	{"LSX_Wake6_Pol_STS",            BIT(13)},
	{"LSX_Wake7_En_STS",             BIT(14)},
	{"LSX_Wake7_Pol_STS",            BIT(15)},
	{"LPSS_Wake0_En_STS",            BIT(16)},
	{"LPSS_Wake0_Pol_STS",           BIT(17)},
	{"LPSS_Wake1_En_STS",            BIT(18)},
	{"LPSS_Wake1_Pol_STS",           BIT(19)},
	{"Int_Timer_SS_Wake0_En_STS",    BIT(20)},
	{"Int_Timer_SS_Wake0_Pol_STS",   BIT(21)},
	{"Int_Timer_SS_Wake1_En_STS",    BIT(22)},
	{"Int_Timer_SS_Wake1_Pol_STS",   BIT(23)},
	{"Int_Timer_SS_Wake2_En_STS",    BIT(24)},
	{"Int_Timer_SS_Wake2_Pol_STS",   BIT(25)},
	{"Int_Timer_SS_Wake3_En_STS",    BIT(26)},
	{"Int_Timer_SS_Wake3_Pol_STS",   BIT(27)},
	{"Int_Timer_SS_Wake4_En_STS",    BIT(28)},
	{"Int_Timer_SS_Wake4_Pol_STS",   BIT(29)},
	{"Int_Timer_SS_Wake5_En_STS",    BIT(30)},
	{"Int_Timer_SS_Wake5_Pol_STS",   BIT(31)},
	{}
};

const struct pmc_bit_map *mtl_socm_lpm_maps[] = {
	mtl_socm_clocksource_status_map,
	mtl_socm_power_gating_status_0_map,
	mtl_socm_power_gating_status_1_map,
	mtl_socm_power_gating_status_2_map,
	mtl_socm_d3_status_0_map,
	mtl_socm_d3_status_1_map,
	mtl_socm_d3_status_2_map,
	mtl_socm_d3_status_3_map,
	mtl_socm_vnn_req_status_0_map,
	mtl_socm_vnn_req_status_1_map,
	mtl_socm_vnn_req_status_2_map,
	mtl_socm_vnn_req_status_3_map,
	mtl_socm_vnn_misc_status_map,
	mtl_socm_signal_status_map,
	NULL
};

const struct pmc_reg_map mtl_socm_reg_map = {
	.pfear_sts = ext_mtl_socm_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slp_s0_res_counter_step = TGL_PMC_SLP_S0_RES_COUNTER_STEP,
	.ltr_show_sts = mtl_socm_ltr_show_map,
	.msr_sts = msr_map,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = MTL_SOC_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = MTL_SOCM_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
	.lpm_num_maps = ADL_LPM_NUM_MAPS,
	.ltr_ignore_max = MTL_SOCM_NUM_IP_IGN_ALLOWED,
	.lpm_res_counter_step_x2 = TGL_PMC_LPM_RES_COUNTER_STEP_X2,
	.etr3_offset = ETR3_OFFSET,
	.lpm_sts_latch_en_offset = MTL_LPM_STATUS_LATCH_EN_OFFSET,
	.lpm_priority_offset = MTL_LPM_PRI_OFFSET,
	.lpm_en_offset = MTL_LPM_EN_OFFSET,
	.lpm_residency_offset = MTL_LPM_RESIDENCY_OFFSET,
	.lpm_sts = mtl_socm_lpm_maps,
	.lpm_status_offset = MTL_LPM_STATUS_OFFSET,
	.lpm_live_status_offset = MTL_LPM_LIVE_STATUS_OFFSET,
};

void mtl_core_configure(struct pmc_dev *pmcdev)
{
	/* Due to a hardware limitation, the GBE LTR blocks PC10
	 * when a cable is attached. Tell the PMC to ignore it.
	 */
	dev_dbg(&pmcdev->pdev->dev, "ignoring GBE LTR\n");
	pmc_core_send_ltr_ignore(pmcdev, 3);
}

#define MTL_GNA_PCI_DEV	0x7e4c
#define MTL_IPU_PCI_DEV	0x7d19
#define MTL_VPU_PCI_DEV	0x7d1d
static void mtl_set_device_d3(unsigned int device)
{
	struct pci_dev *pcidev;

	pcidev = pci_get_device(PCI_VENDOR_ID_INTEL, device, NULL);
	if (pcidev) {
		if (!device_trylock(&pcidev->dev)) {
			pci_dev_put(pcidev);
			return;
		}
		if (!pcidev->dev.driver) {
			dev_info(&pcidev->dev, "Setting to D3hot\n");
			pci_set_power_state(pcidev, PCI_D3hot);
		}
		device_unlock(&pcidev->dev);
		pci_dev_put(pcidev);
	}
}

/*
 * Set power state of select devices that do not have drivers to D3
 * so that they do not block Package C entry.
 */
static void mtl_d3_fixup(void)
{
	mtl_set_device_d3(MTL_GNA_PCI_DEV);
	mtl_set_device_d3(MTL_IPU_PCI_DEV);
	mtl_set_device_d3(MTL_VPU_PCI_DEV);
}

static int mtl_resume(struct pmc_dev *pmcdev)
{
	mtl_d3_fixup();
	return pmc_core_resume_common(pmcdev);
}

void mtl_core_init(struct pmc_dev *pmcdev)
{
	pmcdev->map = &mtl_socm_reg_map;
	pmcdev->core_configure = mtl_core_configure;

	mtl_d3_fixup();

	pmcdev->resume = mtl_resume;
}
