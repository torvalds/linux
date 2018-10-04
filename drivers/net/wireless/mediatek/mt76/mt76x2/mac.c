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

int mt76x2_mac_get_rssi(struct mt76x2_dev *dev, s8 rssi, int chain)
{
	struct mt76x2_rx_freq_cal *cal = &dev->cal.rx;

	rssi += cal->rssi_offset[chain];
	rssi -= cal->lna_gain;

	return rssi;
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
