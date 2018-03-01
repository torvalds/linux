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

#include <linux/delay.h>
#include "mt76x2.h"
#include "mt76x2_mcu.h"
#include "mt76x2_eeprom.h"
#include "mt76x2_trace.h"

void mt76x2_mac_set_bssid(struct mt76x2_dev *dev, u8 idx, const u8 *addr)
{
	idx &= 7;
	mt76_wr(dev, MT_MAC_APC_BSSID_L(idx), get_unaligned_le32(addr));
	mt76_rmw_field(dev, MT_MAC_APC_BSSID_H(idx), MT_MAC_APC_BSSID_H_ADDR,
		       get_unaligned_le16(addr + 4));
}

static int
mt76x2_mac_process_rate(struct mt76_rx_status *status, u16 rate)
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

static __le16
mt76x2_mac_tx_rate_val(struct mt76x2_dev *dev,
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
		int band = dev->mt76.chandef.chan->band;
		u16 val;

		r = &mt76_hw(dev)->wiphy->bands[band]->bitrates[rate->idx];
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

void mt76x2_mac_wcid_set_drop(struct mt76x2_dev *dev, u8 idx, bool drop)
{
	u32 val = mt76_rr(dev, MT_WCID_DROP(idx));
	u32 bit = MT_WCID_DROP_MASK(idx);

	/* prevent unnecessary writes */
	if ((val & bit) != (bit * drop))
		mt76_wr(dev, MT_WCID_DROP(idx), (val & ~bit) | (bit * drop));
}

void mt76x2_mac_wcid_set_rate(struct mt76x2_dev *dev, struct mt76_wcid *wcid,
			      const struct ieee80211_tx_rate *rate)
{
	spin_lock_bh(&dev->mt76.lock);
	wcid->tx_rate = mt76x2_mac_tx_rate_val(dev, rate, &wcid->tx_rate_nss);
	wcid->tx_rate_set = true;
	spin_unlock_bh(&dev->mt76.lock);
}

void mt76x2_mac_write_txwi(struct mt76x2_dev *dev, struct mt76x2_txwi *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid,
			   struct ieee80211_sta *sta)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rate = &info->control.rates[0];
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u16 rate_ht_mask = FIELD_PREP(MT_RXWI_RATE_PHY, BIT(1) | BIT(2));
	u16 txwi_flags = 0;
	u8 nss;
	s8 txpwr_adj, max_txpwr_adj;
	u8 ccmp_pn[8];

	memset(txwi, 0, sizeof(*txwi));

	if (wcid)
		txwi->wcid = wcid->idx;
	else
		txwi->wcid = 0xff;

	txwi->pktid = 1;

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
		txwi->iv = *((u32 *) &ccmp_pn[0]);
		txwi->eiv = *((u32 *) &ccmp_pn[1]);
	}

	spin_lock_bh(&dev->mt76.lock);
	if (wcid && (rate->idx < 0 || !rate->count)) {
		txwi->rate = wcid->tx_rate;
		max_txpwr_adj = wcid->max_txpwr_adj;
		nss = wcid->tx_rate_nss;
	} else {
		txwi->rate = mt76x2_mac_tx_rate_val(dev, rate, &nss);
		max_txpwr_adj = mt76x2_tx_get_max_txpwr_adj(dev, rate);
	}
	spin_unlock_bh(&dev->mt76.lock);

	txpwr_adj = mt76x2_tx_get_txpwr_adj(dev, dev->txpower_conf,
					    max_txpwr_adj);
	txwi->ctl2 = FIELD_PREP(MT_TX_PWR_ADJ, txpwr_adj);

	if (mt76xx_rev(dev) >= MT76XX_REV_E4)
		txwi->txstream = 0x13;
	else if (mt76xx_rev(dev) >= MT76XX_REV_E3 &&
		 !(txwi->rate & cpu_to_le16(rate_ht_mask)))
		txwi->txstream = 0x93;

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
	txwi->len_ctl = cpu_to_le16(skb->len);
}

static void mt76x2_remove_hdr_pad(struct sk_buff *skb, int len)
{
	int hdrlen;

	if (!len)
		return;

	hdrlen = ieee80211_get_hdrlen_from_skb(skb);
	memmove(skb->data + len, skb->data, hdrlen);
	skb_pull(skb, len);
}

static struct mt76_wcid *
mt76x2_rx_get_sta_wcid(struct mt76x2_dev *dev, u8 idx, bool unicast)
{
	struct mt76x2_sta *sta;
	struct mt76_wcid *wcid;

	if (idx >= ARRAY_SIZE(dev->wcid))
		return NULL;

	wcid = rcu_dereference(dev->wcid[idx]);
	if (unicast || !wcid)
		return wcid;

	sta = container_of(wcid, struct mt76x2_sta, wcid);
	return &sta->vif->group_wcid;
}

int mt76x2_mac_process_rx(struct mt76x2_dev *dev, struct sk_buff *skb,
			  void *rxi)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *) skb->cb;
	struct mt76x2_rxwi *rxwi = rxi;
	u32 rxinfo = le32_to_cpu(rxwi->rxinfo);
	u32 ctl = le32_to_cpu(rxwi->ctl);
	u16 rate = le16_to_cpu(rxwi->rate);
	u16 tid_sn = le16_to_cpu(rxwi->tid_sn);
	bool unicast = rxwi->rxinfo & cpu_to_le32(MT_RXINFO_UNICAST);
	int pad_len = 0;
	u8 pn_len;
	u8 wcid;
	int len;

	if (rxinfo & MT_RXINFO_L2PAD)
		pad_len += 2;

	if (rxinfo & MT_RXINFO_DECRYPT) {
		status->flag |= RX_FLAG_DECRYPTED;
		status->flag |= RX_FLAG_MMIC_STRIPPED;
		status->flag |= RX_FLAG_MIC_STRIPPED;
		status->flag |= RX_FLAG_IV_STRIPPED;
	}

	wcid = FIELD_GET(MT_RXWI_CTL_WCID, ctl);
	status->wcid = mt76x2_rx_get_sta_wcid(dev, wcid, unicast);

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

	mt76x2_remove_hdr_pad(skb, pad_len);

	if ((rxinfo & MT_RXINFO_BA) && !(rxinfo & MT_RXINFO_NULL))
		status->aggr = true;

	if (WARN_ON_ONCE(len > skb->len))
		return -EINVAL;

	pskb_trim(skb, len);
	status->chains = BIT(0) | BIT(1);
	status->chain_signal[0] = mt76x2_phy_get_rssi(dev, rxwi->rssi[0], 0);
	status->chain_signal[1] = mt76x2_phy_get_rssi(dev, rxwi->rssi[1], 1);
	status->signal = max(status->chain_signal[0], status->chain_signal[1]);
	status->freq = dev->mt76.chandef.chan->center_freq;
	status->band = dev->mt76.chandef.chan->band;

	status->tid = FIELD_GET(MT_RXWI_TID, tid_sn);
	status->seqno = FIELD_GET(MT_RXWI_SN, tid_sn);

	return mt76x2_mac_process_rate(status, rate);
}

static int
mt76x2_mac_process_tx_rate(struct ieee80211_tx_rate *txrate, u16 rate,
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
		break;
	}

	if (rate & MT_RXWI_RATE_SGI)
		txrate->flags |= IEEE80211_TX_RC_SHORT_GI;

	return 0;
}

static void
mt76x2_mac_fill_tx_status(struct mt76x2_dev *dev,
			  struct ieee80211_tx_info *info,
			  struct mt76x2_tx_status *st, int n_frames)
{
	struct ieee80211_tx_rate *rate = info->status.rates;
	int cur_idx, last_rate;
	int i;

	if (!n_frames)
		return;

	last_rate = min_t(int, st->retry, IEEE80211_TX_MAX_RATES - 1);
	mt76x2_mac_process_tx_rate(&rate[last_rate], st->rate,
				 dev->mt76.chandef.chan->band);
	if (last_rate < IEEE80211_TX_MAX_RATES - 1)
		rate[last_rate + 1].idx = -1;

	cur_idx = rate[last_rate].idx + st->retry;
	for (i = 0; i <= last_rate; i++) {
		rate[i].flags = rate[last_rate].flags;
		rate[i].idx = max_t(int, 0, cur_idx - i);
		rate[i].count = 1;
	}

	if (last_rate > 0)
		rate[last_rate - 1].count = st->retry + 1 - last_rate;

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

static void
mt76x2_send_tx_status(struct mt76x2_dev *dev, struct mt76x2_tx_status *stat,
		      u8 *update)
{
	struct ieee80211_tx_info info = {};
	struct ieee80211_sta *sta = NULL;
	struct mt76_wcid *wcid = NULL;
	struct mt76x2_sta *msta = NULL;

	rcu_read_lock();
	if (stat->wcid < ARRAY_SIZE(dev->wcid))
		wcid = rcu_dereference(dev->wcid[stat->wcid]);

	if (wcid) {
		void *priv;

		priv = msta = container_of(wcid, struct mt76x2_sta, wcid);
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

		mt76x2_mac_fill_tx_status(dev, &info, &msta->status,
					  msta->n_frames);

		msta->status = *stat;
		msta->n_frames = 1;
		*update = 0;
	} else {
		mt76x2_mac_fill_tx_status(dev, &info, stat, 1);
		*update = 1;
	}

	ieee80211_tx_status_noskb(mt76_hw(dev), sta, &info);

out:
	rcu_read_unlock();
}

void mt76x2_mac_poll_tx_status(struct mt76x2_dev *dev, bool irq)
{
	struct mt76x2_tx_status stat = {};
	unsigned long flags;
	u8 update = 1;

	if (!test_bit(MT76_STATE_RUNNING, &dev->mt76.state))
		return;

	trace_mac_txstat_poll(dev);

	while (!irq || !kfifo_is_full(&dev->txstatus_fifo)) {
		u32 stat1, stat2;

		spin_lock_irqsave(&dev->irq_lock, flags);
		stat2 = mt76_rr(dev, MT_TX_STAT_FIFO_EXT);
		stat1 = mt76_rr(dev, MT_TX_STAT_FIFO);
		if (!(stat1 & MT_TX_STAT_FIFO_VALID)) {
			spin_unlock_irqrestore(&dev->irq_lock, flags);
			break;
		}

		spin_unlock_irqrestore(&dev->irq_lock, flags);

		stat.valid = 1;
		stat.success = !!(stat1 & MT_TX_STAT_FIFO_SUCCESS);
		stat.aggr = !!(stat1 & MT_TX_STAT_FIFO_AGGR);
		stat.ack_req = !!(stat1 & MT_TX_STAT_FIFO_ACKREQ);
		stat.wcid = FIELD_GET(MT_TX_STAT_FIFO_WCID, stat1);
		stat.rate = FIELD_GET(MT_TX_STAT_FIFO_RATE, stat1);
		stat.retry = FIELD_GET(MT_TX_STAT_FIFO_EXT_RETRY, stat2);
		stat.pktid = FIELD_GET(MT_TX_STAT_FIFO_EXT_PKTID, stat2);
		trace_mac_txstat_fetch(dev, &stat);

		if (!irq) {
			mt76x2_send_tx_status(dev, &stat, &update);
			continue;
		}

		kfifo_put(&dev->txstatus_fifo, stat);
	}
}

static void
mt76x2_mac_queue_txdone(struct mt76x2_dev *dev, struct sk_buff *skb,
			void *txwi_ptr)
{
	struct mt76x2_tx_info *txi = mt76x2_skb_tx_info(skb);
	struct mt76x2_txwi *txwi = txwi_ptr;

	mt76x2_mac_poll_tx_status(dev, false);

	txi->tries = 0;
	txi->jiffies = jiffies;
	txi->wcid = txwi->wcid;
	txi->pktid = txwi->pktid;
	trace_mac_txdone_add(dev, txwi->wcid, txwi->pktid);
	mt76x2_tx_complete(dev, skb);
}

void mt76x2_mac_process_tx_status_fifo(struct mt76x2_dev *dev)
{
	struct mt76x2_tx_status stat;
	u8 update = 1;

	while (kfifo_get(&dev->txstatus_fifo, &stat))
		mt76x2_send_tx_status(dev, &stat, &update);
}

void mt76x2_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue *q,
			    struct mt76_queue_entry *e, bool flush)
{
	struct mt76x2_dev *dev = container_of(mdev, struct mt76x2_dev, mt76);

	if (e->txwi)
		mt76x2_mac_queue_txdone(dev, e->skb, &e->txwi->txwi);
	else
		dev_kfree_skb_any(e->skb);
}

static enum mt76x2_cipher_type
mt76x2_mac_get_key_info(struct ieee80211_key_conf *key, u8 *key_data)
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

void mt76x2_mac_wcid_setup(struct mt76x2_dev *dev, u8 idx, u8 vif_idx, u8 *mac)
{
	struct mt76_wcid_addr addr = {};
	u32 attr;

	attr = FIELD_PREP(MT_WCID_ATTR_BSS_IDX, vif_idx & 7) |
	       FIELD_PREP(MT_WCID_ATTR_BSS_IDX_EXT, !!(vif_idx & 8));

	mt76_wr(dev, MT_WCID_ATTR(idx), attr);

	mt76_wr(dev, MT_WCID_TX_RATE(idx), 0);
	mt76_wr(dev, MT_WCID_TX_RATE(idx) + 4, 0);

	if (idx >= 128)
		return;

	if (mac)
		memcpy(addr.macaddr, mac, ETH_ALEN);

	mt76_wr_copy(dev, MT_WCID_ADDR(idx), &addr, sizeof(addr));
}

int mt76x2_mac_wcid_set_key(struct mt76x2_dev *dev, u8 idx,
			    struct ieee80211_key_conf *key)
{
	enum mt76x2_cipher_type cipher;
	u8 key_data[32];
	u8 iv_data[8];

	cipher = mt76x2_mac_get_key_info(key, key_data);
	if (cipher == MT_CIPHER_NONE && key)
		return -EOPNOTSUPP;

	mt76_rmw_field(dev, MT_WCID_ATTR(idx), MT_WCID_ATTR_PKEY_MODE, cipher);
	mt76_wr_copy(dev, MT_WCID_KEY(idx), key_data, sizeof(key_data));

	memset(iv_data, 0, sizeof(iv_data));
	if (key) {
		mt76_rmw_field(dev, MT_WCID_ATTR(idx), MT_WCID_ATTR_PAIRWISE,
			       !!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE));
		iv_data[3] = key->keyidx << 6;
		if (cipher >= MT_CIPHER_TKIP)
			iv_data[3] |= 0x20;
	}

	mt76_wr_copy(dev, MT_WCID_IV(idx), iv_data, sizeof(iv_data));

	return 0;
}

int mt76x2_mac_shared_key_setup(struct mt76x2_dev *dev, u8 vif_idx, u8 key_idx,
			      struct ieee80211_key_conf *key)
{
	enum mt76x2_cipher_type cipher;
	u8 key_data[32];
	u32 val;

	cipher = mt76x2_mac_get_key_info(key, key_data);
	if (cipher == MT_CIPHER_NONE && key)
		return -EOPNOTSUPP;

	val = mt76_rr(dev, MT_SKEY_MODE(vif_idx));
	val &= ~(MT_SKEY_MODE_MASK << MT_SKEY_MODE_SHIFT(vif_idx, key_idx));
	val |= cipher << MT_SKEY_MODE_SHIFT(vif_idx, key_idx);
	mt76_wr(dev, MT_SKEY_MODE(vif_idx), val);

	mt76_wr_copy(dev, MT_SKEY(vif_idx, key_idx), key_data,
		     sizeof(key_data));

	return 0;
}

static int
mt76_write_beacon(struct mt76x2_dev *dev, int offset, struct sk_buff *skb)
{
	int beacon_len = dev->beacon_offsets[1] - dev->beacon_offsets[0];
	struct mt76x2_txwi txwi;

	if (WARN_ON_ONCE(beacon_len < skb->len + sizeof(struct mt76x2_txwi)))
		return -ENOSPC;

	mt76x2_mac_write_txwi(dev, &txwi, skb, NULL, NULL);

	mt76_wr_copy(dev, offset, &txwi, sizeof(txwi));
	offset += sizeof(txwi);

	mt76_wr_copy(dev, offset, skb->data, skb->len);
	return 0;
}

static int
__mt76x2_mac_set_beacon(struct mt76x2_dev *dev, u8 bcn_idx, struct sk_buff *skb)
{
	int beacon_len = dev->beacon_offsets[1] - dev->beacon_offsets[0];
	int beacon_addr = dev->beacon_offsets[bcn_idx];
	int ret = 0;
	int i;

	/* Prevent corrupt transmissions during update */
	mt76_set(dev, MT_BCN_BYPASS_MASK, BIT(bcn_idx));

	if (skb) {
		ret = mt76_write_beacon(dev, beacon_addr, skb);
		if (!ret)
			dev->beacon_data_mask |= BIT(bcn_idx) &
						 dev->beacon_mask;
	} else {
		dev->beacon_data_mask &= ~BIT(bcn_idx);
		for (i = 0; i < beacon_len; i += 4)
			mt76_wr(dev, beacon_addr + i, 0);
	}

	mt76_wr(dev, MT_BCN_BYPASS_MASK, 0xff00 | ~dev->beacon_data_mask);

	return ret;
}

int mt76x2_mac_set_beacon(struct mt76x2_dev *dev, u8 vif_idx,
			  struct sk_buff *skb)
{
	bool force_update = false;
	int bcn_idx = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->beacons); i++) {
		if (vif_idx == i) {
			force_update = !!dev->beacons[i] ^ !!skb;

			if (dev->beacons[i])
				dev_kfree_skb(dev->beacons[i]);

			dev->beacons[i] = skb;
			__mt76x2_mac_set_beacon(dev, bcn_idx, skb);
		} else if (force_update && dev->beacons[i]) {
			__mt76x2_mac_set_beacon(dev, bcn_idx, dev->beacons[i]);
		}

		bcn_idx += !!dev->beacons[i];
	}

	for (i = bcn_idx; i < ARRAY_SIZE(dev->beacons); i++) {
		if (!(dev->beacon_data_mask & BIT(i)))
			break;

		__mt76x2_mac_set_beacon(dev, i, NULL);
	}

	mt76_rmw_field(dev, MT_MAC_BSSID_DW1, MT_MAC_BSSID_DW1_MBEACON_N,
		       bcn_idx - 1);
	return 0;
}

void mt76x2_mac_set_beacon_enable(struct mt76x2_dev *dev, u8 vif_idx, bool val)
{
	u8 old_mask = dev->beacon_mask;
	bool en;
	u32 reg;

	if (val) {
		dev->beacon_mask |= BIT(vif_idx);
	} else {
		dev->beacon_mask &= ~BIT(vif_idx);
		mt76x2_mac_set_beacon(dev, vif_idx, NULL);
	}

	if (!!old_mask == !!dev->beacon_mask)
		return;

	en = dev->beacon_mask;

	mt76_rmw_field(dev, MT_INT_TIMER_EN, MT_INT_TIMER_EN_PRE_TBTT_EN, en);
	reg = MT_BEACON_TIME_CFG_BEACON_TX |
	      MT_BEACON_TIME_CFG_TBTT_EN |
	      MT_BEACON_TIME_CFG_TIMER_EN;
	mt76_rmw(dev, MT_BEACON_TIME_CFG, reg, reg * en);

	if (en)
		mt76x2_irq_enable(dev, MT_INT_PRE_TBTT | MT_INT_TBTT);
	else
		mt76x2_irq_disable(dev, MT_INT_PRE_TBTT | MT_INT_TBTT);
}

void mt76x2_update_channel(struct mt76_dev *mdev)
{
	struct mt76x2_dev *dev = container_of(mdev, struct mt76x2_dev, mt76);
	struct mt76_channel_state *state;
	u32 active, busy;

	state = mt76_channel_state(&dev->mt76, dev->mt76.chandef.chan);

	busy = mt76_rr(dev, MT_CH_BUSY);
	active = busy + mt76_rr(dev, MT_CH_IDLE);

	spin_lock_bh(&dev->mt76.cc_lock);
	state->cc_busy += busy;
	state->cc_active += active;
	spin_unlock_bh(&dev->mt76.cc_lock);
}

void mt76x2_mac_work(struct work_struct *work)
{
	struct mt76x2_dev *dev = container_of(work, struct mt76x2_dev,
					    mac_work.work);
	int i, idx;

	mt76x2_update_channel(&dev->mt76);
	for (i = 0, idx = 0; i < 16; i++) {
		u32 val = mt76_rr(dev, MT_TX_AGG_CNT(i));

		dev->aggr_stats[idx++] += val & 0xffff;
		dev->aggr_stats[idx++] += val >> 16;
	}

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mac_work,
				     MT_CALIBRATE_INTERVAL);
}
