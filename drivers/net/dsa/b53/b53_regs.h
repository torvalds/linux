/*
 * B53 register definitions
 *
 * Copyright (C) 2004 Broadcom Corporation
 * Copyright (C) 2011-2013 Jonas Gorski <jogo@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __B53_REGS_H
#define __B53_REGS_H

/* Management Port (SMP) Page offsets */
#define B53_CTRL_PAGE			0x00 /* Control */
#define B53_STAT_PAGE			0x01 /* Status */
#define B53_MGMT_PAGE			0x02 /* Management Mode */
#define B53_MIB_AC_PAGE			0x03 /* MIB Autocast */
#define B53_ARLCTRL_PAGE		0x04 /* ARL Control */
#define B53_ARLIO_PAGE			0x05 /* ARL Access */
#define B53_FRAMEBUF_PAGE		0x06 /* Management frame access */
#define B53_MEM_ACCESS_PAGE		0x08 /* Memory access */

/* PHY Registers */
#define B53_PORT_MII_PAGE(i)		(0x10 + (i)) /* Port i MII Registers */
#define B53_IM_PORT_PAGE		0x18 /* Inverse MII Port (to EMAC) */
#define B53_ALL_PORT_PAGE		0x19 /* All ports MII (broadcast) */

/* MIB registers */
#define B53_MIB_PAGE(i)			(0x20 + (i))

/* Quality of Service (QoS) Registers */
#define B53_QOS_PAGE			0x30

/* Port VLAN Page */
#define B53_PVLAN_PAGE			0x31

/* VLAN Registers */
#define B53_VLAN_PAGE			0x34

/* Jumbo Frame Registers */
#define B53_JUMBO_PAGE			0x40

/* EEE Control Registers Page */
#define B53_EEE_PAGE			0x92

/* CFP Configuration Registers Page */
#define B53_CFP_PAGE			0xa1

/*************************************************************************
 * Control Page registers
 *************************************************************************/

/* Port Control Register (8 bit) */
#define B53_PORT_CTRL(i)		(0x00 + (i))
#define   PORT_CTRL_RX_DISABLE		BIT(0)
#define   PORT_CTRL_TX_DISABLE		BIT(1)
#define   PORT_CTRL_RX_BCST_EN		BIT(2) /* Broadcast RX (P8 only) */
#define   PORT_CTRL_RX_MCST_EN		BIT(3) /* Multicast RX (P8 only) */
#define   PORT_CTRL_RX_UCST_EN		BIT(4) /* Unicast RX (P8 only) */
#define	  PORT_CTRL_STP_STATE_S		5
#define   PORT_CTRL_NO_STP		(0 << PORT_CTRL_STP_STATE_S)
#define   PORT_CTRL_DIS_STATE		(1 << PORT_CTRL_STP_STATE_S)
#define   PORT_CTRL_BLOCK_STATE		(2 << PORT_CTRL_STP_STATE_S)
#define   PORT_CTRL_LISTEN_STATE	(3 << PORT_CTRL_STP_STATE_S)
#define   PORT_CTRL_LEARN_STATE		(4 << PORT_CTRL_STP_STATE_S)
#define   PORT_CTRL_FWD_STATE		(5 << PORT_CTRL_STP_STATE_S)
#define   PORT_CTRL_STP_STATE_MASK	(0x7 << PORT_CTRL_STP_STATE_S)

/* SMP Control Register (8 bit) */
#define B53_SMP_CTRL			0x0a

/* Switch Mode Control Register (8 bit) */
#define B53_SWITCH_MODE			0x0b
#define   SM_SW_FWD_MODE		BIT(0)	/* 1 = Managed Mode */
#define   SM_SW_FWD_EN			BIT(1)	/* Forwarding Enable */

/* IMP Port state override register (8 bit) */
#define B53_PORT_OVERRIDE_CTRL		0x0e
#define   PORT_OVERRIDE_LINK		BIT(0)
#define   PORT_OVERRIDE_FULL_DUPLEX	BIT(1) /* 0 = Half Duplex */
#define   PORT_OVERRIDE_SPEED_S		2
#define   PORT_OVERRIDE_SPEED_10M	(0 << PORT_OVERRIDE_SPEED_S)
#define   PORT_OVERRIDE_SPEED_100M	(1 << PORT_OVERRIDE_SPEED_S)
#define   PORT_OVERRIDE_SPEED_1000M	(2 << PORT_OVERRIDE_SPEED_S)
#define   PORT_OVERRIDE_RV_MII_25	BIT(4) /* BCM5325 only */
#define   PORT_OVERRIDE_RX_FLOW		BIT(4)
#define   PORT_OVERRIDE_TX_FLOW		BIT(5)
#define   PORT_OVERRIDE_SPEED_2000M	BIT(6) /* BCM5301X only, requires setting 1000M */
#define   PORT_OVERRIDE_EN		BIT(7) /* Use the register contents */

/* Power-down mode control */
#define B53_PD_MODE_CTRL_25		0x0f

/* IP Multicast control (8 bit) */
#define B53_IP_MULTICAST_CTRL		0x21
#define  B53_IPMC_FWD_EN		BIT(1)
#define  B53_UC_FWD_EN			BIT(6)
#define  B53_MC_FWD_EN			BIT(7)

/* Switch control (8 bit) */
#define B53_SWITCH_CTRL			0x22
#define  B53_MII_DUMB_FWDG_EN		BIT(6)

/* (16 bit) */
#define B53_UC_FLOOD_MASK		0x32
#define B53_MC_FLOOD_MASK		0x34
#define B53_IPMC_FLOOD_MASK		0x36

/*
 * Override Ports 0-7 State on devices with xMII interfaces (8 bit)
 *
 * For port 8 still use B53_PORT_OVERRIDE_CTRL
 * Please note that not all ports are available on every hardware, e.g. BCM5301X
 * don't include overriding port 6, BCM63xx also have some limitations.
 */
#define B53_GMII_PORT_OVERRIDE_CTRL(i)	(0x58 + (i))
#define   GMII_PO_LINK			BIT(0)
#define   GMII_PO_FULL_DUPLEX		BIT(1) /* 0 = Half Duplex */
#define   GMII_PO_SPEED_S		2
#define   GMII_PO_SPEED_10M		(0 << GMII_PO_SPEED_S)
#define   GMII_PO_SPEED_100M		(1 << GMII_PO_SPEED_S)
#define   GMII_PO_SPEED_1000M		(2 << GMII_PO_SPEED_S)
#define   GMII_PO_RX_FLOW		BIT(4)
#define   GMII_PO_TX_FLOW		BIT(5)
#define   GMII_PO_EN			BIT(6) /* Use the register contents */
#define   GMII_PO_SPEED_2000M		BIT(7) /* BCM5301X only, requires setting 1000M */

#define B53_RGMII_CTRL_IMP		0x60
#define   RGMII_CTRL_ENABLE_GMII	BIT(7)
#define   RGMII_CTRL_TIMING_SEL		BIT(2)
#define   RGMII_CTRL_DLL_RXC		BIT(1)
#define   RGMII_CTRL_DLL_TXC		BIT(0)

#define B53_RGMII_CTRL_P(i)		(B53_RGMII_CTRL_IMP + (i))

/* Software reset register (8 bit) */
#define B53_SOFTRESET			0x79
#define   SW_RST			BIT(7)
#define   EN_CH_RST			BIT(6)
#define   EN_SW_RST			BIT(4)

/* Fast Aging Control register (8 bit) */
#define B53_FAST_AGE_CTRL		0x88
#define   FAST_AGE_STATIC		BIT(0)
#define   FAST_AGE_DYNAMIC		BIT(1)
#define   FAST_AGE_PORT			BIT(2)
#define   FAST_AGE_VLAN			BIT(3)
#define   FAST_AGE_STP			BIT(4)
#define   FAST_AGE_MC			BIT(5)
#define   FAST_AGE_DONE			BIT(7)

/* Fast Aging Port Control register (8 bit) */
#define B53_FAST_AGE_PORT_CTRL		0x89

/* Fast Aging VID Control register (16 bit) */
#define B53_FAST_AGE_VID_CTRL		0x8a

/*************************************************************************
 * Status Page registers
 *************************************************************************/

/* Link Status Summary Register (16bit) */
#define B53_LINK_STAT			0x00

/* Link Status Change Register (16 bit) */
#define B53_LINK_STAT_CHANGE		0x02

/* Port Speed Summary Register (16 bit for FE, 32 bit for GE) */
#define B53_SPEED_STAT			0x04
#define  SPEED_PORT_FE(reg, port)	(((reg) >> (port)) & 1)
#define  SPEED_PORT_GE(reg, port)	(((reg) >> 2 * (port)) & 3)
#define  SPEED_STAT_10M			0
#define  SPEED_STAT_100M		1
#define  SPEED_STAT_1000M		2

/* Duplex Status Summary (16 bit) */
#define B53_DUPLEX_STAT_FE		0x06
#define B53_DUPLEX_STAT_GE		0x08
#define B53_DUPLEX_STAT_63XX		0x0c

/* Revision ID register for BCM5325 */
#define B53_REV_ID_25			0x50

/* Strap Value (48 bit) */
#define B53_STRAP_VALUE			0x70
#define   SV_GMII_CTRL_115		BIT(27)

/*************************************************************************
 * Management Mode Page Registers
 *************************************************************************/

/* Global Management Config Register (8 bit) */
#define B53_GLOBAL_CONFIG		0x00
#define   GC_RESET_MIB			0x01
#define   GC_RX_BPDU_EN			0x02
#define   GC_MIB_AC_HDR_EN		0x10
#define   GC_MIB_AC_EN			0x20
#define   GC_FRM_MGMT_PORT_M		0xC0
#define   GC_FRM_MGMT_PORT_04		0x00
#define   GC_FRM_MGMT_PORT_MII		0x80

/* Broadcom Header control register (8 bit) */
#define B53_BRCM_HDR			0x03
#define   BRCM_HDR_P8_EN		BIT(0) /* Enable tagging on port 8 */
#define   BRCM_HDR_P5_EN		BIT(1) /* Enable tagging on port 5 */
#define   BRCM_HDR_P7_EN		BIT(2) /* Enable tagging on port 7 */

/* Mirror capture control register (16 bit) */
#define B53_MIR_CAP_CTL			0x10
#define  CAP_PORT_MASK			0xf
#define  BLK_NOT_MIR			BIT(14)
#define  MIRROR_EN			BIT(15)

/* Ingress mirror control register (16 bit) */
#define B53_IG_MIR_CTL			0x12
#define  MIRROR_MASK			0x1ff
#define  DIV_EN				BIT(13)
#define  MIRROR_FILTER_MASK		0x3
#define  MIRROR_FILTER_SHIFT		14
#define  MIRROR_ALL			0
#define  MIRROR_DA			1
#define  MIRROR_SA			2

/* Ingress mirror divider register (16 bit) */
#define B53_IG_MIR_DIV			0x14
#define  IN_MIRROR_DIV_MASK		0x3ff

/* Ingress mirror MAC address register (48 bit) */
#define B53_IG_MIR_MAC			0x16

/* Egress mirror control register (16 bit) */
#define B53_EG_MIR_CTL			0x1C

/* Egress mirror divider register (16 bit) */
#define B53_EG_MIR_DIV			0x1E

/* Egress mirror MAC address register (48 bit) */
#define B53_EG_MIR_MAC			0x20

/* Device ID register (8 or 32 bit) */
#define B53_DEVICE_ID			0x30

/* Revision ID register (8 bit) */
#define B53_REV_ID			0x40

/* Broadcom header RX control (16 bit) */
#define B53_BRCM_HDR_RX_DIS		0x60

/* Broadcom header TX control (16 bit)	*/
#define B53_BRCM_HDR_TX_DIS		0x62

/*************************************************************************
 * ARL Access Page Registers
 *************************************************************************/

/* VLAN Table Access Register (8 bit) */
#define B53_VT_ACCESS			0x80
#define B53_VT_ACCESS_9798		0x60 /* for BCM5397/BCM5398 */
#define B53_VT_ACCESS_63XX		0x60 /* for BCM6328/62/68 */
#define   VTA_CMD_WRITE			0
#define   VTA_CMD_READ			1
#define   VTA_CMD_CLEAR			2
#define   VTA_START_CMD			BIT(7)

/* VLAN Table Index Register (16 bit) */
#define B53_VT_INDEX			0x81
#define B53_VT_INDEX_9798		0x61
#define B53_VT_INDEX_63XX		0x62

/* VLAN Table Entry Register (32 bit) */
#define B53_VT_ENTRY			0x83
#define B53_VT_ENTRY_9798		0x63
#define B53_VT_ENTRY_63XX		0x64
#define   VTE_MEMBERS			0x1ff
#define   VTE_UNTAG_S			9
#define   VTE_UNTAG			(0x1ff << 9)

/*************************************************************************
 * ARL I/O Registers
 *************************************************************************/

/* ARL Table Read/Write Register (8 bit) */
#define B53_ARLTBL_RW_CTRL		0x00
#define    ARLTBL_RW			BIT(0)
#define    ARLTBL_START_DONE		BIT(7)

/* MAC Address Index Register (48 bit) */
#define B53_MAC_ADDR_IDX		0x02

/* VLAN ID Index Register (16 bit) */
#define B53_VLAN_ID_IDX			0x08

/* ARL Table MAC/VID Entry N Registers (64 bit)
 *
 * BCM5325 and BCM5365 share most definitions below
 */
#define B53_ARLTBL_MAC_VID_ENTRY(n)	((0x10 * (n)) + 0x10)
#define   ARLTBL_MAC_MASK		0xffffffffffffULL
#define   ARLTBL_VID_S			48
#define   ARLTBL_VID_MASK_25		0xff
#define   ARLTBL_VID_MASK		0xfff
#define   ARLTBL_DATA_PORT_ID_S_25	48
#define   ARLTBL_DATA_PORT_ID_MASK_25	0xf
#define   ARLTBL_AGE_25			BIT(61)
#define   ARLTBL_STATIC_25		BIT(62)
#define   ARLTBL_VALID_25		BIT(63)

/* ARL Table Data Entry N Registers (32 bit) */
#define B53_ARLTBL_DATA_ENTRY(n)	((0x10 * (n)) + 0x18)
#define   ARLTBL_DATA_PORT_ID_MASK	0x1ff
#define   ARLTBL_TC(tc)			((3 & tc) << 11)
#define   ARLTBL_AGE			BIT(14)
#define   ARLTBL_STATIC			BIT(15)
#define   ARLTBL_VALID			BIT(16)

/* Maximum number of bin entries in the ARL for all switches */
#define B53_ARLTBL_MAX_BIN_ENTRIES	4

/* ARL Search Control Register (8 bit) */
#define B53_ARL_SRCH_CTL		0x50
#define B53_ARL_SRCH_CTL_25		0x20
#define   ARL_SRCH_VLID			BIT(0)
#define   ARL_SRCH_STDN			BIT(7)

/* ARL Search Address Register (16 bit) */
#define B53_ARL_SRCH_ADDR		0x51
#define B53_ARL_SRCH_ADDR_25		0x22
#define B53_ARL_SRCH_ADDR_65		0x24
#define  ARL_ADDR_MASK			GENMASK(14, 0)

/* ARL Search MAC/VID Result (64 bit) */
#define B53_ARL_SRCH_RSTL_0_MACVID	0x60

/* Single register search result on 5325 */
#define B53_ARL_SRCH_RSTL_0_MACVID_25	0x24
/* Single register search result on 5365 */
#define B53_ARL_SRCH_RSTL_0_MACVID_65	0x30

/* ARL Search Data Result (32 bit) */
#define B53_ARL_SRCH_RSTL_0		0x68

#define B53_ARL_SRCH_RSTL_MACVID(x)	(B53_ARL_SRCH_RSTL_0_MACVID + ((x) * 0x10))
#define B53_ARL_SRCH_RSTL(x)		(B53_ARL_SRCH_RSTL_0 + ((x) * 0x10))

/*************************************************************************
 * Port VLAN Registers
 *************************************************************************/

/* Port VLAN mask (16 bit) IMP port is always 8, also on 5325 & co */
#define B53_PVLAN_PORT_MASK(i)		((i) * 2)

/* Join all VLANs register (16 bit) */
#define B53_JOIN_ALL_VLAN_EN		0x50

/*************************************************************************
 * 802.1Q Page Registers
 *************************************************************************/

/* Global QoS Control (8 bit) */
#define B53_QOS_GLOBAL_CTL		0x00

/* Enable 802.1Q for individual Ports (16 bit) */
#define B53_802_1P_EN			0x04

/*************************************************************************
 * VLAN Page Registers
 *************************************************************************/

/* VLAN Control 0 (8 bit) */
#define B53_VLAN_CTRL0			0x00
#define   VC0_8021PF_CTRL_MASK		0x3
#define   VC0_8021PF_CTRL_NONE		0x0
#define   VC0_8021PF_CTRL_CHANGE_PRI	0x1
#define   VC0_8021PF_CTRL_CHANGE_VID	0x2
#define   VC0_8021PF_CTRL_CHANGE_BOTH	0x3
#define   VC0_8021QF_CTRL_MASK		0xc
#define   VC0_8021QF_CTRL_CHANGE_PRI	0x1
#define   VC0_8021QF_CTRL_CHANGE_VID	0x2
#define   VC0_8021QF_CTRL_CHANGE_BOTH	0x3
#define   VC0_RESERVED_1		BIT(1)
#define   VC0_DROP_VID_MISS		BIT(4)
#define   VC0_VID_HASH_VID		BIT(5)
#define   VC0_VID_CHK_EN		BIT(6)	/* Use VID,DA or VID,SA */
#define   VC0_VLAN_EN			BIT(7)	/* 802.1Q VLAN Enabled */

/* VLAN Control 1 (8 bit) */
#define B53_VLAN_CTRL1			0x01
#define   VC1_RX_MCST_TAG_EN		BIT(1)
#define   VC1_RX_MCST_FWD_EN		BIT(2)
#define   VC1_RX_MCST_UNTAG_EN		BIT(3)

/* VLAN Control 2 (8 bit) */
#define B53_VLAN_CTRL2			0x02

/* VLAN Control 3 (8 bit when BCM5325, 16 bit else) */
#define B53_VLAN_CTRL3			0x03
#define B53_VLAN_CTRL3_63XX		0x04
#define   VC3_MAXSIZE_1532		BIT(6) /* 5325 only */
#define   VC3_HIGH_8BIT_EN		BIT(7) /* 5325 only */

/* VLAN Control 4 (8 bit) */
#define B53_VLAN_CTRL4			0x05
#define B53_VLAN_CTRL4_25		0x04
#define B53_VLAN_CTRL4_63XX		0x06
#define   VC4_ING_VID_CHECK_S		6
#define   VC4_ING_VID_CHECK_MASK	(0x3 << VC4_ING_VID_CHECK_S)
#define   VC4_ING_VID_VIO_FWD		0 /* forward, but do not learn */
#define   VC4_ING_VID_VIO_DROP		1 /* drop VID violations */
#define   VC4_NO_ING_VID_CHK		2 /* do not check */
#define   VC4_ING_VID_VIO_TO_IMP	3 /* redirect to MII port */

/* VLAN Control 5 (8 bit) */
#define B53_VLAN_CTRL5			0x06
#define B53_VLAN_CTRL5_25		0x05
#define B53_VLAN_CTRL5_63XX		0x07
#define   VC5_VID_FFF_EN		BIT(2)
#define   VC5_DROP_VTABLE_MISS		BIT(3)

/* VLAN Control 6 (8 bit) */
#define B53_VLAN_CTRL6			0x07
#define B53_VLAN_CTRL6_63XX		0x08

/* VLAN Table Access Register (16 bit) */
#define B53_VLAN_TABLE_ACCESS_25	0x06	/* BCM5325E/5350 */
#define B53_VLAN_TABLE_ACCESS_65	0x08	/* BCM5365 */
#define   VTA_VID_LOW_MASK_25		0xf
#define   VTA_VID_LOW_MASK_65		0xff
#define   VTA_VID_HIGH_S_25		4
#define   VTA_VID_HIGH_S_65		8
#define   VTA_VID_HIGH_MASK_25		(0xff << VTA_VID_HIGH_S_25E)
#define   VTA_VID_HIGH_MASK_65		(0xf << VTA_VID_HIGH_S_65)
#define   VTA_RW_STATE			BIT(12)
#define   VTA_RW_STATE_RD		0
#define   VTA_RW_STATE_WR		BIT(12)
#define   VTA_RW_OP_EN			BIT(13)

/* VLAN Read/Write Registers for (16/32 bit) */
#define B53_VLAN_WRITE_25		0x08
#define B53_VLAN_WRITE_65		0x0a
#define B53_VLAN_READ			0x0c
#define   VA_MEMBER_MASK		0x3f
#define   VA_UNTAG_S_25			6
#define   VA_UNTAG_MASK_25		0x3f
#define   VA_UNTAG_S_65			7
#define   VA_UNTAG_MASK_65		0x1f
#define   VA_VID_HIGH_S			12
#define   VA_VID_HIGH_MASK		(0xffff << VA_VID_HIGH_S)
#define   VA_VALID_25			BIT(20)
#define   VA_VALID_25_R4		BIT(24)
#define   VA_VALID_65			BIT(14)

/* VLAN Port Default Tag (16 bit) */
#define B53_VLAN_PORT_DEF_TAG(i)	(0x10 + 2 * (i))

/*************************************************************************
 * Jumbo Frame Page Registers
 *************************************************************************/

/* Jumbo Enable Port Mask (bit i == port i enabled) (32 bit) */
#define B53_JUMBO_PORT_MASK		0x01
#define B53_JUMBO_PORT_MASK_63XX	0x04
#define   JPM_10_100_JUMBO_EN		BIT(24) /* GigE always enabled */

/* Good Frame Max Size without 802.1Q TAG (16 bit) */
#define B53_JUMBO_MAX_SIZE		0x05
#define B53_JUMBO_MAX_SIZE_63XX		0x08
#define   JMS_MIN_SIZE			1518
#define   JMS_MAX_SIZE			9724

/*************************************************************************
 * EEE Configuration Page Registers
 *************************************************************************/

/* EEE Enable control register (16 bit) */
#define B53_EEE_EN_CTRL			0x00

/* EEE LPI assert status register (16 bit) */
#define B53_EEE_LPI_ASSERT_STS		0x02

/* EEE LPI indicate status register (16 bit) */
#define B53_EEE_LPI_INDICATE		0x4

/* EEE Receiving idle symbols status register (16 bit) */
#define B53_EEE_RX_IDLE_SYM_STS		0x6

/* EEE Pipeline timer register (32 bit) */
#define B53_EEE_PIP_TIMER		0xC

/* EEE Sleep timer Gig register (32 bit) */
#define B53_EEE_SLEEP_TIMER_GIG(i)	(0x10 + 4 * (i))

/* EEE Sleep timer FE register (32 bit) */
#define B53_EEE_SLEEP_TIMER_FE(i)	(0x34 + 4 * (i))

/* EEE Minimum LP timer Gig register (32 bit) */
#define B53_EEE_MIN_LP_TIMER_GIG(i)	(0x58 + 4 * (i))

/* EEE Minimum LP timer FE register (32 bit) */
#define B53_EEE_MIN_LP_TIMER_FE(i)	(0x7c + 4 * (i))

/* EEE Wake timer Gig register (16 bit) */
#define B53_EEE_WAKE_TIMER_GIG(i)	(0xa0 + 2 * (i))

/* EEE Wake timer FE register (16 bit) */
#define B53_EEE_WAKE_TIMER_FE(i)	(0xb2 + 2 * (i))


/*************************************************************************
 * CFP Configuration Page Registers
 *************************************************************************/

/* CFP Control Register with ports map (8 bit) */
#define B53_CFP_CTRL			0x00

#endif /* !__B53_REGS_H */
