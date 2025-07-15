// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include <linux/crc32.h>

#include "mld.h"
#include "scan.h"
#include "hcmd.h"
#include "iface.h"
#include "phy.h"
#include "mlo.h"

#include "fw/api/scan.h"
#include "fw/dbg.h"

#define IWL_SCAN_DWELL_ACTIVE 10
#define IWL_SCAN_DWELL_PASSIVE 110
#define IWL_SCAN_NUM_OF_FRAGS 3

/* adaptive dwell max budget time [TU] for full scan */
#define IWL_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN 300

/* adaptive dwell max budget time [TU] for directed scan */
#define IWL_SCAN_ADWELL_MAX_BUDGET_DIRECTED_SCAN 100

/* adaptive dwell default high band APs number */
#define IWL_SCAN_ADWELL_DEFAULT_HB_N_APS 8

/* adaptive dwell default low band APs number */
#define IWL_SCAN_ADWELL_DEFAULT_LB_N_APS 2

/* adaptive dwell default APs number for P2P social channels (1, 6, 11) */
#define IWL_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL 10

/* adaptive dwell number of APs override for P2P friendly GO channels */
#define IWL_SCAN_ADWELL_N_APS_GO_FRIENDLY 10

/* adaptive dwell number of APs override for P2P social channels */
#define IWL_SCAN_ADWELL_N_APS_SOCIAL_CHS 2

/* adaptive dwell number of APs override mask for p2p friendly GO */
#define IWL_SCAN_ADWELL_N_APS_GO_FRIENDLY_BIT BIT(20)

/* adaptive dwell number of APs override mask for social channels */
#define IWL_SCAN_ADWELL_N_APS_SOCIAL_CHS_BIT BIT(21)

#define SCAN_TIMEOUT_MSEC (30000 * HZ)

/* minimal number of 2GHz and 5GHz channels in the regular scan request */
#define IWL_MLD_6GHZ_PASSIVE_SCAN_MIN_CHANS 4

enum iwl_mld_scan_type {
	IWL_SCAN_TYPE_NOT_SET,
	IWL_SCAN_TYPE_UNASSOC,
	IWL_SCAN_TYPE_WILD,
	IWL_SCAN_TYPE_MILD,
	IWL_SCAN_TYPE_FRAGMENTED,
	IWL_SCAN_TYPE_FAST_BALANCE,
};

struct iwl_mld_scan_timing_params {
	u32 suspend_time;
	u32 max_out_time;
};

static const struct iwl_mld_scan_timing_params scan_timing[] = {
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

struct iwl_mld_scan_params {
	enum iwl_mld_scan_type type;
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
	bool respect_p2p_go;
	u8 fw_link_id;
	struct cfg80211_scan_6ghz_params *scan_6ghz_params;
	u32 n_6ghz_params;
	bool scan_6ghz;
	bool enable_6ghz_passive;
	u8 bssid[ETH_ALEN] __aligned(2);
};

struct iwl_mld_scan_respect_p2p_go_iter_data {
	struct ieee80211_vif *current_vif;
	bool p2p_go;
};

static void iwl_mld_scan_respect_p2p_go_iter(void *_data, u8 *mac,
					     struct ieee80211_vif *vif)
{
	struct iwl_mld_scan_respect_p2p_go_iter_data *data = _data;

	/* exclude the given vif */
	if (vif == data->current_vif)
		return;

	/* TODO: CDB check the band of the GO */
	if (ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_P2P_GO &&
	    iwl_mld_vif_from_mac80211(vif)->ap_ibss_active)
		data->p2p_go = true;
}

static bool iwl_mld_get_respect_p2p_go(struct iwl_mld *mld,
				       struct ieee80211_vif *vif,
				       bool low_latency)
{
	struct iwl_mld_scan_respect_p2p_go_iter_data data = {
		.current_vif = vif,
		.p2p_go = false,
	};

	if (!low_latency)
		return false;

	ieee80211_iterate_active_interfaces_mtx(mld->hw,
						IEEE80211_IFACE_ITER_NORMAL,
						iwl_mld_scan_respect_p2p_go_iter,
						&data);

	return data.p2p_go;
}

struct iwl_mld_scan_iter_data {
	struct ieee80211_vif *current_vif;
	bool active_vif;
	bool is_dcm_with_p2p_go;
	bool global_low_latency;
};

static void iwl_mld_scan_iterator(void *_data, u8 *mac,
				  struct ieee80211_vif *vif)
{
	struct iwl_mld_scan_iter_data *data = _data;
	struct ieee80211_vif *curr_vif = data->current_vif;
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_vif *curr_mld_vif;
	unsigned long curr_vif_active_links;
	u16 link_id;

	data->global_low_latency |= iwl_mld_vif_low_latency(mld_vif);

	if ((ieee80211_vif_is_mld(vif) && vif->active_links) ||
	    (vif->type != NL80211_IFTYPE_P2P_DEVICE &&
	     mld_vif->deflink.active))
		data->active_vif = true;

	if (vif == curr_vif)
		return;

	if (ieee80211_vif_type_p2p(vif) != NL80211_IFTYPE_P2P_GO)
		return;

	/* Currently P2P GO can't be AP MLD so the logic below assumes that */
	WARN_ON_ONCE(ieee80211_vif_is_mld(vif));

	curr_vif_active_links =
		ieee80211_vif_is_mld(curr_vif) ? curr_vif->active_links : 1;

	curr_mld_vif = iwl_mld_vif_from_mac80211(curr_vif);

	for_each_set_bit(link_id, &curr_vif_active_links,
			 IEEE80211_MLD_MAX_NUM_LINKS) {
		struct iwl_mld_link *curr_mld_link =
			iwl_mld_link_dereference_check(curr_mld_vif, link_id);

		if (WARN_ON(!curr_mld_link))
			return;

		if (rcu_access_pointer(curr_mld_link->chan_ctx) &&
		    rcu_access_pointer(mld_vif->deflink.chan_ctx) !=
		    rcu_access_pointer(curr_mld_link->chan_ctx)) {
			data->is_dcm_with_p2p_go = true;
			return;
		}
	}
}

static enum
iwl_mld_scan_type iwl_mld_get_scan_type(struct iwl_mld *mld,
					struct ieee80211_vif *vif,
					struct iwl_mld_scan_iter_data *data)
{
	enum iwl_mld_traffic_load load = mld->scan.traffic_load.status;

	/* A scanning AP interface probably wants to generate a survey to do
	 * ACS (automatic channel selection).
	 * Force a non-fragmented scan in that case.
	 */
	if (ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_AP)
		return IWL_SCAN_TYPE_WILD;

	if (!data->active_vif)
		return IWL_SCAN_TYPE_UNASSOC;

	if ((load == IWL_MLD_TRAFFIC_HIGH || data->global_low_latency) &&
	    vif->type != NL80211_IFTYPE_P2P_DEVICE)
		return IWL_SCAN_TYPE_FRAGMENTED;

	/* In case of DCM with P2P GO set all scan requests as
	 * fast-balance scan
	 */
	if (vif->type == NL80211_IFTYPE_STATION &&
	    data->is_dcm_with_p2p_go)
		return IWL_SCAN_TYPE_FAST_BALANCE;

	if (load >= IWL_MLD_TRAFFIC_MEDIUM || data->global_low_latency)
		return IWL_SCAN_TYPE_MILD;

	return IWL_SCAN_TYPE_WILD;
}

static u8 *
iwl_mld_scan_add_2ghz_elems(struct iwl_mld *mld, const u8 *ies,
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

static void
iwl_mld_scan_add_tpc_report_elem(u8 *pos)
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

static u32
iwl_mld_scan_ooc_priority(enum iwl_mld_scan_status scan_status)
{
	if (scan_status == IWL_MLD_SCAN_REGULAR)
		return IWL_SCAN_PRIORITY_EXT_6;
	if (scan_status == IWL_MLD_SCAN_INT_MLO)
		return IWL_SCAN_PRIORITY_EXT_4;

	return IWL_SCAN_PRIORITY_EXT_2;
}

static bool
iwl_mld_scan_is_regular(struct iwl_mld_scan_params *params)
{
	return params->n_scan_plans == 1 &&
		params->scan_plans[0].iterations == 1;
}

static bool
iwl_mld_scan_is_fragmented(enum iwl_mld_scan_type type)
{
	return (type == IWL_SCAN_TYPE_FRAGMENTED ||
		type == IWL_SCAN_TYPE_FAST_BALANCE);
}

static int
iwl_mld_scan_uid_by_status(struct iwl_mld *mld, int status)
{
	for (int i = 0; i < ARRAY_SIZE(mld->scan.uid_status); i++)
		if (mld->scan.uid_status[i] == status)
			return i;

	return -ENOENT;
}

static const char *
iwl_mld_scan_ebs_status_str(enum iwl_scan_ebs_status status)
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

static int
iwl_mld_scan_ssid_exist(u8 *ssid, u8 ssid_len, struct iwl_ssid_ie *ssid_list)
{
	for (int i = 0; i < PROBE_OPTION_MAX; i++) {
		if (!ssid_list[i].len)
			return -1;
		if (ssid_list[i].len == ssid_len &&
		    !memcmp(ssid_list[i].ssid, ssid, ssid_len))
			return i;
	}

	return -1;
}

static bool
iwl_mld_scan_fits(struct iwl_mld *mld, int n_ssids,
		  struct ieee80211_scan_ies *ies, int n_channels)
{
	return ((n_ssids <= PROBE_OPTION_MAX) &&
		(n_channels <= mld->fw->ucode_capa.n_scan_channels) &&
		(ies->common_ie_len + ies->len[NL80211_BAND_2GHZ] +
		 ies->len[NL80211_BAND_5GHZ] + ies->len[NL80211_BAND_6GHZ] <=
		 iwl_mld_scan_max_template_size()));
}

static void
iwl_mld_scan_build_probe_req(struct iwl_mld *mld, struct ieee80211_vif *vif,
			     struct ieee80211_scan_ies *ies,
			     struct iwl_mld_scan_params *params)
{
	struct ieee80211_mgmt *frame = (void *)params->preq.buf;
	u8 *pos, *newpos;
	const u8 *mac_addr = params->flags & NL80211_SCAN_FLAG_RANDOM_ADDR ?
		params->mac_addr : NULL;

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

	/* Insert DS parameter set element on 2.4 GHz band */
	newpos = iwl_mld_scan_add_2ghz_elems(mld,
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

	iwl_mld_scan_add_tpc_report_elem(pos + ies->common_ie_len);
	params->preq.common_data.len = cpu_to_le16(ies->common_ie_len +
						   WFA_TPC_IE_LEN);
}

static u16
iwl_mld_scan_get_cmd_gen_flags(struct iwl_mld *mld,
			       struct iwl_mld_scan_params *params,
			       struct ieee80211_vif *vif,
			       enum iwl_mld_scan_status scan_status)
{
	u16 flags = 0;

	/* If no direct SSIDs are provided perform a passive scan. Otherwise,
	 * if there is a single SSID which is not the broadcast SSID, assume
	 * that the scan is intended for roaming purposes and thus enable Rx on
	 * all chains to improve chances of hearing the beacons/probe responses.
	 */
	if (params->n_ssids == 0)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_FORCE_PASSIVE;
	else if (params->n_ssids == 1 && params->ssids[0].ssid_len)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_USE_ALL_RX_CHAINS;

	if (params->pass_all)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_PASS_ALL;
	else
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_MATCH;

	if (iwl_mld_scan_is_fragmented(params->type))
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_FRAGMENTED_LMAC1;

	if (!iwl_mld_scan_is_regular(params))
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_PERIODIC;

	if (params->iter_notif ||
	    mld->scan.pass_all_sched_res == SCHED_SCAN_PASS_ALL_STATE_ENABLED)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_NTFY_ITER_COMPLETE;

	if (scan_status == IWL_MLD_SCAN_SCHED ||
	    scan_status == IWL_MLD_SCAN_NETDETECT)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_PREEMPTIVE;

	if (params->flags & (NL80211_SCAN_FLAG_ACCEPT_BCAST_PROBE_RESP |
			     NL80211_SCAN_FLAG_OCE_PROBE_REQ_HIGH_TX_RATE |
			     NL80211_SCAN_FLAG_FILS_MAX_CHANNEL_TIME))
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_OCE;

	if ((scan_status == IWL_MLD_SCAN_SCHED ||
	     scan_status == IWL_MLD_SCAN_NETDETECT) &&
	    params->flags & NL80211_SCAN_FLAG_COLOCATED_6GHZ)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_TRIGGER_UHB_SCAN;

	if (params->enable_6ghz_passive)
		flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_6GHZ_PASSIVE_SCAN;

	flags |= IWL_UMAC_SCAN_GEN_FLAGS_V2_ADAPTIVE_DWELL;

	return flags;
}

static u8
iwl_mld_scan_get_cmd_gen_flags2(struct iwl_mld *mld,
				struct iwl_mld_scan_params *params,
				struct ieee80211_vif *vif, u16 gen_flags)
{
	u8 flags = 0;

	/* TODO: CDB */
	if (params->respect_p2p_go)
		flags |= IWL_UMAC_SCAN_GEN_PARAMS_FLAGS2_RESPECT_P2P_GO_LB |
			IWL_UMAC_SCAN_GEN_PARAMS_FLAGS2_RESPECT_P2P_GO_HB;

	if (params->scan_6ghz)
		flags |= IWL_UMAC_SCAN_GEN_PARAMS_FLAGS2_DONT_TOGGLE_ANT;

	return flags;
}

static void
iwl_mld_scan_cmd_set_dwell(struct iwl_mld *mld,
			   struct iwl_scan_general_params_v11 *gp,
			   struct iwl_mld_scan_params *params)
{
	const struct iwl_mld_scan_timing_params *timing =
		&scan_timing[params->type];

	gp->adwell_default_social_chn =
	    IWL_SCAN_ADWELL_DEFAULT_N_APS_SOCIAL;
	gp->adwell_default_2g = IWL_SCAN_ADWELL_DEFAULT_LB_N_APS;
	gp->adwell_default_5g = IWL_SCAN_ADWELL_DEFAULT_HB_N_APS;

	if (params->n_ssids && params->ssids[0].ssid_len)
		gp->adwell_max_budget =
			cpu_to_le16(IWL_SCAN_ADWELL_MAX_BUDGET_DIRECTED_SCAN);
	else
		gp->adwell_max_budget =
			cpu_to_le16(IWL_SCAN_ADWELL_MAX_BUDGET_FULL_SCAN);

	gp->scan_priority = cpu_to_le32(IWL_SCAN_PRIORITY_EXT_6);

	gp->max_out_of_time[SCAN_LB_LMAC_IDX] = cpu_to_le32(timing->max_out_time);
	gp->suspend_time[SCAN_LB_LMAC_IDX] = cpu_to_le32(timing->suspend_time);

	gp->active_dwell[SCAN_LB_LMAC_IDX] = IWL_SCAN_DWELL_ACTIVE;
	gp->passive_dwell[SCAN_LB_LMAC_IDX] = IWL_SCAN_DWELL_PASSIVE;
	gp->active_dwell[SCAN_HB_LMAC_IDX] = IWL_SCAN_DWELL_ACTIVE;
	gp->passive_dwell[SCAN_HB_LMAC_IDX] = IWL_SCAN_DWELL_PASSIVE;

	IWL_DEBUG_SCAN(mld,
		       "Scan: adwell_max_budget=%d max_out_of_time=%d suspend_time=%d\n",
		       gp->adwell_max_budget,
		       gp->max_out_of_time[SCAN_LB_LMAC_IDX],
		       gp->suspend_time[SCAN_LB_LMAC_IDX]);
}

static void
iwl_mld_scan_cmd_set_gen_params(struct iwl_mld *mld,
				struct iwl_mld_scan_params *params,
				struct ieee80211_vif *vif,
				struct iwl_scan_general_params_v11 *gp,
				enum iwl_mld_scan_status scan_status)
{
	u16 gen_flags = iwl_mld_scan_get_cmd_gen_flags(mld, params, vif,
						       scan_status);
	u8 gen_flags2 = iwl_mld_scan_get_cmd_gen_flags2(mld, params, vif,
							gen_flags);

	IWL_DEBUG_SCAN(mld, "General: flags=0x%x, flags2=0x%x\n",
		       gen_flags, gen_flags2);

	gp->flags = cpu_to_le16(gen_flags);
	gp->flags2 = gen_flags2;

	iwl_mld_scan_cmd_set_dwell(mld, gp, params);

	if (gen_flags & IWL_UMAC_SCAN_GEN_FLAGS_V2_FRAGMENTED_LMAC1)
		gp->num_of_fragments[SCAN_LB_LMAC_IDX] = IWL_SCAN_NUM_OF_FRAGS;

	if (params->fw_link_id != IWL_MLD_INVALID_FW_ID)
		gp->scan_start_mac_or_link_id = params->fw_link_id;
}

static int
iwl_mld_scan_cmd_set_sched_params(struct iwl_mld_scan_params *params,
				  struct iwl_scan_umac_schedule *schedule,
				  __le16 *delay)
{
	if (WARN_ON(!params->n_scan_plans ||
		    params->n_scan_plans > IWL_MAX_SCHED_SCAN_PLANS))
		return -EINVAL;

	for (int i = 0; i < params->n_scan_plans; i++) {
		struct cfg80211_sched_scan_plan *scan_plan =
		    &params->scan_plans[i];

		schedule[i].iter_count = scan_plan->iterations;
		schedule[i].interval =
		    cpu_to_le16(scan_plan->interval);
	}

	/* If the number of iterations of the last scan plan is set to zero,
	 * it should run infinitely. However, this is not always the case.
	 * For example, when regular scan is requested the driver sets one scan
	 * plan with one iteration.
	 */
	if (!schedule[params->n_scan_plans - 1].iter_count)
		schedule[params->n_scan_plans - 1].iter_count = 0xff;

	*delay = cpu_to_le16(params->delay);

	return 0;
}

/* We insert the SSIDs in an inverted order, because the FW will
 * invert it back.
 */
static void
iwl_mld_scan_cmd_build_ssids(struct iwl_mld_scan_params *params,
			     struct iwl_ssid_ie *ssids, u32 *ssid_bitmap)
{
	int i, j;
	int index;
	u32 tmp_bitmap = 0;

	/* copy SSIDs from match list. iwl_config_sched_scan_profiles()
	 * uses the order of these ssids to config match list.
	 */
	for (i = 0, j = params->n_match_sets - 1;
	     j >= 0 && i < PROBE_OPTION_MAX;
	     i++, j--) {
		/* skip empty SSID match_sets */
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
		index = iwl_mld_scan_ssid_exist(params->ssids[j].ssid,
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

static void
iwl_mld_scan_fill_6g_chan_list(struct iwl_mld_scan_params *params,
			       struct iwl_scan_probe_params_v4 *pp)
{
	int j, idex_s = 0, idex_b = 0;
	struct cfg80211_scan_6ghz_params *scan_6ghz_params =
		params->scan_6ghz_params;

	for (j = 0;
	     j < params->n_ssids && idex_s < SCAN_SHORT_SSID_MAX_SIZE;
	     j++) {
		if (!params->ssids[j].ssid_len)
			continue;

		pp->short_ssid[idex_s] =
			cpu_to_le32(~crc32_le(~0, params->ssids[j].ssid,
					      params->ssids[j].ssid_len));

		/* hidden 6ghz scan */
		pp->direct_scan[idex_s].id = WLAN_EID_SSID;
		pp->direct_scan[idex_s].len = params->ssids[j].ssid_len;
		memcpy(pp->direct_scan[idex_s].ssid, params->ssids[j].ssid,
		       params->ssids[j].ssid_len);
		idex_s++;
	}

	/* Populate the arrays of the short SSIDs and the BSSIDs using the 6GHz
	 * collocated parameters. This might not be optimal, as this processing
	 * does not (yet) correspond to the actual channels, so it is possible
	 * that some entries would be left out.
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

static void
iwl_mld_scan_cmd_set_probe_params(struct iwl_mld_scan_params *params,
				  struct iwl_scan_probe_params_v4 *pp,
				  u32 *bitmap_ssid)
{
	pp->preq = params->preq;

	if (params->scan_6ghz) {
		iwl_mld_scan_fill_6g_chan_list(params, pp);
		return;
	}

	/* relevant only for 2.4 GHz /5 GHz scan */
	iwl_mld_scan_cmd_build_ssids(params, pp->direct_scan, bitmap_ssid);
}

static bool
iwl_mld_scan_use_ebs(struct iwl_mld *mld, struct ieee80211_vif *vif,
		     bool low_latency)
{
	const struct iwl_ucode_capabilities *capa = &mld->fw->ucode_capa;

	/* We can only use EBS if:
	 *	1. the feature is supported.
	 *	2. the last EBS was successful.
	 *	3. it's not a p2p find operation.
	 *	4. we are not in low latency mode,
	 *	   or if fragmented ebs is supported by the FW
	 *	5. the VIF is not an AP interface (scan wants survey results)
	 */
	return ((capa->flags & IWL_UCODE_TLV_FLAGS_EBS_SUPPORT) &&
		!mld->scan.last_ebs_failed &&
		vif->type != NL80211_IFTYPE_P2P_DEVICE &&
		(!low_latency || fw_has_api(capa, IWL_UCODE_TLV_API_FRAG_EBS)) &&
		ieee80211_vif_type_p2p(vif) != NL80211_IFTYPE_AP);
}

static u8
iwl_mld_scan_cmd_set_chan_flags(struct iwl_mld *mld,
				struct iwl_mld_scan_params *params,
				struct ieee80211_vif *vif,
				bool low_latency)
{
	u8 flags = 0;

	flags |= IWL_SCAN_CHANNEL_FLAG_ENABLE_CHAN_ORDER;

	if (iwl_mld_scan_use_ebs(mld, vif, low_latency))
		flags |= IWL_SCAN_CHANNEL_FLAG_EBS |
			 IWL_SCAN_CHANNEL_FLAG_EBS_ACCURATE |
			 IWL_SCAN_CHANNEL_FLAG_CACHE_ADD;

	/* set fragmented ebs for fragmented scan */
	if (iwl_mld_scan_is_fragmented(params->type))
		flags |= IWL_SCAN_CHANNEL_FLAG_EBS_FRAG;

	/* Force EBS in case the scan is a fragmented and there is a need
	 * to take P2P GO operation into consideration during scan operation.
	 */
	/* TODO: CDB */
	if (iwl_mld_scan_is_fragmented(params->type) &&
	    params->respect_p2p_go) {
		IWL_DEBUG_SCAN(mld, "Respect P2P GO. Force EBS\n");
		flags |= IWL_SCAN_CHANNEL_FLAG_FORCE_EBS;
	}

	return flags;
}

static const u8 p2p_go_friendly_chs[] = {
	36, 40, 44, 48, 149, 153, 157, 161, 165,
};

static const u8 social_chs[] = {
	1, 6, 11
};

static u32 iwl_mld_scan_ch_n_aps_flag(enum nl80211_iftype vif_type, u8 ch_id)
{
	if (vif_type != NL80211_IFTYPE_P2P_DEVICE)
		return 0;

	for (int i = 0; i < ARRAY_SIZE(p2p_go_friendly_chs); i++) {
		if (ch_id == p2p_go_friendly_chs[i])
			return IWL_SCAN_ADWELL_N_APS_GO_FRIENDLY_BIT;
	}

	for (int i = 0; i < ARRAY_SIZE(social_chs); i++) {
		if (ch_id == social_chs[i])
			return IWL_SCAN_ADWELL_N_APS_SOCIAL_CHS_BIT;
	}

	return 0;
}

static void
iwl_mld_scan_cmd_set_channels(struct iwl_mld *mld,
			      struct ieee80211_channel **channels,
			      struct iwl_scan_channel_params_v7 *cp,
			      int n_channels, u32 flags,
			      enum nl80211_iftype vif_type)
{
	for (int i = 0; i < n_channels; i++) {
		enum nl80211_band band = channels[i]->band;
		struct iwl_scan_channel_cfg_umac *cfg = &cp->channel_config[i];
		u8 iwl_band = iwl_mld_nl80211_band_to_fw(band);
		u32 n_aps_flag =
			iwl_mld_scan_ch_n_aps_flag(vif_type,
						   channels[i]->hw_value);

		if (IWL_MLD_ADAPTIVE_DWELL_NUM_APS_OVERRIDE)
			n_aps_flag = IWL_SCAN_ADWELL_N_APS_GO_FRIENDLY_BIT;

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
		cfg->flags |= cpu_to_le32(iwl_band <<
					  IWL_CHAN_CFG_FLAGS_BAND_POS);
	}
}

static u8
iwl_mld_scan_cfg_channels_6g(struct iwl_mld *mld,
			     struct iwl_mld_scan_params *params,
			     u32 n_channels,
			     struct iwl_scan_probe_params_v4 *pp,
			     struct iwl_scan_channel_params_v7 *cp,
			     enum nl80211_iftype vif_type)
{
	struct cfg80211_scan_6ghz_params *scan_6ghz_params =
		params->scan_6ghz_params;
	u32 i;
	u8 ch_cnt;

	for (i = 0, ch_cnt = 0; i < params->n_channels; i++) {
		struct iwl_scan_channel_cfg_umac *cfg =
			&cp->channel_config[ch_cnt];

		u32 s_ssid_bitmap = 0, bssid_bitmap = 0, flags = 0;
		u8 k, n_s_ssids = 0, n_bssids = 0;
		u8 max_s_ssids, max_bssids;
		bool force_passive = false, found = false, allow_passive = true,
		     unsolicited_probe_on_chan = false, psc_no_listen = false;
		s8 psd_20 = IEEE80211_RNR_TBTT_PARAMS_PSD_RESERVED;

		/* Avoid performing passive scan on non PSC channels unless the
		 * scan is specifically a passive scan, i.e., no SSIDs
		 * configured in the scan command.
		 */
		if (!cfg80211_channel_is_psc(params->channels[i]) &&
		    !params->n_6ghz_params && params->n_ssids)
			continue;

		cfg->channel_num = params->channels[i]->hw_value;
		cfg->flags |=
			cpu_to_le32(PHY_BAND_6 << IWL_CHAN_CFG_FLAGS_BAND_POS);

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

		/* In the following cases apply passive scan:
		 * 1. Non fragmented scan:
		 *	- PSC channel with NO_LISTEN_FLAG on should be treated
		 *	  like non PSC channel
		 *	- Non PSC channel with more than 3 short SSIDs or more
		 *	  than 9 BSSIDs.
		 *	- Non PSC Channel with unsolicited probe response and
		 *	  more than 2 short SSIDs or more than 6 BSSIDs.
		 *	- PSC channel with more than 2 short SSIDs or more than
		 *	  6 BSSIDs.
		 * 2. Fragmented scan:
		 *	- PSC channel with more than 1 SSID or 3 BSSIDs.
		 *	- Non PSC channel with more than 2 SSIDs or 6 BSSIDs.
		 *	- Non PSC channel with unsolicited probe response and
		 *	  more than 1 SSID or more than 3 BSSIDs.
		 */
		if (!iwl_mld_scan_is_fragmented(params->type)) {
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

		/* To optimize the scan time, i.e., reduce the scan dwell time
		 * on each channel, the below logic tries to set 3 direct BSSID
		 * probe requests for each broadcast probe request with a short
		 * SSID.
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

					/* Prefer creating BSSID entries unless
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
				if (memcmp(&pp->bssid_array[k],
					   scan_6ghz_params[j].bssid,
					   ETH_ALEN))
					continue;

				if (bssid_bitmap & BIT(k))
					break;

				if (n_bssids < max_bssids) {
					bssid_bitmap |= BIT(k);
					n_bssids++;
				} else {
					force_passive = TRUE;
				}

				break;
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
		cfg->v5.psd_20 = psd_20;

		ch_cnt++;
	}

	if (params->n_channels > ch_cnt)
		IWL_DEBUG_SCAN(mld,
			       "6GHz: reducing number channels: (%u->%u)\n",
			       params->n_channels, ch_cnt);

	return ch_cnt;
}

static int
iwl_mld_scan_cmd_set_6ghz_chan_params(struct iwl_mld *mld,
				      struct iwl_mld_scan_params *params,
				      struct ieee80211_vif *vif,
				      struct iwl_scan_req_params_v17 *scan_p,
				      enum iwl_mld_scan_status scan_status)
{
	struct iwl_scan_channel_params_v7 *chan_p = &scan_p->channel_params;
	struct iwl_scan_probe_params_v4 *probe_p = &scan_p->probe_params;

	chan_p->flags = iwl_mld_scan_get_cmd_gen_flags(mld, params, vif,
						       scan_status);
	chan_p->count = iwl_mld_scan_cfg_channels_6g(mld, params,
						     params->n_channels,
						     probe_p, chan_p,
						     vif->type);
	if (!chan_p->count)
		return -EINVAL;

	if (!params->n_ssids ||
	    (params->n_ssids == 1 && !params->ssids[0].ssid_len))
		chan_p->flags |= IWL_SCAN_CHANNEL_FLAG_6G_PSC_NO_FILTER;

	return 0;
}

static int
iwl_mld_scan_cmd_set_chan_params(struct iwl_mld *mld,
				 struct iwl_mld_scan_params *params,
				 struct ieee80211_vif *vif,
				 struct iwl_scan_req_params_v17 *scan_p,
				 bool low_latency,
				 enum iwl_mld_scan_status scan_status,
				 u32 channel_cfg_flags)
{
	struct iwl_scan_channel_params_v7 *cp = &scan_p->channel_params;
	struct ieee80211_supported_band *sband =
		&mld->nvm_data->bands[NL80211_BAND_6GHZ];

	cp->n_aps_override[0] = IWL_SCAN_ADWELL_N_APS_GO_FRIENDLY;
	cp->n_aps_override[1] = IWL_SCAN_ADWELL_N_APS_SOCIAL_CHS;

	if (IWL_MLD_ADAPTIVE_DWELL_NUM_APS_OVERRIDE)
		cp->n_aps_override[0] = IWL_MLD_ADAPTIVE_DWELL_NUM_APS_OVERRIDE;

	if (params->scan_6ghz)
		return iwl_mld_scan_cmd_set_6ghz_chan_params(mld, params,
							     vif, scan_p,
							     scan_status);

	/* relevant only for 2.4 GHz/5 GHz scan */
	cp->flags = iwl_mld_scan_cmd_set_chan_flags(mld, params, vif,
						    low_latency);
	cp->count = params->n_channels;

	iwl_mld_scan_cmd_set_channels(mld, params->channels, cp,
				      params->n_channels, channel_cfg_flags,
				      vif->type);

	if (!params->enable_6ghz_passive)
		return 0;

	/* fill 6 GHz passive scan cfg */
	for (int i = 0; i < sband->n_channels; i++) {
		struct ieee80211_channel *channel =
			&sband->channels[i];
		struct iwl_scan_channel_cfg_umac *cfg =
			&cp->channel_config[cp->count];

		if (!cfg80211_channel_is_psc(channel))
			continue;

		cfg->channel_num = channel->hw_value;
		cfg->v5.iter_count = 1;
		cfg->v5.iter_interval = 0;
		cfg->v5.psd_20 =
			IEEE80211_RNR_TBTT_PARAMS_PSD_RESERVED;
		cfg->flags = cpu_to_le32(PHY_BAND_6 <<
					 IWL_CHAN_CFG_FLAGS_BAND_POS);
		cp->count++;
	}

	return 0;
}

static int
iwl_mld_scan_build_cmd(struct iwl_mld *mld, struct ieee80211_vif *vif,
		       struct iwl_mld_scan_params *params,
		       enum iwl_mld_scan_status scan_status,
		       bool low_latency)
{
	struct iwl_scan_req_umac_v17 *cmd = mld->scan.cmd;
	struct iwl_scan_req_params_v17 *scan_p = &cmd->scan_params;
	u32 bitmap_ssid = 0;
	int uid, ret;

	memset(mld->scan.cmd, 0, mld->scan.cmd_size);

	/* find a free UID entry */
	uid = iwl_mld_scan_uid_by_status(mld, IWL_MLD_SCAN_NONE);
	if (uid < 0)
		return uid;

	cmd->uid = cpu_to_le32(uid);
	cmd->ooc_priority =
		cpu_to_le32(iwl_mld_scan_ooc_priority(scan_status));

	iwl_mld_scan_cmd_set_gen_params(mld, params, vif,
					&scan_p->general_params, scan_status);

	ret = iwl_mld_scan_cmd_set_sched_params(params,
						scan_p->periodic_params.schedule,
						&scan_p->periodic_params.delay);
	if (ret)
		return ret;

	iwl_mld_scan_cmd_set_probe_params(params, &scan_p->probe_params,
					  &bitmap_ssid);

	ret = iwl_mld_scan_cmd_set_chan_params(mld, params, vif, scan_p,
					       low_latency, scan_status,
					       bitmap_ssid);
	if (ret)
		return ret;

	return uid;
}

static bool
iwl_mld_scan_pass_all(struct iwl_mld *mld,
		      struct cfg80211_sched_scan_request *req)
{
	if (req->n_match_sets && req->match_sets[0].ssid.ssid_len) {
		IWL_DEBUG_SCAN(mld,
			       "Sending scheduled scan with filtering, n_match_sets %d\n",
			       req->n_match_sets);
		mld->scan.pass_all_sched_res = SCHED_SCAN_PASS_ALL_STATE_DISABLED;
		return false;
	}

	IWL_DEBUG_SCAN(mld, "Sending Scheduled scan without filtering\n");
	mld->scan.pass_all_sched_res = SCHED_SCAN_PASS_ALL_STATE_ENABLED;

	return true;
}

static int
iwl_mld_config_sched_scan_profiles(struct iwl_mld *mld,
				   struct cfg80211_sched_scan_request *req)
{
	struct iwl_host_cmd hcmd = {
		.id = SCAN_OFFLOAD_UPDATE_PROFILES_CMD,
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
	};
	struct iwl_scan_offload_profile *profile;
	struct iwl_scan_offload_profile_cfg_data *cfg_data;
	struct iwl_scan_offload_profile_cfg *profile_cfg;
	struct iwl_scan_offload_blocklist *blocklist;
	u32 blocklist_size = IWL_SCAN_MAX_BLACKLIST_LEN * sizeof(*blocklist);
	u32 cmd_size = blocklist_size + sizeof(*profile_cfg);
	u8 *cmd;
	int ret;

	if (WARN_ON(req->n_match_sets > IWL_SCAN_MAX_PROFILES_V2))
		return -EIO;

	cmd = kzalloc(cmd_size, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	hcmd.data[0] = cmd;
	hcmd.len[0] = cmd_size;

	blocklist = (struct iwl_scan_offload_blocklist *)cmd;
	profile_cfg = (struct iwl_scan_offload_profile_cfg *)(cmd + blocklist_size);

	/* No blocklist configuration */
	cfg_data = &profile_cfg->data;
	cfg_data->num_profiles = req->n_match_sets;
	cfg_data->active_clients = SCAN_CLIENT_SCHED_SCAN;
	cfg_data->pass_match = SCAN_CLIENT_SCHED_SCAN;
	cfg_data->match_notify = SCAN_CLIENT_SCHED_SCAN;

	if (!req->n_match_sets || !req->match_sets[0].ssid.ssid_len)
		cfg_data->any_beacon_notify = SCAN_CLIENT_SCHED_SCAN;

	for (int i = 0; i < req->n_match_sets; i++) {
		profile = &profile_cfg->profiles[i];

		/* Support any cipher and auth algorithm */
		profile->unicast_cipher = 0xff;
		profile->auth_alg = IWL_AUTH_ALGO_UNSUPPORTED |
			IWL_AUTH_ALGO_NONE | IWL_AUTH_ALGO_PSK |
			IWL_AUTH_ALGO_8021X | IWL_AUTH_ALGO_SAE |
			IWL_AUTH_ALGO_8021X_SHA384 | IWL_AUTH_ALGO_OWE;
		profile->network_type = IWL_NETWORK_TYPE_ANY;
		profile->band_selection = IWL_SCAN_OFFLOAD_SELECT_ANY;
		profile->client_bitmap = SCAN_CLIENT_SCHED_SCAN;
		profile->ssid_index = i;
	}

	IWL_DEBUG_SCAN(mld,
		       "Sending scheduled scan profile config (n_match_sets=%u)\n",
		       req->n_match_sets);

	ret = iwl_mld_send_cmd(mld, &hcmd);

	kfree(cmd);

	return ret;
}

static int
iwl_mld_sched_scan_handle_non_psc_channels(struct iwl_mld_scan_params *params,
					   bool *non_psc_included)
{
	int i, j;

	*non_psc_included = false;
	/* for 6 GHZ band only PSC channels need to be added */
	for (i = 0; i < params->n_channels; i++) {
		struct ieee80211_channel *channel = params->channels[i];

		if (channel->band == NL80211_BAND_6GHZ &&
		    !cfg80211_channel_is_psc(channel)) {
			*non_psc_included = true;
			break;
		}
	}

	if (!*non_psc_included)
		return 0;

	params->channels =
		kmemdup(params->channels,
			sizeof(params->channels[0]) * params->n_channels,
			GFP_KERNEL);
	if (!params->channels)
		return -ENOMEM;

	for (i = j = 0; i < params->n_channels; i++) {
		if (params->channels[i]->band == NL80211_BAND_6GHZ &&
		    !cfg80211_channel_is_psc(params->channels[i]))
			continue;
		params->channels[j++] = params->channels[i];
	}

	params->n_channels = j;

	return 0;
}

static void
iwl_mld_scan_6ghz_passive_scan(struct iwl_mld *mld,
			       struct iwl_mld_scan_params *params,
			       struct ieee80211_vif *vif)
{
	struct ieee80211_supported_band *sband =
		&mld->nvm_data->bands[NL80211_BAND_6GHZ];
	u32 n_disabled, i;

	params->enable_6ghz_passive = false;

	/* 6 GHz passive scan may be enabled in the first 2.4 GHz/5 GHz scan
	 * phase to discover geo location if no AP's are found. Skip it when
	 * we're in the 6 GHz scan phase.
	 */
	if (params->scan_6ghz)
		return;

	/* 6 GHz passive scan allowed only on station interface  */
	if (vif->type != NL80211_IFTYPE_STATION) {
		IWL_DEBUG_SCAN(mld,
			       "6GHz passive scan: not station interface\n");
		return;
	}

	/* 6 GHz passive scan is allowed in a defined time interval following
	 * HW reset or resume flow, or while not associated and a large
	 * interval has passed since the last 6 GHz passive scan.
	 */
	if ((vif->cfg.assoc ||
	     time_after(mld->scan.last_6ghz_passive_jiffies +
			(IWL_MLD_6GHZ_PASSIVE_SCAN_TIMEOUT * HZ), jiffies)) &&
	    (time_before(mld->scan.last_start_time_jiffies +
			 (IWL_MLD_6GHZ_PASSIVE_SCAN_ASSOC_TIMEOUT * HZ),
			 jiffies))) {
		IWL_DEBUG_SCAN(mld, "6GHz passive scan: %s\n",
			       vif->cfg.assoc ? "associated" :
			       "timeout did not expire");
		return;
	}

	/* not enough channels in the regular scan request */
	if (params->n_channels < IWL_MLD_6GHZ_PASSIVE_SCAN_MIN_CHANS) {
		IWL_DEBUG_SCAN(mld,
			       "6GHz passive scan: not enough channels %d\n",
			       params->n_channels);
		return;
	}

	for (i = 0; i < params->n_ssids; i++) {
		if (!params->ssids[i].ssid_len)
			break;
	}

	/* not a wildcard scan, so cannot enable passive 6 GHz scan */
	if (i == params->n_ssids) {
		IWL_DEBUG_SCAN(mld,
			       "6GHz passive scan: no wildcard SSID\n");
		return;
	}

	if (!sband || !sband->n_channels) {
		IWL_DEBUG_SCAN(mld,
			       "6GHz passive scan: no 6GHz channels\n");
		return;
	}

	for (i = 0, n_disabled = 0; i < sband->n_channels; i++) {
		if (sband->channels[i].flags & (IEEE80211_CHAN_DISABLED))
			n_disabled++;
	}

	/* Not all the 6 GHz channels are disabled, so no need for 6 GHz
	 * passive scan
	 */
	if (n_disabled != sband->n_channels) {
		IWL_DEBUG_SCAN(mld,
			       "6GHz passive scan: 6GHz channels enabled\n");
		return;
	}

	/* all conditions to enable 6 GHz passive scan are satisfied */
	IWL_DEBUG_SCAN(mld, "6GHz passive scan: can be enabled\n");
	params->enable_6ghz_passive = true;
}

static void
iwl_mld_scan_set_link_id(struct iwl_mld *mld, struct ieee80211_vif *vif,
			 struct iwl_mld_scan_params *params,
			 s8 tsf_report_link_id,
			 enum iwl_mld_scan_status scan_status)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_link *link;

	if (tsf_report_link_id < 0) {
		if (vif->active_links)
			tsf_report_link_id = __ffs(vif->active_links);
		else
			tsf_report_link_id = 0;
	}

	link = iwl_mld_link_dereference_check(mld_vif, tsf_report_link_id);
	if (!WARN_ON(!link)) {
		params->fw_link_id = link->fw_id;
		/* we to store fw_link_id only for regular scan,
		 * and use it in scan complete notif
		 */
		if (scan_status == IWL_MLD_SCAN_REGULAR)
			mld->scan.fw_link_id = link->fw_id;
	} else {
		mld->scan.fw_link_id = IWL_MLD_INVALID_FW_ID;
		params->fw_link_id = IWL_MLD_INVALID_FW_ID;
	}
}

static int
_iwl_mld_single_scan_start(struct iwl_mld *mld, struct ieee80211_vif *vif,
			   struct cfg80211_scan_request *req,
			   struct ieee80211_scan_ies *ies,
			   enum iwl_mld_scan_status scan_status)
{
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(LONG_GROUP, SCAN_REQ_UMAC),
		.len = { mld->scan.cmd_size, },
		.data = { mld->scan.cmd, },
		.dataflags = { IWL_HCMD_DFL_NOCOPY, },
	};
	struct iwl_mld_scan_iter_data scan_iter_data = {
		.current_vif = vif,
	};
	struct cfg80211_sched_scan_plan scan_plan = {.iterations = 1};
	struct iwl_mld_scan_params params = {};
	int ret, uid;

	/* we should have failed registration if scan_cmd was NULL */
	if (WARN_ON(!mld->scan.cmd))
		return -ENOMEM;

	if (!iwl_mld_scan_fits(mld, req->n_ssids, ies, req->n_channels))
		return -ENOBUFS;

	ieee80211_iterate_active_interfaces_mtx(mld->hw,
						IEEE80211_IFACE_ITER_NORMAL,
						iwl_mld_scan_iterator,
						&scan_iter_data);

	params.type = iwl_mld_get_scan_type(mld, vif, &scan_iter_data);
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

	params.n_6ghz_params = req->n_6ghz_params;
	params.scan_6ghz_params = req->scan_6ghz_params;
	params.scan_6ghz = req->scan_6ghz;

	ether_addr_copy(params.bssid, req->bssid);
	/* TODO: CDB - per-band flag */
	params.respect_p2p_go =
		iwl_mld_get_respect_p2p_go(mld, vif,
					   scan_iter_data.global_low_latency);

	if (req->duration)
		params.iter_notif = true;

	iwl_mld_scan_set_link_id(mld, vif, &params, req->tsf_report_link_id,
				 scan_status);

	iwl_mld_scan_build_probe_req(mld, vif, ies, &params);

	iwl_mld_scan_6ghz_passive_scan(mld, &params, vif);

	uid = iwl_mld_scan_build_cmd(mld, vif, &params, scan_status,
				     scan_iter_data.global_low_latency);
	if (uid < 0)
		return uid;

	ret = iwl_mld_send_cmd(mld, &hcmd);
	if (ret) {
		IWL_ERR(mld, "Scan failed! ret %d\n", ret);
		return ret;
	}

	IWL_DEBUG_SCAN(mld, "Scan request send success: status=%u, uid=%u\n",
		       scan_status, uid);

	mld->scan.uid_status[uid] = scan_status;
	mld->scan.status |= scan_status;

	if (params.enable_6ghz_passive)
		mld->scan.last_6ghz_passive_jiffies = jiffies;

	return 0;
}

static int
iwl_mld_scan_send_abort_cmd_status(struct iwl_mld *mld, int uid, u32 *status)
{
	struct iwl_umac_scan_abort abort_cmd = {
		.uid = cpu_to_le32(uid),
	};
	struct iwl_host_cmd cmd = {
		.id = WIDE_ID(LONG_GROUP, SCAN_ABORT_UMAC),
		.flags = CMD_WANT_SKB,
		.data = { &abort_cmd },
		.len[0] = sizeof(abort_cmd),
	};
	struct iwl_rx_packet *pkt;
	struct iwl_cmd_response *resp;
	u32 resp_len;
	int ret;

	ret = iwl_mld_send_cmd(mld, &cmd);
	if (ret)
		return ret;

	pkt = cmd.resp_pkt;

	resp_len = iwl_rx_packet_payload_len(pkt);
	if (IWL_FW_CHECK(mld, resp_len != sizeof(*resp),
			 "Scan Abort: unexpected response length %d\n",
			 resp_len)) {
		ret = -EIO;
		goto out;
	}

	resp = (void *)pkt->data;
	*status = le32_to_cpu(resp->status);

out:
	iwl_free_resp(&cmd);
	return ret;
}

static int
iwl_mld_scan_abort(struct iwl_mld *mld, int type, int uid, bool *wait)
{
	enum iwl_umac_scan_abort_status status;
	int ret;

	*wait = true;

	IWL_DEBUG_SCAN(mld, "Sending scan abort, uid %u\n", uid);

	ret = iwl_mld_scan_send_abort_cmd_status(mld, uid, &status);

	IWL_DEBUG_SCAN(mld, "Scan abort: ret=%d status=%u\n", ret, status);

	/* We don't need to wait to scan complete in the following cases:
	 * 1. Driver failed to send the scan abort cmd.
	 * 2. The FW is no longer familiar with the scan that needs to be
	 *    stopped. It is expected that the scan complete notification was
	 *    already received but not yet processed.
	 *
	 * In both cases the flow should continue similar to the case that the
	 * scan was really aborted.
	 */
	if (ret || status == IWL_UMAC_SCAN_ABORT_STATUS_NOT_FOUND)
		*wait = false;

	return ret;
}

static int
iwl_mld_scan_stop_wait(struct iwl_mld *mld, int type, int uid)
{
	struct iwl_notification_wait wait_scan_done;
	static const u16 scan_comp_notif[] = { SCAN_COMPLETE_UMAC };
	bool wait = true;
	int ret;

	iwl_init_notification_wait(&mld->notif_wait, &wait_scan_done,
				   scan_comp_notif,
				   ARRAY_SIZE(scan_comp_notif),
				   NULL, NULL);

	IWL_DEBUG_SCAN(mld, "Preparing to stop scan, type=%x\n", type);

	ret = iwl_mld_scan_abort(mld, type, uid, &wait);
	if (ret) {
		IWL_DEBUG_SCAN(mld, "couldn't stop scan type=%d\n", type);
		goto return_no_wait;
	}

	if (!wait) {
		IWL_DEBUG_SCAN(mld, "no need to wait for scan type=%d\n", type);
		goto return_no_wait;
	}

	return iwl_wait_notification(&mld->notif_wait, &wait_scan_done, HZ);

return_no_wait:
	iwl_remove_notification(&mld->notif_wait, &wait_scan_done);
	return ret;
}

int iwl_mld_sched_scan_start(struct iwl_mld *mld,
			     struct ieee80211_vif *vif,
			     struct cfg80211_sched_scan_request *req,
			     struct ieee80211_scan_ies *ies,
			     int type)
{
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(LONG_GROUP, SCAN_REQ_UMAC),
		.len = { mld->scan.cmd_size, },
		.data = { mld->scan.cmd, },
		.dataflags = { IWL_HCMD_DFL_NOCOPY, },
	};
	struct iwl_mld_scan_params params = {};
	struct iwl_mld_scan_iter_data scan_iter_data = {
		.current_vif = vif,
	};
	bool non_psc_included = false;
	int ret, uid;

	/* we should have failed registration if scan_cmd was NULL */
	if (WARN_ON(!mld->scan.cmd))
		return -ENOMEM;

	/* FW supports only a single periodic scan */
	if (mld->scan.status & (IWL_MLD_SCAN_SCHED | IWL_MLD_SCAN_NETDETECT))
		return -EBUSY;

	ieee80211_iterate_active_interfaces_mtx(mld->hw,
						IEEE80211_IFACE_ITER_NORMAL,
						iwl_mld_scan_iterator,
						&scan_iter_data);

	params.type = iwl_mld_get_scan_type(mld, vif, &scan_iter_data);
	params.flags = req->flags;
	params.n_ssids = req->n_ssids;
	params.ssids = req->ssids;
	params.n_channels = req->n_channels;
	params.channels = req->channels;
	params.mac_addr = req->mac_addr;
	params.mac_addr_mask = req->mac_addr_mask;
	params.no_cck = false;
	params.pass_all =  iwl_mld_scan_pass_all(mld, req);
	params.n_match_sets = req->n_match_sets;
	params.match_sets = req->match_sets;
	params.n_scan_plans = req->n_scan_plans;
	params.scan_plans = req->scan_plans;
	/* TODO: CDB - per-band flag */
	params.respect_p2p_go =
		iwl_mld_get_respect_p2p_go(mld, vif,
					   scan_iter_data.global_low_latency);

	/* UMAC scan supports up to 16-bit delays, trim it down to 16-bits */
	params.delay = req->delay > U16_MAX ? U16_MAX : req->delay;

	eth_broadcast_addr(params.bssid);

	ret = iwl_mld_config_sched_scan_profiles(mld, req);
	if (ret)
		return ret;

	iwl_mld_scan_build_probe_req(mld, vif, ies, &params);

	ret = iwl_mld_sched_scan_handle_non_psc_channels(&params,
							 &non_psc_included);
	if (ret)
		goto out;

	if (!iwl_mld_scan_fits(mld, req->n_ssids, ies, params.n_channels)) {
		ret = -ENOBUFS;
		goto out;
	}

	uid = iwl_mld_scan_build_cmd(mld, vif, &params, type,
				     scan_iter_data.global_low_latency);
	if (uid < 0) {
		ret = uid;
		goto out;
	}

	ret = iwl_mld_send_cmd(mld, &hcmd);
	if (!ret) {
		IWL_DEBUG_SCAN(mld,
			       "Sched scan request send success: type=%u, uid=%u\n",
			       type, uid);
		mld->scan.uid_status[uid] = type;
		mld->scan.status |= type;
	} else {
		IWL_ERR(mld, "Sched scan failed! ret %d\n", ret);
		mld->scan.pass_all_sched_res = SCHED_SCAN_PASS_ALL_STATE_DISABLED;
	}

out:
	if (non_psc_included)
		kfree(params.channels);
	return ret;
}

int iwl_mld_scan_stop(struct iwl_mld *mld, int type, bool notify)
{
	int uid, ret;

	IWL_DEBUG_SCAN(mld,
		       "Request to stop scan: type=0x%x, status=0x%x\n",
		       type, mld->scan.status);

	if (!(mld->scan.status & type))
		return 0;

	uid = iwl_mld_scan_uid_by_status(mld, type);
	/* must be valid, we just checked it's running */
	if (WARN_ON_ONCE(uid < 0))
		return uid;

	ret = iwl_mld_scan_stop_wait(mld, type, uid);
	if (ret)
		IWL_DEBUG_SCAN(mld, "Failed to stop scan\n");

	/* Clear the scan status so the next scan requests will
	 * succeed and mark the scan as stopping, so that the Rx
	 * handler doesn't do anything, as the scan was stopped from
	 * above. Also remove the handler to not notify mac80211
	 * erroneously after a new scan starts, for example.
	 */
	mld->scan.status &= ~type;
	mld->scan.uid_status[uid] = IWL_MLD_SCAN_NONE;
	iwl_mld_cancel_notifications_of_object(mld, IWL_MLD_OBJECT_TYPE_SCAN,
					       uid);

	if (type == IWL_MLD_SCAN_REGULAR) {
		if (notify) {
			struct cfg80211_scan_info info = {
				.aborted = true,
			};

			ieee80211_scan_completed(mld->hw, &info);
		}
	} else if (notify) {
		ieee80211_sched_scan_stopped(mld->hw);
		mld->scan.pass_all_sched_res = SCHED_SCAN_PASS_ALL_STATE_DISABLED;
	}

	return ret;
}

int iwl_mld_regular_scan_start(struct iwl_mld *mld, struct ieee80211_vif *vif,
			       struct cfg80211_scan_request *req,
			       struct ieee80211_scan_ies *ies)
{

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE)
		iwl_mld_emlsr_block_tmp_non_bss(mld);

	return _iwl_mld_single_scan_start(mld, vif, req, ies,
					  IWL_MLD_SCAN_REGULAR);
}

static void iwl_mld_int_mlo_scan_start(struct iwl_mld *mld,
				       struct ieee80211_vif *vif,
				       struct ieee80211_channel **channels,
				       size_t n_channels)
{
	struct cfg80211_scan_request *req __free(kfree) = NULL;
	struct ieee80211_scan_ies ies = {};
	size_t size;
	int ret;

	IWL_DEBUG_SCAN(mld, "Starting Internal MLO scan: n_channels=%zu\n",
		       n_channels);

	size = struct_size(req, channels, n_channels);
	req = kzalloc(size, GFP_KERNEL);
	if (!req)
		return;

	/* set the requested channels */
	for (int i = 0; i < n_channels; i++)
		req->channels[i] = channels[i];

	req->n_channels = n_channels;

	/* set the rates */
	for (int i = 0; i < NUM_NL80211_BANDS; i++)
		if (mld->wiphy->bands[i])
			req->rates[i] =
				(1 << mld->wiphy->bands[i]->n_bitrates) - 1;

	req->wdev = ieee80211_vif_to_wdev(vif);
	req->wiphy = mld->wiphy;
	req->scan_start = jiffies;
	req->tsf_report_link_id = -1;

	ret = _iwl_mld_single_scan_start(mld, vif, req, &ies,
					 IWL_MLD_SCAN_INT_MLO);

	if (!ret)
		mld->scan.last_mlo_scan_time = ktime_get_boottime_ns();

	IWL_DEBUG_SCAN(mld, "Internal MLO scan: ret=%d\n", ret);
}

#define IWL_MLD_MLO_SCAN_BLOCKOUT_TIME		5 /* seconds */

void iwl_mld_int_mlo_scan(struct iwl_mld *mld, struct ieee80211_vif *vif)
{
	struct ieee80211_channel *channels[IEEE80211_MLD_MAX_NUM_LINKS];
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	unsigned long usable_links = ieee80211_vif_usable_links(vif);
	size_t n_channels = 0;
	u8 link_id;

	lockdep_assert_wiphy(mld->wiphy);

	if (!IWL_MLD_AUTO_EML_ENABLE || !vif->cfg.assoc ||
	    !ieee80211_vif_is_mld(vif) || hweight16(vif->valid_links) == 1)
		return;

	if (mld->scan.status & IWL_MLD_SCAN_INT_MLO) {
		IWL_DEBUG_SCAN(mld, "Internal MLO scan is already running\n");
		return;
	}

	if (mld_vif->last_link_activation_time > ktime_get_boottime_seconds() -
						 IWL_MLD_MLO_SCAN_BLOCKOUT_TIME) {
		/* timing doesn't matter much, so use the blockout time */
		wiphy_delayed_work_queue(mld->wiphy,
					 &mld_vif->mlo_scan_start_wk,
					 IWL_MLD_MLO_SCAN_BLOCKOUT_TIME);
		return;
	}

	for_each_set_bit(link_id, &usable_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_bss_conf *link_conf =
			link_conf_dereference_check(vif, link_id);

		if (WARN_ON_ONCE(!link_conf))
			continue;

		channels[n_channels++] = link_conf->chanreq.oper.chan;
	}

	if (!n_channels)
		return;

	iwl_mld_int_mlo_scan_start(mld, vif, channels, n_channels);
}

void iwl_mld_handle_scan_iter_complete_notif(struct iwl_mld *mld,
					     struct iwl_rx_packet *pkt)
{
	struct iwl_umac_scan_iter_complete_notif *notif = (void *)pkt->data;
	u32 uid = __le32_to_cpu(notif->uid);

	if (IWL_FW_CHECK(mld, uid >= ARRAY_SIZE(mld->scan.uid_status),
			 "FW reports out-of-range scan UID %d\n", uid))
		return;

	if (mld->scan.uid_status[uid] == IWL_MLD_SCAN_REGULAR)
		mld->scan.start_tsf = le64_to_cpu(notif->start_tsf);

	IWL_DEBUG_SCAN(mld,
		       "UMAC Scan iteration complete: status=0x%x scanned_channels=%d\n",
		       notif->status, notif->scanned_channels);

	if (mld->scan.pass_all_sched_res == SCHED_SCAN_PASS_ALL_STATE_FOUND) {
		IWL_DEBUG_SCAN(mld, "Pass all scheduled scan results found\n");
		ieee80211_sched_scan_results(mld->hw);
		mld->scan.pass_all_sched_res = SCHED_SCAN_PASS_ALL_STATE_ENABLED;
	}

	IWL_DEBUG_SCAN(mld,
		       "UMAC Scan iteration complete: scan started at %llu (TSF)\n",
		       le64_to_cpu(notif->start_tsf));
}

void iwl_mld_handle_match_found_notif(struct iwl_mld *mld,
				      struct iwl_rx_packet *pkt)
{
	IWL_DEBUG_SCAN(mld, "Scheduled scan results\n");
	ieee80211_sched_scan_results(mld->hw);
}

void iwl_mld_handle_scan_complete_notif(struct iwl_mld *mld,
					struct iwl_rx_packet *pkt)
{
	struct iwl_umac_scan_complete *notif = (void *)pkt->data;
	bool aborted = (notif->status == IWL_SCAN_OFFLOAD_ABORTED);
	u32 uid = __le32_to_cpu(notif->uid);

	if (IWL_FW_CHECK(mld, uid >= ARRAY_SIZE(mld->scan.uid_status),
			 "FW reports out-of-range scan UID %d\n", uid))
		return;

	IWL_DEBUG_SCAN(mld,
		       "Scan completed: uid=%u type=%u, status=%s, EBS=%s\n",
		       uid, mld->scan.uid_status[uid],
		       notif->status == IWL_SCAN_OFFLOAD_COMPLETED ?
				"completed" : "aborted",
		       iwl_mld_scan_ebs_status_str(notif->ebs_status));
	IWL_DEBUG_SCAN(mld, "Scan completed: scan_status=0x%x\n",
		       mld->scan.status);
	IWL_DEBUG_SCAN(mld,
		       "Scan completed: line=%u, iter=%u, elapsed time=%u\n",
		       notif->last_schedule, notif->last_iter,
		       __le32_to_cpu(notif->time_from_last_iter));

	if (IWL_FW_CHECK(mld, !(mld->scan.uid_status[uid] & mld->scan.status),
			 "FW reports scan UID %d we didn't trigger\n", uid))
		return;

	/* if the scan is already stopping, we don't need to notify mac80211 */
	if (mld->scan.uid_status[uid] == IWL_MLD_SCAN_REGULAR) {
		struct cfg80211_scan_info info = {
			.aborted = aborted,
			.scan_start_tsf = mld->scan.start_tsf,
		};
		int fw_link_id = mld->scan.fw_link_id;
		struct ieee80211_bss_conf *link_conf = NULL;

		if (fw_link_id != IWL_MLD_INVALID_FW_ID)
			link_conf =
				wiphy_dereference(mld->wiphy,
						  mld->fw_id_to_bss_conf[fw_link_id]);

		/* It is possible that by the time the scan is complete the
		 * link was already removed and is not valid.
		 */
		if (link_conf)
			ether_addr_copy(info.tsf_bssid, link_conf->bssid);
		else
			IWL_DEBUG_SCAN(mld, "Scan link is no longer valid\n");

		ieee80211_scan_completed(mld->hw, &info);

		/* Scan is over, we can check again the tpt counters */
		iwl_mld_stop_ignoring_tpt_updates(mld);
	} else if (mld->scan.uid_status[uid] == IWL_MLD_SCAN_SCHED) {
		ieee80211_sched_scan_stopped(mld->hw);
		mld->scan.pass_all_sched_res = SCHED_SCAN_PASS_ALL_STATE_DISABLED;
	} else if (mld->scan.uid_status[uid] == IWL_MLD_SCAN_INT_MLO) {
		IWL_DEBUG_SCAN(mld, "Internal MLO scan completed\n");

		/*
		 * We limit link selection to internal MLO scans as otherwise
		 * we do not know whether all channels were covered.
		 */
		iwl_mld_select_links(mld);
	}

	mld->scan.status &= ~mld->scan.uid_status[uid];

	IWL_DEBUG_SCAN(mld, "Scan completed: after update: scan_status=0x%x\n",
		       mld->scan.status);

	mld->scan.uid_status[uid] = IWL_MLD_SCAN_NONE;

	if (notif->ebs_status != IWL_SCAN_EBS_SUCCESS &&
	    notif->ebs_status != IWL_SCAN_EBS_INACTIVE)
		mld->scan.last_ebs_failed = true;
}

/* This function is used in nic restart flow, to inform mac80211 about scans
 * that were aborted by restart flow or by an assert.
 */
void iwl_mld_report_scan_aborted(struct iwl_mld *mld)
{
	int uid;

	uid = iwl_mld_scan_uid_by_status(mld, IWL_MLD_SCAN_REGULAR);
	if (uid >= 0) {
		struct cfg80211_scan_info info = {
			.aborted = true,
		};

		ieee80211_scan_completed(mld->hw, &info);
		mld->scan.uid_status[uid] = IWL_MLD_SCAN_NONE;
	}

	uid = iwl_mld_scan_uid_by_status(mld, IWL_MLD_SCAN_SCHED);
	if (uid >= 0) {
		mld->scan.pass_all_sched_res = SCHED_SCAN_PASS_ALL_STATE_DISABLED;
		mld->scan.uid_status[uid] = IWL_MLD_SCAN_NONE;

		/* sched scan will be restarted by mac80211 in reconfig.
		 * report to mac80211 that sched scan stopped only if we won't
		 * restart the firmware.
		 */
		if (!iwlwifi_mod_params.fw_restart)
			ieee80211_sched_scan_stopped(mld->hw);
	}

	uid = iwl_mld_scan_uid_by_status(mld, IWL_MLD_SCAN_INT_MLO);
	if (uid >= 0) {
		IWL_DEBUG_SCAN(mld, "Internal MLO scan aborted\n");
		mld->scan.uid_status[uid] = IWL_MLD_SCAN_NONE;
	}

	BUILD_BUG_ON(IWL_MLD_SCAN_NONE != 0);
	memset(mld->scan.uid_status, 0, sizeof(mld->scan.uid_status));
}

int iwl_mld_alloc_scan_cmd(struct iwl_mld *mld)
{
	u8 scan_cmd_ver = iwl_fw_lookup_cmd_ver(mld->fw, SCAN_REQ_UMAC,
						IWL_FW_CMD_VER_UNKNOWN);
	size_t scan_cmd_size;

	if (scan_cmd_ver == 17) {
		scan_cmd_size = sizeof(struct iwl_scan_req_umac_v17);
	} else {
		IWL_ERR(mld, "Unexpected scan cmd version %d\n", scan_cmd_ver);
		return -EINVAL;
	}

	mld->scan.cmd = kmalloc(scan_cmd_size, GFP_KERNEL);
	if (!mld->scan.cmd)
		return -ENOMEM;

	mld->scan.cmd_size = scan_cmd_size;

	return 0;
}
