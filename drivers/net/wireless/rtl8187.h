/*
 * Definitions for RTL8187 hardware
 *
 * Copyright 2007 Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007 Andrea Merello <andreamrl@tiscali.it>
 *
 * Based on the r8187 driver, which is:
 * Copyright 2005 Andrea Merello <andreamrl@tiscali.it>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RTL8187_H
#define RTL8187_H

#include "rtl818x.h"

#define RTL8187_EEPROM_TXPWR_BASE	0x05
#define RTL8187_EEPROM_MAC_ADDR		0x07
#define RTL8187_EEPROM_TXPWR_CHAN_1	0x16	/* 3 channels */
#define RTL8187_EEPROM_TXPWR_CHAN_6	0x1B	/* 2 channels */
#define RTL8187_EEPROM_TXPWR_CHAN_4	0x3D	/* 2 channels */

#define RTL8187_REQT_READ	0xC0
#define RTL8187_REQT_WRITE	0x40
#define RTL8187_REQ_GET_REG	0x05
#define RTL8187_REQ_SET_REG	0x05

#define RTL8187_MAX_RX		0x9C4

struct rtl8187_rx_info {
	struct urb *urb;
	struct ieee80211_hw *dev;
};

struct rtl8187_rx_hdr {
	__le32 flags;
	u8 noise;
	u8 signal;
	u8 agc;
	u8 reserved;
	__le64 mac_time;
} __attribute__((packed));

struct rtl8187_tx_info {
	struct ieee80211_tx_control *control;
	struct urb *urb;
	struct ieee80211_hw *dev;
};

struct rtl8187_tx_hdr {
	__le32 flags;
#define RTL8187_TX_FLAG_NO_ENCRYPT	(1 << 15)
#define RTL8187_TX_FLAG_MORE_FRAG	(1 << 17)
#define RTL8187_TX_FLAG_CTS		(1 << 18)
#define RTL8187_TX_FLAG_RTS		(1 << 23)
	__le16 rts_duration;
	__le16 len;
	__le32 retry;
} __attribute__((packed));

struct rtl8187_priv {
	/* common between rtl818x drivers */
	struct rtl818x_csr *map;
	const struct rtl818x_rf_ops *rf;
	struct ieee80211_vif *vif;
	int mode;

	/* rtl8187 specific */
	struct ieee80211_channel channels[14];
	struct ieee80211_rate rates[12];
	struct ieee80211_hw_mode modes[2];
	struct usb_device *udev;
	u32 rx_conf;
	u16 txpwr_base;
	u8 asic_rev;
	struct sk_buff_head rx_queue;
};

void rtl8187_write_phy(struct ieee80211_hw *dev, u8 addr, u32 data);

static inline u8 rtl818x_ioread8(struct rtl8187_priv *priv, u8 *addr)
{
	u8 val;

	usb_control_msg(priv->udev, usb_rcvctrlpipe(priv->udev, 0),
			RTL8187_REQ_GET_REG, RTL8187_REQT_READ,
			(unsigned long)addr, 0, &val, sizeof(val), HZ / 2);

	return val;
}

static inline u16 rtl818x_ioread16(struct rtl8187_priv *priv, __le16 *addr)
{
	__le16 val;

	usb_control_msg(priv->udev, usb_rcvctrlpipe(priv->udev, 0),
			RTL8187_REQ_GET_REG, RTL8187_REQT_READ,
			(unsigned long)addr, 0, &val, sizeof(val), HZ / 2);

	return le16_to_cpu(val);
}

static inline u32 rtl818x_ioread32(struct rtl8187_priv *priv, __le32 *addr)
{
	__le32 val;

	usb_control_msg(priv->udev, usb_rcvctrlpipe(priv->udev, 0),
			RTL8187_REQ_GET_REG, RTL8187_REQT_READ,
			(unsigned long)addr, 0, &val, sizeof(val), HZ / 2);

	return le32_to_cpu(val);
}

static inline void rtl818x_iowrite8(struct rtl8187_priv *priv,
				    u8 *addr, u8 val)
{
	usb_control_msg(priv->udev, usb_sndctrlpipe(priv->udev, 0),
			RTL8187_REQ_SET_REG, RTL8187_REQT_WRITE,
			(unsigned long)addr, 0, &val, sizeof(val), HZ / 2);
}

static inline void rtl818x_iowrite16(struct rtl8187_priv *priv,
				     __le16 *addr, u16 val)
{
	__le16 buf = cpu_to_le16(val);

	usb_control_msg(priv->udev, usb_sndctrlpipe(priv->udev, 0),
			RTL8187_REQ_SET_REG, RTL8187_REQT_WRITE,
			(unsigned long)addr, 0, &buf, sizeof(buf), HZ / 2);
}

static inline void rtl818x_iowrite32(struct rtl8187_priv *priv,
				     __le32 *addr, u32 val)
{
	__le32 buf = cpu_to_le32(val);

	usb_control_msg(priv->udev, usb_sndctrlpipe(priv->udev, 0),
			RTL8187_REQ_SET_REG, RTL8187_REQT_WRITE,
			(unsigned long)addr, 0, &buf, sizeof(buf), HZ / 2);
}

#endif /* RTL8187_H */
