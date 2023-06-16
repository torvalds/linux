// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Copyright 2017 Microsemi Corporation
 * Copyright 2018-2019 NXP
 */
#include <linux/fsl/enetc_mdio.h>
#include <soc/mscc/ocelot_qsys.h>
#include <soc/mscc/ocelot_vcap.h>
#include <soc/mscc/ocelot_ana.h>
#include <soc/mscc/ocelot_dev.h>
#include <soc/mscc/ocelot_ptp.h>
#include <soc/mscc/ocelot_sys.h>
#include <net/tc_act/tc_gate.h>
#include <soc/mscc/ocelot.h>
#include <linux/dsa/ocelot.h>
#include <linux/pcs-lynx.h>
#include <net/pkt_sched.h>
#include <linux/iopoll.h>
#include <linux/mdio.h>
#include <linux/pci.h>
#include <linux/time.h>
#include "felix.h"

#define VSC9959_NUM_PORTS		6

#define VSC9959_TAS_GCL_ENTRY_MAX	63
#define VSC9959_TAS_MIN_GATE_LEN_NS	33
#define VSC9959_VCAP_POLICER_BASE	63
#define VSC9959_VCAP_POLICER_MAX	383
#define VSC9959_SWITCH_PCI_BAR		4
#define VSC9959_IMDIO_PCI_BAR		0

#define VSC9959_PORT_MODE_SERDES	(OCELOT_PORT_MODE_SGMII | \
					 OCELOT_PORT_MODE_QSGMII | \
					 OCELOT_PORT_MODE_1000BASEX | \
					 OCELOT_PORT_MODE_2500BASEX | \
					 OCELOT_PORT_MODE_USXGMII)

static const u32 vsc9959_port_modes[VSC9959_NUM_PORTS] = {
	VSC9959_PORT_MODE_SERDES,
	VSC9959_PORT_MODE_SERDES,
	VSC9959_PORT_MODE_SERDES,
	VSC9959_PORT_MODE_SERDES,
	OCELOT_PORT_MODE_INTERNAL,
	OCELOT_PORT_MODE_INTERNAL,
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

static const u32 vsc9959_vcap_regmap[] = {
	/* VCAP_CORE_CFG */
	REG(VCAP_CORE_UPDATE_CTRL,		0x000000),
	REG(VCAP_CORE_MV_CFG,			0x000004),
	/* VCAP_CORE_CACHE */
	REG(VCAP_CACHE_ENTRY_DAT,		0x000008),
	REG(VCAP_CACHE_MASK_DAT,		0x000108),
	REG(VCAP_CACHE_ACTION_DAT,		0x000208),
	REG(VCAP_CACHE_CNT_DAT,			0x000308),
	REG(VCAP_CACHE_TG_DAT,			0x000388),
	/* VCAP_CONST */
	REG(VCAP_CONST_VCAP_VER,		0x000398),
	REG(VCAP_CONST_ENTRY_WIDTH,		0x00039c),
	REG(VCAP_CONST_ENTRY_CNT,		0x0003a0),
	REG(VCAP_CONST_ENTRY_SWCNT,		0x0003a4),
	REG(VCAP_CONST_ENTRY_TG_WIDTH,		0x0003a8),
	REG(VCAP_CONST_ACTION_DEF_CNT,		0x0003ac),
	REG(VCAP_CONST_ACTION_WIDTH,		0x0003b0),
	REG(VCAP_CONST_CNT_WIDTH,		0x0003b4),
	REG(VCAP_CONST_CORE_CNT,		0x0003b8),
	REG(VCAP_CONST_IF_CNT,			0x0003bc),
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
	REG(QSYS_CIR_CFG,			0x000000),
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
	REG(SYS_COUNT_RX_UNICAST,		0x000004),
	REG(SYS_COUNT_RX_MULTICAST,		0x000008),
	REG(SYS_COUNT_RX_BROADCAST,		0x00000c),
	REG(SYS_COUNT_RX_SHORTS,		0x000010),
	REG(SYS_COUNT_RX_FRAGMENTS,		0x000014),
	REG(SYS_COUNT_RX_JABBERS,		0x000018),
	REG(SYS_COUNT_RX_CRC_ALIGN_ERRS,	0x00001c),
	REG(SYS_COUNT_RX_SYM_ERRS,		0x000020),
	REG(SYS_COUNT_RX_64,			0x000024),
	REG(SYS_COUNT_RX_65_127,		0x000028),
	REG(SYS_COUNT_RX_128_255,		0x00002c),
	REG(SYS_COUNT_RX_256_511,		0x000030),
	REG(SYS_COUNT_RX_512_1023,		0x000034),
	REG(SYS_COUNT_RX_1024_1526,		0x000038),
	REG(SYS_COUNT_RX_1527_MAX,		0x00003c),
	REG(SYS_COUNT_RX_PAUSE,			0x000040),
	REG(SYS_COUNT_RX_CONTROL,		0x000044),
	REG(SYS_COUNT_RX_LONGS,			0x000048),
	REG(SYS_COUNT_RX_CLASSIFIED_DROPS,	0x00004c),
	REG(SYS_COUNT_RX_RED_PRIO_0,		0x000050),
	REG(SYS_COUNT_RX_RED_PRIO_1,		0x000054),
	REG(SYS_COUNT_RX_RED_PRIO_2,		0x000058),
	REG(SYS_COUNT_RX_RED_PRIO_3,		0x00005c),
	REG(SYS_COUNT_RX_RED_PRIO_4,		0x000060),
	REG(SYS_COUNT_RX_RED_PRIO_5,		0x000064),
	REG(SYS_COUNT_RX_RED_PRIO_6,		0x000068),
	REG(SYS_COUNT_RX_RED_PRIO_7,		0x00006c),
	REG(SYS_COUNT_RX_YELLOW_PRIO_0,		0x000070),
	REG(SYS_COUNT_RX_YELLOW_PRIO_1,		0x000074),
	REG(SYS_COUNT_RX_YELLOW_PRIO_2,		0x000078),
	REG(SYS_COUNT_RX_YELLOW_PRIO_3,		0x00007c),
	REG(SYS_COUNT_RX_YELLOW_PRIO_4,		0x000080),
	REG(SYS_COUNT_RX_YELLOW_PRIO_5,		0x000084),
	REG(SYS_COUNT_RX_YELLOW_PRIO_6,		0x000088),
	REG(SYS_COUNT_RX_YELLOW_PRIO_7,		0x00008c),
	REG(SYS_COUNT_RX_GREEN_PRIO_0,		0x000090),
	REG(SYS_COUNT_RX_GREEN_PRIO_1,		0x000094),
	REG(SYS_COUNT_RX_GREEN_PRIO_2,		0x000098),
	REG(SYS_COUNT_RX_GREEN_PRIO_3,		0x00009c),
	REG(SYS_COUNT_RX_GREEN_PRIO_4,		0x0000a0),
	REG(SYS_COUNT_RX_GREEN_PRIO_5,		0x0000a4),
	REG(SYS_COUNT_RX_GREEN_PRIO_6,		0x0000a8),
	REG(SYS_COUNT_RX_GREEN_PRIO_7,		0x0000ac),
	REG(SYS_COUNT_RX_ASSEMBLY_ERRS,		0x0000b0),
	REG(SYS_COUNT_RX_SMD_ERRS,		0x0000b4),
	REG(SYS_COUNT_RX_ASSEMBLY_OK,		0x0000b8),
	REG(SYS_COUNT_RX_MERGE_FRAGMENTS,	0x0000bc),
	REG(SYS_COUNT_RX_PMAC_OCTETS,		0x0000c0),
	REG(SYS_COUNT_RX_PMAC_UNICAST,		0x0000c4),
	REG(SYS_COUNT_RX_PMAC_MULTICAST,	0x0000c8),
	REG(SYS_COUNT_RX_PMAC_BROADCAST,	0x0000cc),
	REG(SYS_COUNT_RX_PMAC_SHORTS,		0x0000d0),
	REG(SYS_COUNT_RX_PMAC_FRAGMENTS,	0x0000d4),
	REG(SYS_COUNT_RX_PMAC_JABBERS,		0x0000d8),
	REG(SYS_COUNT_RX_PMAC_CRC_ALIGN_ERRS,	0x0000dc),
	REG(SYS_COUNT_RX_PMAC_SYM_ERRS,		0x0000e0),
	REG(SYS_COUNT_RX_PMAC_64,		0x0000e4),
	REG(SYS_COUNT_RX_PMAC_65_127,		0x0000e8),
	REG(SYS_COUNT_RX_PMAC_128_255,		0x0000ec),
	REG(SYS_COUNT_RX_PMAC_256_511,		0x0000f0),
	REG(SYS_COUNT_RX_PMAC_512_1023,		0x0000f4),
	REG(SYS_COUNT_RX_PMAC_1024_1526,	0x0000f8),
	REG(SYS_COUNT_RX_PMAC_1527_MAX,		0x0000fc),
	REG(SYS_COUNT_RX_PMAC_PAUSE,		0x000100),
	REG(SYS_COUNT_RX_PMAC_CONTROL,		0x000104),
	REG(SYS_COUNT_RX_PMAC_LONGS,		0x000108),
	REG(SYS_COUNT_TX_OCTETS,		0x000200),
	REG(SYS_COUNT_TX_UNICAST,		0x000204),
	REG(SYS_COUNT_TX_MULTICAST,		0x000208),
	REG(SYS_COUNT_TX_BROADCAST,		0x00020c),
	REG(SYS_COUNT_TX_COLLISION,		0x000210),
	REG(SYS_COUNT_TX_DROPS,			0x000214),
	REG(SYS_COUNT_TX_PAUSE,			0x000218),
	REG(SYS_COUNT_TX_64,			0x00021c),
	REG(SYS_COUNT_TX_65_127,		0x000220),
	REG(SYS_COUNT_TX_128_255,		0x000224),
	REG(SYS_COUNT_TX_256_511,		0x000228),
	REG(SYS_COUNT_TX_512_1023,		0x00022c),
	REG(SYS_COUNT_TX_1024_1526,		0x000230),
	REG(SYS_COUNT_TX_1527_MAX,		0x000234),
	REG(SYS_COUNT_TX_YELLOW_PRIO_0,		0x000238),
	REG(SYS_COUNT_TX_YELLOW_PRIO_1,		0x00023c),
	REG(SYS_COUNT_TX_YELLOW_PRIO_2,		0x000240),
	REG(SYS_COUNT_TX_YELLOW_PRIO_3,		0x000244),
	REG(SYS_COUNT_TX_YELLOW_PRIO_4,		0x000248),
	REG(SYS_COUNT_TX_YELLOW_PRIO_5,		0x00024c),
	REG(SYS_COUNT_TX_YELLOW_PRIO_6,		0x000250),
	REG(SYS_COUNT_TX_YELLOW_PRIO_7,		0x000254),
	REG(SYS_COUNT_TX_GREEN_PRIO_0,		0x000258),
	REG(SYS_COUNT_TX_GREEN_PRIO_1,		0x00025c),
	REG(SYS_COUNT_TX_GREEN_PRIO_2,		0x000260),
	REG(SYS_COUNT_TX_GREEN_PRIO_3,		0x000264),
	REG(SYS_COUNT_TX_GREEN_PRIO_4,		0x000268),
	REG(SYS_COUNT_TX_GREEN_PRIO_5,		0x00026c),
	REG(SYS_COUNT_TX_GREEN_PRIO_6,		0x000270),
	REG(SYS_COUNT_TX_GREEN_PRIO_7,		0x000274),
	REG(SYS_COUNT_TX_AGED,			0x000278),
	REG(SYS_COUNT_TX_MM_HOLD,		0x00027c),
	REG(SYS_COUNT_TX_MERGE_FRAGMENTS,	0x000280),
	REG(SYS_COUNT_TX_PMAC_OCTETS,		0x000284),
	REG(SYS_COUNT_TX_PMAC_UNICAST,		0x000288),
	REG(SYS_COUNT_TX_PMAC_MULTICAST,	0x00028c),
	REG(SYS_COUNT_TX_PMAC_BROADCAST,	0x000290),
	REG(SYS_COUNT_TX_PMAC_PAUSE,		0x000294),
	REG(SYS_COUNT_TX_PMAC_64,		0x000298),
	REG(SYS_COUNT_TX_PMAC_65_127,		0x00029c),
	REG(SYS_COUNT_TX_PMAC_128_255,		0x0002a0),
	REG(SYS_COUNT_TX_PMAC_256_511,		0x0002a4),
	REG(SYS_COUNT_TX_PMAC_512_1023,		0x0002a8),
	REG(SYS_COUNT_TX_PMAC_1024_1526,	0x0002ac),
	REG(SYS_COUNT_TX_PMAC_1527_MAX,		0x0002b0),
	REG(SYS_COUNT_DROP_LOCAL,		0x000400),
	REG(SYS_COUNT_DROP_TAIL,		0x000404),
	REG(SYS_COUNT_DROP_YELLOW_PRIO_0,	0x000408),
	REG(SYS_COUNT_DROP_YELLOW_PRIO_1,	0x00040c),
	REG(SYS_COUNT_DROP_YELLOW_PRIO_2,	0x000410),
	REG(SYS_COUNT_DROP_YELLOW_PRIO_3,	0x000414),
	REG(SYS_COUNT_DROP_YELLOW_PRIO_4,	0x000418),
	REG(SYS_COUNT_DROP_YELLOW_PRIO_5,	0x00041c),
	REG(SYS_COUNT_DROP_YELLOW_PRIO_6,	0x000420),
	REG(SYS_COUNT_DROP_YELLOW_PRIO_7,	0x000424),
	REG(SYS_COUNT_DROP_GREEN_PRIO_0,	0x000428),
	REG(SYS_COUNT_DROP_GREEN_PRIO_1,	0x00042c),
	REG(SYS_COUNT_DROP_GREEN_PRIO_2,	0x000430),
	REG(SYS_COUNT_DROP_GREEN_PRIO_3,	0x000434),
	REG(SYS_COUNT_DROP_GREEN_PRIO_4,	0x000438),
	REG(SYS_COUNT_DROP_GREEN_PRIO_5,	0x00043c),
	REG(SYS_COUNT_DROP_GREEN_PRIO_6,	0x000440),
	REG(SYS_COUNT_DROP_GREEN_PRIO_7,	0x000444),
	REG(SYS_COUNT_SF_MATCHING_FRAMES,	0x000800),
	REG(SYS_COUNT_SF_NOT_PASSING_FRAMES,	0x000804),
	REG(SYS_COUNT_SF_NOT_PASSING_SDU,	0x000808),
	REG(SYS_COUNT_SF_RED_FRAMES,		0x00080c),
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
	REG(PTP_PIN_CFG,			0x000000),
	REG(PTP_PIN_TOD_SEC_MSB,		0x000004),
	REG(PTP_PIN_TOD_SEC_LSB,		0x000008),
	REG(PTP_PIN_TOD_NSEC,			0x00000c),
	REG(PTP_PIN_WF_HIGH_PERIOD,		0x000014),
	REG(PTP_PIN_WF_LOW_PERIOD,		0x000018),
	REG(PTP_CFG_MISC,			0x0000a0),
	REG(PTP_CLK_CFG_ADJ_CFG,		0x0000a4),
	REG(PTP_CLK_CFG_ADJ_FREQ,		0x0000a8),
};

static const u32 vsc9959_gcb_regmap[] = {
	REG(GCB_SOFT_RST,			0x000004),
};

static const u32 vsc9959_dev_gmii_regmap[] = {
	REG(DEV_CLOCK_CFG,			0x0),
	REG(DEV_PORT_MISC,			0x4),
	REG(DEV_EVENTS,				0x8),
	REG(DEV_EEE_CFG,			0xc),
	REG(DEV_RX_PATH_DELAY,			0x10),
	REG(DEV_TX_PATH_DELAY,			0x14),
	REG(DEV_PTP_PREDICT_CFG,		0x18),
	REG(DEV_MAC_ENA_CFG,			0x1c),
	REG(DEV_MAC_MODE_CFG,			0x20),
	REG(DEV_MAC_MAXLEN_CFG,			0x24),
	REG(DEV_MAC_TAGS_CFG,			0x28),
	REG(DEV_MAC_ADV_CHK_CFG,		0x2c),
	REG(DEV_MAC_IFG_CFG,			0x30),
	REG(DEV_MAC_HDX_CFG,			0x34),
	REG(DEV_MAC_DBG_CFG,			0x38),
	REG(DEV_MAC_FC_MAC_LOW_CFG,		0x3c),
	REG(DEV_MAC_FC_MAC_HIGH_CFG,		0x40),
	REG(DEV_MAC_STICKY,			0x44),
	REG(DEV_MM_ENABLE_CONFIG,		0x48),
	REG(DEV_MM_VERIF_CONFIG,		0x4C),
	REG(DEV_MM_STATUS,			0x50),
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

static const u32 *vsc9959_regmap[TARGET_MAX] = {
	[ANA]	= vsc9959_ana_regmap,
	[QS]	= vsc9959_qs_regmap,
	[QSYS]	= vsc9959_qsys_regmap,
	[REW]	= vsc9959_rew_regmap,
	[SYS]	= vsc9959_sys_regmap,
	[S0]	= vsc9959_vcap_regmap,
	[S1]	= vsc9959_vcap_regmap,
	[S2]	= vsc9959_vcap_regmap,
	[PTP]	= vsc9959_ptp_regmap,
	[GCB]	= vsc9959_gcb_regmap,
	[DEV_GMII] = vsc9959_dev_gmii_regmap,
};

/* Addresses are relative to the PCI device's base address */
static const struct resource vsc9959_resources[] = {
	DEFINE_RES_MEM_NAMED(0x0010000, 0x0010000, "sys"),
	DEFINE_RES_MEM_NAMED(0x0030000, 0x0010000, "rew"),
	DEFINE_RES_MEM_NAMED(0x0040000, 0x0000400, "s0"),
	DEFINE_RES_MEM_NAMED(0x0050000, 0x0000400, "s1"),
	DEFINE_RES_MEM_NAMED(0x0060000, 0x0000400, "s2"),
	DEFINE_RES_MEM_NAMED(0x0070000, 0x0000200, "devcpu_gcb"),
	DEFINE_RES_MEM_NAMED(0x0080000, 0x0000100, "qs"),
	DEFINE_RES_MEM_NAMED(0x0090000, 0x00000cc, "ptp"),
	DEFINE_RES_MEM_NAMED(0x0100000, 0x0010000, "port0"),
	DEFINE_RES_MEM_NAMED(0x0110000, 0x0010000, "port1"),
	DEFINE_RES_MEM_NAMED(0x0120000, 0x0010000, "port2"),
	DEFINE_RES_MEM_NAMED(0x0130000, 0x0010000, "port3"),
	DEFINE_RES_MEM_NAMED(0x0140000, 0x0010000, "port4"),
	DEFINE_RES_MEM_NAMED(0x0150000, 0x0010000, "port5"),
	DEFINE_RES_MEM_NAMED(0x0200000, 0x0020000, "qsys"),
	DEFINE_RES_MEM_NAMED(0x0280000, 0x0010000, "ana"),
};

static const char * const vsc9959_resource_names[TARGET_MAX] = {
	[SYS] = "sys",
	[REW] = "rew",
	[S0] = "s0",
	[S1] = "s1",
	[S2] = "s2",
	[GCB] = "devcpu_gcb",
	[QS] = "qs",
	[PTP] = "ptp",
	[QSYS] = "qsys",
	[ANA] = "ana",
};

/* Port MAC 0 Internal MDIO bus through which the SerDes acting as an
 * SGMII/QSGMII MAC PCS can be found.
 */
static const struct resource vsc9959_imdio_res =
	DEFINE_RES_MEM_NAMED(0x8030, 0x10, "imdio");

static const struct reg_field vsc9959_regfields[REGFIELD_MAX] = {
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
	/* Replicated per number of ports (7), register size 4 per port */
	[QSYS_SWITCH_PORT_MODE_PORT_ENA] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 14, 14, 7, 4),
	[QSYS_SWITCH_PORT_MODE_SCH_NEXT_CFG] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 11, 13, 7, 4),
	[QSYS_SWITCH_PORT_MODE_YEL_RSRVD] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 10, 10, 7, 4),
	[QSYS_SWITCH_PORT_MODE_INGRESS_DROP_MODE] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 9, 9, 7, 4),
	[QSYS_SWITCH_PORT_MODE_TX_PFC_ENA] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 1, 8, 7, 4),
	[QSYS_SWITCH_PORT_MODE_TX_PFC_MODE] = REG_FIELD_ID(QSYS_SWITCH_PORT_MODE, 0, 0, 7, 4),
	[SYS_PORT_MODE_DATA_WO_TS] = REG_FIELD_ID(SYS_PORT_MODE, 5, 6, 7, 4),
	[SYS_PORT_MODE_INCL_INJ_HDR] = REG_FIELD_ID(SYS_PORT_MODE, 3, 4, 7, 4),
	[SYS_PORT_MODE_INCL_XTR_HDR] = REG_FIELD_ID(SYS_PORT_MODE, 1, 2, 7, 4),
	[SYS_PORT_MODE_INCL_HDR_ERR] = REG_FIELD_ID(SYS_PORT_MODE, 0, 0, 7, 4),
	[SYS_PAUSE_CFG_PAUSE_START] = REG_FIELD_ID(SYS_PAUSE_CFG, 10, 18, 7, 4),
	[SYS_PAUSE_CFG_PAUSE_STOP] = REG_FIELD_ID(SYS_PAUSE_CFG, 1, 9, 7, 4),
	[SYS_PAUSE_CFG_PAUSE_ENA] = REG_FIELD_ID(SYS_PAUSE_CFG, 0, 1, 7, 4),
};

static const struct vcap_field vsc9959_vcap_es0_keys[] = {
	[VCAP_ES0_EGR_PORT]			= {  0,  3},
	[VCAP_ES0_IGR_PORT]			= {  3,  3},
	[VCAP_ES0_RSV]				= {  6,  2},
	[VCAP_ES0_L2_MC]			= {  8,  1},
	[VCAP_ES0_L2_BC]			= {  9,  1},
	[VCAP_ES0_VID]				= { 10, 12},
	[VCAP_ES0_DP]				= { 22,  1},
	[VCAP_ES0_PCP]				= { 23,  3},
};

static const struct vcap_field vsc9959_vcap_es0_actions[] = {
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
	[VCAP_ES0_ACT_RSV]			= { 49, 23},
	[VCAP_ES0_ACT_HIT_STICKY]		= { 72,  1},
};

static const struct vcap_field vsc9959_vcap_is1_keys[] = {
	[VCAP_IS1_HK_TYPE]			= {  0,   1},
	[VCAP_IS1_HK_LOOKUP]			= {  1,   2},
	[VCAP_IS1_HK_IGR_PORT_MASK]		= {  3,   7},
	[VCAP_IS1_HK_RSV]			= { 10,   9},
	[VCAP_IS1_HK_OAM_Y1731]			= { 19,   1},
	[VCAP_IS1_HK_L2_MC]			= { 20,   1},
	[VCAP_IS1_HK_L2_BC]			= { 21,   1},
	[VCAP_IS1_HK_IP_MC]			= { 22,   1},
	[VCAP_IS1_HK_VLAN_TAGGED]		= { 23,   1},
	[VCAP_IS1_HK_VLAN_DBL_TAGGED]		= { 24,   1},
	[VCAP_IS1_HK_TPID]			= { 25,   1},
	[VCAP_IS1_HK_VID]			= { 26,  12},
	[VCAP_IS1_HK_DEI]			= { 38,   1},
	[VCAP_IS1_HK_PCP]			= { 39,   3},
	/* Specific Fields for IS1 Half Key S1_NORMAL */
	[VCAP_IS1_HK_L2_SMAC]			= { 42,  48},
	[VCAP_IS1_HK_ETYPE_LEN]			= { 90,   1},
	[VCAP_IS1_HK_ETYPE]			= { 91,  16},
	[VCAP_IS1_HK_IP_SNAP]			= {107,   1},
	[VCAP_IS1_HK_IP4]			= {108,   1},
	/* Layer-3 Information */
	[VCAP_IS1_HK_L3_FRAGMENT]		= {109,   1},
	[VCAP_IS1_HK_L3_FRAG_OFS_GT0]		= {110,   1},
	[VCAP_IS1_HK_L3_OPTIONS]		= {111,   1},
	[VCAP_IS1_HK_L3_DSCP]			= {112,   6},
	[VCAP_IS1_HK_L3_IP4_SIP]		= {118,  32},
	/* Layer-4 Information */
	[VCAP_IS1_HK_TCP_UDP]			= {150,   1},
	[VCAP_IS1_HK_TCP]			= {151,   1},
	[VCAP_IS1_HK_L4_SPORT]			= {152,  16},
	[VCAP_IS1_HK_L4_RNG]			= {168,   8},
	/* Specific Fields for IS1 Half Key S1_5TUPLE_IP4 */
	[VCAP_IS1_HK_IP4_INNER_TPID]            = { 42,   1},
	[VCAP_IS1_HK_IP4_INNER_VID]		= { 43,  12},
	[VCAP_IS1_HK_IP4_INNER_DEI]		= { 55,   1},
	[VCAP_IS1_HK_IP4_INNER_PCP]		= { 56,   3},
	[VCAP_IS1_HK_IP4_IP4]			= { 59,   1},
	[VCAP_IS1_HK_IP4_L3_FRAGMENT]		= { 60,   1},
	[VCAP_IS1_HK_IP4_L3_FRAG_OFS_GT0]	= { 61,   1},
	[VCAP_IS1_HK_IP4_L3_OPTIONS]		= { 62,   1},
	[VCAP_IS1_HK_IP4_L3_DSCP]		= { 63,   6},
	[VCAP_IS1_HK_IP4_L3_IP4_DIP]		= { 69,  32},
	[VCAP_IS1_HK_IP4_L3_IP4_SIP]		= {101,  32},
	[VCAP_IS1_HK_IP4_L3_PROTO]		= {133,   8},
	[VCAP_IS1_HK_IP4_TCP_UDP]		= {141,   1},
	[VCAP_IS1_HK_IP4_TCP]			= {142,   1},
	[VCAP_IS1_HK_IP4_L4_RNG]		= {143,   8},
	[VCAP_IS1_HK_IP4_IP_PAYLOAD_S1_5TUPLE]	= {151,  32},
};

static const struct vcap_field vsc9959_vcap_is1_actions[] = {
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

static struct vcap_field vsc9959_vcap_is2_keys[] = {
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
	[VCAP_IS2_HK_L4_DPORT]			= {120,  16},
	[VCAP_IS2_HK_L4_SPORT]			= {136,  16},
	[VCAP_IS2_HK_L4_RNG]			= {152,   8},
	[VCAP_IS2_HK_L4_SPORT_EQ_DPORT]		= {160,   1},
	[VCAP_IS2_HK_L4_SEQUENCE_EQ0]		= {161,   1},
	[VCAP_IS2_HK_L4_FIN]			= {162,   1},
	[VCAP_IS2_HK_L4_SYN]			= {163,   1},
	[VCAP_IS2_HK_L4_RST]			= {164,   1},
	[VCAP_IS2_HK_L4_PSH]			= {165,   1},
	[VCAP_IS2_HK_L4_ACK]			= {166,   1},
	[VCAP_IS2_HK_L4_URG]			= {167,   1},
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

static struct vcap_field vsc9959_vcap_is2_actions[] = {
	[VCAP_IS2_ACT_HIT_ME_ONCE]		= {  0,  1},
	[VCAP_IS2_ACT_CPU_COPY_ENA]		= {  1,  1},
	[VCAP_IS2_ACT_CPU_QU_NUM]		= {  2,  3},
	[VCAP_IS2_ACT_MASK_MODE]		= {  5,  2},
	[VCAP_IS2_ACT_MIRROR_ENA]		= {  7,  1},
	[VCAP_IS2_ACT_LRN_DIS]			= {  8,  1},
	[VCAP_IS2_ACT_POLICE_ENA]		= {  9,  1},
	[VCAP_IS2_ACT_POLICE_IDX]		= { 10,  9},
	[VCAP_IS2_ACT_POLICE_VCAP_ONLY]		= { 19,  1},
	[VCAP_IS2_ACT_PORT_MASK]		= { 20,  6},
	[VCAP_IS2_ACT_REW_OP]			= { 26,  9},
	[VCAP_IS2_ACT_SMAC_REPLACE_ENA]		= { 35,  1},
	[VCAP_IS2_ACT_RSV]			= { 36,  2},
	[VCAP_IS2_ACT_ACL_ID]			= { 38,  6},
	[VCAP_IS2_ACT_HIT_CNT]			= { 44, 32},
};

static struct vcap_props vsc9959_vcap_props[] = {
	[VCAP_ES0] = {
		.action_type_width = 0,
		.action_table = {
			[ES0_ACTION_TYPE_NORMAL] = {
				.width = 72, /* HIT_STICKY not included */
				.count = 1,
			},
		},
		.target = S0,
		.keys = vsc9959_vcap_es0_keys,
		.actions = vsc9959_vcap_es0_actions,
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
		.keys = vsc9959_vcap_is1_keys,
		.actions = vsc9959_vcap_is1_actions,
	},
	[VCAP_IS2] = {
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
		.target = S2,
		.keys = vsc9959_vcap_is2_keys,
		.actions = vsc9959_vcap_is2_actions,
	},
};

static const struct ptp_clock_info vsc9959_ptp_caps = {
	.owner		= THIS_MODULE,
	.name		= "felix ptp",
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

#define VSC9959_INIT_TIMEOUT			50000
#define VSC9959_GCB_RST_SLEEP			100
#define VSC9959_SYS_RAMINIT_SLEEP		80

static int vsc9959_gcb_soft_rst_status(struct ocelot *ocelot)
{
	int val;

	ocelot_field_read(ocelot, GCB_SOFT_RST_SWC_RST, &val);

	return val;
}

static int vsc9959_sys_ram_init_status(struct ocelot *ocelot)
{
	return ocelot_read(ocelot, SYS_RAM_INIT);
}

/* CORE_ENA is in SYS:SYSTEM:RESET_CFG
 * RAM_INIT is in SYS:RAM_CTRL:RAM_INIT
 */
static int vsc9959_reset(struct ocelot *ocelot)
{
	int val, err;

	/* soft-reset the switch core */
	ocelot_field_write(ocelot, GCB_SOFT_RST_SWC_RST, 1);

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
	ocelot_field_write(ocelot, SYS_RESET_CFG_CORE_ENA, 1);

	return 0;
}

/* Watermark encode
 * Bit 8:   Unit; 0:1, 1:16
 * Bit 7-0: Value to be multiplied with unit
 */
static u16 vsc9959_wm_enc(u16 value)
{
	WARN_ON(value >= 16 * BIT(8));

	if (value >= BIT(8))
		return BIT(8) | (value / 16);

	return value;
}

static u16 vsc9959_wm_dec(u16 wm)
{
	WARN_ON(wm & ~GENMASK(8, 0));

	if (wm & BIT(8))
		return (wm & GENMASK(7, 0)) * 16;

	return wm;
}

static void vsc9959_wm_stat(u32 val, u32 *inuse, u32 *maxuse)
{
	*inuse = (val & GENMASK(23, 12)) >> 12;
	*maxuse = val & GENMASK(11, 0);
}

static int vsc9959_mdio_bus_alloc(struct ocelot *ocelot)
{
	struct pci_dev *pdev = to_pci_dev(ocelot->dev);
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
				  sizeof(struct phylink_pcs *),
				  GFP_KERNEL);
	if (!felix->pcs) {
		dev_err(dev, "failed to allocate array for PCS PHYs\n");
		return -ENOMEM;
	}

	imdio_base = pci_resource_start(pdev, VSC9959_IMDIO_PCI_BAR);

	memcpy(&res, &vsc9959_imdio_res, sizeof(res));
	res.start += imdio_base;
	res.end += imdio_base;

	imdio_regs = devm_ioremap_resource(dev, &res);
	if (IS_ERR(imdio_regs))
		return PTR_ERR(imdio_regs);

	hw = enetc_hw_alloc(dev, imdio_regs);
	if (IS_ERR(hw)) {
		dev_err(dev, "failed to allocate ENETC HW structure\n");
		return PTR_ERR(hw);
	}

	bus = mdiobus_alloc_size(sizeof(*mdio_priv));
	if (!bus)
		return -ENOMEM;

	bus->name = "VSC9959 internal MDIO bus";
	bus->read = enetc_mdio_read_c22;
	bus->write = enetc_mdio_write_c22;
	bus->read_c45 = enetc_mdio_read_c45;
	bus->write_c45 = enetc_mdio_write_c45;
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
		mdiobus_free(bus);
		return rc;
	}

	felix->imdio = bus;

	for (port = 0; port < felix->info->num_ports; port++) {
		struct ocelot_port *ocelot_port = ocelot->ports[port];
		struct phylink_pcs *phylink_pcs;

		if (dsa_is_unused_port(felix->ds, port))
			continue;

		if (ocelot_port->phy_mode == PHY_INTERFACE_MODE_INTERNAL)
			continue;

		phylink_pcs = lynx_pcs_create_mdiodev(felix->imdio, port);
		if (IS_ERR(phylink_pcs))
			continue;

		felix->pcs[port] = phylink_pcs;

		dev_info(dev, "Found PCS at internal MDIO address %d\n", port);
	}

	return 0;
}

static void vsc9959_mdio_bus_free(struct ocelot *ocelot)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct phylink_pcs *phylink_pcs = felix->pcs[port];

		if (phylink_pcs)
			lynx_pcs_destroy(phylink_pcs);
	}
	mdiobus_unregister(felix->imdio);
	mdiobus_free(felix->imdio);
}

/* The switch considers any frame (regardless of size) as eligible for
 * transmission if the traffic class gate is open for at least 33 ns.
 * Overruns are prevented by cropping an interval at the end of the gate time
 * slot for which egress scheduling is blocked, but we need to still keep 33 ns
 * available for one packet to be transmitted, otherwise the port tc will hang.
 * This function returns the size of a gate interval that remains available for
 * setting the guard band, after reserving the space for one egress frame.
 */
static u64 vsc9959_tas_remaining_gate_len_ps(u64 gate_len_ns)
{
	/* Gate always open */
	if (gate_len_ns == U64_MAX)
		return U64_MAX;

	return (gate_len_ns - VSC9959_TAS_MIN_GATE_LEN_NS) * PSEC_PER_NSEC;
}

/* Extract shortest continuous gate open intervals in ns for each traffic class
 * of a cyclic tc-taprio schedule. If a gate is always open, the duration is
 * considered U64_MAX. If the gate is always closed, it is considered 0.
 */
static void vsc9959_tas_min_gate_lengths(struct tc_taprio_qopt_offload *taprio,
					 u64 min_gate_len[OCELOT_NUM_TC])
{
	struct tc_taprio_sched_entry *entry;
	u64 gate_len[OCELOT_NUM_TC];
	u8 gates_ever_opened = 0;
	int tc, i, n;

	/* Initialize arrays */
	for (tc = 0; tc < OCELOT_NUM_TC; tc++) {
		min_gate_len[tc] = U64_MAX;
		gate_len[tc] = 0;
	}

	/* If we don't have taprio, consider all gates as permanently open */
	if (!taprio)
		return;

	n = taprio->num_entries;

	/* Walk through the gate list twice to determine the length
	 * of consecutively open gates for a traffic class, including
	 * open gates that wrap around. We are just interested in the
	 * minimum window size, and this doesn't change what the
	 * minimum is (if the gate never closes, min_gate_len will
	 * remain U64_MAX).
	 */
	for (i = 0; i < 2 * n; i++) {
		entry = &taprio->entries[i % n];

		for (tc = 0; tc < OCELOT_NUM_TC; tc++) {
			if (entry->gate_mask & BIT(tc)) {
				gate_len[tc] += entry->interval;
				gates_ever_opened |= BIT(tc);
			} else {
				/* Gate closes now, record a potential new
				 * minimum and reinitialize length
				 */
				if (min_gate_len[tc] > gate_len[tc] &&
				    gate_len[tc])
					min_gate_len[tc] = gate_len[tc];
				gate_len[tc] = 0;
			}
		}
	}

	/* min_gate_len[tc] actually tracks minimum *open* gate time, so for
	 * permanently closed gates, min_gate_len[tc] will still be U64_MAX.
	 * Therefore they are currently indistinguishable from permanently
	 * open gates. Overwrite the gate len with 0 when we know they're
	 * actually permanently closed, i.e. after the loop above.
	 */
	for (tc = 0; tc < OCELOT_NUM_TC; tc++)
		if (!(gates_ever_opened & BIT(tc)))
			min_gate_len[tc] = 0;
}

/* ocelot_write_rix is a macro that concatenates QSYS_MAXSDU_CFG_* with _RSZ,
 * so we need to spell out the register access to each traffic class in helper
 * functions, to simplify callers
 */
static void vsc9959_port_qmaxsdu_set(struct ocelot *ocelot, int port, int tc,
				     u32 max_sdu)
{
	switch (tc) {
	case 0:
		ocelot_write_rix(ocelot, max_sdu, QSYS_QMAXSDU_CFG_0,
				 port);
		break;
	case 1:
		ocelot_write_rix(ocelot, max_sdu, QSYS_QMAXSDU_CFG_1,
				 port);
		break;
	case 2:
		ocelot_write_rix(ocelot, max_sdu, QSYS_QMAXSDU_CFG_2,
				 port);
		break;
	case 3:
		ocelot_write_rix(ocelot, max_sdu, QSYS_QMAXSDU_CFG_3,
				 port);
		break;
	case 4:
		ocelot_write_rix(ocelot, max_sdu, QSYS_QMAXSDU_CFG_4,
				 port);
		break;
	case 5:
		ocelot_write_rix(ocelot, max_sdu, QSYS_QMAXSDU_CFG_5,
				 port);
		break;
	case 6:
		ocelot_write_rix(ocelot, max_sdu, QSYS_QMAXSDU_CFG_6,
				 port);
		break;
	case 7:
		ocelot_write_rix(ocelot, max_sdu, QSYS_QMAXSDU_CFG_7,
				 port);
		break;
	}
}

static u32 vsc9959_port_qmaxsdu_get(struct ocelot *ocelot, int port, int tc)
{
	switch (tc) {
	case 0: return ocelot_read_rix(ocelot, QSYS_QMAXSDU_CFG_0, port);
	case 1: return ocelot_read_rix(ocelot, QSYS_QMAXSDU_CFG_1, port);
	case 2: return ocelot_read_rix(ocelot, QSYS_QMAXSDU_CFG_2, port);
	case 3: return ocelot_read_rix(ocelot, QSYS_QMAXSDU_CFG_3, port);
	case 4: return ocelot_read_rix(ocelot, QSYS_QMAXSDU_CFG_4, port);
	case 5: return ocelot_read_rix(ocelot, QSYS_QMAXSDU_CFG_5, port);
	case 6: return ocelot_read_rix(ocelot, QSYS_QMAXSDU_CFG_6, port);
	case 7: return ocelot_read_rix(ocelot, QSYS_QMAXSDU_CFG_7, port);
	default:
		return 0;
	}
}

static u32 vsc9959_tas_tc_max_sdu(struct tc_taprio_qopt_offload *taprio, int tc)
{
	if (!taprio || !taprio->max_sdu[tc])
		return 0;

	return taprio->max_sdu[tc] + ETH_HLEN + 2 * VLAN_HLEN + ETH_FCS_LEN;
}

/* Update QSYS_PORT_MAX_SDU to make sure the static guard bands added by the
 * switch (see the ALWAYS_GUARD_BAND_SCH_Q comment) are correct at all MTU
 * values (the default value is 1518). Also, for traffic class windows smaller
 * than one MTU sized frame, update QSYS_QMAXSDU_CFG to enable oversized frame
 * dropping, such that these won't hang the port, as they will never be sent.
 */
static void vsc9959_tas_guard_bands_update(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct tc_taprio_qopt_offload *taprio;
	u64 min_gate_len[OCELOT_NUM_TC];
	int speed, picos_per_byte;
	u64 needed_bit_time_ps;
	u32 val, maxlen;
	u8 tas_speed;
	int tc;

	lockdep_assert_held(&ocelot->tas_lock);

	taprio = ocelot_port->taprio;

	val = ocelot_read_rix(ocelot, QSYS_TAG_CONFIG, port);
	tas_speed = QSYS_TAG_CONFIG_LINK_SPEED_X(val);

	switch (tas_speed) {
	case OCELOT_SPEED_10:
		speed = SPEED_10;
		break;
	case OCELOT_SPEED_100:
		speed = SPEED_100;
		break;
	case OCELOT_SPEED_1000:
		speed = SPEED_1000;
		break;
	case OCELOT_SPEED_2500:
		speed = SPEED_2500;
		break;
	default:
		return;
	}

	picos_per_byte = (USEC_PER_SEC * 8) / speed;

	val = ocelot_port_readl(ocelot_port, DEV_MAC_MAXLEN_CFG);
	/* MAXLEN_CFG accounts automatically for VLAN. We need to include it
	 * manually in the bit time calculation, plus the preamble and SFD.
	 */
	maxlen = val + 2 * VLAN_HLEN;
	/* Consider the standard Ethernet overhead of 8 octets preamble+SFD,
	 * 4 octets FCS, 12 octets IFG.
	 */
	needed_bit_time_ps = (u64)(maxlen + 24) * picos_per_byte;

	dev_dbg(ocelot->dev,
		"port %d: max frame size %d needs %llu ps at speed %d\n",
		port, maxlen, needed_bit_time_ps, speed);

	vsc9959_tas_min_gate_lengths(taprio, min_gate_len);

	mutex_lock(&ocelot->fwd_domain_lock);

	for (tc = 0; tc < OCELOT_NUM_TC; tc++) {
		u32 requested_max_sdu = vsc9959_tas_tc_max_sdu(taprio, tc);
		u64 remaining_gate_len_ps;
		u32 max_sdu;

		remaining_gate_len_ps =
			vsc9959_tas_remaining_gate_len_ps(min_gate_len[tc]);

		if (remaining_gate_len_ps > needed_bit_time_ps) {
			/* Setting QMAXSDU_CFG to 0 disables oversized frame
			 * dropping.
			 */
			max_sdu = requested_max_sdu;
			dev_dbg(ocelot->dev,
				"port %d tc %d min gate len %llu"
				", sending all frames\n",
				port, tc, min_gate_len[tc]);
		} else {
			/* If traffic class doesn't support a full MTU sized
			 * frame, make sure to enable oversize frame dropping
			 * for frames larger than the smallest that would fit.
			 *
			 * However, the exact same register, QSYS_QMAXSDU_CFG_*,
			 * controls not only oversized frame dropping, but also
			 * per-tc static guard band lengths, so it reduces the
			 * useful gate interval length. Therefore, be careful
			 * to calculate a guard band (and therefore max_sdu)
			 * that still leaves 33 ns available in the time slot.
			 */
			max_sdu = div_u64(remaining_gate_len_ps, picos_per_byte);
			/* A TC gate may be completely closed, which is a
			 * special case where all packets are oversized.
			 * Any limit smaller than 64 octets accomplishes this
			 */
			if (!max_sdu)
				max_sdu = 1;
			/* Take L1 overhead into account, but just don't allow
			 * max_sdu to go negative or to 0. Here we use 20
			 * because QSYS_MAXSDU_CFG_* already counts the 4 FCS
			 * octets as part of packet size.
			 */
			if (max_sdu > 20)
				max_sdu -= 20;

			if (requested_max_sdu && requested_max_sdu < max_sdu)
				max_sdu = requested_max_sdu;

			dev_info(ocelot->dev,
				 "port %d tc %d min gate length %llu"
				 " ns not enough for max frame size %d at %d"
				 " Mbps, dropping frames over %d"
				 " octets including FCS\n",
				 port, tc, min_gate_len[tc], maxlen, speed,
				 max_sdu);
		}

		vsc9959_port_qmaxsdu_set(ocelot, port, tc, max_sdu);
	}

	ocelot_write_rix(ocelot, maxlen, QSYS_PORT_MAX_SDU, port);

	ocelot->ops->cut_through_fwd(ocelot);

	mutex_unlock(&ocelot->fwd_domain_lock);
}

static void vsc9959_sched_speed_set(struct ocelot *ocelot, int port,
				    u32 speed)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	u8 tas_speed;

	switch (speed) {
	case SPEED_10:
		tas_speed = OCELOT_SPEED_10;
		break;
	case SPEED_100:
		tas_speed = OCELOT_SPEED_100;
		break;
	case SPEED_1000:
		tas_speed = OCELOT_SPEED_1000;
		break;
	case SPEED_2500:
		tas_speed = OCELOT_SPEED_2500;
		break;
	default:
		tas_speed = OCELOT_SPEED_1000;
		break;
	}

	mutex_lock(&ocelot->tas_lock);

	ocelot_rmw_rix(ocelot,
		       QSYS_TAG_CONFIG_LINK_SPEED(tas_speed),
		       QSYS_TAG_CONFIG_LINK_SPEED_M,
		       QSYS_TAG_CONFIG, port);

	if (ocelot_port->taprio)
		vsc9959_tas_guard_bands_update(ocelot, port);

	mutex_unlock(&ocelot->tas_lock);
}

static void vsc9959_new_base_time(struct ocelot *ocelot, ktime_t base_time,
				  u64 cycle_time,
				  struct timespec64 *new_base_ts)
{
	struct timespec64 ts;
	ktime_t new_base_time;
	ktime_t current_time;

	ocelot_ptp_gettime64(&ocelot->ptp_info, &ts);
	current_time = timespec64_to_ktime(ts);
	new_base_time = base_time;

	if (base_time < current_time) {
		u64 nr_of_cycles = current_time - base_time;

		do_div(nr_of_cycles, cycle_time);
		new_base_time += cycle_time * (nr_of_cycles + 1);
	}

	*new_base_ts = ktime_to_timespec64(new_base_time);
}

static u32 vsc9959_tas_read_cfg_status(struct ocelot *ocelot)
{
	return ocelot_read(ocelot, QSYS_TAS_PARAM_CFG_CTRL);
}

static void vsc9959_tas_gcl_set(struct ocelot *ocelot, const u32 gcl_ix,
				struct tc_taprio_sched_entry *entry)
{
	ocelot_write(ocelot,
		     QSYS_GCL_CFG_REG_1_GCL_ENTRY_NUM(gcl_ix) |
		     QSYS_GCL_CFG_REG_1_GATE_STATE(entry->gate_mask),
		     QSYS_GCL_CFG_REG_1);
	ocelot_write(ocelot, entry->interval, QSYS_GCL_CFG_REG_2);
}

static int vsc9959_qos_port_tas_set(struct ocelot *ocelot, int port,
				    struct tc_taprio_qopt_offload *taprio)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct timespec64 base_ts;
	int ret, i;
	u32 val;

	mutex_lock(&ocelot->tas_lock);

	if (taprio->cmd == TAPRIO_CMD_DESTROY) {
		ocelot_port_mqprio(ocelot, port, &taprio->mqprio);
		ocelot_rmw_rix(ocelot, 0, QSYS_TAG_CONFIG_ENABLE,
			       QSYS_TAG_CONFIG, port);

		taprio_offload_free(ocelot_port->taprio);
		ocelot_port->taprio = NULL;

		vsc9959_tas_guard_bands_update(ocelot, port);

		mutex_unlock(&ocelot->tas_lock);
		return 0;
	} else if (taprio->cmd != TAPRIO_CMD_REPLACE) {
		ret = -EOPNOTSUPP;
		goto err_unlock;
	}

	ret = ocelot_port_mqprio(ocelot, port, &taprio->mqprio);
	if (ret)
		goto err_unlock;

	if (taprio->cycle_time > NSEC_PER_SEC ||
	    taprio->cycle_time_extension >= NSEC_PER_SEC) {
		ret = -EINVAL;
		goto err_reset_tc;
	}

	if (taprio->num_entries > VSC9959_TAS_GCL_ENTRY_MAX) {
		ret = -ERANGE;
		goto err_reset_tc;
	}

	/* Enable guard band. The switch will schedule frames without taking
	 * their length into account. Thus we'll always need to enable the
	 * guard band which reserves the time of a maximum sized frame at the
	 * end of the time window.
	 *
	 * Although the ALWAYS_GUARD_BAND_SCH_Q bit is global for all ports, we
	 * need to set PORT_NUM, because subsequent writes to PARAM_CFG_REG_n
	 * operate on the port number.
	 */
	ocelot_rmw(ocelot, QSYS_TAS_PARAM_CFG_CTRL_PORT_NUM(port) |
		   QSYS_TAS_PARAM_CFG_CTRL_ALWAYS_GUARD_BAND_SCH_Q,
		   QSYS_TAS_PARAM_CFG_CTRL_PORT_NUM_M |
		   QSYS_TAS_PARAM_CFG_CTRL_ALWAYS_GUARD_BAND_SCH_Q,
		   QSYS_TAS_PARAM_CFG_CTRL);

	/* Hardware errata -  Admin config could not be overwritten if
	 * config is pending, need reset the TAS module
	 */
	val = ocelot_read(ocelot, QSYS_PARAM_STATUS_REG_8);
	if (val & QSYS_PARAM_STATUS_REG_8_CONFIG_PENDING) {
		ret = -EBUSY;
		goto err_reset_tc;
	}

	ocelot_rmw_rix(ocelot,
		       QSYS_TAG_CONFIG_ENABLE |
		       QSYS_TAG_CONFIG_INIT_GATE_STATE(0xFF) |
		       QSYS_TAG_CONFIG_SCH_TRAFFIC_QUEUES(0xFF),
		       QSYS_TAG_CONFIG_ENABLE |
		       QSYS_TAG_CONFIG_INIT_GATE_STATE_M |
		       QSYS_TAG_CONFIG_SCH_TRAFFIC_QUEUES_M,
		       QSYS_TAG_CONFIG, port);

	vsc9959_new_base_time(ocelot, taprio->base_time,
			      taprio->cycle_time, &base_ts);
	ocelot_write(ocelot, base_ts.tv_nsec, QSYS_PARAM_CFG_REG_1);
	ocelot_write(ocelot, lower_32_bits(base_ts.tv_sec), QSYS_PARAM_CFG_REG_2);
	val = upper_32_bits(base_ts.tv_sec);
	ocelot_write(ocelot,
		     QSYS_PARAM_CFG_REG_3_BASE_TIME_SEC_MSB(val) |
		     QSYS_PARAM_CFG_REG_3_LIST_LENGTH(taprio->num_entries),
		     QSYS_PARAM_CFG_REG_3);
	ocelot_write(ocelot, taprio->cycle_time, QSYS_PARAM_CFG_REG_4);
	ocelot_write(ocelot, taprio->cycle_time_extension, QSYS_PARAM_CFG_REG_5);

	for (i = 0; i < taprio->num_entries; i++)
		vsc9959_tas_gcl_set(ocelot, i, &taprio->entries[i]);

	ocelot_rmw(ocelot, QSYS_TAS_PARAM_CFG_CTRL_CONFIG_CHANGE,
		   QSYS_TAS_PARAM_CFG_CTRL_CONFIG_CHANGE,
		   QSYS_TAS_PARAM_CFG_CTRL);

	ret = readx_poll_timeout(vsc9959_tas_read_cfg_status, ocelot, val,
				 !(val & QSYS_TAS_PARAM_CFG_CTRL_CONFIG_CHANGE),
				 10, 100000);
	if (ret)
		goto err_reset_tc;

	ocelot_port->taprio = taprio_offload_get(taprio);
	vsc9959_tas_guard_bands_update(ocelot, port);

	mutex_unlock(&ocelot->tas_lock);

	return 0;

err_reset_tc:
	taprio->mqprio.qopt.num_tc = 0;
	ocelot_port_mqprio(ocelot, port, &taprio->mqprio);
err_unlock:
	mutex_unlock(&ocelot->tas_lock);

	return ret;
}

static void vsc9959_tas_clock_adjust(struct ocelot *ocelot)
{
	struct tc_taprio_qopt_offload *taprio;
	struct ocelot_port *ocelot_port;
	struct timespec64 base_ts;
	int port;
	u32 val;

	mutex_lock(&ocelot->tas_lock);

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		ocelot_port = ocelot->ports[port];
		taprio = ocelot_port->taprio;
		if (!taprio)
			continue;

		ocelot_rmw(ocelot,
			   QSYS_TAS_PARAM_CFG_CTRL_PORT_NUM(port),
			   QSYS_TAS_PARAM_CFG_CTRL_PORT_NUM_M,
			   QSYS_TAS_PARAM_CFG_CTRL);

		/* Disable time-aware shaper */
		ocelot_rmw_rix(ocelot, 0, QSYS_TAG_CONFIG_ENABLE,
			       QSYS_TAG_CONFIG, port);

		vsc9959_new_base_time(ocelot, taprio->base_time,
				      taprio->cycle_time, &base_ts);

		ocelot_write(ocelot, base_ts.tv_nsec, QSYS_PARAM_CFG_REG_1);
		ocelot_write(ocelot, lower_32_bits(base_ts.tv_sec),
			     QSYS_PARAM_CFG_REG_2);
		val = upper_32_bits(base_ts.tv_sec);
		ocelot_rmw(ocelot,
			   QSYS_PARAM_CFG_REG_3_BASE_TIME_SEC_MSB(val),
			   QSYS_PARAM_CFG_REG_3_BASE_TIME_SEC_MSB_M,
			   QSYS_PARAM_CFG_REG_3);

		ocelot_rmw(ocelot, QSYS_TAS_PARAM_CFG_CTRL_CONFIG_CHANGE,
			   QSYS_TAS_PARAM_CFG_CTRL_CONFIG_CHANGE,
			   QSYS_TAS_PARAM_CFG_CTRL);

		/* Re-enable time-aware shaper */
		ocelot_rmw_rix(ocelot, QSYS_TAG_CONFIG_ENABLE,
			       QSYS_TAG_CONFIG_ENABLE,
			       QSYS_TAG_CONFIG, port);
	}
	mutex_unlock(&ocelot->tas_lock);
}

static int vsc9959_qos_port_cbs_set(struct dsa_switch *ds, int port,
				    struct tc_cbs_qopt_offload *cbs_qopt)
{
	struct ocelot *ocelot = ds->priv;
	int port_ix = port * 8 + cbs_qopt->queue;
	u32 rate, burst;

	if (cbs_qopt->queue >= ds->num_tx_queues)
		return -EINVAL;

	if (!cbs_qopt->enable) {
		ocelot_write_gix(ocelot, QSYS_CIR_CFG_CIR_RATE(0) |
				 QSYS_CIR_CFG_CIR_BURST(0),
				 QSYS_CIR_CFG, port_ix);

		ocelot_rmw_gix(ocelot, 0, QSYS_SE_CFG_SE_AVB_ENA,
			       QSYS_SE_CFG, port_ix);

		return 0;
	}

	/* Rate unit is 100 kbps */
	rate = DIV_ROUND_UP(cbs_qopt->idleslope, 100);
	/* Avoid using zero rate */
	rate = clamp_t(u32, rate, 1, GENMASK(14, 0));
	/* Burst unit is 4kB */
	burst = DIV_ROUND_UP(cbs_qopt->hicredit, 4096);
	/* Avoid using zero burst size */
	burst = clamp_t(u32, burst, 1, GENMASK(5, 0));
	ocelot_write_gix(ocelot,
			 QSYS_CIR_CFG_CIR_RATE(rate) |
			 QSYS_CIR_CFG_CIR_BURST(burst),
			 QSYS_CIR_CFG,
			 port_ix);

	ocelot_rmw_gix(ocelot,
		       QSYS_SE_CFG_SE_FRM_MODE(0) |
		       QSYS_SE_CFG_SE_AVB_ENA,
		       QSYS_SE_CFG_SE_AVB_ENA |
		       QSYS_SE_CFG_SE_FRM_MODE_M,
		       QSYS_SE_CFG,
		       port_ix);

	return 0;
}

static int vsc9959_qos_query_caps(struct tc_query_caps_base *base)
{
	switch (base->type) {
	case TC_SETUP_QDISC_MQPRIO: {
		struct tc_mqprio_caps *caps = base->caps;

		caps->validate_queue_counts = true;

		return 0;
	}
	case TC_SETUP_QDISC_TAPRIO: {
		struct tc_taprio_caps *caps = base->caps;

		caps->supports_queue_max_sdu = true;

		return 0;
	}
	default:
		return -EOPNOTSUPP;
	}
}

static int vsc9959_port_setup_tc(struct dsa_switch *ds, int port,
				 enum tc_setup_type type,
				 void *type_data)
{
	struct ocelot *ocelot = ds->priv;

	switch (type) {
	case TC_QUERY_CAPS:
		return vsc9959_qos_query_caps(type_data);
	case TC_SETUP_QDISC_TAPRIO:
		return vsc9959_qos_port_tas_set(ocelot, port, type_data);
	case TC_SETUP_QDISC_MQPRIO:
		return ocelot_port_mqprio(ocelot, port, type_data);
	case TC_SETUP_QDISC_CBS:
		return vsc9959_qos_port_cbs_set(ds, port, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

#define VSC9959_PSFP_SFID_MAX			175
#define VSC9959_PSFP_GATE_ID_MAX		183
#define VSC9959_PSFP_POLICER_BASE		63
#define VSC9959_PSFP_POLICER_MAX		383
#define VSC9959_PSFP_GATE_LIST_NUM		4
#define VSC9959_PSFP_GATE_CYCLETIME_MIN		5000

struct felix_stream {
	struct list_head list;
	unsigned long id;
	bool dummy;
	int ports;
	int port;
	u8 dmac[ETH_ALEN];
	u16 vid;
	s8 prio;
	u8 sfid_valid;
	u8 ssid_valid;
	u32 sfid;
	u32 ssid;
};

struct felix_stream_filter_counters {
	u64 match;
	u64 not_pass_gate;
	u64 not_pass_sdu;
	u64 red;
};

struct felix_stream_filter {
	struct felix_stream_filter_counters stats;
	struct list_head list;
	refcount_t refcount;
	u32 index;
	u8 enable;
	int portmask;
	u8 sg_valid;
	u32 sgid;
	u8 fm_valid;
	u32 fmid;
	u8 prio_valid;
	u8 prio;
	u32 maxsdu;
};

struct felix_stream_gate {
	u32 index;
	u8 enable;
	u8 ipv_valid;
	u8 init_ipv;
	u64 basetime;
	u64 cycletime;
	u64 cycletime_ext;
	u32 num_entries;
	struct action_gate_entry entries[];
};

struct felix_stream_gate_entry {
	struct list_head list;
	refcount_t refcount;
	u32 index;
};

static int vsc9959_stream_identify(struct flow_cls_offload *f,
				   struct felix_stream *stream)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct flow_dissector *dissector = rule->match.dissector;

	if (dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_VLAN) |
	      BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS)))
		return -EOPNOTSUPP;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;

		flow_rule_match_eth_addrs(rule, &match);
		ether_addr_copy(stream->dmac, match.key->dst);
		if (!is_zero_ether_addr(match.mask->src))
			return -EOPNOTSUPP;
	} else {
		return -EOPNOTSUPP;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(rule, &match);
		if (match.mask->vlan_priority)
			stream->prio = match.key->vlan_priority;
		else
			stream->prio = -1;

		if (!match.mask->vlan_id)
			return -EOPNOTSUPP;
		stream->vid = match.key->vlan_id;
	} else {
		return -EOPNOTSUPP;
	}

	stream->id = f->cookie;

	return 0;
}

static int vsc9959_mact_stream_set(struct ocelot *ocelot,
				   struct felix_stream *stream,
				   struct netlink_ext_ack *extack)
{
	enum macaccess_entry_type type;
	int ret, sfid, ssid;
	u32 vid, dst_idx;
	u8 mac[ETH_ALEN];

	ether_addr_copy(mac, stream->dmac);
	vid = stream->vid;

	/* Stream identification desn't support to add a stream with non
	 * existent MAC (The MAC entry has not been learned in MAC table).
	 */
	ret = ocelot_mact_lookup(ocelot, &dst_idx, mac, vid, &type);
	if (ret) {
		if (extack)
			NL_SET_ERR_MSG_MOD(extack, "Stream is not learned in MAC table");
		return -EOPNOTSUPP;
	}

	if ((stream->sfid_valid || stream->ssid_valid) &&
	    type == ENTRYTYPE_NORMAL)
		type = ENTRYTYPE_LOCKED;

	sfid = stream->sfid_valid ? stream->sfid : -1;
	ssid = stream->ssid_valid ? stream->ssid : -1;

	ret = ocelot_mact_learn_streamdata(ocelot, dst_idx, mac, vid, type,
					   sfid, ssid);

	return ret;
}

static struct felix_stream *
vsc9959_stream_table_lookup(struct list_head *stream_list,
			    struct felix_stream *stream)
{
	struct felix_stream *tmp;

	list_for_each_entry(tmp, stream_list, list)
		if (ether_addr_equal(tmp->dmac, stream->dmac) &&
		    tmp->vid == stream->vid)
			return tmp;

	return NULL;
}

static int vsc9959_stream_table_add(struct ocelot *ocelot,
				    struct list_head *stream_list,
				    struct felix_stream *stream,
				    struct netlink_ext_ack *extack)
{
	struct felix_stream *stream_entry;
	int ret;

	stream_entry = kmemdup(stream, sizeof(*stream_entry), GFP_KERNEL);
	if (!stream_entry)
		return -ENOMEM;

	if (!stream->dummy) {
		ret = vsc9959_mact_stream_set(ocelot, stream_entry, extack);
		if (ret) {
			kfree(stream_entry);
			return ret;
		}
	}

	list_add_tail(&stream_entry->list, stream_list);

	return 0;
}

static struct felix_stream *
vsc9959_stream_table_get(struct list_head *stream_list, unsigned long id)
{
	struct felix_stream *tmp;

	list_for_each_entry(tmp, stream_list, list)
		if (tmp->id == id)
			return tmp;

	return NULL;
}

static void vsc9959_stream_table_del(struct ocelot *ocelot,
				     struct felix_stream *stream)
{
	if (!stream->dummy)
		vsc9959_mact_stream_set(ocelot, stream, NULL);

	list_del(&stream->list);
	kfree(stream);
}

static u32 vsc9959_sfi_access_status(struct ocelot *ocelot)
{
	return ocelot_read(ocelot, ANA_TABLES_SFIDACCESS);
}

static int vsc9959_psfp_sfi_set(struct ocelot *ocelot,
				struct felix_stream_filter *sfi)
{
	u32 val;

	if (sfi->index > VSC9959_PSFP_SFID_MAX)
		return -EINVAL;

	if (!sfi->enable) {
		ocelot_write(ocelot, ANA_TABLES_SFIDTIDX_SFID_INDEX(sfi->index),
			     ANA_TABLES_SFIDTIDX);

		val = ANA_TABLES_SFIDACCESS_SFID_TBL_CMD(SFIDACCESS_CMD_WRITE);
		ocelot_write(ocelot, val, ANA_TABLES_SFIDACCESS);

		return readx_poll_timeout(vsc9959_sfi_access_status, ocelot, val,
					  (!ANA_TABLES_SFIDACCESS_SFID_TBL_CMD(val)),
					  10, 100000);
	}

	if (sfi->sgid > VSC9959_PSFP_GATE_ID_MAX ||
	    sfi->fmid > VSC9959_PSFP_POLICER_MAX)
		return -EINVAL;

	ocelot_write(ocelot,
		     (sfi->sg_valid ? ANA_TABLES_SFIDTIDX_SGID_VALID : 0) |
		     ANA_TABLES_SFIDTIDX_SGID(sfi->sgid) |
		     (sfi->fm_valid ? ANA_TABLES_SFIDTIDX_POL_ENA : 0) |
		     ANA_TABLES_SFIDTIDX_POL_IDX(sfi->fmid) |
		     ANA_TABLES_SFIDTIDX_SFID_INDEX(sfi->index),
		     ANA_TABLES_SFIDTIDX);

	ocelot_write(ocelot,
		     (sfi->prio_valid ? ANA_TABLES_SFIDACCESS_IGR_PRIO_MATCH_ENA : 0) |
		     ANA_TABLES_SFIDACCESS_IGR_PRIO(sfi->prio) |
		     ANA_TABLES_SFIDACCESS_MAX_SDU_LEN(sfi->maxsdu) |
		     ANA_TABLES_SFIDACCESS_SFID_TBL_CMD(SFIDACCESS_CMD_WRITE),
		     ANA_TABLES_SFIDACCESS);

	return readx_poll_timeout(vsc9959_sfi_access_status, ocelot, val,
				  (!ANA_TABLES_SFIDACCESS_SFID_TBL_CMD(val)),
				  10, 100000);
}

static int vsc9959_psfp_sfidmask_set(struct ocelot *ocelot, u32 sfid, int ports)
{
	u32 val;

	ocelot_rmw(ocelot,
		   ANA_TABLES_SFIDTIDX_SFID_INDEX(sfid),
		   ANA_TABLES_SFIDTIDX_SFID_INDEX_M,
		   ANA_TABLES_SFIDTIDX);

	ocelot_write(ocelot,
		     ANA_TABLES_SFID_MASK_IGR_PORT_MASK(ports) |
		     ANA_TABLES_SFID_MASK_IGR_SRCPORT_MATCH_ENA,
		     ANA_TABLES_SFID_MASK);

	ocelot_rmw(ocelot,
		   ANA_TABLES_SFIDACCESS_SFID_TBL_CMD(SFIDACCESS_CMD_WRITE),
		   ANA_TABLES_SFIDACCESS_SFID_TBL_CMD_M,
		   ANA_TABLES_SFIDACCESS);

	return readx_poll_timeout(vsc9959_sfi_access_status, ocelot, val,
				  (!ANA_TABLES_SFIDACCESS_SFID_TBL_CMD(val)),
				  10, 100000);
}

static int vsc9959_psfp_sfi_list_add(struct ocelot *ocelot,
				     struct felix_stream_filter *sfi,
				     struct list_head *pos)
{
	struct felix_stream_filter *sfi_entry;
	int ret;

	sfi_entry = kmemdup(sfi, sizeof(*sfi_entry), GFP_KERNEL);
	if (!sfi_entry)
		return -ENOMEM;

	refcount_set(&sfi_entry->refcount, 1);

	ret = vsc9959_psfp_sfi_set(ocelot, sfi_entry);
	if (ret) {
		kfree(sfi_entry);
		return ret;
	}

	vsc9959_psfp_sfidmask_set(ocelot, sfi->index, sfi->portmask);

	list_add(&sfi_entry->list, pos);

	return 0;
}

static int vsc9959_psfp_sfi_table_add(struct ocelot *ocelot,
				      struct felix_stream_filter *sfi)
{
	struct list_head *pos, *q, *last;
	struct felix_stream_filter *tmp;
	struct ocelot_psfp_list *psfp;
	u32 insert = 0;

	psfp = &ocelot->psfp;
	last = &psfp->sfi_list;

	list_for_each_safe(pos, q, &psfp->sfi_list) {
		tmp = list_entry(pos, struct felix_stream_filter, list);
		if (sfi->sg_valid == tmp->sg_valid &&
		    sfi->fm_valid == tmp->fm_valid &&
		    sfi->portmask == tmp->portmask &&
		    tmp->sgid == sfi->sgid &&
		    tmp->fmid == sfi->fmid) {
			sfi->index = tmp->index;
			refcount_inc(&tmp->refcount);
			return 0;
		}
		/* Make sure that the index is increasing in order. */
		if (tmp->index == insert) {
			last = pos;
			insert++;
		}
	}
	sfi->index = insert;

	return vsc9959_psfp_sfi_list_add(ocelot, sfi, last);
}

static int vsc9959_psfp_sfi_table_add2(struct ocelot *ocelot,
				       struct felix_stream_filter *sfi,
				       struct felix_stream_filter *sfi2)
{
	struct felix_stream_filter *tmp;
	struct list_head *pos, *q, *last;
	struct ocelot_psfp_list *psfp;
	u32 insert = 0;
	int ret;

	psfp = &ocelot->psfp;
	last = &psfp->sfi_list;

	list_for_each_safe(pos, q, &psfp->sfi_list) {
		tmp = list_entry(pos, struct felix_stream_filter, list);
		/* Make sure that the index is increasing in order. */
		if (tmp->index >= insert + 2)
			break;

		insert = tmp->index + 1;
		last = pos;
	}
	sfi->index = insert;

	ret = vsc9959_psfp_sfi_list_add(ocelot, sfi, last);
	if (ret)
		return ret;

	sfi2->index = insert + 1;

	return vsc9959_psfp_sfi_list_add(ocelot, sfi2, last->next);
}

static struct felix_stream_filter *
vsc9959_psfp_sfi_table_get(struct list_head *sfi_list, u32 index)
{
	struct felix_stream_filter *tmp;

	list_for_each_entry(tmp, sfi_list, list)
		if (tmp->index == index)
			return tmp;

	return NULL;
}

static void vsc9959_psfp_sfi_table_del(struct ocelot *ocelot, u32 index)
{
	struct felix_stream_filter *tmp, *n;
	struct ocelot_psfp_list *psfp;
	u8 z;

	psfp = &ocelot->psfp;

	list_for_each_entry_safe(tmp, n, &psfp->sfi_list, list)
		if (tmp->index == index) {
			z = refcount_dec_and_test(&tmp->refcount);
			if (z) {
				tmp->enable = 0;
				vsc9959_psfp_sfi_set(ocelot, tmp);
				list_del(&tmp->list);
				kfree(tmp);
			}
			break;
		}
}

static void vsc9959_psfp_parse_gate(const struct flow_action_entry *entry,
				    struct felix_stream_gate *sgi)
{
	sgi->index = entry->hw_index;
	sgi->ipv_valid = (entry->gate.prio < 0) ? 0 : 1;
	sgi->init_ipv = (sgi->ipv_valid) ? entry->gate.prio : 0;
	sgi->basetime = entry->gate.basetime;
	sgi->cycletime = entry->gate.cycletime;
	sgi->num_entries = entry->gate.num_entries;
	sgi->enable = 1;

	memcpy(sgi->entries, entry->gate.entries,
	       entry->gate.num_entries * sizeof(struct action_gate_entry));
}

static u32 vsc9959_sgi_cfg_status(struct ocelot *ocelot)
{
	return ocelot_read(ocelot, ANA_SG_ACCESS_CTRL);
}

static int vsc9959_psfp_sgi_set(struct ocelot *ocelot,
				struct felix_stream_gate *sgi)
{
	struct action_gate_entry *e;
	struct timespec64 base_ts;
	u32 interval_sum = 0;
	u32 val;
	int i;

	if (sgi->index > VSC9959_PSFP_GATE_ID_MAX)
		return -EINVAL;

	ocelot_write(ocelot, ANA_SG_ACCESS_CTRL_SGID(sgi->index),
		     ANA_SG_ACCESS_CTRL);

	if (!sgi->enable) {
		ocelot_rmw(ocelot, ANA_SG_CONFIG_REG_3_INIT_GATE_STATE,
			   ANA_SG_CONFIG_REG_3_INIT_GATE_STATE |
			   ANA_SG_CONFIG_REG_3_GATE_ENABLE,
			   ANA_SG_CONFIG_REG_3);

		return 0;
	}

	if (sgi->cycletime < VSC9959_PSFP_GATE_CYCLETIME_MIN ||
	    sgi->cycletime > NSEC_PER_SEC)
		return -EINVAL;

	if (sgi->num_entries > VSC9959_PSFP_GATE_LIST_NUM)
		return -EINVAL;

	vsc9959_new_base_time(ocelot, sgi->basetime, sgi->cycletime, &base_ts);
	ocelot_write(ocelot, base_ts.tv_nsec, ANA_SG_CONFIG_REG_1);
	val = lower_32_bits(base_ts.tv_sec);
	ocelot_write(ocelot, val, ANA_SG_CONFIG_REG_2);

	val = upper_32_bits(base_ts.tv_sec);
	ocelot_write(ocelot,
		     (sgi->ipv_valid ? ANA_SG_CONFIG_REG_3_IPV_VALID : 0) |
		     ANA_SG_CONFIG_REG_3_INIT_IPV(sgi->init_ipv) |
		     ANA_SG_CONFIG_REG_3_GATE_ENABLE |
		     ANA_SG_CONFIG_REG_3_LIST_LENGTH(sgi->num_entries) |
		     ANA_SG_CONFIG_REG_3_INIT_GATE_STATE |
		     ANA_SG_CONFIG_REG_3_BASE_TIME_SEC_MSB(val),
		     ANA_SG_CONFIG_REG_3);

	ocelot_write(ocelot, sgi->cycletime, ANA_SG_CONFIG_REG_4);

	e = sgi->entries;
	for (i = 0; i < sgi->num_entries; i++) {
		u32 ips = (e[i].ipv < 0) ? 0 : (e[i].ipv + 8);

		ocelot_write_rix(ocelot, ANA_SG_GCL_GS_CONFIG_IPS(ips) |
				 (e[i].gate_state ?
				  ANA_SG_GCL_GS_CONFIG_GATE_STATE : 0),
				 ANA_SG_GCL_GS_CONFIG, i);

		interval_sum += e[i].interval;
		ocelot_write_rix(ocelot, interval_sum, ANA_SG_GCL_TI_CONFIG, i);
	}

	ocelot_rmw(ocelot, ANA_SG_ACCESS_CTRL_CONFIG_CHANGE,
		   ANA_SG_ACCESS_CTRL_CONFIG_CHANGE,
		   ANA_SG_ACCESS_CTRL);

	return readx_poll_timeout(vsc9959_sgi_cfg_status, ocelot, val,
				  (!(ANA_SG_ACCESS_CTRL_CONFIG_CHANGE & val)),
				  10, 100000);
}

static int vsc9959_psfp_sgi_table_add(struct ocelot *ocelot,
				      struct felix_stream_gate *sgi)
{
	struct felix_stream_gate_entry *tmp;
	struct ocelot_psfp_list *psfp;
	int ret;

	psfp = &ocelot->psfp;

	list_for_each_entry(tmp, &psfp->sgi_list, list)
		if (tmp->index == sgi->index) {
			refcount_inc(&tmp->refcount);
			return 0;
		}

	tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	ret = vsc9959_psfp_sgi_set(ocelot, sgi);
	if (ret) {
		kfree(tmp);
		return ret;
	}

	tmp->index = sgi->index;
	refcount_set(&tmp->refcount, 1);
	list_add_tail(&tmp->list, &psfp->sgi_list);

	return 0;
}

static void vsc9959_psfp_sgi_table_del(struct ocelot *ocelot,
				       u32 index)
{
	struct felix_stream_gate_entry *tmp, *n;
	struct felix_stream_gate sgi = {0};
	struct ocelot_psfp_list *psfp;
	u8 z;

	psfp = &ocelot->psfp;

	list_for_each_entry_safe(tmp, n, &psfp->sgi_list, list)
		if (tmp->index == index) {
			z = refcount_dec_and_test(&tmp->refcount);
			if (z) {
				sgi.index = index;
				sgi.enable = 0;
				vsc9959_psfp_sgi_set(ocelot, &sgi);
				list_del(&tmp->list);
				kfree(tmp);
			}
			break;
		}
}

static int vsc9959_psfp_filter_add(struct ocelot *ocelot, int port,
				   struct flow_cls_offload *f)
{
	struct netlink_ext_ack *extack = f->common.extack;
	struct felix_stream_filter old_sfi, *sfi_entry;
	struct felix_stream_filter sfi = {0};
	const struct flow_action_entry *a;
	struct felix_stream *stream_entry;
	struct felix_stream stream = {0};
	struct felix_stream_gate *sgi;
	struct ocelot_psfp_list *psfp;
	struct ocelot_policer pol;
	int ret, i, size;
	u64 rate, burst;
	u32 index;

	psfp = &ocelot->psfp;

	ret = vsc9959_stream_identify(f, &stream);
	if (ret) {
		NL_SET_ERR_MSG_MOD(extack, "Only can match on VID, PCP, and dest MAC");
		return ret;
	}

	mutex_lock(&psfp->lock);

	flow_action_for_each(i, a, &f->rule->action) {
		switch (a->id) {
		case FLOW_ACTION_GATE:
			size = struct_size(sgi, entries, a->gate.num_entries);
			sgi = kzalloc(size, GFP_KERNEL);
			if (!sgi) {
				ret = -ENOMEM;
				goto err;
			}
			vsc9959_psfp_parse_gate(a, sgi);
			ret = vsc9959_psfp_sgi_table_add(ocelot, sgi);
			if (ret) {
				kfree(sgi);
				goto err;
			}
			sfi.sg_valid = 1;
			sfi.sgid = sgi->index;
			kfree(sgi);
			break;
		case FLOW_ACTION_POLICE:
			index = a->hw_index + VSC9959_PSFP_POLICER_BASE;
			if (index > VSC9959_PSFP_POLICER_MAX) {
				ret = -EINVAL;
				goto err;
			}

			rate = a->police.rate_bytes_ps;
			burst = rate * PSCHED_NS2TICKS(a->police.burst);
			pol = (struct ocelot_policer) {
				.burst = div_u64(burst, PSCHED_TICKS_PER_SEC),
				.rate = div_u64(rate, 1000) * 8,
			};
			ret = ocelot_vcap_policer_add(ocelot, index, &pol);
			if (ret)
				goto err;

			sfi.fm_valid = 1;
			sfi.fmid = index;
			sfi.maxsdu = a->police.mtu;
			break;
		default:
			mutex_unlock(&psfp->lock);
			return -EOPNOTSUPP;
		}
	}

	stream.ports = BIT(port);
	stream.port = port;

	sfi.portmask = stream.ports;
	sfi.prio_valid = (stream.prio < 0 ? 0 : 1);
	sfi.prio = (sfi.prio_valid ? stream.prio : 0);
	sfi.enable = 1;

	/* Check if stream is set. */
	stream_entry = vsc9959_stream_table_lookup(&psfp->stream_list, &stream);
	if (stream_entry) {
		if (stream_entry->ports & BIT(port)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "The stream is added on this port");
			ret = -EEXIST;
			goto err;
		}

		if (stream_entry->ports != BIT(stream_entry->port)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "The stream is added on two ports");
			ret = -EEXIST;
			goto err;
		}

		stream_entry->ports |= BIT(port);
		stream.ports = stream_entry->ports;

		sfi_entry = vsc9959_psfp_sfi_table_get(&psfp->sfi_list,
						       stream_entry->sfid);
		memcpy(&old_sfi, sfi_entry, sizeof(old_sfi));

		vsc9959_psfp_sfi_table_del(ocelot, stream_entry->sfid);

		old_sfi.portmask = stream_entry->ports;
		sfi.portmask = stream.ports;

		if (stream_entry->port > port) {
			ret = vsc9959_psfp_sfi_table_add2(ocelot, &sfi,
							  &old_sfi);
			stream_entry->dummy = true;
		} else {
			ret = vsc9959_psfp_sfi_table_add2(ocelot, &old_sfi,
							  &sfi);
			stream.dummy = true;
		}
		if (ret)
			goto err;

		stream_entry->sfid = old_sfi.index;
	} else {
		ret = vsc9959_psfp_sfi_table_add(ocelot, &sfi);
		if (ret)
			goto err;
	}

	stream.sfid = sfi.index;
	stream.sfid_valid = 1;
	ret = vsc9959_stream_table_add(ocelot, &psfp->stream_list,
				       &stream, extack);
	if (ret) {
		vsc9959_psfp_sfi_table_del(ocelot, stream.sfid);
		goto err;
	}

	mutex_unlock(&psfp->lock);

	return 0;

err:
	if (sfi.sg_valid)
		vsc9959_psfp_sgi_table_del(ocelot, sfi.sgid);

	if (sfi.fm_valid)
		ocelot_vcap_policer_del(ocelot, sfi.fmid);

	mutex_unlock(&psfp->lock);

	return ret;
}

static int vsc9959_psfp_filter_del(struct ocelot *ocelot,
				   struct flow_cls_offload *f)
{
	struct felix_stream *stream, tmp, *stream_entry;
	struct ocelot_psfp_list *psfp = &ocelot->psfp;
	static struct felix_stream_filter *sfi;

	mutex_lock(&psfp->lock);

	stream = vsc9959_stream_table_get(&psfp->stream_list, f->cookie);
	if (!stream) {
		mutex_unlock(&psfp->lock);
		return -ENOMEM;
	}

	sfi = vsc9959_psfp_sfi_table_get(&psfp->sfi_list, stream->sfid);
	if (!sfi) {
		mutex_unlock(&psfp->lock);
		return -ENOMEM;
	}

	if (sfi->sg_valid)
		vsc9959_psfp_sgi_table_del(ocelot, sfi->sgid);

	if (sfi->fm_valid)
		ocelot_vcap_policer_del(ocelot, sfi->fmid);

	vsc9959_psfp_sfi_table_del(ocelot, stream->sfid);

	memcpy(&tmp, stream, sizeof(tmp));

	stream->sfid_valid = 0;
	vsc9959_stream_table_del(ocelot, stream);

	stream_entry = vsc9959_stream_table_lookup(&psfp->stream_list, &tmp);
	if (stream_entry) {
		stream_entry->ports = BIT(stream_entry->port);
		if (stream_entry->dummy) {
			stream_entry->dummy = false;
			vsc9959_mact_stream_set(ocelot, stream_entry, NULL);
		}
		vsc9959_psfp_sfidmask_set(ocelot, stream_entry->sfid,
					  stream_entry->ports);
	}

	mutex_unlock(&psfp->lock);

	return 0;
}

static void vsc9959_update_sfid_stats(struct ocelot *ocelot,
				      struct felix_stream_filter *sfi)
{
	struct felix_stream_filter_counters *s = &sfi->stats;
	u32 match, not_pass_gate, not_pass_sdu, red;
	u32 sfid = sfi->index;

	lockdep_assert_held(&ocelot->stat_view_lock);

	ocelot_rmw(ocelot, SYS_STAT_CFG_STAT_VIEW(sfid),
		   SYS_STAT_CFG_STAT_VIEW_M,
		   SYS_STAT_CFG);

	match = ocelot_read(ocelot, SYS_COUNT_SF_MATCHING_FRAMES);
	not_pass_gate = ocelot_read(ocelot, SYS_COUNT_SF_NOT_PASSING_FRAMES);
	not_pass_sdu = ocelot_read(ocelot, SYS_COUNT_SF_NOT_PASSING_SDU);
	red = ocelot_read(ocelot, SYS_COUNT_SF_RED_FRAMES);

	/* Clear the PSFP counter. */
	ocelot_write(ocelot,
		     SYS_STAT_CFG_STAT_VIEW(sfid) |
		     SYS_STAT_CFG_STAT_CLEAR_SHOT(0x10),
		     SYS_STAT_CFG);

	s->match += match;
	s->not_pass_gate += not_pass_gate;
	s->not_pass_sdu += not_pass_sdu;
	s->red += red;
}

/* Caller must hold &ocelot->stat_view_lock */
static void vsc9959_update_stats(struct ocelot *ocelot)
{
	struct ocelot_psfp_list *psfp = &ocelot->psfp;
	struct felix_stream_filter *sfi;

	mutex_lock(&psfp->lock);

	list_for_each_entry(sfi, &psfp->sfi_list, list)
		vsc9959_update_sfid_stats(ocelot, sfi);

	mutex_unlock(&psfp->lock);
}

static int vsc9959_psfp_stats_get(struct ocelot *ocelot,
				  struct flow_cls_offload *f,
				  struct flow_stats *stats)
{
	struct ocelot_psfp_list *psfp = &ocelot->psfp;
	struct felix_stream_filter_counters *s;
	static struct felix_stream_filter *sfi;
	struct felix_stream *stream;

	stream = vsc9959_stream_table_get(&psfp->stream_list, f->cookie);
	if (!stream)
		return -ENOMEM;

	sfi = vsc9959_psfp_sfi_table_get(&psfp->sfi_list, stream->sfid);
	if (!sfi)
		return -EINVAL;

	mutex_lock(&ocelot->stat_view_lock);

	vsc9959_update_sfid_stats(ocelot, sfi);

	s = &sfi->stats;
	stats->pkts = s->match;
	stats->drops = s->not_pass_gate + s->not_pass_sdu + s->red;

	memset(s, 0, sizeof(*s));

	mutex_unlock(&ocelot->stat_view_lock);

	return 0;
}

static void vsc9959_psfp_init(struct ocelot *ocelot)
{
	struct ocelot_psfp_list *psfp = &ocelot->psfp;

	INIT_LIST_HEAD(&psfp->stream_list);
	INIT_LIST_HEAD(&psfp->sfi_list);
	INIT_LIST_HEAD(&psfp->sgi_list);
	mutex_init(&psfp->lock);
}

/* When using cut-through forwarding and the egress port runs at a higher data
 * rate than the ingress port, the packet currently under transmission would
 * suffer an underrun since it would be transmitted faster than it is received.
 * The Felix switch implementation of cut-through forwarding does not check in
 * hardware whether this condition is satisfied or not, so we must restrict the
 * list of ports that have cut-through forwarding enabled on egress to only be
 * the ports operating at the lowest link speed within their respective
 * forwarding domain.
 */
static void vsc9959_cut_through_fwd(struct ocelot *ocelot)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	struct dsa_switch *ds = felix->ds;
	int tc, port, other_port;

	lockdep_assert_held(&ocelot->fwd_domain_lock);

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port *ocelot_port = ocelot->ports[port];
		struct ocelot_mm_state *mm = &ocelot->mm[port];
		int min_speed = ocelot_port->speed;
		unsigned long mask = 0;
		u32 tmp, val = 0;

		/* Disable cut-through on ports that are down */
		if (ocelot_port->speed <= 0)
			goto set;

		if (dsa_is_cpu_port(ds, port)) {
			/* Ocelot switches forward from the NPI port towards
			 * any port, regardless of it being in the NPI port's
			 * forwarding domain or not.
			 */
			mask = dsa_user_ports(ds);
		} else {
			mask = ocelot_get_bridge_fwd_mask(ocelot, port);
			mask &= ~BIT(port);
			if (ocelot->npi >= 0)
				mask |= BIT(ocelot->npi);
			else
				mask |= ocelot_port_assigned_dsa_8021q_cpu_mask(ocelot,
										port);
		}

		/* Calculate the minimum link speed, among the ports that are
		 * up, of this source port's forwarding domain.
		 */
		for_each_set_bit(other_port, &mask, ocelot->num_phys_ports) {
			struct ocelot_port *other_ocelot_port;

			other_ocelot_port = ocelot->ports[other_port];
			if (other_ocelot_port->speed <= 0)
				continue;

			if (min_speed > other_ocelot_port->speed)
				min_speed = other_ocelot_port->speed;
		}

		/* Enable cut-through forwarding for all traffic classes that
		 * don't have oversized dropping enabled, since this check is
		 * bypassed in cut-through mode. Also exclude preemptible
		 * traffic classes, since these would hang the port for some
		 * reason, if sent as cut-through.
		 */
		if (ocelot_port->speed == min_speed) {
			val = GENMASK(7, 0) & ~mm->active_preemptible_tcs;

			for (tc = 0; tc < OCELOT_NUM_TC; tc++)
				if (vsc9959_port_qmaxsdu_get(ocelot, port, tc))
					val &= ~BIT(tc);
		}

set:
		tmp = ocelot_read_rix(ocelot, ANA_CUT_THRU_CFG, port);
		if (tmp == val)
			continue;

		dev_dbg(ocelot->dev,
			"port %d fwd mask 0x%lx speed %d min_speed %d, %s cut-through forwarding on TC mask 0x%x\n",
			port, mask, ocelot_port->speed, min_speed,
			val ? "enabling" : "disabling", val);

		ocelot_write_rix(ocelot, val, ANA_CUT_THRU_CFG, port);
	}
}

static const struct ocelot_ops vsc9959_ops = {
	.reset			= vsc9959_reset,
	.wm_enc			= vsc9959_wm_enc,
	.wm_dec			= vsc9959_wm_dec,
	.wm_stat		= vsc9959_wm_stat,
	.port_to_netdev		= felix_port_to_netdev,
	.netdev_to_port		= felix_netdev_to_port,
	.psfp_init		= vsc9959_psfp_init,
	.psfp_filter_add	= vsc9959_psfp_filter_add,
	.psfp_filter_del	= vsc9959_psfp_filter_del,
	.psfp_stats_get		= vsc9959_psfp_stats_get,
	.cut_through_fwd	= vsc9959_cut_through_fwd,
	.tas_clock_adjust	= vsc9959_tas_clock_adjust,
	.update_stats		= vsc9959_update_stats,
};

static const struct felix_info felix_info_vsc9959 = {
	.resources		= vsc9959_resources,
	.num_resources		= ARRAY_SIZE(vsc9959_resources),
	.resource_names		= vsc9959_resource_names,
	.regfields		= vsc9959_regfields,
	.map			= vsc9959_regmap,
	.ops			= &vsc9959_ops,
	.vcap			= vsc9959_vcap_props,
	.vcap_pol_base		= VSC9959_VCAP_POLICER_BASE,
	.vcap_pol_max		= VSC9959_VCAP_POLICER_MAX,
	.vcap_pol_base2		= 0,
	.vcap_pol_max2		= 0,
	.num_mact_rows		= 2048,
	.num_ports		= VSC9959_NUM_PORTS,
	.num_tx_queues		= OCELOT_NUM_TC,
	.quirks			= FELIX_MAC_QUIRKS,
	.quirk_no_xtr_irq	= true,
	.ptp_caps		= &vsc9959_ptp_caps,
	.mdio_bus_alloc		= vsc9959_mdio_bus_alloc,
	.mdio_bus_free		= vsc9959_mdio_bus_free,
	.port_modes		= vsc9959_port_modes,
	.port_setup_tc		= vsc9959_port_setup_tc,
	.port_sched_speed_set	= vsc9959_sched_speed_set,
	.tas_guard_bands_update	= vsc9959_tas_guard_bands_update,
};

/* The INTB interrupt is shared between for PTP TX timestamp availability
 * notification and MAC Merge status change on each port.
 */
static irqreturn_t felix_irq_handler(int irq, void *data)
{
	struct ocelot *ocelot = (struct ocelot *)data;

	ocelot_get_txtstamp(ocelot);
	ocelot_mm_irq(ocelot);

	return IRQ_HANDLED;
}

static int felix_pci_probe(struct pci_dev *pdev,
			   const struct pci_device_id *id)
{
	struct dsa_switch *ds;
	struct ocelot *ocelot;
	struct felix *felix;
	int err;

	if (pdev->dev.of_node && !of_device_is_available(pdev->dev.of_node)) {
		dev_info(&pdev->dev, "device is disabled, skipping\n");
		return -ENODEV;
	}

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "device enable failed\n");
		goto err_pci_enable;
	}

	felix = kzalloc(sizeof(struct felix), GFP_KERNEL);
	if (!felix) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "Failed to allocate driver memory\n");
		goto err_alloc_felix;
	}

	pci_set_drvdata(pdev, felix);
	ocelot = &felix->ocelot;
	ocelot->dev = &pdev->dev;
	ocelot->num_flooding_pgids = OCELOT_NUM_TC;
	felix->info = &felix_info_vsc9959;
	felix->switch_base = pci_resource_start(pdev, VSC9959_SWITCH_PCI_BAR);

	pci_set_master(pdev);

	err = devm_request_threaded_irq(&pdev->dev, pdev->irq, NULL,
					&felix_irq_handler, IRQF_ONESHOT,
					"felix-intb", ocelot);
	if (err) {
		dev_err(&pdev->dev, "Failed to request irq\n");
		goto err_alloc_irq;
	}

	ocelot->ptp = 1;
	ocelot->mm_supported = true;

	ds = kzalloc(sizeof(struct dsa_switch), GFP_KERNEL);
	if (!ds) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "Failed to allocate DSA switch\n");
		goto err_alloc_ds;
	}

	ds->dev = &pdev->dev;
	ds->num_ports = felix->info->num_ports;
	ds->num_tx_queues = felix->info->num_tx_queues;
	ds->ops = &felix_switch_ops;
	ds->priv = ocelot;
	felix->ds = ds;
	felix->tag_proto = DSA_TAG_PROTO_OCELOT;

	err = dsa_register_switch(ds);
	if (err) {
		dev_err_probe(&pdev->dev, err, "Failed to register DSA switch\n");
		goto err_register_ds;
	}

	return 0;

err_register_ds:
	kfree(ds);
err_alloc_ds:
err_alloc_irq:
	kfree(felix);
err_alloc_felix:
	pci_disable_device(pdev);
err_pci_enable:
	return err;
}

static void felix_pci_remove(struct pci_dev *pdev)
{
	struct felix *felix = pci_get_drvdata(pdev);

	if (!felix)
		return;

	dsa_unregister_switch(felix->ds);

	kfree(felix->ds);
	kfree(felix);

	pci_disable_device(pdev);
}

static void felix_pci_shutdown(struct pci_dev *pdev)
{
	struct felix *felix = pci_get_drvdata(pdev);

	if (!felix)
		return;

	dsa_switch_shutdown(felix->ds);

	pci_set_drvdata(pdev, NULL);
}

static struct pci_device_id felix_ids[] = {
	{
		/* NXP LS1028A */
		PCI_DEVICE(PCI_VENDOR_ID_FREESCALE, 0xEEF0),
	},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, felix_ids);

static struct pci_driver felix_vsc9959_pci_driver = {
	.name		= "mscc_felix",
	.id_table	= felix_ids,
	.probe		= felix_pci_probe,
	.remove		= felix_pci_remove,
	.shutdown	= felix_pci_shutdown,
};
module_pci_driver(felix_vsc9959_pci_driver);

MODULE_DESCRIPTION("Felix Switch driver");
MODULE_LICENSE("GPL v2");
