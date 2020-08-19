// SPDX-License-Identifier: GPL-2.0
/* Ethernet device driver for Cortina Systems Gemini SoC
 * Also known as the StorLink SL3512 and SL3516 (SL351x) or Lepus
 * Net Engine and Gigabit Ethernet MAC (GMAC)
 * This hardware contains a TCP Offload Engine (TOE) but currently the
 * driver does not make use of it.
 *
 * Authors:
 * Linus Walleij <linus.walleij@linaro.org>
 * Tobias Waldvogel <tobias.waldvogel@gmail.com> (OpenWRT)
 * Michał Mirosław <mirq-linux@rere.qmqm.pl>
 * Paulius Zaleckas <paulius.zaleckas@gmail.com>
 * Giuseppe De Robertis <Giuseppe.DeRobertis@ba.infn.it>
 * Gary Chen & Ch Hsu Storlink Semiconductor
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/skbuff.h>
#include <linux/phy.h>
#include <linux/crc32.h>
#include <linux/ethtool.h>
#include <linux/tcp.h>
#include <linux/u64_stats_sync.h>

#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>

#include "gemini.h"

#define DRV_NAME		"gmac-gemini"
#define DRV_VERSION		"1.0"

#define DEFAULT_MSG_ENABLE (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)
static int debug = -1;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

#define HSIZE_8			0x00
#define HSIZE_16		0x01
#define HSIZE_32		0x02

#define HBURST_SINGLE		0x00
#define HBURST_INCR		0x01
#define HBURST_INCR4		0x02
#define HBURST_INCR8		0x03

#define HPROT_DATA_CACHE	BIT(0)
#define HPROT_PRIVILIGED	BIT(1)
#define HPROT_BUFFERABLE	BIT(2)
#define HPROT_CACHABLE		BIT(3)

#define DEFAULT_RX_COALESCE_NSECS	0
#define DEFAULT_GMAC_RXQ_ORDER		9
#define DEFAULT_GMAC_TXQ_ORDER		8
#define DEFAULT_RX_BUF_ORDER		11
#define DEFAULT_NAPI_WEIGHT		64
#define TX_MAX_FRAGS			16
#define TX_QUEUE_NUM			1	/* max: 6 */
#define RX_MAX_ALLOC_ORDER		2

#define GMAC0_IRQ0_2 (GMAC0_TXDERR_INT_BIT | GMAC0_TXPERR_INT_BIT | \
		      GMAC0_RXDERR_INT_BIT | GMAC0_RXPERR_INT_BIT)
#define GMAC0_IRQ0_TXQ0_INTS (GMAC0_SWTQ00_EOF_INT_BIT | \
			      GMAC0_SWTQ00_FIN_INT_BIT)
#define GMAC0_IRQ4_8 (GMAC0_MIB_INT_BIT | GMAC0_RX_OVERRUN_INT_BIT)

#define GMAC_OFFLOAD_FEATURES (NETIF_F_SG | NETIF_F_IP_CSUM | \
		NETIF_F_IPV6_CSUM | NETIF_F_RXCSUM | \
		NETIF_F_TSO | NETIF_F_TSO_ECN | NETIF_F_TSO6)

/**
 * struct gmac_queue_page - page buffer per-page info
 */
struct gmac_queue_page {
	struct page *page;
	dma_addr_t mapping;
};

struct gmac_txq {
	struct gmac_txdesc *ring;
	struct sk_buff	**skb;
	unsigned int	cptr;
	unsigned int	noirq_packets;
};

struct gemini_ethernet;

struct gemini_ethernet_port {
	u8 id; /* 0 or 1 */

	struct gemini_ethernet *geth;
	struct net_device *netdev;
	struct device *dev;
	void __iomem *dma_base;
	void __iomem *gmac_base;
	struct clk *pclk;
	struct reset_control *reset;
	int irq;
	__le32 mac_addr[3];

	void __iomem		*rxq_rwptr;
	struct gmac_rxdesc	*rxq_ring;
	unsigned int		rxq_order;

	struct napi_struct	napi;
	struct hrtimer		rx_coalesce_timer;
	unsigned int		rx_coalesce_nsecs;
	unsigned int		freeq_refill;
	struct gmac_txq		txq[TX_QUEUE_NUM];
	unsigned int		txq_order;
	unsigned int		irq_every_tx_packets;

	dma_addr_t		rxq_dma_base;
	dma_addr_t		txq_dma_base;

	unsigned int		msg_enable;
	spinlock_t		config_lock; /* Locks config register */

	struct u64_stats_sync	tx_stats_syncp;
	struct u64_stats_sync	rx_stats_syncp;
	struct u64_stats_sync	ir_stats_syncp;

	struct rtnl_link_stats64 stats;
	u64			hw_stats[RX_STATS_NUM];
	u64			rx_stats[RX_STATUS_NUM];
	u64			rx_csum_stats[RX_CHKSUM_NUM];
	u64			rx_napi_exits;
	u64			tx_frag_stats[TX_MAX_FRAGS];
	u64			tx_frags_linearized;
	u64			tx_hw_csummed;
};

struct gemini_ethernet {
	struct device *dev;
	void __iomem *base;
	struct gemini_ethernet_port *port0;
	struct gemini_ethernet_port *port1;
	bool initialized;

	spinlock_t	irq_lock; /* Locks IRQ-related registers */
	unsigned int	freeq_order;
	unsigned int	freeq_frag_order;
	struct gmac_rxdesc *freeq_ring;
	dma_addr_t	freeq_dma_base;
	struct gmac_queue_page	*freeq_pages;
	unsigned int	num_freeq_pages;
	spinlock_t	freeq_lock; /* Locks queue from reentrance */
};

#define GMAC_STATS_NUM	( \
	RX_STATS_NUM + RX_STATUS_NUM + RX_CHKSUM_NUM + 1 + \
	TX_MAX_FRAGS + 2)

static const char gmac_stats_strings[GMAC_STATS_NUM][ETH_GSTRING_LEN] = {
	"GMAC_IN_DISCARDS",
	"GMAC_IN_ERRORS",
	"GMAC_IN_MCAST",
	"GMAC_IN_BCAST",
	"GMAC_IN_MAC1",
	"GMAC_IN_MAC2",
	"RX_STATUS_GOOD_FRAME",
	"RX_STATUS_TOO_LONG_GOOD_CRC",
	"RX_STATUS_RUNT_FRAME",
	"RX_STATUS_SFD_NOT_FOUND",
	"RX_STATUS_CRC_ERROR",
	"RX_STATUS_TOO_LONG_BAD_CRC",
	"RX_STATUS_ALIGNMENT_ERROR",
	"RX_STATUS_TOO_LONG_BAD_ALIGN",
	"RX_STATUS_RX_ERR",
	"RX_STATUS_DA_FILTERED",
	"RX_STATUS_BUFFER_FULL",
	"RX_STATUS_11",
	"RX_STATUS_12",
	"RX_STATUS_13",
	"RX_STATUS_14",
	"RX_STATUS_15",
	"RX_CHKSUM_IP_UDP_TCP_OK",
	"RX_CHKSUM_IP_OK_ONLY",
	"RX_CHKSUM_NONE",
	"RX_CHKSUM_3",
	"RX_CHKSUM_IP_ERR_UNKNOWN",
	"RX_CHKSUM_IP_ERR",
	"RX_CHKSUM_TCP_UDP_ERR",
	"RX_CHKSUM_7",
	"RX_NAPI_EXITS",
	"TX_FRAGS[1]",
	"TX_FRAGS[2]",
	"TX_FRAGS[3]",
	"TX_FRAGS[4]",
	"TX_FRAGS[5]",
	"TX_FRAGS[6]",
	"TX_FRAGS[7]",
	"TX_FRAGS[8]",
	"TX_FRAGS[9]",
	"TX_FRAGS[10]",
	"TX_FRAGS[11]",
	"TX_FRAGS[12]",
	"TX_FRAGS[13]",
	"TX_FRAGS[14]",
	"TX_FRAGS[15]",
	"TX_FRAGS[16+]",
	"TX_FRAGS_LINEARIZED",
	"TX_HW_CSUMMED",
};

static void gmac_dump_dma_state(struct net_device *netdev);

static void gmac_update_config0_reg(struct net_device *netdev,
				    u32 val, u32 vmask)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&port->config_lock, flags);

	reg = readl(port->gmac_base + GMAC_CONFIG0);
	reg = (reg & ~vmask) | val;
	writel(reg, port->gmac_base + GMAC_CONFIG0);

	spin_unlock_irqrestore(&port->config_lock, flags);
}

static void gmac_enable_tx_rx(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&port->config_lock, flags);

	reg = readl(port->gmac_base + GMAC_CONFIG0);
	reg &= ~CONFIG0_TX_RX_DISABLE;
	writel(reg, port->gmac_base + GMAC_CONFIG0);

	spin_unlock_irqrestore(&port->config_lock, flags);
}

static void gmac_disable_tx_rx(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&port->config_lock, flags);

	val = readl(port->gmac_base + GMAC_CONFIG0);
	val |= CONFIG0_TX_RX_DISABLE;
	writel(val, port->gmac_base + GMAC_CONFIG0);

	spin_unlock_irqrestore(&port->config_lock, flags);

	mdelay(10);	/* let GMAC consume packet */
}

static void gmac_set_flow_control(struct net_device *netdev, bool tx, bool rx)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&port->config_lock, flags);

	val = readl(port->gmac_base + GMAC_CONFIG0);
	val &= ~CONFIG0_FLOW_CTL;
	if (tx)
		val |= CONFIG0_FLOW_TX;
	if (rx)
		val |= CONFIG0_FLOW_RX;
	writel(val, port->gmac_base + GMAC_CONFIG0);

	spin_unlock_irqrestore(&port->config_lock, flags);
}

static void gmac_speed_set(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	struct phy_device *phydev = netdev->phydev;
	union gmac_status status, old_status;
	int pause_tx = 0;
	int pause_rx = 0;

	status.bits32 = readl(port->gmac_base + GMAC_STATUS);
	old_status.bits32 = status.bits32;
	status.bits.link = phydev->link;
	status.bits.duplex = phydev->duplex;

	switch (phydev->speed) {
	case 1000:
		status.bits.speed = GMAC_SPEED_1000;
		if (phydev->interface == PHY_INTERFACE_MODE_RGMII)
			status.bits.mii_rmii = GMAC_PHY_RGMII_1000;
		netdev_dbg(netdev, "connect %s to RGMII @ 1Gbit\n",
			   phydev_name(phydev));
		break;
	case 100:
		status.bits.speed = GMAC_SPEED_100;
		if (phydev->interface == PHY_INTERFACE_MODE_RGMII)
			status.bits.mii_rmii = GMAC_PHY_RGMII_100_10;
		netdev_dbg(netdev, "connect %s to RGMII @ 100 Mbit\n",
			   phydev_name(phydev));
		break;
	case 10:
		status.bits.speed = GMAC_SPEED_10;
		if (phydev->interface == PHY_INTERFACE_MODE_RGMII)
			status.bits.mii_rmii = GMAC_PHY_RGMII_100_10;
		netdev_dbg(netdev, "connect %s to RGMII @ 10 Mbit\n",
			   phydev_name(phydev));
		break;
	default:
		netdev_warn(netdev, "Unsupported PHY speed (%d) on %s\n",
			    phydev->speed, phydev_name(phydev));
	}

	if (phydev->duplex == DUPLEX_FULL) {
		u16 lcladv = phy_read(phydev, MII_ADVERTISE);
		u16 rmtadv = phy_read(phydev, MII_LPA);
		u8 cap = mii_resolve_flowctrl_fdx(lcladv, rmtadv);

		if (cap & FLOW_CTRL_RX)
			pause_rx = 1;
		if (cap & FLOW_CTRL_TX)
			pause_tx = 1;
	}

	gmac_set_flow_control(netdev, pause_tx, pause_rx);

	if (old_status.bits32 == status.bits32)
		return;

	if (netif_msg_link(port)) {
		phy_print_status(phydev);
		netdev_info(netdev, "link flow control: %s\n",
			    phydev->pause
			    ? (phydev->asym_pause ? "tx" : "both")
			    : (phydev->asym_pause ? "rx" : "none")
		);
	}

	gmac_disable_tx_rx(netdev);
	writel(status.bits32, port->gmac_base + GMAC_STATUS);
	gmac_enable_tx_rx(netdev);
}

static int gmac_setup_phy(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	union gmac_status status = { .bits32 = 0 };
	struct device *dev = port->dev;
	struct phy_device *phy;

	phy = of_phy_get_and_connect(netdev,
				     dev->of_node,
				     gmac_speed_set);
	if (!phy)
		return -ENODEV;
	netdev->phydev = phy;

	phy->supported &= PHY_GBIT_FEATURES;
	phy->supported |= SUPPORTED_Asym_Pause | SUPPORTED_Pause;
	phy->advertising = phy->supported;

	/* set PHY interface type */
	switch (phy->interface) {
	case PHY_INTERFACE_MODE_MII:
		netdev_dbg(netdev,
			   "MII: set GMAC0 to GMII mode, GMAC1 disabled\n");
		status.bits.mii_rmii = GMAC_PHY_MII;
		break;
	case PHY_INTERFACE_MODE_GMII:
		netdev_dbg(netdev,
			   "GMII: set GMAC0 to GMII mode, GMAC1 disabled\n");
		status.bits.mii_rmii = GMAC_PHY_GMII;
		break;
	case PHY_INTERFACE_MODE_RGMII:
		netdev_dbg(netdev,
			   "RGMII: set GMAC0 and GMAC1 to MII/RGMII mode\n");
		status.bits.mii_rmii = GMAC_PHY_RGMII_100_10;
		break;
	default:
		netdev_err(netdev, "Unsupported MII interface\n");
		phy_disconnect(phy);
		netdev->phydev = NULL;
		return -EINVAL;
	}
	writel(status.bits32, port->gmac_base + GMAC_STATUS);

	if (netif_msg_link(port))
		phy_attached_info(phy);

	return 0;
}

/* The maximum frame length is not logically enumerated in the
 * hardware, so we do a table lookup to find the applicable max
 * frame length.
 */
struct gmac_max_framelen {
	unsigned int max_l3_len;
	u8 val;
};

static const struct gmac_max_framelen gmac_maxlens[] = {
	{
		.max_l3_len = 1518,
		.val = CONFIG0_MAXLEN_1518,
	},
	{
		.max_l3_len = 1522,
		.val = CONFIG0_MAXLEN_1522,
	},
	{
		.max_l3_len = 1536,
		.val = CONFIG0_MAXLEN_1536,
	},
	{
		.max_l3_len = 1542,
		.val = CONFIG0_MAXLEN_1542,
	},
	{
		.max_l3_len = 9212,
		.val = CONFIG0_MAXLEN_9k,
	},
	{
		.max_l3_len = 10236,
		.val = CONFIG0_MAXLEN_10k,
	},
};

static int gmac_pick_rx_max_len(unsigned int max_l3_len)
{
	const struct gmac_max_framelen *maxlen;
	int maxtot;
	int i;

	maxtot = max_l3_len + ETH_HLEN + VLAN_HLEN;

	for (i = 0; i < ARRAY_SIZE(gmac_maxlens); i++) {
		maxlen = &gmac_maxlens[i];
		if (maxtot <= maxlen->max_l3_len)
			return maxlen->val;
	}

	return -1;
}

static int gmac_init(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	union gmac_config0 config0 = { .bits = {
		.dis_tx = 1,
		.dis_rx = 1,
		.ipv4_rx_chksum = 1,
		.ipv6_rx_chksum = 1,
		.rx_err_detect = 1,
		.rgmm_edge = 1,
		.port0_chk_hwq = 1,
		.port1_chk_hwq = 1,
		.port0_chk_toeq = 1,
		.port1_chk_toeq = 1,
		.port0_chk_classq = 1,
		.port1_chk_classq = 1,
	} };
	union gmac_ahb_weight ahb_weight = { .bits = {
		.rx_weight = 1,
		.tx_weight = 1,
		.hash_weight = 1,
		.pre_req = 0x1f,
		.tq_dv_threshold = 0,
	} };
	union gmac_tx_wcr0 hw_weigh = { .bits = {
		.hw_tq3 = 1,
		.hw_tq2 = 1,
		.hw_tq1 = 1,
		.hw_tq0 = 1,
	} };
	union gmac_tx_wcr1 sw_weigh = { .bits = {
		.sw_tq5 = 1,
		.sw_tq4 = 1,
		.sw_tq3 = 1,
		.sw_tq2 = 1,
		.sw_tq1 = 1,
		.sw_tq0 = 1,
	} };
	union gmac_config1 config1 = { .bits = {
		.set_threshold = 16,
		.rel_threshold = 24,
	} };
	union gmac_config2 config2 = { .bits = {
		.set_threshold = 16,
		.rel_threshold = 32,
	} };
	union gmac_config3 config3 = { .bits = {
		.set_threshold = 0,
		.rel_threshold = 0,
	} };
	union gmac_config0 tmp;
	u32 val;

	config0.bits.max_len = gmac_pick_rx_max_len(netdev->mtu);
	tmp.bits32 = readl(port->gmac_base + GMAC_CONFIG0);
	config0.bits.reserved = tmp.bits.reserved;
	writel(config0.bits32, port->gmac_base + GMAC_CONFIG0);
	writel(config1.bits32, port->gmac_base + GMAC_CONFIG1);
	writel(config2.bits32, port->gmac_base + GMAC_CONFIG2);
	writel(config3.bits32, port->gmac_base + GMAC_CONFIG3);

	val = readl(port->dma_base + GMAC_AHB_WEIGHT_REG);
	writel(ahb_weight.bits32, port->dma_base + GMAC_AHB_WEIGHT_REG);

	writel(hw_weigh.bits32,
	       port->dma_base + GMAC_TX_WEIGHTING_CTRL_0_REG);
	writel(sw_weigh.bits32,
	       port->dma_base + GMAC_TX_WEIGHTING_CTRL_1_REG);

	port->rxq_order = DEFAULT_GMAC_RXQ_ORDER;
	port->txq_order = DEFAULT_GMAC_TXQ_ORDER;
	port->rx_coalesce_nsecs = DEFAULT_RX_COALESCE_NSECS;

	/* Mark every quarter of the queue a packet for interrupt
	 * in order to be able to wake up the queue if it was stopped
	 */
	port->irq_every_tx_packets = 1 << (port->txq_order - 2);

	return 0;
}

static void gmac_uninit(struct net_device *netdev)
{
	if (netdev->phydev)
		phy_disconnect(netdev->phydev);
}

static int gmac_setup_txqs(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	unsigned int n_txq = netdev->num_tx_queues;
	struct gemini_ethernet *geth = port->geth;
	size_t entries = 1 << port->txq_order;
	struct gmac_txq *txq = port->txq;
	struct gmac_txdesc *desc_ring;
	size_t len = n_txq * entries;
	struct sk_buff **skb_tab;
	void __iomem *rwptr_reg;
	unsigned int r;
	int i;

	rwptr_reg = port->dma_base + GMAC_SW_TX_QUEUE0_PTR_REG;

	skb_tab = kcalloc(len, sizeof(*skb_tab), GFP_KERNEL);
	if (!skb_tab)
		return -ENOMEM;

	desc_ring = dma_alloc_coherent(geth->dev, len * sizeof(*desc_ring),
				       &port->txq_dma_base, GFP_KERNEL);

	if (!desc_ring) {
		kfree(skb_tab);
		return -ENOMEM;
	}

	if (port->txq_dma_base & ~DMA_Q_BASE_MASK) {
		dev_warn(geth->dev, "TX queue base is not aligned\n");
		dma_free_coherent(geth->dev, len * sizeof(*desc_ring),
				  desc_ring, port->txq_dma_base);
		kfree(skb_tab);
		return -ENOMEM;
	}

	writel(port->txq_dma_base | port->txq_order,
	       port->dma_base + GMAC_SW_TX_QUEUE_BASE_REG);

	for (i = 0; i < n_txq; i++) {
		txq->ring = desc_ring;
		txq->skb = skb_tab;
		txq->noirq_packets = 0;

		r = readw(rwptr_reg);
		rwptr_reg += 2;
		writew(r, rwptr_reg);
		rwptr_reg += 2;
		txq->cptr = r;

		txq++;
		desc_ring += entries;
		skb_tab += entries;
	}

	return 0;
}

static void gmac_clean_txq(struct net_device *netdev, struct gmac_txq *txq,
			   unsigned int r)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	unsigned int m = (1 << port->txq_order) - 1;
	struct gemini_ethernet *geth = port->geth;
	unsigned int c = txq->cptr;
	union gmac_txdesc_0 word0;
	union gmac_txdesc_1 word1;
	unsigned int hwchksum = 0;
	unsigned long bytes = 0;
	struct gmac_txdesc *txd;
	unsigned short nfrags;
	unsigned int errs = 0;
	unsigned int pkts = 0;
	unsigned int word3;
	dma_addr_t mapping;

	if (c == r)
		return;

	while (c != r) {
		txd = txq->ring + c;
		word0 = txd->word0;
		word1 = txd->word1;
		mapping = txd->word2.buf_adr;
		word3 = txd->word3.bits32;

		dma_unmap_single(geth->dev, mapping,
				 word0.bits.buffer_size, DMA_TO_DEVICE);

		if (word3 & EOF_BIT)
			dev_kfree_skb(txq->skb[c]);

		c++;
		c &= m;

		if (!(word3 & SOF_BIT))
			continue;

		if (!word0.bits.status_tx_ok) {
			errs++;
			continue;
		}

		pkts++;
		bytes += txd->word1.bits.byte_count;

		if (word1.bits32 & TSS_CHECKUM_ENABLE)
			hwchksum++;

		nfrags = word0.bits.desc_count - 1;
		if (nfrags) {
			if (nfrags >= TX_MAX_FRAGS)
				nfrags = TX_MAX_FRAGS - 1;

			u64_stats_update_begin(&port->tx_stats_syncp);
			port->tx_frag_stats[nfrags]++;
			u64_stats_update_end(&port->tx_stats_syncp);
		}
	}

	u64_stats_update_begin(&port->ir_stats_syncp);
	port->stats.tx_errors += errs;
	port->stats.tx_packets += pkts;
	port->stats.tx_bytes += bytes;
	port->tx_hw_csummed += hwchksum;
	u64_stats_update_end(&port->ir_stats_syncp);

	txq->cptr = c;
}

static void gmac_cleanup_txqs(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	unsigned int n_txq = netdev->num_tx_queues;
	struct gemini_ethernet *geth = port->geth;
	void __iomem *rwptr_reg;
	unsigned int r, i;

	rwptr_reg = port->dma_base + GMAC_SW_TX_QUEUE0_PTR_REG;

	for (i = 0; i < n_txq; i++) {
		r = readw(rwptr_reg);
		rwptr_reg += 2;
		writew(r, rwptr_reg);
		rwptr_reg += 2;

		gmac_clean_txq(netdev, port->txq + i, r);
	}
	writel(0, port->dma_base + GMAC_SW_TX_QUEUE_BASE_REG);

	kfree(port->txq->skb);
	dma_free_coherent(geth->dev,
			  n_txq * sizeof(*port->txq->ring) << port->txq_order,
			  port->txq->ring, port->txq_dma_base);
}

static int gmac_setup_rxq(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	struct gemini_ethernet *geth = port->geth;
	struct nontoe_qhdr __iomem *qhdr;

	qhdr = geth->base + TOE_DEFAULT_Q_HDR_BASE(netdev->dev_id);
	port->rxq_rwptr = &qhdr->word1;

	/* Remap a slew of memory to use for the RX queue */
	port->rxq_ring = dma_alloc_coherent(geth->dev,
				sizeof(*port->rxq_ring) << port->rxq_order,
				&port->rxq_dma_base, GFP_KERNEL);
	if (!port->rxq_ring)
		return -ENOMEM;
	if (port->rxq_dma_base & ~NONTOE_QHDR0_BASE_MASK) {
		dev_warn(geth->dev, "RX queue base is not aligned\n");
		return -ENOMEM;
	}

	writel(port->rxq_dma_base | port->rxq_order, &qhdr->word0);
	writel(0, port->rxq_rwptr);
	return 0;
}

static struct gmac_queue_page *
gmac_get_queue_page(struct gemini_ethernet *geth,
		    struct gemini_ethernet_port *port,
		    dma_addr_t addr)
{
	struct gmac_queue_page *gpage;
	dma_addr_t mapping;
	int i;

	/* Only look for even pages */
	mapping = addr & PAGE_MASK;

	if (!geth->freeq_pages) {
		dev_err(geth->dev, "try to get page with no page list\n");
		return NULL;
	}

	/* Look up a ring buffer page from virtual mapping */
	for (i = 0; i < geth->num_freeq_pages; i++) {
		gpage = &geth->freeq_pages[i];
		if (gpage->mapping == mapping)
			return gpage;
	}

	return NULL;
}

static void gmac_cleanup_rxq(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	struct gemini_ethernet *geth = port->geth;
	struct gmac_rxdesc *rxd = port->rxq_ring;
	static struct gmac_queue_page *gpage;
	struct nontoe_qhdr __iomem *qhdr;
	void __iomem *dma_reg;
	void __iomem *ptr_reg;
	dma_addr_t mapping;
	union dma_rwptr rw;
	unsigned int r, w;

	qhdr = geth->base +
		TOE_DEFAULT_Q_HDR_BASE(netdev->dev_id);
	dma_reg = &qhdr->word0;
	ptr_reg = &qhdr->word1;

	rw.bits32 = readl(ptr_reg);
	r = rw.bits.rptr;
	w = rw.bits.wptr;
	writew(r, ptr_reg + 2);

	writel(0, dma_reg);

	/* Loop from read pointer to write pointer of the RX queue
	 * and free up all pages by the queue.
	 */
	while (r != w) {
		mapping = rxd[r].word2.buf_adr;
		r++;
		r &= ((1 << port->rxq_order) - 1);

		if (!mapping)
			continue;

		/* Freeq pointers are one page off */
		gpage = gmac_get_queue_page(geth, port, mapping + PAGE_SIZE);
		if (!gpage) {
			dev_err(geth->dev, "could not find page\n");
			continue;
		}
		/* Release the RX queue reference to the page */
		put_page(gpage->page);
	}

	dma_free_coherent(geth->dev, sizeof(*port->rxq_ring) << port->rxq_order,
			  port->rxq_ring, port->rxq_dma_base);
}

static struct page *geth_freeq_alloc_map_page(struct gemini_ethernet *geth,
					      int pn)
{
	struct gmac_rxdesc *freeq_entry;
	struct gmac_queue_page *gpage;
	unsigned int fpp_order;
	unsigned int frag_len;
	dma_addr_t mapping;
	struct page *page;
	int i;

	/* First allocate and DMA map a single page */
	page = alloc_page(GFP_ATOMIC);
	if (!page)
		return NULL;

	mapping = dma_map_single(geth->dev, page_address(page),
				 PAGE_SIZE, DMA_FROM_DEVICE);
	if (dma_mapping_error(geth->dev, mapping)) {
		put_page(page);
		return NULL;
	}

	/* The assign the page mapping (physical address) to the buffer address
	 * in the hardware queue. PAGE_SHIFT on ARM is 12 (1 page is 4096 bytes,
	 * 4k), and the default RX frag order is 11 (fragments are up 20 2048
	 * bytes, 2k) so fpp_order (fragments per page order) is default 1. Thus
	 * each page normally needs two entries in the queue.
	 */
	frag_len = 1 << geth->freeq_frag_order; /* Usually 2048 */
	fpp_order = PAGE_SHIFT - geth->freeq_frag_order;
	freeq_entry = geth->freeq_ring + (pn << fpp_order);
	dev_dbg(geth->dev, "allocate page %d fragment length %d fragments per page %d, freeq entry %p\n",
		 pn, frag_len, (1 << fpp_order), freeq_entry);
	for (i = (1 << fpp_order); i > 0; i--) {
		freeq_entry->word2.buf_adr = mapping;
		freeq_entry++;
		mapping += frag_len;
	}

	/* If the freeq entry already has a page mapped, then unmap it. */
	gpage = &geth->freeq_pages[pn];
	if (gpage->page) {
		mapping = geth->freeq_ring[pn << fpp_order].word2.buf_adr;
		dma_unmap_single(geth->dev, mapping, frag_len, DMA_FROM_DEVICE);
		/* This should be the last reference to the page so it gets
		 * released
		 */
		put_page(gpage->page);
	}

	/* Then put our new mapping into the page table */
	dev_dbg(geth->dev, "page %d, DMA addr: %08x, page %p\n",
		pn, (unsigned int)mapping, page);
	gpage->mapping = mapping;
	gpage->page = page;

	return page;
}

/**
 * geth_fill_freeq() - Fill the freeq with empty fragments to use
 * @geth: the ethernet adapter
 * @refill: whether to reset the queue by filling in all freeq entries or
 * just refill it, usually the interrupt to refill the queue happens when
 * the queue is half empty.
 */
static unsigned int geth_fill_freeq(struct gemini_ethernet *geth, bool refill)
{
	unsigned int fpp_order = PAGE_SHIFT - geth->freeq_frag_order;
	unsigned int count = 0;
	unsigned int pn, epn;
	unsigned long flags;
	union dma_rwptr rw;
	unsigned int m_pn;

	/* Mask for page */
	m_pn = (1 << (geth->freeq_order - fpp_order)) - 1;

	spin_lock_irqsave(&geth->freeq_lock, flags);

	rw.bits32 = readl(geth->base + GLOBAL_SWFQ_RWPTR_REG);
	pn = (refill ? rw.bits.wptr : rw.bits.rptr) >> fpp_order;
	epn = (rw.bits.rptr >> fpp_order) - 1;
	epn &= m_pn;

	/* Loop over the freeq ring buffer entries */
	while (pn != epn) {
		struct gmac_queue_page *gpage;
		struct page *page;

		gpage = &geth->freeq_pages[pn];
		page = gpage->page;

		dev_dbg(geth->dev, "fill entry %d page ref count %d add %d refs\n",
			pn, page_ref_count(page), 1 << fpp_order);

		if (page_ref_count(page) > 1) {
			unsigned int fl = (pn - epn) & m_pn;

			if (fl > 64 >> fpp_order)
				break;

			page = geth_freeq_alloc_map_page(geth, pn);
			if (!page)
				break;
		}

		/* Add one reference per fragment in the page */
		page_ref_add(page, 1 << fpp_order);
		count += 1 << fpp_order;
		pn++;
		pn &= m_pn;
	}

	writew(pn << fpp_order, geth->base + GLOBAL_SWFQ_RWPTR_REG + 2);

	spin_unlock_irqrestore(&geth->freeq_lock, flags);

	return count;
}

static int geth_setup_freeq(struct gemini_ethernet *geth)
{
	unsigned int fpp_order = PAGE_SHIFT - geth->freeq_frag_order;
	unsigned int frag_len = 1 << geth->freeq_frag_order;
	unsigned int len = 1 << geth->freeq_order;
	unsigned int pages = len >> fpp_order;
	union queue_threshold qt;
	union dma_skb_size skbsz;
	unsigned int filled;
	unsigned int pn;

	geth->freeq_ring = dma_alloc_coherent(geth->dev,
		sizeof(*geth->freeq_ring) << geth->freeq_order,
		&geth->freeq_dma_base, GFP_KERNEL);
	if (!geth->freeq_ring)
		return -ENOMEM;
	if (geth->freeq_dma_base & ~DMA_Q_BASE_MASK) {
		dev_warn(geth->dev, "queue ring base is not aligned\n");
		goto err_freeq;
	}

	/* Allocate a mapping to page look-up index */
	geth->freeq_pages = kcalloc(pages, sizeof(*geth->freeq_pages),
				    GFP_KERNEL);
	if (!geth->freeq_pages)
		goto err_freeq;
	geth->num_freeq_pages = pages;

	dev_info(geth->dev, "allocate %d pages for queue\n", pages);
	for (pn = 0; pn < pages; pn++)
		if (!geth_freeq_alloc_map_page(geth, pn))
			goto err_freeq_alloc;

	filled = geth_fill_freeq(geth, false);
	if (!filled)
		goto err_freeq_alloc;

	qt.bits32 = readl(geth->base + GLOBAL_QUEUE_THRESHOLD_REG);
	qt.bits.swfq_empty = 32;
	writel(qt.bits32, geth->base + GLOBAL_QUEUE_THRESHOLD_REG);

	skbsz.bits.sw_skb_size = 1 << geth->freeq_frag_order;
	writel(skbsz.bits32, geth->base + GLOBAL_DMA_SKB_SIZE_REG);
	writel(geth->freeq_dma_base | geth->freeq_order,
	       geth->base + GLOBAL_SW_FREEQ_BASE_SIZE_REG);

	return 0;

err_freeq_alloc:
	while (pn > 0) {
		struct gmac_queue_page *gpage;
		dma_addr_t mapping;

		--pn;
		mapping = geth->freeq_ring[pn << fpp_order].word2.buf_adr;
		dma_unmap_single(geth->dev, mapping, frag_len, DMA_FROM_DEVICE);
		gpage = &geth->freeq_pages[pn];
		put_page(gpage->page);
	}

	kfree(geth->freeq_pages);
err_freeq:
	dma_free_coherent(geth->dev,
			  sizeof(*geth->freeq_ring) << geth->freeq_order,
			  geth->freeq_ring, geth->freeq_dma_base);
	geth->freeq_ring = NULL;
	return -ENOMEM;
}

/**
 * geth_cleanup_freeq() - cleanup the DMA mappings and free the queue
 * @geth: the Gemini global ethernet state
 */
static void geth_cleanup_freeq(struct gemini_ethernet *geth)
{
	unsigned int fpp_order = PAGE_SHIFT - geth->freeq_frag_order;
	unsigned int frag_len = 1 << geth->freeq_frag_order;
	unsigned int len = 1 << geth->freeq_order;
	unsigned int pages = len >> fpp_order;
	unsigned int pn;

	writew(readw(geth->base + GLOBAL_SWFQ_RWPTR_REG),
	       geth->base + GLOBAL_SWFQ_RWPTR_REG + 2);
	writel(0, geth->base + GLOBAL_SW_FREEQ_BASE_SIZE_REG);

	for (pn = 0; pn < pages; pn++) {
		struct gmac_queue_page *gpage;
		dma_addr_t mapping;

		mapping = geth->freeq_ring[pn << fpp_order].word2.buf_adr;
		dma_unmap_single(geth->dev, mapping, frag_len, DMA_FROM_DEVICE);

		gpage = &geth->freeq_pages[pn];
		while (page_ref_count(gpage->page) > 0)
			put_page(gpage->page);
	}

	kfree(geth->freeq_pages);

	dma_free_coherent(geth->dev,
			  sizeof(*geth->freeq_ring) << geth->freeq_order,
			  geth->freeq_ring, geth->freeq_dma_base);
}

/**
 * geth_resize_freeq() - resize the software queue depth
 * @port: the port requesting the change
 *
 * This gets called at least once during probe() so the device queue gets
 * "resized" from the hardware defaults. Since both ports/net devices share
 * the same hardware queue, some synchronization between the ports is
 * needed.
 */
static int geth_resize_freeq(struct gemini_ethernet_port *port)
{
	struct gemini_ethernet *geth = port->geth;
	struct net_device *netdev = port->netdev;
	struct gemini_ethernet_port *other_port;
	struct net_device *other_netdev;
	unsigned int new_size = 0;
	unsigned int new_order;
	unsigned long flags;
	u32 en;
	int ret;

	if (netdev->dev_id == 0)
		other_netdev = geth->port1->netdev;
	else
		other_netdev = geth->port0->netdev;

	if (other_netdev && netif_running(other_netdev))
		return -EBUSY;

	new_size = 1 << (port->rxq_order + 1);
	netdev_dbg(netdev, "port %d size: %d order %d\n",
		   netdev->dev_id,
		   new_size,
		   port->rxq_order);
	if (other_netdev) {
		other_port = netdev_priv(other_netdev);
		new_size += 1 << (other_port->rxq_order + 1);
		netdev_dbg(other_netdev, "port %d size: %d order %d\n",
			   other_netdev->dev_id,
			   (1 << (other_port->rxq_order + 1)),
			   other_port->rxq_order);
	}

	new_order = min(15, ilog2(new_size - 1) + 1);
	dev_dbg(geth->dev, "set shared queue to size %d order %d\n",
		new_size, new_order);
	if (geth->freeq_order == new_order)
		return 0;

	spin_lock_irqsave(&geth->irq_lock, flags);

	/* Disable the software queue IRQs */
	en = readl(geth->base + GLOBAL_INTERRUPT_ENABLE_4_REG);
	en &= ~SWFQ_EMPTY_INT_BIT;
	writel(en, geth->base + GLOBAL_INTERRUPT_ENABLE_4_REG);
	spin_unlock_irqrestore(&geth->irq_lock, flags);

	/* Drop the old queue */
	if (geth->freeq_ring)
		geth_cleanup_freeq(geth);

	/* Allocate a new queue with the desired order */
	geth->freeq_order = new_order;
	ret = geth_setup_freeq(geth);

	/* Restart the interrupts - NOTE if this is the first resize
	 * after probe(), this is where the interrupts get turned on
	 * in the first place.
	 */
	spin_lock_irqsave(&geth->irq_lock, flags);
	en |= SWFQ_EMPTY_INT_BIT;
	writel(en, geth->base + GLOBAL_INTERRUPT_ENABLE_4_REG);
	spin_unlock_irqrestore(&geth->irq_lock, flags);

	return ret;
}

static void gmac_tx_irq_enable(struct net_device *netdev,
			       unsigned int txq, int en)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	struct gemini_ethernet *geth = port->geth;
	u32 val, mask;

	netdev_dbg(netdev, "%s device %d\n", __func__, netdev->dev_id);

	mask = GMAC0_IRQ0_TXQ0_INTS << (6 * netdev->dev_id + txq);

	if (en)
		writel(mask, geth->base + GLOBAL_INTERRUPT_STATUS_0_REG);

	val = readl(geth->base + GLOBAL_INTERRUPT_ENABLE_0_REG);
	val = en ? val | mask : val & ~mask;
	writel(val, geth->base + GLOBAL_INTERRUPT_ENABLE_0_REG);
}

static void gmac_tx_irq(struct net_device *netdev, unsigned int txq_num)
{
	struct netdev_queue *ntxq = netdev_get_tx_queue(netdev, txq_num);

	gmac_tx_irq_enable(netdev, txq_num, 0);
	netif_tx_wake_queue(ntxq);
}

static int gmac_map_tx_bufs(struct net_device *netdev, struct sk_buff *skb,
			    struct gmac_txq *txq, unsigned short *desc)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	struct skb_shared_info *skb_si =  skb_shinfo(skb);
	unsigned short m = (1 << port->txq_order) - 1;
	short frag, last_frag = skb_si->nr_frags - 1;
	struct gemini_ethernet *geth = port->geth;
	unsigned int word1, word3, buflen;
	unsigned short w = *desc;
	struct gmac_txdesc *txd;
	skb_frag_t *skb_frag;
	dma_addr_t mapping;
	unsigned short mtu;
	void *buffer;

	mtu  = ETH_HLEN;
	mtu += netdev->mtu;
	if (skb->protocol == htons(ETH_P_8021Q))
		mtu += VLAN_HLEN;

	word1 = skb->len;
	word3 = SOF_BIT;

	if (word1 > mtu) {
		word1 |= TSS_MTU_ENABLE_BIT;
		word3 |= mtu;
	}

	if (skb->ip_summed != CHECKSUM_NONE) {
		int tcp = 0;

		if (skb->protocol == htons(ETH_P_IP)) {
			word1 |= TSS_IP_CHKSUM_BIT;
			tcp = ip_hdr(skb)->protocol == IPPROTO_TCP;
		} else { /* IPv6 */
			word1 |= TSS_IPV6_ENABLE_BIT;
			tcp = ipv6_hdr(skb)->nexthdr == IPPROTO_TCP;
		}

		word1 |= tcp ? TSS_TCP_CHKSUM_BIT : TSS_UDP_CHKSUM_BIT;
	}

	frag = -1;
	while (frag <= last_frag) {
		if (frag == -1) {
			buffer = skb->data;
			buflen = skb_headlen(skb);
		} else {
			skb_frag = skb_si->frags + frag;
			buffer = page_address(skb_frag_page(skb_frag)) +
				 skb_frag->page_offset;
			buflen = skb_frag->size;
		}

		if (frag == last_frag) {
			word3 |= EOF_BIT;
			txq->skb[w] = skb;
		}

		mapping = dma_map_single(geth->dev, buffer, buflen,
					 DMA_TO_DEVICE);
		if (dma_mapping_error(geth->dev, mapping))
			goto map_error;

		txd = txq->ring + w;
		txd->word0.bits32 = buflen;
		txd->word1.bits32 = word1;
		txd->word2.buf_adr = mapping;
		txd->word3.bits32 = word3;

		word3 &= MTU_SIZE_BIT_MASK;
		w++;
		w &= m;
		frag++;
	}

	*desc = w;
	return 0;

map_error:
	while (w != *desc) {
		w--;
		w &= m;

		dma_unmap_page(geth->dev, txq->ring[w].word2.buf_adr,
			       txq->ring[w].word0.bits.buffer_size,
			       DMA_TO_DEVICE);
	}
	return -ENOMEM;
}

static int gmac_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	unsigned short m = (1 << port->txq_order) - 1;
	struct netdev_queue *ntxq;
	unsigned short r, w, d;
	void __iomem *ptr_reg;
	struct gmac_txq *txq;
	int txq_num, nfrags;
	union dma_rwptr rw;

	SKB_FRAG_ASSERT(skb);

	if (skb->len >= 0x10000)
		goto out_drop_free;

	txq_num = skb_get_queue_mapping(skb);
	ptr_reg = port->dma_base + GMAC_SW_TX_QUEUE_PTR_REG(txq_num);
	txq = &port->txq[txq_num];
	ntxq = netdev_get_tx_queue(netdev, txq_num);
	nfrags = skb_shinfo(skb)->nr_frags;

	rw.bits32 = readl(ptr_reg);
	r = rw.bits.rptr;
	w = rw.bits.wptr;

	d = txq->cptr - w - 1;
	d &= m;

	if (d < nfrags + 2) {
		gmac_clean_txq(netdev, txq, r);
		d = txq->cptr - w - 1;
		d &= m;

		if (d < nfrags + 2) {
			netif_tx_stop_queue(ntxq);

			d = txq->cptr + nfrags + 16;
			d &= m;
			txq->ring[d].word3.bits.eofie = 1;
			gmac_tx_irq_enable(netdev, txq_num, 1);

			u64_stats_update_begin(&port->tx_stats_syncp);
			netdev->stats.tx_fifo_errors++;
			u64_stats_update_end(&port->tx_stats_syncp);
			return NETDEV_TX_BUSY;
		}
	}

	if (gmac_map_tx_bufs(netdev, skb, txq, &w)) {
		if (skb_linearize(skb))
			goto out_drop;

		u64_stats_update_begin(&port->tx_stats_syncp);
		port->tx_frags_linearized++;
		u64_stats_update_end(&port->tx_stats_syncp);

		if (gmac_map_tx_bufs(netdev, skb, txq, &w))
			goto out_drop_free;
	}

	writew(w, ptr_reg + 2);

	gmac_clean_txq(netdev, txq, r);
	return NETDEV_TX_OK;

out_drop_free:
	dev_kfree_skb(skb);
out_drop:
	u64_stats_update_begin(&port->tx_stats_syncp);
	port->stats.tx_dropped++;
	u64_stats_update_end(&port->tx_stats_syncp);
	return NETDEV_TX_OK;
}

static void gmac_tx_timeout(struct net_device *netdev)
{
	netdev_err(netdev, "Tx timeout\n");
	gmac_dump_dma_state(netdev);
}

static void gmac_enable_irq(struct net_device *netdev, int enable)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	struct gemini_ethernet *geth = port->geth;
	unsigned long flags;
	u32 val, mask;

	netdev_dbg(netdev, "%s device %d %s\n", __func__,
		   netdev->dev_id, enable ? "enable" : "disable");
	spin_lock_irqsave(&geth->irq_lock, flags);

	mask = GMAC0_IRQ0_2 << (netdev->dev_id * 2);
	val = readl(geth->base + GLOBAL_INTERRUPT_ENABLE_0_REG);
	val = enable ? (val | mask) : (val & ~mask);
	writel(val, geth->base + GLOBAL_INTERRUPT_ENABLE_0_REG);

	mask = DEFAULT_Q0_INT_BIT << netdev->dev_id;
	val = readl(geth->base + GLOBAL_INTERRUPT_ENABLE_1_REG);
	val = enable ? (val | mask) : (val & ~mask);
	writel(val, geth->base + GLOBAL_INTERRUPT_ENABLE_1_REG);

	mask = GMAC0_IRQ4_8 << (netdev->dev_id * 8);
	val = readl(geth->base + GLOBAL_INTERRUPT_ENABLE_4_REG);
	val = enable ? (val | mask) : (val & ~mask);
	writel(val, geth->base + GLOBAL_INTERRUPT_ENABLE_4_REG);

	spin_unlock_irqrestore(&geth->irq_lock, flags);
}

static void gmac_enable_rx_irq(struct net_device *netdev, int enable)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	struct gemini_ethernet *geth = port->geth;
	unsigned long flags;
	u32 val, mask;

	netdev_dbg(netdev, "%s device %d %s\n", __func__, netdev->dev_id,
		   enable ? "enable" : "disable");
	spin_lock_irqsave(&geth->irq_lock, flags);
	mask = DEFAULT_Q0_INT_BIT << netdev->dev_id;

	val = readl(geth->base + GLOBAL_INTERRUPT_ENABLE_1_REG);
	val = enable ? (val | mask) : (val & ~mask);
	writel(val, geth->base + GLOBAL_INTERRUPT_ENABLE_1_REG);

	spin_unlock_irqrestore(&geth->irq_lock, flags);
}

static struct sk_buff *gmac_skb_if_good_frame(struct gemini_ethernet_port *port,
					      union gmac_rxdesc_0 word0,
					      unsigned int frame_len)
{
	unsigned int rx_csum = word0.bits.chksum_status;
	unsigned int rx_status = word0.bits.status;
	struct sk_buff *skb = NULL;

	port->rx_stats[rx_status]++;
	port->rx_csum_stats[rx_csum]++;

	if (word0.bits.derr || word0.bits.perr ||
	    rx_status || frame_len < ETH_ZLEN ||
	    rx_csum >= RX_CHKSUM_IP_ERR_UNKNOWN) {
		port->stats.rx_errors++;

		if (frame_len < ETH_ZLEN || RX_ERROR_LENGTH(rx_status))
			port->stats.rx_length_errors++;
		if (RX_ERROR_OVER(rx_status))
			port->stats.rx_over_errors++;
		if (RX_ERROR_CRC(rx_status))
			port->stats.rx_crc_errors++;
		if (RX_ERROR_FRAME(rx_status))
			port->stats.rx_frame_errors++;
		return NULL;
	}

	skb = napi_get_frags(&port->napi);
	if (!skb)
		goto update_exit;

	if (rx_csum == RX_CHKSUM_IP_UDP_TCP_OK)
		skb->ip_summed = CHECKSUM_UNNECESSARY;

update_exit:
	port->stats.rx_bytes += frame_len;
	port->stats.rx_packets++;
	return skb;
}

static unsigned int gmac_rx(struct net_device *netdev, unsigned int budget)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	unsigned short m = (1 << port->rxq_order) - 1;
	struct gemini_ethernet *geth = port->geth;
	void __iomem *ptr_reg = port->rxq_rwptr;
	unsigned int frame_len, frag_len;
	struct gmac_rxdesc *rx = NULL;
	struct gmac_queue_page *gpage;
	static struct sk_buff *skb;
	union gmac_rxdesc_0 word0;
	union gmac_rxdesc_1 word1;
	union gmac_rxdesc_3 word3;
	struct page *page = NULL;
	unsigned int page_offs;
	unsigned short r, w;
	union dma_rwptr rw;
	dma_addr_t mapping;
	int frag_nr = 0;

	rw.bits32 = readl(ptr_reg);
	/* Reset interrupt as all packages until here are taken into account */
	writel(DEFAULT_Q0_INT_BIT << netdev->dev_id,
	       geth->base + GLOBAL_INTERRUPT_STATUS_1_REG);
	r = rw.bits.rptr;
	w = rw.bits.wptr;

	while (budget && w != r) {
		rx = port->rxq_ring + r;
		word0 = rx->word0;
		word1 = rx->word1;
		mapping = rx->word2.buf_adr;
		word3 = rx->word3;

		r++;
		r &= m;

		frag_len = word0.bits.buffer_size;
		frame_len = word1.bits.byte_count;
		page_offs = mapping & ~PAGE_MASK;

		if (!mapping) {
			netdev_err(netdev,
				   "rxq[%u]: HW BUG: zero DMA desc\n", r);
			goto err_drop;
		}

		/* Freeq pointers are one page off */
		gpage = gmac_get_queue_page(geth, port, mapping + PAGE_SIZE);
		if (!gpage) {
			dev_err(geth->dev, "could not find mapping\n");
			continue;
		}
		page = gpage->page;

		if (word3.bits32 & SOF_BIT) {
			if (skb) {
				napi_free_frags(&port->napi);
				port->stats.rx_dropped++;
			}

			skb = gmac_skb_if_good_frame(port, word0, frame_len);
			if (!skb)
				goto err_drop;

			page_offs += NET_IP_ALIGN;
			frag_len -= NET_IP_ALIGN;
			frag_nr = 0;

		} else if (!skb) {
			put_page(page);
			continue;
		}

		if (word3.bits32 & EOF_BIT)
			frag_len = frame_len - skb->len;

		/* append page frag to skb */
		if (frag_nr == MAX_SKB_FRAGS)
			goto err_drop;

		if (frag_len == 0)
			netdev_err(netdev, "Received fragment with len = 0\n");

		skb_fill_page_desc(skb, frag_nr, page, page_offs, frag_len);
		skb->len += frag_len;
		skb->data_len += frag_len;
		skb->truesize += frag_len;
		frag_nr++;

		if (word3.bits32 & EOF_BIT) {
			napi_gro_frags(&port->napi);
			skb = NULL;
			--budget;
		}
		continue;

err_drop:
		if (skb) {
			napi_free_frags(&port->napi);
			skb = NULL;
		}

		if (mapping)
			put_page(page);

		port->stats.rx_dropped++;
	}

	writew(r, ptr_reg);
	return budget;
}

static int gmac_napi_poll(struct napi_struct *napi, int budget)
{
	struct gemini_ethernet_port *port = netdev_priv(napi->dev);
	struct gemini_ethernet *geth = port->geth;
	unsigned int freeq_threshold;
	unsigned int received;

	freeq_threshold = 1 << (geth->freeq_order - 1);
	u64_stats_update_begin(&port->rx_stats_syncp);

	received = gmac_rx(napi->dev, budget);
	if (received < budget) {
		napi_gro_flush(napi, false);
		napi_complete_done(napi, received);
		gmac_enable_rx_irq(napi->dev, 1);
		++port->rx_napi_exits;
	}

	port->freeq_refill += (budget - received);
	if (port->freeq_refill > freeq_threshold) {
		port->freeq_refill -= freeq_threshold;
		geth_fill_freeq(geth, true);
	}

	u64_stats_update_end(&port->rx_stats_syncp);
	return received;
}

static void gmac_dump_dma_state(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	struct gemini_ethernet *geth = port->geth;
	void __iomem *ptr_reg;
	u32 reg[5];

	/* Interrupt status */
	reg[0] = readl(geth->base + GLOBAL_INTERRUPT_STATUS_0_REG);
	reg[1] = readl(geth->base + GLOBAL_INTERRUPT_STATUS_1_REG);
	reg[2] = readl(geth->base + GLOBAL_INTERRUPT_STATUS_2_REG);
	reg[3] = readl(geth->base + GLOBAL_INTERRUPT_STATUS_3_REG);
	reg[4] = readl(geth->base + GLOBAL_INTERRUPT_STATUS_4_REG);
	netdev_err(netdev, "IRQ status: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
		   reg[0], reg[1], reg[2], reg[3], reg[4]);

	/* Interrupt enable */
	reg[0] = readl(geth->base + GLOBAL_INTERRUPT_ENABLE_0_REG);
	reg[1] = readl(geth->base + GLOBAL_INTERRUPT_ENABLE_1_REG);
	reg[2] = readl(geth->base + GLOBAL_INTERRUPT_ENABLE_2_REG);
	reg[3] = readl(geth->base + GLOBAL_INTERRUPT_ENABLE_3_REG);
	reg[4] = readl(geth->base + GLOBAL_INTERRUPT_ENABLE_4_REG);
	netdev_err(netdev, "IRQ enable: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
		   reg[0], reg[1], reg[2], reg[3], reg[4]);

	/* RX DMA status */
	reg[0] = readl(port->dma_base + GMAC_DMA_RX_FIRST_DESC_REG);
	reg[1] = readl(port->dma_base + GMAC_DMA_RX_CURR_DESC_REG);
	reg[2] = GET_RPTR(port->rxq_rwptr);
	reg[3] = GET_WPTR(port->rxq_rwptr);
	netdev_err(netdev, "RX DMA regs: 0x%08x 0x%08x, ptr: %u %u\n",
		   reg[0], reg[1], reg[2], reg[3]);

	reg[0] = readl(port->dma_base + GMAC_DMA_RX_DESC_WORD0_REG);
	reg[1] = readl(port->dma_base + GMAC_DMA_RX_DESC_WORD1_REG);
	reg[2] = readl(port->dma_base + GMAC_DMA_RX_DESC_WORD2_REG);
	reg[3] = readl(port->dma_base + GMAC_DMA_RX_DESC_WORD3_REG);
	netdev_err(netdev, "RX DMA descriptor: 0x%08x 0x%08x 0x%08x 0x%08x\n",
		   reg[0], reg[1], reg[2], reg[3]);

	/* TX DMA status */
	ptr_reg = port->dma_base + GMAC_SW_TX_QUEUE0_PTR_REG;

	reg[0] = readl(port->dma_base + GMAC_DMA_TX_FIRST_DESC_REG);
	reg[1] = readl(port->dma_base + GMAC_DMA_TX_CURR_DESC_REG);
	reg[2] = GET_RPTR(ptr_reg);
	reg[3] = GET_WPTR(ptr_reg);
	netdev_err(netdev, "TX DMA regs: 0x%08x 0x%08x, ptr: %u %u\n",
		   reg[0], reg[1], reg[2], reg[3]);

	reg[0] = readl(port->dma_base + GMAC_DMA_TX_DESC_WORD0_REG);
	reg[1] = readl(port->dma_base + GMAC_DMA_TX_DESC_WORD1_REG);
	reg[2] = readl(port->dma_base + GMAC_DMA_TX_DESC_WORD2_REG);
	reg[3] = readl(port->dma_base + GMAC_DMA_TX_DESC_WORD3_REG);
	netdev_err(netdev, "TX DMA descriptor: 0x%08x 0x%08x 0x%08x 0x%08x\n",
		   reg[0], reg[1], reg[2], reg[3]);

	/* FREE queues status */
	ptr_reg = geth->base + GLOBAL_SWFQ_RWPTR_REG;

	reg[0] = GET_RPTR(ptr_reg);
	reg[1] = GET_WPTR(ptr_reg);

	ptr_reg = geth->base + GLOBAL_HWFQ_RWPTR_REG;

	reg[2] = GET_RPTR(ptr_reg);
	reg[3] = GET_WPTR(ptr_reg);
	netdev_err(netdev, "FQ SW ptr: %u %u, HW ptr: %u %u\n",
		   reg[0], reg[1], reg[2], reg[3]);
}

static void gmac_update_hw_stats(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	unsigned int rx_discards, rx_mcast, rx_bcast;
	struct gemini_ethernet *geth = port->geth;
	unsigned long flags;

	spin_lock_irqsave(&geth->irq_lock, flags);
	u64_stats_update_begin(&port->ir_stats_syncp);

	rx_discards = readl(port->gmac_base + GMAC_IN_DISCARDS);
	port->hw_stats[0] += rx_discards;
	port->hw_stats[1] += readl(port->gmac_base + GMAC_IN_ERRORS);
	rx_mcast = readl(port->gmac_base + GMAC_IN_MCAST);
	port->hw_stats[2] += rx_mcast;
	rx_bcast = readl(port->gmac_base + GMAC_IN_BCAST);
	port->hw_stats[3] += rx_bcast;
	port->hw_stats[4] += readl(port->gmac_base + GMAC_IN_MAC1);
	port->hw_stats[5] += readl(port->gmac_base + GMAC_IN_MAC2);

	port->stats.rx_missed_errors += rx_discards;
	port->stats.multicast += rx_mcast;
	port->stats.multicast += rx_bcast;

	writel(GMAC0_MIB_INT_BIT << (netdev->dev_id * 8),
	       geth->base + GLOBAL_INTERRUPT_STATUS_4_REG);

	u64_stats_update_end(&port->ir_stats_syncp);
	spin_unlock_irqrestore(&geth->irq_lock, flags);
}

/**
 * gmac_get_intr_flags() - get interrupt status flags for a port from
 * @netdev: the net device for the port to get flags from
 * @i: the interrupt status register 0..4
 */
static u32 gmac_get_intr_flags(struct net_device *netdev, int i)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	struct gemini_ethernet *geth = port->geth;
	void __iomem *irqif_reg, *irqen_reg;
	unsigned int offs, val;

	/* Calculate the offset using the stride of the status registers */
	offs = i * (GLOBAL_INTERRUPT_STATUS_1_REG -
		    GLOBAL_INTERRUPT_STATUS_0_REG);

	irqif_reg = geth->base + GLOBAL_INTERRUPT_STATUS_0_REG + offs;
	irqen_reg = geth->base + GLOBAL_INTERRUPT_ENABLE_0_REG + offs;

	val = readl(irqif_reg) & readl(irqen_reg);
	return val;
}

static enum hrtimer_restart gmac_coalesce_delay_expired(struct hrtimer *timer)
{
	struct gemini_ethernet_port *port =
		container_of(timer, struct gemini_ethernet_port,
			     rx_coalesce_timer);

	napi_schedule(&port->napi);
	return HRTIMER_NORESTART;
}

static irqreturn_t gmac_irq(int irq, void *data)
{
	struct gemini_ethernet_port *port;
	struct net_device *netdev = data;
	struct gemini_ethernet *geth;
	u32 val, orr = 0;

	port = netdev_priv(netdev);
	geth = port->geth;

	val = gmac_get_intr_flags(netdev, 0);
	orr |= val;

	if (val & (GMAC0_IRQ0_2 << (netdev->dev_id * 2))) {
		/* Oh, crap */
		netdev_err(netdev, "hw failure/sw bug\n");
		gmac_dump_dma_state(netdev);

		/* don't know how to recover, just reduce losses */
		gmac_enable_irq(netdev, 0);
		return IRQ_HANDLED;
	}

	if (val & (GMAC0_IRQ0_TXQ0_INTS << (netdev->dev_id * 6)))
		gmac_tx_irq(netdev, 0);

	val = gmac_get_intr_flags(netdev, 1);
	orr |= val;

	if (val & (DEFAULT_Q0_INT_BIT << netdev->dev_id)) {
		gmac_enable_rx_irq(netdev, 0);

		if (!port->rx_coalesce_nsecs) {
			napi_schedule(&port->napi);
		} else {
			ktime_t ktime;

			ktime = ktime_set(0, port->rx_coalesce_nsecs);
			hrtimer_start(&port->rx_coalesce_timer, ktime,
				      HRTIMER_MODE_REL);
		}
	}

	val = gmac_get_intr_flags(netdev, 4);
	orr |= val;

	if (val & (GMAC0_MIB_INT_BIT << (netdev->dev_id * 8)))
		gmac_update_hw_stats(netdev);

	if (val & (GMAC0_RX_OVERRUN_INT_BIT << (netdev->dev_id * 8))) {
		writel(GMAC0_RXDERR_INT_BIT << (netdev->dev_id * 8),
		       geth->base + GLOBAL_INTERRUPT_STATUS_4_REG);

		spin_lock(&geth->irq_lock);
		u64_stats_update_begin(&port->ir_stats_syncp);
		++port->stats.rx_fifo_errors;
		u64_stats_update_end(&port->ir_stats_syncp);
		spin_unlock(&geth->irq_lock);
	}

	return orr ? IRQ_HANDLED : IRQ_NONE;
}

static void gmac_start_dma(struct gemini_ethernet_port *port)
{
	void __iomem *dma_ctrl_reg = port->dma_base + GMAC_DMA_CTRL_REG;
	union gmac_dma_ctrl dma_ctrl;

	dma_ctrl.bits32 = readl(dma_ctrl_reg);
	dma_ctrl.bits.rd_enable = 1;
	dma_ctrl.bits.td_enable = 1;
	dma_ctrl.bits.loopback = 0;
	dma_ctrl.bits.drop_small_ack = 0;
	dma_ctrl.bits.rd_insert_bytes = NET_IP_ALIGN;
	dma_ctrl.bits.rd_prot = HPROT_DATA_CACHE | HPROT_PRIVILIGED;
	dma_ctrl.bits.rd_burst_size = HBURST_INCR8;
	dma_ctrl.bits.rd_bus = HSIZE_8;
	dma_ctrl.bits.td_prot = HPROT_DATA_CACHE;
	dma_ctrl.bits.td_burst_size = HBURST_INCR8;
	dma_ctrl.bits.td_bus = HSIZE_8;

	writel(dma_ctrl.bits32, dma_ctrl_reg);
}

static void gmac_stop_dma(struct gemini_ethernet_port *port)
{
	void __iomem *dma_ctrl_reg = port->dma_base + GMAC_DMA_CTRL_REG;
	union gmac_dma_ctrl dma_ctrl;

	dma_ctrl.bits32 = readl(dma_ctrl_reg);
	dma_ctrl.bits.rd_enable = 0;
	dma_ctrl.bits.td_enable = 0;
	writel(dma_ctrl.bits32, dma_ctrl_reg);
}

static int gmac_open(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	int err;

	if (!netdev->phydev) {
		err = gmac_setup_phy(netdev);
		if (err) {
			netif_err(port, ifup, netdev,
				  "PHY init failed: %d\n", err);
			return err;
		}
	}

	err = request_irq(netdev->irq, gmac_irq,
			  IRQF_SHARED, netdev->name, netdev);
	if (err) {
		netdev_err(netdev, "no IRQ\n");
		return err;
	}

	netif_carrier_off(netdev);
	phy_start(netdev->phydev);

	err = geth_resize_freeq(port);
	/* It's fine if it's just busy, the other port has set up
	 * the freeq in that case.
	 */
	if (err && (err != -EBUSY)) {
		netdev_err(netdev, "could not resize freeq\n");
		goto err_stop_phy;
	}

	err = gmac_setup_rxq(netdev);
	if (err) {
		netdev_err(netdev, "could not setup RXQ\n");
		goto err_stop_phy;
	}

	err = gmac_setup_txqs(netdev);
	if (err) {
		netdev_err(netdev, "could not setup TXQs\n");
		gmac_cleanup_rxq(netdev);
		goto err_stop_phy;
	}

	napi_enable(&port->napi);

	gmac_start_dma(port);
	gmac_enable_irq(netdev, 1);
	gmac_enable_tx_rx(netdev);
	netif_tx_start_all_queues(netdev);

	hrtimer_init(&port->rx_coalesce_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	port->rx_coalesce_timer.function = &gmac_coalesce_delay_expired;

	netdev_dbg(netdev, "opened\n");

	return 0;

err_stop_phy:
	phy_stop(netdev->phydev);
	free_irq(netdev->irq, netdev);
	return err;
}

static int gmac_stop(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);

	hrtimer_cancel(&port->rx_coalesce_timer);
	netif_tx_stop_all_queues(netdev);
	gmac_disable_tx_rx(netdev);
	gmac_stop_dma(port);
	napi_disable(&port->napi);

	gmac_enable_irq(netdev, 0);
	gmac_cleanup_rxq(netdev);
	gmac_cleanup_txqs(netdev);

	phy_stop(netdev->phydev);
	free_irq(netdev->irq, netdev);

	gmac_update_hw_stats(netdev);
	return 0;
}

static void gmac_set_rx_mode(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	union gmac_rx_fltr filter = { .bits = {
		.broadcast = 1,
		.multicast = 1,
		.unicast = 1,
	} };
	struct netdev_hw_addr *ha;
	unsigned int bit_nr;
	u32 mc_filter[2];

	mc_filter[1] = 0;
	mc_filter[0] = 0;

	if (netdev->flags & IFF_PROMISC) {
		filter.bits.error = 1;
		filter.bits.promiscuous = 1;
		mc_filter[1] = ~0;
		mc_filter[0] = ~0;
	} else if (netdev->flags & IFF_ALLMULTI) {
		mc_filter[1] = ~0;
		mc_filter[0] = ~0;
	} else {
		netdev_for_each_mc_addr(ha, netdev) {
			bit_nr = ~crc32_le(~0, ha->addr, ETH_ALEN) & 0x3f;
			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 0x1f);
		}
	}

	writel(mc_filter[0], port->gmac_base + GMAC_MCAST_FIL0);
	writel(mc_filter[1], port->gmac_base + GMAC_MCAST_FIL1);
	writel(filter.bits32, port->gmac_base + GMAC_RX_FLTR);
}

static void gmac_write_mac_address(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	__le32 addr[3];

	memset(addr, 0, sizeof(addr));
	memcpy(addr, netdev->dev_addr, ETH_ALEN);

	writel(le32_to_cpu(addr[0]), port->gmac_base + GMAC_STA_ADD0);
	writel(le32_to_cpu(addr[1]), port->gmac_base + GMAC_STA_ADD1);
	writel(le32_to_cpu(addr[2]), port->gmac_base + GMAC_STA_ADD2);
}

static int gmac_set_mac_address(struct net_device *netdev, void *addr)
{
	struct sockaddr *sa = addr;

	memcpy(netdev->dev_addr, sa->sa_data, ETH_ALEN);
	gmac_write_mac_address(netdev);

	return 0;
}

static void gmac_clear_hw_stats(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);

	readl(port->gmac_base + GMAC_IN_DISCARDS);
	readl(port->gmac_base + GMAC_IN_ERRORS);
	readl(port->gmac_base + GMAC_IN_MCAST);
	readl(port->gmac_base + GMAC_IN_BCAST);
	readl(port->gmac_base + GMAC_IN_MAC1);
	readl(port->gmac_base + GMAC_IN_MAC2);
}

static void gmac_get_stats64(struct net_device *netdev,
			     struct rtnl_link_stats64 *stats)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	unsigned int start;

	gmac_update_hw_stats(netdev);

	/* Racing with RX NAPI */
	do {
		start = u64_stats_fetch_begin(&port->rx_stats_syncp);

		stats->rx_packets = port->stats.rx_packets;
		stats->rx_bytes = port->stats.rx_bytes;
		stats->rx_errors = port->stats.rx_errors;
		stats->rx_dropped = port->stats.rx_dropped;

		stats->rx_length_errors = port->stats.rx_length_errors;
		stats->rx_over_errors = port->stats.rx_over_errors;
		stats->rx_crc_errors = port->stats.rx_crc_errors;
		stats->rx_frame_errors = port->stats.rx_frame_errors;

	} while (u64_stats_fetch_retry(&port->rx_stats_syncp, start));

	/* Racing with MIB and TX completion interrupts */
	do {
		start = u64_stats_fetch_begin(&port->ir_stats_syncp);

		stats->tx_errors = port->stats.tx_errors;
		stats->tx_packets = port->stats.tx_packets;
		stats->tx_bytes = port->stats.tx_bytes;

		stats->multicast = port->stats.multicast;
		stats->rx_missed_errors = port->stats.rx_missed_errors;
		stats->rx_fifo_errors = port->stats.rx_fifo_errors;

	} while (u64_stats_fetch_retry(&port->ir_stats_syncp, start));

	/* Racing with hard_start_xmit */
	do {
		start = u64_stats_fetch_begin(&port->tx_stats_syncp);

		stats->tx_dropped = port->stats.tx_dropped;

	} while (u64_stats_fetch_retry(&port->tx_stats_syncp, start));

	stats->rx_dropped += stats->rx_missed_errors;
}

static int gmac_change_mtu(struct net_device *netdev, int new_mtu)
{
	int max_len = gmac_pick_rx_max_len(new_mtu);

	if (max_len < 0)
		return -EINVAL;

	gmac_disable_tx_rx(netdev);

	netdev->mtu = new_mtu;
	gmac_update_config0_reg(netdev, max_len << CONFIG0_MAXLEN_SHIFT,
				CONFIG0_MAXLEN_MASK);

	netdev_update_features(netdev);

	gmac_enable_tx_rx(netdev);

	return 0;
}

static netdev_features_t gmac_fix_features(struct net_device *netdev,
					   netdev_features_t features)
{
	if (netdev->mtu + ETH_HLEN + VLAN_HLEN > MTU_SIZE_BIT_MASK)
		features &= ~GMAC_OFFLOAD_FEATURES;

	return features;
}

static int gmac_set_features(struct net_device *netdev,
			     netdev_features_t features)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	int enable = features & NETIF_F_RXCSUM;
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&port->config_lock, flags);

	reg = readl(port->gmac_base + GMAC_CONFIG0);
	reg = enable ? reg | CONFIG0_RX_CHKSUM : reg & ~CONFIG0_RX_CHKSUM;
	writel(reg, port->gmac_base + GMAC_CONFIG0);

	spin_unlock_irqrestore(&port->config_lock, flags);
	return 0;
}

static int gmac_get_sset_count(struct net_device *netdev, int sset)
{
	return sset == ETH_SS_STATS ? GMAC_STATS_NUM : 0;
}

static void gmac_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	if (stringset != ETH_SS_STATS)
		return;

	memcpy(data, gmac_stats_strings, sizeof(gmac_stats_strings));
}

static void gmac_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats *estats, u64 *values)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	unsigned int start;
	u64 *p;
	int i;

	gmac_update_hw_stats(netdev);

	/* Racing with MIB interrupt */
	do {
		p = values;
		start = u64_stats_fetch_begin(&port->ir_stats_syncp);

		for (i = 0; i < RX_STATS_NUM; i++)
			*p++ = port->hw_stats[i];

	} while (u64_stats_fetch_retry(&port->ir_stats_syncp, start));
	values = p;

	/* Racing with RX NAPI */
	do {
		p = values;
		start = u64_stats_fetch_begin(&port->rx_stats_syncp);

		for (i = 0; i < RX_STATUS_NUM; i++)
			*p++ = port->rx_stats[i];
		for (i = 0; i < RX_CHKSUM_NUM; i++)
			*p++ = port->rx_csum_stats[i];
		*p++ = port->rx_napi_exits;

	} while (u64_stats_fetch_retry(&port->rx_stats_syncp, start));
	values = p;

	/* Racing with TX start_xmit */
	do {
		p = values;
		start = u64_stats_fetch_begin(&port->tx_stats_syncp);

		for (i = 0; i < TX_MAX_FRAGS; i++) {
			*values++ = port->tx_frag_stats[i];
			port->tx_frag_stats[i] = 0;
		}
		*values++ = port->tx_frags_linearized;
		*values++ = port->tx_hw_csummed;

	} while (u64_stats_fetch_retry(&port->tx_stats_syncp, start));
}

static int gmac_get_ksettings(struct net_device *netdev,
			      struct ethtool_link_ksettings *cmd)
{
	if (!netdev->phydev)
		return -ENXIO;
	phy_ethtool_ksettings_get(netdev->phydev, cmd);

	return 0;
}

static int gmac_set_ksettings(struct net_device *netdev,
			      const struct ethtool_link_ksettings *cmd)
{
	if (!netdev->phydev)
		return -ENXIO;
	return phy_ethtool_ksettings_set(netdev->phydev, cmd);
}

static int gmac_nway_reset(struct net_device *netdev)
{
	if (!netdev->phydev)
		return -ENXIO;
	return phy_start_aneg(netdev->phydev);
}

static void gmac_get_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pparam)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	union gmac_config0 config0;

	config0.bits32 = readl(port->gmac_base + GMAC_CONFIG0);

	pparam->rx_pause = config0.bits.rx_fc_en;
	pparam->tx_pause = config0.bits.tx_fc_en;
	pparam->autoneg = true;
}

static void gmac_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *rp)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	union gmac_config0 config0;

	config0.bits32 = readl(port->gmac_base + GMAC_CONFIG0);

	rp->rx_max_pending = 1 << 15;
	rp->rx_mini_max_pending = 0;
	rp->rx_jumbo_max_pending = 0;
	rp->tx_max_pending = 1 << 15;

	rp->rx_pending = 1 << port->rxq_order;
	rp->rx_mini_pending = 0;
	rp->rx_jumbo_pending = 0;
	rp->tx_pending = 1 << port->txq_order;
}

static int gmac_set_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *rp)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);
	int err = 0;

	if (netif_running(netdev))
		return -EBUSY;

	if (rp->rx_pending) {
		port->rxq_order = min(15, ilog2(rp->rx_pending - 1) + 1);
		err = geth_resize_freeq(port);
	}
	if (rp->tx_pending) {
		port->txq_order = min(15, ilog2(rp->tx_pending - 1) + 1);
		port->irq_every_tx_packets = 1 << (port->txq_order - 2);
	}

	return err;
}

static int gmac_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ecmd)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);

	ecmd->rx_max_coalesced_frames = 1;
	ecmd->tx_max_coalesced_frames = port->irq_every_tx_packets;
	ecmd->rx_coalesce_usecs = port->rx_coalesce_nsecs / 1000;

	return 0;
}

static int gmac_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ecmd)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);

	if (ecmd->tx_max_coalesced_frames < 1)
		return -EINVAL;
	if (ecmd->tx_max_coalesced_frames >= 1 << port->txq_order)
		return -EINVAL;

	port->irq_every_tx_packets = ecmd->tx_max_coalesced_frames;
	port->rx_coalesce_nsecs = ecmd->rx_coalesce_usecs * 1000;

	return 0;
}

static u32 gmac_get_msglevel(struct net_device *netdev)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);

	return port->msg_enable;
}

static void gmac_set_msglevel(struct net_device *netdev, u32 level)
{
	struct gemini_ethernet_port *port = netdev_priv(netdev);

	port->msg_enable = level;
}

static void gmac_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *info)
{
	strcpy(info->driver,  DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	strcpy(info->bus_info, netdev->dev_id ? "1" : "0");
}

static const struct net_device_ops gmac_351x_ops = {
	.ndo_init		= gmac_init,
	.ndo_uninit		= gmac_uninit,
	.ndo_open		= gmac_open,
	.ndo_stop		= gmac_stop,
	.ndo_start_xmit		= gmac_start_xmit,
	.ndo_tx_timeout		= gmac_tx_timeout,
	.ndo_set_rx_mode	= gmac_set_rx_mode,
	.ndo_set_mac_address	= gmac_set_mac_address,
	.ndo_get_stats64	= gmac_get_stats64,
	.ndo_change_mtu		= gmac_change_mtu,
	.ndo_fix_features	= gmac_fix_features,
	.ndo_set_features	= gmac_set_features,
};

static const struct ethtool_ops gmac_351x_ethtool_ops = {
	.get_sset_count	= gmac_get_sset_count,
	.get_strings	= gmac_get_strings,
	.get_ethtool_stats = gmac_get_ethtool_stats,
	.get_link	= ethtool_op_get_link,
	.get_link_ksettings = gmac_get_ksettings,
	.set_link_ksettings = gmac_set_ksettings,
	.nway_reset	= gmac_nway_reset,
	.get_pauseparam	= gmac_get_pauseparam,
	.get_ringparam	= gmac_get_ringparam,
	.set_ringparam	= gmac_set_ringparam,
	.get_coalesce	= gmac_get_coalesce,
	.set_coalesce	= gmac_set_coalesce,
	.get_msglevel	= gmac_get_msglevel,
	.set_msglevel	= gmac_set_msglevel,
	.get_drvinfo	= gmac_get_drvinfo,
};

static irqreturn_t gemini_port_irq_thread(int irq, void *data)
{
	unsigned long irqmask = SWFQ_EMPTY_INT_BIT;
	struct gemini_ethernet_port *port = data;
	struct gemini_ethernet *geth;
	unsigned long flags;

	geth = port->geth;
	/* The queue is half empty so refill it */
	geth_fill_freeq(geth, true);

	spin_lock_irqsave(&geth->irq_lock, flags);
	/* ACK queue interrupt */
	writel(irqmask, geth->base + GLOBAL_INTERRUPT_STATUS_4_REG);
	/* Enable queue interrupt again */
	irqmask |= readl(geth->base + GLOBAL_INTERRUPT_ENABLE_4_REG);
	writel(irqmask, geth->base + GLOBAL_INTERRUPT_ENABLE_4_REG);
	spin_unlock_irqrestore(&geth->irq_lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t gemini_port_irq(int irq, void *data)
{
	struct gemini_ethernet_port *port = data;
	struct gemini_ethernet *geth;
	irqreturn_t ret = IRQ_NONE;
	u32 val, en;

	geth = port->geth;
	spin_lock(&geth->irq_lock);

	val = readl(geth->base + GLOBAL_INTERRUPT_STATUS_4_REG);
	en = readl(geth->base + GLOBAL_INTERRUPT_ENABLE_4_REG);

	if (val & en & SWFQ_EMPTY_INT_BIT) {
		/* Disable the queue empty interrupt while we work on
		 * processing the queue. Also disable overrun interrupts
		 * as there is not much we can do about it here.
		 */
		en &= ~(SWFQ_EMPTY_INT_BIT | GMAC0_RX_OVERRUN_INT_BIT
					   | GMAC1_RX_OVERRUN_INT_BIT);
		writel(en, geth->base + GLOBAL_INTERRUPT_ENABLE_4_REG);
		ret = IRQ_WAKE_THREAD;
	}

	spin_unlock(&geth->irq_lock);

	return ret;
}

static void gemini_port_remove(struct gemini_ethernet_port *port)
{
	if (port->netdev)
		unregister_netdev(port->netdev);
	clk_disable_unprepare(port->pclk);
	geth_cleanup_freeq(port->geth);
}

static void gemini_ethernet_init(struct gemini_ethernet *geth)
{
	/* Only do this once both ports are online */
	if (geth->initialized)
		return;
	if (geth->port0 && geth->port1)
		geth->initialized = true;
	else
		return;

	writel(0, geth->base + GLOBAL_INTERRUPT_ENABLE_0_REG);
	writel(0, geth->base + GLOBAL_INTERRUPT_ENABLE_1_REG);
	writel(0, geth->base + GLOBAL_INTERRUPT_ENABLE_2_REG);
	writel(0, geth->base + GLOBAL_INTERRUPT_ENABLE_3_REG);
	writel(0, geth->base + GLOBAL_INTERRUPT_ENABLE_4_REG);

	/* Interrupt config:
	 *
	 *	GMAC0 intr bits ------> int0 ----> eth0
	 *	GMAC1 intr bits ------> int1 ----> eth1
	 *	TOE intr -------------> int1 ----> eth1
	 *	Classification Intr --> int0 ----> eth0
	 *	Default Q0 -----------> int0 ----> eth0
	 *	Default Q1 -----------> int1 ----> eth1
	 *	FreeQ intr -----------> int1 ----> eth1
	 */
	writel(0xCCFC0FC0, geth->base + GLOBAL_INTERRUPT_SELECT_0_REG);
	writel(0x00F00002, geth->base + GLOBAL_INTERRUPT_SELECT_1_REG);
	writel(0xFFFFFFFF, geth->base + GLOBAL_INTERRUPT_SELECT_2_REG);
	writel(0xFFFFFFFF, geth->base + GLOBAL_INTERRUPT_SELECT_3_REG);
	writel(0xFF000003, geth->base + GLOBAL_INTERRUPT_SELECT_4_REG);

	/* edge-triggered interrupts packed to level-triggered one... */
	writel(~0, geth->base + GLOBAL_INTERRUPT_STATUS_0_REG);
	writel(~0, geth->base + GLOBAL_INTERRUPT_STATUS_1_REG);
	writel(~0, geth->base + GLOBAL_INTERRUPT_STATUS_2_REG);
	writel(~0, geth->base + GLOBAL_INTERRUPT_STATUS_3_REG);
	writel(~0, geth->base + GLOBAL_INTERRUPT_STATUS_4_REG);

	/* Set up queue */
	writel(0, geth->base + GLOBAL_SW_FREEQ_BASE_SIZE_REG);
	writel(0, geth->base + GLOBAL_HW_FREEQ_BASE_SIZE_REG);
	writel(0, geth->base + GLOBAL_SWFQ_RWPTR_REG);
	writel(0, geth->base + GLOBAL_HWFQ_RWPTR_REG);

	geth->freeq_frag_order = DEFAULT_RX_BUF_ORDER;
	/* This makes the queue resize on probe() so that we
	 * set up and enable the queue IRQ. FIXME: fragile.
	 */
	geth->freeq_order = 1;
}

static void gemini_port_save_mac_addr(struct gemini_ethernet_port *port)
{
	port->mac_addr[0] =
		cpu_to_le32(readl(port->gmac_base + GMAC_STA_ADD0));
	port->mac_addr[1] =
		cpu_to_le32(readl(port->gmac_base + GMAC_STA_ADD1));
	port->mac_addr[2] =
		cpu_to_le32(readl(port->gmac_base + GMAC_STA_ADD2));
}

static int gemini_ethernet_port_probe(struct platform_device *pdev)
{
	char *port_names[2] = { "ethernet0", "ethernet1" };
	struct gemini_ethernet_port *port;
	struct device *dev = &pdev->dev;
	struct gemini_ethernet *geth;
	struct net_device *netdev;
	struct resource *gmacres;
	struct resource *dmares;
	struct device *parent;
	unsigned int id;
	int irq;
	int ret;

	parent = dev->parent;
	geth = dev_get_drvdata(parent);

	if (!strcmp(dev_name(dev), "60008000.ethernet-port"))
		id = 0;
	else if (!strcmp(dev_name(dev), "6000c000.ethernet-port"))
		id = 1;
	else
		return -ENODEV;

	dev_info(dev, "probe %s ID %d\n", dev_name(dev), id);

	netdev = devm_alloc_etherdev_mqs(dev, sizeof(*port), TX_QUEUE_NUM, TX_QUEUE_NUM);
	if (!netdev) {
		dev_err(dev, "Can't allocate ethernet device #%d\n", id);
		return -ENOMEM;
	}

	port = netdev_priv(netdev);
	SET_NETDEV_DEV(netdev, dev);
	port->netdev = netdev;
	port->id = id;
	port->geth = geth;
	port->dev = dev;
	port->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);

	/* DMA memory */
	dmares = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!dmares) {
		dev_err(dev, "no DMA resource\n");
		return -ENODEV;
	}
	port->dma_base = devm_ioremap_resource(dev, dmares);
	if (IS_ERR(port->dma_base))
		return PTR_ERR(port->dma_base);

	/* GMAC config memory */
	gmacres = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!gmacres) {
		dev_err(dev, "no GMAC resource\n");
		return -ENODEV;
	}
	port->gmac_base = devm_ioremap_resource(dev, gmacres);
	if (IS_ERR(port->gmac_base))
		return PTR_ERR(port->gmac_base);

	/* Interrupt */
	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(dev, "no IRQ\n");
		return irq ? irq : -ENODEV;
	}
	port->irq = irq;

	/* Clock the port */
	port->pclk = devm_clk_get(dev, "PCLK");
	if (IS_ERR(port->pclk)) {
		dev_err(dev, "no PCLK\n");
		return PTR_ERR(port->pclk);
	}
	ret = clk_prepare_enable(port->pclk);
	if (ret)
		return ret;

	/* Maybe there is a nice ethernet address we should use */
	gemini_port_save_mac_addr(port);

	/* Reset the port */
	port->reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(port->reset)) {
		dev_err(dev, "no reset\n");
		clk_disable_unprepare(port->pclk);
		return PTR_ERR(port->reset);
	}
	reset_control_reset(port->reset);
	usleep_range(100, 500);

	/* Assign pointer in the main state container */
	if (!id)
		geth->port0 = port;
	else
		geth->port1 = port;

	/* This will just be done once both ports are up and reset */
	gemini_ethernet_init(geth);

	platform_set_drvdata(pdev, port);

	/* Set up and register the netdev */
	netdev->dev_id = port->id;
	netdev->irq = irq;
	netdev->netdev_ops = &gmac_351x_ops;
	netdev->ethtool_ops = &gmac_351x_ethtool_ops;

	spin_lock_init(&port->config_lock);
	gmac_clear_hw_stats(netdev);

	netdev->hw_features = GMAC_OFFLOAD_FEATURES;
	netdev->features |= GMAC_OFFLOAD_FEATURES | NETIF_F_GRO;
	/* We can handle jumbo frames up to 10236 bytes so, let's accept
	 * payloads of 10236 bytes minus VLAN and ethernet header
	 */
	netdev->min_mtu = ETH_MIN_MTU;
	netdev->max_mtu = 10236 - VLAN_ETH_HLEN;

	port->freeq_refill = 0;
	netif_napi_add(netdev, &port->napi, gmac_napi_poll,
		       DEFAULT_NAPI_WEIGHT);

	if (is_valid_ether_addr((void *)port->mac_addr)) {
		memcpy(netdev->dev_addr, port->mac_addr, ETH_ALEN);
	} else {
		dev_dbg(dev, "ethernet address 0x%08x%08x%08x invalid\n",
			port->mac_addr[0], port->mac_addr[1],
			port->mac_addr[2]);
		dev_info(dev, "using a random ethernet address\n");
		eth_random_addr(netdev->dev_addr);
	}
	gmac_write_mac_address(netdev);

	ret = devm_request_threaded_irq(port->dev,
					port->irq,
					gemini_port_irq,
					gemini_port_irq_thread,
					IRQF_SHARED,
					port_names[port->id],
					port);
	if (ret) {
		clk_disable_unprepare(port->pclk);
		return ret;
	}

	ret = register_netdev(netdev);
	if (!ret) {
		netdev_info(netdev,
			    "irq %d, DMA @ 0x%pap, GMAC @ 0x%pap\n",
			    port->irq, &dmares->start,
			    &gmacres->start);
		ret = gmac_setup_phy(netdev);
		if (ret)
			netdev_info(netdev,
				    "PHY init failed, deferring to ifup time\n");
		return 0;
	}

	port->netdev = NULL;
	return ret;
}

static int gemini_ethernet_port_remove(struct platform_device *pdev)
{
	struct gemini_ethernet_port *port = platform_get_drvdata(pdev);

	gemini_port_remove(port);
	return 0;
}

static const struct of_device_id gemini_ethernet_port_of_match[] = {
	{
		.compatible = "cortina,gemini-ethernet-port",
	},
	{},
};
MODULE_DEVICE_TABLE(of, gemini_ethernet_port_of_match);

static struct platform_driver gemini_ethernet_port_driver = {
	.driver = {
		.name = "gemini-ethernet-port",
		.of_match_table = of_match_ptr(gemini_ethernet_port_of_match),
	},
	.probe = gemini_ethernet_port_probe,
	.remove = gemini_ethernet_port_remove,
};

static int gemini_ethernet_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gemini_ethernet *geth;
	unsigned int retry = 5;
	struct resource *res;
	u32 val;

	/* Global registers */
	geth = devm_kzalloc(dev, sizeof(*geth), GFP_KERNEL);
	if (!geth)
		return -ENOMEM;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	geth->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(geth->base))
		return PTR_ERR(geth->base);
	geth->dev = dev;

	/* Wait for ports to stabilize */
	do {
		udelay(2);
		val = readl(geth->base + GLOBAL_TOE_VERSION_REG);
		barrier();
	} while (!val && --retry);
	if (!retry) {
		dev_err(dev, "failed to reset ethernet\n");
		return -EIO;
	}
	dev_info(dev, "Ethernet device ID: 0x%03x, revision 0x%01x\n",
		 (val >> 4) & 0xFFFU, val & 0xFU);

	spin_lock_init(&geth->irq_lock);
	spin_lock_init(&geth->freeq_lock);

	/* The children will use this */
	platform_set_drvdata(pdev, geth);

	/* Spawn child devices for the two ports */
	return devm_of_platform_populate(dev);
}

static int gemini_ethernet_remove(struct platform_device *pdev)
{
	struct gemini_ethernet *geth = platform_get_drvdata(pdev);

	geth_cleanup_freeq(geth);
	geth->initialized = false;

	return 0;
}

static const struct of_device_id gemini_ethernet_of_match[] = {
	{
		.compatible = "cortina,gemini-ethernet",
	},
	{},
};
MODULE_DEVICE_TABLE(of, gemini_ethernet_of_match);

static struct platform_driver gemini_ethernet_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(gemini_ethernet_of_match),
	},
	.probe = gemini_ethernet_probe,
	.remove = gemini_ethernet_remove,
};

static int __init gemini_ethernet_module_init(void)
{
	int ret;

	ret = platform_driver_register(&gemini_ethernet_port_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&gemini_ethernet_driver);
	if (ret) {
		platform_driver_unregister(&gemini_ethernet_port_driver);
		return ret;
	}

	return 0;
}
module_init(gemini_ethernet_module_init);

static void __exit gemini_ethernet_module_exit(void)
{
	platform_driver_unregister(&gemini_ethernet_driver);
	platform_driver_unregister(&gemini_ethernet_port_driver);
}
module_exit(gemini_ethernet_module_exit);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("StorLink SL351x (Gemini) ethernet driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
