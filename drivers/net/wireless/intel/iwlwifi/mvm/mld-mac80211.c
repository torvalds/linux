// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2022 Intel Corporation
 */
#include "mvm.h"

static int iwl_mvm_mld_mac_add_interface(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	mutex_lock(&mvm->mutex);

	mvmvif->mvm = mvm;
	RCU_INIT_POINTER(mvmvif->deflink.probe_resp_data, NULL);

	/* Not much to do here. The stack will not allow interface
	 * types or combinations that we didn't advertise, so we
	 * don't really have to check the types.
	 */

	/* make sure that beacon statistics don't go backwards with FW reset */
	if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
		mvmvif->deflink.beacon_stats.accu_num_beacons +=
			mvmvif->deflink.beacon_stats.num_beacons;

	/* Allocate resources for the MAC context, and add it to the fw  */
	ret = iwl_mvm_mac_ctxt_init(mvm, vif);
	if (ret)
		goto out_unlock;

	rcu_assign_pointer(mvm->vif_id_to_mac[mvmvif->id], vif);

	mvmvif->features |= hw->netdev_features;

	/* the first link always points to the default one */
	mvmvif->link[0] = &mvmvif->deflink;

	ret = iwl_mvm_mld_mac_ctxt_add(mvm, vif);
	if (ret)
		goto out_unlock;

	/* beacon filtering */
	ret = iwl_mvm_disable_beacon_filter(mvm, vif, 0);
	if (ret)
		goto out_remove_mac;

	if (!mvm->bf_allowed_vif &&
	    vif->type == NL80211_IFTYPE_STATION && !vif->p2p) {
		mvm->bf_allowed_vif = mvmvif;
		vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER |
				     IEEE80211_VIF_SUPPORTS_CQM_RSSI;
	}

	/*
	 * P2P_DEVICE interface does not have a channel context assigned to it,
	 * so a dedicated PHY context is allocated to it and the corresponding
	 * MAC context is bound to it at this stage.
	 */
	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		mvmvif->deflink.phy_ctxt = iwl_mvm_get_free_phy_ctxt(mvm);
		if (!mvmvif->deflink.phy_ctxt) {
			ret = -ENOSPC;
			goto out_free_bf;
		}

		iwl_mvm_phy_ctxt_ref(mvm, mvmvif->deflink.phy_ctxt);
		ret = iwl_mvm_add_link(mvm, vif);
		if (ret)
			goto out_unref_phy;

		ret = iwl_mvm_link_changed(mvm, vif, LINK_CONTEXT_MODIFY_ACTIVE,
					   true);
		if (ret)
			goto out_remove_link;

		ret = iwl_mvm_mld_add_bcast_sta(mvm, vif);
		if (ret)
			goto out_remove_link;

		/* Save a pointer to p2p device vif, so it can later be used to
		 * update the p2p device MAC when a GO is started/stopped
		 */
		mvm->p2p_device_vif = vif;
	} else {
		ret = iwl_mvm_add_link(mvm, vif);
		if (ret)
			goto out_free_bf;
	}

	ret = iwl_mvm_power_update_mac(mvm);
	if (ret)
		goto out_free_bf;

	iwl_mvm_tcm_add_vif(mvm, vif);
	INIT_DELAYED_WORK(&mvmvif->csa_work,
			  iwl_mvm_channel_switch_disconnect_wk);

	if (vif->type == NL80211_IFTYPE_MONITOR) {
		mvm->monitor_on = true;
		ieee80211_hw_set(mvm->hw, RX_INCLUDES_FCS);
	}

	iwl_mvm_vif_dbgfs_register(mvm, vif);

	if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) &&
	    vif->type == NL80211_IFTYPE_STATION && !vif->p2p &&
	    !mvm->csme_vif && mvm->mei_registered) {
		iwl_mei_set_nic_info(vif->addr, mvm->nvm_data->hw_addr);
		iwl_mei_set_netdev(ieee80211_vif_to_wdev(vif)->netdev);
		mvm->csme_vif = vif;
	}

	goto out_unlock;

 out_remove_link:
	/* Link needs to be deactivated before removal */
	iwl_mvm_link_changed(mvm, vif, LINK_CONTEXT_MODIFY_ACTIVE, false);
	iwl_mvm_remove_link(mvm, vif);
 out_unref_phy:
	iwl_mvm_phy_ctxt_unref(mvm, mvmvif->deflink.phy_ctxt);
 out_free_bf:
	if (mvm->bf_allowed_vif == mvmvif) {
		mvm->bf_allowed_vif = NULL;
		vif->driver_flags &= ~(IEEE80211_VIF_BEACON_FILTER |
				       IEEE80211_VIF_SUPPORTS_CQM_RSSI);
	}
 out_remove_mac:
	mvmvif->deflink.phy_ctxt = NULL;
	mvmvif->link[0] = NULL;
	iwl_mvm_mld_mac_ctxt_remove(mvm, vif);
 out_unlock:
	mutex_unlock(&mvm->mutex);

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

	mutex_lock(&mvm->mutex);

	if (vif == mvm->csme_vif) {
		iwl_mei_set_netdev(NULL);
		mvm->csme_vif = NULL;
	}

	probe_data = rcu_dereference_protected(mvmvif->deflink.probe_resp_data,
					       lockdep_is_held(&mvm->mutex));
	RCU_INIT_POINTER(mvmvif->deflink.probe_resp_data, NULL);
	if (probe_data)
		kfree_rcu(probe_data, rcu_head);

	if (mvm->bf_allowed_vif == mvmvif) {
		mvm->bf_allowed_vif = NULL;
		vif->driver_flags &= ~(IEEE80211_VIF_BEACON_FILTER |
				       IEEE80211_VIF_SUPPORTS_CQM_RSSI);
	}

	if (vif->bss_conf.ftm_responder)
		memset(&mvm->ftm_resp_stats, 0, sizeof(mvm->ftm_resp_stats));

	iwl_mvm_vif_dbgfs_clean(mvm, vif);

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

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		mvm->p2p_device_vif = NULL;
		iwl_mvm_mld_rm_bcast_sta(mvm, vif);
		/* Link needs to be deactivated before removal */
		iwl_mvm_link_changed(mvm, vif, LINK_CONTEXT_MODIFY_ACTIVE,
				     false);
		iwl_mvm_remove_link(mvm, vif);
		iwl_mvm_phy_ctxt_unref(mvm, mvmvif->deflink.phy_ctxt);
		mvmvif->deflink.phy_ctxt = NULL;
	} else {
		iwl_mvm_remove_link(mvm, vif);
	}

	iwl_mvm_mld_mac_ctxt_remove(mvm, vif);

	RCU_INIT_POINTER(mvm->vif_id_to_mac[mvmvif->id], NULL);

	if (vif->type == NL80211_IFTYPE_MONITOR) {
		mvm->monitor_on = false;
		__clear_bit(IEEE80211_HW_RX_INCLUDES_FCS, mvm->hw->flags);
	}

	mutex_unlock(&mvm->mutex);
}

static int __iwl_mvm_mld_assign_vif_chanctx(struct iwl_mvm *mvm,
					    struct ieee80211_vif *vif,
					    struct ieee80211_chanctx_conf *ctx,
					    bool switching_chanctx)
{
	u16 *phy_ctxt_id = (u16 *)ctx->drv_priv;
	struct iwl_mvm_phy_ctxt *phy_ctxt = &mvm->phy_ctxts[*phy_ctxt_id];
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	mvmvif->deflink.phy_ctxt = phy_ctxt;

	if (switching_chanctx) {
		/* reactivate if we turned this off during channel switch */
		if (vif->type == NL80211_IFTYPE_AP)
			mvmvif->ap_ibss_active = true;
	}

	/* send it first with phy context ID */
	ret = iwl_mvm_link_changed(mvm, vif, 0, false);
	if (ret)
		goto out;

	/* then activate */
	ret = iwl_mvm_link_changed(mvm, vif, LINK_CONTEXT_MODIFY_ACTIVE |
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
		ret = iwl_mvm_mld_add_snif_sta(mvm, vif);
		if (ret)
			goto deactivate;
	}

	return 0;

deactivate:
	iwl_mvm_link_changed(mvm, vif, LINK_CONTEXT_MODIFY_ACTIVE, false);
out:
	mvmvif->deflink.phy_ctxt = NULL;
	iwl_mvm_power_update_mac(mvm);
	return ret;
}

static int iwl_mvm_mld_assign_vif_chanctx(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  struct ieee80211_bss_conf *link_conf,
					  struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);
	ret = __iwl_mvm_mld_assign_vif_chanctx(mvm, vif, ctx, false);
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void __iwl_mvm_mld_unassign_vif_chanctx(struct iwl_mvm *mvm,
					       struct ieee80211_vif *vif,
					       struct ieee80211_chanctx_conf *ctx,
					       bool switching_chanctx)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (vif->type == NL80211_IFTYPE_AP && switching_chanctx) {
		mvmvif->csa_countdown = false;

		/* Set CS bit on all the stations */
		iwl_mvm_modify_all_sta_disable_tx(mvm, mvmvif, true);

		/* Save blocked iface, the timeout is set on the next beacon */
		rcu_assign_pointer(mvm->csa_tx_blocked_vif, vif);

		mvmvif->ap_ibss_active = false;
	}

	if (vif->type == NL80211_IFTYPE_MONITOR)
		iwl_mvm_mld_rm_snif_sta(mvm, vif);

	iwl_mvm_link_changed(mvm, vif, LINK_CONTEXT_MODIFY_ACTIVE, false);

	if (switching_chanctx)
		return;
	mvmvif->deflink.phy_ctxt = NULL;
	iwl_mvm_power_update_mac(mvm);
}

static void iwl_mvm_mld_unassign_vif_chanctx(struct ieee80211_hw *hw,
					     struct ieee80211_vif *vif,
					     struct ieee80211_bss_conf *link_conf,
					     struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);
	__iwl_mvm_mld_unassign_vif_chanctx(mvm, vif, ctx, false);
	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_mld_start_ap_ibss(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	mutex_lock(&mvm->mutex);
	/* Send the beacon template */
	ret = iwl_mvm_mac_ctxt_beacon_changed(mvm, vif, link_conf);
	if (ret)
		goto out_unlock;

	ret = iwl_mvm_link_changed(mvm, vif, LINK_CONTEXT_MODIFY_ALL,
				   true);
	if (ret)
		goto out_unlock;

	ret = iwl_mvm_mld_add_mcast_sta(mvm, vif);
	if (ret)
		goto out_unlock;

	/* Send the bcast station. At this stage the TBTT and DTIM time
	 * events are added and applied to the scheduler
	 */
	ret = iwl_mvm_mld_add_bcast_sta(mvm, vif);
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

	iwl_mvm_ftm_restart_responder(mvm, vif);

	goto out_unlock;

out_failed:
	iwl_mvm_power_update_mac(mvm);
	mvmvif->ap_ibss_active = false;
	iwl_mvm_mld_rm_bcast_sta(mvm, vif);
out_rm_mcast:
	iwl_mvm_mld_rm_mcast_sta(mvm, vif);
out_unlock:
	mutex_unlock(&mvm->mutex);
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

	mutex_lock(&mvm->mutex);

	iwl_mvm_stop_ap_ibss_common(mvm, vif);

	/* Need to update the P2P Device MAC (only GO, IBSS is single vif) */
	if (vif->p2p && mvm->p2p_device_vif)
		iwl_mvm_mld_mac_ctxt_changed(mvm, mvm->p2p_device_vif, false);

	iwl_mvm_ftm_responder_clear(mvm, vif);

	iwl_mvm_mld_rm_bcast_sta(mvm, vif);
	iwl_mvm_mld_rm_mcast_sta(mvm, vif);

	iwl_mvm_power_update_mac(mvm);
	mutex_unlock(&mvm->mutex);
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
	struct iwl_mvm_sta_state_ops callbacks = {
		.add_sta = iwl_mvm_mld_add_sta,
		.update_sta = iwl_mvm_mld_update_sta,
		.rm_sta = iwl_mvm_mld_rm_sta,
		.mac_ctxt_changed = iwl_mvm_mld_mac_ctxt_changed,
	};

	return iwl_mvm_mac_sta_state_common(hw, vif, sta, old_state, new_state,
					    &callbacks);
}

static void
iwl_mvm_mld_bss_info_changed_station(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *bss_conf,
				     u64 changes)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;
	u32 link_changes = 0;
	bool has_he = vif->bss_conf.he_support &&
			  !iwlwifi_mod_params.disable_11ax;
	bool has_eht = vif->bss_conf.eht_support &&
			  !iwlwifi_mod_params.disable_11be;

	if (changes & BSS_CHANGED_ASSOC && vif->cfg.assoc &&
	    (has_he || has_eht)) {
		IWL_DEBUG_MAC80211(mvm, "Associated in HE mode\n");
		link_changes |= LINK_CONTEXT_MODIFY_HE_PARAMS;
	}

	/* Update MU EDCA params */
	if (changes & BSS_CHANGED_QOS && vif->cfg.assoc &&
	    (has_he || has_eht))
		link_changes |= LINK_CONTEXT_MODIFY_QOS_PARAMS;

	/* Update EHT Puncturing info */
	if (changes & BSS_CHANGED_EHT_PUNCTURING && vif->cfg.assoc && has_eht)
		link_changes |= LINK_CONTEXT_MODIFY_EHT_PARAMS;

	if (link_changes) {
		ret = iwl_mvm_link_changed(mvm, vif, link_changes, true);
		if (ret)
			IWL_ERR(mvm, "failed to update link\n");
	}

	ret = iwl_mvm_mld_mac_ctxt_changed(mvm, vif, false);
	if (ret)
		IWL_ERR(mvm, "failed to update MAC %pM\n", vif->addr);

	memcpy(mvmvif->deflink.bssid, bss_conf->bssid, ETH_ALEN);
	mvmvif->associated = vif->cfg.assoc;

	if (changes & BSS_CHANGED_ASSOC) {
		if (vif->cfg.assoc) {
			/* clear statistics to get clean beacon counter */
			iwl_mvm_request_statistics(mvm, true);
			memset(&mvmvif->deflink.beacon_stats, 0,
			       sizeof(mvmvif->deflink.beacon_stats));

			if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART,
				      &mvm->status) &&
			    !vif->bss_conf.dtim_period) {
				/* If we're not restarting and still haven't
				 * heard a beacon (dtim period unknown) then
				 * make sure we still have enough minimum time
				 * remaining in the time event, since the auth
				 * might actually have taken quite a while
				 * (especially for SAE) and so the remaining
				 * time could be small without us having heard
				 * a beacon yet.
				 */
				iwl_mvm_protect_assoc(mvm, vif, 0);
			}

			iwl_mvm_sf_update(mvm, vif, false);
			iwl_mvm_power_vif_assoc(mvm, vif);
			if (vif->p2p) {
				iwl_mvm_update_smps(mvm, vif,
						    IWL_MVM_SMPS_REQ_PROT,
						    IEEE80211_SMPS_DYNAMIC, 0);
			}
		} else if (mvmvif->deflink.ap_sta_id != IWL_MVM_INVALID_STA) {
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
			if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART,
				      &mvm->status)) {
				/* first remove remaining keys */
				iwl_mvm_sec_key_remove_ap(mvm, vif);

				/* Remove AP station now that
				 * the MAC is unassoc
				 */
				ret = iwl_mvm_mld_rm_sta_id(mvm, vif,
							    mvmvif->deflink.ap_sta_id);
				if (ret)
					IWL_ERR(mvm,
						"failed to remove AP station\n");

				mvmvif->deflink.ap_sta_id = IWL_MVM_INVALID_STA;
			}
		}

		iwl_mvm_bss_info_changed_station_assoc(mvm, vif, changes);
	}

	iwl_mvm_bss_info_changed_station_common(mvm, vif, &vif->bss_conf, changes);
}

static void
iwl_mvm_mld_bss_info_changed_ap_ibss(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *bss_conf,
				     u64 changes)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	u32 link_changes = LINK_CONTEXT_MODIFY_PROTECT_FLAGS |
			   LINK_CONTEXT_MODIFY_QOS_PARAMS;

	/* Changes will be applied when the AP/IBSS is started */
	if (!mvmvif->ap_ibss_active)
		return;

	if (changes & (BSS_CHANGED_ERP_CTS_PROT | BSS_CHANGED_HT |
		       BSS_CHANGED_BANDWIDTH | BSS_CHANGED_QOS) &&
		       iwl_mvm_link_changed(mvm, vif, link_changes, true))
		IWL_ERR(mvm, "failed to update MAC %pM\n", vif->addr);

	/* Need to send a new beacon template to the FW */
	if (changes & BSS_CHANGED_BEACON &&
	    iwl_mvm_mac_ctxt_beacon_changed(mvm, vif, &vif->bss_conf))
		IWL_WARN(mvm, "Failed updating beacon data\n");

	if (changes & BSS_CHANGED_FTM_RESPONDER) {
		int ret = iwl_mvm_ftm_start_responder(mvm, vif);

		if (ret)
			IWL_WARN(mvm, "Failed to enable FTM responder (%d)\n",
				 ret);
	}
}

static void iwl_mvm_mld_bss_info_changed(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_bss_conf *bss_conf,
					 u64 changes)
{
	struct iwl_mvm_bss_info_changed_ops callbacks = {
		.bss_info_changed_sta = iwl_mvm_mld_bss_info_changed_station,
		.bss_info_changed_ap_ibss =
			iwl_mvm_mld_bss_info_changed_ap_ibss,
	};

	iwl_mvm_bss_info_changed_common(hw, vif, bss_conf, &callbacks,
					changes);
}

static int
iwl_mvm_mld_switch_vif_chanctx(struct ieee80211_hw *hw,
			       struct ieee80211_vif_chanctx_switch *vifs,
			       int n_vifs,
			       enum ieee80211_chanctx_switch_mode mode)
{
	struct iwl_mvm_switch_vif_chanctx_ops ops = {
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

	mutex_lock(&mvm->mutex);
	iwl_mvm_mld_mac_ctxt_changed(mvm, vif, false);
	mutex_unlock(&mvm->mutex);
}

static int
iwl_mvm_mld_mac_conf_tx(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			unsigned int link_id, u16 ac,
			const struct ieee80211_tx_queue_params *params)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	mvmvif->deflink.queue_params[ac] = *params;

	/* No need to update right away, we'll get BSS_CHANGED_QOS
	 * The exception is P2P_DEVICE interface which needs immediate update.
	 */
	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		int ret;

		mutex_lock(&mvm->mutex);
		ret = iwl_mvm_link_changed(mvm, vif,
					   LINK_CONTEXT_MODIFY_QOS_PARAMS,
					   true);
		mutex_unlock(&mvm->mutex);
		return ret;
	}
	return 0;
}

static int iwl_mvm_link_switch_phy_ctx(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif,
				       struct iwl_mvm_phy_ctxt *new_phy_ctxt)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret = 0;

	lockdep_assert_held(&mvm->mutex);

	/* Inorder to change the phy_ctx of a link, the link needs to be
	 * inactive. Therefore, first deactivate the link, then change its
	 * phy_ctx, and then activate it again.
	 */
	ret = iwl_mvm_link_changed(mvm, vif, LINK_CONTEXT_MODIFY_ACTIVE, false);
	if (WARN(ret, "Failed to deactivate link\n"))
		return ret;

	iwl_mvm_phy_ctxt_unref(mvm, mvmvif->deflink.phy_ctxt);

	mvmvif->deflink.phy_ctxt = new_phy_ctxt;

	ret = iwl_mvm_link_changed(mvm, vif, 0, false);
	if (WARN(ret, "Failed to deactivate link\n"))
		return ret;

	ret = iwl_mvm_link_changed(mvm, vif, LINK_CONTEXT_MODIFY_ACTIVE, true);
	WARN(ret, "Failed binding P2P_DEVICE\n");
	return ret;
}

static int iwl_mvm_mld_roc(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_channel *channel, int duration,
			   enum ieee80211_roc_type type)
{
	struct iwl_mvm_roc_ops ops = {
		.add_aux_sta_for_hs20 = iwl_mvm_mld_add_aux_sta,
		.switch_phy_ctxt = iwl_mvm_link_switch_phy_ctx,
	};

	return iwl_mvm_roc_common(hw, vif, channel, duration, type, &ops);
}
const struct ieee80211_ops iwl_mvm_mld_hw_ops = {
	.tx = iwl_mvm_mac_tx,
	.wake_tx_queue = iwl_mvm_mac_wake_tx_queue,
	.ampdu_action = iwl_mvm_mac_ampdu_action,
	.get_antenna = iwl_mvm_op_get_antenna,
	.start = iwl_mvm_mac_start,
	.reconfig_complete = iwl_mvm_mac_reconfig_complete,
	.stop = iwl_mvm_mac_stop,
	.add_interface = iwl_mvm_mld_mac_add_interface,
	.remove_interface = iwl_mvm_mld_mac_remove_interface,
	.config = iwl_mvm_mac_config,
	.prepare_multicast = iwl_mvm_prepare_multicast,
	.configure_filter = iwl_mvm_configure_filter,
	.config_iface_filter = iwl_mvm_mld_config_iface_filter,
	.bss_info_changed = iwl_mvm_mld_bss_info_changed,
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

	.set_tim = iwl_mvm_set_tim,

	.channel_switch = iwl_mvm_channel_switch,
	.pre_channel_switch = iwl_mvm_pre_channel_switch,
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
	.sta_add_debugfs = iwl_mvm_sta_add_debugfs,
#endif
	.set_hw_timestamp = iwl_mvm_set_hw_timestamp,
};
