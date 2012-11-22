/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2012 Texas Instruments. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include "event.h"
#include "scan.h"
#include "../wlcore/cmd.h"
#include "../wlcore/debug.h"

int wl12xx_wait_for_event(struct wl1271 *wl, enum wlcore_wait_event event,
			  bool *timeout)
{
	u32 local_event;

	switch (event) {
	case WLCORE_EVENT_ROLE_STOP_COMPLETE:
		local_event = ROLE_STOP_COMPLETE_EVENT_ID;
		break;

	case WLCORE_EVENT_PEER_REMOVE_COMPLETE:
		local_event = PEER_REMOVE_COMPLETE_EVENT_ID;
		break;

	default:
		/* event not implemented */
		return 0;
	}
	return wlcore_cmd_wait_for_event_or_timeout(wl, local_event, timeout);
}

int wl12xx_process_mailbox_events(struct wl1271 *wl)
{
	struct wl12xx_event_mailbox *mbox = wl->mbox;
	u32 vector;


	vector = le32_to_cpu(mbox->events_vector);
	vector &= ~(le32_to_cpu(mbox->events_mask));

	wl1271_debug(DEBUG_EVENT, "MBOX vector: 0x%x", vector);

	if (vector & SCAN_COMPLETE_EVENT_ID) {
		wl1271_debug(DEBUG_EVENT, "status: 0x%x",
			     mbox->scheduled_scan_status);

		if (wl->scan_wlvif)
			wl12xx_scan_completed(wl, wl->scan_wlvif);
	}

	if (vector & PERIODIC_SCAN_REPORT_EVENT_ID)
		wlcore_event_sched_scan_report(wl,
					       mbox->scheduled_scan_status);

	if (vector & PERIODIC_SCAN_COMPLETE_EVENT_ID)
		wlcore_event_sched_scan_completed(wl,
						  mbox->scheduled_scan_status);
	if (vector & SOFT_GEMINI_SENSE_EVENT_ID)
		wlcore_event_soft_gemini_sense(wl,
					       mbox->soft_gemini_sense_info);

	if (vector & BSS_LOSE_EVENT_ID)
		wlcore_event_beacon_loss(wl, 0xff);

	if (vector & RSSI_SNR_TRIGGER_0_EVENT_ID)
		wlcore_event_rssi_trigger(wl, mbox->rssi_snr_trigger_metric);

	if (vector & BA_SESSION_RX_CONSTRAINT_EVENT_ID)
		wlcore_event_ba_rx_constraint(wl,
					      BIT(mbox->role_id),
					      mbox->rx_ba_allowed);

	if (vector & CHANNEL_SWITCH_COMPLETE_EVENT_ID)
		wlcore_event_channel_switch(wl, 0xff,
					    mbox->channel_switch_status);

	if (vector & DUMMY_PACKET_EVENT_ID)
		wlcore_event_dummy_packet(wl);

	/*
	 * "TX retries exceeded" has a different meaning according to mode.
	 * In AP mode the offending station is disconnected.
	 */
	if (vector & MAX_TX_RETRY_EVENT_ID)
		wlcore_event_max_tx_failure(wl,
				le16_to_cpu(mbox->sta_tx_retry_exceeded));

	if (vector & INACTIVE_STA_EVENT_ID)
		wlcore_event_inactive_sta(wl,
					  le16_to_cpu(mbox->sta_aging_status));

	if (vector & REMAIN_ON_CHANNEL_COMPLETE_EVENT_ID)
		wlcore_event_roc_complete(wl);

	return 0;
}
