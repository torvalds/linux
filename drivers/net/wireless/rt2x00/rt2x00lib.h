/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00lib
	Abstract: Data structures and definitions for the rt2x00lib module.
 */

#ifndef RT2X00LIB_H
#define RT2X00LIB_H

#include "rt2x00dump.h"

/*
 * Interval defines
 * Both the link tuner as the rfkill will be called once per second.
 */
#define LINK_TUNE_INTERVAL	( round_jiffies_relative(HZ) )
#define RFKILL_POLL_INTERVAL	( round_jiffies_relative(HZ) )

/*
 * rt2x00_rate: Per rate device information
 */
struct rt2x00_rate {
	unsigned short flags;
#define DEV_RATE_CCK			0x0001
#define DEV_RATE_OFDM			0x0002
#define DEV_RATE_SHORT_PREAMBLE		0x0004
#define DEV_RATE_BASIC			0x0008

	unsigned short bitrate; /* In 100kbit/s */
	unsigned short ratemask;

	unsigned short plcp;
};

extern const struct rt2x00_rate rt2x00_supported_rates[12];

static inline u16 rt2x00_create_rate_hw_value(const u16 index,
					      const u16 short_preamble)
{
	return (short_preamble << 8) | (index & 0xff);
}

static inline const struct rt2x00_rate *rt2x00_get_rate(const u16 hw_value)
{
	return &rt2x00_supported_rates[hw_value & 0xff];
}

static inline int rt2x00_get_rate_preamble(const u16 hw_value)
{
	return (hw_value & 0xff00);
}

/*
 * Radio control handlers.
 */
int rt2x00lib_enable_radio(struct rt2x00_dev *rt2x00dev);
void rt2x00lib_disable_radio(struct rt2x00_dev *rt2x00dev);
void rt2x00lib_toggle_rx(struct rt2x00_dev *rt2x00dev, enum dev_state state);
void rt2x00lib_reset_link_tuner(struct rt2x00_dev *rt2x00dev);

/*
 * Initialization handlers.
 */
int rt2x00lib_start(struct rt2x00_dev *rt2x00dev);
void rt2x00lib_stop(struct rt2x00_dev *rt2x00dev);

/*
 * Configuration handlers.
 */
void rt2x00lib_config_intf(struct rt2x00_dev *rt2x00dev,
			   struct rt2x00_intf *intf,
			   enum ieee80211_if_types type,
			   u8 *mac, u8 *bssid);
void rt2x00lib_config_erp(struct rt2x00_dev *rt2x00dev,
			  struct rt2x00_intf *intf,
			  struct ieee80211_bss_conf *conf);
void rt2x00lib_config_antenna(struct rt2x00_dev *rt2x00dev,
			      enum antenna rx, enum antenna tx);
void rt2x00lib_config(struct rt2x00_dev *rt2x00dev,
		      struct ieee80211_conf *conf, const int force_config);

/**
 * DOC: Queue handlers
 */

/**
 * rt2x00queue_alloc_rxskb - allocate a skb for RX purposes.
 * @rt2x00dev: Pointer to &struct rt2x00_dev.
 * @queue: The queue for which the skb will be applicable.
 */
struct sk_buff *rt2x00queue_alloc_rxskb(struct rt2x00_dev *rt2x00dev,
					struct queue_entry *entry);

/**
 * rt2x00queue_unmap_skb - Unmap a skb from DMA.
 * @rt2x00dev: Pointer to &struct rt2x00_dev.
 * @skb: The skb to unmap.
 */
void rt2x00queue_unmap_skb(struct rt2x00_dev *rt2x00dev, struct sk_buff *skb);

/**
 * rt2x00queue_free_skb - free a skb
 * @rt2x00dev: Pointer to &struct rt2x00_dev.
 * @skb: The skb to free.
 */
void rt2x00queue_free_skb(struct rt2x00_dev *rt2x00dev, struct sk_buff *skb);

/**
 * rt2x00queue_write_tx_frame - Write TX frame to hardware
 * @queue: Queue over which the frame should be send
 * @skb: The skb to send
 */
int rt2x00queue_write_tx_frame(struct data_queue *queue, struct sk_buff *skb);

/**
 * rt2x00queue_update_beacon - Send new beacon from mac80211 to hardware
 * @rt2x00dev: Pointer to &struct rt2x00_dev.
 * @vif: Interface for which the beacon should be updated.
 */
int rt2x00queue_update_beacon(struct rt2x00_dev *rt2x00dev,
			      struct ieee80211_vif *vif);

/**
 * rt2x00queue_index_inc - Index incrementation function
 * @queue: Queue (&struct data_queue) to perform the action on.
 * @index: Index type (&enum queue_index) to perform the action on.
 *
 * This function will increase the requested index on the queue,
 * it will grab the appropriate locks and handle queue overflow events by
 * resetting the index to the start of the queue.
 */
void rt2x00queue_index_inc(struct data_queue *queue, enum queue_index index);

void rt2x00queue_init_rx(struct rt2x00_dev *rt2x00dev);
void rt2x00queue_init_tx(struct rt2x00_dev *rt2x00dev);
int rt2x00queue_initialize(struct rt2x00_dev *rt2x00dev);
void rt2x00queue_uninitialize(struct rt2x00_dev *rt2x00dev);
int rt2x00queue_allocate(struct rt2x00_dev *rt2x00dev);
void rt2x00queue_free(struct rt2x00_dev *rt2x00dev);

/*
 * Firmware handlers.
 */
#ifdef CONFIG_RT2X00_LIB_FIRMWARE
int rt2x00lib_load_firmware(struct rt2x00_dev *rt2x00dev);
void rt2x00lib_free_firmware(struct rt2x00_dev *rt2x00dev);
#else
static inline int rt2x00lib_load_firmware(struct rt2x00_dev *rt2x00dev)
{
	return 0;
}
static inline void rt2x00lib_free_firmware(struct rt2x00_dev *rt2x00dev)
{
}
#endif /* CONFIG_RT2X00_LIB_FIRMWARE */

/*
 * Debugfs handlers.
 */
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
void rt2x00debug_register(struct rt2x00_dev *rt2x00dev);
void rt2x00debug_deregister(struct rt2x00_dev *rt2x00dev);
void rt2x00debug_dump_frame(struct rt2x00_dev *rt2x00dev,
			    enum rt2x00_dump_type type, struct sk_buff *skb);
void rt2x00debug_update_crypto(struct rt2x00_dev *rt2x00dev,
			       enum cipher cipher, enum rx_crypto status);
#else
static inline void rt2x00debug_register(struct rt2x00_dev *rt2x00dev)
{
}

static inline void rt2x00debug_deregister(struct rt2x00_dev *rt2x00dev)
{
}

static inline void rt2x00debug_dump_frame(struct rt2x00_dev *rt2x00dev,
					  enum rt2x00_dump_type type,
					  struct sk_buff *skb)
{
}

static inline void rt2x00debug_update_crypto(struct rt2x00_dev *rt2x00dev,
					     enum cipher cipher,
					     enum rx_crypto status)
{
}
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */

/*
 * Crypto handlers.
 */
#ifdef CONFIG_RT2X00_LIB_CRYPTO
enum cipher rt2x00crypto_key_to_cipher(struct ieee80211_key_conf *key);
unsigned int rt2x00crypto_tx_overhead(struct ieee80211_tx_info *tx_info);
void rt2x00crypto_tx_remove_iv(struct sk_buff *skb, unsigned int iv_len);
void rt2x00crypto_tx_insert_iv(struct sk_buff *skb);
void rt2x00crypto_rx_insert_iv(struct sk_buff *skb, unsigned int align,
			       unsigned int header_length,
			       struct rxdone_entry_desc *rxdesc);
#else
static inline enum cipher rt2x00crypto_key_to_cipher(struct ieee80211_key_conf *key)
{
	return CIPHER_NONE;
}

static inline unsigned int rt2x00crypto_tx_overhead(struct ieee80211_tx_info *tx_info)
{
	return 0;
}

static inline void rt2x00crypto_tx_remove_iv(struct sk_buff *skb,
					     unsigned int iv_len)
{
}

static inline void rt2x00crypto_tx_insert_iv(struct sk_buff *skb)
{
}

static inline void rt2x00crypto_rx_insert_iv(struct sk_buff *skb,
					     unsigned int align,
					     unsigned int header_length,
					     struct rxdone_entry_desc *rxdesc)
{
}
#endif

/*
 * RFkill handlers.
 */
#ifdef CONFIG_RT2X00_LIB_RFKILL
void rt2x00rfkill_register(struct rt2x00_dev *rt2x00dev);
void rt2x00rfkill_unregister(struct rt2x00_dev *rt2x00dev);
void rt2x00rfkill_allocate(struct rt2x00_dev *rt2x00dev);
void rt2x00rfkill_free(struct rt2x00_dev *rt2x00dev);
#else
static inline void rt2x00rfkill_register(struct rt2x00_dev *rt2x00dev)
{
}

static inline void rt2x00rfkill_unregister(struct rt2x00_dev *rt2x00dev)
{
}

static inline void rt2x00rfkill_allocate(struct rt2x00_dev *rt2x00dev)
{
}

static inline void rt2x00rfkill_free(struct rt2x00_dev *rt2x00dev)
{
}
#endif /* CONFIG_RT2X00_LIB_RFKILL */

/*
 * LED handlers
 */
#ifdef CONFIG_RT2X00_LIB_LEDS
void rt2x00leds_led_quality(struct rt2x00_dev *rt2x00dev, int rssi);
void rt2x00led_led_activity(struct rt2x00_dev *rt2x00dev, bool enabled);
void rt2x00leds_led_assoc(struct rt2x00_dev *rt2x00dev, bool enabled);
void rt2x00leds_led_radio(struct rt2x00_dev *rt2x00dev, bool enabled);
void rt2x00leds_register(struct rt2x00_dev *rt2x00dev);
void rt2x00leds_unregister(struct rt2x00_dev *rt2x00dev);
void rt2x00leds_suspend(struct rt2x00_dev *rt2x00dev);
void rt2x00leds_resume(struct rt2x00_dev *rt2x00dev);
#else
static inline void rt2x00leds_led_quality(struct rt2x00_dev *rt2x00dev,
					  int rssi)
{
}

static inline void rt2x00led_led_activity(struct rt2x00_dev *rt2x00dev,
					  bool enabled)
{
}

static inline void rt2x00leds_led_assoc(struct rt2x00_dev *rt2x00dev,
					bool enabled)
{
}

static inline void rt2x00leds_led_radio(struct rt2x00_dev *rt2x00dev,
					bool enabled)
{
}

static inline void rt2x00leds_register(struct rt2x00_dev *rt2x00dev)
{
}

static inline void rt2x00leds_unregister(struct rt2x00_dev *rt2x00dev)
{
}

static inline void rt2x00leds_suspend(struct rt2x00_dev *rt2x00dev)
{
}

static inline void rt2x00leds_resume(struct rt2x00_dev *rt2x00dev)
{
}
#endif /* CONFIG_RT2X00_LIB_LEDS */

#endif /* RT2X00LIB_H */
