// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 */

#include "mt76x02.h"
#include "mt76x02_trace.h"
#include "trace.h"

void mt76x02_mac_reset_counters(struct mt76x02_dev *dev)
{
	int i;

	mt76_rr(dev, MT_RX_STAT_0);
	mt76_rr(dev, MT_RX_STAT_1);
	mt76_rr(dev, MT_RX_STAT_2);
	mt76_rr(dev, MT_TX_STA_0);
	mt76_rr(dev, MT_TX_STA_1);
	mt76_rr(dev, MT_TX_STA_2);

	for (i = 0; i < 16; i++)
		mt76_rr(dev, MT_TX_AGG_CNT(i));

	for (i = 0; i < 16; i++)
		mt76_rr(dev, MT_TX_STAT_FIFO);

	memset(dev->mt76.aggr_stats, 0, sizeof(dev->mt76.aggr_stats));
}
EXPORT_SYMBOL_GPL(mt76x02_mac_reset_counters);

static enum mt76x02_cipher_type
mt76x02_mac_get_key_info(struct ieee80211_key_conf *key, u8 *key_data)
{
	memset(key_data, 0, 32);
	if (!key)
		return MT76X02_CIPHER_NONE;

	if (key->keylen > 32)
		return MT76X02_CIPHER_NONE;

	memcpy(key_data, key->key, key->keylen);

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		return MT76X02_CIPHER_WEP40;
	case WLAN_CIPHER_SUITE_WEP104:
		return MT76X02_CIPHER_WEP104;
	case WLAN_CIPHER_SUITE_TKIP:
		return MT76X02_CIPHER_TKIP;
	case WLAN_CIPHER_SUITE_CCMP:
		return MT76X02_CIPHER_AES_CCMP;
	default:
		return MT76X02_CIPHER_NONE;
	}
}

int mt76x02_mac_shared_key_setup(struct mt76x02_dev *dev, u8 vif_idx,
				 u8 key_idx, struct ieee80211_key_conf *key)
{
	enum mt76x02_cipher_type cipher;
	u8 key_data[32];
	u32 val;

	cipher = mt76x02_mac_get_key_info(key, key_data);
	if (cipher == MT76X02_CIPHER_NONE && key)
		return -EOPNOTSUPP;

	val = mt76_rr(dev, MT_SKEY_MODE(vif_idx));
	val &= ~(MT_SKEY_MODE_MASK << MT_SKEY_MODE_SHIFT(vif_idx, key_idx));
	val |= cipher << MT_SKEY_MODE_SHIFT(vif_idx, key_idx);
	mt76_wr(dev, MT_SKEY_MODE(vif_idx), val);

	mt76_wr_copy(dev, MT_SKEY(vif_idx, key_idx), key_data,
		     sizeof(key_data));

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_mac_shared_key_setup);

void mt76x02_mac_wcid_sync_pn(struct mt76x02_dev *dev, u8 idx,
			      struct ieee80211_key_conf *key)
{
	enum mt76x02_cipher_type cipher;
	u8 key_data[32];
	u32 iv, eiv;
	u64 pn;

	cipher = mt76x02_mac_get_key_info(key, key_data);
	iv = mt76_rr(dev, MT_WCID_IV(idx));
	eiv = mt76_rr(dev, MT_WCID_IV(idx) + 4);

	pn = (u64)eiv << 16;
	if (cipher == MT76X02_CIPHER_TKIP) {
		pn |= (iv >> 16) & 0xff;
		pn |= (iv & 0xff) << 8;
	} else if (cipher >= MT76X02_CIPHER_AES_CCMP) {
		pn |= iv & 0xffff;
	} else {
		return;
	}

	atomic64_set(&key->tx_pn, pn);
}

int mt76x02_mac_wcid_set_key(struct mt76x02_dev *dev, u8 idx,
			     struct ieee80211_key_conf *key)
{
	enum mt76x02_cipher_type cipher;
	u8 key_data[32];
	u8 iv_data[8];
	u64 pn;

	cipher = mt76x02_mac_get_key_info(key, key_data);
	if (cipher == MT76X02_CIPHER_NONE && key)
		return -EOPNOTSUPP;

	mt76_wr_copy(dev, MT_WCID_KEY(idx), key_data, sizeof(key_data));
	mt76_rmw_field(dev, MT_WCID_ATTR(idx), MT_WCID_ATTR_PKEY_MODE, cipher);

	memset(iv_data, 0, sizeof(iv_data));
	if (key) {
		mt76_rmw_field(dev, MT_WCID_ATTR(idx), MT_WCID_ATTR_PAIRWISE,
			       !!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE));

		pn = atomic64_read(&key->tx_pn);

		iv_data[3] = key->keyidx << 6;
		if (cipher >= MT76X02_CIPHER_TKIP) {
			iv_data[3] |= 0x20;
			put_unaligned_le32(pn >> 16, &iv_data[4]);
		}

		if (cipher == MT76X02_CIPHER_TKIP) {
			iv_data[0] = (pn >> 8) & 0xff;
			iv_data[1] = (iv_data[0] | 0x20) & 0x7f;
			iv_data[2] = pn & 0xff;
		} else if (cipher >= MT76X02_CIPHER_AES_CCMP) {
			put_unaligned_le16((pn & 0xffff), &iv_data[0]);
		}
	}

	mt76_wr_copy(dev, MT_WCID_IV(idx), iv_data, sizeof(iv_data));

	return 0;
}

void mt76x02_mac_wcid_setup(struct mt76x02_dev *dev, u8 idx,
			    u8 vif_idx, u8 *mac)
{
	struct mt76_wcid_addr addr = {};
	u32 attr;

	attr = FIELD_PREP(MT_WCID_ATTR_BSS_IDX, vif_idx & 7) |
	       FIELD_PREP(MT_WCID_ATTR_BSS_IDX_EXT, !!(vif_idx & 8));

	mt76_wr(dev, MT_WCID_ATTR(idx), attr);

	if (idx >= 128)
		return;

	if (mac)
		memcpy(addr.macaddr, mac, ETH_ALEN);

	mt76_wr_copy(dev, MT_WCID_ADDR(idx), &addr, sizeof(addr));
}
EXPORT_SYMBOL_GPL(mt76x02_mac_wcid_setup);

void mt76x02_mac_wcid_set_drop(struct mt76x02_dev *dev, u8 idx, bool drop)
{
	u32 val = mt76_rr(dev, MT_WCID_DROP(idx));
	u32 bit = MT_WCID_DROP_MASK(idx);

	/* prevent unnecessary writes */
	if ((val & bit) != (bit * drop))
		mt76_wr(dev, MT_WCID_DROP(idx), (val & ~bit) | (bit * drop));
}

static u16
mt76x02_mac_tx_rate_val(struct mt76x02_dev *dev,
			const struct ieee80211_tx_rate *rate, u8 *nss_val)
{
	u8 phy, rate_idx, nss, bw = 0;
	u16 rateval;

	if (rate->flags & IEEE80211_TX_RC_VHT_MCS) {
		rate_idx = rate->idx;
		nss = 1 + (rate->idx >> 4);
		phy = MT_PHY_TYPE_VHT;
		if (rate->flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
			bw = 2;
		else if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			bw = 1;
	} else if (rate->flags & IEEE80211_TX_RC_MCS) {
		rate_idx = rate->idx;
		nss = 1 + (rate->idx >> 3);
		phy = MT_PHY_TYPE_HT;
		if (rate->flags & IEEE80211_TX_RC_GREEN_FIELD)
			phy = MT_PHY_TYPE_HT_GF;
		if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			bw = 1;
	} else {
		const struct ieee80211_rate *r;
		int band = dev->mphy.chandef.chan->band;
		u16 val;

		r = &dev->mt76.hw->wiphy->bands[band]->bitrates[rate->idx];
		if (rate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
			val = r->hw_value_short;
		else
			val = r->hw_value;

		phy = val >> 8;
		rate_idx = val & 0xff;
		nss = 1;
	}

	rateval = FIELD_PREP(MT_RXWI_RATE_INDEX, rate_idx);
	rateval |= FIELD_PREP(MT_RXWI_RATE_PHY, phy);
	rateval |= FIELD_PREP(MT_RXWI_RATE_BW, bw);
	if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
		rateval |= MT_RXWI_RATE_SGI;

	*nss_val = nss;
	return rateval;
}

void mt76x02_mac_wcid_set_rate(struct mt76x02_dev *dev, struct mt76_wcid *wcid,
			       const struct ieee80211_tx_rate *rate)
{
	s8 max_txpwr_adj = mt76x02_tx_get_max_txpwr_adj(dev, rate);
	u16 rateval;
	u32 tx_info;
	s8 nss;

	rateval = mt76x02_mac_tx_rate_val(dev, rate, &nss);
	tx_info = FIELD_PREP(MT_WCID_TX_INFO_RATE, rateval) |
		  FIELD_PREP(MT_WCID_TX_INFO_NSS, nss) |
		  FIELD_PREP(MT_WCID_TX_INFO_TXPWR_ADJ, max_txpwr_adj) |
		  MT_WCID_TX_INFO_SET;
	wcid->tx_info = tx_info;
}

void mt76x02_mac_set_short_preamble(struct mt76x02_dev *dev, bool enable)
{
	if (enable)
		mt76_set(dev, MT_AUTO_RSP_CFG, MT_AUTO_RSP_PREAMB_SHORT);
	else
		mt76_clear(dev, MT_AUTO_RSP_CFG, MT_AUTO_RSP_PREAMB_SHORT);
}

bool mt76x02_mac_load_tx_status(struct mt76x02_dev *dev,
				struct mt76x02_tx_status *stat)
{
	u32 stat1, stat2;

	stat2 = mt76_rr(dev, MT_TX_STAT_FIFO_EXT);
	stat1 = mt76_rr(dev, MT_TX_STAT_FIFO);

	stat->valid = !!(stat1 & MT_TX_STAT_FIFO_VALID);
	if (!stat->valid)
		return false;

	stat->success = !!(stat1 & MT_TX_STAT_FIFO_SUCCESS);
	stat->aggr = !!(stat1 & MT_TX_STAT_FIFO_AGGR);
	stat->ack_req = !!(stat1 & MT_TX_STAT_FIFO_ACKREQ);
	stat->wcid = FIELD_GET(MT_TX_STAT_FIFO_WCID, stat1);
	stat->rate = FIELD_GET(MT_TX_STAT_FIFO_RATE, stat1);

	stat->retry = FIELD_GET(MT_TX_STAT_FIFO_EXT_RETRY, stat2);
	stat->pktid = FIELD_GET(MT_TX_STAT_FIFO_EXT_PKTID, stat2);

	trace_mac_txstat_fetch(dev, stat);

	return true;
}

static int
mt76x02_mac_process_tx_rate(struct ieee80211_tx_rate *txrate, u16 rate,
			    enum nl80211_band band)
{
	u8 idx = FIELD_GET(MT_RXWI_RATE_INDEX, rate);

	txrate->idx = 0;
	txrate->flags = 0;
	txrate->count = 1;

	switch (FIELD_GET(MT_RXWI_RATE_PHY, rate)) {
	case MT_PHY_TYPE_OFDM:
		if (band == NL80211_BAND_2GHZ)
			idx += 4;

		txrate->idx = idx;
		return 0;
	case MT_PHY_TYPE_CCK:
		if (idx >= 8)
			idx -= 8;

		txrate->idx = idx;
		return 0;
	case MT_PHY_TYPE_HT_GF:
		txrate->flags |= IEEE80211_TX_RC_GREEN_FIELD;
		fallthrough;
	case MT_PHY_TYPE_HT:
		txrate->flags |= IEEE80211_TX_RC_MCS;
		txrate->idx = idx;
		break;
	case MT_PHY_TYPE_VHT:
		txrate->flags |= IEEE80211_TX_RC_VHT_MCS;
		txrate->idx = idx;
		break;
	default:
		return -EINVAL;
	}

	switch (FIELD_GET(MT_RXWI_RATE_BW, rate)) {
	case MT_PHY_BW_20:
		break;
	case MT_PHY_BW_40:
		txrate->flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
		break;
	case MT_PHY_BW_80:
		txrate->flags |= IEEE80211_TX_RC_80_MHZ_WIDTH;
		break;
	default:
		return -EINVAL;
	}

	if (rate & MT_RXWI_RATE_SGI)
		txrate->flags |= IEEE80211_TX_RC_SHORT_GI;

	return 0;
}

void mt76x02_mac_write_txwi(struct mt76x02_dev *dev, struct mt76x02_txwi *txwi,
			    struct sk_buff *skb, struct mt76_wcid *wcid,
			    struct ieee80211_sta *sta, int len)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rate = &info->control.rates[0];
	struct ieee80211_key_conf *key = info->control.hw_key;
	u32 wcid_tx_info;
	u16 rate_ht_mask = FIELD_PREP(MT_RXWI_RATE_PHY, BIT(1) | BIT(2));
	u16 txwi_flags = 0, rateval;
	u8 nss;
	s8 txpwr_adj, max_txpwr_adj;
	u8 ccmp_pn[8], nstreams = dev->mphy.chainmask & 0xf;

	memset(txwi, 0, sizeof(*txwi));

	mt76_tx_check_agg_ssn(sta, skb);

	if (!info->control.hw_key && wcid && wcid->hw_key_idx != 0xff &&
	    ieee80211_has_protected(hdr->frame_control)) {
		wcid = NULL;
		ieee80211_get_tx_rates(info->control.vif, sta, skb,
				       info->control.rates, 1);
	}

	if (wcid)
		txwi->wcid = wcid->idx;
	else
		txwi->wcid = 0xff;

	if (wcid && wcid->sw_iv && key) {
		u64 pn = atomic64_inc_return(&key->tx_pn);

		ccmp_pn[0] = pn;
		ccmp_pn[1] = pn >> 8;
		ccmp_pn[2] = 0;
		ccmp_pn[3] = 0x20 | (key->keyidx << 6);
		ccmp_pn[4] = pn >> 16;
		ccmp_pn[5] = pn >> 24;
		ccmp_pn[6] = pn >> 32;
		ccmp_pn[7] = pn >> 40;
		txwi->iv = *((__le32 *)&ccmp_pn[0]);
		txwi->eiv = *((__le32 *)&ccmp_pn[4]);
	}

	if (wcid && (rate->idx < 0 || !rate->count)) {
		wcid_tx_info = wcid->tx_info;
		rateval = FIELD_GET(MT_WCID_TX_INFO_RATE, wcid_tx_info);
		max_txpwr_adj = FIELD_GET(MT_WCID_TX_INFO_TXPWR_ADJ,
					  wcid_tx_info);
		nss = FIELD_GET(MT_WCID_TX_INFO_NSS, wcid_tx_info);
	} else {
		rateval = mt76x02_mac_tx_rate_val(dev, rate, &nss);
		max_txpwr_adj = mt76x02_tx_get_max_txpwr_adj(dev, rate);
	}
	txwi->rate = cpu_to_le16(rateval);

	txpwr_adj = mt76x02_tx_get_txpwr_adj(dev, dev->txpower_conf,
					     max_txpwr_adj);
	txwi->ctl2 = FIELD_PREP(MT_TX_PWR_ADJ, txpwr_adj);

	if (nstreams > 1 && mt76_rev(&dev->mt76) >= MT76XX_REV_E4)
		txwi->txstream = 0x13;
	else if (nstreams > 1 && mt76_rev(&dev->mt76) >= MT76XX_REV_E3 &&
		 !(txwi->rate & cpu_to_le16(rate_ht_mask)))
		txwi->txstream = 0x93;

	if (is_mt76x2(dev) && (info->flags & IEEE80211_TX_CTL_LDPC))
		txwi->rate |= cpu_to_le16(MT_RXWI_RATE_LDPC);
	if ((info->flags & IEEE80211_TX_CTL_STBC) && nss == 1)
		txwi->rate |= cpu_to_le16(MT_RXWI_RATE_STBC);
	if (nss > 1 && sta && sta->smps_mode == IEEE80211_SMPS_DYNAMIC)
		txwi_flags |= MT_TXWI_FLAGS_MMPS;
	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
		txwi->ack_ctl |= MT_TXWI_ACK_CTL_REQ;
	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ)
		txwi->ack_ctl |= MT_TXWI_ACK_CTL_NSEQ;
	if ((info->flags & IEEE80211_TX_CTL_AMPDU) && sta) {
		u8 ba_size = IEEE80211_MIN_AMPDU_BUF;
		u8 ampdu_density = sta->ht_cap.ampdu_density;

		ba_size <<= sta->ht_cap.ampdu_factor;
		ba_size = min_t(int, 63, ba_size - 1);
		if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
			ba_size = 0;
		txwi->ack_ctl |= FIELD_PREP(MT_TXWI_ACK_CTL_BA_WINDOW, ba_size);

		if (ampdu_density < IEEE80211_HT_MPDU_DENSITY_4)
			ampdu_density = IEEE80211_HT_MPDU_DENSITY_4;

		txwi_flags |= MT_TXWI_FLAGS_AMPDU |
			 FIELD_PREP(MT_TXWI_FLAGS_MPDU_DENSITY, ampdu_density);
	}

	if (ieee80211_is_probe_resp(hdr->frame_control) ||
	    ieee80211_is_beacon(hdr->frame_control))
		txwi_flags |= MT_TXWI_FLAGS_TS;

	txwi->flags |= cpu_to_le16(txwi_flags);
	txwi->len_ctl = cpu_to_le16(len);
}
EXPORT_SYMBOL_GPL(mt76x02_mac_write_txwi);

static void
mt76x02_tx_rate_fallback(struct ieee80211_tx_rate *rates, int idx, int phy)
{
	u8 mcs, nss;

	if (!idx)
		return;

	rates += idx - 1;
	rates[1] = rates[0];
	switch (phy) {
	case MT_PHY_TYPE_VHT:
		mcs = ieee80211_rate_get_vht_mcs(rates);
		nss = ieee80211_rate_get_vht_nss(rates);

		if (mcs == 0)
			nss = max_t(int, nss - 1, 1);
		else
			mcs--;

		ieee80211_rate_set_vht(rates + 1, mcs, nss);
		break;
	case MT_PHY_TYPE_HT_GF:
	case MT_PHY_TYPE_HT:
		/* MCS 8 falls back to MCS 0 */
		if (rates[0].idx == 8) {
			rates[1].idx = 0;
			break;
		}
		fallthrough;
	default:
		rates[1].idx = max_t(int, rates[0].idx - 1, 0);
		break;
	}
}

static void
mt76x02_mac_fill_tx_status(struct mt76x02_dev *dev, struct mt76x02_sta *msta,
			   struct ieee80211_tx_info *info,
			   struct mt76x02_tx_status *st, int n_frames)
{
	struct ieee80211_tx_rate *rate = info->status.rates;
	struct ieee80211_tx_rate last_rate;
	u16 first_rate;
	int retry = st->retry;
	int phy;
	int i;

	if (!n_frames)
		return;

	phy = FIELD_GET(MT_RXWI_RATE_PHY, st->rate);

	if (st->pktid & MT_PACKET_ID_HAS_RATE) {
		first_rate = st->rate & ~MT_PKTID_RATE;
		first_rate |= st->pktid & MT_PKTID_RATE;

		mt76x02_mac_process_tx_rate(&rate[0], first_rate,
					    dev->mphy.chandef.chan->band);
	} else if (rate[0].idx < 0) {
		if (!msta)
			return;

		mt76x02_mac_process_tx_rate(&rate[0], msta->wcid.tx_info,
					    dev->mphy.chandef.chan->band);
	}

	mt76x02_mac_process_tx_rate(&last_rate, st->rate,
				    dev->mphy.chandef.chan->band);

	for (i = 0; i < ARRAY_SIZE(info->status.rates); i++) {
		retry--;
		if (i + 1 == ARRAY_SIZE(info->status.rates)) {
			info->status.rates[i] = last_rate;
			info->status.rates[i].count = max_t(int, retry, 1);
			break;
		}

		mt76x02_tx_rate_fallback(info->status.rates, i, phy);
		if (info->status.rates[i].idx == last_rate.idx)
			break;
	}

	if (i + 1 < ARRAY_SIZE(info->status.rates)) {
		info->status.rates[i + 1].idx = -1;
		info->status.rates[i + 1].count = 0;
	}

	info->status.ampdu_len = n_frames;
	info->status.ampdu_ack_len = st->success ? n_frames : 0;

	if (st->aggr)
		info->flags |= IEEE80211_TX_CTL_AMPDU |
			       IEEE80211_TX_STAT_AMPDU;

	if (!st->ack_req)
		info->flags |= IEEE80211_TX_CTL_NO_ACK;
	else if (st->success)
		info->flags |= IEEE80211_TX_STAT_ACK;
}

void mt76x02_send_tx_status(struct mt76x02_dev *dev,
			    struct mt76x02_tx_status *stat, u8 *update)
{
	struct ieee80211_tx_info info = {};
	struct ieee80211_tx_status status = {
		.info = &info
	};
	static const u8 ac_to_tid[4] = {
		[IEEE80211_AC_BE] = 0,
		[IEEE80211_AC_BK] = 1,
		[IEEE80211_AC_VI] = 4,
		[IEEE80211_AC_VO] = 6
	};
	struct mt76_wcid *wcid = NULL;
	struct mt76x02_sta *msta = NULL;
	struct mt76_dev *mdev = &dev->mt76;
	struct sk_buff_head list;
	u32 duration = 0;
	u8 cur_pktid;
	u32 ac = 0;
	int len = 0;

	if (stat->pktid == MT_PACKET_ID_NO_ACK)
		return;

	rcu_read_lock();

	if (stat->wcid < MT76x02_N_WCIDS)
		wcid = rcu_dereference(dev->mt76.wcid[stat->wcid]);

	if (wcid && wcid->sta) {
		void *priv;

		priv = msta = container_of(wcid, struct mt76x02_sta, wcid);
		status.sta = container_of(priv, struct ieee80211_sta,
					  drv_priv);
	}

	mt76_tx_status_lock(mdev, &list);

	if (wcid) {
		if (mt76_is_skb_pktid(stat->pktid))
			status.skb = mt76_tx_status_skb_get(mdev, wcid,
							    stat->pktid, &list);
		if (status.skb)
			status.info = IEEE80211_SKB_CB(status.skb);
	}

	if (!status.skb && !(stat->pktid & MT_PACKET_ID_HAS_RATE)) {
		mt76_tx_status_unlock(mdev, &list);
		goto out;
	}


	if (msta && stat->aggr && !status.skb) {
		u32 stat_val, stat_cache;

		stat_val = stat->rate;
		stat_val |= ((u32)stat->retry) << 16;
		stat_cache = msta->status.rate;
		stat_cache |= ((u32)msta->status.retry) << 16;

		if (*update == 0 && stat_val == stat_cache &&
		    stat->wcid == msta->status.wcid && msta->n_frames < 32) {
			msta->n_frames++;
			mt76_tx_status_unlock(mdev, &list);
			goto out;
		}

		cur_pktid = msta->status.pktid;
		mt76x02_mac_fill_tx_status(dev, msta, status.info,
					   &msta->status, msta->n_frames);

		msta->status = *stat;
		msta->n_frames = 1;
		*update = 0;
	} else {
		cur_pktid = stat->pktid;
		mt76x02_mac_fill_tx_status(dev, msta, status.info, stat, 1);
		*update = 1;
	}

	if (status.skb) {
		info = *status.info;
		len = status.skb->len;
		ac = skb_get_queue_mapping(status.skb);
		mt76_tx_status_skb_done(mdev, status.skb, &list);
	} else if (msta) {
		len = status.info->status.ampdu_len * ewma_pktlen_read(&msta->pktlen);
		ac = FIELD_GET(MT_PKTID_AC, cur_pktid);
	}

	mt76_tx_status_unlock(mdev, &list);

	if (!status.skb)
		ieee80211_tx_status_ext(mt76_hw(dev), &status);

	if (!len)
		goto out;

	duration = ieee80211_calc_tx_airtime(mt76_hw(dev), &info, len);

	spin_lock_bh(&dev->mt76.cc_lock);
	dev->tx_airtime += duration;
	spin_unlock_bh(&dev->mt76.cc_lock);

	if (msta)
		ieee80211_sta_register_airtime(status.sta, ac_to_tid[ac], duration, 0);

out:
	rcu_read_unlock();
}

static int
mt76x02_mac_process_rate(struct mt76x02_dev *dev,
			 struct mt76_rx_status *status,
			 u16 rate)
{
	u8 idx = FIELD_GET(MT_RXWI_RATE_INDEX, rate);

	switch (FIELD_GET(MT_RXWI_RATE_PHY, rate)) {
	case MT_PHY_TYPE_OFDM:
		if (idx >= 8)
			idx = 0;

		if (status->band == NL80211_BAND_2GHZ)
			idx += 4;

		status->rate_idx = idx;
		return 0;
	case MT_PHY_TYPE_CCK:
		if (idx >= 8) {
			idx -= 8;
			status->enc_flags |= RX_ENC_FLAG_SHORTPRE;
		}

		if (idx >= 4)
			idx = 0;

		status->rate_idx = idx;
		return 0;
	case MT_PHY_TYPE_HT_GF:
		status->enc_flags |= RX_ENC_FLAG_HT_GF;
		fallthrough;
	case MT_PHY_TYPE_HT:
		status->encoding = RX_ENC_HT;
		status->rate_idx = idx;
		break;
	case MT_PHY_TYPE_VHT: {
		u8 n_rxstream = dev->mphy.chainmask & 0xf;

		status->encoding = RX_ENC_VHT;
		status->rate_idx = FIELD_GET(MT_RATE_INDEX_VHT_IDX, idx);
		status->nss = min_t(u8, n_rxstream,
				    FIELD_GET(MT_RATE_INDEX_VHT_NSS, idx) + 1);
		break;
	}
	default:
		return -EINVAL;
	}

	if (rate & MT_RXWI_RATE_LDPC)
		status->enc_flags |= RX_ENC_FLAG_LDPC;

	if (rate & MT_RXWI_RATE_SGI)
		status->enc_flags |= RX_ENC_FLAG_SHORT_GI;

	if (rate & MT_RXWI_RATE_STBC)
		status->enc_flags |= 1 << RX_ENC_FLAG_STBC_SHIFT;

	switch (FIELD_GET(MT_RXWI_RATE_BW, rate)) {
	case MT_PHY_BW_20:
		break;
	case MT_PHY_BW_40:
		status->bw = RATE_INFO_BW_40;
		break;
	case MT_PHY_BW_80:
		status->bw = RATE_INFO_BW_80;
		break;
	default:
		break;
	}

	return 0;
}

void mt76x02_mac_setaddr(struct mt76x02_dev *dev, const u8 *addr)
{
	static const u8 null_addr[ETH_ALEN] = {};
	int i;

	ether_addr_copy(dev->mphy.macaddr, addr);

	if (!is_valid_ether_addr(dev->mphy.macaddr)) {
		eth_random_addr(dev->mphy.macaddr);
		dev_info(dev->mt76.dev,
			 "Invalid MAC address, using random address %pM\n",
			 dev->mphy.macaddr);
	}

	mt76_wr(dev, MT_MAC_ADDR_DW0, get_unaligned_le32(dev->mphy.macaddr));
	mt76_wr(dev, MT_MAC_ADDR_DW1,
		get_unaligned_le16(dev->mphy.macaddr + 4) |
		FIELD_PREP(MT_MAC_ADDR_DW1_U2ME_MASK, 0xff));

	mt76_wr(dev, MT_MAC_BSSID_DW0,
		get_unaligned_le32(dev->mphy.macaddr));
	mt76_wr(dev, MT_MAC_BSSID_DW1,
		get_unaligned_le16(dev->mphy.macaddr + 4) |
		FIELD_PREP(MT_MAC_BSSID_DW1_MBSS_MODE, 3) | /* 8 APs + 8 STAs */
		MT_MAC_BSSID_DW1_MBSS_LOCAL_BIT);
	/* enable 7 additional beacon slots and control them with bypass mask */
	mt76_rmw_field(dev, MT_MAC_BSSID_DW1, MT_MAC_BSSID_DW1_MBEACON_N, 7);

	for (i = 0; i < 16; i++)
		mt76x02_mac_set_bssid(dev, i, null_addr);
}
EXPORT_SYMBOL_GPL(mt76x02_mac_setaddr);

static int
mt76x02_mac_get_rssi(struct mt76x02_dev *dev, s8 rssi, int chain)
{
	struct mt76x02_rx_freq_cal *cal = &dev->cal.rx;

	rssi += cal->rssi_offset[chain];
	rssi -= cal->lna_gain;

	return rssi;
}

int mt76x02_mac_process_rx(struct mt76x02_dev *dev, struct sk_buff *skb,
			   void *rxi)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct ieee80211_hdr *hdr;
	struct mt76x02_rxwi *rxwi = rxi;
	struct mt76x02_sta *sta;
	u32 rxinfo = le32_to_cpu(rxwi->rxinfo);
	u32 ctl = le32_to_cpu(rxwi->ctl);
	u16 rate = le16_to_cpu(rxwi->rate);
	u16 tid_sn = le16_to_cpu(rxwi->tid_sn);
	bool unicast = rxwi->rxinfo & cpu_to_le32(MT_RXINFO_UNICAST);
	int pad_len = 0, nstreams = dev->mphy.chainmask & 0xf;
	s8 signal;
	u8 pn_len;
	u8 wcid;
	int len;

	if (!test_bit(MT76_STATE_RUNNING, &dev->mphy.state))
		return -EINVAL;

	if (rxinfo & MT_RXINFO_L2PAD)
		pad_len += 2;

	if (rxinfo & MT_RXINFO_DECRYPT) {
		status->flag |= RX_FLAG_DECRYPTED;
		status->flag |= RX_FLAG_MMIC_STRIPPED;
		status->flag |= RX_FLAG_MIC_STRIPPED;
		status->flag |= RX_FLAG_IV_STRIPPED;
	}

	wcid = FIELD_GET(MT_RXWI_CTL_WCID, ctl);
	sta = mt76x02_rx_get_sta(&dev->mt76, wcid);
	status->wcid = mt76x02_rx_get_sta_wcid(sta, unicast);

	len = FIELD_GET(MT_RXWI_CTL_MPDU_LEN, ctl);
	pn_len = FIELD_GET(MT_RXINFO_PN_LEN, rxinfo);
	if (pn_len) {
		int offset = ieee80211_get_hdrlen_from_skb(skb) + pad_len;
		u8 *data = skb->data + offset;

		status->iv[0] = data[7];
		status->iv[1] = data[6];
		status->iv[2] = data[5];
		status->iv[3] = data[4];
		status->iv[4] = data[1];
		status->iv[5] = data[0];

		/*
		 * Driver CCMP validation can't deal with fragments.
		 * Let mac80211 take care of it.
		 */
		if (rxinfo & MT_RXINFO_FRAG) {
			status->flag &= ~RX_FLAG_IV_STRIPPED;
		} else {
			pad_len += pn_len << 2;
			len -= pn_len << 2;
		}
	}

	mt76x02_remove_hdr_pad(skb, pad_len);

	if ((rxinfo & MT_RXINFO_BA) && !(rxinfo & MT_RXINFO_NULL))
		status->aggr = true;

	if (rxinfo & MT_RXINFO_AMPDU) {
		status->flag |= RX_FLAG_AMPDU_DETAILS;
		status->ampdu_ref = dev->ampdu_ref;

		/*
		 * When receiving an A-MPDU subframe and RSSI info is not valid,
		 * we can assume that more subframes belonging to the same A-MPDU
		 * are coming. The last one will have valid RSSI info
		 */
		if (rxinfo & MT_RXINFO_RSSI) {
			if (!++dev->ampdu_ref)
				dev->ampdu_ref++;
		}
	}

	if (WARN_ON_ONCE(len > skb->len))
		return -EINVAL;

	pskb_trim(skb, len);

	status->chains = BIT(0);
	signal = mt76x02_mac_get_rssi(dev, rxwi->rssi[0], 0);
	status->chain_signal[0] = signal;
	if (nstreams > 1) {
		status->chains |= BIT(1);
		status->chain_signal[1] = mt76x02_mac_get_rssi(dev,
							       rxwi->rssi[1],
							       1);
	}
	status->freq = dev->mphy.chandef.chan->center_freq;
	status->band = dev->mphy.chandef.chan->band;

	hdr = (struct ieee80211_hdr *)skb->data;
	status->qos_ctl = *ieee80211_get_qos_ctl(hdr);
	status->seqno = FIELD_GET(MT_RXWI_SN, tid_sn);

	return mt76x02_mac_process_rate(dev, status, rate);
}

void mt76x02_mac_poll_tx_status(struct mt76x02_dev *dev, bool irq)
{
	struct mt76x02_tx_status stat = {};
	u8 update = 1;
	bool ret;

	if (!test_bit(MT76_STATE_RUNNING, &dev->mphy.state))
		return;

	trace_mac_txstat_poll(dev);

	while (!irq || !kfifo_is_full(&dev->txstatus_fifo)) {
		if (!spin_trylock(&dev->txstatus_fifo_lock))
			break;

		ret = mt76x02_mac_load_tx_status(dev, &stat);
		spin_unlock(&dev->txstatus_fifo_lock);

		if (!ret)
			break;

		if (!irq) {
			mt76x02_send_tx_status(dev, &stat, &update);
			continue;
		}

		kfifo_put(&dev->txstatus_fifo, stat);
	}
}

void mt76x02_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue_entry *e)
{
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	struct mt76x02_txwi *txwi;
	u8 *txwi_ptr;

	if (!e->txwi) {
		dev_kfree_skb_any(e->skb);
		return;
	}

	mt76x02_mac_poll_tx_status(dev, false);

	txwi_ptr = mt76_get_txwi_ptr(mdev, e->txwi);
	txwi = (struct mt76x02_txwi *)txwi_ptr;
	trace_mac_txdone(mdev, txwi->wcid, txwi->pktid);

	mt76_tx_complete_skb(mdev, e->wcid, e->skb);
}
EXPORT_SYMBOL_GPL(mt76x02_tx_complete_skb);

void mt76x02_mac_set_rts_thresh(struct mt76x02_dev *dev, u32 val)
{
	u32 data = 0;

	if (val != ~0)
		data = FIELD_PREP(MT_PROT_CFG_CTRL, 1) |
		       MT_PROT_CFG_RTS_THRESH;

	mt76_rmw_field(dev, MT_TX_RTS_CFG, MT_TX_RTS_CFG_THRESH, val);

	mt76_rmw(dev, MT_CCK_PROT_CFG,
		 MT_PROT_CFG_CTRL | MT_PROT_CFG_RTS_THRESH, data);
	mt76_rmw(dev, MT_OFDM_PROT_CFG,
		 MT_PROT_CFG_CTRL | MT_PROT_CFG_RTS_THRESH, data);
}

void mt76x02_mac_set_tx_protection(struct mt76x02_dev *dev, bool legacy_prot,
				   int ht_mode)
{
	int mode = ht_mode & IEEE80211_HT_OP_MODE_PROTECTION;
	bool non_gf = !!(ht_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);
	u32 prot[6];
	u32 vht_prot[3];
	int i;
	u16 rts_thr;

	for (i = 0; i < ARRAY_SIZE(prot); i++) {
		prot[i] = mt76_rr(dev, MT_CCK_PROT_CFG + i * 4);
		prot[i] &= ~MT_PROT_CFG_CTRL;
		if (i >= 2)
			prot[i] &= ~MT_PROT_CFG_RATE;
	}

	for (i = 0; i < ARRAY_SIZE(vht_prot); i++) {
		vht_prot[i] = mt76_rr(dev, MT_TX_PROT_CFG6 + i * 4);
		vht_prot[i] &= ~(MT_PROT_CFG_CTRL | MT_PROT_CFG_RATE);
	}

	rts_thr = mt76_get_field(dev, MT_TX_RTS_CFG, MT_TX_RTS_CFG_THRESH);

	if (rts_thr != 0xffff)
		prot[0] |= MT_PROT_CTRL_RTS_CTS;

	if (legacy_prot) {
		prot[1] |= MT_PROT_CTRL_CTS2SELF;

		prot[2] |= MT_PROT_RATE_CCK_11;
		prot[3] |= MT_PROT_RATE_CCK_11;
		prot[4] |= MT_PROT_RATE_CCK_11;
		prot[5] |= MT_PROT_RATE_CCK_11;

		vht_prot[0] |= MT_PROT_RATE_CCK_11;
		vht_prot[1] |= MT_PROT_RATE_CCK_11;
		vht_prot[2] |= MT_PROT_RATE_CCK_11;
	} else {
		if (rts_thr != 0xffff)
			prot[1] |= MT_PROT_CTRL_RTS_CTS;

		prot[2] |= MT_PROT_RATE_OFDM_24;
		prot[3] |= MT_PROT_RATE_DUP_OFDM_24;
		prot[4] |= MT_PROT_RATE_OFDM_24;
		prot[5] |= MT_PROT_RATE_DUP_OFDM_24;

		vht_prot[0] |= MT_PROT_RATE_OFDM_24;
		vht_prot[1] |= MT_PROT_RATE_DUP_OFDM_24;
		vht_prot[2] |= MT_PROT_RATE_SGI_OFDM_24;
	}

	switch (mode) {
	case IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER:
	case IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED:
		prot[2] |= MT_PROT_CTRL_RTS_CTS;
		prot[3] |= MT_PROT_CTRL_RTS_CTS;
		prot[4] |= MT_PROT_CTRL_RTS_CTS;
		prot[5] |= MT_PROT_CTRL_RTS_CTS;
		vht_prot[0] |= MT_PROT_CTRL_RTS_CTS;
		vht_prot[1] |= MT_PROT_CTRL_RTS_CTS;
		vht_prot[2] |= MT_PROT_CTRL_RTS_CTS;
		break;
	case IEEE80211_HT_OP_MODE_PROTECTION_20MHZ:
		prot[3] |= MT_PROT_CTRL_RTS_CTS;
		prot[5] |= MT_PROT_CTRL_RTS_CTS;
		vht_prot[1] |= MT_PROT_CTRL_RTS_CTS;
		vht_prot[2] |= MT_PROT_CTRL_RTS_CTS;
		break;
	}

	if (non_gf) {
		prot[4] |= MT_PROT_CTRL_RTS_CTS;
		prot[5] |= MT_PROT_CTRL_RTS_CTS;
	}

	for (i = 0; i < ARRAY_SIZE(prot); i++)
		mt76_wr(dev, MT_CCK_PROT_CFG + i * 4, prot[i]);

	for (i = 0; i < ARRAY_SIZE(vht_prot); i++)
		mt76_wr(dev, MT_TX_PROT_CFG6 + i * 4, vht_prot[i]);
}

void mt76x02_update_channel(struct mt76_phy *mphy)
{
	struct mt76x02_dev *dev = container_of(mphy->dev, struct mt76x02_dev, mt76);
	struct mt76_channel_state *state;

	state = mphy->chan_state;
	state->cc_busy += mt76_rr(dev, MT_CH_BUSY);

	spin_lock_bh(&dev->mt76.cc_lock);
	state->cc_tx += dev->tx_airtime;
	dev->tx_airtime = 0;
	spin_unlock_bh(&dev->mt76.cc_lock);
}
EXPORT_SYMBOL_GPL(mt76x02_update_channel);

static void mt76x02_check_mac_err(struct mt76x02_dev *dev)
{
	if (dev->mt76.beacon_mask) {
		if (mt76_rr(dev, MT_TX_STA_0) & MT_TX_STA_0_BEACONS) {
			dev->beacon_hang_check = 0;
			return;
		}

		if (++dev->beacon_hang_check < 10)
			return;

		dev->beacon_hang_check = 0;
	} else {
		u32 val = mt76_rr(dev, 0x10f4);
		if (!(val & BIT(29)) || !(val & (BIT(7) | BIT(5))))
			return;
	}

	dev_err(dev->mt76.dev, "MAC error detected\n");

	mt76_wr(dev, MT_MAC_SYS_CTRL, 0);
	mt76x02_wait_for_txrx_idle(&dev->mt76);

	mt76_set(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_RESET_CSR);
	udelay(10);
	mt76_wr(dev, MT_MAC_SYS_CTRL,
		MT_MAC_SYS_CTRL_ENABLE_TX | MT_MAC_SYS_CTRL_ENABLE_RX);
}

static void
mt76x02_edcca_tx_enable(struct mt76x02_dev *dev, bool enable)
{
	if (enable) {
		u32 data;

		mt76_set(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_TX);
		mt76_set(dev, MT_AUTO_RSP_CFG, MT_AUTO_RSP_EN);
		/* enable pa-lna */
		data = mt76_rr(dev, MT_TX_PIN_CFG);
		data |= MT_TX_PIN_CFG_TXANT |
			MT_TX_PIN_CFG_RXANT |
			MT_TX_PIN_RFTR_EN |
			MT_TX_PIN_TRSW_EN;
		mt76_wr(dev, MT_TX_PIN_CFG, data);
	} else {
		mt76_clear(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_TX);
		mt76_clear(dev, MT_AUTO_RSP_CFG, MT_AUTO_RSP_EN);
		/* disable pa-lna */
		mt76_clear(dev, MT_TX_PIN_CFG, MT_TX_PIN_CFG_TXANT);
		mt76_clear(dev, MT_TX_PIN_CFG, MT_TX_PIN_CFG_RXANT);
	}
	dev->ed_tx_blocked = !enable;
}

void mt76x02_edcca_init(struct mt76x02_dev *dev)
{
	dev->ed_trigger = 0;
	dev->ed_silent = 0;

	if (dev->ed_monitor) {
		struct ieee80211_channel *chan = dev->mphy.chandef.chan;
		u8 ed_th = chan->band == NL80211_BAND_5GHZ ? 0x0e : 0x20;

		mt76_clear(dev, MT_TX_LINK_CFG, MT_TX_CFACK_EN);
		mt76_set(dev, MT_TXOP_CTRL_CFG, MT_TXOP_ED_CCA_EN);
		mt76_rmw(dev, MT_BBP(AGC, 2), GENMASK(15, 0),
			 ed_th << 8 | ed_th);
		mt76_set(dev, MT_TXOP_HLDR_ET, MT_TXOP_HLDR_TX40M_BLK_EN);
	} else {
		mt76_set(dev, MT_TX_LINK_CFG, MT_TX_CFACK_EN);
		mt76_clear(dev, MT_TXOP_CTRL_CFG, MT_TXOP_ED_CCA_EN);
		if (is_mt76x2(dev)) {
			mt76_wr(dev, MT_BBP(AGC, 2), 0x00007070);
			mt76_set(dev, MT_TXOP_HLDR_ET,
				 MT_TXOP_HLDR_TX40M_BLK_EN);
		} else {
			mt76_wr(dev, MT_BBP(AGC, 2), 0x003a6464);
			mt76_clear(dev, MT_TXOP_HLDR_ET,
				   MT_TXOP_HLDR_TX40M_BLK_EN);
		}
	}
	mt76x02_edcca_tx_enable(dev, true);
	dev->ed_monitor_learning = true;

	/* clear previous CCA timer value */
	mt76_rr(dev, MT_ED_CCA_TIMER);
	dev->ed_time = ktime_get_boottime();
}
EXPORT_SYMBOL_GPL(mt76x02_edcca_init);

#define MT_EDCCA_TH		92
#define MT_EDCCA_BLOCK_TH	2
#define MT_EDCCA_LEARN_TH	50
#define MT_EDCCA_LEARN_CCA	180
#define MT_EDCCA_LEARN_TIMEOUT	(20 * HZ)

static void mt76x02_edcca_check(struct mt76x02_dev *dev)
{
	ktime_t cur_time;
	u32 active, val, busy;

	cur_time = ktime_get_boottime();
	val = mt76_rr(dev, MT_ED_CCA_TIMER);

	active = ktime_to_us(ktime_sub(cur_time, dev->ed_time));
	dev->ed_time = cur_time;

	busy = (val * 100) / active;
	busy = min_t(u32, busy, 100);

	if (busy > MT_EDCCA_TH) {
		dev->ed_trigger++;
		dev->ed_silent = 0;
	} else {
		dev->ed_silent++;
		dev->ed_trigger = 0;
	}

	if (dev->cal.agc_lowest_gain &&
	    dev->cal.false_cca > MT_EDCCA_LEARN_CCA &&
	    dev->ed_trigger > MT_EDCCA_LEARN_TH) {
		dev->ed_monitor_learning = false;
		dev->ed_trigger_timeout = jiffies + 20 * HZ;
	} else if (!dev->ed_monitor_learning &&
		   time_is_after_jiffies(dev->ed_trigger_timeout)) {
		dev->ed_monitor_learning = true;
		mt76x02_edcca_tx_enable(dev, true);
	}

	if (dev->ed_monitor_learning)
		return;

	if (dev->ed_trigger > MT_EDCCA_BLOCK_TH && !dev->ed_tx_blocked)
		mt76x02_edcca_tx_enable(dev, false);
	else if (dev->ed_silent > MT_EDCCA_BLOCK_TH && dev->ed_tx_blocked)
		mt76x02_edcca_tx_enable(dev, true);
}

void mt76x02_mac_work(struct work_struct *work)
{
	struct mt76x02_dev *dev = container_of(work, struct mt76x02_dev,
					       mphy.mac_work.work);
	int i, idx;

	mutex_lock(&dev->mt76.mutex);

	mt76_update_survey(&dev->mphy);
	for (i = 0, idx = 0; i < 16; i++) {
		u32 val = mt76_rr(dev, MT_TX_AGG_CNT(i));

		dev->mt76.aggr_stats[idx++] += val & 0xffff;
		dev->mt76.aggr_stats[idx++] += val >> 16;
	}

	mt76x02_check_mac_err(dev);

	if (dev->ed_monitor)
		mt76x02_edcca_check(dev);

	mutex_unlock(&dev->mt76.mutex);

	mt76_tx_status_check(&dev->mt76, false);

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mphy.mac_work,
				     MT_MAC_WORK_INTERVAL);
}

void mt76x02_mac_cc_reset(struct mt76x02_dev *dev)
{
	dev->mphy.survey_time = ktime_get_boottime();

	mt76_wr(dev, MT_CH_TIME_CFG,
		MT_CH_TIME_CFG_TIMER_EN |
		MT_CH_TIME_CFG_TX_AS_BUSY |
		MT_CH_TIME_CFG_RX_AS_BUSY |
		MT_CH_TIME_CFG_NAV_AS_BUSY |
		MT_CH_TIME_CFG_EIFS_AS_BUSY |
		MT_CH_CCA_RC_EN |
		FIELD_PREP(MT_CH_TIME_CFG_CH_TIMER_CLR, 1));

	/* channel cycle counters read-and-clear */
	mt76_rr(dev, MT_CH_BUSY);
	mt76_rr(dev, MT_CH_IDLE);
}
EXPORT_SYMBOL_GPL(mt76x02_mac_cc_reset);

void mt76x02_mac_set_bssid(struct mt76x02_dev *dev, u8 idx, const u8 *addr)
{
	idx &= 7;
	mt76_wr(dev, MT_MAC_APC_BSSID_L(idx), get_unaligned_le32(addr));
	mt76_rmw_field(dev, MT_MAC_APC_BSSID_H(idx), MT_MAC_APC_BSSID_H_ADDR,
		       get_unaligned_le16(addr + 4));
}
