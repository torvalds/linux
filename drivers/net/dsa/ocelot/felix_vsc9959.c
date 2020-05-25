// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Copyright 2017 Microsemi Corporation
 * Copyright 2018-2019 NXP Semiconductors
 */
#include <linux/fsl/enetc_mdio.h>
#include <soc/mscc/ocelot_vcap.h>
#include <soc/mscc/ocelot_sys.h>
#include <soc/mscc/ocelot.h>
#include <linux/iopoll.h>
#include <linux/pci.h>
#include "felix.h"

#define VSC9959_VCAP_IS2_CNT		1024
#define VSC9959_VCAP_IS2_ENTRY_WIDTH	376
#define VSC9959_VCAP_PORT_CNT		6

/* TODO: should find a better place for these */
#define USXGMII_BMCR_RESET		BIT(15)
#define USXGMII_BMCR_AN_EN		BIT(12)
#define USXGMII_BMCR_RST_AN		BIT(9)
#define USXGMII_BMSR_LNKS(status)	(((status) & GENMASK(2, 2)) >> 2)
#define USXGMII_BMSR_AN_CMPL(status)	(((status) & GENMASK(5, 5)) >> 5)
#define USXGMII_ADVERTISE_LNKS(x)	(((x) << 15) & BIT(15))
#define USXGMII_ADVERTISE_FDX		BIT(12)
#define USXGMII_ADVERTISE_SPEED(x)	(((x) << 9) & GENMASK(11, 9))
#define USXGMII_LPA_LNKS(lpa)		((lpa) >> 15)
#define USXGMII_LPA_DUPLEX(lpa)		(((lpa) & GENMASK(12, 12)) >> 12)
#define USXGMII_LPA_SPEED(lpa)		(((lpa) & GENMASK(11, 9)) >> 9)

enum usxgmii_speed {
	USXGMII_SPEED_10	= 0,
	USXGMII_SPEED_100	= 1,
	USXGMII_SPEED_1000	= 2,
	USXGMII_SPEED_2500	= 4,
};

static const u32 vsc9959_ana_regmap[] = {
	REG(ANA_ADVLEARN,			0x0089a0),
	REG(ANA_VLANMASK,			0x0089a4),
	REG_RESERVED(ANA_PORT_B_DOMAIN),
	REG(ANA_ANAGEFIL,			0x0089ac),
	REG(ANA_ANEVENTS,			0x0089b0),
	REG(ANA_STORMLIMIT_BURST,		0x0089b4),
	REG(ANA_STORMLIMIT_CFG,			0x0089b8),
	REG(ANA_ISOLATED_PORTS,			0x0089c8),
	REG(ANA_COMMUNITY_PORTS,		0x0089cc),
	REG(ANA_AUTOAGE,			0x0089d0),
	REG(ANA_MACTOPTIONS,			0x0089d4),
	REG(ANA_LEARNDISC,			0x0089d8),
	REG(ANA_AGENCTRL,			0x0089dc),
	REG(ANA_MIRRORPORTS,			0x0089e0),
	REG(ANA_EMIRRORPORTS,			0x0089e4),
	REG(ANA_FLOODING,			0x0089e8),
	REG(ANA_FLOODING_IPMC,			0x008a08),
	REG(ANA_SFLOW_CFG,			0x008a0c),
	REG(ANA_PORT_MODE,			0x008a28),
	REG(ANA_CUT_THRU_CFG,			0x008a48),
	REG(ANA_PGID_PGID,			0x008400),
	REG(ANA_TABLES_ANMOVED,			0x007f1c),
	REG(ANA_TABLES_MACHDATA,		0x007f20),
	REG(ANA_TABLES_MACLDATA,		0x007f24),
	REG(ANA_TABLES_STREAMDATA,		0x007f28),
	REG(ANA_TABLES_MACACCESS,		0x007f2c),
	REG(ANA_TABLES_MACTINDX,		0x007f30),
	REG(ANA_TABLES_VLANACCESS,		0x007f34),
	REG(ANA_TABLES_VLANTIDX,		0x007f38),
	REG(ANA_TABLES_ISDXACCESS,		0x007f3c),
	REG(ANA_TABLES_ISDXTIDX,		0x007f40),
	REG(ANA_TABLES_ENTRYLIM,		0x007f00),
	REG(ANA_TABLES_PTP_ID_HIGH,		0x007f44),
	REG(ANA_TABLES_PTP_ID_LOW,		0x007f48),
	REG(ANA_TABLES_STREAMACCESS,		0x007f4c),
	REG(ANA_TABLES_STREAMTIDX,		0x007f50),
	REG(ANA_TABLES_SEQ_HISTORY,		0x007f54),
	REG(ANA_TABLES_SEQ_MASK,		0x007f58),
	REG(ANA_TABLES_SFID_MASK,		0x007f5c),
	REG(ANA_TABLES_SFIDACCESS,		0x007f60),
	REG(ANA_TABLES_SFIDTIDX,		0x007f64),
	REG(ANA_MSTI_STATE,			0x008600),
	REG(ANA_OAM_UPM_LM_CNT,			0x008000),
	REG(ANA_SG_ACCESS_CTRL,			0x008a64),
	REG(ANA_SG_CONFIG_REG_1,		0x007fb0),
	REG(ANA_SG_CONFIG_REG_2,		0x007fb4),
	REG(ANA_SG_CONFIG_REG_3,		0x007fb8),
	REG(ANA_SG_CONFIG_REG_4,		0x007fbc),
	REG(ANA_SG_CONFIG_REG_5,		0x007fc0),
	REG(ANA_SG_GCL_GS_CONFIG,		0x007f80),
	REG(ANA_SG_GCL_TI_CONFIG,		0x007f90),
	REG(ANA_SG_STATUS_REG_1,		0x008980),
	REG(ANA_SG_STATUS_REG_2,		0x008984),
	REG(ANA_SG_STATUS_REG_3,		0x008988),
	REG(ANA_PORT_VLAN_CFG,			0x007800),
	REG(ANA_PORT_DROP_CFG,			0x007804),
	REG(ANA_PORT_QOS_CFG,			0x007808),
	REG(ANA_PORT_VCAP_CFG,			0x00780c),
	REG(ANA_PORT_VCAP_S1_KEY_CFG,		0x007810),
	REG(ANA_PORT_VCAP_S2_CFG,		0x00781c),
	REG(ANA_PORT_PCP_DEI_MAP,		0x007820),
	REG(ANA_PORT_CPU_FWD_CFG,		0x007860),
	REG(ANA_PORT_CPU_FWD_BPDU_CFG,		0x007864),
	REG(ANA_PORT_CPU_FWD_GARP_CFG,		0x007868),
	REG(ANA_PORT_CPU_FWD_CCM_CFG,		0x00786c),
	REG(ANA_PORT_PORT_CFG,			0x007870),
	REG(ANA_PORT_POL_CFG,			0x007874),
	REG(ANA_PORT_PTP_CFG,			0x007878),
	REG(ANA_PORT_PTP_DLY1_CFG,		0x00787c),
	REG(ANA_PORT_PTP_DLY2_CFG,		0x007880),
	REG(ANA_PORT_SFID_CFG,			0x007884),
	REG(ANA_PFC_PFC_CFG,			0x008800),
	REG_RESERVED(ANA_PFC_PFC_TIMER),
	REG_RESERVED(ANA_IPT_OAM_MEP_CFG),
	REG_RESERVED(ANA_IPT_IPT),
	REG_RESERVED(ANA_PPT_PPT),
	REG_RESERVED(ANA_FID_MAP_FID_MAP),
	REG(ANA_AGGR_CFG,			0x008a68),
	REG(ANA_CPUQ_CFG,			0x008a6c),
	REG_RESERVED(ANA_CPUQ_CFG2),
	REG(ANA_CPUQ_8021_CFG,			0x008a74),
	REG(ANA_DSCP_CFG,			0x008ab4),
	REG(ANA_DSCP_REWR_CFG,			0x008bb4),
	REG(ANA_VCAP_RNG_TYPE_CFG,		0x008bf4),
	REG(ANA_VCAP_RNG_VAL_CFG,		0x008c14),
	REG_RESERVED(ANA_VRAP_CFG),
	REG_RESERVED(ANA_VRAP_HDR_DATA),
	REG_RESERVED(ANA_VRAP_HDR_MASK),
	REG(ANA_DISCARD_CFG,			0x008c40),
	REG(ANA_FID_CFG,			0x008c44),
	REG(ANA_POL_PIR_CFG,			0x004000),
	REG(ANA_POL_CIR_CFG,			0x004004),
	REG(ANA_POL_MODE_CFG,			0x004008),
	REG(ANA_POL_PIR_STATE,			0x00400c),
	REG(ANA_POL_CIR_STATE,			0x004010),
	REG_RESERVED(ANA_POL_STATE),
	REG(ANA_POL_FLOWC,			0x008c48),
	REG(ANA_POL_HYST,			0x008cb4),
	REG_RESERVED(ANA_POL_MISC_CFG),
};

static const u32 vsc9959_qs_regmap[] = {
	REG(QS_XTR_GRP_CFG,			0x000000),
	REG(QS_XTR_RD,				0x000008),
	REG(QS_XTR_FRM_PRUNING,			0x000010),
	REG(QS_XTR_FLUSH,			0x000018),
	REG(QS_XTR_DATA_PRESENT,		0x00001c),
	REG(QS_XTR_CFG,				0x000020),
	REG(QS_INJ_GRP_CFG,			0x000024),
	REG(QS_INJ_WR,				0x00002c),
	REG(QS_INJ_CTRL,			0x000034),
	REG(QS_INJ_STATUS,			0x00003c),
	REG(QS_INJ_ERR,				0x000040),
	REG_RESERVED(QS_INH_DBG),
};

static const u32 vsc9959_s2_regmap[] = {
	REG(S2_CORE_UPDATE_CTRL,		0x000000),
	REG(S2_CORE_MV_CFG,			0x000004),
	REG(S2_CACHE_ENTRY_DAT,			0x000008),
	REG(S2_CACHE_MASK_DAT,			0x000108),
	REG(S2_CACHE_ACTION_DAT,		0x000208),
	REG(S2_CACHE_CNT_DAT,			0x000308),
	REG(S2_CACHE_TG_DAT,			0x000388),
};

static const u32 vsc9959_qsys_regmap[] = {
	REG(QSYS_PORT_MODE,			0x00f460),
	REG(QSYS_SWITCH_PORT_MODE,		0x00f480),
	REG(QSYS_STAT_CNT_CFG,			0x00f49c),
	REG(QSYS_EEE_CFG,			0x00f4a0),
	REG(QSYS_EEE_THRES,			0x00f4b8),
	REG(QSYS_IGR_NO_SHARING,		0x00f4bc),
	REG(QSYS_EGR_NO_SHARING,		0x00f4c0),
	REG(QSYS_SW_STATUS,			0x00f4c4),
	REG(QSYS_EXT_CPU_CFG,			0x00f4e0),
	REG_RESERVED(QSYS_PAD_CFG),
	REG(QSYS_CPU_GROUP_MAP,			0x00f4e8),
	REG_RESERVED(QSYS_QMAP),
	REG_RESERVED(QSYS_ISDX_SGRP),
	REG_RESERVED(QSYS_TIMED_FRAME_ENTRY),
	REG(QSYS_TFRM_MISC,			0x00f50c),
	REG(QSYS_TFRM_PORT_DLY,			0x00f510),
	REG(QSYS_TFRM_TIMER_CFG_1,		0x00f514),
	REG(QSYS_TFRM_TIMER_CFG_2,		0x00f518),
	REG(QSYS_TFRM_TIMER_CFG_3,		0x00f51c),
	REG(QSYS_TFRM_TIMER_CFG_4,		0x00f520),
	REG(QSYS_TFRM_TIMER_CFG_5,		0x00f524),
	REG(QSYS_TFRM_TIMER_CFG_6,		0x00f528),
	REG(QSYS_TFRM_TIMER_CFG_7,		0x00f52c),
	REG(QSYS_TFRM_TIMER_CFG_8,		0x00f530),
	REG(QSYS_RED_PROFILE,			0x00f534),
	REG(QSYS_RES_QOS_MODE,			0x00f574),
	REG(QSYS_RES_CFG,			0x00c000),
	REG(QSYS_RES_STAT,			0x00c004),
	REG(QSYS_EGR_DROP_MODE,			0x00f578),
	REG(QSYS_EQ_CTRL,			0x00f57c),
	REG_RESERVED(QSYS_EVENTS_CORE),
	REG(QSYS_QMAXSDU_CFG_0,			0x00f584),
	REG(QSYS_QMAXSDU_CFG_1,			0x00f5a0),
	REG(QSYS_QMAXSDU_CFG_2,			0x00f5bc),
	REG(QSYS_QMAXSDU_CFG_3,			0x00f5d8),
	REG(QSYS_QMAXSDU_CFG_4,			0x00f5f4),
	REG(QSYS_QMAXSDU_CFG_5,			0x00f610),
	REG(QSYS_QMAXSDU_CFG_6,			0x00f62c),
	REG(QSYS_QMAXSDU_CFG_7,			0x00f648),
	REG(QSYS_PREEMPTION_CFG,		0x00f664),
	REG_RESERVED(QSYS_CIR_CFG),
	REG(QSYS_EIR_CFG,			0x000004),
	REG(QSYS_SE_CFG,			0x000008),
	REG(QSYS_SE_DWRR_CFG,			0x00000c),
	REG_RESERVED(QSYS_SE_CONNECT),
	REG(QSYS_SE_DLB_SENSE,			0x000040),
	REG(QSYS_CIR_STATE,			0x000044),
	REG(QSYS_EIR_STATE,			0x000048),
	REG_RESERVED(QSYS_SE_STATE),
	REG(QSYS_HSCH_MISC_CFG,			0x00f67c),
	REG(QSYS_TAG_CONFIG,			0x00f680),
	REG(QSYS_TAS_PARAM_CFG_CTRL,		0x00f698),
	REG(QSYS_PORT_MAX_SDU,			0x00f69c),
	REG(QSYS_PARAM_CFG_REG_1,		0x00f440),
	REG(QSYS_PARAM_CFG_REG_2,		0x00f444),
	REG(QSYS_PARAM_CFG_REG_3,		0x00f448),
	REG(QSYS_PARAM_CFG_REG_4,		0x00f44c),
	REG(QSYS_PARAM_CFG_REG_5,		0x00f450),
	REG(QSYS_GCL_CFG_REG_1,			0x00f454),
	REG(QSYS_GCL_CFG_REG_2,			0x00f458),
	REG(QSYS_PARAM_STATUS_REG_1,		0x00f400),
	REG(QSYS_PARAM_STATUS_REG_2,		0x00f404),
	REG(QSYS_PARAM_STATUS_REG_3,		0x00f408),
	REG(QSYS_PARAM_STATUS_REG_4,		0x00f40c),
	REG(QSYS_PARAM_STATUS_REG_5,		0x00f410),
	REG(QSYS_PARAM_STATUS_REG_6,		0x00f414),
	REG(QSYS_PARAM_STATUS_REG_7,		0x00f418),
	REG(QSYS_PARAM_STATUS_REG_8,		0x00f41c),
	REG(QSYS_PARAM_STATUS_REG_9,		0x00f420),
	REG(QSYS_GCL_STATUS_REG_1,		0x00f424),
	REG(QSYS_GCL_STATUS_REG_2,		0x00f428),
};

static const u32 vsc9959_rew_regmap[] = {
	REG(REW_PORT_VLAN_CFG,			0x000000),
	REG(REW_TAG_CFG,			0x000004),
	REG(REW_PORT_CFG,			0x000008),
	REG(REW_DSCP_CFG,			0x00000c),
	REG(REW_PCP_DEI_QOS_MAP_CFG,		0x000010),
	REG(REW_PTP_CFG,			0x000050),
	REG(REW_PTP_DLY1_CFG,			0x000054),
	REG(REW_RED_TAG_CFG,			0x000058),
	REG(REW_DSCP_REMAP_DP1_CFG,		0x000410),
	REG(REW_DSCP_REMAP_CFG,			0x000510),
	REG_RESERVED(REW_STAT_CFG),
	REG_RESERVED(REW_REW_STICKY),
	REG_RESERVED(REW_PPT),
};

static const u32 vsc9959_sys_regmap[] = {
	REG(SYS_COUNT_RX_OCTETS,		0x000000),
	REG(SYS_COUNT_RX_MULTICAST,		0x000008),
	REG(SYS_COUNT_RX_SHORTS,		0x000010),
	REG(SYS_COUNT_RX_FRAGMENTS,		0x000014),
	REG(SYS_COUNT_RX_JABBERS,		0x000018),
	REG(SYS_COUNT_RX_64,			0x000024),
	REG(SYS_COUNT_RX_65_127,		0x000028),
	REG(SYS_COUNT_RX_128_255,		0x00002c),
	REG(SYS_COUNT_RX_256_1023,		0x000030),
	REG(SYS_COUNT_RX_1024_1526,		0x000034),
	REG(SYS_COUNT_RX_1527_MAX,		0x000038),
	REG(SYS_COUNT_RX_LONGS,			0x000044),
	REG(SYS_COUNT_TX_OCTETS,		0x000200),
	REG(SYS_COUNT_TX_COLLISION,		0x000210),
	REG(SYS_COUNT_TX_DROPS,			0x000214),
	REG(SYS_COUNT_TX_64,			0x00021c),
	REG(SYS_COUNT_TX_65_127,		0x000220),
	REG(SYS_COUNT_TX_128_511,		0x000224),
	REG(SYS_COUNT_TX_512_1023,		0x000228),
	REG(SYS_COUNT_TX_1024_1526,		0x00022c),
	REG(SYS_COUNT_TX_1527_MAX,		0x000230),
	REG(SYS_COUNT_TX_AGING,			0x000278),
	REG(SYS_RESET_CFG,			0x000e00),
	REG(SYS_SR_ETYPE_CFG,			0x000e04),
	REG(SYS_VLAN_ETYPE_CFG,			0x000e08),
	REG(SYS_PORT_MODE,			0x000e0c),
	REG(SYS_FRONT_PORT_MODE,		0x000e2c),
	REG(SYS_FRM_AGING,			0x000e44),
	REG(SYS_STAT_CFG,			0x000e48),
	REG(SYS_SW_STATUS,			0x000e4c),
	REG_RESERVED(SYS_MISC_CFG),
	REG(SYS_REW_MAC_HIGH_CFG,		0x000e6c),
	REG(SYS_REW_MAC_LOW_CFG,		0x000e84),
	REG(SYS_TIMESTAMP_OFFSET,		0x000e9c),
	REG(SYS_PAUSE_CFG,			0x000ea0),
	REG(SYS_PAUSE_TOT_CFG,			0x000ebc),
	REG(SYS_ATOP,				0x000ec0),
	REG(SYS_ATOP_TOT_CFG,			0x000edc),
	REG(SYS_MAC_FC_CFG,			0x000ee0),
	REG(SYS_MMGT,				0x000ef8),
	REG_RESERVED(SYS_MMGT_FAST),
	REG_RESERVED(SYS_EVENTS_DIF),
	REG_RESERVED(SYS_EVENTS_CORE),
	REG_RESERVED(SYS_CNT),
	REG(SYS_PTP_STATUS,			0x000f14),
	REG(SYS_PTP_TXSTAMP,			0x000f18),
	REG(SYS_PTP_NXT,			0x000f1c),
	REG(SYS_PTP_CFG,			0x000f20),
	REG(SYS_RAM_INIT,			0x000f24),
	REG_RESERVED(SYS_CM_ADDR),
	REG_RESERVED(SYS_CM_DATA_WR),
	REG_RESERVED(SYS_CM_DATA_RD),
	REG_RESERVED(SYS_CM_OP),
	REG_RESERVED(SYS_CM_DATA),
};

static const u32 vsc9959_ptp_regmap[] = {
	REG(PTP_PIN_CFG,                   0x000000),
	REG(PTP_PIN_TOD_SEC_MSB,           0x000004),
	REG(PTP_PIN_TOD_SEC_LSB,           0x000008),
	REG(PTP_PIN_TOD_NSEC,              0x00000c),
	REG(PTP_CFG_MISC,                  0x0000a0),
	REG(PTP_CLK_CFG_ADJ_CFG,           0x0000a4),
	REG(PTP_CLK_CFG_ADJ_FREQ,          0x0000a8),
};

static const u32 vsc9959_gcb_regmap[] = {
	REG(GCB_SOFT_RST,			0x000004),
};

static const u32 *vsc9959_regmap[] = {
	[ANA]	= vsc9959_ana_regmap,
	[QS]	= vsc9959_qs_regmap,
	[QSYS]	= vsc9959_qsys_regmap,
	[REW]	= vsc9959_rew_regmap,
	[SYS]	= vsc9959_sys_regmap,
	[S2]	= vsc9959_s2_regmap,
	[PTP]	= vsc9959_ptp_regmap,
	[GCB]	= vsc9959_gcb_regmap,
};

/* Addresses are relative to the PCI device's base address */
static const struct resource vsc9959_target_io_res[] = {
	[ANA] = {
		.start	= 0x0280000,
		.end	= 0x028ffff,
		.name	= "ana",
	},
	[QS] = {
		.start	= 0x0080000,
		.end	= 0x00800ff,
		.name	= "qs",
	},
	[QSYS] = {
		.start	= 0x0200000,
		.end	= 0x021ffff,
		.name	= "qsys",
	},
	[REW] = {
		.start	= 0x0030000,
		.end	= 0x003ffff,
		.name	= "rew",
	},
	[SYS] = {
		.start	= 0x0010000,
		.end	= 0x001ffff,
		.name	= "sys",
	},
	[S2] = {
		.start	= 0x0060000,
		.end	= 0x00603ff,
		.name	= "s2",
	},
	[PTP] = {
		.start	= 0x0090000,
		.end	= 0x00900cb,
		.name	= "ptp",
	},
	[GCB] = {
		.start	= 0x0070000,
		.end	= 0x00701ff,
		.name	= "devcpu_gcb",
	},
};

static const struct resource vsc9959_port_io_res[] = {
	{
		.start	= 0x0100000,
		.end	= 0x010ffff,
		.name	= "port0",
	},
	{
		.start	= 0x0110000,
		.end	= 0x011ffff,
		.name	= "port1",
	},
	{
		.start	= 0x0120000,
		.end	= 0x012ffff,
		.name	= "port2",
	},
	{
		.start	= 0x0130000,
		.end	= 0x013ffff,
		.name	= "port3",
	},
	{
		.start	= 0x0140000,
		.end	= 0x014ffff,
		.name	= "port4",
	},
	{
		.start	= 0x0150000,
		.end	= 0x015ffff,
		.name	= "port5",
	},
};

/* Port MAC 0 Internal MDIO bus through which the SerDes acting as an
 * SGMII/QSGMII MAC PCS can be found.
 */
static const struct resource vsc9959_imdio_res = {
	.start		= 0x8030,
	.end		= 0x8040,
	.name		= "imdio",
};

static const struct reg_field vsc9959_regfields[] = {
	[ANA_ADVLEARN_VLAN_CHK] = REG_FIELD(ANA_ADVLEARN, 6, 6),
	[ANA_ADVLEARN_LEARN_MIRROR] = REG_FIELD(ANA_ADVLEARN, 0, 5),
	[ANA_ANEVENTS_FLOOD_DISCARD] = REG_FIELD(ANA_ANEVENTS, 30, 30),
	[ANA_ANEVENTS_AUTOAGE] = REG_FIELD(ANA_ANEVENTS, 26, 26),
	[ANA_ANEVENTS_STORM_DROP] = REG_FIELD(ANA_ANEVENTS, 24, 24),
	[ANA_ANEVENTS_LEARN_DROP] = REG_FIELD(ANA_ANEVENTS, 23, 23),
	[ANA_ANEVENTS_AGED_ENTRY] = REG_FIELD(ANA_ANEVENTS, 22, 22),
	[ANA_ANEVENTS_CPU_LEARN_FAILED] = REG_FIELD(ANA_ANEVENTS, 21, 21),
	[ANA_ANEVENTS_AUTO_LEARN_FAILED] = REG_FIELD(ANA_ANEVENTS, 20, 20),
	[ANA_ANEVENTS_LEARN_REMOVE] = REG_FIELD(ANA_ANEVENTS, 19, 19),
	[ANA_ANEVENTS_AUTO_LEARNED] = REG_FIELD(ANA_ANEVENTS, 18, 18),
	[ANA_ANEVENTS_AUTO_MOVED] = REG_FIELD(ANA_ANEVENTS, 17, 17),
	[ANA_ANEVENTS_CLASSIFIED_DROP] = REG_FIELD(ANA_ANEVENTS, 15, 15),
	[ANA_ANEVENTS_CLASSIFIED_COPY] = REG_FIELD(ANA_ANEVENTS, 14, 14),
	[ANA_ANEVENTS_VLAN_DISCARD] = REG_FIELD(ANA_ANEVENTS, 13, 13),
	[ANA_ANEVENTS_FWD_DISCARD] = REG_FIELD(ANA_ANEVENTS, 12, 12),
	[ANA_ANEVENTS_MULTICAST_FLOOD] = REG_FIELD(ANA_ANEVENTS, 11, 11),
	[ANA_ANEVENTS_UNICAST_FLOOD] = REG_FIELD(ANA_ANEVENTS, 10, 10),
	[ANA_ANEVENTS_DEST_KNOWN] = REG_FIELD(ANA_ANEVENTS, 9, 9),
	[ANA_ANEVENTS_BUCKET3_MATCH] = REG_FIELD(ANA_ANEVENTS, 8, 8),
	[ANA_ANEVENTS_BUCKET2_MATCH] = REG_FIELD(ANA_ANEVENTS, 7, 7),
	[ANA_ANEVENTS_BUCKET1_MATCH] = REG_FIELD(ANA_ANEVENTS, 6, 6),
	[ANA_ANEVENTS_BUCKET0_MATCH] = REG_FIELD(ANA_ANEVENTS, 5, 5),
	[ANA_ANEVENTS_CPU_OPERATION] = REG_FIELD(ANA_ANEVENTS, 4, 4),
	[ANA_ANEVENTS_DMAC_LOOKUP] = REG_FIELD(ANA_ANEVENTS, 3, 3),
	[ANA_ANEVENTS_SMAC_LOOKUP] = REG_FIELD(ANA_ANEVENTS, 2, 2),
	[ANA_ANEVENTS_SEQ_GEN_ERR_0] = REG_FIELD(ANA_ANEVENTS, 1, 1),
	[ANA_ANEVENTS_SEQ_GEN_ERR_1] = REG_FIELD(ANA_ANEVENTS, 0, 0),
	[ANA_TABLES_MACACCESS_B_DOM] = REG_FIELD(ANA_TABLES_MACACCESS, 16, 16),
	[ANA_TABLES_MACTINDX_BUCKET] = REG_FIELD(ANA_TABLES_MACTINDX, 11, 12),
	[ANA_TABLES_MACTINDX_M_INDEX] = REG_FIELD(ANA_TABLES_MACTINDX, 0, 10),
	[SYS_RESET_CFG_CORE_ENA] = REG_FIELD(SYS_RESET_CFG, 0, 0),
	[GCB_SOFT_RST_SWC_RST] = REG_FIELD(GCB_SOFT_RST, 0, 0),
};

static const struct ocelot_stat_layout vsc9959_stats_layout[] = {
	{ .offset = 0x00,	.name = "rx_octets", },
	{ .offset = 0x01,	.name = "rx_unicast", },
	{ .offset = 0x02,	.name = "rx_multicast", },
	{ .offset = 0x03,	.name = "rx_broadcast", },
	{ .offset = 0x04,	.name = "rx_shorts", },
	{ .offset = 0x05,	.name = "rx_fragments", },
	{ .offset = 0x06,	.name = "rx_jabbers", },
	{ .offset = 0x07,	.name = "rx_crc_align_errs", },
	{ .offset = 0x08,	.name = "rx_sym_errs", },
	{ .offset = 0x09,	.name = "rx_frames_below_65_octets", },
	{ .offset = 0x0A,	.name = "rx_frames_65_to_127_octets", },
	{ .offset = 0x0B,	.name = "rx_frames_128_to_255_octets", },
	{ .offset = 0x0C,	.name = "rx_frames_256_to_511_octets", },
	{ .offset = 0x0D,	.name = "rx_frames_512_to_1023_octets", },
	{ .offset = 0x0E,	.name = "rx_frames_1024_to_1526_octets", },
	{ .offset = 0x0F,	.name = "rx_frames_over_1526_octets", },
	{ .offset = 0x10,	.name = "rx_pause", },
	{ .offset = 0x11,	.name = "rx_control", },
	{ .offset = 0x12,	.name = "rx_longs", },
	{ .offset = 0x13,	.name = "rx_classified_drops", },
	{ .offset = 0x14,	.name = "rx_red_prio_0", },
	{ .offset = 0x15,	.name = "rx_red_prio_1", },
	{ .offset = 0x16,	.name = "rx_red_prio_2", },
	{ .offset = 0x17,	.name = "rx_red_prio_3", },
	{ .offset = 0x18,	.name = "rx_red_prio_4", },
	{ .offset = 0x19,	.name = "rx_red_prio_5", },
	{ .offset = 0x1A,	.name = "rx_red_prio_6", },
	{ .offset = 0x1B,	.name = "rx_red_prio_7", },
	{ .offset = 0x1C,	.name = "rx_yellow_prio_0", },
	{ .offset = 0x1D,	.name = "rx_yellow_prio_1", },
	{ .offset = 0x1E,	.name = "rx_yellow_prio_2", },
	{ .offset = 0x1F,	.name = "rx_yellow_prio_3", },
	{ .offset = 0x20,	.name = "rx_yellow_prio_4", },
	{ .offset = 0x21,	.name = "rx_yellow_prio_5", },
	{ .offset = 0x22,	.name = "rx_yellow_prio_6", },
	{ .offset = 0x23,	.name = "rx_yellow_prio_7", },
	{ .offset = 0x24,	.name = "rx_green_prio_0", },
	{ .offset = 0x25,	.name = "rx_green_prio_1", },
	{ .offset = 0x26,	.name = "rx_green_prio_2", },
	{ .offset = 0x27,	.name = "rx_green_prio_3", },
	{ .offset = 0x28,	.name = "rx_green_prio_4", },
	{ .offset = 0x29,	.name = "rx_green_prio_5", },
	{ .offset = 0x2A,	.name = "rx_green_prio_6", },
	{ .offset = 0x2B,	.name = "rx_green_prio_7", },
	{ .offset = 0x80,	.name = "tx_octets", },
	{ .offset = 0x81,	.name = "tx_unicast", },
	{ .offset = 0x82,	.name = "tx_multicast", },
	{ .offset = 0x83,	.name = "tx_broadcast", },
	{ .offset = 0x84,	.name = "tx_collision", },
	{ .offset = 0x85,	.name = "tx_drops", },
	{ .offset = 0x86,	.name = "tx_pause", },
	{ .offset = 0x87,	.name = "tx_frames_below_65_octets", },
	{ .offset = 0x88,	.name = "tx_frames_65_to_127_octets", },
	{ .offset = 0x89,	.name = "tx_frames_128_255_octets", },
	{ .offset = 0x8B,	.name = "tx_frames_256_511_octets", },
	{ .offset = 0x8C,	.name = "tx_frames_1024_1526_octets", },
	{ .offset = 0x8D,	.name = "tx_frames_over_1526_octets", },
	{ .offset = 0x8E,	.name = "tx_yellow_prio_0", },
	{ .offset = 0x8F,	.name = "tx_yellow_prio_1", },
	{ .offset = 0x90,	.name = "tx_yellow_prio_2", },
	{ .offset = 0x91,	.name = "tx_yellow_prio_3", },
	{ .offset = 0x92,	.name = "tx_yellow_prio_4", },
	{ .offset = 0x93,	.name = "tx_yellow_prio_5", },
	{ .offset = 0x94,	.name = "tx_yellow_prio_6", },
	{ .offset = 0x95,	.name = "tx_yellow_prio_7", },
	{ .offset = 0x96,	.name = "tx_green_prio_0", },
	{ .offset = 0x97,	.name = "tx_green_prio_1", },
	{ .offset = 0x98,	.name = "tx_green_prio_2", },
	{ .offset = 0x99,	.name = "tx_green_prio_3", },
	{ .offset = 0x9A,	.name = "tx_green_prio_4", },
	{ .offset = 0x9B,	.name = "tx_green_prio_5", },
	{ .offset = 0x9C,	.name = "tx_green_prio_6", },
	{ .offset = 0x9D,	.name = "tx_green_prio_7", },
	{ .offset = 0x9E,	.name = "tx_aged", },
	{ .offset = 0x100,	.name = "drop_local", },
	{ .offset = 0x101,	.name = "drop_tail", },
	{ .offset = 0x102,	.name = "drop_yellow_prio_0", },
	{ .offset = 0x103,	.name = "drop_yellow_prio_1", },
	{ .offset = 0x104,	.name = "drop_yellow_prio_2", },
	{ .offset = 0x105,	.name = "drop_yellow_prio_3", },
	{ .offset = 0x106,	.name = "drop_yellow_prio_4", },
	{ .offset = 0x107,	.name = "drop_yellow_prio_5", },
	{ .offset = 0x108,	.name = "drop_yellow_prio_6", },
	{ .offset = 0x109,	.name = "drop_yellow_prio_7", },
	{ .offset = 0x10A,	.name = "drop_green_prio_0", },
	{ .offset = 0x10B,	.name = "drop_green_prio_1", },
	{ .offset = 0x10C,	.name = "drop_green_prio_2", },
	{ .offset = 0x10D,	.name = "drop_green_prio_3", },
	{ .offset = 0x10E,	.name = "drop_green_prio_4", },
	{ .offset = 0x10F,	.name = "drop_green_prio_5", },
	{ .offset = 0x110,	.name = "drop_green_prio_6", },
	{ .offset = 0x111,	.name = "drop_green_prio_7", },
};

struct vcap_field vsc9959_vcap_is2_keys[] = {
	/* Common: 41 bits */
	[VCAP_IS2_TYPE]				= {  0,   4},
	[VCAP_IS2_HK_FIRST]			= {  4,   1},
	[VCAP_IS2_HK_PAG]			= {  5,   8},
	[VCAP_IS2_HK_IGR_PORT_MASK]		= { 13,   7},
	[VCAP_IS2_HK_RSV2]			= { 20,   1},
	[VCAP_IS2_HK_HOST_MATCH]		= { 21,   1},
	[VCAP_IS2_HK_L2_MC]			= { 22,   1},
	[VCAP_IS2_HK_L2_BC]			= { 23,   1},
	[VCAP_IS2_HK_VLAN_TAGGED]		= { 24,   1},
	[VCAP_IS2_HK_VID]			= { 25,  12},
	[VCAP_IS2_HK_DEI]			= { 37,   1},
	[VCAP_IS2_HK_PCP]			= { 38,   3},
	/* MAC_ETYPE / MAC_LLC / MAC_SNAP / OAM common */
	[VCAP_IS2_HK_L2_DMAC]			= { 41,  48},
	[VCAP_IS2_HK_L2_SMAC]			= { 89,  48},
	/* MAC_ETYPE (TYPE=000) */
	[VCAP_IS2_HK_MAC_ETYPE_ETYPE]		= {137,  16},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD0]	= {153,  16},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD1]	= {169,   8},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD2]	= {177,   3},
	/* MAC_LLC (TYPE=001) */
	[VCAP_IS2_HK_MAC_LLC_L2_LLC]		= {137,  40},
	/* MAC_SNAP (TYPE=010) */
	[VCAP_IS2_HK_MAC_SNAP_L2_SNAP]		= {137,  40},
	/* MAC_ARP (TYPE=011) */
	[VCAP_IS2_HK_MAC_ARP_SMAC]		= { 41,  48},
	[VCAP_IS2_HK_MAC_ARP_ADDR_SPACE_OK]	= { 89,   1},
	[VCAP_IS2_HK_MAC_ARP_PROTO_SPACE_OK]	= { 90,   1},
	[VCAP_IS2_HK_MAC_ARP_LEN_OK]		= { 91,   1},
	[VCAP_IS2_HK_MAC_ARP_TARGET_MATCH]	= { 92,   1},
	[VCAP_IS2_HK_MAC_ARP_SENDER_MATCH]	= { 93,   1},
	[VCAP_IS2_HK_MAC_ARP_OPCODE_UNKNOWN]	= { 94,   1},
	[VCAP_IS2_HK_MAC_ARP_OPCODE]		= { 95,   2},
	[VCAP_IS2_HK_MAC_ARP_L3_IP4_DIP]	= { 97,  32},
	[VCAP_IS2_HK_MAC_ARP_L3_IP4_SIP]	= {129,  32},
	[VCAP_IS2_HK_MAC_ARP_DIP_EQ_SIP]	= {161,   1},
	/* IP4_TCP_UDP / IP4_OTHER common */
	[VCAP_IS2_HK_IP4]			= { 41,   1},
	[VCAP_IS2_HK_L3_FRAGMENT]		= { 42,   1},
	[VCAP_IS2_HK_L3_FRAG_OFS_GT0]		= { 43,   1},
	[VCAP_IS2_HK_L3_OPTIONS]		= { 44,   1},
	[VCAP_IS2_HK_IP4_L3_TTL_GT0]		= { 45,   1},
	[VCAP_IS2_HK_L3_TOS]			= { 46,   8},
	[VCAP_IS2_HK_L3_IP4_DIP]		= { 54,  32},
	[VCAP_IS2_HK_L3_IP4_SIP]		= { 86,  32},
	[VCAP_IS2_HK_DIP_EQ_SIP]		= {118,   1},
	/* IP4_TCP_UDP (TYPE=100) */
	[VCAP_IS2_HK_TCP]			= {119,   1},
	[VCAP_IS2_HK_L4_SPORT]			= {120,  16},
	[VCAP_IS2_HK_L4_DPORT]			= {136,  16},
	[VCAP_IS2_HK_L4_RNG]			= {152,   8},
	[VCAP_IS2_HK_L4_SPORT_EQ_DPORT]		= {160,   1},
	[VCAP_IS2_HK_L4_SEQUENCE_EQ0]		= {161,   1},
	[VCAP_IS2_HK_L4_URG]			= {162,   1},
	[VCAP_IS2_HK_L4_ACK]			= {163,   1},
	[VCAP_IS2_HK_L4_PSH]			= {164,   1},
	[VCAP_IS2_HK_L4_RST]			= {165,   1},
	[VCAP_IS2_HK_L4_SYN]			= {166,   1},
	[VCAP_IS2_HK_L4_FIN]			= {167,   1},
	[VCAP_IS2_HK_L4_1588_DOM]		= {168,   8},
	[VCAP_IS2_HK_L4_1588_VER]		= {176,   4},
	/* IP4_OTHER (TYPE=101) */
	[VCAP_IS2_HK_IP4_L3_PROTO]		= {119,   8},
	[VCAP_IS2_HK_L3_PAYLOAD]		= {127,  56},
	/* IP6_STD (TYPE=110) */
	[VCAP_IS2_HK_IP6_L3_TTL_GT0]		= { 41,   1},
	[VCAP_IS2_HK_L3_IP6_SIP]		= { 42, 128},
	[VCAP_IS2_HK_IP6_L3_PROTO]		= {170,   8},
	/* OAM (TYPE=111) */
	[VCAP_IS2_HK_OAM_MEL_FLAGS]		= {137,   7},
	[VCAP_IS2_HK_OAM_VER]			= {144,   5},
	[VCAP_IS2_HK_OAM_OPCODE]		= {149,   8},
	[VCAP_IS2_HK_OAM_FLAGS]			= {157,   8},
	[VCAP_IS2_HK_OAM_MEPID]			= {165,  16},
	[VCAP_IS2_HK_OAM_CCM_CNTS_EQ0]		= {181,   1},
	[VCAP_IS2_HK_OAM_IS_Y1731]		= {182,   1},
};

struct vcap_field vsc9959_vcap_is2_actions[] = {
	[VCAP_IS2_ACT_HIT_ME_ONCE]		= {  0,  1},
	[VCAP_IS2_ACT_CPU_COPY_ENA]		= {  1,  1},
	[VCAP_IS2_ACT_CPU_QU_NUM]		= {  2,  3},
	[VCAP_IS2_ACT_MASK_MODE]		= {  5,  2},
	[VCAP_IS2_ACT_MIRROR_ENA]		= {  7,  1},
	[VCAP_IS2_ACT_LRN_DIS]			= {  8,  1},
	[VCAP_IS2_ACT_POLICE_ENA]		= {  9,  1},
	[VCAP_IS2_ACT_POLICE_IDX]		= { 10,  9},
	[VCAP_IS2_ACT_POLICE_VCAP_ONLY]		= { 19,  1},
	[VCAP_IS2_ACT_PORT_MASK]		= { 20, 11},
	[VCAP_IS2_ACT_REW_OP]			= { 31,  9},
	[VCAP_IS2_ACT_SMAC_REPLACE_ENA]		= { 40,  1},
	[VCAP_IS2_ACT_RSV]			= { 41,  2},
	[VCAP_IS2_ACT_ACL_ID]			= { 43,  6},
	[VCAP_IS2_ACT_HIT_CNT]			= { 49, 32},
};

static const struct vcap_props vsc9959_vcap_props[] = {
	[VCAP_IS2] = {
		.tg_width = 2,
		.sw_count = 4,
		.entry_count = VSC9959_VCAP_IS2_CNT,
		.entry_width = VSC9959_VCAP_IS2_ENTRY_WIDTH,
		.action_count = VSC9959_VCAP_IS2_CNT +
				VSC9959_VCAP_PORT_CNT + 2,
		.action_width = 89,
		.action_type_width = 1,
		.action_table = {
			[IS2_ACTION_TYPE_NORMAL] = {
				.width = 44,
				.count = 2
			},
			[IS2_ACTION_TYPE_SMAC_SIP] = {
				.width = 6,
				.count = 4
			},
		},
		.counter_words = 4,
		.counter_width = 32,
	},
};

#define VSC9959_INIT_TIMEOUT			50000
#define VSC9959_GCB_RST_SLEEP			100
#define VSC9959_SYS_RAMINIT_SLEEP		80

static int vsc9959_gcb_soft_rst_status(struct ocelot *ocelot)
{
	int val;

	regmap_field_read(ocelot->regfields[GCB_SOFT_RST_SWC_RST], &val);

	return val;
}

static int vsc9959_sys_ram_init_status(struct ocelot *ocelot)
{
	return ocelot_read(ocelot, SYS_RAM_INIT);
}

static int vsc9959_reset(struct ocelot *ocelot)
{
	int val, err;

	/* soft-reset the switch core */
	regmap_field_write(ocelot->regfields[GCB_SOFT_RST_SWC_RST], 1);

	err = readx_poll_timeout(vsc9959_gcb_soft_rst_status, ocelot, val, !val,
				 VSC9959_GCB_RST_SLEEP, VSC9959_INIT_TIMEOUT);
	if (err) {
		dev_err(ocelot->dev, "timeout: switch core reset\n");
		return err;
	}

	/* initialize switch mem ~40us */
	ocelot_write(ocelot, SYS_RAM_INIT_RAM_INIT, SYS_RAM_INIT);
	err = readx_poll_timeout(vsc9959_sys_ram_init_status, ocelot, val, !val,
				 VSC9959_SYS_RAMINIT_SLEEP,
				 VSC9959_INIT_TIMEOUT);
	if (err) {
		dev_err(ocelot->dev, "timeout: switch sram init\n");
		return err;
	}

	/* enable switch core */
	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_CORE_ENA], 1);

	return 0;
}

static void vsc9959_pcs_an_restart_sgmii(struct phy_device *pcs)
{
	phy_set_bits(pcs, MII_BMCR, BMCR_ANRESTART);
}

static void vsc9959_pcs_an_restart_usxgmii(struct phy_device *pcs)
{
	phy_write_mmd(pcs, MDIO_MMD_VEND2, MII_BMCR,
		      USXGMII_BMCR_RESET |
		      USXGMII_BMCR_AN_EN |
		      USXGMII_BMCR_RST_AN);
}

static void vsc9959_pcs_an_restart(struct ocelot *ocelot, int port)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	struct phy_device *pcs = felix->pcs[port];

	if (!pcs)
		return;

	switch (pcs->interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
		vsc9959_pcs_an_restart_sgmii(pcs);
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		vsc9959_pcs_an_restart_usxgmii(pcs);
		break;
	default:
		dev_err(ocelot->dev, "Invalid PCS interface type %s\n",
			phy_modes(pcs->interface));
		break;
	}
}

/* We enable SGMII AN only when the PHY has managed = "in-band-status" in the
 * device tree. If we are in MLO_AN_PHY mode, we program directly state->speed
 * into the PCS, which is retrieved out-of-band over MDIO. This also has the
 * benefit of working with SGMII fixed-links, like downstream switches, where
 * both link partners attempt to operate as AN slaves and therefore AN never
 * completes.  But it also has the disadvantage that some PHY chips don't pass
 * traffic if SGMII AN is enabled but not completed (acknowledged by us), so
 * setting MLO_AN_INBAND is actually required for those.
 */
static void vsc9959_pcs_init_sgmii(struct phy_device *pcs,
				   unsigned int link_an_mode,
				   const struct phylink_link_state *state)
{
	if (link_an_mode == MLO_AN_INBAND) {
		int bmsr, bmcr;

		/* Some PHYs like VSC8234 don't like it when AN restarts on
		 * their system  side and they restart line side AN too, going
		 * into an endless link up/down loop.  Don't restart PCS AN if
		 * link is up already.
		 * We do check that AN is enabled just in case this is the 1st
		 * call, PCS detects a carrier but AN is disabled from power on
		 * or by boot loader.
		 */
		bmcr = phy_read(pcs, MII_BMCR);
		if (bmcr < 0)
			return;

		bmsr = phy_read(pcs, MII_BMSR);
		if (bmsr < 0)
			return;

		if ((bmcr & BMCR_ANENABLE) && (bmsr & BMSR_LSTATUS))
			return;

		/* SGMII spec requires tx_config_Reg[15:0] to be exactly 0x4001
		 * for the MAC PCS in order to acknowledge the AN.
		 */
		phy_write(pcs, MII_ADVERTISE, ADVERTISE_SGMII |
					      ADVERTISE_LPACK);

		phy_write(pcs, ENETC_PCS_IF_MODE,
			  ENETC_PCS_IF_MODE_SGMII_EN |
			  ENETC_PCS_IF_MODE_USE_SGMII_AN);

		/* Adjust link timer for SGMII */
		phy_write(pcs, ENETC_PCS_LINK_TIMER1,
			  ENETC_PCS_LINK_TIMER1_VAL);
		phy_write(pcs, ENETC_PCS_LINK_TIMER2,
			  ENETC_PCS_LINK_TIMER2_VAL);

		phy_write(pcs, MII_BMCR, BMCR_ANRESTART | BMCR_ANENABLE);
	} else {
		int speed;

		if (state->duplex == DUPLEX_HALF) {
			phydev_err(pcs, "Half duplex not supported\n");
			return;
		}
		switch (state->speed) {
		case SPEED_1000:
			speed = ENETC_PCS_SPEED_1000;
			break;
		case SPEED_100:
			speed = ENETC_PCS_SPEED_100;
			break;
		case SPEED_10:
			speed = ENETC_PCS_SPEED_10;
			break;
		case SPEED_UNKNOWN:
			/* Silently don't do anything */
			return;
		default:
			phydev_err(pcs, "Invalid PCS speed %d\n", state->speed);
			return;
		}

		phy_write(pcs, ENETC_PCS_IF_MODE,
			  ENETC_PCS_IF_MODE_SGMII_EN |
			  ENETC_PCS_IF_MODE_SGMII_SPEED(speed));

		/* Yes, not a mistake: speed is given by IF_MODE. */
		phy_write(pcs, MII_BMCR, BMCR_RESET |
					 BMCR_SPEED1000 |
					 BMCR_FULLDPLX);
	}
}

/* 2500Base-X is SerDes protocol 7 on Felix and 6 on ENETC. It is a SerDes lane
 * clocked at 3.125 GHz which encodes symbols with 8b/10b and does not have
 * auto-negotiation of any link parameters. Electrically it is compatible with
 * a single lane of XAUI.
 * The hardware reference manual wants to call this mode SGMII, but it isn't
 * really, since the fundamental features of SGMII:
 * - Downgrading the link speed by duplicating symbols
 * - Auto-negotiation
 * are not there.
 * The speed is configured at 1000 in the IF_MODE and BMCR MDIO registers
 * because the clock frequency is actually given by a PLL configured in the
 * Reset Configuration Word (RCW).
 * Since there is no difference between fixed speed SGMII w/o AN and 802.3z w/o
 * AN, we call this PHY interface type 2500Base-X. In case a PHY negotiates a
 * lower link speed on line side, the system-side interface remains fixed at
 * 2500 Mbps and we do rate adaptation through pause frames.
 */
static void vsc9959_pcs_init_2500basex(struct phy_device *pcs,
				       unsigned int link_an_mode,
				       const struct phylink_link_state *state)
{
	if (link_an_mode == MLO_AN_INBAND) {
		phydev_err(pcs, "AN not supported on 3.125GHz SerDes lane\n");
		return;
	}

	phy_write(pcs, ENETC_PCS_IF_MODE,
		  ENETC_PCS_IF_MODE_SGMII_EN |
		  ENETC_PCS_IF_MODE_SGMII_SPEED(ENETC_PCS_SPEED_2500));

	phy_write(pcs, MII_BMCR, BMCR_SPEED1000 |
				 BMCR_FULLDPLX |
				 BMCR_RESET);
}

static void vsc9959_pcs_init_usxgmii(struct phy_device *pcs,
				     unsigned int link_an_mode,
				     const struct phylink_link_state *state)
{
	if (link_an_mode != MLO_AN_INBAND) {
		phydev_err(pcs, "USXGMII only supports in-band AN for now\n");
		return;
	}

	/* Configure device ability for the USXGMII Replicator */
	phy_write_mmd(pcs, MDIO_MMD_VEND2, MII_ADVERTISE,
		      USXGMII_ADVERTISE_SPEED(USXGMII_SPEED_2500) |
		      USXGMII_ADVERTISE_LNKS(1) |
		      ADVERTISE_SGMII |
		      ADVERTISE_LPACK |
		      USXGMII_ADVERTISE_FDX);
}

static void vsc9959_pcs_init(struct ocelot *ocelot, int port,
			     unsigned int link_an_mode,
			     const struct phylink_link_state *state)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	struct phy_device *pcs = felix->pcs[port];

	if (!pcs)
		return;

	/* The PCS does not implement the BMSR register fully, so capability
	 * detection via genphy_read_abilities does not work. Since we can get
	 * the PHY config word from the LPA register though, there is still
	 * value in using the generic phy_resolve_aneg_linkmode function. So
	 * populate the supported and advertising link modes manually here.
	 */
	linkmode_set_bit_array(phy_basic_ports_array,
			       ARRAY_SIZE(phy_basic_ports_array),
			       pcs->supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT, pcs->supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT, pcs->supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT, pcs->supported);
	if (pcs->interface == PHY_INTERFACE_MODE_2500BASEX ||
	    pcs->interface == PHY_INTERFACE_MODE_USXGMII)
		linkmode_set_bit(ETHTOOL_LINK_MODE_2500baseX_Full_BIT,
				 pcs->supported);
	if (pcs->interface != PHY_INTERFACE_MODE_2500BASEX)
		linkmode_set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
				 pcs->supported);
	phy_advertise_supported(pcs);

	switch (pcs->interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
		vsc9959_pcs_init_sgmii(pcs, link_an_mode, state);
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		vsc9959_pcs_init_2500basex(pcs, link_an_mode, state);
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		vsc9959_pcs_init_usxgmii(pcs, link_an_mode, state);
		break;
	default:
		dev_err(ocelot->dev, "Unsupported link mode %s\n",
			phy_modes(pcs->interface));
	}
}

static void vsc9959_pcs_link_state_resolve(struct phy_device *pcs,
					   struct phylink_link_state *state)
{
	state->an_complete = pcs->autoneg_complete;
	state->an_enabled = pcs->autoneg;
	state->link = pcs->link;
	state->duplex = pcs->duplex;
	state->speed = pcs->speed;
	/* SGMII AN does not negotiate flow control, but that's ok,
	 * since phylink already knows that, and does:
	 *	link_state.pause |= pl->phy_state.pause;
	 */
	state->pause = MLO_PAUSE_NONE;

	phydev_dbg(pcs,
		   "mode=%s/%s/%s adv=%*pb lpa=%*pb link=%u an_enabled=%u an_complete=%u\n",
		   phy_modes(pcs->interface),
		   phy_speed_to_str(pcs->speed),
		   phy_duplex_to_str(pcs->duplex),
		   __ETHTOOL_LINK_MODE_MASK_NBITS, pcs->advertising,
		   __ETHTOOL_LINK_MODE_MASK_NBITS, pcs->lp_advertising,
		   pcs->link, pcs->autoneg, pcs->autoneg_complete);
}

static void vsc9959_pcs_link_state_sgmii(struct phy_device *pcs,
					 struct phylink_link_state *state)
{
	int err;

	err = genphy_update_link(pcs);
	if (err < 0)
		return;

	if (pcs->autoneg_complete) {
		u16 lpa = phy_read(pcs, MII_LPA);

		mii_lpa_to_linkmode_lpa_sgmii(pcs->lp_advertising, lpa);

		phy_resolve_aneg_linkmode(pcs);
	}
}

static void vsc9959_pcs_link_state_2500basex(struct phy_device *pcs,
					     struct phylink_link_state *state)
{
	int err;

	err = genphy_update_link(pcs);
	if (err < 0)
		return;

	pcs->speed = SPEED_2500;
	pcs->asym_pause = true;
	pcs->pause = true;
}

static void vsc9959_pcs_link_state_usxgmii(struct phy_device *pcs,
					   struct phylink_link_state *state)
{
	int status, lpa;

	status = phy_read_mmd(pcs, MDIO_MMD_VEND2, MII_BMSR);
	if (status < 0)
		return;

	pcs->autoneg = true;
	pcs->autoneg_complete = USXGMII_BMSR_AN_CMPL(status);
	pcs->link = USXGMII_BMSR_LNKS(status);

	if (!pcs->link || !pcs->autoneg_complete)
		return;

	lpa = phy_read_mmd(pcs, MDIO_MMD_VEND2, MII_LPA);
	if (lpa < 0)
		return;

	switch (USXGMII_LPA_SPEED(lpa)) {
	case USXGMII_SPEED_10:
		pcs->speed = SPEED_10;
		break;
	case USXGMII_SPEED_100:
		pcs->speed = SPEED_100;
		break;
	case USXGMII_SPEED_1000:
		pcs->speed = SPEED_1000;
		break;
	case USXGMII_SPEED_2500:
		pcs->speed = SPEED_2500;
		break;
	default:
		break;
	}

	if (USXGMII_LPA_DUPLEX(lpa))
		pcs->duplex = DUPLEX_FULL;
	else
		pcs->duplex = DUPLEX_HALF;
}

static void vsc9959_pcs_link_state(struct ocelot *ocelot, int port,
				   struct phylink_link_state *state)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	struct phy_device *pcs = felix->pcs[port];

	if (!pcs)
		return;

	pcs->speed = SPEED_UNKNOWN;
	pcs->duplex = DUPLEX_UNKNOWN;
	pcs->pause = 0;
	pcs->asym_pause = 0;

	switch (pcs->interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
		vsc9959_pcs_link_state_sgmii(pcs, state);
		break;
	case PHY_INTERFACE_MODE_2500BASEX:
		vsc9959_pcs_link_state_2500basex(pcs, state);
		break;
	case PHY_INTERFACE_MODE_USXGMII:
		vsc9959_pcs_link_state_usxgmii(pcs, state);
		break;
	default:
		return;
	}

	vsc9959_pcs_link_state_resolve(pcs, state);
}

static int vsc9959_prevalidate_phy_mode(struct ocelot *ocelot, int port,
					phy_interface_t phy_mode)
{
	switch (phy_mode) {
	case PHY_INTERFACE_MODE_INTERNAL:
		if (port != 4 && port != 5)
			return -ENOTSUPP;
		return 0;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_2500BASEX:
		/* Not supported on internal to-CPU ports */
		if (port == 4 || port == 5)
			return -ENOTSUPP;
		return 0;
	default:
		return -ENOTSUPP;
	}
}

static const struct ocelot_ops vsc9959_ops = {
	.reset			= vsc9959_reset,
};

static int vsc9959_mdio_bus_alloc(struct ocelot *ocelot)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	struct enetc_mdio_priv *mdio_priv;
	struct device *dev = ocelot->dev;
	resource_size_t imdio_base;
	void __iomem *imdio_regs;
	struct resource res;
	struct enetc_hw *hw;
	struct mii_bus *bus;
	int port;
	int rc;

	felix->pcs = devm_kcalloc(dev, felix->info->num_ports,
				  sizeof(struct phy_device *),
				  GFP_KERNEL);
	if (!felix->pcs) {
		dev_err(dev, "failed to allocate array for PCS PHYs\n");
		return -ENOMEM;
	}

	imdio_base = pci_resource_start(felix->pdev,
					felix->info->imdio_pci_bar);

	memcpy(&res, felix->info->imdio_res, sizeof(res));
	res.flags = IORESOURCE_MEM;
	res.start += imdio_base;
	res.end += imdio_base;

	imdio_regs = devm_ioremap_resource(dev, &res);
	if (IS_ERR(imdio_regs)) {
		dev_err(dev, "failed to map internal MDIO registers\n");
		return PTR_ERR(imdio_regs);
	}

	hw = enetc_hw_alloc(dev, imdio_regs);
	if (IS_ERR(hw)) {
		dev_err(dev, "failed to allocate ENETC HW structure\n");
		return PTR_ERR(hw);
	}

	bus = devm_mdiobus_alloc_size(dev, sizeof(*mdio_priv));
	if (!bus)
		return -ENOMEM;

	bus->name = "VSC9959 internal MDIO bus";
	bus->read = enetc_mdio_read;
	bus->write = enetc_mdio_write;
	bus->parent = dev;
	mdio_priv = bus->priv;
	mdio_priv->hw = hw;
	/* This gets added to imdio_regs, which already maps addresses
	 * starting with the proper offset.
	 */
	mdio_priv->mdio_base = 0;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-imdio", dev_name(dev));

	/* Needed in order to initialize the bus mutex lock */
	rc = mdiobus_register(bus);
	if (rc < 0) {
		dev_err(dev, "failed to register MDIO bus\n");
		return rc;
	}

	felix->imdio = bus;

	for (port = 0; port < felix->info->num_ports; port++) {
		struct ocelot_port *ocelot_port = ocelot->ports[port];
		struct phy_device *pcs;
		bool is_c45 = false;

		if (ocelot_port->phy_mode == PHY_INTERFACE_MODE_USXGMII)
			is_c45 = true;

		pcs = get_phy_device(felix->imdio, port, is_c45);
		if (IS_ERR(pcs))
			continue;

		pcs->interface = ocelot_port->phy_mode;
		felix->pcs[port] = pcs;

		dev_info(dev, "Found PCS at internal MDIO address %d\n", port);
	}

	return 0;
}

static void vsc9959_mdio_bus_free(struct ocelot *ocelot)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct phy_device *pcs = felix->pcs[port];

		if (!pcs)
			continue;

		put_device(&pcs->mdio.dev);
	}
	mdiobus_unregister(felix->imdio);
}

struct felix_info felix_info_vsc9959 = {
	.target_io_res		= vsc9959_target_io_res,
	.port_io_res		= vsc9959_port_io_res,
	.imdio_res		= &vsc9959_imdio_res,
	.regfields		= vsc9959_regfields,
	.map			= vsc9959_regmap,
	.ops			= &vsc9959_ops,
	.stats_layout		= vsc9959_stats_layout,
	.num_stats		= ARRAY_SIZE(vsc9959_stats_layout),
	.vcap_is2_keys		= vsc9959_vcap_is2_keys,
	.vcap_is2_actions	= vsc9959_vcap_is2_actions,
	.vcap			= vsc9959_vcap_props,
	.shared_queue_sz	= 128 * 1024,
	.num_mact_rows		= 2048,
	.num_ports		= 6,
	.switch_pci_bar		= 4,
	.imdio_pci_bar		= 0,
	.mdio_bus_alloc		= vsc9959_mdio_bus_alloc,
	.mdio_bus_free		= vsc9959_mdio_bus_free,
	.pcs_init		= vsc9959_pcs_init,
	.pcs_an_restart		= vsc9959_pcs_an_restart,
	.pcs_link_state		= vsc9959_pcs_link_state,
	.prevalidate_phy_mode	= vsc9959_prevalidate_phy_mode,
};
