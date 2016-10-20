/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2009-2016 John Crispin <blogic@openwrt.org>
 *   Copyright (C) 2009-2016 Felix Fietkau <nbd@openwrt.org>
 *   Copyright (C) 2013-2016 Michael Lee <igvtee@gmail.com>
 */

#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/if_vlan.h>
#include <linux/reset.h>
#include <linux/tcp.h>

#include "mtk_eth_soc.h"

static int mtk_msg_level = -1;
module_param_named(msg_level, mtk_msg_level, int, 0);
MODULE_PARM_DESC(msg_level, "Message level (-1=defaults,0=none,...,16=all)");

#define MTK_ETHTOOL_STAT(x) { #x, \
			      offsetof(struct mtk_hw_stats, x) / sizeof(u64) }

/* strings used by ethtool */
static const struct mtk_ethtool_stats {
	char str[ETH_GSTRING_LEN];
	u32 offset;
} mtk_ethtool_stats[] = {
	MTK_ETHTOOL_STAT(tx_bytes),
	MTK_ETHTOOL_STAT(tx_packets),
	MTK_ETHTOOL_STAT(tx_skip),
	MTK_ETHTOOL_STAT(tx_collisions),
	MTK_ETHTOOL_STAT(rx_bytes),
	MTK_ETHTOOL_STAT(rx_packets),
	MTK_ETHTOOL_STAT(rx_overflow),
	MTK_ETHTOOL_STAT(rx_fcs_errors),
	MTK_ETHTOOL_STAT(rx_short_errors),
	MTK_ETHTOOL_STAT(rx_long_errors),
	MTK_ETHTOOL_STAT(rx_checksum_errors),
	MTK_ETHTOOL_STAT(rx_flow_control_packets),
};

static const char * const mtk_clks_source_name[] = {
	"ethif", "esw", "gp1", "gp2", "trgpll"
};

void mtk_w32(struct mtk_eth *eth, u32 val, unsigned reg)
{
	__raw_writel(val, eth->base + reg);
}

u32 mtk_r32(struct mtk_eth *eth, unsigned reg)
{
	return __raw_readl(eth->base + reg);
}

static int mtk_mdio_busy_wait(struct mtk_eth *eth)
{
	unsigned long t_start = jiffies;

	while (1) {
		if (!(mtk_r32(eth, MTK_PHY_IAC) & PHY_IAC_ACCESS))
			return 0;
		if (time_after(jiffies, t_start + PHY_IAC_TIMEOUT))
			break;
		usleep_range(10, 20);
	}

	dev_err(eth->dev, "mdio: MDIO timeout\n");
	return -1;
}

static u32 _mtk_mdio_write(struct mtk_eth *eth, u32 phy_addr,
			   u32 phy_register, u32 write_data)
{
	if (mtk_mdio_busy_wait(eth))
		return -1;

	write_data &= 0xffff;

	mtk_w32(eth, PHY_IAC_ACCESS | PHY_IAC_START | PHY_IAC_WRITE |
		(phy_register << PHY_IAC_REG_SHIFT) |
		(phy_addr << PHY_IAC_ADDR_SHIFT) | write_data,
		MTK_PHY_IAC);

	if (mtk_mdio_busy_wait(eth))
		return -1;

	return 0;
}

static u32 _mtk_mdio_read(struct mtk_eth *eth, int phy_addr, int phy_reg)
{
	u32 d;

	if (mtk_mdio_busy_wait(eth))
		return 0xffff;

	mtk_w32(eth, PHY_IAC_ACCESS | PHY_IAC_START | PHY_IAC_READ |
		(phy_reg << PHY_IAC_REG_SHIFT) |
		(phy_addr << PHY_IAC_ADDR_SHIFT),
		MTK_PHY_IAC);

	if (mtk_mdio_busy_wait(eth))
		return 0xffff;

	d = mtk_r32(eth, MTK_PHY_IAC) & 0xffff;

	return d;
}

static int mtk_mdio_write(struct mii_bus *bus, int phy_addr,
			  int phy_reg, u16 val)
{
	struct mtk_eth *eth = bus->priv;

	return _mtk_mdio_write(eth, phy_addr, phy_reg, val);
}

static int mtk_mdio_read(struct mii_bus *bus, int phy_addr, int phy_reg)
{
	struct mtk_eth *eth = bus->priv;

	return _mtk_mdio_read(eth, phy_addr, phy_reg);
}

static void mtk_gmac0_rgmii_adjust(struct mtk_eth *eth, int speed)
{
	u32 val;
	int ret;

	val = (speed == SPEED_1000) ?
		INTF_MODE_RGMII_1000 : INTF_MODE_RGMII_10_100;
	mtk_w32(eth, val, INTF_MODE);

	regmap_update_bits(eth->ethsys, ETHSYS_CLKCFG0,
			   ETHSYS_TRGMII_CLK_SEL362_5,
			   ETHSYS_TRGMII_CLK_SEL362_5);

	val = (speed == SPEED_1000) ? 250000000 : 500000000;
	ret = clk_set_rate(eth->clks[MTK_CLK_TRGPLL], val);
	if (ret)
		dev_err(eth->dev, "Failed to set trgmii pll: %d\n", ret);

	val = (speed == SPEED_1000) ?
		RCK_CTRL_RGMII_1000 : RCK_CTRL_RGMII_10_100;
	mtk_w32(eth, val, TRGMII_RCK_CTRL);

	val = (speed == SPEED_1000) ?
		TCK_CTRL_RGMII_1000 : TCK_CTRL_RGMII_10_100;
	mtk_w32(eth, val, TRGMII_TCK_CTRL);
}

static void mtk_phy_link_adjust(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	u16 lcl_adv = 0, rmt_adv = 0;
	u8 flowctrl;
	u32 mcr = MAC_MCR_MAX_RX_1536 | MAC_MCR_IPG_CFG |
		  MAC_MCR_FORCE_MODE | MAC_MCR_TX_EN |
		  MAC_MCR_RX_EN | MAC_MCR_BACKOFF_EN |
		  MAC_MCR_BACKPR_EN;

	if (unlikely(test_bit(MTK_RESETTING, &mac->hw->state)))
		return;

	switch (dev->phydev->speed) {
	case SPEED_1000:
		mcr |= MAC_MCR_SPEED_1000;
		break;
	case SPEED_100:
		mcr |= MAC_MCR_SPEED_100;
		break;
	};

	if (mac->id == 0 && !mac->trgmii)
		mtk_gmac0_rgmii_adjust(mac->hw, dev->phydev->speed);

	if (dev->phydev->link)
		mcr |= MAC_MCR_FORCE_LINK;

	if (dev->phydev->duplex) {
		mcr |= MAC_MCR_FORCE_DPX;

		if (dev->phydev->pause)
			rmt_adv = LPA_PAUSE_CAP;
		if (dev->phydev->asym_pause)
			rmt_adv |= LPA_PAUSE_ASYM;

		if (dev->phydev->advertising & ADVERTISED_Pause)
			lcl_adv |= ADVERTISE_PAUSE_CAP;
		if (dev->phydev->advertising & ADVERTISED_Asym_Pause)
			lcl_adv |= ADVERTISE_PAUSE_ASYM;

		flowctrl = mii_resolve_flowctrl_fdx(lcl_adv, rmt_adv);

		if (flowctrl & FLOW_CTRL_TX)
			mcr |= MAC_MCR_FORCE_TX_FC;
		if (flowctrl & FLOW_CTRL_RX)
			mcr |= MAC_MCR_FORCE_RX_FC;

		netif_dbg(mac->hw, link, dev, "rx pause %s, tx pause %s\n",
			  flowctrl & FLOW_CTRL_RX ? "enabled" : "disabled",
			  flowctrl & FLOW_CTRL_TX ? "enabled" : "disabled");
	}

	mtk_w32(mac->hw, mcr, MTK_MAC_MCR(mac->id));

	if (dev->phydev->link)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);
}

static int mtk_phy_connect_node(struct mtk_eth *eth, struct mtk_mac *mac,
				struct device_node *phy_node)
{
	struct phy_device *phydev;
	int phy_mode;

	phy_mode = of_get_phy_mode(phy_node);
	if (phy_mode < 0) {
		dev_err(eth->dev, "incorrect phy-mode %d\n", phy_mode);
		return -EINVAL;
	}

	phydev = of_phy_connect(eth->netdev[mac->id], phy_node,
				mtk_phy_link_adjust, 0, phy_mode);
	if (!phydev) {
		dev_err(eth->dev, "could not connect to PHY\n");
		return -ENODEV;
	}

	dev_info(eth->dev,
		 "connected mac %d to PHY at %s [uid=%08x, driver=%s]\n",
		 mac->id, phydev_name(phydev), phydev->phy_id,
		 phydev->drv->name);

	return 0;
}

static int mtk_phy_connect(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth;
	struct device_node *np;
	u32 val;

	eth = mac->hw;
	np = of_parse_phandle(mac->of_node, "phy-handle", 0);
	if (!np && of_phy_is_fixed_link(mac->of_node))
		if (!of_phy_register_fixed_link(mac->of_node))
			np = of_node_get(mac->of_node);
	if (!np)
		return -ENODEV;

	switch (of_get_phy_mode(np)) {
	case PHY_INTERFACE_MODE_TRGMII:
		mac->trgmii = true;
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII:
		mac->ge_mode = 0;
		break;
	case PHY_INTERFACE_MODE_MII:
		mac->ge_mode = 1;
		break;
	case PHY_INTERFACE_MODE_REVMII:
		mac->ge_mode = 2;
		break;
	case PHY_INTERFACE_MODE_RMII:
		if (!mac->id)
			goto err_phy;
		mac->ge_mode = 3;
		break;
	default:
		goto err_phy;
	}

	/* put the gmac into the right mode */
	regmap_read(eth->ethsys, ETHSYS_SYSCFG0, &val);
	val &= ~SYSCFG0_GE_MODE(SYSCFG0_GE_MASK, mac->id);
	val |= SYSCFG0_GE_MODE(mac->ge_mode, mac->id);
	regmap_write(eth->ethsys, ETHSYS_SYSCFG0, val);

	/* couple phydev to net_device */
	if (mtk_phy_connect_node(eth, mac, np))
		goto err_phy;

	dev->phydev->autoneg = AUTONEG_ENABLE;
	dev->phydev->speed = 0;
	dev->phydev->duplex = 0;

	if (of_phy_is_fixed_link(mac->of_node))
		dev->phydev->supported |=
		SUPPORTED_Pause | SUPPORTED_Asym_Pause;

	dev->phydev->supported &= PHY_GBIT_FEATURES | SUPPORTED_Pause |
				   SUPPORTED_Asym_Pause;
	dev->phydev->advertising = dev->phydev->supported |
				    ADVERTISED_Autoneg;
	phy_start_aneg(dev->phydev);

	of_node_put(np);

	return 0;

err_phy:
	of_node_put(np);
	dev_err(eth->dev, "%s: invalid phy\n", __func__);
	return -EINVAL;
}

static int mtk_mdio_init(struct mtk_eth *eth)
{
	struct device_node *mii_np;
	int ret;

	mii_np = of_get_child_by_name(eth->dev->of_node, "mdio-bus");
	if (!mii_np) {
		dev_err(eth->dev, "no %s child node found", "mdio-bus");
		return -ENODEV;
	}

	if (!of_device_is_available(mii_np)) {
		ret = -ENODEV;
		goto err_put_node;
	}

	eth->mii_bus = devm_mdiobus_alloc(eth->dev);
	if (!eth->mii_bus) {
		ret = -ENOMEM;
		goto err_put_node;
	}

	eth->mii_bus->name = "mdio";
	eth->mii_bus->read = mtk_mdio_read;
	eth->mii_bus->write = mtk_mdio_write;
	eth->mii_bus->priv = eth;
	eth->mii_bus->parent = eth->dev;

	snprintf(eth->mii_bus->id, MII_BUS_ID_SIZE, "%s", mii_np->name);
	ret = of_mdiobus_register(eth->mii_bus, mii_np);

err_put_node:
	of_node_put(mii_np);
	return ret;
}

static void mtk_mdio_cleanup(struct mtk_eth *eth)
{
	if (!eth->mii_bus)
		return;

	mdiobus_unregister(eth->mii_bus);
}

static inline void mtk_irq_disable(struct mtk_eth *eth,
				   unsigned reg, u32 mask)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&eth->irq_lock, flags);
	val = mtk_r32(eth, reg);
	mtk_w32(eth, val & ~mask, reg);
	spin_unlock_irqrestore(&eth->irq_lock, flags);
}

static inline void mtk_irq_enable(struct mtk_eth *eth,
				  unsigned reg, u32 mask)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&eth->irq_lock, flags);
	val = mtk_r32(eth, reg);
	mtk_w32(eth, val | mask, reg);
	spin_unlock_irqrestore(&eth->irq_lock, flags);
}

static int mtk_set_mac_address(struct net_device *dev, void *p)
{
	int ret = eth_mac_addr(dev, p);
	struct mtk_mac *mac = netdev_priv(dev);
	const char *macaddr = dev->dev_addr;

	if (ret)
		return ret;

	if (unlikely(test_bit(MTK_RESETTING, &mac->hw->state)))
		return -EBUSY;

	spin_lock_bh(&mac->hw->page_lock);
	mtk_w32(mac->hw, (macaddr[0] << 8) | macaddr[1],
		MTK_GDMA_MAC_ADRH(mac->id));
	mtk_w32(mac->hw, (macaddr[2] << 24) | (macaddr[3] << 16) |
		(macaddr[4] << 8) | macaddr[5],
		MTK_GDMA_MAC_ADRL(mac->id));
	spin_unlock_bh(&mac->hw->page_lock);

	return 0;
}

void mtk_stats_update_mac(struct mtk_mac *mac)
{
	struct mtk_hw_stats *hw_stats = mac->hw_stats;
	unsigned int base = MTK_GDM1_TX_GBCNT;
	u64 stats;

	base += hw_stats->reg_offset;

	u64_stats_update_begin(&hw_stats->syncp);

	hw_stats->rx_bytes += mtk_r32(mac->hw, base);
	stats =  mtk_r32(mac->hw, base + 0x04);
	if (stats)
		hw_stats->rx_bytes += (stats << 32);
	hw_stats->rx_packets += mtk_r32(mac->hw, base + 0x08);
	hw_stats->rx_overflow += mtk_r32(mac->hw, base + 0x10);
	hw_stats->rx_fcs_errors += mtk_r32(mac->hw, base + 0x14);
	hw_stats->rx_short_errors += mtk_r32(mac->hw, base + 0x18);
	hw_stats->rx_long_errors += mtk_r32(mac->hw, base + 0x1c);
	hw_stats->rx_checksum_errors += mtk_r32(mac->hw, base + 0x20);
	hw_stats->rx_flow_control_packets +=
					mtk_r32(mac->hw, base + 0x24);
	hw_stats->tx_skip += mtk_r32(mac->hw, base + 0x28);
	hw_stats->tx_collisions += mtk_r32(mac->hw, base + 0x2c);
	hw_stats->tx_bytes += mtk_r32(mac->hw, base + 0x30);
	stats =  mtk_r32(mac->hw, base + 0x34);
	if (stats)
		hw_stats->tx_bytes += (stats << 32);
	hw_stats->tx_packets += mtk_r32(mac->hw, base + 0x38);
	u64_stats_update_end(&hw_stats->syncp);
}

static void mtk_stats_update(struct mtk_eth *eth)
{
	int i;

	for (i = 0; i < MTK_MAC_COUNT; i++) {
		if (!eth->mac[i] || !eth->mac[i]->hw_stats)
			continue;
		if (spin_trylock(&eth->mac[i]->hw_stats->stats_lock)) {
			mtk_stats_update_mac(eth->mac[i]);
			spin_unlock(&eth->mac[i]->hw_stats->stats_lock);
		}
	}
}

static struct rtnl_link_stats64 *mtk_get_stats64(struct net_device *dev,
					struct rtnl_link_stats64 *storage)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_hw_stats *hw_stats = mac->hw_stats;
	unsigned int start;

	if (netif_running(dev) && netif_device_present(dev)) {
		if (spin_trylock(&hw_stats->stats_lock)) {
			mtk_stats_update_mac(mac);
			spin_unlock(&hw_stats->stats_lock);
		}
	}

	do {
		start = u64_stats_fetch_begin_irq(&hw_stats->syncp);
		storage->rx_packets = hw_stats->rx_packets;
		storage->tx_packets = hw_stats->tx_packets;
		storage->rx_bytes = hw_stats->rx_bytes;
		storage->tx_bytes = hw_stats->tx_bytes;
		storage->collisions = hw_stats->tx_collisions;
		storage->rx_length_errors = hw_stats->rx_short_errors +
			hw_stats->rx_long_errors;
		storage->rx_over_errors = hw_stats->rx_overflow;
		storage->rx_crc_errors = hw_stats->rx_fcs_errors;
		storage->rx_errors = hw_stats->rx_checksum_errors;
		storage->tx_aborted_errors = hw_stats->tx_skip;
	} while (u64_stats_fetch_retry_irq(&hw_stats->syncp, start));

	storage->tx_errors = dev->stats.tx_errors;
	storage->rx_dropped = dev->stats.rx_dropped;
	storage->tx_dropped = dev->stats.tx_dropped;

	return storage;
}

static inline int mtk_max_frag_size(int mtu)
{
	/* make sure buf_size will be at least MTK_MAX_RX_LENGTH */
	if (mtu + MTK_RX_ETH_HLEN < MTK_MAX_RX_LENGTH)
		mtu = MTK_MAX_RX_LENGTH - MTK_RX_ETH_HLEN;

	return SKB_DATA_ALIGN(MTK_RX_HLEN + mtu) +
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
}

static inline int mtk_max_buf_size(int frag_size)
{
	int buf_size = frag_size - NET_SKB_PAD - NET_IP_ALIGN -
		       SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	WARN_ON(buf_size < MTK_MAX_RX_LENGTH);

	return buf_size;
}

static inline void mtk_rx_get_desc(struct mtk_rx_dma *rxd,
				   struct mtk_rx_dma *dma_rxd)
{
	rxd->rxd1 = READ_ONCE(dma_rxd->rxd1);
	rxd->rxd2 = READ_ONCE(dma_rxd->rxd2);
	rxd->rxd3 = READ_ONCE(dma_rxd->rxd3);
	rxd->rxd4 = READ_ONCE(dma_rxd->rxd4);
}

/* the qdma core needs scratch memory to be setup */
static int mtk_init_fq_dma(struct mtk_eth *eth)
{
	dma_addr_t phy_ring_tail;
	int cnt = MTK_DMA_SIZE;
	dma_addr_t dma_addr;
	int i;

	eth->scratch_ring = dma_alloc_coherent(eth->dev,
					       cnt * sizeof(struct mtk_tx_dma),
					       &eth->phy_scratch_ring,
					       GFP_ATOMIC | __GFP_ZERO);
	if (unlikely(!eth->scratch_ring))
		return -ENOMEM;

	eth->scratch_head = kcalloc(cnt, MTK_QDMA_PAGE_SIZE,
				    GFP_KERNEL);
	if (unlikely(!eth->scratch_head))
		return -ENOMEM;

	dma_addr = dma_map_single(eth->dev,
				  eth->scratch_head, cnt * MTK_QDMA_PAGE_SIZE,
				  DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(eth->dev, dma_addr)))
		return -ENOMEM;

	memset(eth->scratch_ring, 0x0, sizeof(struct mtk_tx_dma) * cnt);
	phy_ring_tail = eth->phy_scratch_ring +
			(sizeof(struct mtk_tx_dma) * (cnt - 1));

	for (i = 0; i < cnt; i++) {
		eth->scratch_ring[i].txd1 =
					(dma_addr + (i * MTK_QDMA_PAGE_SIZE));
		if (i < cnt - 1)
			eth->scratch_ring[i].txd2 = (eth->phy_scratch_ring +
				((i + 1) * sizeof(struct mtk_tx_dma)));
		eth->scratch_ring[i].txd3 = TX_DMA_SDL(MTK_QDMA_PAGE_SIZE);
	}

	mtk_w32(eth, eth->phy_scratch_ring, MTK_QDMA_FQ_HEAD);
	mtk_w32(eth, phy_ring_tail, MTK_QDMA_FQ_TAIL);
	mtk_w32(eth, (cnt << 16) | cnt, MTK_QDMA_FQ_CNT);
	mtk_w32(eth, MTK_QDMA_PAGE_SIZE << 16, MTK_QDMA_FQ_BLEN);

	return 0;
}

static inline void *mtk_qdma_phys_to_virt(struct mtk_tx_ring *ring, u32 desc)
{
	void *ret = ring->dma;

	return ret + (desc - ring->phys);
}

static inline struct mtk_tx_buf *mtk_desc_to_tx_buf(struct mtk_tx_ring *ring,
						    struct mtk_tx_dma *txd)
{
	int idx = txd - ring->dma;

	return &ring->buf[idx];
}

static void mtk_tx_unmap(struct mtk_eth *eth, struct mtk_tx_buf *tx_buf)
{
	if (tx_buf->flags & MTK_TX_FLAGS_SINGLE0) {
		dma_unmap_single(eth->dev,
				 dma_unmap_addr(tx_buf, dma_addr0),
				 dma_unmap_len(tx_buf, dma_len0),
				 DMA_TO_DEVICE);
	} else if (tx_buf->flags & MTK_TX_FLAGS_PAGE0) {
		dma_unmap_page(eth->dev,
			       dma_unmap_addr(tx_buf, dma_addr0),
			       dma_unmap_len(tx_buf, dma_len0),
			       DMA_TO_DEVICE);
	}
	tx_buf->flags = 0;
	if (tx_buf->skb &&
	    (tx_buf->skb != (struct sk_buff *)MTK_DMA_DUMMY_DESC))
		dev_kfree_skb_any(tx_buf->skb);
	tx_buf->skb = NULL;
}

static int mtk_tx_map(struct sk_buff *skb, struct net_device *dev,
		      int tx_num, struct mtk_tx_ring *ring, bool gso)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	struct mtk_tx_dma *itxd, *txd;
	struct mtk_tx_buf *tx_buf;
	dma_addr_t mapped_addr;
	unsigned int nr_frags;
	int i, n_desc = 1;
	u32 txd4 = 0, fport;

	itxd = ring->next_free;
	if (itxd == ring->last_free)
		return -ENOMEM;

	/* set the forward port */
	fport = (mac->id + 1) << TX_DMA_FPORT_SHIFT;
	txd4 |= fport;

	tx_buf = mtk_desc_to_tx_buf(ring, itxd);
	memset(tx_buf, 0, sizeof(*tx_buf));

	if (gso)
		txd4 |= TX_DMA_TSO;

	/* TX Checksum offload */
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		txd4 |= TX_DMA_CHKSUM;

	/* VLAN header offload */
	if (skb_vlan_tag_present(skb))
		txd4 |= TX_DMA_INS_VLAN | skb_vlan_tag_get(skb);

	mapped_addr = dma_map_single(eth->dev, skb->data,
				     skb_headlen(skb), DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(eth->dev, mapped_addr)))
		return -ENOMEM;

	WRITE_ONCE(itxd->txd1, mapped_addr);
	tx_buf->flags |= MTK_TX_FLAGS_SINGLE0;
	dma_unmap_addr_set(tx_buf, dma_addr0, mapped_addr);
	dma_unmap_len_set(tx_buf, dma_len0, skb_headlen(skb));

	/* TX SG offload */
	txd = itxd;
	nr_frags = skb_shinfo(skb)->nr_frags;
	for (i = 0; i < nr_frags; i++) {
		struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[i];
		unsigned int offset = 0;
		int frag_size = skb_frag_size(frag);

		while (frag_size) {
			bool last_frag = false;
			unsigned int frag_map_size;

			txd = mtk_qdma_phys_to_virt(ring, txd->txd2);
			if (txd == ring->last_free)
				goto err_dma;

			n_desc++;
			frag_map_size = min(frag_size, MTK_TX_DMA_BUF_LEN);
			mapped_addr = skb_frag_dma_map(eth->dev, frag, offset,
						       frag_map_size,
						       DMA_TO_DEVICE);
			if (unlikely(dma_mapping_error(eth->dev, mapped_addr)))
				goto err_dma;

			if (i == nr_frags - 1 &&
			    (frag_size - frag_map_size) == 0)
				last_frag = true;

			WRITE_ONCE(txd->txd1, mapped_addr);
			WRITE_ONCE(txd->txd3, (TX_DMA_SWC |
					       TX_DMA_PLEN0(frag_map_size) |
					       last_frag * TX_DMA_LS0));
			WRITE_ONCE(txd->txd4, fport);

			tx_buf->skb = (struct sk_buff *)MTK_DMA_DUMMY_DESC;
			tx_buf = mtk_desc_to_tx_buf(ring, txd);
			memset(tx_buf, 0, sizeof(*tx_buf));

			tx_buf->flags |= MTK_TX_FLAGS_PAGE0;
			dma_unmap_addr_set(tx_buf, dma_addr0, mapped_addr);
			dma_unmap_len_set(tx_buf, dma_len0, frag_map_size);
			frag_size -= frag_map_size;
			offset += frag_map_size;
		}
	}

	/* store skb to cleanup */
	tx_buf->skb = skb;

	WRITE_ONCE(itxd->txd4, txd4);
	WRITE_ONCE(itxd->txd3, (TX_DMA_SWC | TX_DMA_PLEN0(skb_headlen(skb)) |
				(!nr_frags * TX_DMA_LS0)));

	netdev_sent_queue(dev, skb->len);
	skb_tx_timestamp(skb);

	ring->next_free = mtk_qdma_phys_to_virt(ring, txd->txd2);
	atomic_sub(n_desc, &ring->free_count);

	/* make sure that all changes to the dma ring are flushed before we
	 * continue
	 */
	wmb();

	if (netif_xmit_stopped(netdev_get_tx_queue(dev, 0)) || !skb->xmit_more)
		mtk_w32(eth, txd->txd2, MTK_QTX_CTX_PTR);

	return 0;

err_dma:
	do {
		tx_buf = mtk_desc_to_tx_buf(ring, itxd);

		/* unmap dma */
		mtk_tx_unmap(eth, tx_buf);

		itxd->txd3 = TX_DMA_LS0 | TX_DMA_OWNER_CPU;
		itxd = mtk_qdma_phys_to_virt(ring, itxd->txd2);
	} while (itxd != txd);

	return -ENOMEM;
}

static inline int mtk_cal_txd_req(struct sk_buff *skb)
{
	int i, nfrags;
	struct skb_frag_struct *frag;

	nfrags = 1;
	if (skb_is_gso(skb)) {
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			frag = &skb_shinfo(skb)->frags[i];
			nfrags += DIV_ROUND_UP(frag->size, MTK_TX_DMA_BUF_LEN);
		}
	} else {
		nfrags += skb_shinfo(skb)->nr_frags;
	}

	return nfrags;
}

static int mtk_queue_stopped(struct mtk_eth *eth)
{
	int i;

	for (i = 0; i < MTK_MAC_COUNT; i++) {
		if (!eth->netdev[i])
			continue;
		if (netif_queue_stopped(eth->netdev[i]))
			return 1;
	}

	return 0;
}

static void mtk_wake_queue(struct mtk_eth *eth)
{
	int i;

	for (i = 0; i < MTK_MAC_COUNT; i++) {
		if (!eth->netdev[i])
			continue;
		netif_wake_queue(eth->netdev[i]);
	}
}

static void mtk_stop_queue(struct mtk_eth *eth)
{
	int i;

	for (i = 0; i < MTK_MAC_COUNT; i++) {
		if (!eth->netdev[i])
			continue;
		netif_stop_queue(eth->netdev[i]);
	}
}

static int mtk_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	struct mtk_tx_ring *ring = &eth->tx_ring;
	struct net_device_stats *stats = &dev->stats;
	bool gso = false;
	int tx_num;

	/* normally we can rely on the stack not calling this more than once,
	 * however we have 2 queues running on the same ring so we need to lock
	 * the ring access
	 */
	spin_lock(&eth->page_lock);

	if (unlikely(test_bit(MTK_RESETTING, &eth->state)))
		goto drop;

	tx_num = mtk_cal_txd_req(skb);
	if (unlikely(atomic_read(&ring->free_count) <= tx_num)) {
		mtk_stop_queue(eth);
		netif_err(eth, tx_queued, dev,
			  "Tx Ring full when queue awake!\n");
		spin_unlock(&eth->page_lock);
		return NETDEV_TX_BUSY;
	}

	/* TSO: fill MSS info in tcp checksum field */
	if (skb_is_gso(skb)) {
		if (skb_cow_head(skb, 0)) {
			netif_warn(eth, tx_err, dev,
				   "GSO expand head fail.\n");
			goto drop;
		}

		if (skb_shinfo(skb)->gso_type &
				(SKB_GSO_TCPV4 | SKB_GSO_TCPV6)) {
			gso = true;
			tcp_hdr(skb)->check = htons(skb_shinfo(skb)->gso_size);
		}
	}

	if (mtk_tx_map(skb, dev, tx_num, ring, gso) < 0)
		goto drop;

	if (unlikely(atomic_read(&ring->free_count) <= ring->thresh))
		mtk_stop_queue(eth);

	spin_unlock(&eth->page_lock);

	return NETDEV_TX_OK;

drop:
	spin_unlock(&eth->page_lock);
	stats->tx_dropped++;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static struct mtk_rx_ring *mtk_get_rx_ring(struct mtk_eth *eth)
{
	int i;
	struct mtk_rx_ring *ring;
	int idx;

	if (!eth->hwlro)
		return &eth->rx_ring[0];

	for (i = 0; i < MTK_MAX_RX_RING_NUM; i++) {
		ring = &eth->rx_ring[i];
		idx = NEXT_RX_DESP_IDX(ring->calc_idx, ring->dma_size);
		if (ring->dma[idx].rxd2 & RX_DMA_DONE) {
			ring->calc_idx_update = true;
			return ring;
		}
	}

	return NULL;
}

static void mtk_update_rx_cpu_idx(struct mtk_eth *eth)
{
	struct mtk_rx_ring *ring;
	int i;

	if (!eth->hwlro) {
		ring = &eth->rx_ring[0];
		mtk_w32(eth, ring->calc_idx, ring->crx_idx_reg);
	} else {
		for (i = 0; i < MTK_MAX_RX_RING_NUM; i++) {
			ring = &eth->rx_ring[i];
			if (ring->calc_idx_update) {
				ring->calc_idx_update = false;
				mtk_w32(eth, ring->calc_idx, ring->crx_idx_reg);
			}
		}
	}
}

static int mtk_poll_rx(struct napi_struct *napi, int budget,
		       struct mtk_eth *eth)
{
	struct mtk_rx_ring *ring;
	int idx;
	struct sk_buff *skb;
	u8 *data, *new_data;
	struct mtk_rx_dma *rxd, trxd;
	int done = 0;

	while (done < budget) {
		struct net_device *netdev;
		unsigned int pktlen;
		dma_addr_t dma_addr;
		int mac = 0;

		ring = mtk_get_rx_ring(eth);
		if (unlikely(!ring))
			goto rx_done;

		idx = NEXT_RX_DESP_IDX(ring->calc_idx, ring->dma_size);
		rxd = &ring->dma[idx];
		data = ring->data[idx];

		mtk_rx_get_desc(&trxd, rxd);
		if (!(trxd.rxd2 & RX_DMA_DONE))
			break;

		/* find out which mac the packet come from. values start at 1 */
		mac = (trxd.rxd4 >> RX_DMA_FPORT_SHIFT) &
		      RX_DMA_FPORT_MASK;
		mac--;

		netdev = eth->netdev[mac];

		if (unlikely(test_bit(MTK_RESETTING, &eth->state)))
			goto release_desc;

		/* alloc new buffer */
		new_data = napi_alloc_frag(ring->frag_size);
		if (unlikely(!new_data)) {
			netdev->stats.rx_dropped++;
			goto release_desc;
		}
		dma_addr = dma_map_single(eth->dev,
					  new_data + NET_SKB_PAD,
					  ring->buf_size,
					  DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(eth->dev, dma_addr))) {
			skb_free_frag(new_data);
			netdev->stats.rx_dropped++;
			goto release_desc;
		}

		/* receive data */
		skb = build_skb(data, ring->frag_size);
		if (unlikely(!skb)) {
			skb_free_frag(new_data);
			netdev->stats.rx_dropped++;
			goto release_desc;
		}
		skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);

		dma_unmap_single(eth->dev, trxd.rxd1,
				 ring->buf_size, DMA_FROM_DEVICE);
		pktlen = RX_DMA_GET_PLEN0(trxd.rxd2);
		skb->dev = netdev;
		skb_put(skb, pktlen);
		if (trxd.rxd4 & RX_DMA_L4_VALID)
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb_checksum_none_assert(skb);
		skb->protocol = eth_type_trans(skb, netdev);

		if (netdev->features & NETIF_F_HW_VLAN_CTAG_RX &&
		    RX_DMA_VID(trxd.rxd3))
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
					       RX_DMA_VID(trxd.rxd3));
		napi_gro_receive(napi, skb);

		ring->data[idx] = new_data;
		rxd->rxd1 = (unsigned int)dma_addr;

release_desc:
		rxd->rxd2 = RX_DMA_PLEN0(ring->buf_size);

		ring->calc_idx = idx;

		done++;
	}

rx_done:
	if (done) {
		/* make sure that all changes to the dma ring are flushed before
		 * we continue
		 */
		wmb();
		mtk_update_rx_cpu_idx(eth);
	}

	return done;
}

static int mtk_poll_tx(struct mtk_eth *eth, int budget)
{
	struct mtk_tx_ring *ring = &eth->tx_ring;
	struct mtk_tx_dma *desc;
	struct sk_buff *skb;
	struct mtk_tx_buf *tx_buf;
	unsigned int done[MTK_MAX_DEVS];
	unsigned int bytes[MTK_MAX_DEVS];
	u32 cpu, dma;
	static int condition;
	int total = 0, i;

	memset(done, 0, sizeof(done));
	memset(bytes, 0, sizeof(bytes));

	cpu = mtk_r32(eth, MTK_QTX_CRX_PTR);
	dma = mtk_r32(eth, MTK_QTX_DRX_PTR);

	desc = mtk_qdma_phys_to_virt(ring, cpu);

	while ((cpu != dma) && budget) {
		u32 next_cpu = desc->txd2;
		int mac;

		desc = mtk_qdma_phys_to_virt(ring, desc->txd2);
		if ((desc->txd3 & TX_DMA_OWNER_CPU) == 0)
			break;

		mac = (desc->txd4 >> TX_DMA_FPORT_SHIFT) &
		       TX_DMA_FPORT_MASK;
		mac--;

		tx_buf = mtk_desc_to_tx_buf(ring, desc);
		skb = tx_buf->skb;
		if (!skb) {
			condition = 1;
			break;
		}

		if (skb != (struct sk_buff *)MTK_DMA_DUMMY_DESC) {
			bytes[mac] += skb->len;
			done[mac]++;
			budget--;
		}
		mtk_tx_unmap(eth, tx_buf);

		ring->last_free = desc;
		atomic_inc(&ring->free_count);

		cpu = next_cpu;
	}

	mtk_w32(eth, cpu, MTK_QTX_CRX_PTR);

	for (i = 0; i < MTK_MAC_COUNT; i++) {
		if (!eth->netdev[i] || !done[i])
			continue;
		netdev_completed_queue(eth->netdev[i], done[i], bytes[i]);
		total += done[i];
	}

	if (mtk_queue_stopped(eth) &&
	    (atomic_read(&ring->free_count) > ring->thresh))
		mtk_wake_queue(eth);

	return total;
}

static void mtk_handle_status_irq(struct mtk_eth *eth)
{
	u32 status2 = mtk_r32(eth, MTK_INT_STATUS2);

	if (unlikely(status2 & (MTK_GDM1_AF | MTK_GDM2_AF))) {
		mtk_stats_update(eth);
		mtk_w32(eth, (MTK_GDM1_AF | MTK_GDM2_AF),
			MTK_INT_STATUS2);
	}
}

static int mtk_napi_tx(struct napi_struct *napi, int budget)
{
	struct mtk_eth *eth = container_of(napi, struct mtk_eth, tx_napi);
	u32 status, mask;
	int tx_done = 0;

	mtk_handle_status_irq(eth);
	mtk_w32(eth, MTK_TX_DONE_INT, MTK_QMTK_INT_STATUS);
	tx_done = mtk_poll_tx(eth, budget);

	if (unlikely(netif_msg_intr(eth))) {
		status = mtk_r32(eth, MTK_QMTK_INT_STATUS);
		mask = mtk_r32(eth, MTK_QDMA_INT_MASK);
		dev_info(eth->dev,
			 "done tx %d, intr 0x%08x/0x%x\n",
			 tx_done, status, mask);
	}

	if (tx_done == budget)
		return budget;

	status = mtk_r32(eth, MTK_QMTK_INT_STATUS);
	if (status & MTK_TX_DONE_INT)
		return budget;

	napi_complete(napi);
	mtk_irq_enable(eth, MTK_QDMA_INT_MASK, MTK_TX_DONE_INT);

	return tx_done;
}

static int mtk_napi_rx(struct napi_struct *napi, int budget)
{
	struct mtk_eth *eth = container_of(napi, struct mtk_eth, rx_napi);
	u32 status, mask;
	int rx_done = 0;
	int remain_budget = budget;

	mtk_handle_status_irq(eth);

poll_again:
	mtk_w32(eth, MTK_RX_DONE_INT, MTK_PDMA_INT_STATUS);
	rx_done = mtk_poll_rx(napi, remain_budget, eth);

	if (unlikely(netif_msg_intr(eth))) {
		status = mtk_r32(eth, MTK_PDMA_INT_STATUS);
		mask = mtk_r32(eth, MTK_PDMA_INT_MASK);
		dev_info(eth->dev,
			 "done rx %d, intr 0x%08x/0x%x\n",
			 rx_done, status, mask);
	}
	if (rx_done == remain_budget)
		return budget;

	status = mtk_r32(eth, MTK_PDMA_INT_STATUS);
	if (status & MTK_RX_DONE_INT) {
		remain_budget -= rx_done;
		goto poll_again;
	}
	napi_complete(napi);
	mtk_irq_enable(eth, MTK_PDMA_INT_MASK, MTK_RX_DONE_INT);

	return rx_done + budget - remain_budget;
}

static int mtk_tx_alloc(struct mtk_eth *eth)
{
	struct mtk_tx_ring *ring = &eth->tx_ring;
	int i, sz = sizeof(*ring->dma);

	ring->buf = kcalloc(MTK_DMA_SIZE, sizeof(*ring->buf),
			       GFP_KERNEL);
	if (!ring->buf)
		goto no_tx_mem;

	ring->dma = dma_alloc_coherent(eth->dev,
					  MTK_DMA_SIZE * sz,
					  &ring->phys,
					  GFP_ATOMIC | __GFP_ZERO);
	if (!ring->dma)
		goto no_tx_mem;

	memset(ring->dma, 0, MTK_DMA_SIZE * sz);
	for (i = 0; i < MTK_DMA_SIZE; i++) {
		int next = (i + 1) % MTK_DMA_SIZE;
		u32 next_ptr = ring->phys + next * sz;

		ring->dma[i].txd2 = next_ptr;
		ring->dma[i].txd3 = TX_DMA_LS0 | TX_DMA_OWNER_CPU;
	}

	atomic_set(&ring->free_count, MTK_DMA_SIZE - 2);
	ring->next_free = &ring->dma[0];
	ring->last_free = &ring->dma[MTK_DMA_SIZE - 1];
	ring->thresh = MAX_SKB_FRAGS;

	/* make sure that all changes to the dma ring are flushed before we
	 * continue
	 */
	wmb();

	mtk_w32(eth, ring->phys, MTK_QTX_CTX_PTR);
	mtk_w32(eth, ring->phys, MTK_QTX_DTX_PTR);
	mtk_w32(eth,
		ring->phys + ((MTK_DMA_SIZE - 1) * sz),
		MTK_QTX_CRX_PTR);
	mtk_w32(eth,
		ring->phys + ((MTK_DMA_SIZE - 1) * sz),
		MTK_QTX_DRX_PTR);
	mtk_w32(eth, (QDMA_RES_THRES << 8) | QDMA_RES_THRES, MTK_QTX_CFG(0));

	return 0;

no_tx_mem:
	return -ENOMEM;
}

static void mtk_tx_clean(struct mtk_eth *eth)
{
	struct mtk_tx_ring *ring = &eth->tx_ring;
	int i;

	if (ring->buf) {
		for (i = 0; i < MTK_DMA_SIZE; i++)
			mtk_tx_unmap(eth, &ring->buf[i]);
		kfree(ring->buf);
		ring->buf = NULL;
	}

	if (ring->dma) {
		dma_free_coherent(eth->dev,
				  MTK_DMA_SIZE * sizeof(*ring->dma),
				  ring->dma,
				  ring->phys);
		ring->dma = NULL;
	}
}

static int mtk_rx_alloc(struct mtk_eth *eth, int ring_no, int rx_flag)
{
	struct mtk_rx_ring *ring = &eth->rx_ring[ring_no];
	int rx_data_len, rx_dma_size;
	int i;

	if (rx_flag == MTK_RX_FLAGS_HWLRO) {
		rx_data_len = MTK_MAX_LRO_RX_LENGTH;
		rx_dma_size = MTK_HW_LRO_DMA_SIZE;
	} else {
		rx_data_len = ETH_DATA_LEN;
		rx_dma_size = MTK_DMA_SIZE;
	}

	ring->frag_size = mtk_max_frag_size(rx_data_len);
	ring->buf_size = mtk_max_buf_size(ring->frag_size);
	ring->data = kcalloc(rx_dma_size, sizeof(*ring->data),
			     GFP_KERNEL);
	if (!ring->data)
		return -ENOMEM;

	for (i = 0; i < rx_dma_size; i++) {
		ring->data[i] = netdev_alloc_frag(ring->frag_size);
		if (!ring->data[i])
			return -ENOMEM;
	}

	ring->dma = dma_alloc_coherent(eth->dev,
				       rx_dma_size * sizeof(*ring->dma),
				       &ring->phys,
				       GFP_ATOMIC | __GFP_ZERO);
	if (!ring->dma)
		return -ENOMEM;

	for (i = 0; i < rx_dma_size; i++) {
		dma_addr_t dma_addr = dma_map_single(eth->dev,
				ring->data[i] + NET_SKB_PAD,
				ring->buf_size,
				DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(eth->dev, dma_addr)))
			return -ENOMEM;
		ring->dma[i].rxd1 = (unsigned int)dma_addr;

		ring->dma[i].rxd2 = RX_DMA_PLEN0(ring->buf_size);
	}
	ring->dma_size = rx_dma_size;
	ring->calc_idx_update = false;
	ring->calc_idx = rx_dma_size - 1;
	ring->crx_idx_reg = MTK_PRX_CRX_IDX_CFG(ring_no);
	/* make sure that all changes to the dma ring are flushed before we
	 * continue
	 */
	wmb();

	mtk_w32(eth, ring->phys, MTK_PRX_BASE_PTR_CFG(ring_no));
	mtk_w32(eth, rx_dma_size, MTK_PRX_MAX_CNT_CFG(ring_no));
	mtk_w32(eth, ring->calc_idx, ring->crx_idx_reg);
	mtk_w32(eth, MTK_PST_DRX_IDX_CFG(ring_no), MTK_PDMA_RST_IDX);

	return 0;
}

static void mtk_rx_clean(struct mtk_eth *eth, int ring_no)
{
	struct mtk_rx_ring *ring = &eth->rx_ring[ring_no];
	int i;

	if (ring->data && ring->dma) {
		for (i = 0; i < ring->dma_size; i++) {
			if (!ring->data[i])
				continue;
			if (!ring->dma[i].rxd1)
				continue;
			dma_unmap_single(eth->dev,
					 ring->dma[i].rxd1,
					 ring->buf_size,
					 DMA_FROM_DEVICE);
			skb_free_frag(ring->data[i]);
		}
		kfree(ring->data);
		ring->data = NULL;
	}

	if (ring->dma) {
		dma_free_coherent(eth->dev,
				  ring->dma_size * sizeof(*ring->dma),
				  ring->dma,
				  ring->phys);
		ring->dma = NULL;
	}
}

static int mtk_hwlro_rx_init(struct mtk_eth *eth)
{
	int i;
	u32 ring_ctrl_dw1 = 0, ring_ctrl_dw2 = 0, ring_ctrl_dw3 = 0;
	u32 lro_ctrl_dw0 = 0, lro_ctrl_dw3 = 0;

	/* set LRO rings to auto-learn modes */
	ring_ctrl_dw2 |= MTK_RING_AUTO_LERAN_MODE;

	/* validate LRO ring */
	ring_ctrl_dw2 |= MTK_RING_VLD;

	/* set AGE timer (unit: 20us) */
	ring_ctrl_dw2 |= MTK_RING_AGE_TIME_H;
	ring_ctrl_dw1 |= MTK_RING_AGE_TIME_L;

	/* set max AGG timer (unit: 20us) */
	ring_ctrl_dw2 |= MTK_RING_MAX_AGG_TIME;

	/* set max LRO AGG count */
	ring_ctrl_dw2 |= MTK_RING_MAX_AGG_CNT_L;
	ring_ctrl_dw3 |= MTK_RING_MAX_AGG_CNT_H;

	for (i = 1; i < MTK_MAX_RX_RING_NUM; i++) {
		mtk_w32(eth, ring_ctrl_dw1, MTK_LRO_CTRL_DW1_CFG(i));
		mtk_w32(eth, ring_ctrl_dw2, MTK_LRO_CTRL_DW2_CFG(i));
		mtk_w32(eth, ring_ctrl_dw3, MTK_LRO_CTRL_DW3_CFG(i));
	}

	/* IPv4 checksum update enable */
	lro_ctrl_dw0 |= MTK_L3_CKS_UPD_EN;

	/* switch priority comparison to packet count mode */
	lro_ctrl_dw0 |= MTK_LRO_ALT_PKT_CNT_MODE;

	/* bandwidth threshold setting */
	mtk_w32(eth, MTK_HW_LRO_BW_THRE, MTK_PDMA_LRO_CTRL_DW2);

	/* auto-learn score delta setting */
	mtk_w32(eth, MTK_HW_LRO_REPLACE_DELTA, MTK_PDMA_LRO_ALT_SCORE_DELTA);

	/* set refresh timer for altering flows to 1 sec. (unit: 20us) */
	mtk_w32(eth, (MTK_HW_LRO_TIMER_UNIT << 16) | MTK_HW_LRO_REFRESH_TIME,
		MTK_PDMA_LRO_ALT_REFRESH_TIMER);

	/* set HW LRO mode & the max aggregation count for rx packets */
	lro_ctrl_dw3 |= MTK_ADMA_MODE | (MTK_HW_LRO_MAX_AGG_CNT & 0xff);

	/* the minimal remaining room of SDL0 in RXD for lro aggregation */
	lro_ctrl_dw3 |= MTK_LRO_MIN_RXD_SDL;

	/* enable HW LRO */
	lro_ctrl_dw0 |= MTK_LRO_EN;

	mtk_w32(eth, lro_ctrl_dw3, MTK_PDMA_LRO_CTRL_DW3);
	mtk_w32(eth, lro_ctrl_dw0, MTK_PDMA_LRO_CTRL_DW0);

	return 0;
}

static void mtk_hwlro_rx_uninit(struct mtk_eth *eth)
{
	int i;
	u32 val;

	/* relinquish lro rings, flush aggregated packets */
	mtk_w32(eth, MTK_LRO_RING_RELINQUISH_REQ, MTK_PDMA_LRO_CTRL_DW0);

	/* wait for relinquishments done */
	for (i = 0; i < 10; i++) {
		val = mtk_r32(eth, MTK_PDMA_LRO_CTRL_DW0);
		if (val & MTK_LRO_RING_RELINQUISH_DONE) {
			msleep(20);
			continue;
		}
		break;
	}

	/* invalidate lro rings */
	for (i = 1; i < MTK_MAX_RX_RING_NUM; i++)
		mtk_w32(eth, 0, MTK_LRO_CTRL_DW2_CFG(i));

	/* disable HW LRO */
	mtk_w32(eth, 0, MTK_PDMA_LRO_CTRL_DW0);
}

static void mtk_hwlro_val_ipaddr(struct mtk_eth *eth, int idx, __be32 ip)
{
	u32 reg_val;

	reg_val = mtk_r32(eth, MTK_LRO_CTRL_DW2_CFG(idx));

	/* invalidate the IP setting */
	mtk_w32(eth, (reg_val & ~MTK_RING_MYIP_VLD), MTK_LRO_CTRL_DW2_CFG(idx));

	mtk_w32(eth, ip, MTK_LRO_DIP_DW0_CFG(idx));

	/* validate the IP setting */
	mtk_w32(eth, (reg_val | MTK_RING_MYIP_VLD), MTK_LRO_CTRL_DW2_CFG(idx));
}

static void mtk_hwlro_inval_ipaddr(struct mtk_eth *eth, int idx)
{
	u32 reg_val;

	reg_val = mtk_r32(eth, MTK_LRO_CTRL_DW2_CFG(idx));

	/* invalidate the IP setting */
	mtk_w32(eth, (reg_val & ~MTK_RING_MYIP_VLD), MTK_LRO_CTRL_DW2_CFG(idx));

	mtk_w32(eth, 0, MTK_LRO_DIP_DW0_CFG(idx));
}

static int mtk_hwlro_get_ip_cnt(struct mtk_mac *mac)
{
	int cnt = 0;
	int i;

	for (i = 0; i < MTK_MAX_LRO_IP_CNT; i++) {
		if (mac->hwlro_ip[i])
			cnt++;
	}

	return cnt;
}

static int mtk_hwlro_add_ipaddr(struct net_device *dev,
				struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	int hwlro_idx;

	if ((fsp->flow_type != TCP_V4_FLOW) ||
	    (!fsp->h_u.tcp_ip4_spec.ip4dst) ||
	    (fsp->location > 1))
		return -EINVAL;

	mac->hwlro_ip[fsp->location] = htonl(fsp->h_u.tcp_ip4_spec.ip4dst);
	hwlro_idx = (mac->id * MTK_MAX_LRO_IP_CNT) + fsp->location;

	mac->hwlro_ip_cnt = mtk_hwlro_get_ip_cnt(mac);

	mtk_hwlro_val_ipaddr(eth, hwlro_idx, mac->hwlro_ip[fsp->location]);

	return 0;
}

static int mtk_hwlro_del_ipaddr(struct net_device *dev,
				struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	int hwlro_idx;

	if (fsp->location > 1)
		return -EINVAL;

	mac->hwlro_ip[fsp->location] = 0;
	hwlro_idx = (mac->id * MTK_MAX_LRO_IP_CNT) + fsp->location;

	mac->hwlro_ip_cnt = mtk_hwlro_get_ip_cnt(mac);

	mtk_hwlro_inval_ipaddr(eth, hwlro_idx);

	return 0;
}

static void mtk_hwlro_netdev_disable(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	int i, hwlro_idx;

	for (i = 0; i < MTK_MAX_LRO_IP_CNT; i++) {
		mac->hwlro_ip[i] = 0;
		hwlro_idx = (mac->id * MTK_MAX_LRO_IP_CNT) + i;

		mtk_hwlro_inval_ipaddr(eth, hwlro_idx);
	}

	mac->hwlro_ip_cnt = 0;
}

static int mtk_hwlro_get_fdir_entry(struct net_device *dev,
				    struct ethtool_rxnfc *cmd)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct ethtool_rx_flow_spec *fsp =
		(struct ethtool_rx_flow_spec *)&cmd->fs;

	/* only tcp dst ipv4 is meaningful, others are meaningless */
	fsp->flow_type = TCP_V4_FLOW;
	fsp->h_u.tcp_ip4_spec.ip4dst = ntohl(mac->hwlro_ip[fsp->location]);
	fsp->m_u.tcp_ip4_spec.ip4dst = 0;

	fsp->h_u.tcp_ip4_spec.ip4src = 0;
	fsp->m_u.tcp_ip4_spec.ip4src = 0xffffffff;
	fsp->h_u.tcp_ip4_spec.psrc = 0;
	fsp->m_u.tcp_ip4_spec.psrc = 0xffff;
	fsp->h_u.tcp_ip4_spec.pdst = 0;
	fsp->m_u.tcp_ip4_spec.pdst = 0xffff;
	fsp->h_u.tcp_ip4_spec.tos = 0;
	fsp->m_u.tcp_ip4_spec.tos = 0xff;

	return 0;
}

static int mtk_hwlro_get_fdir_all(struct net_device *dev,
				  struct ethtool_rxnfc *cmd,
				  u32 *rule_locs)
{
	struct mtk_mac *mac = netdev_priv(dev);
	int cnt = 0;
	int i;

	for (i = 0; i < MTK_MAX_LRO_IP_CNT; i++) {
		if (mac->hwlro_ip[i]) {
			rule_locs[cnt] = i;
			cnt++;
		}
	}

	cmd->rule_cnt = cnt;

	return 0;
}

static netdev_features_t mtk_fix_features(struct net_device *dev,
					  netdev_features_t features)
{
	if (!(features & NETIF_F_LRO)) {
		struct mtk_mac *mac = netdev_priv(dev);
		int ip_cnt = mtk_hwlro_get_ip_cnt(mac);

		if (ip_cnt) {
			netdev_info(dev, "RX flow is programmed, LRO should keep on\n");

			features |= NETIF_F_LRO;
		}
	}

	return features;
}

static int mtk_set_features(struct net_device *dev, netdev_features_t features)
{
	int err = 0;

	if (!((dev->features ^ features) & NETIF_F_LRO))
		return 0;

	if (!(features & NETIF_F_LRO))
		mtk_hwlro_netdev_disable(dev);

	return err;
}

/* wait for DMA to finish whatever it is doing before we start using it again */
static int mtk_dma_busy_wait(struct mtk_eth *eth)
{
	unsigned long t_start = jiffies;

	while (1) {
		if (!(mtk_r32(eth, MTK_QDMA_GLO_CFG) &
		      (MTK_RX_DMA_BUSY | MTK_TX_DMA_BUSY)))
			return 0;
		if (time_after(jiffies, t_start + MTK_DMA_BUSY_TIMEOUT))
			break;
	}

	dev_err(eth->dev, "DMA init timeout\n");
	return -1;
}

static int mtk_dma_init(struct mtk_eth *eth)
{
	int err;
	u32 i;

	if (mtk_dma_busy_wait(eth))
		return -EBUSY;

	/* QDMA needs scratch memory for internal reordering of the
	 * descriptors
	 */
	err = mtk_init_fq_dma(eth);
	if (err)
		return err;

	err = mtk_tx_alloc(eth);
	if (err)
		return err;

	err = mtk_rx_alloc(eth, 0, MTK_RX_FLAGS_NORMAL);
	if (err)
		return err;

	if (eth->hwlro) {
		for (i = 1; i < MTK_MAX_RX_RING_NUM; i++) {
			err = mtk_rx_alloc(eth, i, MTK_RX_FLAGS_HWLRO);
			if (err)
				return err;
		}
		err = mtk_hwlro_rx_init(eth);
		if (err)
			return err;
	}

	/* Enable random early drop and set drop threshold automatically */
	mtk_w32(eth, FC_THRES_DROP_MODE | FC_THRES_DROP_EN | FC_THRES_MIN,
		MTK_QDMA_FC_THRES);
	mtk_w32(eth, 0x0, MTK_QDMA_HRED2);

	return 0;
}

static void mtk_dma_free(struct mtk_eth *eth)
{
	int i;

	for (i = 0; i < MTK_MAC_COUNT; i++)
		if (eth->netdev[i])
			netdev_reset_queue(eth->netdev[i]);
	if (eth->scratch_ring) {
		dma_free_coherent(eth->dev,
				  MTK_DMA_SIZE * sizeof(struct mtk_tx_dma),
				  eth->scratch_ring,
				  eth->phy_scratch_ring);
		eth->scratch_ring = NULL;
		eth->phy_scratch_ring = 0;
	}
	mtk_tx_clean(eth);
	mtk_rx_clean(eth, 0);

	if (eth->hwlro) {
		mtk_hwlro_rx_uninit(eth);
		for (i = 1; i < MTK_MAX_RX_RING_NUM; i++)
			mtk_rx_clean(eth, i);
	}

	kfree(eth->scratch_head);
}

static void mtk_tx_timeout(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;

	eth->netdev[mac->id]->stats.tx_errors++;
	netif_err(eth, tx_err, dev,
		  "transmit timed out\n");
	schedule_work(&eth->pending_work);
}

static irqreturn_t mtk_handle_irq_rx(int irq, void *_eth)
{
	struct mtk_eth *eth = _eth;

	if (likely(napi_schedule_prep(&eth->rx_napi))) {
		__napi_schedule(&eth->rx_napi);
		mtk_irq_disable(eth, MTK_PDMA_INT_MASK, MTK_RX_DONE_INT);
	}

	return IRQ_HANDLED;
}

static irqreturn_t mtk_handle_irq_tx(int irq, void *_eth)
{
	struct mtk_eth *eth = _eth;

	if (likely(napi_schedule_prep(&eth->tx_napi))) {
		__napi_schedule(&eth->tx_napi);
		mtk_irq_disable(eth, MTK_QDMA_INT_MASK, MTK_TX_DONE_INT);
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void mtk_poll_controller(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;

	mtk_irq_disable(eth, MTK_QDMA_INT_MASK, MTK_TX_DONE_INT);
	mtk_irq_disable(eth, MTK_PDMA_INT_MASK, MTK_RX_DONE_INT);
	mtk_handle_irq_rx(eth->irq[2], dev);
	mtk_irq_enable(eth, MTK_QDMA_INT_MASK, MTK_TX_DONE_INT);
	mtk_irq_enable(eth, MTK_PDMA_INT_MASK, MTK_RX_DONE_INT);
}
#endif

static int mtk_start_dma(struct mtk_eth *eth)
{
	int err;

	err = mtk_dma_init(eth);
	if (err) {
		mtk_dma_free(eth);
		return err;
	}

	mtk_w32(eth,
		MTK_TX_WB_DDONE | MTK_TX_DMA_EN |
		MTK_DMA_SIZE_16DWORDS | MTK_NDP_CO_PRO,
		MTK_QDMA_GLO_CFG);

	mtk_w32(eth,
		MTK_RX_DMA_EN | MTK_RX_2B_OFFSET |
		MTK_RX_BT_32DWORDS | MTK_MULTI_EN,
		MTK_PDMA_GLO_CFG);

	return 0;
}

static int mtk_open(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;

	/* we run 2 netdevs on the same dma ring so we only bring it up once */
	if (!atomic_read(&eth->dma_refcnt)) {
		int err = mtk_start_dma(eth);

		if (err)
			return err;

		napi_enable(&eth->tx_napi);
		napi_enable(&eth->rx_napi);
		mtk_irq_enable(eth, MTK_QDMA_INT_MASK, MTK_TX_DONE_INT);
		mtk_irq_enable(eth, MTK_PDMA_INT_MASK, MTK_RX_DONE_INT);
	}
	atomic_inc(&eth->dma_refcnt);

	phy_start(dev->phydev);
	netif_start_queue(dev);

	return 0;
}

static void mtk_stop_dma(struct mtk_eth *eth, u32 glo_cfg)
{
	u32 val;
	int i;

	/* stop the dma engine */
	spin_lock_bh(&eth->page_lock);
	val = mtk_r32(eth, glo_cfg);
	mtk_w32(eth, val & ~(MTK_TX_WB_DDONE | MTK_RX_DMA_EN | MTK_TX_DMA_EN),
		glo_cfg);
	spin_unlock_bh(&eth->page_lock);

	/* wait for dma stop */
	for (i = 0; i < 10; i++) {
		val = mtk_r32(eth, glo_cfg);
		if (val & (MTK_TX_DMA_BUSY | MTK_RX_DMA_BUSY)) {
			msleep(20);
			continue;
		}
		break;
	}
}

static int mtk_stop(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;

	netif_tx_disable(dev);
	phy_stop(dev->phydev);

	/* only shutdown DMA if this is the last user */
	if (!atomic_dec_and_test(&eth->dma_refcnt))
		return 0;

	mtk_irq_disable(eth, MTK_QDMA_INT_MASK, MTK_TX_DONE_INT);
	mtk_irq_disable(eth, MTK_PDMA_INT_MASK, MTK_RX_DONE_INT);
	napi_disable(&eth->tx_napi);
	napi_disable(&eth->rx_napi);

	mtk_stop_dma(eth, MTK_QDMA_GLO_CFG);
	mtk_stop_dma(eth, MTK_PDMA_GLO_CFG);

	mtk_dma_free(eth);

	return 0;
}

static void ethsys_reset(struct mtk_eth *eth, u32 reset_bits)
{
	regmap_update_bits(eth->ethsys, ETHSYS_RSTCTRL,
			   reset_bits,
			   reset_bits);

	usleep_range(1000, 1100);
	regmap_update_bits(eth->ethsys, ETHSYS_RSTCTRL,
			   reset_bits,
			   ~reset_bits);
	mdelay(10);
}

static int mtk_hw_init(struct mtk_eth *eth)
{
	int i, val;

	if (test_and_set_bit(MTK_HW_INIT, &eth->state))
		return 0;

	pm_runtime_enable(eth->dev);
	pm_runtime_get_sync(eth->dev);

	clk_prepare_enable(eth->clks[MTK_CLK_ETHIF]);
	clk_prepare_enable(eth->clks[MTK_CLK_ESW]);
	clk_prepare_enable(eth->clks[MTK_CLK_GP1]);
	clk_prepare_enable(eth->clks[MTK_CLK_GP2]);
	ethsys_reset(eth, RSTCTRL_FE);
	ethsys_reset(eth, RSTCTRL_PPE);

	regmap_read(eth->ethsys, ETHSYS_SYSCFG0, &val);
	for (i = 0; i < MTK_MAC_COUNT; i++) {
		if (!eth->mac[i])
			continue;
		val &= ~SYSCFG0_GE_MODE(SYSCFG0_GE_MASK, eth->mac[i]->id);
		val |= SYSCFG0_GE_MODE(eth->mac[i]->ge_mode, eth->mac[i]->id);
	}
	regmap_write(eth->ethsys, ETHSYS_SYSCFG0, val);

	/* Set GE2 driving and slew rate */
	regmap_write(eth->pctl, GPIO_DRV_SEL10, 0xa00);

	/* set GE2 TDSEL */
	regmap_write(eth->pctl, GPIO_OD33_CTRL8, 0x5);

	/* set GE2 TUNE */
	regmap_write(eth->pctl, GPIO_BIAS_CTRL, 0x0);

	/* GE1, Force 1000M/FD, FC ON */
	mtk_w32(eth, MAC_MCR_FIXED_LINK, MTK_MAC_MCR(0));

	/* GE2, Force 1000M/FD, FC ON */
	mtk_w32(eth, MAC_MCR_FIXED_LINK, MTK_MAC_MCR(1));

	/* Enable RX VLan Offloading */
	mtk_w32(eth, 1, MTK_CDMP_EG_CTRL);

	/* disable delay and normal interrupt */
	mtk_w32(eth, 0, MTK_QDMA_DELAY_INT);
	mtk_w32(eth, 0, MTK_PDMA_DELAY_INT);
	mtk_irq_disable(eth, MTK_QDMA_INT_MASK, ~0);
	mtk_irq_disable(eth, MTK_PDMA_INT_MASK, ~0);
	mtk_w32(eth, RST_GL_PSE, MTK_RST_GL);
	mtk_w32(eth, 0, MTK_RST_GL);

	/* FE int grouping */
	mtk_w32(eth, MTK_TX_DONE_INT, MTK_PDMA_INT_GRP1);
	mtk_w32(eth, MTK_RX_DONE_INT, MTK_PDMA_INT_GRP2);
	mtk_w32(eth, MTK_TX_DONE_INT, MTK_QDMA_INT_GRP1);
	mtk_w32(eth, MTK_RX_DONE_INT, MTK_QDMA_INT_GRP2);
	mtk_w32(eth, 0x21021000, MTK_FE_INT_GRP);

	for (i = 0; i < 2; i++) {
		u32 val = mtk_r32(eth, MTK_GDMA_FWD_CFG(i));

		/* setup the forward port to send frame to PDMA */
		val &= ~0xffff;

		/* Enable RX checksum */
		val |= MTK_GDMA_ICS_EN | MTK_GDMA_TCS_EN | MTK_GDMA_UCS_EN;

		/* setup the mac dma */
		mtk_w32(eth, val, MTK_GDMA_FWD_CFG(i));
	}

	return 0;
}

static int mtk_hw_deinit(struct mtk_eth *eth)
{
	if (!test_and_clear_bit(MTK_HW_INIT, &eth->state))
		return 0;

	clk_disable_unprepare(eth->clks[MTK_CLK_GP2]);
	clk_disable_unprepare(eth->clks[MTK_CLK_GP1]);
	clk_disable_unprepare(eth->clks[MTK_CLK_ESW]);
	clk_disable_unprepare(eth->clks[MTK_CLK_ETHIF]);

	pm_runtime_put_sync(eth->dev);
	pm_runtime_disable(eth->dev);

	return 0;
}

static int __init mtk_init(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	const char *mac_addr;

	mac_addr = of_get_mac_address(mac->of_node);
	if (mac_addr)
		ether_addr_copy(dev->dev_addr, mac_addr);

	/* If the mac address is invalid, use random mac address  */
	if (!is_valid_ether_addr(dev->dev_addr)) {
		random_ether_addr(dev->dev_addr);
		dev_err(eth->dev, "generated random MAC address %pM\n",
			dev->dev_addr);
		dev->addr_assign_type = NET_ADDR_RANDOM;
	}

	return mtk_phy_connect(dev);
}

static void mtk_uninit(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;

	phy_disconnect(dev->phydev);
	mtk_irq_disable(eth, MTK_QDMA_INT_MASK, ~0);
	mtk_irq_disable(eth, MTK_PDMA_INT_MASK, ~0);
}

static int mtk_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return phy_mii_ioctl(dev->phydev, ifr, cmd);
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static void mtk_pending_work(struct work_struct *work)
{
	struct mtk_eth *eth = container_of(work, struct mtk_eth, pending_work);
	int err, i;
	unsigned long restart = 0;

	rtnl_lock();

	dev_dbg(eth->dev, "[%s][%d] reset\n", __func__, __LINE__);

	while (test_and_set_bit_lock(MTK_RESETTING, &eth->state))
		cpu_relax();

	dev_dbg(eth->dev, "[%s][%d] mtk_stop starts\n", __func__, __LINE__);
	/* stop all devices to make sure that dma is properly shut down */
	for (i = 0; i < MTK_MAC_COUNT; i++) {
		if (!eth->netdev[i])
			continue;
		mtk_stop(eth->netdev[i]);
		__set_bit(i, &restart);
	}
	dev_dbg(eth->dev, "[%s][%d] mtk_stop ends\n", __func__, __LINE__);

	/* restart underlying hardware such as power, clock, pin mux
	 * and the connected phy
	 */
	mtk_hw_deinit(eth);

	if (eth->dev->pins)
		pinctrl_select_state(eth->dev->pins->p,
				     eth->dev->pins->default_state);
	mtk_hw_init(eth);

	for (i = 0; i < MTK_MAC_COUNT; i++) {
		if (!eth->mac[i] ||
		    of_phy_is_fixed_link(eth->mac[i]->of_node))
			continue;
		err = phy_init_hw(eth->netdev[i]->phydev);
		if (err)
			dev_err(eth->dev, "%s: PHY init failed.\n",
				eth->netdev[i]->name);
	}

	/* restart DMA and enable IRQs */
	for (i = 0; i < MTK_MAC_COUNT; i++) {
		if (!test_bit(i, &restart))
			continue;
		err = mtk_open(eth->netdev[i]);
		if (err) {
			netif_alert(eth, ifup, eth->netdev[i],
			      "Driver up/down cycle failed, closing device.\n");
			dev_close(eth->netdev[i]);
		}
	}

	dev_dbg(eth->dev, "[%s][%d] reset done\n", __func__, __LINE__);

	clear_bit_unlock(MTK_RESETTING, &eth->state);

	rtnl_unlock();
}

static int mtk_free_dev(struct mtk_eth *eth)
{
	int i;

	for (i = 0; i < MTK_MAC_COUNT; i++) {
		if (!eth->netdev[i])
			continue;
		free_netdev(eth->netdev[i]);
	}

	return 0;
}

static int mtk_unreg_dev(struct mtk_eth *eth)
{
	int i;

	for (i = 0; i < MTK_MAC_COUNT; i++) {
		if (!eth->netdev[i])
			continue;
		unregister_netdev(eth->netdev[i]);
	}

	return 0;
}

static int mtk_cleanup(struct mtk_eth *eth)
{
	mtk_unreg_dev(eth);
	mtk_free_dev(eth);
	cancel_work_sync(&eth->pending_work);

	return 0;
}

static int mtk_get_link_ksettings(struct net_device *ndev,
				  struct ethtool_link_ksettings *cmd)
{
	struct mtk_mac *mac = netdev_priv(ndev);

	if (unlikely(test_bit(MTK_RESETTING, &mac->hw->state)))
		return -EBUSY;

	return phy_ethtool_ksettings_get(ndev->phydev, cmd);
}

static int mtk_set_link_ksettings(struct net_device *ndev,
				  const struct ethtool_link_ksettings *cmd)
{
	struct mtk_mac *mac = netdev_priv(ndev);

	if (unlikely(test_bit(MTK_RESETTING, &mac->hw->state)))
		return -EBUSY;

	return phy_ethtool_ksettings_set(ndev->phydev, cmd);
}

static void mtk_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info)
{
	struct mtk_mac *mac = netdev_priv(dev);

	strlcpy(info->driver, mac->hw->dev->driver->name, sizeof(info->driver));
	strlcpy(info->bus_info, dev_name(mac->hw->dev), sizeof(info->bus_info));
	info->n_stats = ARRAY_SIZE(mtk_ethtool_stats);
}

static u32 mtk_get_msglevel(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);

	return mac->hw->msg_enable;
}

static void mtk_set_msglevel(struct net_device *dev, u32 value)
{
	struct mtk_mac *mac = netdev_priv(dev);

	mac->hw->msg_enable = value;
}

static int mtk_nway_reset(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);

	if (unlikely(test_bit(MTK_RESETTING, &mac->hw->state)))
		return -EBUSY;

	return genphy_restart_aneg(dev->phydev);
}

static u32 mtk_get_link(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	int err;

	if (unlikely(test_bit(MTK_RESETTING, &mac->hw->state)))
		return -EBUSY;

	err = genphy_update_link(dev->phydev);
	if (err)
		return ethtool_op_get_link(dev);

	return dev->phydev->link;
}

static void mtk_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(mtk_ethtool_stats); i++) {
			memcpy(data, mtk_ethtool_stats[i].str, ETH_GSTRING_LEN);
			data += ETH_GSTRING_LEN;
		}
		break;
	}
}

static int mtk_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(mtk_ethtool_stats);
	default:
		return -EOPNOTSUPP;
	}
}

static void mtk_get_ethtool_stats(struct net_device *dev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_hw_stats *hwstats = mac->hw_stats;
	u64 *data_src, *data_dst;
	unsigned int start;
	int i;

	if (unlikely(test_bit(MTK_RESETTING, &mac->hw->state)))
		return;

	if (netif_running(dev) && netif_device_present(dev)) {
		if (spin_trylock(&hwstats->stats_lock)) {
			mtk_stats_update_mac(mac);
			spin_unlock(&hwstats->stats_lock);
		}
	}

	data_src = (u64 *)hwstats;

	do {
		data_dst = data;
		start = u64_stats_fetch_begin_irq(&hwstats->syncp);

		for (i = 0; i < ARRAY_SIZE(mtk_ethtool_stats); i++)
			*data_dst++ = *(data_src + mtk_ethtool_stats[i].offset);
	} while (u64_stats_fetch_retry_irq(&hwstats->syncp, start));
}

static int mtk_get_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd,
			 u32 *rule_locs)
{
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		if (dev->features & NETIF_F_LRO) {
			cmd->data = MTK_MAX_RX_RING_NUM;
			ret = 0;
		}
		break;
	case ETHTOOL_GRXCLSRLCNT:
		if (dev->features & NETIF_F_LRO) {
			struct mtk_mac *mac = netdev_priv(dev);

			cmd->rule_cnt = mac->hwlro_ip_cnt;
			ret = 0;
		}
		break;
	case ETHTOOL_GRXCLSRULE:
		if (dev->features & NETIF_F_LRO)
			ret = mtk_hwlro_get_fdir_entry(dev, cmd);
		break;
	case ETHTOOL_GRXCLSRLALL:
		if (dev->features & NETIF_F_LRO)
			ret = mtk_hwlro_get_fdir_all(dev, cmd,
						     rule_locs);
		break;
	default:
		break;
	}

	return ret;
}

static int mtk_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *cmd)
{
	int ret = -EOPNOTSUPP;

	switch (cmd->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		if (dev->features & NETIF_F_LRO)
			ret = mtk_hwlro_add_ipaddr(dev, cmd);
		break;
	case ETHTOOL_SRXCLSRLDEL:
		if (dev->features & NETIF_F_LRO)
			ret = mtk_hwlro_del_ipaddr(dev, cmd);
		break;
	default:
		break;
	}

	return ret;
}

static const struct ethtool_ops mtk_ethtool_ops = {
	.get_link_ksettings	= mtk_get_link_ksettings,
	.set_link_ksettings	= mtk_set_link_ksettings,
	.get_drvinfo		= mtk_get_drvinfo,
	.get_msglevel		= mtk_get_msglevel,
	.set_msglevel		= mtk_set_msglevel,
	.nway_reset		= mtk_nway_reset,
	.get_link		= mtk_get_link,
	.get_strings		= mtk_get_strings,
	.get_sset_count		= mtk_get_sset_count,
	.get_ethtool_stats	= mtk_get_ethtool_stats,
	.get_rxnfc		= mtk_get_rxnfc,
	.set_rxnfc              = mtk_set_rxnfc,
};

static const struct net_device_ops mtk_netdev_ops = {
	.ndo_init		= mtk_init,
	.ndo_uninit		= mtk_uninit,
	.ndo_open		= mtk_open,
	.ndo_stop		= mtk_stop,
	.ndo_start_xmit		= mtk_start_xmit,
	.ndo_set_mac_address	= mtk_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= mtk_do_ioctl,
	.ndo_tx_timeout		= mtk_tx_timeout,
	.ndo_get_stats64        = mtk_get_stats64,
	.ndo_fix_features	= mtk_fix_features,
	.ndo_set_features	= mtk_set_features,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= mtk_poll_controller,
#endif
};

static int mtk_add_mac(struct mtk_eth *eth, struct device_node *np)
{
	struct mtk_mac *mac;
	const __be32 *_id = of_get_property(np, "reg", NULL);
	int id, err;

	if (!_id) {
		dev_err(eth->dev, "missing mac id\n");
		return -EINVAL;
	}

	id = be32_to_cpup(_id);
	if (id >= MTK_MAC_COUNT) {
		dev_err(eth->dev, "%d is not a valid mac id\n", id);
		return -EINVAL;
	}

	if (eth->netdev[id]) {
		dev_err(eth->dev, "duplicate mac id found: %d\n", id);
		return -EINVAL;
	}

	eth->netdev[id] = alloc_etherdev(sizeof(*mac));
	if (!eth->netdev[id]) {
		dev_err(eth->dev, "alloc_etherdev failed\n");
		return -ENOMEM;
	}
	mac = netdev_priv(eth->netdev[id]);
	eth->mac[id] = mac;
	mac->id = id;
	mac->hw = eth;
	mac->of_node = np;

	memset(mac->hwlro_ip, 0, sizeof(mac->hwlro_ip));
	mac->hwlro_ip_cnt = 0;

	mac->hw_stats = devm_kzalloc(eth->dev,
				     sizeof(*mac->hw_stats),
				     GFP_KERNEL);
	if (!mac->hw_stats) {
		dev_err(eth->dev, "failed to allocate counter memory\n");
		err = -ENOMEM;
		goto free_netdev;
	}
	spin_lock_init(&mac->hw_stats->stats_lock);
	u64_stats_init(&mac->hw_stats->syncp);
	mac->hw_stats->reg_offset = id * MTK_STAT_OFFSET;

	SET_NETDEV_DEV(eth->netdev[id], eth->dev);
	eth->netdev[id]->watchdog_timeo = 5 * HZ;
	eth->netdev[id]->netdev_ops = &mtk_netdev_ops;
	eth->netdev[id]->base_addr = (unsigned long)eth->base;

	eth->netdev[id]->hw_features = MTK_HW_FEATURES;
	if (eth->hwlro)
		eth->netdev[id]->hw_features |= NETIF_F_LRO;

	eth->netdev[id]->vlan_features = MTK_HW_FEATURES &
		~(NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX);
	eth->netdev[id]->features |= MTK_HW_FEATURES;
	eth->netdev[id]->ethtool_ops = &mtk_ethtool_ops;

	eth->netdev[id]->irq = eth->irq[0];
	return 0;

free_netdev:
	free_netdev(eth->netdev[id]);
	return err;
}

static int mtk_get_chip_id(struct mtk_eth *eth, u32 *chip_id)
{
	u32 val[2], id[4];

	regmap_read(eth->ethsys, ETHSYS_CHIPID0_3, &val[0]);
	regmap_read(eth->ethsys, ETHSYS_CHIPID4_7, &val[1]);

	id[3] = ((val[0] >> 16) & 0xff) - '0';
	id[2] = ((val[0] >> 24) & 0xff) - '0';
	id[1] = (val[1] & 0xff) - '0';
	id[0] = ((val[1] >> 8) & 0xff) - '0';

	*chip_id = (id[3] * 1000) + (id[2] * 100) +
		   (id[1] * 10) + id[0];

	if (!(*chip_id)) {
		dev_err(eth->dev, "failed to get chip id\n");
		return -ENODEV;
	}

	dev_info(eth->dev, "chip id = %d\n", *chip_id);

	return 0;
}

static bool mtk_is_hwlro_supported(struct mtk_eth *eth)
{
	switch (eth->chip_id) {
	case MT7623_ETH:
		return true;
	}

	return false;
}

static int mtk_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct device_node *mac_np;
	const struct of_device_id *match;
	struct mtk_soc_data *soc;
	struct mtk_eth *eth;
	int err;
	int i;

	match = of_match_device(of_mtk_match, &pdev->dev);
	soc = (struct mtk_soc_data *)match->data;

	eth = devm_kzalloc(&pdev->dev, sizeof(*eth), GFP_KERNEL);
	if (!eth)
		return -ENOMEM;

	eth->dev = &pdev->dev;
	eth->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(eth->base))
		return PTR_ERR(eth->base);

	spin_lock_init(&eth->page_lock);
	spin_lock_init(&eth->irq_lock);

	eth->ethsys = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						      "mediatek,ethsys");
	if (IS_ERR(eth->ethsys)) {
		dev_err(&pdev->dev, "no ethsys regmap found\n");
		return PTR_ERR(eth->ethsys);
	}

	eth->pctl = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						    "mediatek,pctl");
	if (IS_ERR(eth->pctl)) {
		dev_err(&pdev->dev, "no pctl regmap found\n");
		return PTR_ERR(eth->pctl);
	}

	for (i = 0; i < 3; i++) {
		eth->irq[i] = platform_get_irq(pdev, i);
		if (eth->irq[i] < 0) {
			dev_err(&pdev->dev, "no IRQ%d resource found\n", i);
			return -ENXIO;
		}
	}
	for (i = 0; i < ARRAY_SIZE(eth->clks); i++) {
		eth->clks[i] = devm_clk_get(eth->dev,
					    mtk_clks_source_name[i]);
		if (IS_ERR(eth->clks[i])) {
			if (PTR_ERR(eth->clks[i]) == -EPROBE_DEFER)
				return -EPROBE_DEFER;
			return -ENODEV;
		}
	}

	eth->msg_enable = netif_msg_init(mtk_msg_level, MTK_DEFAULT_MSG_ENABLE);
	INIT_WORK(&eth->pending_work, mtk_pending_work);

	err = mtk_hw_init(eth);
	if (err)
		return err;

	err = mtk_get_chip_id(eth, &eth->chip_id);
	if (err)
		return err;

	eth->hwlro = mtk_is_hwlro_supported(eth);

	for_each_child_of_node(pdev->dev.of_node, mac_np) {
		if (!of_device_is_compatible(mac_np,
					     "mediatek,eth-mac"))
			continue;

		if (!of_device_is_available(mac_np))
			continue;

		err = mtk_add_mac(eth, mac_np);
		if (err)
			goto err_deinit_hw;
	}

	err = devm_request_irq(eth->dev, eth->irq[1], mtk_handle_irq_tx, 0,
			       dev_name(eth->dev), eth);
	if (err)
		goto err_free_dev;

	err = devm_request_irq(eth->dev, eth->irq[2], mtk_handle_irq_rx, 0,
			       dev_name(eth->dev), eth);
	if (err)
		goto err_free_dev;

	err = mtk_mdio_init(eth);
	if (err)
		goto err_free_dev;

	for (i = 0; i < MTK_MAX_DEVS; i++) {
		if (!eth->netdev[i])
			continue;

		err = register_netdev(eth->netdev[i]);
		if (err) {
			dev_err(eth->dev, "error bringing up device\n");
			goto err_deinit_mdio;
		} else
			netif_info(eth, probe, eth->netdev[i],
				   "mediatek frame engine at 0x%08lx, irq %d\n",
				   eth->netdev[i]->base_addr, eth->irq[0]);
	}

	/* we run 2 devices on the same DMA ring so we need a dummy device
	 * for NAPI to work
	 */
	init_dummy_netdev(&eth->dummy_dev);
	netif_napi_add(&eth->dummy_dev, &eth->tx_napi, mtk_napi_tx,
		       MTK_NAPI_WEIGHT);
	netif_napi_add(&eth->dummy_dev, &eth->rx_napi, mtk_napi_rx,
		       MTK_NAPI_WEIGHT);

	platform_set_drvdata(pdev, eth);

	return 0;

err_deinit_mdio:
	mtk_mdio_cleanup(eth);
err_free_dev:
	mtk_free_dev(eth);
err_deinit_hw:
	mtk_hw_deinit(eth);

	return err;
}

static int mtk_remove(struct platform_device *pdev)
{
	struct mtk_eth *eth = platform_get_drvdata(pdev);
	int i;

	/* stop all devices to make sure that dma is properly shut down */
	for (i = 0; i < MTK_MAC_COUNT; i++) {
		if (!eth->netdev[i])
			continue;
		mtk_stop(eth->netdev[i]);
	}

	mtk_hw_deinit(eth);

	netif_napi_del(&eth->tx_napi);
	netif_napi_del(&eth->rx_napi);
	mtk_cleanup(eth);
	mtk_mdio_cleanup(eth);

	return 0;
}

const struct of_device_id of_mtk_match[] = {
	{ .compatible = "mediatek,mt7623-eth" },
	{},
};
MODULE_DEVICE_TABLE(of, of_mtk_match);

static struct platform_driver mtk_driver = {
	.probe = mtk_probe,
	.remove = mtk_remove,
	.driver = {
		.name = "mtk_soc_eth",
		.of_match_table = of_mtk_match,
	},
};

module_platform_driver(mtk_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Crispin <blogic@openwrt.org>");
MODULE_DESCRIPTION("Ethernet driver for MediaTek SoC");
