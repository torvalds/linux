// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2022 Intel Corporation
 */
#include "mvm.h"

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

int iwl_mvm_add_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
		     struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	unsigned int link_id = link_conf->link_id;
	struct iwl_link_config_cmd cmd = {};
	struct iwl_mvm_phy_ctxt *phyctxt;

	if (WARN_ON_ONCE(!mvmvif->link[link_id]))
		return -EINVAL;

	/* Update SF - Disable if needed. if this fails, SF might still be on
	 * while many macs are bound, which is forbidden - so fail the binding.
	 */
	if (iwl_mvm_sf_update(mvm, vif, false))
		return -EINVAL;

	/* FIXME: add proper link id allocation */
	cmd.link_id = cpu_to_le32(mvmvif->id);
	cmd.mac_id = cpu_to_le32(mvmvif->id);
	/* P2P-Device already has a valid PHY context during add */
	phyctxt = mvmvif->link[link_id]->phy_ctxt;
	if (phyctxt)
		cmd.phy_id = cpu_to_le32(phyctxt->id);
	else
		cmd.phy_id = cpu_to_le32(FW_CTXT_INVALID);

	memcpy(cmd.local_link_addr, link_conf->addr, ETH_ALEN);

	if (vif->type == NL80211_IFTYPE_ADHOC && link_conf->bssid)
		memcpy(cmd.ibss_bssid_addr, link_conf->bssid, ETH_ALEN);

	return iwl_mvm_link_cmd_send(mvm, &cmd, FW_CTXT_ACTION_ADD);
}

int iwl_mvm_link_changed(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			 struct ieee80211_bss_conf *link_conf,
			 u32 changes, bool active)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	unsigned int link_id = link_conf->link_id;
	struct iwl_mvm_phy_ctxt *phyctxt;
	struct iwl_link_config_cmd cmd = {};
	u32 ht_flag, flags = 0, flags_mask = 0;

	if (WARN_ON_ONCE(!mvmvif->link[link_id]))
		return -EINVAL;

	/* FIXME: add proper link id allocation */
	cmd.link_id = cpu_to_le32(mvmvif->id);

	/* The phy_id, link address and listen_lmac can be modified only until
	 * the link becomes active, otherwise they will be ignored.
	 */
	phyctxt = mvmvif->link[link_id]->phy_ctxt;
	if (phyctxt)
		cmd.phy_id = cpu_to_le32(phyctxt->id);
	else
		cmd.phy_id = cpu_to_le32(FW_CTXT_INVALID);
	cmd.mac_id = cpu_to_le32(mvmvif->id);

	memcpy(cmd.local_link_addr, link_conf->addr, ETH_ALEN);

	cmd.active = cpu_to_le32(active);

	if (vif->type == NL80211_IFTYPE_ADHOC && link_conf->bssid)
		memcpy(cmd.ibss_bssid_addr, link_conf->bssid, ETH_ALEN);

	/* TODO: set a value to cmd.listen_lmac when system requiremens
	 * will define it
	 */

	iwl_mvm_set_fw_basic_rates(mvm, vif, link_conf,
				   &cmd.cck_rates, &cmd.ofdm_rates);

	cmd.cck_short_preamble = cpu_to_le32(link_conf->use_short_preamble);
	cmd.short_slot = cpu_to_le32(link_conf->use_short_slot);

	/* The fw does not distinguish between ht and fat */
	ht_flag = LINK_PROT_FLG_HT_PROT | LINK_PROT_FLG_FAT_PROT;
	iwl_mvm_set_fw_protection_flags(mvm, vif, link_conf,
					&cmd.protection_flags,
					ht_flag, LINK_PROT_FLG_TGG_PROTECT);

	iwl_mvm_set_fw_qos_params(mvm, vif, link_conf, &cmd.ac[0],
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

	if (iwl_mvm_set_fw_mu_edca_params(mvm, mvmvif,
					  &cmd.trig_based_txf[0])) {
		flags |= LINK_FLG_MU_EDCA_CW;
		flags_mask |= LINK_FLG_MU_EDCA_CW;
	}

	if (link_conf->eht_puncturing && !iwlwifi_mod_params.disable_11be)
		cmd.puncture_mask = cpu_to_le16(link_conf->eht_puncturing);
	else
		/* This flag can be set only if the MAC has eht support */
		changes &= ~LINK_CONTEXT_MODIFY_EHT_PARAMS;

	cmd.bss_color = link_conf->he_bss_color.color;

	if (!link_conf->he_bss_color.enabled) {
		flags |= LINK_FLG_BSS_COLOR_DIS;
		flags_mask |= LINK_FLG_BSS_COLOR_DIS;
	}

	cmd.frame_time_rts_th = cpu_to_le16(link_conf->frame_time_rts_th);

	/* Block 26-tone RU OFDMA transmissions */
	if (mvmvif->deflink.he_ru_2mhz_block) {
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

	return iwl_mvm_link_cmd_send(mvm, &cmd, FW_CTXT_ACTION_MODIFY);
}

int iwl_mvm_remove_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_link_config_cmd cmd = {};
	int ret;

	/* FIXME: add proper link id allocation */
	cmd.link_id = cpu_to_le32(mvmvif->id);
	ret = iwl_mvm_link_cmd_send(mvm, &cmd, FW_CTXT_ACTION_REMOVE);

	if (!ret)
		if (iwl_mvm_sf_update(mvm, vif, true))
			IWL_ERR(mvm, "Failed to update SF state\n");

	return ret;
}
