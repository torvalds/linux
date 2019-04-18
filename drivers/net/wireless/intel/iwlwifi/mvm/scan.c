/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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
 *
 *****************************************************************************/

#include <linux/etherdevice.h>
#include <net/mac80211.h>

#include "mvm.h"
#include "fw/api/scan.h"
#include "iwl-io.h"

#define IWL_DENSE_EBS_SCAN_RATIO 5
#define IWL_SPARSE_EBS_SCAN_RATIO 1

#define IWL_SCAN_DWELL_ACTIVE		10
#define IWL_SCAN_DWELL_PASSIVE		110
#define IWL_SCAN_DWELL_FRAGMENTED	44
#define IWL_SCAN_DWELL_EXTENDED		90
#define IWL_SCAN_NUM_OF_FRAGS		3


/* adaptive dwell max budget time [TU] for full scan */
#define IWL_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN 300
/* adaptive dwell max budget time [TU] for directed scan */
#define IWL_SCAN_ADWELL_MAX_BUDGET_DIRECTED_SCAN 100
/* adaptive dwell default APs number */
#define IWL_SCAN_ADWELL_DEFAULT_N_APS 2
/* adaptive dwell default APs number in social channels (1, 6, 11) */
#define IWL_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL 10

struct iwl_mvm_scan_timing_params {
	u32 suspend_time;
	u32 max_out_time;
};

static struct iwl_mvm_scan_timing_params scan_timing[] = {
	[IWL_SCAN_TYPE_UNASSOC] = {
		.suspend_time = 0,
		.max_out_time = 0,
	},
	[IWL_SCAN_TYPE_WILD] = {
		.suspend_time = 30,
		.max_out_time = 120,
	},
	[IWL_SCAN_TYPE_MILD] = {
		.suspend_time = 120,
		.max_out_time = 120,
	},
	[IWL_SCAN_TYPE_FRAGMENTED] = {
		.suspend_time = 95,
		.max_out_time = 44,
	},
	[IWL_SCAN_TYPE_FAST_BALANCE] = {
		.suspend_time = 30,
		.max_out_time = 37,
	},
};

struct iwl_mvm_scan_params {
	/* For CDB this is low band scan type, for non-CDB - type. */
	enum iwl_mvm_scan_type type;
	enum iwl_mvm_scan_type hb_type;
	u32 n_channels;
	u16 delay;
	int n_ssids;
	struct cfg80211_ssid *ssids;
	struct ieee80211_channel **channels;
	u32 flags;
	u8 *mac_addr;
	u8 *mac_addr_mask;
	bool no_cck;
	bool pass_all;
	int n_match_sets;
	struct iwl_scan_probe_req preq;
	struct cfg80211_match_set *match_sets;
	int n_scan_plans;
	struct cfg80211_sched_scan_plan *scan_plans;
	u32 measurement_dwell;
};

static inline void *iwl_mvm_get_scan_req_umac_data(struct iwl_mvm *mvm)
{
	struct iwl_scan_req_umac *cmd = mvm->scan_cmd;

	if (iwl_mvm_is_adaptive_dwell_v2_supported(mvm))
		return (void *)&cmd->v8.data;

	if (iwl_mvm_is_adaptive_dwell_supported(mvm))
		return (void *)&cmd->v7.data;

	if (iwl_mvm_cdb_scan_api(mvm))
		return (void *)&cmd->v6.data;

	return (void *)&cmd->v1.data;
}

static inline struct iwl_scan_umac_chan_param *
iwl_mvm_get_scan_req_umac_channel(struct iwl_mvm *mvm)
{
	struct iwl_scan_req_umac *cmd = mvm->scan_cmd;

	if (iwl_mvm_is_adaptive_dwell_v2_supported(mvm))
		return &cmd->v8.channel;

	if (iwl_mvm_is_adaptive_dwell_supported(mvm))
		return &cmd->v7.channel;

	if (iwl_mvm_cdb_scan_api(mvm))
		return &cmd->v6.channel;

	return &cmd->v1.channel;
}

static u8 iwl_mvm_scan_rx_ant(struct iwl_mvm *mvm)
{
	if (mvm->scan_rx_ant != ANT_NONE)
		return mvm->scan_rx_ant;
	return iwl_mvm_get_valid_rx_ant(mvm);
}

static inline __le16 iwl_mvm_scan_rx_chain(struct iwl_mvm *mvm)
{
	u16 rx_chain;
	u8 rx_ant;

	rx_ant = iwl_mvm_scan_rx_ant(mvm);
	rx_chain = rx_ant << PHY_RX_CHAIN_VALID_POS;
	rx_chain |= rx_ant << PHY_RX_CHAIN_FORCE_MIMO_SEL_POS;
	rx_chain |= rx_ant << PHY_RX_CHAIN_FORCE_SEL_POS;
	rx_chain |= 0x1 << PHY_RX_CHAIN_DRIVER_FORCE_POS;
	return cpu_to_le16(rx_chain);
}

static __le32 iwl_mvm_scan_rxon_flags(enum nl80211_band band)
{
	if (band == NL80211_BAND_2GHZ)
		return cpu_to_le32(PHY_BAND_24);
	else
		return cpu_to_le32(PHY_BAND_5);
}

static inline __le32
iwl_mvm_scan_rate_n_flags(struct iwl_mvm *mvm, enum nl80211_band band,
			  bool no_cck)
{
	u32 tx_ant;

	iwl_mvm_toggle_tx_ant(mvm, &mvm->scan_last_antenna_idx);
	tx_ant = BIT(mvm->scan_last_antenna_idx) << RATE_MCS_ANT_POS;

	if (band == NL80211_BAND_2GHZ && !no_cck)
		return cpu_to_le32(IWL_RATE_1M_PLCP | RATE_MCS_CCK_MSK |
				   tx_ant);
	else
		return cpu_to_le32(IWL_RATE_6M_PLCP | tx_ant);
}

static void iwl_mvm_scan_condition_iterator(void *data, u8 *mac,
					    struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int *global_cnt = data;

	if (vif->type != NL80211_IFTYPE_P2P_DEVICE && mvmvif->phy_ctxt &&
	    mvmvif->phy_ctxt->id < NUM_PHY_CTX)
		*global_cnt += 1;
}

static enum iwl_mvm_traffic_load iwl_mvm_get_traffic_load(struct iwl_mvm *mvm)
{
	return mvm->tcm.result.global_load;
}

static enum iwl_mvm_traffic_load
iwl_mvm_get_traffic_load_band(struct iwl_mvm *mvm, enum nl80211_band band)
{
	return mvm->tcm.result.band_load[band];
}

struct iwl_is_dcm_with_go_iterator_data {
	struct ieee80211_vif *current_vif;
	bool is_dcm_with_p2p_go;
};

static void iwl_mvm_is_dcm_with_go_iterator(void *_data, u8 *mac,
					    struct ieee80211_vif *vif)
{
	struct iwl_is_dcm_with_go_iterator_data *data = _data;
	struct iwl_mvm_vif *other_mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_vif *curr_mvmvif =
		iwl_mvm_vif_from_mac80211(data->current_vif);

	/* exclude the given vif */
	if (vif == data->current_vif)
		return;

	if (vif->type == NL80211_IFTYPE_AP && vif->p2p &&
	    other_mvmvif->phy_ctxt && curr_mvmvif->phy_ctxt &&
	    other_mvmvif->phy_ctxt->id != curr_mvmvif->phy_ctxt->id)
		data->is_dcm_with_p2p_go = true;
}

static enum
iwl_mvm_scan_type _iwl_mvm_get_scan_type(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif,
					 enum iwl_mvm_traffic_load load,
					 bool low_latency)
{
	int global_cnt = 0;

	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
					    IEEE80211_IFACE_ITER_NORMAL,
					    iwl_mvm_scan_condition_iterator,
					    &global_cnt);
	if (!global_cnt)
		return IWL_SCAN_TYPE_UNASSOC;

	if (fw_has_api(&mvm->fw->ucode_capa,
		       IWL_UCODE_TLV_API_FRAGMENTED_SCAN)) {
		if ((load == IWL_MVM_TRAFFIC_HIGH || low_latency) &&
		    (!vif || vif->type != NL80211_IFTYPE_P2P_DEVICE))
			return IWL_SCAN_TYPE_FRAGMENTED;

		/* in case of DCM with GO where BSS DTIM interval < 220msec
		 * set all scan requests as fast-balance scan
		 * */
		if (vif && vif->type == NL80211_IFTYPE_STATION &&
		    vif->bss_conf.dtim_period < 220) {
			struct iwl_is_dcm_with_go_iterator_data data = {
				.current_vif = vif,
				.is_dcm_with_p2p_go = false,
			};

			ieee80211_iterate_active_interfaces_atomic(mvm->hw,
						IEEE80211_IFACE_ITER_NORMAL,
						iwl_mvm_is_dcm_with_go_iterator,
						&data);
			if (data.is_dcm_with_p2p_go)
				return IWL_SCAN_TYPE_FAST_BALANCE;
		}
	}

	if (load >= IWL_MVM_TRAFFIC_MEDIUM || low_latency)
		return IWL_SCAN_TYPE_MILD;

	return IWL_SCAN_TYPE_WILD;
}

static enum
iwl_mvm_scan_type iwl_mvm_get_scan_type(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif)
{
	enum iwl_mvm_traffic_load load;
	bool low_latency;

	load = iwl_mvm_get_traffic_load(mvm);
	low_latency = iwl_mvm_low_latency(mvm);

	return _iwl_mvm_get_scan_type(mvm, vif, load, low_latency);
}

static enum
iwl_mvm_scan_type iwl_mvm_get_scan_type_band(struct iwl_mvm *mvm,
					     struct ieee80211_vif *vif,
					     enum nl80211_band band)
{
	enum iwl_mvm_traffic_load load;
	bool low_latency;

	load = iwl_mvm_get_traffic_load_band(mvm, band);
	low_latency = iwl_mvm_low_latency_band(mvm, band);

	return _iwl_mvm_get_scan_type(mvm, vif, load, low_latency);
}

static int
iwl_mvm_get_measurement_dwell(struct iwl_mvm *mvm,
			      struct cfg80211_scan_request *req,
			      struct iwl_mvm_scan_params *params)
{
	u32 duration = scan_timing[params->type].max_out_time;

	if (!req->duration)
		return 0;

	if (iwl_mvm_is_cdb_supported(mvm)) {
		u32 hb_time = scan_timing[params->hb_type].max_out_time;

		duration = min_t(u32, duration, hb_time);
	}

	if (req->duration_mandatory && req->duration > duration) {
		IWL_DEBUG_SCAN(mvm,
			       "Measurement scan - too long dwell %hu (max out time %u)\n",
			       req->duration,
			       duration);
		return -EOPNOTSUPP;
	}

	return min_t(u32, (u32)req->duration, duration);
}

static inline bool iwl_mvm_rrm_scan_needed(struct iwl_mvm *mvm)
{
	/* require rrm scan whenever the fw supports it */
	return fw_has_capa(&mvm->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT);
}

static int iwl_mvm_max_scan_ie_fw_cmd_room(struct iwl_mvm *mvm)
{
	int max_probe_len;

	max_probe_len = SCAN_OFFLOAD_PROBE_REQ_SIZE;

	/* we create the 802.11 header and SSID element */
	max_probe_len -= 24 + 2;

	/* DS parameter set element is added on 2.4GHZ band if required */
	if (iwl_mvm_rrm_scan_needed(mvm))
		max_probe_len -= 3;

	return max_probe_len;
}

int iwl_mvm_max_scan_ie_len(struct iwl_mvm *mvm)
{
	int max_ie_len = iwl_mvm_max_scan_ie_fw_cmd_room(mvm);

	/* TODO: [BUG] This function should return the maximum allowed size of
	 * scan IEs, however the LMAC scan api contains both 2GHZ and 5GHZ IEs
	 * in the same command. So the correct implementation of this function
	 * is just iwl_mvm_max_scan_ie_fw_cmd_room() / 2. Currently the scan
	 * command has only 512 bytes and it would leave us with about 240
	 * bytes for scan IEs, which is clearly not enough. So meanwhile
	 * we will report an incorrect value. This may result in a failure to
	 * issue a scan in unified_scan_lmac and unified_sched_scan_lmac
	 * functions with -ENOBUFS, if a large enough probe will be provided.
	 */
	return max_ie_len;
}

void iwl_mvm_rx_lmac_scan_iter_complete_notif(struct iwl_mvm *mvm,
					      struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_lmac_scan_complete_notif *notif = (void *)pkt->data;

	IWL_DEBUG_SCAN(mvm,
		       "Scan offload iteration complete: status=0x%x scanned channels=%d\n",
		       notif->status, notif->scanned_channels);

	if (mvm->sched_scan_pass_all == SCHED_SCAN_PASS_ALL_FOUND) {
		IWL_DEBUG_SCAN(mvm, "Pass all scheduled scan results found\n");
		ieee80211_sched_scan_results(mvm->hw);
		mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_ENABLED;
	}
}

void iwl_mvm_rx_scan_match_found(struct iwl_mvm *mvm,
				 struct iwl_rx_cmd_buffer *rxb)
{
	IWL_DEBUG_SCAN(mvm, "Scheduled scan results\n");
	ieee80211_sched_scan_results(mvm->hw);
}

static const char *iwl_mvm_ebs_status_str(enum iwl_scan_ebs_status status)
{
	switch (status) {
	case IWL_SCAN_EBS_SUCCESS:
		return "successful";
	case IWL_SCAN_EBS_INACTIVE:
		return "inactive";
	case IWL_SCAN_EBS_FAILED:
	case IWL_SCAN_EBS_CHAN_NOT_FOUND:
	default:
		return "failed";
	}
}

void iwl_mvm_rx_lmac_scan_complete_notif(struct iwl_mvm *mvm,
					 struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_periodic_scan_complete *scan_notif = (void *)pkt->data;
	bool aborted = (scan_notif->status == IWL_SCAN_OFFLOAD_ABORTED);

	/* If this happens, the firmware has mistakenly sent an LMAC
	 * notification during UMAC scans -- warn and ignore it.
	 */
	if (WARN_ON_ONCE(fw_has_capa(&mvm->fw->ucode_capa,
				     IWL_UCODE_TLV_CAPA_UMAC_SCAN)))
		return;

	/* scan status must be locked for proper checking */
	lockdep_assert_held(&mvm->mutex);

	/* We first check if we were stopping a scan, in which case we
	 * just clear the stopping flag.  Then we check if it was a
	 * firmware initiated stop, in which case we need to inform
	 * mac80211.
	 * Note that we can have a stopping and a running scan
	 * simultaneously, but we can't have two different types of
	 * scans stopping or running at the same time (since LMAC
	 * doesn't support it).
	 */

	if (mvm->scan_status & IWL_MVM_SCAN_STOPPING_SCHED) {
		WARN_ON_ONCE(mvm->scan_status & IWL_MVM_SCAN_STOPPING_REGULAR);

		IWL_DEBUG_SCAN(mvm, "Scheduled scan %s, EBS status %s\n",
			       aborted ? "aborted" : "completed",
			       iwl_mvm_ebs_status_str(scan_notif->ebs_status));
		IWL_DEBUG_SCAN(mvm,
			       "Last line %d, Last iteration %d, Time after last iteration %d\n",
			       scan_notif->last_schedule_line,
			       scan_notif->last_schedule_iteration,
			       __le32_to_cpu(scan_notif->time_after_last_iter));

		mvm->scan_status &= ~IWL_MVM_SCAN_STOPPING_SCHED;
	} else if (mvm->scan_status & IWL_MVM_SCAN_STOPPING_REGULAR) {
		IWL_DEBUG_SCAN(mvm, "Regular scan %s, EBS status %s\n",
			       aborted ? "aborted" : "completed",
			       iwl_mvm_ebs_status_str(scan_notif->ebs_status));

		mvm->scan_status &= ~IWL_MVM_SCAN_STOPPING_REGULAR;
	} else if (mvm->scan_status & IWL_MVM_SCAN_SCHED) {
		WARN_ON_ONCE(mvm->scan_status & IWL_MVM_SCAN_REGULAR);

		IWL_DEBUG_SCAN(mvm, "Scheduled scan %s, EBS status %s\n",
			       aborted ? "aborted" : "completed",
			       iwl_mvm_ebs_status_str(scan_notif->ebs_status));
		IWL_DEBUG_SCAN(mvm,
			       "Last line %d, Last iteration %d, Time after last iteration %d (FW)\n",
			       scan_notif->last_schedule_line,
			       scan_notif->last_schedule_iteration,
			       __le32_to_cpu(scan_notif->time_after_last_iter));

		mvm->scan_status &= ~IWL_MVM_SCAN_SCHED;
		ieee80211_sched_scan_stopped(mvm->hw);
		mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_DISABLED;
	} else if (mvm->scan_status & IWL_MVM_SCAN_REGULAR) {
		struct cfg80211_scan_info info = {
			.aborted = aborted,
		};

		IWL_DEBUG_SCAN(mvm, "Regular scan %s, EBS status %s (FW)\n",
			       aborted ? "aborted" : "completed",
			       iwl_mvm_ebs_status_str(scan_notif->ebs_status));

		mvm->scan_status &= ~IWL_MVM_SCAN_REGULAR;
		ieee80211_scan_completed(mvm->hw, &info);
		iwl_mvm_unref(mvm, IWL_MVM_REF_SCAN);
		cancel_delayed_work(&mvm->scan_timeout_dwork);
		iwl_mvm_resume_tcm(mvm);
	} else {
		IWL_ERR(mvm,
			"got scan complete notification but no scan is running\n");
	}

	mvm->last_ebs_successful =
			scan_notif->ebs_status == IWL_SCAN_EBS_SUCCESS ||
			scan_notif->ebs_status == IWL_SCAN_EBS_INACTIVE;
}

static int iwl_ssid_exist(u8 *ssid, u8 ssid_len, struct iwl_ssid_ie *ssid_list)
{
	int i;

	for (i = 0; i < PROBE_OPTION_MAX; i++) {
		if (!ssid_list[i].len)
			break;
		if (ssid_list[i].len == ssid_len &&
		    !memcmp(ssid_list->ssid, ssid, ssid_len))
			return i;
	}
	return -1;
}

/* We insert the SSIDs in an inverted order, because the FW will
 * invert it back.
 */
static void iwl_scan_build_ssids(struct iwl_mvm_scan_params *params,
				 struct iwl_ssid_ie *ssids,
				 u32 *ssid_bitmap)
{
	int i, j;
	int index;

	/*
	 * copy SSIDs from match list.
	 * iwl_config_sched_scan_profiles() uses the order of these ssids to
	 * config match list.
	 */
	for (i = 0, j = params->n_match_sets - 1;
	     j >= 0 && i < PROBE_OPTION_MAX;
	     i++, j--) {
		/* skip empty SSID matchsets */
		if (!params->match_sets[j].ssid.ssid_len)
			continue;
		ssids[i].id = WLAN_EID_SSID;
		ssids[i].len = params->match_sets[j].ssid.ssid_len;
		memcpy(ssids[i].ssid, params->match_sets[j].ssid.ssid,
		       ssids[i].len);
	}

	/* add SSIDs from scan SSID list */
	*ssid_bitmap = 0;
	for (j = params->n_ssids - 1;
	     j >= 0 && i < PROBE_OPTION_MAX;
	     i++, j--) {
		index = iwl_ssid_exist(params->ssids[j].ssid,
				       params->ssids[j].ssid_len,
				       ssids);
		if (index < 0) {
			ssids[i].id = WLAN_EID_SSID;
			ssids[i].len = params->ssids[j].ssid_len;
			memcpy(ssids[i].ssid, params->ssids[j].ssid,
			       ssids[i].len);
			*ssid_bitmap |= BIT(i);
		} else {
			*ssid_bitmap |= BIT(index);
		}
	}
}

static int
iwl_mvm_config_sched_scan_profiles(struct iwl_mvm *mvm,
				   struct cfg80211_sched_scan_request *req)
{
	struct iwl_scan_offload_profile *profile;
	struct iwl_scan_offload_profile_cfg *profile_cfg;
	struct iwl_scan_offload_blacklist *blacklist;
	struct iwl_host_cmd cmd = {
		.id = SCAN_OFFLOAD_UPDATE_PROFILES_CMD,
		.len[1] = sizeof(*profile_cfg),
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
		.dataflags[1] = IWL_HCMD_DFL_NOCOPY,
	};
	int blacklist_len;
	int i;
	int ret;

	if (WARN_ON(req->n_match_sets > IWL_SCAN_MAX_PROFILES))
		return -EIO;

	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_SHORT_BL)
		blacklist_len = IWL_SCAN_SHORT_BLACKLIST_LEN;
	else
		blacklist_len = IWL_SCAN_MAX_BLACKLIST_LEN;

	blacklist = kcalloc(blacklist_len, sizeof(*blacklist), GFP_KERNEL);
	if (!blacklist)
		return -ENOMEM;

	profile_cfg = kzalloc(sizeof(*profile_cfg), GFP_KERNEL);
	if (!profile_cfg) {
		ret = -ENOMEM;
		goto free_blacklist;
	}

	cmd.data[0] = blacklist;
	cmd.len[0] = sizeof(*blacklist) * blacklist_len;
	cmd.data[1] = profile_cfg;

	/* No blacklist configuration */

	profile_cfg->num_profiles = req->n_match_sets;
	profile_cfg->active_clients = SCAN_CLIENT_SCHED_SCAN;
	profile_cfg->pass_match = SCAN_CLIENT_SCHED_SCAN;
	profile_cfg->match_notify = SCAN_CLIENT_SCHED_SCAN;
	if (!req->n_match_sets || !req->match_sets[0].ssid.ssid_len)
		profile_cfg->any_beacon_notify = SCAN_CLIENT_SCHED_SCAN;

	for (i = 0; i < req->n_match_sets; i++) {
		profile = &profile_cfg->profiles[i];
		profile->ssid_index = i;
		/* Support any cipher and auth algorithm */
		profile->unicast_cipher = 0xff;
		profile->auth_alg = 0xff;
		profile->network_type = IWL_NETWORK_TYPE_ANY;
		profile->band_selection = IWL_SCAN_OFFLOAD_SELECT_ANY;
		profile->client_bitmap = SCAN_CLIENT_SCHED_SCAN;
	}

	IWL_DEBUG_SCAN(mvm, "Sending scheduled scan profile config\n");

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	kfree(profile_cfg);
free_blacklist:
	kfree(blacklist);

	return ret;
}

static bool iwl_mvm_scan_pass_all(struct iwl_mvm *mvm,
				  struct cfg80211_sched_scan_request *req)
{
	if (req->n_match_sets && req->match_sets[0].ssid.ssid_len) {
		IWL_DEBUG_SCAN(mvm,
			       "Sending scheduled scan with filtering, n_match_sets %d\n",
			       req->n_match_sets);
		mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_DISABLED;
		return false;
	}

	IWL_DEBUG_SCAN(mvm, "Sending Scheduled scan without filtering\n");

	mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_ENABLED;
	return true;
}

static int iwl_mvm_lmac_scan_abort(struct iwl_mvm *mvm)
{
	int ret;
	struct iwl_host_cmd cmd = {
		.id = SCAN_OFFLOAD_ABORT_CMD,
	};
	u32 status = CAN_ABORT_STATUS;

	ret = iwl_mvm_send_cmd_status(mvm, &cmd, &status);
	if (ret)
		return ret;

	if (status != CAN_ABORT_STATUS) {
		/*
		 * The scan abort will return 1 for success or
		 * 2 for "failure".  A failure condition can be
		 * due to simply not being in an active scan which
		 * can occur if we send the scan abort before the
		 * microcode has notified us that a scan is completed.
		 */
		IWL_DEBUG_SCAN(mvm, "SCAN OFFLOAD ABORT ret %d.\n", status);
		ret = -ENOENT;
	}

	return ret;
}

static void iwl_mvm_scan_fill_tx_cmd(struct iwl_mvm *mvm,
				     struct iwl_scan_req_tx_cmd *tx_cmd,
				     bool no_cck)
{
	tx_cmd[0].tx_flags = cpu_to_le32(TX_CMD_FLG_SEQ_CTL |
					 TX_CMD_FLG_BT_DIS);
	tx_cmd[0].rate_n_flags = iwl_mvm_scan_rate_n_flags(mvm,
							   NL80211_BAND_2GHZ,
							   no_cck);
	tx_cmd[0].sta_id = mvm->aux_sta.sta_id;

	tx_cmd[1].tx_flags = cpu_to_le32(TX_CMD_FLG_SEQ_CTL |
					 TX_CMD_FLG_BT_DIS);
	tx_cmd[1].rate_n_flags = iwl_mvm_scan_rate_n_flags(mvm,
							   NL80211_BAND_5GHZ,
							   no_cck);
	tx_cmd[1].sta_id = mvm->aux_sta.sta_id;
}

static void
iwl_mvm_lmac_scan_cfg_channels(struct iwl_mvm *mvm,
			       struct ieee80211_channel **channels,
			       int n_channels, u32 ssid_bitmap,
			       struct iwl_scan_req_lmac *cmd)
{
	struct iwl_scan_channel_cfg_lmac *channel_cfg = (void *)&cmd->data;
	int i;

	for (i = 0; i < n_channels; i++) {
		channel_cfg[i].channel_num =
			cpu_to_le16(channels[i]->hw_value);
		channel_cfg[i].iter_count = cpu_to_le16(1);
		channel_cfg[i].iter_interval = 0;
		channel_cfg[i].flags =
			cpu_to_le32(IWL_UNIFIED_SCAN_CHANNEL_PARTIAL |
				    ssid_bitmap);
	}
}

static u8 *iwl_mvm_copy_and_insert_ds_elem(struct iwl_mvm *mvm, const u8 *ies,
					   size_t len, u8 *const pos)
{
	static const u8 before_ds_params[] = {
			WLAN_EID_SSID,
			WLAN_EID_SUPP_RATES,
			WLAN_EID_REQUEST,
			WLAN_EID_EXT_SUPP_RATES,
	};
	size_t offs;
	u8 *newpos = pos;

	if (!iwl_mvm_rrm_scan_needed(mvm)) {
		memcpy(newpos, ies, len);
		return newpos + len;
	}

	offs = ieee80211_ie_split(ies, len,
				  before_ds_params,
				  ARRAY_SIZE(before_ds_params),
				  0);

	memcpy(newpos, ies, offs);
	newpos += offs;

	/* Add a placeholder for DS Parameter Set element */
	*newpos++ = WLAN_EID_DS_PARAMS;
	*newpos++ = 1;
	*newpos++ = 0;

	memcpy(newpos, ies + offs, len - offs);
	newpos += len - offs;

	return newpos;
}

#define WFA_TPC_IE_LEN	9

static void iwl_mvm_add_tpc_report_ie(u8 *pos)
{
	pos[0] = WLAN_EID_VENDOR_SPECIFIC;
	pos[1] = WFA_TPC_IE_LEN - 2;
	pos[2] = (WLAN_OUI_MICROSOFT >> 16) & 0xff;
	pos[3] = (WLAN_OUI_MICROSOFT >> 8) & 0xff;
	pos[4] = WLAN_OUI_MICROSOFT & 0xff;
	pos[5] = WLAN_OUI_TYPE_MICROSOFT_TPC;
	pos[6] = 0;
	/* pos[7] - tx power will be inserted by the FW */
	pos[7] = 0;
	pos[8] = 0;
}

static void
iwl_mvm_build_scan_probe(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			 struct ieee80211_scan_ies *ies,
			 struct iwl_mvm_scan_params *params)
{
	struct ieee80211_mgmt *frame = (void *)params->preq.buf;
	u8 *pos, *newpos;
	const u8 *mac_addr = params->flags & NL80211_SCAN_FLAG_RANDOM_ADDR ?
		params->mac_addr : NULL;

	/*
	 * Unfortunately, right now the offload scan doesn't support randomising
	 * within the firmware, so until the firmware API is ready we implement
	 * it in the driver. This means that the scan iterations won't really be
	 * random, only when it's restarted, but at least that helps a bit.
	 */
	if (mac_addr)
		get_random_mask_addr(frame->sa, mac_addr,
				     params->mac_addr_mask);
	else
		memcpy(frame->sa, vif->addr, ETH_ALEN);

	frame->frame_control = cpu_to_le16(IEEE80211_STYPE_PROBE_REQ);
	eth_broadcast_addr(frame->da);
	eth_broadcast_addr(frame->bssid);
	frame->seq_ctrl = 0;

	pos = frame->u.probe_req.variable;
	*pos++ = WLAN_EID_SSID;
	*pos++ = 0;

	params->preq.mac_header.offset = 0;
	params->preq.mac_header.len = cpu_to_le16(24 + 2);

	/* Insert ds parameter set element on 2.4 GHz band */
	newpos = iwl_mvm_copy_and_insert_ds_elem(mvm,
						 ies->ies[NL80211_BAND_2GHZ],
						 ies->len[NL80211_BAND_2GHZ],
						 pos);
	params->preq.band_data[0].offset = cpu_to_le16(pos - params->preq.buf);
	params->preq.band_data[0].len = cpu_to_le16(newpos - pos);
	pos = newpos;

	memcpy(pos, ies->ies[NL80211_BAND_5GHZ],
	       ies->len[NL80211_BAND_5GHZ]);
	params->preq.band_data[1].offset = cpu_to_le16(pos - params->preq.buf);
	params->preq.band_data[1].len =
		cpu_to_le16(ies->len[NL80211_BAND_5GHZ]);
	pos += ies->len[NL80211_BAND_5GHZ];

	memcpy(pos, ies->common_ies, ies->common_ie_len);
	params->preq.common_data.offset = cpu_to_le16(pos - params->preq.buf);

	if (iwl_mvm_rrm_scan_needed(mvm) &&
	    !fw_has_capa(&mvm->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT)) {
		iwl_mvm_add_tpc_report_ie(pos + ies->common_ie_len);
		params->preq.common_data.len = cpu_to_le16(ies->common_ie_len +
							   WFA_TPC_IE_LEN);
	} else {
		params->preq.common_data.len = cpu_to_le16(ies->common_ie_len);
	}
}

static void iwl_mvm_scan_lmac_dwell(struct iwl_mvm *mvm,
				    struct iwl_scan_req_lmac *cmd,
				    struct iwl_mvm_scan_params *params)
{
	cmd->active_dwell = IWL_SCAN_DWELL_ACTIVE;
	cmd->passive_dwell = IWL_SCAN_DWELL_PASSIVE;
	cmd->fragmented_dwell = IWL_SCAN_DWELL_FRAGMENTED;
	cmd->extended_dwell = IWL_SCAN_DWELL_EXTENDED;
	cmd->max_out_time = cpu_to_le32(scan_timing[params->type].max_out_time);
	cmd->suspend_time = cpu_to_le32(scan_timing[params->type].suspend_time);
	cmd->scan_prio = cpu_to_le32(IWL_SCAN_PRIORITY_EXT_6);
}

static inline bool iwl_mvm_scan_fits(struct iwl_mvm *mvm, int n_ssids,
				     struct ieee80211_scan_ies *ies,
				     int n_channels)
{
	return ((n_ssids <= PROBE_OPTION_MAX) &&
		(n_channels <= mvm->fw->ucode_capa.n_scan_channels) &
		(ies->common_ie_len +
		 ies->len[NL80211_BAND_2GHZ] +
		 ies->len[NL80211_BAND_5GHZ] <=
		 iwl_mvm_max_scan_ie_fw_cmd_room(mvm)));
}

static inline bool iwl_mvm_scan_use_ebs(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif)
{
	const struct iwl_ucode_capabilities *capa = &mvm->fw->ucode_capa;
	bool low_latency;

	if (iwl_mvm_is_cdb_supported(mvm))
		low_latency = iwl_mvm_low_latency_band(mvm, NL80211_BAND_5GHZ);
	else
		low_latency = iwl_mvm_low_latency(mvm);

	/* We can only use EBS if:
	 *	1. the feature is supported;
	 *	2. the last EBS was successful;
	 *	3. if only single scan, the single scan EBS API is supported;
	 *	4. it's not a p2p find operation.
	 *	5. we are not in low latency mode,
	 *	   or if fragmented ebs is supported by the FW
	 */
	return ((capa->flags & IWL_UCODE_TLV_FLAGS_EBS_SUPPORT) &&
		mvm->last_ebs_successful && IWL_MVM_ENABLE_EBS &&
		vif->type != NL80211_IFTYPE_P2P_DEVICE &&
		(!low_latency || iwl_mvm_is_frag_ebs_supported(mvm)));
}

static inline bool iwl_mvm_is_regular_scan(struct iwl_mvm_scan_params *params)
{
	return params->n_scan_plans == 1 &&
		params->scan_plans[0].iterations == 1;
}

static bool iwl_mvm_is_scan_fragmented(enum iwl_mvm_scan_type type)
{
	return (type == IWL_SCAN_TYPE_FRAGMENTED ||
		type == IWL_SCAN_TYPE_FAST_BALANCE);
}

static int iwl_mvm_scan_lmac_flags(struct iwl_mvm *mvm,
				   struct iwl_mvm_scan_params *params,
				   struct ieee80211_vif *vif)
{
	int flags = 0;

	if (params->n_ssids == 0)
		flags |= IWL_MVM_LMAC_SCAN_FLAG_PASSIVE;

	if (params->n_ssids == 1 && params->ssids[0].ssid_len != 0)
		flags |= IWL_MVM_LMAC_SCAN_FLAG_PRE_CONNECTION;

	if (iwl_mvm_is_scan_fragmented(params->type))
		flags |= IWL_MVM_LMAC_SCAN_FLAG_FRAGMENTED;

	if (iwl_mvm_rrm_scan_needed(mvm) &&
	    fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT))
		flags |= IWL_MVM_LMAC_SCAN_FLAGS_RRM_ENABLED;

	if (params->pass_all)
		flags |= IWL_MVM_LMAC_SCAN_FLAG_PASS_ALL;
	else
		flags |= IWL_MVM_LMAC_SCAN_FLAG_MATCH;

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (mvm->scan_iter_notif_enabled)
		flags |= IWL_MVM_LMAC_SCAN_FLAG_ITER_COMPLETE;
#endif

	if (mvm->sched_scan_pass_all == SCHED_SCAN_PASS_ALL_ENABLED)
		flags |= IWL_MVM_LMAC_SCAN_FLAG_ITER_COMPLETE;

	if (iwl_mvm_is_regular_scan(params) &&
	    vif->type != NL80211_IFTYPE_P2P_DEVICE &&
	    !iwl_mvm_is_scan_fragmented(params->type))
		flags |= IWL_MVM_LMAC_SCAN_FLAG_EXTENDED_DWELL;

	return flags;
}

static int iwl_mvm_scan_lmac(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     struct iwl_mvm_scan_params *params)
{
	struct iwl_scan_req_lmac *cmd = mvm->scan_cmd;
	struct iwl_scan_probe_req *preq =
		(void *)(cmd->data + sizeof(struct iwl_scan_channel_cfg_lmac) *
			 mvm->fw->ucode_capa.n_scan_channels);
	u32 ssid_bitmap = 0;
	int i;

	lockdep_assert_held(&mvm->mutex);

	memset(cmd, 0, ksize(cmd));

	if (WARN_ON(params->n_scan_plans > IWL_MAX_SCHED_SCAN_PLANS))
		return -EINVAL;

	iwl_mvm_scan_lmac_dwell(mvm, cmd, params);

	cmd->rx_chain_select = iwl_mvm_scan_rx_chain(mvm);
	cmd->iter_num = cpu_to_le32(1);
	cmd->n_channels = (u8)params->n_channels;

	cmd->delay = cpu_to_le32(params->delay);

	cmd->scan_flags = cpu_to_le32(iwl_mvm_scan_lmac_flags(mvm, params,
							      vif));

	cmd->flags = iwl_mvm_scan_rxon_flags(params->channels[0]->band);
	cmd->filter_flags = cpu_to_le32(MAC_FILTER_ACCEPT_GRP |
					MAC_FILTER_IN_BEACON);
	iwl_mvm_scan_fill_tx_cmd(mvm, cmd->tx_cmd, params->no_cck);
	iwl_scan_build_ssids(params, cmd->direct_scan, &ssid_bitmap);

	/* this API uses bits 1-20 instead of 0-19 */
	ssid_bitmap <<= 1;

	for (i = 0; i < params->n_scan_plans; i++) {
		struct cfg80211_sched_scan_plan *scan_plan =
			&params->scan_plans[i];

		cmd->schedule[i].delay =
			cpu_to_le16(scan_plan->interval);
		cmd->schedule[i].iterations = scan_plan->iterations;
		cmd->schedule[i].full_scan_mul = 1;
	}

	/*
	 * If the number of iterations of the last scan plan is set to
	 * zero, it should run infinitely. However, this is not always the case.
	 * For example, when regular scan is requested the driver sets one scan
	 * plan with one iteration.
	 */
	if (!cmd->schedule[i - 1].iterations)
		cmd->schedule[i - 1].iterations = 0xff;

	if (iwl_mvm_scan_use_ebs(mvm, vif)) {
		cmd->channel_opt[0].flags =
			cpu_to_le16(IWL_SCAN_CHANNEL_FLAG_EBS |
				    IWL_SCAN_CHANNEL_FLAG_EBS_ACCURATE |
				    IWL_SCAN_CHANNEL_FLAG_CACHE_ADD);
		cmd->channel_opt[0].non_ebs_ratio =
			cpu_to_le16(IWL_DENSE_EBS_SCAN_RATIO);
		cmd->channel_opt[1].flags =
			cpu_to_le16(IWL_SCAN_CHANNEL_FLAG_EBS |
				    IWL_SCAN_CHANNEL_FLAG_EBS_ACCURATE |
				    IWL_SCAN_CHANNEL_FLAG_CACHE_ADD);
		cmd->channel_opt[1].non_ebs_ratio =
			cpu_to_le16(IWL_SPARSE_EBS_SCAN_RATIO);
	}

	iwl_mvm_lmac_scan_cfg_channels(mvm, params->channels,
				       params->n_channels, ssid_bitmap, cmd);

	*preq = params->preq;

	return 0;
}

static int rate_to_scan_rate_flag(unsigned int rate)
{
	static const int rate_to_scan_rate[IWL_RATE_COUNT] = {
		[IWL_RATE_1M_INDEX]	= SCAN_CONFIG_RATE_1M,
		[IWL_RATE_2M_INDEX]	= SCAN_CONFIG_RATE_2M,
		[IWL_RATE_5M_INDEX]	= SCAN_CONFIG_RATE_5M,
		[IWL_RATE_11M_INDEX]	= SCAN_CONFIG_RATE_11M,
		[IWL_RATE_6M_INDEX]	= SCAN_CONFIG_RATE_6M,
		[IWL_RATE_9M_INDEX]	= SCAN_CONFIG_RATE_9M,
		[IWL_RATE_12M_INDEX]	= SCAN_CONFIG_RATE_12M,
		[IWL_RATE_18M_INDEX]	= SCAN_CONFIG_RATE_18M,
		[IWL_RATE_24M_INDEX]	= SCAN_CONFIG_RATE_24M,
		[IWL_RATE_36M_INDEX]	= SCAN_CONFIG_RATE_36M,
		[IWL_RATE_48M_INDEX]	= SCAN_CONFIG_RATE_48M,
		[IWL_RATE_54M_INDEX]	= SCAN_CONFIG_RATE_54M,
	};

	return rate_to_scan_rate[rate];
}

static __le32 iwl_mvm_scan_config_rates(struct iwl_mvm *mvm)
{
	struct ieee80211_supported_band *band;
	unsigned int rates = 0;
	int i;

	band = &mvm->nvm_data->bands[NL80211_BAND_2GHZ];
	for (i = 0; i < band->n_bitrates; i++)
		rates |= rate_to_scan_rate_flag(band->bitrates[i].hw_value);
	band = &mvm->nvm_data->bands[NL80211_BAND_5GHZ];
	for (i = 0; i < band->n_bitrates; i++)
		rates |= rate_to_scan_rate_flag(band->bitrates[i].hw_value);

	/* Set both basic rates and supported rates */
	rates |= SCAN_CONFIG_SUPPORTED_RATE(rates);

	return cpu_to_le32(rates);
}

static void iwl_mvm_fill_scan_dwell(struct iwl_mvm *mvm,
				    struct iwl_scan_dwell *dwell)
{
	dwell->active = IWL_SCAN_DWELL_ACTIVE;
	dwell->passive = IWL_SCAN_DWELL_PASSIVE;
	dwell->fragmented = IWL_SCAN_DWELL_FRAGMENTED;
	dwell->extended = IWL_SCAN_DWELL_EXTENDED;
}

static void iwl_mvm_fill_channels(struct iwl_mvm *mvm, u8 *channels,
				  u32 max_channels)
{
	struct ieee80211_supported_band *band;
	int i, j = 0;

	band = &mvm->nvm_data->bands[NL80211_BAND_2GHZ];
	for (i = 0; i < band->n_channels && j < max_channels; i++, j++)
		channels[j] = band->channels[i].hw_value;
	band = &mvm->nvm_data->bands[NL80211_BAND_5GHZ];
	for (i = 0; i < band->n_channels && j < max_channels; i++, j++)
		channels[j] = band->channels[i].hw_value;
}

static void iwl_mvm_fill_scan_config_v1(struct iwl_mvm *mvm, void *config,
					u32 flags, u8 channel_flags,
					u32 max_channels)
{
	enum iwl_mvm_scan_type type = iwl_mvm_get_scan_type(mvm, NULL);
	struct iwl_scan_config_v1 *cfg = config;

	cfg->flags = cpu_to_le32(flags);
	cfg->tx_chains = cpu_to_le32(iwl_mvm_get_valid_tx_ant(mvm));
	cfg->rx_chains = cpu_to_le32(iwl_mvm_scan_rx_ant(mvm));
	cfg->legacy_rates = iwl_mvm_scan_config_rates(mvm);
	cfg->out_of_channel_time = cpu_to_le32(scan_timing[type].max_out_time);
	cfg->suspend_time = cpu_to_le32(scan_timing[type].suspend_time);

	iwl_mvm_fill_scan_dwell(mvm, &cfg->dwell);

	memcpy(&cfg->mac_addr, &mvm->addresses[0].addr, ETH_ALEN);

	cfg->bcast_sta_id = mvm->aux_sta.sta_id;
	cfg->channel_flags = channel_flags;

	iwl_mvm_fill_channels(mvm, cfg->channel_array, max_channels);
}

static void iwl_mvm_fill_scan_config(struct iwl_mvm *mvm, void *config,
				     u32 flags, u8 channel_flags,
				     u32 max_channels)
{
	struct iwl_scan_config *cfg = config;

	cfg->flags = cpu_to_le32(flags);
	cfg->tx_chains = cpu_to_le32(iwl_mvm_get_valid_tx_ant(mvm));
	cfg->rx_chains = cpu_to_le32(iwl_mvm_scan_rx_ant(mvm));
	cfg->legacy_rates = iwl_mvm_scan_config_rates(mvm);

	if (iwl_mvm_is_cdb_supported(mvm)) {
		enum iwl_mvm_scan_type lb_type, hb_type;

		lb_type = iwl_mvm_get_scan_type_band(mvm, NULL,
						     NL80211_BAND_2GHZ);
		hb_type = iwl_mvm_get_scan_type_band(mvm, NULL,
						     NL80211_BAND_5GHZ);

		cfg->out_of_channel_time[SCAN_LB_LMAC_IDX] =
			cpu_to_le32(scan_timing[lb_type].max_out_time);
		cfg->suspend_time[SCAN_LB_LMAC_IDX] =
			cpu_to_le32(scan_timing[lb_type].suspend_time);

		cfg->out_of_channel_time[SCAN_HB_LMAC_IDX] =
			cpu_to_le32(scan_timing[hb_type].max_out_time);
		cfg->suspend_time[SCAN_HB_LMAC_IDX] =
			cpu_to_le32(scan_timing[hb_type].suspend_time);
	} else {
		enum iwl_mvm_scan_type type =
			iwl_mvm_get_scan_type(mvm, NULL);

		cfg->out_of_channel_time[SCAN_LB_LMAC_IDX] =
			cpu_to_le32(scan_timing[type].max_out_time);
		cfg->suspend_time[SCAN_LB_LMAC_IDX] =
			cpu_to_le32(scan_timing[type].suspend_time);
	}

	iwl_mvm_fill_scan_dwell(mvm, &cfg->dwell);

	memcpy(&cfg->mac_addr, &mvm->addresses[0].addr, ETH_ALEN);

	cfg->bcast_sta_id = mvm->aux_sta.sta_id;
	cfg->channel_flags = channel_flags;

	iwl_mvm_fill_channels(mvm, cfg->channel_array, max_channels);
}

int iwl_mvm_config_scan(struct iwl_mvm *mvm)
{
	void *cfg;
	int ret, cmd_size;
	struct iwl_host_cmd cmd = {
		.id = iwl_cmd_id(SCAN_CFG_CMD, IWL_ALWAYS_LONG_GROUP, 0),
	};
	enum iwl_mvm_scan_type type;
	enum iwl_mvm_scan_type hb_type = IWL_SCAN_TYPE_NOT_SET;
	int num_channels =
		mvm->nvm_data->bands[NL80211_BAND_2GHZ].n_channels +
		mvm->nvm_data->bands[NL80211_BAND_5GHZ].n_channels;
	u32 flags;
	u8 channel_flags;

	if (WARN_ON(num_channels > mvm->fw->ucode_capa.n_scan_channels))
		num_channels = mvm->fw->ucode_capa.n_scan_channels;

	if (iwl_mvm_is_cdb_supported(mvm)) {
		type = iwl_mvm_get_scan_type_band(mvm, NULL,
						  NL80211_BAND_2GHZ);
		hb_type = iwl_mvm_get_scan_type_band(mvm, NULL,
						     NL80211_BAND_5GHZ);
		if (type == mvm->scan_type && hb_type == mvm->hb_scan_type)
			return 0;
	} else {
		type = iwl_mvm_get_scan_type(mvm, NULL);
		if (type == mvm->scan_type)
			return 0;
	}

	if (iwl_mvm_cdb_scan_api(mvm))
		cmd_size = sizeof(struct iwl_scan_config);
	else
		cmd_size = sizeof(struct iwl_scan_config_v1);
	cmd_size += mvm->fw->ucode_capa.n_scan_channels;

	cfg = kzalloc(cmd_size, GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	flags = SCAN_CONFIG_FLAG_ACTIVATE |
		 SCAN_CONFIG_FLAG_ALLOW_CHUB_REQS |
		 SCAN_CONFIG_FLAG_SET_TX_CHAINS |
		 SCAN_CONFIG_FLAG_SET_RX_CHAINS |
		 SCAN_CONFIG_FLAG_SET_AUX_STA_ID |
		 SCAN_CONFIG_FLAG_SET_ALL_TIMES |
		 SCAN_CONFIG_FLAG_SET_LEGACY_RATES |
		 SCAN_CONFIG_FLAG_SET_MAC_ADDR |
		 SCAN_CONFIG_FLAG_SET_CHANNEL_FLAGS |
		 SCAN_CONFIG_N_CHANNELS(num_channels) |
		 (iwl_mvm_is_scan_fragmented(type) ?
		  SCAN_CONFIG_FLAG_SET_FRAGMENTED :
		  SCAN_CONFIG_FLAG_CLEAR_FRAGMENTED);

	channel_flags = IWL_CHANNEL_FLAG_EBS |
			IWL_CHANNEL_FLAG_ACCURATE_EBS |
			IWL_CHANNEL_FLAG_EBS_ADD |
			IWL_CHANNEL_FLAG_PRE_SCAN_PASSIVE2ACTIVE;

	/*
	 * Check for fragmented scan on LMAC2 - high band.
	 * LMAC1 - low band is checked above.
	 */
	if (iwl_mvm_cdb_scan_api(mvm)) {
		if (iwl_mvm_is_cdb_supported(mvm))
			flags |= (iwl_mvm_is_scan_fragmented(hb_type)) ?
				 SCAN_CONFIG_FLAG_SET_LMAC2_FRAGMENTED :
				 SCAN_CONFIG_FLAG_CLEAR_LMAC2_FRAGMENTED;
		iwl_mvm_fill_scan_config(mvm, cfg, flags, channel_flags,
					 num_channels);
	} else {
		iwl_mvm_fill_scan_config_v1(mvm, cfg, flags, channel_flags,
					    num_channels);
	}

	cmd.data[0] = cfg;
	cmd.len[0] = cmd_size;
	cmd.dataflags[0] = IWL_HCMD_DFL_NOCOPY;

	IWL_DEBUG_SCAN(mvm, "Sending UMAC scan config\n");

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (!ret) {
		mvm->scan_type = type;
		mvm->hb_scan_type = hb_type;
	}

	kfree(cfg);
	return ret;
}

static int iwl_mvm_scan_uid_by_status(struct iwl_mvm *mvm, int status)
{
	int i;

	for (i = 0; i < mvm->max_scans; i++)
		if (mvm->scan_uid_status[i] == status)
			return i;

	return -ENOENT;
}

static void iwl_mvm_scan_umac_dwell(struct iwl_mvm *mvm,
				    struct iwl_scan_req_umac *cmd,
				    struct iwl_mvm_scan_params *params)
{
	struct iwl_mvm_scan_timing_params *timing, *hb_timing;
	u8 active_dwell, passive_dwell;

	timing = &scan_timing[params->type];
	active_dwell = params->measurement_dwell ?
		params->measurement_dwell : IWL_SCAN_DWELL_ACTIVE;
	passive_dwell = params->measurement_dwell ?
		params->measurement_dwell : IWL_SCAN_DWELL_PASSIVE;

	if (iwl_mvm_is_adaptive_dwell_supported(mvm)) {
		cmd->v7.adwell_default_n_aps_social =
			IWL_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL;
		cmd->v7.adwell_default_n_aps =
			IWL_SCAN_ADWELL_DEFAULT_N_APS;

		/* if custom max budget was configured with debugfs */
		if (IWL_MVM_ADWELL_MAX_BUDGET)
			cmd->v7.adwell_max_budget =
				cpu_to_le16(IWL_MVM_ADWELL_MAX_BUDGET);
		else if (params->ssids && params->ssids[0].ssid_len)
			cmd->v7.adwell_max_budget =
				cpu_to_le16(IWL_SCAN_ADWELL_MAX_BUDGET_DIRECTED_SCAN);
		else
			cmd->v7.adwell_max_budget =
				cpu_to_le16(IWL_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN);

		cmd->v7.scan_priority = cpu_to_le32(IWL_SCAN_PRIORITY_EXT_6);
		cmd->v7.max_out_time[SCAN_LB_LMAC_IDX] =
			cpu_to_le32(timing->max_out_time);
		cmd->v7.suspend_time[SCAN_LB_LMAC_IDX] =
			cpu_to_le32(timing->suspend_time);

		if (iwl_mvm_is_cdb_supported(mvm)) {
			hb_timing = &scan_timing[params->hb_type];

			cmd->v7.max_out_time[SCAN_HB_LMAC_IDX] =
				cpu_to_le32(hb_timing->max_out_time);
			cmd->v7.suspend_time[SCAN_HB_LMAC_IDX] =
				cpu_to_le32(hb_timing->suspend_time);
		}

		if (!iwl_mvm_is_adaptive_dwell_v2_supported(mvm)) {
			cmd->v7.active_dwell = active_dwell;
			cmd->v7.passive_dwell = passive_dwell;
			cmd->v7.fragmented_dwell = IWL_SCAN_DWELL_FRAGMENTED;
		} else {
			cmd->v8.active_dwell[SCAN_LB_LMAC_IDX] = active_dwell;
			cmd->v8.passive_dwell[SCAN_LB_LMAC_IDX] = passive_dwell;
			if (iwl_mvm_is_cdb_supported(mvm)) {
				cmd->v8.active_dwell[SCAN_HB_LMAC_IDX] =
					active_dwell;
				cmd->v8.passive_dwell[SCAN_HB_LMAC_IDX] =
					passive_dwell;
			}
		}
	} else {
		cmd->v1.extended_dwell = params->measurement_dwell ?
			params->measurement_dwell : IWL_SCAN_DWELL_EXTENDED;
		cmd->v1.active_dwell = active_dwell;
		cmd->v1.passive_dwell = passive_dwell;
		cmd->v1.fragmented_dwell = IWL_SCAN_DWELL_FRAGMENTED;

		if (iwl_mvm_is_cdb_supported(mvm)) {
			hb_timing = &scan_timing[params->hb_type];

			cmd->v6.max_out_time[SCAN_HB_LMAC_IDX] =
					cpu_to_le32(hb_timing->max_out_time);
			cmd->v6.suspend_time[SCAN_HB_LMAC_IDX] =
					cpu_to_le32(hb_timing->suspend_time);
		}

		if (iwl_mvm_cdb_scan_api(mvm)) {
			cmd->v6.scan_priority =
				cpu_to_le32(IWL_SCAN_PRIORITY_EXT_6);
			cmd->v6.max_out_time[SCAN_LB_LMAC_IDX] =
				cpu_to_le32(timing->max_out_time);
			cmd->v6.suspend_time[SCAN_LB_LMAC_IDX] =
				cpu_to_le32(timing->suspend_time);
		} else {
			cmd->v1.scan_priority =
				cpu_to_le32(IWL_SCAN_PRIORITY_EXT_6);
			cmd->v1.max_out_time =
				cpu_to_le32(timing->max_out_time);
			cmd->v1.suspend_time =
				cpu_to_le32(timing->suspend_time);
		}
	}

	if (iwl_mvm_is_regular_scan(params))
		cmd->ooc_priority = cpu_to_le32(IWL_SCAN_PRIORITY_EXT_6);
	else
		cmd->ooc_priority = cpu_to_le32(IWL_SCAN_PRIORITY_EXT_2);
}

static void
iwl_mvm_umac_scan_cfg_channels(struct iwl_mvm *mvm,
			       struct ieee80211_channel **channels,
			       int n_channels, u32 ssid_bitmap,
			       struct iwl_scan_channel_cfg_umac *channel_cfg)
{
	int i;

	for (i = 0; i < n_channels; i++) {
		channel_cfg[i].flags = cpu_to_le32(ssid_bitmap);
		channel_cfg[i].channel_num = channels[i]->hw_value;
		channel_cfg[i].iter_count = 1;
		channel_cfg[i].iter_interval = 0;
	}
}

static u16 iwl_mvm_scan_umac_flags(struct iwl_mvm *mvm,
				   struct iwl_mvm_scan_params *params,
				   struct ieee80211_vif *vif)
{
	u16 flags = 0;

	if (params->n_ssids == 0)
		flags = IWL_UMAC_SCAN_GEN_FLAGS_PASSIVE;

	if (params->n_ssids == 1 && params->ssids[0].ssid_len != 0)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_PRE_CONNECT;

	if (iwl_mvm_is_scan_fragmented(params->type))
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_FRAGMENTED;

	if (iwl_mvm_is_cdb_supported(mvm) &&
	    iwl_mvm_is_scan_fragmented(params->hb_type))
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_LMAC2_FRAGMENTED;

	if (iwl_mvm_rrm_scan_needed(mvm) &&
	    fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT))
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_RRM_ENABLED;

	if (params->pass_all)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_PASS_ALL;
	else
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_MATCH;

	if (!iwl_mvm_is_regular_scan(params))
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_PERIODIC;

	if (params->measurement_dwell)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_ITER_COMPLETE;

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (mvm->scan_iter_notif_enabled)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_ITER_COMPLETE;
#endif

	if (mvm->sched_scan_pass_all == SCHED_SCAN_PASS_ALL_ENABLED)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_ITER_COMPLETE;

	if (iwl_mvm_is_adaptive_dwell_supported(mvm) && IWL_MVM_ADWELL_ENABLE &&
	    vif->type != NL80211_IFTYPE_P2P_DEVICE)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_ADAPTIVE_DWELL;

	/*
	 * Extended dwell is relevant only for low band to start with, as it is
	 * being used for social channles only (1, 6, 11), so we can check
	 * only scan type on low band also for CDB.
	 */
	if (iwl_mvm_is_regular_scan(params) &&
	    vif->type != NL80211_IFTYPE_P2P_DEVICE &&
	    !iwl_mvm_is_scan_fragmented(params->type) &&
	    !iwl_mvm_is_adaptive_dwell_supported(mvm) &&
	    !iwl_mvm_is_oce_supported(mvm))
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_EXTENDED_DWELL;

	if (iwl_mvm_is_oce_supported(mvm)) {
		if ((params->flags &
		     NL80211_SCAN_FLAG_OCE_PROBE_REQ_HIGH_TX_RATE))
			flags |= IWL_UMAC_SCAN_GEN_FLAGS_PROB_REQ_HIGH_TX_RATE;
		/* Since IWL_UMAC_SCAN_GEN_FLAGS_EXTENDED_DWELL and
		 * NL80211_SCAN_FLAG_OCE_PROBE_REQ_DEFERRAL_SUPPRESSION shares
		 * the same bit, we need to make sure that we use this bit here
		 * only when IWL_UMAC_SCAN_GEN_FLAGS_EXTENDED_DWELL cannot be
		 * used. */
		if ((params->flags &
		     NL80211_SCAN_FLAG_OCE_PROBE_REQ_DEFERRAL_SUPPRESSION) &&
		     !WARN_ON_ONCE(!iwl_mvm_is_adaptive_dwell_supported(mvm)))
			flags |= IWL_UMAC_SCAN_GEN_FLAGS_PROB_REQ_DEFER_SUPP;
		if ((params->flags & NL80211_SCAN_FLAG_FILS_MAX_CHANNEL_TIME))
			flags |= IWL_UMAC_SCAN_GEN_FLAGS_MAX_CHNL_TIME;
	}

	return flags;
}

static int iwl_mvm_scan_umac(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     struct iwl_mvm_scan_params *params,
			     int type)
{
	struct iwl_scan_req_umac *cmd = mvm->scan_cmd;
	struct iwl_scan_umac_chan_param *chan_param;
	void *cmd_data = iwl_mvm_get_scan_req_umac_data(mvm);
	struct iwl_scan_req_umac_tail *sec_part = cmd_data +
		sizeof(struct iwl_scan_channel_cfg_umac) *
			mvm->fw->ucode_capa.n_scan_channels;
	int uid, i;
	u32 ssid_bitmap = 0;
	u8 channel_flags = 0;
	u16 gen_flags;
	struct iwl_mvm_vif *scan_vif = iwl_mvm_vif_from_mac80211(vif);

	chan_param = iwl_mvm_get_scan_req_umac_channel(mvm);

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON(params->n_scan_plans > IWL_MAX_SCHED_SCAN_PLANS))
		return -EINVAL;

	uid = iwl_mvm_scan_uid_by_status(mvm, 0);
	if (uid < 0)
		return uid;

	memset(cmd, 0, ksize(cmd));

	iwl_mvm_scan_umac_dwell(mvm, cmd, params);

	mvm->scan_uid_status[uid] = type;

	cmd->uid = cpu_to_le32(uid);
	gen_flags = iwl_mvm_scan_umac_flags(mvm, params, vif);
	cmd->general_flags = cpu_to_le16(gen_flags);
	if (iwl_mvm_is_adaptive_dwell_v2_supported(mvm)) {
		if (gen_flags & IWL_UMAC_SCAN_GEN_FLAGS_FRAGMENTED)
			cmd->v8.num_of_fragments[SCAN_LB_LMAC_IDX] =
							IWL_SCAN_NUM_OF_FRAGS;
		if (gen_flags & IWL_UMAC_SCAN_GEN_FLAGS_LMAC2_FRAGMENTED)
			cmd->v8.num_of_fragments[SCAN_HB_LMAC_IDX] =
							IWL_SCAN_NUM_OF_FRAGS;

		cmd->v8.general_flags2 =
			IWL_UMAC_SCAN_GEN_FLAGS2_ALLOW_CHNL_REORDER;
	}

	cmd->scan_start_mac_id = scan_vif->id;

	if (type == IWL_MVM_SCAN_SCHED || type == IWL_MVM_SCAN_NETDETECT)
		cmd->flags = cpu_to_le32(IWL_UMAC_SCAN_FLAG_PREEMPTIVE);

	if (iwl_mvm_scan_use_ebs(mvm, vif)) {
		channel_flags = IWL_SCAN_CHANNEL_FLAG_EBS |
				IWL_SCAN_CHANNEL_FLAG_EBS_ACCURATE |
				IWL_SCAN_CHANNEL_FLAG_CACHE_ADD;

		/* set fragmented ebs for fragmented scan on HB channels */
		if (iwl_mvm_is_frag_ebs_supported(mvm)) {
			if (gen_flags &
			    IWL_UMAC_SCAN_GEN_FLAGS_LMAC2_FRAGMENTED ||
			    (!iwl_mvm_is_cdb_supported(mvm) &&
			     gen_flags & IWL_UMAC_SCAN_GEN_FLAGS_FRAGMENTED))
				channel_flags |= IWL_SCAN_CHANNEL_FLAG_EBS_FRAG;
		}
	}

	chan_param->flags = channel_flags;
	chan_param->count = params->n_channels;

	iwl_scan_build_ssids(params, sec_part->direct_scan, &ssid_bitmap);

	iwl_mvm_umac_scan_cfg_channels(mvm, params->channels,
				       params->n_channels, ssid_bitmap,
				       cmd_data);

	for (i = 0; i < params->n_scan_plans; i++) {
		struct cfg80211_sched_scan_plan *scan_plan =
			&params->scan_plans[i];

		sec_part->schedule[i].iter_count = scan_plan->iterations;
		sec_part->schedule[i].interval =
			cpu_to_le16(scan_plan->interval);
	}

	/*
	 * If the number of iterations of the last scan plan is set to
	 * zero, it should run infinitely. However, this is not always the case.
	 * For example, when regular scan is requested the driver sets one scan
	 * plan with one iteration.
	 */
	if (!sec_part->schedule[i - 1].iter_count)
		sec_part->schedule[i - 1].iter_count = 0xff;

	sec_part->delay = cpu_to_le16(params->delay);
	sec_part->preq = params->preq;

	return 0;
}

static int iwl_mvm_num_scans(struct iwl_mvm *mvm)
{
	return hweight32(mvm->scan_status & IWL_MVM_SCAN_MASK);
}

static int iwl_mvm_check_running_scans(struct iwl_mvm *mvm, int type)
{
	bool unified_image = fw_has_capa(&mvm->fw->ucode_capa,
					 IWL_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG);

	/* This looks a bit arbitrary, but the idea is that if we run
	 * out of possible simultaneous scans and the userspace is
	 * trying to run a scan type that is already running, we
	 * return -EBUSY.  But if the userspace wants to start a
	 * different type of scan, we stop the opposite type to make
	 * space for the new request.  The reason is backwards
	 * compatibility with old wpa_supplicant that wouldn't stop a
	 * scheduled scan before starting a normal scan.
	 */

	/* FW supports only a single periodic scan */
	if ((type == IWL_MVM_SCAN_SCHED || type == IWL_MVM_SCAN_NETDETECT) &&
	    mvm->scan_status & (IWL_MVM_SCAN_SCHED | IWL_MVM_SCAN_NETDETECT))
		return -EBUSY;

	if (iwl_mvm_num_scans(mvm) < mvm->max_scans)
		return 0;

	/* Use a switch, even though this is a bitmask, so that more
	 * than one bits set will fall in default and we will warn.
	 */
	switch (type) {
	case IWL_MVM_SCAN_REGULAR:
		if (mvm->scan_status & IWL_MVM_SCAN_REGULAR_MASK)
			return -EBUSY;
		return iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_SCHED, true);
	case IWL_MVM_SCAN_SCHED:
		if (mvm->scan_status & IWL_MVM_SCAN_SCHED_MASK)
			return -EBUSY;
		return iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_REGULAR, true);
	case IWL_MVM_SCAN_NETDETECT:
		/* For non-unified images, there's no need to stop
		 * anything for net-detect since the firmware is
		 * restarted anyway.  This way, any sched scans that
		 * were running will be restarted when we resume.
		 */
		if (!unified_image)
			return 0;

		/* If this is a unified image and we ran out of scans,
		 * we need to stop something.  Prefer stopping regular
		 * scans, because the results are useless at this
		 * point, and we should be able to keep running
		 * another scheduled scan while suspended.
		 */
		if (mvm->scan_status & IWL_MVM_SCAN_REGULAR_MASK)
			return iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_REGULAR,
						 true);
		if (mvm->scan_status & IWL_MVM_SCAN_SCHED_MASK)
			return iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_SCHED,
						 true);
		/* Something is wrong if no scan was running but we
		 * ran out of scans.
		 */
		/* fall through */
	default:
		WARN_ON(1);
		break;
	}

	return -EIO;
}

#define SCAN_TIMEOUT 20000

void iwl_mvm_scan_timeout_wk(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct iwl_mvm *mvm = container_of(delayed_work, struct iwl_mvm,
					   scan_timeout_dwork);

	IWL_ERR(mvm, "regular scan timed out\n");

	iwl_force_nmi(mvm->trans);
}

static void iwl_mvm_fill_scan_type(struct iwl_mvm *mvm,
				   struct iwl_mvm_scan_params *params,
				   struct ieee80211_vif *vif)
{
	if (iwl_mvm_is_cdb_supported(mvm)) {
		params->type =
			iwl_mvm_get_scan_type_band(mvm, vif,
						   NL80211_BAND_2GHZ);
		params->hb_type =
			iwl_mvm_get_scan_type_band(mvm, vif,
						   NL80211_BAND_5GHZ);
	} else {
		params->type = iwl_mvm_get_scan_type(mvm, vif);
	}
}

int iwl_mvm_reg_scan_start(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			   struct cfg80211_scan_request *req,
			   struct ieee80211_scan_ies *ies)
{
	struct iwl_host_cmd hcmd = {
		.len = { iwl_mvm_scan_size(mvm), },
		.data = { mvm->scan_cmd, },
		.dataflags = { IWL_HCMD_DFL_NOCOPY, },
	};
	struct iwl_mvm_scan_params params = {};
	int ret;
	struct cfg80211_sched_scan_plan scan_plan = { .iterations = 1 };

	lockdep_assert_held(&mvm->mutex);

	if (iwl_mvm_is_lar_supported(mvm) && !mvm->lar_regdom_set) {
		IWL_ERR(mvm, "scan while LAR regdomain is not set\n");
		return -EBUSY;
	}

	ret = iwl_mvm_check_running_scans(mvm, IWL_MVM_SCAN_REGULAR);
	if (ret)
		return ret;

	/* we should have failed registration if scan_cmd was NULL */
	if (WARN_ON(!mvm->scan_cmd))
		return -ENOMEM;

	if (!iwl_mvm_scan_fits(mvm, req->n_ssids, ies, req->n_channels))
		return -ENOBUFS;

	params.n_ssids = req->n_ssids;
	params.flags = req->flags;
	params.n_channels = req->n_channels;
	params.delay = 0;
	params.ssids = req->ssids;
	params.channels = req->channels;
	params.mac_addr = req->mac_addr;
	params.mac_addr_mask = req->mac_addr_mask;
	params.no_cck = req->no_cck;
	params.pass_all = true;
	params.n_match_sets = 0;
	params.match_sets = NULL;

	params.scan_plans = &scan_plan;
	params.n_scan_plans = 1;

	iwl_mvm_fill_scan_type(mvm, &params, vif);

	ret = iwl_mvm_get_measurement_dwell(mvm, req, &params);
	if (ret < 0)
		return ret;

	params.measurement_dwell = ret;

	iwl_mvm_build_scan_probe(mvm, vif, ies, &params);

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN)) {
		hcmd.id = iwl_cmd_id(SCAN_REQ_UMAC, IWL_ALWAYS_LONG_GROUP, 0);
		ret = iwl_mvm_scan_umac(mvm, vif, &params,
					IWL_MVM_SCAN_REGULAR);
	} else {
		hcmd.id = SCAN_OFFLOAD_REQUEST_CMD;
		ret = iwl_mvm_scan_lmac(mvm, vif, &params);
	}

	if (ret)
		return ret;

	iwl_mvm_pause_tcm(mvm, false);

	ret = iwl_mvm_send_cmd(mvm, &hcmd);
	if (ret) {
		/* If the scan failed, it usually means that the FW was unable
		 * to allocate the time events. Warn on it, but maybe we
		 * should try to send the command again with different params.
		 */
		IWL_ERR(mvm, "Scan failed! ret %d\n", ret);
		iwl_mvm_resume_tcm(mvm);
		return ret;
	}

	IWL_DEBUG_SCAN(mvm, "Scan request was sent successfully\n");
	mvm->scan_status |= IWL_MVM_SCAN_REGULAR;
	mvm->scan_vif = iwl_mvm_vif_from_mac80211(vif);
	iwl_mvm_ref(mvm, IWL_MVM_REF_SCAN);

	schedule_delayed_work(&mvm->scan_timeout_dwork,
			      msecs_to_jiffies(SCAN_TIMEOUT));

	return 0;
}

int iwl_mvm_sched_scan_start(struct iwl_mvm *mvm,
			     struct ieee80211_vif *vif,
			     struct cfg80211_sched_scan_request *req,
			     struct ieee80211_scan_ies *ies,
			     int type)
{
	struct iwl_host_cmd hcmd = {
		.len = { iwl_mvm_scan_size(mvm), },
		.data = { mvm->scan_cmd, },
		.dataflags = { IWL_HCMD_DFL_NOCOPY, },
	};
	struct iwl_mvm_scan_params params = {};
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (iwl_mvm_is_lar_supported(mvm) && !mvm->lar_regdom_set) {
		IWL_ERR(mvm, "sched-scan while LAR regdomain is not set\n");
		return -EBUSY;
	}

	ret = iwl_mvm_check_running_scans(mvm, type);
	if (ret)
		return ret;

	/* we should have failed registration if scan_cmd was NULL */
	if (WARN_ON(!mvm->scan_cmd))
		return -ENOMEM;

	if (!iwl_mvm_scan_fits(mvm, req->n_ssids, ies, req->n_channels))
		return -ENOBUFS;

	params.n_ssids = req->n_ssids;
	params.flags = req->flags;
	params.n_channels = req->n_channels;
	params.ssids = req->ssids;
	params.channels = req->channels;
	params.mac_addr = req->mac_addr;
	params.mac_addr_mask = req->mac_addr_mask;
	params.no_cck = false;
	params.pass_all =  iwl_mvm_scan_pass_all(mvm, req);
	params.n_match_sets = req->n_match_sets;
	params.match_sets = req->match_sets;
	if (!req->n_scan_plans)
		return -EINVAL;

	params.n_scan_plans = req->n_scan_plans;
	params.scan_plans = req->scan_plans;

	iwl_mvm_fill_scan_type(mvm, &params, vif);

	/* In theory, LMAC scans can handle a 32-bit delay, but since
	 * waiting for over 18 hours to start the scan is a bit silly
	 * and to keep it aligned with UMAC scans (which only support
	 * 16-bit delays), trim it down to 16-bits.
	 */
	if (req->delay > U16_MAX) {
		IWL_DEBUG_SCAN(mvm,
			       "delay value is > 16-bits, set to max possible\n");
		params.delay = U16_MAX;
	} else {
		params.delay = req->delay;
	}

	ret = iwl_mvm_config_sched_scan_profiles(mvm, req);
	if (ret)
		return ret;

	iwl_mvm_build_scan_probe(mvm, vif, ies, &params);

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN)) {
		hcmd.id = iwl_cmd_id(SCAN_REQ_UMAC, IWL_ALWAYS_LONG_GROUP, 0);
		ret = iwl_mvm_scan_umac(mvm, vif, &params, type);
	} else {
		hcmd.id = SCAN_OFFLOAD_REQUEST_CMD;
		ret = iwl_mvm_scan_lmac(mvm, vif, &params);
	}

	if (ret)
		return ret;

	ret = iwl_mvm_send_cmd(mvm, &hcmd);
	if (!ret) {
		IWL_DEBUG_SCAN(mvm,
			       "Sched scan request was sent successfully\n");
		mvm->scan_status |= type;
	} else {
		/* If the scan failed, it usually means that the FW was unable
		 * to allocate the time events. Warn on it, but maybe we
		 * should try to send the command again with different params.
		 */
		IWL_ERR(mvm, "Sched scan failed! ret %d\n", ret);
	}

	return ret;
}

void iwl_mvm_rx_umac_scan_complete_notif(struct iwl_mvm *mvm,
					 struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_umac_scan_complete *notif = (void *)pkt->data;
	u32 uid = __le32_to_cpu(notif->uid);
	bool aborted = (notif->status == IWL_SCAN_OFFLOAD_ABORTED);

	if (WARN_ON(!(mvm->scan_uid_status[uid] & mvm->scan_status)))
		return;

	/* if the scan is already stopping, we don't need to notify mac80211 */
	if (mvm->scan_uid_status[uid] == IWL_MVM_SCAN_REGULAR) {
		struct cfg80211_scan_info info = {
			.aborted = aborted,
			.scan_start_tsf = mvm->scan_start,
		};

		memcpy(info.tsf_bssid, mvm->scan_vif->bssid, ETH_ALEN);
		ieee80211_scan_completed(mvm->hw, &info);
		mvm->scan_vif = NULL;
		iwl_mvm_unref(mvm, IWL_MVM_REF_SCAN);
		cancel_delayed_work(&mvm->scan_timeout_dwork);
		iwl_mvm_resume_tcm(mvm);
	} else if (mvm->scan_uid_status[uid] == IWL_MVM_SCAN_SCHED) {
		ieee80211_sched_scan_stopped(mvm->hw);
		mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_DISABLED;
	}

	mvm->scan_status &= ~mvm->scan_uid_status[uid];
	IWL_DEBUG_SCAN(mvm,
		       "Scan completed, uid %u type %u, status %s, EBS status %s\n",
		       uid, mvm->scan_uid_status[uid],
		       notif->status == IWL_SCAN_OFFLOAD_COMPLETED ?
				"completed" : "aborted",
		       iwl_mvm_ebs_status_str(notif->ebs_status));
	IWL_DEBUG_SCAN(mvm,
		       "Last line %d, Last iteration %d, Time from last iteration %d\n",
		       notif->last_schedule, notif->last_iter,
		       __le32_to_cpu(notif->time_from_last_iter));

	if (notif->ebs_status != IWL_SCAN_EBS_SUCCESS &&
	    notif->ebs_status != IWL_SCAN_EBS_INACTIVE)
		mvm->last_ebs_successful = false;

	mvm->scan_uid_status[uid] = 0;

	iwl_fw_dbg_apply_point(&mvm->fwrt, IWL_FW_INI_APPLY_SCAN_COMPLETE);
}

void iwl_mvm_rx_umac_scan_iter_complete_notif(struct iwl_mvm *mvm,
					      struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_umac_scan_iter_complete_notif *notif = (void *)pkt->data;

	mvm->scan_start = le64_to_cpu(notif->start_tsf);

	IWL_DEBUG_SCAN(mvm,
		       "UMAC Scan iteration complete: status=0x%x scanned_channels=%d\n",
		       notif->status, notif->scanned_channels);

	if (mvm->sched_scan_pass_all == SCHED_SCAN_PASS_ALL_FOUND) {
		IWL_DEBUG_SCAN(mvm, "Pass all scheduled scan results found\n");
		ieee80211_sched_scan_results(mvm->hw);
		mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_ENABLED;
	}

	IWL_DEBUG_SCAN(mvm,
		       "UMAC Scan iteration complete: scan started at %llu (TSF)\n",
		       mvm->scan_start);
}

static int iwl_mvm_umac_scan_abort(struct iwl_mvm *mvm, int type)
{
	struct iwl_umac_scan_abort cmd = {};
	int uid, ret;

	lockdep_assert_held(&mvm->mutex);

	/* We should always get a valid index here, because we already
	 * checked that this type of scan was running in the generic
	 * code.
	 */
	uid = iwl_mvm_scan_uid_by_status(mvm, type);
	if (WARN_ON_ONCE(uid < 0))
		return uid;

	cmd.uid = cpu_to_le32(uid);

	IWL_DEBUG_SCAN(mvm, "Sending scan abort, uid %u\n", uid);

	ret = iwl_mvm_send_cmd_pdu(mvm,
				   iwl_cmd_id(SCAN_ABORT_UMAC,
					      IWL_ALWAYS_LONG_GROUP, 0),
				   0, sizeof(cmd), &cmd);
	if (!ret)
		mvm->scan_uid_status[uid] = type << IWL_MVM_SCAN_STOPPING_SHIFT;

	return ret;
}

static int iwl_mvm_scan_stop_wait(struct iwl_mvm *mvm, int type)
{
	struct iwl_notification_wait wait_scan_done;
	static const u16 scan_done_notif[] = { SCAN_COMPLETE_UMAC,
					      SCAN_OFFLOAD_COMPLETE, };
	int ret;

	lockdep_assert_held(&mvm->mutex);

	iwl_init_notification_wait(&mvm->notif_wait, &wait_scan_done,
				   scan_done_notif,
				   ARRAY_SIZE(scan_done_notif),
				   NULL, NULL);

	IWL_DEBUG_SCAN(mvm, "Preparing to stop scan, type %x\n", type);

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN))
		ret = iwl_mvm_umac_scan_abort(mvm, type);
	else
		ret = iwl_mvm_lmac_scan_abort(mvm);

	if (ret) {
		IWL_DEBUG_SCAN(mvm, "couldn't stop scan type %d\n", type);
		iwl_remove_notification(&mvm->notif_wait, &wait_scan_done);
		return ret;
	}

	return iwl_wait_notification(&mvm->notif_wait, &wait_scan_done,
				     1 * HZ);
}

int iwl_mvm_scan_size(struct iwl_mvm *mvm)
{
	int base_size = IWL_SCAN_REQ_UMAC_SIZE_V1;

	if (iwl_mvm_is_adaptive_dwell_v2_supported(mvm))
		base_size = IWL_SCAN_REQ_UMAC_SIZE_V8;
	else if (iwl_mvm_is_adaptive_dwell_supported(mvm))
		base_size = IWL_SCAN_REQ_UMAC_SIZE_V7;
	else if (iwl_mvm_cdb_scan_api(mvm))
		base_size = IWL_SCAN_REQ_UMAC_SIZE_V6;

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN))
		return base_size +
			sizeof(struct iwl_scan_channel_cfg_umac) *
				mvm->fw->ucode_capa.n_scan_channels +
			sizeof(struct iwl_scan_req_umac_tail);

	return sizeof(struct iwl_scan_req_lmac) +
		sizeof(struct iwl_scan_channel_cfg_lmac) *
		mvm->fw->ucode_capa.n_scan_channels +
		sizeof(struct iwl_scan_probe_req);
}

/*
 * This function is used in nic restart flow, to inform mac80211 about scans
 * that was aborted by restart flow or by an assert.
 */
void iwl_mvm_report_scan_aborted(struct iwl_mvm *mvm)
{
	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN)) {
		int uid, i;

		uid = iwl_mvm_scan_uid_by_status(mvm, IWL_MVM_SCAN_REGULAR);
		if (uid >= 0) {
			struct cfg80211_scan_info info = {
				.aborted = true,
			};

			ieee80211_scan_completed(mvm->hw, &info);
			mvm->scan_uid_status[uid] = 0;
		}
		uid = iwl_mvm_scan_uid_by_status(mvm, IWL_MVM_SCAN_SCHED);
		if (uid >= 0 && !mvm->fw_restart) {
			ieee80211_sched_scan_stopped(mvm->hw);
			mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_DISABLED;
			mvm->scan_uid_status[uid] = 0;
		}

		/* We shouldn't have any UIDs still set.  Loop over all the
		 * UIDs to make sure there's nothing left there and warn if
		 * any is found.
		 */
		for (i = 0; i < mvm->max_scans; i++) {
			if (WARN_ONCE(mvm->scan_uid_status[i],
				      "UMAC scan UID %d status was not cleaned\n",
				      i))
				mvm->scan_uid_status[i] = 0;
		}
	} else {
		if (mvm->scan_status & IWL_MVM_SCAN_REGULAR) {
			struct cfg80211_scan_info info = {
				.aborted = true,
			};

			ieee80211_scan_completed(mvm->hw, &info);
		}

		/* Sched scan will be restarted by mac80211 in
		 * restart_hw, so do not report if FW is about to be
		 * restarted.
		 */
		if ((mvm->scan_status & IWL_MVM_SCAN_SCHED) &&
		    !mvm->fw_restart) {
			ieee80211_sched_scan_stopped(mvm->hw);
			mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_DISABLED;
		}
	}
}

int iwl_mvm_scan_stop(struct iwl_mvm *mvm, int type, bool notify)
{
	int ret;

	if (!(mvm->scan_status & type))
		return 0;

	if (iwl_mvm_is_radio_killed(mvm)) {
		ret = 0;
		goto out;
	}

	ret = iwl_mvm_scan_stop_wait(mvm, type);
	if (!ret)
		mvm->scan_status |= type << IWL_MVM_SCAN_STOPPING_SHIFT;
out:
	/* Clear the scan status so the next scan requests will
	 * succeed and mark the scan as stopping, so that the Rx
	 * handler doesn't do anything, as the scan was stopped from
	 * above.
	 */
	mvm->scan_status &= ~type;

	if (type == IWL_MVM_SCAN_REGULAR) {
		/* Since the rx handler won't do anything now, we have
		 * to release the scan reference here.
		 */
		iwl_mvm_unref(mvm, IWL_MVM_REF_SCAN);
		cancel_delayed_work(&mvm->scan_timeout_dwork);
		if (notify) {
			struct cfg80211_scan_info info = {
				.aborted = true,
			};

			ieee80211_scan_completed(mvm->hw, &info);
		}
	} else if (notify) {
		ieee80211_sched_scan_stopped(mvm->hw);
		mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_DISABLED;
	}

	return ret;
}
