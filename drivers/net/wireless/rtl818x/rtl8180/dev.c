
/*
 * Linux device driver for RTL8180 / RTL8185
 *
 * Copyright 2007 Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007 Andrea Merello <andrea.merello@gmail.com>
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 *
 * Thanks to Realtek for their support!
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/eeprom_93cx6.h>
#include <linux/module.h>
#include <net/mac80211.h>

#include "rtl8180.h"
#include "rtl8225.h"
#include "sa2400.h"
#include "max2820.h"
#include "grf5101.h"

MODULE_AUTHOR("Michael Wu <flamingice@sourmilk.net>");
MODULE_AUTHOR("Andrea Merello <andrea.merello@gmail.com>");
MODULE_DESCRIPTION("RTL8180 / RTL8185 PCI wireless driver");
MODULE_LICENSE("GPL");

static DEFINE_PCI_DEVICE_TABLE(rtl8180_table) = {
	/* rtl8185 */
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8185) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BELKIN, 0x700f) },
	{ PCI_DEVICE(PCI_VENDOR_ID_BELKIN, 0x701f) },

	/* rtl8180 */
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8180) },
	{ PCI_DEVICE(0x1799, 0x6001) },
	{ PCI_DEVICE(0x1799, 0x6020) },
	{ PCI_DEVICE(PCI_VENDOR_ID_DLINK, 0x3300) },
	{ PCI_DEVICE(0x1186, 0x3301) },
	{ PCI_DEVICE(0x1432, 0x7106) },
	{ }
};

MODULE_DEVICE_TABLE(pci, rtl8180_table);

static const struct ieee80211_rate rtl818x_rates[] = {
	{ .bitrate = 10, .hw_value = 0, },
	{ .bitrate = 20, .hw_value = 1, },
	{ .bitrate = 55, .hw_value = 2, },
	{ .bitrate = 110, .hw_value = 3, },
	{ .bitrate = 60, .hw_value = 4, },
	{ .bitrate = 90, .hw_value = 5, },
	{ .bitrate = 120, .hw_value = 6, },
	{ .bitrate = 180, .hw_value = 7, },
	{ .bitrate = 240, .hw_value = 8, },
	{ .bitrate = 360, .hw_value = 9, },
	{ .bitrate = 480, .hw_value = 10, },
	{ .bitrate = 540, .hw_value = 11, },
};

static const struct ieee80211_channel rtl818x_channels[] = {
	{ .center_freq = 2412 },
	{ .center_freq = 2417 },
	{ .center_freq = 2422 },
	{ .center_freq = 2427 },
	{ .center_freq = 2432 },
	{ .center_freq = 2437 },
	{ .center_freq = 2442 },
	{ .center_freq = 2447 },
	{ .center_freq = 2452 },
	{ .center_freq = 2457 },
	{ .center_freq = 2462 },
	{ .center_freq = 2467 },
	{ .center_freq = 2472 },
	{ .center_freq = 2484 },
};


void rtl8180_write_phy(struct ieee80211_hw *dev, u8 addr, u32 data)
{
	struct rtl8180_priv *priv = dev->priv;
	int i = 10;
	u32 buf;

	buf = (data << 8) | addr;

	rtl818x_iowrite32(priv, (__le32 __iomem *)&priv->map->PHY[0], buf | 0x80);
	while (i--) {
		rtl818x_iowrite32(priv, (__le32 __iomem *)&priv->map->PHY[0], buf);
		if (rtl818x_ioread8(priv, &priv->map->PHY[2]) == (data & 0xFF))
			return;
	}
}

static void rtl8180_handle_rx(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	unsigned int count = 32;
	u8 signal, agc, sq;

	while (count--) {
		struct rtl8180_rx_desc *entry = &priv->rx_ring[priv->rx_idx];
		struct sk_buff *skb = priv->rx_buf[priv->rx_idx];
		u32 flags = le32_to_cpu(entry->flags);

		if (flags & RTL818X_RX_DESC_FLAG_OWN)
			return;

		if (unlikely(flags & (RTL818X_RX_DESC_FLAG_DMA_FAIL |
				      RTL818X_RX_DESC_FLAG_FOF |
				      RTL818X_RX_DESC_FLAG_RX_ERR)))
			goto done;
		else {
			u32 flags2 = le32_to_cpu(entry->flags2);
			struct ieee80211_rx_status rx_status = {0};
			struct sk_buff *new_skb = dev_alloc_skb(MAX_RX_SIZE);

			if (unlikely(!new_skb))
				goto done;

			pci_unmap_single(priv->pdev,
					 *((dma_addr_t *)skb->cb),
					 MAX_RX_SIZE, PCI_DMA_FROMDEVICE);
			skb_put(skb, flags & 0xFFF);

			rx_status.antenna = (flags2 >> 15) & 1;
			rx_status.rate_idx = (flags >> 20) & 0xF;
			agc = (flags2 >> 17) & 0x7F;
			if (priv->r8185) {
				if (rx_status.rate_idx > 3)
					signal = 90 - clamp_t(u8, agc, 25, 90);
				else
					signal = 95 - clamp_t(u8, agc, 30, 95);
			} else {
				sq = flags2 & 0xff;
				signal = priv->rf->calc_rssi(agc, sq);
			}
			rx_status.signal = signal;
			rx_status.freq = dev->conf.chandef.chan->center_freq;
			rx_status.band = dev->conf.chandef.chan->band;
			rx_status.mactime = le64_to_cpu(entry->tsft);
			rx_status.flag |= RX_FLAG_MACTIME_START;
			if (flags & RTL818X_RX_DESC_FLAG_CRC32_ERR)
				rx_status.flag |= RX_FLAG_FAILED_FCS_CRC;

			memcpy(IEEE80211_SKB_RXCB(skb), &rx_status, sizeof(rx_status));
			ieee80211_rx_irqsafe(dev, skb);

			skb = new_skb;
			priv->rx_buf[priv->rx_idx] = skb;
			*((dma_addr_t *) skb->cb) =
				pci_map_single(priv->pdev, skb_tail_pointer(skb),
					       MAX_RX_SIZE, PCI_DMA_FROMDEVICE);
		}

	done:
		entry->rx_buf = cpu_to_le32(*((dma_addr_t *)skb->cb));
		entry->flags = cpu_to_le32(RTL818X_RX_DESC_FLAG_OWN |
					   MAX_RX_SIZE);
		if (priv->rx_idx == 31)
			entry->flags |= cpu_to_le32(RTL818X_RX_DESC_FLAG_EOR);
		priv->rx_idx = (priv->rx_idx + 1) % 32;
	}
}

static void rtl8180_handle_tx(struct ieee80211_hw *dev, unsigned int prio)
{
	struct rtl8180_priv *priv = dev->priv;
	struct rtl8180_tx_ring *ring = &priv->tx_ring[prio];

	while (skb_queue_len(&ring->queue)) {
		struct rtl8180_tx_desc *entry = &ring->desc[ring->idx];
		struct sk_buff *skb;
		struct ieee80211_tx_info *info;
		u32 flags = le32_to_cpu(entry->flags);

		if (flags & RTL818X_TX_DESC_FLAG_OWN)
			return;

		ring->idx = (ring->idx + 1) % ring->entries;
		skb = __skb_dequeue(&ring->queue);
		pci_unmap_single(priv->pdev, le32_to_cpu(entry->tx_buf),
				 skb->len, PCI_DMA_TODEVICE);

		info = IEEE80211_SKB_CB(skb);
		ieee80211_tx_info_clear_status(info);

		if (!(info->flags & IEEE80211_TX_CTL_NO_ACK) &&
		    (flags & RTL818X_TX_DESC_FLAG_TX_OK))
			info->flags |= IEEE80211_TX_STAT_ACK;

		info->status.rates[0].count = (flags & 0xFF) + 1;
		info->status.rates[1].idx = -1;

		ieee80211_tx_status_irqsafe(dev, skb);
		if (ring->entries - skb_queue_len(&ring->queue) == 2)
			ieee80211_wake_queue(dev, prio);
	}
}

static irqreturn_t rtl8180_interrupt(int irq, void *dev_id)
{
	struct ieee80211_hw *dev = dev_id;
	struct rtl8180_priv *priv = dev->priv;
	u16 reg;

	spin_lock(&priv->lock);
	reg = rtl818x_ioread16(priv, &priv->map->INT_STATUS);
	if (unlikely(reg == 0xFFFF)) {
		spin_unlock(&priv->lock);
		return IRQ_HANDLED;
	}

	rtl818x_iowrite16(priv, &priv->map->INT_STATUS, reg);

	if (reg & (RTL818X_INT_TXB_OK | RTL818X_INT_TXB_ERR))
		rtl8180_handle_tx(dev, 3);

	if (reg & (RTL818X_INT_TXH_OK | RTL818X_INT_TXH_ERR))
		rtl8180_handle_tx(dev, 2);

	if (reg & (RTL818X_INT_TXN_OK | RTL818X_INT_TXN_ERR))
		rtl8180_handle_tx(dev, 1);

	if (reg & (RTL818X_INT_TXL_OK | RTL818X_INT_TXL_ERR))
		rtl8180_handle_tx(dev, 0);

	if (reg & (RTL818X_INT_RX_OK | RTL818X_INT_RX_ERR))
		rtl8180_handle_rx(dev);

	spin_unlock(&priv->lock);

	return IRQ_HANDLED;
}

static void rtl8180_tx(struct ieee80211_hw *dev,
		       struct ieee80211_tx_control *control,
		       struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct rtl8180_priv *priv = dev->priv;
	struct rtl8180_tx_ring *ring;
	struct rtl8180_tx_desc *entry;
	unsigned long flags;
	unsigned int idx, prio;
	dma_addr_t mapping;
	u32 tx_flags;
	u8 rc_flags;
	u16 plcp_len = 0;
	__le16 rts_duration = 0;

	prio = skb_get_queue_mapping(skb);
	ring = &priv->tx_ring[prio];

	mapping = pci_map_single(priv->pdev, skb->data,
				 skb->len, PCI_DMA_TODEVICE);

	tx_flags = RTL818X_TX_DESC_FLAG_OWN | RTL818X_TX_DESC_FLAG_FS |
		   RTL818X_TX_DESC_FLAG_LS |
		   (ieee80211_get_tx_rate(dev, info)->hw_value << 24) |
		   skb->len;

	if (priv->r8185)
		tx_flags |= RTL818X_TX_DESC_FLAG_DMA |
			    RTL818X_TX_DESC_FLAG_NO_ENC;

	rc_flags = info->control.rates[0].flags;
	if (rc_flags & IEEE80211_TX_RC_USE_RTS_CTS) {
		tx_flags |= RTL818X_TX_DESC_FLAG_RTS;
		tx_flags |= ieee80211_get_rts_cts_rate(dev, info)->hw_value << 19;
	} else if (rc_flags & IEEE80211_TX_RC_USE_CTS_PROTECT) {
		tx_flags |= RTL818X_TX_DESC_FLAG_CTS;
		tx_flags |= ieee80211_get_rts_cts_rate(dev, info)->hw_value << 19;
	}

	if (rc_flags & IEEE80211_TX_RC_USE_RTS_CTS)
		rts_duration = ieee80211_rts_duration(dev, priv->vif, skb->len,
						      info);

	if (!priv->r8185) {
		unsigned int remainder;

		plcp_len = DIV_ROUND_UP(16 * (skb->len + 4),
				(ieee80211_get_tx_rate(dev, info)->bitrate * 2) / 10);
		remainder = (16 * (skb->len + 4)) %
			    ((ieee80211_get_tx_rate(dev, info)->bitrate * 2) / 10);
		if (remainder <= 6)
			plcp_len |= 1 << 15;
	}

	spin_lock_irqsave(&priv->lock, flags);

	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		if (info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT)
			priv->seqno += 0x10;
		hdr->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
		hdr->seq_ctrl |= cpu_to_le16(priv->seqno);
	}

	idx = (ring->idx + skb_queue_len(&ring->queue)) % ring->entries;
	entry = &ring->desc[idx];

	entry->rts_duration = rts_duration;
	entry->plcp_len = cpu_to_le16(plcp_len);
	entry->tx_buf = cpu_to_le32(mapping);
	entry->frame_len = cpu_to_le32(skb->len);
	entry->flags2 = info->control.rates[1].idx >= 0 ?
		ieee80211_get_alt_retry_rate(dev, info, 0)->bitrate << 4 : 0;
	entry->retry_limit = info->control.rates[0].count;
	entry->flags = cpu_to_le32(tx_flags);
	__skb_queue_tail(&ring->queue, skb);
	if (ring->entries - skb_queue_len(&ring->queue) < 2)
		ieee80211_stop_queue(dev, prio);

	spin_unlock_irqrestore(&priv->lock, flags);

	rtl818x_iowrite8(priv, &priv->map->TX_DMA_POLLING, (1 << (prio + 4)));
}

void rtl8180_set_anaparam(struct rtl8180_priv *priv, u32 anaparam)
{
	u8 reg;

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
	reg = rtl818x_ioread8(priv, &priv->map->CONFIG3);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3,
		 reg | RTL818X_CONFIG3_ANAPARAM_WRITE);
	rtl818x_iowrite32(priv, &priv->map->ANAPARAM, anaparam);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3,
		 reg & ~RTL818X_CONFIG3_ANAPARAM_WRITE);
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);
}

static int rtl8180_init_hw(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	u16 reg;

	rtl818x_iowrite8(priv, &priv->map->CMD, 0);
	rtl818x_ioread8(priv, &priv->map->CMD);
	msleep(10);

	/* reset */
	rtl818x_iowrite16(priv, &priv->map->INT_MASK, 0);
	rtl818x_ioread8(priv, &priv->map->CMD);

	reg = rtl818x_ioread8(priv, &priv->map->CMD);
	reg &= (1 << 1);
	reg |= RTL818X_CMD_RESET;
	rtl818x_iowrite8(priv, &priv->map->CMD, RTL818X_CMD_RESET);
	rtl818x_ioread8(priv, &priv->map->CMD);
	msleep(200);

	/* check success of reset */
	if (rtl818x_ioread8(priv, &priv->map->CMD) & RTL818X_CMD_RESET) {
		wiphy_err(dev->wiphy, "reset timeout!\n");
		return -ETIMEDOUT;
	}

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_LOAD);
	rtl818x_ioread8(priv, &priv->map->CMD);
	msleep(200);

	if (rtl818x_ioread8(priv, &priv->map->CONFIG3) & (1 << 3)) {
		/* For cardbus */
		reg = rtl818x_ioread8(priv, &priv->map->CONFIG3);
		reg |= 1 << 1;
		rtl818x_iowrite8(priv, &priv->map->CONFIG3, reg);
		reg = rtl818x_ioread16(priv, &priv->map->FEMR);
		reg |= (1 << 15) | (1 << 14) | (1 << 4);
		rtl818x_iowrite16(priv, &priv->map->FEMR, reg);
	}

	rtl818x_iowrite8(priv, &priv->map->MSR, 0);

	if (!priv->r8185)
		rtl8180_set_anaparam(priv, priv->anaparam);

	rtl818x_iowrite32(priv, &priv->map->RDSAR, priv->rx_ring_dma);
	rtl818x_iowrite32(priv, &priv->map->TBDA, priv->tx_ring[3].dma);
	rtl818x_iowrite32(priv, &priv->map->THPDA, priv->tx_ring[2].dma);
	rtl818x_iowrite32(priv, &priv->map->TNPDA, priv->tx_ring[1].dma);
	rtl818x_iowrite32(priv, &priv->map->TLPDA, priv->tx_ring[0].dma);

	/* TODO: necessary? specs indicate not */
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
	reg = rtl818x_ioread8(priv, &priv->map->CONFIG2);
	rtl818x_iowrite8(priv, &priv->map->CONFIG2, reg & ~(1 << 3));
	if (priv->r8185) {
		reg = rtl818x_ioread8(priv, &priv->map->CONFIG2);
		rtl818x_iowrite8(priv, &priv->map->CONFIG2, reg | (1 << 4));
	}
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);

	/* TODO: set CONFIG5 for calibrating AGC on rtl8180 + philips radio? */

	/* TODO: turn off hw wep on rtl8180 */

	rtl818x_iowrite32(priv, &priv->map->INT_TIMEOUT, 0);

	if (priv->r8185) {
		rtl818x_iowrite8(priv, &priv->map->WPA_CONF, 0);
		rtl818x_iowrite8(priv, &priv->map->RATE_FALLBACK, 0x81);
		rtl818x_iowrite8(priv, &priv->map->RESP_RATE, (8 << 4) | 0);

		rtl818x_iowrite16(priv, &priv->map->BRSR, 0x01F3);

		/* TODO: set ClkRun enable? necessary? */
		reg = rtl818x_ioread8(priv, &priv->map->GP_ENABLE);
		rtl818x_iowrite8(priv, &priv->map->GP_ENABLE, reg & ~(1 << 6));
		rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
		reg = rtl818x_ioread8(priv, &priv->map->CONFIG3);
		rtl818x_iowrite8(priv, &priv->map->CONFIG3, reg | (1 << 2));
		rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);
	} else {
		rtl818x_iowrite16(priv, &priv->map->BRSR, 0x1);
		rtl818x_iowrite8(priv, &priv->map->SECURITY, 0);

		rtl818x_iowrite8(priv, &priv->map->PHY_DELAY, 0x6);
		rtl818x_iowrite8(priv, &priv->map->CARRIER_SENSE_COUNTER, 0x4C);
	}

	priv->rf->init(dev);
	if (priv->r8185)
		rtl818x_iowrite16(priv, &priv->map->BRSR, 0x01F3);
	return 0;
}

static int rtl8180_init_rx_ring(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	struct rtl8180_rx_desc *entry;
	int i;

	priv->rx_ring = pci_alloc_consistent(priv->pdev,
					     sizeof(*priv->rx_ring) * 32,
					     &priv->rx_ring_dma);

	if (!priv->rx_ring || (unsigned long)priv->rx_ring & 0xFF) {
		wiphy_err(dev->wiphy, "Cannot allocate RX ring\n");
		return -ENOMEM;
	}

	memset(priv->rx_ring, 0, sizeof(*priv->rx_ring) * 32);
	priv->rx_idx = 0;

	for (i = 0; i < 32; i++) {
		struct sk_buff *skb = dev_alloc_skb(MAX_RX_SIZE);
		dma_addr_t *mapping;
		entry = &priv->rx_ring[i];
		if (!skb)
			return 0;

		priv->rx_buf[i] = skb;
		mapping = (dma_addr_t *)skb->cb;
		*mapping = pci_map_single(priv->pdev, skb_tail_pointer(skb),
					  MAX_RX_SIZE, PCI_DMA_FROMDEVICE);
		entry->rx_buf = cpu_to_le32(*mapping);
		entry->flags = cpu_to_le32(RTL818X_RX_DESC_FLAG_OWN |
					   MAX_RX_SIZE);
	}
	entry->flags |= cpu_to_le32(RTL818X_RX_DESC_FLAG_EOR);
	return 0;
}

static void rtl8180_free_rx_ring(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	int i;

	for (i = 0; i < 32; i++) {
		struct sk_buff *skb = priv->rx_buf[i];
		if (!skb)
			continue;

		pci_unmap_single(priv->pdev,
				 *((dma_addr_t *)skb->cb),
				 MAX_RX_SIZE, PCI_DMA_FROMDEVICE);
		kfree_skb(skb);
	}

	pci_free_consistent(priv->pdev, sizeof(*priv->rx_ring) * 32,
			    priv->rx_ring, priv->rx_ring_dma);
	priv->rx_ring = NULL;
}

static int rtl8180_init_tx_ring(struct ieee80211_hw *dev,
				unsigned int prio, unsigned int entries)
{
	struct rtl8180_priv *priv = dev->priv;
	struct rtl8180_tx_desc *ring;
	dma_addr_t dma;
	int i;

	ring = pci_alloc_consistent(priv->pdev, sizeof(*ring) * entries, &dma);
	if (!ring || (unsigned long)ring & 0xFF) {
		wiphy_err(dev->wiphy, "Cannot allocate TX ring (prio = %d)\n",
			  prio);
		return -ENOMEM;
	}

	memset(ring, 0, sizeof(*ring)*entries);
	priv->tx_ring[prio].desc = ring;
	priv->tx_ring[prio].dma = dma;
	priv->tx_ring[prio].idx = 0;
	priv->tx_ring[prio].entries = entries;
	skb_queue_head_init(&priv->tx_ring[prio].queue);

	for (i = 0; i < entries; i++)
		ring[i].next_tx_desc =
			cpu_to_le32((u32)dma + ((i + 1) % entries) * sizeof(*ring));

	return 0;
}

static void rtl8180_free_tx_ring(struct ieee80211_hw *dev, unsigned int prio)
{
	struct rtl8180_priv *priv = dev->priv;
	struct rtl8180_tx_ring *ring = &priv->tx_ring[prio];

	while (skb_queue_len(&ring->queue)) {
		struct rtl8180_tx_desc *entry = &ring->desc[ring->idx];
		struct sk_buff *skb = __skb_dequeue(&ring->queue);

		pci_unmap_single(priv->pdev, le32_to_cpu(entry->tx_buf),
				 skb->len, PCI_DMA_TODEVICE);
		kfree_skb(skb);
		ring->idx = (ring->idx + 1) % ring->entries;
	}

	pci_free_consistent(priv->pdev, sizeof(*ring->desc)*ring->entries,
			    ring->desc, ring->dma);
	ring->desc = NULL;
}

static int rtl8180_start(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	int ret, i;
	u32 reg;

	ret = rtl8180_init_rx_ring(dev);
	if (ret)
		return ret;

	for (i = 0; i < 4; i++)
		if ((ret = rtl8180_init_tx_ring(dev, i, 16)))
			goto err_free_rings;

	ret = rtl8180_init_hw(dev);
	if (ret)
		goto err_free_rings;

	rtl818x_iowrite32(priv, &priv->map->RDSAR, priv->rx_ring_dma);
	rtl818x_iowrite32(priv, &priv->map->TBDA, priv->tx_ring[3].dma);
	rtl818x_iowrite32(priv, &priv->map->THPDA, priv->tx_ring[2].dma);
	rtl818x_iowrite32(priv, &priv->map->TNPDA, priv->tx_ring[1].dma);
	rtl818x_iowrite32(priv, &priv->map->TLPDA, priv->tx_ring[0].dma);

	ret = request_irq(priv->pdev->irq, rtl8180_interrupt,
			  IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret) {
		wiphy_err(dev->wiphy, "failed to register IRQ handler\n");
		goto err_free_rings;
	}

	rtl818x_iowrite16(priv, &priv->map->INT_MASK, 0xFFFF);

	rtl818x_iowrite32(priv, &priv->map->MAR[0], ~0);
	rtl818x_iowrite32(priv, &priv->map->MAR[1], ~0);

	reg = RTL818X_RX_CONF_ONLYERLPKT |
	      RTL818X_RX_CONF_RX_AUTORESETPHY |
	      RTL818X_RX_CONF_MGMT |
	      RTL818X_RX_CONF_DATA |
	      (7 << 8 /* MAX RX DMA */) |
	      RTL818X_RX_CONF_BROADCAST |
	      RTL818X_RX_CONF_NICMAC;

	if (priv->r8185)
		reg |= RTL818X_RX_CONF_CSDM1 | RTL818X_RX_CONF_CSDM2;
	else {
		reg |= (priv->rfparam & RF_PARAM_CARRIERSENSE1)
			? RTL818X_RX_CONF_CSDM1 : 0;
		reg |= (priv->rfparam & RF_PARAM_CARRIERSENSE2)
			? RTL818X_RX_CONF_CSDM2 : 0;
	}

	priv->rx_conf = reg;
	rtl818x_iowrite32(priv, &priv->map->RX_CONF, reg);

	if (priv->r8185) {
		reg = rtl818x_ioread8(priv, &priv->map->CW_CONF);
		reg &= ~RTL818X_CW_CONF_PERPACKET_CW_SHIFT;
		reg |= RTL818X_CW_CONF_PERPACKET_RETRY_SHIFT;
		rtl818x_iowrite8(priv, &priv->map->CW_CONF, reg);

		reg = rtl818x_ioread8(priv, &priv->map->TX_AGC_CTL);
		reg &= ~RTL818X_TX_AGC_CTL_PERPACKET_GAIN_SHIFT;
		reg &= ~RTL818X_TX_AGC_CTL_PERPACKET_ANTSEL_SHIFT;
		reg |=  RTL818X_TX_AGC_CTL_FEEDBACK_ANT;
		rtl818x_iowrite8(priv, &priv->map->TX_AGC_CTL, reg);

		/* disable early TX */
		rtl818x_iowrite8(priv, (u8 __iomem *)priv->map + 0xec, 0x3f);
	}

	reg = rtl818x_ioread32(priv, &priv->map->TX_CONF);
	reg |= (6 << 21 /* MAX TX DMA */) |
	       RTL818X_TX_CONF_NO_ICV;

	if (priv->r8185)
		reg &= ~RTL818X_TX_CONF_PROBE_DTS;
	else
		reg &= ~RTL818X_TX_CONF_HW_SEQNUM;

	/* different meaning, same value on both rtl8185 and rtl8180 */
	reg &= ~RTL818X_TX_CONF_SAT_HWPLCP;

	rtl818x_iowrite32(priv, &priv->map->TX_CONF, reg);

	reg = rtl818x_ioread8(priv, &priv->map->CMD);
	reg |= RTL818X_CMD_RX_ENABLE;
	reg |= RTL818X_CMD_TX_ENABLE;
	rtl818x_iowrite8(priv, &priv->map->CMD, reg);

	return 0;

 err_free_rings:
	rtl8180_free_rx_ring(dev);
	for (i = 0; i < 4; i++)
		if (priv->tx_ring[i].desc)
			rtl8180_free_tx_ring(dev, i);

	return ret;
}

static void rtl8180_stop(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	u8 reg;
	int i;

	rtl818x_iowrite16(priv, &priv->map->INT_MASK, 0);

	reg = rtl818x_ioread8(priv, &priv->map->CMD);
	reg &= ~RTL818X_CMD_TX_ENABLE;
	reg &= ~RTL818X_CMD_RX_ENABLE;
	rtl818x_iowrite8(priv, &priv->map->CMD, reg);

	priv->rf->stop(dev);

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
	reg = rtl818x_ioread8(priv, &priv->map->CONFIG4);
	rtl818x_iowrite8(priv, &priv->map->CONFIG4, reg | RTL818X_CONFIG4_VCOOFF);
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);

	free_irq(priv->pdev->irq, dev);

	rtl8180_free_rx_ring(dev);
	for (i = 0; i < 4; i++)
		rtl8180_free_tx_ring(dev, i);
}

static u64 rtl8180_get_tsf(struct ieee80211_hw *dev,
			   struct ieee80211_vif *vif)
{
	struct rtl8180_priv *priv = dev->priv;

	return rtl818x_ioread32(priv, &priv->map->TSFT[0]) |
	       (u64)(rtl818x_ioread32(priv, &priv->map->TSFT[1])) << 32;
}

static void rtl8180_beacon_work(struct work_struct *work)
{
	struct rtl8180_vif *vif_priv =
		container_of(work, struct rtl8180_vif, beacon_work.work);
	struct ieee80211_vif *vif =
		container_of((void *)vif_priv, struct ieee80211_vif, drv_priv);
	struct ieee80211_hw *dev = vif_priv->dev;
	struct ieee80211_mgmt *mgmt;
	struct sk_buff *skb;

	/* don't overflow the tx ring */
	if (ieee80211_queue_stopped(dev, 0))
		goto resched;

	/* grab a fresh beacon */
	skb = ieee80211_beacon_get(dev, vif);
	if (!skb)
		goto resched;

	/*
	 * update beacon timestamp w/ TSF value
	 * TODO: make hardware update beacon timestamp
	 */
	mgmt = (struct ieee80211_mgmt *)skb->data;
	mgmt->u.beacon.timestamp = cpu_to_le64(rtl8180_get_tsf(dev, vif));

	/* TODO: use actual beacon queue */
	skb_set_queue_mapping(skb, 0);

	rtl8180_tx(dev, NULL, skb);

resched:
	/*
	 * schedule next beacon
	 * TODO: use hardware support for beacon timing
	 */
	schedule_delayed_work(&vif_priv->beacon_work,
			usecs_to_jiffies(1024 * vif->bss_conf.beacon_int));
}

static int rtl8180_add_interface(struct ieee80211_hw *dev,
				 struct ieee80211_vif *vif)
{
	struct rtl8180_priv *priv = dev->priv;
	struct rtl8180_vif *vif_priv;

	/*
	 * We only support one active interface at a time.
	 */
	if (priv->vif)
		return -EBUSY;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
		break;
	default:
		return -EOPNOTSUPP;
	}

	priv->vif = vif;

	/* Initialize driver private area */
	vif_priv = (struct rtl8180_vif *)&vif->drv_priv;
	vif_priv->dev = dev;
	INIT_DELAYED_WORK(&vif_priv->beacon_work, rtl8180_beacon_work);
	vif_priv->enable_beacon = false;

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
	rtl818x_iowrite32(priv, (__le32 __iomem *)&priv->map->MAC[0],
			  le32_to_cpu(*(__le32 *)vif->addr));
	rtl818x_iowrite16(priv, (__le16 __iomem *)&priv->map->MAC[4],
			  le16_to_cpu(*(__le16 *)(vif->addr + 4)));
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);

	return 0;
}

static void rtl8180_remove_interface(struct ieee80211_hw *dev,
				     struct ieee80211_vif *vif)
{
	struct rtl8180_priv *priv = dev->priv;
	priv->vif = NULL;
}

static int rtl8180_config(struct ieee80211_hw *dev, u32 changed)
{
	struct rtl8180_priv *priv = dev->priv;
	struct ieee80211_conf *conf = &dev->conf;

	priv->rf->set_chan(dev, conf);

	return 0;
}

static void rtl8180_bss_info_changed(struct ieee80211_hw *dev,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *info,
				     u32 changed)
{
	struct rtl8180_priv *priv = dev->priv;
	struct rtl8180_vif *vif_priv;
	int i;
	u8 reg;

	vif_priv = (struct rtl8180_vif *)&vif->drv_priv;

	if (changed & BSS_CHANGED_BSSID) {
		for (i = 0; i < ETH_ALEN; i++)
			rtl818x_iowrite8(priv, &priv->map->BSSID[i],
					 info->bssid[i]);

		if (is_valid_ether_addr(info->bssid)) {
			if (vif->type == NL80211_IFTYPE_ADHOC)
				reg = RTL818X_MSR_ADHOC;
			else
				reg = RTL818X_MSR_INFRA;
		} else
			reg = RTL818X_MSR_NO_LINK;
		rtl818x_iowrite8(priv, &priv->map->MSR, reg);
	}

	if (changed & BSS_CHANGED_ERP_SLOT && priv->rf->conf_erp)
		priv->rf->conf_erp(dev, info);

	if (changed & BSS_CHANGED_BEACON_ENABLED)
		vif_priv->enable_beacon = info->enable_beacon;

	if (changed & (BSS_CHANGED_BEACON_ENABLED | BSS_CHANGED_BEACON)) {
		cancel_delayed_work_sync(&vif_priv->beacon_work);
		if (vif_priv->enable_beacon)
			schedule_work(&vif_priv->beacon_work.work);
	}
}

static u64 rtl8180_prepare_multicast(struct ieee80211_hw *dev,
				     struct netdev_hw_addr_list *mc_list)
{
	return netdev_hw_addr_list_count(mc_list);
}

static void rtl8180_configure_filter(struct ieee80211_hw *dev,
				     unsigned int changed_flags,
				     unsigned int *total_flags,
				     u64 multicast)
{
	struct rtl8180_priv *priv = dev->priv;

	if (changed_flags & FIF_FCSFAIL)
		priv->rx_conf ^= RTL818X_RX_CONF_FCS;
	if (changed_flags & FIF_CONTROL)
		priv->rx_conf ^= RTL818X_RX_CONF_CTRL;
	if (changed_flags & FIF_OTHER_BSS)
		priv->rx_conf ^= RTL818X_RX_CONF_MONITOR;
	if (*total_flags & FIF_ALLMULTI || multicast > 0)
		priv->rx_conf |= RTL818X_RX_CONF_MULTICAST;
	else
		priv->rx_conf &= ~RTL818X_RX_CONF_MULTICAST;

	*total_flags = 0;

	if (priv->rx_conf & RTL818X_RX_CONF_FCS)
		*total_flags |= FIF_FCSFAIL;
	if (priv->rx_conf & RTL818X_RX_CONF_CTRL)
		*total_flags |= FIF_CONTROL;
	if (priv->rx_conf & RTL818X_RX_CONF_MONITOR)
		*total_flags |= FIF_OTHER_BSS;
	if (priv->rx_conf & RTL818X_RX_CONF_MULTICAST)
		*total_flags |= FIF_ALLMULTI;

	rtl818x_iowrite32(priv, &priv->map->RX_CONF, priv->rx_conf);
}

static const struct ieee80211_ops rtl8180_ops = {
	.tx			= rtl8180_tx,
	.start			= rtl8180_start,
	.stop			= rtl8180_stop,
	.add_interface		= rtl8180_add_interface,
	.remove_interface	= rtl8180_remove_interface,
	.config			= rtl8180_config,
	.bss_info_changed	= rtl8180_bss_info_changed,
	.prepare_multicast	= rtl8180_prepare_multicast,
	.configure_filter	= rtl8180_configure_filter,
	.get_tsf		= rtl8180_get_tsf,
};

static void rtl8180_eeprom_register_read(struct eeprom_93cx6 *eeprom)
{
	struct ieee80211_hw *dev = eeprom->data;
	struct rtl8180_priv *priv = dev->priv;
	u8 reg = rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);

	eeprom->reg_data_in = reg & RTL818X_EEPROM_CMD_WRITE;
	eeprom->reg_data_out = reg & RTL818X_EEPROM_CMD_READ;
	eeprom->reg_data_clock = reg & RTL818X_EEPROM_CMD_CK;
	eeprom->reg_chip_select = reg & RTL818X_EEPROM_CMD_CS;
}

static void rtl8180_eeprom_register_write(struct eeprom_93cx6 *eeprom)
{
	struct ieee80211_hw *dev = eeprom->data;
	struct rtl8180_priv *priv = dev->priv;
	u8 reg = 2 << 6;

	if (eeprom->reg_data_in)
		reg |= RTL818X_EEPROM_CMD_WRITE;
	if (eeprom->reg_data_out)
		reg |= RTL818X_EEPROM_CMD_READ;
	if (eeprom->reg_data_clock)
		reg |= RTL818X_EEPROM_CMD_CK;
	if (eeprom->reg_chip_select)
		reg |= RTL818X_EEPROM_CMD_CS;

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, reg);
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	udelay(10);
}

static int rtl8180_probe(struct pci_dev *pdev,
				   const struct pci_device_id *id)
{
	struct ieee80211_hw *dev;
	struct rtl8180_priv *priv;
	unsigned long mem_addr, mem_len;
	unsigned int io_addr, io_len;
	int err, i;
	struct eeprom_93cx6 eeprom;
	const char *chip_name, *rf_name = NULL;
	u32 reg;
	u16 eeprom_val;
	u8 mac_addr[ETH_ALEN];

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR "%s (rtl8180): Cannot enable new PCI device\n",
		       pci_name(pdev));
		return err;
	}

	err = pci_request_regions(pdev, KBUILD_MODNAME);
	if (err) {
		printk(KERN_ERR "%s (rtl8180): Cannot obtain PCI resources\n",
		       pci_name(pdev));
		return err;
	}

	io_addr = pci_resource_start(pdev, 0);
	io_len = pci_resource_len(pdev, 0);
	mem_addr = pci_resource_start(pdev, 1);
	mem_len = pci_resource_len(pdev, 1);

	if (mem_len < sizeof(struct rtl818x_csr) ||
	    io_len < sizeof(struct rtl818x_csr)) {
		printk(KERN_ERR "%s (rtl8180): Too short PCI resources\n",
		       pci_name(pdev));
		err = -ENOMEM;
		goto err_free_reg;
	}

	if ((err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) ||
	    (err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32)))) {
		printk(KERN_ERR "%s (rtl8180): No suitable DMA available\n",
		       pci_name(pdev));
		goto err_free_reg;
	}

	pci_set_master(pdev);

	dev = ieee80211_alloc_hw(sizeof(*priv), &rtl8180_ops);
	if (!dev) {
		printk(KERN_ERR "%s (rtl8180): ieee80211 alloc failed\n",
		       pci_name(pdev));
		err = -ENOMEM;
		goto err_free_reg;
	}

	priv = dev->priv;
	priv->pdev = pdev;

	dev->max_rates = 2;
	SET_IEEE80211_DEV(dev, &pdev->dev);
	pci_set_drvdata(pdev, dev);

	priv->map = pci_iomap(pdev, 1, mem_len);
	if (!priv->map)
		priv->map = pci_iomap(pdev, 0, io_len);

	if (!priv->map) {
		printk(KERN_ERR "%s (rtl8180): Cannot map device memory\n",
		       pci_name(pdev));
		goto err_free_dev;
	}

	BUILD_BUG_ON(sizeof(priv->channels) != sizeof(rtl818x_channels));
	BUILD_BUG_ON(sizeof(priv->rates) != sizeof(rtl818x_rates));

	memcpy(priv->channels, rtl818x_channels, sizeof(rtl818x_channels));
	memcpy(priv->rates, rtl818x_rates, sizeof(rtl818x_rates));

	priv->band.band = IEEE80211_BAND_2GHZ;
	priv->band.channels = priv->channels;
	priv->band.n_channels = ARRAY_SIZE(rtl818x_channels);
	priv->band.bitrates = priv->rates;
	priv->band.n_bitrates = 4;
	dev->wiphy->bands[IEEE80211_BAND_2GHZ] = &priv->band;

	dev->flags = IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING |
		     IEEE80211_HW_RX_INCLUDES_FCS |
		     IEEE80211_HW_SIGNAL_UNSPEC;
	dev->vif_data_size = sizeof(struct rtl8180_vif);
	dev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_ADHOC);
	dev->queues = 1;
	dev->max_signal = 65;

	reg = rtl818x_ioread32(priv, &priv->map->TX_CONF);
	reg &= RTL818X_TX_CONF_HWVER_MASK;
	switch (reg) {
	case RTL818X_TX_CONF_R8180_ABCD:
		chip_name = "RTL8180";
		break;
	case RTL818X_TX_CONF_R8180_F:
		chip_name = "RTL8180vF";
		break;
	case RTL818X_TX_CONF_R8185_ABC:
		chip_name = "RTL8185";
		break;
	case RTL818X_TX_CONF_R8185_D:
		chip_name = "RTL8185vD";
		break;
	default:
		printk(KERN_ERR "%s (rtl8180): Unknown chip! (0x%x)\n",
		       pci_name(pdev), reg >> 25);
		goto err_iounmap;
	}

	priv->r8185 = reg & RTL818X_TX_CONF_R8185_ABC;
	if (priv->r8185) {
		priv->band.n_bitrates = ARRAY_SIZE(rtl818x_rates);
		pci_try_set_mwi(pdev);
	}

	eeprom.data = dev;
	eeprom.register_read = rtl8180_eeprom_register_read;
	eeprom.register_write = rtl8180_eeprom_register_write;
	if (rtl818x_ioread32(priv, &priv->map->RX_CONF) & (1 << 6))
		eeprom.width = PCI_EEPROM_WIDTH_93C66;
	else
		eeprom.width = PCI_EEPROM_WIDTH_93C46;

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_PROGRAM);
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	udelay(10);

	eeprom_93cx6_read(&eeprom, 0x06, &eeprom_val);
	eeprom_val &= 0xFF;
	switch (eeprom_val) {
	case 1:	rf_name = "Intersil";
		break;
	case 2:	rf_name = "RFMD";
		break;
	case 3:	priv->rf = &sa2400_rf_ops;
		break;
	case 4:	priv->rf = &max2820_rf_ops;
		break;
	case 5:	priv->rf = &grf5101_rf_ops;
		break;
	case 9:	priv->rf = rtl8180_detect_rf(dev);
		break;
	case 10:
		rf_name = "RTL8255";
		break;
	default:
		printk(KERN_ERR "%s (rtl8180): Unknown RF! (0x%x)\n",
		       pci_name(pdev), eeprom_val);
		goto err_iounmap;
	}

	if (!priv->rf) {
		printk(KERN_ERR "%s (rtl8180): %s RF frontend not supported!\n",
		       pci_name(pdev), rf_name);
		goto err_iounmap;
	}

	eeprom_93cx6_read(&eeprom, 0x17, &eeprom_val);
	priv->csthreshold = eeprom_val >> 8;
	if (!priv->r8185) {
		__le32 anaparam;
		eeprom_93cx6_multiread(&eeprom, 0xD, (__le16 *)&anaparam, 2);
		priv->anaparam = le32_to_cpu(anaparam);
		eeprom_93cx6_read(&eeprom, 0x19, &priv->rfparam);
	}

	eeprom_93cx6_multiread(&eeprom, 0x7, (__le16 *)mac_addr, 3);
	if (!is_valid_ether_addr(mac_addr)) {
		printk(KERN_WARNING "%s (rtl8180): Invalid hwaddr! Using"
		       " randomly generated MAC addr\n", pci_name(pdev));
		eth_random_addr(mac_addr);
	}
	SET_IEEE80211_PERM_ADDR(dev, mac_addr);

	/* CCK TX power */
	for (i = 0; i < 14; i += 2) {
		u16 txpwr;
		eeprom_93cx6_read(&eeprom, 0x10 + (i >> 1), &txpwr);
		priv->channels[i].hw_value = txpwr & 0xFF;
		priv->channels[i + 1].hw_value = txpwr >> 8;
	}

	/* OFDM TX power */
	if (priv->r8185) {
		for (i = 0; i < 14; i += 2) {
			u16 txpwr;
			eeprom_93cx6_read(&eeprom, 0x20 + (i >> 1), &txpwr);
			priv->channels[i].hw_value |= (txpwr & 0xFF) << 8;
			priv->channels[i + 1].hw_value |= txpwr & 0xFF00;
		}
	}

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);

	spin_lock_init(&priv->lock);

	err = ieee80211_register_hw(dev);
	if (err) {
		printk(KERN_ERR "%s (rtl8180): Cannot register device\n",
		       pci_name(pdev));
		goto err_iounmap;
	}

	wiphy_info(dev->wiphy, "hwaddr %pm, %s + %s\n",
		   mac_addr, chip_name, priv->rf->name);

	return 0;

 err_iounmap:
	iounmap(priv->map);

 err_free_dev:
	ieee80211_free_hw(dev);

 err_free_reg:
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	return err;
}

static void rtl8180_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *dev = pci_get_drvdata(pdev);
	struct rtl8180_priv *priv;

	if (!dev)
		return;

	ieee80211_unregister_hw(dev);

	priv = dev->priv;

	pci_iounmap(pdev, priv->map);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	ieee80211_free_hw(dev);
}

#ifdef CONFIG_PM
static int rtl8180_suspend(struct pci_dev *pdev, pm_message_t state)
{
	pci_save_state(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	return 0;
}

static int rtl8180_resume(struct pci_dev *pdev)
{
	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	return 0;
}

#endif /* CONFIG_PM */

static struct pci_driver rtl8180_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= rtl8180_table,
	.probe		= rtl8180_probe,
	.remove		= rtl8180_remove,
#ifdef CONFIG_PM
	.suspend	= rtl8180_suspend,
	.resume		= rtl8180_resume,
#endif /* CONFIG_PM */
};

module_pci_driver(rtl8180_driver);
