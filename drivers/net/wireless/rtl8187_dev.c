/*
 * Linux device driver for RTL8187
 *
 * Copyright 2007 Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007 Andrea Merello <andreamrl@tiscali.it>
 *
 * Based on the r8187 driver, which is:
 * Copyright 2005 Andrea Merello <andreamrl@tiscali.it>, et al.
 *
 * Magic delays and register offsets below are taken from the original
 * r8187 driver sources.  Thanks to Realtek for their support!
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/usb.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/eeprom_93cx6.h>
#include <net/mac80211.h>

#include "rtl8187.h"
#include "rtl8187_rtl8225.h"

MODULE_AUTHOR("Michael Wu <flamingice@sourmilk.net>");
MODULE_AUTHOR("Andrea Merello <andreamrl@tiscali.it>");
MODULE_DESCRIPTION("RTL8187 USB wireless driver");
MODULE_LICENSE("GPL");

static struct usb_device_id rtl8187_table[] __devinitdata = {
	/* Realtek */
	{USB_DEVICE(0x0bda, 0x8187)},
	/* Netgear */
	{USB_DEVICE(0x0846, 0x6100)},
	{USB_DEVICE(0x0846, 0x6a00)},
	/* HP */
	{USB_DEVICE(0x03f0, 0xca02)},
	/* Sitecom */
	{USB_DEVICE(0x0df6, 0x000d)},
	{}
};

MODULE_DEVICE_TABLE(usb, rtl8187_table);

static void rtl8187_iowrite_async_cb(struct urb *urb)
{
	kfree(urb->context);
	usb_free_urb(urb);
}

static void rtl8187_iowrite_async(struct rtl8187_priv *priv, __le16 addr,
				  void *data, u16 len)
{
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	struct rtl8187_async_write_data {
		u8 data[4];
		struct usb_ctrlrequest dr;
	} *buf;

	buf = kmalloc(sizeof(*buf), GFP_ATOMIC);
	if (!buf)
		return;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		kfree(buf);
		return;
	}

	dr = &buf->dr;

	dr->bRequestType = RTL8187_REQT_WRITE;
	dr->bRequest = RTL8187_REQ_SET_REG;
	dr->wValue = addr;
	dr->wIndex = 0;
	dr->wLength = cpu_to_le16(len);

	memcpy(buf, data, len);

	usb_fill_control_urb(urb, priv->udev, usb_sndctrlpipe(priv->udev, 0),
			     (unsigned char *)dr, buf, len,
			     rtl8187_iowrite_async_cb, buf);
	usb_submit_urb(urb, GFP_ATOMIC);
}

static inline void rtl818x_iowrite32_async(struct rtl8187_priv *priv,
					   __le32 *addr, u32 val)
{
	__le32 buf = cpu_to_le32(val);

	rtl8187_iowrite_async(priv, cpu_to_le16((unsigned long)addr),
			      &buf, sizeof(buf));
}

void rtl8187_write_phy(struct ieee80211_hw *dev, u8 addr, u32 data)
{
	struct rtl8187_priv *priv = dev->priv;

	data <<= 8;
	data |= addr | 0x80;

	rtl818x_iowrite8(priv, &priv->map->PHY[3], (data >> 24) & 0xFF);
	rtl818x_iowrite8(priv, &priv->map->PHY[2], (data >> 16) & 0xFF);
	rtl818x_iowrite8(priv, &priv->map->PHY[1], (data >> 8) & 0xFF);
	rtl818x_iowrite8(priv, &priv->map->PHY[0], data & 0xFF);

	msleep(1);
}

static void rtl8187_tx_cb(struct urb *urb)
{
	struct ieee80211_tx_status status = { {0} };
	struct sk_buff *skb = (struct sk_buff *)urb->context;
	struct rtl8187_tx_info *info = (struct rtl8187_tx_info *)skb->cb;

	usb_free_urb(info->urb);
	if (info->control)
		memcpy(&status.control, info->control, sizeof(status.control));
	kfree(info->control);
	skb_pull(skb, sizeof(struct rtl8187_tx_hdr));
	status.flags |= IEEE80211_TX_STATUS_ACK;
	ieee80211_tx_status_irqsafe(info->dev, skb, &status);
}

static int rtl8187_tx(struct ieee80211_hw *dev, struct sk_buff *skb,
		      struct ieee80211_tx_control *control)
{
	struct rtl8187_priv *priv = dev->priv;
	struct rtl8187_tx_hdr *hdr;
	struct rtl8187_tx_info *info;
	struct urb *urb;
	__le16 rts_dur = 0;
	u32 flags;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		kfree_skb(skb);
		return 0;
	}

	flags = skb->len;
	flags |= RTL8187_TX_FLAG_NO_ENCRYPT;
	flags |= control->rts_cts_rate << 19;
	flags |= control->tx_rate << 24;
	if (ieee80211_get_morefrag((struct ieee80211_hdr *)skb->data))
		flags |= RTL8187_TX_FLAG_MORE_FRAG;
	if (control->flags & IEEE80211_TXCTL_USE_RTS_CTS) {
		flags |= RTL8187_TX_FLAG_RTS;
		rts_dur = ieee80211_rts_duration(dev, priv->vif,
						 skb->len, control);
	}
	if (control->flags & IEEE80211_TXCTL_USE_CTS_PROTECT)
		flags |= RTL8187_TX_FLAG_CTS;

	hdr = (struct rtl8187_tx_hdr *)skb_push(skb, sizeof(*hdr));
	hdr->flags = cpu_to_le32(flags);
	hdr->len = 0;
	hdr->rts_duration = rts_dur;
	hdr->retry = cpu_to_le32(control->retry_limit << 8);

	info = (struct rtl8187_tx_info *)skb->cb;
	info->control = kmemdup(control, sizeof(*control), GFP_ATOMIC);
	info->urb = urb;
	info->dev = dev;
	usb_fill_bulk_urb(urb, priv->udev, usb_sndbulkpipe(priv->udev, 2),
			  hdr, skb->len, rtl8187_tx_cb, skb);
	usb_submit_urb(urb, GFP_ATOMIC);

	return 0;
}

static void rtl8187_rx_cb(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *)urb->context;
	struct rtl8187_rx_info *info = (struct rtl8187_rx_info *)skb->cb;
	struct ieee80211_hw *dev = info->dev;
	struct rtl8187_priv *priv = dev->priv;
	struct rtl8187_rx_hdr *hdr;
	struct ieee80211_rx_status rx_status = { 0 };
	int rate, signal;
	u32 flags;

	spin_lock(&priv->rx_queue.lock);
	if (skb->next)
		__skb_unlink(skb, &priv->rx_queue);
	else {
		spin_unlock(&priv->rx_queue.lock);
		return;
	}
	spin_unlock(&priv->rx_queue.lock);

	if (unlikely(urb->status)) {
		usb_free_urb(urb);
		dev_kfree_skb_irq(skb);
		return;
	}

	skb_put(skb, urb->actual_length);
	hdr = (struct rtl8187_rx_hdr *)(skb_tail_pointer(skb) - sizeof(*hdr));
	flags = le32_to_cpu(hdr->flags);
	skb_trim(skb, flags & 0x0FFF);

	signal = hdr->agc >> 1;
	rate = (flags >> 20) & 0xF;
	if (rate > 3) {	/* OFDM rate */
		if (signal > 90)
			signal = 90;
		else if (signal < 25)
			signal = 25;
		signal = 90 - signal;
	} else {	/* CCK rate */
		if (signal > 95)
			signal = 95;
		else if (signal < 30)
			signal = 30;
		signal = 95 - signal;
	}

	rx_status.antenna = (hdr->signal >> 7) & 1;
	rx_status.signal = 64 - min(hdr->noise, (u8)64);
	rx_status.ssi = signal;
	rx_status.rate = rate;
	rx_status.freq = dev->conf.freq;
	rx_status.channel = dev->conf.channel;
	rx_status.phymode = dev->conf.phymode;
	rx_status.mactime = le64_to_cpu(hdr->mac_time);
	rx_status.flag |= RX_FLAG_TSFT;
	if (flags & (1 << 13))
		rx_status.flag |= RX_FLAG_FAILED_FCS_CRC;
	ieee80211_rx_irqsafe(dev, skb, &rx_status);

	skb = dev_alloc_skb(RTL8187_MAX_RX);
	if (unlikely(!skb)) {
		usb_free_urb(urb);
		/* TODO check rx queue length and refill *somewhere* */
		return;
	}

	info = (struct rtl8187_rx_info *)skb->cb;
	info->urb = urb;
	info->dev = dev;
	urb->transfer_buffer = skb_tail_pointer(skb);
	urb->context = skb;
	skb_queue_tail(&priv->rx_queue, skb);

	usb_submit_urb(urb, GFP_ATOMIC);
}

static int rtl8187_init_urbs(struct ieee80211_hw *dev)
{
	struct rtl8187_priv *priv = dev->priv;
	struct urb *entry;
	struct sk_buff *skb;
	struct rtl8187_rx_info *info;

	while (skb_queue_len(&priv->rx_queue) < 8) {
		skb = __dev_alloc_skb(RTL8187_MAX_RX, GFP_KERNEL);
		if (!skb)
			break;
		entry = usb_alloc_urb(0, GFP_KERNEL);
		if (!entry) {
			kfree_skb(skb);
			break;
		}
		usb_fill_bulk_urb(entry, priv->udev,
				  usb_rcvbulkpipe(priv->udev, 1),
				  skb_tail_pointer(skb),
				  RTL8187_MAX_RX, rtl8187_rx_cb, skb);
		info = (struct rtl8187_rx_info *)skb->cb;
		info->urb = entry;
		info->dev = dev;
		skb_queue_tail(&priv->rx_queue, skb);
		usb_submit_urb(entry, GFP_KERNEL);
	}

	return 0;
}

static int rtl8187_init_hw(struct ieee80211_hw *dev)
{
	struct rtl8187_priv *priv = dev->priv;
	u8 reg;
	int i;

	/* reset */
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
	reg = rtl818x_ioread8(priv, &priv->map->CONFIG3);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3, reg | RTL818X_CONFIG3_ANAPARAM_WRITE);
	rtl818x_iowrite32(priv, &priv->map->ANAPARAM, RTL8225_ANAPARAM_ON);
	rtl818x_iowrite32(priv, &priv->map->ANAPARAM2, RTL8225_ANAPARAM2_ON);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3, reg & ~RTL818X_CONFIG3_ANAPARAM_WRITE);
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);

	rtl818x_iowrite16(priv, &priv->map->INT_MASK, 0);

	msleep(200);
	rtl818x_iowrite8(priv, (u8 *)0xFE18, 0x10);
	rtl818x_iowrite8(priv, (u8 *)0xFE18, 0x11);
	rtl818x_iowrite8(priv, (u8 *)0xFE18, 0x00);
	msleep(200);

	reg = rtl818x_ioread8(priv, &priv->map->CMD);
	reg &= (1 << 1);
	reg |= RTL818X_CMD_RESET;
	rtl818x_iowrite8(priv, &priv->map->CMD, reg);

	i = 10;
	do {
		msleep(2);
		if (!(rtl818x_ioread8(priv, &priv->map->CMD) &
		      RTL818X_CMD_RESET))
			break;
	} while (--i);

	if (!i) {
		printk(KERN_ERR "%s: Reset timeout!\n", wiphy_name(dev->wiphy));
		return -ETIMEDOUT;
	}

	/* reload registers from eeprom */
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_LOAD);

	i = 10;
	do {
		msleep(4);
		if (!(rtl818x_ioread8(priv, &priv->map->EEPROM_CMD) &
		      RTL818X_EEPROM_CMD_CONFIG))
			break;
	} while (--i);

	if (!i) {
		printk(KERN_ERR "%s: eeprom reset timeout!\n",
		       wiphy_name(dev->wiphy));
		return -ETIMEDOUT;
	}

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
	reg = rtl818x_ioread8(priv, &priv->map->CONFIG3);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3, reg | RTL818X_CONFIG3_ANAPARAM_WRITE);
	rtl818x_iowrite32(priv, &priv->map->ANAPARAM, RTL8225_ANAPARAM_ON);
	rtl818x_iowrite32(priv, &priv->map->ANAPARAM2, RTL8225_ANAPARAM2_ON);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3, reg & ~RTL818X_CONFIG3_ANAPARAM_WRITE);
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);

	/* setup card */
	rtl818x_iowrite16(priv, &priv->map->RFPinsSelect, 0);
	rtl818x_iowrite8(priv, &priv->map->GPIO, 0);

	rtl818x_iowrite16(priv, &priv->map->RFPinsSelect, (4 << 8));
	rtl818x_iowrite8(priv, &priv->map->GPIO, 1);
	rtl818x_iowrite8(priv, &priv->map->GP_ENABLE, 0);

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);

	rtl818x_iowrite16(priv, (__le16 *)0xFFF4, 0xFFFF);
	reg = rtl818x_ioread8(priv, &priv->map->CONFIG1);
	reg &= 0x3F;
	reg |= 0x80;
	rtl818x_iowrite8(priv, &priv->map->CONFIG1, reg);

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);

	rtl818x_iowrite32(priv, &priv->map->INT_TIMEOUT, 0);
	rtl818x_iowrite8(priv, &priv->map->WPA_CONF, 0);
	rtl818x_iowrite8(priv, &priv->map->RATE_FALLBACK, 0x81);

	// TODO: set RESP_RATE and BRSR properly
	rtl818x_iowrite8(priv, &priv->map->RESP_RATE, (8 << 4) | 0);
	rtl818x_iowrite16(priv, &priv->map->BRSR, 0x01F3);

	/* host_usb_init */
	rtl818x_iowrite16(priv, &priv->map->RFPinsSelect, 0);
	rtl818x_iowrite8(priv, &priv->map->GPIO, 0);
	reg = rtl818x_ioread8(priv, (u8 *)0xFE53);
	rtl818x_iowrite8(priv, (u8 *)0xFE53, reg | (1 << 7));
	rtl818x_iowrite16(priv, &priv->map->RFPinsSelect, (4 << 8));
	rtl818x_iowrite8(priv, &priv->map->GPIO, 0x20);
	rtl818x_iowrite8(priv, &priv->map->GP_ENABLE, 0);
	rtl818x_iowrite16(priv, &priv->map->RFPinsOutput, 0x80);
	rtl818x_iowrite16(priv, &priv->map->RFPinsSelect, 0x80);
	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, 0x80);
	msleep(100);

	rtl818x_iowrite32(priv, &priv->map->RF_TIMING, 0x000a8008);
	rtl818x_iowrite16(priv, &priv->map->BRSR, 0xFFFF);
	rtl818x_iowrite32(priv, &priv->map->RF_PARA, 0x00100044);
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
	rtl818x_iowrite8(priv, &priv->map->CONFIG3, 0x44);
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);
	rtl818x_iowrite16(priv, &priv->map->RFPinsEnable, 0x1FF7);
	msleep(100);

	priv->rf->init(dev);

	rtl818x_iowrite16(priv, &priv->map->BRSR, 0x01F3);
	reg = rtl818x_ioread8(priv, &priv->map->PGSELECT) & ~1;
	rtl818x_iowrite8(priv, &priv->map->PGSELECT, reg | 1);
	rtl818x_iowrite16(priv, (__le16 *)0xFFFE, 0x10);
	rtl818x_iowrite8(priv, &priv->map->TALLY_SEL, 0x80);
	rtl818x_iowrite8(priv, (u8 *)0xFFFF, 0x60);
	rtl818x_iowrite8(priv, &priv->map->PGSELECT, reg);

	return 0;
}

static int rtl8187_start(struct ieee80211_hw *dev)
{
	struct rtl8187_priv *priv = dev->priv;
	u32 reg;
	int ret;

	ret = rtl8187_init_hw(dev);
	if (ret)
		return ret;

	rtl818x_iowrite16(priv, &priv->map->INT_MASK, 0xFFFF);

	rtl818x_iowrite32(priv, &priv->map->MAR[0], ~0);
	rtl818x_iowrite32(priv, &priv->map->MAR[1], ~0);

	rtl8187_init_urbs(dev);

	reg = RTL818X_RX_CONF_ONLYERLPKT |
	      RTL818X_RX_CONF_RX_AUTORESETPHY |
	      RTL818X_RX_CONF_BSSID |
	      RTL818X_RX_CONF_MGMT |
	      RTL818X_RX_CONF_DATA |
	      (7 << 13 /* RX FIFO threshold NONE */) |
	      (7 << 10 /* MAX RX DMA */) |
	      RTL818X_RX_CONF_BROADCAST |
	      RTL818X_RX_CONF_NICMAC;

	priv->rx_conf = reg;
	rtl818x_iowrite32(priv, &priv->map->RX_CONF, reg);

	reg = rtl818x_ioread8(priv, &priv->map->CW_CONF);
	reg &= ~RTL818X_CW_CONF_PERPACKET_CW_SHIFT;
	reg |= RTL818X_CW_CONF_PERPACKET_RETRY_SHIFT;
	rtl818x_iowrite8(priv, &priv->map->CW_CONF, reg);

	reg = rtl818x_ioread8(priv, &priv->map->TX_AGC_CTL);
	reg &= ~RTL818X_TX_AGC_CTL_PERPACKET_GAIN_SHIFT;
	reg &= ~RTL818X_TX_AGC_CTL_PERPACKET_ANTSEL_SHIFT;
	reg &= ~RTL818X_TX_AGC_CTL_FEEDBACK_ANT;
	rtl818x_iowrite8(priv, &priv->map->TX_AGC_CTL, reg);

	reg  = RTL818X_TX_CONF_CW_MIN |
	       (7 << 21 /* MAX TX DMA */) |
	       RTL818X_TX_CONF_NO_ICV;
	rtl818x_iowrite32(priv, &priv->map->TX_CONF, reg);

	reg = rtl818x_ioread8(priv, &priv->map->CMD);
	reg |= RTL818X_CMD_TX_ENABLE;
	reg |= RTL818X_CMD_RX_ENABLE;
	rtl818x_iowrite8(priv, &priv->map->CMD, reg);

	return 0;
}

static void rtl8187_stop(struct ieee80211_hw *dev)
{
	struct rtl8187_priv *priv = dev->priv;
	struct rtl8187_rx_info *info;
	struct sk_buff *skb;
	u32 reg;

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

	while ((skb = skb_dequeue(&priv->rx_queue))) {
		info = (struct rtl8187_rx_info *)skb->cb;
		usb_kill_urb(info->urb);
		kfree_skb(skb);
	}
	return;
}

static int rtl8187_add_interface(struct ieee80211_hw *dev,
				 struct ieee80211_if_init_conf *conf)
{
	struct rtl8187_priv *priv = dev->priv;
	int i;

	if (priv->mode != IEEE80211_IF_TYPE_MNTR)
		return -EOPNOTSUPP;

	switch (conf->type) {
	case IEEE80211_IF_TYPE_STA:
		priv->mode = conf->type;
		break;
	default:
		return -EOPNOTSUPP;
	}

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
	for (i = 0; i < ETH_ALEN; i++)
		rtl818x_iowrite8(priv, &priv->map->MAC[i],
				 ((u8 *)conf->mac_addr)[i]);
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);

	return 0;
}

static void rtl8187_remove_interface(struct ieee80211_hw *dev,
				     struct ieee80211_if_init_conf *conf)
{
	struct rtl8187_priv *priv = dev->priv;
	priv->mode = IEEE80211_IF_TYPE_MNTR;
}

static int rtl8187_config(struct ieee80211_hw *dev, struct ieee80211_conf *conf)
{
	struct rtl8187_priv *priv = dev->priv;
	u32 reg;

	reg = rtl818x_ioread32(priv, &priv->map->TX_CONF);
	/* Enable TX loopback on MAC level to avoid TX during channel
	 * changes, as this has be seen to causes problems and the
	 * card will stop work until next reset
	 */
	rtl818x_iowrite32(priv, &priv->map->TX_CONF,
			  reg | RTL818X_TX_CONF_LOOPBACK_MAC);
	msleep(10);
	priv->rf->set_chan(dev, conf);
	msleep(10);
	rtl818x_iowrite32(priv, &priv->map->TX_CONF, reg);

	rtl818x_iowrite8(priv, &priv->map->SIFS, 0x22);

	if (conf->flags & IEEE80211_CONF_SHORT_SLOT_TIME) {
		rtl818x_iowrite8(priv, &priv->map->SLOT, 0x9);
		rtl818x_iowrite8(priv, &priv->map->DIFS, 0x14);
		rtl818x_iowrite8(priv, &priv->map->EIFS, 91 - 0x14);
		rtl818x_iowrite8(priv, &priv->map->CW_VAL, 0x73);
	} else {
		rtl818x_iowrite8(priv, &priv->map->SLOT, 0x14);
		rtl818x_iowrite8(priv, &priv->map->DIFS, 0x24);
		rtl818x_iowrite8(priv, &priv->map->EIFS, 91 - 0x24);
		rtl818x_iowrite8(priv, &priv->map->CW_VAL, 0xa5);
	}

	rtl818x_iowrite16(priv, &priv->map->ATIM_WND, 2);
	rtl818x_iowrite16(priv, &priv->map->ATIMTR_INTERVAL, 100);
	rtl818x_iowrite16(priv, &priv->map->BEACON_INTERVAL, 100);
	rtl818x_iowrite16(priv, &priv->map->BEACON_INTERVAL_TIME, 100);
	return 0;
}

static int rtl8187_config_interface(struct ieee80211_hw *dev,
				    struct ieee80211_vif *vif,
				    struct ieee80211_if_conf *conf)
{
	struct rtl8187_priv *priv = dev->priv;
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		rtl818x_iowrite8(priv, &priv->map->BSSID[i], conf->bssid[i]);

	if (is_valid_ether_addr(conf->bssid))
		rtl818x_iowrite8(priv, &priv->map->MSR, RTL818X_MSR_INFRA);
	else
		rtl818x_iowrite8(priv, &priv->map->MSR, RTL818X_MSR_NO_LINK);

	return 0;
}

static void rtl8187_configure_filter(struct ieee80211_hw *dev,
				     unsigned int changed_flags,
				     unsigned int *total_flags,
				     int mc_count, struct dev_addr_list *mclist)
{
	struct rtl8187_priv *priv = dev->priv;

	if (changed_flags & FIF_FCSFAIL)
		priv->rx_conf ^= RTL818X_RX_CONF_FCS;
	if (changed_flags & FIF_CONTROL)
		priv->rx_conf ^= RTL818X_RX_CONF_CTRL;
	if (changed_flags & FIF_OTHER_BSS)
		priv->rx_conf ^= RTL818X_RX_CONF_MONITOR;
	if (*total_flags & FIF_ALLMULTI || mc_count > 0)
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

	rtl818x_iowrite32_async(priv, &priv->map->RX_CONF, priv->rx_conf);
}

static const struct ieee80211_ops rtl8187_ops = {
	.tx			= rtl8187_tx,
	.start			= rtl8187_start,
	.stop			= rtl8187_stop,
	.add_interface		= rtl8187_add_interface,
	.remove_interface	= rtl8187_remove_interface,
	.config			= rtl8187_config,
	.config_interface	= rtl8187_config_interface,
	.configure_filter	= rtl8187_configure_filter,
};

static void rtl8187_eeprom_register_read(struct eeprom_93cx6 *eeprom)
{
	struct ieee80211_hw *dev = eeprom->data;
	struct rtl8187_priv *priv = dev->priv;
	u8 reg = rtl818x_ioread8(priv, &priv->map->EEPROM_CMD);

	eeprom->reg_data_in = reg & RTL818X_EEPROM_CMD_WRITE;
	eeprom->reg_data_out = reg & RTL818X_EEPROM_CMD_READ;
	eeprom->reg_data_clock = reg & RTL818X_EEPROM_CMD_CK;
	eeprom->reg_chip_select = reg & RTL818X_EEPROM_CMD_CS;
}

static void rtl8187_eeprom_register_write(struct eeprom_93cx6 *eeprom)
{
	struct ieee80211_hw *dev = eeprom->data;
	struct rtl8187_priv *priv = dev->priv;
	u8 reg = RTL818X_EEPROM_CMD_PROGRAM;

	if (eeprom->reg_data_in)
		reg |= RTL818X_EEPROM_CMD_WRITE;
	if (eeprom->reg_data_out)
		reg |= RTL818X_EEPROM_CMD_READ;
	if (eeprom->reg_data_clock)
		reg |= RTL818X_EEPROM_CMD_CK;
	if (eeprom->reg_chip_select)
		reg |= RTL818X_EEPROM_CMD_CS;

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, reg);
	udelay(10);
}

static int __devinit rtl8187_probe(struct usb_interface *intf,
				   const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct ieee80211_hw *dev;
	struct rtl8187_priv *priv;
	struct eeprom_93cx6 eeprom;
	struct ieee80211_channel *channel;
	u16 txpwr, reg;
	int err, i;
	DECLARE_MAC_BUF(mac);

	dev = ieee80211_alloc_hw(sizeof(*priv), &rtl8187_ops);
	if (!dev) {
		printk(KERN_ERR "rtl8187: ieee80211 alloc failed\n");
		return -ENOMEM;
	}

	priv = dev->priv;

	SET_IEEE80211_DEV(dev, &intf->dev);
	usb_set_intfdata(intf, dev);
	priv->udev = udev;

	usb_get_dev(udev);

	skb_queue_head_init(&priv->rx_queue);
	memcpy(priv->channels, rtl818x_channels, sizeof(rtl818x_channels));
	memcpy(priv->rates, rtl818x_rates, sizeof(rtl818x_rates));
	priv->map = (struct rtl818x_csr *)0xFF00;
	priv->modes[0].mode = MODE_IEEE80211G;
	priv->modes[0].num_rates = ARRAY_SIZE(rtl818x_rates);
	priv->modes[0].rates = priv->rates;
	priv->modes[0].num_channels = ARRAY_SIZE(rtl818x_channels);
	priv->modes[0].channels = priv->channels;
	priv->modes[1].mode = MODE_IEEE80211B;
	priv->modes[1].num_rates = 4;
	priv->modes[1].rates = priv->rates;
	priv->modes[1].num_channels = ARRAY_SIZE(rtl818x_channels);
	priv->modes[1].channels = priv->channels;
	priv->mode = IEEE80211_IF_TYPE_MNTR;
	dev->flags = IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING |
		     IEEE80211_HW_RX_INCLUDES_FCS;
	dev->extra_tx_headroom = sizeof(struct rtl8187_tx_hdr);
	dev->queues = 1;
	dev->max_rssi = 65;
	dev->max_signal = 64;

	for (i = 0; i < 2; i++)
		if ((err = ieee80211_register_hwmode(dev, &priv->modes[i])))
			goto err_free_dev;

	eeprom.data = dev;
	eeprom.register_read = rtl8187_eeprom_register_read;
	eeprom.register_write = rtl8187_eeprom_register_write;
	if (rtl818x_ioread32(priv, &priv->map->RX_CONF) & (1 << 6))
		eeprom.width = PCI_EEPROM_WIDTH_93C66;
	else
		eeprom.width = PCI_EEPROM_WIDTH_93C46;

	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_CONFIG);
	udelay(10);

	eeprom_93cx6_multiread(&eeprom, RTL8187_EEPROM_MAC_ADDR,
			       (__le16 __force *)dev->wiphy->perm_addr, 3);
	if (!is_valid_ether_addr(dev->wiphy->perm_addr)) {
		printk(KERN_WARNING "rtl8187: Invalid hwaddr! Using randomly "
		       "generated MAC address\n");
		random_ether_addr(dev->wiphy->perm_addr);
	}

	channel = priv->channels;
	for (i = 0; i < 3; i++) {
		eeprom_93cx6_read(&eeprom, RTL8187_EEPROM_TXPWR_CHAN_1 + i,
				  &txpwr);
		(*channel++).val = txpwr & 0xFF;
		(*channel++).val = txpwr >> 8;
	}
	for (i = 0; i < 2; i++) {
		eeprom_93cx6_read(&eeprom, RTL8187_EEPROM_TXPWR_CHAN_4 + i,
				  &txpwr);
		(*channel++).val = txpwr & 0xFF;
		(*channel++).val = txpwr >> 8;
	}
	for (i = 0; i < 2; i++) {
		eeprom_93cx6_read(&eeprom, RTL8187_EEPROM_TXPWR_CHAN_6 + i,
				  &txpwr);
		(*channel++).val = txpwr & 0xFF;
		(*channel++).val = txpwr >> 8;
	}

	eeprom_93cx6_read(&eeprom, RTL8187_EEPROM_TXPWR_BASE,
			  &priv->txpwr_base);

	reg = rtl818x_ioread8(priv, &priv->map->PGSELECT) & ~1;
	rtl818x_iowrite8(priv, &priv->map->PGSELECT, reg | 1);
	/* 0 means asic B-cut, we should use SW 3 wire
	 * bit-by-bit banging for radio. 1 means we can use
	 * USB specific request to write radio registers */
	priv->asic_rev = rtl818x_ioread8(priv, (u8 *)0xFFFE) & 0x3;
	rtl818x_iowrite8(priv, &priv->map->PGSELECT, reg);
	rtl818x_iowrite8(priv, &priv->map->EEPROM_CMD, RTL818X_EEPROM_CMD_NORMAL);

	priv->rf = rtl8187_detect_rf(dev);

	err = ieee80211_register_hw(dev);
	if (err) {
		printk(KERN_ERR "rtl8187: Cannot register device\n");
		goto err_free_dev;
	}

	printk(KERN_INFO "%s: hwaddr %s, rtl8187 V%d + %s\n",
	       wiphy_name(dev->wiphy), print_mac(mac, dev->wiphy->perm_addr),
	       priv->asic_rev, priv->rf->name);

	return 0;

 err_free_dev:
	ieee80211_free_hw(dev);
	usb_set_intfdata(intf, NULL);
	usb_put_dev(udev);
	return err;
}

static void __devexit rtl8187_disconnect(struct usb_interface *intf)
{
	struct ieee80211_hw *dev = usb_get_intfdata(intf);
	struct rtl8187_priv *priv;

	if (!dev)
		return;

	ieee80211_unregister_hw(dev);

	priv = dev->priv;
	usb_put_dev(interface_to_usbdev(intf));
	ieee80211_free_hw(dev);
}

static struct usb_driver rtl8187_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= rtl8187_table,
	.probe		= rtl8187_probe,
	.disconnect	= rtl8187_disconnect,
};

static int __init rtl8187_init(void)
{
	return usb_register(&rtl8187_driver);
}

static void __exit rtl8187_exit(void)
{
	usb_deregister(&rtl8187_driver);
}

module_init(rtl8187_init);
module_exit(rtl8187_exit);
