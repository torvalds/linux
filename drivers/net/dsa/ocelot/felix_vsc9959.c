// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Copyright 2017 Microsemi Corporation
 * Copyright 2018-2019 NXP Semiconductors
 */
#include <soc/mscc/ocelot_sys.h>
#include <soc/mscc/ocelot.h>
#include <linux/iopoll.h>
#include <linux/pci.h>
#include "felix.h"

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

/* Addresses are relative to the PCI device's base address and
 * will be fixed up at ioremap time.
 */
static struct resource vsc9959_target_io_res[] = {
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

static struct resource vsc9959_port_io_res[] = {
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

static const struct ocelot_ops vsc9959_ops = {
	.reset			= vsc9959_reset,
};

struct felix_info felix_info_vsc9959 = {
	.target_io_res		= vsc9959_target_io_res,
	.port_io_res		= vsc9959_port_io_res,
	.regfields		= vsc9959_regfields,
	.map			= vsc9959_regmap,
	.ops			= &vsc9959_ops,
	.stats_layout		= vsc9959_stats_layout,
	.num_stats		= ARRAY_SIZE(vsc9959_stats_layout),
	.shared_queue_sz	= 128 * 1024,
	.num_ports		= 6,
	.pci_bar		= 4,
};
