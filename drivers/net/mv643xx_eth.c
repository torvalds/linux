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
static char mv643xx_eth_driver_version[] = "1.0";

#define MV643XX_ETH_CHECKSUM_OFFLOAD_TX
#define MV643XX_ETH_NAPI
#define MV643XX_ETH_TX_FAST_REFILL
#undef	MV643XX_ETH_COAL

#define MV643XX_ETH_TX_COAL 100
#ifdef MV643XX_ETH_COAL
#define MV643XX_ETH_RX_COAL 100
#endif

#ifdef MV643XX_ETH_CHECKSUM_OFFLOAD_TX
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
#define TXQ_COMMAND(p)			(0x0448 + ((p) << 10))
#define TX_BW_MTU(p)			(0x0458 + ((p) << 10))
#define INT_CAUSE(p)			(0x0460 + ((p) << 10))
#define  INT_RX				0x00000804
#define  INT_EXT			0x00000002
#define INT_CAUSE_EXT(p)		(0x0464 + ((p) << 10))
#define  INT_EXT_LINK			0x00100000
#define  INT_EXT_PHY			0x00010000
#define  INT_EXT_TX_ERROR_0		0x00000100
#define  INT_EXT_TX_0			0x00000001
#define  INT_EXT_TX			0x00000101
#define INT_MASK(p)			(0x0468 + ((p) << 10))
#define INT_MASK_EXT(p)			(0x046c + ((p) << 10))
#define TX_FIFO_URGENT_THRESHOLD(p)	(0x0474 + ((p) << 10))
#define RXQ_CURRENT_DESC_PTR(p)		(0x060c + ((p) << 10))
#define RXQ_COMMAND(p)			(0x0680 + ((p) << 10))
#define TXQ_CURRENT_DESC_PTR(p)		(0x06c0 + ((p) << 10))
#define MIB_COUNTERS(p)			(0x1000 + ((p) << 7))
#define SPECIAL_MCAST_TABLE(p)		(0x1400 + ((p) << 10))
#define OTHER_MCAST_TABLE(p)		(0x1500 + ((p) << 10))
#define UNICAST_TABLE(p)		(0x1600 + ((p) << 10))


/*
 * SDMA configuration register.
 */
#define RX_BURST_SIZE_4_64BIT		(2 << 1)
#define BLM_RX_NO_SWAP			(1 << 4)
#define BLM_TX_NO_SWAP			(1 << 5)
#define TX_BURST_SIZE_4_64BIT		(2 << 22)

#if defined(__BIG_ENDIAN)
#define PORT_SDMA_CONFIG_DEFAULT_VALUE		\
		RX_BURST_SIZE_4_64BIT	|	\
		TX_BURST_SIZE_4_64BIT
#elif defined(__LITTLE_ENDIAN)
#define PORT_SDMA_CONFIG_DEFAULT_VALUE		\
		RX_BURST_SIZE_4_64BIT	|	\
		BLM_RX_NO_SWAP		|	\
		BLM_TX_NO_SWAP		|	\
		TX_BURST_SIZE_4_64BIT
#else
#error One of __BIG_ENDIAN or __LITTLE_ENDIAN must be defined
#endif


/*
 * Port serial control register.
 */
#define SET_MII_SPEED_TO_100			(1 << 24)
#define SET_GMII_SPEED_TO_1000			(1 << 23)
#define SET_FULL_DUPLEX_MODE			(1 << 21)
#define MAX_RX_PACKET_1522BYTE			(1 << 17)
#define MAX_RX_PACKET_9700BYTE			(5 << 17)
#define MAX_RX_PACKET_MASK			(7 << 17)
#define DISABLE_AUTO_NEG_SPEED_GMII		(1 << 13)
#define DO_NOT_FORCE_LINK_FAIL			(1 << 10)
#define SERIAL_PORT_CONTROL_RESERVED		(1 << 9)
#define DISABLE_AUTO_NEG_FOR_FLOW_CTRL		(1 << 3)
#define DISABLE_AUTO_NEG_FOR_DUPLEX		(1 << 2)
#define FORCE_LINK_PASS				(1 << 1)
#define SERIAL_PORT_ENABLE			(1 << 0)

#define DEFAULT_RX_QUEUE_SIZE		400
#define DEFAULT_TX_QUEUE_SIZE		800

/* SMI reg */
#define SMI_BUSY		0x10000000	/* 0 - Write, 1 - Read	*/
#define SMI_READ_VALID		0x08000000	/* 0 - Write, 1 - Read	*/
#define SMI_OPCODE_WRITE	0		/* Completion of Read	*/
#define SMI_OPCODE_READ		0x04000000	/* Operation is in progress */

/* typedefs */

typedef enum _func_ret_status {
	ETH_OK,			/* Returned as expected.		*/
	ETH_ERROR,		/* Fundamental error.			*/
	ETH_RETRY,		/* Could not process request. Try later.*/
	ETH_END_OF_JOB,		/* Ring has nothing to process.		*/
	ETH_QUEUE_FULL,		/* Ring resource error.			*/
	ETH_QUEUE_LAST_RESOURCE	/* Ring resources about to exhaust.	*/
} FUNC_RET_STATUS;

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

#define TX_IHL_SHIFT			11


/* Unified struct for Rx and Tx operations. The user is not required to	*/
/* be familier with neither Tx nor Rx descriptors.			*/
struct pkt_info {
	unsigned short byte_cnt;	/* Descriptor buffer byte count	*/
	unsigned short l4i_chk;		/* Tx CPU provided TCP Checksum	*/
	unsigned int cmd_sts;		/* Descriptor command status	*/
	dma_addr_t buf_ptr;		/* Descriptor buffer pointer	*/
	struct sk_buff *return_info;	/* User resource return information */
};


/* global *******************************************************************/
struct mv643xx_eth_shared_private {
	void __iomem *base;

	/* used to protect SMI_REG, which is shared across ports */
	spinlock_t phy_lock;

	u32 win_protect;

	unsigned int t_clk;
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

struct mv643xx_eth_private {
	struct mv643xx_eth_shared_private *shared;
	int port_num;			/* User Ethernet port number	*/

	struct mv643xx_eth_shared_private *shared_smi;

	u32 rx_sram_addr;		/* Base address of rx sram area */
	u32 rx_sram_size;		/* Size of rx sram area		*/
	u32 tx_sram_addr;		/* Base address of tx sram area */
	u32 tx_sram_size;		/* Size of tx sram area		*/

	/* Tx/Rx rings managment indexes fields. For driver use */

	/* Next available and first returning Rx resource */
	int rx_curr_desc, rx_used_desc;

	/* Next available and first returning Tx resource */
	int tx_curr_desc, tx_used_desc;

#ifdef MV643XX_ETH_TX_FAST_REFILL
	u32 tx_clean_threshold;
#endif

	struct rx_desc *rx_desc_area;
	dma_addr_t rx_desc_dma;
	int rx_desc_area_size;
	struct sk_buff **rx_skb;

	struct tx_desc *tx_desc_area;
	dma_addr_t tx_desc_dma;
	int tx_desc_area_size;
	struct sk_buff **tx_skb;

	struct work_struct tx_timeout_task;

	struct net_device *dev;
	struct napi_struct napi;
	struct net_device_stats stats;
	struct mib_counters mib_counters;
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
static void mv643xx_eth_port_enable_rx(struct mv643xx_eth_private *mp,
					unsigned int queues)
{
	wrl(mp, RXQ_COMMAND(mp->port_num), queues);
}

static unsigned int mv643xx_eth_port_disable_rx(struct mv643xx_eth_private *mp)
{
	unsigned int port_num = mp->port_num;
	u32 queues;

	/* Stop Rx port activity. Check port Rx activity. */
	queues = rdl(mp, RXQ_COMMAND(port_num)) & 0xFF;
	if (queues) {
		/* Issue stop command for active queues only */
		wrl(mp, RXQ_COMMAND(port_num), (queues << 8));

		/* Wait for all Rx activity to terminate. */
		/* Check port cause register that all Rx queues are stopped */
		while (rdl(mp, RXQ_COMMAND(port_num)) & 0xFF)
			udelay(10);
	}

	return queues;
}

static void mv643xx_eth_port_enable_tx(struct mv643xx_eth_private *mp,
					unsigned int queues)
{
	wrl(mp, TXQ_COMMAND(mp->port_num), queues);
}

static unsigned int mv643xx_eth_port_disable_tx(struct mv643xx_eth_private *mp)
{
	unsigned int port_num = mp->port_num;
	u32 queues;

	/* Stop Tx port activity. Check port Tx activity. */
	queues = rdl(mp, TXQ_COMMAND(port_num)) & 0xFF;
	if (queues) {
		/* Issue stop command for active queues only */
		wrl(mp, TXQ_COMMAND(port_num), (queues << 8));

		/* Wait for all Tx activity to terminate. */
		/* Check port cause register that all Tx queues are stopped */
		while (rdl(mp, TXQ_COMMAND(port_num)) & 0xFF)
			udelay(10);

		/* Wait for Tx FIFO to empty */
		while (rdl(mp, PORT_STATUS(port_num)) & TX_FIFO_EMPTY)
			udelay(10);
	}

	return queues;
}


/* rx ***********************************************************************/
static void mv643xx_eth_free_completed_tx_descs(struct net_device *dev);

static void mv643xx_eth_rx_refill_descs(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&mp->lock, flags);

	while (mp->rx_desc_count < mp->rx_ring_size) {
		struct sk_buff *skb;
		int unaligned;
		int rx;

		skb = dev_alloc_skb(ETH_RX_SKB_SIZE + dma_get_cache_alignment());
		if (skb == NULL)
			break;

		unaligned = (u32)skb->data & (dma_get_cache_alignment() - 1);
		if (unaligned)
			skb_reserve(skb, dma_get_cache_alignment() - unaligned);

		mp->rx_desc_count++;
		rx = mp->rx_used_desc;
		mp->rx_used_desc = (rx + 1) % mp->rx_ring_size;

		mp->rx_desc_area[rx].buf_ptr = dma_map_single(NULL,
							skb->data,
							ETH_RX_SKB_SIZE,
							DMA_FROM_DEVICE);
		mp->rx_desc_area[rx].buf_size = ETH_RX_SKB_SIZE;
		mp->rx_skb[rx] = skb;
		wmb();
		mp->rx_desc_area[rx].cmd_sts = BUFFER_OWNED_BY_DMA |
						RX_ENABLE_INTERRUPT;
		wmb();

		skb_reserve(skb, ETH_HW_IP_ALIGN);
	}

	if (mp->rx_desc_count == 0) {
		mp->timeout.expires = jiffies + (HZ / 10);
		add_timer(&mp->timeout);
	}

	spin_unlock_irqrestore(&mp->lock, flags);
}

static inline void mv643xx_eth_rx_refill_descs_timer_wrapper(unsigned long data)
{
	mv643xx_eth_rx_refill_descs((struct net_device *)data);
}

static int mv643xx_eth_receive_queue(struct net_device *dev, int budget)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	unsigned int received_packets = 0;

	while (budget-- > 0) {
		struct sk_buff *skb;
		volatile struct rx_desc *rx_desc;
		unsigned int cmd_sts;
		unsigned long flags;

		spin_lock_irqsave(&mp->lock, flags);

		rx_desc = &mp->rx_desc_area[mp->rx_curr_desc];

		cmd_sts = rx_desc->cmd_sts;
		if (cmd_sts & BUFFER_OWNED_BY_DMA) {
			spin_unlock_irqrestore(&mp->lock, flags);
			break;
		}
		rmb();

		skb = mp->rx_skb[mp->rx_curr_desc];
		mp->rx_skb[mp->rx_curr_desc] = NULL;

		mp->rx_curr_desc = (mp->rx_curr_desc + 1) % mp->rx_ring_size;

		spin_unlock_irqrestore(&mp->lock, flags);

		dma_unmap_single(NULL, rx_desc->buf_ptr + ETH_HW_IP_ALIGN,
					ETH_RX_SKB_SIZE, DMA_FROM_DEVICE);
		mp->rx_desc_count--;
		received_packets++;

		/*
		 * Update statistics.
		 * Note byte count includes 4 byte CRC count
		 */
		stats->rx_packets++;
		stats->rx_bytes += rx_desc->byte_cnt - ETH_HW_IP_ALIGN;

		/*
		 * In case received a packet without first / last bits on OR
		 * the error summary bit is on, the packets needs to be dropeed.
		 */
		if (((cmd_sts & (RX_FIRST_DESC | RX_LAST_DESC)) !=
					(RX_FIRST_DESC | RX_LAST_DESC))
				|| (cmd_sts & ERROR_SUMMARY)) {
			stats->rx_dropped++;
			if ((cmd_sts & (RX_FIRST_DESC | RX_LAST_DESC)) !=
				(RX_FIRST_DESC | RX_LAST_DESC)) {
				if (net_ratelimit())
					printk(KERN_ERR
						"%s: Received packet spread "
						"on multiple descriptors\n",
						dev->name);
			}
			if (cmd_sts & ERROR_SUMMARY)
				stats->rx_errors++;

			dev_kfree_skb_irq(skb);
		} else {
			/*
			 * The -4 is for the CRC in the trailer of the
			 * received packet
			 */
			skb_put(skb, rx_desc->byte_cnt - ETH_HW_IP_ALIGN - 4);

			if (cmd_sts & LAYER_4_CHECKSUM_OK) {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
				skb->csum = htons(
					(cmd_sts & 0x0007fff8) >> 3);
			}
			skb->protocol = eth_type_trans(skb, dev);
#ifdef MV643XX_ETH_NAPI
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

#ifdef MV643XX_ETH_NAPI
static int mv643xx_eth_poll(struct napi_struct *napi, int budget)
{
	struct mv643xx_eth_private *mp = container_of(napi, struct mv643xx_eth_private, napi);
	struct net_device *dev = mp->dev;
	unsigned int port_num = mp->port_num;
	int work_done;

#ifdef MV643XX_ETH_TX_FAST_REFILL
	if (++mp->tx_clean_threshold > 5) {
		mv643xx_eth_free_completed_tx_descs(dev);
		mp->tx_clean_threshold = 0;
	}
#endif

	work_done = 0;
	if ((rdl(mp, RXQ_CURRENT_DESC_PTR(port_num)))
	    != (u32) mp->rx_used_desc)
		work_done = mv643xx_eth_receive_queue(dev, budget);

	if (work_done < budget) {
		netif_rx_complete(dev, napi);
		wrl(mp, INT_CAUSE(port_num), 0);
		wrl(mp, INT_CAUSE_EXT(port_num), 0);
		wrl(mp, INT_MASK(port_num), INT_RX | INT_EXT);
	}

	return work_done;
}
#endif


/* tx ***********************************************************************/
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

static int alloc_tx_desc_index(struct mv643xx_eth_private *mp)
{
	int tx_desc_curr;

	BUG_ON(mp->tx_desc_count >= mp->tx_ring_size);

	tx_desc_curr = mp->tx_curr_desc;
	mp->tx_curr_desc = (tx_desc_curr + 1) % mp->tx_ring_size;

	BUG_ON(mp->tx_curr_desc == mp->tx_used_desc);

	return tx_desc_curr;
}

static void tx_fill_frag_descs(struct mv643xx_eth_private *mp,
				   struct sk_buff *skb)
{
	int frag;
	int tx_index;
	struct tx_desc *desc;

	for (frag = 0; frag < skb_shinfo(skb)->nr_frags; frag++) {
		skb_frag_t *this_frag = &skb_shinfo(skb)->frags[frag];

		tx_index = alloc_tx_desc_index(mp);
		desc = &mp->tx_desc_area[tx_index];

		desc->cmd_sts = BUFFER_OWNED_BY_DMA;
		/* Last Frag enables interrupt and frees the skb */
		if (frag == (skb_shinfo(skb)->nr_frags - 1)) {
			desc->cmd_sts |= ZERO_PADDING |
					 TX_LAST_DESC |
					 TX_ENABLE_INTERRUPT;
			mp->tx_skb[tx_index] = skb;
		} else
			mp->tx_skb[tx_index] = NULL;

		desc = &mp->tx_desc_area[tx_index];
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

static void tx_submit_descs_for_skb(struct mv643xx_eth_private *mp,
					struct sk_buff *skb)
{
	int tx_index;
	struct tx_desc *desc;
	u32 cmd_sts;
	int length;
	int nr_frags = skb_shinfo(skb)->nr_frags;

	cmd_sts = TX_FIRST_DESC | GEN_CRC | BUFFER_OWNED_BY_DMA;

	tx_index = alloc_tx_desc_index(mp);
	desc = &mp->tx_desc_area[tx_index];

	if (nr_frags) {
		tx_fill_frag_descs(mp, skb);

		length = skb_headlen(skb);
		mp->tx_skb[tx_index] = NULL;
	} else {
		cmd_sts |= ZERO_PADDING | TX_LAST_DESC | TX_ENABLE_INTERRUPT;
		length = skb->len;
		mp->tx_skb[tx_index] = skb;
	}

	desc->byte_cnt = length;
	desc->buf_ptr = dma_map_single(NULL, skb->data, length, DMA_TO_DEVICE);

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		BUG_ON(skb->protocol != htons(ETH_P_IP));

		cmd_sts |= GEN_TCP_UDP_CHECKSUM |
			   GEN_IP_V4_CHECKSUM   |
			   ip_hdr(skb)->ihl << TX_IHL_SHIFT;

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

	/* ensure all descriptors are written before poking hardware */
	wmb();
	mv643xx_eth_port_enable_tx(mp, 1);

	mp->tx_desc_count += nr_frags + 1;
}

static int mv643xx_eth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	unsigned long flags;

	BUG_ON(netif_queue_stopped(dev));

	if (has_tiny_unaligned_frags(skb) && __skb_linearize(skb)) {
		stats->tx_dropped++;
		printk(KERN_DEBUG "%s: failed to linearize tiny "
				"unaligned fragment\n", dev->name);
		return NETDEV_TX_BUSY;
	}

	spin_lock_irqsave(&mp->lock, flags);

	if (mp->tx_ring_size - mp->tx_desc_count < MAX_DESCS_PER_SKB) {
		printk(KERN_ERR "%s: transmit with queue full\n", dev->name);
		netif_stop_queue(dev);
		spin_unlock_irqrestore(&mp->lock, flags);
		return NETDEV_TX_BUSY;
	}

	tx_submit_descs_for_skb(mp, skb);
	stats->tx_bytes += skb->len;
	stats->tx_packets++;
	dev->trans_start = jiffies;

	if (mp->tx_ring_size - mp->tx_desc_count < MAX_DESCS_PER_SKB)
		netif_stop_queue(dev);

	spin_unlock_irqrestore(&mp->lock, flags);

	return NETDEV_TX_OK;
}


/* mii management interface *************************************************/
static int phy_addr_get(struct mv643xx_eth_private *mp);

static void read_smi_reg(struct mv643xx_eth_private *mp,
				unsigned int phy_reg, unsigned int *value)
{
	void __iomem *smi_reg = mp->shared_smi->base + SMI_REG;
	int phy_addr = phy_addr_get(mp);
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

	writel((phy_addr << 16) | (phy_reg << 21) | SMI_OPCODE_READ, smi_reg);

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

static void write_smi_reg(struct mv643xx_eth_private *mp,
				   unsigned int phy_reg, unsigned int value)
{
	void __iomem *smi_reg = mp->shared_smi->base + SMI_REG;
	int phy_addr = phy_addr_get(mp);
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

	writel((phy_addr << 16) | (phy_reg << 21) |
		SMI_OPCODE_WRITE | (value & 0xffff), smi_reg);
out:
	spin_unlock_irqrestore(&mp->shared_smi->phy_lock, flags);
}


/* mib counters *************************************************************/
static void clear_mib_counters(struct mv643xx_eth_private *mp)
{
	unsigned int port_num = mp->port_num;
	int i;

	/* Perform dummy reads from MIB counters */
	for (i = 0; i < 0x80; i += 4)
		rdl(mp, MIB_COUNTERS(port_num) + i);
}

static inline u32 read_mib(struct mv643xx_eth_private *mp, int offset)
{
	return rdl(mp, MIB_COUNTERS(mp->port_num) + offset);
}

static void update_mib_counters(struct mv643xx_eth_private *mp)
{
	struct mib_counters *p = &mp->mib_counters;

	p->good_octets_received += read_mib(mp, 0x00);
	p->good_octets_received += (u64)read_mib(mp, 0x04) << 32;
	p->bad_octets_received += read_mib(mp, 0x08);
	p->internal_mac_transmit_err += read_mib(mp, 0x0c);
	p->good_frames_received += read_mib(mp, 0x10);
	p->bad_frames_received += read_mib(mp, 0x14);
	p->broadcast_frames_received += read_mib(mp, 0x18);
	p->multicast_frames_received += read_mib(mp, 0x1c);
	p->frames_64_octets += read_mib(mp, 0x20);
	p->frames_65_to_127_octets += read_mib(mp, 0x24);
	p->frames_128_to_255_octets += read_mib(mp, 0x28);
	p->frames_256_to_511_octets += read_mib(mp, 0x2c);
	p->frames_512_to_1023_octets += read_mib(mp, 0x30);
	p->frames_1024_to_max_octets += read_mib(mp, 0x34);
	p->good_octets_sent += read_mib(mp, 0x38);
	p->good_octets_sent += (u64)read_mib(mp, 0x3c) << 32;
	p->good_frames_sent += read_mib(mp, 0x40);
	p->excessive_collision += read_mib(mp, 0x44);
	p->multicast_frames_sent += read_mib(mp, 0x48);
	p->broadcast_frames_sent += read_mib(mp, 0x4c);
	p->unrec_mac_control_received += read_mib(mp, 0x50);
	p->fc_sent += read_mib(mp, 0x54);
	p->good_fc_received += read_mib(mp, 0x58);
	p->bad_fc_received += read_mib(mp, 0x5c);
	p->undersize_received += read_mib(mp, 0x60);
	p->fragments_received += read_mib(mp, 0x64);
	p->oversize_received += read_mib(mp, 0x68);
	p->jabber_received += read_mib(mp, 0x6c);
	p->mac_receive_error += read_mib(mp, 0x70);
	p->bad_crc_event += read_mib(mp, 0x74);
	p->collision += read_mib(mp, 0x78);
	p->late_collision += read_mib(mp, 0x7c);
}


/* ethtool ******************************************************************/
struct mv643xx_eth_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
};

#define MV643XX_ETH_STAT(m) FIELD_SIZEOF(struct mv643xx_eth_private, m), \
					offsetof(struct mv643xx_eth_private, m)

static const struct mv643xx_eth_stats mv643xx_eth_gstrings_stats[] = {
	{ "rx_packets", MV643XX_ETH_STAT(stats.rx_packets) },
	{ "tx_packets", MV643XX_ETH_STAT(stats.tx_packets) },
	{ "rx_bytes", MV643XX_ETH_STAT(stats.rx_bytes) },
	{ "tx_bytes", MV643XX_ETH_STAT(stats.tx_bytes) },
	{ "rx_errors", MV643XX_ETH_STAT(stats.rx_errors) },
	{ "tx_errors", MV643XX_ETH_STAT(stats.tx_errors) },
	{ "rx_dropped", MV643XX_ETH_STAT(stats.rx_dropped) },
	{ "tx_dropped", MV643XX_ETH_STAT(stats.tx_dropped) },
	{ "good_octets_received", MV643XX_ETH_STAT(mib_counters.good_octets_received) },
	{ "bad_octets_received", MV643XX_ETH_STAT(mib_counters.bad_octets_received) },
	{ "internal_mac_transmit_err", MV643XX_ETH_STAT(mib_counters.internal_mac_transmit_err) },
	{ "good_frames_received", MV643XX_ETH_STAT(mib_counters.good_frames_received) },
	{ "bad_frames_received", MV643XX_ETH_STAT(mib_counters.bad_frames_received) },
	{ "broadcast_frames_received", MV643XX_ETH_STAT(mib_counters.broadcast_frames_received) },
	{ "multicast_frames_received", MV643XX_ETH_STAT(mib_counters.multicast_frames_received) },
	{ "frames_64_octets", MV643XX_ETH_STAT(mib_counters.frames_64_octets) },
	{ "frames_65_to_127_octets", MV643XX_ETH_STAT(mib_counters.frames_65_to_127_octets) },
	{ "frames_128_to_255_octets", MV643XX_ETH_STAT(mib_counters.frames_128_to_255_octets) },
	{ "frames_256_to_511_octets", MV643XX_ETH_STAT(mib_counters.frames_256_to_511_octets) },
	{ "frames_512_to_1023_octets", MV643XX_ETH_STAT(mib_counters.frames_512_to_1023_octets) },
	{ "frames_1024_to_max_octets", MV643XX_ETH_STAT(mib_counters.frames_1024_to_max_octets) },
	{ "good_octets_sent", MV643XX_ETH_STAT(mib_counters.good_octets_sent) },
	{ "good_frames_sent", MV643XX_ETH_STAT(mib_counters.good_frames_sent) },
	{ "excessive_collision", MV643XX_ETH_STAT(mib_counters.excessive_collision) },
	{ "multicast_frames_sent", MV643XX_ETH_STAT(mib_counters.multicast_frames_sent) },
	{ "broadcast_frames_sent", MV643XX_ETH_STAT(mib_counters.broadcast_frames_sent) },
	{ "unrec_mac_control_received", MV643XX_ETH_STAT(mib_counters.unrec_mac_control_received) },
	{ "fc_sent", MV643XX_ETH_STAT(mib_counters.fc_sent) },
	{ "good_fc_received", MV643XX_ETH_STAT(mib_counters.good_fc_received) },
	{ "bad_fc_received", MV643XX_ETH_STAT(mib_counters.bad_fc_received) },
	{ "undersize_received", MV643XX_ETH_STAT(mib_counters.undersize_received) },
	{ "fragments_received", MV643XX_ETH_STAT(mib_counters.fragments_received) },
	{ "oversize_received", MV643XX_ETH_STAT(mib_counters.oversize_received) },
	{ "jabber_received", MV643XX_ETH_STAT(mib_counters.jabber_received) },
	{ "mac_receive_error", MV643XX_ETH_STAT(mib_counters.mac_receive_error) },
	{ "bad_crc_event", MV643XX_ETH_STAT(mib_counters.bad_crc_event) },
	{ "collision", MV643XX_ETH_STAT(mib_counters.collision) },
	{ "late_collision", MV643XX_ETH_STAT(mib_counters.late_collision) },
};

#define MV643XX_ETH_STATS_LEN	ARRAY_SIZE(mv643xx_eth_gstrings_stats)

static int mv643xx_eth_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int err;

	spin_lock_irq(&mp->lock);
	err = mii_ethtool_gset(&mp->mii, cmd);
	spin_unlock_irq(&mp->lock);

	/* The PHY may support 1000baseT_Half, but the mv643xx does not */
	cmd->supported &= ~SUPPORTED_1000baseT_Half;
	cmd->advertising &= ~ADVERTISED_1000baseT_Half;

	return err;
}

static int mv643xx_eth_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int err;

	spin_lock_irq(&mp->lock);
	err = mii_ethtool_sset(&mp->mii, cmd);
	spin_unlock_irq(&mp->lock);

	return err;
}

static void mv643xx_eth_get_drvinfo(struct net_device *netdev,
				struct ethtool_drvinfo *drvinfo)
{
	strncpy(drvinfo->driver,  mv643xx_eth_driver_name, 32);
	strncpy(drvinfo->version, mv643xx_eth_driver_version, 32);
	strncpy(drvinfo->fw_version, "N/A", 32);
	strncpy(drvinfo->bus_info, "mv643xx", 32);
	drvinfo->n_stats = MV643XX_ETH_STATS_LEN;
}

static int mv643xx_eth_nway_restart(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	return mii_nway_restart(&mp->mii);
}

static u32 mv643xx_eth_get_link(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	return mii_link_ok(&mp->mii);
}

static void mv643xx_eth_get_strings(struct net_device *netdev, uint32_t stringset,
				uint8_t *data)
{
	int i;

	switch(stringset) {
	case ETH_SS_STATS:
		for (i=0; i < MV643XX_ETH_STATS_LEN; i++) {
			memcpy(data + i * ETH_GSTRING_LEN,
				mv643xx_eth_gstrings_stats[i].stat_string,
				ETH_GSTRING_LEN);
		}
		break;
	}
}

static void mv643xx_eth_get_ethtool_stats(struct net_device *netdev,
				struct ethtool_stats *stats, uint64_t *data)
{
	struct mv643xx_eth_private *mp = netdev->priv;
	int i;

	update_mib_counters(mp);

	for (i = 0; i < MV643XX_ETH_STATS_LEN; i++) {
		char *p = (char *)mp+mv643xx_eth_gstrings_stats[i].stat_offset;
		data[i] = (mv643xx_eth_gstrings_stats[i].sizeof_stat ==
			sizeof(uint64_t)) ? *(uint64_t *)p : *(uint32_t *)p;
	}
}

static int mv643xx_eth_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return MV643XX_ETH_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct ethtool_ops mv643xx_eth_ethtool_ops = {
	.get_settings           = mv643xx_eth_get_settings,
	.set_settings           = mv643xx_eth_set_settings,
	.get_drvinfo            = mv643xx_eth_get_drvinfo,
	.get_link               = mv643xx_eth_get_link,
	.set_sg			= ethtool_op_set_sg,
	.get_sset_count		= mv643xx_eth_get_sset_count,
	.get_ethtool_stats      = mv643xx_eth_get_ethtool_stats,
	.get_strings            = mv643xx_eth_get_strings,
	.nway_reset		= mv643xx_eth_nway_restart,
};


/* address handling *********************************************************/
static void uc_addr_get(struct mv643xx_eth_private *mp, unsigned char *addr)
{
	unsigned int port_num = mp->port_num;
	unsigned int mac_h;
	unsigned int mac_l;

	mac_h = rdl(mp, MAC_ADDR_HIGH(port_num));
	mac_l = rdl(mp, MAC_ADDR_LOW(port_num));

	addr[0] = (mac_h >> 24) & 0xff;
	addr[1] = (mac_h >> 16) & 0xff;
	addr[2] = (mac_h >> 8) & 0xff;
	addr[3] = mac_h & 0xff;
	addr[4] = (mac_l >> 8) & 0xff;
	addr[5] = mac_l & 0xff;
}

static void init_mac_tables(struct mv643xx_eth_private *mp)
{
	unsigned int port_num = mp->port_num;
	int table_index;

	/* Clear DA filter unicast table (Ex_dFUT) */
	for (table_index = 0; table_index <= 0xC; table_index += 4)
		wrl(mp, UNICAST_TABLE(port_num) + table_index, 0);

	for (table_index = 0; table_index <= 0xFC; table_index += 4) {
		/* Clear DA filter special multicast table (Ex_dFSMT) */
		wrl(mp, SPECIAL_MCAST_TABLE(port_num) + table_index, 0);
		/* Clear DA filter other multicast table (Ex_dFOMT) */
		wrl(mp, OTHER_MCAST_TABLE(port_num) + table_index, 0);
	}
}

static void set_filter_table_entry(struct mv643xx_eth_private *mp,
					    int table, unsigned char entry)
{
	unsigned int table_reg;
	unsigned int tbl_offset;
	unsigned int reg_offset;

	tbl_offset = (entry / 4) * 4;	/* Register offset of DA table entry */
	reg_offset = entry % 4;		/* Entry offset within the register */

	/* Set "accepts frame bit" at specified table entry */
	table_reg = rdl(mp, table + tbl_offset);
	table_reg |= 0x01 << (8 * reg_offset);
	wrl(mp, table + tbl_offset, table_reg);
}

static void uc_addr_set(struct mv643xx_eth_private *mp, unsigned char *addr)
{
	unsigned int port_num = mp->port_num;
	unsigned int mac_h;
	unsigned int mac_l;
	int table;

	mac_l = (addr[4] << 8) | (addr[5]);
	mac_h = (addr[0] << 24) | (addr[1] << 16) | (addr[2] << 8) |
							(addr[3] << 0);

	wrl(mp, MAC_ADDR_LOW(port_num), mac_l);
	wrl(mp, MAC_ADDR_HIGH(port_num), mac_h);

	/* Accept frames with this address */
	table = UNICAST_TABLE(port_num);
	set_filter_table_entry(mp, table, addr[5] & 0x0f);
}

static void mv643xx_eth_update_mac_address(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	init_mac_tables(mp);
	uc_addr_set(mp, dev->dev_addr);
}

static int mv643xx_eth_set_mac_address(struct net_device *dev, void *addr)
{
	int i;

	for (i = 0; i < 6; i++)
		/* +2 is for the offset of the HW addr type */
		dev->dev_addr[i] = ((unsigned char *)addr)[i + 2];
	mv643xx_eth_update_mac_address(dev);
	return 0;
}

static void mc_addr(struct mv643xx_eth_private *mp, unsigned char *addr)
{
	unsigned int port_num = mp->port_num;
	unsigned int mac_h;
	unsigned int mac_l;
	unsigned char crc_result = 0;
	int table;
	int mac_array[48];
	int crc[8];
	int i;

	if ((addr[0] == 0x01) && (addr[1] == 0x00) &&
	    (addr[2] == 0x5E) && (addr[3] == 0x00) && (addr[4] == 0x00)) {
		table = SPECIAL_MCAST_TABLE(port_num);
		set_filter_table_entry(mp, table, addr[5]);
		return;
	}

	/* Calculate CRC-8 out of the given address */
	mac_h = (addr[0] << 8) | (addr[1]);
	mac_l = (addr[2] << 24) | (addr[3] << 16) |
			(addr[4] << 8) | (addr[5] << 0);

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

	table = OTHER_MCAST_TABLE(port_num);
	set_filter_table_entry(mp, table, crc_result);
}

static void set_multicast_list(struct net_device *dev)
{

	struct dev_mc_list	*mc_list;
	int			i;
	int			table_index;
	struct mv643xx_eth_private	*mp = netdev_priv(dev);
	unsigned int		port_num = mp->port_num;

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
			wrl(mp, SPECIAL_MCAST_TABLE(port_num) + table_index, 0x01010101);

			/* Set all entries in DA filter other multicast
			 * table (Ex_dFOMT)
			 * Set for ETH_Q0 for now
			 * Bits
			 * 0	  Accept=1, Drop=0
			 * 3-1  Queue	 ETH_Q0=0
			 * 7-4  Reserved = 0;
			 */
			wrl(mp, OTHER_MCAST_TABLE(port_num) + table_index, 0x01010101);
		}
		return;
	}

	/* We will clear out multicast tables every time we get the list.
	 * Then add the entire new list...
	 */
	for (table_index = 0; table_index <= 0xFC; table_index += 4) {
		/* Clear DA filter special multicast table (Ex_dFSMT) */
		wrl(mp, SPECIAL_MCAST_TABLE(port_num) + table_index, 0);

		/* Clear DA filter other multicast table (Ex_dFOMT) */
		wrl(mp, OTHER_MCAST_TABLE(port_num) + table_index, 0);
	}

	/* Get pointer to net_device multicast list and add each one... */
	for (i = 0, mc_list = dev->mc_list;
			(i < 256) && (mc_list != NULL) && (i < dev->mc_count);
			i++, mc_list = mc_list->next)
		if (mc_list->dmi_addrlen == 6)
			mc_addr(mp, mc_list->dmi_addr);
}

static void mv643xx_eth_set_rx_mode(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	u32 config_reg;

	config_reg = rdl(mp, PORT_CONFIG(mp->port_num));
	if (dev->flags & IFF_PROMISC)
		config_reg |= UNICAST_PROMISCUOUS_MODE;
	else
		config_reg &= ~UNICAST_PROMISCUOUS_MODE;
	wrl(mp, PORT_CONFIG(mp->port_num), config_reg);

	set_multicast_list(dev);
}


/* rx/tx queue initialisation ***********************************************/
static void ether_init_rx_desc_ring(struct mv643xx_eth_private *mp)
{
	volatile struct rx_desc *p_rx_desc;
	int rx_desc_num = mp->rx_ring_size;
	int i;

	/* initialize the next_desc_ptr links in the Rx descriptors ring */
	p_rx_desc = (struct rx_desc *)mp->rx_desc_area;
	for (i = 0; i < rx_desc_num; i++) {
		p_rx_desc[i].next_desc_ptr = mp->rx_desc_dma +
			((i + 1) % rx_desc_num) * sizeof(struct rx_desc);
	}

	/* Save Rx desc pointer to driver struct. */
	mp->rx_curr_desc = 0;
	mp->rx_used_desc = 0;

	mp->rx_desc_area_size = rx_desc_num * sizeof(struct rx_desc);
}

static void mv643xx_eth_free_rx_rings(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int curr;

	/* Stop RX Queues */
	mv643xx_eth_port_disable_rx(mp);

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
		iounmap(mp->rx_desc_area);
	else
		dma_free_coherent(NULL, mp->rx_desc_area_size,
				mp->rx_desc_area, mp->rx_desc_dma);
}

static void ether_init_tx_desc_ring(struct mv643xx_eth_private *mp)
{
	int tx_desc_num = mp->tx_ring_size;
	struct tx_desc *p_tx_desc;
	int i;

	/* Initialize the next_desc_ptr links in the Tx descriptors ring */
	p_tx_desc = (struct tx_desc *)mp->tx_desc_area;
	for (i = 0; i < tx_desc_num; i++) {
		p_tx_desc[i].next_desc_ptr = mp->tx_desc_dma +
			((i + 1) % tx_desc_num) * sizeof(struct tx_desc);
	}

	mp->tx_curr_desc = 0;
	mp->tx_used_desc = 0;

	mp->tx_desc_area_size = tx_desc_num * sizeof(struct tx_desc);
}

static int mv643xx_eth_free_tx_descs(struct net_device *dev, int force)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	struct tx_desc *desc;
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

		tx_index = mp->tx_used_desc;
		desc = &mp->tx_desc_area[tx_index];
		cmd_sts = desc->cmd_sts;

		if (!force && (cmd_sts & BUFFER_OWNED_BY_DMA)) {
			spin_unlock_irqrestore(&mp->lock, flags);
			return released;
		}

		mp->tx_used_desc = (tx_index + 1) % mp->tx_ring_size;
		mp->tx_desc_count--;

		addr = desc->buf_ptr;
		count = desc->byte_cnt;
		skb = mp->tx_skb[tx_index];
		if (skb)
			mp->tx_skb[tx_index] = NULL;

		if (cmd_sts & ERROR_SUMMARY) {
			printk("%s: Error in TX\n", dev->name);
			dev->stats.tx_errors++;
		}

		spin_unlock_irqrestore(&mp->lock, flags);

		if (cmd_sts & TX_FIRST_DESC)
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
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	if (mv643xx_eth_free_tx_descs(dev, 0) &&
	    mp->tx_ring_size - mp->tx_desc_count >= MAX_DESCS_PER_SKB)
		netif_wake_queue(dev);
}

static void mv643xx_eth_free_all_tx_descs(struct net_device *dev)
{
	mv643xx_eth_free_tx_descs(dev, 1);
}

static void mv643xx_eth_free_tx_rings(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	/* Stop Tx Queues */
	mv643xx_eth_port_disable_tx(mp);

	/* Free outstanding skb's on TX ring */
	mv643xx_eth_free_all_tx_descs(dev);

	BUG_ON(mp->tx_used_desc != mp->tx_curr_desc);

	/* Free TX ring */
	if (mp->tx_sram_size)
		iounmap(mp->tx_desc_area);
	else
		dma_free_coherent(NULL, mp->tx_desc_area_size,
				mp->tx_desc_area, mp->tx_desc_dma);
}


/* netdev ops and related ***************************************************/
static void port_reset(struct mv643xx_eth_private *mp);

static void mv643xx_eth_update_pscr(struct net_device *dev,
				    struct ethtool_cmd *ecmd)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int port_num = mp->port_num;
	u32 o_pscr, n_pscr;
	unsigned int queues;

	o_pscr = rdl(mp, PORT_SERIAL_CONTROL(port_num));
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
			wrl(mp, PORT_SERIAL_CONTROL(port_num), n_pscr);
		else {
			queues = mv643xx_eth_port_disable_tx(mp);

			o_pscr &= ~SERIAL_PORT_ENABLE;
			wrl(mp, PORT_SERIAL_CONTROL(port_num), o_pscr);
			wrl(mp, PORT_SERIAL_CONTROL(port_num), n_pscr);
			wrl(mp, PORT_SERIAL_CONTROL(port_num), n_pscr);
			if (queues)
				mv643xx_eth_port_enable_tx(mp, queues);
		}
	}
}

static irqreturn_t mv643xx_eth_int_handler(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	u32 int_cause, int_cause_ext = 0;
	unsigned int port_num = mp->port_num;

	/* Read interrupt cause registers */
	int_cause = rdl(mp, INT_CAUSE(port_num)) & (INT_RX | INT_EXT);
	if (int_cause & INT_EXT) {
		int_cause_ext = rdl(mp, INT_CAUSE_EXT(port_num))
				& (INT_EXT_LINK | INT_EXT_PHY | INT_EXT_TX);
		wrl(mp, INT_CAUSE_EXT(port_num), ~int_cause_ext);
	}

	/* PHY status changed */
	if (int_cause_ext & (INT_EXT_LINK | INT_EXT_PHY)) {
		struct ethtool_cmd cmd;

		if (mii_link_ok(&mp->mii)) {
			mii_ethtool_gset(&mp->mii, &cmd);
			mv643xx_eth_update_pscr(dev, &cmd);
			mv643xx_eth_port_enable_tx(mp, 1);
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

#ifdef MV643XX_ETH_NAPI
	if (int_cause & INT_RX) {
		/* schedule the NAPI poll routine to maintain port */
		wrl(mp, INT_MASK(port_num), 0x00000000);

		/* wait for previous write to complete */
		rdl(mp, INT_MASK(port_num));

		netif_rx_schedule(dev, &mp->napi);
	}
#else
	if (int_cause & INT_RX)
		mv643xx_eth_receive_queue(dev, INT_MAX);
#endif
	if (int_cause_ext & INT_EXT_TX)
		mv643xx_eth_free_completed_tx_descs(dev);

	/*
	 * If no real interrupt occured, exit.
	 * This can happen when using gigE interrupt coalescing mechanism.
	 */
	if ((int_cause == 0x0) && (int_cause_ext == 0x0))
		return IRQ_NONE;

	return IRQ_HANDLED;
}

static void phy_reset(struct mv643xx_eth_private *mp)
{
	unsigned int phy_reg_data;

	/* Reset the PHY */
	read_smi_reg(mp, 0, &phy_reg_data);
	phy_reg_data |= 0x8000;	/* Set bit 15 to reset the PHY */
	write_smi_reg(mp, 0, phy_reg_data);

	/* wait for PHY to come out of reset */
	do {
		udelay(1);
		read_smi_reg(mp, 0, &phy_reg_data);
	} while (phy_reg_data & 0x8000);
}

static void port_start(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;
	int tx_curr_desc, rx_curr_desc;
	u32 pscr;
	struct ethtool_cmd ethtool_cmd;

	/* Assignment of Tx CTRP of given queue */
	tx_curr_desc = mp->tx_curr_desc;
	wrl(mp, TXQ_CURRENT_DESC_PTR(port_num),
		(u32)((struct tx_desc *)mp->tx_desc_dma + tx_curr_desc));

	/* Assignment of Rx CRDP of given queue */
	rx_curr_desc = mp->rx_curr_desc;
	wrl(mp, RXQ_CURRENT_DESC_PTR(port_num),
		(u32)((struct rx_desc *)mp->rx_desc_dma + rx_curr_desc));

	/* Add the assigned Ethernet address to the port's address table */
	uc_addr_set(mp, dev->dev_addr);

	/*
	 * Receive all unmatched unicast, TCP, UDP, BPDU and broadcast
	 * frames to RX queue #0.
	 */
	wrl(mp, PORT_CONFIG(port_num), 0x00000000);

	/*
	 * Treat BPDUs as normal multicasts, and disable partition mode.
	 */
	wrl(mp, PORT_CONFIG_EXT(port_num), 0x00000000);

	pscr = rdl(mp, PORT_SERIAL_CONTROL(port_num));

	pscr &= ~(SERIAL_PORT_ENABLE | FORCE_LINK_PASS);
	wrl(mp, PORT_SERIAL_CONTROL(port_num), pscr);

	pscr |= DISABLE_AUTO_NEG_FOR_FLOW_CTRL |
		DISABLE_AUTO_NEG_SPEED_GMII    |
		DISABLE_AUTO_NEG_FOR_DUPLEX    |
		DO_NOT_FORCE_LINK_FAIL	   |
		SERIAL_PORT_CONTROL_RESERVED;

	wrl(mp, PORT_SERIAL_CONTROL(port_num), pscr);

	pscr |= SERIAL_PORT_ENABLE;
	wrl(mp, PORT_SERIAL_CONTROL(port_num), pscr);

	/* Assign port SDMA configuration */
	wrl(mp, SDMA_CONFIG(port_num), PORT_SDMA_CONFIG_DEFAULT_VALUE);

	/* Enable port Rx. */
	mv643xx_eth_port_enable_rx(mp, 1);

	/* Disable port bandwidth limits by clearing MTU register */
	wrl(mp, TX_BW_MTU(port_num), 0);

	/* save phy settings across reset */
	mv643xx_eth_get_settings(dev, &ethtool_cmd);
	phy_reset(mp);
	mv643xx_eth_set_settings(dev, &ethtool_cmd);
}

#ifdef MV643XX_ETH_COAL
static unsigned int set_rx_coal(struct mv643xx_eth_private *mp,
					unsigned int delay)
{
	unsigned int port_num = mp->port_num;
	unsigned int coal = ((mp->shared->t_clk / 1000000) * delay) / 64;

	/* Set RX Coalescing mechanism */
	wrl(mp, SDMA_CONFIG(port_num),
		((coal & 0x3fff) << 8) |
		(rdl(mp, SDMA_CONFIG(port_num))
			& 0xffc000ff));

	return coal;
}
#endif

static unsigned int set_tx_coal(struct mv643xx_eth_private *mp,
					unsigned int delay)
{
	unsigned int coal = ((mp->shared->t_clk / 1000000) * delay) / 64;

	/* Set TX Coalescing mechanism */
	wrl(mp, TX_FIFO_URGENT_THRESHOLD(mp->port_num), coal << 4);

	return coal;
}

static void port_init(struct mv643xx_eth_private *mp)
{
	port_reset(mp);

	init_mac_tables(mp);
}

static int mv643xx_eth_open(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;
	unsigned int size;
	int err;

	/* Clear any pending ethernet port interrupts */
	wrl(mp, INT_CAUSE(port_num), 0);
	wrl(mp, INT_CAUSE_EXT(port_num), 0);
	/* wait for previous write to complete */
	rdl(mp, INT_CAUSE_EXT(port_num));

	err = request_irq(dev->irq, mv643xx_eth_int_handler,
			IRQF_SHARED | IRQF_SAMPLE_RANDOM, dev->name, dev);
	if (err) {
		printk(KERN_ERR "%s: Can not assign IRQ\n", dev->name);
		return -EAGAIN;
	}

	port_init(mp);

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
	size = mp->tx_ring_size * sizeof(struct tx_desc);
	mp->tx_desc_area_size = size;

	if (mp->tx_sram_size) {
		mp->tx_desc_area = ioremap(mp->tx_sram_addr,
							mp->tx_sram_size);
		mp->tx_desc_dma = mp->tx_sram_addr;
	} else
		mp->tx_desc_area = dma_alloc_coherent(NULL, size,
							&mp->tx_desc_dma,
							GFP_KERNEL);

	if (!mp->tx_desc_area) {
		printk(KERN_ERR "%s: Cannot allocate Tx Ring (size %d bytes)\n",
							dev->name, size);
		err = -ENOMEM;
		goto out_free_tx_skb;
	}
	BUG_ON((u32) mp->tx_desc_area & 0xf);	/* check 16-byte alignment */
	memset((void *)mp->tx_desc_area, 0, mp->tx_desc_area_size);

	ether_init_tx_desc_ring(mp);

	/* Allocate RX ring */
	mp->rx_desc_count = 0;
	size = mp->rx_ring_size * sizeof(struct rx_desc);
	mp->rx_desc_area_size = size;

	if (mp->rx_sram_size) {
		mp->rx_desc_area = ioremap(mp->rx_sram_addr,
							mp->rx_sram_size);
		mp->rx_desc_dma = mp->rx_sram_addr;
	} else
		mp->rx_desc_area = dma_alloc_coherent(NULL, size,
							&mp->rx_desc_dma,
							GFP_KERNEL);

	if (!mp->rx_desc_area) {
		printk(KERN_ERR "%s: Cannot allocate Rx ring (size %d bytes)\n",
							dev->name, size);
		printk(KERN_ERR "%s: Freeing previously allocated TX queues...",
							dev->name);
		if (mp->rx_sram_size)
			iounmap(mp->tx_desc_area);
		else
			dma_free_coherent(NULL, mp->tx_desc_area_size,
					mp->tx_desc_area, mp->tx_desc_dma);
		err = -ENOMEM;
		goto out_free_tx_skb;
	}
	memset((void *)mp->rx_desc_area, 0, size);

	ether_init_rx_desc_ring(mp);

	mv643xx_eth_rx_refill_descs(dev);	/* Fill RX ring with skb's */

#ifdef MV643XX_ETH_NAPI
	napi_enable(&mp->napi);
#endif

	port_start(dev);

	/* Interrupt Coalescing */

#ifdef MV643XX_ETH_COAL
	mp->rx_int_coal = set_rx_coal(mp, MV643XX_ETH_RX_COAL);
#endif

	mp->tx_int_coal = set_tx_coal(mp, MV643XX_ETH_TX_COAL);

	/* Unmask phy and link status changes interrupts */
	wrl(mp, INT_MASK_EXT(port_num), INT_EXT_LINK | INT_EXT_PHY | INT_EXT_TX);

	/* Unmask RX buffer and TX end interrupt */
	wrl(mp, INT_MASK(port_num), INT_RX | INT_EXT);

	return 0;

out_free_tx_skb:
	kfree(mp->tx_skb);
out_free_rx_skb:
	kfree(mp->rx_skb);
out_free_irq:
	free_irq(dev->irq, dev);

	return err;
}

static void port_reset(struct mv643xx_eth_private *mp)
{
	unsigned int port_num = mp->port_num;
	unsigned int reg_data;

	mv643xx_eth_port_disable_tx(mp);
	mv643xx_eth_port_disable_rx(mp);

	/* Clear all MIB counters */
	clear_mib_counters(mp);

	/* Reset the Enable bit in the Configuration Register */
	reg_data = rdl(mp, PORT_SERIAL_CONTROL(port_num));
	reg_data &= ~(SERIAL_PORT_ENABLE		|
			DO_NOT_FORCE_LINK_FAIL	|
			FORCE_LINK_PASS);
	wrl(mp, PORT_SERIAL_CONTROL(port_num), reg_data);
}

static int mv643xx_eth_stop(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;

	/* Mask all interrupts on ethernet port */
	wrl(mp, INT_MASK(port_num), 0x00000000);
	/* wait for previous write to complete */
	rdl(mp, INT_MASK(port_num));

#ifdef MV643XX_ETH_NAPI
	napi_disable(&mp->napi);
#endif
	netif_carrier_off(dev);
	netif_stop_queue(dev);

	port_reset(mp);

	mv643xx_eth_free_tx_rings(dev);
	mv643xx_eth_free_rx_rings(dev);

	free_irq(dev->irq, dev);

	return 0;
}

static int mv643xx_eth_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	return generic_mii_ioctl(&mp->mii, if_mii(ifr), cmd, NULL);
}

static int mv643xx_eth_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu > 9500) || (new_mtu < 64))
		return -EINVAL;

	dev->mtu = new_mtu;
	if (!netif_running(dev))
		return 0;

	/*
	 * Stop and then re-open the interface. This will allocate RX
	 * skbs of the new MTU.
	 * There is a possible danger that the open will not succeed,
	 * due to memory being full, which might fail the open function.
	 */
	mv643xx_eth_stop(dev);
	if (mv643xx_eth_open(dev)) {
		printk(KERN_ERR "%s: Fatal error on opening device\n",
			dev->name);
	}

	return 0;
}

static void mv643xx_eth_tx_timeout_task(struct work_struct *ugly)
{
	struct mv643xx_eth_private *mp = container_of(ugly, struct mv643xx_eth_private,
						  tx_timeout_task);
	struct net_device *dev = mp->dev;

	if (!netif_running(dev))
		return;

	netif_stop_queue(dev);

	port_reset(mp);
	port_start(dev);

	if (mp->tx_ring_size - mp->tx_desc_count >= MAX_DESCS_PER_SKB)
		netif_wake_queue(dev);
}

static void mv643xx_eth_tx_timeout(struct net_device *dev)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

	printk(KERN_INFO "%s: TX timeout  ", dev->name);

	/* Do the reset outside of interrupt context */
	schedule_work(&mp->tx_timeout_task);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void mv643xx_eth_netpoll(struct net_device *netdev)
{
	struct mv643xx_eth_private *mp = netdev_priv(netdev);
	int port_num = mp->port_num;

	wrl(mp, INT_MASK(port_num), 0x00000000);
	/* wait for previous write to complete */
	rdl(mp, INT_MASK(port_num));

	mv643xx_eth_int_handler(netdev->irq, netdev);

	wrl(mp, INT_MASK(port_num), INT_RX | INT_CAUSE_EXT);
}
#endif

static int mv643xx_eth_mdio_read(struct net_device *dev, int phy_id, int location)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	int val;

	read_smi_reg(mp, location, &val);
	return val;
}

static void mv643xx_eth_mdio_write(struct net_device *dev, int phy_id, int location, int val)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	write_smi_reg(mp, location, val);
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

static int mv643xx_eth_shared_probe(struct platform_device *pdev)
{
	static int mv643xx_eth_version_printed = 0;
	struct mv643xx_eth_shared_platform_data *pd = pdev->dev.platform_data;
	struct mv643xx_eth_shared_private *msp;
	struct resource *res;
	int ret;

	if (!mv643xx_eth_version_printed++)
		printk(KERN_NOTICE "MV-643xx 10/100/1000 Ethernet Driver\n");

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
	msp->t_clk = (pd != NULL && pd->t_clk != 0) ? pd->t_clk : 133000000;

	platform_set_drvdata(pdev, msp);

	/*
	 * (Re-)program MBUS remapping windows if we are asked to.
	 */
	if (pd != NULL && pd->dram != NULL)
		mv643xx_eth_conf_mbus_windows(msp, pd->dram);

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
	.probe = mv643xx_eth_shared_probe,
	.remove = mv643xx_eth_shared_remove,
	.driver = {
		.name = MV643XX_ETH_SHARED_NAME,
		.owner	= THIS_MODULE,
	},
};

static void phy_addr_set(struct mv643xx_eth_private *mp, int phy_addr)
{
	u32 reg_data;
	int addr_shift = 5 * mp->port_num;

	reg_data = rdl(mp, PHY_ADDR);
	reg_data &= ~(0x1f << addr_shift);
	reg_data |= (phy_addr & 0x1f) << addr_shift;
	wrl(mp, PHY_ADDR, reg_data);
}

static int phy_addr_get(struct mv643xx_eth_private *mp)
{
	unsigned int reg_data;

	reg_data = rdl(mp, PHY_ADDR);

	return ((reg_data >> (5 * mp->port_num)) & 0x1f);
}

static int phy_detect(struct mv643xx_eth_private *mp)
{
	unsigned int phy_reg_data0;
	int auto_neg;

	read_smi_reg(mp, 0, &phy_reg_data0);
	auto_neg = phy_reg_data0 & 0x1000;
	phy_reg_data0 ^= 0x1000;	/* invert auto_neg */
	write_smi_reg(mp, 0, phy_reg_data0);

	read_smi_reg(mp, 0, &phy_reg_data0);
	if ((phy_reg_data0 & 0x1000) == auto_neg)
		return -ENODEV;				/* change didn't take */

	phy_reg_data0 ^= 0x1000;
	write_smi_reg(mp, 0, phy_reg_data0);
	return 0;
}

static void mv643xx_init_ethtool_cmd(struct net_device *dev, int phy_address,
				     int speed, int duplex,
				     struct ethtool_cmd *cmd)
{
	struct mv643xx_eth_private *mp = netdev_priv(dev);

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

static int mv643xx_eth_probe(struct platform_device *pdev)
{
	struct mv643xx_eth_platform_data *pd;
	int port_num;
	struct mv643xx_eth_private *mp;
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

	if (pd->shared == NULL) {
		printk(KERN_ERR "No mv643xx_eth_platform_data->shared\n");
		return -ENODEV;
	}

	dev = alloc_etherdev(sizeof(struct mv643xx_eth_private));
	if (!dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, dev);

	mp = netdev_priv(dev);
	mp->dev = dev;
#ifdef MV643XX_ETH_NAPI
	netif_napi_add(dev, &mp->napi, mv643xx_eth_poll, 64);
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
	dev->poll_controller = mv643xx_eth_netpoll;
#endif

	dev->watchdog_timeo = 2 * HZ;
	dev->base_addr = 0;
	dev->change_mtu = mv643xx_eth_change_mtu;
	dev->do_ioctl = mv643xx_eth_do_ioctl;
	SET_ETHTOOL_OPS(dev, &mv643xx_eth_ethtool_ops);

#ifdef MV643XX_ETH_CHECKSUM_OFFLOAD_TX
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

	mp->shared = platform_get_drvdata(pd->shared);
	port_num = mp->port_num = pd->port_number;

	if (mp->shared->win_protect)
		wrl(mp, WINDOW_PROTECT(port_num), mp->shared->win_protect);

	mp->shared_smi = mp->shared;
	if (pd->shared_smi != NULL)
		mp->shared_smi = platform_get_drvdata(pd->shared_smi);

	/* set default config values */
	uc_addr_get(mp, dev->dev_addr);
	mp->rx_ring_size = DEFAULT_RX_QUEUE_SIZE;
	mp->tx_ring_size = DEFAULT_TX_QUEUE_SIZE;

	if (is_valid_ether_addr(pd->mac_addr))
		memcpy(dev->dev_addr, pd->mac_addr, 6);

	if (pd->phy_addr || pd->force_phy_addr)
		phy_addr_set(mp, pd->phy_addr);

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
	mp->mii.mdio_read = mv643xx_eth_mdio_read;
	mp->mii.mdio_write = mv643xx_eth_mdio_write;
	mp->mii.phy_id = phy_addr_get(mp);
	mp->mii.phy_id_mask = 0x3f;
	mp->mii.reg_num_mask = 0x1f;

	err = phy_detect(mp);
	if (err) {
		pr_debug("%s: No PHY detected at addr %d\n",
				dev->name, phy_addr_get(mp));
		goto out;
	}

	phy_reset(mp);
	mp->mii.supports_gmii = mii_check_gmii_support(&mp->mii);
	mv643xx_init_ethtool_cmd(dev, mp->mii.phy_id, speed, duplex, &cmd);
	mv643xx_eth_update_pscr(dev, &cmd);
	mv643xx_eth_set_settings(dev, &cmd);

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

#ifdef MV643XX_ETH_CHECKSUM_OFFLOAD_TX
	printk(KERN_NOTICE "%s: RX TCP/UDP Checksum Offload ON \n", dev->name);
#endif

#ifdef MV643XX_ETH_COAL
	printk(KERN_NOTICE "%s: TX and RX Interrupt Coalescing ON \n",
								dev->name);
#endif

#ifdef MV643XX_ETH_NAPI
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

static void mv643xx_eth_shutdown(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct mv643xx_eth_private *mp = netdev_priv(dev);
	unsigned int port_num = mp->port_num;

	/* Mask all interrupts on ethernet port */
	wrl(mp, INT_MASK(port_num), 0);
	rdl(mp, INT_MASK(port_num));

	port_reset(mp);
}

static struct platform_driver mv643xx_eth_driver = {
	.probe = mv643xx_eth_probe,
	.remove = mv643xx_eth_remove,
	.shutdown = mv643xx_eth_shutdown,
	.driver = {
		.name = MV643XX_ETH_NAME,
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

static void __exit mv643xx_eth_cleanup_module(void)
{
	platform_driver_unregister(&mv643xx_eth_driver);
	platform_driver_unregister(&mv643xx_eth_shared_driver);
}

module_init(mv643xx_eth_init_module);
module_exit(mv643xx_eth_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(	"Rabeeh Khoury, Assaf Hoffman, Matthew Dharm, Manish Lachwani"
		" and Dale Farnsworth");
MODULE_DESCRIPTION("Ethernet driver for Marvell MV643XX");
MODULE_ALIAS("platform:" MV643XX_ETH_NAME);
MODULE_ALIAS("platform:" MV643XX_ETH_SHARED_NAME);
