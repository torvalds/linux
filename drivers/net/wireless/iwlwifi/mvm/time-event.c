/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
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
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
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

#include <linux/jiffies.h>
#include <net/mac80211.h>

#include "iwl-notif-wait.h"
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

	if (te_data->id == TE_MAX)
		return;

	list_del(&te_data->list);
	te_data->running = false;
	te_data->uid = 0;
	te_data->id = TE_MAX;
	te_data->vif = NULL;
}

void iwl_mvm_roc_done_wk(struct work_struct *wk)
{
	struct iwl_mvm *mvm = container_of(wk, struct iwl_mvm, roc_done_wk);
	u32 queues = 0;

	/*
	 * Clear the ROC_RUNNING /ROC_AUX_RUNNING status bit.
	 * This will cause the TX path to drop offchannel transmissions.
	 * That would also be done by mac80211, but it is racy, in particular
	 * in the case that the time event actually completed in the firmware
	 * (which is handled in iwl_mvm_te_handle_notif).
	 */
	if (test_and_clear_bit(IWL_MVM_STATUS_ROC_RUNNING, &mvm->status)) {
		queues |= BIT(IWL_MVM_OFFCHANNEL_QUEUE);
		iwl_mvm_unref(mvm, IWL_MVM_REF_ROC);
	}
	if (test_and_clear_bit(IWL_MVM_STATUS_ROC_AUX_RUNNING, &mvm->status)) {
		queues |= BIT(mvm->aux_queue);
		iwl_mvm_unref(mvm, IWL_MVM_REF_ROC_AUX);
	}

	synchronize_net();

	/*
	 * Flush the offchannel queue -- this is called when the time
	 * event finishes or is canceled, so that frames queued for it
	 * won't get stuck on the queue and be transmitted in the next
	 * time event.
	 * We have to send the command asynchronously since this cannot
	 * be under the mutex for locking reasons, but that's not an
	 * issue as it will have to complete before the next command is
	 * executed, and a new time event means a new command.
	 */
	iwl_mvm_flush_tx_path(mvm, queues, false);
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
	if (!csa_vif || !csa_vif->csa_active)
		goto out_unlock;

	IWL_DEBUG_TE(mvm, "CSA NOA started\n");

	/*
	 * CSA NoA is started but we still have beacons to
	 * transmit on the current channel.
	 * So we just do nothing here and the switch
	 * will be performed on the last TBTT.
	 */
	if (!ieee80211_csa_is_complete(csa_vif)) {
		IWL_WARN(mvm, "CSA NOA started too early\n");
		goto out_unlock;
	}

	ieee80211_csa_finish(csa_vif);

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
	if (vif->type != NL80211_IFTYPE_STATION)
		return false;
	if (vif->bss_conf.assoc && vif->bss_conf.dtim_period)
		return false;
	if (errmsg)
		IWL_ERR(mvm, "%s\n", errmsg);

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
		ieee80211_chswitch_done(te_data->vif, true);
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

	if (!iwl_fw_dbg_trigger_enabled(mvm->fw, FW_DBG_TRIGGER_TIME_EVENT))
		return;

	trig = iwl_fw_dbg_get_trigger(mvm->fw, FW_DBG_TRIGGER_TIME_EVENT);
	te_trig = (void *)trig->data;

	if (!iwl_fw_dbg_trigger_check_stop(mvm, te_data->vif, trig))
		return;

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

		iwl_mvm_fw_dbg_collect_trig(mvm, trig,
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
			 * By now, we should have finished association
			 * and know the dtim period.
			 */
			iwl_mvm_te_check_disconnect(mvm, te_data->vif,
				"No association and the time event is over already...");
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
			iwl_mvm_ref(mvm, IWL_MVM_REF_ROC);
			ieee80211_ready_on_channel(mvm->hw);
		} else if (te_data->id == TE_CHANNEL_SWITCH_PERIOD) {
			iwl_mvm_te_handle_notify_csa(mvm, te_data, notif);
		}
	} else {
		IWL_WARN(mvm, "Got TE with unknown action\n");
	}
}

/*
 * Handle A Aux ROC time event
 */
static int iwl_mvm_aux_roc_te_handle_notif(struct iwl_mvm *mvm,
					   struct iwl_time_event_notif *notif)
{
	struct iwl_mvm_time_event_data *te_data, *tmp;
	bool aux_roc_te = false;

	list_for_each_entry_safe(te_data, tmp, &mvm->aux_roc_te_list, list) {
		if (le32_to_cpu(notif->unique_id) == te_data->uid) {
			aux_roc_te = true;
			break;
		}
	}
	if (!aux_roc_te) /* Not a Aux ROC time event */
		return -EINVAL;

	iwl_mvm_te_check_trigger(mvm, notif, te_data);

	if (!le32_to_cpu(notif->status)) {
		IWL_DEBUG_TE(mvm,
			     "ERROR: Aux ROC Time Event %s notification failure\n",
			     (le32_to_cpu(notif->action) &
			      TE_V2_NOTIF_HOST_EVENT_START) ? "start" : "end");
		return -EINVAL;
	}

	IWL_DEBUG_TE(mvm,
		     "Aux ROC time event notification  - UID = 0x%x action %d\n",
		     le32_to_cpu(notif->unique_id),
		     le32_to_cpu(notif->action));

	if (le32_to_cpu(notif->action) == TE_V2_NOTIF_HOST_EVENT_END) {
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
		iwl_mvm_ref(mvm, IWL_MVM_REF_ROC_AUX);
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
int iwl_mvm_rx_time_event_notif(struct iwl_mvm *mvm,
				struct iwl_rx_cmd_buffer *rxb,
				struct iwl_device_cmd *cmd)
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

	return 0;
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
	static const u8 time_event_response[] = { TIME_EVENT_CMD };
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
	const u8 te_notif_response[] = { TIME_EVENT_NOTIFICATION };
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

	time_cmd.apply_time =
		cpu_to_le32(iwl_read_prph(mvm->trans, DEVICE_SYSTEM_TIME_REG));

	time_cmd.max_frags = TE_V2_FRAG_NONE;
	time_cmd.max_delay = cpu_to_le32(max_delay);
	/* TODO: why do we need to interval = bi if it is not periodic? */
	time_cmd.interval = cpu_to_le32(1);
	time_cmd.duration = cpu_to_le32(duration);
	time_cmd.repeat = 1;
	time_cmd.policy = cpu_to_le16(TE_V2_NOTIF_HOST_EVENT_START |
				      TE_V2_NOTIF_HOST_EVENT_END |
				      T2_V2_START_IMMEDIATELY);

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

static bool __iwl_mvm_remove_time_event(struct iwl_mvm *mvm,
					struct iwl_mvm_time_event_data *te_data,
					u32 *uid)
{
	u32 id;

	/*
	 * It is possible that by the time we got to this point the time
	 * event was already removed.
	 */
	spin_lock_bh(&mvm->time_event_lock);

	/* Save time event uid before clearing its data */
	*uid = te_data->uid;
	id = te_data->id;

	/*
	 * The clear_data function handles time events that were already removed
	 */
	iwl_mvm_te_clear_data(mvm, te_data);
	spin_unlock_bh(&mvm->time_event_lock);

	/*
	 * It is possible that by the time we try to remove it, the time event
	 * has already ended and removed. In such a case there is no need to
	 * send a removal command.
	 */
	if (id == TE_MAX) {
		IWL_DEBUG_TE(mvm, "TE 0x%x has already ended\n", *uid);
		return false;
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
				   sizeof(aux_cmd), &aux_cmd);

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
	if (WARN_ON(ret))
		return;
}

void iwl_mvm_stop_session_protection(struct iwl_mvm *mvm,
				     struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm_time_event_data *te_data = &mvmvif->time_event_data;

	lockdep_assert_held(&mvm->mutex);
	iwl_mvm_remove_time_event(mvm, mvmvif, te_data);
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

	/*
	 * Flush the done work, just in case it's still pending, so that
	 * the work it does can complete and we can accept new frames.
	 */
	flush_work(&mvm->roc_done_wk);

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
				      T2_V2_START_IMMEDIATELY);

	return iwl_mvm_time_event_send_add(mvm, vif, te_data, &time_cmd);
}

void iwl_mvm_stop_roc(struct iwl_mvm *mvm)
{
	struct iwl_mvm_vif *mvmvif;
	struct iwl_mvm_time_event_data *te_data;
	bool is_p2p = false;

	lockdep_assert_held(&mvm->mutex);

	mvmvif = NULL;
	spin_lock_bh(&mvm->time_event_lock);

	/*
	 * Iterate over the list of time events and find the time event that is
	 * associated with a P2P_DEVICE interface.
	 * This assumes that a P2P_DEVICE interface can have only a single time
	 * event at any given time and this time event coresponds to a ROC
	 * request
	 */
	list_for_each_entry(te_data, &mvm->time_event_list, list) {
		if (te_data->vif->type == NL80211_IFTYPE_P2P_DEVICE) {
			mvmvif = iwl_mvm_vif_from_mac80211(te_data->vif);
			is_p2p = true;
			goto remove_te;
		}
	}

	/*
	 * Iterate over the list of aux roc time events and find the time
	 * event that is associated with a BSS interface.
	 * This assumes that a BSS interface can have only a single time
	 * event at any given time and this time event corresponds to a ROC
	 * request
	 */
	list_for_each_entry(te_data, &mvm->aux_roc_te_list, list) {
		mvmvif = iwl_mvm_vif_from_mac80211(te_data->vif);
		goto remove_te;
	}

remove_te:
	spin_unlock_bh(&mvm->time_event_lock);

	if (!mvmvif) {
		IWL_WARN(mvm, "No remain on channel event\n");
		return;
	}

	if (is_p2p)
		iwl_mvm_remove_time_event(mvm, mvmvif, te_data);
	else
		iwl_mvm_remove_aux_roc_te(mvm, mvmvif, te_data);

	iwl_mvm_roc_finished(mvm);
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
		IWL_DEBUG_TE(mvm, "CS period is already scheduled\n");
		return -EBUSY;
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

	return iwl_mvm_time_event_send_add(mvm, vif, te_data, &time_cmd);
}
