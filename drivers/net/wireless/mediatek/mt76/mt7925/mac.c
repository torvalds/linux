// SPDX-License-Identifier: ISC
/* Copyright (C) 2023 MediaTek Inc. */

#include <linux/devcoredump.h>
#include <linux/etherdevice.h>
#include <linux/timekeeping.h>
#include "mt7925.h"
#include "../dma.h"
#include "mac.h"
#include "mcu.h"

bool mt7925_mac_wtbl_update(struct mt792x_dev *dev, int idx, u32 mask)
{
	mt76_rmw(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_WLAN_IDX,
		 FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, idx) | mask);

	return mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY,
			 0, 5000);
}

static void mt7925_mac_sta_poll(struct mt792x_dev *dev)
{
	static const u8 ac_to_tid[] = {
		[IEEE80211_AC_BE] = 0,
		[IEEE80211_AC_BK] = 1,
		[IEEE80211_AC_VI] = 4,
		[IEEE80211_AC_VO] = 6
	};
	struct ieee80211_sta *sta;
	struct mt792x_sta *msta;
	struct mt792x_link_sta *mlink;
	u32 tx_time[IEEE80211_NUM_ACS], rx_time[IEEE80211_NUM_ACS];
	LIST_HEAD(sta_poll_list);
	struct rate_info *rate;
	s8 rssi[4];
	int i;

	spin_lock_bh(&dev->mt76.sta_poll_lock);
	list_splice_init(&dev->mt76.sta_poll_list, &sta_poll_list);
	spin_unlock_bh(&dev->mt76.sta_poll_lock);

	while (true) {
		bool clear = false;
		u32 addr, val;
		u16 idx;
		u8 bw;

		if (list_empty(&sta_poll_list))
			break;
		mlink = list_first_entry(&sta_poll_list,
					 struct mt792x_link_sta, wcid.poll_list);
		msta = mlink->sta;
		spin_lock_bh(&dev->mt76.sta_poll_lock);
		list_del_init(&mlink->wcid.poll_list);
		spin_unlock_bh(&dev->mt76.sta_poll_lock);

		idx = mlink->wcid.idx;
		addr = mt7925_mac_wtbl_lmac_addr(dev, idx, MT_WTBL_AC0_CTT_OFFSET);

		for (i = 0; i < IEEE80211_NUM_ACS; i++) {
			u32 tx_last = mlink->airtime_ac[i];
			u32 rx_last = mlink->airtime_ac[i + 4];

			mlink->airtime_ac[i] = mt76_rr(dev, addr);
			mlink->airtime_ac[i + 4] = mt76_rr(dev, addr + 4);

			tx_time[i] = mlink->airtime_ac[i] - tx_last;
			rx_time[i] = mlink->airtime_ac[i + 4] - rx_last;

			if ((tx_last | rx_last) & BIT(30))
				clear = true;

			addr += 8;
		}

		if (clear) {
			mt7925_mac_wtbl_update(dev, idx,
					       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
			memset(mlink->airtime_ac, 0, sizeof(mlink->airtime_ac));
		}

		if (!mlink->wcid.sta)
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
		rate = &mlink->wcid.rate;

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

		addr = mt7925_mac_wtbl_lmac_addr(dev, idx, 6);
		val = mt76_rr(dev, addr);
		if (rate->flags & RATE_INFO_FLAGS_EHT_MCS) {
			addr = mt7925_mac_wtbl_lmac_addr(dev, idx, 5);
			val = mt76_rr(dev, addr);
			rate->eht_gi = FIELD_GET(GENMASK(25, 24), val);
		} else if (rate->flags & RATE_INFO_FLAGS_HE_MCS) {
			u8 offs = MT_WTBL_TXRX_RATE_G2_HE + 2 * bw;

			rate->he_gi = (val & (0x3 << offs)) >> offs;
		} else if (rate->flags &
			   (RATE_INFO_FLAGS_VHT_MCS | RATE_INFO_FLAGS_MCS)) {
			if (val & BIT(MT_WTBL_TXRX_RATE_G2 + bw))
				rate->flags |= RATE_INFO_FLAGS_SHORT_GI;
			else
				rate->flags &= ~RATE_INFO_FLAGS_SHORT_GI;
		}

		/* get signal strength of resp frames (CTS/BA/ACK) */
		addr = mt7925_mac_wtbl_lmac_addr(dev, idx, 34);
		val = mt76_rr(dev, addr);

		rssi[0] = to_rssi(GENMASK(7, 0), val);
		rssi[1] = to_rssi(GENMASK(15, 8), val);
		rssi[2] = to_rssi(GENMASK(23, 16), val);
		rssi[3] = to_rssi(GENMASK(31, 14), val);

		mlink->ack_signal =
			mt76_rx_signal(msta->vif->phy->mt76->antenna_mask, rssi);

		ewma_avg_signal_add(&mlink->avg_ack_signal, -mlink->ack_signal);
	}
}

void mt7925_mac_set_fixed_rate_table(struct mt792x_dev *dev,
				     u8 tbl_idx, u16 rate_idx)
{
	u32 ctrl = MT_WTBL_ITCR_WR | MT_WTBL_ITCR_EXEC | tbl_idx;

	mt76_wr(dev, MT_WTBL_ITDR0, rate_idx);
	/* use wtbl spe idx */
	mt76_wr(dev, MT_WTBL_ITDR1, MT_WTBL_SPE_IDX_SEL);
	mt76_wr(dev, MT_WTBL_ITCR, ctrl);
}

/* The HW does not translate the mac header to 802.3 for mesh point */
static int mt7925_reverse_frag0_hdr_trans(struct sk_buff *skb, u16 hdr_gap)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct ethhdr *eth_hdr = (struct ethhdr *)(skb->data + hdr_gap);
	struct mt792x_sta *msta = (struct mt792x_sta *)status->wcid;
	__le32 *rxd = (__le32 *)skb->data;
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif;
	struct ieee80211_hdr hdr;
	u16 frame_control;

	if (le32_get_bits(rxd[3], MT_RXD3_NORMAL_ADDR_TYPE) !=
	    MT_RXD3_NORMAL_U2M)
		return -EINVAL;

	if (!(le32_to_cpu(rxd[1]) & MT_RXD1_NORMAL_GROUP_4))
		return -EINVAL;

	if (!msta || !msta->vif)
		return -EINVAL;

	sta = container_of((void *)msta, struct ieee80211_sta, drv_priv);
	vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);

	/* store the info from RXD and ethhdr to avoid being overridden */
	frame_control = le32_get_bits(rxd[8], MT_RXD8_FRAME_CONTROL);
	hdr.frame_control = cpu_to_le16(frame_control);
	hdr.seq_ctrl = cpu_to_le16(le32_get_bits(rxd[10], MT_RXD10_SEQ_CTRL));
	hdr.duration_id = 0;

	ether_addr_copy(hdr.addr1, vif->addr);
	ether_addr_copy(hdr.addr2, sta->addr);
	switch (frame_control & (IEEE80211_FCTL_TODS |
				 IEEE80211_FCTL_FROMDS)) {
	case 0:
		ether_addr_copy(hdr.addr3, vif->bss_conf.bssid);
		break;
	case IEEE80211_FCTL_FROMDS:
		ether_addr_copy(hdr.addr3, eth_hdr->h_source);
		break;
	case IEEE80211_FCTL_TODS:
		ether_addr_copy(hdr.addr3, eth_hdr->h_dest);
		break;
	case IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS:
		ether_addr_copy(hdr.addr3, eth_hdr->h_dest);
		ether_addr_copy(hdr.addr4, eth_hdr->h_source);
		break;
	default:
		break;
	}

	skb_pull(skb, hdr_gap + sizeof(struct ethhdr) - 2);
	if (eth_hdr->h_proto == cpu_to_be16(ETH_P_AARP) ||
	    eth_hdr->h_proto == cpu_to_be16(ETH_P_IPX))
		ether_addr_copy(skb_push(skb, ETH_ALEN), bridge_tunnel_header);
	else if (be16_to_cpu(eth_hdr->h_proto) >= ETH_P_802_3_MIN)
		ether_addr_copy(skb_push(skb, ETH_ALEN), rfc1042_header);
	else
		skb_pull(skb, 2);

	if (ieee80211_has_order(hdr.frame_control))
		memcpy(skb_push(skb, IEEE80211_HT_CTL_LEN), &rxd[11],
		       IEEE80211_HT_CTL_LEN);
	if (ieee80211_is_data_qos(hdr.frame_control)) {
		__le16 qos_ctrl;

		qos_ctrl = cpu_to_le16(le32_get_bits(rxd[10], MT_RXD10_QOS_CTL));
		memcpy(skb_push(skb, IEEE80211_QOS_CTL_LEN), &qos_ctrl,
		       IEEE80211_QOS_CTL_LEN);
	}

	if (ieee80211_has_a4(hdr.frame_control))
		memcpy(skb_push(skb, sizeof(hdr)), &hdr, sizeof(hdr));
	else
		memcpy(skb_push(skb, sizeof(hdr) - 6), &hdr, sizeof(hdr) - 6);

	return 0;
}

static int
mt7925_mac_fill_rx_rate(struct mt792x_dev *dev,
			struct mt76_rx_status *status,
			struct ieee80211_supported_band *sband,
			__le32 *rxv, u8 *mode)
{
	u32 v0, v2;
	u8 stbc, gi, bw, dcm, nss;
	int i, idx;
	bool cck = false;

	v0 = le32_to_cpu(rxv[0]);
	v2 = le32_to_cpu(rxv[2]);

	idx = FIELD_GET(MT_PRXV_TX_RATE, v0);
	i = idx;
	nss = FIELD_GET(MT_PRXV_NSTS, v0) + 1;

	stbc = FIELD_GET(MT_PRXV_HT_STBC, v2);
	gi = FIELD_GET(MT_PRXV_HT_SHORT_GI, v2);
	*mode = FIELD_GET(MT_PRXV_TX_MODE, v2);
	dcm = FIELD_GET(MT_PRXV_DCM, v2);
	bw = FIELD_GET(MT_PRXV_FRAME_MODE, v2);

	switch (*mode) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		fallthrough;
	case MT_PHY_TYPE_OFDM:
		i = mt76_get_rate(&dev->mt76, sband, i, cck);
		break;
	case MT_PHY_TYPE_HT_GF:
	case MT_PHY_TYPE_HT:
		status->encoding = RX_ENC_HT;
		if (gi)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		if (i > 31)
			return -EINVAL;
		break;
	case MT_PHY_TYPE_VHT:
		status->nss = nss;
		status->encoding = RX_ENC_VHT;
		if (gi)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		if (i > 11)
			return -EINVAL;
		break;
	case MT_PHY_TYPE_HE_MU:
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
		status->nss = nss;
		status->encoding = RX_ENC_HE;
		i &= GENMASK(3, 0);

		if (gi <= NL80211_RATE_INFO_HE_GI_3_2)
			status->he_gi = gi;

		status->he_dcm = dcm;
		break;
	case MT_PHY_TYPE_EHT_SU:
	case MT_PHY_TYPE_EHT_TRIG:
	case MT_PHY_TYPE_EHT_MU:
		status->nss = nss;
		status->encoding = RX_ENC_EHT;
		i &= GENMASK(3, 0);

		if (gi <= NL80211_RATE_INFO_EHT_GI_3_2)
			status->eht.gi = gi;
		break;
	default:
		return -EINVAL;
	}
	status->rate_idx = i;

	switch (bw) {
	case IEEE80211_STA_RX_BW_20:
		break;
	case IEEE80211_STA_RX_BW_40:
		if (*mode & MT_PHY_TYPE_HE_EXT_SU &&
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
	if (*mode < MT_PHY_TYPE_HE_SU && gi)
		status->enc_flags |= RX_ENC_FLAG_SHORT_GI;

	return 0;
}

static int
mt7925_mac_fill_rx(struct mt792x_dev *dev, struct sk_buff *skb)
{
	u32 csum_mask = MT_RXD3_NORMAL_IP_SUM | MT_RXD3_NORMAL_UDP_TCP_SUM;
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	bool hdr_trans, unicast, insert_ccmp_hdr = false;
	u8 chfreq, qos_ctl = 0, remove_pad, amsdu_info;
	u16 hdr_gap;
	__le32 *rxv = NULL, *rxd = (__le32 *)skb->data;
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt792x_phy *phy = &dev->phy;
	struct ieee80211_supported_band *sband;
	u32 csum_status = *(u32 *)skb->cb;
	u32 rxd1 = le32_to_cpu(rxd[1]);
	u32 rxd2 = le32_to_cpu(rxd[2]);
	u32 rxd3 = le32_to_cpu(rxd[3]);
	u32 rxd4 = le32_to_cpu(rxd[4]);
	struct mt792x_link_sta *mlink;
	u8 mode = 0; /* , band_idx; */
	u16 seq_ctrl = 0;
	__le16 fc = 0;
	int idx;

	memset(status, 0, sizeof(*status));

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
	status->wcid = mt792x_rx_get_wcid(dev, idx, unicast);

	if (status->wcid) {
		mlink = container_of(status->wcid, struct mt792x_link_sta, wcid);
		mt76_wcid_add_poll(&dev->mt76, &mlink->wcid);
	}

	mt792x_get_status_freq_info(status, chfreq);

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

	if (mt76_is_mmio(&dev->mt76) && (rxd3 & csum_mask) == csum_mask &&
	    !(csum_status & (BIT(0) | BIT(2) | BIT(3))))
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (rxd3 & MT_RXD3_NORMAL_FCS_ERR)
		status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (rxd1 & MT_RXD1_NORMAL_TKIP_MIC_ERR)
		status->flag |= RX_FLAG_MMIC_ERROR;

	if (FIELD_GET(MT_RXD2_NORMAL_SEC_MODE, rxd2) != 0 &&
	    !(rxd1 & (MT_RXD1_NORMAL_CLM | MT_RXD1_NORMAL_CM))) {
		status->flag |= RX_FLAG_DECRYPTED;
		status->flag |= RX_FLAG_IV_STRIPPED;
		status->flag |= RX_FLAG_MMIC_STRIPPED | RX_FLAG_MIC_STRIPPED;
	}

	remove_pad = FIELD_GET(MT_RXD2_NORMAL_HDR_OFFSET, rxd2);

	if (rxd2 & MT_RXD2_NORMAL_MAX_LEN_ERROR)
		return -EINVAL;

	rxd += 8;
	if (rxd1 & MT_RXD1_NORMAL_GROUP_4) {
		u32 v0 = le32_to_cpu(rxd[0]);
		u32 v2 = le32_to_cpu(rxd[2]);

		/* TODO: need to map rxd address */
		fc = cpu_to_le16(FIELD_GET(MT_RXD8_FRAME_CONTROL, v0));
		seq_ctrl = FIELD_GET(MT_RXD10_SEQ_CTRL, v2);
		qos_ctl = FIELD_GET(MT_RXD10_QOS_CTL, v2);

		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd1 & MT_RXD1_NORMAL_GROUP_1) {
		u8 *data = (u8 *)rxd;

		if (status->flag & RX_FLAG_DECRYPTED) {
			switch (FIELD_GET(MT_RXD2_NORMAL_SEC_MODE, rxd2)) {
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

		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	/* RXD Group 3 - P-RXV */
	if (rxd1 & MT_RXD1_NORMAL_GROUP_3) {
		u32 v3;
		int ret;

		rxv = rxd;
		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;

		v3 = le32_to_cpu(rxv[3]);

		status->chains = mphy->antenna_mask;
		status->chain_signal[0] = to_rssi(MT_PRXV_RCPI0, v3);
		status->chain_signal[1] = to_rssi(MT_PRXV_RCPI1, v3);
		status->chain_signal[2] = to_rssi(MT_PRXV_RCPI2, v3);
		status->chain_signal[3] = to_rssi(MT_PRXV_RCPI3, v3);

		/* RXD Group 5 - C-RXV */
		if (rxd1 & MT_RXD1_NORMAL_GROUP_5) {
			rxd += 24;
			if ((u8 *)rxd - skb->data >= skb->len)
				return -EINVAL;
		}

		ret = mt7925_mac_fill_rx_rate(dev, status, sband, rxv, &mode);
		if (ret < 0)
			return ret;
	}

	amsdu_info = FIELD_GET(MT_RXD4_NORMAL_PAYLOAD_FORMAT, rxd4);
	status->amsdu = !!amsdu_info;
	if (status->amsdu) {
		status->first_amsdu = amsdu_info == MT_RXD4_FIRST_AMSDU_FRAME;
		status->last_amsdu = amsdu_info == MT_RXD4_LAST_AMSDU_FRAME;
	}

	hdr_gap = (u8 *)rxd - skb->data + 2 * remove_pad;
	if (hdr_trans && ieee80211_has_morefrags(fc)) {
		if (mt7925_reverse_frag0_hdr_trans(skb, hdr_gap))
			return -EINVAL;
		hdr_trans = false;
	} else {
		int pad_start = 0;

		skb_pull(skb, hdr_gap);
		if (!hdr_trans && status->amsdu) {
			pad_start = ieee80211_get_hdrlen_from_skb(skb);
		} else if (hdr_trans && (rxd2 & MT_RXD2_NORMAL_HDR_TRANS_ERROR)) {
			/* When header translation failure is indicated,
			 * the hardware will insert an extra 2-byte field
			 * containing the data length after the protocol
			 * type field.
			 */
			pad_start = 12;
			if (get_unaligned_be16(skb->data + pad_start) == ETH_P_8021Q)
				pad_start += 4;
			else
				pad_start = 0;
		}

		if (pad_start) {
			memmove(skb->data + 2, skb->data, pad_start);
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
		skb_set_mac_header(skb, (unsigned char *)hdr - skb->data);
	} else {
		status->flag |= RX_FLAG_8023;
	}

	mt792x_mac_assoc_rssi(dev, skb);

	if (rxv && !(status->flag & RX_FLAG_8023)) {
		switch (status->encoding) {
		case RX_ENC_EHT:
			mt76_connac3_mac_decode_eht_radiotap(skb, rxv, mode);
			break;
		case RX_ENC_HE:
			mt76_connac3_mac_decode_he_radiotap(skb, rxv, mode);
			break;
		default:
			break;
		}
	}

	if (!status->wcid || !ieee80211_is_data_qos(fc))
		return 0;

	status->aggr = unicast && !ieee80211_is_qos_nullfunc(fc);
	status->seqno = IEEE80211_SEQ_TO_SN(seq_ctrl);
	status->qos_ctl = qos_ctl;

	return 0;
}

static void
mt7925_mac_write_txwi_8023(__le32 *txwi, struct sk_buff *skb,
			   struct mt76_wcid *wcid)
{
	u8 tid = skb->priority & IEEE80211_QOS_CTL_TID_MASK;
	u8 fc_type, fc_stype;
	u16 ethertype;
	bool wmm = false;
	u32 val;

	if (wcid->sta) {
		struct ieee80211_sta *sta;

		sta = container_of((void *)wcid, struct ieee80211_sta, drv_priv);
		wmm = sta->wme;
	}

	val = FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_3) |
	      FIELD_PREP(MT_TXD1_TID, tid);

	ethertype = get_unaligned_be16(&skb->data[12]);
	if (ethertype >= ETH_P_802_3_MIN)
		val |= MT_TXD1_ETH_802_3;

	txwi[1] |= cpu_to_le32(val);

	fc_type = IEEE80211_FTYPE_DATA >> 2;
	fc_stype = wmm ? IEEE80211_STYPE_QOS_DATA >> 4 : 0;

	val = FIELD_PREP(MT_TXD2_FRAME_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD2_SUB_TYPE, fc_stype);

	txwi[2] |= cpu_to_le32(val);
}

static void
mt7925_mac_write_txwi_80211(struct mt76_dev *dev, __le32 *txwi,
			    struct sk_buff *skb,
			    struct ieee80211_key_conf *key)
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
	    mgmt->u.action.u.addba_req.action_code == WLAN_ACTION_ADDBA_REQ)
		tid = MT_TX_ADDBA;
	else if (ieee80211_is_mgmt(hdr->frame_control))
		tid = MT_TX_NORMAL;

	val = FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_11) |
	      FIELD_PREP(MT_TXD1_HDR_INFO,
			 ieee80211_get_hdrlen_from_skb(skb) / 2) |
	      FIELD_PREP(MT_TXD1_TID, tid);

	if (!ieee80211_is_data(fc) || multicast ||
	    info->flags & IEEE80211_TX_CTL_USE_MINRATE)
		val |= MT_TXD1_FIXED_RATE;

	if (key && multicast && ieee80211_is_robust_mgmt_frame(skb) &&
	    key->cipher == WLAN_CIPHER_SUITE_AES_CMAC) {
		val |= MT_TXD1_BIP;
		txwi[3] &= ~cpu_to_le32(MT_TXD3_PROTECT_FRAME);
	}

	txwi[1] |= cpu_to_le32(val);

	fc_type = (le16_to_cpu(fc) & IEEE80211_FCTL_FTYPE) >> 2;
	fc_stype = (le16_to_cpu(fc) & IEEE80211_FCTL_STYPE) >> 4;

	val = FIELD_PREP(MT_TXD2_FRAME_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD2_SUB_TYPE, fc_stype);

	txwi[2] |= cpu_to_le32(val);

	txwi[3] |= cpu_to_le32(FIELD_PREP(MT_TXD3_BCM, multicast));
	if (ieee80211_is_beacon(fc))
		txwi[3] |= cpu_to_le32(MT_TXD3_REM_TX_COUNT);

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
		txwi[3] &= ~cpu_to_le32(MT_TXD3_HW_AMSDU);
	}
}

void
mt7925_mac_write_txwi(struct mt76_dev *dev, __le32 *txwi,
		      struct sk_buff *skb, struct mt76_wcid *wcid,
		      struct ieee80211_key_conf *key, int pid,
		      enum mt76_txq_id qid, u32 changed)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	u8 p_fmt, q_idx, omac_idx = 0, wmm_idx = 0, band_idx = 0;
	u32 val, sz_txd = mt76_is_mmio(dev) ? MT_TXD_SIZE : MT_SDIO_TXD_SIZE;
	bool is_8023 = info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP;
	struct mt76_vif_link *mvif;
	bool beacon = !!(changed & (BSS_CHANGED_BEACON |
				    BSS_CHANGED_BEACON_ENABLED));
	bool inband_disc = !!(changed & (BSS_CHANGED_UNSOL_BCAST_PROBE_RESP |
					 BSS_CHANGED_FILS_DISCOVERY));
	struct mt792x_bss_conf *mconf;

	mconf = vif ? mt792x_vif_to_link((struct mt792x_vif *)vif->drv_priv,
					 wcid->link_id) : NULL;
	mvif = mconf ? (struct mt76_vif_link *)&mconf->mt76 : NULL;

	if (mvif) {
		omac_idx = mvif->omac_idx;
		wmm_idx = mvif->wmm_idx;
		band_idx = mvif->band_idx;
	}

	if (inband_disc) {
		p_fmt = MT_TX_TYPE_FW;
		q_idx = MT_LMAC_ALTX0;
	} else if (beacon) {
		p_fmt = MT_TX_TYPE_FW;
		q_idx = MT_LMAC_BCN0;
	} else if (qid >= MT_TXQ_PSD) {
		p_fmt = mt76_is_mmio(dev) ? MT_TX_TYPE_CT : MT_TX_TYPE_SF;
		q_idx = MT_LMAC_ALTX0;
	} else {
		p_fmt = mt76_is_mmio(dev) ? MT_TX_TYPE_CT : MT_TX_TYPE_SF;
		q_idx = wmm_idx * MT76_CONNAC_MAX_WMM_SETS +
			mt76_connac_lmac_mapping(skb_get_queue_mapping(skb));

		/* counting non-offloading skbs */
		wcid->stats.tx_bytes += skb->len;
		wcid->stats.tx_packets++;
	}

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len + sz_txd) |
	      FIELD_PREP(MT_TXD0_PKT_FMT, p_fmt) |
	      FIELD_PREP(MT_TXD0_Q_IDX, q_idx);
	txwi[0] = cpu_to_le32(val);

	val = FIELD_PREP(MT_TXD1_WLAN_IDX, wcid->idx) |
	      FIELD_PREP(MT_TXD1_OWN_MAC, omac_idx);

	if (band_idx)
		val |= FIELD_PREP(MT_TXD1_TGID, band_idx);

	txwi[1] = cpu_to_le32(val);
	txwi[2] = 0;

	val = FIELD_PREP(MT_TXD3_REM_TX_COUNT, 15);

	if (key)
		val |= MT_TXD3_PROTECT_FRAME;
	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		val |= MT_TXD3_NO_ACK;
	if (wcid->amsdu)
		val |= MT_TXD3_HW_AMSDU;

	txwi[3] = cpu_to_le32(val);
	txwi[4] = 0;

	val = FIELD_PREP(MT_TXD5_PID, pid);
	if (pid >= MT_PACKET_ID_FIRST) {
		val |= MT_TXD5_TX_STATUS_HOST;
		txwi[3] |= cpu_to_le32(MT_TXD3_BA_DISABLE);
		txwi[3] &= ~cpu_to_le32(MT_TXD3_HW_AMSDU);
	}

	txwi[5] = cpu_to_le32(val);

	val = MT_TXD6_DAS | FIELD_PREP(MT_TXD6_MSDU_CNT, 1);
	if (!ieee80211_vif_is_mld(vif) ||
	    (q_idx >= MT_LMAC_ALTX0 && q_idx <= MT_LMAC_BCN0))
		val |= MT_TXD6_DIS_MAT;
	txwi[6] = cpu_to_le32(val);
	txwi[7] = 0;

	if (is_8023)
		mt7925_mac_write_txwi_8023(txwi, skb, wcid);
	else
		mt7925_mac_write_txwi_80211(dev, txwi, skb, key);

	if (txwi[1] & cpu_to_le32(MT_TXD1_FIXED_RATE)) {
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
		bool mcast = ieee80211_is_data(hdr->frame_control) &&
			     is_multicast_ether_addr(hdr->addr1);
		u8 idx = MT792x_BASIC_RATES_TBL;

		if (mvif) {
			if (mcast && mvif->mcast_rates_idx)
				idx = mvif->mcast_rates_idx;
			else if (beacon && mvif->beacon_rates_idx)
				idx = mvif->beacon_rates_idx;
			else
				idx = mvif->basic_rates_idx;
		}

		txwi[6] |= cpu_to_le32(FIELD_PREP(MT_TXD6_TX_RATE, idx));
		txwi[3] |= cpu_to_le32(MT_TXD3_BA_DISABLE);
	}
}
EXPORT_SYMBOL_GPL(mt7925_mac_write_txwi);

static void mt7925_tx_check_aggr(struct ieee80211_sta *sta, struct sk_buff *skb,
				 struct mt76_wcid *wcid)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_link_sta *link_sta;
	struct mt792x_link_sta *mlink;
	struct mt792x_sta *msta;
	bool is_8023;
	u16 fc, tid;

	link_sta = rcu_dereference(sta->link[wcid->link_id]);
	if (!link_sta)
		return;

	if (!sta || !(link_sta->ht_cap.ht_supported || link_sta->he_cap.has_he))
		return;

	tid = skb->priority & IEEE80211_QOS_CTL_TID_MASK;
	is_8023 = info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP;

	if (is_8023) {
		fc = IEEE80211_FTYPE_DATA |
		     (sta->wme ? IEEE80211_STYPE_QOS_DATA :
		      IEEE80211_STYPE_DATA);
	} else {
		/* No need to get precise TID for Action/Management Frame,
		 * since it will not meet the following Frame Control
		 * condition anyway.
		 */

		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

		fc = le16_to_cpu(hdr->frame_control) &
		     (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE);
	}

	if (unlikely(fc != (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_DATA)))
		return;

	msta = (struct mt792x_sta *)sta->drv_priv;

	if (sta->mlo && msta->deflink_id != IEEE80211_LINK_UNSPECIFIED)
		mlink = rcu_dereference(msta->link[msta->deflink_id]);
	else
		mlink = &msta->deflink;

	if (!test_and_set_bit(tid, &mlink->wcid.ampdu_state))
		ieee80211_start_tx_ba_session(sta, tid, 0);
}

static bool
mt7925_mac_add_txs_skb(struct mt792x_dev *dev, struct mt76_wcid *wcid,
		       int pid, __le32 *txs_data)
{
	struct mt76_sta_stats *stats = &wcid->stats;
	struct ieee80211_supported_band *sband;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_phy *mphy;
	struct ieee80211_tx_info *info;
	struct sk_buff_head list;
	struct rate_info rate = {};
	struct sk_buff *skb;
	bool cck = false;
	u32 txrate, txs, mode, stbc;

	mt76_tx_status_lock(mdev, &list);
	skb = mt76_tx_status_skb_get(mdev, wcid, pid, &list);
	if (!skb)
		goto out_no_skb;

	txs = le32_to_cpu(txs_data[0]);

	info = IEEE80211_SKB_CB(skb);
	if (!(txs & MT_TXS0_ACK_ERROR_MASK))
		info->flags |= IEEE80211_TX_STAT_ACK;

	info->status.ampdu_len = 1;
	info->status.ampdu_ack_len = !!(info->flags &
					IEEE80211_TX_STAT_ACK);

	info->status.rates[0].idx = -1;

	txrate = FIELD_GET(MT_TXS0_TX_RATE, txs);

	rate.mcs = FIELD_GET(MT_TX_RATE_IDX, txrate);
	rate.nss = FIELD_GET(MT_TX_RATE_NSS, txrate) + 1;
	stbc = le32_get_bits(txs_data[3], MT_TXS3_RATE_STBC);

	if (stbc && rate.nss > 1)
		rate.nss >>= 1;

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
		mphy = mt76_dev_phy(mdev, wcid->phy_idx);

		if (mphy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &mphy->sband_5g.sband;
		else if (mphy->chandef.chan->band == NL80211_BAND_6GHZ)
			sband = &mphy->sband_6g.sband;
		else
			sband = &mphy->sband_2g.sband;

		rate.mcs = mt76_get_rate(mphy->dev, sband, rate.mcs, cck);
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
	case MT_PHY_TYPE_EHT_SU:
	case MT_PHY_TYPE_EHT_TRIG:
	case MT_PHY_TYPE_EHT_MU:
		if (rate.mcs > 13)
			goto out;

		rate.eht_gi = wcid->rate.eht_gi;
		rate.flags = RATE_INFO_FLAGS_EHT_MCS;
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
	mt76_tx_status_skb_done(mdev, skb, &list);

out_no_skb:
	mt76_tx_status_unlock(mdev, &list);

	return !!skb;
}

void mt7925_mac_add_txs(struct mt792x_dev *dev, void *data)
{
	struct mt792x_link_sta *mlink = NULL;
	struct mt76_wcid *wcid;
	__le32 *txs_data = data;
	u16 wcidx;
	u8 pid;

	if (le32_get_bits(txs_data[0], MT_TXS0_TXS_FORMAT) > 1)
		return;

	wcidx = le32_get_bits(txs_data[2], MT_TXS2_WCID);
	pid = le32_get_bits(txs_data[3], MT_TXS3_PID);

	if (pid < MT_PACKET_ID_FIRST)
		return;

	if (wcidx >= MT792x_WTBL_SIZE)
		return;

	rcu_read_lock();

	wcid = mt76_wcid_ptr(dev, wcidx);
	if (!wcid)
		goto out;

	mlink = container_of(wcid, struct mt792x_link_sta, wcid);

	mt7925_mac_add_txs_skb(dev, wcid, pid, txs_data);
	if (!wcid->sta)
		goto out;

	mt76_wcid_add_poll(&dev->mt76, &mlink->wcid);

out:
	rcu_read_unlock();
}

void mt7925_txwi_free(struct mt792x_dev *dev, struct mt76_txwi_cache *t,
		      struct ieee80211_sta *sta, struct mt76_wcid *wcid,
		      struct list_head *free_list)
{
	struct mt76_dev *mdev = &dev->mt76;
	__le32 *txwi;
	u16 wcid_idx;

	mt76_connac_txp_skb_unmap(mdev, t);
	if (!t->skb)
		goto out;

	txwi = (__le32 *)mt76_get_txwi_ptr(mdev, t);
	if (sta) {
		if (likely(t->skb->protocol != cpu_to_be16(ETH_P_PAE)))
			mt7925_tx_check_aggr(sta, t->skb, wcid);

		wcid_idx = wcid->idx;
	} else {
		wcid_idx = le32_get_bits(txwi[1], MT_TXD1_WLAN_IDX);
	}

	__mt76_tx_complete_skb(mdev, wcid_idx, t->skb, free_list);
out:
	t->skb = NULL;
	mt76_put_txwi(mdev, t);
}
EXPORT_SYMBOL_GPL(mt7925_txwi_free);

static void
mt7925_mac_tx_free(struct mt792x_dev *dev, void *data, int len)
{
	__le32 *tx_free = (__le32 *)data, *cur_info;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_txwi_cache *txwi;
	struct ieee80211_sta *sta = NULL;
	struct mt76_wcid *wcid = NULL;
	LIST_HEAD(free_list);
	struct sk_buff *skb, *tmp;
	void *end = data + len;
	bool wake = false;
	u16 total, count = 0;

	/* clean DMA queues and unmap buffers first */
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_PSD], false);
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_BE], false);

	if (WARN_ON_ONCE(le32_get_bits(tx_free[1], MT_TXFREE1_VER) < 4))
		return;

	total = le32_get_bits(tx_free[0], MT_TXFREE0_MSDU_CNT);
	for (cur_info = &tx_free[2]; count < total; cur_info++) {
		u32 msdu, info;
		u8 i;

		if (WARN_ON_ONCE((void *)cur_info >= end))
			return;
		/* 1'b1: new wcid pair.
		 * 1'b0: msdu_id with the same 'wcid pair' as above.
		 */
		info = le32_to_cpu(*cur_info);
		if (info & MT_TXFREE_INFO_PAIR) {
			struct mt792x_link_sta *mlink;
			u16 idx;

			idx = FIELD_GET(MT_TXFREE_INFO_WLAN_ID, info);
			wcid = mt76_wcid_ptr(dev, idx);
			sta = wcid_to_sta(wcid);
			if (!sta)
				continue;

			mlink = container_of(wcid, struct mt792x_link_sta, wcid);
			mt76_wcid_add_poll(&dev->mt76, &mlink->wcid);
			continue;
		}

		if (info & MT_TXFREE_INFO_HEADER) {
			if (wcid) {
				wcid->stats.tx_retries +=
					FIELD_GET(MT_TXFREE_INFO_COUNT, info) - 1;
				wcid->stats.tx_failed +=
					!!FIELD_GET(MT_TXFREE_INFO_STAT, info);
			}
			continue;
		}

		for (i = 0; i < 2; i++) {
			msdu = (info >> (15 * i)) & MT_TXFREE_INFO_MSDU_ID;
			if (msdu == MT_TXFREE_INFO_MSDU_ID)
				continue;

			count++;
			txwi = mt76_token_release(mdev, msdu, &wake);
			if (!txwi)
				continue;

			mt7925_txwi_free(dev, txwi, sta, wcid, &free_list);
		}
	}

	mt7925_mac_sta_poll(dev);

	if (wake)
		mt76_set_tx_blocked(&dev->mt76, false);

	mt76_worker_schedule(&dev->mt76.tx_worker);

	list_for_each_entry_safe(skb, tmp, &free_list, list) {
		skb_list_del_init(skb);
		napi_consume_skb(skb, 1);
	}
}

bool mt7925_rx_check(struct mt76_dev *mdev, void *data, int len)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	__le32 *rxd = (__le32 *)data;
	__le32 *end = (__le32 *)&rxd[len / 4];
	enum rx_pkt_type type;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);
	if (type != PKT_TYPE_NORMAL) {
		u32 sw_type = le32_get_bits(rxd[0], MT_RXD0_SW_PKT_TYPE_MASK);

		if (unlikely((sw_type & MT_RXD0_SW_PKT_TYPE_MAP) ==
			     MT_RXD0_SW_PKT_TYPE_FRAME))
			return true;
	}

	switch (type) {
	case PKT_TYPE_TXRX_NOTIFY:
		/* PKT_TYPE_TXRX_NOTIFY can be received only by mmio devices */
		mt7925_mac_tx_free(dev, data, len); /* mmio */
		return false;
	case PKT_TYPE_TXS:
		for (rxd += 4; rxd + 12 <= end; rxd += 12)
			mt7925_mac_add_txs(dev, rxd);
		return false;
	default:
		return true;
	}
}
EXPORT_SYMBOL_GPL(mt7925_rx_check);

void mt7925_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb, u32 *info)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *end = (__le32 *)&skb->data[skb->len];
	enum rx_pkt_type type;
	u16 flag;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);
	flag = le32_get_bits(rxd[0], MT_RXD0_PKT_FLAG);
	if (type != PKT_TYPE_NORMAL) {
		u32 sw_type = le32_get_bits(rxd[0], MT_RXD0_SW_PKT_TYPE_MASK);

		if (unlikely((sw_type & MT_RXD0_SW_PKT_TYPE_MAP) ==
			     MT_RXD0_SW_PKT_TYPE_FRAME))
			type = PKT_TYPE_NORMAL;
	}

	if (type == PKT_TYPE_RX_EVENT && flag == 0x1)
		type = PKT_TYPE_NORMAL_MCU;

	switch (type) {
	case PKT_TYPE_TXRX_NOTIFY:
		/* PKT_TYPE_TXRX_NOTIFY can be received only by mmio devices */
		mt7925_mac_tx_free(dev, skb->data, skb->len);
		napi_consume_skb(skb, 1);
		break;
	case PKT_TYPE_RX_EVENT:
		mt7925_mcu_rx_event(dev, skb);
		break;
	case PKT_TYPE_TXS:
		for (rxd += 2; rxd + 8 <= end; rxd += 8)
			mt7925_mac_add_txs(dev, rxd);
		dev_kfree_skb(skb);
		break;
	case PKT_TYPE_NORMAL_MCU:
	case PKT_TYPE_NORMAL:
		if (!mt7925_mac_fill_rx(dev, skb)) {
			mt76_rx(&dev->mt76, q, skb);
			return;
		}
		fallthrough;
	default:
		dev_kfree_skb(skb);
		break;
	}
}
EXPORT_SYMBOL_GPL(mt7925_queue_rx_skb);

static void
mt7925_vif_connect_iter(void *priv, u8 *mac,
			struct ieee80211_vif *vif)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	unsigned long valid = ieee80211_vif_is_mld(vif) ?
			      mvif->valid_links : BIT(0);
	struct mt792x_dev *dev = mvif->phy->dev;
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct ieee80211_bss_conf *bss_conf;
	struct mt792x_bss_conf *mconf;
	int i;

	if (vif->type == NL80211_IFTYPE_STATION)
		ieee80211_disconnect(vif, true);

	for_each_set_bit(i, &valid, IEEE80211_MLD_MAX_NUM_LINKS) {
		bss_conf = mt792x_vif_to_bss_conf(vif, i);
		mconf = mt792x_vif_to_link(mvif, i);

		mt76_connac_mcu_uni_add_dev(&dev->mphy, bss_conf, &mconf->mt76,
					    &mvif->sta.deflink.wcid, true);
		mt7925_mcu_set_tx(dev, bss_conf);
	}

	if (vif->type == NL80211_IFTYPE_AP) {
		mt76_connac_mcu_uni_add_bss(dev->phy.mt76, vif, &mvif->sta.deflink.wcid,
					    true, NULL);
		mt7925_mcu_sta_update(dev, NULL, vif, true,
				      MT76_STA_INFO_STATE_NONE);
		mt7925_mcu_uni_add_beacon_offload(dev, hw, vif, true);
	}
}

/* system error recovery */
void mt7925_mac_reset_work(struct work_struct *work)
{
	struct mt792x_dev *dev = container_of(work, struct mt792x_dev,
					      reset_work);
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct mt76_connac_pm *pm = &dev->pm;
	int i, ret;

	dev_dbg(dev->mt76.dev, "chip reset\n");
	dev->hw_full_reset = true;
	ieee80211_stop_queues(hw);

	cancel_delayed_work_sync(&dev->mphy.mac_work);
	cancel_delayed_work_sync(&pm->ps_work);
	cancel_work_sync(&pm->wake_work);
	dev->sar_inited = false;

	for (i = 0; i < 10; i++) {
		mutex_lock(&dev->mt76.mutex);
		ret = mt792x_dev_reset(dev);
		mutex_unlock(&dev->mt76.mutex);

		if (!ret)
			break;
	}

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
					    mt7925_vif_connect_iter, NULL);
	mt76_connac_power_save_sched(&dev->mt76.phy, pm);
}

void mt7925_coredump_work(struct work_struct *work)
{
	struct mt792x_dev *dev;
	char *dump, *data;

	dev = (struct mt792x_dev *)container_of(work, struct mt792x_dev,
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

		skb_pull(skb, sizeof(struct mt7925_mcu_rxd) + 8);
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

	mt792x_reset(&dev->mt76);
}

/* usb_sdio */
static void
mt7925_usb_sdio_write_txwi(struct mt792x_dev *dev, struct mt76_wcid *wcid,
			   enum mt76_txq_id qid, struct ieee80211_sta *sta,
			   struct ieee80211_key_conf *key, int pid,
			   struct sk_buff *skb)
{
	__le32 *txwi = (__le32 *)(skb->data - MT_SDIO_TXD_SIZE);

	memset(txwi, 0, MT_SDIO_TXD_SIZE);
	mt7925_mac_write_txwi(&dev->mt76, txwi, skb, wcid, key, pid, qid, 0);
	skb_push(skb, MT_SDIO_TXD_SIZE);
}

int mt7925_usb_sdio_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
				   enum mt76_txq_id qid, struct mt76_wcid *wcid,
				   struct ieee80211_sta *sta,
				   struct mt76_tx_info *tx_info)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_info->skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct sk_buff *skb = tx_info->skb;
	int err, pad, pktid;

	if (unlikely(tx_info->skb->len <= ETH_HLEN))
		return -EINVAL;

	if (!wcid)
		wcid = &dev->mt76.global_wcid;

	if (sta) {
		struct mt792x_sta *msta = (struct mt792x_sta *)sta->drv_priv;

		if (time_after(jiffies, msta->deflink.last_txs + HZ / 4)) {
			info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;
			msta->deflink.last_txs = jiffies;
		}
	}

	pktid = mt76_tx_status_skb_add(&dev->mt76, wcid, skb);
	mt7925_usb_sdio_write_txwi(dev, wcid, qid, sta, key, pktid, skb);

	mt792x_skb_add_usb_sdio_hdr(dev, skb, 0);
	pad = round_up(skb->len, 4) - skb->len;
	if (mt76_is_usb(mdev))
		pad += 4;

	err = mt76_skb_adjust_pad(skb, pad);
	if (err)
		/* Release pktid in case of error. */
		idr_remove(&wcid->pktid, pktid);

	return err;
}
EXPORT_SYMBOL_GPL(mt7925_usb_sdio_tx_prepare_skb);

void mt7925_usb_sdio_tx_complete_skb(struct mt76_dev *mdev,
				     struct mt76_queue_entry *e)
{
	__le32 *txwi = (__le32 *)(e->skb->data + MT_SDIO_HDR_SIZE);
	unsigned int headroom = MT_SDIO_TXD_SIZE + MT_SDIO_HDR_SIZE;
	struct ieee80211_sta *sta;
	struct mt76_wcid *wcid;
	u16 idx;

	idx = le32_get_bits(txwi[1], MT_TXD1_WLAN_IDX);
	wcid = __mt76_wcid_ptr(mdev, idx);
	sta = wcid_to_sta(wcid);

	if (sta && likely(e->skb->protocol != cpu_to_be16(ETH_P_PAE)))
		mt7925_tx_check_aggr(sta, e->skb, wcid);

	skb_pull(e->skb, headroom);
	mt76_tx_complete_skb(mdev, e->wcid, e->skb);
}
EXPORT_SYMBOL_GPL(mt7925_usb_sdio_tx_complete_skb);

bool mt7925_usb_sdio_tx_status_data(struct mt76_dev *mdev, u8 *update)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);

	mt792x_mutex_acquire(dev);
	mt7925_mac_sta_poll(dev);
	mt792x_mutex_release(dev);

	return false;
}
EXPORT_SYMBOL_GPL(mt7925_usb_sdio_tx_status_data);

#if IS_ENABLED(CONFIG_IPV6)
void mt7925_set_ipv6_ns_work(struct work_struct *work)
{
	struct mt792x_dev *dev = container_of(work, struct mt792x_dev,
						ipv6_ns_work);
	struct sk_buff *skb;
	int ret = 0;

	do {
		skb = skb_dequeue(&dev->ipv6_ns_list);

		if (!skb)
			break;

		mt792x_mutex_acquire(dev);
		ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
					    MCU_UNI_CMD(OFFLOAD), true);
		mt792x_mutex_release(dev);

	} while (!ret);

	if (ret)
		skb_queue_purge(&dev->ipv6_ns_list);
}
#endif
