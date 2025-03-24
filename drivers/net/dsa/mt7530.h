/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017 Sean Wang <sean.wang@mediatek.com>
 */

#ifndef __MT7530_H
#define __MT7530_H

#define MT7530_NUM_PORTS		7
#define MT7530_NUM_PHYS			5
#define MT7530_NUM_FDB_RECORDS		2048
#define MT7530_ALL_MEMBERS		0xff

#define MTK_HDR_LEN	4
#define MT7530_MAX_MTU	(15 * 1024 - ETH_HLEN - ETH_FCS_LEN - MTK_HDR_LEN)

enum mt753x_id {
	ID_MT7530 = 0,
	ID_MT7621 = 1,
	ID_MT7531 = 2,
	ID_MT7988 = 3,
	ID_EN7581 = 4,
};

#define	NUM_TRGMII_CTRL			5

#define TRGMII_BASE(x)			(0x10000 + (x))

/* Registers to ethsys access */
#define ETHSYS_CLKCFG0			0x2c
#define  ETHSYS_TRGMII_CLK_SEL362_5	BIT(11)

#define SYSC_REG_RSTCTRL		0x34
#define  RESET_MCM			BIT(2)

/* Register for ARL global control */
#define MT753X_AGC			0xc
#define  LOCAL_EN			BIT(7)

/* Register for MAC forward control */
#define MT753X_MFC			0x10
#define  BC_FFP_MASK			GENMASK(31, 24)
#define  BC_FFP(x)			FIELD_PREP(BC_FFP_MASK, x)
#define  UNM_FFP_MASK			GENMASK(23, 16)
#define  UNM_FFP(x)			FIELD_PREP(UNM_FFP_MASK, x)
#define  UNU_FFP_MASK			GENMASK(15, 8)
#define  UNU_FFP(x)			FIELD_PREP(UNU_FFP_MASK, x)
#define  MT7530_CPU_EN			BIT(7)
#define  MT7530_CPU_PORT_MASK		GENMASK(6, 4)
#define  MT7530_CPU_PORT(x)		FIELD_PREP(MT7530_CPU_PORT_MASK, x)
#define  MT7530_MIRROR_EN		BIT(3)
#define  MT7530_MIRROR_PORT_MASK	GENMASK(2, 0)
#define  MT7530_MIRROR_PORT_GET(x)	FIELD_GET(MT7530_MIRROR_PORT_MASK, x)
#define  MT7530_MIRROR_PORT_SET(x)	FIELD_PREP(MT7530_MIRROR_PORT_MASK, x)
#define  MT7531_QRY_FFP_MASK		GENMASK(7, 0)
#define  MT7531_QRY_FFP(x)		FIELD_PREP(MT7531_QRY_FFP_MASK, x)

/* Register for CPU forward control */
#define MT7531_CFC			0x4
#define  MT7531_MIRROR_EN		BIT(19)
#define  MT7531_MIRROR_PORT_MASK	GENMASK(18, 16)
#define  MT7531_MIRROR_PORT_GET(x)	FIELD_GET(MT7531_MIRROR_PORT_MASK, x)
#define  MT7531_MIRROR_PORT_SET(x)	FIELD_PREP(MT7531_MIRROR_PORT_MASK, x)
#define  MT7531_CPU_PMAP_MASK		GENMASK(7, 0)
#define  MT7531_CPU_PMAP(x)		FIELD_PREP(MT7531_CPU_PMAP_MASK, x)

#define MT753X_MIRROR_REG(id)		((id == ID_MT7531 || \
					  id == ID_MT7988 || \
					  id == ID_EN7581) ? \
					 MT7531_CFC : MT753X_MFC)

#define MT753X_MIRROR_EN(id)		((id == ID_MT7531 || \
					  id == ID_MT7988 || \
					  id == ID_EN7581) ? \
					 MT7531_MIRROR_EN : MT7530_MIRROR_EN)

#define MT753X_MIRROR_PORT_MASK(id)	((id == ID_MT7531 || \
					  id == ID_MT7988 || \
					  id == ID_EN7581) ? \
					 MT7531_MIRROR_PORT_MASK : \
					 MT7530_MIRROR_PORT_MASK)

#define MT753X_MIRROR_PORT_GET(id, val)	((id == ID_MT7531 || \
					  id == ID_MT7988 || \
					  id == ID_EN7581) ? \
					 MT7531_MIRROR_PORT_GET(val) : \
					 MT7530_MIRROR_PORT_GET(val))

#define MT753X_MIRROR_PORT_SET(id, val)	((id == ID_MT7531 || \
					  id == ID_MT7988 || \
					  id == ID_EN7581) ? \
					 MT7531_MIRROR_PORT_SET(val) : \
					 MT7530_MIRROR_PORT_SET(val))

/* Register for BPDU and PAE frame control */
#define MT753X_BPC			0x24
#define  PAE_BPDU_FR			BIT(25)
#define  PAE_EG_TAG_MASK		GENMASK(24, 22)
#define  PAE_EG_TAG(x)			FIELD_PREP(PAE_EG_TAG_MASK, x)
#define  PAE_PORT_FW_MASK		GENMASK(18, 16)
#define  PAE_PORT_FW(x)			FIELD_PREP(PAE_PORT_FW_MASK, x)
#define  BPDU_EG_TAG_MASK		GENMASK(8, 6)
#define  BPDU_EG_TAG(x)			FIELD_PREP(BPDU_EG_TAG_MASK, x)
#define  BPDU_PORT_FW_MASK		GENMASK(2, 0)

/* Register for 01-80-C2-00-00-[01,02] MAC DA frame control */
#define MT753X_RGAC1			0x28
#define  R02_BPDU_FR			BIT(25)
#define  R02_EG_TAG_MASK		GENMASK(24, 22)
#define  R02_EG_TAG(x)			FIELD_PREP(R02_EG_TAG_MASK, x)
#define  R02_PORT_FW_MASK		GENMASK(18, 16)
#define  R02_PORT_FW(x)			FIELD_PREP(R02_PORT_FW_MASK, x)
#define  R01_BPDU_FR			BIT(9)
#define  R01_EG_TAG_MASK		GENMASK(8, 6)
#define  R01_EG_TAG(x)			FIELD_PREP(R01_EG_TAG_MASK, x)
#define  R01_PORT_FW_MASK		GENMASK(2, 0)

/* Register for 01-80-C2-00-00-[03,0E] MAC DA frame control */
#define MT753X_RGAC2			0x2c
#define  R0E_BPDU_FR			BIT(25)
#define  R0E_EG_TAG_MASK		GENMASK(24, 22)
#define  R0E_EG_TAG(x)			FIELD_PREP(R0E_EG_TAG_MASK, x)
#define  R0E_PORT_FW_MASK		GENMASK(18, 16)
#define  R0E_PORT_FW(x)			FIELD_PREP(R0E_PORT_FW_MASK, x)
#define  R03_BPDU_FR			BIT(9)
#define  R03_EG_TAG_MASK		GENMASK(8, 6)
#define  R03_EG_TAG(x)			FIELD_PREP(R03_EG_TAG_MASK, x)
#define  R03_PORT_FW_MASK		GENMASK(2, 0)

enum mt753x_to_cpu_fw {
	TO_CPU_FW_SYSTEM_DEFAULT,
	TO_CPU_FW_CPU_EXCLUDE = 4,
	TO_CPU_FW_CPU_INCLUDE = 5,
	TO_CPU_FW_CPU_ONLY = 6,
	TO_CPU_FW_DROP = 7,
};

/* Registers for address table access */
#define MT7530_ATA1			0x74
#define  STATIC_EMP			0
#define  STATIC_ENT			3
#define MT7530_ATA2			0x78
#define  ATA2_IVL			BIT(15)
#define  ATA2_FID(x)			(((x) & 0x7) << 12)

/* Register for address table write data */
#define MT7530_ATWD			0x7c

/* Register for address table control */
#define MT7530_ATC			0x80
#define  ATC_HASH			(((x) & 0xfff) << 16)
#define  ATC_BUSY			BIT(15)
#define  ATC_SRCH_END			BIT(14)
#define  ATC_SRCH_HIT			BIT(13)
#define  ATC_INVALID			BIT(12)
#define  ATC_MAT(x)			(((x) & 0xf) << 8)
#define  ATC_MAT_MACTAB			ATC_MAT(0)

enum mt7530_fdb_cmd {
	MT7530_FDB_READ	= 0,
	MT7530_FDB_WRITE = 1,
	MT7530_FDB_FLUSH = 2,
	MT7530_FDB_START = 4,
	MT7530_FDB_NEXT = 5,
};

/* Registers for table search read address */
#define MT7530_TSRA1			0x84
#define  MAC_BYTE_0			24
#define  MAC_BYTE_1			16
#define  MAC_BYTE_2			8
#define  MAC_BYTE_3			0
#define  MAC_BYTE_MASK			0xff

#define MT7530_TSRA2			0x88
#define  MAC_BYTE_4			24
#define  MAC_BYTE_5			16
#define  CVID				0
#define  CVID_MASK			0xfff

#define MT7530_ATRD			0x8C
#define	 AGE_TIMER			24
#define  AGE_TIMER_MASK			0xff
#define  PORT_MAP			4
#define  PORT_MAP_MASK			0xff
#define  ENT_STATUS			2
#define  ENT_STATUS_MASK		0x3

/* Register for vlan table control */
#define MT7530_VTCR			0x90
#define  VTCR_BUSY			BIT(31)
#define  VTCR_INVALID			BIT(16)
#define  VTCR_FUNC(x)			(((x) & 0xf) << 12)
#define  VTCR_VID			((x) & 0xfff)

enum mt7530_vlan_cmd {
	/* Read/Write the specified VID entry from VAWD register based
	 * on VID.
	 */
	MT7530_VTCR_RD_VID = 0,
	MT7530_VTCR_WR_VID = 1,
};

/* Register for setup vlan and acl write data */
#define MT7530_VAWD1			0x94
#define  PORT_STAG			BIT(31)
/* Independent VLAN Learning */
#define  IVL_MAC			BIT(30)
/* Egress Tag Consistent */
#define  EG_CON				BIT(29)
/* Per VLAN Egress Tag Control */
#define  VTAG_EN			BIT(28)
/* VLAN Member Control */
#define  PORT_MEM(x)			(((x) & 0xff) << 16)
/* Filter ID */
#define  FID(x)				(((x) & 0x7) << 1)
/* VLAN Entry Valid */
#define  VLAN_VALID			BIT(0)
#define  PORT_MEM_SHFT			16
#define  PORT_MEM_MASK			0xff

enum mt7530_fid {
	FID_STANDALONE = 0,
	FID_BRIDGED = 1,
};

#define MT7530_VAWD2			0x98
/* Egress Tag Control */
#define  ETAG_CTRL_P(p, x)		(((x) & 0x3) << ((p) << 1))
#define  ETAG_CTRL_P_MASK(p)		ETAG_CTRL_P(p, 3)

enum mt7530_vlan_egress_attr {
	MT7530_VLAN_EGRESS_UNTAG = 0,
	MT7530_VLAN_EGRESS_TAG = 2,
	MT7530_VLAN_EGRESS_STACK = 3,
};

/* Register for address age control */
#define MT7530_AAC			0xa0
/* Disable ageing */
#define  AGE_DIS			BIT(20)
/* Age count */
#define  AGE_CNT_MASK			GENMASK(19, 12)
#define  AGE_CNT_MAX			0xff
#define  AGE_CNT(x)			(AGE_CNT_MASK & ((x) << 12))
/* Age unit */
#define  AGE_UNIT_MASK			GENMASK(11, 0)
#define  AGE_UNIT_MAX			0xfff
#define  AGE_UNIT(x)			(AGE_UNIT_MASK & (x))

#define MT753X_ERLCR_P(x)		(0x1040 + ((x) * 0x100))
#define  ERLCR_CIR_MASK			GENMASK(31, 16)
#define  ERLCR_EN_MASK			BIT(15)
#define  ERLCR_EXP_MASK			GENMASK(11, 8)
#define  ERLCR_TBF_MODE_MASK		BIT(7)
#define  ERLCR_MANT_MASK		GENMASK(6, 0)

#define MT753X_GERLCR			0x10e0
#define  EGR_BC_MASK			GENMASK(7, 0)
#define  EGR_BC_CRC			0x4	/* crc */
#define  EGR_BC_CRC_IPG_PREAMBLE	0x18	/* crc + ipg + preamble */

/* Register for port STP state control */
#define MT7530_SSP_P(x)			(0x2000 + ((x) * 0x100))
#define  FID_PST(fid, state)		(((state) & 0x3) << ((fid) * 2))
#define  FID_PST_MASK(fid)		FID_PST(fid, 0x3)

enum mt7530_stp_state {
	MT7530_STP_DISABLED = 0,
	MT7530_STP_BLOCKING = 1,
	MT7530_STP_LISTENING = 1,
	MT7530_STP_LEARNING = 2,
	MT7530_STP_FORWARDING  = 3
};

/* Register for port control */
#define MT7530_PCR_P(x)			(0x2004 + ((x) * 0x100))
#define  PORT_TX_MIR			BIT(9)
#define  PORT_RX_MIR			BIT(8)
#define  PORT_VLAN(x)			((x) & 0x3)

enum mt7530_port_mode {
	/* Port Matrix Mode: Frames are forwarded by the PCR_MATRIX members. */
	MT7530_PORT_MATRIX_MODE = PORT_VLAN(0),

	/* Fallback Mode: Forward received frames with ingress ports that do
	 * not belong to the VLAN member. Frames whose VID is not listed on
	 * the VLAN table are forwarded by the PCR_MATRIX members.
	 */
	MT7530_PORT_FALLBACK_MODE = PORT_VLAN(1),

	/* Security Mode: Discard any frame due to ingress membership
	 * violation or VID missed on the VLAN table.
	 */
	MT7530_PORT_SECURITY_MODE = PORT_VLAN(3),
};

#define  PCR_MATRIX(x)			(((x) & 0xff) << 16)
#define  PORT_PRI(x)			(((x) & 0x7) << 24)
#define  EG_TAG(x)			(((x) & 0x3) << 28)
#define  PCR_MATRIX_MASK		PCR_MATRIX(0xff)
#define  PCR_MATRIX_CLR			PCR_MATRIX(0)
#define  PCR_PORT_VLAN_MASK		PORT_VLAN(3)

/* Register for port security control */
#define MT7530_PSC_P(x)			(0x200c + ((x) * 0x100))
#define  SA_DIS				BIT(4)

/* Register for port vlan control */
#define MT7530_PVC_P(x)			(0x2010 + ((x) * 0x100))
#define  PORT_SPEC_TAG			BIT(5)
#define  PVC_EG_TAG(x)			(((x) & 0x7) << 8)
#define  PVC_EG_TAG_MASK		PVC_EG_TAG(7)
#define  VLAN_ATTR(x)			(((x) & 0x3) << 6)
#define  VLAN_ATTR_MASK			VLAN_ATTR(3)
#define  ACC_FRM_MASK			GENMASK(1, 0)

enum mt7530_vlan_port_eg_tag {
	MT7530_VLAN_EG_DISABLED = 0,
	MT7530_VLAN_EG_CONSISTENT = 1,
	MT7530_VLAN_EG_UNTAGGED = 4,
};

enum mt7530_vlan_port_attr {
	MT7530_VLAN_USER = 0,
	MT7530_VLAN_TRANSPARENT = 3,
};

enum mt7530_vlan_port_acc_frm {
	MT7530_VLAN_ACC_ALL = 0,
	MT7530_VLAN_ACC_TAGGED = 1,
	MT7530_VLAN_ACC_UNTAGGED = 2,
};

#define  STAG_VPID			(((x) & 0xffff) << 16)

/* Register for port port-and-protocol based vlan 1 control */
#define MT7530_PPBV1_P(x)		(0x2014 + ((x) * 0x100))
#define  G0_PORT_VID(x)			(((x) & 0xfff) << 0)
#define  G0_PORT_VID_MASK		G0_PORT_VID(0xfff)
#define  G0_PORT_VID_DEF		G0_PORT_VID(0)

/* Register for port MAC control register */
#define MT753X_PMCR_P(x)		(0x3000 + ((x) * 0x100))
#define  PMCR_IFG_XMIT_MASK		GENMASK(19, 18)
#define  PMCR_IFG_XMIT(x)		FIELD_PREP(PMCR_IFG_XMIT_MASK, x)
#define  PMCR_EXT_PHY			BIT(17)
#define  PMCR_MAC_MODE			BIT(16)
#define  MT7530_FORCE_MODE		BIT(15)
#define  PMCR_MAC_TX_EN			BIT(14)
#define  PMCR_MAC_RX_EN			BIT(13)
#define  PMCR_BACKOFF_EN		BIT(9)
#define  PMCR_BACKPR_EN			BIT(8)
#define  PMCR_FORCE_EEE1G		BIT(7)
#define  PMCR_FORCE_EEE100		BIT(6)
#define  PMCR_FORCE_RX_FC_EN		BIT(5)
#define  PMCR_FORCE_TX_FC_EN		BIT(4)
#define  PMCR_FORCE_SPEED_1000		BIT(3)
#define  PMCR_FORCE_SPEED_100		BIT(2)
#define  PMCR_FORCE_FDX			BIT(1)
#define  PMCR_FORCE_LNK			BIT(0)
#define  MT7531_FORCE_MODE_LNK		BIT(31)
#define  MT7531_FORCE_MODE_SPD		BIT(30)
#define  MT7531_FORCE_MODE_DPX		BIT(29)
#define  MT7531_FORCE_MODE_RX_FC	BIT(28)
#define  MT7531_FORCE_MODE_TX_FC	BIT(27)
#define  MT7531_FORCE_MODE_EEE100	BIT(26)
#define  MT7531_FORCE_MODE_EEE1G	BIT(25)
#define  MT7531_FORCE_MODE_MASK		(MT7531_FORCE_MODE_LNK | \
					 MT7531_FORCE_MODE_SPD | \
					 MT7531_FORCE_MODE_DPX | \
					 MT7531_FORCE_MODE_RX_FC | \
					 MT7531_FORCE_MODE_TX_FC | \
					 MT7531_FORCE_MODE_EEE100 | \
					 MT7531_FORCE_MODE_EEE1G)
#define  MT753X_FORCE_MODE(id)		((id == ID_MT7531 || \
					  id == ID_MT7988) ? \
					 MT7531_FORCE_MODE_MASK : \
					 MT7530_FORCE_MODE)
#define  PMCR_LINK_SETTINGS_MASK	(PMCR_MAC_TX_EN | PMCR_MAC_RX_EN | \
					 PMCR_FORCE_EEE1G | \
					 PMCR_FORCE_EEE100 | \
					 PMCR_FORCE_RX_FC_EN | \
					 PMCR_FORCE_TX_FC_EN | \
					 PMCR_FORCE_SPEED_1000 | \
					 PMCR_FORCE_SPEED_100 | \
					 PMCR_FORCE_FDX | PMCR_FORCE_LNK)

#define MT753X_PMEEECR_P(x)		(0x3004 + (x) * 0x100)
#define  WAKEUP_TIME_1000_MASK		GENMASK(31, 24)
#define  WAKEUP_TIME_1000(x)		FIELD_PREP(WAKEUP_TIME_1000_MASK, x)
#define  WAKEUP_TIME_100_MASK		GENMASK(23, 16)
#define  WAKEUP_TIME_100(x)		FIELD_PREP(WAKEUP_TIME_100_MASK, x)
#define  LPI_THRESH_MASK		GENMASK(15, 4)
#define  LPI_THRESH_GET(x)		FIELD_GET(LPI_THRESH_MASK, x)
#define  LPI_THRESH_SET(x)		FIELD_PREP(LPI_THRESH_MASK, x)
#define  LPI_MODE_EN			BIT(0)

#define MT7530_PMSR_P(x)		(0x3008 + (x) * 0x100)
#define  PMSR_EEE1G			BIT(7)
#define  PMSR_EEE100M			BIT(6)
#define  PMSR_RX_FC			BIT(5)
#define  PMSR_TX_FC			BIT(4)
#define  PMSR_SPEED_1000		BIT(3)
#define  PMSR_SPEED_100			BIT(2)
#define  PMSR_SPEED_10			0x00
#define  PMSR_SPEED_MASK		(PMSR_SPEED_100 | PMSR_SPEED_1000)
#define  PMSR_DPX			BIT(1)
#define  PMSR_LINK			BIT(0)

/* Register for port debug count */
#define MT7531_DBG_CNT(x)		(0x3018 + (x) * 0x100)
#define  MT7531_DIS_CLR			BIT(31)

#define MT7530_GMACCR			0x30e0
#define  MAX_RX_JUMBO(x)		((x) << 2)
#define  MAX_RX_JUMBO_MASK		GENMASK(5, 2)
#define  MAX_RX_PKT_LEN_MASK		GENMASK(1, 0)
#define  MAX_RX_PKT_LEN_1522		0x0
#define  MAX_RX_PKT_LEN_1536		0x1
#define  MAX_RX_PKT_LEN_1552		0x2
#define  MAX_RX_PKT_LEN_JUMBO		0x3

/* Register for MIB */
#define MT7530_PORT_MIB_COUNTER(x)	(0x4000 + (x) * 0x100)
#define MT7530_MIB_CCR			0x4fe0
#define  CCR_MIB_ENABLE			BIT(31)
#define  CCR_RX_OCT_CNT_GOOD		BIT(7)
#define  CCR_RX_OCT_CNT_BAD		BIT(6)
#define  CCR_TX_OCT_CNT_GOOD		BIT(5)
#define  CCR_TX_OCT_CNT_BAD		BIT(4)
#define  CCR_MIB_FLUSH			(CCR_RX_OCT_CNT_GOOD | \
					 CCR_RX_OCT_CNT_BAD | \
					 CCR_TX_OCT_CNT_GOOD | \
					 CCR_TX_OCT_CNT_BAD)
#define  CCR_MIB_ACTIVATE		(CCR_MIB_ENABLE | \
					 CCR_RX_OCT_CNT_GOOD | \
					 CCR_RX_OCT_CNT_BAD | \
					 CCR_TX_OCT_CNT_GOOD | \
					 CCR_TX_OCT_CNT_BAD)

/* MT7531 SGMII register group */
#define MT7531_SGMII_REG_BASE(p)	(0x5000 + ((p) - 5) * 0x1000)
#define MT7531_PHYA_CTRL_SIGNAL3	0x128

/* Register for system reset */
#define MT7530_SYS_CTRL			0x7000
#define  SYS_CTRL_PHY_RST		BIT(2)
#define  SYS_CTRL_SW_RST		BIT(1)
#define  SYS_CTRL_REG_RST		BIT(0)

/* Register for system interrupt */
#define MT7530_SYS_INT_EN		0x7008

/* Register for system interrupt status */
#define MT7530_SYS_INT_STS		0x700c

/* Register for PHY Indirect Access Control */
#define MT7531_PHY_IAC			0x701C
#define  MT7531_PHY_ACS_ST		BIT(31)
#define  MT7531_MDIO_REG_ADDR_MASK	(0x1f << 25)
#define  MT7531_MDIO_PHY_ADDR_MASK	(0x1f << 20)
#define  MT7531_MDIO_CMD_MASK		(0x3 << 18)
#define  MT7531_MDIO_ST_MASK		(0x3 << 16)
#define  MT7531_MDIO_RW_DATA_MASK	(0xffff)
#define  MT7531_MDIO_REG_ADDR(x)	(((x) & 0x1f) << 25)
#define  MT7531_MDIO_DEV_ADDR(x)	(((x) & 0x1f) << 25)
#define  MT7531_MDIO_PHY_ADDR(x)	(((x) & 0x1f) << 20)
#define  MT7531_MDIO_CMD(x)		(((x) & 0x3) << 18)
#define  MT7531_MDIO_ST(x)		(((x) & 0x3) << 16)

enum mt7531_phy_iac_cmd {
	MT7531_MDIO_ADDR = 0,
	MT7531_MDIO_WRITE = 1,
	MT7531_MDIO_READ = 2,
	MT7531_MDIO_READ_CL45 = 3,
};

/* MDIO_ST: MDIO start field */
enum mt7531_mdio_st {
	MT7531_MDIO_ST_CL45 = 0,
	MT7531_MDIO_ST_CL22 = 1,
};

#define  MT7531_MDIO_CL22_READ		(MT7531_MDIO_ST(MT7531_MDIO_ST_CL22) | \
					 MT7531_MDIO_CMD(MT7531_MDIO_READ))
#define  MT7531_MDIO_CL22_WRITE		(MT7531_MDIO_ST(MT7531_MDIO_ST_CL22) | \
					 MT7531_MDIO_CMD(MT7531_MDIO_WRITE))
#define  MT7531_MDIO_CL45_ADDR		(MT7531_MDIO_ST(MT7531_MDIO_ST_CL45) | \
					 MT7531_MDIO_CMD(MT7531_MDIO_ADDR))
#define  MT7531_MDIO_CL45_READ		(MT7531_MDIO_ST(MT7531_MDIO_ST_CL45) | \
					 MT7531_MDIO_CMD(MT7531_MDIO_READ))
#define  MT7531_MDIO_CL45_WRITE		(MT7531_MDIO_ST(MT7531_MDIO_ST_CL45) | \
					 MT7531_MDIO_CMD(MT7531_MDIO_WRITE))

/* Register for RGMII clock phase */
#define MT7531_CLKGEN_CTRL		0x7500
#define  CLK_SKEW_OUT(x)		(((x) & 0x3) << 8)
#define  CLK_SKEW_OUT_MASK		GENMASK(9, 8)
#define  CLK_SKEW_IN(x)			(((x) & 0x3) << 6)
#define  CLK_SKEW_IN_MASK		GENMASK(7, 6)
#define  RXCLK_NO_DELAY			BIT(5)
#define  TXCLK_NO_REVERSE		BIT(4)
#define  GP_MODE(x)			(((x) & 0x3) << 1)
#define  GP_MODE_MASK			GENMASK(2, 1)
#define  GP_CLK_EN			BIT(0)

enum mt7531_gp_mode {
	MT7531_GP_MODE_RGMII = 0,
	MT7531_GP_MODE_MII = 1,
	MT7531_GP_MODE_REV_MII = 2
};

enum mt7531_clk_skew {
	MT7531_CLK_SKEW_NO_CHG = 0,
	MT7531_CLK_SKEW_DLY_100PPS = 1,
	MT7531_CLK_SKEW_DLY_200PPS = 2,
	MT7531_CLK_SKEW_REVERSE = 3,
};

/* Register for trap status */
#define MT753X_TRAP			0x7800
#define  MT7530_XTAL_MASK		(BIT(10) | BIT(9))
#define  MT7530_XTAL_25MHZ		(BIT(10) | BIT(9))
#define  MT7530_XTAL_40MHZ		BIT(10)
#define  MT7530_XTAL_20MHZ		BIT(9)
#define  MT7531_XTAL25			BIT(7)

/* Register for trap modification */
#define MT753X_MTRAP			0x7804
#define  MT7530_P5_PHY0_SEL		BIT(20)
#define  MT7530_CHG_TRAP		BIT(16)
#define  MT7530_P5_MAC_SEL		BIT(13)
#define  MT7530_P6_DIS			BIT(8)
#define  MT7530_P5_RGMII_MODE		BIT(7)
#define  MT7530_P5_DIS			BIT(6)
#define  MT7530_PHY_INDIRECT_ACCESS	BIT(5)
#define  MT7531_CHG_STRAP		BIT(8)
#define  MT7531_PHY_EN			BIT(6)

enum mt7531_xtal_fsel {
	MT7531_XTAL_FSEL_25MHZ,
	MT7531_XTAL_FSEL_40MHZ,
};

/* Register for TOP signal control */
#define MT7530_TOP_SIG_CTRL		0x7808
#define  TOP_SIG_CTRL_NORMAL		(BIT(17) | BIT(16))

#define MT7531_TOP_SIG_SR		0x780c
#define  PAD_DUAL_SGMII_EN		BIT(1)
#define  PAD_MCM_SMI_EN			BIT(0)

#define MT7530_IO_DRV_CR		0x7810
#define  P5_IO_CLK_DRV(x)		((x) & 0x3)
#define  P5_IO_DATA_DRV(x)		(((x) & 0x3) << 4)

#define MT7531_CHIP_REV			0x781C

#define MT7531_PLLGP_EN			0x7820
#define  EN_COREPLL			BIT(2)
#define  SW_CLKSW			BIT(1)
#define  SW_PLLGP			BIT(0)

#define MT7530_P6ECR			0x7830
#define  P6_INTF_MODE_MASK		0x3
#define  P6_INTF_MODE(x)		((x) & 0x3)

#define MT7531_PLLGP_CR0		0x78a8
#define  RG_COREPLL_EN			BIT(22)
#define  RG_COREPLL_POSDIV_S		23
#define  RG_COREPLL_POSDIV_M		0x3800000
#define  RG_COREPLL_SDM_PCW_S		1
#define  RG_COREPLL_SDM_PCW_M		0x3ffffe
#define  RG_COREPLL_SDM_PCW_CHG		BIT(0)

/* Registers for RGMII and SGMII PLL clock */
#define MT7531_ANA_PLLGP_CR2		0x78b0
#define MT7531_ANA_PLLGP_CR5		0x78bc

/* Registers for TRGMII on the both side */
#define MT7530_TRGMII_RCK_CTRL		0x7a00
#define  RX_RST				BIT(31)
#define  RXC_DQSISEL			BIT(30)
#define  DQSI1_TAP_MASK			(0x7f << 8)
#define  DQSI0_TAP_MASK			0x7f
#define  DQSI1_TAP(x)			(((x) & 0x7f) << 8)
#define  DQSI0_TAP(x)			((x) & 0x7f)

#define MT7530_TRGMII_RCK_RTT		0x7a04
#define  DQS1_GATE			BIT(31)
#define  DQS0_GATE			BIT(30)

#define MT7530_TRGMII_RD(x)		(0x7a10 + (x) * 8)
#define  BSLIP_EN			BIT(31)
#define  EDGE_CHK			BIT(30)
#define  RD_TAP_MASK			0x7f
#define  RD_TAP(x)			((x) & 0x7f)

#define MT7530_TRGMII_TXCTRL		0x7a40
#define  TRAIN_TXEN			BIT(31)
#define  TXC_INV			BIT(30)
#define  TX_RST				BIT(28)

#define MT7530_TRGMII_TD_ODT(i)		(0x7a54 + 8 * (i))
#define  TD_DM_DRVP(x)			((x) & 0xf)
#define  TD_DM_DRVN(x)			(((x) & 0xf) << 4)

#define MT7530_TRGMII_TCK_CTRL		0x7a78
#define  TCK_TAP(x)			(((x) & 0xf) << 8)

#define MT7530_P5RGMIIRXCR		0x7b00
#define  CSR_RGMII_EDGE_ALIGN		BIT(8)
#define  CSR_RGMII_RXC_0DEG_CFG(x)	((x) & 0xf)

#define MT7530_P5RGMIITXCR		0x7b04
#define  CSR_RGMII_TXC_CFG(x)		((x) & 0x1f)

/* Registers for GPIO mode */
#define MT7531_GPIO_MODE0		0x7c0c
#define  MT7531_GPIO0_MASK		GENMASK(3, 0)
#define  MT7531_GPIO0_INTERRUPT		1

#define MT7531_GPIO_MODE1		0x7c10
#define  MT7531_GPIO11_RG_RXD2_MASK	GENMASK(15, 12)
#define  MT7531_EXT_P_MDC_11		(2 << 12)
#define  MT7531_GPIO12_RG_RXD3_MASK	GENMASK(19, 16)
#define  MT7531_EXT_P_MDIO_12		(2 << 16)

/* Registers for LED GPIO control (MT7530 only)
 * All registers follow this pattern:
 * [ 2: 0]  port 0
 * [ 6: 4]  port 1
 * [10: 8]  port 2
 * [14:12]  port 3
 * [18:16]  port 4
 */

/* LED enable, 0: Disable, 1: Enable (Default) */
#define MT7530_LED_EN			0x7d00
/* LED mode, 0: GPIO mode, 1: PHY mode (Default) */
#define MT7530_LED_IO_MODE		0x7d04
/* GPIO direction, 0: Input, 1: Output */
#define MT7530_LED_GPIO_DIR		0x7d10
/* GPIO output enable, 0: Disable, 1: Enable */
#define MT7530_LED_GPIO_OE		0x7d14
/* GPIO value, 0: Low, 1: High */
#define MT7530_LED_GPIO_DATA		0x7d18

#define MT7530_CREV			0x7ffc
#define  CHIP_NAME_SHIFT		16
#define  MT7530_ID			0x7530

#define MT7531_CREV			0x781C
#define  CHIP_REV_M			0x0f
#define  MT7531_ID			0x7531

/* Registers for core PLL access through mmd indirect */
#define CORE_PLL_GROUP2			0x401
#define  RG_SYSPLL_EN_NORMAL		BIT(15)
#define  RG_SYSPLL_VODEN		BIT(14)
#define  RG_SYSPLL_LF			BIT(13)
#define  RG_SYSPLL_RST_DLY(x)		(((x) & 0x3) << 12)
#define  RG_SYSPLL_LVROD_EN		BIT(10)
#define  RG_SYSPLL_PREDIV(x)		(((x) & 0x3) << 8)
#define  RG_SYSPLL_POSDIV(x)		(((x) & 0x3) << 5)
#define  RG_SYSPLL_FBKSEL		BIT(4)
#define  RT_SYSPLL_EN_AFE_OLT		BIT(0)

#define CORE_PLL_GROUP4			0x403
#define  RG_SYSPLL_DDSFBK_EN		BIT(12)
#define  RG_SYSPLL_BIAS_EN		BIT(11)
#define  RG_SYSPLL_BIAS_LPF_EN		BIT(10)
#define  MT7531_RG_SYSPLL_DMY2		BIT(6)
#define  MT7531_PHY_PLL_OFF		BIT(5)
#define  MT7531_PHY_PLL_BYPASS_MODE	BIT(4)

#define MT753X_CTRL_PHY_ADDR(addr)	((addr + 1) & 0x1f)

#define CORE_PLL_GROUP5			0x404
#define  RG_LCDDS_PCW_NCPO1(x)		((x) & 0xffff)

#define CORE_PLL_GROUP6			0x405
#define  RG_LCDDS_PCW_NCPO0(x)		((x) & 0xffff)

#define CORE_PLL_GROUP7			0x406
#define  RG_LCDDS_PWDB			BIT(15)
#define  RG_LCDDS_ISO_EN		BIT(13)
#define  RG_LCCDS_C(x)			(((x) & 0x7) << 4)
#define  RG_LCDDS_PCW_NCPO_CHG		BIT(3)

#define CORE_PLL_GROUP10		0x409
#define  RG_LCDDS_SSC_DELTA(x)		((x) & 0xfff)

#define CORE_PLL_GROUP11		0x40a
#define  RG_LCDDS_SSC_DELTA1(x)		((x) & 0xfff)

#define CORE_GSWPLL_GRP1		0x40d
#define  RG_GSWPLL_PREDIV(x)		(((x) & 0x3) << 14)
#define  RG_GSWPLL_POSDIV_200M(x)	(((x) & 0x3) << 12)
#define  RG_GSWPLL_EN_PRE		BIT(11)
#define  RG_GSWPLL_FBKSEL		BIT(10)
#define  RG_GSWPLL_BP			BIT(9)
#define  RG_GSWPLL_BR			BIT(8)
#define  RG_GSWPLL_FBKDIV_200M(x)	((x) & 0xff)

#define CORE_GSWPLL_GRP2		0x40e
#define  RG_GSWPLL_POSDIV_500M(x)	(((x) & 0x3) << 8)
#define  RG_GSWPLL_FBKDIV_500M(x)	((x) & 0xff)

#define CORE_TRGMII_GSW_CLK_CG		0x410
#define  REG_GSWCK_EN			BIT(0)
#define  REG_TRGMIICK_EN		BIT(1)

#define MIB_DESC(_s, _o, _n)	\
	{			\
		.size = (_s),	\
		.offset = (_o),	\
		.name = (_n),	\
	}

struct mt7530_mib_desc {
	unsigned int size;
	unsigned int offset;
	const char *name;
};

struct mt7530_fdb {
	u16 vid;
	u8 port_mask;
	u8 aging;
	u8 mac[6];
	bool noarp;
};

/* struct mt7530_port -	This is the main data structure for holding the state
 *			of the port.
 * @enable:	The status used for show port is enabled or not.
 * @pm:		The matrix used to show all connections with the port.
 * @pvid:	The VLAN specified is to be considered a PVID at ingress.  Any
 *		untagged frames will be assigned to the related VLAN.
 * @sgmii_pcs:	Pointer to PCS instance for SerDes ports
 */
struct mt7530_port {
	bool enable;
	bool isolated;
	u32 pm;
	u16 pvid;
	struct phylink_pcs *sgmii_pcs;
};

/* Port 5 mode definitions of the MT7530 switch */
enum mt7530_p5_mode {
	GMAC5,
	MUX_PHY_P0,
	MUX_PHY_P4,
};

struct mt7530_priv;

struct mt753x_pcs {
	struct phylink_pcs pcs;
	struct mt7530_priv *priv;
	int port;
};

/* struct mt753x_info -	This is the main data structure for holding the specific
 *			part for each supported device
 * @id:			Holding the identifier to a switch model
 * @pcs_ops:		Holding the pointer to the MAC PCS operations structure
 * @sw_setup:		Holding the handler to a device initialization
 * @phy_read_c22:	Holding the way reading PHY port using C22
 * @phy_write_c22:	Holding the way writing PHY port using C22
 * @phy_read_c45:	Holding the way reading PHY port using C45
 * @phy_write_c45:	Holding the way writing PHY port using C45
 * @mac_port_get_caps:	Holding the handler that provides MAC capabilities
 * @mac_port_config:	Holding the way setting up the PHY attribute to a
 *			certain MAC port
 */
struct mt753x_info {
	enum mt753x_id id;

	const struct phylink_pcs_ops *pcs_ops;

	int (*sw_setup)(struct dsa_switch *ds);
	int (*phy_read_c22)(struct mt7530_priv *priv, int port, int regnum);
	int (*phy_write_c22)(struct mt7530_priv *priv, int port, int regnum,
			     u16 val);
	int (*phy_read_c45)(struct mt7530_priv *priv, int port, int devad,
			    int regnum);
	int (*phy_write_c45)(struct mt7530_priv *priv, int port, int devad,
			     int regnum, u16 val);
	void (*mac_port_get_caps)(struct dsa_switch *ds, int port,
				  struct phylink_config *config);
	void (*mac_port_config)(struct dsa_switch *ds, int port,
				unsigned int mode,
				phy_interface_t interface);
};

/* struct mt7530_priv -	This is the main data structure for holding the state
 *			of the driver
 * @dev:		The device pointer
 * @ds:			The pointer to the dsa core structure
 * @bus:		The bus used for the device and built-in PHY
 * @regmap:		The regmap instance representing all switch registers
 * @rstc:		The pointer to reset control used by MCM
 * @core_pwr:		The power supplied into the core
 * @io_pwr:		The power supplied into the I/O
 * @reset:		The descriptor for GPIO line tied to its reset pin
 * @mcm:		Flag for distinguishing if standalone IC or module
 *			coupling
 * @ports:		Holding the state among ports
 * @reg_mutex:		The lock for protecting among process accessing
 *			registers
 * @p5_mode:		Holding the current mode of port 5 of the MT7530 switch
 * @p5_sgmii:		Flag for distinguishing if port 5 of the MT7531 switch
 *			has got SGMII
 * @irq:		IRQ number of the switch
 * @irq_domain:		IRQ domain of the switch irq_chip
 * @irq_enable:		IRQ enable bits, synced to SYS_INT_EN
 * @create_sgmii:	Pointer to function creating SGMII PCS instance(s)
 * @active_cpu_ports:	Holding the active CPU ports
 * @mdiodev:		The pointer to the MDIO device structure
 */
struct mt7530_priv {
	struct device		*dev;
	struct dsa_switch	*ds;
	struct mii_bus		*bus;
	struct regmap		*regmap;
	struct reset_control	*rstc;
	struct regulator	*core_pwr;
	struct regulator	*io_pwr;
	struct gpio_desc	*reset;
	const struct mt753x_info *info;
	unsigned int		id;
	bool			mcm;
	enum mt7530_p5_mode	p5_mode;
	bool			p5_sgmii;
	u8			mirror_rx;
	u8			mirror_tx;
	struct mt7530_port	ports[MT7530_NUM_PORTS];
	struct mt753x_pcs	pcs[MT7530_NUM_PORTS];
	/* protect among processes for registers access*/
	struct mutex reg_mutex;
	int irq;
	struct irq_domain *irq_domain;
	u32 irq_enable;
	int (*create_sgmii)(struct mt7530_priv *priv);
	u8 active_cpu_ports;
	struct mdio_device *mdiodev;
};

struct mt7530_hw_vlan_entry {
	int port;
	u8  old_members;
	bool untagged;
};

static inline void mt7530_hw_vlan_entry_init(struct mt7530_hw_vlan_entry *e,
					     int port, bool untagged)
{
	e->port = port;
	e->untagged = untagged;
}

typedef void (*mt7530_vlan_op)(struct mt7530_priv *,
			       struct mt7530_hw_vlan_entry *);

struct mt7530_hw_stats {
	const char	*string;
	u16		reg;
	u8		sizeof_stat;
};

struct mt7530_dummy_poll {
	struct mt7530_priv *priv;
	u32 reg;
};

static inline void INIT_MT7530_DUMMY_POLL(struct mt7530_dummy_poll *p,
					  struct mt7530_priv *priv, u32 reg)
{
	p->priv = priv;
	p->reg = reg;
}

int mt7530_probe_common(struct mt7530_priv *priv);
void mt7530_remove_common(struct mt7530_priv *priv);

extern const struct dsa_switch_ops mt7530_switch_ops;
extern const struct mt753x_info mt753x_table[];

#endif /* __MT7530_H */
