/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __MT76X02_UTIL_H
#define __MT76X02_UTIL_H

#include <linux/kfifo.h>

#include "mt76x02_mac.h"
#include "mt76x02_dfs.h"

struct mt76x02_mac_stats {
	u64 rx_stat[6];
	u64 tx_stat[6];
	u64 aggr_stat[2];
	u64 aggr_n[32];
	u64 zero_len_del[2];
};

#define MT_MAX_CHAINS		2
struct mt76x02_rx_freq_cal {
	s8 high_gain[MT_MAX_CHAINS];
	s8 rssi_offset[MT_MAX_CHAINS];
	s8 lna_gain;
	u32 mcu_gain;
	s16 temp_offset;
	u8 freq_offset;
};

struct mt76x02_calibration {
	struct mt76x02_rx_freq_cal rx;

	u8 agc_gain_init[MT_MAX_CHAINS];
	u8 agc_gain_cur[MT_MAX_CHAINS];

	u16 false_cca;
	s8 avg_rssi_all;
	s8 agc_gain_adjust;
	s8 low_gain;

	u8 temp;

	bool init_cal_done;
	bool tssi_cal_done;
	bool tssi_comp_pending;
	bool dpd_cal_done;
	bool channel_cal_done;
};

struct mt76x02_dev {
	struct mt76_dev mt76; /* must be first */

	struct mac_address macaddr_list[8];

	struct mutex phy_mutex;
	struct mutex mutex;

	u8 txdone_seq;
	DECLARE_KFIFO_PTR(txstatus_fifo, struct mt76x02_tx_status);

	struct sk_buff *rx_head;

	struct tasklet_struct tx_tasklet;
	struct tasklet_struct pre_tbtt_tasklet;
	struct delayed_work cal_work;
	struct delayed_work mac_work;

	struct mt76x02_mac_stats stats;
	atomic_t avg_ampdu_len;
	u32 aggr_stats[32];

	struct sk_buff *beacons[8];
	u8 beacon_mask;
	u8 beacon_data_mask;

	u8 tbtt_count;
	u16 beacon_int;

	struct mt76x02_calibration cal;

	s8 target_power;
	s8 target_power_delta[2];
	bool enable_tpc;

	bool no_2ghz;

	u8 agc_save;

	u8 coverage_class;
	u8 slottime;

	struct mt76x02_dfs_pattern_detector dfs_pd;
};

extern struct ieee80211_rate mt76x02_rates[12];

void mt76x02_configure_filter(struct ieee80211_hw *hw,
			     unsigned int changed_flags,
			     unsigned int *total_flags, u64 multicast);
int mt76x02_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta);
int mt76x02_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta);

void mt76x02_vif_init(struct mt76_dev *dev, struct ieee80211_vif *vif,
		     unsigned int idx);
int mt76x02_add_interface(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif);
void mt76x02_remove_interface(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif);

int mt76x02_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_ampdu_params *params);
int mt76x02_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		   struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		   struct ieee80211_key_conf *key);
int mt76x02_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   u16 queue, const struct ieee80211_tx_queue_params *params);
void mt76x02_sta_rate_tbl_update(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta);
s8 mt76x02_tx_get_max_txpwr_adj(struct mt76_dev *dev,
				const struct ieee80211_tx_rate *rate);
int mt76x02_insert_hdr_pad(struct sk_buff *skb);
void mt76x02_remove_hdr_pad(struct sk_buff *skb, int len);
void mt76x02_tx_complete(struct mt76_dev *dev, struct sk_buff *skb);
void mt76x02_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue *q,
			    struct mt76_queue_entry *e, bool flush);
bool mt76x02_tx_status_data(struct mt76_dev *dev, u8 *update);

extern const u16 mt76x02_beacon_offsets[16];
void mt76x02_set_beacon_offsets(struct mt76_dev *dev);
void mt76x02_set_irq_mask(struct mt76_dev *dev, u32 clear, u32 set);
void mt76x02_mac_start(struct mt76_dev *dev);

static inline void mt76x02_irq_enable(struct mt76_dev *dev, u32 mask)
{
	mt76x02_set_irq_mask(dev, 0, mask);
}

static inline void mt76x02_irq_disable(struct mt76_dev *dev, u32 mask)
{
	mt76x02_set_irq_mask(dev, mask, 0);
}

static inline bool
mt76x02_wait_for_txrx_idle(struct mt76_dev *dev)
{
	return __mt76_poll_msec(dev, MT_MAC_STATUS,
				MT_MAC_STATUS_TX | MT_MAC_STATUS_RX,
				0, 100);
}

static inline struct mt76x02_sta *
mt76x02_rx_get_sta(struct mt76_dev *dev, u8 idx)
{
	struct mt76_wcid *wcid;

	if (idx >= ARRAY_SIZE(dev->wcid))
		return NULL;

	wcid = rcu_dereference(dev->wcid[idx]);
	if (!wcid)
		return NULL;

	return container_of(wcid, struct mt76x02_sta, wcid);
}

static inline struct mt76_wcid *
mt76x02_rx_get_sta_wcid(struct mt76x02_sta *sta, bool unicast)
{
	if (!sta)
		return NULL;

	if (unicast)
		return &sta->wcid;
	else
		return &sta->vif->group_wcid;
}

#endif
