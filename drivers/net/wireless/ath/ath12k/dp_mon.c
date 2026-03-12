// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "dp_mon.h"
#include "debug.h"
#include "dp_tx.h"
#include "peer.h"

static void
ath12k_dp_mon_fill_rx_stats_info(struct hal_rx_mon_ppdu_info *ppdu_info,
				 struct ieee80211_rx_status *rx_status)
{
	u32 center_freq = ppdu_info->freq;

	rx_status->freq = center_freq;
	rx_status->bw = ath12k_mac_bw_to_mac80211_bw(ppdu_info->bw);
	rx_status->nss = ppdu_info->nss;
	rx_status->rate_idx = 0;
	rx_status->encoding = RX_ENC_LEGACY;
	rx_status->flag |= RX_FLAG_NO_SIGNAL_VAL;

	if (center_freq >= ATH12K_MIN_6GHZ_FREQ &&
	    center_freq <= ATH12K_MAX_6GHZ_FREQ) {
		rx_status->band = NL80211_BAND_6GHZ;
	} else if (center_freq >= ATH12K_MIN_2GHZ_FREQ &&
		   center_freq <= ATH12K_MAX_2GHZ_FREQ) {
		rx_status->band = NL80211_BAND_2GHZ;
	} else if (center_freq >= ATH12K_MIN_5GHZ_FREQ &&
		   center_freq <= ATH12K_MAX_5GHZ_FREQ) {
		rx_status->band = NL80211_BAND_5GHZ;
	} else {
		rx_status->band = NUM_NL80211_BANDS;
	}
}

struct sk_buff
*ath12k_dp_rx_alloc_mon_status_buf(struct ath12k_base *ab,
				   struct dp_rxdma_mon_ring *rx_ring,
				   int *buf_id)
{
	struct sk_buff *skb;
	dma_addr_t paddr;

	skb = dev_alloc_skb(RX_MON_STATUS_BUF_SIZE);

	if (!skb)
		goto fail_alloc_skb;

	if (!IS_ALIGNED((unsigned long)skb->data,
			RX_MON_STATUS_BUF_ALIGN)) {
		skb_pull(skb, PTR_ALIGN(skb->data, RX_MON_STATUS_BUF_ALIGN) -
			 skb->data);
	}

	paddr = dma_map_single(ab->dev, skb->data,
			       skb->len + skb_tailroom(skb),
			       DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(ab->dev, paddr)))
		goto fail_free_skb;

	spin_lock_bh(&rx_ring->idr_lock);
	*buf_id = idr_alloc(&rx_ring->bufs_idr, skb, 0,
			    rx_ring->bufs_max, GFP_ATOMIC);
	spin_unlock_bh(&rx_ring->idr_lock);
	if (*buf_id < 0)
		goto fail_dma_unmap;

	ATH12K_SKB_RXCB(skb)->paddr = paddr;
	return skb;

fail_dma_unmap:
	dma_unmap_single(ab->dev, paddr, skb->len + skb_tailroom(skb),
			 DMA_FROM_DEVICE);
fail_free_skb:
	dev_kfree_skb_any(skb);
fail_alloc_skb:
	return NULL;
}
EXPORT_SYMBOL(ath12k_dp_rx_alloc_mon_status_buf);

u32 ath12k_dp_mon_comp_ppduid(u32 msdu_ppdu_id, u32 *ppdu_id)
{
	u32 ret = 0;

	if ((*ppdu_id < msdu_ppdu_id) &&
	    ((msdu_ppdu_id - *ppdu_id) < DP_NOT_PPDU_ID_WRAP_AROUND)) {
		/* Hold on mon dest ring, and reap mon status ring. */
		*ppdu_id = msdu_ppdu_id;
		ret = msdu_ppdu_id;
	} else if ((*ppdu_id > msdu_ppdu_id) &&
		((*ppdu_id - msdu_ppdu_id) > DP_NOT_PPDU_ID_WRAP_AROUND)) {
		/* PPDU ID has exceeded the maximum value and will
		 * restart from 0.
		 */
		*ppdu_id = msdu_ppdu_id;
		ret = msdu_ppdu_id;
	}
	return ret;
}
EXPORT_SYMBOL(ath12k_dp_mon_comp_ppduid);

static void
ath12k_dp_mon_fill_rx_rate(struct ath12k_pdev_dp *dp_pdev,
			   struct hal_rx_mon_ppdu_info *ppdu_info,
			   struct ieee80211_rx_status *rx_status)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_base *ab = dp->ab;
	struct ieee80211_supported_band *sband;
	enum rx_msdu_start_pkt_type pkt_type;
	u8 rate_mcs, nss, sgi;
	bool is_cck;

	pkt_type = ppdu_info->preamble_type;
	rate_mcs = ppdu_info->rate;
	nss = ppdu_info->nss;
	sgi = ppdu_info->gi;

	switch (pkt_type) {
	case RX_MSDU_START_PKT_TYPE_11A:
	case RX_MSDU_START_PKT_TYPE_11B:
		is_cck = (pkt_type == RX_MSDU_START_PKT_TYPE_11B);
		if (rx_status->band < NUM_NL80211_BANDS) {
			struct ath12k *ar = ath12k_pdev_dp_to_ar(dp_pdev);

			sband = &ar->mac.sbands[rx_status->band];
			rx_status->rate_idx = ath12k_mac_hw_rate_to_idx(sband, rate_mcs,
									is_cck);
		}
		break;
	case RX_MSDU_START_PKT_TYPE_11N:
		rx_status->encoding = RX_ENC_HT;
		if (rate_mcs > ATH12K_HT_MCS_MAX) {
			ath12k_warn(ab,
				    "Received with invalid mcs in HT mode %d\n",
				     rate_mcs);
			break;
		}
		rx_status->rate_idx = rate_mcs + (8 * (nss - 1));
		if (sgi)
			rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		break;
	case RX_MSDU_START_PKT_TYPE_11AC:
		rx_status->encoding = RX_ENC_VHT;
		rx_status->rate_idx = rate_mcs;
		if (rate_mcs > ATH12K_VHT_MCS_MAX) {
			ath12k_warn(ab,
				    "Received with invalid mcs in VHT mode %d\n",
				     rate_mcs);
			break;
		}
		if (sgi)
			rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		break;
	case RX_MSDU_START_PKT_TYPE_11AX:
		rx_status->rate_idx = rate_mcs;
		if (rate_mcs > ATH12K_HE_MCS_MAX) {
			ath12k_warn(ab,
				    "Received with invalid mcs in HE mode %d\n",
				    rate_mcs);
			break;
		}
		rx_status->encoding = RX_ENC_HE;
		rx_status->he_gi = ath12k_he_gi_to_nl80211_he_gi(sgi);
		break;
	case RX_MSDU_START_PKT_TYPE_11BE:
		rx_status->rate_idx = rate_mcs;
		if (rate_mcs > ATH12K_EHT_MCS_MAX) {
			ath12k_warn(ab,
				    "Received with invalid mcs in EHT mode %d\n",
				    rate_mcs);
			break;
		}
		rx_status->encoding = RX_ENC_EHT;
		rx_status->he_gi = ath12k_he_gi_to_nl80211_he_gi(sgi);
		break;
	default:
		ath12k_dbg(ab, ATH12K_DBG_DATA,
			   "monitor receives invalid preamble type %d",
			    pkt_type);
		break;
	}
}

static void ath12k_dp_mon_rx_msdus_set_payload(struct ath12k_base *ab,
					       struct sk_buff *head_msdu,
					       struct sk_buff *tail_msdu)
{
	u32 rx_pkt_offset, l2_hdr_offset, total_offset;

	rx_pkt_offset = ab->hal.hal_desc_sz;
	l2_hdr_offset =
		ath12k_dp_rx_h_l3pad(ab, (struct hal_rx_desc *)tail_msdu->data);

	if (ab->hw_params->rxdma1_enable)
		total_offset = ATH12K_MON_RX_PKT_OFFSET;
	else
		total_offset = rx_pkt_offset + l2_hdr_offset;

	skb_pull(head_msdu, total_offset);
}

struct sk_buff *
ath12k_dp_mon_rx_merg_msdus(struct ath12k_pdev_dp *dp_pdev,
			    struct dp_mon_mpdu *mon_mpdu,
			    struct hal_rx_mon_ppdu_info *ppdu_info,
			    struct ieee80211_rx_status *rxs)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_base *ab = dp->ab;
	struct sk_buff *msdu, *mpdu_buf, *prev_buf, *head_frag_list;
	struct sk_buff *head_msdu, *tail_msdu;
	struct hal_rx_desc *rx_desc;
	u8 *hdr_desc, *dest, decap_format = mon_mpdu->decap_format;
	struct ieee80211_hdr_3addr *wh;
	struct ieee80211_channel *channel;
	u32 frag_list_sum_len = 0;
	u8 channel_num = ppdu_info->chan_num;

	mpdu_buf = NULL;
	head_msdu = mon_mpdu->head;
	tail_msdu = mon_mpdu->tail;

	if (!head_msdu || !tail_msdu)
		goto err_merge_fail;

	ath12k_dp_mon_fill_rx_stats_info(ppdu_info, rxs);

	if (unlikely(rxs->band == NUM_NL80211_BANDS ||
		     !ath12k_pdev_dp_to_hw(dp_pdev)->wiphy->bands[rxs->band])) {
		struct ath12k *ar = ath12k_pdev_dp_to_ar(dp_pdev);

		ath12k_dbg(ab, ATH12K_DBG_DATA,
			   "sband is NULL for status band %d channel_num %d center_freq %d pdev_id %d\n",
			   rxs->band, channel_num, ppdu_info->freq, ar->pdev_idx);

		spin_lock_bh(&ar->data_lock);
		channel = ar->rx_channel;
		if (channel) {
			rxs->band = channel->band;
			channel_num =
				ieee80211_frequency_to_channel(channel->center_freq);
		}
		spin_unlock_bh(&ar->data_lock);
	}

	if (rxs->band < NUM_NL80211_BANDS)
		rxs->freq = ieee80211_channel_to_frequency(channel_num,
							   rxs->band);

	ath12k_dp_mon_fill_rx_rate(dp_pdev, ppdu_info, rxs);

	if (decap_format == DP_RX_DECAP_TYPE_RAW) {
		ath12k_dp_mon_rx_msdus_set_payload(ab, head_msdu, tail_msdu);

		prev_buf = head_msdu;
		msdu = head_msdu->next;
		head_frag_list = NULL;

		while (msdu) {
			ath12k_dp_mon_rx_msdus_set_payload(ab, head_msdu, tail_msdu);

			if (!head_frag_list)
				head_frag_list = msdu;

			frag_list_sum_len += msdu->len;
			prev_buf = msdu;
			msdu = msdu->next;
		}

		prev_buf->next = NULL;

		skb_trim(prev_buf, prev_buf->len);
		if (head_frag_list) {
			skb_shinfo(head_msdu)->frag_list = head_frag_list;
			head_msdu->data_len = frag_list_sum_len;
			head_msdu->len += head_msdu->data_len;
			head_msdu->next = NULL;
		}
	} else if (decap_format == DP_RX_DECAP_TYPE_NATIVE_WIFI) {
		u8 qos_pkt = 0;

		rx_desc = (struct hal_rx_desc *)head_msdu->data;
		hdr_desc =
			ab->hal.ops->rx_desc_get_msdu_payload(rx_desc);

		/* Base size */
		wh = (struct ieee80211_hdr_3addr *)hdr_desc;

		if (ieee80211_is_data_qos(wh->frame_control))
			qos_pkt = 1;

		msdu = head_msdu;

		while (msdu) {
			ath12k_dp_mon_rx_msdus_set_payload(ab, head_msdu, tail_msdu);
			if (qos_pkt) {
				dest = skb_push(msdu, sizeof(__le16));
				if (!dest)
					goto err_merge_fail;
				memcpy(dest, hdr_desc, sizeof(struct ieee80211_qos_hdr));
			}
			prev_buf = msdu;
			msdu = msdu->next;
		}
		dest = skb_put(prev_buf, HAL_RX_FCS_LEN);
		if (!dest)
			goto err_merge_fail;

		ath12k_dbg(ab, ATH12K_DBG_DATA,
			   "mpdu_buf %p mpdu_buf->len %u",
			   prev_buf, prev_buf->len);
	} else {
		ath12k_dbg(ab, ATH12K_DBG_DATA,
			   "decap format %d is not supported!\n",
			   decap_format);
		goto err_merge_fail;
	}

	return head_msdu;

err_merge_fail:
	if (mpdu_buf && decap_format != DP_RX_DECAP_TYPE_RAW) {
		ath12k_dbg(ab, ATH12K_DBG_DATA,
			   "err_merge_fail mpdu_buf %p", mpdu_buf);
		/* Free the head buffer */
		dev_kfree_skb_any(mpdu_buf);
	}
	return NULL;
}
EXPORT_SYMBOL(ath12k_dp_mon_rx_merg_msdus);

static void
ath12k_dp_mon_rx_update_radiotap_he(struct hal_rx_mon_ppdu_info *rx_status,
				    u8 *rtap_buf)
{
	u32 rtap_len = 0;

	put_unaligned_le16(rx_status->he_data1, &rtap_buf[rtap_len]);
	rtap_len += 2;

	put_unaligned_le16(rx_status->he_data2, &rtap_buf[rtap_len]);
	rtap_len += 2;

	put_unaligned_le16(rx_status->he_data3, &rtap_buf[rtap_len]);
	rtap_len += 2;

	put_unaligned_le16(rx_status->he_data4, &rtap_buf[rtap_len]);
	rtap_len += 2;

	put_unaligned_le16(rx_status->he_data5, &rtap_buf[rtap_len]);
	rtap_len += 2;

	put_unaligned_le16(rx_status->he_data6, &rtap_buf[rtap_len]);
}

static void
ath12k_dp_mon_rx_update_radiotap_he_mu(struct hal_rx_mon_ppdu_info *rx_status,
				       u8 *rtap_buf)
{
	u32 rtap_len = 0;

	put_unaligned_le16(rx_status->he_flags1, &rtap_buf[rtap_len]);
	rtap_len += 2;

	put_unaligned_le16(rx_status->he_flags2, &rtap_buf[rtap_len]);
	rtap_len += 2;

	rtap_buf[rtap_len] = rx_status->he_RU[0];
	rtap_len += 1;

	rtap_buf[rtap_len] = rx_status->he_RU[1];
	rtap_len += 1;

	rtap_buf[rtap_len] = rx_status->he_RU[2];
	rtap_len += 1;

	rtap_buf[rtap_len] = rx_status->he_RU[3];
}

void ath12k_dp_mon_update_radiotap(struct ath12k_pdev_dp *dp_pdev,
				   struct hal_rx_mon_ppdu_info *ppduinfo,
				   struct sk_buff *mon_skb,
				   struct ieee80211_rx_status *rxs)
{
	struct ath12k *ar = ath12k_pdev_dp_to_ar(dp_pdev);
	struct ieee80211_supported_band *sband;
	s32 noise_floor;
	u8 *ptr = NULL;

	spin_lock_bh(&ar->data_lock);
	noise_floor = ath12k_pdev_get_noise_floor(ar);
	spin_unlock_bh(&ar->data_lock);

	rxs->flag |= RX_FLAG_MACTIME_START;
	rxs->nss = ppduinfo->nss;
	if (test_bit(WMI_TLV_SERVICE_HW_DB2DBM_CONVERSION_SUPPORT,
		     ar->ab->wmi_ab.svc_map))
		rxs->signal = ppduinfo->rssi_comb;
	else
		rxs->signal = ppduinfo->rssi_comb + noise_floor;

	if (ppduinfo->userstats[ppduinfo->userid].ampdu_present) {
		rxs->flag |= RX_FLAG_AMPDU_DETAILS;
		rxs->ampdu_reference = ppduinfo->userstats[ppduinfo->userid].ampdu_id;
	}

	if (ppduinfo->is_eht || ppduinfo->eht_usig) {
		struct ieee80211_radiotap_tlv *tlv;
		struct ieee80211_radiotap_eht *eht;
		struct ieee80211_radiotap_eht_usig *usig;
		u16 len = 0, i, eht_len, usig_len;
		u8 user;

		if (ppduinfo->is_eht) {
			eht_len = struct_size(eht,
					      user_info,
					      ppduinfo->eht_info.num_user_info);
			len += sizeof(*tlv) + eht_len;
		}

		if (ppduinfo->eht_usig) {
			usig_len = sizeof(*usig);
			len += sizeof(*tlv) + usig_len;
		}

		rxs->flag |= RX_FLAG_RADIOTAP_TLV_AT_END;
		rxs->encoding = RX_ENC_EHT;

		skb_reset_mac_header(mon_skb);

		tlv = skb_push(mon_skb, len);

		if (ppduinfo->is_eht) {
			tlv->type = cpu_to_le16(IEEE80211_RADIOTAP_EHT);
			tlv->len = cpu_to_le16(eht_len);

			eht = (struct ieee80211_radiotap_eht *)tlv->data;
			eht->known = ppduinfo->eht_info.eht.known;

			for (i = 0;
			     i < ARRAY_SIZE(eht->data) &&
			     i < ARRAY_SIZE(ppduinfo->eht_info.eht.data);
			     i++)
				eht->data[i] = ppduinfo->eht_info.eht.data[i];

			for (user = 0; user < ppduinfo->eht_info.num_user_info; user++)
				put_unaligned_le32(ppduinfo->eht_info.user_info[user],
						   &eht->user_info[user]);

			tlv = (struct ieee80211_radiotap_tlv *)&tlv->data[eht_len];
		}

		if (ppduinfo->eht_usig) {
			tlv->type = cpu_to_le16(IEEE80211_RADIOTAP_EHT_USIG);
			tlv->len = cpu_to_le16(usig_len);

			usig = (struct ieee80211_radiotap_eht_usig *)tlv->data;
			*usig = ppduinfo->u_sig_info.usig;
		}
	} else if (ppduinfo->he_mu_flags) {
		rxs->flag |= RX_FLAG_RADIOTAP_HE_MU;
		rxs->encoding = RX_ENC_HE;
		ptr = skb_push(mon_skb, sizeof(struct ieee80211_radiotap_he_mu));
		ath12k_dp_mon_rx_update_radiotap_he_mu(ppduinfo, ptr);
	} else if (ppduinfo->he_flags) {
		rxs->flag |= RX_FLAG_RADIOTAP_HE;
		rxs->encoding = RX_ENC_HE;
		ptr = skb_push(mon_skb, sizeof(struct ieee80211_radiotap_he));
		ath12k_dp_mon_rx_update_radiotap_he(ppduinfo, ptr);
		rxs->rate_idx = ppduinfo->rate;
	} else if (ppduinfo->vht_flags) {
		rxs->encoding = RX_ENC_VHT;
		rxs->rate_idx = ppduinfo->rate;
	} else if (ppduinfo->ht_flags) {
		rxs->encoding = RX_ENC_HT;
		rxs->rate_idx = ppduinfo->rate;
	} else {
		struct ath12k *ar;

		ar = ath12k_pdev_dp_to_ar(dp_pdev);
		rxs->encoding = RX_ENC_LEGACY;
		sband = &ar->mac.sbands[rxs->band];
		rxs->rate_idx = ath12k_mac_hw_rate_to_idx(sband, ppduinfo->rate,
							  ppduinfo->cck_flag);
	}

	rxs->mactime = ppduinfo->tsft;
}
EXPORT_SYMBOL(ath12k_dp_mon_update_radiotap);

void ath12k_dp_mon_rx_deliver_msdu(struct ath12k_pdev_dp *dp_pdev,
				   struct napi_struct *napi,
				   struct sk_buff *msdu,
				   const struct hal_rx_mon_ppdu_info *ppduinfo,
				   struct ieee80211_rx_status *status,
				   u8 decap)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_base *ab = dp->ab;
	static const struct ieee80211_radiotap_he known = {
		.data1 = cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_DATA_MCS_KNOWN |
				     IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN),
		.data2 = cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_GI_KNOWN),
	};
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_radiotap_he *he = NULL;
	struct ieee80211_sta *pubsta = NULL;
	struct ath12k_dp_link_peer *peer;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	struct hal_rx_desc_data rx_info;
	bool is_mcbc = rxcb->is_mcbc;
	bool is_eapol_tkip = rxcb->is_eapol;
	struct hal_rx_desc *rx_desc = (struct hal_rx_desc *)msdu->data;
	u8 addr[ETH_ALEN] = {};

	status->link_valid = 0;

	if ((status->encoding == RX_ENC_HE) && !(status->flag & RX_FLAG_RADIOTAP_HE) &&
	    !(status->flag & RX_FLAG_SKIP_MONITOR)) {
		he = skb_push(msdu, sizeof(known));
		memcpy(he, &known, sizeof(known));
		status->flag |= RX_FLAG_RADIOTAP_HE;
	}

	ath12k_dp_extract_rx_desc_data(dp->hal, &rx_info, rx_desc, rx_desc);

	rcu_read_lock();
	spin_lock_bh(&dp->dp_lock);
	peer = ath12k_dp_rx_h_find_link_peer(dp_pdev, msdu, &rx_info);
	if (peer && peer->sta) {
		pubsta = peer->sta;
		memcpy(addr, peer->addr, ETH_ALEN);
		if (pubsta->valid_links) {
			status->link_valid = 1;
			status->link_id = peer->link_id;
		}
	}

	spin_unlock_bh(&dp->dp_lock);
	rcu_read_unlock();

	ath12k_dbg(ab, ATH12K_DBG_DATA,
		   "rx skb %p len %u peer %pM %u %s %s%s%s%s%s%s%s%s %srate_idx %u vht_nss %u freq %u band %u flag 0x%x fcs-err %i mic-err %i amsdu-more %i\n",
		   msdu,
		   msdu->len,
		   addr,
		   rxcb->tid,
		   (is_mcbc) ? "mcast" : "ucast",
		   (status->encoding == RX_ENC_LEGACY) ? "legacy" : "",
		   (status->encoding == RX_ENC_HT) ? "ht" : "",
		   (status->encoding == RX_ENC_VHT) ? "vht" : "",
		   (status->encoding == RX_ENC_HE) ? "he" : "",
		   (status->bw == RATE_INFO_BW_40) ? "40" : "",
		   (status->bw == RATE_INFO_BW_80) ? "80" : "",
		   (status->bw == RATE_INFO_BW_160) ? "160" : "",
		   (status->bw == RATE_INFO_BW_320) ? "320" : "",
		   status->enc_flags & RX_ENC_FLAG_SHORT_GI ? "sgi " : "",
		   status->rate_idx,
		   status->nss,
		   status->freq,
		   status->band, status->flag,
		   !!(status->flag & RX_FLAG_FAILED_FCS_CRC),
		   !!(status->flag & RX_FLAG_MMIC_ERROR),
		   !!(status->flag & RX_FLAG_AMSDU_MORE));

	ath12k_dbg_dump(ab, ATH12K_DBG_DP_RX, NULL, "dp rx msdu: ",
			msdu->data, msdu->len);
	rx_status = IEEE80211_SKB_RXCB(msdu);
	*rx_status = *status;

	/* TODO: trace rx packet */

	/* PN for multicast packets are not validate in HW,
	 * so skip 802.3 rx path
	 * Also, fast_rx expects the STA to be authorized, hence
	 * eapol packets are sent in slow path.
	 */
	if (decap == DP_RX_DECAP_TYPE_ETHERNET2_DIX && !is_eapol_tkip &&
	    !(is_mcbc && rx_status->flag & RX_FLAG_DECRYPTED))
		rx_status->flag |= RX_FLAG_8023;

	ieee80211_rx_napi(ath12k_pdev_dp_to_hw(dp_pdev), pubsta, msdu, napi);
}
EXPORT_SYMBOL(ath12k_dp_mon_rx_deliver_msdu);

int ath12k_dp_pkt_set_pktlen(struct sk_buff *skb, u32 len)
{
	if (skb->len > len) {
		skb_trim(skb, len);
	} else {
		if (skb_tailroom(skb) < len - skb->len) {
			if ((pskb_expand_head(skb, 0,
					      len - skb->len - skb_tailroom(skb),
					      GFP_ATOMIC))) {
				return -ENOMEM;
			}
		}
		skb_put(skb, (len - skb->len));
	}

	return 0;
}
EXPORT_SYMBOL(ath12k_dp_pkt_set_pktlen);

int
ath12k_dp_mon_parse_status_buf(struct ath12k_pdev_dp *dp_pdev,
			       struct ath12k_mon_data *pmon,
			       const struct dp_mon_packet_info *packet_info)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_base *ab = dp->ab;
	struct dp_rxdma_mon_ring *buf_ring = &dp->rxdma_mon_buf_ring;
	struct sk_buff *msdu;
	int buf_id;
	u32 offset;

	buf_id = u32_get_bits(packet_info->cookie, DP_RXDMA_BUF_COOKIE_BUF_ID);

	spin_lock_bh(&buf_ring->idr_lock);
	msdu = idr_remove(&buf_ring->bufs_idr, buf_id);
	spin_unlock_bh(&buf_ring->idr_lock);

	if (unlikely(!msdu)) {
		ath12k_warn(ab, "mon dest desc with inval buf_id %d\n", buf_id);
		return 0;
	}

	dma_unmap_single(ab->dev, ATH12K_SKB_RXCB(msdu)->paddr,
			 msdu->len + skb_tailroom(msdu),
			 DMA_FROM_DEVICE);

	offset = packet_info->dma_length + ATH12K_MON_RX_DOT11_OFFSET;
	if (ath12k_dp_pkt_set_pktlen(msdu, offset)) {
		dev_kfree_skb_any(msdu);
		goto dest_replenish;
	}

	if (!pmon->mon_mpdu->head)
		pmon->mon_mpdu->head = msdu;
	else
		pmon->mon_mpdu->tail->next = msdu;

	pmon->mon_mpdu->tail = msdu;

dest_replenish:
	ath12k_dp_mon_buf_replenish(ab, buf_ring, 1);

	return 0;
}
EXPORT_SYMBOL(ath12k_dp_mon_parse_status_buf);

int ath12k_dp_mon_buf_replenish(struct ath12k_base *ab,
				struct dp_rxdma_mon_ring *buf_ring,
				int req_entries)
{
	struct hal_mon_buf_ring *mon_buf;
	struct sk_buff *skb;
	struct hal_srng *srng;
	dma_addr_t paddr;
	u32 cookie;
	int buf_id;

	srng = &ab->hal.srng_list[buf_ring->refill_buf_ring.ring_id];
	spin_lock_bh(&srng->lock);
	ath12k_hal_srng_access_begin(ab, srng);

	while (req_entries > 0) {
		skb = dev_alloc_skb(DP_RX_BUFFER_SIZE + DP_RX_BUFFER_ALIGN_SIZE);
		if (unlikely(!skb))
			goto fail_alloc_skb;

		if (!IS_ALIGNED((unsigned long)skb->data, DP_RX_BUFFER_ALIGN_SIZE)) {
			skb_pull(skb,
				 PTR_ALIGN(skb->data, DP_RX_BUFFER_ALIGN_SIZE) -
				 skb->data);
		}

		paddr = dma_map_single(ab->dev, skb->data,
				       skb->len + skb_tailroom(skb),
				       DMA_FROM_DEVICE);

		if (unlikely(dma_mapping_error(ab->dev, paddr)))
			goto fail_free_skb;

		spin_lock_bh(&buf_ring->idr_lock);
		buf_id = idr_alloc(&buf_ring->bufs_idr, skb, 0,
				   buf_ring->bufs_max * 3, GFP_ATOMIC);
		spin_unlock_bh(&buf_ring->idr_lock);

		if (unlikely(buf_id < 0))
			goto fail_dma_unmap;

		mon_buf = ath12k_hal_srng_src_get_next_entry(ab, srng);
		if (unlikely(!mon_buf))
			goto fail_idr_remove;

		ATH12K_SKB_RXCB(skb)->paddr = paddr;

		cookie = u32_encode_bits(buf_id, DP_RXDMA_BUF_COOKIE_BUF_ID);

		mon_buf->paddr_lo = cpu_to_le32(lower_32_bits(paddr));
		mon_buf->paddr_hi = cpu_to_le32(upper_32_bits(paddr));
		mon_buf->cookie = cpu_to_le64(cookie);

		req_entries--;
	}

	ath12k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);
	return 0;

fail_idr_remove:
	spin_lock_bh(&buf_ring->idr_lock);
	idr_remove(&buf_ring->bufs_idr, buf_id);
	spin_unlock_bh(&buf_ring->idr_lock);
fail_dma_unmap:
	dma_unmap_single(ab->dev, paddr, skb->len + skb_tailroom(skb),
			 DMA_FROM_DEVICE);
fail_free_skb:
	dev_kfree_skb_any(skb);
fail_alloc_skb:
	ath12k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);
	return -ENOMEM;
}
EXPORT_SYMBOL(ath12k_dp_mon_buf_replenish);

int ath12k_dp_mon_status_bufs_replenish(struct ath12k_base *ab,
					struct dp_rxdma_mon_ring *rx_ring,
					int req_entries)
{
	enum hal_rx_buf_return_buf_manager mgr =
		ab->hal.hal_params->rx_buf_rbm;
	int num_free, num_remain, buf_id;
	struct ath12k_buffer_addr *desc;
	struct hal_srng *srng;
	struct sk_buff *skb;
	dma_addr_t paddr;
	u32 cookie;

	req_entries = min(req_entries, rx_ring->bufs_max);

	srng = &ab->hal.srng_list[rx_ring->refill_buf_ring.ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	num_free = ath12k_hal_srng_src_num_free(ab, srng, true);
	if (!req_entries && (num_free > (rx_ring->bufs_max * 3) / 4))
		req_entries = num_free;

	req_entries = min(num_free, req_entries);
	num_remain = req_entries;

	while (num_remain > 0) {
		skb = dev_alloc_skb(RX_MON_STATUS_BUF_SIZE);
		if (!skb)
			break;

		if (!IS_ALIGNED((unsigned long)skb->data,
				RX_MON_STATUS_BUF_ALIGN)) {
			skb_pull(skb,
				 PTR_ALIGN(skb->data, RX_MON_STATUS_BUF_ALIGN) -
				 skb->data);
		}

		paddr = dma_map_single(ab->dev, skb->data,
				       skb->len + skb_tailroom(skb),
				       DMA_FROM_DEVICE);
		if (dma_mapping_error(ab->dev, paddr))
			goto fail_free_skb;

		spin_lock_bh(&rx_ring->idr_lock);
		buf_id = idr_alloc(&rx_ring->bufs_idr, skb, 0,
				   rx_ring->bufs_max * 3, GFP_ATOMIC);
		spin_unlock_bh(&rx_ring->idr_lock);
		if (buf_id < 0)
			goto fail_dma_unmap;
		cookie = u32_encode_bits(buf_id, DP_RXDMA_BUF_COOKIE_BUF_ID);

		desc = ath12k_hal_srng_src_get_next_entry(ab, srng);
		if (!desc)
			goto fail_buf_unassign;

		ATH12K_SKB_RXCB(skb)->paddr = paddr;

		num_remain--;

		ath12k_hal_rx_buf_addr_info_set(&ab->hal, desc, paddr, cookie, mgr);
	}

	ath12k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	return req_entries - num_remain;

fail_buf_unassign:
	spin_lock_bh(&rx_ring->idr_lock);
	idr_remove(&rx_ring->bufs_idr, buf_id);
	spin_unlock_bh(&rx_ring->idr_lock);
fail_dma_unmap:
	dma_unmap_single(ab->dev, paddr, skb->len + skb_tailroom(skb),
			 DMA_FROM_DEVICE);
fail_free_skb:
	dev_kfree_skb_any(skb);

	ath12k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	return req_entries - num_remain;
}

static void
ath12k_dp_mon_rx_update_peer_rate_table_stats(struct ath12k_rx_peer_stats *rx_stats,
					      struct hal_rx_mon_ppdu_info *ppdu_info,
					      struct hal_rx_user_status *user_stats,
					      u32 num_msdu)
{
	struct ath12k_rx_peer_rate_stats *stats;
	u32 mcs_idx = (user_stats) ? user_stats->mcs : ppdu_info->mcs;
	u32 nss_idx = (user_stats) ? user_stats->nss - 1 : ppdu_info->nss - 1;
	u32 bw_idx = ppdu_info->bw;
	u32 gi_idx = ppdu_info->gi;
	u32 len;

	if (!rx_stats)
		return;

	if (mcs_idx > HAL_RX_MAX_MCS_HT || nss_idx >= HAL_RX_MAX_NSS ||
	    bw_idx >= HAL_RX_BW_MAX || gi_idx >= HAL_RX_GI_MAX) {
		return;
	}

	if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11AX ||
	    ppdu_info->preamble_type == HAL_RX_PREAMBLE_11BE)
		gi_idx = ath12k_he_gi_to_nl80211_he_gi(ppdu_info->gi);

	rx_stats->pkt_stats.rx_rate[bw_idx][gi_idx][nss_idx][mcs_idx] += num_msdu;
	stats = &rx_stats->byte_stats;

	if (user_stats)
		len = user_stats->mpdu_ok_byte_count;
	else
		len = ppdu_info->mpdu_len;

	stats->rx_rate[bw_idx][gi_idx][nss_idx][mcs_idx] += len;
}

void ath12k_dp_mon_rx_update_peer_su_stats(struct ath12k_dp_link_peer *peer,
					   struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_rx_peer_stats *rx_stats = peer->peer_stats.rx_stats;
	u32 num_msdu;

	peer->rssi_comb = ppdu_info->rssi_comb;
	ewma_avg_rssi_add(&peer->avg_rssi, ppdu_info->rssi_comb);
	if (!rx_stats)
		return;

	num_msdu = ppdu_info->tcp_msdu_count + ppdu_info->tcp_ack_msdu_count +
		   ppdu_info->udp_msdu_count + ppdu_info->other_msdu_count;

	rx_stats->num_msdu += num_msdu;
	rx_stats->tcp_msdu_count += ppdu_info->tcp_msdu_count +
				    ppdu_info->tcp_ack_msdu_count;
	rx_stats->udp_msdu_count += ppdu_info->udp_msdu_count;
	rx_stats->other_msdu_count += ppdu_info->other_msdu_count;

	if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11A ||
	    ppdu_info->preamble_type == HAL_RX_PREAMBLE_11B) {
		ppdu_info->nss = 1;
		ppdu_info->mcs = HAL_RX_MAX_MCS;
		ppdu_info->tid = IEEE80211_NUM_TIDS;
	}

	if (ppdu_info->ldpc < HAL_RX_SU_MU_CODING_MAX)
		rx_stats->coding_count[ppdu_info->ldpc] += num_msdu;

	if (ppdu_info->tid <= IEEE80211_NUM_TIDS)
		rx_stats->tid_count[ppdu_info->tid] += num_msdu;

	if (ppdu_info->preamble_type < HAL_RX_PREAMBLE_MAX)
		rx_stats->pream_cnt[ppdu_info->preamble_type] += num_msdu;

	if (ppdu_info->reception_type < HAL_RX_RECEPTION_TYPE_MAX)
		rx_stats->reception_type[ppdu_info->reception_type] += num_msdu;

	if (ppdu_info->is_stbc)
		rx_stats->stbc_count += num_msdu;

	if (ppdu_info->beamformed)
		rx_stats->beamformed_count += num_msdu;

	if (ppdu_info->num_mpdu_fcs_ok > 1)
		rx_stats->ampdu_msdu_count += num_msdu;
	else
		rx_stats->non_ampdu_msdu_count += num_msdu;

	rx_stats->num_mpdu_fcs_ok += ppdu_info->num_mpdu_fcs_ok;
	rx_stats->num_mpdu_fcs_err += ppdu_info->num_mpdu_fcs_err;
	rx_stats->dcm_count += ppdu_info->dcm;

	rx_stats->rx_duration += ppdu_info->rx_duration;
	peer->rx_duration = rx_stats->rx_duration;

	if (ppdu_info->nss > 0 && ppdu_info->nss <= HAL_RX_MAX_NSS) {
		rx_stats->pkt_stats.nss_count[ppdu_info->nss - 1] += num_msdu;
		rx_stats->byte_stats.nss_count[ppdu_info->nss - 1] += ppdu_info->mpdu_len;
	}

	if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11N &&
	    ppdu_info->mcs <= HAL_RX_MAX_MCS_HT) {
		rx_stats->pkt_stats.ht_mcs_count[ppdu_info->mcs] += num_msdu;
		rx_stats->byte_stats.ht_mcs_count[ppdu_info->mcs] += ppdu_info->mpdu_len;
		/* To fit into rate table for HT packets */
		ppdu_info->mcs = ppdu_info->mcs % 8;
	}

	if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11AC &&
	    ppdu_info->mcs <= HAL_RX_MAX_MCS_VHT) {
		rx_stats->pkt_stats.vht_mcs_count[ppdu_info->mcs] += num_msdu;
		rx_stats->byte_stats.vht_mcs_count[ppdu_info->mcs] += ppdu_info->mpdu_len;
	}

	if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11AX &&
	    ppdu_info->mcs <= HAL_RX_MAX_MCS_HE) {
		rx_stats->pkt_stats.he_mcs_count[ppdu_info->mcs] += num_msdu;
		rx_stats->byte_stats.he_mcs_count[ppdu_info->mcs] += ppdu_info->mpdu_len;
	}

	if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11BE &&
	    ppdu_info->mcs <= HAL_RX_MAX_MCS_BE) {
		rx_stats->pkt_stats.be_mcs_count[ppdu_info->mcs] += num_msdu;
		rx_stats->byte_stats.be_mcs_count[ppdu_info->mcs] += ppdu_info->mpdu_len;
	}

	if ((ppdu_info->preamble_type == HAL_RX_PREAMBLE_11A ||
	     ppdu_info->preamble_type == HAL_RX_PREAMBLE_11B) &&
	     ppdu_info->rate < HAL_RX_LEGACY_RATE_INVALID) {
		rx_stats->pkt_stats.legacy_count[ppdu_info->rate] += num_msdu;
		rx_stats->byte_stats.legacy_count[ppdu_info->rate] += ppdu_info->mpdu_len;
	}

	if (ppdu_info->gi < HAL_RX_GI_MAX) {
		rx_stats->pkt_stats.gi_count[ppdu_info->gi] += num_msdu;
		rx_stats->byte_stats.gi_count[ppdu_info->gi] += ppdu_info->mpdu_len;
	}

	if (ppdu_info->bw < HAL_RX_BW_MAX) {
		rx_stats->pkt_stats.bw_count[ppdu_info->bw] += num_msdu;
		rx_stats->byte_stats.bw_count[ppdu_info->bw] += ppdu_info->mpdu_len;
	}

	ath12k_dp_mon_rx_update_peer_rate_table_stats(rx_stats, ppdu_info,
						      NULL, num_msdu);
}
EXPORT_SYMBOL(ath12k_dp_mon_rx_update_peer_su_stats);

void ath12k_dp_mon_rx_process_ulofdma(struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_user_status *rx_user_status;
	u32 num_users, i, mu_ul_user_v0_word0, mu_ul_user_v0_word1, ru_size;

	if (!(ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_MU_MIMO ||
	      ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_MU_OFDMA ||
	      ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_MU_OFDMA_MIMO))
		return;

	num_users = ppdu_info->num_users;
	if (num_users > HAL_MAX_UL_MU_USERS)
		num_users = HAL_MAX_UL_MU_USERS;

	for (i = 0; i < num_users; i++) {
		rx_user_status = &ppdu_info->userstats[i];
		mu_ul_user_v0_word0 =
			rx_user_status->ul_ofdma_user_v0_word0;
		mu_ul_user_v0_word1 =
			rx_user_status->ul_ofdma_user_v0_word1;

		if (u32_get_bits(mu_ul_user_v0_word0,
				 HAL_RX_UL_OFDMA_USER_INFO_V0_W0_VALID) &&
		    !u32_get_bits(mu_ul_user_v0_word0,
				  HAL_RX_UL_OFDMA_USER_INFO_V0_W0_VER)) {
			rx_user_status->mcs =
				u32_get_bits(mu_ul_user_v0_word1,
					     HAL_RX_UL_OFDMA_USER_INFO_V0_W1_MCS);
			rx_user_status->nss =
				u32_get_bits(mu_ul_user_v0_word1,
					     HAL_RX_UL_OFDMA_USER_INFO_V0_W1_NSS) + 1;

			rx_user_status->ofdma_info_valid = 1;
			rx_user_status->ul_ofdma_ru_start_index =
				u32_get_bits(mu_ul_user_v0_word1,
					     HAL_RX_UL_OFDMA_USER_INFO_V0_W1_RU_START);

			ru_size = u32_get_bits(mu_ul_user_v0_word1,
					       HAL_RX_UL_OFDMA_USER_INFO_V0_W1_RU_SIZE);
			rx_user_status->ul_ofdma_ru_width = ru_size;
			rx_user_status->ul_ofdma_ru_size = ru_size;
		}
		rx_user_status->ldpc = u32_get_bits(mu_ul_user_v0_word1,
						    HAL_RX_UL_OFDMA_USER_INFO_V0_W1_LDPC);
	}
	ppdu_info->ldpc = 1;
}
EXPORT_SYMBOL(ath12k_dp_mon_rx_process_ulofdma);

static void
ath12k_dp_mon_rx_update_user_stats(struct ath12k_base *ab,
				   struct hal_rx_mon_ppdu_info *ppdu_info,
				   u32 uid)
{
	struct ath12k_rx_peer_stats *rx_stats = NULL;
	struct hal_rx_user_status *user_stats = &ppdu_info->userstats[uid];
	struct ath12k_dp_link_peer *peer;
	u32 num_msdu;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	if (user_stats->ast_index == 0 || user_stats->ast_index == 0xFFFF)
		return;

	peer = ath12k_dp_link_peer_find_by_ast(dp, user_stats->ast_index);

	if (!peer) {
		ath12k_warn(ab, "peer ast idx %d can't be found\n",
			    user_stats->ast_index);
		return;
	}

	peer->rssi_comb = ppdu_info->rssi_comb;
	ewma_avg_rssi_add(&peer->avg_rssi, ppdu_info->rssi_comb);
	rx_stats = peer->peer_stats.rx_stats;
	if (!rx_stats)
		return;

	num_msdu = user_stats->tcp_msdu_count + user_stats->tcp_ack_msdu_count +
		   user_stats->udp_msdu_count + user_stats->other_msdu_count;

	rx_stats->num_msdu += num_msdu;
	rx_stats->tcp_msdu_count += user_stats->tcp_msdu_count +
				    user_stats->tcp_ack_msdu_count;
	rx_stats->udp_msdu_count += user_stats->udp_msdu_count;
	rx_stats->other_msdu_count += user_stats->other_msdu_count;

	if (ppdu_info->ldpc < HAL_RX_SU_MU_CODING_MAX)
		rx_stats->coding_count[ppdu_info->ldpc] += num_msdu;

	if (user_stats->tid <= IEEE80211_NUM_TIDS)
		rx_stats->tid_count[user_stats->tid] += num_msdu;

	if (user_stats->preamble_type < HAL_RX_PREAMBLE_MAX)
		rx_stats->pream_cnt[user_stats->preamble_type] += num_msdu;

	if (ppdu_info->reception_type < HAL_RX_RECEPTION_TYPE_MAX)
		rx_stats->reception_type[ppdu_info->reception_type] += num_msdu;

	if (ppdu_info->is_stbc)
		rx_stats->stbc_count += num_msdu;

	if (ppdu_info->beamformed)
		rx_stats->beamformed_count += num_msdu;

	if (user_stats->mpdu_cnt_fcs_ok > 1)
		rx_stats->ampdu_msdu_count += num_msdu;
	else
		rx_stats->non_ampdu_msdu_count += num_msdu;

	rx_stats->num_mpdu_fcs_ok += user_stats->mpdu_cnt_fcs_ok;
	rx_stats->num_mpdu_fcs_err += user_stats->mpdu_cnt_fcs_err;
	rx_stats->dcm_count += ppdu_info->dcm;
	if (ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_MU_OFDMA ||
	    ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_MU_OFDMA_MIMO)
		rx_stats->ru_alloc_cnt[user_stats->ul_ofdma_ru_size] += num_msdu;

	rx_stats->rx_duration += ppdu_info->rx_duration;
	peer->rx_duration = rx_stats->rx_duration;

	if (user_stats->nss > 0 && user_stats->nss <= HAL_RX_MAX_NSS) {
		rx_stats->pkt_stats.nss_count[user_stats->nss - 1] += num_msdu;
		rx_stats->byte_stats.nss_count[user_stats->nss - 1] +=
						user_stats->mpdu_ok_byte_count;
	}

	if (user_stats->preamble_type == HAL_RX_PREAMBLE_11AX &&
	    user_stats->mcs <= HAL_RX_MAX_MCS_HE) {
		rx_stats->pkt_stats.he_mcs_count[user_stats->mcs] += num_msdu;
		rx_stats->byte_stats.he_mcs_count[user_stats->mcs] +=
						user_stats->mpdu_ok_byte_count;
	}

	if (ppdu_info->gi < HAL_RX_GI_MAX) {
		rx_stats->pkt_stats.gi_count[ppdu_info->gi] += num_msdu;
		rx_stats->byte_stats.gi_count[ppdu_info->gi] +=
						user_stats->mpdu_ok_byte_count;
	}

	if (ppdu_info->bw < HAL_RX_BW_MAX) {
		rx_stats->pkt_stats.bw_count[ppdu_info->bw] += num_msdu;
		rx_stats->byte_stats.bw_count[ppdu_info->bw] +=
						user_stats->mpdu_ok_byte_count;
	}

	ath12k_dp_mon_rx_update_peer_rate_table_stats(rx_stats, ppdu_info,
						      user_stats, num_msdu);
}

void
ath12k_dp_mon_rx_update_peer_mu_stats(struct ath12k_base *ab,
				      struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 num_users, i;

	num_users = ppdu_info->num_users;
	if (num_users > HAL_MAX_UL_MU_USERS)
		num_users = HAL_MAX_UL_MU_USERS;

	for (i = 0; i < num_users; i++)
		ath12k_dp_mon_rx_update_user_stats(ab, ppdu_info, i);
}
EXPORT_SYMBOL(ath12k_dp_mon_rx_update_peer_mu_stats);
