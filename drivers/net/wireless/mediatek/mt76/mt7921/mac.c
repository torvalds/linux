// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/devcoredump.h>
#include <linux/etherdevice.h>
#include <linux/timekeeping.h>
#include "mt7921.h"
#include "../dma.h"
#include "mac.h"
#include "mcu.h"

#define to_rssi(field, rxv)	((FIELD_GET(field, rxv) - 220) / 2)

#define HE_BITS(f)		cpu_to_le16(IEEE80211_RADIOTAP_HE_##f)
#define HE_PREP(f, m, v)	le16_encode_bits(le32_get_bits(v, MT_CRXV_HE_##m),\
						 IEEE80211_RADIOTAP_HE_##f)

static struct mt76_wcid *mt7921_rx_get_wcid(struct mt7921_dev *dev,
					    u16 idx, bool unicast)
{
	struct mt7921_sta *sta;
	struct mt76_wcid *wcid;

	if (idx >= ARRAY_SIZE(dev->mt76.wcid))
		return NULL;

	wcid = rcu_dereference(dev->mt76.wcid[idx]);
	if (unicast || !wcid)
		return wcid;

	if (!wcid->sta)
		return NULL;

	sta = container_of(wcid, struct mt7921_sta, wcid);
	if (!sta->vif)
		return NULL;

	return &sta->vif->sta.wcid;
}

void mt7921_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps)
{
}

bool mt7921_mac_wtbl_update(struct mt7921_dev *dev, int idx, u32 mask)
{
	mt76_rmw(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_WLAN_IDX,
		 FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, idx) | mask);

	return mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY,
			 0, 5000);
}

static u32 mt7921_mac_wtbl_lmac_addr(struct mt7921_dev *dev, u16 wcid)
{
	mt76_wr(dev, MT_WTBLON_TOP_WDUCR,
		FIELD_PREP(MT_WTBLON_TOP_WDUCR_GROUP, (wcid >> 7)));

	return MT_WTBL_LMAC_OFFS(wcid, 0);
}

static void mt7921_mac_sta_poll(struct mt7921_dev *dev)
{
	static const u8 ac_to_tid[] = {
		[IEEE80211_AC_BE] = 0,
		[IEEE80211_AC_BK] = 1,
		[IEEE80211_AC_VI] = 4,
		[IEEE80211_AC_VO] = 6
	};
	struct ieee80211_sta *sta;
	struct mt7921_sta *msta;
	u32 tx_time[IEEE80211_NUM_ACS], rx_time[IEEE80211_NUM_ACS];
	LIST_HEAD(sta_poll_list);
	int i;

	spin_lock_bh(&dev->sta_poll_lock);
	list_splice_init(&dev->sta_poll_list, &sta_poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

	rcu_read_lock();

	while (true) {
		bool clear = false;
		u32 addr;
		u16 idx;

		spin_lock_bh(&dev->sta_poll_lock);
		if (list_empty(&sta_poll_list)) {
			spin_unlock_bh(&dev->sta_poll_lock);
			break;
		}
		msta = list_first_entry(&sta_poll_list,
					struct mt7921_sta, poll_list);
		list_del_init(&msta->poll_list);
		spin_unlock_bh(&dev->sta_poll_lock);

		idx = msta->wcid.idx;
		addr = mt7921_mac_wtbl_lmac_addr(dev, idx) + 20 * 4;

		for (i = 0; i < IEEE80211_NUM_ACS; i++) {
			u32 tx_last = msta->airtime_ac[i];
			u32 rx_last = msta->airtime_ac[i + 4];

			msta->airtime_ac[i] = mt76_rr(dev, addr);
			msta->airtime_ac[i + 4] = mt76_rr(dev, addr + 4);

			tx_time[i] = msta->airtime_ac[i] - tx_last;
			rx_time[i] = msta->airtime_ac[i + 4] - rx_last;

			if ((tx_last | rx_last) & BIT(30))
				clear = true;

			addr += 8;
		}

		if (clear) {
			mt7921_mac_wtbl_update(dev, idx,
					       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
			memset(msta->airtime_ac, 0, sizeof(msta->airtime_ac));
		}

		if (!msta->wcid.sta)
			continue;

		sta = container_of((void *)msta, struct ieee80211_sta,
				   drv_priv);
		for (i = 0; i < IEEE80211_NUM_ACS; i++) {
			u8 q = mt7921_lmac_mapping(dev, i);
			u32 tx_cur = tx_time[q];
			u32 rx_cur = rx_time[q];
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
mt7921_mac_decode_he_radiotap_ru(struct mt76_rx_status *status,
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
mt7921_mac_decode_he_radiotap(struct sk_buff *skb,
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

		mt7921_mac_decode_he_radiotap_ru(status, he, rxv);
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

		mt7921_mac_decode_he_radiotap_ru(status, he, rxv);
		break;
	default:
		break;
	}
}

static void
mt7921_get_status_freq_info(struct mt7921_dev *dev, struct mt76_phy *mphy,
			    struct mt76_rx_status *status, u8 chfreq)
{
	if (!test_bit(MT76_HW_SCANNING, &mphy->state) &&
	    !test_bit(MT76_HW_SCHED_SCANNING, &mphy->state) &&
	    !test_bit(MT76_STATE_ROC, &mphy->state)) {
		status->freq = mphy->chandef.chan->center_freq;
		status->band = mphy->chandef.chan->band;
		return;
	}

	status->band = chfreq <= 14 ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ;
	status->freq = ieee80211_channel_to_frequency(chfreq, status->band);
}

int mt7921_mac_fill_rx(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7921_phy *phy = &dev->phy;
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
	u8 chfreq;

	memset(status, 0, sizeof(*status));

	if (rxd1 & MT_RXD1_NORMAL_BAND_IDX)
		return -EINVAL;

	if (!test_bit(MT76_STATE_RUNNING, &mphy->state))
		return -EINVAL;

	chfreq = FIELD_GET(MT_RXD3_NORMAL_CH_FREQ, rxd3);
	unicast = FIELD_GET(MT_RXD3_NORMAL_ADDR_TYPE, rxd3) == MT_RXD3_NORMAL_U2M;
	idx = FIELD_GET(MT_RXD1_NORMAL_WLAN_IDX, rxd1);
	status->wcid = mt7921_rx_get_wcid(dev, idx, unicast);

	if (status->wcid) {
		struct mt7921_sta *msta;

		msta = container_of(status->wcid, struct mt7921_sta, wcid);
		spin_lock_bh(&dev->sta_poll_lock);
		if (list_empty(&msta->poll_list))
			list_add_tail(&msta->poll_list, &dev->sta_poll_list);
		spin_unlock_bh(&dev->sta_poll_lock);
	}

	mt7921_get_status_freq_info(dev, mphy, status, chfreq);

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
				fallthrough;
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
				fallthrough;
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
		mt7921_mac_decode_he_radiotap(skb, status, rxv, mode);

	hdr = mt76_skb_get_hdr(skb);
	if (!status->wcid || !ieee80211_is_data_qos(hdr->frame_control))
		return 0;

	status->aggr = unicast &&
		       !ieee80211_is_qos_nullfunc(hdr->frame_control);
	status->tid = *ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_TID_MASK;
	status->seqno = IEEE80211_SEQ_TO_SN(le16_to_cpu(hdr->seq_ctrl));

	return 0;
}

static void
mt7921_mac_write_txwi_8023(struct mt7921_dev *dev, __le32 *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid)
{
	u8 tid = skb->priority & IEEE80211_QOS_CTL_TID_MASK;
	u8 fc_type, fc_stype;
	bool wmm = false;
	u32 val;

	if (wcid->sta) {
		struct ieee80211_sta *sta;

		sta = container_of((void *)wcid, struct ieee80211_sta, drv_priv);
		wmm = sta->wme;
	}

	val = FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_3) |
	      FIELD_PREP(MT_TXD1_TID, tid);

	if (be16_to_cpu(skb->protocol) >= ETH_P_802_3_MIN)
		val |= MT_TXD1_ETH_802_3;

	txwi[1] |= cpu_to_le32(val);

	fc_type = IEEE80211_FTYPE_DATA >> 2;
	fc_stype = wmm ? IEEE80211_STYPE_QOS_DATA >> 4 : 0;

	val = FIELD_PREP(MT_TXD2_FRAME_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD2_SUB_TYPE, fc_stype);

	txwi[2] |= cpu_to_le32(val);

	val = FIELD_PREP(MT_TXD7_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD7_SUB_TYPE, fc_stype);
	txwi[7] |= cpu_to_le32(val);
}

static void
mt7921_mac_write_txwi_80211(struct mt7921_dev *dev, __le32 *txwi,
			    struct sk_buff *skb, struct ieee80211_key_conf *key)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	bool multicast = is_multicast_ether_addr(hdr->addr1);
	u8 tid = skb->priority & IEEE80211_QOS_CTL_TID_MASK;
	__le16 fc = hdr->frame_control;
	u8 fc_type, fc_stype;
	u32 val;

	if (ieee80211_is_action(fc) &&
	    mgmt->u.action.category == WLAN_CATEGORY_BACK &&
	    mgmt->u.action.u.addba_req.action_code == WLAN_ACTION_ADDBA_REQ) {
		u16 capab = le16_to_cpu(mgmt->u.action.u.addba_req.capab);

		txwi[5] |= cpu_to_le32(MT_TXD5_ADD_BA);
		tid = (capab >> 2) & IEEE80211_QOS_CTL_TID_MASK;
	} else if (ieee80211_is_back_req(hdr->frame_control)) {
		struct ieee80211_bar *bar = (struct ieee80211_bar *)hdr;
		u16 control = le16_to_cpu(bar->control);

		tid = FIELD_GET(IEEE80211_BAR_CTRL_TID_INFO_MASK, control);
	}

	val = FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_11) |
	      FIELD_PREP(MT_TXD1_HDR_INFO,
			 ieee80211_get_hdrlen_from_skb(skb) / 2) |
	      FIELD_PREP(MT_TXD1_TID, tid);
	txwi[1] |= cpu_to_le32(val);

	fc_type = (le16_to_cpu(fc) & IEEE80211_FCTL_FTYPE) >> 2;
	fc_stype = (le16_to_cpu(fc) & IEEE80211_FCTL_STYPE) >> 4;

	val = FIELD_PREP(MT_TXD2_FRAME_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD2_SUB_TYPE, fc_stype) |
	      FIELD_PREP(MT_TXD2_MULTICAST, multicast);

	if (key && multicast && ieee80211_is_robust_mgmt_frame(skb) &&
	    key->cipher == WLAN_CIPHER_SUITE_AES_CMAC) {
		val |= MT_TXD2_BIP;
		txwi[3] &= ~cpu_to_le32(MT_TXD3_PROTECT_FRAME);
	}

	if (!ieee80211_is_data(fc) || multicast)
		val |= MT_TXD2_FIX_RATE;

	txwi[2] |= cpu_to_le32(val);

	if (ieee80211_is_beacon(fc)) {
		txwi[3] &= ~cpu_to_le32(MT_TXD3_SW_POWER_MGMT);
		txwi[3] |= cpu_to_le32(MT_TXD3_REM_TX_COUNT);
	}

	if (info->flags & IEEE80211_TX_CTL_INJECTED) {
		u16 seqno = le16_to_cpu(hdr->seq_ctrl);

		if (ieee80211_is_back_req(hdr->frame_control)) {
			struct ieee80211_bar *bar;

			bar = (struct ieee80211_bar *)skb->data;
			seqno = le16_to_cpu(bar->start_seq_num);
		}

		val = MT_TXD3_SN_VALID |
		      FIELD_PREP(MT_TXD3_SEQ, IEEE80211_SEQ_TO_SN(seqno));
		txwi[3] |= cpu_to_le32(val);
	}

	val = FIELD_PREP(MT_TXD7_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD7_SUB_TYPE, fc_stype);
	txwi[7] |= cpu_to_le32(val);
}

void mt7921_mac_write_txwi(struct mt7921_dev *dev, __le32 *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid,
			   struct ieee80211_key_conf *key, bool beacon)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_phy *mphy = &dev->mphy;
	u8 p_fmt, q_idx, omac_idx = 0, wmm_idx = 0;
	bool is_8023 = info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP;
	u16 tx_count = 15;
	u32 val;

	if (vif) {
		struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;

		omac_idx = mvif->omac_idx;
		wmm_idx = mvif->wmm_idx;
	}

	if (beacon) {
		p_fmt = MT_TX_TYPE_FW;
		q_idx = MT_LMAC_BCN0;
	} else if (skb_get_queue_mapping(skb) >= MT_TXQ_PSD) {
		p_fmt = MT_TX_TYPE_CT;
		q_idx = MT_LMAC_ALTX0;
	} else {
		p_fmt = MT_TX_TYPE_CT;
		q_idx = wmm_idx * MT7921_MAX_WMM_SETS +
			mt7921_lmac_mapping(dev, skb_get_queue_mapping(skb));
	}

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len + MT_TXD_SIZE) |
	      FIELD_PREP(MT_TXD0_PKT_FMT, p_fmt) |
	      FIELD_PREP(MT_TXD0_Q_IDX, q_idx);
	txwi[0] = cpu_to_le32(val);

	val = MT_TXD1_LONG_FORMAT |
	      FIELD_PREP(MT_TXD1_WLAN_IDX, wcid->idx) |
	      FIELD_PREP(MT_TXD1_OWN_MAC, omac_idx);

	txwi[1] = cpu_to_le32(val);
	txwi[2] = 0;

	val = FIELD_PREP(MT_TXD3_REM_TX_COUNT, tx_count);
	if (key)
		val |= MT_TXD3_PROTECT_FRAME;
	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		val |= MT_TXD3_NO_ACK;

	txwi[3] = cpu_to_le32(val);
	txwi[4] = 0;
	txwi[5] = 0;
	txwi[6] = 0;
	txwi[7] = wcid->amsdu ? cpu_to_le32(MT_TXD7_HW_AMSDU) : 0;

	if (is_8023)
		mt7921_mac_write_txwi_8023(dev, txwi, skb, wcid);
	else
		mt7921_mac_write_txwi_80211(dev, txwi, skb, key);

	if (txwi[2] & cpu_to_le32(MT_TXD2_FIX_RATE)) {
		u16 rate;

		/* hardware won't add HTC for mgmt/ctrl frame */
		txwi[2] |= cpu_to_le32(MT_TXD2_HTC_VLD);

		if (mphy->chandef.chan->band == NL80211_BAND_5GHZ)
			rate = MT7921_5G_RATE_DEFAULT;
		else
			rate = MT7921_2G_RATE_DEFAULT;

		val = MT_TXD6_FIXED_BW |
		      FIELD_PREP(MT_TXD6_TX_RATE, rate);
		txwi[6] |= cpu_to_le32(val);
		txwi[3] |= cpu_to_le32(MT_TXD3_BA_DISABLE);
	}
}

static void
mt7921_write_hw_txp(struct mt7921_dev *dev, struct mt76_tx_info *tx_info,
		    void *txp_ptr, u32 id)
{
	struct mt7921_hw_txp *txp = txp_ptr;
	struct mt7921_txp_ptr *ptr = &txp->ptr[0];
	int i, nbuf = tx_info->nbuf - 1;

	tx_info->buf[0].len = MT_TXD_SIZE + sizeof(*txp);
	tx_info->nbuf = 1;

	txp->msdu_id[0] = cpu_to_le16(id | MT_MSDU_ID_VALID);

	for (i = 0; i < nbuf; i++) {
		u16 len = tx_info->buf[i + 1].len & MT_TXD_LEN_MASK;
		u32 addr = tx_info->buf[i + 1].addr;

		if (i == nbuf - 1)
			len |= MT_TXD_LEN_LAST;

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

static void mt7921_set_tx_blocked(struct mt7921_dev *dev, bool blocked)
{
	struct mt76_phy *mphy = &dev->mphy;
	struct mt76_queue *q;

	q = mphy->q_tx[0];
	if (blocked == q->blocked)
		return;

	q->blocked = blocked;
	if (!blocked)
		mt76_worker_schedule(&dev->mt76.tx_worker);
}

int mt7921_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_info->skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct mt76_tx_cb *cb = mt76_tx_skb_cb(tx_info->skb);
	struct mt76_txwi_cache *t;
	struct mt7921_txp_common *txp;
	int id;
	u8 *txwi = (u8 *)txwi_ptr;

	if (unlikely(tx_info->skb->len <= ETH_HLEN))
		return -EINVAL;

	if (!wcid)
		wcid = &dev->mt76.global_wcid;

	cb->wcid = wcid->idx;

	t = (struct mt76_txwi_cache *)(txwi + mdev->drv->txwi_size);
	t->skb = tx_info->skb;

	spin_lock_bh(&dev->token_lock);
	id = idr_alloc(&dev->token, t, 0, MT7921_TOKEN_SIZE, GFP_ATOMIC);
	if (id >= 0)
		dev->token_count++;

	if (dev->token_count >= MT7921_TOKEN_SIZE - MT7921_TOKEN_FREE_THR)
		mt7921_set_tx_blocked(dev, true);
	spin_unlock_bh(&dev->token_lock);

	if (id < 0)
		return id;

	mt7921_mac_write_txwi(dev, txwi_ptr, tx_info->skb, wcid, key,
			      false);

	txp = (struct mt7921_txp_common *)(txwi + MT_TXD_SIZE);
	memset(txp, 0, sizeof(struct mt7921_txp_common));
	mt7921_write_hw_txp(dev, tx_info, txp, id);

	tx_info->skb = DMA_DUMMY_DATA;

	return 0;
}

static void
mt7921_tx_check_aggr(struct ieee80211_sta *sta, __le32 *txwi)
{
	struct mt7921_sta *msta;
	u16 fc, tid;
	u32 val;

	if (!sta || !sta->ht_cap.ht_supported)
		return;

	tid = FIELD_GET(MT_TXD1_TID, le32_to_cpu(txwi[1]));
	if (tid >= 6) /* skip VO queue */
		return;

	val = le32_to_cpu(txwi[2]);
	fc = FIELD_GET(MT_TXD2_FRAME_TYPE, val) << 2 |
	     FIELD_GET(MT_TXD2_SUB_TYPE, val) << 4;
	if (unlikely(fc != (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_DATA)))
		return;

	msta = (struct mt7921_sta *)sta->drv_priv;
	if (!test_and_set_bit(tid, &msta->ampdu_state))
		ieee80211_start_tx_ba_session(sta, tid, 0);
}

static void
mt7921_tx_complete_status(struct mt76_dev *mdev, struct sk_buff *skb,
			  struct ieee80211_sta *sta, u8 stat,
			  struct list_head *free_list)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_status status = {
		.sta = sta,
		.info = info,
		.skb = skb,
		.free_list = free_list,
	};
	struct ieee80211_hw *hw;

	if (sta) {
		struct mt7921_sta *msta;

		msta = (struct mt7921_sta *)sta->drv_priv;
		status.rate = &msta->stats.tx_rate;
	}

	hw = mt76_tx_status_get_hw(mdev, skb);

	if (info->flags & IEEE80211_TX_CTL_AMPDU)
		info->flags |= IEEE80211_TX_STAT_AMPDU;

	if (stat)
		ieee80211_tx_info_clear_status(info);

	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
		info->flags |= IEEE80211_TX_STAT_ACK;

	info->status.tx_time = 0;
	ieee80211_tx_status_ext(hw, &status);
}

void mt7921_txp_skb_unmap(struct mt76_dev *dev,
			  struct mt76_txwi_cache *t)
{
	struct mt7921_txp_common *txp;
	int i;

	txp = mt7921_txwi_to_txp(dev, t);

	for (i = 0; i < ARRAY_SIZE(txp->hw.ptr); i++) {
		struct mt7921_txp_ptr *ptr = &txp->hw.ptr[i];
		bool last;
		u16 len;

		len = le16_to_cpu(ptr->len0);
		last = len & MT_TXD_LEN_LAST;
		len &= MT_TXD_LEN_MASK;
		dma_unmap_single(dev->dev, le32_to_cpu(ptr->buf0), len,
				 DMA_TO_DEVICE);
		if (last)
			break;

		len = le16_to_cpu(ptr->len1);
		last = len & MT_TXD_LEN_LAST;
		len &= MT_TXD_LEN_MASK;
		dma_unmap_single(dev->dev, le32_to_cpu(ptr->buf1), len,
				 DMA_TO_DEVICE);
		if (last)
			break;
	}
}

void mt7921_mac_tx_free(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct mt7921_tx_free *free = (struct mt7921_tx_free *)skb->data;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_txwi_cache *txwi;
	struct ieee80211_sta *sta = NULL;
	LIST_HEAD(free_list);
	struct sk_buff *tmp;
	bool wake = false;
	u8 i, count;

	/* clean DMA queues and unmap buffers first */
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_PSD], false);
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_BE], false);

	/* TODO: MT_TX_FREE_LATENCY is msdu time from the TXD is queued into PLE,
	 * to the time ack is received or dropped by hw (air + hw queue time).
	 * Should avoid accessing WTBL to get Tx airtime, and use it instead.
	 */
	count = FIELD_GET(MT_TX_FREE_MSDU_CNT, le16_to_cpu(free->ctrl));
	for (i = 0; i < count; i++) {
		u32 msdu, info = le32_to_cpu(free->info[i]);
		u8 stat;

		/* 1'b1: new wcid pair.
		 * 1'b0: msdu_id with the same 'wcid pair' as above.
		 */
		if (info & MT_TX_FREE_PAIR) {
			struct mt7921_sta *msta;
			struct mt7921_phy *phy;
			struct mt76_wcid *wcid;
			u16 idx;

			count++;
			idx = FIELD_GET(MT_TX_FREE_WLAN_ID, info);
			wcid = rcu_dereference(dev->mt76.wcid[idx]);
			sta = wcid_to_sta(wcid);
			if (!sta)
				continue;

			msta = container_of(wcid, struct mt7921_sta, wcid);
			phy = msta->vif->phy;
			spin_lock_bh(&dev->sta_poll_lock);
			if (list_empty(&msta->stats_list))
				list_add_tail(&msta->stats_list, &phy->stats_list);
			if (list_empty(&msta->poll_list))
				list_add_tail(&msta->poll_list, &dev->sta_poll_list);
			spin_unlock_bh(&dev->sta_poll_lock);
			continue;
		}

		msdu = FIELD_GET(MT_TX_FREE_MSDU_ID, info);
		stat = FIELD_GET(MT_TX_FREE_STATUS, info);

		spin_lock_bh(&dev->token_lock);
		txwi = idr_remove(&dev->token, msdu);
		if (txwi)
			dev->token_count--;
		if (dev->token_count < MT7921_TOKEN_SIZE - MT7921_TOKEN_FREE_THR &&
		    dev->mphy.q_tx[0]->blocked)
			wake = true;
		spin_unlock_bh(&dev->token_lock);

		if (!txwi)
			continue;

		mt7921_txp_skb_unmap(mdev, txwi);
		if (txwi->skb) {
			struct ieee80211_tx_info *info = IEEE80211_SKB_CB(txwi->skb);
			void *txwi_ptr = mt76_get_txwi_ptr(mdev, txwi);

			if (likely(txwi->skb->protocol != cpu_to_be16(ETH_P_PAE)))
				mt7921_tx_check_aggr(sta, txwi_ptr);

			if (sta && !info->tx_time_est) {
				struct mt76_wcid *wcid = (struct mt76_wcid *)sta->drv_priv;
				int pending;

				pending = atomic_dec_return(&wcid->non_aql_packets);
				if (pending < 0)
					atomic_cmpxchg(&wcid->non_aql_packets, pending, 0);
			}

			mt7921_tx_complete_status(mdev, txwi->skb, sta, stat, &free_list);
			txwi->skb = NULL;
		}

		mt76_put_txwi(mdev, txwi);
	}

	if (wake) {
		spin_lock_bh(&dev->token_lock);
		mt7921_set_tx_blocked(dev, false);
		spin_unlock_bh(&dev->token_lock);
	}

	napi_consume_skb(skb, 1);

	list_for_each_entry_safe(skb, tmp, &free_list, list) {
		skb_list_del_init(skb);
		napi_consume_skb(skb, 1);
	}

	if (test_bit(MT76_STATE_PM, &dev->phy.mt76->state))
		return;

	mt7921_mac_sta_poll(dev);

	mt76_connac_power_save_sched(&dev->mphy, &dev->pm);

	mt76_worker_schedule(&dev->mt76.tx_worker);
}

void mt7921_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue_entry *e)
{
	struct mt7921_dev *dev;

	if (!e->txwi) {
		dev_kfree_skb_any(e->skb);
		return;
	}

	dev = container_of(mdev, struct mt7921_dev, mt76);

	/* error path */
	if (e->skb == DMA_DUMMY_DATA) {
		struct mt76_txwi_cache *t;
		struct mt7921_txp_common *txp;
		u16 token;

		txp = mt7921_txwi_to_txp(mdev, e->txwi);

		token = le16_to_cpu(txp->hw.msdu_id[0]) & ~MT_MSDU_ID_VALID;
		spin_lock_bh(&dev->token_lock);
		t = idr_remove(&dev->token, token);
		spin_unlock_bh(&dev->token_lock);
		e->skb = t ? t->skb : NULL;
	}

	if (e->skb) {
		struct mt76_tx_cb *cb = mt76_tx_skb_cb(e->skb);
		struct mt76_wcid *wcid;

		wcid = rcu_dereference(dev->mt76.wcid[cb->wcid]);

		mt7921_tx_complete_status(mdev, e->skb, wcid_to_sta(wcid), 0,
					  NULL);
	}
}

void mt7921_mac_reset_counters(struct mt7921_phy *phy)
{
	struct mt7921_dev *dev = phy->dev;
	int i;

	for (i = 0; i < 4; i++) {
		mt76_rr(dev, MT_TX_AGG_CNT(0, i));
		mt76_rr(dev, MT_TX_AGG_CNT2(0, i));
	}

	dev->mt76.phy.survey_time = ktime_get_boottime();
	memset(&dev->mt76.aggr_stats[0], 0, sizeof(dev->mt76.aggr_stats) / 2);

	/* reset airtime counters */
	mt76_rr(dev, MT_MIB_SDR9(0));
	mt76_rr(dev, MT_MIB_SDR36(0));
	mt76_rr(dev, MT_MIB_SDR37(0));

	mt76_set(dev, MT_WF_RMAC_MIB_TIME0(0), MT_WF_RMAC_MIB_RXTIME_CLR);
	mt76_set(dev, MT_WF_RMAC_MIB_AIRTIME0(0), MT_WF_RMAC_MIB_RXTIME_CLR);
}

void mt7921_mac_set_timing(struct mt7921_phy *phy)
{
	s16 coverage_class = phy->coverage_class;
	struct mt7921_dev *dev = phy->dev;
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

	mt76_set(dev, MT_ARB_SCR(0),
		 MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
	udelay(1);

	offset = 3 * coverage_class;
	reg_offset = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, offset) |
		     FIELD_PREP(MT_TIMEOUT_VAL_CCA, offset);

	mt76_wr(dev, MT_TMAC_CDTR(0), cck + reg_offset);
	mt76_wr(dev, MT_TMAC_ODTR(0), ofdm + reg_offset);
	mt76_wr(dev, MT_TMAC_ICR0(0),
		FIELD_PREP(MT_IFS_EIFS, 360) |
		FIELD_PREP(MT_IFS_RIFS, 2) |
		FIELD_PREP(MT_IFS_SIFS, sifs) |
		FIELD_PREP(MT_IFS_SLOT, phy->slottime));

	if (phy->slottime < 20 || is_5ghz)
		val = MT7921_CFEND_RATE_DEFAULT;
	else
		val = MT7921_CFEND_RATE_11B;

	mt76_rmw_field(dev, MT_AGG_ACR0(0), MT_AGG_ACR_CFEND_RATE, val);
	mt76_clear(dev, MT_ARB_SCR(0),
		   MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
}

static u8
mt7921_phy_get_nf(struct mt7921_phy *phy, int idx)
{
	return 0;
}

static void
mt7921_phy_update_channel(struct mt76_phy *mphy, int idx)
{
	struct mt7921_dev *dev = container_of(mphy->dev, struct mt7921_dev, mt76);
	struct mt7921_phy *phy = (struct mt7921_phy *)mphy->priv;
	struct mt76_channel_state *state;
	u64 busy_time, tx_time, rx_time, obss_time;
	int nf;

	busy_time = mt76_get_field(dev, MT_MIB_SDR9(idx),
				   MT_MIB_SDR9_BUSY_MASK);
	tx_time = mt76_get_field(dev, MT_MIB_SDR36(idx),
				 MT_MIB_SDR36_TXTIME_MASK);
	rx_time = mt76_get_field(dev, MT_MIB_SDR37(idx),
				 MT_MIB_SDR37_RXTIME_MASK);
	obss_time = mt76_get_field(dev, MT_WF_RMAC_MIB_AIRTIME14(idx),
				   MT_MIB_OBSSTIME_MASK);

	nf = mt7921_phy_get_nf(phy, idx);
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

void mt7921_update_channel(struct mt76_dev *mdev)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);

	if (mt76_connac_pm_wake(&dev->mphy, &dev->pm))
		return;

	mt7921_phy_update_channel(&mdev->phy, 0);
	/* reset obss airtime */
	mt76_set(dev, MT_WF_RMAC_MIB_TIME0(0), MT_WF_RMAC_MIB_RXTIME_CLR);

	mt76_connac_power_save_sched(&dev->mphy, &dev->pm);
}

static bool
mt7921_wait_reset_state(struct mt7921_dev *dev, u32 state)
{
	bool ret;

	ret = wait_event_timeout(dev->reset_wait,
				 (READ_ONCE(dev->reset_state) & state),
				 MT7921_RESET_TIMEOUT);

	WARN(!ret, "Timeout waiting for MCU reset state %x\n", state);
	return ret;
}

static void
mt7921_dma_reset(struct mt7921_phy *phy)
{
	struct mt7921_dev *dev = phy->dev;
	int i;

	mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	usleep_range(1000, 2000);

	mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[MT_MCUQ_WA], true);
	for (i = 0; i < __MT_TXQ_MAX; i++)
		mt76_queue_tx_cleanup(dev, phy->mt76->q_tx[i], true);

	mt76_for_each_q_rx(&dev->mt76, i) {
		mt76_queue_rx_reset(dev, i);
	}

	/* re-init prefetch settings after reset */
	mt7921_dma_prefetch(dev);

	mt76_set(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);
}

void mt7921_tx_token_put(struct mt7921_dev *dev)
{
	struct mt76_txwi_cache *txwi;
	int id;

	spin_lock_bh(&dev->token_lock);
	idr_for_each_entry(&dev->token, txwi, id) {
		mt7921_txp_skb_unmap(&dev->mt76, txwi);
		if (txwi->skb) {
			struct ieee80211_hw *hw;

			hw = mt76_tx_status_get_hw(&dev->mt76, txwi->skb);
			ieee80211_free_txskb(hw, txwi->skb);
		}
		mt76_put_txwi(&dev->mt76, txwi);
		dev->token_count--;
	}
	spin_unlock_bh(&dev->token_lock);
	idr_destroy(&dev->token);
}

/* system error recovery */
void mt7921_mac_reset_work(struct work_struct *work)
{
	struct mt7921_dev *dev;

	dev = container_of(work, struct mt7921_dev, reset_work);

	if (!(READ_ONCE(dev->reset_state) & MT_MCU_CMD_STOP_DMA))
		return;

	ieee80211_stop_queues(mt76_hw(dev));

	set_bit(MT76_RESET, &dev->mphy.state);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);
	wake_up(&dev->mt76.mcu.wait);
	cancel_delayed_work_sync(&dev->mphy.mac_work);

	/* lock/unlock all queues to ensure that no tx is pending */
	mt76_txq_schedule_all(&dev->mphy);

	mt76_worker_disable(&dev->mt76.tx_worker);
	napi_disable(&dev->mt76.napi[0]);
	napi_disable(&dev->mt76.napi[1]);
	napi_disable(&dev->mt76.napi[2]);
	napi_disable(&dev->mt76.tx_napi);

	mt7921_mutex_acquire(dev);

	mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_DMA_STOPPED);

	mt7921_tx_token_put(dev);
	idr_init(&dev->token);

	if (mt7921_wait_reset_state(dev, MT_MCU_CMD_RESET_DONE)) {
		mt7921_dma_reset(&dev->phy);

		mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_DMA_INIT);
		mt7921_wait_reset_state(dev, MT_MCU_CMD_RECOVERY_DONE);
	}

	clear_bit(MT76_MCU_RESET, &dev->mphy.state);
	clear_bit(MT76_RESET, &dev->mphy.state);

	mt76_worker_enable(&dev->mt76.tx_worker);
	napi_enable(&dev->mt76.tx_napi);
	napi_schedule(&dev->mt76.tx_napi);

	napi_enable(&dev->mt76.napi[0]);
	napi_schedule(&dev->mt76.napi[0]);

	napi_enable(&dev->mt76.napi[1]);
	napi_schedule(&dev->mt76.napi[1]);

	napi_enable(&dev->mt76.napi[2]);
	napi_schedule(&dev->mt76.napi[2]);

	ieee80211_wake_queues(mt76_hw(dev));

	mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_RESET_DONE);
	mt7921_wait_reset_state(dev, MT_MCU_CMD_NORMAL_STATE);

	mt7921_mutex_release(dev);

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mphy.mac_work,
				     MT7921_WATCHDOG_TIME);
}

static void
mt7921_mac_update_mib_stats(struct mt7921_phy *phy)
{
	struct mt7921_dev *dev = phy->dev;
	struct mib_stats *mib = &phy->mib;
	int i, aggr0 = 0, aggr1;

	memset(mib, 0, sizeof(*mib));

	mib->fcs_err_cnt = mt76_get_field(dev, MT_MIB_SDR3(0),
					  MT_MIB_SDR3_FCS_ERR_MASK);

	for (i = 0, aggr1 = aggr0 + 4; i < 4; i++) {
		u32 val, val2;

		val = mt76_rr(dev, MT_MIB_MB_SDR1(0, i));

		val2 = FIELD_GET(MT_MIB_ACK_FAIL_COUNT_MASK, val);
		if (val2 > mib->ack_fail_cnt)
			mib->ack_fail_cnt = val2;

		val2 = FIELD_GET(MT_MIB_BA_MISS_COUNT_MASK, val);
		if (val2 > mib->ba_miss_cnt)
			mib->ba_miss_cnt = val2;

		val = mt76_rr(dev, MT_MIB_MB_SDR0(0, i));
		val2 = FIELD_GET(MT_MIB_RTS_RETRIES_COUNT_MASK, val);
		if (val2 > mib->rts_retries_cnt) {
			mib->rts_cnt = FIELD_GET(MT_MIB_RTS_COUNT_MASK, val);
			mib->rts_retries_cnt = val2;
		}

		val = mt76_rr(dev, MT_TX_AGG_CNT(0, i));
		val2 = mt76_rr(dev, MT_TX_AGG_CNT2(0, i));

		dev->mt76.aggr_stats[aggr0++] += val & 0xffff;
		dev->mt76.aggr_stats[aggr0++] += val >> 16;
		dev->mt76.aggr_stats[aggr1++] += val2 & 0xffff;
		dev->mt76.aggr_stats[aggr1++] += val2 >> 16;
	}
}

static void
mt7921_mac_sta_stats_work(struct mt7921_phy *phy)
{
	struct mt7921_dev *dev = phy->dev;
	struct mt7921_sta *msta;
	LIST_HEAD(list);

	spin_lock_bh(&dev->sta_poll_lock);
	list_splice_init(&phy->stats_list, &list);

	while (!list_empty(&list)) {
		msta = list_first_entry(&list, struct mt7921_sta, stats_list);
		list_del_init(&msta->stats_list);
		spin_unlock_bh(&dev->sta_poll_lock);

		/* query wtbl info to report tx rate for further devices */
		mt7921_get_wtbl_info(dev, msta->wcid.idx);

		spin_lock_bh(&dev->sta_poll_lock);
	}

	spin_unlock_bh(&dev->sta_poll_lock);
}

void mt7921_mac_work(struct work_struct *work)
{
	struct mt7921_phy *phy;
	struct mt76_phy *mphy;

	mphy = (struct mt76_phy *)container_of(work, struct mt76_phy,
					       mac_work.work);
	phy = mphy->priv;

	if (test_bit(MT76_STATE_PM, &mphy->state))
		goto out;

	mt7921_mutex_acquire(phy->dev);

	mt76_update_survey(mphy->dev);
	if (++mphy->mac_work_count == 5) {
		mphy->mac_work_count = 0;

		mt7921_mac_update_mib_stats(phy);
	}
	if (++phy->sta_work_count == 10) {
		phy->sta_work_count = 0;
		mt7921_mac_sta_stats_work(phy);
	};

	mt7921_mutex_release(phy->dev);

out:
	ieee80211_queue_delayed_work(phy->mt76->hw, &mphy->mac_work,
				     MT7921_WATCHDOG_TIME);
}

void mt7921_pm_wake_work(struct work_struct *work)
{
	struct mt7921_dev *dev;
	struct mt76_phy *mphy;

	dev = (struct mt7921_dev *)container_of(work, struct mt7921_dev,
						pm.wake_work);
	mphy = dev->phy.mt76;

	if (!mt7921_mcu_drv_pmctrl(dev))
		mt76_connac_pm_dequeue_skbs(mphy, &dev->pm);
	else
		dev_err(mphy->dev->dev, "failed to wake device\n");

	ieee80211_wake_queues(mphy->hw);
	complete_all(&dev->pm.wake_cmpl);
}

void mt7921_pm_power_save_work(struct work_struct *work)
{
	struct mt7921_dev *dev;
	unsigned long delta;

	dev = (struct mt7921_dev *)container_of(work, struct mt7921_dev,
						pm.ps_work.work);

	delta = dev->pm.idle_timeout;
	if (time_is_after_jiffies(dev->pm.last_activity + delta)) {
		delta = dev->pm.last_activity + delta - jiffies;
		goto out;
	}

	if (!mt7921_mcu_fw_pmctrl(dev))
		return;
out:
	queue_delayed_work(dev->mt76.wq, &dev->pm.ps_work, delta);
}

int mt7921_mac_set_beacon_filter(struct mt7921_phy *phy,
				 struct ieee80211_vif *vif,
				 bool enable)
{
	struct mt7921_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	int err;

	if (!dev->pm.enable)
		return -EOPNOTSUPP;

	err = mt7921_mcu_set_bss_pm(dev, vif, enable);
	if (err)
		return err;

	if (enable) {
		vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER;
		mt76_set(dev, MT_WF_RFCR(ext_phy),
			 MT_WF_RFCR_DROP_OTHER_BEACON);
	} else {
		vif->driver_flags &= ~IEEE80211_VIF_BEACON_FILTER;
		mt76_clear(dev, MT_WF_RFCR(ext_phy),
			   MT_WF_RFCR_DROP_OTHER_BEACON);
	}

	return 0;
}

void mt7921_coredump_work(struct work_struct *work)
{
	struct mt7921_dev *dev;
	char *dump, *data;

	dev = (struct mt7921_dev *)container_of(work, struct mt7921_dev,
						coredump.work.work);

	if (time_is_after_jiffies(dev->coredump.last_activity +
				  4 * MT76_CONNAC_COREDUMP_TIMEOUT)) {
		queue_delayed_work(dev->mt76.wq, &dev->coredump.work,
				   MT76_CONNAC_COREDUMP_TIMEOUT);
		return;
	}

	dump = vzalloc(MT76_CONNAC_COREDUMP_SZ);
	data = dump;

	while (true) {
		struct sk_buff *skb;

		spin_lock_bh(&dev->mt76.lock);
		skb = __skb_dequeue(&dev->coredump.msg_list);
		spin_unlock_bh(&dev->mt76.lock);

		if (!skb)
			break;

		skb_pull(skb, sizeof(struct mt7921_mcu_rxd));
		if (data + skb->len - dump > MT76_CONNAC_COREDUMP_SZ)
			break;

		memcpy(data, skb->data, skb->len);
		data += skb->len;

		dev_kfree_skb(skb);
	}
	dev_coredumpv(dev->mt76.dev, dump, MT76_CONNAC_COREDUMP_SZ,
		      GFP_KERNEL);
}
