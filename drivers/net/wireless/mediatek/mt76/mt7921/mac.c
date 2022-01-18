// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/devcoredump.h>
#include <linux/etherdevice.h>
#include <linux/timekeeping.h>
#include "mt7921.h"
#include "../dma.h"
#include "mac.h"
#include "mcu.h"

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
EXPORT_SYMBOL_GPL(mt7921_sta_ps);

bool mt7921_mac_wtbl_update(struct mt7921_dev *dev, int idx, u32 mask)
{
	mt76_rmw(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_WLAN_IDX,
		 FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, idx) | mask);

	return mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY,
			 0, 5000);
}

void mt7921_mac_sta_poll(struct mt7921_dev *dev)
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
	struct rate_info *rate;
	int i;

	spin_lock_bh(&dev->sta_poll_lock);
	list_splice_init(&dev->sta_poll_list, &sta_poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

	while (true) {
		bool clear = false;
		u32 addr, val;
		u16 idx;
		u8 bw;

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
		addr = mt7921_mac_wtbl_lmac_addr(idx, MT_WTBL_AC0_CTT_OFFSET);

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
			u8 q = mt76_connac_lmac_mapping(i);
			u32 tx_cur = tx_time[q];
			u32 rx_cur = rx_time[q];
			u8 tid = ac_to_tid[i];

			if (!tx_cur && !rx_cur)
				continue;

			ieee80211_sta_register_airtime(sta, tid, tx_cur,
						       rx_cur);
		}

		/* We don't support reading GI info from txs packets.
		 * For accurate tx status reporting and AQL improvement,
		 * we need to make sure that flags match so polling GI
		 * from per-sta counters directly.
		 */
		rate = &msta->wcid.rate;
		addr = mt7921_mac_wtbl_lmac_addr(idx,
						 MT_WTBL_TXRX_CAP_RATE_OFFSET);
		val = mt76_rr(dev, addr);

		switch (rate->bw) {
		case RATE_INFO_BW_160:
			bw = IEEE80211_STA_RX_BW_160;
			break;
		case RATE_INFO_BW_80:
			bw = IEEE80211_STA_RX_BW_80;
			break;
		case RATE_INFO_BW_40:
			bw = IEEE80211_STA_RX_BW_40;
			break;
		default:
			bw = IEEE80211_STA_RX_BW_20;
			break;
		}

		if (rate->flags & RATE_INFO_FLAGS_HE_MCS) {
			u8 offs = MT_WTBL_TXRX_RATE_G2_HE + 2 * bw;

			rate->he_gi = (val & (0x3 << offs)) >> offs;
		} else if (rate->flags &
			   (RATE_INFO_FLAGS_VHT_MCS | RATE_INFO_FLAGS_MCS)) {
			if (val & BIT(MT_WTBL_TXRX_RATE_G2 + bw))
				rate->flags |= RATE_INFO_FLAGS_SHORT_GI;
			else
				rate->flags &= ~RATE_INFO_FLAGS_SHORT_GI;
		}
	}
}
EXPORT_SYMBOL_GPL(mt7921_mac_sta_poll);

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
mt7921_mac_decode_he_mu_radiotap(struct sk_buff *skb, __le32 *rxv)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	static const struct ieee80211_radiotap_he_mu mu_known = {
		.flags1 = HE_BITS(MU_FLAGS1_SIG_B_MCS_KNOWN) |
			  HE_BITS(MU_FLAGS1_SIG_B_DCM_KNOWN) |
			  HE_BITS(MU_FLAGS1_CH1_RU_KNOWN) |
			  HE_BITS(MU_FLAGS1_SIG_B_SYMS_USERS_KNOWN) |
			  HE_BITS(MU_FLAGS1_SIG_B_COMP_KNOWN),
		.flags2 = HE_BITS(MU_FLAGS2_BW_FROM_SIG_A_BW_KNOWN) |
			  HE_BITS(MU_FLAGS2_PUNC_FROM_SIG_A_BW_KNOWN),
	};
	struct ieee80211_radiotap_he_mu *he_mu;

	status->flag |= RX_FLAG_RADIOTAP_HE_MU;

	he_mu = skb_push(skb, sizeof(mu_known));
	memcpy(he_mu, &mu_known, sizeof(mu_known));

#define MU_PREP(f, v)	le16_encode_bits(v, IEEE80211_RADIOTAP_HE_MU_##f)

	he_mu->flags1 |= MU_PREP(FLAGS1_SIG_B_MCS, status->rate_idx);
	if (status->he_dcm)
		he_mu->flags1 |= MU_PREP(FLAGS1_SIG_B_DCM, status->he_dcm);

	he_mu->flags2 |= MU_PREP(FLAGS2_BW_FROM_SIG_A_BW, status->bw) |
			 MU_PREP(FLAGS2_SIG_B_SYMS_USERS,
				 le32_get_bits(rxv[2], MT_CRXV_HE_NUM_USER));

	he_mu->ru_ch1[0] = FIELD_GET(MT_CRXV_HE_RU0, le32_to_cpu(rxv[3]));

	if (status->bw >= RATE_INFO_BW_40) {
		he_mu->flags1 |= HE_BITS(MU_FLAGS1_CH2_RU_KNOWN);
		he_mu->ru_ch2[0] =
			FIELD_GET(MT_CRXV_HE_RU1, le32_to_cpu(rxv[3]));
	}

	if (status->bw >= RATE_INFO_BW_80) {
		he_mu->ru_ch1[1] =
			FIELD_GET(MT_CRXV_HE_RU2, le32_to_cpu(rxv[3]));
		he_mu->ru_ch2[1] =
			FIELD_GET(MT_CRXV_HE_RU3, le32_to_cpu(rxv[3]));
	}
}

static void
mt7921_mac_decode_he_radiotap(struct sk_buff *skb, __le32 *rxv, u32 mode)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	static const struct ieee80211_radiotap_he known = {
		.data1 = HE_BITS(DATA1_DATA_MCS_KNOWN) |
			 HE_BITS(DATA1_DATA_DCM_KNOWN) |
			 HE_BITS(DATA1_STBC_KNOWN) |
			 HE_BITS(DATA1_CODING_KNOWN) |
			 HE_BITS(DATA1_LDPC_XSYMSEG_KNOWN) |
			 HE_BITS(DATA1_DOPPLER_KNOWN) |
			 HE_BITS(DATA1_SPTL_REUSE_KNOWN) |
			 HE_BITS(DATA1_BSS_COLOR_KNOWN),
		.data2 = HE_BITS(DATA2_GI_KNOWN) |
			 HE_BITS(DATA2_TXBF_KNOWN) |
			 HE_BITS(DATA2_PE_DISAMBIG_KNOWN) |
			 HE_BITS(DATA2_TXOP_KNOWN),
	};
	struct ieee80211_radiotap_he *he = NULL;
	u32 ltf_size = le32_get_bits(rxv[2], MT_CRXV_HE_LTF_SIZE) + 1;

	status->flag |= RX_FLAG_RADIOTAP_HE;

	he = skb_push(skb, sizeof(known));
	memcpy(he, &known, sizeof(known));

	he->data3 = HE_PREP(DATA3_BSS_COLOR, BSS_COLOR, rxv[14]) |
		    HE_PREP(DATA3_LDPC_XSYMSEG, LDPC_EXT_SYM, rxv[2]);
	he->data4 = HE_PREP(DATA4_SU_MU_SPTL_REUSE, SR_MASK, rxv[11]);
	he->data5 = HE_PREP(DATA5_PE_DISAMBIG, PE_DISAMBIG, rxv[2]) |
		    le16_encode_bits(ltf_size,
				     IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE);
	if (le32_to_cpu(rxv[0]) & MT_PRXV_TXBF)
		he->data5 |= HE_BITS(DATA5_TXBF);
	he->data6 = HE_PREP(DATA6_TXOP, TXOP_DUR, rxv[14]) |
		    HE_PREP(DATA6_DOPPLER, DOPPLER, rxv[14]);

	switch (mode) {
	case MT_PHY_TYPE_HE_SU:
		he->data1 |= HE_BITS(DATA1_FORMAT_SU) |
			     HE_BITS(DATA1_UL_DL_KNOWN) |
			     HE_BITS(DATA1_BEAM_CHANGE_KNOWN);

		he->data3 |= HE_PREP(DATA3_BEAM_CHANGE, BEAM_CHNG, rxv[14]) |
			     HE_PREP(DATA3_UL_DL, UPLINK, rxv[2]);
		break;
	case MT_PHY_TYPE_HE_EXT_SU:
		he->data1 |= HE_BITS(DATA1_FORMAT_EXT_SU) |
			     HE_BITS(DATA1_UL_DL_KNOWN);

		he->data3 |= HE_PREP(DATA3_UL_DL, UPLINK, rxv[2]);
		break;
	case MT_PHY_TYPE_HE_MU:
		he->data1 |= HE_BITS(DATA1_FORMAT_MU) |
			     HE_BITS(DATA1_UL_DL_KNOWN);

		he->data3 |= HE_PREP(DATA3_UL_DL, UPLINK, rxv[2]);
		he->data4 |= HE_PREP(DATA4_MU_STA_ID, MU_AID, rxv[7]);

		mt7921_mac_decode_he_radiotap_ru(status, he, rxv);
		mt7921_mac_decode_he_mu_radiotap(skb, rxv);
		break;
	case MT_PHY_TYPE_HE_TB:
		he->data1 |= HE_BITS(DATA1_FORMAT_TRIG) |
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

	if (chfreq > 180) {
		status->band = NL80211_BAND_6GHZ;
		chfreq = (chfreq - 181) * 4 + 1;
	} else if (chfreq > 14) {
		status->band = NL80211_BAND_5GHZ;
	} else {
		status->band = NL80211_BAND_2GHZ;
	}
	status->freq = ieee80211_channel_to_frequency(chfreq, status->band);
}

static void
mt7921_mac_rssi_iter(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct sk_buff *skb = priv;
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct ieee80211_hdr *hdr = mt76_skb_get_hdr(skb);

	if (status->signal > 0)
		return;

	if (!ether_addr_equal(vif->addr, hdr->addr1))
		return;

	ewma_rssi_add(&mvif->rssi, -status->signal);
}

static void
mt7921_mac_assoc_rssi(struct mt7921_dev *dev, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = mt76_skb_get_hdr(skb);

	if (!ieee80211_is_assoc_resp(hdr->frame_control) &&
	    !ieee80211_is_auth(hdr->frame_control))
		return;

	ieee80211_iterate_active_interfaces_atomic(mt76_hw(dev),
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7921_mac_rssi_iter, skb);
}

/* The HW does not translate the mac header to 802.3 for mesh point */
static int mt7921_reverse_frag0_hdr_trans(struct sk_buff *skb, u16 hdr_gap)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt7921_sta *msta = (struct mt7921_sta *)status->wcid;
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif;
	struct ieee80211_hdr hdr;
	struct ethhdr eth_hdr;
	__le32 *rxd = (__le32 *)skb->data;
	__le32 qos_ctrl, ht_ctrl;

	if (FIELD_GET(MT_RXD3_NORMAL_ADDR_TYPE, le32_to_cpu(rxd[3])) !=
	    MT_RXD3_NORMAL_U2M)
		return -EINVAL;

	if (!(le32_to_cpu(rxd[1]) & MT_RXD1_NORMAL_GROUP_4))
		return -EINVAL;

	if (!msta || !msta->vif)
		return -EINVAL;

	sta = container_of((void *)msta, struct ieee80211_sta, drv_priv);
	vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);

	/* store the info from RXD and ethhdr to avoid being overridden */
	memcpy(&eth_hdr, skb->data + hdr_gap, sizeof(eth_hdr));
	hdr.frame_control = FIELD_GET(MT_RXD6_FRAME_CONTROL, rxd[6]);
	hdr.seq_ctrl = FIELD_GET(MT_RXD8_SEQ_CTRL, rxd[8]);
	qos_ctrl = FIELD_GET(MT_RXD8_QOS_CTL, rxd[8]);
	ht_ctrl = FIELD_GET(MT_RXD9_HT_CONTROL, rxd[9]);

	hdr.duration_id = 0;
	ether_addr_copy(hdr.addr1, vif->addr);
	ether_addr_copy(hdr.addr2, sta->addr);
	switch (le16_to_cpu(hdr.frame_control) &
		(IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS)) {
	case 0:
		ether_addr_copy(hdr.addr3, vif->bss_conf.bssid);
		break;
	case IEEE80211_FCTL_FROMDS:
		ether_addr_copy(hdr.addr3, eth_hdr.h_source);
		break;
	case IEEE80211_FCTL_TODS:
		ether_addr_copy(hdr.addr3, eth_hdr.h_dest);
		break;
	case IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS:
		ether_addr_copy(hdr.addr3, eth_hdr.h_dest);
		ether_addr_copy(hdr.addr4, eth_hdr.h_source);
		break;
	default:
		break;
	}

	skb_pull(skb, hdr_gap + sizeof(struct ethhdr) - 2);
	if (eth_hdr.h_proto == htons(ETH_P_AARP) ||
	    eth_hdr.h_proto == htons(ETH_P_IPX))
		ether_addr_copy(skb_push(skb, ETH_ALEN), bridge_tunnel_header);
	else if (eth_hdr.h_proto >= htons(ETH_P_802_3_MIN))
		ether_addr_copy(skb_push(skb, ETH_ALEN), rfc1042_header);
	else
		skb_pull(skb, 2);

	if (ieee80211_has_order(hdr.frame_control))
		memcpy(skb_push(skb, 2), &ht_ctrl, 2);
	if (ieee80211_is_data_qos(hdr.frame_control))
		memcpy(skb_push(skb, 2), &qos_ctrl, 2);
	if (ieee80211_has_a4(hdr.frame_control))
		memcpy(skb_push(skb, sizeof(hdr)), &hdr, sizeof(hdr));
	else
		memcpy(skb_push(skb, sizeof(hdr) - 6), &hdr, sizeof(hdr) - 6);

	return 0;
}

static int
mt7921_mac_fill_rx(struct mt7921_dev *dev, struct sk_buff *skb)
{
	u32 csum_mask = MT_RXD0_NORMAL_IP_SUM | MT_RXD0_NORMAL_UDP_TCP_SUM;
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	bool hdr_trans, unicast, insert_ccmp_hdr = false;
	u8 chfreq, qos_ctl = 0, remove_pad, amsdu_info;
	u16 hdr_gap;
	__le32 *rxv = NULL, *rxd = (__le32 *)skb->data;
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7921_phy *phy = &dev->phy;
	struct ieee80211_supported_band *sband;
	u32 rxd0 = le32_to_cpu(rxd[0]);
	u32 rxd1 = le32_to_cpu(rxd[1]);
	u32 rxd2 = le32_to_cpu(rxd[2]);
	u32 rxd3 = le32_to_cpu(rxd[3]);
	u32 rxd4 = le32_to_cpu(rxd[4]);
	u16 seq_ctrl = 0;
	__le16 fc = 0;
	u32 mode = 0;
	int i, idx;

	memset(status, 0, sizeof(*status));

	if (rxd1 & MT_RXD1_NORMAL_BAND_IDX)
		return -EINVAL;

	if (!test_bit(MT76_STATE_RUNNING, &mphy->state))
		return -EINVAL;

	if (rxd2 & MT_RXD2_NORMAL_AMSDU_ERR)
		return -EINVAL;

	hdr_trans = rxd2 & MT_RXD2_NORMAL_HDR_TRANS;
	if (hdr_trans && (rxd1 & MT_RXD1_NORMAL_CM))
		return -EINVAL;

	/* ICV error or CCMP/BIP/WPI MIC error */
	if (rxd1 & MT_RXD1_NORMAL_ICV_ERR)
		status->flag |= RX_FLAG_ONLY_MONITOR;

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

	switch (status->band) {
	case NL80211_BAND_5GHZ:
		sband = &mphy->sband_5g.sband;
		break;
	case NL80211_BAND_6GHZ:
		sband = &mphy->sband_6g.sband;
		break;
	default:
		sband = &mphy->sband_2g.sband;
		break;
	}

	if (!sband->channels)
		return -EINVAL;

	if ((rxd0 & csum_mask) == csum_mask)
		skb->ip_summed = CHECKSUM_UNNECESSARY;

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

	remove_pad = FIELD_GET(MT_RXD2_NORMAL_HDR_OFFSET, rxd2);

	if (rxd2 & MT_RXD2_NORMAL_MAX_LEN_ERROR)
		return -EINVAL;

	rxd += 6;
	if (rxd1 & MT_RXD1_NORMAL_GROUP_4) {
		u32 v0 = le32_to_cpu(rxd[0]);
		u32 v2 = le32_to_cpu(rxd[2]);

		fc = cpu_to_le16(FIELD_GET(MT_RXD6_FRAME_CONTROL, v0));
		seq_ctrl = FIELD_GET(MT_RXD8_SEQ_CTRL, v2);
		qos_ctl = FIELD_GET(MT_RXD8_QOS_CTL, v2);

		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd1 & MT_RXD1_NORMAL_GROUP_1) {
		u8 *data = (u8 *)rxd;

		if (status->flag & RX_FLAG_DECRYPTED) {
			switch (FIELD_GET(MT_RXD1_NORMAL_SEC_MODE, rxd1)) {
			case MT_CIPHER_AES_CCMP:
			case MT_CIPHER_CCMP_CCX:
			case MT_CIPHER_CCMP_256:
				insert_ccmp_hdr =
					FIELD_GET(MT_RXD2_NORMAL_FRAG, rxd2);
				fallthrough;
			case MT_CIPHER_TKIP:
			case MT_CIPHER_TKIP_NO_MIC:
			case MT_CIPHER_GCMP:
			case MT_CIPHER_GCMP_256:
				status->iv[0] = data[5];
				status->iv[1] = data[4];
				status->iv[2] = data[3];
				status->iv[3] = data[2];
				status->iv[4] = data[1];
				status->iv[5] = data[0];
				break;
			default:
				break;
			}
		}
		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd1 & MT_RXD1_NORMAL_GROUP_2) {
		status->timestamp = le32_to_cpu(rxd[0]);
		status->flag |= RX_FLAG_MACTIME_START;

		if (!(rxd2 & MT_RXD2_NORMAL_NON_AMPDU)) {
			status->flag |= RX_FLAG_AMPDU_DETAILS;

			/* all subframes of an A-MPDU have the same timestamp */
			if (phy->rx_ampdu_ts != status->timestamp) {
				if (!++phy->ampdu_ref)
					phy->ampdu_ref++;
			}
			phy->rx_ampdu_ts = status->timestamp;

			status->ampdu_ref = phy->ampdu_ref;
		}

		rxd += 2;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	/* RXD Group 3 - P-RXV */
	if (rxd1 & MT_RXD1_NORMAL_GROUP_3) {
		u8 stbc, gi;
		u32 v0, v1;
		bool cck;

		rxv = rxd;
		rxd += 2;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;

		v0 = le32_to_cpu(rxv[0]);
		v1 = le32_to_cpu(rxv[1]);

		if (v0 & MT_PRXV_HT_AD_CODE)
			status->enc_flags |= RX_ENC_FLAG_LDPC;

		status->chains = mphy->antenna_mask;
		status->chain_signal[0] = to_rssi(MT_PRXV_RCPI0, v1);
		status->chain_signal[1] = to_rssi(MT_PRXV_RCPI1, v1);
		status->chain_signal[2] = to_rssi(MT_PRXV_RCPI2, v1);
		status->chain_signal[3] = to_rssi(MT_PRXV_RCPI3, v1);
		status->signal = -128;
		for (i = 0; i < hweight8(mphy->antenna_mask); i++) {
			if (!(status->chains & BIT(i)) ||
			    status->chain_signal[i] >= 0)
				continue;

			status->signal = max(status->signal,
					     status->chain_signal[i]);
		}

		if (status->signal == -128)
			status->flag |= RX_FLAG_NO_SIGNAL_VAL;

		stbc = FIELD_GET(MT_PRXV_STBC, v0);
		gi = FIELD_GET(MT_PRXV_SGI, v0);
		cck = false;

		idx = i = FIELD_GET(MT_PRXV_TX_RATE, v0);
		mode = FIELD_GET(MT_PRXV_TX_MODE, v0);

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
		case MT_PHY_TYPE_HE_SU:
		case MT_PHY_TYPE_HE_EXT_SU:
		case MT_PHY_TYPE_HE_TB:
			status->nss =
				FIELD_GET(MT_PRXV_NSTS, v0) + 1;
			status->encoding = RX_ENC_HE;
			i &= GENMASK(3, 0);

			if (gi <= NL80211_RATE_INFO_HE_GI_3_2)
				status->he_gi = gi;

			status->he_dcm = !!(idx & MT_PRXV_TX_DCM);
			break;
		default:
			return -EINVAL;
		}

		status->rate_idx = i;

		switch (FIELD_GET(MT_PRXV_FRAME_MODE, v0)) {
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

		if (rxd1 & MT_RXD1_NORMAL_GROUP_5) {
			rxd += 18;
			if ((u8 *)rxd - skb->data >= skb->len)
				return -EINVAL;
		}
	}

	amsdu_info = FIELD_GET(MT_RXD4_NORMAL_PAYLOAD_FORMAT, rxd4);
	status->amsdu = !!amsdu_info;
	if (status->amsdu) {
		status->first_amsdu = amsdu_info == MT_RXD4_FIRST_AMSDU_FRAME;
		status->last_amsdu = amsdu_info == MT_RXD4_LAST_AMSDU_FRAME;
	}

	hdr_gap = (u8 *)rxd - skb->data + 2 * remove_pad;
	if (hdr_trans && ieee80211_has_morefrags(fc)) {
		if (mt7921_reverse_frag0_hdr_trans(skb, hdr_gap))
			return -EINVAL;
		hdr_trans = false;
	} else {
		skb_pull(skb, hdr_gap);
		if (!hdr_trans && status->amsdu) {
			memmove(skb->data + 2, skb->data,
				ieee80211_get_hdrlen_from_skb(skb));
			skb_pull(skb, 2);
		}
	}

	if (!hdr_trans) {
		struct ieee80211_hdr *hdr;

		if (insert_ccmp_hdr) {
			u8 key_id = FIELD_GET(MT_RXD1_NORMAL_KEY_ID, rxd1);

			mt76_insert_ccmp_hdr(skb, key_id);
		}

		hdr = mt76_skb_get_hdr(skb);
		fc = hdr->frame_control;
		if (ieee80211_is_data_qos(fc)) {
			seq_ctrl = le16_to_cpu(hdr->seq_ctrl);
			qos_ctl = *ieee80211_get_qos_ctl(hdr);
		}
	} else {
		status->flag |= RX_FLAG_8023;
	}

	mt7921_mac_assoc_rssi(dev, skb);

	if (rxv && mode >= MT_PHY_TYPE_HE_SU && !(status->flag & RX_FLAG_8023))
		mt7921_mac_decode_he_radiotap(skb, rxv, mode);

	if (!status->wcid || !ieee80211_is_data_qos(fc))
		return 0;

	status->aggr = unicast && !ieee80211_is_qos_nullfunc(fc);
	status->seqno = IEEE80211_SEQ_TO_SN(seq_ctrl);
	status->qos_ctl = qos_ctl;

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

	if (!ieee80211_is_data(fc) || multicast ||
	    info->flags & IEEE80211_TX_CTL_USE_MINRATE)
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
			   struct ieee80211_key_conf *key, int pid,
			   bool beacon)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_phy *mphy = &dev->mphy;
	u8 p_fmt, q_idx, omac_idx = 0, wmm_idx = 0;
	bool is_mmio = mt76_is_mmio(&dev->mt76);
	u32 sz_txd = is_mmio ? MT_TXD_SIZE : MT_SDIO_TXD_SIZE;
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
		p_fmt = is_mmio ? MT_TX_TYPE_CT : MT_TX_TYPE_SF;
		q_idx = MT_LMAC_ALTX0;
	} else {
		p_fmt = is_mmio ? MT_TX_TYPE_CT : MT_TX_TYPE_SF;
		q_idx = wmm_idx * MT7921_MAX_WMM_SETS +
			mt76_connac_lmac_mapping(skb_get_queue_mapping(skb));
	}

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len + sz_txd) |
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

	val = FIELD_PREP(MT_TXD5_PID, pid);
	if (pid >= MT_PACKET_ID_FIRST)
		val |= MT_TXD5_TX_STATUS_HOST;
	txwi[5] = cpu_to_le32(val);

	txwi[6] = 0;
	txwi[7] = wcid->amsdu ? cpu_to_le32(MT_TXD7_HW_AMSDU) : 0;

	if (is_8023)
		mt7921_mac_write_txwi_8023(dev, txwi, skb, wcid);
	else
		mt7921_mac_write_txwi_80211(dev, txwi, skb, key);

	if (txwi[2] & cpu_to_le32(MT_TXD2_FIX_RATE)) {
		int rateidx = vif ? ffs(vif->bss_conf.basic_rates) - 1 : 0;
		u16 rate, mode;

		/* hardware won't add HTC for mgmt/ctrl frame */
		txwi[2] |= cpu_to_le32(MT_TXD2_HTC_VLD);

		rate = mt76_calculate_default_rate(mphy, rateidx);
		mode = rate >> 8;
		rate &= GENMASK(7, 0);
		rate |= FIELD_PREP(MT_TX_RATE_MODE, mode);

		val = MT_TXD6_FIXED_BW |
		      FIELD_PREP(MT_TXD6_TX_RATE, rate);
		txwi[6] |= cpu_to_le32(val);
		txwi[3] |= cpu_to_le32(MT_TXD3_BA_DISABLE);
	}
}
EXPORT_SYMBOL_GPL(mt7921_mac_write_txwi);

void mt7921_tx_check_aggr(struct ieee80211_sta *sta, __le32 *txwi)
{
	struct mt7921_sta *msta;
	u16 fc, tid;
	u32 val;

	if (!sta || !(sta->ht_cap.ht_supported || sta->he_cap.has_he))
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
EXPORT_SYMBOL_GPL(mt7921_tx_check_aggr);

static bool
mt7921_mac_add_txs_skb(struct mt7921_dev *dev, struct mt76_wcid *wcid, int pid,
		       __le32 *txs_data)
{
	struct mt7921_sta *msta = container_of(wcid, struct mt7921_sta, wcid);
	struct mt76_sta_stats *stats = &msta->stats;
	struct ieee80211_supported_band *sband;
	struct mt76_dev *mdev = &dev->mt76;
	struct ieee80211_tx_info *info;
	struct rate_info rate = {};
	struct sk_buff_head list;
	u32 txrate, txs, mode;
	struct sk_buff *skb;
	bool cck = false;

	mt76_tx_status_lock(mdev, &list);
	skb = mt76_tx_status_skb_get(mdev, wcid, pid, &list);
	if (!skb)
		goto out;

	info = IEEE80211_SKB_CB(skb);
	txs = le32_to_cpu(txs_data[0]);
	if (!(txs & MT_TXS0_ACK_ERROR_MASK))
		info->flags |= IEEE80211_TX_STAT_ACK;

	info->status.ampdu_len = 1;
	info->status.ampdu_ack_len = !!(info->flags &
					IEEE80211_TX_STAT_ACK);

	info->status.rates[0].idx = -1;

	if (!wcid->sta)
		goto out;

	txrate = FIELD_GET(MT_TXS0_TX_RATE, txs);

	rate.mcs = FIELD_GET(MT_TX_RATE_IDX, txrate);
	rate.nss = FIELD_GET(MT_TX_RATE_NSS, txrate) + 1;

	if (rate.nss - 1 < ARRAY_SIZE(stats->tx_nss))
		stats->tx_nss[rate.nss - 1]++;
	if (rate.mcs < ARRAY_SIZE(stats->tx_mcs))
		stats->tx_mcs[rate.mcs]++;

	mode = FIELD_GET(MT_TX_RATE_MODE, txrate);
	switch (mode) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		fallthrough;
	case MT_PHY_TYPE_OFDM:
		if (dev->mphy.chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &dev->mphy.sband_5g.sband;
		else
			sband = &dev->mphy.sband_2g.sband;

		rate.mcs = mt76_get_rate(dev->mphy.dev, sband, rate.mcs, cck);
		rate.legacy = sband->bitrates[rate.mcs].bitrate;
		break;
	case MT_PHY_TYPE_HT:
	case MT_PHY_TYPE_HT_GF:
		if (rate.mcs > 31)
			goto out;

		rate.flags = RATE_INFO_FLAGS_MCS;
		if (wcid->rate.flags & RATE_INFO_FLAGS_SHORT_GI)
			rate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_VHT:
		if (rate.mcs > 9)
			goto out;

		rate.flags = RATE_INFO_FLAGS_VHT_MCS;
		break;
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
	case MT_PHY_TYPE_HE_MU:
		if (rate.mcs > 11)
			goto out;

		rate.he_gi = wcid->rate.he_gi;
		rate.he_dcm = FIELD_GET(MT_TX_RATE_DCM, txrate);
		rate.flags = RATE_INFO_FLAGS_HE_MCS;
		break;
	default:
		goto out;
	}
	stats->tx_mode[mode]++;

	switch (FIELD_GET(MT_TXS0_BW, txs)) {
	case IEEE80211_STA_RX_BW_160:
		rate.bw = RATE_INFO_BW_160;
		stats->tx_bw[3]++;
		break;
	case IEEE80211_STA_RX_BW_80:
		rate.bw = RATE_INFO_BW_80;
		stats->tx_bw[2]++;
		break;
	case IEEE80211_STA_RX_BW_40:
		rate.bw = RATE_INFO_BW_40;
		stats->tx_bw[1]++;
		break;
	default:
		rate.bw = RATE_INFO_BW_20;
		stats->tx_bw[0]++;
		break;
	}
	wcid->rate = rate;

out:
	if (skb)
		mt76_tx_status_skb_done(mdev, skb, &list);
	mt76_tx_status_unlock(mdev, &list);

	return !!skb;
}

void mt7921_mac_add_txs(struct mt7921_dev *dev, void *data)
{
	struct mt7921_sta *msta = NULL;
	struct mt76_wcid *wcid;
	__le32 *txs_data = data;
	u16 wcidx;
	u32 txs;
	u8 pid;

	txs = le32_to_cpu(txs_data[0]);
	if (FIELD_GET(MT_TXS0_TXS_FORMAT, txs) > 1)
		return;

	txs = le32_to_cpu(txs_data[2]);
	wcidx = FIELD_GET(MT_TXS2_WCID, txs);

	txs = le32_to_cpu(txs_data[3]);
	pid = FIELD_GET(MT_TXS3_PID, txs);

	if (pid < MT_PACKET_ID_FIRST)
		return;

	if (wcidx >= MT7921_WTBL_SIZE)
		return;

	rcu_read_lock();

	wcid = rcu_dereference(dev->mt76.wcid[wcidx]);
	if (!wcid)
		goto out;

	mt7921_mac_add_txs_skb(dev, wcid, pid, txs_data);

	if (!wcid->sta)
		goto out;

	msta = container_of(wcid, struct mt7921_sta, wcid);
	spin_lock_bh(&dev->sta_poll_lock);
	if (list_empty(&msta->poll_list))
		list_add_tail(&msta->poll_list, &dev->sta_poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

out:
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(mt7921_mac_add_txs);

void mt7921_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *end = (__le32 *)&skb->data[skb->len];
	enum rx_pkt_type type;
	u16 flag;

	type = FIELD_GET(MT_RXD0_PKT_TYPE, le32_to_cpu(rxd[0]));
	flag = FIELD_GET(MT_RXD0_PKT_FLAG, le32_to_cpu(rxd[0]));

	if (type == PKT_TYPE_RX_EVENT && flag == 0x1)
		type = PKT_TYPE_NORMAL_MCU;

	switch (type) {
	case PKT_TYPE_RX_EVENT:
		mt7921_mcu_rx_event(dev, skb);
		break;
	case PKT_TYPE_TXS:
		for (rxd += 2; rxd + 8 <= end; rxd += 8)
			mt7921_mac_add_txs(dev, rxd);
		dev_kfree_skb(skb);
		break;
	case PKT_TYPE_NORMAL_MCU:
	case PKT_TYPE_NORMAL:
		if (!mt7921_mac_fill_rx(dev, skb)) {
			mt76_rx(&dev->mt76, q, skb);
			return;
		}
		fallthrough;
	default:
		dev_kfree_skb(skb);
		break;
	}
}
EXPORT_SYMBOL_GPL(mt7921_queue_rx_skb);

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
	bool is_2ghz = phy->mt76->chandef.chan->band == NL80211_BAND_2GHZ;
	int sifs = is_2ghz ? 10 : 16, offset;

	if (!test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;

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

	if (phy->slottime < 20 || !is_2ghz)
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

void mt7921_update_channel(struct mt76_phy *mphy)
{
	struct mt7921_dev *dev = container_of(mphy->dev, struct mt7921_dev, mt76);

	if (mt76_connac_pm_wake(mphy, &dev->pm))
		return;

	mt7921_phy_update_channel(mphy, 0);
	/* reset obss airtime */
	mt76_set(dev, MT_WF_RMAC_MIB_TIME0(0), MT_WF_RMAC_MIB_RXTIME_CLR);

	mt76_connac_power_save_sched(mphy, &dev->pm);
}
EXPORT_SYMBOL_GPL(mt7921_update_channel);

static void
mt7921_vif_connect_iter(void *priv, u8 *mac,
			struct ieee80211_vif *vif)
{
	struct mt7921_vif *mvif = (struct mt7921_vif *)vif->drv_priv;
	struct mt7921_dev *dev = mvif->phy->dev;

	if (vif->type == NL80211_IFTYPE_STATION)
		ieee80211_disconnect(vif, true);

	mt76_connac_mcu_uni_add_dev(&dev->mphy, vif, &mvif->sta.wcid, true);
	mt7921_mcu_set_tx(dev, vif);
}

/* system error recovery */
void mt7921_mac_reset_work(struct work_struct *work)
{
	struct mt7921_dev *dev = container_of(work, struct mt7921_dev,
					      reset_work);
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct mt76_connac_pm *pm = &dev->pm;
	int i;

	dev_err(dev->mt76.dev, "chip reset\n");
	dev->hw_full_reset = true;
	ieee80211_stop_queues(hw);

	cancel_delayed_work_sync(&dev->mphy.mac_work);
	cancel_delayed_work_sync(&pm->ps_work);
	cancel_work_sync(&pm->wake_work);

	mutex_lock(&dev->mt76.mutex);
	for (i = 0; i < 10; i++)
		if (!mt7921_dev_reset(dev))
			break;
	mutex_unlock(&dev->mt76.mutex);

	if (i == 10)
		dev_err(dev->mt76.dev, "chip reset failed\n");

	if (test_and_clear_bit(MT76_HW_SCANNING, &dev->mphy.state)) {
		struct cfg80211_scan_info info = {
			.aborted = true,
		};

		ieee80211_scan_completed(dev->mphy.hw, &info);
	}

	dev->hw_full_reset = false;
	pm->suspended = false;
	ieee80211_wake_queues(hw);
	ieee80211_iterate_active_interfaces(hw,
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    mt7921_vif_connect_iter, NULL);
	mt76_connac_power_save_sched(&dev->mt76.phy, pm);
}

void mt7921_reset(struct mt76_dev *mdev)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);

	if (!dev->hw_init_done)
		return;

	if (dev->hw_full_reset)
		return;

	queue_work(dev->mt76.wq, &dev->reset_work);
}

void mt7921_mac_update_mib_stats(struct mt7921_phy *phy)
{
	struct mt7921_dev *dev = phy->dev;
	struct mib_stats *mib = &phy->mib;
	int i, aggr0 = 0, aggr1;
	u32 val;

	mib->fcs_err_cnt += mt76_get_field(dev, MT_MIB_SDR3(0),
					   MT_MIB_SDR3_FCS_ERR_MASK);
	mib->ack_fail_cnt += mt76_get_field(dev, MT_MIB_MB_BSDR3(0),
					    MT_MIB_ACK_FAIL_COUNT_MASK);
	mib->ba_miss_cnt += mt76_get_field(dev, MT_MIB_MB_BSDR2(0),
					   MT_MIB_BA_FAIL_COUNT_MASK);
	mib->rts_cnt += mt76_get_field(dev, MT_MIB_MB_BSDR0(0),
				       MT_MIB_RTS_COUNT_MASK);
	mib->rts_retries_cnt += mt76_get_field(dev, MT_MIB_MB_BSDR1(0),
					       MT_MIB_RTS_FAIL_COUNT_MASK);

	mib->tx_ampdu_cnt += mt76_rr(dev, MT_MIB_SDR12(0));
	mib->tx_mpdu_attempts_cnt += mt76_rr(dev, MT_MIB_SDR14(0));
	mib->tx_mpdu_success_cnt += mt76_rr(dev, MT_MIB_SDR15(0));

	val = mt76_rr(dev, MT_MIB_SDR32(0));
	mib->tx_pkt_ebf_cnt += FIELD_GET(MT_MIB_SDR9_EBF_CNT_MASK, val);
	mib->tx_pkt_ibf_cnt += FIELD_GET(MT_MIB_SDR9_IBF_CNT_MASK, val);

	val = mt76_rr(dev, MT_ETBF_TX_APP_CNT(0));
	mib->tx_bf_ibf_ppdu_cnt += FIELD_GET(MT_ETBF_TX_IBF_CNT, val);
	mib->tx_bf_ebf_ppdu_cnt += FIELD_GET(MT_ETBF_TX_EBF_CNT, val);

	val = mt76_rr(dev, MT_ETBF_RX_FB_CNT(0));
	mib->tx_bf_rx_fb_all_cnt += FIELD_GET(MT_ETBF_RX_FB_ALL, val);
	mib->tx_bf_rx_fb_he_cnt += FIELD_GET(MT_ETBF_RX_FB_HE, val);
	mib->tx_bf_rx_fb_vht_cnt += FIELD_GET(MT_ETBF_RX_FB_VHT, val);
	mib->tx_bf_rx_fb_ht_cnt += FIELD_GET(MT_ETBF_RX_FB_HT, val);

	mib->rx_mpdu_cnt += mt76_rr(dev, MT_MIB_SDR5(0));
	mib->rx_ampdu_cnt += mt76_rr(dev, MT_MIB_SDR22(0));
	mib->rx_ampdu_bytes_cnt += mt76_rr(dev, MT_MIB_SDR23(0));
	mib->rx_ba_cnt += mt76_rr(dev, MT_MIB_SDR31(0));

	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++) {
		val = mt76_rr(dev, MT_PLE_AMSDU_PACK_MSDU_CNT(i));
		mib->tx_amsdu[i] += val;
		mib->tx_amsdu_cnt += val;
	}

	for (i = 0, aggr1 = aggr0 + 4; i < 4; i++) {
		u32 val2;

		val = mt76_rr(dev, MT_TX_AGG_CNT(0, i));
		val2 = mt76_rr(dev, MT_TX_AGG_CNT2(0, i));

		dev->mt76.aggr_stats[aggr0++] += val & 0xffff;
		dev->mt76.aggr_stats[aggr0++] += val >> 16;
		dev->mt76.aggr_stats[aggr1++] += val2 & 0xffff;
		dev->mt76.aggr_stats[aggr1++] += val2 >> 16;
	}
}

void mt7921_mac_work(struct work_struct *work)
{
	struct mt7921_phy *phy;
	struct mt76_phy *mphy;

	mphy = (struct mt76_phy *)container_of(work, struct mt76_phy,
					       mac_work.work);
	phy = mphy->priv;

	mt7921_mutex_acquire(phy->dev);

	mt76_update_survey(mphy);
	if (++mphy->mac_work_count == 2) {
		mphy->mac_work_count = 0;

		mt7921_mac_update_mib_stats(phy);
	}

	mt7921_mutex_release(phy->dev);

	mt76_tx_status_check(mphy->dev, false);
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

	if (!mt7921_mcu_drv_pmctrl(dev)) {
		struct mt76_dev *mdev = &dev->mt76;
		int i;

		if (mt76_is_sdio(mdev)) {
			mt76_connac_pm_dequeue_skbs(mphy, &dev->pm);
			mt76_worker_schedule(&mdev->sdio.txrx_worker);
		} else {
			mt76_for_each_q_rx(mdev, i)
				napi_schedule(&mdev->napi[i]);
			mt76_connac_pm_dequeue_skbs(mphy, &dev->pm);
			mt7921_mcu_tx_cleanup(dev);
		}
		if (test_bit(MT76_STATE_RUNNING, &mphy->state))
			ieee80211_queue_delayed_work(mphy->hw, &mphy->mac_work,
						     MT7921_WATCHDOG_TIME);
	}

	ieee80211_wake_queues(mphy->hw);
	wake_up(&dev->pm.wait);
}

void mt7921_pm_power_save_work(struct work_struct *work)
{
	struct mt7921_dev *dev;
	unsigned long delta;
	struct mt76_phy *mphy;

	dev = (struct mt7921_dev *)container_of(work, struct mt7921_dev,
						pm.ps_work.work);
	mphy = dev->phy.mt76;

	delta = dev->pm.idle_timeout;
	if (test_bit(MT76_HW_SCANNING, &mphy->state) ||
	    test_bit(MT76_HW_SCHED_SCANNING, &mphy->state) ||
	    dev->fw_assert)
		goto out;

	if (mutex_is_locked(&dev->mt76.mutex))
		/* if mt76 mutex is held we should not put the device
		 * to sleep since we are currently accessing device
		 * register map. We need to wait for the next power_save
		 * trigger.
		 */
		goto out;

	if (time_is_after_jiffies(dev->pm.last_activity + delta)) {
		delta = dev->pm.last_activity + delta - jiffies;
		goto out;
	}

	if (!mt7921_mcu_fw_pmctrl(dev)) {
		cancel_delayed_work_sync(&mphy->mac_work);
		return;
	}
out:
	queue_delayed_work(dev->mt76.wq, &dev->pm.ps_work, delta);
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
		if (!dump || data + skb->len - dump > MT76_CONNAC_COREDUMP_SZ) {
			dev_kfree_skb(skb);
			continue;
		}

		memcpy(data, skb->data, skb->len);
		data += skb->len;

		dev_kfree_skb(skb);
	}

	if (dump)
		dev_coredumpv(dev->mt76.dev, dump, MT76_CONNAC_COREDUMP_SZ,
			      GFP_KERNEL);

	mt7921_reset(&dev->mt76);
}
