/*
 * Driver for Marvell NETA network card for Armada XP and Armada 370 SoCs.
 *
 * Copyright (C) 2012 Marvell
 *
 * Rami Rosen <rosenr@marvell.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include <linux/inetdevice.h>
#include <linux/mbus.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_address.h>
#include <linux/phy.h>

/* Registers */
#define MVNETA_RXQ_CONFIG_REG(q)                (0x1400 + ((q) << 2))
#define      MVNETA_RXQ_HW_BUF_ALLOC            BIT(1)
#define      MVNETA_RXQ_PKT_OFFSET_ALL_MASK     (0xf    << 8)
#define      MVNETA_RXQ_PKT_OFFSET_MASK(offs)   ((offs) << 8)
#define MVNETA_RXQ_THRESHOLD_REG(q)             (0x14c0 + ((q) << 2))
#define      MVNETA_RXQ_NON_OCCUPIED(v)         ((v) << 16)
#define MVNETA_RXQ_BASE_ADDR_REG(q)             (0x1480 + ((q) << 2))
#define MVNETA_RXQ_SIZE_REG(q)                  (0x14a0 + ((q) << 2))
#define      MVNETA_RXQ_BUF_SIZE_SHIFT          19
#define      MVNETA_RXQ_BUF_SIZE_MASK           (0x1fff << 19)
#define MVNETA_RXQ_STATUS_REG(q)                (0x14e0 + ((q) << 2))
#define      MVNETA_RXQ_OCCUPIED_ALL_MASK       0x3fff
#define MVNETA_RXQ_STATUS_UPDATE_REG(q)         (0x1500 + ((q) << 2))
#define      MVNETA_RXQ_ADD_NON_OCCUPIED_SHIFT  16
#define      MVNETA_RXQ_ADD_NON_OCCUPIED_MAX    255
#define MVNETA_PORT_RX_RESET                    0x1cc0
#define      MVNETA_PORT_RX_DMA_RESET           BIT(0)
#define MVNETA_PHY_ADDR                         0x2000
#define      MVNETA_PHY_ADDR_MASK               0x1f
#define MVNETA_MBUS_RETRY                       0x2010
#define MVNETA_UNIT_INTR_CAUSE                  0x2080
#define MVNETA_UNIT_CONTROL                     0x20B0
#define      MVNETA_PHY_POLLING_ENABLE          BIT(1)
#define MVNETA_WIN_BASE(w)                      (0x2200 + ((w) << 3))
#define MVNETA_WIN_SIZE(w)                      (0x2204 + ((w) << 3))
#define MVNETA_WIN_REMAP(w)                     (0x2280 + ((w) << 2))
#define MVNETA_BASE_ADDR_ENABLE                 0x2290
#define MVNETA_PORT_CONFIG                      0x2400
#define      MVNETA_UNI_PROMISC_MODE            BIT(0)
#define      MVNETA_DEF_RXQ(q)                  ((q) << 1)
#define      MVNETA_DEF_RXQ_ARP(q)              ((q) << 4)
#define      MVNETA_TX_UNSET_ERR_SUM            BIT(12)
#define      MVNETA_DEF_RXQ_TCP(q)              ((q) << 16)
#define      MVNETA_DEF_RXQ_UDP(q)              ((q) << 19)
#define      MVNETA_DEF_RXQ_BPDU(q)             ((q) << 22)
#define      MVNETA_RX_CSUM_WITH_PSEUDO_HDR     BIT(25)
#define      MVNETA_PORT_CONFIG_DEFL_VALUE(q)   (MVNETA_DEF_RXQ(q)       | \
						 MVNETA_DEF_RXQ_ARP(q)	 | \
						 MVNETA_DEF_RXQ_TCP(q)	 | \
						 MVNETA_DEF_RXQ_UDP(q)	 | \
						 MVNETA_DEF_RXQ_BPDU(q)	 | \
						 MVNETA_TX_UNSET_ERR_SUM | \
						 MVNETA_RX_CSUM_WITH_PSEUDO_HDR)
#define MVNETA_PORT_CONFIG_EXTEND                0x2404
#define MVNETA_MAC_ADDR_LOW                      0x2414
#define MVNETA_MAC_ADDR_HIGH                     0x2418
#define MVNETA_SDMA_CONFIG                       0x241c
#define      MVNETA_SDMA_BRST_SIZE_16            4
#define      MVNETA_NO_DESC_SWAP                 0x0
#define      MVNETA_RX_BRST_SZ_MASK(burst)       ((burst) << 1)
#define      MVNETA_RX_NO_DATA_SWAP              BIT(4)
#define      MVNETA_TX_NO_DATA_SWAP              BIT(5)
#define      MVNETA_TX_BRST_SZ_MASK(burst)       ((burst) << 22)
#define MVNETA_PORT_STATUS                       0x2444
#define      MVNETA_TX_IN_PRGRS                  BIT(1)
#define      MVNETA_TX_FIFO_EMPTY                BIT(8)
#define MVNETA_RX_MIN_FRAME_SIZE                 0x247c
#define MVNETA_TYPE_PRIO                         0x24bc
#define      MVNETA_FORCE_UNI                    BIT(21)
#define MVNETA_TXQ_CMD_1                         0x24e4
#define MVNETA_TXQ_CMD                           0x2448
#define      MVNETA_TXQ_DISABLE_SHIFT            8
#define      MVNETA_TXQ_ENABLE_MASK              0x000000ff
#define MVNETA_ACC_MODE                          0x2500
#define MVNETA_CPU_MAP(cpu)                      (0x2540 + ((cpu) << 2))
#define      MVNETA_CPU_RXQ_ACCESS_ALL_MASK      0x000000ff
#define      MVNETA_CPU_TXQ_ACCESS_ALL_MASK      0x0000ff00
#define MVNETA_RXQ_TIME_COAL_REG(q)              (0x2580 + ((q) << 2))
#define MVNETA_INTR_NEW_CAUSE                    0x25a0
#define      MVNETA_RX_INTR_MASK(nr_rxqs)        (((1 << nr_rxqs) - 1) << 8)
#define MVNETA_INTR_NEW_MASK                     0x25a4
#define MVNETA_INTR_OLD_CAUSE                    0x25a8
#define MVNETA_INTR_OLD_MASK                     0x25ac
#define MVNETA_INTR_MISC_CAUSE                   0x25b0
#define MVNETA_INTR_MISC_MASK                    0x25b4
#define MVNETA_INTR_ENABLE                       0x25b8
#define      MVNETA_TXQ_INTR_ENABLE_ALL_MASK     0x0000ff00
#define      MVNETA_RXQ_INTR_ENABLE_ALL_MASK     0xff000000
#define MVNETA_RXQ_CMD                           0x2680
#define      MVNETA_RXQ_DISABLE_SHIFT            8
#define      MVNETA_RXQ_ENABLE_MASK              0x000000ff
#define MVETH_TXQ_TOKEN_COUNT_REG(q)             (0x2700 + ((q) << 4))
#define MVETH_TXQ_TOKEN_CFG_REG(q)               (0x2704 + ((q) << 4))
#define MVNETA_GMAC_CTRL_0                       0x2c00
#define      MVNETA_GMAC_MAX_RX_SIZE_SHIFT       2
#define      MVNETA_GMAC_MAX_RX_SIZE_MASK        0x7ffc
#define      MVNETA_GMAC0_PORT_ENABLE            BIT(0)
#define MVNETA_GMAC_CTRL_2                       0x2c08
#define      MVNETA_GMAC2_PSC_ENABLE             BIT(3)
#define      MVNETA_GMAC2_PORT_RGMII             BIT(4)
#define      MVNETA_GMAC2_PORT_RESET             BIT(6)
#define MVNETA_GMAC_STATUS                       0x2c10
#define      MVNETA_GMAC_LINK_UP                 BIT(0)
#define      MVNETA_GMAC_SPEED_1000              BIT(1)
#define      MVNETA_GMAC_SPEED_100               BIT(2)
#define      MVNETA_GMAC_FULL_DUPLEX             BIT(3)
#define      MVNETA_GMAC_RX_FLOW_CTRL_ENABLE     BIT(4)
#define      MVNETA_GMAC_TX_FLOW_CTRL_ENABLE     BIT(5)
#define      MVNETA_GMAC_RX_FLOW_CTRL_ACTIVE     BIT(6)
#define      MVNETA_GMAC_TX_FLOW_CTRL_ACTIVE     BIT(7)
#define MVNETA_GMAC_AUTONEG_CONFIG               0x2c0c
#define      MVNETA_GMAC_FORCE_LINK_DOWN         BIT(0)
#define      MVNETA_GMAC_FORCE_LINK_PASS         BIT(1)
#define      MVNETA_GMAC_CONFIG_MII_SPEED        BIT(5)
#define      MVNETA_GMAC_CONFIG_GMII_SPEED       BIT(6)
#define      MVNETA_GMAC_CONFIG_FULL_DUPLEX      BIT(12)
#define MVNETA_MIB_COUNTERS_BASE                 0x3080
#define      MVNETA_MIB_LATE_COLLISION           0x7c
#define MVNETA_DA_FILT_SPEC_MCAST                0x3400
#define MVNETA_DA_FILT_OTH_MCAST                 0x3500
#define MVNETA_DA_FILT_UCAST_BASE                0x3600
#define MVNETA_TXQ_BASE_ADDR_REG(q)              (0x3c00 + ((q) << 2))
#define MVNETA_TXQ_SIZE_REG(q)                   (0x3c20 + ((q) << 2))
#define      MVNETA_TXQ_SENT_THRESH_ALL_MASK     0x3fff0000
#define      MVNETA_TXQ_SENT_THRESH_MASK(coal)   ((coal) << 16)
#define MVNETA_TXQ_UPDATE_REG(q)                 (0x3c60 + ((q) << 2))
#define      MVNETA_TXQ_DEC_SENT_SHIFT           16
#define MVNETA_TXQ_STATUS_REG(q)                 (0x3c40 + ((q) << 2))
#define      MVNETA_TXQ_SENT_DESC_SHIFT          16
#define      MVNETA_TXQ_SENT_DESC_MASK           0x3fff0000
#define MVNETA_PORT_TX_RESET                     0x3cf0
#define      MVNETA_PORT_TX_DMA_RESET            BIT(0)
#define MVNETA_TX_MTU                            0x3e0c
#define MVNETA_TX_TOKEN_SIZE                     0x3e14
#define      MVNETA_TX_TOKEN_SIZE_MAX            0xffffffff
#define MVNETA_TXQ_TOKEN_SIZE_REG(q)             (0x3e40 + ((q) << 2))
#define      MVNETA_TXQ_TOKEN_SIZE_MAX           0x7fffffff

#define MVNETA_CAUSE_TXQ_SENT_DESC_ALL_MASK	 0xff

/* Descriptor ring Macros */
#define MVNETA_QUEUE_NEXT_DESC(q, index)	\
	(((index) < (q)->last_desc) ? ((index) + 1) : 0)

/* Various constants */

/* Coalescing */
#define MVNETA_TXDONE_COAL_PKTS		16
#define MVNETA_RX_COAL_PKTS		32
#define MVNETA_RX_COAL_USEC		100

/* Timer */
#define MVNETA_TX_DONE_TIMER_PERIOD	10

/* Napi polling weight */
#define MVNETA_RX_POLL_WEIGHT		64

/* The two bytes Marvell header. Either contains a special value used
 * by Marvell switches when a specific hardware mode is enabled (not
 * supported by this driver) or is filled automatically by zeroes on
 * the RX side. Those two bytes being at the front of the Ethernet
 * header, they allow to have the IP header aligned on a 4 bytes
 * boundary automatically: the hardware skips those two bytes on its
 * own.
 */
#define MVNETA_MH_SIZE			2

#define MVNETA_VLAN_TAG_LEN             4

#define MVNETA_CPU_D_CACHE_LINE_SIZE    32
#define MVNETA_TX_CSUM_MAX_SIZE		9800
#define MVNETA_ACC_MODE_EXT		1

/* Timeout constants */
#define MVNETA_TX_DISABLE_TIMEOUT_MSEC	1000
#define MVNETA_RX_DISABLE_TIMEOUT_MSEC	1000
#define MVNETA_TX_FIFO_EMPTY_TIMEOUT	10000

#define MVNETA_TX_MTU_MAX		0x3ffff

/* Max number of Rx descriptors */
#define MVNETA_MAX_RXD 128

/* Max number of Tx descriptors */
#define MVNETA_MAX_TXD 532

/* descriptor aligned size */
#define MVNETA_DESC_ALIGNED_SIZE	32

#define MVNETA_RX_PKT_SIZE(mtu) \
	ALIGN((mtu) + MVNETA_MH_SIZE + MVNETA_VLAN_TAG_LEN + \
	      ETH_HLEN + ETH_FCS_LEN,			     \
	      MVNETA_CPU_D_CACHE_LINE_SIZE)

#define MVNETA_RX_BUF_SIZE(pkt_size)   ((pkt_size) + NET_SKB_PAD)

struct mvneta_stats {
	struct	u64_stats_sync syncp;
	u64	packets;
	u64	bytes;
};

struct mvneta_port {
	int pkt_size;
	void __iomem *base;
	struct mvneta_rx_queue *rxqs;
	struct mvneta_tx_queue *txqs;
	struct timer_list tx_done_timer;
	struct net_device *dev;

	u32 cause_rx_tx;
	struct napi_struct napi;

	/* Flags */
	unsigned long flags;
#define MVNETA_F_TX_DONE_TIMER_BIT  0

	/* Napi weight */
	int weight;

	/* Core clock */
	unsigned int clk_rate_hz;
	u8 mcast_count[256];
	u16 tx_ring_size;
	u16 rx_ring_size;
	struct mvneta_stats tx_stats;
	struct mvneta_stats rx_stats;

	struct mii_bus *mii_bus;
	struct phy_device *phy_dev;
	phy_interface_t phy_interface;
	struct device_node *phy_node;
	unsigned int link;
	unsigned int duplex;
	unsigned int speed;
};

/* The mvneta_tx_desc and mvneta_rx_desc structures describe the
 * layout of the transmit and reception DMA descriptors, and their
 * layout is therefore defined by the hardware design
 */
struct mvneta_tx_desc {
	u32  command;		/* Options used by HW for packet transmitting.*/
#define MVNETA_TX_L3_OFF_SHIFT	0
#define MVNETA_TX_IP_HLEN_SHIFT	8
#define MVNETA_TX_L4_UDP	BIT(16)
#define MVNETA_TX_L3_IP6	BIT(17)
#define MVNETA_TXD_IP_CSUM	BIT(18)
#define MVNETA_TXD_Z_PAD	BIT(19)
#define MVNETA_TXD_L_DESC	BIT(20)
#define MVNETA_TXD_F_DESC	BIT(21)
#define MVNETA_TXD_FLZ_DESC	(MVNETA_TXD_Z_PAD  | \
				 MVNETA_TXD_L_DESC | \
				 MVNETA_TXD_F_DESC)
#define MVNETA_TX_L4_CSUM_FULL	BIT(30)
#define MVNETA_TX_L4_CSUM_NOT	BIT(31)

	u16  reserverd1;	/* csum_l4 (for future use)		*/
	u16  data_size;		/* Data size of transmitted packet in bytes */
	u32  buf_phys_addr;	/* Physical addr of transmitted buffer	*/
	u32  reserved2;		/* hw_cmd - (for future use, PMT)	*/
	u32  reserved3[4];	/* Reserved - (for future use)		*/
};

struct mvneta_rx_desc {
	u32  status;		/* Info about received packet		*/
#define MVNETA_RXD_ERR_CRC		0x0
#define MVNETA_RXD_ERR_SUMMARY		BIT(16)
#define MVNETA_RXD_ERR_OVERRUN		BIT(17)
#define MVNETA_RXD_ERR_LEN		BIT(18)
#define MVNETA_RXD_ERR_RESOURCE		(BIT(17) | BIT(18))
#define MVNETA_RXD_ERR_CODE_MASK	(BIT(17) | BIT(18))
#define MVNETA_RXD_L3_IP4		BIT(25)
#define MVNETA_RXD_FIRST_LAST_DESC	(BIT(26) | BIT(27))
#define MVNETA_RXD_L4_CSUM_OK		BIT(30)

	u16  reserved1;		/* pnc_info - (for future use, PnC)	*/
	u16  data_size;		/* Size of received packet in bytes	*/
	u32  buf_phys_addr;	/* Physical address of the buffer	*/
	u32  reserved2;		/* pnc_flow_id  (for future use, PnC)	*/
	u32  buf_cookie;	/* cookie for access to RX buffer in rx path */
	u16  reserved3;		/* prefetch_cmd, for future use		*/
	u16  reserved4;		/* csum_l4 - (for future use, PnC)	*/
	u32  reserved5;		/* pnc_extra PnC (for future use, PnC)	*/
	u32  reserved6;		/* hw_cmd (for future use, PnC and HWF)	*/
};

struct mvneta_tx_queue {
	/* Number of this TX queue, in the range 0-7 */
	u8 id;

	/* Number of TX DMA descriptors in the descriptor ring */
	int size;

	/* Number of currently used TX DMA descriptor in the
	 * descriptor ring
	 */
	int count;

	/* Array of transmitted skb */
	struct sk_buff **tx_skb;

	/* Index of last TX DMA descriptor that was inserted */
	int txq_put_index;

	/* Index of the TX DMA descriptor to be cleaned up */
	int txq_get_index;

	u32 done_pkts_coal;

	/* Virtual address of the TX DMA descriptors array */
	struct mvneta_tx_desc *descs;

	/* DMA address of the TX DMA descriptors array */
	dma_addr_t descs_phys;

	/* Index of the last TX DMA descriptor */
	int last_desc;

	/* Index of the next TX DMA descriptor to process */
	int next_desc_to_proc;
};

struct mvneta_rx_queue {
	/* rx queue number, in the range 0-7 */
	u8 id;

	/* num of rx descriptors in the rx descriptor ring */
	int size;

	/* counter of times when mvneta_refill() failed */
	int missed;

	u32 pkts_coal;
	u32 time_coal;

	/* Virtual address of the RX DMA descriptors array */
	struct mvneta_rx_desc *descs;

	/* DMA address of the RX DMA descriptors array */
	dma_addr_t descs_phys;

	/* Index of the last RX DMA descriptor */
	int last_desc;

	/* Index of the next RX DMA descriptor to process */
	int next_desc_to_proc;
};

static int rxq_number = 8;
static int txq_number = 8;

static int rxq_def;
static int txq_def;

#define MVNETA_DRIVER_NAME "mvneta"
#define MVNETA_DRIVER_VERSION "1.0"

/* Utility/helper methods */

/* Write helper method */
static void mvreg_write(struct mvneta_port *pp, u32 offset, u32 data)
{
	writel(data, pp->base + offset);
}

/* Read helper method */
static u32 mvreg_read(struct mvneta_port *pp, u32 offset)
{
	return readl(pp->base + offset);
}

/* Increment txq get counter */
static void mvneta_txq_inc_get(struct mvneta_tx_queue *txq)
{
	txq->txq_get_index++;
	if (txq->txq_get_index == txq->size)
		txq->txq_get_index = 0;
}

/* Increment txq put counter */
static void mvneta_txq_inc_put(struct mvneta_tx_queue *txq)
{
	txq->txq_put_index++;
	if (txq->txq_put_index == txq->size)
		txq->txq_put_index = 0;
}


/* Clear all MIB counters */
static void mvneta_mib_counters_clear(struct mvneta_port *pp)
{
	int i;
	u32 dummy;

	/* Perform dummy reads from MIB counters */
	for (i = 0; i < MVNETA_MIB_LATE_COLLISION; i += 4)
		dummy = mvreg_read(pp, (MVNETA_MIB_COUNTERS_BASE + i));
}

/* Get System Network Statistics */
struct rtnl_link_stats64 *mvneta_get_stats64(struct net_device *dev,
					     struct rtnl_link_stats64 *stats)
{
	struct mvneta_port *pp = netdev_priv(dev);
	unsigned int start;

	memset(stats, 0, sizeof(struct rtnl_link_stats64));

	do {
		start = u64_stats_fetch_begin_bh(&pp->rx_stats.syncp);
		stats->rx_packets = pp->rx_stats.packets;
		stats->rx_bytes	= pp->rx_stats.bytes;
	} while (u64_stats_fetch_retry_bh(&pp->rx_stats.syncp, start));


	do {
		start = u64_stats_fetch_begin_bh(&pp->tx_stats.syncp);
		stats->tx_packets = pp->tx_stats.packets;
		stats->tx_bytes	= pp->tx_stats.bytes;
	} while (u64_stats_fetch_retry_bh(&pp->tx_stats.syncp, start));

	stats->rx_errors	= dev->stats.rx_errors;
	stats->rx_dropped	= dev->stats.rx_dropped;

	stats->tx_dropped	= dev->stats.tx_dropped;

	return stats;
}

/* Rx descriptors helper methods */

/* Checks whether the given RX descriptor is both the first and the
 * last descriptor for the RX packet. Each RX packet is currently
 * received through a single RX descriptor, so not having each RX
 * descriptor with its first and last bits set is an error
 */
static int mvneta_rxq_desc_is_first_last(struct mvneta_rx_desc *desc)
{
	return (desc->status & MVNETA_RXD_FIRST_LAST_DESC) ==
		MVNETA_RXD_FIRST_LAST_DESC;
}

/* Add number of descriptors ready to receive new packets */
static void mvneta_rxq_non_occup_desc_add(struct mvneta_port *pp,
					  struct mvneta_rx_queue *rxq,
					  int ndescs)
{
	/* Only MVNETA_RXQ_ADD_NON_OCCUPIED_MAX (255) descriptors can
	 * be added at once
	 */
	while (ndescs > MVNETA_RXQ_ADD_NON_OCCUPIED_MAX) {
		mvreg_write(pp, MVNETA_RXQ_STATUS_UPDATE_REG(rxq->id),
			    (MVNETA_RXQ_ADD_NON_OCCUPIED_MAX <<
			     MVNETA_RXQ_ADD_NON_OCCUPIED_SHIFT));
		ndescs -= MVNETA_RXQ_ADD_NON_OCCUPIED_MAX;
	}

	mvreg_write(pp, MVNETA_RXQ_STATUS_UPDATE_REG(rxq->id),
		    (ndescs << MVNETA_RXQ_ADD_NON_OCCUPIED_SHIFT));
}

/* Get number of RX descriptors occupied by received packets */
static int mvneta_rxq_busy_desc_num_get(struct mvneta_port *pp,
					struct mvneta_rx_queue *rxq)
{
	u32 val;

	val = mvreg_read(pp, MVNETA_RXQ_STATUS_REG(rxq->id));
	return val & MVNETA_RXQ_OCCUPIED_ALL_MASK;
}

/* Update num of rx desc called upon return from rx path or
 * from mvneta_rxq_drop_pkts().
 */
static void mvneta_rxq_desc_num_update(struct mvneta_port *pp,
				       struct mvneta_rx_queue *rxq,
				       int rx_done, int rx_filled)
{
	u32 val;

	if ((rx_done <= 0xff) && (rx_filled <= 0xff)) {
		val = rx_done |
		  (rx_filled << MVNETA_RXQ_ADD_NON_OCCUPIED_SHIFT);
		mvreg_write(pp, MVNETA_RXQ_STATUS_UPDATE_REG(rxq->id), val);
		return;
	}

	/* Only 255 descriptors can be added at once */
	while ((rx_done > 0) || (rx_filled > 0)) {
		if (rx_done <= 0xff) {
			val = rx_done;
			rx_done = 0;
		} else {
			val = 0xff;
			rx_done -= 0xff;
		}
		if (rx_filled <= 0xff) {
			val |= rx_filled << MVNETA_RXQ_ADD_NON_OCCUPIED_SHIFT;
			rx_filled = 0;
		} else {
			val |= 0xff << MVNETA_RXQ_ADD_NON_OCCUPIED_SHIFT;
			rx_filled -= 0xff;
		}
		mvreg_write(pp, MVNETA_RXQ_STATUS_UPDATE_REG(rxq->id), val);
	}
}

/* Get pointer to next RX descriptor to be processed by SW */
static struct mvneta_rx_desc *
mvneta_rxq_next_desc_get(struct mvneta_rx_queue *rxq)
{
	int rx_desc = rxq->next_desc_to_proc;

	rxq->next_desc_to_proc = MVNETA_QUEUE_NEXT_DESC(rxq, rx_desc);
	return rxq->descs + rx_desc;
}

/* Change maximum receive size of the port. */
static void mvneta_max_rx_size_set(struct mvneta_port *pp, int max_rx_size)
{
	u32 val;

	val =  mvreg_read(pp, MVNETA_GMAC_CTRL_0);
	val &= ~MVNETA_GMAC_MAX_RX_SIZE_MASK;
	val |= ((max_rx_size - MVNETA_MH_SIZE) / 2) <<
		MVNETA_GMAC_MAX_RX_SIZE_SHIFT;
	mvreg_write(pp, MVNETA_GMAC_CTRL_0, val);
}


/* Set rx queue offset */
static void mvneta_rxq_offset_set(struct mvneta_port *pp,
				  struct mvneta_rx_queue *rxq,
				  int offset)
{
	u32 val;

	val = mvreg_read(pp, MVNETA_RXQ_CONFIG_REG(rxq->id));
	val &= ~MVNETA_RXQ_PKT_OFFSET_ALL_MASK;

	/* Offset is in */
	val |= MVNETA_RXQ_PKT_OFFSET_MASK(offset >> 3);
	mvreg_write(pp, MVNETA_RXQ_CONFIG_REG(rxq->id), val);
}


/* Tx descriptors helper methods */

/* Update HW with number of TX descriptors to be sent */
static void mvneta_txq_pend_desc_add(struct mvneta_port *pp,
				     struct mvneta_tx_queue *txq,
				     int pend_desc)
{
	u32 val;

	/* Only 255 descriptors can be added at once ; Assume caller
	 * process TX desriptors in quanta less than 256
	 */
	val = pend_desc;
	mvreg_write(pp, MVNETA_TXQ_UPDATE_REG(txq->id), val);
}

/* Get pointer to next TX descriptor to be processed (send) by HW */
static struct mvneta_tx_desc *
mvneta_txq_next_desc_get(struct mvneta_tx_queue *txq)
{
	int tx_desc = txq->next_desc_to_proc;

	txq->next_desc_to_proc = MVNETA_QUEUE_NEXT_DESC(txq, tx_desc);
	return txq->descs + tx_desc;
}

/* Release the last allocated TX descriptor. Useful to handle DMA
 * mapping failures in the TX path.
 */
static void mvneta_txq_desc_put(struct mvneta_tx_queue *txq)
{
	if (txq->next_desc_to_proc == 0)
		txq->next_desc_to_proc = txq->last_desc - 1;
	else
		txq->next_desc_to_proc--;
}

/* Set rxq buf size */
static void mvneta_rxq_buf_size_set(struct mvneta_port *pp,
				    struct mvneta_rx_queue *rxq,
				    int buf_size)
{
	u32 val;

	val = mvreg_read(pp, MVNETA_RXQ_SIZE_REG(rxq->id));

	val &= ~MVNETA_RXQ_BUF_SIZE_MASK;
	val |= ((buf_size >> 3) << MVNETA_RXQ_BUF_SIZE_SHIFT);

	mvreg_write(pp, MVNETA_RXQ_SIZE_REG(rxq->id), val);
}

/* Disable buffer management (BM) */
static void mvneta_rxq_bm_disable(struct mvneta_port *pp,
				  struct mvneta_rx_queue *rxq)
{
	u32 val;

	val = mvreg_read(pp, MVNETA_RXQ_CONFIG_REG(rxq->id));
	val &= ~MVNETA_RXQ_HW_BUF_ALLOC;
	mvreg_write(pp, MVNETA_RXQ_CONFIG_REG(rxq->id), val);
}



/* Sets the RGMII Enable bit (RGMIIEn) in port MAC control register */
static void __devinit mvneta_gmac_rgmii_set(struct mvneta_port *pp, int enable)
{
	u32  val;

	val = mvreg_read(pp, MVNETA_GMAC_CTRL_2);

	if (enable)
		val |= MVNETA_GMAC2_PORT_RGMII;
	else
		val &= ~MVNETA_GMAC2_PORT_RGMII;

	mvreg_write(pp, MVNETA_GMAC_CTRL_2, val);
}

/* Config SGMII port */
static void __devinit mvneta_port_sgmii_config(struct mvneta_port *pp)
{
	u32 val;

	val = mvreg_read(pp, MVNETA_GMAC_CTRL_2);
	val |= MVNETA_GMAC2_PSC_ENABLE;
	mvreg_write(pp, MVNETA_GMAC_CTRL_2, val);
}

/* Start the Ethernet port RX and TX activity */
static void mvneta_port_up(struct mvneta_port *pp)
{
	int queue;
	u32 q_map;

	/* Enable all initialized TXs. */
	mvneta_mib_counters_clear(pp);
	q_map = 0;
	for (queue = 0; queue < txq_number; queue++) {
		struct mvneta_tx_queue *txq = &pp->txqs[queue];
		if (txq->descs != NULL)
			q_map |= (1 << queue);
	}
	mvreg_write(pp, MVNETA_TXQ_CMD, q_map);

	/* Enable all initialized RXQs. */
	q_map = 0;
	for (queue = 0; queue < rxq_number; queue++) {
		struct mvneta_rx_queue *rxq = &pp->rxqs[queue];
		if (rxq->descs != NULL)
			q_map |= (1 << queue);
	}

	mvreg_write(pp, MVNETA_RXQ_CMD, q_map);
}

/* Stop the Ethernet port activity */
static void mvneta_port_down(struct mvneta_port *pp)
{
	u32 val;
	int count;

	/* Stop Rx port activity. Check port Rx activity. */
	val = mvreg_read(pp, MVNETA_RXQ_CMD) & MVNETA_RXQ_ENABLE_MASK;

	/* Issue stop command for active channels only */
	if (val != 0)
		mvreg_write(pp, MVNETA_RXQ_CMD,
			    val << MVNETA_RXQ_DISABLE_SHIFT);

	/* Wait for all Rx activity to terminate. */
	count = 0;
	do {
		if (count++ >= MVNETA_RX_DISABLE_TIMEOUT_MSEC) {
			netdev_warn(pp->dev,
				    "TIMEOUT for RX stopped ! rx_queue_cmd: 0x08%x\n",
				    val);
			break;
		}
		mdelay(1);

		val = mvreg_read(pp, MVNETA_RXQ_CMD);
	} while (val & 0xff);

	/* Stop Tx port activity. Check port Tx activity. Issue stop
	 * command for active channels only
	 */
	val = (mvreg_read(pp, MVNETA_TXQ_CMD)) & MVNETA_TXQ_ENABLE_MASK;

	if (val != 0)
		mvreg_write(pp, MVNETA_TXQ_CMD,
			    (val << MVNETA_TXQ_DISABLE_SHIFT));

	/* Wait for all Tx activity to terminate. */
	count = 0;
	do {
		if (count++ >= MVNETA_TX_DISABLE_TIMEOUT_MSEC) {
			netdev_warn(pp->dev,
				    "TIMEOUT for TX stopped status=0x%08x\n",
				    val);
			break;
		}
		mdelay(1);

		/* Check TX Command reg that all Txqs are stopped */
		val = mvreg_read(pp, MVNETA_TXQ_CMD);

	} while (val & 0xff);

	/* Double check to verify that TX FIFO is empty */
	count = 0;
	do {
		if (count++ >= MVNETA_TX_FIFO_EMPTY_TIMEOUT) {
			netdev_warn(pp->dev,
				    "TX FIFO empty timeout status=0x08%x\n",
				    val);
			break;
		}
		mdelay(1);

		val = mvreg_read(pp, MVNETA_PORT_STATUS);
	} while (!(val & MVNETA_TX_FIFO_EMPTY) &&
		 (val & MVNETA_TX_IN_PRGRS));

	udelay(200);
}

/* Enable the port by setting the port enable bit of the MAC control register */
static void mvneta_port_enable(struct mvneta_port *pp)
{
	u32 val;

	/* Enable port */
	val = mvreg_read(pp, MVNETA_GMAC_CTRL_0);
	val |= MVNETA_GMAC0_PORT_ENABLE;
	mvreg_write(pp, MVNETA_GMAC_CTRL_0, val);
}

/* Disable the port and wait for about 200 usec before retuning */
static void mvneta_port_disable(struct mvneta_port *pp)
{
	u32 val;

	/* Reset the Enable bit in the Serial Control Register */
	val = mvreg_read(pp, MVNETA_GMAC_CTRL_0);
	val &= ~MVNETA_GMAC0_PORT_ENABLE;
	mvreg_write(pp, MVNETA_GMAC_CTRL_0, val);

	udelay(200);
}

/* Multicast tables methods */

/* Set all entries in Unicast MAC Table; queue==-1 means reject all */
static void mvneta_set_ucast_table(struct mvneta_port *pp, int queue)
{
	int offset;
	u32 val;

	if (queue == -1) {
		val = 0;
	} else {
		val = 0x1 | (queue << 1);
		val |= (val << 24) | (val << 16) | (val << 8);
	}

	for (offset = 0; offset <= 0xc; offset += 4)
		mvreg_write(pp, MVNETA_DA_FILT_UCAST_BASE + offset, val);
}

/* Set all entries in Special Multicast MAC Table; queue==-1 means reject all */
static void mvneta_set_special_mcast_table(struct mvneta_port *pp, int queue)
{
	int offset;
	u32 val;

	if (queue == -1) {
		val = 0;
	} else {
		val = 0x1 | (queue << 1);
		val |= (val << 24) | (val << 16) | (val << 8);
	}

	for (offset = 0; offset <= 0xfc; offset += 4)
		mvreg_write(pp, MVNETA_DA_FILT_SPEC_MCAST + offset, val);

}

/* Set all entries in Other Multicast MAC Table. queue==-1 means reject all */
static void mvneta_set_other_mcast_table(struct mvneta_port *pp, int queue)
{
	int offset;
	u32 val;

	if (queue == -1) {
		memset(pp->mcast_count, 0, sizeof(pp->mcast_count));
		val = 0;
	} else {
		memset(pp->mcast_count, 1, sizeof(pp->mcast_count));
		val = 0x1 | (queue << 1);
		val |= (val << 24) | (val << 16) | (val << 8);
	}

	for (offset = 0; offset <= 0xfc; offset += 4)
		mvreg_write(pp, MVNETA_DA_FILT_OTH_MCAST + offset, val);
}

/* This method sets defaults to the NETA port:
 *	Clears interrupt Cause and Mask registers.
 *	Clears all MAC tables.
 *	Sets defaults to all registers.
 *	Resets RX and TX descriptor rings.
 *	Resets PHY.
 * This method can be called after mvneta_port_down() to return the port
 *	settings to defaults.
 */
static void mvneta_defaults_set(struct mvneta_port *pp)
{
	int cpu;
	int queue;
	u32 val;

	/* Clear all Cause registers */
	mvreg_write(pp, MVNETA_INTR_NEW_CAUSE, 0);
	mvreg_write(pp, MVNETA_INTR_OLD_CAUSE, 0);
	mvreg_write(pp, MVNETA_INTR_MISC_CAUSE, 0);

	/* Mask all interrupts */
	mvreg_write(pp, MVNETA_INTR_NEW_MASK, 0);
	mvreg_write(pp, MVNETA_INTR_OLD_MASK, 0);
	mvreg_write(pp, MVNETA_INTR_MISC_MASK, 0);
	mvreg_write(pp, MVNETA_INTR_ENABLE, 0);

	/* Enable MBUS Retry bit16 */
	mvreg_write(pp, MVNETA_MBUS_RETRY, 0x20);

	/* Set CPU queue access map - all CPUs have access to all RX
	 * queues and to all TX queues
	 */
	for (cpu = 0; cpu < CONFIG_NR_CPUS; cpu++)
		mvreg_write(pp, MVNETA_CPU_MAP(cpu),
			    (MVNETA_CPU_RXQ_ACCESS_ALL_MASK |
			     MVNETA_CPU_TXQ_ACCESS_ALL_MASK));

	/* Reset RX and TX DMAs */
	mvreg_write(pp, MVNETA_PORT_RX_RESET, MVNETA_PORT_RX_DMA_RESET);
	mvreg_write(pp, MVNETA_PORT_TX_RESET, MVNETA_PORT_TX_DMA_RESET);

	/* Disable Legacy WRR, Disable EJP, Release from reset */
	mvreg_write(pp, MVNETA_TXQ_CMD_1, 0);
	for (queue = 0; queue < txq_number; queue++) {
		mvreg_write(pp, MVETH_TXQ_TOKEN_COUNT_REG(queue), 0);
		mvreg_write(pp, MVETH_TXQ_TOKEN_CFG_REG(queue), 0);
	}

	mvreg_write(pp, MVNETA_PORT_TX_RESET, 0);
	mvreg_write(pp, MVNETA_PORT_RX_RESET, 0);

	/* Set Port Acceleration Mode */
	val = MVNETA_ACC_MODE_EXT;
	mvreg_write(pp, MVNETA_ACC_MODE, val);

	/* Update val of portCfg register accordingly with all RxQueue types */
	val = MVNETA_PORT_CONFIG_DEFL_VALUE(rxq_def);
	mvreg_write(pp, MVNETA_PORT_CONFIG, val);

	val = 0;
	mvreg_write(pp, MVNETA_PORT_CONFIG_EXTEND, val);
	mvreg_write(pp, MVNETA_RX_MIN_FRAME_SIZE, 64);

	/* Build PORT_SDMA_CONFIG_REG */
	val = 0;

	/* Default burst size */
	val |= MVNETA_TX_BRST_SZ_MASK(MVNETA_SDMA_BRST_SIZE_16);
	val |= MVNETA_RX_BRST_SZ_MASK(MVNETA_SDMA_BRST_SIZE_16);

	val |= (MVNETA_RX_NO_DATA_SWAP | MVNETA_TX_NO_DATA_SWAP |
		MVNETA_NO_DESC_SWAP);

	/* Assign port SDMA configuration */
	mvreg_write(pp, MVNETA_SDMA_CONFIG, val);

	mvneta_set_ucast_table(pp, -1);
	mvneta_set_special_mcast_table(pp, -1);
	mvneta_set_other_mcast_table(pp, -1);

	/* Set port interrupt enable register - default enable all */
	mvreg_write(pp, MVNETA_INTR_ENABLE,
		    (MVNETA_RXQ_INTR_ENABLE_ALL_MASK
		     | MVNETA_TXQ_INTR_ENABLE_ALL_MASK));
}

/* Set max sizes for tx queues */
static void mvneta_txq_max_tx_size_set(struct mvneta_port *pp, int max_tx_size)

{
	u32 val, size, mtu;
	int queue;

	mtu = max_tx_size * 8;
	if (mtu > MVNETA_TX_MTU_MAX)
		mtu = MVNETA_TX_MTU_MAX;

	/* Set MTU */
	val = mvreg_read(pp, MVNETA_TX_MTU);
	val &= ~MVNETA_TX_MTU_MAX;
	val |= mtu;
	mvreg_write(pp, MVNETA_TX_MTU, val);

	/* TX token size and all TXQs token size must be larger that MTU */
	val = mvreg_read(pp, MVNETA_TX_TOKEN_SIZE);

	size = val & MVNETA_TX_TOKEN_SIZE_MAX;
	if (size < mtu) {
		size = mtu;
		val &= ~MVNETA_TX_TOKEN_SIZE_MAX;
		val |= size;
		mvreg_write(pp, MVNETA_TX_TOKEN_SIZE, val);
	}
	for (queue = 0; queue < txq_number; queue++) {
		val = mvreg_read(pp, MVNETA_TXQ_TOKEN_SIZE_REG(queue));

		size = val & MVNETA_TXQ_TOKEN_SIZE_MAX;
		if (size < mtu) {
			size = mtu;
			val &= ~MVNETA_TXQ_TOKEN_SIZE_MAX;
			val |= size;
			mvreg_write(pp, MVNETA_TXQ_TOKEN_SIZE_REG(queue), val);
		}
	}
}

/* Set unicast address */
static void mvneta_set_ucast_addr(struct mvneta_port *pp, u8 last_nibble,
				  int queue)
{
	unsigned int unicast_reg;
	unsigned int tbl_offset;
	unsigned int reg_offset;

	/* Locate the Unicast table entry */
	last_nibble = (0xf & last_nibble);

	/* offset from unicast tbl base */
	tbl_offset = (last_nibble / 4) * 4;

	/* offset within the above reg  */
	reg_offset = last_nibble % 4;

	unicast_reg = mvreg_read(pp, (MVNETA_DA_FILT_UCAST_BASE + tbl_offset));

	if (queue == -1) {
		/* Clear accepts frame bit at specified unicast DA tbl entry */
		unicast_reg &= ~(0xff << (8 * reg_offset));
	} else {
		unicast_reg &= ~(0xff << (8 * reg_offset));
		unicast_reg |= ((0x01 | (queue << 1)) << (8 * reg_offset));
	}

	mvreg_write(pp, (MVNETA_DA_FILT_UCAST_BASE + tbl_offset), unicast_reg);
}

/* Set mac address */
static void mvneta_mac_addr_set(struct mvneta_port *pp, unsigned char *addr,
				int queue)
{
	unsigned int mac_h;
	unsigned int mac_l;

	if (queue != -1) {
		mac_l = (addr[4] << 8) | (addr[5]);
		mac_h = (addr[0] << 24) | (addr[1] << 16) |
			(addr[2] << 8) | (addr[3] << 0);

		mvreg_write(pp, MVNETA_MAC_ADDR_LOW, mac_l);
		mvreg_write(pp, MVNETA_MAC_ADDR_HIGH, mac_h);
	}

	/* Accept frames of this address */
	mvneta_set_ucast_addr(pp, addr[5], queue);
}

/* Set the number of packets that will be received before RX interrupt
 * will be generated by HW.
 */
static void mvneta_rx_pkts_coal_set(struct mvneta_port *pp,
				    struct mvneta_rx_queue *rxq, u32 value)
{
	mvreg_write(pp, MVNETA_RXQ_THRESHOLD_REG(rxq->id),
		    value | MVNETA_RXQ_NON_OCCUPIED(0));
	rxq->pkts_coal = value;
}

/* Set the time delay in usec before RX interrupt will be generated by
 * HW.
 */
static void mvneta_rx_time_coal_set(struct mvneta_port *pp,
				    struct mvneta_rx_queue *rxq, u32 value)
{
	u32 val = (pp->clk_rate_hz / 1000000) * value;

	mvreg_write(pp, MVNETA_RXQ_TIME_COAL_REG(rxq->id), val);
	rxq->time_coal = value;
}

/* Set threshold for TX_DONE pkts coalescing */
static void mvneta_tx_done_pkts_coal_set(struct mvneta_port *pp,
					 struct mvneta_tx_queue *txq, u32 value)
{
	u32 val;

	val = mvreg_read(pp, MVNETA_TXQ_SIZE_REG(txq->id));

	val &= ~MVNETA_TXQ_SENT_THRESH_ALL_MASK;
	val |= MVNETA_TXQ_SENT_THRESH_MASK(value);

	mvreg_write(pp, MVNETA_TXQ_SIZE_REG(txq->id), val);

	txq->done_pkts_coal = value;
}

/* Trigger tx done timer in MVNETA_TX_DONE_TIMER_PERIOD msecs */
static void mvneta_add_tx_done_timer(struct mvneta_port *pp)
{
	if (test_and_set_bit(MVNETA_F_TX_DONE_TIMER_BIT, &pp->flags) == 0) {
		pp->tx_done_timer.expires = jiffies +
			msecs_to_jiffies(MVNETA_TX_DONE_TIMER_PERIOD);
		add_timer(&pp->tx_done_timer);
	}
}


/* Handle rx descriptor fill by setting buf_cookie and buf_phys_addr */
static void mvneta_rx_desc_fill(struct mvneta_rx_desc *rx_desc,
				u32 phys_addr, u32 cookie)
{
	rx_desc->buf_cookie = cookie;
	rx_desc->buf_phys_addr = phys_addr;
}

/* Decrement sent descriptors counter */
static void mvneta_txq_sent_desc_dec(struct mvneta_port *pp,
				     struct mvneta_tx_queue *txq,
				     int sent_desc)
{
	u32 val;

	/* Only 255 TX descriptors can be updated at once */
	while (sent_desc > 0xff) {
		val = 0xff << MVNETA_TXQ_DEC_SENT_SHIFT;
		mvreg_write(pp, MVNETA_TXQ_UPDATE_REG(txq->id), val);
		sent_desc = sent_desc - 0xff;
	}

	val = sent_desc << MVNETA_TXQ_DEC_SENT_SHIFT;
	mvreg_write(pp, MVNETA_TXQ_UPDATE_REG(txq->id), val);
}

/* Get number of TX descriptors already sent by HW */
static int mvneta_txq_sent_desc_num_get(struct mvneta_port *pp,
					struct mvneta_tx_queue *txq)
{
	u32 val;
	int sent_desc;

	val = mvreg_read(pp, MVNETA_TXQ_STATUS_REG(txq->id));
	sent_desc = (val & MVNETA_TXQ_SENT_DESC_MASK) >>
		MVNETA_TXQ_SENT_DESC_SHIFT;

	return sent_desc;
}

/* Get number of sent descriptors and decrement counter.
 *  The number of sent descriptors is returned.
 */
static int mvneta_txq_sent_desc_proc(struct mvneta_port *pp,
				     struct mvneta_tx_queue *txq)
{
	int sent_desc;

	/* Get number of sent descriptors */
	sent_desc = mvneta_txq_sent_desc_num_get(pp, txq);

	/* Decrement sent descriptors counter */
	if (sent_desc)
		mvneta_txq_sent_desc_dec(pp, txq, sent_desc);

	return sent_desc;
}

/* Set TXQ descriptors fields relevant for CSUM calculation */
static u32 mvneta_txq_desc_csum(int l3_offs, int l3_proto,
				int ip_hdr_len, int l4_proto)
{
	u32 command;

	/* Fields: L3_offset, IP_hdrlen, L3_type, G_IPv4_chk,
	 * G_L4_chk, L4_type; required only for checksum
	 * calculation
	 */
	command =  l3_offs    << MVNETA_TX_L3_OFF_SHIFT;
	command |= ip_hdr_len << MVNETA_TX_IP_HLEN_SHIFT;

	if (l3_proto == swab16(ETH_P_IP))
		command |= MVNETA_TXD_IP_CSUM;
	else
		command |= MVNETA_TX_L3_IP6;

	if (l4_proto == IPPROTO_TCP)
		command |=  MVNETA_TX_L4_CSUM_FULL;
	else if (l4_proto == IPPROTO_UDP)
		command |= MVNETA_TX_L4_UDP | MVNETA_TX_L4_CSUM_FULL;
	else
		command |= MVNETA_TX_L4_CSUM_NOT;

	return command;
}


/* Display more error info */
static void mvneta_rx_error(struct mvneta_port *pp,
			    struct mvneta_rx_desc *rx_desc)
{
	u32 status = rx_desc->status;

	if (!mvneta_rxq_desc_is_first_last(rx_desc)) {
		netdev_err(pp->dev,
			   "bad rx status %08x (buffer oversize), size=%d\n",
			   rx_desc->status, rx_desc->data_size);
		return;
	}

	switch (status & MVNETA_RXD_ERR_CODE_MASK) {
	case MVNETA_RXD_ERR_CRC:
		netdev_err(pp->dev, "bad rx status %08x (crc error), size=%d\n",
			   status, rx_desc->data_size);
		break;
	case MVNETA_RXD_ERR_OVERRUN:
		netdev_err(pp->dev, "bad rx status %08x (overrun error), size=%d\n",
			   status, rx_desc->data_size);
		break;
	case MVNETA_RXD_ERR_LEN:
		netdev_err(pp->dev, "bad rx status %08x (max frame length error), size=%d\n",
			   status, rx_desc->data_size);
		break;
	case MVNETA_RXD_ERR_RESOURCE:
		netdev_err(pp->dev, "bad rx status %08x (resource error), size=%d\n",
			   status, rx_desc->data_size);
		break;
	}
}

/* Handle RX checksum offload */
static void mvneta_rx_csum(struct mvneta_port *pp,
			   struct mvneta_rx_desc *rx_desc,
			   struct sk_buff *skb)
{
	if ((rx_desc->status & MVNETA_RXD_L3_IP4) &&
	    (rx_desc->status & MVNETA_RXD_L4_CSUM_OK)) {
		skb->csum = 0;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
		return;
	}

	skb->ip_summed = CHECKSUM_NONE;
}

/* Return tx queue pointer (find last set bit) according to causeTxDone reg */
static struct mvneta_tx_queue *mvneta_tx_done_policy(struct mvneta_port *pp,
						     u32 cause)
{
	int queue = fls(cause) - 1;

	return (queue < 0 || queue >= txq_number) ? NULL : &pp->txqs[queue];
}

/* Free tx queue skbuffs */
static void mvneta_txq_bufs_free(struct mvneta_port *pp,
				 struct mvneta_tx_queue *txq, int num)
{
	int i;

	for (i = 0; i < num; i++) {
		struct mvneta_tx_desc *tx_desc = txq->descs +
			txq->txq_get_index;
		struct sk_buff *skb = txq->tx_skb[txq->txq_get_index];

		mvneta_txq_inc_get(txq);

		if (!skb)
			continue;

		dma_unmap_single(pp->dev->dev.parent, tx_desc->buf_phys_addr,
				 tx_desc->data_size, DMA_TO_DEVICE);
		dev_kfree_skb_any(skb);
	}
}

/* Handle end of transmission */
static int mvneta_txq_done(struct mvneta_port *pp,
			   struct mvneta_tx_queue *txq)
{
	struct netdev_queue *nq = netdev_get_tx_queue(pp->dev, txq->id);
	int tx_done;

	tx_done = mvneta_txq_sent_desc_proc(pp, txq);
	if (tx_done == 0)
		return tx_done;
	mvneta_txq_bufs_free(pp, txq, tx_done);

	txq->count -= tx_done;

	if (netif_tx_queue_stopped(nq)) {
		if (txq->size - txq->count >= MAX_SKB_FRAGS + 1)
			netif_tx_wake_queue(nq);
	}

	return tx_done;
}

/* Refill processing */
static int mvneta_rx_refill(struct mvneta_port *pp,
			    struct mvneta_rx_desc *rx_desc)

{
	dma_addr_t phys_addr;
	struct sk_buff *skb;

	skb = netdev_alloc_skb(pp->dev, pp->pkt_size);
	if (!skb)
		return -ENOMEM;

	phys_addr = dma_map_single(pp->dev->dev.parent, skb->head,
				   MVNETA_RX_BUF_SIZE(pp->pkt_size),
				   DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(pp->dev->dev.parent, phys_addr))) {
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	mvneta_rx_desc_fill(rx_desc, phys_addr, (u32)skb);

	return 0;
}

/* Handle tx checksum */
static u32 mvneta_skb_tx_csum(struct mvneta_port *pp, struct sk_buff *skb)
{
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		int ip_hdr_len = 0;
		u8 l4_proto;

		if (skb->protocol == htons(ETH_P_IP)) {
			struct iphdr *ip4h = ip_hdr(skb);

			/* Calculate IPv4 checksum and L4 checksum */
			ip_hdr_len = ip4h->ihl;
			l4_proto = ip4h->protocol;
		} else if (skb->protocol == htons(ETH_P_IPV6)) {
			struct ipv6hdr *ip6h = ipv6_hdr(skb);

			/* Read l4_protocol from one of IPv6 extra headers */
			if (skb_network_header_len(skb) > 0)
				ip_hdr_len = (skb_network_header_len(skb) >> 2);
			l4_proto = ip6h->nexthdr;
		} else
			return MVNETA_TX_L4_CSUM_NOT;

		return mvneta_txq_desc_csum(skb_network_offset(skb),
				skb->protocol, ip_hdr_len, l4_proto);
	}

	return MVNETA_TX_L4_CSUM_NOT;
}

/* Returns rx queue pointer (find last set bit) according to causeRxTx
 * value
 */
static struct mvneta_rx_queue *mvneta_rx_policy(struct mvneta_port *pp,
						u32 cause)
{
	int queue = fls(cause >> 8) - 1;

	return (queue < 0 || queue >= rxq_number) ? NULL : &pp->rxqs[queue];
}

/* Drop packets received by the RXQ and free buffers */
static void mvneta_rxq_drop_pkts(struct mvneta_port *pp,
				 struct mvneta_rx_queue *rxq)
{
	int rx_done, i;

	rx_done = mvneta_rxq_busy_desc_num_get(pp, rxq);
	for (i = 0; i < rxq->size; i++) {
		struct mvneta_rx_desc *rx_desc = rxq->descs + i;
		struct sk_buff *skb = (struct sk_buff *)rx_desc->buf_cookie;

		dev_kfree_skb_any(skb);
		dma_unmap_single(pp->dev->dev.parent, rx_desc->buf_phys_addr,
				 rx_desc->data_size, DMA_FROM_DEVICE);
	}

	if (rx_done)
		mvneta_rxq_desc_num_update(pp, rxq, rx_done, rx_done);
}

/* Main rx processing */
static int mvneta_rx(struct mvneta_port *pp, int rx_todo,
		     struct mvneta_rx_queue *rxq)
{
	struct net_device *dev = pp->dev;
	int rx_done, rx_filled;

	/* Get number of received packets */
	rx_done = mvneta_rxq_busy_desc_num_get(pp, rxq);

	if (rx_todo > rx_done)
		rx_todo = rx_done;

	rx_done = 0;
	rx_filled = 0;

	/* Fairness NAPI loop */
	while (rx_done < rx_todo) {
		struct mvneta_rx_desc *rx_desc = mvneta_rxq_next_desc_get(rxq);
		struct sk_buff *skb;
		u32 rx_status;
		int rx_bytes, err;

		prefetch(rx_desc);
		rx_done++;
		rx_filled++;
		rx_status = rx_desc->status;
		skb = (struct sk_buff *)rx_desc->buf_cookie;

		if (!mvneta_rxq_desc_is_first_last(rx_desc) ||
		    (rx_status & MVNETA_RXD_ERR_SUMMARY)) {
			dev->stats.rx_errors++;
			mvneta_rx_error(pp, rx_desc);
			mvneta_rx_desc_fill(rx_desc, rx_desc->buf_phys_addr,
					    (u32)skb);
			continue;
		}

		dma_unmap_single(pp->dev->dev.parent, rx_desc->buf_phys_addr,
				 rx_desc->data_size, DMA_FROM_DEVICE);

		rx_bytes = rx_desc->data_size -
			(ETH_FCS_LEN + MVNETA_MH_SIZE);
		u64_stats_update_begin(&pp->rx_stats.syncp);
		pp->rx_stats.packets++;
		pp->rx_stats.bytes += rx_bytes;
		u64_stats_update_end(&pp->rx_stats.syncp);

		/* Linux processing */
		skb_reserve(skb, MVNETA_MH_SIZE);
		skb_put(skb, rx_bytes);

		skb->protocol = eth_type_trans(skb, dev);

		mvneta_rx_csum(pp, rx_desc, skb);

		napi_gro_receive(&pp->napi, skb);

		/* Refill processing */
		err = mvneta_rx_refill(pp, rx_desc);
		if (err) {
			netdev_err(pp->dev, "Linux processing - Can't refill\n");
			rxq->missed++;
			rx_filled--;
		}
	}

	/* Update rxq management counters */
	mvneta_rxq_desc_num_update(pp, rxq, rx_done, rx_filled);

	return rx_done;
}

/* Handle tx fragmentation processing */
static int mvneta_tx_frag_process(struct mvneta_port *pp, struct sk_buff *skb,
				  struct mvneta_tx_queue *txq)
{
	struct mvneta_tx_desc *tx_desc;
	int i;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		void *addr = page_address(frag->page.p) + frag->page_offset;

		tx_desc = mvneta_txq_next_desc_get(txq);
		tx_desc->data_size = frag->size;

		tx_desc->buf_phys_addr =
			dma_map_single(pp->dev->dev.parent, addr,
				       tx_desc->data_size, DMA_TO_DEVICE);

		if (dma_mapping_error(pp->dev->dev.parent,
				      tx_desc->buf_phys_addr)) {
			mvneta_txq_desc_put(txq);
			goto error;
		}

		if (i == (skb_shinfo(skb)->nr_frags - 1)) {
			/* Last descriptor */
			tx_desc->command = MVNETA_TXD_L_DESC | MVNETA_TXD_Z_PAD;

			txq->tx_skb[txq->txq_put_index] = skb;

			mvneta_txq_inc_put(txq);
		} else {
			/* Descriptor in the middle: Not First, Not Last */
			tx_desc->command = 0;

			txq->tx_skb[txq->txq_put_index] = NULL;
			mvneta_txq_inc_put(txq);
		}
	}

	return 0;

error:
	/* Release all descriptors that were used to map fragments of
	 * this packet, as well as the corresponding DMA mappings
	 */
	for (i = i - 1; i >= 0; i--) {
		tx_desc = txq->descs + i;
		dma_unmap_single(pp->dev->dev.parent,
				 tx_desc->buf_phys_addr,
				 tx_desc->data_size,
				 DMA_TO_DEVICE);
		mvneta_txq_desc_put(txq);
	}

	return -ENOMEM;
}

/* Main tx processing */
static int mvneta_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct mvneta_port *pp = netdev_priv(dev);
	struct mvneta_tx_queue *txq = &pp->txqs[txq_def];
	struct mvneta_tx_desc *tx_desc;
	struct netdev_queue *nq;
	int frags = 0;
	u32 tx_cmd;

	if (!netif_running(dev))
		goto out;

	frags = skb_shinfo(skb)->nr_frags + 1;
	nq    = netdev_get_tx_queue(dev, txq_def);

	/* Get a descriptor for the first part of the packet */
	tx_desc = mvneta_txq_next_desc_get(txq);

	tx_cmd = mvneta_skb_tx_csum(pp, skb);

	tx_desc->data_size = skb_headlen(skb);

	tx_desc->buf_phys_addr = dma_map_single(dev->dev.parent, skb->data,
						tx_desc->data_size,
						DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev->dev.parent,
				       tx_desc->buf_phys_addr))) {
		mvneta_txq_desc_put(txq);
		frags = 0;
		goto out;
	}

	if (frags == 1) {
		/* First and Last descriptor */
		tx_cmd |= MVNETA_TXD_FLZ_DESC;
		tx_desc->command = tx_cmd;
		txq->tx_skb[txq->txq_put_index] = skb;
		mvneta_txq_inc_put(txq);
	} else {
		/* First but not Last */
		tx_cmd |= MVNETA_TXD_F_DESC;
		txq->tx_skb[txq->txq_put_index] = NULL;
		mvneta_txq_inc_put(txq);
		tx_desc->command = tx_cmd;
		/* Continue with other skb fragments */
		if (mvneta_tx_frag_process(pp, skb, txq)) {
			dma_unmap_single(dev->dev.parent,
					 tx_desc->buf_phys_addr,
					 tx_desc->data_size,
					 DMA_TO_DEVICE);
			mvneta_txq_desc_put(txq);
			frags = 0;
			goto out;
		}
	}

	txq->count += frags;
	mvneta_txq_pend_desc_add(pp, txq, frags);

	if (txq->size - txq->count < MAX_SKB_FRAGS + 1)
		netif_tx_stop_queue(nq);

out:
	if (frags > 0) {
		u64_stats_update_begin(&pp->tx_stats.syncp);
		pp->tx_stats.packets++;
		pp->tx_stats.bytes += skb->len;
		u64_stats_update_end(&pp->tx_stats.syncp);

	} else {
		dev->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
	}

	if (txq->count >= MVNETA_TXDONE_COAL_PKTS)
		mvneta_txq_done(pp, txq);

	/* If after calling mvneta_txq_done, count equals
	 * frags, we need to set the timer
	 */
	if (txq->count == frags && frags > 0)
		mvneta_add_tx_done_timer(pp);

	return NETDEV_TX_OK;
}


/* Free tx resources, when resetting a port */
static void mvneta_txq_done_force(struct mvneta_port *pp,
				  struct mvneta_tx_queue *txq)

{
	int tx_done = txq->count;

	mvneta_txq_bufs_free(pp, txq, tx_done);

	/* reset txq */
	txq->count = 0;
	txq->txq_put_index = 0;
	txq->txq_get_index = 0;
}

/* handle tx done - called from tx done timer callback */
static u32 mvneta_tx_done_gbe(struct mvneta_port *pp, u32 cause_tx_done,
			      int *tx_todo)
{
	struct mvneta_tx_queue *txq;
	u32 tx_done = 0;
	struct netdev_queue *nq;

	*tx_todo = 0;
	while (cause_tx_done != 0) {
		txq = mvneta_tx_done_policy(pp, cause_tx_done);
		if (!txq)
			break;

		nq = netdev_get_tx_queue(pp->dev, txq->id);
		__netif_tx_lock(nq, smp_processor_id());

		if (txq->count) {
			tx_done += mvneta_txq_done(pp, txq);
			*tx_todo += txq->count;
		}

		__netif_tx_unlock(nq);
		cause_tx_done &= ~((1 << txq->id));
	}

	return tx_done;
}

/* Compute crc8 of the specified address, using a unique algorithm ,
 * according to hw spec, different than generic crc8 algorithm
 */
static int mvneta_addr_crc(unsigned char *addr)
{
	int crc = 0;
	int i;

	for (i = 0; i < ETH_ALEN; i++) {
		int j;

		crc = (crc ^ addr[i]) << 8;
		for (j = 7; j >= 0; j--) {
			if (crc & (0x100 << j))
				crc ^= 0x107 << j;
		}
	}

	return crc;
}

/* This method controls the net device special MAC multicast support.
 * The Special Multicast Table for MAC addresses supports MAC of the form
 * 0x01-00-5E-00-00-XX (where XX is between 0x00 and 0xFF).
 * The MAC DA[7:0] bits are used as a pointer to the Special Multicast
 * Table entries in the DA-Filter table. This method set the Special
 * Multicast Table appropriate entry.
 */
static void mvneta_set_special_mcast_addr(struct mvneta_port *pp,
					  unsigned char last_byte,
					  int queue)
{
	unsigned int smc_table_reg;
	unsigned int tbl_offset;
	unsigned int reg_offset;

	/* Register offset from SMC table base    */
	tbl_offset = (last_byte / 4);
	/* Entry offset within the above reg */
	reg_offset = last_byte % 4;

	smc_table_reg = mvreg_read(pp, (MVNETA_DA_FILT_SPEC_MCAST
					+ tbl_offset * 4));

	if (queue == -1)
		smc_table_reg &= ~(0xff << (8 * reg_offset));
	else {
		smc_table_reg &= ~(0xff << (8 * reg_offset));
		smc_table_reg |= ((0x01 | (queue << 1)) << (8 * reg_offset));
	}

	mvreg_write(pp, MVNETA_DA_FILT_SPEC_MCAST + tbl_offset * 4,
		    smc_table_reg);
}

/* This method controls the network device Other MAC multicast support.
 * The Other Multicast Table is used for multicast of another type.
 * A CRC-8 is used as an index to the Other Multicast Table entries
 * in the DA-Filter table.
 * The method gets the CRC-8 value from the calling routine and
 * sets the Other Multicast Table appropriate entry according to the
 * specified CRC-8 .
 */
static void mvneta_set_other_mcast_addr(struct mvneta_port *pp,
					unsigned char crc8,
					int queue)
{
	unsigned int omc_table_reg;
	unsigned int tbl_offset;
	unsigned int reg_offset;

	tbl_offset = (crc8 / 4) * 4; /* Register offset from OMC table base */
	reg_offset = crc8 % 4;	     /* Entry offset within the above reg   */

	omc_table_reg = mvreg_read(pp, MVNETA_DA_FILT_OTH_MCAST + tbl_offset);

	if (queue == -1) {
		/* Clear accepts frame bit at specified Other DA table entry */
		omc_table_reg &= ~(0xff << (8 * reg_offset));
	} else {
		omc_table_reg &= ~(0xff << (8 * reg_offset));
		omc_table_reg |= ((0x01 | (queue << 1)) << (8 * reg_offset));
	}

	mvreg_write(pp, MVNETA_DA_FILT_OTH_MCAST + tbl_offset, omc_table_reg);
}

/* The network device supports multicast using two tables:
 *    1) Special Multicast Table for MAC addresses of the form
 *       0x01-00-5E-00-00-XX (where XX is between 0x00 and 0xFF).
 *       The MAC DA[7:0] bits are used as a pointer to the Special Multicast
 *       Table entries in the DA-Filter table.
 *    2) Other Multicast Table for multicast of another type. A CRC-8 value
 *       is used as an index to the Other Multicast Table entries in the
 *       DA-Filter table.
 */
static int mvneta_mcast_addr_set(struct mvneta_port *pp, unsigned char *p_addr,
				 int queue)
{
	unsigned char crc_result = 0;

	if (memcmp(p_addr, "\x01\x00\x5e\x00\x00", 5) == 0) {
		mvneta_set_special_mcast_addr(pp, p_addr[5], queue);
		return 0;
	}

	crc_result = mvneta_addr_crc(p_addr);
	if (queue == -1) {
		if (pp->mcast_count[crc_result] == 0) {
			netdev_info(pp->dev, "No valid Mcast for crc8=0x%02x\n",
				    crc_result);
			return -EINVAL;
		}

		pp->mcast_count[crc_result]--;
		if (pp->mcast_count[crc_result] != 0) {
			netdev_info(pp->dev,
				    "After delete there are %d valid Mcast for crc8=0x%02x\n",
				    pp->mcast_count[crc_result], crc_result);
			return -EINVAL;
		}
	} else
		pp->mcast_count[crc_result]++;

	mvneta_set_other_mcast_addr(pp, crc_result, queue);

	return 0;
}

/* Configure Fitering mode of Ethernet port */
static void mvneta_rx_unicast_promisc_set(struct mvneta_port *pp,
					  int is_promisc)
{
	u32 port_cfg_reg, val;

	port_cfg_reg = mvreg_read(pp, MVNETA_PORT_CONFIG);

	val = mvreg_read(pp, MVNETA_TYPE_PRIO);

	/* Set / Clear UPM bit in port configuration register */
	if (is_promisc) {
		/* Accept all Unicast addresses */
		port_cfg_reg |= MVNETA_UNI_PROMISC_MODE;
		val |= MVNETA_FORCE_UNI;
		mvreg_write(pp, MVNETA_MAC_ADDR_LOW, 0xffff);
		mvreg_write(pp, MVNETA_MAC_ADDR_HIGH, 0xffffffff);
	} else {
		/* Reject all Unicast addresses */
		port_cfg_reg &= ~MVNETA_UNI_PROMISC_MODE;
		val &= ~MVNETA_FORCE_UNI;
	}

	mvreg_write(pp, MVNETA_PORT_CONFIG, port_cfg_reg);
	mvreg_write(pp, MVNETA_TYPE_PRIO, val);
}

/* register unicast and multicast addresses */
static void mvneta_set_rx_mode(struct net_device *dev)
{
	struct mvneta_port *pp = netdev_priv(dev);
	struct netdev_hw_addr *ha;

	if (dev->flags & IFF_PROMISC) {
		/* Accept all: Multicast + Unicast */
		mvneta_rx_unicast_promisc_set(pp, 1);
		mvneta_set_ucast_table(pp, rxq_def);
		mvneta_set_special_mcast_table(pp, rxq_def);
		mvneta_set_other_mcast_table(pp, rxq_def);
	} else {
		/* Accept single Unicast */
		mvneta_rx_unicast_promisc_set(pp, 0);
		mvneta_set_ucast_table(pp, -1);
		mvneta_mac_addr_set(pp, dev->dev_addr, rxq_def);

		if (dev->flags & IFF_ALLMULTI) {
			/* Accept all multicast */
			mvneta_set_special_mcast_table(pp, rxq_def);
			mvneta_set_other_mcast_table(pp, rxq_def);
		} else {
			/* Accept only initialized multicast */
			mvneta_set_special_mcast_table(pp, -1);
			mvneta_set_other_mcast_table(pp, -1);

			if (!netdev_mc_empty(dev)) {
				netdev_for_each_mc_addr(ha, dev) {
					mvneta_mcast_addr_set(pp, ha->addr,
							      rxq_def);
				}
			}
		}
	}
}

/* Interrupt handling - the callback for request_irq() */
static irqreturn_t mvneta_isr(int irq, void *dev_id)
{
	struct mvneta_port *pp = (struct mvneta_port *)dev_id;

	/* Mask all interrupts */
	mvreg_write(pp, MVNETA_INTR_NEW_MASK, 0);

	napi_schedule(&pp->napi);

	return IRQ_HANDLED;
}

/* NAPI handler
 * Bits 0 - 7 of the causeRxTx register indicate that are transmitted
 * packets on the corresponding TXQ (Bit 0 is for TX queue 1).
 * Bits 8 -15 of the cause Rx Tx register indicate that are received
 * packets on the corresponding RXQ (Bit 8 is for RX queue 0).
 * Each CPU has its own causeRxTx register
 */
static int mvneta_poll(struct napi_struct *napi, int budget)
{
	int rx_done = 0;
	u32 cause_rx_tx;
	unsigned long flags;
	struct mvneta_port *pp = netdev_priv(napi->dev);

	if (!netif_running(pp->dev)) {
		napi_complete(napi);
		return rx_done;
	}

	/* Read cause register */
	cause_rx_tx = mvreg_read(pp, MVNETA_INTR_NEW_CAUSE) &
		MVNETA_RX_INTR_MASK(rxq_number);

	/* For the case where the last mvneta_poll did not process all
	 * RX packets
	 */
	cause_rx_tx |= pp->cause_rx_tx;
	if (rxq_number > 1) {
		while ((cause_rx_tx != 0) && (budget > 0)) {
			int count;
			struct mvneta_rx_queue *rxq;
			/* get rx queue number from cause_rx_tx */
			rxq = mvneta_rx_policy(pp, cause_rx_tx);
			if (!rxq)
				break;

			/* process the packet in that rx queue */
			count = mvneta_rx(pp, budget, rxq);
			rx_done += count;
			budget -= count;
			if (budget > 0) {
				/* set off the rx bit of the
				 * corresponding bit in the cause rx
				 * tx register, so that next iteration
				 * will find the next rx queue where
				 * packets are received on
				 */
				cause_rx_tx &= ~((1 << rxq->id) << 8);
			}
		}
	} else {
		rx_done = mvneta_rx(pp, budget, &pp->rxqs[rxq_def]);
		budget -= rx_done;
	}

	if (budget > 0) {
		cause_rx_tx = 0;
		napi_complete(napi);
		local_irq_save(flags);
		mvreg_write(pp, MVNETA_INTR_NEW_MASK,
			    MVNETA_RX_INTR_MASK(rxq_number));
		local_irq_restore(flags);
	}

	pp->cause_rx_tx = cause_rx_tx;
	return rx_done;
}

/* tx done timer callback */
static void mvneta_tx_done_timer_callback(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct mvneta_port *pp = netdev_priv(dev);
	int tx_done = 0, tx_todo = 0;

	if (!netif_running(dev))
		return ;

	clear_bit(MVNETA_F_TX_DONE_TIMER_BIT, &pp->flags);

	tx_done = mvneta_tx_done_gbe(pp,
				     (((1 << txq_number) - 1) &
				      MVNETA_CAUSE_TXQ_SENT_DESC_ALL_MASK),
				     &tx_todo);
	if (tx_todo > 0)
		mvneta_add_tx_done_timer(pp);
}

/* Handle rxq fill: allocates rxq skbs; called when initializing a port */
static int mvneta_rxq_fill(struct mvneta_port *pp, struct mvneta_rx_queue *rxq,
			   int num)
{
	struct net_device *dev = pp->dev;
	int i;

	for (i = 0; i < num; i++) {
		struct sk_buff *skb;
		struct mvneta_rx_desc *rx_desc;
		unsigned long phys_addr;

		skb = dev_alloc_skb(pp->pkt_size);
		if (!skb) {
			netdev_err(dev, "%s:rxq %d, %d of %d buffs  filled\n",
				__func__, rxq->id, i, num);
			break;
		}

		rx_desc = rxq->descs + i;
		memset(rx_desc, 0, sizeof(struct mvneta_rx_desc));
		phys_addr = dma_map_single(dev->dev.parent, skb->head,
					   MVNETA_RX_BUF_SIZE(pp->pkt_size),
					   DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(dev->dev.parent, phys_addr))) {
			dev_kfree_skb(skb);
			break;
		}

		mvneta_rx_desc_fill(rx_desc, phys_addr, (u32)skb);
	}

	/* Add this number of RX descriptors as non occupied (ready to
	 * get packets)
	 */
	mvneta_rxq_non_occup_desc_add(pp, rxq, i);

	return i;
}

/* Free all packets pending transmit from all TXQs and reset TX port */
static void mvneta_tx_reset(struct mvneta_port *pp)
{
	int queue;

	/* free the skb's in the hal tx ring */
	for (queue = 0; queue < txq_number; queue++)
		mvneta_txq_done_force(pp, &pp->txqs[queue]);

	mvreg_write(pp, MVNETA_PORT_TX_RESET, MVNETA_PORT_TX_DMA_RESET);
	mvreg_write(pp, MVNETA_PORT_TX_RESET, 0);
}

static void mvneta_rx_reset(struct mvneta_port *pp)
{
	mvreg_write(pp, MVNETA_PORT_RX_RESET, MVNETA_PORT_RX_DMA_RESET);
	mvreg_write(pp, MVNETA_PORT_RX_RESET, 0);
}

/* Rx/Tx queue initialization/cleanup methods */

/* Create a specified RX queue */
static int mvneta_rxq_init(struct mvneta_port *pp,
			   struct mvneta_rx_queue *rxq)

{
	rxq->size = pp->rx_ring_size;

	/* Allocate memory for RX descriptors */
	rxq->descs = dma_alloc_coherent(pp->dev->dev.parent,
					rxq->size * MVNETA_DESC_ALIGNED_SIZE,
					&rxq->descs_phys, GFP_KERNEL);
	if (rxq->descs == NULL) {
		netdev_err(pp->dev,
			   "rxq=%d: Can't allocate %d bytes for %d RX descr\n",
			   rxq->id, rxq->size * MVNETA_DESC_ALIGNED_SIZE,
			   rxq->size);
		return -ENOMEM;
	}

	BUG_ON(rxq->descs !=
	       PTR_ALIGN(rxq->descs, MVNETA_CPU_D_CACHE_LINE_SIZE));

	rxq->last_desc = rxq->size - 1;

	/* Set Rx descriptors queue starting address */
	mvreg_write(pp, MVNETA_RXQ_BASE_ADDR_REG(rxq->id), rxq->descs_phys);
	mvreg_write(pp, MVNETA_RXQ_SIZE_REG(rxq->id), rxq->size);

	/* Set Offset */
	mvneta_rxq_offset_set(pp, rxq, NET_SKB_PAD);

	/* Set coalescing pkts and time */
	mvneta_rx_pkts_coal_set(pp, rxq, rxq->pkts_coal);
	mvneta_rx_time_coal_set(pp, rxq, rxq->time_coal);

	/* Fill RXQ with buffers from RX pool */
	mvneta_rxq_buf_size_set(pp, rxq, MVNETA_RX_BUF_SIZE(pp->pkt_size));
	mvneta_rxq_bm_disable(pp, rxq);
	mvneta_rxq_fill(pp, rxq, rxq->size);

	return 0;
}

/* Cleanup Rx queue */
static void mvneta_rxq_deinit(struct mvneta_port *pp,
			      struct mvneta_rx_queue *rxq)
{
	mvneta_rxq_drop_pkts(pp, rxq);

	if (rxq->descs)
		dma_free_coherent(pp->dev->dev.parent,
				  rxq->size * MVNETA_DESC_ALIGNED_SIZE,
				  rxq->descs,
				  rxq->descs_phys);

	rxq->descs             = NULL;
	rxq->last_desc         = 0;
	rxq->next_desc_to_proc = 0;
	rxq->descs_phys        = 0;
}

/* Create and initialize a tx queue */
static int mvneta_txq_init(struct mvneta_port *pp,
			   struct mvneta_tx_queue *txq)
{
	txq->size = pp->tx_ring_size;

	/* Allocate memory for TX descriptors */
	txq->descs = dma_alloc_coherent(pp->dev->dev.parent,
					txq->size * MVNETA_DESC_ALIGNED_SIZE,
					&txq->descs_phys, GFP_KERNEL);
	if (txq->descs == NULL) {
		netdev_err(pp->dev,
			   "txQ=%d: Can't allocate %d bytes for %d TX descr\n",
			   txq->id, txq->size * MVNETA_DESC_ALIGNED_SIZE,
			   txq->size);
		return -ENOMEM;
	}

	/* Make sure descriptor address is cache line size aligned  */
	BUG_ON(txq->descs !=
	       PTR_ALIGN(txq->descs, MVNETA_CPU_D_CACHE_LINE_SIZE));

	txq->last_desc = txq->size - 1;

	/* Set maximum bandwidth for enabled TXQs */
	mvreg_write(pp, MVETH_TXQ_TOKEN_CFG_REG(txq->id), 0x03ffffff);
	mvreg_write(pp, MVETH_TXQ_TOKEN_COUNT_REG(txq->id), 0x3fffffff);

	/* Set Tx descriptors queue starting address */
	mvreg_write(pp, MVNETA_TXQ_BASE_ADDR_REG(txq->id), txq->descs_phys);
	mvreg_write(pp, MVNETA_TXQ_SIZE_REG(txq->id), txq->size);

	txq->tx_skb = kmalloc(txq->size * sizeof(*txq->tx_skb), GFP_KERNEL);
	if (txq->tx_skb == NULL) {
		dma_free_coherent(pp->dev->dev.parent,
				  txq->size * MVNETA_DESC_ALIGNED_SIZE,
				  txq->descs, txq->descs_phys);
		return -ENOMEM;
	}
	mvneta_tx_done_pkts_coal_set(pp, txq, txq->done_pkts_coal);

	return 0;
}

/* Free allocated resources when mvneta_txq_init() fails to allocate memory*/
static void mvneta_txq_deinit(struct mvneta_port *pp,
			      struct mvneta_tx_queue *txq)
{
	kfree(txq->tx_skb);

	if (txq->descs)
		dma_free_coherent(pp->dev->dev.parent,
				  txq->size * MVNETA_DESC_ALIGNED_SIZE,
				  txq->descs, txq->descs_phys);

	txq->descs             = NULL;
	txq->last_desc         = 0;
	txq->next_desc_to_proc = 0;
	txq->descs_phys        = 0;

	/* Set minimum bandwidth for disabled TXQs */
	mvreg_write(pp, MVETH_TXQ_TOKEN_CFG_REG(txq->id), 0);
	mvreg_write(pp, MVETH_TXQ_TOKEN_COUNT_REG(txq->id), 0);

	/* Set Tx descriptors queue starting address and size */
	mvreg_write(pp, MVNETA_TXQ_BASE_ADDR_REG(txq->id), 0);
	mvreg_write(pp, MVNETA_TXQ_SIZE_REG(txq->id), 0);
}

/* Cleanup all Tx queues */
static void mvneta_cleanup_txqs(struct mvneta_port *pp)
{
	int queue;

	for (queue = 0; queue < txq_number; queue++)
		mvneta_txq_deinit(pp, &pp->txqs[queue]);
}

/* Cleanup all Rx queues */
static void mvneta_cleanup_rxqs(struct mvneta_port *pp)
{
	int queue;

	for (queue = 0; queue < rxq_number; queue++)
		mvneta_rxq_deinit(pp, &pp->rxqs[queue]);
}


/* Init all Rx queues */
static int mvneta_setup_rxqs(struct mvneta_port *pp)
{
	int queue;

	for (queue = 0; queue < rxq_number; queue++) {
		int err = mvneta_rxq_init(pp, &pp->rxqs[queue]);
		if (err) {
			netdev_err(pp->dev, "%s: can't create rxq=%d\n",
				   __func__, queue);
			mvneta_cleanup_rxqs(pp);
			return err;
		}
	}

	return 0;
}

/* Init all tx queues */
static int mvneta_setup_txqs(struct mvneta_port *pp)
{
	int queue;

	for (queue = 0; queue < txq_number; queue++) {
		int err = mvneta_txq_init(pp, &pp->txqs[queue]);
		if (err) {
			netdev_err(pp->dev, "%s: can't create txq=%d\n",
				   __func__, queue);
			mvneta_cleanup_txqs(pp);
			return err;
		}
	}

	return 0;
}

static void mvneta_start_dev(struct mvneta_port *pp)
{
	mvneta_max_rx_size_set(pp, pp->pkt_size);
	mvneta_txq_max_tx_size_set(pp, pp->pkt_size);

	/* start the Rx/Tx activity */
	mvneta_port_enable(pp);

	/* Enable polling on the port */
	napi_enable(&pp->napi);

	/* Unmask interrupts */
	mvreg_write(pp, MVNETA_INTR_NEW_MASK,
		    MVNETA_RX_INTR_MASK(rxq_number));

	phy_start(pp->phy_dev);
	netif_tx_start_all_queues(pp->dev);
}

static void mvneta_stop_dev(struct mvneta_port *pp)
{
	phy_stop(pp->phy_dev);

	napi_disable(&pp->napi);

	netif_carrier_off(pp->dev);

	mvneta_port_down(pp);
	netif_tx_stop_all_queues(pp->dev);

	/* Stop the port activity */
	mvneta_port_disable(pp);

	/* Clear all ethernet port interrupts */
	mvreg_write(pp, MVNETA_INTR_MISC_CAUSE, 0);
	mvreg_write(pp, MVNETA_INTR_OLD_CAUSE, 0);

	/* Mask all ethernet port interrupts */
	mvreg_write(pp, MVNETA_INTR_NEW_MASK, 0);
	mvreg_write(pp, MVNETA_INTR_OLD_MASK, 0);
	mvreg_write(pp, MVNETA_INTR_MISC_MASK, 0);

	mvneta_tx_reset(pp);
	mvneta_rx_reset(pp);
}

/* tx timeout callback - display a message and stop/start the network device */
static void mvneta_tx_timeout(struct net_device *dev)
{
	struct mvneta_port *pp = netdev_priv(dev);

	netdev_info(dev, "tx timeout\n");
	mvneta_stop_dev(pp);
	mvneta_start_dev(pp);
}

/* Return positive if MTU is valid */
static int mvneta_check_mtu_valid(struct net_device *dev, int mtu)
{
	if (mtu < 68) {
		netdev_err(dev, "cannot change mtu to less than 68\n");
		return -EINVAL;
	}

	/* 9676 == 9700 - 20 and rounding to 8 */
	if (mtu > 9676) {
		netdev_info(dev, "Illegal MTU value %d, round to 9676\n", mtu);
		mtu = 9676;
	}

	if (!IS_ALIGNED(MVNETA_RX_PKT_SIZE(mtu), 8)) {
		netdev_info(dev, "Illegal MTU value %d, rounding to %d\n",
			mtu, ALIGN(MVNETA_RX_PKT_SIZE(mtu), 8));
		mtu = ALIGN(MVNETA_RX_PKT_SIZE(mtu), 8);
	}

	return mtu;
}

/* Change the device mtu */
static int mvneta_change_mtu(struct net_device *dev, int mtu)
{
	struct mvneta_port *pp = netdev_priv(dev);
	int ret;

	mtu = mvneta_check_mtu_valid(dev, mtu);
	if (mtu < 0)
		return -EINVAL;

	dev->mtu = mtu;

	if (!netif_running(dev))
		return 0;

	/* The interface is running, so we have to force a
	 * reallocation of the RXQs
	 */
	mvneta_stop_dev(pp);

	mvneta_cleanup_txqs(pp);
	mvneta_cleanup_rxqs(pp);

	pp->pkt_size = MVNETA_RX_PKT_SIZE(pp->dev->mtu);

	ret = mvneta_setup_rxqs(pp);
	if (ret) {
		netdev_err(pp->dev, "unable to setup rxqs after MTU change\n");
		return ret;
	}

	mvneta_setup_txqs(pp);

	mvneta_start_dev(pp);
	mvneta_port_up(pp);

	return 0;
}

/* Handle setting mac address */
static int mvneta_set_mac_addr(struct net_device *dev, void *addr)
{
	struct mvneta_port *pp = netdev_priv(dev);
	u8 *mac = addr + 2;
	int i;

	if (netif_running(dev))
		return -EBUSY;

	/* Remove previous address table entry */
	mvneta_mac_addr_set(pp, dev->dev_addr, -1);

	/* Set new addr in hw */
	mvneta_mac_addr_set(pp, mac, rxq_def);

	/* Set addr in the device */
	for (i = 0; i < ETH_ALEN; i++)
		dev->dev_addr[i] = mac[i];

	return 0;
}

static void mvneta_adjust_link(struct net_device *ndev)
{
	struct mvneta_port *pp = netdev_priv(ndev);
	struct phy_device *phydev = pp->phy_dev;
	int status_change = 0;

	if (phydev->link) {
		if ((pp->speed != phydev->speed) ||
		    (pp->duplex != phydev->duplex)) {
			u32 val;

			val = mvreg_read(pp, MVNETA_GMAC_AUTONEG_CONFIG);
			val &= ~(MVNETA_GMAC_CONFIG_MII_SPEED |
				 MVNETA_GMAC_CONFIG_GMII_SPEED |
				 MVNETA_GMAC_CONFIG_FULL_DUPLEX);

			if (phydev->duplex)
				val |= MVNETA_GMAC_CONFIG_FULL_DUPLEX;

			if (phydev->speed == SPEED_1000)
				val |= MVNETA_GMAC_CONFIG_GMII_SPEED;
			else
				val |= MVNETA_GMAC_CONFIG_MII_SPEED;

			mvreg_write(pp, MVNETA_GMAC_AUTONEG_CONFIG, val);

			pp->duplex = phydev->duplex;
			pp->speed  = phydev->speed;
		}
	}

	if (phydev->link != pp->link) {
		if (!phydev->link) {
			pp->duplex = -1;
			pp->speed = 0;
		}

		pp->link = phydev->link;
		status_change = 1;
	}

	if (status_change) {
		if (phydev->link) {
			u32 val = mvreg_read(pp, MVNETA_GMAC_AUTONEG_CONFIG);
			val |= (MVNETA_GMAC_FORCE_LINK_PASS |
				MVNETA_GMAC_FORCE_LINK_DOWN);
			mvreg_write(pp, MVNETA_GMAC_AUTONEG_CONFIG, val);
			mvneta_port_up(pp);
			netdev_info(pp->dev, "link up\n");
		} else {
			mvneta_port_down(pp);
			netdev_info(pp->dev, "link down\n");
		}
	}
}

static int mvneta_mdio_probe(struct mvneta_port *pp)
{
	struct phy_device *phy_dev;

	phy_dev = of_phy_connect(pp->dev, pp->phy_node, mvneta_adjust_link, 0,
				 pp->phy_interface);
	if (!phy_dev) {
		netdev_err(pp->dev, "could not find the PHY\n");
		return -ENODEV;
	}

	phy_dev->supported &= PHY_GBIT_FEATURES;
	phy_dev->advertising = phy_dev->supported;

	pp->phy_dev = phy_dev;
	pp->link    = 0;
	pp->duplex  = 0;
	pp->speed   = 0;

	return 0;
}

static void mvneta_mdio_remove(struct mvneta_port *pp)
{
	phy_disconnect(pp->phy_dev);
	pp->phy_dev = NULL;
}

static int mvneta_open(struct net_device *dev)
{
	struct mvneta_port *pp = netdev_priv(dev);
	int ret;

	mvneta_mac_addr_set(pp, dev->dev_addr, rxq_def);

	pp->pkt_size = MVNETA_RX_PKT_SIZE(pp->dev->mtu);

	ret = mvneta_setup_rxqs(pp);
	if (ret)
		return ret;

	ret = mvneta_setup_txqs(pp);
	if (ret)
		goto err_cleanup_rxqs;

	/* Connect to port interrupt line */
	ret = request_irq(pp->dev->irq, mvneta_isr, 0,
			  MVNETA_DRIVER_NAME, pp);
	if (ret) {
		netdev_err(pp->dev, "cannot request irq %d\n", pp->dev->irq);
		goto err_cleanup_txqs;
	}

	/* In default link is down */
	netif_carrier_off(pp->dev);

	ret = mvneta_mdio_probe(pp);
	if (ret < 0) {
		netdev_err(dev, "cannot probe MDIO bus\n");
		goto err_free_irq;
	}

	mvneta_start_dev(pp);

	return 0;

err_free_irq:
	free_irq(pp->dev->irq, pp);
err_cleanup_txqs:
	mvneta_cleanup_txqs(pp);
err_cleanup_rxqs:
	mvneta_cleanup_rxqs(pp);
	return ret;
}

/* Stop the port, free port interrupt line */
static int mvneta_stop(struct net_device *dev)
{
	struct mvneta_port *pp = netdev_priv(dev);

	mvneta_stop_dev(pp);
	mvneta_mdio_remove(pp);
	free_irq(dev->irq, pp);
	mvneta_cleanup_rxqs(pp);
	mvneta_cleanup_txqs(pp);
	del_timer(&pp->tx_done_timer);
	clear_bit(MVNETA_F_TX_DONE_TIMER_BIT, &pp->flags);

	return 0;
}

/* Ethtool methods */

/* Get settings (phy address, speed) for ethtools */
int mvneta_ethtool_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct mvneta_port *pp = netdev_priv(dev);

	if (!pp->phy_dev)
		return -ENODEV;

	return phy_ethtool_gset(pp->phy_dev, cmd);
}

/* Set settings (phy address, speed) for ethtools */
int mvneta_ethtool_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct mvneta_port *pp = netdev_priv(dev);

	if (!pp->phy_dev)
		return -ENODEV;

	return phy_ethtool_sset(pp->phy_dev, cmd);
}

/* Set interrupt coalescing for ethtools */
static int mvneta_ethtool_set_coalesce(struct net_device *dev,
				       struct ethtool_coalesce *c)
{
	struct mvneta_port *pp = netdev_priv(dev);
	int queue;

	for (queue = 0; queue < rxq_number; queue++) {
		struct mvneta_rx_queue *rxq = &pp->rxqs[queue];
		rxq->time_coal = c->rx_coalesce_usecs;
		rxq->pkts_coal = c->rx_max_coalesced_frames;
		mvneta_rx_pkts_coal_set(pp, rxq, rxq->pkts_coal);
		mvneta_rx_time_coal_set(pp, rxq, rxq->time_coal);
	}

	for (queue = 0; queue < txq_number; queue++) {
		struct mvneta_tx_queue *txq = &pp->txqs[queue];
		txq->done_pkts_coal = c->tx_max_coalesced_frames;
		mvneta_tx_done_pkts_coal_set(pp, txq, txq->done_pkts_coal);
	}

	return 0;
}

/* get coalescing for ethtools */
static int mvneta_ethtool_get_coalesce(struct net_device *dev,
				       struct ethtool_coalesce *c)
{
	struct mvneta_port *pp = netdev_priv(dev);

	c->rx_coalesce_usecs        = pp->rxqs[0].time_coal;
	c->rx_max_coalesced_frames  = pp->rxqs[0].pkts_coal;

	c->tx_max_coalesced_frames =  pp->txqs[0].done_pkts_coal;
	return 0;
}


static void mvneta_ethtool_get_drvinfo(struct net_device *dev,
				    struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, MVNETA_DRIVER_NAME,
		sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, MVNETA_DRIVER_VERSION,
		sizeof(drvinfo->version));
	strlcpy(drvinfo->bus_info, dev_name(&dev->dev),
		sizeof(drvinfo->bus_info));
}


static void mvneta_ethtool_get_ringparam(struct net_device *netdev,
					 struct ethtool_ringparam *ring)
{
	struct mvneta_port *pp = netdev_priv(netdev);

	ring->rx_max_pending = MVNETA_MAX_RXD;
	ring->tx_max_pending = MVNETA_MAX_TXD;
	ring->rx_pending = pp->rx_ring_size;
	ring->tx_pending = pp->tx_ring_size;
}

static int mvneta_ethtool_set_ringparam(struct net_device *dev,
					struct ethtool_ringparam *ring)
{
	struct mvneta_port *pp = netdev_priv(dev);

	if ((ring->rx_pending == 0) || (ring->tx_pending == 0))
		return -EINVAL;
	pp->rx_ring_size = ring->rx_pending < MVNETA_MAX_RXD ?
		ring->rx_pending : MVNETA_MAX_RXD;
	pp->tx_ring_size = ring->tx_pending < MVNETA_MAX_TXD ?
		ring->tx_pending : MVNETA_MAX_TXD;

	if (netif_running(dev)) {
		mvneta_stop(dev);
		if (mvneta_open(dev)) {
			netdev_err(dev,
				   "error on opening device after ring param change\n");
			return -ENOMEM;
		}
	}

	return 0;
}

static const struct net_device_ops mvneta_netdev_ops = {
	.ndo_open            = mvneta_open,
	.ndo_stop            = mvneta_stop,
	.ndo_start_xmit      = mvneta_tx,
	.ndo_set_rx_mode     = mvneta_set_rx_mode,
	.ndo_set_mac_address = mvneta_set_mac_addr,
	.ndo_change_mtu      = mvneta_change_mtu,
	.ndo_tx_timeout      = mvneta_tx_timeout,
	.ndo_get_stats64     = mvneta_get_stats64,
};

const struct ethtool_ops mvneta_eth_tool_ops = {
	.get_link       = ethtool_op_get_link,
	.get_settings   = mvneta_ethtool_get_settings,
	.set_settings   = mvneta_ethtool_set_settings,
	.set_coalesce   = mvneta_ethtool_set_coalesce,
	.get_coalesce   = mvneta_ethtool_get_coalesce,
	.get_drvinfo    = mvneta_ethtool_get_drvinfo,
	.get_ringparam  = mvneta_ethtool_get_ringparam,
	.set_ringparam	= mvneta_ethtool_set_ringparam,
};

/* Initialize hw */
static int __devinit mvneta_init(struct mvneta_port *pp, int phy_addr)
{
	int queue;

	/* Disable port */
	mvneta_port_disable(pp);

	/* Set port default values */
	mvneta_defaults_set(pp);

	pp->txqs = kzalloc(txq_number * sizeof(struct mvneta_tx_queue),
			   GFP_KERNEL);
	if (!pp->txqs)
		return -ENOMEM;

	/* Initialize TX descriptor rings */
	for (queue = 0; queue < txq_number; queue++) {
		struct mvneta_tx_queue *txq = &pp->txqs[queue];
		txq->id = queue;
		txq->size = pp->tx_ring_size;
		txq->done_pkts_coal = MVNETA_TXDONE_COAL_PKTS;
	}

	pp->rxqs = kzalloc(rxq_number * sizeof(struct mvneta_rx_queue),
			   GFP_KERNEL);
	if (!pp->rxqs) {
		kfree(pp->txqs);
		return -ENOMEM;
	}

	/* Create Rx descriptor rings */
	for (queue = 0; queue < rxq_number; queue++) {
		struct mvneta_rx_queue *rxq = &pp->rxqs[queue];
		rxq->id = queue;
		rxq->size = pp->rx_ring_size;
		rxq->pkts_coal = MVNETA_RX_COAL_PKTS;
		rxq->time_coal = MVNETA_RX_COAL_USEC;
	}

	return 0;
}

static void __devexit mvneta_deinit(struct mvneta_port *pp)
{
	kfree(pp->txqs);
	kfree(pp->rxqs);
}

/* platform glue : initialize decoding windows */
static void __devinit
mvneta_conf_mbus_windows(struct mvneta_port *pp,
			 const struct mbus_dram_target_info *dram)
{
	u32 win_enable;
	u32 win_protect;
	int i;

	for (i = 0; i < 6; i++) {
		mvreg_write(pp, MVNETA_WIN_BASE(i), 0);
		mvreg_write(pp, MVNETA_WIN_SIZE(i), 0);

		if (i < 4)
			mvreg_write(pp, MVNETA_WIN_REMAP(i), 0);
	}

	win_enable = 0x3f;
	win_protect = 0;

	for (i = 0; i < dram->num_cs; i++) {
		const struct mbus_dram_window *cs = dram->cs + i;
		mvreg_write(pp, MVNETA_WIN_BASE(i), (cs->base & 0xffff0000) |
			    (cs->mbus_attr << 8) | dram->mbus_dram_target_id);

		mvreg_write(pp, MVNETA_WIN_SIZE(i),
			    (cs->size - 1) & 0xffff0000);

		win_enable &= ~(1 << i);
		win_protect |= 3 << (2 * i);
	}

	mvreg_write(pp, MVNETA_BASE_ADDR_ENABLE, win_enable);
}

/* Power up the port */
static void __devinit mvneta_port_power_up(struct mvneta_port *pp, int phy_mode)
{
	u32 val;

	/* MAC Cause register should be cleared */
	mvreg_write(pp, MVNETA_UNIT_INTR_CAUSE, 0);

	if (phy_mode == PHY_INTERFACE_MODE_SGMII)
		mvneta_port_sgmii_config(pp);

	mvneta_gmac_rgmii_set(pp, 1);

	/* Cancel Port Reset */
	val = mvreg_read(pp, MVNETA_GMAC_CTRL_2);
	val &= ~MVNETA_GMAC2_PORT_RESET;
	mvreg_write(pp, MVNETA_GMAC_CTRL_2, val);

	while ((mvreg_read(pp, MVNETA_GMAC_CTRL_2) &
		MVNETA_GMAC2_PORT_RESET) != 0)
		continue;
}

/* Device initialization routine */
static int __devinit mvneta_probe(struct platform_device *pdev)
{
	const struct mbus_dram_target_info *dram_target_info;
	struct device_node *dn = pdev->dev.of_node;
	struct device_node *phy_node;
	u32 phy_addr, clk_rate_hz;
	struct mvneta_port *pp;
	struct net_device *dev;
	const char *mac_addr;
	int phy_mode;
	int err;

	/* Our multiqueue support is not complete, so for now, only
	 * allow the usage of the first RX queue
	 */
	if (rxq_def != 0) {
		dev_err(&pdev->dev, "Invalid rxq_def argument: %d\n", rxq_def);
		return -EINVAL;
	}

	dev = alloc_etherdev_mq(sizeof(struct mvneta_port), 8);
	if (!dev)
		return -ENOMEM;

	dev->irq = irq_of_parse_and_map(dn, 0);
	if (dev->irq == 0) {
		err = -EINVAL;
		goto err_free_netdev;
	}

	phy_node = of_parse_phandle(dn, "phy", 0);
	if (!phy_node) {
		dev_err(&pdev->dev, "no associated PHY\n");
		err = -ENODEV;
		goto err_free_irq;
	}

	phy_mode = of_get_phy_mode(dn);
	if (phy_mode < 0) {
		dev_err(&pdev->dev, "incorrect phy-mode\n");
		err = -EINVAL;
		goto err_free_irq;
	}

	if (of_property_read_u32(dn, "clock-frequency", &clk_rate_hz) != 0) {
		dev_err(&pdev->dev, "could not read clock-frequency\n");
		err = -EINVAL;
		goto err_free_irq;
	}

	mac_addr = of_get_mac_address(dn);

	if (!mac_addr || !is_valid_ether_addr(mac_addr))
		eth_hw_addr_random(dev);
	else
		memcpy(dev->dev_addr, mac_addr, ETH_ALEN);

	dev->tx_queue_len = MVNETA_MAX_TXD;
	dev->watchdog_timeo = 5 * HZ;
	dev->netdev_ops = &mvneta_netdev_ops;

	SET_ETHTOOL_OPS(dev, &mvneta_eth_tool_ops);

	pp = netdev_priv(dev);

	pp->tx_done_timer.function = mvneta_tx_done_timer_callback;
	init_timer(&pp->tx_done_timer);
	clear_bit(MVNETA_F_TX_DONE_TIMER_BIT, &pp->flags);

	pp->weight = MVNETA_RX_POLL_WEIGHT;
	pp->clk_rate_hz = clk_rate_hz;
	pp->phy_node = phy_node;
	pp->phy_interface = phy_mode;

	pp->base = of_iomap(dn, 0);
	if (pp->base == NULL) {
		err = -ENOMEM;
		goto err_free_irq;
	}

	pp->tx_done_timer.data = (unsigned long)dev;

	pp->tx_ring_size = MVNETA_MAX_TXD;
	pp->rx_ring_size = MVNETA_MAX_RXD;

	pp->dev = dev;
	SET_NETDEV_DEV(dev, &pdev->dev);

	err = mvneta_init(pp, phy_addr);
	if (err < 0) {
		dev_err(&pdev->dev, "can't init eth hal\n");
		goto err_unmap;
	}
	mvneta_port_power_up(pp, phy_mode);

	dram_target_info = mv_mbus_dram_info();
	if (dram_target_info)
		mvneta_conf_mbus_windows(pp, dram_target_info);

	netif_napi_add(dev, &pp->napi, mvneta_poll, pp->weight);

	err = register_netdev(dev);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register\n");
		goto err_deinit;
	}

	dev->features = NETIF_F_SG | NETIF_F_IP_CSUM;
	dev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM;
	dev->priv_flags |= IFF_UNICAST_FLT;

	netdev_info(dev, "mac: %pM\n", dev->dev_addr);

	platform_set_drvdata(pdev, pp->dev);

	return 0;

err_deinit:
	mvneta_deinit(pp);
err_unmap:
	iounmap(pp->base);
err_free_irq:
	irq_dispose_mapping(dev->irq);
err_free_netdev:
	free_netdev(dev);
	return err;
}

/* Device removal routine */
static int __devexit mvneta_remove(struct platform_device *pdev)
{
	struct net_device  *dev = platform_get_drvdata(pdev);
	struct mvneta_port *pp = netdev_priv(dev);

	unregister_netdev(dev);
	mvneta_deinit(pp);
	iounmap(pp->base);
	irq_dispose_mapping(dev->irq);
	free_netdev(dev);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id mvneta_match[] = {
	{ .compatible = "marvell,armada-370-neta" },
	{ }
};
MODULE_DEVICE_TABLE(of, mvneta_match);

static struct platform_driver mvneta_driver = {
	.probe = mvneta_probe,
	.remove = __devexit_p(mvneta_remove),
	.driver = {
		.name = MVNETA_DRIVER_NAME,
		.of_match_table = mvneta_match,
	},
};

module_platform_driver(mvneta_driver);

MODULE_DESCRIPTION("Marvell NETA Ethernet Driver - www.marvell.com");
MODULE_AUTHOR("Rami Rosen <rosenr@marvell.com>, Thomas Petazzoni <thomas.petazzoni@free-electrons.com>");
MODULE_LICENSE("GPL");

module_param(rxq_number, int, S_IRUGO);
module_param(txq_number, int, S_IRUGO);

module_param(rxq_def, int, S_IRUGO);
module_param(txq_def, int, S_IRUGO);
