// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains platform specific structure definitions
 * and init function used by Nova Lake PCH.
 *
 * Copyright (c) 2026, Intel Corporation.
 */

#include <linux/bits.h>
#include <linux/pci.h>

#include "core.h"

/* PMC SSRAM PMT Telemetry GUIDS */
#define PCDH_LPM_REQ_GUID 0x01093101
#define PCHS_LPM_REQ_GUID 0x01092101
#define PCDS_LPM_REQ_GUID 0x01091102

/*
 * Die Mapping to Product.
 * Product PCDDie PCHDie
 * NVL-H   PCD-H  None
 * NVL-S   PCD-S  PCH-S
 */

static const struct pmc_bit_map nvl_pcdh_pfear_map[] = {
	{"PMC_PGD0",                 BIT(0)},
	{"FUSE_OSSE_PGD0",           BIT(1)},
	{"SPI_PGD0",                 BIT(2)},
	{"XHCI_PGD0",                BIT(3)},
	{"SPA_PGD0",                 BIT(4)},
	{"SPB_PGD0",                 BIT(5)},
	{"MPFPW2_PGD0",              BIT(6)},
	{"GBE_PGD0",                 BIT(7)},

	{"SBR16B20_PGD0",            BIT(0)},
	{"DBG_SBR_PGD0",             BIT(1)},
	{"SBR16B7_PGD0",             BIT(2)},
	{"STRC_PGD0",                BIT(3)},
	{"SBR16B8_PGD0",             BIT(4)},
	{"D2D_DISP_PGD1",            BIT(5)},
	{"LPSS_PGD0",                BIT(6)},
	{"LPC_PGD0",                 BIT(7)},

	{"SMB_PGD0",                 BIT(0)},
	{"ISH_PGD0",                 BIT(1)},
	{"SBR16B2_PGD0",             BIT(2)},
	{"NPK_PGD0",                 BIT(3)},
	{"D2D_NOC_PGD1",             BIT(4)},
	{"DBG_SBR16B_PGD0",          BIT(5)},
	{"FUSE_PGD0",                BIT(6)},
	{"SBR16B0_PGD0",             BIT(7)},

	{"P2SB0_PGD0",               BIT(0)},
	{"OTG_PGD0",                 BIT(1)},
	{"EXI_PGD0",                 BIT(2)},
	{"CSE_PGD0",                 BIT(3)},
	{"CSME_KVM_PGD0",            BIT(4)},
	{"CSME_PMT_PGD0",            BIT(5)},
	{"CSME_CLINK_PGD0",          BIT(6)},
	{"SBR16B21_PGD0",            BIT(7)},

	{"CSME_USBR_PGD0",           BIT(0)},
	{"SBR16B22_PGD0",            BIT(1)},
	{"CSME_SMT1_PGD0",           BIT(2)},
	{"MPFPW1_PGD0",              BIT(3)},
	{"CSME_SMS2_PGD0",           BIT(4)},
	{"CSME_SMS_PGD0",            BIT(5)},
	{"CSME_RTC_PGD0",            BIT(6)},
	{"CSMEPSF_PGD0",             BIT(7)},

	{"D2D_NOC_PGD0",             BIT(0)},
	{"ESE_PGD0",                 BIT(1)},
	{"SBR16B6_PGD0",             BIT(2)},
	{"P2SB1_PGD0",               BIT(3)},
	{"SBR16B3_PGD0",             BIT(4)},
	{"OSSE_SMT1_PGD0",           BIT(5)},
	{"D2D_DISP_PGD0",            BIT(6)},
	{"SNPS_USB2_A_PGD0",         BIT(7)},

	{"U3FPW1_PGD0",              BIT(0)},
	{"FIA_X_PGD0",               BIT(1)},
	{"PSF4_PGD0",                BIT(2)},
	{"CNVI_PGD0",                BIT(3)},
	{"UFSX2_PGD0",               BIT(4)},
	{"ENDBG_PGD0",               BIT(5)},
	{"DBC_PGD0",                 BIT(6)},
	{"FIA_PG_PGD0",              BIT(7)},

	{"D2D_IPU_PGD0",             BIT(0)},
	{"NPK_PGD1",                 BIT(1)},
	{"FIACPCB_X_PGD0",           BIT(2)},
	{"SBR8B4_PGD0",              BIT(3)},
	{"DBG_PSF_PGD0",             BIT(4)},
	{"PSF6_PGD0",                BIT(5)},
	{"UFSPW1_PGD0",              BIT(6)},
	{"FIA_U_PGD0",               BIT(7)},

	{"PSF8_PGD0",                BIT(0)},
	{"SBR16B9_PGD0",             BIT(1)},
	{"PSF0_PGD0",                BIT(2)},
	{"FIACPCB_U_PGD0",           BIT(3)},
	{"TAM_PGD0",                 BIT(4)},
	{"D2D_NOC_PGD2",             BIT(5)},
	{"SBR8B2_PGD0",              BIT(6)},
	{"THC0_PGD0",                BIT(7)},

	{"THC1_PGD0",                BIT(0)},
	{"PMC_PGD1",                 BIT(1)},
	{"DISP_PGA1_PGD0",           BIT(2)},
	{"TCSS_PGD0",                BIT(3)},
	{"DISP_PGA_PGD0",            BIT(4)},
	{"SBR16B1_PGD0",             BIT(5)},
	{"SBRG_PGD0",                BIT(6)},
	{"PSF5_PGD0",                BIT(7)},

	{"SBR8B3_PGD0",              BIT(0)},
	{"ACE_PGD0",                 BIT(1)},
	{"ACE_PGD1",                 BIT(2)},
	{"ACE_PGD2",                 BIT(3)},
	{"ACE_PGD3",                 BIT(4)},
	{"ACE_PGD4",                 BIT(5)},
	{"ACE_PGD5",                 BIT(6)},
	{"ACE_PGD6",                 BIT(7)},

	{"ACE_PGD7",                 BIT(0)},
	{"ACE_PGD8",                 BIT(1)},
	{"ACE_PGD9",                 BIT(2)},
	{"ACE_PGD10",                BIT(3)},
	{"FIACPCB_PG_PGD0",          BIT(4)},
	{"SNPS_USB2_B_PGD0",         BIT(5)},
	{"OSSE_PGD0",                BIT(6)},
	{"SBR8B0_PGD0",              BIT(7)},

	{"SBR16B4_PGD0",             BIT(0)},
	{"CSME_PTIO_PGD0",           BIT(1)},
	{}
};

static const struct pmc_bit_map *ext_nvl_pcdh_pfear_map[] = {
	nvl_pcdh_pfear_map,
	NULL
};

static const struct pmc_bit_map nvl_pcdh_clocksource_status_map[] = {
	{"AON2_OFF_STS",                 BIT(0),	1},
	{"AON3_OFF_STS",                 BIT(1),	0},
	{"AON4_OFF_STS",                 BIT(2),	1},
	{"AON5_OFF_STS",                 BIT(3),	1},
	{"AON1_OFF_STS",                 BIT(4),	0},
	{"XTAL_LVM_OFF_STS",             BIT(5),	0},
	{"MPFPW1_0_PLL_OFF_STS",         BIT(6),	1},
	{"D2D_PLL_OFF_STS",              BIT(7),	1},
	{"USB3_PLL_OFF_STS",             BIT(8),	1},
	{"AON3_SPL_OFF_STS",             BIT(9),	1},
	{"MPFPW2_0_PLL_OFF_STS",         BIT(12),	1},
	{"XTAL_AGGR_OFF_STS",            BIT(17),	1},
	{"USB2_PLL_OFF_STS",             BIT(18),	0},
	{"DDI2_PLL_OFF_STS",             BIT(19),	1},
	{"SE_TCSS_PLL_OFF_STS",          BIT(20),	1},
	{"DDI_PLL_OFF_STS",              BIT(21),	1},
	{"FILTER_PLL_OFF_STS",           BIT(22),	1},
	{"ACE_PLL_OFF_STS",              BIT(24),	0},
	{"FABRIC_PLL_OFF_STS",           BIT(25),	1},
	{"SOC_PLL_OFF_STS",              BIT(26),	1},
	{"REF_PLL_OFF_STS",              BIT(28),	1},
	{"IMG_PLL_OFF_STS",              BIT(29),	1},
	{"GENLOCK_FILTER_PLL_OFF_STS",   BIT(30),	1},
	{"RTC_PLL_OFF_STS",              BIT(31),	0},
	{}
};

static const struct pmc_bit_map nvl_pcdh_power_gating_status_0_map[] = {
	{"PMC_PGD0_PG_STS",              BIT(0),	0},
	{"FUSE_OSSE_PGD0_PG_STS",	 BIT(1),	0},
	{"ESPISPI_PGD0_PG_STS",          BIT(2),	0},
	{"XHCI_PGD0_PG_STS",             BIT(3),	1},
	{"SPA_PGD0_PG_STS",              BIT(4),	1},
	{"SPB_PGD0_PG_STS",              BIT(5),	1},
	{"MPFPW2_PGD0_PG_STS",           BIT(6),	0},
	{"GBE_PGD0_PG_STS",              BIT(7),	1},
	{"SBR16B20_PGD0_PG_STS",         BIT(8),	0},
	{"DBG_PGD0_PG_STS",              BIT(9),	0},
	{"SBR16B7_PGD0_PG_STS",          BIT(10),	0},
	{"STRC_PGD0_PG_STS",             BIT(11),	0},
	{"SBR16B8_PGD0_PG_STS",          BIT(12),	0},
	{"D2D_DISP_PGD1_PG_STS",         BIT(13),	1},
	{"LPSS_PGD0_PG_STS",             BIT(14),	1},
	{"LPC_PGD0_PG_STS",              BIT(15),	0},
	{"SMB_PGD0_PG_STS",              BIT(16),	0},
	{"ISH_PGD0_PG_STS",              BIT(17),	0},
	{"SBR16B2_PGD0_PG_STS",          BIT(18),	0},
	{"NPK_PGD0_PG_STS",              BIT(19),	0},
	{"D2D_NOC_PGD1_PG_STS",          BIT(20),	1},
	{"DBG_SBR16B_PGD0_PG_STS",       BIT(21),	0},
	{"FUSE_PGD0_PG_STS",             BIT(22),	0},
	{"SBR16B0_PGD0_PG_STS",          BIT(23),	0},
	{"P2SB0_PGD0_PG_STS",            BIT(24),	1},
	{"XDCI_PGD0_PG_STS",             BIT(25),	1},
	{"EXI_PGD0_PG_STS",              BIT(26),	0},
	{"CSE_PGD0_PG_STS",              BIT(27),	1},
	{"KVMCC_PGD0_PG_STS",            BIT(28),	1},
	{"PMT_PGD0_PG_STS",              BIT(29),	1},
	{"CLINK_PGD0_PG_STS",            BIT(30),	1},
	{"SBR16B21_PGD0_PG_STS",         BIT(31),	0},
	{}
};

static const struct pmc_bit_map nvl_pcdh_power_gating_status_1_map[] = {
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
	{"SBR16B6_PGD0_PG_STS",          BIT(10),	0},
	{"P2SB1_PGD0_PG_STS",            BIT(11),	1},
	{"SBR16B3_PGD0_PG_STS",          BIT(12),	0},
	{"OSSE_SMT1_PGD0_PG_STS",        BIT(13),	1},
	{"D2D_DISP_PGD0_PG_STS",         BIT(14),	1},
	{"SNPA_USB2_A_PGD0_PG_STS",      BIT(15),	0},
	{"U3FPW1_PGD0_PG_STS",           BIT(16),	0},
	{"FIA_X_PGD0_PG_STS",            BIT(17),	0},
	{"PSF4_PGD0_PG_STS",             BIT(18),	0},
	{"CNVI_PGD0_PG_STS",             BIT(19),	0},
	{"UFSX2_PGD0_PG_STS",            BIT(20),	1},
	{"ENDBG_PGD0_PG_STS",            BIT(21),	0},
	{"DBC_PGD0_PG_STS",              BIT(22),	0},
	{"FIA_PG_PGD0_PG_STS",           BIT(23),	0},
	{"D2D_IPU_PGD0_PG_STS",          BIT(24),	1},
	{"NPK_PGD1_PG_STS",              BIT(25),	0},
	{"FIACPCB_X_PGD0_PG_STS",        BIT(26),	0},
	{"SBR8B4_PGD0_PG_STS",           BIT(27),	0},
	{"DBG_PSF_PGD0_PG_STS",          BIT(28),	0},
	{"PSF6_PGD0_PG_STS",             BIT(29),	0},
	{"UFSPW1_PGD0_PG_STS",           BIT(30),	0},
	{"FIA_U_PGD0_PG_STS",            BIT(31),	0},
	{}
};

static const struct pmc_bit_map nvl_pcdh_power_gating_status_2_map[] = {
	{"PSF8_PGD0_PG_STS",             BIT(0),	0},
	{"SBR16B9_PGD0_PG_STS",          BIT(1),	0},
	{"PSF0_PGD0_PG_STS",             BIT(2),	0},
	{"FIACPCB_U_PGD0_PG_STS",        BIT(3),	0},
	{"TAM_PGD0_PG_STS",              BIT(4),	1},
	{"D2D_NOC_PGD2_PG_STS",          BIT(5),	1},
	{"SBR8B2_PGD0_PG_STS",           BIT(6),	0},
	{"THC0_PGD0_PG_STS",             BIT(7),	1},
	{"THC1_PGD0_PG_STS",             BIT(8),	1},
	{"PMC_PGD1_PG_STS",              BIT(9),	0},
	{"DISP_PGA1_PGD0_PG_STS",        BIT(10),	0},
	{"TCSS_PGD0_PG_STS",             BIT(11),	0},
	{"DISP_PGA_PGD0_PG_STS",         BIT(12),	0},
	{"SBR16B1_PGD0_PG_STS",          BIT(13),	0},
	{"SBRG_PGD0_PG_STS",             BIT(14),	0},
	{"PSF5_PGD0_PG_STS",             BIT(15),	0},
	{"SBR8B3_PGD0_PG_STS",           BIT(16),	0},
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
	{"FIACPCB_PG_PGD0_PG_STS",       BIT(28),	0},
	{"SNPS_USB2_B_PGD0_PG_STS",      BIT(29),	0},
	{"OSSE_PGD0_PG_STS",             BIT(30),	1},
	{"SBR8B0_PGD0_PG_STS",           BIT(31),	0},
	{}
};

static const struct pmc_bit_map nvl_pcdh_power_gating_status_3_map[] = {
	{"SBR16B4_PGD0_PG_STS",          BIT(0),	0},
	{"PTIO_PGD0_PG_STS",             BIT(1),	1},
	{}
};

static const struct pmc_bit_map nvl_pcdh_d3_status_0_map[] = {
	{"LPSS_D3_STS",                  BIT(3),	1},
	{"XDCI_D3_STS",                  BIT(4),	1},
	{"XHCI_D3_STS",                  BIT(5),	1},
	{"OSSE_D3_STS",                  BIT(6),	0},
	{"SPA_D3_STS",                   BIT(12),	0},
	{"SPB_D3_STS",                   BIT(13),	0},
	{"ESPISPI_D3_STS",               BIT(18),	0},
	{"PSTH_D3_STS",                  BIT(21),	0},
	{}
};

static const struct pmc_bit_map nvl_pcdh_d3_status_1_map[] = {
	{"OSSE_SMT1_D3_STS",             BIT(0),	0},
	{"GBE_D3_STS",                   BIT(19),	0},
	{"ITSS_D3_STS",                  BIT(23),	0},
	{"CNVI_D3_STS",                  BIT(27),	0},
	{"UFSX2_D3_STS",                 BIT(28),	0},
	{"ESE_D3_STS",                   BIT(29),	0},
	{}
};

static const struct pmc_bit_map nvl_pcdh_d3_status_2_map[] = {
	{"CSMERTC_D3_STS",               BIT(1),	0},
	{"CSE_D3_STS",                   BIT(4),	0},
	{"KVMCC_D3_STS",                 BIT(5),	0},
	{"USBR0_D3_STS",                 BIT(6),	0},
	{"ISH_D3_STS",                   BIT(7),	0},
	{"SMT1_D3_STS",                  BIT(8),	0},
	{"SMT2_D3_STS",                  BIT(9),	0},
	{"SMT3_D3_STS",                  BIT(10),	0},
	{"OSSE_SMT2_D3_STS",             BIT(11),	0},
	{"CLINK_D3_STS",                 BIT(14),	0},
	{"PTIO_D3_STS",                  BIT(16),	0},
	{"PMT_D3_STS",                   BIT(17),	0},
	{"SMS1_D3_STS",                  BIT(18),	0},
	{"SMS2_D3_STS",                  BIT(19),	0},
	{}
};

static const struct pmc_bit_map nvl_pcdh_d3_status_3_map[] = {
	{"THC0_D3_STS",                  BIT(14),	1},
	{"THC1_D3_STS",                  BIT(15),	1},
	{"OSSE_SMT3_D3_STS",             BIT(16),	0},
	{"ACE_D3_STS",                   BIT(23),	0},
	{}
};

static const struct pmc_bit_map nvl_pcdh_vnn_req_status_0_map[] = {
	{"LPSS_VNN_REQ_STS",             BIT(3),	1},
	{"OSSE_VNN_REQ_STS",             BIT(6),	1},
	{"ESPISPI_VNN_REQ_STS",          BIT(18),	1},
	{}
};

static const struct pmc_bit_map nvl_pcdh_vnn_req_status_1_map[] = {
	{"OSSE_SMT1_VNN_REQ_STS",        BIT(0),	1},
	{"NPK_VNN_REQ_STS",              BIT(4),	1},
	{"DFXAGG_VNN_REQ_STS",           BIT(8),	0},
	{"EXI_VNN_REQ_STS",              BIT(9),	1},
	{"P2D_VNN_REQ_STS",              BIT(18),	1},
	{"GBE_VNN_REQ_STS",              BIT(19),	1},
	{"SMB_VNN_REQ_STS",              BIT(25),	1},
	{"LPC_VNN_REQ_STS",              BIT(26),	0},
	{"ESE_VNN_REQ_STS",              BIT(29),	1},
	{}
};

static const struct pmc_bit_map nvl_pcdh_vnn_req_status_2_map[] = {
	{"CSMERTC_VNN_REQ_STS",          BIT(1),	1},
	{"CSE_VNN_REQ_STS",              BIT(4),	1},
	{"ISH_VNN_REQ_STS",              BIT(7),	1},
	{"SMT1_VNN_REQ_STS",             BIT(8),	1},
	{"CLINK_VNN_REQ_STS",            BIT(14),	1},
	{"SMS1_VNN_REQ_STS",             BIT(18),	1},
	{"SMS2_VNN_REQ_STS",             BIT(19),	1},
	{"GPIOCOM4_VNN_REQ_STS",         BIT(20),	1},
	{"GPIOCOM3_VNN_REQ_STS",         BIT(21),	1},
	{"DISP_SHIM_VNN_REQ_STS",        BIT(22),	1},
	{"GPIOCOM1_VNN_REQ_STS",         BIT(23),	1},
	{"GPIOCOM0_VNN_REQ_STS",         BIT(24),	1},
	{}
};

static const struct pmc_bit_map nvl_pcdh_vnn_req_status_3_map[] = {
	{"DTS0_VNN_REQ_STS",             BIT(7),	0},
	{"GPIOCOM5_VNN_REQ_STS",         BIT(11),	1},
	{}
};

static const struct pmc_bit_map nvl_pcdh_vnn_misc_status_map[] = {
	{"CPU_C10_REQ_STS",              BIT(0),	0},
	{"TS_OFF_REQ_STS",               BIT(1),	0},
	{"PNDE_MET_REQ_STS",             BIT(2),	1},
	{"PG5_PMA0_REQ_STS",             BIT(3),	1},
	{"FW_THROTTLE_ALLOWED_REQ_STS",  BIT(4),	0},
	{"VNN_SOC_REQ_STS",              BIT(6),	1},
	{"ISH_VNNAON_REQ_STS",           BIT(7),	0},
	{"D2D_NOC_CFI_QACTIVE_REQ_STS",	 BIT(8),	1},
	{"D2D_NOC_GPSB_QACTIVE_REQ_STS", BIT(9),	1},
	{"D2D_IPU_QACTIVE_REQ_STS",      BIT(10),	1},
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
	{"PG5_PMA1_REQ_STS",             BIT(22),	1},
	{"FIA_DEEP_PM_REQ_STS",          BIT(23),	0},
	{"XDCI_ATTACHED_REQ_STS",        BIT(24),	1},
	{"ARC_INTERRUPT_WAKE_REQ_STS",   BIT(25),	0},
	{"D2D_DISP_DDI_QACTIVE_REQ_STS", BIT(26),	1},
	{"PRE_WAKE0_REQ_STS",            BIT(27),	1},
	{"PRE_WAKE1_REQ_STS",            BIT(28),	1},
	{"PRE_WAKE2_REQ_STS",            BIT(29),	1},
	{"PG5_PMA2_GVNN",                BIT(30),	1},
	{"D2D_DISP_EDP_QACTIVE_REQ_STS", BIT(31),	1},
	{}
};

static const struct pmc_bit_map nvl_pcdh_rsc_status_map[] = {
	{"CORE",		0,		1},
	{"Memory",		0,		1},
	{"PRIM_D2D",		0,		1},
	{"PSF0",		0,		1},
	{"PSF4",		0,		1},
	{"PSF6",		0,		1},
	{"PSF8",		0,		1},
	{"SB",			0,		1},
	{}
};

static const struct pmc_bit_map *nvl_pcdh_lpm_maps[] = {
	nvl_pcdh_clocksource_status_map,
	nvl_pcdh_power_gating_status_0_map,
	nvl_pcdh_power_gating_status_1_map,
	nvl_pcdh_power_gating_status_2_map,
	nvl_pcdh_power_gating_status_3_map,
	nvl_pcdh_d3_status_0_map,
	nvl_pcdh_d3_status_1_map,
	nvl_pcdh_d3_status_2_map,
	nvl_pcdh_d3_status_3_map,
	nvl_pcdh_vnn_req_status_0_map,
	nvl_pcdh_vnn_req_status_1_map,
	nvl_pcdh_vnn_req_status_2_map,
	nvl_pcdh_vnn_req_status_3_map,
	nvl_pcdh_vnn_misc_status_map,
	ptl_pcdp_signal_status_map,
	NULL
};

static const struct pmc_bit_map *nvl_pcdh_blk_maps[] = {
	nvl_pcdh_power_gating_status_0_map,
	nvl_pcdh_power_gating_status_1_map,
	nvl_pcdh_power_gating_status_2_map,
	nvl_pcdh_power_gating_status_3_map,
	nvl_pcdh_rsc_status_map,
	nvl_pcdh_vnn_req_status_0_map,
	nvl_pcdh_vnn_req_status_1_map,
	nvl_pcdh_vnn_req_status_2_map,
	nvl_pcdh_vnn_req_status_3_map,
	nvl_pcdh_d3_status_0_map,
	nvl_pcdh_d3_status_1_map,
	nvl_pcdh_d3_status_2_map,
	nvl_pcdh_d3_status_3_map,
	nvl_pcdh_clocksource_status_map,
	nvl_pcdh_vnn_misc_status_map,
	ptl_pcdp_signal_status_map,
	NULL
};

static const struct pmc_bit_map nvl_pcds_pfear_map[] = {
	{"PMC_PGD0",                 BIT(0)},
	{"FUSE_OSSE_PGD0",           BIT(1)},
	{"SPI_PGD0",                 BIT(2)},
	{"XHCI_PGD0",                BIT(3)},
	{"SPA_PGD0",                 BIT(4)},
	{"SPB_PGD0",                 BIT(5)},
	{"RSVD6",                    BIT(6)},
	{"GBE_PGD0",                 BIT(7)},

	{"RSVD8",                    BIT(0)},
	{"RSVD9",                    BIT(1)},
	{"SBR16B7_PGD0",             BIT(2)},
	{"SBR16B21_PGD0",            BIT(3)},
	{"RSVD12",                   BIT(4)},
	{"D2D_DISP_PGD1",            BIT(5)},
	{"LPSS_PGD0",                BIT(6)},
	{"LPC_PGD0",                 BIT(7)},

	{"SMB_PGD0",                 BIT(0)},
	{"ISH_PGD0",                 BIT(1)},
	{"SBR16B1_PGD0",             BIT(2)},
	{"NPK_PGD0",                 BIT(3)},
	{"D2D_NOC_PGD1",             BIT(4)},
	{"DBG_SBR16B_PGD0",          BIT(5)},
	{"FUSE_PGD0",                BIT(6)},
	{"RSVD23",                   BIT(7)},

	{"P2SB0_PGD0",               BIT(0)},
	{"OTG_PGD0",                 BIT(1)},
	{"EXI_PGD0",                 BIT(2)},
	{"CSE_PGD0",                 BIT(3)},
	{"CSME_KVM_PGD0",            BIT(4)},
	{"CSME_PMT_PGD0",            BIT(5)},
	{"CSME_CLINK_PGD0",          BIT(6)},
	{"CSME_PTIO_PGD0",           BIT(7)},

	{"CSME_USBR_PGD0",           BIT(0)},
	{"SBR16B22_PGD0",            BIT(1)},
	{"CSME_SMT1_PGD0",           BIT(2)},
	{"P2SB1_PGD0",               BIT(3)},
	{"CSME_SMS2_PGD0",           BIT(4)},
	{"CSME_SMS_PGD0",            BIT(5)},
	{"CSME_RTC_PGD0",            BIT(6)},
	{"CSMEPSF_PGD0",             BIT(7)},

	{"D2D_NOC_PGD0",             BIT(0)},
	{"RSVD41",                   BIT(1)},
	{"RSVD42",                   BIT(2)},
	{"RSVD43",                   BIT(3)},
	{"SBR16B2_PGD0",             BIT(4)},
	{"OSSE_SMT1_PGD0",           BIT(5)},
	{"D2D_DISP_PGD0",            BIT(6)},
	{"RSVD47_PGD0",              BIT(7)},

	{"RSVD48",                   BIT(0)},
	{"DBG_PSF_PGD0",             BIT(1)},
	{"RSVD50",                   BIT(2)},
	{"CNVI_PGD0",                BIT(3)},
	{"UFSX2_PGD0",               BIT(4)},
	{"ENDBG_PGD0",               BIT(5)},
	{"DBC_PGD0",                 BIT(6)},
	{"SBR16B4_PGD0",             BIT(7)},

	{"RSVD56",                   BIT(0)},
	{"NPK_PGD1",                 BIT(1)},
	{"RSVD58",                   BIT(2)},
	{"SBR16B20_PGD0",            BIT(3)},
	{"RSVD60",                   BIT(4)},
	{"SBR8B20_PGD0",             BIT(5)},
	{"RSVD62",                   BIT(6)},
	{"FIA_U_PGD0",               BIT(7)},

	{"PSF8_PGD0",                BIT(0)},
	{"RSVD65",                   BIT(1)},
	{"RSVD66",                   BIT(2)},
	{"FIACPCB_U_PGD0",           BIT(3)},
	{"TAM_PGD0",                 BIT(4)},
	{"D2D_NOC_PGD2",             BIT(5)},
	{"SBR8B2_PGD0",              BIT(6)},
	{"THC0_PGD0",                BIT(7)},

	{"THC1_PGD0",                BIT(0)},
	{"PMC_PGD1",                 BIT(1)},
	{"SBR16B3_PGD0",             BIT(2)},
	{"TCSS_PGD0",                BIT(3)},
	{"DISP_PGA_PGD0",            BIT(4)},
	{"RSVD77",                   BIT(5)},
	{"RSVD78",                   BIT(6)},
	{"RSVD79",                   BIT(7)},

	{"SBRG_PGD0",                BIT(0)},
	{"RSVD81",                   BIT(1)},
	{"SBR16B0_PGD0",             BIT(2)},
	{"SBR8B0_PGD0",              BIT(3)},
	{"PSF7_PGD0",                BIT(4)},
	{"RSVD85",                   BIT(5)},
	{"RSVD86",                   BIT(6)},
	{"RSVD87",                   BIT(7)},

	{"SBR16B6_PGD0",             BIT(0)},
	{"PSD0_PGD0",                BIT(1)},
	{"STRC_PGD0",                BIT(2)},
	{"RSVD91",                   BIT(3)},
	{"DBG_SBR_PGD0",             BIT(4)},
	{"RSVD93",                   BIT(5)},
	{"OSSE_PGD0",                BIT(6)},
	{"DISP_PGA1_PGD0",           BIT(7)},
	{}
};

static const struct pmc_bit_map *ext_nvl_pcds_pfear_map[] = {
	nvl_pcds_pfear_map,
	NULL
};

static const struct pmc_bit_map nvl_pcds_ltr_show_map[] = {
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
	{"RSVD",		NVL_PCDS_PMC_LTR_RESERVED},
	{"IOE_PMC",		MTL_PMC_LTR_IOE_PMC},
	{"DMI3",		ARL_PMC_LTR_DMI3},
	{"OSSE",		LNL_PMC_LTR_OSSE},

	/* Below two cannot be used for LTR_IGNORE */
	{"CURRENT_PLATFORM",	PTL_PMC_LTR_CUR_PLT},
	{"AGGREGATED_SYSTEM",	PTL_PMC_LTR_CUR_ASLT},
	{}
};

static const struct pmc_bit_map nvl_pcds_clocksource_status_map[] = {
	{"AON2_OFF_STS",                 BIT(0),	1},
	{"AON3_OFF_STS",                 BIT(1),	0},
	{"AON4_OFF_STS",                 BIT(2),	1},
	{"AON5_OFF_STS",                 BIT(3),	1},
	{"AON1_OFF_STS",                 BIT(4),	0},
	{"XTAL_LVM_OFF_STS",             BIT(5),	0},
	{"D2D_OFF_STS",                  BIT(8),	1},
	{"AON3_SPL_OFF_STS",             BIT(9),	1},
	{"XTAL_AGGR_OFF_STS",            BIT(17),	1},
	{"BCLK_EXT_INJ_OFF_STS",         BIT(18),	1},
	{"DDI2_PLL_OFF_STS",             BIT(19),	1},
	{"SE_TCSS_PLL_OFF_STS",          BIT(20),	1},
	{"DDI_PLL_OFF_STS",              BIT(21),	1},
	{"FILTER_PLL_OFF_STS",           BIT(22),	1},
	{"PHY_OC_EXT_INJ_OFF_STS",       BIT(23),	1},
	{"ACE_PLL_OFF_STS",              BIT(24),	0},
	{"FABRIC_PLL_OFF_STS",           BIT(25),	1},
	{"SOC_PLL_OFF_STS",              BIT(26),	1},
	{"REF_PLL_OFF_STS",              BIT(28),	1},
	{"GENLOCK_FILTER_PLL_OFF_STS",   BIT(30),	1},
	{"RTC_PLL_OFF_STS",              BIT(31),	0},
	{}
};

static const struct pmc_bit_map nvl_pcds_power_gating_status_0_map[] = {
	{"PMC_PGD0_PG_STS",              BIT(0),	0},
	{"FUSE_OSSE_PGD0_PG_STS",	 BIT(1),	0},
	{"ESPISPI_PGD0_PG_STS",          BIT(2),	0},
	{"XHCI_PGD0_PG_STS",             BIT(3),	0},
	{"SPA_PGD0_PG_STS",              BIT(4),	0},
	{"SPB_PGD0_PG_STS",              BIT(5),	0},
	{"RSVD_6",                       BIT(6),	0},
	{"GBE_PGD0_PG_STS",              BIT(7),	0},
	{"RSVD_8",                       BIT(8),	0},
	{"RSVD_9",                       BIT(9),	0},
	{"SBR16B7_PGD0_PG_STS",          BIT(10),	0},
	{"SBR16B21_PGD0_PG_STS",         BIT(11),	0},
	{"RSVD_12",                      BIT(12),	0},
	{"D2D_DISP_PGD1_PG_STS",         BIT(13),	1},
	{"LPSS_PGD0_PG_STS",             BIT(14),	0},
	{"LPC_PGD0_PG_STS",              BIT(15),	0},
	{"SMB_PGD0_PG_STS",              BIT(16),	0},
	{"ISH_PGD0_PG_STS",              BIT(17),	0},
	{"SBR16B1_PGD0_PG_STS",          BIT(18),	0},
	{"NPK_PGD0_PG_STS",              BIT(19),	0},
	{"D2D_NOC_PGD1_PG_STS",          BIT(20),	1},
	{"DBG_SBR16B_PGD0_PG_STS",       BIT(21),	0},
	{"FUSE_PGD0_PG_STS",             BIT(22),	0},
	{"RSVD_23",                      BIT(23),	0},
	{"P2SB0_PGD0_PG_STS",            BIT(24),	1},
	{"XDCI_PGD0_PG_STS",             BIT(25),	0},
	{"EXI_PGD0_PG_STS",              BIT(26),	0},
	{"CSE_PGD0_PG_STS",              BIT(27),	1},
	{"KVMCC_PGD0_PG_STS",            BIT(28),	0},
	{"PMT_PGD0_PG_STS",              BIT(29),	0},
	{"CLINK_PGD0_PG_STS",            BIT(30),	0},
	{"PTIO_PGD0_PG_STS",             BIT(31),	0},
	{}
};

static const struct pmc_bit_map nvl_pcds_power_gating_status_1_map[] = {
	{"USBR0_PGD0_PG_STS",            BIT(0),	0},
	{"SBR16B22_PGD0_PG_STS",         BIT(1),	0},
	{"SMT1_PGD0_PG_STS",             BIT(2),	0},
	{"P2SB1_PGD0_PG_STS",            BIT(3),	1},
	{"SMS2_PGD0_PG_STS",             BIT(4),	0},
	{"SMS1_PGD0_PG_STS",             BIT(5),	0},
	{"CSMERTC_PGD0_PG_STS",          BIT(6),	0},
	{"CSMEPSF_PGD0_PG_STS",          BIT(7),	0},
	{"D2D_NOC_PGD0_PG_STS",          BIT(8),	0},
	{"RSVD_9",                       BIT(9),	0},
	{"RSVD_10",                      BIT(10),	0},
	{"RSVD_11",                      BIT(11),	0},
	{"SBR16B2_PGD0_PG_STS",          BIT(12),	0},
	{"OSSE_SMT1_PGD0_PG_STS",        BIT(13),	1},
	{"D2D_DISP_PGD0_PG_STS",         BIT(14),	1},
	{"RSVD_15",                      BIT(15),	0},
	{"RSVD_16",                      BIT(16),	0},
	{"DBG_PSF_PGD0_PG_STS",          BIT(17),	0},
	{"RSVD_18",                      BIT(18),	0},
	{"CNVI_PGD0_PG_STS",             BIT(19),	0},
	{"UFSX2_PGD0_PG_STS",            BIT(20),	0},
	{"ENDBG_PGD0_PG_STS",            BIT(21),	0},
	{"DBC_PGD0_PG_STS",              BIT(22),	0},
	{"SBR16B4_PGD0_PG_STS",          BIT(23),	0},
	{"RSVD_24",                      BIT(24),	0},
	{"NPK_PGD1_PG_STS",              BIT(25),	0},
	{"RSVD_26",                      BIT(26),	0},
	{"SBR16B20_PGD0_PG_STS",         BIT(27),	0},
	{"RSVD_28",                      BIT(28),	0},
	{"SBR8B20_PGD0_PG_STS",          BIT(29),	0},
	{"RSVD_30",                      BIT(30),	0},
	{"FIA_U_PGD0_PG_STS",            BIT(31),	0},
	{}
};

static const struct pmc_bit_map nvl_pcds_power_gating_status_2_map[] = {
	{"PSF8_PGD0_PG_STS",             BIT(0),	0},
	{"RSVD_1",                       BIT(1),	0},
	{"RSVD_2",                       BIT(2),	0},
	{"FIACPCB_U_PGD0_PG_STS",        BIT(3),	0},
	{"TAM_PGD0_PG_STS",              BIT(4),	1},
	{"D2D_NOC_PGD2_PG_STS",          BIT(5),	1},
	{"SBR8B2_PGD0_PG_STS",           BIT(6),	0},
	{"THC0_PGD0_PG_STS",             BIT(7),	0},
	{"THC1_PGD0_PG_STS",             BIT(8),	0},
	{"PMC_PGD1_PG_STS",              BIT(9),	0},
	{"SBR16B3_PGD0_PG_STS",          BIT(10),	0},
	{"TCSS_PGD0_PG_STS",             BIT(11),	0},
	{"DISP_PGA_PGD0_PG_STS",         BIT(12),	0},
	{"RSVD_13",                      BIT(13),	0},
	{"RSVD_14",                      BIT(14),	0},
	{"RSVD_15",                      BIT(15),	0},
	{"SBRG_PGD0_PG_STS",             BIT(16),	0},
	{"RSVD_17",                      BIT(17),	0},
	{"SBR16B0_PGD0_PG_STS",          BIT(18),	0},
	{"SBR8B0_PGD0_PG_STS",           BIT(19),	0},
	{"PSF7_PGD0_PG_STS",             BIT(20),	0},
	{"RSVD_21",                      BIT(21),	0},
	{"RSVD_22",                      BIT(22),	0},
	{"RSVD_23",                      BIT(23),	0},
	{"SBR16B6_PGD0_PG_STS",          BIT(24),	0},
	{"PSF0_PGD0_PG_STS",             BIT(25),	0},
	{"STRC_PGD0_PG_STS",             BIT(26),	0},
	{"RSVD_27",                      BIT(27),	0},
	{"DBG_SBR_PGD0_PG_STS",          BIT(28),	0},
	{"RSVD_29",                      BIT(29),	0},
	{"OSSE_PGD0_PG_STS",             BIT(30),	1},
	{"DISP_PGA1_PGD0_PG_STS",        BIT(31),	0},
	{}
};

static const struct pmc_bit_map nvl_pcds_d3_status_0_map[] = {
	{"LPSS_D3_STS",                  BIT(3),	1},
	{"XDCI_D3_STS",                  BIT(4),	1},
	{"XHCI_D3_STS",                  BIT(5),	1},
	{"SPA_D3_STS",                   BIT(12),	0},
	{"SPB_D3_STS",                   BIT(13),	0},
	{"ESPISPI_D3_STS",               BIT(18),	0},
	{"PSTH_D3_STS",                  BIT(21),	0},
	{}
};

static const struct pmc_bit_map nvl_pcds_d3_status_1_map[] = {
	{"OSSE_D3_STS",                  BIT(14),	0},
	{"GBE_D3_STS",                   BIT(19),	0},
	{"ITSS_D3_STS",                  BIT(23),	0},
	{"CNVI_D3_STS",                  BIT(27),	0},
	{"UFSX2_D3_STS",                 BIT(28),	0},
	{}
};

static const struct pmc_bit_map nvl_pcds_d3_status_2_map[] = {
	{"CSMERTC_D3_STS",               BIT(1),	0},
	{"CSE_D3_STS",                   BIT(4),	0},
	{"KVMCC_D3_STS",                 BIT(5),	0},
	{"USBR0_D3_STS",                 BIT(6),	0},
	{"ISH_D3_STS",                   BIT(7),	0},
	{"SMT1_D3_STS",                  BIT(8),	0},
	{"SMT2_D3_STS",                  BIT(9),	0},
	{"SMT3_D3_STS",                  BIT(10),	0},
	{"OSSE_SMT1_D3_STS",             BIT(12),	0},
	{"CLINK_D3_STS",                 BIT(14),	0},
	{"PTIO_D3_STS",                  BIT(16),	0},
	{"PMT_D3_STS",                   BIT(17),	0},
	{"SMS1_D3_STS",                  BIT(18),	0},
	{"SMS2_D3_STS",                  BIT(19),	0},
	{}
};

static const struct pmc_bit_map nvl_pcds_d3_status_3_map[] = {
	{"OSSE_SMT2_D3_STS",             BIT(0),	0},
	{"THC0_D3_STS",                  BIT(14),	1},
	{"THC1_D3_STS",                  BIT(15),	1},
	{"OSSE_SMT3_D3_STS",             BIT(19),	0},
	{}
};

static const struct pmc_bit_map nvl_pcds_vnn_req_status_0_map[] = {
	{"LPSS_VNN_REQ_STS",             BIT(3),	0},
	{"ESPISPI_VNN_REQ_STS",          BIT(18),	1},
	{}
};

static const struct pmc_bit_map nvl_pcds_vnn_req_status_1_map[] = {
	{"NPK_VNN_REQ_STS",              BIT(4),	1},
	{"DFXAGG_VNN_REQ_STS",           BIT(8),	0},
	{"EXI_VNN_REQ_STS",              BIT(9),	1},
	{"OSSE_VNN_REQ_STS",             BIT(14),	1},
	{"P2D_VNN_REQ_STS",              BIT(18),	1},
	{"GBE_VNN_REQ_STS",              BIT(19),	0},
	{"SMB_VNN_REQ_STS",              BIT(25),	1},
	{"LPC_VNN_REQ_STS",              BIT(26),	0},
	{}
};

static const struct pmc_bit_map nvl_pcds_vnn_req_status_2_map[] = {
	{"CSMERTC_VNN_REQ_STS",          BIT(1),	0},
	{"CSE_VNN_REQ_STS",              BIT(4),	1},
	{"ISH_VNN_REQ_STS",              BIT(7),	0},
	{"SMT1_VNN_REQ_STS",             BIT(8),	0},
	{"OSSE_SMT1_VNN_REQ_STS",        BIT(12),	1},
	{"CLINK_VNN_REQ_STS",            BIT(14),	0},
	{"SMS1_VNN_REQ_STS",             BIT(18),	0},
	{"SMS2_VNN_REQ_STS",             BIT(19),	0},
	{"GPIOCOM4_VNN_REQ_STS",         BIT(20),	0},
	{"GPIOCOM3_VNN_REQ_STS",         BIT(21),	1},
	{"GPIOCOM1_VNN_REQ_STS",         BIT(23),	1},
	{"GPIOCOM0_VNN_REQ_STS",         BIT(24),	1},
	{}
};

static const struct pmc_bit_map nvl_pcds_vnn_req_status_3_map[] = {
	{"DISP_SHIM_VNN_REQ_STS",        BIT(4),	1},
	{"DTS0_VNN_REQ_STS",             BIT(7),	0},
	{"GPIOCOM5_VNN_REQ_STS",         BIT(11),	0},
	{}
};

static const struct pmc_bit_map nvl_pcds_vnn_misc_status_map[] = {
	{"CPU_C10_REQ_STS",              BIT(0),	0},
	{"TS_OFF_REQ_STS",               BIT(1),	0},
	{"PNDE_MET_REQ_STS",             BIT(2),	1},
	{"PG5_PMA0_REQ_STS",             BIT(3),	1},
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
	{"PG5_PMA1_REQ_STS",             BIT(22),	1},
	{"DG5_PMA0_REQ_STS",             BIT(23),	1},
	{"ARC_INTERRUPT_WAKE_REQ_STS",   BIT(25),	0},
	{"D2D_DISP_DDI_QACTIVE_REQ_STS", BIT(26),	1},
	{"PRE_WAKE0_REQ_STS",            BIT(27),	1},
	{"PRE_WAKE1_REQ_STS",            BIT(28),	1},
	{"PRE_WAKE2_REQ_STS",            BIT(29),	1},
	{"D2D_DISP_EDP_QACTIVE_REQ_STS", BIT(31),	1},
	{}
};

static const struct pmc_bit_map nvl_pcds_rsc_status_map[] = {
	{"CORE",		0,		1},
	{"Memory",		0,		1},
	{"PRIM_D2D",		0,		1},
	{"PSF0",		0,		1},
	{"SB",			0,		1},
	{}
};

static const struct pmc_bit_map nvl_pcds_signal_status_map[] = {
	{"LSX_Wake0_STS",		 BIT(0),	0},
	{"LSX_Wake1_STS",		 BIT(1),	0},
	{"LSX_Wake2_STS",		 BIT(2),	0},
	{"LSX_Wake3_STS",		 BIT(3),	0},
	{"LSX_Wake4_STS",		 BIT(4),	0},
	{"LSX_Wake5_STS",		 BIT(5),	0},
	{"LSX_Wake6_STS",		 BIT(6),	0},
	{"LSX_Wake7_STS",		 BIT(7),	0},
	{"LPSS_Wake0_STS",		 BIT(8),	1},
	{"LPSS_Wake1_STS",		 BIT(9),	1},
	{"Int_Timer_SS_Wake0_STS",	 BIT(10),	1},
	{"Int_Timer_SS_Wake1_STS",	 BIT(11),	1},
	{"Int_Timer_SS_Wake2_STS",	 BIT(12),	1},
	{"Int_Timer_SS_Wake3_STS",	 BIT(13),	1},
	{"Int_Timer_SS_Wake4_STS",	 BIT(14),	1},
	{"Int_Timer_SS_Wake5_STS",	 BIT(15),	1},
	{}
};

static const struct pmc_bit_map *nvl_pcds_lpm_maps[] = {
	nvl_pcds_clocksource_status_map,
	nvl_pcds_power_gating_status_0_map,
	nvl_pcds_power_gating_status_1_map,
	nvl_pcds_power_gating_status_2_map,
	nvl_pcds_d3_status_0_map,
	nvl_pcds_d3_status_1_map,
	nvl_pcds_d3_status_2_map,
	nvl_pcds_d3_status_3_map,
	nvl_pcds_vnn_req_status_0_map,
	nvl_pcds_vnn_req_status_1_map,
	nvl_pcds_vnn_req_status_2_map,
	nvl_pcds_vnn_req_status_3_map,
	nvl_pcds_vnn_misc_status_map,
	nvl_pcds_signal_status_map,
	NULL
};

static const struct pmc_bit_map *nvl_pcds_blk_maps[] = {
	nvl_pcds_power_gating_status_0_map,
	nvl_pcds_power_gating_status_1_map,
	nvl_pcds_power_gating_status_2_map,
	nvl_pcds_rsc_status_map,
	nvl_pcds_vnn_req_status_0_map,
	nvl_pcds_vnn_req_status_1_map,
	nvl_pcds_vnn_req_status_2_map,
	nvl_pcds_vnn_req_status_3_map,
	nvl_pcds_d3_status_0_map,
	nvl_pcds_d3_status_1_map,
	nvl_pcds_d3_status_2_map,
	nvl_pcds_d3_status_3_map,
	nvl_pcds_clocksource_status_map,
	nvl_pcds_vnn_misc_status_map,
	nvl_pcds_signal_status_map,
	NULL
};

static const struct pmc_bit_map nvl_pchs_pfear_map[] = {
	{"PMC_PGD0",                 BIT(0)},
	{"FIA_D_PGD0",               BIT(1)},
	{"SPI_PGD0",                 BIT(2)},
	{"XHCI_PGD0",                BIT(3)},
	{"SPA_PGD0",                 BIT(4)},
	{"SPB_PGD0",                 BIT(5)},
	{"MPFPW2_PGD0",              BIT(6)},
	{"GBE_PGD0",                 BIT(7)},

	{"RSVD8",                    BIT(0)},
	{"PSF3_PGD0",                BIT(1)},
	{"SBR5_PGD0",                BIT(2)},
	{"SBR0_PGD0",                BIT(3)},
	{"RSVD12",                   BIT(4)},
	{"D2D_DISP_PGD1",            BIT(5)},
	{"LPSS_PGD0",                BIT(6)},
	{"LPC_PGD0",                 BIT(7)},

	{"SMB_PGD0",                 BIT(0)},
	{"ISH_PGD0",                 BIT(1)},
	{"P2SB_PGD0",                BIT(2)},
	{"NPK_PGD0",                 BIT(3)},
	{"D2D_NOC_PGD1",             BIT(4)},
	{"EAH_PGD0",                 BIT(5)},
	{"FUSE_PGD0",                BIT(6)},
	{"SBR8_PGD0",                BIT(7)},

	{"PSF7_PGD0",                BIT(0)},
	{"OTG_PGD0",                 BIT(1)},
	{"EXI_PGD0",                 BIT(2)},
	{"CSE_PGD0",                 BIT(3)},
	{"CSME_KVM_PGD0",            BIT(4)},
	{"CSME_PMT_PGD0",            BIT(5)},
	{"CSME_CLINK_PGD0",          BIT(6)},
	{"CSME_PTIO_PGD0",           BIT(7)},

	{"CSME_USBR_PGD0",           BIT(0)},
	{"SBR1_PGD0",                BIT(1)},
	{"CSME_SMT1_PGD0",           BIT(2)},
	{"MPFPW1_PGD0",              BIT(3)},
	{"CSME_SMS2_PGD0",           BIT(4)},
	{"CSME_SMS_PGD0",            BIT(5)},
	{"CSME_RTC_PGD0",            BIT(6)},
	{"CSMEPSF_PGD0",             BIT(7)},

	{"D2D_NOC_PGD0",             BIT(0)},
	{"ESE_PGD0",                 BIT(1)},
	{"SBR2_PGD0",                BIT(2)},
	{"SBR3_PGD0",                BIT(3)},
	{"SBR4_PGD0",                BIT(4)},
	{"RSVD45",                   BIT(5)},
	{"D2D_DISP_PGD0",            BIT(6)},
	{"PSF1_PGD0",                BIT(7)},

	{"U3FPW1_PGD0",              BIT(0)},
	{"DMI3FPW_PGD0",             BIT(1)},
	{"PSF4_PGD0",                BIT(2)},
	{"CNVI_PGD0",                BIT(3)},
	{"RSVD52",                   BIT(4)},
	{"ENDBG_PGD0",               BIT(5)},
	{"DBC_PGD0",                 BIT(6)},
	{"SMT4_PGD0",                BIT(7)},

	{"RSVD56",                   BIT(0)},
	{"NPK_PGD1",                 BIT(1)},
	{"RSVD58",                   BIT(2)},
	{"DMI3_PGD0",                BIT(3)},
	{"RSVD60",                   BIT(4)},
	{"FIACPCB_D_PGD0",           BIT(5)},
	{"RSVD62",                   BIT(6)},
	{"FIA_U_PGD0",               BIT(7)},

	{"FIACPCB_PGS_PGD0",         BIT(0)},
	{"FIA_PGS_PGD0",             BIT(1)},
	{"RSVD66",                   BIT(2)},
	{"FIACPCB_U_PGD0",           BIT(3)},
	{"TAM_PGD0",                 BIT(4)},
	{"D2D_NOC_PGD2",             BIT(5)},
	{"PSF2_PGD0",                BIT(6)},
	{"THC0_PGD0",                BIT(7)},

	{"THC1_PGD0",                BIT(0)},
	{"PMC_PGD1",                 BIT(1)},
	{"SBR9_PGD0",                BIT(2)},
	{"U3FPW2_PGD0",              BIT(3)},
	{"RSVD76",                   BIT(4)},
	{"DBG_PSF_PGD0",             BIT(5)},
	{"DBG_SBR_PGD0",             BIT(6)},
	{"SBR6_PGD0",                BIT(7)},

	{"SPC_PGD0",                 BIT(0)},
	{"ACE_PGD0",                 BIT(1)},
	{"ACE_PGD1",                 BIT(2)},
	{"ACE_PGD2",                 BIT(3)},
	{"ACE_PGD3",                 BIT(4)},
	{"ACE_PGD4",                 BIT(5)},
	{"ACE_PGD5",                 BIT(6)},
	{"ACE_PGD6",                 BIT(7)},

	{"ACE_PGD7",                 BIT(0)},
	{"ACE_PGD8",                 BIT(1)},
	{"ACE_PGD9",                 BIT(2)},
	{"ACE_PGD10",                BIT(3)},
	{"U3FPW3_PGD0",              BIT(4)},
	{"SBR7_PGD0",                BIT(5)},
	{"OSSE_PGD0",                BIT(6)},
	{"ST_PGD0",                  BIT(7)},
	{}
};

static const struct pmc_bit_map *ext_nvl_pchs_pfear_map[] = {
	nvl_pchs_pfear_map,
	NULL
};

static const struct pmc_bit_map nvl_pchs_clocksource_status_map[] = {
	{"AON2_OFF_STS",                 BIT(0),	1},
	{"AON3_OFF_STS",                 BIT(1),	0},
	{"AON4_OFF_STS",                 BIT(2),	0},
	{"AON2_SPL_OFF_STS",             BIT(3),	0},
	{"AONL_OFF_STS",                 BIT(4),	0},
	{"XTAL_LVM_OFF_STS",             BIT(5),	0},
	{"AON5_OFF_STS",                 BIT(6),	0},
	{"USB3_PLL_OFF_STS",             BIT(8),	1},
	{"MAIN_CRO_OFF_STS",             BIT(11),	0},
	{"MAIN_DIVIDER_OFF_STS",         BIT(12),	1},
	{"REF_PLL_NON_OC_OFF_STS",       BIT(13),	1},
	{"DMI_PLL_OFF_STS",              BIT(14),	1},
	{"PHY_EXT_INJ_OFF_STS",          BIT(15),	1},
	{"AON6_MCRO_OFF_STS",            BIT(16),	0},
	{"XTAL_AGGR_OFF_STS",            BIT(17),	0},
	{"USB2_PLL_OFF_STS",             BIT(18),	1},
	{"GBE_PLL_OFF_STS",              BIT(21),	1},
	{"SATA_PLL_OFF_STS",             BIT(22),	1},
	{"PCIE0_PLL_OFF_STS",            BIT(23),	1},
	{"PCIE1_PLL_OFF_STS",            BIT(24),	1},
	{"FABRIC_PLL_OFF_STS",           BIT(25),	1},
	{"PCIE2_PLL_OFF_STS",            BIT(26),	1},
	{"REF_PLL_OFF_STS",              BIT(28),	1},
	{"REF38P4_PLL_OFF_STS",          BIT(31),	1},
	{}
};

static const struct pmc_bit_map nvl_pchs_power_gating_status_0_map[] = {
	{"PMC_PGD0_PG_STS",              BIT(0),	0},
	{"FIA_D_PGD0_PG_STS",            BIT(1),	0},
	{"ESPISPI_PGD0_PG_STS",          BIT(2),	0},
	{"XHCI_PGD0_PG_STS",             BIT(3),	0},
	{"SPA_PGD0_PG_STS",              BIT(4),	1},
	{"SPB_PGD0_PG_STS",              BIT(5),	1},
	{"MPFPW2_PGD0_PG_STS",           BIT(6),	0},
	{"GBE_PGD0_PG_STS",              BIT(7),	1},
	{"RSVD_8",                       BIT(8),	0},
	{"PSF3_PGD0_PG_STS",             BIT(9),	0},
	{"SBR5_PGD0_PG_STS",             BIT(10),	0},
	{"SBR0_PGD0_PG_STS",             BIT(11),	0},
	{"RSVD_12",                      BIT(12),	0},
	{"D2D_DISP_PGD1_PG_STS",         BIT(13),	0},
	{"LPSS_PGD0_PG_STS",             BIT(14),	1},
	{"LPC_PGD0_PG_STS",              BIT(15),	0},
	{"SMB_PGD0_PG_STS",              BIT(16),	0},
	{"ISH_PGD0_PG_STS",              BIT(17),	0},
	{"P2S_PGD0_PG_STS",              BIT(18),	0},
	{"NPK_PGD0_PG_STS",              BIT(19),	0},
	{"D2D_NOC_PGD1_PG_STS",          BIT(20),	0},
	{"EAH_PGD0_PG_STS",              BIT(21),	0},
	{"FUSE_PGD0_PG_STS",             BIT(22),	0},
	{"SBR8_PGD0_PG_STS",             BIT(23),	0},
	{"PSF7_PGD0_PG_STS",             BIT(24),	0},
	{"XDCI_PGD0_PG_STS",             BIT(25),	1},
	{"EXI_PGD0_PG_STS",              BIT(26),	0},
	{"CSE_PGD0_PG_STS",              BIT(27),	1},
	{"KVMCC_PGD0_PG_STS",            BIT(28),	1},
	{"PMT_PGD0_PG_STS",              BIT(29),	1},
	{"CLINK_PGD0_PG_STS",            BIT(30),	1},
	{"PTIO_PGD0_PG_STS",             BIT(31),	1},
	{}
};

static const struct pmc_bit_map nvl_pchs_power_gating_status_1_map[] = {
	{"USBR0_PGD0_PG_STS",            BIT(0),	1},
	{"SBR1_PGD0_PG_STS",             BIT(1),	0},
	{"SMT1_PGD0_PG_STS",             BIT(2),	1},
	{"MPFPW1_PGD0_PG_STS",           BIT(3),	0},
	{"SMS2_PGD0_PG_STS",             BIT(4),	1},
	{"SMS1_PGD0_PG_STS",             BIT(5),	1},
	{"CSMERTC_PGD0_PG_STS",          BIT(6),	0},
	{"CSMEPSF_PGD0_PG_STS",          BIT(7),	0},
	{"D2D_NOC_PGD0_PG_STS",          BIT(8),	0},
	{"ESE_PGD0_PG_STS",              BIT(9),	1},
	{"SBR2_PGD0_PG_STS",             BIT(10),	0},
	{"SBR3_PGD0_PG_STS",             BIT(11),	0},
	{"SBR4_PGD0_PG_STS",             BIT(12),	0},
	{"RSVD_13",                      BIT(13),	0},
	{"D2D_DISP_PGD0_PG_STS",         BIT(14),	0},
	{"PSF1_PGD0_PG_STS",             BIT(15),	0},
	{"U3FPW1_PGD0_PG_STS",           BIT(16),	0},
	{"DMI3FPW_PGD0_PG_STS",          BIT(17),	0},
	{"PSF4_PGD0_PG_STS",             BIT(18),	0},
	{"CNVI_PGD0_PG_STS",             BIT(19),	0},
	{"RSVD_20",                      BIT(20),	0},
	{"ENDBG_PGD0_PG_STS",            BIT(21),	0},
	{"DBC_PGD0_PG_STS",              BIT(22),	0},
	{"SMT4_PGD0_PG_STS",             BIT(23),	1},
	{"RSVD_24",                      BIT(24),	0},
	{"NPK_PGD1_PG_STS",              BIT(25),	0},
	{"RSVD_26",                      BIT(26),	0},
	{"DMI3_PGD0_PG_STS",             BIT(27),	1},
	{"RSVD_28",                      BIT(28),	0},
	{"FIACPCB_D_PGD0_PG_STS",        BIT(29),	0},
	{"RSVD_30",                      BIT(30),	0},
	{"FIA_U_PGD0_PG_STS",            BIT(31),	0},
	{}
};

static const struct pmc_bit_map nvl_pchs_power_gating_status_2_map[] = {
	{"FIACPCB_PGS_PGD0_PG_STS",      BIT(0),	0},
	{"FIA_PGS_PGD0_PG_STS",          BIT(1),	0},
	{"RSVD_2",                       BIT(2),	0},
	{"FIACPCB_U_PGD0_PG_STS",        BIT(3),	0},
	{"TAM_PGD0_PG_STS",              BIT(4),	0},
	{"D2D_NOC_PGD2_PG_STS",          BIT(5),	0},
	{"PSF2_PGD0_PG_STS",             BIT(6),	0},
	{"THC0_PGD0_PG_STS",             BIT(7),	1},
	{"THC1_PGD0_PG_STS",             BIT(8),	1},
	{"PMC_PGD1_PG_STS",              BIT(9),	0},
	{"SBR9_PGA0_PGD0_PG_STS",        BIT(10),	0},
	{"U3FPW2_PGD0_PG_STS",           BIT(11),	0},
	{"RSVD_12",                      BIT(12),	0},
	{"DBG_PSF_PGD0_PG_STS",          BIT(13),	0},
	{"DBG_SBR_PGD0_PG_STS",          BIT(14),	0},
	{"SBR6_PGD0_PG_STS",             BIT(15),	0},
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
	{"U3FPW3_PGD0_PG_STS",           BIT(28),	0},
	{"SBR7_PGD0_PG_STS",             BIT(29),	0},
	{"OSSE_PGD0_PG_STS",             BIT(30),	0},
	{"SATA_PGD0_PG_STS",             BIT(31),	1},
	{}
};

static const struct pmc_bit_map nvl_pchs_d3_status_0_map[] = {
	{"LPSS_D3_STS",                  BIT(3),	1},
	{"XDCI_D3_STS",                  BIT(4),	1},
	{"XHCI_D3_STS",                  BIT(5),	0},
	{"SPA_D3_STS",                   BIT(12),	0},
	{"SPB_D3_STS",                   BIT(13),	0},
	{"SPC_D3_STS",                   BIT(14),	0},
	{"ESPISPI_D3_STS",               BIT(18),	0},
	{"SATA_D3_STS",                  BIT(20),	1},
	{}
};

static const struct pmc_bit_map nvl_pchs_d3_status_1_map[] = {
	{"OSSE_D3_STS",                  BIT(6),	0},
	{"GBE_D3_STS",                   BIT(19),	0},
	{"ITSS_D3_STS",                  BIT(23),	0},
	{"P2S_D3_STS",                   BIT(24),	0},
	{"CNVI_D3_STS",                  BIT(27),	0},
	{}
};

static const struct pmc_bit_map nvl_pchs_d3_status_2_map[] = {
	{"CSMERTC_D3_STS",               BIT(1),	0},
	{"CSE_D3_STS",                   BIT(4),	0},
	{"KVMCC_D3_STS",                 BIT(5),	0},
	{"USBR0_D3_STS",                 BIT(6),	0},
	{"ISH_D3_STS",                   BIT(7),	0},
	{"SMT1_D3_STS",                  BIT(8),	0},
	{"SMT2_D3_STS",                  BIT(9),	0},
	{"SMT3_D3_STS",                  BIT(10),	0},
	{"SMT4_D3_STS",                  BIT(11),	0},
	{"SMT5_D3_STS",                  BIT(12),	0},
	{"SMT6_D3_STS",                  BIT(13),	0},
	{"CLINK_D3_STS",                 BIT(14),	0},
	{"PTIO_D3_STS",                  BIT(16),	0},
	{"PMT_D3_STS",                   BIT(17),	0},
	{"SMS1_D3_STS",                  BIT(18),	0},
	{"SMS2_D3_STS",                  BIT(19),	0},
	{}
};

static const struct pmc_bit_map nvl_pchs_d3_status_3_map[] = {
	{"THC0_D3_STS",                  BIT(14),	0},
	{"THC1_D3_STS",                  BIT(15),	0},
	{"ACE_D3_STS",                   BIT(23),	0},
	{}
};

static const struct pmc_bit_map nvl_pchs_vnn_req_status_1_map[] = {
	{"NPK_VNN_REQ_STS",              BIT(4),	0},
	{"OSSE_VNN_REQ_STS",             BIT(6),	0},
	{"DFXAGG_VNN_REQ_STS",           BIT(8),	0},
	{"EXI_VNN_REQ_STS",              BIT(9),	0},
	{"GBE_VNN_REQ_STS",              BIT(19),	0},
	{"SMB_VNN_REQ_STS",              BIT(25),	0},
	{"LPC_VNN_REQ_STS",              BIT(26),	0},
	{}
};

static const struct pmc_bit_map nvl_pchs_vnn_req_status_2_map[] = {
	{"CSMERTC_VNN_REQ_STS",          BIT(1),	0},
	{"CSE_VNN_REQ_STS",              BIT(4),	0},
	{"ISH_VNN_REQ_STS",              BIT(7),	0},
	{"SMT1_VNN_REQ_STS",             BIT(8),	0},
	{"SMT4_VNN_REQ_STS",             BIT(11),	0},
	{"CLINK_VNN_REQ_STS",            BIT(14),	0},
	{"SMS1_VNN_REQ_STS",             BIT(18),	0},
	{"SMS2_VNN_REQ_STS",             BIT(19),	0},
	{"GPIOCOM4_VNN_REQ_STS",         BIT(20),	0},
	{"GPIOCOM3_VNN_REQ_STS",         BIT(21),	0},
	{"GPIOCOM2_VNN_REQ_STS",         BIT(22),	0},
	{"GPIOCOM1_VNN_REQ_STS",         BIT(23),	0},
	{"GPIOCOM0_VNN_REQ_STS",         BIT(24),	0},
	{}
};

static const struct pmc_bit_map nvl_pchs_vnn_misc_status_map[] = {
	{"CPU_C10_REQ_STS",              BIT(0),	0},
	{"TS_OFF_REQ_STS",               BIT(1),	0},
	{"PNDE_MET_REQ_STS",             BIT(2),	1},
	{"PG5_PMA0_GVNN_REQ_STS",        BIT(3),	1},
	{"FW_THROTTLE_ALLOWED_REQ_STS",  BIT(4),	0},
	{"DMI_IN_L1_REQ_STS",            BIT(6),	0},
	{"ISH_VNNAON_REQ_STS",           BIT(7),	0},
	{"PLT_GREATER_REQ_STS",          BIT(11),	1},
	{"ALL_SBR_IDLE_REQ_STS",         BIT(12),	0},
	{"PMC_IDLE_FB_OCP_REQ_STS",      BIT(13),	0},
	{"PM_SYNC_STATES_REQ_STS",       BIT(14),	0},
	{"EA_REQ_STS",                   BIT(15),	0},
	{"DMI_CLKREQ_B_REQ_STS",         BIT(16),	0},
	{"BRK_EV_EN_REQ_STS",            BIT(17),	0},
	{"AUTO_DEMO_EN_REQ_STS",         BIT(18),	0},
	{"ITSS_CLK_SRC_REQ_STS",         BIT(19),	1},
	{"ARC_IDLE_REQ_STS",             BIT(21),	0},
	{"PG5_PMA1_GVNN_REQ_STS",        BIT(22),	1},
	{"FIA_DEEP_PM_REQ_STS",          BIT(23),	0},
	{"XDCI_ATTACHED_REQ_STS",        BIT(24),	0},
	{"ARC_INTERRUPT_WAKE_REQ_STS",   BIT(25),	0},
	{"PRE_WAKE0_REQ_STS",            BIT(27),	1},
	{"PRE_WAKE1_REQ_STS",            BIT(28),	1},
	{"PRE_WAKE2_EN_REQ_STS",         BIT(29),	0},
	{"PG5_PMA2_GVNN_REQ_STS",        BIT(30),	1},
	{}
};

static const struct pmc_bit_map nvl_pchs_rsc_status_map[] = {
	{"Memory",		0,		1},
	{"Memory_NS",		0,		1},
	{"PSF1",		0,		1},
	{"PSF2",		0,		1},
	{"PSF3",		0,		1},
	{"REF_PLL",		0,		1},
	{"SB",			0,		1},
	{}
};

static const struct pmc_bit_map *nvl_pchs_lpm_maps[] = {
	nvl_pchs_clocksource_status_map,
	nvl_pchs_power_gating_status_0_map,
	nvl_pchs_power_gating_status_1_map,
	nvl_pchs_power_gating_status_2_map,
	nvl_pchs_d3_status_0_map,
	nvl_pchs_d3_status_1_map,
	nvl_pchs_d3_status_2_map,
	nvl_pchs_d3_status_3_map,
	nvl_pcds_vnn_req_status_0_map,
	nvl_pchs_vnn_req_status_1_map,
	nvl_pchs_vnn_req_status_2_map,
	nvl_pcdh_vnn_req_status_3_map,
	nvl_pchs_vnn_misc_status_map,
	ptl_pcdp_signal_status_map,
	NULL
};

static const struct pmc_bit_map *nvl_pchs_blk_maps[] = {
	nvl_pchs_power_gating_status_0_map,
	nvl_pchs_power_gating_status_1_map,
	nvl_pchs_power_gating_status_2_map,
	nvl_pchs_rsc_status_map,
	nvl_pchs_d3_status_0_map,
	nvl_pchs_clocksource_status_map,
	nvl_pchs_vnn_misc_status_map,
	NULL
};

static const struct pmc_reg_map nvl_pcdh_reg_map = {
	.pfear_sts = ext_nvl_pcdh_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slp_s0_res_counter_step = TGL_PMC_SLP_S0_RES_COUNTER_STEP,
	.ltr_show_sts = ptl_pcdp_ltr_show_map,
	.msr_sts = msr_map,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = NVL_PCDH_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = NVL_PCDH_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
	.lpm_num_maps = NVL_LPM_NUM_MAPS,
	.ltr_ignore_max = LNL_NUM_IP_IGN_ALLOWED,
	.lpm_res_counter_step_x2 = TGL_PMC_LPM_RES_COUNTER_STEP_X2,
	.etr3_offset = ETR3_OFFSET,
	.lpm_sts_latch_en_offset = MTL_LPM_STATUS_LATCH_EN_OFFSET,
	.lpm_priority_offset = NVL_LPM_PRI_OFFSET,
	.lpm_en_offset = NVL_LPM_EN_OFFSET,
	.lpm_residency_offset = NVL_LPM_RESIDENCY_OFFSET,
	.lpm_sts = nvl_pcdh_lpm_maps,
	.lpm_status_offset = MTL_LPM_STATUS_OFFSET,
	.lpm_live_status_offset = NVL_LPM_LIVE_STATUS_OFFSET,
	.s0ix_blocker_maps = nvl_pcdh_blk_maps,
	.s0ix_blocker_offset = LNL_S0IX_BLOCKER_OFFSET,
	.num_s0ix_blocker = NVL_PCDH_NUM_S0IX_BLOCKER,
	.blocker_req_offset = NVL_PCDH_BLK_REQ_OFFSET,
	.lpm_req_guid = PCDH_LPM_REQ_GUID,
};

static const struct pmc_reg_map nvl_pcds_reg_map = {
	.pfear_sts = ext_nvl_pcds_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slp_s0_res_counter_step = TGL_PMC_SLP_S0_RES_COUNTER_STEP,
	.ltr_show_sts = nvl_pcds_ltr_show_map,
	.msr_sts = msr_map,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = NVL_PCDS_PMC_MMIO_REG_LEN,
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
	.lpm_sts = nvl_pcds_lpm_maps,
	.lpm_status_offset = MTL_LPM_STATUS_OFFSET,
	.lpm_live_status_offset = MTL_LPM_LIVE_STATUS_OFFSET,
	.s0ix_blocker_maps = nvl_pcds_blk_maps,
	.s0ix_blocker_offset = LNL_S0IX_BLOCKER_OFFSET,
	.num_s0ix_blocker = NVL_PCDS_NUM_S0IX_BLOCKER,
	.lpm_req_guid = PCDS_LPM_REQ_GUID,
	.blocker_req_offset = NVL_PCDS_BLK_REQ_OFFSET,
};

static const struct pmc_reg_map nvl_pchs_reg_map = {
	.pfear_sts = ext_nvl_pchs_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slp_s0_res_counter_step = TGL_PMC_SLP_S0_RES_COUNTER_STEP,
	.ltr_show_sts = ptl_pcdp_ltr_show_map,
	.msr_sts = msr_map,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = NVL_PCHS_PMC_MMIO_REG_LEN,
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
	.lpm_sts = nvl_pchs_lpm_maps,
	.lpm_status_offset = MTL_LPM_STATUS_OFFSET,
	.lpm_live_status_offset = MTL_LPM_LIVE_STATUS_OFFSET,
	.s0ix_blocker_maps = nvl_pchs_blk_maps,
	.s0ix_blocker_offset = LNL_S0IX_BLOCKER_OFFSET,
	.num_s0ix_blocker = NVL_PCHS_NUM_S0IX_BLOCKER,
	.blocker_req_offset = NVL_PCHS_BLK_REQ_OFFSET,
	.lpm_req_guid = PCHS_LPM_REQ_GUID,
};

static struct pmc_info nvl_pmc_info_list[] = {
	{
		.devid	= PMC_DEVID_NVL_PCDH,
		.map	= &nvl_pcdh_reg_map,
	},
	{
		.devid  = PMC_DEVID_NVL_PCDS,
		.map    = &nvl_pcds_reg_map,
	},
	{
		.devid  = PMC_DEVID_NVL_PCHS,
		.map    = &nvl_pchs_reg_map,
	},
	{}
};

static const char *nvl_ltr_block_counter_arr[] = {
	"PKGC_PREVENT_LTR_IADOMAIN",
	"PKGC_PREVENT_LTR_GDIE",
	"PKGC_PREVENT_LTR_PCH",
	"PKGC_PREVENT_LTR_DISPLAY",
	"PKGC_PREVENT_LTR_IPU",
	NULL
};

static const char *nvl_pkgc_blocker_residency[] = {
	"PKGC_BLOCK_RESIDENCY_INVALID",
	"PKGC_BLOCK_RESIDENCY_MISC",
	"PKGC_BLOCK_RESIDENCY_CDIE_MISC",
	"PKGC_BLOCK_RESIDENCY_MEDIA_MISC",
	"PKGC_BLOCK_RESIDENCY_GT_MISC",
	"PKGC_BLOCK_RESIDENCY_HUBATOM_MISC",
	"PKGC_BLOCK_RESIDENCY_IPU_BUSY",
	"PKGC_BLOCK_RESIDENCY_IPU_LTR",
	"PKGC_BLOCK_RESIDENCY_IPU_TIMER",
	"PKGC_BLOCK_RESIDENCY_DISP_BUSY",
	"PKGC_BLOCK_RESIDENCY_DISP_LTR",
	"PKGC_BLOCK_RESIDENCY_DISP_TIMER",
	"PKGC_BLOCK_RESIDENCY_VPU_BUSY",
	"PKGC_BLOCK_RESIDENCY_VPU_TIMER",
	"PKGC_BLOCK_RESIDENCY_PMC_BUSY",
	"PKGC_BLOCK_RESIDENCY_PMC_LTR",
	"PKGC_BLOCK_RESIDENCY_PMC_TIMER",
	"PKGC_BLOCK_RESIDENCY_HUBATOM_ARAT",
	"PKGC_BLOCK_RESIDENCY_CDIE0_ARAT",
	"PKGC_BLOCK_RESIDENCY_CDIE1_ARAT",
	"PKGC_BLOCK_RESIDENCY_GT_ARAT",
	"PKGC_BLOCK_RESIDENCY_MEDIA_ARAT",
	"PKGC_BLOCK_RESIDENCY_DEMOTION",
	"PKGC_BLOCK_RESIDENCY_THERMALS",
	"PKGC_BLOCK_RESIDENCY_SNCU",
	"PKGC_BLOCK_RESIDENCY_SVTU",
	"PKGC_BLOCK_RESIDENCY_IAA",
	"PKGC_BLOCK_RESIDENCY_IOC",
	NULL,
};

static const u8 nvl_pmc_list[] = {PMC_IDX_MAIN, PMC_IDX_PCH};

#define NVL_NPU_PCI_DEV                0xd71d

/*
 * Set power state of select devices that do not have drivers to D3
 * so that they do not block Package C entry.
 */
static void nvl_d3_fixup(void)
{
	pmc_core_set_device_d3(NVL_NPU_PCI_DEV);
}

static int nvl_resume(struct pmc_dev *pmcdev)
{
	nvl_d3_fixup();
	return cnl_resume(pmcdev);
}

static int nvl_core_init(struct pmc_dev *pmcdev, struct pmc_dev_info *pmc_dev_info)
{
	nvl_d3_fixup();
	return generic_core_init(pmcdev, pmc_dev_info);
}

static u32 nvl_pmt_dmu_guids[] = {NVL_PMT_DMU_GUID, 0x0};
struct pmc_dev_info nvl_s_pmc_dev = {
	.num_pmcs = ARRAY_SIZE(nvl_pmc_list),
	.pmc_list = nvl_pmc_list,
	.regmap_list = nvl_pmc_info_list,
	.map = &nvl_pcds_reg_map,
	.sub_req_show = &pmc_core_substate_blk_req_fops,
	.suspend = cnl_suspend,
	.resume = nvl_resume,
	.init = nvl_core_init,
	.sub_req = pmc_core_pmt_get_blk_sub_req,
	.dmu_guids = nvl_pmt_dmu_guids,
	.pc_guid = NVL_PMT_PC_GUID,
	.pkgc_ltr_blocker_offset = NVL_LTR_BLK_OFFSET,
	.pkgc_ltr_blocker_counters = nvl_ltr_block_counter_arr,
	.pkgc_blocker_offset = NVL_PKGC_BLK_OFFSET,
	.pkgc_blocker_counters = nvl_pkgc_blocker_residency,
	.ssram_hidden = false,
	.die_c6_offset = NVL_PMT_DMU_DIE_C6_OFFSET,
};

struct pmc_dev_info nvl_h_pmc_dev = {
	.num_pmcs = ARRAY_SIZE(nvl_pmc_list),
	.pmc_list = nvl_pmc_list,
	.regmap_list = nvl_pmc_info_list,
	.map = &nvl_pcdh_reg_map,
	.sub_req_show = &pmc_core_substate_blk_req_fops,
	.suspend = cnl_suspend,
	.resume = nvl_resume,
	.init = nvl_core_init,
	.sub_req = pmc_core_pmt_get_blk_sub_req,
	.dmu_guids = nvl_pmt_dmu_guids,
	.pc_guid = NVL_PMT_PC_GUID,
	.pkgc_ltr_blocker_offset = NVL_LTR_BLK_OFFSET,
	.pkgc_ltr_blocker_counters = nvl_ltr_block_counter_arr,
	.pkgc_blocker_offset = NVL_PKGC_BLK_OFFSET,
	.pkgc_blocker_counters = nvl_pkgc_blocker_residency,
	.ssram_hidden = false,
	.die_c6_offset = NVL_PMT_DMU_DIE_C6_OFFSET,
};
