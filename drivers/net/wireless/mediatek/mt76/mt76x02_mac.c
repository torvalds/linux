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

#include "mt76.h"
#include "mt76x02_regs.h"
#include "mt76x02_mac.h"

enum mt76x02_cipher_type
mt76x02_mac_get_key_info(struct ieee80211_key_conf *key, u8 *key_data)
{
	memset(key_data, 0, 32);
	if (!key)
		return MT_CIPHER_NONE;

	if (key->keylen > 32)
		return MT_CIPHER_NONE;

	memcpy(key_data, key->key, key->keylen);

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		return MT_CIPHER_WEP40;
	case WLAN_CIPHER_SUITE_WEP104:
		return MT_CIPHER_WEP104;
	case WLAN_CIPHER_SUITE_TKIP:
		return MT_CIPHER_TKIP;
	case WLAN_CIPHER_SUITE_CCMP:
		return MT_CIPHER_AES_CCMP;
	default:
		return MT_CIPHER_NONE;
	}
}
EXPORT_SYMBOL_GPL(mt76x02_mac_get_key_info);

int mt76x02_mac_shared_key_setup(struct mt76_dev *dev, u8 vif_idx, u8 key_idx,
				struct ieee80211_key_conf *key)
{
	enum mt76x02_cipher_type cipher;
	u8 key_data[32];
	u32 val;

	cipher = mt76x02_mac_get_key_info(key, key_data);
	if (cipher == MT_CIPHER_NONE && key)
		return -EOPNOTSUPP;

	val = __mt76_rr(dev, MT_SKEY_MODE(vif_idx));
	val &= ~(MT_SKEY_MODE_MASK << MT_SKEY_MODE_SHIFT(vif_idx, key_idx));
	val |= cipher << MT_SKEY_MODE_SHIFT(vif_idx, key_idx);
	__mt76_wr(dev, MT_SKEY_MODE(vif_idx), val);

	__mt76_wr_copy(dev, MT_SKEY(vif_idx, key_idx), key_data,
		       sizeof(key_data));

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_mac_shared_key_setup);

int mt76x02_mac_wcid_set_key(struct mt76_dev *dev, u8 idx,
			    struct ieee80211_key_conf *key)
{
	enum mt76x02_cipher_type cipher;
	u8 key_data[32];
	u8 iv_data[8];

	cipher = mt76x02_mac_get_key_info(key, key_data);
	if (cipher == MT_CIPHER_NONE && key)
		return -EOPNOTSUPP;

	__mt76_wr_copy(dev, MT_WCID_KEY(idx), key_data, sizeof(key_data));
	__mt76_rmw_field(dev, MT_WCID_ATTR(idx), MT_WCID_ATTR_PKEY_MODE, cipher);

	memset(iv_data, 0, sizeof(iv_data));
	if (key) {
		__mt76_rmw_field(dev, MT_WCID_ATTR(idx), MT_WCID_ATTR_PAIRWISE,
				 !!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE));
		iv_data[3] = key->keyidx << 6;
		if (cipher >= MT_CIPHER_TKIP)
			iv_data[3] |= 0x20;
	}

	__mt76_wr_copy(dev, MT_WCID_IV(idx), iv_data, sizeof(iv_data));

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_mac_wcid_set_key);

void mt76x02_mac_wcid_setup(struct mt76_dev *dev, u8 idx, u8 vif_idx, u8 *mac)
{
	struct mt76_wcid_addr addr = {};
	u32 attr;

	attr = FIELD_PREP(MT_WCID_ATTR_BSS_IDX, vif_idx & 7) |
	       FIELD_PREP(MT_WCID_ATTR_BSS_IDX_EXT, !!(vif_idx & 8));

	__mt76_wr(dev, MT_WCID_ATTR(idx), attr);

	__mt76_wr(dev, MT_WCID_TX_RATE(idx), 0);
	__mt76_wr(dev, MT_WCID_TX_RATE(idx) + 4, 0);

	if (idx >= 128)
		return;

	if (mac)
		memcpy(addr.macaddr, mac, ETH_ALEN);

	__mt76_wr_copy(dev, MT_WCID_ADDR(idx), &addr, sizeof(addr));
}
EXPORT_SYMBOL_GPL(mt76x02_mac_wcid_setup);

void mt76x02_mac_wcid_set_drop(struct mt76_dev *dev, u8 idx, bool drop)
{
	u32 val = __mt76_rr(dev, MT_WCID_DROP(idx));
	u32 bit = MT_WCID_DROP_MASK(idx);

	/* prevent unnecessary writes */
	if ((val & bit) != (bit * drop))
		__mt76_wr(dev, MT_WCID_DROP(idx), (val & ~bit) | (bit * drop));
}
EXPORT_SYMBOL_GPL(mt76x02_mac_wcid_set_drop);

void mt76x02_txq_init(struct mt76_dev *dev, struct ieee80211_txq *txq)
{
	struct mt76_txq *mtxq;

	if (!txq)
		return;

	mtxq = (struct mt76_txq *) txq->drv_priv;
	if (txq->sta) {
		struct mt76x02_sta *sta;

		sta = (struct mt76x02_sta *) txq->sta->drv_priv;
		mtxq->wcid = &sta->wcid;
	} else {
		struct mt76x02_vif *mvif;

		mvif = (struct mt76x02_vif *) txq->vif->drv_priv;
		mtxq->wcid = &mvif->group_wcid;
	}

	mt76_txq_init(dev, txq);
}
EXPORT_SYMBOL_GPL(mt76x02_txq_init);

void mt76x02_mac_fill_txwi(struct mt76x02_txwi *txwi, struct sk_buff *skb,
			  struct ieee80211_sta *sta, int len, u8 nss)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u16 txwi_flags = 0;

	if (info->flags & IEEE80211_TX_CTL_LDPC)
		txwi->rate |= cpu_to_le16(MT_RXWI_RATE_LDPC);
	if ((info->flags & IEEE80211_TX_CTL_STBC) && nss == 1)
		txwi->rate |= cpu_to_le16(MT_RXWI_RATE_STBC);
	if (nss > 1 && sta && sta->smps_mode == IEEE80211_SMPS_DYNAMIC)
		txwi_flags |= MT_TXWI_FLAGS_MMPS;
	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
		txwi->ack_ctl |= MT_TXWI_ACK_CTL_REQ;
	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ)
		txwi->ack_ctl |= MT_TXWI_ACK_CTL_NSEQ;
	if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
		txwi->pktid |= MT_TXWI_PKTID_PROBE;
	if ((info->flags & IEEE80211_TX_CTL_AMPDU) && sta) {
		u8 ba_size = IEEE80211_MIN_AMPDU_BUF;

		ba_size <<= sta->ht_cap.ampdu_factor;
		ba_size = min_t(int, 63, ba_size - 1);
		if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)
			ba_size = 0;
		txwi->ack_ctl |= FIELD_PREP(MT_TXWI_ACK_CTL_BA_WINDOW, ba_size);

		txwi_flags |= MT_TXWI_FLAGS_AMPDU |
			 FIELD_PREP(MT_TXWI_FLAGS_MPDU_DENSITY,
				    sta->ht_cap.ampdu_density);
	}

	if (ieee80211_is_probe_resp(hdr->frame_control) ||
	    ieee80211_is_beacon(hdr->frame_control))
		txwi_flags |= MT_TXWI_FLAGS_TS;

	txwi->flags |= cpu_to_le16(txwi_flags);
	txwi->len_ctl = cpu_to_le16(len);
}
EXPORT_SYMBOL_GPL(mt76x02_mac_fill_txwi);

__le16
mt76x02_mac_tx_rate_val(struct mt76_dev *dev,
		       const struct ieee80211_tx_rate *rate, u8 *nss_val)
{
	u16 rateval;
	u8 phy, rate_idx;
	u8 nss = 1;
	u8 bw = 0;

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
		int band = dev->chandef.chan->band;
		u16 val;

		r = &dev->hw->wiphy->bands[band]->bitrates[rate->idx];
		if (rate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
			val = r->hw_value_short;
		else
			val = r->hw_value;

		phy = val >> 8;
		rate_idx = val & 0xff;
		bw = 0;
	}

	rateval = FIELD_PREP(MT_RXWI_RATE_INDEX, rate_idx);
	rateval |= FIELD_PREP(MT_RXWI_RATE_PHY, phy);
	rateval |= FIELD_PREP(MT_RXWI_RATE_BW, bw);
	if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
		rateval |= MT_RXWI_RATE_SGI;

	*nss_val = nss;
	return cpu_to_le16(rateval);
}
EXPORT_SYMBOL_GPL(mt76x02_mac_tx_rate_val);

void mt76x02_mac_wcid_set_rate(struct mt76_dev *dev, struct mt76_wcid *wcid,
			      const struct ieee80211_tx_rate *rate)
{
	spin_lock_bh(&dev->lock);
	wcid->tx_rate = mt76x02_mac_tx_rate_val(dev, rate, &wcid->tx_rate_nss);
	wcid->tx_rate_set = true;
	spin_unlock_bh(&dev->lock);
}

bool mt76x02_mac_load_tx_status(struct mt76_dev *dev,
			       struct mt76x02_tx_status *stat)
{
	u32 stat1, stat2;

	stat2 = __mt76_rr(dev, MT_TX_STAT_FIFO_EXT);
	stat1 = __mt76_rr(dev, MT_TX_STAT_FIFO);

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

	return true;
}
EXPORT_SYMBOL_GPL(mt76x02_mac_load_tx_status);

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
		/* fall through */
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

static void
mt76x02_mac_fill_tx_status(struct mt76_dev *dev,
			  struct ieee80211_tx_info *info,
			  struct mt76x02_tx_status *st, int n_frames)
{
	struct ieee80211_tx_rate *rate = info->status.rates;
	int cur_idx, last_rate;
	int i;

	if (!n_frames)
		return;

	last_rate = min_t(int, st->retry, IEEE80211_TX_MAX_RATES - 1);
	mt76x02_mac_process_tx_rate(&rate[last_rate], st->rate,
				   dev->chandef.chan->band);
	if (last_rate < IEEE80211_TX_MAX_RATES - 1)
		rate[last_rate + 1].idx = -1;

	cur_idx = rate[last_rate].idx + last_rate;
	for (i = 0; i <= last_rate; i++) {
		rate[i].flags = rate[last_rate].flags;
		rate[i].idx = max_t(int, 0, cur_idx - i);
		rate[i].count = 1;
	}
	rate[last_rate].count = st->retry + 1 - last_rate;

	info->status.ampdu_len = n_frames;
	info->status.ampdu_ack_len = st->success ? n_frames : 0;

	if (st->pktid & MT_TXWI_PKTID_PROBE)
		info->flags |= IEEE80211_TX_CTL_RATE_CTRL_PROBE;

	if (st->aggr)
		info->flags |= IEEE80211_TX_CTL_AMPDU |
			       IEEE80211_TX_STAT_AMPDU;

	if (!st->ack_req)
		info->flags |= IEEE80211_TX_CTL_NO_ACK;
	else if (st->success)
		info->flags |= IEEE80211_TX_STAT_ACK;
}

void mt76x02_send_tx_status(struct mt76_dev *dev,
			   struct mt76x02_tx_status *stat, u8 *update)
{
	struct ieee80211_tx_info info = {};
	struct ieee80211_sta *sta = NULL;
	struct mt76_wcid *wcid = NULL;
	struct mt76x02_sta *msta = NULL;

	rcu_read_lock();
	if (stat->wcid < ARRAY_SIZE(dev->wcid))
		wcid = rcu_dereference(dev->wcid[stat->wcid]);

	if (wcid) {
		void *priv;

		priv = msta = container_of(wcid, struct mt76x02_sta, wcid);
		sta = container_of(priv, struct ieee80211_sta,
				   drv_priv);
	}

	if (msta && stat->aggr) {
		u32 stat_val, stat_cache;

		stat_val = stat->rate;
		stat_val |= ((u32) stat->retry) << 16;
		stat_cache = msta->status.rate;
		stat_cache |= ((u32) msta->status.retry) << 16;

		if (*update == 0 && stat_val == stat_cache &&
		    stat->wcid == msta->status.wcid && msta->n_frames < 32) {
			msta->n_frames++;
			goto out;
		}

		mt76x02_mac_fill_tx_status(dev, &info, &msta->status,
					  msta->n_frames);

		msta->status = *stat;
		msta->n_frames = 1;
		*update = 0;
	} else {
		mt76x02_mac_fill_tx_status(dev, &info, stat, 1);
		*update = 1;
	}

	ieee80211_tx_status_noskb(dev->hw, sta, &info);

out:
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(mt76x02_send_tx_status);

int
mt76x02_mac_process_rate(struct mt76_rx_status *status, u16 rate)
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
		/* fall through */
	case MT_PHY_TYPE_HT:
		status->encoding = RX_ENC_HT;
		status->rate_idx = idx;
		break;
	case MT_PHY_TYPE_VHT:
		status->encoding = RX_ENC_VHT;
		status->rate_idx = FIELD_GET(MT_RATE_INDEX_VHT_IDX, idx);
		status->nss = FIELD_GET(MT_RATE_INDEX_VHT_NSS, idx) + 1;
		break;
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
EXPORT_SYMBOL_GPL(mt76x02_mac_process_rate);

void mt76x02_mac_setaddr(struct mt76_dev *dev, u8 *addr)
{
	ether_addr_copy(dev->macaddr, addr);

	if (!is_valid_ether_addr(dev->macaddr)) {
		eth_random_addr(dev->macaddr);
		dev_info(dev->dev,
			 "Invalid MAC address, using random address %pM\n",
			 dev->macaddr);
	}

	__mt76_wr(dev, MT_MAC_ADDR_DW0, get_unaligned_le32(dev->macaddr));
	__mt76_wr(dev, MT_MAC_ADDR_DW1,
		  get_unaligned_le16(dev->macaddr + 4) |
		  FIELD_PREP(MT_MAC_ADDR_DW1_U2ME_MASK, 0xff));
}
EXPORT_SYMBOL_GPL(mt76x02_mac_setaddr);
