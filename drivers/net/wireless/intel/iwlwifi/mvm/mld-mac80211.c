// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2022-2024 Intel Corporation
 */
#include "mvm.h"

static int iwl_mvm_mld_mac_add_interface(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;
	int i;

	guard(mvm)(mvm);

	iwl_mvm_mac_init_mvmvif(mvm, mvmvif);

	mvmvif->mvm = mvm;

	/* Not much to do here. The stack will not allow interface
	 * types or combinations that we didn't advertise, so we
	 * don't really have to check the types.
	 */

	/* make sure that beacon statistics don't go backwards with FW reset */
	if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
		for_each_mvm_vif_valid_link(mvmvif, i)
			mvmvif->link[i]->beacon_stats.accu_num_beacons +=
				mvmvif->link[i]->beacon_stats.num_beacons;

	/* Allocate resources for the MAC context, and add it to the fw  */
	ret = iwl_mvm_mac_ctxt_init(mvm, vif);
	if (ret)
		return ret;

	rcu_assign_pointer(mvm->vif_id_to_mac[mvmvif->id], vif);

	mvmvif->features |= hw->netdev_features;

	/* reset deflink MLO parameters */
	mvmvif->deflink.fw_link_id = IWL_MVM_FW_LINK_ID_INVALID;
	mvmvif->deflink.active = 0;
	/* the first link always points to the default one */
	mvmvif->link[0] = &mvmvif->deflink;

	ret = iwl_mvm_mld_mac_ctxt_add(mvm, vif);
	if (ret)
		return ret;

	/* beacon filtering */
	ret = iwl_mvm_disable_beacon_filter(mvm, vif);
	if (ret)
		goto out_remove_mac;

	if (!mvm->bf_allowed_vif &&
	    vif->type == NL80211_IFTYPE_STATION && !vif->p2p) {
		mvm->bf_allowed_vif = mvmvif;
		vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER |
				     IEEE80211_VIF_SUPPORTS_CQM_RSSI;
	}

	ret = iwl_mvm_add_link(mvm, vif, &vif->bss_conf);
	if (ret)
		goto out_free_bf;

	/* Save a pointer to p2p device vif, so it can later be used to
	 * update the p2p device MAC when a GO is started/stopped
	 */
	if (vif->type == NL80211_IFTYPE_P2P_DEVICE)
		mvm->p2p_device_vif = vif;

	ret = iwl_mvm_power_update_mac(mvm);
	if (ret)
		goto out_free_bf;

	iwl_mvm_tcm_add_vif(mvm, vif);

	if (vif->type == NL80211_IFTYPE_MONITOR) {
		mvm->monitor_on = true;
		ieee80211_hw_set(mvm->hw, RX_INCLUDES_FCS);
	}

	if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
		iwl_mvm_vif_dbgfs_add_link(mvm, vif);

	if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) &&
	    vif->type == NL80211_IFTYPE_STATION && !vif->p2p &&
	    !mvm->csme_vif && mvm->mei_registered) {
		iwl_mei_set_nic_info(vif->addr, mvm->nvm_data->hw_addr);
		iwl_mei_set_netdev(ieee80211_vif_to_wdev(vif)->netdev);
		mvm->csme_vif = vif;
	}

	if (vif->p2p || iwl_fw_lookup_cmd_ver(mvm->fw, PHY_CONTEXT_CMD, 1) < 5)
		vif->driver_flags |= IEEE80211_VIF_IGNORE_OFDMA_WIDER_BW;

	return 0;

 out_free_bf:
	if (mvm->bf_allowed_vif == mvmvif) {
		mvm->bf_allowed_vif = NULL;
		vif->driver_flags &= ~(IEEE80211_VIF_BEACON_FILTER |
				       IEEE80211_VIF_SUPPORTS_CQM_RSSI);
	}
 out_remove_mac:
	mvmvif->link[0] = NULL;
	iwl_mvm_mld_mac_ctxt_remove(mvm, vif);
	return ret;
}

static void iwl_mvm_mld_mac_remove_interface(struct ieee80211_hw *hw,
					     struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_probe_resp_data *probe_data;

	iwl_mvm_prepare_mac_removal(mvm, vif);

	if (!(vif->type == NL80211_IFTYPE_AP ||
	      vif->type == NL80211_IFTYPE_ADHOC))
		iwl_mvm_tcm_rm_vif(mvm, vif);

	guard(mvm)(mvm);

	if (vif == mvm->csme_vif) {
		iwl_mei_set_netdev(NULL);
		mvm->csme_vif = NULL;
	}

	if (mvm->bf_allowed_vif == mvmvif) {
		mvm->bf_allowed_vif = NULL;
		vif->driver_flags &= ~(IEEE80211_VIF_BEACON_FILTER |
				       IEEE80211_VIF_SUPPORTS_CQM_RSSI);
	}

	if (vif->bss_conf.ftm_responder)
		memset(&mvm->ftm_resp_stats, 0, sizeof(mvm->ftm_resp_stats));

	iwl_mvm_vif_dbgfs_rm_link(mvm, vif);

	/* For AP/GO interface, the tear down of the resources allocated to the
	 * interface is be handled as part of the stop_ap flow.
	 */
	if (vif->type == NL80211_IFTYPE_AP ||
	    vif->type == NL80211_IFTYPE_ADHOC) {
#ifdef CONFIG_NL80211_TESTMODE
		if (vif == mvm->noa_vif) {
			mvm->noa_vif = NULL;
			mvm->noa_duration = 0;
		}
#endif
	}

	iwl_mvm_power_update_mac(mvm);

	/* Before the interface removal, mac80211 would cancel the ROC, and the
	 * ROC worker would be scheduled if needed. The worker would be flushed
	 * in iwl_mvm_prepare_mac_removal() and thus at this point the link is
	 * not active. So need only to remove the link.
	 */
	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		if (mvmvif->deflink.phy_ctxt) {
			iwl_mvm_phy_ctxt_unref(mvm, mvmvif->deflink.phy_ctxt);
			mvmvif->deflink.phy_ctxt = NULL;
		}
		mvm->p2p_device_vif = NULL;
		iwl_mvm_remove_link(mvm, vif, &vif->bss_conf);
	} else {
		iwl_mvm_disable_link(mvm, vif, &vif->bss_conf);
	}

	iwl_mvm_mld_mac_ctxt_remove(mvm, vif);

	RCU_INIT_POINTER(mvm->vif_id_to_mac[mvmvif->id], NULL);

	probe_data = rcu_dereference_protected(mvmvif->deflink.probe_resp_data,
					       lockdep_is_held(&mvm->mutex));
	RCU_INIT_POINTER(mvmvif->deflink.probe_resp_data, NULL);
	if (probe_data)
		kfree_rcu(probe_data, rcu_head);

	if (vif->type == NL80211_IFTYPE_MONITOR) {
		mvm->monitor_on = false;
		__clear_bit(IEEE80211_HW_RX_INCLUDES_FCS, mvm->hw->flags);
	}
}

static unsigned int iwl_mvm_mld_count_active_links(struct iwl_mvm_vif *mvmvif)
{
	unsigned int n_active = 0;
	int i;

	for (i = 0; i < IEEE80211_MLD_MAX_NUM_LINKS; i++) {
		if (mvmvif->link[i] && mvmvif->link[i]->phy_ctxt)
			n_active++;
	}

	return n_active;
}

static void iwl_mvm_restart_mpdu_count(struct iwl_mvm *mvm,
				       struct iwl_mvm_vif *mvmvif)
{
	struct ieee80211_sta *ap_sta = mvmvif->ap_sta;
	struct iwl_mvm_sta *mvmsta;

	lockdep_assert_held(&mvm->mutex);

	if (!ap_sta)
		return;

	mvmsta = iwl_mvm_sta_from_mac80211(ap_sta);
	if (!mvmsta->mpdu_counters)
		return;

	for (int q = 0; q < mvm->trans->num_rx_queues; q++) {
		spin_lock_bh(&mvmsta->mpdu_counters[q].lock);
		memset(mvmsta->mpdu_counters[q].per_link, 0,
		       sizeof(mvmsta->mpdu_counters[q].per_link));
		mvmsta->mpdu_counters[q].window_start = jiffies;
		spin_unlock_bh(&mvmsta->mpdu_counters[q].lock);
	}

	IWL_DEBUG_STATS(mvm, "MPDU counters are cleared\n");
}

static int iwl_mvm_esr_mode_active(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int link_id, ret = 0;

	mvmvif->esr_active = true;

	/* Indicate to mac80211 that EML is enabled */
	vif->driver_flags |= IEEE80211_VIF_EML_ACTIVE;

	iwl_mvm_update_smps_on_active_links(mvm, vif, IWL_MVM_SMPS_REQ_FW,
					    IEEE80211_SMPS_OFF);

	for_each_mvm_vif_valid_link(mvmvif, link_id) {
		struct iwl_mvm_vif_link_info *link = mvmvif->link[link_id];

		if (!link->phy_ctxt)
			continue;

		ret = iwl_mvm_phy_send_rlc(mvm, link->phy_ctxt, 2, 2);
		if (ret)
			break;

		link->phy_ctxt->rlc_disabled = true;
	}

	if (vif->active_links == mvmvif->link_selection_res &&
	    !WARN_ON(!(vif->active_links & BIT(mvmvif->link_selection_primary))))
		mvmvif->primary_link = mvmvif->link_selection_primary;
	else
		mvmvif->primary_link = __ffs(vif->active_links);

	/* Needed for tracking RSSI */
	iwl_mvm_request_periodic_system_statistics(mvm, true);

	/*
	 * Restart the MPDU counters and the counting window, so when the
	 * statistics arrive (which is where we look at the counters) we
	 * will be at the end of the window.
	 */
	iwl_mvm_restart_mpdu_count(mvm, mvmvif);

	return ret;
}

static int
__iwl_mvm_mld_assign_vif_chanctx(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *link_conf,
				 struct ieee80211_chanctx_conf *ctx,
				 bool switching_chanctx)
{
	u16 *phy_ctxt_id = (u16 *)ctx->drv_priv;
	struct iwl_mvm_phy_ctxt *phy_ctxt = &mvm->phy_ctxts[*phy_ctxt_id];
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	unsigned int n_active = iwl_mvm_mld_count_active_links(mvmvif);
	unsigned int link_id = link_conf->link_id;
	int ret;

	if (WARN_ON_ONCE(!mvmvif->link[link_id]))
		return -EINVAL;

	/* if the assigned one was not counted yet, count it now */
	if (!mvmvif->link[link_id]->phy_ctxt)
		n_active++;

	/* mac parameters such as HE support can change at this stage
	 * For sta, need first to configure correct state from drv_sta_state
	 * and only after that update mac config.
	 */
	if (vif->type == NL80211_IFTYPE_AP) {
		ret = iwl_mvm_mld_mac_ctxt_changed(mvm, vif, false);
		if (ret) {
			IWL_ERR(mvm, "failed to update MAC %pM\n", vif->addr);
			return -EINVAL;
		}
	}

	mvmvif->link[link_id]->phy_ctxt = phy_ctxt;

	if (iwl_mvm_is_esr_supported(mvm->fwrt.trans) && n_active > 1) {
		mvmvif->link[link_id]->listen_lmac = true;
		ret = iwl_mvm_esr_mode_active(mvm, vif);
		if (ret) {
			IWL_ERR(mvm, "failed to activate ESR mode (%d)\n", ret);
			iwl_mvm_request_periodic_system_statistics(mvm, false);
			goto out;
		}
	}

	if (switching_chanctx) {
		/* reactivate if we turned this off during channel switch */
		if (vif->type == NL80211_IFTYPE_AP)
			mvmvif->ap_ibss_active = true;
	}

	/* send it first with phy context ID */
	ret = iwl_mvm_link_changed(mvm, vif, link_conf, 0, false);
	if (ret)
		goto out;

	/* Initialize rate control for the AP station, since we might be
	 * doing a link switch here - we cannot initialize it before since
	 * this needs the phy context assigned (and in FW?), and we cannot
	 * do it later because it needs to be initialized as soon as we're
	 * able to TX on the link, i.e. when active.
	 */
	if (mvmvif->ap_sta) {
		struct ieee80211_link_sta *link_sta;

		rcu_read_lock();
		link_sta = rcu_dereference(mvmvif->ap_sta->link[link_id]);

		if (!WARN_ON_ONCE(!link_sta))
			iwl_mvm_rs_rate_init(mvm, vif, mvmvif->ap_sta,
					     link_conf, link_sta,
					     phy_ctxt->channel->band);
		rcu_read_unlock();
	}

	if (vif->type == NL80211_IFTYPE_STATION)
		iwl_mvm_send_ap_tx_power_constraint_cmd(mvm, vif,
							link_conf,
							false);

	/* then activate */
	ret = iwl_mvm_link_changed(mvm, vif, link_conf,
				   LINK_CONTEXT_MODIFY_ACTIVE |
				   LINK_CONTEXT_MODIFY_RATES_INFO,
				   true);
	if (ret)
		goto out;

	/*
	 * Power state must be updated before quotas,
	 * otherwise fw will complain.
	 */
	iwl_mvm_power_update_mac(mvm);

	if (vif->type == NL80211_IFTYPE_MONITOR) {
		ret = iwl_mvm_mld_add_snif_sta(mvm, vif, link_conf);
		if (ret)
			goto deactivate;
	}

	return 0;

deactivate:
	iwl_mvm_link_changed(mvm, vif, link_conf, LINK_CONTEXT_MODIFY_ACTIVE,
			     false);
out:
	mvmvif->link[link_id]->phy_ctxt = NULL;
	iwl_mvm_power_update_mac(mvm);
	return ret;
}

static int iwl_mvm_mld_assign_vif_chanctx(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  struct ieee80211_bss_conf *link_conf,
					  struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	/* update EMLSR mode */
	if (ieee80211_vif_type_p2p(vif) != NL80211_IFTYPE_STATION) {
		int ret;

		ret = iwl_mvm_esr_non_bss_link(mvm, vif, link_conf->link_id,
					       true);
		/*
		 * Don't activate this link if failed to exit EMLSR in
		 * the BSS interface
		 */
		if (ret)
			return ret;
	}

	guard(mvm)(mvm);
	return __iwl_mvm_mld_assign_vif_chanctx(mvm, vif, link_conf, ctx, false);
}

static int iwl_mvm_esr_mode_inactive(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct ieee80211_bss_conf *link_conf;
	int link_id, ret = 0;

	mvmvif->esr_active = false;

	vif->driver_flags &= ~IEEE80211_VIF_EML_ACTIVE;

	iwl_mvm_update_smps_on_active_links(mvm, vif, IWL_MVM_SMPS_REQ_FW,
					    IEEE80211_SMPS_AUTOMATIC);

	for_each_vif_active_link(vif, link_conf, link_id) {
		struct ieee80211_chanctx_conf *chanctx_conf;
		struct iwl_mvm_phy_ctxt *phy_ctxt;
		u8 static_chains, dynamic_chains;

		mvmvif->link[link_id]->listen_lmac = false;

		rcu_read_lock();

		chanctx_conf = rcu_dereference(link_conf->chanctx_conf);
		phy_ctxt = mvmvif->link[link_id]->phy_ctxt;

		if (!chanctx_conf || !phy_ctxt) {
			rcu_read_unlock();
			continue;
		}

		phy_ctxt->rlc_disabled = false;
		static_chains = chanctx_conf->rx_chains_static;
		dynamic_chains = chanctx_conf->rx_chains_dynamic;

		rcu_read_unlock();

		ret = iwl_mvm_phy_send_rlc(mvm, phy_ctxt, static_chains,
					   dynamic_chains);
		if (ret)
			break;
	}

	iwl_mvm_request_periodic_system_statistics(mvm, false);

	/* Start a new counting window */
	iwl_mvm_restart_mpdu_count(mvm, mvmvif);

	return ret;
}

static void
__iwl_mvm_mld_unassign_vif_chanctx(struct iwl_mvm *mvm,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *link_conf,
				   struct ieee80211_chanctx_conf *ctx,
				   bool switching_chanctx)

{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	unsigned int n_active = iwl_mvm_mld_count_active_links(mvmvif);
	unsigned int link_id = link_conf->link_id;

	/* shouldn't happen, but verify link_id is valid before accessing */
	if (WARN_ON_ONCE(!mvmvif->link[link_id]))
		return;

	if (vif->type == NL80211_IFTYPE_AP && switching_chanctx) {
		mvmvif->csa_countdown = false;

		/* Set CS bit on all the stations */
		iwl_mvm_modify_all_sta_disable_tx(mvm, mvmvif, true);

		/* Save blocked iface, the timeout is set on the next beacon */
		rcu_assign_pointer(mvm->csa_tx_blocked_vif, vif);

		mvmvif->ap_ibss_active = false;
	}

	iwl_mvm_link_changed(mvm, vif, link_conf,
			     LINK_CONTEXT_MODIFY_ACTIVE, false);

	if (iwl_mvm_is_esr_supported(mvm->fwrt.trans) && n_active > 1) {
		int ret = iwl_mvm_esr_mode_inactive(mvm, vif);

		if (ret)
			IWL_ERR(mvm, "failed to deactivate ESR mode (%d)\n",
				ret);
	}

	if (vif->type == NL80211_IFTYPE_MONITOR)
		iwl_mvm_mld_rm_snif_sta(mvm, vif);

	if (switching_chanctx)
		return;
	mvmvif->link[link_id]->phy_ctxt = NULL;
	iwl_mvm_power_update_mac(mvm);
}

static void iwl_mvm_mld_unassign_vif_chanctx(struct ieee80211_hw *hw,
					     struct ieee80211_vif *vif,
					     struct ieee80211_bss_conf *link_conf,
					     struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);
	__iwl_mvm_mld_unassign_vif_chanctx(mvm, vif, link_conf, ctx, false);
	/* in the non-MLD case, remove/re-add the link to clean up FW state */
	if (!ieee80211_vif_is_mld(vif) && !mvmvif->ap_sta &&
	    !WARN_ON_ONCE(vif->cfg.assoc)) {
		iwl_mvm_remove_link(mvm, vif, link_conf);
		iwl_mvm_add_link(mvm, vif, link_conf);
	}
	mutex_unlock(&mvm->mutex);

	/* update EMLSR mode */
	if (ieee80211_vif_type_p2p(vif) != NL80211_IFTYPE_STATION)
		iwl_mvm_esr_non_bss_link(mvm, vif, link_conf->link_id, false);
}

static void
iwl_mvm_tpe_sta_cmd_data(struct iwl_txpower_constraints_cmd *cmd,
			 const struct ieee80211_bss_conf *bss_info)
{
	u8 i;

	/*
	 * NOTE: the 0 here is IEEE80211_TPE_CAT_6GHZ_DEFAULT,
	 * we fully ignore IEEE80211_TPE_CAT_6GHZ_SUBORDINATE
	 */

	BUILD_BUG_ON(ARRAY_SIZE(cmd->psd_pwr) !=
		     ARRAY_SIZE(bss_info->tpe.psd_local[0].power));

	/* if not valid, mac80211 puts default (max value) */
	for (i = 0; i < ARRAY_SIZE(cmd->psd_pwr); i++)
		cmd->psd_pwr[i] = min(bss_info->tpe.psd_local[0].power[i],
				      bss_info->tpe.psd_reg_client[0].power[i]);

	BUILD_BUG_ON(ARRAY_SIZE(cmd->eirp_pwr) !=
		     ARRAY_SIZE(bss_info->tpe.max_local[0].power));

	for (i = 0; i < ARRAY_SIZE(cmd->eirp_pwr); i++)
		cmd->eirp_pwr[i] = min(bss_info->tpe.max_local[0].power[i],
				       bss_info->tpe.max_reg_client[0].power[i]);
}

void
iwl_mvm_send_ap_tx_power_constraint_cmd(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					struct ieee80211_bss_conf *bss_conf,
					bool is_ap)
{
	struct iwl_txpower_constraints_cmd cmd = {};
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_vif_link_info *link_info =
			mvmvif->link[bss_conf->link_id];
	u32 cmd_id = WIDE_ID(PHY_OPS_GROUP, AP_TX_POWER_CONSTRAINTS_CMD);
	u32 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd_id,
					    IWL_FW_CMD_VER_UNKNOWN);
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (cmd_ver == IWL_FW_CMD_VER_UNKNOWN)
		return;

	if (!link_info->active ||
	    link_info->fw_link_id == IWL_MVM_FW_LINK_ID_INVALID)
		return;

	if (bss_conf->chanreq.oper.chan->band != NL80211_BAND_6GHZ)
		return;

	cmd.link_id = cpu_to_le16(link_info->fw_link_id);
	memset(cmd.psd_pwr, DEFAULT_TPE_TX_POWER, sizeof(cmd.psd_pwr));
	memset(cmd.eirp_pwr, DEFAULT_TPE_TX_POWER, sizeof(cmd.eirp_pwr));

	if (is_ap) {
		cmd.ap_type = cpu_to_le16(IWL_6GHZ_AP_TYPE_VLP);
	} else if (bss_conf->power_type == IEEE80211_REG_UNSET_AP) {
		return;
	} else {
		cmd.ap_type = cpu_to_le16(bss_conf->power_type - 1);
		iwl_mvm_tpe_sta_cmd_data(&cmd, bss_conf);
	}

	ret = iwl_mvm_send_cmd_pdu(mvm,
				   WIDE_ID(PHY_OPS_GROUP,
					   AP_TX_POWER_CONSTRAINTS_CMD),
				   0, sizeof(cmd), &cmd);
	if (ret)
		IWL_ERR(mvm,
			"failed to send AP_TX_POWER_CONSTRAINTS_CMD (%d)\n",
			ret);
}

static int iwl_mvm_mld_start_ap_ibss(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	guard(mvm)(mvm);

	if (vif->type == NL80211_IFTYPE_AP)
		iwl_mvm_send_ap_tx_power_constraint_cmd(mvm, vif,
							link_conf, true);

	/* Send the beacon template */
	ret = iwl_mvm_mac_ctxt_beacon_changed(mvm, vif, link_conf);
	if (ret)
		return ret;

	/* the link should be already activated when assigning chan context */
	ret = iwl_mvm_link_changed(mvm, vif, link_conf,
				   LINK_CONTEXT_MODIFY_ALL &
				   ~LINK_CONTEXT_MODIFY_ACTIVE,
				   true);
	if (ret)
		return ret;

	ret = iwl_mvm_mld_add_mcast_sta(mvm, vif, link_conf);
	if (ret)
		return ret;

	/* Send the bcast station. At this stage the TBTT and DTIM time
	 * events are added and applied to the scheduler
	 */
	ret = iwl_mvm_mld_add_bcast_sta(mvm, vif, link_conf);
	if (ret)
		goto out_rm_mcast;

	if (iwl_mvm_start_ap_ibss_common(hw, vif, &ret))
		goto out_failed;

	/* Need to update the P2P Device MAC (only GO, IBSS is single vif) */
	if (vif->p2p && mvm->p2p_device_vif)
		iwl_mvm_mld_mac_ctxt_changed(mvm, mvm->p2p_device_vif, false);

	iwl_mvm_bt_coex_vif_change(mvm);

	/* we don't support TDLS during DCM */
	if (iwl_mvm_phy_ctx_count(mvm) > 1)
		iwl_mvm_teardown_tdls_peers(mvm);

	iwl_mvm_ftm_restart_responder(mvm, vif, link_conf);

	return 0;

out_failed:
	iwl_mvm_power_update_mac(mvm);
	mvmvif->ap_ibss_active = false;
	iwl_mvm_mld_rm_bcast_sta(mvm, vif, link_conf);
out_rm_mcast:
	iwl_mvm_mld_rm_mcast_sta(mvm, vif, link_conf);
	return ret;
}

static int iwl_mvm_mld_start_ap(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *link_conf)
{
	return iwl_mvm_mld_start_ap_ibss(hw, vif, link_conf);
}

static int iwl_mvm_mld_start_ibss(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif)
{
	return iwl_mvm_mld_start_ap_ibss(hw, vif, &vif->bss_conf);
}

static void iwl_mvm_mld_stop_ap_ibss(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	guard(mvm)(mvm);

	iwl_mvm_stop_ap_ibss_common(mvm, vif);

	/* Need to update the P2P Device MAC (only GO, IBSS is single vif) */
	if (vif->p2p && mvm->p2p_device_vif)
		iwl_mvm_mld_mac_ctxt_changed(mvm, mvm->p2p_device_vif, false);

	iwl_mvm_ftm_responder_clear(mvm, vif);

	iwl_mvm_mld_rm_bcast_sta(mvm, vif, link_conf);
	iwl_mvm_mld_rm_mcast_sta(mvm, vif, link_conf);

	iwl_mvm_power_update_mac(mvm);
}

static void iwl_mvm_mld_stop_ap(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *link_conf)
{
	iwl_mvm_mld_stop_ap_ibss(hw, vif, link_conf);
}

static void iwl_mvm_mld_stop_ibss(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif)
{
	iwl_mvm_mld_stop_ap_ibss(hw, vif, &vif->bss_conf);
}

static int iwl_mvm_mld_mac_sta_state(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta,
				     enum ieee80211_sta_state old_state,
				     enum ieee80211_sta_state new_state)
{
	static const struct iwl_mvm_sta_state_ops callbacks = {
		.add_sta = iwl_mvm_mld_add_sta,
		.update_sta = iwl_mvm_mld_update_sta,
		.rm_sta = iwl_mvm_mld_rm_sta,
		.mac_ctxt_changed = iwl_mvm_mld_mac_ctxt_changed,
	};

	return iwl_mvm_mac_sta_state_common(hw, vif, sta, old_state, new_state,
					    &callbacks);
}

static bool iwl_mvm_esr_bw_criteria(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif,
				    struct ieee80211_bss_conf *link_conf)
{
	struct ieee80211_bss_conf *other_link;
	int link_id;

	/* Exit EMLSR if links don't have equal bandwidths */
	for_each_vif_active_link(vif, other_link, link_id) {
		if (link_id == link_conf->link_id)
			continue;
		if (link_conf->chanreq.oper.width ==
		    other_link->chanreq.oper.width)
			return true;
	}

	return false;
}

static void
iwl_mvm_mld_link_info_changed_station(struct iwl_mvm *mvm,
				      struct ieee80211_vif *vif,
				      struct ieee80211_bss_conf *link_conf,
				      u64 changes)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	bool has_he, has_eht;
	u32 link_changes = 0;
	int ret;

	if (WARN_ON_ONCE(!mvmvif->link[link_conf->link_id]))
		return;

	has_he = link_conf->he_support && !iwlwifi_mod_params.disable_11ax;
	has_eht = link_conf->eht_support && !iwlwifi_mod_params.disable_11be;

	/* Update EDCA params */
	if (changes & BSS_CHANGED_QOS && vif->cfg.assoc && link_conf->qos)
		link_changes |= LINK_CONTEXT_MODIFY_QOS_PARAMS;

	if (changes & BSS_CHANGED_ERP_SLOT)
		link_changes |= LINK_CONTEXT_MODIFY_RATES_INFO;

	if (vif->cfg.assoc && (has_he || has_eht)) {
		IWL_DEBUG_MAC80211(mvm, "Associated in HE mode\n");
		link_changes |= LINK_CONTEXT_MODIFY_HE_PARAMS;
	}

	if ((changes & BSS_CHANGED_BANDWIDTH) &&
	    ieee80211_vif_link_active(vif, link_conf->link_id) &&
	    mvmvif->esr_active &&
	    !iwl_mvm_esr_bw_criteria(mvm, vif, link_conf))
		iwl_mvm_exit_esr(mvm, vif,
				 IWL_MVM_ESR_EXIT_BANDWIDTH,
				 iwl_mvm_get_primary_link(vif));

	/* if associated, maybe puncturing changed - we'll check later */
	if (vif->cfg.assoc)
		link_changes |= LINK_CONTEXT_MODIFY_EHT_PARAMS;

	if (link_changes) {
		ret = iwl_mvm_link_changed(mvm, vif, link_conf, link_changes,
					   true);
		if (ret)
			IWL_ERR(mvm, "failed to update link\n");
	}

	ret = iwl_mvm_mld_mac_ctxt_changed(mvm, vif, false);
	if (ret)
		IWL_ERR(mvm, "failed to update MAC %pM\n", vif->addr);

	memcpy(mvmvif->link[link_conf->link_id]->bssid, link_conf->bssid,
	       ETH_ALEN);

	iwl_mvm_bss_info_changed_station_common(mvm, vif, link_conf, changes);
}

static bool iwl_mvm_mld_vif_have_valid_ap_sta(struct iwl_mvm_vif *mvmvif)
{
	int i;

	for_each_mvm_vif_valid_link(mvmvif, i) {
		if (mvmvif->link[i]->ap_sta_id != IWL_MVM_INVALID_STA)
			return true;
	}

	return false;
}

static void iwl_mvm_mld_vif_delete_all_stas(struct iwl_mvm *mvm,
					    struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int i, ret;

	if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
		return;

	for_each_mvm_vif_valid_link(mvmvif, i) {
		struct iwl_mvm_vif_link_info *link = mvmvif->link[i];

		if (!link)
			continue;

		iwl_mvm_sec_key_remove_ap(mvm, vif, link, i);
		ret = iwl_mvm_mld_rm_sta_id(mvm, link->ap_sta_id);
		if (ret)
			IWL_ERR(mvm, "failed to remove AP station\n");

		link->ap_sta_id = IWL_MVM_INVALID_STA;
	}
}

static void iwl_mvm_mld_vif_cfg_changed_station(struct iwl_mvm *mvm,
						struct ieee80211_vif *vif,
						u64 changes)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct ieee80211_bss_conf *link_conf;
	bool protect = false;
	unsigned int i;
	int ret;

	/* This might get called without active links during the
	 * chanctx switch, but we don't care about it anyway.
	 */
	if (changes == BSS_CHANGED_IDLE)
		return;

	ret = iwl_mvm_mld_mac_ctxt_changed(mvm, vif, false);
	if (ret)
		IWL_ERR(mvm, "failed to update MAC %pM\n", vif->addr);

	mvmvif->associated = vif->cfg.assoc;

	if (changes & BSS_CHANGED_ASSOC) {
		if (vif->cfg.assoc) {
			mvmvif->session_prot_connection_loss = false;

			/* clear statistics to get clean beacon counter */
			iwl_mvm_request_statistics(mvm, true);
			iwl_mvm_sf_update(mvm, vif, false);
			iwl_mvm_power_vif_assoc(mvm, vif);

			for_each_mvm_vif_valid_link(mvmvif, i) {
				memset(&mvmvif->link[i]->beacon_stats, 0,
				       sizeof(mvmvif->link[i]->beacon_stats));

				if (vif->p2p) {
					iwl_mvm_update_smps(mvm, vif,
							    IWL_MVM_SMPS_REQ_PROT,
							    IEEE80211_SMPS_DYNAMIC, i);
				}

				rcu_read_lock();
				link_conf = rcu_dereference(vif->link_conf[i]);
				if (link_conf && !link_conf->dtim_period)
					protect = true;
				rcu_read_unlock();
			}

			if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) &&
			    protect) {
				/* We are in assoc so only one link is active-
				 * The association link
				 */
				unsigned int link_id =
					ffs(vif->active_links) - 1;

				/* If we're not restarting and still haven't
				 * heard a beacon (dtim period unknown) then
				 * make sure we still have enough minimum time
				 * remaining in the time event, since the auth
				 * might actually have taken quite a while
				 * (especially for SAE) and so the remaining
				 * time could be small without us having heard
				 * a beacon yet.
				 */
				iwl_mvm_protect_assoc(mvm, vif, 0, link_id);
			}

			iwl_mvm_sf_update(mvm, vif, false);

			/* FIXME: need to decide about misbehaving AP handling */
			iwl_mvm_power_vif_assoc(mvm, vif);
		} else if (iwl_mvm_mld_vif_have_valid_ap_sta(mvmvif)) {
			iwl_mvm_mei_host_disassociated(mvm);

			/* If update fails - SF might be running in associated
			 * mode while disassociated - which is forbidden.
			 */
			ret = iwl_mvm_sf_update(mvm, vif, false);
			WARN_ONCE(ret &&
				  !test_bit(IWL_MVM_STATUS_HW_RESTART_REQUESTED,
					    &mvm->status),
				  "Failed to update SF upon disassociation\n");

			/* If we get an assert during the connection (after the
			 * station has been added, but before the vif is set
			 * to associated), mac80211 will re-add the station and
			 * then configure the vif. Since the vif is not
			 * associated, we would remove the station here and
			 * this would fail the recovery.
			 */
			iwl_mvm_mld_vif_delete_all_stas(mvm, vif);
		}

		iwl_mvm_bss_info_changed_station_assoc(mvm, vif, changes);
	}

	if (changes & BSS_CHANGED_PS) {
		ret = iwl_mvm_power_update_mac(mvm);
		if (ret)
			IWL_ERR(mvm, "failed to update power mode\n");
	}

	if (changes & (BSS_CHANGED_MLD_VALID_LINKS | BSS_CHANGED_MLD_TTLM) &&
	    ieee80211_vif_is_mld(vif) && mvmvif->authorized)
		wiphy_delayed_work_queue(mvm->hw->wiphy,
					 &mvmvif->mlo_int_scan_wk, 0);
}

static void
iwl_mvm_mld_link_info_changed_ap_ibss(struct iwl_mvm *mvm,
				      struct ieee80211_vif *vif,
				      struct ieee80211_bss_conf *link_conf,
				      u64 changes)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	u32 link_changes = LINK_CONTEXT_MODIFY_PROTECT_FLAGS |
			   LINK_CONTEXT_MODIFY_QOS_PARAMS;

	/* Changes will be applied when the AP/IBSS is started */
	if (!mvmvif->ap_ibss_active)
		return;

	if (link_conf->he_support)
		link_changes |= LINK_CONTEXT_MODIFY_HE_PARAMS;

	if (changes & BSS_CHANGED_ERP_SLOT)
		link_changes |= LINK_CONTEXT_MODIFY_RATES_INFO;

	if (changes & (BSS_CHANGED_ERP_CTS_PROT | BSS_CHANGED_ERP_SLOT |
		       BSS_CHANGED_HT |
		       BSS_CHANGED_BANDWIDTH | BSS_CHANGED_QOS |
		       BSS_CHANGED_HE_BSS_COLOR) &&
		       iwl_mvm_link_changed(mvm, vif, link_conf,
					    link_changes, true))
		IWL_ERR(mvm, "failed to update MAC %pM\n", vif->addr);

	/* Need to send a new beacon template to the FW */
	if (changes & BSS_CHANGED_BEACON &&
	    iwl_mvm_mac_ctxt_beacon_changed(mvm, vif, link_conf))
		IWL_WARN(mvm, "Failed updating beacon data\n");

	/* FIXME: need to decide if we need FTM responder per link */
	if (changes & BSS_CHANGED_FTM_RESPONDER) {
		int ret = iwl_mvm_ftm_start_responder(mvm, vif, link_conf);

		if (ret)
			IWL_WARN(mvm, "Failed to enable FTM responder (%d)\n",
				 ret);
	}
}

static void iwl_mvm_mld_link_info_changed(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  struct ieee80211_bss_conf *link_conf,
					  u64 changes)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	guard(mvm)(mvm);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		iwl_mvm_mld_link_info_changed_station(mvm, vif, link_conf,
						      changes);
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
		iwl_mvm_mld_link_info_changed_ap_ibss(mvm, vif, link_conf,
						      changes);
		break;
	case NL80211_IFTYPE_MONITOR:
		if (changes & BSS_CHANGED_MU_GROUPS)
			iwl_mvm_update_mu_groups(mvm, vif);
		break;
	default:
		/* shouldn't happen */
		WARN_ON_ONCE(1);
	}

	if (changes & BSS_CHANGED_TXPOWER) {
		IWL_DEBUG_CALIB(mvm, "Changing TX Power to %d dBm\n",
				link_conf->txpower);
		iwl_mvm_set_tx_power(mvm, vif, link_conf->txpower);
	}
}

static void iwl_mvm_mld_vif_cfg_changed(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					u64 changes)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	guard(mvm)(mvm);

	if (changes & BSS_CHANGED_IDLE && !vif->cfg.idle)
		iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_SCHED, true);

	if (vif->type == NL80211_IFTYPE_STATION)
		iwl_mvm_mld_vif_cfg_changed_station(mvm, vif, changes);
}

static int
iwl_mvm_mld_switch_vif_chanctx(struct ieee80211_hw *hw,
			       struct ieee80211_vif_chanctx_switch *vifs,
			       int n_vifs,
			       enum ieee80211_chanctx_switch_mode mode)
{
	static const struct iwl_mvm_switch_vif_chanctx_ops ops = {
		.__assign_vif_chanctx = __iwl_mvm_mld_assign_vif_chanctx,
		.__unassign_vif_chanctx = __iwl_mvm_mld_unassign_vif_chanctx,
	};

	return iwl_mvm_switch_vif_chanctx_common(hw, vifs, n_vifs, mode, &ops);
}

static void iwl_mvm_mld_config_iface_filter(struct ieee80211_hw *hw,
					    struct ieee80211_vif *vif,
					    unsigned int filter_flags,
					    unsigned int changed_flags)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	/* We support only filter for probe requests */
	if (!(changed_flags & FIF_PROBE_REQ))
		return;

	/* Supported only for p2p client interfaces */
	if (vif->type != NL80211_IFTYPE_STATION || !vif->cfg.assoc ||
	    !vif->p2p)
		return;

	guard(mvm)(mvm);
	iwl_mvm_mld_mac_ctxt_changed(mvm, vif, false);
}

static int
iwl_mvm_mld_mac_conf_tx(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			unsigned int link_id, u16 ac,
			const struct ieee80211_tx_queue_params *params)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_vif_link_info *mvm_link = mvmvif->link[link_id];

	if (!mvm_link)
		return -EINVAL;

	mvm_link->queue_params[ac] = *params;

	/* No need to update right away, we'll get BSS_CHANGED_QOS
	 * The exception is P2P_DEVICE interface which needs immediate update.
	 */
	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		guard(mvm)(mvm);
		return iwl_mvm_link_changed(mvm, vif, &vif->bss_conf,
					    LINK_CONTEXT_MODIFY_QOS_PARAMS,
					    true);
	}
	return 0;
}

static int iwl_mvm_mld_roc_link(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	int ret;

	lockdep_assert_held(&mvm->mutex);

	/* The PHY context ID might have changed so need to set it */
	ret = iwl_mvm_link_changed(mvm, vif, &vif->bss_conf, 0, false);
	if (WARN(ret, "Failed to set PHY context ID\n"))
		return ret;

	ret = iwl_mvm_link_changed(mvm, vif, &vif->bss_conf,
				   LINK_CONTEXT_MODIFY_ACTIVE |
				   LINK_CONTEXT_MODIFY_RATES_INFO,
				   true);

	if (WARN(ret, "Failed linking P2P_DEVICE\n"))
		return ret;

	/* The station and queue allocation must be done only after the linking
	 * is done, as otherwise the FW might incorrectly configure its state.
	 */
	return iwl_mvm_mld_add_bcast_sta(mvm, vif, &vif->bss_conf);
}

static int iwl_mvm_mld_roc(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_channel *channel, int duration,
			   enum ieee80211_roc_type type)
{
	static const struct iwl_mvm_roc_ops ops = {
		.add_aux_sta_for_hs20 = iwl_mvm_mld_add_aux_sta,
		.link = iwl_mvm_mld_roc_link,
	};

	return iwl_mvm_roc_common(hw, vif, channel, duration, type, &ops);
}

static int
iwl_mvm_mld_change_vif_links(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     u16 old_links, u16 new_links,
			     struct ieee80211_bss_conf *old[IEEE80211_MLD_MAX_NUM_LINKS])
{
	struct iwl_mvm_vif_link_info *new_link[IEEE80211_MLD_MAX_NUM_LINKS] = {};
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	u16 removed = old_links & ~new_links;
	u16 added = new_links & ~old_links;
	int err, i;

	for (i = 0; i < IEEE80211_MLD_MAX_NUM_LINKS; i++) {
		int r;

		if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
			break;

		if (!(added & BIT(i)))
			continue;
		new_link[i] = kzalloc(sizeof(*new_link[i]), GFP_KERNEL);
		if (!new_link[i]) {
			err = -ENOMEM;
			goto free;
		}

		new_link[i]->bcast_sta.sta_id = IWL_MVM_INVALID_STA;
		new_link[i]->mcast_sta.sta_id = IWL_MVM_INVALID_STA;
		new_link[i]->ap_sta_id = IWL_MVM_INVALID_STA;
		new_link[i]->fw_link_id = IWL_MVM_FW_LINK_ID_INVALID;

		for (r = 0; r < NUM_IWL_MVM_SMPS_REQ; r++)
			new_link[i]->smps_requests[r] =
				IEEE80211_SMPS_AUTOMATIC;
	}

	mutex_lock(&mvm->mutex);

	if (old_links == 0) {
		err = iwl_mvm_disable_link(mvm, vif, &vif->bss_conf);
		if (err)
			goto out_err;
		mvmvif->link[0] = NULL;
	}

	for (i = 0; i < IEEE80211_MLD_MAX_NUM_LINKS; i++) {
		if (removed & BIT(i)) {
			struct ieee80211_bss_conf *link_conf = old[i];

			err = iwl_mvm_disable_link(mvm, vif, link_conf);
			if (err)
				goto out_err;
			kfree(mvmvif->link[i]);
			mvmvif->link[i] = NULL;
		} else if (added & BIT(i)) {
			struct ieee80211_bss_conf *link_conf;

			link_conf = link_conf_dereference_protected(vif, i);
			if (WARN_ON(!link_conf))
				continue;

			if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART,
				      &mvm->status))
				mvmvif->link[i] = new_link[i];
			new_link[i] = NULL;
			err = iwl_mvm_add_link(mvm, vif, link_conf);
			if (err)
				goto out_err;
		}
	}

	err = 0;
	if (new_links == 0) {
		mvmvif->link[0] = &mvmvif->deflink;
		err = iwl_mvm_add_link(mvm, vif, &vif->bss_conf);
		if (err == 0)
			mvmvif->primary_link = 0;
	} else if (!(new_links & BIT(mvmvif->primary_link))) {
		/*
		 * Ensure we always have a valid primary_link, the real
		 * decision happens later when PHY is activated.
		 */
		mvmvif->primary_link = __ffs(new_links);
	}

out_err:
	/* we really don't have a good way to roll back here ... */
	mutex_unlock(&mvm->mutex);

free:
	for (i = 0; i < IEEE80211_MLD_MAX_NUM_LINKS; i++)
		kfree(new_link[i]);
	return err;
}

static int
iwl_mvm_mld_change_sta_links(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     u16 old_links, u16 new_links)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	guard(mvm)(mvm);
	return iwl_mvm_mld_update_sta_links(mvm, vif, sta, old_links, new_links);
}

bool iwl_mvm_vif_has_esr_cap(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	const struct wiphy_iftype_ext_capab *ext_capa;

	lockdep_assert_held(&mvm->mutex);

	if (!ieee80211_vif_is_mld(vif) || !vif->cfg.assoc ||
	    hweight16(ieee80211_vif_usable_links(vif)) == 1)
		return false;

	if (!(vif->cfg.eml_cap & IEEE80211_EML_CAP_EMLSR_SUPP))
		return false;

	ext_capa = cfg80211_get_iftype_ext_capa(mvm->hw->wiphy,
						ieee80211_vif_type_p2p(vif));
	return (ext_capa &&
		(ext_capa->eml_capabilities & IEEE80211_EML_CAP_EMLSR_SUPP));
}

static bool iwl_mvm_mld_can_activate_links(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   u16 desired_links)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int n_links = hweight16(desired_links);

	if (n_links <= 1)
		return true;

	guard(mvm)(mvm);

	/* Check if HW supports the wanted number of links */
	if (n_links > iwl_mvm_max_active_links(mvm, vif))
		return false;

	/* If it is an eSR device, check that we can enter eSR */
	return iwl_mvm_is_esr_supported(mvm->fwrt.trans) &&
	       iwl_mvm_vif_has_esr_cap(mvm, vif);
}

static enum ieee80211_neg_ttlm_res
iwl_mvm_mld_can_neg_ttlm(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ieee80211_neg_ttlm *neg_ttlm)
{
	u16 map;
	u8 i;

	/* Verify all TIDs are mapped to the same links set */
	map = neg_ttlm->downlink[0];
	for (i = 0; i < IEEE80211_TTLM_NUM_TIDS; i++) {
		if (neg_ttlm->downlink[i] != neg_ttlm->uplink[i] ||
		    neg_ttlm->uplink[i] != map)
			return NEG_TTLM_RES_REJECT;
	}

	return NEG_TTLM_RES_ACCEPT;
}

static int
iwl_mvm_mld_mac_pre_channel_switch(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_channel_switch *chsw)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);
	if (mvmvif->esr_active) {
		u8 primary = iwl_mvm_get_primary_link(vif);
		int selected;

		/* prefer primary unless quiet CSA on it */
		if (chsw->link_id == primary && chsw->block_tx)
			selected = iwl_mvm_get_other_link(vif, primary);
		else
			selected = primary;

		iwl_mvm_exit_esr(mvm, vif, IWL_MVM_ESR_EXIT_CSA, selected);
		mutex_unlock(&mvm->mutex);

		/*
		 * If we've not kept the link active that's doing the CSA
		 * then we don't need to do anything else, just return.
		 */
		if (selected != chsw->link_id)
			return 0;

		mutex_lock(&mvm->mutex);
	}

	ret = iwl_mvm_pre_channel_switch(mvm, vif, chsw);
	mutex_unlock(&mvm->mutex);

	return ret;
}

const struct ieee80211_ops iwl_mvm_mld_hw_ops = {
	.tx = iwl_mvm_mac_tx,
	.wake_tx_queue = iwl_mvm_mac_wake_tx_queue,
	.ampdu_action = iwl_mvm_mac_ampdu_action,
	.get_antenna = iwl_mvm_op_get_antenna,
	.set_antenna = iwl_mvm_op_set_antenna,
	.start = iwl_mvm_mac_start,
	.reconfig_complete = iwl_mvm_mac_reconfig_complete,
	.stop = iwl_mvm_mac_stop,
	.add_interface = iwl_mvm_mld_mac_add_interface,
	.remove_interface = iwl_mvm_mld_mac_remove_interface,
	.config = iwl_mvm_mac_config,
	.prepare_multicast = iwl_mvm_prepare_multicast,
	.configure_filter = iwl_mvm_configure_filter,
	.config_iface_filter = iwl_mvm_mld_config_iface_filter,
	.link_info_changed = iwl_mvm_mld_link_info_changed,
	.vif_cfg_changed = iwl_mvm_mld_vif_cfg_changed,
	.hw_scan = iwl_mvm_mac_hw_scan,
	.cancel_hw_scan = iwl_mvm_mac_cancel_hw_scan,
	.sta_pre_rcu_remove = iwl_mvm_sta_pre_rcu_remove,
	.sta_state = iwl_mvm_mld_mac_sta_state,
	.sta_notify = iwl_mvm_mac_sta_notify,
	.allow_buffered_frames = iwl_mvm_mac_allow_buffered_frames,
	.release_buffered_frames = iwl_mvm_mac_release_buffered_frames,
	.set_rts_threshold = iwl_mvm_mac_set_rts_threshold,
	.sta_rc_update = iwl_mvm_sta_rc_update,
	.conf_tx = iwl_mvm_mld_mac_conf_tx,
	.mgd_prepare_tx = iwl_mvm_mac_mgd_prepare_tx,
	.mgd_complete_tx = iwl_mvm_mac_mgd_complete_tx,
	.mgd_protect_tdls_discover = iwl_mvm_mac_mgd_protect_tdls_discover,
	.flush = iwl_mvm_mac_flush,
	.flush_sta = iwl_mvm_mac_flush_sta,
	.sched_scan_start = iwl_mvm_mac_sched_scan_start,
	.sched_scan_stop = iwl_mvm_mac_sched_scan_stop,
	.set_key = iwl_mvm_mac_set_key,
	.update_tkip_key = iwl_mvm_mac_update_tkip_key,
	.remain_on_channel = iwl_mvm_mld_roc,
	.cancel_remain_on_channel = iwl_mvm_cancel_roc,
	.add_chanctx = iwl_mvm_add_chanctx,
	.remove_chanctx = iwl_mvm_remove_chanctx,
	.change_chanctx = iwl_mvm_change_chanctx,
	.assign_vif_chanctx = iwl_mvm_mld_assign_vif_chanctx,
	.unassign_vif_chanctx = iwl_mvm_mld_unassign_vif_chanctx,
	.switch_vif_chanctx = iwl_mvm_mld_switch_vif_chanctx,

	.start_ap = iwl_mvm_mld_start_ap,
	.stop_ap = iwl_mvm_mld_stop_ap,
	.join_ibss = iwl_mvm_mld_start_ibss,
	.leave_ibss = iwl_mvm_mld_stop_ibss,

	.tx_last_beacon = iwl_mvm_tx_last_beacon,

	.channel_switch = iwl_mvm_channel_switch,
	.pre_channel_switch = iwl_mvm_mld_mac_pre_channel_switch,
	.post_channel_switch = iwl_mvm_post_channel_switch,
	.abort_channel_switch = iwl_mvm_abort_channel_switch,
	.channel_switch_rx_beacon = iwl_mvm_channel_switch_rx_beacon,

	.tdls_channel_switch = iwl_mvm_tdls_channel_switch,
	.tdls_cancel_channel_switch = iwl_mvm_tdls_cancel_channel_switch,
	.tdls_recv_channel_switch = iwl_mvm_tdls_recv_channel_switch,

	.event_callback = iwl_mvm_mac_event_callback,

	.sync_rx_queues = iwl_mvm_sync_rx_queues,

	CFG80211_TESTMODE_CMD(iwl_mvm_mac_testmode_cmd)

#ifdef CONFIG_PM_SLEEP
	/* look at d3.c */
	.suspend = iwl_mvm_suspend,
	.resume = iwl_mvm_resume,
	.set_wakeup = iwl_mvm_set_wakeup,
	.set_rekey_data = iwl_mvm_set_rekey_data,
#if IS_ENABLED(CONFIG_IPV6)
	.ipv6_addr_change = iwl_mvm_ipv6_addr_change,
#endif
	.set_default_unicast_key = iwl_mvm_set_default_unicast_key,
#endif
	.get_survey = iwl_mvm_mac_get_survey,
	.sta_statistics = iwl_mvm_mac_sta_statistics,
	.get_ftm_responder_stats = iwl_mvm_mac_get_ftm_responder_stats,
	.start_pmsr = iwl_mvm_start_pmsr,
	.abort_pmsr = iwl_mvm_abort_pmsr,

#ifdef CONFIG_IWLWIFI_DEBUGFS
	.vif_add_debugfs = iwl_mvm_vif_add_debugfs,
	.link_add_debugfs = iwl_mvm_link_add_debugfs,
	.link_sta_add_debugfs = iwl_mvm_link_sta_add_debugfs,
#endif
	.set_hw_timestamp = iwl_mvm_set_hw_timestamp,

	.change_vif_links = iwl_mvm_mld_change_vif_links,
	.change_sta_links = iwl_mvm_mld_change_sta_links,
	.can_activate_links = iwl_mvm_mld_can_activate_links,
	.can_neg_ttlm = iwl_mvm_mld_can_neg_ttlm,
};
