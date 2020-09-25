// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/etherdevice.h>
#include <linux/timekeeping.h>
#include "mt7915.h"
#include "../dma.h"
#include "mac.h"

#define to_rssi(field, rxv)	((FIELD_GET(field, rxv) - 220) / 2)

#define HE_BITS(f)		cpu_to_le16(IEEE80211_RADIOTAP_HE_##f)
#define HE_PREP(f, m, v)	le16_encode_bits(le32_get_bits(v, MT_CRXV_HE_##m),\
						 IEEE80211_RADIOTAP_HE_##f)

static const struct mt7915_dfs_radar_spec etsi_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[5] =  { 1, 0,  6, 32, 28, 0,  990, 5010, 17, 1, 1 },
		[6] =  { 1, 0,  9, 32, 28, 0,  615, 5010, 27, 1, 1 },
		[7] =  { 1, 0, 15, 32, 28, 0,  240,  445, 27, 1, 1 },
		[8] =  { 1, 0, 12, 32, 28, 0,  240,  510, 42, 1, 1 },
		[9] =  { 1, 1,  0,  0,  0, 0, 2490, 3343, 14, 0, 0, 12, 32, 28, { }, 126 },
		[10] = { 1, 1,  0,  0,  0, 0, 2490, 3343, 14, 0, 0, 15, 32, 24, { }, 126 },
		[11] = { 1, 1,  0,  0,  0, 0,  823, 2510, 14, 0, 0, 18, 32, 28, { },  54 },
		[12] = { 1, 1,  0,  0,  0, 0,  823, 2510, 14, 0, 0, 27, 32, 24, { },  54 },
	},
};

static const struct mt7915_dfs_radar_spec fcc_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[0] = { 1, 0,  8,  32, 28, 0, 508, 3076, 13, 1,  1 },
		[1] = { 1, 0, 12,  32, 28, 0, 140,  240, 17, 1,  1 },
		[2] = { 1, 0,  8,  32, 28, 0, 190,  510, 22, 1,  1 },
		[3] = { 1, 0,  6,  32, 28, 0, 190,  510, 32, 1,  1 },
		[4] = { 1, 0,  9, 255, 28, 0, 323,  343, 13, 1, 32 },
	},
};

static const struct mt7915_dfs_radar_spec jp_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[0] =  { 1, 0,  8,  32, 28, 0,  508, 3076,  13, 1,  1 },
		[1] =  { 1, 0, 12,  32, 28, 0,  140,  240,  17, 1,  1 },
		[2] =  { 1, 0,  8,  32, 28, 0,  190,  510,  22, 1,  1 },
		[3] =  { 1, 0,  6,  32, 28, 0,  190,  510,  32, 1,  1 },
		[4] =  { 1, 0,  9, 255, 28, 0,  323,  343,  13, 1, 32 },
		[13] = { 1, 0,  7,  32, 28, 0, 3836, 3856,  14, 1,  1 },
		[14] = { 1, 0,  6,  32, 28, 0,  615, 5010, 110, 1,  1 },
		[15] = { 1, 1,  0,   0,  0, 0,   15, 5010, 110, 0,  0, 12, 32, 28 },
	},
};

static struct mt76_wcid *mt7915_rx_get_wcid(struct mt7915_dev *dev,
					    u16 idx, bool unicast)
{
	struct mt7915_sta *sta;
	struct mt76_wcid *wcid;

	if (idx >= ARRAY_SIZE(dev->mt76.wcid))
		return NULL;

	wcid = rcu_dereference(dev->mt76.wcid[idx]);
	if (unicast || !wcid)
		return wcid;

	if (!wcid->sta)
		return NULL;

	sta = container_of(wcid, struct mt7915_sta, wcid);
	if (!sta->vif)
		return NULL;

	return &sta->vif->sta.wcid;
}

void mt7915_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps)
{
}

bool mt7915_mac_wtbl_update(struct mt7915_dev *dev, int idx, u32 mask)
{
	mt76_rmw(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_WLAN_IDX,
		 FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, idx) | mask);

	return mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY,
			 0, 5000);
}

static u32 mt7915_mac_wtbl_lmac_read(struct mt7915_dev *dev, u16 wcid,
				     u16 addr)
{
	mt76_wr(dev, MT_WTBLON_TOP_WDUCR,
		FIELD_PREP(MT_WTBLON_TOP_WDUCR_GROUP, (wcid >> 7)));

	return mt76_rr(dev, MT_WTBL_LMAC_OFFS(wcid, addr));
}

/* TODO: use txfree airtime info to avoid runtime accessing in the long run */
void mt7915_mac_sta_poll(struct mt7915_dev *dev)
{
	static const u8 ac_to_tid[] = {
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
	struct mt7915_sta *msta;
	u32 tx_time[IEEE80211_NUM_ACS], rx_time[IEEE80211_NUM_ACS];
	int i;

	rcu_read_lock();

	while (true) {
		bool clear = false;
		u16 idx;

		spin_lock_bh(&dev->sta_poll_lock);
		if (list_empty(&dev->sta_poll_list)) {
			spin_unlock_bh(&dev->sta_poll_lock);
			break;
		}
		msta = list_first_entry(&dev->sta_poll_list,
					struct mt7915_sta, poll_list);
		list_del_init(&msta->poll_list);
		spin_unlock_bh(&dev->sta_poll_lock);

		for (i = 0, idx = msta->wcid.idx; i < IEEE80211_NUM_ACS; i++) {
			u32 tx_last = msta->airtime_ac[i];
			u32 rx_last = msta->airtime_ac[i + IEEE80211_NUM_ACS];

			msta->airtime_ac[i] =
				mt7915_mac_wtbl_lmac_read(dev, idx, 20 + i);
			msta->airtime_ac[i + IEEE80211_NUM_ACS] =
				mt7915_mac_wtbl_lmac_read(dev, idx, 21 + i);
			tx_time[i] = msta->airtime_ac[i] - tx_last;
			rx_time[i] = msta->airtime_ac[i + IEEE80211_NUM_ACS] -
				     rx_last;

			if ((tx_last | rx_last) & BIT(30))
				clear = true;
		}

		if (clear) {
			mt7915_mac_wtbl_update(dev, idx,
					       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
			memset(msta->airtime_ac, 0, sizeof(msta->airtime_ac));
		}

		if (!msta->wcid.sta)
			continue;

		sta = container_of((void *)msta, struct ieee80211_sta,
				   drv_priv);
		for (i = 0; i < IEEE80211_NUM_ACS; i++) {
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

static void
mt7915_mac_decode_he_radiotap_ru(struct mt76_rx_status *status,
				 struct ieee80211_radiotap_he *he,
				 __le32 *rxv)
{
	u32 ru_h, ru_l;
	u8 ru, offs = 0;

	ru_l = FIELD_GET(MT_PRXV_HE_RU_ALLOC_L, le32_to_cpu(rxv[0]));
	ru_h = FIELD_GET(MT_PRXV_HE_RU_ALLOC_H, le32_to_cpu(rxv[1]));
	ru = (u8)(ru_l | ru_h << 4);

	status->bw = RATE_INFO_BW_HE_RU;

	switch (ru) {
	case 0 ... 36:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_26;
		offs = ru;
		break;
	case 37 ... 52:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_52;
		offs = ru - 37;
		break;
	case 53 ... 60:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_106;
		offs = ru - 53;
		break;
	case 61 ... 64:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_242;
		offs = ru - 61;
		break;
	case 65 ... 66:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_484;
		offs = ru - 65;
		break;
	case 67:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_996;
		break;
	case 68:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_2x996;
		break;
	}

	he->data1 |= HE_BITS(DATA1_BW_RU_ALLOC_KNOWN);
	he->data2 |= HE_BITS(DATA2_RU_OFFSET_KNOWN) |
		     le16_encode_bits(offs,
				      IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET);
}

static void
mt7915_mac_decode_he_radiotap(struct sk_buff *skb,
			      struct mt76_rx_status *status,
			      __le32 *rxv, u32 phy)
{
	/* TODO: struct ieee80211_radiotap_he_mu */
	static const struct ieee80211_radiotap_he known = {
		.data1 = HE_BITS(DATA1_DATA_MCS_KNOWN) |
			 HE_BITS(DATA1_DATA_DCM_KNOWN) |
			 HE_BITS(DATA1_STBC_KNOWN) |
			 HE_BITS(DATA1_CODING_KNOWN) |
			 HE_BITS(DATA1_LDPC_XSYMSEG_KNOWN) |
			 HE_BITS(DATA1_DOPPLER_KNOWN) |
			 HE_BITS(DATA1_BSS_COLOR_KNOWN),
		.data2 = HE_BITS(DATA2_GI_KNOWN) |
			 HE_BITS(DATA2_TXBF_KNOWN) |
			 HE_BITS(DATA2_PE_DISAMBIG_KNOWN) |
			 HE_BITS(DATA2_TXOP_KNOWN),
	};
	struct ieee80211_radiotap_he *he = NULL;
	u32 ltf_size = le32_get_bits(rxv[2], MT_CRXV_HE_LTF_SIZE) + 1;

	he = skb_push(skb, sizeof(known));
	memcpy(he, &known, sizeof(known));

	he->data3 = HE_PREP(DATA3_BSS_COLOR, BSS_COLOR, rxv[14]) |
		    HE_PREP(DATA3_LDPC_XSYMSEG, LDPC_EXT_SYM, rxv[2]);
	he->data5 = HE_PREP(DATA5_PE_DISAMBIG, PE_DISAMBIG, rxv[2]) |
		    le16_encode_bits(ltf_size,
				     IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE);
	he->data6 = HE_PREP(DATA6_TXOP, TXOP_DUR, rxv[14]) |
		    HE_PREP(DATA6_DOPPLER, DOPPLER, rxv[14]);

	switch (phy) {
	case MT_PHY_TYPE_HE_SU:
		he->data1 |= HE_BITS(DATA1_FORMAT_SU) |
			     HE_BITS(DATA1_UL_DL_KNOWN) |
			     HE_BITS(DATA1_BEAM_CHANGE_KNOWN) |
			     HE_BITS(DATA1_SPTL_REUSE_KNOWN);

		he->data3 |= HE_PREP(DATA3_BEAM_CHANGE, BEAM_CHNG, rxv[14]) |
			     HE_PREP(DATA3_UL_DL, UPLINK, rxv[2]);
		he->data4 |= HE_PREP(DATA4_SU_MU_SPTL_REUSE, SR_MASK, rxv[11]);
		break;
	case MT_PHY_TYPE_HE_EXT_SU:
		he->data1 |= HE_BITS(DATA1_FORMAT_EXT_SU) |
			     HE_BITS(DATA1_UL_DL_KNOWN);

		he->data3 |= HE_PREP(DATA3_UL_DL, UPLINK, rxv[2]);
		break;
	case MT_PHY_TYPE_HE_MU:
		he->data1 |= HE_BITS(DATA1_FORMAT_MU) |
			     HE_BITS(DATA1_UL_DL_KNOWN) |
			     HE_BITS(DATA1_SPTL_REUSE_KNOWN);

		he->data3 |= HE_PREP(DATA3_UL_DL, UPLINK, rxv[2]);
		he->data4 |= HE_PREP(DATA4_SU_MU_SPTL_REUSE, SR_MASK, rxv[11]);

		mt7915_mac_decode_he_radiotap_ru(status, he, rxv);
		break;
	case MT_PHY_TYPE_HE_TB:
		he->data1 |= HE_BITS(DATA1_FORMAT_TRIG) |
			     HE_BITS(DATA1_SPTL_REUSE_KNOWN) |
			     HE_BITS(DATA1_SPTL_REUSE2_KNOWN) |
			     HE_BITS(DATA1_SPTL_REUSE3_KNOWN) |
			     HE_BITS(DATA1_SPTL_REUSE4_KNOWN);

		he->data4 |= HE_PREP(DATA4_TB_SPTL_REUSE1, SR_MASK, rxv[11]) |
			     HE_PREP(DATA4_TB_SPTL_REUSE2, SR1_MASK, rxv[11]) |
			     HE_PREP(DATA4_TB_SPTL_REUSE3, SR2_MASK, rxv[11]) |
			     HE_PREP(DATA4_TB_SPTL_REUSE4, SR3_MASK, rxv[11]);

		mt7915_mac_decode_he_radiotap_ru(status, he, rxv);
		break;
	default:
		break;
	}
}

int mt7915_mac_fill_rx(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7915_phy *phy = &dev->phy;
	struct ieee80211_supported_band *sband;
	struct ieee80211_hdr *hdr;
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *rxv = NULL;
	u32 mode = 0;
	u32 rxd1 = le32_to_cpu(rxd[1]);
	u32 rxd2 = le32_to_cpu(rxd[2]);
	u32 rxd3 = le32_to_cpu(rxd[3]);
	bool unicast, insert_ccmp_hdr = false;
	u8 remove_pad;
	int i, idx;

	memset(status, 0, sizeof(*status));

	if (rxd1 & MT_RXD1_NORMAL_BAND_IDX) {
		mphy = dev->mt76.phy2;
		if (!mphy)
			return -EINVAL;

		phy = mphy->priv;
		status->ext_phy = true;
	}

	if (!test_bit(MT76_STATE_RUNNING, &mphy->state))
		return -EINVAL;

	unicast = FIELD_GET(MT_RXD3_NORMAL_ADDR_TYPE, rxd3) == MT_RXD3_NORMAL_U2M;
	idx = FIELD_GET(MT_RXD1_NORMAL_WLAN_IDX, rxd1);
	status->wcid = mt7915_rx_get_wcid(dev, idx, unicast);

	if (status->wcid) {
		struct mt7915_sta *msta;

		msta = container_of(status->wcid, struct mt7915_sta, wcid);
		spin_lock_bh(&dev->sta_poll_lock);
		if (list_empty(&msta->poll_list))
			list_add_tail(&msta->poll_list, &dev->sta_poll_list);
		spin_unlock_bh(&dev->sta_poll_lock);
	}

	status->freq = mphy->chandef.chan->center_freq;
	status->band = mphy->chandef.chan->band;
	if (status->band == NL80211_BAND_5GHZ)
		sband = &mphy->sband_5g.sband;
	else
		sband = &mphy->sband_2g.sband;

	if (!sband->channels)
		return -EINVAL;

	if (rxd1 & MT_RXD1_NORMAL_FCS_ERR)
		status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (rxd1 & MT_RXD1_NORMAL_TKIP_MIC_ERR)
		status->flag |= RX_FLAG_MMIC_ERROR;

	if (FIELD_GET(MT_RXD1_NORMAL_SEC_MODE, rxd1) != 0 &&
	    !(rxd1 & (MT_RXD1_NORMAL_CLM | MT_RXD1_NORMAL_CM))) {
		status->flag |= RX_FLAG_DECRYPTED;
		status->flag |= RX_FLAG_IV_STRIPPED;
		status->flag |= RX_FLAG_MMIC_STRIPPED | RX_FLAG_MIC_STRIPPED;
	}

	if (!(rxd2 & MT_RXD2_NORMAL_NON_AMPDU)) {
		status->flag |= RX_FLAG_AMPDU_DETAILS;

		/* all subframes of an A-MPDU have the same timestamp */
		if (phy->rx_ampdu_ts != rxd[14]) {
			if (!++phy->ampdu_ref)
				phy->ampdu_ref++;
		}
		phy->rx_ampdu_ts = rxd[14];

		status->ampdu_ref = phy->ampdu_ref;
	}

	remove_pad = FIELD_GET(MT_RXD2_NORMAL_HDR_OFFSET, rxd2);

	if (rxd2 & MT_RXD2_NORMAL_MAX_LEN_ERROR)
		return -EINVAL;

	rxd += 6;
	if (rxd1 & MT_RXD1_NORMAL_GROUP_4) {
		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd1 & MT_RXD1_NORMAL_GROUP_1) {
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

	if (rxd1 & MT_RXD1_NORMAL_GROUP_2) {
		rxd += 2;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	/* RXD Group 3 - P-RXV */
	if (rxd1 & MT_RXD1_NORMAL_GROUP_3) {
		u32 v0, v1, v2;

		rxv = rxd;
		rxd += 2;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;

		v0 = le32_to_cpu(rxv[0]);
		v1 = le32_to_cpu(rxv[1]);
		v2 = le32_to_cpu(rxv[2]);

		if (v0 & MT_PRXV_HT_AD_CODE)
			status->enc_flags |= RX_ENC_FLAG_LDPC;

		status->chains = mphy->antenna_mask;
		status->chain_signal[0] = to_rssi(MT_PRXV_RCPI0, v1);
		status->chain_signal[1] = to_rssi(MT_PRXV_RCPI1, v1);
		status->chain_signal[2] = to_rssi(MT_PRXV_RCPI2, v1);
		status->chain_signal[3] = to_rssi(MT_PRXV_RCPI3, v1);
		status->signal = status->chain_signal[0];

		for (i = 1; i < hweight8(mphy->antenna_mask); i++) {
			if (!(status->chains & BIT(i)))
				continue;

			status->signal = max(status->signal,
					     status->chain_signal[i]);
		}

		/* RXD Group 5 - C-RXV */
		if (rxd1 & MT_RXD1_NORMAL_GROUP_5) {
			u8 stbc = FIELD_GET(MT_CRXV_HT_STBC, v2);
			u8 gi = FIELD_GET(MT_CRXV_HT_SHORT_GI, v2);
			bool cck = false;

			rxd += 18;
			if ((u8 *)rxd - skb->data >= skb->len)
				return -EINVAL;

			idx = i = FIELD_GET(MT_PRXV_TX_RATE, v0);
			mode = FIELD_GET(MT_CRXV_TX_MODE, v2);

			switch (mode) {
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
				status->nss =
					FIELD_GET(MT_PRXV_NSTS, v0) + 1;
				status->encoding = RX_ENC_VHT;
				if (i > 9)
					return -EINVAL;
				break;
			case MT_PHY_TYPE_HE_MU:
				status->flag |= RX_FLAG_RADIOTAP_HE_MU;
				/* fall through */
			case MT_PHY_TYPE_HE_SU:
			case MT_PHY_TYPE_HE_EXT_SU:
			case MT_PHY_TYPE_HE_TB:
				status->nss =
					FIELD_GET(MT_PRXV_NSTS, v0) + 1;
				status->encoding = RX_ENC_HE;
				status->flag |= RX_FLAG_RADIOTAP_HE;
				i &= GENMASK(3, 0);

				if (gi <= NL80211_RATE_INFO_HE_GI_3_2)
					status->he_gi = gi;

				status->he_dcm = !!(idx & MT_PRXV_TX_DCM);
				break;
			default:
				return -EINVAL;
			}
			status->rate_idx = i;

			switch (FIELD_GET(MT_CRXV_FRAME_MODE, v2)) {
			case IEEE80211_STA_RX_BW_20:
				break;
			case IEEE80211_STA_RX_BW_40:
				if (mode & MT_PHY_TYPE_HE_EXT_SU &&
				    (idx & MT_PRXV_TX_ER_SU_106T)) {
					status->bw = RATE_INFO_BW_HE_RU;
					status->he_ru =
						NL80211_RATE_INFO_HE_RU_ALLOC_106;
				} else {
					status->bw = RATE_INFO_BW_40;
				}
				break;
			case IEEE80211_STA_RX_BW_80:
				status->bw = RATE_INFO_BW_80;
				break;
			case IEEE80211_STA_RX_BW_160:
				status->bw = RATE_INFO_BW_160;
				break;
			default:
				return -EINVAL;
			}

			status->enc_flags |= RX_ENC_FLAG_STBC_MASK * stbc;
			if (mode < MT_PHY_TYPE_HE_SU && gi)
				status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		}
	}

	skb_pull(skb, (u8 *)rxd - skb->data + 2 * remove_pad);

	if (insert_ccmp_hdr) {
		u8 key_id = FIELD_GET(MT_RXD1_NORMAL_KEY_ID, rxd1);

		mt76_insert_ccmp_hdr(skb, key_id);
	}

	if (rxv && status->flag & RX_FLAG_RADIOTAP_HE)
		mt7915_mac_decode_he_radiotap(skb, status, rxv, mode);

	hdr = mt76_skb_get_hdr(skb);
	if (!status->wcid || !ieee80211_is_data_qos(hdr->frame_control))
		return 0;

	status->aggr = unicast &&
		       !ieee80211_is_qos_nullfunc(hdr->frame_control);
	status->tid = *ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_TID_MASK;
	status->seqno = IEEE80211_SEQ_TO_SN(le16_to_cpu(hdr->seq_ctrl));

	return 0;
}

void mt7915_mac_write_txwi(struct mt7915_dev *dev, __le32 *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid,
			   struct ieee80211_key_conf *key, bool beacon)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	bool multicast = is_multicast_ether_addr(hdr->addr1);
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_phy *mphy = &dev->mphy;
	bool ext_phy = info->hw_queue & MT_TX_HW_QUEUE_EXT_PHY;
	u8 fc_type, fc_stype, p_fmt, q_idx, omac_idx = 0, wmm_idx = 0;
	__le16 fc = hdr->frame_control;
	u16 tx_count = 4, seqno = 0;
	u32 val;

	if (vif) {
		struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;

		omac_idx = mvif->omac_idx;
		wmm_idx = mvif->wmm_idx;
	}

	if (ext_phy && dev->mt76.phy2)
		mphy = dev->mt76.phy2;

	fc_type = (le16_to_cpu(fc) & IEEE80211_FCTL_FTYPE) >> 2;
	fc_stype = (le16_to_cpu(fc) & IEEE80211_FCTL_STYPE) >> 4;

	if (beacon) {
		p_fmt = MT_TX_TYPE_FW;
		q_idx = MT_LMAC_BCN0;
	} else if (skb_get_queue_mapping(skb) >= MT_TXQ_PSD) {
		p_fmt = MT_TX_TYPE_CT;
		q_idx = MT_LMAC_ALTX0;
	} else {
		p_fmt = MT_TX_TYPE_CT;
		q_idx = wmm_idx * MT7915_MAX_WMM_SETS +
			mt7915_lmac_mapping(dev, skb_get_queue_mapping(skb));
	}

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len + MT_TXD_SIZE) |
	      FIELD_PREP(MT_TXD0_PKT_FMT, p_fmt) |
	      FIELD_PREP(MT_TXD0_Q_IDX, q_idx);
	txwi[0] = cpu_to_le32(val);

	val = MT_TXD1_LONG_FORMAT |
	      FIELD_PREP(MT_TXD1_WLAN_IDX, wcid->idx) |
	      FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_11) |
	      FIELD_PREP(MT_TXD1_HDR_INFO,
			 ieee80211_get_hdrlen_from_skb(skb) / 2) |
	      FIELD_PREP(MT_TXD1_TID,
			 skb->priority & IEEE80211_QOS_CTL_TID_MASK) |
	      FIELD_PREP(MT_TXD1_OWN_MAC, omac_idx);

	if (ext_phy && q_idx >= MT_LMAC_ALTX0 && q_idx <= MT_LMAC_BCN0)
		val |= MT_TXD1_TGID;

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

	txwi[4] = 0;
	txwi[5] = 0;
	txwi[6] = 0;

	if (!ieee80211_is_data(fc) || multicast) {
		u16 rate;

		/* hardware won't add HTC for mgmt/ctrl frame */
		txwi[2] |= cpu_to_le32(MT_TXD2_FIX_RATE | MT_TXD2_HTC_VLD);

		if (mphy->chandef.chan->band == NL80211_BAND_5GHZ)
			rate = MT7915_5G_RATE_DEFAULT;
		else
			rate = MT7915_2G_RATE_DEFAULT;

		val = MT_TXD6_FIXED_BW |
		      FIELD_PREP(MT_TXD6_TX_RATE, rate);
		txwi[6] |= cpu_to_le32(val);
		txwi[3] |= cpu_to_le32(MT_TXD3_BA_DISABLE);
	}

	if (!ieee80211_is_beacon(fc))
		txwi[3] |= cpu_to_le32(MT_TXD3_SW_POWER_MGMT);
	else
		tx_count = 0x1f;

	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		txwi[3] |= cpu_to_le32(MT_TXD3_NO_ACK);

	val = FIELD_PREP(MT_TXD7_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD7_SUB_TYPE, fc_stype);
	txwi[7] = cpu_to_le32(val);

	val = FIELD_PREP(MT_TXD3_REM_TX_COUNT, tx_count);
	if (ieee80211_is_data_qos(fc)) {
		seqno = IEEE80211_SEQ_TO_SN(le16_to_cpu(hdr->seq_ctrl));
		val |= MT_TXD3_SN_VALID;
	} else if (ieee80211_is_back_req(fc)) {
		struct ieee80211_bar *bar;

		bar = (struct ieee80211_bar *)skb->data;
		seqno = IEEE80211_SEQ_TO_SN(le16_to_cpu(bar->start_seq_num));
		val |= MT_TXD3_SN_VALID;
	}
	val |= FIELD_PREP(MT_TXD3_SEQ, seqno);
	txwi[3] |= cpu_to_le32(val);
}

int mt7915_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx_info->skb->data;
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_info->skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_tx_cb *cb = mt76_tx_skb_cb(tx_info->skb);
	struct mt76_txwi_cache *t;
	struct mt7915_txp *txp;
	int id, i, nbuf = tx_info->nbuf - 1;
	u8 *txwi = (u8 *)txwi_ptr;

	if (!wcid)
		wcid = &dev->mt76.global_wcid;

	cb->wcid = wcid->idx;

	mt7915_mac_write_txwi(dev, txwi_ptr, tx_info->skb, wcid, key,
			      false);

	txp = (struct mt7915_txp *)(txwi + MT_TXD_SIZE);
	for (i = 0; i < nbuf; i++) {
		txp->buf[i] = cpu_to_le32(tx_info->buf[i + 1].addr);
		txp->len[i] = cpu_to_le16(tx_info->buf[i + 1].len);
	}
	txp->nbuf = nbuf;

	/* pass partial skb header to fw */
	tx_info->buf[1].len = MT_CT_PARSE_LEN;
	tx_info->nbuf = MT_CT_DMA_BUF_NUM;

	txp->flags = cpu_to_le16(MT_CT_INFO_APPLY_TXD);

	if (!key)
		txp->flags |= cpu_to_le16(MT_CT_INFO_NONE_CIPHER_FRAME);

	if (ieee80211_is_mgmt(hdr->frame_control))
		txp->flags |= cpu_to_le16(MT_CT_INFO_MGMT_FRAME);

	if (vif) {
		struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;

		txp->bss_idx = mvif->idx;
	}

	t = (struct mt76_txwi_cache *)(txwi + mdev->drv->txwi_size);
	t->skb = tx_info->skb;

	spin_lock_bh(&dev->token_lock);
	id = idr_alloc(&dev->token, t, 0, MT7915_TOKEN_SIZE, GFP_ATOMIC);
	spin_unlock_bh(&dev->token_lock);
	if (id < 0)
		return id;

	txp->token = cpu_to_le16(id);
	txp->rept_wds_wcid = 0xff;
	tx_info->skb = DMA_DUMMY_DATA;

	return 0;
}

static inline bool
mt7915_tx_check_aggr_tid(struct mt7915_sta *msta, u8 tid)
{
	bool ret = false;

	spin_lock_bh(&msta->ampdu_lock);
	if (msta->ampdu_state[tid] == MT7915_AGGR_STOP)
		ret = true;
	spin_unlock_bh(&msta->ampdu_lock);

	return ret;
}

static void
mt7915_tx_check_aggr(struct ieee80211_sta *sta, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct mt7915_sta *msta;
	u16 tid;

	if (!sta->ht_cap.ht_supported)
		return;

	if (skb_get_queue_mapping(skb) == IEEE80211_AC_VO)
		return;

	if (unlikely(!ieee80211_is_data_qos(hdr->frame_control)))
		return;

	if (unlikely(skb->protocol == cpu_to_be16(ETH_P_PAE)))
		return;

	msta = (struct mt7915_sta *)sta->drv_priv;
	tid = ieee80211_get_tid(hdr);

	if (mt7915_tx_check_aggr_tid(msta, tid)) {
		ieee80211_start_tx_ba_session(sta, tid, 0);
		mt7915_set_aggr_state(msta, tid, MT7915_AGGR_PROGRESS);
	}
}

static inline void
mt7915_tx_status(struct ieee80211_sta *sta, struct ieee80211_hw *hw,
		 struct ieee80211_tx_info *info, struct sk_buff *skb)
{
	struct ieee80211_tx_status status = {
		.sta = sta,
		.info = info,
	};

	if (skb)
		status.skb = skb;

	if (sta) {
		struct mt7915_sta *msta;

		msta = (struct mt7915_sta *)sta->drv_priv;
		status.rate = &msta->stats.tx_rate;
	}

	/* use status_ext to report HE rate */
	ieee80211_tx_status_ext(hw, &status);
}

static void
mt7915_tx_complete_status(struct mt76_dev *mdev, struct sk_buff *skb,
			  struct ieee80211_sta *sta, u8 stat)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hw *hw;

	hw = mt76_tx_status_get_hw(mdev, skb);

	if (info->flags & IEEE80211_TX_CTL_AMPDU)
		info->flags |= IEEE80211_TX_STAT_AMPDU;
	else if (sta)
		mt7915_tx_check_aggr(sta, skb);

	if (stat)
		ieee80211_tx_info_clear_status(info);

	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
		info->flags |= IEEE80211_TX_STAT_ACK;

	info->status.tx_time = 0;

	if (info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS) {
		mt7915_tx_status(sta, hw, info, skb);
		return;
	}

	if (sta || !(info->flags & IEEE80211_TX_CTL_NO_ACK))
		mt7915_tx_status(sta, hw, info, NULL);

	ieee80211_free_txskb(hw, skb);
}

void mt7915_txp_skb_unmap(struct mt76_dev *dev,
			  struct mt76_txwi_cache *t)
{
	struct mt7915_txp *txp;
	int i;

	txp = mt7915_txwi_to_txp(dev, t);
	for (i = 1; i < txp->nbuf; i++)
		dma_unmap_single(dev->dev, le32_to_cpu(txp->buf[i]),
				 le16_to_cpu(txp->len[i]), DMA_TO_DEVICE);
}

void mt7915_mac_tx_free(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt7915_tx_free *free = (struct mt7915_tx_free *)skb->data;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_txwi_cache *txwi;
	struct ieee80211_sta *sta = NULL;
	u8 i, count;

	/*
	 * TODO: MT_TX_FREE_LATENCY is msdu time from the TXD is queued into PLE,
	 * to the time ack is received or dropped by hw (air + hw queue time).
	 * Should avoid accessing WTBL to get Tx airtime, and use it instead.
	 */
	count = FIELD_GET(MT_TX_FREE_MSDU_CNT, le16_to_cpu(free->ctrl));
	for (i = 0; i < count; i++) {
		u32 msdu, info = le32_to_cpu(free->info[i]);
		u8 stat;

		/*
		 * 1'b1: new wcid pair.
		 * 1'b0: msdu_id with the same 'wcid pair' as above.
		 */
		if (info & MT_TX_FREE_PAIR) {
			struct mt7915_sta *msta;
			struct mt76_wcid *wcid;
			u16 idx;

			count++;
			idx = FIELD_GET(MT_TX_FREE_WLAN_ID, info);
			wcid = rcu_dereference(dev->mt76.wcid[idx]);
			sta = wcid_to_sta(wcid);
			if (!sta)
				continue;

			msta = container_of(wcid, struct mt7915_sta, wcid);
			ieee80211_queue_work(mt76_hw(dev), &msta->stats_work);
			continue;
		}

		msdu = FIELD_GET(MT_TX_FREE_MSDU_ID, info);
		stat = FIELD_GET(MT_TX_FREE_STATUS, info);

		spin_lock_bh(&dev->token_lock);
		txwi = idr_remove(&dev->token, msdu);
		spin_unlock_bh(&dev->token_lock);

		if (!txwi)
			continue;

		mt7915_txp_skb_unmap(mdev, txwi);
		if (txwi->skb) {
			mt7915_tx_complete_status(mdev, txwi->skb, sta, stat);
			txwi->skb = NULL;
		}

		mt76_put_txwi(mdev, txwi);
	}
	dev_kfree_skb(skb);
}

void mt7915_tx_complete_skb(struct mt76_dev *mdev, enum mt76_txq_id qid,
			    struct mt76_queue_entry *e)
{
	struct mt7915_dev *dev;

	if (!e->txwi) {
		dev_kfree_skb_any(e->skb);
		return;
	}

	dev = container_of(mdev, struct mt7915_dev, mt76);

	/* error path */
	if (e->skb == DMA_DUMMY_DATA) {
		struct mt76_txwi_cache *t;
		struct mt7915_txp *txp;

		txp = mt7915_txwi_to_txp(mdev, e->txwi);

		spin_lock_bh(&dev->token_lock);
		t = idr_remove(&dev->token, le16_to_cpu(txp->token));
		spin_unlock_bh(&dev->token_lock);
		e->skb = t ? t->skb : NULL;
	}

	if (e->skb) {
		struct mt76_tx_cb *cb = mt76_tx_skb_cb(e->skb);
		struct mt76_wcid *wcid;

		wcid = rcu_dereference(dev->mt76.wcid[cb->wcid]);

		mt7915_tx_complete_status(mdev, e->skb, wcid_to_sta(wcid), 0);
	}
}

void mt7915_mac_cca_stats_reset(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	u32 reg = MT_WF_PHY_RX_CTRL1(ext_phy);

	mt7915_l2_clear(dev, reg, MT_WF_PHY_RX_CTRL1_STSCNT_EN);
	mt7915_l2_set(dev, reg, BIT(11) | BIT(9));
}

void mt7915_mac_reset_counters(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	int i;

	for (i = 0; i < 4; i++) {
		mt76_rr(dev, MT_TX_AGG_CNT(ext_phy, i));
		mt76_rr(dev, MT_TX_AGG_CNT2(ext_phy, i));
	}

	if (ext_phy) {
		dev->mt76.phy2->survey_time = ktime_get_boottime();
		i = ARRAY_SIZE(dev->mt76.aggr_stats) / 2;
	} else {
		dev->mt76.phy.survey_time = ktime_get_boottime();
		i = 0;
	}
	memset(&dev->mt76.aggr_stats[i], 0, sizeof(dev->mt76.aggr_stats) / 2);

	/* reset airtime counters */
	mt76_rr(dev, MT_MIB_SDR9(ext_phy));
	mt76_rr(dev, MT_MIB_SDR36(ext_phy));
	mt76_rr(dev, MT_MIB_SDR37(ext_phy));

	mt76_set(dev, MT_WF_RMAC_MIB_TIME0(ext_phy),
		 MT_WF_RMAC_MIB_RXTIME_CLR);
	mt76_set(dev, MT_WF_RMAC_MIB_AIRTIME0(ext_phy),
		 MT_WF_RMAC_MIB_RXTIME_CLR);
}

void mt7915_mac_set_timing(struct mt7915_phy *phy)
{
	s16 coverage_class = phy->coverage_class;
	struct mt7915_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	u32 val, reg_offset;
	u32 cck = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 231) |
		  FIELD_PREP(MT_TIMEOUT_VAL_CCA, 48);
	u32 ofdm = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 60) |
		   FIELD_PREP(MT_TIMEOUT_VAL_CCA, 28);
	int sifs, offset;
	bool is_5ghz = phy->mt76->chandef.chan->band == NL80211_BAND_5GHZ;

	if (!test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;

	if (is_5ghz)
		sifs = 16;
	else
		sifs = 10;

	if (ext_phy) {
		coverage_class = max_t(s16, dev->phy.coverage_class,
				       coverage_class);
	} else {
		struct mt7915_phy *phy_ext = mt7915_ext_phy(dev);

		if (phy_ext)
			coverage_class = max_t(s16, phy_ext->coverage_class,
					       coverage_class);
	}
	mt76_set(dev, MT_ARB_SCR(ext_phy),
		 MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
	udelay(1);

	offset = 3 * coverage_class;
	reg_offset = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, offset) |
		     FIELD_PREP(MT_TIMEOUT_VAL_CCA, offset);

	mt76_wr(dev, MT_TMAC_CDTR(ext_phy), cck + reg_offset);
	mt76_wr(dev, MT_TMAC_ODTR(ext_phy), ofdm + reg_offset);
	mt76_wr(dev, MT_TMAC_ICR0(ext_phy),
		FIELD_PREP(MT_IFS_EIFS, 360) |
		FIELD_PREP(MT_IFS_RIFS, 2) |
		FIELD_PREP(MT_IFS_SIFS, sifs) |
		FIELD_PREP(MT_IFS_SLOT, phy->slottime));

	if (phy->slottime < 20 || is_5ghz)
		val = MT7915_CFEND_RATE_DEFAULT;
	else
		val = MT7915_CFEND_RATE_11B;

	mt76_rmw_field(dev, MT_AGG_ACR0(ext_phy), MT_AGG_ACR_CFEND_RATE, val);
	mt76_clear(dev, MT_ARB_SCR(ext_phy),
		   MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
}

/*
 * TODO: mib counters are read-clear and there're many HE functionalities need
 * such info, hence firmware prepares a task to read the fields out to a shared
 * structure. User should switch to use event format to avoid race condition.
 */
static void
mt7915_phy_update_channel(struct mt76_phy *mphy, int idx)
{
	struct mt7915_dev *dev = container_of(mphy->dev, struct mt7915_dev, mt76);
	struct mt76_channel_state *state;
	u64 busy_time, tx_time, rx_time, obss_time;

	busy_time = mt76_get_field(dev, MT_MIB_SDR9(idx),
				   MT_MIB_SDR9_BUSY_MASK);
	tx_time = mt76_get_field(dev, MT_MIB_SDR36(idx),
				 MT_MIB_SDR36_TXTIME_MASK);
	rx_time = mt76_get_field(dev, MT_MIB_SDR37(idx),
				 MT_MIB_SDR37_RXTIME_MASK);
	obss_time = mt76_get_field(dev, MT_WF_RMAC_MIB_AIRTIME14(idx),
				   MT_MIB_OBSSTIME_MASK);

	/* TODO: state->noise */
	state = mphy->chan_state;
	state->cc_busy += busy_time;
	state->cc_tx += tx_time;
	state->cc_rx += rx_time + obss_time;
	state->cc_bss_rx += rx_time;
}

void mt7915_update_channel(struct mt76_dev *mdev)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);

	mt7915_phy_update_channel(&mdev->phy, 0);
	if (mdev->phy2)
		mt7915_phy_update_channel(mdev->phy2, 1);

	/* reset obss airtime */
	mt76_set(dev, MT_WF_RMAC_MIB_TIME0(0), MT_WF_RMAC_MIB_RXTIME_CLR);
	if (mdev->phy2)
		mt76_set(dev, MT_WF_RMAC_MIB_TIME0(1),
			 MT_WF_RMAC_MIB_RXTIME_CLR);
}

static bool
mt7915_wait_reset_state(struct mt7915_dev *dev, u32 state)
{
	bool ret;

	ret = wait_event_timeout(dev->reset_wait,
				 (READ_ONCE(dev->reset_state) & state),
				 MT7915_RESET_TIMEOUT);

	WARN(!ret, "Timeout waiting for MCU reset state %x\n", state);
	return ret;
}

static void
mt7915_update_vif_beacon(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct ieee80211_hw *hw = priv;

	mt7915_mcu_add_beacon(hw, vif, vif->bss_conf.enable_beacon);
}

static void
mt7915_update_beacons(struct mt7915_dev *dev)
{
	ieee80211_iterate_active_interfaces(dev->mt76.hw,
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7915_update_vif_beacon, dev->mt76.hw);

	if (!dev->mt76.phy2)
		return;

	ieee80211_iterate_active_interfaces(dev->mt76.phy2->hw,
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7915_update_vif_beacon, dev->mt76.phy2->hw);
}

static void
mt7915_dma_reset(struct mt7915_dev *dev)
{
	int i;

	mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);
	mt76_clear(dev, MT_WFDMA1_GLO_CFG,
		   MT_WFDMA1_GLO_CFG_TX_DMA_EN | MT_WFDMA1_GLO_CFG_RX_DMA_EN);
	usleep_range(1000, 2000);

	for (i = 0; i < __MT_TXQ_MAX; i++)
		mt76_queue_tx_cleanup(dev, i, true);

	mt76_for_each_q_rx(&dev->mt76, i) {
		mt76_queue_rx_reset(dev, i);
	}

	/* re-init prefetch settings after reset */
	mt7915_dma_prefetch(dev);

	mt76_set(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);
	mt76_set(dev, MT_WFDMA1_GLO_CFG,
		 MT_WFDMA1_GLO_CFG_TX_DMA_EN | MT_WFDMA1_GLO_CFG_RX_DMA_EN);
}

/* system error recovery */
void mt7915_mac_reset_work(struct work_struct *work)
{
	struct mt7915_phy *phy2;
	struct mt76_phy *ext_phy;
	struct mt7915_dev *dev;

	dev = container_of(work, struct mt7915_dev, reset_work);
	ext_phy = dev->mt76.phy2;
	phy2 = ext_phy ? ext_phy->priv : NULL;

	if (!(READ_ONCE(dev->reset_state) & MT_MCU_CMD_STOP_DMA))
		return;

	ieee80211_stop_queues(mt76_hw(dev));
	if (ext_phy)
		ieee80211_stop_queues(ext_phy->hw);

	set_bit(MT76_RESET, &dev->mphy.state);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);
	wake_up(&dev->mt76.mcu.wait);
	cancel_delayed_work_sync(&dev->phy.mac_work);
	if (phy2)
		cancel_delayed_work_sync(&phy2->mac_work);

	/* lock/unlock all queues to ensure that no tx is pending */
	mt76_txq_schedule_all(&dev->mphy);
	if (ext_phy)
		mt76_txq_schedule_all(ext_phy);

	tasklet_disable(&dev->mt76.tx_tasklet);
	napi_disable(&dev->mt76.napi[0]);
	napi_disable(&dev->mt76.napi[1]);
	napi_disable(&dev->mt76.napi[2]);
	napi_disable(&dev->mt76.tx_napi);

	mutex_lock(&dev->mt76.mutex);

	mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_DMA_STOPPED);

	if (mt7915_wait_reset_state(dev, MT_MCU_CMD_RESET_DONE)) {
		mt7915_dma_reset(dev);

		mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_DMA_INIT);
		mt7915_wait_reset_state(dev, MT_MCU_CMD_RECOVERY_DONE);
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

	napi_enable(&dev->mt76.napi[2]);
	napi_schedule(&dev->mt76.napi[2]);

	ieee80211_wake_queues(mt76_hw(dev));
	if (ext_phy)
		ieee80211_wake_queues(ext_phy->hw);

	mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_RESET_DONE);
	mt7915_wait_reset_state(dev, MT_MCU_CMD_NORMAL_STATE);

	mutex_unlock(&dev->mt76.mutex);

	mt7915_update_beacons(dev);

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->phy.mac_work,
				     MT7915_WATCHDOG_TIME);
	if (phy2)
		ieee80211_queue_delayed_work(ext_phy->hw, &phy2->mac_work,
					     MT7915_WATCHDOG_TIME);
}

static void
mt7915_mac_update_mib_stats(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	struct mib_stats *mib = &phy->mib;
	bool ext_phy = phy != &dev->phy;
	int i, aggr0, aggr1;

	memset(mib, 0, sizeof(*mib));

	mib->fcs_err_cnt = mt76_get_field(dev, MT_MIB_SDR3(ext_phy),
					  MT_MIB_SDR3_FCS_ERR_MASK);

	aggr0 = ext_phy ? ARRAY_SIZE(dev->mt76.aggr_stats) / 2 : 0;
	for (i = 0, aggr1 = aggr0 + 4; i < 4; i++) {
		u32 val, val2;

		val = mt76_rr(dev, MT_MIB_MB_SDR1(ext_phy, i));

		val2 = FIELD_GET(MT_MIB_ACK_FAIL_COUNT_MASK, val);
		if (val2 > mib->ack_fail_cnt)
			mib->ack_fail_cnt = val2;

		val2 = FIELD_GET(MT_MIB_BA_MISS_COUNT_MASK, val);
		if (val2 > mib->ba_miss_cnt)
			mib->ba_miss_cnt = val2;

		val = mt76_rr(dev, MT_MIB_MB_SDR0(ext_phy, i));
		val2 = FIELD_GET(MT_MIB_RTS_RETRIES_COUNT_MASK, val);
		if (val2 > mib->rts_retries_cnt) {
			mib->rts_cnt = FIELD_GET(MT_MIB_RTS_COUNT_MASK, val);
			mib->rts_retries_cnt = val2;
		}

		val = mt76_rr(dev, MT_TX_AGG_CNT(ext_phy, i));
		val2 = mt76_rr(dev, MT_TX_AGG_CNT2(ext_phy, i));

		dev->mt76.aggr_stats[aggr0++] += val & 0xffff;
		dev->mt76.aggr_stats[aggr0++] += val >> 16;
		dev->mt76.aggr_stats[aggr1++] += val2 & 0xffff;
		dev->mt76.aggr_stats[aggr1++] += val2 >> 16;
	}
}

void mt7915_mac_sta_stats_work(struct work_struct *work)
{
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif;
	struct mt7915_sta_stats *stats;
	struct mt7915_sta *msta;
	struct mt7915_dev *dev;

	msta = container_of(work, struct mt7915_sta, stats_work);
	sta = container_of((void *)msta, struct ieee80211_sta, drv_priv);
	vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);
	dev = msta->vif->dev;
	stats = &msta->stats;

	/* use MT_TX_FREE_RATE to report Tx rate for further devices */
	if (time_after(jiffies, stats->jiffies + HZ)) {
		mt7915_mcu_get_rate_info(dev, RATE_CTRL_RU_INFO,
					 msta->wcid.idx);

		stats->jiffies = jiffies;
	}

	if (test_and_clear_bit(IEEE80211_RC_SUPP_RATES_CHANGED |
			       IEEE80211_RC_NSS_CHANGED |
			       IEEE80211_RC_BW_CHANGED, &stats->changed))
		mt7915_mcu_add_rate_ctrl(dev, vif, sta);

	if (test_and_clear_bit(IEEE80211_RC_SMPS_CHANGED, &stats->changed))
		mt7915_mcu_add_smps(dev, vif, sta);

	spin_lock_bh(&dev->sta_poll_lock);
	if (list_empty(&msta->poll_list))
		list_add_tail(&msta->poll_list, &dev->sta_poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);
}

void mt7915_mac_work(struct work_struct *work)
{
	struct mt7915_phy *phy;
	struct mt76_dev *mdev;

	phy = (struct mt7915_phy *)container_of(work, struct mt7915_phy,
						mac_work.work);
	mdev = &phy->dev->mt76;

	mutex_lock(&mdev->mutex);

	mt76_update_survey(mdev);
	if (++phy->mac_work_count == 5) {
		phy->mac_work_count = 0;

		mt7915_mac_update_mib_stats(phy);
	}

	mutex_unlock(&mdev->mutex);

	ieee80211_queue_delayed_work(phy->mt76->hw, &phy->mac_work,
				     MT7915_WATCHDOG_TIME);
}

static void mt7915_dfs_stop_radar_detector(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;

	if (phy->rdd_state & BIT(0))
		mt7915_mcu_rdd_cmd(dev, RDD_STOP, 0, MT_RX_SEL0, 0);
	if (phy->rdd_state & BIT(1))
		mt7915_mcu_rdd_cmd(dev, RDD_STOP, 1, MT_RX_SEL0, 0);
}

static int mt7915_dfs_start_rdd(struct mt7915_dev *dev, int chain)
{
	int err;

	err = mt7915_mcu_rdd_cmd(dev, RDD_START, chain, MT_RX_SEL0, 0);
	if (err < 0)
		return err;

	return mt7915_mcu_rdd_cmd(dev, RDD_DET_MODE, chain, MT_RX_SEL0, 1);
}

static int mt7915_dfs_start_radar_detector(struct mt7915_phy *phy)
{
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	struct mt7915_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	int err;

	/* start CAC */
	err = mt7915_mcu_rdd_cmd(dev, RDD_CAC_START, ext_phy, MT_RX_SEL0, 0);
	if (err < 0)
		return err;

	err = mt7915_dfs_start_rdd(dev, ext_phy);
	if (err < 0)
		return err;

	phy->rdd_state |= BIT(ext_phy);

	if (chandef->width == NL80211_CHAN_WIDTH_160 ||
	    chandef->width == NL80211_CHAN_WIDTH_80P80) {
		err = mt7915_dfs_start_rdd(dev, 1);
		if (err < 0)
			return err;

		phy->rdd_state |= BIT(1);
	}

	return 0;
}

static int
mt7915_dfs_init_radar_specs(struct mt7915_phy *phy)
{
	const struct mt7915_dfs_radar_spec *radar_specs;
	struct mt7915_dev *dev = phy->dev;
	int err, i;

	switch (dev->mt76.region) {
	case NL80211_DFS_FCC:
		radar_specs = &fcc_radar_specs;
		err = mt7915_mcu_set_fcc5_lpn(dev, 8);
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
		err = mt7915_mcu_set_radar_th(dev, i,
					      &radar_specs->radar_pattern[i]);
		if (err < 0)
			return err;
	}

	return mt7915_mcu_set_pulse_th(dev, &radar_specs->pulse_th);
}

int mt7915_dfs_init_radar_detector(struct mt7915_phy *phy)
{
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	struct mt7915_dev *dev = phy->dev;
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

	err = mt7915_dfs_init_radar_specs(phy);
	if (err < 0) {
		phy->dfs_state = -1;
		goto stop;
	}

	phy->dfs_state = chandef->chan->dfs_state;

	if (chandef->chan->flags & IEEE80211_CHAN_RADAR) {
		if (chandef->chan->dfs_state != NL80211_DFS_AVAILABLE)
			return mt7915_dfs_start_radar_detector(phy);

		return mt7915_mcu_rdd_cmd(dev, RDD_CAC_END, ext_phy,
					  MT_RX_SEL0, 0);
	}

stop:
	err = mt7915_mcu_rdd_cmd(dev, RDD_NORMAL_START, ext_phy,
				 MT_RX_SEL0, 0);
	if (err < 0)
		return err;

	mt7915_dfs_stop_radar_detector(phy);
	return 0;
}
