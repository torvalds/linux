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
	} else if (vif) {
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
		mt76_mcu_rx_event(&dev->mt76, skb);
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

s8 mt76x02_tx_get_max_txpwr_adj(struct mt76x02_dev *dev,
				const struct ieee80211_tx_rate *rate)
{
	s8 max_txpwr;

	if (rate->flags & IEEE80211_TX_RC_VHT_MCS) {
		u8 mcs = ieee80211_rate_get_vht_mcs(rate);

		if (mcs == 8 || mcs == 9) {
			max_txpwr = dev->mt76.rate_power.vht[8];
		} else {
			u8 nss, idx;

			nss = ieee80211_rate_get_vht_nss(rate);
			idx = ((nss - 1) << 3) + mcs;
			max_txpwr = dev->mt76.rate_power.ht[idx & 0xf];
		}
	} else if (rate->flags & IEEE80211_TX_RC_MCS) {
		max_txpwr = dev->mt76.rate_power.ht[rate->idx & 0xf];
	} else {
		enum nl80211_band band = dev->mt76.chandef.chan->band;

		if (band == NL80211_BAND_2GHZ) {
			const struct ieee80211_rate *r;
			struct wiphy *wiphy = dev->mt76.hw->wiphy;
			struct mt76_rate_power *rp = &dev->mt76.rate_power;

			r = &wiphy->bands[band]->bitrates[rate->idx];
			if (r->flags & IEEE80211_RATE_SHORT_PREAMBLE)
				max_txpwr = rp->cck[r->hw_value & 0x3];
			else
				max_txpwr = rp->ofdm[r->hw_value & 0x7];
		} else {
			max_txpwr = dev->mt76.rate_power.ofdm[rate->idx & 0x7];
		}
	}

	return max_txpwr;
}

s8 mt76x02_tx_get_txpwr_adj(struct mt76x02_dev *dev, s8 txpwr, s8 max_txpwr_adj)
{
	txpwr = min_t(s8, txpwr, dev->mt76.txpower_conf);
	txpwr -= (dev->target_power + dev->target_power_delta[0]);
	txpwr = min_t(s8, txpwr, max_txpwr_adj);

	if (!dev->enable_tpc)
		return 0;
	else if (txpwr >= 0)
		return min_t(s8, txpwr, 7);
	else
		return (txpwr < -16) ? 8 : (txpwr + 32) / 2;
}

void mt76x02_tx_set_txpwr_auto(struct mt76x02_dev *dev, s8 txpwr)
{
	s8 txpwr_adj;

	txpwr_adj = mt76x02_tx_get_txpwr_adj(dev, txpwr,
					     dev->mt76.rate_power.ofdm[4]);
	mt76_rmw_field(dev, MT_PROT_AUTO_TX_CFG,
		       MT_PROT_AUTO_TX_CFG_PROT_PADJ, txpwr_adj);
	mt76_rmw_field(dev, MT_PROT_AUTO_TX_CFG,
		       MT_PROT_AUTO_TX_CFG_AUTO_PADJ, txpwr_adj);
}
EXPORT_SYMBOL_GPL(mt76x02_tx_set_txpwr_auto);

bool mt76x02_tx_status_data(struct mt76_dev *mdev, u8 *update)
{
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	struct mt76x02_tx_status stat;

	if (!mt76x02_mac_load_tx_status(dev, &stat))
		return false;

	mt76x02_send_tx_status(dev, &stat, update);

	return true;
}
EXPORT_SYMBOL_GPL(mt76x02_tx_status_data);

int mt76x02_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			   enum mt76_txq_id qid, struct mt76_wcid *wcid,
			   struct ieee80211_sta *sta,
			   struct mt76_tx_info *tx_info)
{
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx_info->skb->data;
	struct mt76x02_txwi *txwi = txwi_ptr;
	int hdrlen, len, pid, qsel = MT_QSEL_EDCA;

	if (qid == MT_TXQ_PSD && wcid && wcid->idx < 128)
		mt76x02_mac_wcid_set_drop(dev, wcid->idx, false);

	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	len = tx_info->skb->len - (hdrlen & 2);
	mt76x02_mac_write_txwi(dev, txwi, tx_info->skb, wcid, sta, len);

	pid = mt76_tx_status_skb_add(mdev, wcid, tx_info->skb);
	txwi->pktid = pid;

	if (pid >= MT_PACKET_ID_FIRST)
		qsel = MT_QSEL_MGMT;

	tx_info->info = FIELD_PREP(MT_TXD_INFO_QSEL, qsel) |
			MT_TXD_INFO_80211;

	if (!wcid || wcid->hw_key_idx == 0xff || wcid->sw_iv)
		tx_info->info |= MT_TXD_INFO_WIV;

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_tx_prepare_skb);
