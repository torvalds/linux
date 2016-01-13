/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2015        Intel Deutschland GmbH
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
 * Copyright(c) 2015        Intel Deutschland GmbH
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
#include "fw-dbg.h"

void iwl_mvm_rx_phy_cmd_mq(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb)
{
	mvm->ampdu_ref++;

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (mvm->last_phy_info.phy_flags & cpu_to_le16(RX_RES_PHY_FLAGS_AGG)) {
		spin_lock(&mvm->drv_stats_lock);
		mvm->drv_rx_stats.ampdu_count++;
		spin_unlock(&mvm->drv_stats_lock);
	}
#endif
}

static inline int iwl_mvm_check_pn(struct iwl_mvm *mvm, struct sk_buff *skb,
				   int queue, struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvmsta;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_rx_status *stats = IEEE80211_SKB_RXCB(skb);
	struct iwl_mvm_key_pn *ptk_pn;
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
		tid = *ieee80211_get_qos_ctl(hdr) & IEEE80211_QOS_CTL_TID_MASK;
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

	if (memcmp(pn, ptk_pn->q[queue].pn[tid],
		   IEEE80211_CCMP_PN_LEN) <= 0)
		return -1;

	memcpy(ptk_pn->q[queue].pn[tid], pn, IEEE80211_CCMP_PN_LEN);
	stats->flag |= RX_FLAG_PN_VALIDATED;

	return 0;
}

/* iwl_mvm_create_skb Adds the rxb to a new skb */
static void iwl_mvm_create_skb(struct sk_buff *skb, struct ieee80211_hdr *hdr,
			       u16 len, u8 crypt_len,
			       struct iwl_rx_cmd_buffer *rxb)
{
	unsigned int hdrlen, fraglen;

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
	hdrlen = (len <= skb_tailroom(skb)) ? len :
					      sizeof(*hdr) + crypt_len + 8;

	memcpy(skb_put(skb, hdrlen), hdr, hdrlen);
	fraglen = len - hdrlen;

	if (fraglen) {
		int offset = (void *)hdr + hdrlen -
			     rxb_addr(rxb) + rxb_offset(rxb);

		skb_add_rx_frag(skb, 0, rxb_steal_page(rxb), offset,
				fraglen, rxb->truesize);
	}
}

/* iwl_mvm_pass_packet_to_mac80211 - passes the packet for mac80211 */
static void iwl_mvm_pass_packet_to_mac80211(struct iwl_mvm *mvm,
					    struct napi_struct *napi,
					    struct sk_buff *skb, int queue,
					    struct ieee80211_sta *sta)
{
	if (iwl_mvm_check_pn(mvm, skb, queue, sta))
		kfree_skb(skb);
	else
		ieee80211_rx_napi(mvm->hw, skb, napi);
}

static void iwl_mvm_get_signal_strength(struct iwl_mvm *mvm,
					struct iwl_rx_mpdu_desc *desc,
					struct ieee80211_rx_status *rx_status)
{
	int energy_a, energy_b, energy_c, max_energy;

	energy_a = desc->energy_a;
	energy_a = energy_a ? -energy_a : S8_MIN;
	energy_b = desc->energy_b;
	energy_b = energy_b ? -energy_b : S8_MIN;
	energy_c = desc->energy_c;
	energy_c = energy_c ? -energy_c : S8_MIN;
	max_energy = max(energy_a, energy_b);
	max_energy = max(max_energy, energy_c);

	IWL_DEBUG_STATS(mvm, "energy In A %d B %d C %d , and max %d\n",
			energy_a, energy_b, energy_c, max_energy);

	rx_status->signal = max_energy;
	rx_status->chains = 0; /* TODO: phy info */
	rx_status->chain_signal[0] = energy_a;
	rx_status->chain_signal[1] = energy_b;
	rx_status->chain_signal[2] = energy_c;
}

static int iwl_mvm_rx_crypto(struct iwl_mvm *mvm, struct ieee80211_hdr *hdr,
			     struct ieee80211_rx_status *stats,
			     struct iwl_rx_mpdu_desc *desc, int queue,
			     u8 *crypt_len)
{
	u16 status = le16_to_cpu(desc->status);

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
		*crypt_len = IEEE80211_CCMP_HDR_LEN;
		return 0;
	case IWL_RX_MPDU_STATUS_SEC_TKIP:
		/* Don't drop the frame and decrypt it in SW */
		if (!(status & IWL_RX_MPDU_RES_STATUS_TTAK_OK))
			return 0;

		*crypt_len = IEEE80211_TKIP_IV_LEN;
		/* fall through if TTAK OK */
	case IWL_RX_MPDU_STATUS_SEC_WEP:
		if (!(status & IWL_RX_MPDU_STATUS_ICV_OK))
			return -1;

		stats->flag |= RX_FLAG_DECRYPTED;
		if ((status & IWL_RX_MPDU_STATUS_SEC_MASK) ==
				IWL_RX_MPDU_STATUS_SEC_WEP)
			*crypt_len = IEEE80211_WEP_IV_LEN;
		return 0;
	case IWL_RX_MPDU_STATUS_SEC_EXT_ENC:
		if (!(status & IWL_RX_MPDU_STATUS_MIC_OK))
			return -1;
		stats->flag |= RX_FLAG_DECRYPTED;
		return 0;
	default:
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

	if (mvmvif->features & NETIF_F_RXCSUM &&
	    desc->l3l4_flags & cpu_to_le16(IWL_RX_L3L4_IP_HDR_CSUM_OK) &&
	    desc->l3l4_flags & cpu_to_le16(IWL_RX_L3L4_TCP_UDP_CSUM_OK))
		skb->ip_summed = CHECKSUM_UNNECESSARY;
}

void iwl_mvm_rx_mpdu_mq(struct iwl_mvm *mvm, struct napi_struct *napi,
			struct iwl_rx_cmd_buffer *rxb, int queue)
{
	struct ieee80211_rx_status *rx_status;
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_rx_mpdu_desc *desc = (void *)pkt->data;
	struct ieee80211_hdr *hdr = (void *)(desc + 1);
	u32 len = le16_to_cpu(desc->mpdu_len);
	u32 rate_n_flags = le32_to_cpu(desc->rate_n_flags);
	struct ieee80211_sta *sta = NULL;
	struct sk_buff *skb;
	u8 crypt_len = 0;

	/* Dont use dev_alloc_skb(), we'll have enough headroom once
	 * ieee80211_hdr pulled.
	 */
	skb = alloc_skb(128, GFP_ATOMIC);
	if (!skb) {
		IWL_ERR(mvm, "alloc_skb failed\n");
		return;
	}

	rx_status = IEEE80211_SKB_RXCB(skb);

	if (iwl_mvm_rx_crypto(mvm, hdr, rx_status, desc, queue, &crypt_len)) {
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

	rx_status->mactime = le64_to_cpu(desc->tsf_on_air_rise);
	rx_status->device_timestamp = le32_to_cpu(desc->gp2_on_air_rise);
	rx_status->band = desc->channel > 14 ? IEEE80211_BAND_5GHZ :
					       IEEE80211_BAND_2GHZ;
	rx_status->freq = ieee80211_channel_to_frequency(desc->channel,
							 rx_status->band);
	iwl_mvm_get_signal_strength(mvm, desc, rx_status);

	rcu_read_lock();

	if (le16_to_cpu(desc->status) & IWL_RX_MPDU_STATUS_SRC_STA_FOUND) {
		u8 id = desc->sta_id_flags & IWL_RX_MPDU_SIF_STA_ID_MASK;

		if (!WARN_ON_ONCE(id >= IWL_MVM_STATION_COUNT)) {
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

		/*
		 * We have tx blocked stations (with CS bit). If we heard
		 * frames from a blocked station on a new channel we can
		 * TX to it again.
		 */
		if (unlikely(mvm->csa_tx_block_bcn_timeout))
			iwl_mvm_sta_modify_disable_tx_ap(mvm, sta, false);

		rs_update_last_rssi(mvm, &mvmsta->lq_sta, rx_status);

		if (iwl_fw_dbg_trigger_enabled(mvm->fw, FW_DBG_TRIGGER_RSSI) &&
		    ieee80211_is_beacon(hdr->frame_control)) {
			struct iwl_fw_dbg_trigger_tlv *trig;
			struct iwl_fw_dbg_trigger_low_rssi *rssi_trig;
			bool trig_check;
			s32 rssi;

			trig = iwl_fw_dbg_get_trigger(mvm->fw,
						      FW_DBG_TRIGGER_RSSI);
			rssi_trig = (void *)trig->data;
			rssi = le32_to_cpu(rssi_trig->rssi);

			trig_check =
				iwl_fw_dbg_trigger_check_stop(mvm, mvmsta->vif,
							      trig);
			if (trig_check && rx_status->signal < rssi)
				iwl_mvm_fw_dbg_collect_trig(mvm, trig, NULL);
		}

		/* TODO: multi queue TCM */

		if (ieee80211_is_data(hdr->frame_control))
			iwl_mvm_rx_csum(sta, skb, desc);
	}

	/*
	 * TODO: PHY info.
	 * Verify we don't have the information in the MPDU descriptor and
	 * that it is not needed.
	 * Make sure for monitor mode that we are on default queue, update
	 * ampdu_ref and the rest of phy info then
	 */

	/* Set up the HT phy flags */
	switch (rate_n_flags & RATE_MCS_CHAN_WIDTH_MSK) {
	case RATE_MCS_CHAN_WIDTH_20:
		break;
	case RATE_MCS_CHAN_WIDTH_40:
		rx_status->flag |= RX_FLAG_40MHZ;
		break;
	case RATE_MCS_CHAN_WIDTH_80:
		rx_status->vht_flag |= RX_VHT_FLAG_80MHZ;
		break;
	case RATE_MCS_CHAN_WIDTH_160:
		rx_status->vht_flag |= RX_VHT_FLAG_160MHZ;
		break;
	}
	if (rate_n_flags & RATE_MCS_SGI_MSK)
		rx_status->flag |= RX_FLAG_SHORT_GI;
	if (rate_n_flags & RATE_HT_MCS_GF_MSK)
		rx_status->flag |= RX_FLAG_HT_GF;
	if (rate_n_flags & RATE_MCS_LDPC_MSK)
		rx_status->flag |= RX_FLAG_LDPC;
	if (rate_n_flags & RATE_MCS_HT_MSK) {
		u8 stbc = (rate_n_flags & RATE_MCS_HT_STBC_MSK) >>
				RATE_MCS_STBC_POS;
		rx_status->flag |= RX_FLAG_HT;
		rx_status->rate_idx = rate_n_flags & RATE_HT_MCS_INDEX_MSK;
		rx_status->flag |= stbc << RX_FLAG_STBC_SHIFT;
	} else if (rate_n_flags & RATE_MCS_VHT_MSK) {
		u8 stbc = (rate_n_flags & RATE_MCS_VHT_STBC_MSK) >>
				RATE_MCS_STBC_POS;
		rx_status->vht_nss =
			((rate_n_flags & RATE_VHT_MCS_NSS_MSK) >>
						RATE_VHT_MCS_NSS_POS) + 1;
		rx_status->rate_idx = rate_n_flags & RATE_VHT_MCS_RATE_CODE_MSK;
		rx_status->flag |= RX_FLAG_VHT;
		rx_status->flag |= stbc << RX_FLAG_STBC_SHIFT;
		if (rate_n_flags & RATE_MCS_BF_MSK)
			rx_status->vht_flag |= RX_VHT_FLAG_BF;
	} else {
		rx_status->rate_idx =
			iwl_mvm_legacy_rate_to_mac80211_idx(rate_n_flags,
							    rx_status->band);
	}

	/* TODO: PHY info - update ampdu queue statistics (for debugfs) */
	/* TODO: PHY info - gscan */

	iwl_mvm_create_skb(skb, hdr, len, crypt_len, rxb);
	iwl_mvm_pass_packet_to_mac80211(mvm, napi, skb, queue, sta);
	rcu_read_unlock();
}

void iwl_mvm_rx_frame_release(struct iwl_mvm *mvm,
			      struct iwl_rx_cmd_buffer *rxb, int queue)
{
	/* TODO */
}
