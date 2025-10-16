// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2025 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include <linux/etherdevice.h>
#include <net/mac80211.h>
#include <linux/crc32.h>

#include "mvm.h"
#include "fw/api/scan.h"
#include "iwl-io.h"
#include "iwl-utils.h"

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
/* adaptive dwell default high band APs number */
#define IWL_SCAN_ADWELL_DEFAULT_HB_N_APS 8
/* adaptive dwell default low band APs number */
#define IWL_SCAN_ADWELL_DEFAULT_LB_N_APS 2
/* adaptive dwell default APs number in social channels (1, 6, 11) */
#define IWL_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL 10
/* number of scan channels */
#define IWL_SCAN_NUM_CHANNELS 112
/* adaptive dwell number of APs override mask for p2p friendly GO */
#define IWL_SCAN_ADWELL_N_APS_GO_FRIENDLY_BIT BIT(20)
/* adaptive dwell number of APs override mask for social channels */
#define IWL_SCAN_ADWELL_N_APS_SOCIAL_CHS_BIT BIT(21)
/* adaptive dwell number of APs override for p2p friendly GO channels */
#define IWL_SCAN_ADWELL_N_APS_GO_FRIENDLY 10
/* adaptive dwell number of APs override for social channels */
#define IWL_SCAN_ADWELL_N_APS_SOCIAL_CHS 2

/* minimal number of 2GHz and 5GHz channels in the regular scan request */
#define IWL_MVM_6GHZ_PASSIVE_SCAN_MIN_CHANS 4

/* Number of iterations on the channel for mei filtered scan */
#define IWL_MEI_SCAN_NUM_ITER	5U

#define WFA_TPC_IE_LEN	9

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
	bool iter_notif;
	struct cfg80211_scan_6ghz_params *scan_6ghz_params;
	u32 n_6ghz_params;
	bool scan_6ghz;
	bool enable_6ghz_passive;
	bool respect_p2p_go, respect_p2p_go_hb;
	s8 tsf_report_link_id;
	u8 bssid[ETH_ALEN] __aligned(2);
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

static inline __le32
iwl_mvm_scan_rate_n_flags(struct iwl_mvm *mvm, enum nl80211_band band,
			  bool no_cck)
{
	u32 tx_ant;

	iwl_mvm_toggle_tx_ant(mvm, &mvm->scan_last_antenna_idx);
	tx_ant = BIT(mvm->scan_last_antenna_idx) << RATE_MCS_ANT_POS;

	if (band == NL80211_BAND_2GHZ && !no_cck)
		return cpu_to_le32(IWL_RATE_1M_PLCP | RATE_MCS_CCK_MSK_V1 |
				   tx_ant);
	else
		return cpu_to_le32(IWL_RATE_6M_PLCP | tx_ant);
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

struct iwl_mvm_scan_iter_data {
	u32 global_cnt;
	struct ieee80211_vif *current_vif;
	bool is_dcm_with_p2p_go;
};

static void iwl_mvm_scan_iterator(void *_data, u8 *mac,
				  struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_scan_iter_data *data = _data;
	struct iwl_mvm_vif *curr_mvmvif;

	if (vif->type != NL80211_IFTYPE_P2P_DEVICE &&
	    mvmvif->deflink.phy_ctxt &&
	    mvmvif->deflink.phy_ctxt->id < NUM_PHY_CTX)
		data->global_cnt += 1;

	if (!data->current_vif || vif == data->current_vif)
		return;

	curr_mvmvif = iwl_mvm_vif_from_mac80211(data->current_vif);

	if (ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_P2P_GO &&
	    mvmvif->deflink.phy_ctxt && curr_mvmvif->deflink.phy_ctxt &&
	    mvmvif->deflink.phy_ctxt->id != curr_mvmvif->deflink.phy_ctxt->id)
		data->is_dcm_with_p2p_go = true;
}

static enum
iwl_mvm_scan_type _iwl_mvm_get_scan_type(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif,
					 enum iwl_mvm_traffic_load load,
					 bool low_latency)
{
	struct iwl_mvm_scan_iter_data data = {
		.current_vif = vif,
		.is_dcm_with_p2p_go = false,
		.global_cnt = 0,
	};

	/*
	 * A scanning AP interface probably wants to generate a survey to do
	 * ACS (automatic channel selection).
	 * Force a non-fragmented scan in that case.
	 */
	if (vif && ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_AP)
		return IWL_SCAN_TYPE_WILD;

	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   iwl_mvm_scan_iterator,
						   &data);

	if (!data.global_cnt)
		return IWL_SCAN_TYPE_UNASSOC;

	if (fw_has_api(&mvm->fw->ucode_capa,
		       IWL_UCODE_TLV_API_FRAGMENTED_SCAN)) {
		if ((load == IWL_MVM_TRAFFIC_HIGH || low_latency) &&
		    (!vif || vif->type != NL80211_IFTYPE_P2P_DEVICE))
			return IWL_SCAN_TYPE_FRAGMENTED;

		/*
		 * in case of DCM with P2P GO set all scan requests as
		 * fast-balance scan
		 */
		if (vif && vif->type == NL80211_IFTYPE_STATION &&
		    data.is_dcm_with_p2p_go)
			return IWL_SCAN_TYPE_FAST_BALANCE;
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

	/* we create the 802.11 header SSID element and WFA TPC element */
	max_probe_len -= 24 + 2 + WFA_TPC_IE_LEN;

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
		    !memcmp(ssid_list[i].ssid, ssid, ssid_len))
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
	u32 tmp_bitmap = 0;

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
			tmp_bitmap |= BIT(i);
		} else {
			tmp_bitmap |= BIT(index);
		}
	}
	if (ssid_bitmap)
		*ssid_bitmap = tmp_bitmap;
}

static int
iwl_mvm_config_sched_scan_profiles(struct iwl_mvm *mvm,
				   struct cfg80211_sched_scan_request *req)
{
	struct iwl_scan_offload_profile *profile;
	struct iwl_scan_offload_profile_cfg_v1 *profile_cfg_v1;
	struct iwl_scan_offload_blocklist *blocklist;
	struct iwl_scan_offload_profile_cfg_data *data;
	int max_profiles = iwl_umac_scan_get_max_profiles(mvm->fw);
	int profile_cfg_size = sizeof(*data) +
		sizeof(*profile) * max_profiles;
	struct iwl_host_cmd cmd = {
		.id = SCAN_OFFLOAD_UPDATE_PROFILES_CMD,
		.len[1] = profile_cfg_size,
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
		.dataflags[1] = IWL_HCMD_DFL_NOCOPY,
	};
	int blocklist_len;
	int i;
	int ret;

	if (WARN_ON(req->n_match_sets > max_profiles))
		return -EIO;

	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_SHORT_BL)
		blocklist_len = IWL_SCAN_SHORT_BLACKLIST_LEN;
	else
		blocklist_len = IWL_SCAN_MAX_BLACKLIST_LEN;

	blocklist = kcalloc(blocklist_len, sizeof(*blocklist), GFP_KERNEL);
	if (!blocklist)
		return -ENOMEM;

	profile_cfg_v1 = kzalloc(profile_cfg_size, GFP_KERNEL);
	if (!profile_cfg_v1) {
		ret = -ENOMEM;
		goto free_blocklist;
	}

	cmd.data[0] = blocklist;
	cmd.len[0] = sizeof(*blocklist) * blocklist_len;
	cmd.data[1] = profile_cfg_v1;

	/* if max_profile is MAX_PROFILES_V2, we have the new API */
	if (max_profiles == IWL_SCAN_MAX_PROFILES_V2) {
		struct iwl_scan_offload_profile_cfg *profile_cfg =
			(struct iwl_scan_offload_profile_cfg *)profile_cfg_v1;

		data = &profile_cfg->data;
	} else {
		data = &profile_cfg_v1->data;
	}

	/* No blocklist configuration */
	data->num_profiles = req->n_match_sets;
	data->active_clients = SCAN_CLIENT_SCHED_SCAN;
	data->pass_match = SCAN_CLIENT_SCHED_SCAN;
	data->match_notify = SCAN_CLIENT_SCHED_SCAN;

	if (!req->n_match_sets || !req->match_sets[0].ssid.ssid_len)
		data->any_beacon_notify = SCAN_CLIENT_SCHED_SCAN;

	for (i = 0; i < req->n_match_sets; i++) {
		profile = &profile_cfg_v1->profiles[i];
		profile->ssid_index = i;
		/* Support any cipher and auth algorithm */
		profile->unicast_cipher = 0xff;
		profile->auth_alg = IWL_AUTH_ALGO_UNSUPPORTED |
			IWL_AUTH_ALGO_NONE | IWL_AUTH_ALGO_PSK | IWL_AUTH_ALGO_8021X |
			IWL_AUTH_ALGO_SAE | IWL_AUTH_ALGO_8021X_SHA384 | IWL_AUTH_ALGO_OWE;
		profile->network_type = IWL_NETWORK_TYPE_ANY;
		profile->band_selection = IWL_SCAN_OFFLOAD_SELECT_ANY;
		profile->client_bitmap = SCAN_CLIENT_SCHED_SCAN;
	}

	IWL_DEBUG_SCAN(mvm, "Sending scheduled scan profile config\n");

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	kfree(profile_cfg_v1);
free_blocklist:
	kfree(blocklist);

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

	if (!iwl_mvm_has_new_station_api(mvm->fw)) {
		tx_cmd[0].sta_id = mvm->aux_sta.sta_id;
		tx_cmd[1].sta_id = mvm->aux_sta.sta_id;

	/*
	 * Fw doesn't use this sta anymore, pending deprecation via HOST API
	 * change
	 */
	} else {
		tx_cmd[0].sta_id = 0xff;
		tx_cmd[1].sta_id = 0xff;
	}

	tx_cmd[1].tx_flags = cpu_to_le32(TX_CMD_FLG_SEQ_CTL |
					 TX_CMD_FLG_BT_DIS);

	tx_cmd[1].rate_n_flags = iwl_mvm_scan_rate_n_flags(mvm,
							   NL80211_BAND_5GHZ,
							   no_cck);
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
	ether_addr_copy(frame->bssid, params->bssid);
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

	memcpy(pos, ies->ies[NL80211_BAND_6GHZ],
	       ies->len[NL80211_BAND_6GHZ]);
	params->preq.band_data[2].offset = cpu_to_le16(pos - params->preq.buf);
	params->preq.band_data[2].len =
		cpu_to_le16(ies->len[NL80211_BAND_6GHZ]);
	pos += ies->len[NL80211_BAND_6GHZ];
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
		(n_channels <= mvm->fw->ucode_capa.n_scan_channels) &&
		(ies->common_ie_len +
		 ies->len[NL80211_BAND_2GHZ] + ies->len[NL80211_BAND_5GHZ] +
		 ies->len[NL80211_BAND_6GHZ] <=
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
	 *	6. the VIF is not an AP interface (scan wants survey results)
	 */
	return ((capa->flags & IWL_UCODE_TLV_FLAGS_EBS_SUPPORT) &&
		mvm->last_ebs_successful && IWL_MVM_ENABLE_EBS &&
		vif->type != NL80211_IFTYPE_P2P_DEVICE &&
		(!low_latency || iwl_mvm_is_frag_ebs_supported(mvm)) &&
		ieee80211_vif_type_p2p(vif) != NL80211_IFTYPE_AP);
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

static void
iwl_mvm_scan_set_legacy_probe_req(struct iwl_scan_probe_req_v1 *p_req,
				  struct iwl_scan_probe_req *src_p_req)
{
	int i;

	p_req->mac_header = src_p_req->mac_header;
	for (i = 0; i < SCAN_NUM_BAND_PROBE_DATA_V_1; i++)
		p_req->band_data[i] = src_p_req->band_data[i];
	p_req->common_data = src_p_req->common_data;
	memcpy(p_req->buf, src_p_req->buf, sizeof(p_req->buf));
}

static int iwl_mvm_scan_lmac(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     struct iwl_mvm_scan_params *params)
{
	struct iwl_scan_req_lmac *cmd = mvm->scan_cmd;
	struct iwl_scan_probe_req_v1 *preq =
		(void *)(cmd->data + sizeof(struct iwl_scan_channel_cfg_lmac) *
			 mvm->fw->ucode_capa.n_scan_channels);
	u32 ssid_bitmap = 0;
	int i;
	u8 band;

	if (WARN_ON(params->n_scan_plans > IWL_MAX_SCHED_SCAN_PLANS))
		return -EINVAL;

	iwl_mvm_scan_lmac_dwell(mvm, cmd, params);

	cmd->rx_chain_select = iwl_mvm_scan_rx_chain(mvm);
	cmd->iter_num = cpu_to_le32(1);
	cmd->n_channels = (u8)params->n_channels;

	cmd->delay = cpu_to_le32(params->delay);

	cmd->scan_flags = cpu_to_le32(iwl_mvm_scan_lmac_flags(mvm, params,
							      vif));

	band = iwl_mvm_phy_band_from_nl80211(params->channels[0]->band);
	cmd->flags = cpu_to_le32(band);
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

	iwl_mvm_scan_set_legacy_probe_req(preq, &params->preq);

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

	/* This function should not be called when using ADD_STA ver >=12 */
	WARN_ON_ONCE(iwl_mvm_has_new_station_api(mvm->fw));

	cfg->bcast_sta_id = mvm->aux_sta.sta_id;
	cfg->channel_flags = channel_flags;

	iwl_mvm_fill_channels(mvm, cfg->channel_array, max_channels);
}

static void iwl_mvm_fill_scan_config_v2(struct iwl_mvm *mvm, void *config,
					u32 flags, u8 channel_flags,
					u32 max_channels)
{
	struct iwl_scan_config_v2 *cfg = config;

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

	/* This function should not be called when using ADD_STA ver >=12 */
	WARN_ON_ONCE(iwl_mvm_has_new_station_api(mvm->fw));

	cfg->bcast_sta_id = mvm->aux_sta.sta_id;
	cfg->channel_flags = channel_flags;

	iwl_mvm_fill_channels(mvm, cfg->channel_array, max_channels);
}

static int iwl_mvm_legacy_config_scan(struct iwl_mvm *mvm)
{
	void *cfg;
	int ret, cmd_size;
	struct iwl_host_cmd cmd = {
		.id = WIDE_ID(IWL_ALWAYS_LONG_GROUP, SCAN_CFG_CMD),
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
		cmd_size = sizeof(struct iwl_scan_config_v2);
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
		iwl_mvm_fill_scan_config_v2(mvm, cfg, flags, channel_flags,
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

int iwl_mvm_config_scan(struct iwl_mvm *mvm)
{
	struct iwl_scan_config cfg;
	struct iwl_host_cmd cmd = {
		.id = WIDE_ID(IWL_ALWAYS_LONG_GROUP, SCAN_CFG_CMD),
		.len[0] = sizeof(cfg),
		.data[0] = &cfg,
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
	};

	if (!iwl_mvm_is_reduced_config_scan_supported(mvm))
		return iwl_mvm_legacy_config_scan(mvm);

	memset(&cfg, 0, sizeof(cfg));

	if (!iwl_mvm_has_new_station_api(mvm->fw)) {
		cfg.bcast_sta_id = mvm->aux_sta.sta_id;
	} else if (iwl_fw_lookup_cmd_ver(mvm->fw, SCAN_CFG_CMD, 0) < 5) {
		/*
		 * Fw doesn't use this sta anymore. Deprecated on SCAN_CFG_CMD
		 * version 5.
		 */
		cfg.bcast_sta_id = 0xff;
	}

	cfg.tx_chains = cpu_to_le32(iwl_mvm_get_valid_tx_ant(mvm));
	cfg.rx_chains = cpu_to_le32(iwl_mvm_scan_rx_ant(mvm));

	IWL_DEBUG_SCAN(mvm, "Sending UMAC scan config\n");

	return iwl_mvm_send_cmd(mvm, &cmd);
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
	active_dwell = IWL_SCAN_DWELL_ACTIVE;
	passive_dwell = IWL_SCAN_DWELL_PASSIVE;

	if (iwl_mvm_is_adaptive_dwell_supported(mvm)) {
		cmd->v7.adwell_default_n_aps_social =
			IWL_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL;
		cmd->v7.adwell_default_n_aps =
			IWL_SCAN_ADWELL_DEFAULT_LB_N_APS;

		if (iwl_mvm_is_adwell_hb_ap_num_supported(mvm))
			cmd->v9.adwell_default_hb_n_aps =
				IWL_SCAN_ADWELL_DEFAULT_HB_N_APS;

		/* if custom max budget was configured with debugfs */
		if (IWL_MVM_ADWELL_MAX_BUDGET)
			cmd->v7.adwell_max_budget =
				cpu_to_le16(IWL_MVM_ADWELL_MAX_BUDGET);
		else if (params->n_ssids && params->ssids[0].ssid_len)
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
		cmd->v1.extended_dwell = IWL_SCAN_DWELL_EXTENDED;
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

static u32 iwl_mvm_scan_umac_ooc_priority(int type)
{
	if (type == IWL_MVM_SCAN_REGULAR)
		return IWL_SCAN_PRIORITY_EXT_6;

	return IWL_SCAN_PRIORITY_EXT_2;
}

static void
iwl_mvm_scan_umac_dwell_v11(struct iwl_mvm *mvm,
			    struct iwl_scan_general_params_v11 *general_params,
			    struct iwl_mvm_scan_params *params)
{
	struct iwl_mvm_scan_timing_params *timing, *hb_timing;
	u8 active_dwell, passive_dwell;

	timing = &scan_timing[params->type];
	active_dwell = IWL_SCAN_DWELL_ACTIVE;
	passive_dwell = IWL_SCAN_DWELL_PASSIVE;

	general_params->adwell_default_social_chn =
		IWL_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL;
	general_params->adwell_default_2g = IWL_SCAN_ADWELL_DEFAULT_LB_N_APS;
	general_params->adwell_default_5g = IWL_SCAN_ADWELL_DEFAULT_HB_N_APS;

	/* if custom max budget was configured with debugfs */
	if (IWL_MVM_ADWELL_MAX_BUDGET)
		general_params->adwell_max_budget =
			cpu_to_le16(IWL_MVM_ADWELL_MAX_BUDGET);
	else if (params->n_ssids && params->ssids[0].ssid_len)
		general_params->adwell_max_budget =
			cpu_to_le16(IWL_SCAN_ADWELL_MAX_BUDGET_DIRECTED_SCAN);
	else
		general_params->adwell_max_budget =
			cpu_to_le16(IWL_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN);

	general_params->scan_priority = cpu_to_le32(IWL_SCAN_PRIORITY_EXT_6);
	general_params->max_out_of_time[SCAN_LB_LMAC_IDX] =
		cpu_to_le32(timing->max_out_time);
	general_params->suspend_time[SCAN_LB_LMAC_IDX] =
		cpu_to_le32(timing->suspend_time);

	hb_timing = &scan_timing[params->hb_type];

	general_params->max_out_of_time[SCAN_HB_LMAC_IDX] =
		cpu_to_le32(hb_timing->max_out_time);
	general_params->suspend_time[SCAN_HB_LMAC_IDX] =
		cpu_to_le32(hb_timing->suspend_time);

	general_params->active_dwell[SCAN_LB_LMAC_IDX] = active_dwell;
	general_params->passive_dwell[SCAN_LB_LMAC_IDX] = passive_dwell;
	general_params->active_dwell[SCAN_HB_LMAC_IDX] = active_dwell;
	general_params->passive_dwell[SCAN_HB_LMAC_IDX] = passive_dwell;
}

struct iwl_mvm_scan_channel_segment {
	u8 start_idx;
	u8 end_idx;
	u8 first_channel_id;
	u8 last_channel_id;
	u8 channel_spacing_shift;
	u8 band;
};

static const struct iwl_mvm_scan_channel_segment scan_channel_segments[] = {
	{
		.start_idx = 0,
		.end_idx = 13,
		.first_channel_id = 1,
		.last_channel_id = 14,
		.channel_spacing_shift = 0,
		.band = PHY_BAND_24
	},
	{
		.start_idx = 14,
		.end_idx = 41,
		.first_channel_id = 36,
		.last_channel_id = 144,
		.channel_spacing_shift = 2,
		.band = PHY_BAND_5
	},
	{
		.start_idx = 42,
		.end_idx = 50,
		.first_channel_id = 149,
		.last_channel_id = 181,
		.channel_spacing_shift = 2,
		.band = PHY_BAND_5
	},
	{
		.start_idx = 51,
		.end_idx = 111,
		.first_channel_id = 1,
		.last_channel_id = 241,
		.channel_spacing_shift = 2,
		.band = PHY_BAND_6
	},
};

static int iwl_mvm_scan_ch_and_band_to_idx(u8 channel_id, u8 band)
{
	int i, index;

	if (!channel_id)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(scan_channel_segments); i++) {
		const struct iwl_mvm_scan_channel_segment *ch_segment =
			&scan_channel_segments[i];
		u32 ch_offset;

		if (ch_segment->band != band ||
		    ch_segment->first_channel_id > channel_id ||
		    ch_segment->last_channel_id < channel_id)
			continue;

		ch_offset = (channel_id - ch_segment->first_channel_id) >>
			ch_segment->channel_spacing_shift;

		index = scan_channel_segments[i].start_idx + ch_offset;
		if (index < IWL_SCAN_NUM_CHANNELS)
			return index;

		break;
	}

	return -EINVAL;
}

static const u8 p2p_go_friendly_chs[] = {
	36, 40, 44, 48, 149, 153, 157, 161, 165,
};

static const u8 social_chs[] = {
	1, 6, 11
};

static void iwl_mvm_scan_ch_add_n_aps_override(enum nl80211_iftype vif_type,
					       u8 ch_id, u8 band, u8 *ch_bitmap,
					       size_t bitmap_n_entries)
{
	int i;

	if (vif_type != NL80211_IFTYPE_P2P_DEVICE)
		return;

	for (i = 0; i < ARRAY_SIZE(p2p_go_friendly_chs); i++) {
		if (p2p_go_friendly_chs[i] == ch_id) {
			int ch_idx, bitmap_idx;

			ch_idx = iwl_mvm_scan_ch_and_band_to_idx(ch_id, band);
			if (ch_idx < 0)
				return;

			bitmap_idx = ch_idx / 8;
			if (bitmap_idx >= bitmap_n_entries)
				return;

			ch_idx = ch_idx % 8;
			ch_bitmap[bitmap_idx] |= BIT(ch_idx);

			return;
		}
	}
}

static u32 iwl_mvm_scan_ch_n_aps_flag(enum nl80211_iftype vif_type, u8 ch_id)
{
	int i;
	u32 flags = 0;

	if (vif_type != NL80211_IFTYPE_P2P_DEVICE)
		goto out;

	for (i = 0; i < ARRAY_SIZE(p2p_go_friendly_chs); i++) {
		if (p2p_go_friendly_chs[i] == ch_id) {
			flags |= IWL_SCAN_ADWELL_N_APS_GO_FRIENDLY_BIT;
			break;
		}
	}

	if (flags)
		goto out;

	for (i = 0; i < ARRAY_SIZE(social_chs); i++) {
		if (social_chs[i] == ch_id) {
			flags |= IWL_SCAN_ADWELL_N_APS_SOCIAL_CHS_BIT;
			break;
		}
	}

out:
	return flags;
}

static void
iwl_mvm_umac_scan_cfg_channels(struct iwl_mvm *mvm,
			       struct ieee80211_channel **channels,
			       int n_channels, u32 flags,
			       struct iwl_scan_channel_cfg_umac *channel_cfg)
{
	int i;

	for (i = 0; i < n_channels; i++) {
		channel_cfg[i].flags = cpu_to_le32(flags);
		channel_cfg[i].channel_num = channels[i]->hw_value;
		if (iwl_mvm_is_scan_ext_chan_supported(mvm)) {
			enum nl80211_band band = channels[i]->band;

			channel_cfg[i].v2.band =
				iwl_mvm_phy_band_from_nl80211(band);
			channel_cfg[i].v2.iter_count = 1;
			channel_cfg[i].v2.iter_interval = 0;
		} else {
			channel_cfg[i].v1.iter_count = 1;
			channel_cfg[i].v1.iter_interval = 0;
		}
	}
}

static void
iwl_mvm_umac_scan_cfg_channels_v4(struct iwl_mvm *mvm,
				  struct ieee80211_channel **channels,
				  struct iwl_scan_channel_params_v4 *cp,
				  int n_channels, u32 flags,
				  enum nl80211_iftype vif_type)
{
	u8 *bitmap = cp->adwell_ch_override_bitmap;
	size_t bitmap_n_entries = ARRAY_SIZE(cp->adwell_ch_override_bitmap);
	int i;

	for (i = 0; i < n_channels; i++) {
		enum nl80211_band band = channels[i]->band;
		struct iwl_scan_channel_cfg_umac *cfg =
			&cp->channel_config[i];

		cfg->flags = cpu_to_le32(flags);
		cfg->channel_num = channels[i]->hw_value;
		cfg->v2.band = iwl_mvm_phy_band_from_nl80211(band);
		cfg->v2.iter_count = 1;
		cfg->v2.iter_interval = 0;

		iwl_mvm_scan_ch_add_n_aps_override(vif_type,
						   cfg->channel_num,
						   cfg->v2.band, bitmap,
						   bitmap_n_entries);
	}
}

static void
iwl_mvm_umac_scan_cfg_channels_v7(struct iwl_mvm *mvm,
				  struct ieee80211_channel **channels,
				  struct iwl_scan_channel_params_v7 *cp,
				  int n_channels, u32 flags,
				  enum nl80211_iftype vif_type, u32 version)
{
	int i;

	for (i = 0; i < n_channels; i++) {
		enum nl80211_band band = channels[i]->band;
		struct iwl_scan_channel_cfg_umac *cfg = &cp->channel_config[i];
		u32 n_aps_flag =
			iwl_mvm_scan_ch_n_aps_flag(vif_type,
						   channels[i]->hw_value);
		u8 iwl_band = iwl_mvm_phy_band_from_nl80211(band);

		cfg->flags = cpu_to_le32(flags | n_aps_flag);
		cfg->channel_num = channels[i]->hw_value;
		if (cfg80211_channel_is_psc(channels[i]))
			cfg->flags = 0;

		if (band == NL80211_BAND_6GHZ) {
			/* 6 GHz channels should only appear in a scan request
			 * that has scan_6ghz set. The only exception is MLO
			 * scan, which has to be passive.
			 */
			WARN_ON_ONCE(cfg->flags != 0);
			cfg->flags =
				cpu_to_le32(IWL_UHB_CHAN_CFG_FLAG_FORCE_PASSIVE);
		}

		cfg->v2.iter_count = 1;
		cfg->v2.iter_interval = 0;
		if (version < 17)
			cfg->v2.band = iwl_band;
		else
			cfg->flags |= cpu_to_le32((iwl_band <<
						   IWL_CHAN_CFG_FLAGS_BAND_POS));
	}
}

static void
iwl_mvm_umac_scan_fill_6g_chan_list(struct iwl_mvm *mvm,
				    struct iwl_mvm_scan_params *params,
				     struct iwl_scan_probe_params_v4 *pp)
{
	int j, idex_s = 0, idex_b = 0;
	struct cfg80211_scan_6ghz_params *scan_6ghz_params =
		params->scan_6ghz_params;
	bool hidden_supported = fw_has_capa(&mvm->fw->ucode_capa,
					    IWL_UCODE_TLV_CAPA_HIDDEN_6GHZ_SCAN);

	for (j = 0; j < params->n_ssids && idex_s < SCAN_SHORT_SSID_MAX_SIZE;
	     j++) {
		if (!params->ssids[j].ssid_len)
			continue;

		pp->short_ssid[idex_s] =
			cpu_to_le32(~crc32_le(~0, params->ssids[j].ssid,
					      params->ssids[j].ssid_len));

		if (hidden_supported) {
			pp->direct_scan[idex_s].id = WLAN_EID_SSID;
			pp->direct_scan[idex_s].len = params->ssids[j].ssid_len;
			memcpy(pp->direct_scan[idex_s].ssid, params->ssids[j].ssid,
			       params->ssids[j].ssid_len);
		}
		idex_s++;
	}

	/*
	 * Populate the arrays of the short SSIDs and the BSSIDs using the 6GHz
	 * collocated parameters. This might not be optimal, as this processing
	 * does not (yet) correspond to the actual channels, so it is possible
	 * that some entries would be left out.
	 *
	 * TODO: improve this logic.
	 */
	for (j = 0; j < params->n_6ghz_params; j++) {
		int k;

		/* First, try to place the short SSID */
		if (scan_6ghz_params[j].short_ssid_valid) {
			for (k = 0; k < idex_s; k++) {
				if (pp->short_ssid[k] ==
				    cpu_to_le32(scan_6ghz_params[j].short_ssid))
					break;
			}

			if (k == idex_s && idex_s < SCAN_SHORT_SSID_MAX_SIZE) {
				pp->short_ssid[idex_s++] =
					cpu_to_le32(scan_6ghz_params[j].short_ssid);
			}
		}

		/* try to place BSSID for the same entry */
		for (k = 0; k < idex_b; k++) {
			if (!memcmp(&pp->bssid_array[k],
				    scan_6ghz_params[j].bssid, ETH_ALEN))
				break;
		}

		if (k == idex_b && idex_b < SCAN_BSSID_MAX_SIZE &&
		    !WARN_ONCE(!is_valid_ether_addr(scan_6ghz_params[j].bssid),
			       "scan: invalid BSSID at index %u, index_b=%u\n",
			       j, idex_b)) {
			memcpy(&pp->bssid_array[idex_b++],
			       scan_6ghz_params[j].bssid, ETH_ALEN);
		}
	}

	pp->short_ssid_num = idex_s;
	pp->bssid_num = idex_b;
}

/* TODO: this function can be merged with iwl_mvm_scan_umac_fill_ch_p_v7 */
static u32
iwl_mvm_umac_scan_cfg_channels_v7_6g(struct iwl_mvm *mvm,
				     struct iwl_mvm_scan_params *params,
				     u32 n_channels,
				     struct iwl_scan_probe_params_v4 *pp,
				     struct iwl_scan_channel_params_v7 *cp,
				     enum nl80211_iftype vif_type,
				     u32 version)
{
	int i;
	struct cfg80211_scan_6ghz_params *scan_6ghz_params =
		params->scan_6ghz_params;
	u32 ch_cnt;

	for (i = 0, ch_cnt = 0; i < params->n_channels; i++) {
		struct iwl_scan_channel_cfg_umac *cfg =
			&cp->channel_config[ch_cnt];

		u32 s_ssid_bitmap = 0, bssid_bitmap = 0, flags = 0;
		u8 k, n_s_ssids = 0, n_bssids = 0;
		u8 max_s_ssids, max_bssids;
		bool force_passive = false, found = false, allow_passive = true,
		     unsolicited_probe_on_chan = false, psc_no_listen = false;
		s8 psd_20 = IEEE80211_RNR_TBTT_PARAMS_PSD_RESERVED;

		/*
		 * Avoid performing passive scan on non PSC channels unless the
		 * scan is specifically a passive scan, i.e., no SSIDs
		 * configured in the scan command.
		 */
		if (!cfg80211_channel_is_psc(params->channels[i]) &&
		    !params->n_6ghz_params && params->n_ssids)
			continue;

		cfg->channel_num = params->channels[i]->hw_value;
		if (version < 17)
			cfg->v2.band = PHY_BAND_6;
		else
			cfg->flags |= cpu_to_le32(PHY_BAND_6 <<
						  IWL_CHAN_CFG_FLAGS_BAND_POS);

		cfg->v5.iter_count = 1;
		cfg->v5.iter_interval = 0;

		for (u32 j = 0; j < params->n_6ghz_params; j++) {
			s8 tmp_psd_20;

			if (!(scan_6ghz_params[j].channel_idx == i))
				continue;

			unsolicited_probe_on_chan |=
				scan_6ghz_params[j].unsolicited_probe;

			/* Use the highest PSD value allowed as advertised by
			 * APs for this channel
			 */
			tmp_psd_20 = scan_6ghz_params[j].psd_20;
			if (tmp_psd_20 !=
			    IEEE80211_RNR_TBTT_PARAMS_PSD_RESERVED &&
			    (psd_20 ==
			     IEEE80211_RNR_TBTT_PARAMS_PSD_RESERVED ||
			     psd_20 < tmp_psd_20))
				psd_20 = tmp_psd_20;

			psc_no_listen |= scan_6ghz_params[j].psc_no_listen;
		}

		/*
		 * In the following cases apply passive scan:
		 * 1. Non fragmented scan:
		 *	- PSC channel with NO_LISTEN_FLAG on should be treated
		 *	  like non PSC channel
		 *	- Non PSC channel with more than 3 short SSIDs or more
		 *	  than 9 BSSIDs.
		 *	- Non PSC Channel with unsolicited probe response and
		 *	  more than 2 short SSIDs or more than 6 BSSIDs.
		 *	- PSC channel with more than 2 short SSIDs or more than
		 *	  6 BSSIDs.
		 * 3. Fragmented scan:
		 *	- PSC channel with more than 1 SSID or 3 BSSIDs.
		 *	- Non PSC channel with more than 2 SSIDs or 6 BSSIDs.
		 *	- Non PSC channel with unsolicited probe response and
		 *	  more than 1 SSID or more than 3 BSSIDs.
		 */
		if (!iwl_mvm_is_scan_fragmented(params->type)) {
			if (!cfg80211_channel_is_psc(params->channels[i]) ||
			    psc_no_listen) {
				if (unsolicited_probe_on_chan) {
					max_s_ssids = 2;
					max_bssids = 6;
				} else {
					max_s_ssids = 3;
					max_bssids = 9;
				}
			} else {
				max_s_ssids = 2;
				max_bssids = 6;
			}
		} else if (cfg80211_channel_is_psc(params->channels[i])) {
			max_s_ssids = 1;
			max_bssids = 3;
		} else {
			if (unsolicited_probe_on_chan) {
				max_s_ssids = 1;
				max_bssids = 3;
			} else {
				max_s_ssids = 2;
				max_bssids = 6;
			}
		}

		/*
		 * The optimize the scan time, i.e., reduce the scan dwell time
		 * on each channel, the below logic tries to set 3 direct BSSID
		 * probe requests for each broadcast probe request with a short
		 * SSID.
		 * TODO: improve this logic
		 */
		for (u32 j = 0; j < params->n_6ghz_params; j++) {
			if (!(scan_6ghz_params[j].channel_idx == i))
				continue;

			found = false;

			for (k = 0;
			     k < pp->short_ssid_num && n_s_ssids < max_s_ssids;
			     k++) {
				if (!scan_6ghz_params[j].unsolicited_probe &&
				    le32_to_cpu(pp->short_ssid[k]) ==
				    scan_6ghz_params[j].short_ssid) {
					/* Relevant short SSID bit set */
					if (s_ssid_bitmap & BIT(k)) {
						found = true;
						break;
					}

					/*
					 * Prefer creating BSSID entries unless
					 * the short SSID probe can be done in
					 * the same channel dwell iteration.
					 *
					 * We also need to create a short SSID
					 * entry for any hidden AP.
					 */
					if (3 * n_s_ssids > n_bssids &&
					    !pp->direct_scan[k].len)
						break;

					/* Hidden AP, cannot do passive scan */
					if (pp->direct_scan[k].len)
						allow_passive = false;

					s_ssid_bitmap |= BIT(k);
					n_s_ssids++;
					found = true;
					break;
				}
			}

			if (found)
				continue;

			for (k = 0; k < pp->bssid_num; k++) {
				if (!memcmp(&pp->bssid_array[k],
					    scan_6ghz_params[j].bssid,
					    ETH_ALEN)) {
					if (!(bssid_bitmap & BIT(k))) {
						if (n_bssids < max_bssids) {
							bssid_bitmap |= BIT(k);
							n_bssids++;
						} else {
							force_passive = TRUE;
						}
					}
					break;
				}
			}
		}

		if (cfg80211_channel_is_psc(params->channels[i]) &&
		    psc_no_listen)
			flags |= IWL_UHB_CHAN_CFG_FLAG_PSC_CHAN_NO_LISTEN;

		if (unsolicited_probe_on_chan)
			flags |= IWL_UHB_CHAN_CFG_FLAG_UNSOLICITED_PROBE_RES;

		if ((allow_passive && force_passive) ||
		    (!(bssid_bitmap | s_ssid_bitmap) &&
		     !cfg80211_channel_is_psc(params->channels[i])))
			flags |= IWL_UHB_CHAN_CFG_FLAG_FORCE_PASSIVE;
		else
			flags |= bssid_bitmap | (s_ssid_bitmap << 16);

		cfg->flags |= cpu_to_le32(flags);
		if (version >= 17)
			cfg->v5.psd_20 = psd_20;

		ch_cnt++;
	}

	if (params->n_channels > ch_cnt)
		IWL_DEBUG_SCAN(mvm,
			       "6GHz: reducing number channels: (%u->%u)\n",
			       params->n_channels, ch_cnt);

	return ch_cnt;
}

static u8 iwl_mvm_scan_umac_chan_flags_v2(struct iwl_mvm *mvm,
					  struct iwl_mvm_scan_params *params,
					  struct ieee80211_vif *vif)
{
	u8 flags = 0;

	flags |= IWL_SCAN_CHANNEL_FLAG_ENABLE_CHAN_ORDER;

	if (iwl_mvm_scan_use_ebs(mvm, vif))
		flags |= IWL_SCAN_CHANNEL_FLAG_EBS |
			IWL_SCAN_CHANNEL_FLAG_EBS_ACCURATE |
			IWL_SCAN_CHANNEL_FLAG_CACHE_ADD;

	/* set fragmented ebs for fragmented scan on HB channels */
	if ((!iwl_mvm_is_cdb_supported(mvm) &&
	     iwl_mvm_is_scan_fragmented(params->type)) ||
	    (iwl_mvm_is_cdb_supported(mvm) &&
	     iwl_mvm_is_scan_fragmented(params->hb_type)))
		flags |= IWL_SCAN_CHANNEL_FLAG_EBS_FRAG;

	/*
	 * force EBS in case the scan is a fragmented and there is a need to take P2P
	 * GO operation into consideration during scan operation.
	 */
	if ((!iwl_mvm_is_cdb_supported(mvm) &&
	     iwl_mvm_is_scan_fragmented(params->type) && params->respect_p2p_go) ||
	    (iwl_mvm_is_cdb_supported(mvm) &&
	     iwl_mvm_is_scan_fragmented(params->hb_type) &&
	     params->respect_p2p_go_hb)) {
		IWL_DEBUG_SCAN(mvm, "Respect P2P GO. Force EBS\n");
		flags |= IWL_SCAN_CHANNEL_FLAG_FORCE_EBS;
	}

	return flags;
}

static void iwl_mvm_scan_6ghz_passive_scan(struct iwl_mvm *mvm,
					   struct iwl_mvm_scan_params *params,
					   struct ieee80211_vif *vif)
{
	struct ieee80211_supported_band *sband =
		&mvm->nvm_data->bands[NL80211_BAND_6GHZ];
	u32 n_disabled, i;

	params->enable_6ghz_passive = false;

	if (params->scan_6ghz)
		return;

	if (!fw_has_capa(&mvm->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_PASSIVE_6GHZ_SCAN)) {
		IWL_DEBUG_SCAN(mvm,
			       "6GHz passive scan: Not supported by FW\n");
		return;
	}

	/* 6GHz passive scan allowed only on station interface  */
	if (vif->type != NL80211_IFTYPE_STATION) {
		IWL_DEBUG_SCAN(mvm,
			       "6GHz passive scan: not station interface\n");
		return;
	}

	/*
	 * 6GHz passive scan is allowed in a defined time interval following HW
	 * reset or resume flow, or while not associated and a large interval
	 * has passed since the last 6GHz passive scan.
	 */
	if ((vif->cfg.assoc ||
	     time_after(mvm->last_6ghz_passive_scan_jiffies +
			(IWL_MVM_6GHZ_PASSIVE_SCAN_TIMEOUT * HZ), jiffies)) &&
	    (time_before(mvm->last_reset_or_resume_time_jiffies +
			 (IWL_MVM_6GHZ_PASSIVE_SCAN_ASSOC_TIMEOUT * HZ),
			 jiffies))) {
		IWL_DEBUG_SCAN(mvm, "6GHz passive scan: %s\n",
			       vif->cfg.assoc ? "associated" :
			       "timeout did not expire");
		return;
	}

	/* not enough channels in the regular scan request */
	if (params->n_channels < IWL_MVM_6GHZ_PASSIVE_SCAN_MIN_CHANS) {
		IWL_DEBUG_SCAN(mvm,
			       "6GHz passive scan: not enough channels\n");
		return;
	}

	for (i = 0; i < params->n_ssids; i++) {
		if (!params->ssids[i].ssid_len)
			break;
	}

	/* not a wildcard scan, so cannot enable passive 6GHz scan */
	if (i == params->n_ssids) {
		IWL_DEBUG_SCAN(mvm,
			       "6GHz passive scan: no wildcard SSID\n");
		return;
	}

	if (!sband || !sband->n_channels) {
		IWL_DEBUG_SCAN(mvm,
			       "6GHz passive scan: no 6GHz channels\n");
		return;
	}

	for (i = 0, n_disabled = 0; i < sband->n_channels; i++) {
		if (sband->channels[i].flags & (IEEE80211_CHAN_DISABLED))
			n_disabled++;
	}

	/*
	 * Not all the 6GHz channels are disabled, so no need for 6GHz passive
	 * scan
	 */
	if (n_disabled != sband->n_channels) {
		IWL_DEBUG_SCAN(mvm,
			       "6GHz passive scan: 6GHz channels enabled\n");
		return;
	}

	/* all conditions to enable 6ghz passive scan are satisfied */
	IWL_DEBUG_SCAN(mvm, "6GHz passive scan: can be enabled\n");
	params->enable_6ghz_passive = true;
}

static u16 iwl_mvm_scan_umac_flags_v2(struct iwl_mvm *mvm,
				      struct iwl_mvm_scan_params *params,
				      struct ieee80211_vif *vif,
				      int type)
{
	u16 flags = 0;

	/*
	 * If no direct SSIDs are provided perform a passive scan. Otherwise,
	 * if there is a single SSID which is not the broadcast SSID, assume
	 * that the scan is intended for roaming purposes and thus enable Rx on
	 * all chains to improve chances of hearing the beacons/probe responses.
	 */
	if (params->n_ssids == 0)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_FORCE_PASSIVE;
	else if (params->n_ssids == 1 && params->ssids[0].ssid_len)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_USE_ALL_RX_CHAINS;

	if (iwl_mvm_is_scan_fragmented(params->type))
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_FRAGMENTED_LMAC1;

	if (iwl_mvm_is_scan_fragmented(params->hb_type))
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_FRAGMENTED_LMAC2;

	if (params->pass_all)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_PASS_ALL;
	else
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_MATCH;

	if (!iwl_mvm_is_regular_scan(params))
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_PERIODIC;

	if (params->iter_notif ||
	    mvm->sched_scan_pass_all == SCHED_SCAN_PASS_ALL_ENABLED)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_NTFY_ITER_COMPLETE;

	if (IWL_MVM_ADWELL_ENABLE)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_ADAPTIVE_DWELL;

	if (type == IWL_MVM_SCAN_SCHED || type == IWL_MVM_SCAN_NETDETECT)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_PREEMPTIVE;

	if ((type == IWL_MVM_SCAN_SCHED || type == IWL_MVM_SCAN_NETDETECT) &&
	    params->flags & NL80211_SCAN_FLAG_COLOCATED_6GHZ)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_TRIGGER_UHB_SCAN;

	if (params->enable_6ghz_passive)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_6GHZ_PASSIVE_SCAN;

	if (iwl_mvm_is_oce_supported(mvm) &&
	    (params->flags & (NL80211_SCAN_FLAG_ACCEPT_BCAST_PROBE_RESP |
			      NL80211_SCAN_FLAG_OCE_PROBE_REQ_HIGH_TX_RATE |
			      NL80211_SCAN_FLAG_FILS_MAX_CHANNEL_TIME)))
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_OCE;

	return flags;
}

static u8 iwl_mvm_scan_umac_flags2(struct iwl_mvm *mvm,
				   struct iwl_mvm_scan_params *params,
				   struct ieee80211_vif *vif, int type,
				   u16 gen_flags)
{
	u8 flags = 0;

	if (iwl_mvm_is_cdb_supported(mvm)) {
		if (params->respect_p2p_go)
			flags |= IWL_UMAC_SCAN_GEN_PARAMS_FLAGS2_RESPECT_P2P_GO_LB;
		if (params->respect_p2p_go_hb)
			flags |= IWL_UMAC_SCAN_GEN_PARAMS_FLAGS2_RESPECT_P2P_GO_HB;
	} else {
		if (params->respect_p2p_go)
			flags = IWL_UMAC_SCAN_GEN_PARAMS_FLAGS2_RESPECT_P2P_GO_LB |
				IWL_UMAC_SCAN_GEN_PARAMS_FLAGS2_RESPECT_P2P_GO_HB;
	}

	if (params->scan_6ghz &&
	    fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_SCAN_DONT_TOGGLE_ANT))
		flags |= IWL_UMAC_SCAN_GEN_PARAMS_FLAGS2_DONT_TOGGLE_ANT;

	/* Passive and AP interface -> ACS (automatic channel selection) */
	if (gen_flags & IWL_UMAC_SCAN_GEN_FLAGS_V2_FORCE_PASSIVE &&
	    ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_AP &&
	    iwl_fw_lookup_notif_ver(mvm->fw, SCAN_GROUP, CHANNEL_SURVEY_NOTIF,
				    0) >= 1)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS2_COLLECT_CHANNEL_STATS;

	return flags;
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

	if (params->iter_notif)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_ITER_COMPLETE;

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (mvm->scan_iter_notif_enabled)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_ITER_COMPLETE;
#endif

	if (mvm->sched_scan_pass_all == SCHED_SCAN_PASS_ALL_ENABLED)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_ITER_COMPLETE;

	if (iwl_mvm_is_adaptive_dwell_supported(mvm) && IWL_MVM_ADWELL_ENABLE)
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

static int
iwl_mvm_fill_scan_sched_params(struct iwl_mvm_scan_params *params,
			       struct iwl_scan_umac_schedule *schedule,
			       __le16 *delay)
{
	int i;
	if (WARN_ON(!params->n_scan_plans ||
		    params->n_scan_plans > IWL_MAX_SCHED_SCAN_PLANS))
		return -EINVAL;

	for (i = 0; i < params->n_scan_plans; i++) {
		struct cfg80211_sched_scan_plan *scan_plan =
			&params->scan_plans[i];

		schedule[i].iter_count = scan_plan->iterations;
		schedule[i].interval =
			cpu_to_le16(scan_plan->interval);
	}

	/*
	 * If the number of iterations of the last scan plan is set to
	 * zero, it should run infinitely. However, this is not always the case.
	 * For example, when regular scan is requested the driver sets one scan
	 * plan with one iteration.
	 */
	if (!schedule[params->n_scan_plans - 1].iter_count)
		schedule[params->n_scan_plans - 1].iter_count = 0xff;

	*delay = cpu_to_le16(params->delay);

	return 0;
}

static int iwl_mvm_scan_umac(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     struct iwl_mvm_scan_params *params,
			     int type, int uid)
{
	struct iwl_scan_req_umac *cmd = mvm->scan_cmd;
	struct iwl_scan_umac_chan_param *chan_param;
	void *cmd_data = iwl_mvm_get_scan_req_umac_data(mvm);
	void *sec_part = (u8 *)cmd_data + sizeof(struct iwl_scan_channel_cfg_umac) *
		mvm->fw->ucode_capa.n_scan_channels;
	struct iwl_scan_req_umac_tail_v2 *tail_v2 =
		(struct iwl_scan_req_umac_tail_v2 *)sec_part;
	struct iwl_scan_req_umac_tail_v1 *tail_v1;
	struct iwl_ssid_ie *direct_scan;
	int ret = 0;
	u32 ssid_bitmap = 0;
	u8 channel_flags = 0;
	u16 gen_flags;
	struct iwl_mvm_vif *scan_vif = iwl_mvm_vif_from_mac80211(vif);

	chan_param = iwl_mvm_get_scan_req_umac_channel(mvm);

	iwl_mvm_scan_umac_dwell(mvm, cmd, params);

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

	ret = iwl_mvm_fill_scan_sched_params(params, tail_v2->schedule,
					     &tail_v2->delay);
	if (ret)
		return ret;

	if (iwl_mvm_is_scan_ext_chan_supported(mvm)) {
		tail_v2->preq = params->preq;
		direct_scan = tail_v2->direct_scan;
	} else {
		tail_v1 = (struct iwl_scan_req_umac_tail_v1 *)sec_part;
		iwl_mvm_scan_set_legacy_probe_req(&tail_v1->preq,
						  &params->preq);
		direct_scan = tail_v1->direct_scan;
	}
	iwl_scan_build_ssids(params, direct_scan, &ssid_bitmap);
	iwl_mvm_umac_scan_cfg_channels(mvm, params->channels,
				       params->n_channels, ssid_bitmap,
				       cmd_data);
	return 0;
}

static void
iwl_mvm_scan_umac_fill_general_p_v12(struct iwl_mvm *mvm,
				     struct iwl_mvm_scan_params *params,
				     struct ieee80211_vif *vif,
				     struct iwl_scan_general_params_v11 *gp,
				     u16 gen_flags, u8 gen_flags2,
				     u32 version)
{
	struct iwl_mvm_vif *scan_vif = iwl_mvm_vif_from_mac80211(vif);

	iwl_mvm_scan_umac_dwell_v11(mvm, gp, params);

	IWL_DEBUG_SCAN(mvm, "General: flags=0x%x, flags2=0x%x\n",
		       gen_flags, gen_flags2);

	gp->flags = cpu_to_le16(gen_flags);
	gp->flags2 = gen_flags2;

	if (gen_flags & IWL_UMAC_SCAN_GEN_FLAGS_V2_FRAGMENTED_LMAC1)
		gp->num_of_fragments[SCAN_LB_LMAC_IDX] = IWL_SCAN_NUM_OF_FRAGS;
	if (gen_flags & IWL_UMAC_SCAN_GEN_FLAGS_V2_FRAGMENTED_LMAC2)
		gp->num_of_fragments[SCAN_HB_LMAC_IDX] = IWL_SCAN_NUM_OF_FRAGS;

	mvm->scan_link_id = 0;

	if (version < 16) {
		gp->scan_start_mac_or_link_id = scan_vif->id;
	} else {
		struct iwl_mvm_vif_link_info *link_info =
			scan_vif->link[params->tsf_report_link_id];

		mvm->scan_link_id = params->tsf_report_link_id;
		if (!WARN_ON(!link_info))
			gp->scan_start_mac_or_link_id = link_info->fw_link_id;
	}
}

static void
iwl_mvm_scan_umac_fill_probe_p_v3(struct iwl_mvm_scan_params *params,
				  struct iwl_scan_probe_params_v3 *pp)
{
	pp->preq = params->preq;
	pp->ssid_num = params->n_ssids;
	iwl_scan_build_ssids(params, pp->direct_scan, NULL);
}

static void
iwl_mvm_scan_umac_fill_probe_p_v4(struct iwl_mvm_scan_params *params,
				  struct iwl_scan_probe_params_v4 *pp,
				  u32 *bitmap_ssid)
{
	pp->preq = params->preq;
	iwl_scan_build_ssids(params, pp->direct_scan, bitmap_ssid);
}

static void
iwl_mvm_scan_umac_fill_ch_p_v4(struct iwl_mvm *mvm,
			       struct iwl_mvm_scan_params *params,
			       struct ieee80211_vif *vif,
			       struct iwl_scan_channel_params_v4 *cp,
			       u32 channel_cfg_flags)
{
	cp->flags = iwl_mvm_scan_umac_chan_flags_v2(mvm, params, vif);
	cp->count = params->n_channels;
	cp->num_of_aps_override = IWL_SCAN_ADWELL_N_APS_GO_FRIENDLY;

	iwl_mvm_umac_scan_cfg_channels_v4(mvm, params->channels, cp,
					  params->n_channels,
					  channel_cfg_flags,
					  vif->type);
}

static void
iwl_mvm_scan_umac_fill_ch_p_v7(struct iwl_mvm *mvm,
			       struct iwl_mvm_scan_params *params,
			       struct ieee80211_vif *vif,
			       struct iwl_scan_channel_params_v7 *cp,
			       u32 channel_cfg_flags,
			       u32 version)
{
	cp->flags = iwl_mvm_scan_umac_chan_flags_v2(mvm, params, vif);
	cp->count = params->n_channels;
	cp->n_aps_override[0] = IWL_SCAN_ADWELL_N_APS_GO_FRIENDLY;
	cp->n_aps_override[1] = IWL_SCAN_ADWELL_N_APS_SOCIAL_CHS;

	iwl_mvm_umac_scan_cfg_channels_v7(mvm, params->channels, cp,
					  params->n_channels,
					  channel_cfg_flags,
					  vif->type, version);

	if (params->enable_6ghz_passive) {
		struct ieee80211_supported_band *sband =
			&mvm->nvm_data->bands[NL80211_BAND_6GHZ];
		u32 i;

		for (i = 0; i < sband->n_channels; i++) {
			struct ieee80211_channel *channel =
				&sband->channels[i];

			struct iwl_scan_channel_cfg_umac *cfg =
				&cp->channel_config[cp->count];

			if (!cfg80211_channel_is_psc(channel))
				continue;

			cfg->channel_num = channel->hw_value;
			cfg->v5.iter_count = 1;
			cfg->v5.iter_interval = 0;

			if (version < 17) {
				cfg->flags = 0;
				cfg->v2.band = PHY_BAND_6;
			} else {
				cfg->flags = cpu_to_le32(PHY_BAND_6 <<
							 IWL_CHAN_CFG_FLAGS_BAND_POS);
				cfg->v5.psd_20 =
					IEEE80211_RNR_TBTT_PARAMS_PSD_RESERVED;
			}
			cp->count++;
		}
	}
}

static int iwl_mvm_scan_umac_v12(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				 struct iwl_mvm_scan_params *params, int type,
				 int uid)
{
	struct iwl_scan_req_umac_v12 *cmd = mvm->scan_cmd;
	struct iwl_scan_req_params_v12 *scan_p = &cmd->scan_params;
	int ret;
	u16 gen_flags;

	cmd->ooc_priority = cpu_to_le32(iwl_mvm_scan_umac_ooc_priority(type));
	cmd->uid = cpu_to_le32(uid);

	gen_flags = iwl_mvm_scan_umac_flags_v2(mvm, params, vif, type);
	iwl_mvm_scan_umac_fill_general_p_v12(mvm, params, vif,
					     &scan_p->general_params,
					     gen_flags, 0, 12);

	ret = iwl_mvm_fill_scan_sched_params(params,
					     scan_p->periodic_params.schedule,
					     &scan_p->periodic_params.delay);
	if (ret)
		return ret;

	iwl_mvm_scan_umac_fill_probe_p_v3(params, &scan_p->probe_params);
	iwl_mvm_scan_umac_fill_ch_p_v4(mvm, params, vif,
				       &scan_p->channel_params, 0);

	return 0;
}

static int iwl_mvm_scan_umac_v14_and_above(struct iwl_mvm *mvm,
					   struct ieee80211_vif *vif,
					   struct iwl_mvm_scan_params *params,
					   int type, int uid, u32 version)
{
	struct iwl_scan_req_umac_v17 *cmd = mvm->scan_cmd;
	struct iwl_scan_req_params_v17 *scan_p = &cmd->scan_params;
	struct iwl_scan_channel_params_v7 *cp = &scan_p->channel_params;
	struct iwl_scan_probe_params_v4 *pb = &scan_p->probe_params;
	int ret;
	u16 gen_flags;
	u8 gen_flags2;
	u32 bitmap_ssid = 0;

	cmd->ooc_priority = cpu_to_le32(iwl_mvm_scan_umac_ooc_priority(type));
	cmd->uid = cpu_to_le32(uid);

	gen_flags = iwl_mvm_scan_umac_flags_v2(mvm, params, vif, type);

	if (version >= 15)
		gen_flags2 = iwl_mvm_scan_umac_flags2(mvm, params, vif, type,
						      gen_flags);
	else
		gen_flags2 = 0;

	iwl_mvm_scan_umac_fill_general_p_v12(mvm, params, vif,
					     &scan_p->general_params,
					     gen_flags, gen_flags2, version);

	ret = iwl_mvm_fill_scan_sched_params(params,
					     scan_p->periodic_params.schedule,
					     &scan_p->periodic_params.delay);
	if (ret)
		return ret;

	if (!params->scan_6ghz) {
		iwl_mvm_scan_umac_fill_probe_p_v4(params,
						  &scan_p->probe_params,
						  &bitmap_ssid);
		iwl_mvm_scan_umac_fill_ch_p_v7(mvm, params, vif,
					       &scan_p->channel_params,
					       bitmap_ssid,
					       version);
		return 0;
	} else {
		pb->preq = params->preq;
	}

	cp->flags = iwl_mvm_scan_umac_chan_flags_v2(mvm, params, vif);
	cp->n_aps_override[0] = IWL_SCAN_ADWELL_N_APS_GO_FRIENDLY;
	cp->n_aps_override[1] = IWL_SCAN_ADWELL_N_APS_SOCIAL_CHS;

	iwl_mvm_umac_scan_fill_6g_chan_list(mvm, params, pb);

	cp->count = iwl_mvm_umac_scan_cfg_channels_v7_6g(mvm, params,
							 params->n_channels,
							 pb, cp, vif->type,
							 version);
	if (!cp->count)
		return -EINVAL;

	if (!params->n_ssids ||
	    (params->n_ssids == 1 && !params->ssids[0].ssid_len))
		cp->flags |= IWL_SCAN_CHANNEL_FLAG_6G_PSC_NO_FILTER;

	return 0;
}

static int iwl_mvm_scan_umac_v14(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				 struct iwl_mvm_scan_params *params, int type,
				 int uid)
{
	return iwl_mvm_scan_umac_v14_and_above(mvm, vif, params, type, uid, 14);
}

static int iwl_mvm_scan_umac_v15(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				 struct iwl_mvm_scan_params *params, int type,
				 int uid)
{
	return iwl_mvm_scan_umac_v14_and_above(mvm, vif, params, type, uid, 15);
}

static int iwl_mvm_scan_umac_v16(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				 struct iwl_mvm_scan_params *params, int type,
				 int uid)
{
	return iwl_mvm_scan_umac_v14_and_above(mvm, vif, params, type, uid, 16);
}

static int iwl_mvm_scan_umac_v17(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				 struct iwl_mvm_scan_params *params, int type,
				 int uid)
{
	return iwl_mvm_scan_umac_v14_and_above(mvm, vif, params, type, uid, 17);
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
		fallthrough;
	default:
		WARN_ON(1);
		break;
	}

	return -EIO;
}

#define SCAN_TIMEOUT 30000

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

struct iwl_scan_umac_handler {
	u8 version;
	int (*handler)(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		       struct iwl_mvm_scan_params *params, int type, int uid);
};

#define IWL_SCAN_UMAC_HANDLER(_ver) {		\
	.version = _ver,			\
	.handler = iwl_mvm_scan_umac_v##_ver,	\
}

static const struct iwl_scan_umac_handler iwl_scan_umac_handlers[] = {
	/* set the newest version first to shorten the list traverse time */
	IWL_SCAN_UMAC_HANDLER(17),
	IWL_SCAN_UMAC_HANDLER(16),
	IWL_SCAN_UMAC_HANDLER(15),
	IWL_SCAN_UMAC_HANDLER(14),
	IWL_SCAN_UMAC_HANDLER(12),
};

static void iwl_mvm_mei_scan_work(struct work_struct *wk)
{
	struct iwl_mei_scan_filter *scan_filter =
		container_of(wk, struct iwl_mei_scan_filter, scan_work);
	struct iwl_mvm *mvm =
		container_of(scan_filter, struct iwl_mvm, mei_scan_filter);
	struct iwl_mvm_csme_conn_info *info;
	struct sk_buff *skb;
	u8 bssid[ETH_ALEN];

	mutex_lock(&mvm->mutex);
	info = iwl_mvm_get_csme_conn_info(mvm);
	memcpy(bssid, info->conn_info.bssid, ETH_ALEN);
	mutex_unlock(&mvm->mutex);

	while ((skb = skb_dequeue(&scan_filter->scan_res))) {
		struct ieee80211_mgmt *mgmt = (void *)skb->data;

		if (!memcmp(mgmt->bssid, bssid, ETH_ALEN))
			ieee80211_rx_irqsafe(mvm->hw, skb);
		else
			kfree_skb(skb);
	}
}

void iwl_mvm_mei_scan_filter_init(struct iwl_mei_scan_filter *mei_scan_filter)
{
	skb_queue_head_init(&mei_scan_filter->scan_res);
	INIT_WORK(&mei_scan_filter->scan_work, iwl_mvm_mei_scan_work);
}

/* In case CSME is connected and has link protection set, this function will
 * override the scan request to scan only the associated channel and only for
 * the associated SSID.
 */
static void iwl_mvm_mei_limited_scan(struct iwl_mvm *mvm,
				     struct iwl_mvm_scan_params *params)
{
	struct iwl_mvm_csme_conn_info *info = iwl_mvm_get_csme_conn_info(mvm);
	struct iwl_mei_conn_info *conn_info;
	struct ieee80211_channel *chan;
	int scan_iters, i;

	if (!info) {
		IWL_DEBUG_SCAN(mvm, "mei_limited_scan: no connection info\n");
		return;
	}

	conn_info = &info->conn_info;
	if (!info->conn_info.lp_state || !info->conn_info.ssid_len)
		return;

	if (!params->n_channels || !params->n_ssids)
		return;

	mvm->mei_scan_filter.is_mei_limited_scan = true;

	chan = ieee80211_get_channel(mvm->hw->wiphy,
				     ieee80211_channel_to_frequency(conn_info->channel,
								    conn_info->band));
	if (!chan) {
		IWL_DEBUG_SCAN(mvm,
			       "Failed to get CSME channel (chan=%u band=%u)\n",
			       conn_info->channel, conn_info->band);
		return;
	}

	/* The mei filtered scan must find the AP, otherwise CSME will
	 * take the NIC ownership. Add several iterations on the channel to
	 * make the scan more robust.
	 */
	scan_iters = min(IWL_MEI_SCAN_NUM_ITER, params->n_channels);
	params->n_channels = scan_iters;
	for (i = 0; i < scan_iters; i++)
		params->channels[i] = chan;

	IWL_DEBUG_SCAN(mvm, "Mei scan: num iterations=%u\n", scan_iters);

	params->n_ssids = 1;
	params->ssids[0].ssid_len = conn_info->ssid_len;
	memcpy(params->ssids[0].ssid, conn_info->ssid, conn_info->ssid_len);
}

static int iwl_mvm_build_scan_cmd(struct iwl_mvm *mvm,
				  struct ieee80211_vif *vif,
				  struct iwl_host_cmd *hcmd,
				  struct iwl_mvm_scan_params *params,
				  int type)
{
	int uid, i, err;
	u8 scan_ver;

	lockdep_assert_held(&mvm->mutex);
	memset(mvm->scan_cmd, 0, mvm->scan_cmd_size);

	iwl_mvm_mei_limited_scan(mvm, params);

	if (!fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN)) {
		hcmd->id = SCAN_OFFLOAD_REQUEST_CMD;

		return iwl_mvm_scan_lmac(mvm, vif, params);
	}

	uid = iwl_mvm_scan_uid_by_status(mvm, 0);
	if (uid < 0)
		return uid;

	hcmd->id = WIDE_ID(IWL_ALWAYS_LONG_GROUP, SCAN_REQ_UMAC);

	scan_ver = iwl_fw_lookup_cmd_ver(mvm->fw, SCAN_REQ_UMAC,
					 IWL_FW_CMD_VER_UNKNOWN);

	for (i = 0; i < ARRAY_SIZE(iwl_scan_umac_handlers); i++) {
		const struct iwl_scan_umac_handler *ver_handler =
			&iwl_scan_umac_handlers[i];

		if (ver_handler->version != scan_ver)
			continue;

		err = ver_handler->handler(mvm, vif, params, type, uid);
		return err ? : uid;
	}

	err = iwl_mvm_scan_umac(mvm, vif, params, type, uid);
	if (err)
		return err;

	return uid;
}

struct iwl_mvm_scan_respect_p2p_go_iter_data {
	struct ieee80211_vif *current_vif;
	bool p2p_go;
	enum nl80211_band band;
};

static void iwl_mvm_scan_respect_p2p_go_iter(void *_data, u8 *mac,
					     struct ieee80211_vif *vif)
{
	struct iwl_mvm_scan_respect_p2p_go_iter_data *data = _data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	/* exclude the given vif */
	if (vif == data->current_vif)
		return;

	if (ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_P2P_GO) {
		u32 link_id;

		for (link_id = 0;
		     link_id < ARRAY_SIZE(mvmvif->link);
		     link_id++) {
			struct iwl_mvm_vif_link_info *link =
				mvmvif->link[link_id];

			if (link && link->phy_ctxt->id < NUM_PHY_CTX &&
			    (data->band == NUM_NL80211_BANDS ||
			     link->phy_ctxt->channel->band == data->band)) {
				data->p2p_go = true;
				break;
			}
		}
	}
}

static bool _iwl_mvm_get_respect_p2p_go(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					bool low_latency,
					enum nl80211_band band)
{
	struct iwl_mvm_scan_respect_p2p_go_iter_data data = {
		.current_vif = vif,
		.p2p_go = false,
		.band = band,
	};

	if (!low_latency)
		return false;

	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   iwl_mvm_scan_respect_p2p_go_iter,
						   &data);

	return data.p2p_go;
}

static bool iwl_mvm_get_respect_p2p_go_band(struct iwl_mvm *mvm,
					    struct ieee80211_vif *vif,
					    enum nl80211_band band)
{
	bool low_latency = iwl_mvm_low_latency_band(mvm, band);

	return _iwl_mvm_get_respect_p2p_go(mvm, vif, low_latency, band);
}

static bool iwl_mvm_get_respect_p2p_go(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif)
{
	bool low_latency = iwl_mvm_low_latency(mvm);

	return _iwl_mvm_get_respect_p2p_go(mvm, vif, low_latency,
					   NUM_NL80211_BANDS);
}

static void iwl_mvm_fill_respect_p2p_go(struct iwl_mvm *mvm,
					struct iwl_mvm_scan_params *params,
					struct ieee80211_vif *vif)
{
	if (iwl_mvm_is_cdb_supported(mvm)) {
		params->respect_p2p_go =
			iwl_mvm_get_respect_p2p_go_band(mvm, vif,
							NL80211_BAND_2GHZ);
		params->respect_p2p_go_hb =
			iwl_mvm_get_respect_p2p_go_band(mvm, vif,
							NL80211_BAND_5GHZ);
	} else {
		params->respect_p2p_go = iwl_mvm_get_respect_p2p_go(mvm, vif);
	}
}

static int _iwl_mvm_single_scan_start(struct iwl_mvm *mvm,
				      struct ieee80211_vif *vif,
				      struct cfg80211_scan_request *req,
				      struct ieee80211_scan_ies *ies,
				      int type)
{
	struct iwl_host_cmd hcmd = {
		.len = { iwl_mvm_scan_size(mvm), },
		.data = { mvm->scan_cmd, },
		.dataflags = { IWL_HCMD_DFL_NOCOPY, },
	};
	struct iwl_mvm_scan_params params = {};
	int ret, uid;
	struct cfg80211_sched_scan_plan scan_plan = { .iterations = 1 };

	lockdep_assert_held(&mvm->mutex);

	if (iwl_mvm_is_lar_supported(mvm) && !mvm->lar_regdom_set) {
		IWL_ERR(mvm, "scan while LAR regdomain is not set\n");
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
	params.delay = 0;
	params.ssids = req->ssids;
	params.channels = req->channels;
	params.mac_addr = req->mac_addr;
	params.mac_addr_mask = req->mac_addr_mask;
	params.no_cck = req->no_cck;
	params.pass_all = true;
	params.n_match_sets = 0;
	params.match_sets = NULL;
	ether_addr_copy(params.bssid, req->bssid);

	params.scan_plans = &scan_plan;
	params.n_scan_plans = 1;

	params.n_6ghz_params = req->n_6ghz_params;
	params.scan_6ghz_params = req->scan_6ghz_params;
	params.scan_6ghz = req->scan_6ghz;
	iwl_mvm_fill_scan_type(mvm, &params, vif);
	iwl_mvm_fill_respect_p2p_go(mvm, &params, vif);

	if (req->duration)
		params.iter_notif = true;

	params.tsf_report_link_id = req->tsf_report_link_id;
	if (params.tsf_report_link_id < 0) {
		if (vif->active_links)
			params.tsf_report_link_id = __ffs(vif->active_links);
		else
			params.tsf_report_link_id = 0;
	}

	iwl_mvm_build_scan_probe(mvm, vif, ies, &params);

	iwl_mvm_scan_6ghz_passive_scan(mvm, &params, vif);

	uid = iwl_mvm_build_scan_cmd(mvm, vif, &hcmd, &params, type);

	if (uid < 0)
		return uid;

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

	IWL_DEBUG_SCAN(mvm, "Scan request send success: type=%u, uid=%u\n",
		       type, uid);

	mvm->scan_uid_status[uid] = type;
	mvm->scan_status |= type;

	if (type == IWL_MVM_SCAN_REGULAR) {
		mvm->scan_vif = iwl_mvm_vif_from_mac80211(vif);
		schedule_delayed_work(&mvm->scan_timeout_dwork,
				      msecs_to_jiffies(SCAN_TIMEOUT));
	}

	if (params.enable_6ghz_passive)
		mvm->last_6ghz_passive_scan_jiffies = jiffies;

	return 0;
}

int iwl_mvm_reg_scan_start(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			   struct cfg80211_scan_request *req,
			   struct ieee80211_scan_ies *ies)
{
	return _iwl_mvm_single_scan_start(mvm, vif, req, ies,
					  IWL_MVM_SCAN_REGULAR);
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
	int ret, uid;
	int i, j;
	bool non_psc_included = false;

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
	eth_broadcast_addr(params.bssid);
	if (!req->n_scan_plans)
		return -EINVAL;

	params.n_scan_plans = req->n_scan_plans;
	params.scan_plans = req->scan_plans;

	iwl_mvm_fill_scan_type(mvm, &params, vif);
	iwl_mvm_fill_respect_p2p_go(mvm, &params, vif);

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

	/* for 6 GHZ band only PSC channels need to be added */
	for (i = 0; i < params.n_channels; i++) {
		struct ieee80211_channel *channel = params.channels[i];

		if (channel->band == NL80211_BAND_6GHZ &&
		    !cfg80211_channel_is_psc(channel)) {
			non_psc_included = true;
			break;
		}
	}

	if (non_psc_included) {
		params.channels = kmemdup(params.channels,
					  sizeof(params.channels[0]) *
					  params.n_channels,
					  GFP_KERNEL);
		if (!params.channels)
			return -ENOMEM;

		for (i = j = 0; i < params.n_channels; i++) {
			if (params.channels[i]->band == NL80211_BAND_6GHZ &&
			    !cfg80211_channel_is_psc(params.channels[i]))
				continue;
			params.channels[j++] = params.channels[i];
		}
		params.n_channels = j;
	}

	if (!iwl_mvm_scan_fits(mvm, req->n_ssids, ies, params.n_channels)) {
		ret = -ENOBUFS;
		goto out;
	}

	uid = iwl_mvm_build_scan_cmd(mvm, vif, &hcmd, &params, type);
	if (uid < 0) {
		ret = uid;
		goto out;
	}

	ret = iwl_mvm_send_cmd(mvm, &hcmd);
	if (!ret) {
		IWL_DEBUG_SCAN(mvm,
			       "Sched scan request send success: type=%u, uid=%u\n",
			       type, uid);
		mvm->scan_uid_status[uid] = type;
		mvm->scan_status |= type;
	} else {
		/* If the scan failed, it usually means that the FW was unable
		 * to allocate the time events. Warn on it, but maybe we
		 * should try to send the command again with different params.
		 */
		IWL_ERR(mvm, "Sched scan failed! ret %d\n", ret);
		mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_DISABLED;
	}

out:
	if (non_psc_included)
		kfree(params.channels);
	return ret;
}

void iwl_mvm_rx_umac_scan_complete_notif(struct iwl_mvm *mvm,
					 struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_umac_scan_complete *notif = (void *)pkt->data;
	u32 uid = __le32_to_cpu(notif->uid);
	bool aborted = (notif->status == IWL_SCAN_OFFLOAD_ABORTED);

	mvm->mei_scan_filter.is_mei_limited_scan = false;

	IWL_DEBUG_SCAN(mvm,
		       "Scan completed: uid=%u type=%u, status=%s, EBS=%s\n",
		       uid, mvm->scan_uid_status[uid],
		       notif->status == IWL_SCAN_OFFLOAD_COMPLETED ?
				"completed" : "aborted",
		       iwl_mvm_ebs_status_str(notif->ebs_status));

	IWL_DEBUG_SCAN(mvm, "Scan completed: scan_status=0x%x\n",
		       mvm->scan_status);

	IWL_DEBUG_SCAN(mvm,
		       "Scan completed: line=%u, iter=%u, elapsed time=%u\n",
		       notif->last_schedule, notif->last_iter,
		       __le32_to_cpu(notif->time_from_last_iter));

	if (WARN_ON(!(mvm->scan_uid_status[uid] & mvm->scan_status)))
		return;

	/* if the scan is already stopping, we don't need to notify mac80211 */
	if (mvm->scan_uid_status[uid] == IWL_MVM_SCAN_REGULAR) {
		struct cfg80211_scan_info info = {
			.aborted = aborted,
			.scan_start_tsf = mvm->scan_start,
		};
		struct iwl_mvm_vif *scan_vif = mvm->scan_vif;
		struct iwl_mvm_vif_link_info *link_info =
			scan_vif->link[mvm->scan_link_id];

		/* It is possible that by the time the scan is complete the link
		 * was already removed and is not valid.
		 */
		if (link_info)
			memcpy(info.tsf_bssid, link_info->bssid, ETH_ALEN);
		else
			IWL_DEBUG_SCAN(mvm, "Scan link is no longer valid\n");

		ieee80211_scan_completed(mvm->hw, &info);
		mvm->scan_vif = NULL;
		cancel_delayed_work(&mvm->scan_timeout_dwork);
		iwl_mvm_resume_tcm(mvm);
	} else if (mvm->scan_uid_status[uid] == IWL_MVM_SCAN_SCHED) {
		ieee80211_sched_scan_stopped(mvm->hw);
		mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_DISABLED;
	}

	mvm->scan_status &= ~mvm->scan_uid_status[uid];

	IWL_DEBUG_SCAN(mvm, "Scan completed: after update: scan_status=0x%x\n",
		       mvm->scan_status);

	if (notif->ebs_status != IWL_SCAN_EBS_SUCCESS &&
	    notif->ebs_status != IWL_SCAN_EBS_INACTIVE)
		mvm->last_ebs_successful = false;

	mvm->scan_uid_status[uid] = 0;
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

static int iwl_mvm_umac_scan_abort(struct iwl_mvm *mvm, int type, bool *wait)
{
	struct iwl_umac_scan_abort abort_cmd = {};
	struct iwl_host_cmd cmd = {
		.id = WIDE_ID(IWL_ALWAYS_LONG_GROUP, SCAN_ABORT_UMAC),
		.len = { sizeof(abort_cmd), },
		.data = { &abort_cmd, },
		.flags = CMD_SEND_IN_RFKILL,
	};

	int uid, ret;
	u32 status = IWL_UMAC_SCAN_ABORT_STATUS_NOT_FOUND;

	lockdep_assert_held(&mvm->mutex);

	*wait = true;

	/* We should always get a valid index here, because we already
	 * checked that this type of scan was running in the generic
	 * code.
	 */
	uid = iwl_mvm_scan_uid_by_status(mvm, type);
	if (WARN_ON_ONCE(uid < 0))
		return uid;

	abort_cmd.uid = cpu_to_le32(uid);

	IWL_DEBUG_SCAN(mvm, "Sending scan abort, uid %u\n", uid);

	ret = iwl_mvm_send_cmd_status(mvm, &cmd, &status);

	IWL_DEBUG_SCAN(mvm, "Scan abort: ret=%d, status=%u\n", ret, status);
	if (!ret)
		mvm->scan_uid_status[uid] = type << IWL_MVM_SCAN_STOPPING_SHIFT;

	/* Handle the case that the FW is no longer familiar with the scan that
	 * is to be stopped. In such a case, it is expected that the scan
	 * complete notification was already received but not yet processed.
	 * In such a case, there is no need to wait for a scan complete
	 * notification and the flow should continue similar to the case that
	 * the scan was really aborted.
	 */
	if (status == IWL_UMAC_SCAN_ABORT_STATUS_NOT_FOUND) {
		mvm->scan_uid_status[uid] = type << IWL_MVM_SCAN_STOPPING_SHIFT;
		*wait = false;
	}

	return ret;
}

static int iwl_mvm_scan_stop_wait(struct iwl_mvm *mvm, int type)
{
	struct iwl_notification_wait wait_scan_done;
	static const u16 scan_done_notif[] = { SCAN_COMPLETE_UMAC,
					      SCAN_OFFLOAD_COMPLETE, };
	int ret;
	bool wait = true;

	lockdep_assert_held(&mvm->mutex);

	iwl_init_notification_wait(&mvm->notif_wait, &wait_scan_done,
				   scan_done_notif,
				   ARRAY_SIZE(scan_done_notif),
				   NULL, NULL);

	IWL_DEBUG_SCAN(mvm, "Preparing to stop scan, type %x\n", type);

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN))
		ret = iwl_mvm_umac_scan_abort(mvm, type, &wait);
	else
		ret = iwl_mvm_lmac_scan_abort(mvm);

	if (ret) {
		IWL_DEBUG_SCAN(mvm, "couldn't stop scan type %d\n", type);
		iwl_remove_notification(&mvm->notif_wait, &wait_scan_done);
		return ret;
	} else if (!wait) {
		IWL_DEBUG_SCAN(mvm, "no need to wait for scan type %d\n", type);
		iwl_remove_notification(&mvm->notif_wait, &wait_scan_done);
		return 0;
	}

	return iwl_wait_notification(&mvm->notif_wait, &wait_scan_done,
				     1 * HZ);
}

static size_t iwl_scan_req_umac_get_size(u8 scan_ver)
{
	switch (scan_ver) {
	case 12:
		return sizeof(struct iwl_scan_req_umac_v12);
	case 14:
	case 15:
	case 16:
	case 17:
		return sizeof(struct iwl_scan_req_umac_v17);
	}

	return 0;
}

size_t iwl_mvm_scan_size(struct iwl_mvm *mvm)
{
	int base_size, tail_size;
	u8 scan_ver = iwl_fw_lookup_cmd_ver(mvm->fw, SCAN_REQ_UMAC,
					    IWL_FW_CMD_VER_UNKNOWN);

	base_size = iwl_scan_req_umac_get_size(scan_ver);
	if (base_size)
		return base_size;


	if (iwl_mvm_is_adaptive_dwell_v2_supported(mvm))
		base_size = IWL_SCAN_REQ_UMAC_SIZE_V8;
	else if (iwl_mvm_is_adaptive_dwell_supported(mvm))
		base_size = IWL_SCAN_REQ_UMAC_SIZE_V7;
	else if (iwl_mvm_cdb_scan_api(mvm))
		base_size = IWL_SCAN_REQ_UMAC_SIZE_V6;
	else
		base_size = IWL_SCAN_REQ_UMAC_SIZE_V1;

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN)) {
		if (iwl_mvm_is_scan_ext_chan_supported(mvm))
			tail_size = sizeof(struct iwl_scan_req_umac_tail_v2);
		else
			tail_size = sizeof(struct iwl_scan_req_umac_tail_v1);

		return base_size +
			sizeof(struct iwl_scan_channel_cfg_umac) *
				mvm->fw->ucode_capa.n_scan_channels +
			tail_size;
	}
	return sizeof(struct iwl_scan_req_lmac) +
		sizeof(struct iwl_scan_channel_cfg_lmac) *
		mvm->fw->ucode_capa.n_scan_channels +
		sizeof(struct iwl_scan_probe_req_v1);
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

			cancel_delayed_work(&mvm->scan_timeout_dwork);

			ieee80211_scan_completed(mvm->hw, &info);
			mvm->scan_uid_status[uid] = 0;
		}
		uid = iwl_mvm_scan_uid_by_status(mvm, IWL_MVM_SCAN_SCHED);
		if (uid >= 0) {
			/* Sched scan will be restarted by mac80211 in
			 * restart_hw, so do not report if FW is about to be
			 * restarted.
			 */
			if (!iwlwifi_mod_params.fw_restart)
				ieee80211_sched_scan_stopped(mvm->hw);
			mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_DISABLED;
			mvm->scan_uid_status[uid] = 0;
		}

		uid = iwl_mvm_scan_uid_by_status(mvm,
						 IWL_MVM_SCAN_STOPPING_REGULAR);
		if (uid >= 0)
			mvm->scan_uid_status[uid] = 0;

		uid = iwl_mvm_scan_uid_by_status(mvm,
						 IWL_MVM_SCAN_STOPPING_SCHED);
		if (uid >= 0)
			mvm->scan_uid_status[uid] = 0;

		uid = iwl_mvm_scan_uid_by_status(mvm,
						 IWL_MVM_SCAN_STOPPING_INT_MLO);
		if (uid >= 0)
			mvm->scan_uid_status[uid] = 0;

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

			cancel_delayed_work(&mvm->scan_timeout_dwork);
			ieee80211_scan_completed(mvm->hw, &info);
		}

		/* Sched scan will be restarted by mac80211 in
		 * restart_hw, so do not report if FW is about to be
		 * restarted.
		 */
		if ((mvm->scan_status & IWL_MVM_SCAN_SCHED) &&
		    !iwlwifi_mod_params.fw_restart) {
			ieee80211_sched_scan_stopped(mvm->hw);
			mvm->sched_scan_pass_all = SCHED_SCAN_PASS_ALL_DISABLED;
		}
	}
}

int iwl_mvm_scan_stop(struct iwl_mvm *mvm, int type, bool notify)
{
	int ret;

	IWL_DEBUG_SCAN(mvm,
		       "Request to stop scan: type=0x%x, status=0x%x\n",
		       type, mvm->scan_status);

	if (!(mvm->scan_status & type))
		return 0;

	if (!iwl_trans_device_enabled(mvm->trans)) {
		ret = 0;
		goto out;
	}

	ret = iwl_mvm_scan_stop_wait(mvm, type);
	if (!ret)
		mvm->scan_status |= type << IWL_MVM_SCAN_STOPPING_SHIFT;
	else
		IWL_DEBUG_SCAN(mvm, "Failed to stop scan\n");

out:
	/* Clear the scan status so the next scan requests will
	 * succeed and mark the scan as stopping, so that the Rx
	 * handler doesn't do anything, as the scan was stopped from
	 * above.
	 */
	mvm->scan_status &= ~type;

	if (type == IWL_MVM_SCAN_REGULAR) {
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

static int iwl_mvm_chanidx_from_phy(struct iwl_mvm *mvm,
				    enum nl80211_band band,
				    u16 phy_chan_num)
{
	struct ieee80211_supported_band *sband = mvm->hw->wiphy->bands[band];
	int chan_idx;

	if (WARN_ON_ONCE(!sband))
		return -EINVAL;

	for (chan_idx = 0; chan_idx < sband->n_channels; chan_idx++) {
		struct ieee80211_channel *channel = &sband->channels[chan_idx];

		if (channel->hw_value == phy_chan_num)
			return chan_idx;
	}

	return -EINVAL;
}

void iwl_mvm_rx_channel_survey_notif(struct iwl_mvm *mvm,
				     struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	const struct iwl_umac_scan_channel_survey_notif *notif =
		(void *)pkt->data;
	struct iwl_mvm_acs_survey_channel *info;
	enum nl80211_band band;
	int chan_idx;

	lockdep_assert_held(&mvm->mutex);

	if (!mvm->acs_survey) {
		size_t n_channels = 0;

		for (band = 0; band < NUM_NL80211_BANDS; band++) {
			if (!mvm->hw->wiphy->bands[band])
				continue;

			n_channels += mvm->hw->wiphy->bands[band]->n_channels;
		}

		mvm->acs_survey = kzalloc(struct_size(mvm->acs_survey,
						      channels, n_channels),
					  GFP_KERNEL);

		if (!mvm->acs_survey)
			return;

		mvm->acs_survey->n_channels = n_channels;
		n_channels = 0;
		for (band = 0; band < NUM_NL80211_BANDS; band++) {
			if (!mvm->hw->wiphy->bands[band])
				continue;

			mvm->acs_survey->bands[band] =
				&mvm->acs_survey->channels[n_channels];
			n_channels += mvm->hw->wiphy->bands[band]->n_channels;
		}
	}

	band = iwl_mvm_nl80211_band_from_phy(le32_to_cpu(notif->band));
	chan_idx = iwl_mvm_chanidx_from_phy(mvm, band,
					    le32_to_cpu(notif->channel));
	if (WARN_ON_ONCE(chan_idx < 0))
		return;

	IWL_DEBUG_SCAN(mvm, "channel survey received for freq %d\n",
		       mvm->hw->wiphy->bands[band]->channels[chan_idx].center_freq);

	info = &mvm->acs_survey->bands[band][chan_idx];

	/* Times are all in ms */
	info->time = le32_to_cpu(notif->active_time);
	info->time_busy = le32_to_cpu(notif->busy_time);
	info->time_rx = le32_to_cpu(notif->rx_time);
	info->time_tx = le32_to_cpu(notif->tx_time);
	info->noise =
		iwl_average_neg_dbm(notif->noise, ARRAY_SIZE(notif->noise));
}
