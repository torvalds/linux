/*
 * TC956X ethernet driver.
 *
 * common.h - Common Header File
 *
 * Copyright (C) 2007-2009 STMicroelectronics Ltd
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *
 *  05 Jul 2021 : 1. Used Systick handler instead of Driver kernel timer to process transmitted Tx descriptors.
 *                2. XFI interface support and module parameters for selection of Port0 and Port1 interface
 *  VERSION     : 01-00-01
 *  15 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported without module parameter
 *  VERSION     : 01-00-02
 *  20 Jul 2021 : CONFIG_DEBUG_FS_TC956X removed and renamed as CONFIG_DEBUG_FS
 *  VERSION     : 01-00-03
 *  22 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported with module parameters
 *  VERSION     : 01-00-04
 *  23 Jul 2021 : 1. Enable DMA IPA OFFLOAD and FRP by default
 *  VERSION     : 01-00-06
 *  02 Sep 2021 : 1. Configuration of Link state L0 and L1 transaction delay for PCIe switch ports & Endpoint.
 *  VERSION     : 01-00-11
 *  23 Sep 2021 : 1. Enabling MSI MASK for MAC EVENT Interrupt to process RBU status and update to ethtool statistics
 *  VERSION     : 01-00-14
 *  14 Oct 2021 : 1. Moving common Macros to common header file 
 *  VERSION     : 01-00-16
 *  19 Oct 2021 : 1. Adding M3 SRAM Debug counters to ethtool statistics
 *                2. Adding MTL RX Overflow/packet miss count, TX underflow counts,Rx Watchdog value to ethtool statistics.
 *  VERSION     : 01-00-17
 *  21 Oct 2021 : 1. Added support for GPIO configuration API
 *  VERSION     : 01-00-18
 *  26 Oct 2021 : 1. Added macro to enable/disable EEE.
		: 2. Added enums for PM Suspend-Resume.
		: 3. Added macros for EEE, LPI Timer and MAC RST Status.
 *  VERSION     : 01-00-19
 *  04 Nov 2021 : 1. Disabled link state latency configuration for all PCIe ports by default
 *  VERSION     : 01-00-20
 *  08 Nov 2021 : 1. Added macro for Maximum Port
 *  VERSION     : 01-00-21
 *  24 Nov 2021 : 1. Single Port Suspend/Resume supported
 *  VERSION     : 01-00-22
 *  24 Nov 2021 : 1. EEE update for runtime configuration and LPI interrupt disabled.
 *  VERSION     : 01-00-24
 *  08 Dec 2021 : 1. Added Macro for Maximum Tx, Rx Queue Size and byte size.
 *  VERSION     : 01-00-30
 *  10 Dec 2021 : 1. Added link partner pause frame count debug counters to ethtool statistics.
 *  VERSION     : 01-00-31
 *  27 Dec 2021 : 1. Support for eMAC Reset and unused clock disable during Suspend and restoring it back during resume.
 *  VERSION     : 01-00-32
 *  24 Jan 2022 : 1. Set Clock control and Reset control register to default value on driver unload.
 *  VERSION     : 01-00-38
 *  31 Jan 2022 : 1. Additional macros defined for debug dump API usage.
 *  VERSION     : 01-00-39
 *  04 Feb 2021 : 1. Ethtool statistics added to print doorbell SRAM area for all the channels.
 *  VERSION     : 01-00-41
 *  14 Feb 2021 : 1. Reset assert and clock disable support during Link Down.
 *  VERSION     : 01-00-42
 *  06 Apr 2022 : 1.Max MTU supported is 2000 bytes.
 *  VERSION     : 01-00-48
*/

#ifndef __COMMON_H__
#define __COMMON_H__

#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include "tc956xmac_inc.h"
#include <linux/phy.h>
#include <linux/module.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>
#if IS_ENABLED(CONFIG_VLAN_8021Q)
#define TC956XMAC_VLAN_TAG_USED
#include <linux/if_vlan.h>
#endif

#include "descs.h"
#include "hwif.h"
#include "mmc.h"
#ifdef TC956X
#ifndef TC956X_SRIOV_VF
#include "tc956x_xpcs.h"
#endif
#include "tc956x_pma.h"
#endif

/* Enable DMA IPA offload */
#define TC956X_DMA_OFFLOAD_ENABLE
//#define TC956X_LPI_INTERRUPT
/* Indepenedent Suspend/Resume Debug */
#undef TC956X_PM_DEBUG
#define TC956X_MAX_PORT			2
#define TC956X_ALL_MAC_PORT_SUSPENDED	0 /* All EMAC Port Suspended. To be used just after suspend and before resume. */
#define TC956X_NO_MAC_DEVICE_IN_USE	0 /* No EMAC Port in use. To be used at probe and remove. */
#define TC956X_SINGLE_MAC_DEVICE_IN_USE	1 /* One of the EMAC Port in use. To be used at remove. */
#define TC956X_ALL_MAC_PORT_LINK_DOWN	2 /* All ports are Link Down */
/* Suspend-Resume Arguments */
enum TC956X_PORT_PM_STATE {
	SUSPEND = 0,
	RESUME,
};

/* Link Down Change Power State Arguments */
enum TC956X_PORT_LINK_CHANGE_STATE {
	LINK_DOWN = 0,
	LINK_UP,
};
#if defined(TC956X_SRIOV_PF)
//#define TC956X_PCIE_LINK_STATE_LATENCY_CTRL
#define TC956X_PCIE_DSP_CUT_THROUGH
#endif
#define DISABLE		0x0U
#define ENABLE		0x1U
#define SIZE_512B	0x200U
#define SIZE_1KB	0x400U

/* Synopsys Core versions */
#define DWMAC_CORE_3_40		0x34
#define DWMAC_CORE_3_50		0x35
#define DWMAC_CORE_4_00		0x40
#define DWMAC_CORE_4_10		0x41
#define DWMAC_CORE_5_00		0x50
#define DWMAC_CORE_5_10		0x51
#define DWXGMAC_CORE_2_10	0x21
#define DWXGMAC_CORE_3_01	0x30

//#define DISABLE_EMAC_PORT1
#ifndef TC956X_SRIOV_VF
#define EEE /* Enable for EEE support */
#endif

#define TC956X_MAC_ADDR_CNT 6
/* Note: Multiple macro definitions for TC956X_PCIE_LOGSTAT and TC956X_PCIE_LOGSTAT_SUMMARY_ENABLE.
 * Please also define/undefine same macro in tc956xmac_ioctl.h, if changing in this file
 */
/* #undef TC956X_PCIE_LOGSTAT */
#define TC956X_PCIE_LOGSTAT
#ifdef TC956X_PCIE_LOGSTAT
/* Enable/Disable Logstat Summary Print during probe and resume. */
#undef TC956X_PCIE_LOGSTAT_SUMMARY_ENABLE
#endif

/* This macro to be enabled for loading cbs based on speed change */
// #define TC956X_DYNAMIC_LOAD_CBS

/* This macro to be enabled while running TSN demo Application */
// #define TSN_DEMO_AUTOMOTIVE

#define TC956X_M3_DMEM_OFFSET	0x40000
#define MAC2MAC_ETH0_RXDESC_L	0x7800
#define MAC2MAC_ETH0_RXDESC_H	0x7804
#define MAC2MAC_ETH1_RXDESC_L	0xB800
#define MAC2MAC_ETH1_RXDESC_H	0xB804

//#define   TX_LOGGING_TRACE
//#define   RX_LOGGING_TRACE
#define TC956XMAC_CHAN0	0  /* Always supported and default for all chips */
//#define TC956X_SRIOV_PF
#define TC956X_SRIOV_LOCK
//#define TC956X_SRIOV_DEBUG


#define TC956XMAC_CHA_NO_0	BIT(0)
#define TC956XMAC_CHA_NO_1	BIT(1)
#define TC956XMAC_CHA_NO_2	BIT(2)
#define TC956XMAC_CHA_NO_3	BIT(3)

#ifdef TC956X_SRIOV_PF
/* VF number for each VF Application */
#define TC956XMAC_VF_IVI 1
#define TC956XMAC_VF_TCU 2
#define TC956XMAC_VF_ADAS 3

#if defined(TC956X_AUTOMOTIVE_CONFIG) || defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
/* Legacy channel for each VF Application */
#define TC956XMAC_CHA_IVI TC956XMAC_CHA_NO_0
#define TC956XMAC_CHA_TCU TC956XMAC_CHA_NO_0
#define TC956XMAC_CHA_ADAS TC956XMAC_CHA_NO_0
#define TC956XMAC_CHA_PF TC956XMAC_CHA_NO_0
#if defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
#define TC956XMAC_CHA_M2M TC956XMAC_CHA_NO_2
#endif
#else
/* Legacy channel for each VF Application */
#define TC956XMAC_CHA_IVI TC956XMAC_CHA_NO_0
#define TC956XMAC_CHA_TCU TC956XMAC_CHA_NO_1
#define TC956XMAC_CHA_ADAS TC956XMAC_CHA_NO_2
#define TC956XMAC_CHA_PF TC956XMAC_CHA_NO_3
#endif
#endif

#if defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
#define TC956X_TOT_MSI_VEC	2
#else
#define TC956X_TOT_MSI_VEC	1
#endif

#define TC956X_MSI_VECTOR_0	0
#define TC956X_MSI_VECTOR_1	1

#define ETH_DMA_DUMP_OFFSET1 (0x3000 / 4)
#define ETH_DMA_DUMP_OFFSET1_END   (0x30A0 / 4)
#define ETH_DMA_DUMP_OFFSET2 (0x3100 / 4)

#define ETH_CORE_DUMP_OFFSET1     (0x0)
#define ETH_CORE_DUMP_OFFSET1_END (0xe0 / 4)
#define ETH_CORE_DUMP_OFFSET2     (0x110 / 4)
#define ETH_CORE_DUMP_OFFSET2_END (0xdd0 / 4)
#define ETH_CORE_DUMP_OFFSET3     (0x1000 / 4)
#define ETH_CORE_DUMP_OFFSET3_END (0x10f4 / 4)
#define ETH_CORE_DUMP_OFFSET4     (0x1100 / 4)
#define ETH_CORE_DUMP_OFFSET4_END (0x1108 / 4)
#define ETH_CORE_DUMP_OFFSET5     (0x1110 / 4)
#define ETH_CORE_DUMP_OFFSET5_END (0x1124 / 4)
#define ETH_CORE_DUMP_OFFSET6     (0x1140 / 4)
#define ETH_CORE_DUMP_OFFSET6_END (0x1174 / 4)

/* 0x20004000 to 0x2000401C is for Port0, 0x20004020 to 0x2000403C is for Port1 */
#define SRAM_TX_PCIE_ADDR_LOC	0x44000

/* 0x20004040 to 0x2000405C is for Port0, 0x20004060 to 0x2000407C is for Port1 */
#define SRAM_RX_PCIE_ADDR_LOC	0x44040

#ifdef CONFIG_DEBUG_FS

#ifdef TC956X
int tc956xmac_init(void);
#endif

#ifdef TC956X
void tc956xmac_exit(void);
#endif

#endif

#ifdef TC956X_SRIOV_PF
/* TC956X Semaphore lock/unlock register */
#define NSEM(reg)			(0x1100 + (reg * 4))
/* TC956X Semaphore read register */
#define NSEM_RD(reg)			(0x1140 + (reg * 4))

#define MBX_MSG_OFST		4 /* First 4 bytes for ACK management */
#define MBX_MSG_SIZE		60 /* Excluding ACK */
#define MBX_ACK_SIZE		4 /* ACK management */
#define MBX_TOT_SIZE		(MBX_MSG_SIZE + MBX_ACK_SIZE)

#define ACK			0x01
#define NACK			0x02

#define MAX_NO_OF_VFS 3

/* Mailbox opcode definitions */
#define OPCODE_MBX_ADD_MAC_ADDR		0x01
#define OPCODE_MBX_SET_TX_Q_WEIGHT	0x02
#define OPCODE_MBX_CFG_CBS		0x03
#define OPCODE_MBX_SET_TX_Q_PRIOR	0x04
#define OPCODE_MBX_RESET_EEE_MODE       0x05
#define OPCODE_MBX_VF_IOCTL		        0x06
#define OPCODE_MBX_VF_ETHTOOL		    0x07
#define OPCODE_MBX_VF_ADD_MAC		    0x08
#define OPCODE_MBX_VF_DELETE_MAC	    0x09
#define OPCODE_MBX_VF_ADD_VLAN		    0x10
#define OPCODE_MBX_VF_DELETE_VLAN	    0x11
#define OPCODE_MBX_SET_DMA_TX_MODE	0x12
#define OPCODE_MBX_VF_GET_LINK_STATUS	0x13
#define OPCODE_MBX_PHY_LINK		0x14
#define OPCODE_MBX_SETUP_CBS		0x15
#define OPCODE_MBX_RX_CRC		0x16
#define OPCODE_MBX_RX_CSUM		0x17
#define OPCODE_MBX_DRV_CAP		0x18
#define OPCODE_MBX_VF_GET_MII_PHY		0x23
#define TC956XMAC_GET_PAUSE_PARAM_2		0x24
#define OPCODE_MBX_GET_UMAC_ADDR        0x25
#define OPCODE_VF_RESET                 0x26
#define OPCODE_MBX_DMA_CH_TLPTR		0x29
#define OPCODE_MBX_SETUP_ETF		    0x30
#define OPCODE_MBX_FLR					0x33
#define OPCODE_MBX_DMA_ERR		0x34
#define OPCODE_MBX_ACK_MSG		0xFF

#define SIZE_MBX_SET_TX_Q_WEIGHT		8
#define SIZE_MBX_CFG_CBS			20
#define SIZE_MBX_SETUP_ETF			5
#define SIZE_MBX_CFG_EST			540 /* 28 + (4*128) */
#define SIZE_MBX_CFG_FPE			20
#define SIZE_MBX_SETUP_CBS				21
#define SIZE_MBX_SET_TX_Q_PRIOR			8
#define SIZE_MBX_SET_DMA_TX_MODE		13
#define SIZE_MBX_VF_GET_LINK_STATUS		0
#define SIZE_MBX_PHY_LINK		12
#define SIZE_MBX_RX_CRC		4
#define SIZE_MBX_RX_CSUM		4
#define SIZE_MBX_DRV_CAP		        4
#define SIZE_GET_UMAC_ADDR              4
#define SIZE_SET_UMAC_ADDR              10
#define SIZE_MBX_VF_MAC				6
#define SIZE_MBX_VF_VLAN				2
#define SIZE_MBX_VF_REG_WR			13
#define SIZE_MBX_VF_SPEED			1
#define SIZE_MBX_SET_GET_CBS_1			56
#define SIZE_MBX_SET_GET_RXP_1		56
#define SIZE_MBX_SET_GET_CBS_2			44
#define SIZE_MBX_VF_PAUSE_PARAM		1
#define SIZE_MBX_VF_EEE				1
#define SIZE_MBX_VF_TS_INFO			1
#define SIZE_VF_RESET			    1
#define SIZE_MBX_VF_TIMESTAMP		2
#define SIZE_MBX_SET_GET_EST_1		56
#define SIZE_MBX_RX_DMA_TL_PTR		4
#define SIZE_MBX_SET_GET_FPE_1    20
#define SIZE_MBX_GET_HW_STMP    12

#define MAX_SIZE_GCL_MSG 14
#define EST_FIX_MSG_LEN 28
#define RXP_FIX_MSG_LEN 16
#define VF_UP         1
#define VF_DOWN       0
#define VF_SUSPEND    2
#define VF_RELEASE    3

#define SCH_WQ_PHY_REG_RD 1
#define SCH_WQ_RX_DMA_ERR 2
#define SCH_WQ_GET_PAUSE_PARAM 3

#endif /* #ifdef TC956X_SRIOV_PF */

#ifdef TC956X_SRIOV_VF
#define TC956X_MIN_MSI_VEC	1
#define TC956X_MAX_MSI_VEC	1

#define TC956X_MSI_VECTOR_0	0
#define TC956X_MSI_VECTOR_1	1
#endif

enum mbx_msg_fns {
	mcu = 0,
	pf0 = 1,
	pf1 = 2,
	vf0 = 3,
	vf1 = 4,
	vf2 = 5
};

struct fn_id {
	bool fn_type; /*0 - PF; 1-VF*/
	u8 pf_no; /* PF number [0-1], In case of VF, this gives information
		   * about associated PF
		   */
	u8 vf_no; /*VF number [1-3]*/
};


/* Packets types */
enum packets_types {
	PACKET_AVCPQ = 0x1, /* AV Untagged Control packets */
	PACKET_PTPQ = 0x2, /* PTP Packets */
	PACKET_DCBCPQ = 0x3, /* DCB Control Packets */
	PACKET_UPQ = 0x4, /* Untagged Packets */
	PACKET_MCBCQ = 0x5, /* Multicast & Broadcast Packets */
#ifdef TC956X
	PACKET_FPE_RESIDUE = 0x6, /* Frame Pre-emption residue packets */
#endif
#if defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
	PACKET_FILTER_FAIL = 0x7, /* Filter Fail packets */
#endif
};

//#define TX_LOGGING_TRACE

/*	Dual Port related Macros	*/
#define RM_PF0_ID		(0)
#define RM_PF1_ID		(1)
#define RM_IS_PF		(0)
#define RM_IS_VF		(1)
#define MAC_PORT_NUM_CHECK	(priv->port_num == RM_PF0_ID)

/* Select the MDC range based on PHY specification.
 * IEEE recommends max of 2.5MHz. But if PHY supports more than that, then it can be used
 */
#define PORT0_MDC	TC956XMAC_XGMAC_MDC_CSR_12
#define PORT1_MDC	TC956XMAC_XGMAC_MDC_CSR_62

#ifdef TC956X
#define PORT0_C45_STATE		true
#define PORT1_C45_STATE		false
#endif

#if defined(TX_LOGGING_TRACE)
#define PACKET_IPG      125000
#define PACKET_CDT_IPG  500000
#endif

#define TC956X_M3_FW_EXIT_VALUE		2

#ifdef TC956X

#define TC956X_AVB_PRIORITY_CLASS_A	(3)
#define TC956X_AVB_PRIORITY_CLASS_B	(2)
#define TC956X_PRIORITY_CLASS_CDT	(7)

/* skip the addresses added during hw setup*/

/*
 * Note: If source address (SA) replacement or SA insertion feature is
 * supported, MAC_ADDR_ADD_SKIP_OFST should be increased accordingly
 */
#define TC956X_MAX_PERFECT_ADDRESSES	32
#define TC956X_MAX_PERFECT_VLAN		16

#if defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
#define XGMAC_ADDR_ADD_SKIP_OFST	6
#else
#define XGMAC_ADDR_ADD_SKIP_OFST	3
#endif


#define TC956X_MAC_STATE_VACANT		0x0
#define TC956X_MAC_STATE_OCCUPIED	0x1
#define TC956X_MAC_STATE_NEW			0x2
#define TC956X_MAC_STATE_MODIFIED	0x4


/* C45 registers for Port0 PHY */

/* CL45_CTRL_REG is the equivalent of CL22 BMCR */
#define PHY0_CL45_CTRL_REG_MMD_BANK      (7)
#define PHY0_CL45_CTRL_REG_ADDR          (0x0)

/* CL45_STATUS_REG is the equivalent of CL22 BMSR */
#define PHY0_CL45_STATUS_REG_MMD_BANK    (7)
#define PHY0_CL45_STATUS_REG_ADDR        (0x1)

/* CL45_PHYID1 is the equivalent of CL22 PHYIDR1 */
#define PHY0_CL45_PHYID1_MMD_BANK        (1)
#define PHY0_CL45_PHYID1_ADDR            (2)

/* CL45_PHYID1 is the equivalent of CL22 PHYIDR2 */
#define PHY0_CL45_PHYID2_MMD_BANK        (1)
#define PHY0_CL45_PHYID2_ADDR            (3)

#define PHY0_CL45_PHYID1_REG	\
((PHY0_CL45_PHYID1_MMD_BANK << 16) | (PHY0_CL45_PHYID1_ADDR))

#define PHY0_CL45_PHYID2_REG	\
((PHY0_CL45_PHYID2_MMD_BANK << 16) | (PHY0_CL45_PHYID2_ADDR))


/* C45 registers for Port1 PHY */

/* CL45_CTRL_REG is the equivalent of CL22 BMCR */
#define PHY1_CL45_CTRL_REG_MMD_BANK      (7)
#define PHY1_CL45_CTRL_REG_ADDR          (0x0)

/* CL45_STATUS_REG is the equivalent of CL22 BMSR */
#define PHY1_CL45_STATUS_REG_MMD_BANK    (7)
#define PHY1_CL45_STATUS_REG_ADDR        (0x1)

/* CL45_PHYID1 is the equivalent of CL22 PHYIDR1 */
#define PHY1_CL45_PHYID1_MMD_BANK        (1)
#define PHY1_CL45_PHYID1_ADDR            (2)

/* CL45_PHYID1 is the equivalent of CL22 PHYIDR2 */
#define PHY1_CL45_PHYID2_MMD_BANK        (1)
#define PHY1_CL45_PHYID2_ADDR            (3)

#define PHY1_CL45_PHYID1_REG	\
((PHY1_CL45_PHYID1_MMD_BANK << 16) | (PHY1_CL45_PHYID1_ADDR))

#define PHY1_CL45_PHYID2_REG	\
((PHY1_CL45_PHYID2_MMD_BANK << 16) | (PHY1_CL45_PHYID2_ADDR))


/* CL45_CTRL_REG is the equivalent of CL22 BMCR */
#define PHY_CL45_CTRL_REG_MMD_BANK		((MAC_PORT_NUM_CHECK) ? \
						(PHY0_CL45_CTRL_REG_MMD_BANK) : (PHY1_CL45_CTRL_REG_MMD_BANK))

#define PHY_CL45_CTRL_REG_ADDR			((MAC_PORT_NUM_CHECK) ? \
						(PHY0_CL45_CTRL_REG_ADDR) : (PHY1_CL45_CTRL_REG_ADDR))

/* CL45_STATUS_REG is the equivalent of CL22 BMSR */
#define PHY_CL45_STATUS_REG_MMD_BANK	((MAC_PORT_NUM_CHECK) ? \
						(PHY0_CL45_STATUS_REG_MMD_BANK) : (PHY1_CL45_STATUS_REG_MMD_BANK))

#define PHY_CL45_STATUS_REG_ADDR		((MAC_PORT_NUM_CHECK) ? \
						(PHY0_CL45_STATUS_REG_ADDR) : (PHY1_CL45_STATUS_REG_ADDR))

/* CL45_PHYID1 is the equivalent of CL22 PHYIDR1 */
#define PHY_CL45_PHYID1_MMD_BANK		((MAC_PORT_NUM_CHECK) ? \
						(PHY0_CL45_PHYID1_MMD_BANK) : (PHY1_CL45_PHYID1_MMD_BANK))

#define PHY_CL45_PHYID1_ADDR		((MAC_PORT_NUM_CHECK) ? \
						(PHY0_CL45_PHYID1_ADDR) : (PHY1_CL45_PHYID1_ADDR))

/* CL45_PHYID1 is the equivalent of CL22 PHYIDR2 */
#define PHY_CL45_PHYID2_MMD_BANK		((MAC_PORT_NUM_CHECK) ? \
						(PHY0_CL45_PHYID2_MMD_BANK) : (PHY1_CL45_PHYID2_MMD_BANK))

#define PHY_CL45_PHYID2_ADDR		((MAC_PORT_NUM_CHECK) ? \
						(PHY0_CL45_PHYID2_ADDR) : (PHY1_CL45_PHYID2_ADDR))

#define PHY_CL45_PHYID1_REG		((MAC_PORT_NUM_CHECK) ? \
						(PHY0_CL45_PHYID1_REG) : (PHY1_CL45_PHYID1_REG))

#define PHY_CL45_PHYID2_REG		((MAC_PORT_NUM_CHECK) ? \
						(PHY0_CL45_PHYID2_REG) : (PHY1_CL45_PHYID2_REG))

#define NFUNCEN4_OFFSET			(0x1528)

#define MAX_RX_QUEUE_SIZE	47104 /* 46KB Maximun RX Queue size */
#define MAX_TX_QUEUE_SIZE	47104 /* 46KB Maximun TX Queue size */

#if defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)

/************************ TC956X_SRIOV_PF Starts ************************/

#define TC956X_DMA_RX_AIS  BIT(14)
#define TC956X_DMA_RX_FBE  BIT(12)
#define TC956X_DMA_RX_RPS  BIT(8)
#define MAX_TX_QUEUES_TO_USE	8
#define MAX_RX_QUEUES_TO_USE	8

/* Tx Queue Size*/
#define TX_QUEUE0_SIZE	4096
#define TX_QUEUE1_SIZE	4096
#define TX_QUEUE2_SIZE	18432
#define TX_QUEUE3_SIZE	4096
#define TX_QUEUE4_SIZE	1024
#define TX_QUEUE5_SIZE	4096
#define TX_QUEUE6_SIZE	4096
#define TX_QUEUE7_SIZE	4096

/* TX Queue 0: Legacy and Jumbo packets */
#define TX_QUEUE0_MODE		MTL_QUEUE_DCB
/* TX Queue 1:Legacy*/
#define TX_QUEUE1_MODE		MTL_QUEUE_DCB
/* TX Queue 2: Legacy */
#define TX_QUEUE2_MODE		MTL_QUEUE_DCB
/* TX Queue 3: Legacy */
#define TX_QUEUE3_MODE		MTL_QUEUE_DCB
/* TX Queue 4: Untagged PTP */
#define TX_QUEUE4_MODE		MTL_QUEUE_DCB
/* TX Queue 5: AVB Class B AVTP packet */
#define TX_QUEUE5_MODE		MTL_QUEUE_AVB
/* TX Queue 6: AVB Class A AVTP packet */
#define TX_QUEUE6_MODE		MTL_QUEUE_AVB
/* TX Queue 7: TSN Class CDT packet */
#if defined(TSN_DEMO_AUTOMOTIVE)
#define TX_QUEUE7_MODE		MTL_QUEUE_DCB
#else
#define TX_QUEUE7_MODE		MTL_QUEUE_AVB
#endif

/* Tx Queue TBS Enable/Disable */
#define TX_QUEUE0_TBS		0
#define TX_QUEUE1_TBS		0
#define TX_QUEUE2_TBS		0
#define TX_QUEUE3_TBS		0
#define TX_QUEUE4_TBS		0
#define TX_QUEUE5_TBS		1
#define TX_QUEUE6_TBS		1
#define TX_QUEUE7_TBS		1

/* Tx Queue TSO Enable/Disable */
#define TX_QUEUE0_TSO		1
#define TX_QUEUE1_TSO		1
#define TX_QUEUE2_TSO		1
#define TX_QUEUE3_TSO		1
#define TX_QUEUE4_TSO		0
#define TX_QUEUE5_TSO		0
#define TX_QUEUE6_TSO		0
#define TX_QUEUE7_TSO		0

/* Configure TxQueue - Traffic Class mapping */
#define TX_QUEUE0_TC	0x0
#define TX_QUEUE1_TC	0x0
#define TX_QUEUE2_TC	0x0
#define TX_QUEUE3_TC	0x0
#define TX_QUEUE4_TC	0x1
#define TX_QUEUE5_TC	0x2
#define TX_QUEUE6_TC	0x3
#define TX_QUEUE7_TC	0x4


/*
 * RX Queue 0: Unicast/Untagged Packets - Packets with
 * unique MAC Address of Host/Guest OS DMA channel selection will be based on
 * MAC_Address(#i)_High.DCS
 */
#define RX_QUEUE0_MODE		MTL_QUEUE_DCB
/* RX Queue 1: VLAN Tagged Legacy packets- Pkt routing will be based on VLAN */
#define RX_QUEUE1_MODE		MTL_QUEUE_DCB
/* RX Queue 2: Untagged gPTP packets */
#define RX_QUEUE2_MODE		MTL_QUEUE_DCB
/* RX Queue 3: Filter Fail packet queue */
#define RX_QUEUE3_MODE		MTL_QUEUE_DCB
/* RX Queue 4: AVB Class B AVTP packets */
#define RX_QUEUE4_MODE		MTL_QUEUE_AVB
/* RX Queue 5: AVB Class A AVTP packets */
#define RX_QUEUE5_MODE		MTL_QUEUE_AVB
/* RX Queue 6:TSN  Class CDT packets */
#define RX_QUEUE6_MODE		MTL_QUEUE_AVB
/* RX Queue 7: Broadcast/Multicast packets */
#define RX_QUEUE7_MODE		MTL_QUEUE_DCB

/* Rx Queue Size */
#define RX_QUEUE0_SIZE	18432
#define RX_QUEUE1_SIZE	4096
#define RX_QUEUE2_SIZE	1024
#define RX_QUEUE3_SIZE	4096
#define RX_QUEUE4_SIZE	4096
#define RX_QUEUE5_SIZE	4096
#define RX_QUEUE6_SIZE	4096
#define RX_QUEUE7_SIZE	4096

/* Rx Queue Packet Routing */
#define RX_QUEUE0_PKT_ROUTE	PACKET_UPQ
#define RX_QUEUE1_PKT_ROUTE	0
#define RX_QUEUE2_PKT_ROUTE	PACKET_PTPQ
#define RX_QUEUE3_PKT_ROUTE	PACKET_FILTER_FAIL
/* Queue 4,5,6 Routed based on Packet Priority Configured in
 * MAC_RxQ_Ctrl2/MAC_RxQ_Ctrl3
 */
#define RX_QUEUE4_PKT_ROUTE	0
#define RX_QUEUE5_PKT_ROUTE	0
#define RX_QUEUE6_PKT_ROUTE	0
#define RX_QUEUE7_PKT_ROUTE	PACKET_MCBCQ


#define TC956X_DA_MAP		0xF

/* Unicast/Untagged packet */
#define LEG_UNTAGGED_PACKET	TC956X_DA_MAP
/* VLAN tagged packets */
#define LEG_TAGGED_PACKET	TC956X_DA_MAP
/* Untagged gPTP packet */
#define UNTAGGED_GPTP_PACKET	4
/* Filter Failed Unicast, Multicast, Tagged packets.
 * Route to PF Best Effort Channel. Used only in SRIOV promiscuous mode
 */
#define FILTER_FAIL_PACKET	3
/* Class B AVB Packet */
#define AVB_CLASS_B_PACKET	5
/* Class A AVB Packet */
#define AVB_CLASS_A_PACKET	6
/* TSN Class CDT Packet */
#define TSN_CLASS_CDT_PACKET	7
/* Broadcast/Multicast packet */
#define BC_MC_PACKET		TC956X_DA_MAP

#define TX_QUEUE0_USE_PRIO	true
#define TX_QUEUE1_USE_PRIO	true
#define TX_QUEUE2_USE_PRIO	true
#define TX_QUEUE3_USE_PRIO	true
#define TX_QUEUE4_USE_PRIO	false
#define TX_QUEUE5_USE_PRIO	false
#define TX_QUEUE6_USE_PRIO	false
#define TX_QUEUE7_USE_PRIO	false

#define TX_QUEUE0_PRIO		0xFF	/* TC0 Priority */
#define TX_QUEUE1_PRIO		0xFF
#define TX_QUEUE2_PRIO		0xFF
#define TX_QUEUE3_PRIO		0xFF
#define TX_QUEUE4_PRIO		0x00
#define TX_QUEUE5_PRIO		0x00
#define TX_QUEUE6_PRIO		0x00
#define TX_QUEUE7_PRIO		0x00

/* Rx Queue Use Priority */
#define RX_QUEUE0_USE_PRIO		false
#define RX_QUEUE1_USE_PRIO		true
#define RX_QUEUE2_USE_PRIO		false
#define RX_QUEUE3_USE_PRIO		false
#define RX_QUEUE4_USE_PRIO		true
#define RX_QUEUE5_USE_PRIO		true
#define RX_QUEUE6_USE_PRIO		true
#define RX_QUEUE7_USE_PRIO		false

/* Rx Queue VLAN tagged Priority mapping */
#define RX_QUEUE0_PRIO		0
#define RX_QUEUE1_PRIO		0xFF
#define RX_QUEUE2_PRIO		0
#define RX_QUEUE3_PRIO		0
#define RX_QUEUE4_PRIO		(1 << TC956X_AVB_PRIORITY_CLASS_B)
#define RX_QUEUE5_PRIO		(1 << TC956X_AVB_PRIORITY_CLASS_A)
#define RX_QUEUE6_PRIO		(1 << TC956X_PRIORITY_CLASS_CDT)
#define RX_QUEUE7_PRIO		0

#define EEPROM_OFFSET		0
#define EEPROM_MAC_COUNT	8

/* DMA Ch allocation in tx-rx pairs for PF & VF */
#define TC956X_PF_CH_ALLOC	0x18	/* PF - Ch 3, 4 */
#define TC956X_VF0_CH_ALLOC	0x21	/* VF0 - Ch 0, 5 */
#define TC956X_VF1_CH_ALLOC	0x02	/* VF1 - Ch 1 */
#define TC956X_VF2_CH_ALLOC	0xC4	/* VF2 - Ch 2, 6, 7 */
/************************* TC956X_SRIOV_PF Ends *************************/


#elif defined(TC956X_SRIOV_PF)

/***************** Automotive and Port Bridge Config Starts *************/


#if defined(TC956X_DMA_OFFLOAD_ENABLE)
#define TX_DMA_CH0_OWNER USE_IN_TC956X_SW
#define TX_DMA_CH1_OWNER NOT_USED
#define TX_DMA_CH2_OWNER USE_IN_TC956X_SW
#define TX_DMA_CH3_OWNER USE_IN_TC956X_SW
#define TX_DMA_CH4_OWNER USE_IN_TC956X_SW
#define TX_DMA_CH5_OWNER USE_IN_TC956X_SW
#define TX_DMA_CH6_OWNER USE_IN_TC956X_SW
#define TX_DMA_CH7_OWNER USE_IN_TC956X_SW

#define RX_DMA_CH0_OWNER USE_IN_TC956X_SW
#define RX_DMA_CH1_OWNER NOT_USED
#define RX_DMA_CH2_OWNER USE_IN_TC956X_SW
#define RX_DMA_CH3_OWNER USE_IN_TC956X_SW
#define RX_DMA_CH4_OWNER USE_IN_TC956X_SW
#define RX_DMA_CH5_OWNER USE_IN_TC956X_SW
#define RX_DMA_CH6_OWNER USE_IN_TC956X_SW
#define RX_DMA_CH7_OWNER USE_IN_TC956X_SW
#endif
#define TC956X_DMA_RX_AIS  BIT(14)
#define TC956X_DMA_RX_FBE  BIT(12)
#define TC956X_DMA_RX_RPS  BIT(8)
#define MAX_TX_QUEUES_TO_USE	8
#define MAX_RX_QUEUES_TO_USE	8

/* Tx Queue Size*/
#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE
#define TX_QUEUE0_SIZE	18432
#define TX_QUEUE1_SIZE	0
#define TX_QUEUE2_SIZE	18432
#define TX_QUEUE3_SIZE	0
#define TX_QUEUE4_SIZE	1024
#define TX_QUEUE5_SIZE	2048
#define TX_QUEUE6_SIZE	2048
#define TX_QUEUE7_SIZE	4096
#else
#define TX_QUEUE0_SIZE	18432
#define TX_QUEUE1_SIZE	14336
#define TX_QUEUE2_SIZE	0
#define TX_QUEUE3_SIZE	0
#define TX_QUEUE4_SIZE	1024
#define TX_QUEUE5_SIZE	4096
#define TX_QUEUE6_SIZE	4096
#define TX_QUEUE7_SIZE	4096
#endif

/* TX Queue 0: Legacy and Jumbo packets */
#define TX_QUEUE0_MODE		MTL_QUEUE_DCB
/* TX Queue 1: Legacy */
#define TX_QUEUE1_MODE		MTL_QUEUE_DCB
/* TX Queue 2: Legacy */
#define TX_QUEUE2_MODE		MTL_QUEUE_DISABLE
/* TX Queue 3: Legacy */
#define TX_QUEUE3_MODE		MTL_QUEUE_DISABLE
/* TX Queue 4: Untagged PTP */
#define TX_QUEUE4_MODE		MTL_QUEUE_DCB
/* TX Queue 5: AVB Class B AVTP packet */
#define TX_QUEUE5_MODE		MTL_QUEUE_AVB
/* TX Queue 6: AVB Class A AVTP packet */
#define TX_QUEUE6_MODE		MTL_QUEUE_AVB
/* TX Queue 7: TSN Class CDT packet */
#if defined(TSN_DEMO_AUTOMOTIVE)
#define TX_QUEUE7_MODE		MTL_QUEUE_DCB
#else
#define TX_QUEUE7_MODE		MTL_QUEUE_AVB
#endif

/* Tx Queue TBS Enable/Disable */
#define TX_QUEUE0_TBS		0
#define TX_QUEUE1_TBS		0
#define TX_QUEUE2_TBS		0
#define TX_QUEUE3_TBS		0
#define TX_QUEUE4_TBS		0
#define TX_QUEUE5_TBS		1
#define TX_QUEUE6_TBS		1
#define TX_QUEUE7_TBS		1

/* Tx Queue TSO Enable/Disable */
#define TX_QUEUE0_TSO		1
#define TX_QUEUE1_TSO		1
#define TX_QUEUE2_TSO		0
#define TX_QUEUE3_TSO		0
#define TX_QUEUE4_TSO		0
#define TX_QUEUE5_TSO		0
#define TX_QUEUE6_TSO		0
#define TX_QUEUE7_TSO		0

/* Configure TxQueue - Traffic Class mapping */
#define TX_QUEUE0_TC	0x0
#define TX_QUEUE1_TC	0x0
#define TX_QUEUE2_TC	0x0
#define TX_QUEUE3_TC	0x0
#define TX_QUEUE4_TC	0x1
#define TX_QUEUE5_TC	0x2
#define TX_QUEUE6_TC	0x3
#define TX_QUEUE7_TC	0x4


/*
 * RX Queue 0: Unicast/Untagged Packets - Packets with
 * unique MAC Address of Host/Guest OS DMA channel selection will be based on
 * MAC_Address(#i)_High.DCS
 */
#define RX_QUEUE0_MODE		MTL_QUEUE_DCB
/* RX Queue 1: VLAN Tagged Legacy packets- Pkt routing will be based on VLAN */
/* Queue is used for MAC2MAC Usecase */
#define RX_QUEUE1_MODE		MTL_QUEUE_DCB
/* RX Queue 2: Untagged gPTP packets */
#define RX_QUEUE2_MODE		MTL_QUEUE_DCB
/* RX Queue 3: Untagged AV Control packets */
#define RX_QUEUE3_MODE		MTL_QUEUE_AVB
/* RX Queue 4: AVB Class B AVTP packets */
#define RX_QUEUE4_MODE		MTL_QUEUE_AVB
/* RX Queue 5: AVB Class A AVTP packets */
#define RX_QUEUE5_MODE		MTL_QUEUE_AVB
/* RX Queue 6:TSN  Class CDT packets */
#define RX_QUEUE6_MODE		MTL_QUEUE_AVB
/* RX Queue 7: Broadcast/Multicast packets */
#define RX_QUEUE7_MODE		MTL_QUEUE_DCB

/* Rx Queue Size */
#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE
#define RX_QUEUE0_SIZE	18432
#define RX_QUEUE1_SIZE	18432
#define RX_QUEUE2_SIZE	1024
#define RX_QUEUE3_SIZE	1024
#define RX_QUEUE4_SIZE	2048
#define RX_QUEUE5_SIZE	1024
#define RX_QUEUE6_SIZE	1024
#define RX_QUEUE7_SIZE	4096
#else
#define RX_QUEUE0_SIZE	18432
#define RX_QUEUE1_SIZE	4096
#define RX_QUEUE2_SIZE	1024
#define RX_QUEUE3_SIZE	1024
#define RX_QUEUE4_SIZE	4096
#define RX_QUEUE5_SIZE	4096
#define RX_QUEUE6_SIZE	4096
#define RX_QUEUE7_SIZE	4096
#endif
/* Rx Queue Packet Routing */
#define RX_QUEUE0_PKT_ROUTE	PACKET_UPQ
#define RX_QUEUE1_PKT_ROUTE	0
#define RX_QUEUE2_PKT_ROUTE	PACKET_PTPQ
#define RX_QUEUE3_PKT_ROUTE	PACKET_AVCPQ
/* Queue 4,5,6 Routed based on Packet Priority Configured in
 * MAC_RxQ_Ctrl2/MAC_RxQ_Ctrl3
 */
#define RX_QUEUE4_PKT_ROUTE	0
#define RX_QUEUE5_PKT_ROUTE	0
#define RX_QUEUE6_PKT_ROUTE	0
#define RX_QUEUE7_PKT_ROUTE	PACKET_MCBCQ


#define TC956X_DA_MAP		0xF

/* Unicast/Untagged packet */
#define LEG_UNTAGGED_PACKET	0
/* VLAN tagged packets */
#define LEG_TAGGED_PACKET	0
/* Untagged gPTP packet */
#define UNTAGGED_GPTP_PACKET	4
/* Untagged AV Control Packet */
#define UNTAGGED_AVCTRL_PACKET	3
/* Class B AVB Packet */
#define AVB_CLASS_B_PACKET	5
/* Class A AVB Packet */
#define AVB_CLASS_A_PACKET	6
/* TSN Class CDT Packet */
#define TSN_CLASS_CDT_PACKET	7
/* Broadcast/Multicast packet */
#define BC_MC_PACKET		0

#define TX_QUEUE0_USE_PRIO	true
#define TX_QUEUE1_USE_PRIO	true
#define TX_QUEUE2_USE_PRIO	true
#define TX_QUEUE3_USE_PRIO	true
#define TX_QUEUE4_USE_PRIO	false
#define TX_QUEUE5_USE_PRIO	false
#define TX_QUEUE6_USE_PRIO	false
#define TX_QUEUE7_USE_PRIO	false

#define TX_QUEUE0_PRIO		0xFF	/* TC0 Priority */
#define TX_QUEUE1_PRIO		0xFF
#define TX_QUEUE2_PRIO		0xFF
#define TX_QUEUE3_PRIO		0xFF
#define TX_QUEUE4_PRIO		0x00
#define TX_QUEUE5_PRIO		0x00
#define TX_QUEUE6_PRIO		0x00
#define TX_QUEUE7_PRIO		0x00

/* Rx Queue Use Priority */
#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE
/* RXQ0 used for Host unicast/untagged and vlan tagged packets
RXQ1 used for MAC2MAC */
#define RX_QUEUE0_USE_PRIO		true
#define RX_QUEUE1_USE_PRIO		false
#else
#define RX_QUEUE0_USE_PRIO		false
#define RX_QUEUE1_USE_PRIO		true
#endif
#define RX_QUEUE2_USE_PRIO		false
#define RX_QUEUE3_USE_PRIO		false
#define RX_QUEUE4_USE_PRIO		true
#define RX_QUEUE5_USE_PRIO		true
#define RX_QUEUE6_USE_PRIO		true
#define RX_QUEUE7_USE_PRIO		false

/* Rx Queue VLAN tagged Priority mapping */
#ifdef TC956X_ENABLE_MAC2MAC_BRIDGE
#define RX_QUEUE0_PRIO		0xFF
#define RX_QUEUE1_PRIO		0
#else
#define RX_QUEUE0_PRIO		0
#define RX_QUEUE1_PRIO		0xFF
#endif
#define RX_QUEUE2_PRIO		0
#define RX_QUEUE3_PRIO		0
#define RX_QUEUE4_PRIO		(1 << TC956X_AVB_PRIORITY_CLASS_B)
#define RX_QUEUE5_PRIO		(1 << TC956X_AVB_PRIORITY_CLASS_A)
#define RX_QUEUE6_PRIO		(1 << TC956X_PRIORITY_CLASS_CDT)
#define RX_QUEUE7_PRIO		0

#define EEPROM_OFFSET		0
#define EEPROM_MAC_COUNT	2

/* DMA Ch allocation in tx-rx pairs for PF & VF */
#define TC956X_PF_CH_ALLOC	0xFF	/* PF - All Channels */
#define TC956X_VF0_CH_ALLOC	0x00	/* VF0 - NA */
#define TC956X_VF1_CH_ALLOC	0x00	/* VF1 - NA */
#define TC956X_VF2_CH_ALLOC	0x00	/* VF2 - NA */

/***************** Automotive and Port Bridge Config Ends ***************/

#elif defined TC956X_SRIOV_VF

/************************ TC956X_SRIOV_VF Starts ************************/
#define MAX_TX_QUEUES_TO_USE 8
#define MAX_RX_QUEUES_TO_USE 8

#define MAX_FUNCS_PER_PF 4
#define TX_TC_ZERO		0x0
#define TC956X_DMA_CH0_MASK 0x01

#define EEPROM_PORT_OFFSET	((MAC_PORT_NUM_CHECK) ? \
					(EEPROM_VF_SLOT_OFST1) : (EEPROM_VF_SLOT_OFST2))

#define EEPROM_VF_SLOT_OFST1	2
#define EEPROM_VF_SLOT_OFST2	3

/************************** TC956X_SRIOV_VF Ends ************************/

#else /* TC956X_SRIOV_PF */

#ifdef TC956X
/* CPE usecase Configruations */

#define MAX_TX_QUEUES_TO_USE 2
#define MAX_RX_QUEUES_TO_USE 2

/* Tx Queue Size*/
#define TX_QUEUE0_SIZE		18432
#define TX_QUEUE1_SIZE		18432
#define TX_QUEUE2_SIZE		0
#define TX_QUEUE3_SIZE		0
#define TX_QUEUE4_SIZE		0
#define TX_QUEUE5_SIZE		0
#define TX_QUEUE6_SIZE		0
#define TX_QUEUE7_SIZE		0

/* TX Queue 0: Legacy and Jumbo packets */
#define TX_QUEUE0_MODE		MTL_QUEUE_DCB
/* TX Queue 1: Legacy */
#define TX_QUEUE1_MODE		MTL_QUEUE_DCB
/* TX Queue 2: Legacy */
#define TX_QUEUE2_MODE		MTL_QUEUE_DISABLE
/* TX Queue 3: Legacy */
#define TX_QUEUE3_MODE		MTL_QUEUE_DISABLE
/* TX Queue 4: Untagged PTP */
#define TX_QUEUE4_MODE		MTL_QUEUE_DISABLE
/* TX Queue 5: AVB Class B AVTP packet */
#define TX_QUEUE5_MODE		MTL_QUEUE_DISABLE
/* TX Queue 6: AVB Class A AVTP packet */
#define TX_QUEUE6_MODE		MTL_QUEUE_DISABLE
/* TX Queue 7: TSN Class CDT packet */
#define TX_QUEUE7_MODE		MTL_QUEUE_DISABLE

/* Tx Queue TBS Enable/Disable */
#define TX_QUEUE0_TBS		0
#define TX_QUEUE1_TBS		0
#define TX_QUEUE2_TBS		0
#define TX_QUEUE3_TBS		0
#define TX_QUEUE4_TBS		0
#define TX_QUEUE5_TBS		0
#define TX_QUEUE6_TBS		0
#define TX_QUEUE7_TBS		0

/* Tx Queue TSO Enable/Disable */
#define TX_QUEUE0_TSO		1
#define TX_QUEUE1_TSO		0
#define TX_QUEUE2_TSO		0
#define TX_QUEUE3_TSO		0
#define TX_QUEUE4_TSO		0
#define TX_QUEUE5_TSO		0
#define TX_QUEUE6_TSO		0
#define TX_QUEUE7_TSO		0

/* Configure TxQueue - Traffic Class mapping */
#define TX_QUEUE0_TC	0x0
#define TX_QUEUE1_TC	0x1
#define TX_QUEUE2_TC	0x0
#define TX_QUEUE3_TC	0x0
#define TX_QUEUE4_TC	0x0
#define TX_QUEUE5_TC	0x0
#define TX_QUEUE6_TC	0x0
#define TX_QUEUE7_TC	0x0


/* Rx Queue Size */
#define RX_QUEUE0_SIZE		18432
#define RX_QUEUE1_SIZE		18432
#define RX_QUEUE2_SIZE		0
#define RX_QUEUE3_SIZE		0
#define RX_QUEUE4_SIZE		0
#define RX_QUEUE5_SIZE		0
#define RX_QUEUE6_SIZE		0
#define RX_QUEUE7_SIZE		0

/*
 * RX Queue 0: Unicast/Untagged Packets - Packets with
 * unique MAC Address of Host/Guest OS DMA channel selection will be based on
 * MAC_Address(#i)_High.DCS
 */
#define RX_QUEUE0_MODE		MTL_QUEUE_DCB
/* RX Queue 1: VLAN Tagged Legacy packets - Pkt routing will be based on VLAN */
#define RX_QUEUE1_MODE		MTL_QUEUE_DCB
/* RX Queue 2: Untagged gPTP packets */
#define RX_QUEUE2_MODE		MTL_QUEUE_DISABLE
/* RX Queue 3: Untagged AV Control packets */
#define RX_QUEUE3_MODE		MTL_QUEUE_DISABLE
/* RX Queue 4: AVB Class B AVTP packets */
#define RX_QUEUE4_MODE		MTL_QUEUE_DISABLE
/* RX Queue 5: AVB Class A AVTP packets */
#define RX_QUEUE5_MODE		MTL_QUEUE_DISABLE
/* RX Queue 6:TSN  Class CDT packets */
#define RX_QUEUE6_MODE		MTL_QUEUE_DISABLE
/* RX Queue 7: Broadcast/Multicast packets */
#define RX_QUEUE7_MODE		MTL_QUEUE_DISABLE

/* Rx Queue Packet Routing */
#define RX_QUEUE0_PKT_ROUTE	PACKET_MCBCQ
#define RX_QUEUE1_PKT_ROUTE	PACKET_UPQ
#define RX_QUEUE2_PKT_ROUTE	0
#define RX_QUEUE3_PKT_ROUTE	0
#define RX_QUEUE4_PKT_ROUTE	0
#define RX_QUEUE5_PKT_ROUTE	0
#define RX_QUEUE6_PKT_ROUTE	0
#define RX_QUEUE7_PKT_ROUTE	0


/* Packet Type - Rx DMA channel static mapping */
/* Unicast/Untagged packet */
#define LEG_UNTAGGED_PACKET	0

/* VLAN tagged packets
 * For static mapping, route to RxCh0
 */
#define LEG_TAGGED_PACKET	0

/* Untagged gPTP packet */
#define UNTAGGED_GPTP_PACKET	4	/*Not used in CPE case */

/* Untagged AV Control Packet */
#define UNTAGGED_AVCTRL_PACKET	3	/*Not used in CPE case */

/* Class B AVB Packet */
#define AVB_CLASS_B_PACKET	5	/*Not used in CPE case */

/* Class A AVB Packet */
#define AVB_CLASS_A_PACKET	6	/*Not used in CPE case */

/* TSN Class CDT Packet */
#define TSN_CLASS_CDT_PACKET	7	/*Not used in CPE case */

/* Broadcast/Multicast packet */
#define BC_MC_PACKET		0

/* Rx Queue Use Priority */
#define RX_QUEUE0_USE_PRIO	false
#define RX_QUEUE1_USE_PRIO	true
#define RX_QUEUE2_USE_PRIO	false
#define RX_QUEUE3_USE_PRIO	false
#define RX_QUEUE4_USE_PRIO	false
#define RX_QUEUE5_USE_PRIO	false
#define RX_QUEUE6_USE_PRIO	false
#define RX_QUEUE7_USE_PRIO	false

/* Rx Queue VLAN tagged Priority mapping */
#define RX_QUEUE0_PRIO		0
#define RX_QUEUE1_PRIO		0xFF
#define RX_QUEUE2_PRIO		0
#define RX_QUEUE3_PRIO		0
#define RX_QUEUE4_PRIO		0
#define RX_QUEUE5_PRIO		0
#define RX_QUEUE6_PRIO		0
#define RX_QUEUE7_PRIO		0

#define EEPROM_OFFSET		0
#define EEPROM_MAC_COUNT	2

#define TX_DMA_CH0_OWNER USE_IN_TC956X_SW
#define TX_DMA_CH1_OWNER NOT_USED
#define TX_DMA_CH2_OWNER USE_IN_TC956X_SW
#define TX_DMA_CH3_OWNER USE_IN_TC956X_SW
#define TX_DMA_CH4_OWNER USE_IN_TC956X_SW
#define TX_DMA_CH5_OWNER USE_IN_TC956X_SW
#define TX_DMA_CH6_OWNER USE_IN_TC956X_SW
#define TX_DMA_CH7_OWNER USE_IN_TC956X_SW

#define RX_DMA_CH0_OWNER USE_IN_TC956X_SW
#define RX_DMA_CH1_OWNER NOT_USED
#define RX_DMA_CH2_OWNER USE_IN_TC956X_SW
#define RX_DMA_CH3_OWNER USE_IN_TC956X_SW
#define RX_DMA_CH4_OWNER USE_IN_TC956X_SW
#define RX_DMA_CH5_OWNER USE_IN_TC956X_SW
#define RX_DMA_CH6_OWNER USE_IN_TC956X_SW
#define RX_DMA_CH7_OWNER USE_IN_TC956X_SW

#endif /* TC956X */
#endif /* TC956X_SRIOV_PF */

/* PCI new class code  */
#define PCI_ETHC_CLASS_CODE		0x020000

#ifdef TC956X
#define MAC0_BASE_OFFSET		0x40000/* eMAC0 Base Offset */
#define MAC1_BASE_OFFSET		0x48000 /* eMAC1 Base Offset */
#else
#define MAC0_BASE_OFFSET		0xA000/* eMAC0 Base Offset */
#define MAC1_BASE_OFFSET		0xA000 /* eMAC1 Base Offset */
#endif

#define TC956X_PTP_SYSCLOCK		250000000 /* System clock is 250MHz */
#define TC956X_TARGET_PTP_CLK	50000000

/* Debug prints */
#define NMSGPR_INFO(dev, x...)		dev_info(dev, x)
#define NMSGPR_ALERT(dev, x...)		dev_alert(dev, x)
#define NMSGPR_ERR(dev, x...)		dev_err(dev, x)

//#define TC956X_DBG_FUNC
//#define TC956X_DBG_PTP
//#define TC956X_DBG_TSN
//#define TC956X_DBG_MDIO
//#define TC956X_DBG_ETHTOOL
//#define TC956X_DBG_L1
//#define TC956X_DBG_L2
//#define TC956X_TEST
//#define TC956X_MSI_GEN_SW_AGENT /*Macro to enable and handle SW MSI interrupt*/
//#define TC956X_TEST_RXCH1_FRP_DISABLED
//#define TC956X_PKT_DUP
#define TC956X_FRP_ENABLE


#ifdef TC956X_DBG_PTP
#define DBGPR_FUNC_PTP(x...)	pr_alert(x)
#else
#define DBGPR_FUNC_PTP(x...)	do { } while (0)
#endif

#ifdef TC956X_DBG_TSN
#define DBGPR_FUNC_TSN(x...)	pr_alert(x)
#else
#define DBGPR_FUNC_TSN(x...)	do { } while (0)
#endif

#ifdef TC956X_DBG_MDIO
#define DBGPR_FUNC_MDIO(x...)	pr_alert(x)
#else
#define DBGPR_FUNC_MDIO(x...)	do { } while (0)
#endif

#ifdef TC956X_DBG_ETHTOOL
#define DBGPR_FUNC_ETHTOOL(x...)	pr_alert(x)
#else
#define DBGPR_FUNC_ETHTOOL(x...)	do { } while (0)
#endif

#ifdef TC956X_SRIOV_VF
/* TC956X Semaphore lock/unlock register */
#define NSEM(reg)					(0x1100 + (reg * 4))
/* TC956X Semaphore read register */
#define NSEM_RD(reg)				(0x1140 + (reg * 4))

#endif

#define TC956X_DBG_FUNC
#define TC956X_TEST
#define TC956X_DBG_L1
#define TC956X_DBG_L2


//#define TC956X_KPRINT_DEBUG_L1
#define TC956X_KPRINT_INFO
#define TC956X_KPRINT_NOTICE
#define TC956X_KPRINT_WARNING
#define TC956X_KPRINT_ERR
#define TC956X_KPRINT_CRIT
#define TC956X_KPRINT_ALERT


/* Debug prints */
#define NMSGPR_INFO(dev, x...)  dev_info(dev, x)
#define NMSGPR_ALERT(dev, x...) dev_alert(dev, x)
#define NMSGPR_ERR(dev, x...)   dev_err(dev, x)

#ifdef TC956X_DBG_FUNC
#define DBGPR_FUNC(dev, x...) dev_alert(dev, x)
#else
#define DBGPR_FUNC(dev, x...) do { } while (0)
#endif
#ifdef TC956X_TEST
#define DBGPR_TEST(dev, x...) dev_alert(dev, x)
#else
#define DBGPR_TEST(dev, x...) do { } while (0)
#endif

#ifdef TC956X_DBG_L1
#define NDBGPR_L1(dev, x...) dev_dbg(dev, x)
#else
#define NDBGPR_L1(dev, x...) do { } while (0)
#endif

#ifdef TC956X_DBG_L2
#define NDBGPR_L2(dev, x...) dev_dbg(dev, x)
#else
#define NDBGPR_L2(dev, x...) do { } while (0)
#endif

/* Kernel Print without dev */
#ifdef TC956X_KPRINT_DEBUG_L2
#define KPRINT_DEBUG2(x...) printk(KERN_DEBUG x)
#else
#define KPRINT_DEBUG2(x...) do { } while (0)
#endif

#ifdef TC956X_KPRINT_DEBUG_L1
#define KPRINT_DEBUG1(x...) printk(KERN_DEBUG x)
#else
#define KPRINT_DEBUG1(x...) do { } while (0)
#endif

#ifdef TC956X_KPRINT_INFO
#define KPRINT_INFO(x...) printk(KERN_INFO x)
#else
#define KPRINT_INFO(x...) do { } while (0)
#endif

#ifdef TC956X_KPRINT_NOTICE
#define KPRINT_NOTICE(x...) printk(KERN_NOTICE x)
#else
#define KPRINT_NOTICE(x...) do { } while (0)
#endif

#ifdef TC956X_KPRINT_WARNING
#define KPRINT_WARNING(x...) printk(KERN_WARNING x)
#else
#define KPRINT_WARNING(x...) do { } while (0)
#endif

#ifdef TC956X_KPRINT_ERR
#define KPRINT_ERR(x...) printk(KERN_ERR x)
#else
#define KPRINT_ERR(x...) do { } while (0)
#endif

#ifdef TC956X_KPRINT_CRIT
#define KPRINT_CRIT(x...) printk(KERN_CRIT x)
#else
#define KPRINT_CRIT(x...) do { } while (0)
#endif

#ifdef TC956X_KPRINT_ALERT
#define KPRINT_ALERT(x...) printk(KERN_ALERT x)
#else
#define KPRINT_ALERT(x...) do { } while (0)
#endif


#define SPEED_1GBPS		(1000) /* 1Gbps Interms of MBPS */
#define SPEED_10GBPS		(10000) /* 10Gbps Interms of MBPS */

//#define RSC_MNG_OFFSET		0x2000
//#define RSCMNG_ID_REG		((RSC_MNG_OFFSET) + 0x00000000)
#define RSCMNG_PFN		GENMASK(3, 0)
#define RSCMNG_PFN_SHIFT	0

/*	Configuration Register Address	*/
#define NCID_OFFSET		(0x0000) /* TC956X Chip and revision ID */
#define NMODESTS_OFFSET		(0x0004) /* TC956X current operation mode */
#define NFUNCEN0_OFFSET		(0x0008) /* TC956X pin mux control */
#define NPCIEBOOT_OFFSET	(0x0018) /* TC956X PCIE Boot HW Sequence Status and Control */

/* Pinmux for PPS Out PPO01 and PPO11*/
#define NFUNCEN0_JTAGEN_MASK	0x40000000U
#define NFUNCEN0_JTAG_MASK	0x000F0000U
#define NFUNCEN0_JTAG_SHIFT	16U
#define FUNCTION1		0x1U
#define FUNCTION2		0x2U
#define NFUNCEN4_GPIO2_MASK	0x00000F00U
#define NFUNCEN4_GPIO2_SHIFT	8U
#define NFUNCEN4_GPIO4_MASK	0x000F0000
#define NFUNCEN4_GPIO4_SHIFT	16U

#define NSYSDATA0_OFFSET	(0x0100)
#define NSYSDATA1_OFFSET	(0x0104)

#ifdef TC956X
#define NCTLSTS_OFFSET		(0x1000)  /* TC956X control and status */
#define NCLKCTRL0_OFFSET	(0x1004)  /* TC956X clock control Register-0 */
#define NCLKCTRL0_MCUCEN	BIT(0)
#define NCLKCTRL0_INTCEN	BIT(4)
#define NCLKCTRL0_MAC0TXCEN	BIT(7)
#define NCLKCTRL0_PCIECEN	BIT(9)
#define NCLKCTRL0_I2SSPIEN	BIT(12)
#define NCLKCTRL0_SRMCEM	BIT(13)
#define NCLKCTRL0_MAC0RXCEN	BIT(14)
#define NCLKCTRL0_UARTOCEN	BIT(16)
#define NCLKCTRL0_MSIGENCEN	BIT(18)
#define NCLKCTRL0_POEPLLCEN	BIT(24)
#define NCLKCTRL0_SGMPCIEN	BIT(25)
#define NCLKCTRL0_REFCLKOCEN	BIT(26)
#define NCLKCTRL0_MAC0125CLKEN	BIT(29)
#define NCLKCTRL0_MAC0312CLKEN	BIT(30)
#define NCLKCTRL0_MAC0ALLCLKEN	BIT(31)
#define NRSTCTRL0_OFFSET	(0x1008)  /* TC956X reset control Register-0 */
#define NRSTCTRL0_MCURST	BIT(0)
#define NRSTCTRL0_INTRST	BIT(4)
#define NRSTCTRL0_MAC0RST	BIT(7)
#define NRSTCTRL0_PCIERST	BIT(9)
#define NRSTCTRL0_UART0RST	BIT(16)
#define NRSTCTRL0_MSIGENRST	BIT(18)
#define NRSTCTRL0_MAC0PMARST	BIT(30)
#define NRSTCTRL0_MAC0PONRST	BIT(31)
#define NCLKCTRL1_OFFSET	(0x100C)  /* TC956X clock control Register-1 for eMAC Port-1*/
#define NCLKCTRL1_MAC1TXCEN	BIT(7)
#define NCLKCTRL1_MAC1RXCEN	BIT(14)
#define NCLKCTRL1_MAC1RMCEN	BIT(15)
#define NCLKCTRL1_MAC1125CLKEN1	BIT(29)
#define NCLKCTRL1_MAC1312CLKEN1	BIT(30)
#define NCLKCTRL1_MAC1ALLCLKEN1	BIT(31)
#define NRSTCTRL1_OFFSET	(0x1010)  /* TC956X reset control Register-1 for eMAC Port-1*/
#define NRSTCTRL1_MAC1RST1	BIT(7)
#define NRSTCTRL1_MAC1PMARST1	BIT(30)
#define NRSTCTRL1_MAC1PONRST1	BIT(31)
#define NRSTCTRL_EMAC_MASK     (NRSTCTRL0_MAC0RST | NRSTCTRL0_MAC0PMARST | \
				 NRSTCTRL0_MAC0PONRST)
#define NCLKCTRL_EMAC_MASK     (NCLKCTRL0_MAC0TXCEN | NCLKCTRL0_MAC0RXCEN | \
				 NCLKCTRL0_MAC0125CLKEN | NCLKCTRL0_MAC0312CLKEN | \
				 NCLKCTRL1_MAC1RMCEN | NCLKCTRL0_MAC0ALLCLKEN)
#define NCLKCTRL0_COMMON_EMAC_MASK     (NCLKCTRL0_POEPLLCEN | NCLKCTRL0_SGMPCIEN | \
				 NCLKCTRL0_REFCLKOCEN)
#define NRSTCTRL0_DEFAULT	(NRSTCTRL0_MAC0PONRST | NRSTCTRL0_MAC0PMARST | \
					NRSTCTRL0_MAC0RST)
#define NRSTCTRL_COMMON (NRSTCTRL0_MSIGENRST  | NRSTCTRL0_UART0RST | \
					NRSTCTRL0_INTRST | NRSTCTRL0_MCURST)
#define NCLKCTRL0_DEFAULT	(NCLKCTRL0_SRMCEM | NCLKCTRL0_I2SSPIEN | \
					NCLKCTRL0_PCIECEN | NCLKCTRL0_MCUCEN)
#define NCLKCTRL_ENABLE_COMMON_EMAC_MASK (NCLKCTRL0_SRMCEM | NCLKCTRL0_I2SSPIEN | \
					NCLKCTRL0_PCIECEN | NCLKCTRL0_MCUCEN)
#define NCLKCTRL_DISABLE_COMMON_EMAC_MASK (NCLKCTRL0_MAC0ALLCLKEN | NCLKCTRL0_MAC0312CLKEN | \
								NCLKCTRL0_MAC0125CLKEN | NCLKCTRL0_REFCLKOCEN | \
								NCLKCTRL0_SGMPCIEN | NCLKCTRL0_POEPLLCEN | \
								NCLKCTRL0_MSIGENCEN | NCLKCTRL0_UARTOCEN | \
								NCLKCTRL0_MAC0RXCEN | NCLKCTRL0_MAC0TXCEN | \
								NCLKCTRL0_INTCEN)
#define NCLKCTRL_PORT0_EMAC_MASK     (NCLKCTRL0_MAC0TXCEN | NCLKCTRL0_MAC0RXCEN | \
				 NCLKCTRL0_MAC0125CLKEN | NCLKCTRL0_MAC0312CLKEN | \
				 NCLKCTRL0_MAC0ALLCLKEN)

#define NBUSCTRL_OFFSET		(0x1014)

#define NRSTCTRL_LINK_DOWN		(NRSTCTRL0_MAC0PMARST | NRSTCTRL0_MAC0PONRST)
#define NCLKCTRL_LINK_DOWN		(NCLKCTRL0_MAC0TXCEN | NCLKCTRL0_MAC0RXCEN | \
						NCLKCTRL0_MAC0125CLKEN | NCLKCTRL0_MAC0312CLKEN | \
						NCLKCTRL1_MAC1RMCEN)
#define NRSTCTRL_LINK_DOWN_SAVE			(0xC0000080U) /* Save Non-Common Reset Register Bits Between Port 0 and Port 1 */
#define NCLKCTRL_LINK_DOWN_SAVE			(0xE000C080U) /* Save Non-Common Clock Register Bits Between Port 0 and Port 1 */
#define NRSTCTRL_LINK_DOWN_CMN_SAVE		(0x00051213U) /* Save Common Reset Register Bits Between Port 0 and Port 1 */
#define NCLKCTRL_LINK_DOWN_CMN_SAVE		(0x07053211U) /* Save Common Clock Register Bits Between Port 0 and Port 1 */

#define TC956X_MSIGENCEN	18

/* Interrupts to CM3 to be enabled in FW */
#define TC956X_INT_MASK0	0xFFFF1FFF/*0x00000000*/
#define TC956X_INT_MASK1	0xFFFF1F80/*0x00000000*/
#define TC956X_INT_MASK2	0x00007740/*0x00000000*/
#endif

#define NSPLLPARAM_OFFSET	(0x1020) /* TC956X System PLL parameters */
#define NSPLLUPDT_OFFSET		(0x1024) /* TC956X System PLL update */
#define NOSCCTRL_OFFSET		(0x1028) /* TC956X OSC drive strength control */

#define NEMACTXCDLY_OFFSET	(0x1050) /* Integrated Delay on RGMII TXC */
#define NEMACINTO00EN_OFFSET	(0x1054)
#define NEMACINTO01EN_OFFSET	(0x1058)
#define NEMACINTO10EN_OFFSET	(0x105C)
#define NEMACINTO11EN_OFFSET	(0x1060)

#ifdef TC956X
#define NEMAC0CTL_OFFSET	(0x1070) /* eMAC Port-0 Control */
#endif

#ifdef TC956X_SRIOV_PF
/* MSIGEN Registers */

#define MSI_INT_TX_CH0		3
#define MSI_INT_RX_CH0		11

#define MSI_INT_EXT_PHY		20

#define TC956X_MSI_BASE		(0xF000)

#define TC956X_MSI_F_OFFSET		(0x0100)
#define TC956X_MSI_OUT_EN_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0000))
#define TC956X_MSI_MASK_SET_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0008))
#define TC956X_MSI_INT_RAW_STS_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE + \
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0014))
#define TC956X_MSI_STS_OFFSET(pf_id, vf_id)		(TC956X_MSI_BASE + \
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0018))
#define TC956X_MSI_MASK_CLR_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x000c))
#define TC956X_MSI_INT_STS_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0010))
#define TC956X_MSI_VECT_SET0_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0020))
#define TC956X_MSI_VECT_SET1_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0024))
#define TC956X_MSI_VECT_SET2_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0028))
#define TC956X_MSI_VECT_SET3_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x002C))
#define TC956X_MSI_VECT_SET4_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0030))
#define TC956X_MSI_VECT_SET5_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0034))
#define TC956X_MSI_VECT_SET6_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0038))
#define TC956X_MSI_VECT_SET7_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x003C))
#define TC956X_MSI_SW_MSI_CLR(pf_id, vf_id)	(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0054))
#define TC956X_MSI_EVENT_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0068))

#define TC956X_MSI_CNT0(pf_id, vf_id)			(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0080))
#define TC956X_MSI_CNT1(pf_id, vf_id)			(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0084))
#define TC956X_MSI_CNT2(pf_id, vf_id)			(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0088))
#define TC956X_MSI_CNT3(pf_id, vf_id)			(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x008C))
#define TC956X_MSI_CNT4(pf_id, vf_id)			(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x0090))
#define TC956X_MSI_CNT11(pf_id, vf_id)			(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x00AC))
#define TC956X_MSI_CNT12(pf_id, vf_id)			(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x00B0))
#define TC956X_MSI_CNT20(pf_id, vf_id)			(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x00D0))
#define TC956X_MSI_CNT24(pf_id, vf_id)			(TC956X_MSI_BASE +\
						(vf_id * TC956X_MSI_F_OFFSET) + (pf_id * TC956X_MSI_F_OFFSET) + (0x00E0))

#define TC956X_MSI_OUT_EN_CLR	(0x00000000)

/* Enabled LPI_INT, EVENT_INT, TX_INT[3], TX_INT[4], RX_INT[3], RX_INT[4]
 * XPCS_INT, PFMAILBOX_INT
 * INT 6, 7, 14, 15 is allocated to MSI Vector 1.
 * All other enabled INT to Vector 0.
 */

#if defined(TC956X_AUTOMOTIVE_CONFIG)
#ifdef TC956X_LPI_INTERRUPT
#define TC956X_MSI_OUT_EN (0x0017FFFD)
#else /* #ifdef TC956X_LPI_INTERRUPT */
#define TC956X_MSI_OUT_EN (0x0017FFFC)
#endif /* #ifdef TC956X_LPI_INTERRUPT */
#else /* #if defined(TC956X_AUTOMOTIVE_CONFIG) */
#define TC956X_MSI_OUT_EN (0x0037FFFD)
#endif /* #if defined(TC956X_AUTOMOTIVE_CONFIG) */

#if defined(TC956X_AUTOMOTIVE_CONFIG) || defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
#define TC956X_MSI_MASK_SET	(0xFFFFFFFE)
#define TC956X_MSI_MASK_CLR	(0x00000001)
#define TC956X_MSI_SET0		(0x00000000)
#define TC956X_MSI_SET1		(0x00000000)
#define TC956X_MSI_SET2		(0x00000000)
#define TC956X_MSI_SET3		(0x00000000)
#define TC956X_MSI_SET4		(0x00000000)
#define TC956X_MSI_SET5		(0x00000000)
#define TC956X_MSI_SET6		(0x00000000)
#define TC956X_MSI_SET7		(0x00000000)
#else
#define TC956X_MSI_MASK_SET	(0xFFFFFFFC)
#define TC956X_MSI_MASK_CLR	(0x00000003)
#define TC956X_MSI_SET0		(0x00010101)
#define TC956X_MSI_SET1		(0x00000000)
#define TC956X_MSI_SET2		(0x00000000)
#define TC956X_MSI_SET3		(0x00000000)
#define TC956X_MSI_SET4		(0x01000000)
#define TC956X_MSI_SET5		(0x01010101)
#define TC956X_MSI_SET6		(0x01010101)
#define TC956X_MSI_SET7		(0x01010101)
#endif

#endif /* TC956X_SRIOV_PF */

#define NEMAC1CTL_OFFSET	(0x1074) /* eMAC Port-1 Control */
#define NEMACSTS_OFFSET		(0x1078) /* eMAC status */
#define NEMACIOCTL_OFFSET	(0x107C) /* eMAC IO Control */

#define NEMACCTL_SP_SEL_MASK			GENMASK(3, 0)
#define NEMACCTL_INIT_DONE			0x200000
#define NEMACCTL_LPIHWCLKEN			(0x100)
#define NEMACCTL_PHY_INF_SEL_MASK		GENMASK(5, 4)
#define NEMACCTL_PHY_INF_SEL			(0x10)/* Phy_intf_sel : clock from PHY */
#define NEMACCTL_SP_SEL_SGMII_10M		(0x7) /* SGMII 10M */
#define NEMACCTL_SP_SEL_SGMII_100M		(0x6) /* SGMII 100M */
#define NEMACCTL_SP_SEL_SGMII_1000M		(0x5) /* SGMII 1000M */
#define NEMACCTL_SP_SEL_SGMII_2500M		(0x4) /* SGMII 2500M */
#define NEMACCTL_SP_SEL_USXGMII_2_5G_2_5G	(0xD) /* USXGMII 2.5G/2.5G */
#define NEMACCTL_SP_SEL_USXGMII_2_5G_5G		(0xC) /* USXGMII 2.5G/5G */
#define NEMACCTL_SP_SEL_USXGMII_2_5G_10G	(0xB) /* USXGMII 2.5G/10G */
#define NEMACCTL_SP_SEL_USXGMII_5G_5G		(0xA) /* USXGMII 5G/5G */
#define NEMACCTL_SP_SEL_USXGMII_5G_10G		(0x9) /* USXGMII 5G/10G */
#define NEMACCTL_SP_SEL_USXGMII_10G_10G		(0x8) /* USXGMII 10G/10G */
#define NEMACCTL_SP_SEL_RGMII_10M		(0x2) /* RGMII 10M */
#define NEMACCTL_SP_SEL_RGMII_100M		(0x1) /* RGMII 100M */
#define NEMACCTL_SP_SEL_RGMII_1000M		(0x0) /* RGMII 1000M */


#define GPIOI0_OFFSET	(0x1200) /* GPIO Input-0 register */
#define GPIOI1_OFFSET	(0x1204) /* GPIO Input-1 register */
#define GPIOE0_OFFSET	(0x1208) /* GPIO Enable-0 register */
#define GPIOE1_OFFSET	(0x120C) /* GPIO Enable-1 register */
#define GPIOO0_OFFSET	(0x1210) /* GPIO Output-0 register */
#define GPIOO1_OFFSET	(0x1214) /* GPIO Output-1 register */

#define NPCIEPWR_OFFSET	(0x1300) /* PCIe power gating control */

#define I2CERRADD_OFFSET	(0x1400)
#define SPIERRADD_OFFSET	(0x1404)
#define NFUNCEN1_OFFSET		(0x1514)
#define NFUNCEN2_OFFSET		(0x151C)
#define NFUNCEN3_OFFSET		(0x1524)
#define NFUNCEN4_OFFSET		(0x1528)
#define NFUNCEN5_OFFSET		(0x152C)
#define NFUNCEN6_OFFSET		(0x1530)
#define NFUNCEN7_OFFSET		(0x153C)

#define NFUNCEN_FUNC0		(0)
#define NFUNCEN4_GPIO_00	GENMASK(3, 0)
#define NFUNCEN4_GPIO_00_SHIFT	(0)
#define NFUNCEN4_GPIO_01	GENMASK(7, 4)
#define NFUNCEN4_GPIO_01_SHIFT	(4)
#define NFUNCEN4_GPIO_02	GENMASK(11, 8)
#define NFUNCEN4_GPIO_02_SHIFT	(8)
#define NFUNCEN4_GPIO_03	GENMASK(15, 12)
#define NFUNCEN4_GPIO_03_SHIFT	(12)
#define NFUNCEN4_GPIO_04	GENMASK(19, 16)
#define NFUNCEN4_GPIO_04_SHIFT	(16)
#define NFUNCEN4_GPIO_05	GENMASK(23, 20)
#define NFUNCEN4_GPIO_05_SHIFT	(20)
#define NFUNCEN4_GPIO_06	GENMASK(27, 24)
#define NFUNCEN4_GPIO_06_SHIFT	(24)
#define NFUNCEN5_GPIO_10	GENMASK(3, 0)
#define NFUNCEN5_GPIO_10_SHIFT	(0)
#define NFUNCEN5_GPIO_11	GENMASK(7, 4)
#define NFUNCEN5_GPIO_11_SHIFT	(4)
#define NFUNCEN6_GPIO_12	GENMASK(19, 16)
#define NFUNCEN6_GPIO_12_SHIFT	(16)

#define NIOCFG1_OFFSET		(0x1614)
#define NIOCFG7_OFFSET		(0x163C)
#define NIOEN7_OFFSET		(0x173C)

#define GPIO_00			(0)
#define GPIO_01			(1)
#define GPIO_02			(2)
#define GPIO_03			(3)
#define GPIO_04			(4)
#define GPIO_05			(5)
#define GPIO_06			(6)
#define GPIO_10			(10)
#define GPIO_11			(11)
#define GPIO_12			(12)
#define GPIO_32			(32)

#define TRIG00_SHIFT		4
#define TRIG10_SHIFT		12
#define TRIG00_MASK			0xF0
#define TRIG10_MASK			0xF000
#define TSIE_SHIFT			12

/* PCIe registers */
#define PCIE_OFFSET				(0x20000)
#define PCIE_RANGE_UP_OFFSET_RgOffAddr(no)	(PCIE_OFFSET + 0x6200 + (no*0x10))
#define PCIE_RANGE_EN_RgOffAddr(no)		(PCIE_OFFSET + 0x6204 + (no*0x10))
#define PCIE_RANGE_UP_RPLC_RgOffAddr(no)	(PCIE_OFFSET + 0x6208 + (no*0x10))
#define PCIE_RANGE_WIDTH_RgOffAddr(no)		(PCIE_OFFSET + 0x620C + (no*0x10))

/* INTC Registers */
#define INTC_OFFSET		(0x8000)
#define INTC_INTSTATUS		(INTC_OFFSET)
#define INTC_MAC0STATUS		(INTC_OFFSET + 0x0C)
#define INTC_MAC1STATUS		(INTC_OFFSET + 0x10)
#define INTC_EXTINTFLG		(INTC_OFFSET + 0x14)
#define INTC_PCIEL12FLG         (INTC_OFFSET + 0x18)
#define INTC_I2CSPIINTFLG	(INTC_OFFSET + 0x1C)

#define INTMCUMASK0		(INTC_OFFSET + 0x20)
#define INTMCUMASK1		(INTC_OFFSET + 0x24)
#define INTMCUMASK2		(INTC_OFFSET + 0x28)
#define INTMCUMASK3		(INTC_OFFSET + 0x2C)
#define INTMCUMASK_TX_CH0	16
#define INTMCUMASK_RX_CH0	24

#define INTC_EXTINTCFG		(INTC_OFFSET + 0x4C)
#define INTC_MCUFLG		(INTC_OFFSET + 0x54)
#define INTC_EXTFLG		(INTC_OFFSET + 0x58)
#define INTC_MACPPOFLG		(INTC_OFFSET + 0x5C)
#define INTC_INTINTWDCTL	(INTC_OFFSET + 0x60)
#define INTC_INTINTWDEXP	(INTC_OFFSET + 0x64)
#define INTC_INTINTWDMON	(INTC_OFFSET + 0x68)

#define MACRXSTS_MASK		(0xFF)
#define MACTXSTS_MASK		(0xFF0000)

#define INTC_INTSTS_MAC_EVENT	BIT(11)

#define MAC_OFFSET		((MAC_PORT_NUM_CHECK) ? \
				(MAC0_BASE_OFFSET) : (MAC1_BASE_OFFSET))

#define NEMACCTL_OFFSET	\
	((MAC_PORT_NUM_CHECK) ? (NEMAC0CTL_OFFSET) : (NEMAC1CTL_OFFSET))

#define INTC_MAC_STATUS	\
	((MAC_PORT_NUM_CHECK) ? (INTC_MAC0STATUS) : (INTC_MAC1STATUS))



#ifdef TC956X
#define TC956X_ETH_XPCS_INT				BIT(19)
#define TC956X_EXT_PHY_ETH_INT			BIT(20)

#ifdef TC956X_SW_MSI
#define TC956X_SW_MSI_INT			BIT(24)
#endif

#define TC956X_SSREG_BRREG_REG_BASE		(0x00024000U)

#define TC956X_GLUE_LOGIC_BASE_OFST		(0x0002C000U)

/*All phy core use the same base address, glue register we need to select correct phy core*/
#define TC956X_PHY_CORE0_REG_BASE		(0x00028000U)
#define TC956X_PHY_CORE1_REG_BASE		(0x00028000U)
#define TC956X_PHY_CORE2_REG_BASE		(0x00028000U)
#define TC956X_PHY_CORE3_REG_BASE		(0x00028000U)


#define TC956X_SSREG_K_PCICONF_015_000		(TC956X_SSREG_BRREG_REG_BASE \
						+ 0x00000850U)
#define TC956X_SSREG_K_PCICONF_031_016		(TC956X_SSREG_BRREG_REG_BASE \
						+ 0x00000854U)
#define TC956X_SSREG_K_PCICONF_021_021		(TC956X_SSREG_BRREG_REG_BASE \
						+ 0x000009E4U)
#define TC956X_SSREG_K_PCICONF_022_022		(TC956X_SSREG_BRREG_REG_BASE \
						+ 0x000009E8U)

#define TC956X_GLUE_EFUSE_CTRL			(TC956X_GLUE_LOGIC_BASE_OFST \
						+ 0x0000001CU)
#define TC956X_GLUE_SW_REG_ACCESS_CTRL		(TC956X_GLUE_LOGIC_BASE_OFST \
						+ 0x0000002CU)
#define TC956X_GLUE_PHY_REG_ACCESS_CTRL		(TC956X_GLUE_LOGIC_BASE_OFST \
						+ 0x00000030U)
#define TC956X_GLUE_SW_RESET_CTRL		(TC956X_GLUE_LOGIC_BASE_OFST \
						+ 0x00000044U)
#define TC956X_GLUE_SW_DSP1_TEST_IN_31_00	(TC956X_GLUE_LOGIC_BASE_OFST \
						+ 0x0000006CU)
#define TC956X_GLUE_SW_DSP2_TEST_IN_31_00	(TC956X_GLUE_LOGIC_BASE_OFST \
						+ 0x00000074U)
#define TC956X_GLUE_SW_USP_TEST_OUT_127_096	(TC956X_GLUE_LOGIC_BASE_OFST \
						+ 0x00000098U)
#define TC956X_GLUE_TL_LINK_SPEED_MON		(TC956X_GLUE_LOGIC_BASE_OFST \
						+ 0x00000244U)
#define TC956X_GLUE_TL_NUM_LANES_MON		(TC956X_GLUE_LOGIC_BASE_OFST \
						+ 0x00000248U)
#define TC956X_GLUE_RSVD_RW0			(TC956X_GLUE_LOGIC_BASE_OFST \
						+ 0x0000024CU)

#ifdef TC956X_PCIE_LINK_STATE_LATENCY_CTRL
#define TC956X_PCIE_S_REG_OFFSET		(0x00024000U)
#define TC956X_PCIE_S_L0s_ENTRY_LATENCY		(TC956X_PCIE_S_REG_OFFSET \
						+ 0x0000096CU)
#define TC956X_PCIE_S_L1_ENTRY_LATENCY		(TC956X_PCIE_S_REG_OFFSET \
						+ 0x00000970U)

#define TC956X_PCIE_EP_REG_OFFSET		(0x00020000U)
#define TC956X_PCIE_EP_CAPB_SET			(TC956X_PCIE_EP_REG_OFFSET \
						+ 0x000000D8U)

#define	TC956X_PCIE_EP_L0s_ENTRY_SHIFT		(13)
#define	TC956X_PCIE_EP_L1_ENTRY_SHIFT		(18)

#define TC956X_PCIE_EP_L0s_ENTRY_MASK		GENMASK(17, 13)
#define TC956X_PCIE_EP_L1_ENTRY_MASK		GENMASK(27, 18)

#define TC956X_PCIE_S_EN_ALL_PORTS_ACCESS	(0xF)

/*
L0s value range: 1-31
L1 value range : 1-1023

Ex: entry value is n then
entry delay = n * 256 ns */

/* Link state change delay configuration for Upstream Port */
#define USP_L0s_ENTRY_DELAY	(0x1FU)
#define USP_L1_ENTRY_DELAY	(0x3FFU)

/* Link state change delay configuration for Downstream Port-1 */
#define DSP1_L0s_ENTRY_DELAY	(0x1FU)
#define DSP1_L1_ENTRY_DELAY	(0x3FFU)

/* Link state change delay configuration for Downstream Port-2 */
#define DSP2_L0s_ENTRY_DELAY	(0x1FU)
#define DSP2_L1_ENTRY_DELAY	(0x3FFU)

/* Link state change delay configuration for Virtual Downstream Port */
#define VDSP_L0s_ENTRY_DELAY	(0x1FU)
#define VDSP_L1_ENTRY_DELAY	(0x3FFU)

/* Link state change delay configuration for Internal Endpoint */
#define EP_L0s_ENTRY_DELAY	(0x1FU)
#define EP_L1_ENTRY_DELAY	(0x3FFU)

#endif /* end of TC956X_PCIE_LINK_STATE_LATENCY_CTRL */

#define TC956X_PHY_COREX_PMACNT_GL_PM_PWRST2_CFG0	(TC956X_PHY_CORE0_REG_BASE \
							+ 0x0000009CU)
#define TC956X_PHY_COREX_PMACNT_GL_PM_PWRST2_CFG1	(TC956X_PHY_CORE0_REG_BASE \
							+ 0x000000A0U)
#define TC956X_PHY_COREX_PCS_GL_MD_CFG_TXMARGIN0	(TC956X_PHY_CORE0_REG_BASE \
							+ 0x00000234U)
#define TC956X_PHY_COREX_PMA_LN_PCS_TAP_ADV_R0		(TC956X_PHY_CORE0_REG_BASE \
							+ 0x00003148U)
#define TC956X_PHY_COREX_PMA_LN_PCS_TAP_DLY_R0		(TC956X_PHY_CORE0_REG_BASE \
							+ 0x0000314CU)
#define TC956X_PHY_COREX_PMA_LN_PCS_TAP_ADV_R1		(TC956X_PHY_CORE0_REG_BASE \
							+ 0x000031B4U)
#define TC956X_PHY_COREX_PMA_LN_PCS_TAP_DLY_R1		(TC956X_PHY_CORE0_REG_BASE \
							+ 0x000031B8U)
#define TC956X_PHY_COREX_PMA_LN_PCS_TAP_ADV_R2		(TC956X_PHY_CORE0_REG_BASE \
							+ 0x00003220U)
#define TC956X_PHY_COREX_PMA_LN_PCS_TAP_DLY_R2		(TC956X_PHY_CORE0_REG_BASE \
							+ 0x00003224U)
#define TC956X_PHY_COREX_PMA_LN_RT_OVREN_PCS_TAP_ADV	(TC956X_PHY_CORE0_REG_BASE \
							+ 0x000032F0U)
#define TC956X_PHY_COREX_PMA_LN_RT_OVREN_PCS_TAP_DLY	(TC956X_PHY_CORE0_REG_BASE \
							+ 0x000032F4U)
#define TC956X_PHY_COREX_PMACNT_LN_PM_LOSCNT_CNT0	(TC956X_PHY_CORE0_REG_BASE \
							+ 0x00000844U)

#define TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_RESULT_BEST_MON0	(TC956X_PHY_CORE0_REG_BASE \
								+ 0x00000890U)
#define TC956X_PHY_COREX_PMACNT_LN_PM_EMCNT_INIT_CFG0_R2	(TC956X_PHY_CORE0_REG_BASE \
								+ 0x00000A18U)
#define TC956X_PHY_COREX_PMACNT_LN_PM_EQ_CFG0_R2		(TC956X_PHY_CORE0_REG_BASE \
								+ 0x00000A04U)
#define TC956X_PHY_COREX_PMACNT_LN_PM_EQ_CFG1_R2		(TC956X_PHY_CORE0_REG_BASE \
								+ 0x00000A08U)
#define TC956X_PHY_COREX_PMACNT_LN_PM_EQ1_CFG0_R2		(TC956X_PHY_CORE0_REG_BASE \
								+ 0x00000A0CU)
#define TC956X_PHY_COREX_PMACNT_LN_PM_EQ2_CFG0_R2		(TC956X_PHY_CORE0_REG_BASE \
								+ 0x00000A14U)
#define TC956X_PHY_COREX_PMACNT_GL_PM_DFE_PD_CTRL		(TC956X_PHY_CORE0_REG_BASE \
								+ 0x00000254U)
#define TC956X_PHY_CORE1_PMACNT_GL_PM_DFE_PD_CTRL		(TC956X_PHY_CORE0_REG_BASE \
								+ 0x00000244U)
#define TC956X_PHY_COREX_PMACNT_GL_PM_IF_CNT0			(TC956X_PHY_CORE0_REG_BASE \
								+ 0x00000040U)
#define TC956X_PHY_COREX_PMACNT_GL_PM_IF_CNT4			(TC956X_PHY_CORE0_REG_BASE \
								+ 0x00000050U)

/* PHY core 0 registers */
#define TC956X_PHY_CORE0_GL_LANE_ACCESS		(TC956X_PHY_CORE0_REG_BASE \
						+ 0x00000000U)
#define TC956X_PHY_CORE0_POWER_CTL		(TC956X_PHY_CORE0_REG_BASE \
						+ 0x0000309CU)
#define TC956X_PHY_CORE0_OVEREN_POWER_CTL	(TC956X_PHY_CORE0_REG_BASE \
						+ 0x000032C8U)
#define TC956X_PMA_LN_PCS2PMA_PHYMODE_R2	(TC956X_PHY_CORE0_REG_BASE \
						+ 0x00003268U)
#define TC956X_PHY_CORE0_MISC_RW0_R0		(TC956X_PHY_CORE0_REG_BASE \
						+ 0x00000288U)
#define TC956X_PHY_CORE0_MISC_RW0_R1		(TC956X_PHY_CORE0_REG_BASE \
						+ 0x000002BCU)
#define TC956X_PHY_CORE0_MISC_RW0_R2		(TC956X_PHY_CORE0_REG_BASE \
						+ 0x000002F0U)
/* PHY core 1 registers */
#define TC956X_PHY_CORE1_GL_LANE_ACCESS		(TC956X_PHY_CORE1_REG_BASE \
						+ 0x00000000U)
#define TC956X_PHY_CORE1_POWER_CTL		(TC956X_PHY_CORE1_REG_BASE \
						+ 0x0000309CU)
#define TC956X_PHY_CORE1_OVEREN_POWER_CTL	(TC956X_PHY_CORE1_REG_BASE \
						+ 0x000032C8U)

#define TC956X_PHY_CORE1_MISC_RW0_R0		(TC956X_PHY_CORE1_REG_BASE \
						+ 0x00000278U)
#define TC956X_PHY_CORE1_MISC_RW0_R1		(TC956X_PHY_CORE1_REG_BASE \
						+ 0x000002A0U)
#define TC956X_PHY_CORE1_MISC_RW0_R2		(TC956X_PHY_CORE1_REG_BASE \
						+ 0x000002C8U)

#define TC956X_GLUE_LTSSM_STATE_MASK		(0x0000001FU)
#define TC956X_GLUE_LTSSM_STATE_SHIFT		(0)
#define TC956X_GLUE_DLL_MASK			(0x00000020U)
#define TC956X_GLUE_DLL_SHIFT			(5)
#define TC956X_GLUE_SPEED_MASK(x)		(0x0000000FU << (8*x))
#define TC956X_GLUE_SPEED_SHIFT(x)		(8*x)

#define USP_LANE_WIDTH_MASK			(0x0000003F)
#define DSP1_LANE_WIDTH_MASK			(0x00003F00)
#define DSP1_LANE_WIDTH_SHIFT			(8)
#define TC956X_GLUE_LANE_WIDTH_MASK(x)		(0x0000003FU << (8*x))
#define TC956X_GLUE_LANE_WIDTH_SHIFT(x)		(8*x)

#define USP_LINK_WIDTH_CHANGE_4_TO_1		(0x00000010)
#define USP_LINK_WIDTH_CHANGE_4_TO_2		(0x00000011)
#define USP_LINK_WIDTH_CHANGE_1_2_TO_4		(0x00000013)
#define USP_LINK_WIDTH_CHANGE_2_TO_1		(0x00000010)
#define USP_LINK_WIDTH_CHANGE_1_TO_2		(0x00000011)
#define DSP1_LINK_WIDTH_CHANGE_2_TO_1		(0x00100000)
#define DSP1_LINK_WIDTH_CHANGE_1_TO_2		(0x00110000)

#define SW_PORT_ENABLE_MASK			GENMASK(3, 0)
#define SW_PORT_ENABLE_SHIFT			0U
#define SW_USP_ENABLE				(0x00000001)
#define SW_DSP1_ENABLE				(0x00000002)
#define SW_DSP2_ENABLE				(0x00000004)
#define SW_VDSP_ENABLE				(0x00000008)


#define PHY_CORE_ENABLE_MASK			GENMASK(3, 0)
#define PHY_CORE_0_ENABLE			(0x00000001)
#define PHY_CORE_1_ENABLE			(0x00000002)
#define PHY_CORE_2_ENABLE			(0x00000004)
#define PHY_CORE_3_ENABLE			(0x00000008)


#define LANE_ENABLE_MASK			GENMASK(1, 0)
#define LANE_0_ENABLE				(0x00000001)
#define LANE_1_ENABLE				(0x00000002)

#define POWER_CTL_MASK				GENMASK(24, 0)
#define POWER_CTL_LOW_POWER_ENABLE		0x00474804

#define POWER_CTL_OVER_ENABLE			0x00000001
#define PC_DEBUG_P1_TOGGLE			0x00000001

#define ENABLE_CUT_THROUGH_ON_RX_PATH_MASK	0x1U
#define ENABLE_CUT_THROUGH_ON_TX_PATH_MASK	0x1U
#endif

/* MSIGEN Registers */

#define MSI_INT_TX_CH0		3
#define MSI_INT_RX_CH0		11
#define MSI_INT_EXT_PHY		20

#ifdef TC956X_SW_MSI
#define MSI_INT_SW_MSI		24
#endif

#ifdef TC956X_SRIOV_VF //VF_DRV related
#define TC956X_MSIGENCEN	18

/* MSIGEN Registers */
#define TC956X_MSI_BASE		(0xF000)
#define TC956X_MSI_PF0		(0x0000)
#define TC956X_MSI_PF1		(0x0100)

#define TC956X_MSI_OUT_EN_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						TC956X_MSI_PF1 + (vf_id * 0x100) + (pf_id * 0x300)  + (0x0000))
#define TC956X_MSI_MASK_SET_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						TC956X_MSI_PF1 + (vf_id * 0x100) + (pf_id * 0x300) + (0x0008))
#define TC956X_MSI_MASK_CLR_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						TC956X_MSI_PF1 + (vf_id * 0x100) + (pf_id * 0x300) + (0x000c))
#define TC956X_MSI_VECT_SET0_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						TC956X_MSI_PF1 + (vf_id * 0x100) + (pf_id * 0x300) + (0x0020))
#define TC956X_MSI_VECT_SET1_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						TC956X_MSI_PF1 + (vf_id * 0x100) + (pf_id * 0x300) + (0x0024))
#define TC956X_MSI_VECT_SET2_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						TC956X_MSI_PF1 + (vf_id * 0x100) + (pf_id * 0x300) + (0x0028))
#define TC956X_MSI_VECT_SET3_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						TC956X_MSI_PF1 + (vf_id * 0x100) + (pf_id * 0x300) + (0x002C))
#define TC956X_MSI_VECT_SET4_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						TC956X_MSI_PF1 + (vf_id * 0x100) + (pf_id * 0x300) + (0x0030))
#define TC956X_MSI_VECT_SET5_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						TC956X_MSI_PF1 + (vf_id * 0x100) + (pf_id * 0x300) + (0x0034))
#define TC956X_MSI_VECT_SET6_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						TC956X_MSI_PF1 + (vf_id * 0x100) + (pf_id * 0x300) + (0x0038))
#define TC956X_MSI_VECT_SET7_OFFSET(pf_id, vf_id)	(TC956X_MSI_BASE +\
						TC956X_MSI_PF1 + (vf_id * 0x100) + (pf_id * 0x300) + (0x003C))

#define TC956X_MSI_EVENT_OFFSET(pf_id, vf_id)		(TC956X_MSI_BASE +\
						TC956X_MSI_PF1 + (vf_id * 0x100) + (pf_id * 0x300) + (0x0068))


#define TC956X_MSI_OUT_EN_CLR	(0x00000000)
/* Enabled LPI_INT, EVENT_INT, TX_INT[3], TX_INT[4], RX_INT[3], RX_INT[4]
 * XPCS_INT, PFMAILBOX_INT
 */
//#define TC956X_MSI_OUT_EN	(0x0028C0C5)
#define TC956X_MSI_OUT_EN	(0x0027FFF8)


#define TC956X_MSI_MASK_SET	(0xFFFFFFFE)
#define TC956X_MSI_MASK_CLR	(0x00000001)
#define TC956X_MSI_SET0		(0x00000000)
#define TC956X_MSI_SET1		(0x00000000)
#define TC956X_MSI_SET2		(0x00000000)
#define TC956X_MSI_SET3		(0x00000000)
#define TC956X_MSI_SET4		(0x00000000)
#define TC956X_MSI_SET5		(0x00000000)
#define TC956X_MSI_SET6		(0x00000000)
#define TC956X_MSI_SET7		(0x00000000)

#define TC956X_MSI_INT_TXDMA_CH0	(3)
#define TC956X_MSI_INT_RXDMA_CH0	(11)
#define TC956X_MSI_INT_MBX		(21)

#endif // #ifdef TC956X_SRIOV_VF

#if defined(TC956X_AUTOMOTIVE_CONFIG)
#define CLASS_B_CH			5 /* Class B Queue */
#define CLASS_CDT_CH			7 /* CDT Queue */
#endif

#if defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
#define HOST_BEST_EFF_CH		3 /* Best effort traffic Queue*/
#define UNTAGGED_PTP_CH			4 /* Untagged PTP Queue */
#define CLASS_B_CH			5 /* Class B Queue */
#define CLASS_A_CH			6 /* Class A Queue */
#define CLASS_CDT_CH			7 /* CDT Queue */

#define TC956X_GPTP_TX_CH		UNTAGGED_PTP_CH
/* All data is redirected to Legacy Channel for SRIOV Usecase */
#define LEGACY_VLAN_TAGGED_CH		HOST_BEST_EFF_CH
#define AVB_CLASS_B_TX_CH		HOST_BEST_EFF_CH
#define AVB_CLASS_A_TX_CH		HOST_BEST_EFF_CH
#define TSN_CLASS_CDT_TX_CH		HOST_BEST_EFF_CH

#elif defined(TC956X_SRIOV_PF) && (defined(TC956X_AUTOMOTIVE_CONFIG) || defined(TC956X_ENABLE_MAC2MAC_BRIDGE))
#define HOST_BEST_EFF_CH		0 /* Legacy channel is best effort traffic */
#define LEGACY_VLAN_TAGGED_CH		0 /* Legacy VLAN tagged queue */
#define TC956X_GPTP_TX_CH		4 /* gPTP Tx Channel */
#define AVB_CLASS_B_TX_CH		5 /* AVB Class B Qeuue */
#define AVB_CLASS_A_TX_CH		6 /* AVB Class A Qeuue */
#define TSN_CLASS_CDT_TX_CH		7 /* Express Control Traffic */

#else
#ifdef TC956X

#ifndef TC956X_SRIOV_VF
/* CPE usecase only TxCH 0 is applicable */
#define HOST_BEST_EFF_CH		0 /* Legacy channel is best effort traffic */
#define LEGACY_VLAN_TAGGED_CH	0 /* Legacy VLAN tagged queue */
#define TC956X_GPTP_TX_CH		0 /* gPTP Tx Channel */
#define AVB_CLASS_B_TX_CH		0 /* AVB Class B Qeuue */
#define AVB_CLASS_A_TX_CH		0 /* AVB Class A Qeuue */
#define TSN_CLASS_CDT_TX_CH		0 /* Express Control Traffic */
#endif
#endif
#endif

/* Scale factor for the CBS calculus */
#define AVB_CBS_SCALE	1024

#define TC956X_HOST_PHYSICAL_ADRS_MASK	(0x10) /* bit no 37: (1<<36) */

#define ETHNORMAL_LEN		1500
#define MAX_SUPPORTED_MTU	(ETH_FRAME_LEN + ETH_FCS_LEN + VLAN_HLEN)
#define MIN_SUPPORTED_MTU	(ETH_ZLEN + ETH_FCS_LEN + VLAN_HLEN)

#define HOST_MAC_ADDR_OFFSET 2
#define HOST_BC_ADDR_OFFSET 0

#endif /* End of TC956X Define */

/* These need to be power of two, and >= 4 */
#define DMA_TX_SIZE 512
#define DMA_RX_SIZE 512
#define TC956XMAC_GET_ENTRY(x, size)	((x + 1) & (size - 1))

/* default DMA channel for TC */
#if defined(TC956X_SRIOV_PF) && !defined(TC956X_AUTOMOTIVE_CONFIG) && !defined(TC956X_ENABLE_MAC2MAC_BRIDGE)
#define TC956X_TC_DEFAULT_DMA_CH (0x1 << 3)
#else
#define TC956X_TC_DEFAULT_DMA_CH (0x1 << 0)
#endif

#undef FRAME_FILTER_DEBUG
/* #define FRAME_FILTER_DEBUG */

#define TOTAL_VF 3
/* Extra statistic and debug information exposed by ethtool */
struct tc956xmac_extra_stats {
	/* Transmit errors */
	u64 tx_underflow ____cacheline_aligned;
	u64 tx_carrier;
	u64 tx_losscarrier;
	u64 vlan_tag;
	u64 tx_deferred;
	u64 tx_vlan;
	u64 tx_jabber;
	u64 tx_frame_flushed;
	u64 tx_payload_error;
	u64 tx_ip_header_error;
	/* Receive errors */
	u64 rx_desc;
	u64 sa_filter_fail;
	u64 overflow_error;
	u64 ipc_csum_error;
	u64 rx_collision;
	u64 rx_crc_errors;
	u64 dribbling_bit;
	u64 rx_length;
	u64 rx_mii;
	u64 rx_multicast;
	u64 rx_gmac_overflow;
	u64 rx_watchdog;
	u64 da_rx_filter_fail;
	u64 sa_rx_filter_fail;
	u64 rx_missed_cntr;
	u64 rx_overflow_cntr;
	u64 rx_vlan;
	u64 rx_split_hdr_pkt_n;
	/* Tx/Rx IRQ error info */
	u64 tx_undeflow_irq;
	u64 tx_process_stopped_irq[TC956XMAC_CH_MAX];
	u64 tx_jabber_irq;
	u64 rx_overflow_irq;
	u64 rx_buf_unav_irq[TC956XMAC_CH_MAX];
	u64 rx_process_stopped_irq;
	u64 rx_watchdog_irq;
	u64 tx_early_irq;
	u64 fatal_bus_error_irq[TC956XMAC_CH_MAX];
	/* Tx/Rx IRQ Events */
	u64 rx_early_irq;
	u64 threshold;
	u64 tx_pkt_n[TC956XMAC_CH_MAX];
	u64 tx_pkt_errors_n[TC956XMAC_CH_MAX];
	u64 rx_pkt_n[TC956XMAC_CH_MAX];
	u64 normal_irq_n[TC956XMAC_CH_MAX];
	u64 rx_normal_irq_n[TC956XMAC_CH_MAX];
	u64 napi_poll_tx[TC956XMAC_CH_MAX];
	u64 napi_poll_rx[TC956XMAC_CH_MAX];
	u64 tx_normal_irq_n[TC956XMAC_CH_MAX];
	u64 tx_clean[TC956XMAC_CH_MAX];
	u64 tx_set_ic_bit;
	u64 irq_receive_pmt_irq_n;
	/* MMC info */
	u64 mmc_tx_irq_n;
	u64 mmc_rx_irq_n;
	u64 mmc_rx_csum_offload_irq_n;
	/* EEE */
	u64 irq_tx_path_in_lpi_mode_n;
	u64 irq_tx_path_exit_lpi_mode_n;
	u64 irq_rx_path_in_lpi_mode_n;
	u64 irq_rx_path_exit_lpi_mode_n;
	u64 phy_eee_wakeup_error_n;
	/* Extended RDES status */
	u64 ip_hdr_err;
	u64 ip_payload_err;
	u64 ip_csum_bypassed;
	u64 ipv4_pkt_rcvd;
	u64 ipv6_pkt_rcvd;
	u64 no_ptp_rx_msg_type_ext;
	u64 ptp_rx_msg_type_sync;
	u64 ptp_rx_msg_type_follow_up;
	u64 ptp_rx_msg_type_delay_req;
	u64 ptp_rx_msg_type_delay_resp;
	u64 ptp_rx_msg_type_pdelay_req;
	u64 ptp_rx_msg_type_pdelay_resp;
	u64 ptp_rx_msg_type_pdelay_follow_up;
	u64 ptp_rx_msg_type_announce;
	u64 ptp_rx_msg_type_management;
	u64 ptp_rx_msg_pkt_reserved_type;
	u64 ptp_frame_type;
	u64 ptp_ver;
	u64 timestamp_dropped;
	u64 av_pkt_rcvd;
	u64 av_tagged_pkt_rcvd;
	u64 vlan_tag_priority_val;
	u64 l3_filter_match;
	u64 l4_filter_match;
	u64 l3_l4_filter_no_match;
	/* PCS */
	u64 irq_pcs_ane_n;
	u64 irq_pcs_link_n;
	u64 irq_rgmii_n;
	u64 pcs_link;
	u64 pcs_duplex;
	u64 pcs_speed;
	/* debug register */
	u64 mtl_tx_status_fifo_full;
	u64 mtl_tx_fifo_not_empty[MTL_MAX_TX_QUEUES];
	u64 mmtl_fifo_ctrl[MTL_MAX_TX_QUEUES];
	u64 mtl_tx_fifo_read_ctrl_write[MTL_MAX_TX_QUEUES];
	u64 mtl_tx_fifo_read_ctrl_wait[MTL_MAX_TX_QUEUES];
	u64 mtl_tx_fifo_read_ctrl_read[MTL_MAX_TX_QUEUES];
	u64 mtl_tx_fifo_read_ctrl_idle[MTL_MAX_TX_QUEUES];
	u64 mac_tx_in_pause[MTL_MAX_TX_QUEUES];
	u64 mac_tx_frame_ctrl_xfer;
	u64 mac_tx_frame_ctrl_idle;
	u64 mac_tx_frame_ctrl_wait;
	u64 mac_tx_frame_ctrl_pause;
	u64 mac_gmii_tx_proto_engine;
	u64 mtl_rx_fifo_fill_level_full[MTL_MAX_RX_QUEUES];
	u64 mtl_rx_fifo_fill_above_thresh[MTL_MAX_RX_QUEUES];
	u64 mtl_rx_fifo_fill_below_thresh[MTL_MAX_RX_QUEUES];
	u64 mtl_rx_fifo_fill_level_empty[MTL_MAX_RX_QUEUES];
	u64 mtl_rx_fifo_read_ctrl_flush[MTL_MAX_RX_QUEUES];
	u64 mtl_rx_fifo_read_ctrl_read[MTL_MAX_RX_QUEUES];
	u64 mtl_rx_fifo_read_ctrl_status[MTL_MAX_RX_QUEUES];
	u64 mtl_rx_fifo_read_ctrl_idle[MTL_MAX_RX_QUEUES];
	u64 mtl_rx_fifo_ctrl_active[MTL_MAX_RX_QUEUES];
	u64 mac_rx_frame_ctrl_fifo;
	u64 mac_gmii_rx_proto_engine;
	/* TSO */
	u64 tx_tso_frames[TC956XMAC_CH_MAX];
	u64 tx_tso_nfrags[TC956XMAC_CH_MAX];

	/* Tx desc statistics */
	u64 txch_status[TC956XMAC_CH_MAX];
	u64 txch_control[TC956XMAC_CH_MAX];
	u64 txch_desc_list_haddr[TC956XMAC_CH_MAX];
	u64 txch_desc_list_laddr[TC956XMAC_CH_MAX];
	u64 txch_desc_ring_len[TC956XMAC_CH_MAX];
	u64 txch_desc_curr_haddr[TC956XMAC_CH_MAX];
	u64 txch_desc_curr_laddr[TC956XMAC_CH_MAX];
	u64 txch_desc_tail[TC956XMAC_CH_MAX];
	u64 txch_desc_buf_haddr[TC956XMAC_CH_MAX];
	u64 txch_desc_buf_laddr[TC956XMAC_CH_MAX];
	u64 txch_sw_cur_tx[TC956XMAC_CH_MAX];
	u64 txch_sw_dirty_tx[TC956XMAC_CH_MAX];

	/* Rx desc statistics */
	u64 rxch_status[TC956XMAC_CH_MAX];
	u64 rxch_control[TC956XMAC_CH_MAX];
	u64 rxch_desc_list_haddr[TC956XMAC_CH_MAX];
	u64 rxch_desc_list_laddr[TC956XMAC_CH_MAX];
	u64 rxch_desc_ring_len[TC956XMAC_CH_MAX];
	u64 rxch_desc_curr_haddr[TC956XMAC_CH_MAX];
	u64 rxch_desc_curr_laddr[TC956XMAC_CH_MAX];
	u64 rxch_desc_tail[TC956XMAC_CH_MAX];
	u64 rxch_desc_buf_haddr[TC956XMAC_CH_MAX];
	u64 rxch_desc_buf_laddr[TC956XMAC_CH_MAX];
	u64 rxch_sw_cur_rx[TC956XMAC_CH_MAX];
	u64 rxch_sw_dirty_rx[TC956XMAC_CH_MAX];
#ifdef TC956X_SRIOV_PF
	u64 mbx_pf_sent_vf[TOTAL_VF];
	u64 mbx_pf_rcvd_vf[TOTAL_VF];
#else
	u64 mbx_vf_sent_pf;
	u64 mbx_vf_rcvd_pf;
#endif

	/*debug interrupt counters */
	u64 total_interrupts;
	u64 lpi_intr_n;
	u64 pmt_intr_n;
	u64 event_intr_n;
	u64 tx_intr_n;
	u64 rx_intr_n;
	u64 xpcs_intr_n;
	u64 phy_intr_n;
	u64 sw_msi_n;
	/*MTL Debug counters */
	u64 mtl_tx_underflow[MTL_MAX_TX_QUEUES];
	u64 mtl_rx_miss_pkt_cnt[MTL_MAX_RX_QUEUES];
	u64 mtl_rx_overflow_pkt_cnt[MTL_MAX_RX_QUEUES];
	u64 rxch_watchdog_timer[TC956XMAC_CH_MAX];
	u64 link_partner_pause_frame_cnt;

	/*m3 SRAM debug counters */
	u64 m3_debug_cnt0;
	u64 m3_debug_cnt1;
	u64 m3_debug_cnt2;
	u64 m3_debug_cnt3;
	u64 m3_debug_cnt4;
	u64 m3_debug_cnt5;
	u64 m3_debug_cnt6;
	u64 m3_debug_cnt7;
	u64 m3_debug_cnt8;
	u64 m3_debug_cnt9;
	u64 m3_debug_cnt10;
	u64 m3_watchdog_exp_cnt;
	u64 m3_watchdog_monitor_cnt;
	u64 m3_debug_cnt13;
	u64 m3_debug_cnt14;
	u64 m3_systick_cnt_upper_value;
	u64 m3_systick_cnt_lower_value;
	u64 m3_tx_timeout_port0;
	u64 m3_tx_timeout_port1;
	u64 m3_debug_cnt19;
	u64 m3_tx_pcie_addr_loc_port0[TC956XMAC_CH_MAX];
	u64 m3_tx_pcie_addr_loc_port1[TC956XMAC_CH_MAX];
	u64 m3_rx_pcie_addr_loc_port0[TC956XMAC_CH_MAX];
	u64 m3_rx_pcie_addr_loc_port1[TC956XMAC_CH_MAX];

};

/* Safety Feature statistics exposed by ethtool */
struct tc956xmac_safety_stats {
	unsigned long mac_errors[32];
	unsigned long mtl_errors[32];
	unsigned long dma_errors[32];
};

#ifdef TC956X_SRIOV_VF
struct tc956x_sw_counters {
	u64 tx_frame_count_good_bad;
	u64 rx_frame_count_good_bad;
	u64 rx_frame_count_good;
	u64 rx_fame_count_bad;
	u64 rx_packet_good_octets;
	u64 rx_header_good_octets;
	u64 rx_av_tagged_datapacket_count;
	u64 rx_av_tagged_controlpacket_count;
	u64 rx_nonav_packet_count;
	u64 rx_tunnel_packet_count;
	u64 rx_non_ip_pkt_count;
	u64 rx_ipv4_tcp_pkt_count;
	u64 rx_ipv4_udp_pkt_count;
	u64 rx_ipv4_icmp_pkt_count;
	u64 rx_ipv4_igmp_pkt_count;
	u64 rx_ipv4_unkown_pkt_count;
	u64 rx_ipv6_tcp_pkt_count;
	u64 rx_ipv6_udp_pkt_count;
	u64 rx_ipv6_icmp_pkt_count;
	u64 rx_ipv6_unkown_pkt_count;
	u64 rx_err_wd_timeout_count;
	u64 rx_err_gmii_inv_count;
	u64 rx_err_crc_count;
	u64 rx_err_giant_count;
	u64 rx_err_checksum_count;
	u64 rx_err_overflow_count;
	u64 rx_err_bus_count;
	u64 rx_err_pkt_len_count;
	u64 rx_err_runt_pkt_count;
	u64 rx_err_dribble_count;
	u64 rx_err_t_out_ip_header_count;
	u64 rx_err_t_out_ip_pl_l4_csum_count;
	u64 rx_err_t_in_ip_header_count;
	u64 rx_err_t_in_ip_pl_l4_csum_count;
	u64 rx_err_t_invalid_vlan_header;
	u64 rx_l2_len_pkt_count;
	u64 rx_l2_mac_control_pkt_count;
	u64 rx_l2_dcb_control_pkt_count;
	u64 rx_l2_arp_pkt_count;
	u64 rx_l2_oam_type_pkt_count;
	u64 rx_l2_untg_typ_match_pkt_count;
	u64 rx_l2_other_type_pkt_count;
	u64 rx_l2_single_svlan_pkt_count;
	u64 rx_l2_single_cvlan_pkt_count;
	u64 rx_l2_d_cvlan_cvlan_pkt_count;
	u64 rx_l2_d_svlan_svlan_pkt_count;
	u64 rx_l2_d_svlan_cvlan_pkt_count;
	u64 rx_l2_d_cvlan_svlan_pkt_count;
	u64 rx_l2_untg_av_control_pkt_count;
	u64 rx_ptp_no_msg;
	u64 rx_ptp_msg_type_sync;
	u64 rx_ptp_msg_type_follow_up;
	u64 rx_ptp_msg_type_delay_req;
	u64 rx_ptp_msg_type_delay_resp;
	u64 rx_ptp_msg_type_pdelay_req;
	u64 rx_ptp_msg_type_pdelay_resp;
	u64 rx_ptp_msg_type_pdelay_follow_up;
	u64 rx_ptp_msg_type_announce;
	u64 rx_ptp_msg_type_management;
	u64 rx_ptp_msg_pkt_signaling;
	u64 rx_ptp_msg_pkt_reserved_type;
};
#endif //#ifdef TC956X_SRIOV_VF

struct tc956x_mac_addr {
	u8 mac_address[6];
	u8 status;
	u8 counter;
	u8 vf[4];
};

struct vf_status {
	u8 vf_number;
	u8 loc_counter;
};

struct tc956x_vlan_id {
	u16 vid;
	u8 status;
	u8 glo_counter;
	struct vf_status vf[4];
};
/* Number of fields in Safety Stats */
#define TC956XMAC_SAFETY_FEAT_SIZE	\
	(sizeof(struct tc956xmac_safety_stats) / sizeof(unsigned long))

/* CSR Frequency Access Defines*/
#define CSR_F_35M	35000000
#define CSR_F_60M	60000000
#define CSR_F_100M	100000000
#define CSR_F_150M	150000000
#define CSR_F_250M	250000000
#define CSR_F_300M	300000000

#define	MAC_CSR_H_FRQ_MASK	0x20

#define HASH_TABLE_SIZE 64
#define MAX_MAC_ADDR_FILTERS 32
#define PAUSE_TIME 0xffff

/* Flow Control defines */
#define FLOW_OFF	0
#define FLOW_RX		1
#define FLOW_TX		2
#define FLOW_AUTO	(FLOW_TX | FLOW_RX)

/* PCS defines */
#define TC956XMAC_PCS_RGMII	(1 << 0)
#define TC956XMAC_PCS_SGMII	(1 << 1)
#define TC956XMAC_PCS_TBI		(1 << 2)
#define TC956XMAC_PCS_RTBI		(1 << 3)
#define TC956XMAC_PCS_USXGMII	(1 << 4)


#define SF_DMA_MODE 1		/* DMA STORE-AND-FORWARD Operation Mode */

/* DAM HW feature register fields */
#define DMA_HW_FEAT_MIISEL	0x00000001	/* 10/100 Mbps Support */
#define DMA_HW_FEAT_GMIISEL	0x00000002	/* 1000 Mbps Support */
#define DMA_HW_FEAT_HDSEL	0x00000004	/* Half-Duplex Support */
#define DMA_HW_FEAT_EXTHASHEN	0x00000008	/* Expanded DA Hash Filter */
#define DMA_HW_FEAT_HASHSEL	0x00000010	/* HASH Filter */
#define DMA_HW_FEAT_ADDMAC	0x00000020	/* Multiple MAC Addr Reg */
#define DMA_HW_FEAT_PCSSEL	0x00000040	/* PCS registers */
#define DMA_HW_FEAT_L3L4FLTREN	0x00000080	/* Layer 3 & Layer 4 Feature */
#define DMA_HW_FEAT_SMASEL	0x00000100	/* SMA(MDIO) Interface */
#define DMA_HW_FEAT_RWKSEL	0x00000200	/* PMT Remote Wakeup */
#define DMA_HW_FEAT_MGKSEL	0x00000400	/* PMT Magic Packet */
#define DMA_HW_FEAT_MMCSEL	0x00000800	/* RMON Module */
#define DMA_HW_FEAT_TSVER1SEL	0x00001000	/* Only IEEE 1588-2002 */
#define DMA_HW_FEAT_TSVER2SEL	0x00002000	/* IEEE 1588-2008 PTPv2 */
#define DMA_HW_FEAT_EEESEL	0x00004000	/* Energy Efficient Ethernet */
#define DMA_HW_FEAT_AVSEL	0x00008000	/* AV Feature */
#define DMA_HW_FEAT_TXCOESEL	0x00010000	/* Checksum Offload in Tx */
#define DMA_HW_FEAT_RXTYP1COE	0x00020000	/* IP COE (Type 1) in Rx */
#define DMA_HW_FEAT_RXTYP2COE	0x00040000	/* IP COE (Type 2) in Rx */
#define DMA_HW_FEAT_RXFIFOSIZE	0x00080000	/* Rx FIFO > 2048 Bytes */
#define DMA_HW_FEAT_RXCHCNT	0x00300000	/* No. additional Rx Channels */
#define DMA_HW_FEAT_TXCHCNT	0x00c00000	/* No. additional Tx Channels */
#define DMA_HW_FEAT_ENHDESSEL	0x01000000	/* Alternate Descriptor */
/* Timestamping with Internal System Time */
#define DMA_HW_FEAT_INTTSEN	0x02000000
#define DMA_HW_FEAT_FLEXIPPSEN	0x04000000	/* Flexible PPS Output */
#define DMA_HW_FEAT_SAVLANINS	0x08000000	/* Source Addr or VLAN */
#define DMA_HW_FEAT_ACTPHYIF	0x70000000	/* Active/selected PHY iface */
#define DEFAULT_DMA_PBL		8

/* PCS status and mask defines */
#define	PCS_ANE_IRQ		BIT(2)	/* PCS Auto-Negotiation */
#define	PCS_LINK_IRQ		BIT(1)	/* PCS Link */
#define	PCS_RGSMIIIS_IRQ	BIT(0)	/* RGMII or SMII Interrupt */

/* Max/Min RI Watchdog Timer count value */
#define MAX_DMA_RIWT		0xFF
#define MIN_DMA_RIWT		0x1
#define DEF_DMA_RIWT		0xa0
/* Tx coalesce parameters */
#define TC956XMAC_COAL_TX_TIMER	1000
#define TC956XMAC_MAX_COAL_TX_TICK	100000
#define TC956XMAC_TX_MAX_FRAMES	256
#define TC956XMAC_RX_MAX_FRAMES    32
#define TC956XMAC_TX_FRAMES	64
#define TC956XMAC_RX_FRAMES	0

/* Rx IPC status */
enum rx_frame_status {
	good_frame = 0x0,
	discard_frame = 0x1,
	csum_none = 0x2,
	llc_snap = 0x4,
	dma_own = 0x8,
	rx_not_ls = 0x10,
};

/* Tx status */
enum tx_frame_status {
	tx_done = 0x0,
	tx_not_ls = 0x1,
	tx_err = 0x2,
	tx_dma_own = 0x4,
};

enum dma_irq_status {
	tx_hard_error = 0x1,
	tx_hard_error_bump_tc = 0x2,
	handle_rx = 0x4,
	handle_tx = 0x8,
};

#ifdef TC956X_SRIOV_VF

#define MBX_MSG_OFST		4 /* First 4 bytes for ACK management */
#define MBX_MSG_SIZE		60 /* Excluding ACK */
#define MBX_ACK_SIZE		4 /* ACK management */
#define MBX_TOT_SIZE		(MBX_MSG_SIZE + MBX_ACK_SIZE)

#define ACK			0x01
#define NACK			0x02

/* Mailbox opcode definitions */
#define OPCODE_MBX_ADD_MAC_ADDR			0x01
#define OPCODE_MBX_SET_TX_Q_WEIGHT		0x02
#define OPCODE_MBX_CFG_CBS				0x03
#define OPCODE_MBX_SET_TX_Q_PRIOR		0x04
#define OPCODE_MBX_RESET_EEE_MODE       0x05
#define OPCODE_MBX_VF_IOCTL				0x06
#define OPCODE_MBX_VF_ETHTOOL			0x07
#define OPCODE_MBX_VF_ADD_MAC			0x08
#define OPCODE_MBX_VF_DELETE_MAC		0x09
#define OPCODE_MBX_VF_ADD_VLAN			0x10
#define OPCODE_MBX_VF_DELETE_VLAN		0x11
#define OPCODE_MBX_SET_DMA_TX_MODE		0x12
#define OPCODE_MBX_VF_GET_LINK_STATUS	0x13
#define OPCODE_MBX_PHY_LINK		0x14
#define OPCODE_MBX_SETUP_CBS		0x15
#define OPCODE_MBX_RX_CRC		0x16
#define OPCODE_MBX_RX_CSUM		0x17
#define OPCODE_MBX_DRV_CAP		0x18
#define OPCODE_MBX_VF_GET_PAUSE_PARAM	0x19
#define OPCODE_MBX_VF_GET_EEE			0x20
#define OPCODE_MBX_VF_GET_TS_INFO		0x22
#define OPCODE_MBX_VF_GET_MII_PHY		0x23
#define OPCODE_MBX_VF_GET_PAUSE_PARAM_2 0x24
#define OPCODE_MBX_GET_UMAC_ADDR        0x25
#define OPCODE_VF_RESET                 0x26
#define OPCODE_MBX_DMA_CH_TLPTR			0x29
#define OPCODE_MBX_SETUP_ETF			0x30
#define OPCODE_MBX_FLR					0x33
#define OPCODE_MBX_DMA_ERR		0x34
#define OPCODE_MBX_ACK_MSG				0xFF

#define SIZE_MBX_SET_TX_Q_WEIGHT		8
#define SIZE_MBX_CFG_CBS				20
#define SIZE_MBX_SETUP_ETF				5
#define SIZE_MBX_SETUP_CBS				21
#define SIZE_MBX_CFG_EST				56
#define SIZE_MBX_CFG_FPE				20
#define SIZE_MBX_SET_TX_Q_PRIOR			8
#define SIZE_MBX_SET_DMA_TX_MODE		13
#define SIZE_MBX_VF_GET_LINK_STATUS		0
#define SIZE_MBX_PHY_LINK			12
#define SIZE_MBX_RX_CRC		4
#define SIZE_MBX_RX_CSUM	4
#define SIZE_MBX_DRV_CAP	            4
#define SIZE_SET_UMAC_ADDR              10
#define SIZE_GET_UMAC_ADDR              4
#define SIZE_MBX_VF_MAC				6
#define SIZE_MBX_VF_VLAN				2
#define SIZE_MBX_VF_REG_WR			13
#define SIZE_MBX_VF_SPEED			1
#define SIZE_MBX_SET_GET_CBS_1			56
#define SIZE_MBX_SET_GET_CBS_2			44
#define SIZE_MBX_SET_GET_EST_1		56
#define SIZE_MBX_SET_GET_RXP_1		56
#define SIZE_MBX_SET_GET_FPE_1		20
#define SIZE_MBX_VF_GET_CBS			2
#define SIZE_MBX_VF_PAUSE_PARAM		1
#define SIZE_MBX_VF_EEE				1
#define SIZE_MBX_VF_TS_INFO			1
#define SIZE_VF_RESET			    1
#define SIZE_MBX_VF_TIMESTAMP		2
#define SIZE_MBX_VF_GET_EST			1
#define SIZE_MBX_VF_GET_RXP			1
#define SIZE_MBX_VF_GET_FPE			1
#define SIZE_MBX_VF_GET_MII_REG		23
#define SIZE_MBX_RX_DMA_TL_PTR		4
#define SIZE_MBX_GET_HW_STMP    12

#define MAX_SIZE_GCL_MSG 14
#define EST_FIX_MSG_LEN 28
#define RXP_FIX_MSG_LEN 16
#define VF_UP       1
#define VF_DOWN     0
#define VF_SUSPEND  2
#define VF_RELEASE  3

#define SCH_WQ_PF_FLR 1
#define SCH_WQ_RX_DMA_ERR 2
#define SCH_WQ_LINK_STATE_UP 3
#endif /* #ifdef TC956X_SRIOV_VF */

/* EEE and LPI defines */
#define	CORE_IRQ_TX_PATH_IN_LPI_MODE	(1 << 0)
#define	CORE_IRQ_TX_PATH_EXIT_LPI_MODE	(1 << 1)
#define	CORE_IRQ_RX_PATH_IN_LPI_MODE	(1 << 2)
#define	CORE_IRQ_RX_PATH_EXIT_LPI_MODE	(1 << 3)

#define CORE_IRQ_MTL_RX_OVERFLOW	BIT(8)

/* Physical Coding Sublayer */
struct rgmii_adv {
	unsigned int pause;
	unsigned int duplex;
	unsigned int lp_pause;
	unsigned int lp_duplex;
};

#define TC956XMAC_PCS_PAUSE	1
#define TC956XMAC_PCS_ASYM_PAUSE	2

/* DMA HW capabilities */
struct dma_features {
	unsigned int mbps_10_100;
	unsigned int mbps_1000;
	unsigned int half_duplex;
	unsigned int hash_filter;
	unsigned int multi_addr;
	unsigned int pcs;
	unsigned int sma_mdio;
	unsigned int pmt_remote_wake_up;
	unsigned int pmt_magic_frame;
	unsigned int rmon;
	/* IEEE 1588-2002 */
	unsigned int time_stamp;
	/* IEEE 1588-2008 */
	unsigned int atime_stamp;
	/* 802.3az - Energy-Efficient Ethernet (EEE) */
	unsigned int eee;
	unsigned int av;
	unsigned int hash_tb_sz;
	unsigned int tsoen;
	/* TX and RX csum */
	unsigned int tx_coe;
	unsigned int rx_coe;
	unsigned int rx_coe_type1;
	unsigned int rx_coe_type2;
	unsigned int rxfifo_over_2048;
	/* TX and RX number of channels */
	unsigned int number_rx_channel;
	unsigned int number_tx_channel;
	/* TX and RX number of queues */
	unsigned int number_rx_queues;
	unsigned int number_tx_queues;
	/* PPS output */
	unsigned int pps_out_num;
	/* Alternate (enhanced) DESC mode */
	unsigned int enh_desc;
	/* TX and RX FIFO sizes */
	unsigned int tx_fifo_size;
	unsigned int rx_fifo_size;
	/* Automotive Safety Package */
	unsigned int asp;
	/* RX Parser */
	unsigned int spram;
	unsigned int frpsel;
	unsigned int frpbs;
	unsigned int frpes;
	unsigned int addr64;
	unsigned int rssen;
	unsigned int vlhash;
	unsigned int sphen;
	unsigned int vlins;
	unsigned int dvlan;
	unsigned int l3l4fnum;
	unsigned int arpoffsel;
	/* TSN Features */
	unsigned int estwid;
	unsigned int estdep;
	unsigned int estsel;
	unsigned int fpesel;
	unsigned int tbssel;
	unsigned int ptoen;
	unsigned int osten;
};

/* RX Buffer size must be multiple of 4/8/16 bytes */
#define BUF_SIZE_16KiB 16368
#define BUF_SIZE_8KiB 8188
#define BUF_SIZE_4KiB 4096
#define BUF_SIZE_2KiB 2048

/* Power Down and WOL */
#define PMT_NOT_SUPPORTED 0
#define PMT_SUPPORTED 1

/* Common MAC defines */
#define MAC_CTRL_REG		(MAC_OFFSET + 0x00000000)	/* MAC Control */
#define MAC_ENABLE_TX		0x00000008	/* Transmitter Enable */
#define MAC_ENABLE_RX		0x00000004	/* Receiver Enable */

/* Default LPI timers */
#define TC956XMAC_DEFAULT_LIT_LS	0x3E8
#define TC956XMAC_DEFAULT_TWT_LS	0x1E
#define TC956XMAC_LIT_LS		0x0011
#define TC956XMAC_TWT_LS		0x0028
#define TC956XMAC_TIC_1US_CNTR		0x7c
#define TC956XMAC_LPIET_600US		0x258
#define TC956X_PHY_SPEED_5G		5000
#define TC956X_PHY_SPEED_2_5G		2500

#define TC956XMAC_CHAIN_MODE	0x1
#define TC956XMAC_RING_MODE	0x2

#define JUMBO_LEN		(TC956XMAC_ALIGN(9000))

/* Receive Side Scaling */
#define TC956XMAC_RSS_HASH_KEY_SIZE	40
#define TC956XMAC_RSS_MAX_TABLE_SIZE	256

/* VLAN */
#define TC956XMAC_VLAN_NONE	0x0
#define TC956XMAC_VLAN_REMOVE	0x1
#define TC956XMAC_VLAN_INSERT	0x2
#define TC956XMAC_VLAN_REPLACE	0x3

#define TC956X_ENABLE 1
#define TC956X_DISABLE 0

/* Numbers in Words */

#define TC956X_ZERO		0
#define TC956X_ONE		1
#define TC956X_TWO		2
#define TC956X_THREE		3
#define TC956X_FOUR		4
#define TC956X_FIVE		5
#define TC956X_SIX		6
#define TC956X_SEVEN		7
#define TC956X_EIGHT		8
#define TC956X_NINE		9
#define TC956X_TEN		10
#define TC956X_SIXTEEN		16
#define TC956X_TWENTY_FOUR	24
#define TC956X_FORTY_EIGHT	48
#define TC956X_FORTY_NINE	49

/* EEPROM Mac address mask values */
#define EEPROM_MAC_ADDR_MASK1 0x000000FF
#define EEPROM_MAC_ADDR_MASK2 0x0000FF00
#define EEPROM_MAC_ADDR_MASK3 0x00FF0000
#define EEPROM_MAC_ADDR_MASK4 0xFF000000

#define TC956X_MIN_LPI_AUTO_ENTRY_TIMER		0
#define TC956X_MAX_LPI_AUTO_ENTRY_TIMER		0xFFFF8 /* LPI Entry timer is in the units of 8 micro second granularity. So mask the last 3 bits. */

extern const struct tc956xmac_desc_ops enh_desc_ops;
extern const struct tc956xmac_desc_ops ndesc_ops;

struct mac_device_info;

extern const struct tc956xmac_hwtimestamp tc956xmac_ptp;
extern const struct tc956xmac_mode_ops dwmac4_ring_mode_ops;

struct mac_link {
	u32 speed_mask;
	u32 speed10;
	u32 speed100;
	u32 speed1000;
	u32 speed2500;
	u32 duplex;
	struct {
		u32 speed2500;
		u32 speed5000;
		u32 speed10000;
	} xgmii;
};

struct mii_regs {
	unsigned int addr;	/* MII Address */
	unsigned int data;	/* MII Data */
	unsigned int addr_shift;	/* MII address shift */
	unsigned int reg_shift;		/* MII reg shift */
	unsigned int addr_mask;		/* MII address mask */
	unsigned int reg_mask;		/* MII reg mask */
	unsigned int clk_csr_shift;
	unsigned int clk_csr_mask;
};

struct mac_device_info {
	const struct tc956xmac_ops *mac;
	const struct tc956xmac_desc_ops *desc;
	const struct tc956xmac_dma_ops *dma;
	const struct tc956xmac_mode_ops *mode;
	const struct tc956xmac_hwtimestamp *ptp;
	const struct tc956xmac_tc_ops *tc;
	const struct tc956x_msi_ops *msi;
#ifdef TC956X_SRIOV_PF
	const struct mac_rsc_mng_ops *rsc;
	const struct mac_mbx_ops *mbx;
	const struct tc956x_mbx_wrapper_ops *mbx_wrapper;
#endif
#ifdef TC956X_SRIOV_VF
	const struct mac_rsc_mng_ops *rsc;
	const struct mac_mbx_ops *mbx;
	const struct tc956xmac_mbx_wrapper_ops *mbx_wrapper;
#endif
	const struct tc956xmac_mmc_ops *mmc;
	const struct tc956xmac_pma_ops *pma;
	struct mii_regs mii;	/* MII register Addresses */
	struct mac_link link;
	void __iomem *pcsr;     /* vpointer to device CSRs */
	unsigned int multicast_filter_bins;
	unsigned int unicast_filter_entries;
	unsigned int mcast_bits_log2;
	unsigned int rx_csum;
	unsigned int pcs;
#ifdef TC956X
	unsigned int xpcs;
#endif
	unsigned int pmt;
	unsigned int ps;
};

struct tc956xmac_rx_routing {
	u32 reg_mask;
	u32 reg_shift;
};

int dwmac100_setup(struct tc956xmac_priv *priv);
int dwmac1000_setup(struct tc956xmac_priv *priv);
int dwmac4_setup(struct tc956xmac_priv *priv);
int dwxgmac2_setup(struct tc956xmac_priv *priv);

#ifdef TC956X_SRIOV_PF
void tc956xmac_set_mac_addr(void __iomem *ioaddr, u8 addr[6],
			 unsigned int high, unsigned int low);
void tc956xmac_get_mac_addr(void __iomem *ioaddr, unsigned char *addr,
			 unsigned int high, unsigned int low);
void tc956xmac_set_mac(struct tc956xmac_priv *priv, void __iomem *ioaddr, bool enable);
int tc956x_add_mac_addr(struct net_device *dev, const unsigned char *mac);
#elif defined TC956X_SRIOV_VF
void tc956xmac_vf_set_mac_addr(void __iomem *ioaddr, u8 addr[6],
			 unsigned int high, unsigned int low);
void tc956xmac_vf_get_mac_addr(void __iomem *ioaddr, unsigned char *addr,
			 unsigned int high, unsigned int low);
void tc956xmac_set_mac(struct tc956xmac_priv *priv, void __iomem *ioaddr, bool enable);
#endif
void tc956xmac_dwmac4_set_mac_addr(void __iomem *ioaddr, u8 addr[6],
				unsigned int high, unsigned int low);
void tc956xmac_dwmac4_get_mac_addr(void __iomem *ioaddr, unsigned char *addr,
				unsigned int high, unsigned int low);
void tc956xmac_dwmac4_set_mac(struct tc956xmac_priv *priv, void __iomem *ioaddr, bool enable);

void dwmac_dma_flush_tx_fifo(void __iomem *ioaddr);

extern const struct tc956xmac_mode_ops ring_mode_ops;
extern const struct tc956xmac_mode_ops chain_mode_ops;
extern const struct tc956xmac_desc_ops dwmac4_desc_ops;

#ifdef TC956X_SRIOV_VF
void tc956xmac_mailbox_service_event_schedule(struct tc956xmac_priv *priv);
#endif
#endif /* __COMMON_H__ */
