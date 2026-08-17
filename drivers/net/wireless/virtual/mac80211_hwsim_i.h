// SPDX-License-Identifier: GPL-2.0-only
/*
 * mac80211_hwsim - software simulator of 802.11 radio(s) for mac80211
 * Copyright (c) 2008, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2011, Javier Lopez <jlopex@gmail.com>
 * Copyright (c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018 - 2026 Intel Corporation
 */

#ifndef __MAC80211_HWSIM_I_H
#define __MAC80211_HWSIM_I_H

#include <net/mac80211.h>
#include "mac80211_hwsim.h"
#include "mac80211_hwsim_nan.h"

struct hwsim_sta_nan_sched {
	/* Later members are protected by this lock */
	spinlock_t lock;
	u16 committed_dw;
	struct {
		u8 map_id;
		struct cfg80211_chan_def chans[CFG80211_NAN_SCHED_NUM_TIME_SLOTS];
	} maps[CFG80211_NAN_MAX_PEER_MAPS];
};

struct hwsim_sta_priv {
	u32 magic;
	unsigned int last_link;
	u16 active_links_rx;

	/* NAN peer schedule - must be accessed under nan_sched.lock */
	struct hwsim_sta_nan_sched nan_sched;
};

#define HWSIM_STA_MAGIC	0x6d537749

struct mac80211_hwsim_link_data {
	u32 link_id;
	u64 beacon_int	/* beacon interval in us */;
	struct hrtimer beacon_timer;
};

#define HWSIM_NUM_CHANNELS_2GHZ		14
#define HWSIM_NUM_CHANNELS_5GHZ		40
#define HWSIM_NUM_CHANNELS_6GHZ		59
#define HWSIM_NUM_S1G_CHANNELS_US	51
#define HWSIM_NUM_RATES			12
#define HWSIM_NUM_CIPHERS		11

struct mac80211_hwsim_data {
	struct list_head list;
	struct rhash_head rht;
	struct ieee80211_hw *hw;
	struct device *dev;
	struct ieee80211_supported_band bands[NUM_NL80211_BANDS];
	struct ieee80211_channel channels_2ghz[HWSIM_NUM_CHANNELS_2GHZ];
	struct ieee80211_channel channels_5ghz[HWSIM_NUM_CHANNELS_5GHZ];
	struct ieee80211_channel channels_6ghz[HWSIM_NUM_CHANNELS_6GHZ];
	struct ieee80211_channel channels_s1g[HWSIM_NUM_S1G_CHANNELS_US];
	struct ieee80211_rate rates[HWSIM_NUM_RATES];
	struct ieee80211_iface_combination if_combination;
	struct ieee80211_iface_limit if_limits[5];
	int n_if_limits;
	/* Storage space for channels, etc. */
	struct mac80211_hwsim_phy_data *phy_data;

	struct ieee80211_iface_combination if_combination_radio;
	struct wiphy_radio_freq_range radio_range[NUM_NL80211_BANDS];
	struct wiphy_radio radio[NUM_NL80211_BANDS];

	u32 ciphers[HWSIM_NUM_CIPHERS];

	struct mac_address addresses[3];
	int channels, idx;
	bool use_chanctx;
	bool destroy_on_close;
	u32 portid;
	char alpha2[2];
	const struct ieee80211_regdomain *regd;

	struct ieee80211_channel *tmp_chan;
	struct ieee80211_channel *roc_chan;
	u32 roc_duration;
	struct delayed_work roc_start;
	struct delayed_work roc_done;
	struct delayed_work hw_scan;
	struct cfg80211_scan_request *hw_scan_request;
	struct ieee80211_vif *hw_scan_vif;
	int scan_chan_idx;
	u8 scan_addr[ETH_ALEN];
	struct {
		struct ieee80211_channel *channel;
		unsigned long next_start, start, end;
	} survey_data[HWSIM_NUM_CHANNELS_2GHZ +
		      HWSIM_NUM_CHANNELS_5GHZ +
		      HWSIM_NUM_CHANNELS_6GHZ];

	struct ieee80211_channel *channel;
	enum nl80211_chan_width bw;
	unsigned int rx_filter;
	bool started, idle, scanning;
	struct mutex mutex;
	enum ps_mode {
		PS_DISABLED, PS_ENABLED, PS_AUTO_POLL, PS_MANUAL_POLL
	} ps;
	bool ps_poll_pending;
	struct dentry *debugfs;
	struct cfg80211_chan_def radar_background_chandef;

	atomic_t pending_cookie;
	struct sk_buff_head pending;	/* packets pending */
	/*
	 * Only radios in the same group can communicate together (the
	 * channel has to match too). Each bit represents a group. A
	 * radio can be in more than one group.
	 */
	u64 group;

	/* group shared by radios created in the same netns */
	int netgroup;
	/* wmediumd portid responsible for netgroup of this radio */
	u32 wmediumd;

	/* difference between this hw's clock and the real clock, in usecs */
	spinlock_t tsf_offset_lock;
	s64 tsf_offset;

	/* Stats */
	u64 tx_pkts;
	u64 rx_pkts;
	u64 tx_bytes;
	u64 rx_bytes;
	u64 tx_dropped;
	u64 tx_failed;

	/* RSSI in rx status of the receiver */
	int rx_rssi;

	/* only used when pmsr capability is supplied */
	struct cfg80211_pmsr_capabilities pmsr_capa;
	struct cfg80211_pmsr_request *pmsr_request;
	struct wireless_dev *pmsr_request_wdev;

	struct mac80211_hwsim_link_data link_data[IEEE80211_MLD_MAX_NUM_LINKS];

	struct mac80211_hwsim_nan_data nan;
};

extern spinlock_t hwsim_radio_lock;
extern struct list_head hwsim_radios;

ktime_t mac80211_hwsim_tsf_to_boottime(struct mac80211_hwsim_data *data,
				       u64 tsf);
u64 mac80211_hwsim_boottime_to_tsf(struct mac80211_hwsim_data *data,
				   ktime_t ts);

u64 mac80211_hwsim_get_tsf(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif);

void mac80211_hwsim_tx_frame(struct ieee80211_hw *hw,
			     struct sk_buff *skb,
			     struct ieee80211_channel *chan);

void ieee80211_hwsim_wake_tx_queue(struct ieee80211_hw *hw,
				   struct ieee80211_txq *txq);

#endif /* __MAC80211_HWSIM_I_H */
