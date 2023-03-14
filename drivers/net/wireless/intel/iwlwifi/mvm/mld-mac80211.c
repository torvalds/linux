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

	/* Common for MLD and non-MLD API */
	if (iwl_mvm_mac_add_interface_common(mvm, hw, vif, &ret))
		goto out_unlock;

	ret = iwl_mvm_mld_mac_ctxt_add(mvm, vif);
	if (ret)
		goto out_unlock;

	ret = iwl_mvm_power_update_mac(mvm);
	if (ret)
		goto out_remove_mac;

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
		mvmvif->phy_ctxt = iwl_mvm_get_free_phy_ctxt(mvm);
		if (!mvmvif->phy_ctxt) {
			ret = -ENOSPC;
			goto out_free_bf;
		}

		iwl_mvm_phy_ctxt_ref(mvm, mvmvif->phy_ctxt);
		ret = iwl_mvm_add_link(mvm, vif);
		if (ret)
			goto out_unref_phy;

		ret = iwl_mvm_link_changed(mvm, vif,
					   LINK_CONTEXT_MODIFY_ACTIVE |
					   LINK_CONTEXT_MODIFY_RATES_INFO,
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
	}

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
	iwl_mvm_phy_ctxt_unref(mvm, mvmvif->phy_ctxt);
 out_free_bf:
	if (mvm->bf_allowed_vif == mvmvif) {
		mvm->bf_allowed_vif = NULL;
		vif->driver_flags &= ~(IEEE80211_VIF_BEACON_FILTER |
				       IEEE80211_VIF_SUPPORTS_CQM_RSSI);
	}
 out_remove_mac:
	mvmvif->phy_ctxt = NULL;
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

	if (iwl_mvm_mac_remove_interface_common(hw, vif))
		goto out;

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		mvm->p2p_device_vif = NULL;
		iwl_mvm_mld_rm_bcast_sta(mvm, vif);
		/* Link needs to be deactivated before removal */
		iwl_mvm_link_changed(mvm, vif, LINK_CONTEXT_MODIFY_ACTIVE,
				     false);
		iwl_mvm_remove_link(mvm, vif);
		iwl_mvm_phy_ctxt_unref(mvm, mvmvif->phy_ctxt);
		mvmvif->phy_ctxt = NULL;
	}

	iwl_mvm_mld_mac_ctxt_remove(mvm, vif);

	RCU_INIT_POINTER(mvm->vif_id_to_mac[mvmvif->id], NULL);

	if (vif->type == NL80211_IFTYPE_MONITOR) {
		mvm->monitor_on = false;
		__clear_bit(IEEE80211_HW_RX_INCLUDES_FCS, mvm->hw->flags);
	}

out:
	mutex_unlock(&mvm->mutex);
}

static int __iwl_mvm_mld_assign_vif_chanctx(struct iwl_mvm *mvm,
					    struct ieee80211_vif *vif,
					    struct ieee80211_chanctx_conf *ctx,
					    bool switching_chanctx)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	if (__iwl_mvm_assign_vif_chanctx_common(mvm, vif, ctx,
						switching_chanctx, &ret))
		goto out;

	ret = iwl_mvm_add_link(mvm, vif);
	if (ret)
		goto out;
	ret = iwl_mvm_link_changed(mvm, vif, LINK_CONTEXT_MODIFY_ACTIVE,
				   true);
	if (ret)
		goto out_remove_link;

	/*
	 * Power state must be updated before quotas,
	 * otherwise fw will complain.
	 */
	iwl_mvm_power_update_mac(mvm);

	if (vif->type == NL80211_IFTYPE_MONITOR) {
		ret = iwl_mvm_mld_add_snif_sta(mvm, vif);
		if (ret)
			goto out_remove_link;
	}

	goto out;

out_remove_link:
	/* Link needs to be deactivated before removal */
	iwl_mvm_link_changed(mvm, vif, LINK_CONTEXT_MODIFY_ACTIVE, false);
	iwl_mvm_remove_link(mvm, vif);
	iwl_mvm_power_update_mac(mvm);
out:
	if (ret)
		mvmvif->phy_ctxt = NULL;
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

	if (__iwl_mvm_unassign_vif_chanctx_common(mvm, vif, switching_chanctx))
		goto out;

	if (vif->type == NL80211_IFTYPE_MONITOR)
		iwl_mvm_mld_rm_snif_sta(mvm, vif);

	if (vif->type == NL80211_IFTYPE_AP)
		/* Set CS bit on all the stations */
		iwl_mvm_mld_modify_all_sta_disable_tx(mvm, mvmvif, true);

	/* Link needs to be deactivated before removal */
	iwl_mvm_link_changed(mvm, vif, LINK_CONTEXT_MODIFY_ACTIVE, false);
	iwl_mvm_remove_link(mvm, vif);

out:
	if (switching_chanctx)
		return;
	mvmvif->phy_ctxt = NULL;
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
const struct ieee80211_ops iwl_mvm_mld_hw_ops = {
	.add_interface = iwl_mvm_mld_mac_add_interface,
	.remove_interface = iwl_mvm_mld_mac_remove_interface,
	.assign_vif_chanctx = iwl_mvm_mld_assign_vif_chanctx,
	.unassign_vif_chanctx = iwl_mvm_mld_unassign_vif_chanctx,
};
