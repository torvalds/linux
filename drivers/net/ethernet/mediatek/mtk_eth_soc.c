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

static void mtk_phy_link_adjust(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	u16 lcl_adv = 0, rmt_adv = 0;
	u8 flowctrl;
	u32 mcr = MAC_MCR_MAX_RX_1536 | MAC_MCR_IPG_CFG |
		  MAC_MCR_FORCE_MODE | MAC_MCR_TX_EN |
		  MAC_MCR_RX_EN | MAC_MCR_BACKOFF_EN |
		  MAC_MCR_BACKPR_EN;

	switch (mac->phy_dev->speed) {
	case SPEED_1000:
		mcr |= MAC_MCR_SPEED_1000;
		break;
	case SPEED_100:
		mcr |= MAC_MCR_SPEED_100;
		break;
	};

	if (mac->phy_dev->link)
		mcr |= MAC_MCR_FORCE_LINK;

	if (mac->phy_dev->duplex) {
		mcr |= MAC_MCR_FORCE_DPX;

		if (mac->phy_dev->pause)
			rmt_adv = LPA_PAUSE_CAP;
		if (mac->phy_dev->asym_pause)
			rmt_adv |= LPA_PAUSE_ASYM;

		if (mac->phy_dev->advertising & ADVERTISED_Pause)
			lcl_adv |= ADVERTISE_PAUSE_CAP;
		if (mac->phy_dev->advertising & ADVERTISED_Asym_Pause)
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

	if (mac->phy_dev->link)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);
}

static int mtk_phy_connect_node(struct mtk_eth *eth, struct mtk_mac *mac,
				struct device_node *phy_node)
{
	const __be32 *_addr = NULL;
	struct phy_device *phydev;
	int phy_mode, addr;

	_addr = of_get_property(phy_node, "reg", NULL);

	if (!_addr || (be32_to_cpu(*_addr) >= 0x20)) {
		pr_err("%s: invalid phy address\n", phy_node->name);
		return -EINVAL;
	}
	addr = be32_to_cpu(*_addr);
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

	mac->phy_dev = phydev;

	return 0;
}

static int mtk_phy_connect(struct mtk_mac *mac)
{
	struct mtk_eth *eth = mac->hw;
	struct device_node *np;
	u32 val, ge_mode;

	np = of_parse_phandle(mac->of_node, "phy-handle", 0);
	if (!np && of_phy_is_fixed_link(mac->of_node))
		if (!of_phy_register_fixed_link(mac->of_node))
			np = of_node_get(mac->of_node);
	if (!np)
		return -ENODEV;

	switch (of_get_phy_mode(np)) {
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII:
		ge_mode = 0;
		break;
	case PHY_INTERFACE_MODE_MII:
		ge_mode = 1;
		break;
	case PHY_INTERFACE_MODE_REVMII:
		ge_mode = 2;
		break;
	case PHY_INTERFACE_MODE_RMII:
		if (!mac->id)
			goto err_phy;
		ge_mode = 3;
		break;
	default:
		goto err_phy;
	}

	/* put the gmac into the right mode */
	regmap_read(eth->ethsys, ETHSYS_SYSCFG0, &val);
	val &= ~SYSCFG0_GE_MODE(SYSCFG0_GE_MASK, mac->id);
	val |= SYSCFG0_GE_MODE(ge_mode, mac->id);
	regmap_write(eth->ethsys, ETHSYS_SYSCFG0, val);

	mtk_phy_connect_node(eth, mac, np);
	mac->phy_dev->autoneg = AUTONEG_ENABLE;
	mac->phy_dev->speed = 0;
	mac->phy_dev->duplex = 0;

	if (of_phy_is_fixed_link(mac->of_node))
		mac->phy_dev->supported |=
		SUPPORTED_Pause | SUPPORTED_Asym_Pause;

	mac->phy_dev->supported &= PHY_GBIT_FEATURES | SUPPORTED_Pause |
				   SUPPORTED_Asym_Pause;
	mac->phy_dev->advertising = mac->phy_dev->supported |
				    ADVERTISED_Autoneg;
	phy_start_aneg(mac->phy_dev);

	of_node_put(np);

	return 0;

err_phy:
	of_node_put(np);
	dev_err(eth->dev, "invalid phy_mode\n");
	return -EINVAL;
}

static int mtk_mdio_init(struct mtk_eth *eth)
{
	struct device_node *mii_np;
	int err;

	mii_np = of_get_child_by_name(eth->dev->of_node, "mdio-bus");
	if (!mii_np) {
		dev_err(eth->dev, "no %s child node found", "mdio-bus");
		return -ENODEV;
	}

	if (!of_device_is_available(mii_np)) {
		err = 0;
		goto err_put_node;
	}

	eth->mii_bus = mdiobus_alloc();
	if (!eth->mii_bus) {
		err = -ENOMEM;
		goto err_put_node;
	}

	eth->mii_bus->name = "mdio";
	eth->mii_bus->read = mtk_mdio_read;
	eth->mii_bus->write = mtk_mdio_write;
	eth->mii_bus->priv = eth;
	eth->mii_bus->parent = eth->dev;

	snprintf(eth->mii_bus->id, MII_BUS_ID_SIZE, "%s", mii_np->name);
	err = of_mdiobus_register(eth->mii_bus, mii_np);
	if (err)
		goto err_free_bus;

	return 0;

err_free_bus:
	mdiobus_free(eth->mii_bus);

err_put_node:
	of_node_put(mii_np);
	eth->mii_bus = NULL;
	return err;
}

static void mtk_mdio_cleanup(struct mtk_eth *eth)
{
	if (!eth->mii_bus)
		return;

	mdiobus_unregister(eth->mii_bus);
	of_node_put(eth->mii_bus->dev.of_node);
	mdiobus_free(eth->mii_bus);
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
	u32 txd4 = 0;

	itxd = ring->next_free;
	if (itxd == ring->last_free)
		return -ENOMEM;

	/* set the forward port */
	txd4 |= (mac->id + 1) << TX_DMA_FPORT_SHIFT;

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
			WRITE_ONCE(txd->txd4, 0);

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
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int mtk_poll_rx(struct napi_struct *napi, int budget,
		       struct mtk_eth *eth)
{
	struct mtk_rx_ring *ring = &eth->rx_ring;
	int idx = ring->calc_idx;
	struct sk_buff *skb;
	u8 *data, *new_data;
	struct mtk_rx_dma *rxd, trxd;
	int done = 0;

	while (done < budget) {
		struct net_device *netdev;
		unsigned int pktlen;
		dma_addr_t dma_addr;
		int mac = 0;

		idx = NEXT_RX_DESP_IDX(idx);
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
			put_page(virt_to_head_page(new_data));
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
		/* make sure that all changes to the dma ring are flushed before
		 * we continue
		 */
		wmb();
		mtk_w32(eth, ring->calc_idx, MTK_PRX_CRX_IDX0);
		done++;
	}

	if (done < budget)
		mtk_w32(eth, MTK_RX_DONE_INT, MTK_PDMA_INT_STATUS);

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

	mtk_handle_status_irq(eth);
	mtk_w32(eth, MTK_RX_DONE_INT, MTK_PDMA_INT_STATUS);
	rx_done = mtk_poll_rx(napi, budget, eth);

	if (unlikely(netif_msg_intr(eth))) {
		status = mtk_r32(eth, MTK_PDMA_INT_STATUS);
		mask = mtk_r32(eth, MTK_PDMA_INT_MASK);
		dev_info(eth->dev,
			 "done rx %d, intr 0x%08x/0x%x\n",
			 rx_done, status, mask);
	}

	if (rx_done == budget)
		return budget;

	status = mtk_r32(eth, MTK_PDMA_INT_STATUS);
	if (status & MTK_RX_DONE_INT)
		return budget;

	napi_complete(napi);
	mtk_irq_enable(eth, MTK_PDMA_INT_MASK, MTK_RX_DONE_INT);

	return rx_done;
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

static int mtk_rx_alloc(struct mtk_eth *eth)
{
	struct mtk_rx_ring *ring = &eth->rx_ring;
	int i;

	ring->frag_size = mtk_max_frag_size(ETH_DATA_LEN);
	ring->buf_size = mtk_max_buf_size(ring->frag_size);
	ring->data = kcalloc(MTK_DMA_SIZE, sizeof(*ring->data),
			     GFP_KERNEL);
	if (!ring->data)
		return -ENOMEM;

	for (i = 0; i < MTK_DMA_SIZE; i++) {
		ring->data[i] = netdev_alloc_frag(ring->frag_size);
		if (!ring->data[i])
			return -ENOMEM;
	}

	ring->dma = dma_alloc_coherent(eth->dev,
				       MTK_DMA_SIZE * sizeof(*ring->dma),
				       &ring->phys,
				       GFP_ATOMIC | __GFP_ZERO);
	if (!ring->dma)
		return -ENOMEM;

	for (i = 0; i < MTK_DMA_SIZE; i++) {
		dma_addr_t dma_addr = dma_map_single(eth->dev,
				ring->data[i] + NET_SKB_PAD,
				ring->buf_size,
				DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(eth->dev, dma_addr)))
			return -ENOMEM;
		ring->dma[i].rxd1 = (unsigned int)dma_addr;

		ring->dma[i].rxd2 = RX_DMA_PLEN0(ring->buf_size);
	}
	ring->calc_idx = MTK_DMA_SIZE - 1;
	/* make sure that all changes to the dma ring are flushed before we
	 * continue
	 */
	wmb();

	mtk_w32(eth, eth->rx_ring.phys, MTK_PRX_BASE_PTR0);
	mtk_w32(eth, MTK_DMA_SIZE, MTK_PRX_MAX_CNT0);
	mtk_w32(eth, eth->rx_ring.calc_idx, MTK_PRX_CRX_IDX0);
	mtk_w32(eth, MTK_PST_DRX_IDX0, MTK_PDMA_RST_IDX);

	return 0;
}

static void mtk_rx_clean(struct mtk_eth *eth)
{
	struct mtk_rx_ring *ring = &eth->rx_ring;
	int i;

	if (ring->data && ring->dma) {
		for (i = 0; i < MTK_DMA_SIZE; i++) {
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
				  MTK_DMA_SIZE * sizeof(*ring->dma),
				  ring->dma,
				  ring->phys);
		ring->dma = NULL;
	}
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

	err = mtk_rx_alloc(eth);
	if (err)
		return err;

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
	mtk_rx_clean(eth);
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

	phy_start(mac->phy_dev);
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
	phy_stop(mac->phy_dev);

	/* only shutdown DMA if this is the last user */
	if (!atomic_dec_and_test(&eth->dma_refcnt))
		return 0;

	mtk_irq_disable(eth, MTK_QDMA_INT_MASK, MTK_TX_DONE_INT);
	mtk_irq_disable(eth, MTK_PDMA_INT_MASK, MTK_RX_DONE_INT);
	napi_disable(&eth->tx_napi);
	napi_disable(&eth->rx_napi);

	mtk_stop_dma(eth, MTK_QDMA_GLO_CFG);

	mtk_dma_free(eth);

	return 0;
}

static int __init mtk_hw_init(struct mtk_eth *eth)
{
	int err, i;

	/* reset the frame engine */
	reset_control_assert(eth->rstc);
	usleep_range(10, 20);
	reset_control_deassert(eth->rstc);
	usleep_range(10, 20);

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

	err = devm_request_irq(eth->dev, eth->irq[1], mtk_handle_irq_tx, 0,
			       dev_name(eth->dev), eth);
	if (err)
		return err;
	err = devm_request_irq(eth->dev, eth->irq[2], mtk_handle_irq_rx, 0,
			       dev_name(eth->dev), eth);
	if (err)
		return err;

	err = mtk_mdio_init(eth);
	if (err)
		return err;

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

	return mtk_phy_connect(mac);
}

static void mtk_uninit(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;

	phy_disconnect(mac->phy_dev);
	mtk_mdio_cleanup(eth);
	mtk_irq_disable(eth, MTK_QDMA_INT_MASK, ~0);
	mtk_irq_disable(eth, MTK_PDMA_INT_MASK, ~0);
	free_irq(eth->irq[1], dev);
	free_irq(eth->irq[2], dev);
}

static int mtk_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mtk_mac *mac = netdev_priv(dev);

	switch (cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return phy_mii_ioctl(mac->phy_dev, ifr, cmd);
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

	/* stop all devices to make sure that dma is properly shut down */
	for (i = 0; i < MTK_MAC_COUNT; i++) {
		if (!eth->netdev[i])
			continue;
		mtk_stop(eth->netdev[i]);
		__set_bit(i, &restart);
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
	rtnl_unlock();
}

static int mtk_cleanup(struct mtk_eth *eth)
{
	int i;

	for (i = 0; i < MTK_MAC_COUNT; i++) {
		if (!eth->netdev[i])
			continue;

		unregister_netdev(eth->netdev[i]);
		free_netdev(eth->netdev[i]);
	}
	cancel_work_sync(&eth->pending_work);

	return 0;
}

static int mtk_get_settings(struct net_device *dev,
			    struct ethtool_cmd *cmd)
{
	struct mtk_mac *mac = netdev_priv(dev);
	int err;

	err = phy_read_status(mac->phy_dev);
	if (err)
		return -ENODEV;

	return phy_ethtool_gset(mac->phy_dev, cmd);
}

static int mtk_set_settings(struct net_device *dev,
			    struct ethtool_cmd *cmd)
{
	struct mtk_mac *mac = netdev_priv(dev);

	if (cmd->phy_address != mac->phy_dev->mdio.addr) {
		mac->phy_dev = mdiobus_get_phy(mac->hw->mii_bus,
					       cmd->phy_address);
		if (!mac->phy_dev)
			return -ENODEV;
	}

	return phy_ethtool_sset(mac->phy_dev, cmd);
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

	return genphy_restart_aneg(mac->phy_dev);
}

static u32 mtk_get_link(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	int err;

	err = genphy_update_link(mac->phy_dev);
	if (err)
		return ethtool_op_get_link(dev);

	return mac->phy_dev->link;
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

	if (netif_running(dev) && netif_device_present(dev)) {
		if (spin_trylock(&hwstats->stats_lock)) {
			mtk_stats_update_mac(mac);
			spin_unlock(&hwstats->stats_lock);
		}
	}

	do {
		data_src = (u64 *)hwstats;
		data_dst = data;
		start = u64_stats_fetch_begin_irq(&hwstats->syncp);

		for (i = 0; i < ARRAY_SIZE(mtk_ethtool_stats); i++)
			*data_dst++ = *(data_src + mtk_ethtool_stats[i].offset);
	} while (u64_stats_fetch_retry_irq(&hwstats->syncp, start));
}

static struct ethtool_ops mtk_ethtool_ops = {
	.get_settings		= mtk_get_settings,
	.set_settings		= mtk_set_settings,
	.get_drvinfo		= mtk_get_drvinfo,
	.get_msglevel		= mtk_get_msglevel,
	.set_msglevel		= mtk_set_msglevel,
	.nway_reset		= mtk_nway_reset,
	.get_link		= mtk_get_link,
	.get_strings		= mtk_get_strings,
	.get_sset_count		= mtk_get_sset_count,
	.get_ethtool_stats	= mtk_get_ethtool_stats,
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
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_tx_timeout		= mtk_tx_timeout,
	.ndo_get_stats64        = mtk_get_stats64,
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
	eth->netdev[id]->vlan_features = MTK_HW_FEATURES &
		~(NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX);
	eth->netdev[id]->features |= MTK_HW_FEATURES;
	eth->netdev[id]->ethtool_ops = &mtk_ethtool_ops;

	err = register_netdev(eth->netdev[id]);
	if (err) {
		dev_err(eth->dev, "error bringing up device\n");
		goto free_netdev;
	}
	eth->netdev[id]->irq = eth->irq[0];
	netif_info(eth, probe, eth->netdev[id],
		   "mediatek frame engine at 0x%08lx, irq %d\n",
		   eth->netdev[id]->base_addr, eth->irq[0]);

	return 0;

free_netdev:
	free_netdev(eth->netdev[id]);
	return err;
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

	eth->rstc = devm_reset_control_get(&pdev->dev, "eth");
	if (IS_ERR(eth->rstc)) {
		dev_err(&pdev->dev, "no eth reset found\n");
		return PTR_ERR(eth->rstc);
	}

	for (i = 0; i < 3; i++) {
		eth->irq[i] = platform_get_irq(pdev, i);
		if (eth->irq[i] < 0) {
			dev_err(&pdev->dev, "no IRQ%d resource found\n", i);
			return -ENXIO;
		}
	}

	eth->clk_ethif = devm_clk_get(&pdev->dev, "ethif");
	eth->clk_esw = devm_clk_get(&pdev->dev, "esw");
	eth->clk_gp1 = devm_clk_get(&pdev->dev, "gp1");
	eth->clk_gp2 = devm_clk_get(&pdev->dev, "gp2");
	if (IS_ERR(eth->clk_esw) || IS_ERR(eth->clk_gp1) ||
	    IS_ERR(eth->clk_gp2) || IS_ERR(eth->clk_ethif))
		return -ENODEV;

	clk_prepare_enable(eth->clk_ethif);
	clk_prepare_enable(eth->clk_esw);
	clk_prepare_enable(eth->clk_gp1);
	clk_prepare_enable(eth->clk_gp2);

	eth->dev = &pdev->dev;
	eth->msg_enable = netif_msg_init(mtk_msg_level, MTK_DEFAULT_MSG_ENABLE);
	INIT_WORK(&eth->pending_work, mtk_pending_work);

	err = mtk_hw_init(eth);
	if (err)
		return err;

	for_each_child_of_node(pdev->dev.of_node, mac_np) {
		if (!of_device_is_compatible(mac_np,
					     "mediatek,eth-mac"))
			continue;

		if (!of_device_is_available(mac_np))
			continue;

		err = mtk_add_mac(eth, mac_np);
		if (err)
			goto err_free_dev;
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

err_free_dev:
	mtk_cleanup(eth);
	return err;
}

static int mtk_remove(struct platform_device *pdev)
{
	struct mtk_eth *eth = platform_get_drvdata(pdev);

	clk_disable_unprepare(eth->clk_ethif);
	clk_disable_unprepare(eth->clk_esw);
	clk_disable_unprepare(eth->clk_gp1);
	clk_disable_unprepare(eth->clk_gp2);

	netif_napi_del(&eth->tx_napi);
	netif_napi_del(&eth->rx_napi);
	mtk_cleanup(eth);

	return 0;
}

const struct of_device_id of_mtk_match[] = {
	{ .compatible = "mediatek,mt7623-eth" },
	{},
};

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
