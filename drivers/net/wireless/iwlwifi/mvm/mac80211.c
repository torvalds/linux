/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
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
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
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
#include "testmode.h"

static const struct ieee80211_iface_limit iwl_mvm_limits[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_STATION),
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_AP) |
			BIT(NL80211_IFTYPE_P2P_CLIENT) |
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

static void iwl_mvm_reset_phy_ctxts(struct iwl_mvm *mvm)
{
	int i;

	memset(mvm->phy_ctxts, 0, sizeof(mvm->phy_ctxts));
	for (i = 0; i < NUM_PHY_CTX; i++) {
		mvm->phy_ctxts[i].id = i;
		mvm->phy_ctxts[i].ref = 0;
	}
}

static int iwl_mvm_max_scan_ie_len(struct iwl_mvm *mvm)
{
	/* we create the 802.11 header and SSID element */
	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_NO_BASIC_SSID)
		return mvm->fw->ucode_capa.max_probe_length - 24 - 2;
	return mvm->fw->ucode_capa.max_probe_length - 24 - 34;
}

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
		    IEEE80211_HW_TIMING_BEACON_ONLY |
		    IEEE80211_HW_CONNECTION_MONITOR |
		    IEEE80211_HW_SUPPORTS_DYNAMIC_SMPS |
		    IEEE80211_HW_SUPPORTS_STATIC_SMPS;

	hw->queues = mvm->first_agg_queue;
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

	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_UAPSD_SUPPORT) {
		hw->flags |= IEEE80211_HW_SUPPORTS_UAPSD;
		hw->uapsd_queues = IWL_UAPSD_AC_INFO;
		hw->uapsd_max_sp_len = IWL_UAPSD_MAX_SP;
	}

	hw->sta_data_size = sizeof(struct iwl_mvm_sta);
	hw->vif_data_size = sizeof(struct iwl_mvm_vif);
	hw->chanctx_data_size = sizeof(u16);

	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_P2P_CLIENT) |
		BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_P2P_GO) |
		BIT(NL80211_IFTYPE_P2P_DEVICE);

	/* IBSS has bugs in older versions */
	if (IWL_UCODE_API(mvm->fw->ucode_ver) >= 8)
		hw->wiphy->interface_modes |= BIT(NL80211_IFTYPE_ADHOC);

	hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN;
	hw->wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG |
				       REGULATORY_DISABLE_BEACON_HINTS;

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

	iwl_mvm_reset_phy_ctxts(mvm);

	hw->wiphy->max_scan_ie_len = iwl_mvm_max_scan_ie_len(mvm);

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

	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_SCHED_SCAN) {
		hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_SCHED_SCAN;
		hw->wiphy->max_sched_scan_ssids = PROBE_OPTION_MAX;
		hw->wiphy->max_match_sets = IWL_SCAN_MAX_PROFILES;
		/* we create the 802.11 header and zero length SSID IE. */
		hw->wiphy->max_sched_scan_ie_len =
					SCAN_OFFLOAD_PROBE_REQ_SIZE - 24 - 2;
	}

	hw->wiphy->features |= NL80211_FEATURE_P2P_GO_CTWIN |
			       NL80211_FEATURE_P2P_GO_OPPPS |
			       NL80211_FEATURE_LOW_PRIORITY_SCAN;

	mvm->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;

	/* currently FW API supports only one optional cipher scheme */
	if (mvm->fw->cs[0].cipher) {
		mvm->hw->n_cipher_schemes = 1;
		mvm->hw->cipher_schemes = &mvm->fw->cs[0];
	}

#ifdef CONFIG_PM_SLEEP
	if (mvm->fw->img[IWL_UCODE_WOWLAN].sec[0].len &&
	    mvm->trans->ops->d3_suspend &&
	    mvm->trans->ops->d3_resume &&
	    device_can_wakeup(mvm->trans->dev)) {
		mvm->wowlan.flags = WIPHY_WOWLAN_MAGIC_PKT |
				    WIPHY_WOWLAN_DISCONNECT |
				    WIPHY_WOWLAN_EAP_IDENTITY_REQ |
				    WIPHY_WOWLAN_RFKILL_RELEASE;
		if (!iwlwifi_mod_params.sw_crypto)
			mvm->wowlan.flags |= WIPHY_WOWLAN_SUPPORTS_GTK_REKEY |
					     WIPHY_WOWLAN_GTK_REKEY_FAILURE |
					     WIPHY_WOWLAN_4WAY_HANDSHAKE;

		mvm->wowlan.n_patterns = IWL_WOWLAN_MAX_PATTERNS;
		mvm->wowlan.pattern_min_len = IWL_WOWLAN_MIN_PATTERN_LEN;
		mvm->wowlan.pattern_max_len = IWL_WOWLAN_MAX_PATTERN_LEN;
		mvm->wowlan.tcp = &iwl_mvm_wowlan_tcp_support;
		hw->wiphy->wowlan = &mvm->wowlan;
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

	if (iwl_mvm_is_radio_killed(mvm)) {
		IWL_DEBUG_DROP(mvm, "Dropping - RF/CT KILL\n");
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
		if (iwlwifi_mod_params.disable_11n & IWL_DISABLE_HT_RXAGG) {
			ret = -EINVAL;
			break;
		}
		ret = iwl_mvm_sta_rx_agg(mvm, sta, tid, *ssn, true);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		ret = iwl_mvm_sta_rx_agg(mvm, sta, tid, 0, false);
		break;
	case IEEE80211_AMPDU_TX_START:
		if (iwlwifi_mod_params.disable_11n & IWL_DISABLE_HT_TXAGG) {
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

	mvmvif->phy_ctxt = NULL;
}

static void iwl_mvm_restart_cleanup(struct iwl_mvm *mvm)
{
	iwl_trans_stop_device(mvm->trans);

	mvm->scan_status = IWL_MVM_SCAN_NONE;

	/* just in case one was running */
	ieee80211_remain_on_channel_expired(mvm->hw);

	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
		iwl_mvm_cleanup_iterator, mvm);

	mvm->p2p_device_vif = NULL;

	iwl_mvm_reset_phy_ctxts(mvm);
	memset(mvm->fw_key_table, 0, sizeof(mvm->fw_key_table));
	memset(mvm->sta_drained, 0, sizeof(mvm->sta_drained));

	ieee80211_wake_queues(mvm->hw);

	mvm->vif_count = 0;
	mvm->rx_ba_sessions = 0;
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

static void iwl_mvm_power_update_iterator(void *data, u8 *mac,
					  struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = data;

	iwl_mvm_power_update_mode(mvm, vif);
}

static struct iwl_mvm_phy_ctxt *iwl_mvm_get_free_phy_ctxt(struct iwl_mvm *mvm)
{
	u16 i;

	lockdep_assert_held(&mvm->mutex);

	for (i = 0; i < NUM_PHY_CTX; i++)
		if (!mvm->phy_ctxts[i].ref)
			return &mvm->phy_ctxts[i];

	IWL_ERR(mvm, "No available PHY context\n");
	return NULL;
}

static int iwl_mvm_set_tx_power(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				s8 tx_power)
{
	/* FW is in charge of regulatory enforcement */
	struct iwl_reduce_tx_power_cmd reduce_txpwr_cmd = {
		.mac_context_id = iwl_mvm_vif_from_mac80211(vif)->id,
		.pwr_restriction = cpu_to_le16(tx_power),
	};

	return iwl_mvm_send_cmd_pdu(mvm, REDUCE_TX_POWER_CMD, CMD_SYNC,
				    sizeof(reduce_txpwr_cmd),
				    &reduce_txpwr_cmd);
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

	/* Allocate resources for the MAC context, and add it to the fw  */
	ret = iwl_mvm_mac_ctxt_init(mvm, vif);
	if (ret)
		goto out_unlock;

	/* Counting number of interfaces is needed for legacy PM */
	if (vif->type != NL80211_IFTYPE_P2P_DEVICE)
		mvm->vif_count++;

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
	if (vif->type == NL80211_IFTYPE_AP ||
	    vif->type == NL80211_IFTYPE_ADHOC) {
		u32 qmask = iwl_mvm_mac_get_queues_mask(mvm, vif);
		ret = iwl_mvm_allocate_int_sta(mvm, &mvmvif->bcast_sta,
					       qmask);
		if (ret) {
			IWL_ERR(mvm, "Failed to allocate bcast sta\n");
			goto out_release;
		}

		iwl_mvm_vif_dbgfs_register(mvm, vif);
		goto out_unlock;
	}

	ret = iwl_mvm_mac_ctxt_add(mvm, vif);
	if (ret)
		goto out_release;

	iwl_mvm_power_disable(mvm, vif);

	/* beacon filtering */
	ret = iwl_mvm_disable_beacon_filter(mvm, vif);
	if (ret)
		goto out_remove_mac;

	if (!mvm->bf_allowed_vif &&
	    vif->type == NL80211_IFTYPE_STATION && !vif->p2p &&
	    mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_BF_UPDATED){
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
		ret = iwl_mvm_binding_add_vif(mvm, vif);
		if (ret)
			goto out_unref_phy;

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
	iwl_mvm_mac_ctxt_remove(mvm, vif);
 out_release:
	if (vif->type != NL80211_IFTYPE_P2P_DEVICE)
		mvm->vif_count--;

	/* TODO: remove this when legacy PM will be discarded */
	ieee80211_iterate_active_interfaces(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_power_update_iterator, mvm);

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
		 * triggered. Flush it.
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

	if (mvm->bf_allowed_vif == mvmvif) {
		mvm->bf_allowed_vif = NULL;
		vif->driver_flags &= ~(IEEE80211_VIF_BEACON_FILTER |
				       IEEE80211_VIF_SUPPORTS_CQM_RSSI);
	}

	iwl_mvm_vif_dbgfs_clean(mvm, vif);

	/*
	 * For AP/GO interface, the tear down of the resources allocated to the
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
		iwl_mvm_dealloc_int_sta(mvm, &mvmvif->bcast_sta);
		goto out_release;
	}

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		mvm->p2p_device_vif = NULL;
		iwl_mvm_rm_bcast_sta(mvm, &mvmvif->bcast_sta);
		iwl_mvm_binding_remove_vif(mvm, vif);
		iwl_mvm_phy_ctxt_unref(mvm, mvmvif->phy_ctxt);
		mvmvif->phy_ctxt = NULL;
	}

	if (mvm->vif_count && vif->type != NL80211_IFTYPE_P2P_DEVICE)
		mvm->vif_count--;

	/* TODO: remove this when legacy PM will be discarded */
	ieee80211_iterate_active_interfaces(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_power_update_iterator, mvm);

	iwl_mvm_mac_ctxt_remove(mvm, vif);

out_release:
	iwl_mvm_mac_ctxt_release(mvm, vif);
	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_mac_config(struct ieee80211_hw *hw, u32 changed)
{
	return 0;
}

struct iwl_mvm_mc_iter_data {
	struct iwl_mvm *mvm;
	int port_id;
};

static void iwl_mvm_mc_iface_iterator(void *_data, u8 *mac,
				      struct ieee80211_vif *vif)
{
	struct iwl_mvm_mc_iter_data *data = _data;
	struct iwl_mvm *mvm = data->mvm;
	struct iwl_mcast_filter_cmd *cmd = mvm->mcast_filter_cmd;
	int ret, len;

	/* if we don't have free ports, mcast frames will be dropped */
	if (WARN_ON_ONCE(data->port_id >= MAX_PORT_ID_NUM))
		return;

	if (vif->type != NL80211_IFTYPE_STATION ||
	    !vif->bss_conf.assoc)
		return;

	cmd->port_id = data->port_id++;
	memcpy(cmd->bssid, vif->bss_conf.bssid, ETH_ALEN);
	len = roundup(sizeof(*cmd) + cmd->count * ETH_ALEN, 4);

	ret = iwl_mvm_send_cmd_pdu(mvm, MCAST_FILTER_CMD, CMD_SYNC, len, cmd);
	if (ret)
		IWL_ERR(mvm, "mcast filter cmd error. ret=%d\n", ret);
}

static void iwl_mvm_recalc_multicast(struct iwl_mvm *mvm)
{
	struct iwl_mvm_mc_iter_data iter_data = {
		.mvm = mvm,
	};

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON_ONCE(!mvm->mcast_filter_cmd))
		return;

	ieee80211_iterate_active_interfaces(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_mc_iface_iterator, &iter_data);
}

static u64 iwl_mvm_prepare_multicast(struct ieee80211_hw *hw,
				     struct netdev_hw_addr_list *mc_list)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mcast_filter_cmd *cmd;
	struct netdev_hw_addr *addr;
	int addr_count = netdev_hw_addr_list_count(mc_list);
	bool pass_all = false;
	int len;

	if (addr_count > MAX_MCAST_FILTERING_ADDRESSES) {
		pass_all = true;
		addr_count = 0;
	}

	len = roundup(sizeof(*cmd) + addr_count * ETH_ALEN, 4);
	cmd = kzalloc(len, GFP_ATOMIC);
	if (!cmd)
		return 0;

	if (pass_all) {
		cmd->pass_all = 1;
		return (u64)(unsigned long)cmd;
	}

	netdev_hw_addr_list_for_each(addr, mc_list) {
		IWL_DEBUG_MAC80211(mvm, "mcast addr (%d): %pM\n",
				   cmd->count, addr->addr);
		memcpy(&cmd->addr_list[cmd->count * ETH_ALEN],
		       addr->addr, ETH_ALEN);
		cmd->count++;
	}

	return (u64)(unsigned long)cmd;
}

static void iwl_mvm_configure_filter(struct ieee80211_hw *hw,
				     unsigned int changed_flags,
				     unsigned int *total_flags,
				     u64 multicast)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mcast_filter_cmd *cmd = (void *)(unsigned long)multicast;

	mutex_lock(&mvm->mutex);

	/* replace previous configuration */
	kfree(mvm->mcast_filter_cmd);
	mvm->mcast_filter_cmd = cmd;

	if (!cmd)
		goto out;

	iwl_mvm_recalc_multicast(mvm);
out:
	mutex_unlock(&mvm->mutex);
	*total_flags = 0;
}

static void iwl_mvm_bss_info_changed_station(struct iwl_mvm *mvm,
					     struct ieee80211_vif *vif,
					     struct ieee80211_bss_conf *bss_conf,
					     u32 changes)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	/*
	 * Re-calculate the tsf id, as the master-slave relations depend on the
	 * beacon interval, which was not known when the station interface was
	 * added.
	 */
	if (changes & BSS_CHANGED_ASSOC && bss_conf->assoc)
		iwl_mvm_mac_ctxt_recalc_tsf_id(mvm, vif);

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

			if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART,
				     &mvm->status)) {
				/*
				 * If we're restarting then the firmware will
				 * obviously have lost synchronisation with
				 * the AP. It will attempt to synchronise by
				 * itself, but we can make it more reliable by
				 * scheduling a session protection time event.
				 *
				 * The firmware needs to receive a beacon to
				 * catch up with synchronisation, use 110% of
				 * the beacon interval.
				 *
				 * Set a large maximum delay to allow for more
				 * than a single interface.
				 */
				u32 dur = (11 * vif->bss_conf.beacon_int) / 10;
				iwl_mvm_protect_session(mvm, vif, dur, dur,
							5 * dur);
			}

			iwl_mvm_sf_update(mvm, vif, false);
			iwl_mvm_power_vif_assoc(mvm, vif);
		} else if (mvmvif->ap_sta_id != IWL_MVM_STATION_COUNT) {
			/*
			 * If update fails - SF might be running in associated
			 * mode while disassociated - which is forbidden.
			 */
			WARN_ONCE(iwl_mvm_sf_update(mvm, vif, false),
				  "Failed to update SF upon disassociation\n");

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

		iwl_mvm_recalc_multicast(mvm);

		/* reset rssi values */
		mvmvif->bf_data.ave_beacon_signal = 0;

		if (!(mvm->fw->ucode_capa.flags &
					IWL_UCODE_TLV_FLAGS_PM_CMD_SUPPORT)) {
			/* Workaround for FW bug, otherwise FW disables device
			 * power save upon disassociation
			 */
			ret = iwl_mvm_power_update_mode(mvm, vif);
			if (ret)
				IWL_ERR(mvm, "failed to update power mode\n");
		}
		iwl_mvm_bt_coex_vif_change(mvm);
		iwl_mvm_update_smps(mvm, vif, IWL_MVM_SMPS_REQ_TT,
				    IEEE80211_SMPS_AUTOMATIC);
	} else if (changes & BSS_CHANGED_BEACON_INFO) {
		/*
		 * We received a beacon _after_ association so
		 * remove the session protection.
		 */
		iwl_mvm_remove_time_event(mvm, mvmvif,
					  &mvmvif->time_event_data);
	} else if (changes & (BSS_CHANGED_PS | BSS_CHANGED_P2P_PS |
			      BSS_CHANGED_QOS)) {
		ret = iwl_mvm_power_update_mode(mvm, vif);
		if (ret)
			IWL_ERR(mvm, "failed to update power mode\n");
	}
	if (changes & BSS_CHANGED_TXPOWER) {
		IWL_DEBUG_CALIB(mvm, "Changing TX Power to %d\n",
				bss_conf->txpower);
		iwl_mvm_set_tx_power(mvm, vif, bss_conf->txpower);
	}

	if (changes & BSS_CHANGED_CQM) {
		IWL_DEBUG_MAC80211(mvm, "cqm info_changed");
		/* reset cqm events tracking */
		mvmvif->bf_data.last_cqm_event = 0;
		ret = iwl_mvm_update_beacon_filter(mvm, vif);
		if (ret)
			IWL_ERR(mvm, "failed to update CQM thresholds\n");
	}
}

static int iwl_mvm_start_ap_ibss(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	mutex_lock(&mvm->mutex);

	/* Send the beacon template */
	ret = iwl_mvm_mac_ctxt_beacon_changed(mvm, vif);
	if (ret)
		goto out_unlock;

	/*
	 * Re-calculate the tsf id, as the master-slave relations depend on the
	 * beacon interval, which was not known when the AP interface was added.
	 */
	if (vif->type == NL80211_IFTYPE_AP)
		iwl_mvm_mac_ctxt_recalc_tsf_id(mvm, vif);

	/* Add the mac context */
	ret = iwl_mvm_mac_ctxt_add(mvm, vif);
	if (ret)
		goto out_unlock;

	/* Perform the binding */
	ret = iwl_mvm_binding_add_vif(mvm, vif);
	if (ret)
		goto out_remove;

	mvmvif->ap_ibss_active = true;

	/* Send the bcast station. At this stage the TBTT and DTIM time events
	 * are added and applied to the scheduler */
	ret = iwl_mvm_send_bcast_sta(mvm, vif, &mvmvif->bcast_sta);
	if (ret)
		goto out_unbind;

	/* must be set before quota calculations */
	mvmvif->ap_ibss_active = true;

	/* power updated needs to be done before quotas */
	mvm->bound_vif_cnt++;
	iwl_mvm_power_update_binding(mvm, vif, true);

	ret = iwl_mvm_update_quotas(mvm, vif);
	if (ret)
		goto out_quota_failed;

	/* Need to update the P2P Device MAC (only GO, IBSS is single vif) */
	if (vif->p2p && mvm->p2p_device_vif)
		iwl_mvm_mac_ctxt_changed(mvm, mvm->p2p_device_vif);

	iwl_mvm_bt_coex_vif_change(mvm);

	mutex_unlock(&mvm->mutex);
	return 0;

out_quota_failed:
	mvm->bound_vif_cnt--;
	iwl_mvm_power_update_binding(mvm, vif, false);
	mvmvif->ap_ibss_active = false;
	iwl_mvm_send_rm_bcast_sta(mvm, &mvmvif->bcast_sta);
out_unbind:
	iwl_mvm_binding_remove_vif(mvm, vif);
out_remove:
	iwl_mvm_mac_ctxt_remove(mvm, vif);
out_unlock:
	mutex_unlock(&mvm->mutex);
	return ret;
}

static void iwl_mvm_stop_ap_ibss(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	iwl_mvm_prepare_mac_removal(mvm, vif);

	mutex_lock(&mvm->mutex);

	mvmvif->ap_ibss_active = false;

	iwl_mvm_bt_coex_vif_change(mvm);

	/* Need to update the P2P Device MAC (only GO, IBSS is single vif) */
	if (vif->p2p && mvm->p2p_device_vif)
		iwl_mvm_mac_ctxt_changed(mvm, mvm->p2p_device_vif);

	iwl_mvm_update_quotas(mvm, NULL);
	iwl_mvm_send_rm_bcast_sta(mvm, &mvmvif->bcast_sta);
	iwl_mvm_binding_remove_vif(mvm, vif);

	mvm->bound_vif_cnt--;
	iwl_mvm_power_update_binding(mvm, vif, false);

	iwl_mvm_mac_ctxt_remove(mvm, vif);

	mutex_unlock(&mvm->mutex);
}

static void
iwl_mvm_bss_info_changed_ap_ibss(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *bss_conf,
				 u32 changes)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	enum ieee80211_bss_change ht_change = BSS_CHANGED_ERP_CTS_PROT |
					      BSS_CHANGED_HT |
					      BSS_CHANGED_BANDWIDTH;
	int ret;

	/* Changes will be applied when the AP/IBSS is started */
	if (!mvmvif->ap_ibss_active)
		return;

	if (changes & ht_change) {
		ret = iwl_mvm_mac_ctxt_changed(mvm, vif);
		if (ret)
			IWL_ERR(mvm, "failed to update MAC %pM\n", vif->addr);
	}

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
	case NL80211_IFTYPE_ADHOC:
		iwl_mvm_bss_info_changed_ap_ibss(mvm, vif, bss_conf, changes);
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
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);

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

static void iwl_mvm_sta_pre_rcu_remove(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_sta *mvm_sta = (void *)sta->drv_priv;

	/*
	 * This is called before mac80211 does RCU synchronisation,
	 * so here we already invalidate our internal RCU-protected
	 * station pointer. The rest of the code will thus no longer
	 * be able to find the station this way, and we don't rely
	 * on further RCU synchronisation after the sta_state()
	 * callback deleted the station.
	 */
	mutex_lock(&mvm->mutex);
	if (sta == rcu_access_pointer(mvm->fw_id_to_mac_id[mvm_sta->sta_id]))
		rcu_assign_pointer(mvm->fw_id_to_mac_id[mvm_sta->sta_id],
				   ERR_PTR(-ENOENT));
	mutex_unlock(&mvm->mutex);
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
					     mvmvif->phy_ctxt->channel->band,
					     true);
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTHORIZED) {
		/* enable beacon filtering */
		WARN_ON(iwl_mvm_enable_beacon_filter(mvm, vif));
		ret = 0;
	} else if (old_state == IEEE80211_STA_AUTHORIZED &&
		   new_state == IEEE80211_STA_ASSOC) {
		/* disable beacon filtering */
		WARN_ON(iwl_mvm_disable_beacon_filter(mvm, vif));
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

static void iwl_mvm_sta_rc_update(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta, u32 changed)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	if (vif->type == NL80211_IFTYPE_STATION &&
	    changed & IEEE80211_RC_NSS_CHANGED)
		iwl_mvm_sf_update(mvm, vif, false);
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
	iwl_mvm_protect_session(mvm, vif, duration, min_duration, 500);
	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_mac_sched_scan_start(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct cfg80211_sched_scan_request *req,
					struct ieee80211_sched_scan_ies *ies)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);

	if (mvm->scan_status != IWL_MVM_SCAN_NONE) {
		IWL_DEBUG_SCAN(mvm,
			       "SCHED SCAN request during internal scan - abort\n");
		ret = -EBUSY;
		goto out;
	}

	mvm->scan_status = IWL_MVM_SCAN_SCHED;

	ret = iwl_mvm_config_sched_scan(mvm, vif, req, ies);
	if (ret)
		goto err;

	ret = iwl_mvm_config_sched_scan_profiles(mvm, req);
	if (ret)
		goto err;

	ret = iwl_mvm_sched_scan_start(mvm, req);
	if (!ret)
		goto out;
err:
	mvm->scan_status = IWL_MVM_SCAN_NONE;
out:
	mutex_unlock(&mvm->mutex);
	return ret;
}

static void iwl_mvm_mac_sched_scan_stop(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);
	iwl_mvm_sched_scan_stop(mvm);
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
		/* currently FW supports only one optional cipher scheme */
		if (hw->n_cipher_schemes &&
		    hw->cipher_schemes->cipher == key->cipher)
			key->flags |= IEEE80211_KEY_FLAG_PUT_IV_SPACE;
		else
			return -EOPNOTSUPP;
	}

	mutex_lock(&mvm->mutex);

	switch (cmd) {
	case SET_KEY:
		if ((vif->type == NL80211_IFTYPE_ADHOC ||
		     vif->type == NL80211_IFTYPE_AP) && !sta) {
			/*
			 * GTK on AP interface is a TX-only key, return 0;
			 * on IBSS they're per-station and because we're lazy
			 * we don't support them for RX, so do the same.
			 */
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

	if (keyconf->hw_key_idx == STA_KEY_IDX_INVALID)
		return;

	iwl_mvm_update_tkip_key(mvm, vif, keyconf, sta, iv32, phase1key);
}


static int iwl_mvm_roc(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif,
		       struct ieee80211_channel *channel,
		       int duration,
		       enum ieee80211_roc_type type)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct cfg80211_chan_def chandef;
	struct iwl_mvm_phy_ctxt *phy_ctxt;
	int ret, i;

	IWL_DEBUG_MAC80211(mvm, "enter (%d, %d, %d)\n", channel->hw_value,
			   duration, type);

	if (vif->type != NL80211_IFTYPE_P2P_DEVICE) {
		IWL_ERR(mvm, "vif isn't a P2P_DEVICE: %d\n", vif->type);
		return -EINVAL;
	}

	mutex_lock(&mvm->mutex);

	for (i = 0; i < NUM_PHY_CTX; i++) {
		phy_ctxt = &mvm->phy_ctxts[i];
		if (phy_ctxt->ref == 0 || mvmvif->phy_ctxt == phy_ctxt)
			continue;

		if (phy_ctxt->ref && channel == phy_ctxt->channel) {
			/*
			 * Unbind the P2P_DEVICE from the current PHY context,
			 * and if the PHY context is not used remove it.
			 */
			ret = iwl_mvm_binding_remove_vif(mvm, vif);
			if (WARN(ret, "Failed unbinding P2P_DEVICE\n"))
				goto out_unlock;

			iwl_mvm_phy_ctxt_unref(mvm, mvmvif->phy_ctxt);

			/* Bind the P2P_DEVICE to the current PHY Context */
			mvmvif->phy_ctxt = phy_ctxt;

			ret = iwl_mvm_binding_add_vif(mvm, vif);
			if (WARN(ret, "Failed binding P2P_DEVICE\n"))
				goto out_unlock;

			iwl_mvm_phy_ctxt_ref(mvm, mvmvif->phy_ctxt);
			goto schedule_time_event;
		}
	}

	/* Need to update the PHY context only if the ROC channel changed */
	if (channel == mvmvif->phy_ctxt->channel)
		goto schedule_time_event;

	cfg80211_chandef_create(&chandef, channel, NL80211_CHAN_NO_HT);

	/*
	 * Change the PHY context configuration as it is currently referenced
	 * only by the P2P Device MAC
	 */
	if (mvmvif->phy_ctxt->ref == 1) {
		ret = iwl_mvm_phy_ctxt_changed(mvm, mvmvif->phy_ctxt,
					       &chandef, 1, 1);
		if (ret)
			goto out_unlock;
	} else {
		/*
		 * The PHY context is shared with other MACs. Need to remove the
		 * P2P Device from the binding, allocate an new PHY context and
		 * create a new binding
		 */
		phy_ctxt = iwl_mvm_get_free_phy_ctxt(mvm);
		if (!phy_ctxt) {
			ret = -ENOSPC;
			goto out_unlock;
		}

		ret = iwl_mvm_phy_ctxt_changed(mvm, phy_ctxt, &chandef,
					       1, 1);
		if (ret) {
			IWL_ERR(mvm, "Failed to change PHY context\n");
			goto out_unlock;
		}

		/* Unbind the P2P_DEVICE from the current PHY context */
		ret = iwl_mvm_binding_remove_vif(mvm, vif);
		if (WARN(ret, "Failed unbinding P2P_DEVICE\n"))
			goto out_unlock;

		iwl_mvm_phy_ctxt_unref(mvm, mvmvif->phy_ctxt);

		/* Bind the P2P_DEVICE to the new allocated PHY context */
		mvmvif->phy_ctxt = phy_ctxt;

		ret = iwl_mvm_binding_add_vif(mvm, vif);
		if (WARN(ret, "Failed binding P2P_DEVICE\n"))
			goto out_unlock;

		iwl_mvm_phy_ctxt_ref(mvm, mvmvif->phy_ctxt);
	}

schedule_time_event:
	/* Schedule the time events */
	ret = iwl_mvm_start_p2p_roc(mvm, vif, duration, type);

out_unlock:
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
	u16 *phy_ctxt_id = (u16 *)ctx->drv_priv;
	struct iwl_mvm_phy_ctxt *phy_ctxt;
	int ret;

	IWL_DEBUG_MAC80211(mvm, "Add channel context\n");

	mutex_lock(&mvm->mutex);
	phy_ctxt = iwl_mvm_get_free_phy_ctxt(mvm);
	if (!phy_ctxt) {
		ret = -ENOSPC;
		goto out;
	}

	ret = iwl_mvm_phy_ctxt_changed(mvm, phy_ctxt, &ctx->min_def,
				       ctx->rx_chains_static,
				       ctx->rx_chains_dynamic);
	if (ret) {
		IWL_ERR(mvm, "Failed to add PHY context\n");
		goto out;
	}

	iwl_mvm_phy_ctxt_ref(mvm, phy_ctxt);
	*phy_ctxt_id = phy_ctxt->id;
out:
	mutex_unlock(&mvm->mutex);
	return ret;
}

static void iwl_mvm_remove_chanctx(struct ieee80211_hw *hw,
				   struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	u16 *phy_ctxt_id = (u16 *)ctx->drv_priv;
	struct iwl_mvm_phy_ctxt *phy_ctxt = &mvm->phy_ctxts[*phy_ctxt_id];

	mutex_lock(&mvm->mutex);
	iwl_mvm_phy_ctxt_unref(mvm, phy_ctxt);
	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_change_chanctx(struct ieee80211_hw *hw,
				   struct ieee80211_chanctx_conf *ctx,
				   u32 changed)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	u16 *phy_ctxt_id = (u16 *)ctx->drv_priv;
	struct iwl_mvm_phy_ctxt *phy_ctxt = &mvm->phy_ctxts[*phy_ctxt_id];

	if (WARN_ONCE((phy_ctxt->ref > 1) &&
		      (changed & ~(IEEE80211_CHANCTX_CHANGE_WIDTH |
				   IEEE80211_CHANCTX_CHANGE_RX_CHAINS |
				   IEEE80211_CHANCTX_CHANGE_RADAR |
				   IEEE80211_CHANCTX_CHANGE_MIN_WIDTH)),
		      "Cannot change PHY. Ref=%d, changed=0x%X\n",
		      phy_ctxt->ref, changed))
		return;

	mutex_lock(&mvm->mutex);
	iwl_mvm_phy_ctxt_changed(mvm, phy_ctxt, &ctx->min_def,
				 ctx->rx_chains_static,
				 ctx->rx_chains_dynamic);
	iwl_mvm_bt_coex_vif_change(mvm);
	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_assign_vif_chanctx(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	u16 *phy_ctxt_id = (u16 *)ctx->drv_priv;
	struct iwl_mvm_phy_ctxt *phy_ctxt = &mvm->phy_ctxts[*phy_ctxt_id];
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	mutex_lock(&mvm->mutex);

	mvmvif->phy_ctxt = phy_ctxt;

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
		/*
		 * The AP binding flow is handled as part of the start_ap flow
		 * (in bss_info_changed), similarly for IBSS.
		 */
		ret = 0;
		goto out_unlock;
	case NL80211_IFTYPE_STATION:
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
	 * Power state must be updated before quotas,
	 * otherwise fw will complain.
	 */
	mvm->bound_vif_cnt++;
	iwl_mvm_power_update_binding(mvm, vif, true);

	/* Setting the quota at this stage is only required for monitor
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
	mvm->bound_vif_cnt--;
	iwl_mvm_power_update_binding(mvm, vif, false);
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

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
		goto out_unlock;
	case NL80211_IFTYPE_MONITOR:
		mvmvif->monitor_active = false;
		iwl_mvm_update_quotas(mvm, NULL);
		break;
	default:
		break;
	}

	iwl_mvm_binding_remove_vif(mvm, vif);
	mvm->bound_vif_cnt--;
	iwl_mvm_power_update_binding(mvm, vif, false);

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

#ifdef CONFIG_NL80211_TESTMODE
static const struct nla_policy iwl_mvm_tm_policy[IWL_MVM_TM_ATTR_MAX + 1] = {
	[IWL_MVM_TM_ATTR_CMD] = { .type = NLA_U32 },
	[IWL_MVM_TM_ATTR_NOA_DURATION] = { .type = NLA_U32 },
	[IWL_MVM_TM_ATTR_BEACON_FILTER_STATE] = { .type = NLA_U32 },
};

static int __iwl_mvm_mac_testmode_cmd(struct iwl_mvm *mvm,
				      struct ieee80211_vif *vif,
				      void *data, int len)
{
	struct nlattr *tb[IWL_MVM_TM_ATTR_MAX + 1];
	int err;
	u32 noa_duration;

	err = nla_parse(tb, IWL_MVM_TM_ATTR_MAX, data, len, iwl_mvm_tm_policy);
	if (err)
		return err;

	if (!tb[IWL_MVM_TM_ATTR_CMD])
		return -EINVAL;

	switch (nla_get_u32(tb[IWL_MVM_TM_ATTR_CMD])) {
	case IWL_MVM_TM_CMD_SET_NOA:
		if (!vif || vif->type != NL80211_IFTYPE_AP || !vif->p2p ||
		    !vif->bss_conf.enable_beacon ||
		    !tb[IWL_MVM_TM_ATTR_NOA_DURATION])
			return -EINVAL;

		noa_duration = nla_get_u32(tb[IWL_MVM_TM_ATTR_NOA_DURATION]);
		if (noa_duration >= vif->bss_conf.beacon_int)
			return -EINVAL;

		mvm->noa_duration = noa_duration;
		mvm->noa_vif = vif;

		return iwl_mvm_update_quotas(mvm, NULL);
	case IWL_MVM_TM_CMD_SET_BEACON_FILTER:
		/* must be associated client vif - ignore authorized */
		if (!vif || vif->type != NL80211_IFTYPE_STATION ||
		    !vif->bss_conf.assoc || !vif->bss_conf.dtim_period ||
		    !tb[IWL_MVM_TM_ATTR_BEACON_FILTER_STATE])
			return -EINVAL;

		if (nla_get_u32(tb[IWL_MVM_TM_ATTR_BEACON_FILTER_STATE]))
			return iwl_mvm_enable_beacon_filter(mvm, vif);
		return iwl_mvm_disable_beacon_filter(mvm, vif);
	}

	return -EOPNOTSUPP;
}

static int iwl_mvm_mac_testmode_cmd(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    void *data, int len)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int err;

	mutex_lock(&mvm->mutex);
	err = __iwl_mvm_mac_testmode_cmd(mvm, vif, data, len);
	mutex_unlock(&mvm->mutex);

	return err;
}
#endif

struct ieee80211_ops iwl_mvm_hw_ops = {
	.tx = iwl_mvm_mac_tx,
	.ampdu_action = iwl_mvm_mac_ampdu_action,
	.start = iwl_mvm_mac_start,
	.restart_complete = iwl_mvm_mac_restart_complete,
	.stop = iwl_mvm_mac_stop,
	.add_interface = iwl_mvm_mac_add_interface,
	.remove_interface = iwl_mvm_mac_remove_interface,
	.config = iwl_mvm_mac_config,
	.prepare_multicast = iwl_mvm_prepare_multicast,
	.configure_filter = iwl_mvm_configure_filter,
	.bss_info_changed = iwl_mvm_bss_info_changed,
	.hw_scan = iwl_mvm_mac_hw_scan,
	.cancel_hw_scan = iwl_mvm_mac_cancel_hw_scan,
	.sta_pre_rcu_remove = iwl_mvm_sta_pre_rcu_remove,
	.sta_state = iwl_mvm_mac_sta_state,
	.sta_notify = iwl_mvm_mac_sta_notify,
	.allow_buffered_frames = iwl_mvm_mac_allow_buffered_frames,
	.set_rts_threshold = iwl_mvm_mac_set_rts_threshold,
	.sta_rc_update = iwl_mvm_sta_rc_update,
	.conf_tx = iwl_mvm_mac_conf_tx,
	.mgd_prepare_tx = iwl_mvm_mac_mgd_prepare_tx,
	.sched_scan_start = iwl_mvm_mac_sched_scan_start,
	.sched_scan_stop = iwl_mvm_mac_sched_scan_stop,
	.set_key = iwl_mvm_mac_set_key,
	.update_tkip_key = iwl_mvm_mac_update_tkip_key,
	.remain_on_channel = iwl_mvm_roc,
	.cancel_remain_on_channel = iwl_mvm_cancel_roc,
	.add_chanctx = iwl_mvm_add_chanctx,
	.remove_chanctx = iwl_mvm_remove_chanctx,
	.change_chanctx = iwl_mvm_change_chanctx,
	.assign_vif_chanctx = iwl_mvm_assign_vif_chanctx,
	.unassign_vif_chanctx = iwl_mvm_unassign_vif_chanctx,

	.start_ap = iwl_mvm_start_ap_ibss,
	.stop_ap = iwl_mvm_stop_ap_ibss,
	.join_ibss = iwl_mvm_start_ap_ibss,
	.leave_ibss = iwl_mvm_stop_ap_ibss,

	.set_tim = iwl_mvm_set_tim,

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
};
