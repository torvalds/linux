/*
 * Driver for Marvell Discovery (MV643XX) and Marvell Orion ethernet ports
 * Copyright (C) 2002 Matthew Dharm <mdharm@momenco.com>
 *
 * Based on the 64360 driver from:
 * Copyright (C) 2002 rabeeh@galileo.co.il
 *
 * Copyright (C) 2003 PMC-Sierra, Inc.,
 *	written by Manish Lachwani
 *
 * Copyright (C) 2003 Ralf Baechle <ralf@linux-mips.org>
 *
 * Copyright (C) 2004-2006 MontaVista Software, Inc.
 *			   Dale Farnsworth <dale@farnsworth.org>
 *
 * Copyright (C) 2004 Steven J. Hill <sjhill1@rockwellcollins.com>
 *				     <sjhill@realitydiluted.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/etherdevice.h>

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/mii.h>

#include <linux/mv643xx_eth.h>

#include <asm/io.h>
#include <asm/types.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/delay.h>
#include <asm/dma-mapping.h>

#define MV643XX_CHECKSUM_OFFLOAD_TX
#define MV643XX_NAPI
#define MV643XX_TX_FAST_REFILL
#undef	MV643XX_COAL

/*
 * Number of RX / TX descriptors on RX / TX rings.
 * Note that allocating RX descriptors is done by allocating the RX
 * ring AND a preallocated RX buffers (skb's) for each descriptor.
 * The TX descriptors only allocates the TX descriptors ring,
 * with no pre allocated TX buffers (skb's are allocated by higher layers.
 */

/* Default TX ring size is 1000 descriptors */
#define MV643XX_DEFAULT_TX_QUEUE_SIZE 1000

/* Default RX ring size is 400 descriptors */
#define MV643XX_DEFAULT_RX_QUEUE_SIZE 400

#define MV643XX_TX_COAL 100
#ifdef MV643XX_COAL
#define MV643XX_RX_COAL 100
#endif

#ifdef MV643XX_CHECKSUM_OFFLOAD_TX
#define MAX_DESCS_PER_SKB	(MAX_SKB_FRAGS + 1)
#else
#define MAX_DESCS_PER_SKB	1
#endif

#define ETH_VLAN_HLEN		4
#define ETH_FCS_LEN		4
#define ETH_HW_IP_ALIGN		2		/* hw aligns IP header */
#define ETH_WRAPPER_LEN		(ETH_HW_IP_ALIGN + ETH_HLEN + \
					ETH_VLAN_HLEN + ETH_FCS_LEN)
#define ETH_RX_SKB_SIZE		(dev->mtu + ETH_WRAPPER_LEN + \
					dma_get_cache_alignment())

/*
 * Registers shared between all ports.
 */
#define PHY_ADDR_REG				0x0000
#define SMI_REG					0x0004

/*
 * Per-port registers.
 */
#define PORT_CONFIG_REG(p)				(0x0400 + ((p) << 10))
#define PORT_CONFIG_EXTEND_REG(p)			(0x0404 + ((p) << 10))
#define MAC_ADDR_LOW(p)					(0x0414 + ((p) << 10))
#define MAC_ADDR_HIGH(p)				(0x0418 + ((p) << 10))
#define SDMA_CONFIG_REG(p)				(0x041c + ((p) << 10))
#define PORT_SERIAL_CONTROL_REG(p)			(0x043c + ((p) << 10))
#define PORT_STATUS_REG(p)				(0x0444 + ((p) << 10))
#define TRANSMIT_QUEUE_COMMAND_REG(p)			(0x0448 + ((p) << 10))
#define MAXIMUM_TRANSMIT_UNIT(p)			(0x0458 + ((p) << 10))
#define INTERRUPT_CAUSE_REG(p)				(0x0460 + ((p) << 10))
#define INTERRUPT_CAUSE_EXTEND_REG(p)			(0x0464 + ((p) << 10))
#define INTERRUPT_MASK_REG(p)				(0x0468 + ((p) << 10))
#define INTERRUPT_EXTEND_MASK_REG(p)			(0x046c + ((p) << 10))
#define TX_FIFO_URGENT_THRESHOLD_REG(p)			(0x0474 + ((p) << 10))
#define RX_CURRENT_QUEUE_DESC_PTR_0(p)			(0x060c + ((p) << 10))
#define RECEIVE_QUEUE_COMMAND_REG(p)			(0x0680 + ((p) << 10))
#define TX_CURRENT_QUEUE_DESC_PTR_0(p)			(0x06c0 + ((p) << 10))
#define MIB_COUNTERS_BASE(p)				(0x1000 + ((p) << 7))
#define DA_FILTER_SPECIAL_MULTICAST_TABLE_BASE(p)	(0x1400 + ((p) << 10))
#define DA_FILTER_OTHER_MULTICAST_TABLE_BASE(p)		(0x1500 + ((p) << 10))
#define DA_FILTER_UNICAST_TABLE_BASE(p)			(0x1600 + ((p) << 10))

/* These macros describe Ethernet Port configuration reg (Px_cR) bits */
#define UNICAST_NORMAL_MODE		(0 << 0)
#define UNICAST_PROMISCUOUS_MODE	(1 << 0)
#define DEFAULT_RX_QUEUE(queue)		((queue) << 1)
#define DEFAULT_RX_ARP_QUEUE(queue)	((queue) << 4)
#define RECEIVE_BC_IF_NOT_IP_OR_ARP	(0 << 7)
#define REJECT_BC_IF_NOT_IP_OR_ARP	(1 << 7)
#define RECEIVE_BC_IF_IP		(0 << 8)
#define REJECT_BC_IF_IP			(1 << 8)
#define RECEIVE_BC_IF_ARP		(0 << 9)
#define REJECT_BC_IF_ARP		(1 << 9)
#define TX_AM_NO_UPDATE_ERROR_SUMMARY	(1 << 12)
#define CAPTURE_TCP_FRAMES_DIS		(0 << 14)
#define CAPTURE_TCP_FRAMES_EN		(1 << 14)
#define CAPTURE_UDP_FRAMES_DIS		(0 << 15)
#define CAPTURE_UDP_FRAMES_EN		(1 << 15)
#define DEFAULT_RX_TCP_QUEUE(queue)	((queue) << 16)
#define DEFAULT_RX_UDP_QUEUE(queue)	((queue) << 19)
#define DEFAULT_RX_BPDU_QUEUE(queue)	((queue) << 22)

#define PORT_CONFIG_DEFAULT_VALUE			\
		UNICAST_NORMAL_MODE		|	\
		DEFAULT_RX_QUEUE(0)		|	\
		DEFAULT_RX_ARP_QUEUE(0)		|	\
		RECEIVE_BC_IF_NOT_IP_OR_ARP	|	\
		RECEIVE_BC_IF_IP		|	\
		RECEIVE_BC_IF_ARP		|	\
		CAPTURE_TCP_FRAMES_DIS		|	\
		CAPTURE_UDP_FRAMES_DIS		|	\
		DEFAULT_RX_TCP_QUEUE(0)		|	\
		DEFAULT_RX_UDP_QUEUE(0)		|	\
		DEFAULT_RX_BPDU_QUEUE(0)

/* These macros describe Ethernet Port configuration extend reg (Px_cXR) bits*/
#define CLASSIFY_EN				(1 << 0)
#define SPAN_BPDU_PACKETS_AS_NORMAL		(0 << 1)
#define SPAN_BPDU_PACKETS_TO_RX_QUEUE_7		(1 << 1)
#define PARTITION_DISABLE			(0 << 2)
#define PARTITION_ENABLE			(1 << 2)

#define PORT_CONFIG_EXTEND_DEFAULT_VALUE		\
		SPAN_BPDU_PACKETS_AS_NORMAL	|	\
		PARTITION_DISABLE

/* These macros describe Ethernet Port Sdma configuration reg (SDCR) bits */
#define RIFB				(1 << 0)
#define RX_BURST_SIZE_1_64BIT		(0 << 1)
#define RX_BURST_SIZE_2_64BIT		(1 << 1)
#define RX_BURST_SIZE_4_64BIT		(2 << 1)
#define RX_BURST_SIZE_8_64BIT		(3 << 1)
#define RX_BURST_SIZE_16_64BIT		(4 << 1)
#define BLM_RX_NO_SWAP			(1 << 4)
#define BLM_RX_BYTE_SWAP		(0 << 4)
#define BLM_TX_NO_SWAP			(1 << 5)
#define BLM_TX_BYTE_SWAP		(0 << 5)
#define DESCRIPTORS_BYTE_SWAP		(1 << 6)
#define DESCRIPTORS_NO_SWAP		(0 << 6)
#define IPG_INT_RX(value)		(((value) & 0x3fff) << 8)
#define TX_BURST_SIZE_1_64BIT		(0 << 22)
#define TX_BURST_SIZE_2_64BIT		(1 << 22)
#define TX_BURST_SIZE_4_64BIT		(2 << 22)
#define TX_BURST_SIZE_8_64BIT		(3 << 22)
#define TX_BURST_SIZE_16_64BIT		(4 << 22)

#if defined(__BIG_ENDIAN)
#define PORT_SDMA_CONFIG_DEFAULT_VALUE		\
		RX_BURST_SIZE_4_64BIT	|	\
		IPG_INT_RX(0)		|	\
		TX_BURST_SIZE_4_64BIT
#elif defined(__LITTLE_ENDIAN)
#define PORT_SDMA_CONFIG_DEFAULT_VALUE		\
		RX_BURST_SIZE_4_64BIT	|	\
		BLM_RX_NO_SWAP		|	\
		BLM_TX_NO_SWAP		|	\
		IPG_INT_RX(0)		|	\
		TX_BURST_SIZE_4_64BIT
#else
#error One of __BIG_ENDIAN or __LITTLE_ENDIAN must be defined
#endif

/* These macros describe Ethernet Port serial control reg (PSCR) bits */
#define SERIAL_PORT_DISABLE			(0 << 0)
#define SERIAL_PORT_ENABLE			(1 << 0)
#define DO_NOT_FORCE_LINK_PASS			(0 << 1)
#define FORCE_LINK_PASS				(1 << 1)
#define ENABLE_AUTO_NEG_FOR_DUPLX		(0 << 2)
#define DISABLE_AUTO_NEG_FOR_DUPLX		(1 << 2)
#define ENABLE_AUTO_NEG_FOR_FLOW_CTRL		(0 << 3)
#define DISABLE_AUTO_NEG_FOR_FLOW_CTRL		(1 << 3)
#define ADV_NO_FLOW_CTRL			(0 << 4)
#define ADV_SYMMETRIC_FLOW_CTRL			(1 << 4)
#define FORCE_FC_MODE_NO_PAUSE_DIS_TX		(0 << 5)
#define FORCE_FC_MODE_TX_PAUSE_DIS		(1 << 5)
#define FORCE_BP_MODE_NO_JAM			(0 << 7)
#define FORCE_BP_MODE_JAM_TX			(1 << 7)
#define FORCE_BP_MODE_JAM_TX_ON_RX_ERR		(2 << 7)
#define SERIAL_PORT_CONTROL_RESERVED		(1 << 9)
#define FORCE_LINK_FAIL				(0 << 10)
#define DO_NOT_FORCE_LINK_FAIL			(1 << 10)
#define RETRANSMIT_16_ATTEMPTS			(0 << 11)
#define RETRANSMIT_FOREVER			(1 << 11)
#define ENABLE_AUTO_NEG_SPEED_GMII		(0 << 13)
#define DISABLE_AUTO_NEG_SPEED_GMII		(1 << 13)
#define DTE_ADV_0				(0 << 14)
#define DTE_ADV_1				(1 << 14)
#define DISABLE_AUTO_NEG_BYPASS			(0 << 15)
#define ENABLE_AUTO_NEG_BYPASS			(1 << 15)
#define AUTO_NEG_NO_CHANGE			(0 << 16)
#define RESTART_AUTO_NEG			(1 << 16)
#define MAX_RX_PACKET_1518BYTE			(0 << 17)
#define MAX_RX_PACKET_1522BYTE			(1 << 17)
#define MAX_RX_PACKET_1552BYTE			(2 << 17)
#define MAX_RX_PACKET_9022BYTE			(3 << 17)
#define MAX_RX_PACKET_9192BYTE			(4 << 17)
#define MAX_RX_PACKET_9700BYTE			(5 << 17)
#define MAX_RX_PACKET_MASK			(7 << 17)
#define CLR_EXT_LOOPBACK			(0 << 20)
#define SET_EXT_LOOPBACK			(1 << 20)
#define SET_HALF_DUPLEX_MODE			(0 << 21)
#define SET_FULL_DUPLEX_MODE			(1 << 21)
#define DISABLE_FLOW_CTRL_TX_RX_IN_FULL_DUPLEX	(0 << 22)
#define ENABLE_FLOW_CTRL_TX_RX_IN_FULL_DUPLEX	(1 << 22)
#define SET_GMII_SPEED_TO_10_100		(0 << 23)
#define SET_GMII_SPEED_TO_1000			(1 << 23)
#define SET_MII_SPEED_TO_10			(0 << 24)
#define SET_MII_SPEED_TO_100			(1 << 24)

#define PORT_SERIAL_CONTROL_DEFAULT_VALUE		\
		DO_NOT_FORCE_LINK_PASS		|	\
		ENABLE_AUTO_NEG_FOR_DUPLX	|	\
		DISABLE_AUTO_NEG_FOR_FLOW_CTRL	|	\
		ADV_SYMMETRIC_FLOW_CTRL		|	\
		FORCE_FC_MODE_NO_PAUSE_DIS_TX	|	\
		FORCE_BP_MODE_NO_JAM		|	\
		(1 << 9) /* reserved */		|	\
		DO_NOT_FORCE_LINK_FAIL		|	\
		RETRANSMIT_16_ATTEMPTS		|	\
		ENABLE_AUTO_NEG_SPEED_GMII	|	\
		DTE_ADV_0			|	\
		DISABLE_AUTO_NEG_BYPASS		|	\
		AUTO_NEG_NO_CHANGE		|	\
		MAX_RX_PACKET_9700BYTE		|	\
		CLR_EXT_LOOPBACK		|	\
		SET_FULL_DUPLEX_MODE		|	\
		ENABLE_FLOW_CTRL_TX_RX_IN_FULL_DUPLEX

/* These macros describe Ethernet Serial Status reg (PSR) bits */
#define PORT_STATUS_MODE_10_BIT		(1 << 0)
#define PORT_STATUS_LINK_UP		(1 << 1)
#define PORT_STATUS_FULL_DUPLEX		(1 << 2)
#define PORT_STATUS_FLOW_CONTROL	(1 << 3)
#define PORT_STATUS_GMII_1000		(1 << 4)
#define PORT_STATUS_MII_100		(1 << 5)
/* PSR bit 6 is undocumented */
#define PORT_STATUS_TX_IN_PROGRESS	(1 << 7)
#define PORT_STATUS_AUTONEG_BYPASSED	(1 << 8)
#define PORT_STATUS_PARTITION		(1 << 9)
#define PORT_STATUS_TX_FIFO_EMPTY	(1 << 10)
/* PSR bits 11-31 are reserved */

#define PORT_DEFAULT_TRANSMIT_QUEUE_SIZE	800
#define PORT_DEFAULT_RECEIVE_QUEUE_SIZE		400

#define DESC_SIZE				64

#define ETH_RX_QUEUES_ENABLED	(1 << 0)	/* use only Q0 for receive */
#define ETH_TX_QUEUES_ENABLED	(1 << 0)	/* use only Q0 for transmit */

#define ETH_INT_CAUSE_RX_DONE	(ETH_RX_QUEUES_ENABLED << 2)
#define ETH_INT_CAUSE_RX_ERROR	(ETH_RX_QUEUES_ENABLED << 9)
#define ETH_INT_CAUSE_RX	(ETH_INT_CAUSE_RX_DONE | ETH_INT_CAUSE_RX_ERROR)
#define ETH_INT_CAUSE_EXT	0x00000002
#define ETH_INT_UNMASK_ALL	(ETH_INT_CAUSE_RX | ETH_INT_CAUSE_EXT)

#define ETH_INT_CAUSE_TX_DONE	(ETH_TX_QUEUES_ENABLED << 0)
#define ETH_INT_CAUSE_TX_ERROR	(ETH_TX_QUEUES_ENABLED << 8)
#define ETH_INT_CAUSE_TX	(ETH_INT_CAUSE_TX_DONE | ETH_INT_CAUSE_TX_ERROR)
#define ETH_INT_CAUSE_PHY	0x00010000
#define ETH_INT_CAUSE_STATE	0x00100000
#define ETH_INT_UNMASK_ALL_EXT	(ETH_INT_CAUSE_TX | ETH_INT_CAUSE_PHY | \
					ETH_INT_CAUSE_STATE)

#define ETH_INT_MASK_ALL	0x00000000
#define ETH_INT_MASK_ALL_EXT	0x00000000

#define PHY_WAIT_ITERATIONS	1000	/* 1000 iterations * 10uS = 10mS max */
#define PHY_WAIT_MICRO_SECONDS	10

/* Buffer offset from buffer pointer */
#define RX_BUF_OFFSET				0x2

/* Gigabit Ethernet Unit Global Registers */

/* MIB Counters register definitions */
#define ETH_MIB_GOOD_OCTETS_RECEIVED_LOW	0x0
#define ETH_MIB_GOOD_OCTETS_RECEIVED_HIGH	0x4
#define ETH_MIB_BAD_OCTETS_RECEIVED		0x8
#define ETH_MIB_INTERNAL_MAC_TRANSMIT_ERR	0xc
#define ETH_MIB_GOOD_FRAMES_RECEIVED		0x10
#define ETH_MIB_BAD_FRAMES_RECEIVED		0x14
#define ETH_MIB_BROADCAST_FRAMES_RECEIVED	0x18
#define ETH_MIB_MULTICAST_FRAMES_RECEIVED	0x1c
#define ETH_MIB_FRAMES_64_OCTETS		0x20
#define ETH_MIB_FRAMES_65_TO_127_OCTETS		0x24
#define ETH_MIB_FRAMES_128_TO_255_OCTETS	0x28
#define ETH_MIB_FRAMES_256_TO_511_OCTETS	0x2c
#define ETH_MIB_FRAMES_512_TO_1023_OCTETS	0x30
#define ETH_MIB_FRAMES_1024_TO_MAX_OCTETS	0x34
#define ETH_MIB_GOOD_OCTETS_SENT_LOW		0x38
#define ETH_MIB_GOOD_OCTETS_SENT_HIGH		0x3c
#define ETH_MIB_GOOD_FRAMES_SENT		0x40
#define ETH_MIB_EXCESSIVE_COLLISION		0x44
#define ETH_MIB_MULTICAST_FRAMES_SENT		0x48
#define ETH_MIB_BROADCAST_FRAMES_SENT		0x4c
#define ETH_MIB_UNREC_MAC_CONTROL_RECEIVED	0x50
#define ETH_MIB_FC_SENT				0x54
#define ETH_MIB_GOOD_FC_RECEIVED		0x58
#define ETH_MIB_BAD_FC_RECEIVED			0x5c
#define ETH_MIB_UNDERSIZE_RECEIVED		0x60
#define ETH_MIB_FRAGMENTS_RECEIVED		0x64
#define ETH_MIB_OVERSIZE_RECEIVED		0x68
#define ETH_MIB_JABBER_RECEIVED			0x6c
#define ETH_MIB_MAC_RECEIVE_ERROR		0x70
#define ETH_MIB_BAD_CRC_EVENT			0x74
#define ETH_MIB_COLLISION			0x78
#define ETH_MIB_LATE_COLLISION			0x7c

/* Port serial status reg (PSR) */
#define ETH_INTERFACE_PCM			0x00000001
#define ETH_LINK_IS_UP				0x00000002
#define ETH_PORT_AT_FULL_DUPLEX			0x00000004
#define ETH_RX_FLOW_CTRL_ENABLED		0x00000008
#define ETH_GMII_SPEED_1000			0x00000010
#define ETH_MII_SPEED_100			0x00000020
#define ETH_TX_IN_PROGRESS			0x00000080
#define ETH_BYPASS_ACTIVE			0x00000100
#define ETH_PORT_AT_PARTITION_STATE		0x00000200
#define ETH_PORT_TX_FIFO_EMPTY			0x00000400

/* SMI reg */
#define ETH_SMI_BUSY		0x10000000	/* 0 - Write, 1 - Read	*/
#define ETH_SMI_READ_VALID	0x08000000	/* 0 - Write, 1 - Read	*/
#define ETH_SMI_OPCODE_WRITE	0		/* Completion of Read	*/
#define ETH_SMI_OPCODE_READ	0x04000000	/* Operation is in progress */

/* Interrupt Cause Register Bit Definitions */

/* SDMA command status fields macros */

/* Tx & Rx descriptors status */
#define ETH_ERROR_SUMMARY			0x00000001

/* Tx & Rx descriptors command */
#define ETH_BUFFER_OWNED_BY_DMA			0x80000000

/* Tx descriptors status */
#define ETH_LC_ERROR				0
#define ETH_UR_ERROR				0x00000002
#define ETH_RL_ERROR				0x00000004
#define ETH_LLC_SNAP_FORMAT			0x00000200

/* Rx descriptors status */
#define ETH_OVERRUN_ERROR			0x00000002
#define ETH_MAX_FRAME_LENGTH_ERROR		0x00000004
#define ETH_RESOURCE_ERROR			0x00000006
#define ETH_VLAN_TAGGED				0x00080000
#define ETH_BPDU_FRAME				0x00100000
#define ETH_UDP_FRAME_OVER_IP_V_4		0x00200000
#define ETH_OTHER_FRAME_TYPE			0x00400000
#define ETH_LAYER_2_IS_ETH_V_2			0x00800000
#define ETH_FRAME_TYPE_IP_V_4			0x01000000
#define ETH_FRAME_HEADER_OK			0x02000000
#define ETH_RX_LAST_DESC			0x04000000
#define ETH_RX_FIRST_DESC			0x08000000
#define ETH_UNKNOWN_DESTINATION_ADDR		0x10000000
#define ETH_RX_ENABLE_INTERRUPT			0x20000000
#define ETH_LAYER_4_CHECKSUM_OK			0x40000000

/* Rx descriptors byte count */
#define ETH_FRAME_FRAGMENTED			0x00000004

/* Tx descriptors command */
#define ETH_LAYER_4_CHECKSUM_FIRST_DESC		0x00000400
#define ETH_FRAME_SET_TO_VLAN			0x00008000
#define ETH_UDP_FRAME				0x00010000
#define ETH_GEN_TCP_UDP_CHECKSUM		0x00020000
#define ETH_GEN_IP_V_4_CHECKSUM			0x00040000
#define ETH_ZERO_PADDING			0x00080000
#define ETH_TX_LAST_DESC			0x00100000
#define ETH_TX_FIRST_DESC			0x00200000
#define ETH_GEN_CRC				0x00400000
#define ETH_TX_ENABLE_INTERRUPT			0x00800000
#define ETH_AUTO_MODE				0x40000000

#define ETH_TX_IHL_SHIFT			11

/* typedefs */

typedef enum _eth_func_ret_status {
	ETH_OK,			/* Returned as expected.		*/
	ETH_ERROR,		/* Fundamental error.			*/
	ETH_RETRY,		/* Could not process request. Try later.*/
	ETH_END_OF_JOB,		/* Ring has nothing to process.		*/
	ETH_QUEUE_FULL,		/* Ring resource error.			*/
	ETH_QUEUE_LAST_RESOURCE	/* Ring resources about to exhaust.	*/
} ETH_FUNC_RET_STATUS;

typedef enum _eth_target {
	ETH_TARGET_DRAM,
	ETH_TARGET_DEVICE,
	ETH_TARGET_CBS,
	ETH_TARGET_PCI0,
	ETH_TARGET_PCI1
} ETH_TARGET;

/* These are for big-endian machines.  Little endian needs different
 * definitions.
 */
#if defined(__BIG_ENDIAN)
struct eth_rx_desc {
	u16 byte_cnt;		/* Descriptor buffer byte count		*/
	u16 buf_size;		/* Buffer size				*/
	u32 cmd_sts;		/* Descriptor command status		*/
	u32 next_desc_ptr;	/* Next descriptor pointer		*/
	u32 buf_ptr;		/* Descriptor buffer pointer		*/
};

struct eth_tx_desc {
	u16 byte_cnt;		/* buffer byte count			*/
	u16 l4i_chk;		/* CPU provided TCP checksum		*/
	u32 cmd_sts;		/* Command/status field			*/
	u32 next_desc_ptr;	/* Pointer to next descriptor		*/
	u32 buf_ptr;		/* pointer to buffer for this descriptor*/
};
#elif defined(__LITTLE_ENDIAN)
struct eth_rx_desc {
	u32 cmd_sts;		/* Descriptor command status		*/
	u16 buf_size;		/* Buffer size				*/
	u16 byte_cnt;		/* Descriptor buffer byte count		*/
	u32 buf_ptr;		/* Descriptor buffer pointer		*/
	u32 next_desc_ptr;	/* Next descriptor pointer		*/
};

struct eth_tx_desc {
	u32 cmd_sts;		/* Command/status field			*/
	u16 l4i_chk;		/* CPU provided TCP checksum		*/
	u16 byte_cnt;		/* buffer byte count			*/
	u32 buf_ptr;		/* pointer to buffer for this descriptor*/
	u32 next_desc_ptr;	/* Pointer to next descriptor		*/
};
#else
#error One of __BIG_ENDIAN or __LITTLE_ENDIAN must be defined
#endif

/* Unified struct for Rx and Tx operations. The user is not required to	*/
/* be familier with neither Tx nor Rx descriptors.			*/
struct pkt_info {
	unsigned short byte_cnt;	/* Descriptor buffer byte count	*/
	unsigned short l4i_chk;		/* Tx CPU provided TCP Checksum	*/
	unsigned int cmd_sts;		/* Descriptor command status	*/
	dma_addr_t buf_ptr;		/* Descriptor buffer pointer	*/
	struct sk_buff *return_info;	/* User resource return information */
};

/* Ethernet port specific information */
struct mv643xx_mib_counters {
	u64 good_octets_received;
	u32 bad_octets_received;
	u32 internal_mac_transmit_err;
	u32 good_frames_received;
	u32 bad_frames_received;
	u32 broadcast_frames_received;
	u32 multicast_frames_received;
	u32 frames_64_octets;
	u32 frames_65_to_127_octets;
	u32 frames_128_to_255_octets;
	u32 frames_256_to_511_octets;
	u32 frames_512_to_1023_octets;
	u32 frames_1024_to_max_octets;
	u64 good_octets_sent;
	u32 good_frames_sent;
	u32 excessive_collision;
	u32 multicast_frames_sent;
	u32 broadcast_frames_sent;
	u32 unrec_mac_control_received;
	u32 fc_sent;
	u32 good_fc_received;
	u32 bad_fc_received;
	u32 undersize_received;
	u32 fragments_received;
	u32 oversize_received;
	u32 jabber_received;
	u32 mac_receive_error;
	u32 bad_crc_event;
	u32 collision;
	u32 late_collision;
};

struct mv643xx_private {
	int port_num;			/* User Ethernet port number	*/

	u32 rx_sram_addr;		/* Base address of rx sram area */
	u32 rx_sram_size;		/* Size of rx sram area		*/
	u32 tx_sram_addr;		/* Base address of tx sram area */
	u32 tx_sram_size;		/* Size of tx sram area		*/

	int rx_resource_err;		/* Rx ring resource error flag */

	/* Tx/Rx rings managment indexes fields. For driver use */

	/* Next available and first returning Rx resource */
	int rx_curr_desc_q, rx_used_desc_q;

	/* Next available and first returning Tx resource */
	int tx_curr_desc_q, tx_used_desc_q;

#ifdef MV643XX_TX_FAST_REFILL
	u32 tx_clean_threshold;
#endif

	struct eth_rx_desc *p_rx_desc_area;
	dma_addr_t rx_desc_dma;
	int rx_desc_area_size;
	struct sk_buff **rx_skb;

	struct eth_tx_desc *p_tx_desc_area;
	dma_addr_t tx_desc_dma;
	int tx_desc_area_size;
	struct sk_buff **tx_skb;

	struct work_struct tx_timeout_task;

	struct net_device *dev;
	struct napi_struct napi;
	struct net_device_stats stats;
	struct mv643xx_mib_counters mib_counters;
	spinlock_t lock;
	/* Size of Tx Ring per queue */
	int tx_ring_size;
	/* Number of tx descriptors in use */
	int tx_desc_count;
	/* Size of Rx Ring per queue */
	int rx_ring_size;
	/* Number of rx descriptors in use */
	int rx_desc_count;

	/*
	 * Used in case RX Ring is empty, which can be caused when
	 * system does not have resources (skb's)
	 */
	struct timer_list timeout;

	u32 rx_int_coal;
	u32 tx_int_coal;
	struct mii_if_info mii;
};

/* Static function declarations */
static void eth_port_init(struct mv643xx_private *mp);
static void eth_port_reset(unsigned int eth_port_num);
static void eth_port_start(struct net_device *dev);

static void ethernet_phy_reset(unsigned int eth_port_num);

static void eth_port_write_smi_reg(unsigned int eth_port_num,
				   unsigned int phy_reg, unsigned int value);

static void eth_port_read_smi_reg(unsigned int eth_port_num,
				  unsigned int phy_reg, unsigned int *value);

static void eth_clear_mib_counters(unsigned int eth_port_num);

static ETH_FUNC_RET_STATUS eth_port_receive(struct mv643xx_private *mp,
					    struct pkt_info *p_pkt_info);
static ETH_FUNC_RET_STATUS eth_rx_return_buff(struct mv643xx_private *mp,
					      struct pkt_info *p_pkt_info);

static void eth_port_uc_addr_get(unsigned int port_num, unsigned char *p_addr);
static void eth_port_uc_addr_set(unsigned int port_num, unsigned char *p_addr);
static void eth_port_set_multicast_list(struct net_device *);
static void mv643xx_eth_port_enable_tx(unsigned int port_num,
						unsigned int queues);
static void mv643xx_eth_port_enable_rx(unsigned int port_num,
						unsigned int queues);
static unsigned int mv643xx_eth_port_disable_tx(unsigned int port_num);
static unsigned int mv643xx_eth_port_disable_rx(unsigned int port_num);
static int mv643xx_eth_open(struct net_device *);
static int mv643xx_eth_stop(struct net_device *);
static int mv643xx_eth_change_mtu(struct net_device *, int);
static void eth_port_init_mac_tables(unsigned int eth_port_num);
#ifdef MV643XX_NAPI
static int mv643xx_poll(struct napi_struct *napi, int budget);
#endif
static int ethernet_phy_get(unsigned int eth_port_num);
static void ethernet_phy_set(unsigned int eth_port_num, int phy_addr);
static int ethernet_phy_detect(unsigned int eth_port_num);
static int mv643xx_mdio_read(struct net_device *dev, int phy_id, int location);
static void mv643xx_mdio_write(struct net_device *dev, int phy_id, int location, int val);
static int mv643xx_eth_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
static const struct ethtool_ops mv643xx_ethtool_ops;

static char mv643xx_driver_name[] = "mv643xx_eth";
static char mv643xx_driver_version[] = "1.0";

static void __iomem *mv643xx_eth_base;

/* used to protect SMI_REG, which is shared across ports */
static DEFINE_SPINLOCK(mv643xx_eth_phy_lock);

static inline u32 mv_read(int offset)
{
	return readl(mv643xx_eth_base + offset);
}

static inline void mv_write(int offset, u32 data)
{
	writel(data, mv643xx_eth_base + offset);
}

/*
 * Changes MTU (maximum transfer unit) of the gigabit ethenret port
 *
 * Input :	pointer to ethernet interface network device structure
 *		new mtu size
 * Output :	0 upon success, -EINVAL upon failure
 */
static int mv643xx_eth_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu > 9500) || (new_mtu < 64))
		return -EINVAL;

	dev->mtu = new_mtu;
	/*
	 * Stop then re-open the interface. This will allocate RX skb's with
	 * the new MTU.
	 * There is a possible danger that the open will not successed, due
	 * to memory is full, which might fail the open function.
	 */
	if (netif_running(dev)) {
		mv643xx_eth_stop(dev);
		if (mv643xx_eth_open(dev))
			printk(KERN_ERR
				"%s: Fatal error on opening device\n",
				dev->name);
	}

	return 0;
}

/*
 * mv643xx_eth_rx_refill_descs
 *
 * Fills / refills RX queue on a certain gigabit ethernet port
 *
 * Input :	pointer to ethernet interface network device structure
 * Output :	N/A
 */
static void mv643xx_eth_rx_refill_descs(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	struct pkt_info pkt_info;
	struct sk_buff *skb;
	int unaligned;

	while (mp->rx_desc_count < mp->rx_ring_size) {
		skb = dev_alloc_skb(ETH_RX_SKB_SIZE + dma_get_cache_alignment());
		if (!skb)
			break;
		mp->rx_desc_count++;
		unaligned = (u32)skb->data & (dma_get_cache_alignment() - 1);
		if (unaligned)
			skb_reserve(skb, dma_get_cache_alignment() - unaligned);
		pkt_info.cmd_sts = ETH_RX_ENABLE_INTERRUPT;
		pkt_info.byte_cnt = ETH_RX_SKB_SIZE;
		pkt_info.buf_ptr = dma_map_single(NULL, skb->data,
					ETH_RX_SKB_SIZE, DMA_FROM_DEVICE);
		pkt_info.return_info = skb;
		if (eth_rx_return_buff(mp, &pkt_info) != ETH_OK) {
			printk(KERN_ERR
				"%s: Error allocating RX Ring\n", dev->name);
			break;
		}
		skb_reserve(skb, ETH_HW_IP_ALIGN);
	}
	/*
	 * If RX ring is empty of SKB, set a timer to try allocating
	 * again at a later time.
	 */
	if (mp->rx_desc_count == 0) {
		printk(KERN_INFO "%s: Rx ring is empty\n", dev->name);
		mp->timeout.expires = jiffies + (HZ / 10);	/* 100 mSec */
		add_timer(&mp->timeout);
	}
}

/*
 * mv643xx_eth_rx_refill_descs_timer_wrapper
 *
 * Timer routine to wake up RX queue filling task. This function is
 * used only in case the RX queue is empty, and all alloc_skb has
 * failed (due to out of memory event).
 *
 * Input :	pointer to ethernet interface network device structure
 * Output :	N/A
 */
static inline void mv643xx_eth_rx_refill_descs_timer_wrapper(unsigned long data)
{
	mv643xx_eth_rx_refill_descs((struct net_device *)data);
}

/*
 * mv643xx_eth_update_mac_address
 *
 * Update the MAC address of the port in the address table
 *
 * Input :	pointer to ethernet interface network device structure
 * Output :	N/A
 */
static void mv643xx_eth_update_mac_address(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;

	eth_port_init_mac_tables(port_num);
	eth_port_uc_addr_set(port_num, dev->dev_addr);
}

/*
 * mv643xx_eth_set_rx_mode
 *
 * Change from promiscuos to regular rx mode
 *
 * Input :	pointer to ethernet interface network device structure
 * Output :	N/A
 */
static void mv643xx_eth_set_rx_mode(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	u32 config_reg;

	config_reg = mv_read(PORT_CONFIG_REG(mp->port_num));
	if (dev->flags & IFF_PROMISC)
		config_reg |= (u32) UNICAST_PROMISCUOUS_MODE;
	else
		config_reg &= ~(u32) UNICAST_PROMISCUOUS_MODE;
	mv_write(PORT_CONFIG_REG(mp->port_num), config_reg);

	eth_port_set_multicast_list(dev);
}

/*
 * mv643xx_eth_set_mac_address
 *
 * Change the interface's mac address.
 * No special hardware thing should be done because interface is always
 * put in promiscuous mode.
 *
 * Input :	pointer to ethernet interface network device structure and
 *		a pointer to the designated entry to be added to the cache.
 * Output :	zero upon success, negative upon failure
 */
static int mv643xx_eth_set_mac_address(struct net_device *dev, void *addr)
{
	int i;

	for (i = 0; i < 6; i++)
		/* +2 is for the offset of the HW addr type */
		dev->dev_addr[i] = ((unsigned char *)addr)[i + 2];
	mv643xx_eth_update_mac_address(dev);
	return 0;
}

/*
 * mv643xx_eth_tx_timeout
 *
 * Called upon a timeout on transmitting a packet
 *
 * Input :	pointer to ethernet interface network device structure.
 * Output :	N/A
 */
static void mv643xx_eth_tx_timeout(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);

	printk(KERN_INFO "%s: TX timeout  ", dev->name);

	/* Do the reset outside of interrupt context */
	schedule_work(&mp->tx_timeout_task);
}

/*
 * mv643xx_eth_tx_timeout_task
 *
 * Actual routine to reset the adapter when a timeout on Tx has occurred
 */
static void mv643xx_eth_tx_timeout_task(struct work_struct *ugly)
{
	struct mv643xx_private *mp = container_of(ugly, struct mv643xx_private,
						  tx_timeout_task);
	struct net_device *dev = mp->mii.dev; /* yuck */

	if (!netif_running(dev))
		return;

	netif_stop_queue(dev);

	eth_port_reset(mp->port_num);
	eth_port_start(dev);

	if (mp->tx_ring_size - mp->tx_desc_count >= MAX_DESCS_PER_SKB)
		netif_wake_queue(dev);
}

/**
 * mv643xx_eth_free_tx_descs - Free the tx desc data for completed descriptors
 *
 * If force is non-zero, frees uncompleted descriptors as well
 */
int mv643xx_eth_free_tx_descs(struct net_device *dev, int force)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	struct eth_tx_desc *desc;
	u32 cmd_sts;
	struct sk_buff *skb;
	unsigned long flags;
	int tx_index;
	dma_addr_t addr;
	int count;
	int released = 0;

	while (mp->tx_desc_count > 0) {
		spin_lock_irqsave(&mp->lock, flags);

		/* tx_desc_count might have changed before acquiring the lock */
		if (mp->tx_desc_count <= 0) {
			spin_unlock_irqrestore(&mp->lock, flags);
			return released;
		}

		tx_index = mp->tx_used_desc_q;
		desc = &mp->p_tx_desc_area[tx_index];
		cmd_sts = desc->cmd_sts;

		if (!force && (cmd_sts & ETH_BUFFER_OWNED_BY_DMA)) {
			spin_unlock_irqrestore(&mp->lock, flags);
			return released;
		}

		mp->tx_used_desc_q = (tx_index + 1) % mp->tx_ring_size;
		mp->tx_desc_count--;

		addr = desc->buf_ptr;
		count = desc->byte_cnt;
		skb = mp->tx_skb[tx_index];
		if (skb)
			mp->tx_skb[tx_index] = NULL;

		if (cmd_sts & ETH_ERROR_SUMMARY) {
			printk("%s: Error in TX\n", dev->name);
			dev->stats.tx_errors++;
		}

		spin_unlock_irqrestore(&mp->lock, flags);

		if (cmd_sts & ETH_TX_FIRST_DESC)
			dma_unmap_single(NULL, addr, count, DMA_TO_DEVICE);
		else
			dma_unmap_page(NULL, addr, count, DMA_TO_DEVICE);

		if (skb)
			dev_kfree_skb_irq(skb);

		released = 1;
	}

	return released;
}

static void mv643xx_eth_free_completed_tx_descs(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);

	if (mv643xx_eth_free_tx_descs(dev, 0) &&
	    mp->tx_ring_size - mp->tx_desc_count >= MAX_DESCS_PER_SKB)
		netif_wake_queue(dev);
}

static void mv643xx_eth_free_all_tx_descs(struct net_device *dev)
{
	mv643xx_eth_free_tx_descs(dev, 1);
}

/*
 * mv643xx_eth_receive
 *
 * This function is forward packets that are received from the port's
 * queues toward kernel core or FastRoute them to another interface.
 *
 * Input :	dev - a pointer to the required interface
 *		max - maximum number to receive (0 means unlimted)
 *
 * Output :	number of served packets
 */
static int mv643xx_eth_receive_queue(struct net_device *dev, int budget)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	unsigned int received_packets = 0;
	struct sk_buff *skb;
	struct pkt_info pkt_info;

	while (budget-- > 0 && eth_port_receive(mp, &pkt_info) == ETH_OK) {
		dma_unmap_single(NULL, pkt_info.buf_ptr, ETH_RX_SKB_SIZE,
							DMA_FROM_DEVICE);
		mp->rx_desc_count--;
		received_packets++;

		/*
		 * Update statistics.
		 * Note byte count includes 4 byte CRC count
		 */
		stats->rx_packets++;
		stats->rx_bytes += pkt_info.byte_cnt;
		skb = pkt_info.return_info;
		/*
		 * In case received a packet without first / last bits on OR
		 * the error summary bit is on, the packets needs to be dropeed.
		 */
		if (((pkt_info.cmd_sts
				& (ETH_RX_FIRST_DESC | ETH_RX_LAST_DESC)) !=
					(ETH_RX_FIRST_DESC | ETH_RX_LAST_DESC))
				|| (pkt_info.cmd_sts & ETH_ERROR_SUMMARY)) {
			stats->rx_dropped++;
			if ((pkt_info.cmd_sts & (ETH_RX_FIRST_DESC |
							ETH_RX_LAST_DESC)) !=
				(ETH_RX_FIRST_DESC | ETH_RX_LAST_DESC)) {
				if (net_ratelimit())
					printk(KERN_ERR
						"%s: Received packet spread "
						"on multiple descriptors\n",
						dev->name);
			}
			if (pkt_info.cmd_sts & ETH_ERROR_SUMMARY)
				stats->rx_errors++;

			dev_kfree_skb_irq(skb);
		} else {
			/*
			 * The -4 is for the CRC in the trailer of the
			 * received packet
			 */
			skb_put(skb, pkt_info.byte_cnt - 4);

			if (pkt_info.cmd_sts & ETH_LAYER_4_CHECKSUM_OK) {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
				skb->csum = htons(
					(pkt_info.cmd_sts & 0x0007fff8) >> 3);
			}
			skb->protocol = eth_type_trans(skb, dev);
#ifdef MV643XX_NAPI
			netif_receive_skb(skb);
#else
			netif_rx(skb);
#endif
		}
		dev->last_rx = jiffies;
	}
	mv643xx_eth_rx_refill_descs(dev);	/* Fill RX ring with skb's */

	return received_packets;
}

/* Set the mv643xx port configuration register for the speed/duplex mode. */
static void mv643xx_eth_update_pscr(struct net_device *dev,
				    struct ethtool_cmd *ecmd)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	int port_num = mp->port_num;
	u32 o_pscr, n_pscr;
	unsigned int queues;

	o_pscr = mv_read(PORT_SERIAL_CONTROL_REG(port_num));
	n_pscr = o_pscr;

	/* clear speed, duplex and rx buffer size fields */
	n_pscr &= ~(SET_MII_SPEED_TO_100  |
		   SET_GMII_SPEED_TO_1000 |
		   SET_FULL_DUPLEX_MODE   |
		   MAX_RX_PACKET_MASK);

	if (ecmd->duplex == DUPLEX_FULL)
		n_pscr |= SET_FULL_DUPLEX_MODE;

	if (ecmd->speed == SPEED_1000)
		n_pscr |= SET_GMII_SPEED_TO_1000 |
			  MAX_RX_PACKET_9700BYTE;
	else {
		if (ecmd->speed == SPEED_100)
			n_pscr |= SET_MII_SPEED_TO_100;
		n_pscr |= MAX_RX_PACKET_1522BYTE;
	}

	if (n_pscr != o_pscr) {
		if ((o_pscr & SERIAL_PORT_ENABLE) == 0)
			mv_write(PORT_SERIAL_CONTROL_REG(port_num), n_pscr);
		else {
			queues = mv643xx_eth_port_disable_tx(port_num);

			o_pscr &= ~SERIAL_PORT_ENABLE;
			mv_write(PORT_SERIAL_CONTROL_REG(port_num), o_pscr);
			mv_write(PORT_SERIAL_CONTROL_REG(port_num), n_pscr);
			mv_write(PORT_SERIAL_CONTROL_REG(port_num), n_pscr);
			if (queues)
				mv643xx_eth_port_enable_tx(port_num, queues);
		}
	}
}

/*
 * mv643xx_eth_int_handler
 *
 * Main interrupt handler for the gigbit ethernet ports
 *
 * Input :	irq	- irq number (not used)
 *		dev_id	- a pointer to the required interface's data structure
 *		regs	- not used
 * Output :	N/A
 */

static irqreturn_t mv643xx_eth_int_handler(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct mv643xx_private *mp = netdev_priv(dev);
	u32 eth_int_cause, eth_int_cause_ext = 0;
	unsigned int port_num = mp->port_num;

	/* Read interrupt cause registers */
	eth_int_cause = mv_read(INTERRUPT_CAUSE_REG(port_num)) &
						ETH_INT_UNMASK_ALL;
	if (eth_int_cause & ETH_INT_CAUSE_EXT) {
		eth_int_cause_ext = mv_read(
			INTERRUPT_CAUSE_EXTEND_REG(port_num)) &
						ETH_INT_UNMASK_ALL_EXT;
		mv_write(INTERRUPT_CAUSE_EXTEND_REG(port_num),
							~eth_int_cause_ext);
	}

	/* PHY status changed */
	if (eth_int_cause_ext & (ETH_INT_CAUSE_PHY | ETH_INT_CAUSE_STATE)) {
		struct ethtool_cmd cmd;

		if (mii_link_ok(&mp->mii)) {
			mii_ethtool_gset(&mp->mii, &cmd);
			mv643xx_eth_update_pscr(dev, &cmd);
			mv643xx_eth_port_enable_tx(port_num,
						   ETH_TX_QUEUES_ENABLED);
			if (!netif_carrier_ok(dev)) {
				netif_carrier_on(dev);
				if (mp->tx_ring_size - mp->tx_desc_count >=
							MAX_DESCS_PER_SKB)
					netif_wake_queue(dev);
			}
		} else if (netif_carrier_ok(dev)) {
			netif_stop_queue(dev);
			netif_carrier_off(dev);
		}
	}

#ifdef MV643XX_NAPI
	if (eth_int_cause & ETH_INT_CAUSE_RX) {
		/* schedule the NAPI poll routine to maintain port */
		mv_write(INTERRUPT_MASK_REG(port_num), ETH_INT_MASK_ALL);

		/* wait for previous write to complete */
		mv_read(INTERRUPT_MASK_REG(port_num));

		netif_rx_schedule(dev, &mp->napi);
	}
#else
	if (eth_int_cause & ETH_INT_CAUSE_RX)
		mv643xx_eth_receive_queue(dev, INT_MAX);
#endif
	if (eth_int_cause_ext & ETH_INT_CAUSE_TX)
		mv643xx_eth_free_completed_tx_descs(dev);

	/*
	 * If no real interrupt occured, exit.
	 * This can happen when using gigE interrupt coalescing mechanism.
	 */
	if ((eth_int_cause == 0x0) && (eth_int_cause_ext == 0x0))
		return IRQ_NONE;

	return IRQ_HANDLED;
}

#ifdef MV643XX_COAL

/*
 * eth_port_set_rx_coal - Sets coalescing interrupt mechanism on RX path
 *
 * DESCRIPTION:
 *	This routine sets the RX coalescing interrupt mechanism parameter.
 *	This parameter is a timeout counter, that counts in 64 t_clk
 *	chunks ; that when timeout event occurs a maskable interrupt
 *	occurs.
 *	The parameter is calculated using the tClk of the MV-643xx chip
 *	, and the required delay of the interrupt in usec.
 *
 * INPUT:
 *	unsigned int eth_port_num	Ethernet port number
 *	unsigned int t_clk		t_clk of the MV-643xx chip in HZ units
 *	unsigned int delay		Delay in usec
 *
 * OUTPUT:
 *	Interrupt coalescing mechanism value is set in MV-643xx chip.
 *
 * RETURN:
 *	The interrupt coalescing value set in the gigE port.
 *
 */
static unsigned int eth_port_set_rx_coal(unsigned int eth_port_num,
					unsigned int t_clk, unsigned int delay)
{
	unsigned int coal = ((t_clk / 1000000) * delay) / 64;

	/* Set RX Coalescing mechanism */
	mv_write(SDMA_CONFIG_REG(eth_port_num),
		((coal & 0x3fff) << 8) |
		(mv_read(SDMA_CONFIG_REG(eth_port_num))
			& 0xffc000ff));

	return coal;
}
#endif

/*
 * eth_port_set_tx_coal - Sets coalescing interrupt mechanism on TX path
 *
 * DESCRIPTION:
 *	This routine sets the TX coalescing interrupt mechanism parameter.
 *	This parameter is a timeout counter, that counts in 64 t_clk
 *	chunks ; that when timeout event occurs a maskable interrupt
 *	occurs.
 *	The parameter is calculated using the t_cLK frequency of the
 *	MV-643xx chip and the required delay in the interrupt in uSec
 *
 * INPUT:
 *	unsigned int eth_port_num	Ethernet port number
 *	unsigned int t_clk		t_clk of the MV-643xx chip in HZ units
 *	unsigned int delay		Delay in uSeconds
 *
 * OUTPUT:
 *	Interrupt coalescing mechanism value is set in MV-643xx chip.
 *
 * RETURN:
 *	The interrupt coalescing value set in the gigE port.
 *
 */
static unsigned int eth_port_set_tx_coal(unsigned int eth_port_num,
					unsigned int t_clk, unsigned int delay)
{
	unsigned int coal;
	coal = ((t_clk / 1000000) * delay) / 64;
	/* Set TX Coalescing mechanism */
	mv_write(TX_FIFO_URGENT_THRESHOLD_REG(eth_port_num), coal << 4);
	return coal;
}

/*
 * ether_init_rx_desc_ring - Curve a Rx chain desc list and buffer in memory.
 *
 * DESCRIPTION:
 *	This function prepares a Rx chained list of descriptors and packet
 *	buffers in a form of a ring. The routine must be called after port
 *	initialization routine and before port start routine.
 *	The Ethernet SDMA engine uses CPU bus addresses to access the various
 *	devices in the system (i.e. DRAM). This function uses the ethernet
 *	struct 'virtual to physical' routine (set by the user) to set the ring
 *	with physical addresses.
 *
 * INPUT:
 *	struct mv643xx_private *mp	Ethernet Port Control srtuct.
 *
 * OUTPUT:
 *	The routine updates the Ethernet port control struct with information
 *	regarding the Rx descriptors and buffers.
 *
 * RETURN:
 *	None.
 */
static void ether_init_rx_desc_ring(struct mv643xx_private *mp)
{
	volatile struct eth_rx_desc *p_rx_desc;
	int rx_desc_num = mp->rx_ring_size;
	int i;

	/* initialize the next_desc_ptr links in the Rx descriptors ring */
	p_rx_desc = (struct eth_rx_desc *)mp->p_rx_desc_area;
	for (i = 0; i < rx_desc_num; i++) {
		p_rx_desc[i].next_desc_ptr = mp->rx_desc_dma +
			((i + 1) % rx_desc_num) * sizeof(struct eth_rx_desc);
	}

	/* Save Rx desc pointer to driver struct. */
	mp->rx_curr_desc_q = 0;
	mp->rx_used_desc_q = 0;

	mp->rx_desc_area_size = rx_desc_num * sizeof(struct eth_rx_desc);
}

/*
 * ether_init_tx_desc_ring - Curve a Tx chain desc list and buffer in memory.
 *
 * DESCRIPTION:
 *	This function prepares a Tx chained list of descriptors and packet
 *	buffers in a form of a ring. The routine must be called after port
 *	initialization routine and before port start routine.
 *	The Ethernet SDMA engine uses CPU bus addresses to access the various
 *	devices in the system (i.e. DRAM). This function uses the ethernet
 *	struct 'virtual to physical' routine (set by the user) to set the ring
 *	with physical addresses.
 *
 * INPUT:
 *	struct mv643xx_private *mp	Ethernet Port Control srtuct.
 *
 * OUTPUT:
 *	The routine updates the Ethernet port control struct with information
 *	regarding the Tx descriptors and buffers.
 *
 * RETURN:
 *	None.
 */
static void ether_init_tx_desc_ring(struct mv643xx_private *mp)
{
	int tx_desc_num = mp->tx_ring_size;
	struct eth_tx_desc *p_tx_desc;
	int i;

	/* Initialize the next_desc_ptr links in the Tx descriptors ring */
	p_tx_desc = (struct eth_tx_desc *)mp->p_tx_desc_area;
	for (i = 0; i < tx_desc_num; i++) {
		p_tx_desc[i].next_desc_ptr = mp->tx_desc_dma +
			((i + 1) % tx_desc_num) * sizeof(struct eth_tx_desc);
	}

	mp->tx_curr_desc_q = 0;
	mp->tx_used_desc_q = 0;

	mp->tx_desc_area_size = tx_desc_num * sizeof(struct eth_tx_desc);
}

static int mv643xx_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	int err;

	spin_lock_irq(&mp->lock);
	err = mii_ethtool_sset(&mp->mii, cmd);
	spin_unlock_irq(&mp->lock);

	return err;
}

static int mv643xx_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	int err;

	spin_lock_irq(&mp->lock);
	err = mii_ethtool_gset(&mp->mii, cmd);
	spin_unlock_irq(&mp->lock);

	/* The PHY may support 1000baseT_Half, but the mv643xx does not */
	cmd->supported &= ~SUPPORTED_1000baseT_Half;
	cmd->advertising &= ~ADVERTISED_1000baseT_Half;

	return err;
}

/*
 * mv643xx_eth_open
 *
 * This function is called when openning the network device. The function
 * should initialize all the hardware, initialize cyclic Rx/Tx
 * descriptors chain and buffers and allocate an IRQ to the network
 * device.
 *
 * Input :	a pointer to the network device structure
 *
 * Output :	zero of success , nonzero if fails.
 */

static int mv643xx_eth_open(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;
	unsigned int size;
	int err;

	/* Clear any pending ethernet port interrupts */
	mv_write(INTERRUPT_CAUSE_REG(port_num), 0);
	mv_write(INTERRUPT_CAUSE_EXTEND_REG(port_num), 0);
	/* wait for previous write to complete */
	mv_read (INTERRUPT_CAUSE_EXTEND_REG(port_num));

	err = request_irq(dev->irq, mv643xx_eth_int_handler,
			IRQF_SHARED | IRQF_SAMPLE_RANDOM, dev->name, dev);
	if (err) {
		printk(KERN_ERR "Can not assign IRQ number to MV643XX_eth%d\n",
								port_num);
		return -EAGAIN;
	}

	eth_port_init(mp);

	memset(&mp->timeout, 0, sizeof(struct timer_list));
	mp->timeout.function = mv643xx_eth_rx_refill_descs_timer_wrapper;
	mp->timeout.data = (unsigned long)dev;

	/* Allocate RX and TX skb rings */
	mp->rx_skb = kmalloc(sizeof(*mp->rx_skb) * mp->rx_ring_size,
								GFP_KERNEL);
	if (!mp->rx_skb) {
		printk(KERN_ERR "%s: Cannot allocate Rx skb ring\n", dev->name);
		err = -ENOMEM;
		goto out_free_irq;
	}
	mp->tx_skb = kmalloc(sizeof(*mp->tx_skb) * mp->tx_ring_size,
								GFP_KERNEL);
	if (!mp->tx_skb) {
		printk(KERN_ERR "%s: Cannot allocate Tx skb ring\n", dev->name);
		err = -ENOMEM;
		goto out_free_rx_skb;
	}

	/* Allocate TX ring */
	mp->tx_desc_count = 0;
	size = mp->tx_ring_size * sizeof(struct eth_tx_desc);
	mp->tx_desc_area_size = size;

	if (mp->tx_sram_size) {
		mp->p_tx_desc_area = ioremap(mp->tx_sram_addr,
							mp->tx_sram_size);
		mp->tx_desc_dma = mp->tx_sram_addr;
	} else
		mp->p_tx_desc_area = dma_alloc_coherent(NULL, size,
							&mp->tx_desc_dma,
							GFP_KERNEL);

	if (!mp->p_tx_desc_area) {
		printk(KERN_ERR "%s: Cannot allocate Tx Ring (size %d bytes)\n",
							dev->name, size);
		err = -ENOMEM;
		goto out_free_tx_skb;
	}
	BUG_ON((u32) mp->p_tx_desc_area & 0xf);	/* check 16-byte alignment */
	memset((void *)mp->p_tx_desc_area, 0, mp->tx_desc_area_size);

	ether_init_tx_desc_ring(mp);

	/* Allocate RX ring */
	mp->rx_desc_count = 0;
	size = mp->rx_ring_size * sizeof(struct eth_rx_desc);
	mp->rx_desc_area_size = size;

	if (mp->rx_sram_size) {
		mp->p_rx_desc_area = ioremap(mp->rx_sram_addr,
							mp->rx_sram_size);
		mp->rx_desc_dma = mp->rx_sram_addr;
	} else
		mp->p_rx_desc_area = dma_alloc_coherent(NULL, size,
							&mp->rx_desc_dma,
							GFP_KERNEL);

	if (!mp->p_rx_desc_area) {
		printk(KERN_ERR "%s: Cannot allocate Rx ring (size %d bytes)\n",
							dev->name, size);
		printk(KERN_ERR "%s: Freeing previously allocated TX queues...",
							dev->name);
		if (mp->rx_sram_size)
			iounmap(mp->p_tx_desc_area);
		else
			dma_free_coherent(NULL, mp->tx_desc_area_size,
					mp->p_tx_desc_area, mp->tx_desc_dma);
		err = -ENOMEM;
		goto out_free_tx_skb;
	}
	memset((void *)mp->p_rx_desc_area, 0, size);

	ether_init_rx_desc_ring(mp);

	mv643xx_eth_rx_refill_descs(dev);	/* Fill RX ring with skb's */

#ifdef MV643XX_NAPI
	napi_enable(&mp->napi);
#endif

	eth_port_start(dev);

	/* Interrupt Coalescing */

#ifdef MV643XX_COAL
	mp->rx_int_coal =
		eth_port_set_rx_coal(port_num, 133000000, MV643XX_RX_COAL);
#endif

	mp->tx_int_coal =
		eth_port_set_tx_coal(port_num, 133000000, MV643XX_TX_COAL);

	/* Unmask phy and link status changes interrupts */
	mv_write(INTERRUPT_EXTEND_MASK_REG(port_num), ETH_INT_UNMASK_ALL_EXT);

	/* Unmask RX buffer and TX end interrupt */
	mv_write(INTERRUPT_MASK_REG(port_num), ETH_INT_UNMASK_ALL);

	return 0;

out_free_tx_skb:
	kfree(mp->tx_skb);
out_free_rx_skb:
	kfree(mp->rx_skb);
out_free_irq:
	free_irq(dev->irq, dev);

	return err;
}

static void mv643xx_eth_free_tx_rings(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);

	/* Stop Tx Queues */
	mv643xx_eth_port_disable_tx(mp->port_num);

	/* Free outstanding skb's on TX ring */
	mv643xx_eth_free_all_tx_descs(dev);

	BUG_ON(mp->tx_used_desc_q != mp->tx_curr_desc_q);

	/* Free TX ring */
	if (mp->tx_sram_size)
		iounmap(mp->p_tx_desc_area);
	else
		dma_free_coherent(NULL, mp->tx_desc_area_size,
				mp->p_tx_desc_area, mp->tx_desc_dma);
}

static void mv643xx_eth_free_rx_rings(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;
	int curr;

	/* Stop RX Queues */
	mv643xx_eth_port_disable_rx(port_num);

	/* Free preallocated skb's on RX rings */
	for (curr = 0; mp->rx_desc_count && curr < mp->rx_ring_size; curr++) {
		if (mp->rx_skb[curr]) {
			dev_kfree_skb(mp->rx_skb[curr]);
			mp->rx_desc_count--;
		}
	}

	if (mp->rx_desc_count)
		printk(KERN_ERR
			"%s: Error in freeing Rx Ring. %d skb's still"
			" stuck in RX Ring - ignoring them\n", dev->name,
			mp->rx_desc_count);
	/* Free RX ring */
	if (mp->rx_sram_size)
		iounmap(mp->p_rx_desc_area);
	else
		dma_free_coherent(NULL, mp->rx_desc_area_size,
				mp->p_rx_desc_area, mp->rx_desc_dma);
}

/*
 * mv643xx_eth_stop
 *
 * This function is used when closing the network device.
 * It updates the hardware,
 * release all memory that holds buffers and descriptors and release the IRQ.
 * Input :	a pointer to the device structure
 * Output :	zero if success , nonzero if fails
 */

static int mv643xx_eth_stop(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;

	/* Mask all interrupts on ethernet port */
	mv_write(INTERRUPT_MASK_REG(port_num), ETH_INT_MASK_ALL);
	/* wait for previous write to complete */
	mv_read(INTERRUPT_MASK_REG(port_num));

#ifdef MV643XX_NAPI
	napi_disable(&mp->napi);
#endif
	netif_carrier_off(dev);
	netif_stop_queue(dev);

	eth_port_reset(mp->port_num);

	mv643xx_eth_free_tx_rings(dev);
	mv643xx_eth_free_rx_rings(dev);

	free_irq(dev->irq, dev);

	return 0;
}

#ifdef MV643XX_NAPI
/*
 * mv643xx_poll
 *
 * This function is used in case of NAPI
 */
static int mv643xx_poll(struct napi_struct *napi, int budget)
{
	struct mv643xx_private *mp = container_of(napi, struct mv643xx_private, napi);
	struct net_device *dev = mp->dev;
	unsigned int port_num = mp->port_num;
	int work_done;

#ifdef MV643XX_TX_FAST_REFILL
	if (++mp->tx_clean_threshold > 5) {
		mv643xx_eth_free_completed_tx_descs(dev);
		mp->tx_clean_threshold = 0;
	}
#endif

	work_done = 0;
	if ((mv_read(RX_CURRENT_QUEUE_DESC_PTR_0(port_num)))
	    != (u32) mp->rx_used_desc_q)
		work_done = mv643xx_eth_receive_queue(dev, budget);

	if (work_done < budget) {
		netif_rx_complete(dev, napi);
		mv_write(INTERRUPT_CAUSE_REG(port_num), 0);
		mv_write(INTERRUPT_CAUSE_EXTEND_REG(port_num), 0);
		mv_write(INTERRUPT_MASK_REG(port_num), ETH_INT_UNMASK_ALL);
	}

	return work_done;
}
#endif

/**
 * has_tiny_unaligned_frags - check if skb has any small, unaligned fragments
 *
 * Hardware can't handle unaligned fragments smaller than 9 bytes.
 * This helper function detects that case.
 */

static inline unsigned int has_tiny_unaligned_frags(struct sk_buff *skb)
{
	unsigned int frag;
	skb_frag_t *fragp;

	for (frag = 0; frag < skb_shinfo(skb)->nr_frags; frag++) {
		fragp = &skb_shinfo(skb)->frags[frag];
		if (fragp->size <= 8 && fragp->page_offset & 0x7)
			return 1;
	}
	return 0;
}

/**
 * eth_alloc_tx_desc_index - return the index of the next available tx desc
 */
static int eth_alloc_tx_desc_index(struct mv643xx_private *mp)
{
	int tx_desc_curr;

	BUG_ON(mp->tx_desc_count >= mp->tx_ring_size);

	tx_desc_curr = mp->tx_curr_desc_q;
	mp->tx_curr_desc_q = (tx_desc_curr + 1) % mp->tx_ring_size;

	BUG_ON(mp->tx_curr_desc_q == mp->tx_used_desc_q);

	return tx_desc_curr;
}

/**
 * eth_tx_fill_frag_descs - fill tx hw descriptors for an skb's fragments.
 *
 * Ensure the data for each fragment to be transmitted is mapped properly,
 * then fill in descriptors in the tx hw queue.
 */
static void eth_tx_fill_frag_descs(struct mv643xx_private *mp,
				   struct sk_buff *skb)
{
	int frag;
	int tx_index;
	struct eth_tx_desc *desc;

	for (frag = 0; frag < skb_shinfo(skb)->nr_frags; frag++) {
		skb_frag_t *this_frag = &skb_shinfo(skb)->frags[frag];

		tx_index = eth_alloc_tx_desc_index(mp);
		desc = &mp->p_tx_desc_area[tx_index];

		desc->cmd_sts = ETH_BUFFER_OWNED_BY_DMA;
		/* Last Frag enables interrupt and frees the skb */
		if (frag == (skb_shinfo(skb)->nr_frags - 1)) {
			desc->cmd_sts |= ETH_ZERO_PADDING |
					 ETH_TX_LAST_DESC |
					 ETH_TX_ENABLE_INTERRUPT;
			mp->tx_skb[tx_index] = skb;
		} else
			mp->tx_skb[tx_index] = NULL;

		desc = &mp->p_tx_desc_area[tx_index];
		desc->l4i_chk = 0;
		desc->byte_cnt = this_frag->size;
		desc->buf_ptr = dma_map_page(NULL, this_frag->page,
						this_frag->page_offset,
						this_frag->size,
						DMA_TO_DEVICE);
	}
}

/**
 * eth_tx_submit_descs_for_skb - submit data from an skb to the tx hw
 *
 * Ensure the data for an skb to be transmitted is mapped properly,
 * then fill in descriptors in the tx hw queue and start the hardware.
 */
static void eth_tx_submit_descs_for_skb(struct mv643xx_private *mp,
					struct sk_buff *skb)
{
	int tx_index;
	struct eth_tx_desc *desc;
	u32 cmd_sts;
	int length;
	int nr_frags = skb_shinfo(skb)->nr_frags;

	cmd_sts = ETH_TX_FIRST_DESC | ETH_GEN_CRC | ETH_BUFFER_OWNED_BY_DMA;

	tx_index = eth_alloc_tx_desc_index(mp);
	desc = &mp->p_tx_desc_area[tx_index];

	if (nr_frags) {
		eth_tx_fill_frag_descs(mp, skb);

		length = skb_headlen(skb);
		mp->tx_skb[tx_index] = NULL;
	} else {
		cmd_sts |= ETH_ZERO_PADDING |
			   ETH_TX_LAST_DESC |
			   ETH_TX_ENABLE_INTERRUPT;
		length = skb->len;
		mp->tx_skb[tx_index] = skb;
	}

	desc->byte_cnt = length;
	desc->buf_ptr = dma_map_single(NULL, skb->data, length, DMA_TO_DEVICE);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		BUG_ON(skb->protocol != ETH_P_IP);

		cmd_sts |= ETH_GEN_TCP_UDP_CHECKSUM |
			   ETH_GEN_IP_V_4_CHECKSUM  |
			   ip_hdr(skb)->ihl << ETH_TX_IHL_SHIFT;

		switch (ip_hdr(skb)->protocol) {
		case IPPROTO_UDP:
			cmd_sts |= ETH_UDP_FRAME;
			desc->l4i_chk = udp_hdr(skb)->check;
			break;
		case IPPROTO_TCP:
			desc->l4i_chk = tcp_hdr(skb)->check;
			break;
		default:
			BUG();
		}
	} else {
		/* Errata BTS #50, IHL must be 5 if no HW checksum */
		cmd_sts |= 5 << ETH_TX_IHL_SHIFT;
		desc->l4i_chk = 0;
	}

	/* ensure all other descriptors are written before first cmd_sts */
	wmb();
	desc->cmd_sts = cmd_sts;

	/* ensure all descriptors are written before poking hardware */
	wmb();
	mv643xx_eth_port_enable_tx(mp->port_num, ETH_TX_QUEUES_ENABLED);

	mp->tx_desc_count += nr_frags + 1;
}

/**
 * mv643xx_eth_start_xmit - queue an skb to the hardware for transmission
 *
 */
static int mv643xx_eth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	unsigned long flags;

	BUG_ON(netif_queue_stopped(dev));
	BUG_ON(skb == NULL);

	if (mp->tx_ring_size - mp->tx_desc_count < MAX_DESCS_PER_SKB) {
		printk(KERN_ERR "%s: transmit with queue full\n", dev->name);
		netif_stop_queue(dev);
		return 1;
	}

	if (has_tiny_unaligned_frags(skb)) {
		if (__skb_linearize(skb)) {
			stats->tx_dropped++;
			printk(KERN_DEBUG "%s: failed to linearize tiny "
					"unaligned fragment\n", dev->name);
			return 1;
		}
	}

	spin_lock_irqsave(&mp->lock, flags);

	eth_tx_submit_descs_for_skb(mp, skb);
	stats->tx_bytes += skb->len;
	stats->tx_packets++;
	dev->trans_start = jiffies;

	if (mp->tx_ring_size - mp->tx_desc_count < MAX_DESCS_PER_SKB)
		netif_stop_queue(dev);

	spin_unlock_irqrestore(&mp->lock, flags);

	return 0;		/* success */
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void mv643xx_netpoll(struct net_device *netdev)
{
	struct mv643xx_private *mp = netdev_priv(netdev);
	int port_num = mp->port_num;

	mv_write(INTERRUPT_MASK_REG(port_num), ETH_INT_MASK_ALL);
	/* wait for previous write to complete */
	mv_read(INTERRUPT_MASK_REG(port_num));

	mv643xx_eth_int_handler(netdev->irq, netdev);

	mv_write(INTERRUPT_MASK_REG(port_num), ETH_INT_UNMASK_ALL);
}
#endif

static void mv643xx_init_ethtool_cmd(struct net_device *dev, int phy_address,
				     int speed, int duplex,
				     struct ethtool_cmd *cmd)
{
	struct mv643xx_private *mp = netdev_priv(dev);

	memset(cmd, 0, sizeof(*cmd));

	cmd->port = PORT_MII;
	cmd->transceiver = XCVR_INTERNAL;
	cmd->phy_address = phy_address;

	if (speed == 0) {
		cmd->autoneg = AUTONEG_ENABLE;
		/* mii lib checks, but doesn't use speed on AUTONEG_ENABLE */
		cmd->speed = SPEED_100;
		cmd->advertising = ADVERTISED_10baseT_Half  |
				   ADVERTISED_10baseT_Full  |
				   ADVERTISED_100baseT_Half |
				   ADVERTISED_100baseT_Full;
		if (mp->mii.supports_gmii)
			cmd->advertising |= ADVERTISED_1000baseT_Full;
	} else {
		cmd->autoneg = AUTONEG_DISABLE;
		cmd->speed = speed;
		cmd->duplex = duplex;
	}
}

/*/
 * mv643xx_eth_probe
 *
 * First function called after registering the network device.
 * It's purpose is to initialize the device as an ethernet device,
 * fill the ethernet device structure with pointers * to functions,
 * and set the MAC address of the interface
 *
 * Input :	struct device *
 * Output :	-ENOMEM if failed , 0 if success
 */
static int mv643xx_eth_probe(struct platform_device *pdev)
{
	struct mv643xx_eth_platform_data *pd;
	int port_num;
	struct mv643xx_private *mp;
	struct net_device *dev;
	u8 *p;
	struct resource *res;
	int err;
	struct ethtool_cmd cmd;
	int duplex = DUPLEX_HALF;
	int speed = 0;			/* default to auto-negotiation */
	DECLARE_MAC_BUF(mac);

	pd = pdev->dev.platform_data;
	if (pd == NULL) {
		printk(KERN_ERR "No mv643xx_eth_platform_data\n");
		return -ENODEV;
	}

	dev = alloc_etherdev(sizeof(struct mv643xx_private));
	if (!dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, dev);

	mp = netdev_priv(dev);
	mp->dev = dev;
#ifdef MV643XX_NAPI
	netif_napi_add(dev, &mp->napi, mv643xx_poll, 64);
#endif

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	BUG_ON(!res);
	dev->irq = res->start;

	dev->open = mv643xx_eth_open;
	dev->stop = mv643xx_eth_stop;
	dev->hard_start_xmit = mv643xx_eth_start_xmit;
	dev->set_mac_address = mv643xx_eth_set_mac_address;
	dev->set_multicast_list = mv643xx_eth_set_rx_mode;

	/* No need to Tx Timeout */
	dev->tx_timeout = mv643xx_eth_tx_timeout;

#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = mv643xx_netpoll;
#endif

	dev->watchdog_timeo = 2 * HZ;
	dev->base_addr = 0;
	dev->change_mtu = mv643xx_eth_change_mtu;
	dev->do_ioctl = mv643xx_eth_do_ioctl;
	SET_ETHTOOL_OPS(dev, &mv643xx_ethtool_ops);

#ifdef MV643XX_CHECKSUM_OFFLOAD_TX
#ifdef MAX_SKB_FRAGS
	/*
	 * Zero copy can only work if we use Discovery II memory. Else, we will
	 * have to map the buffers to ISA memory which is only 16 MB
	 */
	dev->features = NETIF_F_SG | NETIF_F_IP_CSUM;
#endif
#endif

	/* Configure the timeout task */
	INIT_WORK(&mp->tx_timeout_task, mv643xx_eth_tx_timeout_task);

	spin_lock_init(&mp->lock);

	port_num = mp->port_num = pd->port_number;

	/* set default config values */
	eth_port_uc_addr_get(port_num, dev->dev_addr);
	mp->rx_ring_size = PORT_DEFAULT_RECEIVE_QUEUE_SIZE;
	mp->tx_ring_size = PORT_DEFAULT_TRANSMIT_QUEUE_SIZE;

	if (is_valid_ether_addr(pd->mac_addr))
		memcpy(dev->dev_addr, pd->mac_addr, 6);

	if (pd->phy_addr || pd->force_phy_addr)
		ethernet_phy_set(port_num, pd->phy_addr);

	if (pd->rx_queue_size)
		mp->rx_ring_size = pd->rx_queue_size;

	if (pd->tx_queue_size)
		mp->tx_ring_size = pd->tx_queue_size;

	if (pd->tx_sram_size) {
		mp->tx_sram_size = pd->tx_sram_size;
		mp->tx_sram_addr = pd->tx_sram_addr;
	}

	if (pd->rx_sram_size) {
		mp->rx_sram_size = pd->rx_sram_size;
		mp->rx_sram_addr = pd->rx_sram_addr;
	}

	duplex = pd->duplex;
	speed = pd->speed;

	/* Hook up MII support for ethtool */
	mp->mii.dev = dev;
	mp->mii.mdio_read = mv643xx_mdio_read;
	mp->mii.mdio_write = mv643xx_mdio_write;
	mp->mii.phy_id = ethernet_phy_get(port_num);
	mp->mii.phy_id_mask = 0x3f;
	mp->mii.reg_num_mask = 0x1f;

	err = ethernet_phy_detect(port_num);
	if (err) {
		pr_debug("MV643xx ethernet port %d: "
					"No PHY detected at addr %d\n",
					port_num, ethernet_phy_get(port_num));
		goto out;
	}

	ethernet_phy_reset(port_num);
	mp->mii.supports_gmii = mii_check_gmii_support(&mp->mii);
	mv643xx_init_ethtool_cmd(dev, mp->mii.phy_id, speed, duplex, &cmd);
	mv643xx_eth_update_pscr(dev, &cmd);
	mv643xx_set_settings(dev, &cmd);

	SET_NETDEV_DEV(dev, &pdev->dev);
	err = register_netdev(dev);
	if (err)
		goto out;

	p = dev->dev_addr;
	printk(KERN_NOTICE
		"%s: port %d with MAC address %s\n",
		dev->name, port_num, print_mac(mac, p));

	if (dev->features & NETIF_F_SG)
		printk(KERN_NOTICE "%s: Scatter Gather Enabled\n", dev->name);

	if (dev->features & NETIF_F_IP_CSUM)
		printk(KERN_NOTICE "%s: TX TCP/IP Checksumming Supported\n",
								dev->name);

#ifdef MV643XX_CHECKSUM_OFFLOAD_TX
	printk(KERN_NOTICE "%s: RX TCP/UDP Checksum Offload ON \n", dev->name);
#endif

#ifdef MV643XX_COAL
	printk(KERN_NOTICE "%s: TX and RX Interrupt Coalescing ON \n",
								dev->name);
#endif

#ifdef MV643XX_NAPI
	printk(KERN_NOTICE "%s: RX NAPI Enabled \n", dev->name);
#endif

	if (mp->tx_sram_size > 0)
		printk(KERN_NOTICE "%s: Using SRAM\n", dev->name);

	return 0;

out:
	free_netdev(dev);

	return err;
}

static int mv643xx_eth_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);

	unregister_netdev(dev);
	flush_scheduled_work();

	free_netdev(dev);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static int mv643xx_eth_shared_probe(struct platform_device *pdev)
{
	struct resource *res;

	printk(KERN_NOTICE "MV-643xx 10/100/1000 Ethernet Driver\n");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENODEV;

	mv643xx_eth_base = ioremap(res->start, res->end - res->start + 1);
	if (mv643xx_eth_base == NULL)
		return -ENOMEM;

	return 0;

}

static int mv643xx_eth_shared_remove(struct platform_device *pdev)
{
	iounmap(mv643xx_eth_base);
	mv643xx_eth_base = NULL;

	return 0;
}

static void mv643xx_eth_shutdown(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct mv643xx_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;

	/* Mask all interrupts on ethernet port */
	mv_write(INTERRUPT_MASK_REG(port_num), 0);
	mv_read (INTERRUPT_MASK_REG(port_num));

	eth_port_reset(port_num);
}

static struct platform_driver mv643xx_eth_driver = {
	.probe = mv643xx_eth_probe,
	.remove = mv643xx_eth_remove,
	.shutdown = mv643xx_eth_shutdown,
	.driver = {
		.name = MV643XX_ETH_NAME,
	},
};

static struct platform_driver mv643xx_eth_shared_driver = {
	.probe = mv643xx_eth_shared_probe,
	.remove = mv643xx_eth_shared_remove,
	.driver = {
		.name = MV643XX_ETH_SHARED_NAME,
	},
};

/*
 * mv643xx_init_module
 *
 * Registers the network drivers into the Linux kernel
 *
 * Input :	N/A
 *
 * Output :	N/A
 */
static int __init mv643xx_init_module(void)
{
	int rc;

	rc = platform_driver_register(&mv643xx_eth_shared_driver);
	if (!rc) {
		rc = platform_driver_register(&mv643xx_eth_driver);
		if (rc)
			platform_driver_unregister(&mv643xx_eth_shared_driver);
	}
	return rc;
}

/*
 * mv643xx_cleanup_module
 *
 * Registers the network drivers into the Linux kernel
 *
 * Input :	N/A
 *
 * Output :	N/A
 */
static void __exit mv643xx_cleanup_module(void)
{
	platform_driver_unregister(&mv643xx_eth_driver);
	platform_driver_unregister(&mv643xx_eth_shared_driver);
}

module_init(mv643xx_init_module);
module_exit(mv643xx_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(	"Rabeeh Khoury, Assaf Hoffman, Matthew Dharm, Manish Lachwani"
		" and Dale Farnsworth");
MODULE_DESCRIPTION("Ethernet driver for Marvell MV643XX");

/*
 * The second part is the low level driver of the gigE ethernet ports.
 */

/*
 * Marvell's Gigabit Ethernet controller low level driver
 *
 * DESCRIPTION:
 *	This file introduce low level API to Marvell's Gigabit Ethernet
 *		controller. This Gigabit Ethernet Controller driver API controls
 *		1) Operations (i.e. port init, start, reset etc').
 *		2) Data flow (i.e. port send, receive etc').
 *		Each Gigabit Ethernet port is controlled via
 *		struct mv643xx_private.
 *		This struct includes user configuration information as well as
 *		driver internal data needed for its operations.
 *
 *		Supported Features:
 *		- This low level driver is OS independent. Allocating memory for
 *		  the descriptor rings and buffers are not within the scope of
 *		  this driver.
 *		- The user is free from Rx/Tx queue managing.
 *		- This low level driver introduce functionality API that enable
 *		  the to operate Marvell's Gigabit Ethernet Controller in a
 *		  convenient way.
 *		- Simple Gigabit Ethernet port operation API.
 *		- Simple Gigabit Ethernet port data flow API.
 *		- Data flow and operation API support per queue functionality.
 *		- Support cached descriptors for better performance.
 *		- Enable access to all four DRAM banks and internal SRAM memory
 *		  spaces.
 *		- PHY access and control API.
 *		- Port control register configuration API.
 *		- Full control over Unicast and Multicast MAC configurations.
 *
 *		Operation flow:
 *
 *		Initialization phase
 *		This phase complete the initialization of the the
 *		mv643xx_private struct.
 *		User information regarding port configuration has to be set
 *		prior to calling the port initialization routine.
 *
 *		In this phase any port Tx/Rx activity is halted, MIB counters
 *		are cleared, PHY address is set according to user parameter and
 *		access to DRAM and internal SRAM memory spaces.
 *
 *		Driver ring initialization
 *		Allocating memory for the descriptor rings and buffers is not
 *		within the scope of this driver. Thus, the user is required to
 *		allocate memory for the descriptors ring and buffers. Those
 *		memory parameters are used by the Rx and Tx ring initialization
 *		routines in order to curve the descriptor linked list in a form
 *		of a ring.
 *		Note: Pay special attention to alignment issues when using
 *		cached descriptors/buffers. In this phase the driver store
 *		information in the mv643xx_private struct regarding each queue
 *		ring.
 *
 *		Driver start
 *		This phase prepares the Ethernet port for Rx and Tx activity.
 *		It uses the information stored in the mv643xx_private struct to
 *		initialize the various port registers.
 *
 *		Data flow:
 *		All packet references to/from the driver are done using
 *		struct pkt_info.
 *		This struct is a unified struct used with Rx and Tx operations.
 *		This way the user is not required to be familiar with neither
 *		Tx nor Rx descriptors structures.
 *		The driver's descriptors rings are management by indexes.
 *		Those indexes controls the ring resources and used to indicate
 *		a SW resource error:
 *		'current'
 *		This index points to the current available resource for use. For
 *		example in Rx process this index will point to the descriptor
 *		that will be passed to the user upon calling the receive
 *		routine.  In Tx process, this index will point to the descriptor
 *		that will be assigned with the user packet info and transmitted.
 *		'used'
 *		This index points to the descriptor that need to restore its
 *		resources. For example in Rx process, using the Rx buffer return
 *		API will attach the buffer returned in packet info to the
 *		descriptor pointed by 'used'. In Tx process, using the Tx
 *		descriptor return will merely return the user packet info with
 *		the command status of the transmitted buffer pointed by the
 *		'used' index. Nevertheless, it is essential to use this routine
 *		to update the 'used' index.
 *		'first'
 *		This index supports Tx Scatter-Gather. It points to the first
 *		descriptor of a packet assembled of multiple buffers. For
 *		example when in middle of Such packet we have a Tx resource
 *		error the 'curr' index get the value of 'first' to indicate
 *		that the ring returned to its state before trying to transmit
 *		this packet.
 *
 *		Receive operation:
 *		The eth_port_receive API set the packet information struct,
 *		passed by the caller, with received information from the
 *		'current' SDMA descriptor.
 *		It is the user responsibility to return this resource back
 *		to the Rx descriptor ring to enable the reuse of this source.
 *		Return Rx resource is done using the eth_rx_return_buff API.
 *
 *	Prior to calling the initialization routine eth_port_init() the user
 *	must set the following fields under mv643xx_private struct:
 *	port_num		User Ethernet port number.
 *	port_config		User port configuration value.
 *	port_config_extend	User port config extend value.
 *	port_sdma_config	User port SDMA config value.
 *	port_serial_control	User port serial control value.
 *
 *		This driver data flow is done using the struct pkt_info which
 *		is a unified struct for Rx and Tx operations:
 *
 *		byte_cnt	Tx/Rx descriptor buffer byte count.
 *		l4i_chk		CPU provided TCP Checksum. For Tx operation
 *				only.
 *		cmd_sts		Tx/Rx descriptor command status.
 *		buf_ptr		Tx/Rx descriptor buffer pointer.
 *		return_info	Tx/Rx user resource return information.
 */

/* PHY routines */
static int ethernet_phy_get(unsigned int eth_port_num);
static void ethernet_phy_set(unsigned int eth_port_num, int phy_addr);

/* Ethernet Port routines */
static void eth_port_set_filter_table_entry(int table, unsigned char entry);

/*
 * eth_port_init - Initialize the Ethernet port driver
 *
 * DESCRIPTION:
 *	This function prepares the ethernet port to start its activity:
 *	1) Completes the ethernet port driver struct initialization toward port
 *		start routine.
 *	2) Resets the device to a quiescent state in case of warm reboot.
 *	3) Enable SDMA access to all four DRAM banks as well as internal SRAM.
 *	4) Clean MAC tables. The reset status of those tables is unknown.
 *	5) Set PHY address.
 *	Note: Call this routine prior to eth_port_start routine and after
 *	setting user values in the user fields of Ethernet port control
 *	struct.
 *
 * INPUT:
 *	struct mv643xx_private *mp	Ethernet port control struct
 *
 * OUTPUT:
 *	See description.
 *
 * RETURN:
 *	None.
 */
static void eth_port_init(struct mv643xx_private *mp)
{
	mp->rx_resource_err = 0;

	eth_port_reset(mp->port_num);

	eth_port_init_mac_tables(mp->port_num);
}

/*
 * eth_port_start - Start the Ethernet port activity.
 *
 * DESCRIPTION:
 *	This routine prepares the Ethernet port for Rx and Tx activity:
 *	 1. Initialize Tx and Rx Current Descriptor Pointer for each queue that
 *	    has been initialized a descriptor's ring (using
 *	    ether_init_tx_desc_ring for Tx and ether_init_rx_desc_ring for Rx)
 *	 2. Initialize and enable the Ethernet configuration port by writing to
 *	    the port's configuration and command registers.
 *	 3. Initialize and enable the SDMA by writing to the SDMA's
 *	    configuration and command registers.  After completing these steps,
 *	    the ethernet port SDMA can starts to perform Rx and Tx activities.
 *
 *	Note: Each Rx and Tx queue descriptor's list must be initialized prior
 *	to calling this function (use ether_init_tx_desc_ring for Tx queues
 *	and ether_init_rx_desc_ring for Rx queues).
 *
 * INPUT:
 *	dev - a pointer to the required interface
 *
 * OUTPUT:
 *	Ethernet port is ready to receive and transmit.
 *
 * RETURN:
 *	None.
 */
static void eth_port_start(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;
	int tx_curr_desc, rx_curr_desc;
	u32 pscr;
	struct ethtool_cmd ethtool_cmd;

	/* Assignment of Tx CTRP of given queue */
	tx_curr_desc = mp->tx_curr_desc_q;
	mv_write(TX_CURRENT_QUEUE_DESC_PTR_0(port_num),
		(u32)((struct eth_tx_desc *)mp->tx_desc_dma + tx_curr_desc));

	/* Assignment of Rx CRDP of given queue */
	rx_curr_desc = mp->rx_curr_desc_q;
	mv_write(RX_CURRENT_QUEUE_DESC_PTR_0(port_num),
		(u32)((struct eth_rx_desc *)mp->rx_desc_dma + rx_curr_desc));

	/* Add the assigned Ethernet address to the port's address table */
	eth_port_uc_addr_set(port_num, dev->dev_addr);

	/* Assign port configuration and command. */
	mv_write(PORT_CONFIG_REG(port_num),
			  PORT_CONFIG_DEFAULT_VALUE);

	mv_write(PORT_CONFIG_EXTEND_REG(port_num),
			  PORT_CONFIG_EXTEND_DEFAULT_VALUE);

	pscr = mv_read(PORT_SERIAL_CONTROL_REG(port_num));

	pscr &= ~(SERIAL_PORT_ENABLE | FORCE_LINK_PASS);
	mv_write(PORT_SERIAL_CONTROL_REG(port_num), pscr);

	pscr |= DISABLE_AUTO_NEG_FOR_FLOW_CTRL |
		DISABLE_AUTO_NEG_SPEED_GMII    |
		DISABLE_AUTO_NEG_FOR_DUPLX     |
		DO_NOT_FORCE_LINK_FAIL	   |
		SERIAL_PORT_CONTROL_RESERVED;

	mv_write(PORT_SERIAL_CONTROL_REG(port_num), pscr);

	pscr |= SERIAL_PORT_ENABLE;
	mv_write(PORT_SERIAL_CONTROL_REG(port_num), pscr);

	/* Assign port SDMA configuration */
	mv_write(SDMA_CONFIG_REG(port_num),
			  PORT_SDMA_CONFIG_DEFAULT_VALUE);

	/* Enable port Rx. */
	mv643xx_eth_port_enable_rx(port_num, ETH_RX_QUEUES_ENABLED);

	/* Disable port bandwidth limits by clearing MTU register */
	mv_write(MAXIMUM_TRANSMIT_UNIT(port_num), 0);

	/* save phy settings across reset */
	mv643xx_get_settings(dev, &ethtool_cmd);
	ethernet_phy_reset(mp->port_num);
	mv643xx_set_settings(dev, &ethtool_cmd);
}

/*
 * eth_port_uc_addr_set - Write a MAC address into the port's hw registers
 */
static void eth_port_uc_addr_set(unsigned int port_num, unsigned char *p_addr)
{
	unsigned int mac_h;
	unsigned int mac_l;
	int table;

	mac_l = (p_addr[4] << 8) | (p_addr[5]);
	mac_h = (p_addr[0] << 24) | (p_addr[1] << 16) | (p_addr[2] << 8) |
							(p_addr[3] << 0);

	mv_write(MAC_ADDR_LOW(port_num), mac_l);
	mv_write(MAC_ADDR_HIGH(port_num), mac_h);

	/* Accept frames with this address */
	table = DA_FILTER_UNICAST_TABLE_BASE(port_num);
	eth_port_set_filter_table_entry(table, p_addr[5] & 0x0f);
}

/*
 * eth_port_uc_addr_get - Read the MAC address from the port's hw registers
 */
static void eth_port_uc_addr_get(unsigned int port_num, unsigned char *p_addr)
{
	unsigned int mac_h;
	unsigned int mac_l;

	mac_h = mv_read(MAC_ADDR_HIGH(port_num));
	mac_l = mv_read(MAC_ADDR_LOW(port_num));

	p_addr[0] = (mac_h >> 24) & 0xff;
	p_addr[1] = (mac_h >> 16) & 0xff;
	p_addr[2] = (mac_h >> 8) & 0xff;
	p_addr[3] = mac_h & 0xff;
	p_addr[4] = (mac_l >> 8) & 0xff;
	p_addr[5] = mac_l & 0xff;
}

/*
 * The entries in each table are indexed by a hash of a packet's MAC
 * address.  One bit in each entry determines whether the packet is
 * accepted.  There are 4 entries (each 8 bits wide) in each register
 * of the table.  The bits in each entry are defined as follows:
 *	0	Accept=1, Drop=0
 *	3-1	Queue			(ETH_Q0=0)
 *	7-4	Reserved = 0;
 */
static void eth_port_set_filter_table_entry(int table, unsigned char entry)
{
	unsigned int table_reg;
	unsigned int tbl_offset;
	unsigned int reg_offset;

	tbl_offset = (entry / 4) * 4;	/* Register offset of DA table entry */
	reg_offset = entry % 4;		/* Entry offset within the register */

	/* Set "accepts frame bit" at specified table entry */
	table_reg = mv_read(table + tbl_offset);
	table_reg |= 0x01 << (8 * reg_offset);
	mv_write(table + tbl_offset, table_reg);
}

/*
 * eth_port_mc_addr - Multicast address settings.
 *
 * The MV device supports multicast using two tables:
 * 1) Special Multicast Table for MAC addresses of the form
 *    0x01-00-5E-00-00-XX (where XX is between 0x00 and 0x_FF).
 *    The MAC DA[7:0] bits are used as a pointer to the Special Multicast
 *    Table entries in the DA-Filter table.
 * 2) Other Multicast Table for multicast of another type. A CRC-8bit
 *    is used as an index to the Other Multicast Table entries in the
 *    DA-Filter table.  This function calculates the CRC-8bit value.
 * In either case, eth_port_set_filter_table_entry() is then called
 * to set to set the actual table entry.
 */
static void eth_port_mc_addr(unsigned int eth_port_num, unsigned char *p_addr)
{
	unsigned int mac_h;
	unsigned int mac_l;
	unsigned char crc_result = 0;
	int table;
	int mac_array[48];
	int crc[8];
	int i;

	if ((p_addr[0] == 0x01) && (p_addr[1] == 0x00) &&
	    (p_addr[2] == 0x5E) && (p_addr[3] == 0x00) && (p_addr[4] == 0x00)) {
		table = DA_FILTER_SPECIAL_MULTICAST_TABLE_BASE
					(eth_port_num);
		eth_port_set_filter_table_entry(table, p_addr[5]);
		return;
	}

	/* Calculate CRC-8 out of the given address */
	mac_h = (p_addr[0] << 8) | (p_addr[1]);
	mac_l = (p_addr[2] << 24) | (p_addr[3] << 16) |
			(p_addr[4] << 8) | (p_addr[5] << 0);

	for (i = 0; i < 32; i++)
		mac_array[i] = (mac_l >> i) & 0x1;
	for (i = 32; i < 48; i++)
		mac_array[i] = (mac_h >> (i - 32)) & 0x1;

	crc[0] = mac_array[45] ^ mac_array[43] ^ mac_array[40] ^ mac_array[39] ^
		 mac_array[35] ^ mac_array[34] ^ mac_array[31] ^ mac_array[30] ^
		 mac_array[28] ^ mac_array[23] ^ mac_array[21] ^ mac_array[19] ^
		 mac_array[18] ^ mac_array[16] ^ mac_array[14] ^ mac_array[12] ^
		 mac_array[8]  ^ mac_array[7]  ^ mac_array[6]  ^ mac_array[0];

	crc[1] = mac_array[46] ^ mac_array[45] ^ mac_array[44] ^ mac_array[43] ^
		 mac_array[41] ^ mac_array[39] ^ mac_array[36] ^ mac_array[34] ^
		 mac_array[32] ^ mac_array[30] ^ mac_array[29] ^ mac_array[28] ^
		 mac_array[24] ^ mac_array[23] ^ mac_array[22] ^ mac_array[21] ^
		 mac_array[20] ^ mac_array[18] ^ mac_array[17] ^ mac_array[16] ^
		 mac_array[15] ^ mac_array[14] ^ mac_array[13] ^ mac_array[12] ^
		 mac_array[9]  ^ mac_array[6]  ^ mac_array[1]  ^ mac_array[0];

	crc[2] = mac_array[47] ^ mac_array[46] ^ mac_array[44] ^ mac_array[43] ^
		 mac_array[42] ^ mac_array[39] ^ mac_array[37] ^ mac_array[34] ^
		 mac_array[33] ^ mac_array[29] ^ mac_array[28] ^ mac_array[25] ^
		 mac_array[24] ^ mac_array[22] ^ mac_array[17] ^ mac_array[15] ^
		 mac_array[13] ^ mac_array[12] ^ mac_array[10] ^ mac_array[8]  ^
		 mac_array[6]  ^ mac_array[2]  ^ mac_array[1]  ^ mac_array[0];

	crc[3] = mac_array[47] ^ mac_array[45] ^ mac_array[44] ^ mac_array[43] ^
		 mac_array[40] ^ mac_array[38] ^ mac_array[35] ^ mac_array[34] ^
		 mac_array[30] ^ mac_array[29] ^ mac_array[26] ^ mac_array[25] ^
		 mac_array[23] ^ mac_array[18] ^ mac_array[16] ^ mac_array[14] ^
		 mac_array[13] ^ mac_array[11] ^ mac_array[9]  ^ mac_array[7]  ^
		 mac_array[3]  ^ mac_array[2]  ^ mac_array[1];

	crc[4] = mac_array[46] ^ mac_array[45] ^ mac_array[44] ^ mac_array[41] ^
		 mac_array[39] ^ mac_array[36] ^ mac_array[35] ^ mac_array[31] ^
		 mac_array[30] ^ mac_array[27] ^ mac_array[26] ^ mac_array[24] ^
		 mac_array[19] ^ mac_array[17] ^ mac_array[15] ^ mac_array[14] ^
		 mac_array[12] ^ mac_array[10] ^ mac_array[8]  ^ mac_array[4]  ^
		 mac_array[3]  ^ mac_array[2];

	crc[5] = mac_array[47] ^ mac_array[46] ^ mac_array[45] ^ mac_array[42] ^
		 mac_array[40] ^ mac_array[37] ^ mac_array[36] ^ mac_array[32] ^
		 mac_array[31] ^ mac_array[28] ^ mac_array[27] ^ mac_array[25] ^
		 mac_array[20] ^ mac_array[18] ^ mac_array[16] ^ mac_array[15] ^
		 mac_array[13] ^ mac_array[11] ^ mac_array[9]  ^ mac_array[5]  ^
		 mac_array[4]  ^ mac_array[3];

	crc[6] = mac_array[47] ^ mac_array[46] ^ mac_array[43] ^ mac_array[41] ^
		 mac_array[38] ^ mac_array[37] ^ mac_array[33] ^ mac_array[32] ^
		 mac_array[29] ^ mac_array[28] ^ mac_array[26] ^ mac_array[21] ^
		 mac_array[19] ^ mac_array[17] ^ mac_array[16] ^ mac_array[14] ^
		 mac_array[12] ^ mac_array[10] ^ mac_array[6]  ^ mac_array[5]  ^
		 mac_array[4];

	crc[7] = mac_array[47] ^ mac_array[44] ^ mac_array[42] ^ mac_array[39] ^
		 mac_array[38] ^ mac_array[34] ^ mac_array[33] ^ mac_array[30] ^
		 mac_array[29] ^ mac_array[27] ^ mac_array[22] ^ mac_array[20] ^
		 mac_array[18] ^ mac_array[17] ^ mac_array[15] ^ mac_array[13] ^
		 mac_array[11] ^ mac_array[7]  ^ mac_array[6]  ^ mac_array[5];

	for (i = 0; i < 8; i++)
		crc_result = crc_result | (crc[i] << i);

	table = DA_FILTER_OTHER_MULTICAST_TABLE_BASE(eth_port_num);
	eth_port_set_filter_table_entry(table, crc_result);
}

/*
 * Set the entire multicast list based on dev->mc_list.
 */
static void eth_port_set_multicast_list(struct net_device *dev)
{

	struct dev_mc_list	*mc_list;
	int			i;
	int			table_index;
	struct mv643xx_private	*mp = netdev_priv(dev);
	unsigned int		eth_port_num = mp->port_num;

	/* If the device is in promiscuous mode or in all multicast mode,
	 * we will fully populate both multicast tables with accept.
	 * This is guaranteed to yield a match on all multicast addresses...
	 */
	if ((dev->flags & IFF_PROMISC) || (dev->flags & IFF_ALLMULTI)) {
		for (table_index = 0; table_index <= 0xFC; table_index += 4) {
			/* Set all entries in DA filter special multicast
			 * table (Ex_dFSMT)
			 * Set for ETH_Q0 for now
			 * Bits
			 * 0	  Accept=1, Drop=0
			 * 3-1  Queue	 ETH_Q0=0
			 * 7-4  Reserved = 0;
			 */
			mv_write(DA_FILTER_SPECIAL_MULTICAST_TABLE_BASE(eth_port_num) + table_index, 0x01010101);

			/* Set all entries in DA filter other multicast
			 * table (Ex_dFOMT)
			 * Set for ETH_Q0 for now
			 * Bits
			 * 0	  Accept=1, Drop=0
			 * 3-1  Queue	 ETH_Q0=0
			 * 7-4  Reserved = 0;
			 */
			mv_write(DA_FILTER_OTHER_MULTICAST_TABLE_BASE(eth_port_num) + table_index, 0x01010101);
		}
		return;
	}

	/* We will clear out multicast tables every time we get the list.
	 * Then add the entire new list...
	 */
	for (table_index = 0; table_index <= 0xFC; table_index += 4) {
		/* Clear DA filter special multicast table (Ex_dFSMT) */
		mv_write(DA_FILTER_SPECIAL_MULTICAST_TABLE_BASE
				(eth_port_num) + table_index, 0);

		/* Clear DA filter other multicast table (Ex_dFOMT) */
		mv_write(DA_FILTER_OTHER_MULTICAST_TABLE_BASE
				(eth_port_num) + table_index, 0);
	}

	/* Get pointer to net_device multicast list and add each one... */
	for (i = 0, mc_list = dev->mc_list;
			(i < 256) && (mc_list != NULL) && (i < dev->mc_count);
			i++, mc_list = mc_list->next)
		if (mc_list->dmi_addrlen == 6)
			eth_port_mc_addr(eth_port_num, mc_list->dmi_addr);
}

/*
 * eth_port_init_mac_tables - Clear all entrance in the UC, SMC and OMC tables
 *
 * DESCRIPTION:
 *	Go through all the DA filter tables (Unicast, Special Multicast &
 *	Other Multicast) and set each entry to 0.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *
 * OUTPUT:
 *	Multicast and Unicast packets are rejected.
 *
 * RETURN:
 *	None.
 */
static void eth_port_init_mac_tables(unsigned int eth_port_num)
{
	int table_index;

	/* Clear DA filter unicast table (Ex_dFUT) */
	for (table_index = 0; table_index <= 0xC; table_index += 4)
		mv_write(DA_FILTER_UNICAST_TABLE_BASE
					(eth_port_num) + table_index, 0);

	for (table_index = 0; table_index <= 0xFC; table_index += 4) {
		/* Clear DA filter special multicast table (Ex_dFSMT) */
		mv_write(DA_FILTER_SPECIAL_MULTICAST_TABLE_BASE
					(eth_port_num) + table_index, 0);
		/* Clear DA filter other multicast table (Ex_dFOMT) */
		mv_write(DA_FILTER_OTHER_MULTICAST_TABLE_BASE
					(eth_port_num) + table_index, 0);
	}
}

/*
 * eth_clear_mib_counters - Clear all MIB counters
 *
 * DESCRIPTION:
 *	This function clears all MIB counters of a specific ethernet port.
 *	A read from the MIB counter will reset the counter.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *
 * OUTPUT:
 *	After reading all MIB counters, the counters resets.
 *
 * RETURN:
 *	MIB counter value.
 *
 */
static void eth_clear_mib_counters(unsigned int eth_port_num)
{
	int i;

	/* Perform dummy reads from MIB counters */
	for (i = ETH_MIB_GOOD_OCTETS_RECEIVED_LOW; i < ETH_MIB_LATE_COLLISION;
									i += 4)
		mv_read(MIB_COUNTERS_BASE(eth_port_num) + i);
}

static inline u32 read_mib(struct mv643xx_private *mp, int offset)
{
	return mv_read(MIB_COUNTERS_BASE(mp->port_num) + offset);
}

static void eth_update_mib_counters(struct mv643xx_private *mp)
{
	struct mv643xx_mib_counters *p = &mp->mib_counters;
	int offset;

	p->good_octets_received +=
		read_mib(mp, ETH_MIB_GOOD_OCTETS_RECEIVED_LOW);
	p->good_octets_received +=
		(u64)read_mib(mp, ETH_MIB_GOOD_OCTETS_RECEIVED_HIGH) << 32;

	for (offset = ETH_MIB_BAD_OCTETS_RECEIVED;
			offset <= ETH_MIB_FRAMES_1024_TO_MAX_OCTETS;
			offset += 4)
		*(u32 *)((char *)p + offset) += read_mib(mp, offset);

	p->good_octets_sent += read_mib(mp, ETH_MIB_GOOD_OCTETS_SENT_LOW);
	p->good_octets_sent +=
		(u64)read_mib(mp, ETH_MIB_GOOD_OCTETS_SENT_HIGH) << 32;

	for (offset = ETH_MIB_GOOD_FRAMES_SENT;
			offset <= ETH_MIB_LATE_COLLISION;
			offset += 4)
		*(u32 *)((char *)p + offset) += read_mib(mp, offset);
}

/*
 * ethernet_phy_detect - Detect whether a phy is present
 *
 * DESCRIPTION:
 *	This function tests whether there is a PHY present on
 *	the specified port.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *
 * OUTPUT:
 *	None
 *
 * RETURN:
 *	0 on success
 *	-ENODEV on failure
 *
 */
static int ethernet_phy_detect(unsigned int port_num)
{
	unsigned int phy_reg_data0;
	int auto_neg;

	eth_port_read_smi_reg(port_num, 0, &phy_reg_data0);
	auto_neg = phy_reg_data0 & 0x1000;
	phy_reg_data0 ^= 0x1000;	/* invert auto_neg */
	eth_port_write_smi_reg(port_num, 0, phy_reg_data0);

	eth_port_read_smi_reg(port_num, 0, &phy_reg_data0);
	if ((phy_reg_data0 & 0x1000) == auto_neg)
		return -ENODEV;				/* change didn't take */

	phy_reg_data0 ^= 0x1000;
	eth_port_write_smi_reg(port_num, 0, phy_reg_data0);
	return 0;
}

/*
 * ethernet_phy_get - Get the ethernet port PHY address.
 *
 * DESCRIPTION:
 *	This routine returns the given ethernet port PHY address.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *
 * OUTPUT:
 *	None.
 *
 * RETURN:
 *	PHY address.
 *
 */
static int ethernet_phy_get(unsigned int eth_port_num)
{
	unsigned int reg_data;

	reg_data = mv_read(PHY_ADDR_REG);

	return ((reg_data >> (5 * eth_port_num)) & 0x1f);
}

/*
 * ethernet_phy_set - Set the ethernet port PHY address.
 *
 * DESCRIPTION:
 *	This routine sets the given ethernet port PHY address.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *	int		phy_addr	PHY address.
 *
 * OUTPUT:
 *	None.
 *
 * RETURN:
 *	None.
 *
 */
static void ethernet_phy_set(unsigned int eth_port_num, int phy_addr)
{
	u32 reg_data;
	int addr_shift = 5 * eth_port_num;

	reg_data = mv_read(PHY_ADDR_REG);
	reg_data &= ~(0x1f << addr_shift);
	reg_data |= (phy_addr & 0x1f) << addr_shift;
	mv_write(PHY_ADDR_REG, reg_data);
}

/*
 * ethernet_phy_reset - Reset Ethernet port PHY.
 *
 * DESCRIPTION:
 *	This routine utilizes the SMI interface to reset the ethernet port PHY.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *
 * OUTPUT:
 *	The PHY is reset.
 *
 * RETURN:
 *	None.
 *
 */
static void ethernet_phy_reset(unsigned int eth_port_num)
{
	unsigned int phy_reg_data;

	/* Reset the PHY */
	eth_port_read_smi_reg(eth_port_num, 0, &phy_reg_data);
	phy_reg_data |= 0x8000;	/* Set bit 15 to reset the PHY */
	eth_port_write_smi_reg(eth_port_num, 0, phy_reg_data);

	/* wait for PHY to come out of reset */
	do {
		udelay(1);
		eth_port_read_smi_reg(eth_port_num, 0, &phy_reg_data);
	} while (phy_reg_data & 0x8000);
}

static void mv643xx_eth_port_enable_tx(unsigned int port_num,
					unsigned int queues)
{
	mv_write(TRANSMIT_QUEUE_COMMAND_REG(port_num), queues);
}

static void mv643xx_eth_port_enable_rx(unsigned int port_num,
					unsigned int queues)
{
	mv_write(RECEIVE_QUEUE_COMMAND_REG(port_num), queues);
}

static unsigned int mv643xx_eth_port_disable_tx(unsigned int port_num)
{
	u32 queues;

	/* Stop Tx port activity. Check port Tx activity. */
	queues = mv_read(TRANSMIT_QUEUE_COMMAND_REG(port_num)) & 0xFF;
	if (queues) {
		/* Issue stop command for active queues only */
		mv_write(TRANSMIT_QUEUE_COMMAND_REG(port_num), (queues << 8));

		/* Wait for all Tx activity to terminate. */
		/* Check port cause register that all Tx queues are stopped */
		while (mv_read(TRANSMIT_QUEUE_COMMAND_REG(port_num)) & 0xFF)
			udelay(PHY_WAIT_MICRO_SECONDS);

		/* Wait for Tx FIFO to empty */
		while (mv_read(PORT_STATUS_REG(port_num)) &
							ETH_PORT_TX_FIFO_EMPTY)
			udelay(PHY_WAIT_MICRO_SECONDS);
	}

	return queues;
}

static unsigned int mv643xx_eth_port_disable_rx(unsigned int port_num)
{
	u32 queues;

	/* Stop Rx port activity. Check port Rx activity. */
	queues = mv_read(RECEIVE_QUEUE_COMMAND_REG(port_num)) & 0xFF;
	if (queues) {
		/* Issue stop command for active queues only */
		mv_write(RECEIVE_QUEUE_COMMAND_REG(port_num), (queues << 8));

		/* Wait for all Rx activity to terminate. */
		/* Check port cause register that all Rx queues are stopped */
		while (mv_read(RECEIVE_QUEUE_COMMAND_REG(port_num)) & 0xFF)
			udelay(PHY_WAIT_MICRO_SECONDS);
	}

	return queues;
}

/*
 * eth_port_reset - Reset Ethernet port
 *
 * DESCRIPTION:
 * 	This routine resets the chip by aborting any SDMA engine activity and
 *	clearing the MIB counters. The Receiver and the Transmit unit are in
 *	idle state after this command is performed and the port is disabled.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *
 * OUTPUT:
 *	Channel activity is halted.
 *
 * RETURN:
 *	None.
 *
 */
static void eth_port_reset(unsigned int port_num)
{
	unsigned int reg_data;

	mv643xx_eth_port_disable_tx(port_num);
	mv643xx_eth_port_disable_rx(port_num);

	/* Clear all MIB counters */
	eth_clear_mib_counters(port_num);

	/* Reset the Enable bit in the Configuration Register */
	reg_data = mv_read(PORT_SERIAL_CONTROL_REG(port_num));
	reg_data &= ~(SERIAL_PORT_ENABLE		|
			DO_NOT_FORCE_LINK_FAIL	|
			FORCE_LINK_PASS);
	mv_write(PORT_SERIAL_CONTROL_REG(port_num), reg_data);
}


/*
 * eth_port_read_smi_reg - Read PHY registers
 *
 * DESCRIPTION:
 *	This routine utilize the SMI interface to interact with the PHY in
 *	order to perform PHY register read.
 *
 * INPUT:
 *	unsigned int	port_num	Ethernet Port number.
 *	unsigned int	phy_reg		PHY register address offset.
 *	unsigned int	*value		Register value buffer.
 *
 * OUTPUT:
 *	Write the value of a specified PHY register into given buffer.
 *
 * RETURN:
 *	false if the PHY is busy or read data is not in valid state.
 *	true otherwise.
 *
 */
static void eth_port_read_smi_reg(unsigned int port_num,
				unsigned int phy_reg, unsigned int *value)
{
	int phy_addr = ethernet_phy_get(port_num);
	unsigned long flags;
	int i;

	/* the SMI register is a shared resource */
	spin_lock_irqsave(&mv643xx_eth_phy_lock, flags);

	/* wait for the SMI register to become available */
	for (i = 0; mv_read(SMI_REG) & ETH_SMI_BUSY; i++) {
		if (i == PHY_WAIT_ITERATIONS) {
			printk("mv643xx PHY busy timeout, port %d\n", port_num);
			goto out;
		}
		udelay(PHY_WAIT_MICRO_SECONDS);
	}

	mv_write(SMI_REG,
		(phy_addr << 16) | (phy_reg << 21) | ETH_SMI_OPCODE_READ);

	/* now wait for the data to be valid */
	for (i = 0; !(mv_read(SMI_REG) & ETH_SMI_READ_VALID); i++) {
		if (i == PHY_WAIT_ITERATIONS) {
			printk("mv643xx PHY read timeout, port %d\n", port_num);
			goto out;
		}
		udelay(PHY_WAIT_MICRO_SECONDS);
	}

	*value = mv_read(SMI_REG) & 0xffff;
out:
	spin_unlock_irqrestore(&mv643xx_eth_phy_lock, flags);
}

/*
 * eth_port_write_smi_reg - Write to PHY registers
 *
 * DESCRIPTION:
 *	This routine utilize the SMI interface to interact with the PHY in
 *	order to perform writes to PHY registers.
 *
 * INPUT:
 *	unsigned int	eth_port_num	Ethernet Port number.
 *	unsigned int	phy_reg		PHY register address offset.
 *	unsigned int	value		Register value.
 *
 * OUTPUT:
 *	Write the given value to the specified PHY register.
 *
 * RETURN:
 *	false if the PHY is busy.
 *	true otherwise.
 *
 */
static void eth_port_write_smi_reg(unsigned int eth_port_num,
				   unsigned int phy_reg, unsigned int value)
{
	int phy_addr;
	int i;
	unsigned long flags;

	phy_addr = ethernet_phy_get(eth_port_num);

	/* the SMI register is a shared resource */
	spin_lock_irqsave(&mv643xx_eth_phy_lock, flags);

	/* wait for the SMI register to become available */
	for (i = 0; mv_read(SMI_REG) & ETH_SMI_BUSY; i++) {
		if (i == PHY_WAIT_ITERATIONS) {
			printk("mv643xx PHY busy timeout, port %d\n",
								eth_port_num);
			goto out;
		}
		udelay(PHY_WAIT_MICRO_SECONDS);
	}

	mv_write(SMI_REG, (phy_addr << 16) | (phy_reg << 21) |
				ETH_SMI_OPCODE_WRITE | (value & 0xffff));
out:
	spin_unlock_irqrestore(&mv643xx_eth_phy_lock, flags);
}

/*
 * Wrappers for MII support library.
 */
static int mv643xx_mdio_read(struct net_device *dev, int phy_id, int location)
{
	int val;
	struct mv643xx_private *mp = netdev_priv(dev);

	eth_port_read_smi_reg(mp->port_num, location, &val);
	return val;
}

static void mv643xx_mdio_write(struct net_device *dev, int phy_id, int location, int val)
{
	struct mv643xx_private *mp = netdev_priv(dev);
	eth_port_write_smi_reg(mp->port_num, location, val);
}

/*
 * eth_port_receive - Get received information from Rx ring.
 *
 * DESCRIPTION:
 * 	This routine returns the received data to the caller. There is no
 *	data copying during routine operation. All information is returned
 *	using pointer to packet information struct passed from the caller.
 *	If the routine exhausts Rx ring resources then the resource error flag
 *	is set.
 *
 * INPUT:
 *	struct mv643xx_private	*mp		Ethernet Port Control srtuct.
 *	struct pkt_info		*p_pkt_info	User packet buffer.
 *
 * OUTPUT:
 *	Rx ring current and used indexes are updated.
 *
 * RETURN:
 *	ETH_ERROR in case the routine can not access Rx desc ring.
 *	ETH_QUEUE_FULL if Rx ring resources are exhausted.
 *	ETH_END_OF_JOB if there is no received data.
 *	ETH_OK otherwise.
 */
static ETH_FUNC_RET_STATUS eth_port_receive(struct mv643xx_private *mp,
						struct pkt_info *p_pkt_info)
{
	int rx_next_curr_desc, rx_curr_desc, rx_used_desc;
	volatile struct eth_rx_desc *p_rx_desc;
	unsigned int command_status;
	unsigned long flags;

	/* Do not process Rx ring in case of Rx ring resource error */
	if (mp->rx_resource_err)
		return ETH_QUEUE_FULL;

	spin_lock_irqsave(&mp->lock, flags);

	/* Get the Rx Desc ring 'curr and 'used' indexes */
	rx_curr_desc = mp->rx_curr_desc_q;
	rx_used_desc = mp->rx_used_desc_q;

	p_rx_desc = &mp->p_rx_desc_area[rx_curr_desc];

	/* The following parameters are used to save readings from memory */
	command_status = p_rx_desc->cmd_sts;
	rmb();

	/* Nothing to receive... */
	if (command_status & (ETH_BUFFER_OWNED_BY_DMA)) {
		spin_unlock_irqrestore(&mp->lock, flags);
		return ETH_END_OF_JOB;
	}

	p_pkt_info->byte_cnt = (p_rx_desc->byte_cnt) - RX_BUF_OFFSET;
	p_pkt_info->cmd_sts = command_status;
	p_pkt_info->buf_ptr = (p_rx_desc->buf_ptr) + RX_BUF_OFFSET;
	p_pkt_info->return_info = mp->rx_skb[rx_curr_desc];
	p_pkt_info->l4i_chk = p_rx_desc->buf_size;

	/*
	 * Clean the return info field to indicate that the
	 * packet has been moved to the upper layers
	 */
	mp->rx_skb[rx_curr_desc] = NULL;

	/* Update current index in data structure */
	rx_next_curr_desc = (rx_curr_desc + 1) % mp->rx_ring_size;
	mp->rx_curr_desc_q = rx_next_curr_desc;

	/* Rx descriptors exhausted. Set the Rx ring resource error flag */
	if (rx_next_curr_desc == rx_used_desc)
		mp->rx_resource_err = 1;

	spin_unlock_irqrestore(&mp->lock, flags);

	return ETH_OK;
}

/*
 * eth_rx_return_buff - Returns a Rx buffer back to the Rx ring.
 *
 * DESCRIPTION:
 *	This routine returns a Rx buffer back to the Rx ring. It retrieves the
 *	next 'used' descriptor and attached the returned buffer to it.
 *	In case the Rx ring was in "resource error" condition, where there are
 *	no available Rx resources, the function resets the resource error flag.
 *
 * INPUT:
 *	struct mv643xx_private	*mp		Ethernet Port Control srtuct.
 *	struct pkt_info		*p_pkt_info	Information on returned buffer.
 *
 * OUTPUT:
 *	New available Rx resource in Rx descriptor ring.
 *
 * RETURN:
 *	ETH_ERROR in case the routine can not access Rx desc ring.
 *	ETH_OK otherwise.
 */
static ETH_FUNC_RET_STATUS eth_rx_return_buff(struct mv643xx_private *mp,
						struct pkt_info *p_pkt_info)
{
	int used_rx_desc;	/* Where to return Rx resource */
	volatile struct eth_rx_desc *p_used_rx_desc;
	unsigned long flags;

	spin_lock_irqsave(&mp->lock, flags);

	/* Get 'used' Rx descriptor */
	used_rx_desc = mp->rx_used_desc_q;
	p_used_rx_desc = &mp->p_rx_desc_area[used_rx_desc];

	p_used_rx_desc->buf_ptr = p_pkt_info->buf_ptr;
	p_used_rx_desc->buf_size = p_pkt_info->byte_cnt;
	mp->rx_skb[used_rx_desc] = p_pkt_info->return_info;

	/* Flush the write pipe */

	/* Return the descriptor to DMA ownership */
	wmb();
	p_used_rx_desc->cmd_sts =
			ETH_BUFFER_OWNED_BY_DMA | ETH_RX_ENABLE_INTERRUPT;
	wmb();

	/* Move the used descriptor pointer to the next descriptor */
	mp->rx_used_desc_q = (used_rx_desc + 1) % mp->rx_ring_size;

	/* Any Rx return cancels the Rx resource error status */
	mp->rx_resource_err = 0;

	spin_unlock_irqrestore(&mp->lock, flags);

	return ETH_OK;
}

/************* Begin ethtool support *************************/

struct mv643xx_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define MV643XX_STAT(m) sizeof(((struct mv643xx_private *)0)->m), \
					offsetof(struct mv643xx_private, m)

static const struct mv643xx_stats mv643xx_gstrings_stats[] = {
	{ "rx_packets", MV643XX_STAT(stats.rx_packets) },
	{ "tx_packets", MV643XX_STAT(stats.tx_packets) },
	{ "rx_bytes", MV643XX_STAT(stats.rx_bytes) },
	{ "tx_bytes", MV643XX_STAT(stats.tx_bytes) },
	{ "rx_errors", MV643XX_STAT(stats.rx_errors) },
	{ "tx_errors", MV643XX_STAT(stats.tx_errors) },
	{ "rx_dropped", MV643XX_STAT(stats.rx_dropped) },
	{ "tx_dropped", MV643XX_STAT(stats.tx_dropped) },
	{ "good_octets_received", MV643XX_STAT(mib_counters.good_octets_received) },
	{ "bad_octets_received", MV643XX_STAT(mib_counters.bad_octets_received) },
	{ "internal_mac_transmit_err", MV643XX_STAT(mib_counters.internal_mac_transmit_err) },
	{ "good_frames_received", MV643XX_STAT(mib_counters.good_frames_received) },
	{ "bad_frames_received", MV643XX_STAT(mib_counters.bad_frames_received) },
	{ "broadcast_frames_received", MV643XX_STAT(mib_counters.broadcast_frames_received) },
	{ "multicast_frames_received", MV643XX_STAT(mib_counters.multicast_frames_received) },
	{ "frames_64_octets", MV643XX_STAT(mib_counters.frames_64_octets) },
	{ "frames_65_to_127_octets", MV643XX_STAT(mib_counters.frames_65_to_127_octets) },
	{ "frames_128_to_255_octets", MV643XX_STAT(mib_counters.frames_128_to_255_octets) },
	{ "frames_256_to_511_octets", MV643XX_STAT(mib_counters.frames_256_to_511_octets) },
	{ "frames_512_to_1023_octets", MV643XX_STAT(mib_counters.frames_512_to_1023_octets) },
	{ "frames_1024_to_max_octets", MV643XX_STAT(mib_counters.frames_1024_to_max_octets) },
	{ "good_octets_sent", MV643XX_STAT(mib_counters.good_octets_sent) },
	{ "good_frames_sent", MV643XX_STAT(mib_counters.good_frames_sent) },
	{ "excessive_collision", MV643XX_STAT(mib_counters.excessive_collision) },
	{ "multicast_frames_sent", MV643XX_STAT(mib_counters.multicast_frames_sent) },
	{ "broadcast_frames_sent", MV643XX_STAT(mib_counters.broadcast_frames_sent) },
	{ "unrec_mac_control_received", MV643XX_STAT(mib_counters.unrec_mac_control_received) },
	{ "fc_sent", MV643XX_STAT(mib_counters.fc_sent) },
	{ "good_fc_received", MV643XX_STAT(mib_counters.good_fc_received) },
	{ "bad_fc_received", MV643XX_STAT(mib_counters.bad_fc_received) },
	{ "undersize_received", MV643XX_STAT(mib_counters.undersize_received) },
	{ "fragments_received", MV643XX_STAT(mib_counters.fragments_received) },
	{ "oversize_received", MV643XX_STAT(mib_counters.oversize_received) },
	{ "jabber_received", MV643XX_STAT(mib_counters.jabber_received) },
	{ "mac_receive_error", MV643XX_STAT(mib_counters.mac_receive_error) },
	{ "bad_crc_event", MV643XX_STAT(mib_counters.bad_crc_event) },
	{ "collision", MV643XX_STAT(mib_counters.collision) },
	{ "late_collision", MV643XX_STAT(mib_counters.late_collision) },
};

#define MV643XX_STATS_LEN	ARRAY_SIZE(mv643xx_gstrings_stats)

static void mv643xx_get_drvinfo(struct net_device *netdev,
				struct ethtool_drvinfo *drvinfo)
{
	strncpy(drvinfo->driver,  mv643xx_driver_name, 32);
	strncpy(drvinfo->version, mv643xx_driver_version, 32);
	strncpy(drvinfo->fw_version, "N/A", 32);
	strncpy(drvinfo->bus_info, "mv643xx", 32);
	drvinfo->n_stats = MV643XX_STATS_LEN;
}

static int mv643xx_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return MV643XX_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void mv643xx_get_ethtool_stats(struct net_device *netdev,
				struct ethtool_stats *stats, uint64_t *data)
{
	struct mv643xx_private *mp = netdev->priv;
	int i;

	eth_update_mib_counters(mp);

	for (i = 0; i < MV643XX_STATS_LEN; i++) {
		char *p = (char *)mp+mv643xx_gstrings_stats[i].stat_offset;
		data[i] = (mv643xx_gstrings_stats[i].sizeof_stat ==
			sizeof(uint64_t)) ? *(uint64_t *)p : *(uint32_t *)p;
	}
}

static void mv643xx_get_strings(struct net_device *netdev, uint32_t stringset,
				uint8_t *data)
{
	int i;

	switch(stringset) {
	case ETH_SS_STATS:
		for (i=0; i < MV643XX_STATS_LEN; i++) {
			memcpy(data + i * ETH_GSTRING_LEN,
					mv643xx_gstrings_stats[i].stat_string,
					ETH_GSTRING_LEN);
		}
		break;
	}
}

static u32 mv643xx_eth_get_link(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);

	return mii_link_ok(&mp->mii);
}

static int mv643xx_eth_nway_restart(struct net_device *dev)
{
	struct mv643xx_private *mp = netdev_priv(dev);

	return mii_nway_restart(&mp->mii);
}

static int mv643xx_eth_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mv643xx_private *mp = netdev_priv(dev);

	return generic_mii_ioctl(&mp->mii, if_mii(ifr), cmd, NULL);
}

static const struct ethtool_ops mv643xx_ethtool_ops = {
	.get_settings           = mv643xx_get_settings,
	.set_settings           = mv643xx_set_settings,
	.get_drvinfo            = mv643xx_get_drvinfo,
	.get_link               = mv643xx_eth_get_link,
	.set_sg			= ethtool_op_set_sg,
	.get_sset_count		= mv643xx_get_sset_count,
	.get_ethtool_stats      = mv643xx_get_ethtool_stats,
	.get_strings            = mv643xx_get_strings,
	.nway_reset		= mv643xx_eth_nway_restart,
};

/************* End ethtool support *************************/
