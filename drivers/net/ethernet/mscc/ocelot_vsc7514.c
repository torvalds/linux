// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <linux/dsa/ocelot.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_net.h>
#include <linux/netdevice.h>
#include <linux/phylink.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/mfd/syscon.h>
#include <linux/skbuff.h>
#include <net/switchdev.h>

#include <soc/mscc/ocelot_vcap.h>
#include <soc/mscc/ocelot_hsio.h>
#include <soc/mscc/vsc7514_regs.h>
#include "ocelot_fdma.h"
#include "ocelot.h"

#define VSC7514_VCAP_POLICER_BASE			128
#define VSC7514_VCAP_POLICER_MAX			191

static const u32 *ocelot_regmap[TARGET_MAX] = {
	[ANA] = vsc7514_ana_regmap,
	[QS] = vsc7514_qs_regmap,
	[QSYS] = vsc7514_qsys_regmap,
	[REW] = vsc7514_rew_regmap,
	[SYS] = vsc7514_sys_regmap,
	[S0] = vsc7514_vcap_regmap,
	[S1] = vsc7514_vcap_regmap,
	[S2] = vsc7514_vcap_regmap,
	[PTP] = vsc7514_ptp_regmap,
	[DEV_GMII] = vsc7514_dev_gmii_regmap,
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

static const struct ocelot_stat_layout ocelot_stats_layout[OCELOT_NUM_STATS] = {
	[OCELOT_STAT_RX_OCTETS] = {
		.name = "rx_octets",
		.offset = 0x00,
	},
	[OCELOT_STAT_RX_UNICAST] = {
		.name = "rx_unicast",
		.offset = 0x01,
	},
	[OCELOT_STAT_RX_MULTICAST] = {
		.name = "rx_multicast",
		.offset = 0x02,
	},
	[OCELOT_STAT_RX_BROADCAST] = {
		.name = "rx_broadcast",
		.offset = 0x03,
	},
	[OCELOT_STAT_RX_SHORTS] = {
		.name = "rx_shorts",
		.offset = 0x04,
	},
	[OCELOT_STAT_RX_FRAGMENTS] = {
		.name = "rx_fragments",
		.offset = 0x05,
	},
	[OCELOT_STAT_RX_JABBERS] = {
		.name = "rx_jabbers",
		.offset = 0x06,
	},
	[OCELOT_STAT_RX_CRC_ALIGN_ERRS] = {
		.name = "rx_crc_align_errs",
		.offset = 0x07,
	},
	[OCELOT_STAT_RX_SYM_ERRS] = {
		.name = "rx_sym_errs",
		.offset = 0x08,
	},
	[OCELOT_STAT_RX_64] = {
		.name = "rx_frames_below_65_octets",
		.offset = 0x09,
	},
	[OCELOT_STAT_RX_65_127] = {
		.name = "rx_frames_65_to_127_octets",
		.offset = 0x0A,
	},
	[OCELOT_STAT_RX_128_255] = {
		.name = "rx_frames_128_to_255_octets",
		.offset = 0x0B,
	},
	[OCELOT_STAT_RX_256_511] = {
		.name = "rx_frames_256_to_511_octets",
		.offset = 0x0C,
	},
	[OCELOT_STAT_RX_512_1023] = {
		.name = "rx_frames_512_to_1023_octets",
		.offset = 0x0D,
	},
	[OCELOT_STAT_RX_1024_1526] = {
		.name = "rx_frames_1024_to_1526_octets",
		.offset = 0x0E,
	},
	[OCELOT_STAT_RX_1527_MAX] = {
		.name = "rx_frames_over_1526_octets",
		.offset = 0x0F,
	},
	[OCELOT_STAT_RX_PAUSE] = {
		.name = "rx_pause",
		.offset = 0x10,
	},
	[OCELOT_STAT_RX_CONTROL] = {
		.name = "rx_control",
		.offset = 0x11,
	},
	[OCELOT_STAT_RX_LONGS] = {
		.name = "rx_longs",
		.offset = 0x12,
	},
	[OCELOT_STAT_RX_CLASSIFIED_DROPS] = {
		.name = "rx_classified_drops",
		.offset = 0x13,
	},
	[OCELOT_STAT_RX_RED_PRIO_0] = {
		.name = "rx_red_prio_0",
		.offset = 0x14,
	},
	[OCELOT_STAT_RX_RED_PRIO_1] = {
		.name = "rx_red_prio_1",
		.offset = 0x15,
	},
	[OCELOT_STAT_RX_RED_PRIO_2] = {
		.name = "rx_red_prio_2",
		.offset = 0x16,
	},
	[OCELOT_STAT_RX_RED_PRIO_3] = {
		.name = "rx_red_prio_3",
		.offset = 0x17,
	},
	[OCELOT_STAT_RX_RED_PRIO_4] = {
		.name = "rx_red_prio_4",
		.offset = 0x18,
	},
	[OCELOT_STAT_RX_RED_PRIO_5] = {
		.name = "rx_red_prio_5",
		.offset = 0x19,
	},
	[OCELOT_STAT_RX_RED_PRIO_6] = {
		.name = "rx_red_prio_6",
		.offset = 0x1A,
	},
	[OCELOT_STAT_RX_RED_PRIO_7] = {
		.name = "rx_red_prio_7",
		.offset = 0x1B,
	},
	[OCELOT_STAT_RX_YELLOW_PRIO_0] = {
		.name = "rx_yellow_prio_0",
		.offset = 0x1C,
	},
	[OCELOT_STAT_RX_YELLOW_PRIO_1] = {
		.name = "rx_yellow_prio_1",
		.offset = 0x1D,
	},
	[OCELOT_STAT_RX_YELLOW_PRIO_2] = {
		.name = "rx_yellow_prio_2",
		.offset = 0x1E,
	},
	[OCELOT_STAT_RX_YELLOW_PRIO_3] = {
		.name = "rx_yellow_prio_3",
		.offset = 0x1F,
	},
	[OCELOT_STAT_RX_YELLOW_PRIO_4] = {
		.name = "rx_yellow_prio_4",
		.offset = 0x20,
	},
	[OCELOT_STAT_RX_YELLOW_PRIO_5] = {
		.name = "rx_yellow_prio_5",
		.offset = 0x21,
	},
	[OCELOT_STAT_RX_YELLOW_PRIO_6] = {
		.name = "rx_yellow_prio_6",
		.offset = 0x22,
	},
	[OCELOT_STAT_RX_YELLOW_PRIO_7] = {
		.name = "rx_yellow_prio_7",
		.offset = 0x23,
	},
	[OCELOT_STAT_RX_GREEN_PRIO_0] = {
		.name = "rx_green_prio_0",
		.offset = 0x24,
	},
	[OCELOT_STAT_RX_GREEN_PRIO_1] = {
		.name = "rx_green_prio_1",
		.offset = 0x25,
	},
	[OCELOT_STAT_RX_GREEN_PRIO_2] = {
		.name = "rx_green_prio_2",
		.offset = 0x26,
	},
	[OCELOT_STAT_RX_GREEN_PRIO_3] = {
		.name = "rx_green_prio_3",
		.offset = 0x27,
	},
	[OCELOT_STAT_RX_GREEN_PRIO_4] = {
		.name = "rx_green_prio_4",
		.offset = 0x28,
	},
	[OCELOT_STAT_RX_GREEN_PRIO_5] = {
		.name = "rx_green_prio_5",
		.offset = 0x29,
	},
	[OCELOT_STAT_RX_GREEN_PRIO_6] = {
		.name = "rx_green_prio_6",
		.offset = 0x2A,
	},
	[OCELOT_STAT_RX_GREEN_PRIO_7] = {
		.name = "rx_green_prio_7",
		.offset = 0x2B,
	},
	[OCELOT_STAT_TX_OCTETS] = {
		.name = "tx_octets",
		.offset = 0x40,
	},
	[OCELOT_STAT_TX_UNICAST] = {
		.name = "tx_unicast",
		.offset = 0x41,
	},
	[OCELOT_STAT_TX_MULTICAST] = {
		.name = "tx_multicast",
		.offset = 0x42,
	},
	[OCELOT_STAT_TX_BROADCAST] = {
		.name = "tx_broadcast",
		.offset = 0x43,
	},
	[OCELOT_STAT_TX_COLLISION] = {
		.name = "tx_collision",
		.offset = 0x44,
	},
	[OCELOT_STAT_TX_DROPS] = {
		.name = "tx_drops",
		.offset = 0x45,
	},
	[OCELOT_STAT_TX_PAUSE] = {
		.name = "tx_pause",
		.offset = 0x46,
	},
	[OCELOT_STAT_TX_64] = {
		.name = "tx_frames_below_65_octets",
		.offset = 0x47,
	},
	[OCELOT_STAT_TX_65_127] = {
		.name = "tx_frames_65_to_127_octets",
		.offset = 0x48,
	},
	[OCELOT_STAT_TX_128_255] = {
		.name = "tx_frames_128_255_octets",
		.offset = 0x49,
	},
	[OCELOT_STAT_TX_256_511] = {
		.name = "tx_frames_256_511_octets",
		.offset = 0x4A,
	},
	[OCELOT_STAT_TX_512_1023] = {
		.name = "tx_frames_512_1023_octets",
		.offset = 0x4B,
	},
	[OCELOT_STAT_TX_1024_1526] = {
		.name = "tx_frames_1024_1526_octets",
		.offset = 0x4C,
	},
	[OCELOT_STAT_TX_1527_MAX] = {
		.name = "tx_frames_over_1526_octets",
		.offset = 0x4D,
	},
	[OCELOT_STAT_TX_YELLOW_PRIO_0] = {
		.name = "tx_yellow_prio_0",
		.offset = 0x4E,
	},
	[OCELOT_STAT_TX_YELLOW_PRIO_1] = {
		.name = "tx_yellow_prio_1",
		.offset = 0x4F,
	},
	[OCELOT_STAT_TX_YELLOW_PRIO_2] = {
		.name = "tx_yellow_prio_2",
		.offset = 0x50,
	},
	[OCELOT_STAT_TX_YELLOW_PRIO_3] = {
		.name = "tx_yellow_prio_3",
		.offset = 0x51,
	},
	[OCELOT_STAT_TX_YELLOW_PRIO_4] = {
		.name = "tx_yellow_prio_4",
		.offset = 0x52,
	},
	[OCELOT_STAT_TX_YELLOW_PRIO_5] = {
		.name = "tx_yellow_prio_5",
		.offset = 0x53,
	},
	[OCELOT_STAT_TX_YELLOW_PRIO_6] = {
		.name = "tx_yellow_prio_6",
		.offset = 0x54,
	},
	[OCELOT_STAT_TX_YELLOW_PRIO_7] = {
		.name = "tx_yellow_prio_7",
		.offset = 0x55,
	},
	[OCELOT_STAT_TX_GREEN_PRIO_0] = {
		.name = "tx_green_prio_0",
		.offset = 0x56,
	},
	[OCELOT_STAT_TX_GREEN_PRIO_1] = {
		.name = "tx_green_prio_1",
		.offset = 0x57,
	},
	[OCELOT_STAT_TX_GREEN_PRIO_2] = {
		.name = "tx_green_prio_2",
		.offset = 0x58,
	},
	[OCELOT_STAT_TX_GREEN_PRIO_3] = {
		.name = "tx_green_prio_3",
		.offset = 0x59,
	},
	[OCELOT_STAT_TX_GREEN_PRIO_4] = {
		.name = "tx_green_prio_4",
		.offset = 0x5A,
	},
	[OCELOT_STAT_TX_GREEN_PRIO_5] = {
		.name = "tx_green_prio_5",
		.offset = 0x5B,
	},
	[OCELOT_STAT_TX_GREEN_PRIO_6] = {
		.name = "tx_green_prio_6",
		.offset = 0x5C,
	},
	[OCELOT_STAT_TX_GREEN_PRIO_7] = {
		.name = "tx_green_prio_7",
		.offset = 0x5D,
	},
	[OCELOT_STAT_TX_AGED] = {
		.name = "tx_aged",
		.offset = 0x5E,
	},
	[OCELOT_STAT_DROP_LOCAL] = {
		.name = "drop_local",
		.offset = 0x80,
	},
	[OCELOT_STAT_DROP_TAIL] = {
		.name = "drop_tail",
		.offset = 0x81,
	},
	[OCELOT_STAT_DROP_YELLOW_PRIO_0] = {
		.name = "drop_yellow_prio_0",
		.offset = 0x82,
	},
	[OCELOT_STAT_DROP_YELLOW_PRIO_1] = {
		.name = "drop_yellow_prio_1",
		.offset = 0x83,
	},
	[OCELOT_STAT_DROP_YELLOW_PRIO_2] = {
		.name = "drop_yellow_prio_2",
		.offset = 0x84,
	},
	[OCELOT_STAT_DROP_YELLOW_PRIO_3] = {
		.name = "drop_yellow_prio_3",
		.offset = 0x85,
	},
	[OCELOT_STAT_DROP_YELLOW_PRIO_4] = {
		.name = "drop_yellow_prio_4",
		.offset = 0x86,
	},
	[OCELOT_STAT_DROP_YELLOW_PRIO_5] = {
		.name = "drop_yellow_prio_5",
		.offset = 0x87,
	},
	[OCELOT_STAT_DROP_YELLOW_PRIO_6] = {
		.name = "drop_yellow_prio_6",
		.offset = 0x88,
	},
	[OCELOT_STAT_DROP_YELLOW_PRIO_7] = {
		.name = "drop_yellow_prio_7",
		.offset = 0x89,
	},
	[OCELOT_STAT_DROP_GREEN_PRIO_0] = {
		.name = "drop_green_prio_0",
		.offset = 0x8A,
	},
	[OCELOT_STAT_DROP_GREEN_PRIO_1] = {
		.name = "drop_green_prio_1",
		.offset = 0x8B,
	},
	[OCELOT_STAT_DROP_GREEN_PRIO_2] = {
		.name = "drop_green_prio_2",
		.offset = 0x8C,
	},
	[OCELOT_STAT_DROP_GREEN_PRIO_3] = {
		.name = "drop_green_prio_3",
		.offset = 0x8D,
	},
	[OCELOT_STAT_DROP_GREEN_PRIO_4] = {
		.name = "drop_green_prio_4",
		.offset = 0x8E,
	},
	[OCELOT_STAT_DROP_GREEN_PRIO_5] = {
		.name = "drop_green_prio_5",
		.offset = 0x8F,
	},
	[OCELOT_STAT_DROP_GREEN_PRIO_6] = {
		.name = "drop_green_prio_6",
		.offset = 0x90,
	},
	[OCELOT_STAT_DROP_GREEN_PRIO_7] = {
		.name = "drop_green_prio_7",
		.offset = 0x91,
	},
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

static irqreturn_t ocelot_xtr_irq_handler(int irq, void *arg)
{
	struct ocelot *ocelot = arg;
	int grp = 0, err;

	while (ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp)) {
		struct sk_buff *skb;

		err = ocelot_xtr_poll_frame(ocelot, grp, &skb);
		if (err)
			goto out;

		skb->dev->stats.rx_bytes += skb->len;
		skb->dev->stats.rx_packets++;

		if (!skb_defer_rx_timestamp(skb))
			netif_rx(skb);
	}

out:
	if (err < 0)
		ocelot_drain_cpu_queue(ocelot, 0);

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
	WARN_ON(value >= 16 * BIT(8));

	if (value >= BIT(8))
		return BIT(8) | (value / 16);

	return value;
}

static u16 ocelot_wm_dec(u16 wm)
{
	if (wm & BIT(8))
		return (wm & GENMASK(7, 0)) * 16;

	return wm;
}

static void ocelot_wm_stat(u32 val, u32 *inuse, u32 *maxuse)
{
	*inuse = (val & GENMASK(23, 12)) >> 12;
	*maxuse = val & GENMASK(11, 0);
}

static const struct ocelot_ops ocelot_ops = {
	.reset			= ocelot_reset,
	.wm_enc			= ocelot_wm_enc,
	.wm_dec			= ocelot_wm_dec,
	.wm_stat		= ocelot_wm_stat,
	.port_to_netdev		= ocelot_port_to_netdev,
	.netdev_to_port		= ocelot_netdev_to_port,
};

static struct vcap_props vsc7514_vcap_props[] = {
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

static void mscc_ocelot_teardown_devlink_ports(struct ocelot *ocelot)
{
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++)
		ocelot_port_devlink_teardown(ocelot, port);
}

static void mscc_ocelot_release_ports(struct ocelot *ocelot)
{
	int port;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		struct ocelot_port *ocelot_port;

		ocelot_port = ocelot->ports[port];
		if (!ocelot_port)
			continue;

		ocelot_deinit_port(ocelot, port);
		ocelot_release_port(ocelot_port);
	}
}

static int mscc_ocelot_init_ports(struct platform_device *pdev,
				  struct device_node *ports)
{
	struct ocelot *ocelot = platform_get_drvdata(pdev);
	u32 devlink_ports_registered = 0;
	struct device_node *portnp;
	int port, err;
	u32 reg;

	ocelot->ports = devm_kcalloc(ocelot->dev, ocelot->num_phys_ports,
				     sizeof(struct ocelot_port *), GFP_KERNEL);
	if (!ocelot->ports)
		return -ENOMEM;

	ocelot->devlink_ports = devm_kcalloc(ocelot->dev,
					     ocelot->num_phys_ports,
					     sizeof(*ocelot->devlink_ports),
					     GFP_KERNEL);
	if (!ocelot->devlink_ports)
		return -ENOMEM;

	for_each_available_child_of_node(ports, portnp) {
		struct ocelot_port_private *priv;
		struct ocelot_port *ocelot_port;
		struct devlink_port *dlp;
		struct regmap *target;
		struct resource *res;
		char res_name[8];

		if (of_property_read_u32(portnp, "reg", &reg))
			continue;

		port = reg;
		if (port < 0 || port >= ocelot->num_phys_ports) {
			dev_err(ocelot->dev,
				"invalid port number: %d >= %d\n", port,
				ocelot->num_phys_ports);
			continue;
		}

		snprintf(res_name, sizeof(res_name), "port%d", port);

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   res_name);
		target = ocelot_regmap_init(ocelot, res);
		if (IS_ERR(target)) {
			err = PTR_ERR(target);
			of_node_put(portnp);
			goto out_teardown;
		}

		err = ocelot_port_devlink_init(ocelot, port,
					       DEVLINK_PORT_FLAVOUR_PHYSICAL);
		if (err) {
			of_node_put(portnp);
			goto out_teardown;
		}

		err = ocelot_probe_port(ocelot, port, target, portnp);
		if (err) {
			ocelot_port_devlink_teardown(ocelot, port);
			continue;
		}

		devlink_ports_registered |= BIT(port);

		ocelot_port = ocelot->ports[port];
		priv = container_of(ocelot_port, struct ocelot_port_private,
				    port);
		dlp = &ocelot->devlink_ports[port];
		devlink_port_type_eth_set(dlp, priv->dev);
	}

	/* Initialize unused devlink ports at the end */
	for (port = 0; port < ocelot->num_phys_ports; port++) {
		if (devlink_ports_registered & BIT(port))
			continue;

		err = ocelot_port_devlink_init(ocelot, port,
					       DEVLINK_PORT_FLAVOUR_UNUSED);
		if (err)
			goto out_teardown;

		devlink_ports_registered |= BIT(port);
	}

	return 0;

out_teardown:
	/* Unregister the network interfaces */
	mscc_ocelot_release_ports(ocelot);
	/* Tear down devlink ports for the registered network interfaces */
	for (port = 0; port < ocelot->num_phys_ports; port++) {
		if (devlink_ports_registered & BIT(port))
			ocelot_port_devlink_teardown(ocelot, port);
	}
	return err;
}

static int mscc_ocelot_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int err, irq_xtr, irq_ptp_rdy;
	struct device_node *ports;
	struct devlink *devlink;
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
		{ FDMA, "fdma", 1 },
	};

	if (!np && !pdev->dev.platform_data)
		return -ENODEV;

	devlink =
		devlink_alloc(&ocelot_devlink_ops, sizeof(*ocelot), &pdev->dev);
	if (!devlink)
		return -ENOMEM;

	ocelot = devlink_priv(devlink);
	ocelot->devlink = priv_to_devlink(ocelot);
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
			err = PTR_ERR(target);
			goto out_free_devlink;
		}

		ocelot->targets[io_target[i].id] = target;
	}

	if (ocelot->targets[FDMA])
		ocelot_fdma_init(pdev, ocelot);

	hsio = syscon_regmap_lookup_by_compatible("mscc,ocelot-hsio");
	if (IS_ERR(hsio)) {
		dev_err(&pdev->dev, "missing hsio syscon\n");
		err = PTR_ERR(hsio);
		goto out_free_devlink;
	}

	ocelot->targets[HSIO] = hsio;

	err = ocelot_chip_init(ocelot, &ocelot_ops);
	if (err)
		goto out_free_devlink;

	irq_xtr = platform_get_irq_byname(pdev, "xtr");
	if (irq_xtr < 0) {
		err = irq_xtr;
		goto out_free_devlink;
	}

	err = devm_request_threaded_irq(&pdev->dev, irq_xtr, NULL,
					ocelot_xtr_irq_handler, IRQF_ONESHOT,
					"frame extraction", ocelot);
	if (err)
		goto out_free_devlink;

	irq_ptp_rdy = platform_get_irq_byname(pdev, "ptp_rdy");
	if (irq_ptp_rdy > 0 && ocelot->targets[PTP]) {
		err = devm_request_threaded_irq(&pdev->dev, irq_ptp_rdy, NULL,
						ocelot_ptp_rdy_irq_handler,
						IRQF_ONESHOT, "ptp ready",
						ocelot);
		if (err)
			goto out_free_devlink;

		/* Both the PTP interrupt and the PTP bank are available */
		ocelot->ptp = 1;
	}

	ports = of_get_child_by_name(np, "ethernet-ports");
	if (!ports) {
		dev_err(ocelot->dev, "no ethernet-ports child node found\n");
		err = -ENODEV;
		goto out_free_devlink;
	}

	ocelot->num_phys_ports = of_get_child_count(ports);
	ocelot->num_flooding_pgids = 1;

	ocelot->vcap = vsc7514_vcap_props;

	ocelot->vcap_pol.base = VSC7514_VCAP_POLICER_BASE;
	ocelot->vcap_pol.max = VSC7514_VCAP_POLICER_MAX;

	ocelot->npi = -1;

	err = ocelot_init(ocelot);
	if (err)
		goto out_put_ports;

	err = mscc_ocelot_init_ports(pdev, ports);
	if (err)
		goto out_ocelot_devlink_unregister;

	if (ocelot->fdma)
		ocelot_fdma_start(ocelot);

	err = ocelot_devlink_sb_register(ocelot);
	if (err)
		goto out_ocelot_release_ports;

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

	of_node_put(ports);
	devlink_register(devlink);

	dev_info(&pdev->dev, "Ocelot switch probed\n");

	return 0;

out_ocelot_release_ports:
	mscc_ocelot_release_ports(ocelot);
	mscc_ocelot_teardown_devlink_ports(ocelot);
out_ocelot_devlink_unregister:
	ocelot_deinit(ocelot);
out_put_ports:
	of_node_put(ports);
out_free_devlink:
	devlink_free(devlink);
	return err;
}

static int mscc_ocelot_remove(struct platform_device *pdev)
{
	struct ocelot *ocelot = platform_get_drvdata(pdev);

	if (ocelot->fdma)
		ocelot_fdma_deinit(ocelot);
	devlink_unregister(ocelot->devlink);
	ocelot_deinit_timestamp(ocelot);
	ocelot_devlink_sb_unregister(ocelot);
	mscc_ocelot_release_ports(ocelot);
	mscc_ocelot_teardown_devlink_ports(ocelot);
	ocelot_deinit(ocelot);
	unregister_switchdev_blocking_notifier(&ocelot_switchdev_blocking_nb);
	unregister_switchdev_notifier(&ocelot_switchdev_nb);
	unregister_netdevice_notifier(&ocelot_netdevice_nb);
	devlink_free(ocelot->devlink);

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
