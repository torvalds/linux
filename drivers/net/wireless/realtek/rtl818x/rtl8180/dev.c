
/* Linux device driver for RTL8180 / RTL8185 / RTL8187SE
 *
 * Copyright 2007 Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007,2014 Andrea Merello <andrea.merello@gmail.com>
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 *
 * Thanks to Realtek for their support!
 *
 ************************************************************************
 *
 * The driver was extended to the RTL8187SE in 2014 by
 * Andrea Merello <andrea.merello@gmail.com>
 *
 * based also on:
 *  - portions of rtl8187se Linux staging driver, Copyright Realtek corp.
 *    (available in drivers/staging/rtl8187se directory of Linux 3.14)
 *  - other GPL, unpublished (until now), Linux driver code,
 *    Copyright Larry Finger <Larry.Finger@lwfinger.net>
 *
 * A huge thanks goes to Sara V. Nari who forgives me when I'm
 * sitting in front of my laptop at evening, week-end, night...
 *
 * A special thanks goes to Antonio Cuni, who helped me with
 * some python userspace stuff I used to debug RTL8187SE code, and who
 * bought a laptop with an unsupported Wi-Fi card some years ago...
 *
 * Thanks to Larry Finger for writing some code for rtl8187se and for
 * his suggestions.
 *
 * Thanks to Dan Carpenter for reviewing my initial patch and for his
 * suggestions.
 *
 * Thanks to Bernhard Schiffner for his help in testing and for his
 * suggestions.
 *
 ************************************************************************
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
#include "rtl8225se.h"

MODULE_AUTHOR("Michael Wu <flamingice@sourmilk.net>");
MODULE_AUTHOR("Andrea Merello <andrea.merello@gmail.com>");
MODULE_DESCRIPTION("RTL8180 / RTL8185 / RTL8187SE PCI wireless driver");
MODULE_LICENSE("GPL");

static const struct pci_device_id rtl8180_table[] = {

	/* rtl8187se */
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK, 0x8199) },

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

/* Queues for rtl8187se card
 *
 * name | reg  |  queue
 *  BC  |  7   |   6
 *  MG  |  1   |   0
 *  HI  |  6   |   1
 *  VO  |  5   |   2
 *  VI  |  4   |   3
 *  BE  |  3   |   4
 *  BK  |  2   |   5
 *
 * The complete map for DMA kick reg using use all queue is:
 * static const int rtl8187se_queues_map[RTL8187SE_NR_TX_QUEUES] =
 *	{1, 6, 5, 4, 3, 2, 7};
 *
 * .. but.. Because for mac80211 4 queues are enough for QoS we use this
 *
 * name | reg  |  queue
 *  BC  |  7   |   4  <- currently not used yet
 *  MG  |  1   |   x  <- Not used
 *  HI  |  6   |   x  <- Not used
 *  VO  |  5   |   0  <- used
 *  VI  |  4   |   1  <- used
 *  BE  |  3   |   2  <- used
 *  BK  |  2   |   3  <- used
 *
 * Beacon queue could be used, but this is not finished yet.
 *
 * I thougth about using the other two queues but I decided not to do this:
 *
 * - I'm unsure whether the mac80211 will ever try to use more than 4 queues
 *   by itself.
 *
 * - I could route MGMT frames (currently sent over VO queue) to the MGMT
 *   queue but since mac80211 will do not know about it, I will probably gain
 *   some HW priority whenever the VO queue is not empty, but this gain is
 *   limited by the fact that I had to stop the mac80211 queue whenever one of
 *   the VO or MGMT queues is full, stopping also submitting of MGMT frame
 *   to the driver.
 *
 * - I don't know how to set in the HW the contention window params for MGMT
 *   and HI-prio queues.
 */

static const int rtl8187se_queues_map[RTL8187SE_NR_TX_QUEUES] = {5, 4, 3, 2, 7};

/* Queues for rtl8180/rtl8185 cards
 *
 * name | reg  |  prio
 *  BC  |  7   |   3
 *  HI  |  6   |   0
 *  NO  |  5   |   1
 *  LO  |  4   |   2
 *
 * The complete map for DMA kick reg using all queue is:
 * static const int rtl8180_queues_map[RTL8180_NR_TX_QUEUES] = {6, 5, 4, 7};
 *
 * .. but .. Because the mac80211 needs at least 4 queues for QoS or
 * otherwise QoS can't be done, we use just one.
 * Beacon queue could be used, but this is not finished yet.
 * Actual map is:
 *
 * name | reg  |  prio
 *  BC  |  7   |   1  <- currently not used yet.
 *  HI  |  6   |   x  <- not used
 *  NO  |  5   |   x  <- not used
 *  LO  |  4   |   0  <- used
 */

static const int rtl8180_queues_map[RTL8180_NR_TX_QUEUES] = {4, 7};

/* LNA gain table for rtl8187se */
static const u8 rtl8187se_lna_gain[4] = {02, 17, 29, 39};

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
	struct rtl818x_rx_cmd_desc *cmd_desc;
	unsigned int count = 32;
	u8 agc, sq;
	s8 signal = 1;
	dma_addr_t mapping;

	while (count--) {
		void *entry = priv->rx_ring + priv->rx_idx * priv->rx_ring_sz;
		struct sk_buff *skb = priv->rx_buf[priv->rx_idx];
		u32 flags, flags2, flags3 = 0;
		u64 tsft;

		if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE) {
			struct rtl8187se_rx_desc *desc = entry;

			flags = le32_to_cpu(desc->flags);
			/* if ownership flag is set, then we can trust the
			 * HW has written other fields. We must not trust
			 * other descriptor data read before we checked (read)
			 * the ownership flag
			 */
			rmb();
			flags3 = le32_to_cpu(desc->flags3);
			flags2 = le32_to_cpu(desc->flags2);
			tsft = le64_to_cpu(desc->tsft);
		} else {
			struct rtl8180_rx_desc *desc = entry;

			flags = le32_to_cpu(desc->flags);
			/* same as above */
			rmb();
			flags2 = le32_to_cpu(desc->flags2);
			tsft = le64_to_cpu(desc->tsft);
		}

		if (flags & RTL818X_RX_DESC_FLAG_OWN)
			return;

		if (unlikely(flags & (RTL818X_RX_DESC_FLAG_DMA_FAIL |
				      RTL818X_RX_DESC_FLAG_FOF |
				      RTL818X_RX_DESC_FLAG_RX_ERR)))
			goto done;
		else {
			struct ieee80211_rx_status rx_status = {0};
			struct sk_buff *new_skb = dev_alloc_skb(MAX_RX_SIZE);

			if (unlikely(!new_skb))
				goto done;

			mapping = pci_map_single(priv->pdev,
					       skb_tail_pointer(new_skb),
					       MAX_RX_SIZE, PCI_DMA_FROMDEVICE);

			if (pci_dma_mapping_error(priv->pdev, mapping)) {
				kfree_skb(new_skb);
				dev_err(&priv->pdev->dev, "RX DMA map error\n");

				goto done;
			}

			pci_unmap_single(priv->pdev,
					 *((dma_addr_t *)skb->cb),
					 MAX_RX_SIZE, PCI_DMA_FROMDEVICE);
			skb_put(skb, flags & 0xFFF);

			rx_status.antenna = (flags2 >> 15) & 1;
			rx_status.rate_idx = (flags >> 20) & 0xF;
			agc = (flags2 >> 17) & 0x7F;

			switch (priv->chip_family) {
			case RTL818X_CHIP_FAMILY_RTL8185:
				if (rx_status.rate_idx > 3)
					signal = -clamp_t(u8, agc, 25, 90) - 9;
				else
					signal = -clamp_t(u8, agc, 30, 95);
				break;
			case RTL818X_CHIP_FAMILY_RTL8180:
				sq = flags2 & 0xff;
				signal = priv->rf->calc_rssi(agc, sq);
				break;
			case RTL818X_CHIP_FAMILY_RTL8187SE:
				/* OFDM measure reported by HW is signed,
				 * in 0.5dBm unit, with zero centered @ -41dBm
				 * input signal.
				 */
				if (rx_status.rate_idx > 3) {
					signal = (s8)((flags3 >> 16) & 0xff);
					signal = signal / 2 - 41;
				} else {
					int idx, bb;

					idx = (agc & 0x60) >> 5;
					bb = (agc & 0x1F) * 2;
					/* bias + BB gain + LNA gain */
					signal = 4 - bb - rtl8187se_lna_gain[idx];
				}
				break;
			}
			rx_status.signal = signal;
			rx_status.freq = dev->conf.chandef.chan->center_freq;
			rx_status.band = dev->conf.chandef.chan->band;
			rx_status.mactime = tsft;
			rx_status.flag |= RX_FLAG_MACTIME_START;
			if (flags & RTL818X_RX_DESC_FLAG_SPLCP)
				rx_status.enc_flags |= RX_ENC_FLAG_SHORTPRE;
			if (flags & RTL818X_RX_DESC_FLAG_CRC32_ERR)
				rx_status.flag |= RX_FLAG_FAILED_FCS_CRC;

			memcpy(IEEE80211_SKB_RXCB(skb), &rx_status, sizeof(rx_status));
			ieee80211_rx_irqsafe(dev, skb);

			skb = new_skb;
			priv->rx_buf[priv->rx_idx] = skb;
			*((dma_addr_t *) skb->cb) = mapping;
		}

	done:
		cmd_desc = entry;
		cmd_desc->rx_buf = cpu_to_le32(*((dma_addr_t *)skb->cb));
		cmd_desc->flags = cpu_to_le32(RTL818X_RX_DESC_FLAG_OWN |
					   MAX_RX_SIZE);
		if (priv->rx_idx == 31)
			cmd_desc->flags |=
				cpu_to_le32(RTL818X_RX_DESC_FLAG_EOR);
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

		ieee80211_tx_status_irqsafe(dev, skb);
		if (ring->entries - skb_queue_len(&ring->queue) == 2)
			ieee80211_wake_queue(dev, prio);
	}
}

static irqreturn_t rtl8187se_interrupt(int irq, void *dev_id)
{
	struct ieee80211_hw *dev = dev_id;
	struct rtl8180_priv *priv = dev->priv;
	u32 reg;
	unsigned long flags;
	static int desc_err;

	spin_lock_irqsave(&priv->lock, flags);
	/* Note: 32-bit interrupt status */
	reg = rtl818x_ioread32(priv, &priv->map->INT_STATUS_SE);
	if (unlikely(reg == 0xFFFFFFFF)) {
		spin_unlock_irqrestore(&priv->lock, flags);
		return IRQ_HANDLED;
	}

	rtl818x_iowrite32(priv, &priv->map->INT_STATUS_SE, reg);

	if (reg & IMR_TIMEOUT1)
		rtl818x_iowrite32(priv, &priv->map->INT_TIMEOUT, 0);

	if (reg & (IMR_TBDOK | IMR_TBDER))
		rtl8180_handle_tx(dev, 4);

	if (reg & (IMR_TVODOK | IMR_TVODER))
		rtl8180_handle_tx(dev, 0);

	if (reg & (IMR_TVIDOK | IMR_TVIDER))
		rtl8180_handle_tx(dev, 1);

	if (reg & (IMR_TBEDOK | IMR_TBEDER))
		rtl8180_handle_tx(dev, 2);

	if (reg & (IMR_TBKDOK | IMR_TBKDER))
		rtl8180_handle_tx(dev, 3);

	if (reg & (IMR_ROK | IMR_RER | RTL818X_INT_SE_RX_DU | IMR_RQOSOK))
		rtl8180_handle_rx(dev);
	/* The interface sometimes generates several RX DMA descriptor errors
	 * at startup. Do not report these.
	 */
	if ((reg & RTL818X_INT_SE_RX_DU) && desc_err++ > 2)
		if (net_ratelimit())
			wiphy_err(dev->wiphy, "No RX DMA Descriptor avail\n");

	spin_unlock_irqrestore(&priv->lock, flags);
	return IRQ_HANDLED;
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
	unsigned int idx, prio, hw_prio;
	dma_addr_t mapping;
	u32 tx_flags;
	u8 rc_flags;
	u16 plcp_len = 0;
	__le16 rts_duration = 0;
	/* do arithmetic and then convert to le16 */
	u16 frame_duration = 0;

	prio = skb_get_queue_mapping(skb);
	ring = &priv->tx_ring[prio];

	mapping = pci_map_single(priv->pdev, skb->data,
				 skb->len, PCI_DMA_TODEVICE);

	if (pci_dma_mapping_error(priv->pdev, mapping)) {
		kfree_skb(skb);
		dev_err(&priv->pdev->dev, "TX DMA mapping error\n");
		return;
	}

	tx_flags = RTL818X_TX_DESC_FLAG_OWN | RTL818X_TX_DESC_FLAG_FS |
		   RTL818X_TX_DESC_FLAG_LS |
		   (ieee80211_get_tx_rate(dev, info)->hw_value << 24) |
		   skb->len;

	if (priv->chip_family != RTL818X_CHIP_FAMILY_RTL8180)
		tx_flags |= RTL818X_TX_DESC_FLAG_DMA |
			    RTL818X_TX_DESC_FLAG_NO_ENC;

	rc_flags = info->control.rates[0].flags;

	/* HW will perform RTS-CTS when only RTS flags is set.
	 * HW will perform CTS-to-self when both RTS and CTS flags are set.
	 * RTS rate and RTS duration will be used also for CTS-to-self.
	 */
	if (rc_flags & IEEE80211_TX_RC_USE_RTS_CTS) {
		tx_flags |= RTL818X_TX_DESC_FLAG_RTS;
		tx_flags |= ieee80211_get_rts_cts_rate(dev, info)->hw_value << 19;
		rts_duration = ieee80211_rts_duration(dev, priv->vif,
						skb->len, info);
	} else if (rc_flags & IEEE80211_TX_RC_USE_CTS_PROTECT) {
		tx_flags |= RTL818X_TX_DESC_FLAG_RTS | RTL818X_TX_DESC_FLAG_CTS;
		tx_flags |= ieee80211_get_rts_cts_rate(dev, info)->hw_value << 19;
		rts_duration = ieee80211_ctstoself_duration(dev, priv->vif,
						skb->len, info);
	}

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8180) {
		unsigned int remainder;

		plcp_len = DIV_ROUND_UP(16 * (skb->len + 4),
				(ieee80211_get_tx_rate(dev, info)->bitrate * 2) / 10);
		remainder = (16 * (skb->len + 4)) %
			    ((ieee80211_get_tx_rate(dev, info)->bitrate * 2) / 10);
		if (remainder <= 6)
			plcp_len |= 1 << 15;
	}

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE) {
		__le16 duration;
		/* SIFS time (required by HW) is already included by
		 * ieee80211_generic_frame_duration
		 */
		duration = ieee80211_generic_frame_duration(dev, priv->vif,
					NL80211_BAND_2GHZ, skb->len,
					ieee80211_get_tx_rate(dev, info));

		frame_duration =  priv->ack_time + le16_to_cpu(duration);
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

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE) {
		entry->frame_duration = cpu_to_le16(frame_duration);
		entry->frame_len_se = cpu_to_le16(skb->len);

		/* tpc polarity */
		entry->flags3 = cpu_to_le16(1<<4);
	} else
		entry->frame_len = cpu_to_le32(skb->len);

	entry->rts_duration = rts_duration;
	entry->plcp_len = cpu_to_le16(plcp_len);
	entry->tx_buf = cpu_to_le32(mapping);

	entry->retry_limit = info->control.rates[0].count - 1;

	/* We must be sure that tx_flags is written last because the HW
	 * looks at it to check if the rest of data is valid or not
	 */
	wmb();
	entry->flags = cpu_to_le32(tx_flags);
	/* We must be sure this has been written before followings HW
	 * register write, because this write will made the HW attempts
	 * to DMA the just-written data
	 */
	wmb();

	__skb_queue_tail(&ring->queue, skb);
	if (ring->entries - skb_queue_len(&ring->queue) < 2)
		ieee80211_stop_queue(dev, prio);

	spin_unlock_irqrestore(&priv->lock, flags);

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE) {
		/* just poll: rings are stopped with TPPollStop reg */
		hw_prio = rtl8187se_queues_map[prio];
		rtl818x_iowrite8(priv, &priv->map->TX_DMA_POLLING,
			 (1 << hw_prio));
	} else {
		hw_prio = rtl8180_queues_map[prio];
		rtl818x_iowrite8(priv, &priv->map->TX_DMA_POLLING,
			 (1 << hw_prio) | /* ring to poll  */
			 (1<<1) | (1<<2));/* stopped rings */
	}
}

static void rtl8180_set_anaparam3(struct rtl8180_priv *priv, u16 anaparam3)
{
	u8 reg;

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD,
			 RTL818X_EEPROM_CMD_CONFIG);

	reg = rtl818x_ioread8(priv, &priv->map->CONFIG3);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3,
		 reg | RTL818X_CONFIG3_ANAPARAM_WRITE);

	rtl818x_iowrite16(priv, &priv->map->ANAPARAM3, anaparam3);

	rtl818x_iowrite8(priv, &priv->map->CONFIG3,
		 reg & ~RTL818X_CONFIG3_ANAPARAM_WRITE);

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD,
			 RTL818X_EEPROM_CMD_NORMAL);
}

void rtl8180_set_anaparam2(struct rtl8180_priv *priv, u32 anaparam2)
{
	u8 reg;

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD,
			 RTL818X_EEPROM_CMD_CONFIG);

	reg = rtl818x_ioread8(priv, &priv->map->CONFIG3);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3,
		 reg | RTL818X_CONFIG3_ANAPARAM_WRITE);

	rtl818x_iowrite32(priv, &priv->map->ANAPARAM2, anaparam2);

	rtl818x_iowrite8(priv, &priv->map->CONFIG3,
		 reg & ~RTL818X_CONFIG3_ANAPARAM_WRITE);

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD,
			 RTL818X_EEPROM_CMD_NORMAL);
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

static void rtl8187se_mac_config(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	u8 reg;

	rtl818x_iowrite32(priv, REG_ADDR4(0x1F0), 0);
	rtl818x_ioread32(priv, REG_ADDR4(0x1F0));
	rtl818x_iowrite32(priv, REG_ADDR4(0x1F4), 0);
	rtl818x_ioread32(priv, REG_ADDR4(0x1F4));
	rtl818x_iowrite8(priv, REG_ADDR1(0x1F8), 0);
	rtl818x_ioread8(priv, REG_ADDR1(0x1F8));
	/* Enable DA10 TX power saving */
	reg = rtl818x_ioread8(priv, &priv->map->PHY_PR);
	rtl818x_iowrite8(priv, &priv->map->PHY_PR, reg | 0x04);
	/* Power */
	rtl818x_iowrite16(priv, PI_DATA_REG, 0x1000);
	rtl818x_iowrite16(priv, SI_DATA_REG, 0x1000);
	/* AFE - default to power ON */
	rtl818x_iowrite16(priv, REG_ADDR2(0x370), 0x0560);
	rtl818x_iowrite16(priv, REG_ADDR2(0x372), 0x0560);
	rtl818x_iowrite16(priv, REG_ADDR2(0x374), 0x0DA4);
	rtl818x_iowrite16(priv, REG_ADDR2(0x376), 0x0DA4);
	rtl818x_iowrite16(priv, REG_ADDR2(0x378), 0x0560);
	rtl818x_iowrite16(priv, REG_ADDR2(0x37A), 0x0560);
	rtl818x_iowrite16(priv, REG_ADDR2(0x37C), 0x00EC);
	rtl818x_iowrite16(priv, REG_ADDR2(0x37E), 0x00EC);
	rtl818x_iowrite8(priv, REG_ADDR1(0x24E), 0x01);
	/* unknown, needed for suspend to RAM resume */
	rtl818x_iowrite8(priv, REG_ADDR1(0x0A), 0x72);
}

static void rtl8187se_set_antenna_config(struct ieee80211_hw *dev, u8 def_ant,
					 bool diversity)
{
	struct rtl8180_priv *priv = dev->priv;

	rtl8225_write_phy_cck(dev, 0x0C, 0x09);
	if (diversity) {
		if (def_ant == 1) {
			rtl818x_iowrite8(priv, &priv->map->TX_ANTENNA, 0x00);
			rtl8225_write_phy_cck(dev, 0x11, 0xBB);
			rtl8225_write_phy_cck(dev, 0x01, 0xC7);
			rtl8225_write_phy_ofdm(dev, 0x0D, 0x54);
			rtl8225_write_phy_ofdm(dev, 0x18, 0xB2);
		} else { /* main antenna */
			rtl818x_iowrite8(priv, &priv->map->TX_ANTENNA, 0x03);
			rtl8225_write_phy_cck(dev, 0x11, 0x9B);
			rtl8225_write_phy_cck(dev, 0x01, 0xC7);
			rtl8225_write_phy_ofdm(dev, 0x0D, 0x5C);
			rtl8225_write_phy_ofdm(dev, 0x18, 0xB2);
		}
	} else { /* disable antenna diversity */
		if (def_ant == 1) {
			rtl818x_iowrite8(priv, &priv->map->TX_ANTENNA, 0x00);
			rtl8225_write_phy_cck(dev, 0x11, 0xBB);
			rtl8225_write_phy_cck(dev, 0x01, 0x47);
			rtl8225_write_phy_ofdm(dev, 0x0D, 0x54);
			rtl8225_write_phy_ofdm(dev, 0x18, 0x32);
		} else { /* main antenna */
			rtl818x_iowrite8(priv, &priv->map->TX_ANTENNA, 0x03);
			rtl8225_write_phy_cck(dev, 0x11, 0x9B);
			rtl8225_write_phy_cck(dev, 0x01, 0x47);
			rtl8225_write_phy_ofdm(dev, 0x0D, 0x5C);
			rtl8225_write_phy_ofdm(dev, 0x18, 0x32);
		}
	}
	/* priv->curr_ant = def_ant; */
}

static void rtl8180_int_enable(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE) {
		rtl818x_iowrite32(priv, &priv->map->IMR,
			  IMR_TBDER | IMR_TBDOK |
			  IMR_TVODER | IMR_TVODOK |
			  IMR_TVIDER | IMR_TVIDOK |
			  IMR_TBEDER | IMR_TBEDOK |
			  IMR_TBKDER | IMR_TBKDOK |
			  IMR_RDU | IMR_RER |
			  IMR_ROK | IMR_RQOSOK);
	} else {
		rtl818x_iowrite16(priv, &priv->map->INT_MASK, 0xFFFF);
	}
}

static void rtl8180_int_disable(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE) {
		rtl818x_iowrite32(priv, &priv->map->IMR, 0);
	} else {
		rtl818x_iowrite16(priv, &priv->map->INT_MASK, 0);
	}
}

static void rtl8180_conf_basic_rates(struct ieee80211_hw *dev,
			    u32 basic_mask)
{
	struct rtl8180_priv *priv = dev->priv;
	u16 reg;
	u32 resp_mask;
	u8 basic_max;
	u8 resp_max, resp_min;

	resp_mask = basic_mask;
	/* IEEE80211 says the response rate should be equal to the highest basic
	 * rate that is not faster than received frame. But it says also that if
	 * the basic rate set does not contains any rate for the current
	 * modulation class then mandatory rate set must be used for that
	 * modulation class. Eventually add OFDM mandatory rates..
	 */
	if ((resp_mask & 0xf) == resp_mask)
		resp_mask |= 0x150; /* 6, 12, 24Mbps */

	switch (priv->chip_family) {

	case RTL818X_CHIP_FAMILY_RTL8180:
		/* in 8180 this is NOT a BITMAP */
		basic_max = fls(basic_mask) - 1;
		reg = rtl818x_ioread16(priv, &priv->map->BRSR);
		reg &= ~3;
		reg |= basic_max;
		rtl818x_iowrite16(priv, &priv->map->BRSR, reg);
		break;

	case RTL818X_CHIP_FAMILY_RTL8185:
		resp_max = fls(resp_mask) - 1;
		resp_min = ffs(resp_mask) - 1;
		/* in 8185 this is a BITMAP */
		rtl818x_iowrite16(priv, &priv->map->BRSR, basic_mask);
		rtl818x_iowrite8(priv, &priv->map->RESP_RATE, (resp_max << 4) |
				resp_min);
		break;

	case RTL818X_CHIP_FAMILY_RTL8187SE:
		/* in 8187se this is a BITMAP. BRSR reg actually sets
		 * response rates.
		 */
		rtl818x_iowrite16(priv, &priv->map->BRSR_8187SE, resp_mask);
		break;
	}
}

static void rtl8180_config_cardbus(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	u16 reg16;
	u8 reg8;

	reg8 = rtl818x_ioread8(priv, &priv->map->CONFIG3);
	reg8 |= 1 << 1;
	rtl818x_iowrite8(priv, &priv->map->CONFIG3, reg8);

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE) {
		rtl818x_iowrite16(priv, FEMR_SE, 0xffff);
	} else {
		reg16 = rtl818x_ioread16(priv, &priv->map->FEMR);
		reg16 |= (1 << 15) | (1 << 14) | (1 << 4);
		rtl818x_iowrite16(priv, &priv->map->FEMR, reg16);
	}

}

static int rtl8180_init_hw(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	u16 reg;
	u32 reg32;

	rtl818x_iowrite8(priv, &priv->map->CMD, 0);
	rtl818x_ioread8(priv, &priv->map->CMD);
	msleep(10);

	/* reset */
	rtl8180_int_disable(dev);
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
		rtl8180_config_cardbus(dev);
	}

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE)
		rtl818x_iowrite8(priv, &priv->map->MSR, RTL818X_MSR_ENEDCA);
	else
		rtl818x_iowrite8(priv, &priv->map->MSR, 0);

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8180)
		rtl8180_set_anaparam(priv, priv->anaparam);

	rtl818x_iowrite32(priv, &priv->map->RDSAR, priv->rx_ring_dma);
	/* mac80211 queue have higher prio for lower index. The last queue
	 * (that mac80211 is not aware of) is reserved for beacons (and have
	 * the highest priority on the NIC)
	 */
	if (priv->chip_family != RTL818X_CHIP_FAMILY_RTL8187SE) {
		rtl818x_iowrite32(priv, &priv->map->TBDA,
				  priv->tx_ring[1].dma);
		rtl818x_iowrite32(priv, &priv->map->TLPDA,
				  priv->tx_ring[0].dma);
	} else {
		rtl818x_iowrite32(priv, &priv->map->TBDA,
				  priv->tx_ring[4].dma);
		rtl818x_iowrite32(priv, &priv->map->TVODA,
				  priv->tx_ring[0].dma);
		rtl818x_iowrite32(priv, &priv->map->TVIDA,
				  priv->tx_ring[1].dma);
		rtl818x_iowrite32(priv, &priv->map->TBEDA,
				  priv->tx_ring[2].dma);
		rtl818x_iowrite32(priv, &priv->map->TBKDA,
				  priv->tx_ring[3].dma);
	}

	/* TODO: necessary? specs indicate not */
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
	reg = rtl818x_ioread8(priv, &priv->map->CONFIG2);
	rtl818x_iowrite8(priv, &priv->map->CONFIG2, reg & ~(1 << 3));
	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8185) {
		reg = rtl818x_ioread8(priv, &priv->map->CONFIG2);
		rtl818x_iowrite8(priv, &priv->map->CONFIG2, reg | (1 << 4));
	}
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);

	/* TODO: set CONFIG5 for calibrating AGC on rtl8180 + philips radio? */

	/* TODO: turn off hw wep on rtl8180 */

	rtl818x_iowrite32(priv, &priv->map->INT_TIMEOUT, 0);

	if (priv->chip_family != RTL818X_CHIP_FAMILY_RTL8180) {
		rtl818x_iowrite8(priv, &priv->map->WPA_CONF, 0);
		rtl818x_iowrite8(priv, &priv->map->RATE_FALLBACK, 0);
	} else {
		rtl818x_iowrite8(priv, &priv->map->SECURITY, 0);

		rtl818x_iowrite8(priv, &priv->map->PHY_DELAY, 0x6);
		rtl818x_iowrite8(priv, &priv->map->CARRIER_SENSE_COUNTER, 0x4C);
	}

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8185) {
		/* TODO: set ClkRun enable? necessary? */
		reg = rtl818x_ioread8(priv, &priv->map->GP_ENABLE);
		rtl818x_iowrite8(priv, &priv->map->GP_ENABLE, reg & ~(1 << 6));
		rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
		reg = rtl818x_ioread8(priv, &priv->map->CONFIG3);
		rtl818x_iowrite8(priv, &priv->map->CONFIG3, reg | (1 << 2));
		rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);
		/* fix eccessive IFS after CTS-to-self */
		if (priv->map_pio) {
			u8 reg;

			reg = rtl818x_ioread8(priv, &priv->map->PGSELECT);
			rtl818x_iowrite8(priv, &priv->map->PGSELECT, reg | 1);
			rtl818x_iowrite8(priv, REG_ADDR1(0xff), 0x35);
			rtl818x_iowrite8(priv, &priv->map->PGSELECT, reg);
		} else
			rtl818x_iowrite8(priv, REG_ADDR1(0x1ff), 0x35);
	}

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE) {

		/* the set auto rate fallback bitmask from 1M to 54 Mb/s */
		rtl818x_iowrite16(priv, ARFR, 0xFFF);
		rtl818x_ioread16(priv, ARFR);

		/* stop unused queus (no dma alloc) */
		rtl818x_iowrite8(priv, &priv->map->TPPOLL_STOP,
			       RTL818x_TPPOLL_STOP_MG | RTL818x_TPPOLL_STOP_HI);

		rtl818x_iowrite8(priv, &priv->map->ACM_CONTROL, 0x00);
		rtl818x_iowrite16(priv, &priv->map->TID_AC_MAP, 0xFA50);

		rtl818x_iowrite16(priv, &priv->map->INT_MIG, 0);

		/* some black magic here.. */
		rtl8187se_mac_config(dev);

		rtl818x_iowrite16(priv, RFSW_CTRL, 0x569A);
		rtl818x_ioread16(priv, RFSW_CTRL);

		rtl8180_set_anaparam(priv, RTL8225SE_ANAPARAM_ON);
		rtl8180_set_anaparam2(priv, RTL8225SE_ANAPARAM2_ON);
		rtl8180_set_anaparam3(priv, RTL8225SE_ANAPARAM3);


		rtl818x_iowrite8(priv, &priv->map->CONFIG5,
			    rtl818x_ioread8(priv, &priv->map->CONFIG5) & 0x7F);

		/*probably this switch led on */
		rtl818x_iowrite8(priv, &priv->map->PGSELECT,
			    rtl818x_ioread8(priv, &priv->map->PGSELECT) | 0x08);

		rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, 0x0480);
		rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, 0x1BFF);
		rtl818x_iowrite16(priv, &priv->map->RFPinsSelect, 0x2488);

		rtl818x_iowrite32(priv, &priv->map->RF_TIMING, 0x4003);

		/* the reference code mac hardcode table write
		 * this reg by doing byte-wide accesses.
		 * It does it just for lowest and highest byte..
		 */
		reg32 = rtl818x_ioread32(priv, &priv->map->RF_PARA);
		reg32 &= 0x00ffff00;
		reg32 |= 0xb8000054;
		rtl818x_iowrite32(priv, &priv->map->RF_PARA, reg32);
	} else
		/* stop unused queus (no dma alloc) */
		rtl818x_iowrite8(priv, &priv->map->TX_DMA_POLLING,
			    (1<<1) | (1<<2));

	priv->rf->init(dev);

	/* default basic rates are 1,2 Mbps for rtl8180. 1,2,6,9,12,18,24 Mbps
	 * otherwise. bitmask 0x3 and 0x01f3 respectively.
	 * NOTE: currenty rtl8225 RF code changes basic rates, so we need to do
	 * this after rf init.
	 * TODO: try to find out whether RF code really needs to do this..
	 */
	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8180)
		rtl8180_conf_basic_rates(dev, 0x3);
	else
		rtl8180_conf_basic_rates(dev, 0x1f3);

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE)
		rtl8187se_set_antenna_config(dev,
					     priv->antenna_diversity_default,
					     priv->antenna_diversity_en);
	return 0;
}

static int rtl8180_init_rx_ring(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	struct rtl818x_rx_cmd_desc *entry;
	int i;

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE)
		priv->rx_ring_sz = sizeof(struct rtl8187se_rx_desc);
	else
		priv->rx_ring_sz = sizeof(struct rtl8180_rx_desc);

	priv->rx_ring = pci_zalloc_consistent(priv->pdev, priv->rx_ring_sz * 32,
					      &priv->rx_ring_dma);
	if (!priv->rx_ring || (unsigned long)priv->rx_ring & 0xFF) {
		wiphy_err(dev->wiphy, "Cannot allocate RX ring\n");
		return -ENOMEM;
	}

	priv->rx_idx = 0;

	for (i = 0; i < 32; i++) {
		struct sk_buff *skb = dev_alloc_skb(MAX_RX_SIZE);
		dma_addr_t *mapping;
		entry = priv->rx_ring + priv->rx_ring_sz*i;
		if (!skb) {
			pci_free_consistent(priv->pdev, priv->rx_ring_sz * 32,
					priv->rx_ring, priv->rx_ring_dma);
			wiphy_err(dev->wiphy, "Cannot allocate RX skb\n");
			return -ENOMEM;
		}
		priv->rx_buf[i] = skb;
		mapping = (dma_addr_t *)skb->cb;
		*mapping = pci_map_single(priv->pdev, skb_tail_pointer(skb),
					  MAX_RX_SIZE, PCI_DMA_FROMDEVICE);

		if (pci_dma_mapping_error(priv->pdev, *mapping)) {
			kfree_skb(skb);
			pci_free_consistent(priv->pdev, priv->rx_ring_sz * 32,
					priv->rx_ring, priv->rx_ring_dma);
			wiphy_err(dev->wiphy, "Cannot map DMA for RX skb\n");
			return -ENOMEM;
		}

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

	pci_free_consistent(priv->pdev, priv->rx_ring_sz * 32,
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

	ring = pci_zalloc_consistent(priv->pdev, sizeof(*ring) * entries,
				     &dma);
	if (!ring || (unsigned long)ring & 0xFF) {
		wiphy_err(dev->wiphy, "Cannot allocate TX ring (prio = %d)\n",
			  prio);
		return -ENOMEM;
	}

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

	for (i = 0; i < (dev->queues + 1); i++)
		if ((ret = rtl8180_init_tx_ring(dev, i, 16)))
			goto err_free_rings;

	ret = rtl8180_init_hw(dev);
	if (ret)
		goto err_free_rings;

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE) {
		ret = request_irq(priv->pdev->irq, rtl8187se_interrupt,
			  IRQF_SHARED, KBUILD_MODNAME, dev);
	} else {
		ret = request_irq(priv->pdev->irq, rtl8180_interrupt,
			  IRQF_SHARED, KBUILD_MODNAME, dev);
	}

	if (ret) {
		wiphy_err(dev->wiphy, "failed to register IRQ handler\n");
		goto err_free_rings;
	}

	rtl8180_int_enable(dev);

	/* in rtl8187se at MAR regs offset there is the management
	 * TX descriptor DMA addres..
	 */
	if (priv->chip_family != RTL818X_CHIP_FAMILY_RTL8187SE) {
		rtl818x_iowrite32(priv, &priv->map->MAR[0], ~0);
		rtl818x_iowrite32(priv, &priv->map->MAR[1], ~0);
	}

	reg = RTL818X_RX_CONF_ONLYERLPKT |
	      RTL818X_RX_CONF_RX_AUTORESETPHY |
	      RTL818X_RX_CONF_MGMT |
	      RTL818X_RX_CONF_DATA |
	      (7 << 8 /* MAX RX DMA */) |
	      RTL818X_RX_CONF_BROADCAST |
	      RTL818X_RX_CONF_NICMAC;

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8185)
		reg |= RTL818X_RX_CONF_CSDM1 | RTL818X_RX_CONF_CSDM2;
	else if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8180) {
		reg |= (priv->rfparam & RF_PARAM_CARRIERSENSE1)
			? RTL818X_RX_CONF_CSDM1 : 0;
		reg |= (priv->rfparam & RF_PARAM_CARRIERSENSE2)
			? RTL818X_RX_CONF_CSDM2 : 0;
	} else {
		reg &= ~(RTL818X_RX_CONF_CSDM1 | RTL818X_RX_CONF_CSDM2);
	}

	priv->rx_conf = reg;
	rtl818x_iowrite32(priv, &priv->map->RX_CONF, reg);

	if (priv->chip_family != RTL818X_CHIP_FAMILY_RTL8180) {
		reg = rtl818x_ioread8(priv, &priv->map->CW_CONF);

		/* CW is not on per-packet basis.
		 * in rtl8185 the CW_VALUE reg is used.
		 * in rtl8187se the AC param regs are used.
		 */
		reg &= ~RTL818X_CW_CONF_PERPACKET_CW;
		/* retry limit IS on per-packet basis.
		 * the short and long retry limit in TX_CONF
		 * reg are ignored
		 */
		reg |= RTL818X_CW_CONF_PERPACKET_RETRY;
		rtl818x_iowrite8(priv, &priv->map->CW_CONF, reg);

		reg = rtl818x_ioread8(priv, &priv->map->TX_AGC_CTL);
		/* TX antenna and TX gain are not on per-packet basis.
		 * TX Antenna is selected by ANTSEL reg (RX in BB regs).
		 * TX gain is selected with CCK_TX_AGC and OFDM_TX_AGC regs
		 */
		reg &= ~RTL818X_TX_AGC_CTL_PERPACKET_GAIN;
		reg &= ~RTL818X_TX_AGC_CTL_PERPACKET_ANTSEL;
		reg |=  RTL818X_TX_AGC_CTL_FEEDBACK_ANT;
		rtl818x_iowrite8(priv, &priv->map->TX_AGC_CTL, reg);

		/* disable early TX */
		rtl818x_iowrite8(priv, (u8 __iomem *)priv->map + 0xec, 0x3f);
	}

	reg = rtl818x_ioread32(priv, &priv->map->TX_CONF);
	reg |= (6 << 21 /* MAX TX DMA */) |
	       RTL818X_TX_CONF_NO_ICV;

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE)
		reg |= 1<<30;  /*  "duration procedure mode" */

	if (priv->chip_family != RTL818X_CHIP_FAMILY_RTL8180)
		reg &= ~RTL818X_TX_CONF_PROBE_DTS;
	else
		reg &= ~RTL818X_TX_CONF_HW_SEQNUM;

	reg &= ~RTL818X_TX_CONF_DISCW;

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
	for (i = 0; i < (dev->queues + 1); i++)
		if (priv->tx_ring[i].desc)
			rtl8180_free_tx_ring(dev, i);

	return ret;
}

static void rtl8180_stop(struct ieee80211_hw *dev)
{
	struct rtl8180_priv *priv = dev->priv;
	u8 reg;
	int i;

	rtl8180_int_disable(dev);

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
	for (i = 0; i < (dev->queues + 1); i++)
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

static void rtl8187se_conf_ac_parm(struct ieee80211_hw *dev, u8 queue)
{
	const struct ieee80211_tx_queue_params *params;
	struct rtl8180_priv *priv = dev->priv;

	/* hw value */
	u32 ac_param;

	u8 aifs;
	u8 txop;
	u8 cw_min, cw_max;

	params = &priv->queue_param[queue];

	cw_min = fls(params->cw_min);
	cw_max = fls(params->cw_max);

	aifs = 10 + params->aifs * priv->slot_time;

	/* TODO: check if txop HW is in us (mult by 32) */
	txop = params->txop;

	ac_param = txop << AC_PARAM_TXOP_LIMIT_SHIFT |
		cw_max << AC_PARAM_ECW_MAX_SHIFT |
		cw_min << AC_PARAM_ECW_MIN_SHIFT |
		aifs << AC_PARAM_AIFS_SHIFT;

	switch (queue) {
	case IEEE80211_AC_BK:
		rtl818x_iowrite32(priv, &priv->map->AC_BK_PARAM, ac_param);
		break;
	case IEEE80211_AC_BE:
		rtl818x_iowrite32(priv, &priv->map->AC_BE_PARAM, ac_param);
		break;
	case IEEE80211_AC_VI:
		rtl818x_iowrite32(priv, &priv->map->AC_VI_PARAM, ac_param);
		break;
	case IEEE80211_AC_VO:
		rtl818x_iowrite32(priv, &priv->map->AC_VO_PARAM, ac_param);
		break;
	}
}

static int rtl8180_conf_tx(struct ieee80211_hw *dev,
			    struct ieee80211_vif *vif, u16 queue,
			    const struct ieee80211_tx_queue_params *params)
{
	struct rtl8180_priv *priv = dev->priv;
	u8 cw_min, cw_max;

	/* nothing to do ? */
	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8180)
		return 0;

	cw_min = fls(params->cw_min);
	cw_max = fls(params->cw_max);

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE) {
		priv->queue_param[queue] = *params;
		rtl8187se_conf_ac_parm(dev, queue);
	} else
		rtl818x_iowrite8(priv, &priv->map->CW_VAL,
				 (cw_max << 4) | cw_min);
	return 0;
}

static void rtl8180_conf_erp(struct ieee80211_hw *dev,
			    struct ieee80211_bss_conf *info)
{
	struct rtl8180_priv *priv = dev->priv;
	u8 sifs, difs;
	int eifs;
	u8 hw_eifs;

	/* TODO: should we do something ? */
	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8180)
		return;

	/* I _hope_ this means 10uS for the HW.
	 * In reference code it is 0x22 for
	 * both rtl8187L and rtl8187SE
	 */
	sifs = 0x22;

	if (info->use_short_slot)
		priv->slot_time = 9;
	else
		priv->slot_time = 20;

	/* 10 is SIFS time in uS */
	difs = 10 + 2 * priv->slot_time;
	eifs = 10 + difs + priv->ack_time;

	/* HW should use 4uS units for EIFS (I'm sure for rtl8185)*/
	hw_eifs = DIV_ROUND_UP(eifs, 4);


	rtl818x_iowrite8(priv, &priv->map->SLOT, priv->slot_time);
	rtl818x_iowrite8(priv, &priv->map->SIFS, sifs);
	rtl818x_iowrite8(priv, &priv->map->DIFS, difs);

	/* from reference code. set ack timeout reg = eifs reg */
	rtl818x_iowrite8(priv, &priv->map->CARRIER_SENSE_COUNTER, hw_eifs);

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE)
		rtl818x_iowrite8(priv, &priv->map->EIFS_8187SE, hw_eifs);
	else if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8185) {
		/* rtl8187/rtl8185 HW bug. After EIFS is elapsed,
		 * the HW still wait for DIFS.
		 * HW uses 4uS units for EIFS.
		 */
		hw_eifs = DIV_ROUND_UP(eifs - difs, 4);

		rtl818x_iowrite8(priv, &priv->map->EIFS, hw_eifs);
	}
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
		rtl818x_iowrite16(priv, (__le16 __iomem *)&priv->map->BSSID[0],
				  le16_to_cpu(*(__le16 *)info->bssid));
		rtl818x_iowrite32(priv, (__le32 __iomem *)&priv->map->BSSID[2],
				  le32_to_cpu(*(__le32 *)(info->bssid + 2)));

		if (is_valid_ether_addr(info->bssid)) {
			if (vif->type == NL80211_IFTYPE_ADHOC)
				reg = RTL818X_MSR_ADHOC;
			else
				reg = RTL818X_MSR_INFRA;
		} else
			reg = RTL818X_MSR_NO_LINK;

		if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE)
			reg |= RTL818X_MSR_ENEDCA;

		rtl818x_iowrite8(priv, &priv->map->MSR, reg);
	}

	if (changed & BSS_CHANGED_BASIC_RATES)
		rtl8180_conf_basic_rates(dev, info->basic_rates);

	if (changed & (BSS_CHANGED_ERP_SLOT | BSS_CHANGED_ERP_PREAMBLE)) {

		/* when preamble changes, acktime duration changes, and erp must
		 * be recalculated. ACK time is calculated at lowest rate.
		 * Since mac80211 include SIFS time we remove it (-10)
		 */
		priv->ack_time =
			le16_to_cpu(ieee80211_generic_frame_duration(dev,
					priv->vif,
					NL80211_BAND_2GHZ, 10,
					&priv->rates[0])) - 10;

		rtl8180_conf_erp(dev, info);

		/* mac80211 supplies aifs_n to driver and calls
		 * conf_tx callback whether aifs_n changes, NOT
		 * when aifs changes.
		 * Aifs should be recalculated if slot changes.
		 */
		if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE) {
			for (i = 0; i < 4; i++)
				rtl8187se_conf_ac_parm(dev, i);
		}
	}

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
	.conf_tx		= rtl8180_conf_tx,
	.prepare_multicast	= rtl8180_prepare_multicast,
	.configure_filter	= rtl8180_configure_filter,
	.get_tsf		= rtl8180_get_tsf,
};

static void rtl8180_eeprom_register_read(struct eeprom_93cx6 *eeprom)
{
	struct rtl8180_priv *priv = eeprom->data;
	u8 reg = rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);

	eeprom->reg_data_in = reg & RTL818X_EEPROM_CMD_WRITE;
	eeprom->reg_data_out = reg & RTL818X_EEPROM_CMD_READ;
	eeprom->reg_data_clock = reg & RTL818X_EEPROM_CMD_CK;
	eeprom->reg_chip_select = reg & RTL818X_EEPROM_CMD_CS;
}

static void rtl8180_eeprom_register_write(struct eeprom_93cx6 *eeprom)
{
	struct rtl8180_priv *priv = eeprom->data;
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

static void rtl8180_eeprom_read(struct rtl8180_priv *priv)
{
	struct eeprom_93cx6 eeprom;
	int eeprom_cck_table_adr;
	u16 eeprom_val;
	int i;

	eeprom.data = priv;
	eeprom.register_read = rtl8180_eeprom_register_read;
	eeprom.register_write = rtl8180_eeprom_register_write;
	if (rtl818x_ioread32(priv, &priv->map->RX_CONF) & (1 << 6))
		eeprom.width = PCI_EEPROM_WIDTH_93C66;
	else
		eeprom.width = PCI_EEPROM_WIDTH_93C46;

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD,
			RTL818X_EEPROM_CMD_PROGRAM);
	rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);
	udelay(10);

	eeprom_93cx6_read(&eeprom, 0x06, &eeprom_val);
	eeprom_val &= 0xFF;
	priv->rf_type = eeprom_val;

	eeprom_93cx6_read(&eeprom, 0x17, &eeprom_val);
	priv->csthreshold = eeprom_val >> 8;

	eeprom_93cx6_multiread(&eeprom, 0x7, (__le16 *)priv->mac_addr, 3);

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE)
		eeprom_cck_table_adr = 0x30;
	else
		eeprom_cck_table_adr = 0x10;

	/* CCK TX power */
	for (i = 0; i < 14; i += 2) {
		u16 txpwr;
		eeprom_93cx6_read(&eeprom, eeprom_cck_table_adr + (i >> 1),
				&txpwr);
		priv->channels[i].hw_value = txpwr & 0xFF;
		priv->channels[i + 1].hw_value = txpwr >> 8;
	}

	/* OFDM TX power */
	if (priv->chip_family != RTL818X_CHIP_FAMILY_RTL8180) {
		for (i = 0; i < 14; i += 2) {
			u16 txpwr;
			eeprom_93cx6_read(&eeprom, 0x20 + (i >> 1), &txpwr);
			priv->channels[i].hw_value |= (txpwr & 0xFF) << 8;
			priv->channels[i + 1].hw_value |= txpwr & 0xFF00;
		}
	}

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8180) {
		__le32 anaparam;
		eeprom_93cx6_multiread(&eeprom, 0xD, (__le16 *)&anaparam, 2);
		priv->anaparam = le32_to_cpu(anaparam);
		eeprom_93cx6_read(&eeprom, 0x19, &priv->rfparam);
	}

	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE) {
		eeprom_93cx6_read(&eeprom, 0x3F, &eeprom_val);
		priv->antenna_diversity_en = !!(eeprom_val & 0x100);
		priv->antenna_diversity_default = (eeprom_val & 0xC00) == 0x400;

		eeprom_93cx6_read(&eeprom, 0x7C, &eeprom_val);
		priv->xtal_out = eeprom_val & 0xF;
		priv->xtal_in = (eeprom_val & 0xF0) >> 4;
		priv->xtal_cal = !!(eeprom_val & 0x1000);
		priv->thermal_meter_val = (eeprom_val & 0xF00) >> 8;
		priv->thermal_meter_en = !!(eeprom_val & 0x2000);
	}

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD,
			RTL818X_EEPROM_CMD_NORMAL);
}

static int rtl8180_probe(struct pci_dev *pdev,
				   const struct pci_device_id *id)
{
	struct ieee80211_hw *dev;
	struct rtl8180_priv *priv;
	unsigned long mem_addr, mem_len;
	unsigned int io_addr, io_len;
	int err;
	const char *chip_name, *rf_name = NULL;
	u32 reg;

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
		goto err_disable_dev;
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

	dev->max_rates = 1;
	SET_IEEE80211_DEV(dev, &pdev->dev);
	pci_set_drvdata(pdev, dev);

	priv->map_pio = false;
	priv->map = pci_iomap(pdev, 1, mem_len);
	if (!priv->map) {
		priv->map = pci_iomap(pdev, 0, io_len);
		priv->map_pio = true;
	}

	if (!priv->map) {
		dev_err(&pdev->dev, "Cannot map device memory/PIO\n");
		err = -ENOMEM;
		goto err_free_dev;
	}

	BUILD_BUG_ON(sizeof(priv->channels) != sizeof(rtl818x_channels));
	BUILD_BUG_ON(sizeof(priv->rates) != sizeof(rtl818x_rates));

	memcpy(priv->channels, rtl818x_channels, sizeof(rtl818x_channels));
	memcpy(priv->rates, rtl818x_rates, sizeof(rtl818x_rates));

	priv->band.band = NL80211_BAND_2GHZ;
	priv->band.channels = priv->channels;
	priv->band.n_channels = ARRAY_SIZE(rtl818x_channels);
	priv->band.bitrates = priv->rates;
	priv->band.n_bitrates = 4;
	dev->wiphy->bands[NL80211_BAND_2GHZ] = &priv->band;

	ieee80211_hw_set(dev, HOST_BROADCAST_PS_BUFFERING);
	ieee80211_hw_set(dev, RX_INCLUDES_FCS);

	dev->vif_data_size = sizeof(struct rtl8180_vif);
	dev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_ADHOC);
	dev->max_signal = 65;

	reg = rtl818x_ioread32(priv, &priv->map->TX_CONF);
	reg &= RTL818X_TX_CONF_HWVER_MASK;
	switch (reg) {
	case RTL818X_TX_CONF_R8180_ABCD:
		chip_name = "RTL8180";
		priv->chip_family = RTL818X_CHIP_FAMILY_RTL8180;
		break;

	case RTL818X_TX_CONF_R8180_F:
		chip_name = "RTL8180vF";
		priv->chip_family = RTL818X_CHIP_FAMILY_RTL8180;
		break;

	case RTL818X_TX_CONF_R8185_ABC:
		chip_name = "RTL8185";
		priv->chip_family = RTL818X_CHIP_FAMILY_RTL8185;
		break;

	case RTL818X_TX_CONF_R8185_D:
		chip_name = "RTL8185vD";
		priv->chip_family = RTL818X_CHIP_FAMILY_RTL8185;
		break;

	case RTL818X_TX_CONF_RTL8187SE:
		chip_name = "RTL8187SE";
		if (priv->map_pio) {
			dev_err(&pdev->dev,
				"MMIO failed. PIO not supported on RTL8187SE\n");
			err = -ENOMEM;
			goto err_iounmap;
		}
		priv->chip_family = RTL818X_CHIP_FAMILY_RTL8187SE;
		break;

	default:
		printk(KERN_ERR "%s (rtl8180): Unknown chip! (0x%x)\n",
		       pci_name(pdev), reg >> 25);
		err = -ENODEV;
		goto err_iounmap;
	}

	/* we declare to MAC80211 all the queues except for beacon queue
	 * that will be eventually handled by DRV.
	 * TX rings are arranged in such a way that lower is the IDX,
	 * higher is the priority, in order to achieve direct mapping
	 * with mac80211, however the beacon queue is an exception and it
	 * is mapped on the highst tx ring IDX.
	 */
	if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE)
		dev->queues = RTL8187SE_NR_TX_QUEUES - 1;
	else
		dev->queues = RTL8180_NR_TX_QUEUES - 1;

	if (priv->chip_family != RTL818X_CHIP_FAMILY_RTL8180) {
		priv->band.n_bitrates = ARRAY_SIZE(rtl818x_rates);
		pci_try_set_mwi(pdev);
	}

	if (priv->chip_family != RTL818X_CHIP_FAMILY_RTL8180)
		ieee80211_hw_set(dev, SIGNAL_DBM);
	else
		ieee80211_hw_set(dev, SIGNAL_UNSPEC);

	wiphy_ext_feature_set(dev->wiphy, NL80211_EXT_FEATURE_CQM_RSSI_LIST);

	rtl8180_eeprom_read(priv);

	switch (priv->rf_type) {
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
	case 9:
		if (priv->chip_family == RTL818X_CHIP_FAMILY_RTL8187SE)
			priv->rf = rtl8187se_detect_rf(dev);
		else
			priv->rf = rtl8180_detect_rf(dev);
		break;
	case 10:
		rf_name = "RTL8255";
		break;
	default:
		printk(KERN_ERR "%s (rtl8180): Unknown RF! (0x%x)\n",
		       pci_name(pdev), priv->rf_type);
		err = -ENODEV;
		goto err_iounmap;
	}

	if (!priv->rf) {
		printk(KERN_ERR "%s (rtl8180): %s RF frontend not supported!\n",
		       pci_name(pdev), rf_name);
		err = -ENODEV;
		goto err_iounmap;
	}

	if (!is_valid_ether_addr(priv->mac_addr)) {
		printk(KERN_WARNING "%s (rtl8180): Invalid hwaddr! Using"
		       " randomly generated MAC addr\n", pci_name(pdev));
		eth_random_addr(priv->mac_addr);
	}
	SET_IEEE80211_PERM_ADDR(dev, priv->mac_addr);

	spin_lock_init(&priv->lock);

	err = ieee80211_register_hw(dev);
	if (err) {
		printk(KERN_ERR "%s (rtl8180): Cannot register device\n",
		       pci_name(pdev));
		goto err_iounmap;
	}

	wiphy_info(dev->wiphy, "hwaddr %pm, %s + %s\n",
		   priv->mac_addr, chip_name, priv->rf->name);

	return 0;

 err_iounmap:
	pci_iounmap(pdev, priv->map);

 err_free_dev:
	ieee80211_free_hw(dev);

 err_free_reg:
	pci_release_regions(pdev);

 err_disable_dev:
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
