// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */

#include <net/mac80211.h>
#include <linux/ip.h>

#include "mld.h"
#include "mac80211.h"
#include "phy.h"
#include "iface.h"
#include "power.h"
#include "sta.h"
#include "agg.h"
#include "scan.h"
#include "d3.h"
#include "tlc.h"
#include "key.h"
#include "ap.h"
#include "tx.h"
#include "roc.h"
#include "mlo.h"
#include "stats.h"
#include "ftm-initiator.h"
#include "low_latency.h"
#include "fw/api/scan.h"
#include "fw/api/context.h"
#include "fw/api/filter.h"
#include "fw/api/sta.h"
#include "fw/api/tdls.h"
#ifdef CONFIG_PM_SLEEP
#include "fw/api/d3.h"
#endif /* CONFIG_PM_SLEEP */
#include "iwl-trans.h"

#define IWL_MLD_LIMITS(ap)					\
	{							\
		.max = 2,					\
		.types = BIT(NL80211_IFTYPE_STATION),		\
	},							\
	{							\
		.max = 1,					\
		.types = ap |					\
			 BIT(NL80211_IFTYPE_P2P_CLIENT) |	\
			 BIT(NL80211_IFTYPE_P2P_GO),		\
	},							\
	{							\
		.max = 1,					\
		.types = BIT(NL80211_IFTYPE_P2P_DEVICE),	\
	}

static const struct ieee80211_iface_limit iwl_mld_limits[] = {
	IWL_MLD_LIMITS(0)
};

static const struct ieee80211_iface_limit iwl_mld_limits_ap[] = {
	IWL_MLD_LIMITS(BIT(NL80211_IFTYPE_AP))
};

static const struct ieee80211_iface_combination
iwl_mld_iface_combinations[] = {
	{
		.num_different_channels = 2,
		.max_interfaces = 4,
		.limits = iwl_mld_limits,
		.n_limits = ARRAY_SIZE(iwl_mld_limits),
	},
	{
		.num_different_channels = 1,
		.max_interfaces = 4,
		.limits = iwl_mld_limits_ap,
		.n_limits = ARRAY_SIZE(iwl_mld_limits_ap),
	},
};

static const u8 if_types_ext_capa_sta[] = {
	 [0] = WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING,
	 [2] = WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT,
	 [7] = WLAN_EXT_CAPA8_OPMODE_NOTIF |
	       WLAN_EXT_CAPA8_MAX_MSDU_IN_AMSDU_LSB,
	 [8] = WLAN_EXT_CAPA9_MAX_MSDU_IN_AMSDU_MSB,
	 [9] = WLAN_EXT_CAPA10_TWT_REQUESTER_SUPPORT,
};

#define IWL_MLD_EMLSR_CAPA	(IEEE80211_EML_CAP_EMLSR_SUPP | \
				 IEEE80211_EML_CAP_EMLSR_PADDING_DELAY_32US << \
					__bf_shf(IEEE80211_EML_CAP_EMLSR_PADDING_DELAY) | \
				 IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY_64US << \
					__bf_shf(IEEE80211_EML_CAP_EMLSR_TRANSITION_DELAY))
#define IWL_MLD_CAPA_OPS (FIELD_PREP_CONST( \
			IEEE80211_MLD_CAP_OP_TID_TO_LINK_MAP_NEG_SUPP, \
			IEEE80211_MLD_CAP_OP_TID_TO_LINK_MAP_NEG_SUPP_SAME) | \
			IEEE80211_MLD_CAP_OP_LINK_RECONF_SUPPORT)

static const struct wiphy_iftype_ext_capab iftypes_ext_capa[] = {
	{
		.iftype = NL80211_IFTYPE_STATION,
		.extended_capabilities = if_types_ext_capa_sta,
		.extended_capabilities_mask = if_types_ext_capa_sta,
		.extended_capabilities_len = sizeof(if_types_ext_capa_sta),
		/* relevant only if EHT is supported */
		.eml_capabilities = IWL_MLD_EMLSR_CAPA,
		.mld_capa_and_ops = IWL_MLD_CAPA_OPS,
	},
};

static void iwl_mld_hw_set_addresses(struct iwl_mld *mld)
{
	struct wiphy *wiphy = mld->wiphy;
	int num_addrs = 1;

	/* Extract MAC address */
	memcpy(mld->addresses[0].addr, mld->nvm_data->hw_addr, ETH_ALEN);
	wiphy->addresses = mld->addresses;
	wiphy->n_addresses = 1;

	/* Extract additional MAC addresses if available */
	if (mld->nvm_data->n_hw_addrs > 1)
		num_addrs = min(mld->nvm_data->n_hw_addrs,
				IWL_MLD_MAX_ADDRESSES);

	for (int i = 1; i < num_addrs; i++) {
		memcpy(mld->addresses[i].addr,
		       mld->addresses[i - 1].addr,
		       ETH_ALEN);
		mld->addresses[i].addr[ETH_ALEN - 1]++;
		wiphy->n_addresses++;
	}
}

static void iwl_mld_hw_set_channels(struct iwl_mld *mld)
{
	struct wiphy *wiphy = mld->wiphy;
	struct ieee80211_supported_band *bands = mld->nvm_data->bands;

	wiphy->bands[NL80211_BAND_2GHZ] = &bands[NL80211_BAND_2GHZ];
	wiphy->bands[NL80211_BAND_5GHZ] = &bands[NL80211_BAND_5GHZ];

	if (bands[NL80211_BAND_6GHZ].n_channels)
		wiphy->bands[NL80211_BAND_6GHZ] = &bands[NL80211_BAND_6GHZ];
}

static void iwl_mld_hw_set_security(struct iwl_mld *mld)
{
	struct ieee80211_hw *hw = mld->hw;
	static const u32 mld_ciphers[] = {
		WLAN_CIPHER_SUITE_WEP40,
		WLAN_CIPHER_SUITE_WEP104,
		WLAN_CIPHER_SUITE_TKIP,
		WLAN_CIPHER_SUITE_CCMP,
		WLAN_CIPHER_SUITE_GCMP,
		WLAN_CIPHER_SUITE_GCMP_256,
		WLAN_CIPHER_SUITE_AES_CMAC,
		WLAN_CIPHER_SUITE_BIP_GMAC_128,
		WLAN_CIPHER_SUITE_BIP_GMAC_256
	};

	hw->wiphy->n_cipher_suites = ARRAY_SIZE(mld_ciphers);
	hw->wiphy->cipher_suites = mld_ciphers;

	ieee80211_hw_set(hw, MFP_CAPABLE);
	wiphy_ext_feature_set(hw->wiphy,
			      NL80211_EXT_FEATURE_BEACON_PROTECTION);
}

static void iwl_mld_hw_set_antennas(struct iwl_mld *mld)
{
	struct wiphy *wiphy = mld->wiphy;

	wiphy->available_antennas_tx = iwl_mld_get_valid_tx_ant(mld);
	wiphy->available_antennas_rx = iwl_mld_get_valid_rx_ant(mld);
}

static void iwl_mld_hw_set_pm(struct iwl_mld *mld)
{
#ifdef CONFIG_PM_SLEEP
	struct wiphy *wiphy = mld->wiphy;

	if (!device_can_wakeup(mld->trans->dev))
		return;

	mld->wowlan.flags |= WIPHY_WOWLAN_MAGIC_PKT |
			     WIPHY_WOWLAN_DISCONNECT |
			     WIPHY_WOWLAN_EAP_IDENTITY_REQ |
			     WIPHY_WOWLAN_RFKILL_RELEASE |
			     WIPHY_WOWLAN_NET_DETECT |
			     WIPHY_WOWLAN_SUPPORTS_GTK_REKEY |
			     WIPHY_WOWLAN_GTK_REKEY_FAILURE |
			     WIPHY_WOWLAN_4WAY_HANDSHAKE;

	mld->wowlan.n_patterns = IWL_WOWLAN_MAX_PATTERNS;
	mld->wowlan.pattern_min_len = IWL_WOWLAN_MIN_PATTERN_LEN;
	mld->wowlan.pattern_max_len = IWL_WOWLAN_MAX_PATTERN_LEN;
	mld->wowlan.max_nd_match_sets = IWL_SCAN_MAX_PROFILES_V2;

	wiphy->wowlan = &mld->wowlan;
#endif /* CONFIG_PM_SLEEP */
}

static void iwl_mac_hw_set_radiotap(struct iwl_mld *mld)
{
	struct ieee80211_hw *hw = mld->hw;

	hw->radiotap_mcs_details |= IEEE80211_RADIOTAP_MCS_HAVE_FEC |
				    IEEE80211_RADIOTAP_MCS_HAVE_STBC;

	hw->radiotap_vht_details |= IEEE80211_RADIOTAP_VHT_KNOWN_STBC |
				    IEEE80211_RADIOTAP_VHT_KNOWN_BEAMFORMED;

	hw->radiotap_timestamp.units_pos =
		IEEE80211_RADIOTAP_TIMESTAMP_UNIT_US |
		IEEE80211_RADIOTAP_TIMESTAMP_SPOS_PLCP_SIG_ACQ;

	/* this is the case for CCK frames, it's better (only 8) for OFDM */
	hw->radiotap_timestamp.accuracy = 22;
}

static void iwl_mac_hw_set_flags(struct iwl_mld *mld)
{
	struct ieee80211_hw *hw = mld->hw;

	ieee80211_hw_set(hw, USES_RSS);
	ieee80211_hw_set(hw, HANDLES_QUIET_CSA);
	ieee80211_hw_set(hw, AP_LINK_PS);
	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, SPECTRUM_MGMT);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(hw, WANT_MONITOR_VIF);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, CONNECTION_MONITOR);
	ieee80211_hw_set(hw, CHANCTX_STA_CSA);
	ieee80211_hw_set(hw, SUPPORT_FAST_XMIT);
	ieee80211_hw_set(hw, SUPPORTS_CLONED_SKBS);
	ieee80211_hw_set(hw, NEEDS_UNIQUE_STA_ADDR);
	ieee80211_hw_set(hw, SUPPORTS_VHT_EXT_NSS_BW);
	ieee80211_hw_set(hw, BUFF_MMPDU_TXQ);
	ieee80211_hw_set(hw, STA_MMPDU_TXQ);
	ieee80211_hw_set(hw, TX_AMSDU);
	ieee80211_hw_set(hw, TX_FRAG_LIST);
	ieee80211_hw_set(hw, TX_AMPDU_SETUP_IN_HW);
	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(hw, SUPPORTS_REORDERING_BUFFER);
	ieee80211_hw_set(hw, DISALLOW_PUNCTURING_5GHZ);
	ieee80211_hw_set(hw, SINGLE_SCAN_ON_ALL_BANDS);
	ieee80211_hw_set(hw, SUPPORTS_AMSDU_IN_AMPDU);
	ieee80211_hw_set(hw, TDLS_WIDER_BW);
}

static void iwl_mac_hw_set_wiphy(struct iwl_mld *mld)
{
	struct ieee80211_hw *hw = mld->hw;
	struct wiphy *wiphy = hw->wiphy;
	const struct iwl_ucode_capabilities *ucode_capa = &mld->fw->ucode_capa;

	snprintf(wiphy->fw_version,
		 sizeof(wiphy->fw_version),
		 "%.31s", mld->fw->fw_version);

	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				 BIT(NL80211_IFTYPE_P2P_CLIENT) |
				 BIT(NL80211_IFTYPE_AP) |
				 BIT(NL80211_IFTYPE_P2P_GO) |
				 BIT(NL80211_IFTYPE_P2P_DEVICE) |
				 BIT(NL80211_IFTYPE_ADHOC);

	wiphy->features |= NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR |
			   NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR |
			   NL80211_FEATURE_ND_RANDOM_MAC_ADDR |
			   NL80211_FEATURE_HT_IBSS |
			   NL80211_FEATURE_P2P_GO_CTWIN |
			   NL80211_FEATURE_LOW_PRIORITY_SCAN |
			   NL80211_FEATURE_P2P_GO_OPPPS |
			   NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE |
			   NL80211_FEATURE_SUPPORTS_WMM_ADMISSION |
			   NL80211_FEATURE_TX_POWER_INSERTION |
			   NL80211_FEATURE_DS_PARAM_SET_IE_IN_PROBES;

	wiphy->flags |= WIPHY_FLAG_IBSS_RSN |
			WIPHY_FLAG_AP_UAPSD |
			WIPHY_FLAG_HAS_CHANNEL_SWITCH |
			WIPHY_FLAG_SPLIT_SCAN_6GHZ |
			WIPHY_FLAG_SUPPORTS_TDLS |
			WIPHY_FLAG_SUPPORTS_EXT_KEK_KCK;

	if (mld->nvm_data->sku_cap_11be_enable &&
	    !iwlwifi_mod_params.disable_11ax &&
	    !iwlwifi_mod_params.disable_11be)
		wiphy->flags |= WIPHY_FLAG_SUPPORTS_MLO;

	/* the firmware uses u8 for num of iterations, but 0xff is saved for
	 * infinite loop, so the maximum number of iterations is actually 254.
	 */
	wiphy->max_sched_scan_plan_iterations = 254;
	wiphy->max_sched_scan_ie_len = iwl_mld_scan_max_template_size();
	wiphy->max_scan_ie_len = iwl_mld_scan_max_template_size();
	wiphy->max_sched_scan_ssids = PROBE_OPTION_MAX;
	wiphy->max_scan_ssids = PROBE_OPTION_MAX;
	wiphy->max_sched_scan_plans = IWL_MAX_SCHED_SCAN_PLANS;
	wiphy->max_sched_scan_reqs = 1;
	wiphy->max_sched_scan_plan_interval = U16_MAX;
	wiphy->max_match_sets = IWL_SCAN_MAX_PROFILES_V2;

	wiphy->max_remain_on_channel_duration = 10000;

	wiphy->hw_version = mld->trans->hw_id;

	wiphy->hw_timestamp_max_peers = 1;

	wiphy->iface_combinations = iwl_mld_iface_combinations;
	wiphy->n_iface_combinations = ARRAY_SIZE(iwl_mld_iface_combinations);

	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_VHT_IBSS);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_DFS_CONCURRENT);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_LEGACY);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_SCAN_START_TIME);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BSS_PARENT_TSF);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_SCAN_MIN_PREQ_CONTENT);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_ACCEPT_BCAST_PROBE_RESP);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_FILS_MAX_CHANNEL_TIME);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_OCE_PROBE_REQ_HIGH_TX_RATE);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_MU_MIMO_AIR_SNIFFER);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_SPP_AMSDU_SUPPORT);

	if (fw_has_capa(ucode_capa, IWL_UCODE_TLV_CAPA_PROTECTED_TWT))
		wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_PROTECTED_TWT);

	wiphy->iftype_ext_capab = NULL;
	wiphy->num_iftype_ext_capab = 0;

	if (!iwlwifi_mod_params.disable_11ax) {
		wiphy->iftype_ext_capab = iftypes_ext_capa;
		wiphy->num_iftype_ext_capab = ARRAY_SIZE(iftypes_ext_capa);

		ieee80211_hw_set(hw, SUPPORTS_MULTI_BSSID);
		ieee80211_hw_set(hw, SUPPORTS_ONLY_HE_MULTI_BSSID);
	}

	if (iwlmld_mod_params.power_scheme != IWL_POWER_SCHEME_CAM)
		wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
	else
		wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;
}

static void iwl_mac_hw_set_misc(struct iwl_mld *mld)
{
	struct ieee80211_hw *hw = mld->hw;

	hw->queues = IEEE80211_NUM_ACS;

	hw->netdev_features = NETIF_F_HIGHDMA | NETIF_F_SG;
	hw->netdev_features |= mld->cfg->features;

	hw->max_tx_fragments = mld->trans->max_skb_frags;
	hw->max_listen_interval = IWL_MLD_CONN_LISTEN_INTERVAL;

	hw->uapsd_max_sp_len = IEEE80211_WMM_IE_STA_QOSINFO_SP_ALL;
	hw->uapsd_queues = IEEE80211_WMM_IE_STA_QOSINFO_AC_VO |
			   IEEE80211_WMM_IE_STA_QOSINFO_AC_VI |
			   IEEE80211_WMM_IE_STA_QOSINFO_AC_BK |
			   IEEE80211_WMM_IE_STA_QOSINFO_AC_BE;

	hw->chanctx_data_size = sizeof(struct iwl_mld_phy);
	hw->vif_data_size = sizeof(struct iwl_mld_vif);
	hw->sta_data_size = sizeof(struct iwl_mld_sta);
	hw->txq_data_size = sizeof(struct iwl_mld_txq);

	/* TODO: Remove this division when IEEE80211_MAX_AMPDU_BUF_EHT size
	 * is supported.
	 * Note: ensure that IWL_DEFAULT_QUEUE_SIZE_EHT is updated accordingly.
	 */
	hw->max_rx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF_EHT / 2;
}

static int iwl_mld_hw_verify_preconditions(struct iwl_mld *mld)
{
	/* 11ax is expected to be enabled for all supported devices */
	if (WARN_ON(!mld->nvm_data->sku_cap_11ax_enable))
		return -EINVAL;

	/* LAR is expected to be enabled for all supported devices */
	if (WARN_ON(!mld->nvm_data->lar_enabled))
		return -EINVAL;

	/* All supported devices are currently using version 3 of the cmd.
	 * Since version 3, IWL_SCAN_MAX_PROFILES_V2 shall be used where
	 * necessary.
	 */
	if (WARN_ON(iwl_fw_lookup_cmd_ver(mld->fw,
					  SCAN_OFFLOAD_UPDATE_PROFILES_CMD,
					  IWL_FW_CMD_VER_UNKNOWN) != 3))
		return -EINVAL;

	return 0;
}

int iwl_mld_register_hw(struct iwl_mld *mld)
{
	/* verify once essential preconditions required for setting
	 * the hw capabilities
	 */
	if (iwl_mld_hw_verify_preconditions(mld))
		return -EINVAL;

	iwl_mld_hw_set_addresses(mld);
	iwl_mld_hw_set_channels(mld);
	iwl_mld_hw_set_security(mld);
	iwl_mld_hw_set_pm(mld);
	iwl_mld_hw_set_antennas(mld);
	iwl_mac_hw_set_radiotap(mld);
	iwl_mac_hw_set_flags(mld);
	iwl_mac_hw_set_wiphy(mld);
	iwl_mac_hw_set_misc(mld);

	SET_IEEE80211_DEV(mld->hw, mld->trans->dev);

	return ieee80211_register_hw(mld->hw);
}

static void
iwl_mld_mac80211_tx(struct ieee80211_hw *hw,
		    struct ieee80211_tx_control *control, struct sk_buff *skb)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct ieee80211_sta *sta = control->sta;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	u32 link_id = u32_get_bits(info->control.flags,
				   IEEE80211_TX_CTRL_MLO_LINK);

	/* In AP mode, mgmt frames are sent on the bcast station,
	 * so the FW can't translate the MLD addr to the link addr. Do it here
	 */
	if (ieee80211_is_mgmt(hdr->frame_control) && sta &&
	    link_id != IEEE80211_LINK_UNSPECIFIED &&
	    !ieee80211_is_probe_resp(hdr->frame_control)) {
		/* translate MLD addresses to LINK addresses */
		struct ieee80211_link_sta *link_sta =
			rcu_dereference(sta->link[link_id]);
		struct ieee80211_bss_conf *link_conf =
			rcu_dereference(info->control.vif->link_conf[link_id]);
		struct ieee80211_mgmt *mgmt;

		if (WARN_ON(!link_sta || !link_conf)) {
			ieee80211_free_txskb(hw, skb);
			return;
		}

		mgmt = (void *)hdr;
		memcpy(mgmt->da, link_sta->addr, ETH_ALEN);
		memcpy(mgmt->sa, link_conf->addr, ETH_ALEN);
		memcpy(mgmt->bssid, link_conf->bssid, ETH_ALEN);
	}

	iwl_mld_tx_skb(mld, skb, NULL);
}

static void
iwl_mld_restart_cleanup(struct iwl_mld *mld)
{
	iwl_cleanup_mld(mld);

	ieee80211_iterate_interfaces(mld->hw, IEEE80211_IFACE_ITER_ACTIVE,
				     iwl_mld_cleanup_vif, NULL);

	ieee80211_iterate_stations_atomic(mld->hw,
					  iwl_mld_cleanup_sta, NULL);

	iwl_mld_ftm_restart_cleanup(mld);
}

static
int iwl_mld_mac80211_start(struct ieee80211_hw *hw)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	int ret;
	bool in_d3 = false;

	lockdep_assert_wiphy(mld->wiphy);

#ifdef CONFIG_PM_SLEEP
	/* Unless the host goes into hibernate the FW always stays on and
	 * the d3_resume flow is used. When wowlan is configured, mac80211
	 * would call it's resume callback and the wowlan_resume flow
	 * would be used.
	 */

	in_d3 = mld->fw_status.in_d3;
	if (in_d3) {
		/* mac80211 already cleaned up the state, no need for cleanup */
		ret = iwl_mld_no_wowlan_resume(mld);
		if (ret)
			iwl_mld_stop_fw(mld);
	}
#endif /* CONFIG_PM_SLEEP */

	if (mld->fw_status.in_hw_restart) {
		iwl_mld_stop_fw(mld);
		iwl_mld_restart_cleanup(mld);
	}

	if (!in_d3 || ret) {
		ret = iwl_mld_start_fw(mld);
		if (ret)
			goto error;
	}

	mld->scan.last_start_time_jiffies = jiffies;

	iwl_dbg_tlv_time_point(&mld->fwrt, IWL_FW_INI_TIME_POINT_POST_INIT,
			       NULL);
	iwl_dbg_tlv_time_point(&mld->fwrt, IWL_FW_INI_TIME_POINT_PERIODIC,
			       NULL);

	return 0;

error:
	/* If we failed to restart the hw, there is nothing useful
	 * we can do but indicate we are no longer in restart.
	 */
	mld->fw_status.in_hw_restart = false;

	return ret;
}

static
void iwl_mld_mac80211_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	lockdep_assert_wiphy(mld->wiphy);

	wiphy_work_cancel(mld->wiphy, &mld->add_txqs_wk);

	/* if the suspend flow fails the fw is in error. Stop it here, and it
	 * will be started upon wakeup
	 */
	if (!suspend || iwl_mld_no_wowlan_suspend(mld))
		iwl_mld_stop_fw(mld);

	/* HW is stopped, no more coming RX. OTOH, the worker can't run as the
	 * wiphy lock is held. Cancel it in case it was scheduled just before
	 * we stopped the HW.
	 */
	wiphy_work_cancel(mld->wiphy, &mld->async_handlers_wk);

	/* Empty out the list, as the worker won't do that */
	iwl_mld_purge_async_handlers_list(mld);

	/* Clear in_hw_restart flag when stopping the hw, as mac80211 won't
	 * execute the restart.
	 */
	mld->fw_status.in_hw_restart = false;

	/* We shouldn't have any UIDs still set. Loop over all the UIDs to
	 * make sure there's nothing left there and warn if any is found.
	 */
	for (int i = 0; i < ARRAY_SIZE(mld->scan.uid_status); i++)
		if (WARN_ONCE(mld->scan.uid_status[i],
			      "UMAC scan UID %d status was not cleaned (0x%x 0x%x)\n",
			      i, mld->scan.uid_status[i], mld->scan.status))
			mld->scan.uid_status[i] = 0;
}

static
int iwl_mld_mac80211_config(struct ieee80211_hw *hw, u32 changed)
{
	return 0;
}

static
int iwl_mld_mac80211_add_interface(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	/* Construct mld_vif, add it to fw, and map its ID to ieee80211_vif */
	ret = iwl_mld_add_vif(mld, vif);
	if (ret)
		return ret;

	/*
	 * Add the default link, but not if this is an MLD vif as that implies
	 * the HW is restarting and it will be configured by change_vif_links.
	 */
	if (!ieee80211_vif_is_mld(vif))
		ret = iwl_mld_add_link(mld, &vif->bss_conf);
	if (ret)
		goto err;

	if (vif->type == NL80211_IFTYPE_STATION) {
		vif->driver_flags |= IEEE80211_VIF_REMOVE_AP_AFTER_DISASSOC;
		if (!vif->p2p)
			vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER |
					     IEEE80211_VIF_SUPPORTS_CQM_RSSI;
	}

	if (vif->p2p || iwl_fw_lookup_cmd_ver(mld->fw, PHY_CONTEXT_CMD, 0) < 5)
		vif->driver_flags |= IEEE80211_VIF_IGNORE_OFDMA_WIDER_BW;

	/*
	 * For an MLD vif (in restart) we may not have a link; delay the call
	 * the initial change_vif_links.
	 */
	if (vif->type == NL80211_IFTYPE_STATION &&
	    !ieee80211_vif_is_mld(vif))
		iwl_mld_update_mac_power(mld, vif, false);

	if (vif->type == NL80211_IFTYPE_MONITOR) {
		mld->monitor.on = true;
		ieee80211_hw_set(mld->hw, RX_INCLUDES_FCS);
	}

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE)
		mld->p2p_device_vif = vif;

	return 0;

err:
	iwl_mld_rm_vif(mld, vif);
	return ret;
}

static
void iwl_mld_mac80211_remove_interface(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	lockdep_assert_wiphy(mld->wiphy);

	if (ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_STATION)
		vif->driver_flags &= ~(IEEE80211_VIF_BEACON_FILTER |
				       IEEE80211_VIF_SUPPORTS_CQM_RSSI);

	if (vif->type == NL80211_IFTYPE_MONITOR) {
		__clear_bit(IEEE80211_HW_RX_INCLUDES_FCS, mld->hw->flags);
		mld->monitor.on = false;
	}

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE)
		mld->p2p_device_vif = NULL;

	iwl_mld_remove_link(mld, &vif->bss_conf);

#ifdef CONFIG_IWLWIFI_DEBUGFS
	debugfs_remove(iwl_mld_vif_from_mac80211(vif)->dbgfs_slink);
#endif

	iwl_mld_rm_vif(mld, vif);
}

struct iwl_mld_mc_iter_data {
	struct iwl_mld *mld;
	int port_id;
};

static void iwl_mld_mc_iface_iterator(void *data, u8 *mac,
				      struct ieee80211_vif *vif)
{
	struct iwl_mld_mc_iter_data *mc_data = data;
	struct iwl_mld *mld = mc_data->mld;
	struct iwl_mcast_filter_cmd *cmd = mld->mcast_filter_cmd;
	struct iwl_host_cmd hcmd = {
		.id = MCAST_FILTER_CMD,
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
	};
	int ret, len;

	/* If we don't have free ports, mcast frames will be dropped */
	if (WARN_ON_ONCE(mc_data->port_id >= MAX_PORT_ID_NUM))
		return;

	if (vif->type != NL80211_IFTYPE_STATION || !vif->cfg.assoc)
		return;

	cmd->port_id = mc_data->port_id++;
	ether_addr_copy(cmd->bssid, vif->bss_conf.bssid);
	len = roundup(sizeof(*cmd) + cmd->count * ETH_ALEN, 4);

	hcmd.len[0] = len;
	hcmd.data[0] = cmd;

	ret = iwl_mld_send_cmd(mld, &hcmd);
	if (ret)
		IWL_ERR(mld, "mcast filter cmd error. ret=%d\n", ret);
}

void iwl_mld_recalc_multicast_filter(struct iwl_mld *mld)
{
	struct iwl_mld_mc_iter_data iter_data = {
		.mld = mld,
	};

	if (WARN_ON_ONCE(!mld->mcast_filter_cmd))
		return;

	ieee80211_iterate_active_interfaces(mld->hw,
					    IEEE80211_IFACE_ITER_NORMAL,
					    iwl_mld_mc_iface_iterator,
					    &iter_data);
}

static u64
iwl_mld_mac80211_prepare_multicast(struct ieee80211_hw *hw,
				   struct netdev_hw_addr_list *mc_list)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mcast_filter_cmd *cmd;
	struct netdev_hw_addr *addr;
	int addr_count = netdev_hw_addr_list_count(mc_list);
	bool pass_all = addr_count > MAX_MCAST_FILTERING_ADDRESSES;
	int len;

	if (pass_all)
		addr_count = 0;

	/* len must be a multiple of 4 */
	len = roundup(sizeof(*cmd) + addr_count * ETH_ALEN, 4);
	cmd = kzalloc(len, GFP_ATOMIC);
	if (!cmd)
		return 0;

	if (pass_all) {
		cmd->pass_all = 1;
		goto out;
	}

	netdev_hw_addr_list_for_each(addr, mc_list) {
		IWL_DEBUG_MAC80211(mld, "mcast addr (%d): %pM\n",
				   cmd->count, addr->addr);
		ether_addr_copy(&cmd->addr_list[cmd->count * ETH_ALEN],
				addr->addr);
		cmd->count++;
	}

out:
	return (u64)(unsigned long)cmd;
}

static
void iwl_mld_mac80211_configure_filter(struct ieee80211_hw *hw,
				       unsigned int changed_flags,
				       unsigned int *total_flags,
				       u64 multicast)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mcast_filter_cmd *cmd = (void *)(unsigned long)multicast;

	/* Replace previous configuration */
	kfree(mld->mcast_filter_cmd);
	mld->mcast_filter_cmd = cmd;

	if (!cmd)
		goto out;

	if (changed_flags & FIF_ALLMULTI)
		cmd->pass_all = !!(*total_flags & FIF_ALLMULTI);

	if (cmd->pass_all)
		cmd->count = 0;

	iwl_mld_recalc_multicast_filter(mld);
out:
	*total_flags = 0;
}

static
void iwl_mld_mac80211_wake_tx_queue(struct ieee80211_hw *hw,
				    struct ieee80211_txq *txq)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_txq *mld_txq = iwl_mld_txq_from_mac80211(txq);

	if (likely(mld_txq->status.allocated) || !txq->sta) {
		iwl_mld_tx_from_txq(mld, txq);
		return;
	}

	/* We don't support TSPEC tids. %IEEE80211_NUM_TIDS is for mgmt */
	if (txq->tid != IEEE80211_NUM_TIDS && txq->tid >= IWL_MAX_TID_COUNT) {
		IWL_DEBUG_MAC80211(mld, "TID %d is not supported\n", txq->tid);
		return;
	}

	/* The worker will handle any packets we leave on the txq now */

	spin_lock_bh(&mld->add_txqs_lock);
	/* The list is being deleted only after the queue is fully allocated. */
	if (list_empty(&mld_txq->list) &&
	    /* recheck under lock, otherwise it can be added twice */
	    !mld_txq->status.allocated) {
		list_add_tail(&mld_txq->list, &mld->txqs_to_add);
		wiphy_work_queue(mld->wiphy, &mld->add_txqs_wk);
	}
	spin_unlock_bh(&mld->add_txqs_lock);
}

static void iwl_mld_teardown_tdls_peers(struct iwl_mld *mld)
{
	lockdep_assert_wiphy(mld->wiphy);

	for (int i = 0; i < mld->fw->ucode_capa.num_stations; i++) {
		struct ieee80211_link_sta *link_sta;
		struct iwl_mld_sta *mld_sta;

		link_sta = wiphy_dereference(mld->wiphy,
					     mld->fw_id_to_link_sta[i]);
		if (IS_ERR_OR_NULL(link_sta))
			continue;

		if (!link_sta->sta->tdls)
			continue;

		mld_sta = iwl_mld_sta_from_mac80211(link_sta->sta);

		ieee80211_tdls_oper_request(mld_sta->vif, link_sta->addr,
					    NL80211_TDLS_TEARDOWN,
					    WLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED,
					    GFP_KERNEL);
	}
}

static
int iwl_mld_add_chanctx(struct ieee80211_hw *hw,
			struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_phy *phy = iwl_mld_phy_from_mac80211(ctx);
	int fw_id = iwl_mld_allocate_fw_phy_id(mld);
	int ret;

	if (fw_id < 0)
		return fw_id;

	phy->mld = mld;
	phy->fw_id = fw_id;
	phy->chandef = *iwl_mld_get_chandef_from_chanctx(mld, ctx);

	ret = iwl_mld_phy_fw_action(mld, ctx, FW_CTXT_ACTION_ADD);
	if (ret) {
		mld->used_phy_ids &= ~BIT(phy->fw_id);
		return ret;
	}

	if (hweight8(mld->used_phy_ids) > 1)
		iwl_mld_teardown_tdls_peers(mld);

	return 0;
}

static
void iwl_mld_remove_chanctx(struct ieee80211_hw *hw,
			    struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_phy *phy = iwl_mld_phy_from_mac80211(ctx);

	iwl_mld_phy_fw_action(mld, ctx, FW_CTXT_ACTION_REMOVE);
	mld->used_phy_ids &= ~BIT(phy->fw_id);
}

static
void iwl_mld_change_chanctx(struct ieee80211_hw *hw,
			    struct ieee80211_chanctx_conf *ctx, u32 changed)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_phy *phy = iwl_mld_phy_from_mac80211(ctx);
	struct cfg80211_chan_def *chandef =
		iwl_mld_get_chandef_from_chanctx(mld, ctx);

	/* We don't care about these */
	if (!(changed & ~(IEEE80211_CHANCTX_CHANGE_RX_CHAINS |
			  IEEE80211_CHANCTX_CHANGE_RADAR |
			  IEEE80211_CHANCTX_CHANGE_CHANNEL)))
		return;

	/* Check if a FW update is required */

	if (changed & IEEE80211_CHANCTX_CHANGE_AP)
		goto update;

	if (chandef->chan == phy->chandef.chan &&
	    chandef->center_freq1 == phy->chandef.center_freq1 &&
	    chandef->punctured == phy->chandef.punctured) {
		/* Check if we are toggling between HT and non-HT, no-op */
		if (phy->chandef.width == chandef->width ||
		    (phy->chandef.width <= NL80211_CHAN_WIDTH_20 &&
		     chandef->width <= NL80211_CHAN_WIDTH_20))
			return;
	}
update:
	phy->chandef = *chandef;

	iwl_mld_phy_fw_action(mld, ctx, FW_CTXT_ACTION_MODIFY);
}

static u8
iwl_mld_chandef_get_primary_80(struct cfg80211_chan_def *chandef)
{
	int data_start;
	int control_start;
	int bw;

	if (chandef->width == NL80211_CHAN_WIDTH_320)
		bw = 320;
	else if (chandef->width == NL80211_CHAN_WIDTH_160)
		bw = 160;
	else
		return 0;

	/* data is bw wide so the start is half the width */
	data_start = chandef->center_freq1 - bw / 2;
	/* control is 20Mhz width */
	control_start = chandef->chan->center_freq - 10;

	return (control_start - data_start) / 80;
}

static bool iwl_mld_can_activate_link(struct iwl_mld *mld,
				      struct ieee80211_vif *vif,
				      struct ieee80211_bss_conf *link)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_sta *mld_sta;
	struct iwl_mld_link_sta *link_sta;

	/* In association, we activate the assoc link before adding the STA. */
	if (!mld_vif->ap_sta || !vif->cfg.assoc)
		return true;

	mld_sta = iwl_mld_sta_from_mac80211(mld_vif->ap_sta);

	/* When switching links, we need to wait with the activation until the
	 * STA was added to the FW. It'll be activated in
	 * iwl_mld_update_link_stas
	 */
	link_sta = wiphy_dereference(mld->wiphy, mld_sta->link[link->link_id]);

	/* In restart we can have a link_sta that doesn't exist in FW yet */
	return link_sta && link_sta->in_fw;
}

static
int iwl_mld_assign_vif_chanctx(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_bss_conf *link,
			       struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link);
	unsigned int n_active = iwl_mld_count_active_links(mld, vif);
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(!mld_link))
		return -EINVAL;

	/* if the assigned one was not counted yet, count it now */
	if (!rcu_access_pointer(mld_link->chan_ctx)) {
		n_active++;

		/* Track addition of non-BSS link */
		if (ieee80211_vif_type_p2p(vif) != NL80211_IFTYPE_STATION) {
			ret = iwl_mld_emlsr_check_non_bss_block(mld, 1);
			if (ret)
				return ret;
		}
	}

	/* for AP, mac parameters such as HE support are updated at this stage. */
	if (vif->type == NL80211_IFTYPE_AP) {
		ret = iwl_mld_mac_fw_action(mld, vif, FW_CTXT_ACTION_MODIFY);

		if (ret) {
			IWL_ERR(mld, "failed to update MAC %pM\n", vif->addr);
			return -EINVAL;
		}
	}

	rcu_assign_pointer(mld_link->chan_ctx, ctx);

	if (n_active > 1) {
		struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

		iwl_mld_leave_omi_bw_reduction(mld);

		/* Indicate to mac80211 that EML is enabled */
		vif->driver_flags |= IEEE80211_VIF_EML_ACTIVE;

		if (vif->active_links & BIT(mld_vif->emlsr.selected_links))
			mld_vif->emlsr.primary = mld_vif->emlsr.selected_primary;
		else
			mld_vif->emlsr.primary = __ffs(vif->active_links);

		iwl_dbg_tlv_time_point(&mld->fwrt, IWL_FW_INI_TIME_ESR_LINK_UP,
				       NULL);
	}

	/* First send the link command with the phy context ID.
	 * Now that we have the phy, we know the band so also the rates
	 */
	ret = iwl_mld_change_link_in_fw(mld, link,
					LINK_CONTEXT_MODIFY_RATES_INFO);
	if (ret)
		goto err;

	/* TODO: Initialize rate control for the AP station, since we might be
	 * doing a link switch here - we cannot initialize it before since
	 * this needs the phy context assigned (and in FW?), and we cannot
	 * do it later because it needs to be initialized as soon as we're
	 * able to TX on the link, i.e. when active. (task=link-switch)
	 */

	/* Now activate the link */
	if (iwl_mld_can_activate_link(mld, vif, link)) {
		ret = iwl_mld_activate_link(mld, link);
		if (ret)
			goto err;
	}

	if (vif->type == NL80211_IFTYPE_STATION)
		iwl_mld_send_ap_tx_power_constraint_cmd(mld, vif, link);

	if (vif->type == NL80211_IFTYPE_MONITOR) {
		/* TODO: task=sniffer add sniffer station */
		mld->monitor.p80 =
			iwl_mld_chandef_get_primary_80(&vif->bss_conf.chanreq.oper);
	}

	return 0;
err:
	RCU_INIT_POINTER(mld_link->chan_ctx, NULL);
	return ret;
}

static
void iwl_mld_unassign_vif_chanctx(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_bss_conf *link,
				  struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link);
	unsigned int n_active = iwl_mld_count_active_links(mld, vif);

	if (WARN_ON(!mld_link))
		return;

	/* Track removal of non-BSS link */
	if (ieee80211_vif_type_p2p(vif) != NL80211_IFTYPE_STATION)
		iwl_mld_emlsr_check_non_bss_block(mld, -1);

	iwl_mld_deactivate_link(mld, link);

	/* TODO: task=sniffer remove sniffer station */

	if (n_active > 1) {
		/* Indicate to mac80211 that EML is disabled */
		vif->driver_flags &= ~IEEE80211_VIF_EML_ACTIVE;

		iwl_dbg_tlv_time_point(&mld->fwrt,
				       IWL_FW_INI_TIME_ESR_LINK_DOWN,
				       NULL);
	}

	RCU_INIT_POINTER(mld_link->chan_ctx, NULL);

	/* in the non-MLO case, remove/re-add the link to clean up FW state.
	 * In MLO, it'll be done in drv_change_vif_link
	 */
	if (!ieee80211_vif_is_mld(vif) && !mld_vif->ap_sta &&
	    !WARN_ON_ONCE(vif->cfg.assoc) &&
	    vif->type != NL80211_IFTYPE_AP && !mld->fw_status.in_hw_restart) {
		iwl_mld_remove_link(mld, link);
		iwl_mld_add_link(mld, link);
	}
}

static
int iwl_mld_mac80211_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	return 0;
}

static void
iwl_mld_link_info_changed_ap_ibss(struct iwl_mld *mld,
				  struct ieee80211_vif *vif,
				  struct ieee80211_bss_conf *link,
				  u64 changes)
{
	u32 link_changes = 0;

	if (changes & BSS_CHANGED_ERP_SLOT)
		link_changes |= LINK_CONTEXT_MODIFY_RATES_INFO;

	if (changes & (BSS_CHANGED_ERP_CTS_PROT | BSS_CHANGED_HT))
		link_changes |= LINK_CONTEXT_MODIFY_PROTECT_FLAGS;

	if (changes & (BSS_CHANGED_QOS | BSS_CHANGED_BANDWIDTH))
		link_changes |= LINK_CONTEXT_MODIFY_QOS_PARAMS;

	if (changes & BSS_CHANGED_HE_BSS_COLOR)
		link_changes |= LINK_CONTEXT_MODIFY_HE_PARAMS;

	if (link_changes)
		iwl_mld_change_link_in_fw(mld, link, link_changes);

	if (changes & BSS_CHANGED_BEACON)
		iwl_mld_update_beacon_template(mld, vif, link);
}

static
u32 iwl_mld_link_changed_mapping(struct iwl_mld *mld,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *link_conf,
				 u64 changes)
{
	u32 link_changes = 0;
	bool has_he, has_eht;

	if (changes & BSS_CHANGED_QOS && vif->cfg.assoc && link_conf->qos)
		link_changes |= LINK_CONTEXT_MODIFY_QOS_PARAMS;

	if (changes & (BSS_CHANGED_ERP_PREAMBLE | BSS_CHANGED_BASIC_RATES |
		       BSS_CHANGED_ERP_SLOT))
		link_changes |= LINK_CONTEXT_MODIFY_RATES_INFO;

	if (changes & (BSS_CHANGED_HT | BSS_CHANGED_ERP_CTS_PROT))
		link_changes |= LINK_CONTEXT_MODIFY_PROTECT_FLAGS;

	/* TODO: task=MLO check mac80211's HE flags and if command is needed
	 * every time there's a link change. Currently used flags are
	 * BSS_CHANGED_HE_OBSS_PD and BSS_CHANGED_HE_BSS_COLOR.
	 */
	has_he = link_conf->he_support && !iwlwifi_mod_params.disable_11ax;
	has_eht = link_conf->eht_support && !iwlwifi_mod_params.disable_11be;

	if (vif->cfg.assoc && (has_he || has_eht)) {
		IWL_DEBUG_MAC80211(mld, "Associated in HE mode\n");
		link_changes |= LINK_CONTEXT_MODIFY_HE_PARAMS;
	}

	return link_changes;
}

static void
iwl_mld_mac80211_link_info_changed_sta(struct iwl_mld *mld,
				       struct ieee80211_vif *vif,
				       struct ieee80211_bss_conf *link_conf,
				       u64 changes)
{
	u32 link_changes = iwl_mld_link_changed_mapping(mld, vif, link_conf,
							changes);

	if (link_changes)
		iwl_mld_change_link_in_fw(mld, link_conf, link_changes);

	if (changes & BSS_CHANGED_TPE)
		iwl_mld_send_ap_tx_power_constraint_cmd(mld, vif, link_conf);

	if (changes & BSS_CHANGED_BEACON_INFO)
		iwl_mld_update_mac_power(mld, vif, false);

	/* The firmware will wait quite a while after association before it
	 * starts filtering the beacons. We can safely enable beacon filtering
	 * upon CQM configuration, even if we didn't get a beacon yet.
	 */
	if (changes & (BSS_CHANGED_CQM | BSS_CHANGED_BEACON_INFO))
		iwl_mld_enable_beacon_filter(mld, link_conf, false);

	/* If we have used OMI before to reduce bandwidth to 80 MHz and then
	 * increased to 160 MHz again, and then the AP changes to 320 MHz, it
	 * will think that we're limited to 160 MHz right now. Update it by
	 * requesting a new OMI bandwidth.
	 */
	if (changes & BSS_CHANGED_BANDWIDTH) {
		enum ieee80211_sta_rx_bandwidth bw;

		bw = ieee80211_chan_width_to_rx_bw(link_conf->chanreq.oper.width);

		iwl_mld_omi_ap_changed_bw(mld, link_conf, bw);

	}

	if (changes & BSS_CHANGED_BANDWIDTH)
		iwl_mld_retry_emlsr(mld, vif);
}

static int iwl_mld_update_mu_groups(struct iwl_mld *mld,
				    struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mu_group_mgmt_cmd cmd = {};

	BUILD_BUG_ON(sizeof(cmd.membership_status) !=
		     sizeof(link_conf->mu_group.membership));
	BUILD_BUG_ON(sizeof(cmd.user_position) !=
		     sizeof(link_conf->mu_group.position));

	memcpy(cmd.membership_status, link_conf->mu_group.membership,
	       WLAN_MEMBERSHIP_LEN);
	memcpy(cmd.user_position, link_conf->mu_group.position,
	       WLAN_USER_POSITION_LEN);

	return iwl_mld_send_cmd_pdu(mld,
				    WIDE_ID(DATA_PATH_GROUP,
					    UPDATE_MU_GROUPS_CMD),
				    &cmd);
}

static void
iwl_mld_mac80211_link_info_changed(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *link_conf,
				   u64 changes)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		iwl_mld_mac80211_link_info_changed_sta(mld, vif, link_conf,
						       changes);
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
		iwl_mld_link_info_changed_ap_ibss(mld, vif, link_conf,
						  changes);
		break;
	case NL80211_IFTYPE_MONITOR:
		/* The firmware tracks this on its own in STATION mode, but
		 * obviously not in sniffer mode.
		 */
		if (changes & BSS_CHANGED_MU_GROUPS)
			iwl_mld_update_mu_groups(mld, link_conf);
		break;
	default:
		/* shouldn't happen */
		WARN_ON_ONCE(1);
	}

	/* We now know our BSSID, we can configure the MAC context with
	 * eht_support if needed.
	 */
	if (changes & BSS_CHANGED_BSSID)
		iwl_mld_mac_fw_action(mld, vif, FW_CTXT_ACTION_MODIFY);

	if (changes & BSS_CHANGED_TXPOWER)
		iwl_mld_set_tx_power(mld, link_conf, link_conf->txpower);
}

static void
iwl_mld_smps_wa(struct iwl_mld *mld, struct ieee80211_vif *vif, bool enable)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

	/* Send the device-level power commands since the
	 * firmware checks the POWER_TABLE_CMD's POWER_SAVE_EN bit to
	 * determine SMPS mode.
	 */
	if (mld_vif->ps_disabled == !enable)
		return;

	mld_vif->ps_disabled = !enable;

	iwl_mld_update_device_power(mld, false);
}

static
void iwl_mld_mac80211_vif_cfg_changed(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      u64 changes)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	if (changes & BSS_CHANGED_ASSOC) {
		ret = iwl_mld_mac_fw_action(mld, vif, FW_CTXT_ACTION_MODIFY);
		if (ret)
			IWL_ERR(mld, "failed to update context\n");

		if (vif->cfg.assoc) {
			/* Clear statistics to get clean beacon counter, and
			 * ask for periodic statistics, as they are needed for
			 * link selection and RX OMI decisions.
			 */
			iwl_mld_clear_stats_in_fw(mld);
			iwl_mld_request_periodic_fw_stats(mld, true);

			iwl_mld_set_vif_associated(mld, vif);
		} else {
			iwl_mld_request_periodic_fw_stats(mld, false);
		}
	}

	if (changes & BSS_CHANGED_PS) {
		iwl_mld_smps_wa(mld, vif, vif->cfg.ps);
		iwl_mld_update_mac_power(mld, vif, false);
	}

	/* TODO: task=MLO BSS_CHANGED_MLD_VALID_LINKS/CHANGED_MLD_TTLM */
}

static int
iwl_mld_mac80211_hw_scan(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 struct ieee80211_scan_request *hw_req)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	if (WARN_ON(!hw_req->req.n_channels ||
		    hw_req->req.n_channels >
		    mld->fw->ucode_capa.n_scan_channels))
		return -EINVAL;

	return iwl_mld_regular_scan_start(mld, vif, &hw_req->req, &hw_req->ies);
}

static void
iwl_mld_mac80211_cancel_hw_scan(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	/* Due to a race condition, it's possible that mac80211 asks
	 * us to stop a hw_scan when it's already stopped. This can
	 * happen, for instance, if we stopped the scan ourselves,
	 * called ieee80211_scan_completed() and the userspace called
	 * cancel scan before ieee80211_scan_work() could run.
	 * To handle that, simply return if the scan is not running.
	 */
	if (mld->scan.status & IWL_MLD_SCAN_REGULAR)
		iwl_mld_scan_stop(mld, IWL_MLD_SCAN_REGULAR, true);
}

static int
iwl_mld_mac80211_sched_scan_start(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct cfg80211_sched_scan_request *req,
				  struct ieee80211_scan_ies *ies)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	return iwl_mld_sched_scan_start(mld, vif, req, ies, IWL_MLD_SCAN_SCHED);
}

static int
iwl_mld_mac80211_sched_scan_stop(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	/* Due to a race condition, it's possible that mac80211 asks
	 * us to stop a sched_scan when it's already stopped. This
	 * can happen, for instance, if we stopped the scan ourselves,
	 * called ieee80211_sched_scan_stopped() and the userspace called
	 * stop sched scan before ieee80211_sched_scan_stopped_work()
	 * could run. To handle this, simply return if the scan is
	 * not running.
	 */
	if (!(mld->scan.status & IWL_MLD_SCAN_SCHED))
		return 0;

	return iwl_mld_scan_stop(mld, IWL_MLD_SCAN_SCHED, false);
}

static void
iwl_mld_restart_complete_vif(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct ieee80211_bss_conf *link_conf;
	struct iwl_mld *mld = data;
	int link_id;

	for_each_vif_active_link(vif, link_conf, link_id) {
		enum ieee80211_sta_rx_bandwidth bw;
		struct iwl_mld_link *mld_link;

		mld_link = wiphy_dereference(mld->wiphy,
					     mld_vif->link[link_id]);

		if (WARN_ON_ONCE(!mld_link))
			continue;

		bw = mld_link->rx_omi.bw_in_progress;
		if (bw)
			iwl_mld_change_link_omi_bw(mld, link_conf, bw);
	}
}

static void
iwl_mld_mac80211_reconfig_complete(struct ieee80211_hw *hw,
				   enum ieee80211_reconfig_type reconfig_type)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	switch (reconfig_type) {
	case IEEE80211_RECONFIG_TYPE_RESTART:
		mld->fw_status.in_hw_restart = false;
		iwl_mld_send_recovery_cmd(mld, ERROR_RECOVERY_END_OF_RECOVERY);

		ieee80211_iterate_interfaces(mld->hw,
					     IEEE80211_IFACE_ITER_NORMAL,
					     iwl_mld_restart_complete_vif, mld);

		iwl_trans_finish_sw_reset(mld->trans);
		/* no need to lock, adding in parallel would schedule too */
		if (!list_empty(&mld->txqs_to_add))
			wiphy_work_queue(mld->wiphy, &mld->add_txqs_wk);

		IWL_INFO(mld, "restart completed\n");
		break;
	case IEEE80211_RECONFIG_TYPE_SUSPEND:
		break;
	}
}

static
void iwl_mld_mac80211_mgd_prepare_tx(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_prep_tx_info *info)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	u32 duration = IWL_MLD_SESSION_PROTECTION_ASSOC_TIME_MS;

	/* After a successful association the connection is etalibeshed
	 * and we can rely on the quota to send the disassociation frame.
	 */
	if (info->was_assoc)
		return;

	if (info->duration > duration)
		duration = info->duration;

	iwl_mld_schedule_session_protection(mld, vif, duration,
					    IWL_MLD_SESSION_PROTECTION_MIN_TIME_MS,
					    info->link_id);
}

static
void iwl_mld_mac_mgd_complete_tx(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_prep_tx_info *info)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	/* Successful authentication is the only case that requires to let
	 * the session protection go. We'll need it for the upcoming
	 * association. For all the other cases, we need to cancel the session
	 * protection.
	 * After successful association the connection is established and
	 * further mgd tx can rely on the quota.
	 */
	if (info->success && info->subtype == IEEE80211_STYPE_AUTH)
		return;

	/* The firmware will be on medium after we configure the vif as
	 * associated. Removing the session protection allows the firmware
	 * to stop being on medium. In order to ensure the continuity of our
	 * presence on medium, we need first to configure the vif as associated
	 * and only then, remove the session protection.
	 * Currently, mac80211 calls vif_cfg_changed() first and then,
	 * drv_mgd_complete_tx(). Ensure that this assumption stays true by
	 * a warning.
	 */
	WARN_ON(info->success &&
		(info->subtype == IEEE80211_STYPE_ASSOC_REQ ||
		 info->subtype == IEEE80211_STYPE_REASSOC_REQ) &&
		!vif->cfg.assoc);

	iwl_mld_cancel_session_protection(mld, vif, info->link_id);
}

static int
iwl_mld_mac80211_conf_tx(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 unsigned int link_id, u16 ac,
			 const struct ieee80211_tx_queue_params *params)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_link *link;

	lockdep_assert_wiphy(mld->wiphy);

	link = iwl_mld_link_dereference_check(mld_vif, link_id);
	if (!link)
		return -EINVAL;

	link->queue_params[ac] = *params;

	/* No need to update right away, we'll get BSS_CHANGED_QOS
	 * The exception is P2P_DEVICE interface which needs immediate update.
	 */
	if (vif->type == NL80211_IFTYPE_P2P_DEVICE)
		iwl_mld_change_link_in_fw(mld, &vif->bss_conf,
					  LINK_CONTEXT_MODIFY_QOS_PARAMS);

	return 0;
}

static void iwl_mld_set_uapsd(struct iwl_mld *mld, struct ieee80211_vif *vif)
{
	vif->driver_flags &= ~IEEE80211_VIF_SUPPORTS_UAPSD;

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	if (vif->p2p &&
	    !(iwlwifi_mod_params.uapsd_disable & IWL_DISABLE_UAPSD_P2P_CLIENT))
		vif->driver_flags |= IEEE80211_VIF_SUPPORTS_UAPSD;

	if (!vif->p2p &&
	    !(iwlwifi_mod_params.uapsd_disable & IWL_DISABLE_UAPSD_BSS))
		vif->driver_flags |= IEEE80211_VIF_SUPPORTS_UAPSD;
}

int iwl_mld_tdls_sta_count(struct iwl_mld *mld)
{
	int count = 0;

	lockdep_assert_wiphy(mld->wiphy);

	for (int i = 0; i < mld->fw->ucode_capa.num_stations; i++) {
		struct ieee80211_link_sta *link_sta;

		link_sta = wiphy_dereference(mld->wiphy,
					     mld->fw_id_to_link_sta[i]);
		if (IS_ERR_OR_NULL(link_sta))
			continue;

		if (!link_sta->sta->tdls)
			continue;

		count++;
	}

	return count;
}

static void iwl_mld_check_he_obss_narrow_bw_ru_iter(struct wiphy *wiphy,
						    struct cfg80211_bss *bss,
						    void *_data)
{
	bool *tolerated = _data;
	const struct cfg80211_bss_ies *ies;
	const struct element *elem;

	rcu_read_lock();
	ies = rcu_dereference(bss->ies);
	elem = cfg80211_find_elem(WLAN_EID_EXT_CAPABILITY, ies->data,
				  ies->len);

	if (!elem || elem->datalen < 10 ||
	    !(elem->data[10] &
	      WLAN_EXT_CAPA10_OBSS_NARROW_BW_RU_TOLERANCE_SUPPORT)) {
		*tolerated = false;
	}
	rcu_read_unlock();
}

static void
iwl_mld_check_he_obss_narrow_bw_ru(struct iwl_mld *mld,
				   struct iwl_mld_link *mld_link,
				   struct ieee80211_bss_conf *link_conf)
{
	bool tolerated = true;

	if (WARN_ON_ONCE(!link_conf->chanreq.oper.chan))
		return;

	if (!(link_conf->chanreq.oper.chan->flags & IEEE80211_CHAN_RADAR)) {
		mld_link->he_ru_2mhz_block = false;
		return;
	}

	cfg80211_bss_iter(mld->wiphy, &link_conf->chanreq.oper,
			  iwl_mld_check_he_obss_narrow_bw_ru_iter, &tolerated);

	/* If there is at least one AP on radar channel that cannot
	 * tolerate 26-tone RU UL OFDMA transmissions using HE TB PPDU.
	 */
	mld_link->he_ru_2mhz_block = !tolerated;
}

static void iwl_mld_link_set_2mhz_block(struct iwl_mld *mld,
					struct ieee80211_vif *vif,
					struct ieee80211_sta *sta)
{
	struct ieee80211_link_sta *link_sta;
	unsigned int link_id;

	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct ieee80211_bss_conf *link_conf =
			link_conf_dereference_protected(vif, link_id);
		struct iwl_mld_link *mld_link =
			iwl_mld_link_from_mac80211(link_conf);

		if (WARN_ON(!link_conf || !mld_link))
			continue;

		if (link_sta->he_cap.has_he)
			iwl_mld_check_he_obss_narrow_bw_ru(mld, mld_link,
							   link_conf);
	}
}

static int iwl_mld_move_sta_state_up(struct iwl_mld *mld,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta,
				     enum ieee80211_sta_state old_state,
				     enum ieee80211_sta_state new_state)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	int tdls_count = 0;
	int ret;

	if (old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE) {
		if (sta->tdls) {
			if (vif->p2p || hweight8(mld->used_phy_ids) != 1)
				return -EBUSY;

			tdls_count = iwl_mld_tdls_sta_count(mld);
			if (tdls_count >= IWL_TDLS_STA_COUNT)
				return -EBUSY;
		}

		/*
		 * If this is the first STA (i.e. the AP) it won't do
		 * anything, otherwise must leave for any new STA on
		 * any other interface, or for TDLS, etc.
		 * Need to call this _before_ adding the STA so it can
		 * look up the one STA to use to ask mac80211 to leave
		 * OMI; in the unlikely event that adding the new STA
		 * then fails we'll just re-enter OMI later (via the
		 * statistics notification handling.)
		 */
		iwl_mld_leave_omi_bw_reduction(mld);

		ret = iwl_mld_add_sta(mld, sta, vif, STATION_TYPE_PEER);
		if (ret)
			return ret;

		/* just added first TDLS STA, so disable PM */
		if (sta->tdls && tdls_count == 0)
			iwl_mld_update_mac_power(mld, vif, false);

		if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls)
			mld_vif->ap_sta = sta;

		/* Initialize TLC here already - this really tells
		 * the firmware only what the supported legacy rates are
		 * (may be) since it's initialized already from what the
		 * AP advertised in the beacon/probe response. This will
		 * allow the firmware to send auth/assoc frames with one
		 * of the supported rates already, rather than having to
		 * use a mandatory rate.
		 * If we're the AP, we'll just assume mandatory rates at
		 * this point, but we know nothing about the STA anyway.
		 */
		iwl_mld_config_tlc(mld, vif, sta);

		return ret;
	} else if (old_state == IEEE80211_STA_NONE &&
		   new_state == IEEE80211_STA_AUTH) {
		iwl_mld_set_uapsd(mld, vif);
		return 0;
	} else if (old_state == IEEE80211_STA_AUTH &&
		   new_state == IEEE80211_STA_ASSOC) {
		ret = iwl_mld_update_all_link_stations(mld, sta);

		if (vif->type == NL80211_IFTYPE_STATION)
			iwl_mld_link_set_2mhz_block(mld, vif, sta);
		/* Now the link_sta's capabilities are set, update the FW */
		iwl_mld_config_tlc(mld, vif, sta);

		if (vif->type == NL80211_IFTYPE_AP) {
			/* Update MAC_CFG_FILTER_ACCEPT_BEACON if at least
			 * one sta is associated
			 */
			if (++mld_vif->num_associated_stas == 1)
				ret = iwl_mld_mac_fw_action(mld, vif,
							    FW_CTXT_ACTION_MODIFY);
		}

		return ret;
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTHORIZED) {
		ret = 0;

		if (!sta->tdls) {
			mld_vif->authorized = true;

			/* Ensure any block due to a non-BSS link is synced */
			iwl_mld_emlsr_check_non_bss_block(mld, 0);

			/* Block EMLSR until a certain throughput it reached */
			if (!mld->fw_status.in_hw_restart &&
			    IWL_MLD_ENTER_EMLSR_TPT_THRESH > 0)
				iwl_mld_block_emlsr(mld_vif->mld, vif,
						    IWL_MLD_EMLSR_BLOCKED_TPT,
						    0);

			/* clear COEX_HIGH_PRIORITY_ENABLE */
			ret = iwl_mld_mac_fw_action(mld, vif,
						    FW_CTXT_ACTION_MODIFY);
			if (ret)
				return ret;
			iwl_mld_smps_wa(mld, vif, vif->cfg.ps);
		}

		/* MFP is set by default before the station is authorized.
		 * Clear it here in case it's not used.
		 */
		if (!sta->mfp)
			ret = iwl_mld_update_all_link_stations(mld, sta);

		/* We can use wide bandwidth now, not only 20 MHz */
		iwl_mld_config_tlc(mld, vif, sta);

		return ret;
	} else {
		return -EINVAL;
	}
}

static int iwl_mld_move_sta_state_down(struct iwl_mld *mld,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta,
				       enum ieee80211_sta_state old_state,
				       enum ieee80211_sta_state new_state)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

	if (old_state == IEEE80211_STA_AUTHORIZED &&
	    new_state == IEEE80211_STA_ASSOC) {
		if (!sta->tdls) {
			mld_vif->authorized = false;

			memset(&mld_vif->emlsr.zeroed_on_not_authorized, 0,
			       sizeof(mld_vif->emlsr.zeroed_on_not_authorized));

			wiphy_delayed_work_cancel(mld->wiphy,
						  &mld_vif->emlsr.prevent_done_wk);
			wiphy_delayed_work_cancel(mld->wiphy,
						  &mld_vif->emlsr.tmp_non_bss_done_wk);
			wiphy_work_cancel(mld->wiphy, &mld_vif->emlsr.unblock_tpt_wk);
			wiphy_delayed_work_cancel(mld->wiphy,
						  &mld_vif->emlsr.check_tpt_wk);

			iwl_mld_reset_cca_40mhz_workaround(mld, vif);
			iwl_mld_smps_wa(mld, vif, true);
		}

		/* once we move into assoc state, need to update the FW to
		 * stop using wide bandwidth
		 */
		iwl_mld_config_tlc(mld, vif, sta);
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTH) {
		if (vif->type == NL80211_IFTYPE_AP &&
		    !WARN_ON(!mld_vif->num_associated_stas)) {
			/* Update MAC_CFG_FILTER_ACCEPT_BEACON if the last sta
			 * is disassociating
			 */
			if (--mld_vif->num_associated_stas == 0)
				iwl_mld_mac_fw_action(mld, vif,
						      FW_CTXT_ACTION_MODIFY);
		}
	} else if (old_state == IEEE80211_STA_AUTH &&
		   new_state == IEEE80211_STA_NONE) {
		/* nothing */
	} else if (old_state == IEEE80211_STA_NONE &&
		   new_state == IEEE80211_STA_NOTEXIST) {
		iwl_mld_remove_sta(mld, sta);

		if (sta->tdls && iwl_mld_tdls_sta_count(mld) == 0) {
			/* just removed last TDLS STA, so enable PM */
			iwl_mld_update_mac_power(mld, vif, false);
		}
	} else {
		return -EINVAL;
	}
	return 0;
}

static int iwl_mld_mac80211_sta_state(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta,
				      enum ieee80211_sta_state old_state,
				      enum ieee80211_sta_state new_state)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_sta *mld_sta = iwl_mld_sta_from_mac80211(sta);

	IWL_DEBUG_MAC80211(mld, "station %pM state change %d->%d\n",
			   sta->addr, old_state, new_state);

	mld_sta->sta_state = new_state;

	if (old_state < new_state)
		return iwl_mld_move_sta_state_up(mld, vif, sta, old_state,
						 new_state);
	else
		return iwl_mld_move_sta_state_down(mld, vif, sta, old_state,
						   new_state);
}

static void iwl_mld_mac80211_flush(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   u32 queues, bool drop)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	/* Make sure we're done with the deferred traffic before flushing */
	iwl_mld_add_txq_list(mld);

	for (int i = 0; i < mld->fw->ucode_capa.num_stations; i++) {
		struct ieee80211_link_sta *link_sta =
			wiphy_dereference(mld->wiphy,
					  mld->fw_id_to_link_sta[i]);

		if (IS_ERR_OR_NULL(link_sta))
			continue;

		/* Check that the sta belongs to the given vif */
		if (vif && vif != iwl_mld_sta_from_mac80211(link_sta->sta)->vif)
			continue;

		if (drop)
			iwl_mld_flush_sta_txqs(mld, link_sta->sta);
		else
			iwl_mld_wait_sta_txqs_empty(mld, link_sta->sta);
	}
}

static void iwl_mld_mac80211_flush_sta(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	iwl_mld_flush_sta_txqs(mld, sta);
}

static int
iwl_mld_mac80211_ampdu_action(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_ampdu_params *params)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct ieee80211_sta *sta = params->sta;
	enum ieee80211_ampdu_mlme_action action = params->action;
	u16 tid = params->tid;
	u16 ssn = params->ssn;
	u16 buf_size = params->buf_size;
	u16 timeout = params->timeout;
	int ret;

	IWL_DEBUG_HT(mld, "A-MPDU action on addr %pM tid: %d action: %d\n",
		     sta->addr, tid, action);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		ret = iwl_mld_ampdu_rx_start(mld, sta, tid, ssn, buf_size,
					     timeout);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		ret = iwl_mld_ampdu_rx_stop(mld, sta, tid);
		break;
	default:
		/* The mac80211 TX_AMPDU_SETUP_IN_HW flag is set for all
		 * devices, since all support TX A-MPDU offload in hardware.
		 * Therefore, no TX action should be requested here.
		 */
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	return ret;
}

static bool iwl_mld_can_hw_csum(struct sk_buff *skb)
{
	u8 protocol = ip_hdr(skb)->protocol;

	return protocol == IPPROTO_TCP || protocol == IPPROTO_UDP;
}

static bool iwl_mld_mac80211_can_aggregate(struct ieee80211_hw *hw,
					   struct sk_buff *head,
					   struct sk_buff *skb)
{
	if (!IS_ENABLED(CONFIG_INET))
		return false;

	/* For now don't aggregate IPv6 in AMSDU */
	if (skb->protocol != htons(ETH_P_IP))
		return false;

	/* Allow aggregation only if both frames have the same HW csum offload
	 * capability, ensuring consistent HW or SW csum handling in A-MSDU.
	 */
	return iwl_mld_can_hw_csum(skb) == iwl_mld_can_hw_csum(head);
}

static void iwl_mld_mac80211_sync_rx_queues(struct ieee80211_hw *hw)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	iwl_mld_sync_rx_queues(mld, IWL_MLD_RXQ_EMPTY, NULL, 0);
}

static void iwl_mld_sta_rc_update(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_link_sta *link_sta,
				  u32 changed)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	if (changed & (IEEE80211_RC_BW_CHANGED |
		       IEEE80211_RC_SUPP_RATES_CHANGED |
		       IEEE80211_RC_NSS_CHANGED)) {
		struct ieee80211_bss_conf *link =
			link_conf_dereference_check(vif, link_sta->link_id);

		if (WARN_ON(!link))
			return;

		iwl_mld_config_tlc_link(mld, vif, link, link_sta);
	}
}

static void iwl_mld_set_wakeup(struct ieee80211_hw *hw, bool enabled)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	device_set_wakeup_enable(mld->trans->dev, enabled);
}

/* Returns 0 on success. 1 if failed to suspend with wowlan:
 * If the circumstances didn't satisfy the conditions for suspension
 * with wowlan, mac80211 would use the no_wowlan flow.
 * If an error had occurred we update the trans status and state here
 * and the result will be stopping the FW.
 */
static int
iwl_mld_suspend(struct ieee80211_hw *hw, struct cfg80211_wowlan *wowlan)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	int ret;

	iwl_fw_runtime_suspend(&mld->fwrt);

	ret = iwl_mld_wowlan_suspend(mld, wowlan);
	if (ret) {
		if (ret < 0) {
			mld->trans->state = IWL_TRANS_NO_FW;
			set_bit(STATUS_FW_ERROR, &mld->trans->status);
		}
		return 1;
	}

	if (iwl_mld_no_wowlan_suspend(mld))
		return 1;

	return 0;
}

static int iwl_mld_resume(struct ieee80211_hw *hw)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	int ret;

	ret = iwl_mld_wowlan_resume(mld);
	if (ret)
		return ret;

	iwl_fw_runtime_resume(&mld->fwrt);

	iwl_mld_low_latency_restart(mld);

	return 0;
}

static int iwl_mld_alloc_ptk_pn(struct iwl_mld *mld,
				struct iwl_mld_sta *mld_sta,
				struct ieee80211_key_conf *key,
				struct iwl_mld_ptk_pn **ptk_pn)
{
	u8 num_rx_queues = mld->trans->num_rx_queues;
	int keyidx = key->keyidx;
	struct ieee80211_key_seq seq;

	if (WARN_ON(keyidx >= ARRAY_SIZE(mld_sta->ptk_pn)))
		return -EINVAL;

	WARN_ON(rcu_access_pointer(mld_sta->ptk_pn[keyidx]));
	*ptk_pn = kzalloc(struct_size(*ptk_pn, q, num_rx_queues),
			  GFP_KERNEL);
	if (!*ptk_pn)
		return -ENOMEM;

	for (u8 tid = 0; tid < IWL_MAX_TID_COUNT; tid++) {
		ieee80211_get_key_rx_seq(key, tid, &seq);
		for (u8 q = 0; q < num_rx_queues; q++)
			memcpy((*ptk_pn)->q[q].pn[tid], seq.ccmp.pn,
			       IEEE80211_CCMP_PN_LEN);
	}

	rcu_assign_pointer(mld_sta->ptk_pn[keyidx], *ptk_pn);

	return 0;
}

static int iwl_mld_set_key_add(struct iwl_mld *mld,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       struct ieee80211_key_conf *key)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_sta *mld_sta =
		sta ? iwl_mld_sta_from_mac80211(sta) : NULL;
	struct iwl_mld_ptk_pn *ptk_pn = NULL;
	int keyidx = key->keyidx;
	int ret;

	/* Will be set to 0 if added successfully */
	key->hw_key_idx = STA_KEY_IDX_INVALID;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		IWL_DEBUG_MAC80211(mld, "Use SW encryption for WEP\n");
		return -EOPNOTSUPP;
	case WLAN_CIPHER_SUITE_TKIP:
		if (vif->type == NL80211_IFTYPE_STATION) {
			key->flags |= IEEE80211_KEY_FLAG_PUT_MIC_SPACE;
			break;
		}
		IWL_DEBUG_MAC80211(mld, "Use SW encryption for TKIP\n");
		return -EOPNOTSUPP;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (vif->type == NL80211_IFTYPE_STATION &&
	    (keyidx == 6 || keyidx == 7))
		rcu_assign_pointer(mld_vif->bigtks[keyidx - 6], key);

	/* After exiting from RFKILL, hostapd configures GTK/ITGK before the
	 * AP is started, but those keys can't be sent to the FW before the
	 * MCAST/BCAST STAs are added to it (which happens upon AP start).
	 * Store it here to be sent later when the AP is started.
	 */
	if ((vif->type == NL80211_IFTYPE_ADHOC ||
	     vif->type == NL80211_IFTYPE_AP) && !sta &&
	     !mld_vif->ap_ibss_active)
		return iwl_mld_store_ap_early_key(mld, key, mld_vif);

	if (!mld->fw_status.in_hw_restart && mld_sta &&
	    key->flags & IEEE80211_KEY_FLAG_PAIRWISE &&
	    (key->cipher == WLAN_CIPHER_SUITE_CCMP ||
	     key->cipher == WLAN_CIPHER_SUITE_GCMP ||
	     key->cipher == WLAN_CIPHER_SUITE_GCMP_256)) {
		ret = iwl_mld_alloc_ptk_pn(mld, mld_sta, key, &ptk_pn);
		if (ret)
			return ret;
	}

	IWL_DEBUG_MAC80211(mld, "set hwcrypto key (sta:%pM, id:%d)\n",
			   sta ? sta->addr : NULL, keyidx);

	ret = iwl_mld_add_key(mld, vif, sta, key);
	if (ret) {
		IWL_WARN(mld, "set key failed (%d)\n", ret);
		if (ptk_pn) {
			RCU_INIT_POINTER(mld_sta->ptk_pn[keyidx], NULL);
			kfree(ptk_pn);
		}

		return -EOPNOTSUPP;
	}

	return 0;
}

static void iwl_mld_set_key_remove(struct iwl_mld *mld,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   struct ieee80211_key_conf *key)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_sta *mld_sta =
		sta ? iwl_mld_sta_from_mac80211(sta) : NULL;
	int keyidx = key->keyidx;

	if (vif->type == NL80211_IFTYPE_STATION &&
	    (keyidx == 6 || keyidx == 7))
		RCU_INIT_POINTER(mld_vif->bigtks[keyidx - 6], NULL);

	if (mld_sta && key->flags & IEEE80211_KEY_FLAG_PAIRWISE &&
	    (key->cipher == WLAN_CIPHER_SUITE_CCMP ||
	     key->cipher == WLAN_CIPHER_SUITE_GCMP ||
	     key->cipher == WLAN_CIPHER_SUITE_GCMP_256)) {
		struct iwl_mld_ptk_pn *ptk_pn;

		if (WARN_ON(keyidx >= ARRAY_SIZE(mld_sta->ptk_pn)))
			return;

		ptk_pn = wiphy_dereference(mld->wiphy,
					   mld_sta->ptk_pn[keyidx]);
		RCU_INIT_POINTER(mld_sta->ptk_pn[keyidx], NULL);
		if (!WARN_ON(!ptk_pn))
			kfree_rcu(ptk_pn, rcu_head);
	}

	/* if this key was stored to be added later to the FW - free it here */
	if (!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		iwl_mld_free_ap_early_key(mld, key, mld_vif);

	/* We already removed it */
	if (key->hw_key_idx == STA_KEY_IDX_INVALID)
		return;

	IWL_DEBUG_MAC80211(mld, "disable hwcrypto key\n");

	iwl_mld_remove_key(mld, vif, sta, key);
}

static int iwl_mld_mac80211_set_key(struct ieee80211_hw *hw,
				    enum set_key_cmd cmd,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta,
				    struct ieee80211_key_conf *key)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	int ret;

	switch (cmd) {
	case SET_KEY:
		ret = iwl_mld_set_key_add(mld, vif, sta, key);
		if (ret)
			ret = -EOPNOTSUPP;
		break;
	case DISABLE_KEY:
		iwl_mld_set_key_remove(mld, vif, sta, key);
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int
iwl_mld_pre_channel_switch(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   struct ieee80211_channel_switch *chsw)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_link *mld_link =
		iwl_mld_link_dereference_check(mld_vif, chsw->link_id);
	u8 primary;
	int selected;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(!mld_link))
		return -EINVAL;

	IWL_DEBUG_MAC80211(mld, "pre CSA to freq %d\n",
			   chsw->chandef.center_freq1);

	if (!iwl_mld_emlsr_active(vif))
		return 0;

	primary = iwl_mld_get_primary_link(vif);

	/* stay on the primary link unless it is undergoing a CSA with quiet */
	if (chsw->link_id == primary && chsw->block_tx)
		selected = iwl_mld_get_other_link(vif, primary);
	else
		selected = primary;

	/* Remember to tell the firmware that this link can't tx
	 * Note that this logic seems to be unrelated to emlsr, but it
	 * really is needed only when emlsr is active. When we have a
	 * single link, the firmware will handle all this on its own.
	 * In multi-link scenarios, we can learn about the CSA from
	 * another link and this logic is too complex for the firmware
	 * to track.
	 * Since we want to de-activate the link that got a CSA with mode=1,
	 * we need to tell the firmware not to send any frame on that link
	 * as the firmware may not be aware that link is under a CSA
	 * with mode=1 (no Tx allowed).
	 */
	mld_link->silent_deactivation = chsw->block_tx;
	iwl_mld_exit_emlsr(mld, vif, IWL_MLD_EMLSR_EXIT_CSA, selected);

	return 0;
}

static void
iwl_mld_channel_switch(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif,
		       struct ieee80211_channel_switch *chsw)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	/* By implementing this operation, we prevent mac80211 from
	 * starting its own channel switch timer, so that we can call
	 * ieee80211_chswitch_done() ourselves at the right time
	 * (Upon receiving the channel_switch_start notification from the fw)
	 */
	IWL_DEBUG_MAC80211(mld,
			   "dummy channel switch op\n");
}

static int
iwl_mld_post_channel_switch(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link_conf);

	lockdep_assert_wiphy(mld->wiphy);

	WARN_ON(mld_link->silent_deactivation);

	return 0;
}

static void
iwl_mld_abort_channel_switch(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link_conf);

	IWL_DEBUG_MAC80211(mld,
			   "abort channel switch op\n");
	mld_link->silent_deactivation = false;
}

static int
iwl_mld_switch_vif_chanctx_swap(struct ieee80211_hw *hw,
				struct ieee80211_vif_chanctx_switch *vifs)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	int ret;

	iwl_mld_unassign_vif_chanctx(hw, vifs[0].vif, vifs[0].link_conf,
				     vifs[0].old_ctx);
	iwl_mld_remove_chanctx(hw, vifs[0].old_ctx);

	ret = iwl_mld_add_chanctx(hw, vifs[0].new_ctx);
	if (ret) {
		IWL_ERR(mld, "failed to add new_ctx during channel switch\n");
		goto out_reassign;
	}

	ret = iwl_mld_assign_vif_chanctx(hw, vifs[0].vif, vifs[0].link_conf,
					 vifs[0].new_ctx);
	if (ret) {
		IWL_ERR(mld,
			"failed to assign new_ctx during channel switch\n");
		goto out_remove;
	}

	return 0;

 out_remove:
	iwl_mld_remove_chanctx(hw, vifs[0].new_ctx);
 out_reassign:
	if (iwl_mld_add_chanctx(hw, vifs[0].old_ctx)) {
		IWL_ERR(mld, "failed to add old_ctx after failure\n");
		return ret;
	}

	if (iwl_mld_assign_vif_chanctx(hw, vifs[0].vif, vifs[0].link_conf,
				       vifs[0].old_ctx))
		IWL_ERR(mld, "failed to reassign old_ctx after failure\n");

	return ret;
}

static int
iwl_mld_switch_vif_chanctx_reassign(struct ieee80211_hw *hw,
				    struct ieee80211_vif_chanctx_switch *vifs)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	int ret;

	iwl_mld_unassign_vif_chanctx(hw, vifs[0].vif, vifs[0].link_conf,
				     vifs[0].old_ctx);
	ret = iwl_mld_assign_vif_chanctx(hw, vifs[0].vif, vifs[0].link_conf,
					 vifs[0].new_ctx);
	if (ret) {
		IWL_ERR(mld,
			"failed to assign new_ctx during channel switch\n");
		goto out_reassign;
	}

	return 0;

out_reassign:
	if (iwl_mld_assign_vif_chanctx(hw, vifs[0].vif, vifs[0].link_conf,
				       vifs[0].old_ctx))
		IWL_ERR(mld, "failed to reassign old_ctx after failure\n");

	return ret;
}

static int
iwl_mld_switch_vif_chanctx(struct ieee80211_hw *hw,
			   struct ieee80211_vif_chanctx_switch *vifs,
			   int n_vifs,
			   enum ieee80211_chanctx_switch_mode mode)
{
	int ret;

	/* we only support a single-vif right now */
	if (n_vifs > 1)
		return -EOPNOTSUPP;

	switch (mode) {
	case CHANCTX_SWMODE_SWAP_CONTEXTS:
		ret = iwl_mld_switch_vif_chanctx_swap(hw, vifs);
		break;
	case CHANCTX_SWMODE_REASSIGN_VIF:
		ret = iwl_mld_switch_vif_chanctx_reassign(hw, vifs);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static void iwl_mld_sta_pre_rcu_remove(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_sta *mld_sta = iwl_mld_sta_from_mac80211(sta);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_link_sta *mld_link_sta;
	u8 link_id;

	lockdep_assert_wiphy(mld->wiphy);

	/* This is called before mac80211 does RCU synchronisation,
	 * so here we already invalidate our internal RCU-protected
	 * station pointer. The rest of the code will thus no longer
	 * be able to find the station this way, and we don't rely
	 * on further RCU synchronisation after the sta_state()
	 * callback deleted the station.
	 */
	for_each_mld_link_sta(mld_sta, mld_link_sta, link_id)
		RCU_INIT_POINTER(mld->fw_id_to_link_sta[mld_link_sta->fw_id],
				 NULL);

	if (sta == mld_vif->ap_sta)
		mld_vif->ap_sta = NULL;
}

static void
iwl_mld_mac80211_mgd_protect_tdls_discover(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   unsigned int link_id)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct ieee80211_bss_conf *link_conf;
	u32 duration;
	int ret;

	link_conf = wiphy_dereference(hw->wiphy, vif->link_conf[link_id]);
	if (WARN_ON_ONCE(!link_conf))
		return;

	/* Protect the session to hear the TDLS setup response on the channel */

	duration = 2 * link_conf->dtim_period * link_conf->beacon_int;

	ret = iwl_mld_start_session_protection(mld, vif, duration, duration,
					       link_id, HZ / 5);
	if (ret)
		IWL_ERR(mld,
			"Failed to start session protection for TDLS: %d\n",
			ret);
}

static bool iwl_mld_can_activate_links(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       u16 desired_links)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	int n_links = hweight16(desired_links);

	/* Check if HW supports the wanted number of links */
	return n_links <= iwl_mld_max_active_links(mld, vif);
}

static int
iwl_mld_change_vif_links(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 u16 old_links, u16 new_links,
			 struct ieee80211_bss_conf *old[IEEE80211_MLD_MAX_NUM_LINKS])
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct ieee80211_bss_conf *link_conf;
	u16 removed = old_links & ~new_links;
	u16 added = new_links & ~old_links;
	int err;

	lockdep_assert_wiphy(mld->wiphy);

	/*
	 * No bits designate non-MLO mode. We can handle MLO exit/enter by
	 * simply mapping that to link ID zero internally.
	 * Note that mac80211 does such a non-MLO to MLO switch during restart
	 * if it was in MLO before. In that case, we do not have a link to
	 * remove.
	 */
	if (old_links == 0 && !mld->fw_status.in_hw_restart)
		removed |= BIT(0);

	if (new_links == 0)
		added |= BIT(0);

	for (int i = 0; i < IEEE80211_MLD_MAX_NUM_LINKS; i++) {
		if (removed & BIT(i))
			iwl_mld_remove_link(mld, old[i]);
	}

	for (int i = 0; i < IEEE80211_MLD_MAX_NUM_LINKS; i++) {
		if (added & BIT(i)) {
			link_conf = link_conf_dereference_protected(vif, i);
			if (WARN_ON(!link_conf))
				return -EINVAL;

			err = iwl_mld_add_link(mld, link_conf);
			if (err)
				goto remove_added_links;
		}
	}

	/*
	 * Ensure we always have a valid primary_link. When using multiple
	 * links the proper value is set in assign_vif_chanctx.
	 */
	mld_vif->emlsr.primary = new_links ? __ffs(new_links) : 0;

	/*
	 * Special MLO restart case. We did not have a link when the interface
	 * was added, so do the power configuration now.
	 */
	if (old_links == 0 && mld->fw_status.in_hw_restart)
		iwl_mld_update_mac_power(mld, vif, false);

	return 0;

remove_added_links:
	for (int i = 0; i < IEEE80211_MLD_MAX_NUM_LINKS; i++) {
		if (!(added & BIT(i)))
			continue;

		link_conf = link_conf_dereference_protected(vif, i);
		if (!link_conf || !iwl_mld_link_from_mac80211(link_conf))
			continue;

		iwl_mld_remove_link(mld, link_conf);
	}

	return err;
}

static int iwl_mld_change_sta_links(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta,
				    u16 old_links, u16 new_links)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	return iwl_mld_update_link_stas(mld, vif, sta, old_links, new_links);
}

static int iwl_mld_mac80211_join_ibss(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif)
{
	return iwl_mld_start_ap_ibss(hw, vif, &vif->bss_conf);
}

static void iwl_mld_mac80211_leave_ibss(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif)
{
	return iwl_mld_stop_ap_ibss(hw, vif, &vif->bss_conf);
}

static int iwl_mld_mac80211_tx_last_beacon(struct ieee80211_hw *hw)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	return mld->ibss_manager;
}

#define IWL_MLD_EMLSR_BLOCKED_TMP_NON_BSS_TIMEOUT (5 * HZ)

static void iwl_mld_vif_iter_emlsr_block_tmp_non_bss(void *_data, u8 *mac,
						     struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	int ret;

	if (!iwl_mld_vif_has_emlsr_cap(vif))
		return;

	ret = iwl_mld_block_emlsr_sync(mld_vif->mld, vif,
				       IWL_MLD_EMLSR_BLOCKED_TMP_NON_BSS,
				       iwl_mld_get_primary_link(vif));
	if (ret)
		return;

	wiphy_delayed_work_queue(mld_vif->mld->wiphy,
				 &mld_vif->emlsr.tmp_non_bss_done_wk,
				 IWL_MLD_EMLSR_BLOCKED_TMP_NON_BSS_TIMEOUT);
}

static void iwl_mld_prep_add_interface(struct ieee80211_hw *hw,
				       enum nl80211_iftype type)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	IWL_DEBUG_MAC80211(mld, "prep_add_interface: type=%u\n", type);

	if (!(type == NL80211_IFTYPE_AP ||
	      type == NL80211_IFTYPE_P2P_GO ||
	      type == NL80211_IFTYPE_P2P_CLIENT))
		return;

	ieee80211_iterate_active_interfaces_mtx(mld->hw,
						IEEE80211_IFACE_ITER_NORMAL,
						iwl_mld_vif_iter_emlsr_block_tmp_non_bss,
						NULL);
}

static int iwl_mld_set_hw_timestamp(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct cfg80211_set_hw_timestamp *hwts)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	u32 protocols = 0;

	/* HW timestamping is only supported for a specific station */
	if (!hwts->macaddr)
		return -EOPNOTSUPP;

	if (hwts->enable)
		protocols =
			IWL_TIME_SYNC_PROTOCOL_TM | IWL_TIME_SYNC_PROTOCOL_FTM;

	return iwl_mld_time_sync_config(mld, hwts->macaddr, protocols);
}

static int iwl_mld_start_pmsr(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct cfg80211_pmsr_request *request)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	return iwl_mld_ftm_start(mld, vif, request);
}

const struct ieee80211_ops iwl_mld_hw_ops = {
	.tx = iwl_mld_mac80211_tx,
	.start = iwl_mld_mac80211_start,
	.stop = iwl_mld_mac80211_stop,
	.config = iwl_mld_mac80211_config,
	.add_interface = iwl_mld_mac80211_add_interface,
	.remove_interface = iwl_mld_mac80211_remove_interface,
	.conf_tx = iwl_mld_mac80211_conf_tx,
	.prepare_multicast = iwl_mld_mac80211_prepare_multicast,
	.configure_filter = iwl_mld_mac80211_configure_filter,
	.reconfig_complete = iwl_mld_mac80211_reconfig_complete,
	.wake_tx_queue = iwl_mld_mac80211_wake_tx_queue,
	.add_chanctx = iwl_mld_add_chanctx,
	.remove_chanctx = iwl_mld_remove_chanctx,
	.change_chanctx = iwl_mld_change_chanctx,
	.assign_vif_chanctx = iwl_mld_assign_vif_chanctx,
	.unassign_vif_chanctx = iwl_mld_unassign_vif_chanctx,
	.set_rts_threshold = iwl_mld_mac80211_set_rts_threshold,
	.link_info_changed = iwl_mld_mac80211_link_info_changed,
	.vif_cfg_changed = iwl_mld_mac80211_vif_cfg_changed,
	.set_key = iwl_mld_mac80211_set_key,
	.hw_scan = iwl_mld_mac80211_hw_scan,
	.cancel_hw_scan = iwl_mld_mac80211_cancel_hw_scan,
	.sched_scan_start = iwl_mld_mac80211_sched_scan_start,
	.sched_scan_stop = iwl_mld_mac80211_sched_scan_stop,
	.mgd_prepare_tx = iwl_mld_mac80211_mgd_prepare_tx,
	.mgd_complete_tx = iwl_mld_mac_mgd_complete_tx,
	.sta_state = iwl_mld_mac80211_sta_state,
	.sta_statistics = iwl_mld_mac80211_sta_statistics,
	.flush = iwl_mld_mac80211_flush,
	.flush_sta = iwl_mld_mac80211_flush_sta,
	.ampdu_action = iwl_mld_mac80211_ampdu_action,
	.can_aggregate_in_amsdu = iwl_mld_mac80211_can_aggregate,
	.sync_rx_queues = iwl_mld_mac80211_sync_rx_queues,
	.link_sta_rc_update = iwl_mld_sta_rc_update,
	.start_ap = iwl_mld_start_ap_ibss,
	.stop_ap = iwl_mld_stop_ap_ibss,
	.pre_channel_switch = iwl_mld_pre_channel_switch,
	.channel_switch = iwl_mld_channel_switch,
	.post_channel_switch = iwl_mld_post_channel_switch,
	.abort_channel_switch = iwl_mld_abort_channel_switch,
	.switch_vif_chanctx = iwl_mld_switch_vif_chanctx,
	.sta_pre_rcu_remove = iwl_mld_sta_pre_rcu_remove,
	.remain_on_channel = iwl_mld_start_roc,
	.cancel_remain_on_channel = iwl_mld_cancel_roc,
	.can_activate_links = iwl_mld_can_activate_links,
	.change_vif_links = iwl_mld_change_vif_links,
	.change_sta_links = iwl_mld_change_sta_links,
#ifdef CONFIG_PM_SLEEP
	.suspend = iwl_mld_suspend,
	.resume = iwl_mld_resume,
	.set_wakeup = iwl_mld_set_wakeup,
	.set_rekey_data = iwl_mld_set_rekey_data,
#if IS_ENABLED(CONFIG_IPV6)
	.ipv6_addr_change = iwl_mld_ipv6_addr_change,
#endif /* IS_ENABLED(CONFIG_IPV6) */
#endif /* CONFIG_PM_SLEEP */
#ifdef CONFIG_IWLWIFI_DEBUGFS
	.vif_add_debugfs = iwl_mld_add_vif_debugfs,
	.link_add_debugfs = iwl_mld_add_link_debugfs,
	.link_sta_add_debugfs = iwl_mld_add_link_sta_debugfs,
#endif
	.mgd_protect_tdls_discover = iwl_mld_mac80211_mgd_protect_tdls_discover,
	.join_ibss = iwl_mld_mac80211_join_ibss,
	.leave_ibss = iwl_mld_mac80211_leave_ibss,
	.tx_last_beacon = iwl_mld_mac80211_tx_last_beacon,
	.prep_add_interface = iwl_mld_prep_add_interface,
	.set_hw_timestamp = iwl_mld_set_hw_timestamp,
	.start_pmsr = iwl_mld_start_pmsr,
};
