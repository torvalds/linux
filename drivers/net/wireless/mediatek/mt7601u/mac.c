/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "mt7601u.h"
#include "trace.h"
#include <linux/etherdevice.h>

static void
mt76_mac_process_tx_rate(struct ieee80211_tx_rate *txrate, u16 rate)
{
	u8 idx = FIELD_GET(MT_TXWI_RATE_MCS, rate);

	txrate->idx = 0;
	txrate->flags = 0;
	txrate->count = 1;

	switch (FIELD_GET(MT_TXWI_RATE_PHY_MODE, rate)) {
	case MT_PHY_TYPE_OFDM:
		txrate->idx = idx + 4;
		return;
	case MT_PHY_TYPE_CCK:
		if (idx >= 8)
			idx -= 8;

		txrate->idx = idx;
		return;
	case MT_PHY_TYPE_HT_GF:
		txrate->flags |= IEEE80211_TX_RC_GREEN_FIELD;
		/* fall through */
	case MT_PHY_TYPE_HT:
		txrate->flags |= IEEE80211_TX_RC_MCS;
		txrate->idx = idx;
		break;
	default:
		WARN_ON(1);
		return;
	}

	if (FIELD_GET(MT_TXWI_RATE_BW, rate) == MT_PHY_BW_40)
		txrate->flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;

	if (rate & MT_TXWI_RATE_SGI)
		txrate->flags |= IEEE80211_TX_RC_SHORT_GI;
}

static void
mt76_mac_fill_tx_status(struct mt7601u_dev *dev, struct ieee80211_tx_info *info,
			struct mt76_tx_status *st)
{
	struct ieee80211_tx_rate *rate = info->status.rates;
	int cur_idx, last_rate;
	int i;

	last_rate = min_t(int, st->retry, IEEE80211_TX_MAX_RATES - 1);
	mt76_mac_process_tx_rate(&rate[last_rate], st->rate);
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

	info->status.ampdu_len = 1;
	info->status.ampdu_ack_len = st->success;

	if (st->is_probe)
		info->flags |= IEEE80211_TX_CTL_RATE_CTRL_PROBE;

	if (st->aggr)
		info->flags |= IEEE80211_TX_CTL_AMPDU |
			       IEEE80211_TX_STAT_AMPDU;

	if (!st->ack_req)
		info->flags |= IEEE80211_TX_CTL_NO_ACK;
	else if (st->success)
		info->flags |= IEEE80211_TX_STAT_ACK;
}

u16 mt76_mac_tx_rate_val(struct mt7601u_dev *dev,
			 const struct ieee80211_tx_rate *rate, u8 *nss_val)
{
	u16 rateval;
	u8 phy, rate_idx;
	u8 nss = 1;
	u8 bw = 0;

	if (rate->flags & IEEE80211_TX_RC_MCS) {
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

	rateval = FIELD_PREP(MT_RXWI_RATE_MCS, rate_idx);
	rateval |= FIELD_PREP(MT_RXWI_RATE_PHY, phy);
	rateval |= FIELD_PREP(MT_RXWI_RATE_BW, bw);
	if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
		rateval |= MT_RXWI_RATE_SGI;

	*nss_val = nss;
	return rateval;
}

void mt76_mac_wcid_set_rate(struct mt7601u_dev *dev, struct mt76_wcid *wcid,
			    const struct ieee80211_tx_rate *rate)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	wcid->tx_rate = mt76_mac_tx_rate_val(dev, rate, &wcid->tx_rate_nss);
	wcid->tx_rate_set = true;
	spin_unlock_irqrestore(&dev->lock, flags);
}

struct mt76_tx_status mt7601u_mac_fetch_tx_status(struct mt7601u_dev *dev)
{
	struct mt76_tx_status stat = {};
	u32 val;

	val = mt7601u_rr(dev, MT_TX_STAT_FIFO);
	stat.valid = !!(val & MT_TX_STAT_FIFO_VALID);
	stat.success = !!(val & MT_TX_STAT_FIFO_SUCCESS);
	stat.aggr = !!(val & MT_TX_STAT_FIFO_AGGR);
	stat.ack_req = !!(val & MT_TX_STAT_FIFO_ACKREQ);
	stat.pktid = FIELD_GET(MT_TX_STAT_FIFO_PID_TYPE, val);
	stat.wcid = FIELD_GET(MT_TX_STAT_FIFO_WCID, val);
	stat.rate = FIELD_GET(MT_TX_STAT_FIFO_RATE, val);

	return stat;
}

void mt76_send_tx_status(struct mt7601u_dev *dev, struct mt76_tx_status *stat)
{
	struct ieee80211_tx_info info = {};
	struct ieee80211_sta *sta = NULL;
	struct mt76_wcid *wcid = NULL;
	void *msta;

	rcu_read_lock();
	if (stat->wcid < ARRAY_SIZE(dev->wcid))
		wcid = rcu_dereference(dev->wcid[stat->wcid]);

	if (wcid) {
		msta = container_of(wcid, struct mt76_sta, wcid);
		sta = container_of(msta, struct ieee80211_sta,
				   drv_priv);
	}

	mt76_mac_fill_tx_status(dev, &info, stat);

	spin_lock_bh(&dev->mac_lock);
	ieee80211_tx_status_noskb(dev->hw, sta, &info);
	spin_unlock_bh(&dev->mac_lock);

	rcu_read_unlock();
}

void mt7601u_mac_set_protection(struct mt7601u_dev *dev, bool legacy_prot,
				int ht_mode)
{
	int mode = ht_mode & IEEE80211_HT_OP_MODE_PROTECTION;
	bool non_gf = !!(ht_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);
	u32 prot[6];
	bool ht_rts[4] = {};
	int i;

	prot[0] = MT_PROT_NAV_SHORT |
		  MT_PROT_TXOP_ALLOW_ALL |
		  MT_PROT_RTS_THR_EN;
	prot[1] = prot[0];
	if (legacy_prot)
		prot[1] |= MT_PROT_CTRL_CTS2SELF;

	prot[2] = prot[4] = MT_PROT_NAV_SHORT | MT_PROT_TXOP_ALLOW_BW20;
	prot[3] = prot[5] = MT_PROT_NAV_SHORT | MT_PROT_TXOP_ALLOW_ALL;

	if (legacy_prot) {
		prot[2] |= MT_PROT_RATE_CCK_11;
		prot[3] |= MT_PROT_RATE_CCK_11;
		prot[4] |= MT_PROT_RATE_CCK_11;
		prot[5] |= MT_PROT_RATE_CCK_11;
	} else {
		prot[2] |= MT_PROT_RATE_OFDM_24;
		prot[3] |= MT_PROT_RATE_DUP_OFDM_24;
		prot[4] |= MT_PROT_RATE_OFDM_24;
		prot[5] |= MT_PROT_RATE_DUP_OFDM_24;
	}

	switch (mode) {
	case IEEE80211_HT_OP_MODE_PROTECTION_NONE:
		break;

	case IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER:
		ht_rts[0] = ht_rts[1] = ht_rts[2] = ht_rts[3] = true;
		break;

	case IEEE80211_HT_OP_MODE_PROTECTION_20MHZ:
		ht_rts[1] = ht_rts[3] = true;
		break;

	case IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED:
		ht_rts[0] = ht_rts[1] = ht_rts[2] = ht_rts[3] = true;
		break;
	}

	if (non_gf)
		ht_rts[2] = ht_rts[3] = true;

	for (i = 0; i < 4; i++)
		if (ht_rts[i])
			prot[i + 2] |= MT_PROT_CTRL_RTS_CTS;

	for (i = 0; i < 6; i++)
		mt7601u_wr(dev, MT_CCK_PROT_CFG + i * 4, prot[i]);
}

void mt7601u_mac_set_short_preamble(struct mt7601u_dev *dev, bool short_preamb)
{
	if (short_preamb)
		mt76_set(dev, MT_AUTO_RSP_CFG, MT_AUTO_RSP_PREAMB_SHORT);
	else
		mt76_clear(dev, MT_AUTO_RSP_CFG, MT_AUTO_RSP_PREAMB_SHORT);
}

void mt7601u_mac_config_tsf(struct mt7601u_dev *dev, bool enable, int interval)
{
	u32 val = mt7601u_rr(dev, MT_BEACON_TIME_CFG);

	val &= ~(MT_BEACON_TIME_CFG_TIMER_EN |
		 MT_BEACON_TIME_CFG_SYNC_MODE |
		 MT_BEACON_TIME_CFG_TBTT_EN);

	if (!enable) {
		mt7601u_wr(dev, MT_BEACON_TIME_CFG, val);
		return;
	}

	val &= ~MT_BEACON_TIME_CFG_INTVAL;
	val |= FIELD_PREP(MT_BEACON_TIME_CFG_INTVAL, interval << 4) |
		MT_BEACON_TIME_CFG_TIMER_EN |
		MT_BEACON_TIME_CFG_SYNC_MODE |
		MT_BEACON_TIME_CFG_TBTT_EN;
}

static void mt7601u_check_mac_err(struct mt7601u_dev *dev)
{
	u32 val = mt7601u_rr(dev, 0x10f4);

	if (!(val & BIT(29)) || !(val & (BIT(7) | BIT(5))))
		return;

	dev_err(dev->dev, "Error: MAC specific condition occurred\n");

	mt76_set(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_RESET_CSR);
	udelay(10);
	mt76_clear(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_RESET_CSR);
}

void mt7601u_mac_work(struct work_struct *work)
{
	struct mt7601u_dev *dev = container_of(work, struct mt7601u_dev,
					       mac_work.work);
	struct {
		u32 addr_base;
		u32 span;
		u64 *stat_base;
	} spans[] = {
		{ MT_RX_STA_CNT0,	3,	dev->stats.rx_stat },
		{ MT_TX_STA_CNT0,	3,	dev->stats.tx_stat },
		{ MT_TX_AGG_STAT,	1,	dev->stats.aggr_stat },
		{ MT_MPDU_DENSITY_CNT,	1,	dev->stats.zero_len_del },
		{ MT_TX_AGG_CNT_BASE0,	8,	&dev->stats.aggr_n[0] },
		{ MT_TX_AGG_CNT_BASE1,	8,	&dev->stats.aggr_n[16] },
	};
	u32 sum, n;
	int i, j, k;

	/* Note: using MCU_RANDOM_READ is actually slower then reading all the
	 *	 registers by hand.  MCU takes ca. 20ms to complete read of 24
	 *	 registers while reading them one by one will takes roughly
	 *	 24*200us =~ 5ms.
	 */

	k = 0;
	n = 0;
	sum = 0;
	for (i = 0; i < ARRAY_SIZE(spans); i++)
		for (j = 0; j < spans[i].span; j++) {
			u32 val = mt7601u_rr(dev, spans[i].addr_base + j * 4);

			spans[i].stat_base[j * 2] += val & 0xffff;
			spans[i].stat_base[j * 2 + 1] += val >> 16;

			/* Calculate average AMPDU length */
			if (spans[i].addr_base != MT_TX_AGG_CNT_BASE0 &&
			    spans[i].addr_base != MT_TX_AGG_CNT_BASE1)
				continue;

			n += (val >> 16) + (val & 0xffff);
			sum += (val & 0xffff) * (1 + k * 2) +
				(val >> 16) * (2 + k * 2);
			k++;
		}

	atomic_set(&dev->avg_ampdu_len, n ? DIV_ROUND_CLOSEST(sum, n) : 1);

	mt7601u_check_mac_err(dev);

	ieee80211_queue_delayed_work(dev->hw, &dev->mac_work, 10 * HZ);
}

void
mt7601u_mac_wcid_setup(struct mt7601u_dev *dev, u8 idx, u8 vif_idx, u8 *mac)
{
	u8 zmac[ETH_ALEN] = {};
	u32 attr;

	attr = FIELD_PREP(MT_WCID_ATTR_BSS_IDX, vif_idx & 7) |
	       FIELD_PREP(MT_WCID_ATTR_BSS_IDX_EXT, !!(vif_idx & 8));

	mt76_wr(dev, MT_WCID_ATTR(idx), attr);

	if (mac)
		memcpy(zmac, mac, sizeof(zmac));

	mt7601u_addr_wr(dev, MT_WCID_ADDR(idx), zmac);
}

void mt7601u_mac_set_ampdu_factor(struct mt7601u_dev *dev)
{
	struct ieee80211_sta *sta;
	struct mt76_wcid *wcid;
	void *msta;
	u8 min_factor = 3;
	int i;

	rcu_read_lock();
	for (i = 0; i < ARRAY_SIZE(dev->wcid); i++) {
		wcid = rcu_dereference(dev->wcid[i]);
		if (!wcid)
			continue;

		msta = container_of(wcid, struct mt76_sta, wcid);
		sta = container_of(msta, struct ieee80211_sta, drv_priv);

		min_factor = min(min_factor, sta->ht_cap.ampdu_factor);
	}
	rcu_read_unlock();

	mt7601u_wr(dev, MT_MAX_LEN_CFG, 0xa0fff |
		   FIELD_PREP(MT_MAX_LEN_CFG_AMPDU, min_factor));
}

static void
mt76_mac_process_rate(struct ieee80211_rx_status *status, u16 rate)
{
	u8 idx = FIELD_GET(MT_RXWI_RATE_MCS, rate);

	switch (FIELD_GET(MT_RXWI_RATE_PHY, rate)) {
	case MT_PHY_TYPE_OFDM:
		if (WARN_ON(idx >= 8))
			idx = 0;
		idx += 4;

		status->rate_idx = idx;
		return;
	case MT_PHY_TYPE_CCK:
		if (idx >= 8) {
			idx -= 8;
			status->enc_flags |= RX_ENC_FLAG_SHORTPRE;
		}

		if (WARN_ON(idx >= 4))
			idx = 0;

		status->rate_idx = idx;
		return;
	case MT_PHY_TYPE_HT_GF:
		status->enc_flags |= RX_ENC_FLAG_HT_GF;
		/* fall through */
	case MT_PHY_TYPE_HT:
		status->encoding = RX_ENC_HT;
		status->rate_idx = idx;
		break;
	default:
		WARN_ON(1);
		return;
	}

	if (rate & MT_RXWI_RATE_SGI)
		status->enc_flags |= RX_ENC_FLAG_SHORT_GI;

	if (rate & MT_RXWI_RATE_STBC)
		status->enc_flags |= 1 << RX_ENC_FLAG_STBC_SHIFT;

	if (rate & MT_RXWI_RATE_BW)
		status->bw = RATE_INFO_BW_40;
}

static void
mt7601u_rx_monitor_beacon(struct mt7601u_dev *dev, struct mt7601u_rxwi *rxwi,
			  u16 rate, int rssi)
{
	dev->bcn_freq_off = rxwi->freq_off;
	dev->bcn_phy_mode = FIELD_GET(MT_RXWI_RATE_PHY, rate);
	dev->avg_rssi = (dev->avg_rssi * 15) / 16 + (rssi << 8);
}

static int
mt7601u_rx_is_our_beacon(struct mt7601u_dev *dev, u8 *data)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)data;

	return ieee80211_is_beacon(hdr->frame_control) &&
		ether_addr_equal(hdr->addr2, dev->ap_bssid);
}

u32 mt76_mac_process_rx(struct mt7601u_dev *dev, struct sk_buff *skb,
			u8 *data, void *rxi)
{
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct mt7601u_rxwi *rxwi = rxi;
	u32 len, ctl = le32_to_cpu(rxwi->ctl);
	u16 rate = le16_to_cpu(rxwi->rate);
	int rssi;

	len = FIELD_GET(MT_RXWI_CTL_MPDU_LEN, ctl);
	if (len < 10)
		return 0;

	if (rxwi->rxinfo & cpu_to_le32(MT_RXINFO_DECRYPT)) {
		status->flag |= RX_FLAG_DECRYPTED;
		status->flag |= RX_FLAG_IV_STRIPPED | RX_FLAG_MMIC_STRIPPED;
	}

	status->chains = BIT(0);
	rssi = mt7601u_phy_get_rssi(dev, rxwi, rate);
	status->chain_signal[0] = status->signal = rssi;
	status->freq = dev->chandef.chan->center_freq;
	status->band = dev->chandef.chan->band;

	mt76_mac_process_rate(status, rate);

	spin_lock_bh(&dev->con_mon_lock);
	if (mt7601u_rx_is_our_beacon(dev, data))
		mt7601u_rx_monitor_beacon(dev, rxwi, rate, rssi);
	else if (rxwi->rxinfo & cpu_to_le32(MT_RXINFO_U2M))
		dev->avg_rssi = (dev->avg_rssi * 15) / 16 + (rssi << 8);
	spin_unlock_bh(&dev->con_mon_lock);

	return len;
}

static enum mt76_cipher_type
mt76_mac_get_key_info(struct ieee80211_key_conf *key, u8 *key_data)
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

int mt76_mac_wcid_set_key(struct mt7601u_dev *dev, u8 idx,
			  struct ieee80211_key_conf *key)
{
	enum mt76_cipher_type cipher;
	u8 key_data[32];
	u8 iv_data[8];
	u32 val;

	cipher = mt76_mac_get_key_info(key, key_data);
	if (cipher == MT_CIPHER_NONE && key)
		return -EINVAL;

	trace_set_key(dev, idx);

	mt7601u_wr_copy(dev, MT_WCID_KEY(idx), key_data, sizeof(key_data));

	memset(iv_data, 0, sizeof(iv_data));
	if (key) {
		iv_data[3] = key->keyidx << 6;
		if (cipher >= MT_CIPHER_TKIP) {
			/* Note: start with 1 to comply with spec,
			 *	 (see comment on common/cmm_wpa.c:4291).
			 */
			iv_data[0] |= 1;
			iv_data[3] |= 0x20;
		}
	}
	mt7601u_wr_copy(dev, MT_WCID_IV(idx), iv_data, sizeof(iv_data));

	val = mt7601u_rr(dev, MT_WCID_ATTR(idx));
	val &= ~MT_WCID_ATTR_PKEY_MODE & ~MT_WCID_ATTR_PKEY_MODE_EXT;
	val |= FIELD_PREP(MT_WCID_ATTR_PKEY_MODE, cipher & 7) |
	       FIELD_PREP(MT_WCID_ATTR_PKEY_MODE_EXT, cipher >> 3);
	val &= ~MT_WCID_ATTR_PAIRWISE;
	val |= MT_WCID_ATTR_PAIRWISE *
		!!(key && key->flags & IEEE80211_KEY_FLAG_PAIRWISE);
	mt7601u_wr(dev, MT_WCID_ATTR(idx), val);

	return 0;
}

int mt76_mac_shared_key_setup(struct mt7601u_dev *dev, u8 vif_idx, u8 key_idx,
			      struct ieee80211_key_conf *key)
{
	enum mt76_cipher_type cipher;
	u8 key_data[32];
	u32 val;

	cipher = mt76_mac_get_key_info(key, key_data);
	if (cipher == MT_CIPHER_NONE && key)
		return -EINVAL;

	trace_set_shared_key(dev, vif_idx, key_idx);

	mt7601u_wr_copy(dev, MT_SKEY(vif_idx, key_idx),
			key_data, sizeof(key_data));

	val = mt76_rr(dev, MT_SKEY_MODE(vif_idx));
	val &= ~(MT_SKEY_MODE_MASK << MT_SKEY_MODE_SHIFT(vif_idx, key_idx));
	val |= cipher << MT_SKEY_MODE_SHIFT(vif_idx, key_idx);
	mt76_wr(dev, MT_SKEY_MODE(vif_idx), val);

	return 0;
}
