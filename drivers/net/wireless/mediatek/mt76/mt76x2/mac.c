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

#include "mt76x2.h"
#include "../mt76x02_util.h"

void mt76x2_mac_stop(struct mt76x2_dev *dev, bool force)
{
	bool stopped = false;
	u32 rts_cfg;
	int i;

	mt76_wr(dev, MT_MAC_SYS_CTRL, 0);

	rts_cfg = mt76_rr(dev, MT_TX_RTS_CFG);
	mt76_wr(dev, MT_TX_RTS_CFG, rts_cfg & ~MT_TX_RTS_CFG_RETRY_LIMIT);

	/* Wait for MAC to become idle */
	for (i = 0; i < 300; i++) {
		if ((mt76_rr(dev, MT_MAC_STATUS) &
		     (MT_MAC_STATUS_RX | MT_MAC_STATUS_TX)) ||
		    mt76_rr(dev, MT_BBP(IBI, 12))) {
			udelay(1);
			continue;
		}

		stopped = true;
		break;
	}

	if (force && !stopped) {
		mt76_set(dev, MT_BBP(CORE, 4), BIT(1));
		mt76_clear(dev, MT_BBP(CORE, 4), BIT(1));

		mt76_set(dev, MT_BBP(CORE, 4), BIT(0));
		mt76_clear(dev, MT_BBP(CORE, 4), BIT(0));
	}

	mt76_wr(dev, MT_TX_RTS_CFG, rts_cfg);
}
EXPORT_SYMBOL_GPL(mt76x2_mac_stop);

void mt76x2_mac_write_txwi(struct mt76x2_dev *dev, struct mt76x02_txwi *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid,
			   struct ieee80211_sta *sta, int len)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rate = &info->control.rates[0];
	struct ieee80211_key_conf *key = info->control.hw_key;
	u16 rate_ht_mask = FIELD_PREP(MT_RXWI_RATE_PHY, BIT(1) | BIT(2));
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
		txwi->iv = *((__le32 *)&ccmp_pn[0]);
		txwi->eiv = *((__le32 *)&ccmp_pn[1]);
	}

	spin_lock_bh(&dev->mt76.lock);
	if (wcid && (rate->idx < 0 || !rate->count)) {
		txwi->rate = wcid->tx_rate;
		max_txpwr_adj = wcid->max_txpwr_adj;
		nss = wcid->tx_rate_nss;
	} else {
		txwi->rate = mt76x02_mac_tx_rate_val(&dev->mt76, rate, &nss);
		max_txpwr_adj = mt76x02_tx_get_max_txpwr_adj(&dev->mt76, rate);
	}
	spin_unlock_bh(&dev->mt76.lock);

	txpwr_adj = mt76x2_tx_get_txpwr_adj(&dev->mt76, dev->mt76.txpower_conf,
					    max_txpwr_adj);
	txwi->ctl2 = FIELD_PREP(MT_TX_PWR_ADJ, txpwr_adj);

	if (mt76xx_rev(dev) >= MT76XX_REV_E4)
		txwi->txstream = 0x13;
	else if (mt76xx_rev(dev) >= MT76XX_REV_E3 &&
		 !(txwi->rate & cpu_to_le16(rate_ht_mask)))
		txwi->txstream = 0x93;

	mt76x02_mac_fill_txwi(txwi, skb, sta, len, nss);
}
EXPORT_SYMBOL_GPL(mt76x2_mac_write_txwi);

int mt76x2_mac_get_rssi(struct mt76x2_dev *dev, s8 rssi, int chain)
{
	struct mt76x2_rx_freq_cal *cal = &dev->cal.rx;

	rssi += cal->rssi_offset[chain];
	rssi -= cal->lna_gain;

	return rssi;
}

static struct mt76x02_sta *
mt76x2_rx_get_sta(struct mt76x2_dev *dev, u8 idx)
{
	struct mt76_wcid *wcid;

	if (idx >= ARRAY_SIZE(dev->mt76.wcid))
		return NULL;

	wcid = rcu_dereference(dev->mt76.wcid[idx]);
	if (!wcid)
		return NULL;

	return container_of(wcid, struct mt76x02_sta, wcid);
}

static struct mt76_wcid *
mt76x2_rx_get_sta_wcid(struct mt76x2_dev *dev, struct mt76x02_sta *sta,
		       bool unicast)
{
	if (!sta)
		return NULL;

	if (unicast)
		return &sta->wcid;
	else
		return &sta->vif->group_wcid;
}

int mt76x2_mac_process_rx(struct mt76x2_dev *dev, struct sk_buff *skb,
			  void *rxi)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *) skb->cb;
	struct mt76x02_rxwi *rxwi = rxi;
	struct mt76x02_sta *sta;
	u32 rxinfo = le32_to_cpu(rxwi->rxinfo);
	u32 ctl = le32_to_cpu(rxwi->ctl);
	u16 rate = le16_to_cpu(rxwi->rate);
	u16 tid_sn = le16_to_cpu(rxwi->tid_sn);
	bool unicast = rxwi->rxinfo & cpu_to_le32(MT_RXINFO_UNICAST);
	int pad_len = 0;
	u8 pn_len;
	u8 wcid;
	int len;

	if (!test_bit(MT76_STATE_RUNNING, &dev->mt76.state))
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
	sta = mt76x2_rx_get_sta(dev, wcid);
	status->wcid = mt76x2_rx_get_sta_wcid(dev, sta, unicast);

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

	if (WARN_ON_ONCE(len > skb->len))
		return -EINVAL;

	pskb_trim(skb, len);
	status->chains = BIT(0) | BIT(1);
	status->chain_signal[0] = mt76x2_mac_get_rssi(dev, rxwi->rssi[0], 0);
	status->chain_signal[1] = mt76x2_mac_get_rssi(dev, rxwi->rssi[1], 1);
	status->signal = max(status->chain_signal[0], status->chain_signal[1]);
	status->freq = dev->mt76.chandef.chan->center_freq;
	status->band = dev->mt76.chandef.chan->band;

	status->tid = FIELD_GET(MT_RXWI_TID, tid_sn);
	status->seqno = FIELD_GET(MT_RXWI_SN, tid_sn);

	if (sta) {
		ewma_signal_add(&sta->rssi, status->signal);
		sta->inactive_count = 0;
	}

	return mt76x02_mac_process_rate(status, rate);
}
EXPORT_SYMBOL_GPL(mt76x2_mac_process_rx);
