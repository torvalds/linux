// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2021-2022 NXP. */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include "phy-fsl-lynx-core.h"

#define LYNX_28G_NUM_LANE			8

/* SoC IP wrapper for protocol converters */
#define PCC8					0x10a0
#define PCC8_SGMIIa_KX				BIT(3)
#define PCC8_SGMIIa_CFG				BIT(0)

#define PCCC					0x10b0
#define PCCC_SXGMIIn_XFI			BIT(3)
#define PCCC_SXGMIIn_CFG			BIT(0)

#define PCCD					0x10b4
#define PCCD_E25Gn_CFG				BIT(0)

#define PCCE					0x10b8
#define PCCE_E40Gn_LRV				BIT(3)
#define PCCE_E40Gn_CFG				BIT(0)
#define PCCE_E50Gn_LRV				BIT(3)
#define PCCE_E50GnCFG				BIT(0)
#define PCCE_E100Gn_LRV				BIT(3)
#define PCCE_E100Gn_CFG				BIT(0)

#define SGMII_CFG(id)				(28 - (id) * 4) /* Offset into PCC8 */
#define SXGMII_CFG(id)				(28 - (id) * 4) /* Offset into PCCC */
#define E25G_CFG(id)				(28 - (id) * 4) /* Offset into PCCD */
#define E40G_CFG(id)				(28 - (id) * 4) /* Offset into PCCE */
#define E50G_CFG(id)				(20 - (id) * 4) /* Offset into PCCE */
#define E100G_CFG(id)				(12 - (id) * 4) /* Offset into PCCE */

/* Per PLL registers */
#define PLLnRSTCTL(pll)				(0x400 + (pll) * 0x100 + 0x0)
#define PLLnRSTCTL_DIS				BIT(24)
#define PLLnRSTCTL_LOCK				BIT(23)

#define PLLnCR0(pll)				(0x400 + (pll) * 0x100 + 0x4)
#define PLLnCR0_REFCLK_SEL			GENMASK(20, 16)
#define PLLnCR0_REFCLK_SEL_100MHZ		0x0
#define PLLnCR0_REFCLK_SEL_125MHZ		0x1
#define PLLnCR0_REFCLK_SEL_156MHZ		0x2
#define PLLnCR0_REFCLK_SEL_150MHZ		0x3
#define PLLnCR0_REFCLK_SEL_161MHZ		0x4

#define PLLnCR1(pll)				(0x400 + (pll) * 0x100 + 0x8)
#define PLLnCR1_FRATE_SEL			GENMASK(28, 24)
#define PLLnCR1_FRATE_5G_10GVCO			0x0
#define PLLnCR1_FRATE_5G_25GVCO			0x10
#define PLLnCR1_FRATE_10G_20GVCO		0x6
#define PLLnCR1_FRATE_12G_25GVCO		0x16

/* Per SerDes lane registers */
/* Lane a General Control Register */
#define LNaGCR0(lane)				(0x800 + (lane) * 0x100 + 0x0)
#define LNaGCR0_PROTO_SEL			GENMASK(7, 3)
#define LNaGCR0_PROTO_SEL_SGMII			0x1
#define LNaGCR0_PROTO_SEL_XFI			0xa
#define LNaGCR0_PROTO_SEL_25G			0x1a
#define LNaGCR0_IF_WIDTH			GENMASK(2, 0)
#define LNaGCR0_IF_WIDTH_10_BIT			0x0
#define LNaGCR0_IF_WIDTH_20_BIT			0x2
#define LNaGCR0_IF_WIDTH_40_BIT			0x4

/* Lane a Tx Reset Control Register */
#define LNaTRSTCTL(lane)			(0x800 + (lane) * 0x100 + 0x20)
#define LNaTRSTCTL_RST_REQ			BIT(31)
#define LNaTRSTCTL_RST_DONE			BIT(30)
#define LNaTRSTCTL_HLT_REQ			BIT(27)
#define LNaTRSTCTL_STP_REQ			BIT(26)
#define LNaTRSTCTL_DIS				BIT(24)

/* Lane a Tx General Control Register */
#define LNaTGCR0(lane)				(0x800 + (lane) * 0x100 + 0x24)
#define LNaTGCR0_USE_PLL			BIT(28)
#define LNaTGCR0_USE_PLLF			0x0
#define LNaTGCR0_USE_PLLS			0x1
#define LNaTGCR0_N_RATE				GENMASK(26, 24)
#define LNaTGCR0_N_RATE_FULL			0x0
#define LNaTGCR0_N_RATE_HALF			0x1
#define LNaTGCR0_N_RATE_QUARTER			0x2
#define LNaTGCR0_N_RATE_DOUBLE			0x3

#define LNaTECR0(lane)				(0x800 + (lane) * 0x100 + 0x30)
#define LNaTECR0_EQ_TYPE			GENMASK(30, 28)
#define LNaTECR0_EQ_SGN_PREQ			BIT(23)
#define LNaTECR0_EQ_PREQ			GENMASK(19, 16)
#define LNaTECR0_EQ_SGN_POST1Q			BIT(15)
#define LNaTECR0_EQ_POST1Q			GENMASK(12, 8)
#define LNaTECR0_EQ_AMP_RED			GENMASK(5, 0)

#define LNaTECR1(lane)				(0x800 + (lane) * 0x100 + 0x34)
#define LNaTECR1_EQ_ADPT_EQ_DRVR_DIS		BIT(31)
#define LNaTECR1_EQ_ADPT_EQ			GENMASK(29, 24)

/* Lane a Rx Reset Control Register */
#define LNaRRSTCTL(lane)			(0x800 + (lane) * 0x100 + 0x40)
#define LNaRRSTCTL_RST_REQ			BIT(31)
#define LNaRRSTCTL_RST_DONE			BIT(30)
#define LNaRRSTCTL_HLT_REQ			BIT(27)
#define LNaRRSTCTL_STP_REQ			BIT(26)
#define LNaRRSTCTL_DIS				BIT(24)
#define LNaRRSTCTL_CDR_LOCK			BIT(12)

/* Lane a Rx General Control Register */
#define LNaRGCR0(lane)				(0x800 + (lane) * 0x100 + 0x44)
#define LNaRGCR0_USE_PLL			BIT(28)
#define LNaRGCR0_USE_PLLF			0x0
#define LNaRGCR0_USE_PLLS			0x1
#define LNaRGCR0_N_RATE				GENMASK(26, 24)
#define LNaRGCR0_N_RATE_FULL			0x0
#define LNaRGCR0_N_RATE_HALF			0x1
#define LNaRGCR0_N_RATE_QUARTER			0x2
#define LNaRGCR0_N_RATE_DOUBLE			0x3

#define LNaRGCR1(lane)				(0x800 + (lane) * 0x100 + 0x48)
#define LNaRGCR1_RX_ORD_ELECIDLE		BIT(31)
#define LNaRGCR1_DATA_LOST_FLT			BIT(30)
#define LNaRGCR1_DATA_LOST			BIT(29)
#define LNaRGCR1_IDLE_CONFIG			BIT(28)
#define LNaRGCR1_ENTER_IDLE_FLT_SEL		GENMASK(26, 24)
#define LNaRGCR1_EXIT_IDLE_FLT_SEL		GENMASK(22, 20)
#define LNaRGCR1_DATA_LOST_TH_SEL		GENMASK(18, 16)
#define LNaRGCR1_EXT_REC_CLK_SEL		GENMASK(10, 8)
#define LNaRGCR1_WAKE_TX_DIS			BIT(5)
#define LNaRGCR1_PHY_RDY			BIT(4)
#define LNaRGCR1_CHANGE_RX_CLK			BIT(3)
#define LNaRGCR1_PWR_MGT			GENMASK(2, 0)

#define LNaRECR0(lane)				(0x800 + (lane) * 0x100 + 0x50)
#define LNaRECR0_EQ_GAINK2_HF_OV_EN		BIT(31)
#define LNaRECR0_EQ_GAINK2_HF_OV		GENMASK(28, 24)
#define LNaRECR0_EQ_GAINK3_MF_OV_EN		BIT(23)
#define LNaRECR0_EQ_GAINK3_MF_OV		GENMASK(20, 16)
#define LNaRECR0_EQ_GAINK4_LF_OV_EN		BIT(7)
#define LNaRECR0_EQ_GAINK4_LF_DIS		BIT(6)
#define LNaRECR0_EQ_GAINK4_LF_OV		GENMASK(4, 0)

#define LNaRECR1(lane)				(0x800 + (lane) * 0x100 + 0x54)
#define LNaRECR1_EQ_BLW_OV_EN			BIT(31)
#define LNaRECR1_EQ_BLW_OV			GENMASK(28, 24)
#define LNaRECR1_EQ_OFFSET_OV_EN		BIT(23)
#define LNaRECR1_EQ_OFFSET_OV			GENMASK(21, 16)

#define LNaRECR2(lane)				(0x800 + (lane) * 0x100 + 0x58)
#define LNaRECR2_EQ_OFFSET_RNG_DBL		BIT(31)
#define LNaRECR2_EQ_BOOST			GENMASK(29, 28)
#define LNaRECR2_EQ_BLW_SEL			GENMASK(25, 24)
#define LNaRECR2_EQ_ZERO			GENMASK(17, 16)
#define LNaRECR2_EQ_IND				GENMASK(13, 12)
#define LNaRECR2_EQ_BIN_DATA_AVG_TC		GENMASK(5, 4)
#define LNaRECR2_SPARE_IN			GENMASK(1, 0)

#define LNaRECR3(lane)				(0x800 + (lane) * 0x100 + 0x5c)
#define LNaRECR3_EQ_SNAP_START			BIT(31)
#define LNaRECR3_EQ_SNAP_DONE			BIT(30)
#define LNaRECR3_EQ_GAINK2_HF_STAT		GENMASK(28, 24)
#define LNaRECR3_EQ_GAINK3_MF_STAT		GENMASK(20, 16)
#define LNaRECR3_SPARE_OUT			GENMASK(13, 12)
#define LNaRECR3_EQ_GAINK4_LF_STAT		GENMASK(4, 0)

#define LNaRECR4(lane)				(0x800 + (lane) * 0x100 + 0x60)
#define LNaRECR4_BLW_STAT			GENMASK(28, 24)
#define LNaRECR4_EQ_OFFSET_STAT			GENMASK(21, 16)
#define LNaRECR4_EQ_BIN_DATA_SEL		GENMASK(15, 12)
#define LNaRECR4_EQ_BIN_DATA			GENMASK(8, 0) /* bit 9 is reserved */
#define LNaRECR4_EQ_BIN_DATA_SGN		BIT(8)

#define LNaRCCR0(lane)				(0x800 + (lane) * 0x100 + 0x68)
#define LNaRCCR0_CAL_EN				BIT(31)
#define LNaRCCR0_MEAS_EN			BIT(30)
#define LNaRCCR0_CAL_BIN_SEL			BIT(28)
#define LNaRCCR0_CAL_DC3_DIS			BIT(27)
#define LNaRCCR0_CAL_DC2_DIS			BIT(26)
#define LNaRCCR0_CAL_DC1_DIS			BIT(25)
#define LNaRCCR0_CAL_DC0_DIS			BIT(24)
#define LNaRCCR0_CAL_AC3_OV_EN			BIT(15)
#define LNaRCCR0_CAL_AC3_OV			GENMASK(11, 8)
#define LNaRCCR0_CAL_AC2_OV_EN			BIT(7)

#define LNaRSCCR0(lane)				(0x800 + (lane) * 0x100 + 0x74)
#define LNaRSCCR0_SMP_OFF_EN			BIT(31)
#define LNaRSCCR0_SMP_OFF_OV_EN			BIT(30)
#define LNaRSCCR0_SMP_MAN_OFF_EN		BIT(29)
#define LNaRSCCR0_SMP_OFF_RNG_OV_EN		BIT(27)
#define LNaRSCCR0_SMP_OFF_RNG_4X_OV		BIT(25)
#define LNaRSCCR0_SMP_OFF_RNG_2X_OV		BIT(24)
#define LNaRSCCR0_SMP_AUTOZ_PD			BIT(23)
#define LNaRSCCR0_SMP_AUTOZ_CTRL		GENMASK(19, 16)
#define LNaRSCCR0_SMP_AUTOZ_D1R			GENMASK(13, 12)
#define LNaRSCCR0_SMP_AUTOZ_D1F			GENMASK(9, 8)
#define LNaRSCCR0_SMP_AUTOZ_EG1R		GENMASK(5, 4)
#define LNaRSCCR0_SMP_AUTOZ_EG1F		GENMASK(1, 0)

#define LNaTTLCR0(lane)				(0x800 + (lane) * 0x100 + 0x80)
#define LNaTTLCR0_TTL_FLT_SEL			GENMASK(29, 24)
#define LNaTTLCR0_TTL_SLO_PM_BYP		BIT(22)
#define LNaTTLCR0_STALL_DET_DIS			BIT(21)
#define LNaTTLCR0_INACT_MON_DIS			BIT(20)
#define LNaTTLCR0_CDR_OV			GENMASK(18, 16)
#define LNaTTLCR0_DATA_IN_SSC			BIT(15)
#define LNaTTLCR0_CDR_MIN_SMP_ON		GENMASK(1, 0)

#define LNaTCSR0(lane)				(0x800 + (lane) * 0x100 + 0xa0)
#define LNaTCSR0_SD_STAT_OBS_EN			BIT(31)
#define LNaTCSR0_SD_LPBK_SEL			GENMASK(29, 28)

#define LNaPSS(lane)				(0x1000 + (lane) * 0x4)
#define LNaPSS_TYPE				GENMASK(30, 24)
#define LNaPSS_TYPE_SGMII			(PROTO_SEL_SGMII_BASEX_KX << 2)
#define LNaPSS_TYPE_XFI				(PROTO_SEL_XFI_10GBASER_KR_SXGMII << 2)
#define LNaPSS_TYPE_40G				((PROTO_SEL_XFI_10GBASER_KR_SXGMII << 2) | 3)
#define LNaPSS_TYPE_25G				(PROTO_SEL_25G_50G_100G << 2)
#define LNaPSS_TYPE_100G			((PROTO_SEL_25G_50G_100G << 2) | 2)

/* MDEV_PORT is at the same bitfield address for all protocol converters */
#define MDEV_PORT				GENMASK(31, 27)

#define SGMIIaCR0(lane)				(0x1800 + (lane) * 0x10)
#define SGMIIaCR1(lane)				(0x1804 + (lane) * 0x10)
#define SGMIIaCR1_SGPCS_EN			BIT(11)

#define ANLTaCR0(lane)				(0x1a00 + (lane) * 0x10)
#define ANLTaCR1(lane)				(0x1a04 + (lane) * 0x10)

#define SXGMIIaCR0(lane)			(0x1a80 + (lane) * 0x10)
#define SXGMIIaCR0_RST				BIT(31)
#define SXGMIIaCR0_PD				BIT(30)

#define SXGMIIaCR1(lane)			(0x1a84 + (lane) * 0x10)

#define E25GaCR0(lane)				(0x1b00 + (lane) * 0x10)
#define E25GaCR0_RST				BIT(31)
#define E25GaCR0_PD				BIT(30)

#define E25GaCR1(lane)				(0x1b04 + (lane) * 0x10)

#define E25GaCR2(lane)				(0x1b08 + (lane) * 0x10)
#define E25GaCR2_FEC_ENA			BIT(23)
#define E25GaCR2_FEC_ERR_ENA			BIT(22)
#define E25GaCR2_FEC91_ENA			BIT(20)

#define E40GaCR0(pcvt)				(0x1b40 + (pcvt) * 0x20)
#define E40GaCR1(pcvt)				(0x1b44 + (pcvt) * 0x20)

#define E50GaCR1(pcvt)				(0x1b84 + (pcvt) * 0x10)

#define E100GaCR1(pcvt)				(0x1c04 + (pcvt) * 0x20)

#define CR(x)					((x) * 4)

#define LYNX_28G_LANE_HALT_SLEEP_US		100
#define LYNX_28G_LANE_HALT_TIMEOUT_US		1000000

#define LYNX_28G_LANE_RESET_SLEEP_US		100
#define LYNX_28G_LANE_RESET_TIMEOUT_US		1000000

#define LYNX_28G_LANE_STOP_SLEEP_US		100
#define LYNX_28G_LANE_STOP_TIMEOUT_US		1000000

#define lynx_28g_lane_rmw			lynx_lane_rmw
#define lynx_28g_lane_read			lynx_lane_read
#define lynx_28g_lane_write			lynx_lane_write

#define lynx_28g_priv				lynx_priv
#define lynx_28g_lane				lynx_lane
#define lynx_28g_pll				lynx_pll

enum lynx_28g_eq_type {
	EQ_TYPE_NO_EQ = 0,
	EQ_TYPE_2TAP = 1,
	EQ_TYPE_3TAP = 2,
};

enum lynx_28g_proto_sel {
	PROTO_SEL_PCIE = 0,
	PROTO_SEL_SGMII_BASEX_KX = 1,
	PROTO_SEL_SATA = 2,
	PROTO_SEL_XAUI = 4,
	PROTO_SEL_XFI_10GBASER_KR_SXGMII = 0xa,
	PROTO_SEL_25G_50G_100G = 0x1a,
};

struct lynx_28g_proto_conf {
	/* LNaGCR0 */
	int proto_sel;
	int if_width;
	/* LNaTECR0 */
	int teq_type;
	int sgn_preq;
	int ratio_preq;
	int sgn_post1q;
	int ratio_post1q;
	int amp_red;
	/* LNaTECR1 */
	int adpt_eq;
	/* LNaRGCR1 */
	int enter_idle_flt_sel;
	int exit_idle_flt_sel;
	int data_lost_th_sel;
	/* LNaRECR0 */
	int gk2ovd;
	int gk3ovd;
	int gk4ovd;
	int gk2ovd_en;
	int gk3ovd_en;
	int gk4ovd_en;
	/* LNaRECR1 ? */
	int eq_offset_ovd;
	int eq_offset_ovd_en;
	/* LNaRECR2 */
	int eq_offset_rng_dbl;
	int eq_blw_sel;
	int eq_boost;
	int spare_in;
	/* LNaRSCCR0 */
	int smp_autoz_d1r;
	int smp_autoz_eg1r;
	/* LNaRCCR0 */
	int rccr0;
	/* LNaTTLCR0 */
	int ttlcr0;
};

static const struct lynx_28g_proto_conf lynx_28g_proto_conf[LANE_MODE_MAX] = {
	[LANE_MODE_1000BASEX_SGMII] = {
		.proto_sel = LNaGCR0_PROTO_SEL_SGMII,
		.if_width = LNaGCR0_IF_WIDTH_10_BIT,
		.teq_type = EQ_TYPE_NO_EQ,
		.sgn_preq = 1,
		.ratio_preq = 0,
		.sgn_post1q = 1,
		.ratio_post1q = 0,
		.amp_red = 6,
		.adpt_eq = 48,
		.enter_idle_flt_sel = 4,
		.exit_idle_flt_sel = 3,
		.data_lost_th_sel = 1,
		.gk2ovd = 0x1f,
		.gk3ovd = 0,
		.gk4ovd = 0,
		.gk2ovd_en = 1,
		.gk3ovd_en = 1,
		.gk4ovd_en = 0,
		.eq_offset_ovd = 0x1f,
		.eq_offset_ovd_en = 0,
		.eq_offset_rng_dbl = 0,
		.eq_blw_sel = 0,
		.eq_boost = 0,
		.spare_in = 0,
		.smp_autoz_d1r = 0,
		.smp_autoz_eg1r = 0,
		.rccr0 = LNaRCCR0_CAL_EN,
		.ttlcr0 = LNaTTLCR0_TTL_SLO_PM_BYP |
			  LNaTTLCR0_DATA_IN_SSC,
	},
	[LANE_MODE_USXGMII] = {
		.proto_sel = LNaGCR0_PROTO_SEL_XFI,
		.if_width = LNaGCR0_IF_WIDTH_20_BIT,
		.teq_type = EQ_TYPE_2TAP,
		.sgn_preq = 1,
		.ratio_preq = 0,
		.sgn_post1q = 1,
		.ratio_post1q = 3,
		.amp_red = 7,
		.adpt_eq = 48,
		.enter_idle_flt_sel = 0,
		.exit_idle_flt_sel = 0,
		.data_lost_th_sel = 0,
		.gk2ovd = 0,
		.gk3ovd = 0,
		.gk4ovd = 0,
		.gk2ovd_en = 0,
		.gk3ovd_en = 0,
		.gk4ovd_en = 0,
		.eq_offset_ovd = 0x1f,
		.eq_offset_ovd_en = 0,
		.eq_offset_rng_dbl = 1,
		.eq_blw_sel = 1,
		.eq_boost = 0,
		.spare_in = 0,
		.smp_autoz_d1r = 2,
		.smp_autoz_eg1r = 0,
		.rccr0 = LNaRCCR0_CAL_EN,
		.ttlcr0 = LNaTTLCR0_TTL_SLO_PM_BYP |
			  LNaTTLCR0_DATA_IN_SSC,
	},
	[LANE_MODE_10GBASER] = {
		.proto_sel = LNaGCR0_PROTO_SEL_XFI,
		.if_width = LNaGCR0_IF_WIDTH_20_BIT,
		.teq_type = EQ_TYPE_2TAP,
		.sgn_preq = 1,
		.ratio_preq = 0,
		.sgn_post1q = 1,
		.ratio_post1q = 3,
		.amp_red = 7,
		.adpt_eq = 48,
		.enter_idle_flt_sel = 0,
		.exit_idle_flt_sel = 0,
		.data_lost_th_sel = 0,
		.gk2ovd = 0,
		.gk3ovd = 0,
		.gk4ovd = 0,
		.gk2ovd_en = 0,
		.gk3ovd_en = 0,
		.gk4ovd_en = 0,
		.eq_offset_ovd = 0x1f,
		.eq_offset_ovd_en = 0,
		.eq_offset_rng_dbl = 1,
		.eq_blw_sel = 1,
		.eq_boost = 0,
		.spare_in = 0,
		.smp_autoz_d1r = 2,
		.smp_autoz_eg1r = 0,
		.rccr0 = LNaRCCR0_CAL_EN,
		.ttlcr0 = LNaTTLCR0_TTL_SLO_PM_BYP |
			  LNaTTLCR0_DATA_IN_SSC,
	},
	[LANE_MODE_25GBASER] = {
		.proto_sel = LNaGCR0_PROTO_SEL_25G,
		.if_width = LNaGCR0_IF_WIDTH_40_BIT,
		.teq_type = EQ_TYPE_3TAP,
		.sgn_preq = 1,
		.ratio_preq = 2,
		.sgn_post1q = 1,
		.ratio_post1q = 7,
		.amp_red = 0,
		.adpt_eq = 48,
		.enter_idle_flt_sel = 0,
		.exit_idle_flt_sel = 0,
		.data_lost_th_sel = 0,
		.gk2ovd = 0,
		.gk3ovd = 0,
		.gk4ovd = 5,
		.gk2ovd_en = 0,
		.gk3ovd_en = 0,
		.gk4ovd_en = 1,
		.eq_offset_ovd = 0x1f,
		.eq_offset_ovd_en = 0,
		.eq_offset_rng_dbl = 1,
		.eq_blw_sel = 1,
		.eq_boost = 2,
		.spare_in = 3,
		.smp_autoz_d1r = 2,
		.smp_autoz_eg1r = 2,
		.rccr0 = LNaRCCR0_CAL_EN |
			 LNaRCCR0_CAL_DC3_DIS |
			 LNaRCCR0_CAL_DC2_DIS |
			 LNaRCCR0_CAL_DC1_DIS |
			 LNaRCCR0_CAL_DC0_DIS,
		.ttlcr0 = LNaTTLCR0_DATA_IN_SSC |
			  FIELD_PREP_CONST(LNaTTLCR0_CDR_MIN_SMP_ON, 1),
	},
};

static void lynx_28g_lane_set_nrate(struct lynx_28g_lane *lane,
				    struct lynx_28g_pll *pll,
				    enum lynx_lane_mode lane_mode)
{
	switch (pll->frate_sel) {
	case PLLnCR1_FRATE_5G_10GVCO:
	case PLLnCR1_FRATE_5G_25GVCO:
		switch (lane_mode) {
		case LANE_MODE_1000BASEX_SGMII:
			lynx_28g_lane_rmw(lane, LNaTGCR0,
					  FIELD_PREP(LNaTGCR0_N_RATE, LNaTGCR0_N_RATE_QUARTER),
					  LNaTGCR0_N_RATE);
			lynx_28g_lane_rmw(lane, LNaRGCR0,
					  FIELD_PREP(LNaRGCR0_N_RATE, LNaRGCR0_N_RATE_QUARTER),
					  LNaRGCR0_N_RATE);
			break;
		default:
			break;
		}
		break;
	case PLLnCR1_FRATE_10G_20GVCO:
		switch (lane_mode) {
		case LANE_MODE_10GBASER:
		case LANE_MODE_USXGMII:
			lynx_28g_lane_rmw(lane, LNaTGCR0,
					  FIELD_PREP(LNaTGCR0_N_RATE, LNaTGCR0_N_RATE_FULL),
					  LNaTGCR0_N_RATE);
			lynx_28g_lane_rmw(lane, LNaRGCR0,
					  FIELD_PREP(LNaRGCR0_N_RATE, LNaRGCR0_N_RATE_FULL),
					  LNaRGCR0_N_RATE);
			break;
		default:
			break;
		}
		break;
	case PLLnCR1_FRATE_12G_25GVCO:
		switch (lane_mode) {
		case LANE_MODE_25GBASER:
			lynx_28g_lane_rmw(lane, LNaTGCR0,
					  FIELD_PREP(LNaTGCR0_N_RATE, LNaTGCR0_N_RATE_DOUBLE),
					  LNaTGCR0_N_RATE);
			lynx_28g_lane_rmw(lane, LNaRGCR0,
					  FIELD_PREP(LNaRGCR0_N_RATE, LNaRGCR0_N_RATE_DOUBLE),
					  LNaRGCR0_N_RATE);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void lynx_28g_lane_set_pll(struct lynx_28g_lane *lane,
				  struct lynx_28g_pll *pll)
{
	if (pll->id == 0) {
		lynx_28g_lane_rmw(lane, LNaTGCR0,
				  FIELD_PREP(LNaTGCR0_USE_PLL, LNaTGCR0_USE_PLLF),
				  LNaTGCR0_USE_PLL);
		lynx_28g_lane_rmw(lane, LNaRGCR0,
				  FIELD_PREP(LNaRGCR0_USE_PLL, LNaRGCR0_USE_PLLF),
				  LNaRGCR0_USE_PLL);
	} else {
		lynx_28g_lane_rmw(lane, LNaTGCR0,
				  FIELD_PREP(LNaTGCR0_USE_PLL, LNaTGCR0_USE_PLLS),
				  LNaTGCR0_USE_PLL);
		lynx_28g_lane_rmw(lane, LNaRGCR0,
				  FIELD_PREP(LNaRGCR0_USE_PLL, LNaRGCR0_USE_PLLS),
				  LNaRGCR0_USE_PLL);
	}
}

static bool lynx_28g_lane_halt_done(struct lynx_28g_lane *lane)
{
	u32 trstctl = lynx_28g_lane_read(lane, LNaTRSTCTL);
	u32 rrstctl = lynx_28g_lane_read(lane, LNaRRSTCTL);

	return !(trstctl & LNaTRSTCTL_HLT_REQ) &&
	       !(rrstctl & LNaRRSTCTL_HLT_REQ);
}

static bool lynx_28g_lane_stop_done(struct lynx_28g_lane *lane)
{
	u32 trstctl = lynx_28g_lane_read(lane, LNaTRSTCTL);
	u32 rrstctl = lynx_28g_lane_read(lane, LNaRRSTCTL);

	return !(trstctl & LNaTRSTCTL_STP_REQ) &&
	       !(rrstctl & LNaRRSTCTL_STP_REQ);
}

static bool lynx_28g_lane_reset_done(struct lynx_28g_lane *lane)
{
	u32 trstctl = lynx_28g_lane_read(lane, LNaTRSTCTL);
	u32 rrstctl = lynx_28g_lane_read(lane, LNaRRSTCTL);

	return (trstctl & LNaTRSTCTL_RST_DONE) &&
	       (rrstctl & LNaRRSTCTL_RST_DONE);
}

/* Halting puts the lane in a mode in which it can be reconfigured */
static int lynx_28g_lane_halt(struct phy *phy)
{
	struct lynx_28g_lane *lane = phy_get_drvdata(phy);
	bool done;
	int err;

	/* Issue a halt request */
	lynx_28g_lane_rmw(lane, LNaTRSTCTL, LNaTRSTCTL_HLT_REQ,
			  LNaTRSTCTL_HLT_REQ);
	lynx_28g_lane_rmw(lane, LNaRRSTCTL, LNaRRSTCTL_HLT_REQ,
			  LNaRRSTCTL_HLT_REQ);

	/* Wait until the halting process is complete */
	err = read_poll_timeout(lynx_28g_lane_halt_done, done, done,
				LYNX_28G_LANE_HALT_SLEEP_US,
				LYNX_28G_LANE_HALT_TIMEOUT_US,
				false, lane);
	if (err) {
		dev_err(&phy->dev, "Lane %c halt failed: %pe\n",
			'A' + lane->id, ERR_PTR(err));
	}

	return err;
}

static int lynx_28g_lane_reset(struct phy *phy)
{
	struct lynx_28g_lane *lane = phy_get_drvdata(phy);
	bool done;
	int err;

	/* Issue a reset request on the lane */
	lynx_28g_lane_rmw(lane, LNaTRSTCTL, LNaTRSTCTL_RST_REQ,
			  LNaTRSTCTL_RST_REQ);
	lynx_28g_lane_rmw(lane, LNaRRSTCTL, LNaRRSTCTL_RST_REQ,
			  LNaRRSTCTL_RST_REQ);

	/* Wait until the reset sequence is completed */
	err = read_poll_timeout(lynx_28g_lane_reset_done, done, done,
				LYNX_28G_LANE_RESET_SLEEP_US,
				LYNX_28G_LANE_RESET_TIMEOUT_US,
				false, lane);
	if (err) {
		dev_err(&phy->dev, "Lane %c reset failed: %pe\n",
			'A' + lane->id, ERR_PTR(err));
	}

	return err;
}

static int lynx_28g_power_off(struct phy *phy)
{
	struct lynx_28g_lane *lane = phy_get_drvdata(phy);
	bool done;
	int err;

	if (!lane->powered_up)
		return 0;

	/* Issue a stop request */
	lynx_28g_lane_rmw(lane, LNaTRSTCTL, LNaTRSTCTL_STP_REQ,
			  LNaTRSTCTL_STP_REQ);
	lynx_28g_lane_rmw(lane, LNaRRSTCTL, LNaRRSTCTL_STP_REQ,
			  LNaRRSTCTL_STP_REQ);

	/* Wait until the stop process is complete */
	err = read_poll_timeout(lynx_28g_lane_stop_done, done, done,
				LYNX_28G_LANE_STOP_SLEEP_US,
				LYNX_28G_LANE_STOP_TIMEOUT_US,
				false, lane);
	if (err) {
		dev_err(&phy->dev, "Lane %c stop failed: %pe\n",
			'A' + lane->id, ERR_PTR(err));
	}

	/* Power down the RX and TX portions of the lane */
	lynx_28g_lane_rmw(lane, LNaRRSTCTL, LNaRRSTCTL_DIS,
			  LNaRRSTCTL_DIS);
	lynx_28g_lane_rmw(lane, LNaTRSTCTL, LNaTRSTCTL_DIS,
			  LNaTRSTCTL_DIS);

	lane->powered_up = false;

	return 0;
}

static int lynx_28g_power_on(struct phy *phy)
{
	struct lynx_28g_lane *lane = phy_get_drvdata(phy);
	int err;

	if (lane->powered_up)
		return 0;

	/* Power up the RX and TX portions of the lane */
	lynx_28g_lane_rmw(lane, LNaRRSTCTL, 0, LNaRRSTCTL_DIS);
	lynx_28g_lane_rmw(lane, LNaTRSTCTL, 0, LNaTRSTCTL_DIS);

	err = lynx_28g_lane_reset(phy);
	if (err)
		return err;

	lane->powered_up = true;

	return 0;
}

static int lynx_28g_e25g_pcvt(int lane)
{
	return 7 - lane;
}

static int lynx_28g_get_pccr(enum lynx_lane_mode lane_mode, int lane,
			     struct lynx_pccr *pccr)
{
	switch (lane_mode) {
	case LANE_MODE_1000BASEX_SGMII:
		pccr->offset = PCC8;
		pccr->width = 4;
		pccr->shift = SGMII_CFG(lane);
		break;
	case LANE_MODE_USXGMII:
	case LANE_MODE_10GBASER:
		pccr->offset = PCCC;
		pccr->width = 4;
		pccr->shift = SXGMII_CFG(lane);
		break;
	case LANE_MODE_25GBASER:
		pccr->offset = PCCD;
		pccr->width = 4;
		pccr->shift = E25G_CFG(lynx_28g_e25g_pcvt(lane));
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int lynx_28g_get_pcvt_offset(int lane, enum lynx_lane_mode lane_mode)
{
	switch (lane_mode) {
	case LANE_MODE_1000BASEX_SGMII:
		return SGMIIaCR0(lane);
	case LANE_MODE_USXGMII:
	case LANE_MODE_10GBASER:
		return SXGMIIaCR0(lane);
	case LANE_MODE_25GBASER:
		return E25GaCR0(lynx_28g_e25g_pcvt(lane));
	default:
		return -EOPNOTSUPP;
	}
}

static bool lx2160a_serdes1_lane_supports_mode(int lane,
					       enum lynx_lane_mode mode)
{
	switch (mode) {
	case LANE_MODE_25GBASER:
		return lane != 2 && lane != 3;
	default:
		return true;
	}
}

static bool lx2160a_serdes2_lane_supports_mode(int lane,
					       enum lynx_lane_mode mode)
{
	switch (mode) {
	case LANE_MODE_1000BASEX_SGMII:
		return true;
	case LANE_MODE_USXGMII:
	case LANE_MODE_10GBASER:
		return lane == 6 || lane == 7;
	default:
		return false;
	}
}

static bool lx2160a_serdes3_lane_supports_mode(int lane,
					       enum lynx_lane_mode mode)
{
	/*
	 * Non-networking SerDes, and this driver supports only
	 * networking protocols
	 */
	return false;
}

static bool lx2162a_serdes1_lane_supports_mode(int lane,
					       enum lynx_lane_mode mode)
{
	return true;
}

static bool lx2162a_serdes2_lane_supports_mode(int lane,
					       enum lynx_lane_mode mode)
{
	return lx2160a_serdes2_lane_supports_mode(lane, mode);
}

/* Feature set is not expected to grow for the deprecated compatible string */
static bool lynx_28g_compat_lane_supports_mode(int lane,
					       enum lynx_lane_mode mode)
{
	switch (mode) {
	case LANE_MODE_1000BASEX_SGMII:
	case LANE_MODE_USXGMII:
	case LANE_MODE_10GBASER:
		return true;
	default:
		return false;
	}
}

static void lynx_28g_cdr_lock_check(struct lynx_lane *lane)
{
	u32 rrstctl;
	int err;

	rrstctl = lynx_28g_lane_read(lane, LNaRRSTCTL);
	if (!!(rrstctl & LNaRRSTCTL_CDR_LOCK))
		return;

	lynx_28g_lane_rmw(lane, LNaRRSTCTL, LNaRRSTCTL_RST_REQ,
			  LNaRRSTCTL_RST_REQ);

	err = read_poll_timeout(lynx_28g_lane_read, rrstctl,
				!!(rrstctl & LNaRRSTCTL_RST_DONE),
				LYNX_28G_LANE_RESET_SLEEP_US,
				LYNX_28G_LANE_RESET_TIMEOUT_US,
				false, lane, LNaRRSTCTL);
	if (err) {
		dev_warn_once(&lane->phy->dev,
			      "Lane %c receiver reset failed: %pe\n",
			      'A' + lane->id, ERR_PTR(err));
	}
}

static void lynx_28g_lane_remap_pll(struct lynx_lane *lane,
				    enum lynx_lane_mode lane_mode)
{
	struct lynx_priv *priv = lane->priv;
	struct lynx_pll *pll;

	/* Switch to the PLL that works with this interface type */
	pll = lynx_pll_get(priv, lane_mode);
	if (unlikely(!pll))
		return;

	lynx_28g_lane_set_pll(lane, pll);

	/* Choose the portion of clock net to be used on this lane */
	lynx_28g_lane_set_nrate(lane, pll, lane_mode);
}

static void lynx_28g_lane_change_proto_conf(struct lynx_28g_lane *lane,
					    enum lynx_lane_mode lane_mode)
{
	const struct lynx_28g_proto_conf *conf = &lynx_28g_proto_conf[lane_mode];

	lynx_28g_lane_rmw(lane, LNaGCR0,
			  FIELD_PREP(LNaGCR0_PROTO_SEL, conf->proto_sel) |
			  FIELD_PREP(LNaGCR0_IF_WIDTH, conf->if_width),
			  LNaGCR0_PROTO_SEL | LNaGCR0_IF_WIDTH);

	lynx_28g_lane_rmw(lane, LNaTECR0,
			  FIELD_PREP(LNaTECR0_EQ_TYPE, conf->teq_type) |
			  FIELD_PREP(LNaTECR0_EQ_SGN_PREQ, conf->sgn_preq) |
			  FIELD_PREP(LNaTECR0_EQ_PREQ, conf->ratio_preq) |
			  FIELD_PREP(LNaTECR0_EQ_SGN_POST1Q, conf->sgn_post1q) |
			  FIELD_PREP(LNaTECR0_EQ_POST1Q, conf->ratio_post1q) |
			  FIELD_PREP(LNaTECR0_EQ_AMP_RED, conf->amp_red),
			  LNaTECR0_EQ_TYPE |
			  LNaTECR0_EQ_SGN_PREQ |
			  LNaTECR0_EQ_PREQ |
			  LNaTECR0_EQ_SGN_POST1Q |
			  LNaTECR0_EQ_POST1Q |
			  LNaTECR0_EQ_AMP_RED);

	lynx_28g_lane_rmw(lane, LNaTECR1,
			  FIELD_PREP(LNaTECR1_EQ_ADPT_EQ, conf->adpt_eq),
			  LNaTECR1_EQ_ADPT_EQ);

	lynx_28g_lane_rmw(lane, LNaRGCR1,
			  FIELD_PREP(LNaRGCR1_ENTER_IDLE_FLT_SEL, conf->enter_idle_flt_sel) |
			  FIELD_PREP(LNaRGCR1_EXIT_IDLE_FLT_SEL, conf->exit_idle_flt_sel) |
			  FIELD_PREP(LNaRGCR1_DATA_LOST_TH_SEL, conf->data_lost_th_sel),
			  LNaRGCR1_ENTER_IDLE_FLT_SEL |
			  LNaRGCR1_EXIT_IDLE_FLT_SEL |
			  LNaRGCR1_DATA_LOST_TH_SEL);

	lynx_28g_lane_rmw(lane, LNaRECR0,
			  FIELD_PREP(LNaRECR0_EQ_GAINK2_HF_OV_EN, conf->gk2ovd_en) |
			  FIELD_PREP(LNaRECR0_EQ_GAINK3_MF_OV_EN, conf->gk3ovd_en) |
			  FIELD_PREP(LNaRECR0_EQ_GAINK4_LF_OV_EN, conf->gk4ovd_en) |
			  FIELD_PREP(LNaRECR0_EQ_GAINK2_HF_OV, conf->gk2ovd) |
			  FIELD_PREP(LNaRECR0_EQ_GAINK3_MF_OV, conf->gk3ovd) |
			  FIELD_PREP(LNaRECR0_EQ_GAINK4_LF_OV, conf->gk4ovd),
			  LNaRECR0_EQ_GAINK2_HF_OV |
			  LNaRECR0_EQ_GAINK3_MF_OV |
			  LNaRECR0_EQ_GAINK4_LF_OV |
			  LNaRECR0_EQ_GAINK2_HF_OV_EN |
			  LNaRECR0_EQ_GAINK3_MF_OV_EN |
			  LNaRECR0_EQ_GAINK4_LF_OV_EN);

	lynx_28g_lane_rmw(lane, LNaRECR1,
			  FIELD_PREP(LNaRECR1_EQ_OFFSET_OV, conf->eq_offset_ovd) |
			  FIELD_PREP(LNaRECR1_EQ_OFFSET_OV_EN, conf->eq_offset_ovd_en),
			  LNaRECR1_EQ_OFFSET_OV |
			  LNaRECR1_EQ_OFFSET_OV_EN);

	lynx_28g_lane_rmw(lane, LNaRECR2,
			  FIELD_PREP(LNaRECR2_EQ_OFFSET_RNG_DBL, conf->eq_offset_rng_dbl) |
			  FIELD_PREP(LNaRECR2_EQ_BLW_SEL, conf->eq_blw_sel) |
			  FIELD_PREP(LNaRECR2_EQ_BOOST, conf->eq_boost) |
			  FIELD_PREP(LNaRECR2_SPARE_IN, conf->spare_in),
			  LNaRECR2_EQ_OFFSET_RNG_DBL |
			  LNaRECR2_EQ_BLW_SEL |
			  LNaRECR2_EQ_BOOST |
			  LNaRECR2_SPARE_IN);

	lynx_28g_lane_rmw(lane, LNaRSCCR0,
			  FIELD_PREP(LNaRSCCR0_SMP_AUTOZ_D1R, conf->smp_autoz_d1r) |
			  FIELD_PREP(LNaRSCCR0_SMP_AUTOZ_EG1R, conf->smp_autoz_eg1r),
			  LNaRSCCR0_SMP_AUTOZ_D1R |
			  LNaRSCCR0_SMP_AUTOZ_EG1R);

	lynx_28g_lane_write(lane, LNaRCCR0, conf->rccr0);
	lynx_28g_lane_write(lane, LNaTTLCR0, conf->ttlcr0);
}

static int lynx_28g_lane_disable_pcvt(struct lynx_28g_lane *lane,
				      enum lynx_lane_mode lane_mode)
{
	struct lynx_28g_priv *priv = lane->priv;
	int err;

	spin_lock(&priv->pcc_lock);

	err = lynx_pccr_write(lane, lane_mode, 0);
	if (err)
		goto out;

	switch (lane_mode) {
	case LANE_MODE_1000BASEX_SGMII:
		err = lynx_pcvt_rmw(lane, lane_mode, CR(1), 0,
				    SGMIIaCR1_SGPCS_EN);
		break;
	default:
		err = 0;
	}

out:
	spin_unlock(&priv->pcc_lock);

	return err;
}

static int lynx_28g_lane_enable_pcvt(struct lynx_28g_lane *lane,
				     enum lynx_lane_mode lane_mode)
{
	struct lynx_28g_priv *priv = lane->priv;
	u32 val;
	int err;

	spin_lock(&priv->pcc_lock);

	switch (lane_mode) {
	case LANE_MODE_1000BASEX_SGMII:
		err = lynx_pcvt_rmw(lane, lane_mode, CR(1), SGMIIaCR1_SGPCS_EN,
				    SGMIIaCR1_SGPCS_EN);
		break;
	default:
		err = 0;
	}

	val = 0;

	switch (lane_mode) {
	case LANE_MODE_1000BASEX_SGMII:
		val |= PCC8_SGMIIa_CFG;
		break;
	case LANE_MODE_10GBASER:
		val |= PCCC_SXGMIIn_XFI;
		fallthrough;
	case LANE_MODE_USXGMII:
		val |= PCCC_SXGMIIn_CFG;
		break;
	case LANE_MODE_25GBASER:
		val |= PCCD_E25Gn_CFG;
		break;
	default:
		break;
	}

	err = lynx_pccr_write(lane, lane_mode, val);

	spin_unlock(&priv->pcc_lock);

	return err;
}

static int lynx_28g_validate(struct phy *phy, enum phy_mode mode, int submode,
			     union phy_configure_opts *opts)
{
	return lynx_phy_mode_to_lane_mode(phy, mode, submode, NULL);
}

static int lynx_28g_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct lynx_lane *lane = phy_get_drvdata(phy);
	int powered_up = lane->powered_up;
	enum lynx_lane_mode lane_mode;
	int err;

	err = lynx_phy_mode_to_lane_mode(phy, mode, submode, &lane_mode);
	if (err)
		return err;

	if (lane_mode == lane->mode)
		return 0;

	/* If the lane is powered up, put the lane into the halt state while
	 * the reconfiguration is being done.
	 */
	if (powered_up) {
		err = lynx_28g_lane_halt(phy);
		if (err)
			goto out;
	}

	err = lynx_28g_lane_disable_pcvt(lane, lane->mode);
	if (err)
		goto out;

	lynx_28g_lane_change_proto_conf(lane, lane_mode);
	lynx_28g_lane_remap_pll(lane, lane_mode);
	WARN_ON(lynx_28g_lane_enable_pcvt(lane, lane_mode));

	lane->mode = lane_mode;

out:
	if (powered_up)
		lynx_28g_lane_reset(phy);

	return err;
}

static int lynx_28g_init(struct phy *phy)
{
	struct lynx_28g_lane *lane = phy_get_drvdata(phy);

	/* Mark the fact that the lane was init */
	lane->init = true;

	/* SerDes lanes are powered on at boot time.  Any lane that is managed
	 * by this driver will get powered down at init time aka at dpaa2-eth
	 * probe time.
	 */
	lane->powered_up = true;
	lynx_28g_power_off(phy);

	return 0;
}

static int lynx_28g_exit(struct phy *phy)
{
	struct lynx_28g_lane *lane = phy_get_drvdata(phy);

	/* The lane returns to the state where it isn't managed by the
	 * consumer, so we must treat is as if it isn't initialized, and
	 * always powered on.
	 */
	lane->init = false;
	lane->powered_up = false;

	lynx_28g_power_on(phy);

	return 0;
}

static const struct phy_ops lynx_28g_ops = {
	.init		= lynx_28g_init,
	.exit		= lynx_28g_exit,
	.power_on	= lynx_28g_power_on,
	.power_off	= lynx_28g_power_off,
	.set_mode	= lynx_28g_set_mode,
	.validate	= lynx_28g_validate,
	.owner		= THIS_MODULE,
};

static void lynx_28g_pll_read_configuration(struct lynx_pll *pll)
{
	u32 val;

	val = lynx_pll_read(pll, PLLnRSTCTL);
	pll->enabled = !(val & PLLnRSTCTL_DIS);
	pll->locked = !!(val & PLLnRSTCTL_LOCK);

	val = lynx_pll_read(pll, PLLnCR0);
	pll->refclk_sel = FIELD_GET(PLLnCR0_REFCLK_SEL, val);

	val = lynx_pll_read(pll, PLLnCR1);
	pll->frate_sel = FIELD_GET(PLLnCR1_FRATE_SEL, val);

	if (!pll->enabled)
		return;

	switch (pll->frate_sel) {
	case PLLnCR1_FRATE_5G_10GVCO:
	case PLLnCR1_FRATE_5G_25GVCO:
		/* 5GHz clock net */
		__set_bit(LANE_MODE_1000BASEX_SGMII, pll->supported);
		break;
	case PLLnCR1_FRATE_10G_20GVCO:
		/* 10.3125GHz clock net */
		__set_bit(LANE_MODE_10GBASER, pll->supported);
		__set_bit(LANE_MODE_USXGMII, pll->supported);
		break;
	case PLLnCR1_FRATE_12G_25GVCO:
		/* 12.890625GHz clock net */
		__set_bit(LANE_MODE_25GBASER, pll->supported);
		break;
	default:
		/* 6GHz, 8GHz */
		break;
	}
}

static void lynx_28g_lane_read_configuration(struct lynx_28g_lane *lane)
{
	u32 pccr, pss, protocol;

	pss = lynx_28g_lane_read(lane, LNaPSS);
	protocol = FIELD_GET(LNaPSS_TYPE, pss);
	switch (protocol) {
	case LNaPSS_TYPE_SGMII:
		lane->mode = LANE_MODE_1000BASEX_SGMII;
		break;
	case LNaPSS_TYPE_XFI:
		lynx_pccr_read(lane, LANE_MODE_10GBASER, &pccr);
		if (pccr & PCCC_SXGMIIn_XFI)
			lane->mode = LANE_MODE_10GBASER;
		else
			lane->mode = LANE_MODE_USXGMII;
		break;
	case LNaPSS_TYPE_25G:
		lane->mode = LANE_MODE_25GBASER;
		break;
	default:
		lane->mode = LANE_MODE_UNKNOWN;
	}
}

static const struct lynx_info lynx_info_compat = {
	.get_pccr = lynx_28g_get_pccr,
	.get_pcvt_offset = lynx_28g_get_pcvt_offset,
	.lane_supports_mode = lynx_28g_compat_lane_supports_mode,
	.pll_read_configuration = lynx_28g_pll_read_configuration,
	.lane_read_configuration = lynx_28g_lane_read_configuration,
	.cdr_lock_check = lynx_28g_cdr_lock_check,
	.num_lanes = LYNX_28G_NUM_LANE,
};

static const struct lynx_info lynx_info_lx2160a_serdes1 = {
	.get_pccr = lynx_28g_get_pccr,
	.get_pcvt_offset = lynx_28g_get_pcvt_offset,
	.lane_supports_mode = lx2160a_serdes1_lane_supports_mode,
	.pll_read_configuration = lynx_28g_pll_read_configuration,
	.lane_read_configuration = lynx_28g_lane_read_configuration,
	.cdr_lock_check = lynx_28g_cdr_lock_check,
	.num_lanes = LYNX_28G_NUM_LANE,
};

static const struct lynx_info lynx_info_lx2160a_serdes2 = {
	.get_pccr = lynx_28g_get_pccr,
	.get_pcvt_offset = lynx_28g_get_pcvt_offset,
	.lane_supports_mode = lx2160a_serdes2_lane_supports_mode,
	.pll_read_configuration = lynx_28g_pll_read_configuration,
	.lane_read_configuration = lynx_28g_lane_read_configuration,
	.cdr_lock_check = lynx_28g_cdr_lock_check,
	.num_lanes = LYNX_28G_NUM_LANE,
};

static const struct lynx_info lynx_info_lx2160a_serdes3 = {
	.get_pccr = lynx_28g_get_pccr,
	.get_pcvt_offset = lynx_28g_get_pcvt_offset,
	.lane_supports_mode = lx2160a_serdes3_lane_supports_mode,
	.pll_read_configuration = lynx_28g_pll_read_configuration,
	.lane_read_configuration = lynx_28g_lane_read_configuration,
	.cdr_lock_check = lynx_28g_cdr_lock_check,
	.num_lanes = LYNX_28G_NUM_LANE,
};

static const struct lynx_info lynx_info_lx2162a_serdes1 = {
	.get_pccr = lynx_28g_get_pccr,
	.get_pcvt_offset = lynx_28g_get_pcvt_offset,
	.lane_supports_mode = lx2162a_serdes1_lane_supports_mode,
	.pll_read_configuration = lynx_28g_pll_read_configuration,
	.lane_read_configuration = lynx_28g_lane_read_configuration,
	.cdr_lock_check = lynx_28g_cdr_lock_check,
	.first_lane = 4,
	.num_lanes = LYNX_28G_NUM_LANE,
};

static const struct lynx_info lynx_info_lx2162a_serdes2 = {
	.get_pccr = lynx_28g_get_pccr,
	.get_pcvt_offset = lynx_28g_get_pcvt_offset,
	.lane_supports_mode = lx2162a_serdes2_lane_supports_mode,
	.pll_read_configuration = lynx_28g_pll_read_configuration,
	.lane_read_configuration = lynx_28g_lane_read_configuration,
	.cdr_lock_check = lynx_28g_cdr_lock_check,
	.num_lanes = LYNX_28G_NUM_LANE,
};

static int lynx_28g_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct lynx_info *info;

	/*
	 * If we get here it means we probed on a device tree where
	 * "fsl,lynx-28g" wasn't the fallback, but the sole compatible string.
	 */
	info = of_device_get_match_data(dev);
	if (info == &lynx_info_compat)
		dev_warn(dev, "Please update device tree to use per-device compatible strings\n");

	return lynx_probe(pdev, info, &lynx_28g_ops);
}

static const struct of_device_id lynx_28g_of_match_table[] = {
	{ .compatible = "fsl,lx2160a-serdes1", .data = &lynx_info_lx2160a_serdes1 },
	{ .compatible = "fsl,lx2160a-serdes2", .data = &lynx_info_lx2160a_serdes2 },
	{ .compatible = "fsl,lx2160a-serdes3", .data = &lynx_info_lx2160a_serdes3 },
	{ .compatible = "fsl,lx2162a-serdes1", .data = &lynx_info_lx2162a_serdes1 },
	{ .compatible = "fsl,lx2162a-serdes2", .data = &lynx_info_lx2162a_serdes2 },
	{ .compatible = "fsl,lynx-28g", .data = &lynx_info_compat }, /* fallback, keep last */
	{ },
};
MODULE_DEVICE_TABLE(of, lynx_28g_of_match_table);

static struct platform_driver lynx_28g_driver = {
	.probe = lynx_28g_probe,
	.remove = lynx_remove,
	.driver = {
		.name = "lynx-28g",
		.of_match_table = lynx_28g_of_match_table,
	},
};
module_platform_driver(lynx_28g_driver);

MODULE_IMPORT_NS("PHY_FSL_LYNX");
MODULE_AUTHOR("Ioana Ciornei <ioana.ciornei@nxp.com>");
MODULE_DESCRIPTION("Lynx 28G SerDes PHY driver for Layerscape SoCs");
MODULE_LICENSE("GPL v2");
