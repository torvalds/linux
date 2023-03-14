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

const struct ieee80211_ops iwl_mvm_mld_hw_ops = {
	.add_interface = iwl_mvm_mld_mac_add_interface,
};
