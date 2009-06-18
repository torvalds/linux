/*
 * Atheros AR9170 driver
 *
 * Driver specific definitions
 *
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, see
 * http://www.gnu.org/licenses/.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *    Copyright (c) 2007-2008 Atheros Communications, Inc.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __AR9170_H
#define __AR9170_H

#include <linux/completion.h>
#include <linux/spinlock.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#ifdef CONFIG_AR9170_LEDS
#include <linux/leds.h>
#endif /* CONFIG_AR9170_LEDS */
#include "eeprom.h"
#include "hw.h"

#include "../regd.h"

#define PAYLOAD_MAX	(AR9170_MAX_CMD_LEN/4 - 1)

enum ar9170_bw {
	AR9170_BW_20,
	AR9170_BW_40_BELOW,
	AR9170_BW_40_ABOVE,

	__AR9170_NUM_BW,
};

static inline enum ar9170_bw nl80211_to_ar9170(enum nl80211_channel_type type)
{
	switch (type) {
	case NL80211_CHAN_NO_HT:
	case NL80211_CHAN_HT20:
		return AR9170_BW_20;
	case NL80211_CHAN_HT40MINUS:
		return AR9170_BW_40_BELOW;
	case NL80211_CHAN_HT40PLUS:
		return AR9170_BW_40_ABOVE;
	default:
		BUG();
	}
}

enum ar9170_rf_init_mode {
	AR9170_RFI_NONE,
	AR9170_RFI_WARM,
	AR9170_RFI_COLD,
};

#define AR9170_MAX_RX_BUFFER_SIZE		8192

#ifdef CONFIG_AR9170_LEDS
struct ar9170;

struct ar9170_led {
	struct ar9170 *ar;
	struct led_classdev l;
	char name[32];
	unsigned int toggled;
	bool last_state;
	bool registered;
};

#endif /* CONFIG_AR9170_LEDS */

enum ar9170_device_state {
	AR9170_UNKNOWN_STATE,
	AR9170_STOPPED,
	AR9170_IDLE,
	AR9170_STARTED,
};

struct ar9170_rxstream_mpdu_merge {
	struct ar9170_rx_head plcp;
	bool has_plcp;
};

#define AR9170_QUEUE_TIMEOUT		64
#define AR9170_TX_TIMEOUT		8
#define AR9170_JANITOR_DELAY		128
#define AR9170_TX_INVALID_RATE		0xffffffff

struct ar9170 {
	struct ieee80211_hw *hw;
	struct mutex mutex;
	enum ar9170_device_state state;
	unsigned long bad_hw_nagger;

	int (*open)(struct ar9170 *);
	void (*stop)(struct ar9170 *);
	int (*tx)(struct ar9170 *, struct sk_buff *);
	int (*exec_cmd)(struct ar9170 *, enum ar9170_cmd, u32 ,
			void *, u32 , void *);
	void (*callback_cmd)(struct ar9170 *, u32 , void *);
	int (*flush)(struct ar9170 *);

	/* interface mode settings */
	struct ieee80211_vif *vif;
	u8 mac_addr[ETH_ALEN];
	u8 bssid[ETH_ALEN];

	/* beaconing */
	struct sk_buff *beacon;
	struct work_struct beacon_work;

	/* cryptographic engine */
	u64 usedkeys;
	bool rx_software_decryption;
	bool disable_offload;

	/* filter settings */
	struct work_struct filter_config_work;
	u64 cur_mc_hash, want_mc_hash;
	u32 cur_filter, want_filter;
	unsigned long filter_changed;
	unsigned int filter_state;
	bool sniffer_enabled;

	/* PHY */
	struct ieee80211_channel *channel;
	int noise[4];

	/* power calibration data */
	u8 power_5G_leg[4];
	u8 power_2G_cck[4];
	u8 power_2G_ofdm[4];
	u8 power_5G_ht20[8];
	u8 power_5G_ht40[8];
	u8 power_2G_ht20[8];
	u8 power_2G_ht40[8];

#ifdef CONFIG_AR9170_LEDS
	struct delayed_work led_work;
	struct ar9170_led leds[AR9170_NUM_LEDS];
#endif /* CONFIG_AR9170_LEDS */

	/* qos queue settings */
	spinlock_t tx_stats_lock;
	struct ieee80211_tx_queue_stats tx_stats[5];
	struct ieee80211_tx_queue_params edcf[5];

	spinlock_t cmdlock;
	__le32 cmdbuf[PAYLOAD_MAX + 1];

	/* MAC statistics */
	struct ieee80211_low_level_stats stats;

	/* EEPROM */
	struct ar9170_eeprom eeprom;
	struct ath_regulatory regulatory;

	/* tx queues - as seen by hw - */
	struct sk_buff_head tx_pending[__AR9170_NUM_TXQ];
	struct sk_buff_head tx_status[__AR9170_NUM_TXQ];
	struct delayed_work tx_janitor;

	/* rxstream mpdu merge */
	struct ar9170_rxstream_mpdu_merge rx_mpdu;
	struct sk_buff *rx_failover;
	int rx_failover_missing;
};

struct ar9170_sta_info {
};

#define AR9170_TX_FLAG_WAIT_FOR_ACK	BIT(0)
#define AR9170_TX_FLAG_NO_ACK		BIT(1)
#define AR9170_TX_FLAG_BLOCK_ACK	BIT(2)

struct ar9170_tx_info {
	unsigned long timeout;
	unsigned int flags;
};

#define IS_STARTED(a)		(((struct ar9170 *)a)->state >= AR9170_STARTED)
#define IS_ACCEPTING_CMD(a)	(((struct ar9170 *)a)->state >= AR9170_IDLE)

#define AR9170_FILTER_CHANGED_MODE		BIT(0)
#define AR9170_FILTER_CHANGED_MULTICAST		BIT(1)
#define AR9170_FILTER_CHANGED_FRAMEFILTER	BIT(2)

/* exported interface */
void *ar9170_alloc(size_t priv_size);
int ar9170_register(struct ar9170 *ar, struct device *pdev);
void ar9170_rx(struct ar9170 *ar, struct sk_buff *skb);
void ar9170_unregister(struct ar9170 *ar);
void ar9170_tx_callback(struct ar9170 *ar, struct sk_buff *skb);
void ar9170_handle_command_response(struct ar9170 *ar, void *buf, u32 len);
int ar9170_nag_limiter(struct ar9170 *ar);

/* MAC */
int ar9170_op_tx(struct ieee80211_hw *hw, struct sk_buff *skb);
int ar9170_init_mac(struct ar9170 *ar);
int ar9170_set_qos(struct ar9170 *ar);
int ar9170_update_multicast(struct ar9170 *ar);
int ar9170_update_frame_filter(struct ar9170 *ar);
int ar9170_set_operating_mode(struct ar9170 *ar);
int ar9170_set_beacon_timers(struct ar9170 *ar);
int ar9170_set_dyn_sifs_ack(struct ar9170 *ar);
int ar9170_set_slot_time(struct ar9170 *ar);
int ar9170_set_basic_rates(struct ar9170 *ar);
int ar9170_set_hwretry_limit(struct ar9170 *ar, u32 max_retry);
int ar9170_update_beacon(struct ar9170 *ar);
void ar9170_new_beacon(struct work_struct *work);
int ar9170_upload_key(struct ar9170 *ar, u8 id, const u8 *mac, u8 ktype,
		      u8 keyidx, u8 *keydata, int keylen);
int ar9170_disable_key(struct ar9170 *ar, u8 id);

/* LEDs */
#ifdef CONFIG_AR9170_LEDS
int ar9170_register_leds(struct ar9170 *ar);
void ar9170_unregister_leds(struct ar9170 *ar);
#endif /* CONFIG_AR9170_LEDS */
int ar9170_init_leds(struct ar9170 *ar);
int ar9170_set_leds_state(struct ar9170 *ar, u32 led_state);

/* PHY / RF */
int ar9170_init_phy(struct ar9170 *ar, enum ieee80211_band band);
int ar9170_init_rf(struct ar9170 *ar);
int ar9170_set_channel(struct ar9170 *ar, struct ieee80211_channel *channel,
		       enum ar9170_rf_init_mode rfi, enum ar9170_bw bw);

#endif /* __AR9170_H */
