// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2022 - 2025 Intel Corporation
 */
#include "mvm.h"

static void iwl_mvm_mld_set_he_support(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif,
				       struct iwl_mac_config_cmd *cmd,
				       int cmd_ver)
{
	if (vif->type == NL80211_IFTYPE_AP) {
		if (cmd_ver == 2)
			cmd->wifi_gen_v2.he_ap_support = cpu_to_le16(1);
		else
			cmd->wifi_gen.he_ap_support = 1;
	} else {
		if (cmd_ver == 2)
			cmd->wifi_gen_v2.he_support = cpu_to_le16(1);
		else
			cmd->wifi_gen.he_support = 1;
	}
}

static void iwl_mvm_mld_mac_ctxt_cmd_common(struct iwl_mvm *mvm,
					    struct ieee80211_vif *vif,
					    struct iwl_mac_config_cmd *cmd,
					    u32 action)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct ieee80211_bss_conf *link_conf;
	unsigned int link_id;
	int cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw,
					    WIDE_ID(MAC_CONF_GROUP,
						    MAC_CONFIG_CMD), 0);

	if (WARN_ON(cmd_ver < 1 && cmd_ver > 3))
		return;

	cmd->id_and_color = cpu_to_le32(mvmvif->id);
	cmd->action = cpu_to_le32(action);

	cmd->mac_type = cpu_to_le32(iwl_mvm_get_mac_type(vif));

	memcpy(cmd->local_mld_addr, vif->addr, ETH_ALEN);

	cmd->wifi_gen_v2.he_support = 0;
	cmd->wifi_gen_v2.eht_support = 0;

	/* should be set by specific context type handler */
	cmd->filter_flags = 0;

	cmd->nic_not_ack_enabled =
		cpu_to_le32(!iwl_mvm_is_nic_ack_enabled(mvm, vif));

	if (iwlwifi_mod_params.disable_11ax)
		return;

	/* If we have MLO enabled, then the firmware needs to enable
	 * address translation for the station(s) we add. That depends
	 * on having EHT enabled in firmware, which in turn depends on
	 * mac80211 in the code below.
	 * However, mac80211 doesn't enable HE/EHT until it has parsed
	 * the association response successfully, so just skip all that
	 * and enable both when we have MLO.
	 */
	if (ieee80211_vif_is_mld(vif)) {
		iwl_mvm_mld_set_he_support(mvm, vif, cmd, cmd_ver);
		if (cmd_ver == 2)
			cmd->wifi_gen_v2.eht_support = cpu_to_le32(1);
		else
			cmd->wifi_gen.eht_support = 1;
		return;
	}

	rcu_read_lock();
	for (link_id = 0; link_id < ARRAY_SIZE((vif)->link_conf); link_id++) {
		link_conf = rcu_dereference(vif->link_conf[link_id]);
		if (!link_conf)
			continue;

		if (link_conf->he_support)
			iwl_mvm_mld_set_he_support(mvm, vif, cmd, cmd_ver);

		/* It's not reasonable to have EHT without HE and FW API doesn't
		 * support it. Ignore EHT in this case.
		 */
		if (!link_conf->he_support && link_conf->eht_support)
			continue;

		if (link_conf->eht_support) {
			if (cmd_ver == 2)
				cmd->wifi_gen_v2.eht_support = cpu_to_le32(1);
			else
				cmd->wifi_gen.eht_support = 1;
			break;
		}
	}
	rcu_read_unlock();
}

static int iwl_mvm_mld_mac_ctxt_send_cmd(struct iwl_mvm *mvm,
					 struct iwl_mac_config_cmd *cmd)
{
	int ret = iwl_mvm_send_cmd_pdu(mvm,
				       WIDE_ID(MAC_CONF_GROUP, MAC_CONFIG_CMD),
				       0, sizeof(*cmd), cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send MAC_CONFIG_CMD (action:%d): %d\n",
			le32_to_cpu(cmd->action), ret);
	return ret;
}

static int iwl_mvm_mld_mac_ctxt_cmd_sta(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					u32 action, bool force_assoc_off)
{
	struct iwl_mac_config_cmd cmd = {};
	u16 esr_transition_timeout;

	WARN_ON(vif->type != NL80211_IFTYPE_STATION);

	/* Fill the common data for all mac context types */
	iwl_mvm_mld_mac_ctxt_cmd_common(mvm, vif, &cmd, action);

	/*
	 * We always want to hear MCAST frames, if we're not authorized yet,
	 * we'll drop them.
	 */
	cmd.filter_flags |= cpu_to_le32(MAC_CFG_FILTER_ACCEPT_GRP);

	if (vif->p2p)
		cmd.client.ctwin =
			iwl_mvm_mac_ctxt_cmd_p2p_sta_get_oppps_ctwin(mvm, vif);

	if (vif->cfg.assoc && !force_assoc_off) {
		struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

		cmd.client.is_assoc = 1;

		if (!mvmvif->authorized &&
		    fw_has_capa(&mvm->fw->ucode_capa,
				IWL_UCODE_TLV_CAPA_COEX_HIGH_PRIO))
			cmd.client.data_policy |=
				cpu_to_le16(COEX_HIGH_PRIORITY_ENABLE);

	} else {
		cmd.client.is_assoc = 0;

		/* Allow beacons to pass through as long as we are not
		 * associated, or we do not have dtim period information.
		 */
		cmd.filter_flags |= cpu_to_le32(MAC_CFG_FILTER_ACCEPT_BEACON);
	}

	cmd.client.assoc_id = cpu_to_le16(vif->cfg.aid);
	if (ieee80211_vif_is_mld(vif)) {
		esr_transition_timeout =
			u16_get_bits(vif->cfg.eml_cap,
				     IEEE80211_EML_CAP_TRANSITION_TIMEOUT);

		cmd.client.esr_transition_timeout =
			min_t(u16, IEEE80211_EML_CAP_TRANSITION_TIMEOUT_128TU,
			      esr_transition_timeout);
		cmd.client.medium_sync_delay =
			cpu_to_le16(vif->cfg.eml_med_sync_delay);
	}

	if (vif->probe_req_reg && vif->cfg.assoc && vif->p2p)
		cmd.filter_flags |= cpu_to_le32(MAC_CFG_FILTER_ACCEPT_PROBE_REQ);

	if (vif->bss_conf.he_support && !iwlwifi_mod_params.disable_11ax)
		cmd.client.data_policy |=
			cpu_to_le16(iwl_mvm_mac_ctxt_cmd_sta_get_twt_policy(mvm, vif));

	return iwl_mvm_mld_mac_ctxt_send_cmd(mvm, &cmd);
}

static int iwl_mvm_mld_mac_ctxt_cmd_listener(struct iwl_mvm *mvm,
					     struct ieee80211_vif *vif,
					     u32 action)
{
	struct iwl_mac_config_cmd cmd = {};

	WARN_ON(vif->type != NL80211_IFTYPE_MONITOR);

	iwl_mvm_mld_mac_ctxt_cmd_common(mvm, vif, &cmd, action);

	cmd.filter_flags = cpu_to_le32(MAC_CFG_FILTER_PROMISC |
				       MAC_CFG_FILTER_ACCEPT_CONTROL_AND_MGMT |
				       MAC_CFG_FILTER_ACCEPT_BEACON |
				       MAC_CFG_FILTER_ACCEPT_PROBE_REQ |
				       MAC_CFG_FILTER_ACCEPT_GRP);

	return iwl_mvm_mld_mac_ctxt_send_cmd(mvm, &cmd);
}

static int iwl_mvm_mld_mac_ctxt_cmd_ibss(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif,
					 u32 action)
{
	struct iwl_mac_config_cmd cmd = {};

	WARN_ON(vif->type != NL80211_IFTYPE_ADHOC);

	iwl_mvm_mld_mac_ctxt_cmd_common(mvm, vif, &cmd, action);

	cmd.filter_flags = cpu_to_le32(MAC_CFG_FILTER_ACCEPT_BEACON |
				       MAC_CFG_FILTER_ACCEPT_PROBE_REQ |
				       MAC_CFG_FILTER_ACCEPT_GRP);

	return iwl_mvm_mld_mac_ctxt_send_cmd(mvm, &cmd);
}

static int iwl_mvm_mld_mac_ctxt_cmd_p2p_device(struct iwl_mvm *mvm,
					       struct ieee80211_vif *vif,
					       u32 action)
{
	struct iwl_mac_config_cmd cmd = {};

	WARN_ON(vif->type != NL80211_IFTYPE_P2P_DEVICE);

	iwl_mvm_mld_mac_ctxt_cmd_common(mvm, vif, &cmd, action);

	cmd.p2p_dev.is_disc_extended =
		iwl_mac_ctxt_p2p_dev_has_extended_disc(mvm, vif);

	/* Override the filter flags to accept all management frames. This is
	 * needed to support both P2P device discovery using probe requests and
	 * P2P service discovery using action frames
	 */
	cmd.filter_flags = cpu_to_le32(MAC_CFG_FILTER_ACCEPT_CONTROL_AND_MGMT);

	return iwl_mvm_mld_mac_ctxt_send_cmd(mvm, &cmd);
}

static int iwl_mvm_mld_mac_ctxt_cmd_ap_go(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  u32 action)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mac_config_cmd cmd = {};

	WARN_ON(vif->type != NL80211_IFTYPE_AP);

	/* Fill the common data for all mac context types */
	iwl_mvm_mld_mac_ctxt_cmd_common(mvm, vif, &cmd, action);

	iwl_mvm_mac_ctxt_cmd_ap_set_filter_flags(mvm, mvmvif,
						 &cmd.filter_flags,
						 MAC_CFG_FILTER_ACCEPT_PROBE_REQ,
						 MAC_CFG_FILTER_ACCEPT_BEACON);

	return iwl_mvm_mld_mac_ctxt_send_cmd(mvm, &cmd);
}

static int iwl_mvm_mld_mac_ctx_send(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    u32 action, bool force_assoc_off)
{
	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		return iwl_mvm_mld_mac_ctxt_cmd_sta(mvm, vif, action,
						    force_assoc_off);
	case NL80211_IFTYPE_AP:
		return iwl_mvm_mld_mac_ctxt_cmd_ap_go(mvm, vif, action);
	case NL80211_IFTYPE_MONITOR:
		return iwl_mvm_mld_mac_ctxt_cmd_listener(mvm, vif, action);
	case NL80211_IFTYPE_P2P_DEVICE:
		return iwl_mvm_mld_mac_ctxt_cmd_p2p_device(mvm, vif, action);
	case NL80211_IFTYPE_ADHOC:
		return iwl_mvm_mld_mac_ctxt_cmd_ibss(mvm, vif, action);
	default:
		break;
	}

	return -EOPNOTSUPP;
}

int iwl_mvm_mld_mac_ctxt_add(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	if (WARN_ON_ONCE(vif->type == NL80211_IFTYPE_NAN))
		return -EOPNOTSUPP;

	if (WARN_ONCE(mvmvif->uploaded, "Adding active MAC %pM/%d\n",
		      vif->addr, ieee80211_vif_type_p2p(vif)))
		return -EIO;

	ret = iwl_mvm_mld_mac_ctx_send(mvm, vif, FW_CTXT_ACTION_ADD,
				       true);
	if (ret)
		return ret;

	/* will only do anything at resume from D3 time */
	iwl_mvm_set_last_nonqos_seq(mvm, vif);

	mvmvif->uploaded = true;
	return 0;
}

int iwl_mvm_mld_mac_ctxt_changed(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 bool force_assoc_off)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (WARN_ON_ONCE(vif->type == NL80211_IFTYPE_NAN))
		return -EOPNOTSUPP;

	if (WARN_ONCE(!mvmvif->uploaded, "Changing inactive MAC %pM/%d\n",
		      vif->addr, ieee80211_vif_type_p2p(vif)))
		return -EIO;

	return iwl_mvm_mld_mac_ctx_send(mvm, vif, FW_CTXT_ACTION_MODIFY,
					force_assoc_off);
}

int iwl_mvm_mld_mac_ctxt_remove(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mac_config_cmd cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE),
		.id_and_color = cpu_to_le32(mvmvif->id),
	};
	int ret;

	if (WARN_ON_ONCE(vif->type == NL80211_IFTYPE_NAN))
		return -EOPNOTSUPP;

	if (WARN_ONCE(!mvmvif->uploaded, "Removing inactive MAC %pM/%d\n",
		      vif->addr, ieee80211_vif_type_p2p(vif)))
		return -EIO;

	ret = iwl_mvm_mld_mac_ctxt_send_cmd(mvm, &cmd);
	if (ret)
		return ret;

	mvmvif->uploaded = false;

	return 0;
}
