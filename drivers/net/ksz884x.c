/**
 * drivers/net/ksx884x.c - Micrel KSZ8841/2 PCI Ethernet driver
 *
 * Copyright (c) 2009-2010 Micrel, Inc.
 * 	Tristram Ha <Tristram.Ha@micrel.com>
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/mii.h>
#include <linux/platform_device.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <linux/sched.h>
#include <linux/slab.h>


/* DMA Registers */

#define KS_DMA_TX_CTRL			0x0000
#define DMA_TX_ENABLE			0x00000001
#define DMA_TX_CRC_ENABLE		0x00000002
#define DMA_TX_PAD_ENABLE		0x00000004
#define DMA_TX_LOOPBACK			0x00000100
#define DMA_TX_FLOW_ENABLE		0x00000200
#define DMA_TX_CSUM_IP			0x00010000
#define DMA_TX_CSUM_TCP			0x00020000
#define DMA_TX_CSUM_UDP			0x00040000
#define DMA_TX_BURST_SIZE		0x3F000000

#define KS_DMA_RX_CTRL			0x0004
#define DMA_RX_ENABLE			0x00000001
#define KS884X_DMA_RX_MULTICAST		0x00000002
#define DMA_RX_PROMISCUOUS		0x00000004
#define DMA_RX_ERROR			0x00000008
#define DMA_RX_UNICAST			0x00000010
#define DMA_RX_ALL_MULTICAST		0x00000020
#define DMA_RX_BROADCAST		0x00000040
#define DMA_RX_FLOW_ENABLE		0x00000200
#define DMA_RX_CSUM_IP			0x00010000
#define DMA_RX_CSUM_TCP			0x00020000
#define DMA_RX_CSUM_UDP			0x00040000
#define DMA_RX_BURST_SIZE		0x3F000000

#define DMA_BURST_SHIFT			24
#define DMA_BURST_DEFAULT		8

#define KS_DMA_TX_START			0x0008
#define KS_DMA_RX_START			0x000C
#define DMA_START			0x00000001

#define KS_DMA_TX_ADDR			0x0010
#define KS_DMA_RX_ADDR			0x0014

#define DMA_ADDR_LIST_MASK		0xFFFFFFFC
#define DMA_ADDR_LIST_SHIFT		2

/* MTR0 */
#define KS884X_MULTICAST_0_OFFSET	0x0020
#define KS884X_MULTICAST_1_OFFSET	0x0021
#define KS884X_MULTICAST_2_OFFSET	0x0022
#define KS884x_MULTICAST_3_OFFSET	0x0023
/* MTR1 */
#define KS884X_MULTICAST_4_OFFSET	0x0024
#define KS884X_MULTICAST_5_OFFSET	0x0025
#define KS884X_MULTICAST_6_OFFSET	0x0026
#define KS884X_MULTICAST_7_OFFSET	0x0027

/* Interrupt Registers */

/* INTEN */
#define KS884X_INTERRUPTS_ENABLE	0x0028
/* INTST */
#define KS884X_INTERRUPTS_STATUS	0x002C

#define KS884X_INT_RX_STOPPED		0x02000000
#define KS884X_INT_TX_STOPPED		0x04000000
#define KS884X_INT_RX_OVERRUN		0x08000000
#define KS884X_INT_TX_EMPTY		0x10000000
#define KS884X_INT_RX			0x20000000
#define KS884X_INT_TX			0x40000000
#define KS884X_INT_PHY			0x80000000

#define KS884X_INT_RX_MASK		\
	(KS884X_INT_RX | KS884X_INT_RX_OVERRUN)
#define KS884X_INT_TX_MASK		\
	(KS884X_INT_TX | KS884X_INT_TX_EMPTY)
#define KS884X_INT_MASK	(KS884X_INT_RX | KS884X_INT_TX | KS884X_INT_PHY)

/* MAC Additional Station Address */

/* MAAL0 */
#define KS_ADD_ADDR_0_LO		0x0080
/* MAAH0 */
#define KS_ADD_ADDR_0_HI		0x0084
/* MAAL1 */
#define KS_ADD_ADDR_1_LO		0x0088
/* MAAH1 */
#define KS_ADD_ADDR_1_HI		0x008C
/* MAAL2 */
#define KS_ADD_ADDR_2_LO		0x0090
/* MAAH2 */
#define KS_ADD_ADDR_2_HI		0x0094
/* MAAL3 */
#define KS_ADD_ADDR_3_LO		0x0098
/* MAAH3 */
#define KS_ADD_ADDR_3_HI		0x009C
/* MAAL4 */
#define KS_ADD_ADDR_4_LO		0x00A0
/* MAAH4 */
#define KS_ADD_ADDR_4_HI		0x00A4
/* MAAL5 */
#define KS_ADD_ADDR_5_LO		0x00A8
/* MAAH5 */
#define KS_ADD_ADDR_5_HI		0x00AC
/* MAAL6 */
#define KS_ADD_ADDR_6_LO		0x00B0
/* MAAH6 */
#define KS_ADD_ADDR_6_HI		0x00B4
/* MAAL7 */
#define KS_ADD_ADDR_7_LO		0x00B8
/* MAAH7 */
#define KS_ADD_ADDR_7_HI		0x00BC
/* MAAL8 */
#define KS_ADD_ADDR_8_LO		0x00C0
/* MAAH8 */
#define KS_ADD_ADDR_8_HI		0x00C4
/* MAAL9 */
#define KS_ADD_ADDR_9_LO		0x00C8
/* MAAH9 */
#define KS_ADD_ADDR_9_HI		0x00CC
/* MAAL10 */
#define KS_ADD_ADDR_A_LO		0x00D0
/* MAAH10 */
#define KS_ADD_ADDR_A_HI		0x00D4
/* MAAL11 */
#define KS_ADD_ADDR_B_LO		0x00D8
/* MAAH11 */
#define KS_ADD_ADDR_B_HI		0x00DC
/* MAAL12 */
#define KS_ADD_ADDR_C_LO		0x00E0
/* MAAH12 */
#define KS_ADD_ADDR_C_HI		0x00E4
/* MAAL13 */
#define KS_ADD_ADDR_D_LO		0x00E8
/* MAAH13 */
#define KS_ADD_ADDR_D_HI		0x00EC
/* MAAL14 */
#define KS_ADD_ADDR_E_LO		0x00F0
/* MAAH14 */
#define KS_ADD_ADDR_E_HI		0x00F4
/* MAAL15 */
#define KS_ADD_ADDR_F_LO		0x00F8
/* MAAH15 */
#define KS_ADD_ADDR_F_HI		0x00FC

#define ADD_ADDR_HI_MASK		0x0000FFFF
#define ADD_ADDR_ENABLE			0x80000000
#define ADD_ADDR_INCR			8

/* Miscellaneous Registers */

/* MARL */
#define KS884X_ADDR_0_OFFSET		0x0200
#define KS884X_ADDR_1_OFFSET		0x0201
/* MARM */
#define KS884X_ADDR_2_OFFSET		0x0202
#define KS884X_ADDR_3_OFFSET		0x0203
/* MARH */
#define KS884X_ADDR_4_OFFSET		0x0204
#define KS884X_ADDR_5_OFFSET		0x0205

/* OBCR */
#define KS884X_BUS_CTRL_OFFSET		0x0210

#define BUS_SPEED_125_MHZ		0x0000
#define BUS_SPEED_62_5_MHZ		0x0001
#define BUS_SPEED_41_66_MHZ		0x0002
#define BUS_SPEED_25_MHZ		0x0003

/* EEPCR */
#define KS884X_EEPROM_CTRL_OFFSET	0x0212

#define EEPROM_CHIP_SELECT		0x0001
#define EEPROM_SERIAL_CLOCK		0x0002
#define EEPROM_DATA_OUT			0x0004
#define EEPROM_DATA_IN			0x0008
#define EEPROM_ACCESS_ENABLE		0x0010

/* MBIR */
#define KS884X_MEM_INFO_OFFSET		0x0214

#define RX_MEM_TEST_FAILED		0x0008
#define RX_MEM_TEST_FINISHED		0x0010
#define TX_MEM_TEST_FAILED		0x0800
#define TX_MEM_TEST_FINISHED		0x1000

/* GCR */
#define KS884X_GLOBAL_CTRL_OFFSET	0x0216
#define GLOBAL_SOFTWARE_RESET		0x0001

#define KS8841_POWER_MANAGE_OFFSET	0x0218

/* WFCR */
#define KS8841_WOL_CTRL_OFFSET		0x021A
#define KS8841_WOL_MAGIC_ENABLE		0x0080
#define KS8841_WOL_FRAME3_ENABLE	0x0008
#define KS8841_WOL_FRAME2_ENABLE	0x0004
#define KS8841_WOL_FRAME1_ENABLE	0x0002
#define KS8841_WOL_FRAME0_ENABLE	0x0001

/* WF0 */
#define KS8841_WOL_FRAME_CRC_OFFSET	0x0220
#define KS8841_WOL_FRAME_BYTE0_OFFSET	0x0224
#define KS8841_WOL_FRAME_BYTE2_OFFSET	0x0228

/* IACR */
#define KS884X_IACR_P			0x04A0
#define KS884X_IACR_OFFSET		KS884X_IACR_P

/* IADR1 */
#define KS884X_IADR1_P			0x04A2
#define KS884X_IADR2_P			0x04A4
#define KS884X_IADR3_P			0x04A6
#define KS884X_IADR4_P			0x04A8
#define KS884X_IADR5_P			0x04AA

#define KS884X_ACC_CTRL_SEL_OFFSET	KS884X_IACR_P
#define KS884X_ACC_CTRL_INDEX_OFFSET	(KS884X_ACC_CTRL_SEL_OFFSET + 1)

#define KS884X_ACC_DATA_0_OFFSET	KS884X_IADR4_P
#define KS884X_ACC_DATA_1_OFFSET	(KS884X_ACC_DATA_0_OFFSET + 1)
#define KS884X_ACC_DATA_2_OFFSET	KS884X_IADR5_P
#define KS884X_ACC_DATA_3_OFFSET	(KS884X_ACC_DATA_2_OFFSET + 1)
#define KS884X_ACC_DATA_4_OFFSET	KS884X_IADR2_P
#define KS884X_ACC_DATA_5_OFFSET	(KS884X_ACC_DATA_4_OFFSET + 1)
#define KS884X_ACC_DATA_6_OFFSET	KS884X_IADR3_P
#define KS884X_ACC_DATA_7_OFFSET	(KS884X_ACC_DATA_6_OFFSET + 1)
#define KS884X_ACC_DATA_8_OFFSET	KS884X_IADR1_P

/* P1MBCR */
#define KS884X_P1MBCR_P			0x04D0
#define KS884X_P1MBSR_P			0x04D2
#define KS884X_PHY1ILR_P		0x04D4
#define KS884X_PHY1IHR_P		0x04D6
#define KS884X_P1ANAR_P			0x04D8
#define KS884X_P1ANLPR_P		0x04DA

/* P2MBCR */
#define KS884X_P2MBCR_P			0x04E0
#define KS884X_P2MBSR_P			0x04E2
#define KS884X_PHY2ILR_P		0x04E4
#define KS884X_PHY2IHR_P		0x04E6
#define KS884X_P2ANAR_P			0x04E8
#define KS884X_P2ANLPR_P		0x04EA

#define KS884X_PHY_1_CTRL_OFFSET	KS884X_P1MBCR_P
#define PHY_CTRL_INTERVAL		(KS884X_P2MBCR_P - KS884X_P1MBCR_P)

#define KS884X_PHY_CTRL_OFFSET		0x00

/* Mode Control Register */
#define PHY_REG_CTRL			0

#define PHY_RESET			0x8000
#define PHY_LOOPBACK			0x4000
#define PHY_SPEED_100MBIT		0x2000
#define PHY_AUTO_NEG_ENABLE		0x1000
#define PHY_POWER_DOWN			0x0800
#define PHY_MII_DISABLE			0x0400
#define PHY_AUTO_NEG_RESTART		0x0200
#define PHY_FULL_DUPLEX			0x0100
#define PHY_COLLISION_TEST		0x0080
#define PHY_HP_MDIX			0x0020
#define PHY_FORCE_MDIX			0x0010
#define PHY_AUTO_MDIX_DISABLE		0x0008
#define PHY_REMOTE_FAULT_DISABLE	0x0004
#define PHY_TRANSMIT_DISABLE		0x0002
#define PHY_LED_DISABLE			0x0001

#define KS884X_PHY_STATUS_OFFSET	0x02

/* Mode Status Register */
#define PHY_REG_STATUS			1

#define PHY_100BT4_CAPABLE		0x8000
#define PHY_100BTX_FD_CAPABLE		0x4000
#define PHY_100BTX_CAPABLE		0x2000
#define PHY_10BT_FD_CAPABLE		0x1000
#define PHY_10BT_CAPABLE		0x0800
#define PHY_MII_SUPPRESS_CAPABLE	0x0040
#define PHY_AUTO_NEG_ACKNOWLEDGE	0x0020
#define PHY_REMOTE_FAULT		0x0010
#define PHY_AUTO_NEG_CAPABLE		0x0008
#define PHY_LINK_STATUS			0x0004
#define PHY_JABBER_DETECT		0x0002
#define PHY_EXTENDED_CAPABILITY		0x0001

#define KS884X_PHY_ID_1_OFFSET		0x04
#define KS884X_PHY_ID_2_OFFSET		0x06

/* PHY Identifier Registers */
#define PHY_REG_ID_1			2
#define PHY_REG_ID_2			3

#define KS884X_PHY_AUTO_NEG_OFFSET	0x08

/* Auto-Negotiation Advertisement Register */
#define PHY_REG_AUTO_NEGOTIATION	4

#define PHY_AUTO_NEG_NEXT_PAGE		0x8000
#define PHY_AUTO_NEG_REMOTE_FAULT	0x2000
/* Not supported. */
#define PHY_AUTO_NEG_ASYM_PAUSE		0x0800
#define PHY_AUTO_NEG_SYM_PAUSE		0x0400
#define PHY_AUTO_NEG_100BT4		0x0200
#define PHY_AUTO_NEG_100BTX_FD		0x0100
#define PHY_AUTO_NEG_100BTX		0x0080
#define PHY_AUTO_NEG_10BT_FD		0x0040
#define PHY_AUTO_NEG_10BT		0x0020
#define PHY_AUTO_NEG_SELECTOR		0x001F
#define PHY_AUTO_NEG_802_3		0x0001

#define PHY_AUTO_NEG_PAUSE  (PHY_AUTO_NEG_SYM_PAUSE | PHY_AUTO_NEG_ASYM_PAUSE)

#define KS884X_PHY_REMOTE_CAP_OFFSET	0x0A

/* Auto-Negotiation Link Partner Ability Register */
#define PHY_REG_REMOTE_CAPABILITY	5

#define PHY_REMOTE_NEXT_PAGE		0x8000
#define PHY_REMOTE_ACKNOWLEDGE		0x4000
#define PHY_REMOTE_REMOTE_FAULT		0x2000
#define PHY_REMOTE_SYM_PAUSE		0x0400
#define PHY_REMOTE_100BTX_FD		0x0100
#define PHY_REMOTE_100BTX		0x0080
#define PHY_REMOTE_10BT_FD		0x0040
#define PHY_REMOTE_10BT			0x0020

/* P1VCT */
#define KS884X_P1VCT_P			0x04F0
#define KS884X_P1PHYCTRL_P		0x04F2

/* P2VCT */
#define KS884X_P2VCT_P			0x04F4
#define KS884X_P2PHYCTRL_P		0x04F6

#define KS884X_PHY_SPECIAL_OFFSET	KS884X_P1VCT_P
#define PHY_SPECIAL_INTERVAL		(KS884X_P2VCT_P - KS884X_P1VCT_P)

#define KS884X_PHY_LINK_MD_OFFSET	0x00

#define PHY_START_CABLE_DIAG		0x8000
#define PHY_CABLE_DIAG_RESULT		0x6000
#define PHY_CABLE_STAT_NORMAL		0x0000
#define PHY_CABLE_STAT_OPEN		0x2000
#define PHY_CABLE_STAT_SHORT		0x4000
#define PHY_CABLE_STAT_FAILED		0x6000
#define PHY_CABLE_10M_SHORT		0x1000
#define PHY_CABLE_FAULT_COUNTER		0x01FF

#define KS884X_PHY_PHY_CTRL_OFFSET	0x02

#define PHY_STAT_REVERSED_POLARITY	0x0020
#define PHY_STAT_MDIX			0x0010
#define PHY_FORCE_LINK			0x0008
#define PHY_POWER_SAVING_DISABLE	0x0004
#define PHY_REMOTE_LOOPBACK		0x0002

/* SIDER */
#define KS884X_SIDER_P			0x0400
#define KS884X_CHIP_ID_OFFSET		KS884X_SIDER_P
#define KS884X_FAMILY_ID_OFFSET		(KS884X_CHIP_ID_OFFSET + 1)

#define REG_FAMILY_ID			0x88

#define REG_CHIP_ID_41			0x8810
#define REG_CHIP_ID_42			0x8800

#define KS884X_CHIP_ID_MASK_41		0xFF10
#define KS884X_CHIP_ID_MASK		0xFFF0
#define KS884X_CHIP_ID_SHIFT		4
#define KS884X_REVISION_MASK		0x000E
#define KS884X_REVISION_SHIFT		1
#define KS8842_START			0x0001

#define CHIP_IP_41_M			0x8810
#define CHIP_IP_42_M			0x8800
#define CHIP_IP_61_M			0x8890
#define CHIP_IP_62_M			0x8880

#define CHIP_IP_41_P			0x8850
#define CHIP_IP_42_P			0x8840
#define CHIP_IP_61_P			0x88D0
#define CHIP_IP_62_P			0x88C0

/* SGCR1 */
#define KS8842_SGCR1_P			0x0402
#define KS8842_SWITCH_CTRL_1_OFFSET	KS8842_SGCR1_P

#define SWITCH_PASS_ALL			0x8000
#define SWITCH_TX_FLOW_CTRL		0x2000
#define SWITCH_RX_FLOW_CTRL		0x1000
#define SWITCH_CHECK_LENGTH		0x0800
#define SWITCH_AGING_ENABLE		0x0400
#define SWITCH_FAST_AGING		0x0200
#define SWITCH_AGGR_BACKOFF		0x0100
#define SWITCH_PASS_PAUSE		0x0008
#define SWITCH_LINK_AUTO_AGING		0x0001

/* SGCR2 */
#define KS8842_SGCR2_P			0x0404
#define KS8842_SWITCH_CTRL_2_OFFSET	KS8842_SGCR2_P

#define SWITCH_VLAN_ENABLE		0x8000
#define SWITCH_IGMP_SNOOP		0x4000
#define IPV6_MLD_SNOOP_ENABLE		0x2000
#define IPV6_MLD_SNOOP_OPTION		0x1000
#define PRIORITY_SCHEME_SELECT		0x0800
#define SWITCH_MIRROR_RX_TX		0x0100
#define UNICAST_VLAN_BOUNDARY		0x0080
#define MULTICAST_STORM_DISABLE		0x0040
#define SWITCH_BACK_PRESSURE		0x0020
#define FAIR_FLOW_CTRL			0x0010
#define NO_EXC_COLLISION_DROP		0x0008
#define SWITCH_HUGE_PACKET		0x0004
#define SWITCH_LEGAL_PACKET		0x0002
#define SWITCH_BUF_RESERVE		0x0001

/* SGCR3 */
#define KS8842_SGCR3_P			0x0406
#define KS8842_SWITCH_CTRL_3_OFFSET	KS8842_SGCR3_P

#define BROADCAST_STORM_RATE_LO		0xFF00
#define SWITCH_REPEATER			0x0080
#define SWITCH_HALF_DUPLEX		0x0040
#define SWITCH_FLOW_CTRL		0x0020
#define SWITCH_10_MBIT			0x0010
#define SWITCH_REPLACE_NULL_VID		0x0008
#define BROADCAST_STORM_RATE_HI		0x0007

#define BROADCAST_STORM_RATE		0x07FF

/* SGCR4 */
#define KS8842_SGCR4_P			0x0408

/* SGCR5 */
#define KS8842_SGCR5_P			0x040A
#define KS8842_SWITCH_CTRL_5_OFFSET	KS8842_SGCR5_P

#define LED_MODE			0x8200
#define LED_SPEED_DUPLEX_ACT		0x0000
#define LED_SPEED_DUPLEX_LINK_ACT	0x8000
#define LED_DUPLEX_10_100		0x0200

/* SGCR6 */
#define KS8842_SGCR6_P			0x0410
#define KS8842_SWITCH_CTRL_6_OFFSET	KS8842_SGCR6_P

#define KS8842_PRIORITY_MASK		3
#define KS8842_PRIORITY_SHIFT		2

/* SGCR7 */
#define KS8842_SGCR7_P			0x0412
#define KS8842_SWITCH_CTRL_7_OFFSET	KS8842_SGCR7_P

#define SWITCH_UNK_DEF_PORT_ENABLE	0x0008
#define SWITCH_UNK_DEF_PORT_3		0x0004
#define SWITCH_UNK_DEF_PORT_2		0x0002
#define SWITCH_UNK_DEF_PORT_1		0x0001

/* MACAR1 */
#define KS8842_MACAR1_P			0x0470
#define KS8842_MACAR2_P			0x0472
#define KS8842_MACAR3_P			0x0474
#define KS8842_MAC_ADDR_1_OFFSET	KS8842_MACAR1_P
#define KS8842_MAC_ADDR_0_OFFSET	(KS8842_MAC_ADDR_1_OFFSET + 1)
#define KS8842_MAC_ADDR_3_OFFSET	KS8842_MACAR2_P
#define KS8842_MAC_ADDR_2_OFFSET	(KS8842_MAC_ADDR_3_OFFSET + 1)
#define KS8842_MAC_ADDR_5_OFFSET	KS8842_MACAR3_P
#define KS8842_MAC_ADDR_4_OFFSET	(KS8842_MAC_ADDR_5_OFFSET + 1)

/* TOSR1 */
#define KS8842_TOSR1_P			0x0480
#define KS8842_TOSR2_P			0x0482
#define KS8842_TOSR3_P			0x0484
#define KS8842_TOSR4_P			0x0486
#define KS8842_TOSR5_P			0x0488
#define KS8842_TOSR6_P			0x048A
#define KS8842_TOSR7_P			0x0490
#define KS8842_TOSR8_P			0x0492
#define KS8842_TOS_1_OFFSET		KS8842_TOSR1_P
#define KS8842_TOS_2_OFFSET		KS8842_TOSR2_P
#define KS8842_TOS_3_OFFSET		KS8842_TOSR3_P
#define KS8842_TOS_4_OFFSET		KS8842_TOSR4_P
#define KS8842_TOS_5_OFFSET		KS8842_TOSR5_P
#define KS8842_TOS_6_OFFSET		KS8842_TOSR6_P

#define KS8842_TOS_7_OFFSET		KS8842_TOSR7_P
#define KS8842_TOS_8_OFFSET		KS8842_TOSR8_P

/* P1CR1 */
#define KS8842_P1CR1_P			0x0500
#define KS8842_P1CR2_P			0x0502
#define KS8842_P1VIDR_P			0x0504
#define KS8842_P1CR3_P			0x0506
#define KS8842_P1IRCR_P			0x0508
#define KS8842_P1ERCR_P			0x050A
#define KS884X_P1SCSLMD_P		0x0510
#define KS884X_P1CR4_P			0x0512
#define KS884X_P1SR_P			0x0514

/* P2CR1 */
#define KS8842_P2CR1_P			0x0520
#define KS8842_P2CR2_P			0x0522
#define KS8842_P2VIDR_P			0x0524
#define KS8842_P2CR3_P			0x0526
#define KS8842_P2IRCR_P			0x0528
#define KS8842_P2ERCR_P			0x052A
#define KS884X_P2SCSLMD_P		0x0530
#define KS884X_P2CR4_P			0x0532
#define KS884X_P2SR_P			0x0534

/* P3CR1 */
#define KS8842_P3CR1_P			0x0540
#define KS8842_P3CR2_P			0x0542
#define KS8842_P3VIDR_P			0x0544
#define KS8842_P3CR3_P			0x0546
#define KS8842_P3IRCR_P			0x0548
#define KS8842_P3ERCR_P			0x054A

#define KS8842_PORT_1_CTRL_1		KS8842_P1CR1_P
#define KS8842_PORT_2_CTRL_1		KS8842_P2CR1_P
#define KS8842_PORT_3_CTRL_1		KS8842_P3CR1_P

#define PORT_CTRL_ADDR(port, addr)		\
	(addr = KS8842_PORT_1_CTRL_1 + (port) *	\
		(KS8842_PORT_2_CTRL_1 - KS8842_PORT_1_CTRL_1))

#define KS8842_PORT_CTRL_1_OFFSET	0x00

#define PORT_BROADCAST_STORM		0x0080
#define PORT_DIFFSERV_ENABLE		0x0040
#define PORT_802_1P_ENABLE		0x0020
#define PORT_BASED_PRIORITY_MASK	0x0018
#define PORT_BASED_PRIORITY_BASE	0x0003
#define PORT_BASED_PRIORITY_SHIFT	3
#define PORT_BASED_PRIORITY_0		0x0000
#define PORT_BASED_PRIORITY_1		0x0008
#define PORT_BASED_PRIORITY_2		0x0010
#define PORT_BASED_PRIORITY_3		0x0018
#define PORT_INSERT_TAG			0x0004
#define PORT_REMOVE_TAG			0x0002
#define PORT_PRIO_QUEUE_ENABLE		0x0001

#define KS8842_PORT_CTRL_2_OFFSET	0x02

#define PORT_INGRESS_VLAN_FILTER	0x4000
#define PORT_DISCARD_NON_VID		0x2000
#define PORT_FORCE_FLOW_CTRL		0x1000
#define PORT_BACK_PRESSURE		0x0800
#define PORT_TX_ENABLE			0x0400
#define PORT_RX_ENABLE			0x0200
#define PORT_LEARN_DISABLE		0x0100
#define PORT_MIRROR_SNIFFER		0x0080
#define PORT_MIRROR_RX			0x0040
#define PORT_MIRROR_TX			0x0020
#define PORT_USER_PRIORITY_CEILING	0x0008
#define PORT_VLAN_MEMBERSHIP		0x0007

#define KS8842_PORT_CTRL_VID_OFFSET	0x04

#define PORT_DEFAULT_VID		0x0001

#define KS8842_PORT_CTRL_3_OFFSET	0x06

#define PORT_INGRESS_LIMIT_MODE		0x000C
#define PORT_INGRESS_ALL		0x0000
#define PORT_INGRESS_UNICAST		0x0004
#define PORT_INGRESS_MULTICAST		0x0008
#define PORT_INGRESS_BROADCAST		0x000C
#define PORT_COUNT_IFG			0x0002
#define PORT_COUNT_PREAMBLE		0x0001

#define KS8842_PORT_IN_RATE_OFFSET	0x08
#define KS8842_PORT_OUT_RATE_OFFSET	0x0A

#define PORT_PRIORITY_RATE		0x0F
#define PORT_PRIORITY_RATE_SHIFT	4

#define KS884X_PORT_LINK_MD		0x10

#define PORT_CABLE_10M_SHORT		0x8000
#define PORT_CABLE_DIAG_RESULT		0x6000
#define PORT_CABLE_STAT_NORMAL		0x0000
#define PORT_CABLE_STAT_OPEN		0x2000
#define PORT_CABLE_STAT_SHORT		0x4000
#define PORT_CABLE_STAT_FAILED		0x6000
#define PORT_START_CABLE_DIAG		0x1000
#define PORT_FORCE_LINK			0x0800
#define PORT_POWER_SAVING_DISABLE	0x0400
#define PORT_PHY_REMOTE_LOOPBACK	0x0200
#define PORT_CABLE_FAULT_COUNTER	0x01FF

#define KS884X_PORT_CTRL_4_OFFSET	0x12

#define PORT_LED_OFF			0x8000
#define PORT_TX_DISABLE			0x4000
#define PORT_AUTO_NEG_RESTART		0x2000
#define PORT_REMOTE_FAULT_DISABLE	0x1000
#define PORT_POWER_DOWN			0x0800
#define PORT_AUTO_MDIX_DISABLE		0x0400
#define PORT_FORCE_MDIX			0x0200
#define PORT_LOOPBACK			0x0100
#define PORT_AUTO_NEG_ENABLE		0x0080
#define PORT_FORCE_100_MBIT		0x0040
#define PORT_FORCE_FULL_DUPLEX		0x0020
#define PORT_AUTO_NEG_SYM_PAUSE		0x0010
#define PORT_AUTO_NEG_100BTX_FD		0x0008
#define PORT_AUTO_NEG_100BTX		0x0004
#define PORT_AUTO_NEG_10BT_FD		0x0002
#define PORT_AUTO_NEG_10BT		0x0001

#define KS884X_PORT_STATUS_OFFSET	0x14

#define PORT_HP_MDIX			0x8000
#define PORT_REVERSED_POLARITY		0x2000
#define PORT_RX_FLOW_CTRL		0x0800
#define PORT_TX_FLOW_CTRL		0x1000
#define PORT_STATUS_SPEED_100MBIT	0x0400
#define PORT_STATUS_FULL_DUPLEX		0x0200
#define PORT_REMOTE_FAULT		0x0100
#define PORT_MDIX_STATUS		0x0080
#define PORT_AUTO_NEG_COMPLETE		0x0040
#define PORT_STATUS_LINK_GOOD		0x0020
#define PORT_REMOTE_SYM_PAUSE		0x0010
#define PORT_REMOTE_100BTX_FD		0x0008
#define PORT_REMOTE_100BTX		0x0004
#define PORT_REMOTE_10BT_FD		0x0002
#define PORT_REMOTE_10BT		0x0001

/*
#define STATIC_MAC_TABLE_ADDR		00-0000FFFF-FFFFFFFF
#define STATIC_MAC_TABLE_FWD_PORTS	00-00070000-00000000
#define STATIC_MAC_TABLE_VALID		00-00080000-00000000
#define STATIC_MAC_TABLE_OVERRIDE	00-00100000-00000000
#define STATIC_MAC_TABLE_USE_FID	00-00200000-00000000
#define STATIC_MAC_TABLE_FID		00-03C00000-00000000
*/

#define STATIC_MAC_TABLE_ADDR		0x0000FFFF
#define STATIC_MAC_TABLE_FWD_PORTS	0x00070000
#define STATIC_MAC_TABLE_VALID		0x00080000
#define STATIC_MAC_TABLE_OVERRIDE	0x00100000
#define STATIC_MAC_TABLE_USE_FID	0x00200000
#define STATIC_MAC_TABLE_FID		0x03C00000

#define STATIC_MAC_FWD_PORTS_SHIFT	16
#define STATIC_MAC_FID_SHIFT		22

/*
#define VLAN_TABLE_VID			00-00000000-00000FFF
#define VLAN_TABLE_FID			00-00000000-0000F000
#define VLAN_TABLE_MEMBERSHIP		00-00000000-00070000
#define VLAN_TABLE_VALID		00-00000000-00080000
*/

#define VLAN_TABLE_VID			0x00000FFF
#define VLAN_TABLE_FID			0x0000F000
#define VLAN_TABLE_MEMBERSHIP		0x00070000
#define VLAN_TABLE_VALID		0x00080000

#define VLAN_TABLE_FID_SHIFT		12
#define VLAN_TABLE_MEMBERSHIP_SHIFT	16

/*
#define DYNAMIC_MAC_TABLE_ADDR		00-0000FFFF-FFFFFFFF
#define DYNAMIC_MAC_TABLE_FID		00-000F0000-00000000
#define DYNAMIC_MAC_TABLE_SRC_PORT	00-00300000-00000000
#define DYNAMIC_MAC_TABLE_TIMESTAMP	00-00C00000-00000000
#define DYNAMIC_MAC_TABLE_ENTRIES	03-FF000000-00000000
#define DYNAMIC_MAC_TABLE_MAC_EMPTY	04-00000000-00000000
#define DYNAMIC_MAC_TABLE_RESERVED	78-00000000-00000000
#define DYNAMIC_MAC_TABLE_NOT_READY	80-00000000-00000000
*/

#define DYNAMIC_MAC_TABLE_ADDR		0x0000FFFF
#define DYNAMIC_MAC_TABLE_FID		0x000F0000
#define DYNAMIC_MAC_TABLE_SRC_PORT	0x00300000
#define DYNAMIC_MAC_TABLE_TIMESTAMP	0x00C00000
#define DYNAMIC_MAC_TABLE_ENTRIES	0xFF000000

#define DYNAMIC_MAC_TABLE_ENTRIES_H	0x03
#define DYNAMIC_MAC_TABLE_MAC_EMPTY	0x04
#define DYNAMIC_MAC_TABLE_RESERVED	0x78
#define DYNAMIC_MAC_TABLE_NOT_READY	0x80

#define DYNAMIC_MAC_FID_SHIFT		16
#define DYNAMIC_MAC_SRC_PORT_SHIFT	20
#define DYNAMIC_MAC_TIMESTAMP_SHIFT	22
#define DYNAMIC_MAC_ENTRIES_SHIFT	24
#define DYNAMIC_MAC_ENTRIES_H_SHIFT	8

/*
#define MIB_COUNTER_VALUE		00-00000000-3FFFFFFF
#define MIB_COUNTER_VALID		00-00000000-40000000
#define MIB_COUNTER_OVERFLOW		00-00000000-80000000
*/

#define MIB_COUNTER_VALUE		0x3FFFFFFF
#define MIB_COUNTER_VALID		0x40000000
#define MIB_COUNTER_OVERFLOW		0x80000000

#define MIB_PACKET_DROPPED		0x0000FFFF

#define KS_MIB_PACKET_DROPPED_TX_0	0x100
#define KS_MIB_PACKET_DROPPED_TX_1	0x101
#define KS_MIB_PACKET_DROPPED_TX	0x102
#define KS_MIB_PACKET_DROPPED_RX_0	0x103
#define KS_MIB_PACKET_DROPPED_RX_1	0x104
#define KS_MIB_PACKET_DROPPED_RX	0x105

/* Change default LED mode. */
#define SET_DEFAULT_LED			LED_SPEED_DUPLEX_ACT

#define MAC_ADDR_LEN			6
#define MAC_ADDR_ORDER(i)		(MAC_ADDR_LEN - 1 - (i))

#define MAX_ETHERNET_BODY_SIZE		1500
#define ETHERNET_HEADER_SIZE		14

#define MAX_ETHERNET_PACKET_SIZE	\
	(MAX_ETHERNET_BODY_SIZE + ETHERNET_HEADER_SIZE)

#define REGULAR_RX_BUF_SIZE		(MAX_ETHERNET_PACKET_SIZE + 4)
#define MAX_RX_BUF_SIZE			(1912 + 4)

#define ADDITIONAL_ENTRIES		16
#define MAX_MULTICAST_LIST		32

#define HW_MULTICAST_SIZE		8

#define HW_TO_DEV_PORT(port)		(port - 1)

enum {
	media_connected,
	media_disconnected
};

enum {
	OID_COUNTER_UNKOWN,

	OID_COUNTER_FIRST,

	/* total transmit errors */
	OID_COUNTER_XMIT_ERROR,

	/* total receive errors */
	OID_COUNTER_RCV_ERROR,

	OID_COUNTER_LAST
};

/*
 * Hardware descriptor definitions
 */

#define DESC_ALIGNMENT			16
#define BUFFER_ALIGNMENT		8

#define NUM_OF_RX_DESC			64
#define NUM_OF_TX_DESC			64

#define KS_DESC_RX_FRAME_LEN		0x000007FF
#define KS_DESC_RX_FRAME_TYPE		0x00008000
#define KS_DESC_RX_ERROR_CRC		0x00010000
#define KS_DESC_RX_ERROR_RUNT		0x00020000
#define KS_DESC_RX_ERROR_TOO_LONG	0x00040000
#define KS_DESC_RX_ERROR_PHY		0x00080000
#define KS884X_DESC_RX_PORT_MASK	0x00300000
#define KS_DESC_RX_MULTICAST		0x01000000
#define KS_DESC_RX_ERROR		0x02000000
#define KS_DESC_RX_ERROR_CSUM_UDP	0x04000000
#define KS_DESC_RX_ERROR_CSUM_TCP	0x08000000
#define KS_DESC_RX_ERROR_CSUM_IP	0x10000000
#define KS_DESC_RX_LAST			0x20000000
#define KS_DESC_RX_FIRST		0x40000000
#define KS_DESC_RX_ERROR_COND		\
	(KS_DESC_RX_ERROR_CRC |		\
	KS_DESC_RX_ERROR_RUNT |		\
	KS_DESC_RX_ERROR_PHY |		\
	KS_DESC_RX_ERROR_TOO_LONG)

#define KS_DESC_HW_OWNED		0x80000000

#define KS_DESC_BUF_SIZE		0x000007FF
#define KS884X_DESC_TX_PORT_MASK	0x00300000
#define KS_DESC_END_OF_RING		0x02000000
#define KS_DESC_TX_CSUM_GEN_UDP		0x04000000
#define KS_DESC_TX_CSUM_GEN_TCP		0x08000000
#define KS_DESC_TX_CSUM_GEN_IP		0x10000000
#define KS_DESC_TX_LAST			0x20000000
#define KS_DESC_TX_FIRST		0x40000000
#define KS_DESC_TX_INTERRUPT		0x80000000

#define KS_DESC_PORT_SHIFT		20

#define KS_DESC_RX_MASK			(KS_DESC_BUF_SIZE)

#define KS_DESC_TX_MASK			\
	(KS_DESC_TX_INTERRUPT |		\
	KS_DESC_TX_FIRST |		\
	KS_DESC_TX_LAST |		\
	KS_DESC_TX_CSUM_GEN_IP |	\
	KS_DESC_TX_CSUM_GEN_TCP |	\
	KS_DESC_TX_CSUM_GEN_UDP |	\
	KS_DESC_BUF_SIZE)

struct ksz_desc_rx_stat {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 hw_owned:1;
	u32 first_desc:1;
	u32 last_desc:1;
	u32 csum_err_ip:1;
	u32 csum_err_tcp:1;
	u32 csum_err_udp:1;
	u32 error:1;
	u32 multicast:1;
	u32 src_port:4;
	u32 err_phy:1;
	u32 err_too_long:1;
	u32 err_runt:1;
	u32 err_crc:1;
	u32 frame_type:1;
	u32 reserved1:4;
	u32 frame_len:11;
#else
	u32 frame_len:11;
	u32 reserved1:4;
	u32 frame_type:1;
	u32 err_crc:1;
	u32 err_runt:1;
	u32 err_too_long:1;
	u32 err_phy:1;
	u32 src_port:4;
	u32 multicast:1;
	u32 error:1;
	u32 csum_err_udp:1;
	u32 csum_err_tcp:1;
	u32 csum_err_ip:1;
	u32 last_desc:1;
	u32 first_desc:1;
	u32 hw_owned:1;
#endif
};

struct ksz_desc_tx_stat {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 hw_owned:1;
	u32 reserved1:31;
#else
	u32 reserved1:31;
	u32 hw_owned:1;
#endif
};

struct ksz_desc_rx_buf {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 reserved4:6;
	u32 end_of_ring:1;
	u32 reserved3:14;
	u32 buf_size:11;
#else
	u32 buf_size:11;
	u32 reserved3:14;
	u32 end_of_ring:1;
	u32 reserved4:6;
#endif
};

struct ksz_desc_tx_buf {
#ifdef __BIG_ENDIAN_BITFIELD
	u32 intr:1;
	u32 first_seg:1;
	u32 last_seg:1;
	u32 csum_gen_ip:1;
	u32 csum_gen_tcp:1;
	u32 csum_gen_udp:1;
	u32 end_of_ring:1;
	u32 reserved4:1;
	u32 dest_port:4;
	u32 reserved3:9;
	u32 buf_size:11;
#else
	u32 buf_size:11;
	u32 reserved3:9;
	u32 dest_port:4;
	u32 reserved4:1;
	u32 end_of_ring:1;
	u32 csum_gen_udp:1;
	u32 csum_gen_tcp:1;
	u32 csum_gen_ip:1;
	u32 last_seg:1;
	u32 first_seg:1;
	u32 intr:1;
#endif
};

union desc_stat {
	struct ksz_desc_rx_stat rx;
	struct ksz_desc_tx_stat tx;
	u32 data;
};

union desc_buf {
	struct ksz_desc_rx_buf rx;
	struct ksz_desc_tx_buf tx;
	u32 data;
};

/**
 * struct ksz_hw_desc - Hardware descriptor data structure
 * @ctrl:	Descriptor control value.
 * @buf:	Descriptor buffer value.
 * @addr:	Physical address of memory buffer.
 * @next:	Pointer to next hardware descriptor.
 */
struct ksz_hw_desc {
	union desc_stat ctrl;
	union desc_buf buf;
	u32 addr;
	u32 next;
};

/**
 * struct ksz_sw_desc - Software descriptor data structure
 * @ctrl:	Descriptor control value.
 * @buf:	Descriptor buffer value.
 * @buf_size:	Current buffers size value in hardware descriptor.
 */
struct ksz_sw_desc {
	union desc_stat ctrl;
	union desc_buf buf;
	u32 buf_size;
};

/**
 * struct ksz_dma_buf - OS dependent DMA buffer data structure
 * @skb:	Associated socket buffer.
 * @dma:	Associated physical DMA address.
 * len:		Actual len used.
 */
struct ksz_dma_buf {
	struct sk_buff *skb;
	dma_addr_t dma;
	int len;
};

/**
 * struct ksz_desc - Descriptor structure
 * @phw:	Hardware descriptor pointer to uncached physical memory.
 * @sw:		Cached memory to hold hardware descriptor values for
 * 		manipulation.
 * @dma_buf:	Operating system dependent data structure to hold physical
 * 		memory buffer allocation information.
 */
struct ksz_desc {
	struct ksz_hw_desc *phw;
	struct ksz_sw_desc sw;
	struct ksz_dma_buf dma_buf;
};

#define DMA_BUFFER(desc)  ((struct ksz_dma_buf *)(&(desc)->dma_buf))

/**
 * struct ksz_desc_info - Descriptor information data structure
 * @ring:	First descriptor in the ring.
 * @cur:	Current descriptor being manipulated.
 * @ring_virt:	First hardware descriptor in the ring.
 * @ring_phys:	The physical address of the first descriptor of the ring.
 * @size:	Size of hardware descriptor.
 * @alloc:	Number of descriptors allocated.
 * @avail:	Number of descriptors available for use.
 * @last:	Index for last descriptor released to hardware.
 * @next:	Index for next descriptor available for use.
 * @mask:	Mask for index wrapping.
 */
struct ksz_desc_info {
	struct ksz_desc *ring;
	struct ksz_desc *cur;
	struct ksz_hw_desc *ring_virt;
	u32 ring_phys;
	int size;
	int alloc;
	int avail;
	int last;
	int next;
	int mask;
};

/*
 * KSZ8842 switch definitions
 */

enum {
	TABLE_STATIC_MAC = 0,
	TABLE_VLAN,
	TABLE_DYNAMIC_MAC,
	TABLE_MIB
};

#define LEARNED_MAC_TABLE_ENTRIES	1024
#define STATIC_MAC_TABLE_ENTRIES	8

/**
 * struct ksz_mac_table - Static MAC table data structure
 * @mac_addr:	MAC address to filter.
 * @vid:	VID value.
 * @fid:	FID value.
 * @ports:	Port membership.
 * @override:	Override setting.
 * @use_fid:	FID use setting.
 * @valid:	Valid setting indicating the entry is being used.
 */
struct ksz_mac_table {
	u8 mac_addr[MAC_ADDR_LEN];
	u16 vid;
	u8 fid;
	u8 ports;
	u8 override:1;
	u8 use_fid:1;
	u8 valid:1;
};

#define VLAN_TABLE_ENTRIES		16

/**
 * struct ksz_vlan_table - VLAN table data structure
 * @vid:	VID value.
 * @fid:	FID value.
 * @member:	Port membership.
 */
struct ksz_vlan_table {
	u16 vid;
	u8 fid;
	u8 member;
};

#define DIFFSERV_ENTRIES		64
#define PRIO_802_1P_ENTRIES		8
#define PRIO_QUEUES			4

#define SWITCH_PORT_NUM			2
#define TOTAL_PORT_NUM			(SWITCH_PORT_NUM + 1)
#define HOST_MASK			(1 << SWITCH_PORT_NUM)
#define PORT_MASK			7

#define MAIN_PORT			0
#define OTHER_PORT			1
#define HOST_PORT			SWITCH_PORT_NUM

#define PORT_COUNTER_NUM		0x20
#define TOTAL_PORT_COUNTER_NUM		(PORT_COUNTER_NUM + 2)

#define MIB_COUNTER_RX_LO_PRIORITY	0x00
#define MIB_COUNTER_RX_HI_PRIORITY	0x01
#define MIB_COUNTER_RX_UNDERSIZE	0x02
#define MIB_COUNTER_RX_FRAGMENT		0x03
#define MIB_COUNTER_RX_OVERSIZE		0x04
#define MIB_COUNTER_RX_JABBER		0x05
#define MIB_COUNTER_RX_SYMBOL_ERR	0x06
#define MIB_COUNTER_RX_CRC_ERR		0x07
#define MIB_COUNTER_RX_ALIGNMENT_ERR	0x08
#define MIB_COUNTER_RX_CTRL_8808	0x09
#define MIB_COUNTER_RX_PAUSE		0x0A
#define MIB_COUNTER_RX_BROADCAST	0x0B
#define MIB_COUNTER_RX_MULTICAST	0x0C
#define MIB_COUNTER_RX_UNICAST		0x0D
#define MIB_COUNTER_RX_OCTET_64		0x0E
#define MIB_COUNTER_RX_OCTET_65_127	0x0F
#define MIB_COUNTER_RX_OCTET_128_255	0x10
#define MIB_COUNTER_RX_OCTET_256_511	0x11
#define MIB_COUNTER_RX_OCTET_512_1023	0x12
#define MIB_COUNTER_RX_OCTET_1024_1522	0x13
#define MIB_COUNTER_TX_LO_PRIORITY	0x14
#define MIB_COUNTER_TX_HI_PRIORITY	0x15
#define MIB_COUNTER_TX_LATE_COLLISION	0x16
#define MIB_COUNTER_TX_PAUSE		0x17
#define MIB_COUNTER_TX_BROADCAST	0x18
#define MIB_COUNTER_TX_MULTICAST	0x19
#define MIB_COUNTER_TX_UNICAST		0x1A
#define MIB_COUNTER_TX_DEFERRED		0x1B
#define MIB_COUNTER_TX_TOTAL_COLLISION	0x1C
#define MIB_COUNTER_TX_EXCESS_COLLISION	0x1D
#define MIB_COUNTER_TX_SINGLE_COLLISION	0x1E
#define MIB_COUNTER_TX_MULTI_COLLISION	0x1F

#define MIB_COUNTER_RX_DROPPED_PACKET	0x20
#define MIB_COUNTER_TX_DROPPED_PACKET	0x21

/**
 * struct ksz_port_mib - Port MIB data structure
 * @cnt_ptr:	Current pointer to MIB counter index.
 * @link_down:	Indication the link has just gone down.
 * @state:	Connection status of the port.
 * @mib_start:	The starting counter index.  Some ports do not start at 0.
 * @counter:	64-bit MIB counter value.
 * @dropped:	Temporary buffer to remember last read packet dropped values.
 *
 * MIB counters needs to be read periodically so that counters do not get
 * overflowed and give incorrect values.  A right balance is needed to
 * satisfy this condition and not waste too much CPU time.
 *
 * It is pointless to read MIB counters when the port is disconnected.  The
 * @state provides the connection status so that MIB counters are read only
 * when the port is connected.  The @link_down indicates the port is just
 * disconnected so that all MIB counters are read one last time to update the
 * information.
 */
struct ksz_port_mib {
	u8 cnt_ptr;
	u8 link_down;
	u8 state;
	u8 mib_start;

	u64 counter[TOTAL_PORT_COUNTER_NUM];
	u32 dropped[2];
};

/**
 * struct ksz_port_cfg - Port configuration data structure
 * @vid:	VID value.
 * @member:	Port membership.
 * @port_prio:	Port priority.
 * @rx_rate:	Receive priority rate.
 * @tx_rate:	Transmit priority rate.
 * @stp_state:	Current Spanning Tree Protocol state.
 */
struct ksz_port_cfg {
	u16 vid;
	u8 member;
	u8 port_prio;
	u32 rx_rate[PRIO_QUEUES];
	u32 tx_rate[PRIO_QUEUES];
	int stp_state;
};

/**
 * struct ksz_switch - KSZ8842 switch data structure
 * @mac_table:	MAC table entries information.
 * @vlan_table:	VLAN table entries information.
 * @port_cfg:	Port configuration information.
 * @diffserv:	DiffServ priority settings.  Possible values from 6-bit of ToS
 * 		(bit7 ~ bit2) field.
 * @p_802_1p:	802.1P priority settings.  Possible values from 3-bit of 802.1p
 * 		Tag priority field.
 * @br_addr:	Bridge address.  Used for STP.
 * @other_addr:	Other MAC address.  Used for multiple network device mode.
 * @broad_per:	Broadcast storm percentage.
 * @member:	Current port membership.  Used for STP.
 */
struct ksz_switch {
	struct ksz_mac_table mac_table[STATIC_MAC_TABLE_ENTRIES];
	struct ksz_vlan_table vlan_table[VLAN_TABLE_ENTRIES];
	struct ksz_port_cfg port_cfg[TOTAL_PORT_NUM];

	u8 diffserv[DIFFSERV_ENTRIES];
	u8 p_802_1p[PRIO_802_1P_ENTRIES];

	u8 br_addr[MAC_ADDR_LEN];
	u8 other_addr[MAC_ADDR_LEN];

	u8 broad_per;
	u8 member;
};

#define TX_RATE_UNIT			10000

/**
 * struct ksz_port_info - Port information data structure
 * @state:	Connection status of the port.
 * @tx_rate:	Transmit rate divided by 10000 to get Mbit.
 * @duplex:	Duplex mode.
 * @advertised:	Advertised auto-negotiation setting.  Used to determine link.
 * @partner:	Auto-negotiation partner setting.  Used to determine link.
 * @port_id:	Port index to access actual hardware register.
 * @pdev:	Pointer to OS dependent network device.
 */
struct ksz_port_info {
	uint state;
	uint tx_rate;
	u8 duplex;
	u8 advertised;
	u8 partner;
	u8 port_id;
	void *pdev;
};

#define MAX_TX_HELD_SIZE		52000

/* Hardware features and bug fixes. */
#define LINK_INT_WORKING		(1 << 0)
#define SMALL_PACKET_TX_BUG		(1 << 1)
#define HALF_DUPLEX_SIGNAL_BUG		(1 << 2)
#define IPV6_CSUM_GEN_HACK		(1 << 3)
#define RX_HUGE_FRAME			(1 << 4)
#define STP_SUPPORT			(1 << 8)

/* Software overrides. */
#define PAUSE_FLOW_CTRL			(1 << 0)
#define FAST_AGING			(1 << 1)

/**
 * struct ksz_hw - KSZ884X hardware data structure
 * @io:			Virtual address assigned.
 * @ksz_switch:		Pointer to KSZ8842 switch.
 * @port_info:		Port information.
 * @port_mib:		Port MIB information.
 * @dev_count:		Number of network devices this hardware supports.
 * @dst_ports:		Destination ports in switch for transmission.
 * @id:			Hardware ID.  Used for display only.
 * @mib_cnt:		Number of MIB counters this hardware has.
 * @mib_port_cnt:	Number of ports with MIB counters.
 * @tx_cfg:		Cached transmit control settings.
 * @rx_cfg:		Cached receive control settings.
 * @intr_mask:		Current interrupt mask.
 * @intr_set:		Current interrup set.
 * @intr_blocked:	Interrupt blocked.
 * @rx_desc_info:	Receive descriptor information.
 * @tx_desc_info:	Transmit descriptor information.
 * @tx_int_cnt:		Transmit interrupt count.  Used for TX optimization.
 * @tx_int_mask:	Transmit interrupt mask.  Used for TX optimization.
 * @tx_size:		Transmit data size.  Used for TX optimization.
 * 			The maximum is defined by MAX_TX_HELD_SIZE.
 * @perm_addr:		Permanent MAC address.
 * @override_addr:	Overrided MAC address.
 * @address:		Additional MAC address entries.
 * @addr_list_size:	Additional MAC address list size.
 * @mac_override:	Indication of MAC address overrided.
 * @promiscuous:	Counter to keep track of promiscuous mode set.
 * @all_multi:		Counter to keep track of all multicast mode set.
 * @multi_list:		Multicast address entries.
 * @multi_bits:		Cached multicast hash table settings.
 * @multi_list_size:	Multicast address list size.
 * @enabled:		Indication of hardware enabled.
 * @rx_stop:		Indication of receive process stop.
 * @features:		Hardware features to enable.
 * @overrides:		Hardware features to override.
 * @parent:		Pointer to parent, network device private structure.
 */
struct ksz_hw {
	void __iomem *io;

	struct ksz_switch *ksz_switch;
	struct ksz_port_info port_info[SWITCH_PORT_NUM];
	struct ksz_port_mib port_mib[TOTAL_PORT_NUM];
	int dev_count;
	int dst_ports;
	int id;
	int mib_cnt;
	int mib_port_cnt;

	u32 tx_cfg;
	u32 rx_cfg;
	u32 intr_mask;
	u32 intr_set;
	uint intr_blocked;

	struct ksz_desc_info rx_desc_info;
	struct ksz_desc_info tx_desc_info;

	int tx_int_cnt;
	int tx_int_mask;
	int tx_size;

	u8 perm_addr[MAC_ADDR_LEN];
	u8 override_addr[MAC_ADDR_LEN];
	u8 address[ADDITIONAL_ENTRIES][MAC_ADDR_LEN];
	u8 addr_list_size;
	u8 mac_override;
	u8 promiscuous;
	u8 all_multi;
	u8 multi_list[MAX_MULTICAST_LIST][MAC_ADDR_LEN];
	u8 multi_bits[HW_MULTICAST_SIZE];
	u8 multi_list_size;

	u8 enabled;
	u8 rx_stop;
	u8 reserved2[1];

	uint features;
	uint overrides;

	void *parent;
};

enum {
	PHY_NO_FLOW_CTRL,
	PHY_FLOW_CTRL,
	PHY_TX_ONLY,
	PHY_RX_ONLY
};

/**
 * struct ksz_port - Virtual port data structure
 * @duplex:		Duplex mode setting.  1 for half duplex, 2 for full
 * 			duplex, and 0 for auto, which normally results in full
 * 			duplex.
 * @speed:		Speed setting.  10 for 10 Mbit, 100 for 100 Mbit, and
 * 			0 for auto, which normally results in 100 Mbit.
 * @force_link:		Force link setting.  0 for auto-negotiation, and 1 for
 * 			force.
 * @flow_ctrl:		Flow control setting.  PHY_NO_FLOW_CTRL for no flow
 * 			control, and PHY_FLOW_CTRL for flow control.
 * 			PHY_TX_ONLY and PHY_RX_ONLY are not supported for 100
 * 			Mbit PHY.
 * @first_port:		Index of first port this port supports.
 * @mib_port_cnt:	Number of ports with MIB counters.
 * @port_cnt:		Number of ports this port supports.
 * @counter:		Port statistics counter.
 * @hw:			Pointer to hardware structure.
 * @linked:		Pointer to port information linked to this port.
 */
struct ksz_port {
	u8 duplex;
	u8 speed;
	u8 force_link;
	u8 flow_ctrl;

	int first_port;
	int mib_port_cnt;
	int port_cnt;
	u64 counter[OID_COUNTER_LAST];

	struct ksz_hw *hw;
	struct ksz_port_info *linked;
};

/**
 * struct ksz_timer_info - Timer information data structure
 * @timer:	Kernel timer.
 * @cnt:	Running timer counter.
 * @max:	Number of times to run timer; -1 for infinity.
 * @period:	Timer period in jiffies.
 */
struct ksz_timer_info {
	struct timer_list timer;
	int cnt;
	int max;
	int period;
};

/**
 * struct ksz_shared_mem - OS dependent shared memory data structure
 * @dma_addr:	Physical DMA address allocated.
 * @alloc_size:	Allocation size.
 * @phys:	Actual physical address used.
 * @alloc_virt:	Virtual address allocated.
 * @virt:	Actual virtual address used.
 */
struct ksz_shared_mem {
	dma_addr_t dma_addr;
	uint alloc_size;
	uint phys;
	u8 *alloc_virt;
	u8 *virt;
};

/**
 * struct ksz_counter_info - OS dependent counter information data structure
 * @counter:	Wait queue to wakeup after counters are read.
 * @time:	Next time in jiffies to read counter.
 * @read:	Indication of counters read in full or not.
 */
struct ksz_counter_info {
	wait_queue_head_t counter;
	unsigned long time;
	int read;
};

/**
 * struct dev_info - Network device information data structure
 * @dev:		Pointer to network device.
 * @pdev:		Pointer to PCI device.
 * @hw:			Hardware structure.
 * @desc_pool:		Physical memory used for descriptor pool.
 * @hwlock:		Spinlock to prevent hardware from accessing.
 * @lock:		Mutex lock to prevent device from accessing.
 * @dev_rcv:		Receive process function used.
 * @last_skb:		Socket buffer allocated for descriptor rx fragments.
 * @skb_index:		Buffer index for receiving fragments.
 * @skb_len:		Buffer length for receiving fragments.
 * @mib_read:		Workqueue to read MIB counters.
 * @mib_timer_info:	Timer to read MIB counters.
 * @counter:		Used for MIB reading.
 * @mtu:		Current MTU used.  The default is REGULAR_RX_BUF_SIZE;
 * 			the maximum is MAX_RX_BUF_SIZE.
 * @opened:		Counter to keep track of device open.
 * @rx_tasklet:		Receive processing tasklet.
 * @tx_tasklet:		Transmit processing tasklet.
 * @wol_enable:		Wake-on-LAN enable set by ethtool.
 * @wol_support:	Wake-on-LAN support used by ethtool.
 * @pme_wait:		Used for KSZ8841 power management.
 */
struct dev_info {
	struct net_device *dev;
	struct pci_dev *pdev;

	struct ksz_hw hw;
	struct ksz_shared_mem desc_pool;

	spinlock_t hwlock;
	struct mutex lock;

	int (*dev_rcv)(struct dev_info *);

	struct sk_buff *last_skb;
	int skb_index;
	int skb_len;

	struct work_struct mib_read;
	struct ksz_timer_info mib_timer_info;
	struct ksz_counter_info counter[TOTAL_PORT_NUM];

	int mtu;
	int opened;

	struct tasklet_struct rx_tasklet;
	struct tasklet_struct tx_tasklet;

	int wol_enable;
	int wol_support;
	unsigned long pme_wait;
};

/**
 * struct dev_priv - Network device private data structure
 * @adapter:		Adapter device information.
 * @port:		Port information.
 * @monitor_time_info:	Timer to monitor ports.
 * @proc_sem:		Semaphore for proc accessing.
 * @id:			Device ID.
 * @mii_if:		MII interface information.
 * @advertising:	Temporary variable to store advertised settings.
 * @msg_enable:		The message flags controlling driver output.
 * @media_state:	The connection status of the device.
 * @multicast:		The all multicast state of the device.
 * @promiscuous:	The promiscuous state of the device.
 */
struct dev_priv {
	struct dev_info *adapter;
	struct ksz_port port;
	struct ksz_timer_info monitor_timer_info;

	struct semaphore proc_sem;
	int id;

	struct mii_if_info mii_if;
	u32 advertising;

	u32 msg_enable;
	int media_state;
	int multicast;
	int promiscuous;
};

#define DRV_NAME		"KSZ884X PCI"
#define DEVICE_NAME		"KSZ884x PCI"
#define DRV_VERSION		"1.0.0"
#define DRV_RELDATE		"Feb 8, 2010"

static char version[] __devinitdata =
	"Micrel " DEVICE_NAME " " DRV_VERSION " (" DRV_RELDATE ")";

static u8 DEFAULT_MAC_ADDRESS[] = { 0x00, 0x10, 0xA1, 0x88, 0x42, 0x01 };

/*
 * Interrupt processing primary routines
 */

static inline void hw_ack_intr(struct ksz_hw *hw, uint interrupt)
{
	writel(interrupt, hw->io + KS884X_INTERRUPTS_STATUS);
}

static inline void hw_dis_intr(struct ksz_hw *hw)
{
	hw->intr_blocked = hw->intr_mask;
	writel(0, hw->io + KS884X_INTERRUPTS_ENABLE);
	hw->intr_set = readl(hw->io + KS884X_INTERRUPTS_ENABLE);
}

static inline void hw_set_intr(struct ksz_hw *hw, uint interrupt)
{
	hw->intr_set = interrupt;
	writel(interrupt, hw->io + KS884X_INTERRUPTS_ENABLE);
}

static inline void hw_ena_intr(struct ksz_hw *hw)
{
	hw->intr_blocked = 0;
	hw_set_intr(hw, hw->intr_mask);
}

static inline void hw_dis_intr_bit(struct ksz_hw *hw, uint bit)
{
	hw->intr_mask &= ~(bit);
}

static inline void hw_turn_off_intr(struct ksz_hw *hw, uint interrupt)
{
	u32 read_intr;

	read_intr = readl(hw->io + KS884X_INTERRUPTS_ENABLE);
	hw->intr_set = read_intr & ~interrupt;
	writel(hw->intr_set, hw->io + KS884X_INTERRUPTS_ENABLE);
	hw_dis_intr_bit(hw, interrupt);
}

/**
 * hw_turn_on_intr - turn on specified interrupts
 * @hw: 	The hardware instance.
 * @bit:	The interrupt bits to be on.
 *
 * This routine turns on the specified interrupts in the interrupt mask so that
 * those interrupts will be enabled.
 */
static void hw_turn_on_intr(struct ksz_hw *hw, u32 bit)
{
	hw->intr_mask |= bit;

	if (!hw->intr_blocked)
		hw_set_intr(hw, hw->intr_mask);
}

static inline void hw_ena_intr_bit(struct ksz_hw *hw, uint interrupt)
{
	u32 read_intr;

	read_intr = readl(hw->io + KS884X_INTERRUPTS_ENABLE);
	hw->intr_set = read_intr | interrupt;
	writel(hw->intr_set, hw->io + KS884X_INTERRUPTS_ENABLE);
}

static inline void hw_read_intr(struct ksz_hw *hw, uint *status)
{
	*status = readl(hw->io + KS884X_INTERRUPTS_STATUS);
	*status = *status & hw->intr_set;
}

static inline void hw_restore_intr(struct ksz_hw *hw, uint interrupt)
{
	if (interrupt)
		hw_ena_intr(hw);
}

/**
 * hw_block_intr - block hardware interrupts
 *
 * This function blocks all interrupts of the hardware and returns the current
 * interrupt enable mask so that interrupts can be restored later.
 *
 * Return the current interrupt enable mask.
 */
static uint hw_block_intr(struct ksz_hw *hw)
{
	uint interrupt = 0;

	if (!hw->intr_blocked) {
		hw_dis_intr(hw);
		interrupt = hw->intr_blocked;
	}
	return interrupt;
}

/*
 * Hardware descriptor routines
 */

static inline void reset_desc(struct ksz_desc *desc, union desc_stat status)
{
	status.rx.hw_owned = 0;
	desc->phw->ctrl.data = cpu_to_le32(status.data);
}

static inline void release_desc(struct ksz_desc *desc)
{
	desc->sw.ctrl.tx.hw_owned = 1;
	if (desc->sw.buf_size != desc->sw.buf.data) {
		desc->sw.buf_size = desc->sw.buf.data;
		desc->phw->buf.data = cpu_to_le32(desc->sw.buf.data);
	}
	desc->phw->ctrl.data = cpu_to_le32(desc->sw.ctrl.data);
}

static void get_rx_pkt(struct ksz_desc_info *info, struct ksz_desc **desc)
{
	*desc = &info->ring[info->last];
	info->last++;
	info->last &= info->mask;
	info->avail--;
	(*desc)->sw.buf.data &= ~KS_DESC_RX_MASK;
}

static inline void set_rx_buf(struct ksz_desc *desc, u32 addr)
{
	desc->phw->addr = cpu_to_le32(addr);
}

static inline void set_rx_len(struct ksz_desc *desc, u32 len)
{
	desc->sw.buf.rx.buf_size = len;
}

static inline void get_tx_pkt(struct ksz_desc_info *info,
	struct ksz_desc **desc)
{
	*desc = &info->ring[info->next];
	info->next++;
	info->next &= info->mask;
	info->avail--;
	(*desc)->sw.buf.data &= ~KS_DESC_TX_MASK;
}

static inline void set_tx_buf(struct ksz_desc *desc, u32 addr)
{
	desc->phw->addr = cpu_to_le32(addr);
}

static inline void set_tx_len(struct ksz_desc *desc, u32 len)
{
	desc->sw.buf.tx.buf_size = len;
}

/* Switch functions */

#define TABLE_READ			0x10
#define TABLE_SEL_SHIFT			2

#define HW_DELAY(hw, reg)			\
	do {					\
		u16 dummy;			\
		dummy = readw(hw->io + reg);	\
	} while (0)

/**
 * sw_r_table - read 4 bytes of data from switch table
 * @hw:		The hardware instance.
 * @table:	The table selector.
 * @addr:	The address of the table entry.
 * @data:	Buffer to store the read data.
 *
 * This routine reads 4 bytes of data from the table of the switch.
 * Hardware interrupts are disabled to minimize corruption of read data.
 */
static void sw_r_table(struct ksz_hw *hw, int table, u16 addr, u32 *data)
{
	u16 ctrl_addr;
	uint interrupt;

	ctrl_addr = (((table << TABLE_SEL_SHIFT) | TABLE_READ) << 8) | addr;

	interrupt = hw_block_intr(hw);

	writew(ctrl_addr, hw->io + KS884X_IACR_OFFSET);
	HW_DELAY(hw, KS884X_IACR_OFFSET);
	*data = readl(hw->io + KS884X_ACC_DATA_0_OFFSET);

	hw_restore_intr(hw, interrupt);
}

/**
 * sw_w_table_64 - write 8 bytes of data to the switch table
 * @hw:		The hardware instance.
 * @table:	The table selector.
 * @addr:	The address of the table entry.
 * @data_hi:	The high part of data to be written (bit63 ~ bit32).
 * @data_lo:	The low part of data to be written (bit31 ~ bit0).
 *
 * This routine writes 8 bytes of data to the table of the switch.
 * Hardware interrupts are disabled to minimize corruption of written data.
 */
static void sw_w_table_64(struct ksz_hw *hw, int table, u16 addr, u32 data_hi,
	u32 data_lo)
{
	u16 ctrl_addr;
	uint interrupt;

	ctrl_addr = ((table << TABLE_SEL_SHIFT) << 8) | addr;

	interrupt = hw_block_intr(hw);

	writel(data_hi, hw->io + KS884X_ACC_DATA_4_OFFSET);
	writel(data_lo, hw->io + KS884X_ACC_DATA_0_OFFSET);

	writew(ctrl_addr, hw->io + KS884X_IACR_OFFSET);
	HW_DELAY(hw, KS884X_IACR_OFFSET);

	hw_restore_intr(hw, interrupt);
}

/**
 * sw_w_sta_mac_table - write to the static MAC table
 * @hw: 	The hardware instance.
 * @addr:	The address of the table entry.
 * @mac_addr:	The MAC address.
 * @ports:	The port members.
 * @override:	The flag to override the port receive/transmit settings.
 * @valid:	The flag to indicate entry is valid.
 * @use_fid:	The flag to indicate the FID is valid.
 * @fid:	The FID value.
 *
 * This routine writes an entry of the static MAC table of the switch.  It
 * calls sw_w_table_64() to write the data.
 */
static void sw_w_sta_mac_table(struct ksz_hw *hw, u16 addr, u8 *mac_addr,
	u8 ports, int override, int valid, int use_fid, u8 fid)
{
	u32 data_hi;
	u32 data_lo;

	data_lo = ((u32) mac_addr[2] << 24) |
		((u32) mac_addr[3] << 16) |
		((u32) mac_addr[4] << 8) | mac_addr[5];
	data_hi = ((u32) mac_addr[0] << 8) | mac_addr[1];
	data_hi |= (u32) ports << STATIC_MAC_FWD_PORTS_SHIFT;

	if (override)
		data_hi |= STATIC_MAC_TABLE_OVERRIDE;
	if (use_fid) {
		data_hi |= STATIC_MAC_TABLE_USE_FID;
		data_hi |= (u32) fid << STATIC_MAC_FID_SHIFT;
	}
	if (valid)
		data_hi |= STATIC_MAC_TABLE_VALID;

	sw_w_table_64(hw, TABLE_STATIC_MAC, addr, data_hi, data_lo);
}

/**
 * sw_r_vlan_table - read from the VLAN table
 * @hw: 	The hardware instance.
 * @addr:	The address of the table entry.
 * @vid:	Buffer to store the VID.
 * @fid:	Buffer to store the VID.
 * @member:	Buffer to store the port membership.
 *
 * This function reads an entry of the VLAN table of the switch.  It calls
 * sw_r_table() to get the data.
 *
 * Return 0 if the entry is valid; otherwise -1.
 */
static int sw_r_vlan_table(struct ksz_hw *hw, u16 addr, u16 *vid, u8 *fid,
	u8 *member)
{
	u32 data;

	sw_r_table(hw, TABLE_VLAN, addr, &data);
	if (data & VLAN_TABLE_VALID) {
		*vid = (u16)(data & VLAN_TABLE_VID);
		*fid = (u8)((data & VLAN_TABLE_FID) >> VLAN_TABLE_FID_SHIFT);
		*member = (u8)((data & VLAN_TABLE_MEMBERSHIP) >>
			VLAN_TABLE_MEMBERSHIP_SHIFT);
		return 0;
	}
	return -1;
}

/**
 * port_r_mib_cnt - read MIB counter
 * @hw: 	The hardware instance.
 * @port:	The port index.
 * @addr:	The address of the counter.
 * @cnt:	Buffer to store the counter.
 *
 * This routine reads a MIB counter of the port.
 * Hardware interrupts are disabled to minimize corruption of read data.
 */
static void port_r_mib_cnt(struct ksz_hw *hw, int port, u16 addr, u64 *cnt)
{
	u32 data;
	u16 ctrl_addr;
	uint interrupt;
	int timeout;

	ctrl_addr = addr + PORT_COUNTER_NUM * port;

	interrupt = hw_block_intr(hw);

	ctrl_addr |= (((TABLE_MIB << TABLE_SEL_SHIFT) | TABLE_READ) << 8);
	writew(ctrl_addr, hw->io + KS884X_IACR_OFFSET);
	HW_DELAY(hw, KS884X_IACR_OFFSET);

	for (timeout = 100; timeout > 0; timeout--) {
		data = readl(hw->io + KS884X_ACC_DATA_0_OFFSET);

		if (data & MIB_COUNTER_VALID) {
			if (data & MIB_COUNTER_OVERFLOW)
				*cnt += MIB_COUNTER_VALUE + 1;
			*cnt += data & MIB_COUNTER_VALUE;
			break;
		}
	}

	hw_restore_intr(hw, interrupt);
}

/**
 * port_r_mib_pkt - read dropped packet counts
 * @hw: 	The hardware instance.
 * @port:	The port index.
 * @cnt:	Buffer to store the receive and transmit dropped packet counts.
 *
 * This routine reads the dropped packet counts of the port.
 * Hardware interrupts are disabled to minimize corruption of read data.
 */
static void port_r_mib_pkt(struct ksz_hw *hw, int port, u32 *last, u64 *cnt)
{
	u32 cur;
	u32 data;
	u16 ctrl_addr;
	uint interrupt;
	int index;

	index = KS_MIB_PACKET_DROPPED_RX_0 + port;
	do {
		interrupt = hw_block_intr(hw);

		ctrl_addr = (u16) index;
		ctrl_addr |= (((TABLE_MIB << TABLE_SEL_SHIFT) | TABLE_READ)
			<< 8);
		writew(ctrl_addr, hw->io + KS884X_IACR_OFFSET);
		HW_DELAY(hw, KS884X_IACR_OFFSET);
		data = readl(hw->io + KS884X_ACC_DATA_0_OFFSET);

		hw_restore_intr(hw, interrupt);

		data &= MIB_PACKET_DROPPED;
		cur = *last;
		if (data != cur) {
			*last = data;
			if (data < cur)
				data += MIB_PACKET_DROPPED + 1;
			data -= cur;
			*cnt += data;
		}
		++last;
		++cnt;
		index -= KS_MIB_PACKET_DROPPED_TX -
			KS_MIB_PACKET_DROPPED_TX_0 + 1;
	} while (index >= KS_MIB_PACKET_DROPPED_TX_0 + port);
}

/**
 * port_r_cnt - read MIB counters periodically
 * @hw: 	The hardware instance.
 * @port:	The port index.
 *
 * This routine is used to read the counters of the port periodically to avoid
 * counter overflow.  The hardware should be acquired first before calling this
 * routine.
 *
 * Return non-zero when not all counters not read.
 */
static int port_r_cnt(struct ksz_hw *hw, int port)
{
	struct ksz_port_mib *mib = &hw->port_mib[port];

	if (mib->mib_start < PORT_COUNTER_NUM)
		while (mib->cnt_ptr < PORT_COUNTER_NUM) {
			port_r_mib_cnt(hw, port, mib->cnt_ptr,
				&mib->counter[mib->cnt_ptr]);
			++mib->cnt_ptr;
		}
	if (hw->mib_cnt > PORT_COUNTER_NUM)
		port_r_mib_pkt(hw, port, mib->dropped,
			&mib->counter[PORT_COUNTER_NUM]);
	mib->cnt_ptr = 0;
	return 0;
}

/**
 * port_init_cnt - initialize MIB counter values
 * @hw: 	The hardware instance.
 * @port:	The port index.
 *
 * This routine is used to initialize all counters to zero if the hardware
 * cannot do it after reset.
 */
static void port_init_cnt(struct ksz_hw *hw, int port)
{
	struct ksz_port_mib *mib = &hw->port_mib[port];

	mib->cnt_ptr = 0;
	if (mib->mib_start < PORT_COUNTER_NUM)
		do {
			port_r_mib_cnt(hw, port, mib->cnt_ptr,
				&mib->counter[mib->cnt_ptr]);
			++mib->cnt_ptr;
		} while (mib->cnt_ptr < PORT_COUNTER_NUM);
	if (hw->mib_cnt > PORT_COUNTER_NUM)
		port_r_mib_pkt(hw, port, mib->dropped,
			&mib->counter[PORT_COUNTER_NUM]);
	memset((void *) mib->counter, 0, sizeof(u64) * TOTAL_PORT_COUNTER_NUM);
	mib->cnt_ptr = 0;
}

/*
 * Port functions
 */

/**
 * port_chk - check port register bits
 * @hw: 	The hardware instance.
 * @port:	The port index.
 * @offset:	The offset of the port register.
 * @bits:	The data bits to check.
 *
 * This function checks whether the specified bits of the port register are set
 * or not.
 *
 * Return 0 if the bits are not set.
 */
static int port_chk(struct ksz_hw *hw, int port, int offset, u16 bits)
{
	u32 addr;
	u16 data;

	PORT_CTRL_ADDR(port, addr);
	addr += offset;
	data = readw(hw->io + addr);
	return (data & bits) == bits;
}

/**
 * port_cfg - set port register bits
 * @hw: 	The hardware instance.
 * @port:	The port index.
 * @offset:	The offset of the port register.
 * @bits:	The data bits to set.
 * @set:	The flag indicating whether the bits are to be set or not.
 *
 * This routine sets or resets the specified bits of the port register.
 */
static void port_cfg(struct ksz_hw *hw, int port, int offset, u16 bits,
	int set)
{
	u32 addr;
	u16 data;

	PORT_CTRL_ADDR(port, addr);
	addr += offset;
	data = readw(hw->io + addr);
	if (set)
		data |= bits;
	else
		data &= ~bits;
	writew(data, hw->io + addr);
}

/**
 * port_chk_shift - check port bit
 * @hw: 	The hardware instance.
 * @port:	The port index.
 * @offset:	The offset of the register.
 * @shift:	Number of bits to shift.
 *
 * This function checks whether the specified port is set in the register or
 * not.
 *
 * Return 0 if the port is not set.
 */
static int port_chk_shift(struct ksz_hw *hw, int port, u32 addr, int shift)
{
	u16 data;
	u16 bit = 1 << port;

	data = readw(hw->io + addr);
	data >>= shift;
	return (data & bit) == bit;
}

/**
 * port_cfg_shift - set port bit
 * @hw: 	The hardware instance.
 * @port:	The port index.
 * @offset:	The offset of the register.
 * @shift:	Number of bits to shift.
 * @set:	The flag indicating whether the port is to be set or not.
 *
 * This routine sets or resets the specified port in the register.
 */
static void port_cfg_shift(struct ksz_hw *hw, int port, u32 addr, int shift,
	int set)
{
	u16 data;
	u16 bits = 1 << port;

	data = readw(hw->io + addr);
	bits <<= shift;
	if (set)
		data |= bits;
	else
		data &= ~bits;
	writew(data, hw->io + addr);
}

/**
 * port_r8 - read byte from port register
 * @hw: 	The hardware instance.
 * @port:	The port index.
 * @offset:	The offset of the port register.
 * @data:	Buffer to store the data.
 *
 * This routine reads a byte from the port register.
 */
static void port_r8(struct ksz_hw *hw, int port, int offset, u8 *data)
{
	u32 addr;

	PORT_CTRL_ADDR(port, addr);
	addr += offset;
	*data = readb(hw->io + addr);
}

/**
 * port_r16 - read word from port register.
 * @hw: 	The hardware instance.
 * @port:	The port index.
 * @offset:	The offset of the port register.
 * @data:	Buffer to store the data.
 *
 * This routine reads a word from the port register.
 */
static void port_r16(struct ksz_hw *hw, int port, int offset, u16 *data)
{
	u32 addr;

	PORT_CTRL_ADDR(port, addr);
	addr += offset;
	*data = readw(hw->io + addr);
}

/**
 * port_w16 - write word to port register.
 * @hw: 	The hardware instance.
 * @port:	The port index.
 * @offset:	The offset of the port register.
 * @data:	Data to write.
 *
 * This routine writes a word to the port register.
 */
static void port_w16(struct ksz_hw *hw, int port, int offset, u16 data)
{
	u32 addr;

	PORT_CTRL_ADDR(port, addr);
	addr += offset;
	writew(data, hw->io + addr);
}

/**
 * sw_chk - check switch register bits
 * @hw: 	The hardware instance.
 * @addr:	The address of the switch register.
 * @bits:	The data bits to check.
 *
 * This function checks whether the specified bits of the switch register are
 * set or not.
 *
 * Return 0 if the bits are not set.
 */
static int sw_chk(struct ksz_hw *hw, u32 addr, u16 bits)
{
	u16 data;

	data = readw(hw->io + addr);
	return (data & bits) == bits;
}

/**
 * sw_cfg - set switch register bits
 * @hw: 	The hardware instance.
 * @addr:	The address of the switch register.
 * @bits:	The data bits to set.
 * @set:	The flag indicating whether the bits are to be set or not.
 *
 * This function sets or resets the specified bits of the switch register.
 */
static void sw_cfg(struct ksz_hw *hw, u32 addr, u16 bits, int set)
{
	u16 data;

	data = readw(hw->io + addr);
	if (set)
		data |= bits;
	else
		data &= ~bits;
	writew(data, hw->io + addr);
}

/* Bandwidth */

static inline void port_cfg_broad_storm(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_1_OFFSET, PORT_BROADCAST_STORM, set);
}

static inline int port_chk_broad_storm(struct ksz_hw *hw, int p)
{
	return port_chk(hw, p,
		KS8842_PORT_CTRL_1_OFFSET, PORT_BROADCAST_STORM);
}

/* Driver set switch broadcast storm protection at 10% rate. */
#define BROADCAST_STORM_PROTECTION_RATE	10

/* 148,800 frames * 67 ms / 100 */
#define BROADCAST_STORM_VALUE		9969

/**
 * sw_cfg_broad_storm - configure broadcast storm threshold
 * @hw: 	The hardware instance.
 * @percent:	Broadcast storm threshold in percent of transmit rate.
 *
 * This routine configures the broadcast storm threshold of the switch.
 */
static void sw_cfg_broad_storm(struct ksz_hw *hw, u8 percent)
{
	u16 data;
	u32 value = ((u32) BROADCAST_STORM_VALUE * (u32) percent / 100);

	if (value > BROADCAST_STORM_RATE)
		value = BROADCAST_STORM_RATE;

	data = readw(hw->io + KS8842_SWITCH_CTRL_3_OFFSET);
	data &= ~(BROADCAST_STORM_RATE_LO | BROADCAST_STORM_RATE_HI);
	data |= ((value & 0x00FF) << 8) | ((value & 0xFF00) >> 8);
	writew(data, hw->io + KS8842_SWITCH_CTRL_3_OFFSET);
}

/**
 * sw_get_board_storm - get broadcast storm threshold
 * @hw: 	The hardware instance.
 * @percent:	Buffer to store the broadcast storm threshold percentage.
 *
 * This routine retrieves the broadcast storm threshold of the switch.
 */
static void sw_get_broad_storm(struct ksz_hw *hw, u8 *percent)
{
	int num;
	u16 data;

	data = readw(hw->io + KS8842_SWITCH_CTRL_3_OFFSET);
	num = (data & BROADCAST_STORM_RATE_HI);
	num <<= 8;
	num |= (data & BROADCAST_STORM_RATE_LO) >> 8;
	num = (num * 100 + BROADCAST_STORM_VALUE / 2) / BROADCAST_STORM_VALUE;
	*percent = (u8) num;
}

/**
 * sw_dis_broad_storm - disable broadstorm
 * @hw: 	The hardware instance.
 * @port:	The port index.
 *
 * This routine disables the broadcast storm limit function of the switch.
 */
static void sw_dis_broad_storm(struct ksz_hw *hw, int port)
{
	port_cfg_broad_storm(hw, port, 0);
}

/**
 * sw_ena_broad_storm - enable broadcast storm
 * @hw: 	The hardware instance.
 * @port:	The port index.
 *
 * This routine enables the broadcast storm limit function of the switch.
 */
static void sw_ena_broad_storm(struct ksz_hw *hw, int port)
{
	sw_cfg_broad_storm(hw, hw->ksz_switch->broad_per);
	port_cfg_broad_storm(hw, port, 1);
}

/**
 * sw_init_broad_storm - initialize broadcast storm
 * @hw: 	The hardware instance.
 *
 * This routine initializes the broadcast storm limit function of the switch.
 */
static void sw_init_broad_storm(struct ksz_hw *hw)
{
	int port;

	hw->ksz_switch->broad_per = 1;
	sw_cfg_broad_storm(hw, hw->ksz_switch->broad_per);
	for (port = 0; port < TOTAL_PORT_NUM; port++)
		sw_dis_broad_storm(hw, port);
	sw_cfg(hw, KS8842_SWITCH_CTRL_2_OFFSET, MULTICAST_STORM_DISABLE, 1);
}

/**
 * hw_cfg_broad_storm - configure broadcast storm
 * @hw: 	The hardware instance.
 * @percent:	Broadcast storm threshold in percent of transmit rate.
 *
 * This routine configures the broadcast storm threshold of the switch.
 * It is called by user functions.  The hardware should be acquired first.
 */
static void hw_cfg_broad_storm(struct ksz_hw *hw, u8 percent)
{
	if (percent > 100)
		percent = 100;

	sw_cfg_broad_storm(hw, percent);
	sw_get_broad_storm(hw, &percent);
	hw->ksz_switch->broad_per = percent;
}

/**
 * sw_dis_prio_rate - disable switch priority rate
 * @hw: 	The hardware instance.
 * @port:	The port index.
 *
 * This routine disables the priority rate function of the switch.
 */
static void sw_dis_prio_rate(struct ksz_hw *hw, int port)
{
	u32 addr;

	PORT_CTRL_ADDR(port, addr);
	addr += KS8842_PORT_IN_RATE_OFFSET;
	writel(0, hw->io + addr);
}

/**
 * sw_init_prio_rate - initialize switch prioirty rate
 * @hw: 	The hardware instance.
 *
 * This routine initializes the priority rate function of the switch.
 */
static void sw_init_prio_rate(struct ksz_hw *hw)
{
	int port;
	int prio;
	struct ksz_switch *sw = hw->ksz_switch;

	for (port = 0; port < TOTAL_PORT_NUM; port++) {
		for (prio = 0; prio < PRIO_QUEUES; prio++) {
			sw->port_cfg[port].rx_rate[prio] =
			sw->port_cfg[port].tx_rate[prio] = 0;
		}
		sw_dis_prio_rate(hw, port);
	}
}

/* Communication */

static inline void port_cfg_back_pressure(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_BACK_PRESSURE, set);
}

static inline void port_cfg_force_flow_ctrl(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_FORCE_FLOW_CTRL, set);
}

static inline int port_chk_back_pressure(struct ksz_hw *hw, int p)
{
	return port_chk(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_BACK_PRESSURE);
}

static inline int port_chk_force_flow_ctrl(struct ksz_hw *hw, int p)
{
	return port_chk(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_FORCE_FLOW_CTRL);
}

/* Spanning Tree */

static inline void port_cfg_dis_learn(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_LEARN_DISABLE, set);
}

static inline void port_cfg_rx(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_RX_ENABLE, set);
}

static inline void port_cfg_tx(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_TX_ENABLE, set);
}

static inline void sw_cfg_fast_aging(struct ksz_hw *hw, int set)
{
	sw_cfg(hw, KS8842_SWITCH_CTRL_1_OFFSET, SWITCH_FAST_AGING, set);
}

static inline void sw_flush_dyn_mac_table(struct ksz_hw *hw)
{
	if (!(hw->overrides & FAST_AGING)) {
		sw_cfg_fast_aging(hw, 1);
		mdelay(1);
		sw_cfg_fast_aging(hw, 0);
	}
}

/* VLAN */

static inline void port_cfg_ins_tag(struct ksz_hw *hw, int p, int insert)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_1_OFFSET, PORT_INSERT_TAG, insert);
}

static inline void port_cfg_rmv_tag(struct ksz_hw *hw, int p, int remove)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_1_OFFSET, PORT_REMOVE_TAG, remove);
}

static inline int port_chk_ins_tag(struct ksz_hw *hw, int p)
{
	return port_chk(hw, p,
		KS8842_PORT_CTRL_1_OFFSET, PORT_INSERT_TAG);
}

static inline int port_chk_rmv_tag(struct ksz_hw *hw, int p)
{
	return port_chk(hw, p,
		KS8842_PORT_CTRL_1_OFFSET, PORT_REMOVE_TAG);
}

static inline void port_cfg_dis_non_vid(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_DISCARD_NON_VID, set);
}

static inline void port_cfg_in_filter(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_INGRESS_VLAN_FILTER, set);
}

static inline int port_chk_dis_non_vid(struct ksz_hw *hw, int p)
{
	return port_chk(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_DISCARD_NON_VID);
}

static inline int port_chk_in_filter(struct ksz_hw *hw, int p)
{
	return port_chk(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_INGRESS_VLAN_FILTER);
}

/* Mirroring */

static inline void port_cfg_mirror_sniffer(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_MIRROR_SNIFFER, set);
}

static inline void port_cfg_mirror_rx(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_MIRROR_RX, set);
}

static inline void port_cfg_mirror_tx(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_MIRROR_TX, set);
}

static inline void sw_cfg_mirror_rx_tx(struct ksz_hw *hw, int set)
{
	sw_cfg(hw, KS8842_SWITCH_CTRL_2_OFFSET, SWITCH_MIRROR_RX_TX, set);
}

static void sw_init_mirror(struct ksz_hw *hw)
{
	int port;

	for (port = 0; port < TOTAL_PORT_NUM; port++) {
		port_cfg_mirror_sniffer(hw, port, 0);
		port_cfg_mirror_rx(hw, port, 0);
		port_cfg_mirror_tx(hw, port, 0);
	}
	sw_cfg_mirror_rx_tx(hw, 0);
}

static inline void sw_cfg_unk_def_deliver(struct ksz_hw *hw, int set)
{
	sw_cfg(hw, KS8842_SWITCH_CTRL_7_OFFSET,
		SWITCH_UNK_DEF_PORT_ENABLE, set);
}

static inline int sw_cfg_chk_unk_def_deliver(struct ksz_hw *hw)
{
	return sw_chk(hw, KS8842_SWITCH_CTRL_7_OFFSET,
		SWITCH_UNK_DEF_PORT_ENABLE);
}

static inline void sw_cfg_unk_def_port(struct ksz_hw *hw, int port, int set)
{
	port_cfg_shift(hw, port, KS8842_SWITCH_CTRL_7_OFFSET, 0, set);
}

static inline int sw_chk_unk_def_port(struct ksz_hw *hw, int port)
{
	return port_chk_shift(hw, port, KS8842_SWITCH_CTRL_7_OFFSET, 0);
}

/* Priority */

static inline void port_cfg_diffserv(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_1_OFFSET, PORT_DIFFSERV_ENABLE, set);
}

static inline void port_cfg_802_1p(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_1_OFFSET, PORT_802_1P_ENABLE, set);
}

static inline void port_cfg_replace_vid(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_USER_PRIORITY_CEILING, set);
}

static inline void port_cfg_prio(struct ksz_hw *hw, int p, int set)
{
	port_cfg(hw, p,
		KS8842_PORT_CTRL_1_OFFSET, PORT_PRIO_QUEUE_ENABLE, set);
}

static inline int port_chk_diffserv(struct ksz_hw *hw, int p)
{
	return port_chk(hw, p,
		KS8842_PORT_CTRL_1_OFFSET, PORT_DIFFSERV_ENABLE);
}

static inline int port_chk_802_1p(struct ksz_hw *hw, int p)
{
	return port_chk(hw, p,
		KS8842_PORT_CTRL_1_OFFSET, PORT_802_1P_ENABLE);
}

static inline int port_chk_replace_vid(struct ksz_hw *hw, int p)
{
	return port_chk(hw, p,
		KS8842_PORT_CTRL_2_OFFSET, PORT_USER_PRIORITY_CEILING);
}

static inline int port_chk_prio(struct ksz_hw *hw, int p)
{
	return port_chk(hw, p,
		KS8842_PORT_CTRL_1_OFFSET, PORT_PRIO_QUEUE_ENABLE);
}

/**
 * sw_dis_diffserv - disable switch DiffServ priority
 * @hw: 	The hardware instance.
 * @port:	The port index.
 *
 * This routine disables the DiffServ priority function of the switch.
 */
static void sw_dis_diffserv(struct ksz_hw *hw, int port)
{
	port_cfg_diffserv(hw, port, 0);
}

/**
 * sw_dis_802_1p - disable switch 802.1p priority
 * @hw: 	The hardware instance.
 * @port:	The port index.
 *
 * This routine disables the 802.1p priority function of the switch.
 */
static void sw_dis_802_1p(struct ksz_hw *hw, int port)
{
	port_cfg_802_1p(hw, port, 0);
}

/**
 * sw_cfg_replace_null_vid -
 * @hw: 	The hardware instance.
 * @set:	The flag to disable or enable.
 *
 */
static void sw_cfg_replace_null_vid(struct ksz_hw *hw, int set)
{
	sw_cfg(hw, KS8842_SWITCH_CTRL_3_OFFSET, SWITCH_REPLACE_NULL_VID, set);
}

/**
 * sw_cfg_replace_vid - enable switch 802.10 priority re-mapping
 * @hw: 	The hardware instance.
 * @port:	The port index.
 * @set:	The flag to disable or enable.
 *
 * This routine enables the 802.1p priority re-mapping function of the switch.
 * That allows 802.1p priority field to be replaced with the port's default
 * tag's priority value if the ingress packet's 802.1p priority has a higher
 * priority than port's default tag's priority.
 */
static void sw_cfg_replace_vid(struct ksz_hw *hw, int port, int set)
{
	port_cfg_replace_vid(hw, port, set);
}

/**
 * sw_cfg_port_based - configure switch port based priority
 * @hw: 	The hardware instance.
 * @port:	The port index.
 * @prio:	The priority to set.
 *
 * This routine configures the port based priority of the switch.
 */
static void sw_cfg_port_based(struct ksz_hw *hw, int port, u8 prio)
{
	u16 data;

	if (prio > PORT_BASED_PRIORITY_BASE)
		prio = PORT_BASED_PRIORITY_BASE;

	hw->ksz_switch->port_cfg[port].port_prio = prio;

	port_r16(hw, port, KS8842_PORT_CTRL_1_OFFSET, &data);
	data &= ~PORT_BASED_PRIORITY_MASK;
	data |= prio << PORT_BASED_PRIORITY_SHIFT;
	port_w16(hw, port, KS8842_PORT_CTRL_1_OFFSET, data);
}

/**
 * sw_dis_multi_queue - disable transmit multiple queues
 * @hw: 	The hardware instance.
 * @port:	The port index.
 *
 * This routine disables the transmit multiple queues selection of the switch
 * port.  Only single transmit queue on the port.
 */
static void sw_dis_multi_queue(struct ksz_hw *hw, int port)
{
	port_cfg_prio(hw, port, 0);
}

/**
 * sw_init_prio - initialize switch priority
 * @hw: 	The hardware instance.
 *
 * This routine initializes the switch QoS priority functions.
 */
static void sw_init_prio(struct ksz_hw *hw)
{
	int port;
	int tos;
	struct ksz_switch *sw = hw->ksz_switch;

	/*
	 * Init all the 802.1p tag priority value to be assigned to different
	 * priority queue.
	 */
	sw->p_802_1p[0] = 0;
	sw->p_802_1p[1] = 0;
	sw->p_802_1p[2] = 1;
	sw->p_802_1p[3] = 1;
	sw->p_802_1p[4] = 2;
	sw->p_802_1p[5] = 2;
	sw->p_802_1p[6] = 3;
	sw->p_802_1p[7] = 3;

	/*
	 * Init all the DiffServ priority value to be assigned to priority
	 * queue 0.
	 */
	for (tos = 0; tos < DIFFSERV_ENTRIES; tos++)
		sw->diffserv[tos] = 0;

	/* All QoS functions disabled. */
	for (port = 0; port < TOTAL_PORT_NUM; port++) {
		sw_dis_multi_queue(hw, port);
		sw_dis_diffserv(hw, port);
		sw_dis_802_1p(hw, port);
		sw_cfg_replace_vid(hw, port, 0);

		sw->port_cfg[port].port_prio = 0;
		sw_cfg_port_based(hw, port, sw->port_cfg[port].port_prio);
	}
	sw_cfg_replace_null_vid(hw, 0);
}

/**
 * port_get_def_vid - get port default VID.
 * @hw: 	The hardware instance.
 * @port:	The port index.
 * @vid:	Buffer to store the VID.
 *
 * This routine retrieves the default VID of the port.
 */
static void port_get_def_vid(struct ksz_hw *hw, int port, u16 *vid)
{
	u32 addr;

	PORT_CTRL_ADDR(port, addr);
	addr += KS8842_PORT_CTRL_VID_OFFSET;
	*vid = readw(hw->io + addr);
}

/**
 * sw_init_vlan - initialize switch VLAN
 * @hw: 	The hardware instance.
 *
 * This routine initializes the VLAN function of the switch.
 */
static void sw_init_vlan(struct ksz_hw *hw)
{
	int port;
	int entry;
	struct ksz_switch *sw = hw->ksz_switch;

	/* Read 16 VLAN entries from device's VLAN table. */
	for (entry = 0; entry < VLAN_TABLE_ENTRIES; entry++) {
		sw_r_vlan_table(hw, entry,
			&sw->vlan_table[entry].vid,
			&sw->vlan_table[entry].fid,
			&sw->vlan_table[entry].member);
	}

	for (port = 0; port < TOTAL_PORT_NUM; port++) {
		port_get_def_vid(hw, port, &sw->port_cfg[port].vid);
		sw->port_cfg[port].member = PORT_MASK;
	}
}

/**
 * sw_cfg_port_base_vlan - configure port-based VLAN membership
 * @hw: 	The hardware instance.
 * @port:	The port index.
 * @member:	The port-based VLAN membership.
 *
 * This routine configures the port-based VLAN membership of the port.
 */
static void sw_cfg_port_base_vlan(struct ksz_hw *hw, int port, u8 member)
{
	u32 addr;
	u8 data;

	PORT_CTRL_ADDR(port, addr);
	addr += KS8842_PORT_CTRL_2_OFFSET;

	data = readb(hw->io + addr);
	data &= ~PORT_VLAN_MEMBERSHIP;
	data |= (member & PORT_MASK);
	writeb(data, hw->io + addr);

	hw->ksz_switch->port_cfg[port].member = member;
}

/**
 * sw_get_addr - get the switch MAC address.
 * @hw: 	The hardware instance.
 * @mac_addr:	Buffer to store the MAC address.
 *
 * This function retrieves the MAC address of the switch.
 */
static inline void sw_get_addr(struct ksz_hw *hw, u8 *mac_addr)
{
	int i;

	for (i = 0; i < 6; i += 2) {
		mac_addr[i] = readb(hw->io + KS8842_MAC_ADDR_0_OFFSET + i);
		mac_addr[1 + i] = readb(hw->io + KS8842_MAC_ADDR_1_OFFSET + i);
	}
}

/**
 * sw_set_addr - configure switch MAC address
 * @hw: 	The hardware instance.
 * @mac_addr:	The MAC address.
 *
 * This function configures the MAC address of the switch.
 */
static void sw_set_addr(struct ksz_hw *hw, u8 *mac_addr)
{
	int i;

	for (i = 0; i < 6; i += 2) {
		writeb(mac_addr[i], hw->io + KS8842_MAC_ADDR_0_OFFSET + i);
		writeb(mac_addr[1 + i], hw->io + KS8842_MAC_ADDR_1_OFFSET + i);
	}
}

/**
 * sw_set_global_ctrl - set switch global control
 * @hw: 	The hardware instance.
 *
 * This routine sets the global control of the switch function.
 */
static void sw_set_global_ctrl(struct ksz_hw *hw)
{
	u16 data;

	/* Enable switch MII flow control. */
	data = readw(hw->io + KS8842_SWITCH_CTRL_3_OFFSET);
	data |= SWITCH_FLOW_CTRL;
	writew(data, hw->io + KS8842_SWITCH_CTRL_3_OFFSET);

	data = readw(hw->io + KS8842_SWITCH_CTRL_1_OFFSET);

	/* Enable aggressive back off algorithm in half duplex mode. */
	data |= SWITCH_AGGR_BACKOFF;

	/* Enable automatic fast aging when link changed detected. */
	data |= SWITCH_AGING_ENABLE;
	data |= SWITCH_LINK_AUTO_AGING;

	if (hw->overrides & FAST_AGING)
		data |= SWITCH_FAST_AGING;
	else
		data &= ~SWITCH_FAST_AGING;
	writew(data, hw->io + KS8842_SWITCH_CTRL_1_OFFSET);

	data = readw(hw->io + KS8842_SWITCH_CTRL_2_OFFSET);

	/* Enable no excessive collision drop. */
	data |= NO_EXC_COLLISION_DROP;
	writew(data, hw->io + KS8842_SWITCH_CTRL_2_OFFSET);
}

enum {
	STP_STATE_DISABLED = 0,
	STP_STATE_LISTENING,
	STP_STATE_LEARNING,
	STP_STATE_FORWARDING,
	STP_STATE_BLOCKED,
	STP_STATE_SIMPLE
};

/**
 * port_set_stp_state - configure port spanning tree state
 * @hw: 	The hardware instance.
 * @port:	The port index.
 * @state:	The spanning tree state.
 *
 * This routine configures the spanning tree state of the port.
 */
static void port_set_stp_state(struct ksz_hw *hw, int port, int state)
{
	u16 data;

	port_r16(hw, port, KS8842_PORT_CTRL_2_OFFSET, &data);
	switch (state) {
	case STP_STATE_DISABLED:
		data &= ~(PORT_TX_ENABLE | PORT_RX_ENABLE);
		data |= PORT_LEARN_DISABLE;
		break;
	case STP_STATE_LISTENING:
/*
 * No need to turn on transmit because of port direct mode.
 * Turning on receive is required if static MAC table is not setup.
 */
		data &= ~PORT_TX_ENABLE;
		data |= PORT_RX_ENABLE;
		data |= PORT_LEARN_DISABLE;
		break;
	case STP_STATE_LEARNING:
		data &= ~PORT_TX_ENABLE;
		data |= PORT_RX_ENABLE;
		data &= ~PORT_LEARN_DISABLE;
		break;
	case STP_STATE_FORWARDING:
		data |= (PORT_TX_ENABLE | PORT_RX_ENABLE);
		data &= ~PORT_LEARN_DISABLE;
		break;
	case STP_STATE_BLOCKED:
/*
 * Need to setup static MAC table with override to keep receiving BPDU
 * messages.  See sw_init_stp routine.
 */
		data &= ~(PORT_TX_ENABLE | PORT_RX_ENABLE);
		data |= PORT_LEARN_DISABLE;
		break;
	case STP_STATE_SIMPLE:
		data |= (PORT_TX_ENABLE | PORT_RX_ENABLE);
		data |= PORT_LEARN_DISABLE;
		break;
	}
	port_w16(hw, port, KS8842_PORT_CTRL_2_OFFSET, data);
	hw->ksz_switch->port_cfg[port].stp_state = state;
}

#define STP_ENTRY			0
#define BROADCAST_ENTRY			1
#define BRIDGE_ADDR_ENTRY		2
#define IPV6_ADDR_ENTRY			3

/**
 * sw_clr_sta_mac_table - clear static MAC table
 * @hw: 	The hardware instance.
 *
 * This routine clears the static MAC table.
 */
static void sw_clr_sta_mac_table(struct ksz_hw *hw)
{
	struct ksz_mac_table *entry;
	int i;

	for (i = 0; i < STATIC_MAC_TABLE_ENTRIES; i++) {
		entry = &hw->ksz_switch->mac_table[i];
		sw_w_sta_mac_table(hw, i,
			entry->mac_addr, entry->ports,
			entry->override, 0,
			entry->use_fid, entry->fid);
	}
}

/**
 * sw_init_stp - initialize switch spanning tree support
 * @hw: 	The hardware instance.
 *
 * This routine initializes the spanning tree support of the switch.
 */
static void sw_init_stp(struct ksz_hw *hw)
{
	struct ksz_mac_table *entry;

	entry = &hw->ksz_switch->mac_table[STP_ENTRY];
	entry->mac_addr[0] = 0x01;
	entry->mac_addr[1] = 0x80;
	entry->mac_addr[2] = 0xC2;
	entry->mac_addr[3] = 0x00;
	entry->mac_addr[4] = 0x00;
	entry->mac_addr[5] = 0x00;
	entry->ports = HOST_MASK;
	entry->override = 1;
	entry->valid = 1;
	sw_w_sta_mac_table(hw, STP_ENTRY,
		entry->mac_addr, entry->ports,
		entry->override, entry->valid,
		entry->use_fid, entry->fid);
}

/**
 * sw_block_addr - block certain packets from the host port
 * @hw: 	The hardware instance.
 *
 * This routine blocks certain packets from reaching to the host port.
 */
static void sw_block_addr(struct ksz_hw *hw)
{
	struct ksz_mac_table *entry;
	int i;

	for (i = BROADCAST_ENTRY; i <= IPV6_ADDR_ENTRY; i++) {
		entry = &hw->ksz_switch->mac_table[i];
		entry->valid = 0;
		sw_w_sta_mac_table(hw, i,
			entry->mac_addr, entry->ports,
			entry->override, entry->valid,
			entry->use_fid, entry->fid);
	}
}

#define PHY_LINK_SUPPORT		\
	(PHY_AUTO_NEG_ASYM_PAUSE |	\
	PHY_AUTO_NEG_SYM_PAUSE |	\
	PHY_AUTO_NEG_100BT4 |		\
	PHY_AUTO_NEG_100BTX_FD |	\
	PHY_AUTO_NEG_100BTX |		\
	PHY_AUTO_NEG_10BT_FD |		\
	PHY_AUTO_NEG_10BT)

static inline void hw_r_phy_ctrl(struct ksz_hw *hw, int phy, u16 *data)
{
	*data = readw(hw->io + phy + KS884X_PHY_CTRL_OFFSET);
}

static inline void hw_w_phy_ctrl(struct ksz_hw *hw, int phy, u16 data)
{
	writew(data, hw->io + phy + KS884X_PHY_CTRL_OFFSET);
}

static inline void hw_r_phy_link_stat(struct ksz_hw *hw, int phy, u16 *data)
{
	*data = readw(hw->io + phy + KS884X_PHY_STATUS_OFFSET);
}

static inline void hw_r_phy_auto_neg(struct ksz_hw *hw, int phy, u16 *data)
{
	*data = readw(hw->io + phy + KS884X_PHY_AUTO_NEG_OFFSET);
}

static inline void hw_w_phy_auto_neg(struct ksz_hw *hw, int phy, u16 data)
{
	writew(data, hw->io + phy + KS884X_PHY_AUTO_NEG_OFFSET);
}

static inline void hw_r_phy_rem_cap(struct ksz_hw *hw, int phy, u16 *data)
{
	*data = readw(hw->io + phy + KS884X_PHY_REMOTE_CAP_OFFSET);
}

static inline void hw_r_phy_crossover(struct ksz_hw *hw, int phy, u16 *data)
{
	*data = readw(hw->io + phy + KS884X_PHY_CTRL_OFFSET);
}

static inline void hw_w_phy_crossover(struct ksz_hw *hw, int phy, u16 data)
{
	writew(data, hw->io + phy + KS884X_PHY_CTRL_OFFSET);
}

static inline void hw_r_phy_polarity(struct ksz_hw *hw, int phy, u16 *data)
{
	*data = readw(hw->io + phy + KS884X_PHY_PHY_CTRL_OFFSET);
}

static inline void hw_w_phy_polarity(struct ksz_hw *hw, int phy, u16 data)
{
	writew(data, hw->io + phy + KS884X_PHY_PHY_CTRL_OFFSET);
}

static inline void hw_r_phy_link_md(struct ksz_hw *hw, int phy, u16 *data)
{
	*data = readw(hw->io + phy + KS884X_PHY_LINK_MD_OFFSET);
}

static inline void hw_w_phy_link_md(struct ksz_hw *hw, int phy, u16 data)
{
	writew(data, hw->io + phy + KS884X_PHY_LINK_MD_OFFSET);
}

/**
 * hw_r_phy - read data from PHY register
 * @hw: 	The hardware instance.
 * @port:	Port to read.
 * @reg:	PHY register to read.
 * @val:	Buffer to store the read data.
 *
 * This routine reads data from the PHY register.
 */
static void hw_r_phy(struct ksz_hw *hw, int port, u16 reg, u16 *val)
{
	int phy;

	phy = KS884X_PHY_1_CTRL_OFFSET + port * PHY_CTRL_INTERVAL + reg;
	*val = readw(hw->io + phy);
}

/**
 * port_w_phy - write data to PHY register
 * @hw: 	The hardware instance.
 * @port:	Port to write.
 * @reg:	PHY register to write.
 * @val:	Word data to write.
 *
 * This routine writes data to the PHY register.
 */
static void hw_w_phy(struct ksz_hw *hw, int port, u16 reg, u16 val)
{
	int phy;

	phy = KS884X_PHY_1_CTRL_OFFSET + port * PHY_CTRL_INTERVAL + reg;
	writew(val, hw->io + phy);
}

/*
 * EEPROM access functions
 */

#define AT93C_CODE			0
#define AT93C_WR_OFF			0x00
#define AT93C_WR_ALL			0x10
#define AT93C_ER_ALL			0x20
#define AT93C_WR_ON			0x30

#define AT93C_WRITE			1
#define AT93C_READ			2
#define AT93C_ERASE			3

#define EEPROM_DELAY			4

static inline void drop_gpio(struct ksz_hw *hw, u8 gpio)
{
	u16 data;

	data = readw(hw->io + KS884X_EEPROM_CTRL_OFFSET);
	data &= ~gpio;
	writew(data, hw->io + KS884X_EEPROM_CTRL_OFFSET);
}

static inline void raise_gpio(struct ksz_hw *hw, u8 gpio)
{
	u16 data;

	data = readw(hw->io + KS884X_EEPROM_CTRL_OFFSET);
	data |= gpio;
	writew(data, hw->io + KS884X_EEPROM_CTRL_OFFSET);
}

static inline u8 state_gpio(struct ksz_hw *hw, u8 gpio)
{
	u16 data;

	data = readw(hw->io + KS884X_EEPROM_CTRL_OFFSET);
	return (u8)(data & gpio);
}

static void eeprom_clk(struct ksz_hw *hw)
{
	raise_gpio(hw, EEPROM_SERIAL_CLOCK);
	udelay(EEPROM_DELAY);
	drop_gpio(hw, EEPROM_SERIAL_CLOCK);
	udelay(EEPROM_DELAY);
}

static u16 spi_r(struct ksz_hw *hw)
{
	int i;
	u16 temp = 0;

	for (i = 15; i >= 0; i--) {
		raise_gpio(hw, EEPROM_SERIAL_CLOCK);
		udelay(EEPROM_DELAY);

		temp |= (state_gpio(hw, EEPROM_DATA_IN)) ? 1 << i : 0;

		drop_gpio(hw, EEPROM_SERIAL_CLOCK);
		udelay(EEPROM_DELAY);
	}
	return temp;
}

static void spi_w(struct ksz_hw *hw, u16 data)
{
	int i;

	for (i = 15; i >= 0; i--) {
		(data & (0x01 << i)) ? raise_gpio(hw, EEPROM_DATA_OUT) :
			drop_gpio(hw, EEPROM_DATA_OUT);
		eeprom_clk(hw);
	}
}

static void spi_reg(struct ksz_hw *hw, u8 data, u8 reg)
{
	int i;

	/* Initial start bit */
	raise_gpio(hw, EEPROM_DATA_OUT);
	eeprom_clk(hw);

	/* AT93C operation */
	for (i = 1; i >= 0; i--) {
		(data & (0x01 << i)) ? raise_gpio(hw, EEPROM_DATA_OUT) :
			drop_gpio(hw, EEPROM_DATA_OUT);
		eeprom_clk(hw);
	}

	/* Address location */
	for (i = 5; i >= 0; i--) {
		(reg & (0x01 << i)) ? raise_gpio(hw, EEPROM_DATA_OUT) :
			drop_gpio(hw, EEPROM_DATA_OUT);
		eeprom_clk(hw);
	}
}

#define EEPROM_DATA_RESERVED		0
#define EEPROM_DATA_MAC_ADDR_0		1
#define EEPROM_DATA_MAC_ADDR_1		2
#define EEPROM_DATA_MAC_ADDR_2		3
#define EEPROM_DATA_SUBSYS_ID		4
#define EEPROM_DATA_SUBSYS_VEN_ID	5
#define EEPROM_DATA_PM_CAP		6

/* User defined EEPROM data */
#define EEPROM_DATA_OTHER_MAC_ADDR	9

/**
 * eeprom_read - read from AT93C46 EEPROM
 * @hw: 	The hardware instance.
 * @reg:	The register offset.
 *
 * This function reads a word from the AT93C46 EEPROM.
 *
 * Return the data value.
 */
static u16 eeprom_read(struct ksz_hw *hw, u8 reg)
{
	u16 data;

	raise_gpio(hw, EEPROM_ACCESS_ENABLE | EEPROM_CHIP_SELECT);

	spi_reg(hw, AT93C_READ, reg);
	data = spi_r(hw);

	drop_gpio(hw, EEPROM_ACCESS_ENABLE | EEPROM_CHIP_SELECT);

	return data;
}

/**
 * eeprom_write - write to AT93C46 EEPROM
 * @hw: 	The hardware instance.
 * @reg:	The register offset.
 * @data:	The data value.
 *
 * This procedure writes a word to the AT93C46 EEPROM.
 */
static void eeprom_write(struct ksz_hw *hw, u8 reg, u16 data)
{
	int timeout;

	raise_gpio(hw, EEPROM_ACCESS_ENABLE | EEPROM_CHIP_SELECT);

	/* Enable write. */
	spi_reg(hw, AT93C_CODE, AT93C_WR_ON);
	drop_gpio(hw, EEPROM_CHIP_SELECT);
	udelay(1);

	/* Erase the register. */
	raise_gpio(hw, EEPROM_CHIP_SELECT);
	spi_reg(hw, AT93C_ERASE, reg);
	drop_gpio(hw, EEPROM_CHIP_SELECT);
	udelay(1);

	/* Check operation complete. */
	raise_gpio(hw, EEPROM_CHIP_SELECT);
	timeout = 8;
	mdelay(2);
	do {
		mdelay(1);
	} while (!state_gpio(hw, EEPROM_DATA_IN) && --timeout);
	drop_gpio(hw, EEPROM_CHIP_SELECT);
	udelay(1);

	/* Write the register. */
	raise_gpio(hw, EEPROM_CHIP_SELECT);
	spi_reg(hw, AT93C_WRITE, reg);
	spi_w(hw, data);
	drop_gpio(hw, EEPROM_CHIP_SELECT);
	udelay(1);

	/* Check operation complete. */
	raise_gpio(hw, EEPROM_CHIP_SELECT);
	timeout = 8;
	mdelay(2);
	do {
		mdelay(1);
	} while (!state_gpio(hw, EEPROM_DATA_IN) && --timeout);
	drop_gpio(hw, EEPROM_CHIP_SELECT);
	udelay(1);

	/* Disable write. */
	raise_gpio(hw, EEPROM_CHIP_SELECT);
	spi_reg(hw, AT93C_CODE, AT93C_WR_OFF);

	drop_gpio(hw, EEPROM_ACCESS_ENABLE | EEPROM_CHIP_SELECT);
}

/*
 * Link detection routines
 */

static u16 advertised_flow_ctrl(struct ksz_port *port, u16 ctrl)
{
	ctrl &= ~PORT_AUTO_NEG_SYM_PAUSE;
	switch (port->flow_ctrl) {
	case PHY_FLOW_CTRL:
		ctrl |= PORT_AUTO_NEG_SYM_PAUSE;
		break;
	/* Not supported. */
	case PHY_TX_ONLY:
	case PHY_RX_ONLY:
	default:
		break;
	}
	return ctrl;
}

static void set_flow_ctrl(struct ksz_hw *hw, int rx, int tx)
{
	u32 rx_cfg;
	u32 tx_cfg;

	rx_cfg = hw->rx_cfg;
	tx_cfg = hw->tx_cfg;
	if (rx)
		hw->rx_cfg |= DMA_RX_FLOW_ENABLE;
	else
		hw->rx_cfg &= ~DMA_RX_FLOW_ENABLE;
	if (tx)
		hw->tx_cfg |= DMA_TX_FLOW_ENABLE;
	else
		hw->tx_cfg &= ~DMA_TX_FLOW_ENABLE;
	if (hw->enabled) {
		if (rx_cfg != hw->rx_cfg)
			writel(hw->rx_cfg, hw->io + KS_DMA_RX_CTRL);
		if (tx_cfg != hw->tx_cfg)
			writel(hw->tx_cfg, hw->io + KS_DMA_TX_CTRL);
	}
}

static void determine_flow_ctrl(struct ksz_hw *hw, struct ksz_port *port,
	u16 local, u16 remote)
{
	int rx;
	int tx;

	if (hw->overrides & PAUSE_FLOW_CTRL)
		return;

	rx = tx = 0;
	if (port->force_link)
		rx = tx = 1;
	if (remote & PHY_AUTO_NEG_SYM_PAUSE) {
		if (local & PHY_AUTO_NEG_SYM_PAUSE) {
			rx = tx = 1;
		} else if ((remote & PHY_AUTO_NEG_ASYM_PAUSE) &&
				(local & PHY_AUTO_NEG_PAUSE) ==
				PHY_AUTO_NEG_ASYM_PAUSE) {
			tx = 1;
		}
	} else if (remote & PHY_AUTO_NEG_ASYM_PAUSE) {
		if ((local & PHY_AUTO_NEG_PAUSE) == PHY_AUTO_NEG_PAUSE)
			rx = 1;
	}
	if (!hw->ksz_switch)
		set_flow_ctrl(hw, rx, tx);
}

static inline void port_cfg_change(struct ksz_hw *hw, struct ksz_port *port,
	struct ksz_port_info *info, u16 link_status)
{
	if ((hw->features & HALF_DUPLEX_SIGNAL_BUG) &&
			!(hw->overrides & PAUSE_FLOW_CTRL)) {
		u32 cfg = hw->tx_cfg;

		/* Disable flow control in the half duplex mode. */
		if (1 == info->duplex)
			hw->tx_cfg &= ~DMA_TX_FLOW_ENABLE;
		if (hw->enabled && cfg != hw->tx_cfg)
			writel(hw->tx_cfg, hw->io + KS_DMA_TX_CTRL);
	}
}

/**
 * port_get_link_speed - get current link status
 * @port: 	The port instance.
 *
 * This routine reads PHY registers to determine the current link status of the
 * switch ports.
 */
static void port_get_link_speed(struct ksz_port *port)
{
	uint interrupt;
	struct ksz_port_info *info;
	struct ksz_port_info *linked = NULL;
	struct ksz_hw *hw = port->hw;
	u16 data;
	u16 status;
	u8 local;
	u8 remote;
	int i;
	int p;
	int change = 0;

	interrupt = hw_block_intr(hw);

	for (i = 0, p = port->first_port; i < port->port_cnt; i++, p++) {
		info = &hw->port_info[p];
		port_r16(hw, p, KS884X_PORT_CTRL_4_OFFSET, &data);
		port_r16(hw, p, KS884X_PORT_STATUS_OFFSET, &status);

		/*
		 * Link status is changing all the time even when there is no
		 * cable connection!
		 */
		remote = status & (PORT_AUTO_NEG_COMPLETE |
			PORT_STATUS_LINK_GOOD);
		local = (u8) data;

		/* No change to status. */
		if (local == info->advertised && remote == info->partner)
			continue;

		info->advertised = local;
		info->partner = remote;
		if (status & PORT_STATUS_LINK_GOOD) {

			/* Remember the first linked port. */
			if (!linked)
				linked = info;

			info->tx_rate = 10 * TX_RATE_UNIT;
			if (status & PORT_STATUS_SPEED_100MBIT)
				info->tx_rate = 100 * TX_RATE_UNIT;

			info->duplex = 1;
			if (status & PORT_STATUS_FULL_DUPLEX)
				info->duplex = 2;

			if (media_connected != info->state) {
				hw_r_phy(hw, p, KS884X_PHY_AUTO_NEG_OFFSET,
					&data);
				hw_r_phy(hw, p, KS884X_PHY_REMOTE_CAP_OFFSET,
					&status);
				determine_flow_ctrl(hw, port, data, status);
				if (hw->ksz_switch) {
					port_cfg_back_pressure(hw, p,
						(1 == info->duplex));
				}
				change |= 1 << i;
				port_cfg_change(hw, port, info, status);
			}
			info->state = media_connected;
		} else {
			if (media_disconnected != info->state) {
				change |= 1 << i;

				/* Indicate the link just goes down. */
				hw->port_mib[p].link_down = 1;
			}
			info->state = media_disconnected;
		}
		hw->port_mib[p].state = (u8) info->state;
	}

	if (linked && media_disconnected == port->linked->state)
		port->linked = linked;

	hw_restore_intr(hw, interrupt);
}

#define PHY_RESET_TIMEOUT		10

/**
 * port_set_link_speed - set port speed
 * @port: 	The port instance.
 *
 * This routine sets the link speed of the switch ports.
 */
static void port_set_link_speed(struct ksz_port *port)
{
	struct ksz_port_info *info;
	struct ksz_hw *hw = port->hw;
	u16 data;
	u16 cfg;
	u8 status;
	int i;
	int p;

	for (i = 0, p = port->first_port; i < port->port_cnt; i++, p++) {
		info = &hw->port_info[p];

		port_r16(hw, p, KS884X_PORT_CTRL_4_OFFSET, &data);
		port_r8(hw, p, KS884X_PORT_STATUS_OFFSET, &status);

		cfg = 0;
		if (status & PORT_STATUS_LINK_GOOD)
			cfg = data;

		data |= PORT_AUTO_NEG_ENABLE;
		data = advertised_flow_ctrl(port, data);

		data |= PORT_AUTO_NEG_100BTX_FD | PORT_AUTO_NEG_100BTX |
			PORT_AUTO_NEG_10BT_FD | PORT_AUTO_NEG_10BT;

		/* Check if manual configuration is specified by the user. */
		if (port->speed || port->duplex) {
			if (10 == port->speed)
				data &= ~(PORT_AUTO_NEG_100BTX_FD |
					PORT_AUTO_NEG_100BTX);
			else if (100 == port->speed)
				data &= ~(PORT_AUTO_NEG_10BT_FD |
					PORT_AUTO_NEG_10BT);
			if (1 == port->duplex)
				data &= ~(PORT_AUTO_NEG_100BTX_FD |
					PORT_AUTO_NEG_10BT_FD);
			else if (2 == port->duplex)
				data &= ~(PORT_AUTO_NEG_100BTX |
					PORT_AUTO_NEG_10BT);
		}
		if (data != cfg) {
			data |= PORT_AUTO_NEG_RESTART;
			port_w16(hw, p, KS884X_PORT_CTRL_4_OFFSET, data);
		}
	}
}

/**
 * port_force_link_speed - force port speed
 * @port: 	The port instance.
 *
 * This routine forces the link speed of the switch ports.
 */
static void port_force_link_speed(struct ksz_port *port)
{
	struct ksz_hw *hw = port->hw;
	u16 data;
	int i;
	int phy;
	int p;

	for (i = 0, p = port->first_port; i < port->port_cnt; i++, p++) {
		phy = KS884X_PHY_1_CTRL_OFFSET + p * PHY_CTRL_INTERVAL;
		hw_r_phy_ctrl(hw, phy, &data);

		data &= ~PHY_AUTO_NEG_ENABLE;

		if (10 == port->speed)
			data &= ~PHY_SPEED_100MBIT;
		else if (100 == port->speed)
			data |= PHY_SPEED_100MBIT;
		if (1 == port->duplex)
			data &= ~PHY_FULL_DUPLEX;
		else if (2 == port->duplex)
			data |= PHY_FULL_DUPLEX;
		hw_w_phy_ctrl(hw, phy, data);
	}
}

static void port_set_power_saving(struct ksz_port *port, int enable)
{
	struct ksz_hw *hw = port->hw;
	int i;
	int p;

	for (i = 0, p = port->first_port; i < port->port_cnt; i++, p++)
		port_cfg(hw, p,
			KS884X_PORT_CTRL_4_OFFSET, PORT_POWER_DOWN, enable);
}

/*
 * KSZ8841 power management functions
 */

/**
 * hw_chk_wol_pme_status - check PMEN pin
 * @hw: 	The hardware instance.
 *
 * This function is used to check PMEN pin is asserted.
 *
 * Return 1 if PMEN pin is asserted; otherwise, 0.
 */
static int hw_chk_wol_pme_status(struct ksz_hw *hw)
{
	struct dev_info *hw_priv = container_of(hw, struct dev_info, hw);
	struct pci_dev *pdev = hw_priv->pdev;
	u16 data;

	if (!pdev->pm_cap)
		return 0;
	pci_read_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, &data);
	return (data & PCI_PM_CTRL_PME_STATUS) == PCI_PM_CTRL_PME_STATUS;
}

/**
 * hw_clr_wol_pme_status - clear PMEN pin
 * @hw: 	The hardware instance.
 *
 * This routine is used to clear PME_Status to deassert PMEN pin.
 */
static void hw_clr_wol_pme_status(struct ksz_hw *hw)
{
	struct dev_info *hw_priv = container_of(hw, struct dev_info, hw);
	struct pci_dev *pdev = hw_priv->pdev;
	u16 data;

	if (!pdev->pm_cap)
		return;

	/* Clear PME_Status to deassert PMEN pin. */
	pci_read_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, &data);
	data |= PCI_PM_CTRL_PME_STATUS;
	pci_write_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, data);
}

/**
 * hw_cfg_wol_pme - enable or disable Wake-on-LAN
 * @hw: 	The hardware instance.
 * @set:	The flag indicating whether to enable or disable.
 *
 * This routine is used to enable or disable Wake-on-LAN.
 */
static void hw_cfg_wol_pme(struct ksz_hw *hw, int set)
{
	struct dev_info *hw_priv = container_of(hw, struct dev_info, hw);
	struct pci_dev *pdev = hw_priv->pdev;
	u16 data;

	if (!pdev->pm_cap)
		return;
	pci_read_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, &data);
	data &= ~PCI_PM_CTRL_STATE_MASK;
	if (set)
		data |= PCI_PM_CTRL_PME_ENABLE | PCI_D3hot;
	else
		data &= ~PCI_PM_CTRL_PME_ENABLE;
	pci_write_config_word(pdev, pdev->pm_cap + PCI_PM_CTRL, data);
}

/**
 * hw_cfg_wol - configure Wake-on-LAN features
 * @hw: 	The hardware instance.
 * @frame:	The pattern frame bit.
 * @set:	The flag indicating whether to enable or disable.
 *
 * This routine is used to enable or disable certain Wake-on-LAN features.
 */
static void hw_cfg_wol(struct ksz_hw *hw, u16 frame, int set)
{
	u16 data;

	data = readw(hw->io + KS8841_WOL_CTRL_OFFSET);
	if (set)
		data |= frame;
	else
		data &= ~frame;
	writew(data, hw->io + KS8841_WOL_CTRL_OFFSET);
}

/**
 * hw_set_wol_frame - program Wake-on-LAN pattern
 * @hw: 	The hardware instance.
 * @i:		The frame index.
 * @mask_size:	The size of the mask.
 * @mask:	Mask to ignore certain bytes in the pattern.
 * @frame_size:	The size of the frame.
 * @pattern:	The frame data.
 *
 * This routine is used to program Wake-on-LAN pattern.
 */
static void hw_set_wol_frame(struct ksz_hw *hw, int i, uint mask_size,
	const u8 *mask, uint frame_size, const u8 *pattern)
{
	int bits;
	int from;
	int len;
	int to;
	u32 crc;
	u8 data[64];
	u8 val = 0;

	if (frame_size > mask_size * 8)
		frame_size = mask_size * 8;
	if (frame_size > 64)
		frame_size = 64;

	i *= 0x10;
	writel(0, hw->io + KS8841_WOL_FRAME_BYTE0_OFFSET + i);
	writel(0, hw->io + KS8841_WOL_FRAME_BYTE2_OFFSET + i);

	bits = len = from = to = 0;
	do {
		if (bits) {
			if ((val & 1))
				data[to++] = pattern[from];
			val >>= 1;
			++from;
			--bits;
		} else {
			val = mask[len];
			writeb(val, hw->io + KS8841_WOL_FRAME_BYTE0_OFFSET + i
				+ len);
			++len;
			if (val)
				bits = 8;
			else
				from += 8;
		}
	} while (from < (int) frame_size);
	if (val) {
		bits = mask[len - 1];
		val <<= (from % 8);
		bits &= ~val;
		writeb(bits, hw->io + KS8841_WOL_FRAME_BYTE0_OFFSET + i + len -
			1);
	}
	crc = ether_crc(to, data);
	writel(crc, hw->io + KS8841_WOL_FRAME_CRC_OFFSET + i);
}

/**
 * hw_add_wol_arp - add ARP pattern
 * @hw: 	The hardware instance.
 * @ip_addr:	The IPv4 address assigned to the device.
 *
 * This routine is used to add ARP pattern for waking up the host.
 */
static void hw_add_wol_arp(struct ksz_hw *hw, const u8 *ip_addr)
{
	static const u8 mask[6] = { 0x3F, 0xF0, 0x3F, 0x00, 0xC0, 0x03 };
	u8 pattern[42] = {
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x08, 0x06,
		0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00 };

	memcpy(&pattern[38], ip_addr, 4);
	hw_set_wol_frame(hw, 3, 6, mask, 42, pattern);
}

/**
 * hw_add_wol_bcast - add broadcast pattern
 * @hw: 	The hardware instance.
 *
 * This routine is used to add broadcast pattern for waking up the host.
 */
static void hw_add_wol_bcast(struct ksz_hw *hw)
{
	static const u8 mask[] = { 0x3F };
	static const u8 pattern[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

	hw_set_wol_frame(hw, 2, 1, mask, MAC_ADDR_LEN, pattern);
}

/**
 * hw_add_wol_mcast - add multicast pattern
 * @hw: 	The hardware instance.
 *
 * This routine is used to add multicast pattern for waking up the host.
 *
 * It is assumed the multicast packet is the ICMPv6 neighbor solicitation used
 * by IPv6 ping command.  Note that multicast packets are filtred through the
 * multicast hash table, so not all multicast packets can wake up the host.
 */
static void hw_add_wol_mcast(struct ksz_hw *hw)
{
	static const u8 mask[] = { 0x3F };
	u8 pattern[] = { 0x33, 0x33, 0xFF, 0x00, 0x00, 0x00 };

	memcpy(&pattern[3], &hw->override_addr[3], 3);
	hw_set_wol_frame(hw, 1, 1, mask, 6, pattern);
}

/**
 * hw_add_wol_ucast - add unicast pattern
 * @hw: 	The hardware instance.
 *
 * This routine is used to add unicast pattern to wakeup the host.
 *
 * It is assumed the unicast packet is directed to the device, as the hardware
 * can only receive them in normal case.
 */
static void hw_add_wol_ucast(struct ksz_hw *hw)
{
	static const u8 mask[] = { 0x3F };

	hw_set_wol_frame(hw, 0, 1, mask, MAC_ADDR_LEN, hw->override_addr);
}

/**
 * hw_enable_wol - enable Wake-on-LAN
 * @hw: 	The hardware instance.
 * @wol_enable:	The Wake-on-LAN settings.
 * @net_addr:	The IPv4 address assigned to the device.
 *
 * This routine is used to enable Wake-on-LAN depending on driver settings.
 */
static void hw_enable_wol(struct ksz_hw *hw, u32 wol_enable, const u8 *net_addr)
{
	hw_cfg_wol(hw, KS8841_WOL_MAGIC_ENABLE, (wol_enable & WAKE_MAGIC));
	hw_cfg_wol(hw, KS8841_WOL_FRAME0_ENABLE, (wol_enable & WAKE_UCAST));
	hw_add_wol_ucast(hw);
	hw_cfg_wol(hw, KS8841_WOL_FRAME1_ENABLE, (wol_enable & WAKE_MCAST));
	hw_add_wol_mcast(hw);
	hw_cfg_wol(hw, KS8841_WOL_FRAME2_ENABLE, (wol_enable & WAKE_BCAST));
	hw_cfg_wol(hw, KS8841_WOL_FRAME3_ENABLE, (wol_enable & WAKE_ARP));
	hw_add_wol_arp(hw, net_addr);
}

/**
 * hw_init - check driver is correct for the hardware
 * @hw: 	The hardware instance.
 *
 * This function checks the hardware is correct for this driver and sets the
 * hardware up for proper initialization.
 *
 * Return number of ports or 0 if not right.
 */
static int hw_init(struct ksz_hw *hw)
{
	int rc = 0;
	u16 data;
	u16 revision;

	/* Set bus speed to 125MHz. */
	writew(BUS_SPEED_125_MHZ, hw->io + KS884X_BUS_CTRL_OFFSET);

	/* Check KSZ884x chip ID. */
	data = readw(hw->io + KS884X_CHIP_ID_OFFSET);

	revision = (data & KS884X_REVISION_MASK) >> KS884X_REVISION_SHIFT;
	data &= KS884X_CHIP_ID_MASK_41;
	if (REG_CHIP_ID_41 == data)
		rc = 1;
	else if (REG_CHIP_ID_42 == data)
		rc = 2;
	else
		return 0;

	/* Setup hardware features or bug workarounds. */
	if (revision <= 1) {
		hw->features |= SMALL_PACKET_TX_BUG;
		if (1 == rc)
			hw->features |= HALF_DUPLEX_SIGNAL_BUG;
	}
	hw->features |= IPV6_CSUM_GEN_HACK;
	return rc;
}

/**
 * hw_reset - reset the hardware
 * @hw: 	The hardware instance.
 *
 * This routine resets the hardware.
 */
static void hw_reset(struct ksz_hw *hw)
{
	writew(GLOBAL_SOFTWARE_RESET, hw->io + KS884X_GLOBAL_CTRL_OFFSET);

	/* Wait for device to reset. */
	mdelay(10);

	/* Write 0 to clear device reset. */
	writew(0, hw->io + KS884X_GLOBAL_CTRL_OFFSET);
}

/**
 * hw_setup - setup the hardware
 * @hw: 	The hardware instance.
 *
 * This routine setup the hardware for proper operation.
 */
static void hw_setup(struct ksz_hw *hw)
{
#if SET_DEFAULT_LED
	u16 data;

	/* Change default LED mode. */
	data = readw(hw->io + KS8842_SWITCH_CTRL_5_OFFSET);
	data &= ~LED_MODE;
	data |= SET_DEFAULT_LED;
	writew(data, hw->io + KS8842_SWITCH_CTRL_5_OFFSET);
#endif

	/* Setup transmit control. */
	hw->tx_cfg = (DMA_TX_PAD_ENABLE | DMA_TX_CRC_ENABLE |
		(DMA_BURST_DEFAULT << DMA_BURST_SHIFT) | DMA_TX_ENABLE);

	/* Setup receive control. */
	hw->rx_cfg = (DMA_RX_BROADCAST | DMA_RX_UNICAST |
		(DMA_BURST_DEFAULT << DMA_BURST_SHIFT) | DMA_RX_ENABLE);
	hw->rx_cfg |= KS884X_DMA_RX_MULTICAST;

	/* Hardware cannot handle UDP packet in IP fragments. */
	hw->rx_cfg |= (DMA_RX_CSUM_TCP | DMA_RX_CSUM_IP);

	if (hw->all_multi)
		hw->rx_cfg |= DMA_RX_ALL_MULTICAST;
	if (hw->promiscuous)
		hw->rx_cfg |= DMA_RX_PROMISCUOUS;
}

/**
 * hw_setup_intr - setup interrupt mask
 * @hw: 	The hardware instance.
 *
 * This routine setup the interrupt mask for proper operation.
 */
static void hw_setup_intr(struct ksz_hw *hw)
{
	hw->intr_mask = KS884X_INT_MASK | KS884X_INT_RX_OVERRUN;
}

static void ksz_check_desc_num(struct ksz_desc_info *info)
{
#define MIN_DESC_SHIFT  2

	int alloc = info->alloc;
	int shift;

	shift = 0;
	while (!(alloc & 1)) {
		shift++;
		alloc >>= 1;
	}
	if (alloc != 1 || shift < MIN_DESC_SHIFT) {
		pr_alert("Hardware descriptor numbers not right!\n");
		while (alloc) {
			shift++;
			alloc >>= 1;
		}
		if (shift < MIN_DESC_SHIFT)
			shift = MIN_DESC_SHIFT;
		alloc = 1 << shift;
		info->alloc = alloc;
	}
	info->mask = info->alloc - 1;
}

static void hw_init_desc(struct ksz_desc_info *desc_info, int transmit)
{
	int i;
	u32 phys = desc_info->ring_phys;
	struct ksz_hw_desc *desc = desc_info->ring_virt;
	struct ksz_desc *cur = desc_info->ring;
	struct ksz_desc *previous = NULL;

	for (i = 0; i < desc_info->alloc; i++) {
		cur->phw = desc++;
		phys += desc_info->size;
		previous = cur++;
		previous->phw->next = cpu_to_le32(phys);
	}
	previous->phw->next = cpu_to_le32(desc_info->ring_phys);
	previous->sw.buf.rx.end_of_ring = 1;
	previous->phw->buf.data = cpu_to_le32(previous->sw.buf.data);

	desc_info->avail = desc_info->alloc;
	desc_info->last = desc_info->next = 0;

	desc_info->cur = desc_info->ring;
}

/**
 * hw_set_desc_base - set descriptor base addresses
 * @hw: 	The hardware instance.
 * @tx_addr:	The transmit descriptor base.
 * @rx_addr:	The receive descriptor base.
 *
 * This routine programs the descriptor base addresses after reset.
 */
static void hw_set_desc_base(struct ksz_hw *hw, u32 tx_addr, u32 rx_addr)
{
	/* Set base address of Tx/Rx descriptors. */
	writel(tx_addr, hw->io + KS_DMA_TX_ADDR);
	writel(rx_addr, hw->io + KS_DMA_RX_ADDR);
}

static void hw_reset_pkts(struct ksz_desc_info *info)
{
	info->cur = info->ring;
	info->avail = info->alloc;
	info->last = info->next = 0;
}

static inline void hw_resume_rx(struct ksz_hw *hw)
{
	writel(DMA_START, hw->io + KS_DMA_RX_START);
}

/**
 * hw_start_rx - start receiving
 * @hw: 	The hardware instance.
 *
 * This routine starts the receive function of the hardware.
 */
static void hw_start_rx(struct ksz_hw *hw)
{
	writel(hw->rx_cfg, hw->io + KS_DMA_RX_CTRL);

	/* Notify when the receive stops. */
	hw->intr_mask |= KS884X_INT_RX_STOPPED;

	writel(DMA_START, hw->io + KS_DMA_RX_START);
	hw_ack_intr(hw, KS884X_INT_RX_STOPPED);
	hw->rx_stop++;

	/* Variable overflows. */
	if (0 == hw->rx_stop)
		hw->rx_stop = 2;
}

/*
 * hw_stop_rx - stop receiving
 * @hw: 	The hardware instance.
 *
 * This routine stops the receive function of the hardware.
 */
static void hw_stop_rx(struct ksz_hw *hw)
{
	hw->rx_stop = 0;
	hw_turn_off_intr(hw, KS884X_INT_RX_STOPPED);
	writel((hw->rx_cfg & ~DMA_RX_ENABLE), hw->io + KS_DMA_RX_CTRL);
}

/**
 * hw_start_tx - start transmitting
 * @hw: 	The hardware instance.
 *
 * This routine starts the transmit function of the hardware.
 */
static void hw_start_tx(struct ksz_hw *hw)
{
	writel(hw->tx_cfg, hw->io + KS_DMA_TX_CTRL);
}

/**
 * hw_stop_tx - stop transmitting
 * @hw: 	The hardware instance.
 *
 * This routine stops the transmit function of the hardware.
 */
static void hw_stop_tx(struct ksz_hw *hw)
{
	writel((hw->tx_cfg & ~DMA_TX_ENABLE), hw->io + KS_DMA_TX_CTRL);
}

/**
 * hw_disable - disable hardware
 * @hw: 	The hardware instance.
 *
 * This routine disables the hardware.
 */
static void hw_disable(struct ksz_hw *hw)
{
	hw_stop_rx(hw);
	hw_stop_tx(hw);
	hw->enabled = 0;
}

/**
 * hw_enable - enable hardware
 * @hw: 	The hardware instance.
 *
 * This routine enables the hardware.
 */
static void hw_enable(struct ksz_hw *hw)
{
	hw_start_tx(hw);
	hw_start_rx(hw);
	hw->enabled = 1;
}

/**
 * hw_alloc_pkt - allocate enough descriptors for transmission
 * @hw: 	The hardware instance.
 * @length:	The length of the packet.
 * @physical:	Number of descriptors required.
 *
 * This function allocates descriptors for transmission.
 *
 * Return 0 if not successful; 1 for buffer copy; or number of descriptors.
 */
static int hw_alloc_pkt(struct ksz_hw *hw, int length, int physical)
{
	/* Always leave one descriptor free. */
	if (hw->tx_desc_info.avail <= 1)
		return 0;

	/* Allocate a descriptor for transmission and mark it current. */
	get_tx_pkt(&hw->tx_desc_info, &hw->tx_desc_info.cur);
	hw->tx_desc_info.cur->sw.buf.tx.first_seg = 1;

	/* Keep track of number of transmit descriptors used so far. */
	++hw->tx_int_cnt;
	hw->tx_size += length;

	/* Cannot hold on too much data. */
	if (hw->tx_size >= MAX_TX_HELD_SIZE)
		hw->tx_int_cnt = hw->tx_int_mask + 1;

	if (physical > hw->tx_desc_info.avail)
		return 1;

	return hw->tx_desc_info.avail;
}

/**
 * hw_send_pkt - mark packet for transmission
 * @hw: 	The hardware instance.
 *
 * This routine marks the packet for transmission in PCI version.
 */
static void hw_send_pkt(struct ksz_hw *hw)
{
	struct ksz_desc *cur = hw->tx_desc_info.cur;

	cur->sw.buf.tx.last_seg = 1;

	/* Interrupt only after specified number of descriptors used. */
	if (hw->tx_int_cnt > hw->tx_int_mask) {
		cur->sw.buf.tx.intr = 1;
		hw->tx_int_cnt = 0;
		hw->tx_size = 0;
	}

	/* KSZ8842 supports port directed transmission. */
	cur->sw.buf.tx.dest_port = hw->dst_ports;

	release_desc(cur);

	writel(0, hw->io + KS_DMA_TX_START);
}

static int empty_addr(u8 *addr)
{
	u32 *addr1 = (u32 *) addr;
	u16 *addr2 = (u16 *) &addr[4];

	return 0 == *addr1 && 0 == *addr2;
}

/**
 * hw_set_addr - set MAC address
 * @hw: 	The hardware instance.
 *
 * This routine programs the MAC address of the hardware when the address is
 * overrided.
 */
static void hw_set_addr(struct ksz_hw *hw)
{
	int i;

	for (i = 0; i < MAC_ADDR_LEN; i++)
		writeb(hw->override_addr[MAC_ADDR_ORDER(i)],
			hw->io + KS884X_ADDR_0_OFFSET + i);

	sw_set_addr(hw, hw->override_addr);
}

/**
 * hw_read_addr - read MAC address
 * @hw: 	The hardware instance.
 *
 * This routine retrieves the MAC address of the hardware.
 */
static void hw_read_addr(struct ksz_hw *hw)
{
	int i;

	for (i = 0; i < MAC_ADDR_LEN; i++)
		hw->perm_addr[MAC_ADDR_ORDER(i)] = readb(hw->io +
			KS884X_ADDR_0_OFFSET + i);

	if (!hw->mac_override) {
		memcpy(hw->override_addr, hw->perm_addr, MAC_ADDR_LEN);
		if (empty_addr(hw->override_addr)) {
			memcpy(hw->perm_addr, DEFAULT_MAC_ADDRESS,
				MAC_ADDR_LEN);
			memcpy(hw->override_addr, DEFAULT_MAC_ADDRESS,
				MAC_ADDR_LEN);
			hw->override_addr[5] += hw->id;
			hw_set_addr(hw);
		}
	}
}

static void hw_ena_add_addr(struct ksz_hw *hw, int index, u8 *mac_addr)
{
	int i;
	u32 mac_addr_lo;
	u32 mac_addr_hi;

	mac_addr_hi = 0;
	for (i = 0; i < 2; i++) {
		mac_addr_hi <<= 8;
		mac_addr_hi |= mac_addr[i];
	}
	mac_addr_hi |= ADD_ADDR_ENABLE;
	mac_addr_lo = 0;
	for (i = 2; i < 6; i++) {
		mac_addr_lo <<= 8;
		mac_addr_lo |= mac_addr[i];
	}
	index *= ADD_ADDR_INCR;

	writel(mac_addr_lo, hw->io + index + KS_ADD_ADDR_0_LO);
	writel(mac_addr_hi, hw->io + index + KS_ADD_ADDR_0_HI);
}

static void hw_set_add_addr(struct ksz_hw *hw)
{
	int i;

	for (i = 0; i < ADDITIONAL_ENTRIES; i++) {
		if (empty_addr(hw->address[i]))
			writel(0, hw->io + ADD_ADDR_INCR * i +
				KS_ADD_ADDR_0_HI);
		else
			hw_ena_add_addr(hw, i, hw->address[i]);
	}
}

static int hw_add_addr(struct ksz_hw *hw, u8 *mac_addr)
{
	int i;
	int j = ADDITIONAL_ENTRIES;

	if (!memcmp(hw->override_addr, mac_addr, MAC_ADDR_LEN))
		return 0;
	for (i = 0; i < hw->addr_list_size; i++) {
		if (!memcmp(hw->address[i], mac_addr, MAC_ADDR_LEN))
			return 0;
		if (ADDITIONAL_ENTRIES == j && empty_addr(hw->address[i]))
			j = i;
	}
	if (j < ADDITIONAL_ENTRIES) {
		memcpy(hw->address[j], mac_addr, MAC_ADDR_LEN);
		hw_ena_add_addr(hw, j, hw->address[j]);
		return 0;
	}
	return -1;
}

static int hw_del_addr(struct ksz_hw *hw, u8 *mac_addr)
{
	int i;

	for (i = 0; i < hw->addr_list_size; i++) {
		if (!memcmp(hw->address[i], mac_addr, MAC_ADDR_LEN)) {
			memset(hw->address[i], 0, MAC_ADDR_LEN);
			writel(0, hw->io + ADD_ADDR_INCR * i +
				KS_ADD_ADDR_0_HI);
			return 0;
		}
	}
	return -1;
}

/**
 * hw_clr_multicast - clear multicast addresses
 * @hw: 	The hardware instance.
 *
 * This routine removes all multicast addresses set in the hardware.
 */
static void hw_clr_multicast(struct ksz_hw *hw)
{
	int i;

	for (i = 0; i < HW_MULTICAST_SIZE; i++) {
		hw->multi_bits[i] = 0;

		writeb(0, hw->io + KS884X_MULTICAST_0_OFFSET + i);
	}
}

/**
 * hw_set_grp_addr - set multicast addresses
 * @hw: 	The hardware instance.
 *
 * This routine programs multicast addresses for the hardware to accept those
 * addresses.
 */
static void hw_set_grp_addr(struct ksz_hw *hw)
{
	int i;
	int index;
	int position;
	int value;

	memset(hw->multi_bits, 0, sizeof(u8) * HW_MULTICAST_SIZE);

	for (i = 0; i < hw->multi_list_size; i++) {
		position = (ether_crc(6, hw->multi_list[i]) >> 26) & 0x3f;
		index = position >> 3;
		value = 1 << (position & 7);
		hw->multi_bits[index] |= (u8) value;
	}

	for (i = 0; i < HW_MULTICAST_SIZE; i++)
		writeb(hw->multi_bits[i], hw->io + KS884X_MULTICAST_0_OFFSET +
			i);
}

/**
 * hw_set_multicast - enable or disable all multicast receiving
 * @hw: 	The hardware instance.
 * @multicast:	To turn on or off the all multicast feature.
 *
 * This routine enables/disables the hardware to accept all multicast packets.
 */
static void hw_set_multicast(struct ksz_hw *hw, u8 multicast)
{
	/* Stop receiving for reconfiguration. */
	hw_stop_rx(hw);

	if (multicast)
		hw->rx_cfg |= DMA_RX_ALL_MULTICAST;
	else
		hw->rx_cfg &= ~DMA_RX_ALL_MULTICAST;

	if (hw->enabled)
		hw_start_rx(hw);
}

/**
 * hw_set_promiscuous - enable or disable promiscuous receiving
 * @hw: 	The hardware instance.
 * @prom:	To turn on or off the promiscuous feature.
 *
 * This routine enables/disables the hardware to accept all packets.
 */
static void hw_set_promiscuous(struct ksz_hw *hw, u8 prom)
{
	/* Stop receiving for reconfiguration. */
	hw_stop_rx(hw);

	if (prom)
		hw->rx_cfg |= DMA_RX_PROMISCUOUS;
	else
		hw->rx_cfg &= ~DMA_RX_PROMISCUOUS;

	if (hw->enabled)
		hw_start_rx(hw);
}

/**
 * sw_enable - enable the switch
 * @hw: 	The hardware instance.
 * @enable:	The flag to enable or disable the switch
 *
 * This routine is used to enable/disable the switch in KSZ8842.
 */
static void sw_enable(struct ksz_hw *hw, int enable)
{
	int port;

	for (port = 0; port < SWITCH_PORT_NUM; port++) {
		if (hw->dev_count > 1) {
			/* Set port-base vlan membership with host port. */
			sw_cfg_port_base_vlan(hw, port,
				HOST_MASK | (1 << port));
			port_set_stp_state(hw, port, STP_STATE_DISABLED);
		} else {
			sw_cfg_port_base_vlan(hw, port, PORT_MASK);
			port_set_stp_state(hw, port, STP_STATE_FORWARDING);
		}
	}
	if (hw->dev_count > 1)
		port_set_stp_state(hw, SWITCH_PORT_NUM, STP_STATE_SIMPLE);
	else
		port_set_stp_state(hw, SWITCH_PORT_NUM, STP_STATE_FORWARDING);

	if (enable)
		enable = KS8842_START;
	writew(enable, hw->io + KS884X_CHIP_ID_OFFSET);
}

/**
 * sw_setup - setup the switch
 * @hw: 	The hardware instance.
 *
 * This routine setup the hardware switch engine for default operation.
 */
static void sw_setup(struct ksz_hw *hw)
{
	int port;

	sw_set_global_ctrl(hw);

	/* Enable switch broadcast storm protection at 10% percent rate. */
	sw_init_broad_storm(hw);
	hw_cfg_broad_storm(hw, BROADCAST_STORM_PROTECTION_RATE);
	for (port = 0; port < SWITCH_PORT_NUM; port++)
		sw_ena_broad_storm(hw, port);

	sw_init_prio(hw);

	sw_init_mirror(hw);

	sw_init_prio_rate(hw);

	sw_init_vlan(hw);

	if (hw->features & STP_SUPPORT)
		sw_init_stp(hw);
	if (!sw_chk(hw, KS8842_SWITCH_CTRL_1_OFFSET,
			SWITCH_TX_FLOW_CTRL | SWITCH_RX_FLOW_CTRL))
		hw->overrides |= PAUSE_FLOW_CTRL;
	sw_enable(hw, 1);
}

/**
 * ksz_start_timer - start kernel timer
 * @info:	Kernel timer information.
 * @time:	The time tick.
 *
 * This routine starts the kernel timer after the specified time tick.
 */
static void ksz_start_timer(struct ksz_timer_info *info, int time)
{
	info->cnt = 0;
	info->timer.expires = jiffies + time;
	add_timer(&info->timer);

	/* infinity */
	info->max = -1;
}

/**
 * ksz_stop_timer - stop kernel timer
 * @info:	Kernel timer information.
 *
 * This routine stops the kernel timer.
 */
static void ksz_stop_timer(struct ksz_timer_info *info)
{
	if (info->max) {
		info->max = 0;
		del_timer_sync(&info->timer);
	}
}

static void ksz_init_timer(struct ksz_timer_info *info, int period,
	void (*function)(unsigned long), void *data)
{
	info->max = 0;
	info->period = period;
	init_timer(&info->timer);
	info->timer.function = function;
	info->timer.data = (unsigned long) data;
}

static void ksz_update_timer(struct ksz_timer_info *info)
{
	++info->cnt;
	if (info->max > 0) {
		if (info->cnt < info->max) {
			info->timer.expires = jiffies + info->period;
			add_timer(&info->timer);
		} else
			info->max = 0;
	} else if (info->max < 0) {
		info->timer.expires = jiffies + info->period;
		add_timer(&info->timer);
	}
}

/**
 * ksz_alloc_soft_desc - allocate software descriptors
 * @desc_info:	Descriptor information structure.
 * @transmit:	Indication that descriptors are for transmit.
 *
 * This local function allocates software descriptors for manipulation in
 * memory.
 *
 * Return 0 if successful.
 */
static int ksz_alloc_soft_desc(struct ksz_desc_info *desc_info, int transmit)
{
	desc_info->ring = kmalloc(sizeof(struct ksz_desc) * desc_info->alloc,
		GFP_KERNEL);
	if (!desc_info->ring)
		return 1;
	memset((void *) desc_info->ring, 0,
		sizeof(struct ksz_desc) * desc_info->alloc);
	hw_init_desc(desc_info, transmit);
	return 0;
}

/**
 * ksz_alloc_desc - allocate hardware descriptors
 * @adapter:	Adapter information structure.
 *
 * This local function allocates hardware descriptors for receiving and
 * transmitting.
 *
 * Return 0 if successful.
 */
static int ksz_alloc_desc(struct dev_info *adapter)
{
	struct ksz_hw *hw = &adapter->hw;
	int offset;

	/* Allocate memory for RX & TX descriptors. */
	adapter->desc_pool.alloc_size =
		hw->rx_desc_info.size * hw->rx_desc_info.alloc +
		hw->tx_desc_info.size * hw->tx_desc_info.alloc +
		DESC_ALIGNMENT;

	adapter->desc_pool.alloc_virt =
		pci_alloc_consistent(
			adapter->pdev, adapter->desc_pool.alloc_size,
			&adapter->desc_pool.dma_addr);
	if (adapter->desc_pool.alloc_virt == NULL) {
		adapter->desc_pool.alloc_size = 0;
		return 1;
	}
	memset(adapter->desc_pool.alloc_virt, 0, adapter->desc_pool.alloc_size);

	/* Align to the next cache line boundary. */
	offset = (((ulong) adapter->desc_pool.alloc_virt % DESC_ALIGNMENT) ?
		(DESC_ALIGNMENT -
		((ulong) adapter->desc_pool.alloc_virt % DESC_ALIGNMENT)) : 0);
	adapter->desc_pool.virt = adapter->desc_pool.alloc_virt + offset;
	adapter->desc_pool.phys = adapter->desc_pool.dma_addr + offset;

	/* Allocate receive/transmit descriptors. */
	hw->rx_desc_info.ring_virt = (struct ksz_hw_desc *)
		adapter->desc_pool.virt;
	hw->rx_desc_info.ring_phys = adapter->desc_pool.phys;
	offset = hw->rx_desc_info.alloc * hw->rx_desc_info.size;
	hw->tx_desc_info.ring_virt = (struct ksz_hw_desc *)
		(adapter->desc_pool.virt + offset);
	hw->tx_desc_info.ring_phys = adapter->desc_pool.phys + offset;

	if (ksz_alloc_soft_desc(&hw->rx_desc_info, 0))
		return 1;
	if (ksz_alloc_soft_desc(&hw->tx_desc_info, 1))
		return 1;

	return 0;
}

/**
 * free_dma_buf - release DMA buffer resources
 * @adapter:	Adapter information structure.
 *
 * This routine is just a helper function to release the DMA buffer resources.
 */
static void free_dma_buf(struct dev_info *adapter, struct ksz_dma_buf *dma_buf,
	int direction)
{
	pci_unmap_single(adapter->pdev, dma_buf->dma, dma_buf->len, direction);
	dev_kfree_skb(dma_buf->skb);
	dma_buf->skb = NULL;
	dma_buf->dma = 0;
}

/**
 * ksz_init_rx_buffers - initialize receive descriptors
 * @adapter:	Adapter information structure.
 *
 * This routine initializes DMA buffers for receiving.
 */
static void ksz_init_rx_buffers(struct dev_info *adapter)
{
	int i;
	struct ksz_desc *desc;
	struct ksz_dma_buf *dma_buf;
	struct ksz_hw *hw = &adapter->hw;
	struct ksz_desc_info *info = &hw->rx_desc_info;

	for (i = 0; i < hw->rx_desc_info.alloc; i++) {
		get_rx_pkt(info, &desc);

		dma_buf = DMA_BUFFER(desc);
		if (dma_buf->skb && dma_buf->len != adapter->mtu)
			free_dma_buf(adapter, dma_buf, PCI_DMA_FROMDEVICE);
		dma_buf->len = adapter->mtu;
		if (!dma_buf->skb)
			dma_buf->skb = alloc_skb(dma_buf->len, GFP_ATOMIC);
		if (dma_buf->skb && !dma_buf->dma) {
			dma_buf->skb->dev = adapter->dev;
			dma_buf->dma = pci_map_single(
				adapter->pdev,
				skb_tail_pointer(dma_buf->skb),
				dma_buf->len,
				PCI_DMA_FROMDEVICE);
		}

		/* Set descriptor. */
		set_rx_buf(desc, dma_buf->dma);
		set_rx_len(desc, dma_buf->len);
		release_desc(desc);
	}
}

/**
 * ksz_alloc_mem - allocate memory for hardware descriptors
 * @adapter:	Adapter information structure.
 *
 * This function allocates memory for use by hardware descriptors for receiving
 * and transmitting.
 *
 * Return 0 if successful.
 */
static int ksz_alloc_mem(struct dev_info *adapter)
{
	struct ksz_hw *hw = &adapter->hw;

	/* Determine the number of receive and transmit descriptors. */
	hw->rx_desc_info.alloc = NUM_OF_RX_DESC;
	hw->tx_desc_info.alloc = NUM_OF_TX_DESC;

	/* Determine how many descriptors to skip transmit interrupt. */
	hw->tx_int_cnt = 0;
	hw->tx_int_mask = NUM_OF_TX_DESC / 4;
	if (hw->tx_int_mask > 8)
		hw->tx_int_mask = 8;
	while (hw->tx_int_mask) {
		hw->tx_int_cnt++;
		hw->tx_int_mask >>= 1;
	}
	if (hw->tx_int_cnt) {
		hw->tx_int_mask = (1 << (hw->tx_int_cnt - 1)) - 1;
		hw->tx_int_cnt = 0;
	}

	/* Determine the descriptor size. */
	hw->rx_desc_info.size =
		(((sizeof(struct ksz_hw_desc) + DESC_ALIGNMENT - 1) /
		DESC_ALIGNMENT) * DESC_ALIGNMENT);
	hw->tx_desc_info.size =
		(((sizeof(struct ksz_hw_desc) + DESC_ALIGNMENT - 1) /
		DESC_ALIGNMENT) * DESC_ALIGNMENT);
	if (hw->rx_desc_info.size != sizeof(struct ksz_hw_desc))
		pr_alert("Hardware descriptor size not right!\n");
	ksz_check_desc_num(&hw->rx_desc_info);
	ksz_check_desc_num(&hw->tx_desc_info);

	/* Allocate descriptors. */
	if (ksz_alloc_desc(adapter))
		return 1;

	return 0;
}

/**
 * ksz_free_desc - free software and hardware descriptors
 * @adapter:	Adapter information structure.
 *
 * This local routine frees the software and hardware descriptors allocated by
 * ksz_alloc_desc().
 */
static void ksz_free_desc(struct dev_info *adapter)
{
	struct ksz_hw *hw = &adapter->hw;

	/* Reset descriptor. */
	hw->rx_desc_info.ring_virt = NULL;
	hw->tx_desc_info.ring_virt = NULL;
	hw->rx_desc_info.ring_phys = 0;
	hw->tx_desc_info.ring_phys = 0;

	/* Free memory. */
	if (adapter->desc_pool.alloc_virt)
		pci_free_consistent(
			adapter->pdev,
			adapter->desc_pool.alloc_size,
			adapter->desc_pool.alloc_virt,
			adapter->desc_pool.dma_addr);

	/* Reset resource pool. */
	adapter->desc_pool.alloc_size = 0;
	adapter->desc_pool.alloc_virt = NULL;

	kfree(hw->rx_desc_info.ring);
	hw->rx_desc_info.ring = NULL;
	kfree(hw->tx_desc_info.ring);
	hw->tx_desc_info.ring = NULL;
}

/**
 * ksz_free_buffers - free buffers used in the descriptors
 * @adapter:	Adapter information structure.
 * @desc_info:	Descriptor information structure.
 *
 * This local routine frees buffers used in the DMA buffers.
 */
static void ksz_free_buffers(struct dev_info *adapter,
	struct ksz_desc_info *desc_info, int direction)
{
	int i;
	struct ksz_dma_buf *dma_buf;
	struct ksz_desc *desc = desc_info->ring;

	for (i = 0; i < desc_info->alloc; i++) {
		dma_buf = DMA_BUFFER(desc);
		if (dma_buf->skb)
			free_dma_buf(adapter, dma_buf, direction);
		desc++;
	}
}

/**
 * ksz_free_mem - free all resources used by descriptors
 * @adapter:	Adapter information structure.
 *
 * This local routine frees all the resources allocated by ksz_alloc_mem().
 */
static void ksz_free_mem(struct dev_info *adapter)
{
	/* Free transmit buffers. */
	ksz_free_buffers(adapter, &adapter->hw.tx_desc_info,
		PCI_DMA_TODEVICE);

	/* Free receive buffers. */
	ksz_free_buffers(adapter, &adapter->hw.rx_desc_info,
		PCI_DMA_FROMDEVICE);

	/* Free descriptors. */
	ksz_free_desc(adapter);
}

static void get_mib_counters(struct ksz_hw *hw, int first, int cnt,
	u64 *counter)
{
	int i;
	int mib;
	int port;
	struct ksz_port_mib *port_mib;

	memset(counter, 0, sizeof(u64) * TOTAL_PORT_COUNTER_NUM);
	for (i = 0, port = first; i < cnt; i++, port++) {
		port_mib = &hw->port_mib[port];
		for (mib = port_mib->mib_start; mib < hw->mib_cnt; mib++)
			counter[mib] += port_mib->counter[mib];
	}
}

/**
 * send_packet - send packet
 * @skb:	Socket buffer.
 * @dev:	Network device.
 *
 * This routine is used to send a packet out to the network.
 */
static void send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct ksz_desc *desc;
	struct ksz_desc *first;
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;
	struct ksz_desc_info *info = &hw->tx_desc_info;
	struct ksz_dma_buf *dma_buf;
	int len;
	int last_frag = skb_shinfo(skb)->nr_frags;

	/*
	 * KSZ8842 with multiple device interfaces needs to be told which port
	 * to send.
	 */
	if (hw->dev_count > 1)
		hw->dst_ports = 1 << priv->port.first_port;

	/* Hardware will pad the length to 60. */
	len = skb->len;

	/* Remember the very first descriptor. */
	first = info->cur;
	desc = first;

	dma_buf = DMA_BUFFER(desc);
	if (last_frag) {
		int frag;
		skb_frag_t *this_frag;

		dma_buf->len = skb_headlen(skb);

		dma_buf->dma = pci_map_single(
			hw_priv->pdev, skb->data, dma_buf->len,
			PCI_DMA_TODEVICE);
		set_tx_buf(desc, dma_buf->dma);
		set_tx_len(desc, dma_buf->len);

		frag = 0;
		do {
			this_frag = &skb_shinfo(skb)->frags[frag];

			/* Get a new descriptor. */
			get_tx_pkt(info, &desc);

			/* Keep track of descriptors used so far. */
			++hw->tx_int_cnt;

			dma_buf = DMA_BUFFER(desc);
			dma_buf->len = this_frag->size;

			dma_buf->dma = pci_map_single(
				hw_priv->pdev,
				page_address(this_frag->page) +
				this_frag->page_offset,
				dma_buf->len,
				PCI_DMA_TODEVICE);
			set_tx_buf(desc, dma_buf->dma);
			set_tx_len(desc, dma_buf->len);

			frag++;
			if (frag == last_frag)
				break;

			/* Do not release the last descriptor here. */
			release_desc(desc);
		} while (1);

		/* current points to the last descriptor. */
		info->cur = desc;

		/* Release the first descriptor. */
		release_desc(first);
	} else {
		dma_buf->len = len;

		dma_buf->dma = pci_map_single(
			hw_priv->pdev, skb->data, dma_buf->len,
			PCI_DMA_TODEVICE);
		set_tx_buf(desc, dma_buf->dma);
		set_tx_len(desc, dma_buf->len);
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		(desc)->sw.buf.tx.csum_gen_tcp = 1;
		(desc)->sw.buf.tx.csum_gen_udp = 1;
	}

	/*
	 * The last descriptor holds the packet so that it can be returned to
	 * network subsystem after all descriptors are transmitted.
	 */
	dma_buf->skb = skb;

	hw_send_pkt(hw);

	/* Update transmit statistics. */
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += len;
}

/**
 * transmit_cleanup - clean up transmit descriptors
 * @dev:	Network device.
 *
 * This routine is called to clean up the transmitted buffers.
 */
static void transmit_cleanup(struct dev_info *hw_priv, int normal)
{
	int last;
	union desc_stat status;
	struct ksz_hw *hw = &hw_priv->hw;
	struct ksz_desc_info *info = &hw->tx_desc_info;
	struct ksz_desc *desc;
	struct ksz_dma_buf *dma_buf;
	struct net_device *dev = NULL;

	spin_lock(&hw_priv->hwlock);
	last = info->last;

	while (info->avail < info->alloc) {
		/* Get next descriptor which is not hardware owned. */
		desc = &info->ring[last];
		status.data = le32_to_cpu(desc->phw->ctrl.data);
		if (status.tx.hw_owned) {
			if (normal)
				break;
			else
				reset_desc(desc, status);
		}

		dma_buf = DMA_BUFFER(desc);
		pci_unmap_single(
			hw_priv->pdev, dma_buf->dma, dma_buf->len,
			PCI_DMA_TODEVICE);

		/* This descriptor contains the last buffer in the packet. */
		if (dma_buf->skb) {
			dev = dma_buf->skb->dev;

			/* Release the packet back to network subsystem. */
			dev_kfree_skb_irq(dma_buf->skb);
			dma_buf->skb = NULL;
		}

		/* Free the transmitted descriptor. */
		last++;
		last &= info->mask;
		info->avail++;
	}
	info->last = last;
	spin_unlock(&hw_priv->hwlock);

	/* Notify the network subsystem that the packet has been sent. */
	if (dev)
		dev->trans_start = jiffies;
}

/**
 * transmit_done - transmit done processing
 * @dev:	Network device.
 *
 * This routine is called when the transmit interrupt is triggered, indicating
 * either a packet is sent successfully or there are transmit errors.
 */
static void tx_done(struct dev_info *hw_priv)
{
	struct ksz_hw *hw = &hw_priv->hw;
	int port;

	transmit_cleanup(hw_priv, 1);

	for (port = 0; port < hw->dev_count; port++) {
		struct net_device *dev = hw->port_info[port].pdev;

		if (netif_running(dev) && netif_queue_stopped(dev))
			netif_wake_queue(dev);
	}
}

static inline void copy_old_skb(struct sk_buff *old, struct sk_buff *skb)
{
	skb->dev = old->dev;
	skb->protocol = old->protocol;
	skb->ip_summed = old->ip_summed;
	skb->csum = old->csum;
	skb_set_network_header(skb, ETH_HLEN);

	dev_kfree_skb(old);
}

/**
 * netdev_tx - send out packet
 * @skb:	Socket buffer.
 * @dev:	Network device.
 *
 * This function is used by the upper network layer to send out a packet.
 *
 * Return 0 if successful; otherwise an error code indicating failure.
 */
static netdev_tx_t netdev_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;
	int left;
	int num = 1;
	int rc = 0;

	if (hw->features & SMALL_PACKET_TX_BUG) {
		struct sk_buff *org_skb = skb;

		if (skb->len <= 48) {
			if (skb_end_pointer(skb) - skb->data >= 50) {
				memset(&skb->data[skb->len], 0, 50 - skb->len);
				skb->len = 50;
			} else {
				skb = dev_alloc_skb(50);
				if (!skb)
					return NETDEV_TX_BUSY;
				memcpy(skb->data, org_skb->data, org_skb->len);
				memset(&skb->data[org_skb->len], 0,
					50 - org_skb->len);
				skb->len = 50;
				copy_old_skb(org_skb, skb);
			}
		}
	}

	spin_lock_irq(&hw_priv->hwlock);

	num = skb_shinfo(skb)->nr_frags + 1;
	left = hw_alloc_pkt(hw, skb->len, num);
	if (left) {
		if (left < num ||
				((hw->features & IPV6_CSUM_GEN_HACK) &&
				(CHECKSUM_PARTIAL == skb->ip_summed) &&
				(ETH_P_IPV6 == htons(skb->protocol)))) {
			struct sk_buff *org_skb = skb;

			skb = dev_alloc_skb(org_skb->len);
			if (!skb) {
				rc = NETDEV_TX_BUSY;
				goto unlock;
			}
			skb_copy_and_csum_dev(org_skb, skb->data);
			org_skb->ip_summed = 0;
			skb->len = org_skb->len;
			copy_old_skb(org_skb, skb);
		}
		send_packet(skb, dev);
		if (left <= num)
			netif_stop_queue(dev);
	} else {
		/* Stop the transmit queue until packet is allocated. */
		netif_stop_queue(dev);
		rc = NETDEV_TX_BUSY;
	}
unlock:
	spin_unlock_irq(&hw_priv->hwlock);

	return rc;
}

/**
 * netdev_tx_timeout - transmit timeout processing
 * @dev:	Network device.
 *
 * This routine is called when the transmit timer expires.  That indicates the
 * hardware is not running correctly because transmit interrupts are not
 * triggered to free up resources so that the transmit routine can continue
 * sending out packets.  The hardware is reset to correct the problem.
 */
static void netdev_tx_timeout(struct net_device *dev)
{
	static unsigned long last_reset;

	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;
	int port;

	if (hw->dev_count > 1) {
		/*
		 * Only reset the hardware if time between calls is long
		 * enough.
		 */
		if (jiffies - last_reset <= dev->watchdog_timeo)
			hw_priv = NULL;
	}

	last_reset = jiffies;
	if (hw_priv) {
		hw_dis_intr(hw);
		hw_disable(hw);

		transmit_cleanup(hw_priv, 0);
		hw_reset_pkts(&hw->rx_desc_info);
		hw_reset_pkts(&hw->tx_desc_info);
		ksz_init_rx_buffers(hw_priv);

		hw_reset(hw);

		hw_set_desc_base(hw,
			hw->tx_desc_info.ring_phys,
			hw->rx_desc_info.ring_phys);
		hw_set_addr(hw);
		if (hw->all_multi)
			hw_set_multicast(hw, hw->all_multi);
		else if (hw->multi_list_size)
			hw_set_grp_addr(hw);

		if (hw->dev_count > 1) {
			hw_set_add_addr(hw);
			for (port = 0; port < SWITCH_PORT_NUM; port++) {
				struct net_device *port_dev;

				port_set_stp_state(hw, port,
					STP_STATE_DISABLED);

				port_dev = hw->port_info[port].pdev;
				if (netif_running(port_dev))
					port_set_stp_state(hw, port,
						STP_STATE_SIMPLE);
			}
		}

		hw_enable(hw);
		hw_ena_intr(hw);
	}

	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

static inline void csum_verified(struct sk_buff *skb)
{
	unsigned short protocol;
	struct iphdr *iph;

	protocol = skb->protocol;
	skb_reset_network_header(skb);
	iph = (struct iphdr *) skb_network_header(skb);
	if (protocol == htons(ETH_P_8021Q)) {
		protocol = iph->tot_len;
		skb_set_network_header(skb, VLAN_HLEN);
		iph = (struct iphdr *) skb_network_header(skb);
	}
	if (protocol == htons(ETH_P_IP)) {
		if (iph->protocol == IPPROTO_TCP)
			skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
}

static inline int rx_proc(struct net_device *dev, struct ksz_hw* hw,
	struct ksz_desc *desc, union desc_stat status)
{
	int packet_len;
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_dma_buf *dma_buf;
	struct sk_buff *skb;
	int rx_status;

	/* Received length includes 4-byte CRC. */
	packet_len = status.rx.frame_len - 4;

	dma_buf = DMA_BUFFER(desc);
	pci_dma_sync_single_for_cpu(
		hw_priv->pdev, dma_buf->dma, packet_len + 4,
		PCI_DMA_FROMDEVICE);

	do {
		/* skb->data != skb->head */
		skb = dev_alloc_skb(packet_len + 2);
		if (!skb) {
			dev->stats.rx_dropped++;
			return -ENOMEM;
		}

		/*
		 * Align socket buffer in 4-byte boundary for better
		 * performance.
		 */
		skb_reserve(skb, 2);

		memcpy(skb_put(skb, packet_len),
			dma_buf->skb->data, packet_len);
	} while (0);

	skb->protocol = eth_type_trans(skb, dev);

	if (hw->rx_cfg & (DMA_RX_CSUM_UDP | DMA_RX_CSUM_TCP))
		csum_verified(skb);

	/* Update receive statistics. */
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += packet_len;

	/* Notify upper layer for received packet. */
	rx_status = netif_rx(skb);

	return 0;
}

static int dev_rcv_packets(struct dev_info *hw_priv)
{
	int next;
	union desc_stat status;
	struct ksz_hw *hw = &hw_priv->hw;
	struct net_device *dev = hw->port_info[0].pdev;
	struct ksz_desc_info *info = &hw->rx_desc_info;
	int left = info->alloc;
	struct ksz_desc *desc;
	int received = 0;

	next = info->next;
	while (left--) {
		/* Get next descriptor which is not hardware owned. */
		desc = &info->ring[next];
		status.data = le32_to_cpu(desc->phw->ctrl.data);
		if (status.rx.hw_owned)
			break;

		/* Status valid only when last descriptor bit is set. */
		if (status.rx.last_desc && status.rx.first_desc) {
			if (rx_proc(dev, hw, desc, status))
				goto release_packet;
			received++;
		}

release_packet:
		release_desc(desc);
		next++;
		next &= info->mask;
	}
	info->next = next;

	return received;
}

static int port_rcv_packets(struct dev_info *hw_priv)
{
	int next;
	union desc_stat status;
	struct ksz_hw *hw = &hw_priv->hw;
	struct net_device *dev = hw->port_info[0].pdev;
	struct ksz_desc_info *info = &hw->rx_desc_info;
	int left = info->alloc;
	struct ksz_desc *desc;
	int received = 0;

	next = info->next;
	while (left--) {
		/* Get next descriptor which is not hardware owned. */
		desc = &info->ring[next];
		status.data = le32_to_cpu(desc->phw->ctrl.data);
		if (status.rx.hw_owned)
			break;

		if (hw->dev_count > 1) {
			/* Get received port number. */
			int p = HW_TO_DEV_PORT(status.rx.src_port);

			dev = hw->port_info[p].pdev;
			if (!netif_running(dev))
				goto release_packet;
		}

		/* Status valid only when last descriptor bit is set. */
		if (status.rx.last_desc && status.rx.first_desc) {
			if (rx_proc(dev, hw, desc, status))
				goto release_packet;
			received++;
		}

release_packet:
		release_desc(desc);
		next++;
		next &= info->mask;
	}
	info->next = next;

	return received;
}

static int dev_rcv_special(struct dev_info *hw_priv)
{
	int next;
	union desc_stat status;
	struct ksz_hw *hw = &hw_priv->hw;
	struct net_device *dev = hw->port_info[0].pdev;
	struct ksz_desc_info *info = &hw->rx_desc_info;
	int left = info->alloc;
	struct ksz_desc *desc;
	int received = 0;

	next = info->next;
	while (left--) {
		/* Get next descriptor which is not hardware owned. */
		desc = &info->ring[next];
		status.data = le32_to_cpu(desc->phw->ctrl.data);
		if (status.rx.hw_owned)
			break;

		if (hw->dev_count > 1) {
			/* Get received port number. */
			int p = HW_TO_DEV_PORT(status.rx.src_port);

			dev = hw->port_info[p].pdev;
			if (!netif_running(dev))
				goto release_packet;
		}

		/* Status valid only when last descriptor bit is set. */
		if (status.rx.last_desc && status.rx.first_desc) {
			/*
			 * Receive without error.  With receive errors
			 * disabled, packets with receive errors will be
			 * dropped, so no need to check the error bit.
			 */
			if (!status.rx.error || (status.data &
					KS_DESC_RX_ERROR_COND) ==
					KS_DESC_RX_ERROR_TOO_LONG) {
				if (rx_proc(dev, hw, desc, status))
					goto release_packet;
				received++;
			} else {
				struct dev_priv *priv = netdev_priv(dev);

				/* Update receive error statistics. */
				priv->port.counter[OID_COUNTER_RCV_ERROR]++;
			}
		}

release_packet:
		release_desc(desc);
		next++;
		next &= info->mask;
	}
	info->next = next;

	return received;
}

static void rx_proc_task(unsigned long data)
{
	struct dev_info *hw_priv = (struct dev_info *) data;
	struct ksz_hw *hw = &hw_priv->hw;

	if (!hw->enabled)
		return;
	if (unlikely(!hw_priv->dev_rcv(hw_priv))) {

		/* In case receive process is suspended because of overrun. */
		hw_resume_rx(hw);

		/* tasklets are interruptible. */
		spin_lock_irq(&hw_priv->hwlock);
		hw_turn_on_intr(hw, KS884X_INT_RX_MASK);
		spin_unlock_irq(&hw_priv->hwlock);
	} else {
		hw_ack_intr(hw, KS884X_INT_RX);
		tasklet_schedule(&hw_priv->rx_tasklet);
	}
}

static void tx_proc_task(unsigned long data)
{
	struct dev_info *hw_priv = (struct dev_info *) data;
	struct ksz_hw *hw = &hw_priv->hw;

	hw_ack_intr(hw, KS884X_INT_TX_MASK);

	tx_done(hw_priv);

	/* tasklets are interruptible. */
	spin_lock_irq(&hw_priv->hwlock);
	hw_turn_on_intr(hw, KS884X_INT_TX);
	spin_unlock_irq(&hw_priv->hwlock);
}

static inline void handle_rx_stop(struct ksz_hw *hw)
{
	/* Receive just has been stopped. */
	if (0 == hw->rx_stop)
		hw->intr_mask &= ~KS884X_INT_RX_STOPPED;
	else if (hw->rx_stop > 1) {
		if (hw->enabled && (hw->rx_cfg & DMA_RX_ENABLE)) {
			hw_start_rx(hw);
		} else {
			hw->intr_mask &= ~KS884X_INT_RX_STOPPED;
			hw->rx_stop = 0;
		}
	} else
		/* Receive just has been started. */
		hw->rx_stop++;
}

/**
 * netdev_intr - interrupt handling
 * @irq:	Interrupt number.
 * @dev_id:	Network device.
 *
 * This function is called by upper network layer to signal interrupt.
 *
 * Return IRQ_HANDLED if interrupt is handled.
 */
static irqreturn_t netdev_intr(int irq, void *dev_id)
{
	uint int_enable = 0;
	struct net_device *dev = (struct net_device *) dev_id;
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;

	hw_read_intr(hw, &int_enable);

	/* Not our interrupt! */
	if (!int_enable)
		return IRQ_NONE;

	do {
		hw_ack_intr(hw, int_enable);
		int_enable &= hw->intr_mask;

		if (unlikely(int_enable & KS884X_INT_TX_MASK)) {
			hw_dis_intr_bit(hw, KS884X_INT_TX_MASK);
			tasklet_schedule(&hw_priv->tx_tasklet);
		}

		if (likely(int_enable & KS884X_INT_RX)) {
			hw_dis_intr_bit(hw, KS884X_INT_RX);
			tasklet_schedule(&hw_priv->rx_tasklet);
		}

		if (unlikely(int_enable & KS884X_INT_RX_OVERRUN)) {
			dev->stats.rx_fifo_errors++;
			hw_resume_rx(hw);
		}

		if (unlikely(int_enable & KS884X_INT_PHY)) {
			struct ksz_port *port = &priv->port;

			hw->features |= LINK_INT_WORKING;
			port_get_link_speed(port);
		}

		if (unlikely(int_enable & KS884X_INT_RX_STOPPED)) {
			handle_rx_stop(hw);
			break;
		}

		if (unlikely(int_enable & KS884X_INT_TX_STOPPED)) {
			u32 data;

			hw->intr_mask &= ~KS884X_INT_TX_STOPPED;
			pr_info("Tx stopped\n");
			data = readl(hw->io + KS_DMA_TX_CTRL);
			if (!(data & DMA_TX_ENABLE))
				pr_info("Tx disabled\n");
			break;
		}
	} while (0);

	hw_ena_intr(hw);

	return IRQ_HANDLED;
}

/*
 * Linux network device functions
 */

static unsigned long next_jiffies;

#ifdef CONFIG_NET_POLL_CONTROLLER
static void netdev_netpoll(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;

	hw_dis_intr(&hw_priv->hw);
	netdev_intr(dev->irq, dev);
}
#endif

static void bridge_change(struct ksz_hw *hw)
{
	int port;
	u8  member;
	struct ksz_switch *sw = hw->ksz_switch;

	/* No ports in forwarding state. */
	if (!sw->member) {
		port_set_stp_state(hw, SWITCH_PORT_NUM, STP_STATE_SIMPLE);
		sw_block_addr(hw);
	}
	for (port = 0; port < SWITCH_PORT_NUM; port++) {
		if (STP_STATE_FORWARDING == sw->port_cfg[port].stp_state)
			member = HOST_MASK | sw->member;
		else
			member = HOST_MASK | (1 << port);
		if (member != sw->port_cfg[port].member)
			sw_cfg_port_base_vlan(hw, port, member);
	}
}

/**
 * netdev_close - close network device
 * @dev:	Network device.
 *
 * This function process the close operation of network device.  This is caused
 * by the user command "ifconfig ethX down."
 *
 * Return 0 if successful; otherwise an error code indicating failure.
 */
static int netdev_close(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_port *port = &priv->port;
	struct ksz_hw *hw = &hw_priv->hw;
	int pi;

	netif_stop_queue(dev);

	ksz_stop_timer(&priv->monitor_timer_info);

	/* Need to shut the port manually in multiple device interfaces mode. */
	if (hw->dev_count > 1) {
		port_set_stp_state(hw, port->first_port, STP_STATE_DISABLED);

		/* Port is closed.  Need to change bridge setting. */
		if (hw->features & STP_SUPPORT) {
			pi = 1 << port->first_port;
			if (hw->ksz_switch->member & pi) {
				hw->ksz_switch->member &= ~pi;
				bridge_change(hw);
			}
		}
	}
	if (port->first_port > 0)
		hw_del_addr(hw, dev->dev_addr);
	if (!hw_priv->wol_enable)
		port_set_power_saving(port, true);

	if (priv->multicast)
		--hw->all_multi;
	if (priv->promiscuous)
		--hw->promiscuous;

	hw_priv->opened--;
	if (!(hw_priv->opened)) {
		ksz_stop_timer(&hw_priv->mib_timer_info);
		flush_work(&hw_priv->mib_read);

		hw_dis_intr(hw);
		hw_disable(hw);
		hw_clr_multicast(hw);

		/* Delay for receive task to stop scheduling itself. */
		msleep(2000 / HZ);

		tasklet_disable(&hw_priv->rx_tasklet);
		tasklet_disable(&hw_priv->tx_tasklet);
		free_irq(dev->irq, hw_priv->dev);

		transmit_cleanup(hw_priv, 0);
		hw_reset_pkts(&hw->rx_desc_info);
		hw_reset_pkts(&hw->tx_desc_info);

		/* Clean out static MAC table when the switch is shutdown. */
		if (hw->features & STP_SUPPORT)
			sw_clr_sta_mac_table(hw);
	}

	return 0;
}

static void hw_cfg_huge_frame(struct dev_info *hw_priv, struct ksz_hw *hw)
{
	if (hw->ksz_switch) {
		u32 data;

		data = readw(hw->io + KS8842_SWITCH_CTRL_2_OFFSET);
		if (hw->features & RX_HUGE_FRAME)
			data |= SWITCH_HUGE_PACKET;
		else
			data &= ~SWITCH_HUGE_PACKET;
		writew(data, hw->io + KS8842_SWITCH_CTRL_2_OFFSET);
	}
	if (hw->features & RX_HUGE_FRAME) {
		hw->rx_cfg |= DMA_RX_ERROR;
		hw_priv->dev_rcv = dev_rcv_special;
	} else {
		hw->rx_cfg &= ~DMA_RX_ERROR;
		if (hw->dev_count > 1)
			hw_priv->dev_rcv = port_rcv_packets;
		else
			hw_priv->dev_rcv = dev_rcv_packets;
	}
}

static int prepare_hardware(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;
	int rc = 0;

	/* Remember the network device that requests interrupts. */
	hw_priv->dev = dev;
	rc = request_irq(dev->irq, netdev_intr, IRQF_SHARED, dev->name, dev);
	if (rc)
		return rc;
	tasklet_enable(&hw_priv->rx_tasklet);
	tasklet_enable(&hw_priv->tx_tasklet);

	hw->promiscuous = 0;
	hw->all_multi = 0;
	hw->multi_list_size = 0;

	hw_reset(hw);

	hw_set_desc_base(hw,
		hw->tx_desc_info.ring_phys, hw->rx_desc_info.ring_phys);
	hw_set_addr(hw);
	hw_cfg_huge_frame(hw_priv, hw);
	ksz_init_rx_buffers(hw_priv);
	return 0;
}

static void set_media_state(struct net_device *dev, int media_state)
{
	struct dev_priv *priv = netdev_priv(dev);

	if (media_state == priv->media_state)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);
	netif_info(priv, link, dev, "link %s\n",
		   media_state == priv->media_state ? "on" : "off");
}

/**
 * netdev_open - open network device
 * @dev:	Network device.
 *
 * This function process the open operation of network device.  This is caused
 * by the user command "ifconfig ethX up."
 *
 * Return 0 if successful; otherwise an error code indicating failure.
 */
static int netdev_open(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;
	struct ksz_port *port = &priv->port;
	int i;
	int p;
	int rc = 0;

	priv->multicast = 0;
	priv->promiscuous = 0;

	/* Reset device statistics. */
	memset(&dev->stats, 0, sizeof(struct net_device_stats));
	memset((void *) port->counter, 0,
		(sizeof(u64) * OID_COUNTER_LAST));

	if (!(hw_priv->opened)) {
		rc = prepare_hardware(dev);
		if (rc)
			return rc;
		for (i = 0; i < hw->mib_port_cnt; i++) {
			if (next_jiffies < jiffies)
				next_jiffies = jiffies + HZ * 2;
			else
				next_jiffies += HZ * 1;
			hw_priv->counter[i].time = next_jiffies;
			hw->port_mib[i].state = media_disconnected;
			port_init_cnt(hw, i);
		}
		if (hw->ksz_switch)
			hw->port_mib[HOST_PORT].state = media_connected;
		else {
			hw_add_wol_bcast(hw);
			hw_cfg_wol_pme(hw, 0);
			hw_clr_wol_pme_status(&hw_priv->hw);
		}
	}
	port_set_power_saving(port, false);

	for (i = 0, p = port->first_port; i < port->port_cnt; i++, p++) {
		/*
		 * Initialize to invalid value so that link detection
		 * is done.
		 */
		hw->port_info[p].partner = 0xFF;
		hw->port_info[p].state = media_disconnected;
	}

	/* Need to open the port in multiple device interfaces mode. */
	if (hw->dev_count > 1) {
		port_set_stp_state(hw, port->first_port, STP_STATE_SIMPLE);
		if (port->first_port > 0)
			hw_add_addr(hw, dev->dev_addr);
	}

	port_get_link_speed(port);
	if (port->force_link)
		port_force_link_speed(port);
	else
		port_set_link_speed(port);

	if (!(hw_priv->opened)) {
		hw_setup_intr(hw);
		hw_enable(hw);
		hw_ena_intr(hw);

		if (hw->mib_port_cnt)
			ksz_start_timer(&hw_priv->mib_timer_info,
				hw_priv->mib_timer_info.period);
	}

	hw_priv->opened++;

	ksz_start_timer(&priv->monitor_timer_info,
		priv->monitor_timer_info.period);

	priv->media_state = port->linked->state;

	set_media_state(dev, media_connected);
	netif_start_queue(dev);

	return 0;
}

/* RX errors = rx_errors */
/* RX dropped = rx_dropped */
/* RX overruns = rx_fifo_errors */
/* RX frame = rx_crc_errors + rx_frame_errors + rx_length_errors */
/* TX errors = tx_errors */
/* TX dropped = tx_dropped */
/* TX overruns = tx_fifo_errors */
/* TX carrier = tx_aborted_errors + tx_carrier_errors + tx_window_errors */
/* collisions = collisions */

/**
 * netdev_query_statistics - query network device statistics
 * @dev:	Network device.
 *
 * This function returns the statistics of the network device.  The device
 * needs not be opened.
 *
 * Return network device statistics.
 */
static struct net_device_stats *netdev_query_statistics(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct ksz_port *port = &priv->port;
	struct ksz_hw *hw = &priv->adapter->hw;
	struct ksz_port_mib *mib;
	int i;
	int p;

	dev->stats.rx_errors = port->counter[OID_COUNTER_RCV_ERROR];
	dev->stats.tx_errors = port->counter[OID_COUNTER_XMIT_ERROR];

	/* Reset to zero to add count later. */
	dev->stats.multicast = 0;
	dev->stats.collisions = 0;
	dev->stats.rx_length_errors = 0;
	dev->stats.rx_crc_errors = 0;
	dev->stats.rx_frame_errors = 0;
	dev->stats.tx_window_errors = 0;

	for (i = 0, p = port->first_port; i < port->mib_port_cnt; i++, p++) {
		mib = &hw->port_mib[p];

		dev->stats.multicast += (unsigned long)
			mib->counter[MIB_COUNTER_RX_MULTICAST];

		dev->stats.collisions += (unsigned long)
			mib->counter[MIB_COUNTER_TX_TOTAL_COLLISION];

		dev->stats.rx_length_errors += (unsigned long)(
			mib->counter[MIB_COUNTER_RX_UNDERSIZE] +
			mib->counter[MIB_COUNTER_RX_FRAGMENT] +
			mib->counter[MIB_COUNTER_RX_OVERSIZE] +
			mib->counter[MIB_COUNTER_RX_JABBER]);
		dev->stats.rx_crc_errors += (unsigned long)
			mib->counter[MIB_COUNTER_RX_CRC_ERR];
		dev->stats.rx_frame_errors += (unsigned long)(
			mib->counter[MIB_COUNTER_RX_ALIGNMENT_ERR] +
			mib->counter[MIB_COUNTER_RX_SYMBOL_ERR]);

		dev->stats.tx_window_errors += (unsigned long)
			mib->counter[MIB_COUNTER_TX_LATE_COLLISION];
	}

	return &dev->stats;
}

/**
 * netdev_set_mac_address - set network device MAC address
 * @dev:	Network device.
 * @addr:	Buffer of MAC address.
 *
 * This function is used to set the MAC address of the network device.
 *
 * Return 0 to indicate success.
 */
static int netdev_set_mac_address(struct net_device *dev, void *addr)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;
	struct sockaddr *mac = addr;
	uint interrupt;

	if (priv->port.first_port > 0)
		hw_del_addr(hw, dev->dev_addr);
	else {
		hw->mac_override = 1;
		memcpy(hw->override_addr, mac->sa_data, MAC_ADDR_LEN);
	}

	memcpy(dev->dev_addr, mac->sa_data, MAX_ADDR_LEN);

	interrupt = hw_block_intr(hw);

	if (priv->port.first_port > 0)
		hw_add_addr(hw, dev->dev_addr);
	else
		hw_set_addr(hw);
	hw_restore_intr(hw, interrupt);

	return 0;
}

static void dev_set_promiscuous(struct net_device *dev, struct dev_priv *priv,
	struct ksz_hw *hw, int promiscuous)
{
	if (promiscuous != priv->promiscuous) {
		u8 prev_state = hw->promiscuous;

		if (promiscuous)
			++hw->promiscuous;
		else
			--hw->promiscuous;
		priv->promiscuous = promiscuous;

		/* Turn on/off promiscuous mode. */
		if (hw->promiscuous <= 1 && prev_state <= 1)
			hw_set_promiscuous(hw, hw->promiscuous);

		/*
		 * Port is not in promiscuous mode, meaning it is released
		 * from the bridge.
		 */
		if ((hw->features & STP_SUPPORT) && !promiscuous &&
		    (dev->priv_flags & IFF_BRIDGE_PORT)) {
			struct ksz_switch *sw = hw->ksz_switch;
			int port = priv->port.first_port;

			port_set_stp_state(hw, port, STP_STATE_DISABLED);
			port = 1 << port;
			if (sw->member & port) {
				sw->member &= ~port;
				bridge_change(hw);
			}
		}
	}
}

static void dev_set_multicast(struct dev_priv *priv, struct ksz_hw *hw,
	int multicast)
{
	if (multicast != priv->multicast) {
		u8 all_multi = hw->all_multi;

		if (multicast)
			++hw->all_multi;
		else
			--hw->all_multi;
		priv->multicast = multicast;

		/* Turn on/off all multicast mode. */
		if (hw->all_multi <= 1 && all_multi <= 1)
			hw_set_multicast(hw, hw->all_multi);
	}
}

/**
 * netdev_set_rx_mode
 * @dev:	Network device.
 *
 * This routine is used to set multicast addresses or put the network device
 * into promiscuous mode.
 */
static void netdev_set_rx_mode(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;
	struct netdev_hw_addr *ha;
	int multicast = (dev->flags & IFF_ALLMULTI);

	dev_set_promiscuous(dev, priv, hw, (dev->flags & IFF_PROMISC));

	if (hw_priv->hw.dev_count > 1)
		multicast |= (dev->flags & IFF_MULTICAST);
	dev_set_multicast(priv, hw, multicast);

	/* Cannot use different hashes in multiple device interfaces mode. */
	if (hw_priv->hw.dev_count > 1)
		return;

	if ((dev->flags & IFF_MULTICAST) && !netdev_mc_empty(dev)) {
		int i = 0;

		/* List too big to support so turn on all multicast mode. */
		if (netdev_mc_count(dev) > MAX_MULTICAST_LIST) {
			if (MAX_MULTICAST_LIST != hw->multi_list_size) {
				hw->multi_list_size = MAX_MULTICAST_LIST;
				++hw->all_multi;
				hw_set_multicast(hw, hw->all_multi);
			}
			return;
		}

		netdev_for_each_mc_addr(ha, dev) {
			if (!(*ha->addr & 1))
				continue;
			if (i >= MAX_MULTICAST_LIST)
				break;
			memcpy(hw->multi_list[i++], ha->addr, MAC_ADDR_LEN);
		}
		hw->multi_list_size = (u8) i;
		hw_set_grp_addr(hw);
	} else {
		if (MAX_MULTICAST_LIST == hw->multi_list_size) {
			--hw->all_multi;
			hw_set_multicast(hw, hw->all_multi);
		}
		hw->multi_list_size = 0;
		hw_clr_multicast(hw);
	}
}

static int netdev_change_mtu(struct net_device *dev, int new_mtu)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;
	int hw_mtu;

	if (netif_running(dev))
		return -EBUSY;

	/* Cannot use different MTU in multiple device interfaces mode. */
	if (hw->dev_count > 1)
		if (dev != hw_priv->dev)
			return 0;
	if (new_mtu < 60)
		return -EINVAL;

	if (dev->mtu != new_mtu) {
		hw_mtu = new_mtu + ETHERNET_HEADER_SIZE + 4;
		if (hw_mtu > MAX_RX_BUF_SIZE)
			return -EINVAL;
		if (hw_mtu > REGULAR_RX_BUF_SIZE) {
			hw->features |= RX_HUGE_FRAME;
			hw_mtu = MAX_RX_BUF_SIZE;
		} else {
			hw->features &= ~RX_HUGE_FRAME;
			hw_mtu = REGULAR_RX_BUF_SIZE;
		}
		hw_mtu = (hw_mtu + 3) & ~3;
		hw_priv->mtu = hw_mtu;
		dev->mtu = new_mtu;
	}
	return 0;
}

/**
 * netdev_ioctl - I/O control processing
 * @dev:	Network device.
 * @ifr:	Interface request structure.
 * @cmd:	I/O control code.
 *
 * This function is used to process I/O control calls.
 *
 * Return 0 to indicate success.
 */
static int netdev_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;
	struct ksz_port *port = &priv->port;
	int rc;
	int result = 0;
	struct mii_ioctl_data *data = if_mii(ifr);

	if (down_interruptible(&priv->proc_sem))
		return -ERESTARTSYS;

	/* assume success */
	rc = 0;
	switch (cmd) {
	/* Get address of MII PHY in use. */
	case SIOCGMIIPHY:
		data->phy_id = priv->id;

		/* Fallthrough... */

	/* Read MII PHY register. */
	case SIOCGMIIREG:
		if (data->phy_id != priv->id || data->reg_num >= 6)
			result = -EIO;
		else
			hw_r_phy(hw, port->linked->port_id, data->reg_num,
				&data->val_out);
		break;

	/* Write MII PHY register. */
	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN))
			result = -EPERM;
		else if (data->phy_id != priv->id || data->reg_num >= 6)
			result = -EIO;
		else
			hw_w_phy(hw, port->linked->port_id, data->reg_num,
				data->val_in);
		break;

	default:
		result = -EOPNOTSUPP;
	}

	up(&priv->proc_sem);

	return result;
}

/*
 * MII support
 */

/**
 * mdio_read - read PHY register
 * @dev:	Network device.
 * @phy_id:	The PHY id.
 * @reg_num:	The register number.
 *
 * This function returns the PHY register value.
 *
 * Return the register value.
 */
static int mdio_read(struct net_device *dev, int phy_id, int reg_num)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct ksz_port *port = &priv->port;
	struct ksz_hw *hw = port->hw;
	u16 val_out;

	hw_r_phy(hw, port->linked->port_id, reg_num << 1, &val_out);
	return val_out;
}

/**
 * mdio_write - set PHY register
 * @dev:	Network device.
 * @phy_id:	The PHY id.
 * @reg_num:	The register number.
 * @val:	The register value.
 *
 * This procedure sets the PHY register value.
 */
static void mdio_write(struct net_device *dev, int phy_id, int reg_num, int val)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct ksz_port *port = &priv->port;
	struct ksz_hw *hw = port->hw;
	int i;
	int pi;

	for (i = 0, pi = port->first_port; i < port->port_cnt; i++, pi++)
		hw_w_phy(hw, pi, reg_num << 1, val);
}

/*
 * ethtool support
 */

#define EEPROM_SIZE			0x40

static u16 eeprom_data[EEPROM_SIZE] = { 0 };

#define ADVERTISED_ALL			\
	(ADVERTISED_10baseT_Half |	\
	ADVERTISED_10baseT_Full |	\
	ADVERTISED_100baseT_Half |	\
	ADVERTISED_100baseT_Full)

/* These functions use the MII functions in mii.c. */

/**
 * netdev_get_settings - get network device settings
 * @dev:	Network device.
 * @cmd:	Ethtool command.
 *
 * This function queries the PHY and returns its state in the ethtool command.
 *
 * Return 0 if successful; otherwise an error code.
 */
static int netdev_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;

	mutex_lock(&hw_priv->lock);
	mii_ethtool_gset(&priv->mii_if, cmd);
	cmd->advertising |= SUPPORTED_TP;
	mutex_unlock(&hw_priv->lock);

	/* Save advertised settings for workaround in next function. */
	priv->advertising = cmd->advertising;
	return 0;
}

/**
 * netdev_set_settings - set network device settings
 * @dev:	Network device.
 * @cmd:	Ethtool command.
 *
 * This function sets the PHY according to the ethtool command.
 *
 * Return 0 if successful; otherwise an error code.
 */
static int netdev_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_port *port = &priv->port;
	int rc;

	/*
	 * ethtool utility does not change advertised setting if auto
	 * negotiation is not specified explicitly.
	 */
	if (cmd->autoneg && priv->advertising == cmd->advertising) {
		cmd->advertising |= ADVERTISED_ALL;
		if (10 == cmd->speed)
			cmd->advertising &=
				~(ADVERTISED_100baseT_Full |
				ADVERTISED_100baseT_Half);
		else if (100 == cmd->speed)
			cmd->advertising &=
				~(ADVERTISED_10baseT_Full |
				ADVERTISED_10baseT_Half);
		if (0 == cmd->duplex)
			cmd->advertising &=
				~(ADVERTISED_100baseT_Full |
				ADVERTISED_10baseT_Full);
		else if (1 == cmd->duplex)
			cmd->advertising &=
				~(ADVERTISED_100baseT_Half |
				ADVERTISED_10baseT_Half);
	}
	mutex_lock(&hw_priv->lock);
	if (cmd->autoneg &&
			(cmd->advertising & ADVERTISED_ALL) ==
			ADVERTISED_ALL) {
		port->duplex = 0;
		port->speed = 0;
		port->force_link = 0;
	} else {
		port->duplex = cmd->duplex + 1;
		if (cmd->speed != 1000)
			port->speed = cmd->speed;
		if (cmd->autoneg)
			port->force_link = 0;
		else
			port->force_link = 1;
	}
	rc = mii_ethtool_sset(&priv->mii_if, cmd);
	mutex_unlock(&hw_priv->lock);
	return rc;
}

/**
 * netdev_nway_reset - restart auto-negotiation
 * @dev:	Network device.
 *
 * This function restarts the PHY for auto-negotiation.
 *
 * Return 0 if successful; otherwise an error code.
 */
static int netdev_nway_reset(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	int rc;

	mutex_lock(&hw_priv->lock);
	rc = mii_nway_restart(&priv->mii_if);
	mutex_unlock(&hw_priv->lock);
	return rc;
}

/**
 * netdev_get_link - get network device link status
 * @dev:	Network device.
 *
 * This function gets the link status from the PHY.
 *
 * Return true if PHY is linked and false otherwise.
 */
static u32 netdev_get_link(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	int rc;

	rc = mii_link_ok(&priv->mii_if);
	return rc;
}

/**
 * netdev_get_drvinfo - get network driver information
 * @dev:	Network device.
 * @info:	Ethtool driver info data structure.
 *
 * This procedure returns the driver information.
 */
static void netdev_get_drvinfo(struct net_device *dev,
	struct ethtool_drvinfo *info)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;

	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	strcpy(info->bus_info, pci_name(hw_priv->pdev));
}

/**
 * netdev_get_regs_len - get length of register dump
 * @dev:	Network device.
 *
 * This function returns the length of the register dump.
 *
 * Return length of the register dump.
 */
static struct hw_regs {
	int start;
	int end;
} hw_regs_range[] = {
	{ KS_DMA_TX_CTRL,	KS884X_INTERRUPTS_STATUS },
	{ KS_ADD_ADDR_0_LO,	KS_ADD_ADDR_F_HI },
	{ KS884X_ADDR_0_OFFSET,	KS8841_WOL_FRAME_BYTE2_OFFSET },
	{ KS884X_SIDER_P,	KS8842_SGCR7_P },
	{ KS8842_MACAR1_P,	KS8842_TOSR8_P },
	{ KS884X_P1MBCR_P,	KS8842_P3ERCR_P },
	{ 0, 0 }
};

static int netdev_get_regs_len(struct net_device *dev)
{
	struct hw_regs *range = hw_regs_range;
	int regs_len = 0x10 * sizeof(u32);

	while (range->end > range->start) {
		regs_len += (range->end - range->start + 3) / 4 * 4;
		range++;
	}
	return regs_len;
}

/**
 * netdev_get_regs - get register dump
 * @dev:	Network device.
 * @regs:	Ethtool registers data structure.
 * @ptr:	Buffer to store the register values.
 *
 * This procedure dumps the register values in the provided buffer.
 */
static void netdev_get_regs(struct net_device *dev, struct ethtool_regs *regs,
	void *ptr)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;
	int *buf = (int *) ptr;
	struct hw_regs *range = hw_regs_range;
	int len;

	mutex_lock(&hw_priv->lock);
	regs->version = 0;
	for (len = 0; len < 0x40; len += 4) {
		pci_read_config_dword(hw_priv->pdev, len, buf);
		buf++;
	}
	while (range->end > range->start) {
		for (len = range->start; len < range->end; len += 4) {
			*buf = readl(hw->io + len);
			buf++;
		}
		range++;
	}
	mutex_unlock(&hw_priv->lock);
}

#define WOL_SUPPORT			\
	(WAKE_PHY | WAKE_MAGIC |	\
	WAKE_UCAST | WAKE_MCAST |	\
	WAKE_BCAST | WAKE_ARP)

/**
 * netdev_get_wol - get Wake-on-LAN support
 * @dev:	Network device.
 * @wol:	Ethtool Wake-on-LAN data structure.
 *
 * This procedure returns Wake-on-LAN support.
 */
static void netdev_get_wol(struct net_device *dev,
	struct ethtool_wolinfo *wol)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;

	wol->supported = hw_priv->wol_support;
	wol->wolopts = hw_priv->wol_enable;
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}

/**
 * netdev_set_wol - set Wake-on-LAN support
 * @dev:	Network device.
 * @wol:	Ethtool Wake-on-LAN data structure.
 *
 * This function sets Wake-on-LAN support.
 *
 * Return 0 if successful; otherwise an error code.
 */
static int netdev_set_wol(struct net_device *dev,
	struct ethtool_wolinfo *wol)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;

	/* Need to find a way to retrieve the device IP address. */
	static const u8 net_addr[] = { 192, 168, 1, 1 };

	if (wol->wolopts & ~hw_priv->wol_support)
		return -EINVAL;

	hw_priv->wol_enable = wol->wolopts;

	/* Link wakeup cannot really be disabled. */
	if (wol->wolopts)
		hw_priv->wol_enable |= WAKE_PHY;
	hw_enable_wol(&hw_priv->hw, hw_priv->wol_enable, net_addr);
	return 0;
}

/**
 * netdev_get_msglevel - get debug message level
 * @dev:	Network device.
 *
 * This function returns current debug message level.
 *
 * Return current debug message flags.
 */
static u32 netdev_get_msglevel(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);

	return priv->msg_enable;
}

/**
 * netdev_set_msglevel - set debug message level
 * @dev:	Network device.
 * @value:	Debug message flags.
 *
 * This procedure sets debug message level.
 */
static void netdev_set_msglevel(struct net_device *dev, u32 value)
{
	struct dev_priv *priv = netdev_priv(dev);

	priv->msg_enable = value;
}

/**
 * netdev_get_eeprom_len - get EEPROM length
 * @dev:	Network device.
 *
 * This function returns the length of the EEPROM.
 *
 * Return length of the EEPROM.
 */
static int netdev_get_eeprom_len(struct net_device *dev)
{
	return EEPROM_SIZE * 2;
}

/**
 * netdev_get_eeprom - get EEPROM data
 * @dev:	Network device.
 * @eeprom:	Ethtool EEPROM data structure.
 * @data:	Buffer to store the EEPROM data.
 *
 * This function dumps the EEPROM data in the provided buffer.
 *
 * Return 0 if successful; otherwise an error code.
 */
#define EEPROM_MAGIC			0x10A18842

static int netdev_get_eeprom(struct net_device *dev,
	struct ethtool_eeprom *eeprom, u8 *data)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	u8 *eeprom_byte = (u8 *) eeprom_data;
	int i;
	int len;

	len = (eeprom->offset + eeprom->len + 1) / 2;
	for (i = eeprom->offset / 2; i < len; i++)
		eeprom_data[i] = eeprom_read(&hw_priv->hw, i);
	eeprom->magic = EEPROM_MAGIC;
	memcpy(data, &eeprom_byte[eeprom->offset], eeprom->len);

	return 0;
}

/**
 * netdev_set_eeprom - write EEPROM data
 * @dev:	Network device.
 * @eeprom:	Ethtool EEPROM data structure.
 * @data:	Data buffer.
 *
 * This function modifies the EEPROM data one byte at a time.
 *
 * Return 0 if successful; otherwise an error code.
 */
static int netdev_set_eeprom(struct net_device *dev,
	struct ethtool_eeprom *eeprom, u8 *data)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	u16 eeprom_word[EEPROM_SIZE];
	u8 *eeprom_byte = (u8 *) eeprom_word;
	int i;
	int len;

	if (eeprom->magic != EEPROM_MAGIC)
		return -EINVAL;

	len = (eeprom->offset + eeprom->len + 1) / 2;
	for (i = eeprom->offset / 2; i < len; i++)
		eeprom_data[i] = eeprom_read(&hw_priv->hw, i);
	memcpy(eeprom_word, eeprom_data, EEPROM_SIZE * 2);
	memcpy(&eeprom_byte[eeprom->offset], data, eeprom->len);
	for (i = 0; i < EEPROM_SIZE; i++)
		if (eeprom_word[i] != eeprom_data[i]) {
			eeprom_data[i] = eeprom_word[i];
			eeprom_write(&hw_priv->hw, i, eeprom_data[i]);
	}

	return 0;
}

/**
 * netdev_get_pauseparam - get flow control parameters
 * @dev:	Network device.
 * @pause:	Ethtool PAUSE settings data structure.
 *
 * This procedure returns the PAUSE control flow settings.
 */
static void netdev_get_pauseparam(struct net_device *dev,
	struct ethtool_pauseparam *pause)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;

	pause->autoneg = (hw->overrides & PAUSE_FLOW_CTRL) ? 0 : 1;
	if (!hw->ksz_switch) {
		pause->rx_pause =
			(hw->rx_cfg & DMA_RX_FLOW_ENABLE) ? 1 : 0;
		pause->tx_pause =
			(hw->tx_cfg & DMA_TX_FLOW_ENABLE) ? 1 : 0;
	} else {
		pause->rx_pause =
			(sw_chk(hw, KS8842_SWITCH_CTRL_1_OFFSET,
				SWITCH_RX_FLOW_CTRL)) ? 1 : 0;
		pause->tx_pause =
			(sw_chk(hw, KS8842_SWITCH_CTRL_1_OFFSET,
				SWITCH_TX_FLOW_CTRL)) ? 1 : 0;
	}
}

/**
 * netdev_set_pauseparam - set flow control parameters
 * @dev:	Network device.
 * @pause:	Ethtool PAUSE settings data structure.
 *
 * This function sets the PAUSE control flow settings.
 * Not implemented yet.
 *
 * Return 0 if successful; otherwise an error code.
 */
static int netdev_set_pauseparam(struct net_device *dev,
	struct ethtool_pauseparam *pause)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;
	struct ksz_port *port = &priv->port;

	mutex_lock(&hw_priv->lock);
	if (pause->autoneg) {
		if (!pause->rx_pause && !pause->tx_pause)
			port->flow_ctrl = PHY_NO_FLOW_CTRL;
		else
			port->flow_ctrl = PHY_FLOW_CTRL;
		hw->overrides &= ~PAUSE_FLOW_CTRL;
		port->force_link = 0;
		if (hw->ksz_switch) {
			sw_cfg(hw, KS8842_SWITCH_CTRL_1_OFFSET,
				SWITCH_RX_FLOW_CTRL, 1);
			sw_cfg(hw, KS8842_SWITCH_CTRL_1_OFFSET,
				SWITCH_TX_FLOW_CTRL, 1);
		}
		port_set_link_speed(port);
	} else {
		hw->overrides |= PAUSE_FLOW_CTRL;
		if (hw->ksz_switch) {
			sw_cfg(hw, KS8842_SWITCH_CTRL_1_OFFSET,
				SWITCH_RX_FLOW_CTRL, pause->rx_pause);
			sw_cfg(hw, KS8842_SWITCH_CTRL_1_OFFSET,
				SWITCH_TX_FLOW_CTRL, pause->tx_pause);
		} else
			set_flow_ctrl(hw, pause->rx_pause, pause->tx_pause);
	}
	mutex_unlock(&hw_priv->lock);

	return 0;
}

/**
 * netdev_get_ringparam - get tx/rx ring parameters
 * @dev:	Network device.
 * @pause:	Ethtool RING settings data structure.
 *
 * This procedure returns the TX/RX ring settings.
 */
static void netdev_get_ringparam(struct net_device *dev,
	struct ethtool_ringparam *ring)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;

	ring->tx_max_pending = (1 << 9);
	ring->tx_pending = hw->tx_desc_info.alloc;
	ring->rx_max_pending = (1 << 9);
	ring->rx_pending = hw->rx_desc_info.alloc;
}

#define STATS_LEN			(TOTAL_PORT_COUNTER_NUM)

static struct {
	char string[ETH_GSTRING_LEN];
} ethtool_stats_keys[STATS_LEN] = {
	{ "rx_lo_priority_octets" },
	{ "rx_hi_priority_octets" },
	{ "rx_undersize_packets" },
	{ "rx_fragments" },
	{ "rx_oversize_packets" },
	{ "rx_jabbers" },
	{ "rx_symbol_errors" },
	{ "rx_crc_errors" },
	{ "rx_align_errors" },
	{ "rx_mac_ctrl_packets" },
	{ "rx_pause_packets" },
	{ "rx_bcast_packets" },
	{ "rx_mcast_packets" },
	{ "rx_ucast_packets" },
	{ "rx_64_or_less_octet_packets" },
	{ "rx_65_to_127_octet_packets" },
	{ "rx_128_to_255_octet_packets" },
	{ "rx_256_to_511_octet_packets" },
	{ "rx_512_to_1023_octet_packets" },
	{ "rx_1024_to_1522_octet_packets" },

	{ "tx_lo_priority_octets" },
	{ "tx_hi_priority_octets" },
	{ "tx_late_collisions" },
	{ "tx_pause_packets" },
	{ "tx_bcast_packets" },
	{ "tx_mcast_packets" },
	{ "tx_ucast_packets" },
	{ "tx_deferred" },
	{ "tx_total_collisions" },
	{ "tx_excessive_collisions" },
	{ "tx_single_collisions" },
	{ "tx_mult_collisions" },

	{ "rx_discards" },
	{ "tx_discards" },
};

/**
 * netdev_get_strings - get statistics identity strings
 * @dev:	Network device.
 * @stringset:	String set identifier.
 * @buf:	Buffer to store the strings.
 *
 * This procedure returns the strings used to identify the statistics.
 */
static void netdev_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;

	if (ETH_SS_STATS == stringset)
		memcpy(buf, &ethtool_stats_keys,
			ETH_GSTRING_LEN * hw->mib_cnt);
}

/**
 * netdev_get_sset_count - get statistics size
 * @dev:	Network device.
 * @sset:	The statistics set number.
 *
 * This function returns the size of the statistics to be reported.
 *
 * Return size of the statistics to be reported.
 */
static int netdev_get_sset_count(struct net_device *dev, int sset)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;

	switch (sset) {
	case ETH_SS_STATS:
		return hw->mib_cnt;
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * netdev_get_ethtool_stats - get network device statistics
 * @dev:	Network device.
 * @stats:	Ethtool statistics data structure.
 * @data:	Buffer to store the statistics.
 *
 * This procedure returns the statistics.
 */
static void netdev_get_ethtool_stats(struct net_device *dev,
	struct ethtool_stats *stats, u64 *data)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;
	struct ksz_port *port = &priv->port;
	int n_stats = stats->n_stats;
	int i;
	int n;
	int p;
	int rc;
	u64 counter[TOTAL_PORT_COUNTER_NUM];

	mutex_lock(&hw_priv->lock);
	n = SWITCH_PORT_NUM;
	for (i = 0, p = port->first_port; i < port->mib_port_cnt; i++, p++) {
		if (media_connected == hw->port_mib[p].state) {
			hw_priv->counter[p].read = 1;

			/* Remember first port that requests read. */
			if (n == SWITCH_PORT_NUM)
				n = p;
		}
	}
	mutex_unlock(&hw_priv->lock);

	if (n < SWITCH_PORT_NUM)
		schedule_work(&hw_priv->mib_read);

	if (1 == port->mib_port_cnt && n < SWITCH_PORT_NUM) {
		p = n;
		rc = wait_event_interruptible_timeout(
			hw_priv->counter[p].counter,
			2 == hw_priv->counter[p].read,
			HZ * 1);
	} else
		for (i = 0, p = n; i < port->mib_port_cnt - n; i++, p++) {
			if (0 == i) {
				rc = wait_event_interruptible_timeout(
					hw_priv->counter[p].counter,
					2 == hw_priv->counter[p].read,
					HZ * 2);
			} else if (hw->port_mib[p].cnt_ptr) {
				rc = wait_event_interruptible_timeout(
					hw_priv->counter[p].counter,
					2 == hw_priv->counter[p].read,
					HZ * 1);
			}
		}

	get_mib_counters(hw, port->first_port, port->mib_port_cnt, counter);
	n = hw->mib_cnt;
	if (n > n_stats)
		n = n_stats;
	n_stats -= n;
	for (i = 0; i < n; i++)
		*data++ = counter[i];
}

/**
 * netdev_get_rx_csum - get receive checksum support
 * @dev:	Network device.
 *
 * This function gets receive checksum support setting.
 *
 * Return true if receive checksum is enabled; false otherwise.
 */
static u32 netdev_get_rx_csum(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;

	return hw->rx_cfg &
		(DMA_RX_CSUM_UDP |
		DMA_RX_CSUM_TCP |
		DMA_RX_CSUM_IP);
}

/**
 * netdev_set_rx_csum - set receive checksum support
 * @dev:	Network device.
 * @data:	Zero to disable receive checksum support.
 *
 * This function sets receive checksum support setting.
 *
 * Return 0 if successful; otherwise an error code.
 */
static int netdev_set_rx_csum(struct net_device *dev, u32 data)
{
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;
	u32 new_setting = hw->rx_cfg;

	if (data)
		new_setting |=
			(DMA_RX_CSUM_UDP | DMA_RX_CSUM_TCP |
			DMA_RX_CSUM_IP);
	else
		new_setting &=
			~(DMA_RX_CSUM_UDP | DMA_RX_CSUM_TCP |
			DMA_RX_CSUM_IP);
	new_setting &= ~DMA_RX_CSUM_UDP;
	mutex_lock(&hw_priv->lock);
	if (new_setting != hw->rx_cfg) {
		hw->rx_cfg = new_setting;
		if (hw->enabled)
			writel(hw->rx_cfg, hw->io + KS_DMA_RX_CTRL);
	}
	mutex_unlock(&hw_priv->lock);
	return 0;
}

static struct ethtool_ops netdev_ethtool_ops = {
	.get_settings		= netdev_get_settings,
	.set_settings		= netdev_set_settings,
	.nway_reset		= netdev_nway_reset,
	.get_link		= netdev_get_link,
	.get_drvinfo		= netdev_get_drvinfo,
	.get_regs_len		= netdev_get_regs_len,
	.get_regs		= netdev_get_regs,
	.get_wol		= netdev_get_wol,
	.set_wol		= netdev_set_wol,
	.get_msglevel		= netdev_get_msglevel,
	.set_msglevel		= netdev_set_msglevel,
	.get_eeprom_len		= netdev_get_eeprom_len,
	.get_eeprom		= netdev_get_eeprom,
	.set_eeprom		= netdev_set_eeprom,
	.get_pauseparam		= netdev_get_pauseparam,
	.set_pauseparam		= netdev_set_pauseparam,
	.get_ringparam		= netdev_get_ringparam,
	.get_strings		= netdev_get_strings,
	.get_sset_count		= netdev_get_sset_count,
	.get_ethtool_stats	= netdev_get_ethtool_stats,
	.get_rx_csum		= netdev_get_rx_csum,
	.set_rx_csum		= netdev_set_rx_csum,
	.get_tx_csum		= ethtool_op_get_tx_csum,
	.set_tx_csum		= ethtool_op_set_tx_csum,
	.get_sg			= ethtool_op_get_sg,
	.set_sg			= ethtool_op_set_sg,
};

/*
 * Hardware monitoring
 */

static void update_link(struct net_device *dev, struct dev_priv *priv,
	struct ksz_port *port)
{
	if (priv->media_state != port->linked->state) {
		priv->media_state = port->linked->state;
		if (netif_running(dev))
			set_media_state(dev, media_connected);
	}
}

static void mib_read_work(struct work_struct *work)
{
	struct dev_info *hw_priv =
		container_of(work, struct dev_info, mib_read);
	struct ksz_hw *hw = &hw_priv->hw;
	struct ksz_port_mib *mib;
	int i;

	next_jiffies = jiffies;
	for (i = 0; i < hw->mib_port_cnt; i++) {
		mib = &hw->port_mib[i];

		/* Reading MIB counters or requested to read. */
		if (mib->cnt_ptr || 1 == hw_priv->counter[i].read) {

			/* Need to process receive interrupt. */
			if (port_r_cnt(hw, i))
				break;
			hw_priv->counter[i].read = 0;

			/* Finish reading counters. */
			if (0 == mib->cnt_ptr) {
				hw_priv->counter[i].read = 2;
				wake_up_interruptible(
					&hw_priv->counter[i].counter);
			}
		} else if (jiffies >= hw_priv->counter[i].time) {
			/* Only read MIB counters when the port is connected. */
			if (media_connected == mib->state)
				hw_priv->counter[i].read = 1;
			next_jiffies += HZ * 1 * hw->mib_port_cnt;
			hw_priv->counter[i].time = next_jiffies;

		/* Port is just disconnected. */
		} else if (mib->link_down) {
			mib->link_down = 0;

			/* Read counters one last time after link is lost. */
			hw_priv->counter[i].read = 1;
		}
	}
}

static void mib_monitor(unsigned long ptr)
{
	struct dev_info *hw_priv = (struct dev_info *) ptr;

	mib_read_work(&hw_priv->mib_read);

	/* This is used to verify Wake-on-LAN is working. */
	if (hw_priv->pme_wait) {
		if (hw_priv->pme_wait <= jiffies) {
			hw_clr_wol_pme_status(&hw_priv->hw);
			hw_priv->pme_wait = 0;
		}
	} else if (hw_chk_wol_pme_status(&hw_priv->hw)) {

		/* PME is asserted.  Wait 2 seconds to clear it. */
		hw_priv->pme_wait = jiffies + HZ * 2;
	}

	ksz_update_timer(&hw_priv->mib_timer_info);
}

/**
 * dev_monitor - periodic monitoring
 * @ptr:	Network device pointer.
 *
 * This routine is run in a kernel timer to monitor the network device.
 */
static void dev_monitor(unsigned long ptr)
{
	struct net_device *dev = (struct net_device *) ptr;
	struct dev_priv *priv = netdev_priv(dev);
	struct dev_info *hw_priv = priv->adapter;
	struct ksz_hw *hw = &hw_priv->hw;
	struct ksz_port *port = &priv->port;

	if (!(hw->features & LINK_INT_WORKING))
		port_get_link_speed(port);
	update_link(dev, priv, port);

	ksz_update_timer(&priv->monitor_timer_info);
}

/*
 * Linux network device interface functions
 */

/* Driver exported variables */

static int msg_enable;

static char *macaddr = ":";
static char *mac1addr = ":";

/*
 * This enables multiple network device mode for KSZ8842, which contains a
 * switch with two physical ports.  Some users like to take control of the
 * ports for running Spanning Tree Protocol.  The driver will create an
 * additional eth? device for the other port.
 *
 * Some limitations are the network devices cannot have different MTU and
 * multicast hash tables.
 */
static int multi_dev;

/*
 * As most users select multiple network device mode to use Spanning Tree
 * Protocol, this enables a feature in which most unicast and multicast packets
 * are forwarded inside the switch and not passed to the host.  Only packets
 * that need the host's attention are passed to it.  This prevents the host
 * wasting CPU time to examine each and every incoming packets and do the
 * forwarding itself.
 *
 * As the hack requires the private bridge header, the driver cannot compile
 * with just the kernel headers.
 *
 * Enabling STP support also turns on multiple network device mode.
 */
static int stp;

/*
 * This enables fast aging in the KSZ8842 switch.  Not sure what situation
 * needs that.  However, fast aging is used to flush the dynamic MAC table when
 * STP suport is enabled.
 */
static int fast_aging;

/**
 * netdev_init - initialize network device.
 * @dev:	Network device.
 *
 * This function initializes the network device.
 *
 * Return 0 if successful; otherwise an error code indicating failure.
 */
static int __init netdev_init(struct net_device *dev)
{
	struct dev_priv *priv = netdev_priv(dev);

	/* 500 ms timeout */
	ksz_init_timer(&priv->monitor_timer_info, 500 * HZ / 1000,
		dev_monitor, dev);

	/* 500 ms timeout */
	dev->watchdog_timeo = HZ / 2;

	dev->features |= NETIF_F_IP_CSUM;

	/*
	 * Hardware does not really support IPv6 checksum generation, but
	 * driver actually runs faster with this on.  Refer IPV6_CSUM_GEN_HACK.
	 */
	dev->features |= NETIF_F_IPV6_CSUM;
	dev->features |= NETIF_F_SG;

	sema_init(&priv->proc_sem, 1);

	priv->mii_if.phy_id_mask = 0x1;
	priv->mii_if.reg_num_mask = 0x7;
	priv->mii_if.dev = dev;
	priv->mii_if.mdio_read = mdio_read;
	priv->mii_if.mdio_write = mdio_write;
	priv->mii_if.phy_id = priv->port.first_port + 1;

	priv->msg_enable = netif_msg_init(msg_enable,
		(NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK));

	return 0;
}

static const struct net_device_ops netdev_ops = {
	.ndo_init		= netdev_init,
	.ndo_open		= netdev_open,
	.ndo_stop		= netdev_close,
	.ndo_get_stats		= netdev_query_statistics,
	.ndo_start_xmit		= netdev_tx,
	.ndo_tx_timeout		= netdev_tx_timeout,
	.ndo_change_mtu		= netdev_change_mtu,
	.ndo_set_mac_address	= netdev_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= netdev_ioctl,
	.ndo_set_rx_mode	= netdev_set_rx_mode,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= netdev_netpoll,
#endif
};

static void netdev_free(struct net_device *dev)
{
	if (dev->watchdog_timeo)
		unregister_netdev(dev);

	free_netdev(dev);
}

struct platform_info {
	struct dev_info dev_info;
	struct net_device *netdev[SWITCH_PORT_NUM];
};

static int net_device_present;

static void get_mac_addr(struct dev_info *hw_priv, u8 *macaddr, int port)
{
	int i;
	int j;
	int got_num;
	int num;

	i = j = num = got_num = 0;
	while (j < MAC_ADDR_LEN) {
		if (macaddr[i]) {
			int digit;

			got_num = 1;
			digit = hex_to_bin(macaddr[i]);
			if (digit >= 0)
				num = num * 16 + digit;
			else if (':' == macaddr[i])
				got_num = 2;
			else
				break;
		} else if (got_num)
			got_num = 2;
		else
			break;
		if (2 == got_num) {
			if (MAIN_PORT == port) {
				hw_priv->hw.override_addr[j++] = (u8) num;
				hw_priv->hw.override_addr[5] +=
					hw_priv->hw.id;
			} else {
				hw_priv->hw.ksz_switch->other_addr[j++] =
					(u8) num;
				hw_priv->hw.ksz_switch->other_addr[5] +=
					hw_priv->hw.id;
			}
			num = got_num = 0;
		}
		i++;
	}
	if (MAC_ADDR_LEN == j) {
		if (MAIN_PORT == port)
			hw_priv->hw.mac_override = 1;
	}
}

#define KS884X_DMA_MASK			(~0x0UL)

static void read_other_addr(struct ksz_hw *hw)
{
	int i;
	u16 data[3];
	struct ksz_switch *sw = hw->ksz_switch;

	for (i = 0; i < 3; i++)
		data[i] = eeprom_read(hw, i + EEPROM_DATA_OTHER_MAC_ADDR);
	if ((data[0] || data[1] || data[2]) && data[0] != 0xffff) {
		sw->other_addr[5] = (u8) data[0];
		sw->other_addr[4] = (u8)(data[0] >> 8);
		sw->other_addr[3] = (u8) data[1];
		sw->other_addr[2] = (u8)(data[1] >> 8);
		sw->other_addr[1] = (u8) data[2];
		sw->other_addr[0] = (u8)(data[2] >> 8);
	}
}

#ifndef PCI_VENDOR_ID_MICREL_KS
#define PCI_VENDOR_ID_MICREL_KS		0x16c6
#endif

static int __devinit pcidev_init(struct pci_dev *pdev,
	const struct pci_device_id *id)
{
	struct net_device *dev;
	struct dev_priv *priv;
	struct dev_info *hw_priv;
	struct ksz_hw *hw;
	struct platform_info *info;
	struct ksz_port *port;
	unsigned long reg_base;
	unsigned long reg_len;
	int cnt;
	int i;
	int mib_port_count;
	int pi;
	int port_count;
	int result;
	char banner[sizeof(version)];
	struct ksz_switch *sw = NULL;

	result = pci_enable_device(pdev);
	if (result)
		return result;

	result = -ENODEV;

	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(32)) ||
			pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)))
		return result;

	reg_base = pci_resource_start(pdev, 0);
	reg_len = pci_resource_len(pdev, 0);
	if ((pci_resource_flags(pdev, 0) & IORESOURCE_IO) != 0)
		return result;

	if (!request_mem_region(reg_base, reg_len, DRV_NAME))
		return result;
	pci_set_master(pdev);

	result = -ENOMEM;

	info = kzalloc(sizeof(struct platform_info), GFP_KERNEL);
	if (!info)
		goto pcidev_init_dev_err;

	hw_priv = &info->dev_info;
	hw_priv->pdev = pdev;

	hw = &hw_priv->hw;

	hw->io = ioremap(reg_base, reg_len);
	if (!hw->io)
		goto pcidev_init_io_err;

	cnt = hw_init(hw);
	if (!cnt) {
		if (msg_enable & NETIF_MSG_PROBE)
			pr_alert("chip not detected\n");
		result = -ENODEV;
		goto pcidev_init_alloc_err;
	}

	snprintf(banner, sizeof(banner), "%s", version);
	banner[13] = cnt + '0';		/* Replace x in "Micrel KSZ884x" */
	dev_info(&hw_priv->pdev->dev, "%s\n", banner);
	dev_dbg(&hw_priv->pdev->dev, "Mem = %p; IRQ = %d\n", hw->io, pdev->irq);

	/* Assume device is KSZ8841. */
	hw->dev_count = 1;
	port_count = 1;
	mib_port_count = 1;
	hw->addr_list_size = 0;
	hw->mib_cnt = PORT_COUNTER_NUM;
	hw->mib_port_cnt = 1;

	/* KSZ8842 has a switch with multiple ports. */
	if (2 == cnt) {
		if (fast_aging)
			hw->overrides |= FAST_AGING;

		hw->mib_cnt = TOTAL_PORT_COUNTER_NUM;

		/* Multiple network device interfaces are required. */
		if (multi_dev) {
			hw->dev_count = SWITCH_PORT_NUM;
			hw->addr_list_size = SWITCH_PORT_NUM - 1;
		}

		/* Single network device has multiple ports. */
		if (1 == hw->dev_count) {
			port_count = SWITCH_PORT_NUM;
			mib_port_count = SWITCH_PORT_NUM;
		}
		hw->mib_port_cnt = TOTAL_PORT_NUM;
		hw->ksz_switch = kzalloc(sizeof(struct ksz_switch), GFP_KERNEL);
		if (!hw->ksz_switch)
			goto pcidev_init_alloc_err;

		sw = hw->ksz_switch;
	}
	for (i = 0; i < hw->mib_port_cnt; i++)
		hw->port_mib[i].mib_start = 0;

	hw->parent = hw_priv;

	/* Default MTU is 1500. */
	hw_priv->mtu = (REGULAR_RX_BUF_SIZE + 3) & ~3;

	if (ksz_alloc_mem(hw_priv))
		goto pcidev_init_mem_err;

	hw_priv->hw.id = net_device_present;

	spin_lock_init(&hw_priv->hwlock);
	mutex_init(&hw_priv->lock);

	/* tasklet is enabled. */
	tasklet_init(&hw_priv->rx_tasklet, rx_proc_task,
		(unsigned long) hw_priv);
	tasklet_init(&hw_priv->tx_tasklet, tx_proc_task,
		(unsigned long) hw_priv);

	/* tasklet_enable will decrement the atomic counter. */
	tasklet_disable(&hw_priv->rx_tasklet);
	tasklet_disable(&hw_priv->tx_tasklet);

	for (i = 0; i < TOTAL_PORT_NUM; i++)
		init_waitqueue_head(&hw_priv->counter[i].counter);

	if (macaddr[0] != ':')
		get_mac_addr(hw_priv, macaddr, MAIN_PORT);

	/* Read MAC address and initialize override address if not overrided. */
	hw_read_addr(hw);

	/* Multiple device interfaces mode requires a second MAC address. */
	if (hw->dev_count > 1) {
		memcpy(sw->other_addr, hw->override_addr, MAC_ADDR_LEN);
		read_other_addr(hw);
		if (mac1addr[0] != ':')
			get_mac_addr(hw_priv, mac1addr, OTHER_PORT);
	}

	hw_setup(hw);
	if (hw->ksz_switch)
		sw_setup(hw);
	else {
		hw_priv->wol_support = WOL_SUPPORT;
		hw_priv->wol_enable = 0;
	}

	INIT_WORK(&hw_priv->mib_read, mib_read_work);

	/* 500 ms timeout */
	ksz_init_timer(&hw_priv->mib_timer_info, 500 * HZ / 1000,
		mib_monitor, hw_priv);

	for (i = 0; i < hw->dev_count; i++) {
		dev = alloc_etherdev(sizeof(struct dev_priv));
		if (!dev)
			goto pcidev_init_reg_err;
		info->netdev[i] = dev;

		priv = netdev_priv(dev);
		priv->adapter = hw_priv;
		priv->id = net_device_present++;

		port = &priv->port;
		port->port_cnt = port_count;
		port->mib_port_cnt = mib_port_count;
		port->first_port = i;
		port->flow_ctrl = PHY_FLOW_CTRL;

		port->hw = hw;
		port->linked = &hw->port_info[port->first_port];

		for (cnt = 0, pi = i; cnt < port_count; cnt++, pi++) {
			hw->port_info[pi].port_id = pi;
			hw->port_info[pi].pdev = dev;
			hw->port_info[pi].state = media_disconnected;
		}

		dev->mem_start = (unsigned long) hw->io;
		dev->mem_end = dev->mem_start + reg_len - 1;
		dev->irq = pdev->irq;
		if (MAIN_PORT == i)
			memcpy(dev->dev_addr, hw_priv->hw.override_addr,
				MAC_ADDR_LEN);
		else {
			memcpy(dev->dev_addr, sw->other_addr,
				MAC_ADDR_LEN);
			if (!memcmp(sw->other_addr, hw->override_addr,
					MAC_ADDR_LEN))
				dev->dev_addr[5] += port->first_port;
		}

		dev->netdev_ops = &netdev_ops;
		SET_ETHTOOL_OPS(dev, &netdev_ethtool_ops);
		if (register_netdev(dev))
			goto pcidev_init_reg_err;
		port_set_power_saving(port, true);
	}

	pci_dev_get(hw_priv->pdev);
	pci_set_drvdata(pdev, info);
	return 0;

pcidev_init_reg_err:
	for (i = 0; i < hw->dev_count; i++) {
		if (info->netdev[i]) {
			netdev_free(info->netdev[i]);
			info->netdev[i] = NULL;
		}
	}

pcidev_init_mem_err:
	ksz_free_mem(hw_priv);
	kfree(hw->ksz_switch);

pcidev_init_alloc_err:
	iounmap(hw->io);

pcidev_init_io_err:
	kfree(info);

pcidev_init_dev_err:
	release_mem_region(reg_base, reg_len);

	return result;
}

static void pcidev_exit(struct pci_dev *pdev)
{
	int i;
	struct platform_info *info = pci_get_drvdata(pdev);
	struct dev_info *hw_priv = &info->dev_info;

	pci_set_drvdata(pdev, NULL);

	release_mem_region(pci_resource_start(pdev, 0),
		pci_resource_len(pdev, 0));
	for (i = 0; i < hw_priv->hw.dev_count; i++) {
		if (info->netdev[i])
			netdev_free(info->netdev[i]);
	}
	if (hw_priv->hw.io)
		iounmap(hw_priv->hw.io);
	ksz_free_mem(hw_priv);
	kfree(hw_priv->hw.ksz_switch);
	pci_dev_put(hw_priv->pdev);
	kfree(info);
}

#ifdef CONFIG_PM
static int pcidev_resume(struct pci_dev *pdev)
{
	int i;
	struct platform_info *info = pci_get_drvdata(pdev);
	struct dev_info *hw_priv = &info->dev_info;
	struct ksz_hw *hw = &hw_priv->hw;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_enable_wake(pdev, PCI_D0, 0);

	if (hw_priv->wol_enable)
		hw_cfg_wol_pme(hw, 0);
	for (i = 0; i < hw->dev_count; i++) {
		if (info->netdev[i]) {
			struct net_device *dev = info->netdev[i];

			if (netif_running(dev)) {
				netdev_open(dev);
				netif_device_attach(dev);
			}
		}
	}
	return 0;
}

static int pcidev_suspend(struct pci_dev *pdev, pm_message_t state)
{
	int i;
	struct platform_info *info = pci_get_drvdata(pdev);
	struct dev_info *hw_priv = &info->dev_info;
	struct ksz_hw *hw = &hw_priv->hw;

	/* Need to find a way to retrieve the device IP address. */
	static const u8 net_addr[] = { 192, 168, 1, 1 };

	for (i = 0; i < hw->dev_count; i++) {
		if (info->netdev[i]) {
			struct net_device *dev = info->netdev[i];

			if (netif_running(dev)) {
				netif_device_detach(dev);
				netdev_close(dev);
			}
		}
	}
	if (hw_priv->wol_enable) {
		hw_enable_wol(hw, hw_priv->wol_enable, net_addr);
		hw_cfg_wol_pme(hw, 1);
	}

	pci_save_state(pdev);
	pci_enable_wake(pdev, pci_choose_state(pdev, state), 1);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	return 0;
}
#endif

static char pcidev_name[] = "ksz884xp";

static struct pci_device_id pcidev_table[] = {
	{ PCI_VENDOR_ID_MICREL_KS, 0x8841,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_MICREL_KS, 0x8842,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, pcidev_table);

static struct pci_driver pci_device_driver = {
#ifdef CONFIG_PM
	.suspend	= pcidev_suspend,
	.resume		= pcidev_resume,
#endif
	.name		= pcidev_name,
	.id_table	= pcidev_table,
	.probe		= pcidev_init,
	.remove		= pcidev_exit
};

static int __init ksz884x_init_module(void)
{
	return pci_register_driver(&pci_device_driver);
}

static void __exit ksz884x_cleanup_module(void)
{
	pci_unregister_driver(&pci_device_driver);
}

module_init(ksz884x_init_module);
module_exit(ksz884x_cleanup_module);

MODULE_DESCRIPTION("KSZ8841/2 PCI network driver");
MODULE_AUTHOR("Tristram Ha <Tristram.Ha@micrel.com>");
MODULE_LICENSE("GPL");

module_param_named(message, msg_enable, int, 0);
MODULE_PARM_DESC(message, "Message verbosity level (0=none, 31=all)");

module_param(macaddr, charp, 0);
module_param(mac1addr, charp, 0);
module_param(fast_aging, int, 0);
module_param(multi_dev, int, 0);
module_param(stp, int, 0);
MODULE_PARM_DESC(macaddr, "MAC address");
MODULE_PARM_DESC(mac1addr, "Second MAC address");
MODULE_PARM_DESC(fast_aging, "Fast aging");
MODULE_PARM_DESC(multi_dev, "Multiple device interfaces");
MODULE_PARM_DESC(stp, "STP support");
