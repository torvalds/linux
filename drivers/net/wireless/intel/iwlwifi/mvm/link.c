// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2022 - 2024 Intel Corporation
 */
#include "mvm.h"
#include "time-event.h"

static u32 iwl_mvm_get_free_fw_link_id(struct iwl_mvm *mvm,
				       struct iwl_mvm_vif *mvm_vif)
{
	u32 link_id;

	lockdep_assert_held(&mvm->mutex);

	link_id = ffz(mvm->fw_link_ids_map);

	/* this case can happen if there're deactivated but not removed links */
	if (link_id > IWL_MVM_FW_MAX_LINK_ID)
		return IWL_MVM_FW_LINK_ID_INVALID;

	mvm->fw_link_ids_map |= BIT(link_id);
	return link_id;
}

static void iwl_mvm_release_fw_link_id(struct iwl_mvm *mvm, u32 link_id)
{
	lockdep_assert_held(&mvm->mutex);

	if (!WARN_ON(link_id > IWL_MVM_FW_MAX_LINK_ID))
		mvm->fw_link_ids_map &= ~BIT(link_id);
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
		 * be stopped
		 */
		if (!active && vif->type == NL80211_IFTYPE_STATION)
			iwl_mvm_stop_session_protection(mvm, vif);
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

	iwl_mvm_set_fw_basic_rates(mvm, vif, link_conf,
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
	iwl_mvm_release_fw_link_id(mvm, link_info->fw_link_id);
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
