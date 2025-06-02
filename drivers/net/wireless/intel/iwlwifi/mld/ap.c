// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024 Intel Corporation
 */
#include <linux/crc32.h>

#include <net/mac80211.h>

#include "ap.h"
#include "hcmd.h"
#include "tx.h"
#include "power.h"
#include "key.h"
#include "iwl-utils.h"

#include "fw/api/sta.h"

void iwl_mld_set_tim_idx(struct iwl_mld *mld, __le32 *tim_index,
			 u8 *beacon, u32 frame_size)
{
	u32 tim_idx;
	struct ieee80211_mgmt *mgmt = (void *)beacon;

	/* The index is relative to frame start but we start looking at the
	 * variable-length part of the beacon.
	 */
	tim_idx = mgmt->u.beacon.variable - beacon;

	/* Parse variable-length elements of beacon to find WLAN_EID_TIM */
	while ((tim_idx < (frame_size - 2)) &&
	       (beacon[tim_idx] != WLAN_EID_TIM))
		tim_idx += beacon[tim_idx + 1] + 2;

	/* If TIM field was found, set variables */
	if ((tim_idx < (frame_size - 1)) && beacon[tim_idx] == WLAN_EID_TIM)
		*tim_index = cpu_to_le32(tim_idx);
	else
		IWL_WARN(mld, "Unable to find TIM Element in beacon\n");
}

u8 iwl_mld_get_rate_flags(struct iwl_mld *mld,
			  struct ieee80211_tx_info *info,
			  struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link,
			  enum nl80211_band band)
{
	u32 legacy = link->beacon_tx_rate.control[band].legacy;
	u32 rate_idx, rate_flags = 0, fw_rate;

	/* if beacon rate was configured try using it */
	if (hweight32(legacy) == 1) {
		u32 rate = ffs(legacy) - 1;
		struct ieee80211_supported_band *sband =
			mld->hw->wiphy->bands[band];

		rate_idx = sband->bitrates[rate].hw_value;
	} else {
		rate_idx = iwl_mld_get_lowest_rate(mld, info, vif);
	}

	if (rate_idx <= IWL_LAST_CCK_RATE)
		rate_flags = IWL_MAC_BEACON_CCK;

	/* Legacy rates are indexed as follows:
	 * 0 - 3 for CCK and 0 - 7 for OFDM.
	 */
	fw_rate = (rate_idx >= IWL_FIRST_OFDM_RATE ?
		     rate_idx - IWL_FIRST_OFDM_RATE : rate_idx);

	return fw_rate | rate_flags;
}

int iwl_mld_send_beacon_template_cmd(struct iwl_mld *mld,
				     struct sk_buff *beacon,
				     struct iwl_mac_beacon_cmd *cmd)
{
	struct iwl_host_cmd hcmd = {
		.id = BEACON_TEMPLATE_CMD,
	};

	hcmd.len[0] = sizeof(*cmd);
	hcmd.data[0] = cmd;

	hcmd.len[1] = beacon->len;
	hcmd.data[1] = beacon->data;
	hcmd.dataflags[1] = IWL_HCMD_DFL_DUP;

	return iwl_mld_send_cmd(mld, &hcmd);
}

static int iwl_mld_fill_beacon_template_cmd(struct iwl_mld *mld,
					    struct ieee80211_vif *vif,
					    struct sk_buff *beacon,
					    struct iwl_mac_beacon_cmd *cmd,
					    struct ieee80211_bss_conf *link)
{
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(beacon);
	struct ieee80211_chanctx_conf *ctx;
	bool enable_fils;
	u16 flags = 0;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(!mld_link))
		return -EINVAL;

	cmd->link_id = cpu_to_le32(mld_link->fw_id);

	ctx = wiphy_dereference(mld->wiphy, link->chanctx_conf);
	if (WARN_ON(!ctx || !ctx->def.chan))
		return -EINVAL;

	enable_fils = cfg80211_channel_is_psc(ctx->def.chan) ||
		(ctx->def.chan->band == NL80211_BAND_6GHZ &&
		 ctx->def.width >= NL80211_CHAN_WIDTH_80);

	if (enable_fils) {
		flags |= IWL_MAC_BEACON_FILS;
		cmd->short_ssid = cpu_to_le32(~crc32_le(~0, vif->cfg.ssid,
							vif->cfg.ssid_len));
	}

	cmd->byte_cnt = cpu_to_le16((u16)beacon->len);

	flags |= iwl_mld_get_rate_flags(mld, info, vif, link,
					ctx->def.chan->band);

	cmd->flags = cpu_to_le16(flags);

	if (vif->type == NL80211_IFTYPE_AP) {
		iwl_mld_set_tim_idx(mld, &cmd->tim_idx,
				    beacon->data, beacon->len);

		cmd->btwt_offset =
			cpu_to_le32(iwl_find_ie_offset(beacon->data,
						       WLAN_EID_S1G_TWT,
						       beacon->len));
	}

	cmd->csa_offset =
		cpu_to_le32(iwl_find_ie_offset(beacon->data,
					       WLAN_EID_CHANNEL_SWITCH,
					       beacon->len));
	cmd->ecsa_offset =
		cpu_to_le32(iwl_find_ie_offset(beacon->data,
					       WLAN_EID_EXT_CHANSWITCH_ANN,
					       beacon->len));

	return 0;
}

/* The beacon template for the AP/GO/IBSS has changed and needs update */
int iwl_mld_update_beacon_template(struct iwl_mld *mld,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *link_conf)
{
	struct iwl_mac_beacon_cmd cmd = {};
	struct sk_buff *beacon;
	int ret;
#ifdef CONFIG_IWLWIFI_DEBUGFS
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
#endif

	WARN_ON(vif->type != NL80211_IFTYPE_AP &&
		vif->type != NL80211_IFTYPE_ADHOC);

	if (IWL_MLD_NON_TRANSMITTING_AP)
		return 0;

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (mld_vif->beacon_inject_active) {
		IWL_DEBUG_INFO(mld,
			       "Can't update template, beacon injection's active\n");
		return -EBUSY;
	}

#endif
	beacon = ieee80211_beacon_get_template(mld->hw, vif, NULL,
					       link_conf->link_id);
	if (!beacon)
		return -ENOMEM;

	ret = iwl_mld_fill_beacon_template_cmd(mld, vif, beacon, &cmd,
					       link_conf);

	if (!ret)
		ret = iwl_mld_send_beacon_template_cmd(mld, beacon, &cmd);

	dev_kfree_skb(beacon);

	return ret;
}

void iwl_mld_free_ap_early_key(struct iwl_mld *mld,
			       struct ieee80211_key_conf *key,
			       struct iwl_mld_vif *mld_vif)
{
	struct iwl_mld_link *link;

	if (WARN_ON(key->link_id < 0))
		return;

	link = iwl_mld_link_dereference_check(mld_vif, key->link_id);
	if (WARN_ON(!link))
		return;

	for (int i = 0; i < ARRAY_SIZE(link->ap_early_keys); i++) {
		if (link->ap_early_keys[i] != key)
			continue;
		/* Those weren't sent to FW, so should be marked as INVALID */
		if (WARN_ON(key->hw_key_idx != STA_KEY_IDX_INVALID))
			key->hw_key_idx = STA_KEY_IDX_INVALID;
		link->ap_early_keys[i] = NULL;
	}
}

int iwl_mld_store_ap_early_key(struct iwl_mld *mld,
			       struct ieee80211_key_conf *key,
			       struct iwl_mld_vif *mld_vif)
{
	struct iwl_mld_link *link;

	if (WARN_ON(key->link_id < 0))
		return -EINVAL;

	link = iwl_mld_link_dereference_check(mld_vif, key->link_id);
	if (WARN_ON(!link))
		return -EINVAL;

	for (int i = 0; i < ARRAY_SIZE(link->ap_early_keys); i++) {
		if (!link->ap_early_keys[i]) {
			link->ap_early_keys[i] = key;
			return 0;
		}
	}

	return -ENOSPC;
}

static int iwl_mld_send_ap_early_keys(struct iwl_mld *mld,
				      struct ieee80211_vif *vif,
				      struct ieee80211_bss_conf *link)
{
	struct iwl_mld_link *mld_link = iwl_mld_link_from_mac80211(link);
	int ret = 0;

	if (WARN_ON(!link))
		return -EINVAL;

	for (int i = 0; i < ARRAY_SIZE(mld_link->ap_early_keys); i++) {
		struct ieee80211_key_conf *key = mld_link->ap_early_keys[i];

		if (!key)
			continue;

		mld_link->ap_early_keys[i] = NULL;

		ret = iwl_mld_add_key(mld, vif, NULL, key);
		if (ret)
			break;
	}
	return ret;
}

int iwl_mld_start_ap_ibss(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	int ret;

	if (vif->type == NL80211_IFTYPE_AP)
		iwl_mld_send_ap_tx_power_constraint_cmd(mld, vif, link);

	ret = iwl_mld_update_beacon_template(mld, vif, link);
	if (ret)
		return ret;

	/* the link should be already activated when assigning chan context,
	 * and LINK_CONTEXT_MODIFY_EHT_PARAMS is deprecated
	 */
	ret = iwl_mld_change_link_in_fw(mld, link,
					LINK_CONTEXT_MODIFY_ALL &
					~(LINK_CONTEXT_MODIFY_ACTIVE |
					  LINK_CONTEXT_MODIFY_EHT_PARAMS));
	if (ret)
		return ret;

	ret = iwl_mld_add_mcast_sta(mld, vif, link);
	if (ret)
		return ret;

	ret = iwl_mld_add_bcast_sta(mld, vif, link);
	if (ret)
		goto rm_mcast;

	/* Those keys were configured by the upper layers before starting the
	 * AP. Now that it is started and the bcast and mcast sta were added to
	 * the FW, we can add the keys too.
	 */
	ret = iwl_mld_send_ap_early_keys(mld, vif, link);
	if (ret)
		goto rm_bcast;

	if (ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_AP)
		iwl_mld_vif_update_low_latency(mld, vif, true,
					       LOW_LATENCY_VIF_TYPE);

	mld_vif->ap_ibss_active = true;

	if (vif->p2p && mld->p2p_device_vif)
		return iwl_mld_mac_fw_action(mld, mld->p2p_device_vif,
					     FW_CTXT_ACTION_MODIFY);

	return 0;
rm_bcast:
	iwl_mld_remove_bcast_sta(mld, vif, link);
rm_mcast:
	iwl_mld_remove_mcast_sta(mld, vif, link);
	return ret;
}

void iwl_mld_stop_ap_ibss(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

	mld_vif->ap_ibss_active = false;

	if (vif->p2p && mld->p2p_device_vif)
		iwl_mld_mac_fw_action(mld, mld->p2p_device_vif,
				      FW_CTXT_ACTION_MODIFY);

	if (ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_AP)
		iwl_mld_vif_update_low_latency(mld, vif, false,
					       LOW_LATENCY_VIF_TYPE);

	iwl_mld_remove_bcast_sta(mld, vif, link);

	iwl_mld_remove_mcast_sta(mld, vif, link);
}
