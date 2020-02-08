// SPDX-License-Identifier: ISC
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 *         Roy Luo <royluo@google.com>
 *         Felix Fietkau <nbd@nbd.name>
 *         Lorenzo Bianconi <lorenzo@kernel.org>
 */

#include <linux/etherdevice.h>
#include <linux/timekeeping.h>
#include "mt7615.h"
#include "../trace.h"
#include "../dma.h"
#include "mt7615_trace.h"
#include "mac.h"

#define to_rssi(field, rxv)		((FIELD_GET(field, rxv) - 220) / 2)

static const struct mt7615_dfs_radar_spec etsi_radar_specs = {
	.pulse_th = { 40, -10, -80, 800, 3360, 128, 5200 },
	.radar_pattern = {
		[5] =  { 1, 0,  6, 32, 28, 0, 17,  990, 5010, 1, 1 },
		[6] =  { 1, 0,  9, 32, 28, 0, 27,  615, 5010, 1, 1 },
		[7] =  { 1, 0, 15, 32, 28, 0, 27,  240,  445, 1, 1 },
		[8] =  { 1, 0, 12, 32, 28, 0, 42,  240,  510, 1, 1 },
		[9] =  { 1, 1,  0,  0,  0, 0, 14, 2490, 3343, 0, 0, 12, 32, 28 },
		[10] = { 1, 1,  0,  0,  0, 0, 14, 2490, 3343, 0, 0, 15, 32, 24 },
		[11] = { 1, 1,  0,  0,  0, 0, 14,  823, 2510, 0, 0, 18, 32, 28 },
		[12] = { 1, 1,  0,  0,  0, 0, 14,  823, 2510, 0, 0, 27, 32, 24 },
	},
};

static const struct mt7615_dfs_radar_spec fcc_radar_specs = {
	.pulse_th = { 40, -10, -80, 800, 3360, 128, 5200 },
	.radar_pattern = {
		[0] = { 1, 0,  9,  32, 28, 0, 13, 508, 3076, 1,  1 },
		[1] = { 1, 0, 12,  32, 28, 0, 17, 140,  240, 1,  1 },
		[2] = { 1, 0,  8,  32, 28, 0, 22, 190,  510, 1,  1 },
		[3] = { 1, 0,  6,  32, 28, 0, 32, 190,  510, 1,  1 },
		[4] = { 1, 0,  9, 255, 28, 0, 13, 323,  343, 1, 32 },
	},
};

static const struct mt7615_dfs_radar_spec jp_radar_specs = {
	.pulse_th = { 40, -10, -80, 800, 3360, 128, 5200 },
	.radar_pattern = {
		[0] =  { 1, 0,  8, 32, 28, 0, 13,  508, 3076, 1,  1 },
		[1] =  { 1, 0, 12, 32, 28, 0, 17,  140,  240, 1,  1 },
		[2] =  { 1, 0,  8, 32, 28, 0, 22,  190,  510, 1,  1 },
		[3] =  { 1, 0,  6, 32, 28, 0, 32,  190,  510, 1,  1 },
		[4] =  { 1, 0,  9, 32, 28, 0, 13,  323,  343, 1, 32 },
		[13] = { 1, 0, 8,  32, 28, 0, 14, 3836, 3856, 1,  1 },
		[14] = { 1, 0, 8,  32, 28, 0, 14, 3990, 4010, 1,  1 },
	},
};

static struct mt76_wcid *mt7615_rx_get_wcid(struct mt7615_dev *dev,
					    u8 idx, bool unicast)
{
	struct mt7615_sta *sta;
	struct mt76_wcid *wcid;

	if (idx >= ARRAY_SIZE(dev->mt76.wcid))
		return NULL;

	wcid = rcu_dereference(dev->mt76.wcid[idx]);
	if (unicast || !wcid)
		return wcid;

	if (!wcid->sta)
		return NULL;

	sta = container_of(wcid, struct mt7615_sta, wcid);
	if (!sta->vif)
		return NULL;

	return &sta->vif->sta.wcid;
}

void mt7615_mac_reset_counters(struct mt7615_dev *dev)
{
	int i;

	for (i = 0; i < 4; i++)
		mt76_rr(dev, MT_TX_AGG_CNT(i));

	memset(dev->mt76.aggr_stats, 0, sizeof(dev->mt76.aggr_stats));
	dev->mt76.phy.survey_time = ktime_get_boottime();
	if (dev->mt76.phy2)
		dev->mt76.phy2->survey_time = ktime_get_boottime();

	/* reset airtime counters */
	mt76_rr(dev, MT_MIB_SDR9(0));
	mt76_rr(dev, MT_MIB_SDR9(1));

	mt76_rr(dev, MT_MIB_SDR36(0));
	mt76_rr(dev, MT_MIB_SDR36(1));

	mt76_rr(dev, MT_MIB_SDR37(0));
	mt76_rr(dev, MT_MIB_SDR37(1));

	mt76_set(dev, MT_WF_RMAC_MIB_TIME0, MT_WF_RMAC_MIB_RXTIME_CLR);
	mt76_set(dev, MT_WF_RMAC_MIB_AIRTIME0, MT_WF_RMAC_MIB_RXTIME_CLR);
}

void mt7615_mac_set_timing(struct mt7615_phy *phy)
{
	s16 coverage_class = phy->coverage_class;
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	u32 val, reg_offset;
	u32 cck = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 231) |
		  FIELD_PREP(MT_TIMEOUT_VAL_CCA, 48);
	u32 ofdm = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 60) |
		   FIELD_PREP(MT_TIMEOUT_VAL_CCA, 24);
	int sifs, offset;

	if (phy->mt76->chandef.chan->band == NL80211_BAND_5GHZ)
		sifs = 16;
	else
		sifs = 10;

	if (ext_phy) {
		coverage_class = max_t(s16, dev->phy.coverage_class,
				       coverage_class);
		mt76_set(dev, MT_ARB_SCR,
			 MT_ARB_SCR_TX1_DISABLE | MT_ARB_SCR_RX1_DISABLE);
	} else {
		struct mt7615_phy *phy_ext = mt7615_ext_phy(dev);

		if (phy_ext)
			coverage_class = max_t(s16, phy_ext->coverage_class,
					       coverage_class);
		mt76_set(dev, MT_ARB_SCR,
			 MT_ARB_SCR_TX0_DISABLE | MT_ARB_SCR_RX0_DISABLE);
	}
	udelay(1);

	offset = 3 * coverage_class;
	reg_offset = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, offset) |
		     FIELD_PREP(MT_TIMEOUT_VAL_CCA, offset);
	mt76_wr(dev, MT_TMAC_CDTR, cck + reg_offset);
	mt76_wr(dev, MT_TMAC_ODTR, ofdm + reg_offset);

	mt76_wr(dev, MT_TMAC_ICR(ext_phy),
		FIELD_PREP(MT_IFS_EIFS, 360) |
		FIELD_PREP(MT_IFS_RIFS, 2) |
		FIELD_PREP(MT_IFS_SIFS, sifs) |
		FIELD_PREP(MT_IFS_SLOT, phy->slottime));

	if (phy->slottime < 20)
		val = MT7615_CFEND_RATE_DEFAULT;
	else
		val = MT7615_CFEND_RATE_11B;

	mt76_rmw_field(dev, MT_AGG_ACR(ext_phy), MT_AGG_ACR_CFEND_RATE, val);
	if (ext_phy)
		mt76_clear(dev, MT_ARB_SCR,
			   MT_ARB_SCR_TX1_DISABLE | MT_ARB_SCR_RX1_DISABLE);
	else
		mt76_clear(dev, MT_ARB_SCR,
			   MT_ARB_SCR_TX0_DISABLE | MT_ARB_SCR_RX0_DISABLE);

}

int mt7615_mac_fill_rx(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7615_phy *phy = &dev->phy;
	struct mt7615_phy *phy2 = dev->mt76.phy2 ? dev->mt76.phy2->priv : NULL;
	struct ieee80211_supported_band *sband;
	struct ieee80211_hdr *hdr;
	__le32 *rxd = (__le32 *)skb->data;
	u32 rxd0 = le32_to_cpu(rxd[0]);
	u32 rxd1 = le32_to_cpu(rxd[1]);
	u32 rxd2 = le32_to_cpu(rxd[2]);
	__le32 rxd12 = rxd[12];
	bool unicast, remove_pad, insert_ccmp_hdr = false;
	int phy_idx;
	int i, idx;
	u8 chfreq;

	memset(status, 0, sizeof(*status));

	chfreq = FIELD_GET(MT_RXD1_NORMAL_CH_FREQ, rxd1);
	if (!phy2)
		phy_idx = 0;
	else if (phy2->chfreq == phy->chfreq)
		phy_idx = -1;
	else if (phy->chfreq == chfreq)
		phy_idx = 0;
	else if (phy2->chfreq == chfreq)
		phy_idx = 1;
	else
		phy_idx = -1;

	unicast = (rxd1 & MT_RXD1_NORMAL_ADDR_TYPE) == MT_RXD1_NORMAL_U2M;
	idx = FIELD_GET(MT_RXD2_NORMAL_WLAN_IDX, rxd2);
	status->wcid = mt7615_rx_get_wcid(dev, idx, unicast);

	if (status->wcid) {
		struct mt7615_sta *msta;

		msta = container_of(status->wcid, struct mt7615_sta, wcid);
		spin_lock_bh(&dev->sta_poll_lock);
		if (list_empty(&msta->poll_list))
			list_add_tail(&msta->poll_list, &dev->sta_poll_list);
		spin_unlock_bh(&dev->sta_poll_lock);
	}

	if (rxd2 & MT_RXD2_NORMAL_FCS_ERR)
		status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (rxd2 & MT_RXD2_NORMAL_TKIP_MIC_ERR)
		status->flag |= RX_FLAG_MMIC_ERROR;

	if (FIELD_GET(MT_RXD2_NORMAL_SEC_MODE, rxd2) != 0 &&
	    !(rxd2 & (MT_RXD2_NORMAL_CLM | MT_RXD2_NORMAL_CM))) {
		status->flag |= RX_FLAG_DECRYPTED;
		status->flag |= RX_FLAG_IV_STRIPPED;
		status->flag |= RX_FLAG_MMIC_STRIPPED | RX_FLAG_MIC_STRIPPED;
	}

	remove_pad = rxd1 & MT_RXD1_NORMAL_HDR_OFFSET;

	if (rxd2 & MT_RXD2_NORMAL_MAX_LEN_ERROR)
		return -EINVAL;

	rxd += 4;
	if (rxd0 & MT_RXD0_NORMAL_GROUP_4) {
		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd0 & MT_RXD0_NORMAL_GROUP_1) {
		u8 *data = (u8 *)rxd;

		if (status->flag & RX_FLAG_DECRYPTED) {
			status->iv[0] = data[5];
			status->iv[1] = data[4];
			status->iv[2] = data[3];
			status->iv[3] = data[2];
			status->iv[4] = data[1];
			status->iv[5] = data[0];

			insert_ccmp_hdr = FIELD_GET(MT_RXD2_NORMAL_FRAG, rxd2);
		}
		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd0 & MT_RXD0_NORMAL_GROUP_2) {
		rxd += 2;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd0 & MT_RXD0_NORMAL_GROUP_3) {
		u32 rxdg5 = le32_to_cpu(rxd[5]);

		/*
		 * If both PHYs are on the same channel and we don't have a WCID,
		 * we need to figure out which PHY this packet was received on.
		 * On the primary PHY, the noise value for the chains belonging to the
		 * second PHY will be set to the noise value of the last packet from
		 * that PHY.
		 */
		if (phy_idx < 0) {
			int first_chain = ffs(phy2->chainmask) - 1;

			phy_idx = ((rxdg5 >> (first_chain * 8)) & 0xff) == 0;
		}
	}

	if (phy_idx == 1 && phy2) {
		mphy = dev->mt76.phy2;
		phy = phy2;
		status->ext_phy = true;
	}

	if (chfreq != phy->chfreq)
		return -EINVAL;

	status->freq = mphy->chandef.chan->center_freq;
	status->band = mphy->chandef.chan->band;
	if (status->band == NL80211_BAND_5GHZ)
		sband = &mphy->sband_5g.sband;
	else
		sband = &mphy->sband_2g.sband;

	if (!test_bit(MT76_STATE_RUNNING, &mphy->state))
		return -EINVAL;

	if (!sband->channels)
		return -EINVAL;

	if (!(rxd2 & (MT_RXD2_NORMAL_NON_AMPDU_SUB |
		      MT_RXD2_NORMAL_NON_AMPDU))) {
		status->flag |= RX_FLAG_AMPDU_DETAILS;

		/* all subframes of an A-MPDU have the same timestamp */
		if (phy->rx_ampdu_ts != rxd12) {
			if (!++phy->ampdu_ref)
				phy->ampdu_ref++;
		}
		phy->rx_ampdu_ts = rxd12;

		status->ampdu_ref = phy->ampdu_ref;
	}

	if (rxd0 & MT_RXD0_NORMAL_GROUP_3) {
		u32 rxdg0 = le32_to_cpu(rxd[0]);
		u32 rxdg1 = le32_to_cpu(rxd[1]);
		u32 rxdg3 = le32_to_cpu(rxd[3]);
		u8 stbc = FIELD_GET(MT_RXV1_HT_STBC, rxdg0);
		bool cck = false;

		i = FIELD_GET(MT_RXV1_TX_RATE, rxdg0);
		switch (FIELD_GET(MT_RXV1_TX_MODE, rxdg0)) {
		case MT_PHY_TYPE_CCK:
			cck = true;
			/* fall through */
		case MT_PHY_TYPE_OFDM:
			i = mt76_get_rate(&dev->mt76, sband, i, cck);
			break;
		case MT_PHY_TYPE_HT_GF:
		case MT_PHY_TYPE_HT:
			status->encoding = RX_ENC_HT;
			if (i > 31)
				return -EINVAL;
			break;
		case MT_PHY_TYPE_VHT:
			status->nss = FIELD_GET(MT_RXV2_NSTS, rxdg1) + 1;
			status->encoding = RX_ENC_VHT;
			break;
		default:
			return -EINVAL;
		}
		status->rate_idx = i;

		switch (FIELD_GET(MT_RXV1_FRAME_MODE, rxdg0)) {
		case MT_PHY_BW_20:
			break;
		case MT_PHY_BW_40:
			status->bw = RATE_INFO_BW_40;
			break;
		case MT_PHY_BW_80:
			status->bw = RATE_INFO_BW_80;
			break;
		case MT_PHY_BW_160:
			status->bw = RATE_INFO_BW_160;
			break;
		default:
			return -EINVAL;
		}

		if (rxdg0 & MT_RXV1_HT_SHORT_GI)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		if (rxdg0 & MT_RXV1_HT_AD_CODE)
			status->enc_flags |= RX_ENC_FLAG_LDPC;

		status->enc_flags |= RX_ENC_FLAG_STBC_MASK * stbc;

		status->chains = mphy->antenna_mask;
		status->chain_signal[0] = to_rssi(MT_RXV4_RCPI0, rxdg3);
		status->chain_signal[1] = to_rssi(MT_RXV4_RCPI1, rxdg3);
		status->chain_signal[2] = to_rssi(MT_RXV4_RCPI2, rxdg3);
		status->chain_signal[3] = to_rssi(MT_RXV4_RCPI3, rxdg3);
		status->signal = status->chain_signal[0];

		for (i = 1; i < hweight8(mphy->antenna_mask); i++) {
			if (!(status->chains & BIT(i)))
				continue;

			status->signal = max(status->signal,
					     status->chain_signal[i]);
		}

		rxd += 6;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	skb_pull(skb, (u8 *)rxd - skb->data + 2 * remove_pad);

	if (insert_ccmp_hdr) {
		u8 key_id = FIELD_GET(MT_RXD1_NORMAL_KEY_ID, rxd1);

		mt76_insert_ccmp_hdr(skb, key_id);
	}

	hdr = (struct ieee80211_hdr *)skb->data;
	if (!status->wcid || !ieee80211_is_data_qos(hdr->frame_control))
		return 0;

	status->aggr = unicast &&
		       !ieee80211_is_qos_nullfunc(hdr->frame_control);
	status->tid = *ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_TID_MASK;
	status->seqno = IEEE80211_SEQ_TO_SN(le16_to_cpu(hdr->seq_ctrl));

	return 0;
}

void mt7615_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps)
{
}

void mt7615_tx_complete_skb(struct mt76_dev *mdev, enum mt76_txq_id qid,
			    struct mt76_queue_entry *e)
{
	if (!e->txwi) {
		dev_kfree_skb_any(e->skb);
		return;
	}

	/* error path */
	if (e->skb == DMA_DUMMY_DATA) {
		struct mt76_txwi_cache *t;
		struct mt7615_dev *dev;
		struct mt7615_txp_common *txp;
		u16 token;

		dev = container_of(mdev, struct mt7615_dev, mt76);
		txp = mt7615_txwi_to_txp(mdev, e->txwi);

		if (is_mt7615(&dev->mt76))
			token = le16_to_cpu(txp->fw.token);
		else
			token = le16_to_cpu(txp->hw.msdu_id[0]) &
				~MT_MSDU_ID_VALID;

		spin_lock_bh(&dev->token_lock);
		t = idr_remove(&dev->token, token);
		spin_unlock_bh(&dev->token_lock);
		e->skb = t ? t->skb : NULL;
	}

	if (e->skb)
		mt76_tx_complete_skb(mdev, e->skb);
}

static u16
mt7615_mac_tx_rate_val(struct mt7615_dev *dev,
		       struct mt76_phy *mphy,
		       const struct ieee80211_tx_rate *rate,
		       bool stbc, u8 *bw)
{
	u8 phy, nss, rate_idx;
	u16 rateval = 0;

	*bw = 0;

	if (rate->flags & IEEE80211_TX_RC_VHT_MCS) {
		rate_idx = ieee80211_rate_get_vht_mcs(rate);
		nss = ieee80211_rate_get_vht_nss(rate);
		phy = MT_PHY_TYPE_VHT;
		if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			*bw = 1;
		else if (rate->flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
			*bw = 2;
		else if (rate->flags & IEEE80211_TX_RC_160_MHZ_WIDTH)
			*bw = 3;
	} else if (rate->flags & IEEE80211_TX_RC_MCS) {
		rate_idx = rate->idx;
		nss = 1 + (rate->idx >> 3);
		phy = MT_PHY_TYPE_HT;
		if (rate->flags & IEEE80211_TX_RC_GREEN_FIELD)
			phy = MT_PHY_TYPE_HT_GF;
		if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			*bw = 1;
	} else {
		const struct ieee80211_rate *r;
		int band = mphy->chandef.chan->band;
		u16 val;

		nss = 1;
		r = &mphy->hw->wiphy->bands[band]->bitrates[rate->idx];
		if (rate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
			val = r->hw_value_short;
		else
			val = r->hw_value;

		phy = val >> 8;
		rate_idx = val & 0xff;
	}

	if (stbc && nss == 1) {
		nss++;
		rateval |= MT_TX_RATE_STBC;
	}

	rateval |= (FIELD_PREP(MT_TX_RATE_IDX, rate_idx) |
		    FIELD_PREP(MT_TX_RATE_MODE, phy) |
		    FIELD_PREP(MT_TX_RATE_NSS, nss - 1));

	return rateval;
}

int mt7615_mac_write_txwi(struct mt7615_dev *dev, __le32 *txwi,
			  struct sk_buff *skb, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta, int pid,
			  struct ieee80211_key_conf *key)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rate = &info->control.rates[0];
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	bool multicast = is_multicast_ether_addr(hdr->addr1);
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_phy *mphy = &dev->mphy;
	bool ext_phy = info->hw_queue & MT_TX_HW_QUEUE_EXT_PHY;
	int tx_count = 8;
	u8 fc_type, fc_stype, p_fmt, q_idx, omac_idx = 0, wmm_idx = 0;
	__le16 fc = hdr->frame_control;
	u16 seqno = 0;
	u32 val;

	if (vif) {
		struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;

		omac_idx = mvif->omac_idx;
		wmm_idx = mvif->wmm_idx;
	}

	if (sta) {
		struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;

		tx_count = msta->rate_count;
	}

	if (ext_phy && dev->mt76.phy2)
		mphy = dev->mt76.phy2;

	fc_type = (le16_to_cpu(fc) & IEEE80211_FCTL_FTYPE) >> 2;
	fc_stype = (le16_to_cpu(fc) & IEEE80211_FCTL_STYPE) >> 4;

	if (ieee80211_is_data(fc) || ieee80211_is_bufferable_mmpdu(fc)) {
		q_idx = wmm_idx * MT7615_MAX_WMM_SETS +
			skb_get_queue_mapping(skb);
		p_fmt = MT_TX_TYPE_CT;
	} else if (ieee80211_is_beacon(fc)) {
		if (ext_phy)
			q_idx = MT_LMAC_BCN1;
		else
			q_idx = MT_LMAC_BCN0;
		p_fmt = MT_TX_TYPE_FW;
	} else {
		if (ext_phy)
			q_idx = MT_LMAC_ALTX1;
		else
			q_idx = MT_LMAC_ALTX0;
		p_fmt = MT_TX_TYPE_CT;
	}

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len + MT_TXD_SIZE) |
	      FIELD_PREP(MT_TXD0_P_IDX, MT_TX_PORT_IDX_LMAC) |
	      FIELD_PREP(MT_TXD0_Q_IDX, q_idx);
	txwi[0] = cpu_to_le32(val);

	val = MT_TXD1_LONG_FORMAT |
	      FIELD_PREP(MT_TXD1_WLAN_IDX, wcid->idx) |
	      FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_11) |
	      FIELD_PREP(MT_TXD1_HDR_INFO,
			 ieee80211_get_hdrlen_from_skb(skb) / 2) |
	      FIELD_PREP(MT_TXD1_TID,
			 skb->priority & IEEE80211_QOS_CTL_TID_MASK) |
	      FIELD_PREP(MT_TXD1_PKT_FMT, p_fmt) |
	      FIELD_PREP(MT_TXD1_OWN_MAC, omac_idx);
	txwi[1] = cpu_to_le32(val);

	val = FIELD_PREP(MT_TXD2_FRAME_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD2_SUB_TYPE, fc_stype) |
	      FIELD_PREP(MT_TXD2_MULTICAST, multicast);
	if (key) {
		if (multicast && ieee80211_is_robust_mgmt_frame(skb) &&
		    key->cipher == WLAN_CIPHER_SUITE_AES_CMAC) {
			val |= MT_TXD2_BIP;
			txwi[3] = 0;
		} else {
			txwi[3] = cpu_to_le32(MT_TXD3_PROTECT_FRAME);
		}
	} else {
		txwi[3] = 0;
	}
	txwi[2] = cpu_to_le32(val);

	if (!(info->flags & IEEE80211_TX_CTL_AMPDU))
		txwi[2] |= cpu_to_le32(MT_TXD2_BA_DISABLE);

	txwi[4] = 0;
	txwi[6] = 0;

	if (rate->idx >= 0 && rate->count &&
	    !(info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)) {
		bool stbc = info->flags & IEEE80211_TX_CTL_STBC;
		u8 bw;
		u16 rateval = mt7615_mac_tx_rate_val(dev, mphy, rate, stbc,
						     &bw);

		txwi[2] |= cpu_to_le32(MT_TXD2_FIX_RATE);

		val = MT_TXD6_FIXED_BW |
		      FIELD_PREP(MT_TXD6_BW, bw) |
		      FIELD_PREP(MT_TXD6_TX_RATE, rateval);
		txwi[6] |= cpu_to_le32(val);

		if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
			txwi[6] |= cpu_to_le32(MT_TXD6_SGI);

		if (info->flags & IEEE80211_TX_CTL_LDPC)
			txwi[6] |= cpu_to_le32(MT_TXD6_LDPC);

		if (!(rate->flags & (IEEE80211_TX_RC_MCS |
				     IEEE80211_TX_RC_VHT_MCS)))
			txwi[2] |= cpu_to_le32(MT_TXD2_BA_DISABLE);

		tx_count = rate->count;
	}

	if (!ieee80211_is_beacon(fc)) {
		val = MT_TXD5_TX_STATUS_HOST | MT_TXD5_SW_POWER_MGMT |
		      FIELD_PREP(MT_TXD5_PID, pid);
		txwi[5] = cpu_to_le32(val);
	} else {
		txwi[5] = 0;
		/* use maximum tx count for beacons */
		tx_count = 0x1f;
	}

	val = FIELD_PREP(MT_TXD3_REM_TX_COUNT, tx_count);
	if (ieee80211_is_data_qos(hdr->frame_control)) {
		seqno = IEEE80211_SEQ_TO_SN(le16_to_cpu(hdr->seq_ctrl));
		val |= MT_TXD3_SN_VALID;
	} else if (ieee80211_is_back_req(hdr->frame_control)) {
		struct ieee80211_bar *bar = (struct ieee80211_bar *)skb->data;

		seqno = IEEE80211_SEQ_TO_SN(le16_to_cpu(bar->start_seq_num));
		val |= MT_TXD3_SN_VALID;
	}
	val |= FIELD_PREP(MT_TXD3_SEQ, seqno);

	txwi[3] |= cpu_to_le32(val);

	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		txwi[3] |= cpu_to_le32(MT_TXD3_NO_ACK);

	txwi[7] = FIELD_PREP(MT_TXD7_TYPE, fc_type) |
		  FIELD_PREP(MT_TXD7_SUB_TYPE, fc_stype);

	return 0;
}

static void
mt7615_txp_skb_unmap_fw(struct mt76_dev *dev, struct mt7615_fw_txp *txp)
{
	int i;

	for (i = 1; i < txp->nbuf; i++)
		dma_unmap_single(dev->dev, le32_to_cpu(txp->buf[i]),
				 le16_to_cpu(txp->len[i]), DMA_TO_DEVICE);
}

static void
mt7615_txp_skb_unmap_hw(struct mt76_dev *dev, struct mt7615_hw_txp *txp)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(txp->ptr); i++) {
		struct mt7615_txp_ptr *ptr = &txp->ptr[i];
		bool last;
		u16 len;

		len = le16_to_cpu(ptr->len0);
		last = len & MT_TXD_LEN_MSDU_LAST;
		len &= ~MT_TXD_LEN_MSDU_LAST;
		dma_unmap_single(dev->dev, le32_to_cpu(ptr->buf0), len,
				 DMA_TO_DEVICE);
		if (last)
			break;

		len = le16_to_cpu(ptr->len1);
		last = len & MT_TXD_LEN_MSDU_LAST;
		len &= ~MT_TXD_LEN_MSDU_LAST;
		dma_unmap_single(dev->dev, le32_to_cpu(ptr->buf1), len,
				 DMA_TO_DEVICE);
		if (last)
			break;
	}
}

void mt7615_txp_skb_unmap(struct mt76_dev *dev,
			  struct mt76_txwi_cache *t)
{
	struct mt7615_txp_common *txp;

	txp = mt7615_txwi_to_txp(dev, t);
	if (is_mt7615(dev))
		mt7615_txp_skb_unmap_fw(dev, &txp->fw);
	else
		mt7615_txp_skb_unmap_hw(dev, &txp->hw);
}

static u32 mt7615_mac_wtbl_addr(int wcid)
{
	return MT_WTBL_BASE + wcid * MT_WTBL_ENTRY_SIZE;
}

bool mt7615_mac_wtbl_update(struct mt7615_dev *dev, int idx, u32 mask)
{
	mt76_rmw(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_WLAN_IDX,
		 FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, idx) | mask);

	return mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY,
			 0, 5000);
}

void mt7615_mac_sta_poll(struct mt7615_dev *dev)
{
	static const u8 ac_to_tid[4] = {
		[IEEE80211_AC_BE] = 0,
		[IEEE80211_AC_BK] = 1,
		[IEEE80211_AC_VI] = 4,
		[IEEE80211_AC_VO] = 6
	};
	static const u8 hw_queue_map[] = {
		[IEEE80211_AC_BK] = 0,
		[IEEE80211_AC_BE] = 1,
		[IEEE80211_AC_VI] = 2,
		[IEEE80211_AC_VO] = 3,
	};
	struct ieee80211_sta *sta;
	struct mt7615_sta *msta;
	u32 addr, tx_time[4], rx_time[4];
	int i;

	rcu_read_lock();

	while (true) {
		bool clear = false;

		spin_lock_bh(&dev->sta_poll_lock);
		if (list_empty(&dev->sta_poll_list)) {
			spin_unlock_bh(&dev->sta_poll_lock);
			break;
		}
		msta = list_first_entry(&dev->sta_poll_list,
					struct mt7615_sta, poll_list);
		list_del_init(&msta->poll_list);
		spin_unlock_bh(&dev->sta_poll_lock);

		addr = mt7615_mac_wtbl_addr(msta->wcid.idx) + 19 * 4;

		for (i = 0; i < 4; i++, addr += 8) {
			u32 tx_last = msta->airtime_ac[i];
			u32 rx_last = msta->airtime_ac[i + 4];

			msta->airtime_ac[i] = mt76_rr(dev, addr);
			msta->airtime_ac[i + 4] = mt76_rr(dev, addr + 4);
			tx_time[i] = msta->airtime_ac[i] - tx_last;
			rx_time[i] = msta->airtime_ac[i + 4] - rx_last;

			if ((tx_last | rx_last) & BIT(30))
				clear = true;
		}

		if (clear) {
			mt7615_mac_wtbl_update(dev, msta->wcid.idx,
					       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
			memset(msta->airtime_ac, 0, sizeof(msta->airtime_ac));
		}

		if (!msta->wcid.sta)
			continue;

		sta = container_of((void *)msta, struct ieee80211_sta,
				   drv_priv);
		for (i = 0; i < 4; i++) {
			u32 tx_cur = tx_time[i];
			u32 rx_cur = rx_time[hw_queue_map[i]];
			u8 tid = ac_to_tid[i];

			if (!tx_cur && !rx_cur)
				continue;

			ieee80211_sta_register_airtime(sta, tid, tx_cur,
						       rx_cur);
		}
	}

	rcu_read_unlock();
}

void mt7615_mac_set_rates(struct mt7615_phy *phy, struct mt7615_sta *sta,
			  struct ieee80211_tx_rate *probe_rate,
			  struct ieee80211_tx_rate *rates)
{
	struct mt7615_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_tx_rate *ref;
	int wcid = sta->wcid.idx;
	u32 addr = mt7615_mac_wtbl_addr(wcid);
	bool stbc = false;
	int n_rates = sta->n_rates;
	u8 bw, bw_prev, bw_idx = 0;
	u16 val[4];
	u16 probe_val;
	u32 w5, w27;
	bool rateset;
	int i, k;

	if (!mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000))
		return;

	for (i = n_rates; i < 4; i++)
		rates[i] = rates[n_rates - 1];

	rateset = !(sta->rate_set_tsf & BIT(0));
	memcpy(sta->rateset[rateset].rates, rates,
	       sizeof(sta->rateset[rateset].rates));
	if (probe_rate) {
		sta->rateset[rateset].probe_rate = *probe_rate;
		ref = &sta->rateset[rateset].probe_rate;
	} else {
		sta->rateset[rateset].probe_rate.idx = -1;
		ref = &sta->rateset[rateset].rates[0];
	}

	rates = sta->rateset[rateset].rates;
	for (i = 0; i < ARRAY_SIZE(sta->rateset[rateset].rates); i++) {
		/*
		 * We don't support switching between short and long GI
		 * within the rate set. For accurate tx status reporting, we
		 * need to make sure that flags match.
		 * For improved performance, avoid duplicate entries by
		 * decrementing the MCS index if necessary
		 */
		if ((ref->flags ^ rates[i].flags) & IEEE80211_TX_RC_SHORT_GI)
			rates[i].flags ^= IEEE80211_TX_RC_SHORT_GI;

		for (k = 0; k < i; k++) {
			if (rates[i].idx != rates[k].idx)
				continue;
			if ((rates[i].flags ^ rates[k].flags) &
			    (IEEE80211_TX_RC_40_MHZ_WIDTH |
			     IEEE80211_TX_RC_80_MHZ_WIDTH |
			     IEEE80211_TX_RC_160_MHZ_WIDTH))
				continue;

			if (!rates[i].idx)
				continue;

			rates[i].idx--;
		}
	}

	val[0] = mt7615_mac_tx_rate_val(dev, mphy, &rates[0], stbc, &bw);
	bw_prev = bw;

	if (probe_rate) {
		probe_val = mt7615_mac_tx_rate_val(dev, mphy, probe_rate,
						   stbc, &bw);
		if (bw)
			bw_idx = 1;
		else
			bw_prev = 0;
	} else {
		probe_val = val[0];
	}

	val[1] = mt7615_mac_tx_rate_val(dev, mphy, &rates[1], stbc, &bw);
	if (bw_prev) {
		bw_idx = 3;
		bw_prev = bw;
	}

	val[2] = mt7615_mac_tx_rate_val(dev, mphy, &rates[2], stbc, &bw);
	if (bw_prev) {
		bw_idx = 5;
		bw_prev = bw;
	}

	val[3] = mt7615_mac_tx_rate_val(dev, mphy, &rates[3], stbc, &bw);
	if (bw_prev)
		bw_idx = 7;

	w27 = mt76_rr(dev, addr + 27 * 4);
	w27 &= ~MT_WTBL_W27_CC_BW_SEL;
	w27 |= FIELD_PREP(MT_WTBL_W27_CC_BW_SEL, bw);

	w5 = mt76_rr(dev, addr + 5 * 4);
	w5 &= ~(MT_WTBL_W5_BW_CAP | MT_WTBL_W5_CHANGE_BW_RATE |
		MT_WTBL_W5_MPDU_OK_COUNT |
		MT_WTBL_W5_MPDU_FAIL_COUNT |
		MT_WTBL_W5_RATE_IDX);
	w5 |= FIELD_PREP(MT_WTBL_W5_BW_CAP, bw) |
	      FIELD_PREP(MT_WTBL_W5_CHANGE_BW_RATE, bw_idx ? bw_idx - 1 : 7);

	mt76_wr(dev, MT_WTBL_RIUCR0, w5);

	mt76_wr(dev, MT_WTBL_RIUCR1,
		FIELD_PREP(MT_WTBL_RIUCR1_RATE0, probe_val) |
		FIELD_PREP(MT_WTBL_RIUCR1_RATE1, val[0]) |
		FIELD_PREP(MT_WTBL_RIUCR1_RATE2_LO, val[1]));

	mt76_wr(dev, MT_WTBL_RIUCR2,
		FIELD_PREP(MT_WTBL_RIUCR2_RATE2_HI, val[1] >> 8) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE3, val[1]) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE4, val[2]) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE5_LO, val[2]));

	mt76_wr(dev, MT_WTBL_RIUCR3,
		FIELD_PREP(MT_WTBL_RIUCR3_RATE5_HI, val[2] >> 4) |
		FIELD_PREP(MT_WTBL_RIUCR3_RATE6, val[3]) |
		FIELD_PREP(MT_WTBL_RIUCR3_RATE7, val[3]));

	mt76_wr(dev, MT_WTBL_UPDATE,
		FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, wcid) |
		MT_WTBL_UPDATE_RATE_UPDATE |
		MT_WTBL_UPDATE_TX_COUNT_CLEAR);

	mt76_wr(dev, addr + 27 * 4, w27);

	mt76_set(dev, MT_LPON_T0CR, MT_LPON_T0CR_MODE); /* TSF read */
	sta->rate_set_tsf = (mt76_rr(dev, MT_LPON_UTTR0) & ~BIT(0)) | rateset;

	if (!(sta->wcid.tx_info & MT_WCID_TX_INFO_SET))
		mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000);

	sta->rate_count = 2 * MT7615_RATE_RETRY * n_rates;
	sta->wcid.tx_info |= MT_WCID_TX_INFO_SET;
}

static enum mt7615_cipher_type
mt7615_mac_get_cipher(int cipher)
{
	switch (cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		return MT_CIPHER_WEP40;
	case WLAN_CIPHER_SUITE_WEP104:
		return MT_CIPHER_WEP104;
	case WLAN_CIPHER_SUITE_TKIP:
		return MT_CIPHER_TKIP;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		return MT_CIPHER_BIP_CMAC_128;
	case WLAN_CIPHER_SUITE_CCMP:
		return MT_CIPHER_AES_CCMP;
	case WLAN_CIPHER_SUITE_CCMP_256:
		return MT_CIPHER_CCMP_256;
	case WLAN_CIPHER_SUITE_GCMP:
		return MT_CIPHER_GCMP;
	case WLAN_CIPHER_SUITE_GCMP_256:
		return MT_CIPHER_GCMP_256;
	case WLAN_CIPHER_SUITE_SMS4:
		return MT_CIPHER_WAPI;
	default:
		return MT_CIPHER_NONE;
	}
}

static int
mt7615_mac_wtbl_update_key(struct mt7615_dev *dev, struct mt76_wcid *wcid,
			   struct ieee80211_key_conf *key,
			   enum mt7615_cipher_type cipher,
			   enum set_key_cmd cmd)
{
	u32 addr = mt7615_mac_wtbl_addr(wcid->idx) + 30 * 4;
	u8 data[32] = {};

	if (key->keylen > sizeof(data))
		return -EINVAL;

	mt76_rr_copy(dev, addr, data, sizeof(data));
	if (cmd == SET_KEY) {
		if (cipher == MT_CIPHER_TKIP) {
			/* Rx/Tx MIC keys are swapped */
			memcpy(data + 16, key->key + 24, 8);
			memcpy(data + 24, key->key + 16, 8);
		}
		if (cipher != MT_CIPHER_BIP_CMAC_128 && wcid->cipher)
			memmove(data + 16, data, 16);
		if (cipher != MT_CIPHER_BIP_CMAC_128 || !wcid->cipher)
			memcpy(data, key->key, key->keylen);
		else if (cipher == MT_CIPHER_BIP_CMAC_128)
			memcpy(data + 16, key->key, 16);
	} else {
		if (wcid->cipher & ~BIT(cipher)) {
			if (cipher != MT_CIPHER_BIP_CMAC_128)
				memmove(data, data + 16, 16);
			memset(data + 16, 0, 16);
		} else {
			memset(data, 0, sizeof(data));
		}
	}
	mt76_wr_copy(dev, addr, data, sizeof(data));

	return 0;
}

static int
mt7615_mac_wtbl_update_pk(struct mt7615_dev *dev, struct mt76_wcid *wcid,
			  enum mt7615_cipher_type cipher, int keyidx,
			  enum set_key_cmd cmd)
{
	u32 addr = mt7615_mac_wtbl_addr(wcid->idx), w0, w1;

	if (!mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000))
		return -ETIMEDOUT;

	w0 = mt76_rr(dev, addr);
	w1 = mt76_rr(dev, addr + 4);
	if (cmd == SET_KEY) {
		w0 |= MT_WTBL_W0_RX_KEY_VALID |
		      FIELD_PREP(MT_WTBL_W0_RX_IK_VALID,
				 cipher == MT_CIPHER_BIP_CMAC_128);
		if (cipher != MT_CIPHER_BIP_CMAC_128 ||
		    !wcid->cipher)
			w0 |= FIELD_PREP(MT_WTBL_W0_KEY_IDX, keyidx);
	}  else {
		if (!(wcid->cipher & ~BIT(cipher)))
			w0 &= ~(MT_WTBL_W0_RX_KEY_VALID |
				MT_WTBL_W0_KEY_IDX);
		if (cipher == MT_CIPHER_BIP_CMAC_128)
			w0 &= ~MT_WTBL_W0_RX_IK_VALID;
	}
	mt76_wr(dev, MT_WTBL_RICR0, w0);
	mt76_wr(dev, MT_WTBL_RICR1, w1);

	if (!mt7615_mac_wtbl_update(dev, wcid->idx,
				    MT_WTBL_UPDATE_RXINFO_UPDATE))
		return -ETIMEDOUT;

	return 0;
}

static void
mt7615_mac_wtbl_update_cipher(struct mt7615_dev *dev, struct mt76_wcid *wcid,
			      enum mt7615_cipher_type cipher,
			      enum set_key_cmd cmd)
{
	u32 addr = mt7615_mac_wtbl_addr(wcid->idx);

	if (cmd == SET_KEY) {
		if (cipher != MT_CIPHER_BIP_CMAC_128 || !wcid->cipher)
			mt76_rmw(dev, addr + 2 * 4, MT_WTBL_W2_KEY_TYPE,
				 FIELD_PREP(MT_WTBL_W2_KEY_TYPE, cipher));
	} else {
		if (cipher != MT_CIPHER_BIP_CMAC_128 &&
		    wcid->cipher & BIT(MT_CIPHER_BIP_CMAC_128))
			mt76_rmw(dev, addr + 2 * 4, MT_WTBL_W2_KEY_TYPE,
				 FIELD_PREP(MT_WTBL_W2_KEY_TYPE,
					    MT_CIPHER_BIP_CMAC_128));
		else if (!(wcid->cipher & ~BIT(cipher)))
			mt76_clear(dev, addr + 2 * 4, MT_WTBL_W2_KEY_TYPE);
	}
}

int mt7615_mac_wtbl_set_key(struct mt7615_dev *dev,
			    struct mt76_wcid *wcid,
			    struct ieee80211_key_conf *key,
			    enum set_key_cmd cmd)
{
	enum mt7615_cipher_type cipher;
	int err;

	cipher = mt7615_mac_get_cipher(key->cipher);
	if (cipher == MT_CIPHER_NONE)
		return -EOPNOTSUPP;

	spin_lock_bh(&dev->mt76.lock);

	mt7615_mac_wtbl_update_cipher(dev, wcid, cipher, cmd);
	err = mt7615_mac_wtbl_update_key(dev, wcid, key, cipher, cmd);
	if (err < 0)
		goto out;

	err = mt7615_mac_wtbl_update_pk(dev, wcid, cipher, key->keyidx,
					cmd);
	if (err < 0)
		goto out;

	if (cmd == SET_KEY)
		wcid->cipher |= BIT(cipher);
	else
		wcid->cipher &= ~BIT(cipher);

out:
	spin_unlock_bh(&dev->mt76.lock);

	return err;
}

static void
mt7615_write_hw_txp(struct mt7615_dev *dev, struct mt76_tx_info *tx_info,
		    void *txp_ptr, u32 id)
{
	struct mt7615_hw_txp *txp = txp_ptr;
	struct mt7615_txp_ptr *ptr = &txp->ptr[0];
	int nbuf = tx_info->nbuf - 1;
	int i;

	tx_info->buf[0].len = MT_TXD_SIZE + sizeof(*txp);
	tx_info->nbuf = 1;

	txp->msdu_id[0] = cpu_to_le16(id | MT_MSDU_ID_VALID);

	for (i = 0; i < nbuf; i++) {
		u32 addr = tx_info->buf[i + 1].addr;
		u16 len = tx_info->buf[i + 1].len;

		if (i == nbuf - 1)
			len |= MT_TXD_LEN_MSDU_LAST |
			       MT_TXD_LEN_AMSDU_LAST;

		if (i & 1) {
			ptr->buf1 = cpu_to_le32(addr);
			ptr->len1 = cpu_to_le16(len);
			ptr++;
		} else {
			ptr->buf0 = cpu_to_le32(addr);
			ptr->len0 = cpu_to_le16(len);
		}
	}
}

static void
mt7615_write_fw_txp(struct mt7615_dev *dev, struct mt76_tx_info *tx_info,
		    void *txp_ptr, u32 id)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx_info->skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_info->skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct ieee80211_vif *vif = info->control.vif;
	struct mt7615_fw_txp *txp = txp_ptr;
	int nbuf = tx_info->nbuf - 1;
	int i;

	for (i = 0; i < nbuf; i++) {
		txp->buf[i] = cpu_to_le32(tx_info->buf[i + 1].addr);
		txp->len[i] = cpu_to_le16(tx_info->buf[i + 1].len);
	}
	txp->nbuf = nbuf;

	/* pass partial skb header to fw */
	tx_info->buf[0].len = MT_TXD_SIZE + sizeof(*txp);
	tx_info->buf[1].len = MT_CT_PARSE_LEN;
	tx_info->nbuf = MT_CT_DMA_BUF_NUM;

	txp->flags = cpu_to_le16(MT_CT_INFO_APPLY_TXD);

	if (!key)
		txp->flags |= cpu_to_le16(MT_CT_INFO_NONE_CIPHER_FRAME);

	if (ieee80211_is_mgmt(hdr->frame_control))
		txp->flags |= cpu_to_le16(MT_CT_INFO_MGMT_FRAME);

	if (vif) {
		struct mt7615_vif *mvif = (struct mt7615_vif *)vif->drv_priv;

		txp->bss_idx = mvif->idx;
	}

	txp->token = cpu_to_le16(id);
	txp->rept_wds_wcid = 0xff;
}

int mt7615_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	struct mt7615_sta *msta = container_of(wcid, struct mt7615_sta, wcid);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_info->skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	int pid, id;
	u8 *txwi = (u8 *)txwi_ptr;
	struct mt76_txwi_cache *t;
	void *txp;

	if (!wcid)
		wcid = &dev->mt76.global_wcid;

	pid = mt76_tx_status_skb_add(mdev, wcid, tx_info->skb);

	if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE) {
		struct mt7615_phy *phy = &dev->phy;

		if ((info->hw_queue & MT_TX_HW_QUEUE_EXT_PHY) && mdev->phy2)
			phy = mdev->phy2->priv;

		spin_lock_bh(&dev->mt76.lock);
		mt7615_mac_set_rates(phy, msta, &info->control.rates[0],
				     msta->rates);
		msta->rate_probe = true;
		spin_unlock_bh(&dev->mt76.lock);
	}

	t = (struct mt76_txwi_cache *)(txwi + mdev->drv->txwi_size);
	t->skb = tx_info->skb;

	spin_lock_bh(&dev->token_lock);
	id = idr_alloc(&dev->token, t, 0, MT7615_TOKEN_SIZE, GFP_ATOMIC);
	spin_unlock_bh(&dev->token_lock);
	if (id < 0)
		return id;

	mt7615_mac_write_txwi(dev, txwi_ptr, tx_info->skb, wcid, sta,
			      pid, key);

	txp = txwi + MT_TXD_SIZE;
	memset(txp, 0, sizeof(struct mt7615_txp_common));
	if (is_mt7615(&dev->mt76))
		mt7615_write_fw_txp(dev, tx_info, txp, id);
	else
		mt7615_write_hw_txp(dev, tx_info, txp, id);

	tx_info->skb = DMA_DUMMY_DATA;

	return 0;
}

static bool mt7615_fill_txs(struct mt7615_dev *dev, struct mt7615_sta *sta,
			    struct ieee80211_tx_info *info, __le32 *txs_data)
{
	struct ieee80211_supported_band *sband;
	struct mt7615_rate_set *rs;
	struct mt76_phy *mphy;
	int first_idx = 0, last_idx;
	int i, idx, count;
	bool fixed_rate, ack_timeout;
	bool probe, ampdu, cck = false;
	bool rs_idx;
	u32 rate_set_tsf;
	u32 final_rate, final_rate_flags, final_nss, txs;

	fixed_rate = info->status.rates[0].count;
	probe = !!(info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE);

	txs = le32_to_cpu(txs_data[1]);
	ampdu = !fixed_rate && (txs & MT_TXS1_AMPDU);

	txs = le32_to_cpu(txs_data[3]);
	count = FIELD_GET(MT_TXS3_TX_COUNT, txs);
	last_idx = FIELD_GET(MT_TXS3_LAST_TX_RATE, txs);

	txs = le32_to_cpu(txs_data[0]);
	final_rate = FIELD_GET(MT_TXS0_TX_RATE, txs);
	ack_timeout = txs & MT_TXS0_ACK_TIMEOUT;

	if (!ampdu && (txs & MT_TXS0_RTS_TIMEOUT))
		return false;

	if (txs & MT_TXS0_QUEUE_TIMEOUT)
		return false;

	if (!ack_timeout)
		info->flags |= IEEE80211_TX_STAT_ACK;

	info->status.ampdu_len = 1;
	info->status.ampdu_ack_len = !!(info->flags &
					IEEE80211_TX_STAT_ACK);

	if (ampdu || (info->flags & IEEE80211_TX_CTL_AMPDU))
		info->flags |= IEEE80211_TX_STAT_AMPDU | IEEE80211_TX_CTL_AMPDU;

	first_idx = max_t(int, 0, last_idx - (count + 1) / MT7615_RATE_RETRY);

	if (fixed_rate && !probe) {
		info->status.rates[0].count = count;
		i = 0;
		goto out;
	}

	rate_set_tsf = READ_ONCE(sta->rate_set_tsf);
	rs_idx = !((u32)(FIELD_GET(MT_TXS4_F0_TIMESTAMP, le32_to_cpu(txs_data[4])) -
			 rate_set_tsf) < 1000000);
	rs_idx ^= rate_set_tsf & BIT(0);
	rs = &sta->rateset[rs_idx];

	if (!first_idx && rs->probe_rate.idx >= 0) {
		info->status.rates[0] = rs->probe_rate;

		spin_lock_bh(&dev->mt76.lock);
		if (sta->rate_probe) {
			struct mt7615_phy *phy = &dev->phy;

			if (sta->wcid.ext_phy && dev->mt76.phy2)
				phy = dev->mt76.phy2->priv;

			mt7615_mac_set_rates(phy, sta, NULL, sta->rates);
			sta->rate_probe = false;
		}
		spin_unlock_bh(&dev->mt76.lock);
	} else {
		info->status.rates[0] = rs->rates[first_idx / 2];
	}
	info->status.rates[0].count = 0;

	for (i = 0, idx = first_idx; count && idx <= last_idx; idx++) {
		struct ieee80211_tx_rate *cur_rate;
		int cur_count;

		cur_rate = &rs->rates[idx / 2];
		cur_count = min_t(int, MT7615_RATE_RETRY, count);
		count -= cur_count;

		if (idx && (cur_rate->idx != info->status.rates[i].idx ||
			    cur_rate->flags != info->status.rates[i].flags)) {
			i++;
			if (i == ARRAY_SIZE(info->status.rates)) {
				i--;
				break;
			}

			info->status.rates[i] = *cur_rate;
			info->status.rates[i].count = 0;
		}

		info->status.rates[i].count += cur_count;
	}

out:
	final_rate_flags = info->status.rates[i].flags;

	switch (FIELD_GET(MT_TX_RATE_MODE, final_rate)) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		/* fall through */
	case MT_PHY_TYPE_OFDM:
		mphy = &dev->mphy;
		if (sta->wcid.ext_phy && dev->mt76.phy2)
			mphy = dev->mt76.phy2;

		if (mphy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &mphy->sband_5g.sband;
		else
			sband = &mphy->sband_2g.sband;
		final_rate &= MT_TX_RATE_IDX;
		final_rate = mt76_get_rate(&dev->mt76, sband, final_rate,
					   cck);
		final_rate_flags = 0;
		break;
	case MT_PHY_TYPE_HT_GF:
	case MT_PHY_TYPE_HT:
		final_rate_flags |= IEEE80211_TX_RC_MCS;
		final_rate &= MT_TX_RATE_IDX;
		if (final_rate > 31)
			return false;
		break;
	case MT_PHY_TYPE_VHT:
		final_nss = FIELD_GET(MT_TX_RATE_NSS, final_rate);

		if ((final_rate & MT_TX_RATE_STBC) && final_nss)
			final_nss--;

		final_rate_flags |= IEEE80211_TX_RC_VHT_MCS;
		final_rate = (final_rate & MT_TX_RATE_IDX) | (final_nss << 4);
		break;
	default:
		return false;
	}

	info->status.rates[i].idx = final_rate;
	info->status.rates[i].flags = final_rate_flags;

	return true;
}

static bool mt7615_mac_add_txs_skb(struct mt7615_dev *dev,
				   struct mt7615_sta *sta, int pid,
				   __le32 *txs_data)
{
	struct mt76_dev *mdev = &dev->mt76;
	struct sk_buff_head list;
	struct sk_buff *skb;

	if (pid < MT_PACKET_ID_FIRST)
		return false;

	trace_mac_txdone(mdev, sta->wcid.idx, pid);

	mt76_tx_status_lock(mdev, &list);
	skb = mt76_tx_status_skb_get(mdev, &sta->wcid, pid, &list);
	if (skb) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

		if (!mt7615_fill_txs(dev, sta, info, txs_data)) {
			ieee80211_tx_info_clear_status(info);
			info->status.rates[0].idx = -1;
		}

		mt76_tx_status_skb_done(mdev, skb, &list);
	}
	mt76_tx_status_unlock(mdev, &list);

	return !!skb;
}

void mt7615_mac_add_txs(struct mt7615_dev *dev, void *data)
{
	struct ieee80211_tx_info info = {};
	struct ieee80211_sta *sta = NULL;
	struct mt7615_sta *msta = NULL;
	struct mt76_wcid *wcid;
	struct mt76_phy *mphy = &dev->mt76.phy;
	__le32 *txs_data = data;
	u32 txs;
	u8 wcidx;
	u8 pid;

	txs = le32_to_cpu(txs_data[0]);
	pid = FIELD_GET(MT_TXS0_PID, txs);
	txs = le32_to_cpu(txs_data[2]);
	wcidx = FIELD_GET(MT_TXS2_WCID, txs);

	if (pid == MT_PACKET_ID_NO_ACK)
		return;

	if (wcidx >= ARRAY_SIZE(dev->mt76.wcid))
		return;

	rcu_read_lock();

	wcid = rcu_dereference(dev->mt76.wcid[wcidx]);
	if (!wcid)
		goto out;

	msta = container_of(wcid, struct mt7615_sta, wcid);
	sta = wcid_to_sta(wcid);

	spin_lock_bh(&dev->sta_poll_lock);
	if (list_empty(&msta->poll_list))
		list_add_tail(&msta->poll_list, &dev->sta_poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

	if (mt7615_mac_add_txs_skb(dev, msta, pid, txs_data))
		goto out;

	if (wcidx >= MT7615_WTBL_STA || !sta)
		goto out;

	if (wcid->ext_phy && dev->mt76.phy2)
		mphy = dev->mt76.phy2;

	if (mt7615_fill_txs(dev, msta, &info, txs_data))
		ieee80211_tx_status_noskb(mphy->hw, sta, &info);

out:
	rcu_read_unlock();
}

static void
mt7615_mac_tx_free_token(struct mt7615_dev *dev, u16 token)
{
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_txwi_cache *txwi;

	trace_mac_tx_free(dev, token);

	spin_lock_bh(&dev->token_lock);
	txwi = idr_remove(&dev->token, token);
	spin_unlock_bh(&dev->token_lock);

	if (!txwi)
		return;

	mt7615_txp_skb_unmap(mdev, txwi);
	if (txwi->skb) {
		mt76_tx_complete_skb(mdev, txwi->skb);
		txwi->skb = NULL;
	}

	mt76_put_txwi(mdev, txwi);
}

void mt7615_mac_tx_free(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt7615_tx_free *free = (struct mt7615_tx_free *)skb->data;
	u8 i, count;

	count = FIELD_GET(MT_TX_FREE_MSDU_ID_CNT, le16_to_cpu(free->ctrl));
	if (is_mt7615(&dev->mt76)) {
		__le16 *token = &free->token[0];

		for (i = 0; i < count; i++)
			mt7615_mac_tx_free_token(dev, le16_to_cpu(token[i]));
	} else {
		__le32 *token = (__le32 *)&free->token[0];

		for (i = 0; i < count; i++)
			mt7615_mac_tx_free_token(dev, le32_to_cpu(token[i]));
	}

	dev_kfree_skb(skb);
}

static void
mt7615_mac_set_default_sensitivity(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;

	mt76_rmw(dev, MT_WF_PHY_MIN_PRI_PWR(ext_phy),
		 MT_WF_PHY_PD_OFDM_MASK(ext_phy),
		 MT_WF_PHY_PD_OFDM(ext_phy, 0x13c));
	mt76_rmw(dev, MT_WF_PHY_RXTD_CCK_PD(ext_phy),
		 MT_WF_PHY_PD_CCK_MASK(ext_phy),
		 MT_WF_PHY_PD_CCK(ext_phy, 0x92));

	phy->ofdm_sensitivity = -98;
	phy->cck_sensitivity = -110;
	phy->last_cca_adj = jiffies;
}

void mt7615_mac_set_scs(struct mt7615_dev *dev, bool enable)
{
	struct mt7615_phy *ext_phy;

	mutex_lock(&dev->mt76.mutex);

	if (dev->scs_en == enable)
		goto out;

	if (enable) {
		mt76_set(dev, MT_WF_PHY_MIN_PRI_PWR(0),
			 MT_WF_PHY_PD_BLK(0));
		mt76_set(dev, MT_WF_PHY_MIN_PRI_PWR(1),
			 MT_WF_PHY_PD_BLK(1));
		if (is_mt7622(&dev->mt76)) {
			mt76_set(dev, MT_MIB_M0_MISC_CR, 0x7 << 8);
			mt76_set(dev, MT_MIB_M0_MISC_CR, 0x7);
		}
	} else {
		mt76_clear(dev, MT_WF_PHY_MIN_PRI_PWR(0),
			   MT_WF_PHY_PD_BLK(0));
		mt76_clear(dev, MT_WF_PHY_MIN_PRI_PWR(1),
			   MT_WF_PHY_PD_BLK(1));
	}

	mt7615_mac_set_default_sensitivity(&dev->phy);
	ext_phy = mt7615_ext_phy(dev);
	if (ext_phy)
		mt7615_mac_set_default_sensitivity(ext_phy);

	dev->scs_en = enable;

out:
	mutex_unlock(&dev->mt76.mutex);
}

void mt7615_mac_enable_nf(struct mt7615_dev *dev, bool ext_phy)
{
	u32 rxtd;

	if (ext_phy)
		rxtd = MT_WF_PHY_RXTD2(10);
	else
		rxtd = MT_WF_PHY_RXTD(12);

	mt76_set(dev, rxtd, BIT(18) | BIT(29));
	mt76_set(dev, MT_WF_PHY_R0_PHYMUX_5(ext_phy), 0x5 << 12);
}

void mt7615_mac_cca_stats_reset(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	u32 reg = MT_WF_PHY_R0_PHYMUX_5(ext_phy);

	mt76_clear(dev, reg, GENMASK(22, 20));
	mt76_set(dev, reg, BIT(22) | BIT(20));
}

static void
mt7615_mac_adjust_sensitivity(struct mt7615_phy *phy,
			      u32 rts_err_rate, bool ofdm)
{
	struct mt7615_dev *dev = phy->dev;
	int false_cca = ofdm ? phy->false_cca_ofdm : phy->false_cca_cck;
	bool ext_phy = phy != &dev->phy;
	u16 def_th = ofdm ? -98 : -110;
	bool update = false;
	s8 *sensitivity;
	int signal;

	sensitivity = ofdm ? &phy->ofdm_sensitivity : &phy->cck_sensitivity;
	signal = mt76_get_min_avg_rssi(&dev->mt76, ext_phy);
	if (!signal) {
		mt7615_mac_set_default_sensitivity(phy);
		return;
	}

	signal = min(signal, -72);
	if (false_cca > 500) {
		if (rts_err_rate > MT_FRAC(40, 100))
			return;

		/* decrease coverage */
		if (*sensitivity == def_th && signal > -90) {
			*sensitivity = -90;
			update = true;
		} else if (*sensitivity + 2 < signal) {
			*sensitivity += 2;
			update = true;
		}
	} else if ((false_cca > 0 && false_cca < 50) ||
		   rts_err_rate > MT_FRAC(60, 100)) {
		/* increase coverage */
		if (*sensitivity - 2 >= def_th) {
			*sensitivity -= 2;
			update = true;
		}
	}

	if (*sensitivity > signal) {
		*sensitivity = signal;
		update = true;
	}

	if (update) {
		u16 val;

		if (ofdm) {
			val = *sensitivity * 2 + 512;
			mt76_rmw(dev, MT_WF_PHY_MIN_PRI_PWR(ext_phy),
				 MT_WF_PHY_PD_OFDM_MASK(ext_phy),
				 MT_WF_PHY_PD_OFDM(ext_phy, val));
		} else {
			val = *sensitivity + 256;
			if (!ext_phy)
			mt76_rmw(dev, MT_WF_PHY_RXTD_CCK_PD(ext_phy),
				 MT_WF_PHY_PD_CCK_MASK(ext_phy),
				 MT_WF_PHY_PD_CCK(ext_phy, val));
		}
		phy->last_cca_adj = jiffies;
	}
}

static void
mt7615_mac_scs_check(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;
	struct mib_stats *mib = &phy->mib;
	u32 val, rts_err_rate = 0;
	u32 mdrdy_cck, mdrdy_ofdm, pd_cck, pd_ofdm;
	bool ext_phy = phy != &dev->phy;

	if (!dev->scs_en)
		return;

	val = mt76_rr(dev, MT_WF_PHY_R0_PHYCTRL_STS0(ext_phy));
	pd_cck = FIELD_GET(MT_WF_PHYCTRL_STAT_PD_CCK, val);
	pd_ofdm = FIELD_GET(MT_WF_PHYCTRL_STAT_PD_OFDM, val);

	val = mt76_rr(dev, MT_WF_PHY_R0_PHYCTRL_STS5(ext_phy));
	mdrdy_cck = FIELD_GET(MT_WF_PHYCTRL_STAT_MDRDY_CCK, val);
	mdrdy_ofdm = FIELD_GET(MT_WF_PHYCTRL_STAT_MDRDY_OFDM, val);

	phy->false_cca_ofdm = pd_ofdm - mdrdy_ofdm;
	phy->false_cca_cck = pd_cck - mdrdy_cck;
	mt7615_mac_cca_stats_reset(phy);

	if (mib->rts_cnt + mib->rts_retries_cnt)
		rts_err_rate = MT_FRAC(mib->rts_retries_cnt,
				       mib->rts_cnt + mib->rts_retries_cnt);

	/* cck */
	mt7615_mac_adjust_sensitivity(phy, rts_err_rate, false);
	/* ofdm */
	mt7615_mac_adjust_sensitivity(phy, rts_err_rate, true);

	if (time_after(jiffies, phy->last_cca_adj + 10 * HZ))
		mt7615_mac_set_default_sensitivity(phy);
}

static u8
mt7615_phy_get_nf(struct mt7615_dev *dev, int idx)
{
	static const u8 nf_power[] = { 92, 89, 86, 83, 80, 75, 70, 65, 60, 55, 52 };
	u32 reg = idx ? MT_WF_PHY_RXTD2(17) : MT_WF_PHY_RXTD(20);
	u32 val, sum = 0, n = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(nf_power); i++, reg += 4) {
		val = mt76_rr(dev, reg);
		sum += val * nf_power[i];
		n += val;
	}

	if (!n)
		return 0;

	return sum / n;
}

static void
mt7615_phy_update_channel(struct mt76_phy *mphy, int idx)
{
	struct mt7615_dev *dev = container_of(mphy->dev, struct mt7615_dev, mt76);
	struct mt7615_phy *phy = mphy->priv;
	struct mt76_channel_state *state;
	u64 busy_time, tx_time, rx_time, obss_time;
	u32 obss_reg = idx ? MT_WF_RMAC_MIB_TIME6 : MT_WF_RMAC_MIB_TIME5;
	int nf;

	busy_time = mt76_get_field(dev, MT_MIB_SDR9(idx),
				   MT_MIB_SDR9_BUSY_MASK);
	tx_time = mt76_get_field(dev, MT_MIB_SDR36(idx),
				 MT_MIB_SDR36_TXTIME_MASK);
	rx_time = mt76_get_field(dev, MT_MIB_SDR37(idx),
				 MT_MIB_SDR37_RXTIME_MASK);
	obss_time = mt76_get_field(dev, obss_reg, MT_MIB_OBSSTIME_MASK);

	nf = mt7615_phy_get_nf(dev, idx);
	if (!phy->noise)
		phy->noise = nf << 4;
	else if (nf)
		phy->noise += nf - (phy->noise >> 4);

	state = mphy->chan_state;
	state->cc_busy += busy_time;
	state->cc_tx += tx_time;
	state->cc_rx += rx_time + obss_time;
	state->cc_bss_rx += rx_time;
	state->noise = -(phy->noise >> 4);
}

void mt7615_update_channel(struct mt76_dev *mdev)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);

	mt7615_phy_update_channel(&mdev->phy, 0);
	if (mdev->phy2)
		mt7615_phy_update_channel(mdev->phy2, 1);

	/* reset obss airtime */
	mt76_set(dev, MT_WF_RMAC_MIB_TIME0, MT_WF_RMAC_MIB_RXTIME_CLR);
}

static void
mt7615_mac_update_mib_stats(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;
	struct mib_stats *mib = &phy->mib;
	bool ext_phy = phy != &dev->phy;
	int i;

	memset(mib, 0, sizeof(*mib));

	mib->fcs_err_cnt = mt76_get_field(dev, MT_MIB_SDR3(ext_phy),
					  MT_MIB_SDR3_FCS_ERR_MASK);

	for (i = 0; i < 4; i++) {
		u32 data, val, val2;

		val = mt76_get_field(dev, MT_MIB_MB_SDR1(ext_phy, i),
				     MT_MIB_ACK_FAIL_COUNT_MASK);
		if (val > mib->ack_fail_cnt)
			mib->ack_fail_cnt = val;

		val2 = mt76_rr(dev, MT_MIB_MB_SDR0(ext_phy, i));
		data = FIELD_GET(MT_MIB_RTS_RETRIES_COUNT_MASK, val2);
		if (data > mib->rts_retries_cnt) {
			mib->rts_cnt = FIELD_GET(MT_MIB_RTS_COUNT_MASK, val2);
			mib->rts_retries_cnt = data;
		}
	}
}

void mt7615_mac_work(struct work_struct *work)
{
	struct mt7615_dev *dev;
	struct mt7615_phy *ext_phy;
	int i, idx;

	dev = (struct mt7615_dev *)container_of(work, struct mt76_dev,
						mac_work.work);

	mutex_lock(&dev->mt76.mutex);
	mt76_update_survey(&dev->mt76);
	if (++dev->mac_work_count == 5) {
		ext_phy = mt7615_ext_phy(dev);

		mt7615_mac_update_mib_stats(&dev->phy);
		mt7615_mac_scs_check(&dev->phy);
		if (ext_phy) {
			mt7615_mac_update_mib_stats(ext_phy);
			mt7615_mac_scs_check(ext_phy);
		}

		dev->mac_work_count = 0;
	}

	for (i = 0, idx = 0; i < 4; i++) {
		u32 val = mt76_rr(dev, MT_TX_AGG_CNT(i));

		dev->mt76.aggr_stats[idx++] += val & 0xffff;
		dev->mt76.aggr_stats[idx++] += val >> 16;
	}
	mutex_unlock(&dev->mt76.mutex);

	mt76_tx_status_check(&dev->mt76, NULL, false);
	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mt76.mac_work,
				     MT7615_WATCHDOG_TIME);
}

static bool
mt7615_wait_reset_state(struct mt7615_dev *dev, u32 state)
{
	bool ret;

	ret = wait_event_timeout(dev->reset_wait,
				 (READ_ONCE(dev->reset_state) & state),
				 MT7615_RESET_TIMEOUT);
	WARN(!ret, "Timeout waiting for MCU reset state %x\n", state);
	return ret;
}

static void
mt7615_update_vif_beacon(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct ieee80211_hw *hw = priv;

	mt7615_mcu_set_bcn(hw, vif, vif->bss_conf.enable_beacon);
}

static void
mt7615_update_beacons(struct mt7615_dev *dev)
{
	ieee80211_iterate_active_interfaces(dev->mt76.hw,
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7615_update_vif_beacon, dev->mt76.hw);

	if (!dev->mt76.phy2)
		return;

	ieee80211_iterate_active_interfaces(dev->mt76.phy2->hw,
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7615_update_vif_beacon, dev->mt76.phy2->hw);
}

static void
mt7615_dma_reset(struct mt7615_dev *dev)
{
	int i;

	mt76_clear(dev, MT_WPDMA_GLO_CFG,
		   MT_WPDMA_GLO_CFG_RX_DMA_EN | MT_WPDMA_GLO_CFG_TX_DMA_EN |
		   MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE);
	usleep_range(1000, 2000);

	for (i = 0; i < __MT_TXQ_MAX; i++)
		mt76_queue_tx_cleanup(dev, i, true);

	for (i = 0; i < ARRAY_SIZE(dev->mt76.q_rx); i++)
		mt76_queue_rx_reset(dev, i);

	mt76_set(dev, MT_WPDMA_GLO_CFG,
		 MT_WPDMA_GLO_CFG_RX_DMA_EN | MT_WPDMA_GLO_CFG_TX_DMA_EN |
		 MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE);
}

void mt7615_mac_reset_work(struct work_struct *work)
{
	struct mt7615_dev *dev;

	dev = container_of(work, struct mt7615_dev, reset_work);

	if (!(READ_ONCE(dev->reset_state) & MT_MCU_CMD_STOP_PDMA))
		return;

	ieee80211_stop_queues(mt76_hw(dev));
	if (dev->mt76.phy2)
		ieee80211_stop_queues(dev->mt76.phy2->hw);

	set_bit(MT76_RESET, &dev->mphy.state);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);
	wake_up(&dev->mt76.mcu.wait);
	cancel_delayed_work_sync(&dev->mt76.mac_work);

	/* lock/unlock all queues to ensure that no tx is pending */
	mt76_txq_schedule_all(&dev->mphy);
	if (dev->mt76.phy2)
		mt76_txq_schedule_all(dev->mt76.phy2);

	tasklet_disable(&dev->mt76.tx_tasklet);
	napi_disable(&dev->mt76.napi[0]);
	napi_disable(&dev->mt76.napi[1]);
	napi_disable(&dev->mt76.tx_napi);

	mutex_lock(&dev->mt76.mutex);

	mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_PDMA_STOPPED);

	if (mt7615_wait_reset_state(dev, MT_MCU_CMD_RESET_DONE)) {
		mt7615_dma_reset(dev);

		mt76_wr(dev, MT_WPDMA_MEM_RNG_ERR, 0);

		mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_PDMA_INIT);
		mt7615_wait_reset_state(dev, MT_MCU_CMD_RECOVERY_DONE);
	}

	clear_bit(MT76_MCU_RESET, &dev->mphy.state);
	clear_bit(MT76_RESET, &dev->mphy.state);

	tasklet_enable(&dev->mt76.tx_tasklet);
	napi_enable(&dev->mt76.tx_napi);
	napi_schedule(&dev->mt76.tx_napi);

	napi_enable(&dev->mt76.napi[0]);
	napi_schedule(&dev->mt76.napi[0]);

	napi_enable(&dev->mt76.napi[1]);
	napi_schedule(&dev->mt76.napi[1]);

	ieee80211_wake_queues(mt76_hw(dev));
	if (dev->mt76.phy2)
		ieee80211_wake_queues(dev->mt76.phy2->hw);

	mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_RESET_DONE);
	mt7615_wait_reset_state(dev, MT_MCU_CMD_NORMAL_STATE);

	mutex_unlock(&dev->mt76.mutex);

	mt7615_update_beacons(dev);

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mt76.mac_work,
				     MT7615_WATCHDOG_TIME);
}

static void mt7615_dfs_stop_radar_detector(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;

	if (phy->rdd_state & BIT(0))
		mt7615_mcu_rdd_cmd(dev, RDD_STOP, 0, MT_RX_SEL0, 0);
	if (phy->rdd_state & BIT(1))
		mt7615_mcu_rdd_cmd(dev, RDD_STOP, 1, MT_RX_SEL0, 0);
}

static int mt7615_dfs_start_rdd(struct mt7615_dev *dev, int chain)
{
	int err;

	err = mt7615_mcu_rdd_cmd(dev, RDD_START, chain, MT_RX_SEL0, 0);
	if (err < 0)
		return err;

	return mt7615_mcu_rdd_cmd(dev, RDD_DET_MODE, chain,
				  MT_RX_SEL0, 1);
}

static int mt7615_dfs_start_radar_detector(struct mt7615_phy *phy)
{
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	int err;

	/* start CAC */
	err = mt7615_mcu_rdd_cmd(dev, RDD_CAC_START, ext_phy, MT_RX_SEL0, 0);
	if (err < 0)
		return err;

	err = mt7615_dfs_start_rdd(dev, ext_phy);
	if (err < 0)
		return err;

	phy->rdd_state |= BIT(ext_phy);

	if (chandef->width == NL80211_CHAN_WIDTH_160 ||
	    chandef->width == NL80211_CHAN_WIDTH_80P80) {
		err = mt7615_dfs_start_rdd(dev, 1);
		if (err < 0)
			return err;

		phy->rdd_state |= BIT(1);
	}

	return 0;
}

static int
mt7615_dfs_init_radar_specs(struct mt7615_phy *phy)
{
	const struct mt7615_dfs_radar_spec *radar_specs;
	struct mt7615_dev *dev = phy->dev;
	int err, i;

	switch (dev->mt76.region) {
	case NL80211_DFS_FCC:
		radar_specs = &fcc_radar_specs;
		err = mt7615_mcu_set_fcc5_lpn(dev, 8);
		if (err < 0)
			return err;
		break;
	case NL80211_DFS_ETSI:
		radar_specs = &etsi_radar_specs;
		break;
	case NL80211_DFS_JP:
		radar_specs = &jp_radar_specs;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(radar_specs->radar_pattern); i++) {
		err = mt7615_mcu_set_radar_th(dev, i,
					      &radar_specs->radar_pattern[i]);
		if (err < 0)
			return err;
	}

	return mt7615_mcu_set_pulse_th(dev, &radar_specs->pulse_th);
}

int mt7615_dfs_init_radar_detector(struct mt7615_phy *phy)
{
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	int err;

	if (dev->mt76.region == NL80211_DFS_UNSET) {
		phy->dfs_state = -1;
		if (phy->rdd_state)
			goto stop;

		return 0;
	}

	if (test_bit(MT76_SCANNING, &phy->mt76->state))
		return 0;

	if (phy->dfs_state == chandef->chan->dfs_state)
		return 0;

	err = mt7615_dfs_init_radar_specs(phy);
	if (err < 0) {
		phy->dfs_state = -1;
		goto stop;
	}

	phy->dfs_state = chandef->chan->dfs_state;

	if (chandef->chan->flags & IEEE80211_CHAN_RADAR) {
		if (chandef->chan->dfs_state != NL80211_DFS_AVAILABLE)
			return mt7615_dfs_start_radar_detector(phy);

		return mt7615_mcu_rdd_cmd(dev, RDD_CAC_END, ext_phy,
					  MT_RX_SEL0, 0);
	}

stop:
	err = mt7615_mcu_rdd_cmd(dev, RDD_NORMAL_START, ext_phy, MT_RX_SEL0, 0);
	if (err < 0)
		return err;

	mt7615_dfs_stop_radar_detector(phy);
	return 0;
}
