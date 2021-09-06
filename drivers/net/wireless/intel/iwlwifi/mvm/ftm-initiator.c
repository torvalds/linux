// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2020 Intel Corporation
 */
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

struct iwl_mvm_smooth_entry {
	struct list_head list;
	u8 addr[ETH_ALEN];
	s64 rtt_avg;
	u64 host_time;
};

struct iwl_mvm_ftm_pasn_entry {
	struct list_head list;
	u8 addr[ETH_ALEN];
	u8 hltk[HLTK_11AZ_LEN];
	u8 tk[TK_11AZ_LEN];
	u8 cipher;
	u8 tx_pn[IEEE80211_CCMP_PN_LEN];
	u8 rx_pn[IEEE80211_CCMP_PN_LEN];
};

int iwl_mvm_ftm_add_pasn_sta(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     u8 *addr, u32 cipher, u8 *tk, u32 tk_len,
			     u8 *hltk, u32 hltk_len)
{
	struct iwl_mvm_ftm_pasn_entry *pasn = kzalloc(sizeof(*pasn),
						      GFP_KERNEL);
	u32 expected_tk_len;

	lockdep_assert_held(&mvm->mutex);

	if (!pasn)
		return -ENOBUFS;

	pasn->cipher = iwl_mvm_cipher_to_location_cipher(cipher);

	switch (pasn->cipher) {
	case IWL_LOCATION_CIPHER_CCMP_128:
	case IWL_LOCATION_CIPHER_GCMP_128:
		expected_tk_len = WLAN_KEY_LEN_CCMP;
		break;
	case IWL_LOCATION_CIPHER_GCMP_256:
		expected_tk_len = WLAN_KEY_LEN_GCMP_256;
		break;
	default:
		goto out;
	}

	/*
	 * If associated to this AP and already have security context,
	 * the TK is already configured for this station, so it
	 * shouldn't be set again here.
	 */
	if (vif->bss_conf.assoc &&
	    !memcmp(addr, vif->bss_conf.bssid, ETH_ALEN)) {
		struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
		struct ieee80211_sta *sta;

		rcu_read_lock();
		sta = rcu_dereference(mvm->fw_id_to_mac_id[mvmvif->ap_sta_id]);
		if (!IS_ERR_OR_NULL(sta) && sta->mfp)
			expected_tk_len = 0;
		rcu_read_unlock();
	}

	if (tk_len != expected_tk_len || hltk_len != sizeof(pasn->hltk)) {
		IWL_ERR(mvm, "Invalid key length: tk_len=%u hltk_len=%u\n",
			tk_len, hltk_len);
		goto out;
	}

	memcpy(pasn->addr, addr, sizeof(pasn->addr));
	memcpy(pasn->hltk, hltk, sizeof(pasn->hltk));

	if (tk && tk_len)
		memcpy(pasn->tk, tk, sizeof(pasn->tk));

	list_add_tail(&pasn->list, &mvm->ftm_initiator.pasn_list);
	return 0;
out:
	kfree(pasn);
	return -EINVAL;
}

void iwl_mvm_ftm_remove_pasn_sta(struct iwl_mvm *mvm, u8 *addr)
{
	struct iwl_mvm_ftm_pasn_entry *entry, *prev;

	lockdep_assert_held(&mvm->mutex);

	list_for_each_entry_safe(entry, prev, &mvm->ftm_initiator.pasn_list,
				 list) {
		if (memcmp(entry->addr, addr, sizeof(entry->addr)))
			continue;

		list_del(&entry->list);
		kfree(entry);
		return;
	}
}

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

void iwl_mvm_ftm_initiator_smooth_config(struct iwl_mvm *mvm)
{
	INIT_LIST_HEAD(&mvm->ftm_initiator.smooth.resp);

	IWL_DEBUG_INFO(mvm,
		       "enable=%u, alpha=%u, age_jiffies=%u, thresh=(%u:%u)\n",
			IWL_MVM_FTM_INITIATOR_ENABLE_SMOOTH,
			IWL_MVM_FTM_INITIATOR_SMOOTH_ALPHA,
			IWL_MVM_FTM_INITIATOR_SMOOTH_AGE_SEC * HZ,
			IWL_MVM_FTM_INITIATOR_SMOOTH_OVERSHOOT,
			IWL_MVM_FTM_INITIATOR_SMOOTH_UNDERSHOOT);
}

void iwl_mvm_ftm_initiator_smooth_stop(struct iwl_mvm *mvm)
{
	struct iwl_mvm_smooth_entry *se, *st;

	list_for_each_entry_safe(se, st, &mvm->ftm_initiator.smooth.resp,
				 list) {
		list_del(&se->list);
		kfree(se);
	}
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

static void iwl_mvm_ftm_cmd_common(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif,
				   struct iwl_tof_range_req_cmd_v9 *cmd,
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

static void iwl_mvm_ftm_cmd_v8(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			       struct iwl_tof_range_req_cmd_v8 *cmd,
			       struct cfg80211_pmsr_request *req)
{
	iwl_mvm_ftm_cmd_common(mvm, vif, (void *)cmd, req);
}

static int
iwl_mvm_ftm_target_chandef_v1(struct iwl_mvm *mvm,
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
iwl_mvm_ftm_target_chandef_v2(struct iwl_mvm *mvm,
			      struct cfg80211_pmsr_request_peer *peer,
			      u8 *channel, u8 *format_bw,
			      u8 *ctrl_ch_position)
{
	u32 freq = peer->chandef.chan->center_freq;

	*channel = ieee80211_frequency_to_channel(freq);

	switch (peer->chandef.width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
		*format_bw = IWL_LOCATION_FRAME_FORMAT_LEGACY;
		*format_bw |= IWL_LOCATION_BW_20MHZ << LOCATION_BW_POS;
		break;
	case NL80211_CHAN_WIDTH_20:
		*format_bw = IWL_LOCATION_FRAME_FORMAT_HT;
		*format_bw |= IWL_LOCATION_BW_20MHZ << LOCATION_BW_POS;
		break;
	case NL80211_CHAN_WIDTH_40:
		*format_bw = IWL_LOCATION_FRAME_FORMAT_HT;
		*format_bw |= IWL_LOCATION_BW_40MHZ << LOCATION_BW_POS;
		break;
	case NL80211_CHAN_WIDTH_80:
		*format_bw = IWL_LOCATION_FRAME_FORMAT_VHT;
		*format_bw |= IWL_LOCATION_BW_80MHZ << LOCATION_BW_POS;
		break;
	default:
		IWL_ERR(mvm, "Unsupported BW in FTM request (%d)\n",
			peer->chandef.width);
		return -EINVAL;
	}

	/* non EDCA based measurement must use HE preamble */
	if (peer->ftm.trigger_based || peer->ftm.non_trigger_based)
		*format_bw |= IWL_LOCATION_FRAME_FORMAT_HE;

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

	ret = iwl_mvm_ftm_target_chandef_v1(mvm, peer, &target->channel_num,
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

static void
iwl_mvm_ftm_put_target_common(struct iwl_mvm *mvm,
			      struct cfg80211_pmsr_request_peer *peer,
			      struct iwl_tof_range_req_ap_entry_v6 *target)
{
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

	if (peer->ftm.trigger_based)
		FTM_PUT_FLAG(TB);
	else if (peer->ftm.non_trigger_based)
		FTM_PUT_FLAG(NON_TB);
}

static int
iwl_mvm_ftm_put_target_v3(struct iwl_mvm *mvm,
			  struct cfg80211_pmsr_request_peer *peer,
			  struct iwl_tof_range_req_ap_entry_v3 *target)
{
	int ret;

	ret = iwl_mvm_ftm_target_chandef_v1(mvm, peer, &target->channel_num,
					    &target->bandwidth,
					    &target->ctrl_ch_position);
	if (ret)
		return ret;

	/*
	 * Versions 3 and 4 has some common fields, so
	 * iwl_mvm_ftm_put_target_common() can be used for version 7 too.
	 */
	iwl_mvm_ftm_put_target_common(mvm, peer, (void *)target);

	return 0;
}

static int
iwl_mvm_ftm_put_target_v4(struct iwl_mvm *mvm,
			  struct cfg80211_pmsr_request_peer *peer,
			  struct iwl_tof_range_req_ap_entry_v4 *target)
{
	int ret;

	ret = iwl_mvm_ftm_target_chandef_v2(mvm, peer, &target->channel_num,
					    &target->format_bw,
					    &target->ctrl_ch_position);
	if (ret)
		return ret;

	iwl_mvm_ftm_put_target_common(mvm, peer, (void *)target);

	return 0;
}

static int
iwl_mvm_ftm_put_target(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		       struct cfg80211_pmsr_request_peer *peer,
		       struct iwl_tof_range_req_ap_entry_v6 *target)
{
	int ret;

	ret = iwl_mvm_ftm_target_chandef_v2(mvm, peer, &target->channel_num,
					    &target->format_bw,
					    &target->ctrl_ch_position);
	if (ret)
		return ret;

	iwl_mvm_ftm_put_target_common(mvm, peer, target);

	if (vif->bss_conf.assoc &&
	    !memcmp(peer->addr, vif->bss_conf.bssid, ETH_ALEN)) {
		struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
		struct ieee80211_sta *sta;

		rcu_read_lock();

		sta = rcu_dereference(mvm->fw_id_to_mac_id[mvmvif->ap_sta_id]);
		if (sta->mfp)
			FTM_PUT_FLAG(PMF);

		rcu_read_unlock();

		target->sta_id = mvmvif->ap_sta_id;
	} else {
		target->sta_id = IWL_MVM_INVALID_STA;
	}

	/*
	 * TODO: Beacon interval is currently unknown, so use the common value
	 * of 100 TUs.
	 */
	target->beacon_interval = cpu_to_le16(100);
	return 0;
}

static int iwl_mvm_ftm_send_cmd(struct iwl_mvm *mvm, struct iwl_host_cmd *hcmd)
{
	u32 status;
	int err = iwl_mvm_send_cmd_status(mvm, hcmd, &status);

	if (!err && status) {
		IWL_ERR(mvm, "FTM range request command failure, status: %u\n",
			status);
		err = iwl_ftm_range_request_status_to_err(status);
	}

	return err;
}

static int iwl_mvm_ftm_start_v5(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				struct cfg80211_pmsr_request *req)
{
	struct iwl_tof_range_req_cmd_v5 cmd_v5;
	struct iwl_host_cmd hcmd = {
		.id = iwl_cmd_id(TOF_RANGE_REQ_CMD, LOCATION_GROUP, 0),
		.dataflags[0] = IWL_HCMD_DFL_DUP,
		.data[0] = &cmd_v5,
		.len[0] = sizeof(cmd_v5),
	};
	u8 i;
	int err;

	iwl_mvm_ftm_cmd_v5(mvm, vif, &cmd_v5, req);

	for (i = 0; i < cmd_v5.num_of_ap; i++) {
		struct cfg80211_pmsr_request_peer *peer = &req->peers[i];

		err = iwl_mvm_ftm_put_target_v2(mvm, peer, &cmd_v5.ap[i]);
		if (err)
			return err;
	}

	return iwl_mvm_ftm_send_cmd(mvm, &hcmd);
}

static int iwl_mvm_ftm_start_v7(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				struct cfg80211_pmsr_request *req)
{
	struct iwl_tof_range_req_cmd_v7 cmd_v7;
	struct iwl_host_cmd hcmd = {
		.id = iwl_cmd_id(TOF_RANGE_REQ_CMD, LOCATION_GROUP, 0),
		.dataflags[0] = IWL_HCMD_DFL_DUP,
		.data[0] = &cmd_v7,
		.len[0] = sizeof(cmd_v7),
	};
	u8 i;
	int err;

	/*
	 * Versions 7 and 8 has the same structure except from the responders
	 * list, so iwl_mvm_ftm_cmd() can be used for version 7 too.
	 */
	iwl_mvm_ftm_cmd_v8(mvm, vif, (void *)&cmd_v7, req);

	for (i = 0; i < cmd_v7.num_of_ap; i++) {
		struct cfg80211_pmsr_request_peer *peer = &req->peers[i];

		err = iwl_mvm_ftm_put_target_v3(mvm, peer, &cmd_v7.ap[i]);
		if (err)
			return err;
	}

	return iwl_mvm_ftm_send_cmd(mvm, &hcmd);
}

static int iwl_mvm_ftm_start_v8(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				struct cfg80211_pmsr_request *req)
{
	struct iwl_tof_range_req_cmd_v8 cmd;
	struct iwl_host_cmd hcmd = {
		.id = iwl_cmd_id(TOF_RANGE_REQ_CMD, LOCATION_GROUP, 0),
		.dataflags[0] = IWL_HCMD_DFL_DUP,
		.data[0] = &cmd,
		.len[0] = sizeof(cmd),
	};
	u8 i;
	int err;

	iwl_mvm_ftm_cmd_v8(mvm, vif, (void *)&cmd, req);

	for (i = 0; i < cmd.num_of_ap; i++) {
		struct cfg80211_pmsr_request_peer *peer = &req->peers[i];

		err = iwl_mvm_ftm_put_target_v4(mvm, peer, &cmd.ap[i]);
		if (err)
			return err;
	}

	return iwl_mvm_ftm_send_cmd(mvm, &hcmd);
}

static int iwl_mvm_ftm_start_v9(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				struct cfg80211_pmsr_request *req)
{
	struct iwl_tof_range_req_cmd_v9 cmd;
	struct iwl_host_cmd hcmd = {
		.id = iwl_cmd_id(TOF_RANGE_REQ_CMD, LOCATION_GROUP, 0),
		.dataflags[0] = IWL_HCMD_DFL_DUP,
		.data[0] = &cmd,
		.len[0] = sizeof(cmd),
	};
	u8 i;
	int err;

	iwl_mvm_ftm_cmd_common(mvm, vif, &cmd, req);

	for (i = 0; i < cmd.num_of_ap; i++) {
		struct cfg80211_pmsr_request_peer *peer = &req->peers[i];
		struct iwl_tof_range_req_ap_entry_v6 *target = &cmd.ap[i];

		err = iwl_mvm_ftm_put_target(mvm, vif, peer, target);
		if (err)
			return err;
	}

	return iwl_mvm_ftm_send_cmd(mvm, &hcmd);
}

static void iter(struct ieee80211_hw *hw,
		 struct ieee80211_vif *vif,
		 struct ieee80211_sta *sta,
		 struct ieee80211_key_conf *key,
		 void *data)
{
	struct iwl_tof_range_req_ap_entry_v6 *target = data;

	if (!sta || memcmp(sta->addr, target->bssid, ETH_ALEN))
		return;

	WARN_ON(!sta->mfp);

	if (WARN_ON(key->keylen > sizeof(target->tk)))
		return;

	memcpy(target->tk, key->key, key->keylen);
	target->cipher = iwl_mvm_cipher_to_location_cipher(key->cipher);
	WARN_ON(target->cipher == IWL_LOCATION_CIPHER_INVALID);
}

static void
iwl_mvm_ftm_set_secured_ranging(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				struct iwl_tof_range_req_ap_entry_v7 *target)
{
	struct iwl_mvm_ftm_pasn_entry *entry;
	u32 flags = le32_to_cpu(target->initiator_ap_flags);

	if (!(flags & (IWL_INITIATOR_AP_FLAGS_NON_TB |
		       IWL_INITIATOR_AP_FLAGS_TB)))
		return;

	lockdep_assert_held(&mvm->mutex);

	list_for_each_entry(entry, &mvm->ftm_initiator.pasn_list, list) {
		if (memcmp(entry->addr, target->bssid, sizeof(entry->addr)))
			continue;

		target->cipher = entry->cipher;
		memcpy(target->hltk, entry->hltk, sizeof(target->hltk));

		if (vif->bss_conf.assoc &&
		    !memcmp(vif->bss_conf.bssid, target->bssid,
			    sizeof(target->bssid)))
			ieee80211_iter_keys(mvm->hw, vif, iter, target);
		else
			memcpy(target->tk, entry->tk, sizeof(target->tk));

		memcpy(target->rx_pn, entry->rx_pn, sizeof(target->rx_pn));
		memcpy(target->tx_pn, entry->tx_pn, sizeof(target->tx_pn));

		target->initiator_ap_flags |=
			cpu_to_le32(IWL_INITIATOR_AP_FLAGS_SECURED);
		return;
	}
}

static int
iwl_mvm_ftm_put_target_v7(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			  struct cfg80211_pmsr_request_peer *peer,
			  struct iwl_tof_range_req_ap_entry_v7 *target)
{
	int err = iwl_mvm_ftm_put_target(mvm, vif, peer, (void *)target);
	if (err)
		return err;

	iwl_mvm_ftm_set_secured_ranging(mvm, vif, target);
	return err;
}

static int iwl_mvm_ftm_start_v11(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 struct cfg80211_pmsr_request *req)
{
	struct iwl_tof_range_req_cmd_v11 cmd;
	struct iwl_host_cmd hcmd = {
		.id = iwl_cmd_id(TOF_RANGE_REQ_CMD, LOCATION_GROUP, 0),
		.dataflags[0] = IWL_HCMD_DFL_DUP,
		.data[0] = &cmd,
		.len[0] = sizeof(cmd),
	};
	u8 i;
	int err;

	iwl_mvm_ftm_cmd_common(mvm, vif, (void *)&cmd, req);

	for (i = 0; i < cmd.num_of_ap; i++) {
		struct cfg80211_pmsr_request_peer *peer = &req->peers[i];
		struct iwl_tof_range_req_ap_entry_v7 *target = &cmd.ap[i];

		err = iwl_mvm_ftm_put_target_v7(mvm, vif, peer, target);
		if (err)
			return err;
	}

	return iwl_mvm_ftm_send_cmd(mvm, &hcmd);
}

static void
iwl_mvm_ftm_set_ndp_params(struct iwl_mvm *mvm,
			   struct iwl_tof_range_req_ap_entry_v8 *target)
{
	/* Only 2 STS are supported on Tx */
	u32 i2r_max_sts = IWL_MVM_FTM_I2R_MAX_STS > 1 ? 1 :
		IWL_MVM_FTM_I2R_MAX_STS;

	target->r2i_ndp_params = IWL_MVM_FTM_R2I_MAX_REP |
		(IWL_MVM_FTM_R2I_MAX_STS << IWL_LOCATION_MAX_STS_POS);
	target->i2r_ndp_params = IWL_MVM_FTM_I2R_MAX_REP |
		(i2r_max_sts << IWL_LOCATION_MAX_STS_POS);
	target->r2i_max_total_ltf = IWL_MVM_FTM_R2I_MAX_TOTAL_LTF;
	target->i2r_max_total_ltf = IWL_MVM_FTM_I2R_MAX_TOTAL_LTF;
}

static int iwl_mvm_ftm_start_v12(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 struct cfg80211_pmsr_request *req)
{
	struct iwl_tof_range_req_cmd_v12 cmd;
	struct iwl_host_cmd hcmd = {
		.id = iwl_cmd_id(TOF_RANGE_REQ_CMD, LOCATION_GROUP, 0),
		.dataflags[0] = IWL_HCMD_DFL_DUP,
		.data[0] = &cmd,
		.len[0] = sizeof(cmd),
	};
	u8 i;
	int err;

	iwl_mvm_ftm_cmd_common(mvm, vif, (void *)&cmd, req);

	for (i = 0; i < cmd.num_of_ap; i++) {
		struct cfg80211_pmsr_request_peer *peer = &req->peers[i];
		struct iwl_tof_range_req_ap_entry_v8 *target = &cmd.ap[i];
		u32 flags;

		err = iwl_mvm_ftm_put_target_v7(mvm, vif, peer, (void *)target);
		if (err)
			return err;

		iwl_mvm_ftm_set_ndp_params(mvm, target);

		/*
		 * If secure LTF is turned off, replace the flag with PMF only
		 */
		flags = le32_to_cpu(target->initiator_ap_flags);
		if ((flags & IWL_INITIATOR_AP_FLAGS_SECURED) &&
		    !IWL_MVM_FTM_INITIATOR_SECURE_LTF) {
			flags &= ~IWL_INITIATOR_AP_FLAGS_SECURED;
			flags |= IWL_INITIATOR_AP_FLAGS_PMF;
			target->initiator_ap_flags = cpu_to_le32(flags);
		}
	}

	return iwl_mvm_ftm_send_cmd(mvm, &hcmd);
}

int iwl_mvm_ftm_start(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		      struct cfg80211_pmsr_request *req)
{
	bool new_api = fw_has_api(&mvm->fw->ucode_capa,
				  IWL_UCODE_TLV_API_FTM_NEW_RANGE_REQ);
	int err;

	lockdep_assert_held(&mvm->mutex);

	if (mvm->ftm_initiator.req)
		return -EBUSY;

	if (new_api) {
		u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw, LOCATION_GROUP,
						   TOF_RANGE_REQ_CMD,
						   IWL_FW_CMD_VER_UNKNOWN);

		switch (cmd_ver) {
		case 12:
			err = iwl_mvm_ftm_start_v12(mvm, vif, req);
			break;
		case 11:
			err = iwl_mvm_ftm_start_v11(mvm, vif, req);
			break;
		case 9:
		case 10:
			err = iwl_mvm_ftm_start_v9(mvm, vif, req);
			break;
		case 8:
			err = iwl_mvm_ftm_start_v8(mvm, vif, req);
			break;
		default:
			err = iwl_mvm_ftm_start_v7(mvm, vif, req);
			break;
		}
	} else {
		err = iwl_mvm_ftm_start_v5(mvm, vif, req);
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

	iwl_mvm_ftm_reset(mvm);

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

static void iwl_mvm_ftm_rtt_smoothing(struct iwl_mvm *mvm,
				      struct cfg80211_pmsr_result *res)
{
	struct iwl_mvm_smooth_entry *resp;
	s64 rtt_avg, rtt = res->ftm.rtt_avg;
	u32 undershoot, overshoot;
	u8 alpha;
	bool found;

	if (!IWL_MVM_FTM_INITIATOR_ENABLE_SMOOTH)
		return;

	WARN_ON(rtt < 0);

	if (res->status != NL80211_PMSR_STATUS_SUCCESS) {
		IWL_DEBUG_INFO(mvm,
			       ": %pM: ignore failed measurement. Status=%u\n",
			       res->addr, res->status);
		return;
	}

	found = false;
	list_for_each_entry(resp, &mvm->ftm_initiator.smooth.resp, list) {
		if (!memcmp(res->addr, resp->addr, ETH_ALEN)) {
			found = true;
			break;
		}
	}

	if (!found) {
		resp = kzalloc(sizeof(*resp), GFP_KERNEL);
		if (!resp)
			return;

		memcpy(resp->addr, res->addr, ETH_ALEN);
		list_add_tail(&resp->list, &mvm->ftm_initiator.smooth.resp);

		resp->rtt_avg = rtt;

		IWL_DEBUG_INFO(mvm, "new: %pM: rtt_avg=%lld\n",
			       resp->addr, resp->rtt_avg);
		goto update_time;
	}

	if (res->host_time - resp->host_time >
	    IWL_MVM_FTM_INITIATOR_SMOOTH_AGE_SEC * 1000000000) {
		resp->rtt_avg = rtt;

		IWL_DEBUG_INFO(mvm, "expired: %pM: rtt_avg=%lld\n",
			       resp->addr, resp->rtt_avg);
		goto update_time;
	}

	/* Smooth the results based on the tracked RTT average */
	undershoot = IWL_MVM_FTM_INITIATOR_SMOOTH_UNDERSHOOT;
	overshoot = IWL_MVM_FTM_INITIATOR_SMOOTH_OVERSHOOT;
	alpha = IWL_MVM_FTM_INITIATOR_SMOOTH_ALPHA;

	rtt_avg = (alpha * rtt + (100 - alpha) * resp->rtt_avg) / 100;

	IWL_DEBUG_INFO(mvm,
		       "%pM: prev rtt_avg=%lld, new rtt_avg=%lld, rtt=%lld\n",
		       resp->addr, resp->rtt_avg, rtt_avg, rtt);

	/*
	 * update the responder's average RTT results regardless of
	 * the under/over shoot logic below
	 */
	resp->rtt_avg = rtt_avg;

	/* smooth the results */
	if (rtt_avg > rtt && (rtt_avg - rtt) > undershoot) {
		res->ftm.rtt_avg = rtt_avg;

		IWL_DEBUG_INFO(mvm,
			       "undershoot: val=%lld\n",
			       (rtt_avg - rtt));
	} else if (rtt_avg < rtt && (rtt - rtt_avg) >
		   overshoot) {
		res->ftm.rtt_avg = rtt_avg;
		IWL_DEBUG_INFO(mvm,
			       "overshoot: val=%lld\n",
			       (rtt - rtt_avg));
	}

update_time:
	resp->host_time = res->host_time;
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

static void
iwl_mvm_ftm_pasn_update_pn(struct iwl_mvm *mvm,
			   struct iwl_tof_range_rsp_ap_entry_ntfy_v6 *fw_ap)
{
	struct iwl_mvm_ftm_pasn_entry *entry;

	lockdep_assert_held(&mvm->mutex);

	list_for_each_entry(entry, &mvm->ftm_initiator.pasn_list, list) {
		if (memcmp(fw_ap->bssid, entry->addr, sizeof(entry->addr)))
			continue;

		memcpy(entry->rx_pn, fw_ap->rx_pn, sizeof(entry->rx_pn));
		memcpy(entry->tx_pn, fw_ap->tx_pn, sizeof(entry->tx_pn));
		return;
	}
}

static u8 iwl_mvm_ftm_get_range_resp_ver(struct iwl_mvm *mvm)
{
	if (!fw_has_api(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_API_FTM_NEW_RANGE_REQ))
		return 5;

	/* Starting from version 8, the FW advertises the version */
	if (mvm->cmd_ver.range_resp >= 8)
		return mvm->cmd_ver.range_resp;
	else if (fw_has_api(&mvm->fw->ucode_capa,
			    IWL_UCODE_TLV_API_FTM_RTT_ACCURACY))
		return 7;

	/* The first version of the new range request API */
	return 6;
}

static bool iwl_mvm_ftm_resp_size_validation(u8 ver, unsigned int pkt_len)
{
	switch (ver) {
	case 8:
		return pkt_len == sizeof(struct iwl_tof_range_rsp_ntfy_v8);
	case 7:
		return pkt_len == sizeof(struct iwl_tof_range_rsp_ntfy_v7);
	case 6:
		return pkt_len == sizeof(struct iwl_tof_range_rsp_ntfy_v6);
	case 5:
		return pkt_len == sizeof(struct iwl_tof_range_rsp_ntfy_v5);
	default:
		WARN_ONCE(1, "FTM: unsupported range response version %u", ver);
		return false;
	}
}

void iwl_mvm_ftm_range_resp(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	unsigned int pkt_len = iwl_rx_packet_payload_len(pkt);
	struct iwl_tof_range_rsp_ntfy_v5 *fw_resp_v5 = (void *)pkt->data;
	struct iwl_tof_range_rsp_ntfy_v6 *fw_resp_v6 = (void *)pkt->data;
	struct iwl_tof_range_rsp_ntfy_v7 *fw_resp_v7 = (void *)pkt->data;
	struct iwl_tof_range_rsp_ntfy_v8 *fw_resp_v8 = (void *)pkt->data;
	int i;
	bool new_api = fw_has_api(&mvm->fw->ucode_capa,
				  IWL_UCODE_TLV_API_FTM_NEW_RANGE_REQ);
	u8 num_of_aps, last_in_batch;
	u8 notif_ver = iwl_mvm_ftm_get_range_resp_ver(mvm);

	lockdep_assert_held(&mvm->mutex);

	if (!mvm->ftm_initiator.req) {
		return;
	}

	if (unlikely(!iwl_mvm_ftm_resp_size_validation(notif_ver, pkt_len)))
		return;

	if (new_api) {
		if (iwl_mvm_ftm_range_resp_valid(mvm, fw_resp_v8->request_id,
						 fw_resp_v8->num_of_aps))
			return;

		num_of_aps = fw_resp_v8->num_of_aps;
		last_in_batch = fw_resp_v8->last_report;
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
		struct iwl_tof_range_rsp_ap_entry_ntfy_v6 *fw_ap;
		int peer_idx;

		if (new_api) {
			if (notif_ver == 8) {
				fw_ap = &fw_resp_v8->ap[i];
				iwl_mvm_ftm_pasn_update_pn(mvm, fw_ap);
			} else if (notif_ver == 7) {
				fw_ap = (void *)&fw_resp_v7->ap[i];
			} else {
				fw_ap = (void *)&fw_resp_v6->ap[i];
			}

			result.final = fw_ap->last_burst;
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

		iwl_mvm_ftm_rtt_smoothing(mvm, &result);

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
