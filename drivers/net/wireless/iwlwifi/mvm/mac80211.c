/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <net/mac80211.h>
#include <net/tcp.h>

#include "iwl-op-mode.h"
#include "iwl-io.h"
#include "mvm.h"
#include "sta.h"
#include "time-event.h"
#include "iwl-eeprom-parse.h"
#include "fw-api-scan.h"
#include "iwl-phy-db.h"

static const struct ieee80211_iface_limit iwl_mvm_limits[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_STATION) |
			BIT(NL80211_IFTYPE_AP),
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_P2P_CLIENT) |
			BIT(NL80211_IFTYPE_P2P_GO),
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_P2P_DEVICE),
	},
};

static const struct ieee80211_iface_combination iwl_mvm_iface_combinations[] = {
	{
		.num_different_channels = 1,
		.max_interfaces = 3,
		.limits = iwl_mvm_limits,
		.n_limits = ARRAY_SIZE(iwl_mvm_limits),
	},
};

#ifdef CONFIG_PM_SLEEP
static const struct nl80211_wowlan_tcp_data_token_feature
iwl_mvm_wowlan_tcp_token_feature = {
	.min_len = 0,
	.max_len = 255,
	.bufsize = IWL_WOWLAN_REMOTE_WAKE_MAX_TOKENS,
};

static const struct wiphy_wowlan_tcp_support iwl_mvm_wowlan_tcp_support = {
	.tok = &iwl_mvm_wowlan_tcp_token_feature,
	.data_payload_max = IWL_WOWLAN_TCP_MAX_PACKET_LEN -
			    sizeof(struct ethhdr) -
			    sizeof(struct iphdr) -
			    sizeof(struct tcphdr),
	.data_interval_max = 65535, /* __le16 in API */
	.wake_payload_max = IWL_WOWLAN_REMOTE_WAKE_MAX_PACKET_LEN -
			    sizeof(struct ethhdr) -
			    sizeof(struct iphdr) -
			    sizeof(struct tcphdr),
	.seq = true,
};
#endif

int iwl_mvm_mac_setup_register(struct iwl_mvm *mvm)
{
	struct ieee80211_hw *hw = mvm->hw;
	int num_mac, ret, i;

	/* Tell mac80211 our characteristics */
	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_SPECTRUM_MGMT |
		    IEEE80211_HW_REPORTS_TX_ACK_STATUS |
		    IEEE80211_HW_QUEUE_CONTROL |
		    IEEE80211_HW_WANT_MONITOR_VIF |
		    IEEE80211_HW_SUPPORTS_PS |
		    IEEE80211_HW_SUPPORTS_DYNAMIC_PS |
		    IEEE80211_HW_AMPDU_AGGREGATION |
		    IEEE80211_HW_TIMING_BEACON_ONLY;

	hw->queues = IWL_MVM_FIRST_AGG_QUEUE;
	hw->offchannel_tx_hw_queue = IWL_MVM_OFFCHANNEL_QUEUE;
	hw->rate_control_algorithm = "iwl-mvm-rs";

	/*
	 * Enable 11w if advertised by firmware and software crypto
	 * is not enabled (as the firmware will interpret some mgmt
	 * packets, so enabling it with software crypto isn't safe)
	 */
	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_MFP &&
	    !iwlwifi_mod_params.sw_crypto)
		hw->flags |= IEEE80211_HW_MFP_CAPABLE;

	hw->sta_data_size = sizeof(struct iwl_mvm_sta);
	hw->vif_data_size = sizeof(struct iwl_mvm_vif);
	hw->chanctx_data_size = sizeof(struct iwl_mvm_phy_ctxt);

	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_P2P_CLIENT) |
		BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_P2P_GO) |
		BIT(NL80211_IFTYPE_P2P_DEVICE);

	hw->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY |
			    WIPHY_FLAG_DISABLE_BEACON_HINTS |
			    WIPHY_FLAG_IBSS_RSN;

	hw->wiphy->iface_combinations = iwl_mvm_iface_combinations;
	hw->wiphy->n_iface_combinations =
		ARRAY_SIZE(iwl_mvm_iface_combinations);

	hw->wiphy->max_remain_on_channel_duration = 10000;
	hw->max_listen_interval = IWL_CONN_MAX_LISTEN_INTERVAL;

	/* Extract MAC address */
	memcpy(mvm->addresses[0].addr, mvm->nvm_data->hw_addr, ETH_ALEN);
	hw->wiphy->addresses = mvm->addresses;
	hw->wiphy->n_addresses = 1;

	/* Extract additional MAC addresses if available */
	num_mac = (mvm->nvm_data->n_hw_addrs > 1) ?
		min(IWL_MVM_MAX_ADDRESSES, mvm->nvm_data->n_hw_addrs) : 1;

	for (i = 1; i < num_mac; i++) {
		memcpy(mvm->addresses[i].addr, mvm->addresses[i-1].addr,
		       ETH_ALEN);
		mvm->addresses[i].addr[5]++;
		hw->wiphy->n_addresses++;
	}

	/* we create the 802.11 header and a max-length SSID element */
	hw->wiphy->max_scan_ie_len =
		mvm->fw->ucode_capa.max_probe_length - 24 - 34;
	hw->wiphy->max_scan_ssids = PROBE_OPTION_MAX;

	if (mvm->nvm_data->bands[IEEE80211_BAND_2GHZ].n_channels)
		hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
			&mvm->nvm_data->bands[IEEE80211_BAND_2GHZ];
	if (mvm->nvm_data->bands[IEEE80211_BAND_5GHZ].n_channels)
		hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&mvm->nvm_data->bands[IEEE80211_BAND_5GHZ];

	hw->wiphy->hw_version = mvm->trans->hw_id;

	if (iwlmvm_mod_params.power_scheme != IWL_POWER_SCHEME_CAM)
		hw->wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
	else
		hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	hw->wiphy->features |= NL80211_FEATURE_P2P_GO_CTWIN |
			       NL80211_FEATURE_P2P_GO_OPPPS;

	mvm->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;

#ifdef CONFIG_PM_SLEEP
	if (mvm->fw->img[IWL_UCODE_WOWLAN].sec[0].len &&
	    mvm->trans->ops->d3_suspend &&
	    mvm->trans->ops->d3_resume &&
	    device_can_wakeup(mvm->trans->dev)) {
		hw->wiphy->wowlan.flags = WIPHY_WOWLAN_MAGIC_PKT |
					  WIPHY_WOWLAN_DISCONNECT |
					  WIPHY_WOWLAN_EAP_IDENTITY_REQ |
					  WIPHY_WOWLAN_RFKILL_RELEASE;
		if (!iwlwifi_mod_params.sw_crypto)
			hw->wiphy->wowlan.flags |=
				WIPHY_WOWLAN_SUPPORTS_GTK_REKEY |
				WIPHY_WOWLAN_GTK_REKEY_FAILURE |
				WIPHY_WOWLAN_4WAY_HANDSHAKE;

		hw->wiphy->wowlan.n_patterns = IWL_WOWLAN_MAX_PATTERNS;
		hw->wiphy->wowlan.pattern_min_len = IWL_WOWLAN_MIN_PATTERN_LEN;
		hw->wiphy->wowlan.pattern_max_len = IWL_WOWLAN_MAX_PATTERN_LEN;
		hw->wiphy->wowlan.tcp = &iwl_mvm_wowlan_tcp_support;
	}
#endif

	ret = iwl_mvm_leds_init(mvm);
	if (ret)
		return ret;

	ret = ieee80211_register_hw(mvm->hw);
	if (ret)
		iwl_mvm_leds_exit(mvm);

	return ret;
}

static void iwl_mvm_mac_tx(struct ieee80211_hw *hw,
			   struct ieee80211_tx_control *control,
			   struct sk_buff *skb)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	if (test_bit(IWL_MVM_STATUS_HW_RFKILL, &mvm->status)) {
		IWL_DEBUG_DROP(mvm, "Dropping - RF KILL\n");
		goto drop;
	}

	if (IEEE80211_SKB_CB(skb)->hw_queue == IWL_MVM_OFFCHANNEL_QUEUE &&
	    !test_bit(IWL_MVM_STATUS_ROC_RUNNING, &mvm->status))
		goto drop;

	if (control->sta) {
		if (iwl_mvm_tx_skb(mvm, skb, control->sta))
			goto drop;
		return;
	}

	if (iwl_mvm_tx_skb_non_sta(mvm, skb))
		goto drop;
	return;
 drop:
	ieee80211_free_txskb(hw, skb);
}

static inline bool iwl_enable_rx_ampdu(const struct iwl_cfg *cfg)
{
	if (iwlwifi_mod_params.disable_11n & IWL_DISABLE_HT_RXAGG)
		return false;
	return true;
}

static inline bool iwl_enable_tx_ampdu(const struct iwl_cfg *cfg)
{
	if (iwlwifi_mod_params.disable_11n & IWL_DISABLE_HT_TXAGG)
		return false;
	if (iwlwifi_mod_params.disable_11n & IWL_ENABLE_HT_TXAGG)
		return true;

	/* enabled by default */
	return true;
}

static int iwl_mvm_mac_ampdu_action(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    enum ieee80211_ampdu_mlme_action action,
				    struct ieee80211_sta *sta, u16 tid,
				    u16 *ssn, u8 buf_size)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	IWL_DEBUG_HT(mvm, "A-MPDU action on addr %pM tid %d: action %d\n",
		     sta->addr, tid, action);

	if (!(mvm->nvm_data->sku_cap_11n_enable))
		return -EACCES;

	mutex_lock(&mvm->mutex);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		if (!iwl_enable_rx_ampdu(mvm->cfg)) {
			ret = -EINVAL;
			break;
		}
		ret = iwl_mvm_sta_rx_agg(mvm, sta, tid, *ssn, true);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		ret = iwl_mvm_sta_rx_agg(mvm, sta, tid, 0, false);
		break;
	case IEEE80211_AMPDU_TX_START:
		if (!iwl_enable_tx_ampdu(mvm->cfg)) {
			ret = -EINVAL;
			break;
		}
		ret = iwl_mvm_sta_tx_agg_start(mvm, vif, sta, tid, ssn);
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
		ret = iwl_mvm_sta_tx_agg_stop(mvm, vif, sta, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		ret = iwl_mvm_sta_tx_agg_flush(mvm, vif, sta, tid);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		ret = iwl_mvm_sta_tx_agg_oper(mvm, vif, sta, tid, buf_size);
		break;
	default:
		WARN_ON_ONCE(1);
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void iwl_mvm_cleanup_iterator(void *data, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	mvmvif->uploaded = false;
	mvmvif->ap_sta_id = IWL_MVM_STATION_COUNT;

	/* does this make sense at all? */
	mvmvif->color++;

	spin_lock_bh(&mvm->time_event_lock);
	iwl_mvm_te_clear_data(mvm, &mvmvif->time_event_data);
	spin_unlock_bh(&mvm->time_event_lock);

	if (vif->type != NL80211_IFTYPE_P2P_DEVICE)
		mvmvif->phy_ctxt = NULL;
}

static void iwl_mvm_restart_cleanup(struct iwl_mvm *mvm)
{
	iwl_trans_stop_device(mvm->trans);
	iwl_trans_stop_hw(mvm->trans, false);

	mvm->scan_status = IWL_MVM_SCAN_NONE;

	/* just in case one was running */
	ieee80211_remain_on_channel_expired(mvm->hw);

	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
		iwl_mvm_cleanup_iterator, mvm);

	memset(mvm->fw_key_table, 0, sizeof(mvm->fw_key_table));
	memset(mvm->sta_drained, 0, sizeof(mvm->sta_drained));

	ieee80211_wake_queues(mvm->hw);

	mvm->vif_count = 0;
}

static int iwl_mvm_mac_start(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);

	/* Clean up some internal and mac80211 state on restart */
	if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
		iwl_mvm_restart_cleanup(mvm);

	ret = iwl_mvm_up(mvm);
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void iwl_mvm_mac_restart_complete(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);

	clear_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status);
	ret = iwl_mvm_update_quotas(mvm, NULL);
	if (ret)
		IWL_ERR(mvm, "Failed to update quotas after restart (%d)\n",
			ret);

	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_mac_stop(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	flush_work(&mvm->async_handlers_wk);

	mutex_lock(&mvm->mutex);
	/* async_handlers_wk is now blocked */

	/*
	 * The work item could be running or queued if the
	 * ROC time event stops just as we get here.
	 */
	cancel_work_sync(&mvm->roc_done_wk);

	iwl_trans_stop_device(mvm->trans);
	iwl_trans_stop_hw(mvm->trans, false);

	iwl_mvm_async_handlers_purge(mvm);
	/* async_handlers_list is empty and will stay empty: HW is stopped */

	/* the fw is stopped, the aux sta is dead: clean up driver state */
	iwl_mvm_dealloc_int_sta(mvm, &mvm->aux_sta);

	mutex_unlock(&mvm->mutex);

	/*
	 * The worker might have been waiting for the mutex, let it run and
	 * discover that its list is now empty.
	 */
	cancel_work_sync(&mvm->async_handlers_wk);
}

static void iwl_mvm_pm_disable_iterator(void *data, u8 *mac,
					struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = data;
	int ret;

	ret = iwl_mvm_power_disable(mvm, vif);
	if (ret)
		IWL_ERR(mvm, "failed to disable power management\n");
}

static void iwl_mvm_power_update_iterator(void *data, u8 *mac,
					  struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = data;

	iwl_mvm_power_update_mode(mvm, vif);
}

static int iwl_mvm_mac_add_interface(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	/*
	 * Not much to do here. The stack will not allow interface
	 * types or combinations that we didn't advertise, so we
	 * don't really have to check the types.
	 */

	mutex_lock(&mvm->mutex);

	/* Allocate resources for the MAC context, and add it the the fw  */
	ret = iwl_mvm_mac_ctxt_init(mvm, vif);
	if (ret)
		goto out_unlock;

	/*
	 * The AP binding flow can be done only after the beacon
	 * template is configured (which happens only in the mac80211
	 * start_ap() flow), and adding the broadcast station can happen
	 * only after the binding.
	 * In addition, since modifying the MAC before adding a bcast
	 * station is not allowed by the FW, delay the adding of MAC context to
	 * the point where we can also add the bcast station.
	 * In short: there's not much we can do at this point, other than
	 * allocating resources :)
	 */
	if (vif->type == NL80211_IFTYPE_AP) {
		u32 qmask = iwl_mvm_mac_get_queues_mask(mvm, vif);
		ret = iwl_mvm_allocate_int_sta(mvm, &mvmvif->bcast_sta,
					       qmask);
		if (ret) {
			IWL_ERR(mvm, "Failed to allocate bcast sta\n");
			goto out_release;
		}

		goto out_unlock;
	}

	/*
	 * TODO: remove this temporary code.
	 * Currently MVM FW supports power management only on single MAC.
	 * If new interface added, disable PM on existing interface.
	 * P2P device is a special case, since it is handled by FW similary to
	 * scan. If P2P deviced is added, PM remains enabled on existing
	 * interface.
	 * Note: the method below does not count the new interface being added
	 * at this moment.
	 */
	if (vif->type != NL80211_IFTYPE_P2P_DEVICE)
		mvm->vif_count++;
	if (mvm->vif_count > 1) {
		IWL_DEBUG_MAC80211(mvm,
				   "Disable power on existing interfaces\n");
		ieee80211_iterate_active_interfaces_atomic(
					    mvm->hw,
					    IEEE80211_IFACE_ITER_NORMAL,
					    iwl_mvm_pm_disable_iterator, mvm);
	}

	ret = iwl_mvm_mac_ctxt_add(mvm, vif);
	if (ret)
		goto out_release;

	/*
	 * Update power state on the new interface. Admittedly, based on
	 * mac80211 logics this power update will disable power management
	 */
	iwl_mvm_power_update_mode(mvm, vif);

	/*
	 * P2P_DEVICE interface does not have a channel context assigned to it,
	 * so a dedicated PHY context is allocated to it and the corresponding
	 * MAC context is bound to it at this stage.
	 */
	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		struct ieee80211_channel *chan;
		struct cfg80211_chan_def chandef;

		mvmvif->phy_ctxt = &mvm->phy_ctxt_roc;

		/*
		 * The channel used here isn't relevant as it's
		 * going to be overwritten as part of the ROC flow.
		 * For now use the first channel we have.
		 */
		chan = &mvm->hw->wiphy->bands[IEEE80211_BAND_2GHZ]->channels[0];
		cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);
		ret = iwl_mvm_phy_ctxt_add(mvm, mvmvif->phy_ctxt,
					   &chandef, 1, 1);
		if (ret)
			goto out_remove_mac;

		ret = iwl_mvm_binding_add_vif(mvm, vif);
		if (ret)
			goto out_remove_phy;

		ret = iwl_mvm_add_bcast_sta(mvm, vif, &mvmvif->bcast_sta);
		if (ret)
			goto out_unbind;

		/* Save a pointer to p2p device vif, so it can later be used to
		 * update the p2p device MAC when a GO is started/stopped */
		mvm->p2p_device_vif = vif;
	}

	iwl_mvm_vif_dbgfs_register(mvm, vif);
	goto out_unlock;

 out_unbind:
	iwl_mvm_binding_remove_vif(mvm, vif);
 out_remove_phy:
	iwl_mvm_phy_ctxt_remove(mvm, mvmvif->phy_ctxt);
 out_remove_mac:
	mvmvif->phy_ctxt = NULL;
	iwl_mvm_mac_ctxt_remove(mvm, vif);
 out_release:
	/*
	 * TODO: remove this temporary code.
	 * Currently MVM FW supports power management only on single MAC.
	 * Check if only one additional interface remains after releasing
	 * current one. Update power mode on the remaining interface.
	 */
	if (vif->type != NL80211_IFTYPE_P2P_DEVICE)
		mvm->vif_count--;
	IWL_DEBUG_MAC80211(mvm, "Currently %d interfaces active\n",
			   mvm->vif_count);
	if (mvm->vif_count == 1) {
		ieee80211_iterate_active_interfaces(
					mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_power_update_iterator, mvm);
	}
	iwl_mvm_mac_ctxt_release(mvm, vif);
 out_unlock:
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void iwl_mvm_prepare_mac_removal(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif)
{
	u32 tfd_msk = 0, ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		if (vif->hw_queue[ac] != IEEE80211_INVAL_HW_QUEUE)
			tfd_msk |= BIT(vif->hw_queue[ac]);

	if (vif->cab_queue != IEEE80211_INVAL_HW_QUEUE)
		tfd_msk |= BIT(vif->cab_queue);

	if (tfd_msk) {
		mutex_lock(&mvm->mutex);
		iwl_mvm_flush_tx_path(mvm, tfd_msk, true);
		mutex_unlock(&mvm->mutex);
	}

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		/*
		 * Flush the ROC worker which will flush the OFFCHANNEL queue.
		 * We assume here that all the packets sent to the OFFCHANNEL
		 * queue are sent in ROC session.
		 */
		flush_work(&mvm->roc_done_wk);
	} else {
		/*
		 * By now, all the AC queues are empty. The AGG queues are
		 * empty too. We already got all the Tx responses for all the
		 * packets in the queues. The drain work can have been
		 * triggered. Flush it. This work item takes the mutex, so kill
		 * it before we take it.
		 */
		flush_work(&mvm->sta_drained_wk);
	}
}

static void iwl_mvm_mac_remove_interface(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	iwl_mvm_prepare_mac_removal(mvm, vif);

	mutex_lock(&mvm->mutex);

	iwl_mvm_vif_dbgfs_clean(mvm, vif);

	/*
	 * For AP/GO interface, the tear down of the resources allocated to the
	 * interface is be handled as part of the stop_ap flow.
	 */
	if (vif->type == NL80211_IFTYPE_AP) {
		iwl_mvm_dealloc_int_sta(mvm, &mvmvif->bcast_sta);
		goto out_release;
	}

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		mvm->p2p_device_vif = NULL;
		iwl_mvm_rm_bcast_sta(mvm, &mvmvif->bcast_sta);
		iwl_mvm_binding_remove_vif(mvm, vif);
		iwl_mvm_phy_ctxt_remove(mvm, mvmvif->phy_ctxt);
		mvmvif->phy_ctxt = NULL;
	}

	/*
	 * TODO: remove this temporary code.
	 * Currently MVM FW supports power management only on single MAC.
	 * Check if only one additional interface remains after removing
	 * current one. Update power mode on the remaining interface.
	 */
	if (mvm->vif_count && vif->type != NL80211_IFTYPE_P2P_DEVICE)
		mvm->vif_count--;
	IWL_DEBUG_MAC80211(mvm, "Currently %d interfaces active\n",
			   mvm->vif_count);
	if (mvm->vif_count == 1) {
		ieee80211_iterate_active_interfaces(
					mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_power_update_iterator, mvm);
	}

	iwl_mvm_mac_ctxt_remove(mvm, vif);

out_release:
	iwl_mvm_mac_ctxt_release(mvm, vif);
	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_mac_config(struct ieee80211_hw *hw, u32 changed)
{
	return 0;
}

static void iwl_mvm_configure_filter(struct ieee80211_hw *hw,
				     unsigned int changed_flags,
				     unsigned int *total_flags,
				     u64 multicast)
{
	*total_flags = 0;
}

static int iwl_mvm_configure_mcast_filter(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif)
{
	struct iwl_mcast_filter_cmd mcast_filter_cmd = {
		.pass_all = 1,
	};

	memcpy(mcast_filter_cmd.bssid, vif->bss_conf.bssid, ETH_ALEN);

	return iwl_mvm_send_cmd_pdu(mvm, MCAST_FILTER_CMD, CMD_SYNC,
				    sizeof(mcast_filter_cmd),
				    &mcast_filter_cmd);
}

static void iwl_mvm_bss_info_changed_station(struct iwl_mvm *mvm,
					     struct ieee80211_vif *vif,
					     struct ieee80211_bss_conf *bss_conf,
					     u32 changes)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	ret = iwl_mvm_mac_ctxt_changed(mvm, vif);
	if (ret)
		IWL_ERR(mvm, "failed to update MAC %pM\n", vif->addr);

	if (changes & BSS_CHANGED_ASSOC) {
		if (bss_conf->assoc) {
			/* add quota for this interface */
			ret = iwl_mvm_update_quotas(mvm, vif);
			if (ret) {
				IWL_ERR(mvm, "failed to update quotas\n");
				return;
			}
			iwl_mvm_bt_coex_vif_assoc(mvm, vif);
			iwl_mvm_configure_mcast_filter(mvm, vif);
		} else if (mvmvif->ap_sta_id != IWL_MVM_STATION_COUNT) {
			/* remove AP station now that the MAC is unassoc */
			ret = iwl_mvm_rm_sta_id(mvm, vif, mvmvif->ap_sta_id);
			if (ret)
				IWL_ERR(mvm, "failed to remove AP station\n");
			mvmvif->ap_sta_id = IWL_MVM_STATION_COUNT;
			/* remove quota for this interface */
			ret = iwl_mvm_update_quotas(mvm, NULL);
			if (ret)
				IWL_ERR(mvm, "failed to update quotas\n");
		}
	} else if (changes & BSS_CHANGED_DTIM_PERIOD) {
		/*
		 * We received a beacon _after_ association so
		 * remove the session protection.
		 */
		iwl_mvm_remove_time_event(mvm, mvmvif,
					  &mvmvif->time_event_data);
	} else if (changes & BSS_CHANGED_PS) {
		/*
		 * TODO: remove this temporary code.
		 * Currently MVM FW supports power management only on single
		 * MAC. Avoid power mode update if more than one interface
		 * is active.
		 */
		IWL_DEBUG_MAC80211(mvm, "Currently %d interfaces active\n",
				   mvm->vif_count);
		if (mvm->vif_count == 1) {
			ret = iwl_mvm_power_update_mode(mvm, vif);
			if (ret)
				IWL_ERR(mvm, "failed to update power mode\n");
		}
	}
}

static int iwl_mvm_start_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	mutex_lock(&mvm->mutex);

	/* Send the beacon template */
	ret = iwl_mvm_mac_ctxt_beacon_changed(mvm, vif);
	if (ret)
		goto out_unlock;

	/* Add the mac context */
	ret = iwl_mvm_mac_ctxt_add(mvm, vif);
	if (ret)
		goto out_unlock;

	/* Perform the binding */
	ret = iwl_mvm_binding_add_vif(mvm, vif);
	if (ret)
		goto out_remove;

	mvmvif->ap_active = true;

	/* Send the bcast station. At this stage the TBTT and DTIM time events
	 * are added and applied to the scheduler */
	ret = iwl_mvm_send_bcast_sta(mvm, vif, &mvmvif->bcast_sta);
	if (ret)
		goto out_unbind;

	ret = iwl_mvm_update_quotas(mvm, vif);
	if (ret)
		goto out_rm_bcast;

	/* Need to update the P2P Device MAC */
	if (vif->p2p && mvm->p2p_device_vif)
		iwl_mvm_mac_ctxt_changed(mvm, mvm->p2p_device_vif);

	mutex_unlock(&mvm->mutex);
	return 0;

out_rm_bcast:
	iwl_mvm_send_rm_bcast_sta(mvm, &mvmvif->bcast_sta);
out_unbind:
	iwl_mvm_binding_remove_vif(mvm, vif);
out_remove:
	iwl_mvm_mac_ctxt_remove(mvm, vif);
out_unlock:
	mutex_unlock(&mvm->mutex);
	return ret;
}

static void iwl_mvm_stop_ap(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	iwl_mvm_prepare_mac_removal(mvm, vif);

	mutex_lock(&mvm->mutex);

	mvmvif->ap_active = false;

	/* Need to update the P2P Device MAC */
	if (vif->p2p && mvm->p2p_device_vif)
		iwl_mvm_mac_ctxt_changed(mvm, mvm->p2p_device_vif);

	iwl_mvm_update_quotas(mvm, NULL);
	iwl_mvm_send_rm_bcast_sta(mvm, &mvmvif->bcast_sta);
	iwl_mvm_binding_remove_vif(mvm, vif);
	iwl_mvm_mac_ctxt_remove(mvm, vif);

	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_bss_info_changed_ap(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					struct ieee80211_bss_conf *bss_conf,
					u32 changes)
{
	/* Need to send a new beacon template to the FW */
	if (changes & BSS_CHANGED_BEACON) {
		if (iwl_mvm_mac_ctxt_beacon_changed(mvm, vif))
			IWL_WARN(mvm, "Failed updating beacon data\n");
	}
}

static void iwl_mvm_bss_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *bss_conf,
				     u32 changes)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		iwl_mvm_bss_info_changed_station(mvm, vif, bss_conf, changes);
		break;
	case NL80211_IFTYPE_AP:
		iwl_mvm_bss_info_changed_ap(mvm, vif, bss_conf, changes);
		break;
	default:
		/* shouldn't happen */
		WARN_ON_ONCE(1);
	}

	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_mac_hw_scan(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct cfg80211_scan_request *req)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	if (req->n_channels == 0 || req->n_channels > MAX_NUM_SCAN_CHANNELS)
		return -EINVAL;

	mutex_lock(&mvm->mutex);

	if (mvm->scan_status == IWL_MVM_SCAN_NONE)
		ret = iwl_mvm_scan_request(mvm, vif, req);
	else
		ret = -EBUSY;

	mutex_unlock(&mvm->mutex);

	return ret;
}

static void iwl_mvm_mac_cancel_hw_scan(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);

	iwl_mvm_cancel_scan(mvm);

	mutex_unlock(&mvm->mutex);
}

static void
iwl_mvm_mac_allow_buffered_frames(struct ieee80211_hw *hw,
				  struct ieee80211_sta *sta, u16 tid,
				  int num_frames,
				  enum ieee80211_frame_release_type reason,
				  bool more_data)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	/* TODO: how do we tell the fw to send frames for a specific TID */

	/*
	 * The fw will send EOSP notification when the last frame will be
	 * transmitted.
	 */
	iwl_mvm_sta_modify_sleep_tx_count(mvm, sta, reason, num_frames);
}

static void iwl_mvm_mac_sta_notify(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   enum sta_notify_cmd cmd,
				   struct ieee80211_sta *sta)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_sta *mvmsta = (void *)sta->drv_priv;

	switch (cmd) {
	case STA_NOTIFY_SLEEP:
		if (atomic_read(&mvm->pending_frames[mvmsta->sta_id]) > 0)
			ieee80211_sta_block_awake(hw, sta, true);
		/*
		 * The fw updates the STA to be asleep. Tx packets on the Tx
		 * queues to this station will not be transmitted. The fw will
		 * send a Tx response with TX_STATUS_FAIL_DEST_PS.
		 */
		break;
	case STA_NOTIFY_AWAKE:
		if (WARN_ON(mvmsta->sta_id == IWL_MVM_STATION_COUNT))
			break;
		iwl_mvm_sta_modify_ps_wake(mvm, sta);
		break;
	default:
		break;
	}
}

static int iwl_mvm_mac_sta_state(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 enum ieee80211_sta_state old_state,
				 enum ieee80211_sta_state new_state)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	IWL_DEBUG_MAC80211(mvm, "station %pM state change %d->%d\n",
			   sta->addr, old_state, new_state);

	/* this would be a mac80211 bug ... but don't crash */
	if (WARN_ON_ONCE(!mvmvif->phy_ctxt))
		return -EINVAL;

	/* if a STA is being removed, reuse its ID */
	flush_work(&mvm->sta_drained_wk);

	mutex_lock(&mvm->mutex);
	if (old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE) {
		/*
		 * Firmware bug - it'll crash if the beacon interval is less
		 * than 16. We can't avoid connecting at all, so refuse the
		 * station state change, this will cause mac80211 to abandon
		 * attempts to connect to this AP, and eventually wpa_s will
		 * blacklist the AP...
		 */
		if (vif->type == NL80211_IFTYPE_STATION &&
		    vif->bss_conf.beacon_int < 16) {
			IWL_ERR(mvm,
				"AP %pM beacon interval is %d, refusing due to firmware bug!\n",
				sta->addr, vif->bss_conf.beacon_int);
			ret = -EINVAL;
			goto out_unlock;
		}
		ret = iwl_mvm_add_sta(mvm, vif, sta);
	} else if (old_state == IEEE80211_STA_NONE &&
		   new_state == IEEE80211_STA_AUTH) {
		ret = 0;
	} else if (old_state == IEEE80211_STA_AUTH &&
		   new_state == IEEE80211_STA_ASSOC) {
		ret = iwl_mvm_update_sta(mvm, vif, sta);
		if (ret == 0)
			iwl_mvm_rs_rate_init(mvm, sta,
					     mvmvif->phy_ctxt->channel->band);
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTHORIZED) {
		ret = 0;
	} else if (old_state == IEEE80211_STA_AUTHORIZED &&
		   new_state == IEEE80211_STA_ASSOC) {
		ret = 0;
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTH) {
		ret = 0;
	} else if (old_state == IEEE80211_STA_AUTH &&
		   new_state == IEEE80211_STA_NONE) {
		ret = 0;
	} else if (old_state == IEEE80211_STA_NONE &&
		   new_state == IEEE80211_STA_NOTEXIST) {
		ret = iwl_mvm_rm_sta(mvm, vif, sta);
	} else {
		ret = -EIO;
	}
 out_unlock:
	mutex_unlock(&mvm->mutex);

	return ret;
}

static int iwl_mvm_mac_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mvm->rts_threshold = value;

	return 0;
}

static int iwl_mvm_mac_conf_tx(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif, u16 ac,
			       const struct ieee80211_tx_queue_params *params)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	mvmvif->queue_params[ac] = *params;

	/*
	 * No need to update right away, we'll get BSS_CHANGED_QOS
	 * The exception is P2P_DEVICE interface which needs immediate update.
	 */
	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		int ret;

		mutex_lock(&mvm->mutex);
		ret = iwl_mvm_mac_ctxt_changed(mvm, vif);
		mutex_unlock(&mvm->mutex);
		return ret;
	}
	return 0;
}

static void iwl_mvm_mac_mgd_prepare_tx(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	u32 duration = min(IWL_MVM_TE_SESSION_PROTECTION_MAX_TIME_MS,
			   200 + vif->bss_conf.beacon_int);
	u32 min_duration = min(IWL_MVM_TE_SESSION_PROTECTION_MIN_TIME_MS,
			       100 + vif->bss_conf.beacon_int);

	if (WARN_ON_ONCE(vif->bss_conf.assoc))
		return;

	mutex_lock(&mvm->mutex);
	/* Try really hard to protect the session and hear a beacon */
	iwl_mvm_protect_session(mvm, vif, duration, min_duration);
	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_mac_set_key(struct ieee80211_hw *hw,
			       enum set_key_cmd cmd,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       struct ieee80211_key_conf *key)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	if (iwlwifi_mod_params.sw_crypto) {
		IWL_DEBUG_MAC80211(mvm, "leave - hwcrypto disabled\n");
		return -EOPNOTSUPP;
	}

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		/* fall-through */
	case WLAN_CIPHER_SUITE_CCMP:
		key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		WARN_ON_ONCE(!(hw->flags & IEEE80211_HW_MFP_CAPABLE));
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		/*
		 * Support for TX only, at least for now, so accept
		 * the key and do nothing else. Then mac80211 will
		 * pass it for TX but we don't have to use it for RX.
		 */
		return 0;
	default:
		return -EOPNOTSUPP;
	}

	mutex_lock(&mvm->mutex);

	switch (cmd) {
	case SET_KEY:
		if (vif->type == NL80211_IFTYPE_AP && !sta) {
			/* GTK on AP interface is a TX-only key, return 0 */
			ret = 0;
			key->hw_key_idx = STA_KEY_IDX_INVALID;
			break;
		}

		IWL_DEBUG_MAC80211(mvm, "set hwcrypto key\n");
		ret = iwl_mvm_set_sta_key(mvm, vif, sta, key, false);
		if (ret) {
			IWL_WARN(mvm, "set key failed\n");
			/*
			 * can't add key for RX, but we don't need it
			 * in the device for TX so still return 0
			 */
			key->hw_key_idx = STA_KEY_IDX_INVALID;
			ret = 0;
		}

		break;
	case DISABLE_KEY:
		if (key->hw_key_idx == STA_KEY_IDX_INVALID) {
			ret = 0;
			break;
		}

		IWL_DEBUG_MAC80211(mvm, "disable hwcrypto key\n");
		ret = iwl_mvm_remove_sta_key(mvm, vif, sta, key);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&mvm->mutex);
	return ret;
}

static void iwl_mvm_mac_update_tkip_key(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_key_conf *keyconf,
					struct ieee80211_sta *sta,
					u32 iv32, u16 *phase1key)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	iwl_mvm_update_tkip_key(mvm, vif, keyconf, sta, iv32, phase1key);
}


static int iwl_mvm_roc(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif,
		       struct ieee80211_channel *channel,
		       int duration,
		       enum ieee80211_roc_type type)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct cfg80211_chan_def chandef;
	int ret;

	if (vif->type != NL80211_IFTYPE_P2P_DEVICE) {
		IWL_ERR(mvm, "vif isn't a P2P_DEVICE: %d\n", vif->type);
		return -EINVAL;
	}

	IWL_DEBUG_MAC80211(mvm, "enter (%d, %d, %d)\n", channel->hw_value,
			   duration, type);

	mutex_lock(&mvm->mutex);

	cfg80211_chandef_create(&chandef, channel, NL80211_CHAN_NO_HT);
	ret = iwl_mvm_phy_ctxt_changed(mvm, &mvm->phy_ctxt_roc,
				       &chandef, 1, 1);

	/* Schedule the time events */
	ret = iwl_mvm_start_p2p_roc(mvm, vif, duration, type);

	mutex_unlock(&mvm->mutex);
	IWL_DEBUG_MAC80211(mvm, "leave\n");

	return ret;
}

static int iwl_mvm_cancel_roc(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	IWL_DEBUG_MAC80211(mvm, "enter\n");

	mutex_lock(&mvm->mutex);
	iwl_mvm_stop_p2p_roc(mvm);
	mutex_unlock(&mvm->mutex);

	IWL_DEBUG_MAC80211(mvm, "leave\n");
	return 0;
}

static int iwl_mvm_add_chanctx(struct ieee80211_hw *hw,
			       struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_phy_ctxt *phy_ctxt = (void *)ctx->drv_priv;
	int ret;

	mutex_lock(&mvm->mutex);

	IWL_DEBUG_MAC80211(mvm, "Add PHY context\n");
	ret = iwl_mvm_phy_ctxt_add(mvm, phy_ctxt, &ctx->def,
				   ctx->rx_chains_static,
				   ctx->rx_chains_dynamic);
	mutex_unlock(&mvm->mutex);
	return ret;
}

static void iwl_mvm_remove_chanctx(struct ieee80211_hw *hw,
				   struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_phy_ctxt *phy_ctxt = (void *)ctx->drv_priv;

	mutex_lock(&mvm->mutex);
	iwl_mvm_phy_ctxt_remove(mvm, phy_ctxt);
	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_change_chanctx(struct ieee80211_hw *hw,
				   struct ieee80211_chanctx_conf *ctx,
				   u32 changed)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_phy_ctxt *phy_ctxt = (void *)ctx->drv_priv;

	mutex_lock(&mvm->mutex);
	iwl_mvm_phy_ctxt_changed(mvm, phy_ctxt, &ctx->def,
				 ctx->rx_chains_static,
				 ctx->rx_chains_dynamic);
	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_assign_vif_chanctx(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_phy_ctxt *phyctx = (void *)ctx->drv_priv;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	mutex_lock(&mvm->mutex);

	mvmvif->phy_ctxt = phyctx;

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
		/*
		 * The AP binding flow is handled as part of the start_ap flow
		 * (in bss_info_changed).
		 */
		ret = 0;
		goto out_unlock;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MONITOR:
		break;
	default:
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = iwl_mvm_binding_add_vif(mvm, vif);
	if (ret)
		goto out_unlock;

	/*
	 * Setting the quota at this stage is only required for monitor
	 * interfaces. For the other types, the bss_info changed flow
	 * will handle quota settings.
	 */
	if (vif->type == NL80211_IFTYPE_MONITOR) {
		mvmvif->monitor_active = true;
		ret = iwl_mvm_update_quotas(mvm, vif);
		if (ret)
			goto out_remove_binding;
	}

	goto out_unlock;

 out_remove_binding:
	iwl_mvm_binding_remove_vif(mvm, vif);
 out_unlock:
	mutex_unlock(&mvm->mutex);
	if (ret)
		mvmvif->phy_ctxt = NULL;
	return ret;
}

static void iwl_mvm_unassign_vif_chanctx(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	mutex_lock(&mvm->mutex);

	iwl_mvm_remove_time_event(mvm, mvmvif, &mvmvif->time_event_data);

	if (vif->type == NL80211_IFTYPE_AP)
		goto out_unlock;

	switch (vif->type) {
	case NL80211_IFTYPE_MONITOR:
		mvmvif->monitor_active = false;
		iwl_mvm_update_quotas(mvm, NULL);
		break;
	default:
		break;
	}

	iwl_mvm_binding_remove_vif(mvm, vif);
out_unlock:
	mvmvif->phy_ctxt = NULL;
	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_set_tim(struct ieee80211_hw *hw,
			   struct ieee80211_sta *sta,
			   bool set)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_sta *mvm_sta = (void *)sta->drv_priv;

	if (!mvm_sta || !mvm_sta->vif) {
		IWL_ERR(mvm, "Station is not associated to a vif\n");
		return -EINVAL;
	}

	return iwl_mvm_mac_ctxt_beacon_changed(mvm, mvm_sta->vif);
}

static void iwl_mvm_mac_rssi_callback(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      enum ieee80211_rssi_event rssi_event)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	iwl_mvm_bt_rssi_event(mvm, vif, rssi_event);
}

struct ieee80211_ops iwl_mvm_hw_ops = {
	.tx = iwl_mvm_mac_tx,
	.ampdu_action = iwl_mvm_mac_ampdu_action,
	.start = iwl_mvm_mac_start,
	.restart_complete = iwl_mvm_mac_restart_complete,
	.stop = iwl_mvm_mac_stop,
	.add_interface = iwl_mvm_mac_add_interface,
	.remove_interface = iwl_mvm_mac_remove_interface,
	.config = iwl_mvm_mac_config,
	.configure_filter = iwl_mvm_configure_filter,
	.bss_info_changed = iwl_mvm_bss_info_changed,
	.hw_scan = iwl_mvm_mac_hw_scan,
	.cancel_hw_scan = iwl_mvm_mac_cancel_hw_scan,
	.sta_state = iwl_mvm_mac_sta_state,
	.sta_notify = iwl_mvm_mac_sta_notify,
	.allow_buffered_frames = iwl_mvm_mac_allow_buffered_frames,
	.set_rts_threshold = iwl_mvm_mac_set_rts_threshold,
	.conf_tx = iwl_mvm_mac_conf_tx,
	.mgd_prepare_tx = iwl_mvm_mac_mgd_prepare_tx,
	.set_key = iwl_mvm_mac_set_key,
	.update_tkip_key = iwl_mvm_mac_update_tkip_key,
	.remain_on_channel = iwl_mvm_roc,
	.cancel_remain_on_channel = iwl_mvm_cancel_roc,
	.rssi_callback = iwl_mvm_mac_rssi_callback,

	.add_chanctx = iwl_mvm_add_chanctx,
	.remove_chanctx = iwl_mvm_remove_chanctx,
	.change_chanctx = iwl_mvm_change_chanctx,
	.assign_vif_chanctx = iwl_mvm_assign_vif_chanctx,
	.unassign_vif_chanctx = iwl_mvm_unassign_vif_chanctx,

	.start_ap = iwl_mvm_start_ap,
	.stop_ap = iwl_mvm_stop_ap,

	.set_tim = iwl_mvm_set_tim,

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
};
