// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025 Intel Corporation
 */

#include "mld.h"
#include "iface.h"
#include "mlo.h"
#include "fw/api/mac-cfg.h"

#define IWL_NAN_DISOVERY_BEACON_INTERNVAL_TU 512
#define IWL_NAN_RSSI_CLOSE 55
#define IWL_NAN_RSSI_MIDDLE 70

bool iwl_mld_nan_supported(struct iwl_mld *mld)
{
	return fw_has_capa(&mld->fw->ucode_capa,
			   IWL_UCODE_TLV_CAPA_NAN_SYNC_SUPPORT);
}

static int iwl_mld_nan_send_config_cmd(struct iwl_mld *mld,
				       struct iwl_nan_config_cmd *cmd,
				       u8 *beacon_data, size_t beacon_data_len)
{
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(MAC_CONF_GROUP, NAN_CFG_CMD),
	};

	hcmd.len[0] = sizeof(*cmd);
	hcmd.data[0] = cmd;

	if (beacon_data_len) {
		hcmd.len[1] = beacon_data_len;
		hcmd.data[1] = beacon_data;
		hcmd.dataflags[1] = IWL_HCMD_DFL_DUP;
	}

	return iwl_mld_send_cmd(mld, &hcmd);
}

static int iwl_mld_nan_config(struct iwl_mld *mld,
			      struct ieee80211_vif *vif,
			      struct cfg80211_nan_conf *conf,
			      enum iwl_ctxt_action action)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_nan_config_cmd cmd = {
		.action = cpu_to_le32(action),
	};
	u8 *data __free(kfree) = NULL;

	lockdep_assert_wiphy(mld->wiphy);

	ether_addr_copy(cmd.nmi_addr, vif->addr);
	cmd.master_pref = conf->master_pref;

	if (conf->cluster_id)
		memcpy(cmd.cluster_id, conf->cluster_id + 4,
		       sizeof(cmd.cluster_id));

	cmd.scan_period = conf->scan_period < 255 ? conf->scan_period : 255;
	cmd.dwell_time =
		conf->scan_dwell_time < 255 ? conf->scan_dwell_time : 255;

	if (conf->discovery_beacon_interval)
		cmd.discovery_beacon_interval =
			cpu_to_le32(conf->discovery_beacon_interval);
	else
		cmd.discovery_beacon_interval =
			cpu_to_le32(IWL_NAN_DISOVERY_BEACON_INTERNVAL_TU);

	if (conf->enable_dw_notification)
		cmd.flags = IWL_NAN_FLAG_DW_END_NOTIF_ENABLED;

	/* 2 GHz band must be supported */
	cmd.band_config[IWL_NAN_BAND_2GHZ].rssi_close =
		abs(conf->band_cfgs[NL80211_BAND_2GHZ].rssi_close);
	cmd.band_config[IWL_NAN_BAND_2GHZ].rssi_middle =
		abs(conf->band_cfgs[NL80211_BAND_2GHZ].rssi_middle);
	cmd.band_config[IWL_NAN_BAND_2GHZ].dw_interval =
		conf->band_cfgs[NL80211_BAND_2GHZ].awake_dw_interval;

	/* 5 GHz band operation is optional. Configure its operation if
	 * supported. Note that conf->bands might be zero, so we need to check
	 * the channel pointer, not the band mask.
	 */
	if (conf->band_cfgs[NL80211_BAND_5GHZ].chan) {
		cmd.hb_channel =
			conf->band_cfgs[NL80211_BAND_5GHZ].chan->hw_value;

		cmd.band_config[IWL_NAN_BAND_5GHZ].rssi_close =
			abs(conf->band_cfgs[NL80211_BAND_5GHZ].rssi_close);
		cmd.band_config[IWL_NAN_BAND_5GHZ].rssi_middle =
			abs(conf->band_cfgs[NL80211_BAND_5GHZ].rssi_middle);
		cmd.band_config[IWL_NAN_BAND_5GHZ].dw_interval =
			conf->band_cfgs[NL80211_BAND_5GHZ].awake_dw_interval;
	}

	if (conf->extra_nan_attrs_len || conf->vendor_elems_len) {
		data = kmalloc(conf->extra_nan_attrs_len +
			       conf->vendor_elems_len, GFP_KERNEL);
		if (!data)
			return -ENOMEM;

		cmd.nan_attr_len = cpu_to_le32(conf->extra_nan_attrs_len);
		cmd.nan_vendor_elems_len = cpu_to_le32(conf->vendor_elems_len);

		if (conf->extra_nan_attrs_len)
			memcpy(data, conf->extra_nan_attrs,
			       conf->extra_nan_attrs_len);

		if (conf->vendor_elems_len)
			memcpy(data + conf->extra_nan_attrs_len,
			       conf->vendor_elems,
			       conf->vendor_elems_len);
	}

	cmd.sta_id = mld_vif->aux_sta.sta_id;
	return iwl_mld_nan_send_config_cmd(mld, &cmd, data,
					   conf->extra_nan_attrs_len +
					   conf->vendor_elems_len);
}

int iwl_mld_start_nan(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct cfg80211_nan_conf *conf)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_int_sta *aux_sta = &mld_vif->aux_sta;
	int ret;

	IWL_DEBUG_MAC80211(mld, "NAN: start: bands=0x%x\n", conf->bands);

	ret = iwl_mld_update_emlsr_block(mld, true, IWL_MLD_EMLSR_BLOCKED_NAN);
	if (ret)
		return ret;

	ret = iwl_mld_add_aux_sta(mld, aux_sta);
	if (ret)
		goto unblock_emlsr;

	ret = iwl_mld_nan_config(mld, vif, conf, FW_CTXT_ACTION_ADD);
	if (ret) {
		IWL_ERR(mld, "Failed to start NAN. ret=%d\n", ret);
		goto remove_aux;
	}
	return 0;

remove_aux:
	iwl_mld_remove_aux_sta(mld, vif);
unblock_emlsr:
	iwl_mld_update_emlsr_block(mld, false, IWL_MLD_EMLSR_BLOCKED_NAN);

	return ret;
}

int iwl_mld_nan_change_config(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct cfg80211_nan_conf *conf,
			      u32 changes)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);

	IWL_DEBUG_MAC80211(mld, "NAN: change: changes=0x%x, bands=0x%x\n",
			   changes, conf->bands);

	/* Note that we do not use 'changes' as the FW always expects the
	 * complete configuration, and mac80211 always provides the complete
	 * configuration.
	 */
	return iwl_mld_nan_config(mld, vif, conf, FW_CTXT_ACTION_MODIFY);
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

	/* cancel based on object type being NAN, as the NAN objects do
	 * not have a unique identifier associated with them
	 */
	iwl_mld_cancel_notifications_of_object(mld,
					       IWL_MLD_OBJECT_TYPE_NAN,
					       0);

	iwl_mld_update_emlsr_block(mld, false, IWL_MLD_EMLSR_BLOCKED_NAN);

	return 0;
}

void iwl_mld_handle_nan_cluster_notif(struct iwl_mld *mld,
				      struct iwl_rx_packet *pkt)
{
	struct iwl_nan_cluster_notif *notif = (void *)pkt->data;
	struct wireless_dev *wdev = mld->nan_device_vif ?
		ieee80211_vif_to_wdev(mld->nan_device_vif) : NULL;
	bool new_cluster = !!(notif->flags &
			      IWL_NAN_CLUSTER_NOTIF_FLAG_NEW_CLUSTER);
	u8 cluster_id[ETH_ALEN] = {
		0x50, 0x6f, 0x9a, 0x01,
		notif->cluster_id[0], notif->cluster_id[1]
	};

	IWL_DEBUG_INFO(mld,
		       "NAN: cluster event: cluster_id=%pM, flags=0x%x\n",
		       cluster_id, notif->flags);

	if (IWL_FW_CHECK(mld, !wdev, "NAN: cluster event without wdev\n"))
		return;

	if (IWL_FW_CHECK(mld, !ieee80211_vif_nan_started(mld->nan_device_vif),
			 "NAN: cluster event without NAN started\n"))
		return;

	cfg80211_nan_cluster_joined(wdev, cluster_id, new_cluster, GFP_KERNEL);
}

bool iwl_mld_cancel_nan_cluster_notif(struct iwl_mld *mld,
				      struct iwl_rx_packet *pkt,
				      u32 obj_id)
{
	return true;
}

bool iwl_mld_cancel_nan_dw_end_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt,
				     u32 obj_id)
{
	return true;
}

void iwl_mld_handle_nan_dw_end_notif(struct iwl_mld *mld,
				     struct iwl_rx_packet *pkt)
{
	struct iwl_nan_dw_end_notif *notif = (void *)pkt->data;
	struct iwl_mld_vif *mld_vif = mld->nan_device_vif ?
		iwl_mld_vif_from_mac80211(mld->nan_device_vif) :
		NULL;
	struct wireless_dev *wdev;
	struct ieee80211_channel *chan;

	IWL_INFO(mld, "NAN: DW end: band=%u\n", notif->band);

	if (IWL_FW_CHECK(mld, !mld_vif, "NAN: DW end without mld_vif\n"))
		return;

	if (IWL_FW_CHECK(mld, !ieee80211_vif_nan_started(mld->nan_device_vif),
			 "NAN: DW end without NAN started\n"))
		return;

	if (WARN_ON(mld_vif->aux_sta.sta_id == IWL_INVALID_STA))
		return;

	IWL_DEBUG_INFO(mld, "NAN: flush queues for aux sta=%u\n",
		       mld_vif->aux_sta.sta_id);

	iwl_mld_flush_link_sta_txqs(mld, mld_vif->aux_sta.sta_id);

	/* TODO: currently the notification specified the band on which the DW
	 * ended. Need to change that to the actual channel on which the next DW
	 * will be started.
	 */
	switch (notif->band) {
	case IWL_NAN_BAND_2GHZ:
		chan = ieee80211_get_channel(mld->wiphy, 2437);
		break;
	case IWL_NAN_BAND_5GHZ:
		/* TODO: use the actual channel */
		chan = ieee80211_get_channel(mld->wiphy, 5745);
		break;
	default:
		IWL_FW_CHECK(mld, false,
			     "NAN: Invalid band %u in DW end notif\n",
			     notif->band);
		return;
	}

	wdev = ieee80211_vif_to_wdev(mld->nan_device_vif);
	cfg80211_next_nan_dw_notif(wdev, chan, GFP_KERNEL);
}
