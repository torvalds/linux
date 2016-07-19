/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016        Intel Deutschland GmbH
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
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
 *
 *****************************************************************************/
#include <linux/ieee80211.h>
#include <linux/etherdevice.h>
#include <linux/tcp.h>
#include <net/ip.h>

#include "iwl-trans.h"
#include "iwl-eeprom-parse.h"
#include "mvm.h"
#include "sta.h"
#include "fw-dbg.h"

static void
iwl_mvm_bar_check_trigger(struct iwl_mvm *mvm, const u8 *addr,
			  u16 tid, u16 ssn)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_ba *ba_trig;

	if (!iwl_fw_dbg_trigger_enabled(mvm->fw, FW_DBG_TRIGGER_BA))
		return;

	trig = iwl_fw_dbg_get_trigger(mvm->fw, FW_DBG_TRIGGER_BA);
	ba_trig = (void *)trig->data;

	if (!iwl_fw_dbg_trigger_check_stop(mvm, NULL, trig))
		return;

	if (!(le16_to_cpu(ba_trig->tx_bar) & BIT(tid)))
		return;

	iwl_mvm_fw_dbg_collect_trig(mvm, trig,
				    "BAR sent to %pM, tid %d, ssn %d",
				    addr, tid, ssn);
}

/*
 * Sets most of the Tx cmd's fields
 */
void iwl_mvm_set_tx_cmd(struct iwl_mvm *mvm, struct sk_buff *skb,
			struct iwl_tx_cmd *tx_cmd,
			struct ieee80211_tx_info *info, u8 sta_id)
{
	struct ieee80211_tx_info *skb_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	__le16 fc = hdr->frame_control;
	u32 tx_flags = le32_to_cpu(tx_cmd->tx_flags);
	u32 len = skb->len + FCS_LEN;
	u8 ac;

	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
		tx_flags |= TX_CMD_FLG_ACK;
	else
		tx_flags &= ~TX_CMD_FLG_ACK;

	if (ieee80211_is_probe_resp(fc))
		tx_flags |= TX_CMD_FLG_TSF;

	if (ieee80211_has_morefrags(fc))
		tx_flags |= TX_CMD_FLG_MORE_FRAG;

	if (ieee80211_is_data_qos(fc)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);
		tx_cmd->tid_tspec = qc[0] & 0xf;
		tx_flags &= ~TX_CMD_FLG_SEQ_CTL;
	} else if (ieee80211_is_back_req(fc)) {
		struct ieee80211_bar *bar = (void *)skb->data;
		u16 control = le16_to_cpu(bar->control);
		u16 ssn = le16_to_cpu(bar->start_seq_num);

		tx_flags |= TX_CMD_FLG_ACK | TX_CMD_FLG_BAR;
		tx_cmd->tid_tspec = (control &
				     IEEE80211_BAR_CTRL_TID_INFO_MASK) >>
			IEEE80211_BAR_CTRL_TID_INFO_SHIFT;
		WARN_ON_ONCE(tx_cmd->tid_tspec >= IWL_MAX_TID_COUNT);
		iwl_mvm_bar_check_trigger(mvm, bar->ra, tx_cmd->tid_tspec,
					  ssn);
	} else {
		tx_cmd->tid_tspec = IWL_TID_NON_QOS;
		if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ)
			tx_flags |= TX_CMD_FLG_SEQ_CTL;
		else
			tx_flags &= ~TX_CMD_FLG_SEQ_CTL;
	}

	/* Default to 0 (BE) when tid_spec is set to IWL_TID_NON_QOS */
	if (tx_cmd->tid_tspec < IWL_MAX_TID_COUNT)
		ac = tid_to_mac80211_ac[tx_cmd->tid_tspec];
	else
		ac = tid_to_mac80211_ac[0];

	tx_flags |= iwl_mvm_bt_coex_tx_prio(mvm, hdr, info, ac) <<
			TX_CMD_FLG_BT_PRIO_POS;

	if (ieee80211_is_mgmt(fc)) {
		if (ieee80211_is_assoc_req(fc) || ieee80211_is_reassoc_req(fc))
			tx_cmd->pm_frame_timeout = cpu_to_le16(PM_FRAME_ASSOC);
		else if (ieee80211_is_action(fc))
			tx_cmd->pm_frame_timeout = cpu_to_le16(PM_FRAME_NONE);
		else
			tx_cmd->pm_frame_timeout = cpu_to_le16(PM_FRAME_MGMT);

		/* The spec allows Action frames in A-MPDU, we don't support
		 * it
		 */
		WARN_ON_ONCE(info->flags & IEEE80211_TX_CTL_AMPDU);
	} else if (info->control.flags & IEEE80211_TX_CTRL_PORT_CTRL_PROTO) {
		tx_cmd->pm_frame_timeout = cpu_to_le16(PM_FRAME_MGMT);
	} else {
		tx_cmd->pm_frame_timeout = cpu_to_le16(PM_FRAME_NONE);
	}

	if (ieee80211_is_data(fc) && len > mvm->rts_threshold &&
	    !is_multicast_ether_addr(ieee80211_get_DA(hdr)))
		tx_flags |= TX_CMD_FLG_PROT_REQUIRE;

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_TXPOWER_INSERTION_SUPPORT) &&
	    ieee80211_action_contains_tpc(skb))
		tx_flags |= TX_CMD_FLG_WRITE_TX_POWER;

	tx_cmd->tx_flags = cpu_to_le32(tx_flags);
	/* Total # bytes to be transmitted */
	tx_cmd->len = cpu_to_le16((u16)skb->len +
		(uintptr_t)skb_info->driver_data[0]);
	tx_cmd->next_frame_len = 0;
	tx_cmd->life_time = cpu_to_le32(TX_CMD_LIFE_TIME_INFINITE);
	tx_cmd->sta_id = sta_id;
}

/*
 * Sets the fields in the Tx cmd that are rate related
 */
void iwl_mvm_set_tx_cmd_rate(struct iwl_mvm *mvm, struct iwl_tx_cmd *tx_cmd,
			    struct ieee80211_tx_info *info,
			    struct ieee80211_sta *sta, __le16 fc)
{
	u32 rate_flags;
	int rate_idx;
	u8 rate_plcp;

	/* Set retry limit on RTS packets */
	tx_cmd->rts_retry_limit = IWL_RTS_DFAULT_RETRY_LIMIT;

	/* Set retry limit on DATA packets and Probe Responses*/
	if (ieee80211_is_probe_resp(fc)) {
		tx_cmd->data_retry_limit = IWL_MGMT_DFAULT_RETRY_LIMIT;
		tx_cmd->rts_retry_limit =
			min(tx_cmd->data_retry_limit, tx_cmd->rts_retry_limit);
	} else if (ieee80211_is_back_req(fc)) {
		tx_cmd->data_retry_limit = IWL_BAR_DFAULT_RETRY_LIMIT;
	} else {
		tx_cmd->data_retry_limit = IWL_DEFAULT_TX_RETRY;
	}

	/*
	 * for data packets, rate info comes from the table inside the fw. This
	 * table is controlled by LINK_QUALITY commands
	 */

	if (ieee80211_is_data(fc) && sta) {
		tx_cmd->initial_rate_index = 0;
		tx_cmd->tx_flags |= cpu_to_le32(TX_CMD_FLG_STA_RATE);
		return;
	} else if (ieee80211_is_back_req(fc)) {
		tx_cmd->tx_flags |=
			cpu_to_le32(TX_CMD_FLG_ACK | TX_CMD_FLG_BAR);
	}

	/* HT rate doesn't make sense for a non data frame */
	WARN_ONCE(info->control.rates[0].flags & IEEE80211_TX_RC_MCS,
		  "Got an HT rate (flags:0x%x/mcs:%d) for a non data frame (fc:0x%x)\n",
		  info->control.rates[0].flags,
		  info->control.rates[0].idx,
		  le16_to_cpu(fc));

	rate_idx = info->control.rates[0].idx;
	/* if the rate isn't a well known legacy rate, take the lowest one */
	if (rate_idx < 0 || rate_idx > IWL_RATE_COUNT_LEGACY)
		rate_idx = rate_lowest_index(
				&mvm->nvm_data->bands[info->band], sta);

	/* For 5 GHZ band, remap mac80211 rate indices into driver indices */
	if (info->band == IEEE80211_BAND_5GHZ)
		rate_idx += IWL_FIRST_OFDM_RATE;

	/* For 2.4 GHZ band, check that there is no need to remap */
	BUILD_BUG_ON(IWL_FIRST_CCK_RATE != 0);

	/* Get PLCP rate for tx_cmd->rate_n_flags */
	rate_plcp = iwl_mvm_mac80211_idx_to_hwrate(rate_idx);

	mvm->mgmt_last_antenna_idx =
		iwl_mvm_next_antenna(mvm, iwl_mvm_get_valid_tx_ant(mvm),
				     mvm->mgmt_last_antenna_idx);

	if (info->band == IEEE80211_BAND_2GHZ &&
	    !iwl_mvm_bt_coex_is_shared_ant_avail(mvm))
		rate_flags = mvm->cfg->non_shared_ant << RATE_MCS_ANT_POS;
	else
		rate_flags =
			BIT(mvm->mgmt_last_antenna_idx) << RATE_MCS_ANT_POS;

	/* Set CCK flag as needed */
	if ((rate_idx >= IWL_FIRST_CCK_RATE) && (rate_idx <= IWL_LAST_CCK_RATE))
		rate_flags |= RATE_MCS_CCK_MSK;

	/* Set the rate in the TX cmd */
	tx_cmd->rate_n_flags = cpu_to_le32((u32)rate_plcp | rate_flags);
}

/*
 * Sets the fields in the Tx cmd that are crypto related
 */
static void iwl_mvm_set_tx_cmd_crypto(struct iwl_mvm *mvm,
				      struct ieee80211_tx_info *info,
				      struct iwl_tx_cmd *tx_cmd,
				      struct sk_buff *skb_frag,
				      int hdrlen)
{
	struct ieee80211_key_conf *keyconf = info->control.hw_key;
	u8 *crypto_hdr = skb_frag->data + hdrlen;
	u64 pn;

	switch (keyconf->cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
		iwl_mvm_set_tx_cmd_ccmp(info, tx_cmd);
		pn = atomic64_inc_return(&keyconf->tx_pn);
		crypto_hdr[0] = pn;
		crypto_hdr[2] = 0;
		crypto_hdr[3] = 0x20 | (keyconf->keyidx << 6);
		crypto_hdr[1] = pn >> 8;
		crypto_hdr[4] = pn >> 16;
		crypto_hdr[5] = pn >> 24;
		crypto_hdr[6] = pn >> 32;
		crypto_hdr[7] = pn >> 40;
		break;

	case WLAN_CIPHER_SUITE_TKIP:
		tx_cmd->sec_ctl = TX_CMD_SEC_TKIP;
		pn = atomic64_inc_return(&keyconf->tx_pn);
		ieee80211_tkip_add_iv(crypto_hdr, keyconf, pn);
		ieee80211_get_tkip_p2k(keyconf, skb_frag, tx_cmd->key);
		break;

	case WLAN_CIPHER_SUITE_WEP104:
		tx_cmd->sec_ctl |= TX_CMD_SEC_KEY128;
		/* fall through */
	case WLAN_CIPHER_SUITE_WEP40:
		tx_cmd->sec_ctl |= TX_CMD_SEC_WEP |
			((keyconf->keyidx << TX_CMD_SEC_WEP_KEY_IDX_POS) &
			  TX_CMD_SEC_WEP_KEY_IDX_MSK);

		memcpy(&tx_cmd->key[3], keyconf->key, keyconf->keylen);
		break;
	default:
		tx_cmd->sec_ctl |= TX_CMD_SEC_EXT;
	}
}

/*
 * Allocates and sets the Tx cmd the driver data pointers in the skb
 */
static struct iwl_device_cmd *
iwl_mvm_set_tx_params(struct iwl_mvm *mvm, struct sk_buff *skb,
		      struct ieee80211_tx_info *info, int hdrlen,
		      struct ieee80211_sta *sta, u8 sta_id)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *skb_info = IEEE80211_SKB_CB(skb);
	struct iwl_device_cmd *dev_cmd;
	struct iwl_tx_cmd *tx_cmd;

	dev_cmd = iwl_trans_alloc_tx_cmd(mvm->trans);

	if (unlikely(!dev_cmd))
		return NULL;

	memset(dev_cmd, 0, sizeof(*dev_cmd));
	dev_cmd->hdr.cmd = TX_CMD;
	tx_cmd = (struct iwl_tx_cmd *)dev_cmd->payload;

	if (info->control.hw_key)
		iwl_mvm_set_tx_cmd_crypto(mvm, info, tx_cmd, skb, hdrlen);

	iwl_mvm_set_tx_cmd(mvm, skb, tx_cmd, info, sta_id);

	iwl_mvm_set_tx_cmd_rate(mvm, tx_cmd, info, sta, hdr->frame_control);

	memset(&skb_info->status, 0, sizeof(skb_info->status));
	memset(skb_info->driver_data, 0, sizeof(skb_info->driver_data));

	skb_info->driver_data[1] = dev_cmd;

	return dev_cmd;
}

int iwl_mvm_tx_skb_non_sta(struct iwl_mvm *mvm, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *skb_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_info info;
	struct iwl_device_cmd *dev_cmd;
	struct iwl_tx_cmd *tx_cmd;
	u8 sta_id;
	int hdrlen = ieee80211_hdrlen(hdr->frame_control);

	memcpy(&info, skb->cb, sizeof(info));

	if (WARN_ON_ONCE(info.flags & IEEE80211_TX_CTL_AMPDU))
		return -1;

	if (WARN_ON_ONCE(info.flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM &&
			 (!info.control.vif ||
			  info.hw_queue != info.control.vif->cab_queue)))
		return -1;

	/* This holds the amsdu headers length */
	skb_info->driver_data[0] = (void *)(uintptr_t)0;

	/*
	 * IWL_MVM_OFFCHANNEL_QUEUE is used for ROC packets that can be used
	 * in 2 different types of vifs, P2P & STATION. P2P uses the offchannel
	 * queue. STATION (HS2.0) uses the auxiliary context of the FW,
	 * and hence needs to be sent on the aux queue
	 */
	if (IEEE80211_SKB_CB(skb)->hw_queue == IWL_MVM_OFFCHANNEL_QUEUE &&
	    info.control.vif->type == NL80211_IFTYPE_STATION)
		IEEE80211_SKB_CB(skb)->hw_queue = mvm->aux_queue;

	/*
	 * If the interface on which the frame is sent is the P2P_DEVICE
	 * or an AP/GO interface use the broadcast station associated
	 * with it; otherwise if the interface is a managed interface
	 * use the AP station associated with it for multicast traffic
	 * (this is not possible for unicast packets as a TLDS discovery
	 * response are sent without a station entry); otherwise use the
	 * AUX station.
	 */
	sta_id = mvm->aux_sta.sta_id;
	if (info.control.vif) {
		struct iwl_mvm_vif *mvmvif =
			iwl_mvm_vif_from_mac80211(info.control.vif);

		if (info.control.vif->type == NL80211_IFTYPE_P2P_DEVICE ||
		    info.control.vif->type == NL80211_IFTYPE_AP)
			sta_id = mvmvif->bcast_sta.sta_id;
		else if (info.control.vif->type == NL80211_IFTYPE_STATION &&
			 is_multicast_ether_addr(hdr->addr1)) {
			u8 ap_sta_id = ACCESS_ONCE(mvmvif->ap_sta_id);

			if (ap_sta_id != IWL_MVM_STATION_COUNT)
				sta_id = ap_sta_id;
		}
	}

	IWL_DEBUG_TX(mvm, "station Id %d, queue=%d\n", sta_id, info.hw_queue);

	dev_cmd = iwl_mvm_set_tx_params(mvm, skb, &info, hdrlen, NULL, sta_id);
	if (!dev_cmd)
		return -1;

	tx_cmd = (struct iwl_tx_cmd *)dev_cmd->payload;

	/* Copy MAC header from skb into command buffer */
	memcpy(tx_cmd->hdr, hdr, hdrlen);

	if (iwl_trans_tx(mvm->trans, skb, dev_cmd, info.hw_queue)) {
		iwl_trans_free_tx_cmd(mvm->trans, dev_cmd);
		return -1;
	}

	/*
	 * Increase the pending frames counter, so that later when a reply comes
	 * in and the counter is decreased - we don't start getting negative
	 * values.
	 * Note that we don't need to make sure it isn't agg'd, since we're
	 * TXing non-sta
	 */
	atomic_inc(&mvm->pending_frames[sta_id]);

	return 0;
}

#ifdef CONFIG_INET
static int iwl_mvm_tx_tso(struct iwl_mvm *mvm, struct sk_buff *skb,
			  struct ieee80211_tx_info *info,
			  struct ieee80211_sta *sta,
			  struct sk_buff_head *mpdus_skb)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	unsigned int mss = skb_shinfo(skb)->gso_size;
	struct sk_buff *tmp, *next;
	char cb[sizeof(skb->cb)];
	unsigned int num_subframes, tcp_payload_len, subf_len, max_amsdu_len;
	bool ipv4 = (skb->protocol == htons(ETH_P_IP));
	u16 ip_base_id = ipv4 ? ntohs(ip_hdr(skb)->id) : 0;
	u16 amsdu_add, snap_ip_tcp, pad, i = 0;
	unsigned int dbg_max_amsdu_len;
	u8 *qc, tid, txf;

	snap_ip_tcp = 8 + skb_transport_header(skb) - skb_network_header(skb) +
		tcp_hdrlen(skb);

	qc = ieee80211_get_qos_ctl(hdr);
	tid = *qc & IEEE80211_QOS_CTL_TID_MASK;
	if (WARN_ON_ONCE(tid >= IWL_MAX_TID_COUNT))
		return -EINVAL;

	if (!sta->max_amsdu_len ||
	    !ieee80211_is_data_qos(hdr->frame_control) ||
	    !mvmsta->tlc_amsdu) {
		num_subframes = 1;
		pad = 0;
		goto segment;
	}

	/*
	 * No need to lock amsdu_in_ampdu_allowed since it can't be modified
	 * during an BA session.
	 */
	if (info->flags & IEEE80211_TX_CTL_AMPDU &&
	    !mvmsta->tid_data[tid].amsdu_in_ampdu_allowed) {
		num_subframes = 1;
		pad = 0;
		goto segment;
	}

	max_amsdu_len = sta->max_amsdu_len;
	dbg_max_amsdu_len = ACCESS_ONCE(mvm->max_amsdu_len);

	/* the Tx FIFO to which this A-MSDU will be routed */
	txf = iwl_mvm_ac_to_tx_fifo[tid_to_mac80211_ac[tid]];

	/*
	 * Don't send an AMSDU that will be longer than the TXF.
	 * Add a security margin of 256 for the TX command + headers.
	 * We also want to have the start of the next packet inside the
	 * fifo to be able to send bursts.
	 */
	max_amsdu_len = min_t(unsigned int, max_amsdu_len,
			      mvm->shared_mem_cfg.txfifo_size[txf] - 256);

	if (dbg_max_amsdu_len)
		max_amsdu_len = min_t(unsigned int, max_amsdu_len,
				      dbg_max_amsdu_len);

	/*
	 * Limit A-MSDU in A-MPDU to 4095 bytes when VHT is not
	 * supported. This is a spec requirement (IEEE 802.11-2015
	 * section 8.7.3 NOTE 3).
	 */
	if (info->flags & IEEE80211_TX_CTL_AMPDU &&
	    !sta->vht_cap.vht_supported)
		max_amsdu_len = min_t(unsigned int, max_amsdu_len, 4095);

	/* Sub frame header + SNAP + IP header + TCP header + MSS */
	subf_len = sizeof(struct ethhdr) + snap_ip_tcp + mss;
	pad = (4 - subf_len) & 0x3;

	/*
	 * If we have N subframes in the A-MSDU, then the A-MSDU's size is
	 * N * subf_len + (N - 1) * pad.
	 */
	num_subframes = (max_amsdu_len + pad) / (subf_len + pad);
	if (num_subframes > 1)
		*qc |= IEEE80211_QOS_CTL_A_MSDU_PRESENT;

	tcp_payload_len = skb_tail_pointer(skb) - skb_transport_header(skb) -
		tcp_hdrlen(skb) + skb->data_len;

	/*
	 * Make sure we have enough TBs for the A-MSDU:
	 *	2 for each subframe
	 *	1 more for each fragment
	 *	1 more for the potential data in the header
	 */
	num_subframes =
		min_t(unsigned int, num_subframes,
		      (mvm->trans->max_skb_frags - 1 -
		       skb_shinfo(skb)->nr_frags) / 2);

	/* This skb fits in one single A-MSDU */
	if (num_subframes * mss >= tcp_payload_len) {
		struct ieee80211_tx_info *skb_info = IEEE80211_SKB_CB(skb);

		/*
		 * Compute the length of all the data added for the A-MSDU.
		 * This will be used to compute the length to write in the TX
		 * command. We have: SNAP + IP + TCP for n -1 subframes and
		 * ETH header for n subframes. Note that the original skb
		 * already had one set of SNAP / IP / TCP headers.
		 */
		num_subframes = DIV_ROUND_UP(tcp_payload_len, mss);
		amsdu_add = num_subframes * sizeof(struct ethhdr) +
			(num_subframes - 1) * (snap_ip_tcp + pad);
		/* This holds the amsdu headers length */
		skb_info->driver_data[0] = (void *)(uintptr_t)amsdu_add;

		__skb_queue_tail(mpdus_skb, skb);
		return 0;
	}

	/*
	 * Trick the segmentation function to make it
	 * create SKBs that can fit into one A-MSDU.
	 */
segment:
	skb_shinfo(skb)->gso_size = num_subframes * mss;
	memcpy(cb, skb->cb, sizeof(cb));

	next = skb_gso_segment(skb, NETIF_F_CSUM_MASK | NETIF_F_SG);
	skb_shinfo(skb)->gso_size = mss;
	if (WARN_ON_ONCE(IS_ERR(next)))
		return -EINVAL;
	else if (next)
		consume_skb(skb);

	while (next) {
		tmp = next;
		next = tmp->next;

		memcpy(tmp->cb, cb, sizeof(tmp->cb));
		/*
		 * Compute the length of all the data added for the A-MSDU.
		 * This will be used to compute the length to write in the TX
		 * command. We have: SNAP + IP + TCP for n -1 subframes and
		 * ETH header for n subframes.
		 */
		tcp_payload_len = skb_tail_pointer(tmp) -
			skb_transport_header(tmp) -
			tcp_hdrlen(tmp) + tmp->data_len;

		if (ipv4)
			ip_hdr(tmp)->id = htons(ip_base_id + i * num_subframes);

		if (tcp_payload_len > mss) {
			struct ieee80211_tx_info *skb_info =
				IEEE80211_SKB_CB(tmp);

			num_subframes = DIV_ROUND_UP(tcp_payload_len, mss);
			amsdu_add = num_subframes * sizeof(struct ethhdr) +
				(num_subframes - 1) * (snap_ip_tcp + pad);
			skb_info->driver_data[0] =
				(void *)(uintptr_t)amsdu_add;
			skb_shinfo(tmp)->gso_size = mss;
		} else {
			qc = ieee80211_get_qos_ctl((void *)tmp->data);

			if (ipv4)
				ip_send_check(ip_hdr(tmp));
			*qc &= ~IEEE80211_QOS_CTL_A_MSDU_PRESENT;
			skb_shinfo(tmp)->gso_size = 0;
		}

		tmp->prev = NULL;
		tmp->next = NULL;

		__skb_queue_tail(mpdus_skb, tmp);
		i++;
	}

	return 0;
}
#else /* CONFIG_INET */
static int iwl_mvm_tx_tso(struct iwl_mvm *mvm, struct sk_buff *skb,
			  struct ieee80211_tx_info *info,
			  struct ieee80211_sta *sta,
			  struct sk_buff_head *mpdus_skb)
{
	/* Impossible to get TSO with CONFIG_INET */
	WARN_ON(1);

	return -1;
}
#endif

/*
 * Sets the fields in the Tx cmd that are crypto related
 */
static int iwl_mvm_tx_mpdu(struct iwl_mvm *mvm, struct sk_buff *skb,
			   struct ieee80211_tx_info *info,
			   struct ieee80211_sta *sta)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct iwl_mvm_sta *mvmsta;
	struct iwl_device_cmd *dev_cmd;
	struct iwl_tx_cmd *tx_cmd;
	__le16 fc;
	u16 seq_number = 0;
	u8 tid = IWL_MAX_TID_COUNT;
	u8 txq_id = info->hw_queue;
	bool is_data_qos = false, is_ampdu = false;
	int hdrlen;

	mvmsta = iwl_mvm_sta_from_mac80211(sta);
	fc = hdr->frame_control;
	hdrlen = ieee80211_hdrlen(fc);

	if (WARN_ON_ONCE(!mvmsta))
		return -1;

	if (WARN_ON_ONCE(mvmsta->sta_id == IWL_MVM_STATION_COUNT))
		return -1;

	dev_cmd = iwl_mvm_set_tx_params(mvm, skb, info, hdrlen,
					sta, mvmsta->sta_id);
	if (!dev_cmd)
		goto drop;

	tx_cmd = (struct iwl_tx_cmd *)dev_cmd->payload;
	/* From now on, we cannot access info->control */

	/*
	 * we handle that entirely ourselves -- for uAPSD the firmware
	 * will always send a notification, and for PS-Poll responses
	 * we'll notify mac80211 when getting frame status
	 */
	info->flags &= ~IEEE80211_TX_STATUS_EOSP;

	spin_lock(&mvmsta->lock);

	if (ieee80211_is_data_qos(fc) && !ieee80211_is_qos_nullfunc(fc)) {
		u8 *qc = NULL;
		qc = ieee80211_get_qos_ctl(hdr);
		tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;
		if (WARN_ON_ONCE(tid >= IWL_MAX_TID_COUNT))
			goto drop_unlock_sta;

		seq_number = mvmsta->tid_data[tid].seq_number;
		seq_number &= IEEE80211_SCTL_SEQ;
		hdr->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
		hdr->seq_ctrl |= cpu_to_le16(seq_number);
		is_data_qos = true;
		is_ampdu = info->flags & IEEE80211_TX_CTL_AMPDU;
	}

	/* Copy MAC header from skb into command buffer */
	memcpy(tx_cmd->hdr, hdr, hdrlen);

	WARN_ON_ONCE(info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM);

	if (sta->tdls) {
		/* default to TID 0 for non-QoS packets */
		u8 tdls_tid = tid == IWL_MAX_TID_COUNT ? 0 : tid;

		txq_id = mvmsta->hw_queue[tid_to_mac80211_ac[tdls_tid]];
	}

	if (is_ampdu) {
		if (WARN_ON_ONCE(mvmsta->tid_data[tid].state != IWL_AGG_ON))
			goto drop_unlock_sta;
		txq_id = mvmsta->tid_data[tid].txq_id;
	}

	IWL_DEBUG_TX(mvm, "TX to [%d|%d] Q:%d - seq: 0x%x\n", mvmsta->sta_id,
		     tid, txq_id, IEEE80211_SEQ_TO_SN(seq_number));

	if (iwl_trans_tx(mvm->trans, skb, dev_cmd, txq_id))
		goto drop_unlock_sta;

	if (is_data_qos && !ieee80211_has_morefrags(fc))
		mvmsta->tid_data[tid].seq_number = seq_number + 0x10;

	spin_unlock(&mvmsta->lock);

	if (txq_id < mvm->first_agg_queue)
		atomic_inc(&mvm->pending_frames[mvmsta->sta_id]);

	return 0;

drop_unlock_sta:
	iwl_trans_free_tx_cmd(mvm->trans, dev_cmd);
	spin_unlock(&mvmsta->lock);
drop:
	return -1;
}

int iwl_mvm_tx_skb(struct iwl_mvm *mvm, struct sk_buff *skb,
		   struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct ieee80211_tx_info *skb_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_info info;
	struct sk_buff_head mpdus_skbs;
	unsigned int payload_len;
	int ret;

	if (WARN_ON_ONCE(!mvmsta))
		return -1;

	if (WARN_ON_ONCE(mvmsta->sta_id == IWL_MVM_STATION_COUNT))
		return -1;

	memcpy(&info, skb->cb, sizeof(info));

	/* This holds the amsdu headers length */
	skb_info->driver_data[0] = (void *)(uintptr_t)0;

	if (!skb_is_gso(skb))
		return iwl_mvm_tx_mpdu(mvm, skb, &info, sta);

	payload_len = skb_tail_pointer(skb) - skb_transport_header(skb) -
		tcp_hdrlen(skb) + skb->data_len;

	if (payload_len <= skb_shinfo(skb)->gso_size)
		return iwl_mvm_tx_mpdu(mvm, skb, &info, sta);

	__skb_queue_head_init(&mpdus_skbs);

	ret = iwl_mvm_tx_tso(mvm, skb, &info, sta, &mpdus_skbs);
	if (ret)
		return ret;

	if (WARN_ON(skb_queue_empty(&mpdus_skbs)))
		return ret;

	while (!skb_queue_empty(&mpdus_skbs)) {
		skb = __skb_dequeue(&mpdus_skbs);

		ret = iwl_mvm_tx_mpdu(mvm, skb, &info, sta);
		if (ret) {
			__skb_queue_purge(&mpdus_skbs);
			return ret;
		}
	}

	return 0;
}

static void iwl_mvm_check_ratid_empty(struct iwl_mvm *mvm,
				      struct ieee80211_sta *sta, u8 tid)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_tid_data *tid_data = &mvmsta->tid_data[tid];
	struct ieee80211_vif *vif = mvmsta->vif;

	lockdep_assert_held(&mvmsta->lock);

	if ((tid_data->state == IWL_AGG_ON ||
	     tid_data->state == IWL_EMPTYING_HW_QUEUE_DELBA) &&
	    iwl_mvm_tid_queued(tid_data) == 0) {
		/*
		 * Now that this aggregation queue is empty tell mac80211 so it
		 * knows we no longer have frames buffered for the station on
		 * this TID (for the TIM bitmap calculation.)
		 */
		ieee80211_sta_set_buffered(sta, tid, false);
	}

	if (tid_data->ssn != tid_data->next_reclaimed)
		return;

	switch (tid_data->state) {
	case IWL_EMPTYING_HW_QUEUE_ADDBA:
		IWL_DEBUG_TX_QUEUES(mvm,
				    "Can continue addBA flow ssn = next_recl = %d\n",
				    tid_data->next_reclaimed);
		tid_data->state = IWL_AGG_STARTING;
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;

	case IWL_EMPTYING_HW_QUEUE_DELBA:
		IWL_DEBUG_TX_QUEUES(mvm,
				    "Can continue DELBA flow ssn = next_recl = %d\n",
				    tid_data->next_reclaimed);
		iwl_mvm_disable_txq(mvm, tid_data->txq_id,
				    vif->hw_queue[tid_to_mac80211_ac[tid]], tid,
				    CMD_ASYNC);
		tid_data->state = IWL_AGG_OFF;
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;

	default:
		break;
	}
}

#ifdef CONFIG_IWLWIFI_DEBUG
const char *iwl_mvm_get_tx_fail_reason(u32 status)
{
#define TX_STATUS_FAIL(x) case TX_STATUS_FAIL_ ## x: return #x
#define TX_STATUS_POSTPONE(x) case TX_STATUS_POSTPONE_ ## x: return #x

	switch (status & TX_STATUS_MSK) {
	case TX_STATUS_SUCCESS:
		return "SUCCESS";
	TX_STATUS_POSTPONE(DELAY);
	TX_STATUS_POSTPONE(FEW_BYTES);
	TX_STATUS_POSTPONE(BT_PRIO);
	TX_STATUS_POSTPONE(QUIET_PERIOD);
	TX_STATUS_POSTPONE(CALC_TTAK);
	TX_STATUS_FAIL(INTERNAL_CROSSED_RETRY);
	TX_STATUS_FAIL(SHORT_LIMIT);
	TX_STATUS_FAIL(LONG_LIMIT);
	TX_STATUS_FAIL(UNDERRUN);
	TX_STATUS_FAIL(DRAIN_FLOW);
	TX_STATUS_FAIL(RFKILL_FLUSH);
	TX_STATUS_FAIL(LIFE_EXPIRE);
	TX_STATUS_FAIL(DEST_PS);
	TX_STATUS_FAIL(HOST_ABORTED);
	TX_STATUS_FAIL(BT_RETRY);
	TX_STATUS_FAIL(STA_INVALID);
	TX_STATUS_FAIL(FRAG_DROPPED);
	TX_STATUS_FAIL(TID_DISABLE);
	TX_STATUS_FAIL(FIFO_FLUSHED);
	TX_STATUS_FAIL(SMALL_CF_POLL);
	TX_STATUS_FAIL(FW_DROP);
	TX_STATUS_FAIL(STA_COLOR_MISMATCH);
	}

	return "UNKNOWN";

#undef TX_STATUS_FAIL
#undef TX_STATUS_POSTPONE
}
#endif /* CONFIG_IWLWIFI_DEBUG */

void iwl_mvm_hwrate_to_tx_rate(u32 rate_n_flags,
			       enum ieee80211_band band,
			       struct ieee80211_tx_rate *r)
{
	if (rate_n_flags & RATE_HT_MCS_GF_MSK)
		r->flags |= IEEE80211_TX_RC_GREEN_FIELD;
	switch (rate_n_flags & RATE_MCS_CHAN_WIDTH_MSK) {
	case RATE_MCS_CHAN_WIDTH_20:
		break;
	case RATE_MCS_CHAN_WIDTH_40:
		r->flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
		break;
	case RATE_MCS_CHAN_WIDTH_80:
		r->flags |= IEEE80211_TX_RC_80_MHZ_WIDTH;
		break;
	case RATE_MCS_CHAN_WIDTH_160:
		r->flags |= IEEE80211_TX_RC_160_MHZ_WIDTH;
		break;
	}
	if (rate_n_flags & RATE_MCS_SGI_MSK)
		r->flags |= IEEE80211_TX_RC_SHORT_GI;
	if (rate_n_flags & RATE_MCS_HT_MSK) {
		r->flags |= IEEE80211_TX_RC_MCS;
		r->idx = rate_n_flags & RATE_HT_MCS_INDEX_MSK;
	} else if (rate_n_flags & RATE_MCS_VHT_MSK) {
		ieee80211_rate_set_vht(
			r, rate_n_flags & RATE_VHT_MCS_RATE_CODE_MSK,
			((rate_n_flags & RATE_VHT_MCS_NSS_MSK) >>
						RATE_VHT_MCS_NSS_POS) + 1);
		r->flags |= IEEE80211_TX_RC_VHT_MCS;
	} else {
		r->idx = iwl_mvm_legacy_rate_to_mac80211_idx(rate_n_flags,
							     band);
	}
}

/**
 * translate ucode response to mac80211 tx status control values
 */
static void iwl_mvm_hwrate_to_tx_status(u32 rate_n_flags,
					struct ieee80211_tx_info *info)
{
	struct ieee80211_tx_rate *r = &info->status.rates[0];

	info->status.antenna =
		((rate_n_flags & RATE_MCS_ANT_ABC_MSK) >> RATE_MCS_ANT_POS);
	iwl_mvm_hwrate_to_tx_rate(rate_n_flags, info->band, r);
}

static void iwl_mvm_tx_status_check_trigger(struct iwl_mvm *mvm,
					    u32 status)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_tx_status *status_trig;
	int i;

	if (!iwl_fw_dbg_trigger_enabled(mvm->fw, FW_DBG_TRIGGER_TX_STATUS))
		return;

	trig = iwl_fw_dbg_get_trigger(mvm->fw, FW_DBG_TRIGGER_TX_STATUS);
	status_trig = (void *)trig->data;

	if (!iwl_fw_dbg_trigger_check_stop(mvm, NULL, trig))
		return;

	for (i = 0; i < ARRAY_SIZE(status_trig->statuses); i++) {
		/* don't collect on status 0 */
		if (!status_trig->statuses[i].status)
			break;

		if (status_trig->statuses[i].status != (status & TX_STATUS_MSK))
			continue;

		iwl_mvm_fw_dbg_collect_trig(mvm, trig,
					    "Tx status %d was received",
					    status & TX_STATUS_MSK);
		break;
	}
}

static void iwl_mvm_rx_tx_cmd_single(struct iwl_mvm *mvm,
				     struct iwl_rx_packet *pkt)
{
	struct ieee80211_sta *sta;
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	int txq_id = SEQ_TO_QUEUE(sequence);
	struct iwl_mvm_tx_resp *tx_resp = (void *)pkt->data;
	int sta_id = IWL_MVM_TX_RES_GET_RA(tx_resp->ra_tid);
	int tid = IWL_MVM_TX_RES_GET_TID(tx_resp->ra_tid);
	u32 status = le16_to_cpu(tx_resp->status.status);
	u16 ssn = iwl_mvm_get_scd_ssn(tx_resp);
	struct iwl_mvm_sta *mvmsta;
	struct sk_buff_head skbs;
	u8 skb_freed = 0;
	u16 next_reclaimed, seq_ctl;
	bool is_ndp = false;

	__skb_queue_head_init(&skbs);

	seq_ctl = le16_to_cpu(tx_resp->seq_ctl);

	/* we can free until ssn % q.n_bd not inclusive */
	iwl_trans_reclaim(mvm->trans, txq_id, ssn, &skbs);

	while (!skb_queue_empty(&skbs)) {
		struct sk_buff *skb = __skb_dequeue(&skbs);
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

		skb_freed++;

		iwl_trans_free_tx_cmd(mvm->trans, info->driver_data[1]);

		memset(&info->status, 0, sizeof(info->status));

		info->flags &= ~IEEE80211_TX_CTL_AMPDU;

		/* inform mac80211 about what happened with the frame */
		switch (status & TX_STATUS_MSK) {
		case TX_STATUS_SUCCESS:
		case TX_STATUS_DIRECT_DONE:
			info->flags |= IEEE80211_TX_STAT_ACK;
			break;
		case TX_STATUS_FAIL_DEST_PS:
			info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
			break;
		default:
			break;
		}

		iwl_mvm_tx_status_check_trigger(mvm, status);

		info->status.rates[0].count = tx_resp->failure_frame + 1;
		iwl_mvm_hwrate_to_tx_status(le32_to_cpu(tx_resp->initial_rate),
					    info);
		info->status.status_driver_data[1] =
			(void *)(uintptr_t)le32_to_cpu(tx_resp->initial_rate);

		/* Single frame failure in an AMPDU queue => send BAR */
		if (txq_id >= mvm->first_agg_queue &&
		    !(info->flags & IEEE80211_TX_STAT_ACK) &&
		    !(info->flags & IEEE80211_TX_STAT_TX_FILTERED))
			info->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;

		/* W/A FW bug: seq_ctl is wrong when the status isn't success */
		if (status != TX_STATUS_SUCCESS) {
			struct ieee80211_hdr *hdr = (void *)skb->data;
			seq_ctl = le16_to_cpu(hdr->seq_ctrl);
		}

		if (unlikely(!seq_ctl)) {
			struct ieee80211_hdr *hdr = (void *)skb->data;

			/*
			 * If it is an NDP, we can't update next_reclaim since
			 * its sequence control is 0. Note that for that same
			 * reason, NDPs are never sent to A-MPDU'able queues
			 * so that we can never have more than one freed frame
			 * for a single Tx resonse (see WARN_ON below).
			 */
			if (ieee80211_is_qos_nullfunc(hdr->frame_control))
				is_ndp = true;
		}

		/*
		 * TODO: this is not accurate if we are freeing more than one
		 * packet.
		 */
		info->status.tx_time =
			le16_to_cpu(tx_resp->wireless_media_time);
		BUILD_BUG_ON(ARRAY_SIZE(info->status.status_driver_data) < 1);
		info->status.status_driver_data[0] =
				(void *)(uintptr_t)tx_resp->reduced_tpc;

		ieee80211_tx_status(mvm->hw, skb);
	}

	if (txq_id >= mvm->first_agg_queue) {
		/* If this is an aggregation queue, we use the ssn since:
		 * ssn = wifi seq_num % 256.
		 * The seq_ctl is the sequence control of the packet to which
		 * this Tx response relates. But if there is a hole in the
		 * bitmap of the BA we received, this Tx response may allow to
		 * reclaim the hole and all the subsequent packets that were
		 * already acked. In that case, seq_ctl != ssn, and the next
		 * packet to be reclaimed will be ssn and not seq_ctl. In that
		 * case, several packets will be reclaimed even if
		 * frame_count = 1.
		 *
		 * The ssn is the index (% 256) of the latest packet that has
		 * treated (acked / dropped) + 1.
		 */
		next_reclaimed = ssn;
	} else {
		/* The next packet to be reclaimed is the one after this one */
		next_reclaimed = IEEE80211_SEQ_TO_SN(seq_ctl + 0x10);
	}

	IWL_DEBUG_TX_REPLY(mvm,
			   "TXQ %d status %s (0x%08x)\n",
			   txq_id, iwl_mvm_get_tx_fail_reason(status), status);

	IWL_DEBUG_TX_REPLY(mvm,
			   "\t\t\t\tinitial_rate 0x%x retries %d, idx=%d ssn=%d next_reclaimed=0x%x seq_ctl=0x%x\n",
			   le32_to_cpu(tx_resp->initial_rate),
			   tx_resp->failure_frame, SEQ_TO_INDEX(sequence),
			   ssn, next_reclaimed, seq_ctl);

	rcu_read_lock();

	sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);
	/*
	 * sta can't be NULL otherwise it'd mean that the sta has been freed in
	 * the firmware while we still have packets for it in the Tx queues.
	 */
	if (WARN_ON_ONCE(!sta))
		goto out;

	if (!IS_ERR(sta)) {
		mvmsta = iwl_mvm_sta_from_mac80211(sta);

		if (tid != IWL_TID_NON_QOS) {
			struct iwl_mvm_tid_data *tid_data =
				&mvmsta->tid_data[tid];
			bool send_eosp_ndp = false;

			spin_lock_bh(&mvmsta->lock);
			if (!is_ndp) {
				tid_data->next_reclaimed = next_reclaimed;
				IWL_DEBUG_TX_REPLY(mvm,
						   "Next reclaimed packet:%d\n",
						   next_reclaimed);
			} else {
				IWL_DEBUG_TX_REPLY(mvm,
						   "NDP - don't update next_reclaimed\n");
			}

			iwl_mvm_check_ratid_empty(mvm, sta, tid);

			if (mvmsta->sleep_tx_count) {
				mvmsta->sleep_tx_count--;
				if (mvmsta->sleep_tx_count &&
				    !iwl_mvm_tid_queued(tid_data)) {
					/*
					 * The number of frames in the queue
					 * dropped to 0 even if we sent less
					 * frames than we thought we had on the
					 * Tx queue.
					 * This means we had holes in the BA
					 * window that we just filled, ask
					 * mac80211 to send EOSP since the
					 * firmware won't know how to do that.
					 * Send NDP and the firmware will send
					 * EOSP notification that will trigger
					 * a call to ieee80211_sta_eosp().
					 */
					send_eosp_ndp = true;
				}
			}

			spin_unlock_bh(&mvmsta->lock);
			if (send_eosp_ndp) {
				iwl_mvm_sta_modify_sleep_tx_count(mvm, sta,
					IEEE80211_FRAME_RELEASE_UAPSD,
					1, tid, false, false);
				mvmsta->sleep_tx_count = 0;
				ieee80211_send_eosp_nullfunc(sta, tid);
			}
		}

		if (mvmsta->next_status_eosp) {
			mvmsta->next_status_eosp = false;
			ieee80211_sta_eosp(sta);
		}
	} else {
		mvmsta = NULL;
	}

	/*
	 * If the txq is not an AMPDU queue, there is no chance we freed
	 * several skbs. Check that out...
	 */
	if (txq_id >= mvm->first_agg_queue)
		goto out;

	/* We can't free more than one frame at once on a shared queue */
	WARN_ON(skb_freed > 1);

	/* If we have still frames for this STA nothing to do here */
	if (!atomic_sub_and_test(skb_freed, &mvm->pending_frames[sta_id]))
		goto out;

	if (mvmsta && mvmsta->vif->type == NL80211_IFTYPE_AP) {

		/*
		 * If there are no pending frames for this STA and
		 * the tx to this station is not disabled, notify
		 * mac80211 that this station can now wake up in its
		 * STA table.
		 * If mvmsta is not NULL, sta is valid.
		 */

		spin_lock_bh(&mvmsta->lock);

		if (!mvmsta->disable_tx)
			ieee80211_sta_block_awake(mvm->hw, sta, false);

		spin_unlock_bh(&mvmsta->lock);
	}

	if (PTR_ERR(sta) == -EBUSY || PTR_ERR(sta) == -ENOENT) {
		/*
		 * We are draining and this was the last packet - pre_rcu_remove
		 * has been called already. We might be after the
		 * synchronize_net already.
		 * Don't rely on iwl_mvm_rm_sta to see the empty Tx queues.
		 */
		set_bit(sta_id, mvm->sta_drained);
		schedule_work(&mvm->sta_drained_wk);
	}

out:
	rcu_read_unlock();
}

#ifdef CONFIG_IWLWIFI_DEBUG
#define AGG_TX_STATE_(x) case AGG_TX_STATE_ ## x: return #x
static const char *iwl_get_agg_tx_status(u16 status)
{
	switch (status & AGG_TX_STATE_STATUS_MSK) {
	AGG_TX_STATE_(TRANSMITTED);
	AGG_TX_STATE_(UNDERRUN);
	AGG_TX_STATE_(BT_PRIO);
	AGG_TX_STATE_(FEW_BYTES);
	AGG_TX_STATE_(ABORT);
	AGG_TX_STATE_(LAST_SENT_TTL);
	AGG_TX_STATE_(LAST_SENT_TRY_CNT);
	AGG_TX_STATE_(LAST_SENT_BT_KILL);
	AGG_TX_STATE_(SCD_QUERY);
	AGG_TX_STATE_(TEST_BAD_CRC32);
	AGG_TX_STATE_(RESPONSE);
	AGG_TX_STATE_(DUMP_TX);
	AGG_TX_STATE_(DELAY_TX);
	}

	return "UNKNOWN";
}

static void iwl_mvm_rx_tx_cmd_agg_dbg(struct iwl_mvm *mvm,
				      struct iwl_rx_packet *pkt)
{
	struct iwl_mvm_tx_resp *tx_resp = (void *)pkt->data;
	struct agg_tx_status *frame_status = &tx_resp->status;
	int i;

	for (i = 0; i < tx_resp->frame_count; i++) {
		u16 fstatus = le16_to_cpu(frame_status[i].status);

		IWL_DEBUG_TX_REPLY(mvm,
				   "status %s (0x%04x), try-count (%d) seq (0x%x)\n",
				   iwl_get_agg_tx_status(fstatus),
				   fstatus & AGG_TX_STATE_STATUS_MSK,
				   (fstatus & AGG_TX_STATE_TRY_CNT_MSK) >>
					AGG_TX_STATE_TRY_CNT_POS,
				   le16_to_cpu(frame_status[i].sequence));
	}
}
#else
static void iwl_mvm_rx_tx_cmd_agg_dbg(struct iwl_mvm *mvm,
				      struct iwl_rx_packet *pkt)
{}
#endif /* CONFIG_IWLWIFI_DEBUG */

static void iwl_mvm_rx_tx_cmd_agg(struct iwl_mvm *mvm,
				  struct iwl_rx_packet *pkt)
{
	struct iwl_mvm_tx_resp *tx_resp = (void *)pkt->data;
	int sta_id = IWL_MVM_TX_RES_GET_RA(tx_resp->ra_tid);
	int tid = IWL_MVM_TX_RES_GET_TID(tx_resp->ra_tid);
	u16 sequence = le16_to_cpu(pkt->hdr.sequence);
	struct ieee80211_sta *sta;

	if (WARN_ON_ONCE(SEQ_TO_QUEUE(sequence) < mvm->first_agg_queue))
		return;

	if (WARN_ON_ONCE(tid == IWL_TID_NON_QOS))
		return;

	iwl_mvm_rx_tx_cmd_agg_dbg(mvm, pkt);

	rcu_read_lock();

	sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);

	if (!WARN_ON_ONCE(IS_ERR_OR_NULL(sta))) {
		struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
		mvmsta->tid_data[tid].rate_n_flags =
			le32_to_cpu(tx_resp->initial_rate);
		mvmsta->tid_data[tid].tx_time =
			le16_to_cpu(tx_resp->wireless_media_time);
	}

	rcu_read_unlock();
}

void iwl_mvm_rx_tx_cmd(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mvm_tx_resp *tx_resp = (void *)pkt->data;

	if (tx_resp->frame_count == 1)
		iwl_mvm_rx_tx_cmd_single(mvm, pkt);
	else
		iwl_mvm_rx_tx_cmd_agg(mvm, pkt);
}

static void iwl_mvm_tx_info_from_ba_notif(struct ieee80211_tx_info *info,
					  struct iwl_mvm_ba_notif *ba_notif,
					  struct iwl_mvm_tid_data *tid_data)
{
	info->flags |= IEEE80211_TX_STAT_AMPDU;
	info->status.ampdu_ack_len = ba_notif->txed_2_done;
	info->status.ampdu_len = ba_notif->txed;
	iwl_mvm_hwrate_to_tx_status(tid_data->rate_n_flags,
				    info);
	/* TODO: not accounted if the whole A-MPDU failed */
	info->status.tx_time = tid_data->tx_time;
	info->status.status_driver_data[0] =
		(void *)(uintptr_t)ba_notif->reduced_txp;
	info->status.status_driver_data[1] =
		(void *)(uintptr_t)tid_data->rate_n_flags;
}

void iwl_mvm_rx_ba_notif(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mvm_ba_notif *ba_notif = (void *)pkt->data;
	struct sk_buff_head reclaimed_skbs;
	struct iwl_mvm_tid_data *tid_data;
	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvmsta;
	struct sk_buff *skb;
	int sta_id, tid, freed;
	/* "flow" corresponds to Tx queue */
	u16 scd_flow = le16_to_cpu(ba_notif->scd_flow);
	/* "ssn" is start of block-ack Tx window, corresponds to index
	 * (in Tx queue's circular buffer) of first TFD/frame in window */
	u16 ba_resp_scd_ssn = le16_to_cpu(ba_notif->scd_ssn);

	sta_id = ba_notif->sta_id;
	tid = ba_notif->tid;

	if (WARN_ONCE(sta_id >= IWL_MVM_STATION_COUNT ||
		      tid >= IWL_MAX_TID_COUNT,
		      "sta_id %d tid %d", sta_id, tid))
		return;

	rcu_read_lock();

	sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_id]);

	/* Reclaiming frames for a station that has been deleted ? */
	if (WARN_ON_ONCE(IS_ERR_OR_NULL(sta))) {
		rcu_read_unlock();
		return;
	}

	mvmsta = iwl_mvm_sta_from_mac80211(sta);
	tid_data = &mvmsta->tid_data[tid];

	if (tid_data->txq_id != scd_flow) {
		IWL_ERR(mvm,
			"invalid BA notification: Q %d, tid %d, flow %d\n",
			tid_data->txq_id, tid, scd_flow);
		rcu_read_unlock();
		return;
	}

	spin_lock_bh(&mvmsta->lock);

	__skb_queue_head_init(&reclaimed_skbs);

	/*
	 * Release all TFDs before the SSN, i.e. all TFDs in front of
	 * block-ack window (we assume that they've been successfully
	 * transmitted ... if not, it's too late anyway).
	 */
	iwl_trans_reclaim(mvm->trans, scd_flow, ba_resp_scd_ssn,
			  &reclaimed_skbs);

	IWL_DEBUG_TX_REPLY(mvm,
			   "BA_NOTIFICATION Received from %pM, sta_id = %d\n",
			   (u8 *)&ba_notif->sta_addr_lo32,
			   ba_notif->sta_id);
	IWL_DEBUG_TX_REPLY(mvm,
			   "TID = %d, SeqCtl = %d, bitmap = 0x%llx, scd_flow = %d, scd_ssn = %d sent:%d, acked:%d\n",
			   ba_notif->tid, le16_to_cpu(ba_notif->seq_ctl),
			   (unsigned long long)le64_to_cpu(ba_notif->bitmap),
			   scd_flow, ba_resp_scd_ssn, ba_notif->txed,
			   ba_notif->txed_2_done);

	IWL_DEBUG_TX_REPLY(mvm, "reduced txp from ba notif %d\n",
			   ba_notif->reduced_txp);
	tid_data->next_reclaimed = ba_resp_scd_ssn;

	iwl_mvm_check_ratid_empty(mvm, sta, tid);

	freed = 0;

	skb_queue_walk(&reclaimed_skbs, skb) {
		struct ieee80211_hdr *hdr = (void *)skb->data;
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

		if (ieee80211_is_data_qos(hdr->frame_control))
			freed++;
		else
			WARN_ON_ONCE(1);

		iwl_trans_free_tx_cmd(mvm->trans, info->driver_data[1]);

		memset(&info->status, 0, sizeof(info->status));
		/* Packet was transmitted successfully, failures come as single
		 * frames because before failing a frame the firmware transmits
		 * it without aggregation at least once.
		 */
		info->flags |= IEEE80211_TX_STAT_ACK;

		/* this is the first skb we deliver in this batch */
		/* put the rate scaling data there */
		if (freed == 1)
			iwl_mvm_tx_info_from_ba_notif(info, ba_notif, tid_data);
	}

	spin_unlock_bh(&mvmsta->lock);

	/* We got a BA notif with 0 acked or scd_ssn didn't progress which is
	 * possible (i.e. first MPDU in the aggregation wasn't acked)
	 * Still it's important to update RS about sent vs. acked.
	 */
	if (skb_queue_empty(&reclaimed_skbs)) {
		struct ieee80211_tx_info ba_info = {};
		struct ieee80211_chanctx_conf *chanctx_conf = NULL;

		if (mvmsta->vif)
			chanctx_conf =
				rcu_dereference(mvmsta->vif->chanctx_conf);

		if (WARN_ON_ONCE(!chanctx_conf))
			goto out;

		ba_info.band = chanctx_conf->def.chan->band;
		iwl_mvm_tx_info_from_ba_notif(&ba_info, ba_notif, tid_data);

		IWL_DEBUG_TX_REPLY(mvm, "No reclaim. Update rs directly\n");
		iwl_mvm_rs_tx_status(mvm, sta, tid, &ba_info);
	}

out:
	rcu_read_unlock();

	while (!skb_queue_empty(&reclaimed_skbs)) {
		skb = __skb_dequeue(&reclaimed_skbs);
		ieee80211_tx_status(mvm->hw, skb);
	}
}

/*
 * Note that there are transports that buffer frames before they reach
 * the firmware. This means that after flush_tx_path is called, the
 * queue might not be empty. The race-free way to handle this is to:
 * 1) set the station as draining
 * 2) flush the Tx path
 * 3) wait for the transport queues to be empty
 */
int iwl_mvm_flush_tx_path(struct iwl_mvm *mvm, u32 tfd_msk, u32 flags)
{
	int ret;
	struct iwl_tx_path_flush_cmd flush_cmd = {
		.queues_ctl = cpu_to_le32(tfd_msk),
		.flush_ctl = cpu_to_le16(DUMP_TX_FIFO_FLUSH),
	};

	ret = iwl_mvm_send_cmd_pdu(mvm, TXPATH_FLUSH, flags,
				   sizeof(flush_cmd), &flush_cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send flush command (%d)\n", ret);
	return ret;
}
