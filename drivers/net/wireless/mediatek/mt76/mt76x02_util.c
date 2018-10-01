/*
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
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

#include <linux/module.h>
#include "mt76.h"
#include "mt76x02_dma.h"
#include "mt76x02_regs.h"
#include "mt76x02_mac.h"

#define CCK_RATE(_idx, _rate) {					\
	.bitrate = _rate,					\
	.flags = IEEE80211_RATE_SHORT_PREAMBLE,			\
	.hw_value = (MT_PHY_TYPE_CCK << 8) | _idx,		\
	.hw_value_short = (MT_PHY_TYPE_CCK << 8) | (8 + _idx),	\
}

#define OFDM_RATE(_idx, _rate) {				\
	.bitrate = _rate,					\
	.hw_value = (MT_PHY_TYPE_OFDM << 8) | _idx,		\
	.hw_value_short = (MT_PHY_TYPE_OFDM << 8) | _idx,	\
}

struct ieee80211_rate mt76x02_rates[] = {
	CCK_RATE(0, 10),
	CCK_RATE(1, 20),
	CCK_RATE(2, 55),
	CCK_RATE(3, 110),
	OFDM_RATE(0, 60),
	OFDM_RATE(1, 90),
	OFDM_RATE(2, 120),
	OFDM_RATE(3, 180),
	OFDM_RATE(4, 240),
	OFDM_RATE(5, 360),
	OFDM_RATE(6, 480),
	OFDM_RATE(7, 540),
};
EXPORT_SYMBOL_GPL(mt76x02_rates);

void mt76x02_configure_filter(struct ieee80211_hw *hw,
			     unsigned int changed_flags,
			     unsigned int *total_flags, u64 multicast)
{
	struct mt76_dev *dev = hw->priv;
	u32 flags = 0;

#define MT76_FILTER(_flag, _hw) do { \
		flags |= *total_flags & FIF_##_flag;			\
		dev->rxfilter &= ~(_hw);				\
		dev->rxfilter |= !(flags & FIF_##_flag) * (_hw);	\
	} while (0)

	mutex_lock(&dev->mutex);

	dev->rxfilter &= ~MT_RX_FILTR_CFG_OTHER_BSS;

	MT76_FILTER(FCSFAIL, MT_RX_FILTR_CFG_CRC_ERR);
	MT76_FILTER(PLCPFAIL, MT_RX_FILTR_CFG_PHY_ERR);
	MT76_FILTER(CONTROL, MT_RX_FILTR_CFG_ACK |
			     MT_RX_FILTR_CFG_CTS |
			     MT_RX_FILTR_CFG_CFEND |
			     MT_RX_FILTR_CFG_CFACK |
			     MT_RX_FILTR_CFG_BA |
			     MT_RX_FILTR_CFG_CTRL_RSV);
	MT76_FILTER(PSPOLL, MT_RX_FILTR_CFG_PSPOLL);

	*total_flags = flags;
	dev->bus->wr(dev, MT_RX_FILTR_CFG, dev->rxfilter);

	mutex_unlock(&dev->mutex);
}
EXPORT_SYMBOL_GPL(mt76x02_configure_filter);

int mt76x02_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta)
{
	struct mt76_dev *dev = hw->priv;
	struct mt76x02_sta *msta = (struct mt76x02_sta *) sta->drv_priv;
	struct mt76x02_vif *mvif = (struct mt76x02_vif *) vif->drv_priv;
	int ret = 0;
	int idx = 0;
	int i;

	mutex_lock(&dev->mutex);

	idx = mt76_wcid_alloc(dev->wcid_mask, ARRAY_SIZE(dev->wcid));
	if (idx < 0) {
		ret = -ENOSPC;
		goto out;
	}

	msta->vif = mvif;
	msta->wcid.sta = 1;
	msta->wcid.idx = idx;
	msta->wcid.hw_key_idx = -1;
	mt76x02_mac_wcid_setup(dev, idx, mvif->idx, sta->addr);
	mt76x02_mac_wcid_set_drop(dev, idx, false);
	for (i = 0; i < ARRAY_SIZE(sta->txq); i++)
		mt76x02_txq_init(dev, sta->txq[i]);

	if (vif->type == NL80211_IFTYPE_AP)
		set_bit(MT_WCID_FLAG_CHECK_PS, &msta->wcid.flags);

	ewma_signal_init(&msta->rssi);

	rcu_assign_pointer(dev->wcid[idx], &msta->wcid);

out:
	mutex_unlock(&dev->mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76x02_sta_add);

int mt76x02_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta)
{
	struct mt76_dev *dev = hw->priv;
	struct mt76x02_sta *msta = (struct mt76x02_sta *) sta->drv_priv;
	int idx = msta->wcid.idx;
	int i;

	mutex_lock(&dev->mutex);
	rcu_assign_pointer(dev->wcid[idx], NULL);
	for (i = 0; i < ARRAY_SIZE(sta->txq); i++)
		mt76_txq_remove(dev, sta->txq[i]);
	mt76x02_mac_wcid_set_drop(dev, idx, true);
	mt76_wcid_free(dev->wcid_mask, idx);
	mt76x02_mac_wcid_setup(dev, idx, 0, NULL);
	mutex_unlock(&dev->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_sta_remove);

void mt76x02_vif_init(struct mt76_dev *dev, struct ieee80211_vif *vif,
		     unsigned int idx)
{
	struct mt76x02_vif *mvif = (struct mt76x02_vif *) vif->drv_priv;

	mvif->idx = idx;
	mvif->group_wcid.idx = MT_VIF_WCID(idx);
	mvif->group_wcid.hw_key_idx = -1;
	mt76x02_txq_init(dev, vif->txq);
}
EXPORT_SYMBOL_GPL(mt76x02_vif_init);

int
mt76x02_add_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt76_dev *dev = hw->priv;
	unsigned int idx = 0;

	if (vif->addr[0] & BIT(1))
		idx = 1 + (((dev->macaddr[0] ^ vif->addr[0]) >> 2) & 7);

	/*
	 * Client mode typically only has one configurable BSSID register,
	 * which is used for bssidx=0. This is linked to the MAC address.
	 * Since mac80211 allows changing interface types, and we cannot
	 * force the use of the primary MAC address for a station mode
	 * interface, we need some other way of configuring a per-interface
	 * remote BSSID.
	 * The hardware provides an AP-Client feature, where bssidx 0-7 are
	 * used for AP mode and bssidx 8-15 for client mode.
	 * We shift the station interface bss index by 8 to force the
	 * hardware to recognize the BSSID.
	 * The resulting bssidx mismatch for unicast frames is ignored by hw.
	 */
	if (vif->type == NL80211_IFTYPE_STATION)
		idx += 8;

	mt76x02_vif_init(dev, vif, idx);
	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_add_interface);

void mt76x02_remove_interface(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif)
{
	struct mt76_dev *dev = hw->priv;

	mt76_txq_remove(dev, vif->txq);
}
EXPORT_SYMBOL_GPL(mt76x02_remove_interface);

int mt76x02_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_ampdu_params *params)
{
	enum ieee80211_ampdu_mlme_action action = params->action;
	struct ieee80211_sta *sta = params->sta;
	struct mt76_dev *dev = hw->priv;
	struct mt76x02_sta *msta = (struct mt76x02_sta *) sta->drv_priv;
	struct ieee80211_txq *txq = sta->txq[params->tid];
	u16 tid = params->tid;
	u16 *ssn = &params->ssn;
	struct mt76_txq *mtxq;

	if (!txq)
		return -EINVAL;

	mtxq = (struct mt76_txq *)txq->drv_priv;

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		mt76_rx_aggr_start(dev, &msta->wcid, tid, *ssn, params->buf_size);
		__mt76_set(dev, MT_WCID_ADDR(msta->wcid.idx) + 4, BIT(16 + tid));
		break;
	case IEEE80211_AMPDU_RX_STOP:
		mt76_rx_aggr_stop(dev, &msta->wcid, tid);
		__mt76_clear(dev, MT_WCID_ADDR(msta->wcid.idx) + 4, BIT(16 + tid));
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		mtxq->aggr = true;
		mtxq->send_bar = false;
		ieee80211_send_bar(vif, sta->addr, tid, mtxq->agg_ssn);
		break;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		mtxq->aggr = false;
		ieee80211_send_bar(vif, sta->addr, tid, mtxq->agg_ssn);
		break;
	case IEEE80211_AMPDU_TX_START:
		mtxq->agg_ssn = *ssn << 4;
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
		mtxq->aggr = false;
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_ampdu_action);

int mt76x02_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		   struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		   struct ieee80211_key_conf *key)
{
	struct mt76_dev *dev = hw->priv;
	struct mt76x02_vif *mvif = (struct mt76x02_vif *) vif->drv_priv;
	struct mt76x02_sta *msta;
	struct mt76_wcid *wcid;
	int idx = key->keyidx;
	int ret;

	/* fall back to sw encryption for unsupported ciphers */
	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
		break;
	default:
		return -EOPNOTSUPP;
	}

	/*
	 * The hardware does not support per-STA RX GTK, fall back
	 * to software mode for these.
	 */
	if ((vif->type == NL80211_IFTYPE_ADHOC ||
	     vif->type == NL80211_IFTYPE_MESH_POINT) &&
	    (key->cipher == WLAN_CIPHER_SUITE_TKIP ||
	     key->cipher == WLAN_CIPHER_SUITE_CCMP) &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return -EOPNOTSUPP;

	msta = sta ? (struct mt76x02_sta *) sta->drv_priv : NULL;
	wcid = msta ? &msta->wcid : &mvif->group_wcid;

	if (cmd == SET_KEY) {
		key->hw_key_idx = wcid->idx;
		wcid->hw_key_idx = idx;
		if (key->flags & IEEE80211_KEY_FLAG_RX_MGMT) {
			key->flags |= IEEE80211_KEY_FLAG_SW_MGMT_TX;
			wcid->sw_iv = true;
		}
	} else {
		if (idx == wcid->hw_key_idx) {
			wcid->hw_key_idx = -1;
			wcid->sw_iv = true;
		}

		key = NULL;
	}
	mt76_wcid_key_setup(dev, wcid, key);

	if (!msta) {
		if (key || wcid->hw_key_idx == idx) {
			ret = mt76x02_mac_wcid_set_key(dev, wcid->idx, key);
			if (ret)
				return ret;
		}

		return mt76x02_mac_shared_key_setup(dev, mvif->idx, idx, key);
	}

	return mt76x02_mac_wcid_set_key(dev, msta->wcid.idx, key);
}
EXPORT_SYMBOL_GPL(mt76x02_set_key);

int mt76x02_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   u16 queue, const struct ieee80211_tx_queue_params *params)
{
	struct mt76_dev *dev = hw->priv;
	u8 cw_min = 5, cw_max = 10, qid;
	u32 val;

	qid = dev->q_tx[queue].hw_idx;

	if (params->cw_min)
		cw_min = fls(params->cw_min);
	if (params->cw_max)
		cw_max = fls(params->cw_max);

	val = FIELD_PREP(MT_EDCA_CFG_TXOP, params->txop) |
	      FIELD_PREP(MT_EDCA_CFG_AIFSN, params->aifs) |
	      FIELD_PREP(MT_EDCA_CFG_CWMIN, cw_min) |
	      FIELD_PREP(MT_EDCA_CFG_CWMAX, cw_max);
	__mt76_wr(dev, MT_EDCA_CFG_AC(qid), val);

	val = __mt76_rr(dev, MT_WMM_TXOP(qid));
	val &= ~(MT_WMM_TXOP_MASK << MT_WMM_TXOP_SHIFT(qid));
	val |= params->txop << MT_WMM_TXOP_SHIFT(qid);
	__mt76_wr(dev, MT_WMM_TXOP(qid), val);

	val = __mt76_rr(dev, MT_WMM_AIFSN);
	val &= ~(MT_WMM_AIFSN_MASK << MT_WMM_AIFSN_SHIFT(qid));
	val |= params->aifs << MT_WMM_AIFSN_SHIFT(qid);
	__mt76_wr(dev, MT_WMM_AIFSN, val);

	val = __mt76_rr(dev, MT_WMM_CWMIN);
	val &= ~(MT_WMM_CWMIN_MASK << MT_WMM_CWMIN_SHIFT(qid));
	val |= cw_min << MT_WMM_CWMIN_SHIFT(qid);
	__mt76_wr(dev, MT_WMM_CWMIN, val);

	val = __mt76_rr(dev, MT_WMM_CWMAX);
	val &= ~(MT_WMM_CWMAX_MASK << MT_WMM_CWMAX_SHIFT(qid));
	val |= cw_max << MT_WMM_CWMAX_SHIFT(qid);
	__mt76_wr(dev, MT_WMM_CWMAX, val);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_conf_tx);

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

void mt76x02_sta_rate_tbl_update(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta)
{
	struct mt76_dev *dev = hw->priv;
	struct mt76x02_sta *msta = (struct mt76x02_sta *) sta->drv_priv;
	struct ieee80211_sta_rates *rates = rcu_dereference(sta->rates);
	struct ieee80211_tx_rate rate = {};

	if (!rates)
		return;

	rate.idx = rates->rate[0].idx;
	rate.flags = rates->rate[0].flags;
	mt76x02_mac_wcid_set_rate(dev, &msta->wcid, &rate);
	msta->wcid.max_txpwr_adj = mt76x02_tx_get_max_txpwr_adj(dev, &rate);
}
EXPORT_SYMBOL_GPL(mt76x02_sta_rate_tbl_update);

int mt76x02_insert_hdr_pad(struct sk_buff *skb)
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
EXPORT_SYMBOL_GPL(mt76x02_insert_hdr_pad);

void mt76x02_remove_hdr_pad(struct sk_buff *skb, int len)
{
	int hdrlen;

	if (!len)
		return;

	hdrlen = ieee80211_get_hdrlen_from_skb(skb);
	memmove(skb->data + len, skb->data, hdrlen);
	skb_pull(skb, len);
}
EXPORT_SYMBOL_GPL(mt76x02_remove_hdr_pad);

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

const u16 mt76x02_beacon_offsets[16] = {
	/* 1024 byte per beacon */
	0xc000,
	0xc400,
	0xc800,
	0xcc00,
	0xd000,
	0xd400,
	0xd800,
	0xdc00,
	/* BSS idx 8-15 not used for beacons */
	0xc000,
	0xc000,
	0xc000,
	0xc000,
	0xc000,
	0xc000,
	0xc000,
	0xc000,
};
EXPORT_SYMBOL_GPL(mt76x02_beacon_offsets);

void mt76x02_set_beacon_offsets(struct mt76_dev *dev)
{
	u16 val, base = MT_BEACON_BASE;
	u32 regs[4] = {};
	int i;

	for (i = 0; i < 16; i++) {
		val = mt76x02_beacon_offsets[i] - base;
		regs[i / 4] |= (val / 64) << (8 * (i % 4));
	}

	for (i = 0; i < 4; i++)
		__mt76_wr(dev, MT_BCN_OFFSET(i), regs[i]);
}
EXPORT_SYMBOL_GPL(mt76x02_set_beacon_offsets);

MODULE_LICENSE("Dual BSD/GPL");
