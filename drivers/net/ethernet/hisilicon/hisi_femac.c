// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hisilicon Fast Ethernet MAC Driver
 *
 * Copyright (c) 2016 HiSilicon Technologies Co., Ltd.
 */

#include <linux/circ_buf.h>
#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

/* MAC control register list */
#define MAC_PORTSEL			0x0200
#define MAC_PORTSEL_STAT_CPU		BIT(0)
#define MAC_PORTSEL_RMII		BIT(1)
#define MAC_PORTSET			0x0208
#define MAC_PORTSET_DUPLEX_FULL		BIT(0)
#define MAC_PORTSET_LINKED		BIT(1)
#define MAC_PORTSET_SPEED_100M		BIT(2)
#define MAC_SET				0x0210
#define MAX_FRAME_SIZE			1600
#define MAX_FRAME_SIZE_MASK		GENMASK(10, 0)
#define BIT_PAUSE_EN			BIT(18)
#define RX_COALESCE_SET			0x0340
#define RX_COALESCED_FRAME_OFFSET	24
#define RX_COALESCED_FRAMES		8
#define RX_COALESCED_TIMER		0x74
#define QLEN_SET			0x0344
#define RX_DEPTH_OFFSET			8
#define MAX_HW_FIFO_DEPTH		64
#define HW_TX_FIFO_DEPTH		12
#define HW_RX_FIFO_DEPTH		(MAX_HW_FIFO_DEPTH - HW_TX_FIFO_DEPTH)
#define IQFRM_DES			0x0354
#define RX_FRAME_LEN_MASK		GENMASK(11, 0)
#define IQ_ADDR				0x0358
#define EQ_ADDR				0x0360
#define EQFRM_LEN			0x0364
#define ADDRQ_STAT			0x036C
#define TX_CNT_INUSE_MASK		GENMASK(5, 0)
#define BIT_TX_READY			BIT(24)
#define BIT_RX_READY			BIT(25)
/* global control register list */
#define GLB_HOSTMAC_L32			0x0000
#define GLB_HOSTMAC_H16			0x0004
#define GLB_SOFT_RESET			0x0008
#define SOFT_RESET_ALL			BIT(0)
#define GLB_FWCTRL			0x0010
#define FWCTRL_VLAN_ENABLE		BIT(0)
#define FWCTRL_FW2CPU_ENA		BIT(5)
#define FWCTRL_FWALL2CPU		BIT(7)
#define GLB_MACTCTRL			0x0014
#define MACTCTRL_UNI2CPU		BIT(1)
#define MACTCTRL_MULTI2CPU		BIT(3)
#define MACTCTRL_BROAD2CPU		BIT(5)
#define MACTCTRL_MACT_ENA		BIT(7)
#define GLB_IRQ_STAT			0x0030
#define GLB_IRQ_ENA			0x0034
#define IRQ_ENA_PORT0_MASK		GENMASK(7, 0)
#define IRQ_ENA_PORT0			BIT(18)
#define IRQ_ENA_ALL			BIT(19)
#define GLB_IRQ_RAW			0x0038
#define IRQ_INT_RX_RDY			BIT(0)
#define IRQ_INT_TX_PER_PACKET		BIT(1)
#define IRQ_INT_TX_FIFO_EMPTY		BIT(6)
#define IRQ_INT_MULTI_RXRDY		BIT(7)
#define DEF_INT_MASK			(IRQ_INT_MULTI_RXRDY | \
					IRQ_INT_TX_PER_PACKET | \
					IRQ_INT_TX_FIFO_EMPTY)
#define GLB_MAC_L32_BASE		0x0100
#define GLB_MAC_H16_BASE		0x0104
#define MACFLT_HI16_MASK		GENMASK(15, 0)
#define BIT_MACFLT_ENA			BIT(17)
#define BIT_MACFLT_FW2CPU		BIT(21)
#define GLB_MAC_H16(reg)		(GLB_MAC_H16_BASE + ((reg) * 0x8))
#define GLB_MAC_L32(reg)		(GLB_MAC_L32_BASE + ((reg) * 0x8))
#define MAX_MAC_FILTER_NUM		8
#define MAX_UNICAST_ADDRESSES		2
#define MAX_MULTICAST_ADDRESSES		(MAX_MAC_FILTER_NUM - \
					MAX_UNICAST_ADDRESSES)
/* software tx and rx queue number, should be power of 2 */
#define TXQ_NUM				64
#define RXQ_NUM				128
#define FEMAC_POLL_WEIGHT		16

#define PHY_RESET_DELAYS_PROPERTY	"hisilicon,phy-reset-delays-us"

enum phy_reset_delays {
	PRE_DELAY,
	PULSE,
	POST_DELAY,
	DELAYS_NUM,
};

struct hisi_femac_queue {
	struct sk_buff **skb;
	dma_addr_t *dma_phys;
	int num;
	unsigned int head;
	unsigned int tail;
};

struct hisi_femac_priv {
	void __iomem *port_base;
	void __iomem *glb_base;
	struct clk *clk;
	struct reset_control *mac_rst;
	struct reset_control *phy_rst;
	u32 phy_reset_delays[DELAYS_NUM];
	u32 link_status;

	struct device *dev;
	struct net_device *ndev;

	struct hisi_femac_queue txq;
	struct hisi_femac_queue rxq;
	u32 tx_fifo_used_cnt;
	struct napi_struct napi;
};

static void hisi_femac_irq_enable(struct hisi_femac_priv *priv, int irqs)
{
	u32 val;

	val = readl(priv->glb_base + GLB_IRQ_ENA);
	writel(val | irqs, priv->glb_base + GLB_IRQ_ENA);
}

static void hisi_femac_irq_disable(struct hisi_femac_priv *priv, int irqs)
{
	u32 val;

	val = readl(priv->glb_base + GLB_IRQ_ENA);
	writel(val & (~irqs), priv->glb_base + GLB_IRQ_ENA);
}

static void hisi_femac_tx_dma_unmap(struct hisi_femac_priv *priv,
				    struct sk_buff *skb, unsigned int pos)
{
	dma_addr_t dma_addr;

	dma_addr = priv->txq.dma_phys[pos];
	dma_unmap_single(priv->dev, dma_addr, skb->len, DMA_TO_DEVICE);
}

static void hisi_femac_xmit_reclaim(struct net_device *dev)
{
	struct sk_buff *skb;
	struct hisi_femac_priv *priv = netdev_priv(dev);
	struct hisi_femac_queue *txq = &priv->txq;
	unsigned int bytes_compl = 0, pkts_compl = 0;
	u32 val;

	netif_tx_lock(dev);

	val = readl(priv->port_base + ADDRQ_STAT) & TX_CNT_INUSE_MASK;
	while (val < priv->tx_fifo_used_cnt) {
		skb = txq->skb[txq->tail];
		if (unlikely(!skb)) {
			netdev_err(dev, "xmitq_cnt_inuse=%d, tx_fifo_used=%d\n",
				   val, priv->tx_fifo_used_cnt);
			break;
		}
		hisi_femac_tx_dma_unmap(priv, skb, txq->tail);
		pkts_compl++;
		bytes_compl += skb->len;
		dev_kfree_skb_any(skb);

		priv->tx_fifo_used_cnt--;

		val = readl(priv->port_base + ADDRQ_STAT) & TX_CNT_INUSE_MASK;
		txq->skb[txq->tail] = NULL;
		txq->tail = (txq->tail + 1) % txq->num;
	}

	netdev_completed_queue(dev, pkts_compl, bytes_compl);

	if (unlikely(netif_queue_stopped(dev)) && pkts_compl)
		netif_wake_queue(dev);

	netif_tx_unlock(dev);
}

static void hisi_femac_adjust_link(struct net_device *dev)
{
	struct hisi_femac_priv *priv = netdev_priv(dev);
	struct phy_device *phy = dev->phydev;
	u32 status = 0;

	if (phy->link)
		status |= MAC_PORTSET_LINKED;
	if (phy->duplex == DUPLEX_FULL)
		status |= MAC_PORTSET_DUPLEX_FULL;
	if (phy->speed == SPEED_100)
		status |= MAC_PORTSET_SPEED_100M;

	if ((status != priv->link_status) &&
	    ((status | priv->link_status) & MAC_PORTSET_LINKED)) {
		writel(status, priv->port_base + MAC_PORTSET);
		priv->link_status = status;
		phy_print_status(phy);
	}
}

static void hisi_femac_rx_refill(struct hisi_femac_priv *priv)
{
	struct hisi_femac_queue *rxq = &priv->rxq;
	struct sk_buff *skb;
	u32 pos;
	u32 len = MAX_FRAME_SIZE;
	dma_addr_t addr;

	pos = rxq->head;
	while (readl(priv->port_base + ADDRQ_STAT) & BIT_RX_READY) {
		if (!CIRC_SPACE(pos, rxq->tail, rxq->num))
			break;
		if (unlikely(rxq->skb[pos])) {
			netdev_err(priv->ndev, "err skb[%d]=%p\n",
				   pos, rxq->skb[pos]);
			break;
		}
		skb = netdev_alloc_skb_ip_align(priv->ndev, len);
		if (unlikely(!skb))
			break;

		addr = dma_map_single(priv->dev, skb->data, len,
				      DMA_FROM_DEVICE);
		if (dma_mapping_error(priv->dev, addr)) {
			dev_kfree_skb_any(skb);
			break;
		}
		rxq->dma_phys[pos] = addr;
		rxq->skb[pos] = skb;
		writel(addr, priv->port_base + IQ_ADDR);
		pos = (pos + 1) % rxq->num;
	}
	rxq->head = pos;
}

static int hisi_femac_rx(struct net_device *dev, int limit)
{
	struct hisi_femac_priv *priv = netdev_priv(dev);
	struct hisi_femac_queue *rxq = &priv->rxq;
	struct sk_buff *skb;
	dma_addr_t addr;
	u32 rx_pkt_info, pos, len, rx_pkts_num = 0;

	pos = rxq->tail;
	while (readl(priv->glb_base + GLB_IRQ_RAW) & IRQ_INT_RX_RDY) {
		rx_pkt_info = readl(priv->port_base + IQFRM_DES);
		len = rx_pkt_info & RX_FRAME_LEN_MASK;
		len -= ETH_FCS_LEN;

		/* tell hardware we will deal with this packet */
		writel(IRQ_INT_RX_RDY, priv->glb_base + GLB_IRQ_RAW);

		rx_pkts_num++;

		skb = rxq->skb[pos];
		if (unlikely(!skb)) {
			netdev_err(dev, "rx skb NULL. pos=%d\n", pos);
			break;
		}
		rxq->skb[pos] = NULL;

		addr = rxq->dma_phys[pos];
		dma_unmap_single(priv->dev, addr, MAX_FRAME_SIZE,
				 DMA_FROM_DEVICE);
		skb_put(skb, len);
		if (unlikely(skb->len > MAX_FRAME_SIZE)) {
			netdev_err(dev, "rcv len err, len = %d\n", skb->len);
			dev->stats.rx_errors++;
			dev->stats.rx_length_errors++;
			dev_kfree_skb_any(skb);
			goto next;
		}

		skb->protocol = eth_type_trans(skb, dev);
		napi_gro_receive(&priv->napi, skb);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += skb->len;
next:
		pos = (pos + 1) % rxq->num;
		if (rx_pkts_num >= limit)
			break;
	}
	rxq->tail = pos;

	hisi_femac_rx_refill(priv);

	return rx_pkts_num;
}

static int hisi_femac_poll(struct napi_struct *napi, int budget)
{
	struct hisi_femac_priv *priv = container_of(napi,
					struct hisi_femac_priv, napi);
	struct net_device *dev = priv->ndev;
	int work_done = 0, task = budget;
	int ints, num;

	do {
		hisi_femac_xmit_reclaim(dev);
		num = hisi_femac_rx(dev, task);
		work_done += num;
		task -= num;
		if (work_done >= budget)
			break;

		ints = readl(priv->glb_base + GLB_IRQ_RAW);
		writel(ints & DEF_INT_MASK,
		       priv->glb_base + GLB_IRQ_RAW);
	} while (ints & DEF_INT_MASK);

	if (work_done < budget) {
		napi_complete_done(napi, work_done);
		hisi_femac_irq_enable(priv, DEF_INT_MASK &
					(~IRQ_INT_TX_PER_PACKET));
	}

	return work_done;
}

static irqreturn_t hisi_femac_interrupt(int irq, void *dev_id)
{
	int ints;
	struct net_device *dev = (struct net_device *)dev_id;
	struct hisi_femac_priv *priv = netdev_priv(dev);

	ints = readl(priv->glb_base + GLB_IRQ_RAW);

	if (likely(ints & DEF_INT_MASK)) {
		writel(ints & DEF_INT_MASK,
		       priv->glb_base + GLB_IRQ_RAW);
		hisi_femac_irq_disable(priv, DEF_INT_MASK);
		napi_schedule(&priv->napi);
	}

	return IRQ_HANDLED;
}

static int hisi_femac_init_queue(struct device *dev,
				 struct hisi_femac_queue *queue,
				 unsigned int num)
{
	queue->skb = devm_kcalloc(dev, num, sizeof(struct sk_buff *),
				  GFP_KERNEL);
	if (!queue->skb)
		return -ENOMEM;

	queue->dma_phys = devm_kcalloc(dev, num, sizeof(dma_addr_t),
				       GFP_KERNEL);
	if (!queue->dma_phys)
		return -ENOMEM;

	queue->num = num;
	queue->head = 0;
	queue->tail = 0;

	return 0;
}

static int hisi_femac_init_tx_and_rx_queues(struct hisi_femac_priv *priv)
{
	int ret;

	ret = hisi_femac_init_queue(priv->dev, &priv->txq, TXQ_NUM);
	if (ret)
		return ret;

	ret = hisi_femac_init_queue(priv->dev, &priv->rxq, RXQ_NUM);
	if (ret)
		return ret;

	priv->tx_fifo_used_cnt = 0;

	return 0;
}

static void hisi_femac_free_skb_rings(struct hisi_femac_priv *priv)
{
	struct hisi_femac_queue *txq = &priv->txq;
	struct hisi_femac_queue *rxq = &priv->rxq;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	u32 pos;

	pos = rxq->tail;
	while (pos != rxq->head) {
		skb = rxq->skb[pos];
		if (unlikely(!skb)) {
			netdev_err(priv->ndev, "NULL rx skb. pos=%d, head=%d\n",
				   pos, rxq->head);
			continue;
		}

		dma_addr = rxq->dma_phys[pos];
		dma_unmap_single(priv->dev, dma_addr, MAX_FRAME_SIZE,
				 DMA_FROM_DEVICE);

		dev_kfree_skb_any(skb);
		rxq->skb[pos] = NULL;
		pos = (pos + 1) % rxq->num;
	}
	rxq->tail = pos;

	pos = txq->tail;
	while (pos != txq->head) {
		skb = txq->skb[pos];
		if (unlikely(!skb)) {
			netdev_err(priv->ndev, "NULL tx skb. pos=%d, head=%d\n",
				   pos, txq->head);
			continue;
		}
		hisi_femac_tx_dma_unmap(priv, skb, pos);
		dev_kfree_skb_any(skb);
		txq->skb[pos] = NULL;
		pos = (pos + 1) % txq->num;
	}
	txq->tail = pos;
	priv->tx_fifo_used_cnt = 0;
}

static int hisi_femac_set_hw_mac_addr(struct hisi_femac_priv *priv,
				      unsigned char *mac)
{
	u32 reg;

	reg = mac[1] | (mac[0] << 8);
	writel(reg, priv->glb_base + GLB_HOSTMAC_H16);

	reg = mac[5] | (mac[4] << 8) | (mac[3] << 16) | (mac[2] << 24);
	writel(reg, priv->glb_base + GLB_HOSTMAC_L32);

	return 0;
}

static int hisi_femac_port_reset(struct hisi_femac_priv *priv)
{
	u32 val;

	val = readl(priv->glb_base + GLB_SOFT_RESET);
	val |= SOFT_RESET_ALL;
	writel(val, priv->glb_base + GLB_SOFT_RESET);

	usleep_range(500, 800);

	val &= ~SOFT_RESET_ALL;
	writel(val, priv->glb_base + GLB_SOFT_RESET);

	return 0;
}

static int hisi_femac_net_open(struct net_device *dev)
{
	struct hisi_femac_priv *priv = netdev_priv(dev);

	hisi_femac_port_reset(priv);
	hisi_femac_set_hw_mac_addr(priv, dev->dev_addr);
	hisi_femac_rx_refill(priv);

	netif_carrier_off(dev);
	netdev_reset_queue(dev);
	netif_start_queue(dev);
	napi_enable(&priv->napi);

	priv->link_status = 0;
	if (dev->phydev)
		phy_start(dev->phydev);

	writel(IRQ_ENA_PORT0_MASK, priv->glb_base + GLB_IRQ_RAW);
	hisi_femac_irq_enable(priv, IRQ_ENA_ALL | IRQ_ENA_PORT0 | DEF_INT_MASK);

	return 0;
}

static int hisi_femac_net_close(struct net_device *dev)
{
	struct hisi_femac_priv *priv = netdev_priv(dev);

	hisi_femac_irq_disable(priv, IRQ_ENA_PORT0);

	if (dev->phydev)
		phy_stop(dev->phydev);

	netif_stop_queue(dev);
	napi_disable(&priv->napi);

	hisi_femac_free_skb_rings(priv);

	return 0;
}

static netdev_tx_t hisi_femac_net_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	struct hisi_femac_priv *priv = netdev_priv(dev);
	struct hisi_femac_queue *txq = &priv->txq;
	dma_addr_t addr;
	u32 val;

	val = readl(priv->port_base + ADDRQ_STAT);
	val &= BIT_TX_READY;
	if (!val) {
		hisi_femac_irq_enable(priv, IRQ_INT_TX_PER_PACKET);
		dev->stats.tx_dropped++;
		dev->stats.tx_fifo_errors++;
		netif_stop_queue(dev);
		return NETDEV_TX_BUSY;
	}

	if (unlikely(!CIRC_SPACE(txq->head, txq->tail,
				 txq->num))) {
		hisi_femac_irq_enable(priv, IRQ_INT_TX_PER_PACKET);
		dev->stats.tx_dropped++;
		dev->stats.tx_fifo_errors++;
		netif_stop_queue(dev);
		return NETDEV_TX_BUSY;
	}

	addr = dma_map_single(priv->dev, skb->data,
			      skb->len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(priv->dev, addr))) {
		dev_kfree_skb_any(skb);
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}
	txq->dma_phys[txq->head] = addr;

	txq->skb[txq->head] = skb;
	txq->head = (txq->head + 1) % txq->num;

	writel(addr, priv->port_base + EQ_ADDR);
	writel(skb->len + ETH_FCS_LEN, priv->port_base + EQFRM_LEN);

	priv->tx_fifo_used_cnt++;

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;
	netdev_sent_queue(dev, skb->len);

	return NETDEV_TX_OK;
}

static int hisi_femac_set_mac_address(struct net_device *dev, void *p)
{
	struct hisi_femac_priv *priv = netdev_priv(dev);
	struct sockaddr *skaddr = p;

	if (!is_valid_ether_addr(skaddr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, skaddr->sa_data, dev->addr_len);
	dev->addr_assign_type &= ~NET_ADDR_RANDOM;

	hisi_femac_set_hw_mac_addr(priv, dev->dev_addr);

	return 0;
}

static void hisi_femac_enable_hw_addr_filter(struct hisi_femac_priv *priv,
					     unsigned int reg_n, bool enable)
{
	u32 val;

	val = readl(priv->glb_base + GLB_MAC_H16(reg_n));
	if (enable)
		val |= BIT_MACFLT_ENA;
	else
		val &= ~BIT_MACFLT_ENA;
	writel(val, priv->glb_base + GLB_MAC_H16(reg_n));
}

static void hisi_femac_set_hw_addr_filter(struct hisi_femac_priv *priv,
					  unsigned char *addr,
					  unsigned int reg_n)
{
	unsigned int high, low;
	u32 val;

	high = GLB_MAC_H16(reg_n);
	low = GLB_MAC_L32(reg_n);

	val = (addr[2] << 24) | (addr[3] << 16) | (addr[4] << 8) | addr[5];
	writel(val, priv->glb_base + low);

	val = readl(priv->glb_base + high);
	val &= ~MACFLT_HI16_MASK;
	val |= ((addr[0] << 8) | addr[1]);
	val |= (BIT_MACFLT_ENA | BIT_MACFLT_FW2CPU);
	writel(val, priv->glb_base + high);
}

static void hisi_femac_set_promisc_mode(struct hisi_femac_priv *priv,
					bool promisc_mode)
{
	u32 val;

	val = readl(priv->glb_base + GLB_FWCTRL);
	if (promisc_mode)
		val |= FWCTRL_FWALL2CPU;
	else
		val &= ~FWCTRL_FWALL2CPU;
	writel(val, priv->glb_base + GLB_FWCTRL);
}

/* Handle multiple multicast addresses (perfect filtering)*/
static void hisi_femac_set_mc_addr_filter(struct hisi_femac_priv *priv)
{
	struct net_device *dev = priv->ndev;
	u32 val;

	val = readl(priv->glb_base + GLB_MACTCTRL);
	if ((netdev_mc_count(dev) > MAX_MULTICAST_ADDRESSES) ||
	    (dev->flags & IFF_ALLMULTI)) {
		val |= MACTCTRL_MULTI2CPU;
	} else {
		int reg = MAX_UNICAST_ADDRESSES;
		int i;
		struct netdev_hw_addr *ha;

		for (i = reg; i < MAX_MAC_FILTER_NUM; i++)
			hisi_femac_enable_hw_addr_filter(priv, i, false);

		netdev_for_each_mc_addr(ha, dev) {
			hisi_femac_set_hw_addr_filter(priv, ha->addr, reg);
			reg++;
		}
		val &= ~MACTCTRL_MULTI2CPU;
	}
	writel(val, priv->glb_base + GLB_MACTCTRL);
}

/* Handle multiple unicast addresses (perfect filtering)*/
static void hisi_femac_set_uc_addr_filter(struct hisi_femac_priv *priv)
{
	struct net_device *dev = priv->ndev;
	u32 val;

	val = readl(priv->glb_base + GLB_MACTCTRL);
	if (netdev_uc_count(dev) > MAX_UNICAST_ADDRESSES) {
		val |= MACTCTRL_UNI2CPU;
	} else {
		int reg = 0;
		int i;
		struct netdev_hw_addr *ha;

		for (i = reg; i < MAX_UNICAST_ADDRESSES; i++)
			hisi_femac_enable_hw_addr_filter(priv, i, false);

		netdev_for_each_uc_addr(ha, dev) {
			hisi_femac_set_hw_addr_filter(priv, ha->addr, reg);
			reg++;
		}
		val &= ~MACTCTRL_UNI2CPU;
	}
	writel(val, priv->glb_base + GLB_MACTCTRL);
}

static void hisi_femac_net_set_rx_mode(struct net_device *dev)
{
	struct hisi_femac_priv *priv = netdev_priv(dev);

	if (dev->flags & IFF_PROMISC) {
		hisi_femac_set_promisc_mode(priv, true);
	} else {
		hisi_femac_set_promisc_mode(priv, false);
		hisi_femac_set_mc_addr_filter(priv);
		hisi_femac_set_uc_addr_filter(priv);
	}
}

static const struct ethtool_ops hisi_femac_ethtools_ops = {
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
};

static const struct net_device_ops hisi_femac_netdev_ops = {
	.ndo_open		= hisi_femac_net_open,
	.ndo_stop		= hisi_femac_net_close,
	.ndo_start_xmit		= hisi_femac_net_xmit,
	.ndo_do_ioctl		= phy_do_ioctl_running,
	.ndo_set_mac_address	= hisi_femac_set_mac_address,
	.ndo_set_rx_mode	= hisi_femac_net_set_rx_mode,
};

static void hisi_femac_core_reset(struct hisi_femac_priv *priv)
{
	reset_control_assert(priv->mac_rst);
	reset_control_deassert(priv->mac_rst);
}

static void hisi_femac_sleep_us(u32 time_us)
{
	u32 time_ms;

	if (!time_us)
		return;

	time_ms = DIV_ROUND_UP(time_us, 1000);
	if (time_ms < 20)
		usleep_range(time_us, time_us + 500);
	else
		msleep(time_ms);
}

static void hisi_femac_phy_reset(struct hisi_femac_priv *priv)
{
	/* To make sure PHY hardware reset success,
	 * we must keep PHY in deassert state first and
	 * then complete the hardware reset operation
	 */
	reset_control_deassert(priv->phy_rst);
	hisi_femac_sleep_us(priv->phy_reset_delays[PRE_DELAY]);

	reset_control_assert(priv->phy_rst);
	/* delay some time to ensure reset ok,
	 * this depends on PHY hardware feature
	 */
	hisi_femac_sleep_us(priv->phy_reset_delays[PULSE]);
	reset_control_deassert(priv->phy_rst);
	/* delay some time to ensure later MDIO access */
	hisi_femac_sleep_us(priv->phy_reset_delays[POST_DELAY]);
}

static void hisi_femac_port_init(struct hisi_femac_priv *priv)
{
	u32 val;

	/* MAC gets link status info and phy mode by software config */
	val = MAC_PORTSEL_STAT_CPU;
	if (priv->ndev->phydev->interface == PHY_INTERFACE_MODE_RMII)
		val |= MAC_PORTSEL_RMII;
	writel(val, priv->port_base + MAC_PORTSEL);

	/*clear all interrupt status */
	writel(IRQ_ENA_PORT0_MASK, priv->glb_base + GLB_IRQ_RAW);
	hisi_femac_irq_disable(priv, IRQ_ENA_PORT0_MASK | IRQ_ENA_PORT0);

	val = readl(priv->glb_base + GLB_FWCTRL);
	val &= ~(FWCTRL_VLAN_ENABLE | FWCTRL_FWALL2CPU);
	val |= FWCTRL_FW2CPU_ENA;
	writel(val, priv->glb_base + GLB_FWCTRL);

	val = readl(priv->glb_base + GLB_MACTCTRL);
	val |= (MACTCTRL_BROAD2CPU | MACTCTRL_MACT_ENA);
	writel(val, priv->glb_base + GLB_MACTCTRL);

	val = readl(priv->port_base + MAC_SET);
	val &= ~MAX_FRAME_SIZE_MASK;
	val |= MAX_FRAME_SIZE;
	writel(val, priv->port_base + MAC_SET);

	val = RX_COALESCED_TIMER |
		(RX_COALESCED_FRAMES << RX_COALESCED_FRAME_OFFSET);
	writel(val, priv->port_base + RX_COALESCE_SET);

	val = (HW_RX_FIFO_DEPTH << RX_DEPTH_OFFSET) | HW_TX_FIFO_DEPTH;
	writel(val, priv->port_base + QLEN_SET);
}

static int hisi_femac_drv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct net_device *ndev;
	struct hisi_femac_priv *priv;
	struct phy_device *phy;
	const char *mac_addr;
	int ret;

	ndev = alloc_etherdev(sizeof(*priv));
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);

	priv = netdev_priv(ndev);
	priv->dev = dev;
	priv->ndev = ndev;

	priv->port_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->port_base)) {
		ret = PTR_ERR(priv->port_base);
		goto out_free_netdev;
	}

	priv->glb_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(priv->glb_base)) {
		ret = PTR_ERR(priv->glb_base);
		goto out_free_netdev;
	}

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get clk\n");
		ret = -ENODEV;
		goto out_free_netdev;
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "failed to enable clk %d\n", ret);
		goto out_free_netdev;
	}

	priv->mac_rst = devm_reset_control_get(dev, "mac");
	if (IS_ERR(priv->mac_rst)) {
		ret = PTR_ERR(priv->mac_rst);
		goto out_disable_clk;
	}
	hisi_femac_core_reset(priv);

	priv->phy_rst = devm_reset_control_get(dev, "phy");
	if (IS_ERR(priv->phy_rst)) {
		priv->phy_rst = NULL;
	} else {
		ret = of_property_read_u32_array(node,
						 PHY_RESET_DELAYS_PROPERTY,
						 priv->phy_reset_delays,
						 DELAYS_NUM);
		if (ret)
			goto out_disable_clk;
		hisi_femac_phy_reset(priv);
	}

	phy = of_phy_get_and_connect(ndev, node, hisi_femac_adjust_link);
	if (!phy) {
		dev_err(dev, "connect to PHY failed!\n");
		ret = -ENODEV;
		goto out_disable_clk;
	}

	phy_attached_print(phy, "phy_id=0x%.8lx, phy_mode=%s\n",
			   (unsigned long)phy->phy_id,
			   phy_modes(phy->interface));

	mac_addr = of_get_mac_address(node);
	if (!IS_ERR(mac_addr))
		ether_addr_copy(ndev->dev_addr, mac_addr);
	if (!is_valid_ether_addr(ndev->dev_addr)) {
		eth_hw_addr_random(ndev);
		dev_warn(dev, "using random MAC address %pM\n",
			 ndev->dev_addr);
	}

	ndev->watchdog_timeo = 6 * HZ;
	ndev->priv_flags |= IFF_UNICAST_FLT;
	ndev->netdev_ops = &hisi_femac_netdev_ops;
	ndev->ethtool_ops = &hisi_femac_ethtools_ops;
	netif_napi_add(ndev, &priv->napi, hisi_femac_poll, FEMAC_POLL_WEIGHT);

	hisi_femac_port_init(priv);

	ret = hisi_femac_init_tx_and_rx_queues(priv);
	if (ret)
		goto out_disconnect_phy;

	ndev->irq = platform_get_irq(pdev, 0);
	if (ndev->irq <= 0) {
		ret = -ENODEV;
		goto out_disconnect_phy;
	}

	ret = devm_request_irq(dev, ndev->irq, hisi_femac_interrupt,
			       IRQF_SHARED, pdev->name, ndev);
	if (ret) {
		dev_err(dev, "devm_request_irq %d failed!\n", ndev->irq);
		goto out_disconnect_phy;
	}

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(dev, "register_netdev failed!\n");
		goto out_disconnect_phy;
	}

	return ret;

out_disconnect_phy:
	netif_napi_del(&priv->napi);
	phy_disconnect(phy);
out_disable_clk:
	clk_disable_unprepare(priv->clk);
out_free_netdev:
	free_netdev(ndev);

	return ret;
}

static int hisi_femac_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct hisi_femac_priv *priv = netdev_priv(ndev);

	netif_napi_del(&priv->napi);
	unregister_netdev(ndev);

	phy_disconnect(ndev->phydev);
	clk_disable_unprepare(priv->clk);
	free_netdev(ndev);

	return 0;
}

#ifdef CONFIG_PM
static int hisi_femac_drv_suspend(struct platform_device *pdev,
				  pm_message_t state)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct hisi_femac_priv *priv = netdev_priv(ndev);

	disable_irq(ndev->irq);
	if (netif_running(ndev)) {
		hisi_femac_net_close(ndev);
		netif_device_detach(ndev);
	}

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int hisi_femac_drv_resume(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct hisi_femac_priv *priv = netdev_priv(ndev);

	clk_prepare_enable(priv->clk);
	if (priv->phy_rst)
		hisi_femac_phy_reset(priv);

	if (netif_running(ndev)) {
		hisi_femac_port_init(priv);
		hisi_femac_net_open(ndev);
		netif_device_attach(ndev);
	}
	enable_irq(ndev->irq);

	return 0;
}
#endif

static const struct of_device_id hisi_femac_match[] = {
	{.compatible = "hisilicon,hisi-femac-v1",},
	{.compatible = "hisilicon,hisi-femac-v2",},
	{.compatible = "hisilicon,hi3516cv300-femac",},
	{},
};

MODULE_DEVICE_TABLE(of, hisi_femac_match);

static struct platform_driver hisi_femac_driver = {
	.driver = {
		.name = "hisi-femac",
		.of_match_table = hisi_femac_match,
	},
	.probe = hisi_femac_drv_probe,
	.remove = hisi_femac_drv_remove,
#ifdef CONFIG_PM
	.suspend = hisi_femac_drv_suspend,
	.resume = hisi_femac_drv_resume,
#endif
};

module_platform_driver(hisi_femac_driver);

MODULE_DESCRIPTION("Hisilicon Fast Ethernet MAC driver");
MODULE_AUTHOR("Dongpo Li <lidongpo@hisilicon.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:hisi-femac");
