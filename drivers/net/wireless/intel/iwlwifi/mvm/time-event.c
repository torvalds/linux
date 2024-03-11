// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2024 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2017 Intel Deutschland GmbH
 */
#include <linux/jiffies.h>
#include <net/mac80211.h>

#include "fw/notif-wait.h"
#include "iwl-trans.h"
#include "fw-api.h"
#include "time-event.h"
#include "mvm.h"
#include "iwl-io.h"
#include "iwl-prph.h"

/*
 * For the high priority TE use a time event type that has similar priority to
 * the FW's action scan priority.
 */
#define IWL_MVM_ROC_TE_TYPE_NORMAL TE_P2P_DEVICE_DISCOVERABLE
#define IWL_MVM_ROC_TE_TYPE_MGMT_TX TE_P2P_CLIENT_ASSOC

void iwl_mvm_te_clear_data(struct iwl_mvm *mvm,
			   struct iwl_mvm_time_event_data *te_data)
{
	lockdep_assert_held(&mvm->time_event_lock);

	if (!te_data || !te_data->vif)
		return;

	list_del(&te_data->list);

	/*
	 * the list is only used for AUX ROC events so make sure it is always
	 * initialized
	 */
	INIT_LIST_HEAD(&te_data->list);

	te_data->running = false;
	te_data->uid = 0;
	te_data->id = TE_MAX;
	te_data->vif = NULL;
	te_data->link_id = -1;
}

static void iwl_mvm_cleanup_roc(struct iwl_mvm *mvm)
{
	/*
	 * Clear the ROC_RUNNING status bit.
	 * This will cause the TX path to drop offchannel transmissions.
	 * That would also be done by mac80211, but it is racy, in particular
	 * in the case that the time event actually completed in the firmware.
	 *
	 * Also flush the offchannel queue -- this is called when the time
	 * event finishes or is canceled, so that frames queued for it
	 * won't get stuck on the queue and be transmitted in the next
	 * time event.
	 */
	if (test_and_clear_bit(IWL_MVM_STATUS_ROC_RUNNING, &mvm->status)) {
		struct iwl_mvm_vif *mvmvif;

		synchronize_net();

		/*
		 * NB: access to this pointer would be racy, but the flush bit
		 * can only be set when we had a P2P-Device VIF, and we have a
		 * flush of this work in iwl_mvm_prepare_mac_removal() so it's
		 * not really racy.
		 */

		if (!WARN_ON(!mvm->p2p_device_vif)) {
			struct ieee80211_vif *vif = mvm->p2p_device_vif;

			mvmvif = iwl_mvm_vif_from_mac80211(vif);
			iwl_mvm_flush_sta(mvm, mvmvif->deflink.bcast_sta.sta_id,
					  mvmvif->deflink.bcast_sta.tfd_queue_msk);

			if (mvm->mld_api_is_used) {
				iwl_mvm_mld_rm_bcast_sta(mvm, vif,
							 &vif->bss_conf);

				iwl_mvm_link_changed(mvm, vif, &vif->bss_conf,
						     LINK_CONTEXT_MODIFY_ACTIVE,
						     false);
			} else {
				iwl_mvm_rm_p2p_bcast_sta(mvm, vif);
				iwl_mvm_binding_remove_vif(mvm, vif);
			}

			/* Do not remove the PHY context as removing and adding
			 * a PHY context has timing overheads. Leaving it
			 * configured in FW would be useful in case the next ROC
			 * is with the same channel.
			 */
		}
	}

	/* Do the same for AUX ROC */
	if (test_and_clear_bit(IWL_MVM_STATUS_ROC_AUX_RUNNING, &mvm->status)) {
		synchronize_net();

		iwl_mvm_flush_sta(mvm, mvm->aux_sta.sta_id,
				  mvm->aux_sta.tfd_queue_msk);

		if (mvm->mld_api_is_used) {
			iwl_mvm_mld_rm_aux_sta(mvm);
			return;
		}

		/* In newer version of this command an aux station is added only
		 * in cases of dedicated tx queue and need to be removed in end
		 * of use */
		if (iwl_mvm_has_new_station_api(mvm->fw))
			iwl_mvm_rm_aux_sta(mvm);
	}
}

void iwl_mvm_roc_done_wk(struct work_struct *wk)
{
	struct iwl_mvm *mvm = container_of(wk, struct iwl_mvm, roc_done_wk);

	mutex_lock(&mvm->mutex);
	iwl_mvm_cleanup_roc(mvm);
	mutex_unlock(&mvm->mutex);
}

static void iwl_mvm_roc_finished(struct iwl_mvm *mvm)
{
	/*
	 * Of course, our status bit is just as racy as mac80211, so in
	 * addition, fire off the work struct which will drop all frames
	 * from the hardware queues that made it through the race. First
	 * it will of course synchronize the TX path to make sure that
	 * any *new* TX will be rejected.
	 */
	schedule_work(&mvm->roc_done_wk);
}

static void iwl_mvm_csa_noa_start(struct iwl_mvm *mvm)
{
	struct ieee80211_vif *csa_vif;

	rcu_read_lock();

	csa_vif = rcu_dereference(mvm->csa_vif);
	if (!csa_vif || !csa_vif->bss_conf.csa_active)
		goto out_unlock;

	IWL_DEBUG_TE(mvm, "CSA NOA started\n");

	/*
	 * CSA NoA is started but we still have beacons to
	 * transmit on the current channel.
	 * So we just do nothing here and the switch
	 * will be performed on the last TBTT.
	 */
	if (!ieee80211_beacon_cntdwn_is_complete(csa_vif, 0)) {
		IWL_WARN(mvm, "CSA NOA started too early\n");
		goto out_unlock;
	}

	ieee80211_csa_finish(csa_vif, 0);

	rcu_read_unlock();

	RCU_INIT_POINTER(mvm->csa_vif, NULL);

	return;

out_unlock:
	rcu_read_unlock();
}

static bool iwl_mvm_te_check_disconnect(struct iwl_mvm *mvm,
					struct ieee80211_vif *vif,
					const char *errmsg)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (vif->type != NL80211_IFTYPE_STATION)
		return false;

	if (!mvmvif->csa_bcn_pending && vif->cfg.assoc &&
	    vif->bss_conf.dtim_period)
		return false;
	if (errmsg)
		IWL_ERR(mvm, "%s\n", errmsg);

	if (mvmvif->csa_bcn_pending) {
		struct iwl_mvm_sta *mvmsta;

		rcu_read_lock();
		mvmsta = iwl_mvm_sta_from_staid_rcu(mvm,
						    mvmvif->deflink.ap_sta_id);
		if (!WARN_ON(!mvmsta))
			iwl_mvm_sta_modify_disable_tx(mvm, mvmsta, false);
		rcu_read_unlock();
	}

	if (vif->cfg.assoc) {
		/*
		 * When not associated, this will be called from
		 * iwl_mvm_event_mlme_callback_ini()
		 */
		iwl_dbg_tlv_time_point(&mvm->fwrt,
				       IWL_FW_INI_TIME_POINT_ASSOC_FAILED,
				       NULL);
	}

	iwl_mvm_connection_loss(mvm, vif, errmsg);
	return true;
}

static void
iwl_mvm_te_handle_notify_csa(struct iwl_mvm *mvm,
			     struct iwl_mvm_time_event_data *te_data,
			     struct iwl_time_event_notif *notif)
{
	struct ieee80211_vif *vif = te_data->vif;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (!notif->status)
		IWL_DEBUG_TE(mvm, "CSA time event failed to start\n");

	switch (te_data->vif->type) {
	case NL80211_IFTYPE_AP:
		if (!notif->status)
			mvmvif->csa_failed = true;
		iwl_mvm_csa_noa_start(mvm);
		break;
	case NL80211_IFTYPE_STATION:
		if (!notif->status) {
			iwl_mvm_connection_loss(mvm, vif,
						"CSA TE failed to start");
			break;
		}
		iwl_mvm_csa_client_absent(mvm, te_data->vif);
		cancel_delayed_work(&mvmvif->csa_work);
		ieee80211_chswitch_done(te_data->vif, true, 0);
		break;
	default:
		/* should never happen */
		WARN_ON_ONCE(1);
		break;
	}

	/* we don't need it anymore */
	iwl_mvm_te_clear_data(mvm, te_data);
}

static void iwl_mvm_te_check_trigger(struct iwl_mvm *mvm,
				     struct iwl_time_event_notif *notif,
				     struct iwl_mvm_time_event_data *te_data)
{
	struct iwl_fw_dbg_trigger_tlv *trig;
	struct iwl_fw_dbg_trigger_time_event *te_trig;
	int i;

	trig = iwl_fw_dbg_trigger_on(&mvm->fwrt,
				     ieee80211_vif_to_wdev(te_data->vif),
				     FW_DBG_TRIGGER_TIME_EVENT);
	if (!trig)
		return;

	te_trig = (void *)trig->data;

	for (i = 0; i < ARRAY_SIZE(te_trig->time_events); i++) {
		u32 trig_te_id = le32_to_cpu(te_trig->time_events[i].id);
		u32 trig_action_bitmap =
			le32_to_cpu(te_trig->time_events[i].action_bitmap);
		u32 trig_status_bitmap =
			le32_to_cpu(te_trig->time_events[i].status_bitmap);

		if (trig_te_id != te_data->id ||
		    !(trig_action_bitmap & le32_to_cpu(notif->action)) ||
		    !(trig_status_bitmap & BIT(le32_to_cpu(notif->status))))
			continue;

		iwl_fw_dbg_collect_trig(&mvm->fwrt, trig,
					"Time event %d Action 0x%x received status: %d",
					te_data->id,
					le32_to_cpu(notif->action),
					le32_to_cpu(notif->status));
		break;
	}
}

/*
 * Handles a FW notification for an event that is known to the driver.
 *
 * @mvm: the mvm component
 * @te_data: the time event data
 * @notif: the notification data corresponding the time event data.
 */
static void iwl_mvm_te_handle_notif(struct iwl_mvm *mvm,
				    struct iwl_mvm_time_event_data *te_data,
				    struct iwl_time_event_notif *notif)
{
	lockdep_assert_held(&mvm->time_event_lock);

	IWL_DEBUG_TE(mvm, "Handle time event notif - UID = 0x%x action %d\n",
		     le32_to_cpu(notif->unique_id),
		     le32_to_cpu(notif->action));

	iwl_mvm_te_check_trigger(mvm, notif, te_data);

	/*
	 * The FW sends the start/end time event notifications even for events
	 * that it fails to schedule. This is indicated in the status field of
	 * the notification. This happens in cases that the scheduler cannot
	 * find a schedule that can handle the event (for example requesting a
	 * P2P Device discoveribility, while there are other higher priority
	 * events in the system).
	 */
	if (!le32_to_cpu(notif->status)) {
		const char *msg;

		if (notif->action & cpu_to_le32(TE_V2_NOTIF_HOST_EVENT_START))
			msg = "Time Event start notification failure";
		else
			msg = "Time Event end notification failure";

		IWL_DEBUG_TE(mvm, "%s\n", msg);

		if (iwl_mvm_te_check_disconnect(mvm, te_data->vif, msg)) {
			iwl_mvm_te_clear_data(mvm, te_data);
			return;
		}
	}

	if (le32_to_cpu(notif->action) & TE_V2_NOTIF_HOST_EVENT_END) {
		IWL_DEBUG_TE(mvm,
			     "TE ended - current time %lu, estimated end %lu\n",
			     jiffies, te_data->end_jiffies);

		switch (te_data->vif->type) {
		case NL80211_IFTYPE_P2P_DEVICE:
			ieee80211_remain_on_channel_expired(mvm->hw);
			iwl_mvm_roc_finished(mvm);
			break;
		case NL80211_IFTYPE_STATION:
			/*
			 * If we are switching channel, don't disconnect
			 * if the time event is already done. Beacons can
			 * be delayed a bit after the switch.
			 */
			if (te_data->id == TE_CHANNEL_SWITCH_PERIOD) {
				IWL_DEBUG_TE(mvm,
					     "No beacon heard and the CS time event is over, don't disconnect\n");
				break;
			}

			/*
			 * By now, we should have finished association
			 * and know the dtim period.
			 */
			iwl_mvm_te_check_disconnect(mvm, te_data->vif,
				!te_data->vif->cfg.assoc ?
				"Not associated and the time event is over already..." :
				"No beacon heard and the time event is over already...");
			break;
		default:
			break;
		}

		iwl_mvm_te_clear_data(mvm, te_data);
	} else if (le32_to_cpu(notif->action) & TE_V2_NOTIF_HOST_EVENT_START) {
		te_data->running = true;
		te_data->end_jiffies = TU_TO_EXP_TIME(te_data->duration);

		if (te_data->vif->type == NL80211_IFTYPE_P2P_DEVICE) {
			set_bit(IWL_MVM_STATUS_ROC_RUNNING, &mvm->status);
			ieee80211_ready_on_channel(mvm->hw);
		} else if (te_data->id == TE_CHANNEL_SWITCH_PERIOD) {
			iwl_mvm_te_handle_notify_csa(mvm, te_data, notif);
		}
	} else {
		IWL_WARN(mvm, "Got TE with unknown action\n");
	}
}

void iwl_mvm_rx_roc_notif(struct iwl_mvm *mvm,
			  struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_roc_notif *notif = (void *)pkt->data;

	if (le32_to_cpu(notif->success) && le32_to_cpu(notif->started) &&
	    le32_to_cpu(notif->activity) == ROC_ACTIVITY_HOTSPOT) {
		set_bit(IWL_MVM_STATUS_ROC_AUX_RUNNING, &mvm->status);
		ieee80211_ready_on_channel(mvm->hw);
	} else {
		iwl_mvm_roc_finished(mvm);
		ieee80211_remain_on_channel_expired(mvm->hw);
	}
}

/*
 * Handle A Aux ROC time event
 */
static int iwl_mvm_aux_roc_te_handle_notif(struct iwl_mvm *mvm,
					   struct iwl_time_event_notif *notif)
{
	struct iwl_mvm_time_event_data *aux_roc_te = NULL, *te_data;

	list_for_each_entry(te_data, &mvm->aux_roc_te_list, list) {
		if (le32_to_cpu(notif->unique_id) == te_data->uid) {
			aux_roc_te = te_data;
			break;
		}
	}
	if (!aux_roc_te) /* Not a Aux ROC time event */
		return -EINVAL;

	iwl_mvm_te_check_trigger(mvm, notif, te_data);

	IWL_DEBUG_TE(mvm,
		     "Aux ROC time event notification  - UID = 0x%x action %d (error = %d)\n",
		     le32_to_cpu(notif->unique_id),
		     le32_to_cpu(notif->action), le32_to_cpu(notif->status));

	if (!le32_to_cpu(notif->status) ||
	    le32_to_cpu(notif->action) == TE_V2_NOTIF_HOST_EVENT_END) {
		/* End TE, notify mac80211 */
		ieee80211_remain_on_channel_expired(mvm->hw);
		iwl_mvm_roc_finished(mvm); /* flush aux queue */
		list_del(&te_data->list); /* remove from list */
		te_data->running = false;
		te_data->vif = NULL;
		te_data->uid = 0;
		te_data->id = TE_MAX;
	} else if (le32_to_cpu(notif->action) == TE_V2_NOTIF_HOST_EVENT_START) {
		set_bit(IWL_MVM_STATUS_ROC_AUX_RUNNING, &mvm->status);
		te_data->running = true;
		ieee80211_ready_on_channel(mvm->hw); /* Start TE */
	} else {
		IWL_DEBUG_TE(mvm,
			     "ERROR: Unknown Aux ROC Time Event (action = %d)\n",
			     le32_to_cpu(notif->action));
		return -EINVAL;
	}

	return 0;
}

/*
 * The Rx handler for time event notifications
 */
void iwl_mvm_rx_time_event_notif(struct iwl_mvm *mvm,
				 struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_time_event_notif *notif = (void *)pkt->data;
	struct iwl_mvm_time_event_data *te_data, *tmp;

	IWL_DEBUG_TE(mvm, "Time event notification - UID = 0x%x action %d\n",
		     le32_to_cpu(notif->unique_id),
		     le32_to_cpu(notif->action));

	spin_lock_bh(&mvm->time_event_lock);
	/* This time event is triggered for Aux ROC request */
	if (!iwl_mvm_aux_roc_te_handle_notif(mvm, notif))
		goto unlock;

	list_for_each_entry_safe(te_data, tmp, &mvm->time_event_list, list) {
		if (le32_to_cpu(notif->unique_id) == te_data->uid)
			iwl_mvm_te_handle_notif(mvm, te_data, notif);
	}
unlock:
	spin_unlock_bh(&mvm->time_event_lock);
}

static bool iwl_mvm_te_notif(struct iwl_notif_wait_data *notif_wait,
			     struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_mvm *mvm =
		container_of(notif_wait, struct iwl_mvm, notif_wait);
	struct iwl_mvm_time_event_data *te_data = data;
	struct iwl_time_event_notif *resp;
	int resp_len = iwl_rx_packet_payload_len(pkt);

	if (WARN_ON(pkt->hdr.cmd != TIME_EVENT_NOTIFICATION))
		return true;

	if (WARN_ON_ONCE(resp_len != sizeof(*resp))) {
		IWL_ERR(mvm, "Invalid TIME_EVENT_NOTIFICATION response\n");
		return true;
	}

	resp = (void *)pkt->data;

	/* te_data->uid is already set in the TIME_EVENT_CMD response */
	if (le32_to_cpu(resp->unique_id) != te_data->uid)
		return false;

	IWL_DEBUG_TE(mvm, "TIME_EVENT_NOTIFICATION response - UID = 0x%x\n",
		     te_data->uid);
	if (!resp->status)
		IWL_ERR(mvm,
			"TIME_EVENT_NOTIFICATION received but not executed\n");

	return true;
}

static bool iwl_mvm_time_event_response(struct iwl_notif_wait_data *notif_wait,
					struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_mvm *mvm =
		container_of(notif_wait, struct iwl_mvm, notif_wait);
	struct iwl_mvm_time_event_data *te_data = data;
	struct iwl_time_event_resp *resp;
	int resp_len = iwl_rx_packet_payload_len(pkt);

	if (WARN_ON(pkt->hdr.cmd != TIME_EVENT_CMD))
		return true;

	if (WARN_ON_ONCE(resp_len != sizeof(*resp))) {
		IWL_ERR(mvm, "Invalid TIME_EVENT_CMD response\n");
		return true;
	}

	resp = (void *)pkt->data;

	/* we should never get a response to another TIME_EVENT_CMD here */
	if (WARN_ON_ONCE(le32_to_cpu(resp->id) != te_data->id))
		return false;

	te_data->uid = le32_to_cpu(resp->unique_id);
	IWL_DEBUG_TE(mvm, "TIME_EVENT_CMD response - UID = 0x%x\n",
		     te_data->uid);
	return true;
}

static int iwl_mvm_time_event_send_add(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif,
				       struct iwl_mvm_time_event_data *te_data,
				       struct iwl_time_event_cmd *te_cmd)
{
	static const u16 time_event_response[] = { TIME_EVENT_CMD };
	struct iwl_notification_wait wait_time_event;
	int ret;

	lockdep_assert_held(&mvm->mutex);

	IWL_DEBUG_TE(mvm, "Add new TE, duration %d TU\n",
		     le32_to_cpu(te_cmd->duration));

	spin_lock_bh(&mvm->time_event_lock);
	if (WARN_ON(te_data->id != TE_MAX)) {
		spin_unlock_bh(&mvm->time_event_lock);
		return -EIO;
	}
	te_data->vif = vif;
	te_data->duration = le32_to_cpu(te_cmd->duration);
	te_data->id = le32_to_cpu(te_cmd->id);
	list_add_tail(&te_data->list, &mvm->time_event_list);
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
				   iwl_mvm_time_event_response, te_data);

	ret = iwl_mvm_send_cmd_pdu(mvm, TIME_EVENT_CMD, 0,
					    sizeof(*te_cmd), te_cmd);
	if (ret) {
		IWL_ERR(mvm, "Couldn't send TIME_EVENT_CMD: %d\n", ret);
		iwl_remove_notification(&mvm->notif_wait, &wait_time_event);
		goto out_clear_te;
	}

	/* No need to wait for anything, so just pass 1 (0 isn't valid) */
	ret = iwl_wait_notification(&mvm->notif_wait, &wait_time_event, 1);
	/* should never fail */
	WARN_ON_ONCE(ret);

	if (ret) {
 out_clear_te:
		spin_lock_bh(&mvm->time_event_lock);
		iwl_mvm_te_clear_data(mvm, te_data);
		spin_unlock_bh(&mvm->time_event_lock);
	}
	return ret;
}

void iwl_mvm_protect_session(struct iwl_mvm *mvm,
			     struct ieee80211_vif *vif,
			     u32 duration, u32 min_duration,
			     u32 max_delay, bool wait_for_notif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_time_event_data *te_data = &mvmvif->time_event_data;
	const u16 te_notif_response[] = { TIME_EVENT_NOTIFICATION };
	struct iwl_notification_wait wait_te_notif;
	struct iwl_time_event_cmd time_cmd = {};

	lockdep_assert_held(&mvm->mutex);

	if (te_data->running &&
	    time_after(te_data->end_jiffies, TU_TO_EXP_TIME(min_duration))) {
		IWL_DEBUG_TE(mvm, "We have enough time in the current TE: %u\n",
			     jiffies_to_msecs(te_data->end_jiffies - jiffies));
		return;
	}

	if (te_data->running) {
		IWL_DEBUG_TE(mvm, "extend 0x%x: only %u ms left\n",
			     te_data->uid,
			     jiffies_to_msecs(te_data->end_jiffies - jiffies));
		/*
		 * we don't have enough time
		 * cancel the current TE and issue a new one
		 * Of course it would be better to remove the old one only
		 * when the new one is added, but we don't care if we are off
		 * channel for a bit. All we need to do, is not to return
		 * before we actually begin to be on the channel.
		 */
		iwl_mvm_stop_session_protection(mvm, vif);
	}

	time_cmd.action = cpu_to_le32(FW_CTXT_ACTION_ADD);
	time_cmd.id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color));
	time_cmd.id = cpu_to_le32(TE_BSS_STA_AGGRESSIVE_ASSOC);

	time_cmd.apply_time = cpu_to_le32(0);

	time_cmd.max_frags = TE_V2_FRAG_NONE;
	time_cmd.max_delay = cpu_to_le32(max_delay);
	/* TODO: why do we need to interval = bi if it is not periodic? */
	time_cmd.interval = cpu_to_le32(1);
	time_cmd.duration = cpu_to_le32(duration);
	time_cmd.repeat = 1;
	time_cmd.policy = cpu_to_le16(TE_V2_NOTIF_HOST_EVENT_START |
				      TE_V2_NOTIF_HOST_EVENT_END |
				      TE_V2_START_IMMEDIATELY);

	if (!wait_for_notif) {
		iwl_mvm_time_event_send_add(mvm, vif, te_data, &time_cmd);
		return;
	}

	/*
	 * Create notification_wait for the TIME_EVENT_NOTIFICATION to use
	 * right after we send the time event
	 */
	iwl_init_notification_wait(&mvm->notif_wait, &wait_te_notif,
				   te_notif_response,
				   ARRAY_SIZE(te_notif_response),
				   iwl_mvm_te_notif, te_data);

	/* If TE was sent OK - wait for the notification that started */
	if (iwl_mvm_time_event_send_add(mvm, vif, te_data, &time_cmd)) {
		IWL_ERR(mvm, "Failed to add TE to protect session\n");
		iwl_remove_notification(&mvm->notif_wait, &wait_te_notif);
	} else if (iwl_wait_notification(&mvm->notif_wait, &wait_te_notif,
					 TU_TO_JIFFIES(max_delay))) {
		IWL_ERR(mvm, "Failed to protect session until TE\n");
	}
}

/* Determine whether mac or link id should be used, and validate the link id */
static int iwl_mvm_get_session_prot_id(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif,
				       s8 link_id)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ver = iwl_fw_lookup_cmd_ver(mvm->fw,
					WIDE_ID(MAC_CONF_GROUP,
						SESSION_PROTECTION_CMD), 1);

	if (ver < 2)
		return mvmvif->id;

	if (WARN(link_id < 0 || !mvmvif->link[link_id],
		 "Invalid link ID for session protection: %u\n", link_id))
		return -EINVAL;

	if (WARN(!mvmvif->link[link_id]->active,
		 "Session Protection on an inactive link: %u\n", link_id))
		return -EINVAL;

	return mvmvif->link[link_id]->fw_link_id;
}

static void iwl_mvm_cancel_session_protection(struct iwl_mvm *mvm,
					      struct ieee80211_vif *vif,
					      u32 id, s8 link_id)
{
	int mac_link_id = iwl_mvm_get_session_prot_id(mvm, vif, link_id);
	struct iwl_mvm_session_prot_cmd cmd = {
		.id_and_color = cpu_to_le32(mac_link_id),
		.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE),
		.conf_id = cpu_to_le32(id),
	};
	int ret;

	if (mac_link_id < 0)
		return;

	ret = iwl_mvm_send_cmd_pdu(mvm,
				   WIDE_ID(MAC_CONF_GROUP, SESSION_PROTECTION_CMD),
				   0, sizeof(cmd), &cmd);
	if (ret)
		IWL_ERR(mvm,
			"Couldn't send the SESSION_PROTECTION_CMD: %d\n", ret);
}

static bool __iwl_mvm_remove_time_event(struct iwl_mvm *mvm,
					struct iwl_mvm_time_event_data *te_data,
					u32 *uid)
{
	u32 id;
	struct ieee80211_vif *vif = te_data->vif;
	struct iwl_mvm_vif *mvmvif;
	enum nl80211_iftype iftype;
	s8 link_id;

	if (!vif)
		return false;

	mvmvif = iwl_mvm_vif_from_mac80211(te_data->vif);
	iftype = te_data->vif->type;

	/*
	 * It is possible that by the time we got to this point the time
	 * event was already removed.
	 */
	spin_lock_bh(&mvm->time_event_lock);

	/* Save time event uid before clearing its data */
	*uid = te_data->uid;
	id = te_data->id;
	link_id = te_data->link_id;

	/*
	 * The clear_data function handles time events that were already removed
	 */
	iwl_mvm_te_clear_data(mvm, te_data);
	spin_unlock_bh(&mvm->time_event_lock);

	/* When session protection is used, the te_data->id field
	 * is reused to save session protection's configuration.
	 * For AUX ROC, HOT_SPOT_CMD is used and the te_data->id field is set
	 * to HOT_SPOT_CMD.
	 */
	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_SESSION_PROT_CMD) &&
	    id != HOT_SPOT_CMD) {
		if (mvmvif && id < SESSION_PROTECT_CONF_MAX_ID) {
			/* Session protection is still ongoing. Cancel it */
			iwl_mvm_cancel_session_protection(mvm, vif, id,
							  link_id);
			if (iftype == NL80211_IFTYPE_P2P_DEVICE) {
				iwl_mvm_roc_finished(mvm);
			}
		}
		return false;
	} else {
		/* It is possible that by the time we try to remove it, the
		 * time event has already ended and removed. In such a case
		 * there is no need to send a removal command.
		 */
		if (id == TE_MAX) {
			IWL_DEBUG_TE(mvm, "TE 0x%x has already ended\n", *uid);
			return false;
		}
	}

	return true;
}

/*
 * Explicit request to remove a aux roc time event. The removal of a time
 * event needs to be synchronized with the flow of a time event's end
 * notification, which also removes the time event from the op mode
 * data structures.
 */
static void iwl_mvm_remove_aux_roc_te(struct iwl_mvm *mvm,
				      struct iwl_mvm_vif *mvmvif,
				      struct iwl_mvm_time_event_data *te_data)
{
	struct iwl_hs20_roc_req aux_cmd = {};
	u16 len = sizeof(aux_cmd) - iwl_mvm_chan_info_padding(mvm);

	u32 uid;
	int ret;

	if (!__iwl_mvm_remove_time_event(mvm, te_data, &uid))
		return;

	aux_cmd.event_unique_id = cpu_to_le32(uid);
	aux_cmd.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE);
	aux_cmd.id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color));
	IWL_DEBUG_TE(mvm, "Removing BSS AUX ROC TE 0x%x\n",
		     le32_to_cpu(aux_cmd.event_unique_id));
	ret = iwl_mvm_send_cmd_pdu(mvm, HOT_SPOT_CMD, 0,
				   len, &aux_cmd);

	if (WARN_ON(ret))
		return;
}

/*
 * Explicit request to remove a time event. The removal of a time event needs to
 * be synchronized with the flow of a time event's end notification, which also
 * removes the time event from the op mode data structures.
 */
void iwl_mvm_remove_time_event(struct iwl_mvm *mvm,
			       struct iwl_mvm_vif *mvmvif,
			       struct iwl_mvm_time_event_data *te_data)
{
	struct iwl_time_event_cmd time_cmd = {};
	u32 uid;
	int ret;

	if (!__iwl_mvm_remove_time_event(mvm, te_data, &uid))
		return;

	/* When we remove a TE, the UID is to be set in the id field */
	time_cmd.id = cpu_to_le32(uid);
	time_cmd.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE);
	time_cmd.id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color));

	IWL_DEBUG_TE(mvm, "Removing TE 0x%x\n", le32_to_cpu(time_cmd.id));
	ret = iwl_mvm_send_cmd_pdu(mvm, TIME_EVENT_CMD, 0,
				   sizeof(time_cmd), &time_cmd);
	if (ret)
		IWL_ERR(mvm, "Couldn't remove the time event\n");
}

void iwl_mvm_stop_session_protection(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_time_event_data *te_data = &mvmvif->time_event_data;
	u32 id;

	lockdep_assert_held(&mvm->mutex);

	spin_lock_bh(&mvm->time_event_lock);
	id = te_data->id;
	spin_unlock_bh(&mvm->time_event_lock);

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_SESSION_PROT_CMD)) {
		if (id != SESSION_PROTECT_CONF_ASSOC) {
			IWL_DEBUG_TE(mvm,
				     "don't remove session protection id=%u\n",
				     id);
			return;
		}
	} else if (id != TE_BSS_STA_AGGRESSIVE_ASSOC) {
		IWL_DEBUG_TE(mvm,
			     "don't remove TE with id=%u (not session protection)\n",
			     id);
		return;
	}

	iwl_mvm_remove_time_event(mvm, mvmvif, te_data);
}

void iwl_mvm_rx_session_protect_notif(struct iwl_mvm *mvm,
				      struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mvm_session_prot_notif *notif = (void *)pkt->data;
	unsigned int ver =
		iwl_fw_lookup_notif_ver(mvm->fw, MAC_CONF_GROUP,
					SESSION_PROTECTION_NOTIF, 2);
	int id = le32_to_cpu(notif->mac_link_id);
	struct ieee80211_vif *vif;
	struct iwl_mvm_vif *mvmvif;
	unsigned int notif_link_id;

	rcu_read_lock();

	if (ver <= 2) {
		vif = iwl_mvm_rcu_dereference_vif_id(mvm, id, true);
	} else {
		struct ieee80211_bss_conf *link_conf =
			iwl_mvm_rcu_fw_link_id_to_link_conf(mvm, id, true);

		if (!link_conf)
			goto out_unlock;

		notif_link_id = link_conf->link_id;
		vif = link_conf->vif;
	}

	if (!vif)
		goto out_unlock;

	mvmvif = iwl_mvm_vif_from_mac80211(vif);

	if (WARN(ver > 2 && mvmvif->time_event_data.link_id >= 0 &&
		 mvmvif->time_event_data.link_id != notif_link_id,
		 "SESSION_PROTECTION_NOTIF was received for link %u, while the current time event is on link %u\n",
		 notif_link_id, mvmvif->time_event_data.link_id))
		goto out_unlock;

	/* The vif is not a P2P_DEVICE, maintain its time_event_data */
	if (vif->type != NL80211_IFTYPE_P2P_DEVICE) {
		struct iwl_mvm_time_event_data *te_data =
			&mvmvif->time_event_data;

		if (!le32_to_cpu(notif->status)) {
			iwl_mvm_te_check_disconnect(mvm, vif,
						    "Session protection failure");
			spin_lock_bh(&mvm->time_event_lock);
			iwl_mvm_te_clear_data(mvm, te_data);
			spin_unlock_bh(&mvm->time_event_lock);
		}

		if (le32_to_cpu(notif->start)) {
			spin_lock_bh(&mvm->time_event_lock);
			te_data->running = le32_to_cpu(notif->start);
			te_data->end_jiffies =
				TU_TO_EXP_TIME(te_data->duration);
			spin_unlock_bh(&mvm->time_event_lock);
		} else {
			/*
			 * By now, we should have finished association
			 * and know the dtim period.
			 */
			iwl_mvm_te_check_disconnect(mvm, vif,
						    !vif->cfg.assoc ?
						    "Not associated and the session protection is over already..." :
						    "No beacon heard and the session protection is over already...");
			spin_lock_bh(&mvm->time_event_lock);
			iwl_mvm_te_clear_data(mvm, te_data);
			spin_unlock_bh(&mvm->time_event_lock);
		}

		goto out_unlock;
	}

	if (!le32_to_cpu(notif->status) || !le32_to_cpu(notif->start)) {
		/* End TE, notify mac80211 */
		mvmvif->time_event_data.id = SESSION_PROTECT_CONF_MAX_ID;
		mvmvif->time_event_data.link_id = -1;
		iwl_mvm_roc_finished(mvm);
		ieee80211_remain_on_channel_expired(mvm->hw);
	} else if (le32_to_cpu(notif->start)) {
		if (WARN_ON(mvmvif->time_event_data.id !=
				le32_to_cpu(notif->conf_id)))
			goto out_unlock;
		set_bit(IWL_MVM_STATUS_ROC_RUNNING, &mvm->status);
		ieee80211_ready_on_channel(mvm->hw); /* Start TE */
	}

 out_unlock:
	rcu_read_unlock();
}

#define AUX_ROC_MIN_DURATION MSEC_TO_TU(100)
#define AUX_ROC_MIN_DELAY MSEC_TO_TU(200)
#define AUX_ROC_MAX_DELAY MSEC_TO_TU(600)
#define AUX_ROC_SAFETY_BUFFER MSEC_TO_TU(20)
#define AUX_ROC_MIN_SAFETY_BUFFER MSEC_TO_TU(10)

void iwl_mvm_roc_duration_and_delay(struct ieee80211_vif *vif,
				    u32 duration_ms,
				    u32 *duration_tu,
				    u32 *delay)
{
	u32 dtim_interval = vif->bss_conf.dtim_period *
		vif->bss_conf.beacon_int;

	*delay = AUX_ROC_MIN_DELAY;
	*duration_tu = MSEC_TO_TU(duration_ms);

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
		*delay = min_t(u32, dtim_interval * 3, AUX_ROC_MAX_DELAY);
		/* We cannot remain off-channel longer than the DTIM interval */
		if (dtim_interval <= *duration_tu) {
			*duration_tu = dtim_interval - AUX_ROC_SAFETY_BUFFER;
			if (*duration_tu <= AUX_ROC_MIN_DURATION)
				*duration_tu = dtim_interval -
					AUX_ROC_MIN_SAFETY_BUFFER;
		}
	}
}

int iwl_mvm_roc_add_cmd(struct iwl_mvm *mvm,
			struct ieee80211_channel *channel,
			struct ieee80211_vif *vif,
			int duration, u32 activity)
{
	int res;
	u32 duration_tu, delay;
	struct iwl_roc_req roc_req = {
		.action = cpu_to_le32(FW_CTXT_ACTION_ADD),
		.activity = cpu_to_le32(activity),
		.sta_id = cpu_to_le32(mvm->aux_sta.sta_id),
	};

	lockdep_assert_held(&mvm->mutex);

	/* Set the channel info data */
	iwl_mvm_set_chan_info(mvm, &roc_req.channel_info,
			      channel->hw_value,
			      iwl_mvm_phy_band_from_nl80211(channel->band),
			      IWL_PHY_CHANNEL_MODE20, 0);

	iwl_mvm_roc_duration_and_delay(vif, duration, &duration_tu,
				       &delay);
	roc_req.duration = cpu_to_le32(duration_tu);
	roc_req.max_delay = cpu_to_le32(delay);

	IWL_DEBUG_TE(mvm,
		     "\t(requested = %ums, max_delay = %ums)\n",
		     duration, delay);
	IWL_DEBUG_TE(mvm,
		     "Requesting to remain on channel %u for %utu\n",
		     channel->hw_value, duration_tu);

	/* Set the node address */
	memcpy(roc_req.node_addr, vif->addr, ETH_ALEN);

	res = iwl_mvm_send_cmd_pdu(mvm, WIDE_ID(MAC_CONF_GROUP, ROC_CMD),
				   0, sizeof(roc_req), &roc_req);

	return res;
}

static int
iwl_mvm_start_p2p_roc_session_protection(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif,
					 int duration,
					 enum ieee80211_roc_type type)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_session_prot_cmd cmd = {
		.id_and_color =
			cpu_to_le32(iwl_mvm_get_session_prot_id(mvm, vif, 0)),
		.action = cpu_to_le32(FW_CTXT_ACTION_ADD),
		.duration_tu = cpu_to_le32(MSEC_TO_TU(duration)),
	};

	lockdep_assert_held(&mvm->mutex);

	/* The time_event_data.id field is reused to save session
	 * protection's configuration.
	 */

	mvmvif->time_event_data.link_id = 0;

	switch (type) {
	case IEEE80211_ROC_TYPE_NORMAL:
		mvmvif->time_event_data.id =
			SESSION_PROTECT_CONF_P2P_DEVICE_DISCOV;
		break;
	case IEEE80211_ROC_TYPE_MGMT_TX:
		mvmvif->time_event_data.id =
			SESSION_PROTECT_CONF_P2P_GO_NEGOTIATION;
		break;
	default:
		WARN_ONCE(1, "Got an invalid ROC type\n");
		return -EINVAL;
	}

	cmd.conf_id = cpu_to_le32(mvmvif->time_event_data.id);
	return iwl_mvm_send_cmd_pdu(mvm,
				    WIDE_ID(MAC_CONF_GROUP, SESSION_PROTECTION_CMD),
				    0, sizeof(cmd), &cmd);
}

int iwl_mvm_start_p2p_roc(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			  int duration, enum ieee80211_roc_type type)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_time_event_data *te_data = &mvmvif->time_event_data;
	struct iwl_time_event_cmd time_cmd = {};

	lockdep_assert_held(&mvm->mutex);
	if (te_data->running) {
		IWL_WARN(mvm, "P2P_DEVICE remain on channel already running\n");
		return -EBUSY;
	}

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_SESSION_PROT_CMD))
		return iwl_mvm_start_p2p_roc_session_protection(mvm, vif,
								duration,
								type);

	time_cmd.action = cpu_to_le32(FW_CTXT_ACTION_ADD);
	time_cmd.id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color));

	switch (type) {
	case IEEE80211_ROC_TYPE_NORMAL:
		time_cmd.id = cpu_to_le32(IWL_MVM_ROC_TE_TYPE_NORMAL);
		break;
	case IEEE80211_ROC_TYPE_MGMT_TX:
		time_cmd.id = cpu_to_le32(IWL_MVM_ROC_TE_TYPE_MGMT_TX);
		break;
	default:
		WARN_ONCE(1, "Got an invalid ROC type\n");
		return -EINVAL;
	}

	time_cmd.apply_time = cpu_to_le32(0);
	time_cmd.interval = cpu_to_le32(1);

	/*
	 * The P2P Device TEs can have lower priority than other events
	 * that are being scheduled by the driver/fw, and thus it might not be
	 * scheduled. To improve the chances of it being scheduled, allow them
	 * to be fragmented, and in addition allow them to be delayed.
	 */
	time_cmd.max_frags = min(MSEC_TO_TU(duration)/50, TE_V2_FRAG_ENDLESS);
	time_cmd.max_delay = cpu_to_le32(MSEC_TO_TU(duration/2));
	time_cmd.duration = cpu_to_le32(MSEC_TO_TU(duration));
	time_cmd.repeat = 1;
	time_cmd.policy = cpu_to_le16(TE_V2_NOTIF_HOST_EVENT_START |
				      TE_V2_NOTIF_HOST_EVENT_END |
				      TE_V2_START_IMMEDIATELY);

	return iwl_mvm_time_event_send_add(mvm, vif, te_data, &time_cmd);
}

static struct iwl_mvm_time_event_data *iwl_mvm_get_roc_te(struct iwl_mvm *mvm)
{
	struct iwl_mvm_time_event_data *te_data;

	lockdep_assert_held(&mvm->mutex);

	spin_lock_bh(&mvm->time_event_lock);

	/*
	 * Iterate over the list of time events and find the time event that is
	 * associated with a P2P_DEVICE interface.
	 * This assumes that a P2P_DEVICE interface can have only a single time
	 * event at any given time and this time event coresponds to a ROC
	 * request
	 */
	list_for_each_entry(te_data, &mvm->time_event_list, list) {
		if (te_data->vif->type == NL80211_IFTYPE_P2P_DEVICE)
			goto out;
	}

	/* There can only be at most one AUX ROC time event, we just use the
	 * list to simplify/unify code. Remove it if it exists.
	 */
	te_data = list_first_entry_or_null(&mvm->aux_roc_te_list,
					   struct iwl_mvm_time_event_data,
					   list);
out:
	spin_unlock_bh(&mvm->time_event_lock);
	return te_data;
}

void iwl_mvm_cleanup_roc_te(struct iwl_mvm *mvm)
{
	struct iwl_mvm_time_event_data *te_data;
	u32 uid;

	te_data = iwl_mvm_get_roc_te(mvm);
	if (te_data)
		__iwl_mvm_remove_time_event(mvm, te_data, &uid);
}

static void iwl_mvm_roc_rm_cmd(struct iwl_mvm *mvm, u32 activity)
{
	int ret;
	struct iwl_roc_req roc_cmd = {
		.action = cpu_to_le32(FW_CTXT_ACTION_REMOVE),
		.activity = cpu_to_le32(activity),
	};

	lockdep_assert_held(&mvm->mutex);
	ret = iwl_mvm_send_cmd_pdu(mvm,
				   WIDE_ID(MAC_CONF_GROUP, ROC_CMD),
				   0, sizeof(roc_cmd), &roc_cmd);
	WARN_ON(ret);
}

static void iwl_mvm_roc_station_remove(struct iwl_mvm *mvm,
				       struct iwl_mvm_vif *mvmvif)
{
	u32 cmd_id = WIDE_ID(MAC_CONF_GROUP, ROC_CMD);
	u8 fw_ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd_id,
					  IWL_FW_CMD_VER_UNKNOWN);

	if (fw_ver == IWL_FW_CMD_VER_UNKNOWN)
		iwl_mvm_remove_aux_roc_te(mvm, mvmvif,
					  &mvmvif->hs_time_event_data);
	else if (fw_ver == 3)
		iwl_mvm_roc_rm_cmd(mvm, ROC_ACTIVITY_HOTSPOT);
	else
		IWL_ERR(mvm, "ROC command version %d mismatch!\n", fw_ver);
}

void iwl_mvm_stop_roc(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif;
	struct iwl_mvm_time_event_data *te_data;

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_SESSION_PROT_CMD)) {
		mvmvif = iwl_mvm_vif_from_mac80211(vif);
		te_data = &mvmvif->time_event_data;

		if (vif->type == NL80211_IFTYPE_P2P_DEVICE) {
			if (te_data->id >= SESSION_PROTECT_CONF_MAX_ID) {
				IWL_DEBUG_TE(mvm,
					     "No remain on channel event\n");
				return;
			}

			iwl_mvm_cancel_session_protection(mvm, vif,
							  te_data->id,
							  te_data->link_id);
		} else {
			iwl_mvm_roc_station_remove(mvm, mvmvif);
		}
		goto cleanup_roc;
	}

	te_data = iwl_mvm_get_roc_te(mvm);
	if (!te_data) {
		IWL_WARN(mvm, "No remain on channel event\n");
		return;
	}

	mvmvif = iwl_mvm_vif_from_mac80211(te_data->vif);

	if (te_data->vif->type == NL80211_IFTYPE_P2P_DEVICE)
		iwl_mvm_remove_time_event(mvm, mvmvif, te_data);
	else
		iwl_mvm_remove_aux_roc_te(mvm, mvmvif, te_data);

cleanup_roc:
	/*
	 * In case we get here before the ROC event started,
	 * (so the status bit isn't set) set it here so iwl_mvm_cleanup_roc will
	 * cleanup things properly
	 */
	set_bit(vif->type == NL80211_IFTYPE_P2P_DEVICE ?
		IWL_MVM_STATUS_ROC_RUNNING : IWL_MVM_STATUS_ROC_AUX_RUNNING,
		&mvm->status);
	iwl_mvm_cleanup_roc(mvm);
}

void iwl_mvm_remove_csa_period(struct iwl_mvm *mvm,
			       struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_time_event_data *te_data = &mvmvif->time_event_data;
	u32 id;

	lockdep_assert_held(&mvm->mutex);

	spin_lock_bh(&mvm->time_event_lock);
	id = te_data->id;
	spin_unlock_bh(&mvm->time_event_lock);

	if (id != TE_CHANNEL_SWITCH_PERIOD)
		return;

	iwl_mvm_remove_time_event(mvm, mvmvif, te_data);
}

int iwl_mvm_schedule_csa_period(struct iwl_mvm *mvm,
				struct ieee80211_vif *vif,
				u32 duration, u32 apply_time)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_time_event_data *te_data = &mvmvif->time_event_data;
	struct iwl_time_event_cmd time_cmd = {};

	lockdep_assert_held(&mvm->mutex);

	if (te_data->running) {
		u32 id;

		spin_lock_bh(&mvm->time_event_lock);
		id = te_data->id;
		spin_unlock_bh(&mvm->time_event_lock);

		if (id == TE_CHANNEL_SWITCH_PERIOD) {
			IWL_DEBUG_TE(mvm, "CS period is already scheduled\n");
			return -EBUSY;
		}

		/*
		 * Remove the session protection time event to allow the
		 * channel switch. If we got here, we just heard a beacon so
		 * the session protection is not needed anymore anyway.
		 */
		iwl_mvm_remove_time_event(mvm, mvmvif, te_data);
	}

	time_cmd.action = cpu_to_le32(FW_CTXT_ACTION_ADD);
	time_cmd.id_and_color =
		cpu_to_le32(FW_CMD_ID_AND_COLOR(mvmvif->id, mvmvif->color));
	time_cmd.id = cpu_to_le32(TE_CHANNEL_SWITCH_PERIOD);
	time_cmd.apply_time = cpu_to_le32(apply_time);
	time_cmd.max_frags = TE_V2_FRAG_NONE;
	time_cmd.duration = cpu_to_le32(duration);
	time_cmd.repeat = 1;
	time_cmd.interval = cpu_to_le32(1);
	time_cmd.policy = cpu_to_le16(TE_V2_NOTIF_HOST_EVENT_START |
				      TE_V2_ABSENCE);
	if (!apply_time)
		time_cmd.policy |= cpu_to_le16(TE_V2_START_IMMEDIATELY);

	return iwl_mvm_time_event_send_add(mvm, vif, te_data, &time_cmd);
}

static bool iwl_mvm_session_prot_notif(struct iwl_notif_wait_data *notif_wait,
				       struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_mvm *mvm =
		container_of(notif_wait, struct iwl_mvm, notif_wait);
	struct iwl_mvm_session_prot_notif *resp;
	int resp_len = iwl_rx_packet_payload_len(pkt);

	if (WARN_ON(pkt->hdr.cmd != SESSION_PROTECTION_NOTIF ||
		    pkt->hdr.group_id != MAC_CONF_GROUP))
		return true;

	if (WARN_ON_ONCE(resp_len != sizeof(*resp))) {
		IWL_ERR(mvm, "Invalid SESSION_PROTECTION_NOTIF response\n");
		return true;
	}

	resp = (void *)pkt->data;

	if (!resp->status)
		IWL_ERR(mvm,
			"TIME_EVENT_NOTIFICATION received but not executed\n");

	return true;
}

void iwl_mvm_schedule_session_protection(struct iwl_mvm *mvm,
					 struct ieee80211_vif *vif,
					 u32 duration, u32 min_duration,
					 bool wait_for_notif,
					 unsigned int link_id)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_time_event_data *te_data = &mvmvif->time_event_data;
	const u16 notif[] = { WIDE_ID(MAC_CONF_GROUP, SESSION_PROTECTION_NOTIF) };
	struct iwl_notification_wait wait_notif;
	int mac_link_id = iwl_mvm_get_session_prot_id(mvm, vif, (s8)link_id);
	struct iwl_mvm_session_prot_cmd cmd = {
		.id_and_color = cpu_to_le32(mac_link_id),
		.action = cpu_to_le32(FW_CTXT_ACTION_ADD),
		.conf_id = cpu_to_le32(SESSION_PROTECT_CONF_ASSOC),
		.duration_tu = cpu_to_le32(MSEC_TO_TU(duration)),
	};

	if (mac_link_id < 0)
		return;

	lockdep_assert_held(&mvm->mutex);

	spin_lock_bh(&mvm->time_event_lock);
	if (te_data->running && te_data->link_id == link_id &&
	    time_after(te_data->end_jiffies, TU_TO_EXP_TIME(min_duration))) {
		IWL_DEBUG_TE(mvm, "We have enough time in the current TE: %u\n",
			     jiffies_to_msecs(te_data->end_jiffies - jiffies));
		spin_unlock_bh(&mvm->time_event_lock);

		return;
	}

	iwl_mvm_te_clear_data(mvm, te_data);
	/*
	 * The time_event_data.id field is reused to save session
	 * protection's configuration.
	 */
	te_data->id = le32_to_cpu(cmd.conf_id);
	te_data->duration = le32_to_cpu(cmd.duration_tu);
	te_data->vif = vif;
	te_data->link_id = link_id;
	spin_unlock_bh(&mvm->time_event_lock);

	IWL_DEBUG_TE(mvm, "Add new session protection, duration %d TU\n",
		     le32_to_cpu(cmd.duration_tu));

	if (!wait_for_notif) {
		if (iwl_mvm_send_cmd_pdu(mvm,
					 WIDE_ID(MAC_CONF_GROUP, SESSION_PROTECTION_CMD),
					 0, sizeof(cmd), &cmd)) {
			goto send_cmd_err;
		}

		return;
	}

	iwl_init_notification_wait(&mvm->notif_wait, &wait_notif,
				   notif, ARRAY_SIZE(notif),
				   iwl_mvm_session_prot_notif, NULL);

	if (iwl_mvm_send_cmd_pdu(mvm,
				 WIDE_ID(MAC_CONF_GROUP, SESSION_PROTECTION_CMD),
				 0, sizeof(cmd), &cmd)) {
		iwl_remove_notification(&mvm->notif_wait, &wait_notif);
		goto send_cmd_err;
	} else if (iwl_wait_notification(&mvm->notif_wait, &wait_notif,
					 TU_TO_JIFFIES(100))) {
		IWL_ERR(mvm,
			"Failed to protect session until session protection\n");
	}
	return;

send_cmd_err:
	IWL_ERR(mvm,
		"Couldn't send the SESSION_PROTECTION_CMD\n");
	spin_lock_bh(&mvm->time_event_lock);
	iwl_mvm_te_clear_data(mvm, te_data);
	spin_unlock_bh(&mvm->time_event_lock);
}
