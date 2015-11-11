/*
 * Definitions for RTL8187 hardware
 *
 * Copyright 2007 Michael Wu <flamingice@sourmilk.net>
 * Copyright 2007 Andrea Merello <andrea.merello@gmail.com>
 *
 * Based on the r8187 driver, which is:
 * Copyright 2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RTL8187_H
#define RTL8187_H

#include <linux/cache.h>

#include "rtl818x.h"
#include "leds.h"

#define RTL8187_EEPROM_TXPWR_BASE	0x05
#define RTL8187_EEPROM_MAC_ADDR		0x07
#define RTL8187_EEPROM_TXPWR_CHAN_1	0x16	/* 3 channels */
#define RTL8187_EEPROM_TXPWR_CHAN_6	0x1B	/* 2 channels */
#define RTL8187_EEPROM_TXPWR_CHAN_4	0x3D	/* 2 channels */
#define RTL8187_EEPROM_SELECT_GPIO	0x3B

#define RTL8187_REQT_READ	0xC0
#define RTL8187_REQT_WRITE	0x40
#define RTL8187_REQ_GET_REG	0x05
#define RTL8187_REQ_SET_REG	0x05

#define RTL8187_MAX_RX		0x9C4

#define RFKILL_MASK_8187_89_97	0x2
#define RFKILL_MASK_8198	0x4

#define RETRY_COUNT		7

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
} __packed;

struct rtl8187b_rx_hdr {
	__le32 flags;
	__le64 mac_time;
	u8 sq;
	u8 rssi;
	u8 agc;
	u8 flags2;
	__le16 snr_long2end;
	s8 pwdb_g12;
	u8 fot;
} __packed;

/* {rtl8187,rtl8187b}_tx_info is in skb */

struct rtl8187_tx_hdr {
	__le32 flags;
	__le16 rts_duration;
	__le16 len;
	__le32 retry;
} __packed;

struct rtl8187b_tx_hdr {
	__le32 flags;
	__le16 rts_duration;
	__le16 len;
	__le32 unused_1;
	__le16 unused_2;
	__le16 tx_duration;
	__le32 unused_3;
	__le32 retry;
	__le32 unused_4[2];
} __packed;

enum {
	DEVICE_RTL8187,
	DEVICE_RTL8187B
};

struct rtl8187_vif {
	struct ieee80211_hw *dev;

	/* beaconing */
	struct delayed_work beacon_work;
	bool enable_beacon;
};

struct rtl8187_priv {
	/* common between rtl818x drivers */
	struct rtl818x_csr *map;
	const struct rtl818x_rf_ops *rf;
	struct ieee80211_vif *vif;

	/* The mutex protects the TX loopback state.
	 * Any attempt to set channels concurrently locks the device.
	 */
	struct mutex conf_mutex;

	/* rtl8187 specific */
	struct ieee80211_channel channels[14];
	struct ieee80211_rate rates[12];
	struct ieee80211_supported_band band;
	struct usb_device *udev;
	u32 rx_conf;
	struct usb_anchor anchored;
	struct delayed_work work;
	struct ieee80211_hw *dev;
#ifdef CONFIG_RTL8187_LEDS
	struct rtl8187_led led_radio;
	struct rtl8187_led led_tx;
	struct rtl8187_led led_rx;
	struct delayed_work led_on;
	struct delayed_work led_off;
#endif
	u16 txpwr_base;
	u8 asic_rev;
	u8 is_rtl8187b;
	enum {
		RTL8187BvB,
		RTL8187BvD,
		RTL8187BvE
	} hw_rev;
	struct sk_buff_head rx_queue;
	u8 signal;
	u8 noise;
	u8 slot_time;
	u8 aifsn[4];
	u8 rfkill_mask;
	struct {
		union {
			__le64 buf;
			u8 dummy1[L1_CACHE_BYTES];
		} ____cacheline_aligned;
		struct sk_buff_head queue;
	} b_tx_status; /* This queue is used by both -b and non-b devices */
	struct mutex io_mutex;
	union {
		u8 bits8;
		__le16 bits16;
		__le32 bits32;
		u8 dummy2[L1_CACHE_BYTES];
	} *io_dmabuf ____cacheline_aligned;
	bool rfkill_off;
	u16 seqno;
};

void rtl8187_write_phy(struct ieee80211_hw *dev, u8 addr, u32 data);

static inline u8 rtl818x_ioread8_idx(struct rtl8187_priv *priv,
				     u8 *addr, u8 idx)
{
	u8 val;

	mutex_lock(&priv->io_mutex);
	usb_control_msg(priv->udev, usb_rcvctrlpipe(priv->udev, 0),
			RTL8187_REQ_GET_REG, RTL8187_REQT_READ,
			(unsigned long)addr, idx & 0x03,
			&priv->io_dmabuf->bits8, sizeof(val), HZ / 2);

	val = priv->io_dmabuf->bits8;
	mutex_unlock(&priv->io_mutex);

	return val;
}

static inline u8 rtl818x_ioread8(struct rtl8187_priv *priv, u8 *addr)
{
	return rtl818x_ioread8_idx(priv, addr, 0);
}

static inline u16 rtl818x_ioread16_idx(struct rtl8187_priv *priv,
				       __le16 *addr, u8 idx)
{
	__le16 val;

	mutex_lock(&priv->io_mutex);
	usb_control_msg(priv->udev, usb_rcvctrlpipe(priv->udev, 0),
			RTL8187_REQ_GET_REG, RTL8187_REQT_READ,
			(unsigned long)addr, idx & 0x03,
			&priv->io_dmabuf->bits16, sizeof(val), HZ / 2);

	val = priv->io_dmabuf->bits16;
	mutex_unlock(&priv->io_mutex);

	return le16_to_cpu(val);
}

static inline u16 rtl818x_ioread16(struct rtl8187_priv *priv, __le16 *addr)
{
	return rtl818x_ioread16_idx(priv, addr, 0);
}

static inline u32 rtl818x_ioread32_idx(struct rtl8187_priv *priv,
				       __le32 *addr, u8 idx)
{
	__le32 val;

	mutex_lock(&priv->io_mutex);
	usb_control_msg(priv->udev, usb_rcvctrlpipe(priv->udev, 0),
			RTL8187_REQ_GET_REG, RTL8187_REQT_READ,
			(unsigned long)addr, idx & 0x03,
			&priv->io_dmabuf->bits32, sizeof(val), HZ / 2);

	val = priv->io_dmabuf->bits32;
	mutex_unlock(&priv->io_mutex);

	return le32_to_cpu(val);
}

static inline u32 rtl818x_ioread32(struct rtl8187_priv *priv, __le32 *addr)
{
	return rtl818x_ioread32_idx(priv, addr, 0);
}

static inline void rtl818x_iowrite8_idx(struct rtl8187_priv *priv,
					u8 *addr, u8 val, u8 idx)
{
	mutex_lock(&priv->io_mutex);

	priv->io_dmabuf->bits8 = val;
	usb_control_msg(priv->udev, usb_sndctrlpipe(priv->udev, 0),
			RTL8187_REQ_SET_REG, RTL8187_REQT_WRITE,
			(unsigned long)addr, idx & 0x03,
			&priv->io_dmabuf->bits8, sizeof(val), HZ / 2);

	mutex_unlock(&priv->io_mutex);
}

static inline void rtl818x_iowrite8(struct rtl8187_priv *priv, u8 *addr, u8 val)
{
	rtl818x_iowrite8_idx(priv, addr, val, 0);
}

static inline void rtl818x_iowrite16_idx(struct rtl8187_priv *priv,
					 __le16 *addr, u16 val, u8 idx)
{
	mutex_lock(&priv->io_mutex);

	priv->io_dmabuf->bits16 = cpu_to_le16(val);
	usb_control_msg(priv->udev, usb_sndctrlpipe(priv->udev, 0),
			RTL8187_REQ_SET_REG, RTL8187_REQT_WRITE,
			(unsigned long)addr, idx & 0x03,
			&priv->io_dmabuf->bits16, sizeof(val), HZ / 2);

	mutex_unlock(&priv->io_mutex);
}

static inline void rtl818x_iowrite16(struct rtl8187_priv *priv, __le16 *addr,
				     u16 val)
{
	rtl818x_iowrite16_idx(priv, addr, val, 0);
}

static inline void rtl818x_iowrite32_idx(struct rtl8187_priv *priv,
					 __le32 *addr, u32 val, u8 idx)
{
	mutex_lock(&priv->io_mutex);

	priv->io_dmabuf->bits32 = cpu_to_le32(val);
	usb_control_msg(priv->udev, usb_sndctrlpipe(priv->udev, 0),
			RTL8187_REQ_SET_REG, RTL8187_REQT_WRITE,
			(unsigned long)addr, idx & 0x03,
			&priv->io_dmabuf->bits32, sizeof(val), HZ / 2);

	mutex_unlock(&priv->io_mutex);
}

static inline void rtl818x_iowrite32(struct rtl8187_priv *priv, __le32 *addr,
				     u32 val)
{
	rtl818x_iowrite32_idx(priv, addr, val, 0);
}

#endif /* RTL8187_H */
