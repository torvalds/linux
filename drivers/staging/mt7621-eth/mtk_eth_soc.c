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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/mfd/syscon.h>
#include <linux/clk.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>
#include <linux/if_vlan.h>
#include <linux/reset.h>
#include <linux/tcp.h>
#include <linux/io.h>
#include <linux/bug.h>
#include <linux/regmap.h>

#include "mtk_eth_soc.h"
#include "mdio.h"
#include "ethtool.h"

#define	MAX_RX_LENGTH		1536
#define MTK_RX_ETH_HLEN		(VLAN_ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN)
#define MTK_RX_HLEN		(NET_SKB_PAD + MTK_RX_ETH_HLEN + NET_IP_ALIGN)
#define DMA_DUMMY_DESC		0xffffffff
#define MTK_DEFAULT_MSG_ENABLE \
		(NETIF_MSG_DRV | \
		NETIF_MSG_PROBE | \
		NETIF_MSG_LINK | \
		NETIF_MSG_TIMER | \
		NETIF_MSG_IFDOWN | \
		NETIF_MSG_IFUP | \
		NETIF_MSG_RX_ERR | \
		NETIF_MSG_TX_ERR)

#define TX_DMA_DESP2_DEF	(TX_DMA_LS0 | TX_DMA_DONE)
#define NEXT_TX_DESP_IDX(X)	(((X) + 1) & (ring->tx_ring_size - 1))
#define NEXT_RX_DESP_IDX(X)	(((X) + 1) & (ring->rx_ring_size - 1))

#define SYSC_REG_RSTCTRL	0x34

static int mtk_msg_level = -1;
module_param_named(msg_level, mtk_msg_level, int, 0);
MODULE_PARM_DESC(msg_level, "Message level (-1=defaults,0=none,...,16=all)");

static const u16 mtk_reg_table_default[MTK_REG_COUNT] = {
	[MTK_REG_PDMA_GLO_CFG] = MTK_PDMA_GLO_CFG,
	[MTK_REG_PDMA_RST_CFG] = MTK_PDMA_RST_CFG,
	[MTK_REG_DLY_INT_CFG] = MTK_DLY_INT_CFG,
	[MTK_REG_TX_BASE_PTR0] = MTK_TX_BASE_PTR0,
	[MTK_REG_TX_MAX_CNT0] = MTK_TX_MAX_CNT0,
	[MTK_REG_TX_CTX_IDX0] = MTK_TX_CTX_IDX0,
	[MTK_REG_TX_DTX_IDX0] = MTK_TX_DTX_IDX0,
	[MTK_REG_RX_BASE_PTR0] = MTK_RX_BASE_PTR0,
	[MTK_REG_RX_MAX_CNT0] = MTK_RX_MAX_CNT0,
	[MTK_REG_RX_CALC_IDX0] = MTK_RX_CALC_IDX0,
	[MTK_REG_RX_DRX_IDX0] = MTK_RX_DRX_IDX0,
	[MTK_REG_MTK_INT_ENABLE] = MTK_INT_ENABLE,
	[MTK_REG_MTK_INT_STATUS] = MTK_INT_STATUS,
	[MTK_REG_MTK_DMA_VID_BASE] = MTK_DMA_VID0,
	[MTK_REG_MTK_COUNTER_BASE] = MTK_GDMA1_TX_GBCNT,
	[MTK_REG_MTK_RST_GL] = MTK_RST_GL,
};

static const u16 *mtk_reg_table = mtk_reg_table_default;

void mtk_w32(struct mtk_eth *eth, u32 val, unsigned int reg)
{
	__raw_writel(val, eth->base + reg);
}

u32 mtk_r32(struct mtk_eth *eth, unsigned int reg)
{
	return __raw_readl(eth->base + reg);
}

static void mtk_reg_w32(struct mtk_eth *eth, u32 val, enum mtk_reg reg)
{
	mtk_w32(eth, val, mtk_reg_table[reg]);
}

static u32 mtk_reg_r32(struct mtk_eth *eth, enum mtk_reg reg)
{
	return mtk_r32(eth, mtk_reg_table[reg]);
}

/* these bits are also exposed via the reset-controller API. however the switch
 * and FE need to be brought out of reset in the exakt same moemtn and the
 * reset-controller api does not provide this feature yet. Do the reset manually
 * until we fixed the reset-controller api to be able to do this
 */
void mtk_reset(struct mtk_eth *eth, u32 reset_bits)
{
	u32 val;

	regmap_read(eth->ethsys, SYSC_REG_RSTCTRL, &val);
	val |= reset_bits;
	regmap_write(eth->ethsys, SYSC_REG_RSTCTRL, val);
	usleep_range(10, 20);
	val &= ~reset_bits;
	regmap_write(eth->ethsys, SYSC_REG_RSTCTRL, val);
	usleep_range(10, 20);
}
EXPORT_SYMBOL(mtk_reset);

static inline void mtk_irq_ack(struct mtk_eth *eth, u32 mask)
{
	if (eth->soc->dma_type & MTK_PDMA)
		mtk_reg_w32(eth, mask, MTK_REG_MTK_INT_STATUS);
	if (eth->soc->dma_type & MTK_QDMA)
		mtk_w32(eth, mask, MTK_QMTK_INT_STATUS);
}

static inline u32 mtk_irq_pending(struct mtk_eth *eth)
{
	u32 status = 0;

	if (eth->soc->dma_type & MTK_PDMA)
		status |= mtk_reg_r32(eth, MTK_REG_MTK_INT_STATUS);
	if (eth->soc->dma_type & MTK_QDMA)
		status |= mtk_r32(eth, MTK_QMTK_INT_STATUS);

	return status;
}

static void mtk_irq_ack_status(struct mtk_eth *eth, u32 mask)
{
	u32 status_reg = MTK_REG_MTK_INT_STATUS;

	if (mtk_reg_table[MTK_REG_MTK_INT_STATUS2])
		status_reg = MTK_REG_MTK_INT_STATUS2;

	mtk_reg_w32(eth, mask, status_reg);
}

static u32 mtk_irq_pending_status(struct mtk_eth *eth)
{
	u32 status_reg = MTK_REG_MTK_INT_STATUS;

	if (mtk_reg_table[MTK_REG_MTK_INT_STATUS2])
		status_reg = MTK_REG_MTK_INT_STATUS2;

	return mtk_reg_r32(eth, status_reg);
}

static inline void mtk_irq_disable(struct mtk_eth *eth, u32 mask)
{
	u32 val;

	if (eth->soc->dma_type & MTK_PDMA) {
		val = mtk_reg_r32(eth, MTK_REG_MTK_INT_ENABLE);
		mtk_reg_w32(eth, val & ~mask, MTK_REG_MTK_INT_ENABLE);
		/* flush write */
		mtk_reg_r32(eth, MTK_REG_MTK_INT_ENABLE);
	}
	if (eth->soc->dma_type & MTK_QDMA) {
		val = mtk_r32(eth, MTK_QMTK_INT_ENABLE);
		mtk_w32(eth, val & ~mask, MTK_QMTK_INT_ENABLE);
		/* flush write */
		mtk_r32(eth, MTK_QMTK_INT_ENABLE);
	}
}

static inline void mtk_irq_enable(struct mtk_eth *eth, u32 mask)
{
	u32 val;

	if (eth->soc->dma_type & MTK_PDMA) {
		val = mtk_reg_r32(eth, MTK_REG_MTK_INT_ENABLE);
		mtk_reg_w32(eth, val | mask, MTK_REG_MTK_INT_ENABLE);
		/* flush write */
		mtk_reg_r32(eth, MTK_REG_MTK_INT_ENABLE);
	}
	if (eth->soc->dma_type & MTK_QDMA) {
		val = mtk_r32(eth, MTK_QMTK_INT_ENABLE);
		mtk_w32(eth, val | mask, MTK_QMTK_INT_ENABLE);
		/* flush write */
		mtk_r32(eth, MTK_QMTK_INT_ENABLE);
	}
}

static inline u32 mtk_irq_enabled(struct mtk_eth *eth)
{
	u32 enabled = 0;

	if (eth->soc->dma_type & MTK_PDMA)
		enabled |= mtk_reg_r32(eth, MTK_REG_MTK_INT_ENABLE);
	if (eth->soc->dma_type & MTK_QDMA)
		enabled |= mtk_r32(eth, MTK_QMTK_INT_ENABLE);

	return enabled;
}

static inline void mtk_hw_set_macaddr(struct mtk_mac *mac,
				      unsigned char *macaddr)
{
	unsigned long flags;

	spin_lock_irqsave(&mac->hw->page_lock, flags);
	mtk_w32(mac->hw, (macaddr[0] << 8) | macaddr[1], MTK_GDMA1_MAC_ADRH);
	mtk_w32(mac->hw, (macaddr[2] << 24) | (macaddr[3] << 16) |
		(macaddr[4] << 8) | macaddr[5],
		MTK_GDMA1_MAC_ADRL);
	spin_unlock_irqrestore(&mac->hw->page_lock, flags);
}

static int mtk_set_mac_address(struct net_device *dev, void *p)
{
	int ret = eth_mac_addr(dev, p);
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;

	if (ret)
		return ret;

	if (eth->soc->set_mac)
		eth->soc->set_mac(mac, dev->dev_addr);
	else
		mtk_hw_set_macaddr(mac, p);

	return 0;
}

static inline int mtk_max_frag_size(int mtu)
{
	/* make sure buf_size will be at least MAX_RX_LENGTH */
	if (mtu + MTK_RX_ETH_HLEN < MAX_RX_LENGTH)
		mtu = MAX_RX_LENGTH - MTK_RX_ETH_HLEN;

	return SKB_DATA_ALIGN(MTK_RX_HLEN + mtu) +
		SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
}

static inline int mtk_max_buf_size(int frag_size)
{
	int buf_size = frag_size - NET_SKB_PAD - NET_IP_ALIGN -
		       SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	WARN_ON(buf_size < MAX_RX_LENGTH);

	return buf_size;
}

static inline void mtk_get_rxd(struct mtk_rx_dma *rxd,
			       struct mtk_rx_dma *dma_rxd)
{
	rxd->rxd1 = READ_ONCE(dma_rxd->rxd1);
	rxd->rxd2 = READ_ONCE(dma_rxd->rxd2);
	rxd->rxd3 = READ_ONCE(dma_rxd->rxd3);
	rxd->rxd4 = READ_ONCE(dma_rxd->rxd4);
}

static inline void mtk_set_txd_pdma(struct mtk_tx_dma *txd,
				    struct mtk_tx_dma *dma_txd)
{
	WRITE_ONCE(dma_txd->txd1, txd->txd1);
	WRITE_ONCE(dma_txd->txd3, txd->txd3);
	WRITE_ONCE(dma_txd->txd4, txd->txd4);
	/* clean dma done flag last */
	WRITE_ONCE(dma_txd->txd2, txd->txd2);
}

static void mtk_clean_rx(struct mtk_eth *eth, struct mtk_rx_ring *ring)
{
	int i;

	if (ring->rx_data && ring->rx_dma) {
		for (i = 0; i < ring->rx_ring_size; i++) {
			if (!ring->rx_data[i])
				continue;
			if (!ring->rx_dma[i].rxd1)
				continue;
			dma_unmap_single(eth->dev,
					 ring->rx_dma[i].rxd1,
					 ring->rx_buf_size,
					 DMA_FROM_DEVICE);
			skb_free_frag(ring->rx_data[i]);
		}
		kfree(ring->rx_data);
		ring->rx_data = NULL;
	}

	if (ring->rx_dma) {
		dma_free_coherent(eth->dev,
				  ring->rx_ring_size * sizeof(*ring->rx_dma),
				  ring->rx_dma,
				  ring->rx_phys);
		ring->rx_dma = NULL;
	}
}

static int mtk_dma_rx_alloc(struct mtk_eth *eth, struct mtk_rx_ring *ring)
{
	int i, pad = 0;

	ring->frag_size = mtk_max_frag_size(ETH_DATA_LEN);
	ring->rx_buf_size = mtk_max_buf_size(ring->frag_size);
	ring->rx_ring_size = eth->soc->dma_ring_size;
	ring->rx_data = kcalloc(ring->rx_ring_size, sizeof(*ring->rx_data),
				GFP_KERNEL);
	if (!ring->rx_data)
		goto no_rx_mem;

	for (i = 0; i < ring->rx_ring_size; i++) {
		ring->rx_data[i] = netdev_alloc_frag(ring->frag_size);
		if (!ring->rx_data[i])
			goto no_rx_mem;
	}

	ring->rx_dma =
		dma_alloc_coherent(eth->dev,
				   ring->rx_ring_size * sizeof(*ring->rx_dma),
				   &ring->rx_phys, GFP_ATOMIC | __GFP_ZERO);
	if (!ring->rx_dma)
		goto no_rx_mem;

	if (!eth->soc->rx_2b_offset)
		pad = NET_IP_ALIGN;

	for (i = 0; i < ring->rx_ring_size; i++) {
		dma_addr_t dma_addr = dma_map_single(eth->dev,
				ring->rx_data[i] + NET_SKB_PAD + pad,
				ring->rx_buf_size,
				DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(eth->dev, dma_addr)))
			goto no_rx_mem;
		ring->rx_dma[i].rxd1 = (unsigned int)dma_addr;

		if (eth->soc->rx_sg_dma)
			ring->rx_dma[i].rxd2 = RX_DMA_PLEN0(ring->rx_buf_size);
		else
			ring->rx_dma[i].rxd2 = RX_DMA_LSO;
	}
	ring->rx_calc_idx = ring->rx_ring_size - 1;
	/* make sure that all changes to the dma ring are flushed before we
	 * continue
	 */
	wmb();

	return 0;

no_rx_mem:
	return -ENOMEM;
}

static void mtk_txd_unmap(struct device *dev, struct mtk_tx_buf *tx_buf)
{
	if (tx_buf->flags & MTK_TX_FLAGS_SINGLE0) {
		dma_unmap_single(dev,
				 dma_unmap_addr(tx_buf, dma_addr0),
				 dma_unmap_len(tx_buf, dma_len0),
				 DMA_TO_DEVICE);
	} else if (tx_buf->flags & MTK_TX_FLAGS_PAGE0) {
		dma_unmap_page(dev,
			       dma_unmap_addr(tx_buf, dma_addr0),
			       dma_unmap_len(tx_buf, dma_len0),
			       DMA_TO_DEVICE);
	}
	if (tx_buf->flags & MTK_TX_FLAGS_PAGE1)
		dma_unmap_page(dev,
			       dma_unmap_addr(tx_buf, dma_addr1),
			       dma_unmap_len(tx_buf, dma_len1),
			       DMA_TO_DEVICE);

	tx_buf->flags = 0;
	if (tx_buf->skb && (tx_buf->skb != (struct sk_buff *)DMA_DUMMY_DESC))
		dev_kfree_skb_any(tx_buf->skb);
	tx_buf->skb = NULL;
}

static void mtk_pdma_tx_clean(struct mtk_eth *eth)
{
	struct mtk_tx_ring *ring = &eth->tx_ring;
	int i;

	if (ring->tx_buf) {
		for (i = 0; i < ring->tx_ring_size; i++)
			mtk_txd_unmap(eth->dev, &ring->tx_buf[i]);
		kfree(ring->tx_buf);
		ring->tx_buf = NULL;
	}

	if (ring->tx_dma) {
		dma_free_coherent(eth->dev,
				  ring->tx_ring_size * sizeof(*ring->tx_dma),
				  ring->tx_dma,
				  ring->tx_phys);
		ring->tx_dma = NULL;
	}
}

static void mtk_qdma_tx_clean(struct mtk_eth *eth)
{
	struct mtk_tx_ring *ring = &eth->tx_ring;
	int i;

	if (ring->tx_buf) {
		for (i = 0; i < ring->tx_ring_size; i++)
			mtk_txd_unmap(eth->dev, &ring->tx_buf[i]);
		kfree(ring->tx_buf);
		ring->tx_buf = NULL;
	}

	if (ring->tx_dma) {
		dma_free_coherent(eth->dev,
				  ring->tx_ring_size * sizeof(*ring->tx_dma),
				  ring->tx_dma,
				  ring->tx_phys);
		ring->tx_dma = NULL;
	}
}

void mtk_stats_update_mac(struct mtk_mac *mac)
{
	struct mtk_hw_stats *hw_stats = mac->hw_stats;
	unsigned int base = mtk_reg_table[MTK_REG_MTK_COUNTER_BASE];
	u64 stats;

	base += hw_stats->reg_offset;

	u64_stats_update_begin(&hw_stats->syncp);

	if (mac->hw->soc->new_stats) {
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
	} else {
		hw_stats->tx_bytes += mtk_r32(mac->hw, base);
		hw_stats->tx_packets += mtk_r32(mac->hw, base + 0x04);
		hw_stats->tx_skip += mtk_r32(mac->hw, base + 0x08);
		hw_stats->tx_collisions += mtk_r32(mac->hw, base + 0x0c);
		hw_stats->rx_bytes += mtk_r32(mac->hw, base + 0x20);
		hw_stats->rx_packets += mtk_r32(mac->hw, base + 0x24);
		hw_stats->rx_overflow += mtk_r32(mac->hw, base + 0x28);
		hw_stats->rx_fcs_errors += mtk_r32(mac->hw, base + 0x2c);
		hw_stats->rx_short_errors += mtk_r32(mac->hw, base + 0x30);
		hw_stats->rx_long_errors += mtk_r32(mac->hw, base + 0x34);
		hw_stats->rx_checksum_errors += mtk_r32(mac->hw, base + 0x38);
		hw_stats->rx_flow_control_packets +=
						mtk_r32(mac->hw, base + 0x3c);
	}

	u64_stats_update_end(&hw_stats->syncp);
}

static void mtk_get_stats64(struct net_device *dev,
			    struct rtnl_link_stats64 *storage)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_hw_stats *hw_stats = mac->hw_stats;
	unsigned int base = mtk_reg_table[MTK_REG_MTK_COUNTER_BASE];
	unsigned int start;

	if (!base) {
		netdev_stats_to_stats64(storage, &dev->stats);
		return;
	}

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
}

static int mtk_vlan_rx_add_vid(struct net_device *dev,
			       __be16 proto, u16 vid)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	u32 idx = (vid & 0xf);
	u32 vlan_cfg;

	if (!((mtk_reg_table[MTK_REG_MTK_DMA_VID_BASE]) &&
	      (dev->features & NETIF_F_HW_VLAN_CTAG_TX)))
		return 0;

	if (test_bit(idx, &eth->vlan_map)) {
		netdev_warn(dev, "disable tx vlan offload\n");
		dev->wanted_features &= ~NETIF_F_HW_VLAN_CTAG_TX;
		netdev_update_features(dev);
	} else {
		vlan_cfg = mtk_r32(eth,
				   mtk_reg_table[MTK_REG_MTK_DMA_VID_BASE] +
				   ((idx >> 1) << 2));
		if (idx & 0x1) {
			vlan_cfg &= 0xffff;
			vlan_cfg |= (vid << 16);
		} else {
			vlan_cfg &= 0xffff0000;
			vlan_cfg |= vid;
		}
		mtk_w32(eth,
			vlan_cfg, mtk_reg_table[MTK_REG_MTK_DMA_VID_BASE] +
			((idx >> 1) << 2));
		set_bit(idx, &eth->vlan_map);
	}

	return 0;
}

static int mtk_vlan_rx_kill_vid(struct net_device *dev,
				__be16 proto, u16 vid)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	u32 idx = (vid & 0xf);

	if (!((mtk_reg_table[MTK_REG_MTK_DMA_VID_BASE]) &&
	      (dev->features & NETIF_F_HW_VLAN_CTAG_TX)))
		return 0;

	clear_bit(idx, &eth->vlan_map);

	return 0;
}

static inline u32 mtk_pdma_empty_txd(struct mtk_tx_ring *ring)
{
	barrier();
	return (u32)(ring->tx_ring_size -
		     ((ring->tx_next_idx - ring->tx_free_idx) &
		      (ring->tx_ring_size - 1)));
}

static int mtk_skb_padto(struct sk_buff *skb, struct mtk_eth *eth)
{
	unsigned int len;
	int ret;

	if (unlikely(skb->len >= VLAN_ETH_ZLEN))
		return 0;

	if (eth->soc->padding_64b && !eth->soc->padding_bug)
		return 0;

	if (skb_vlan_tag_present(skb))
		len = ETH_ZLEN;
	else if (skb->protocol == cpu_to_be16(ETH_P_8021Q))
		len = VLAN_ETH_ZLEN;
	else if (!eth->soc->padding_64b)
		len = ETH_ZLEN;
	else
		return 0;

	if (skb->len >= len)
		return 0;

	ret = skb_pad(skb, len - skb->len);
	if (ret < 0)
		return ret;
	skb->len = len;
	skb_set_tail_pointer(skb, len);

	return ret;
}

static int mtk_pdma_tx_map(struct sk_buff *skb, struct net_device *dev,
			   int tx_num, struct mtk_tx_ring *ring, bool gso)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	struct skb_frag_struct *frag;
	struct mtk_tx_dma txd, *ptxd;
	struct mtk_tx_buf *tx_buf;
	int i, j, k, frag_size, frag_map_size, offset;
	dma_addr_t mapped_addr;
	unsigned int nr_frags;
	u32 def_txd4;

	if (mtk_skb_padto(skb, eth)) {
		netif_warn(eth, tx_err, dev, "tx padding failed!\n");
		return -1;
	}

	tx_buf = &ring->tx_buf[ring->tx_next_idx];
	memset(tx_buf, 0, sizeof(*tx_buf));
	memset(&txd, 0, sizeof(txd));
	nr_frags = skb_shinfo(skb)->nr_frags;

	/* init tx descriptor */
	def_txd4 = eth->soc->txd4;
	txd.txd4 = def_txd4;

	if (eth->soc->mac_count > 1)
		txd.txd4 |= (mac->id + 1) << TX_DMA_FPORT_SHIFT;

	if (gso)
		txd.txd4 |= TX_DMA_TSO;

	/* TX Checksum offload */
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		txd.txd4 |= TX_DMA_CHKSUM;

	/* VLAN header offload */
	if (skb_vlan_tag_present(skb)) {
		u16 tag = skb_vlan_tag_get(skb);

		txd.txd4 |= TX_DMA_INS_VLAN |
			((tag >> VLAN_PRIO_SHIFT) << 4) |
			(tag & 0xF);
	}

	mapped_addr = dma_map_single(&dev->dev, skb->data,
				     skb_headlen(skb), DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(&dev->dev, mapped_addr)))
		return -1;

	txd.txd1 = mapped_addr;
	txd.txd2 = TX_DMA_PLEN0(skb_headlen(skb));

	tx_buf->flags |= MTK_TX_FLAGS_SINGLE0;
	dma_unmap_addr_set(tx_buf, dma_addr0, mapped_addr);
	dma_unmap_len_set(tx_buf, dma_len0, skb_headlen(skb));

	/* TX SG offload */
	j = ring->tx_next_idx;
	k = 0;
	for (i = 0; i < nr_frags; i++) {
		offset = 0;
		frag = &skb_shinfo(skb)->frags[i];
		frag_size = skb_frag_size(frag);

		while (frag_size > 0) {
			frag_map_size = min(frag_size, TX_DMA_BUF_LEN);
			mapped_addr = skb_frag_dma_map(&dev->dev, frag, offset,
						       frag_map_size,
						       DMA_TO_DEVICE);
			if (unlikely(dma_mapping_error(&dev->dev, mapped_addr)))
				goto err_dma;

			if (k & 0x1) {
				j = NEXT_TX_DESP_IDX(j);
				txd.txd1 = mapped_addr;
				txd.txd2 = TX_DMA_PLEN0(frag_map_size);
				txd.txd4 = def_txd4;

				tx_buf = &ring->tx_buf[j];
				memset(tx_buf, 0, sizeof(*tx_buf));

				tx_buf->flags |= MTK_TX_FLAGS_PAGE0;
				dma_unmap_addr_set(tx_buf, dma_addr0,
						   mapped_addr);
				dma_unmap_len_set(tx_buf, dma_len0,
						  frag_map_size);
			} else {
				txd.txd3 = mapped_addr;
				txd.txd2 |= TX_DMA_PLEN1(frag_map_size);

				tx_buf->skb = (struct sk_buff *)DMA_DUMMY_DESC;
				tx_buf->flags |= MTK_TX_FLAGS_PAGE1;
				dma_unmap_addr_set(tx_buf, dma_addr1,
						   mapped_addr);
				dma_unmap_len_set(tx_buf, dma_len1,
						  frag_map_size);

				if (!((i == (nr_frags - 1)) &&
				      (frag_map_size == frag_size))) {
					mtk_set_txd_pdma(&txd,
							 &ring->tx_dma[j]);
					memset(&txd, 0, sizeof(txd));
				}
			}
			frag_size -= frag_map_size;
			offset += frag_map_size;
			k++;
		}
	}

	/* set last segment */
	if (k & 0x1)
		txd.txd2 |= TX_DMA_LS1;
	else
		txd.txd2 |= TX_DMA_LS0;
	mtk_set_txd_pdma(&txd, &ring->tx_dma[j]);

	/* store skb to cleanup */
	tx_buf->skb = skb;

	netdev_sent_queue(dev, skb->len);
	skb_tx_timestamp(skb);

	ring->tx_next_idx = NEXT_TX_DESP_IDX(j);
	/* make sure that all changes to the dma ring are flushed before we
	 * continue
	 */
	wmb();
	atomic_set(&ring->tx_free_count, mtk_pdma_empty_txd(ring));

	if (netif_xmit_stopped(netdev_get_tx_queue(dev, 0)) || !skb->xmit_more)
		mtk_reg_w32(eth, ring->tx_next_idx, MTK_REG_TX_CTX_IDX0);

	return 0;

err_dma:
	j = ring->tx_next_idx;
	for (i = 0; i < tx_num; i++) {
		ptxd = &ring->tx_dma[j];
		tx_buf = &ring->tx_buf[j];

		/* unmap dma */
		mtk_txd_unmap(&dev->dev, tx_buf);

		ptxd->txd2 = TX_DMA_DESP2_DEF;
		j = NEXT_TX_DESP_IDX(j);
	}
	/* make sure that all changes to the dma ring are flushed before we
	 * continue
	 */
	wmb();
	return -1;
}

/* the qdma core needs scratch memory to be setup */
static int mtk_init_fq_dma(struct mtk_eth *eth)
{
	dma_addr_t dma_addr, phy_ring_head, phy_ring_tail;
	int cnt = eth->soc->dma_ring_size;
	int i;

	eth->scratch_ring = dma_alloc_coherent(eth->dev,
					       cnt * sizeof(struct mtk_tx_dma),
					       &phy_ring_head,
					       GFP_ATOMIC | __GFP_ZERO);
	if (unlikely(!eth->scratch_ring))
		return -ENOMEM;

	eth->scratch_head = kcalloc(cnt, QDMA_PAGE_SIZE,
				    GFP_KERNEL);
	dma_addr = dma_map_single(eth->dev,
				  eth->scratch_head, cnt * QDMA_PAGE_SIZE,
				  DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(eth->dev, dma_addr)))
		return -ENOMEM;

	memset(eth->scratch_ring, 0x0, sizeof(struct mtk_tx_dma) * cnt);
	phy_ring_tail = phy_ring_head + (sizeof(struct mtk_tx_dma) * (cnt - 1));

	for (i = 0; i < cnt; i++) {
		eth->scratch_ring[i].txd1 = (dma_addr + (i * QDMA_PAGE_SIZE));
		if (i < cnt - 1)
			eth->scratch_ring[i].txd2 = (phy_ring_head +
				((i + 1) * sizeof(struct mtk_tx_dma)));
		eth->scratch_ring[i].txd3 = TX_QDMA_SDL(QDMA_PAGE_SIZE);
	}

	mtk_w32(eth, phy_ring_head, MTK_QDMA_FQ_HEAD);
	mtk_w32(eth, phy_ring_tail, MTK_QDMA_FQ_TAIL);
	mtk_w32(eth, (cnt << 16) | cnt, MTK_QDMA_FQ_CNT);
	mtk_w32(eth, QDMA_PAGE_SIZE << 16, MTK_QDMA_FQ_BLEN);

	return 0;
}

static void *mtk_qdma_phys_to_virt(struct mtk_tx_ring *ring, u32 desc)
{
	void *ret = ring->tx_dma;

	return ret + (desc - ring->tx_phys);
}

static struct mtk_tx_dma *mtk_tx_next_qdma(struct mtk_tx_ring *ring,
					   struct mtk_tx_dma *txd)
{
	return mtk_qdma_phys_to_virt(ring, txd->txd2);
}

static struct mtk_tx_buf *mtk_desc_to_tx_buf(struct mtk_tx_ring *ring,
					     struct mtk_tx_dma *txd)
{
	int idx = txd - ring->tx_dma;

	return &ring->tx_buf[idx];
}

static int mtk_qdma_tx_map(struct sk_buff *skb, struct net_device *dev,
			   int tx_num, struct mtk_tx_ring *ring, bool gso)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	struct mtk_tx_dma *itxd, *txd;
	struct mtk_tx_buf *tx_buf;
	dma_addr_t mapped_addr;
	unsigned int nr_frags;
	int i, n_desc = 1;
	u32 txd4 = eth->soc->txd4;

	itxd = ring->tx_next_free;
	if (itxd == ring->tx_last_free)
		return -ENOMEM;

	if (eth->soc->mac_count > 1)
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
		txd4 |= TX_DMA_INS_VLAN_MT7621 | skb_vlan_tag_get(skb);

	mapped_addr = dma_map_single(&dev->dev, skb->data,
				     skb_headlen(skb), DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(&dev->dev, mapped_addr)))
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

			txd = mtk_tx_next_qdma(ring, txd);
			if (txd == ring->tx_last_free)
				goto err_dma;

			n_desc++;
			frag_map_size = min(frag_size, TX_DMA_BUF_LEN);
			mapped_addr = skb_frag_dma_map(&dev->dev, frag, offset,
						       frag_map_size,
						       DMA_TO_DEVICE);
			if (unlikely(dma_mapping_error(&dev->dev, mapped_addr)))
				goto err_dma;

			if (i == nr_frags - 1 &&
			    (frag_size - frag_map_size) == 0)
				last_frag = true;

			WRITE_ONCE(txd->txd1, mapped_addr);
			WRITE_ONCE(txd->txd3, (QDMA_TX_SWC |
					       TX_DMA_PLEN0(frag_map_size) |
					       last_frag * TX_DMA_LS0) |
					       mac->id);
			WRITE_ONCE(txd->txd4, 0);

			tx_buf->skb = (struct sk_buff *)DMA_DUMMY_DESC;
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
	WRITE_ONCE(itxd->txd3, (QDMA_TX_SWC | TX_DMA_PLEN0(skb_headlen(skb)) |
				(!nr_frags * TX_DMA_LS0)));

	netdev_sent_queue(dev, skb->len);
	skb_tx_timestamp(skb);

	ring->tx_next_free = mtk_tx_next_qdma(ring, txd);
	atomic_sub(n_desc, &ring->tx_free_count);

	/* make sure that all changes to the dma ring are flushed before we
	 * continue
	 */
	wmb();

	if (netif_xmit_stopped(netdev_get_tx_queue(dev, 0)) || !skb->xmit_more)
		mtk_w32(eth, txd->txd2, MTK_QTX_CTX_PTR);

	return 0;

err_dma:
	do {
		tx_buf = mtk_desc_to_tx_buf(ring, txd);

		/* unmap dma */
		mtk_txd_unmap(&dev->dev, tx_buf);

		itxd->txd3 = TX_DMA_DESP2_DEF;
		itxd = mtk_tx_next_qdma(ring, itxd);
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
			nfrags += DIV_ROUND_UP(frag->size, TX_DMA_BUF_LEN);
		}
	} else {
		nfrags += skb_shinfo(skb)->nr_frags;
	}

	return DIV_ROUND_UP(nfrags, 2);
}

static int mtk_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	struct mtk_tx_ring *ring = &eth->tx_ring;
	struct net_device_stats *stats = &dev->stats;
	int tx_num;
	int len = skb->len;
	bool gso = false;

	tx_num = mtk_cal_txd_req(skb);
	if (unlikely(atomic_read(&ring->tx_free_count) <= tx_num)) {
		netif_stop_queue(dev);
		netif_err(eth, tx_queued, dev,
			  "Tx Ring full when queue awake!\n");
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

	if (ring->tx_map(skb, dev, tx_num, ring, gso) < 0)
		goto drop;

	stats->tx_packets++;
	stats->tx_bytes += len;

	if (unlikely(atomic_read(&ring->tx_free_count) <= ring->tx_thresh)) {
		netif_stop_queue(dev);
		smp_mb();
		if (unlikely(atomic_read(&ring->tx_free_count) >
			     ring->tx_thresh))
			netif_wake_queue(dev);
	}

	return NETDEV_TX_OK;

drop:
	stats->tx_dropped++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int mtk_poll_rx(struct napi_struct *napi, int budget,
		       struct mtk_eth *eth, u32 rx_intr)
{
	struct mtk_soc_data *soc = eth->soc;
	struct mtk_rx_ring *ring = &eth->rx_ring[0];
	int idx = ring->rx_calc_idx;
	u32 checksum_bit;
	struct sk_buff *skb;
	u8 *data, *new_data;
	struct mtk_rx_dma *rxd, trxd;
	int done = 0, pad;

	if (eth->soc->hw_features & NETIF_F_RXCSUM)
		checksum_bit = soc->checksum_bit;
	else
		checksum_bit = 0;

	if (eth->soc->rx_2b_offset)
		pad = 0;
	else
		pad = NET_IP_ALIGN;

	while (done < budget) {
		struct net_device *netdev;
		unsigned int pktlen;
		dma_addr_t dma_addr;
		int mac = 0;

		idx = NEXT_RX_DESP_IDX(idx);
		rxd = &ring->rx_dma[idx];
		data = ring->rx_data[idx];

		mtk_get_rxd(&trxd, rxd);
		if (!(trxd.rxd2 & RX_DMA_DONE))
			break;

		/* find out which mac the packet come from. values start at 1 */
		if (eth->soc->mac_count > 1) {
			mac = (trxd.rxd4 >> RX_DMA_FPORT_SHIFT) &
			      RX_DMA_FPORT_MASK;
			mac--;
			if (mac < 0 || mac >= eth->soc->mac_count)
				goto release_desc;
		}

		netdev = eth->netdev[mac];

		/* alloc new buffer */
		new_data = napi_alloc_frag(ring->frag_size);
		if (unlikely(!new_data || !netdev)) {
			netdev->stats.rx_dropped++;
			goto release_desc;
		}
		dma_addr = dma_map_single(&netdev->dev,
					  new_data + NET_SKB_PAD + pad,
					  ring->rx_buf_size,
					  DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(&netdev->dev, dma_addr))) {
			skb_free_frag(new_data);
			goto release_desc;
		}

		/* receive data */
		skb = build_skb(data, ring->frag_size);
		if (unlikely(!skb)) {
			put_page(virt_to_head_page(new_data));
			goto release_desc;
		}
		skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);

		dma_unmap_single(&netdev->dev, trxd.rxd1,
				 ring->rx_buf_size, DMA_FROM_DEVICE);
		pktlen = RX_DMA_GET_PLEN0(trxd.rxd2);
		skb->dev = netdev;
		skb_put(skb, pktlen);
		if (trxd.rxd4 & checksum_bit)
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb_checksum_none_assert(skb);
		skb->protocol = eth_type_trans(skb, netdev);

		netdev->stats.rx_packets++;
		netdev->stats.rx_bytes += pktlen;

		if (netdev->features & NETIF_F_HW_VLAN_CTAG_RX &&
		    RX_DMA_VID(trxd.rxd3))
			__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q),
					       RX_DMA_VID(trxd.rxd3));
		napi_gro_receive(napi, skb);

		ring->rx_data[idx] = new_data;
		rxd->rxd1 = (unsigned int)dma_addr;

release_desc:
		if (eth->soc->rx_sg_dma)
			rxd->rxd2 = RX_DMA_PLEN0(ring->rx_buf_size);
		else
			rxd->rxd2 = RX_DMA_LSO;

		ring->rx_calc_idx = idx;
		/* make sure that all changes to the dma ring are flushed before
		 * we continue
		 */
		wmb();
		if (eth->soc->dma_type == MTK_QDMA)
			mtk_w32(eth, ring->rx_calc_idx, MTK_QRX_CRX_IDX0);
		else
			mtk_reg_w32(eth, ring->rx_calc_idx,
				    MTK_REG_RX_CALC_IDX0);
		done++;
	}

	if (done < budget)
		mtk_irq_ack(eth, rx_intr);

	return done;
}

static int mtk_pdma_tx_poll(struct mtk_eth *eth, int budget, bool *tx_again)
{
	struct sk_buff *skb;
	struct mtk_tx_buf *tx_buf;
	int done = 0;
	u32 idx, hwidx;
	struct mtk_tx_ring *ring = &eth->tx_ring;
	unsigned int bytes = 0;

	idx = ring->tx_free_idx;
	hwidx = mtk_reg_r32(eth, MTK_REG_TX_DTX_IDX0);

	while ((idx != hwidx) && budget) {
		tx_buf = &ring->tx_buf[idx];
		skb = tx_buf->skb;

		if (!skb)
			break;

		if (skb != (struct sk_buff *)DMA_DUMMY_DESC) {
			bytes += skb->len;
			done++;
			budget--;
		}
		mtk_txd_unmap(eth->dev, tx_buf);
		idx = NEXT_TX_DESP_IDX(idx);
	}
	ring->tx_free_idx = idx;
	atomic_set(&ring->tx_free_count, mtk_pdma_empty_txd(ring));

	/* read hw index again make sure no new tx packet */
	if (idx != hwidx || idx != mtk_reg_r32(eth, MTK_REG_TX_DTX_IDX0))
		*tx_again = 1;

	if (done)
		netdev_completed_queue(*eth->netdev, done, bytes);

	return done;
}

static int mtk_qdma_tx_poll(struct mtk_eth *eth, int budget, bool *tx_again)
{
	struct mtk_tx_ring *ring = &eth->tx_ring;
	struct mtk_tx_dma *desc;
	struct sk_buff *skb;
	struct mtk_tx_buf *tx_buf;
	int total = 0, done[MTK_MAX_DEVS];
	unsigned int bytes[MTK_MAX_DEVS];
	u32 cpu, dma;
	int i;

	memset(done, 0, sizeof(done));
	memset(bytes, 0, sizeof(bytes));

	cpu = mtk_r32(eth, MTK_QTX_CRX_PTR);
	dma = mtk_r32(eth, MTK_QTX_DRX_PTR);

	desc = mtk_qdma_phys_to_virt(ring, cpu);

	while ((cpu != dma) && budget) {
		u32 next_cpu = desc->txd2;
		int mac;

		desc = mtk_tx_next_qdma(ring, desc);
		if ((desc->txd3 & QDMA_TX_OWNER_CPU) == 0)
			break;

		mac = (desc->txd4 >> TX_DMA_FPORT_SHIFT) &
		       TX_DMA_FPORT_MASK;
		mac--;

		tx_buf = mtk_desc_to_tx_buf(ring, desc);
		skb = tx_buf->skb;
		if (!skb)
			break;

		if (skb != (struct sk_buff *)DMA_DUMMY_DESC) {
			bytes[mac] += skb->len;
			done[mac]++;
			budget--;
		}
		mtk_txd_unmap(eth->dev, tx_buf);

		ring->tx_last_free->txd2 = next_cpu;
		ring->tx_last_free = desc;
		atomic_inc(&ring->tx_free_count);

		cpu = next_cpu;
	}

	mtk_w32(eth, cpu, MTK_QTX_CRX_PTR);

	/* read hw index again make sure no new tx packet */
	if (cpu != dma || cpu != mtk_r32(eth, MTK_QTX_DRX_PTR))
		*tx_again = true;

	for (i = 0; i < eth->soc->mac_count; i++) {
		if (!done[i])
			continue;
		netdev_completed_queue(eth->netdev[i], done[i], bytes[i]);
		total += done[i];
	}

	return total;
}

static int mtk_poll_tx(struct mtk_eth *eth, int budget, u32 tx_intr,
		       bool *tx_again)
{
	struct mtk_tx_ring *ring = &eth->tx_ring;
	struct net_device *netdev = eth->netdev[0];
	int done;

	done = eth->tx_ring.tx_poll(eth, budget, tx_again);
	if (!*tx_again)
		mtk_irq_ack(eth, tx_intr);

	if (!done)
		return 0;

	smp_mb();
	if (unlikely(!netif_queue_stopped(netdev)))
		return done;

	if (atomic_read(&ring->tx_free_count) > ring->tx_thresh)
		netif_wake_queue(netdev);

	return done;
}

static void mtk_stats_update(struct mtk_eth *eth)
{
	int i;

	for (i = 0; i < eth->soc->mac_count; i++) {
		if (!eth->mac[i] || !eth->mac[i]->hw_stats)
			continue;
		if (spin_trylock(&eth->mac[i]->hw_stats->stats_lock)) {
			mtk_stats_update_mac(eth->mac[i]);
			spin_unlock(&eth->mac[i]->hw_stats->stats_lock);
		}
	}
}

static int mtk_poll(struct napi_struct *napi, int budget)
{
	struct mtk_eth *eth = container_of(napi, struct mtk_eth, rx_napi);
	u32 status, mtk_status, mask, tx_intr, rx_intr, status_intr;
	int tx_done, rx_done;
	bool tx_again = false;

	status = mtk_irq_pending(eth);
	mtk_status = mtk_irq_pending_status(eth);
	tx_intr = eth->soc->tx_int;
	rx_intr = eth->soc->rx_int;
	status_intr = eth->soc->status_int;
	tx_done = 0;
	rx_done = 0;
	tx_again = 0;

	if (status & tx_intr)
		tx_done = mtk_poll_tx(eth, budget, tx_intr, &tx_again);

	if (status & rx_intr)
		rx_done = mtk_poll_rx(napi, budget, eth, rx_intr);

	if (unlikely(mtk_status & status_intr)) {
		mtk_stats_update(eth);
		mtk_irq_ack_status(eth, status_intr);
	}

	if (unlikely(netif_msg_intr(eth))) {
		mask = mtk_irq_enabled(eth);
		netdev_info(eth->netdev[0],
			    "done tx %d, rx %d, intr 0x%08x/0x%x\n",
			    tx_done, rx_done, status, mask);
	}

	if (tx_again || rx_done == budget)
		return budget;

	status = mtk_irq_pending(eth);
	if (status & (tx_intr | rx_intr))
		return budget;

	napi_complete(napi);
	mtk_irq_enable(eth, tx_intr | rx_intr);

	return rx_done;
}

static int mtk_pdma_tx_alloc(struct mtk_eth *eth)
{
	int i;
	struct mtk_tx_ring *ring = &eth->tx_ring;

	ring->tx_ring_size = eth->soc->dma_ring_size;
	ring->tx_free_idx = 0;
	ring->tx_next_idx = 0;
	ring->tx_thresh = max((unsigned long)ring->tx_ring_size >> 2,
			      MAX_SKB_FRAGS);

	ring->tx_buf = kcalloc(ring->tx_ring_size, sizeof(*ring->tx_buf),
			       GFP_KERNEL);
	if (!ring->tx_buf)
		goto no_tx_mem;

	ring->tx_dma =
		dma_alloc_coherent(eth->dev,
				   ring->tx_ring_size * sizeof(*ring->tx_dma),
				   &ring->tx_phys, GFP_ATOMIC | __GFP_ZERO);
	if (!ring->tx_dma)
		goto no_tx_mem;

	for (i = 0; i < ring->tx_ring_size; i++) {
		ring->tx_dma[i].txd2 = TX_DMA_DESP2_DEF;
		ring->tx_dma[i].txd4 = eth->soc->txd4;
	}

	atomic_set(&ring->tx_free_count, mtk_pdma_empty_txd(ring));
	ring->tx_map = mtk_pdma_tx_map;
	ring->tx_poll = mtk_pdma_tx_poll;
	ring->tx_clean = mtk_pdma_tx_clean;

	/* make sure that all changes to the dma ring are flushed before we
	 * continue
	 */
	wmb();

	mtk_reg_w32(eth, ring->tx_phys, MTK_REG_TX_BASE_PTR0);
	mtk_reg_w32(eth, ring->tx_ring_size, MTK_REG_TX_MAX_CNT0);
	mtk_reg_w32(eth, 0, MTK_REG_TX_CTX_IDX0);
	mtk_reg_w32(eth, MTK_PST_DTX_IDX0, MTK_REG_PDMA_RST_CFG);

	return 0;

no_tx_mem:
	return -ENOMEM;
}

static int mtk_qdma_tx_alloc_tx(struct mtk_eth *eth)
{
	struct mtk_tx_ring *ring = &eth->tx_ring;
	int i, sz = sizeof(*ring->tx_dma);

	ring->tx_ring_size = eth->soc->dma_ring_size;
	ring->tx_buf = kcalloc(ring->tx_ring_size, sizeof(*ring->tx_buf),
			       GFP_KERNEL);
	if (!ring->tx_buf)
		goto no_tx_mem;

	ring->tx_dma = dma_alloc_coherent(eth->dev,
					  ring->tx_ring_size * sz,
					  &ring->tx_phys,
					  GFP_ATOMIC | __GFP_ZERO);
	if (!ring->tx_dma)
		goto no_tx_mem;

	memset(ring->tx_dma, 0, ring->tx_ring_size * sz);
	for (i = 0; i < ring->tx_ring_size; i++) {
		int next = (i + 1) % ring->tx_ring_size;
		u32 next_ptr = ring->tx_phys + next * sz;

		ring->tx_dma[i].txd2 = next_ptr;
		ring->tx_dma[i].txd3 = TX_DMA_DESP2_DEF;
	}

	atomic_set(&ring->tx_free_count, ring->tx_ring_size - 2);
	ring->tx_next_free = &ring->tx_dma[0];
	ring->tx_last_free = &ring->tx_dma[ring->tx_ring_size - 2];
	ring->tx_thresh = max((unsigned long)ring->tx_ring_size >> 2,
			      MAX_SKB_FRAGS);

	ring->tx_map = mtk_qdma_tx_map;
	ring->tx_poll = mtk_qdma_tx_poll;
	ring->tx_clean = mtk_qdma_tx_clean;

	/* make sure that all changes to the dma ring are flushed before we
	 * continue
	 */
	wmb();

	mtk_w32(eth, ring->tx_phys, MTK_QTX_CTX_PTR);
	mtk_w32(eth, ring->tx_phys, MTK_QTX_DTX_PTR);
	mtk_w32(eth,
		ring->tx_phys + ((ring->tx_ring_size - 1) * sz),
		MTK_QTX_CRX_PTR);
	mtk_w32(eth,
		ring->tx_phys + ((ring->tx_ring_size - 1) * sz),
		MTK_QTX_DRX_PTR);

	return 0;

no_tx_mem:
	return -ENOMEM;
}

static int mtk_qdma_init(struct mtk_eth *eth, int ring)
{
	int err;

	err = mtk_init_fq_dma(eth);
	if (err)
		return err;

	err = mtk_qdma_tx_alloc_tx(eth);
	if (err)
		return err;

	err = mtk_dma_rx_alloc(eth, &eth->rx_ring[ring]);
	if (err)
		return err;

	mtk_w32(eth, eth->rx_ring[ring].rx_phys, MTK_QRX_BASE_PTR0);
	mtk_w32(eth, eth->rx_ring[ring].rx_ring_size, MTK_QRX_MAX_CNT0);
	mtk_w32(eth, eth->rx_ring[ring].rx_calc_idx, MTK_QRX_CRX_IDX0);
	mtk_w32(eth, MTK_PST_DRX_IDX0, MTK_QDMA_RST_IDX);
	mtk_w32(eth, (QDMA_RES_THRES << 8) | QDMA_RES_THRES, MTK_QTX_CFG(0));

	/* Enable random early drop and set drop threshold automatically */
	mtk_w32(eth, 0x174444, MTK_QDMA_FC_THRES);
	mtk_w32(eth, 0x0, MTK_QDMA_HRED2);

	return 0;
}

static int mtk_pdma_qdma_init(struct mtk_eth *eth)
{
	int err = mtk_qdma_init(eth, 1);

	if (err)
		return err;

	err = mtk_dma_rx_alloc(eth, &eth->rx_ring[0]);
	if (err)
		return err;

	mtk_reg_w32(eth, eth->rx_ring[0].rx_phys, MTK_REG_RX_BASE_PTR0);
	mtk_reg_w32(eth, eth->rx_ring[0].rx_ring_size, MTK_REG_RX_MAX_CNT0);
	mtk_reg_w32(eth, eth->rx_ring[0].rx_calc_idx, MTK_REG_RX_CALC_IDX0);
	mtk_reg_w32(eth, MTK_PST_DRX_IDX0, MTK_REG_PDMA_RST_CFG);

	return 0;
}

static int mtk_pdma_init(struct mtk_eth *eth)
{
	struct mtk_rx_ring *ring = &eth->rx_ring[0];
	int err;

	err = mtk_pdma_tx_alloc(eth);
	if (err)
		return err;

	err = mtk_dma_rx_alloc(eth, ring);
	if (err)
		return err;

	mtk_reg_w32(eth, ring->rx_phys, MTK_REG_RX_BASE_PTR0);
	mtk_reg_w32(eth, ring->rx_ring_size, MTK_REG_RX_MAX_CNT0);
	mtk_reg_w32(eth, ring->rx_calc_idx, MTK_REG_RX_CALC_IDX0);
	mtk_reg_w32(eth, MTK_PST_DRX_IDX0, MTK_REG_PDMA_RST_CFG);

	return 0;
}

static void mtk_dma_free(struct mtk_eth *eth)
{
	int i;

	for (i = 0; i < eth->soc->mac_count; i++)
		if (eth->netdev[i])
			netdev_reset_queue(eth->netdev[i]);
	eth->tx_ring.tx_clean(eth);
	mtk_clean_rx(eth, &eth->rx_ring[0]);
	mtk_clean_rx(eth, &eth->rx_ring[1]);
	kfree(eth->scratch_head);
}

static void mtk_tx_timeout(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	struct mtk_tx_ring *ring = &eth->tx_ring;

	eth->netdev[mac->id]->stats.tx_errors++;
	netif_err(eth, tx_err, dev,
		  "transmit timed out\n");
	if (eth->soc->dma_type & MTK_PDMA) {
		netif_info(eth, drv, dev, "pdma_cfg:%08x\n",
			   mtk_reg_r32(eth, MTK_REG_PDMA_GLO_CFG));
		netif_info(eth, drv, dev,
			   "tx_ring=%d, base=%08x, max=%u, ctx=%u, dtx=%u, fdx=%hu, next=%hu\n",
			   0, mtk_reg_r32(eth, MTK_REG_TX_BASE_PTR0),
			   mtk_reg_r32(eth, MTK_REG_TX_MAX_CNT0),
			   mtk_reg_r32(eth, MTK_REG_TX_CTX_IDX0),
			   mtk_reg_r32(eth, MTK_REG_TX_DTX_IDX0),
			   ring->tx_free_idx,
			   ring->tx_next_idx);
	}
	if (eth->soc->dma_type & MTK_QDMA) {
		netif_info(eth, drv, dev, "qdma_cfg:%08x\n",
			   mtk_r32(eth, MTK_QDMA_GLO_CFG));
		netif_info(eth, drv, dev,
			   "tx_ring=%d, ctx=%08x, dtx=%08x, crx=%08x, drx=%08x, free=%hu\n",
			   0, mtk_r32(eth, MTK_QTX_CTX_PTR),
			   mtk_r32(eth, MTK_QTX_DTX_PTR),
			   mtk_r32(eth, MTK_QTX_CRX_PTR),
			   mtk_r32(eth, MTK_QTX_DRX_PTR),
			   atomic_read(&ring->tx_free_count));
	}
	netif_info(eth, drv, dev,
		   "rx_ring=%d, base=%08x, max=%u, calc=%u, drx=%u\n",
		   0, mtk_reg_r32(eth, MTK_REG_RX_BASE_PTR0),
		   mtk_reg_r32(eth, MTK_REG_RX_MAX_CNT0),
		   mtk_reg_r32(eth, MTK_REG_RX_CALC_IDX0),
		   mtk_reg_r32(eth, MTK_REG_RX_DRX_IDX0));

	schedule_work(&mac->pending_work);
}

static irqreturn_t mtk_handle_irq(int irq, void *_eth)
{
	struct mtk_eth *eth = _eth;
	u32 status, int_mask;

	status = mtk_irq_pending(eth);
	if (unlikely(!status))
		return IRQ_NONE;

	int_mask = (eth->soc->rx_int | eth->soc->tx_int);
	if (likely(status & int_mask)) {
		if (likely(napi_schedule_prep(&eth->rx_napi)))
			__napi_schedule(&eth->rx_napi);
	} else {
		mtk_irq_ack(eth, status);
	}
	mtk_irq_disable(eth, int_mask);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void mtk_poll_controller(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	u32 int_mask = eth->soc->tx_int | eth->soc->rx_int;

	mtk_irq_disable(eth, int_mask);
	mtk_handle_irq(dev->irq, dev);
	mtk_irq_enable(eth, int_mask);
}
#endif

int mtk_set_clock_cycle(struct mtk_eth *eth)
{
	unsigned long sysclk = eth->sysclk;

	sysclk /= MTK_US_CYC_CNT_DIVISOR;
	sysclk <<= MTK_US_CYC_CNT_SHIFT;

	mtk_w32(eth, (mtk_r32(eth, MTK_GLO_CFG) &
			~(MTK_US_CYC_CNT_MASK << MTK_US_CYC_CNT_SHIFT)) |
			sysclk,
			MTK_GLO_CFG);
	return 0;
}

void mtk_fwd_config(struct mtk_eth *eth)
{
	u32 fwd_cfg;

	fwd_cfg = mtk_r32(eth, MTK_GDMA1_FWD_CFG);

	/* disable jumbo frame */
	if (eth->soc->jumbo_frame)
		fwd_cfg &= ~MTK_GDM1_JMB_EN;

	/* set unicast/multicast/broadcast frame to cpu */
	fwd_cfg &= ~0xffff;

	mtk_w32(eth, fwd_cfg, MTK_GDMA1_FWD_CFG);
}

void mtk_csum_config(struct mtk_eth *eth)
{
	if (eth->soc->hw_features & NETIF_F_RXCSUM)
		mtk_w32(eth, mtk_r32(eth, MTK_GDMA1_FWD_CFG) |
			(MTK_GDM1_ICS_EN | MTK_GDM1_TCS_EN | MTK_GDM1_UCS_EN),
			MTK_GDMA1_FWD_CFG);
	else
		mtk_w32(eth, mtk_r32(eth, MTK_GDMA1_FWD_CFG) &
			~(MTK_GDM1_ICS_EN | MTK_GDM1_TCS_EN | MTK_GDM1_UCS_EN),
			MTK_GDMA1_FWD_CFG);
	if (eth->soc->hw_features & NETIF_F_IP_CSUM)
		mtk_w32(eth, mtk_r32(eth, MTK_CDMA_CSG_CFG) |
			(MTK_ICS_GEN_EN | MTK_TCS_GEN_EN | MTK_UCS_GEN_EN),
			MTK_CDMA_CSG_CFG);
	else
		mtk_w32(eth, mtk_r32(eth, MTK_CDMA_CSG_CFG) &
			~(MTK_ICS_GEN_EN | MTK_TCS_GEN_EN | MTK_UCS_GEN_EN),
			MTK_CDMA_CSG_CFG);
}

static int mtk_start_dma(struct mtk_eth *eth)
{
	unsigned long flags;
	u32 val;
	int err;

	if (eth->soc->dma_type == MTK_PDMA)
		err = mtk_pdma_init(eth);
	else if (eth->soc->dma_type == MTK_QDMA)
		err = mtk_qdma_init(eth, 0);
	else
		err = mtk_pdma_qdma_init(eth);
	if (err) {
		mtk_dma_free(eth);
		return err;
	}

	spin_lock_irqsave(&eth->page_lock, flags);

	val = MTK_TX_WB_DDONE | MTK_RX_DMA_EN | MTK_TX_DMA_EN;
	if (eth->soc->rx_2b_offset)
		val |= MTK_RX_2B_OFFSET;
	val |= eth->soc->pdma_glo_cfg;

	if (eth->soc->dma_type & MTK_PDMA)
		mtk_reg_w32(eth, val, MTK_REG_PDMA_GLO_CFG);

	if (eth->soc->dma_type & MTK_QDMA)
		mtk_w32(eth, val, MTK_QDMA_GLO_CFG);

	spin_unlock_irqrestore(&eth->page_lock, flags);

	return 0;
}

static int mtk_open(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;

	if (!atomic_read(&eth->dma_refcnt)) {
		int err = mtk_start_dma(eth);

		if (err)
			return err;

		napi_enable(&eth->rx_napi);
		mtk_irq_enable(eth, eth->soc->tx_int | eth->soc->rx_int);
	}
	atomic_inc(&eth->dma_refcnt);

	if (eth->phy)
		eth->phy->start(mac);

	if (eth->soc->has_carrier && eth->soc->has_carrier(eth))
		netif_carrier_on(dev);

	netif_start_queue(dev);
	eth->soc->fwd_config(eth);

	return 0;
}

static void mtk_stop_dma(struct mtk_eth *eth, u32 glo_cfg)
{
	unsigned long flags;
	u32 val;
	int i;

	/* stop the dma enfine */
	spin_lock_irqsave(&eth->page_lock, flags);
	val = mtk_r32(eth, glo_cfg);
	mtk_w32(eth, val & ~(MTK_TX_WB_DDONE | MTK_RX_DMA_EN | MTK_TX_DMA_EN),
		glo_cfg);
	spin_unlock_irqrestore(&eth->page_lock, flags);

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
	if (eth->phy)
		eth->phy->stop(mac);

	if (!atomic_dec_and_test(&eth->dma_refcnt))
		return 0;

	mtk_irq_disable(eth, eth->soc->tx_int | eth->soc->rx_int);
	napi_disable(&eth->rx_napi);

	if (eth->soc->dma_type & MTK_PDMA)
		mtk_stop_dma(eth, mtk_reg_table[MTK_REG_PDMA_GLO_CFG]);

	if (eth->soc->dma_type & MTK_QDMA)
		mtk_stop_dma(eth, MTK_QDMA_GLO_CFG);

	mtk_dma_free(eth);

	return 0;
}

static int __init mtk_init_hw(struct mtk_eth *eth)
{
	int i, err;

	eth->soc->reset_fe(eth);

	if (eth->soc->switch_init)
		if (eth->soc->switch_init(eth)) {
			dev_err(eth->dev, "failed to initialize switch core\n");
			return -ENODEV;
		}

	err = devm_request_irq(eth->dev, eth->irq, mtk_handle_irq, 0,
			       dev_name(eth->dev), eth);
	if (err)
		return err;

	err = mtk_mdio_init(eth);
	if (err)
		return err;

	/* disable delay and normal interrupt */
	mtk_reg_w32(eth, 0, MTK_REG_DLY_INT_CFG);
	if (eth->soc->dma_type & MTK_QDMA)
		mtk_w32(eth, 0, MTK_QDMA_DELAY_INT);
	mtk_irq_disable(eth, eth->soc->tx_int | eth->soc->rx_int);

	/* frame engine will push VLAN tag regarding to VIDX field in Tx desc */
	if (mtk_reg_table[MTK_REG_MTK_DMA_VID_BASE])
		for (i = 0; i < 16; i += 2)
			mtk_w32(eth, ((i + 1) << 16) + i,
				mtk_reg_table[MTK_REG_MTK_DMA_VID_BASE] +
				(i * 2));

	if (eth->soc->fwd_config(eth))
		dev_err(eth->dev, "unable to get clock\n");

	if (mtk_reg_table[MTK_REG_MTK_RST_GL]) {
		mtk_reg_w32(eth, 1, MTK_REG_MTK_RST_GL);
		mtk_reg_w32(eth, 0, MTK_REG_MTK_RST_GL);
	}

	return 0;
}

static int __init mtk_init(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	struct device_node *port;
	const char *mac_addr;
	int err;

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
	mac->hw->soc->set_mac(mac, dev->dev_addr);

	if (eth->soc->port_init)
		for_each_child_of_node(mac->of_node, port)
			if (of_device_is_compatible(port,
						    "mediatek,eth-port") &&
			    of_device_is_available(port))
				eth->soc->port_init(eth, mac, port);

	if (eth->phy) {
		err = eth->phy->connect(mac);
		if (err)
			return err;
	}

	return 0;
}

static void mtk_uninit(struct net_device *dev)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;

	if (eth->phy)
		eth->phy->disconnect(mac);
	mtk_mdio_cleanup(eth);

	mtk_irq_disable(eth, ~0);
	free_irq(dev->irq, dev);
}

static int mtk_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mtk_mac *mac = netdev_priv(dev);

	if (!mac->phy_dev)
		return -ENODEV;

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

static int mtk_change_mtu(struct net_device *dev, int new_mtu)
{
	struct mtk_mac *mac = netdev_priv(dev);
	struct mtk_eth *eth = mac->hw;
	int frag_size, old_mtu;
	u32 fwd_cfg;

	if (!eth->soc->jumbo_frame)
		return eth_change_mtu(dev, new_mtu);

	frag_size = mtk_max_frag_size(new_mtu);
	if (new_mtu < 68 || frag_size > PAGE_SIZE)
		return -EINVAL;

	old_mtu = dev->mtu;
	dev->mtu = new_mtu;

	/* return early if the buffer sizes will not change */
	if (old_mtu <= ETH_DATA_LEN && new_mtu <= ETH_DATA_LEN)
		return 0;
	if (old_mtu > ETH_DATA_LEN && new_mtu > ETH_DATA_LEN)
		return 0;

	if (new_mtu <= ETH_DATA_LEN)
		eth->rx_ring[0].frag_size = mtk_max_frag_size(ETH_DATA_LEN);
	else
		eth->rx_ring[0].frag_size = PAGE_SIZE;
	eth->rx_ring[0].rx_buf_size =
				mtk_max_buf_size(eth->rx_ring[0].frag_size);

	if (!netif_running(dev))
		return 0;

	mtk_stop(dev);
	fwd_cfg = mtk_r32(eth, MTK_GDMA1_FWD_CFG);
	if (new_mtu <= ETH_DATA_LEN) {
		fwd_cfg &= ~MTK_GDM1_JMB_EN;
	} else {
		fwd_cfg &= ~(MTK_GDM1_JMB_LEN_MASK << MTK_GDM1_JMB_LEN_SHIFT);
		fwd_cfg |= (DIV_ROUND_UP(frag_size, 1024) <<
				MTK_GDM1_JMB_LEN_SHIFT) | MTK_GDM1_JMB_EN;
	}
	mtk_w32(eth, fwd_cfg, MTK_GDMA1_FWD_CFG);

	return mtk_open(dev);
}

static void mtk_pending_work(struct work_struct *work)
{
	struct mtk_mac *mac = container_of(work, struct mtk_mac, pending_work);
	struct mtk_eth *eth = mac->hw;
	struct net_device *dev = eth->netdev[mac->id];
	int err;

	rtnl_lock();
	mtk_stop(dev);

	err = mtk_open(dev);
	if (err) {
		netif_alert(eth, ifup, dev,
			    "Driver up/down cycle failed, closing device.\n");
		dev_close(dev);
	}
	rtnl_unlock();
}

static int mtk_cleanup(struct mtk_eth *eth)
{
	int i;

	for (i = 0; i < eth->soc->mac_count; i++) {
		struct mtk_mac *mac = netdev_priv(eth->netdev[i]);

		if (!eth->netdev[i])
			continue;

		unregister_netdev(eth->netdev[i]);
		free_netdev(eth->netdev[i]);
		cancel_work_sync(&mac->pending_work);
	}

	return 0;
}

static const struct net_device_ops mtk_netdev_ops = {
	.ndo_init		= mtk_init,
	.ndo_uninit		= mtk_uninit,
	.ndo_open		= mtk_open,
	.ndo_stop		= mtk_stop,
	.ndo_start_xmit		= mtk_start_xmit,
	.ndo_set_mac_address	= mtk_set_mac_address,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl		= mtk_do_ioctl,
	.ndo_change_mtu		= mtk_change_mtu,
	.ndo_tx_timeout		= mtk_tx_timeout,
	.ndo_get_stats64        = mtk_get_stats64,
	.ndo_vlan_rx_add_vid	= mtk_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= mtk_vlan_rx_kill_vid,
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
	if (id >= eth->soc->mac_count || eth->netdev[id]) {
		dev_err(eth->dev, "%d is not a valid mac id\n", id);
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
	INIT_WORK(&mac->pending_work, mtk_pending_work);

	if (mtk_reg_table[MTK_REG_MTK_COUNTER_BASE]) {
		mac->hw_stats = devm_kzalloc(eth->dev,
					     sizeof(*mac->hw_stats),
					     GFP_KERNEL);
		if (!mac->hw_stats) {
			err = -ENOMEM;
			goto free_netdev;
		}
		spin_lock_init(&mac->hw_stats->stats_lock);
		mac->hw_stats->reg_offset = id * MTK_STAT_OFFSET;
	}

	SET_NETDEV_DEV(eth->netdev[id], eth->dev);
	eth->netdev[id]->netdev_ops = &mtk_netdev_ops;
	eth->netdev[id]->base_addr = (unsigned long)eth->base;

	if (eth->soc->init_data)
		eth->soc->init_data(eth->soc, eth->netdev[id]);

	eth->netdev[id]->vlan_features = eth->soc->hw_features &
		~(NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX);
	eth->netdev[id]->features |= eth->soc->hw_features;

	if (mtk_reg_table[MTK_REG_MTK_DMA_VID_BASE])
		eth->netdev[id]->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	mtk_set_ethtool_ops(eth->netdev[id]);

	err = register_netdev(eth->netdev[id]);
	if (err) {
		dev_err(eth->dev, "error bringing up device\n");
		err = -ENOMEM;
		goto free_netdev;
	}
	eth->netdev[id]->irq = eth->irq;
	netif_info(eth, probe, eth->netdev[id],
		   "mediatek frame engine at 0x%08lx, irq %d\n",
		   eth->netdev[id]->base_addr, eth->netdev[id]->irq);

	return 0;

free_netdev:
	free_netdev(eth->netdev[id]);
	return err;
}

static int mtk_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	const struct of_device_id *match;
	struct device_node *mac_np;
	struct mtk_soc_data *soc;
	struct mtk_eth *eth;
	struct clk *sysclk;
	int err;

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	device_reset(&pdev->dev);

	match = of_match_device(of_mtk_match, &pdev->dev);
	soc = (struct mtk_soc_data *)match->data;

	if (soc->reg_table)
		mtk_reg_table = soc->reg_table;

	eth = devm_kzalloc(&pdev->dev, sizeof(*eth), GFP_KERNEL);
	if (!eth)
		return -ENOMEM;

	eth->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(eth->base))
		return PTR_ERR(eth->base);

	spin_lock_init(&eth->page_lock);

	eth->ethsys = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						      "mediatek,ethsys");
	if (IS_ERR(eth->ethsys))
		return PTR_ERR(eth->ethsys);

	eth->irq = platform_get_irq(pdev, 0);
	if (eth->irq < 0) {
		dev_err(&pdev->dev, "no IRQ resource found\n");
		return -ENXIO;
	}

	sysclk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(sysclk)) {
		dev_err(&pdev->dev,
			"the clock is not defined in the devicetree\n");
		return -ENXIO;
	}
	eth->sysclk = clk_get_rate(sysclk);

	eth->switch_np = of_parse_phandle(pdev->dev.of_node,
					  "mediatek,switch", 0);
	if (soc->has_switch && !eth->switch_np) {
		dev_err(&pdev->dev, "failed to read switch phandle\n");
		return -ENODEV;
	}

	eth->dev = &pdev->dev;
	eth->soc = soc;
	eth->msg_enable = netif_msg_init(mtk_msg_level, MTK_DEFAULT_MSG_ENABLE);

	err = mtk_init_hw(eth);
	if (err)
		return err;

	if (eth->soc->mac_count > 1) {
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

		init_dummy_netdev(&eth->dummy_dev);
		netif_napi_add(&eth->dummy_dev, &eth->rx_napi, mtk_poll,
			       soc->napi_weight);
	} else {
		err = mtk_add_mac(eth, pdev->dev.of_node);
		if (err)
			goto err_free_dev;
		netif_napi_add(eth->netdev[0], &eth->rx_napi, mtk_poll,
			       soc->napi_weight);
	}

	platform_set_drvdata(pdev, eth);

	return 0;

err_free_dev:
	mtk_cleanup(eth);
	return err;
}

static int mtk_remove(struct platform_device *pdev)
{
	struct mtk_eth *eth = platform_get_drvdata(pdev);

	netif_napi_del(&eth->rx_napi);
	mtk_cleanup(eth);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver mtk_driver = {
	.probe = mtk_probe,
	.remove = mtk_remove,
	.driver = {
		.name = "mtk_soc_eth",
		.owner = THIS_MODULE,
		.of_match_table = of_mtk_match,
	},
};

module_platform_driver(mtk_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Crispin <blogic@openwrt.org>");
MODULE_DESCRIPTION("Ethernet driver for MediaTek SoC");
