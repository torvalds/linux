// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2022 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/if_arp.h>
#include <linux/time.h>
#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>
#include <net/tcp.h>

#include "iwl-drv.h"
#include "iwl-op-mode.h"
#include "iwl-io.h"
#include "mvm.h"
#include "sta.h"
#include "time-event.h"
#include "iwl-eeprom-parse.h"
#include "iwl-phy-db.h"
#include "testmode.h"
#include "fw/error-dump.h"
#include "iwl-prph.h"
#include "iwl-nvm-parse.h"

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
		.num_different_channels = 2,
		.max_interfaces = 3,
		.limits = iwl_mvm_limits,
		.n_limits = ARRAY_SIZE(iwl_mvm_limits),
	},
};

static const struct cfg80211_pmsr_capabilities iwl_mvm_pmsr_capa = {
	.max_peers = IWL_MVM_TOF_MAX_APS,
	.report_ap_tsf = 1,
	.randomize_mac_addr = 1,

	.ftm = {
		.supported = 1,
		.asap = 1,
		.non_asap = 1,
		.request_lci = 1,
		.request_civicloc = 1,
		.trigger_based = 1,
		.non_trigger_based = 1,
		.max_bursts_exponent = -1, /* all supported */
		.max_ftms_per_burst = 0, /* no limits */
		.bandwidths = BIT(NL80211_CHAN_WIDTH_20_NOHT) |
			      BIT(NL80211_CHAN_WIDTH_20) |
			      BIT(NL80211_CHAN_WIDTH_40) |
			      BIT(NL80211_CHAN_WIDTH_80) |
			      BIT(NL80211_CHAN_WIDTH_160),
		.preambles = BIT(NL80211_PREAMBLE_LEGACY) |
			     BIT(NL80211_PREAMBLE_HT) |
			     BIT(NL80211_PREAMBLE_VHT) |
			     BIT(NL80211_PREAMBLE_HE),
	},
};

static int __iwl_mvm_mac_set_key(struct ieee80211_hw *hw,
				 enum set_key_cmd cmd,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 struct ieee80211_key_conf *key);

static void iwl_mvm_reset_phy_ctxts(struct iwl_mvm *mvm)
{
	int i;

	memset(mvm->phy_ctxts, 0, sizeof(mvm->phy_ctxts));
	for (i = 0; i < NUM_PHY_CTX; i++) {
		mvm->phy_ctxts[i].id = i;
		mvm->phy_ctxts[i].ref = 0;
	}
}

struct ieee80211_regdomain *iwl_mvm_get_regdomain(struct wiphy *wiphy,
						  const char *alpha2,
						  enum iwl_mcc_source src_id,
						  bool *changed)
{
	struct ieee80211_regdomain *regd = NULL;
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mcc_update_resp *resp;
	u8 resp_ver;

	IWL_DEBUG_LAR(mvm, "Getting regdomain data for %s from FW\n", alpha2);

	lockdep_assert_held(&mvm->mutex);

	resp = iwl_mvm_update_mcc(mvm, alpha2, src_id);
	if (IS_ERR_OR_NULL(resp)) {
		IWL_DEBUG_LAR(mvm, "Could not get update from FW %d\n",
			      PTR_ERR_OR_ZERO(resp));
		resp = NULL;
		goto out;
	}

	if (changed) {
		u32 status = le32_to_cpu(resp->status);

		*changed = (status == MCC_RESP_NEW_CHAN_PROFILE ||
			    status == MCC_RESP_ILLEGAL);
	}
	resp_ver = iwl_fw_lookup_notif_ver(mvm->fw, IWL_ALWAYS_LONG_GROUP,
					   MCC_UPDATE_CMD, 0);
	IWL_DEBUG_LAR(mvm, "MCC update response version: %d\n", resp_ver);

	regd = iwl_parse_nvm_mcc_info(mvm->trans->dev, mvm->cfg,
				      __le32_to_cpu(resp->n_channels),
				      resp->channels,
				      __le16_to_cpu(resp->mcc),
				      __le16_to_cpu(resp->geo_info),
				      __le16_to_cpu(resp->cap), resp_ver);
	/* Store the return source id */
	src_id = resp->source_id;
	if (IS_ERR_OR_NULL(regd)) {
		IWL_DEBUG_LAR(mvm, "Could not get parse update from FW %d\n",
			      PTR_ERR_OR_ZERO(regd));
		goto out;
	}

	IWL_DEBUG_LAR(mvm, "setting alpha2 from FW to %s (0x%x, 0x%x) src=%d\n",
		      regd->alpha2, regd->alpha2[0], regd->alpha2[1], src_id);
	mvm->lar_regdom_set = true;
	mvm->mcc_src = src_id;

	iwl_mei_set_country_code(__le16_to_cpu(resp->mcc));

out:
	kfree(resp);
	return regd;
}

void iwl_mvm_update_changed_regdom(struct iwl_mvm *mvm)
{
	bool changed;
	struct ieee80211_regdomain *regd;

	if (!iwl_mvm_is_lar_supported(mvm))
		return;

	regd = iwl_mvm_get_current_regdomain(mvm, &changed);
	if (!IS_ERR_OR_NULL(regd)) {
		/* only update the regulatory core if changed */
		if (changed)
			regulatory_set_wiphy_regd(mvm->hw->wiphy, regd);

		kfree(regd);
	}
}

struct ieee80211_regdomain *iwl_mvm_get_current_regdomain(struct iwl_mvm *mvm,
							  bool *changed)
{
	return iwl_mvm_get_regdomain(mvm->hw->wiphy, "ZZ",
				     iwl_mvm_is_wifi_mcc_supported(mvm) ?
				     MCC_SOURCE_GET_CURRENT :
				     MCC_SOURCE_OLD_FW, changed);
}

int iwl_mvm_init_fw_regd(struct iwl_mvm *mvm)
{
	enum iwl_mcc_source used_src;
	struct ieee80211_regdomain *regd;
	int ret;
	bool changed;
	const struct ieee80211_regdomain *r =
			wiphy_dereference(mvm->hw->wiphy, mvm->hw->wiphy->regd);

	if (!r)
		return -ENOENT;

	/* save the last source in case we overwrite it below */
	used_src = mvm->mcc_src;
	if (iwl_mvm_is_wifi_mcc_supported(mvm)) {
		/* Notify the firmware we support wifi location updates */
		regd = iwl_mvm_get_current_regdomain(mvm, NULL);
		if (!IS_ERR_OR_NULL(regd))
			kfree(regd);
	}

	/* Now set our last stored MCC and source */
	regd = iwl_mvm_get_regdomain(mvm->hw->wiphy, r->alpha2, used_src,
				     &changed);
	if (IS_ERR_OR_NULL(regd))
		return -EIO;

	/* update cfg80211 if the regdomain was changed */
	if (changed)
		ret = regulatory_set_wiphy_regd_sync(mvm->hw->wiphy, regd);
	else
		ret = 0;

	kfree(regd);
	return ret;
}

static const u8 he_if_types_ext_capa_sta[] = {
	 [0] = WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING,
	 [2] = WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT,
	 [7] = WLAN_EXT_CAPA8_OPMODE_NOTIF,
};

static const struct wiphy_iftype_ext_capab he_iftypes_ext_capa[] = {
	{
		.iftype = NL80211_IFTYPE_STATION,
		.extended_capabilities = he_if_types_ext_capa_sta,
		.extended_capabilities_mask = he_if_types_ext_capa_sta,
		.extended_capabilities_len = sizeof(he_if_types_ext_capa_sta),
	},
};

static int
iwl_mvm_op_get_antenna(struct ieee80211_hw *hw, u32 *tx_ant, u32 *rx_ant)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	*tx_ant = iwl_mvm_get_valid_tx_ant(mvm);
	*rx_ant = iwl_mvm_get_valid_rx_ant(mvm);
	return 0;
}

int iwl_mvm_mac_setup_register(struct iwl_mvm *mvm)
{
	struct ieee80211_hw *hw = mvm->hw;
	int num_mac, ret, i;
	static const u32 mvm_ciphers[] = {
		WLAN_CIPHER_SUITE_WEP40,
		WLAN_CIPHER_SUITE_WEP104,
		WLAN_CIPHER_SUITE_TKIP,
		WLAN_CIPHER_SUITE_CCMP,
	};
#ifdef CONFIG_PM_SLEEP
	bool unified = fw_has_capa(&mvm->fw->ucode_capa,
				   IWL_UCODE_TLV_CAPA_CNSLDTD_D3_D0_IMG);
#endif

	/* Tell mac80211 our characteristics */
	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, SPECTRUM_MGMT);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(hw, WANT_MONITOR_VIF);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, TIMING_BEACON_ONLY);
	ieee80211_hw_set(hw, CONNECTION_MONITOR);
	ieee80211_hw_set(hw, CHANCTX_STA_CSA);
	ieee80211_hw_set(hw, SUPPORT_FAST_XMIT);
	ieee80211_hw_set(hw, SUPPORTS_CLONED_SKBS);
	ieee80211_hw_set(hw, SUPPORTS_AMSDU_IN_AMPDU);
	ieee80211_hw_set(hw, NEEDS_UNIQUE_STA_ADDR);
	ieee80211_hw_set(hw, DEAUTH_NEED_MGD_TX_PREP);
	ieee80211_hw_set(hw, SUPPORTS_VHT_EXT_NSS_BW);
	ieee80211_hw_set(hw, BUFF_MMPDU_TXQ);
	ieee80211_hw_set(hw, STA_MMPDU_TXQ);
	/*
	 * On older devices, enabling TX A-MSDU occasionally leads to
	 * something getting messed up, the command read from the FIFO
	 * gets out of sync and isn't a TX command, so that we have an
	 * assert EDC.
	 *
	 * It's not clear where the bug is, but since we didn't used to
	 * support A-MSDU until moving the mac80211 iTXQs, just leave it
	 * for older devices. We also don't see this issue on any newer
	 * devices.
	 */
	if (mvm->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_9000)
		ieee80211_hw_set(hw, TX_AMSDU);
	ieee80211_hw_set(hw, TX_FRAG_LIST);

	if (iwl_mvm_has_tlc_offload(mvm)) {
		ieee80211_hw_set(hw, TX_AMPDU_SETUP_IN_HW);
		ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	}

	if (iwl_mvm_has_new_rx_api(mvm))
		ieee80211_hw_set(hw, SUPPORTS_REORDERING_BUFFER);

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_STA_PM_NOTIF)) {
		ieee80211_hw_set(hw, AP_LINK_PS);
	} else if (WARN_ON(iwl_mvm_has_new_tx_api(mvm))) {
		/*
		 * we absolutely need this for the new TX API since that comes
		 * with many more queues than the current code can deal with
		 * for station powersave
		 */
		return -EINVAL;
	}

	if (mvm->trans->num_rx_queues > 1)
		ieee80211_hw_set(hw, USES_RSS);

	if (mvm->trans->max_skb_frags)
		hw->netdev_features = NETIF_F_HIGHDMA | NETIF_F_SG;

	hw->queues = IEEE80211_NUM_ACS;
	hw->offchannel_tx_hw_queue = IWL_MVM_OFFCHANNEL_QUEUE;
	hw->radiotap_mcs_details |= IEEE80211_RADIOTAP_MCS_HAVE_FEC |
				    IEEE80211_RADIOTAP_MCS_HAVE_STBC;
	hw->radiotap_vht_details |= IEEE80211_RADIOTAP_VHT_KNOWN_STBC |
		IEEE80211_RADIOTAP_VHT_KNOWN_BEAMFORMED;

	hw->radiotap_timestamp.units_pos =
		IEEE80211_RADIOTAP_TIMESTAMP_UNIT_US |
		IEEE80211_RADIOTAP_TIMESTAMP_SPOS_PLCP_SIG_ACQ;
	/* this is the case for CCK frames, it's better (only 8) for OFDM */
	hw->radiotap_timestamp.accuracy = 22;

	if (!iwl_mvm_has_tlc_offload(mvm))
		hw->rate_control_algorithm = RS_NAME;

	hw->uapsd_queues = IWL_MVM_UAPSD_QUEUES;
	hw->uapsd_max_sp_len = IWL_UAPSD_MAX_SP;
	hw->max_tx_fragments = mvm->trans->max_skb_frags;

	BUILD_BUG_ON(ARRAY_SIZE(mvm->ciphers) < ARRAY_SIZE(mvm_ciphers) + 6);
	memcpy(mvm->ciphers, mvm_ciphers, sizeof(mvm_ciphers));
	hw->wiphy->n_cipher_suites = ARRAY_SIZE(mvm_ciphers);
	hw->wiphy->cipher_suites = mvm->ciphers;

	if (iwl_mvm_has_new_rx_api(mvm)) {
		mvm->ciphers[hw->wiphy->n_cipher_suites] =
			WLAN_CIPHER_SUITE_GCMP;
		hw->wiphy->n_cipher_suites++;
		mvm->ciphers[hw->wiphy->n_cipher_suites] =
			WLAN_CIPHER_SUITE_GCMP_256;
		hw->wiphy->n_cipher_suites++;
	}

	if (iwlwifi_mod_params.swcrypto)
		IWL_ERR(mvm,
			"iwlmvm doesn't allow to disable HW crypto, check swcrypto module parameter\n");
	if (!iwlwifi_mod_params.bt_coex_active)
		IWL_ERR(mvm,
			"iwlmvm doesn't allow to disable BT Coex, check bt_coex_active module parameter\n");

	ieee80211_hw_set(hw, MFP_CAPABLE);
	mvm->ciphers[hw->wiphy->n_cipher_suites] = WLAN_CIPHER_SUITE_AES_CMAC;
	hw->wiphy->n_cipher_suites++;
	if (iwl_mvm_has_new_rx_api(mvm)) {
		mvm->ciphers[hw->wiphy->n_cipher_suites] =
			WLAN_CIPHER_SUITE_BIP_GMAC_128;
		hw->wiphy->n_cipher_suites++;
		mvm->ciphers[hw->wiphy->n_cipher_suites] =
			WLAN_CIPHER_SUITE_BIP_GMAC_256;
		hw->wiphy->n_cipher_suites++;
	}

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_FTM_CALIBRATED)) {
		wiphy_ext_feature_set(hw->wiphy,
				      NL80211_EXT_FEATURE_ENABLE_FTM_RESPONDER);
		hw->wiphy->pmsr_capa = &iwl_mvm_pmsr_capa;
	}

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_BIGTK_SUPPORT))
		wiphy_ext_feature_set(hw->wiphy,
				      NL80211_EXT_FEATURE_BEACON_PROTECTION_CLIENT);

	ieee80211_hw_set(hw, SINGLE_SCAN_ON_ALL_BANDS);
	hw->wiphy->features |=
		NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR |
		NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR |
		NL80211_FEATURE_ND_RANDOM_MAC_ADDR;

	hw->sta_data_size = sizeof(struct iwl_mvm_sta);
	hw->vif_data_size = sizeof(struct iwl_mvm_vif);
	hw->chanctx_data_size = sizeof(u16);
	hw->txq_data_size = sizeof(struct iwl_mvm_txq);

	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_P2P_CLIENT) |
		BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_P2P_GO) |
		BIT(NL80211_IFTYPE_P2P_DEVICE) |
		BIT(NL80211_IFTYPE_ADHOC);

	hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN;
	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_VHT_IBSS);

	/* The new Tx API does not allow to pass the key or keyid of a MPDU to
	 * the hw, preventing us to control which key(id) to use per MPDU.
	 * Till that's fixed we can't use Extended Key ID for the newer cards.
	 */
	if (!iwl_mvm_has_new_tx_api(mvm))
		wiphy_ext_feature_set(hw->wiphy,
				      NL80211_EXT_FEATURE_EXT_KEY_ID);
	hw->wiphy->features |= NL80211_FEATURE_HT_IBSS;

	hw->wiphy->regulatory_flags |= REGULATORY_ENABLE_RELAX_NO_IR;
	if (iwl_mvm_is_lar_supported(mvm))
		hw->wiphy->regulatory_flags |= REGULATORY_WIPHY_SELF_MANAGED;
	else
		hw->wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG |
					       REGULATORY_DISABLE_BEACON_HINTS;

	hw->wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
	hw->wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;
	hw->wiphy->flags |= WIPHY_FLAG_SPLIT_SCAN_6GHZ;

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

	BUILD_BUG_ON(IWL_MVM_SCAN_STOPPING_MASK & IWL_MVM_SCAN_MASK);
	BUILD_BUG_ON(IWL_MVM_MAX_UMAC_SCANS > HWEIGHT32(IWL_MVM_SCAN_MASK) ||
		     IWL_MVM_MAX_LMAC_SCANS > HWEIGHT32(IWL_MVM_SCAN_MASK));

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN))
		mvm->max_scans = IWL_MVM_MAX_UMAC_SCANS;
	else
		mvm->max_scans = IWL_MVM_MAX_LMAC_SCANS;

	if (mvm->nvm_data->bands[NL80211_BAND_2GHZ].n_channels)
		hw->wiphy->bands[NL80211_BAND_2GHZ] =
			&mvm->nvm_data->bands[NL80211_BAND_2GHZ];
	if (mvm->nvm_data->bands[NL80211_BAND_5GHZ].n_channels) {
		hw->wiphy->bands[NL80211_BAND_5GHZ] =
			&mvm->nvm_data->bands[NL80211_BAND_5GHZ];

		if (fw_has_capa(&mvm->fw->ucode_capa,
				IWL_UCODE_TLV_CAPA_BEAMFORMER) &&
		    fw_has_api(&mvm->fw->ucode_capa,
			       IWL_UCODE_TLV_API_LQ_SS_PARAMS))
			hw->wiphy->bands[NL80211_BAND_5GHZ]->vht_cap.cap |=
				IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE;
	}
	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_PSC_CHAN_SUPPORT) &&
	    mvm->nvm_data->bands[NL80211_BAND_6GHZ].n_channels)
		hw->wiphy->bands[NL80211_BAND_6GHZ] =
			&mvm->nvm_data->bands[NL80211_BAND_6GHZ];

	hw->wiphy->hw_version = mvm->trans->hw_id;

	if (iwlmvm_mod_params.power_scheme != IWL_POWER_SCHEME_CAM)
		hw->wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
	else
		hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	hw->wiphy->max_sched_scan_reqs = 1;
	hw->wiphy->max_sched_scan_ssids = PROBE_OPTION_MAX;
	hw->wiphy->max_match_sets = iwl_umac_scan_get_max_profiles(mvm->fw);
	/* we create the 802.11 header and zero length SSID IE. */
	hw->wiphy->max_sched_scan_ie_len =
		SCAN_OFFLOAD_PROBE_REQ_SIZE - 24 - 2;
	hw->wiphy->max_sched_scan_plans = IWL_MAX_SCHED_SCAN_PLANS;
	hw->wiphy->max_sched_scan_plan_interval = U16_MAX;

	/*
	 * the firmware uses u8 for num of iterations, but 0xff is saved for
	 * infinite loop, so the maximum number of iterations is actually 254.
	 */
	hw->wiphy->max_sched_scan_plan_iterations = 254;

	hw->wiphy->features |= NL80211_FEATURE_P2P_GO_CTWIN |
			       NL80211_FEATURE_LOW_PRIORITY_SCAN |
			       NL80211_FEATURE_P2P_GO_OPPPS |
			       NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE |
			       NL80211_FEATURE_DYNAMIC_SMPS |
			       NL80211_FEATURE_STATIC_SMPS |
			       NL80211_FEATURE_SUPPORTS_WMM_ADMISSION;

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_TXPOWER_INSERTION_SUPPORT))
		hw->wiphy->features |= NL80211_FEATURE_TX_POWER_INSERTION;
	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_QUIET_PERIOD_SUPPORT))
		hw->wiphy->features |= NL80211_FEATURE_QUIET;

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT))
		hw->wiphy->features |=
			NL80211_FEATURE_DS_PARAM_SET_IE_IN_PROBES;

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT))
		hw->wiphy->features |= NL80211_FEATURE_WFA_TPC_IE_IN_PROBES;

	if (iwl_fw_lookup_cmd_ver(mvm->fw, WOWLAN_KEK_KCK_MATERIAL,
				  IWL_FW_CMD_VER_UNKNOWN) == 3)
		hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_EXT_KEK_KCK;

	if (fw_has_api(&mvm->fw->ucode_capa,
		       IWL_UCODE_TLV_API_SCAN_TSF_REPORT)) {
		wiphy_ext_feature_set(hw->wiphy,
				      NL80211_EXT_FEATURE_SCAN_START_TIME);
		wiphy_ext_feature_set(hw->wiphy,
				      NL80211_EXT_FEATURE_BSS_PARENT_TSF);
	}

	if (iwl_mvm_is_oce_supported(mvm)) {
		u8 scan_ver = iwl_fw_lookup_cmd_ver(mvm->fw, SCAN_REQ_UMAC, 0);

		wiphy_ext_feature_set(hw->wiphy,
			NL80211_EXT_FEATURE_ACCEPT_BCAST_PROBE_RESP);
		wiphy_ext_feature_set(hw->wiphy,
			NL80211_EXT_FEATURE_FILS_MAX_CHANNEL_TIME);
		wiphy_ext_feature_set(hw->wiphy,
			NL80211_EXT_FEATURE_OCE_PROBE_REQ_HIGH_TX_RATE);

		/* Old firmware also supports probe deferral and suppression */
		if (scan_ver < 15)
			wiphy_ext_feature_set(hw->wiphy,
					      NL80211_EXT_FEATURE_OCE_PROBE_REQ_DEFERRAL_SUPPRESSION);
	}

	if (mvm->nvm_data->sku_cap_11ax_enable &&
	    !iwlwifi_mod_params.disable_11ax) {
		hw->wiphy->iftype_ext_capab = he_iftypes_ext_capa;
		hw->wiphy->num_iftype_ext_capab =
			ARRAY_SIZE(he_iftypes_ext_capa);

		ieee80211_hw_set(hw, SUPPORTS_MULTI_BSSID);
		ieee80211_hw_set(hw, SUPPORTS_ONLY_HE_MULTI_BSSID);
	}

	mvm->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;

#ifdef CONFIG_PM_SLEEP
	if ((unified || mvm->fw->img[IWL_UCODE_WOWLAN].num_sec) &&
	    mvm->trans->ops->d3_suspend &&
	    mvm->trans->ops->d3_resume &&
	    device_can_wakeup(mvm->trans->dev)) {
		mvm->wowlan.flags |= WIPHY_WOWLAN_MAGIC_PKT |
				     WIPHY_WOWLAN_DISCONNECT |
				     WIPHY_WOWLAN_EAP_IDENTITY_REQ |
				     WIPHY_WOWLAN_RFKILL_RELEASE |
				     WIPHY_WOWLAN_NET_DETECT;
		mvm->wowlan.flags |= WIPHY_WOWLAN_SUPPORTS_GTK_REKEY |
				     WIPHY_WOWLAN_GTK_REKEY_FAILURE |
				     WIPHY_WOWLAN_4WAY_HANDSHAKE;

		mvm->wowlan.n_patterns = IWL_WOWLAN_MAX_PATTERNS;
		mvm->wowlan.pattern_min_len = IWL_WOWLAN_MIN_PATTERN_LEN;
		mvm->wowlan.pattern_max_len = IWL_WOWLAN_MAX_PATTERN_LEN;
		mvm->wowlan.max_nd_match_sets =
			iwl_umac_scan_get_max_profiles(mvm->fw);
		hw->wiphy->wowlan = &mvm->wowlan;
	}
#endif

	ret = iwl_mvm_leds_init(mvm);
	if (ret)
		return ret;

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_TDLS_SUPPORT)) {
		IWL_DEBUG_TDLS(mvm, "TDLS supported\n");
		hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
		ieee80211_hw_set(hw, TDLS_WIDER_BW);
	}

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_TDLS_CHANNEL_SWITCH)) {
		IWL_DEBUG_TDLS(mvm, "TDLS channel switch supported\n");
		hw->wiphy->features |= NL80211_FEATURE_TDLS_CHANNEL_SWITCH;
	}

	hw->netdev_features |= mvm->cfg->features;
	if (!iwl_mvm_is_csum_supported(mvm))
		hw->netdev_features &= ~IWL_CSUM_NETIF_FLAGS_MASK;

	if (mvm->cfg->vht_mu_mimo_supported)
		wiphy_ext_feature_set(hw->wiphy,
				      NL80211_EXT_FEATURE_MU_MIMO_AIR_SNIFFER);

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_PROTECTED_TWT))
		wiphy_ext_feature_set(hw->wiphy,
				      NL80211_EXT_FEATURE_PROTECTED_TWT);

	iwl_mvm_vendor_cmds_register(mvm);

	hw->wiphy->available_antennas_tx = iwl_mvm_get_valid_tx_ant(mvm);
	hw->wiphy->available_antennas_rx = iwl_mvm_get_valid_rx_ant(mvm);

	ret = ieee80211_register_hw(mvm->hw);
	if (ret) {
		iwl_mvm_leds_exit(mvm);
	}

	return ret;
}

static void iwl_mvm_tx_skb(struct iwl_mvm *mvm, struct sk_buff *skb,
			   struct ieee80211_sta *sta)
{
	if (likely(sta)) {
		if (likely(iwl_mvm_tx_skb_sta(mvm, skb, sta) == 0))
			return;
	} else {
		if (likely(iwl_mvm_tx_skb_non_sta(mvm, skb) == 0))
			return;
	}

	ieee80211_free_txskb(mvm->hw, skb);
}

static void iwl_mvm_mac_tx(struct ieee80211_hw *hw,
			   struct ieee80211_tx_control *control,
			   struct sk_buff *skb)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct ieee80211_sta *sta = control->sta;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	bool offchannel = IEEE80211_SKB_CB(skb)->flags &
		IEEE80211_TX_CTL_TX_OFFCHAN;

	if (iwl_mvm_is_radio_killed(mvm)) {
		IWL_DEBUG_DROP(mvm, "Dropping - RF/CT KILL\n");
		goto drop;
	}

	if (offchannel &&
	    !test_bit(IWL_MVM_STATUS_ROC_RUNNING, &mvm->status) &&
	    !test_bit(IWL_MVM_STATUS_ROC_AUX_RUNNING, &mvm->status))
		goto drop;

	/*
	 * bufferable MMPDUs or MMPDUs on STA interfaces come via TXQs
	 * so we treat the others as broadcast
	 */
	if (ieee80211_is_mgmt(hdr->frame_control))
		sta = NULL;

	/* If there is no sta, and it's not offchannel - send through AP */
	if (!sta && info->control.vif->type == NL80211_IFTYPE_STATION &&
	    !offchannel) {
		struct iwl_mvm_vif *mvmvif =
			iwl_mvm_vif_from_mac80211(info->control.vif);
		u8 ap_sta_id = READ_ONCE(mvmvif->ap_sta_id);

		if (ap_sta_id < mvm->fw->ucode_capa.num_stations) {
			/* mac80211 holds rcu read lock */
			sta = rcu_dereference(mvm->fw_id_to_mac_id[ap_sta_id]);
			if (IS_ERR_OR_NULL(sta))
				goto drop;
		}
	}

	iwl_mvm_tx_skb(mvm, skb, sta);
	return;
 drop:
	ieee80211_free_txskb(hw, skb);
}

void iwl_mvm_mac_itxq_xmit(struct ieee80211_hw *hw, struct ieee80211_txq *txq)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_txq *mvmtxq = iwl_mvm_txq_from_mac80211(txq);
	struct sk_buff *skb = NULL;

	/*
	 * No need for threads to be pending here, they can leave the first
	 * taker all the work.
	 *
	 * mvmtxq->tx_request logic:
	 *
	 * If 0, no one is currently TXing, set to 1 to indicate current thread
	 * will now start TX and other threads should quit.
	 *
	 * If 1, another thread is currently TXing, set to 2 to indicate to
	 * that thread that there was another request. Since that request may
	 * have raced with the check whether the queue is empty, the TXing
	 * thread should check the queue's status one more time before leaving.
	 * This check is done in order to not leave any TX hanging in the queue
	 * until the next TX invocation (which may not even happen).
	 *
	 * If 2, another thread is currently TXing, and it will already double
	 * check the queue, so do nothing.
	 */
	if (atomic_fetch_add_unless(&mvmtxq->tx_request, 1, 2))
		return;

	rcu_read_lock();
	do {
		while (likely(!test_bit(IWL_MVM_TXQ_STATE_STOP_FULL,
					&mvmtxq->state) &&
			      !test_bit(IWL_MVM_TXQ_STATE_STOP_REDIRECT,
					&mvmtxq->state) &&
			      !test_bit(IWL_MVM_STATUS_IN_D3, &mvm->status))) {
			skb = ieee80211_tx_dequeue(hw, txq);

			if (!skb) {
				if (txq->sta)
					IWL_DEBUG_TX(mvm,
						     "TXQ of sta %pM tid %d is now empty\n",
						     txq->sta->addr,
						     txq->tid);
				break;
			}

			iwl_mvm_tx_skb(mvm, skb, txq->sta);
		}
	} while (atomic_dec_return(&mvmtxq->tx_request));
	rcu_read_unlock();
}

static void iwl_mvm_mac_wake_tx_queue(struct ieee80211_hw *hw,
				      struct ieee80211_txq *txq)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_txq *mvmtxq = iwl_mvm_txq_from_mac80211(txq);

	if (likely(test_bit(IWL_MVM_TXQ_STATE_READY, &mvmtxq->state)) ||
	    !txq->sta) {
		iwl_mvm_mac_itxq_xmit(hw, txq);
		return;
	}

	/* iwl_mvm_mac_itxq_xmit() will later be called by the worker
	 * to handle any packets we leave on the txq now
	 */

	spin_lock_bh(&mvm->add_stream_lock);
	/* The list is being deleted only after the queue is fully allocated. */
	if (list_empty(&mvmtxq->list) &&
	    /* recheck under lock */
	    !test_bit(IWL_MVM_TXQ_STATE_READY, &mvmtxq->state)) {
		list_add_tail(&mvmtxq->list, &mvm->add_stream_txqs);
		schedule_work(&mvm->add_stream_wk);
	}
	spin_unlock_bh(&mvm->add_stream_lock);
}

#define CHECK_BA_TRIGGER(_mvm, _trig, _tid_bm, _tid, _fmt...)		\
	do {								\
		if (!(le16_to_cpu(_tid_bm) & BIT(_tid)))		\
			break;						\
		iwl_fw_dbg_collect_trig(&(_mvm)->fwrt, _trig, _fmt);	\
	} while (0)

static void
iwl_mvm_ampdu_check_trigger(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, u16 tid, u16 rx_ba_ssn,
			    enum ieee80211_ampdu_mlme_action action)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_ba *ba_trig;

	trig = iwl_fw_dbg_trigger_on(&mvm->fwrt, ieee80211_vif_to_wdev(vif),
				     FW_DBG_TRIGGER_BA);
	if (!trig)
		return;

	ba_trig = (void *)trig->data;

	switch (action) {
	case IEEE80211_AMPDU_TX_OPERATIONAL: {
		struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
		struct iwl_mvm_tid_data *tid_data = &mvmsta->tid_data[tid];

		CHECK_BA_TRIGGER(mvm, trig, ba_trig->tx_ba_start, tid,
				 "TX AGG START: MAC %pM tid %d ssn %d\n",
				 sta->addr, tid, tid_data->ssn);
		break;
		}
	case IEEE80211_AMPDU_TX_STOP_CONT:
		CHECK_BA_TRIGGER(mvm, trig, ba_trig->tx_ba_stop, tid,
				 "TX AGG STOP: MAC %pM tid %d\n",
				 sta->addr, tid);
		break;
	case IEEE80211_AMPDU_RX_START:
		CHECK_BA_TRIGGER(mvm, trig, ba_trig->rx_ba_start, tid,
				 "RX AGG START: MAC %pM tid %d ssn %d\n",
				 sta->addr, tid, rx_ba_ssn);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		CHECK_BA_TRIGGER(mvm, trig, ba_trig->rx_ba_stop, tid,
				 "RX AGG STOP: MAC %pM tid %d\n",
				 sta->addr, tid);
		break;
	default:
		break;
	}
}

static int iwl_mvm_mac_ampdu_action(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_ampdu_params *params)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;
	struct ieee80211_sta *sta = params->sta;
	enum ieee80211_ampdu_mlme_action action = params->action;
	u16 tid = params->tid;
	u16 *ssn = &params->ssn;
	u16 buf_size = params->buf_size;
	bool amsdu = params->amsdu;
	u16 timeout = params->timeout;

	IWL_DEBUG_HT(mvm, "A-MPDU action on addr %pM tid %d: action %d\n",
		     sta->addr, tid, action);

	if (!(mvm->nvm_data->sku_cap_11n_enable))
		return -EACCES;

	mutex_lock(&mvm->mutex);

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		if (iwl_mvm_vif_from_mac80211(vif)->ap_sta_id ==
				iwl_mvm_sta_from_mac80211(sta)->sta_id) {
			struct iwl_mvm_vif *mvmvif;
			u16 macid = iwl_mvm_vif_from_mac80211(vif)->id;
			struct iwl_mvm_tcm_mac *mdata = &mvm->tcm.data[macid];

			mdata->opened_rx_ba_sessions = true;
			mvmvif = iwl_mvm_vif_from_mac80211(vif);
			cancel_delayed_work(&mvmvif->uapsd_nonagg_detected_wk);
		}
		if (!iwl_enable_rx_ampdu()) {
			ret = -EINVAL;
			break;
		}
		ret = iwl_mvm_sta_rx_agg(mvm, sta, tid, *ssn, true, buf_size,
					 timeout);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		ret = iwl_mvm_sta_rx_agg(mvm, sta, tid, 0, false, buf_size,
					 timeout);
		break;
	case IEEE80211_AMPDU_TX_START:
		if (!iwl_enable_tx_ampdu()) {
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
		ret = iwl_mvm_sta_tx_agg_oper(mvm, vif, sta, tid,
					      buf_size, amsdu);
		break;
	default:
		WARN_ON_ONCE(1);
		ret = -EINVAL;
		break;
	}

	if (!ret) {
		u16 rx_ba_ssn = 0;

		if (action == IEEE80211_AMPDU_RX_START)
			rx_ba_ssn = *ssn;

		iwl_mvm_ampdu_check_trigger(mvm, vif, sta, tid,
					    rx_ba_ssn, action);
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
	mvmvif->ap_sta_id = IWL_MVM_INVALID_STA;

	spin_lock_bh(&mvm->time_event_lock);
	iwl_mvm_te_clear_data(mvm, &mvmvif->time_event_data);
	spin_unlock_bh(&mvm->time_event_lock);

	mvmvif->phy_ctxt = NULL;
	memset(&mvmvif->bf_data, 0, sizeof(mvmvif->bf_data));
	memset(&mvmvif->probe_resp_data, 0, sizeof(mvmvif->probe_resp_data));
}

static void iwl_mvm_restart_cleanup(struct iwl_mvm *mvm)
{
	iwl_mvm_stop_device(mvm);

	mvm->cur_aid = 0;

	mvm->scan_status = 0;
	mvm->ps_disabled = false;
	mvm->rfkill_safe_init_done = false;

	/* just in case one was running */
	iwl_mvm_cleanup_roc_te(mvm);
	ieee80211_remain_on_channel_expired(mvm->hw);

	iwl_mvm_ftm_restart(mvm);

	/*
	 * cleanup all interfaces, even inactive ones, as some might have
	 * gone down during the HW restart
	 */
	ieee80211_iterate_interfaces(mvm->hw, 0, iwl_mvm_cleanup_iterator, mvm);

	mvm->p2p_device_vif = NULL;

	iwl_mvm_reset_phy_ctxts(mvm);
	memset(mvm->fw_key_table, 0, sizeof(mvm->fw_key_table));
	memset(&mvm->last_bt_notif, 0, sizeof(mvm->last_bt_notif));
	memset(&mvm->last_bt_ci_cmd, 0, sizeof(mvm->last_bt_ci_cmd));

	ieee80211_wake_queues(mvm->hw);

	mvm->rx_ba_sessions = 0;
	mvm->fwrt.dump.conf = FW_DBG_INVALID;
	mvm->monitor_on = false;

	/* keep statistics ticking */
	iwl_mvm_accu_radio_stats(mvm);
}

int __iwl_mvm_mac_start(struct iwl_mvm *mvm)
{
	int ret;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_mvm_mei_get_ownership(mvm);
	if (ret)
		return ret;

	if (mvm->mei_nvm_data) {
		/* We got the NIC, we can now free the MEI NVM data */
		kfree(mvm->mei_nvm_data);
		mvm->mei_nvm_data = NULL;

		/*
		 * We can't free the nvm_data we allocated based on the SAP
		 * data because we registered to cfg80211 with the channels
		 * allocated on mvm->nvm_data. Keep a pointer in temp_nvm_data
		 * just in order to be able free it later.
		 * NULLify nvm_data so that we will read the NVM from the
		 * firmware this time.
		 */
		mvm->temp_nvm_data = mvm->nvm_data;
		mvm->nvm_data = NULL;
	}

	if (test_bit(IWL_MVM_STATUS_HW_RESTART_REQUESTED, &mvm->status)) {
		/*
		 * Now convert the HW_RESTART_REQUESTED flag to IN_HW_RESTART
		 * so later code will - from now on - see that we're doing it.
		 */
		set_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status);
		clear_bit(IWL_MVM_STATUS_HW_RESTART_REQUESTED, &mvm->status);
		/* Clean up some internal and mac80211 state on restart */
		iwl_mvm_restart_cleanup(mvm);
	}
	ret = iwl_mvm_up(mvm);

	iwl_dbg_tlv_time_point(&mvm->fwrt, IWL_FW_INI_TIME_POINT_POST_INIT,
			       NULL);
	iwl_dbg_tlv_time_point(&mvm->fwrt, IWL_FW_INI_TIME_POINT_PERIODIC,
			       NULL);

	mvm->last_reset_or_resume_time_jiffies = jiffies;

	if (ret && test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		/* Something went wrong - we need to finish some cleanup
		 * that normally iwl_mvm_mac_restart_complete() below
		 * would do.
		 */
		clear_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status);
	}

	return ret;
}

static int iwl_mvm_mac_start(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;
	int retry, max_retry = 0;

	mutex_lock(&mvm->mutex);

	/* we are starting the mac not in error flow, and restart is enabled */
	if (!test_bit(IWL_MVM_STATUS_HW_RESTART_REQUESTED, &mvm->status) &&
	    iwlwifi_mod_params.fw_restart) {
		max_retry = IWL_MAX_INIT_RETRY;
		/*
		 * This will prevent mac80211 recovery flows to trigger during
		 * init failures
		 */
		set_bit(IWL_MVM_STATUS_STARTING, &mvm->status);
	}

	for (retry = 0; retry <= max_retry; retry++) {
		ret = __iwl_mvm_mac_start(mvm);
		if (!ret)
			break;

		IWL_ERR(mvm, "mac start retry %d\n", retry);
	}
	clear_bit(IWL_MVM_STATUS_STARTING, &mvm->status);

	mutex_unlock(&mvm->mutex);

	iwl_mvm_mei_set_sw_rfkill_state(mvm);

	return ret;
}

static void iwl_mvm_restart_complete(struct iwl_mvm *mvm)
{
	int ret;

	mutex_lock(&mvm->mutex);

	clear_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status);

	ret = iwl_mvm_update_quotas(mvm, true, NULL);
	if (ret)
		IWL_ERR(mvm, "Failed to update quotas after restart (%d)\n",
			ret);

	iwl_mvm_send_recovery_cmd(mvm, ERROR_RECOVERY_END_OF_RECOVERY);

	/*
	 * If we have TDLS peers, remove them. We don't know the last seqno/PN
	 * of packets the FW sent out, so we must reconnect.
	 */
	iwl_mvm_teardown_tdls_peers(mvm);

	mutex_unlock(&mvm->mutex);
}

static void
iwl_mvm_mac_reconfig_complete(struct ieee80211_hw *hw,
			      enum ieee80211_reconfig_type reconfig_type)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	switch (reconfig_type) {
	case IEEE80211_RECONFIG_TYPE_RESTART:
		iwl_mvm_restart_complete(mvm);
		break;
	case IEEE80211_RECONFIG_TYPE_SUSPEND:
		break;
	}
}

void __iwl_mvm_mac_stop(struct iwl_mvm *mvm)
{
	lockdep_assert_held(&mvm->mutex);

	iwl_mvm_ftm_initiator_smooth_stop(mvm);

	/* firmware counters are obviously reset now, but we shouldn't
	 * partially track so also clear the fw_reset_accu counters.
	 */
	memset(&mvm->accu_radio_stats, 0, sizeof(mvm->accu_radio_stats));

	/* async_handlers_wk is now blocked */

	if (iwl_fw_lookup_cmd_ver(mvm->fw, ADD_STA, 0) < 12)
		iwl_mvm_rm_aux_sta(mvm);

	iwl_mvm_stop_device(mvm);

	iwl_mvm_async_handlers_purge(mvm);
	/* async_handlers_list is empty and will stay empty: HW is stopped */

	/*
	 * Clear IN_HW_RESTART and HW_RESTART_REQUESTED flag when stopping the
	 * hw (as restart_complete() won't be called in this case) and mac80211
	 * won't execute the restart.
	 * But make sure to cleanup interfaces that have gone down before/during
	 * HW restart was requested.
	 */
	if (test_and_clear_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) ||
	    test_and_clear_bit(IWL_MVM_STATUS_HW_RESTART_REQUESTED,
			       &mvm->status))
		ieee80211_iterate_interfaces(mvm->hw, 0,
					     iwl_mvm_cleanup_iterator, mvm);

	/* We shouldn't have any UIDs still set.  Loop over all the UIDs to
	 * make sure there's nothing left there and warn if any is found.
	 */
	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN)) {
		int i;

		for (i = 0; i < mvm->max_scans; i++) {
			if (WARN_ONCE(mvm->scan_uid_status[i],
				      "UMAC scan UID %d status was not cleaned\n",
				      i))
				mvm->scan_uid_status[i] = 0;
		}
	}
}

static void iwl_mvm_mac_stop(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	flush_work(&mvm->async_handlers_wk);
	flush_work(&mvm->add_stream_wk);

	/*
	 * Lock and clear the firmware running bit here already, so that
	 * new commands coming in elsewhere, e.g. from debugfs, will not
	 * be able to proceed. This is important here because one of those
	 * debugfs files causes the firmware dump to be triggered, and if we
	 * don't stop debugfs accesses before canceling that it could be
	 * retriggered after we flush it but before we've cleared the bit.
	 */
	clear_bit(IWL_MVM_STATUS_FIRMWARE_RUNNING, &mvm->status);

	cancel_delayed_work_sync(&mvm->cs_tx_unblock_dwork);
	cancel_delayed_work_sync(&mvm->scan_timeout_dwork);

	/*
	 * The work item could be running or queued if the
	 * ROC time event stops just as we get here.
	 */
	flush_work(&mvm->roc_done_wk);

	iwl_mvm_mei_set_sw_rfkill_state(mvm);

	mutex_lock(&mvm->mutex);
	__iwl_mvm_mac_stop(mvm);
	mutex_unlock(&mvm->mutex);

	/*
	 * The worker might have been waiting for the mutex, let it run and
	 * discover that its list is now empty.
	 */
	cancel_work_sync(&mvm->async_handlers_wk);
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
				s16 tx_power)
{
	u32 cmd_id = REDUCE_TX_POWER_CMD;
	int len;
	struct iwl_dev_tx_power_cmd cmd = {
		.common.set_mode = cpu_to_le32(IWL_TX_POWER_MODE_SET_MAC),
		.common.mac_context_id =
			cpu_to_le32(iwl_mvm_vif_from_mac80211(vif)->id),
		.common.pwr_restriction = cpu_to_le16(8 * tx_power),
	};
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd_id,
					   IWL_FW_CMD_VER_UNKNOWN);

	if (tx_power == IWL_DEFAULT_MAX_TX_POWER)
		cmd.common.pwr_restriction = cpu_to_le16(IWL_DEV_MAX_TX_POWER);

	if (cmd_ver == 7)
		len = sizeof(cmd.v7);
	else if (cmd_ver == 6)
		len = sizeof(cmd.v6);
	else if (fw_has_api(&mvm->fw->ucode_capa,
			    IWL_UCODE_TLV_API_REDUCE_TX_POWER))
		len = sizeof(cmd.v5);
	else if (fw_has_capa(&mvm->fw->ucode_capa,
			     IWL_UCODE_TLV_CAPA_TX_POWER_ACK))
		len = sizeof(cmd.v4);
	else
		len = sizeof(cmd.v3);

	/* all structs have the same common part, add it */
	len += sizeof(cmd.common);

	return iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, len, &cmd);
}

static int iwl_mvm_post_channel_switch(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);

	if (vif->type == NL80211_IFTYPE_STATION) {
		struct iwl_mvm_sta *mvmsta;

		mvmvif->csa_bcn_pending = false;
		mvmsta = iwl_mvm_sta_from_staid_protected(mvm,
							  mvmvif->ap_sta_id);

		if (WARN_ON(!mvmsta)) {
			ret = -EIO;
			goto out_unlock;
		}

		iwl_mvm_sta_modify_disable_tx(mvm, mvmsta, false);

		iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);

		if (!fw_has_capa(&mvm->fw->ucode_capa,
				 IWL_UCODE_TLV_CAPA_CHANNEL_SWITCH_CMD)) {
			ret = iwl_mvm_enable_beacon_filter(mvm, vif, 0);
			if (ret)
				goto out_unlock;

			iwl_mvm_stop_session_protection(mvm, vif);
		}
	}

	mvmvif->ps_disabled = false;

	ret = iwl_mvm_power_update_ps(mvm);

out_unlock:
	if (mvmvif->csa_failed)
		ret = -EIO;
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void iwl_mvm_abort_channel_switch(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_chan_switch_te_cmd cmd = {
		.mac_id = cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
							  mvmvif->color)),
		.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE),
	};

	/*
	 * In the new flow since FW is in charge of the timing,
	 * if driver has canceled the channel switch he will receive the
	 * CHANNEL_SWITCH_START_NOTIF notification from FW and then cancel it
	 */
	if (iwl_fw_lookup_notif_ver(mvm->fw, MAC_CONF_GROUP,
				    CHANNEL_SWITCH_ERROR_NOTIF, 0))
		return;

	IWL_DEBUG_MAC80211(mvm, "Abort CSA on mac %d\n", mvmvif->id);

	mutex_lock(&mvm->mutex);
	if (!fw_has_capa(&mvm->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_CHANNEL_SWITCH_CMD))
		iwl_mvm_remove_csa_period(mvm, vif);
	else
		WARN_ON(iwl_mvm_send_cmd_pdu(mvm,
					     WIDE_ID(MAC_CONF_GROUP,
						     CHANNEL_SWITCH_TIME_EVENT_CMD),
					     0, sizeof(cmd), &cmd));
	mvmvif->csa_failed = true;
	mutex_unlock(&mvm->mutex);

	iwl_mvm_post_channel_switch(hw, vif);
}

static void iwl_mvm_channel_switch_disconnect_wk(struct work_struct *wk)
{
	struct iwl_mvm_vif *mvmvif;
	struct ieee80211_vif *vif;

	mvmvif = container_of(wk, struct iwl_mvm_vif, csa_work.work);
	vif = container_of((void *)mvmvif, struct ieee80211_vif, drv_priv);

	/* Trigger disconnect (should clear the CSA state) */
	ieee80211_chswitch_done(vif, false);
}

static int iwl_mvm_mac_add_interface(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	mvmvif->mvm = mvm;
	RCU_INIT_POINTER(mvmvif->probe_resp_data, NULL);

	/*
	 * Not much to do here. The stack will not allow interface
	 * types or combinations that we didn't advertise, so we
	 * don't really have to check the types.
	 */

	mutex_lock(&mvm->mutex);

	/* make sure that beacon statistics don't go backwards with FW reset */
	if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
		mvmvif->beacon_stats.accu_num_beacons +=
			mvmvif->beacon_stats.num_beacons;

	/* Allocate resources for the MAC context, and add it to the fw  */
	ret = iwl_mvm_mac_ctxt_init(mvm, vif);
	if (ret)
		goto out_unlock;

	rcu_assign_pointer(mvm->vif_id_to_mac[mvmvif->id], vif);

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
		ret = iwl_mvm_alloc_bcast_sta(mvm, vif);
		if (ret) {
			IWL_ERR(mvm, "Failed to allocate bcast sta\n");
			goto out_unlock;
		}

		/*
		 * Only queue for this station is the mcast queue,
		 * which shouldn't be in TFD mask anyway
		 */
		ret = iwl_mvm_allocate_int_sta(mvm, &mvmvif->mcast_sta,
					       0, vif->type,
					       IWL_STA_MULTICAST);
		if (ret)
			goto out_unlock;

		iwl_mvm_vif_dbgfs_register(mvm, vif);
		goto out_unlock;
	}

	mvmvif->features |= hw->netdev_features;

	ret = iwl_mvm_mac_ctxt_add(mvm, vif);
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
		ret = iwl_mvm_binding_add_vif(mvm, vif);
		if (ret)
			goto out_unref_phy;

		ret = iwl_mvm_add_p2p_bcast_sta(mvm, vif);
		if (ret)
			goto out_unbind;

		/* Save a pointer to p2p device vif, so it can later be used to
		 * update the p2p device MAC when a GO is started/stopped */
		mvm->p2p_device_vif = vif;
	}

	iwl_mvm_tcm_add_vif(mvm, vif);
	INIT_DELAYED_WORK(&mvmvif->csa_work,
			  iwl_mvm_channel_switch_disconnect_wk);

	if (vif->type == NL80211_IFTYPE_MONITOR)
		mvm->monitor_on = true;

	iwl_mvm_vif_dbgfs_register(mvm, vif);

	if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) &&
	    vif->type == NL80211_IFTYPE_STATION && !vif->p2p &&
	    !mvm->csme_vif && mvm->mei_registered) {
		iwl_mei_set_nic_info(vif->addr, mvm->nvm_data->hw_addr);
		iwl_mei_set_netdev(ieee80211_vif_to_wdev(vif)->netdev);
		mvm->csme_vif = vif;
	}

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
 out_unlock:
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void iwl_mvm_prepare_mac_removal(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif)
{
	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		/*
		 * Flush the ROC worker which will flush the OFFCHANNEL queue.
		 * We assume here that all the packets sent to the OFFCHANNEL
		 * queue are sent in ROC session.
		 */
		flush_work(&mvm->roc_done_wk);
	}
}

static void iwl_mvm_mac_remove_interface(struct ieee80211_hw *hw,
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

	probe_data = rcu_dereference_protected(mvmvif->probe_resp_data,
					       lockdep_is_held(&mvm->mutex));
	RCU_INIT_POINTER(mvmvif->probe_resp_data, NULL);
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
		iwl_mvm_dealloc_int_sta(mvm, &mvmvif->mcast_sta);
		iwl_mvm_dealloc_bcast_sta(mvm, vif);
		goto out_release;
	}

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		mvm->p2p_device_vif = NULL;
		iwl_mvm_rm_p2p_bcast_sta(mvm, vif);
		iwl_mvm_binding_remove_vif(mvm, vif);
		iwl_mvm_phy_ctxt_unref(mvm, mvmvif->phy_ctxt);
		mvmvif->phy_ctxt = NULL;
	}

	iwl_mvm_power_update_mac(mvm);
	iwl_mvm_mac_ctxt_remove(mvm, vif);

	RCU_INIT_POINTER(mvm->vif_id_to_mac[mvmvif->id], NULL);

	if (vif->type == NL80211_IFTYPE_MONITOR)
		mvm->monitor_on = false;

out_release:
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
	struct iwl_host_cmd hcmd = {
		.id = MCAST_FILTER_CMD,
		.flags = CMD_ASYNC,
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
	};
	int ret, len;

	/* if we don't have free ports, mcast frames will be dropped */
	if (WARN_ON_ONCE(data->port_id >= MAX_PORT_ID_NUM))
		return;

	if (vif->type != NL80211_IFTYPE_STATION ||
	    !vif->cfg.assoc)
		return;

	cmd->port_id = data->port_id++;
	memcpy(cmd->bssid, vif->bss_conf.bssid, ETH_ALEN);
	len = roundup(sizeof(*cmd) + cmd->count * ETH_ALEN, 4);

	hcmd.len[0] = len;
	hcmd.data[0] = cmd;

	ret = iwl_mvm_send_cmd(mvm, &hcmd);
	if (ret)
		IWL_ERR(mvm, "mcast filter cmd error. ret=%d\n", ret);
}

static void iwl_mvm_recalc_multicast(struct iwl_mvm *mvm)
{
	struct iwl_mvm_mc_iter_data iter_data = {
		.mvm = mvm,
	};
	int ret;

	lockdep_assert_held(&mvm->mutex);

	if (WARN_ON_ONCE(!mvm->mcast_filter_cmd))
		return;

	ieee80211_iterate_active_interfaces_atomic(
		mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
		iwl_mvm_mc_iface_iterator, &iter_data);

	/*
	 * Send a (synchronous) ech command so that we wait for the
	 * multiple asynchronous MCAST_FILTER_CMD commands sent by
	 * the interface iterator. Otherwise, we might get here over
	 * and over again (by userspace just sending a lot of these)
	 * and the CPU can send them faster than the firmware can
	 * process them.
	 * Note that the CPU is still faster - but with this we'll
	 * actually send fewer commands overall because the CPU will
	 * not schedule the work in mac80211 as frequently if it's
	 * still running when rescheduled (possibly multiple times).
	 */
	ret = iwl_mvm_send_cmd_pdu(mvm, ECHO_CMD, 0, 0, NULL);
	if (ret)
		IWL_ERR(mvm, "Failed to synchronize multicast groups update\n");
}

static u64 iwl_mvm_prepare_multicast(struct ieee80211_hw *hw,
				     struct netdev_hw_addr_list *mc_list)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mcast_filter_cmd *cmd;
	struct netdev_hw_addr *addr;
	int addr_count;
	bool pass_all;
	int len;

	addr_count = netdev_hw_addr_list_count(mc_list);
	pass_all = addr_count > MAX_MCAST_FILTERING_ADDRESSES ||
		   IWL_MVM_FW_MCAST_FILTER_PASS_ALL;
	if (pass_all)
		addr_count = 0;

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

	if (changed_flags & FIF_ALLMULTI)
		cmd->pass_all = !!(*total_flags & FIF_ALLMULTI);

	if (cmd->pass_all)
		cmd->count = 0;

	iwl_mvm_recalc_multicast(mvm);
out:
	mutex_unlock(&mvm->mutex);
	*total_flags = 0;
}

static void iwl_mvm_config_iface_filter(struct ieee80211_hw *hw,
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
	iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_update_mu_groups(struct iwl_mvm *mvm,
				    struct ieee80211_vif *vif)
{
	struct iwl_mu_group_mgmt_cmd cmd = {};

	memcpy(cmd.membership_status, vif->bss_conf.mu_group.membership,
	       WLAN_MEMBERSHIP_LEN);
	memcpy(cmd.user_position, vif->bss_conf.mu_group.position,
	       WLAN_USER_POSITION_LEN);

	return iwl_mvm_send_cmd_pdu(mvm,
				    WIDE_ID(DATA_PATH_GROUP,
					    UPDATE_MU_GROUPS_CMD),
				    0, sizeof(cmd), &cmd);
}

static void iwl_mvm_mu_mimo_iface_iterator(void *_data, u8 *mac,
					   struct ieee80211_vif *vif)
{
	if (vif->bss_conf.mu_mimo_owner) {
		struct iwl_mu_group_mgmt_notif *notif = _data;

		/*
		 * MU-MIMO Group Id action frame is little endian. We treat
		 * the data received from firmware as if it came from the
		 * action frame, so no conversion is needed.
		 */
		ieee80211_update_mu_groups(vif, 0,
					   (u8 *)&notif->membership_status,
					   (u8 *)&notif->user_position);
	}
}

void iwl_mvm_mu_mimo_grp_notif(struct iwl_mvm *mvm,
			       struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mu_group_mgmt_notif *notif = (void *)pkt->data;

	ieee80211_iterate_active_interfaces_atomic(
			mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
			iwl_mvm_mu_mimo_iface_iterator, notif);
}

static u8 iwl_mvm_he_get_ppe_val(u8 *ppe, u8 ppe_pos_bit)
{
	u8 byte_num = ppe_pos_bit / 8;
	u8 bit_num = ppe_pos_bit % 8;
	u8 residue_bits;
	u8 res;

	if (bit_num <= 5)
		return (ppe[byte_num] >> bit_num) &
		       (BIT(IEEE80211_PPE_THRES_INFO_PPET_SIZE) - 1);

	/*
	 * If bit_num > 5, we have to combine bits with next byte.
	 * Calculate how many bits we need to take from current byte (called
	 * here "residue_bits"), and add them to bits from next byte.
	 */

	residue_bits = 8 - bit_num;

	res = (ppe[byte_num + 1] &
	       (BIT(IEEE80211_PPE_THRES_INFO_PPET_SIZE - residue_bits) - 1)) <<
	      residue_bits;
	res += (ppe[byte_num] >> bit_num) & (BIT(residue_bits) - 1);

	return res;
}

static void iwl_mvm_parse_ppe(struct iwl_mvm *mvm,
			      struct iwl_he_pkt_ext_v2 *pkt_ext, u8 nss,
			      u8 ru_index_bitmap, u8 *ppe, u8 ppe_pos_bit)
{
	int i;

	/*
	* FW currently supports only nss == MAX_HE_SUPP_NSS
	*
	* If nss > MAX: we can ignore values we don't support
	* If nss < MAX: we can set zeros in other streams
	*/
	if (nss > MAX_HE_SUPP_NSS) {
		IWL_DEBUG_INFO(mvm, "Got NSS = %d - trimming to %d\n", nss,
			       MAX_HE_SUPP_NSS);
		nss = MAX_HE_SUPP_NSS;
	}

	for (i = 0; i < nss; i++) {
		u8 ru_index_tmp = ru_index_bitmap << 1;
		u8 low_th = IWL_HE_PKT_EXT_NONE, high_th = IWL_HE_PKT_EXT_NONE;
		u8 bw;

		for (bw = 0;
		     bw < ARRAY_SIZE(pkt_ext->pkt_ext_qam_th[i]);
		     bw++) {
			ru_index_tmp >>= 1;

			if (!(ru_index_tmp & 1))
				continue;

			high_th = iwl_mvm_he_get_ppe_val(ppe, ppe_pos_bit);
			ppe_pos_bit += IEEE80211_PPE_THRES_INFO_PPET_SIZE;
			low_th = iwl_mvm_he_get_ppe_val(ppe, ppe_pos_bit);
			ppe_pos_bit += IEEE80211_PPE_THRES_INFO_PPET_SIZE;

			pkt_ext->pkt_ext_qam_th[i][bw][0] = low_th;
			pkt_ext->pkt_ext_qam_th[i][bw][1] = high_th;
		}
	}
}

static void iwl_mvm_set_pkt_ext_from_he_ppe(struct iwl_mvm *mvm,
					    struct ieee80211_sta *sta,
					    struct iwl_he_pkt_ext_v2 *pkt_ext)
{
	u8 nss = (sta->deflink.he_cap.ppe_thres[0] & IEEE80211_PPE_THRES_NSS_MASK) + 1;
	u8 *ppe = &sta->deflink.he_cap.ppe_thres[0];
	u8 ru_index_bitmap =
		u8_get_bits(*ppe,
			    IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK);
	/* Starting after PPE header */
	u8 ppe_pos_bit = IEEE80211_HE_PPE_THRES_INFO_HEADER_SIZE;

	iwl_mvm_parse_ppe(mvm, pkt_ext, nss, ru_index_bitmap, ppe, ppe_pos_bit);
}

static void iwl_mvm_set_pkt_ext_from_nominal_padding(struct iwl_he_pkt_ext_v2 *pkt_ext,
						     u8 nominal_padding,
						     u32 *flags)
{
	int low_th = -1;
	int high_th = -1;
	int i;

	switch (nominal_padding) {
	case IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_0US:
		low_th = IWL_HE_PKT_EXT_NONE;
		high_th = IWL_HE_PKT_EXT_NONE;
		break;
	case IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_8US:
		low_th = IWL_HE_PKT_EXT_BPSK;
		high_th = IWL_HE_PKT_EXT_NONE;
		break;
	case IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US:
		low_th = IWL_HE_PKT_EXT_NONE;
		high_th = IWL_HE_PKT_EXT_BPSK;
		break;
	}

	/* Set the PPE thresholds accordingly */
	if (low_th >= 0 && high_th >= 0) {
		for (i = 0; i < MAX_HE_SUPP_NSS; i++) {
			u8 bw;

			for (bw = 0;
			     bw < ARRAY_SIZE(pkt_ext->pkt_ext_qam_th[i]);
			     bw++) {
				pkt_ext->pkt_ext_qam_th[i][bw][0] = low_th;
				pkt_ext->pkt_ext_qam_th[i][bw][1] = high_th;
			}
		}

		*flags |= STA_CTXT_HE_PACKET_EXT;
	}
}

static void iwl_mvm_cfg_he_sta(struct iwl_mvm *mvm,
			       struct ieee80211_vif *vif, u8 sta_id)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_he_sta_context_cmd_v3 sta_ctxt_cmd = {
		.sta_id = sta_id,
		.tid_limit = IWL_MAX_TID_COUNT,
		.bss_color = vif->bss_conf.he_bss_color.color,
		.htc_trig_based_pkt_ext = vif->bss_conf.htc_trig_based_pkt_ext,
		.frame_time_rts_th =
			cpu_to_le16(vif->bss_conf.frame_time_rts_th),
	};
	struct iwl_he_sta_context_cmd_v2 sta_ctxt_cmd_v2 = {};
	u32 cmd_id = WIDE_ID(DATA_PATH_GROUP, STA_HE_CTXT_CMD);
	u8 ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd_id, 2);
	int size;
	struct ieee80211_sta *sta;
	u32 flags;
	int i;
	const struct ieee80211_sta_he_cap *own_he_cap = NULL;
	struct ieee80211_chanctx_conf *chanctx_conf;
	const struct ieee80211_supported_band *sband;
	void *cmd;

	if (!fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_MBSSID_HE))
		ver = 1;

	switch (ver) {
	case 1:
		/* same layout as v2 except some data at the end */
		cmd = &sta_ctxt_cmd_v2;
		size = sizeof(struct iwl_he_sta_context_cmd_v1);
		break;
	case 2:
		cmd = &sta_ctxt_cmd_v2;
		size = sizeof(struct iwl_he_sta_context_cmd_v2);
		break;
	case 3:
		cmd = &sta_ctxt_cmd;
		size = sizeof(struct iwl_he_sta_context_cmd_v3);
		break;
	default:
		IWL_ERR(mvm, "bad STA_HE_CTXT_CMD version %d\n", ver);
		return;
	}

	rcu_read_lock();

	chanctx_conf = rcu_dereference(vif->bss_conf.chanctx_conf);
	if (WARN_ON(!chanctx_conf)) {
		rcu_read_unlock();
		return;
	}

	sband = mvm->hw->wiphy->bands[chanctx_conf->def.chan->band];
	own_he_cap = ieee80211_get_he_iftype_cap(sband,
						 ieee80211_vif_type_p2p(vif));

	sta = rcu_dereference(mvm->fw_id_to_mac_id[sta_ctxt_cmd.sta_id]);
	if (IS_ERR_OR_NULL(sta)) {
		rcu_read_unlock();
		WARN(1, "Can't find STA to configure HE\n");
		return;
	}

	if (!sta->deflink.he_cap.has_he) {
		rcu_read_unlock();
		return;
	}

	flags = 0;

	/* Block 26-tone RU OFDMA transmissions */
	if (mvmvif->he_ru_2mhz_block)
		flags |= STA_CTXT_HE_RU_2MHZ_BLOCK;

	/* HTC flags */
	if (sta->deflink.he_cap.he_cap_elem.mac_cap_info[0] &
	    IEEE80211_HE_MAC_CAP0_HTC_HE)
		sta_ctxt_cmd.htc_flags |= cpu_to_le32(IWL_HE_HTC_SUPPORT);
	if ((sta->deflink.he_cap.he_cap_elem.mac_cap_info[1] &
	      IEEE80211_HE_MAC_CAP1_LINK_ADAPTATION) ||
	    (sta->deflink.he_cap.he_cap_elem.mac_cap_info[2] &
	      IEEE80211_HE_MAC_CAP2_LINK_ADAPTATION)) {
		u8 link_adap =
			((sta->deflink.he_cap.he_cap_elem.mac_cap_info[2] &
			  IEEE80211_HE_MAC_CAP2_LINK_ADAPTATION) << 1) +
			 (sta->deflink.he_cap.he_cap_elem.mac_cap_info[1] &
			  IEEE80211_HE_MAC_CAP1_LINK_ADAPTATION);

		if (link_adap == 2)
			sta_ctxt_cmd.htc_flags |=
				cpu_to_le32(IWL_HE_HTC_LINK_ADAP_UNSOLICITED);
		else if (link_adap == 3)
			sta_ctxt_cmd.htc_flags |=
				cpu_to_le32(IWL_HE_HTC_LINK_ADAP_BOTH);
	}
	if (sta->deflink.he_cap.he_cap_elem.mac_cap_info[2] & IEEE80211_HE_MAC_CAP2_BSR)
		sta_ctxt_cmd.htc_flags |= cpu_to_le32(IWL_HE_HTC_BSR_SUPP);
	if (sta->deflink.he_cap.he_cap_elem.mac_cap_info[3] &
	    IEEE80211_HE_MAC_CAP3_OMI_CONTROL)
		sta_ctxt_cmd.htc_flags |= cpu_to_le32(IWL_HE_HTC_OMI_SUPP);
	if (sta->deflink.he_cap.he_cap_elem.mac_cap_info[4] & IEEE80211_HE_MAC_CAP4_BQR)
		sta_ctxt_cmd.htc_flags |= cpu_to_le32(IWL_HE_HTC_BQR_SUPP);

	/*
	 * Initialize the PPE thresholds to "None" (7), as described in Table
	 * 9-262ac of 80211.ax/D3.0.
	 */
	memset(&sta_ctxt_cmd.pkt_ext, IWL_HE_PKT_EXT_NONE,
	       sizeof(sta_ctxt_cmd.pkt_ext));

	/* If PPE Thresholds exist, parse them into a FW-familiar format. */
	if (sta->deflink.he_cap.he_cap_elem.phy_cap_info[6] &
		IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) {
		iwl_mvm_set_pkt_ext_from_he_ppe(mvm, sta,
						&sta_ctxt_cmd.pkt_ext);
		flags |= STA_CTXT_HE_PACKET_EXT;
	/* PPE Thresholds doesn't exist - set the API PPE values
	* according to Common Nominal Packet Padding fiels. */
	} else {
		u8 nominal_padding =
			u8_get_bits(sta->deflink.he_cap.he_cap_elem.phy_cap_info[9],
				    IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK);
		if (nominal_padding != IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_RESERVED)
			iwl_mvm_set_pkt_ext_from_nominal_padding(&sta_ctxt_cmd.pkt_ext,
								 nominal_padding,
								 &flags);
	}

	if (sta->deflink.he_cap.he_cap_elem.mac_cap_info[2] &
	    IEEE80211_HE_MAC_CAP2_32BIT_BA_BITMAP)
		flags |= STA_CTXT_HE_32BIT_BA_BITMAP;

	if (sta->deflink.he_cap.he_cap_elem.mac_cap_info[2] &
	    IEEE80211_HE_MAC_CAP2_ACK_EN)
		flags |= STA_CTXT_HE_ACK_ENABLED;

	rcu_read_unlock();

	/* Mark MU EDCA as enabled, unless none detected on some AC */
	flags |= STA_CTXT_HE_MU_EDCA_CW;
	for (i = 0; i < IEEE80211_NUM_ACS; i++) {
		struct ieee80211_he_mu_edca_param_ac_rec *mu_edca =
			&mvmvif->queue_params[i].mu_edca_param_rec;
		u8 ac = iwl_mvm_mac80211_ac_to_ucode_ac(i);

		if (!mvmvif->queue_params[i].mu_edca) {
			flags &= ~STA_CTXT_HE_MU_EDCA_CW;
			break;
		}

		sta_ctxt_cmd.trig_based_txf[ac].cwmin =
			cpu_to_le16(mu_edca->ecw_min_max & 0xf);
		sta_ctxt_cmd.trig_based_txf[ac].cwmax =
			cpu_to_le16((mu_edca->ecw_min_max & 0xf0) >> 4);
		sta_ctxt_cmd.trig_based_txf[ac].aifsn =
			cpu_to_le16(mu_edca->aifsn);
		sta_ctxt_cmd.trig_based_txf[ac].mu_time =
			cpu_to_le16(mu_edca->mu_edca_timer);
	}


	if (vif->bss_conf.uora_exists) {
		flags |= STA_CTXT_HE_TRIG_RND_ALLOC;

		sta_ctxt_cmd.rand_alloc_ecwmin =
			vif->bss_conf.uora_ocw_range & 0x7;
		sta_ctxt_cmd.rand_alloc_ecwmax =
			(vif->bss_conf.uora_ocw_range >> 3) & 0x7;
	}

	if (own_he_cap && !(own_he_cap->he_cap_elem.mac_cap_info[2] &
			    IEEE80211_HE_MAC_CAP2_ACK_EN))
		flags |= STA_CTXT_HE_NIC_NOT_ACK_ENABLED;

	if (vif->bss_conf.nontransmitted) {
		flags |= STA_CTXT_HE_REF_BSSID_VALID;
		ether_addr_copy(sta_ctxt_cmd.ref_bssid_addr,
				vif->bss_conf.transmitter_bssid);
		sta_ctxt_cmd.max_bssid_indicator =
			vif->bss_conf.bssid_indicator;
		sta_ctxt_cmd.bssid_index = vif->bss_conf.bssid_index;
		sta_ctxt_cmd.ema_ap = vif->bss_conf.ema_ap;
		sta_ctxt_cmd.profile_periodicity =
			vif->bss_conf.profile_periodicity;
	}

	sta_ctxt_cmd.flags = cpu_to_le32(flags);

	if (ver < 3) {
		/* fields before pkt_ext */
		BUILD_BUG_ON(offsetof(typeof(sta_ctxt_cmd), pkt_ext) !=
			     offsetof(typeof(sta_ctxt_cmd_v2), pkt_ext));
		memcpy(&sta_ctxt_cmd_v2, &sta_ctxt_cmd,
		       offsetof(typeof(sta_ctxt_cmd), pkt_ext));

		/* pkt_ext */
		for (i = 0;
		     i < ARRAY_SIZE(sta_ctxt_cmd_v2.pkt_ext.pkt_ext_qam_th);
		     i++) {
			u8 bw;

			for (bw = 0;
			     bw < ARRAY_SIZE(sta_ctxt_cmd_v2.pkt_ext.pkt_ext_qam_th[i]);
			     bw++) {
				BUILD_BUG_ON(sizeof(sta_ctxt_cmd.pkt_ext.pkt_ext_qam_th[i][bw]) !=
					     sizeof(sta_ctxt_cmd_v2.pkt_ext.pkt_ext_qam_th[i][bw]));

				memcpy(&sta_ctxt_cmd_v2.pkt_ext.pkt_ext_qam_th[i][bw],
				       &sta_ctxt_cmd.pkt_ext.pkt_ext_qam_th[i][bw],
				       sizeof(sta_ctxt_cmd.pkt_ext.pkt_ext_qam_th[i][bw]));
			}
		}

		/* fields after pkt_ext */
		BUILD_BUG_ON(sizeof(sta_ctxt_cmd) -
			     offsetofend(typeof(sta_ctxt_cmd), pkt_ext) !=
			     sizeof(sta_ctxt_cmd_v2) -
			     offsetofend(typeof(sta_ctxt_cmd_v2), pkt_ext));
		memcpy((u8 *)&sta_ctxt_cmd_v2 +
				offsetofend(typeof(sta_ctxt_cmd_v2), pkt_ext),
		       (u8 *)&sta_ctxt_cmd +
				offsetofend(typeof(sta_ctxt_cmd), pkt_ext),
		       sizeof(sta_ctxt_cmd) -
				offsetofend(typeof(sta_ctxt_cmd), pkt_ext));
		sta_ctxt_cmd_v2.reserved3 = 0;
	}

	if (iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, size, cmd))
		IWL_ERR(mvm, "Failed to config FW to work HE!\n");
}

static void iwl_mvm_protect_assoc(struct iwl_mvm *mvm,
				  struct ieee80211_vif *vif,
				  u32 duration_override)
{
	u32 duration = IWL_MVM_TE_SESSION_PROTECTION_MAX_TIME_MS;
	u32 min_duration = IWL_MVM_TE_SESSION_PROTECTION_MIN_TIME_MS;

	if (duration_override > duration)
		duration = duration_override;

	/* Try really hard to protect the session and hear a beacon
	 * The new session protection command allows us to protect the
	 * session for a much longer time since the firmware will internally
	 * create two events: a 300TU one with a very high priority that
	 * won't be fragmented which should be enough for 99% of the cases,
	 * and another one (which we configure here to be 900TU long) which
	 * will have a slightly lower priority, but more importantly, can be
	 * fragmented so that it'll allow other activities to run.
	 */
	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_SESSION_PROT_CMD))
		iwl_mvm_schedule_session_protection(mvm, vif, 900,
						    min_duration, false);
	else
		iwl_mvm_protect_session(mvm, vif, duration,
					min_duration, 500, false);
}

static void iwl_mvm_bss_info_changed_station(struct iwl_mvm *mvm,
					     struct ieee80211_vif *vif,
					     struct ieee80211_bss_conf *bss_conf,
					     u64 changes)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	/*
	 * Re-calculate the tsf id, as the leader-follower relations depend
	 * on the beacon interval, which was not known when the station
	 * interface was added.
	 */
	if (changes & BSS_CHANGED_ASSOC && vif->cfg.assoc) {
		if (vif->bss_conf.he_support &&
		    !iwlwifi_mod_params.disable_11ax)
			iwl_mvm_cfg_he_sta(mvm, vif, mvmvif->ap_sta_id);

		iwl_mvm_mac_ctxt_recalc_tsf_id(mvm, vif);
	}

	/* Update MU EDCA params */
	if (changes & BSS_CHANGED_QOS && mvmvif->associated &&
	    vif->cfg.assoc && vif->bss_conf.he_support &&
	    !iwlwifi_mod_params.disable_11ax)
		iwl_mvm_cfg_he_sta(mvm, vif, mvmvif->ap_sta_id);

	/*
	 * If we're not associated yet, take the (new) BSSID before associating
	 * so the firmware knows. If we're already associated, then use the old
	 * BSSID here, and we'll send a cleared one later in the CHANGED_ASSOC
	 * branch for disassociation below.
	 */
	if (changes & BSS_CHANGED_BSSID && !mvmvif->associated)
		memcpy(mvmvif->bssid, bss_conf->bssid, ETH_ALEN);

	ret = iwl_mvm_mac_ctxt_changed(mvm, vif, false, mvmvif->bssid);
	if (ret)
		IWL_ERR(mvm, "failed to update MAC %pM\n", vif->addr);

	/* after sending it once, adopt mac80211 data */
	memcpy(mvmvif->bssid, bss_conf->bssid, ETH_ALEN);
	mvmvif->associated = vif->cfg.assoc;

	if (changes & BSS_CHANGED_ASSOC) {
		if (vif->cfg.assoc) {
			/* clear statistics to get clean beacon counter */
			iwl_mvm_request_statistics(mvm, true);
			memset(&mvmvif->beacon_stats, 0,
			       sizeof(mvmvif->beacon_stats));

			/* add quota for this interface */
			ret = iwl_mvm_update_quotas(mvm, true, NULL);
			if (ret) {
				IWL_ERR(mvm, "failed to update quotas\n");
				return;
			}

			if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART,
				     &mvm->status) &&
			    !fw_has_capa(&mvm->fw->ucode_capa,
					 IWL_UCODE_TLV_CAPA_SESSION_PROT_CMD)) {
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
				 *
				 * For new firmware versions, rely on the
				 * firmware. This is relevant for DCM scenarios
				 * only anyway.
				 */
				u32 dur = (11 * vif->bss_conf.beacon_int) / 10;
				iwl_mvm_protect_session(mvm, vif, dur, dur,
							5 * dur, false);
			} else if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART,
					     &mvm->status) &&
				   !vif->bss_conf.dtim_period) {
				/*
				 * If we're not restarting and still haven't
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
						    IEEE80211_SMPS_DYNAMIC);
			}
		} else if (mvmvif->ap_sta_id != IWL_MVM_INVALID_STA) {
			iwl_mvm_mei_host_disassociated(mvm);
			/*
			 * If update fails - SF might be running in associated
			 * mode while disassociated - which is forbidden.
			 */
			ret = iwl_mvm_sf_update(mvm, vif, false);
			WARN_ONCE(ret &&
				  !test_bit(IWL_MVM_STATUS_HW_RESTART_REQUESTED,
					    &mvm->status),
				  "Failed to update SF upon disassociation\n");

			/*
			 * If we get an assert during the connection (after the
			 * station has been added, but before the vif is set
			 * to associated), mac80211 will re-add the station and
			 * then configure the vif. Since the vif is not
			 * associated, we would remove the station here and
			 * this would fail the recovery.
			 */
			if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART,
				      &mvm->status)) {
				/*
				 * Remove AP station now that
				 * the MAC is unassoc
				 */
				ret = iwl_mvm_rm_sta_id(mvm, vif,
							mvmvif->ap_sta_id);
				if (ret)
					IWL_ERR(mvm,
						"failed to remove AP station\n");

				mvmvif->ap_sta_id = IWL_MVM_INVALID_STA;
			}

			/* remove quota for this interface */
			ret = iwl_mvm_update_quotas(mvm, false, NULL);
			if (ret)
				IWL_ERR(mvm, "failed to update quotas\n");

			/* this will take the cleared BSSID from bss_conf */
			ret = iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
			if (ret)
				IWL_ERR(mvm,
					"failed to update MAC %pM (clear after unassoc)\n",
					vif->addr);
		}

		/*
		 * The firmware tracks the MU-MIMO group on its own.
		 * However, on HW restart we should restore this data.
		 */
		if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) &&
		    (changes & BSS_CHANGED_MU_GROUPS) && vif->bss_conf.mu_mimo_owner) {
			ret = iwl_mvm_update_mu_groups(mvm, vif);
			if (ret)
				IWL_ERR(mvm,
					"failed to update VHT MU_MIMO groups\n");
		}

		iwl_mvm_recalc_multicast(mvm);

		/* reset rssi values */
		mvmvif->bf_data.ave_beacon_signal = 0;

		iwl_mvm_bt_coex_vif_change(mvm);
		iwl_mvm_update_smps(mvm, vif, IWL_MVM_SMPS_REQ_TT,
				    IEEE80211_SMPS_AUTOMATIC);
		if (fw_has_capa(&mvm->fw->ucode_capa,
				IWL_UCODE_TLV_CAPA_UMAC_SCAN))
			iwl_mvm_config_scan(mvm);
	}

	if (changes & BSS_CHANGED_BEACON_INFO) {
		/*
		 * We received a beacon from the associated AP so
		 * remove the session protection.
		 */
		iwl_mvm_stop_session_protection(mvm, vif);

		iwl_mvm_sf_update(mvm, vif, false);
		WARN_ON(iwl_mvm_enable_beacon_filter(mvm, vif, 0));
	}

	if (changes & (BSS_CHANGED_PS | BSS_CHANGED_P2P_PS | BSS_CHANGED_QOS |
		       /*
			* Send power command on every beacon change,
			* because we may have not enabled beacon abort yet.
			*/
		       BSS_CHANGED_BEACON_INFO)) {
		ret = iwl_mvm_power_update_mac(mvm);
		if (ret)
			IWL_ERR(mvm, "failed to update power mode\n");
	}

	if (changes & BSS_CHANGED_CQM) {
		IWL_DEBUG_MAC80211(mvm, "cqm info_changed\n");
		/* reset cqm events tracking */
		mvmvif->bf_data.last_cqm_event = 0;
		if (mvmvif->bf_data.bf_enabled) {
			ret = iwl_mvm_enable_beacon_filter(mvm, vif, 0);
			if (ret)
				IWL_ERR(mvm,
					"failed to update CQM thresholds\n");
		}
	}

	if (changes & BSS_CHANGED_BANDWIDTH)
		iwl_mvm_apply_fw_smps_request(vif);
}

static int iwl_mvm_start_ap_ibss(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret, i;

	mutex_lock(&mvm->mutex);

	/* Send the beacon template */
	ret = iwl_mvm_mac_ctxt_beacon_changed(mvm, vif);
	if (ret)
		goto out_unlock;

	/*
	 * Re-calculate the tsf id, as the leader-follower relations depend on
	 * the beacon interval, which was not known when the AP interface
	 * was added.
	 */
	if (vif->type == NL80211_IFTYPE_AP)
		iwl_mvm_mac_ctxt_recalc_tsf_id(mvm, vif);

	mvmvif->ap_assoc_sta_count = 0;

	/* Add the mac context */
	ret = iwl_mvm_mac_ctxt_add(mvm, vif);
	if (ret)
		goto out_unlock;

	/* Perform the binding */
	ret = iwl_mvm_binding_add_vif(mvm, vif);
	if (ret)
		goto out_remove;

	/*
	 * This is not very nice, but the simplest:
	 * For older FWs adding the mcast sta before the bcast station may
	 * cause assert 0x2b00.
	 * This is fixed in later FW so make the order of removal depend on
	 * the TLV
	 */
	if (fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_STA_TYPE)) {
		ret = iwl_mvm_add_mcast_sta(mvm, vif);
		if (ret)
			goto out_unbind;
		/*
		 * Send the bcast station. At this stage the TBTT and DTIM time
		 * events are added and applied to the scheduler
		 */
		ret = iwl_mvm_send_add_bcast_sta(mvm, vif);
		if (ret) {
			iwl_mvm_rm_mcast_sta(mvm, vif);
			goto out_unbind;
		}
	} else {
		/*
		 * Send the bcast station. At this stage the TBTT and DTIM time
		 * events are added and applied to the scheduler
		 */
		ret = iwl_mvm_send_add_bcast_sta(mvm, vif);
		if (ret)
			goto out_unbind;
		ret = iwl_mvm_add_mcast_sta(mvm, vif);
		if (ret) {
			iwl_mvm_send_rm_bcast_sta(mvm, vif);
			goto out_unbind;
		}
	}

	/* must be set before quota calculations */
	mvmvif->ap_ibss_active = true;

	/* send all the early keys to the device now */
	for (i = 0; i < ARRAY_SIZE(mvmvif->ap_early_keys); i++) {
		struct ieee80211_key_conf *key = mvmvif->ap_early_keys[i];

		if (!key)
			continue;

		mvmvif->ap_early_keys[i] = NULL;

		ret = __iwl_mvm_mac_set_key(hw, SET_KEY, vif, NULL, key);
		if (ret)
			goto out_quota_failed;
	}

	if (vif->type == NL80211_IFTYPE_AP && !vif->p2p) {
		iwl_mvm_vif_set_low_latency(mvmvif, true,
					    LOW_LATENCY_VIF_TYPE);
		iwl_mvm_send_low_latency_cmd(mvm, true, mvmvif->id);
	}

	/* power updated needs to be done before quotas */
	iwl_mvm_power_update_mac(mvm);

	ret = iwl_mvm_update_quotas(mvm, false, NULL);
	if (ret)
		goto out_quota_failed;

	/* Need to update the P2P Device MAC (only GO, IBSS is single vif) */
	if (vif->p2p && mvm->p2p_device_vif)
		iwl_mvm_mac_ctxt_changed(mvm, mvm->p2p_device_vif, false, NULL);

	iwl_mvm_bt_coex_vif_change(mvm);

	/* we don't support TDLS during DCM */
	if (iwl_mvm_phy_ctx_count(mvm) > 1)
		iwl_mvm_teardown_tdls_peers(mvm);

	iwl_mvm_ftm_restart_responder(mvm, vif);

	goto out_unlock;

out_quota_failed:
	iwl_mvm_power_update_mac(mvm);
	mvmvif->ap_ibss_active = false;
	iwl_mvm_send_rm_bcast_sta(mvm, vif);
	iwl_mvm_rm_mcast_sta(mvm, vif);
out_unbind:
	iwl_mvm_binding_remove_vif(mvm, vif);
out_remove:
	iwl_mvm_mac_ctxt_remove(mvm, vif);
out_unlock:
	mutex_unlock(&mvm->mutex);
	return ret;
}

static int iwl_mvm_start_ap(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf)
{
	return iwl_mvm_start_ap_ibss(hw, vif, link_conf);
}

static int iwl_mvm_start_ibss(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif)
{
	return iwl_mvm_start_ap_ibss(hw, vif, &vif->bss_conf);
}

static void iwl_mvm_stop_ap_ibss(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	iwl_mvm_prepare_mac_removal(mvm, vif);

	mutex_lock(&mvm->mutex);

	/* Handle AP stop while in CSA */
	if (rcu_access_pointer(mvm->csa_vif) == vif) {
		iwl_mvm_remove_time_event(mvm, mvmvif,
					  &mvmvif->time_event_data);
		RCU_INIT_POINTER(mvm->csa_vif, NULL);
		mvmvif->csa_countdown = false;
	}

	if (rcu_access_pointer(mvm->csa_tx_blocked_vif) == vif) {
		RCU_INIT_POINTER(mvm->csa_tx_blocked_vif, NULL);
		mvm->csa_tx_block_bcn_timeout = 0;
	}

	mvmvif->ap_ibss_active = false;
	mvm->ap_last_beacon_gp2 = 0;

	if (vif->type == NL80211_IFTYPE_AP && !vif->p2p) {
		iwl_mvm_vif_set_low_latency(mvmvif, false,
					    LOW_LATENCY_VIF_TYPE);
		iwl_mvm_send_low_latency_cmd(mvm, false,  mvmvif->id);
	}

	iwl_mvm_bt_coex_vif_change(mvm);

	/* Need to update the P2P Device MAC (only GO, IBSS is single vif) */
	if (vif->p2p && mvm->p2p_device_vif)
		iwl_mvm_mac_ctxt_changed(mvm, mvm->p2p_device_vif, false, NULL);

	iwl_mvm_update_quotas(mvm, false, NULL);

	iwl_mvm_ftm_responder_clear(mvm, vif);

	/*
	 * This is not very nice, but the simplest:
	 * For older FWs removing the mcast sta before the bcast station may
	 * cause assert 0x2b00.
	 * This is fixed in later FW (which will stop beaconing when removing
	 * bcast station).
	 * So make the order of removal depend on the TLV
	 */
	if (!fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_STA_TYPE))
		iwl_mvm_rm_mcast_sta(mvm, vif);
	iwl_mvm_send_rm_bcast_sta(mvm, vif);
	if (fw_has_api(&mvm->fw->ucode_capa, IWL_UCODE_TLV_API_STA_TYPE))
		iwl_mvm_rm_mcast_sta(mvm, vif);
	iwl_mvm_binding_remove_vif(mvm, vif);

	iwl_mvm_power_update_mac(mvm);

	iwl_mvm_mac_ctxt_remove(mvm, vif);

	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_stop_ap(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf)
{
	iwl_mvm_stop_ap_ibss(hw, vif, link_conf);
}

static void iwl_mvm_stop_ibss(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif)
{
	iwl_mvm_stop_ap_ibss(hw, vif, &vif->bss_conf);
}

static void
iwl_mvm_bss_info_changed_ap_ibss(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *bss_conf,
				 u64 changes)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	/* Changes will be applied when the AP/IBSS is started */
	if (!mvmvif->ap_ibss_active)
		return;

	if (changes & (BSS_CHANGED_ERP_CTS_PROT | BSS_CHANGED_HT |
		       BSS_CHANGED_BANDWIDTH | BSS_CHANGED_QOS) &&
	    iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL))
		IWL_ERR(mvm, "failed to update MAC %pM\n", vif->addr);

	/* Need to send a new beacon template to the FW */
	if (changes & BSS_CHANGED_BEACON &&
	    iwl_mvm_mac_ctxt_beacon_changed(mvm, vif))
		IWL_WARN(mvm, "Failed updating beacon data\n");

	if (changes & BSS_CHANGED_FTM_RESPONDER) {
		int ret = iwl_mvm_ftm_start_responder(mvm, vif);

		if (ret)
			IWL_WARN(mvm, "Failed to enable FTM responder (%d)\n",
				 ret);
	}

}

static void iwl_mvm_bss_info_changed(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_bss_conf *bss_conf,
				     u64 changes)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);

	if (changes & BSS_CHANGED_IDLE && !vif->cfg.idle)
		iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_SCHED, true);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		iwl_mvm_bss_info_changed_station(mvm, vif, bss_conf, changes);
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
		iwl_mvm_bss_info_changed_ap_ibss(mvm, vif, bss_conf, changes);
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
				bss_conf->txpower);
		iwl_mvm_set_tx_power(mvm, vif, bss_conf->txpower);
	}

	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_mac_hw_scan(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct ieee80211_scan_request *hw_req)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	if (hw_req->req.n_channels == 0 ||
	    hw_req->req.n_channels > mvm->fw->ucode_capa.n_scan_channels)
		return -EINVAL;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_reg_scan_start(mvm, vif, &hw_req->req, &hw_req->ies);
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void iwl_mvm_mac_cancel_hw_scan(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);

	/* Due to a race condition, it's possible that mac80211 asks
	 * us to stop a hw_scan when it's already stopped.  This can
	 * happen, for instance, if we stopped the scan ourselves,
	 * called ieee80211_scan_completed() and the userspace called
	 * cancel scan scan before ieee80211_scan_work() could run.
	 * To handle that, simply return if the scan is not running.
	*/
	if (mvm->scan_status & IWL_MVM_SCAN_REGULAR)
		iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_REGULAR, true);

	mutex_unlock(&mvm->mutex);
}

static void
iwl_mvm_mac_allow_buffered_frames(struct ieee80211_hw *hw,
				  struct ieee80211_sta *sta, u16 tids,
				  int num_frames,
				  enum ieee80211_frame_release_type reason,
				  bool more_data)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	/* Called when we need to transmit (a) frame(s) from mac80211 */

	iwl_mvm_sta_modify_sleep_tx_count(mvm, sta, reason, num_frames,
					  tids, more_data, false);
}

static void
iwl_mvm_mac_release_buffered_frames(struct ieee80211_hw *hw,
				    struct ieee80211_sta *sta, u16 tids,
				    int num_frames,
				    enum ieee80211_frame_release_type reason,
				    bool more_data)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	/* Called when we need to transmit (a) frame(s) from agg or dqa queue */

	iwl_mvm_sta_modify_sleep_tx_count(mvm, sta, reason, num_frames,
					  tids, more_data, true);
}

static void __iwl_mvm_mac_sta_notify(struct ieee80211_hw *hw,
				     enum sta_notify_cmd cmd,
				     struct ieee80211_sta *sta)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	unsigned long txqs = 0, tids = 0;
	int tid;

	/*
	 * If we have TVQM then we get too high queue numbers - luckily
	 * we really shouldn't get here with that because such hardware
	 * should have firmware supporting buffer station offload.
	 */
	if (WARN_ON(iwl_mvm_has_new_tx_api(mvm)))
		return;

	spin_lock_bh(&mvmsta->lock);
	for (tid = 0; tid < ARRAY_SIZE(mvmsta->tid_data); tid++) {
		struct iwl_mvm_tid_data *tid_data = &mvmsta->tid_data[tid];

		if (tid_data->txq_id == IWL_MVM_INVALID_QUEUE)
			continue;

		__set_bit(tid_data->txq_id, &txqs);

		if (iwl_mvm_tid_queued(mvm, tid_data) == 0)
			continue;

		__set_bit(tid, &tids);
	}

	switch (cmd) {
	case STA_NOTIFY_SLEEP:
		for_each_set_bit(tid, &tids, IWL_MAX_TID_COUNT)
			ieee80211_sta_set_buffered(sta, tid, true);

		if (txqs)
			iwl_trans_freeze_txq_timer(mvm->trans, txqs, true);
		/*
		 * The fw updates the STA to be asleep. Tx packets on the Tx
		 * queues to this station will not be transmitted. The fw will
		 * send a Tx response with TX_STATUS_FAIL_DEST_PS.
		 */
		break;
	case STA_NOTIFY_AWAKE:
		if (WARN_ON(mvmsta->sta_id == IWL_MVM_INVALID_STA))
			break;

		if (txqs)
			iwl_trans_freeze_txq_timer(mvm->trans, txqs, false);
		iwl_mvm_sta_modify_ps_wake(mvm, sta);
		break;
	default:
		break;
	}
	spin_unlock_bh(&mvmsta->lock);
}

static void iwl_mvm_mac_sta_notify(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   enum sta_notify_cmd cmd,
				   struct ieee80211_sta *sta)
{
	__iwl_mvm_mac_sta_notify(hw, cmd, sta);
}

void iwl_mvm_sta_pm_notif(struct iwl_mvm *mvm, struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mvm_pm_state_notification *notif = (void *)pkt->data;
	struct ieee80211_sta *sta;
	struct iwl_mvm_sta *mvmsta;
	bool sleeping = (notif->type != IWL_MVM_PM_EVENT_AWAKE);

	if (WARN_ON(notif->sta_id >= mvm->fw->ucode_capa.num_stations))
		return;

	rcu_read_lock();
	sta = rcu_dereference(mvm->fw_id_to_mac_id[notif->sta_id]);
	if (WARN_ON(IS_ERR_OR_NULL(sta))) {
		rcu_read_unlock();
		return;
	}

	mvmsta = iwl_mvm_sta_from_mac80211(sta);

	if (!mvmsta->vif ||
	    mvmsta->vif->type != NL80211_IFTYPE_AP) {
		rcu_read_unlock();
		return;
	}

	if (mvmsta->sleeping != sleeping) {
		mvmsta->sleeping = sleeping;
		__iwl_mvm_mac_sta_notify(mvm->hw,
			sleeping ? STA_NOTIFY_SLEEP : STA_NOTIFY_AWAKE,
			sta);
		ieee80211_sta_ps_transition(sta, sleeping);
	}

	if (sleeping) {
		switch (notif->type) {
		case IWL_MVM_PM_EVENT_AWAKE:
		case IWL_MVM_PM_EVENT_ASLEEP:
			break;
		case IWL_MVM_PM_EVENT_UAPSD:
			ieee80211_sta_uapsd_trigger(sta, IEEE80211_NUM_TIDS);
			break;
		case IWL_MVM_PM_EVENT_PS_POLL:
			ieee80211_sta_pspoll(sta);
			break;
		default:
			break;
		}
	}

	rcu_read_unlock();
}

static void iwl_mvm_sta_pre_rcu_remove(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);

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

static void iwl_mvm_check_uapsd(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
				const u8 *bssid)
{
	int i;

	if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		struct iwl_mvm_tcm_mac *mdata;

		mdata = &mvm->tcm.data[iwl_mvm_vif_from_mac80211(vif)->id];
		ewma_rate_init(&mdata->uapsd_nonagg_detect.rate);
		mdata->opened_rx_ba_sessions = false;
	}

	if (!(mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_UAPSD_SUPPORT))
		return;

	if (vif->p2p && !iwl_mvm_is_p2p_scm_uapsd_supported(mvm)) {
		vif->driver_flags &= ~IEEE80211_VIF_SUPPORTS_UAPSD;
		return;
	}

	if (!vif->p2p &&
	    (iwlwifi_mod_params.uapsd_disable & IWL_DISABLE_UAPSD_BSS)) {
		vif->driver_flags &= ~IEEE80211_VIF_SUPPORTS_UAPSD;
		return;
	}

	for (i = 0; i < IWL_MVM_UAPSD_NOAGG_LIST_LEN; i++) {
		if (ether_addr_equal(mvm->uapsd_noagg_bssids[i].addr, bssid)) {
			vif->driver_flags &= ~IEEE80211_VIF_SUPPORTS_UAPSD;
			return;
		}
	}

	vif->driver_flags |= IEEE80211_VIF_SUPPORTS_UAPSD;
}

static void
iwl_mvm_tdls_check_trigger(struct iwl_mvm *mvm,
			   struct ieee80211_vif *vif, u8 *peer_addr,
			   enum nl80211_tdls_operation action)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_tdls *tdls_trig;

	trig = iwl_fw_dbg_trigger_on(&mvm->fwrt, ieee80211_vif_to_wdev(vif),
				     FW_DBG_TRIGGER_TDLS);
	if (!trig)
		return;

	tdls_trig = (void *)trig->data;

	if (!(tdls_trig->action_bitmap & BIT(action)))
		return;

	if (tdls_trig->peer_mode &&
	    memcmp(tdls_trig->peer, peer_addr, ETH_ALEN) != 0)
		return;

	iwl_fw_dbg_collect_trig(&mvm->fwrt, trig,
				"TDLS event occurred, peer %pM, action %d",
				peer_addr, action);
}

struct iwl_mvm_he_obss_narrow_bw_ru_data {
	bool tolerated;
};

static void iwl_mvm_check_he_obss_narrow_bw_ru_iter(struct wiphy *wiphy,
						    struct cfg80211_bss *bss,
						    void *_data)
{
	struct iwl_mvm_he_obss_narrow_bw_ru_data *data = _data;
	const struct cfg80211_bss_ies *ies;
	const struct element *elem;

	rcu_read_lock();
	ies = rcu_dereference(bss->ies);
	elem = cfg80211_find_elem(WLAN_EID_EXT_CAPABILITY, ies->data,
				  ies->len);

	if (!elem || elem->datalen < 10 ||
	    !(elem->data[10] &
	      WLAN_EXT_CAPA10_OBSS_NARROW_BW_RU_TOLERANCE_SUPPORT)) {
		data->tolerated = false;
	}
	rcu_read_unlock();
}

static void iwl_mvm_check_he_obss_narrow_bw_ru(struct ieee80211_hw *hw,
					       struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_he_obss_narrow_bw_ru_data iter_data = {
		.tolerated = true,
	};

	if (!(vif->bss_conf.chandef.chan->flags & IEEE80211_CHAN_RADAR)) {
		mvmvif->he_ru_2mhz_block = false;
		return;
	}

	cfg80211_bss_iter(hw->wiphy, &vif->bss_conf.chandef,
			  iwl_mvm_check_he_obss_narrow_bw_ru_iter,
			  &iter_data);

	/*
	 * If there is at least one AP on radar channel that cannot
	 * tolerate 26-tone RU UL OFDMA transmissions using HE TB PPDU.
	 */
	mvmvif->he_ru_2mhz_block = !iter_data.tolerated;
}

static void iwl_mvm_reset_cca_40mhz_workaround(struct iwl_mvm *mvm,
					       struct ieee80211_vif *vif)
{
	struct ieee80211_supported_band *sband;
	const struct ieee80211_sta_he_cap *he_cap;

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	if (!mvm->cca_40mhz_workaround)
		return;

	/* decrement and check that we reached zero */
	mvm->cca_40mhz_workaround--;
	if (mvm->cca_40mhz_workaround)
		return;

	sband = mvm->hw->wiphy->bands[NL80211_BAND_2GHZ];

	sband->ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;

	he_cap = ieee80211_get_he_iftype_cap(sband,
					     ieee80211_vif_type_p2p(vif));

	if (he_cap) {
		/* we know that ours is writable */
		struct ieee80211_sta_he_cap *he = (void *)(uintptr_t)he_cap;

		he->he_cap_elem.phy_cap_info[0] |=
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
	}
}

static void iwl_mvm_mei_host_associated(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					struct iwl_mvm_sta *mvm_sta)
{
#if IS_ENABLED(CONFIG_IWLMEI)
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mei_conn_info conn_info = {
		.ssid_len = vif->cfg.ssid_len,
		.channel = vif->bss_conf.chandef.chan->hw_value,
	};

	if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
		return;

	if (!mvm->mei_registered)
		return;

	switch (mvm_sta->pairwise_cipher) {
	case WLAN_CIPHER_SUITE_CCMP:
		conn_info.pairwise_cipher = IWL_MEI_CIPHER_CCMP;
		break;
	case WLAN_CIPHER_SUITE_GCMP:
		conn_info.pairwise_cipher = IWL_MEI_CIPHER_GCMP;
		break;
	case WLAN_CIPHER_SUITE_GCMP_256:
		conn_info.pairwise_cipher = IWL_MEI_CIPHER_GCMP_256;
		break;
	case 0:
		/* open profile */
		break;
	default:
		/* cipher not supported, don't send anything to iwlmei */
		return;
	}

	switch (mvmvif->rekey_data.akm) {
	case WLAN_AKM_SUITE_SAE & 0xff:
		conn_info.auth_mode = IWL_MEI_AKM_AUTH_SAE;
		break;
	case WLAN_AKM_SUITE_PSK & 0xff:
		conn_info.auth_mode = IWL_MEI_AKM_AUTH_RSNA_PSK;
		break;
	case WLAN_AKM_SUITE_8021X & 0xff:
		conn_info.auth_mode = IWL_MEI_AKM_AUTH_RSNA;
		break;
	case 0:
		/* open profile */
		conn_info.auth_mode = IWL_MEI_AKM_AUTH_OPEN;
		break;
	default:
		/* auth method / AKM not supported */
		/* TODO: All the FT vesions of these? */
		return;
	}

	memcpy(conn_info.ssid, vif->cfg.ssid, vif->cfg.ssid_len);
	memcpy(conn_info.bssid,  vif->bss_conf.bssid, ETH_ALEN);

	/* TODO: add support for collocated AP data */
	iwl_mei_host_associated(&conn_info, NULL);
#endif
}

static int iwl_mvm_mac_sta_state(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 enum ieee80211_sta_state old_state,
				 enum ieee80211_sta_state new_state)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);
	int ret;

	IWL_DEBUG_MAC80211(mvm, "station %pM state change %d->%d\n",
			   sta->addr, old_state, new_state);

	/* this would be a mac80211 bug ... but don't crash */
	if (WARN_ON_ONCE(!mvmvif->phy_ctxt))
		return test_bit(IWL_MVM_STATUS_HW_RESTART_REQUESTED, &mvm->status) ? 0 : -EINVAL;

	/*
	 * If we are in a STA removal flow and in DQA mode:
	 *
	 * This is after the sync_rcu part, so the queues have already been
	 * flushed. No more TXs on their way in mac80211's path, and no more in
	 * the queues.
	 * Also, we won't be getting any new TX frames for this station.
	 * What we might have are deferred TX frames that need to be taken care
	 * of.
	 *
	 * Drop any still-queued deferred-frame before removing the STA, and
	 * make sure the worker is no longer handling frames for this STA.
	 */
	if (old_state == IEEE80211_STA_NONE &&
	    new_state == IEEE80211_STA_NOTEXIST) {
		flush_work(&mvm->add_stream_wk);

		/*
		 * No need to make sure deferred TX indication is off since the
		 * worker will already remove it if it was on
		 */

		/*
		 * Additionally, reset the 40 MHz capability if we disconnected
		 * from the AP now.
		 */
		iwl_mvm_reset_cca_40mhz_workaround(mvm, vif);
	}

	mutex_lock(&mvm->mutex);
	/* track whether or not the station is associated */
	mvm_sta->sta_state = new_state;

	if (old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE) {
		/*
		 * Firmware bug - it'll crash if the beacon interval is less
		 * than 16. We can't avoid connecting at all, so refuse the
		 * station state change, this will cause mac80211 to abandon
		 * attempts to connect to this AP, and eventually wpa_s will
		 * blocklist the AP...
		 */
		if (vif->type == NL80211_IFTYPE_STATION &&
		    vif->bss_conf.beacon_int < 16) {
			IWL_ERR(mvm,
				"AP %pM beacon interval is %d, refusing due to firmware bug!\n",
				sta->addr, vif->bss_conf.beacon_int);
			ret = -EINVAL;
			goto out_unlock;
		}

		if (vif->type == NL80211_IFTYPE_STATION)
			vif->bss_conf.he_support = sta->deflink.he_cap.has_he;

		if (sta->tdls &&
		    (vif->p2p ||
		     iwl_mvm_tdls_sta_count(mvm, NULL) ==
						IWL_MVM_TDLS_STA_COUNT ||
		     iwl_mvm_phy_ctx_count(mvm) > 1)) {
			IWL_DEBUG_MAC80211(mvm, "refusing TDLS sta\n");
			ret = -EBUSY;
			goto out_unlock;
		}

		ret = iwl_mvm_add_sta(mvm, vif, sta);
		if (sta->tdls && ret == 0) {
			iwl_mvm_recalc_tdls_state(mvm, vif, true);
			iwl_mvm_tdls_check_trigger(mvm, vif, sta->addr,
						   NL80211_TDLS_SETUP);
		}

		sta->deflink.agg.max_rc_amsdu_len = 1;
	} else if (old_state == IEEE80211_STA_NONE &&
		   new_state == IEEE80211_STA_AUTH) {
		/*
		 * EBS may be disabled due to previous failures reported by FW.
		 * Reset EBS status here assuming environment has been changed.
		 */
		mvm->last_ebs_successful = true;
		iwl_mvm_check_uapsd(mvm, vif, sta->addr);
		ret = 0;
	} else if (old_state == IEEE80211_STA_AUTH &&
		   new_state == IEEE80211_STA_ASSOC) {
		if (vif->type == NL80211_IFTYPE_AP) {
			vif->bss_conf.he_support = sta->deflink.he_cap.has_he;
			mvmvif->ap_assoc_sta_count++;
			iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
			if (vif->bss_conf.he_support &&
			    !iwlwifi_mod_params.disable_11ax)
				iwl_mvm_cfg_he_sta(mvm, vif, mvm_sta->sta_id);
		} else if (vif->type == NL80211_IFTYPE_STATION) {
			vif->bss_conf.he_support = sta->deflink.he_cap.has_he;

			mvmvif->he_ru_2mhz_block = false;
			if (sta->deflink.he_cap.has_he)
				iwl_mvm_check_he_obss_narrow_bw_ru(hw, vif);

			iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
		}

		iwl_mvm_rs_rate_init(mvm, sta, mvmvif->phy_ctxt->channel->band,
				     false);
		ret = iwl_mvm_update_sta(mvm, vif, sta);
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTHORIZED) {
		ret = 0;

		/* we don't support TDLS during DCM */
		if (iwl_mvm_phy_ctx_count(mvm) > 1)
			iwl_mvm_teardown_tdls_peers(mvm);

		if (sta->tdls) {
			iwl_mvm_tdls_check_trigger(mvm, vif, sta->addr,
						   NL80211_TDLS_ENABLE_LINK);
		} else {
			/* enable beacon filtering */
			WARN_ON(iwl_mvm_enable_beacon_filter(mvm, vif, 0));

			mvmvif->authorized = 1;

			/*
			 * Now that the station is authorized, i.e., keys were already
			 * installed, need to indicate to the FW that
			 * multicast data frames can be forwarded to the driver
			 */
			iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
			iwl_mvm_mei_host_associated(mvm, vif, mvm_sta);
		}

		iwl_mvm_rs_rate_init(mvm, sta, mvmvif->phy_ctxt->channel->band,
				     true);
	} else if (old_state == IEEE80211_STA_AUTHORIZED &&
		   new_state == IEEE80211_STA_ASSOC) {
		/* once we move into assoc state, need to update rate scale to
		 * disable using wide bandwidth
		 */
		iwl_mvm_rs_rate_init(mvm, sta, mvmvif->phy_ctxt->channel->band,
				     false);
		if (!sta->tdls) {
			/* Multicast data frames are no longer allowed */
			iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);

			/*
			 * Set this after the above iwl_mvm_mac_ctxt_changed()
			 * to avoid sending high prio again for a little time.
			 */
			mvmvif->authorized = 0;

			/* disable beacon filtering */
			ret = iwl_mvm_disable_beacon_filter(mvm, vif, 0);
			WARN_ON(ret &&
				!test_bit(IWL_MVM_STATUS_HW_RESTART_REQUESTED,
					  &mvm->status));
		}
		ret = 0;
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTH) {
		if (vif->type == NL80211_IFTYPE_AP) {
			mvmvif->ap_assoc_sta_count--;
			iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
		} else if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls)
			iwl_mvm_stop_session_protection(mvm, vif);
		ret = 0;
	} else if (old_state == IEEE80211_STA_AUTH &&
		   new_state == IEEE80211_STA_NONE) {
		ret = 0;
	} else if (old_state == IEEE80211_STA_NONE &&
		   new_state == IEEE80211_STA_NOTEXIST) {
		if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls)
			iwl_mvm_stop_session_protection(mvm, vif);
		ret = iwl_mvm_rm_sta(mvm, vif, sta);
		if (sta->tdls) {
			iwl_mvm_recalc_tdls_state(mvm, vif, false);
			iwl_mvm_tdls_check_trigger(mvm, vif, sta->addr,
						   NL80211_TDLS_DISABLE_LINK);
		}

		if (unlikely(ret &&
			     test_bit(IWL_MVM_STATUS_HW_RESTART_REQUESTED,
				      &mvm->status)))
			ret = 0;
	} else {
		ret = -EIO;
	}
 out_unlock:
	mutex_unlock(&mvm->mutex);

	if (sta->tdls && ret == 0) {
		if (old_state == IEEE80211_STA_NOTEXIST &&
		    new_state == IEEE80211_STA_NONE)
			ieee80211_reserve_tid(sta, IWL_MVM_TDLS_FW_TID);
		else if (old_state == IEEE80211_STA_NONE &&
			 new_state == IEEE80211_STA_NOTEXIST)
			ieee80211_unreserve_tid(sta, IWL_MVM_TDLS_FW_TID);
	}

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
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (changed & (IEEE80211_RC_BW_CHANGED |
		       IEEE80211_RC_SUPP_RATES_CHANGED |
		       IEEE80211_RC_NSS_CHANGED))
		iwl_mvm_rs_rate_init(mvm, sta, mvmvif->phy_ctxt->channel->band,
				     true);

	if (vif->type == NL80211_IFTYPE_STATION &&
	    changed & IEEE80211_RC_NSS_CHANGED)
		iwl_mvm_sf_update(mvm, vif, false);
}

static int iwl_mvm_mac_conf_tx(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       unsigned int link_id, u16 ac,
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
		ret = iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
		mutex_unlock(&mvm->mutex);
		return ret;
	}
	return 0;
}

static void iwl_mvm_mac_mgd_prepare_tx(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_prep_tx_info *info)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);
	iwl_mvm_protect_assoc(mvm, vif, info->duration);
	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_mac_mgd_complete_tx(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ieee80211_prep_tx_info *info)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	/* for successful cases (auth/assoc), don't cancel session protection */
	if (info->success)
		return;

	mutex_lock(&mvm->mutex);
	iwl_mvm_stop_session_protection(mvm, vif);
	mutex_unlock(&mvm->mutex);
}

static int iwl_mvm_mac_sched_scan_start(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct cfg80211_sched_scan_request *req,
					struct ieee80211_scan_ies *ies)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	int ret;

	mutex_lock(&mvm->mutex);

	if (!vif->cfg.idle) {
		ret = -EBUSY;
		goto out;
	}

	ret = iwl_mvm_sched_scan_start(mvm, vif, req, ies, IWL_MVM_SCAN_SCHED);

out:
	mutex_unlock(&mvm->mutex);
	return ret;
}

static int iwl_mvm_mac_sched_scan_stop(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);

	/* Due to a race condition, it's possible that mac80211 asks
	 * us to stop a sched_scan when it's already stopped.  This
	 * can happen, for instance, if we stopped the scan ourselves,
	 * called ieee80211_sched_scan_stopped() and the userspace called
	 * stop sched scan scan before ieee80211_sched_scan_stopped_work()
	 * could run.  To handle this, simply return if the scan is
	 * not running.
	*/
	if (!(mvm->scan_status & IWL_MVM_SCAN_SCHED)) {
		mutex_unlock(&mvm->mutex);
		return 0;
	}

	ret = iwl_mvm_scan_stop(mvm, IWL_MVM_SCAN_SCHED, false);
	mutex_unlock(&mvm->mutex);
	iwl_mvm_wait_for_async_handlers(mvm);

	return ret;
}

static int __iwl_mvm_mac_set_key(struct ieee80211_hw *hw,
				 enum set_key_cmd cmd,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 struct ieee80211_key_conf *key)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_sta *mvmsta = NULL;
	struct iwl_mvm_key_pn *ptk_pn = NULL;
	int keyidx = key->keyidx;
	int ret, i;
	u8 key_offset;

	if (sta)
		mvmsta = iwl_mvm_sta_from_mac80211(sta);

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
		if (!mvm->trans->trans_cfg->gen2) {
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
			key->flags |= IEEE80211_KEY_FLAG_PUT_IV_SPACE;
		} else if (vif->type == NL80211_IFTYPE_STATION) {
			key->flags |= IEEE80211_KEY_FLAG_PUT_MIC_SPACE;
		} else {
			IWL_DEBUG_MAC80211(mvm, "Use SW encryption for TKIP\n");
			return -EOPNOTSUPP;
		}
		break;
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		if (!iwl_mvm_has_new_tx_api(mvm))
			key->flags |= IEEE80211_KEY_FLAG_PUT_IV_SPACE;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		WARN_ON_ONCE(!ieee80211_hw_check(hw, MFP_CAPABLE));
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		if (vif->type == NL80211_IFTYPE_STATION)
			break;
		if (iwl_mvm_has_new_tx_api(mvm))
			return -EOPNOTSUPP;
		/* support HW crypto on TX */
		return 0;
	default:
		return -EOPNOTSUPP;
	}

	switch (cmd) {
	case SET_KEY:
		if (keyidx == 6 || keyidx == 7)
			rcu_assign_pointer(mvmvif->bcn_prot.keys[keyidx - 6],
					   key);

		if ((vif->type == NL80211_IFTYPE_ADHOC ||
		     vif->type == NL80211_IFTYPE_AP) && !sta) {
			/*
			 * GTK on AP interface is a TX-only key, return 0;
			 * on IBSS they're per-station and because we're lazy
			 * we don't support them for RX, so do the same.
			 * CMAC/GMAC in AP/IBSS modes must be done in software.
			 */
			if (key->cipher == WLAN_CIPHER_SUITE_AES_CMAC ||
			    key->cipher == WLAN_CIPHER_SUITE_BIP_GMAC_128 ||
			    key->cipher == WLAN_CIPHER_SUITE_BIP_GMAC_256) {
				ret = -EOPNOTSUPP;
				break;
			}

			if (key->cipher != WLAN_CIPHER_SUITE_GCMP &&
			    key->cipher != WLAN_CIPHER_SUITE_GCMP_256 &&
			    !iwl_mvm_has_new_tx_api(mvm)) {
				key->hw_key_idx = STA_KEY_IDX_INVALID;
				ret = 0;
				break;
			}

			if (!mvmvif->ap_ibss_active) {
				for (i = 0;
				     i < ARRAY_SIZE(mvmvif->ap_early_keys);
				     i++) {
					if (!mvmvif->ap_early_keys[i]) {
						mvmvif->ap_early_keys[i] = key;
						break;
					}
				}

				if (i >= ARRAY_SIZE(mvmvif->ap_early_keys))
					ret = -ENOSPC;
				else
					ret = 0;

				break;
			}
		}

		/* During FW restart, in order to restore the state as it was,
		 * don't try to reprogram keys we previously failed for.
		 */
		if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) &&
		    key->hw_key_idx == STA_KEY_IDX_INVALID) {
			IWL_DEBUG_MAC80211(mvm,
					   "skip invalid idx key programming during restart\n");
			ret = 0;
			break;
		}

		if (!test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status) &&
		    mvmsta && iwl_mvm_has_new_rx_api(mvm) &&
		    key->flags & IEEE80211_KEY_FLAG_PAIRWISE &&
		    (key->cipher == WLAN_CIPHER_SUITE_CCMP ||
		     key->cipher == WLAN_CIPHER_SUITE_GCMP ||
		     key->cipher == WLAN_CIPHER_SUITE_GCMP_256)) {
			struct ieee80211_key_seq seq;
			int tid, q;

			WARN_ON(rcu_access_pointer(mvmsta->ptk_pn[keyidx]));
			ptk_pn = kzalloc(struct_size(ptk_pn, q,
						     mvm->trans->num_rx_queues),
					 GFP_KERNEL);
			if (!ptk_pn) {
				ret = -ENOMEM;
				break;
			}

			for (tid = 0; tid < IWL_MAX_TID_COUNT; tid++) {
				ieee80211_get_key_rx_seq(key, tid, &seq);
				for (q = 0; q < mvm->trans->num_rx_queues; q++)
					memcpy(ptk_pn->q[q].pn[tid],
					       seq.ccmp.pn,
					       IEEE80211_CCMP_PN_LEN);
			}

			rcu_assign_pointer(mvmsta->ptk_pn[keyidx], ptk_pn);
		}

		/* in HW restart reuse the index, otherwise request a new one */
		if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status))
			key_offset = key->hw_key_idx;
		else
			key_offset = STA_KEY_IDX_INVALID;

		if (mvmsta && key->flags & IEEE80211_KEY_FLAG_PAIRWISE)
			mvmsta->pairwise_cipher = key->cipher;

		IWL_DEBUG_MAC80211(mvm, "set hwcrypto key\n");
		ret = iwl_mvm_set_sta_key(mvm, vif, sta, key, key_offset);
		if (ret) {
			IWL_WARN(mvm, "set key failed\n");
			key->hw_key_idx = STA_KEY_IDX_INVALID;
			if (ptk_pn) {
				RCU_INIT_POINTER(mvmsta->ptk_pn[keyidx], NULL);
				kfree(ptk_pn);
			}
			/*
			 * can't add key for RX, but we don't need it
			 * in the device for TX so still return 0,
			 * unless we have new TX API where we cannot
			 * put key material into the TX_CMD
			 */
			if (iwl_mvm_has_new_tx_api(mvm))
				ret = -EOPNOTSUPP;
			else
				ret = 0;
		}

		break;
	case DISABLE_KEY:
		if (keyidx == 6 || keyidx == 7)
			RCU_INIT_POINTER(mvmvif->bcn_prot.keys[keyidx - 6],
					 NULL);

		ret = -ENOENT;
		for (i = 0; i < ARRAY_SIZE(mvmvif->ap_early_keys); i++) {
			if (mvmvif->ap_early_keys[i] == key) {
				mvmvif->ap_early_keys[i] = NULL;
				ret = 0;
			}
		}

		/* found in pending list - don't do anything else */
		if (ret == 0)
			break;

		if (key->hw_key_idx == STA_KEY_IDX_INVALID) {
			ret = 0;
			break;
		}

		if (mvmsta && iwl_mvm_has_new_rx_api(mvm) &&
		    key->flags & IEEE80211_KEY_FLAG_PAIRWISE &&
		    (key->cipher == WLAN_CIPHER_SUITE_CCMP ||
		     key->cipher == WLAN_CIPHER_SUITE_GCMP ||
		     key->cipher == WLAN_CIPHER_SUITE_GCMP_256)) {
			ptk_pn = rcu_dereference_protected(
						mvmsta->ptk_pn[keyidx],
						lockdep_is_held(&mvm->mutex));
			RCU_INIT_POINTER(mvmsta->ptk_pn[keyidx], NULL);
			if (ptk_pn)
				kfree_rcu(ptk_pn, rcu_head);
		}

		IWL_DEBUG_MAC80211(mvm, "disable hwcrypto key\n");
		ret = iwl_mvm_remove_sta_key(mvm, vif, sta, key);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int iwl_mvm_mac_set_key(struct ieee80211_hw *hw,
			       enum set_key_cmd cmd,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta,
			       struct ieee80211_key_conf *key)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);
	ret = __iwl_mvm_mac_set_key(hw, cmd, vif, sta, key);
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


static bool iwl_mvm_rx_aux_roc(struct iwl_notif_wait_data *notif_wait,
			       struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_mvm *mvm =
		container_of(notif_wait, struct iwl_mvm, notif_wait);
	struct iwl_hs20_roc_res *resp;
	int resp_len = iwl_rx_packet_payload_len(pkt);
	struct iwl_mvm_time_event_data *te_data = data;

	if (WARN_ON(pkt->hdr.cmd != HOT_SPOT_CMD))
		return true;

	if (WARN_ON_ONCE(resp_len != sizeof(*resp))) {
		IWL_ERR(mvm, "Invalid HOT_SPOT_CMD response\n");
		return true;
	}

	resp = (void *)pkt->data;

	IWL_DEBUG_TE(mvm,
		     "Aux ROC: Received response from ucode: status=%d uid=%d\n",
		     resp->status, resp->event_unique_id);

	te_data->uid = le32_to_cpu(resp->event_unique_id);
	IWL_DEBUG_TE(mvm, "TIME_EVENT_CMD response - UID = 0x%x\n",
		     te_data->uid);

	spin_lock_bh(&mvm->time_event_lock);
	list_add_tail(&te_data->list, &mvm->aux_roc_te_list);
	spin_unlock_bh(&mvm->time_event_lock);

	return true;
}

#define AUX_ROC_MIN_DURATION MSEC_TO_TU(100)
#define AUX_ROC_MIN_DELAY MSEC_TO_TU(200)
#define AUX_ROC_MAX_DELAY MSEC_TO_TU(600)
#define AUX_ROC_SAFETY_BUFFER MSEC_TO_TU(20)
#define AUX_ROC_MIN_SAFETY_BUFFER MSEC_TO_TU(10)
static int iwl_mvm_send_aux_roc_cmd(struct iwl_mvm *mvm,
				    struct ieee80211_channel *channel,
				    struct ieee80211_vif *vif,
				    int duration)
{
	int res;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_time_event_data *te_data = &mvmvif->hs_time_event_data;
	static const u16 time_event_response[] = { HOT_SPOT_CMD };
	struct iwl_notification_wait wait_time_event;
	u32 dtim_interval = vif->bss_conf.dtim_period *
		vif->bss_conf.beacon_int;
	u32 req_dur, delay;
	struct iwl_hs20_roc_req aux_roc_req = {
		.action = cpu_to_le32(FW_CTXT_ACTION_ADD),
		.id_and_color =
			cpu_to_le32(FW_CMD_ID_AND_COLOR(MAC_INDEX_AUX, 0)),
		.sta_id_and_color = cpu_to_le32(mvm->aux_sta.sta_id),
	};
	struct iwl_hs20_roc_req_tail *tail = iwl_mvm_chan_info_cmd_tail(mvm,
		&aux_roc_req.channel_info);
	u16 len = sizeof(aux_roc_req) - iwl_mvm_chan_info_padding(mvm);

	/* Set the channel info data */
	iwl_mvm_set_chan_info(mvm, &aux_roc_req.channel_info, channel->hw_value,
			      iwl_mvm_phy_band_from_nl80211(channel->band),
			      PHY_VHT_CHANNEL_MODE20,
			      0);

	/* Set the time and duration */
	tail->apply_time = cpu_to_le32(iwl_mvm_get_systime(mvm));

	delay = AUX_ROC_MIN_DELAY;
	req_dur = MSEC_TO_TU(duration);

	/*
	 * If we are associated we want the delay time to be at least one
	 * dtim interval so that the FW can wait until after the DTIM and
	 * then start the time event, this will potentially allow us to
	 * remain off-channel for the max duration.
	 * Since we want to use almost a whole dtim interval we would also
	 * like the delay to be for 2-3 dtim intervals, in case there are
	 * other time events with higher priority.
	 */
	if (vif->cfg.assoc) {
		delay = min_t(u32, dtim_interval * 3, AUX_ROC_MAX_DELAY);
		/* We cannot remain off-channel longer than the DTIM interval */
		if (dtim_interval <= req_dur) {
			req_dur = dtim_interval - AUX_ROC_SAFETY_BUFFER;
			if (req_dur <= AUX_ROC_MIN_DURATION)
				req_dur = dtim_interval -
					AUX_ROC_MIN_SAFETY_BUFFER;
		}
	}

	tail->duration = cpu_to_le32(req_dur);
	tail->apply_time_max_delay = cpu_to_le32(delay);

	IWL_DEBUG_TE(mvm,
		     "ROC: Requesting to remain on channel %u for %ums\n",
		     channel->hw_value, req_dur);
	IWL_DEBUG_TE(mvm,
		     "\t(requested = %ums, max_delay = %ums, dtim_interval = %ums)\n",
		     duration, delay, dtim_interval);

	/* Set the node address */
	memcpy(tail->node_addr, vif->addr, ETH_ALEN);

	lockdep_assert_held(&mvm->mutex);

	spin_lock_bh(&mvm->time_event_lock);

	if (WARN_ON(te_data->id == HOT_SPOT_CMD)) {
		spin_unlock_bh(&mvm->time_event_lock);
		return -EIO;
	}

	te_data->vif = vif;
	te_data->duration = duration;
	te_data->id = HOT_SPOT_CMD;

	spin_unlock_bh(&mvm->time_event_lock);

	/*
	 * Use a notification wait, which really just processes the
	 * command response and doesn't wait for anything, in order
	 * to be able to process the response and get the UID inside
	 * the RX path. Using CMD_WANT_SKB doesn't work because it
	 * stores the buffer and then wakes up this thread, by which
	 * time another notification (that the time event started)
	 * might already be processed unsuccessfully.
	 */
	iwl_init_notification_wait(&mvm->notif_wait, &wait_time_event,
				   time_event_response,
				   ARRAY_SIZE(time_event_response),
				   iwl_mvm_rx_aux_roc, te_data);

	res = iwl_mvm_send_cmd_pdu(mvm, HOT_SPOT_CMD, 0, len,
				   &aux_roc_req);

	if (res) {
		IWL_ERR(mvm, "Couldn't send HOT_SPOT_CMD: %d\n", res);
		iwl_remove_notification(&mvm->notif_wait, &wait_time_event);
		goto out_clear_te;
	}

	/* No need to wait for anything, so just pass 1 (0 isn't valid) */
	res = iwl_wait_notification(&mvm->notif_wait, &wait_time_event, 1);
	/* should never fail */
	WARN_ON_ONCE(res);

	if (res) {
 out_clear_te:
		spin_lock_bh(&mvm->time_event_lock);
		iwl_mvm_te_clear_data(mvm, te_data);
		spin_unlock_bh(&mvm->time_event_lock);
	}

	return res;
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
	bool band_change_removal;
	int ret, i;

	IWL_DEBUG_MAC80211(mvm, "enter (%d, %d, %d)\n", channel->hw_value,
			   duration, type);

	/*
	 * Flush the done work, just in case it's still pending, so that
	 * the work it does can complete and we can accept new frames.
	 */
	flush_work(&mvm->roc_done_wk);

	mutex_lock(&mvm->mutex);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		if (fw_has_capa(&mvm->fw->ucode_capa,
				IWL_UCODE_TLV_CAPA_HOTSPOT_SUPPORT)) {
			/* Use aux roc framework (HS20) */
			if (iwl_fw_lookup_cmd_ver(mvm->fw, ADD_STA, 0) >= 12) {
				u32 lmac_id;

				lmac_id = iwl_mvm_get_lmac_id(mvm->fw,
							      channel->band);
				ret = iwl_mvm_add_aux_sta(mvm, lmac_id);
				if (WARN(ret,
					 "Failed to allocate aux station"))
					goto out_unlock;
			}
			ret = iwl_mvm_send_aux_roc_cmd(mvm, channel,
						       vif, duration);
			goto out_unlock;
		}
		IWL_ERR(mvm, "hotspot not supported\n");
		ret = -EINVAL;
		goto out_unlock;
	case NL80211_IFTYPE_P2P_DEVICE:
		/* handle below */
		break;
	default:
		IWL_ERR(mvm, "vif isn't P2P_DEVICE: %d\n", vif->type);
		ret = -EINVAL;
		goto out_unlock;
	}

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
	 * Check if the remain-on-channel is on a different band and that
	 * requires context removal, see iwl_mvm_phy_ctxt_changed(). If
	 * so, we'll need to release and then re-configure here, since we
	 * must not remove a PHY context that's part of a binding.
	 */
	band_change_removal =
		fw_has_capa(&mvm->fw->ucode_capa,
			    IWL_UCODE_TLV_CAPA_BINDING_CDB_SUPPORT) &&
		mvmvif->phy_ctxt->channel->band != chandef.chan->band;

	if (mvmvif->phy_ctxt->ref == 1 && !band_change_removal) {
		/*
		 * Change the PHY context configuration as it is currently
		 * referenced only by the P2P Device MAC (and we can modify it)
		 */
		ret = iwl_mvm_phy_ctxt_changed(mvm, mvmvif->phy_ctxt,
					       &chandef, 1, 1);
		if (ret)
			goto out_unlock;
	} else {
		/*
		 * The PHY context is shared with other MACs (or we're trying to
		 * switch bands), so remove the P2P Device from the binding,
		 * allocate an new PHY context and create a new binding.
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

static int iwl_mvm_cancel_roc(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	IWL_DEBUG_MAC80211(mvm, "enter\n");

	mutex_lock(&mvm->mutex);
	iwl_mvm_stop_roc(mvm, vif);
	mutex_unlock(&mvm->mutex);

	IWL_DEBUG_MAC80211(mvm, "leave\n");
	return 0;
}

struct iwl_mvm_ftm_responder_iter_data {
	bool responder;
	struct ieee80211_chanctx_conf *ctx;
};

static void iwl_mvm_ftm_responder_chanctx_iter(void *_data, u8 *mac,
					       struct ieee80211_vif *vif)
{
	struct iwl_mvm_ftm_responder_iter_data *data = _data;

	if (rcu_access_pointer(vif->bss_conf.chanctx_conf) == data->ctx &&
	    vif->type == NL80211_IFTYPE_AP && vif->bss_conf.ftmr_params)
		data->responder = true;
}

static bool iwl_mvm_is_ftm_responder_chanctx(struct iwl_mvm *mvm,
					     struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm_ftm_responder_iter_data data = {
		.responder = false,
		.ctx = ctx,
	};

	ieee80211_iterate_active_interfaces_atomic(mvm->hw,
					IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_ftm_responder_chanctx_iter,
					&data);
	return data.responder;
}

static int __iwl_mvm_add_chanctx(struct iwl_mvm *mvm,
				 struct ieee80211_chanctx_conf *ctx)
{
	u16 *phy_ctxt_id = (u16 *)ctx->drv_priv;
	struct iwl_mvm_phy_ctxt *phy_ctxt;
	bool responder = iwl_mvm_is_ftm_responder_chanctx(mvm, ctx);
	struct cfg80211_chan_def *def = responder ? &ctx->def : &ctx->min_def;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	IWL_DEBUG_MAC80211(mvm, "Add channel context\n");

	phy_ctxt = iwl_mvm_get_free_phy_ctxt(mvm);
	if (!phy_ctxt) {
		ret = -ENOSPC;
		goto out;
	}

	ret = iwl_mvm_phy_ctxt_changed(mvm, phy_ctxt, def,
				       ctx->rx_chains_static,
				       ctx->rx_chains_dynamic);
	if (ret) {
		IWL_ERR(mvm, "Failed to add PHY context\n");
		goto out;
	}

	iwl_mvm_phy_ctxt_ref(mvm, phy_ctxt);
	*phy_ctxt_id = phy_ctxt->id;
out:
	return ret;
}

static int iwl_mvm_add_chanctx(struct ieee80211_hw *hw,
			       struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);
	ret = __iwl_mvm_add_chanctx(mvm, ctx);
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void __iwl_mvm_remove_chanctx(struct iwl_mvm *mvm,
				     struct ieee80211_chanctx_conf *ctx)
{
	u16 *phy_ctxt_id = (u16 *)ctx->drv_priv;
	struct iwl_mvm_phy_ctxt *phy_ctxt = &mvm->phy_ctxts[*phy_ctxt_id];

	lockdep_assert_held(&mvm->mutex);

	iwl_mvm_phy_ctxt_unref(mvm, phy_ctxt);
}

static void iwl_mvm_remove_chanctx(struct ieee80211_hw *hw,
				   struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);
	__iwl_mvm_remove_chanctx(mvm, ctx);
	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_change_chanctx(struct ieee80211_hw *hw,
				   struct ieee80211_chanctx_conf *ctx,
				   u32 changed)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	u16 *phy_ctxt_id = (u16 *)ctx->drv_priv;
	struct iwl_mvm_phy_ctxt *phy_ctxt = &mvm->phy_ctxts[*phy_ctxt_id];
	bool responder = iwl_mvm_is_ftm_responder_chanctx(mvm, ctx);
	struct cfg80211_chan_def *def = responder ? &ctx->def : &ctx->min_def;

	if (WARN_ONCE((phy_ctxt->ref > 1) &&
		      (changed & ~(IEEE80211_CHANCTX_CHANGE_WIDTH |
				   IEEE80211_CHANCTX_CHANGE_RX_CHAINS |
				   IEEE80211_CHANCTX_CHANGE_RADAR |
				   IEEE80211_CHANCTX_CHANGE_MIN_WIDTH)),
		      "Cannot change PHY. Ref=%d, changed=0x%X\n",
		      phy_ctxt->ref, changed))
		return;

	mutex_lock(&mvm->mutex);

	/* we are only changing the min_width, may be a noop */
	if (changed == IEEE80211_CHANCTX_CHANGE_MIN_WIDTH) {
		if (phy_ctxt->width == def->width)
			goto out_unlock;

		/* we are just toggling between 20_NOHT and 20 */
		if (phy_ctxt->width <= NL80211_CHAN_WIDTH_20 &&
		    def->width <= NL80211_CHAN_WIDTH_20)
			goto out_unlock;
	}

	iwl_mvm_bt_coex_vif_change(mvm);
	iwl_mvm_phy_ctxt_changed(mvm, phy_ctxt, def,
				 ctx->rx_chains_static,
				 ctx->rx_chains_dynamic);

out_unlock:
	mutex_unlock(&mvm->mutex);
}

static int __iwl_mvm_assign_vif_chanctx(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					struct ieee80211_chanctx_conf *ctx,
					bool switching_chanctx)
{
	u16 *phy_ctxt_id = (u16 *)ctx->drv_priv;
	struct iwl_mvm_phy_ctxt *phy_ctxt = &mvm->phy_ctxts[*phy_ctxt_id];
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	lockdep_assert_held(&mvm->mutex);

	mvmvif->phy_ctxt = phy_ctxt;

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
		/* only needed if we're switching chanctx (i.e. during CSA) */
		if (switching_chanctx) {
			mvmvif->ap_ibss_active = true;
			break;
		}
		fallthrough;
	case NL80211_IFTYPE_ADHOC:
		/*
		 * The AP binding flow is handled as part of the start_ap flow
		 * (in bss_info_changed), similarly for IBSS.
		 */
		ret = 0;
		goto out;
	case NL80211_IFTYPE_STATION:
		mvmvif->csa_bcn_pending = false;
		break;
	case NL80211_IFTYPE_MONITOR:
		/* always disable PS when a monitor interface is active */
		mvmvif->ps_disabled = true;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	ret = iwl_mvm_binding_add_vif(mvm, vif);
	if (ret)
		goto out;

	/*
	 * Power state must be updated before quotas,
	 * otherwise fw will complain.
	 */
	iwl_mvm_power_update_mac(mvm);

	/* Setting the quota at this stage is only required for monitor
	 * interfaces. For the other types, the bss_info changed flow
	 * will handle quota settings.
	 */
	if (vif->type == NL80211_IFTYPE_MONITOR) {
		mvmvif->monitor_active = true;
		ret = iwl_mvm_update_quotas(mvm, false, NULL);
		if (ret)
			goto out_remove_binding;

		ret = iwl_mvm_add_snif_sta(mvm, vif);
		if (ret)
			goto out_remove_binding;

	}

	/* Handle binding during CSA */
	if (vif->type == NL80211_IFTYPE_AP) {
		iwl_mvm_update_quotas(mvm, false, NULL);
		iwl_mvm_mac_ctxt_changed(mvm, vif, false, NULL);
	}

	if (switching_chanctx && vif->type == NL80211_IFTYPE_STATION) {
		mvmvif->csa_bcn_pending = true;

		if (!fw_has_capa(&mvm->fw->ucode_capa,
				 IWL_UCODE_TLV_CAPA_CHANNEL_SWITCH_CMD)) {
			u32 duration = 3 * vif->bss_conf.beacon_int;

			/* Protect the session to make sure we hear the first
			 * beacon on the new channel.
			 */
			iwl_mvm_protect_session(mvm, vif, duration, duration,
						vif->bss_conf.beacon_int / 2,
						true);
		}

		iwl_mvm_update_quotas(mvm, false, NULL);
	}

	goto out;

out_remove_binding:
	iwl_mvm_binding_remove_vif(mvm, vif);
	iwl_mvm_power_update_mac(mvm);
out:
	if (ret)
		mvmvif->phy_ctxt = NULL;
	return ret;
}
static int iwl_mvm_assign_vif_chanctx(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_bss_conf *link_conf,
				      struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);
	ret = __iwl_mvm_assign_vif_chanctx(mvm, vif, ctx, false);
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void __iwl_mvm_unassign_vif_chanctx(struct iwl_mvm *mvm,
					   struct ieee80211_vif *vif,
					   struct ieee80211_chanctx_conf *ctx,
					   bool switching_chanctx)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct ieee80211_vif *disabled_vif = NULL;

	lockdep_assert_held(&mvm->mutex);
	iwl_mvm_remove_time_event(mvm, mvmvif, &mvmvif->time_event_data);

	switch (vif->type) {
	case NL80211_IFTYPE_ADHOC:
		goto out;
	case NL80211_IFTYPE_MONITOR:
		mvmvif->monitor_active = false;
		mvmvif->ps_disabled = false;
		iwl_mvm_rm_snif_sta(mvm, vif);
		break;
	case NL80211_IFTYPE_AP:
		/* This part is triggered only during CSA */
		if (!switching_chanctx || !mvmvif->ap_ibss_active)
			goto out;

		mvmvif->csa_countdown = false;

		/* Set CS bit on all the stations */
		iwl_mvm_modify_all_sta_disable_tx(mvm, mvmvif, true);

		/* Save blocked iface, the timeout is set on the next beacon */
		rcu_assign_pointer(mvm->csa_tx_blocked_vif, vif);

		mvmvif->ap_ibss_active = false;
		break;
	case NL80211_IFTYPE_STATION:
		if (!switching_chanctx)
			break;

		disabled_vif = vif;

		if (!fw_has_capa(&mvm->fw->ucode_capa,
				 IWL_UCODE_TLV_CAPA_CHANNEL_SWITCH_CMD))
			iwl_mvm_mac_ctxt_changed(mvm, vif, true, NULL);
		break;
	default:
		break;
	}

	iwl_mvm_update_quotas(mvm, false, disabled_vif);
	iwl_mvm_binding_remove_vif(mvm, vif);

out:
	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_CHANNEL_SWITCH_CMD) &&
	    switching_chanctx)
		return;
	mvmvif->phy_ctxt = NULL;
	iwl_mvm_power_update_mac(mvm);
}

static void iwl_mvm_unassign_vif_chanctx(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_bss_conf *link_conf,
					 struct ieee80211_chanctx_conf *ctx)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);
	__iwl_mvm_unassign_vif_chanctx(mvm, vif, ctx, false);
	mutex_unlock(&mvm->mutex);
}

static int
iwl_mvm_switch_vif_chanctx_swap(struct iwl_mvm *mvm,
				struct ieee80211_vif_chanctx_switch *vifs)
{
	int ret;

	mutex_lock(&mvm->mutex);
	__iwl_mvm_unassign_vif_chanctx(mvm, vifs[0].vif, vifs[0].old_ctx, true);
	__iwl_mvm_remove_chanctx(mvm, vifs[0].old_ctx);

	ret = __iwl_mvm_add_chanctx(mvm, vifs[0].new_ctx);
	if (ret) {
		IWL_ERR(mvm, "failed to add new_ctx during channel switch\n");
		goto out_reassign;
	}

	ret = __iwl_mvm_assign_vif_chanctx(mvm, vifs[0].vif, vifs[0].new_ctx,
					   true);
	if (ret) {
		IWL_ERR(mvm,
			"failed to assign new_ctx during channel switch\n");
		goto out_remove;
	}

	/* we don't support TDLS during DCM - can be caused by channel switch */
	if (iwl_mvm_phy_ctx_count(mvm) > 1)
		iwl_mvm_teardown_tdls_peers(mvm);

	goto out;

out_remove:
	__iwl_mvm_remove_chanctx(mvm, vifs[0].new_ctx);

out_reassign:
	if (__iwl_mvm_add_chanctx(mvm, vifs[0].old_ctx)) {
		IWL_ERR(mvm, "failed to add old_ctx back after failure.\n");
		goto out_restart;
	}

	if (__iwl_mvm_assign_vif_chanctx(mvm, vifs[0].vif, vifs[0].old_ctx,
					 true)) {
		IWL_ERR(mvm, "failed to reassign old_ctx after failure.\n");
		goto out_restart;
	}

	goto out;

out_restart:
	/* things keep failing, better restart the hw */
	iwl_mvm_nic_restart(mvm, false);

out:
	mutex_unlock(&mvm->mutex);

	return ret;
}

static int
iwl_mvm_switch_vif_chanctx_reassign(struct iwl_mvm *mvm,
				    struct ieee80211_vif_chanctx_switch *vifs)
{
	int ret;

	mutex_lock(&mvm->mutex);
	__iwl_mvm_unassign_vif_chanctx(mvm, vifs[0].vif, vifs[0].old_ctx, true);

	ret = __iwl_mvm_assign_vif_chanctx(mvm, vifs[0].vif, vifs[0].new_ctx,
					   true);
	if (ret) {
		IWL_ERR(mvm,
			"failed to assign new_ctx during channel switch\n");
		goto out_reassign;
	}

	goto out;

out_reassign:
	if (__iwl_mvm_assign_vif_chanctx(mvm, vifs[0].vif, vifs[0].old_ctx,
					 true)) {
		IWL_ERR(mvm, "failed to reassign old_ctx after failure.\n");
		goto out_restart;
	}

	goto out;

out_restart:
	/* things keep failing, better restart the hw */
	iwl_mvm_nic_restart(mvm, false);

out:
	mutex_unlock(&mvm->mutex);

	return ret;
}

static int iwl_mvm_switch_vif_chanctx(struct ieee80211_hw *hw,
				      struct ieee80211_vif_chanctx_switch *vifs,
				      int n_vifs,
				      enum ieee80211_chanctx_switch_mode mode)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	/* we only support a single-vif right now */
	if (n_vifs > 1)
		return -EOPNOTSUPP;

	switch (mode) {
	case CHANCTX_SWMODE_SWAP_CONTEXTS:
		ret = iwl_mvm_switch_vif_chanctx_swap(mvm, vifs);
		break;
	case CHANCTX_SWMODE_REASSIGN_VIF:
		ret = iwl_mvm_switch_vif_chanctx_reassign(mvm, vifs);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int iwl_mvm_tx_last_beacon(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	return mvm->ibss_manager;
}

static int iwl_mvm_set_tim(struct ieee80211_hw *hw,
			   struct ieee80211_sta *sta,
			   bool set)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);

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

	err = nla_parse_deprecated(tb, IWL_MVM_TM_ATTR_MAX, data, len,
				   iwl_mvm_tm_policy, NULL);
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

		return iwl_mvm_update_quotas(mvm, true, NULL);
	case IWL_MVM_TM_CMD_SET_BEACON_FILTER:
		/* must be associated client vif - ignore authorized */
		if (!vif || vif->type != NL80211_IFTYPE_STATION ||
		    !vif->cfg.assoc || !vif->bss_conf.dtim_period ||
		    !tb[IWL_MVM_TM_ATTR_BEACON_FILTER_STATE])
			return -EINVAL;

		if (nla_get_u32(tb[IWL_MVM_TM_ATTR_BEACON_FILTER_STATE]))
			return iwl_mvm_enable_beacon_filter(mvm, vif, 0);
		return iwl_mvm_disable_beacon_filter(mvm, vif, 0);
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

static void iwl_mvm_channel_switch(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_channel_switch *chsw)
{
	/* By implementing this operation, we prevent mac80211 from
	 * starting its own channel switch timer, so that we can call
	 * ieee80211_chswitch_done() ourselves at the right time
	 * (which is when the absence time event starts).
	 */

	IWL_DEBUG_MAC80211(IWL_MAC80211_GET_MVM(hw),
			   "dummy channel switch op\n");
}

static int iwl_mvm_schedule_client_csa(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif,
				       struct ieee80211_channel_switch *chsw)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_chan_switch_te_cmd cmd = {
		.mac_id = cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
							  mvmvif->color)),
		.action = cpu_to_le32(FW_CTXT_ACTION_ADD),
		.tsf = cpu_to_le32(chsw->timestamp),
		.cs_count = chsw->count,
		.cs_mode = chsw->block_tx,
	};

	lockdep_assert_held(&mvm->mutex);

	if (chsw->delay)
		cmd.cs_delayed_bcn_count =
			DIV_ROUND_UP(chsw->delay, vif->bss_conf.beacon_int);

	return iwl_mvm_send_cmd_pdu(mvm,
				    WIDE_ID(MAC_CONF_GROUP,
					    CHANNEL_SWITCH_TIME_EVENT_CMD),
				    0, sizeof(cmd), &cmd);
}

static int iwl_mvm_old_pre_chan_sw_sta(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif,
				       struct ieee80211_channel_switch *chsw)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	u32 apply_time;

	/* Schedule the time event to a bit before beacon 1,
	 * to make sure we're in the new channel when the
	 * GO/AP arrives. In case count <= 1 immediately schedule the
	 * TE (this might result with some packet loss or connection
	 * loss).
	 */
	if (chsw->count <= 1)
		apply_time = 0;
	else
		apply_time = chsw->device_timestamp +
			((vif->bss_conf.beacon_int * (chsw->count - 1) -
			  IWL_MVM_CHANNEL_SWITCH_TIME_CLIENT) * 1024);

	if (chsw->block_tx)
		iwl_mvm_csa_client_absent(mvm, vif);

	if (mvmvif->bf_data.bf_enabled) {
		int ret = iwl_mvm_disable_beacon_filter(mvm, vif, 0);

		if (ret)
			return ret;
	}

	iwl_mvm_schedule_csa_period(mvm, vif, vif->bss_conf.beacon_int,
				    apply_time);

	return 0;
}

#define IWL_MAX_CSA_BLOCK_TX 1500
static int iwl_mvm_pre_channel_switch(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_channel_switch *chsw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct ieee80211_vif *csa_vif;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	mutex_lock(&mvm->mutex);

	mvmvif->csa_failed = false;

	IWL_DEBUG_MAC80211(mvm, "pre CSA to freq %d\n",
			   chsw->chandef.center_freq1);

	iwl_fw_dbg_trigger_simple_stop(&mvm->fwrt,
				       ieee80211_vif_to_wdev(vif),
				       FW_DBG_TRIGGER_CHANNEL_SWITCH);

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
		csa_vif =
			rcu_dereference_protected(mvm->csa_vif,
						  lockdep_is_held(&mvm->mutex));
		if (WARN_ONCE(csa_vif && csa_vif->bss_conf.csa_active,
			      "Another CSA is already in progress")) {
			ret = -EBUSY;
			goto out_unlock;
		}

		/* we still didn't unblock tx. prevent new CS meanwhile */
		if (rcu_dereference_protected(mvm->csa_tx_blocked_vif,
					      lockdep_is_held(&mvm->mutex))) {
			ret = -EBUSY;
			goto out_unlock;
		}

		rcu_assign_pointer(mvm->csa_vif, vif);

		if (WARN_ONCE(mvmvif->csa_countdown,
			      "Previous CSA countdown didn't complete")) {
			ret = -EBUSY;
			goto out_unlock;
		}

		mvmvif->csa_target_freq = chsw->chandef.chan->center_freq;

		break;
	case NL80211_IFTYPE_STATION:
		/*
		 * In the new flow FW is in charge of timing the switch so there
		 * is no need for all of this
		 */
		if (iwl_fw_lookup_notif_ver(mvm->fw, MAC_CONF_GROUP,
					    CHANNEL_SWITCH_ERROR_NOTIF,
					    0))
			break;

		/*
		 * We haven't configured the firmware to be associated yet since
		 * we don't know the dtim period. In this case, the firmware can't
		 * track the beacons.
		 */
		if (!vif->cfg.assoc || !vif->bss_conf.dtim_period) {
			ret = -EBUSY;
			goto out_unlock;
		}

		if (chsw->delay > IWL_MAX_CSA_BLOCK_TX)
			schedule_delayed_work(&mvmvif->csa_work, 0);

		if (chsw->block_tx) {
			/*
			 * In case of undetermined / long time with immediate
			 * quiet monitor status to gracefully disconnect
			 */
			if (!chsw->count ||
			    chsw->count * vif->bss_conf.beacon_int >
			    IWL_MAX_CSA_BLOCK_TX)
				schedule_delayed_work(&mvmvif->csa_work,
						      msecs_to_jiffies(IWL_MAX_CSA_BLOCK_TX));
		}

		if (!fw_has_capa(&mvm->fw->ucode_capa,
				 IWL_UCODE_TLV_CAPA_CHANNEL_SWITCH_CMD)) {
			ret = iwl_mvm_old_pre_chan_sw_sta(mvm, vif, chsw);
			if (ret)
				goto out_unlock;
		} else {
			iwl_mvm_schedule_client_csa(mvm, vif, chsw);
		}

		mvmvif->csa_count = chsw->count;
		mvmvif->csa_misbehave = false;
		break;
	default:
		break;
	}

	mvmvif->ps_disabled = true;

	ret = iwl_mvm_power_update_ps(mvm);
	if (ret)
		goto out_unlock;

	/* we won't be on this channel any longer */
	iwl_mvm_teardown_tdls_peers(mvm);

out_unlock:
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void iwl_mvm_channel_switch_rx_beacon(struct ieee80211_hw *hw,
					     struct ieee80211_vif *vif,
					     struct ieee80211_channel_switch *chsw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_chan_switch_te_cmd cmd = {
		.mac_id = cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id,
							  mvmvif->color)),
		.action = cpu_to_le32(FW_CTXT_ACTION_MODIFY),
		.tsf = cpu_to_le32(chsw->timestamp),
		.cs_count = chsw->count,
		.cs_mode = chsw->block_tx,
	};

	/*
	 * In the new flow FW is in charge of timing the switch so there is no
	 * need for all of this
	 */
	if (iwl_fw_lookup_notif_ver(mvm->fw, MAC_CONF_GROUP,
				    CHANNEL_SWITCH_ERROR_NOTIF, 0))
		return;

	if (!fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_CS_MODIFY))
		return;

	IWL_DEBUG_MAC80211(mvm, "Modify CSA on mac %d count = %d (old %d) mode = %d\n",
			   mvmvif->id, chsw->count, mvmvif->csa_count, chsw->block_tx);

	if (chsw->count >= mvmvif->csa_count && chsw->block_tx) {
		if (mvmvif->csa_misbehave) {
			/* Second time, give up on this AP*/
			iwl_mvm_abort_channel_switch(hw, vif);
			ieee80211_chswitch_done(vif, false);
			mvmvif->csa_misbehave = false;
			return;
		}
		mvmvif->csa_misbehave = true;
	}
	mvmvif->csa_count = chsw->count;

	mutex_lock(&mvm->mutex);
	if (mvmvif->csa_failed)
		goto out_unlock;

	WARN_ON(iwl_mvm_send_cmd_pdu(mvm,
				     WIDE_ID(MAC_CONF_GROUP,
					     CHANNEL_SWITCH_TIME_EVENT_CMD),
				     0, sizeof(cmd), &cmd));
out_unlock:
	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_flush_no_vif(struct iwl_mvm *mvm, u32 queues, bool drop)
{
	int i;

	if (!iwl_mvm_has_new_tx_api(mvm)) {
		if (drop) {
			mutex_lock(&mvm->mutex);
			iwl_mvm_flush_tx_path(mvm,
				iwl_mvm_flushable_queues(mvm) & queues);
			mutex_unlock(&mvm->mutex);
		} else {
			iwl_trans_wait_tx_queues_empty(mvm->trans, queues);
		}
		return;
	}

	mutex_lock(&mvm->mutex);
	for (i = 0; i < mvm->fw->ucode_capa.num_stations; i++) {
		struct ieee80211_sta *sta;

		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[i],
						lockdep_is_held(&mvm->mutex));
		if (IS_ERR_OR_NULL(sta))
			continue;

		if (drop)
			iwl_mvm_flush_sta_tids(mvm, i, 0xFFFF);
		else
			iwl_mvm_wait_sta_queues_empty(mvm,
					iwl_mvm_sta_from_mac80211(sta));
	}
	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_mac_flush(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif, u32 queues, bool drop)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif;
	struct iwl_mvm_sta *mvmsta;
	struct ieee80211_sta *sta;
	int i;
	u32 msk = 0;

	if (!vif) {
		iwl_mvm_flush_no_vif(mvm, queues, drop);
		return;
	}

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	/* Make sure we're done with the deferred traffic before flushing */
	flush_work(&mvm->add_stream_wk);

	mutex_lock(&mvm->mutex);
	mvmvif = iwl_mvm_vif_from_mac80211(vif);

	/* flush the AP-station and all TDLS peers */
	for (i = 0; i < mvm->fw->ucode_capa.num_stations; i++) {
		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[i],
						lockdep_is_held(&mvm->mutex));
		if (IS_ERR_OR_NULL(sta))
			continue;

		mvmsta = iwl_mvm_sta_from_mac80211(sta);
		if (mvmsta->vif != vif)
			continue;

		/* make sure only TDLS peers or the AP are flushed */
		WARN_ON(i != mvmvif->ap_sta_id && !sta->tdls);

		if (drop) {
			if (iwl_mvm_flush_sta(mvm, mvmsta, false))
				IWL_ERR(mvm, "flush request fail\n");
		} else {
			msk |= mvmsta->tfd_queue_msk;
			if (iwl_mvm_has_new_tx_api(mvm))
				iwl_mvm_wait_sta_queues_empty(mvm, mvmsta);
		}
	}

	mutex_unlock(&mvm->mutex);

	/* this can take a while, and we may need/want other operations
	 * to succeed while doing this, so do it without the mutex held
	 */
	if (!drop && !iwl_mvm_has_new_tx_api(mvm))
		iwl_trans_wait_tx_queues_empty(mvm->trans, msk);
}

static int iwl_mvm_mac_get_survey(struct ieee80211_hw *hw, int idx,
				  struct survey_info *survey)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	memset(survey, 0, sizeof(*survey));

	/* only support global statistics right now */
	if (idx != 0)
		return -ENOENT;

	if (!fw_has_capa(&mvm->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_RADIO_BEACON_STATS))
		return -ENOENT;

	mutex_lock(&mvm->mutex);

	if (iwl_mvm_firmware_running(mvm)) {
		ret = iwl_mvm_request_statistics(mvm, false);
		if (ret)
			goto out;
	}

	survey->filled = SURVEY_INFO_TIME |
			 SURVEY_INFO_TIME_RX |
			 SURVEY_INFO_TIME_TX |
			 SURVEY_INFO_TIME_SCAN;
	survey->time = mvm->accu_radio_stats.on_time_rf +
		       mvm->radio_stats.on_time_rf;
	do_div(survey->time, USEC_PER_MSEC);

	survey->time_rx = mvm->accu_radio_stats.rx_time +
			  mvm->radio_stats.rx_time;
	do_div(survey->time_rx, USEC_PER_MSEC);

	survey->time_tx = mvm->accu_radio_stats.tx_time +
			  mvm->radio_stats.tx_time;
	do_div(survey->time_tx, USEC_PER_MSEC);

	survey->time_scan = mvm->accu_radio_stats.on_time_scan +
			    mvm->radio_stats.on_time_scan;
	do_div(survey->time_scan, USEC_PER_MSEC);

	ret = 0;
 out:
	mutex_unlock(&mvm->mutex);
	return ret;
}

static void iwl_mvm_set_sta_rate(u32 rate_n_flags, struct rate_info *rinfo)
{
	u32 format = rate_n_flags & RATE_MCS_MOD_TYPE_MSK;
	u32 gi_ltf;

	switch (rate_n_flags & RATE_MCS_CHAN_WIDTH_MSK) {
	case RATE_MCS_CHAN_WIDTH_20:
		rinfo->bw = RATE_INFO_BW_20;
		break;
	case RATE_MCS_CHAN_WIDTH_40:
		rinfo->bw = RATE_INFO_BW_40;
		break;
	case RATE_MCS_CHAN_WIDTH_80:
		rinfo->bw = RATE_INFO_BW_80;
		break;
	case RATE_MCS_CHAN_WIDTH_160:
		rinfo->bw = RATE_INFO_BW_160;
		break;
	}

	if (format == RATE_MCS_CCK_MSK ||
	    format == RATE_MCS_LEGACY_OFDM_MSK) {
		int rate = u32_get_bits(rate_n_flags, RATE_LEGACY_RATE_MSK);

		/* add the offset needed to get to the legacy ofdm indices */
		if (format == RATE_MCS_LEGACY_OFDM_MSK)
			rate += IWL_FIRST_OFDM_RATE;

		switch (rate) {
		case IWL_RATE_1M_INDEX:
			rinfo->legacy = 10;
			break;
		case IWL_RATE_2M_INDEX:
			rinfo->legacy = 20;
			break;
		case IWL_RATE_5M_INDEX:
			rinfo->legacy = 55;
			break;
		case IWL_RATE_11M_INDEX:
			rinfo->legacy = 110;
			break;
		case IWL_RATE_6M_INDEX:
			rinfo->legacy = 60;
			break;
		case IWL_RATE_9M_INDEX:
			rinfo->legacy = 90;
			break;
		case IWL_RATE_12M_INDEX:
			rinfo->legacy = 120;
			break;
		case IWL_RATE_18M_INDEX:
			rinfo->legacy = 180;
			break;
		case IWL_RATE_24M_INDEX:
			rinfo->legacy = 240;
			break;
		case IWL_RATE_36M_INDEX:
			rinfo->legacy = 360;
			break;
		case IWL_RATE_48M_INDEX:
			rinfo->legacy = 480;
			break;
		case IWL_RATE_54M_INDEX:
			rinfo->legacy = 540;
		}
		return;
	}

	rinfo->nss = u32_get_bits(rate_n_flags,
				  RATE_MCS_NSS_MSK) + 1;
	rinfo->mcs = format == RATE_MCS_HT_MSK ?
		RATE_HT_MCS_INDEX(rate_n_flags) :
		u32_get_bits(rate_n_flags, RATE_MCS_CODE_MSK);

	if (rate_n_flags & RATE_MCS_SGI_MSK)
		rinfo->flags |= RATE_INFO_FLAGS_SHORT_GI;

	switch (format) {
	case RATE_MCS_HE_MSK:
		gi_ltf = u32_get_bits(rate_n_flags, RATE_MCS_HE_GI_LTF_MSK);

		rinfo->flags |= RATE_INFO_FLAGS_HE_MCS;

		if (rate_n_flags & RATE_MCS_HE_106T_MSK) {
			rinfo->bw = RATE_INFO_BW_HE_RU;
			rinfo->he_ru_alloc = NL80211_RATE_INFO_HE_RU_ALLOC_106;
		}

		switch (rate_n_flags & RATE_MCS_HE_TYPE_MSK) {
		case RATE_MCS_HE_TYPE_SU:
		case RATE_MCS_HE_TYPE_EXT_SU:
			if (gi_ltf == 0 || gi_ltf == 1)
				rinfo->he_gi = NL80211_RATE_INFO_HE_GI_0_8;
			else if (gi_ltf == 2)
				rinfo->he_gi = NL80211_RATE_INFO_HE_GI_1_6;
			else if (gi_ltf == 3)
				rinfo->he_gi = NL80211_RATE_INFO_HE_GI_3_2;
			else
				rinfo->he_gi = NL80211_RATE_INFO_HE_GI_0_8;
			break;
		case RATE_MCS_HE_TYPE_MU:
			if (gi_ltf == 0 || gi_ltf == 1)
				rinfo->he_gi = NL80211_RATE_INFO_HE_GI_0_8;
			else if (gi_ltf == 2)
				rinfo->he_gi = NL80211_RATE_INFO_HE_GI_1_6;
			else
				rinfo->he_gi = NL80211_RATE_INFO_HE_GI_3_2;
			break;
		case RATE_MCS_HE_TYPE_TRIG:
			if (gi_ltf == 0 || gi_ltf == 1)
				rinfo->he_gi = NL80211_RATE_INFO_HE_GI_1_6;
			else
				rinfo->he_gi = NL80211_RATE_INFO_HE_GI_3_2;
			break;
		}

		if (rate_n_flags & RATE_HE_DUAL_CARRIER_MODE_MSK)
			rinfo->he_dcm = 1;
		break;
	case RATE_MCS_HT_MSK:
		rinfo->flags |= RATE_INFO_FLAGS_MCS;
		break;
	case RATE_MCS_VHT_MSK:
		rinfo->flags |= RATE_INFO_FLAGS_VHT_MCS;
		break;
	}
}

static void iwl_mvm_mac_sta_statistics(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *sta,
				       struct station_info *sinfo)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);

	if (mvmsta->avg_energy) {
		sinfo->signal_avg = -(s8)mvmsta->avg_energy;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL_AVG);
	}

	if (iwl_mvm_has_tlc_offload(mvm)) {
		struct iwl_lq_sta_rs_fw *lq_sta = &mvmsta->lq_sta.rs_fw;

		iwl_mvm_set_sta_rate(lq_sta->last_rate_n_flags, &sinfo->txrate);
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
	}

	/* if beacon filtering isn't on mac80211 does it anyway */
	if (!(vif->driver_flags & IEEE80211_VIF_BEACON_FILTER))
		return;

	if (!vif->cfg.assoc)
		return;

	mutex_lock(&mvm->mutex);

	if (mvmvif->ap_sta_id != mvmsta->sta_id)
		goto unlock;

	if (iwl_mvm_request_statistics(mvm, false))
		goto unlock;

	sinfo->rx_beacon = mvmvif->beacon_stats.num_beacons +
			   mvmvif->beacon_stats.accu_num_beacons;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_BEACON_RX);
	if (mvmvif->beacon_stats.avg_signal) {
		/* firmware only reports a value after RXing a few beacons */
		sinfo->rx_beacon_signal_avg = mvmvif->beacon_stats.avg_signal;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_BEACON_SIGNAL_AVG);
	}
 unlock:
	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_event_mlme_callback_ini(struct iwl_mvm *mvm,
					    struct ieee80211_vif *vif,
					    const  struct ieee80211_mlme_event *mlme)
{
	if ((mlme->data == ASSOC_EVENT || mlme->data == AUTH_EVENT) &&
	    (mlme->status == MLME_DENIED || mlme->status == MLME_TIMEOUT)) {
		iwl_dbg_tlv_time_point(&mvm->fwrt,
				       IWL_FW_INI_TIME_POINT_ASSOC_FAILED,
				       NULL);
		return;
	}

	if (mlme->data == DEAUTH_RX_EVENT || mlme->data == DEAUTH_TX_EVENT) {
		iwl_dbg_tlv_time_point(&mvm->fwrt,
				       IWL_FW_INI_TIME_POINT_DEASSOC,
				       NULL);
		return;
	}
}

static void iwl_mvm_event_mlme_callback(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					const struct ieee80211_event *event)
{
#define CHECK_MLME_TRIGGER(_cnt, _fmt...)				\
	do {								\
		if ((trig_mlme->_cnt) && --(trig_mlme->_cnt))		\
			break;						\
		iwl_fw_dbg_collect_trig(&(mvm)->fwrt, trig, _fmt);	\
	} while (0)

	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_mlme *trig_mlme;

	if (iwl_trans_dbg_ini_valid(mvm->trans)) {
		iwl_mvm_event_mlme_callback_ini(mvm, vif, &event->u.mlme);
		return;
	}

	trig = iwl_fw_dbg_trigger_on(&mvm->fwrt, ieee80211_vif_to_wdev(vif),
				     FW_DBG_TRIGGER_MLME);
	if (!trig)
		return;

	trig_mlme = (void *)trig->data;

	if (event->u.mlme.data == ASSOC_EVENT) {
		if (event->u.mlme.status == MLME_DENIED)
			CHECK_MLME_TRIGGER(stop_assoc_denied,
					   "DENIED ASSOC: reason %d",
					    event->u.mlme.reason);
		else if (event->u.mlme.status == MLME_TIMEOUT)
			CHECK_MLME_TRIGGER(stop_assoc_timeout,
					   "ASSOC TIMEOUT");
	} else if (event->u.mlme.data == AUTH_EVENT) {
		if (event->u.mlme.status == MLME_DENIED)
			CHECK_MLME_TRIGGER(stop_auth_denied,
					   "DENIED AUTH: reason %d",
					   event->u.mlme.reason);
		else if (event->u.mlme.status == MLME_TIMEOUT)
			CHECK_MLME_TRIGGER(stop_auth_timeout,
					   "AUTH TIMEOUT");
	} else if (event->u.mlme.data == DEAUTH_RX_EVENT) {
		CHECK_MLME_TRIGGER(stop_rx_deauth,
				   "DEAUTH RX %d", event->u.mlme.reason);
	} else if (event->u.mlme.data == DEAUTH_TX_EVENT) {
		CHECK_MLME_TRIGGER(stop_tx_deauth,
				   "DEAUTH TX %d", event->u.mlme.reason);
	}
#undef CHECK_MLME_TRIGGER
}

static void iwl_mvm_event_bar_rx_callback(struct iwl_mvm *mvm,
					  struct ieee80211_vif *vif,
					  const struct ieee80211_event *event)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_ba *ba_trig;

	trig = iwl_fw_dbg_trigger_on(&mvm->fwrt, ieee80211_vif_to_wdev(vif),
				     FW_DBG_TRIGGER_BA);
	if (!trig)
		return;

	ba_trig = (void *)trig->data;

	if (!(le16_to_cpu(ba_trig->rx_bar) & BIT(event->u.ba.tid)))
		return;

	iwl_fw_dbg_collect_trig(&mvm->fwrt, trig,
				"BAR received from %pM, tid %d, ssn %d",
				event->u.ba.sta->addr, event->u.ba.tid,
				event->u.ba.ssn);
}

static void iwl_mvm_mac_event_callback(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       const struct ieee80211_event *event)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	switch (event->type) {
	case MLME_EVENT:
		iwl_mvm_event_mlme_callback(mvm, vif, event);
		break;
	case BAR_RX_EVENT:
		iwl_mvm_event_bar_rx_callback(mvm, vif, event);
		break;
	case BA_FRAME_TIMEOUT:
		iwl_mvm_event_frame_timeout_callback(mvm, vif, event->u.ba.sta,
						     event->u.ba.tid);
		break;
	default:
		break;
	}
}

void iwl_mvm_sync_rx_queues_internal(struct iwl_mvm *mvm,
				     enum iwl_mvm_rxq_notif_type type,
				     bool sync,
				     const void *data, u32 size)
{
	struct {
		struct iwl_rxq_sync_cmd cmd;
		struct iwl_mvm_internal_rxq_notif notif;
	} __packed cmd = {
		.cmd.rxq_mask = cpu_to_le32(BIT(mvm->trans->num_rx_queues) - 1),
		.cmd.count =
			cpu_to_le32(sizeof(struct iwl_mvm_internal_rxq_notif) +
				    size),
		.notif.type = type,
		.notif.sync = sync,
	};
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(DATA_PATH_GROUP, TRIGGER_RX_QUEUES_NOTIF_CMD),
		.data[0] = &cmd,
		.len[0] = sizeof(cmd),
		.data[1] = data,
		.len[1] = size,
		.flags = sync ? 0 : CMD_ASYNC,
	};
	int ret;

	/* size must be a multiple of DWORD */
	if (WARN_ON(cmd.cmd.count & cpu_to_le32(3)))
		return;

	if (!iwl_mvm_has_new_rx_api(mvm))
		return;

	if (sync) {
		cmd.notif.cookie = mvm->queue_sync_cookie;
		mvm->queue_sync_state = (1 << mvm->trans->num_rx_queues) - 1;
	}

	ret = iwl_mvm_send_cmd(mvm, &hcmd);
	if (ret) {
		IWL_ERR(mvm, "Failed to trigger RX queues sync (%d)\n", ret);
		goto out;
	}

	if (sync) {
		lockdep_assert_held(&mvm->mutex);
		ret = wait_event_timeout(mvm->rx_sync_waitq,
					 READ_ONCE(mvm->queue_sync_state) == 0 ||
					 iwl_mvm_is_radio_killed(mvm),
					 HZ);
		WARN_ONCE(!ret && !iwl_mvm_is_radio_killed(mvm),
			  "queue sync: failed to sync, state is 0x%lx\n",
			  mvm->queue_sync_state);
	}

out:
	if (sync) {
		mvm->queue_sync_state = 0;
		mvm->queue_sync_cookie++;
	}
}

static void iwl_mvm_sync_rx_queues(struct ieee80211_hw *hw)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);
	iwl_mvm_sync_rx_queues_internal(mvm, IWL_MVM_RXQ_EMPTY, true, NULL, 0);
	mutex_unlock(&mvm->mutex);
}

static int
iwl_mvm_mac_get_ftm_responder_stats(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct cfg80211_ftm_responder_stats *stats)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (vif->p2p || vif->type != NL80211_IFTYPE_AP ||
	    !mvmvif->ap_ibss_active || !vif->bss_conf.ftm_responder)
		return -EINVAL;

	mutex_lock(&mvm->mutex);
	*stats = mvm->ftm_resp_stats;
	mutex_unlock(&mvm->mutex);

	stats->filled = BIT(NL80211_FTM_STATS_SUCCESS_NUM) |
			BIT(NL80211_FTM_STATS_PARTIAL_NUM) |
			BIT(NL80211_FTM_STATS_FAILED_NUM) |
			BIT(NL80211_FTM_STATS_ASAP_NUM) |
			BIT(NL80211_FTM_STATS_NON_ASAP_NUM) |
			BIT(NL80211_FTM_STATS_TOTAL_DURATION_MSEC) |
			BIT(NL80211_FTM_STATS_UNKNOWN_TRIGGERS_NUM) |
			BIT(NL80211_FTM_STATS_RESCHEDULE_REQUESTS_NUM) |
			BIT(NL80211_FTM_STATS_OUT_OF_WINDOW_TRIGGERS_NUM);

	return 0;
}

static int iwl_mvm_start_pmsr(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct cfg80211_pmsr_request *request)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);
	int ret;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_ftm_start(mvm, vif, request);
	mutex_unlock(&mvm->mutex);

	return ret;
}

static void iwl_mvm_abort_pmsr(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif,
			       struct cfg80211_pmsr_request *request)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	mutex_lock(&mvm->mutex);
	iwl_mvm_ftm_abort(mvm, request);
	mutex_unlock(&mvm->mutex);
}

static bool iwl_mvm_can_hw_csum(struct sk_buff *skb)
{
	u8 protocol = ip_hdr(skb)->protocol;

	if (!IS_ENABLED(CONFIG_INET))
		return false;

	return protocol == IPPROTO_TCP || protocol == IPPROTO_UDP;
}

static bool iwl_mvm_mac_can_aggregate(struct ieee80211_hw *hw,
				      struct sk_buff *head,
				      struct sk_buff *skb)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	if (mvm->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_BZ)
		return iwl_mvm_tx_csum_bz(mvm, head, true) ==
		       iwl_mvm_tx_csum_bz(mvm, skb, true);

	/* For now don't aggregate IPv6 in AMSDU */
	if (skb->protocol != htons(ETH_P_IP))
		return false;

	if (!iwl_mvm_is_csum_supported(mvm))
		return true;

	return iwl_mvm_can_hw_csum(skb) == iwl_mvm_can_hw_csum(head);
}

const struct ieee80211_ops iwl_mvm_hw_ops = {
	.tx = iwl_mvm_mac_tx,
	.wake_tx_queue = iwl_mvm_mac_wake_tx_queue,
	.ampdu_action = iwl_mvm_mac_ampdu_action,
	.get_antenna = iwl_mvm_op_get_antenna,
	.start = iwl_mvm_mac_start,
	.reconfig_complete = iwl_mvm_mac_reconfig_complete,
	.stop = iwl_mvm_mac_stop,
	.add_interface = iwl_mvm_mac_add_interface,
	.remove_interface = iwl_mvm_mac_remove_interface,
	.config = iwl_mvm_mac_config,
	.prepare_multicast = iwl_mvm_prepare_multicast,
	.configure_filter = iwl_mvm_configure_filter,
	.config_iface_filter = iwl_mvm_config_iface_filter,
	.bss_info_changed = iwl_mvm_bss_info_changed,
	.hw_scan = iwl_mvm_mac_hw_scan,
	.cancel_hw_scan = iwl_mvm_mac_cancel_hw_scan,
	.sta_pre_rcu_remove = iwl_mvm_sta_pre_rcu_remove,
	.sta_state = iwl_mvm_mac_sta_state,
	.sta_notify = iwl_mvm_mac_sta_notify,
	.allow_buffered_frames = iwl_mvm_mac_allow_buffered_frames,
	.release_buffered_frames = iwl_mvm_mac_release_buffered_frames,
	.set_rts_threshold = iwl_mvm_mac_set_rts_threshold,
	.sta_rc_update = iwl_mvm_sta_rc_update,
	.conf_tx = iwl_mvm_mac_conf_tx,
	.mgd_prepare_tx = iwl_mvm_mac_mgd_prepare_tx,
	.mgd_complete_tx = iwl_mvm_mac_mgd_complete_tx,
	.mgd_protect_tdls_discover = iwl_mvm_mac_mgd_protect_tdls_discover,
	.flush = iwl_mvm_mac_flush,
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
	.switch_vif_chanctx = iwl_mvm_switch_vif_chanctx,

	.start_ap = iwl_mvm_start_ap,
	.stop_ap = iwl_mvm_stop_ap,
	.join_ibss = iwl_mvm_start_ibss,
	.leave_ibss = iwl_mvm_stop_ibss,

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

	.can_aggregate_in_amsdu = iwl_mvm_mac_can_aggregate,
#ifdef CONFIG_IWLWIFI_DEBUGFS
	.sta_add_debugfs = iwl_mvm_sta_add_debugfs,
#endif
};
