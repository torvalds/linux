// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2022 - 2024 Intel Corporation
 */
#include "mvm.h"
#include "time-event.h"

#define HANDLE_ESR_REASONS(HOW)		\
	HOW(BLOCKED_PREVENTION)		\
	HOW(BLOCKED_WOWLAN)		\
	HOW(BLOCKED_TPT)		\
	HOW(BLOCKED_FW)			\
	HOW(BLOCKED_NON_BSS)		\
	HOW(BLOCKED_ROC)		\
	HOW(BLOCKED_TMP_NON_BSS)	\
	HOW(EXIT_MISSED_BEACON)		\
	HOW(EXIT_LOW_RSSI)		\
	HOW(EXIT_COEX)			\
	HOW(EXIT_BANDWIDTH)		\
	HOW(EXIT_CSA)			\
	HOW(EXIT_LINK_USAGE)		\
	HOW(EXIT_FAIL_ENTRY)

static const char *const iwl_mvm_esr_states_names[] = {
#define NAME_ENTRY(x) [ilog2(IWL_MVM_ESR_##x)] = #x,
	HANDLE_ESR_REASONS(NAME_ENTRY)
};

const char *iwl_get_esr_state_string(enum iwl_mvm_esr_state state)
{
	int offs = ilog2(state);

	if (offs >= ARRAY_SIZE(iwl_mvm_esr_states_names) ||
	    !iwl_mvm_esr_states_names[offs])
		return "UNKNOWN";

	return iwl_mvm_esr_states_names[offs];
}

static void iwl_mvm_print_esr_state(struct iwl_mvm *mvm, u32 mask)
{
#define NAME_FMT(x) "%s"
#define NAME_PR(x) (mask & IWL_MVM_ESR_##x) ? "[" #x "]" : "",
	IWL_DEBUG_INFO(mvm,
		       "EMLSR state = " HANDLE_ESR_REASONS(NAME_FMT)
		       " (0x%x)\n",
		       HANDLE_ESR_REASONS(NAME_PR)
		       mask);
#undef NAME_FMT
#undef NAME_PR
}

static u32 iwl_mvm_get_free_fw_link_id(struct iwl_mvm *mvm,
				       struct iwl_mvm_vif *mvm_vif)
{
	u32 i;

	lockdep_assert_held(&mvm->mutex);

	for (i = 0; i < ARRAY_SIZE(mvm->link_id_to_link_conf); i++)
		if (!rcu_access_pointer(mvm->link_id_to_link_conf[i]))
			return i;

	return IWL_MVM_FW_LINK_ID_INVALID;
}

static int iwl_mvm_link_cmd_send(struct iwl_mvm *mvm,
				 struct iwl_link_config_cmd *cmd,
				 enum iwl_ctxt_action action)
{
	int ret;

	cmd->action = cpu_to_le32(action);
	ret = iwl_mvm_send_cmd_pdu(mvm,
				   WIDE_ID(MAC_CONF_GROUP, LINK_CONFIG_CMD), 0,
				   sizeof(*cmd), cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send LINK_CONFIG_CMD (action:%d): %d\n",
			action, ret);
	return ret;
}

int iwl_mvm_set_link_mapping(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_vif_link_info *link_info =
		mvmvif->link[link_conf->link_id];

	if (link_info->fw_link_id == IWL_MVM_FW_LINK_ID_INVALID) {
		link_info->fw_link_id = iwl_mvm_get_free_fw_link_id(mvm,
								    mvmvif);
		if (link_info->fw_link_id >=
		    ARRAY_SIZE(mvm->link_id_to_link_conf))
			return -EINVAL;

		rcu_assign_pointer(mvm->link_id_to_link_conf[link_info->fw_link_id],
				   link_conf);
	}

	return 0;
}

int iwl_mvm_add_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		     struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	unsigned int link_id = link_conf->link_id;
	struct iwl_mvm_vif_link_info *link_info = mvmvif->link[link_id];
	struct iwl_link_config_cmd cmd = {};
	unsigned int cmd_id = WIDE_ID(MAC_CONF_GROUP, LINK_CONFIG_CMD);
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd_id, 1);
	int ret;

	if (WARN_ON_ONCE(!link_info))
		return -EINVAL;

	ret = iwl_mvm_set_link_mapping(mvm, vif, link_conf);
	if (ret)
		return ret;

	/* Update SF - Disable if needed. if this fails, SF might still be on
	 * while many macs are bound, which is forbidden - so fail the binding.
	 */
	if (iwl_mvm_sf_update(mvm, vif, false))
		return -EINVAL;

	cmd.link_id = cpu_to_le32(link_info->fw_link_id);
	cmd.mac_id = cpu_to_le32(mvmvif->id);
	cmd.spec_link_id = link_conf->link_id;
	WARN_ON_ONCE(link_info->phy_ctxt);
	cmd.phy_id = cpu_to_le32(FW_CTXT_INVALID);

	memcpy(cmd.local_link_addr, link_conf->addr, ETH_ALEN);

	if (vif->type == NL80211_IFTYPE_ADHOC && link_conf->bssid)
		memcpy(cmd.ibss_bssid_addr, link_conf->bssid, ETH_ALEN);

	if (cmd_ver < 2)
		cmd.listen_lmac = cpu_to_le32(link_info->listen_lmac);

	return iwl_mvm_link_cmd_send(mvm, &cmd, FW_CTXT_ACTION_ADD);
}

struct iwl_mvm_esr_iter_data {
	struct ieee80211_vif *vif;
	unsigned int link_id;
	bool lift_block;
};

static void iwl_mvm_esr_vif_iterator(void *_data, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct iwl_mvm_esr_iter_data *data = _data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int link_id;

	if (ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_STATION)
		return;

	for_each_mvm_vif_valid_link(mvmvif, link_id) {
		struct iwl_mvm_vif_link_info *link_info =
			mvmvif->link[link_id];
		if (vif == data->vif && link_id == data->link_id)
			continue;
		if (link_info->active)
			data->lift_block = false;
	}
}

int iwl_mvm_esr_non_bss_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			     unsigned int link_id, bool active)
{
	/* An active link of a non-station vif blocks EMLSR. Upon activation
	 * block EMLSR on the bss vif. Upon deactivation, check if this link
	 * was the last non-station link active, and if so unblock the bss vif
	 */
	struct ieee80211_vif *bss_vif = iwl_mvm_get_bss_vif(mvm);
	struct iwl_mvm_esr_iter_data data = {
		.vif = vif,
		.link_id = link_id,
		.lift_block = true,
	};

	if (IS_ERR_OR_NULL(bss_vif))
		return 0;

	if (active)
		return iwl_mvm_block_esr_sync(mvm, bss_vif,
					      IWL_MVM_ESR_BLOCKED_NON_BSS);

	ieee80211_iterate_active_interfaces(mvm->hw,
					    IEEE80211_IFACE_ITER_NORMAL,
					    iwl_mvm_esr_vif_iterator, &data);
	if (data.lift_block) {
		mutex_lock(&mvm->mutex);
		iwl_mvm_unblock_esr(mvm, bss_vif, IWL_MVM_ESR_BLOCKED_NON_BSS);
		mutex_unlock(&mvm->mutex);
	}

	return 0;
}

int iwl_mvm_link_changed(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			 struct ieee80211_bss_conf *link_conf,
			 u32 changes, bool active)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	unsigned int link_id = link_conf->link_id;
	struct iwl_mvm_vif_link_info *link_info = mvmvif->link[link_id];
	struct iwl_mvm_phy_ctxt *phyctxt;
	struct iwl_link_config_cmd cmd = {};
	u32 ht_flag, flags = 0, flags_mask = 0;
	int ret;
	unsigned int cmd_id = WIDE_ID(MAC_CONF_GROUP, LINK_CONFIG_CMD);
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd_id, 1);

	if (WARN_ON_ONCE(!link_info ||
			 link_info->fw_link_id == IWL_MVM_FW_LINK_ID_INVALID))
		return -EINVAL;

	if (changes & LINK_CONTEXT_MODIFY_ACTIVE) {
		/* When activating a link, phy context should be valid;
		 * when deactivating a link, it also should be valid since
		 * the link was active before. So, do nothing in this case.
		 * Since a link is added first with FW_CTXT_INVALID, then we
		 * can get here in case it's removed before it was activated.
		 */
		if (!link_info->phy_ctxt)
			return 0;

		/* Catch early if driver tries to activate or deactivate a link
		 * twice.
		 */
		WARN_ON_ONCE(active == link_info->active);

		/* When deactivating a link session protection should
		 * be stopped. Also let the firmware know if we can't Tx.
		 */
		if (!active && vif->type == NL80211_IFTYPE_STATION) {
			iwl_mvm_stop_session_protection(mvm, vif);
			if (link_info->csa_block_tx) {
				cmd.block_tx = 1;
				link_info->csa_block_tx = false;
			}
		}
	}

	cmd.link_id = cpu_to_le32(link_info->fw_link_id);

	/* The phy_id, link address and listen_lmac can be modified only until
	 * the link becomes active, otherwise they will be ignored.
	 */
	phyctxt = link_info->phy_ctxt;
	if (phyctxt)
		cmd.phy_id = cpu_to_le32(phyctxt->id);
	else
		cmd.phy_id = cpu_to_le32(FW_CTXT_INVALID);
	cmd.mac_id = cpu_to_le32(mvmvif->id);

	memcpy(cmd.local_link_addr, link_conf->addr, ETH_ALEN);

	cmd.active = cpu_to_le32(active);

	if (vif->type == NL80211_IFTYPE_ADHOC && link_conf->bssid)
		memcpy(cmd.ibss_bssid_addr, link_conf->bssid, ETH_ALEN);

	iwl_mvm_set_fw_basic_rates(mvm, vif, link_info,
				   &cmd.cck_rates, &cmd.ofdm_rates);

	cmd.cck_short_preamble = cpu_to_le32(link_conf->use_short_preamble);
	cmd.short_slot = cpu_to_le32(link_conf->use_short_slot);

	/* The fw does not distinguish between ht and fat */
	ht_flag = LINK_PROT_FLG_HT_PROT | LINK_PROT_FLG_FAT_PROT;
	iwl_mvm_set_fw_protection_flags(mvm, vif, link_conf,
					&cmd.protection_flags,
					ht_flag, LINK_PROT_FLG_TGG_PROTECT);

	iwl_mvm_set_fw_qos_params(mvm, vif, link_conf, cmd.ac,
				  &cmd.qos_flags);


	cmd.bi = cpu_to_le32(link_conf->beacon_int);
	cmd.dtim_interval = cpu_to_le32(link_conf->beacon_int *
					link_conf->dtim_period);

	if (!link_conf->he_support || iwlwifi_mod_params.disable_11ax ||
	    (vif->type == NL80211_IFTYPE_STATION && !vif->cfg.assoc)) {
		changes &= ~LINK_CONTEXT_MODIFY_HE_PARAMS;
		goto send_cmd;
	}

	cmd.htc_trig_based_pkt_ext = link_conf->htc_trig_based_pkt_ext;

	if (link_conf->uora_exists) {
		cmd.rand_alloc_ecwmin =
			link_conf->uora_ocw_range & 0x7;
		cmd.rand_alloc_ecwmax =
			(link_conf->uora_ocw_range >> 3) & 0x7;
	}

	/* ap_sta may be NULL if we're disconnecting */
	if (changes & LINK_CONTEXT_MODIFY_HE_PARAMS && mvmvif->ap_sta) {
		struct ieee80211_link_sta *link_sta =
			link_sta_dereference_check(mvmvif->ap_sta, link_id);

		if (!WARN_ON(!link_sta) && link_sta->he_cap.has_he &&
		    link_sta->he_cap.he_cap_elem.mac_cap_info[5] &
		    IEEE80211_HE_MAC_CAP5_OM_CTRL_UL_MU_DATA_DIS_RX)
			cmd.ul_mu_data_disable = 1;
	}

	/* TODO  how to set ndp_fdbk_buff_th_exp? */

	if (iwl_mvm_set_fw_mu_edca_params(mvm, mvmvif->link[link_id],
					  &cmd.trig_based_txf[0])) {
		flags |= LINK_FLG_MU_EDCA_CW;
		flags_mask |= LINK_FLG_MU_EDCA_CW;
	}

	if (changes & LINK_CONTEXT_MODIFY_EHT_PARAMS) {
		struct ieee80211_chanctx_conf *ctx;
		struct cfg80211_chan_def *def = NULL;

		rcu_read_lock();
		ctx = rcu_dereference(link_conf->chanctx_conf);
		if (ctx)
			def = iwl_mvm_chanctx_def(mvm, ctx);

		if (iwlwifi_mod_params.disable_11be ||
		    !link_conf->eht_support || !def ||
		    iwl_fw_lookup_cmd_ver(mvm->fw, PHY_CONTEXT_CMD, 1) >= 6)
			changes &= ~LINK_CONTEXT_MODIFY_EHT_PARAMS;
		else
			cmd.puncture_mask = cpu_to_le16(def->punctured);
		rcu_read_unlock();
	}

	cmd.bss_color = link_conf->he_bss_color.color;

	if (!link_conf->he_bss_color.enabled) {
		flags |= LINK_FLG_BSS_COLOR_DIS;
		flags_mask |= LINK_FLG_BSS_COLOR_DIS;
	}

	cmd.frame_time_rts_th = cpu_to_le16(link_conf->frame_time_rts_th);

	/* Block 26-tone RU OFDMA transmissions */
	if (link_info->he_ru_2mhz_block) {
		flags |= LINK_FLG_RU_2MHZ_BLOCK;
		flags_mask |= LINK_FLG_RU_2MHZ_BLOCK;
	}

	if (link_conf->nontransmitted) {
		ether_addr_copy(cmd.ref_bssid_addr,
				link_conf->transmitter_bssid);
		cmd.bssid_index = link_conf->bssid_index;
	}

send_cmd:
	cmd.modify_mask = cpu_to_le32(changes);
	cmd.flags = cpu_to_le32(flags);
	if (cmd_ver < 6)
		cmd.flags_mask = cpu_to_le32(flags_mask);
	cmd.spec_link_id = link_conf->link_id;
	if (cmd_ver < 2)
		cmd.listen_lmac = cpu_to_le32(link_info->listen_lmac);

	ret = iwl_mvm_link_cmd_send(mvm, &cmd, FW_CTXT_ACTION_MODIFY);
	if (!ret && (changes & LINK_CONTEXT_MODIFY_ACTIVE))
		link_info->active = active;

	return ret;
}

int iwl_mvm_unset_link_mapping(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			       struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_vif_link_info *link_info =
		mvmvif->link[link_conf->link_id];

	/* mac80211 thought we have the link, but it was never configured */
	if (WARN_ON(!link_info ||
		    link_info->fw_link_id >=
		    ARRAY_SIZE(mvm->link_id_to_link_conf)))
		return -EINVAL;

	RCU_INIT_POINTER(mvm->link_id_to_link_conf[link_info->fw_link_id],
			 NULL);
	return 0;
}

int iwl_mvm_remove_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	unsigned int link_id = link_conf->link_id;
	struct iwl_mvm_vif_link_info *link_info = mvmvif->link[link_id];
	struct iwl_link_config_cmd cmd = {};
	int ret;

	ret = iwl_mvm_unset_link_mapping(mvm, vif, link_conf);
	if (ret)
		return 0;

	cmd.link_id = cpu_to_le32(link_info->fw_link_id);
	link_info->fw_link_id = IWL_MVM_FW_LINK_ID_INVALID;
	cmd.spec_link_id = link_conf->link_id;
	cmd.phy_id = cpu_to_le32(FW_CTXT_INVALID);

	ret = iwl_mvm_link_cmd_send(mvm, &cmd, FW_CTXT_ACTION_REMOVE);

	if (!ret)
		if (iwl_mvm_sf_update(mvm, vif, true))
			IWL_ERR(mvm, "Failed to update SF state\n");

	return ret;
}

/* link should be deactivated before removal, so in most cases we need to
 * perform these two operations together
 */
int iwl_mvm_disable_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			 struct ieee80211_bss_conf *link_conf)
{
	int ret;

	ret = iwl_mvm_link_changed(mvm, vif, link_conf,
				   LINK_CONTEXT_MODIFY_ACTIVE, false);
	if (ret)
		return ret;

	ret = iwl_mvm_remove_link(mvm, vif, link_conf);
	if (ret)
		return ret;

	return ret;
}

struct iwl_mvm_rssi_to_grade {
	s8 rssi[2];
	u16 grade;
};

#define RSSI_TO_GRADE_LINE(_lb, _hb_uhb, _grade) \
	{ \
		.rssi = {_lb, _hb_uhb}, \
		.grade = _grade \
	}

/*
 * This array must be sorted by increasing RSSI for proper functionality.
 * The grades are actually estimated throughput, represented as fixed-point
 * with a scale factor of 1/10.
 */
static const struct iwl_mvm_rssi_to_grade rssi_to_grade_map[] = {
	RSSI_TO_GRADE_LINE(-85, -89, 177),
	RSSI_TO_GRADE_LINE(-83, -86, 344),
	RSSI_TO_GRADE_LINE(-82, -85, 516),
	RSSI_TO_GRADE_LINE(-80, -83, 688),
	RSSI_TO_GRADE_LINE(-77, -79, 1032),
	RSSI_TO_GRADE_LINE(-73, -76, 1376),
	RSSI_TO_GRADE_LINE(-70, -74, 1548),
	RSSI_TO_GRADE_LINE(-69, -72, 1750),
	RSSI_TO_GRADE_LINE(-65, -68, 2064),
	RSSI_TO_GRADE_LINE(-61, -66, 2294),
	RSSI_TO_GRADE_LINE(-58, -61, 2580),
	RSSI_TO_GRADE_LINE(-55, -58, 2868),
	RSSI_TO_GRADE_LINE(-46, -55, 3098),
	RSSI_TO_GRADE_LINE(-43, -54, 3442)
};

#define MAX_GRADE (rssi_to_grade_map[ARRAY_SIZE(rssi_to_grade_map) - 1].grade)

#define DEFAULT_CHAN_LOAD_LB	30
#define DEFAULT_CHAN_LOAD_HB	15
#define DEFAULT_CHAN_LOAD_UHB	0

/* Factors calculation is done with fixed-point with a scaling factor of 1/256 */
#define SCALE_FACTOR 256

/* Convert a percentage from [0,100] to [0,255] */
#define NORMALIZE_PERCENT_TO_255(percentage) ((percentage) * SCALE_FACTOR / 100)

static unsigned int
iwl_mvm_get_puncturing_factor(const struct ieee80211_bss_conf *link_conf)
{
	enum nl80211_chan_width chan_width =
		link_conf->chanreq.oper.width;
	int mhz = nl80211_chan_width_to_mhz(chan_width);
	unsigned int n_subchannels, n_punctured, puncturing_penalty;

	if (WARN_ONCE(mhz < 20 || mhz > 320,
		      "Invalid channel width : (%d)\n", mhz))
		return SCALE_FACTOR;

	/* No puncturing, no penalty */
	if (mhz < 80)
		return SCALE_FACTOR;

	/* total number of subchannels */
	n_subchannels = mhz / 20;
	/* how many of these are punctured */
	n_punctured = hweight16(link_conf->chanreq.oper.punctured);

	puncturing_penalty = n_punctured * SCALE_FACTOR / n_subchannels;
	return SCALE_FACTOR - puncturing_penalty;
}

static unsigned int
iwl_mvm_get_chan_load(struct ieee80211_bss_conf *link_conf)
{
	struct ieee80211_vif *vif = link_conf->vif;
	struct iwl_mvm_vif_link_info *mvm_link =
		iwl_mvm_vif_from_mac80211(link_conf->vif)->link[link_conf->link_id];
	const struct element *bss_load_elem;
	const struct ieee80211_bss_load_elem *bss_load;
	enum nl80211_band band = link_conf->chanreq.oper.chan->band;
	const struct cfg80211_bss_ies *ies;
	unsigned int chan_load;
	u32 chan_load_by_us;

	rcu_read_lock();
	if (ieee80211_vif_link_active(vif, link_conf->link_id))
		ies = rcu_dereference(link_conf->bss->beacon_ies);
	else
		ies = rcu_dereference(link_conf->bss->ies);

	if (ies)
		bss_load_elem = cfg80211_find_elem(WLAN_EID_QBSS_LOAD,
						   ies->data, ies->len);
	else
		bss_load_elem = NULL;

	/* If there isn't BSS Load element, take the defaults */
	if (!bss_load_elem ||
	    bss_load_elem->datalen != sizeof(*bss_load)) {
		rcu_read_unlock();
		switch (band) {
		case NL80211_BAND_2GHZ:
			chan_load = DEFAULT_CHAN_LOAD_LB;
			break;
		case NL80211_BAND_5GHZ:
			chan_load = DEFAULT_CHAN_LOAD_HB;
			break;
		case NL80211_BAND_6GHZ:
			chan_load = DEFAULT_CHAN_LOAD_UHB;
			break;
		default:
			chan_load = 0;
			break;
		}
		/* The defaults are given in percentage */
		return NORMALIZE_PERCENT_TO_255(chan_load);
	}

	bss_load = (const void *)bss_load_elem->data;
	/* Channel util is in range 0-255 */
	chan_load = bss_load->channel_util;
	rcu_read_unlock();

	if (!mvm_link || !mvm_link->active)
		return chan_load;

	if (WARN_ONCE(!mvm_link->phy_ctxt,
		      "Active link (%u) without phy ctxt assigned!\n",
		      link_conf->link_id))
		return chan_load;

	/* channel load by us is given in percentage */
	chan_load_by_us =
		NORMALIZE_PERCENT_TO_255(mvm_link->phy_ctxt->channel_load_by_us);

	/* Use only values that firmware sends that can possibly be valid */
	if (chan_load_by_us <= chan_load)
		chan_load -= chan_load_by_us;

	return chan_load;
}

static unsigned int
iwl_mvm_get_chan_load_factor(struct ieee80211_bss_conf *link_conf)
{
	return SCALE_FACTOR - iwl_mvm_get_chan_load(link_conf);
}

/* This function calculates the grade of a link. Returns 0 in error case */
VISIBLE_IF_IWLWIFI_KUNIT
unsigned int iwl_mvm_get_link_grade(struct ieee80211_bss_conf *link_conf)
{
	enum nl80211_band band;
	int i, rssi_idx;
	s32 link_rssi;
	unsigned int grade = MAX_GRADE;

	if (WARN_ON_ONCE(!link_conf))
		return 0;

	band = link_conf->chanreq.oper.chan->band;
	if (WARN_ONCE(band != NL80211_BAND_2GHZ &&
		      band != NL80211_BAND_5GHZ &&
		      band != NL80211_BAND_6GHZ,
		      "Invalid band (%u)\n", band))
		return 0;

	link_rssi = MBM_TO_DBM(link_conf->bss->signal);
	/*
	 * For 6 GHz the RSSI of the beacons is lower than
	 * the RSSI of the data.
	 */
	if (band == NL80211_BAND_6GHZ)
		link_rssi += 4;

	rssi_idx = band == NL80211_BAND_2GHZ ? 0 : 1;

	/* No valid RSSI - take the lowest grade */
	if (!link_rssi)
		link_rssi = rssi_to_grade_map[0].rssi[rssi_idx];

	/* Get grade based on RSSI */
	for (i = 0; i < ARRAY_SIZE(rssi_to_grade_map); i++) {
		const struct iwl_mvm_rssi_to_grade *line =
			&rssi_to_grade_map[i];

		if (link_rssi > line->rssi[rssi_idx])
			continue;
		grade = line->grade;
		break;
	}

	/* apply the channel load and puncturing factors */
	grade = grade * iwl_mvm_get_chan_load_factor(link_conf) / SCALE_FACTOR;
	grade = grade * iwl_mvm_get_puncturing_factor(link_conf) / SCALE_FACTOR;
	return grade;
}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mvm_get_link_grade);

static
u8 iwl_mvm_set_link_selection_data(struct ieee80211_vif *vif,
				   struct iwl_mvm_link_sel_data *data,
				   unsigned long usable_links,
				   u8 *best_link_idx)
{
	u8 n_data = 0;
	u16 max_grade = 0;
	unsigned long link_id;

	/* TODO: don't select links that weren't discovered in the last scan */
	for_each_set_bit(link_id, &usable_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_bss_conf *link_conf =
			link_conf_dereference_protected(vif, link_id);

		if (WARN_ON_ONCE(!link_conf))
			continue;

		data[n_data].link_id = link_id;
		data[n_data].chandef = &link_conf->chanreq.oper;
		data[n_data].signal = link_conf->bss->signal / 100;
		data[n_data].grade = iwl_mvm_get_link_grade(link_conf);

		if (data[n_data].grade > max_grade) {
			max_grade = data[n_data].grade;
			*best_link_idx = n_data;
		}
		n_data++;
	}

	return n_data;
}

struct iwl_mvm_bw_to_rssi_threshs {
	s8 low;
	s8 high;
};

#define BW_TO_RSSI_THRESHOLDS(_bw)				\
	[IWL_PHY_CHANNEL_MODE ## _bw] = {			\
		.low = IWL_MVM_LOW_RSSI_THRESH_##_bw##MHZ,	\
		.high = IWL_MVM_HIGH_RSSI_THRESH_##_bw##MHZ	\
	}

s8 iwl_mvm_get_esr_rssi_thresh(struct iwl_mvm *mvm,
			       const struct cfg80211_chan_def *chandef,
			       bool low)
{
	const struct iwl_mvm_bw_to_rssi_threshs bw_to_rssi_threshs_map[] = {
		BW_TO_RSSI_THRESHOLDS(20),
		BW_TO_RSSI_THRESHOLDS(40),
		BW_TO_RSSI_THRESHOLDS(80),
		BW_TO_RSSI_THRESHOLDS(160)
		/* 320 MHz has the same thresholds as 20 MHz */
	};
	const struct iwl_mvm_bw_to_rssi_threshs *threshs;
	u8 chan_width = iwl_mvm_get_channel_width(chandef);

	if (WARN_ON(chandef->chan->band != NL80211_BAND_2GHZ &&
		    chandef->chan->band != NL80211_BAND_5GHZ &&
		    chandef->chan->band != NL80211_BAND_6GHZ))
		return S8_MAX;

	/* 6 GHz will always use 20 MHz thresholds, regardless of the BW */
	if (chan_width == IWL_PHY_CHANNEL_MODE320)
		chan_width = IWL_PHY_CHANNEL_MODE20;

	threshs = &bw_to_rssi_threshs_map[chan_width];

	return low ? threshs->low : threshs->high;
}

static u32
iwl_mvm_esr_disallowed_with_link(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 const struct iwl_mvm_link_sel_data *link,
				 bool primary)
{
	struct wiphy *wiphy = mvm->hw->wiphy;
	struct ieee80211_bss_conf *conf;
	enum iwl_mvm_esr_state ret = 0;
	s8 thresh;

	conf = wiphy_dereference(wiphy, vif->link_conf[link->link_id]);
	if (WARN_ON_ONCE(!conf))
		return false;

	/* BT Coex effects eSR mode only if one of the links is on LB */
	if (link->chandef->chan->band == NL80211_BAND_2GHZ &&
	    (!iwl_mvm_bt_coex_calculate_esr_mode(mvm, vif, link->signal,
						 primary)))
		ret |= IWL_MVM_ESR_EXIT_COEX;

	thresh = iwl_mvm_get_esr_rssi_thresh(mvm, link->chandef,
					     false);

	if (link->signal < thresh)
		ret |= IWL_MVM_ESR_EXIT_LOW_RSSI;

	if (conf->csa_active)
		ret |= IWL_MVM_ESR_EXIT_CSA;

	if (ret) {
		IWL_DEBUG_INFO(mvm,
			       "Link %d is not allowed for esr\n",
			       link->link_id);
		iwl_mvm_print_esr_state(mvm, ret);
	}
	return ret;
}

VISIBLE_IF_IWLWIFI_KUNIT
bool iwl_mvm_mld_valid_link_pair(struct ieee80211_vif *vif,
				 const struct iwl_mvm_link_sel_data *a,
				 const struct iwl_mvm_link_sel_data *b)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	enum iwl_mvm_esr_state ret = 0;

	/* Per-link considerations */
	if (iwl_mvm_esr_disallowed_with_link(mvm, vif, a, true) ||
	    iwl_mvm_esr_disallowed_with_link(mvm, vif, b, false))
		return false;

	if (a->chandef->width != b->chandef->width ||
	    !(a->chandef->chan->band == NL80211_BAND_6GHZ &&
	      b->chandef->chan->band == NL80211_BAND_5GHZ))
		ret |= IWL_MVM_ESR_EXIT_BANDWIDTH;

	if (ret) {
		IWL_DEBUG_INFO(mvm,
			       "Links %d and %d are not a valid pair for EMLSR\n",
			       a->link_id, b->link_id);
		iwl_mvm_print_esr_state(mvm, ret);
		return false;
	}

	return true;

}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mvm_mld_valid_link_pair);

/*
 * Returns the combined eSR grade of two given links.
 * Returns 0 if eSR is not allowed with these 2 links.
 */
static
unsigned int iwl_mvm_get_esr_grade(struct ieee80211_vif *vif,
				   const struct iwl_mvm_link_sel_data *a,
				   const struct iwl_mvm_link_sel_data *b,
				   u8 *primary_id)
{
	struct ieee80211_bss_conf *primary_conf;
	struct wiphy *wiphy = ieee80211_vif_to_wdev(vif)->wiphy;
	unsigned int primary_load;

	lockdep_assert_wiphy(wiphy);

	/* a is always primary, b is always secondary */
	if (b->grade > a->grade)
		swap(a, b);

	*primary_id = a->link_id;

	if (!iwl_mvm_mld_valid_link_pair(vif, a, b))
		return 0;

	primary_conf = wiphy_dereference(wiphy, vif->link_conf[*primary_id]);

	if (WARN_ON_ONCE(!primary_conf))
		return 0;

	primary_load = iwl_mvm_get_chan_load(primary_conf);

	return a->grade +
		((b->grade * primary_load) / SCALE_FACTOR);
}

void iwl_mvm_select_links(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_link_sel_data data[IEEE80211_MLD_MAX_NUM_LINKS];
	struct iwl_mvm_link_sel_data *best_link;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	u32 max_active_links = iwl_mvm_max_active_links(mvm, vif);
	u16 usable_links = ieee80211_vif_usable_links(vif);
	u8 best, primary_link, best_in_pair, n_data;
	u16 max_esr_grade = 0, new_active_links;

	lockdep_assert_wiphy(mvm->hw->wiphy);

	if (!mvmvif->authorized || !ieee80211_vif_is_mld(vif))
		return;

	if (!IWL_MVM_AUTO_EML_ENABLE)
		return;

	/* The logic below is a simple version that doesn't suit more than 2
	 * links
	 */
	WARN_ON_ONCE(max_active_links > 2);

	n_data = iwl_mvm_set_link_selection_data(vif, data, usable_links,
						 &best);

	if (WARN(!n_data, "Couldn't find a valid grade for any link!\n"))
		return;

	best_link = &data[best];
	primary_link = best_link->link_id;
	new_active_links = BIT(best_link->link_id);

	/* eSR is not supported/blocked, or only one usable link */
	if (max_active_links == 1 || !iwl_mvm_vif_has_esr_cap(mvm, vif) ||
	    mvmvif->esr_disable_reason || n_data == 1)
		goto set_active;

	for (u8 a = 0; a < n_data; a++)
		for (u8 b = a + 1; b < n_data; b++) {
			u16 esr_grade = iwl_mvm_get_esr_grade(vif, &data[a],
							      &data[b],
							      &best_in_pair);

			if (esr_grade <= max_esr_grade)
				continue;

			max_esr_grade = esr_grade;
			primary_link = best_in_pair;
			new_active_links = BIT(data[a].link_id) |
					   BIT(data[b].link_id);
		}

	/* No valid pair was found, go with the best link */
	if (hweight16(new_active_links) <= 1)
		goto set_active;

	/* For equal grade - prefer EMLSR */
	if (best_link->grade > max_esr_grade) {
		primary_link = best_link->link_id;
		new_active_links = BIT(best_link->link_id);
	}
set_active:
	IWL_DEBUG_INFO(mvm, "Link selection result: 0x%x. Primary = %d\n",
		       new_active_links, primary_link);
	ieee80211_set_active_links_async(vif, new_active_links);
	mvmvif->link_selection_res = new_active_links;
	mvmvif->link_selection_primary = primary_link;
}

u8 iwl_mvm_get_primary_link(struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	/* relevant data is written with both locks held, so read with either */
	lockdep_assert(lockdep_is_held(&mvmvif->mvm->mutex) ||
		       lockdep_is_held(&mvmvif->mvm->hw->wiphy->mtx));

	if (!ieee80211_vif_is_mld(vif))
		return 0;

	/* In AP mode, there is no primary link */
	if (vif->type == NL80211_IFTYPE_AP)
		return __ffs(vif->active_links);

	if (mvmvif->esr_active &&
	    !WARN_ON(!(BIT(mvmvif->primary_link) & vif->active_links)))
		return mvmvif->primary_link;

	return __ffs(vif->active_links);
}

/*
 * For non-MLO/single link, this will return the deflink/single active link,
 * respectively
 */
u8 iwl_mvm_get_other_link(struct ieee80211_vif *vif, u8 link_id)
{
	switch (hweight16(vif->active_links)) {
	case 0:
		return 0;
	default:
		WARN_ON(1);
		fallthrough;
	case 1:
		return __ffs(vif->active_links);
	case 2:
		return __ffs(vif->active_links & ~BIT(link_id));
	}
}

/* Reasons that can cause esr prevention */
#define IWL_MVM_ESR_PREVENT_REASONS	IWL_MVM_ESR_EXIT_MISSED_BEACON
#define IWL_MVM_PREVENT_ESR_TIMEOUT	(HZ * 400)
#define IWL_MVM_ESR_PREVENT_SHORT	(HZ * 300)
#define IWL_MVM_ESR_PREVENT_LONG	(HZ * 600)

static bool iwl_mvm_check_esr_prevention(struct iwl_mvm *mvm,
					 struct iwl_mvm_vif *mvmvif,
					 enum iwl_mvm_esr_state reason)
{
	bool timeout_expired = time_after(jiffies,
					  mvmvif->last_esr_exit.ts +
					  IWL_MVM_PREVENT_ESR_TIMEOUT);
	unsigned long delay;

	lockdep_assert_held(&mvm->mutex);

	/* Only handle reasons that can cause prevention */
	if (!(reason & IWL_MVM_ESR_PREVENT_REASONS))
		return false;

	/*
	 * Reset the counter if more than 400 seconds have passed between one
	 * exit and the other, or if we exited due to a different reason.
	 * Will also reset the counter after the long prevention is done.
	 */
	if (timeout_expired || mvmvif->last_esr_exit.reason != reason) {
		mvmvif->exit_same_reason_count = 1;
		return false;
	}

	mvmvif->exit_same_reason_count++;
	if (WARN_ON(mvmvif->exit_same_reason_count < 2 ||
		    mvmvif->exit_same_reason_count > 3))
		return false;

	mvmvif->esr_disable_reason |= IWL_MVM_ESR_BLOCKED_PREVENTION;

	/*
	 * For the second exit, use a short prevention, and for the third one,
	 * use a long prevention.
	 */
	delay = mvmvif->exit_same_reason_count == 2 ?
		IWL_MVM_ESR_PREVENT_SHORT :
		IWL_MVM_ESR_PREVENT_LONG;

	IWL_DEBUG_INFO(mvm,
		       "Preventing EMLSR for %ld seconds due to %u exits with the reason = %s (0x%x)\n",
		       delay / HZ, mvmvif->exit_same_reason_count,
		       iwl_get_esr_state_string(reason), reason);

	wiphy_delayed_work_queue(mvm->hw->wiphy,
				 &mvmvif->prevent_esr_done_wk, delay);
	return true;
}

#define IWL_MVM_TRIGGER_LINK_SEL_TIME (IWL_MVM_TRIGGER_LINK_SEL_TIME_SEC * HZ)

/* API to exit eSR mode */
void iwl_mvm_exit_esr(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		      enum iwl_mvm_esr_state reason,
		      u8 link_to_keep)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	u16 new_active_links;
	bool prevented;

	lockdep_assert_held(&mvm->mutex);

	if (!IWL_MVM_AUTO_EML_ENABLE)
		return;

	/* Nothing to do */
	if (!mvmvif->esr_active)
		return;

	if (WARN_ON(!ieee80211_vif_is_mld(vif) || !mvmvif->authorized))
		return;

	if (WARN_ON(!(vif->active_links & BIT(link_to_keep))))
		link_to_keep = __ffs(vif->active_links);

	new_active_links = BIT(link_to_keep);
	IWL_DEBUG_INFO(mvm,
		       "Exiting EMLSR. reason = %s (0x%x). Current active links=0x%x, new active links = 0x%x\n",
		       iwl_get_esr_state_string(reason), reason,
		       vif->active_links, new_active_links);

	ieee80211_set_active_links_async(vif, new_active_links);

	/* Prevent EMLSR if needed */
	prevented = iwl_mvm_check_esr_prevention(mvm, mvmvif, reason);

	/* Remember why and when we exited EMLSR */
	mvmvif->last_esr_exit.ts = jiffies;
	mvmvif->last_esr_exit.reason = reason;

	/*
	 * If EMLSR is prevented now - don't try to get back to EMLSR.
	 * If we exited due to a blocking event, we will try to get back to
	 * EMLSR when the corresponding unblocking event will happen.
	 */
	if (prevented || reason & IWL_MVM_BLOCK_ESR_REASONS)
		return;

	/* If EMLSR is not blocked - try enabling it again in 30 seconds */
	wiphy_delayed_work_queue(mvm->hw->wiphy,
				 &mvmvif->mlo_int_scan_wk,
				 round_jiffies_relative(IWL_MVM_TRIGGER_LINK_SEL_TIME));
}

void iwl_mvm_block_esr(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		       enum iwl_mvm_esr_state reason,
		       u8 link_to_keep)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	lockdep_assert_held(&mvm->mutex);

	if (!IWL_MVM_AUTO_EML_ENABLE)
		return;

	/* This should be called only with disable reasons */
	if (WARN_ON(!(reason & IWL_MVM_BLOCK_ESR_REASONS)))
		return;

	if (mvmvif->esr_disable_reason & reason)
		return;

	IWL_DEBUG_INFO(mvm,
		       "Blocking EMLSR mode. reason = %s (0x%x)\n",
		       iwl_get_esr_state_string(reason), reason);

	mvmvif->esr_disable_reason |= reason;

	iwl_mvm_print_esr_state(mvm, mvmvif->esr_disable_reason);

	iwl_mvm_exit_esr(mvm, vif, reason, link_to_keep);
}

int iwl_mvm_block_esr_sync(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			   enum iwl_mvm_esr_state reason)
{
	int primary_link = iwl_mvm_get_primary_link(vif);
	int ret;

	if (!IWL_MVM_AUTO_EML_ENABLE || !ieee80211_vif_is_mld(vif))
		return 0;

	/* This should be called only with blocking reasons */
	if (WARN_ON(!(reason & IWL_MVM_BLOCK_ESR_REASONS)))
		return 0;

	/* leave ESR immediately, not only async with iwl_mvm_block_esr() */
	ret = ieee80211_set_active_links(vif, BIT(primary_link));
	if (ret)
		return ret;

	mutex_lock(&mvm->mutex);
	/* only additionally block for consistency and to avoid concurrency */
	iwl_mvm_block_esr(mvm, vif, reason, primary_link);
	mutex_unlock(&mvm->mutex);

	return 0;
}

static void iwl_mvm_esr_unblocked(struct iwl_mvm *mvm,
				  struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	bool need_new_sel = time_after(jiffies, mvmvif->last_esr_exit.ts +
						IWL_MVM_TRIGGER_LINK_SEL_TIME);

	lockdep_assert_held(&mvm->mutex);

	if (!ieee80211_vif_is_mld(vif) || !mvmvif->authorized ||
	    mvmvif->esr_active)
		return;

	IWL_DEBUG_INFO(mvm, "EMLSR is unblocked\n");

	/* If we exited due to an EXIT reason, and the exit was in less than
	 * 30 seconds, then a MLO scan was scheduled already.
	 */
	if (!need_new_sel &&
	    !(mvmvif->last_esr_exit.reason & IWL_MVM_BLOCK_ESR_REASONS)) {
		IWL_DEBUG_INFO(mvm, "Wait for MLO scan\n");
		return;
	}

	/*
	 * If EMLSR was blocked for more than 30 seconds, or the last link
	 * selection decided to not enter EMLSR, trigger a new scan.
	 */
	if (need_new_sel || hweight16(mvmvif->link_selection_res) < 2) {
		IWL_DEBUG_INFO(mvm, "Trigger MLO scan\n");
		wiphy_delayed_work_queue(mvm->hw->wiphy,
					 &mvmvif->mlo_int_scan_wk, 0);
	/*
	 * If EMLSR was blocked for less than 30 seconds, and the last link
	 * selection decided to use EMLSR, activate EMLSR using the previous
	 * link selection result.
	 */
	} else {
		IWL_DEBUG_INFO(mvm,
			       "Use the latest link selection result: 0x%x\n",
			       mvmvif->link_selection_res);
		ieee80211_set_active_links_async(vif,
						 mvmvif->link_selection_res);
	}
}

void iwl_mvm_unblock_esr(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			 enum iwl_mvm_esr_state reason)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	lockdep_assert_held(&mvm->mutex);

	if (!IWL_MVM_AUTO_EML_ENABLE)
		return;

	/* This should be called only with disable reasons */
	if (WARN_ON(!(reason & IWL_MVM_BLOCK_ESR_REASONS)))
		return;

	/* No Change */
	if (!(mvmvif->esr_disable_reason & reason))
		return;

	mvmvif->esr_disable_reason &= ~reason;

	IWL_DEBUG_INFO(mvm,
		       "Unblocking EMLSR mode. reason = %s (0x%x)\n",
		       iwl_get_esr_state_string(reason), reason);
	iwl_mvm_print_esr_state(mvm, mvmvif->esr_disable_reason);

	if (!mvmvif->esr_disable_reason)
		iwl_mvm_esr_unblocked(mvm, vif);
}

void iwl_mvm_init_link(struct iwl_mvm_vif_link_info *link)
{
	link->bcast_sta.sta_id = IWL_INVALID_STA;
	link->mcast_sta.sta_id = IWL_INVALID_STA;
	link->ap_sta_id = IWL_INVALID_STA;

	for (int r = 0; r < NUM_IWL_MVM_SMPS_REQ; r++)
		link->smps_requests[r] =
			IEEE80211_SMPS_AUTOMATIC;
}
