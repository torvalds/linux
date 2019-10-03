/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include "iwl-trans.h"
#include "mvm.h"
#include "fw-api.h"

static void *iwl_mvm_skb_get_hdr(struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	u8 *data = skb->data;

	/* Alignment concerns */
	BUILD_BUG_ON(sizeof(struct ieee80211_radiotap_he) % 4);
	BUILD_BUG_ON(sizeof(struct ieee80211_radiotap_he_mu) % 4);
	BUILD_BUG_ON(sizeof(struct ieee80211_radiotap_lsig) % 4);
	BUILD_BUG_ON(sizeof(struct ieee80211_vendor_radiotap) % 4);

	if (rx_status->flag & RX_FLAG_RADIOTAP_HE)
		data += sizeof(struct ieee80211_radiotap_he);
	if (rx_status->flag & RX_FLAG_RADIOTAP_HE_MU)
		data += sizeof(struct ieee80211_radiotap_he_mu);
	if (rx_status->flag & RX_FLAG_RADIOTAP_LSIG)
		data += sizeof(struct ieee80211_radiotap_lsig);
	if (rx_status->flag & RX_FLAG_RADIOTAP_VENDOR_DATA) {
		struct ieee80211_vendor_radiotap *radiotap = (void *)data;

		data += sizeof(*radiotap) + radiotap->len + radiotap->pad;
	}

	return data;
}

static inline int iwl_mvm_check_pn(struct iwl_mvm *mvm, struct sk_buff *skb,
				   int queue, struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvmsta;
	struct ieee80211_hdr *hdr = iwl_mvm_skb_get_hdr(skb);
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
		IWL_ERR(mvm,
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

	if (desc->mac_flags2 & IWL_RX_MPDU_MFLG2_PAD) {
		len -= 2;
		pad_len = 2;
	}

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

	if (WARN_ONCE(headlen < hdrlen,
		      "invalid packet lengths (hdrlen=%d, len=%d, crypt_len=%d)\n",
		      hdrlen, len, crypt_len)) {
		/*
		 * We warn and trace because we want to be able to see
		 * it in trace-cmd as well.
		 */
		IWL_DEBUG_RX(mvm,
			     "invalid packet lengths (hdrlen=%d, len=%d, crypt_len=%d)\n",
			     hdrlen, len, crypt_len);
		return -EINVAL;
	}

	skb_put_data(skb, hdr, hdrlen);
	skb_put_data(skb, (u8 *)hdr + hdrlen + pad_len, headlen - hdrlen);

	fraglen = len - headlen;

	if (fraglen) {
		int offset = (void *)hdr + headlen + pad_len -
			     rxb_addr(rxb) + rxb_offset(rxb);

		skb_add_rx_frag(skb, 0, rxb_steal_page(rxb), offset,
				fraglen, rxb->truesize);
	}

	return 0;
}

static void iwl_mvm_add_rtap_sniffer_config(struct iwl_mvm *mvm,
					    struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_vendor_radiotap *radiotap;
	const int size = sizeof(*radiotap) + sizeof(__le16);

	if (!mvm->cur_aid)
		return;

	/* ensure alignment */
	BUILD_BUG_ON((size + 2) % 4);

	radiotap = skb_put(skb, size + 2);
	radiotap->align = 1;
	/* Intel OUI */
	radiotap->oui[0] = 0xf6;
	radiotap->oui[1] = 0x54;
	radiotap->oui[2] = 0x25;
	/* radiotap sniffer config sub-namespace */
	radiotap->subns = 1;
	radiotap->present = 0x1;
	radiotap->len = size - sizeof(*radiotap);
	radiotap->pad = 2;

	/* fill the data now */
	memcpy(radiotap->data, &mvm->cur_aid, sizeof(mvm->cur_aid));
	/* and clear the padding */
	memset(radiotap->data + sizeof(__le16), 0, radiotap->pad);

	rx_status->flag |= RX_FLAG_RADIOTAP_VENDOR_DATA;
}

/* iwl_mvm_pass_packet_to_mac80211 - passes the packet for mac80211 */
static void iwl_mvm_pass_packet_to_mac80211(struct iwl_mvm *mvm,
					    struct napi_struct *napi,
					    struct sk_buff *skb, int queue,
					    struct ieee80211_sta *sta,
					    bool csi)
{
	if (iwl_mvm_check_pn(mvm, skb, queue, sta))
		kfree_skb(skb);
	else
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
	rx_status->chain_signal[2] = S8_MIN;
}

static int iwl_mvm_rx_crypto(struct iwl_mvm *mvm, struct ieee80211_hdr *hdr,
			     struct ieee80211_rx_status *stats, u16 phy_info,
			     struct iwl_rx_mpdu_desc *desc,
			     u32 pkt_flags, int queue, u8 *crypt_len)
{
	u16 status = le16_to_cpu(desc->status);

	/*
	 * Drop UNKNOWN frames in aggregation, unless in monitor mode
	 * (where we don't have the keys).
	 * We limit this to aggregation because in TKIP this is a valid
	 * scenario, since we may not have the (correct) TTAK (phase 1
	 * key) in the firmware.
	 */
	if (phy_info & IWL_RX_MPDU_PHY_AMPDU &&
	    (status & IWL_RX_MPDU_STATUS_SEC_MASK) ==
	    IWL_RX_MPDU_STATUS_SEC_UNKNOWN && !mvm->monitor_on)
		return -1;

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
		if (!(status & IWL_RX_MPDU_STATUS_MIC_OK))
			return -1;

		stats->flag |= RX_FLAG_DECRYPTED;
		if (pkt_flags & FH_RSCSR_RADA_EN)
			stats->flag |= RX_FLAG_MIC_STRIPPED;
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
		/* fall through */
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
			IWL_ERR(mvm, "Unhandled alg: 0x%x\n", status);
	}

	return 0;
}

static void iwl_mvm_rx_csum(struct ieee80211_sta *sta,
			    struct sk_buff *skb,
			    struct iwl_rx_mpdu_desc *desc)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(mvmsta->vif);
	u16 flags = le16_to_cpu(desc->l3l4_flags);
	u8 l3_prot = (u8)((flags & IWL_RX_L3L4_L3_PROTO_MASK) >>
			  IWL_RX_L3_PROTO_POS);

	if (mvmvif->features & NETIF_F_RXCSUM &&
	    flags & IWL_RX_L3L4_TCP_UDP_CSUM_OK &&
	    (flags & IWL_RX_L3L4_IP_HDR_CSUM_OK ||
	     l3_prot == IWL_RX_L3_TYPE_IPV6 ||
	     l3_prot == IWL_RX_L3_TYPE_IPV6_FRAG))
		skb->ip_summed = CHECKSUM_UNNECESSARY;
}

/*
 * returns true if a packet is a duplicate and should be dropped.
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
	dup_data = &mvm_sta->dup_data[queue];

	/*
	 * Drop duplicate 802.11 retransmissions
	 * (IEEE 802.11-2012: 9.3.2.10 "Duplicate detection and recovery")
	 */
	if (ieee80211_is_ctl(hdr->frame_control) ||
	    ieee80211_is_qos_nullfunc(hdr->frame_control) ||
	    is_multicast_ether_addr(hdr->addr1)) {
		rx_status->flag |= RX_FLAG_DUP_VALIDATED;
		return false;
	}

	if (ieee80211_is_data_qos(hdr->frame_control))
		/* frame has qos control */
		tid = ieee80211_get_tid(hdr);
	else
		tid = IWL_MAX_TID_COUNT;

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

int iwl_mvm_notify_rx_queue(struct iwl_mvm *mvm, u32 rxq_mask,
			    const u8 *data, u32 count, bool async)
{
	u8 buf[sizeof(struct iwl_rxq_sync_cmd) +
	       sizeof(struct iwl_mvm_rss_sync_notif)];
	struct iwl_rxq_sync_cmd *cmd = (void *)buf;
	u32 data_size = sizeof(*cmd) + count;
	int ret;

	/*
	 * size must be a multiple of DWORD
	 * Ensure we don't overflow buf
	 */
	if (WARN_ON(count & 3 ||
		    count > sizeof(struct iwl_mvm_rss_sync_notif)))
		return -EINVAL;

	cmd->rxq_mask = cpu_to_le32(rxq_mask);
	cmd->count =  cpu_to_le32(count);
	cmd->flags = 0;
	memcpy(cmd->payload, data, count);

	ret = iwl_mvm_send_cmd_pdu(mvm,
				   WIDE_ID(DATA_PATH_GROUP,
					   TRIGGER_RX_QUEUES_NOTIF_CMD),
				   async ? CMD_ASYNC : 0, data_size, cmd);

	return ret;
}

/*
 * Returns true if sn2 - buffer_size < sn1 < sn2.
 * To be used only in order to compare reorder buffer head with NSSN.
 * We fully trust NSSN unless it is behind us due to reorder timeout.
 * Reorder timeout can only bring us up to buffer_size SNs ahead of NSSN.
 */
static bool iwl_mvm_is_sn_less(u16 sn1, u16 sn2, u16 buffer_size)
{
	return ieee80211_sn_less(sn1, sn2) &&
	       !ieee80211_sn_less(sn1, sn2 - buffer_size);
}

static void iwl_mvm_sync_nssn(struct iwl_mvm *mvm, u8 baid, u16 nssn)
{
	struct iwl_mvm_rss_sync_notif notif = {
		.metadata.type = IWL_MVM_RXQ_NSSN_SYNC,
		.metadata.sync = 0,
		.nssn_sync.baid = baid,
		.nssn_sync.nssn = nssn,
	};

	iwl_mvm_sync_rx_queues_internal(mvm, (void *)&notif, sizeof(notif));
}

#define RX_REORDER_BUF_TIMEOUT_MQ (HZ / 10)

enum iwl_mvm_release_flags {
	IWL_MVM_RELEASE_SEND_RSS_SYNC = BIT(0),
	IWL_MVM_RELEASE_FROM_RSS_SYNC = BIT(1),
};

static void iwl_mvm_release_frames(struct iwl_mvm *mvm,
				   struct ieee80211_sta *sta,
				   struct napi_struct *napi,
				   struct iwl_mvm_baid_data *baid_data,
				   struct iwl_mvm_reorder_buffer *reorder_buf,
				   u16 nssn, u32 flags)
{
	struct iwl_mvm_reorder_buf_entry *entries =
		&baid_data->entries[reorder_buf->queue *
				    baid_data->entries_per_queue];
	u16 ssn = reorder_buf->head_sn;

	lockdep_assert_held(&reorder_buf->lock);

	/*
	 * We keep the NSSN not too far behind, if we are sync'ing it and it
	 * is more than 2048 ahead of us, it must be behind us. Discard it.
	 * This can happen if the queue that hit the 0 / 2048 seqno was lagging
	 * behind and this queue already processed packets. The next if
	 * would have caught cases where this queue would have processed less
	 * than 64 packets, but it may have processed more than 64 packets.
	 */
	if ((flags & IWL_MVM_RELEASE_FROM_RSS_SYNC) &&
	    ieee80211_sn_less(nssn, ssn))
		goto set_timer;

	/* ignore nssn smaller than head sn - this can happen due to timeout */
	if (iwl_mvm_is_sn_less(nssn, ssn, reorder_buf->buf_size))
		goto set_timer;

	while (iwl_mvm_is_sn_less(ssn, nssn, reorder_buf->buf_size)) {
		int index = ssn % reorder_buf->buf_size;
		struct sk_buff_head *skb_list = &entries[index].e.frames;
		struct sk_buff *skb;

		ssn = ieee80211_sn_inc(ssn);
		if ((flags & IWL_MVM_RELEASE_SEND_RSS_SYNC) &&
		    (ssn == 2048 || ssn == 0))
			iwl_mvm_sync_nssn(mvm, baid_data->baid, ssn);

		/*
		 * Empty the list. Will have more than one frame for A-MSDU.
		 * Empty list is valid as well since nssn indicates frames were
		 * received.
		 */
		while ((skb = __skb_dequeue(skb_list))) {
			iwl_mvm_pass_packet_to_mac80211(mvm, napi, skb,
							reorder_buf->queue,
							sta, false);
			reorder_buf->num_stored--;
		}
	}
	reorder_buf->head_sn = nssn;

set_timer:
	if (reorder_buf->num_stored && !reorder_buf->removed) {
		u16 index = reorder_buf->head_sn % reorder_buf->buf_size;

		while (skb_queue_empty(&entries[index].e.frames))
			index = (index + 1) % reorder_buf->buf_size;
		/* modify timer to match next frame's expiration time */
		mod_timer(&reorder_buf->reorder_timer,
			  entries[index].e.reorder_time + 1 +
			  RX_REORDER_BUF_TIMEOUT_MQ);
	} else {
		del_timer(&reorder_buf->reorder_timer);
	}
}

void iwl_mvm_reorder_timer_expired(struct timer_list *t)
{
	struct iwl_mvm_reorder_buffer *buf = from_timer(buf, t, reorder_timer);
	struct iwl_mvm_baid_data *baid_data =
		iwl_mvm_baid_data_from_reorder_buf(buf);
	struct iwl_mvm_reorder_buf_entry *entries =
		&baid_data->entries[buf->queue * baid_data->entries_per_queue];
	int i;
	u16 sn = 0, index = 0;
	bool expired = false;
	bool cont = false;

	spin_lock(&buf->lock);

	if (!buf->num_stored || buf->removed) {
		spin_unlock(&buf->lock);
		return;
	}

	for (i = 0; i < buf->buf_size ; i++) {
		index = (buf->head_sn + i) % buf->buf_size;

		if (skb_queue_empty(&entries[index].e.frames)) {
			/*
			 * If there is a hole and the next frame didn't expire
			 * we want to break and not advance SN
			 */
			cont = false;
			continue;
		}
		if (!cont &&
		    !time_after(jiffies, entries[index].e.reorder_time +
					 RX_REORDER_BUF_TIMEOUT_MQ))
			break;

		expired = true;
		/* continue until next hole after this expired frames */
		cont = true;
		sn = ieee80211_sn_add(buf->head_sn, i + 1);
	}

	if (expired) {
		struct ieee80211_sta *sta;
		struct iwl_mvm_sta *mvmsta;
		u8 sta_id = baid_data->sta_id;

		rcu_read_lock();
		sta = rcu_dereference(buf->mvm->fw_id_to_mac_id[sta_id]);
		mvmsta = iwl_mvm_sta_from_mac80211(sta);

		/* SN is set to the last expired frame + 1 */
		IWL_DEBUG_HT(buf->mvm,
			     "Releasing expired frames for sta %u, sn %d\n",
			     sta_id, sn);
		iwl_mvm_event_frame_timeout_callback(buf->mvm, mvmsta->vif,
						     sta, baid_data->tid);
		iwl_mvm_release_frames(buf->mvm, sta, NULL, baid_data,
				       buf, sn, IWL_MVM_RELEASE_SEND_RSS_SYNC);
		rcu_read_unlock();
	} else {
		/*
		 * If no frame expired and there are stored frames, index is now
		 * pointing to the first unexpired frame - modify timer
		 * accordingly to this frame.
		 */
		mod_timer(&buf->reorder_timer,
			  entries[index].e.reorder_time +
			  1 + RX_REORDER_BUF_TIMEOUT_MQ);
	}
	spin_unlock(&buf->lock);
}

static void iwl_mvm_del_ba(struct iwl_mvm *mvm, int queue,
			   struct iwl_mvm_delba_data *data)
{
	struct iwl_mvm_baid_data *ba_data;
	struct ieee80211_sta *sta;
	struct iwl_mvm_reorder_buffer *reorder_buf;
	u8 baid = data->baid;

	if (WARN_ONCE(baid >= IWL_MAX_BAID, "invalid BAID: %x\n", baid))
		return;

	rcu_read_lock();

	ba_data = rcu_dereference(mvm->baid_map[baid]);
	if (WARN_ON_ONCE(!ba_data))
		goto out;

	sta = rcu_dereference(mvm->fw_id_to_mac_id[ba_data->sta_id]);
	if (WARN_ON_ONCE(IS_ERR_OR_NULL(sta)))
		goto out;

	reorder_buf = &ba_data->reorder_buf[queue];

	/* release all frames that are in the reorder buffer to the stack */
	spin_lock_bh(&reorder_buf->lock);
	iwl_mvm_release_frames(mvm, sta, NULL, ba_data, reorder_buf,
			       ieee80211_sn_add(reorder_buf->head_sn,
						reorder_buf->buf_size),
			       0);
	spin_unlock_bh(&reorder_buf->lock);
	del_timer_sync(&reorder_buf->reorder_timer);

out:
	rcu_read_unlock();
}

static void iwl_mvm_release_frames_from_notif(struct iwl_mvm *mvm,
					      struct napi_struct *napi,
					      u8 baid, u16 nssn, int queue,
					      u32 flags)
{
	struct ieee80211_sta *sta;
	struct iwl_mvm_reorder_buffer *reorder_buf;
	struct iwl_mvm_baid_data *ba_data;

	IWL_DEBUG_HT(mvm, "Frame release notification for BAID %u, NSSN %d\n",
		     baid, nssn);

	if (WARN_ON_ONCE(baid == IWL_RX_REORDER_DATA_INVALID_BAID ||
			 baid >= ARRAY_SIZE(mvm->baid_map)))
		return;

	rcu_read_lock();

	ba_data = rcu_dereference(mvm->baid_map[baid]);
	if (WARN_ON_ONCE(!ba_data))
		goto out;

	sta = rcu_dereference(mvm->fw_id_to_mac_id[ba_data->sta_id]);
	if (WARN_ON_ONCE(IS_ERR_OR_NULL(sta)))
		goto out;

	reorder_buf = &ba_data->reorder_buf[queue];

	spin_lock_bh(&reorder_buf->lock);
	iwl_mvm_release_frames(mvm, sta, napi, ba_data,
			       reorder_buf, nssn, flags);
	spin_unlock_bh(&reorder_buf->lock);

out:
	rcu_read_unlock();
}

static void iwl_mvm_nssn_sync(struct iwl_mvm *mvm,
			      struct napi_struct *napi, int queue,
			      const struct iwl_mvm_nssn_sync_data *data)
{
	iwl_mvm_release_frames_from_notif(mvm, napi, data->baid,
					  data->nssn, queue,
					  IWL_MVM_RELEASE_FROM_RSS_SYNC);
}

void iwl_mvm_rx_queue_notif(struct iwl_mvm *mvm, struct napi_struct *napi,
			    struct iwl_rx_cmd_buffer *rxb, int queue)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_rxq_sync_notification *notif;
	struct iwl_mvm_internal_rxq_notif *internal_notif;

	notif = (void *)pkt->data;
	internal_notif = (void *)notif->payload;

	if (internal_notif->sync &&
	    mvm->queue_sync_cookie != internal_notif->cookie) {
		WARN_ONCE(1, "Received expired RX queue sync message\n");
		return;
	}

	switch (internal_notif->type) {
	case IWL_MVM_RXQ_EMPTY:
		break;
	case IWL_MVM_RXQ_NOTIF_DEL_BA:
		iwl_mvm_del_ba(mvm, queue, (void *)internal_notif->data);
		break;
	case IWL_MVM_RXQ_NSSN_SYNC:
		iwl_mvm_nssn_sync(mvm, napi, queue,
				  (void *)internal_notif->data);
		break;
	default:
		WARN_ONCE(1, "Invalid identifier %d", internal_notif->type);
	}

	if (internal_notif->sync &&
	    !atomic_dec_return(&mvm->queue_sync_counter))
		wake_up(&mvm->rx_sync_waitq);
}

static void iwl_mvm_oldsn_workaround(struct iwl_mvm *mvm,
				     struct ieee80211_sta *sta, int tid,
				     struct iwl_mvm_reorder_buffer *buffer,
				     u32 reorder, u32 gp2, int queue)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);

	if (gp2 != buffer->consec_oldsn_ampdu_gp2) {
		/* we have a new (A-)MPDU ... */

		/*
		 * reset counter to 0 if we didn't have any oldsn in
		 * the last A-MPDU (as detected by GP2 being identical)
		 */
		if (!buffer->consec_oldsn_prev_drop)
			buffer->consec_oldsn_drops = 0;

		/* either way, update our tracking state */
		buffer->consec_oldsn_ampdu_gp2 = gp2;
	} else if (buffer->consec_oldsn_prev_drop) {
		/*
		 * tracking state didn't change, and we had an old SN
		 * indication before - do nothing in this case, we
		 * already noted this one down and are waiting for the
		 * next A-MPDU (by GP2)
		 */
		return;
	}

	/* return unless this MPDU has old SN */
	if (!(reorder & IWL_RX_MPDU_REORDER_BA_OLD_SN))
		return;

	/* update state */
	buffer->consec_oldsn_prev_drop = 1;
	buffer->consec_oldsn_drops++;

	/* if limit is reached, send del BA and reset state */
	if (buffer->consec_oldsn_drops == IWL_MVM_AMPDU_CONSEC_DROPS_DELBA) {
		IWL_WARN(mvm,
			 "reached %d old SN frames from %pM on queue %d, stopping BA session on TID %d\n",
			 IWL_MVM_AMPDU_CONSEC_DROPS_DELBA,
			 sta->addr, queue, tid);
		ieee80211_stop_rx_ba_session(mvmsta->vif, BIT(tid), sta->addr);
		buffer->consec_oldsn_prev_drop = 0;
		buffer->consec_oldsn_drops = 0;
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
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_hdr *hdr = iwl_mvm_skb_get_hdr(skb);
	struct iwl_mvm_sta *mvm_sta;
	struct iwl_mvm_baid_data *baid_data;
	struct iwl_mvm_reorder_buffer *buffer;
	struct sk_buff *tail;
	u32 reorder = le32_to_cpu(desc->reorder_data);
	bool amsdu = desc->mac_flags2 & IWL_RX_MPDU_MFLG2_AMSDU;
	bool last_subframe =
		desc->amsdu_info & IWL_RX_MPDU_AMSDU_LAST_SUBFRAME;
	u8 tid = ieee80211_get_tid(hdr);
	u8 sub_frame_idx = desc->amsdu_info &
			   IWL_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK;
	struct iwl_mvm_reorder_buf_entry *entries;
	int index;
	u16 nssn, sn;
	u8 baid;

	baid = (reorder & IWL_RX_MPDU_REORDER_BAID_MASK) >>
		IWL_RX_MPDU_REORDER_BAID_SHIFT;

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

	mvm_sta = iwl_mvm_sta_from_mac80211(sta);

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

	if (WARN(tid != baid_data->tid || mvm_sta->sta_id != baid_data->sta_id,
		 "baid 0x%x is mapped to sta:%d tid:%d, but was received for sta:%d tid:%d\n",
		 baid, baid_data->sta_id, baid_data->tid, mvm_sta->sta_id,
		 tid))
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

	if (ieee80211_is_back_req(hdr->frame_control)) {
		iwl_mvm_release_frames(mvm, sta, napi, baid_data,
				       buffer, nssn, 0);
		goto drop;
	}

	/*
	 * If there was a significant jump in the nssn - adjust.
	 * If the SN is smaller than the NSSN it might need to first go into
	 * the reorder buffer, in which case we just release up to it and the
	 * rest of the function will take care of storing it and releasing up to
	 * the nssn.
	 * This should not happen. This queue has been lagging and it should
	 * have been updated by a IWL_MVM_RXQ_NSSN_SYNC notification. Be nice
	 * and update the other queues.
	 */
	if (!iwl_mvm_is_sn_less(nssn, buffer->head_sn + buffer->buf_size,
				buffer->buf_size) ||
	    !ieee80211_sn_less(sn, buffer->head_sn + buffer->buf_size)) {
		u16 min_sn = ieee80211_sn_less(sn, nssn) ? sn : nssn;

		iwl_mvm_release_frames(mvm, sta, napi, baid_data, buffer,
				       min_sn, IWL_MVM_RELEASE_SEND_RSS_SYNC);
	}

	iwl_mvm_oldsn_workaround(mvm, sta, tid, buffer, reorder,
				 rx_status->device_timestamp, queue);

	/* drop any oudated packets */
	if (ieee80211_sn_less(sn, buffer->head_sn))
		goto drop;

	/* release immediately if allowed by nssn and no stored frames */
	if (!buffer->num_stored && ieee80211_sn_less(sn, nssn)) {
		if (iwl_mvm_is_sn_less(buffer->head_sn, nssn,
				       buffer->buf_size) &&
		   (!amsdu || last_subframe)) {
			/*
			 * If we crossed the 2048 or 0 SN, notify all the
			 * queues. This is done in order to avoid having a
			 * head_sn that lags behind for too long. When that
			 * happens, we can get to a situation where the head_sn
			 * is within the interval [nssn - buf_size : nssn]
			 * which will make us think that the nssn is a packet
			 * that we already freed because of the reordering
			 * buffer and we will ignore it. So maintain the
			 * head_sn somewhat updated across all the queues:
			 * when it crosses 0 and 2048.
			 */
			if (sn == 2048 || sn == 0)
				iwl_mvm_sync_nssn(mvm, baid, sn);
			buffer->head_sn = nssn;
		}
		/* No need to update AMSDU last SN - we are moving the head */
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
		if (!amsdu || last_subframe) {
			if (sn == 2048 || sn == 0)
				iwl_mvm_sync_nssn(mvm, baid, sn);
			buffer->head_sn = ieee80211_sn_inc(buffer->head_sn);
		}
		/* No need to update AMSDU last SN - we are moving the head */
		spin_unlock_bh(&buffer->lock);
		return false;
	}

	index = sn % buffer->buf_size;

	/*
	 * Check if we already stored this frame
	 * As AMSDU is either received or not as whole, logic is simple:
	 * If we have frames in that position in the buffer and the last frame
	 * originated from AMSDU had a different SN then it is a retransmission.
	 * If it is the same SN then if the subframe index is incrementing it
	 * is the same AMSDU - otherwise it is a retransmission.
	 */
	tail = skb_peek_tail(&entries[index].e.frames);
	if (tail && !amsdu)
		goto drop;
	else if (tail && (sn != buffer->last_amsdu ||
			  buffer->last_sub_index >= sub_frame_idx))
		goto drop;

	/* put in reorder buffer */
	__skb_queue_tail(&entries[index].e.frames, skb);
	buffer->num_stored++;
	entries[index].e.reorder_time = jiffies;

	if (amsdu) {
		buffer->last_amsdu = sn;
		buffer->last_sub_index = sub_frame_idx;
	}

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
				       buffer, nssn,
				       IWL_MVM_RELEASE_SEND_RSS_SYNC);

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
	__le32 d0, d1, d2, d3;
	__le16 d4;
};

static void iwl_mvm_decode_he_mu_ext(struct iwl_mvm *mvm,
				     struct iwl_mvm_rx_phy_data *phy_data,
				     u32 rate_n_flags,
				     struct ieee80211_radiotap_he_mu *he_mu)
{
	u32 phy_data2 = le32_to_cpu(phy_data->d2);
	u32 phy_data3 = le32_to_cpu(phy_data->d3);
	u16 phy_data4 = le16_to_cpu(phy_data->d4);

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
	    (rate_n_flags & RATE_MCS_CHAN_WIDTH_MSK) != RATE_MCS_CHAN_WIDTH_20) {
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
			       u32 rate_n_flags,
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
				       u32 rate_n_flags, int queue)
{
	switch (phy_data->info_type) {
	case IWL_RX_PHY_INFO_TYPE_NONE:
	case IWL_RX_PHY_INFO_TYPE_CCK:
	case IWL_RX_PHY_INFO_TYPE_OFDM_LGCY:
	case IWL_RX_PHY_INFO_TYPE_HT:
	case IWL_RX_PHY_INFO_TYPE_VHT_SU:
	case IWL_RX_PHY_INFO_TYPE_VHT_MU:
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
		/* fall through */
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
		iwl_mvm_decode_he_mu_ext(mvm, phy_data, rate_n_flags, he_mu);
		/* fall through */
	case IWL_RX_PHY_INFO_TYPE_HE_MU:
		he_mu->flags2 |=
			le16_encode_bits(le32_get_bits(phy_data->d1,
						       IWL_RX_PHY_DATA1_HE_MU_SIBG_SYM_OR_USER_NUM_MASK),
					 IEEE80211_RADIOTAP_HE_MU_FLAGS2_SIG_B_SYMS_USERS);
		he_mu->flags2 |=
			le16_encode_bits(le32_get_bits(phy_data->d1,
						       IWL_RX_PHY_DATA1_HE_MU_SIGB_COMPRESSION),
					 IEEE80211_RADIOTAP_HE_MU_FLAGS2_SIG_B_COMP);
		/* fall through */
	case IWL_RX_PHY_INFO_TYPE_HE_TB:
	case IWL_RX_PHY_INFO_TYPE_HE_TB_EXT:
		iwl_mvm_decode_he_phy_ru_alloc(phy_data, rate_n_flags,
					       he, he_mu, rx_status);
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

static void iwl_mvm_rx_he(struct iwl_mvm *mvm, struct sk_buff *skb,
			  struct iwl_mvm_rx_phy_data *phy_data,
			  u32 rate_n_flags, u16 phy_info, int queue)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_radiotap_he *he = NULL;
	struct ieee80211_radiotap_he_mu *he_mu = NULL;
	u32 he_type = rate_n_flags & RATE_MCS_HE_TYPE_MSK;
	u8 stbc, ltf;
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
					   rate_n_flags, queue);

	/* update aggregation data for monitor sake on default queue */
	if (!queue && (phy_info & IWL_RX_MPDU_PHY_TSF_OVERLOAD) &&
	    (phy_info & IWL_RX_MPDU_PHY_AMPDU)) {
		bool toggle_bit = phy_info & IWL_RX_MPDU_PHY_AMPDU_TOGGLE;

		/* toggle is switched whenever new aggregation starts */
		if (toggle_bit != mvm->ampdu_toggle) {
			rx_status->flag |= RX_FLAG_AMPDU_EOF_BIT_KNOWN;
			if (phy_data->d0 & cpu_to_le32(IWL_RX_PHY_DATA0_HE_DELIM_EOF))
				rx_status->flag |= RX_FLAG_AMPDU_EOF_BIT;
		}
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

	stbc = (rate_n_flags & RATE_MCS_STBC_MSK) >> RATE_MCS_STBC_POS;
	rx_status->nss =
		((rate_n_flags & RATE_VHT_MCS_NSS_MSK) >>
					RATE_VHT_MCS_NSS_POS) + 1;
	rx_status->rate_idx = rate_n_flags & RATE_VHT_MCS_RATE_CODE_MSK;
	rx_status->encoding = RX_ENC_HE;
	rx_status->enc_flags |= stbc << RX_ENC_FLAG_STBC_SHIFT;
	if (rate_n_flags & RATE_MCS_BF_MSK)
		rx_status->enc_flags |= RX_ENC_FLAG_BF;

	rx_status->he_dcm =
		!!(rate_n_flags & RATE_HE_DUAL_CARRIER_MODE_MSK);

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
		if ((he_type == RATE_MCS_HE_TYPE_SU ||
		     he_type == RATE_MCS_HE_TYPE_EXT_SU) &&
		    rate_n_flags & RATE_MCS_SGI_MSK)
			rx_status->he_gi = NL80211_RATE_INFO_HE_GI_0_8;
		else
			rx_status->he_gi = NL80211_RATE_INFO_HE_GI_3_2;
		ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X;
		break;
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

void iwl_mvm_rx_mpdu_mq(struct iwl_mvm *mvm, struct napi_struct *napi,
			struct iwl_rx_cmd_buffer *rxb, int queue)
{
	struct ieee80211_rx_status *rx_status;
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_rx_mpdu_desc *desc = (void *)pkt->data;
	struct ieee80211_hdr *hdr;
	u32 len = le16_to_cpu(desc->mpdu_len);
	u32 rate_n_flags, gp2_on_air_rise;
	u16 phy_info = le16_to_cpu(desc->phy_info);
	struct ieee80211_sta *sta = NULL;
	struct sk_buff *skb;
	u8 crypt_len = 0, channel, energy_a, energy_b;
	size_t desc_size;
	struct iwl_mvm_rx_phy_data phy_data = {
		.d4 = desc->phy_data4,
		.info_type = IWL_RX_PHY_INFO_TYPE_NONE,
	};
	bool csi = false;

	if (unlikely(test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)))
		return;

	if (mvm->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_22560) {
		rate_n_flags = le32_to_cpu(desc->v3.rate_n_flags);
		channel = desc->v3.channel;
		gp2_on_air_rise = le32_to_cpu(desc->v3.gp2_on_air_rise);
		energy_a = desc->v3.energy_a;
		energy_b = desc->v3.energy_b;
		desc_size = sizeof(*desc);

		phy_data.d0 = desc->v3.phy_data0;
		phy_data.d1 = desc->v3.phy_data1;
		phy_data.d2 = desc->v3.phy_data2;
		phy_data.d3 = desc->v3.phy_data3;
	} else {
		rate_n_flags = le32_to_cpu(desc->v1.rate_n_flags);
		channel = desc->v1.channel;
		gp2_on_air_rise = le32_to_cpu(desc->v1.gp2_on_air_rise);
		energy_a = desc->v1.energy_a;
		energy_b = desc->v1.energy_b;
		desc_size = IWL_RX_DESC_SIZE_V1;

		phy_data.d0 = desc->v1.phy_data0;
		phy_data.d1 = desc->v1.phy_data1;
		phy_data.d2 = desc->v1.phy_data2;
		phy_data.d3 = desc->v1.phy_data3;
	}

	if (phy_info & IWL_RX_MPDU_PHY_TSF_OVERLOAD)
		phy_data.info_type =
			le32_get_bits(phy_data.d1,
				      IWL_RX_PHY_DATA1_INFO_TYPE_MASK);

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
	}

	if (rate_n_flags & RATE_MCS_HE_MSK)
		iwl_mvm_rx_he(mvm, skb, &phy_data, rate_n_flags,
			      phy_info, queue);

	iwl_mvm_decode_lsig(skb, &phy_data);

	rx_status = IEEE80211_SKB_RXCB(skb);

	if (iwl_mvm_rx_crypto(mvm, hdr, rx_status, phy_info, desc,
			      le32_to_cpu(pkt->len_n_flags), queue,
			      &crypt_len)) {
		kfree_skb(skb);
		return;
	}

	/*
	 * Keep packets with CRC errors (and with overrun) for monitor mode
	 * (otherwise the firmware discards them) but mark them as bad.
	 */
	if (!(desc->status & cpu_to_le16(IWL_RX_MPDU_STATUS_CRC_OK)) ||
	    !(desc->status & cpu_to_le16(IWL_RX_MPDU_STATUS_OVERRUN_OK))) {
		IWL_DEBUG_RX(mvm, "Bad CRC or FIFO: 0x%08X.\n",
			     le16_to_cpu(desc->status));
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
	}
	/* set the preamble flag if appropriate */
	if (rate_n_flags & RATE_MCS_CCK_MSK &&
	    phy_info & IWL_RX_MPDU_PHY_SHORT_PREAMBLE)
		rx_status->enc_flags |= RX_ENC_FLAG_SHORTPRE;

	if (likely(!(phy_info & IWL_RX_MPDU_PHY_TSF_OVERLOAD))) {
		u64 tsf_on_air_rise;

		if (mvm->trans->trans_cfg->device_family >=
		    IWL_DEVICE_FAMILY_22560)
			tsf_on_air_rise = le64_to_cpu(desc->v3.tsf_on_air_rise);
		else
			tsf_on_air_rise = le64_to_cpu(desc->v1.tsf_on_air_rise);

		rx_status->mactime = tsf_on_air_rise;
		/* TSF as indicated by the firmware is at INA time */
		rx_status->flag |= RX_FLAG_MACTIME_PLCP_START;
	}

	rx_status->device_timestamp = gp2_on_air_rise;
	rx_status->band = channel > 14 ? NL80211_BAND_5GHZ :
		NL80211_BAND_2GHZ;
	rx_status->freq = ieee80211_channel_to_frequency(channel,
							 rx_status->band);
	iwl_mvm_get_signal_strength(mvm, rx_status, rate_n_flags, energy_a,
				    energy_b);

	/* update aggregation data for monitor sake on default queue */
	if (!queue && (phy_info & IWL_RX_MPDU_PHY_AMPDU)) {
		bool toggle_bit = phy_info & IWL_RX_MPDU_PHY_AMPDU_TOGGLE;

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
		}
		rx_status->ampdu_reference = mvm->ampdu_ref;
	}

	if (unlikely(mvm->monitor_on))
		iwl_mvm_add_rtap_sniffer_config(mvm, skb);

	rcu_read_lock();

	if (desc->status & cpu_to_le16(IWL_RX_MPDU_STATUS_SRC_STA_FOUND)) {
		u8 id = desc->sta_id_flags & IWL_RX_MPDU_SIF_STA_ID_MASK;

		if (!WARN_ON_ONCE(id >= ARRAY_SIZE(mvm->fw_id_to_mac_id))) {
			sta = rcu_dereference(mvm->fw_id_to_mac_id[id]);
			if (IS_ERR(sta))
				sta = NULL;
		}
	} else if (!is_multicast_ether_addr(hdr->addr2)) {
		/*
		 * This is fine since we prevent two stations with the same
		 * address from being added.
		 */
		sta = ieee80211_find_sta_by_ifaddr(mvm->hw, hdr->addr2, NULL);
	}

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

			if (mvmvif->csa_target_freq == rx_status->freq)
				iwl_mvm_sta_modify_disable_tx_ap(mvm, sta,
								 false);
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
			iwl_mvm_rx_csum(sta, skb, desc);

		if (iwl_mvm_is_dup(sta, queue, rx_status, hdr, desc)) {
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
	}

	if (!(rate_n_flags & RATE_MCS_CCK_MSK) &&
	    rate_n_flags & RATE_MCS_SGI_MSK)
		rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
	if (rate_n_flags & RATE_HT_MCS_GF_MSK)
		rx_status->enc_flags |= RX_ENC_FLAG_HT_GF;
	if (rate_n_flags & RATE_MCS_LDPC_MSK)
		rx_status->enc_flags |= RX_ENC_FLAG_LDPC;
	if (rate_n_flags & RATE_MCS_HT_MSK) {
		u8 stbc = (rate_n_flags & RATE_MCS_STBC_MSK) >>
				RATE_MCS_STBC_POS;
		rx_status->encoding = RX_ENC_HT;
		rx_status->rate_idx = rate_n_flags & RATE_HT_MCS_INDEX_MSK;
		rx_status->enc_flags |= stbc << RX_ENC_FLAG_STBC_SHIFT;
	} else if (rate_n_flags & RATE_MCS_VHT_MSK) {
		u8 stbc = (rate_n_flags & RATE_MCS_STBC_MSK) >>
				RATE_MCS_STBC_POS;
		rx_status->nss =
			((rate_n_flags & RATE_VHT_MCS_NSS_MSK) >>
						RATE_VHT_MCS_NSS_POS) + 1;
		rx_status->rate_idx = rate_n_flags & RATE_VHT_MCS_RATE_CODE_MSK;
		rx_status->encoding = RX_ENC_VHT;
		rx_status->enc_flags |= stbc << RX_ENC_FLAG_STBC_SHIFT;
		if (rate_n_flags & RATE_MCS_BF_MSK)
			rx_status->enc_flags |= RX_ENC_FLAG_BF;
	} else if (!(rate_n_flags & RATE_MCS_HE_MSK)) {
		int rate = iwl_mvm_legacy_rate_to_mac80211_idx(rate_n_flags,
							       rx_status->band);

		if (WARN(rate < 0 || rate > 0xFF,
			 "Invalid rate flags 0x%x, band %d,\n",
			 rate_n_flags, rx_status->band)) {
			kfree_skb(skb);
			goto out;
		}
		rx_status->rate_idx = rate;
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

	if (!iwl_mvm_reorder(mvm, napi, queue, sta, skb, desc))
		iwl_mvm_pass_packet_to_mac80211(mvm, napi, skb, queue,
						sta, csi);
out:
	rcu_read_unlock();
}

void iwl_mvm_rx_monitor_no_data(struct iwl_mvm *mvm, struct napi_struct *napi,
				struct iwl_rx_cmd_buffer *rxb, int queue)
{
	struct ieee80211_rx_status *rx_status;
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_rx_no_data *desc = (void *)pkt->data;
	u32 rate_n_flags = le32_to_cpu(desc->rate);
	u32 gp2_on_air_rise = le32_to_cpu(desc->on_air_rise_time);
	u32 rssi = le32_to_cpu(desc->rssi);
	u32 info_type = le32_to_cpu(desc->info) & RX_NO_DATA_INFO_TYPE_MSK;
	u16 phy_info = IWL_RX_MPDU_PHY_TSF_OVERLOAD;
	struct ieee80211_sta *sta = NULL;
	struct sk_buff *skb;
	u8 channel, energy_a, energy_b;
	struct iwl_mvm_rx_phy_data phy_data = {
		.d0 = desc->phy_info[0],
		.info_type = IWL_RX_PHY_INFO_TYPE_NONE,
	};

	if (unlikely(test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)))
		return;

	energy_a = (rssi & RX_NO_DATA_CHAIN_A_MSK) >> RX_NO_DATA_CHAIN_A_POS;
	energy_b = (rssi & RX_NO_DATA_CHAIN_B_MSK) >> RX_NO_DATA_CHAIN_B_POS;
	channel = (rssi & RX_NO_DATA_CHANNEL_MSK) >> RX_NO_DATA_CHANNEL_POS;

	phy_data.info_type =
		le32_get_bits(desc->phy_info[1],
			      IWL_RX_PHY_DATA1_INFO_TYPE_MASK);

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

	switch (info_type) {
	case RX_NO_DATA_INFO_TYPE_NDP:
		rx_status->zero_length_psdu_type =
			IEEE80211_RADIOTAP_ZERO_LEN_PSDU_SOUNDING;
		break;
	case RX_NO_DATA_INFO_TYPE_MU_UNMATCHED:
	case RX_NO_DATA_INFO_TYPE_HE_TB_UNMATCHED:
		rx_status->zero_length_psdu_type =
			IEEE80211_RADIOTAP_ZERO_LEN_PSDU_NOT_CAPTURED;
		break;
	default:
		rx_status->zero_length_psdu_type =
			IEEE80211_RADIOTAP_ZERO_LEN_PSDU_VENDOR;
		break;
	}

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
	}

	if (rate_n_flags & RATE_MCS_HE_MSK)
		iwl_mvm_rx_he(mvm, skb, &phy_data, rate_n_flags,
			      phy_info, queue);

	iwl_mvm_decode_lsig(skb, &phy_data);

	rx_status->device_timestamp = gp2_on_air_rise;
	rx_status->band = channel > 14 ? NL80211_BAND_5GHZ :
		NL80211_BAND_2GHZ;
	rx_status->freq = ieee80211_channel_to_frequency(channel,
							 rx_status->band);
	iwl_mvm_get_signal_strength(mvm, rx_status, rate_n_flags, energy_a,
				    energy_b);

	rcu_read_lock();

	if (!(rate_n_flags & RATE_MCS_CCK_MSK) &&
	    rate_n_flags & RATE_MCS_SGI_MSK)
		rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
	if (rate_n_flags & RATE_HT_MCS_GF_MSK)
		rx_status->enc_flags |= RX_ENC_FLAG_HT_GF;
	if (rate_n_flags & RATE_MCS_LDPC_MSK)
		rx_status->enc_flags |= RX_ENC_FLAG_LDPC;
	if (rate_n_flags & RATE_MCS_HT_MSK) {
		u8 stbc = (rate_n_flags & RATE_MCS_STBC_MSK) >>
				RATE_MCS_STBC_POS;
		rx_status->encoding = RX_ENC_HT;
		rx_status->rate_idx = rate_n_flags & RATE_HT_MCS_INDEX_MSK;
		rx_status->enc_flags |= stbc << RX_ENC_FLAG_STBC_SHIFT;
	} else if (rate_n_flags & RATE_MCS_VHT_MSK) {
		u8 stbc = (rate_n_flags & RATE_MCS_STBC_MSK) >>
				RATE_MCS_STBC_POS;
		rx_status->rate_idx = rate_n_flags & RATE_VHT_MCS_RATE_CODE_MSK;
		rx_status->encoding = RX_ENC_VHT;
		rx_status->enc_flags |= stbc << RX_ENC_FLAG_STBC_SHIFT;
		if (rate_n_flags & RATE_MCS_BF_MSK)
			rx_status->enc_flags |= RX_ENC_FLAG_BF;
		/*
		 * take the nss from the rx_vec since the rate_n_flags has
		 * only 2 bits for the nss which gives a max of 4 ss but
		 * there may be up to 8 spatial streams
		 */
		rx_status->nss =
			le32_get_bits(desc->rx_vec[0],
				      RX_NO_DATA_RX_VEC0_VHT_NSTS_MSK) + 1;
	} else if (rate_n_flags & RATE_MCS_HE_MSK) {
		rx_status->nss =
			le32_get_bits(desc->rx_vec[0],
				      RX_NO_DATA_RX_VEC0_HE_NSTS_MSK) + 1;
	} else {
		int rate = iwl_mvm_legacy_rate_to_mac80211_idx(rate_n_flags,
							       rx_status->band);

		if (WARN(rate < 0 || rate > 0xFF,
			 "Invalid rate flags 0x%x, band %d,\n",
			 rate_n_flags, rx_status->band)) {
			kfree_skb(skb);
			goto out;
		}
		rx_status->rate_idx = rate;
	}

	ieee80211_rx_napi(mvm->hw, sta, skb, napi);
out:
	rcu_read_unlock();
}

void iwl_mvm_rx_frame_release(struct iwl_mvm *mvm, struct napi_struct *napi,
			      struct iwl_rx_cmd_buffer *rxb, int queue)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_frame_release *release = (void *)pkt->data;

	iwl_mvm_release_frames_from_notif(mvm, napi, release->baid,
					  le16_to_cpu(release->nssn),
					  queue, 0);
}

void iwl_mvm_rx_bar_frame_release(struct iwl_mvm *mvm, struct napi_struct *napi,
				  struct iwl_rx_cmd_buffer *rxb, int queue)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_bar_frame_release *release = (void *)pkt->data;
	unsigned int baid = le32_get_bits(release->ba_info,
					  IWL_BAR_FRAME_RELEASE_BAID_MASK);
	unsigned int nssn = le32_get_bits(release->ba_info,
					  IWL_BAR_FRAME_RELEASE_NSSN_MASK);
	unsigned int sta_id = le32_get_bits(release->sta_tid,
					    IWL_BAR_FRAME_RELEASE_STA_MASK);
	unsigned int tid = le32_get_bits(release->sta_tid,
					 IWL_BAR_FRAME_RELEASE_TID_MASK);
	struct iwl_mvm_baid_data *baid_data;

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

	if (WARN(tid != baid_data->tid || sta_id != baid_data->sta_id,
		 "baid 0x%x is mapped to sta:%d tid:%d, but BAR release received for sta:%d tid:%d\n",
		 baid, baid_data->sta_id, baid_data->tid, sta_id,
		 tid))
		goto out;

	iwl_mvm_release_frames_from_notif(mvm, napi, baid, nssn, queue, 0);
out:
	rcu_read_unlock();
}
