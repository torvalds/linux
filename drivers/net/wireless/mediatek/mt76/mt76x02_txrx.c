/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
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

#include <linux/kernel.h>

#include "mt76x02.h"

void mt76x02_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
		struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct mt76x02_dev *dev = hw->priv;
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;

	if (control->sta) {
		struct mt76x02_sta *msta;

		msta = (struct mt76x02_sta *)control->sta->drv_priv;
		wcid = &msta->wcid;
		/* sw encrypted frames */
		if (!info->control.hw_key && wcid->hw_key_idx != 0xff)
			control->sta = NULL;
	}

	if (vif && !control->sta) {
		struct mt76x02_vif *mvif;

		mvif = (struct mt76x02_vif *)vif->drv_priv;
		wcid = &mvif->group_wcid;
	}

	mt76_tx(&dev->mt76, control->sta, wcid, skb);
}
EXPORT_SYMBOL_GPL(mt76x02_tx);

void mt76x02_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			  struct sk_buff *skb)
{
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	void *rxwi = skb->data;

	if (q == MT_RXQ_MCU) {
		/* this is used just by mmio code */
		skb_queue_tail(&mdev->mmio.mcu.res_q, skb);
		wake_up(&mdev->mmio.mcu.wait);
		return;
	}

	skb_pull(skb, sizeof(struct mt76x02_rxwi));
	if (mt76x02_mac_process_rx(dev, skb, rxwi)) {
		dev_kfree_skb(skb);
		return;
	}

	mt76_rx(mdev, q, skb);
}
EXPORT_SYMBOL_GPL(mt76x02_queue_rx_skb);

s8 mt76x02_tx_get_max_txpwr_adj(struct mt76_dev *dev,
				const struct ieee80211_tx_rate *rate)
{
	s8 max_txpwr;

	if (rate->flags & IEEE80211_TX_RC_VHT_MCS) {
		u8 mcs = ieee80211_rate_get_vht_mcs(rate);

		if (mcs == 8 || mcs == 9) {
			max_txpwr = dev->rate_power.vht[8];
		} else {
			u8 nss, idx;

			nss = ieee80211_rate_get_vht_nss(rate);
			idx = ((nss - 1) << 3) + mcs;
			max_txpwr = dev->rate_power.ht[idx & 0xf];
		}
	} else if (rate->flags & IEEE80211_TX_RC_MCS) {
		max_txpwr = dev->rate_power.ht[rate->idx & 0xf];
	} else {
		enum nl80211_band band = dev->chandef.chan->band;

		if (band == NL80211_BAND_2GHZ) {
			const struct ieee80211_rate *r;
			struct wiphy *wiphy = dev->hw->wiphy;
			struct mt76_rate_power *rp = &dev->rate_power;

			r = &wiphy->bands[band]->bitrates[rate->idx];
			if (r->flags & IEEE80211_RATE_SHORT_PREAMBLE)
				max_txpwr = rp->cck[r->hw_value & 0x3];
			else
				max_txpwr = rp->ofdm[r->hw_value & 0x7];
		} else {
			max_txpwr = dev->rate_power.ofdm[rate->idx & 0x7];
		}
	}

	return max_txpwr;
}
EXPORT_SYMBOL_GPL(mt76x02_tx_get_max_txpwr_adj);

static void mt76x02_remove_dma_hdr(struct sk_buff *skb)
{
	int hdr_len;

	skb_pull(skb, sizeof(struct mt76x02_txwi) + MT_DMA_HDR_LEN);
	hdr_len = ieee80211_get_hdrlen_from_skb(skb);
	if (hdr_len % 4)
		mt76x02_remove_hdr_pad(skb, 2);
}

void mt76x02_tx_complete(struct mt76_dev *dev, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	if (info->flags & IEEE80211_TX_CTL_AMPDU) {
		ieee80211_free_txskb(dev->hw, skb);
	} else {
		ieee80211_tx_info_clear_status(info);
		info->status.rates[0].idx = -1;
		info->flags |= IEEE80211_TX_STAT_ACK;
		ieee80211_tx_status(dev->hw, skb);
	}
}
EXPORT_SYMBOL_GPL(mt76x02_tx_complete);

void mt76x02_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue *q,
			     struct mt76_queue_entry *e, bool flush)
{
	mt76x02_remove_dma_hdr(e->skb);
	mt76x02_tx_complete(mdev, e->skb);
}
EXPORT_SYMBOL_GPL(mt76x02_tx_complete_skb);

bool mt76x02_tx_status_data(struct mt76_dev *dev, u8 *update)
{
	struct mt76x02_tx_status stat;

	if (!mt76x02_mac_load_tx_status(dev, &stat))
		return false;

	mt76x02_send_tx_status(dev, &stat, update);

	return true;
}
EXPORT_SYMBOL_GPL(mt76x02_tx_status_data);
