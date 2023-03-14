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

int iwl_mvm_add_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_link_config_cmd cmd = {};

	if (WARN_ON_ONCE(!mvmvif->phy_ctxt))
		return -EINVAL;

	/* Update SF - Disable if needed. if this fails, SF might still be on
	 * while many macs are bound, which is forbidden - so fail the binding.
	 */
	if (iwl_mvm_sf_update(mvm, vif, false))
		return -EINVAL;

	cmd.link_id = cpu_to_le32(mvmvif->phy_ctxt->id);
	cmd.mac_id = cpu_to_le32(mvmvif->id);
	cmd.phy_id = cpu_to_le32(mvmvif->phy_ctxt->id);

	memcpy(cmd.local_link_addr, vif->addr, ETH_ALEN);

	return iwl_mvm_link_cmd_send(mvm, &cmd, FW_CTXT_ACTION_ADD);
}

int iwl_mvm_link_changed(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			 u32 changes, bool active)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_phy_ctxt *phyctxt = mvmvif->phy_ctxt;
	struct iwl_link_config_cmd cmd = {};
	u32 ht_flag, flags = 0, flags_mask = 0;

	if (!phyctxt)
		return -EINVAL;

	cmd.link_id = cpu_to_le32(phyctxt->id);

	/* The phy_id, link address and listen_lmac can be modified only until
	 * the link becomes active, otherwise they will be ignored.
	 */
	cmd.phy_id = cpu_to_le32(phyctxt->id);
	cmd.mac_id = cpu_to_le32(mvmvif->id);

	memcpy(cmd.local_link_addr, vif->addr, ETH_ALEN);

	cmd.active = cpu_to_le32(active);

	/* TODO: set a value to cmd.listen_lmac when system requiremens
	 * will define it
	 */

	iwl_mvm_set_fw_basic_rates(mvm, vif, &cmd.cck_rates, &cmd.ofdm_rates);

	cmd.cck_short_preamble = cpu_to_le32(vif->bss_conf.use_short_preamble);
	cmd.short_slot = cpu_to_le32(vif->bss_conf.use_short_slot);

	/* The fw does not distinguish between ht and fat */
	ht_flag = LINK_PROT_FLG_HT_PROT | LINK_PROT_FLG_FAT_PROT;
	iwl_mvm_set_fw_protection_flags(mvm, vif, &cmd.protection_flags,
					ht_flag, LINK_PROT_FLG_TGG_PROTECT);

	iwl_mvm_set_fw_qos_params(mvm, vif, &cmd.ac[0], &cmd.qos_flags);

	/* We need the dtim_period to set the MAC as associated */
	if (vif->cfg.assoc && vif->bss_conf.dtim_period)
		iwl_mvm_set_fw_dtim_tbtt(mvm, vif, &cmd.dtim_tsf,
					 &cmd.dtim_time,
					 &cmd.assoc_beacon_arrive_time);
	else
		changes &= ~LINK_CONTEXT_MODIFY_BEACON_TIMING;

	cmd.bi = cpu_to_le32(vif->bss_conf.beacon_int);
	cmd.dtim_interval = cpu_to_le32(vif->bss_conf.beacon_int *
					vif->bss_conf.dtim_period);

	/* TODO: Assumes that the beacon id == mac context id */
	cmd.beacon_template = cpu_to_le32(mvmvif->id);

	if (!vif->bss_conf.he_support || iwlwifi_mod_params.disable_11ax ||
	    !vif->cfg.assoc) {
		changes &= ~LINK_CONTEXT_MODIFY_HE_PARAMS;
		goto send_cmd;
	}

	cmd.htc_trig_based_pkt_ext = vif->bss_conf.htc_trig_based_pkt_ext;

	if (vif->bss_conf.uora_exists) {
		cmd.rand_alloc_ecwmin =
			vif->bss_conf.uora_ocw_range & 0x7;
		cmd.rand_alloc_ecwmax =
			(vif->bss_conf.uora_ocw_range >> 3) & 0x7;
	}

	/* TODO  how to set ndp_fdbk_buff_th_exp? */

	if (iwl_mvm_set_fw_mu_edca_params(mvm, mvmvif,
					  &cmd.trig_based_txf[0])) {
		flags |= LINK_FLG_MU_EDCA_CW;
		flags_mask |= LINK_FLG_MU_EDCA_CW;
	}

	if (vif->bss_conf.eht_puncturing && !iwlwifi_mod_params.disable_11be)
		cmd.puncture_mask = cpu_to_le16(vif->bss_conf.eht_puncturing);
	else
		/* This flag can be set only if the MAC has eht support */
		changes &= ~LINK_CONTEXT_MODIFY_EHT_PARAMS;

	cmd.bss_color = vif->bss_conf.he_bss_color.color;

	if (!vif->bss_conf.he_bss_color.enabled) {
		flags |= LINK_FLG_BSS_COLOR_DIS;
		flags_mask |= LINK_FLG_BSS_COLOR_DIS;
	}

	cmd.frame_time_rts_th = cpu_to_le16(vif->bss_conf.frame_time_rts_th);

	/* Block 26-tone RU OFDMA transmissions */
	if (mvmvif->he_ru_2mhz_block) {
		flags |= LINK_FLG_RU_2MHZ_BLOCK;
		flags_mask |= LINK_FLG_RU_2MHZ_BLOCK;
	}

	if (vif->bss_conf.nontransmitted) {
		ether_addr_copy(cmd.ref_bssid_addr,
				vif->bss_conf.transmitter_bssid);
		cmd.bssid_index = vif->bss_conf.bssid_index;
	}

send_cmd:
	cmd.modify_mask = cpu_to_le32(changes);
	cmd.flags = cpu_to_le32(flags);
	cmd.flags_mask = cpu_to_le32(flags_mask);

	return iwl_mvm_link_cmd_send(mvm, &cmd, FW_CTXT_ACTION_MODIFY);
}

int iwl_mvm_remove_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_link_config_cmd cmd = {};
	int ret;

	if (WARN_ON_ONCE(!mvmvif->phy_ctxt))
		return -EINVAL;

	cmd.link_id = cpu_to_le32(mvmvif->phy_ctxt->id);
	ret = iwl_mvm_link_cmd_send(mvm, &cmd, FW_CTXT_ACTION_REMOVE);

	if (!ret)
		if (iwl_mvm_sf_update(mvm, vif, true))
			IWL_ERR(mvm, "Failed to update SF state\n");

	return ret;
}
