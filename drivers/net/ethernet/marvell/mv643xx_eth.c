/*
 * Driver for Marvell Discovery (MV643XX) and Marvell Orion ethernet ports
 * Copyright (C) 2002 Matthew Dharm <mdharm@momenco.com>
 *
 * Based on the 64360 driver from:
 * Copyright (C) 2002 Rabeeh Khoury <rabeeh@galileo.co.il>
 *		      Rabeeh Khoury <rabeeh@marvell.com>
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
 * Copyright (C) 2007-2008 Marvell Semiconductor
 *			   Lennert Buytenhek <buytenh@marvell.com>
 *
 * Copyright (C) 2013 Michael Stapelberg <michael@stapelberg.de>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/phy.h>
#include <linux/mv643xx_eth.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>

static char mv643xx_eth_driver_name[] = "mv643xx_eth";
static char mv643xx_eth_driver_version[] = "1.4";


/*
 * Registers shared between all ports.
 */
#define PHY_ADDR			0x0000
#define WINDOW_BASE(w)			(0x0200 + ((w) << 3))
#define WINDOW_SIZE(w)			(0x0204 + ((w) << 3))
#define WINDOW_REMAP_HIGH(w)		(0x0280 + ((w) << 2))
#define WINDOW_BAR_ENABLE		0x0290
#define WINDOW_PROTECT(w)		(0x0294 + ((w) << 4))

/*
 * Main per-port registers.  These live at offset 0x0400 for
 * port #0, 0x0800 for port #1, and 0x0c00 for port #2.
 */
#define PORT_CONFIG			0x0000
#define  UNICAST_PROMISCUOUS_MODE	0x00000001
#define PORT_CONFIG_EXT			0x0004
#define MAC_ADDR_LOW			0x0014
#define MAC_ADDR_HIGH			0x0018
#define SDMA_CONFIG			0x001c
#define  TX_BURST_SIZE_16_64BIT		0x01000000
#define  TX_BURST_SIZE_4_64BIT		0x00800000
#define  BLM_TX_NO_SWAP			0x00000020
#define  BLM_RX_NO_SWAP			0x00000010
#define  RX_BURST_SIZE_16_64BIT		0x00000008
#define  RX_BURST_SIZE_4_64BIT		0x00000004
#define PORT_SERIAL_CONTROL		0x003c
#define  SET_MII_SPEED_TO_100		0x01000000
#define  SET_GMII_SPEED_TO_1000		0x00800000
#define  SET_FULL_DUPLEX_MODE		0x00200000
#define  MAX_RX_PACKET_9700BYTE		0x000a0000
#define  DISABLE_AUTO_NEG_SPEED_GMII	0x00002000
#define  DO_NOT_FORCE_LINK_FAIL		0x00000400
#define  SERIAL_PORT_CONTROL_RESERVED	0x00000200
#define  DISABLE_AUTO_NEG_FOR_FLOW_CTRL	0x00000008
#define  DISABLE_AUTO_NEG_FOR_DUPLEX	0x00000004
#define  FORCE_LINK_PASS		0x00000002
#define  SERIAL_PORT_ENABLE		0x00000001
#define PORT_STATUS			0x0044
#define  TX_FIFO_EMPTY			0x00000400
#define  TX_IN_PROGRESS			0x00000080
#define  PORT_SPEED_MASK		0x00000030
#define  PORT_SPEED_1000		0x00000010
#define  PORT_SPEED_100			0x00000020
#define  PORT_SPEED_10			0x00000000
#define  FLOW_CONTROL_ENABLED		0x00000008
#define  FULL_DUPLEX			0x00000004
#define  LINK_UP			0x00000002
#define TXQ_COMMAND			0x0048
#define TXQ_FIX_PRIO_CONF		0x004c
#define PORT_SERIAL_CONTROL1		0x004c
#define  CLK125_BYPASS_EN		0x00000010
#define TX_BW_RATE			0x0050
#define TX_BW_MTU			0x0058
#define TX_BW_BURST			0x005c
#define INT_CAUSE			0x0060
#define  INT_TX_END			0x07f80000
#define  INT_TX_END_0			0x00080000
#define  INT_RX				0x000003fc
#define  INT_RX_0			0x00000004
#define  INT_EXT			0x00000002
#define INT_CAUSE_EXT			0x0064
#define  INT_EXT_LINK_PHY		0x00110000
#define  INT_EXT_TX			0x000000ff
#define INT_MASK			0x0068
#define INT_MASK_EXT			0x006c
#define TX_FIFO_URGENT_THRESHOLD	0x0074
#define RX_DISCARD_FRAME_CNT		0x0084
#define RX_OVERRUN_FRAME_CNT		0x0088
#define TXQ_FIX_PRIO_CONF_MOVED		0x00dc
#define TX_BW_RATE_MOVED		0x00e0
#define TX_BW_MTU_MOVED			0x00e8
#define TX_BW_BURST_MOVED		0x00ec
#define RXQ_CURRENT_DESC_PTR(q)		(0x020c + ((q) << 4))
#define RXQ_COMMAND			0x0280
#define TXQ_CURRENT_DESC_PTR(q)		(0x02c0 + ((q) << 2))
#define TXQ_BW_TOKENS(q)		(0x0300 + ((q) << 4))
#define TXQ_BW_CONF(q)			(0x0304 + ((q) << 4))
#define TXQ_BW_WRR_CONF(q)		(0x0308 + ((q) << 4))

/*
 * Misc per-port registers.
 */
#define MIB_COUNTERS(p)			(0x1000 + ((p) << 7))
#define SPECIAL_MCAST_TABLE(p)		(0x1400 + ((p) << 10))
#define OTHER_MCAST_TABLE(p)		(0x1500 + ((p) << 10))
#define UNICAST_TABLE(p)		(0x1600 + ((p) << 10))


/*
 * SDMA configuration register default value.
 */
#if defined(__BIG_ENDIAN)
#define PORT_SDMA_CONFIG_DEFAULT_VALUE		\
		(RX_BURST_SIZE_4_64BIT	|	\
		 TX_BURST_SIZE_4_64BIT)
#elif defined(__LITTLE_ENDIAN)
#define PORT_SDMA_CONFIG_DEFAULT_VALUE		\
		(RX_BURST_SIZE_4_64BIT	|	\
		 BLM_RX_NO_SWAP		|	\
		 BLM_TX_NO_SWAP		|	\
		 TX_BURST_SIZE_4_64BIT)
#else
#error One of __BIG_ENDIAN or __LITTLE_ENDIAN must be defined
#endif


/*
 * Misc definitions.
 */
#define DEFAULT_RX_QUEUE_SIZE	128
#define DEFAULT_TX_QUEUE_SIZE	256
#define SKB_DMA_REALIGN		((PAGE_SIZE - NET_SKB_PAD) % SMP_CACHE_BYTES)


/*
 * RX/TX descriptors.
 */
#if defined(__BIG_ENDIAN)
struct rx_desc {
	u16 byte_cnt;		/* Descriptor buffer byte count		*/
	u16 buf_size;		/* Buffer size				*/
	u32 cmd_sts;		/* Descriptor command status		*/
	u32 next_desc_ptr;	/* Next descriptor pointer		*/
	u32 buf_ptr;		/* Descriptor buffer pointer		*/
};

struct tx_desc {
	u16 byte_cnt;		/* buffer byte count			*/
	u16 l4i_chk;		/* CPU provided TCP checksum		*/
	u32 cmd_sts;		/* Command/status field			*/
	u32 next_desc_ptr;	/* Pointer to next descriptor		*/
	u32 buf_ptr;		/* pointer to buffer for this descriptor*/
};
#elif defined(__LITTLE_ENDIAN)
struct rx_desc {
	u32 cmd_sts;		/* Descriptor command status		*/
	u16 buf_size;		/* Buffer size				*/
	u16 byte_cnt;		/* Descriptor buffer byte count		*/
	u32 buf_ptr;		/* Descriptor buffer pointer		*/
	u32 next_desc_ptr;	/* Next descriptor pointer		*/
};

struct tx_desc {
	u32 cmd_sts;		/* Command/status field			*/
	u16 l4i_chk;		/* CPU provided TCP checksum		*/
	u16 byte_cnt;		/* buffer byte count			*/
	u32 buf_ptr;		/* pointer to buffer for this descriptor*/
	u32 next_desc_ptr;	/* Pointer to next descriptor		*/
};
#else
#error One of __BIG_ENDIAN or __LITTLE_ENDIAN must be defined
#endif

/* RX & TX descriptor command */
#define BUFFER_OWNED_BY_DMA		0x80000000

/* RX & TX descriptor status */
#define ERROR_SUMMARY			0x00000001

/* RX descriptor status */
#define LAYER_4_CHECKSUM_OK		0x40000000
#define RX_ENABLE_INTERRUPT		0x20000000
#define RX_FIRST_DESC			0x08000000
#define RX_LAST_DESC			0x04000000
#define RX_IP_HDR_OK			0x02000000
#define RX_PKT_IS_IPV4			0x01000000
#define RX_PKT_IS_ETHERNETV2		0x00800000
#define RX_PKT_LAYER4_TYPE_MASK		0x00600000
#define RX_PKT_LAYER4_TYPE_TCP_IPV4	0x00000000
#define RX_PKT_IS_VLAN_TAGGED		0x00080000

/* TX descriptor command */
#define TX_ENABLE_INTERRUPT		0x00800000
#define GEN_CRC				0x00400000
#define TX_FIRST_DESC			0x00200000
#define TX_LAST_DESC			0x00100000
#define ZERO_PADDING			0x00080000
#define GEN_IP_V4_CHECKSUM		0x00040000
#define GEN_TCP_UDP_CHECKSUM		0x00020000
#define UDP_FRAME			0x00010000
#define MAC_HDR_EXTRA_4_BYTES		0x00008000
#define MAC_HDR_EXTRA_8_BYTES		0x00000200

#define TX_IHL_SHIFT			11


/* global *******************************************************************/
struct mv643xx_eth_shared_private {
	/*
	 * Ethernet controller base address.
	 */
	void __iomem *base;

	/*
	 * Per-port MBUS window access register value.
	 */
	u32 win_protect;

	/*
	 * Hardware-specific parameters.
	 */
	int extended_rx_coal_limit;
	int tx_bw_control;
	int tx_csum_limit;
	struct clk *clk;
};

#define TX_BW_CONTROL_ABSENT		0
#define TX_BW_CONTROL_OLD_LAYOUT	1
#define TX_BW_CONTROL_NEW_LAYOUT	2

static int mv643xx_eth_open(struct net_device *dev);
static int mv643xx_eth_stop(struct net_device *dev);


/* per-port *****************************************************************/
struct mib_counters {
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
	/* Non MIB hardware counters */
	u32 rx_discard;
	u32 rx_overrun;
};

struct rx_queue {
	int index;

	int rx_ring_size;

	int rx_desc_count;
	int rx_curr_desc;
	int rx_used_desc;

	struct rx_desc *rx_desc_area;
	dma_addr_t rx_desc_dma;
	int rx_desc_area_size;
	struct sk_buff **rx_skb;
};

struct tx_queue {
	int index;

	int tx_ring_size;

	int tx_desc_count;
	int tx_curr_desc;
	int tx_used_desc;

	struct tx_desc *tx_desc_area;
	dma_addr_t tx_desc_dma;
	int tx_desc_area_size;

	struct sk_buff_head tx_skb;

	unsigned long tx_packets;
	unsigned long tx_bytes;
	unsigned long tx_dropped;
};

struct mv643xx_eth_private {
	struct mv643xx_eth_shared_private *shared;
	void __iomem *base;
	int port_num;

	struct net_device *dev;

	struct phy_device *phy;

	struct timer_list mib_counters_timer;
	spinlock_t mib_counters_lock;
	struct mib_counters mib_counters;

	struct work_struct tx_timeout_task;

	struct napi_struct napi;
	u32 int_mask;
	u8 oom;
	u8 work_link;
	u8 work_tx;
	u8 work_tx_end;
	u8 work_rx;
	u8 work_rx_refill;

	int skb_size;

	/*
	 * RX state.
	 */
	int rx_ring_size;
	unsigned long rx_desc_sram_addr;
	int rx_desc_sram_size;
	int rxq_count;
	struct timer_list rx_oom;
	struct rx_queue rxq[8];

	/*
	 * TX state.
	 */
	int tx_ring_size;
	unsigned long tx_desc_sram_addr;
	int tx_desc_sram_size;
	int txq_count;
	struct tx_queue txq[8];

	/*
	 * Hardware-specific parameters.
	 */
	struct clk *clk;
	unsigned int t_clk;
};


/* port register accessors **************************************************/
static inline u32 rdl(struct mv643xx_eth_private *mp, int offset)
{
	return readl(mp->shared->base + offset);
}

static inline u32 rdlp(struct mv643xx_eth_private *mp, int offset)
{
	return readl(mp->base + offset);
}

static inline void wrl(struct mv643xx_eth_private *mp, int offset, u32 data)
{
	writel(data, mp->shared->base + offset);
}

static inline void wrlp(struct mv643xx_eth_private *mp, int offset, u32 data)
{
	writel(data, mp->base + offset);
}


/* rxq/txq helper functions *************************************************/
static struct mv643xx_eth_private *rxq_to_mp(struct rx_queue *rxq)
{
	return container_of(rxq, struct mv643xx_eth_private, rxq[rxq->index]);
}

static struct mv643xx_eth_private *txq_to_mp(struct tx_queue *txq)
{
	return container_of(txq, struct mv643xx_eth_private, txq[txq->index]);
}

static void rxq_enable(struct rx_queue *rxq)
{
	struct mv643xx_eth_private *mp = rxq_to_mp(rxq);
	wrlp(mp, RXQ_COMMAND, 1 << rxq->index);
}

static void rxq_disable(struct rx_queue *rxq)
{
	struct mv643xx_eth_private *mp = rxq_to_mp(rxq);
	u8 mask = 1 << rxq->index;

	wrlp(mp, RXQ_COMMAND, mask << 8);
	while (rdlp(mp, RXQ_COMMAND) & mask)
		udelay(10);
}

static void txq_reset_hw_ptr(struct tx_queue *txq)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	u32 addr;

	addr = (u32)txq->tx_desc_dma;
	addr += txq->tx_curr_desc * sizeof(struct tx_desc);
	wrlp(mp, TXQ_CURRENT_DESC_PTR(txq->index), addr);
}

static void txq_enable(struct tx_queue *txq)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	wrlp(mp, TXQ_COMMAND, 1 << txq->index);
}

static void txq_disable(struct tx_queue *txq)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	u8 mask = 1 << txq->index;

	wrlp(mp, TXQ_COMMAND, mask << 8);
	while (rdlp(mp, TXQ_COMMAND) & mask)
		udelay(10);
}

static void txq_maybe_wake(struct tx_queue *txq)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	struct netdev_queue *nq = netdev_get_tx_queue(mp->dev, txq->index);

	if (netif_tx_queue_stopped(nq)) {
		__netif_tx_lock(nq, smp_processor_id());
		if (txq->tx_ring_size - txq->tx_desc_count >= MAX_SKB_FRAGS + 1)
			netif_tx_wake_queue(nq);
		__netif_tx_unlock(nq);
	}
}

static int rxq_process(struct rx_queue *rxq, int budget)
{
	struct mv643xx_eth_private *mp = rxq_to_mp(rxq);
	struct net_device_stats *stats = &mp->dev->stats;
	int rx;

	rx = 0;
	while (rx < budget && rxq->rx_desc_count) {
		struct rx_desc *rx_desc;
		unsigned int cmd_sts;
		struct sk_buff *skb;
		u16 byte_cnt;

		rx_desc = &rxq->rx_desc_area[rxq->rx_curr_desc];

		cmd_sts = rx_desc->cmd_sts;
		if (cmd_sts & BUFFER_OWNED_BY_DMA)
			break;
		rmb();

		skb = rxq->rx_skb[rxq->rx_curr_desc];
		rxq->rx_skb[rxq->rx_curr_desc] = NULL;

		rxq->rx_curr_desc++;
		if (rxq->rx_curr_desc == rxq->rx_ring_size)
			rxq->rx_curr_desc = 0;

		dma_unmap_single(mp->dev->dev.parent, rx_desc->buf_ptr,
				 rx_desc->buf_size, DMA_FROM_DEVICE);
		rxq->rx_desc_count--;
		rx++;

		mp->work_rx_refill |= 1 << rxq->index;

		byte_cnt = rx_desc->byte_cnt;

		/*
		 * Update statistics.
		 *
		 * Note that the descriptor byte count includes 2 dummy
		 * bytes automatically inserted by the hardware at the
		 * start of the packet (which we don't count), and a 4
		 * byte CRC at the end of the packet (which we do count).
		 */
		stats->rx_packets++;
		stats->rx_bytes += byte_cnt - 2;

		/*
		 * In case we received a packet without first / last bits
		 * on, or the error summary bit is set, the packet needs
		 * to be dropped.
		 */
		if ((cmd_sts & (RX_FIRST_DESC | RX_LAST_DESC | ERROR_SUMMARY))
			!= (RX_FIRST_DESC | RX_LAST_DESC))
			goto err;

		/*
		 * The -4 is for the CRC in the trailer of the
		 * received packet
		 */
		skb_put(skb, byte_cnt - 2 - 4);

		if (cmd_sts & LAYER_4_CHECKSUM_OK)
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		skb->protocol = eth_type_trans(skb, mp->dev);

		napi_gro_receive(&mp->napi, skb);

		continue;

err:
		stats->rx_dropped++;

		if ((cmd_sts & (RX_FIRST_DESC | RX_LAST_DESC)) !=
			(RX_FIRST_DESC | RX_LAST_DESC)) {
			if (net_ratelimit())
				netdev_err(mp->dev,
					   "received packet spanning multiple descriptors\n");
		}

		if (cmd_sts & ERROR_SUMMARY)
			stats->rx_errors++;

		dev_kfree_skb(skb);
	}

	if (rx < budget)
		mp->work_rx &= ~(1 << rxq->index);

	return rx;
}

static int rxq_refill(struct rx_queue *rxq, int budget)
{
	struct mv643xx_eth_private *mp = rxq_to_mp(rxq);
	int refilled;

	refilled = 0;
	while (refilled < budget && rxq->rx_desc_count < rxq->rx_ring_size) {
		struct sk_buff *skb;
		int rx;
		struct rx_desc *rx_desc;
		int size;

		skb = netdev_alloc_skb(mp->dev, mp->skb_size);

		if (skb == NULL) {
			mp->oom = 1;
			goto oom;
		}

		if (SKB_DMA_REALIGN)
			skb_reserve(skb, SKB_DMA_REALIGN);

		refilled++;
		rxq->rx_desc_count++;

		rx = rxq->rx_used_desc++;
		if (rxq->rx_used_desc == rxq->rx_ring_size)
			rxq->rx_used_desc = 0;

		rx_desc = rxq->rx_desc_area + rx;

		size = skb_end_pointer(skb) - skb->data;
		rx_desc->buf_ptr = dma_map_single(mp->dev->dev.parent,
						  skb->data, size,
						  DMA_FROM_DEVICE);
		rx_desc->buf_size = size;
		rxq->rx_skb[rx] = skb;
		wmb();
		rx_desc->cmd_sts = BUFFER_OWNED_BY_DMA | RX_ENABLE_INTERRUPT;
		wmb();

		/*
		 * The hardware automatically prepends 2 bytes of
		 * dummy data to each received packet, so that the
		 * IP header ends up 16-byte aligned.
		 */
		skb_reserve(skb, 2);
	}

	if (refilled < budget)
		mp->work_rx_refill &= ~(1 << rxq->index);

oom:
	return refilled;
}


/* tx ***********************************************************************/
static inline unsigned int has_tiny_unaligned_frags(struct sk_buff *skb)
{
	int frag;

	for (frag = 0; frag < skb_shinfo(skb)->nr_frags; frag++) {
		const skb_frag_t *fragp = &skb_shinfo(skb)->frags[frag];

		if (skb_frag_size(fragp) <= 8 && fragp->page_offset & 7)
			return 1;
	}

	return 0;
}

static void txq_submit_frag_skb(struct tx_queue *txq, struct sk_buff *skb)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	int nr_frags = skb_shinfo(skb)->nr_frags;
	int frag;

	for (frag = 0; frag < nr_frags; frag++) {
		skb_frag_t *this_frag;
		int tx_index;
		struct tx_desc *desc;

		this_frag = &skb_shinfo(skb)->frags[frag];
		tx_index = txq->tx_curr_desc++;
		if (txq->tx_curr_desc == txq->tx_ring_size)
			txq->tx_curr_desc = 0;
		desc = &txq->tx_desc_area[tx_index];

		/*
		 * The last fragment will generate an interrupt
		 * which will free the skb on TX completion.
		 */
		if (frag == nr_frags - 1) {
			desc->cmd_sts = BUFFER_OWNED_BY_DMA |
					ZERO_PADDING | TX_LAST_DESC |
					TX_ENABLE_INTERRUPT;
		} else {
			desc->cmd_sts = BUFFER_OWNED_BY_DMA;
		}

		desc->l4i_chk = 0;
		desc->byte_cnt = skb_frag_size(this_frag);
		desc->buf_ptr = skb_frag_dma_map(mp->dev->dev.parent,
						 this_frag, 0,
						 skb_frag_size(this_frag),
						 DMA_TO_DEVICE);
	}
}

static inline __be16 sum16_as_be(__sum16 sum)
{
	return (__force __be16)sum;
}

static int txq_submit_skb(struct tx_queue *txq, struct sk_buff *skb)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	int nr_frags = skb_shinfo(skb)->nr_frags;
	int tx_index;
	struct tx_desc *desc;
	u32 cmd_sts;
	u16 l4i_chk;
	int length;

	cmd_sts = TX_FIRST_DESC | GEN_CRC | BUFFER_OWNED_BY_DMA;
	l4i_chk = 0;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		int hdr_len;
		int tag_bytes;

		BUG_ON(skb->protocol != htons(ETH_P_IP) &&
		       skb->protocol != htons(ETH_P_8021Q));

		hdr_len = (void *)ip_hdr(skb) - (void *)skb->data;
		tag_bytes = hdr_len - ETH_HLEN;
		if (skb->len - hdr_len > mp->shared->tx_csum_limit ||
		    unlikely(tag_bytes & ~12)) {
			if (skb_checksum_help(skb) == 0)
				goto no_csum;
			kfree_skb(skb);
			return 1;
		}

		if (tag_bytes & 4)
			cmd_sts |= MAC_HDR_EXTRA_4_BYTES;
		if (tag_bytes & 8)
			cmd_sts |= MAC_HDR_EXTRA_8_BYTES;

		cmd_sts |= GEN_TCP_UDP_CHECKSUM |
			   GEN_IP_V4_CHECKSUM   |
			   ip_hdr(skb)->ihl << TX_IHL_SHIFT;

		switch (ip_hdr(skb)->protocol) {
		case IPPROTO_UDP:
			cmd_sts |= UDP_FRAME;
			l4i_chk = ntohs(sum16_as_be(udp_hdr(skb)->check));
			break;
		case IPPROTO_TCP:
			l4i_chk = ntohs(sum16_as_be(tcp_hdr(skb)->check));
			break;
		default:
			BUG();
		}
	} else {
no_csum:
		/* Errata BTS #50, IHL must be 5 if no HW checksum */
		cmd_sts |= 5 << TX_IHL_SHIFT;
	}

	tx_index = txq->tx_curr_desc++;
	if (txq->tx_curr_desc == txq->tx_ring_size)
		txq->tx_curr_desc = 0;
	desc = &txq->tx_desc_area[tx_index];

	if (nr_frags) {
		txq_submit_frag_skb(txq, skb);
		length = skb_headlen(skb);
	} else {
		cmd_sts |= ZERO_PADDING | TX_LAST_DESC | TX_ENABLE_INTERRUPT;
		length = skb->len;
	}

	desc->l4i_chk = l4i_chk;
	desc->byte_cnt = length;
	desc->buf_ptr = dma_map_single(mp->dev->dev.parent, skb->data,
				       length, DMA_TO_DEVICE);

	__skb_queue_tail(&txq->tx_skb, skb);

	skb_tx_timestamp(skb);

	/* ensure all other descriptors are written before first cmd_sts */
	wmb();
	desc->cmd_sts = cmd_sts;

	/* clear TX_END status */
	mp->work_tx_end &= ~(1 << txq->index);

	/* ensure all descriptors are written before poking hardware */
	wmb();
	txq_enable(txq);

	txq->tx_desc_count += nr_frags + 1;

	return 0;
}

static netdev_tx_t mv643xx_eth_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int length, queue;
	struct tx_queue *txq;
	struct netdev_queue *nq;

	queue = skb_get_queue_mapping(skb);
	txq = mp->txq + queue;
	nq = netdev_get_tx_queue(dev, queue);

	if (has_tiny_unaligned_frags(skb) && __skb_linearize(skb)) {
		txq->tx_dropped++;
		netdev_printk(KERN_DEBUG, dev,
			      "failed to linearize skb with tiny unaligned fragment\n");
		return NETDEV_TX_BUSY;
	}

	if (txq->tx_ring_size - txq->tx_desc_count < MAX_SKB_FRAGS + 1) {
		if (net_ratelimit())
			netdev_err(dev, "tx queue full?!\n");
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	length = skb->len;

	if (!txq_submit_skb(txq, skb)) {
		int entries_left;

		txq->tx_bytes += length;
		txq->tx_packets++;

		entries_left = txq->tx_ring_size - txq->tx_desc_count;
		if (entries_left < MAX_SKB_FRAGS + 1)
			netif_tx_stop_queue(nq);
	}

	return NETDEV_TX_OK;
}


/* tx napi ******************************************************************/
static void txq_kick(struct tx_queue *txq)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	struct netdev_queue *nq = netdev_get_tx_queue(mp->dev, txq->index);
	u32 hw_desc_ptr;
	u32 expected_ptr;

	__netif_tx_lock(nq, smp_processor_id());

	if (rdlp(mp, TXQ_COMMAND) & (1 << txq->index))
		goto out;

	hw_desc_ptr = rdlp(mp, TXQ_CURRENT_DESC_PTR(txq->index));
	expected_ptr = (u32)txq->tx_desc_dma +
				txq->tx_curr_desc * sizeof(struct tx_desc);

	if (hw_desc_ptr != expected_ptr)
		txq_enable(txq);

out:
	__netif_tx_unlock(nq);

	mp->work_tx_end &= ~(1 << txq->index);
}

static int txq_reclaim(struct tx_queue *txq, int budget, int force)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	struct netdev_queue *nq = netdev_get_tx_queue(mp->dev, txq->index);
	int reclaimed;

	__netif_tx_lock_bh(nq);

	reclaimed = 0;
	while (reclaimed < budget && txq->tx_desc_count > 0) {
		int tx_index;
		struct tx_desc *desc;
		u32 cmd_sts;
		struct sk_buff *skb;

		tx_index = txq->tx_used_desc;
		desc = &txq->tx_desc_area[tx_index];
		cmd_sts = desc->cmd_sts;

		if (cmd_sts & BUFFER_OWNED_BY_DMA) {
			if (!force)
				break;
			desc->cmd_sts = cmd_sts & ~BUFFER_OWNED_BY_DMA;
		}

		txq->tx_used_desc = tx_index + 1;
		if (txq->tx_used_desc == txq->tx_ring_size)
			txq->tx_used_desc = 0;

		reclaimed++;
		txq->tx_desc_count--;

		skb = NULL;
		if (cmd_sts & TX_LAST_DESC)
			skb = __skb_dequeue(&txq->tx_skb);

		if (cmd_sts & ERROR_SUMMARY) {
			netdev_info(mp->dev, "tx error\n");
			mp->dev->stats.tx_errors++;
		}

		if (cmd_sts & TX_FIRST_DESC) {
			dma_unmap_single(mp->dev->dev.parent, desc->buf_ptr,
					 desc->byte_cnt, DMA_TO_DEVICE);
		} else {
			dma_unmap_page(mp->dev->dev.parent, desc->buf_ptr,
				       desc->byte_cnt, DMA_TO_DEVICE);
		}

		dev_kfree_skb(skb);
	}

	__netif_tx_unlock_bh(nq);

	if (reclaimed < budget)
		mp->work_tx &= ~(1 << txq->index);

	return reclaimed;
}


/* tx rate control **********************************************************/
/*
 * Set total maximum TX rate (shared by all TX queues for this port)
 * to 'rate' bits per second, with a maximum burst of 'burst' bytes.
 */
static void tx_set_rate(struct mv643xx_eth_private *mp, int rate, int burst)
{
	int token_rate;
	int mtu;
	int bucket_size;

	token_rate = ((rate / 1000) * 64) / (mp->t_clk / 1000);
	if (token_rate > 1023)
		token_rate = 1023;

	mtu = (mp->dev->mtu + 255) >> 8;
	if (mtu > 63)
		mtu = 63;

	bucket_size = (burst + 255) >> 8;
	if (bucket_size > 65535)
		bucket_size = 65535;

	switch (mp->shared->tx_bw_control) {
	case TX_BW_CONTROL_OLD_LAYOUT:
		wrlp(mp, TX_BW_RATE, token_rate);
		wrlp(mp, TX_BW_MTU, mtu);
		wrlp(mp, TX_BW_BURST, bucket_size);
		break;
	case TX_BW_CONTROL_NEW_LAYOUT:
		wrlp(mp, TX_BW_RATE_MOVED, token_rate);
		wrlp(mp, TX_BW_MTU_MOVED, mtu);
		wrlp(mp, TX_BW_BURST_MOVED, bucket_size);
		break;
	}
}

static void txq_set_rate(struct tx_queue *txq, int rate, int burst)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	int token_rate;
	int bucket_size;

	token_rate = ((rate / 1000) * 64) / (mp->t_clk / 1000);
	if (token_rate > 1023)
		token_rate = 1023;

	bucket_size = (burst + 255) >> 8;
	if (bucket_size > 65535)
		bucket_size = 65535;

	wrlp(mp, TXQ_BW_TOKENS(txq->index), token_rate << 14);
	wrlp(mp, TXQ_BW_CONF(txq->index), (bucket_size << 10) | token_rate);
}

static void txq_set_fixed_prio_mode(struct tx_queue *txq)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	int off;
	u32 val;

	/*
	 * Turn on fixed priority mode.
	 */
	off = 0;
	switch (mp->shared->tx_bw_control) {
	case TX_BW_CONTROL_OLD_LAYOUT:
		off = TXQ_FIX_PRIO_CONF;
		break;
	case TX_BW_CONTROL_NEW_LAYOUT:
		off = TXQ_FIX_PRIO_CONF_MOVED;
		break;
	}

	if (off) {
		val = rdlp(mp, off);
		val |= 1 << txq->index;
		wrlp(mp, off, val);
	}
}


/* mii management interface *************************************************/
static void mv643xx_adjust_pscr(struct mv643xx_eth_private *mp)
{
	u32 pscr = rdlp(mp, PORT_SERIAL_CONTROL);
	u32 autoneg_disable = FORCE_LINK_PASS |
	             DISABLE_AUTO_NEG_SPEED_GMII |
		     DISABLE_AUTO_NEG_FOR_FLOW_CTRL |
		     DISABLE_AUTO_NEG_FOR_DUPLEX;

	if (mp->phy->autoneg == AUTONEG_ENABLE) {
		/* enable auto negotiation */
		pscr &= ~autoneg_disable;
		goto out_write;
	}

	pscr |= autoneg_disable;

	if (mp->phy->speed == SPEED_1000) {
		/* force gigabit, half duplex not supported */
		pscr |= SET_GMII_SPEED_TO_1000;
		pscr |= SET_FULL_DUPLEX_MODE;
		goto out_write;
	}

	pscr &= ~SET_GMII_SPEED_TO_1000;

	if (mp->phy->speed == SPEED_100)
		pscr |= SET_MII_SPEED_TO_100;
	else
		pscr &= ~SET_MII_SPEED_TO_100;

	if (mp->phy->duplex == DUPLEX_FULL)
		pscr |= SET_FULL_DUPLEX_MODE;
	else
		pscr &= ~SET_FULL_DUPLEX_MODE;

out_write:
	wrlp(mp, PORT_SERIAL_CONTROL, pscr);
}

/* statistics ***************************************************************/
static struct net_device_stats *mv643xx_eth_get_stats(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	unsigned long tx_packets = 0;
	unsigned long tx_bytes = 0;
	unsigned long tx_dropped = 0;
	int i;

	for (i = 0; i < mp->txq_count; i++) {
		struct tx_queue *txq = mp->txq + i;

		tx_packets += txq->tx_packets;
		tx_bytes += txq->tx_bytes;
		tx_dropped += txq->tx_dropped;
	}

	stats->tx_packets = tx_packets;
	stats->tx_bytes = tx_bytes;
	stats->tx_dropped = tx_dropped;

	return stats;
}

static inline u32 mib_read(struct mv643xx_eth_private *mp, int offset)
{
	return rdl(mp, MIB_COUNTERS(mp->port_num) + offset);
}

static void mib_counters_clear(struct mv643xx_eth_private *mp)
{
	int i;

	for (i = 0; i < 0x80; i += 4)
		mib_read(mp, i);

	/* Clear non MIB hw counters also */
	rdlp(mp, RX_DISCARD_FRAME_CNT);
	rdlp(mp, RX_OVERRUN_FRAME_CNT);
}

static void mib_counters_update(struct mv643xx_eth_private *mp)
{
	struct mib_counters *p = &mp->mib_counters;

	spin_lock_bh(&mp->mib_counters_lock);
	p->good_octets_received += mib_read(mp, 0x00);
	p->bad_octets_received += mib_read(mp, 0x08);
	p->internal_mac_transmit_err += mib_read(mp, 0x0c);
	p->good_frames_received += mib_read(mp, 0x10);
	p->bad_frames_received += mib_read(mp, 0x14);
	p->broadcast_frames_received += mib_read(mp, 0x18);
	p->multicast_frames_received += mib_read(mp, 0x1c);
	p->frames_64_octets += mib_read(mp, 0x20);
	p->frames_65_to_127_octets += mib_read(mp, 0x24);
	p->frames_128_to_255_octets += mib_read(mp, 0x28);
	p->frames_256_to_511_octets += mib_read(mp, 0x2c);
	p->frames_512_to_1023_octets += mib_read(mp, 0x30);
	p->frames_1024_to_max_octets += mib_read(mp, 0x34);
	p->good_octets_sent += mib_read(mp, 0x38);
	p->good_frames_sent += mib_read(mp, 0x40);
	p->excessive_collision += mib_read(mp, 0x44);
	p->multicast_frames_sent += mib_read(mp, 0x48);
	p->broadcast_frames_sent += mib_read(mp, 0x4c);
	p->unrec_mac_control_received += mib_read(mp, 0x50);
	p->fc_sent += mib_read(mp, 0x54);
	p->good_fc_received += mib_read(mp, 0x58);
	p->bad_fc_received += mib_read(mp, 0x5c);
	p->undersize_received += mib_read(mp, 0x60);
	p->fragments_received += mib_read(mp, 0x64);
	p->oversize_received += mib_read(mp, 0x68);
	p->jabber_received += mib_read(mp, 0x6c);
	p->mac_receive_error += mib_read(mp, 0x70);
	p->bad_crc_event += mib_read(mp, 0x74);
	p->collision += mib_read(mp, 0x78);
	p->late_collision += mib_read(mp, 0x7c);
	/* Non MIB hardware counters */
	p->rx_discard += rdlp(mp, RX_DISCARD_FRAME_CNT);
	p->rx_overrun += rdlp(mp, RX_OVERRUN_FRAME_CNT);
	spin_unlock_bh(&mp->mib_counters_lock);
}

static void mib_counters_timer_wrapper(unsigned long _mp)
{
	struct mv643xx_eth_private *mp = (void *)_mp;
	mib_counters_update(mp);
	mod_timer(&mp->mib_counters_timer, jiffies + 30 * HZ);
}


/* interrupt coalescing *****************************************************/
/*
 * Hardware coalescing parameters are set in units of 64 t_clk
 * cycles.  I.e.:
 *
 *	coal_delay_in_usec = 64000000 * register_value / t_clk_rate
 *
 *	register_value = coal_delay_in_usec * t_clk_rate / 64000000
 *
 * In the ->set*() methods, we round the computed register value
 * to the nearest integer.
 */
static unsigned int get_rx_coal(struct mv643xx_eth_private *mp)
{
	u32 val = rdlp(mp, SDMA_CONFIG);
	u64 temp;

	if (mp->shared->extended_rx_coal_limit)
		temp = ((val & 0x02000000) >> 10) | ((val & 0x003fff80) >> 7);
	else
		temp = (val & 0x003fff00) >> 8;

	temp *= 64000000;
	do_div(temp, mp->t_clk);

	return (unsigned int)temp;
}

static void set_rx_coal(struct mv643xx_eth_private *mp, unsigned int usec)
{
	u64 temp;
	u32 val;

	temp = (u64)usec * mp->t_clk;
	temp += 31999999;
	do_div(temp, 64000000);

	val = rdlp(mp, SDMA_CONFIG);
	if (mp->shared->extended_rx_coal_limit) {
		if (temp > 0xffff)
			temp = 0xffff;
		val &= ~0x023fff80;
		val |= (temp & 0x8000) << 10;
		val |= (temp & 0x7fff) << 7;
	} else {
		if (temp > 0x3fff)
			temp = 0x3fff;
		val &= ~0x003fff00;
		val |= (temp & 0x3fff) << 8;
	}
	wrlp(mp, SDMA_CONFIG, val);
}

static unsigned int get_tx_coal(struct mv643xx_eth_private *mp)
{
	u64 temp;

	temp = (rdlp(mp, TX_FIFO_URGENT_THRESHOLD) & 0x3fff0) >> 4;
	temp *= 64000000;
	do_div(temp, mp->t_clk);

	return (unsigned int)temp;
}

static void set_tx_coal(struct mv643xx_eth_private *mp, unsigned int usec)
{
	u64 temp;

	temp = (u64)usec * mp->t_clk;
	temp += 31999999;
	do_div(temp, 64000000);

	if (temp > 0x3fff)
		temp = 0x3fff;

	wrlp(mp, TX_FIFO_URGENT_THRESHOLD, temp << 4);
}


/* ethtool ******************************************************************/
struct mv643xx_eth_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int netdev_off;
	int mp_off;
};

#define SSTAT(m)						\
	{ #m, FIELD_SIZEOF(struct net_device_stats, m),		\
	  offsetof(struct net_device, stats.m), -1 }

#define MIBSTAT(m)						\
	{ #m, FIELD_SIZEOF(struct mib_counters, m),		\
	  -1, offsetof(struct mv643xx_eth_private, mib_counters.m) }

static const struct mv643xx_eth_stats mv643xx_eth_stats[] = {
	SSTAT(rx_packets),
	SSTAT(tx_packets),
	SSTAT(rx_bytes),
	SSTAT(tx_bytes),
	SSTAT(rx_errors),
	SSTAT(tx_errors),
	SSTAT(rx_dropped),
	SSTAT(tx_dropped),
	MIBSTAT(good_octets_received),
	MIBSTAT(bad_octets_received),
	MIBSTAT(internal_mac_transmit_err),
	MIBSTAT(good_frames_received),
	MIBSTAT(bad_frames_received),
	MIBSTAT(broadcast_frames_received),
	MIBSTAT(multicast_frames_received),
	MIBSTAT(frames_64_octets),
	MIBSTAT(frames_65_to_127_octets),
	MIBSTAT(frames_128_to_255_octets),
	MIBSTAT(frames_256_to_511_octets),
	MIBSTAT(frames_512_to_1023_octets),
	MIBSTAT(frames_1024_to_max_octets),
	MIBSTAT(good_octets_sent),
	MIBSTAT(good_frames_sent),
	MIBSTAT(excessive_collision),
	MIBSTAT(multicast_frames_sent),
	MIBSTAT(broadcast_frames_sent),
	MIBSTAT(unrec_mac_control_received),
	MIBSTAT(fc_sent),
	MIBSTAT(good_fc_received),
	MIBSTAT(bad_fc_received),
	MIBSTAT(undersize_received),
	MIBSTAT(fragments_received),
	MIBSTAT(oversize_received),
	MIBSTAT(jabber_received),
	MIBSTAT(mac_receive_error),
	MIBSTAT(bad_crc_event),
	MIBSTAT(collision),
	MIBSTAT(late_collision),
	MIBSTAT(rx_discard),
	MIBSTAT(rx_overrun),
};

static int
mv643xx_eth_get_settings_phy(struct mv643xx_eth_private *mp,
			     struct ethtool_cmd *cmd)
{
	int err;

	err = phy_read_status(mp->phy);
	if (err == 0)
		err = phy_ethtool_gset(mp->phy, cmd);

	/*
	 * The MAC does not support 1000baseT_Half.
	 */
	cmd->supported &= ~SUPPORTED_1000baseT_Half;
	cmd->advertising &= ~ADVERTISED_1000baseT_Half;

	return err;
}

static int
mv643xx_eth_get_settings_phyless(struct mv643xx_eth_private *mp,
				 struct ethtool_cmd *cmd)
{
	u32 port_status;

	port_status = rdlp(mp, PORT_STATUS);

	cmd->supported = SUPPORTED_MII;
	cmd->advertising = ADVERTISED_MII;
	switch (port_status & PORT_SPEED_MASK) {
	case PORT_SPEED_10:
		ethtool_cmd_speed_set(cmd, SPEED_10);
		break;
	case PORT_SPEED_100:
		ethtool_cmd_speed_set(cmd, SPEED_100);
		break;
	case PORT_SPEED_1000:
		ethtool_cmd_speed_set(cmd, SPEED_1000);
		break;
	default:
		cmd->speed = -1;
		break;
	}
	cmd->duplex = (port_status & FULL_DUPLEX) ? DUPLEX_FULL : DUPLEX_HALF;
	cmd->port = PORT_MII;
	cmd->phy_address = 0;
	cmd->transceiver = XCVR_INTERNAL;
	cmd->autoneg = AUTONEG_DISABLE;
	cmd->maxtxpkt = 1;
	cmd->maxrxpkt = 1;

	return 0;
}

static void
mv643xx_eth_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	wol->supported = 0;
	wol->wolopts = 0;
	if (mp->phy)
		phy_ethtool_get_wol(mp->phy, wol);
}

static int
mv643xx_eth_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int err;

	if (mp->phy == NULL)
		return -EOPNOTSUPP;

	err = phy_ethtool_set_wol(mp->phy, wol);
	/* Given that mv643xx_eth works without the marvell-specific PHY driver,
	 * this debugging hint is useful to have.
	 */
	if (err == -EOPNOTSUPP)
		netdev_info(dev, "The PHY does not support set_wol, was CONFIG_MARVELL_PHY enabled?\n");
	return err;
}

static int
mv643xx_eth_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	if (mp->phy != NULL)
		return mv643xx_eth_get_settings_phy(mp, cmd);
	else
		return mv643xx_eth_get_settings_phyless(mp, cmd);
}

static int
mv643xx_eth_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int ret;

	if (mp->phy == NULL)
		return -EINVAL;

	/*
	 * The MAC does not support 1000baseT_Half.
	 */
	cmd->advertising &= ~ADVERTISED_1000baseT_Half;

	ret = phy_ethtool_sset(mp->phy, cmd);
	if (!ret)
		mv643xx_adjust_pscr(mp);
	return ret;
}

static void mv643xx_eth_get_drvinfo(struct net_device *dev,
				    struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, mv643xx_eth_driver_name,
		sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, mv643xx_eth_driver_version,
		sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "N/A", sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, "platform", sizeof(drvinfo->bus_info));
	drvinfo->n_stats = ARRAY_SIZE(mv643xx_eth_stats);
}

static int mv643xx_eth_nway_reset(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	if (mp->phy == NULL)
		return -EINVAL;

	return genphy_restart_aneg(mp->phy);
}

static int
mv643xx_eth_get_coalesce(struct net_device *dev, struct ethtool_coalesce *ec)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	ec->rx_coalesce_usecs = get_rx_coal(mp);
	ec->tx_coalesce_usecs = get_tx_coal(mp);

	return 0;
}

static int
mv643xx_eth_set_coalesce(struct net_device *dev, struct ethtool_coalesce *ec)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	set_rx_coal(mp, ec->rx_coalesce_usecs);
	set_tx_coal(mp, ec->tx_coalesce_usecs);

	return 0;
}

static void
mv643xx_eth_get_ringparam(struct net_device *dev, struct ethtool_ringparam *er)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	er->rx_max_pending = 4096;
	er->tx_max_pending = 4096;

	er->rx_pending = mp->rx_ring_size;
	er->tx_pending = mp->tx_ring_size;
}

static int
mv643xx_eth_set_ringparam(struct net_device *dev, struct ethtool_ringparam *er)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	if (er->rx_mini_pending || er->rx_jumbo_pending)
		return -EINVAL;

	mp->rx_ring_size = er->rx_pending < 4096 ? er->rx_pending : 4096;
	mp->tx_ring_size = er->tx_pending < 4096 ? er->tx_pending : 4096;

	if (netif_running(dev)) {
		mv643xx_eth_stop(dev);
		if (mv643xx_eth_open(dev)) {
			netdev_err(dev,
				   "fatal error on re-opening device after ring param change\n");
			return -ENOMEM;
		}
	}

	return 0;
}


static int
mv643xx_eth_set_features(struct net_device *dev, netdev_features_t features)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	bool rx_csum = features & NETIF_F_RXCSUM;

	wrlp(mp, PORT_CONFIG, rx_csum ? 0x02000000 : 0x00000000);

	return 0;
}

static void mv643xx_eth_get_strings(struct net_device *dev,
				    uint32_t stringset, uint8_t *data)
{
	int i;

	if (stringset == ETH_SS_STATS) {
		for (i = 0; i < ARRAY_SIZE(mv643xx_eth_stats); i++) {
			memcpy(data + i * ETH_GSTRING_LEN,
				mv643xx_eth_stats[i].stat_string,
				ETH_GSTRING_LEN);
		}
	}
}

static void mv643xx_eth_get_ethtool_stats(struct net_device *dev,
					  struct ethtool_stats *stats,
					  uint64_t *data)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int i;

	mv643xx_eth_get_stats(dev);
	mib_counters_update(mp);

	for (i = 0; i < ARRAY_SIZE(mv643xx_eth_stats); i++) {
		const struct mv643xx_eth_stats *stat;
		void *p;

		stat = mv643xx_eth_stats + i;

		if (stat->netdev_off >= 0)
			p = ((void *)mp->dev) + stat->netdev_off;
		else
			p = ((void *)mp) + stat->mp_off;

		data[i] = (stat->sizeof_stat == 8) ?
				*(uint64_t *)p : *(uint32_t *)p;
	}
}

static int mv643xx_eth_get_sset_count(struct net_device *dev, int sset)
{
	if (sset == ETH_SS_STATS)
		return ARRAY_SIZE(mv643xx_eth_stats);

	return -EOPNOTSUPP;
}

static const struct ethtool_ops mv643xx_eth_ethtool_ops = {
	.get_settings		= mv643xx_eth_get_settings,
	.set_settings		= mv643xx_eth_set_settings,
	.get_drvinfo		= mv643xx_eth_get_drvinfo,
	.nway_reset		= mv643xx_eth_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_coalesce		= mv643xx_eth_get_coalesce,
	.set_coalesce		= mv643xx_eth_set_coalesce,
	.get_ringparam		= mv643xx_eth_get_ringparam,
	.set_ringparam		= mv643xx_eth_set_ringparam,
	.get_strings		= mv643xx_eth_get_strings,
	.get_ethtool_stats	= mv643xx_eth_get_ethtool_stats,
	.get_sset_count		= mv643xx_eth_get_sset_count,
	.get_ts_info		= ethtool_op_get_ts_info,
	.get_wol                = mv643xx_eth_get_wol,
	.set_wol                = mv643xx_eth_set_wol,
};


/* address handling *********************************************************/
static void uc_addr_get(struct mv643xx_eth_private *mp, unsigned char *addr)
{
	unsigned int mac_h = rdlp(mp, MAC_ADDR_HIGH);
	unsigned int mac_l = rdlp(mp, MAC_ADDR_LOW);

	addr[0] = (mac_h >> 24) & 0xff;
	addr[1] = (mac_h >> 16) & 0xff;
	addr[2] = (mac_h >> 8) & 0xff;
	addr[3] = mac_h & 0xff;
	addr[4] = (mac_l >> 8) & 0xff;
	addr[5] = mac_l & 0xff;
}

static void uc_addr_set(struct mv643xx_eth_private *mp, unsigned char *addr)
{
	wrlp(mp, MAC_ADDR_HIGH,
		(addr[0] << 24) | (addr[1] << 16) | (addr[2] << 8) | addr[3]);
	wrlp(mp, MAC_ADDR_LOW, (addr[4] << 8) | addr[5]);
}

static u32 uc_addr_filter_mask(struct net_device *dev)
{
	struct netdev_hw_addr *ha;
	u32 nibbles;

	if (dev->flags & IFF_PROMISC)
		return 0;

	nibbles = 1 << (dev->dev_addr[5] & 0x0f);
	netdev_for_each_uc_addr(ha, dev) {
		if (memcmp(dev->dev_addr, ha->addr, 5))
			return 0;
		if ((dev->dev_addr[5] ^ ha->addr[5]) & 0xf0)
			return 0;

		nibbles |= 1 << (ha->addr[5] & 0x0f);
	}

	return nibbles;
}

static void mv643xx_eth_program_unicast_filter(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	u32 port_config;
	u32 nibbles;
	int i;

	uc_addr_set(mp, dev->dev_addr);

	port_config = rdlp(mp, PORT_CONFIG) & ~UNICAST_PROMISCUOUS_MODE;

	nibbles = uc_addr_filter_mask(dev);
	if (!nibbles) {
		port_config |= UNICAST_PROMISCUOUS_MODE;
		nibbles = 0xffff;
	}

	for (i = 0; i < 16; i += 4) {
		int off = UNICAST_TABLE(mp->port_num) + i;
		u32 v;

		v = 0;
		if (nibbles & 1)
			v |= 0x00000001;
		if (nibbles & 2)
			v |= 0x00000100;
		if (nibbles & 4)
			v |= 0x00010000;
		if (nibbles & 8)
			v |= 0x01000000;
		nibbles >>= 4;

		wrl(mp, off, v);
	}

	wrlp(mp, PORT_CONFIG, port_config);
}

static int addr_crc(unsigned char *addr)
{
	int crc = 0;
	int i;

	for (i = 0; i < 6; i++) {
		int j;

		crc = (crc ^ addr[i]) << 8;
		for (j = 7; j >= 0; j--) {
			if (crc & (0x100 << j))
				crc ^= 0x107 << j;
		}
	}

	return crc;
}

static void mv643xx_eth_program_multicast_filter(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	u32 *mc_spec;
	u32 *mc_other;
	struct netdev_hw_addr *ha;
	int i;

	if (dev->flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		int port_num;
		u32 accept;

oom:
		port_num = mp->port_num;
		accept = 0x01010101;
		for (i = 0; i < 0x100; i += 4) {
			wrl(mp, SPECIAL_MCAST_TABLE(port_num) + i, accept);
			wrl(mp, OTHER_MCAST_TABLE(port_num) + i, accept);
		}
		return;
	}

	mc_spec = kmalloc(0x200, GFP_ATOMIC);
	if (mc_spec == NULL)
		goto oom;
	mc_other = mc_spec + (0x100 >> 2);

	memset(mc_spec, 0, 0x100);
	memset(mc_other, 0, 0x100);

	netdev_for_each_mc_addr(ha, dev) {
		u8 *a = ha->addr;
		u32 *table;
		int entry;

		if (memcmp(a, "\x01\x00\x5e\x00\x00", 5) == 0) {
			table = mc_spec;
			entry = a[5];
		} else {
			table = mc_other;
			entry = addr_crc(a);
		}

		table[entry >> 2] |= 1 << (8 * (entry & 3));
	}

	for (i = 0; i < 0x100; i += 4) {
		wrl(mp, SPECIAL_MCAST_TABLE(mp->port_num) + i, mc_spec[i >> 2]);
		wrl(mp, OTHER_MCAST_TABLE(mp->port_num) + i, mc_other[i >> 2]);
	}

	kfree(mc_spec);
}

static void mv643xx_eth_set_rx_mode(struct net_device *dev)
{
	mv643xx_eth_program_unicast_filter(dev);
	mv643xx_eth_program_multicast_filter(dev);
}

static int mv643xx_eth_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sa = addr;

	if (!is_valid_ether_addr(sa->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, sa->sa_data, ETH_ALEN);

	netif_addr_lock_bh(dev);
	mv643xx_eth_program_unicast_filter(dev);
	netif_addr_unlock_bh(dev);

	return 0;
}


/* rx/tx queue initialisation ***********************************************/
static int rxq_init(struct mv643xx_eth_private *mp, int index)
{
	struct rx_queue *rxq = mp->rxq + index;
	struct rx_desc *rx_desc;
	int size;
	int i;

	rxq->index = index;

	rxq->rx_ring_size = mp->rx_ring_size;

	rxq->rx_desc_count = 0;
	rxq->rx_curr_desc = 0;
	rxq->rx_used_desc = 0;

	size = rxq->rx_ring_size * sizeof(struct rx_desc);

	if (index == 0 && size <= mp->rx_desc_sram_size) {
		rxq->rx_desc_area = ioremap(mp->rx_desc_sram_addr,
						mp->rx_desc_sram_size);
		rxq->rx_desc_dma = mp->rx_desc_sram_addr;
	} else {
		rxq->rx_desc_area = dma_alloc_coherent(mp->dev->dev.parent,
						       size, &rxq->rx_desc_dma,
						       GFP_KERNEL);
	}

	if (rxq->rx_desc_area == NULL) {
		netdev_err(mp->dev,
			   "can't allocate rx ring (%d bytes)\n", size);
		goto out;
	}
	memset(rxq->rx_desc_area, 0, size);

	rxq->rx_desc_area_size = size;
	rxq->rx_skb = kcalloc(rxq->rx_ring_size, sizeof(*rxq->rx_skb),
				    GFP_KERNEL);
	if (rxq->rx_skb == NULL)
		goto out_free;

	rx_desc = rxq->rx_desc_area;
	for (i = 0; i < rxq->rx_ring_size; i++) {
		int nexti;

		nexti = i + 1;
		if (nexti == rxq->rx_ring_size)
			nexti = 0;

		rx_desc[i].next_desc_ptr = rxq->rx_desc_dma +
					nexti * sizeof(struct rx_desc);
	}

	return 0;


out_free:
	if (index == 0 && size <= mp->rx_desc_sram_size)
		iounmap(rxq->rx_desc_area);
	else
		dma_free_coherent(mp->dev->dev.parent, size,
				  rxq->rx_desc_area,
				  rxq->rx_desc_dma);

out:
	return -ENOMEM;
}

static void rxq_deinit(struct rx_queue *rxq)
{
	struct mv643xx_eth_private *mp = rxq_to_mp(rxq);
	int i;

	rxq_disable(rxq);

	for (i = 0; i < rxq->rx_ring_size; i++) {
		if (rxq->rx_skb[i]) {
			dev_kfree_skb(rxq->rx_skb[i]);
			rxq->rx_desc_count--;
		}
	}

	if (rxq->rx_desc_count) {
		netdev_err(mp->dev, "error freeing rx ring -- %d skbs stuck\n",
			   rxq->rx_desc_count);
	}

	if (rxq->index == 0 &&
	    rxq->rx_desc_area_size <= mp->rx_desc_sram_size)
		iounmap(rxq->rx_desc_area);
	else
		dma_free_coherent(mp->dev->dev.parent, rxq->rx_desc_area_size,
				  rxq->rx_desc_area, rxq->rx_desc_dma);

	kfree(rxq->rx_skb);
}

static int txq_init(struct mv643xx_eth_private *mp, int index)
{
	struct tx_queue *txq = mp->txq + index;
	struct tx_desc *tx_desc;
	int size;
	int i;

	txq->index = index;

	txq->tx_ring_size = mp->tx_ring_size;

	txq->tx_desc_count = 0;
	txq->tx_curr_desc = 0;
	txq->tx_used_desc = 0;

	size = txq->tx_ring_size * sizeof(struct tx_desc);

	if (index == 0 && size <= mp->tx_desc_sram_size) {
		txq->tx_desc_area = ioremap(mp->tx_desc_sram_addr,
						mp->tx_desc_sram_size);
		txq->tx_desc_dma = mp->tx_desc_sram_addr;
	} else {
		txq->tx_desc_area = dma_alloc_coherent(mp->dev->dev.parent,
						       size, &txq->tx_desc_dma,
						       GFP_KERNEL);
	}

	if (txq->tx_desc_area == NULL) {
		netdev_err(mp->dev,
			   "can't allocate tx ring (%d bytes)\n", size);
		return -ENOMEM;
	}
	memset(txq->tx_desc_area, 0, size);

	txq->tx_desc_area_size = size;

	tx_desc = txq->tx_desc_area;
	for (i = 0; i < txq->tx_ring_size; i++) {
		struct tx_desc *txd = tx_desc + i;
		int nexti;

		nexti = i + 1;
		if (nexti == txq->tx_ring_size)
			nexti = 0;

		txd->cmd_sts = 0;
		txd->next_desc_ptr = txq->tx_desc_dma +
					nexti * sizeof(struct tx_desc);
	}

	skb_queue_head_init(&txq->tx_skb);

	return 0;
}

static void txq_deinit(struct tx_queue *txq)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);

	txq_disable(txq);
	txq_reclaim(txq, txq->tx_ring_size, 1);

	BUG_ON(txq->tx_used_desc != txq->tx_curr_desc);

	if (txq->index == 0 &&
	    txq->tx_desc_area_size <= mp->tx_desc_sram_size)
		iounmap(txq->tx_desc_area);
	else
		dma_free_coherent(mp->dev->dev.parent, txq->tx_desc_area_size,
				  txq->tx_desc_area, txq->tx_desc_dma);
}


/* netdev ops and related ***************************************************/
static int mv643xx_eth_collect_events(struct mv643xx_eth_private *mp)
{
	u32 int_cause;
	u32 int_cause_ext;

	int_cause = rdlp(mp, INT_CAUSE) & mp->int_mask;
	if (int_cause == 0)
		return 0;

	int_cause_ext = 0;
	if (int_cause & INT_EXT) {
		int_cause &= ~INT_EXT;
		int_cause_ext = rdlp(mp, INT_CAUSE_EXT);
	}

	if (int_cause) {
		wrlp(mp, INT_CAUSE, ~int_cause);
		mp->work_tx_end |= ((int_cause & INT_TX_END) >> 19) &
				~(rdlp(mp, TXQ_COMMAND) & 0xff);
		mp->work_rx |= (int_cause & INT_RX) >> 2;
	}

	int_cause_ext &= INT_EXT_LINK_PHY | INT_EXT_TX;
	if (int_cause_ext) {
		wrlp(mp, INT_CAUSE_EXT, ~int_cause_ext);
		if (int_cause_ext & INT_EXT_LINK_PHY)
			mp->work_link = 1;
		mp->work_tx |= int_cause_ext & INT_EXT_TX;
	}

	return 1;
}

static irqreturn_t mv643xx_eth_irq(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	if (unlikely(!mv643xx_eth_collect_events(mp)))
		return IRQ_NONE;

	wrlp(mp, INT_MASK, 0);
	napi_schedule(&mp->napi);

	return IRQ_HANDLED;
}

static void handle_link_event(struct mv643xx_eth_private *mp)
{
	struct net_device *dev = mp->dev;
	u32 port_status;
	int speed;
	int duplex;
	int fc;

	port_status = rdlp(mp, PORT_STATUS);
	if (!(port_status & LINK_UP)) {
		if (netif_carrier_ok(dev)) {
			int i;

			netdev_info(dev, "link down\n");

			netif_carrier_off(dev);

			for (i = 0; i < mp->txq_count; i++) {
				struct tx_queue *txq = mp->txq + i;

				txq_reclaim(txq, txq->tx_ring_size, 1);
				txq_reset_hw_ptr(txq);
			}
		}
		return;
	}

	switch (port_status & PORT_SPEED_MASK) {
	case PORT_SPEED_10:
		speed = 10;
		break;
	case PORT_SPEED_100:
		speed = 100;
		break;
	case PORT_SPEED_1000:
		speed = 1000;
		break;
	default:
		speed = -1;
		break;
	}
	duplex = (port_status & FULL_DUPLEX) ? 1 : 0;
	fc = (port_status & FLOW_CONTROL_ENABLED) ? 1 : 0;

	netdev_info(dev, "link up, %d Mb/s, %s duplex, flow control %sabled\n",
		    speed, duplex ? "full" : "half", fc ? "en" : "dis");

	if (!netif_carrier_ok(dev))
		netif_carrier_on(dev);
}

static int mv643xx_eth_poll(struct napi_struct *napi, int budget)
{
	struct mv643xx_eth_private *mp;
	int work_done;

	mp = container_of(napi, struct mv643xx_eth_private, napi);

	if (unlikely(mp->oom)) {
		mp->oom = 0;
		del_timer(&mp->rx_oom);
	}

	work_done = 0;
	while (work_done < budget) {
		u8 queue_mask;
		int queue;
		int work_tbd;

		if (mp->work_link) {
			mp->work_link = 0;
			handle_link_event(mp);
			work_done++;
			continue;
		}

		queue_mask = mp->work_tx | mp->work_tx_end | mp->work_rx;
		if (likely(!mp->oom))
			queue_mask |= mp->work_rx_refill;

		if (!queue_mask) {
			if (mv643xx_eth_collect_events(mp))
				continue;
			break;
		}

		queue = fls(queue_mask) - 1;
		queue_mask = 1 << queue;

		work_tbd = budget - work_done;
		if (work_tbd > 16)
			work_tbd = 16;

		if (mp->work_tx_end & queue_mask) {
			txq_kick(mp->txq + queue);
		} else if (mp->work_tx & queue_mask) {
			work_done += txq_reclaim(mp->txq + queue, work_tbd, 0);
			txq_maybe_wake(mp->txq + queue);
		} else if (mp->work_rx & queue_mask) {
			work_done += rxq_process(mp->rxq + queue, work_tbd);
		} else if (!mp->oom && (mp->work_rx_refill & queue_mask)) {
			work_done += rxq_refill(mp->rxq + queue, work_tbd);
		} else {
			BUG();
		}
	}

	if (work_done < budget) {
		if (mp->oom)
			mod_timer(&mp->rx_oom, jiffies + (HZ / 10));
		napi_complete(napi);
		wrlp(mp, INT_MASK, mp->int_mask);
	}

	return work_done;
}

static inline void oom_timer_wrapper(unsigned long data)
{
	struct mv643xx_eth_private *mp = (void *)data;

	napi_schedule(&mp->napi);
}

static void port_start(struct mv643xx_eth_private *mp)
{
	u32 pscr;
	int i;

	/*
	 * Perform PHY reset, if there is a PHY.
	 */
	if (mp->phy != NULL) {
		struct ethtool_cmd cmd;

		mv643xx_eth_get_settings(mp->dev, &cmd);
		phy_init_hw(mp->phy);
		mv643xx_eth_set_settings(mp->dev, &cmd);
		phy_start(mp->phy);
	}

	/*
	 * Configure basic link parameters.
	 */
	pscr = rdlp(mp, PORT_SERIAL_CONTROL);

	pscr |= SERIAL_PORT_ENABLE;
	wrlp(mp, PORT_SERIAL_CONTROL, pscr);

	pscr |= DO_NOT_FORCE_LINK_FAIL;
	if (mp->phy == NULL)
		pscr |= FORCE_LINK_PASS;
	wrlp(mp, PORT_SERIAL_CONTROL, pscr);

	/*
	 * Configure TX path and queues.
	 */
	tx_set_rate(mp, 1000000000, 16777216);
	for (i = 0; i < mp->txq_count; i++) {
		struct tx_queue *txq = mp->txq + i;

		txq_reset_hw_ptr(txq);
		txq_set_rate(txq, 1000000000, 16777216);
		txq_set_fixed_prio_mode(txq);
	}

	/*
	 * Receive all unmatched unicast, TCP, UDP, BPDU and broadcast
	 * frames to RX queue #0, and include the pseudo-header when
	 * calculating receive checksums.
	 */
	mv643xx_eth_set_features(mp->dev, mp->dev->features);

	/*
	 * Treat BPDUs as normal multicasts, and disable partition mode.
	 */
	wrlp(mp, PORT_CONFIG_EXT, 0x00000000);

	/*
	 * Add configured unicast addresses to address filter table.
	 */
	mv643xx_eth_program_unicast_filter(mp->dev);

	/*
	 * Enable the receive queues.
	 */
	for (i = 0; i < mp->rxq_count; i++) {
		struct rx_queue *rxq = mp->rxq + i;
		u32 addr;

		addr = (u32)rxq->rx_desc_dma;
		addr += rxq->rx_curr_desc * sizeof(struct rx_desc);
		wrlp(mp, RXQ_CURRENT_DESC_PTR(i), addr);

		rxq_enable(rxq);
	}
}

static void mv643xx_eth_recalc_skb_size(struct mv643xx_eth_private *mp)
{
	int skb_size;

	/*
	 * Reserve 2+14 bytes for an ethernet header (the hardware
	 * automatically prepends 2 bytes of dummy data to each
	 * received packet), 16 bytes for up to four VLAN tags, and
	 * 4 bytes for the trailing FCS -- 36 bytes total.
	 */
	skb_size = mp->dev->mtu + 36;

	/*
	 * Make sure that the skb size is a multiple of 8 bytes, as
	 * the lower three bits of the receive descriptor's buffer
	 * size field are ignored by the hardware.
	 */
	mp->skb_size = (skb_size + 7) & ~7;

	/*
	 * If NET_SKB_PAD is smaller than a cache line,
	 * netdev_alloc_skb() will cause skb->data to be misaligned
	 * to a cache line boundary.  If this is the case, include
	 * some extra space to allow re-aligning the data area.
	 */
	mp->skb_size += SKB_DMA_REALIGN;
}

static int mv643xx_eth_open(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int err;
	int i;

	wrlp(mp, INT_CAUSE, 0);
	wrlp(mp, INT_CAUSE_EXT, 0);
	rdlp(mp, INT_CAUSE_EXT);

	err = request_irq(dev->irq, mv643xx_eth_irq,
			  IRQF_SHARED, dev->name, dev);
	if (err) {
		netdev_err(dev, "can't assign irq\n");
		return -EAGAIN;
	}

	mv643xx_eth_recalc_skb_size(mp);

	napi_enable(&mp->napi);

	mp->int_mask = INT_EXT;

	for (i = 0; i < mp->rxq_count; i++) {
		err = rxq_init(mp, i);
		if (err) {
			while (--i >= 0)
				rxq_deinit(mp->rxq + i);
			goto out;
		}

		rxq_refill(mp->rxq + i, INT_MAX);
		mp->int_mask |= INT_RX_0 << i;
	}

	if (mp->oom) {
		mp->rx_oom.expires = jiffies + (HZ / 10);
		add_timer(&mp->rx_oom);
	}

	for (i = 0; i < mp->txq_count; i++) {
		err = txq_init(mp, i);
		if (err) {
			while (--i >= 0)
				txq_deinit(mp->txq + i);
			goto out_free;
		}
		mp->int_mask |= INT_TX_END_0 << i;
	}

	add_timer(&mp->mib_counters_timer);
	port_start(mp);

	wrlp(mp, INT_MASK_EXT, INT_EXT_LINK_PHY | INT_EXT_TX);
	wrlp(mp, INT_MASK, mp->int_mask);

	return 0;


out_free:
	for (i = 0; i < mp->rxq_count; i++)
		rxq_deinit(mp->rxq + i);
out:
	free_irq(dev->irq, dev);

	return err;
}

static void port_reset(struct mv643xx_eth_private *mp)
{
	unsigned int data;
	int i;

	for (i = 0; i < mp->rxq_count; i++)
		rxq_disable(mp->rxq + i);
	for (i = 0; i < mp->txq_count; i++)
		txq_disable(mp->txq + i);

	while (1) {
		u32 ps = rdlp(mp, PORT_STATUS);

		if ((ps & (TX_IN_PROGRESS | TX_FIFO_EMPTY)) == TX_FIFO_EMPTY)
			break;
		udelay(10);
	}

	/* Reset the Enable bit in the Configuration Register */
	data = rdlp(mp, PORT_SERIAL_CONTROL);
	data &= ~(SERIAL_PORT_ENABLE		|
		  DO_NOT_FORCE_LINK_FAIL	|
		  FORCE_LINK_PASS);
	wrlp(mp, PORT_SERIAL_CONTROL, data);
}

static int mv643xx_eth_stop(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int i;

	wrlp(mp, INT_MASK_EXT, 0x00000000);
	wrlp(mp, INT_MASK, 0x00000000);
	rdlp(mp, INT_MASK);

	napi_disable(&mp->napi);

	del_timer_sync(&mp->rx_oom);

	netif_carrier_off(dev);
	if (mp->phy)
		phy_stop(mp->phy);
	free_irq(dev->irq, dev);

	port_reset(mp);
	mv643xx_eth_get_stats(dev);
	mib_counters_update(mp);
	del_timer_sync(&mp->mib_counters_timer);

	for (i = 0; i < mp->rxq_count; i++)
		rxq_deinit(mp->rxq + i);
	for (i = 0; i < mp->txq_count; i++)
		txq_deinit(mp->txq + i);

	return 0;
}

static int mv643xx_eth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int ret;

	if (mp->phy == NULL)
		return -ENOTSUPP;

	ret = phy_mii_ioctl(mp->phy, ifr, cmd);
	if (!ret)
		mv643xx_adjust_pscr(mp);
	return ret;
}

static int mv643xx_eth_change_mtu(struct net_device *dev, int new_mtu)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	if (new_mtu < 64 || new_mtu > 9500)
		return -EINVAL;

	dev->mtu = new_mtu;
	mv643xx_eth_recalc_skb_size(mp);
	tx_set_rate(mp, 1000000000, 16777216);

	if (!netif_running(dev))
		return 0;

	/*
	 * Stop and then re-open the interface. This will allocate RX
	 * skbs of the new MTU.
	 * There is a possible danger that the open will not succeed,
	 * due to memory being full.
	 */
	mv643xx_eth_stop(dev);
	if (mv643xx_eth_open(dev)) {
		netdev_err(dev,
			   "fatal error on re-opening device after MTU change\n");
	}

	return 0;
}

static void tx_timeout_task(struct work_struct *ugly)
{
	struct mv643xx_eth_private *mp;

	mp = container_of(ugly, struct mv643xx_eth_private, tx_timeout_task);
	if (netif_running(mp->dev)) {
		netif_tx_stop_all_queues(mp->dev);
		port_reset(mp);
		port_start(mp);
		netif_tx_wake_all_queues(mp->dev);
	}
}

static void mv643xx_eth_tx_timeout(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	netdev_info(dev, "tx timeout\n");

	schedule_work(&mp->tx_timeout_task);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void mv643xx_eth_netpoll(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	wrlp(mp, INT_MASK, 0x00000000);
	rdlp(mp, INT_MASK);

	mv643xx_eth_irq(dev->irq, dev);

	wrlp(mp, INT_MASK, mp->int_mask);
}
#endif


/* platform glue ************************************************************/
static void
mv643xx_eth_conf_mbus_windows(struct mv643xx_eth_shared_private *msp,
			      const struct mbus_dram_target_info *dram)
{
	void __iomem *base = msp->base;
	u32 win_enable;
	u32 win_protect;
	int i;

	for (i = 0; i < 6; i++) {
		writel(0, base + WINDOW_BASE(i));
		writel(0, base + WINDOW_SIZE(i));
		if (i < 4)
			writel(0, base + WINDOW_REMAP_HIGH(i));
	}

	win_enable = 0x3f;
	win_protect = 0;

	for (i = 0; i < dram->num_cs; i++) {
		const struct mbus_dram_window *cs = dram->cs + i;

		writel((cs->base & 0xffff0000) |
			(cs->mbus_attr << 8) |
			dram->mbus_dram_target_id, base + WINDOW_BASE(i));
		writel((cs->size - 1) & 0xffff0000, base + WINDOW_SIZE(i));

		win_enable &= ~(1 << i);
		win_protect |= 3 << (2 * i);
	}

	writel(win_enable, base + WINDOW_BAR_ENABLE);
	msp->win_protect = win_protect;
}

static void infer_hw_params(struct mv643xx_eth_shared_private *msp)
{
	/*
	 * Check whether we have a 14-bit coal limit field in bits
	 * [21:8], or a 16-bit coal limit in bits [25,21:7] of the
	 * SDMA config register.
	 */
	writel(0x02000000, msp->base + 0x0400 + SDMA_CONFIG);
	if (readl(msp->base + 0x0400 + SDMA_CONFIG) & 0x02000000)
		msp->extended_rx_coal_limit = 1;
	else
		msp->extended_rx_coal_limit = 0;

	/*
	 * Check whether the MAC supports TX rate control, and if
	 * yes, whether its associated registers are in the old or
	 * the new place.
	 */
	writel(1, msp->base + 0x0400 + TX_BW_MTU_MOVED);
	if (readl(msp->base + 0x0400 + TX_BW_MTU_MOVED) & 1) {
		msp->tx_bw_control = TX_BW_CONTROL_NEW_LAYOUT;
	} else {
		writel(7, msp->base + 0x0400 + TX_BW_RATE);
		if (readl(msp->base + 0x0400 + TX_BW_RATE) & 7)
			msp->tx_bw_control = TX_BW_CONTROL_OLD_LAYOUT;
		else
			msp->tx_bw_control = TX_BW_CONTROL_ABSENT;
	}
}

#if defined(CONFIG_OF)
static const struct of_device_id mv643xx_eth_shared_ids[] = {
	{ .compatible = "marvell,orion-eth", },
	{ .compatible = "marvell,kirkwood-eth", },
	{ }
};
MODULE_DEVICE_TABLE(of, mv643xx_eth_shared_ids);
#endif

#if defined(CONFIG_OF) && !defined(CONFIG_MV64X60)
#define mv643xx_eth_property(_np, _name, _v)				\
	do {								\
		u32 tmp;						\
		if (!of_property_read_u32(_np, "marvell," _name, &tmp))	\
			_v = tmp;					\
	} while (0)

static struct platform_device *port_platdev[3];

static int mv643xx_eth_shared_of_add_port(struct platform_device *pdev,
					  struct device_node *pnp)
{
	struct platform_device *ppdev;
	struct mv643xx_eth_platform_data ppd;
	struct resource res;
	const char *mac_addr;
	int ret;
	int dev_num = 0;

	memset(&ppd, 0, sizeof(ppd));
	ppd.shared = pdev;

	memset(&res, 0, sizeof(res));
	if (!of_irq_to_resource(pnp, 0, &res)) {
		dev_err(&pdev->dev, "missing interrupt on %s\n", pnp->name);
		return -EINVAL;
	}

	if (of_property_read_u32(pnp, "reg", &ppd.port_number)) {
		dev_err(&pdev->dev, "missing reg property on %s\n", pnp->name);
		return -EINVAL;
	}

	if (ppd.port_number >= 3) {
		dev_err(&pdev->dev, "invalid reg property on %s\n", pnp->name);
		return -EINVAL;
	}

	while (dev_num < 3 && port_platdev[dev_num])
		dev_num++;

	if (dev_num == 3) {
		dev_err(&pdev->dev, "too many ports registered\n");
		return -EINVAL;
	}

	mac_addr = of_get_mac_address(pnp);
	if (mac_addr)
		memcpy(ppd.mac_addr, mac_addr, ETH_ALEN);

	mv643xx_eth_property(pnp, "tx-queue-size", ppd.tx_queue_size);
	mv643xx_eth_property(pnp, "tx-sram-addr", ppd.tx_sram_addr);
	mv643xx_eth_property(pnp, "tx-sram-size", ppd.tx_sram_size);
	mv643xx_eth_property(pnp, "rx-queue-size", ppd.rx_queue_size);
	mv643xx_eth_property(pnp, "rx-sram-addr", ppd.rx_sram_addr);
	mv643xx_eth_property(pnp, "rx-sram-size", ppd.rx_sram_size);

	ppd.phy_node = of_parse_phandle(pnp, "phy-handle", 0);
	if (!ppd.phy_node) {
		ppd.phy_addr = MV643XX_ETH_PHY_NONE;
		of_property_read_u32(pnp, "speed", &ppd.speed);
		of_property_read_u32(pnp, "duplex", &ppd.duplex);
	}

	ppdev = platform_device_alloc(MV643XX_ETH_NAME, dev_num);
	if (!ppdev)
		return -ENOMEM;
	ppdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	ppdev->dev.of_node = pnp;

	ret = platform_device_add_resources(ppdev, &res, 1);
	if (ret)
		goto port_err;

	ret = platform_device_add_data(ppdev, &ppd, sizeof(ppd));
	if (ret)
		goto port_err;

	ret = platform_device_add(ppdev);
	if (ret)
		goto port_err;

	port_platdev[dev_num] = ppdev;

	return 0;

port_err:
	platform_device_put(ppdev);
	return ret;
}

static int mv643xx_eth_shared_of_probe(struct platform_device *pdev)
{
	struct mv643xx_eth_shared_platform_data *pd;
	struct device_node *pnp, *np = pdev->dev.of_node;
	int ret;

	/* bail out if not registered from DT */
	if (!np)
		return 0;

	pd = devm_kzalloc(&pdev->dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;
	pdev->dev.platform_data = pd;

	mv643xx_eth_property(np, "tx-checksum-limit", pd->tx_csum_limit);

	for_each_available_child_of_node(np, pnp) {
		ret = mv643xx_eth_shared_of_add_port(pdev, pnp);
		if (ret)
			return ret;
	}
	return 0;
}

static void mv643xx_eth_shared_of_remove(void)
{
	int n;

	for (n = 0; n < 3; n++) {
		platform_device_del(port_platdev[n]);
		port_platdev[n] = NULL;
	}
}
#else
static inline int mv643xx_eth_shared_of_probe(struct platform_device *pdev)
{
	return 0;
}

static inline void mv643xx_eth_shared_of_remove(void)
{
}
#endif

static int mv643xx_eth_shared_probe(struct platform_device *pdev)
{
	static int mv643xx_eth_version_printed;
	struct mv643xx_eth_shared_platform_data *pd;
	struct mv643xx_eth_shared_private *msp;
	const struct mbus_dram_target_info *dram;
	struct resource *res;
	int ret;

	if (!mv643xx_eth_version_printed++)
		pr_notice("MV-643xx 10/100/1000 ethernet driver version %s\n",
			  mv643xx_eth_driver_version);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -EINVAL;

	msp = devm_kzalloc(&pdev->dev, sizeof(*msp), GFP_KERNEL);
	if (msp == NULL)
		return -ENOMEM;
	platform_set_drvdata(pdev, msp);

	msp->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (msp->base == NULL)
		return -ENOMEM;

	msp->clk = devm_clk_get(&pdev->dev, NULL);
	if (!IS_ERR(msp->clk))
		clk_prepare_enable(msp->clk);

	/*
	 * (Re-)program MBUS remapping windows if we are asked to.
	 */
	dram = mv_mbus_dram_info();
	if (dram)
		mv643xx_eth_conf_mbus_windows(msp, dram);

	ret = mv643xx_eth_shared_of_probe(pdev);
	if (ret)
		return ret;
	pd = dev_get_platdata(&pdev->dev);

	msp->tx_csum_limit = (pd != NULL && pd->tx_csum_limit) ?
					pd->tx_csum_limit : 9 * 1024;
	infer_hw_params(msp);

	return 0;
}

static int mv643xx_eth_shared_remove(struct platform_device *pdev)
{
	struct mv643xx_eth_shared_private *msp = platform_get_drvdata(pdev);

	mv643xx_eth_shared_of_remove();
	if (!IS_ERR(msp->clk))
		clk_disable_unprepare(msp->clk);
	return 0;
}

static struct platform_driver mv643xx_eth_shared_driver = {
	.probe		= mv643xx_eth_shared_probe,
	.remove		= mv643xx_eth_shared_remove,
	.driver = {
		.name	= MV643XX_ETH_SHARED_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(mv643xx_eth_shared_ids),
	},
};

static void phy_addr_set(struct mv643xx_eth_private *mp, int phy_addr)
{
	int addr_shift = 5 * mp->port_num;
	u32 data;

	data = rdl(mp, PHY_ADDR);
	data &= ~(0x1f << addr_shift);
	data |= (phy_addr & 0x1f) << addr_shift;
	wrl(mp, PHY_ADDR, data);
}

static int phy_addr_get(struct mv643xx_eth_private *mp)
{
	unsigned int data;

	data = rdl(mp, PHY_ADDR);

	return (data >> (5 * mp->port_num)) & 0x1f;
}

static void set_params(struct mv643xx_eth_private *mp,
		       struct mv643xx_eth_platform_data *pd)
{
	struct net_device *dev = mp->dev;

	if (is_valid_ether_addr(pd->mac_addr))
		memcpy(dev->dev_addr, pd->mac_addr, ETH_ALEN);
	else
		uc_addr_get(mp, dev->dev_addr);

	mp->rx_ring_size = DEFAULT_RX_QUEUE_SIZE;
	if (pd->rx_queue_size)
		mp->rx_ring_size = pd->rx_queue_size;
	mp->rx_desc_sram_addr = pd->rx_sram_addr;
	mp->rx_desc_sram_size = pd->rx_sram_size;

	mp->rxq_count = pd->rx_queue_count ? : 1;

	mp->tx_ring_size = DEFAULT_TX_QUEUE_SIZE;
	if (pd->tx_queue_size)
		mp->tx_ring_size = pd->tx_queue_size;
	mp->tx_desc_sram_addr = pd->tx_sram_addr;
	mp->tx_desc_sram_size = pd->tx_sram_size;

	mp->txq_count = pd->tx_queue_count ? : 1;
}

static void mv643xx_eth_adjust_link(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	mv643xx_adjust_pscr(mp);
}

static struct phy_device *phy_scan(struct mv643xx_eth_private *mp,
				   int phy_addr)
{
	struct phy_device *phydev;
	int start;
	int num;
	int i;
	char phy_id[MII_BUS_ID_SIZE + 3];

	if (phy_addr == MV643XX_ETH_PHY_ADDR_DEFAULT) {
		start = phy_addr_get(mp) & 0x1f;
		num = 32;
	} else {
		start = phy_addr & 0x1f;
		num = 1;
	}

	/* Attempt to connect to the PHY using orion-mdio */
	phydev = ERR_PTR(-ENODEV);
	for (i = 0; i < num; i++) {
		int addr = (start + i) & 0x1f;

		snprintf(phy_id, sizeof(phy_id), PHY_ID_FMT,
				"orion-mdio-mii", addr);

		phydev = phy_connect(mp->dev, phy_id, mv643xx_eth_adjust_link,
				PHY_INTERFACE_MODE_GMII);
		if (!IS_ERR(phydev)) {
			phy_addr_set(mp, addr);
			break;
		}
	}

	return phydev;
}

static void phy_init(struct mv643xx_eth_private *mp, int speed, int duplex)
{
	struct phy_device *phy = mp->phy;

	if (speed == 0) {
		phy->autoneg = AUTONEG_ENABLE;
		phy->speed = 0;
		phy->duplex = 0;
		phy->advertising = phy->supported | ADVERTISED_Autoneg;
	} else {
		phy->autoneg = AUTONEG_DISABLE;
		phy->advertising = 0;
		phy->speed = speed;
		phy->duplex = duplex;
	}
	phy_start_aneg(phy);
}

static void init_pscr(struct mv643xx_eth_private *mp, int speed, int duplex)
{
	u32 pscr;

	pscr = rdlp(mp, PORT_SERIAL_CONTROL);
	if (pscr & SERIAL_PORT_ENABLE) {
		pscr &= ~SERIAL_PORT_ENABLE;
		wrlp(mp, PORT_SERIAL_CONTROL, pscr);
	}

	pscr = MAX_RX_PACKET_9700BYTE | SERIAL_PORT_CONTROL_RESERVED;
	if (mp->phy == NULL) {
		pscr |= DISABLE_AUTO_NEG_SPEED_GMII;
		if (speed == SPEED_1000)
			pscr |= SET_GMII_SPEED_TO_1000;
		else if (speed == SPEED_100)
			pscr |= SET_MII_SPEED_TO_100;

		pscr |= DISABLE_AUTO_NEG_FOR_FLOW_CTRL;

		pscr |= DISABLE_AUTO_NEG_FOR_DUPLEX;
		if (duplex == DUPLEX_FULL)
			pscr |= SET_FULL_DUPLEX_MODE;
	}

	wrlp(mp, PORT_SERIAL_CONTROL, pscr);
}

static const struct net_device_ops mv643xx_eth_netdev_ops = {
	.ndo_open		= mv643xx_eth_open,
	.ndo_stop		= mv643xx_eth_stop,
	.ndo_start_xmit		= mv643xx_eth_xmit,
	.ndo_set_rx_mode	= mv643xx_eth_set_rx_mode,
	.ndo_set_mac_address	= mv643xx_eth_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= mv643xx_eth_ioctl,
	.ndo_change_mtu		= mv643xx_eth_change_mtu,
	.ndo_set_features	= mv643xx_eth_set_features,
	.ndo_tx_timeout		= mv643xx_eth_tx_timeout,
	.ndo_get_stats		= mv643xx_eth_get_stats,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= mv643xx_eth_netpoll,
#endif
};

static int mv643xx_eth_probe(struct platform_device *pdev)
{
	struct mv643xx_eth_platform_data *pd;
	struct mv643xx_eth_private *mp;
	struct net_device *dev;
	struct resource *res;
	int err;

	pd = dev_get_platdata(&pdev->dev);
	if (pd == NULL) {
		dev_err(&pdev->dev, "no mv643xx_eth_platform_data\n");
		return -ENODEV;
	}

	if (pd->shared == NULL) {
		dev_err(&pdev->dev, "no mv643xx_eth_platform_data->shared\n");
		return -ENODEV;
	}

	dev = alloc_etherdev_mq(sizeof(struct mv643xx_eth_private), 8);
	if (!dev)
		return -ENOMEM;

	mp = netdev_priv(dev);
	platform_set_drvdata(pdev, mp);

	mp->shared = platform_get_drvdata(pd->shared);
	mp->base = mp->shared->base + 0x0400 + (pd->port_number << 10);
	mp->port_num = pd->port_number;

	mp->dev = dev;

	/* Kirkwood resets some registers on gated clocks. Especially
	 * CLK125_BYPASS_EN must be cleared but is not available on
	 * all other SoCs/System Controllers using this driver.
	 */
	if (of_device_is_compatible(pdev->dev.of_node,
				    "marvell,kirkwood-eth-port"))
		wrlp(mp, PORT_SERIAL_CONTROL1,
		     rdlp(mp, PORT_SERIAL_CONTROL1) & ~CLK125_BYPASS_EN);

	/*
	 * Start with a default rate, and if there is a clock, allow
	 * it to override the default.
	 */
	mp->t_clk = 133000000;
	mp->clk = devm_clk_get(&pdev->dev, NULL);
	if (!IS_ERR(mp->clk)) {
		clk_prepare_enable(mp->clk);
		mp->t_clk = clk_get_rate(mp->clk);
	} else if (!IS_ERR(mp->shared->clk)) {
		mp->t_clk = clk_get_rate(mp->shared->clk);
	}

	set_params(mp, pd);
	netif_set_real_num_tx_queues(dev, mp->txq_count);
	netif_set_real_num_rx_queues(dev, mp->rxq_count);

	err = 0;
	if (pd->phy_node) {
		mp->phy = of_phy_connect(mp->dev, pd->phy_node,
					 mv643xx_eth_adjust_link, 0,
					 PHY_INTERFACE_MODE_GMII);
		if (!mp->phy)
			err = -ENODEV;
		else
			phy_addr_set(mp, mp->phy->addr);
	} else if (pd->phy_addr != MV643XX_ETH_PHY_NONE) {
		mp->phy = phy_scan(mp, pd->phy_addr);

		if (IS_ERR(mp->phy))
			err = PTR_ERR(mp->phy);
		else
			phy_init(mp, pd->speed, pd->duplex);
	}
	if (err == -ENODEV) {
		err = -EPROBE_DEFER;
		goto out;
	}
	if (err)
		goto out;

	SET_ETHTOOL_OPS(dev, &mv643xx_eth_ethtool_ops);

	init_pscr(mp, pd->speed, pd->duplex);


	mib_counters_clear(mp);

	init_timer(&mp->mib_counters_timer);
	mp->mib_counters_timer.data = (unsigned long)mp;
	mp->mib_counters_timer.function = mib_counters_timer_wrapper;
	mp->mib_counters_timer.expires = jiffies + 30 * HZ;

	spin_lock_init(&mp->mib_counters_lock);

	INIT_WORK(&mp->tx_timeout_task, tx_timeout_task);

	netif_napi_add(dev, &mp->napi, mv643xx_eth_poll, NAPI_POLL_WEIGHT);

	init_timer(&mp->rx_oom);
	mp->rx_oom.data = (unsigned long)mp;
	mp->rx_oom.function = oom_timer_wrapper;


	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	BUG_ON(!res);
	dev->irq = res->start;

	dev->netdev_ops = &mv643xx_eth_netdev_ops;

	dev->watchdog_timeo = 2 * HZ;
	dev->base_addr = 0;

	dev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_RXCSUM;
	dev->features = NETIF_F_SG | NETIF_F_IP_CSUM | NETIF_F_RXCSUM;
	dev->vlan_features = NETIF_F_SG | NETIF_F_IP_CSUM;

	dev->priv_flags |= IFF_UNICAST_FLT;

	SET_NETDEV_DEV(dev, &pdev->dev);

	if (mp->shared->win_protect)
		wrl(mp, WINDOW_PROTECT(mp->port_num), mp->shared->win_protect);

	netif_carrier_off(dev);

	wrlp(mp, SDMA_CONFIG, PORT_SDMA_CONFIG_DEFAULT_VALUE);

	set_rx_coal(mp, 250);
	set_tx_coal(mp, 0);

	err = register_netdev(dev);
	if (err)
		goto out;

	netdev_notice(dev, "port %d with MAC address %pM\n",
		      mp->port_num, dev->dev_addr);

	if (mp->tx_desc_sram_size > 0)
		netdev_notice(dev, "configured with sram\n");

	return 0;

out:
	if (!IS_ERR(mp->clk))
		clk_disable_unprepare(mp->clk);
	free_netdev(dev);

	return err;
}

static int mv643xx_eth_remove(struct platform_device *pdev)
{
	struct mv643xx_eth_private *mp = platform_get_drvdata(pdev);

	unregister_netdev(mp->dev);
	if (mp->phy != NULL)
		phy_disconnect(mp->phy);
	cancel_work_sync(&mp->tx_timeout_task);

	if (!IS_ERR(mp->clk))
		clk_disable_unprepare(mp->clk);

	free_netdev(mp->dev);

	return 0;
}

static void mv643xx_eth_shutdown(struct platform_device *pdev)
{
	struct mv643xx_eth_private *mp = platform_get_drvdata(pdev);

	/* Mask all interrupts on ethernet port */
	wrlp(mp, INT_MASK, 0);
	rdlp(mp, INT_MASK);

	if (netif_running(mp->dev))
		port_reset(mp);
}

static struct platform_driver mv643xx_eth_driver = {
	.probe		= mv643xx_eth_probe,
	.remove		= mv643xx_eth_remove,
	.shutdown	= mv643xx_eth_shutdown,
	.driver = {
		.name	= MV643XX_ETH_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init mv643xx_eth_init_module(void)
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
module_init(mv643xx_eth_init_module);

static void __exit mv643xx_eth_cleanup_module(void)
{
	platform_driver_unregister(&mv643xx_eth_driver);
	platform_driver_unregister(&mv643xx_eth_shared_driver);
}
module_exit(mv643xx_eth_cleanup_module);

MODULE_AUTHOR("Rabeeh Khoury, Assaf Hoffman, Matthew Dharm, "
	      "Manish Lachwani, Dale Farnsworth and Lennert Buytenhek");
MODULE_DESCRIPTION("Ethernet driver for Marvell MV643XX");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" MV643XX_ETH_SHARED_NAME);
MODULE_ALIAS("platform:" MV643XX_ETH_NAME);
