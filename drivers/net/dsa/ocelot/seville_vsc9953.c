// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Distributed Switch Architecture VSC9953 driver
 * Copyright (C) 2020, Maxim Kochetkov <fido_max@inbox.ru>
 */
#include <linux/types.h>
#include <soc/mscc/ocelot_vcap.h>
#include <soc/mscc/ocelot_sys.h>
#include <soc/mscc/ocelot.h>
#include <linux/of_platform.h>
#include <linux/packing.h>
#include <linux/iopoll.h>
#include "felix.h"

#define VSC9953_VCAP_IS2_CNT			1024
#define VSC9953_VCAP_IS2_ENTRY_WIDTH		376
#define VSC9953_VCAP_PORT_CNT			10

#define MSCC_MIIM_REG_STATUS			0x0
#define		MSCC_MIIM_STATUS_STAT_BUSY	BIT(3)
#define MSCC_MIIM_REG_CMD			0x8
#define		MSCC_MIIM_CMD_OPR_WRITE		BIT(1)
#define		MSCC_MIIM_CMD_OPR_READ		BIT(2)
#define		MSCC_MIIM_CMD_WRDATA_SHIFT	4
#define		MSCC_MIIM_CMD_REGAD_SHIFT	20
#define		MSCC_MIIM_CMD_PHYAD_SHIFT	25
#define		MSCC_MIIM_CMD_VLD		BIT(31)
#define MSCC_MIIM_REG_DATA			0xC
#define		MSCC_MIIM_DATA_ERROR		(BIT(16) | BIT(17))

#define MSCC_PHY_REG_PHY_CFG		0x0
#define		PHY_CFG_PHY_ENA		(BIT(0) | BIT(1) | BIT(2) | BIT(3))
#define		PHY_CFG_PHY_COMMON_RESET BIT(4)
#define		PHY_CFG_PHY_RESET	(BIT(5) | BIT(6) | BIT(7) | BIT(8))
#define MSCC_PHY_REG_PHY_STATUS		0x4

static const u32 vsc9953_ana_regmap[] = {
	REG(ANA_ADVLEARN,			0x00b500),
	REG(ANA_VLANMASK,			0x00b504),
	REG_RESERVED(ANA_PORT_B_DOMAIN),
	REG(ANA_ANAGEFIL,			0x00b50c),
	REG(ANA_ANEVENTS,			0x00b510),
	REG(ANA_STORMLIMIT_BURST,		0x00b514),
	REG(ANA_STORMLIMIT_CFG,			0x00b518),
	REG(ANA_ISOLATED_PORTS,			0x00b528),
	REG(ANA_COMMUNITY_PORTS,		0x00b52c),
	REG(ANA_AUTOAGE,			0x00b530),
	REG(ANA_MACTOPTIONS,			0x00b534),
	REG(ANA_LEARNDISC,			0x00b538),
	REG(ANA_AGENCTRL,			0x00b53c),
	REG(ANA_MIRRORPORTS,			0x00b540),
	REG(ANA_EMIRRORPORTS,			0x00b544),
	REG(ANA_FLOODING,			0x00b548),
	REG(ANA_FLOODING_IPMC,			0x00b54c),
	REG(ANA_SFLOW_CFG,			0x00b550),
	REG(ANA_PORT_MODE,			0x00b57c),
	REG_RESERVED(ANA_CUT_THRU_CFG),
	REG(ANA_PGID_PGID,			0x00b600),
	REG(ANA_TABLES_ANMOVED,			0x00b4ac),
	REG(ANA_TABLES_MACHDATA,		0x00b4b0),
	REG(ANA_TABLES_MACLDATA,		0x00b4b4),
	REG_RESERVED(ANA_TABLES_STREAMDATA),
	REG(ANA_TABLES_MACACCESS,		0x00b4b8),
	REG(ANA_TABLES_MACTINDX,		0x00b4bc),
	REG(ANA_TABLES_VLANACCESS,		0x00b4c0),
	REG(ANA_TABLES_VLANTIDX,		0x00b4c4),
	REG_RESERVED(ANA_TABLES_ISDXACCESS),
	REG_RESERVED(ANA_TABLES_ISDXTIDX),
	REG(ANA_TABLES_ENTRYLIM,		0x00b480),
	REG_RESERVED(ANA_TABLES_PTP_ID_HIGH),
	REG_RESERVED(ANA_TABLES_PTP_ID_LOW),
	REG_RESERVED(ANA_TABLES_STREAMACCESS),
	REG_RESERVED(ANA_TABLES_STREAMTIDX),
	REG_RESERVED(ANA_TABLES_SEQ_HISTORY),
	REG_RESERVED(ANA_TABLES_SEQ_MASK),
	REG_RESERVED(ANA_TABLES_SFID_MASK),
	REG_RESERVED(ANA_TABLES_SFIDACCESS),
	REG_RESERVED(ANA_TABLES_SFIDTIDX),
	REG_RESERVED(ANA_MSTI_STATE),
	REG_RESERVED(ANA_OAM_UPM_LM_CNT),
	REG_RESERVED(ANA_SG_ACCESS_CTRL),
	REG_RESERVED(ANA_SG_CONFIG_REG_1),
	REG_RESERVED(ANA_SG_CONFIG_REG_2),
	REG_RESERVED(ANA_SG_CONFIG_REG_3),
	REG_RESERVED(ANA_SG_CONFIG_REG_4),
	REG_RESERVED(ANA_SG_CONFIG_REG_5),
	REG_RESERVED(ANA_SG_GCL_GS_CONFIG),
	REG_RESERVED(ANA_SG_GCL_TI_CONFIG),
	REG_RESERVED(ANA_SG_STATUS_REG_1),
	REG_RESERVED(ANA_SG_STATUS_REG_2),
	REG_RESERVED(ANA_SG_STATUS_REG_3),
	REG(ANA_PORT_VLAN_CFG,			0x000000),
	REG(ANA_PORT_DROP_CFG,			0x000004),
	REG(ANA_PORT_QOS_CFG,			0x000008),
	REG(ANA_PORT_VCAP_CFG,			0x00000c),
	REG(ANA_PORT_VCAP_S1_KEY_CFG,		0x000010),
	REG(ANA_PORT_VCAP_S2_CFG,		0x00001c),
	REG(ANA_PORT_PCP_DEI_MAP,		0x000020),
	REG(ANA_PORT_CPU_FWD_CFG,		0x000060),
	REG(ANA_PORT_CPU_FWD_BPDU_CFG,		0x000064),
	REG(ANA_PORT_CPU_FWD_GARP_CFG,		0x000068),
	REG(ANA_PORT_CPU_FWD_CCM_CFG,		0x00006c),
	REG(ANA_PORT_PORT_CFG,			0x000070),
	REG(ANA_PORT_POL_CFG,			0x000074),
	REG_RESERVED(ANA_PORT_PTP_CFG),
	REG_RESERVED(ANA_PORT_PTP_DLY1_CFG),
	REG_RESERVED(ANA_PORT_PTP_DLY2_CFG),
	REG_RESERVED(ANA_PORT_SFID_CFG),
	REG(ANA_PFC_PFC_CFG,			0x00c000),
	REG_RESERVED(ANA_PFC_PFC_TIMER),
	REG_RESERVED(ANA_IPT_OAM_MEP_CFG),
	REG_RESERVED(ANA_IPT_IPT),
	REG_RESERVED(ANA_PPT_PPT),
	REG_RESERVED(ANA_FID_MAP_FID_MAP),
	REG(ANA_AGGR_CFG,			0x00c600),
	REG(ANA_CPUQ_CFG,			0x00c604),
	REG_RESERVED(ANA_CPUQ_CFG2),
	REG(ANA_CPUQ_8021_CFG,			0x00c60c),
	REG(ANA_DSCP_CFG,			0x00c64c),
	REG(ANA_DSCP_REWR_CFG,			0x00c74c),
	REG(ANA_VCAP_RNG_TYPE_CFG,		0x00c78c),
	REG(ANA_VCAP_RNG_VAL_CFG,		0x00c7ac),
	REG_RESERVED(ANA_VRAP_CFG),
	REG_RESERVED(ANA_VRAP_HDR_DATA),
	REG_RESERVED(ANA_VRAP_HDR_MASK),
	REG(ANA_DISCARD_CFG,			0x00c7d8),
	REG(ANA_FID_CFG,			0x00c7dc),
	REG(ANA_POL_PIR_CFG,			0x00a000),
	REG(ANA_POL_CIR_CFG,			0x00a004),
	REG(ANA_POL_MODE_CFG,			0x00a008),
	REG(ANA_POL_PIR_STATE,			0x00a00c),
	REG(ANA_POL_CIR_STATE,			0x00a010),
	REG_RESERVED(ANA_POL_STATE),
	REG(ANA_POL_FLOWC,			0x00c280),
	REG(ANA_POL_HYST,			0x00c2ec),
	REG_RESERVED(ANA_POL_MISC_CFG),
};

static const u32 vsc9953_qs_regmap[] = {
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

static const u32 vsc9953_s2_regmap[] = {
	REG(S2_CORE_UPDATE_CTRL,		0x000000),
	REG(S2_CORE_MV_CFG,			0x000004),
	REG(S2_CACHE_ENTRY_DAT,			0x000008),
	REG(S2_CACHE_MASK_DAT,			0x000108),
	REG(S2_CACHE_ACTION_DAT,		0x000208),
	REG(S2_CACHE_CNT_DAT,			0x000308),
	REG(S2_CACHE_TG_DAT,			0x000388),
};

static const u32 vsc9953_qsys_regmap[] = {
	REG(QSYS_PORT_MODE,			0x003600),
	REG(QSYS_SWITCH_PORT_MODE,		0x003630),
	REG(QSYS_STAT_CNT_CFG,			0x00365c),
	REG(QSYS_EEE_CFG,			0x003660),
	REG(QSYS_EEE_THRES,			0x003688),
	REG(QSYS_IGR_NO_SHARING,		0x00368c),
	REG(QSYS_EGR_NO_SHARING,		0x003690),
	REG(QSYS_SW_STATUS,			0x003694),
	REG(QSYS_EXT_CPU_CFG,			0x0036c0),
	REG_RESERVED(QSYS_PAD_CFG),
	REG(QSYS_CPU_GROUP_MAP,			0x0036c8),
	REG_RESERVED(QSYS_QMAP),
	REG_RESERVED(QSYS_ISDX_SGRP),
	REG_RESERVED(QSYS_TIMED_FRAME_ENTRY),
	REG_RESERVED(QSYS_TFRM_MISC),
	REG_RESERVED(QSYS_TFRM_PORT_DLY),
	REG_RESERVED(QSYS_TFRM_TIMER_CFG_1),
	REG_RESERVED(QSYS_TFRM_TIMER_CFG_2),
	REG_RESERVED(QSYS_TFRM_TIMER_CFG_3),
	REG_RESERVED(QSYS_TFRM_TIMER_CFG_4),
	REG_RESERVED(QSYS_TFRM_TIMER_CFG_5),
	REG_RESERVED(QSYS_TFRM_TIMER_CFG_6),
	REG_RESERVED(QSYS_TFRM_TIMER_CFG_7),
	REG_RESERVED(QSYS_TFRM_TIMER_CFG_8),
	REG(QSYS_RED_PROFILE,			0x003724),
	REG(QSYS_RES_QOS_MODE,			0x003764),
	REG(QSYS_RES_CFG,			0x004000),
	REG(QSYS_RES_STAT,			0x004004),
	REG(QSYS_EGR_DROP_MODE,			0x003768),
	REG(QSYS_EQ_CTRL,			0x00376c),
	REG_RESERVED(QSYS_EVENTS_CORE),
	REG_RESERVED(QSYS_QMAXSDU_CFG_0),
	REG_RESERVED(QSYS_QMAXSDU_CFG_1),
	REG_RESERVED(QSYS_QMAXSDU_CFG_2),
	REG_RESERVED(QSYS_QMAXSDU_CFG_3),
	REG_RESERVED(QSYS_QMAXSDU_CFG_4),
	REG_RESERVED(QSYS_QMAXSDU_CFG_5),
	REG_RESERVED(QSYS_QMAXSDU_CFG_6),
	REG_RESERVED(QSYS_QMAXSDU_CFG_7),
	REG_RESERVED(QSYS_PREEMPTION_CFG),
	REG(QSYS_CIR_CFG,			0x000000),
	REG_RESERVED(QSYS_EIR_CFG),
	REG(QSYS_SE_CFG,			0x000008),
	REG(QSYS_SE_DWRR_CFG,			0x00000c),
	REG_RESERVED(QSYS_SE_CONNECT),
	REG_RESERVED(QSYS_SE_DLB_SENSE),
	REG(QSYS_CIR_STATE,			0x000044),
	REG_RESERVED(QSYS_EIR_STATE),
	REG_RESERVED(QSYS_SE_STATE),
	REG(QSYS_HSCH_MISC_CFG,			0x003774),
	REG_RESERVED(QSYS_TAG_CONFIG),
	REG_RESERVED(QSYS_TAS_PARAM_CFG_CTRL),
	REG_RESERVED(QSYS_PORT_MAX_SDU),
	REG_RESERVED(QSYS_PARAM_CFG_REG_1),
	REG_RESERVED(QSYS_PARAM_CFG_REG_2),
	REG_RESERVED(QSYS_PARAM_CFG_REG_3),
	REG_RESERVED(QSYS_PARAM_CFG_REG_4),
	REG_RESERVED(QSYS_PARAM_CFG_REG_5),
	REG_RESERVED(QSYS_GCL_CFG_REG_1),
	REG_RESERVED(QSYS_GCL_CFG_REG_2),
	REG_RESERVED(QSYS_PARAM_STATUS_REG_1),
	REG_RESERVED(QSYS_PARAM_STATUS_REG_2),
	REG_RESERVED(QSYS_PARAM_STATUS_REG_3),
	REG_RESERVED(QSYS_PARAM_STATUS_REG_4),
	REG_RESERVED(QSYS_PARAM_STATUS_REG_5),
	REG_RESERVED(QSYS_PARAM_STATUS_REG_6),
	REG_RESERVED(QSYS_PARAM_STATUS_REG_7),
	REG_RESERVED(QSYS_PARAM_STATUS_REG_8),
	REG_RESERVED(QSYS_PARAM_STATUS_REG_9),
	REG_RESERVED(QSYS_GCL_STATUS_REG_1),
	REG_RESERVED(QSYS_GCL_STATUS_REG_2),
};

static const u32 vsc9953_rew_regmap[] = {
	REG(REW_PORT_VLAN_CFG,			0x000000),
	REG(REW_TAG_CFG,			0x000004),
	REG(REW_PORT_CFG,			0x000008),
	REG(REW_DSCP_CFG,			0x00000c),
	REG(REW_PCP_DEI_QOS_MAP_CFG,		0x000010),
	REG_RESERVED(REW_PTP_CFG),
	REG_RESERVED(REW_PTP_DLY1_CFG),
	REG_RESERVED(REW_RED_TAG_CFG),
	REG(REW_DSCP_REMAP_DP1_CFG,		0x000610),
	REG(REW_DSCP_REMAP_CFG,			0x000710),
	REG_RESERVED(REW_STAT_CFG),
	REG_RESERVED(REW_REW_STICKY),
	REG_RESERVED(REW_PPT),
};

static const u32 vsc9953_sys_regmap[] = {
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
	REG(SYS_COUNT_RX_LONGS,			0x000048),
	REG(SYS_COUNT_TX_OCTETS,		0x000100),
	REG(SYS_COUNT_TX_COLLISION,		0x000110),
	REG(SYS_COUNT_TX_DROPS,			0x000114),
	REG(SYS_COUNT_TX_64,			0x00011c),
	REG(SYS_COUNT_TX_65_127,		0x000120),
	REG(SYS_COUNT_TX_128_511,		0x000124),
	REG(SYS_COUNT_TX_512_1023,		0x000128),
	REG(SYS_COUNT_TX_1024_1526,		0x00012c),
	REG(SYS_COUNT_TX_1527_MAX,		0x000130),
	REG(SYS_COUNT_TX_AGING,			0x000178),
	REG(SYS_RESET_CFG,			0x000318),
	REG_RESERVED(SYS_SR_ETYPE_CFG),
	REG(SYS_VLAN_ETYPE_CFG,			0x000320),
	REG(SYS_PORT_MODE,			0x000324),
	REG(SYS_FRONT_PORT_MODE,		0x000354),
	REG(SYS_FRM_AGING,			0x00037c),
	REG(SYS_STAT_CFG,			0x000380),
	REG_RESERVED(SYS_SW_STATUS),
	REG_RESERVED(SYS_MISC_CFG),
	REG_RESERVED(SYS_REW_MAC_HIGH_CFG),
	REG_RESERVED(SYS_REW_MAC_LOW_CFG),
	REG_RESERVED(SYS_TIMESTAMP_OFFSET),
	REG(SYS_PAUSE_CFG,			0x00044c),
	REG(SYS_PAUSE_TOT_CFG,			0x000478),
	REG(SYS_ATOP,				0x00047c),
	REG(SYS_ATOP_TOT_CFG,			0x0004a8),
	REG(SYS_MAC_FC_CFG,			0x0004ac),
	REG(SYS_MMGT,				0x0004d4),
	REG_RESERVED(SYS_MMGT_FAST),
	REG_RESERVED(SYS_EVENTS_DIF),
	REG_RESERVED(SYS_EVENTS_CORE),
	REG_RESERVED(SYS_CNT),
	REG_RESERVED(SYS_PTP_STATUS),
	REG_RESERVED(SYS_PTP_TXSTAMP),
	REG_RESERVED(SYS_PTP_NXT),
	REG_RESERVED(SYS_PTP_CFG),
	REG_RESERVED(SYS_RAM_INIT),
	REG_RESERVED(SYS_CM_ADDR),
	REG_RESERVED(SYS_CM_DATA_WR),
	REG_RESERVED(SYS_CM_DATA_RD),
	REG_RESERVED(SYS_CM_OP),
	REG_RESERVED(SYS_CM_DATA),
};

static const u32 vsc9953_gcb_regmap[] = {
	REG(GCB_SOFT_RST,			0x000008),
	REG(GCB_MIIM_MII_STATUS,		0x0000ac),
	REG(GCB_MIIM_MII_CMD,			0x0000b4),
	REG(GCB_MIIM_MII_DATA,			0x0000b8),
};

static const u32 vsc9953_dev_gmii_regmap[] = {
	REG(DEV_CLOCK_CFG,			0x0),
	REG(DEV_PORT_MISC,			0x4),
	REG_RESERVED(DEV_EVENTS),
	REG(DEV_EEE_CFG,			0xc),
	REG_RESERVED(DEV_RX_PATH_DELAY),
	REG_RESERVED(DEV_TX_PATH_DELAY),
	REG_RESERVED(DEV_PTP_PREDICT_CFG),
	REG(DEV_MAC_ENA_CFG,			0x10),
	REG(DEV_MAC_MODE_CFG,			0x14),
	REG(DEV_MAC_MAXLEN_CFG,			0x18),
	REG(DEV_MAC_TAGS_CFG,			0x1c),
	REG(DEV_MAC_ADV_CHK_CFG,		0x20),
	REG(DEV_MAC_IFG_CFG,			0x24),
	REG(DEV_MAC_HDX_CFG,			0x28),
	REG_RESERVED(DEV_MAC_DBG_CFG),
	REG(DEV_MAC_FC_MAC_LOW_CFG,		0x30),
	REG(DEV_MAC_FC_MAC_HIGH_CFG,		0x34),
	REG(DEV_MAC_STICKY,			0x38),
	REG_RESERVED(PCS1G_CFG),
	REG_RESERVED(PCS1G_MODE_CFG),
	REG_RESERVED(PCS1G_SD_CFG),
	REG_RESERVED(PCS1G_ANEG_CFG),
	REG_RESERVED(PCS1G_ANEG_NP_CFG),
	REG_RESERVED(PCS1G_LB_CFG),
	REG_RESERVED(PCS1G_DBG_CFG),
	REG_RESERVED(PCS1G_CDET_CFG),
	REG_RESERVED(PCS1G_ANEG_STATUS),
	REG_RESERVED(PCS1G_ANEG_NP_STATUS),
	REG_RESERVED(PCS1G_LINK_STATUS),
	REG_RESERVED(PCS1G_LINK_DOWN_CNT),
	REG_RESERVED(PCS1G_STICKY),
	REG_RESERVED(PCS1G_DEBUG_STATUS),
	REG_RESERVED(PCS1G_LPI_CFG),
	REG_RESERVED(PCS1G_LPI_WAKE_ERROR_CNT),
	REG_RESERVED(PCS1G_LPI_STATUS),
	REG_RESERVED(PCS1G_TSTPAT_MODE_CFG),
	REG_RESERVED(PCS1G_TSTPAT_STATUS),
	REG_RESERVED(DEV_PCS_FX100_CFG),
	REG_RESERVED(DEV_PCS_FX100_STATUS),
};

static const u32 *vsc9953_regmap[TARGET_MAX] = {
	[ANA]		= vsc9953_ana_regmap,
	[QS]		= vsc9953_qs_regmap,
	[QSYS]		= vsc9953_qsys_regmap,
	[REW]		= vsc9953_rew_regmap,
	[SYS]		= vsc9953_sys_regmap,
	[S2]		= vsc9953_s2_regmap,
	[GCB]		= vsc9953_gcb_regmap,
	[DEV_GMII]	= vsc9953_dev_gmii_regmap,
};

/* Addresses are relative to the device's base address */
static const struct resource vsc9953_target_io_res[TARGET_MAX] = {
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

static const struct resource vsc9953_port_io_res[] = {
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
	{
		.start	= 0x0160000,
		.end	= 0x016ffff,
		.name	= "port6",
	},
	{
		.start	= 0x0170000,
		.end	= 0x017ffff,
		.name	= "port7",
	},
	{
		.start	= 0x0180000,
		.end	= 0x018ffff,
		.name	= "port8",
	},
	{
		.start	= 0x0190000,
		.end	= 0x019ffff,
		.name	= "port9",
	},
};

static const struct reg_field vsc9953_regfields[REGFIELD_MAX] = {
	[ANA_ADVLEARN_VLAN_CHK] = REG_FIELD(ANA_ADVLEARN, 10, 10),
	[ANA_ADVLEARN_LEARN_MIRROR] = REG_FIELD(ANA_ADVLEARN, 0, 9),
	[ANA_ANEVENTS_AUTOAGE] = REG_FIELD(ANA_ANEVENTS, 24, 24),
	[ANA_ANEVENTS_STORM_DROP] = REG_FIELD(ANA_ANEVENTS, 22, 22),
	[ANA_ANEVENTS_LEARN_DROP] = REG_FIELD(ANA_ANEVENTS, 21, 21),
	[ANA_ANEVENTS_AGED_ENTRY] = REG_FIELD(ANA_ANEVENTS, 20, 20),
	[ANA_ANEVENTS_CPU_LEARN_FAILED] = REG_FIELD(ANA_ANEVENTS, 19, 19),
	[ANA_ANEVENTS_AUTO_LEARN_FAILED] = REG_FIELD(ANA_ANEVENTS, 18, 18),
	[ANA_ANEVENTS_LEARN_REMOVE] = REG_FIELD(ANA_ANEVENTS, 17, 17),
	[ANA_ANEVENTS_AUTO_LEARNED] = REG_FIELD(ANA_ANEVENTS, 16, 16),
	[ANA_ANEVENTS_AUTO_MOVED] = REG_FIELD(ANA_ANEVENTS, 15, 15),
	[ANA_ANEVENTS_CLASSIFIED_DROP] = REG_FIELD(ANA_ANEVENTS, 13, 13),
	[ANA_ANEVENTS_CLASSIFIED_COPY] = REG_FIELD(ANA_ANEVENTS, 12, 12),
	[ANA_ANEVENTS_VLAN_DISCARD] = REG_FIELD(ANA_ANEVENTS, 11, 11),
	[ANA_ANEVENTS_FWD_DISCARD] = REG_FIELD(ANA_ANEVENTS, 10, 10),
	[ANA_ANEVENTS_MULTICAST_FLOOD] = REG_FIELD(ANA_ANEVENTS, 9, 9),
	[ANA_ANEVENTS_UNICAST_FLOOD] = REG_FIELD(ANA_ANEVENTS, 8, 8),
	[ANA_ANEVENTS_DEST_KNOWN] = REG_FIELD(ANA_ANEVENTS, 7, 7),
	[ANA_ANEVENTS_BUCKET3_MATCH] = REG_FIELD(ANA_ANEVENTS, 6, 6),
	[ANA_ANEVENTS_BUCKET2_MATCH] = REG_FIELD(ANA_ANEVENTS, 5, 5),
	[ANA_ANEVENTS_BUCKET1_MATCH] = REG_FIELD(ANA_ANEVENTS, 4, 4),
	[ANA_ANEVENTS_BUCKET0_MATCH] = REG_FIELD(ANA_ANEVENTS, 3, 3),
	[ANA_ANEVENTS_CPU_OPERATION] = REG_FIELD(ANA_ANEVENTS, 2, 2),
	[ANA_ANEVENTS_DMAC_LOOKUP] = REG_FIELD(ANA_ANEVENTS, 1, 1),
	[ANA_ANEVENTS_SMAC_LOOKUP] = REG_FIELD(ANA_ANEVENTS, 0, 0),
	[ANA_TABLES_MACACCESS_B_DOM] = REG_FIELD(ANA_TABLES_MACACCESS, 16, 16),
	[ANA_TABLES_MACTINDX_BUCKET] = REG_FIELD(ANA_TABLES_MACTINDX, 11, 12),
	[ANA_TABLES_MACTINDX_M_INDEX] = REG_FIELD(ANA_TABLES_MACTINDX, 0, 10),
	[SYS_RESET_CFG_CORE_ENA] = REG_FIELD(SYS_RESET_CFG, 7, 7),
	[SYS_RESET_CFG_MEM_ENA] = REG_FIELD(SYS_RESET_CFG, 6, 6),
	[SYS_RESET_CFG_MEM_INIT] = REG_FIELD(SYS_RESET_CFG, 5, 5),
	[GCB_SOFT_RST_SWC_RST] = REG_FIELD(GCB_SOFT_RST, 0, 0),
	[GCB_MIIM_MII_STATUS_PENDING] = REG_FIELD(GCB_MIIM_MII_STATUS, 2, 2),
	[GCB_MIIM_MII_STATUS_BUSY] = REG_FIELD(GCB_MIIM_MII_STATUS, 3, 3),
	/* Replicated per number of ports (11), register size 4 per port */
	[QSYS_SWITCH_PORT_MODE_PORT_ENA] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 13, 13, 11, 4),
	[QSYS_SWITCH_PORT_MODE_YEL_RSRVD] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 10, 10, 11, 4),
	[QSYS_SWITCH_PORT_MODE_INGRESS_DROP_MODE] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 9, 9, 11, 4),
	[QSYS_SWITCH_PORT_MODE_TX_PFC_ENA] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 1, 8, 11, 4),
	[QSYS_SWITCH_PORT_MODE_TX_PFC_MODE] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 0, 0, 11, 4),
	[SYS_PORT_MODE_INCL_INJ_HDR] = REG_FIELD_ID(SYS_PORT_MODE, 4, 5, 11, 4),
	[SYS_PORT_MODE_INCL_XTR_HDR] = REG_FIELD_ID(SYS_PORT_MODE, 2, 3, 11, 4),
	[SYS_PORT_MODE_INCL_HDR_ERR] = REG_FIELD_ID(SYS_PORT_MODE, 0, 0, 11, 4),
	[SYS_PAUSE_CFG_PAUSE_START] = REG_FIELD_ID(SYS_PAUSE_CFG, 11, 20, 11, 4),
	[SYS_PAUSE_CFG_PAUSE_STOP] = REG_FIELD_ID(SYS_PAUSE_CFG, 1, 10, 11, 4),
	[SYS_PAUSE_CFG_PAUSE_ENA] = REG_FIELD_ID(SYS_PAUSE_CFG, 0, 1, 11, 4),
};

static const struct ocelot_stat_layout vsc9953_stats_layout[] = {
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
	{ .offset = 0x40,	.name = "tx_octets", },
	{ .offset = 0x41,	.name = "tx_unicast", },
	{ .offset = 0x42,	.name = "tx_multicast", },
	{ .offset = 0x43,	.name = "tx_broadcast", },
	{ .offset = 0x44,	.name = "tx_collision", },
	{ .offset = 0x45,	.name = "tx_drops", },
	{ .offset = 0x46,	.name = "tx_pause", },
	{ .offset = 0x47,	.name = "tx_frames_below_65_octets", },
	{ .offset = 0x48,	.name = "tx_frames_65_to_127_octets", },
	{ .offset = 0x49,	.name = "tx_frames_128_255_octets", },
	{ .offset = 0x4A,	.name = "tx_frames_256_511_octets", },
	{ .offset = 0x4B,	.name = "tx_frames_512_1023_octets", },
	{ .offset = 0x4C,	.name = "tx_frames_1024_1526_octets", },
	{ .offset = 0x4D,	.name = "tx_frames_over_1526_octets", },
	{ .offset = 0x4E,	.name = "tx_yellow_prio_0", },
	{ .offset = 0x4F,	.name = "tx_yellow_prio_1", },
	{ .offset = 0x50,	.name = "tx_yellow_prio_2", },
	{ .offset = 0x51,	.name = "tx_yellow_prio_3", },
	{ .offset = 0x52,	.name = "tx_yellow_prio_4", },
	{ .offset = 0x53,	.name = "tx_yellow_prio_5", },
	{ .offset = 0x54,	.name = "tx_yellow_prio_6", },
	{ .offset = 0x55,	.name = "tx_yellow_prio_7", },
	{ .offset = 0x56,	.name = "tx_green_prio_0", },
	{ .offset = 0x57,	.name = "tx_green_prio_1", },
	{ .offset = 0x58,	.name = "tx_green_prio_2", },
	{ .offset = 0x59,	.name = "tx_green_prio_3", },
	{ .offset = 0x5A,	.name = "tx_green_prio_4", },
	{ .offset = 0x5B,	.name = "tx_green_prio_5", },
	{ .offset = 0x5C,	.name = "tx_green_prio_6", },
	{ .offset = 0x5D,	.name = "tx_green_prio_7", },
	{ .offset = 0x5E,	.name = "tx_aged", },
	{ .offset = 0x80,	.name = "drop_local", },
	{ .offset = 0x81,	.name = "drop_tail", },
	{ .offset = 0x82,	.name = "drop_yellow_prio_0", },
	{ .offset = 0x83,	.name = "drop_yellow_prio_1", },
	{ .offset = 0x84,	.name = "drop_yellow_prio_2", },
	{ .offset = 0x85,	.name = "drop_yellow_prio_3", },
	{ .offset = 0x86,	.name = "drop_yellow_prio_4", },
	{ .offset = 0x87,	.name = "drop_yellow_prio_5", },
	{ .offset = 0x88,	.name = "drop_yellow_prio_6", },
	{ .offset = 0x89,	.name = "drop_yellow_prio_7", },
	{ .offset = 0x8A,	.name = "drop_green_prio_0", },
	{ .offset = 0x8B,	.name = "drop_green_prio_1", },
	{ .offset = 0x8C,	.name = "drop_green_prio_2", },
	{ .offset = 0x8D,	.name = "drop_green_prio_3", },
	{ .offset = 0x8E,	.name = "drop_green_prio_4", },
	{ .offset = 0x8F,	.name = "drop_green_prio_5", },
	{ .offset = 0x90,	.name = "drop_green_prio_6", },
	{ .offset = 0x91,	.name = "drop_green_prio_7", },
};

static struct vcap_field vsc9953_vcap_is2_keys[] = {
	/* Common: 41 bits */
	[VCAP_IS2_TYPE]				= {  0,   4},
	[VCAP_IS2_HK_FIRST]			= {  4,   1},
	[VCAP_IS2_HK_PAG]			= {  5,   8},
	[VCAP_IS2_HK_IGR_PORT_MASK]		= { 13,  11},
	[VCAP_IS2_HK_RSV2]			= { 24,   1},
	[VCAP_IS2_HK_HOST_MATCH]		= { 25,   1},
	[VCAP_IS2_HK_L2_MC]			= { 26,   1},
	[VCAP_IS2_HK_L2_BC]			= { 27,   1},
	[VCAP_IS2_HK_VLAN_TAGGED]		= { 28,   1},
	[VCAP_IS2_HK_VID]			= { 29,  12},
	[VCAP_IS2_HK_DEI]			= { 41,   1},
	[VCAP_IS2_HK_PCP]			= { 42,   3},
	/* MAC_ETYPE / MAC_LLC / MAC_SNAP / OAM common */
	[VCAP_IS2_HK_L2_DMAC]			= { 45,  48},
	[VCAP_IS2_HK_L2_SMAC]			= { 93,  48},
	/* MAC_ETYPE (TYPE=000) */
	[VCAP_IS2_HK_MAC_ETYPE_ETYPE]		= {141,  16},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD0]	= {157,  16},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD1]	= {173,   8},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD2]	= {181,   3},
	/* MAC_LLC (TYPE=001) */
	[VCAP_IS2_HK_MAC_LLC_L2_LLC]		= {141,  40},
	/* MAC_SNAP (TYPE=010) */
	[VCAP_IS2_HK_MAC_SNAP_L2_SNAP]		= {141,  40},
	/* MAC_ARP (TYPE=011) */
	[VCAP_IS2_HK_MAC_ARP_SMAC]		= { 45,  48},
	[VCAP_IS2_HK_MAC_ARP_ADDR_SPACE_OK]	= { 93,   1},
	[VCAP_IS2_HK_MAC_ARP_PROTO_SPACE_OK]	= { 94,   1},
	[VCAP_IS2_HK_MAC_ARP_LEN_OK]		= { 95,   1},
	[VCAP_IS2_HK_MAC_ARP_TARGET_MATCH]	= { 96,   1},
	[VCAP_IS2_HK_MAC_ARP_SENDER_MATCH]	= { 97,   1},
	[VCAP_IS2_HK_MAC_ARP_OPCODE_UNKNOWN]	= { 98,   1},
	[VCAP_IS2_HK_MAC_ARP_OPCODE]		= { 99,   2},
	[VCAP_IS2_HK_MAC_ARP_L3_IP4_DIP]	= {101,  32},
	[VCAP_IS2_HK_MAC_ARP_L3_IP4_SIP]	= {133,  32},
	[VCAP_IS2_HK_MAC_ARP_DIP_EQ_SIP]	= {165,   1},
	/* IP4_TCP_UDP / IP4_OTHER common */
	[VCAP_IS2_HK_IP4]			= { 45,   1},
	[VCAP_IS2_HK_L3_FRAGMENT]		= { 46,   1},
	[VCAP_IS2_HK_L3_FRAG_OFS_GT0]		= { 47,   1},
	[VCAP_IS2_HK_L3_OPTIONS]		= { 48,   1},
	[VCAP_IS2_HK_IP4_L3_TTL_GT0]		= { 49,   1},
	[VCAP_IS2_HK_L3_TOS]			= { 50,   8},
	[VCAP_IS2_HK_L3_IP4_DIP]		= { 58,  32},
	[VCAP_IS2_HK_L3_IP4_SIP]		= { 90,  32},
	[VCAP_IS2_HK_DIP_EQ_SIP]		= {122,   1},
	/* IP4_TCP_UDP (TYPE=100) */
	[VCAP_IS2_HK_TCP]			= {123,   1},
	[VCAP_IS2_HK_L4_DPORT]			= {124,  16},
	[VCAP_IS2_HK_L4_SPORT]			= {140,  16},
	[VCAP_IS2_HK_L4_RNG]			= {156,   8},
	[VCAP_IS2_HK_L4_SPORT_EQ_DPORT]		= {164,   1},
	[VCAP_IS2_HK_L4_SEQUENCE_EQ0]		= {165,   1},
	[VCAP_IS2_HK_L4_FIN]			= {166,   1},
	[VCAP_IS2_HK_L4_SYN]			= {167,   1},
	[VCAP_IS2_HK_L4_RST]			= {168,   1},
	[VCAP_IS2_HK_L4_PSH]			= {169,   1},
	[VCAP_IS2_HK_L4_ACK]			= {170,   1},
	[VCAP_IS2_HK_L4_URG]			= {171,   1},
	/* IP4_OTHER (TYPE=101) */
	[VCAP_IS2_HK_IP4_L3_PROTO]		= {123,   8},
	[VCAP_IS2_HK_L3_PAYLOAD]		= {131,  56},
	/* IP6_STD (TYPE=110) */
	[VCAP_IS2_HK_IP6_L3_TTL_GT0]		= { 45,   1},
	[VCAP_IS2_HK_L3_IP6_SIP]		= { 46, 128},
	[VCAP_IS2_HK_IP6_L3_PROTO]		= {174,   8},
};

static struct vcap_field vsc9953_vcap_is2_actions[] = {
	[VCAP_IS2_ACT_HIT_ME_ONCE]		= {  0,  1},
	[VCAP_IS2_ACT_CPU_COPY_ENA]		= {  1,  1},
	[VCAP_IS2_ACT_CPU_QU_NUM]		= {  2,  3},
	[VCAP_IS2_ACT_MASK_MODE]		= {  5,  2},
	[VCAP_IS2_ACT_MIRROR_ENA]		= {  7,  1},
	[VCAP_IS2_ACT_LRN_DIS]			= {  8,  1},
	[VCAP_IS2_ACT_POLICE_ENA]		= {  9,  1},
	[VCAP_IS2_ACT_POLICE_IDX]		= { 10,  8},
	[VCAP_IS2_ACT_POLICE_VCAP_ONLY]		= { 21,  1},
	[VCAP_IS2_ACT_PORT_MASK]		= { 22, 10},
	[VCAP_IS2_ACT_ACL_ID]			= { 44,  6},
	[VCAP_IS2_ACT_HIT_CNT]			= { 50, 32},
};

static const struct vcap_props vsc9953_vcap_props[] = {
	[VCAP_IS2] = {
		.tg_width = 2,
		.sw_count = 4,
		.entry_count = VSC9953_VCAP_IS2_CNT,
		.entry_width = VSC9953_VCAP_IS2_ENTRY_WIDTH,
		.action_count = VSC9953_VCAP_IS2_CNT +
				VSC9953_VCAP_PORT_CNT + 2,
		.action_width = 101,
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

#define VSC9953_INIT_TIMEOUT			50000
#define VSC9953_GCB_RST_SLEEP			100
#define VSC9953_SYS_RAMINIT_SLEEP		80
#define VCS9953_MII_TIMEOUT			10000

static int vsc9953_gcb_soft_rst_status(struct ocelot *ocelot)
{
	int val;

	ocelot_field_read(ocelot, GCB_SOFT_RST_SWC_RST, &val);

	return val;
}

static int vsc9953_sys_ram_init_status(struct ocelot *ocelot)
{
	int val;

	ocelot_field_read(ocelot, SYS_RESET_CFG_MEM_INIT, &val);

	return val;
}

static int vsc9953_gcb_miim_pending_status(struct ocelot *ocelot)
{
	int val;

	ocelot_field_read(ocelot, GCB_MIIM_MII_STATUS_PENDING, &val);

	return val;
}

static int vsc9953_gcb_miim_busy_status(struct ocelot *ocelot)
{
	int val;

	ocelot_field_read(ocelot, GCB_MIIM_MII_STATUS_BUSY, &val);

	return val;
}

static int vsc9953_mdio_write(struct mii_bus *bus, int phy_id, int regnum,
			      u16 value)
{
	struct ocelot *ocelot = bus->priv;
	int err, cmd, val;

	/* Wait while MIIM controller becomes idle */
	err = readx_poll_timeout(vsc9953_gcb_miim_pending_status, ocelot,
				 val, !val, 10, VCS9953_MII_TIMEOUT);
	if (err) {
		dev_err(ocelot->dev, "MDIO write: pending timeout\n");
		goto out;
	}

	cmd = MSCC_MIIM_CMD_VLD | (phy_id << MSCC_MIIM_CMD_PHYAD_SHIFT) |
	      (regnum << MSCC_MIIM_CMD_REGAD_SHIFT) |
	      (value << MSCC_MIIM_CMD_WRDATA_SHIFT) |
	      MSCC_MIIM_CMD_OPR_WRITE;

	ocelot_write(ocelot, cmd, GCB_MIIM_MII_CMD);

out:
	return err;
}

static int vsc9953_mdio_read(struct mii_bus *bus, int phy_id, int regnum)
{
	struct ocelot *ocelot = bus->priv;
	int err, cmd, val;

	/* Wait until MIIM controller becomes idle */
	err = readx_poll_timeout(vsc9953_gcb_miim_pending_status, ocelot,
				 val, !val, 10, VCS9953_MII_TIMEOUT);
	if (err) {
		dev_err(ocelot->dev, "MDIO read: pending timeout\n");
		goto out;
	}

	/* Write the MIIM COMMAND register */
	cmd = MSCC_MIIM_CMD_VLD | (phy_id << MSCC_MIIM_CMD_PHYAD_SHIFT) |
	      (regnum << MSCC_MIIM_CMD_REGAD_SHIFT) | MSCC_MIIM_CMD_OPR_READ;

	ocelot_write(ocelot, cmd, GCB_MIIM_MII_CMD);

	/* Wait while read operation via the MIIM controller is in progress */
	err = readx_poll_timeout(vsc9953_gcb_miim_busy_status, ocelot,
				 val, !val, 10, VCS9953_MII_TIMEOUT);
	if (err) {
		dev_err(ocelot->dev, "MDIO read: busy timeout\n");
		goto out;
	}

	val = ocelot_read(ocelot, GCB_MIIM_MII_DATA);

	err = val & 0xFFFF;
out:
	return err;
}

static int vsc9953_reset(struct ocelot *ocelot)
{
	int val, err;

	/* soft-reset the switch core */
	ocelot_field_write(ocelot, GCB_SOFT_RST_SWC_RST, 1);

	err = readx_poll_timeout(vsc9953_gcb_soft_rst_status, ocelot, val, !val,
				 VSC9953_GCB_RST_SLEEP, VSC9953_INIT_TIMEOUT);
	if (err) {
		dev_err(ocelot->dev, "timeout: switch core reset\n");
		return err;
	}

	/* initialize switch mem ~40us */
	ocelot_field_write(ocelot, SYS_RESET_CFG_MEM_INIT, 1);
	ocelot_field_write(ocelot, SYS_RESET_CFG_MEM_ENA, 1);

	err = readx_poll_timeout(vsc9953_sys_ram_init_status, ocelot, val, !val,
				 VSC9953_SYS_RAMINIT_SLEEP,
				 VSC9953_INIT_TIMEOUT);
	if (err) {
		dev_err(ocelot->dev, "timeout: switch sram init\n");
		return err;
	}

	/* enable switch core */
	ocelot_field_write(ocelot, SYS_RESET_CFG_MEM_ENA, 1);
	ocelot_field_write(ocelot, SYS_RESET_CFG_CORE_ENA, 1);

	return 0;
}

static void vsc9953_phylink_validate(struct ocelot *ocelot, int port,
				     unsigned long *supported,
				     struct phylink_link_state *state)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };

	if (state->interface != PHY_INTERFACE_MODE_NA &&
	    state->interface != ocelot_port->phy_mode) {
		bitmap_zero(supported, __ETHTOOL_LINK_MODE_MASK_NBITS);
		return;
	}

	phylink_set_port_modes(mask);
	phylink_set(mask, Autoneg);
	phylink_set(mask, Pause);
	phylink_set(mask, Asym_Pause);
	phylink_set(mask, 10baseT_Full);
	phylink_set(mask, 10baseT_Half);
	phylink_set(mask, 100baseT_Full);
	phylink_set(mask, 100baseT_Half);
	phylink_set(mask, 1000baseT_Full);

	if (state->interface == PHY_INTERFACE_MODE_INTERNAL) {
		phylink_set(mask, 2500baseT_Full);
		phylink_set(mask, 2500baseX_Full);
	}

	bitmap_and(supported, supported, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_and(state->advertising, state->advertising, mask,
		   __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static int vsc9953_prevalidate_phy_mode(struct ocelot *ocelot, int port,
					phy_interface_t phy_mode)
{
	switch (phy_mode) {
	case PHY_INTERFACE_MODE_INTERNAL:
		if (port != 8 && port != 9)
			return -ENOTSUPP;
		return 0;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
		/* Not supported on internal to-CPU ports */
		if (port == 8 || port == 9)
			return -ENOTSUPP;
		return 0;
	default:
		return -ENOTSUPP;
	}
}

/* Watermark encode
 * Bit 9:   Unit; 0:1, 1:16
 * Bit 8-0: Value to be multiplied with unit
 */
static u16 vsc9953_wm_enc(u16 value)
{
	if (value >= BIT(9))
		return BIT(9) | (value / 16);

	return value;
}

static const struct ocelot_ops vsc9953_ops = {
	.reset			= vsc9953_reset,
	.wm_enc			= vsc9953_wm_enc,
};

static int vsc9953_mdio_bus_alloc(struct ocelot *ocelot)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	struct device *dev = ocelot->dev;
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

	bus = devm_mdiobus_alloc(dev);
	if (!bus)
		return -ENOMEM;

	bus->name = "VSC9953 internal MDIO bus";
	bus->read = vsc9953_mdio_read;
	bus->write = vsc9953_mdio_write;
	bus->parent = dev;
	bus->priv = ocelot;
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
		int addr = port + 4;

		if (ocelot_port->phy_mode == PHY_INTERFACE_MODE_INTERNAL)
			continue;

		pcs = get_phy_device(felix->imdio, addr, false);
		if (IS_ERR(pcs))
			continue;

		pcs->interface = ocelot_port->phy_mode;
		felix->pcs[port] = pcs;

		dev_info(dev, "Found PCS at internal MDIO address %d\n", addr);
	}

	return 0;
}

static void vsc9953_xmit_template_populate(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	u8 *template = ocelot_port->xmit_template;
	u64 bypass, dest, src;

	/* Set the source port as the CPU port module and not the
	 * NPI port
	 */
	src = ocelot->num_phys_ports;
	dest = BIT(port);
	bypass = true;

	packing(template, &bypass, 127, 127, OCELOT_TAG_LEN, PACK, 0);
	packing(template, &dest,    67,  57, OCELOT_TAG_LEN, PACK, 0);
	packing(template, &src,     46,  43, OCELOT_TAG_LEN, PACK, 0);
}

static const struct felix_info seville_info_vsc9953 = {
	.target_io_res		= vsc9953_target_io_res,
	.port_io_res		= vsc9953_port_io_res,
	.regfields		= vsc9953_regfields,
	.map			= vsc9953_regmap,
	.ops			= &vsc9953_ops,
	.stats_layout		= vsc9953_stats_layout,
	.num_stats		= ARRAY_SIZE(vsc9953_stats_layout),
	.vcap_is2_keys		= vsc9953_vcap_is2_keys,
	.vcap_is2_actions	= vsc9953_vcap_is2_actions,
	.vcap			= vsc9953_vcap_props,
	.shared_queue_sz	= 2048 * 1024,
	.num_mact_rows		= 2048,
	.num_ports		= 10,
	.mdio_bus_alloc		= vsc9953_mdio_bus_alloc,
	.mdio_bus_free		= vsc9959_mdio_bus_free,
	.pcs_config		= vsc9959_pcs_config,
	.pcs_link_up		= vsc9959_pcs_link_up,
	.pcs_link_state		= vsc9959_pcs_link_state,
	.phylink_validate	= vsc9953_phylink_validate,
	.prevalidate_phy_mode	= vsc9953_prevalidate_phy_mode,
	.xmit_template_populate	= vsc9953_xmit_template_populate,
};

static int seville_probe(struct platform_device *pdev)
{
	struct dsa_switch *ds;
	struct ocelot *ocelot;
	struct resource *res;
	struct felix *felix;
	int err;

	felix = kzalloc(sizeof(struct felix), GFP_KERNEL);
	if (!felix) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "Failed to allocate driver memory\n");
		goto err_alloc_felix;
	}

	platform_set_drvdata(pdev, felix);

	ocelot = &felix->ocelot;
	ocelot->dev = &pdev->dev;
	felix->info = &seville_info_vsc9953;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	felix->switch_base = res->start;

	ds = kzalloc(sizeof(struct dsa_switch), GFP_KERNEL);
	if (!ds) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "Failed to allocate DSA switch\n");
		goto err_alloc_ds;
	}

	ds->dev = &pdev->dev;
	ds->num_ports = felix->info->num_ports;
	ds->ops = &felix_switch_ops;
	ds->priv = ocelot;
	felix->ds = ds;

	err = dsa_register_switch(ds);
	if (err) {
		dev_err(&pdev->dev, "Failed to register DSA switch: %d\n", err);
		goto err_register_ds;
	}

	return 0;

err_register_ds:
	kfree(ds);
err_alloc_ds:
err_alloc_felix:
	kfree(felix);
	return err;
}

static int seville_remove(struct platform_device *pdev)
{
	struct felix *felix;

	felix = platform_get_drvdata(pdev);

	dsa_unregister_switch(felix->ds);

	kfree(felix->ds);
	kfree(felix);

	return 0;
}

static const struct of_device_id seville_of_match[] = {
	{ .compatible = "mscc,vsc9953-switch" },
	{ },
};
MODULE_DEVICE_TABLE(of, seville_of_match);

struct platform_driver seville_vsc9953_driver = {
	.probe		= seville_probe,
	.remove		= seville_remove,
	.driver = {
		.name		= "mscc_seville",
		.of_match_table	= of_match_ptr(seville_of_match),
	},
};
