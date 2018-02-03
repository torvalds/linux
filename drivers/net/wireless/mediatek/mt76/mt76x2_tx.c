/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
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

#include "mt76x2.h"
#include "mt76x2_dma.h"

struct beacon_bc_data {
	struct mt76x2_dev *dev;
	struct sk_buff_head q;
	struct sk_buff *tail[8];
};

void mt76x2_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
	     struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct mt76x2_dev *dev = hw->priv;
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_wcid *wcid = &dev->global_wcid;

	if (control->sta) {
		struct mt76x2_sta *msta;

		msta = (struct mt76x2_sta *) control->sta->drv_priv;
		wcid = &msta->wcid;
	}

	if (vif || (!info->control.hw_key && wcid->hw_key_idx != -1)) {
		struct mt76x2_vif *mvif;

		mvif = (struct mt76x2_vif *) vif->drv_priv;
		wcid = &mvif->group_wcid;
	}

	mt76_tx(&dev->mt76, control->sta, wcid, skb);
}

void mt76x2_tx_complete(struct mt76x2_dev *dev, struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	if (info->flags & IEEE80211_TX_CTL_AMPDU) {
		ieee80211_free_txskb(mt76_hw(dev), skb);
	} else {
		ieee80211_tx_info_clear_status(info);
		info->status.rates[0].idx = -1;
		info->flags |= IEEE80211_TX_STAT_ACK;
		ieee80211_tx_status(mt76_hw(dev), skb);
	}
}

s8 mt76x2_tx_get_max_txpwr_adj(struct mt76x2_dev *dev,
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
		enum nl80211_band band = dev->mt76.chandef.chan->band;

		if (band == NL80211_BAND_2GHZ) {
			const struct ieee80211_rate *r;
			struct wiphy *wiphy = mt76_hw(dev)->wiphy;
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

s8 mt76x2_tx_get_txpwr_adj(struct mt76x2_dev *dev, s8 txpwr, s8 max_txpwr_adj)
{
	txpwr = min_t(s8, txpwr, dev->txpower_conf);
	txpwr -= (dev->target_power + dev->target_power_delta[0]);
	txpwr = min_t(s8, txpwr, max_txpwr_adj);

	if (!dev->enable_tpc)
		return 0;
	else if (txpwr >= 0)
		return min_t(s8, txpwr, 7);
	else
		return (txpwr < -16) ? 8 : (txpwr + 32) / 2;
}

void mt76x2_tx_set_txpwr_auto(struct mt76x2_dev *dev, s8 txpwr)
{
	s8 txpwr_adj;

	txpwr_adj = mt76x2_tx_get_txpwr_adj(dev, txpwr,
					    dev->rate_power.ofdm[4]);
	mt76_rmw_field(dev, MT_PROT_AUTO_TX_CFG,
		       MT_PROT_AUTO_TX_CFG_PROT_PADJ, txpwr_adj);
	mt76_rmw_field(dev, MT_PROT_AUTO_TX_CFG,
		       MT_PROT_AUTO_TX_CFG_AUTO_PADJ, txpwr_adj);
}

static int mt76x2_insert_hdr_pad(struct sk_buff *skb)
{
	int len = ieee80211_get_hdrlen_from_skb(skb);

	if (len % 4 == 0)
		return 0;

	skb_push(skb, 2);
	memmove(skb->data, skb->data + 2, len);

	skb->data[len] = 0;
	skb->data[len + 1] = 0;
	return 2;
}

int mt76x2_tx_prepare_skb(struct mt76_dev *mdev, void *txwi,
			  struct sk_buff *skb, struct mt76_queue *q,
			  struct mt76_wcid *wcid, struct ieee80211_sta *sta,
			  u32 *tx_info)
{
	struct mt76x2_dev *dev = container_of(mdev, struct mt76x2_dev, mt76);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int qsel = MT_QSEL_EDCA;
	int ret;

	if (q == &dev->mt76.q_tx[MT_TXQ_PSD] && wcid && wcid->idx < 128)
		mt76x2_mac_wcid_set_drop(dev, wcid->idx, false);

	mt76x2_mac_write_txwi(dev, txwi, skb, wcid, sta);

	ret = mt76x2_insert_hdr_pad(skb);
	if (ret < 0)
		return ret;

	if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
		qsel = MT_QSEL_MGMT;

	*tx_info = FIELD_PREP(MT_TXD_INFO_QSEL, qsel) |
		   MT_TXD_INFO_80211;

	if (!wcid || wcid->hw_key_idx == 0xff || wcid->sw_iv)
		*tx_info |= MT_TXD_INFO_WIV;

	return 0;
}

static void
mt76x2_update_beacon_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt76x2_dev *dev = (struct mt76x2_dev *) priv;
	struct mt76x2_vif *mvif = (struct mt76x2_vif *) vif->drv_priv;
	struct sk_buff *skb = NULL;

	if (!(dev->beacon_mask & BIT(mvif->idx)))
		return;

	skb = ieee80211_beacon_get(mt76_hw(dev), vif);
	if (!skb)
		return;

	mt76x2_mac_set_beacon(dev, mvif->idx, skb);
}

static void
mt76x2_add_buffered_bc(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct beacon_bc_data *data = priv;
	struct mt76x2_dev *dev = data->dev;
	struct mt76x2_vif *mvif = (struct mt76x2_vif *) vif->drv_priv;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb;

	if (!(dev->beacon_mask & BIT(mvif->idx)))
		return;

	skb = ieee80211_get_buffered_bc(mt76_hw(dev), vif);
	if (!skb)
		return;

	info = IEEE80211_SKB_CB(skb);
	info->control.vif = vif;
	info->flags |= IEEE80211_TX_CTL_ASSIGN_SEQ;
	mt76_skb_set_moredata(skb, true);
	__skb_queue_tail(&data->q, skb);
	data->tail[mvif->idx] = skb;
}

void mt76x2_pre_tbtt_tasklet(unsigned long arg)
{
	struct mt76x2_dev *dev = (struct mt76x2_dev *) arg;
	struct mt76_queue *q = &dev->mt76.q_tx[MT_TXQ_PSD];
	struct beacon_bc_data data = {};
	struct sk_buff *skb;
	int i, nframes;

	data.dev = dev;
	__skb_queue_head_init(&data.q);

	ieee80211_iterate_active_interfaces_atomic(mt76_hw(dev),
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt76x2_update_beacon_iter, dev);

	do {
		nframes = skb_queue_len(&data.q);
		ieee80211_iterate_active_interfaces_atomic(mt76_hw(dev),
			IEEE80211_IFACE_ITER_RESUME_ALL,
			mt76x2_add_buffered_bc, &data);
	} while (nframes != skb_queue_len(&data.q));

	if (!nframes)
		return;

	for (i = 0; i < ARRAY_SIZE(data.tail); i++) {
		if (!data.tail[i])
			continue;

		mt76_skb_set_moredata(data.tail[i], false);
	}

	spin_lock_bh(&q->lock);
	while ((skb = __skb_dequeue(&data.q)) != NULL) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
		struct ieee80211_vif *vif = info->control.vif;
		struct mt76x2_vif *mvif = (struct mt76x2_vif *) vif->drv_priv;

		mt76_tx_queue_skb(&dev->mt76, q, skb, &mvif->group_wcid, NULL);
	}
	spin_unlock_bh(&q->lock);
}

