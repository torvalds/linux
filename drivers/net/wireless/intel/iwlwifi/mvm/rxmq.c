// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2025 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 */
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include "iwl-trans.h"
#include "mvm.h"
#include "fw-api.h"
#include "time-sync.h"

static inline int iwl_mvm_check_pn(struct iwl_mvm *mvm, struct sk_buff *skb,
				   int queue, struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvmsta;
	struct ieee80211_hdr *hdr = (void *)skb_mac_header(skb);
	struct ieee80211_rx_status *stats = IEEE80211_SKB_RXCB(skb);
	struct iwl_mvm_key_pn *ptk_pn;
	int res;
	u8 tid, keyidx;
	u8 pn[IEEE80211_CCMP_PN_LEN];
	u8 *extiv;

	/* do PN checking */

	/* multicast and non-data only arrives on default queue */
	if (!ieee80211_is_data(hdr->frame_control) ||
	    is_multicast_ether_addr(hdr->addr1))
		return 0;

	/* do not check PN for open AP */
	if (!(stats->flag & RX_FLAG_DECRYPTED))
		return 0;

	/*
	 * avoid checking for default queue - we don't want to replicate
	 * all the logic that's necessary for checking the PN on fragmented
	 * frames, leave that to mac80211
	 */
	if (queue == 0)
		return 0;

	/* if we are here - this for sure is either CCMP or GCMP */
	if (IS_ERR_OR_NULL(sta)) {
		IWL_DEBUG_DROP(mvm,
			       "expected hw-decrypted unicast frame for station\n");
		return -1;
	}

	mvmsta = iwl_mvm_sta_from_mac80211(sta);

	extiv = (u8 *)hdr + ieee80211_hdrlen(hdr->frame_control);
	keyidx = extiv[3] >> 6;

	ptk_pn = rcu_dereference(mvmsta->ptk_pn[keyidx]);
	if (!ptk_pn)
		return -1;

	if (ieee80211_is_data_qos(hdr->frame_control))
		tid = ieee80211_get_tid(hdr);
	else
		tid = 0;

	/* we don't use HCCA/802.11 QoS TSPECs, so drop such frames */
	if (tid >= IWL_MAX_TID_COUNT)
		return -1;

	/* load pn */
	pn[0] = extiv[7];
	pn[1] = extiv[6];
	pn[2] = extiv[5];
	pn[3] = extiv[4];
	pn[4] = extiv[1];
	pn[5] = extiv[0];

	res = memcmp(pn, ptk_pn->q[queue].pn[tid], IEEE80211_CCMP_PN_LEN);
	if (res < 0)
		return -1;
	if (!res && !(stats->flag & RX_FLAG_ALLOW_SAME_PN))
		return -1;

	memcpy(ptk_pn->q[queue].pn[tid], pn, IEEE80211_CCMP_PN_LEN);
	stats->flag |= RX_FLAG_PN_VALIDATED;

	return 0;
}

/* iwl_mvm_create_skb Adds the rxb to a new skb */
static int iwl_mvm_create_skb(struct iwl_mvm *mvm, struct sk_buff *skb,
			      struct ieee80211_hdr *hdr, u16 len, u8 crypt_len,
			      struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_rx_mpdu_desc *desc = (void *)pkt->data;
	unsigned int headlen, fraglen, pad_len = 0;
	unsigned int hdrlen = ieee80211_hdrlen(hdr->frame_control);
	u8 mic_crc_len = u8_get_bits(desc->mac_flags1,
				     IWL_RX_MPDU_MFLG1_MIC_CRC_LEN_MASK) << 1;

	if (desc->mac_flags2 & IWL_RX_MPDU_MFLG2_PAD) {
		len -= 2;
		pad_len = 2;
	}

	/*
	 * For non monitor interface strip the bytes the RADA might not have
	 * removed (it might be disabled, e.g. for mgmt frames). As a monitor
	 * interface cannot exist with other interfaces, this removal is safe
	 * and sufficient, in monitor mode there's no decryption being done.
	 */
	if (len > mic_crc_len && !ieee80211_hw_check(mvm->hw, RX_INCLUDES_FCS))
		len -= mic_crc_len;

	/* If frame is small enough to fit in skb->head, pull it completely.
	 * If not, only pull ieee80211_hdr (including crypto if present, and
	 * an additional 8 bytes for SNAP/ethertype, see below) so that
	 * splice() or TCP coalesce are more efficient.
	 *
	 * Since, in addition, ieee80211_data_to_8023() always pull in at
	 * least 8 bytes (possibly more for mesh) we can do the same here
	 * to save the cost of doing it later. That still doesn't pull in
	 * the actual IP header since the typical case has a SNAP header.
	 * If the latter changes (there are efforts in the standards group
	 * to do so) we should revisit this and ieee80211_data_to_8023().
	 */
	headlen = (len <= skb_tailroom(skb)) ? len :
					       hdrlen + crypt_len + 8;

	/* The firmware may align the packet to DWORD.
	 * The padding is inserted after the IV.
	 * After copying the header + IV skip the padding if
	 * present before copying packet data.
	 */
	hdrlen += crypt_len;

	if (unlikely(headlen < hdrlen))
		return -EINVAL;

	/* Since data doesn't move data while putting data on skb and that is
	 * the only way we use, data + len is the next place that hdr would be put
	 */
	skb_set_mac_header(skb, skb->len);
	skb_put_data(skb, hdr, hdrlen);
	skb_put_data(skb, (u8 *)hdr + hdrlen + pad_len, headlen - hdrlen);

	/*
	 * If we did CHECKSUM_COMPLETE, the hardware only does it right for
	 * certain cases and starts the checksum after the SNAP. Check if
	 * this is the case - it's easier to just bail out to CHECKSUM_NONE
	 * in the cases the hardware didn't handle, since it's rare to see
	 * such packets, even though the hardware did calculate the checksum
	 * in this case, just starting after the MAC header instead.
	 *
	 * Starting from Bz hardware, it calculates starting directly after
	 * the MAC header, so that matches mac80211's expectation.
	 */
	if (skb->ip_summed == CHECKSUM_COMPLETE) {
		struct {
			u8 hdr[6];
			__be16 type;
		} __packed *shdr = (void *)((u8 *)hdr + hdrlen + pad_len);

		if (unlikely(headlen - hdrlen < sizeof(*shdr) ||
			     !ether_addr_equal(shdr->hdr, rfc1042_header) ||
			     (shdr->type != htons(ETH_P_IP) &&
			      shdr->type != htons(ETH_P_ARP) &&
			      shdr->type != htons(ETH_P_IPV6) &&
			      shdr->type != htons(ETH_P_8021Q) &&
			      shdr->type != htons(ETH_P_PAE) &&
			      shdr->type != htons(ETH_P_TDLS))))
			skb->ip_summed = CHECKSUM_NONE;
		else if (mvm->trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_BZ)
			/* mac80211 assumes full CSUM including SNAP header */
			skb_postpush_rcsum(skb, shdr, sizeof(*shdr));
	}

	fraglen = len - headlen;

	if (fraglen) {
		int offset = (u8 *)hdr + headlen + pad_len -
			     (u8 *)rxb_addr(rxb) + rxb_offset(rxb);

		skb_add_rx_frag(skb, 0, rxb_steal_page(rxb), offset,
				fraglen, rxb->truesize);
	}

	return 0;
}

/* put a TLV on the skb and return data pointer
 *
 * Also pad to 4 the len and zero out all data part
 */
static void *
iwl_mvm_radiotap_put_tlv(struct sk_buff *skb, u16 type, u16 len)
{
	struct ieee80211_radiotap_tlv *tlv;

	tlv = skb_put(skb, sizeof(*tlv));
	tlv->type = cpu_to_le16(type);
	tlv->len = cpu_to_le16(len);
	return skb_put_zero(skb, ALIGN(len, 4));
}

static void iwl_mvm_add_rtap_sniffer_config(struct iwl_mvm *mvm,
					    struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_radiotap_vendor_content *radiotap;
	const u16 vendor_data_len = sizeof(mvm->cur_aid);

	if (!mvm->cur_aid)
		return;

	radiotap = iwl_mvm_radiotap_put_tlv(skb,
					    IEEE80211_RADIOTAP_VENDOR_NAMESPACE,
					    sizeof(*radiotap) + vendor_data_len);

	/* Intel OUI */
	radiotap->oui[0] = 0xf6;
	radiotap->oui[1] = 0x54;
	radiotap->oui[2] = 0x25;
	/* radiotap sniffer config sub-namespace */
	radiotap->oui_subtype = 1;
	radiotap->vendor_type = 0;

	/* fill the data now */
	memcpy(radiotap->data, &mvm->cur_aid, sizeof(mvm->cur_aid));

	rx_status->flag |= RX_FLAG_RADIOTAP_TLV_AT_END;
}

/* iwl_mvm_pass_packet_to_mac80211 - passes the packet for mac80211 */
static void iwl_mvm_pass_packet_to_mac80211(struct iwl_mvm *mvm,
					    struct napi_struct *napi,
					    struct sk_buff *skb, int queue,
					    struct ieee80211_sta *sta)
{
	if (unlikely(iwl_mvm_check_pn(mvm, skb, queue, sta))) {
		kfree_skb(skb);
		return;
	}

	ieee80211_rx_napi(mvm->hw, sta, skb, napi);
}

static void iwl_mvm_get_signal_strength(struct iwl_mvm *mvm,
					struct ieee80211_rx_status *rx_status,
					u32 rate_n_flags, int energy_a,
					int energy_b)
{
	int max_energy;
	u32 rate_flags = rate_n_flags;

	energy_a = energy_a ? -energy_a : S8_MIN;
	energy_b = energy_b ? -energy_b : S8_MIN;
	max_energy = max(energy_a, energy_b);

	IWL_DEBUG_STATS(mvm, "energy In A %d B %d, and max %d\n",
			energy_a, energy_b, max_energy);

	rx_status->signal = max_energy;
	rx_status->chains =
		(rate_flags & RATE_MCS_ANT_AB_MSK) >> RATE_MCS_ANT_POS;
	rx_status->chain_signal[0] = energy_a;
	rx_status->chain_signal[1] = energy_b;
}

static int iwl_mvm_rx_mgmt_prot(struct ieee80211_sta *sta,
				struct ieee80211_hdr *hdr,
				struct iwl_rx_mpdu_desc *desc,
				u32 status,
				struct ieee80211_rx_status *stats)
{
	struct wireless_dev *wdev;
	struct iwl_mvm_sta *mvmsta;
	struct iwl_mvm_vif *mvmvif;
	u8 keyid;
	struct ieee80211_key_conf *key;
	u32 len = le16_to_cpu(desc->mpdu_len);
	const u8 *frame = (void *)hdr;

	if ((status & IWL_RX_MPDU_STATUS_SEC_MASK) == IWL_RX_MPDU_STATUS_SEC_NONE)
		return 0;

	/*
	 * For non-beacon, we don't really care. But beacons may
	 * be filtered out, and we thus need the firmware's replay
	 * detection, otherwise beacons the firmware previously
	 * filtered could be replayed, or something like that, and
	 * it can filter a lot - though usually only if nothing has
	 * changed.
	 */
	if (!ieee80211_is_beacon(hdr->frame_control))
		return 0;

	if (!sta)
		return -1;

	mvmsta = iwl_mvm_sta_from_mac80211(sta);
	mvmvif = iwl_mvm_vif_from_mac80211(mvmsta->vif);

	/* key mismatch - will also report !MIC_OK but we shouldn't count it */
	if (!(status & IWL_RX_MPDU_STATUS_KEY_VALID))
		goto report;

	/* good cases */
	if (likely(status & IWL_RX_MPDU_STATUS_MIC_OK &&
		   !(status & IWL_RX_MPDU_STATUS_REPLAY_ERROR))) {
		stats->flag |= RX_FLAG_DECRYPTED;
		return 0;
	}

	/*
	 * both keys will have the same cipher and MIC length, use
	 * whichever one is available
	 */
	key = rcu_dereference(mvmvif->bcn_prot.keys[0]);
	if (!key) {
		key = rcu_dereference(mvmvif->bcn_prot.keys[1]);
		if (!key)
			goto report;
	}

	if (len < key->icv_len + IEEE80211_GMAC_PN_LEN + 2)
		goto report;

	/* get the real key ID */
	keyid = frame[len - key->icv_len - IEEE80211_GMAC_PN_LEN - 2];
	/* and if that's the other key, look it up */
	if (keyid != key->keyidx) {
		/*
		 * shouldn't happen since firmware checked, but be safe
		 * in case the MIC length is wrong too, for example
		 */
		if (keyid != 6 && keyid != 7)
			return -1;
		key = rcu_dereference(mvmvif->bcn_prot.keys[keyid - 6]);
		if (!key)
			goto report;
	}

	/* Report status to mac80211 */
	if (!(status & IWL_RX_MPDU_STATUS_MIC_OK))
		ieee80211_key_mic_failure(key);
	else if (status & IWL_RX_MPDU_STATUS_REPLAY_ERROR)
		ieee80211_key_replay(key);
report:
	wdev = ieee80211_vif_to_wdev(mvmsta->vif);
	if (wdev->netdev)
		cfg80211_rx_unprot_mlme_mgmt(wdev->netdev, (void *)hdr, len);

	return -1;
}

static int iwl_mvm_rx_crypto(struct iwl_mvm *mvm, struct ieee80211_sta *sta,
			     struct ieee80211_hdr *hdr,
			     struct ieee80211_rx_status *stats, u16 phy_info,
			     struct iwl_rx_mpdu_desc *desc,
			     u32 pkt_flags, int queue, u8 *crypt_len)
{
	u32 status = le32_to_cpu(desc->status);

	/*
	 * Drop UNKNOWN frames in aggregation, unless in monitor mode
	 * (where we don't have the keys).
	 * We limit this to aggregation because in TKIP this is a valid
	 * scenario, since we may not have the (correct) TTAK (phase 1
	 * key) in the firmware.
	 */
	if (phy_info & IWL_RX_MPDU_PHY_AMPDU &&
	    (status & IWL_RX_MPDU_STATUS_SEC_MASK) ==
	    IWL_RX_MPDU_STATUS_SEC_UNKNOWN && !mvm->monitor_on) {
		IWL_DEBUG_DROP(mvm, "Dropping packets, bad enc status\n");
		return -1;
	}

	if (unlikely(ieee80211_is_mgmt(hdr->frame_control) &&
		     !ieee80211_has_protected(hdr->frame_control)))
		return iwl_mvm_rx_mgmt_prot(sta, hdr, desc, status, stats);

	if (!ieee80211_has_protected(hdr->frame_control) ||
	    (status & IWL_RX_MPDU_STATUS_SEC_MASK) ==
	    IWL_RX_MPDU_STATUS_SEC_NONE)
		return 0;

	/* TODO: handle packets encrypted with unknown alg */

	switch (status & IWL_RX_MPDU_STATUS_SEC_MASK) {
	case IWL_RX_MPDU_STATUS_SEC_CCM:
	case IWL_RX_MPDU_STATUS_SEC_GCM:
		BUILD_BUG_ON(IEEE80211_CCMP_PN_LEN != IEEE80211_GCMP_PN_LEN);
		/* alg is CCM: check MIC only */
		if (!(status & IWL_RX_MPDU_STATUS_MIC_OK)) {
			IWL_DEBUG_DROP(mvm,
				       "Dropping packet, bad MIC (CCM/GCM)\n");
			return -1;
		}

		stats->flag |= RX_FLAG_DECRYPTED | RX_FLAG_MIC_STRIPPED;
		*crypt_len = IEEE80211_CCMP_HDR_LEN;
		return 0;
	case IWL_RX_MPDU_STATUS_SEC_TKIP:
		/* Don't drop the frame and decrypt it in SW */
		if (!fw_has_api(&mvm->fw->ucode_capa,
				IWL_UCODE_TLV_API_DEPRECATE_TTAK) &&
		    !(status & IWL_RX_MPDU_RES_STATUS_TTAK_OK))
			return 0;

		if (mvm->trans->trans_cfg->gen2 &&
		    !(status & RX_MPDU_RES_STATUS_MIC_OK))
			stats->flag |= RX_FLAG_MMIC_ERROR;

		*crypt_len = IEEE80211_TKIP_IV_LEN;
		fallthrough;
	case IWL_RX_MPDU_STATUS_SEC_WEP:
		if (!(status & IWL_RX_MPDU_STATUS_ICV_OK))
			return -1;

		stats->flag |= RX_FLAG_DECRYPTED;
		if ((status & IWL_RX_MPDU_STATUS_SEC_MASK) ==
				IWL_RX_MPDU_STATUS_SEC_WEP)
			*crypt_len = IEEE80211_WEP_IV_LEN;

		if (pkt_flags & FH_RSCSR_RADA_EN) {
			stats->flag |= RX_FLAG_ICV_STRIPPED;
			if (mvm->trans->trans_cfg->gen2)
				stats->flag |= RX_FLAG_MMIC_STRIPPED;
		}

		return 0;
	case IWL_RX_MPDU_STATUS_SEC_EXT_ENC:
		if (!(status & IWL_RX_MPDU_STATUS_MIC_OK))
			return -1;
		stats->flag |= RX_FLAG_DECRYPTED;
		return 0;
	case RX_MPDU_RES_STATUS_SEC_CMAC_GMAC_ENC:
		break;
	default:
		/*
		 * Sometimes we can get frames that were not decrypted
		 * because the firmware didn't have the keys yet. This can
		 * happen after connection where we can get multicast frames
		 * before the GTK is installed.
		 * Silently drop those frames.
		 * Also drop un-decrypted frames in monitor mode.
		 */
		if (!is_multicast_ether_addr(hdr->addr1) &&
		    !mvm->monitor_on && net_ratelimit())
			IWL_WARN(mvm, "Unhandled alg: 0x%x\n", status);
	}

	return 0;
}

static void iwl_mvm_rx_csum(struct iwl_mvm *mvm,
			    struct ieee80211_sta *sta,
			    struct sk_buff *skb,
			    struct iwl_rx_packet *pkt)
{
	struct iwl_rx_mpdu_desc *desc = (void *)pkt->data;

	if (mvm->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		if (pkt->len_n_flags & cpu_to_le32(FH_RSCSR_RPA_EN)) {
			u16 hwsum = be16_to_cpu(desc->v3.raw_xsum);

			skb->ip_summed = CHECKSUM_COMPLETE;
			skb->csum = csum_unfold(~(__force __sum16)hwsum);
		}
	} else {
		struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
		struct iwl_mvm_vif *mvmvif;
		u16 flags = le16_to_cpu(desc->l3l4_flags);
		u8 l3_prot = (u8)((flags & IWL_RX_L3L4_L3_PROTO_MASK) >>
				  IWL_RX_L3_PROTO_POS);

		mvmvif = iwl_mvm_vif_from_mac80211(mvmsta->vif);

		if (mvmvif->features & NETIF_F_RXCSUM &&
		    flags & IWL_RX_L3L4_TCP_UDP_CSUM_OK &&
		    (flags & IWL_RX_L3L4_IP_HDR_CSUM_OK ||
		     l3_prot == IWL_RX_L3_TYPE_IPV6 ||
		     l3_prot == IWL_RX_L3_TYPE_IPV6_FRAG))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
}

/*
 * returns true if a packet is a duplicate or invalid tid and should be dropped.
 * Updates AMSDU PN tracking info
 */
static bool iwl_mvm_is_dup(struct ieee80211_sta *sta, int queue,
			   struct ieee80211_rx_status *rx_status,
			   struct ieee80211_hdr *hdr,
			   struct iwl_rx_mpdu_desc *desc)
{
	struct iwl_mvm_sta *mvm_sta;
	struct iwl_mvm_rxq_dup_data *dup_data;
	u8 tid, sub_frame_idx;

	if (WARN_ON(IS_ERR_OR_NULL(sta)))
		return false;

	mvm_sta = iwl_mvm_sta_from_mac80211(sta);

	if (WARN_ON_ONCE(!mvm_sta->dup_data))
		return false;

	dup_data = &mvm_sta->dup_data[queue];

	/*
	 * Drop duplicate 802.11 retransmissions
	 * (IEEE 802.11-2012: 9.3.2.10 "Duplicate detection and recovery")
	 */
	if (ieee80211_is_ctl(hdr->frame_control) ||
	    ieee80211_is_any_nullfunc(hdr->frame_control) ||
	    is_multicast_ether_addr(hdr->addr1))
		return false;

	if (ieee80211_is_data_qos(hdr->frame_control)) {
		/* frame has qos control */
		tid = ieee80211_get_tid(hdr);
		if (tid >= IWL_MAX_TID_COUNT)
			return true;
	} else {
		tid = IWL_MAX_TID_COUNT;
	}

	/* If this wasn't a part of an A-MSDU the sub-frame index will be 0 */
	sub_frame_idx = desc->amsdu_info &
		IWL_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK;

	if (unlikely(ieee80211_has_retry(hdr->frame_control) &&
		     dup_data->last_seq[tid] == hdr->seq_ctrl &&
		     dup_data->last_sub_frame[tid] >= sub_frame_idx))
		return true;

	/* Allow same PN as the first subframe for following sub frames */
	if (dup_data->last_seq[tid] == hdr->seq_ctrl &&
	    sub_frame_idx > dup_data->last_sub_frame[tid] &&
	    desc->mac_flags2 & IWL_RX_MPDU_MFLG2_AMSDU)
		rx_status->flag |= RX_FLAG_ALLOW_SAME_PN;

	dup_data->last_seq[tid] = hdr->seq_ctrl;
	dup_data->last_sub_frame[tid] = sub_frame_idx;

	rx_status->flag |= RX_FLAG_DUP_VALIDATED;

	return false;
}

static void iwl_mvm_release_frames(struct iwl_mvm *mvm,
				   struct ieee80211_sta *sta,
				   struct napi_struct *napi,
				   struct iwl_mvm_baid_data *baid_data,
				   struct iwl_mvm_reorder_buffer *reorder_buf,
				   u16 nssn)
{
	struct iwl_mvm_reorder_buf_entry *entries =
		&baid_data->entries[reorder_buf->queue *
				    baid_data->entries_per_queue];
	u16 ssn = reorder_buf->head_sn;

	lockdep_assert_held(&reorder_buf->lock);

	while (ieee80211_sn_less(ssn, nssn)) {
		int index = ssn % baid_data->buf_size;
		struct sk_buff_head *skb_list = &entries[index].frames;
		struct sk_buff *skb;

		ssn = ieee80211_sn_inc(ssn);

		/*
		 * Empty the list. Will have more than one frame for A-MSDU.
		 * Empty list is valid as well since nssn indicates frames were
		 * received.
		 */
		while ((skb = __skb_dequeue(skb_list))) {
			iwl_mvm_pass_packet_to_mac80211(mvm, napi, skb,
							reorder_buf->queue,
							sta);
			reorder_buf->num_stored--;
		}
	}
	reorder_buf->head_sn = nssn;
}

static void iwl_mvm_del_ba(struct iwl_mvm *mvm, int queue,
			   struct iwl_mvm_delba_data *data)
{
	struct iwl_mvm_baid_data *ba_data;
	struct ieee80211_sta *sta;
	struct iwl_mvm_reorder_buffer *reorder_buf;
	u8 baid = data->baid;
	u32 sta_id;

	if (WARN_ONCE(baid >= IWL_MAX_BAID, "invalid BAID: %x\n", baid))
		return;

	rcu_read_lock();

	ba_data = rcu_dereference(mvm->baid_map[baid]);
	if (WARN_ON_ONCE(!ba_data))
		goto out;

	/* pick any STA ID to find the pointer */
	sta_id = ffs(ba_data->sta_mask) - 1;
	sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);
	if (WARN_ON_ONCE(IS_ERR_OR_NULL(sta)))
		goto out;

	reorder_buf = &ba_data->reorder_buf[queue];

	/* release all frames that are in the reorder buffer to the stack */
	spin_lock_bh(&reorder_buf->lock);
	iwl_mvm_release_frames(mvm, sta, NULL, ba_data, reorder_buf,
			       ieee80211_sn_add(reorder_buf->head_sn,
						ba_data->buf_size));
	spin_unlock_bh(&reorder_buf->lock);

out:
	rcu_read_unlock();
}

static void iwl_mvm_release_frames_from_notif(struct iwl_mvm *mvm,
					      struct napi_struct *napi,
					      u8 baid, u16 nssn, int queue)
{
	struct ieee80211_sta *sta;
	struct iwl_mvm_reorder_buffer *reorder_buf;
	struct iwl_mvm_baid_data *ba_data;
	u32 sta_id;

	IWL_DEBUG_HT(mvm, "Frame release notification for BAID %u, NSSN %d\n",
		     baid, nssn);

	if (IWL_FW_CHECK(mvm,
			 baid == IWL_RX_REORDER_DATA_INVALID_BAID ||
			 baid >= ARRAY_SIZE(mvm->baid_map),
			 "invalid BAID from FW: %d\n", baid))
		return;

	rcu_read_lock();

	ba_data = rcu_dereference(mvm->baid_map[baid]);
	if (!ba_data) {
		IWL_DEBUG_RX(mvm,
			     "Got valid BAID %d but not allocated, invalid frame release!\n",
			     baid);
		goto out;
	}

	/* pick any STA ID to find the pointer */
	sta_id = ffs(ba_data->sta_mask) - 1;
	sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);
	if (WARN_ON_ONCE(IS_ERR_OR_NULL(sta)))
		goto out;

	reorder_buf = &ba_data->reorder_buf[queue];

	spin_lock_bh(&reorder_buf->lock);
	iwl_mvm_release_frames(mvm, sta, napi, ba_data,
			       reorder_buf, nssn);
	spin_unlock_bh(&reorder_buf->lock);

out:
	rcu_read_unlock();
}

void iwl_mvm_rx_queue_notif(struct iwl_mvm *mvm, struct napi_struct *napi,
			    struct iwl_rx_cmd_buffer *rxb, int queue)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_rxq_sync_notification *notif;
	struct iwl_mvm_internal_rxq_notif *internal_notif;
	u32 len = iwl_rx_packet_payload_len(pkt);

	notif = (void *)pkt->data;
	internal_notif = (void *)notif->payload;

	if (WARN_ONCE(len < sizeof(*notif) + sizeof(*internal_notif),
		      "invalid notification size %d (%d)",
		      len, (int)(sizeof(*notif) + sizeof(*internal_notif))))
		return;
	len -= sizeof(*notif) + sizeof(*internal_notif);

	if (WARN_ONCE(internal_notif->sync &&
		      mvm->queue_sync_cookie != internal_notif->cookie,
		      "Received expired RX queue sync message (cookie %d but wanted %d, queue %d)\n",
		      internal_notif->cookie, mvm->queue_sync_cookie, queue))
		return;

	switch (internal_notif->type) {
	case IWL_MVM_RXQ_EMPTY:
		WARN_ONCE(len, "invalid empty notification size %d", len);
		break;
	case IWL_MVM_RXQ_NOTIF_DEL_BA:
		if (WARN_ONCE(len != sizeof(struct iwl_mvm_delba_data),
			      "invalid delba notification size %d (%d)",
			      len, (int)sizeof(struct iwl_mvm_delba_data)))
			break;
		iwl_mvm_del_ba(mvm, queue, (void *)internal_notif->data);
		break;
	default:
		WARN_ONCE(1, "Invalid identifier %d", internal_notif->type);
	}

	if (internal_notif->sync) {
		WARN_ONCE(!test_and_clear_bit(queue, &mvm->queue_sync_state),
			  "queue sync: queue %d responded a second time!\n",
			  queue);
		if (READ_ONCE(mvm->queue_sync_state) == 0)
			wake_up(&mvm->rx_sync_waitq);
	}
}

/*
 * Returns true if the MPDU was buffered\dropped, false if it should be passed
 * to upper layer.
 */
static bool iwl_mvm_reorder(struct iwl_mvm *mvm,
			    struct napi_struct *napi,
			    int queue,
			    struct ieee80211_sta *sta,
			    struct sk_buff *skb,
			    struct iwl_rx_mpdu_desc *desc)
{
	struct ieee80211_hdr *hdr = (void *)skb_mac_header(skb);
	struct iwl_mvm_baid_data *baid_data;
	struct iwl_mvm_reorder_buffer *buffer;
	u32 reorder = le32_to_cpu(desc->reorder_data);
	bool amsdu = desc->mac_flags2 & IWL_RX_MPDU_MFLG2_AMSDU;
	bool last_subframe =
		desc->amsdu_info & IWL_RX_MPDU_AMSDU_LAST_SUBFRAME;
	u8 tid = ieee80211_get_tid(hdr);
	struct iwl_mvm_reorder_buf_entry *entries;
	u32 sta_mask;
	int index;
	u16 nssn, sn;
	u8 baid;

	baid = (reorder & IWL_RX_MPDU_REORDER_BAID_MASK) >>
		IWL_RX_MPDU_REORDER_BAID_SHIFT;

	if (mvm->trans->trans_cfg->device_family == IWL_DEVICE_FAMILY_9000)
		return false;

	/*
	 * This also covers the case of receiving a Block Ack Request
	 * outside a BA session; we'll pass it to mac80211 and that
	 * then sends a delBA action frame.
	 * This also covers pure monitor mode, in which case we won't
	 * have any BA sessions.
	 */
	if (baid == IWL_RX_REORDER_DATA_INVALID_BAID)
		return false;

	/* no sta yet */
	if (WARN_ONCE(IS_ERR_OR_NULL(sta),
		      "Got valid BAID without a valid station assigned\n"))
		return false;

	/* not a data packet or a bar */
	if (!ieee80211_is_back_req(hdr->frame_control) &&
	    (!ieee80211_is_data_qos(hdr->frame_control) ||
	     is_multicast_ether_addr(hdr->addr1)))
		return false;

	if (unlikely(!ieee80211_is_data_present(hdr->frame_control)))
		return false;

	baid_data = rcu_dereference(mvm->baid_map[baid]);
	if (!baid_data) {
		IWL_DEBUG_RX(mvm,
			     "Got valid BAID but no baid allocated, bypass the re-ordering buffer. Baid %d reorder 0x%x\n",
			      baid, reorder);
		return false;
	}

	sta_mask = iwl_mvm_sta_fw_id_mask(mvm, sta, -1);

	if (IWL_FW_CHECK(mvm,
			 tid != baid_data->tid ||
			 !(sta_mask & baid_data->sta_mask),
			 "baid 0x%x is mapped to sta_mask:0x%x tid:%d, but was received for sta_mask:0x%x tid:%d\n",
			 baid, baid_data->sta_mask, baid_data->tid,
			 sta_mask, tid))
		return false;

	nssn = reorder & IWL_RX_MPDU_REORDER_NSSN_MASK;
	sn = (reorder & IWL_RX_MPDU_REORDER_SN_MASK) >>
		IWL_RX_MPDU_REORDER_SN_SHIFT;

	buffer = &baid_data->reorder_buf[queue];
	entries = &baid_data->entries[queue * baid_data->entries_per_queue];

	spin_lock_bh(&buffer->lock);

	if (!buffer->valid) {
		if (reorder & IWL_RX_MPDU_REORDER_BA_OLD_SN) {
			spin_unlock_bh(&buffer->lock);
			return false;
		}
		buffer->valid = true;
	}

	/* drop any duplicated packets */
	if (desc->status & cpu_to_le32(IWL_RX_MPDU_STATUS_DUPLICATE))
		goto drop;

	/* drop any oudated packets */
	if (reorder & IWL_RX_MPDU_REORDER_BA_OLD_SN)
		goto drop;

	/* release immediately if allowed by nssn and no stored frames */
	if (!buffer->num_stored && ieee80211_sn_less(sn, nssn)) {
		if (!amsdu || last_subframe)
			buffer->head_sn = nssn;

		spin_unlock_bh(&buffer->lock);
		return false;
	}

	/*
	 * release immediately if there are no stored frames, and the sn is
	 * equal to the head.
	 * This can happen due to reorder timer, where NSSN is behind head_sn.
	 * When we released everything, and we got the next frame in the
	 * sequence, according to the NSSN we can't release immediately,
	 * while technically there is no hole and we can move forward.
	 */
	if (!buffer->num_stored && sn == buffer->head_sn) {
		if (!amsdu || last_subframe)
			buffer->head_sn = ieee80211_sn_inc(buffer->head_sn);

		spin_unlock_bh(&buffer->lock);
		return false;
	}

	/* put in reorder buffer */
	index = sn % baid_data->buf_size;
	__skb_queue_tail(&entries[index].frames, skb);
	buffer->num_stored++;

	/*
	 * We cannot trust NSSN for AMSDU sub-frames that are not the last.
	 * The reason is that NSSN advances on the first sub-frame, and may
	 * cause the reorder buffer to advance before all the sub-frames arrive.
	 * Example: reorder buffer contains SN 0 & 2, and we receive AMSDU with
	 * SN 1. NSSN for first sub frame will be 3 with the result of driver
	 * releasing SN 0,1, 2. When sub-frame 1 arrives - reorder buffer is
	 * already ahead and it will be dropped.
	 * If the last sub-frame is not on this queue - we will get frame
	 * release notification with up to date NSSN.
	 */
	if (!amsdu || last_subframe)
		iwl_mvm_release_frames(mvm, sta, napi, baid_data,
				       buffer, nssn);

	spin_unlock_bh(&buffer->lock);
	return true;

drop:
	kfree_skb(skb);
	spin_unlock_bh(&buffer->lock);
	return true;
}

static void iwl_mvm_agg_rx_received(struct iwl_mvm *mvm,
				    u32 reorder_data, u8 baid)
{
	unsigned long now = jiffies;
	unsigned long timeout;
	struct iwl_mvm_baid_data *data;

	rcu_read_lock();

	data = rcu_dereference(mvm->baid_map[baid]);
	if (!data) {
		IWL_DEBUG_RX(mvm,
			     "Got valid BAID but no baid allocated, bypass the re-ordering buffer. Baid %d reorder 0x%x\n",
			      baid, reorder_data);
		goto out;
	}

	if (!data->timeout)
		goto out;

	timeout = data->timeout;
	/*
	 * Do not update last rx all the time to avoid cache bouncing
	 * between the rx queues.
	 * Update it every timeout. Worst case is the session will
	 * expire after ~ 2 * timeout, which doesn't matter that much.
	 */
	if (time_before(data->last_rx + TU_TO_JIFFIES(timeout), now))
		/* Update is atomic */
		data->last_rx = now;

out:
	rcu_read_unlock();
}

static void iwl_mvm_flip_address(u8 *addr)
{
	int i;
	u8 mac_addr[ETH_ALEN];

	for (i = 0; i < ETH_ALEN; i++)
		mac_addr[i] = addr[ETH_ALEN - i - 1];
	ether_addr_copy(addr, mac_addr);
}

struct iwl_mvm_rx_phy_data {
	enum iwl_rx_phy_info_type info_type;
	__le32 d0, d1, d2, d3, eht_d4, d5;
	__le16 d4;
	bool with_data;
	bool first_subframe;
	__le32 rx_vec[4];

	u32 rate_n_flags;
	u32 gp2_on_air_rise;
	u16 phy_info;
	u8 energy_a, energy_b;
	u8 channel;
};

static void iwl_mvm_decode_he_mu_ext(struct iwl_mvm *mvm,
				     struct iwl_mvm_rx_phy_data *phy_data,
				     struct ieee80211_radiotap_he_mu *he_mu)
{
	u32 phy_data2 = le32_to_cpu(phy_data->d2);
	u32 phy_data3 = le32_to_cpu(phy_data->d3);
	u16 phy_data4 = le16_to_cpu(phy_data->d4);
	u32 rate_n_flags = phy_data->rate_n_flags;

	if (FIELD_GET(IWL_RX_PHY_DATA4_HE_MU_EXT_CH1_CRC_OK, phy_data4)) {
		he_mu->flags1 |=
			cpu_to_le16(IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_RU_KNOWN |
				    IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_CTR_26T_RU_KNOWN);

		he_mu->flags1 |=
			le16_encode_bits(FIELD_GET(IWL_RX_PHY_DATA4_HE_MU_EXT_CH1_CTR_RU,
						   phy_data4),
					 IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_CTR_26T_RU);

		he_mu->ru_ch1[0] = FIELD_GET(IWL_RX_PHY_DATA2_HE_MU_EXT_CH1_RU0,
					     phy_data2);
		he_mu->ru_ch1[1] = FIELD_GET(IWL_RX_PHY_DATA3_HE_MU_EXT_CH1_RU1,
					     phy_data3);
		he_mu->ru_ch1[2] = FIELD_GET(IWL_RX_PHY_DATA2_HE_MU_EXT_CH1_RU2,
					     phy_data2);
		he_mu->ru_ch1[3] = FIELD_GET(IWL_RX_PHY_DATA3_HE_MU_EXT_CH1_RU3,
					     phy_data3);
	}

	if (FIELD_GET(IWL_RX_PHY_DATA4_HE_MU_EXT_CH2_CRC_OK, phy_data4) &&
	    (rate_n_flags & RATE_MCS_CHAN_WIDTH_MSK_V1) != RATE_MCS_CHAN_WIDTH_20) {
		he_mu->flags1 |=
			cpu_to_le16(IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH2_RU_KNOWN |
				    IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH2_CTR_26T_RU_KNOWN);

		he_mu->flags2 |=
			le16_encode_bits(FIELD_GET(IWL_RX_PHY_DATA4_HE_MU_EXT_CH2_CTR_RU,
						   phy_data4),
					 IEEE80211_RADIOTAP_HE_MU_FLAGS2_CH2_CTR_26T_RU);

		he_mu->ru_ch2[0] = FIELD_GET(IWL_RX_PHY_DATA2_HE_MU_EXT_CH2_RU0,
					     phy_data2);
		he_mu->ru_ch2[1] = FIELD_GET(IWL_RX_PHY_DATA3_HE_MU_EXT_CH2_RU1,
					     phy_data3);
		he_mu->ru_ch2[2] = FIELD_GET(IWL_RX_PHY_DATA2_HE_MU_EXT_CH2_RU2,
					     phy_data2);
		he_mu->ru_ch2[3] = FIELD_GET(IWL_RX_PHY_DATA3_HE_MU_EXT_CH2_RU3,
					     phy_data3);
	}
}

static void
iwl_mvm_decode_he_phy_ru_alloc(struct iwl_mvm_rx_phy_data *phy_data,
			       struct ieee80211_radiotap_he *he,
			       struct ieee80211_radiotap_he_mu *he_mu,
			       struct ieee80211_rx_status *rx_status)
{
	/*
	 * Unfortunately, we have to leave the mac80211 data
	 * incorrect for the case that we receive an HE-MU
	 * transmission and *don't* have the HE phy data (due
	 * to the bits being used for TSF). This shouldn't
	 * happen though as management frames where we need
	 * the TSF/timers are not be transmitted in HE-MU.
	 */
	u8 ru = le32_get_bits(phy_data->d1, IWL_RX_PHY_DATA1_HE_RU_ALLOC_MASK);
	u32 rate_n_flags = phy_data->rate_n_flags;
	u32 he_type = rate_n_flags & RATE_MCS_HE_TYPE_MSK;
	u8 offs = 0;

	rx_status->bw = RATE_INFO_BW_HE_RU;

	he->data1 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN);

	switch (ru) {
	case 0 ... 36:
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_26;
		offs = ru;
		break;
	case 37 ... 52:
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_52;
		offs = ru - 37;
		break;
	case 53 ... 60:
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_106;
		offs = ru - 53;
		break;
	case 61 ... 64:
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_242;
		offs = ru - 61;
		break;
	case 65 ... 66:
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_484;
		offs = ru - 65;
		break;
	case 67:
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_996;
		break;
	case 68:
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_2x996;
		break;
	}
	he->data2 |= le16_encode_bits(offs,
				      IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET);
	he->data2 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_PRISEC_80_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET_KNOWN);
	if (phy_data->d1 & cpu_to_le32(IWL_RX_PHY_DATA1_HE_RU_ALLOC_SEC80))
		he->data2 |=
			cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_PRISEC_80_SEC);

#define CHECK_BW(bw) \
	BUILD_BUG_ON(IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_ ## bw ## MHZ != \
		     RATE_MCS_CHAN_WIDTH_##bw >> RATE_MCS_CHAN_WIDTH_POS); \
	BUILD_BUG_ON(IEEE80211_RADIOTAP_HE_DATA6_TB_PPDU_BW_ ## bw ## MHZ != \
		     RATE_MCS_CHAN_WIDTH_##bw >> RATE_MCS_CHAN_WIDTH_POS)
	CHECK_BW(20);
	CHECK_BW(40);
	CHECK_BW(80);
	CHECK_BW(160);

	if (he_mu)
		he_mu->flags2 |=
			le16_encode_bits(FIELD_GET(RATE_MCS_CHAN_WIDTH_MSK,
						   rate_n_flags),
					 IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW);
	else if (he_type == RATE_MCS_HE_TYPE_TRIG)
		he->data6 |=
			cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA6_TB_PPDU_BW_KNOWN) |
			le16_encode_bits(FIELD_GET(RATE_MCS_CHAN_WIDTH_MSK,
						   rate_n_flags),
					 IEEE80211_RADIOTAP_HE_DATA6_TB_PPDU_BW);
}

static void iwl_mvm_decode_he_phy_data(struct iwl_mvm *mvm,
				       struct iwl_mvm_rx_phy_data *phy_data,
				       struct ieee80211_radiotap_he *he,
				       struct ieee80211_radiotap_he_mu *he_mu,
				       struct ieee80211_rx_status *rx_status,
				       int queue)
{
	switch (phy_data->info_type) {
	case IWL_RX_PHY_INFO_TYPE_NONE:
	case IWL_RX_PHY_INFO_TYPE_CCK:
	case IWL_RX_PHY_INFO_TYPE_OFDM_LGCY:
	case IWL_RX_PHY_INFO_TYPE_HT:
	case IWL_RX_PHY_INFO_TYPE_VHT_SU:
	case IWL_RX_PHY_INFO_TYPE_VHT_MU:
	case IWL_RX_PHY_INFO_TYPE_EHT_MU:
	case IWL_RX_PHY_INFO_TYPE_EHT_TB:
	case IWL_RX_PHY_INFO_TYPE_EHT_MU_EXT:
	case IWL_RX_PHY_INFO_TYPE_EHT_TB_EXT:
		return;
	case IWL_RX_PHY_INFO_TYPE_HE_TB_EXT:
		he->data1 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE_KNOWN |
					 IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE2_KNOWN |
					 IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE3_KNOWN |
					 IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE4_KNOWN);
		he->data4 |= le16_encode_bits(le32_get_bits(phy_data->d2,
							    IWL_RX_PHY_DATA2_HE_TB_EXT_SPTL_REUSE1),
					      IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE1);
		he->data4 |= le16_encode_bits(le32_get_bits(phy_data->d2,
							    IWL_RX_PHY_DATA2_HE_TB_EXT_SPTL_REUSE2),
					      IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE2);
		he->data4 |= le16_encode_bits(le32_get_bits(phy_data->d2,
							    IWL_RX_PHY_DATA2_HE_TB_EXT_SPTL_REUSE3),
					      IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE3);
		he->data4 |= le16_encode_bits(le32_get_bits(phy_data->d2,
							    IWL_RX_PHY_DATA2_HE_TB_EXT_SPTL_REUSE4),
					      IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE4);
		fallthrough;
	case IWL_RX_PHY_INFO_TYPE_HE_SU:
	case IWL_RX_PHY_INFO_TYPE_HE_MU:
	case IWL_RX_PHY_INFO_TYPE_HE_MU_EXT:
	case IWL_RX_PHY_INFO_TYPE_HE_TB:
		/* HE common */
		he->data1 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_LDPC_XSYMSEG_KNOWN |
					 IEEE80211_RADIOTAP_HE_DATA1_DOPPLER_KNOWN |
					 IEEE80211_RADIOTAP_HE_DATA1_BSS_COLOR_KNOWN);
		he->data2 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_PRE_FEC_PAD_KNOWN |
					 IEEE80211_RADIOTAP_HE_DATA2_PE_DISAMBIG_KNOWN |
					 IEEE80211_RADIOTAP_HE_DATA2_TXOP_KNOWN |
					 IEEE80211_RADIOTAP_HE_DATA2_NUM_LTF_SYMS_KNOWN);
		he->data3 |= le16_encode_bits(le32_get_bits(phy_data->d0,
							    IWL_RX_PHY_DATA0_HE_BSS_COLOR_MASK),
					      IEEE80211_RADIOTAP_HE_DATA3_BSS_COLOR);
		if (phy_data->info_type != IWL_RX_PHY_INFO_TYPE_HE_TB &&
		    phy_data->info_type != IWL_RX_PHY_INFO_TYPE_HE_TB_EXT) {
			he->data1 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_UL_DL_KNOWN);
			he->data3 |= le16_encode_bits(le32_get_bits(phy_data->d0,
							    IWL_RX_PHY_DATA0_HE_UPLINK),
						      IEEE80211_RADIOTAP_HE_DATA3_UL_DL);
		}
		he->data3 |= le16_encode_bits(le32_get_bits(phy_data->d0,
							    IWL_RX_PHY_DATA0_HE_LDPC_EXT_SYM),
					      IEEE80211_RADIOTAP_HE_DATA3_LDPC_XSYMSEG);
		he->data5 |= le16_encode_bits(le32_get_bits(phy_data->d0,
							    IWL_RX_PHY_DATA0_HE_PRE_FEC_PAD_MASK),
					      IEEE80211_RADIOTAP_HE_DATA5_PRE_FEC_PAD);
		he->data5 |= le16_encode_bits(le32_get_bits(phy_data->d0,
							    IWL_RX_PHY_DATA0_HE_PE_DISAMBIG),
					      IEEE80211_RADIOTAP_HE_DATA5_PE_DISAMBIG);
		he->data5 |= le16_encode_bits(le32_get_bits(phy_data->d1,
							    IWL_RX_PHY_DATA1_HE_LTF_NUM_MASK),
					      IEEE80211_RADIOTAP_HE_DATA5_NUM_LTF_SYMS);
		he->data6 |= le16_encode_bits(le32_get_bits(phy_data->d0,
							    IWL_RX_PHY_DATA0_HE_TXOP_DUR_MASK),
					      IEEE80211_RADIOTAP_HE_DATA6_TXOP);
		he->data6 |= le16_encode_bits(le32_get_bits(phy_data->d0,
							    IWL_RX_PHY_DATA0_HE_DOPPLER),
					      IEEE80211_RADIOTAP_HE_DATA6_DOPPLER);
		break;
	}

	switch (phy_data->info_type) {
	case IWL_RX_PHY_INFO_TYPE_HE_MU_EXT:
	case IWL_RX_PHY_INFO_TYPE_HE_MU:
	case IWL_RX_PHY_INFO_TYPE_HE_SU:
		he->data1 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE_KNOWN);
		he->data4 |= le16_encode_bits(le32_get_bits(phy_data->d0,
							    IWL_RX_PHY_DATA0_HE_SPATIAL_REUSE_MASK),
					      IEEE80211_RADIOTAP_HE_DATA4_SU_MU_SPTL_REUSE);
		break;
	default:
		/* nothing here */
		break;
	}

	switch (phy_data->info_type) {
	case IWL_RX_PHY_INFO_TYPE_HE_MU_EXT:
		he_mu->flags1 |=
			le16_encode_bits(le16_get_bits(phy_data->d4,
						       IWL_RX_PHY_DATA4_HE_MU_EXT_SIGB_DCM),
					 IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_DCM);
		he_mu->flags1 |=
			le16_encode_bits(le16_get_bits(phy_data->d4,
						       IWL_RX_PHY_DATA4_HE_MU_EXT_SIGB_MCS_MASK),
					 IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_MCS);
		he_mu->flags2 |=
			le16_encode_bits(le16_get_bits(phy_data->d4,
						       IWL_RX_PHY_DATA4_HE_MU_EXT_PREAMBLE_PUNC_TYPE_MASK),
					 IEEE80211_RADIOTAP_HE_MU_FLAGS2_PUNC_FROM_SIG_A_BW);
		iwl_mvm_decode_he_mu_ext(mvm, phy_data, he_mu);
		fallthrough;
	case IWL_RX_PHY_INFO_TYPE_HE_MU:
		he_mu->flags2 |=
			le16_encode_bits(le32_get_bits(phy_data->d1,
						       IWL_RX_PHY_DATA1_HE_MU_SIBG_SYM_OR_USER_NUM_MASK),
					 IEEE80211_RADIOTAP_HE_MU_FLAGS2_SIG_B_SYMS_USERS);
		he_mu->flags2 |=
			le16_encode_bits(le32_get_bits(phy_data->d1,
						       IWL_RX_PHY_DATA1_HE_MU_SIGB_COMPRESSION),
					 IEEE80211_RADIOTAP_HE_MU_FLAGS2_SIG_B_COMP);
		fallthrough;
	case IWL_RX_PHY_INFO_TYPE_HE_TB:
	case IWL_RX_PHY_INFO_TYPE_HE_TB_EXT:
		iwl_mvm_decode_he_phy_ru_alloc(phy_data, he, he_mu, rx_status);
		break;
	case IWL_RX_PHY_INFO_TYPE_HE_SU:
		he->data1 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_BEAM_CHANGE_KNOWN);
		he->data3 |= le16_encode_bits(le32_get_bits(phy_data->d0,
							    IWL_RX_PHY_DATA0_HE_BEAM_CHNG),
					      IEEE80211_RADIOTAP_HE_DATA3_BEAM_CHANGE);
		break;
	default:
		/* nothing */
		break;
	}
}

#define LE32_DEC_ENC(value, dec_bits, enc_bits) \
	le32_encode_bits(le32_get_bits(value, dec_bits), enc_bits)

#define IWL_MVM_ENC_USIG_VALUE_MASK(usig, in_value, dec_bits, enc_bits) do { \
	typeof(enc_bits) _enc_bits = enc_bits; \
	typeof(usig) _usig = usig; \
	(_usig)->mask |= cpu_to_le32(_enc_bits); \
	(_usig)->value |= LE32_DEC_ENC(in_value, dec_bits, _enc_bits); \
} while (0)

#define __IWL_MVM_ENC_EHT_RU(rt_data, rt_ru, fw_data, fw_ru) \
	eht->data[(rt_data)] |= \
		(cpu_to_le32 \
		 (IEEE80211_RADIOTAP_EHT_DATA ## rt_data ## _RU_ALLOC_CC_ ## rt_ru ## _KNOWN) | \
		 LE32_DEC_ENC(data ## fw_data, \
			      IWL_RX_PHY_DATA ## fw_data ## _EHT_MU_EXT_RU_ALLOC_ ## fw_ru, \
			      IEEE80211_RADIOTAP_EHT_DATA ## rt_data ## _RU_ALLOC_CC_ ## rt_ru))

#define _IWL_MVM_ENC_EHT_RU(rt_data, rt_ru, fw_data, fw_ru)	\
	__IWL_MVM_ENC_EHT_RU(rt_data, rt_ru, fw_data, fw_ru)

#define IEEE80211_RADIOTAP_RU_DATA_1_1_1	1
#define IEEE80211_RADIOTAP_RU_DATA_2_1_1	2
#define IEEE80211_RADIOTAP_RU_DATA_1_1_2	2
#define IEEE80211_RADIOTAP_RU_DATA_2_1_2	2
#define IEEE80211_RADIOTAP_RU_DATA_1_2_1	3
#define IEEE80211_RADIOTAP_RU_DATA_2_2_1	3
#define IEEE80211_RADIOTAP_RU_DATA_1_2_2	3
#define IEEE80211_RADIOTAP_RU_DATA_2_2_2	4

#define IWL_RX_RU_DATA_A1			2
#define IWL_RX_RU_DATA_A2			2
#define IWL_RX_RU_DATA_B1			2
#define IWL_RX_RU_DATA_B2			4
#define IWL_RX_RU_DATA_C1			3
#define IWL_RX_RU_DATA_C2			3
#define IWL_RX_RU_DATA_D1			4
#define IWL_RX_RU_DATA_D2			4

#define IWL_MVM_ENC_EHT_RU(rt_ru, fw_ru)				\
	_IWL_MVM_ENC_EHT_RU(IEEE80211_RADIOTAP_RU_DATA_ ## rt_ru,	\
			    rt_ru,					\
			    IWL_RX_RU_DATA_ ## fw_ru,			\
			    fw_ru)

static void iwl_mvm_decode_eht_ext_mu(struct iwl_mvm *mvm,
				      struct iwl_mvm_rx_phy_data *phy_data,
				      struct ieee80211_rx_status *rx_status,
				      struct ieee80211_radiotap_eht *eht,
				      struct ieee80211_radiotap_eht_usig *usig)
{
	if (phy_data->with_data) {
		__le32 data1 = phy_data->d1;
		__le32 data2 = phy_data->d2;
		__le32 data3 = phy_data->d3;
		__le32 data4 = phy_data->eht_d4;
		__le32 data5 = phy_data->d5;
		u32 phy_bw = phy_data->rate_n_flags & RATE_MCS_CHAN_WIDTH_MSK;

		IWL_MVM_ENC_USIG_VALUE_MASK(usig, data5,
					    IWL_RX_PHY_DATA5_EHT_TYPE_AND_COMP,
					    IEEE80211_RADIOTAP_EHT_USIG2_MU_B0_B1_PPDU_TYPE);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, data5,
					    IWL_RX_PHY_DATA5_EHT_MU_PUNC_CH_CODE,
					    IEEE80211_RADIOTAP_EHT_USIG2_MU_B3_B7_PUNCTURED_INFO);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, data4,
					    IWL_RX_PHY_DATA4_EHT_MU_EXT_SIGB_MCS,
					    IEEE80211_RADIOTAP_EHT_USIG2_MU_B9_B10_SIG_MCS);
		IWL_MVM_ENC_USIG_VALUE_MASK
			(usig, data1, IWL_RX_PHY_DATA1_EHT_MU_NUM_SIG_SYM_USIGA2,
			 IEEE80211_RADIOTAP_EHT_USIG2_MU_B11_B15_EHT_SIG_SYMBOLS);

		eht->user_info[0] |=
			cpu_to_le32(IEEE80211_RADIOTAP_EHT_USER_INFO_STA_ID_KNOWN) |
			LE32_DEC_ENC(data5, IWL_RX_PHY_DATA5_EHT_MU_STA_ID_USR,
				     IEEE80211_RADIOTAP_EHT_USER_INFO_STA_ID);

		eht->known |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_NR_NON_OFDMA_USERS_M);
		eht->data[7] |= LE32_DEC_ENC
			(data5, IWL_RX_PHY_DATA5_EHT_MU_NUM_USR_NON_OFDMA,
			 IEEE80211_RADIOTAP_EHT_DATA7_NUM_OF_NON_OFDMA_USERS);

		/*
		 * Hardware labels the content channels/RU allocation values
		 * as follows:
		 *           Content Channel 1		Content Channel 2
		 *   20 MHz: A1
		 *   40 MHz: A1				B1
		 *   80 MHz: A1 C1			B1 D1
		 *  160 MHz: A1 C1 A2 C2		B1 D1 B2 D2
		 *  320 MHz: A1 C1 A2 C2 A3 C3 A4 C4	B1 D1 B2 D2 B3 D3 B4 D4
		 *
		 * However firmware can only give us A1-D2, so the higher
		 * frequencies are missing.
		 */

		switch (phy_bw) {
		case RATE_MCS_CHAN_WIDTH_320:
			/* additional values are missing in RX metadata */
		case RATE_MCS_CHAN_WIDTH_160:
			/* content channel 1 */
			IWL_MVM_ENC_EHT_RU(1_2_1, A2);
			IWL_MVM_ENC_EHT_RU(1_2_2, C2);
			/* content channel 2 */
			IWL_MVM_ENC_EHT_RU(2_2_1, B2);
			IWL_MVM_ENC_EHT_RU(2_2_2, D2);
			fallthrough;
		case RATE_MCS_CHAN_WIDTH_80:
			/* content channel 1 */
			IWL_MVM_ENC_EHT_RU(1_1_2, C1);
			/* content channel 2 */
			IWL_MVM_ENC_EHT_RU(2_1_2, D1);
			fallthrough;
		case RATE_MCS_CHAN_WIDTH_40:
			/* content channel 2 */
			IWL_MVM_ENC_EHT_RU(2_1_1, B1);
			fallthrough;
		case RATE_MCS_CHAN_WIDTH_20:
			IWL_MVM_ENC_EHT_RU(1_1_1, A1);
			break;
		}
	} else {
		__le32 usig_a1 = phy_data->rx_vec[0];
		__le32 usig_a2 = phy_data->rx_vec[1];

		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a1,
					    IWL_RX_USIG_A1_DISREGARD,
					    IEEE80211_RADIOTAP_EHT_USIG1_MU_B20_B24_DISREGARD);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a1,
					    IWL_RX_USIG_A1_VALIDATE,
					    IEEE80211_RADIOTAP_EHT_USIG1_MU_B25_VALIDATE);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a2,
					    IWL_RX_USIG_A2_EHT_PPDU_TYPE,
					    IEEE80211_RADIOTAP_EHT_USIG2_MU_B0_B1_PPDU_TYPE);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a2,
					    IWL_RX_USIG_A2_EHT_USIG2_VALIDATE_B2,
					    IEEE80211_RADIOTAP_EHT_USIG2_MU_B2_VALIDATE);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a2,
					    IWL_RX_USIG_A2_EHT_PUNC_CHANNEL,
					    IEEE80211_RADIOTAP_EHT_USIG2_MU_B3_B7_PUNCTURED_INFO);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a2,
					    IWL_RX_USIG_A2_EHT_USIG2_VALIDATE_B8,
					    IEEE80211_RADIOTAP_EHT_USIG2_MU_B8_VALIDATE);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a2,
					    IWL_RX_USIG_A2_EHT_SIG_MCS,
					    IEEE80211_RADIOTAP_EHT_USIG2_MU_B9_B10_SIG_MCS);
		IWL_MVM_ENC_USIG_VALUE_MASK
			(usig, usig_a2, IWL_RX_USIG_A2_EHT_SIG_SYM_NUM,
			 IEEE80211_RADIOTAP_EHT_USIG2_MU_B11_B15_EHT_SIG_SYMBOLS);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a2,
					    IWL_RX_USIG_A2_EHT_CRC_OK,
					    IEEE80211_RADIOTAP_EHT_USIG2_MU_B16_B19_CRC);
	}
}

static void iwl_mvm_decode_eht_ext_tb(struct iwl_mvm *mvm,
				      struct iwl_mvm_rx_phy_data *phy_data,
				      struct ieee80211_rx_status *rx_status,
				      struct ieee80211_radiotap_eht *eht,
				      struct ieee80211_radiotap_eht_usig *usig)
{
	if (phy_data->with_data) {
		__le32 data5 = phy_data->d5;

		IWL_MVM_ENC_USIG_VALUE_MASK(usig, data5,
					    IWL_RX_PHY_DATA5_EHT_TYPE_AND_COMP,
					    IEEE80211_RADIOTAP_EHT_USIG2_TB_B0_B1_PPDU_TYPE);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, data5,
					    IWL_RX_PHY_DATA5_EHT_TB_SPATIAL_REUSE1,
					    IEEE80211_RADIOTAP_EHT_USIG2_TB_B3_B6_SPATIAL_REUSE_1);

		IWL_MVM_ENC_USIG_VALUE_MASK(usig, data5,
					    IWL_RX_PHY_DATA5_EHT_TB_SPATIAL_REUSE2,
					    IEEE80211_RADIOTAP_EHT_USIG2_TB_B7_B10_SPATIAL_REUSE_2);
	} else {
		__le32 usig_a1 = phy_data->rx_vec[0];
		__le32 usig_a2 = phy_data->rx_vec[1];

		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a1,
					    IWL_RX_USIG_A1_DISREGARD,
					    IEEE80211_RADIOTAP_EHT_USIG1_TB_B20_B25_DISREGARD);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a2,
					    IWL_RX_USIG_A2_EHT_PPDU_TYPE,
					    IEEE80211_RADIOTAP_EHT_USIG2_TB_B0_B1_PPDU_TYPE);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a2,
					    IWL_RX_USIG_A2_EHT_USIG2_VALIDATE_B2,
					    IEEE80211_RADIOTAP_EHT_USIG2_TB_B2_VALIDATE);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a2,
					    IWL_RX_USIG_A2_EHT_TRIG_SPATIAL_REUSE_1,
					    IEEE80211_RADIOTAP_EHT_USIG2_TB_B3_B6_SPATIAL_REUSE_1);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a2,
					    IWL_RX_USIG_A2_EHT_TRIG_SPATIAL_REUSE_2,
					    IEEE80211_RADIOTAP_EHT_USIG2_TB_B7_B10_SPATIAL_REUSE_2);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a2,
					    IWL_RX_USIG_A2_EHT_TRIG_USIG2_DISREGARD,
					    IEEE80211_RADIOTAP_EHT_USIG2_TB_B11_B15_DISREGARD);
		IWL_MVM_ENC_USIG_VALUE_MASK(usig, usig_a2,
					    IWL_RX_USIG_A2_EHT_CRC_OK,
					    IEEE80211_RADIOTAP_EHT_USIG2_TB_B16_B19_CRC);
	}
}

static void iwl_mvm_decode_eht_ru(struct iwl_mvm *mvm,
				  struct ieee80211_rx_status *rx_status,
				  struct ieee80211_radiotap_eht *eht)
{
	u32 ru = le32_get_bits(eht->data[8],
			       IEEE80211_RADIOTAP_EHT_DATA8_RU_ALLOC_TB_FMT_B7_B1);
	enum nl80211_eht_ru_alloc nl_ru;

	/* Using D1.5 Table 9-53a - Encoding of PS160 and RU Allocation subfields
	 * in an EHT variant User Info field
	 */

	switch (ru) {
	case 0 ... 36:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_26;
		break;
	case 37 ... 52:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_52;
		break;
	case 53 ... 60:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_106;
		break;
	case 61 ... 64:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_242;
		break;
	case 65 ... 66:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_484;
		break;
	case 67:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_996;
		break;
	case 68:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_2x996;
		break;
	case 69:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_4x996;
		break;
	case 70 ... 81:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_52P26;
		break;
	case 82 ... 89:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_106P26;
		break;
	case 90 ... 93:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_484P242;
		break;
	case 94 ... 95:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_996P484;
		break;
	case 96 ... 99:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_996P484P242;
		break;
	case 100 ... 103:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_2x996P484;
		break;
	case 104:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_3x996;
		break;
	case 105 ... 106:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_3x996P484;
		break;
	default:
		return;
	}

	rx_status->bw = RATE_INFO_BW_EHT_RU;
	rx_status->eht.ru = nl_ru;
}

static void iwl_mvm_decode_eht_phy_data(struct iwl_mvm *mvm,
					struct iwl_mvm_rx_phy_data *phy_data,
					struct ieee80211_rx_status *rx_status,
					struct ieee80211_radiotap_eht *eht,
					struct ieee80211_radiotap_eht_usig *usig)

{
	__le32 data0 = phy_data->d0;
	__le32 data1 = phy_data->d1;
	__le32 usig_a1 = phy_data->rx_vec[0];
	u8 info_type = phy_data->info_type;

	/* Not in EHT range */
	if (info_type < IWL_RX_PHY_INFO_TYPE_EHT_MU ||
	    info_type > IWL_RX_PHY_INFO_TYPE_EHT_TB_EXT)
		return;

	usig->common |= cpu_to_le32
		(IEEE80211_RADIOTAP_EHT_USIG_COMMON_UL_DL_KNOWN |
		 IEEE80211_RADIOTAP_EHT_USIG_COMMON_BSS_COLOR_KNOWN);
	if (phy_data->with_data) {
		usig->common |= LE32_DEC_ENC(data0,
					     IWL_RX_PHY_DATA0_EHT_UPLINK,
					     IEEE80211_RADIOTAP_EHT_USIG_COMMON_UL_DL);
		usig->common |= LE32_DEC_ENC(data0,
					     IWL_RX_PHY_DATA0_EHT_BSS_COLOR_MASK,
					     IEEE80211_RADIOTAP_EHT_USIG_COMMON_BSS_COLOR);
	} else {
		usig->common |= LE32_DEC_ENC(usig_a1,
					     IWL_RX_USIG_A1_UL_FLAG,
					     IEEE80211_RADIOTAP_EHT_USIG_COMMON_UL_DL);
		usig->common |= LE32_DEC_ENC(usig_a1,
					     IWL_RX_USIG_A1_BSS_COLOR,
					     IEEE80211_RADIOTAP_EHT_USIG_COMMON_BSS_COLOR);
	}

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_SNIFF_VALIDATE_SUPPORT)) {
		usig->common |=
			cpu_to_le32(IEEE80211_RADIOTAP_EHT_USIG_COMMON_VALIDATE_BITS_CHECKED);
		usig->common |=
			LE32_DEC_ENC(data0, IWL_RX_PHY_DATA0_EHT_VALIDATE,
				     IEEE80211_RADIOTAP_EHT_USIG_COMMON_VALIDATE_BITS_OK);
	}

	eht->known |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_SPATIAL_REUSE);
	eht->data[0] |= LE32_DEC_ENC(data0,
				     IWL_RX_PHY_DATA0_ETH_SPATIAL_REUSE_MASK,
				     IEEE80211_RADIOTAP_EHT_DATA0_SPATIAL_REUSE);

	/* All RU allocating size/index is in TB format */
	eht->known |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_RU_ALLOC_TB_FMT);
	eht->data[8] |= LE32_DEC_ENC(data0, IWL_RX_PHY_DATA0_EHT_PS160,
				     IEEE80211_RADIOTAP_EHT_DATA8_RU_ALLOC_TB_FMT_PS_160);
	eht->data[8] |= LE32_DEC_ENC(data1, IWL_RX_PHY_DATA1_EHT_RU_ALLOC_B0,
				     IEEE80211_RADIOTAP_EHT_DATA8_RU_ALLOC_TB_FMT_B0);
	eht->data[8] |= LE32_DEC_ENC(data1, IWL_RX_PHY_DATA1_EHT_RU_ALLOC_B1_B7,
				     IEEE80211_RADIOTAP_EHT_DATA8_RU_ALLOC_TB_FMT_B7_B1);

	iwl_mvm_decode_eht_ru(mvm, rx_status, eht);

	/* We only get here in case of IWL_RX_MPDU_PHY_TSF_OVERLOAD is set
	 * which is on only in case of monitor mode so no need to check monitor
	 * mode
	 */
	eht->known |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_PRIMARY_80);
	eht->data[1] |=
		le32_encode_bits(mvm->monitor_p80,
				 IEEE80211_RADIOTAP_EHT_DATA1_PRIMARY_80);

	usig->common |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_USIG_COMMON_TXOP_KNOWN);
	if (phy_data->with_data)
		usig->common |= LE32_DEC_ENC(data0, IWL_RX_PHY_DATA0_EHT_TXOP_DUR_MASK,
					     IEEE80211_RADIOTAP_EHT_USIG_COMMON_TXOP);
	else
		usig->common |= LE32_DEC_ENC(usig_a1, IWL_RX_USIG_A1_TXOP_DURATION,
					     IEEE80211_RADIOTAP_EHT_USIG_COMMON_TXOP);

	eht->known |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_LDPC_EXTRA_SYM_OM);
	eht->data[0] |= LE32_DEC_ENC(data0, IWL_RX_PHY_DATA0_EHT_LDPC_EXT_SYM,
				     IEEE80211_RADIOTAP_EHT_DATA0_LDPC_EXTRA_SYM_OM);

	eht->known |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_PRE_PADD_FACOR_OM);
	eht->data[0] |= LE32_DEC_ENC(data0, IWL_RX_PHY_DATA0_EHT_PRE_FEC_PAD_MASK,
				    IEEE80211_RADIOTAP_EHT_DATA0_PRE_PADD_FACOR_OM);

	eht->known |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_PE_DISAMBIGUITY_OM);
	eht->data[0] |= LE32_DEC_ENC(data0, IWL_RX_PHY_DATA0_EHT_PE_DISAMBIG,
				     IEEE80211_RADIOTAP_EHT_DATA0_PE_DISAMBIGUITY_OM);

	/* TODO: what about IWL_RX_PHY_DATA0_EHT_BW320_SLOT */

	if (!le32_get_bits(data0, IWL_RX_PHY_DATA0_EHT_SIGA_CRC_OK))
		usig->common |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_USIG_COMMON_BAD_USIG_CRC);

	usig->common |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_USIG_COMMON_PHY_VER_KNOWN);
	usig->common |= LE32_DEC_ENC(data0, IWL_RX_PHY_DATA0_EHT_PHY_VER,
				     IEEE80211_RADIOTAP_EHT_USIG_COMMON_PHY_VER);

	/*
	 * TODO: what about TB - IWL_RX_PHY_DATA1_EHT_TB_PILOT_TYPE,
	 *			 IWL_RX_PHY_DATA1_EHT_TB_LOW_SS
	 */

	eht->known |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_EHT_LTF);
	eht->data[0] |= LE32_DEC_ENC(data1, IWL_RX_PHY_DATA1_EHT_SIG_LTF_NUM,
				     IEEE80211_RADIOTAP_EHT_DATA0_EHT_LTF);

	if (info_type == IWL_RX_PHY_INFO_TYPE_EHT_TB_EXT ||
	    info_type == IWL_RX_PHY_INFO_TYPE_EHT_TB)
		iwl_mvm_decode_eht_ext_tb(mvm, phy_data, rx_status, eht, usig);

	if (info_type == IWL_RX_PHY_INFO_TYPE_EHT_MU_EXT ||
	    info_type == IWL_RX_PHY_INFO_TYPE_EHT_MU)
		iwl_mvm_decode_eht_ext_mu(mvm, phy_data, rx_status, eht, usig);
}

static void iwl_mvm_rx_eht(struct iwl_mvm *mvm, struct sk_buff *skb,
			   struct iwl_mvm_rx_phy_data *phy_data,
			   int queue)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);

	struct ieee80211_radiotap_eht *eht;
	struct ieee80211_radiotap_eht_usig *usig;
	size_t eht_len = sizeof(*eht);

	u32 rate_n_flags = phy_data->rate_n_flags;
	u32 he_type = rate_n_flags & RATE_MCS_HE_TYPE_MSK;
	/* EHT and HE have the same valus for LTF */
	u8 ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_UNKNOWN;
	u16 phy_info = phy_data->phy_info;
	u32 bw;

	/* u32 for 1 user_info */
	if (phy_data->with_data)
		eht_len += sizeof(u32);

	eht = iwl_mvm_radiotap_put_tlv(skb, IEEE80211_RADIOTAP_EHT, eht_len);

	usig = iwl_mvm_radiotap_put_tlv(skb, IEEE80211_RADIOTAP_EHT_USIG,
					sizeof(*usig));
	rx_status->flag |= RX_FLAG_RADIOTAP_TLV_AT_END;
	usig->common |=
		cpu_to_le32(IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_KNOWN);

	/* specific handling for 320MHz */
	bw = FIELD_GET(RATE_MCS_CHAN_WIDTH_MSK, rate_n_flags);
	if (bw == RATE_MCS_CHAN_WIDTH_320_VAL)
		bw += FIELD_GET(IWL_RX_PHY_DATA0_EHT_BW320_SLOT,
				le32_to_cpu(phy_data->d0));

	usig->common |= cpu_to_le32
		(FIELD_PREP(IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW, bw));

	/* report the AMPDU-EOF bit on single frames */
	if (!queue && !(phy_info & IWL_RX_MPDU_PHY_AMPDU)) {
		rx_status->flag |= RX_FLAG_AMPDU_DETAILS;
		rx_status->flag |= RX_FLAG_AMPDU_EOF_BIT_KNOWN;
		if (phy_data->d0 & cpu_to_le32(IWL_RX_PHY_DATA0_EHT_DELIM_EOF))
			rx_status->flag |= RX_FLAG_AMPDU_EOF_BIT;
	}

	/* update aggregation data for monitor sake on default queue */
	if (!queue && (phy_info & IWL_RX_MPDU_PHY_TSF_OVERLOAD) &&
	    (phy_info & IWL_RX_MPDU_PHY_AMPDU) && phy_data->first_subframe) {
		rx_status->flag |= RX_FLAG_AMPDU_EOF_BIT_KNOWN;
		if (phy_data->d0 & cpu_to_le32(IWL_RX_PHY_DATA0_EHT_DELIM_EOF))
			rx_status->flag |= RX_FLAG_AMPDU_EOF_BIT;
	}

	if (phy_info & IWL_RX_MPDU_PHY_TSF_OVERLOAD)
		iwl_mvm_decode_eht_phy_data(mvm, phy_data, rx_status, eht, usig);

#define CHECK_TYPE(F)							\
	BUILD_BUG_ON(IEEE80211_RADIOTAP_HE_DATA1_FORMAT_ ## F !=	\
		     (RATE_MCS_HE_TYPE_ ## F >> RATE_MCS_HE_TYPE_POS))

	CHECK_TYPE(SU);
	CHECK_TYPE(EXT_SU);
	CHECK_TYPE(MU);
	CHECK_TYPE(TRIG);

	switch (FIELD_GET(RATE_MCS_HE_GI_LTF_MSK, rate_n_flags)) {
	case 0:
		if (he_type == RATE_MCS_HE_TYPE_TRIG) {
			rx_status->eht.gi = NL80211_RATE_INFO_EHT_GI_1_6;
			ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_1X;
		} else {
			rx_status->eht.gi = NL80211_RATE_INFO_EHT_GI_0_8;
			ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_2X;
		}
		break;
	case 1:
		rx_status->eht.gi = NL80211_RATE_INFO_EHT_GI_1_6;
		ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_2X;
		break;
	case 2:
		ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X;
		if (he_type == RATE_MCS_HE_TYPE_TRIG)
			rx_status->eht.gi = NL80211_RATE_INFO_EHT_GI_3_2;
		else
			rx_status->eht.gi = NL80211_RATE_INFO_EHT_GI_0_8;
		break;
	case 3:
		if (he_type != RATE_MCS_HE_TYPE_TRIG) {
			ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X;
			rx_status->eht.gi = NL80211_RATE_INFO_EHT_GI_3_2;
		}
		break;
	default:
		/* nothing here */
		break;
	}

	if (ltf != IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_UNKNOWN) {
		eht->known |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_GI);
		eht->data[0] |= cpu_to_le32
			(FIELD_PREP(IEEE80211_RADIOTAP_EHT_DATA0_LTF,
				    ltf) |
			 FIELD_PREP(IEEE80211_RADIOTAP_EHT_DATA0_GI,
				    rx_status->eht.gi));
	}


	if (!phy_data->with_data) {
		eht->known |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_NSS_S |
					  IEEE80211_RADIOTAP_EHT_KNOWN_BEAMFORMED_S);
		eht->data[7] |=
			le32_encode_bits(le32_get_bits(phy_data->rx_vec[2],
						       RX_NO_DATA_RX_VEC2_EHT_NSTS_MSK),
					 IEEE80211_RADIOTAP_EHT_DATA7_NSS_S);
		if (rate_n_flags & RATE_MCS_BF_MSK)
			eht->data[7] |=
				cpu_to_le32(IEEE80211_RADIOTAP_EHT_DATA7_BEAMFORMED_S);
	} else {
		eht->user_info[0] |=
			cpu_to_le32(IEEE80211_RADIOTAP_EHT_USER_INFO_MCS_KNOWN |
				    IEEE80211_RADIOTAP_EHT_USER_INFO_CODING_KNOWN |
				    IEEE80211_RADIOTAP_EHT_USER_INFO_NSS_KNOWN_O |
				    IEEE80211_RADIOTAP_EHT_USER_INFO_BEAMFORMING_KNOWN_O |
				    IEEE80211_RADIOTAP_EHT_USER_INFO_DATA_FOR_USER);

		if (rate_n_flags & RATE_MCS_BF_MSK)
			eht->user_info[0] |=
				cpu_to_le32(IEEE80211_RADIOTAP_EHT_USER_INFO_BEAMFORMING_O);

		if (rate_n_flags & RATE_MCS_LDPC_MSK)
			eht->user_info[0] |=
				cpu_to_le32(IEEE80211_RADIOTAP_EHT_USER_INFO_CODING);

		eht->user_info[0] |= cpu_to_le32
			(FIELD_PREP(IEEE80211_RADIOTAP_EHT_USER_INFO_MCS,
				    FIELD_GET(RATE_VHT_MCS_RATE_CODE_MSK,
					      rate_n_flags)) |
			 FIELD_PREP(IEEE80211_RADIOTAP_EHT_USER_INFO_NSS_O,
				    FIELD_GET(RATE_MCS_NSS_MSK, rate_n_flags)));
	}
}

static void iwl_mvm_rx_he(struct iwl_mvm *mvm, struct sk_buff *skb,
			  struct iwl_mvm_rx_phy_data *phy_data,
			  int queue)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_radiotap_he *he = NULL;
	struct ieee80211_radiotap_he_mu *he_mu = NULL;
	u32 rate_n_flags = phy_data->rate_n_flags;
	u32 he_type = rate_n_flags & RATE_MCS_HE_TYPE_MSK;
	u8 ltf;
	static const struct ieee80211_radiotap_he known = {
		.data1 = cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_DATA_MCS_KNOWN |
				     IEEE80211_RADIOTAP_HE_DATA1_DATA_DCM_KNOWN |
				     IEEE80211_RADIOTAP_HE_DATA1_STBC_KNOWN |
				     IEEE80211_RADIOTAP_HE_DATA1_CODING_KNOWN),
		.data2 = cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_GI_KNOWN |
				     IEEE80211_RADIOTAP_HE_DATA2_TXBF_KNOWN),
	};
	static const struct ieee80211_radiotap_he_mu mu_known = {
		.flags1 = cpu_to_le16(IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_MCS_KNOWN |
				      IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_DCM_KNOWN |
				      IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_SYMS_USERS_KNOWN |
				      IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_COMP_KNOWN),
		.flags2 = cpu_to_le16(IEEE80211_RADIOTAP_HE_MU_FLAGS2_PUNC_FROM_SIG_A_BW_KNOWN |
				      IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_KNOWN),
	};
	u16 phy_info = phy_data->phy_info;

	he = skb_put_data(skb, &known, sizeof(known));
	rx_status->flag |= RX_FLAG_RADIOTAP_HE;

	if (phy_data->info_type == IWL_RX_PHY_INFO_TYPE_HE_MU ||
	    phy_data->info_type == IWL_RX_PHY_INFO_TYPE_HE_MU_EXT) {
		he_mu = skb_put_data(skb, &mu_known, sizeof(mu_known));
		rx_status->flag |= RX_FLAG_RADIOTAP_HE_MU;
	}

	/* report the AMPDU-EOF bit on single frames */
	if (!queue && !(phy_info & IWL_RX_MPDU_PHY_AMPDU)) {
		rx_status->flag |= RX_FLAG_AMPDU_DETAILS;
		rx_status->flag |= RX_FLAG_AMPDU_EOF_BIT_KNOWN;
		if (phy_data->d0 & cpu_to_le32(IWL_RX_PHY_DATA0_HE_DELIM_EOF))
			rx_status->flag |= RX_FLAG_AMPDU_EOF_BIT;
	}

	if (phy_info & IWL_RX_MPDU_PHY_TSF_OVERLOAD)
		iwl_mvm_decode_he_phy_data(mvm, phy_data, he, he_mu, rx_status,
					   queue);

	/* update aggregation data for monitor sake on default queue */
	if (!queue && (phy_info & IWL_RX_MPDU_PHY_TSF_OVERLOAD) &&
	    (phy_info & IWL_RX_MPDU_PHY_AMPDU) && phy_data->first_subframe) {
		rx_status->flag |= RX_FLAG_AMPDU_EOF_BIT_KNOWN;
		if (phy_data->d0 & cpu_to_le32(IWL_RX_PHY_DATA0_EHT_DELIM_EOF))
			rx_status->flag |= RX_FLAG_AMPDU_EOF_BIT;
	}

	if (he_type == RATE_MCS_HE_TYPE_EXT_SU &&
	    rate_n_flags & RATE_MCS_HE_106T_MSK) {
		rx_status->bw = RATE_INFO_BW_HE_RU;
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_106;
	}

	/* actually data is filled in mac80211 */
	if (he_type == RATE_MCS_HE_TYPE_SU ||
	    he_type == RATE_MCS_HE_TYPE_EXT_SU)
		he->data1 |=
			cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN);

#define CHECK_TYPE(F)							\
	BUILD_BUG_ON(IEEE80211_RADIOTAP_HE_DATA1_FORMAT_ ## F !=	\
		     (RATE_MCS_HE_TYPE_ ## F >> RATE_MCS_HE_TYPE_POS))

	CHECK_TYPE(SU);
	CHECK_TYPE(EXT_SU);
	CHECK_TYPE(MU);
	CHECK_TYPE(TRIG);

	he->data1 |= cpu_to_le16(he_type >> RATE_MCS_HE_TYPE_POS);

	if (rate_n_flags & RATE_MCS_BF_MSK)
		he->data5 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA5_TXBF);

	switch ((rate_n_flags & RATE_MCS_HE_GI_LTF_MSK) >>
		RATE_MCS_HE_GI_LTF_POS) {
	case 0:
		if (he_type == RATE_MCS_HE_TYPE_TRIG)
			rx_status->he_gi = NL80211_RATE_INFO_HE_GI_1_6;
		else
			rx_status->he_gi = NL80211_RATE_INFO_HE_GI_0_8;
		if (he_type == RATE_MCS_HE_TYPE_MU)
			ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X;
		else
			ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_1X;
		break;
	case 1:
		if (he_type == RATE_MCS_HE_TYPE_TRIG)
			rx_status->he_gi = NL80211_RATE_INFO_HE_GI_1_6;
		else
			rx_status->he_gi = NL80211_RATE_INFO_HE_GI_0_8;
		ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_2X;
		break;
	case 2:
		if (he_type == RATE_MCS_HE_TYPE_TRIG) {
			rx_status->he_gi = NL80211_RATE_INFO_HE_GI_3_2;
			ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X;
		} else {
			rx_status->he_gi = NL80211_RATE_INFO_HE_GI_1_6;
			ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_2X;
		}
		break;
	case 3:
		rx_status->he_gi = NL80211_RATE_INFO_HE_GI_3_2;
		ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X;
		break;
	case 4:
		rx_status->he_gi = NL80211_RATE_INFO_HE_GI_0_8;
		ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X;
		break;
	default:
		ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_UNKNOWN;
	}

	he->data5 |= le16_encode_bits(ltf,
				      IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE);
}

static void iwl_mvm_decode_lsig(struct sk_buff *skb,
				struct iwl_mvm_rx_phy_data *phy_data)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_radiotap_lsig *lsig;

	switch (phy_data->info_type) {
	case IWL_RX_PHY_INFO_TYPE_HT:
	case IWL_RX_PHY_INFO_TYPE_VHT_SU:
	case IWL_RX_PHY_INFO_TYPE_VHT_MU:
	case IWL_RX_PHY_INFO_TYPE_HE_TB_EXT:
	case IWL_RX_PHY_INFO_TYPE_HE_SU:
	case IWL_RX_PHY_INFO_TYPE_HE_MU:
	case IWL_RX_PHY_INFO_TYPE_HE_MU_EXT:
	case IWL_RX_PHY_INFO_TYPE_HE_TB:
	case IWL_RX_PHY_INFO_TYPE_EHT_MU:
	case IWL_RX_PHY_INFO_TYPE_EHT_TB:
	case IWL_RX_PHY_INFO_TYPE_EHT_MU_EXT:
	case IWL_RX_PHY_INFO_TYPE_EHT_TB_EXT:
		lsig = skb_put(skb, sizeof(*lsig));
		lsig->data1 = cpu_to_le16(IEEE80211_RADIOTAP_LSIG_DATA1_LENGTH_KNOWN);
		lsig->data2 = le16_encode_bits(le32_get_bits(phy_data->d1,
							     IWL_RX_PHY_DATA1_LSIG_LEN_MASK),
					       IEEE80211_RADIOTAP_LSIG_DATA2_LENGTH);
		rx_status->flag |= RX_FLAG_RADIOTAP_LSIG;
		break;
	default:
		break;
	}
}

struct iwl_rx_sta_csa {
	bool all_sta_unblocked;
	struct ieee80211_vif *vif;
};

static void iwl_mvm_rx_get_sta_block_tx(void *data, struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_rx_sta_csa *rx_sta_csa = data;

	if (mvmsta->vif != rx_sta_csa->vif)
		return;

	if (mvmsta->disable_tx)
		rx_sta_csa->all_sta_unblocked = false;
}

/*
 * Note: requires also rx_status->band to be prefilled, as well
 * as phy_data (apart from phy_data->info_type)
 */
static void iwl_mvm_rx_fill_status(struct iwl_mvm *mvm,
				   struct sk_buff *skb,
				   struct iwl_mvm_rx_phy_data *phy_data,
				   int queue)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	u32 rate_n_flags = phy_data->rate_n_flags;
	u8 stbc = u32_get_bits(rate_n_flags, RATE_MCS_STBC_MSK);
	u32 format = rate_n_flags & RATE_MCS_MOD_TYPE_MSK;
	bool is_sgi;

	phy_data->info_type = IWL_RX_PHY_INFO_TYPE_NONE;

	if (phy_data->phy_info & IWL_RX_MPDU_PHY_TSF_OVERLOAD)
		phy_data->info_type =
			le32_get_bits(phy_data->d1,
				      IWL_RX_PHY_DATA1_INFO_TYPE_MASK);

	/* This may be overridden by iwl_mvm_rx_he() to HE_RU */
	switch (rate_n_flags & RATE_MCS_CHAN_WIDTH_MSK) {
	case RATE_MCS_CHAN_WIDTH_20:
		break;
	case RATE_MCS_CHAN_WIDTH_40:
		rx_status->bw = RATE_INFO_BW_40;
		break;
	case RATE_MCS_CHAN_WIDTH_80:
		rx_status->bw = RATE_INFO_BW_80;
		break;
	case RATE_MCS_CHAN_WIDTH_160:
		rx_status->bw = RATE_INFO_BW_160;
		break;
	case RATE_MCS_CHAN_WIDTH_320:
		rx_status->bw = RATE_INFO_BW_320;
		break;
	}

	/* must be before L-SIG data */
	if (format == RATE_MCS_MOD_TYPE_HE)
		iwl_mvm_rx_he(mvm, skb, phy_data, queue);

	iwl_mvm_decode_lsig(skb, phy_data);

	rx_status->device_timestamp = phy_data->gp2_on_air_rise;

	if (mvm->rx_ts_ptp && mvm->monitor_on) {
		u64 adj_time =
			iwl_mvm_ptp_get_adj_time(mvm, phy_data->gp2_on_air_rise * NSEC_PER_USEC);

		rx_status->mactime = div64_u64(adj_time, NSEC_PER_USEC);
		rx_status->flag |= RX_FLAG_MACTIME_IS_RTAP_TS64;
		rx_status->flag &= ~RX_FLAG_MACTIME;
	}

	rx_status->freq = ieee80211_channel_to_frequency(phy_data->channel,
							 rx_status->band);
	iwl_mvm_get_signal_strength(mvm, rx_status, rate_n_flags,
				    phy_data->energy_a, phy_data->energy_b);

	/* using TLV format and must be after all fixed len fields */
	if (format == RATE_MCS_MOD_TYPE_EHT)
		iwl_mvm_rx_eht(mvm, skb, phy_data, queue);

	if (unlikely(mvm->monitor_on))
		iwl_mvm_add_rtap_sniffer_config(mvm, skb);

	is_sgi = format == RATE_MCS_MOD_TYPE_HE ?
		iwl_he_is_sgi(rate_n_flags) :
		rate_n_flags & RATE_MCS_SGI_MSK;

	if (!(format == RATE_MCS_MOD_TYPE_CCK) && is_sgi)
		rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;

	if (rate_n_flags & RATE_MCS_LDPC_MSK)
		rx_status->enc_flags |= RX_ENC_FLAG_LDPC;

	switch (format) {
	case RATE_MCS_MOD_TYPE_VHT:
		rx_status->encoding = RX_ENC_VHT;
		break;
	case RATE_MCS_MOD_TYPE_HE:
		rx_status->encoding = RX_ENC_HE;
		rx_status->he_dcm =
			!!(rate_n_flags & RATE_HE_DUAL_CARRIER_MODE_MSK);
		break;
	case RATE_MCS_MOD_TYPE_EHT:
		rx_status->encoding = RX_ENC_EHT;
		break;
	}

	switch (format) {
	case RATE_MCS_MOD_TYPE_HT:
		rx_status->encoding = RX_ENC_HT;
		rx_status->rate_idx = RATE_HT_MCS_INDEX(rate_n_flags);
		rx_status->enc_flags |= stbc << RX_ENC_FLAG_STBC_SHIFT;
		break;
	case RATE_MCS_MOD_TYPE_VHT:
	case RATE_MCS_MOD_TYPE_HE:
	case RATE_MCS_MOD_TYPE_EHT:
		rx_status->nss =
			u32_get_bits(rate_n_flags, RATE_MCS_NSS_MSK) + 1;
		rx_status->rate_idx = rate_n_flags & RATE_MCS_CODE_MSK;
		rx_status->enc_flags |= stbc << RX_ENC_FLAG_STBC_SHIFT;
		break;
	default: {
		int rate = iwl_mvm_legacy_hw_idx_to_mac80211_idx(rate_n_flags,
								 rx_status->band);

		rx_status->rate_idx = rate;

		if ((rate < 0 || rate > 0xFF)) {
			rx_status->rate_idx = 0;
			if (net_ratelimit())
				IWL_ERR(mvm, "Invalid rate flags 0x%x, band %d,\n",
					rate_n_flags, rx_status->band);
		}

		break;
		}
	}
}

void iwl_mvm_rx_mpdu_mq(struct iwl_mvm *mvm, struct napi_struct *napi,
			struct iwl_rx_cmd_buffer *rxb, int queue)
{
	struct ieee80211_rx_status *rx_status;
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_rx_mpdu_desc *desc = (void *)pkt->data;
	struct ieee80211_hdr *hdr;
	u32 len;
	u32 pkt_len = iwl_rx_packet_payload_len(pkt);
	struct ieee80211_sta *sta = NULL;
	struct sk_buff *skb;
	u8 crypt_len = 0;
	u8 sta_id = le32_get_bits(desc->status, IWL_RX_MPDU_STATUS_STA_ID);
	size_t desc_size;
	struct iwl_mvm_rx_phy_data phy_data = {};
	u32 format;

	if (unlikely(test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)))
		return;

	if (mvm->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210)
		desc_size = sizeof(*desc);
	else
		desc_size = IWL_RX_DESC_SIZE_V1;

	if (unlikely(pkt_len < desc_size)) {
		IWL_DEBUG_DROP(mvm, "Bad REPLY_RX_MPDU_CMD size\n");
		return;
	}

	if (mvm->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		phy_data.rate_n_flags = le32_to_cpu(desc->v3.rate_n_flags);
		phy_data.channel = desc->v3.channel;
		phy_data.gp2_on_air_rise = le32_to_cpu(desc->v3.gp2_on_air_rise);
		phy_data.energy_a = desc->v3.energy_a;
		phy_data.energy_b = desc->v3.energy_b;

		phy_data.d0 = desc->v3.phy_data0;
		phy_data.d1 = desc->v3.phy_data1;
		phy_data.d2 = desc->v3.phy_data2;
		phy_data.d3 = desc->v3.phy_data3;
		phy_data.eht_d4 = desc->phy_eht_data4;
		phy_data.d5 = desc->v3.phy_data5;
	} else {
		phy_data.rate_n_flags = le32_to_cpu(desc->v1.rate_n_flags);
		phy_data.channel = desc->v1.channel;
		phy_data.gp2_on_air_rise = le32_to_cpu(desc->v1.gp2_on_air_rise);
		phy_data.energy_a = desc->v1.energy_a;
		phy_data.energy_b = desc->v1.energy_b;

		phy_data.d0 = desc->v1.phy_data0;
		phy_data.d1 = desc->v1.phy_data1;
		phy_data.d2 = desc->v1.phy_data2;
		phy_data.d3 = desc->v1.phy_data3;
	}

	if (iwl_fw_lookup_notif_ver(mvm->fw, LEGACY_GROUP,
				    REPLY_RX_MPDU_CMD, 0) < 4) {
		phy_data.rate_n_flags = iwl_new_rate_from_v1(phy_data.rate_n_flags);
		IWL_DEBUG_DROP(mvm, "Got old format rate, converting. New rate: 0x%x\n",
			       phy_data.rate_n_flags);
	}

	format = phy_data.rate_n_flags & RATE_MCS_MOD_TYPE_MSK;

	len = le16_to_cpu(desc->mpdu_len);

	if (unlikely(len + desc_size > pkt_len)) {
		IWL_DEBUG_DROP(mvm, "FW lied about packet len\n");
		return;
	}

	phy_data.with_data = true;
	phy_data.phy_info = le16_to_cpu(desc->phy_info);
	phy_data.d4 = desc->phy_data4;

	hdr = (void *)(pkt->data + desc_size);
	/* Dont use dev_alloc_skb(), we'll have enough headroom once
	 * ieee80211_hdr pulled.
	 */
	skb = alloc_skb(128, GFP_ATOMIC);
	if (!skb) {
		IWL_ERR(mvm, "alloc_skb failed\n");
		return;
	}

	if (desc->mac_flags2 & IWL_RX_MPDU_MFLG2_PAD) {
		/*
		 * If the device inserted padding it means that (it thought)
		 * the 802.11 header wasn't a multiple of 4 bytes long. In
		 * this case, reserve two bytes at the start of the SKB to
		 * align the payload properly in case we end up copying it.
		 */
		skb_reserve(skb, 2);
	}

	rx_status = IEEE80211_SKB_RXCB(skb);

	/*
	 * Keep packets with CRC errors (and with overrun) for monitor mode
	 * (otherwise the firmware discards them) but mark them as bad.
	 */
	if (!(desc->status & cpu_to_le32(IWL_RX_MPDU_STATUS_CRC_OK)) ||
	    !(desc->status & cpu_to_le32(IWL_RX_MPDU_STATUS_OVERRUN_OK))) {
		IWL_DEBUG_RX(mvm, "Bad CRC or FIFO: 0x%08X.\n",
			     le32_to_cpu(desc->status));
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
	}

	/* set the preamble flag if appropriate */
	if (format == RATE_MCS_MOD_TYPE_CCK &&
	    phy_data.phy_info & IWL_RX_MPDU_PHY_SHORT_PREAMBLE)
		rx_status->enc_flags |= RX_ENC_FLAG_SHORTPRE;

	if (likely(!(phy_data.phy_info & IWL_RX_MPDU_PHY_TSF_OVERLOAD))) {
		u64 tsf_on_air_rise;

		if (mvm->trans->trans_cfg->device_family >=
		    IWL_DEVICE_FAMILY_AX210)
			tsf_on_air_rise = le64_to_cpu(desc->v3.tsf_on_air_rise);
		else
			tsf_on_air_rise = le64_to_cpu(desc->v1.tsf_on_air_rise);

		rx_status->mactime = tsf_on_air_rise;
		/* TSF as indicated by the firmware is at INA time */
		rx_status->flag |= RX_FLAG_MACTIME_PLCP_START;
	}

	if (iwl_mvm_is_band_in_rx_supported(mvm)) {
		u8 band = u8_get_bits(desc->mac_phy_band,
				      IWL_RX_MPDU_MAC_PHY_BAND_BAND_MASK);

		rx_status->band = iwl_mvm_nl80211_band_from_phy(band);
	} else {
		rx_status->band = phy_data.channel > 14 ? NL80211_BAND_5GHZ :
			NL80211_BAND_2GHZ;
	}

	/* update aggregation data for monitor sake on default queue */
	if (!queue && (phy_data.phy_info & IWL_RX_MPDU_PHY_AMPDU)) {
		bool toggle_bit;

		toggle_bit = phy_data.phy_info & IWL_RX_MPDU_PHY_AMPDU_TOGGLE;
		rx_status->flag |= RX_FLAG_AMPDU_DETAILS;
		/*
		 * Toggle is switched whenever new aggregation starts. Make
		 * sure ampdu_reference is never 0 so we can later use it to
		 * see if the frame was really part of an A-MPDU or not.
		 */
		if (toggle_bit != mvm->ampdu_toggle) {
			mvm->ampdu_ref++;
			if (mvm->ampdu_ref == 0)
				mvm->ampdu_ref++;
			mvm->ampdu_toggle = toggle_bit;
			phy_data.first_subframe = true;
		}
		rx_status->ampdu_reference = mvm->ampdu_ref;
	}

	rcu_read_lock();

	if (desc->status & cpu_to_le32(IWL_RX_MPDU_STATUS_SRC_STA_FOUND)) {
		if (!WARN_ON_ONCE(sta_id >= mvm->fw->ucode_capa.num_stations)) {
			struct ieee80211_link_sta *link_sta;

			sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);
			if (IS_ERR(sta))
				sta = NULL;
			link_sta = rcu_dereference(mvm->fw_id_to_link_sta[sta_id]);

			if (sta && sta->valid_links && link_sta) {
				rx_status->link_valid = 1;
				rx_status->link_id = link_sta->link_id;
			}
		}
	} else if (!is_multicast_ether_addr(hdr->addr2)) {
		/*
		 * This is fine since we prevent two stations with the same
		 * address from being added.
		 */
		sta = ieee80211_find_sta_by_ifaddr(mvm->hw, hdr->addr2, NULL);
	}

	if (iwl_mvm_rx_crypto(mvm, sta, hdr, rx_status, phy_data.phy_info, desc,
			      le32_to_cpu(pkt->len_n_flags), queue,
			      &crypt_len)) {
		kfree_skb(skb);
		goto out;
	}

	iwl_mvm_rx_fill_status(mvm, skb, &phy_data, queue);

	if (sta) {
		struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
		struct ieee80211_vif *tx_blocked_vif =
			rcu_dereference(mvm->csa_tx_blocked_vif);
		u8 baid = (u8)((le32_to_cpu(desc->reorder_data) &
			       IWL_RX_MPDU_REORDER_BAID_MASK) >>
			       IWL_RX_MPDU_REORDER_BAID_SHIFT);
		struct iwl_fw_dbg_trigger_tlv *trig;
		struct ieee80211_vif *vif = mvmsta->vif;

		if (!mvm->tcm.paused && len >= sizeof(*hdr) &&
		    !is_multicast_ether_addr(hdr->addr1) &&
		    ieee80211_is_data(hdr->frame_control) &&
		    time_after(jiffies, mvm->tcm.ts + MVM_TCM_PERIOD))
			schedule_delayed_work(&mvm->tcm.work, 0);

		/*
		 * We have tx blocked stations (with CS bit). If we heard
		 * frames from a blocked station on a new channel we can
		 * TX to it again.
		 */
		if (unlikely(tx_blocked_vif) && tx_blocked_vif == vif) {
			struct iwl_mvm_vif *mvmvif =
				iwl_mvm_vif_from_mac80211(tx_blocked_vif);
			struct iwl_rx_sta_csa rx_sta_csa = {
				.all_sta_unblocked = true,
				.vif = tx_blocked_vif,
			};

			if (mvmvif->csa_target_freq == rx_status->freq)
				iwl_mvm_sta_modify_disable_tx_ap(mvm, sta,
								 false);
			ieee80211_iterate_stations_atomic(mvm->hw,
							  iwl_mvm_rx_get_sta_block_tx,
							  &rx_sta_csa);

			if (rx_sta_csa.all_sta_unblocked) {
				RCU_INIT_POINTER(mvm->csa_tx_blocked_vif, NULL);
				/* Unblock BCAST / MCAST station */
				iwl_mvm_modify_all_sta_disable_tx(mvm, mvmvif, false);
				cancel_delayed_work(&mvm->cs_tx_unblock_dwork);
			}
		}

		rs_update_last_rssi(mvm, mvmsta, rx_status);

		trig = iwl_fw_dbg_trigger_on(&mvm->fwrt,
					     ieee80211_vif_to_wdev(vif),
					     FW_DBG_TRIGGER_RSSI);

		if (trig && ieee80211_is_beacon(hdr->frame_control)) {
			struct iwl_fw_dbg_trigger_low_rssi *rssi_trig;
			s32 rssi;

			rssi_trig = (void *)trig->data;
			rssi = le32_to_cpu(rssi_trig->rssi);

			if (rx_status->signal < rssi)
				iwl_fw_dbg_collect_trig(&mvm->fwrt, trig,
							NULL);
		}

		if (ieee80211_is_data(hdr->frame_control))
			iwl_mvm_rx_csum(mvm, sta, skb, pkt);

		if (iwl_mvm_is_dup(sta, queue, rx_status, hdr, desc)) {
			IWL_DEBUG_DROP(mvm, "Dropping duplicate packet 0x%x\n",
				       le16_to_cpu(hdr->seq_ctrl));
			kfree_skb(skb);
			goto out;
		}

		/*
		 * Our hardware de-aggregates AMSDUs but copies the mac header
		 * as it to the de-aggregated MPDUs. We need to turn off the
		 * AMSDU bit in the QoS control ourselves.
		 * In addition, HW reverses addr3 and addr4 - reverse it back.
		 */
		if ((desc->mac_flags2 & IWL_RX_MPDU_MFLG2_AMSDU) &&
		    !WARN_ON(!ieee80211_is_data_qos(hdr->frame_control))) {
			u8 *qc = ieee80211_get_qos_ctl(hdr);

			*qc &= ~IEEE80211_QOS_CTL_A_MSDU_PRESENT;

			if (mvm->trans->trans_cfg->device_family ==
			    IWL_DEVICE_FAMILY_9000) {
				iwl_mvm_flip_address(hdr->addr3);

				if (ieee80211_has_a4(hdr->frame_control))
					iwl_mvm_flip_address(hdr->addr4);
			}
		}
		if (baid != IWL_RX_REORDER_DATA_INVALID_BAID) {
			u32 reorder_data = le32_to_cpu(desc->reorder_data);

			iwl_mvm_agg_rx_received(mvm, reorder_data, baid);
		}

		if (ieee80211_is_data(hdr->frame_control)) {
			u8 sub_frame_idx = desc->amsdu_info &
				IWL_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK;

			/* 0 means not an A-MSDU, and 1 means a new A-MSDU */
			if (!sub_frame_idx || sub_frame_idx == 1)
				iwl_mvm_count_mpdu(mvmsta, sta_id, 1, false,
						   queue);
		}
	}

	/* management stuff on default queue */
	if (!queue) {
		if (unlikely((ieee80211_is_beacon(hdr->frame_control) ||
			      ieee80211_is_probe_resp(hdr->frame_control)) &&
			     mvm->sched_scan_pass_all ==
			     SCHED_SCAN_PASS_ALL_ENABLED))
			mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_FOUND;

		if (unlikely(ieee80211_is_beacon(hdr->frame_control) ||
			     ieee80211_is_probe_resp(hdr->frame_control)))
			rx_status->boottime_ns = ktime_get_boottime_ns();
	}

	if (iwl_mvm_create_skb(mvm, skb, hdr, len, crypt_len, rxb)) {
		kfree_skb(skb);
		goto out;
	}

	if (!iwl_mvm_reorder(mvm, napi, queue, sta, skb, desc) &&
	    likely(!iwl_mvm_time_sync_frame(mvm, skb, hdr->addr2)) &&
	    likely(!iwl_mvm_mei_filter_scan(mvm, skb))) {
		if (mvm->trans->trans_cfg->device_family == IWL_DEVICE_FAMILY_9000 &&
		    (desc->mac_flags2 & IWL_RX_MPDU_MFLG2_AMSDU) &&
		    !(desc->amsdu_info & IWL_RX_MPDU_AMSDU_LAST_SUBFRAME))
			rx_status->flag |= RX_FLAG_AMSDU_MORE;

		iwl_mvm_pass_packet_to_mac80211(mvm, napi, skb, queue, sta);
	}
out:
	rcu_read_unlock();
}

void iwl_mvm_rx_monitor_no_data(struct iwl_mvm *mvm, struct napi_struct *napi,
				struct iwl_rx_cmd_buffer *rxb, int queue)
{
	struct ieee80211_rx_status *rx_status;
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_rx_no_data_ver_3 *desc = (void *)pkt->data;
	u32 rssi;
	struct ieee80211_sta *sta = NULL;
	struct sk_buff *skb;
	struct iwl_mvm_rx_phy_data phy_data;
	u32 format;

	if (unlikely(test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)))
		return;

	if (unlikely(iwl_rx_packet_payload_len(pkt) < sizeof(struct iwl_rx_no_data)))
		return;

	rssi = le32_to_cpu(desc->rssi);
	phy_data.d0 = desc->phy_info[0];
	phy_data.d1 = desc->phy_info[1];
	phy_data.phy_info = IWL_RX_MPDU_PHY_TSF_OVERLOAD;
	phy_data.gp2_on_air_rise = le32_to_cpu(desc->on_air_rise_time);
	phy_data.rate_n_flags = le32_to_cpu(desc->rate);
	phy_data.energy_a = u32_get_bits(rssi, RX_NO_DATA_CHAIN_A_MSK);
	phy_data.energy_b = u32_get_bits(rssi, RX_NO_DATA_CHAIN_B_MSK);
	phy_data.channel = u32_get_bits(rssi, RX_NO_DATA_CHANNEL_MSK);
	phy_data.with_data = false;
	phy_data.rx_vec[0] = desc->rx_vec[0];
	phy_data.rx_vec[1] = desc->rx_vec[1];

	if (iwl_fw_lookup_notif_ver(mvm->fw, DATA_PATH_GROUP,
				    RX_NO_DATA_NOTIF, 0) < 2) {
		IWL_DEBUG_DROP(mvm, "Got an old rate format. Old rate: 0x%x\n",
			       phy_data.rate_n_flags);
		phy_data.rate_n_flags = iwl_new_rate_from_v1(phy_data.rate_n_flags);
		IWL_DEBUG_DROP(mvm, " Rate after conversion to the new format: 0x%x\n",
			       phy_data.rate_n_flags);
	}

	format = phy_data.rate_n_flags & RATE_MCS_MOD_TYPE_MSK;

	if (iwl_fw_lookup_notif_ver(mvm->fw, DATA_PATH_GROUP,
				    RX_NO_DATA_NOTIF, 0) >= 3) {
		if (unlikely(iwl_rx_packet_payload_len(pkt) <
		    sizeof(struct iwl_rx_no_data_ver_3)))
		/* invalid len for ver 3 */
			return;
		phy_data.rx_vec[2] = desc->rx_vec[2];
		phy_data.rx_vec[3] = desc->rx_vec[3];
	} else {
		if (format == RATE_MCS_MOD_TYPE_EHT)
			/* no support for EHT before version 3 API */
			return;
	}

	/* Dont use dev_alloc_skb(), we'll have enough headroom once
	 * ieee80211_hdr pulled.
	 */
	skb = alloc_skb(128, GFP_ATOMIC);
	if (!skb) {
		IWL_ERR(mvm, "alloc_skb failed\n");
		return;
	}

	rx_status = IEEE80211_SKB_RXCB(skb);

	/* 0-length PSDU */
	rx_status->flag |= RX_FLAG_NO_PSDU;

	/* mark as failed PLCP on any errors to skip checks in mac80211 */
	if (le32_get_bits(desc->info, RX_NO_DATA_INFO_ERR_MSK) !=
	    RX_NO_DATA_INFO_ERR_NONE)
		rx_status->flag |= RX_FLAG_FAILED_PLCP_CRC;

	switch (le32_get_bits(desc->info, RX_NO_DATA_INFO_TYPE_MSK)) {
	case RX_NO_DATA_INFO_TYPE_NDP:
		rx_status->zero_length_psdu_type =
			IEEE80211_RADIOTAP_ZERO_LEN_PSDU_SOUNDING;
		break;
	case RX_NO_DATA_INFO_TYPE_MU_UNMATCHED:
	case RX_NO_DATA_INFO_TYPE_TB_UNMATCHED:
		rx_status->zero_length_psdu_type =
			IEEE80211_RADIOTAP_ZERO_LEN_PSDU_NOT_CAPTURED;
		break;
	default:
		rx_status->zero_length_psdu_type =
			IEEE80211_RADIOTAP_ZERO_LEN_PSDU_VENDOR;
		break;
	}

	rx_status->band = phy_data.channel > 14 ? NL80211_BAND_5GHZ :
		NL80211_BAND_2GHZ;

	iwl_mvm_rx_fill_status(mvm, skb, &phy_data, queue);

	/* no more radio tap info should be put after this point.
	 *
	 * We mark it as mac header, for upper layers to know where
	 * all radio tap header ends.
	 *
	 * Since data doesn't move data while putting data on skb and that is
	 * the only way we use, data + len is the next place that hdr would be put
	 */
	skb_set_mac_header(skb, skb->len);

	/*
	 * Override the nss from the rx_vec since the rate_n_flags has
	 * only 2 bits for the nss which gives a max of 4 ss but there
	 * may be up to 8 spatial streams.
	 */
	switch (format) {
	case RATE_MCS_MOD_TYPE_VHT:
		rx_status->nss =
			le32_get_bits(desc->rx_vec[0],
				      RX_NO_DATA_RX_VEC0_VHT_NSTS_MSK) + 1;
		break;
	case RATE_MCS_MOD_TYPE_HE:
		rx_status->nss =
			le32_get_bits(desc->rx_vec[0],
				      RX_NO_DATA_RX_VEC0_HE_NSTS_MSK) + 1;
		break;
	case RATE_MCS_MOD_TYPE_EHT:
		rx_status->nss =
			le32_get_bits(desc->rx_vec[2],
				      RX_NO_DATA_RX_VEC2_EHT_NSTS_MSK) + 1;
	}

	rcu_read_lock();
	ieee80211_rx_napi(mvm->hw, sta, skb, napi);
	rcu_read_unlock();
}

void iwl_mvm_rx_frame_release(struct iwl_mvm *mvm, struct napi_struct *napi,
			      struct iwl_rx_cmd_buffer *rxb, int queue)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_frame_release *release = (void *)pkt->data;

	if (unlikely(iwl_rx_packet_payload_len(pkt) < sizeof(*release)))
		return;

	iwl_mvm_release_frames_from_notif(mvm, napi, release->baid,
					  le16_to_cpu(release->nssn),
					  queue);
}

void iwl_mvm_rx_bar_frame_release(struct iwl_mvm *mvm, struct napi_struct *napi,
				  struct iwl_rx_cmd_buffer *rxb, int queue)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_bar_frame_release *release = (void *)pkt->data;
	struct iwl_mvm_baid_data *baid_data;
	u32 pkt_len = iwl_rx_packet_payload_len(pkt);
	unsigned int baid, nssn, sta_id, tid;

	if (IWL_FW_CHECK(mvm, pkt_len < sizeof(*release),
			 "Unexpected frame release notif size %d (expected %zu)\n",
			 pkt_len, sizeof(*release)))
		return;

	baid = le32_get_bits(release->ba_info,
			     IWL_BAR_FRAME_RELEASE_BAID_MASK);
	nssn = le32_get_bits(release->ba_info,
			     IWL_BAR_FRAME_RELEASE_NSSN_MASK);
	sta_id = le32_get_bits(release->sta_tid,
			       IWL_BAR_FRAME_RELEASE_STA_MASK);
	tid = le32_get_bits(release->sta_tid,
			    IWL_BAR_FRAME_RELEASE_TID_MASK);

	if (WARN_ON_ONCE(baid == IWL_RX_REORDER_DATA_INVALID_BAID ||
			 baid >= ARRAY_SIZE(mvm->baid_map)))
		return;

	rcu_read_lock();
	baid_data = rcu_dereference(mvm->baid_map[baid]);
	if (!baid_data) {
		IWL_DEBUG_RX(mvm,
			     "Got valid BAID %d but not allocated, invalid BAR release!\n",
			      baid);
		goto out;
	}

	if (WARN(tid != baid_data->tid || sta_id > IWL_STATION_COUNT_MAX ||
		 !(baid_data->sta_mask & BIT(sta_id)),
		 "baid 0x%x is mapped to sta_mask:0x%x tid:%d, but BAR release received for sta:%d tid:%d\n",
		 baid, baid_data->sta_mask, baid_data->tid, sta_id,
		 tid))
		goto out;

	IWL_DEBUG_DROP(mvm, "Received a BAR, expect packet loss: nssn %d\n",
		       nssn);

	iwl_mvm_release_frames_from_notif(mvm, napi, baid, nssn, queue);
out:
	rcu_read_unlock();
}
