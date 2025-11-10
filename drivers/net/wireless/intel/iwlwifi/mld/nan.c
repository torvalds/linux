// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025 Intel Corporation
 */

#include "mld.h"
#include "iface.h"
#include "fw/api/mac-cfg.h"

#define IWL_NAN_DISOVERY_BEACON_INTERNVAL_TU 512
#define IWL_NAN_RSSI_CLOSE 55
#define IWL_NAN_RSSI_MIDDLE 70

/* possible discovery channels for the 5 GHz band*/
#define IWL_NAN_CHANNEL_UNII1 44
#define IWL_NAN_CHANNEL_UNII3 149

bool iwl_mld_nan_supported(struct iwl_mld *mld)
{
	return fw_has_capa(&mld->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_NAN_SYNC_SUPPORT);
}

static bool iwl_mld_nan_can_beacon(struct ieee80211_vif *vif,
				   enum nl80211_band band, u8 channel)
{
	struct wiphy *wiphy = ieee80211_vif_to_wdev(vif)->wiphy;
	int freq = ieee80211_channel_to_frequency(channel, band);
	struct ieee80211_channel *chan = ieee80211_get_channel(wiphy,
							       freq);
	struct cfg80211_chan_def def;

	if (!chan)
		return false;

	cfg80211_chandef_create(&def, chan, NL80211_CHAN_NO_HT);
	return cfg80211_reg_can_beacon(wiphy, &def, vif->type);
}

int iwl_mld_start_nan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct cfg80211_nan_conf *conf)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_int_sta *aux_sta = &mld_vif->aux_sta;
	struct iwl_nan_config_cmd cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_ADD),
		.discovery_beacon_interval =
			cpu_to_le32(IWL_NAN_DISOVERY_BEACON_INTERNVAL_TU),
		.band_config = {
			{
				.rssi_close = IWL_NAN_RSSI_CLOSE,
				.rssi_middle = IWL_NAN_RSSI_MIDDLE,
				.dw_interval = 1,
			},
			{
				.rssi_close = IWL_NAN_RSSI_CLOSE,
				.rssi_middle = IWL_NAN_RSSI_MIDDLE,
				.dw_interval = 1,
			},
		},
	};
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	IWL_DEBUG_MAC80211(mld, "Start NAN: bands=0x%x\n", conf->bands);

	ether_addr_copy(cmd.nmi_addr, vif->addr);
	cmd.master_pref = conf->master_pref;
	cmd.flags = IWL_NAN_FLAG_DW_END_NOTIF_ENABLED;

	if (WARN_ON(!(conf->bands & BIT(NL80211_BAND_2GHZ))))
		return -EINVAL;

	if (conf->bands & BIT(NL80211_BAND_5GHZ)) {
		if (iwl_mld_nan_can_beacon(vif, NL80211_BAND_5GHZ,
					   IWL_NAN_CHANNEL_UNII1)) {
			cmd.hb_channel = IWL_NAN_CHANNEL_UNII1;
		} else if (iwl_mld_nan_can_beacon(vif, NL80211_BAND_5GHZ,
						  IWL_NAN_CHANNEL_UNII3)) {
			cmd.hb_channel = IWL_NAN_CHANNEL_UNII3;
		} else {
			IWL_ERR(mld, "NAN: Can't beacon on 5 GHz band\n");
			ret = -EINVAL;
		}
	} else {
		memset(&cmd.band_config[IWL_NAN_BAND_5GHZ], 0,
		       sizeof(cmd.band_config[0]));
	}

	ret = iwl_mld_add_aux_sta(mld, aux_sta);
	if (ret)
		return ret;

	cmd.sta_id = aux_sta->sta_id;

	ret = iwl_mld_send_cmd_pdu(mld,
				   WIDE_ID(MAC_CONF_GROUP, NAN_CFG_CMD),
				   &cmd);

	if (ret) {
		IWL_ERR(mld, "Failed to start NAN. ret=%d\n", ret);
		iwl_mld_remove_aux_sta(mld, vif);
	}

	return ret;
}

int iwl_mld_stop_nan(struct ieee80211_hw *hw,
		     struct ieee80211_vif *vif)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_nan_config_cmd cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE),
	};
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	ret = iwl_mld_send_cmd_pdu(mld,
				   WIDE_ID(MAC_CONF_GROUP, NAN_CFG_CMD),
				   &cmd);
	if (ret)
		IWL_ERR(mld, "NAN: Failed to stop NAN. ret=%d\n", ret);

	/* assume that higher layer guarantees that no additional frames are
	 * added before calling this callback
	 */
	iwl_mld_flush_link_sta_txqs(mld, mld_vif->aux_sta.sta_id);
	iwl_mld_remove_aux_sta(mld, vif);

	return 0;
}

void iwl_mld_handle_nan_cluster_notif(struct iwl_mld *mld,
				      struct iwl_rx_packet *pkt)
{
	struct iwl_nan_cluster_notif *notif = (void *)pkt->data;

	IWL_DEBUG_INFO(mld,
		       "NAN: cluster event: cluster_id=0x%x, flags=0x%x\n",
		       le16_to_cpu(notif->cluster_id), notif->flags);
}

void iwl_mld_handle_nan_dw_end_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt)
{
	struct iwl_nan_dw_end_notif *notif = (void *)pkt->data;
	struct iwl_mld_vif *mld_vif = mld->nan_device_vif ?
		iwl_mld_vif_from_mac80211(mld->nan_device_vif) :
		NULL;

	IWL_INFO(mld, "NAN: DW end: band=%u\n", notif->band);

	if (!mld_vif)
		return;

	if (WARN_ON(mld_vif->aux_sta.sta_id == IWL_INVALID_STA))
		return;

	IWL_DEBUG_INFO(mld, "NAN: flush queues for aux sta=%u\n",
		       mld_vif->aux_sta.sta_id);

	iwl_mld_flush_link_sta_txqs(mld, mld_vif->aux_sta.sta_id);
}
