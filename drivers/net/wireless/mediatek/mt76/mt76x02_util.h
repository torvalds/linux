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

#endif
