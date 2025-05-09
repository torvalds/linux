// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024 - 2025 Intel Corporation
 */
#include <net/cfg80211.h>
#include <net/mac80211.h>

#include "mld.h"
#include "roc.h"
#include "hcmd.h"
#include "iface.h"
#include "sta.h"
#include "mlo.h"

#include "fw/api/context.h"
#include "fw/api/time-event.h"

#define AUX_ROC_MAX_DELAY MSEC_TO_TU(200)

static void
iwl_mld_vif_iter_emlsr_block_roc(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	int *result = data;
	int ret;

	ret = iwl_mld_block_emlsr_sync(mld_vif->mld, vif,
				       IWL_MLD_EMLSR_BLOCKED_ROC,
				       iwl_mld_get_primary_link(vif));
	if (ret)
		*result = ret;
}

int iwl_mld_start_roc(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct ieee80211_channel *channel, int duration,
		      enum ieee80211_roc_type type)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_mld_int_sta *aux_sta = &mld_vif->aux_sta;
	struct iwl_roc_req cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_ADD),
	};
	u8 ver = iwl_fw_lookup_cmd_ver(mld->fw,
				       WIDE_ID(MAC_CONF_GROUP, ROC_CMD), 0);
	u16 cmd_len = ver < 6 ? sizeof(struct iwl_roc_req_v5) : sizeof(cmd);
	enum iwl_roc_activity activity;
	int ret = 0;

	lockdep_assert_wiphy(mld->wiphy);

	if (vif->type != NL80211_IFTYPE_P2P_DEVICE &&
	    vif->type != NL80211_IFTYPE_STATION) {
		IWL_ERR(mld, "NOT SUPPORTED: ROC on vif->type %d\n",
			vif->type);

		return -EOPNOTSUPP;
	}

	if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
		switch (type) {
		case IEEE80211_ROC_TYPE_NORMAL:
			activity = ROC_ACTIVITY_P2P_DISC;
			break;
		case IEEE80211_ROC_TYPE_MGMT_TX:
			activity = ROC_ACTIVITY_P2P_NEG;
			break;
		default:
			WARN_ONCE(1, "Got an invalid P2P ROC type\n");
			return -EINVAL;
		}
	} else {
		activity = ROC_ACTIVITY_HOTSPOT;
	}

	if (WARN_ON(mld_vif->roc_activity != ROC_NUM_ACTIVITIES))
		return -EBUSY;

	if (vif->type == NL80211_IFTYPE_STATION && mld->bss_roc_vif)
		return -EBUSY;

	ieee80211_iterate_active_interfaces_mtx(mld->hw,
						IEEE80211_IFACE_ITER_NORMAL,
						iwl_mld_vif_iter_emlsr_block_roc,
						&ret);
	if (ret)
		return ret;

	ret = iwl_mld_add_aux_sta(mld, aux_sta);
	if (ret)
		return ret;

	cmd.activity = cpu_to_le32(activity);
	cmd.sta_id = cpu_to_le32(aux_sta->sta_id);
	cmd.channel_info.channel = cpu_to_le32(channel->hw_value);
	cmd.channel_info.band = iwl_mld_nl80211_band_to_fw(channel->band);
	cmd.channel_info.width = IWL_PHY_CHANNEL_MODE20;
	cmd.max_delay = cpu_to_le32(AUX_ROC_MAX_DELAY);
	cmd.duration = cpu_to_le32(MSEC_TO_TU(duration));

	memcpy(cmd.node_addr, vif->addr, ETH_ALEN);

	ret = iwl_mld_send_cmd_pdu(mld, WIDE_ID(MAC_CONF_GROUP, ROC_CMD),
				   &cmd, cmd_len);
	if (ret) {
		IWL_ERR(mld, "Couldn't send the ROC_CMD\n");
		return ret;
	}

	mld_vif->roc_activity = activity;

	if (vif->type == NL80211_IFTYPE_STATION)
		mld->bss_roc_vif = vif;

	return 0;
}

static void
iwl_mld_vif_iter_emlsr_unblock_roc(void *data, u8 *mac,
				   struct ieee80211_vif *vif)
{
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);

	iwl_mld_unblock_emlsr(mld_vif->mld, vif, IWL_MLD_EMLSR_BLOCKED_ROC);
}

static void iwl_mld_destroy_roc(struct iwl_mld *mld,
				struct ieee80211_vif *vif,
				struct iwl_mld_vif *mld_vif)
{
	mld_vif->roc_activity = ROC_NUM_ACTIVITIES;

	if (vif->type == NL80211_IFTYPE_STATION)
		mld->bss_roc_vif = NULL;

	ieee80211_iterate_active_interfaces_mtx(mld->hw,
						IEEE80211_IFACE_ITER_NORMAL,
						iwl_mld_vif_iter_emlsr_unblock_roc,
						NULL);

	/* wait until every tx has seen that roc_activity has been reset */
	synchronize_net();
	/* from here, no new tx will be added
	 * we can flush the Tx on the queues
	 */

	iwl_mld_flush_link_sta_txqs(mld, mld_vif->aux_sta.sta_id);

	iwl_mld_remove_aux_sta(mld, vif);
}

int iwl_mld_cancel_roc(struct ieee80211_hw *hw,
		       struct ieee80211_vif *vif)
{
	struct iwl_mld *mld = IWL_MAC80211_GET_MLD(hw);
	struct iwl_mld_vif *mld_vif = iwl_mld_vif_from_mac80211(vif);
	struct iwl_roc_req cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE),
	};
	u8 ver = iwl_fw_lookup_cmd_ver(mld->fw,
				       WIDE_ID(MAC_CONF_GROUP, ROC_CMD), 0);
	u16 cmd_len = ver < 6 ? sizeof(struct iwl_roc_req_v5) : sizeof(cmd);
	int ret;

	lockdep_assert_wiphy(mld->wiphy);

	if (WARN_ON(vif->type != NL80211_IFTYPE_P2P_DEVICE &&
		    vif->type != NL80211_IFTYPE_STATION))
		return -EOPNOTSUPP;

	/* No roc activity running it's probably already done */
	if (mld_vif->roc_activity == ROC_NUM_ACTIVITIES)
		return 0;

	cmd.activity = cpu_to_le32(mld_vif->roc_activity);

	ret = iwl_mld_send_cmd_pdu(mld, WIDE_ID(MAC_CONF_GROUP, ROC_CMD),
				   &cmd, cmd_len);
	if (ret)
		IWL_ERR(mld, "Couldn't send the command to cancel the ROC\n");

	/* We may have raced with the firmware expiring the ROC instance at
	 * this very moment. In that case, we can have a notification in the
	 * async processing queue. However, none can arrive _after_ this as
	 * ROC_CMD was sent synchronously, i.e. we waited for a response and
	 * the firmware cannot refer to this ROC after the response. Thus,
	 * if we just cancel the notification (if there's one) we'll be at a
	 * clean state for any possible next ROC.
	 */
	iwl_mld_cancel_notifications_of_object(mld, IWL_MLD_OBJECT_TYPE_ROC,
					       mld_vif->roc_activity);

	iwl_mld_destroy_roc(mld, vif, mld_vif);

	return 0;
}

void iwl_mld_handle_roc_notif(struct iwl_mld *mld,
			      struct iwl_rx_packet *pkt)
{
	const struct iwl_roc_notif *notif = (void *)pkt->data;
	u32 activity = le32_to_cpu(notif->activity);
	struct iwl_mld_vif *mld_vif;
	struct ieee80211_vif *vif;

	if (activity == ROC_ACTIVITY_HOTSPOT)
		vif = mld->bss_roc_vif;
	else
		vif = mld->p2p_device_vif;

	if (WARN_ON(!vif))
		return;

	mld_vif = iwl_mld_vif_from_mac80211(vif);
	/* It is possible that the ROC was canceled
	 * but the notification was already fired.
	 */
	if (mld_vif->roc_activity != activity)
		return;

	if (le32_to_cpu(notif->success) &&
	    le32_to_cpu(notif->started)) {
		/* We had a successful start */
		ieee80211_ready_on_channel(mld->hw);
	} else {
		/* ROC was not successful, tell the firmware to remove it */
		if (le32_to_cpu(notif->started))
			iwl_mld_cancel_roc(mld->hw, vif);
		else
			iwl_mld_destroy_roc(mld, vif, mld_vif);
		/* we need to let know mac80211 about end OR
		 * an unsuccessful start
		 */
		ieee80211_remain_on_channel_expired(mld->hw);
	}
}
