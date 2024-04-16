// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Rafał Miłecki <rafal@milecki.pl>
 */

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "bcm4908_enet.h"
#include "unimac.h"

#define ENET_DMA_CH_RX_CFG			ENET_DMA_CH0_CFG
#define ENET_DMA_CH_TX_CFG			ENET_DMA_CH1_CFG
#define ENET_DMA_CH_RX_STATE_RAM		ENET_DMA_CH0_STATE_RAM
#define ENET_DMA_CH_TX_STATE_RAM		ENET_DMA_CH1_STATE_RAM

#define ENET_TX_BDS_NUM				200
#define ENET_RX_BDS_NUM				200
#define ENET_RX_BDS_NUM_MAX			8192

#define ENET_DMA_INT_DEFAULTS			(ENET_DMA_CH_CFG_INT_DONE | \
						 ENET_DMA_CH_CFG_INT_NO_DESC | \
						 ENET_DMA_CH_CFG_INT_BUFF_DONE)
#define ENET_DMA_MAX_BURST_LEN			8 /* in 64 bit words */

#define ENET_MTU_MAX				ETH_DATA_LEN /* Is it possible to support 2044? */
#define BRCM_MAX_TAG_LEN			6
#define ENET_MAX_ETH_OVERHEAD			(ETH_HLEN + BRCM_MAX_TAG_LEN + VLAN_HLEN + \
						 ETH_FCS_LEN + 4) /* 32 */

struct bcm4908_enet_dma_ring_bd {
	__le32 ctl;
	__le32 addr;
} __packed;

struct bcm4908_enet_dma_ring_slot {
	struct sk_buff *skb;
	unsigned int len;
	dma_addr_t dma_addr;
};

struct bcm4908_enet_dma_ring {
	int is_tx;
	int read_idx;
	int write_idx;
	int length;
	u16 cfg_block;
	u16 st_ram_block;
	struct napi_struct napi;

	union {
		void *cpu_addr;
		struct bcm4908_enet_dma_ring_bd *buf_desc;
	};
	dma_addr_t dma_addr;

	struct bcm4908_enet_dma_ring_slot *slots;
};

struct bcm4908_enet {
	struct device *dev;
	struct net_device *netdev;
	void __iomem *base;
	int irq_tx;

	struct bcm4908_enet_dma_ring tx_ring;
	struct bcm4908_enet_dma_ring rx_ring;
};

/***
 * R/W ops
 */

static u32 enet_read(struct bcm4908_enet *enet, u16 offset)
{
	return readl(enet->base + offset);
}

static void enet_write(struct bcm4908_enet *enet, u16 offset, u32 value)
{
	writel(value, enet->base + offset);
}

static void enet_maskset(struct bcm4908_enet *enet, u16 offset, u32 mask, u32 set)
{
	u32 val;

	WARN_ON(set & ~mask);

	val = enet_read(enet, offset);
	val = (val & ~mask) | (set & mask);
	enet_write(enet, offset, val);
}

static void enet_set(struct bcm4908_enet *enet, u16 offset, u32 set)
{
	enet_maskset(enet, offset, set, set);
}

static u32 enet_umac_read(struct bcm4908_enet *enet, u16 offset)
{
	return enet_read(enet, ENET_UNIMAC + offset);
}

static void enet_umac_write(struct bcm4908_enet *enet, u16 offset, u32 value)
{
	enet_write(enet, ENET_UNIMAC + offset, value);
}

static void enet_umac_set(struct bcm4908_enet *enet, u16 offset, u32 set)
{
	enet_set(enet, ENET_UNIMAC + offset, set);
}

/***
 * Helpers
 */

static void bcm4908_enet_set_mtu(struct bcm4908_enet *enet, int mtu)
{
	enet_umac_write(enet, UMAC_MAX_FRAME_LEN, mtu + ENET_MAX_ETH_OVERHEAD);
}

/***
 * DMA ring ops
 */

static void bcm4908_enet_dma_ring_intrs_on(struct bcm4908_enet *enet,
					   struct bcm4908_enet_dma_ring *ring)
{
	enet_write(enet, ring->cfg_block + ENET_DMA_CH_CFG_INT_MASK, ENET_DMA_INT_DEFAULTS);
}

static void bcm4908_enet_dma_ring_intrs_off(struct bcm4908_enet *enet,
					    struct bcm4908_enet_dma_ring *ring)
{
	enet_write(enet, ring->cfg_block + ENET_DMA_CH_CFG_INT_MASK, 0);
}

static void bcm4908_enet_dma_ring_intrs_ack(struct bcm4908_enet *enet,
					    struct bcm4908_enet_dma_ring *ring)
{
	enet_write(enet, ring->cfg_block + ENET_DMA_CH_CFG_INT_STAT, ENET_DMA_INT_DEFAULTS);
}

/***
 * DMA
 */

static int bcm4908_dma_alloc_buf_descs(struct bcm4908_enet *enet,
				       struct bcm4908_enet_dma_ring *ring)
{
	int size = ring->length * sizeof(struct bcm4908_enet_dma_ring_bd);
	struct device *dev = enet->dev;

	ring->cpu_addr = dma_alloc_coherent(dev, size, &ring->dma_addr, GFP_KERNEL);
	if (!ring->cpu_addr)
		return -ENOMEM;

	if (((uintptr_t)ring->cpu_addr) & (0x40 - 1)) {
		dev_err(dev, "Invalid DMA ring alignment\n");
		goto err_free_buf_descs;
	}

	ring->slots = kcalloc(ring->length, sizeof(*ring->slots), GFP_KERNEL);
	if (!ring->slots)
		goto err_free_buf_descs;

	return 0;

err_free_buf_descs:
	dma_free_coherent(dev, size, ring->cpu_addr, ring->dma_addr);
	ring->cpu_addr = NULL;
	return -ENOMEM;
}

static void bcm4908_enet_dma_free(struct bcm4908_enet *enet)
{
	struct bcm4908_enet_dma_ring *tx_ring = &enet->tx_ring;
	struct bcm4908_enet_dma_ring *rx_ring = &enet->rx_ring;
	struct device *dev = enet->dev;
	int size;

	size = rx_ring->length * sizeof(struct bcm4908_enet_dma_ring_bd);
	if (rx_ring->cpu_addr)
		dma_free_coherent(dev, size, rx_ring->cpu_addr, rx_ring->dma_addr);
	kfree(rx_ring->slots);

	size = tx_ring->length * sizeof(struct bcm4908_enet_dma_ring_bd);
	if (tx_ring->cpu_addr)
		dma_free_coherent(dev, size, tx_ring->cpu_addr, tx_ring->dma_addr);
	kfree(tx_ring->slots);
}

static int bcm4908_enet_dma_alloc(struct bcm4908_enet *enet)
{
	struct bcm4908_enet_dma_ring *tx_ring = &enet->tx_ring;
	struct bcm4908_enet_dma_ring *rx_ring = &enet->rx_ring;
	struct device *dev = enet->dev;
	int err;

	tx_ring->length = ENET_TX_BDS_NUM;
	tx_ring->is_tx = 1;
	tx_ring->cfg_block = ENET_DMA_CH_TX_CFG;
	tx_ring->st_ram_block = ENET_DMA_CH_TX_STATE_RAM;
	err = bcm4908_dma_alloc_buf_descs(enet, tx_ring);
	if (err) {
		dev_err(dev, "Failed to alloc TX buf descriptors: %d\n", err);
		return err;
	}

	rx_ring->length = ENET_RX_BDS_NUM;
	rx_ring->is_tx = 0;
	rx_ring->cfg_block = ENET_DMA_CH_RX_CFG;
	rx_ring->st_ram_block = ENET_DMA_CH_RX_STATE_RAM;
	err = bcm4908_dma_alloc_buf_descs(enet, rx_ring);
	if (err) {
		dev_err(dev, "Failed to alloc RX buf descriptors: %d\n", err);
		bcm4908_enet_dma_free(enet);
		return err;
	}

	return 0;
}

static void bcm4908_enet_dma_reset(struct bcm4908_enet *enet)
{
	struct bcm4908_enet_dma_ring *rings[] = { &enet->rx_ring, &enet->tx_ring };
	int i;

	/* Disable the DMA controller and channel */
	for (i = 0; i < ARRAY_SIZE(rings); i++)
		enet_write(enet, rings[i]->cfg_block + ENET_DMA_CH_CFG, 0);
	enet_maskset(enet, ENET_DMA_CONTROLLER_CFG, ENET_DMA_CTRL_CFG_MASTER_EN, 0);

	/* Reset channels state */
	for (i = 0; i < ARRAY_SIZE(rings); i++) {
		struct bcm4908_enet_dma_ring *ring = rings[i];

		enet_write(enet, ring->st_ram_block + ENET_DMA_CH_STATE_RAM_BASE_DESC_PTR, 0);
		enet_write(enet, ring->st_ram_block + ENET_DMA_CH_STATE_RAM_STATE_DATA, 0);
		enet_write(enet, ring->st_ram_block + ENET_DMA_CH_STATE_RAM_DESC_LEN_STATUS, 0);
		enet_write(enet, ring->st_ram_block + ENET_DMA_CH_STATE_RAM_DESC_BASE_BUFPTR, 0);
	}
}

static int bcm4908_enet_dma_alloc_rx_buf(struct bcm4908_enet *enet, unsigned int idx)
{
	struct bcm4908_enet_dma_ring_bd *buf_desc = &enet->rx_ring.buf_desc[idx];
	struct bcm4908_enet_dma_ring_slot *slot = &enet->rx_ring.slots[idx];
	struct device *dev = enet->dev;
	u32 tmp;
	int err;

	slot->len = ENET_MTU_MAX + ENET_MAX_ETH_OVERHEAD;

	slot->skb = netdev_alloc_skb(enet->netdev, slot->len);
	if (!slot->skb)
		return -ENOMEM;

	slot->dma_addr = dma_map_single(dev, slot->skb->data, slot->len, DMA_FROM_DEVICE);
	err = dma_mapping_error(dev, slot->dma_addr);
	if (err) {
		dev_err(dev, "Failed to map DMA buffer: %d\n", err);
		kfree_skb(slot->skb);
		slot->skb = NULL;
		return err;
	}

	tmp = slot->len << DMA_CTL_LEN_DESC_BUFLENGTH_SHIFT;
	tmp |= DMA_CTL_STATUS_OWN;
	if (idx == enet->rx_ring.length - 1)
		tmp |= DMA_CTL_STATUS_WRAP;
	buf_desc->ctl = cpu_to_le32(tmp);
	buf_desc->addr = cpu_to_le32(slot->dma_addr);

	return 0;
}

static void bcm4908_enet_dma_ring_init(struct bcm4908_enet *enet,
				       struct bcm4908_enet_dma_ring *ring)
{
	int reset_channel = 0; /* We support only 1 main channel (with TX and RX) */
	int reset_subch = ring->is_tx ? 1 : 0;

	/* Reset the DMA channel */
	enet_write(enet, ENET_DMA_CTRL_CHANNEL_RESET, BIT(reset_channel * 2 + reset_subch));
	enet_write(enet, ENET_DMA_CTRL_CHANNEL_RESET, 0);

	enet_write(enet, ring->cfg_block + ENET_DMA_CH_CFG, 0);
	enet_write(enet, ring->cfg_block + ENET_DMA_CH_CFG_MAX_BURST, ENET_DMA_MAX_BURST_LEN);
	enet_write(enet, ring->cfg_block + ENET_DMA_CH_CFG_INT_MASK, 0);

	enet_write(enet, ring->st_ram_block + ENET_DMA_CH_STATE_RAM_BASE_DESC_PTR,
		   (uint32_t)ring->dma_addr);

	ring->read_idx = 0;
	ring->write_idx = 0;
}

static void bcm4908_enet_dma_uninit(struct bcm4908_enet *enet)
{
	struct bcm4908_enet_dma_ring *rx_ring = &enet->rx_ring;
	struct bcm4908_enet_dma_ring_slot *slot;
	struct device *dev = enet->dev;
	int i;

	for (i = rx_ring->length - 1; i >= 0; i--) {
		slot = &rx_ring->slots[i];
		if (!slot->skb)
			continue;
		dma_unmap_single(dev, slot->dma_addr, slot->len, DMA_FROM_DEVICE);
		kfree_skb(slot->skb);
		slot->skb = NULL;
	}
}

static int bcm4908_enet_dma_init(struct bcm4908_enet *enet)
{
	struct bcm4908_enet_dma_ring *rx_ring = &enet->rx_ring;
	struct device *dev = enet->dev;
	int err;
	int i;

	for (i = 0; i < rx_ring->length; i++) {
		err = bcm4908_enet_dma_alloc_rx_buf(enet, i);
		if (err) {
			dev_err(dev, "Failed to alloc RX buffer: %d\n", err);
			bcm4908_enet_dma_uninit(enet);
			return err;
		}
	}

	bcm4908_enet_dma_ring_init(enet, &enet->tx_ring);
	bcm4908_enet_dma_ring_init(enet, &enet->rx_ring);

	return 0;
}

static void bcm4908_enet_dma_tx_ring_enable(struct bcm4908_enet *enet,
					    struct bcm4908_enet_dma_ring *ring)
{
	enet_write(enet, ring->cfg_block + ENET_DMA_CH_CFG, ENET_DMA_CH_CFG_ENABLE);
}

static void bcm4908_enet_dma_tx_ring_disable(struct bcm4908_enet *enet,
					     struct bcm4908_enet_dma_ring *ring)
{
	enet_write(enet, ring->cfg_block + ENET_DMA_CH_CFG, 0);
}

static void bcm4908_enet_dma_rx_ring_enable(struct bcm4908_enet *enet,
					    struct bcm4908_enet_dma_ring *ring)
{
	enet_set(enet, ring->cfg_block + ENET_DMA_CH_CFG, ENET_DMA_CH_CFG_ENABLE);
}

static void bcm4908_enet_dma_rx_ring_disable(struct bcm4908_enet *enet,
					     struct bcm4908_enet_dma_ring *ring)
{
	unsigned long deadline;
	u32 tmp;

	enet_maskset(enet, ring->cfg_block + ENET_DMA_CH_CFG, ENET_DMA_CH_CFG_ENABLE, 0);

	deadline = jiffies + usecs_to_jiffies(2000);
	do {
		tmp = enet_read(enet, ring->cfg_block + ENET_DMA_CH_CFG);
		if (!(tmp & ENET_DMA_CH_CFG_ENABLE))
			return;
		enet_maskset(enet, ring->cfg_block + ENET_DMA_CH_CFG, ENET_DMA_CH_CFG_ENABLE, 0);
		usleep_range(10, 30);
	} while (!time_after_eq(jiffies, deadline));

	dev_warn(enet->dev, "Timeout waiting for DMA TX stop\n");
}

/***
 * Ethernet driver
 */

static void bcm4908_enet_gmac_init(struct bcm4908_enet *enet)
{
	u32 cmd;

	bcm4908_enet_set_mtu(enet, enet->netdev->mtu);

	cmd = enet_umac_read(enet, UMAC_CMD);
	enet_umac_write(enet, UMAC_CMD, cmd | CMD_SW_RESET);
	enet_umac_write(enet, UMAC_CMD, cmd & ~CMD_SW_RESET);

	enet_set(enet, ENET_FLUSH, ENET_FLUSH_RXFIFO_FLUSH | ENET_FLUSH_TXFIFO_FLUSH);
	enet_maskset(enet, ENET_FLUSH, ENET_FLUSH_RXFIFO_FLUSH | ENET_FLUSH_TXFIFO_FLUSH, 0);

	enet_set(enet, ENET_MIB_CTRL, ENET_MIB_CTRL_CLR_MIB);
	enet_maskset(enet, ENET_MIB_CTRL, ENET_MIB_CTRL_CLR_MIB, 0);

	cmd = enet_umac_read(enet, UMAC_CMD);
	cmd &= ~(CMD_SPEED_MASK << CMD_SPEED_SHIFT);
	cmd &= ~CMD_TX_EN;
	cmd &= ~CMD_RX_EN;
	cmd |= CMD_SPEED_1000 << CMD_SPEED_SHIFT;
	enet_umac_write(enet, UMAC_CMD, cmd);

	enet_maskset(enet, ENET_GMAC_STATUS,
		     ENET_GMAC_STATUS_ETH_SPEED_MASK |
		     ENET_GMAC_STATUS_HD |
		     ENET_GMAC_STATUS_AUTO_CFG_EN |
		     ENET_GMAC_STATUS_LINK_UP,
		     ENET_GMAC_STATUS_ETH_SPEED_1000 |
		     ENET_GMAC_STATUS_AUTO_CFG_EN |
		     ENET_GMAC_STATUS_LINK_UP);
}

static irqreturn_t bcm4908_enet_irq_handler(int irq, void *dev_id)
{
	struct bcm4908_enet *enet = dev_id;
	struct bcm4908_enet_dma_ring *ring;

	ring = (irq == enet->irq_tx) ? &enet->tx_ring : &enet->rx_ring;

	bcm4908_enet_dma_ring_intrs_off(enet, ring);
	bcm4908_enet_dma_ring_intrs_ack(enet, ring);

	napi_schedule(&ring->napi);

	return IRQ_HANDLED;
}

static int bcm4908_enet_open(struct net_device *netdev)
{
	struct bcm4908_enet *enet = netdev_priv(netdev);
	struct bcm4908_enet_dma_ring *tx_ring = &enet->tx_ring;
	struct bcm4908_enet_dma_ring *rx_ring = &enet->rx_ring;
	struct device *dev = enet->dev;
	int err;

	err = request_irq(netdev->irq, bcm4908_enet_irq_handler, 0, "enet", enet);
	if (err) {
		dev_err(dev, "Failed to request IRQ %d: %d\n", netdev->irq, err);
		return err;
	}

	if (enet->irq_tx > 0) {
		err = request_irq(enet->irq_tx, bcm4908_enet_irq_handler, 0,
				  "tx", enet);
		if (err) {
			dev_err(dev, "Failed to request IRQ %d: %d\n",
				enet->irq_tx, err);
			free_irq(netdev->irq, enet);
			return err;
		}
	}

	bcm4908_enet_gmac_init(enet);
	bcm4908_enet_dma_reset(enet);
	bcm4908_enet_dma_init(enet);

	enet_umac_set(enet, UMAC_CMD, CMD_TX_EN | CMD_RX_EN);

	enet_set(enet, ENET_DMA_CONTROLLER_CFG, ENET_DMA_CTRL_CFG_MASTER_EN);
	enet_maskset(enet, ENET_DMA_CONTROLLER_CFG, ENET_DMA_CTRL_CFG_FLOWC_CH1_EN, 0);

	if (enet->irq_tx > 0) {
		napi_enable(&tx_ring->napi);
		bcm4908_enet_dma_ring_intrs_ack(enet, tx_ring);
		bcm4908_enet_dma_ring_intrs_on(enet, tx_ring);
	}

	bcm4908_enet_dma_rx_ring_enable(enet, rx_ring);
	napi_enable(&rx_ring->napi);
	netif_carrier_on(netdev);
	netif_start_queue(netdev);
	bcm4908_enet_dma_ring_intrs_ack(enet, rx_ring);
	bcm4908_enet_dma_ring_intrs_on(enet, rx_ring);

	return 0;
}

static int bcm4908_enet_stop(struct net_device *netdev)
{
	struct bcm4908_enet *enet = netdev_priv(netdev);
	struct bcm4908_enet_dma_ring *tx_ring = &enet->tx_ring;
	struct bcm4908_enet_dma_ring *rx_ring = &enet->rx_ring;

	netif_stop_queue(netdev);
	netif_carrier_off(netdev);
	napi_disable(&rx_ring->napi);
	napi_disable(&tx_ring->napi);

	bcm4908_enet_dma_rx_ring_disable(enet, &enet->rx_ring);
	bcm4908_enet_dma_tx_ring_disable(enet, &enet->tx_ring);

	bcm4908_enet_dma_uninit(enet);

	free_irq(enet->irq_tx, enet);
	free_irq(enet->netdev->irq, enet);

	return 0;
}

static netdev_tx_t bcm4908_enet_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct bcm4908_enet *enet = netdev_priv(netdev);
	struct bcm4908_enet_dma_ring *ring = &enet->tx_ring;
	struct bcm4908_enet_dma_ring_slot *slot;
	struct device *dev = enet->dev;
	struct bcm4908_enet_dma_ring_bd *buf_desc;
	int free_buf_descs;
	u32 tmp;

	/* Free transmitted skbs */
	if (enet->irq_tx < 0 &&
	    !(le32_to_cpu(ring->buf_desc[ring->read_idx].ctl) & DMA_CTL_STATUS_OWN))
		napi_schedule(&enet->tx_ring.napi);

	/* Don't use the last empty buf descriptor */
	if (ring->read_idx <= ring->write_idx)
		free_buf_descs = ring->read_idx - ring->write_idx + ring->length;
	else
		free_buf_descs = ring->read_idx - ring->write_idx;
	if (free_buf_descs < 2) {
		netif_stop_queue(netdev);
		return NETDEV_TX_BUSY;
	}

	/* Hardware removes OWN bit after sending data */
	buf_desc = &ring->buf_desc[ring->write_idx];
	if (unlikely(le32_to_cpu(buf_desc->ctl) & DMA_CTL_STATUS_OWN)) {
		netif_stop_queue(netdev);
		return NETDEV_TX_BUSY;
	}

	slot = &ring->slots[ring->write_idx];
	slot->skb = skb;
	slot->len = skb->len;
	slot->dma_addr = dma_map_single(dev, skb->data, skb->len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, slot->dma_addr)))
		return NETDEV_TX_BUSY;

	tmp = skb->len << DMA_CTL_LEN_DESC_BUFLENGTH_SHIFT;
	tmp |= DMA_CTL_STATUS_OWN;
	tmp |= DMA_CTL_STATUS_SOP;
	tmp |= DMA_CTL_STATUS_EOP;
	tmp |= DMA_CTL_STATUS_APPEND_CRC;
	if (ring->write_idx + 1 == ring->length - 1)
		tmp |= DMA_CTL_STATUS_WRAP;

	buf_desc->addr = cpu_to_le32((uint32_t)slot->dma_addr);
	buf_desc->ctl = cpu_to_le32(tmp);

	bcm4908_enet_dma_tx_ring_enable(enet, &enet->tx_ring);

	if (++ring->write_idx == ring->length - 1)
		ring->write_idx = 0;

	return NETDEV_TX_OK;
}

static int bcm4908_enet_poll_rx(struct napi_struct *napi, int weight)
{
	struct bcm4908_enet_dma_ring *rx_ring = container_of(napi, struct bcm4908_enet_dma_ring, napi);
	struct bcm4908_enet *enet = container_of(rx_ring, struct bcm4908_enet, rx_ring);
	struct device *dev = enet->dev;
	int handled = 0;

	while (handled < weight) {
		struct bcm4908_enet_dma_ring_bd *buf_desc;
		struct bcm4908_enet_dma_ring_slot slot;
		u32 ctl;
		int len;
		int err;

		buf_desc = &enet->rx_ring.buf_desc[enet->rx_ring.read_idx];
		ctl = le32_to_cpu(buf_desc->ctl);
		if (ctl & DMA_CTL_STATUS_OWN)
			break;

		slot = enet->rx_ring.slots[enet->rx_ring.read_idx];

		/* Provide new buffer before unpinning the old one */
		err = bcm4908_enet_dma_alloc_rx_buf(enet, enet->rx_ring.read_idx);
		if (err)
			break;

		if (++enet->rx_ring.read_idx == enet->rx_ring.length)
			enet->rx_ring.read_idx = 0;

		len = (ctl & DMA_CTL_LEN_DESC_BUFLENGTH) >> DMA_CTL_LEN_DESC_BUFLENGTH_SHIFT;

		if (len < ETH_ZLEN ||
		    (ctl & (DMA_CTL_STATUS_SOP | DMA_CTL_STATUS_EOP)) != (DMA_CTL_STATUS_SOP | DMA_CTL_STATUS_EOP)) {
			kfree_skb(slot.skb);
			enet->netdev->stats.rx_dropped++;
			break;
		}

		dma_unmap_single(dev, slot.dma_addr, slot.len, DMA_FROM_DEVICE);

		skb_put(slot.skb, len - ETH_FCS_LEN);
		slot.skb->protocol = eth_type_trans(slot.skb, enet->netdev);
		netif_receive_skb(slot.skb);

		enet->netdev->stats.rx_packets++;
		enet->netdev->stats.rx_bytes += len;

		handled++;
	}

	if (handled < weight) {
		napi_complete_done(napi, handled);
		bcm4908_enet_dma_ring_intrs_on(enet, rx_ring);
	}

	/* Hardware could disable ring if it run out of descriptors */
	bcm4908_enet_dma_rx_ring_enable(enet, &enet->rx_ring);

	return handled;
}

static int bcm4908_enet_poll_tx(struct napi_struct *napi, int weight)
{
	struct bcm4908_enet_dma_ring *tx_ring = container_of(napi, struct bcm4908_enet_dma_ring, napi);
	struct bcm4908_enet *enet = container_of(tx_ring, struct bcm4908_enet, tx_ring);
	struct bcm4908_enet_dma_ring_bd *buf_desc;
	struct bcm4908_enet_dma_ring_slot *slot;
	struct device *dev = enet->dev;
	unsigned int bytes = 0;
	int handled = 0;

	while (handled < weight && tx_ring->read_idx != tx_ring->write_idx) {
		buf_desc = &tx_ring->buf_desc[tx_ring->read_idx];
		if (le32_to_cpu(buf_desc->ctl) & DMA_CTL_STATUS_OWN)
			break;
		slot = &tx_ring->slots[tx_ring->read_idx];

		dma_unmap_single(dev, slot->dma_addr, slot->len, DMA_TO_DEVICE);
		dev_kfree_skb(slot->skb);

		handled++;
		bytes += slot->len;

		if (++tx_ring->read_idx == tx_ring->length)
			tx_ring->read_idx = 0;
	}

	enet->netdev->stats.tx_packets += handled;
	enet->netdev->stats.tx_bytes += bytes;

	if (handled < weight) {
		napi_complete_done(napi, handled);
		bcm4908_enet_dma_ring_intrs_on(enet, tx_ring);
	}

	if (netif_queue_stopped(enet->netdev))
		netif_wake_queue(enet->netdev);

	return handled;
}

static int bcm4908_enet_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct bcm4908_enet *enet = netdev_priv(netdev);

	bcm4908_enet_set_mtu(enet, new_mtu);

	return 0;
}

static const struct net_device_ops bcm4908_enet_netdev_ops = {
	.ndo_open = bcm4908_enet_open,
	.ndo_stop = bcm4908_enet_stop,
	.ndo_start_xmit = bcm4908_enet_start_xmit,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_change_mtu = bcm4908_enet_change_mtu,
};

static int bcm4908_enet_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct net_device *netdev;
	struct bcm4908_enet *enet;
	int err;

	netdev = devm_alloc_etherdev(dev, sizeof(*enet));
	if (!netdev)
		return -ENOMEM;

	enet = netdev_priv(netdev);
	enet->dev = dev;
	enet->netdev = netdev;

	enet->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(enet->base)) {
		dev_err(dev, "Failed to map registers: %ld\n", PTR_ERR(enet->base));
		return PTR_ERR(enet->base);
	}

	netdev->irq = platform_get_irq_byname(pdev, "rx");
	if (netdev->irq < 0)
		return netdev->irq;

	enet->irq_tx = platform_get_irq_byname(pdev, "tx");

	err = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (err)
		return err;

	err = bcm4908_enet_dma_alloc(enet);
	if (err)
		return err;

	SET_NETDEV_DEV(netdev, &pdev->dev);
	err = of_get_ethdev_address(dev->of_node, netdev);
	if (err == -EPROBE_DEFER)
		goto err_dma_free;
	if (err)
		eth_hw_addr_random(netdev);
	netdev->netdev_ops = &bcm4908_enet_netdev_ops;
	netdev->min_mtu = ETH_ZLEN;
	netdev->mtu = ETH_DATA_LEN;
	netdev->max_mtu = ENET_MTU_MAX;
	netif_napi_add_tx(netdev, &enet->tx_ring.napi, bcm4908_enet_poll_tx);
	netif_napi_add(netdev, &enet->rx_ring.napi, bcm4908_enet_poll_rx);

	err = register_netdev(netdev);
	if (err)
		goto err_dma_free;

	platform_set_drvdata(pdev, enet);

	return 0;

err_dma_free:
	bcm4908_enet_dma_free(enet);

	return err;
}

static int bcm4908_enet_remove(struct platform_device *pdev)
{
	struct bcm4908_enet *enet = platform_get_drvdata(pdev);

	unregister_netdev(enet->netdev);
	netif_napi_del(&enet->rx_ring.napi);
	netif_napi_del(&enet->tx_ring.napi);
	bcm4908_enet_dma_free(enet);

	return 0;
}

static const struct of_device_id bcm4908_enet_of_match[] = {
	{ .compatible = "brcm,bcm4908-enet"},
	{},
};

static struct platform_driver bcm4908_enet_driver = {
	.driver = {
		.name = "bcm4908_enet",
		.of_match_table = bcm4908_enet_of_match,
	},
	.probe	= bcm4908_enet_probe,
	.remove = bcm4908_enet_remove,
};
module_platform_driver(bcm4908_enet_driver);

MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, bcm4908_enet_of_match);
