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
#include <linux/mii.h>
#include <linux/mv643xx_eth.h>
#include <asm/io.h>
#include <asm/types.h>
#include <asm/system.h>

static char mv643xx_eth_driver_name[] = "mv643xx_eth";
static char mv643xx_eth_driver_version[] = "1.2";

#define MV643XX_ETH_CHECKSUM_OFFLOAD_TX
#define MV643XX_ETH_NAPI
#define MV643XX_ETH_TX_FAST_REFILL

#ifdef MV643XX_ETH_CHECKSUM_OFFLOAD_TX
#define MAX_DESCS_PER_SKB	(MAX_SKB_FRAGS + 1)
#else
#define MAX_DESCS_PER_SKB	1
#endif

/*
 * Registers shared between all ports.
 */
#define PHY_ADDR			0x0000
#define SMI_REG				0x0004
#define WINDOW_BASE(w)			(0x0200 + ((w) << 3))
#define WINDOW_SIZE(w)			(0x0204 + ((w) << 3))
#define WINDOW_REMAP_HIGH(w)		(0x0280 + ((w) << 2))
#define WINDOW_BAR_ENABLE		0x0290
#define WINDOW_PROTECT(w)		(0x0294 + ((w) << 4))

/*
 * Per-port registers.
 */
#define PORT_CONFIG(p)			(0x0400 + ((p) << 10))
#define  UNICAST_PROMISCUOUS_MODE	0x00000001
#define PORT_CONFIG_EXT(p)		(0x0404 + ((p) << 10))
#define MAC_ADDR_LOW(p)			(0x0414 + ((p) << 10))
#define MAC_ADDR_HIGH(p)		(0x0418 + ((p) << 10))
#define SDMA_CONFIG(p)			(0x041c + ((p) << 10))
#define PORT_SERIAL_CONTROL(p)		(0x043c + ((p) << 10))
#define PORT_STATUS(p)			(0x0444 + ((p) << 10))
#define  TX_FIFO_EMPTY			0x00000400
#define  TX_IN_PROGRESS			0x00000080
#define  PORT_SPEED_MASK		0x00000030
#define  PORT_SPEED_1000		0x00000010
#define  PORT_SPEED_100			0x00000020
#define  PORT_SPEED_10			0x00000000
#define  FLOW_CONTROL_ENABLED		0x00000008
#define  FULL_DUPLEX			0x00000004
#define  LINK_UP			0x00000002
#define TXQ_COMMAND(p)			(0x0448 + ((p) << 10))
#define TXQ_FIX_PRIO_CONF(p)		(0x044c + ((p) << 10))
#define TX_BW_RATE(p)			(0x0450 + ((p) << 10))
#define TX_BW_MTU(p)			(0x0458 + ((p) << 10))
#define TX_BW_BURST(p)			(0x045c + ((p) << 10))
#define INT_CAUSE(p)			(0x0460 + ((p) << 10))
#define  INT_TX_END_0			0x00080000
#define  INT_TX_END			0x07f80000
#define  INT_RX				0x0007fbfc
#define  INT_EXT			0x00000002
#define INT_CAUSE_EXT(p)		(0x0464 + ((p) << 10))
#define  INT_EXT_LINK			0x00100000
#define  INT_EXT_PHY			0x00010000
#define  INT_EXT_TX_ERROR_0		0x00000100
#define  INT_EXT_TX_0			0x00000001
#define  INT_EXT_TX			0x0000ffff
#define INT_MASK(p)			(0x0468 + ((p) << 10))
#define INT_MASK_EXT(p)			(0x046c + ((p) << 10))
#define TX_FIFO_URGENT_THRESHOLD(p)	(0x0474 + ((p) << 10))
#define TXQ_FIX_PRIO_CONF_MOVED(p)	(0x04dc + ((p) << 10))
#define TX_BW_RATE_MOVED(p)		(0x04e0 + ((p) << 10))
#define TX_BW_MTU_MOVED(p)		(0x04e8 + ((p) << 10))
#define TX_BW_BURST_MOVED(p)		(0x04ec + ((p) << 10))
#define RXQ_CURRENT_DESC_PTR(p, q)	(0x060c + ((p) << 10) + ((q) << 4))
#define RXQ_COMMAND(p)			(0x0680 + ((p) << 10))
#define TXQ_CURRENT_DESC_PTR(p, q)	(0x06c0 + ((p) << 10) + ((q) << 2))
#define TXQ_BW_TOKENS(p, q)		(0x0700 + ((p) << 10) + ((q) << 4))
#define TXQ_BW_CONF(p, q)		(0x0704 + ((p) << 10) + ((q) << 4))
#define TXQ_BW_WRR_CONF(p, q)		(0x0708 + ((p) << 10) + ((q) << 4))
#define MIB_COUNTERS(p)			(0x1000 + ((p) << 7))
#define SPECIAL_MCAST_TABLE(p)		(0x1400 + ((p) << 10))
#define OTHER_MCAST_TABLE(p)		(0x1500 + ((p) << 10))
#define UNICAST_TABLE(p)		(0x1600 + ((p) << 10))


/*
 * SDMA configuration register.
 */
#define RX_BURST_SIZE_16_64BIT		(4 << 1)
#define BLM_RX_NO_SWAP			(1 << 4)
#define BLM_TX_NO_SWAP			(1 << 5)
#define TX_BURST_SIZE_16_64BIT		(4 << 22)

#if defined(__BIG_ENDIAN)
#define PORT_SDMA_CONFIG_DEFAULT_VALUE		\
		RX_BURST_SIZE_16_64BIT	|	\
		TX_BURST_SIZE_16_64BIT
#elif defined(__LITTLE_ENDIAN)
#define PORT_SDMA_CONFIG_DEFAULT_VALUE		\
		RX_BURST_SIZE_16_64BIT	|	\
		BLM_RX_NO_SWAP		|	\
		BLM_TX_NO_SWAP		|	\
		TX_BURST_SIZE_16_64BIT
#else
#error One of __BIG_ENDIAN or __LITTLE_ENDIAN must be defined
#endif


/*
 * Port serial control register.
 */
#define SET_MII_SPEED_TO_100			(1 << 24)
#define SET_GMII_SPEED_TO_1000			(1 << 23)
#define SET_FULL_DUPLEX_MODE			(1 << 21)
#define MAX_RX_PACKET_9700BYTE			(5 << 17)
#define DISABLE_AUTO_NEG_SPEED_GMII		(1 << 13)
#define DO_NOT_FORCE_LINK_FAIL			(1 << 10)
#define SERIAL_PORT_CONTROL_RESERVED		(1 << 9)
#define DISABLE_AUTO_NEG_FOR_FLOW_CTRL		(1 << 3)
#define DISABLE_AUTO_NEG_FOR_DUPLEX		(1 << 2)
#define FORCE_LINK_PASS				(1 << 1)
#define SERIAL_PORT_ENABLE			(1 << 0)

#define DEFAULT_RX_QUEUE_SIZE		400
#define DEFAULT_TX_QUEUE_SIZE		800


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
	 * Protects access to SMI_REG, which is shared between ports.
	 */
	spinlock_t phy_lock;

	/*
	 * Per-port MBUS window access register value.
	 */
	u32 win_protect;

	/*
	 * Hardware-specific parameters.
	 */
	unsigned int t_clk;
	int extended_rx_coal_limit;
	int tx_bw_control_moved;
};


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

	struct timer_list rx_oom;
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
	struct sk_buff **tx_skb;
};

struct mv643xx_eth_private {
	struct mv643xx_eth_shared_private *shared;
	int port_num;

	struct net_device *dev;

	struct mv643xx_eth_shared_private *shared_smi;
	int phy_addr;

	spinlock_t lock;

	struct mib_counters mib_counters;
	struct work_struct tx_timeout_task;
	struct mii_if_info mii;

	/*
	 * RX state.
	 */
	int default_rx_ring_size;
	unsigned long rx_desc_sram_addr;
	int rx_desc_sram_size;
	u8 rxq_mask;
	int rxq_primary;
	struct napi_struct napi;
	struct rx_queue rxq[8];

	/*
	 * TX state.
	 */
	int default_tx_ring_size;
	unsigned long tx_desc_sram_addr;
	int tx_desc_sram_size;
	u8 txq_mask;
	int txq_primary;
	struct tx_queue txq[8];
#ifdef MV643XX_ETH_TX_FAST_REFILL
	int tx_clean_threshold;
#endif
};


/* port register accessors **************************************************/
static inline u32 rdl(struct mv643xx_eth_private *mp, int offset)
{
	return readl(mp->shared->base + offset);
}

static inline void wrl(struct mv643xx_eth_private *mp, int offset, u32 data)
{
	writel(data, mp->shared->base + offset);
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
	wrl(mp, RXQ_COMMAND(mp->port_num), 1 << rxq->index);
}

static void rxq_disable(struct rx_queue *rxq)
{
	struct mv643xx_eth_private *mp = rxq_to_mp(rxq);
	u8 mask = 1 << rxq->index;

	wrl(mp, RXQ_COMMAND(mp->port_num), mask << 8);
	while (rdl(mp, RXQ_COMMAND(mp->port_num)) & mask)
		udelay(10);
}

static void txq_reset_hw_ptr(struct tx_queue *txq)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	int off = TXQ_CURRENT_DESC_PTR(mp->port_num, txq->index);
	u32 addr;

	addr = (u32)txq->tx_desc_dma;
	addr += txq->tx_curr_desc * sizeof(struct tx_desc);
	wrl(mp, off, addr);
}

static void txq_enable(struct tx_queue *txq)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	wrl(mp, TXQ_COMMAND(mp->port_num), 1 << txq->index);
}

static void txq_disable(struct tx_queue *txq)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	u8 mask = 1 << txq->index;

	wrl(mp, TXQ_COMMAND(mp->port_num), mask << 8);
	while (rdl(mp, TXQ_COMMAND(mp->port_num)) & mask)
		udelay(10);
}

static void __txq_maybe_wake(struct tx_queue *txq)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);

	/*
	 * netif_{stop,wake}_queue() flow control only applies to
	 * the primary queue.
	 */
	BUG_ON(txq->index != mp->txq_primary);

	if (txq->tx_ring_size - txq->tx_desc_count >= MAX_DESCS_PER_SKB)
		netif_wake_queue(mp->dev);
}


/* rx ***********************************************************************/
static void txq_reclaim(struct tx_queue *txq, int force);

static void rxq_refill(struct rx_queue *rxq)
{
	struct mv643xx_eth_private *mp = rxq_to_mp(rxq);
	unsigned long flags;

	spin_lock_irqsave(&mp->lock, flags);

	while (rxq->rx_desc_count < rxq->rx_ring_size) {
		int skb_size;
		struct sk_buff *skb;
		int unaligned;
		int rx;

		/*
		 * Reserve 2+14 bytes for an ethernet header (the
		 * hardware automatically prepends 2 bytes of dummy
		 * data to each received packet), 4 bytes for a VLAN
		 * header, and 4 bytes for the trailing FCS -- 24
		 * bytes total.
		 */
		skb_size = mp->dev->mtu + 24;

		skb = dev_alloc_skb(skb_size + dma_get_cache_alignment() - 1);
		if (skb == NULL)
			break;

		unaligned = (u32)skb->data & (dma_get_cache_alignment() - 1);
		if (unaligned)
			skb_reserve(skb, dma_get_cache_alignment() - unaligned);

		rxq->rx_desc_count++;
		rx = rxq->rx_used_desc;
		rxq->rx_used_desc = (rx + 1) % rxq->rx_ring_size;

		rxq->rx_desc_area[rx].buf_ptr = dma_map_single(NULL, skb->data,
						skb_size, DMA_FROM_DEVICE);
		rxq->rx_desc_area[rx].buf_size = skb_size;
		rxq->rx_skb[rx] = skb;
		wmb();
		rxq->rx_desc_area[rx].cmd_sts = BUFFER_OWNED_BY_DMA |
						RX_ENABLE_INTERRUPT;
		wmb();

		/*
		 * The hardware automatically prepends 2 bytes of
		 * dummy data to each received packet, so that the
		 * IP header ends up 16-byte aligned.
		 */
		skb_reserve(skb, 2);
	}

	if (rxq->rx_desc_count != rxq->rx_ring_size)
		mod_timer(&rxq->rx_oom, jiffies + (HZ / 10));

	spin_unlock_irqrestore(&mp->lock, flags);
}

static inline void rxq_refill_timer_wrapper(unsigned long data)
{
	rxq_refill((struct rx_queue *)data);
}

static int rxq_process(struct rx_queue *rxq, int budget)
{
	struct mv643xx_eth_private *mp = rxq_to_mp(rxq);
	struct net_device_stats *stats = &mp->dev->stats;
	int rx;

	rx = 0;
	while (rx < budget) {
		struct rx_desc *rx_desc;
		unsigned int cmd_sts;
		struct sk_buff *skb;
		unsigned long flags;

		spin_lock_irqsave(&mp->lock, flags);

		rx_desc = &rxq->rx_desc_area[rxq->rx_curr_desc];

		cmd_sts = rx_desc->cmd_sts;
		if (cmd_sts & BUFFER_OWNED_BY_DMA) {
			spin_unlock_irqrestore(&mp->lock, flags);
			break;
		}
		rmb();

		skb = rxq->rx_skb[rxq->rx_curr_desc];
		rxq->rx_skb[rxq->rx_curr_desc] = NULL;

		rxq->rx_curr_desc = (rxq->rx_curr_desc + 1) % rxq->rx_ring_size;

		spin_unlock_irqrestore(&mp->lock, flags);

		dma_unmap_single(NULL, rx_desc->buf_ptr + 2,
				 mp->dev->mtu + 24, DMA_FROM_DEVICE);
		rxq->rx_desc_count--;
		rx++;

		/*
		 * Update statistics.
		 *
		 * Note that the descriptor byte count includes 2 dummy
		 * bytes automatically inserted by the hardware at the
		 * start of the packet (which we don't count), and a 4
		 * byte CRC at the end of the packet (which we do count).
		 */
		stats->rx_packets++;
		stats->rx_bytes += rx_desc->byte_cnt - 2;

		/*
		 * In case we received a packet without first / last bits
		 * on, or the error summary bit is set, the packet needs
		 * to be dropped.
		 */
		if (((cmd_sts & (RX_FIRST_DESC | RX_LAST_DESC)) !=
					(RX_FIRST_DESC | RX_LAST_DESC))
				|| (cmd_sts & ERROR_SUMMARY)) {
			stats->rx_dropped++;

			if ((cmd_sts & (RX_FIRST_DESC | RX_LAST_DESC)) !=
				(RX_FIRST_DESC | RX_LAST_DESC)) {
				if (net_ratelimit())
					dev_printk(KERN_ERR, &mp->dev->dev,
						   "received packet spanning "
						   "multiple descriptors\n");
			}

			if (cmd_sts & ERROR_SUMMARY)
				stats->rx_errors++;

			dev_kfree_skb_irq(skb);
		} else {
			/*
			 * The -4 is for the CRC in the trailer of the
			 * received packet
			 */
			skb_put(skb, rx_desc->byte_cnt - 2 - 4);

			if (cmd_sts & LAYER_4_CHECKSUM_OK) {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
				skb->csum = htons(
					(cmd_sts & 0x0007fff8) >> 3);
			}
			skb->protocol = eth_type_trans(skb, mp->dev);
#ifdef MV643XX_ETH_NAPI
			netif_receive_skb(skb);
#else
			netif_rx(skb);
#endif
		}

		mp->dev->last_rx = jiffies;
	}

	rxq_refill(rxq);

	return rx;
}

#ifdef MV643XX_ETH_NAPI
static int mv643xx_eth_poll(struct napi_struct *napi, int budget)
{
	struct mv643xx_eth_private *mp;
	int rx;
	int i;

	mp = container_of(napi, struct mv643xx_eth_private, napi);

#ifdef MV643XX_ETH_TX_FAST_REFILL
	if (++mp->tx_clean_threshold > 5) {
		mp->tx_clean_threshold = 0;
		for (i = 0; i < 8; i++)
			if (mp->txq_mask & (1 << i))
				txq_reclaim(mp->txq + i, 0);

		if (netif_carrier_ok(mp->dev)) {
			spin_lock_irq(&mp->lock);
			__txq_maybe_wake(mp->txq + mp->txq_primary);
			spin_unlock_irq(&mp->lock);
		}
	}
#endif

	rx = 0;
	for (i = 7; rx < budget && i >= 0; i--)
		if (mp->rxq_mask & (1 << i))
			rx += rxq_process(mp->rxq + i, budget - rx);

	if (rx < budget) {
		netif_rx_complete(mp->dev, napi);
		wrl(mp, INT_MASK(mp->port_num), INT_TX_END | INT_RX | INT_EXT);
	}

	return rx;
}
#endif


/* tx ***********************************************************************/
static inline unsigned int has_tiny_unaligned_frags(struct sk_buff *skb)
{
	int frag;

	for (frag = 0; frag < skb_shinfo(skb)->nr_frags; frag++) {
		skb_frag_t *fragp = &skb_shinfo(skb)->frags[frag];
		if (fragp->size <= 8 && fragp->page_offset & 7)
			return 1;
	}

	return 0;
}

static int txq_alloc_desc_index(struct tx_queue *txq)
{
	int tx_desc_curr;

	BUG_ON(txq->tx_desc_count >= txq->tx_ring_size);

	tx_desc_curr = txq->tx_curr_desc;
	txq->tx_curr_desc = (tx_desc_curr + 1) % txq->tx_ring_size;

	BUG_ON(txq->tx_curr_desc == txq->tx_used_desc);

	return tx_desc_curr;
}

static void txq_submit_frag_skb(struct tx_queue *txq, struct sk_buff *skb)
{
	int nr_frags = skb_shinfo(skb)->nr_frags;
	int frag;

	for (frag = 0; frag < nr_frags; frag++) {
		skb_frag_t *this_frag;
		int tx_index;
		struct tx_desc *desc;

		this_frag = &skb_shinfo(skb)->frags[frag];
		tx_index = txq_alloc_desc_index(txq);
		desc = &txq->tx_desc_area[tx_index];

		/*
		 * The last fragment will generate an interrupt
		 * which will free the skb on TX completion.
		 */
		if (frag == nr_frags - 1) {
			desc->cmd_sts = BUFFER_OWNED_BY_DMA |
					ZERO_PADDING | TX_LAST_DESC |
					TX_ENABLE_INTERRUPT;
			txq->tx_skb[tx_index] = skb;
		} else {
			desc->cmd_sts = BUFFER_OWNED_BY_DMA;
			txq->tx_skb[tx_index] = NULL;
		}

		desc->l4i_chk = 0;
		desc->byte_cnt = this_frag->size;
		desc->buf_ptr = dma_map_page(NULL, this_frag->page,
						this_frag->page_offset,
						this_frag->size,
						DMA_TO_DEVICE);
	}
}

static inline __be16 sum16_as_be(__sum16 sum)
{
	return (__force __be16)sum;
}

static void txq_submit_skb(struct tx_queue *txq, struct sk_buff *skb)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	int nr_frags = skb_shinfo(skb)->nr_frags;
	int tx_index;
	struct tx_desc *desc;
	u32 cmd_sts;
	int length;

	cmd_sts = TX_FIRST_DESC | GEN_CRC | BUFFER_OWNED_BY_DMA;

	tx_index = txq_alloc_desc_index(txq);
	desc = &txq->tx_desc_area[tx_index];

	if (nr_frags) {
		txq_submit_frag_skb(txq, skb);

		length = skb_headlen(skb);
		txq->tx_skb[tx_index] = NULL;
	} else {
		cmd_sts |= ZERO_PADDING | TX_LAST_DESC | TX_ENABLE_INTERRUPT;
		length = skb->len;
		txq->tx_skb[tx_index] = skb;
	}

	desc->byte_cnt = length;
	desc->buf_ptr = dma_map_single(NULL, skb->data, length, DMA_TO_DEVICE);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		int mac_hdr_len;

		BUG_ON(skb->protocol != htons(ETH_P_IP) &&
		       skb->protocol != htons(ETH_P_8021Q));

		cmd_sts |= GEN_TCP_UDP_CHECKSUM |
			   GEN_IP_V4_CHECKSUM   |
			   ip_hdr(skb)->ihl << TX_IHL_SHIFT;

		mac_hdr_len = (void *)ip_hdr(skb) - (void *)skb->data;
		switch (mac_hdr_len - ETH_HLEN) {
		case 0:
			break;
		case 4:
			cmd_sts |= MAC_HDR_EXTRA_4_BYTES;
			break;
		case 8:
			cmd_sts |= MAC_HDR_EXTRA_8_BYTES;
			break;
		case 12:
			cmd_sts |= MAC_HDR_EXTRA_4_BYTES;
			cmd_sts |= MAC_HDR_EXTRA_8_BYTES;
			break;
		default:
			if (net_ratelimit())
				dev_printk(KERN_ERR, &txq_to_mp(txq)->dev->dev,
				   "mac header length is %d?!\n", mac_hdr_len);
			break;
		}

		switch (ip_hdr(skb)->protocol) {
		case IPPROTO_UDP:
			cmd_sts |= UDP_FRAME;
			desc->l4i_chk = ntohs(sum16_as_be(udp_hdr(skb)->check));
			break;
		case IPPROTO_TCP:
			desc->l4i_chk = ntohs(sum16_as_be(tcp_hdr(skb)->check));
			break;
		default:
			BUG();
		}
	} else {
		/* Errata BTS #50, IHL must be 5 if no HW checksum */
		cmd_sts |= 5 << TX_IHL_SHIFT;
		desc->l4i_chk = 0;
	}

	/* ensure all other descriptors are written before first cmd_sts */
	wmb();
	desc->cmd_sts = cmd_sts;

	/* clear TX_END interrupt status */
	wrl(mp, INT_CAUSE(mp->port_num), ~(INT_TX_END_0 << txq->index));
	rdl(mp, INT_CAUSE(mp->port_num));

	/* ensure all descriptors are written before poking hardware */
	wmb();
	txq_enable(txq);

	txq->tx_desc_count += nr_frags + 1;
}

static int mv643xx_eth_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct tx_queue *txq;
	unsigned long flags;

	if (has_tiny_unaligned_frags(skb) && __skb_linearize(skb)) {
		stats->tx_dropped++;
		dev_printk(KERN_DEBUG, &dev->dev,
			   "failed to linearize skb with tiny "
			   "unaligned fragment\n");
		return NETDEV_TX_BUSY;
	}

	spin_lock_irqsave(&mp->lock, flags);

	txq = mp->txq + mp->txq_primary;

	if (txq->tx_ring_size - txq->tx_desc_count < MAX_DESCS_PER_SKB) {
		spin_unlock_irqrestore(&mp->lock, flags);
		if (txq->index == mp->txq_primary && net_ratelimit())
			dev_printk(KERN_ERR, &dev->dev,
				   "primary tx queue full?!\n");
		kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	txq_submit_skb(txq, skb);
	stats->tx_bytes += skb->len;
	stats->tx_packets++;
	dev->trans_start = jiffies;

	if (txq->index == mp->txq_primary) {
		int entries_left;

		entries_left = txq->tx_ring_size - txq->tx_desc_count;
		if (entries_left < MAX_DESCS_PER_SKB)
			netif_stop_queue(dev);
	}

	spin_unlock_irqrestore(&mp->lock, flags);

	return NETDEV_TX_OK;
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

	token_rate = ((rate / 1000) * 64) / (mp->shared->t_clk / 1000);
	if (token_rate > 1023)
		token_rate = 1023;

	mtu = (mp->dev->mtu + 255) >> 8;
	if (mtu > 63)
		mtu = 63;

	bucket_size = (burst + 255) >> 8;
	if (bucket_size > 65535)
		bucket_size = 65535;

	if (mp->shared->tx_bw_control_moved) {
		wrl(mp, TX_BW_RATE_MOVED(mp->port_num), token_rate);
		wrl(mp, TX_BW_MTU_MOVED(mp->port_num), mtu);
		wrl(mp, TX_BW_BURST_MOVED(mp->port_num), bucket_size);
	} else {
		wrl(mp, TX_BW_RATE(mp->port_num), token_rate);
		wrl(mp, TX_BW_MTU(mp->port_num), mtu);
		wrl(mp, TX_BW_BURST(mp->port_num), bucket_size);
	}
}

static void txq_set_rate(struct tx_queue *txq, int rate, int burst)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	int token_rate;
	int bucket_size;

	token_rate = ((rate / 1000) * 64) / (mp->shared->t_clk / 1000);
	if (token_rate > 1023)
		token_rate = 1023;

	bucket_size = (burst + 255) >> 8;
	if (bucket_size > 65535)
		bucket_size = 65535;

	wrl(mp, TXQ_BW_TOKENS(mp->port_num, txq->index), token_rate << 14);
	wrl(mp, TXQ_BW_CONF(mp->port_num, txq->index),
			(bucket_size << 10) | token_rate);
}

static void txq_set_fixed_prio_mode(struct tx_queue *txq)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	int off;
	u32 val;

	/*
	 * Turn on fixed priority mode.
	 */
	if (mp->shared->tx_bw_control_moved)
		off = TXQ_FIX_PRIO_CONF_MOVED(mp->port_num);
	else
		off = TXQ_FIX_PRIO_CONF(mp->port_num);

	val = rdl(mp, off);
	val |= 1 << txq->index;
	wrl(mp, off, val);
}

static void txq_set_wrr(struct tx_queue *txq, int weight)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	int off;
	u32 val;

	/*
	 * Turn off fixed priority mode.
	 */
	if (mp->shared->tx_bw_control_moved)
		off = TXQ_FIX_PRIO_CONF_MOVED(mp->port_num);
	else
		off = TXQ_FIX_PRIO_CONF(mp->port_num);

	val = rdl(mp, off);
	val &= ~(1 << txq->index);
	wrl(mp, off, val);

	/*
	 * Configure WRR weight for this queue.
	 */
	off = TXQ_BW_WRR_CONF(mp->port_num, txq->index);

	val = rdl(mp, off);
	val = (val & ~0xff) | (weight & 0xff);
	wrl(mp, off, val);
}


/* mii management interface *************************************************/
#define SMI_BUSY		0x10000000
#define SMI_READ_VALID		0x08000000
#define SMI_OPCODE_READ		0x04000000
#define SMI_OPCODE_WRITE	0x00000000

static void smi_reg_read(struct mv643xx_eth_private *mp, unsigned int addr,
			 unsigned int reg, unsigned int *value)
{
	void __iomem *smi_reg = mp->shared_smi->base + SMI_REG;
	unsigned long flags;
	int i;

	/* the SMI register is a shared resource */
	spin_lock_irqsave(&mp->shared_smi->phy_lock, flags);

	/* wait for the SMI register to become available */
	for (i = 0; readl(smi_reg) & SMI_BUSY; i++) {
		if (i == 1000) {
			printk("%s: PHY busy timeout\n", mp->dev->name);
			goto out;
		}
		udelay(10);
	}

	writel(SMI_OPCODE_READ | (reg << 21) | (addr << 16), smi_reg);

	/* now wait for the data to be valid */
	for (i = 0; !(readl(smi_reg) & SMI_READ_VALID); i++) {
		if (i == 1000) {
			printk("%s: PHY read timeout\n", mp->dev->name);
			goto out;
		}
		udelay(10);
	}

	*value = readl(smi_reg) & 0xffff;
out:
	spin_unlock_irqrestore(&mp->shared_smi->phy_lock, flags);
}

static void smi_reg_write(struct mv643xx_eth_private *mp,
			  unsigned int addr,
			  unsigned int reg, unsigned int value)
{
	void __iomem *smi_reg = mp->shared_smi->base + SMI_REG;
	unsigned long flags;
	int i;

	/* the SMI register is a shared resource */
	spin_lock_irqsave(&mp->shared_smi->phy_lock, flags);

	/* wait for the SMI register to become available */
	for (i = 0; readl(smi_reg) & SMI_BUSY; i++) {
		if (i == 1000) {
			printk("%s: PHY busy timeout\n", mp->dev->name);
			goto out;
		}
		udelay(10);
	}

	writel(SMI_OPCODE_WRITE | (reg << 21) |
		(addr << 16) | (value & 0xffff), smi_reg);
out:
	spin_unlock_irqrestore(&mp->shared_smi->phy_lock, flags);
}


/* mib counters *************************************************************/
static inline u32 mib_read(struct mv643xx_eth_private *mp, int offset)
{
	return rdl(mp, MIB_COUNTERS(mp->port_num) + offset);
}

static void mib_counters_clear(struct mv643xx_eth_private *mp)
{
	int i;

	for (i = 0; i < 0x80; i += 4)
		mib_read(mp, i);
}

static void mib_counters_update(struct mv643xx_eth_private *mp)
{
	struct mib_counters *p = &mp->mib_counters;

	p->good_octets_received += mib_read(mp, 0x00);
	p->good_octets_received += (u64)mib_read(mp, 0x04) << 32;
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
	p->good_octets_sent += (u64)mib_read(mp, 0x3c) << 32;
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
};

static int mv643xx_eth_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int err;

	spin_lock_irq(&mp->lock);
	err = mii_ethtool_gset(&mp->mii, cmd);
	spin_unlock_irq(&mp->lock);

	/*
	 * The MAC does not support 1000baseT_Half.
	 */
	cmd->supported &= ~SUPPORTED_1000baseT_Half;
	cmd->advertising &= ~ADVERTISED_1000baseT_Half;

	return err;
}

static int mv643xx_eth_get_settings_phyless(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	u32 port_status;

	port_status = rdl(mp, PORT_STATUS(mp->port_num));

	cmd->supported = SUPPORTED_MII;
	cmd->advertising = ADVERTISED_MII;
	switch (port_status & PORT_SPEED_MASK) {
	case PORT_SPEED_10:
		cmd->speed = SPEED_10;
		break;
	case PORT_SPEED_100:
		cmd->speed = SPEED_100;
		break;
	case PORT_SPEED_1000:
		cmd->speed = SPEED_1000;
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

static int mv643xx_eth_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int err;

	/*
	 * The MAC does not support 1000baseT_Half.
	 */
	cmd->advertising &= ~ADVERTISED_1000baseT_Half;

	spin_lock_irq(&mp->lock);
	err = mii_ethtool_sset(&mp->mii, cmd);
	spin_unlock_irq(&mp->lock);

	return err;
}

static int mv643xx_eth_set_settings_phyless(struct net_device *dev, struct ethtool_cmd *cmd)
{
	return -EINVAL;
}

static void mv643xx_eth_get_drvinfo(struct net_device *dev,
				    struct ethtool_drvinfo *drvinfo)
{
	strncpy(drvinfo->driver,  mv643xx_eth_driver_name, 32);
	strncpy(drvinfo->version, mv643xx_eth_driver_version, 32);
	strncpy(drvinfo->fw_version, "N/A", 32);
	strncpy(drvinfo->bus_info, "platform", 32);
	drvinfo->n_stats = ARRAY_SIZE(mv643xx_eth_stats);
}

static int mv643xx_eth_nway_reset(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	return mii_nway_restart(&mp->mii);
}

static int mv643xx_eth_nway_reset_phyless(struct net_device *dev)
{
	return -EINVAL;
}

static u32 mv643xx_eth_get_link(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	return mii_link_ok(&mp->mii);
}

static u32 mv643xx_eth_get_link_phyless(struct net_device *dev)
{
	return 1;
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
	struct mv643xx_eth_private *mp = dev->priv;
	int i;

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
	.get_link		= mv643xx_eth_get_link,
	.set_sg			= ethtool_op_set_sg,
	.get_strings		= mv643xx_eth_get_strings,
	.get_ethtool_stats	= mv643xx_eth_get_ethtool_stats,
	.get_sset_count		= mv643xx_eth_get_sset_count,
};

static const struct ethtool_ops mv643xx_eth_ethtool_ops_phyless = {
	.get_settings		= mv643xx_eth_get_settings_phyless,
	.set_settings		= mv643xx_eth_set_settings_phyless,
	.get_drvinfo		= mv643xx_eth_get_drvinfo,
	.nway_reset		= mv643xx_eth_nway_reset_phyless,
	.get_link		= mv643xx_eth_get_link_phyless,
	.set_sg			= ethtool_op_set_sg,
	.get_strings		= mv643xx_eth_get_strings,
	.get_ethtool_stats	= mv643xx_eth_get_ethtool_stats,
	.get_sset_count		= mv643xx_eth_get_sset_count,
};


/* address handling *********************************************************/
static void uc_addr_get(struct mv643xx_eth_private *mp, unsigned char *addr)
{
	unsigned int mac_h;
	unsigned int mac_l;

	mac_h = rdl(mp, MAC_ADDR_HIGH(mp->port_num));
	mac_l = rdl(mp, MAC_ADDR_LOW(mp->port_num));

	addr[0] = (mac_h >> 24) & 0xff;
	addr[1] = (mac_h >> 16) & 0xff;
	addr[2] = (mac_h >> 8) & 0xff;
	addr[3] = mac_h & 0xff;
	addr[4] = (mac_l >> 8) & 0xff;
	addr[5] = mac_l & 0xff;
}

static void init_mac_tables(struct mv643xx_eth_private *mp)
{
	int i;

	for (i = 0; i < 0x100; i += 4) {
		wrl(mp, SPECIAL_MCAST_TABLE(mp->port_num) + i, 0);
		wrl(mp, OTHER_MCAST_TABLE(mp->port_num) + i, 0);
	}

	for (i = 0; i < 0x10; i += 4)
		wrl(mp, UNICAST_TABLE(mp->port_num) + i, 0);
}

static void set_filter_table_entry(struct mv643xx_eth_private *mp,
				   int table, unsigned char entry)
{
	unsigned int table_reg;

	/* Set "accepts frame bit" at specified table entry */
	table_reg = rdl(mp, table + (entry & 0xfc));
	table_reg |= 0x01 << (8 * (entry & 3));
	wrl(mp, table + (entry & 0xfc), table_reg);
}

static void uc_addr_set(struct mv643xx_eth_private *mp, unsigned char *addr)
{
	unsigned int mac_h;
	unsigned int mac_l;
	int table;

	mac_l = (addr[4] << 8) | addr[5];
	mac_h = (addr[0] << 24) | (addr[1] << 16) | (addr[2] << 8) | addr[3];

	wrl(mp, MAC_ADDR_LOW(mp->port_num), mac_l);
	wrl(mp, MAC_ADDR_HIGH(mp->port_num), mac_h);

	table = UNICAST_TABLE(mp->port_num);
	set_filter_table_entry(mp, table, addr[5] & 0x0f);
}

static int mv643xx_eth_set_mac_address(struct net_device *dev, void *addr)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	/* +2 is for the offset of the HW addr type */
	memcpy(dev->dev_addr, addr + 2, 6);

	init_mac_tables(mp);
	uc_addr_set(mp, dev->dev_addr);

	return 0;
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

static void mv643xx_eth_set_rx_mode(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	u32 port_config;
	struct dev_addr_list *addr;
	int i;

	port_config = rdl(mp, PORT_CONFIG(mp->port_num));
	if (dev->flags & IFF_PROMISC)
		port_config |= UNICAST_PROMISCUOUS_MODE;
	else
		port_config &= ~UNICAST_PROMISCUOUS_MODE;
	wrl(mp, PORT_CONFIG(mp->port_num), port_config);

	if (dev->flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		int port_num = mp->port_num;
		u32 accept = 0x01010101;

		for (i = 0; i < 0x100; i += 4) {
			wrl(mp, SPECIAL_MCAST_TABLE(port_num) + i, accept);
			wrl(mp, OTHER_MCAST_TABLE(port_num) + i, accept);
		}
		return;
	}

	for (i = 0; i < 0x100; i += 4) {
		wrl(mp, SPECIAL_MCAST_TABLE(mp->port_num) + i, 0);
		wrl(mp, OTHER_MCAST_TABLE(mp->port_num) + i, 0);
	}

	for (addr = dev->mc_list; addr != NULL; addr = addr->next) {
		u8 *a = addr->da_addr;
		int table;

		if (addr->da_addrlen != 6)
			continue;

		if (memcmp(a, "\x01\x00\x5e\x00\x00", 5) == 0) {
			table = SPECIAL_MCAST_TABLE(mp->port_num);
			set_filter_table_entry(mp, table, a[5]);
		} else {
			int crc = addr_crc(a);

			table = OTHER_MCAST_TABLE(mp->port_num);
			set_filter_table_entry(mp, table, crc);
		}
	}
}


/* rx/tx queue initialisation ***********************************************/
static int rxq_init(struct mv643xx_eth_private *mp, int index)
{
	struct rx_queue *rxq = mp->rxq + index;
	struct rx_desc *rx_desc;
	int size;
	int i;

	rxq->index = index;

	rxq->rx_ring_size = mp->default_rx_ring_size;

	rxq->rx_desc_count = 0;
	rxq->rx_curr_desc = 0;
	rxq->rx_used_desc = 0;

	size = rxq->rx_ring_size * sizeof(struct rx_desc);

	if (index == mp->rxq_primary && size <= mp->rx_desc_sram_size) {
		rxq->rx_desc_area = ioremap(mp->rx_desc_sram_addr,
						mp->rx_desc_sram_size);
		rxq->rx_desc_dma = mp->rx_desc_sram_addr;
	} else {
		rxq->rx_desc_area = dma_alloc_coherent(NULL, size,
							&rxq->rx_desc_dma,
							GFP_KERNEL);
	}

	if (rxq->rx_desc_area == NULL) {
		dev_printk(KERN_ERR, &mp->dev->dev,
			   "can't allocate rx ring (%d bytes)\n", size);
		goto out;
	}
	memset(rxq->rx_desc_area, 0, size);

	rxq->rx_desc_area_size = size;
	rxq->rx_skb = kmalloc(rxq->rx_ring_size * sizeof(*rxq->rx_skb),
								GFP_KERNEL);
	if (rxq->rx_skb == NULL) {
		dev_printk(KERN_ERR, &mp->dev->dev,
			   "can't allocate rx skb ring\n");
		goto out_free;
	}

	rx_desc = (struct rx_desc *)rxq->rx_desc_area;
	for (i = 0; i < rxq->rx_ring_size; i++) {
		int nexti = (i + 1) % rxq->rx_ring_size;
		rx_desc[i].next_desc_ptr = rxq->rx_desc_dma +
					nexti * sizeof(struct rx_desc);
	}

	init_timer(&rxq->rx_oom);
	rxq->rx_oom.data = (unsigned long)rxq;
	rxq->rx_oom.function = rxq_refill_timer_wrapper;

	return 0;


out_free:
	if (index == mp->rxq_primary && size <= mp->rx_desc_sram_size)
		iounmap(rxq->rx_desc_area);
	else
		dma_free_coherent(NULL, size,
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

	del_timer_sync(&rxq->rx_oom);

	for (i = 0; i < rxq->rx_ring_size; i++) {
		if (rxq->rx_skb[i]) {
			dev_kfree_skb(rxq->rx_skb[i]);
			rxq->rx_desc_count--;
		}
	}

	if (rxq->rx_desc_count) {
		dev_printk(KERN_ERR, &mp->dev->dev,
			   "error freeing rx ring -- %d skbs stuck\n",
			   rxq->rx_desc_count);
	}

	if (rxq->index == mp->rxq_primary &&
	    rxq->rx_desc_area_size <= mp->rx_desc_sram_size)
		iounmap(rxq->rx_desc_area);
	else
		dma_free_coherent(NULL, rxq->rx_desc_area_size,
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

	txq->tx_ring_size = mp->default_tx_ring_size;

	txq->tx_desc_count = 0;
	txq->tx_curr_desc = 0;
	txq->tx_used_desc = 0;

	size = txq->tx_ring_size * sizeof(struct tx_desc);

	if (index == mp->txq_primary && size <= mp->tx_desc_sram_size) {
		txq->tx_desc_area = ioremap(mp->tx_desc_sram_addr,
						mp->tx_desc_sram_size);
		txq->tx_desc_dma = mp->tx_desc_sram_addr;
	} else {
		txq->tx_desc_area = dma_alloc_coherent(NULL, size,
							&txq->tx_desc_dma,
							GFP_KERNEL);
	}

	if (txq->tx_desc_area == NULL) {
		dev_printk(KERN_ERR, &mp->dev->dev,
			   "can't allocate tx ring (%d bytes)\n", size);
		goto out;
	}
	memset(txq->tx_desc_area, 0, size);

	txq->tx_desc_area_size = size;
	txq->tx_skb = kmalloc(txq->tx_ring_size * sizeof(*txq->tx_skb),
								GFP_KERNEL);
	if (txq->tx_skb == NULL) {
		dev_printk(KERN_ERR, &mp->dev->dev,
			   "can't allocate tx skb ring\n");
		goto out_free;
	}

	tx_desc = (struct tx_desc *)txq->tx_desc_area;
	for (i = 0; i < txq->tx_ring_size; i++) {
		struct tx_desc *txd = tx_desc + i;
		int nexti = (i + 1) % txq->tx_ring_size;

		txd->cmd_sts = 0;
		txd->next_desc_ptr = txq->tx_desc_dma +
					nexti * sizeof(struct tx_desc);
	}

	return 0;


out_free:
	if (index == mp->txq_primary && size <= mp->tx_desc_sram_size)
		iounmap(txq->tx_desc_area);
	else
		dma_free_coherent(NULL, size,
				  txq->tx_desc_area,
				  txq->tx_desc_dma);

out:
	return -ENOMEM;
}

static void txq_reclaim(struct tx_queue *txq, int force)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);
	unsigned long flags;

	spin_lock_irqsave(&mp->lock, flags);
	while (txq->tx_desc_count > 0) {
		int tx_index;
		struct tx_desc *desc;
		u32 cmd_sts;
		struct sk_buff *skb;
		dma_addr_t addr;
		int count;

		tx_index = txq->tx_used_desc;
		desc = &txq->tx_desc_area[tx_index];
		cmd_sts = desc->cmd_sts;

		if (cmd_sts & BUFFER_OWNED_BY_DMA) {
			if (!force)
				break;
			desc->cmd_sts = cmd_sts & ~BUFFER_OWNED_BY_DMA;
		}

		txq->tx_used_desc = (tx_index + 1) % txq->tx_ring_size;
		txq->tx_desc_count--;

		addr = desc->buf_ptr;
		count = desc->byte_cnt;
		skb = txq->tx_skb[tx_index];
		txq->tx_skb[tx_index] = NULL;

		if (cmd_sts & ERROR_SUMMARY) {
			dev_printk(KERN_INFO, &mp->dev->dev, "tx error\n");
			mp->dev->stats.tx_errors++;
		}

		/*
		 * Drop mp->lock while we free the skb.
		 */
		spin_unlock_irqrestore(&mp->lock, flags);

		if (cmd_sts & TX_FIRST_DESC)
			dma_unmap_single(NULL, addr, count, DMA_TO_DEVICE);
		else
			dma_unmap_page(NULL, addr, count, DMA_TO_DEVICE);

		if (skb)
			dev_kfree_skb_irq(skb);

		spin_lock_irqsave(&mp->lock, flags);
	}
	spin_unlock_irqrestore(&mp->lock, flags);
}

static void txq_deinit(struct tx_queue *txq)
{
	struct mv643xx_eth_private *mp = txq_to_mp(txq);

	txq_disable(txq);
	txq_reclaim(txq, 1);

	BUG_ON(txq->tx_used_desc != txq->tx_curr_desc);

	if (txq->index == mp->txq_primary &&
	    txq->tx_desc_area_size <= mp->tx_desc_sram_size)
		iounmap(txq->tx_desc_area);
	else
		dma_free_coherent(NULL, txq->tx_desc_area_size,
				  txq->tx_desc_area, txq->tx_desc_dma);

	kfree(txq->tx_skb);
}


/* netdev ops and related ***************************************************/
static void handle_link_event(struct mv643xx_eth_private *mp)
{
	struct net_device *dev = mp->dev;
	u32 port_status;
	int speed;
	int duplex;
	int fc;

	port_status = rdl(mp, PORT_STATUS(mp->port_num));
	if (!(port_status & LINK_UP)) {
		if (netif_carrier_ok(dev)) {
			int i;

			printk(KERN_INFO "%s: link down\n", dev->name);

			netif_carrier_off(dev);
			netif_stop_queue(dev);

			for (i = 0; i < 8; i++) {
				struct tx_queue *txq = mp->txq + i;

				if (mp->txq_mask & (1 << i)) {
					txq_reclaim(txq, 1);
					txq_reset_hw_ptr(txq);
				}
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

	printk(KERN_INFO "%s: link up, %d Mb/s, %s duplex, "
			 "flow control %sabled\n", dev->name,
			 speed, duplex ? "full" : "half",
			 fc ? "en" : "dis");

	if (!netif_carrier_ok(dev)) {
		netif_carrier_on(dev);
		netif_wake_queue(dev);
	}
}

static irqreturn_t mv643xx_eth_irq(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	u32 int_cause;
	u32 int_cause_ext;

	int_cause = rdl(mp, INT_CAUSE(mp->port_num)) &
			(INT_TX_END | INT_RX | INT_EXT);
	if (int_cause == 0)
		return IRQ_NONE;

	int_cause_ext = 0;
	if (int_cause & INT_EXT) {
		int_cause_ext = rdl(mp, INT_CAUSE_EXT(mp->port_num))
				& (INT_EXT_LINK | INT_EXT_PHY | INT_EXT_TX);
		wrl(mp, INT_CAUSE_EXT(mp->port_num), ~int_cause_ext);
	}

	if (int_cause_ext & (INT_EXT_PHY | INT_EXT_LINK))
		handle_link_event(mp);

	/*
	 * RxBuffer or RxError set for any of the 8 queues?
	 */
#ifdef MV643XX_ETH_NAPI
	if (int_cause & INT_RX) {
		wrl(mp, INT_CAUSE(mp->port_num), ~(int_cause & INT_RX));
		wrl(mp, INT_MASK(mp->port_num), 0x00000000);
		rdl(mp, INT_MASK(mp->port_num));

		netif_rx_schedule(dev, &mp->napi);
	}
#else
	if (int_cause & INT_RX) {
		int i;

		for (i = 7; i >= 0; i--)
			if (mp->rxq_mask & (1 << i))
				rxq_process(mp->rxq + i, INT_MAX);
	}
#endif

	/*
	 * TxBuffer or TxError set for any of the 8 queues?
	 */
	if (int_cause_ext & INT_EXT_TX) {
		int i;

		for (i = 0; i < 8; i++)
			if (mp->txq_mask & (1 << i))
				txq_reclaim(mp->txq + i, 0);

		/*
		 * Enough space again in the primary TX queue for a
		 * full packet?
		 */
		if (netif_carrier_ok(dev)) {
			spin_lock(&mp->lock);
			__txq_maybe_wake(mp->txq + mp->txq_primary);
			spin_unlock(&mp->lock);
		}
	}

	/*
	 * Any TxEnd interrupts?
	 */
	if (int_cause & INT_TX_END) {
		int i;

		wrl(mp, INT_CAUSE(mp->port_num), ~(int_cause & INT_TX_END));

		spin_lock(&mp->lock);
		for (i = 0; i < 8; i++) {
			struct tx_queue *txq = mp->txq + i;
			u32 hw_desc_ptr;
			u32 expected_ptr;

			if ((int_cause & (INT_TX_END_0 << i)) == 0)
				continue;

			hw_desc_ptr =
				rdl(mp, TXQ_CURRENT_DESC_PTR(mp->port_num, i));
			expected_ptr = (u32)txq->tx_desc_dma +
				txq->tx_curr_desc * sizeof(struct tx_desc);

			if (hw_desc_ptr != expected_ptr)
				txq_enable(txq);
		}
		spin_unlock(&mp->lock);
	}

	return IRQ_HANDLED;
}

static void phy_reset(struct mv643xx_eth_private *mp)
{
	unsigned int data;

	smi_reg_read(mp, mp->phy_addr, MII_BMCR, &data);
	data |= BMCR_RESET;
	smi_reg_write(mp, mp->phy_addr, MII_BMCR, data);

	do {
		udelay(1);
		smi_reg_read(mp, mp->phy_addr, MII_BMCR, &data);
	} while (data & BMCR_RESET);
}

static void port_start(struct mv643xx_eth_private *mp)
{
	u32 pscr;
	int i;

	/*
	 * Perform PHY reset, if there is a PHY.
	 */
	if (mp->phy_addr != -1) {
		struct ethtool_cmd cmd;

		mv643xx_eth_get_settings(mp->dev, &cmd);
		phy_reset(mp);
		mv643xx_eth_set_settings(mp->dev, &cmd);
	}

	/*
	 * Configure basic link parameters.
	 */
	pscr = rdl(mp, PORT_SERIAL_CONTROL(mp->port_num));

	pscr |= SERIAL_PORT_ENABLE;
	wrl(mp, PORT_SERIAL_CONTROL(mp->port_num), pscr);

	pscr |= DO_NOT_FORCE_LINK_FAIL;
	if (mp->phy_addr == -1)
		pscr |= FORCE_LINK_PASS;
	wrl(mp, PORT_SERIAL_CONTROL(mp->port_num), pscr);

	wrl(mp, SDMA_CONFIG(mp->port_num), PORT_SDMA_CONFIG_DEFAULT_VALUE);

	/*
	 * Configure TX path and queues.
	 */
	tx_set_rate(mp, 1000000000, 16777216);
	for (i = 0; i < 8; i++) {
		struct tx_queue *txq = mp->txq + i;

		if ((mp->txq_mask & (1 << i)) == 0)
			continue;

		txq_reset_hw_ptr(txq);
		txq_set_rate(txq, 1000000000, 16777216);
		txq_set_fixed_prio_mode(txq);
	}

	/*
	 * Add configured unicast address to address filter table.
	 */
	uc_addr_set(mp, mp->dev->dev_addr);

	/*
	 * Receive all unmatched unicast, TCP, UDP, BPDU and broadcast
	 * frames to RX queue #0.
	 */
	wrl(mp, PORT_CONFIG(mp->port_num), 0x00000000);

	/*
	 * Treat BPDUs as normal multicasts, and disable partition mode.
	 */
	wrl(mp, PORT_CONFIG_EXT(mp->port_num), 0x00000000);

	/*
	 * Enable the receive queues.
	 */
	for (i = 0; i < 8; i++) {
		struct rx_queue *rxq = mp->rxq + i;
		int off = RXQ_CURRENT_DESC_PTR(mp->port_num, i);
		u32 addr;

		if ((mp->rxq_mask & (1 << i)) == 0)
			continue;

		addr = (u32)rxq->rx_desc_dma;
		addr += rxq->rx_curr_desc * sizeof(struct rx_desc);
		wrl(mp, off, addr);

		rxq_enable(rxq);
	}
}

static void set_rx_coal(struct mv643xx_eth_private *mp, unsigned int delay)
{
	unsigned int coal = ((mp->shared->t_clk / 1000000) * delay) / 64;
	u32 val;

	val = rdl(mp, SDMA_CONFIG(mp->port_num));
	if (mp->shared->extended_rx_coal_limit) {
		if (coal > 0xffff)
			coal = 0xffff;
		val &= ~0x023fff80;
		val |= (coal & 0x8000) << 10;
		val |= (coal & 0x7fff) << 7;
	} else {
		if (coal > 0x3fff)
			coal = 0x3fff;
		val &= ~0x003fff00;
		val |= (coal & 0x3fff) << 8;
	}
	wrl(mp, SDMA_CONFIG(mp->port_num), val);
}

static void set_tx_coal(struct mv643xx_eth_private *mp, unsigned int delay)
{
	unsigned int coal = ((mp->shared->t_clk / 1000000) * delay) / 64;

	if (coal > 0x3fff)
		coal = 0x3fff;
	wrl(mp, TX_FIFO_URGENT_THRESHOLD(mp->port_num), (coal & 0x3fff) << 4);
}

static int mv643xx_eth_open(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int err;
	int i;

	wrl(mp, INT_CAUSE(mp->port_num), 0);
	wrl(mp, INT_CAUSE_EXT(mp->port_num), 0);
	rdl(mp, INT_CAUSE_EXT(mp->port_num));

	err = request_irq(dev->irq, mv643xx_eth_irq,
			  IRQF_SHARED | IRQF_SAMPLE_RANDOM,
			  dev->name, dev);
	if (err) {
		dev_printk(KERN_ERR, &dev->dev, "can't assign irq\n");
		return -EAGAIN;
	}

	init_mac_tables(mp);

	for (i = 0; i < 8; i++) {
		if ((mp->rxq_mask & (1 << i)) == 0)
			continue;

		err = rxq_init(mp, i);
		if (err) {
			while (--i >= 0)
				if (mp->rxq_mask & (1 << i))
					rxq_deinit(mp->rxq + i);
			goto out;
		}

		rxq_refill(mp->rxq + i);
	}

	for (i = 0; i < 8; i++) {
		if ((mp->txq_mask & (1 << i)) == 0)
			continue;

		err = txq_init(mp, i);
		if (err) {
			while (--i >= 0)
				if (mp->txq_mask & (1 << i))
					txq_deinit(mp->txq + i);
			goto out_free;
		}
	}

#ifdef MV643XX_ETH_NAPI
	napi_enable(&mp->napi);
#endif

	netif_carrier_off(dev);
	netif_stop_queue(dev);

	port_start(mp);

	set_rx_coal(mp, 0);
	set_tx_coal(mp, 0);

	wrl(mp, INT_MASK_EXT(mp->port_num),
	    INT_EXT_LINK | INT_EXT_PHY | INT_EXT_TX);

	wrl(mp, INT_MASK(mp->port_num), INT_TX_END | INT_RX | INT_EXT);

	return 0;


out_free:
	for (i = 0; i < 8; i++)
		if (mp->rxq_mask & (1 << i))
			rxq_deinit(mp->rxq + i);
out:
	free_irq(dev->irq, dev);

	return err;
}

static void port_reset(struct mv643xx_eth_private *mp)
{
	unsigned int data;
	int i;

	for (i = 0; i < 8; i++) {
		if (mp->rxq_mask & (1 << i))
			rxq_disable(mp->rxq + i);
		if (mp->txq_mask & (1 << i))
			txq_disable(mp->txq + i);
	}

	while (1) {
		u32 ps = rdl(mp, PORT_STATUS(mp->port_num));

		if ((ps & (TX_IN_PROGRESS | TX_FIFO_EMPTY)) == TX_FIFO_EMPTY)
			break;
		udelay(10);
	}

	/* Reset the Enable bit in the Configuration Register */
	data = rdl(mp, PORT_SERIAL_CONTROL(mp->port_num));
	data &= ~(SERIAL_PORT_ENABLE		|
		  DO_NOT_FORCE_LINK_FAIL	|
		  FORCE_LINK_PASS);
	wrl(mp, PORT_SERIAL_CONTROL(mp->port_num), data);
}

static int mv643xx_eth_stop(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int i;

	wrl(mp, INT_MASK(mp->port_num), 0x00000000);
	rdl(mp, INT_MASK(mp->port_num));

#ifdef MV643XX_ETH_NAPI
	napi_disable(&mp->napi);
#endif
	netif_carrier_off(dev);
	netif_stop_queue(dev);

	free_irq(dev->irq, dev);

	port_reset(mp);
	mib_counters_update(mp);

	for (i = 0; i < 8; i++) {
		if (mp->rxq_mask & (1 << i))
			rxq_deinit(mp->rxq + i);
		if (mp->txq_mask & (1 << i))
			txq_deinit(mp->txq + i);
	}

	return 0;
}

static int mv643xx_eth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	if (mp->phy_addr != -1)
		return generic_mii_ioctl(&mp->mii, if_mii(ifr), cmd, NULL);

	return -EOPNOTSUPP;
}

static int mv643xx_eth_change_mtu(struct net_device *dev, int new_mtu)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	if (new_mtu < 64 || new_mtu > 9500)
		return -EINVAL;

	dev->mtu = new_mtu;
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
		dev_printk(KERN_ERR, &dev->dev,
			   "fatal error on re-opening device after "
			   "MTU change\n");
	}

	return 0;
}

static void tx_timeout_task(struct work_struct *ugly)
{
	struct mv643xx_eth_private *mp;

	mp = container_of(ugly, struct mv643xx_eth_private, tx_timeout_task);
	if (netif_running(mp->dev)) {
		netif_stop_queue(mp->dev);

		port_reset(mp);
		port_start(mp);

		__txq_maybe_wake(mp->txq + mp->txq_primary);
	}
}

static void mv643xx_eth_tx_timeout(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	dev_printk(KERN_INFO, &dev->dev, "tx timeout\n");

	schedule_work(&mp->tx_timeout_task);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void mv643xx_eth_netpoll(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	wrl(mp, INT_MASK(mp->port_num), 0x00000000);
	rdl(mp, INT_MASK(mp->port_num));

	mv643xx_eth_irq(dev->irq, dev);

	wrl(mp, INT_MASK(mp->port_num), INT_TX_END | INT_RX | INT_EXT);
}
#endif

static int mv643xx_eth_mdio_read(struct net_device *dev, int addr, int reg)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int val;

	smi_reg_read(mp, addr, reg, &val);

	return val;
}

static void mv643xx_eth_mdio_write(struct net_device *dev, int addr, int reg, int val)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	smi_reg_write(mp, addr, reg, val);
}


/* platform glue ************************************************************/
static void
mv643xx_eth_conf_mbus_windows(struct mv643xx_eth_shared_private *msp,
			      struct mbus_dram_target_info *dram)
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
		struct mbus_dram_window *cs = dram->cs + i;

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
	writel(0x02000000, msp->base + SDMA_CONFIG(0));
	if (readl(msp->base + SDMA_CONFIG(0)) & 0x02000000)
		msp->extended_rx_coal_limit = 1;
	else
		msp->extended_rx_coal_limit = 0;

	/*
	 * Check whether the TX rate control registers are in the
	 * old or the new place.
	 */
	writel(1, msp->base + TX_BW_MTU_MOVED(0));
	if (readl(msp->base + TX_BW_MTU_MOVED(0)) & 1)
		msp->tx_bw_control_moved = 1;
	else
		msp->tx_bw_control_moved = 0;
}

static int mv643xx_eth_shared_probe(struct platform_device *pdev)
{
	static int mv643xx_eth_version_printed = 0;
	struct mv643xx_eth_shared_platform_data *pd = pdev->dev.platform_data;
	struct mv643xx_eth_shared_private *msp;
	struct resource *res;
	int ret;

	if (!mv643xx_eth_version_printed++)
		printk(KERN_NOTICE "MV-643xx 10/100/1000 ethernet "
			"driver version %s\n", mv643xx_eth_driver_version);

	ret = -EINVAL;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		goto out;

	ret = -ENOMEM;
	msp = kmalloc(sizeof(*msp), GFP_KERNEL);
	if (msp == NULL)
		goto out;
	memset(msp, 0, sizeof(*msp));

	msp->base = ioremap(res->start, res->end - res->start + 1);
	if (msp->base == NULL)
		goto out_free;

	spin_lock_init(&msp->phy_lock);

	/*
	 * (Re-)program MBUS remapping windows if we are asked to.
	 */
	if (pd != NULL && pd->dram != NULL)
		mv643xx_eth_conf_mbus_windows(msp, pd->dram);

	/*
	 * Detect hardware parameters.
	 */
	msp->t_clk = (pd != NULL && pd->t_clk != 0) ? pd->t_clk : 133000000;
	infer_hw_params(msp);

	platform_set_drvdata(pdev, msp);

	return 0;

out_free:
	kfree(msp);
out:
	return ret;
}

static int mv643xx_eth_shared_remove(struct platform_device *pdev)
{
	struct mv643xx_eth_shared_private *msp = platform_get_drvdata(pdev);

	iounmap(msp->base);
	kfree(msp);

	return 0;
}

static struct platform_driver mv643xx_eth_shared_driver = {
	.probe		= mv643xx_eth_shared_probe,
	.remove		= mv643xx_eth_shared_remove,
	.driver = {
		.name	= MV643XX_ETH_SHARED_NAME,
		.owner	= THIS_MODULE,
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
		memcpy(dev->dev_addr, pd->mac_addr, 6);
	else
		uc_addr_get(mp, dev->dev_addr);

	if (pd->phy_addr == -1) {
		mp->shared_smi = NULL;
		mp->phy_addr = -1;
	} else {
		mp->shared_smi = mp->shared;
		if (pd->shared_smi != NULL)
			mp->shared_smi = platform_get_drvdata(pd->shared_smi);

		if (pd->force_phy_addr || pd->phy_addr) {
			mp->phy_addr = pd->phy_addr & 0x3f;
			phy_addr_set(mp, mp->phy_addr);
		} else {
			mp->phy_addr = phy_addr_get(mp);
		}
	}

	mp->default_rx_ring_size = DEFAULT_RX_QUEUE_SIZE;
	if (pd->rx_queue_size)
		mp->default_rx_ring_size = pd->rx_queue_size;
	mp->rx_desc_sram_addr = pd->rx_sram_addr;
	mp->rx_desc_sram_size = pd->rx_sram_size;

	if (pd->rx_queue_mask)
		mp->rxq_mask = pd->rx_queue_mask;
	else
		mp->rxq_mask = 0x01;
	mp->rxq_primary = fls(mp->rxq_mask) - 1;

	mp->default_tx_ring_size = DEFAULT_TX_QUEUE_SIZE;
	if (pd->tx_queue_size)
		mp->default_tx_ring_size = pd->tx_queue_size;
	mp->tx_desc_sram_addr = pd->tx_sram_addr;
	mp->tx_desc_sram_size = pd->tx_sram_size;

	if (pd->tx_queue_mask)
		mp->txq_mask = pd->tx_queue_mask;
	else
		mp->txq_mask = 0x01;
	mp->txq_primary = fls(mp->txq_mask) - 1;
}

static int phy_detect(struct mv643xx_eth_private *mp)
{
	unsigned int data;
	unsigned int data2;

	smi_reg_read(mp, mp->phy_addr, MII_BMCR, &data);
	smi_reg_write(mp, mp->phy_addr, MII_BMCR, data ^ BMCR_ANENABLE);

	smi_reg_read(mp, mp->phy_addr, MII_BMCR, &data2);
	if (((data ^ data2) & BMCR_ANENABLE) == 0)
		return -ENODEV;

	smi_reg_write(mp, mp->phy_addr, MII_BMCR, data);

	return 0;
}

static int phy_init(struct mv643xx_eth_private *mp,
		    struct mv643xx_eth_platform_data *pd)
{
	struct ethtool_cmd cmd;
	int err;

	err = phy_detect(mp);
	if (err) {
		dev_printk(KERN_INFO, &mp->dev->dev,
			   "no PHY detected at addr %d\n", mp->phy_addr);
		return err;
	}
	phy_reset(mp);

	mp->mii.phy_id = mp->phy_addr;
	mp->mii.phy_id_mask = 0x3f;
	mp->mii.reg_num_mask = 0x1f;
	mp->mii.dev = mp->dev;
	mp->mii.mdio_read = mv643xx_eth_mdio_read;
	mp->mii.mdio_write = mv643xx_eth_mdio_write;

	mp->mii.supports_gmii = mii_check_gmii_support(&mp->mii);

	memset(&cmd, 0, sizeof(cmd));

	cmd.port = PORT_MII;
	cmd.transceiver = XCVR_INTERNAL;
	cmd.phy_address = mp->phy_addr;
	if (pd->speed == 0) {
		cmd.autoneg = AUTONEG_ENABLE;
		cmd.speed = SPEED_100;
		cmd.advertising = ADVERTISED_10baseT_Half  |
				  ADVERTISED_10baseT_Full  |
				  ADVERTISED_100baseT_Half |
				  ADVERTISED_100baseT_Full;
		if (mp->mii.supports_gmii)
			cmd.advertising |= ADVERTISED_1000baseT_Full;
	} else {
		cmd.autoneg = AUTONEG_DISABLE;
		cmd.speed = pd->speed;
		cmd.duplex = pd->duplex;
	}

	mv643xx_eth_set_settings(mp->dev, &cmd);

	return 0;
}

static void init_pscr(struct mv643xx_eth_private *mp, int speed, int duplex)
{
	u32 pscr;

	pscr = rdl(mp, PORT_SERIAL_CONTROL(mp->port_num));
	if (pscr & SERIAL_PORT_ENABLE) {
		pscr &= ~SERIAL_PORT_ENABLE;
		wrl(mp, PORT_SERIAL_CONTROL(mp->port_num), pscr);
	}

	pscr = MAX_RX_PACKET_9700BYTE | SERIAL_PORT_CONTROL_RESERVED;
	if (mp->phy_addr == -1) {
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

	wrl(mp, PORT_SERIAL_CONTROL(mp->port_num), pscr);
}

static int mv643xx_eth_probe(struct platform_device *pdev)
{
	struct mv643xx_eth_platform_data *pd;
	struct mv643xx_eth_private *mp;
	struct net_device *dev;
	struct resource *res;
	DECLARE_MAC_BUF(mac);
	int err;

	pd = pdev->dev.platform_data;
	if (pd == NULL) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "no mv643xx_eth_platform_data\n");
		return -ENODEV;
	}

	if (pd->shared == NULL) {
		dev_printk(KERN_ERR, &pdev->dev,
			   "no mv643xx_eth_platform_data->shared\n");
		return -ENODEV;
	}

	dev = alloc_etherdev(sizeof(struct mv643xx_eth_private));
	if (!dev)
		return -ENOMEM;

	mp = netdev_priv(dev);
	platform_set_drvdata(pdev, mp);

	mp->shared = platform_get_drvdata(pd->shared);
	mp->port_num = pd->port_number;

	mp->dev = dev;
#ifdef MV643XX_ETH_NAPI
	netif_napi_add(dev, &mp->napi, mv643xx_eth_poll, 64);
#endif

	set_params(mp, pd);

	spin_lock_init(&mp->lock);

	mib_counters_clear(mp);
	INIT_WORK(&mp->tx_timeout_task, tx_timeout_task);

	if (mp->phy_addr != -1) {
		err = phy_init(mp, pd);
		if (err)
			goto out;

		SET_ETHTOOL_OPS(dev, &mv643xx_eth_ethtool_ops);
	} else {
		SET_ETHTOOL_OPS(dev, &mv643xx_eth_ethtool_ops_phyless);
	}
	init_pscr(mp, pd->speed, pd->duplex);


	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	BUG_ON(!res);
	dev->irq = res->start;

	dev->hard_start_xmit = mv643xx_eth_xmit;
	dev->open = mv643xx_eth_open;
	dev->stop = mv643xx_eth_stop;
	dev->set_multicast_list = mv643xx_eth_set_rx_mode;
	dev->set_mac_address = mv643xx_eth_set_mac_address;
	dev->do_ioctl = mv643xx_eth_ioctl;
	dev->change_mtu = mv643xx_eth_change_mtu;
	dev->tx_timeout = mv643xx_eth_tx_timeout;
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = mv643xx_eth_netpoll;
#endif
	dev->watchdog_timeo = 2 * HZ;
	dev->base_addr = 0;

#ifdef MV643XX_ETH_CHECKSUM_OFFLOAD_TX
	/*
	 * Zero copy can only work if we use Discovery II memory. Else, we will
	 * have to map the buffers to ISA memory which is only 16 MB
	 */
	dev->features = NETIF_F_SG | NETIF_F_IP_CSUM;
	dev->vlan_features = NETIF_F_SG | NETIF_F_IP_CSUM;
#endif

	SET_NETDEV_DEV(dev, &pdev->dev);

	if (mp->shared->win_protect)
		wrl(mp, WINDOW_PROTECT(mp->port_num), mp->shared->win_protect);

	err = register_netdev(dev);
	if (err)
		goto out;

	dev_printk(KERN_NOTICE, &dev->dev, "port %d with MAC address %s\n",
		   mp->port_num, print_mac(mac, dev->dev_addr));

	if (dev->features & NETIF_F_SG)
		dev_printk(KERN_NOTICE, &dev->dev, "scatter/gather enabled\n");

	if (dev->features & NETIF_F_IP_CSUM)
		dev_printk(KERN_NOTICE, &dev->dev, "tx checksum offload\n");

#ifdef MV643XX_ETH_NAPI
	dev_printk(KERN_NOTICE, &dev->dev, "napi enabled\n");
#endif

	if (mp->tx_desc_sram_size > 0)
		dev_printk(KERN_NOTICE, &dev->dev, "configured with sram\n");

	return 0;

out:
	free_netdev(dev);

	return err;
}

static int mv643xx_eth_remove(struct platform_device *pdev)
{
	struct mv643xx_eth_private *mp = platform_get_drvdata(pdev);

	unregister_netdev(mp->dev);
	flush_scheduled_work();
	free_netdev(mp->dev);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static void mv643xx_eth_shutdown(struct platform_device *pdev)
{
	struct mv643xx_eth_private *mp = platform_get_drvdata(pdev);

	/* Mask all interrupts on ethernet port */
	wrl(mp, INT_MASK(mp->port_num), 0);
	rdl(mp, INT_MASK(mp->port_num));

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
