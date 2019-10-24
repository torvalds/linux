/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
 * Copyright (C) 2019 Intel Corporation
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
 * Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
 * Copyright (C) 2019 Intel Corporation
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
#include <linux/etherdevice.h>
#include <linux/math64.h>
#include <net/cfg80211.h>
#include "mvm.h"
#include "iwl-io.h"
#include "iwl-prph.h"
#include "constants.h"

struct iwl_mvm_loc_entry {
	struct list_head list;
	u8 addr[ETH_ALEN];
	u8 lci_len, civic_len;
	u8 buf[];
};

static void iwl_mvm_ftm_reset(struct iwl_mvm *mvm)
{
	struct iwl_mvm_loc_entry *e, *t;

	mvm->ftm_initiator.req = NULL;
	mvm->ftm_initiator.req_wdev = NULL;
	memset(mvm->ftm_initiator.responses, 0,
	       sizeof(mvm->ftm_initiator.responses));
	list_for_each_entry_safe(e, t, &mvm->ftm_initiator.loc_list, list) {
		list_del(&e->list);
		kfree(e);
	}
}

void iwl_mvm_ftm_restart(struct iwl_mvm *mvm)
{
	struct cfg80211_pmsr_result result = {
		.status = NL80211_PMSR_STATUS_FAILURE,
		.final = 1,
		.host_time = ktime_get_boottime_ns(),
		.type = NL80211_PMSR_TYPE_FTM,
	};
	int i;

	lockdep_assert_held(&mvm->mutex);

	if (!mvm->ftm_initiator.req)
		return;

	for (i = 0; i < mvm->ftm_initiator.req->n_peers; i++) {
		memcpy(result.addr, mvm->ftm_initiator.req->peers[i].addr,
		       ETH_ALEN);
		result.ftm.burst_index = mvm->ftm_initiator.responses[i];

		cfg80211_pmsr_report(mvm->ftm_initiator.req_wdev,
				     mvm->ftm_initiator.req,
				     &result, GFP_KERNEL);
	}

	cfg80211_pmsr_complete(mvm->ftm_initiator.req_wdev,
			       mvm->ftm_initiator.req, GFP_KERNEL);
	iwl_mvm_ftm_reset(mvm);
}

static int
iwl_ftm_range_request_status_to_err(enum iwl_tof_range_request_status s)
{
	switch (s) {
	case IWL_TOF_RANGE_REQUEST_STATUS_SUCCESS:
		return 0;
	case IWL_TOF_RANGE_REQUEST_STATUS_BUSY:
		return -EBUSY;
	default:
		WARN_ON_ONCE(1);
		return -EIO;
	}
}

static void iwl_mvm_ftm_cmd_v5(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			       struct iwl_tof_range_req_cmd_v5 *cmd,
			       struct cfg80211_pmsr_request *req)
{
	int i;

	cmd->request_id = req->cookie;
	cmd->num_of_ap = req->n_peers;

	/* use maximum for "no timeout" or bigger than what we can do */
	if (!req->timeout || req->timeout > 255 * 100)
		cmd->req_timeout = 255;
	else
		cmd->req_timeout = DIV_ROUND_UP(req->timeout, 100);

	/*
	 * We treat it always as random, since if not we'll
	 * have filled our local address there instead.
	 */
	cmd->macaddr_random = 1;
	memcpy(cmd->macaddr_template, req->mac_addr, ETH_ALEN);
	for (i = 0; i < ETH_ALEN; i++)
		cmd->macaddr_mask[i] = ~req->mac_addr_mask[i];

	if (vif->bss_conf.assoc)
		memcpy(cmd->range_req_bssid, vif->bss_conf.bssid, ETH_ALEN);
	else
		eth_broadcast_addr(cmd->range_req_bssid);
}

static void iwl_mvm_ftm_cmd(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			    struct iwl_tof_range_req_cmd *cmd,
			    struct cfg80211_pmsr_request *req)
{
	int i;

	cmd->initiator_flags =
		cpu_to_le32(IWL_TOF_INITIATOR_FLAGS_MACADDR_RANDOM |
			    IWL_TOF_INITIATOR_FLAGS_NON_ASAP_SUPPORT);
	cmd->request_id = req->cookie;
	cmd->num_of_ap = req->n_peers;

	/*
	 * Use a large value for "no timeout". Don't use the maximum value
	 * because of fw limitations.
	 */
	if (req->timeout)
		cmd->req_timeout_ms = cpu_to_le32(req->timeout);
	else
		cmd->req_timeout_ms = cpu_to_le32(0xfffff);

	memcpy(cmd->macaddr_template, req->mac_addr, ETH_ALEN);
	for (i = 0; i < ETH_ALEN; i++)
		cmd->macaddr_mask[i] = ~req->mac_addr_mask[i];

	if (vif->bss_conf.assoc) {
		memcpy(cmd->range_req_bssid, vif->bss_conf.bssid, ETH_ALEN);

		/* AP's TSF is only relevant if associated */
		for (i = 0; i < req->n_peers; i++) {
			if (req->peers[i].report_ap_tsf) {
				struct iwl_mvm_vif *mvmvif =
					iwl_mvm_vif_from_mac80211(vif);

				cmd->tsf_mac_id = cpu_to_le32(mvmvif->id);
				return;
			}
		}
	} else {
		eth_broadcast_addr(cmd->range_req_bssid);
	}

	/* Don't report AP's TSF */
	cmd->tsf_mac_id = cpu_to_le32(0xff);
}

static int iwl_mvm_ftm_target_chandef(struct iwl_mvm *mvm,
				      struct cfg80211_pmsr_request_peer *peer,
				      u8 *channel, u8 *bandwidth,
				      u8 *ctrl_ch_position)
{
	u32 freq = peer->chandef.chan->center_freq;

	*channel = ieee80211_frequency_to_channel(freq);

	switch (peer->chandef.width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
		*bandwidth = IWL_TOF_BW_20_LEGACY;
		break;
	case NL80211_CHAN_WIDTH_20:
		*bandwidth = IWL_TOF_BW_20_HT;
		break;
	case NL80211_CHAN_WIDTH_40:
		*bandwidth = IWL_TOF_BW_40;
		break;
	case NL80211_CHAN_WIDTH_80:
		*bandwidth = IWL_TOF_BW_80;
		break;
	default:
		IWL_ERR(mvm, "Unsupported BW in FTM request (%d)\n",
			peer->chandef.width);
		return -EINVAL;
	}

	*ctrl_ch_position = (peer->chandef.width > NL80211_CHAN_WIDTH_20) ?
		iwl_mvm_get_ctrl_pos(&peer->chandef) : 0;

	return 0;
}

static int
iwl_mvm_ftm_put_target_v2(struct iwl_mvm *mvm,
			  struct cfg80211_pmsr_request_peer *peer,
			  struct iwl_tof_range_req_ap_entry_v2 *target)
{
	int ret;

	ret = iwl_mvm_ftm_target_chandef(mvm, peer, &target->channel_num,
					 &target->bandwidth,
					 &target->ctrl_ch_position);
	if (ret)
		return ret;

	memcpy(target->bssid, peer->addr, ETH_ALEN);
	target->burst_period =
		cpu_to_le16(peer->ftm.burst_period);
	target->samples_per_burst = peer->ftm.ftms_per_burst;
	target->num_of_bursts = peer->ftm.num_bursts_exp;
	target->measure_type = 0; /* regular two-sided FTM */
	target->retries_per_sample = peer->ftm.ftmr_retries;
	target->asap_mode = peer->ftm.asap;
	target->enable_dyn_ack = IWL_MVM_FTM_INITIATOR_DYNACK;

	if (peer->ftm.request_lci)
		target->location_req |= IWL_TOF_LOC_LCI;
	if (peer->ftm.request_civicloc)
		target->location_req |= IWL_TOF_LOC_CIVIC;

	target->algo_type = IWL_MVM_FTM_INITIATOR_ALGO;

	return 0;
}

#define FTM_PUT_FLAG(flag)	(target->initiator_ap_flags |= \
				 cpu_to_le32(IWL_INITIATOR_AP_FLAGS_##flag))

static int iwl_mvm_ftm_put_target(struct iwl_mvm *mvm,
				  struct cfg80211_pmsr_request_peer *peer,
				  struct iwl_tof_range_req_ap_entry *target)
{
	int ret;

	ret = iwl_mvm_ftm_target_chandef(mvm, peer, &target->channel_num,
					 &target->bandwidth,
					 &target->ctrl_ch_position);
	if (ret)
		return ret;

	memcpy(target->bssid, peer->addr, ETH_ALEN);
	target->burst_period =
		cpu_to_le16(peer->ftm.burst_period);
	target->samples_per_burst = peer->ftm.ftms_per_burst;
	target->num_of_bursts = peer->ftm.num_bursts_exp;
	target->ftmr_max_retries = peer->ftm.ftmr_retries;
	target->initiator_ap_flags = cpu_to_le32(0);

	if (peer->ftm.asap)
		FTM_PUT_FLAG(ASAP);

	if (peer->ftm.request_lci)
		FTM_PUT_FLAG(LCI_REQUEST);

	if (peer->ftm.request_civicloc)
		FTM_PUT_FLAG(CIVIC_REQUEST);

	if (IWL_MVM_FTM_INITIATOR_DYNACK)
		FTM_PUT_FLAG(DYN_ACK);

	if (IWL_MVM_FTM_INITIATOR_ALGO == IWL_TOF_ALGO_TYPE_LINEAR_REG)
		FTM_PUT_FLAG(ALGO_LR);
	else if (IWL_MVM_FTM_INITIATOR_ALGO == IWL_TOF_ALGO_TYPE_FFT)
		FTM_PUT_FLAG(ALGO_FFT);

	return 0;
}

int iwl_mvm_ftm_start(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		      struct cfg80211_pmsr_request *req)
{
	struct iwl_tof_range_req_cmd_v5 cmd_v5;
	struct iwl_tof_range_req_cmd cmd;
	bool new_api = fw_has_api(&mvm->fw->ucode_capa,
				  IWL_UCODE_TLV_API_FTM_NEW_RANGE_REQ);
	u8 num_of_ap;
	struct iwl_host_cmd hcmd = {
		.id = iwl_cmd_id(TOF_RANGE_REQ_CMD, LOCATION_GROUP, 0),
		.dataflags[0] = IWL_HCMD_DFL_DUP,
	};
	u32 status = 0;
	int err, i;

	lockdep_assert_held(&mvm->mutex);

	if (mvm->ftm_initiator.req)
		return -EBUSY;

	if (new_api) {
		iwl_mvm_ftm_cmd(mvm, vif, &cmd, req);
		hcmd.data[0] = &cmd;
		hcmd.len[0] = sizeof(cmd);
		num_of_ap = cmd.num_of_ap;
	} else {
		iwl_mvm_ftm_cmd_v5(mvm, vif, &cmd_v5, req);
		hcmd.data[0] = &cmd_v5;
		hcmd.len[0] = sizeof(cmd_v5);
		num_of_ap = cmd_v5.num_of_ap;
	}

	for (i = 0; i < num_of_ap; i++) {
		struct cfg80211_pmsr_request_peer *peer = &req->peers[i];

		if (new_api)
			err = iwl_mvm_ftm_put_target(mvm, peer, &cmd.ap[i]);
		else
			err = iwl_mvm_ftm_put_target_v2(mvm, peer,
							&cmd_v5.ap[i]);

		if (err)
			return err;
	}

	err = iwl_mvm_send_cmd_status(mvm, &hcmd, &status);
	if (!err && status) {
		IWL_ERR(mvm, "FTM range request command failure, status: %u\n",
			status);
		err = iwl_ftm_range_request_status_to_err(status);
	}

	if (!err) {
		mvm->ftm_initiator.req = req;
		mvm->ftm_initiator.req_wdev = ieee80211_vif_to_wdev(vif);
	}

	return err;
}

void iwl_mvm_ftm_abort(struct iwl_mvm *mvm, struct cfg80211_pmsr_request *req)
{
	struct iwl_tof_range_abort_cmd cmd = {
		.request_id = req->cookie,
	};

	lockdep_assert_held(&mvm->mutex);

	if (req != mvm->ftm_initiator.req)
		return;

	if (iwl_mvm_send_cmd_pdu(mvm, iwl_cmd_id(TOF_RANGE_ABORT_CMD,
						 LOCATION_GROUP, 0),
				 0, sizeof(cmd), &cmd))
		IWL_ERR(mvm, "failed to abort FTM process\n");
}

static int iwl_mvm_ftm_find_peer(struct cfg80211_pmsr_request *req,
				 const u8 *addr)
{
	int i;

	for (i = 0; i < req->n_peers; i++) {
		struct cfg80211_pmsr_request_peer *peer = &req->peers[i];

		if (ether_addr_equal_unaligned(peer->addr, addr))
			return i;
	}

	return -ENOENT;
}

static u64 iwl_mvm_ftm_get_host_time(struct iwl_mvm *mvm, __le32 fw_gp2_ts)
{
	u32 gp2_ts = le32_to_cpu(fw_gp2_ts);
	u32 curr_gp2, diff;
	u64 now_from_boot_ns;

	iwl_mvm_get_sync_time(mvm, &curr_gp2, &now_from_boot_ns);

	if (curr_gp2 >= gp2_ts)
		diff = curr_gp2 - gp2_ts;
	else
		diff = curr_gp2 + (U32_MAX - gp2_ts + 1);

	return now_from_boot_ns - (u64)diff * 1000;
}

static void iwl_mvm_ftm_get_lci_civic(struct iwl_mvm *mvm,
				      struct cfg80211_pmsr_result *res)
{
	struct iwl_mvm_loc_entry *entry;

	list_for_each_entry(entry, &mvm->ftm_initiator.loc_list, list) {
		if (!ether_addr_equal_unaligned(res->addr, entry->addr))
			continue;

		if (entry->lci_len) {
			res->ftm.lci_len = entry->lci_len;
			res->ftm.lci = entry->buf;
		}

		if (entry->civic_len) {
			res->ftm.civicloc_len = entry->civic_len;
			res->ftm.civicloc = entry->buf + entry->lci_len;
		}

		/* we found the entry we needed */
		break;
	}
}

static int iwl_mvm_ftm_range_resp_valid(struct iwl_mvm *mvm, u8 request_id,
					u8 num_of_aps)
{
	lockdep_assert_held(&mvm->mutex);

	if (request_id != (u8)mvm->ftm_initiator.req->cookie) {
		IWL_ERR(mvm, "Request ID mismatch, got %u, active %u\n",
			request_id, (u8)mvm->ftm_initiator.req->cookie);
		return -EINVAL;
	}

	if (num_of_aps > mvm->ftm_initiator.req->n_peers) {
		IWL_ERR(mvm, "FTM range response invalid\n");
		return -EINVAL;
	}

	return 0;
}

static void iwl_mvm_debug_range_resp(struct iwl_mvm *mvm, u8 index,
				     struct cfg80211_pmsr_result *res)
{
	s64 rtt_avg = div_s64(res->ftm.rtt_avg * 100, 6666);

	IWL_DEBUG_INFO(mvm, "entry %d\n", index);
	IWL_DEBUG_INFO(mvm, "\tstatus: %d\n", res->status);
	IWL_DEBUG_INFO(mvm, "\tBSSID: %pM\n", res->addr);
	IWL_DEBUG_INFO(mvm, "\thost time: %llu\n", res->host_time);
	IWL_DEBUG_INFO(mvm, "\tburst index: %hhu\n", res->ftm.burst_index);
	IWL_DEBUG_INFO(mvm, "\tsuccess num: %u\n", res->ftm.num_ftmr_successes);
	IWL_DEBUG_INFO(mvm, "\trssi: %d\n", res->ftm.rssi_avg);
	IWL_DEBUG_INFO(mvm, "\trssi spread: %hhu\n", res->ftm.rssi_spread);
	IWL_DEBUG_INFO(mvm, "\trtt: %lld\n", res->ftm.rtt_avg);
	IWL_DEBUG_INFO(mvm, "\trtt var: %llu\n", res->ftm.rtt_variance);
	IWL_DEBUG_INFO(mvm, "\trtt spread: %llu\n", res->ftm.rtt_spread);
	IWL_DEBUG_INFO(mvm, "\tdistance: %lld\n", rtt_avg);
}

void iwl_mvm_ftm_range_resp(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_tof_range_rsp_ntfy_v5 *fw_resp_v5 = (void *)pkt->data;
	struct iwl_tof_range_rsp_ntfy_v6 *fw_resp_v6 = (void *)pkt->data;
	struct iwl_tof_range_rsp_ntfy *fw_resp = (void *)pkt->data;
	int i;
	bool new_api = fw_has_api(&mvm->fw->ucode_capa,
				  IWL_UCODE_TLV_API_FTM_NEW_RANGE_REQ);
	u8 num_of_aps, last_in_batch;

	lockdep_assert_held(&mvm->mutex);

	if (!mvm->ftm_initiator.req) {
		IWL_ERR(mvm, "Got FTM response but have no request?\n");
		return;
	}

	if (new_api) {
		if (iwl_mvm_ftm_range_resp_valid(mvm, fw_resp->request_id,
						 fw_resp->num_of_aps))
			return;

		num_of_aps = fw_resp->num_of_aps;
		last_in_batch = fw_resp->last_report;
	} else {
		if (iwl_mvm_ftm_range_resp_valid(mvm, fw_resp_v5->request_id,
						 fw_resp_v5->num_of_aps))
			return;

		num_of_aps = fw_resp_v5->num_of_aps;
		last_in_batch = fw_resp_v5->last_in_batch;
	}

	IWL_DEBUG_INFO(mvm, "Range response received\n");
	IWL_DEBUG_INFO(mvm, "request id: %lld, num of entries: %hhu\n",
		       mvm->ftm_initiator.req->cookie, num_of_aps);

	for (i = 0; i < num_of_aps && i < IWL_MVM_TOF_MAX_APS; i++) {
		struct cfg80211_pmsr_result result = {};
		struct iwl_tof_range_rsp_ap_entry_ntfy *fw_ap;
		int peer_idx;

		if (new_api) {
			if (fw_has_api(&mvm->fw->ucode_capa,
				       IWL_UCODE_TLV_API_FTM_RTT_ACCURACY))
				fw_ap = &fw_resp->ap[i];
			else
				fw_ap = (void *)&fw_resp_v6->ap[i];

			result.final = fw_resp->ap[i].last_burst;
			result.ap_tsf = le32_to_cpu(fw_ap->start_tsf);
			result.ap_tsf_valid = 1;
		} else {
			/* the first part is the same for old and new APIs */
			fw_ap = (void *)&fw_resp_v5->ap[i];
			/*
			 * FIXME: the firmware needs to report this, we don't
			 * even know the number of bursts the responder picked
			 * (if we asked it to)
			 */
			result.final = 0;
		}

		peer_idx = iwl_mvm_ftm_find_peer(mvm->ftm_initiator.req,
						 fw_ap->bssid);
		if (peer_idx < 0) {
			IWL_WARN(mvm,
				 "Unknown address (%pM, target #%d) in FTM response\n",
				 fw_ap->bssid, i);
			continue;
		}

		switch (fw_ap->measure_status) {
		case IWL_TOF_ENTRY_SUCCESS:
			result.status = NL80211_PMSR_STATUS_SUCCESS;
			break;
		case IWL_TOF_ENTRY_TIMING_MEASURE_TIMEOUT:
			result.status = NL80211_PMSR_STATUS_TIMEOUT;
			break;
		case IWL_TOF_ENTRY_NO_RESPONSE:
			result.status = NL80211_PMSR_STATUS_FAILURE;
			result.ftm.failure_reason =
				NL80211_PMSR_FTM_FAILURE_NO_RESPONSE;
			break;
		case IWL_TOF_ENTRY_REQUEST_REJECTED:
			result.status = NL80211_PMSR_STATUS_FAILURE;
			result.ftm.failure_reason =
				NL80211_PMSR_FTM_FAILURE_PEER_BUSY;
			result.ftm.busy_retry_time = fw_ap->refusal_period;
			break;
		default:
			result.status = NL80211_PMSR_STATUS_FAILURE;
			result.ftm.failure_reason =
				NL80211_PMSR_FTM_FAILURE_UNSPECIFIED;
			break;
		}
		memcpy(result.addr, fw_ap->bssid, ETH_ALEN);
		result.host_time = iwl_mvm_ftm_get_host_time(mvm,
							     fw_ap->timestamp);
		result.type = NL80211_PMSR_TYPE_FTM;
		result.ftm.burst_index = mvm->ftm_initiator.responses[peer_idx];
		mvm->ftm_initiator.responses[peer_idx]++;
		result.ftm.rssi_avg = fw_ap->rssi;
		result.ftm.rssi_avg_valid = 1;
		result.ftm.rssi_spread = fw_ap->rssi_spread;
		result.ftm.rssi_spread_valid = 1;
		result.ftm.rtt_avg = (s32)le32_to_cpu(fw_ap->rtt);
		result.ftm.rtt_avg_valid = 1;
		result.ftm.rtt_variance = le32_to_cpu(fw_ap->rtt_variance);
		result.ftm.rtt_variance_valid = 1;
		result.ftm.rtt_spread = le32_to_cpu(fw_ap->rtt_spread);
		result.ftm.rtt_spread_valid = 1;

		iwl_mvm_ftm_get_lci_civic(mvm, &result);

		cfg80211_pmsr_report(mvm->ftm_initiator.req_wdev,
				     mvm->ftm_initiator.req,
				     &result, GFP_KERNEL);

		if (fw_has_api(&mvm->fw->ucode_capa,
			       IWL_UCODE_TLV_API_FTM_RTT_ACCURACY))
			IWL_DEBUG_INFO(mvm, "RTT confidence: %hhu\n",
				       fw_ap->rttConfidence);

		iwl_mvm_debug_range_resp(mvm, i, &result);
	}

	if (last_in_batch) {
		cfg80211_pmsr_complete(mvm->ftm_initiator.req_wdev,
				       mvm->ftm_initiator.req,
				       GFP_KERNEL);
		iwl_mvm_ftm_reset(mvm);
	}
}

void iwl_mvm_ftm_lc_notif(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	const struct ieee80211_mgmt *mgmt = (void *)pkt->data;
	size_t len = iwl_rx_packet_payload_len(pkt);
	struct iwl_mvm_loc_entry *entry;
	const u8 *ies, *lci, *civic, *msr_ie;
	size_t ies_len, lci_len = 0, civic_len = 0;
	size_t baselen = IEEE80211_MIN_ACTION_SIZE +
			 sizeof(mgmt->u.action.u.ftm);
	static const u8 rprt_type_lci = IEEE80211_SPCT_MSR_RPRT_TYPE_LCI;
	static const u8 rprt_type_civic = IEEE80211_SPCT_MSR_RPRT_TYPE_CIVIC;

	if (len <= baselen)
		return;

	lockdep_assert_held(&mvm->mutex);

	ies = mgmt->u.action.u.ftm.variable;
	ies_len = len - baselen;

	msr_ie = cfg80211_find_ie_match(WLAN_EID_MEASURE_REPORT, ies, ies_len,
					&rprt_type_lci, 1, 4);
	if (msr_ie) {
		lci = msr_ie + 2;
		lci_len = msr_ie[1];
	}

	msr_ie = cfg80211_find_ie_match(WLAN_EID_MEASURE_REPORT, ies, ies_len,
					&rprt_type_civic, 1, 4);
	if (msr_ie) {
		civic = msr_ie + 2;
		civic_len = msr_ie[1];
	}

	entry = kmalloc(sizeof(*entry) + lci_len + civic_len, GFP_KERNEL);
	if (!entry)
		return;

	memcpy(entry->addr, mgmt->bssid, ETH_ALEN);

	entry->lci_len = lci_len;
	if (lci_len)
		memcpy(entry->buf, lci, lci_len);

	entry->civic_len = civic_len;
	if (civic_len)
		memcpy(entry->buf + lci_len, civic, civic_len);

	list_add_tail(&entry->list, &mvm->ftm_initiator.loc_list);
}
