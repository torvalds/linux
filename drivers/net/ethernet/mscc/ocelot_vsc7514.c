// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_net.h>
#include <linux/netdevice.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/mfd/syscon.h>
#include <linux/skbuff.h>
#include <net/switchdev.h>

#include <soc/mscc/ocelot_vcap.h>
#include <soc/mscc/ocelot_hsio.h>
#include "ocelot.h"

#define IFH_EXTRACT_BITFIELD64(x, o, w) (((x) >> (o)) & GENMASK_ULL((w) - 1, 0))
#define VSC7514_VCAP_IS2_CNT 64
#define VSC7514_VCAP_IS2_ENTRY_WIDTH 376
#define VSC7514_VCAP_IS2_ACTION_WIDTH 99
#define VSC7514_VCAP_PORT_CNT 11

static const u32 ocelot_ana_regmap[] = {
	REG(ANA_ADVLEARN,				0x009000),
	REG(ANA_VLANMASK,				0x009004),
	REG(ANA_PORT_B_DOMAIN,				0x009008),
	REG(ANA_ANAGEFIL,				0x00900c),
	REG(ANA_ANEVENTS,				0x009010),
	REG(ANA_STORMLIMIT_BURST,			0x009014),
	REG(ANA_STORMLIMIT_CFG,				0x009018),
	REG(ANA_ISOLATED_PORTS,				0x009028),
	REG(ANA_COMMUNITY_PORTS,			0x00902c),
	REG(ANA_AUTOAGE,				0x009030),
	REG(ANA_MACTOPTIONS,				0x009034),
	REG(ANA_LEARNDISC,				0x009038),
	REG(ANA_AGENCTRL,				0x00903c),
	REG(ANA_MIRRORPORTS,				0x009040),
	REG(ANA_EMIRRORPORTS,				0x009044),
	REG(ANA_FLOODING,				0x009048),
	REG(ANA_FLOODING_IPMC,				0x00904c),
	REG(ANA_SFLOW_CFG,				0x009050),
	REG(ANA_PORT_MODE,				0x009080),
	REG(ANA_PGID_PGID,				0x008c00),
	REG(ANA_TABLES_ANMOVED,				0x008b30),
	REG(ANA_TABLES_MACHDATA,			0x008b34),
	REG(ANA_TABLES_MACLDATA,			0x008b38),
	REG(ANA_TABLES_MACACCESS,			0x008b3c),
	REG(ANA_TABLES_MACTINDX,			0x008b40),
	REG(ANA_TABLES_VLANACCESS,			0x008b44),
	REG(ANA_TABLES_VLANTIDX,			0x008b48),
	REG(ANA_TABLES_ISDXACCESS,			0x008b4c),
	REG(ANA_TABLES_ISDXTIDX,			0x008b50),
	REG(ANA_TABLES_ENTRYLIM,			0x008b00),
	REG(ANA_TABLES_PTP_ID_HIGH,			0x008b54),
	REG(ANA_TABLES_PTP_ID_LOW,			0x008b58),
	REG(ANA_MSTI_STATE,				0x008e00),
	REG(ANA_PORT_VLAN_CFG,				0x007000),
	REG(ANA_PORT_DROP_CFG,				0x007004),
	REG(ANA_PORT_QOS_CFG,				0x007008),
	REG(ANA_PORT_VCAP_CFG,				0x00700c),
	REG(ANA_PORT_VCAP_S1_KEY_CFG,			0x007010),
	REG(ANA_PORT_VCAP_S2_CFG,			0x00701c),
	REG(ANA_PORT_PCP_DEI_MAP,			0x007020),
	REG(ANA_PORT_CPU_FWD_CFG,			0x007060),
	REG(ANA_PORT_CPU_FWD_BPDU_CFG,			0x007064),
	REG(ANA_PORT_CPU_FWD_GARP_CFG,			0x007068),
	REG(ANA_PORT_CPU_FWD_CCM_CFG,			0x00706c),
	REG(ANA_PORT_PORT_CFG,				0x007070),
	REG(ANA_PORT_POL_CFG,				0x007074),
	REG(ANA_PORT_PTP_CFG,				0x007078),
	REG(ANA_PORT_PTP_DLY1_CFG,			0x00707c),
	REG(ANA_OAM_UPM_LM_CNT,				0x007c00),
	REG(ANA_PORT_PTP_DLY2_CFG,			0x007080),
	REG(ANA_PFC_PFC_CFG,				0x008800),
	REG(ANA_PFC_PFC_TIMER,				0x008804),
	REG(ANA_IPT_OAM_MEP_CFG,			0x008000),
	REG(ANA_IPT_IPT,				0x008004),
	REG(ANA_PPT_PPT,				0x008ac0),
	REG(ANA_FID_MAP_FID_MAP,			0x000000),
	REG(ANA_AGGR_CFG,				0x0090b4),
	REG(ANA_CPUQ_CFG,				0x0090b8),
	REG(ANA_CPUQ_CFG2,				0x0090bc),
	REG(ANA_CPUQ_8021_CFG,				0x0090c0),
	REG(ANA_DSCP_CFG,				0x009100),
	REG(ANA_DSCP_REWR_CFG,				0x009200),
	REG(ANA_VCAP_RNG_TYPE_CFG,			0x009240),
	REG(ANA_VCAP_RNG_VAL_CFG,			0x009260),
	REG(ANA_VRAP_CFG,				0x009280),
	REG(ANA_VRAP_HDR_DATA,				0x009284),
	REG(ANA_VRAP_HDR_MASK,				0x009288),
	REG(ANA_DISCARD_CFG,				0x00928c),
	REG(ANA_FID_CFG,				0x009290),
	REG(ANA_POL_PIR_CFG,				0x004000),
	REG(ANA_POL_CIR_CFG,				0x004004),
	REG(ANA_POL_MODE_CFG,				0x004008),
	REG(ANA_POL_PIR_STATE,				0x00400c),
	REG(ANA_POL_CIR_STATE,				0x004010),
	REG(ANA_POL_STATE,				0x004014),
	REG(ANA_POL_FLOWC,				0x008b80),
	REG(ANA_POL_HYST,				0x008bec),
	REG(ANA_POL_MISC_CFG,				0x008bf0),
};

static const u32 ocelot_qs_regmap[] = {
	REG(QS_XTR_GRP_CFG,				0x000000),
	REG(QS_XTR_RD,					0x000008),
	REG(QS_XTR_FRM_PRUNING,				0x000010),
	REG(QS_XTR_FLUSH,				0x000018),
	REG(QS_XTR_DATA_PRESENT,			0x00001c),
	REG(QS_XTR_CFG,					0x000020),
	REG(QS_INJ_GRP_CFG,				0x000024),
	REG(QS_INJ_WR,					0x00002c),
	REG(QS_INJ_CTRL,				0x000034),
	REG(QS_INJ_STATUS,				0x00003c),
	REG(QS_INJ_ERR,					0x000040),
	REG(QS_INH_DBG,					0x000048),
};

static const u32 ocelot_qsys_regmap[] = {
	REG(QSYS_PORT_MODE,				0x011200),
	REG(QSYS_SWITCH_PORT_MODE,			0x011234),
	REG(QSYS_STAT_CNT_CFG,				0x011264),
	REG(QSYS_EEE_CFG,				0x011268),
	REG(QSYS_EEE_THRES,				0x011294),
	REG(QSYS_IGR_NO_SHARING,			0x011298),
	REG(QSYS_EGR_NO_SHARING,			0x01129c),
	REG(QSYS_SW_STATUS,				0x0112a0),
	REG(QSYS_EXT_CPU_CFG,				0x0112d0),
	REG(QSYS_PAD_CFG,				0x0112d4),
	REG(QSYS_CPU_GROUP_MAP,				0x0112d8),
	REG(QSYS_QMAP,					0x0112dc),
	REG(QSYS_ISDX_SGRP,				0x011400),
	REG(QSYS_TIMED_FRAME_ENTRY,			0x014000),
	REG(QSYS_TFRM_MISC,				0x011310),
	REG(QSYS_TFRM_PORT_DLY,				0x011314),
	REG(QSYS_TFRM_TIMER_CFG_1,			0x011318),
	REG(QSYS_TFRM_TIMER_CFG_2,			0x01131c),
	REG(QSYS_TFRM_TIMER_CFG_3,			0x011320),
	REG(QSYS_TFRM_TIMER_CFG_4,			0x011324),
	REG(QSYS_TFRM_TIMER_CFG_5,			0x011328),
	REG(QSYS_TFRM_TIMER_CFG_6,			0x01132c),
	REG(QSYS_TFRM_TIMER_CFG_7,			0x011330),
	REG(QSYS_TFRM_TIMER_CFG_8,			0x011334),
	REG(QSYS_RED_PROFILE,				0x011338),
	REG(QSYS_RES_QOS_MODE,				0x011378),
	REG(QSYS_RES_CFG,				0x012000),
	REG(QSYS_RES_STAT,				0x012004),
	REG(QSYS_EGR_DROP_MODE,				0x01137c),
	REG(QSYS_EQ_CTRL,				0x011380),
	REG(QSYS_EVENTS_CORE,				0x011384),
	REG(QSYS_CIR_CFG,				0x000000),
	REG(QSYS_EIR_CFG,				0x000004),
	REG(QSYS_SE_CFG,				0x000008),
	REG(QSYS_SE_DWRR_CFG,				0x00000c),
	REG(QSYS_SE_CONNECT,				0x00003c),
	REG(QSYS_SE_DLB_SENSE,				0x000040),
	REG(QSYS_CIR_STATE,				0x000044),
	REG(QSYS_EIR_STATE,				0x000048),
	REG(QSYS_SE_STATE,				0x00004c),
	REG(QSYS_HSCH_MISC_CFG,				0x011388),
};

static const u32 ocelot_rew_regmap[] = {
	REG(REW_PORT_VLAN_CFG,				0x000000),
	REG(REW_TAG_CFG,				0x000004),
	REG(REW_PORT_CFG,				0x000008),
	REG(REW_DSCP_CFG,				0x00000c),
	REG(REW_PCP_DEI_QOS_MAP_CFG,			0x000010),
	REG(REW_PTP_CFG,				0x000050),
	REG(REW_PTP_DLY1_CFG,				0x000054),
	REG(REW_DSCP_REMAP_DP1_CFG,			0x000690),
	REG(REW_DSCP_REMAP_CFG,				0x000790),
	REG(REW_STAT_CFG,				0x000890),
	REG(REW_PPT,					0x000680),
};

static const u32 ocelot_sys_regmap[] = {
	REG(SYS_COUNT_RX_OCTETS,			0x000000),
	REG(SYS_COUNT_RX_UNICAST,			0x000004),
	REG(SYS_COUNT_RX_MULTICAST,			0x000008),
	REG(SYS_COUNT_RX_BROADCAST,			0x00000c),
	REG(SYS_COUNT_RX_SHORTS,			0x000010),
	REG(SYS_COUNT_RX_FRAGMENTS,			0x000014),
	REG(SYS_COUNT_RX_JABBERS,			0x000018),
	REG(SYS_COUNT_RX_CRC_ALIGN_ERRS,		0x00001c),
	REG(SYS_COUNT_RX_SYM_ERRS,			0x000020),
	REG(SYS_COUNT_RX_64,				0x000024),
	REG(SYS_COUNT_RX_65_127,			0x000028),
	REG(SYS_COUNT_RX_128_255,			0x00002c),
	REG(SYS_COUNT_RX_256_1023,			0x000030),
	REG(SYS_COUNT_RX_1024_1526,			0x000034),
	REG(SYS_COUNT_RX_1527_MAX,			0x000038),
	REG(SYS_COUNT_RX_PAUSE,				0x00003c),
	REG(SYS_COUNT_RX_CONTROL,			0x000040),
	REG(SYS_COUNT_RX_LONGS,				0x000044),
	REG(SYS_COUNT_RX_CLASSIFIED_DROPS,		0x000048),
	REG(SYS_COUNT_TX_OCTETS,			0x000100),
	REG(SYS_COUNT_TX_UNICAST,			0x000104),
	REG(SYS_COUNT_TX_MULTICAST,			0x000108),
	REG(SYS_COUNT_TX_BROADCAST,			0x00010c),
	REG(SYS_COUNT_TX_COLLISION,			0x000110),
	REG(SYS_COUNT_TX_DROPS,				0x000114),
	REG(SYS_COUNT_TX_PAUSE,				0x000118),
	REG(SYS_COUNT_TX_64,				0x00011c),
	REG(SYS_COUNT_TX_65_127,			0x000120),
	REG(SYS_COUNT_TX_128_511,			0x000124),
	REG(SYS_COUNT_TX_512_1023,			0x000128),
	REG(SYS_COUNT_TX_1024_1526,			0x00012c),
	REG(SYS_COUNT_TX_1527_MAX,			0x000130),
	REG(SYS_COUNT_TX_AGING,				0x000170),
	REG(SYS_RESET_CFG,				0x000508),
	REG(SYS_CMID,					0x00050c),
	REG(SYS_VLAN_ETYPE_CFG,				0x000510),
	REG(SYS_PORT_MODE,				0x000514),
	REG(SYS_FRONT_PORT_MODE,			0x000548),
	REG(SYS_FRM_AGING,				0x000574),
	REG(SYS_STAT_CFG,				0x000578),
	REG(SYS_SW_STATUS,				0x00057c),
	REG(SYS_MISC_CFG,				0x0005ac),
	REG(SYS_REW_MAC_HIGH_CFG,			0x0005b0),
	REG(SYS_REW_MAC_LOW_CFG,			0x0005dc),
	REG(SYS_CM_ADDR,				0x000500),
	REG(SYS_CM_DATA,				0x000504),
	REG(SYS_PAUSE_CFG,				0x000608),
	REG(SYS_PAUSE_TOT_CFG,				0x000638),
	REG(SYS_ATOP,					0x00063c),
	REG(SYS_ATOP_TOT_CFG,				0x00066c),
	REG(SYS_MAC_FC_CFG,				0x000670),
	REG(SYS_MMGT,					0x00069c),
	REG(SYS_MMGT_FAST,				0x0006a0),
	REG(SYS_EVENTS_DIF,				0x0006a4),
	REG(SYS_EVENTS_CORE,				0x0006b4),
	REG(SYS_CNT,					0x000000),
	REG(SYS_PTP_STATUS,				0x0006b8),
	REG(SYS_PTP_TXSTAMP,				0x0006bc),
	REG(SYS_PTP_NXT,				0x0006c0),
	REG(SYS_PTP_CFG,				0x0006c4),
};

static const u32 ocelot_vcap_regmap[] = {
	/* VCAP_CORE_CFG */
	REG(VCAP_CORE_UPDATE_CTRL,			0x000000),
	REG(VCAP_CORE_MV_CFG,				0x000004),
	/* VCAP_CORE_CACHE */
	REG(VCAP_CACHE_ENTRY_DAT,			0x000008),
	REG(VCAP_CACHE_MASK_DAT,			0x000108),
	REG(VCAP_CACHE_ACTION_DAT,			0x000208),
	REG(VCAP_CACHE_CNT_DAT,				0x000308),
	REG(VCAP_CACHE_TG_DAT,				0x000388),
};

static const u32 ocelot_ptp_regmap[] = {
	REG(PTP_PIN_CFG,				0x000000),
	REG(PTP_PIN_TOD_SEC_MSB,			0x000004),
	REG(PTP_PIN_TOD_SEC_LSB,			0x000008),
	REG(PTP_PIN_TOD_NSEC,				0x00000c),
	REG(PTP_PIN_WF_HIGH_PERIOD,			0x000014),
	REG(PTP_PIN_WF_LOW_PERIOD,			0x000018),
	REG(PTP_CFG_MISC,				0x0000a0),
	REG(PTP_CLK_CFG_ADJ_CFG,			0x0000a4),
	REG(PTP_CLK_CFG_ADJ_FREQ,			0x0000a8),
};

static const u32 ocelot_dev_gmii_regmap[] = {
	REG(DEV_CLOCK_CFG,				0x0),
	REG(DEV_PORT_MISC,				0x4),
	REG(DEV_EVENTS,					0x8),
	REG(DEV_EEE_CFG,				0xc),
	REG(DEV_RX_PATH_DELAY,				0x10),
	REG(DEV_TX_PATH_DELAY,				0x14),
	REG(DEV_PTP_PREDICT_CFG,			0x18),
	REG(DEV_MAC_ENA_CFG,				0x1c),
	REG(DEV_MAC_MODE_CFG,				0x20),
	REG(DEV_MAC_MAXLEN_CFG,				0x24),
	REG(DEV_MAC_TAGS_CFG,				0x28),
	REG(DEV_MAC_ADV_CHK_CFG,			0x2c),
	REG(DEV_MAC_IFG_CFG,				0x30),
	REG(DEV_MAC_HDX_CFG,				0x34),
	REG(DEV_MAC_DBG_CFG,				0x38),
	REG(DEV_MAC_FC_MAC_LOW_CFG,			0x3c),
	REG(DEV_MAC_FC_MAC_HIGH_CFG,			0x40),
	REG(DEV_MAC_STICKY,				0x44),
	REG(PCS1G_CFG,					0x48),
	REG(PCS1G_MODE_CFG,				0x4c),
	REG(PCS1G_SD_CFG,				0x50),
	REG(PCS1G_ANEG_CFG,				0x54),
	REG(PCS1G_ANEG_NP_CFG,				0x58),
	REG(PCS1G_LB_CFG,				0x5c),
	REG(PCS1G_DBG_CFG,				0x60),
	REG(PCS1G_CDET_CFG,				0x64),
	REG(PCS1G_ANEG_STATUS,				0x68),
	REG(PCS1G_ANEG_NP_STATUS,			0x6c),
	REG(PCS1G_LINK_STATUS,				0x70),
	REG(PCS1G_LINK_DOWN_CNT,			0x74),
	REG(PCS1G_STICKY,				0x78),
	REG(PCS1G_DEBUG_STATUS,				0x7c),
	REG(PCS1G_LPI_CFG,				0x80),
	REG(PCS1G_LPI_WAKE_ERROR_CNT,			0x84),
	REG(PCS1G_LPI_STATUS,				0x88),
	REG(PCS1G_TSTPAT_MODE_CFG,			0x8c),
	REG(PCS1G_TSTPAT_STATUS,			0x90),
	REG(DEV_PCS_FX100_CFG,				0x94),
	REG(DEV_PCS_FX100_STATUS,			0x98),
};

static const u32 *ocelot_regmap[TARGET_MAX] = {
	[ANA] = ocelot_ana_regmap,
	[QS] = ocelot_qs_regmap,
	[QSYS] = ocelot_qsys_regmap,
	[REW] = ocelot_rew_regmap,
	[SYS] = ocelot_sys_regmap,
	[S2] = ocelot_vcap_regmap,
	[PTP] = ocelot_ptp_regmap,
	[DEV_GMII] = ocelot_dev_gmii_regmap,
};

static const struct reg_field ocelot_regfields[REGFIELD_MAX] = {
	[ANA_ADVLEARN_VLAN_CHK] = REG_FIELD(ANA_ADVLEARN, 11, 11),
	[ANA_ADVLEARN_LEARN_MIRROR] = REG_FIELD(ANA_ADVLEARN, 0, 10),
	[ANA_ANEVENTS_MSTI_DROP] = REG_FIELD(ANA_ANEVENTS, 27, 27),
	[ANA_ANEVENTS_ACLKILL] = REG_FIELD(ANA_ANEVENTS, 26, 26),
	[ANA_ANEVENTS_ACLUSED] = REG_FIELD(ANA_ANEVENTS, 25, 25),
	[ANA_ANEVENTS_AUTOAGE] = REG_FIELD(ANA_ANEVENTS, 24, 24),
	[ANA_ANEVENTS_VS2TTL1] = REG_FIELD(ANA_ANEVENTS, 23, 23),
	[ANA_ANEVENTS_STORM_DROP] = REG_FIELD(ANA_ANEVENTS, 22, 22),
	[ANA_ANEVENTS_LEARN_DROP] = REG_FIELD(ANA_ANEVENTS, 21, 21),
	[ANA_ANEVENTS_AGED_ENTRY] = REG_FIELD(ANA_ANEVENTS, 20, 20),
	[ANA_ANEVENTS_CPU_LEARN_FAILED] = REG_FIELD(ANA_ANEVENTS, 19, 19),
	[ANA_ANEVENTS_AUTO_LEARN_FAILED] = REG_FIELD(ANA_ANEVENTS, 18, 18),
	[ANA_ANEVENTS_LEARN_REMOVE] = REG_FIELD(ANA_ANEVENTS, 17, 17),
	[ANA_ANEVENTS_AUTO_LEARNED] = REG_FIELD(ANA_ANEVENTS, 16, 16),
	[ANA_ANEVENTS_AUTO_MOVED] = REG_FIELD(ANA_ANEVENTS, 15, 15),
	[ANA_ANEVENTS_DROPPED] = REG_FIELD(ANA_ANEVENTS, 14, 14),
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
	[ANA_TABLES_MACACCESS_B_DOM] = REG_FIELD(ANA_TABLES_MACACCESS, 18, 18),
	[ANA_TABLES_MACTINDX_BUCKET] = REG_FIELD(ANA_TABLES_MACTINDX, 10, 11),
	[ANA_TABLES_MACTINDX_M_INDEX] = REG_FIELD(ANA_TABLES_MACTINDX, 0, 9),
	[QSYS_TIMED_FRAME_ENTRY_TFRM_VLD] = REG_FIELD(QSYS_TIMED_FRAME_ENTRY, 20, 20),
	[QSYS_TIMED_FRAME_ENTRY_TFRM_FP] = REG_FIELD(QSYS_TIMED_FRAME_ENTRY, 8, 19),
	[QSYS_TIMED_FRAME_ENTRY_TFRM_PORTNO] = REG_FIELD(QSYS_TIMED_FRAME_ENTRY, 4, 7),
	[QSYS_TIMED_FRAME_ENTRY_TFRM_TM_SEL] = REG_FIELD(QSYS_TIMED_FRAME_ENTRY, 1, 3),
	[QSYS_TIMED_FRAME_ENTRY_TFRM_TM_T] = REG_FIELD(QSYS_TIMED_FRAME_ENTRY, 0, 0),
	[SYS_RESET_CFG_CORE_ENA] = REG_FIELD(SYS_RESET_CFG, 2, 2),
	[SYS_RESET_CFG_MEM_ENA] = REG_FIELD(SYS_RESET_CFG, 1, 1),
	[SYS_RESET_CFG_MEM_INIT] = REG_FIELD(SYS_RESET_CFG, 0, 0),
	/* Replicated per number of ports (12), register size 4 per port */
	[QSYS_SWITCH_PORT_MODE_PORT_ENA] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 14, 14, 12, 4),
	[QSYS_SWITCH_PORT_MODE_SCH_NEXT_CFG] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 11, 13, 12, 4),
	[QSYS_SWITCH_PORT_MODE_YEL_RSRVD] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 10, 10, 12, 4),
	[QSYS_SWITCH_PORT_MODE_INGRESS_DROP_MODE] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 9, 9, 12, 4),
	[QSYS_SWITCH_PORT_MODE_TX_PFC_ENA] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 1, 8, 12, 4),
	[QSYS_SWITCH_PORT_MODE_TX_PFC_MODE] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 0, 0, 12, 4),
	[SYS_PORT_MODE_DATA_WO_TS] = REG_FIELD_ID(SYS_PORT_MODE, 5, 6, 12, 4),
	[SYS_PORT_MODE_INCL_INJ_HDR] = REG_FIELD_ID(SYS_PORT_MODE, 3, 4, 12, 4),
	[SYS_PORT_MODE_INCL_XTR_HDR] = REG_FIELD_ID(SYS_PORT_MODE, 1, 2, 12, 4),
	[SYS_PORT_MODE_INCL_HDR_ERR] = REG_FIELD_ID(SYS_PORT_MODE, 0, 0, 12, 4),
	[SYS_PAUSE_CFG_PAUSE_START] = REG_FIELD_ID(SYS_PAUSE_CFG, 10, 18, 12, 4),
	[SYS_PAUSE_CFG_PAUSE_STOP] = REG_FIELD_ID(SYS_PAUSE_CFG, 1, 9, 12, 4),
	[SYS_PAUSE_CFG_PAUSE_ENA] = REG_FIELD_ID(SYS_PAUSE_CFG, 0, 1, 12, 4),
};

static const struct ocelot_stat_layout ocelot_stats_layout[] = {
	{ .name = "rx_octets", .offset = 0x00, },
	{ .name = "rx_unicast", .offset = 0x01, },
	{ .name = "rx_multicast", .offset = 0x02, },
	{ .name = "rx_broadcast", .offset = 0x03, },
	{ .name = "rx_shorts", .offset = 0x04, },
	{ .name = "rx_fragments", .offset = 0x05, },
	{ .name = "rx_jabbers", .offset = 0x06, },
	{ .name = "rx_crc_align_errs", .offset = 0x07, },
	{ .name = "rx_sym_errs", .offset = 0x08, },
	{ .name = "rx_frames_below_65_octets", .offset = 0x09, },
	{ .name = "rx_frames_65_to_127_octets", .offset = 0x0A, },
	{ .name = "rx_frames_128_to_255_octets", .offset = 0x0B, },
	{ .name = "rx_frames_256_to_511_octets", .offset = 0x0C, },
	{ .name = "rx_frames_512_to_1023_octets", .offset = 0x0D, },
	{ .name = "rx_frames_1024_to_1526_octets", .offset = 0x0E, },
	{ .name = "rx_frames_over_1526_octets", .offset = 0x0F, },
	{ .name = "rx_pause", .offset = 0x10, },
	{ .name = "rx_control", .offset = 0x11, },
	{ .name = "rx_longs", .offset = 0x12, },
	{ .name = "rx_classified_drops", .offset = 0x13, },
	{ .name = "rx_red_prio_0", .offset = 0x14, },
	{ .name = "rx_red_prio_1", .offset = 0x15, },
	{ .name = "rx_red_prio_2", .offset = 0x16, },
	{ .name = "rx_red_prio_3", .offset = 0x17, },
	{ .name = "rx_red_prio_4", .offset = 0x18, },
	{ .name = "rx_red_prio_5", .offset = 0x19, },
	{ .name = "rx_red_prio_6", .offset = 0x1A, },
	{ .name = "rx_red_prio_7", .offset = 0x1B, },
	{ .name = "rx_yellow_prio_0", .offset = 0x1C, },
	{ .name = "rx_yellow_prio_1", .offset = 0x1D, },
	{ .name = "rx_yellow_prio_2", .offset = 0x1E, },
	{ .name = "rx_yellow_prio_3", .offset = 0x1F, },
	{ .name = "rx_yellow_prio_4", .offset = 0x20, },
	{ .name = "rx_yellow_prio_5", .offset = 0x21, },
	{ .name = "rx_yellow_prio_6", .offset = 0x22, },
	{ .name = "rx_yellow_prio_7", .offset = 0x23, },
	{ .name = "rx_green_prio_0", .offset = 0x24, },
	{ .name = "rx_green_prio_1", .offset = 0x25, },
	{ .name = "rx_green_prio_2", .offset = 0x26, },
	{ .name = "rx_green_prio_3", .offset = 0x27, },
	{ .name = "rx_green_prio_4", .offset = 0x28, },
	{ .name = "rx_green_prio_5", .offset = 0x29, },
	{ .name = "rx_green_prio_6", .offset = 0x2A, },
	{ .name = "rx_green_prio_7", .offset = 0x2B, },
	{ .name = "tx_octets", .offset = 0x40, },
	{ .name = "tx_unicast", .offset = 0x41, },
	{ .name = "tx_multicast", .offset = 0x42, },
	{ .name = "tx_broadcast", .offset = 0x43, },
	{ .name = "tx_collision", .offset = 0x44, },
	{ .name = "tx_drops", .offset = 0x45, },
	{ .name = "tx_pause", .offset = 0x46, },
	{ .name = "tx_frames_below_65_octets", .offset = 0x47, },
	{ .name = "tx_frames_65_to_127_octets", .offset = 0x48, },
	{ .name = "tx_frames_128_255_octets", .offset = 0x49, },
	{ .name = "tx_frames_256_511_octets", .offset = 0x4A, },
	{ .name = "tx_frames_512_1023_octets", .offset = 0x4B, },
	{ .name = "tx_frames_1024_1526_octets", .offset = 0x4C, },
	{ .name = "tx_frames_over_1526_octets", .offset = 0x4D, },
	{ .name = "tx_yellow_prio_0", .offset = 0x4E, },
	{ .name = "tx_yellow_prio_1", .offset = 0x4F, },
	{ .name = "tx_yellow_prio_2", .offset = 0x50, },
	{ .name = "tx_yellow_prio_3", .offset = 0x51, },
	{ .name = "tx_yellow_prio_4", .offset = 0x52, },
	{ .name = "tx_yellow_prio_5", .offset = 0x53, },
	{ .name = "tx_yellow_prio_6", .offset = 0x54, },
	{ .name = "tx_yellow_prio_7", .offset = 0x55, },
	{ .name = "tx_green_prio_0", .offset = 0x56, },
	{ .name = "tx_green_prio_1", .offset = 0x57, },
	{ .name = "tx_green_prio_2", .offset = 0x58, },
	{ .name = "tx_green_prio_3", .offset = 0x59, },
	{ .name = "tx_green_prio_4", .offset = 0x5A, },
	{ .name = "tx_green_prio_5", .offset = 0x5B, },
	{ .name = "tx_green_prio_6", .offset = 0x5C, },
	{ .name = "tx_green_prio_7", .offset = 0x5D, },
	{ .name = "tx_aged", .offset = 0x5E, },
	{ .name = "drop_local", .offset = 0x80, },
	{ .name = "drop_tail", .offset = 0x81, },
	{ .name = "drop_yellow_prio_0", .offset = 0x82, },
	{ .name = "drop_yellow_prio_1", .offset = 0x83, },
	{ .name = "drop_yellow_prio_2", .offset = 0x84, },
	{ .name = "drop_yellow_prio_3", .offset = 0x85, },
	{ .name = "drop_yellow_prio_4", .offset = 0x86, },
	{ .name = "drop_yellow_prio_5", .offset = 0x87, },
	{ .name = "drop_yellow_prio_6", .offset = 0x88, },
	{ .name = "drop_yellow_prio_7", .offset = 0x89, },
	{ .name = "drop_green_prio_0", .offset = 0x8A, },
	{ .name = "drop_green_prio_1", .offset = 0x8B, },
	{ .name = "drop_green_prio_2", .offset = 0x8C, },
	{ .name = "drop_green_prio_3", .offset = 0x8D, },
	{ .name = "drop_green_prio_4", .offset = 0x8E, },
	{ .name = "drop_green_prio_5", .offset = 0x8F, },
	{ .name = "drop_green_prio_6", .offset = 0x90, },
	{ .name = "drop_green_prio_7", .offset = 0x91, },
};

static void ocelot_pll5_init(struct ocelot *ocelot)
{
	/* Configure PLL5. This will need a proper CCF driver
	 * The values are coming from the VTSS API for Ocelot
	 */
	regmap_write(ocelot->targets[HSIO], HSIO_PLL5G_CFG4,
		     HSIO_PLL5G_CFG4_IB_CTRL(0x7600) |
		     HSIO_PLL5G_CFG4_IB_BIAS_CTRL(0x8));
	regmap_write(ocelot->targets[HSIO], HSIO_PLL5G_CFG0,
		     HSIO_PLL5G_CFG0_CORE_CLK_DIV(0x11) |
		     HSIO_PLL5G_CFG0_CPU_CLK_DIV(2) |
		     HSIO_PLL5G_CFG0_ENA_BIAS |
		     HSIO_PLL5G_CFG0_ENA_VCO_BUF |
		     HSIO_PLL5G_CFG0_ENA_CP1 |
		     HSIO_PLL5G_CFG0_SELCPI(2) |
		     HSIO_PLL5G_CFG0_LOOP_BW_RES(0xe) |
		     HSIO_PLL5G_CFG0_SELBGV820(4) |
		     HSIO_PLL5G_CFG0_DIV4 |
		     HSIO_PLL5G_CFG0_ENA_CLKTREE |
		     HSIO_PLL5G_CFG0_ENA_LANE);
	regmap_write(ocelot->targets[HSIO], HSIO_PLL5G_CFG2,
		     HSIO_PLL5G_CFG2_EN_RESET_FRQ_DET |
		     HSIO_PLL5G_CFG2_EN_RESET_OVERRUN |
		     HSIO_PLL5G_CFG2_GAIN_TEST(0x8) |
		     HSIO_PLL5G_CFG2_ENA_AMPCTRL |
		     HSIO_PLL5G_CFG2_PWD_AMPCTRL_N |
		     HSIO_PLL5G_CFG2_AMPC_SEL(0x10));
}

static int ocelot_chip_init(struct ocelot *ocelot, const struct ocelot_ops *ops)
{
	int ret;

	ocelot->map = ocelot_regmap;
	ocelot->stats_layout = ocelot_stats_layout;
	ocelot->num_stats = ARRAY_SIZE(ocelot_stats_layout);
	ocelot->shared_queue_sz = 224 * 1024;
	ocelot->num_mact_rows = 1024;
	ocelot->ops = ops;

	ret = ocelot_regfields_init(ocelot, ocelot_regfields);
	if (ret)
		return ret;

	ocelot_pll5_init(ocelot);

	eth_random_addr(ocelot->base_mac);
	ocelot->base_mac[5] &= 0xf0;

	return 0;
}

static int ocelot_parse_ifh(u32 *_ifh, struct frame_info *info)
{
	u8 llen, wlen;
	u64 ifh[2];

	ifh[0] = be64_to_cpu(((__force __be64 *)_ifh)[0]);
	ifh[1] = be64_to_cpu(((__force __be64 *)_ifh)[1]);

	wlen = IFH_EXTRACT_BITFIELD64(ifh[0], 7,  8);
	llen = IFH_EXTRACT_BITFIELD64(ifh[0], 15,  6);

	info->len = OCELOT_BUFFER_CELL_SZ * wlen + llen - 80;

	info->timestamp = IFH_EXTRACT_BITFIELD64(ifh[0], 21, 32);

	info->port = IFH_EXTRACT_BITFIELD64(ifh[1], 43, 4);

	info->tag_type = IFH_EXTRACT_BITFIELD64(ifh[1], 16,  1);
	info->vid = IFH_EXTRACT_BITFIELD64(ifh[1], 0,  12);

	return 0;
}

static int ocelot_rx_frame_word(struct ocelot *ocelot, u8 grp, bool ifh,
				u32 *rval)
{
	u32 val;
	u32 bytes_valid;

	val = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
	if (val == XTR_NOT_READY) {
		if (ifh)
			return -EIO;

		do {
			val = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
		} while (val == XTR_NOT_READY);
	}

	switch (val) {
	case XTR_ABORT:
		return -EIO;
	case XTR_EOF_0:
	case XTR_EOF_1:
	case XTR_EOF_2:
	case XTR_EOF_3:
	case XTR_PRUNED:
		bytes_valid = XTR_VALID_BYTES(val);
		val = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
		if (val == XTR_ESCAPE)
			*rval = ocelot_read_rix(ocelot, QS_XTR_RD, grp);
		else
			*rval = val;

		return bytes_valid;
	case XTR_ESCAPE:
		*rval = ocelot_read_rix(ocelot, QS_XTR_RD, grp);

		return 4;
	default:
		*rval = val;

		return 4;
	}
}

static irqreturn_t ocelot_xtr_irq_handler(int irq, void *arg)
{
	struct ocelot *ocelot = arg;
	int i = 0, grp = 0;
	int err = 0;

	if (!(ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp)))
		return IRQ_NONE;

	do {
		struct skb_shared_hwtstamps *shhwtstamps;
		struct ocelot_port_private *priv;
		struct ocelot_port *ocelot_port;
		u64 tod_in_ns, full_ts_in_ns;
		struct frame_info info = {};
		struct net_device *dev;
		u32 ifh[4], val, *buf;
		struct timespec64 ts;
		int sz, len, buf_len;
		struct sk_buff *skb;

		for (i = 0; i < OCELOT_TAG_LEN / 4; i++) {
			err = ocelot_rx_frame_word(ocelot, grp, true, &ifh[i]);
			if (err != 4)
				break;
		}

		if (err != 4)
			break;

		/* At this point the IFH was read correctly, so it is safe to
		 * presume that there is no error. The err needs to be reset
		 * otherwise a frame could come in CPU queue between the while
		 * condition and the check for error later on. And in that case
		 * the new frame is just removed and not processed.
		 */
		err = 0;

		ocelot_parse_ifh(ifh, &info);

		ocelot_port = ocelot->ports[info.port];
		priv = container_of(ocelot_port, struct ocelot_port_private,
				    port);
		dev = priv->dev;

		skb = netdev_alloc_skb(dev, info.len);

		if (unlikely(!skb)) {
			netdev_err(dev, "Unable to allocate sk_buff\n");
			err = -ENOMEM;
			break;
		}
		buf_len = info.len - ETH_FCS_LEN;
		buf = (u32 *)skb_put(skb, buf_len);

		len = 0;
		do {
			sz = ocelot_rx_frame_word(ocelot, grp, false, &val);
			*buf++ = val;
			len += sz;
		} while (len < buf_len);

		/* Read the FCS */
		sz = ocelot_rx_frame_word(ocelot, grp, false, &val);
		/* Update the statistics if part of the FCS was read before */
		len -= ETH_FCS_LEN - sz;

		if (unlikely(dev->features & NETIF_F_RXFCS)) {
			buf = (u32 *)skb_put(skb, ETH_FCS_LEN);
			*buf = val;
		}

		if (sz < 0) {
			err = sz;
			break;
		}

		if (ocelot->ptp) {
			ocelot_ptp_gettime64(&ocelot->ptp_info, &ts);

			tod_in_ns = ktime_set(ts.tv_sec, ts.tv_nsec);
			if ((tod_in_ns & 0xffffffff) < info.timestamp)
				full_ts_in_ns = (((tod_in_ns >> 32) - 1) << 32) |
						info.timestamp;
			else
				full_ts_in_ns = (tod_in_ns & GENMASK_ULL(63, 32)) |
						info.timestamp;

			shhwtstamps = skb_hwtstamps(skb);
			memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
			shhwtstamps->hwtstamp = full_ts_in_ns;
		}

		/* Everything we see on an interface that is in the HW bridge
		 * has already been forwarded.
		 */
		if (ocelot->bridge_mask & BIT(info.port))
			skb->offload_fwd_mark = 1;

		skb->protocol = eth_type_trans(skb, dev);
		if (!skb_defer_rx_timestamp(skb))
			netif_rx(skb);
		dev->stats.rx_bytes += len;
		dev->stats.rx_packets++;
	} while (ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp));

	if (err)
		while (ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp))
			ocelot_read_rix(ocelot, QS_XTR_RD, grp);

	return IRQ_HANDLED;
}

static irqreturn_t ocelot_ptp_rdy_irq_handler(int irq, void *arg)
{
	struct ocelot *ocelot = arg;

	ocelot_get_txtstamp(ocelot);

	return IRQ_HANDLED;
}

static const struct of_device_id mscc_ocelot_match[] = {
	{ .compatible = "mscc,vsc7514-switch" },
	{ }
};
MODULE_DEVICE_TABLE(of, mscc_ocelot_match);

static int ocelot_reset(struct ocelot *ocelot)
{
	int retries = 100;
	u32 val;

	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_INIT], 1);
	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_ENA], 1);

	do {
		msleep(1);
		regmap_field_read(ocelot->regfields[SYS_RESET_CFG_MEM_INIT],
				  &val);
	} while (val && --retries);

	if (!retries)
		return -ETIMEDOUT;

	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_MEM_ENA], 1);
	regmap_field_write(ocelot->regfields[SYS_RESET_CFG_CORE_ENA], 1);

	return 0;
}

/* Watermark encode
 * Bit 8:   Unit; 0:1, 1:16
 * Bit 7-0: Value to be multiplied with unit
 */
static u16 ocelot_wm_enc(u16 value)
{
	if (value >= BIT(8))
		return BIT(8) | (value / 16);

	return value;
}

static const struct ocelot_ops ocelot_ops = {
	.reset			= ocelot_reset,
	.wm_enc			= ocelot_wm_enc,
};

static const struct vcap_field vsc7514_vcap_es0_keys[] = {
	[VCAP_ES0_EGR_PORT]			= {  0,  4},
	[VCAP_ES0_IGR_PORT]			= {  4,  4},
	[VCAP_ES0_RSV]				= {  8,  2},
	[VCAP_ES0_L2_MC]			= { 10,  1},
	[VCAP_ES0_L2_BC]			= { 11,  1},
	[VCAP_ES0_VID]				= { 12, 12},
	[VCAP_ES0_DP]				= { 24,  1},
	[VCAP_ES0_PCP]				= { 25,  3},
};

static const struct vcap_field vsc7514_vcap_es0_actions[] = {
	[VCAP_ES0_ACT_PUSH_OUTER_TAG]		= {  0,  2},
	[VCAP_ES0_ACT_PUSH_INNER_TAG]		= {  2,  1},
	[VCAP_ES0_ACT_TAG_A_TPID_SEL]		= {  3,  2},
	[VCAP_ES0_ACT_TAG_A_VID_SEL]		= {  5,  1},
	[VCAP_ES0_ACT_TAG_A_PCP_SEL]		= {  6,  2},
	[VCAP_ES0_ACT_TAG_A_DEI_SEL]		= {  8,  2},
	[VCAP_ES0_ACT_TAG_B_TPID_SEL]		= { 10,  2},
	[VCAP_ES0_ACT_TAG_B_VID_SEL]		= { 12,  1},
	[VCAP_ES0_ACT_TAG_B_PCP_SEL]		= { 13,  2},
	[VCAP_ES0_ACT_TAG_B_DEI_SEL]		= { 15,  2},
	[VCAP_ES0_ACT_VID_A_VAL]		= { 17, 12},
	[VCAP_ES0_ACT_PCP_A_VAL]		= { 29,  3},
	[VCAP_ES0_ACT_DEI_A_VAL]		= { 32,  1},
	[VCAP_ES0_ACT_VID_B_VAL]		= { 33, 12},
	[VCAP_ES0_ACT_PCP_B_VAL]		= { 45,  3},
	[VCAP_ES0_ACT_DEI_B_VAL]		= { 48,  1},
	[VCAP_ES0_ACT_RSV]			= { 49, 24},
	[VCAP_ES0_ACT_HIT_STICKY]		= { 73,  1},
};

static const struct vcap_field vsc7514_vcap_is1_keys[] = {
	[VCAP_IS1_HK_TYPE]			= {  0,   1},
	[VCAP_IS1_HK_LOOKUP]			= {  1,   2},
	[VCAP_IS1_HK_IGR_PORT_MASK]		= {  3,  12},
	[VCAP_IS1_HK_RSV]			= { 15,   9},
	[VCAP_IS1_HK_OAM_Y1731]			= { 24,   1},
	[VCAP_IS1_HK_L2_MC]			= { 25,   1},
	[VCAP_IS1_HK_L2_BC]			= { 26,   1},
	[VCAP_IS1_HK_IP_MC]			= { 27,   1},
	[VCAP_IS1_HK_VLAN_TAGGED]		= { 28,   1},
	[VCAP_IS1_HK_VLAN_DBL_TAGGED]		= { 29,   1},
	[VCAP_IS1_HK_TPID]			= { 30,   1},
	[VCAP_IS1_HK_VID]			= { 31,  12},
	[VCAP_IS1_HK_DEI]			= { 43,   1},
	[VCAP_IS1_HK_PCP]			= { 44,   3},
	/* Specific Fields for IS1 Half Key S1_NORMAL */
	[VCAP_IS1_HK_L2_SMAC]			= { 47,  48},
	[VCAP_IS1_HK_ETYPE_LEN]			= { 95,   1},
	[VCAP_IS1_HK_ETYPE]			= { 96,  16},
	[VCAP_IS1_HK_IP_SNAP]			= {112,   1},
	[VCAP_IS1_HK_IP4]			= {113,   1},
	/* Layer-3 Information */
	[VCAP_IS1_HK_L3_FRAGMENT]		= {114,   1},
	[VCAP_IS1_HK_L3_FRAG_OFS_GT0]		= {115,   1},
	[VCAP_IS1_HK_L3_OPTIONS]		= {116,   1},
	[VCAP_IS1_HK_L3_DSCP]			= {117,   6},
	[VCAP_IS1_HK_L3_IP4_SIP]		= {123,  32},
	/* Layer-4 Information */
	[VCAP_IS1_HK_TCP_UDP]			= {155,   1},
	[VCAP_IS1_HK_TCP]			= {156,   1},
	[VCAP_IS1_HK_L4_SPORT]			= {157,  16},
	[VCAP_IS1_HK_L4_RNG]			= {173,   8},
	/* Specific Fields for IS1 Half Key S1_5TUPLE_IP4 */
	[VCAP_IS1_HK_IP4_INNER_TPID]            = { 47,   1},
	[VCAP_IS1_HK_IP4_INNER_VID]		= { 48,  12},
	[VCAP_IS1_HK_IP4_INNER_DEI]		= { 60,   1},
	[VCAP_IS1_HK_IP4_INNER_PCP]		= { 61,   3},
	[VCAP_IS1_HK_IP4_IP4]			= { 64,   1},
	[VCAP_IS1_HK_IP4_L3_FRAGMENT]		= { 65,   1},
	[VCAP_IS1_HK_IP4_L3_FRAG_OFS_GT0]	= { 66,   1},
	[VCAP_IS1_HK_IP4_L3_OPTIONS]		= { 67,   1},
	[VCAP_IS1_HK_IP4_L3_DSCP]		= { 68,   6},
	[VCAP_IS1_HK_IP4_L3_IP4_DIP]		= { 74,  32},
	[VCAP_IS1_HK_IP4_L3_IP4_SIP]		= {106,  32},
	[VCAP_IS1_HK_IP4_L3_PROTO]		= {138,   8},
	[VCAP_IS1_HK_IP4_TCP_UDP]		= {146,   1},
	[VCAP_IS1_HK_IP4_TCP]			= {147,   1},
	[VCAP_IS1_HK_IP4_L4_RNG]		= {148,   8},
	[VCAP_IS1_HK_IP4_IP_PAYLOAD_S1_5TUPLE]	= {156,  32},
};

static const struct vcap_field vsc7514_vcap_is1_actions[] = {
	[VCAP_IS1_ACT_DSCP_ENA]			= {  0,  1},
	[VCAP_IS1_ACT_DSCP_VAL]			= {  1,  6},
	[VCAP_IS1_ACT_QOS_ENA]			= {  7,  1},
	[VCAP_IS1_ACT_QOS_VAL]			= {  8,  3},
	[VCAP_IS1_ACT_DP_ENA]			= { 11,  1},
	[VCAP_IS1_ACT_DP_VAL]			= { 12,  1},
	[VCAP_IS1_ACT_PAG_OVERRIDE_MASK]	= { 13,  8},
	[VCAP_IS1_ACT_PAG_VAL]			= { 21,  8},
	[VCAP_IS1_ACT_RSV]			= { 29,  9},
	/* The fields below are incorrectly shifted by 2 in the manual */
	[VCAP_IS1_ACT_VID_REPLACE_ENA]		= { 38,  1},
	[VCAP_IS1_ACT_VID_ADD_VAL]		= { 39, 12},
	[VCAP_IS1_ACT_FID_SEL]			= { 51,  2},
	[VCAP_IS1_ACT_FID_VAL]			= { 53, 13},
	[VCAP_IS1_ACT_PCP_DEI_ENA]		= { 66,  1},
	[VCAP_IS1_ACT_PCP_VAL]			= { 67,  3},
	[VCAP_IS1_ACT_DEI_VAL]			= { 70,  1},
	[VCAP_IS1_ACT_VLAN_POP_CNT_ENA]		= { 71,  1},
	[VCAP_IS1_ACT_VLAN_POP_CNT]		= { 72,  2},
	[VCAP_IS1_ACT_CUSTOM_ACE_TYPE_ENA]	= { 74,  4},
	[VCAP_IS1_ACT_HIT_STICKY]		= { 78,  1},
};

static const struct vcap_field vsc7514_vcap_is2_keys[] = {
	/* Common: 46 bits */
	[VCAP_IS2_TYPE]				= {  0,   4},
	[VCAP_IS2_HK_FIRST]			= {  4,   1},
	[VCAP_IS2_HK_PAG]			= {  5,   8},
	[VCAP_IS2_HK_IGR_PORT_MASK]		= { 13,  12},
	[VCAP_IS2_HK_RSV2]			= { 25,   1},
	[VCAP_IS2_HK_HOST_MATCH]		= { 26,   1},
	[VCAP_IS2_HK_L2_MC]			= { 27,   1},
	[VCAP_IS2_HK_L2_BC]			= { 28,   1},
	[VCAP_IS2_HK_VLAN_TAGGED]		= { 29,   1},
	[VCAP_IS2_HK_VID]			= { 30,  12},
	[VCAP_IS2_HK_DEI]			= { 42,   1},
	[VCAP_IS2_HK_PCP]			= { 43,   3},
	/* MAC_ETYPE / MAC_LLC / MAC_SNAP / OAM common */
	[VCAP_IS2_HK_L2_DMAC]			= { 46,  48},
	[VCAP_IS2_HK_L2_SMAC]			= { 94,  48},
	/* MAC_ETYPE (TYPE=000) */
	[VCAP_IS2_HK_MAC_ETYPE_ETYPE]		= {142,  16},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD0]	= {158,  16},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD1]	= {174,   8},
	[VCAP_IS2_HK_MAC_ETYPE_L2_PAYLOAD2]	= {182,   3},
	/* MAC_LLC (TYPE=001) */
	[VCAP_IS2_HK_MAC_LLC_L2_LLC]		= {142,  40},
	/* MAC_SNAP (TYPE=010) */
	[VCAP_IS2_HK_MAC_SNAP_L2_SNAP]		= {142,  40},
	/* MAC_ARP (TYPE=011) */
	[VCAP_IS2_HK_MAC_ARP_SMAC]		= { 46,  48},
	[VCAP_IS2_HK_MAC_ARP_ADDR_SPACE_OK]	= { 94,   1},
	[VCAP_IS2_HK_MAC_ARP_PROTO_SPACE_OK]	= { 95,   1},
	[VCAP_IS2_HK_MAC_ARP_LEN_OK]		= { 96,   1},
	[VCAP_IS2_HK_MAC_ARP_TARGET_MATCH]	= { 97,   1},
	[VCAP_IS2_HK_MAC_ARP_SENDER_MATCH]	= { 98,   1},
	[VCAP_IS2_HK_MAC_ARP_OPCODE_UNKNOWN]	= { 99,   1},
	[VCAP_IS2_HK_MAC_ARP_OPCODE]		= {100,   2},
	[VCAP_IS2_HK_MAC_ARP_L3_IP4_DIP]	= {102,  32},
	[VCAP_IS2_HK_MAC_ARP_L3_IP4_SIP]	= {134,  32},
	[VCAP_IS2_HK_MAC_ARP_DIP_EQ_SIP]	= {166,   1},
	/* IP4_TCP_UDP / IP4_OTHER common */
	[VCAP_IS2_HK_IP4]			= { 46,   1},
	[VCAP_IS2_HK_L3_FRAGMENT]		= { 47,   1},
	[VCAP_IS2_HK_L3_FRAG_OFS_GT0]		= { 48,   1},
	[VCAP_IS2_HK_L3_OPTIONS]		= { 49,   1},
	[VCAP_IS2_HK_IP4_L3_TTL_GT0]		= { 50,   1},
	[VCAP_IS2_HK_L3_TOS]			= { 51,   8},
	[VCAP_IS2_HK_L3_IP4_DIP]		= { 59,  32},
	[VCAP_IS2_HK_L3_IP4_SIP]		= { 91,  32},
	[VCAP_IS2_HK_DIP_EQ_SIP]		= {123,   1},
	/* IP4_TCP_UDP (TYPE=100) */
	[VCAP_IS2_HK_TCP]			= {124,   1},
	[VCAP_IS2_HK_L4_DPORT]			= {125,  16},
	[VCAP_IS2_HK_L4_SPORT]			= {141,  16},
	[VCAP_IS2_HK_L4_RNG]			= {157,   8},
	[VCAP_IS2_HK_L4_SPORT_EQ_DPORT]		= {165,   1},
	[VCAP_IS2_HK_L4_SEQUENCE_EQ0]		= {166,   1},
	[VCAP_IS2_HK_L4_FIN]			= {167,   1},
	[VCAP_IS2_HK_L4_SYN]			= {168,   1},
	[VCAP_IS2_HK_L4_RST]			= {169,   1},
	[VCAP_IS2_HK_L4_PSH]			= {170,   1},
	[VCAP_IS2_HK_L4_ACK]			= {171,   1},
	[VCAP_IS2_HK_L4_URG]			= {172,   1},
	[VCAP_IS2_HK_L4_1588_DOM]		= {173,   8},
	[VCAP_IS2_HK_L4_1588_VER]		= {181,   4},
	/* IP4_OTHER (TYPE=101) */
	[VCAP_IS2_HK_IP4_L3_PROTO]		= {124,   8},
	[VCAP_IS2_HK_L3_PAYLOAD]		= {132,  56},
	/* IP6_STD (TYPE=110) */
	[VCAP_IS2_HK_IP6_L3_TTL_GT0]		= { 46,   1},
	[VCAP_IS2_HK_L3_IP6_SIP]		= { 47, 128},
	[VCAP_IS2_HK_IP6_L3_PROTO]		= {175,   8},
	/* OAM (TYPE=111) */
	[VCAP_IS2_HK_OAM_MEL_FLAGS]		= {142,   7},
	[VCAP_IS2_HK_OAM_VER]			= {149,   5},
	[VCAP_IS2_HK_OAM_OPCODE]		= {154,   8},
	[VCAP_IS2_HK_OAM_FLAGS]			= {162,   8},
	[VCAP_IS2_HK_OAM_MEPID]			= {170,  16},
	[VCAP_IS2_HK_OAM_CCM_CNTS_EQ0]		= {186,   1},
	[VCAP_IS2_HK_OAM_IS_Y1731]		= {187,   1},
};

static const struct vcap_field vsc7514_vcap_is2_actions[] = {
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

static const struct vcap_props vsc7514_vcap_props[] = {
	[VCAP_ES0] = {
		.action_type_width = 0,
		.action_table = {
			[ES0_ACTION_TYPE_NORMAL] = {
				.width = 73, /* HIT_STICKY not included */
				.count = 1,
			},
		},
		.target = S0,
		.keys = vsc7514_vcap_es0_keys,
		.actions = vsc7514_vcap_es0_actions,
	},
	[VCAP_IS1] = {
		.action_type_width = 0,
		.action_table = {
			[IS1_ACTION_TYPE_NORMAL] = {
				.width = 78, /* HIT_STICKY not included */
				.count = 4,
			},
		},
		.target = S1,
		.keys = vsc7514_vcap_is1_keys,
		.actions = vsc7514_vcap_is1_actions,
	},
	[VCAP_IS2] = {
		.tg_width = 2,
		.sw_count = 4,
		.entry_count = VSC7514_VCAP_IS2_CNT,
		.entry_width = VSC7514_VCAP_IS2_ENTRY_WIDTH,
		.action_count = VSC7514_VCAP_IS2_CNT +
				VSC7514_VCAP_PORT_CNT + 2,
		.action_width = 99,
		.action_type_width = 1,
		.action_table = {
			[IS2_ACTION_TYPE_NORMAL] = {
				.width = 49,
				.count = 2
			},
			[IS2_ACTION_TYPE_SMAC_SIP] = {
				.width = 6,
				.count = 4
			},
		},
		.counter_words = 4,
		.counter_width = 32,
		.target = S2,
		.keys = vsc7514_vcap_is2_keys,
		.actions = vsc7514_vcap_is2_actions,
	},
};

static struct ptp_clock_info ocelot_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "ocelot ptp",
	.max_adj	= 0x7fffffff,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= OCELOT_PTP_PINS_NUM,
	.n_pins		= OCELOT_PTP_PINS_NUM,
	.pps		= 0,
	.gettime64	= ocelot_ptp_gettime64,
	.settime64	= ocelot_ptp_settime64,
	.adjtime	= ocelot_ptp_adjtime,
	.adjfine	= ocelot_ptp_adjfine,
	.verify		= ocelot_ptp_verify,
	.enable		= ocelot_ptp_enable,
};

static void mscc_ocelot_release_ports(struct ocelot *ocelot)
{
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port_private *priv;
		struct ocelot_port *ocelot_port;

		ocelot_port = ocelot->ports[port];
		if (!ocelot_port)
			continue;

		ocelot_deinit_port(ocelot, port);

		priv = container_of(ocelot_port, struct ocelot_port_private,
				    port);

		unregister_netdev(priv->dev);
		free_netdev(priv->dev);
	}
}

static int mscc_ocelot_init_ports(struct platform_device *pdev,
				  struct device_node *ports)
{
	struct ocelot *ocelot = platform_get_drvdata(pdev);
	struct device_node *portnp;
	int err;

	ocelot->ports = devm_kcalloc(ocelot->dev, ocelot->num_phys_ports,
				     sizeof(struct ocelot_port *), GFP_KERNEL);
	if (!ocelot->ports)
		return -ENOMEM;

	for_each_available_child_of_node(ports, portnp) {
		struct ocelot_port_private *priv;
		struct ocelot_port *ocelot_port;
		struct device_node *phy_node;
		phy_interface_t phy_mode;
		struct phy_device *phy;
		struct regmap *target;
		struct resource *res;
		struct phy *serdes;
		char res_name[8];
		u32 port;

		if (of_property_read_u32(portnp, "reg", &port))
			continue;

		snprintf(res_name, sizeof(res_name), "port%d", port);

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   res_name);
		target = ocelot_regmap_init(ocelot, res);
		if (IS_ERR(target))
			continue;

		phy_node = of_parse_phandle(portnp, "phy-handle", 0);
		if (!phy_node)
			continue;

		phy = of_phy_find_device(phy_node);
		of_node_put(phy_node);
		if (!phy)
			continue;

		err = ocelot_probe_port(ocelot, port, target, phy);
		if (err) {
			of_node_put(portnp);
			return err;
		}

		ocelot_port = ocelot->ports[port];
		priv = container_of(ocelot_port, struct ocelot_port_private,
				    port);

		of_get_phy_mode(portnp, &phy_mode);

		ocelot_port->phy_mode = phy_mode;

		switch (ocelot_port->phy_mode) {
		case PHY_INTERFACE_MODE_NA:
			continue;
		case PHY_INTERFACE_MODE_SGMII:
			break;
		case PHY_INTERFACE_MODE_QSGMII:
			/* Ensure clock signals and speed is set on all
			 * QSGMII links
			 */
			ocelot_port_writel(ocelot_port,
					   DEV_CLOCK_CFG_LINK_SPEED
					   (OCELOT_SPEED_1000),
					   DEV_CLOCK_CFG);
			break;
		default:
			dev_err(ocelot->dev,
				"invalid phy mode for port%d, (Q)SGMII only\n",
				port);
			of_node_put(portnp);
			return -EINVAL;
		}

		serdes = devm_of_phy_get(ocelot->dev, portnp, NULL);
		if (IS_ERR(serdes)) {
			err = PTR_ERR(serdes);
			if (err == -EPROBE_DEFER)
				dev_dbg(ocelot->dev, "deferring probe\n");
			else
				dev_err(ocelot->dev,
					"missing SerDes phys for port%d\n",
					port);

			of_node_put(portnp);
			return err;
		}

		priv->serdes = serdes;
	}

	return 0;
}

static int mscc_ocelot_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int err, irq_xtr, irq_ptp_rdy;
	struct device_node *ports;
	struct ocelot *ocelot;
	struct regmap *hsio;
	unsigned int i;

	struct {
		enum ocelot_target id;
		char *name;
		u8 optional:1;
	} io_target[] = {
		{ SYS, "sys" },
		{ REW, "rew" },
		{ QSYS, "qsys" },
		{ ANA, "ana" },
		{ QS, "qs" },
		{ S0, "s0" },
		{ S1, "s1" },
		{ S2, "s2" },
		{ PTP, "ptp", 1 },
	};

	if (!np && !pdev->dev.platform_data)
		return -ENODEV;

	ocelot = devm_kzalloc(&pdev->dev, sizeof(*ocelot), GFP_KERNEL);
	if (!ocelot)
		return -ENOMEM;

	platform_set_drvdata(pdev, ocelot);
	ocelot->dev = &pdev->dev;

	for (i = 0; i < ARRAY_SIZE(io_target); i++) {
		struct regmap *target;
		struct resource *res;

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   io_target[i].name);

		target = ocelot_regmap_init(ocelot, res);
		if (IS_ERR(target)) {
			if (io_target[i].optional) {
				ocelot->targets[io_target[i].id] = NULL;
				continue;
			}
			return PTR_ERR(target);
		}

		ocelot->targets[io_target[i].id] = target;
	}

	hsio = syscon_regmap_lookup_by_compatible("mscc,ocelot-hsio");
	if (IS_ERR(hsio)) {
		dev_err(&pdev->dev, "missing hsio syscon\n");
		return PTR_ERR(hsio);
	}

	ocelot->targets[HSIO] = hsio;

	err = ocelot_chip_init(ocelot, &ocelot_ops);
	if (err)
		return err;

	irq_xtr = platform_get_irq_byname(pdev, "xtr");
	if (irq_xtr < 0)
		return -ENODEV;

	err = devm_request_threaded_irq(&pdev->dev, irq_xtr, NULL,
					ocelot_xtr_irq_handler, IRQF_ONESHOT,
					"frame extraction", ocelot);
	if (err)
		return err;

	irq_ptp_rdy = platform_get_irq_byname(pdev, "ptp_rdy");
	if (irq_ptp_rdy > 0 && ocelot->targets[PTP]) {
		err = devm_request_threaded_irq(&pdev->dev, irq_ptp_rdy, NULL,
						ocelot_ptp_rdy_irq_handler,
						IRQF_ONESHOT, "ptp ready",
						ocelot);
		if (err)
			return err;

		/* Both the PTP interrupt and the PTP bank are available */
		ocelot->ptp = 1;
	}

	ports = of_get_child_by_name(np, "ethernet-ports");
	if (!ports) {
		dev_err(ocelot->dev, "no ethernet-ports child node found\n");
		return -ENODEV;
	}

	ocelot->num_phys_ports = of_get_child_count(ports);

	ocelot->vcap = vsc7514_vcap_props;
	ocelot->inj_prefix = OCELOT_TAG_PREFIX_NONE;
	ocelot->xtr_prefix = OCELOT_TAG_PREFIX_NONE;
	ocelot->npi = -1;

	err = ocelot_init(ocelot);
	if (err)
		goto out_put_ports;

	err = mscc_ocelot_init_ports(pdev, ports);
	if (err)
		goto out_put_ports;

	if (ocelot->ptp) {
		err = ocelot_init_timestamp(ocelot, &ocelot_ptp_clock_info);
		if (err) {
			dev_err(ocelot->dev,
				"Timestamp initialization failed\n");
			ocelot->ptp = 0;
		}
	}

	register_netdevice_notifier(&ocelot_netdevice_nb);
	register_switchdev_notifier(&ocelot_switchdev_nb);
	register_switchdev_blocking_notifier(&ocelot_switchdev_blocking_nb);

	dev_info(&pdev->dev, "Ocelot switch probed\n");

out_put_ports:
	of_node_put(ports);
	return err;
}

static int mscc_ocelot_remove(struct platform_device *pdev)
{
	struct ocelot *ocelot = platform_get_drvdata(pdev);

	ocelot_deinit_timestamp(ocelot);
	mscc_ocelot_release_ports(ocelot);
	ocelot_deinit(ocelot);
	unregister_switchdev_blocking_notifier(&ocelot_switchdev_blocking_nb);
	unregister_switchdev_notifier(&ocelot_switchdev_nb);
	unregister_netdevice_notifier(&ocelot_netdevice_nb);

	return 0;
}

static struct platform_driver mscc_ocelot_driver = {
	.probe = mscc_ocelot_probe,
	.remove = mscc_ocelot_remove,
	.driver = {
		.name = "ocelot-switch",
		.of_match_table = mscc_ocelot_match,
	},
};

module_platform_driver(mscc_ocelot_driver);

MODULE_DESCRIPTION("Microsemi Ocelot switch driver");
MODULE_AUTHOR("Alexandre Belloni <alexandre.belloni@bootlin.com>");
MODULE_LICENSE("Dual MIT/GPL");
