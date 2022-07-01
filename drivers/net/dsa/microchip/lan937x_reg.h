/* SPDX-License-Identifier: GPL-2.0 */
/* Microchip LAN937X switch register definitions
 * Copyright (C) 2019-2021 Microchip Technology Inc.
 */
#ifndef __LAN937X_REG_H
#define __LAN937X_REG_H

#define PORT_CTRL_ADDR(port, addr)	((addr) | (((port) + 1)  << 12))

/* 0 - Operation */
#define REG_SW_INT_STATUS__4		0x0010
#define REG_SW_INT_MASK__4		0x0014

#define LUE_INT				BIT(31)
#define TRIG_TS_INT			BIT(30)
#define APB_TIMEOUT_INT			BIT(29)
#define OVER_TEMP_INT			BIT(28)
#define HSR_INT				BIT(27)
#define PIO_INT				BIT(26)
#define POR_READY_INT			BIT(25)

#define SWITCH_INT_MASK			\
	(LUE_INT | TRIG_TS_INT | APB_TIMEOUT_INT | OVER_TEMP_INT | HSR_INT | \
	 PIO_INT | POR_READY_INT)

#define REG_SW_PORT_INT_STATUS__4	0x0018
#define REG_SW_PORT_INT_MASK__4		0x001C

/* 1 - Global */
#define REG_SW_GLOBAL_OUTPUT_CTRL__1	0x0103
#define SW_CLK125_ENB			BIT(1)
#define SW_CLK25_ENB			BIT(0)

/* 3 - Operation Control */
#define REG_SW_OPERATION		0x0300

#define SW_DOUBLE_TAG			BIT(7)
#define SW_OVER_TEMP_ENABLE		BIT(2)
#define SW_RESET			BIT(1)

#define REG_SW_LUE_CTRL_0		0x0310

#define SW_VLAN_ENABLE			BIT(7)
#define SW_DROP_INVALID_VID		BIT(6)
#define SW_AGE_CNT_M			0x7
#define SW_AGE_CNT_S			3
#define SW_RESV_MCAST_ENABLE		BIT(2)

#define REG_SW_LUE_CTRL_1		0x0311

#define UNICAST_LEARN_DISABLE		BIT(7)
#define SW_FLUSH_STP_TABLE		BIT(5)
#define SW_FLUSH_MSTP_TABLE		BIT(4)
#define SW_SRC_ADDR_FILTER		BIT(3)
#define SW_AGING_ENABLE			BIT(2)
#define SW_FAST_AGING			BIT(1)
#define SW_LINK_AUTO_AGING		BIT(0)

#define REG_SW_MAC_CTRL_0		0x0330
#define SW_NEW_BACKOFF			BIT(7)
#define SW_PAUSE_UNH_MODE		BIT(1)
#define SW_AGGR_BACKOFF			BIT(0)

#define REG_SW_MAC_CTRL_1		0x0331
#define SW_SHORT_IFG			BIT(7)
#define MULTICAST_STORM_DISABLE		BIT(6)
#define SW_BACK_PRESSURE		BIT(5)
#define FAIR_FLOW_CTRL			BIT(4)
#define NO_EXC_COLLISION_DROP		BIT(3)
#define SW_LEGAL_PACKET_DISABLE		BIT(1)
#define SW_PASS_SHORT_FRAME		BIT(0)

#define REG_SW_MAC_CTRL_6		0x0336
#define SW_MIB_COUNTER_FLUSH		BIT(7)
#define SW_MIB_COUNTER_FREEZE		BIT(6)

/* 4 - LUE */
#define REG_SW_ALU_STAT_CTRL__4		0x041C

#define REG_SW_ALU_VAL_B		0x0424
#define ALU_V_OVERRIDE			BIT(31)
#define ALU_V_USE_FID			BIT(30)
#define ALU_V_PORT_MAP			0xFF

/* Port Registers */

/* 0 - Operation */
#define REG_PORT_CTRL_0			0x0020

#define PORT_MAC_LOOPBACK		BIT(7)
#define PORT_MAC_REMOTE_LOOPBACK	BIT(6)
#define PORT_K2L_INSERT_ENABLE		BIT(5)
#define PORT_K2L_DEBUG_ENABLE		BIT(4)
#define PORT_TAIL_TAG_ENABLE		BIT(2)
#define PORT_QUEUE_SPLIT_ENABLE		0x3

/* 3 - xMII */
#define REG_PORT_XMII_CTRL_0		0x0300
#define PORT_SGMII_SEL			BIT(7)
#define PORT_MII_FULL_DUPLEX		BIT(6)
#define PORT_MII_TX_FLOW_CTRL		BIT(5)
#define PORT_MII_100MBIT		BIT(4)
#define PORT_MII_RX_FLOW_CTRL		BIT(3)
#define PORT_GRXC_ENABLE		BIT(0)

/* 4 - MAC */
#define REG_PORT_MAC_CTRL_0		0x0400
#define PORT_CHECK_LENGTH		BIT(2)
#define PORT_BROADCAST_STORM		BIT(1)
#define PORT_JUMBO_PACKET		BIT(0)

#define REG_PORT_MAC_CTRL_1		0x0401
#define PORT_BACK_PRESSURE		BIT(3)
#define PORT_PASS_ALL			BIT(0)

/* 8 - Classification and Policing */
#define REG_PORT_MRI_PRIO_CTRL		0x0801
#define PORT_HIGHEST_PRIO		BIT(7)
#define PORT_OR_PRIO			BIT(6)
#define PORT_MAC_PRIO_ENABLE		BIT(4)
#define PORT_VLAN_PRIO_ENABLE		BIT(3)
#define PORT_802_1P_PRIO_ENABLE		BIT(2)
#define PORT_DIFFSERV_PRIO_ENABLE	BIT(1)
#define PORT_ACL_PRIO_ENABLE		BIT(0)

#define P_PRIO_CTRL			REG_PORT_MRI_PRIO_CTRL

#endif
