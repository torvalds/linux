/*
 * Copyright (C) 2017 Sean Wang <sean.wang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MT7530_H
#define __MT7530_H

#define MT7530_NUM_PORTS		7
#define MT7530_CPU_PORT			6
#define MT7530_NUM_FDB_RECORDS		2048
#define MT7530_ALL_MEMBERS		0xff

#define	NUM_TRGMII_CTRL			5

#define TRGMII_BASE(x)			(0x10000 + (x))

/* Registers to ethsys access */
#define ETHSYS_CLKCFG0			0x2c
#define  ETHSYS_TRGMII_CLK_SEL362_5	BIT(11)

#define SYSC_REG_RSTCTRL		0x34
#define  RESET_MCM			BIT(2)

/* Registers to mac forward control for unknown frames */
#define MT7530_MFC			0x10
#define  BC_FFP(x)			(((x) & 0xff) << 24)
#define  UNM_FFP(x)			(((x) & 0xff) << 16)
#define  UNU_FFP(x)			(((x) & 0xff) << 8)
#define  UNU_FFP_MASK			UNU_FFP(~0)

/* Registers for address table access */
#define MT7530_ATA1			0x74
#define  STATIC_EMP			0
#define  STATIC_ENT			3
#define MT7530_ATA2			0x78

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
/* Per VLAN Egress Tag Control */
#define  VTAG_EN			BIT(28)
/* VLAN Member Control */
#define  PORT_MEM(x)			(((x) & 0xff) << 16)
/* VLAN Entry Valid */
#define  VLAN_VALID			BIT(0)
#define  PORT_MEM_SHFT			16
#define  PORT_MEM_MASK			0xff

#define MT7530_VAWD2			0x98
/* Egress Tag Control */
#define  ETAG_CTRL_P(p, x)		(((x) & 0x3) << ((p) << 1))
#define  ETAG_CTRL_P_MASK(p)		ETAG_CTRL_P(p, 3)

enum mt7530_vlan_egress_attr {
	MT7530_VLAN_EGRESS_UNTAG = 0,
	MT7530_VLAN_EGRESS_TAG = 2,
	MT7530_VLAN_EGRESS_STACK = 3,
};

/* Register for port STP state control */
#define MT7530_SSP_P(x)			(0x2000 + ((x) * 0x100))
#define  FID_PST(x)			((x) & 0x3)
#define  FID_PST_MASK			FID_PST(0x3)

enum mt7530_stp_state {
	MT7530_STP_DISABLED = 0,
	MT7530_STP_BLOCKING = 1,
	MT7530_STP_LISTENING = 1,
	MT7530_STP_LEARNING = 2,
	MT7530_STP_FORWARDING  = 3
};

/* Register for port control */
#define MT7530_PCR_P(x)			(0x2004 + ((x) * 0x100))
#define  PORT_VLAN(x)			((x) & 0x3)

enum mt7530_port_mode {
	/* Port Matrix Mode: Frames are forwarded by the PCR_MATRIX members. */
	MT7530_PORT_MATRIX_MODE = PORT_VLAN(0),

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
#define  VLAN_ATTR(x)			(((x) & 0x3) << 6)
#define  VLAN_ATTR_MASK			VLAN_ATTR(3)

enum mt7530_vlan_port_attr {
	MT7530_VLAN_USER = 0,
	MT7530_VLAN_TRANSPARENT = 3,
};

#define  STAG_VPID			(((x) & 0xffff) << 16)

/* Register for port port-and-protocol based vlan 1 control */
#define MT7530_PPBV1_P(x)		(0x2014 + ((x) * 0x100))
#define  G0_PORT_VID(x)			(((x) & 0xfff) << 0)
#define  G0_PORT_VID_MASK		G0_PORT_VID(0xfff)
#define  G0_PORT_VID_DEF		G0_PORT_VID(1)

/* Register for port MAC control register */
#define MT7530_PMCR_P(x)		(0x3000 + ((x) * 0x100))
#define  PMCR_IFG_XMIT(x)		(((x) & 0x3) << 18)
#define  PMCR_MAC_MODE			BIT(16)
#define  PMCR_FORCE_MODE		BIT(15)
#define  PMCR_TX_EN			BIT(14)
#define  PMCR_RX_EN			BIT(13)
#define  PMCR_BACKOFF_EN		BIT(9)
#define  PMCR_BACKPR_EN			BIT(8)
#define  PMCR_TX_FC_EN			BIT(5)
#define  PMCR_RX_FC_EN			BIT(4)
#define  PMCR_FORCE_SPEED_1000		BIT(3)
#define  PMCR_FORCE_SPEED_100		BIT(2)
#define  PMCR_FORCE_FDX			BIT(1)
#define  PMCR_FORCE_LNK			BIT(0)
#define  PMCR_COMMON_LINK		(PMCR_IFG_XMIT(1) | PMCR_MAC_MODE | \
					 PMCR_BACKOFF_EN | PMCR_BACKPR_EN | \
					 PMCR_TX_EN | PMCR_RX_EN | \
					 PMCR_TX_FC_EN | PMCR_RX_FC_EN)
#define  PMCR_CPUP_LINK			(PMCR_COMMON_LINK | PMCR_FORCE_MODE | \
					 PMCR_FORCE_SPEED_1000 | \
					 PMCR_FORCE_FDX | \
					 PMCR_FORCE_LNK)
#define  PMCR_USERP_LINK		PMCR_COMMON_LINK
#define  PMCR_FIXED_LINK		(PMCR_IFG_XMIT(1) | PMCR_MAC_MODE | \
					 PMCR_FORCE_MODE | PMCR_TX_EN | \
					 PMCR_RX_EN | PMCR_BACKPR_EN | \
					 PMCR_BACKOFF_EN | \
					 PMCR_FORCE_SPEED_1000 | \
					 PMCR_FORCE_FDX | \
					 PMCR_FORCE_LNK)
#define PMCR_FIXED_LINK_FC		(PMCR_FIXED_LINK | \
					 PMCR_TX_FC_EN | PMCR_RX_FC_EN)

#define MT7530_PMSR_P(x)		(0x3008 + (x) * 0x100)

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
/* Register for system reset */
#define MT7530_SYS_CTRL			0x7000
#define  SYS_CTRL_PHY_RST		BIT(2)
#define  SYS_CTRL_SW_RST		BIT(1)
#define  SYS_CTRL_REG_RST		BIT(0)

/* Register for hw trap status */
#define MT7530_HWTRAP			0x7800

/* Register for hw trap modification */
#define MT7530_MHWTRAP			0x7804
#define  MHWTRAP_MANUAL			BIT(16)
#define  MHWTRAP_P5_MAC_SEL		BIT(13)
#define  MHWTRAP_P6_DIS			BIT(8)
#define  MHWTRAP_P5_RGMII_MODE		BIT(7)
#define  MHWTRAP_P5_DIS			BIT(6)
#define  MHWTRAP_PHY_ACCESS		BIT(5)

/* Register for TOP signal control */
#define MT7530_TOP_SIG_CTRL		0x7808
#define  TOP_SIG_CTRL_NORMAL		(BIT(17) | BIT(16))

#define MT7530_IO_DRV_CR		0x7810
#define  P5_IO_CLK_DRV(x)		((x) & 0x3)
#define  P5_IO_DATA_DRV(x)		(((x) & 0x3) << 4)

#define MT7530_P6ECR			0x7830
#define  P6_INTF_MODE_MASK		0x3
#define  P6_INTF_MODE(x)		((x) & 0x3)

/* Registers for TRGMII on the both side */
#define MT7530_TRGMII_RCK_CTRL		0x7a00
#define GSW_TRGMII_RCK_CTRL		0x300
#define  RX_RST				BIT(31)
#define  RXC_DQSISEL			BIT(30)
#define  DQSI1_TAP_MASK			(0x7f << 8)
#define  DQSI0_TAP_MASK			0x7f
#define  DQSI1_TAP(x)			(((x) & 0x7f) << 8)
#define  DQSI0_TAP(x)			((x) & 0x7f)

#define MT7530_TRGMII_RCK_RTT		0x7a04
#define GSW_TRGMII_RCK_RTT		0x304
#define  DQS1_GATE			BIT(31)
#define  DQS0_GATE			BIT(30)

#define MT7530_TRGMII_RD(x)		(0x7a10 + (x) * 8)
#define GSW_TRGMII_RD(x)		(0x310 + (x) * 8)
#define  BSLIP_EN			BIT(31)
#define  EDGE_CHK			BIT(30)
#define  RD_TAP_MASK			0x7f
#define  RD_TAP(x)			((x) & 0x7f)

#define GSW_TRGMII_TXCTRL		0x340
#define MT7530_TRGMII_TXCTRL		0x7a40
#define  TRAIN_TXEN			BIT(31)
#define  TXC_INV			BIT(30)
#define  TX_RST				BIT(28)

#define MT7530_TRGMII_TD_ODT(i)		(0x7a54 + 8 * (i))
#define GSW_TRGMII_TD_ODT(i)		(0x354 + 8 * (i))
#define  TD_DM_DRVP(x)			((x) & 0xf)
#define  TD_DM_DRVN(x)			(((x) & 0xf) << 4)

#define GSW_INTF_MODE			0x390
#define  INTF_MODE_TRGMII		BIT(1)

#define MT7530_TRGMII_TCK_CTRL		0x7a78
#define  TCK_TAP(x)			(((x) & 0xf) << 8)

#define MT7530_P5RGMIIRXCR		0x7b00
#define  CSR_RGMII_EDGE_ALIGN		BIT(8)
#define  CSR_RGMII_RXC_0DEG_CFG(x)	((x) & 0xf)

#define MT7530_P5RGMIITXCR		0x7b04
#define  CSR_RGMII_TXC_CFG(x)		((x) & 0x1f)

#define MT7530_CREV			0x7ffc
#define  CHIP_NAME_SHIFT		16
#define  MT7530_ID			0x7530

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
 * @vlan_filtering: The flags indicating whether the port that can recognize
 *		    VLAN-tagged frames.
 */
struct mt7530_port {
	bool enable;
	u32 pm;
	u16 pvid;
	bool vlan_filtering;
};

/* struct mt7530_priv -	This is the main data structure for holding the state
 *			of the driver
 * @dev:		The device pointer
 * @ds:			The pointer to the dsa core structure
 * @bus:		The bus used for the device and built-in PHY
 * @rstc:		The pointer to reset control used by MCM
 * @ethernet:		The regmap used for access TRGMII-based registers
 * @core_pwr:		The power supplied into the core
 * @io_pwr:		The power supplied into the I/O
 * @reset:		The descriptor for GPIO line tied to its reset pin
 * @mcm:		Flag for distinguishing if standalone IC or module
 *			coupling
 * @ports:		Holding the state among ports
 * @reg_mutex:		The lock for protecting among process accessing
 *			registers
 */
struct mt7530_priv {
	struct device		*dev;
	struct dsa_switch	*ds;
	struct mii_bus		*bus;
	struct reset_control	*rstc;
	struct regmap		*ethernet;
	struct regulator	*core_pwr;
	struct regulator	*io_pwr;
	struct gpio_desc	*reset;
	bool			mcm;

	struct mt7530_port	ports[MT7530_NUM_PORTS];
	/* protect among processes for registers access*/
	struct mutex reg_mutex;
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

#endif /* __MT7530_H */
