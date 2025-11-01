/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (C) 2015-2021 Texas Instruments Incorporated - https://www.ti.com
 */

#ifndef __ICSS_SWITCH_H
#define __ICSS_SWITCH_H

/* Basic Switch Parameters
 * Used to auto compute offset addresses on L3 OCMC RAM. Do not modify these
 * without changing firmware accordingly
 */
#define SWITCH_BUFFER_SIZE	(64 * 1024)	/* L3 buffer */
#define ICSS_BLOCK_SIZE		32		/* data bytes per BD */
#define BD_SIZE			4		/* byte buffer descriptor */
#define NUM_QUEUES		4		/* Queues on Port 0/1/2 */

#define PORT_LINK_MASK		0x1
#define PORT_IS_HD_MASK		0x2

/* Physical Port queue size (number of BDs). Same for both ports */
#define QUEUE_1_SIZE		97	/* Network Management high */
#define QUEUE_2_SIZE		97	/* Network Management low */
#define QUEUE_3_SIZE		97	/* Protocol specific */
#define QUEUE_4_SIZE		97	/* NRT (IP,ARP, ICMP) */

/* Host queue size (number of BDs). Each BD points to data buffer of 32 bytes.
 * HOST PORT QUEUES can buffer up to 4 full sized frames per queue
 */
#define	HOST_QUEUE_1_SIZE	194	/* Protocol and VLAN priority 7 & 6 */
#define HOST_QUEUE_2_SIZE	194	/* Protocol mid */
#define HOST_QUEUE_3_SIZE	194	/* Protocol low */
#define HOST_QUEUE_4_SIZE	194	/* NRT (IP, ARP, ICMP) */

#define COL_QUEUE_SIZE		0

/* NRT Buffer descriptor definition
 * Each buffer descriptor points to a max 32 byte block and has 32 bit in size
 * to have atomic operation.
 * PRU can address bytewise into memory.
 * Definition of 32 bit descriptor is as follows
 *
 * Bits		Name			Meaning
 * =============================================================================
 * 0..7		Index		points to index in buffer queue, max 256 x 32
 *				byte blocks can be addressed
 * 6		LookupSuccess	For switch, FDB lookup was successful (source
 *				MAC address found in FDB).
 *				For RED, NodeTable lookup was successful.
 * 7		Flood		Packet should be flooded (destination MAC
 *				address found in FDB). For switch only.
 * 8..12	Block_length	number of valid bytes in this specific block.
 *				Will be <=32 bytes on last block of packet
 * 13		More		"More" bit indicating that there are more blocks
 * 14		Shadow		indicates that "index" is pointing into shadow
 *				buffer
 * 15		TimeStamp	indicates that this packet has time stamp in
 *				separate buffer - only needed if PTP runs on
 *				host
 * 16..17	Port		different meaning for ingress and egress,
 *				Ingress: Port = 0 indicates phy port 1 and
 *				Port = 1 indicates phy port 2.
 *				Egress: 0 sends on phy port 1 and 1 sends on
 *				phy port 2. Port = 2 goes over MAC table
 *				look-up
 * 18..28	Length		11 bit of total packet length which is put into
 *				first BD only so that host access only one BD
 * 29		VlanTag		indicates that packet has Length/Type field of
 *				0x08100 with VLAN tag in following byte
 * 30		Broadcast	indicates that packet goes out on both physical
 *				ports,	there will be two bd but only one buffer
 * 31		Error		indicates there was an error in the packet
 */
#define PRUETH_BD_START_FLAG_MASK	BIT(0)
#define PRUETH_BD_START_FLAG_SHIFT	0

#define PRUETH_BD_HSR_FRAME_MASK	BIT(4)
#define PRUETH_BD_HSR_FRAME_SHIFT	4

#define PRUETH_BD_SUP_HSR_FRAME_MASK	BIT(5)
#define PRUETH_BD_SUP_HSR_FRAME_SHIFT	5

#define PRUETH_BD_LOOKUP_SUCCESS_MASK	BIT(6)
#define PRUETH_BD_LOOKUP_SUCCESS_SHIFT	6

#define PRUETH_BD_SW_FLOOD_MASK		BIT(7)
#define PRUETH_BD_SW_FLOOD_SHIFT	7

#define	PRUETH_BD_SHADOW_MASK		BIT(14)
#define	PRUETH_BD_SHADOW_SHIFT		14

#define PRUETH_BD_TIMESTAMP_MASK	BIT(15)
#define PRUETH_BD_TIMESTAMP_SHIFT	15

#define PRUETH_BD_PORT_MASK		GENMASK(17, 16)
#define PRUETH_BD_PORT_SHIFT		16

#define PRUETH_BD_LENGTH_MASK		GENMASK(28, 18)
#define PRUETH_BD_LENGTH_SHIFT		18

#define PRUETH_BD_BROADCAST_MASK	BIT(30)
#define PRUETH_BD_BROADCAST_SHIFT	30

#define PRUETH_BD_ERROR_MASK		BIT(31)
#define PRUETH_BD_ERROR_SHIFT		31

/* The following offsets indicate which sections of the memory are used
 * for EMAC internal tasks
 */
#define DRAM_START_OFFSET		0x1E98
#define SRAM_START_OFFSET		0x400

/* General Purpose Statistics
 * These are present on both PRU0 and PRU1 DRAM
 */
/* base statistics offset */
#define STATISTICS_OFFSET	0x1F00
#define STAT_SIZE		0x98

/* Offset for storing
 * 1. Storm Prevention Params
 * 2. PHY Speed Offset
 * 3. Port Status Offset
 * These are present on both PRU0 and PRU1
 */
/* 4 bytes */
#define STORM_PREVENTION_OFFSET_BC	(STATISTICS_OFFSET + STAT_SIZE)
/* 4 bytes */
#define PHY_SPEED_OFFSET		(STATISTICS_OFFSET + STAT_SIZE + 4)
/* 1 byte */
#define PORT_STATUS_OFFSET		(STATISTICS_OFFSET + STAT_SIZE + 8)
/* 1 byte */
#define COLLISION_COUNTER		(STATISTICS_OFFSET + STAT_SIZE + 9)
/* 4 bytes */
#define RX_PKT_SIZE_OFFSET		(STATISTICS_OFFSET + STAT_SIZE + 10)
/* 4 bytes */
#define PORT_CONTROL_ADDR		(STATISTICS_OFFSET + STAT_SIZE + 14)
/* 6 bytes */
#define PORT_MAC_ADDR			(STATISTICS_OFFSET + STAT_SIZE + 18)
/* 1 byte */
#define RX_INT_STATUS_OFFSET		(STATISTICS_OFFSET + STAT_SIZE + 24)
/* 4 bytes */
#define STORM_PREVENTION_OFFSET_MC	(STATISTICS_OFFSET + STAT_SIZE + 25)
/* 4 bytes */
#define STORM_PREVENTION_OFFSET_UC	(STATISTICS_OFFSET + STAT_SIZE + 29)
/* 4 bytes ? */
#define STP_INVALID_STATE_OFFSET	(STATISTICS_OFFSET + STAT_SIZE + 33)

/* DRAM Offsets for EMAC
 * Present on Both DRAM0 and DRAM1
 */

/* 4 queue descriptors for port tx = 32 bytes */
#define TX_CONTEXT_Q1_OFFSET_ADDR	(PORT_QUEUE_DESC_OFFSET + 32)
#define PORT_QUEUE_DESC_OFFSET	(ICSS_EMAC_TTS_CYC_TX_SOF + 8)

/* EMAC Time Triggered Send Offsets */
#define ICSS_EMAC_TTS_CYC_TX_SOF	(ICSS_EMAC_TTS_PREV_TX_SOF + 8)
#define ICSS_EMAC_TTS_PREV_TX_SOF	\
	(ICSS_EMAC_TTS_MISSED_CYCLE_CNT_OFFSET	+ 4)
#define ICSS_EMAC_TTS_MISSED_CYCLE_CNT_OFFSET	(ICSS_EMAC_TTS_STATUS_OFFSET \
						 + 4)
#define ICSS_EMAC_TTS_STATUS_OFFSET	(ICSS_EMAC_TTS_CFG_TIME_OFFSET + 4)
#define ICSS_EMAC_TTS_CFG_TIME_OFFSET	(ICSS_EMAC_TTS_CYCLE_PERIOD_OFFSET + 4)
#define ICSS_EMAC_TTS_CYCLE_PERIOD_OFFSET	\
	(ICSS_EMAC_TTS_CYCLE_START_OFFSET + 8)
#define ICSS_EMAC_TTS_CYCLE_START_OFFSET	ICSS_EMAC_TTS_BASE_OFFSET
#define ICSS_EMAC_TTS_BASE_OFFSET	DRAM_START_OFFSET

/* Shared RAM offsets for EMAC */

/* Queue Descriptors */

/* 4 queue descriptors for port 0 (host receive). 32 bytes */
#define HOST_QUEUE_DESC_OFFSET		(HOST_QUEUE_SIZE_ADDR + 16)

/* table offset for queue size:
 * 3 ports * 4 Queues * 1 byte offset = 12 bytes
 */
#define HOST_QUEUE_SIZE_ADDR		(HOST_QUEUE_OFFSET_ADDR + 8)
/* table offset for queue:
 * 4 Queues * 2 byte offset = 8 bytes
 */
#define HOST_QUEUE_OFFSET_ADDR		(HOST_QUEUE_DESCRIPTOR_OFFSET_ADDR + 8)
/* table offset for Host queue descriptors:
 * 1 ports * 4 Queues * 2 byte offset = 8 bytes
 */
#define HOST_QUEUE_DESCRIPTOR_OFFSET_ADDR	(HOST_Q4_RX_CONTEXT_OFFSET + 8)

/* Host Port Rx Context */
#define HOST_Q4_RX_CONTEXT_OFFSET	(HOST_Q3_RX_CONTEXT_OFFSET + 8)
#define HOST_Q3_RX_CONTEXT_OFFSET	(HOST_Q2_RX_CONTEXT_OFFSET + 8)
#define HOST_Q2_RX_CONTEXT_OFFSET	(HOST_Q1_RX_CONTEXT_OFFSET + 8)
#define HOST_Q1_RX_CONTEXT_OFFSET	(EMAC_PROMISCUOUS_MODE_OFFSET + 4)

/* Promiscuous mode control */
#define EMAC_P1_PROMISCUOUS_BIT		BIT(0)
#define EMAC_P2_PROMISCUOUS_BIT		BIT(1)
#define EMAC_PROMISCUOUS_MODE_OFFSET	(EMAC_RESERVED + 4)
#define EMAC_RESERVED			EOF_48K_BUFFER_BD

/* allow for max 48k buffer which spans the descriptors up to 0x1800 6kB */
#define EOF_48K_BUFFER_BD	(P0_BUFFER_DESC_OFFSET + HOST_BD_SIZE + \
				 PORT_BD_SIZE)

#define HOST_BD_SIZE		((HOST_QUEUE_1_SIZE +	\
				  HOST_QUEUE_2_SIZE + HOST_QUEUE_3_SIZE + \
				  HOST_QUEUE_4_SIZE) * BD_SIZE)
#define PORT_BD_SIZE		((QUEUE_1_SIZE + QUEUE_2_SIZE +	\
				  QUEUE_3_SIZE + QUEUE_4_SIZE) * 2 * BD_SIZE)

#define END_OF_BD_POOL		(P2_Q4_BD_OFFSET + QUEUE_4_SIZE * BD_SIZE)
#define P2_Q4_BD_OFFSET		(P2_Q3_BD_OFFSET + QUEUE_3_SIZE * BD_SIZE)
#define P2_Q3_BD_OFFSET		(P2_Q2_BD_OFFSET + QUEUE_2_SIZE * BD_SIZE)
#define P2_Q2_BD_OFFSET		(P2_Q1_BD_OFFSET + QUEUE_1_SIZE * BD_SIZE)
#define P2_Q1_BD_OFFSET		(P1_Q4_BD_OFFSET + QUEUE_4_SIZE * BD_SIZE)
#define P1_Q4_BD_OFFSET		(P1_Q3_BD_OFFSET + QUEUE_3_SIZE * BD_SIZE)
#define P1_Q3_BD_OFFSET		(P1_Q2_BD_OFFSET + QUEUE_2_SIZE * BD_SIZE)
#define P1_Q2_BD_OFFSET		(P1_Q1_BD_OFFSET + QUEUE_1_SIZE * BD_SIZE)
#define P1_Q1_BD_OFFSET		(P0_Q4_BD_OFFSET + HOST_QUEUE_4_SIZE * BD_SIZE)
#define P0_Q4_BD_OFFSET		(P0_Q3_BD_OFFSET + HOST_QUEUE_3_SIZE * BD_SIZE)
#define P0_Q3_BD_OFFSET		(P0_Q2_BD_OFFSET + HOST_QUEUE_2_SIZE * BD_SIZE)
#define P0_Q2_BD_OFFSET		(P0_Q1_BD_OFFSET + HOST_QUEUE_1_SIZE * BD_SIZE)
#define P0_Q1_BD_OFFSET		P0_BUFFER_DESC_OFFSET
#define P0_BUFFER_DESC_OFFSET	SRAM_START_OFFSET

/* Memory Usage of L3 OCMC RAM */

/* L3 64KB Memory - mainly buffer Pool */
#define END_OF_BUFFER_POOL	(P2_Q4_BUFFER_OFFSET + QUEUE_4_SIZE *	\
				 ICSS_BLOCK_SIZE)
#define P2_Q4_BUFFER_OFFSET	(P2_Q3_BUFFER_OFFSET + QUEUE_3_SIZE *	\
				 ICSS_BLOCK_SIZE)
#define P2_Q3_BUFFER_OFFSET	(P2_Q2_BUFFER_OFFSET + QUEUE_2_SIZE *	\
				 ICSS_BLOCK_SIZE)
#define P2_Q2_BUFFER_OFFSET	(P2_Q1_BUFFER_OFFSET + QUEUE_1_SIZE *	\
				 ICSS_BLOCK_SIZE)
#define P2_Q1_BUFFER_OFFSET	(P1_Q4_BUFFER_OFFSET + QUEUE_4_SIZE *	\
				 ICSS_BLOCK_SIZE)
#define P1_Q4_BUFFER_OFFSET	(P1_Q3_BUFFER_OFFSET + QUEUE_3_SIZE *	\
				 ICSS_BLOCK_SIZE)
#define P1_Q3_BUFFER_OFFSET	(P1_Q2_BUFFER_OFFSET + QUEUE_2_SIZE *	\
				 ICSS_BLOCK_SIZE)
#define P1_Q2_BUFFER_OFFSET	(P1_Q1_BUFFER_OFFSET + QUEUE_1_SIZE *	\
				 ICSS_BLOCK_SIZE)
#define P1_Q1_BUFFER_OFFSET	(P0_Q4_BUFFER_OFFSET + HOST_QUEUE_4_SIZE * \
				 ICSS_BLOCK_SIZE)
#define P0_Q4_BUFFER_OFFSET	(P0_Q3_BUFFER_OFFSET + HOST_QUEUE_3_SIZE * \
				 ICSS_BLOCK_SIZE)
#define P0_Q3_BUFFER_OFFSET	(P0_Q2_BUFFER_OFFSET + HOST_QUEUE_2_SIZE * \
				 ICSS_BLOCK_SIZE)
#define P0_Q2_BUFFER_OFFSET	(P0_Q1_BUFFER_OFFSET + HOST_QUEUE_1_SIZE * \
				 ICSS_BLOCK_SIZE)
#define P0_COL_BUFFER_OFFSET	0xEE00
#define P0_Q1_BUFFER_OFFSET	0x0000

#endif /* __ICSS_SWITCH_H */
