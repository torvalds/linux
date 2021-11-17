/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Broadcom Starfighter 2 switch register defines
 *
 * Copyright (C) 2014, Broadcom Corporation
 */
#ifndef __BCM_SF2_REGS_H
#define __BCM_SF2_REGS_H

/* Register set relative to 'REG' */

enum bcm_sf2_reg_offs {
	REG_SWITCH_CNTRL = 0,
	REG_SWITCH_STATUS,
	REG_DIR_DATA_WRITE,
	REG_DIR_DATA_READ,
	REG_SWITCH_REVISION,
	REG_PHY_REVISION,
	REG_SPHY_CNTRL,
	REG_CROSSBAR,
	REG_RGMII_0_CNTRL,
	REG_RGMII_1_CNTRL,
	REG_RGMII_2_CNTRL,
	REG_RGMII_11_CNTRL,
	REG_LED_0_CNTRL,
	REG_LED_1_CNTRL,
	REG_LED_2_CNTRL,
	REG_SWITCH_REG_MAX,
};

/* Relative to REG_SWITCH_CNTRL */
#define  MDIO_MASTER_SEL		(1 << 0)

/* Relative to REG_SWITCH_REVISION */
#define  SF2_REV_MASK			0xffff
#define  SWITCH_TOP_REV_SHIFT		16
#define  SWITCH_TOP_REV_MASK		0xffff

/* Relative to REG_PHY_REVISION */
#define  PHY_REVISION_MASK		0xffff

/* Relative to REG_SPHY_CNTRL */
#define  IDDQ_BIAS			(1 << 0)
#define  EXT_PWR_DOWN			(1 << 1)
#define  FORCE_DLL_EN			(1 << 2)
#define  IDDQ_GLOBAL_PWR		(1 << 3)
#define  CK25_DIS			(1 << 4)
#define  PHY_RESET			(1 << 5)
#define  PHY_PHYAD_SHIFT		8
#define  PHY_PHYAD_MASK			0x1F

/* Relative to REG_CROSSBAR */
#define CROSSBAR_BCM4908_INT_P7		0
#define CROSSBAR_BCM4908_INT_RUNNER	1
#define CROSSBAR_BCM4908_EXT_SERDES	0
#define CROSSBAR_BCM4908_EXT_GPHY4	1
#define CROSSBAR_BCM4908_EXT_RGMII	2

/* Relative to REG_RGMII_CNTRL */
#define  RGMII_MODE_EN			(1 << 0)
#define  ID_MODE_DIS			(1 << 1)
#define  PORT_MODE_SHIFT		2
#define  INT_EPHY			(0 << PORT_MODE_SHIFT)
#define  INT_GPHY			(1 << PORT_MODE_SHIFT)
#define  EXT_EPHY			(2 << PORT_MODE_SHIFT)
#define  EXT_GPHY			(3 << PORT_MODE_SHIFT)
#define  EXT_REVMII			(4 << PORT_MODE_SHIFT)
#define  PORT_MODE_MASK			0x7
#define  RVMII_REF_SEL			(1 << 5)
#define  RX_PAUSE_EN			(1 << 6)
#define  TX_PAUSE_EN			(1 << 7)
#define  TX_CLK_STOP_EN			(1 << 8)
#define  LPI_COUNT_SHIFT		9
#define  LPI_COUNT_MASK			0x3F

#define REG_LED_CNTRL(x)		(REG_LED_0_CNTRL + (x))

#define  SPDLNK_SRC_SEL			(1 << 24)

/* Register set relative to 'INTRL2_0' and 'INTRL2_1' */
#define INTRL2_CPU_STATUS		0x00
#define INTRL2_CPU_SET			0x04
#define INTRL2_CPU_CLEAR		0x08
#define INTRL2_CPU_MASK_STATUS		0x0c
#define INTRL2_CPU_MASK_SET		0x10
#define INTRL2_CPU_MASK_CLEAR		0x14

/* Shared INTRL2_0 and INTRL2_ interrupt sources macros */
#define P_LINK_UP_IRQ(x)		(1 << (0 + (x)))
#define P_LINK_DOWN_IRQ(x)		(1 << (1 + (x)))
#define P_ENERGY_ON_IRQ(x)		(1 << (2 + (x)))
#define P_ENERGY_OFF_IRQ(x)		(1 << (3 + (x)))
#define P_GPHY_IRQ(x)			(1 << (4 + (x)))
#define P_NUM_IRQ			5
#define P_IRQ_MASK(x)			(P_LINK_UP_IRQ((x)) | \
					 P_LINK_DOWN_IRQ((x)) | \
					 P_ENERGY_ON_IRQ((x)) | \
					 P_ENERGY_OFF_IRQ((x)) | \
					 P_GPHY_IRQ((x)))

/* INTRL2_0 interrupt sources */
#define P0_IRQ_OFF			0
#define MEM_DOUBLE_IRQ			(1 << 5)
#define EEE_LPI_IRQ			(1 << 6)
#define P5_CPU_WAKE_IRQ			(1 << 7)
#define P8_CPU_WAKE_IRQ			(1 << 8)
#define P7_CPU_WAKE_IRQ			(1 << 9)
#define IEEE1588_IRQ			(1 << 10)
#define MDIO_ERR_IRQ			(1 << 11)
#define MDIO_DONE_IRQ			(1 << 12)
#define GISB_ERR_IRQ			(1 << 13)
#define UBUS_ERR_IRQ			(1 << 14)
#define FAILOVER_ON_IRQ			(1 << 15)
#define FAILOVER_OFF_IRQ		(1 << 16)
#define TCAM_SOFT_ERR_IRQ		(1 << 17)

/* INTRL2_1 interrupt sources */
#define P7_IRQ_OFF			0
#define P_IRQ_OFF(x)			((6 - (x)) * P_NUM_IRQ)

/* Register set relative to 'ACB' */
#define ACB_CONTROL			0x00
#define  ACB_EN				(1 << 0)
#define  ACB_ALGORITHM			(1 << 1)
#define  ACB_FLUSH_SHIFT		2
#define  ACB_FLUSH_MASK			0x3

#define ACB_QUEUE_0_CFG			0x08
#define  XOFF_THRESHOLD_MASK		0x7ff
#define  XON_EN				(1 << 11)
#define  TOTAL_XOFF_THRESHOLD_SHIFT	12
#define  TOTAL_XOFF_THRESHOLD_MASK	0x7ff
#define  TOTAL_XOFF_EN			(1 << 23)
#define  TOTAL_XON_EN			(1 << 24)
#define  PKTLEN_SHIFT			25
#define  PKTLEN_MASK			0x3f
#define ACB_QUEUE_CFG(x)		(ACB_QUEUE_0_CFG + ((x) * 0x4))

/* Register set relative to 'CORE' */
#define CORE_G_PCTL_PORT0		0x00000
#define CORE_G_PCTL_PORT(x)		(CORE_G_PCTL_PORT0 + (x * 0x4))
#define CORE_IMP_CTL			0x00020
#define  RX_DIS				(1 << 0)
#define  TX_DIS				(1 << 1)
#define  RX_BCST_EN			(1 << 2)
#define  RX_MCST_EN			(1 << 3)
#define  RX_UCST_EN			(1 << 4)

#define CORE_SWMODE			0x0002c
#define  SW_FWDG_MODE			(1 << 0)
#define  SW_FWDG_EN			(1 << 1)
#define  RTRY_LMT_DIS			(1 << 2)

#define CORE_STS_OVERRIDE_IMP		0x00038
#define  GMII_SPEED_UP_2G		(1 << 6)
#define  MII_SW_OR			(1 << 7)

/* Alternate layout for e.g: 7278 */
#define CORE_STS_OVERRIDE_IMP2		0x39040

#define CORE_NEW_CTRL			0x00084
#define  IP_MC				(1 << 0)
#define  OUTRANGEERR_DISCARD		(1 << 1)
#define  INRANGEERR_DISCARD		(1 << 2)
#define  CABLE_DIAG_LEN			(1 << 3)
#define  OVERRIDE_AUTO_PD_WAR		(1 << 4)
#define  EN_AUTO_PD_WAR			(1 << 5)
#define  UC_FWD_EN			(1 << 6)
#define  MC_FWD_EN			(1 << 7)

#define CORE_SWITCH_CTRL		0x00088
#define  MII_DUMB_FWDG_EN		(1 << 6)

#define CORE_DIS_LEARN			0x000f0

#define CORE_SFT_LRN_CTRL		0x000f8
#define  SW_LEARN_CNTL(x)		(1 << (x))

#define CORE_STS_OVERRIDE_GMIIP_PORT(x)	(0x160 + (x) * 4)
#define CORE_STS_OVERRIDE_GMIIP2_PORT(x) (0x39000 + (x) * 8)
#define  LINK_STS			(1 << 0)
#define  DUPLX_MODE			(1 << 1)
#define  SPEED_SHIFT			2
#define  SPEED_MASK			0x3
#define  RXFLOW_CNTL			(1 << 4)
#define  TXFLOW_CNTL			(1 << 5)
#define  SW_OVERRIDE			(1 << 6)

#define CORE_WATCHDOG_CTRL		0x001e4
#define  SOFTWARE_RESET			(1 << 7)
#define  EN_CHIP_RST			(1 << 6)
#define  EN_SW_RESET			(1 << 4)

#define CORE_FAST_AGE_CTRL		0x00220
#define  EN_FAST_AGE_STATIC		(1 << 0)
#define  EN_AGE_DYNAMIC			(1 << 1)
#define  EN_AGE_PORT			(1 << 2)
#define  EN_AGE_VLAN			(1 << 3)
#define  EN_AGE_SPT			(1 << 4)
#define  EN_AGE_MCAST			(1 << 5)
#define  FAST_AGE_STR_DONE		(1 << 7)

#define CORE_FAST_AGE_PORT		0x00224
#define  AGE_PORT_MASK			0xf

#define CORE_FAST_AGE_VID		0x00228
#define  AGE_VID_MASK			0x3fff

#define CORE_LNKSTS			0x00400
#define  LNK_STS_MASK			0x1ff

#define CORE_SPDSTS			0x00410
#define  SPDSTS_10			0
#define  SPDSTS_100			1
#define  SPDSTS_1000			2
#define  SPDSTS_SHIFT			2
#define  SPDSTS_MASK			0x3

#define CORE_DUPSTS			0x00420
#define  CORE_DUPSTS_MASK		0x1ff

#define CORE_PAUSESTS			0x00428
#define  PAUSESTS_TX_PAUSE_SHIFT	9

#define CORE_GMNCFGCFG			0x0800
#define  RST_MIB_CNT			(1 << 0)
#define  RXBPDU_EN			(1 << 1)

#define CORE_IMP0_PRT_ID		0x0804

#define CORE_RST_MIB_CNT_EN		0x0950

#define CORE_ARLA_VTBL_RWCTRL		0x1600
#define  ARLA_VTBL_CMD_WRITE		0
#define  ARLA_VTBL_CMD_READ		1
#define  ARLA_VTBL_CMD_CLEAR		2
#define  ARLA_VTBL_STDN			(1 << 7)

#define CORE_ARLA_VTBL_ADDR		0x1604
#define  VTBL_ADDR_INDEX_MASK		0xfff

#define CORE_ARLA_VTBL_ENTRY		0x160c
#define  FWD_MAP_MASK			0x1ff
#define  UNTAG_MAP_MASK			0x1ff
#define  UNTAG_MAP_SHIFT		9
#define  MSTP_INDEX_MASK		0x7
#define  MSTP_INDEX_SHIFT		18
#define  FWD_MODE			(1 << 21)

#define CORE_MEM_PSM_VDD_CTRL		0x2380
#define  P_TXQ_PSM_VDD_SHIFT		2
#define  P_TXQ_PSM_VDD_MASK		0x3
#define  P_TXQ_PSM_VDD(x)		(P_TXQ_PSM_VDD_MASK << \
					((x) * P_TXQ_PSM_VDD_SHIFT))

#define CORE_PORT_TC2_QOS_MAP_PORT(x)	(0xc1c0 + ((x) * 0x10))
#define  PRT_TO_QID_MASK		0x3
#define  PRT_TO_QID_SHIFT		3

#define CORE_PORT_VLAN_CTL_PORT(x)	(0xc400 + ((x) * 0x8))
#define  PORT_VLAN_CTRL_MASK		0x1ff

#define CORE_TXQ_THD_PAUSE_QN_PORT_0	0x2c80
#define  TXQ_PAUSE_THD_MASK		0x7ff
#define CORE_TXQ_THD_PAUSE_QN_PORT(x)	(CORE_TXQ_THD_PAUSE_QN_PORT_0 + \
					(x) * 0x8)

#define CORE_DEFAULT_1Q_TAG_P(x)	(0xd040 + ((x) * 8))
#define  CFI_SHIFT			12
#define  PRI_SHIFT			13
#define  PRI_MASK			0x7

#define CORE_JOIN_ALL_VLAN_EN		0xd140

#define CORE_CFP_ACC			0x28000
#define  OP_STR_DONE			(1 << 0)
#define  OP_SEL_SHIFT			1
#define  OP_SEL_READ			(1 << OP_SEL_SHIFT)
#define  OP_SEL_WRITE			(2 << OP_SEL_SHIFT)
#define  OP_SEL_SEARCH			(4 << OP_SEL_SHIFT)
#define  OP_SEL_MASK			(7 << OP_SEL_SHIFT)
#define  CFP_RAM_CLEAR			(1 << 4)
#define  RAM_SEL_SHIFT			10
#define  TCAM_SEL			(1 << RAM_SEL_SHIFT)
#define  ACT_POL_RAM			(2 << RAM_SEL_SHIFT)
#define  RATE_METER_RAM			(4 << RAM_SEL_SHIFT)
#define  GREEN_STAT_RAM			(8 << RAM_SEL_SHIFT)
#define  YELLOW_STAT_RAM		(16 << RAM_SEL_SHIFT)
#define  RED_STAT_RAM			(24 << RAM_SEL_SHIFT)
#define  RAM_SEL_MASK			(0x1f << RAM_SEL_SHIFT)
#define  TCAM_RESET			(1 << 15)
#define  XCESS_ADDR_SHIFT		16
#define  XCESS_ADDR_MASK		0xff
#define  SEARCH_STS			(1 << 27)
#define  RD_STS_SHIFT			28
#define  RD_STS_TCAM			(1 << RD_STS_SHIFT)
#define  RD_STS_ACT_POL_RAM		(2 << RD_STS_SHIFT)
#define  RD_STS_RATE_METER_RAM		(4 << RD_STS_SHIFT)
#define  RD_STS_STAT_RAM		(8 << RD_STS_SHIFT)

#define CORE_CFP_RATE_METER_GLOBAL_CTL	0x28010

#define CORE_CFP_DATA_PORT_0		0x28040
#define CORE_CFP_DATA_PORT(x)		(CORE_CFP_DATA_PORT_0 + \
					(x) * 0x10)

/* UDF_DATA7 */
#define L3_FRAMING_SHIFT		24
#define L3_FRAMING_MASK			(0x3 << L3_FRAMING_SHIFT)
#define IPTOS_SHIFT			16
#define IPTOS_MASK			0xff
#define IPPROTO_SHIFT			8
#define IPPROTO_MASK			(0xff << IPPROTO_SHIFT)
#define IP_FRAG_SHIFT			7
#define IP_FRAG				(1 << IP_FRAG_SHIFT)

/* UDF_DATA0 */
#define  SLICE_VALID			3
#define  SLICE_NUM_SHIFT		2
#define  SLICE_NUM(x)			((x) << SLICE_NUM_SHIFT)
#define  SLICE_NUM_MASK			0x3

#define CORE_CFP_MASK_PORT_0		0x280c0

#define CORE_CFP_MASK_PORT(x)		(CORE_CFP_MASK_PORT_0 + \
					(x) * 0x10)

#define CORE_ACT_POL_DATA0		0x28140
#define  VLAN_BYP			(1 << 0)
#define  EAP_BYP			(1 << 1)
#define  STP_BYP			(1 << 2)
#define  REASON_CODE_SHIFT		3
#define  REASON_CODE_MASK		0x3f
#define  LOOP_BK_EN			(1 << 9)
#define  NEW_TC_SHIFT			10
#define  NEW_TC_MASK			0x7
#define  CHANGE_TC			(1 << 13)
#define  DST_MAP_IB_SHIFT		14
#define  DST_MAP_IB_MASK		0x1ff
#define  CHANGE_FWRD_MAP_IB_SHIFT	24
#define  CHANGE_FWRD_MAP_IB_MASK	0x3
#define  CHANGE_FWRD_MAP_IB_NO_DEST	(0 << CHANGE_FWRD_MAP_IB_SHIFT)
#define  CHANGE_FWRD_MAP_IB_REM_ARL	(1 << CHANGE_FWRD_MAP_IB_SHIFT)
#define  CHANGE_FWRD_MAP_IB_REP_ARL	(2 << CHANGE_FWRD_MAP_IB_SHIFT)
#define  CHANGE_FWRD_MAP_IB_ADD_DST	(3 << CHANGE_FWRD_MAP_IB_SHIFT)
#define  NEW_DSCP_IB_SHIFT		26
#define  NEW_DSCP_IB_MASK		0x3f

#define CORE_ACT_POL_DATA1		0x28150
#define  CHANGE_DSCP_IB			(1 << 0)
#define  DST_MAP_OB_SHIFT		1
#define  DST_MAP_OB_MASK		0x3ff
#define  CHANGE_FWRD_MAP_OB_SHIT	11
#define  CHANGE_FWRD_MAP_OB_MASK	0x3
#define  NEW_DSCP_OB_SHIFT		13
#define  NEW_DSCP_OB_MASK		0x3f
#define  CHANGE_DSCP_OB			(1 << 19)
#define  CHAIN_ID_SHIFT			20
#define  CHAIN_ID_MASK			0xff
#define  CHANGE_COLOR			(1 << 28)
#define  NEW_COLOR_SHIFT		29
#define  NEW_COLOR_MASK			0x3
#define  NEW_COLOR_GREEN		(0 << NEW_COLOR_SHIFT)
#define  NEW_COLOR_YELLOW		(1 << NEW_COLOR_SHIFT)
#define  NEW_COLOR_RED			(2 << NEW_COLOR_SHIFT)
#define  RED_DEFAULT			(1 << 31)

#define CORE_ACT_POL_DATA2		0x28160
#define  MAC_LIMIT_BYPASS		(1 << 0)
#define  CHANGE_TC_O			(1 << 1)
#define  NEW_TC_O_SHIFT			2
#define  NEW_TC_O_MASK			0x7
#define  SPCP_RMK_DISABLE		(1 << 5)
#define  CPCP_RMK_DISABLE		(1 << 6)
#define  DEI_RMK_DISABLE		(1 << 7)

#define CORE_RATE_METER0		0x28180
#define  COLOR_MODE			(1 << 0)
#define  POLICER_ACTION			(1 << 1)
#define  COUPLING_FLAG			(1 << 2)
#define  POLICER_MODE_SHIFT		3
#define  POLICER_MODE_MASK		0x3
#define  POLICER_MODE_RFC2698		(0 << POLICER_MODE_SHIFT)
#define  POLICER_MODE_RFC4115		(1 << POLICER_MODE_SHIFT)
#define  POLICER_MODE_MEF		(2 << POLICER_MODE_SHIFT)
#define  POLICER_MODE_DISABLE		(3 << POLICER_MODE_SHIFT)

#define CORE_RATE_METER1		0x28190
#define  EIR_TK_BKT_MASK		0x7fffff

#define CORE_RATE_METER2		0x281a0
#define  EIR_BKT_SIZE_MASK		0xfffff

#define CORE_RATE_METER3		0x281b0
#define  EIR_REF_CNT_MASK		0x7ffff

#define CORE_RATE_METER4		0x281c0
#define  CIR_TK_BKT_MASK		0x7fffff

#define CORE_RATE_METER5		0x281d0
#define  CIR_BKT_SIZE_MASK		0xfffff

#define CORE_RATE_METER6		0x281e0
#define  CIR_REF_CNT_MASK		0x7ffff

#define CORE_STAT_GREEN_CNTR		0x28200
#define CORE_STAT_YELLOW_CNTR		0x28210
#define CORE_STAT_RED_CNTR		0x28220

#define CORE_CFP_CTL_REG		0x28400
#define  CFP_EN_MAP_MASK		0x1ff

/* IPv4 slices, 3 of them */
#define CORE_UDF_0_A_0_8_PORT_0		0x28440
#define  CFG_UDF_OFFSET_MASK		0x1f
#define  CFG_UDF_OFFSET_BASE_SHIFT	5
#define  CFG_UDF_SOF			(0 << CFG_UDF_OFFSET_BASE_SHIFT)
#define  CFG_UDF_EOL2			(2 << CFG_UDF_OFFSET_BASE_SHIFT)
#define  CFG_UDF_EOL3			(3 << CFG_UDF_OFFSET_BASE_SHIFT)

/* IPv6 slices */
#define CORE_UDF_0_B_0_8_PORT_0		0x28500

/* IPv6 chained slices */
#define CORE_UDF_0_D_0_11_PORT_0	0x28680

/* Number of slices for IPv4, IPv6 and non-IP */
#define UDF_NUM_SLICES			4
#define UDFS_PER_SLICE			9

/* Spacing between different slices */
#define UDF_SLICE_OFFSET		0x40

#define CFP_NUM_RULES			256

/* Number of egress queues per port */
#define SF2_NUM_EGRESS_QUEUES		8

#endif /* __BCM_SF2_REGS_H */
