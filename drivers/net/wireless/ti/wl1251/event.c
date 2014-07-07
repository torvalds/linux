/*
 * This file is part of wl1251
 *
 * Copyright (c) 1998-2007 Texas Instruments Incorporated
 * Copyright (C) 2008 Nokia Corporation
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

#include "wl1251.h"
#include "reg.h"
#include "io.h"
#include "event.h"
#include "ps.h"

static int wl1251_event_scan_complete(struct wl1251 *wl,
				      struct event_mailbox *mbox)
{
	int ret = 0;

	wl1251_debug(DEBUG_EVENT, "status: 0x%x, channels: %d",
		     mbox->scheduled_scan_status,
		     mbox->scheduled_scan_channels);

	if (wl->scanning) {
		ieee80211_scan_completed(wl->hw, false);
		wl1251_debug(DEBUG_MAC80211, "mac80211 hw scan completed");
		wl->scanning = false;
		if (wl->hw->conf.flags & IEEE80211_CONF_IDLE)
			ret = wl1251_ps_set_mode(wl, STATION_IDLE);
	}

	return ret;
}

#define WL1251_PSM_ENTRY_RETRIES  3
static int wl1251_event_ps_report(struct wl1251 *wl,
				  struct event_mailbox *mbox)
{
	int ret = 0;

	wl1251_debug(DEBUG_EVENT, "ps status: %x", mbox->ps_status);

	switch (mbox->ps_status) {
	case EVENT_ENTER_POWER_SAVE_FAIL:
		wl1251_debug(DEBUG_PSM, "PSM entry failed");

		if (wl->station_mode != STATION_POWER_SAVE_MODE) {
			/* remain in active mode */
			wl->psm_entry_retry = 0;
			break;
		}

		if (wl->psm_entry_retry < WL1251_PSM_ENTRY_RETRIES) {
			wl->psm_entry_retry++;
			ret = wl1251_ps_set_mode(wl, STATION_POWER_SAVE_MODE);
		} else {
			wl1251_error("Power save entry failed, giving up");
			wl->psm_entry_retry = 0;
		}
		break;
	case EVENT_ENTER_POWER_SAVE_SUCCESS:
	case EVENT_EXIT_POWER_SAVE_FAIL:
	case EVENT_EXIT_POWER_SAVE_SUCCESS:
	default:
		wl->psm_entry_retry = 0;
		break;
	}

	return 0;
}

static void wl1251_event_mbox_dump(struct event_mailbox *mbox)
{
	wl1251_debug(DEBUG_EVENT, "MBOX DUMP:");
	wl1251_debug(DEBUG_EVENT, "\tvector: 0x%x", mbox->events_vector);
	wl1251_debug(DEBUG_EVENT, "\tmask: 0x%x", mbox->events_mask);
}

static int wl1251_event_process(struct wl1251 *wl, struct event_mailbox *mbox)
{
	int ret;
	u32 vector;

	wl1251_event_mbox_dump(mbox);

	vector = mbox->events_vector & ~(mbox->events_mask);
	wl1251_debug(DEBUG_EVENT, "vector: 0x%x", vector);

	if (vector & SCAN_COMPLETE_EVENT_ID) {
		ret = wl1251_event_scan_complete(wl, mbox);
		if (ret < 0)
			return ret;
	}

	if (vector & BSS_LOSE_EVENT_ID) {
		wl1251_debug(DEBUG_EVENT, "BSS_LOSE_EVENT");

		if (wl->psm_requested &&
		    wl->station_mode != STATION_ACTIVE_MODE) {
			ret = wl1251_ps_set_mode(wl, STATION_ACTIVE_MODE);
			if (ret < 0)
				return ret;
		}
	}

	if (vector & PS_REPORT_EVENT_ID) {
		wl1251_debug(DEBUG_EVENT, "PS_REPORT_EVENT");
		ret = wl1251_event_ps_report(wl, mbox);
		if (ret < 0)
			return ret;
	}

	if (vector & SYNCHRONIZATION_TIMEOUT_EVENT_ID) {
		wl1251_debug(DEBUG_EVENT, "SYNCHRONIZATION_TIMEOUT_EVENT");

		/* indicate to the stack, that beacons have been lost */
		if (wl->vif && wl->vif->type == NL80211_IFTYPE_STATION)
			ieee80211_beacon_loss(wl->vif);
	}

	if (vector & REGAINED_BSS_EVENT_ID) {
		if (wl->psm_requested) {
			ret = wl1251_ps_set_mode(wl, STATION_POWER_SAVE_MODE);
			if (ret < 0)
				return ret;
		}
	}

	if (wl->vif && wl->rssi_thold) {
		if (vector & ROAMING_TRIGGER_LOW_RSSI_EVENT_ID) {
			wl1251_debug(DEBUG_EVENT,
				     "ROAMING_TRIGGER_LOW_RSSI_EVENT");
			ieee80211_cqm_rssi_notify(wl->vif,
				NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW,
				GFP_KERNEL);
		}

		if (vector & ROAMING_TRIGGER_REGAINED_RSSI_EVENT_ID) {
			wl1251_debug(DEBUG_EVENT,
				     "ROAMING_TRIGGER_REGAINED_RSSI_EVENT");
			ieee80211_cqm_rssi_notify(wl->vif,
				NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
				GFP_KERNEL);
		}
	}

	return 0;
}

/*
 * Poll the mailbox event field until any of the bits in the mask is set or a
 * timeout occurs (WL1251_EVENT_TIMEOUT in msecs)
 */
int wl1251_event_wait(struct wl1251 *wl, u32 mask, int timeout_ms)
{
	u32 events_vector, event;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(timeout_ms);

	do {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;

		msleep(1);

		/* read from both event fields */
		wl1251_mem_read(wl, wl->mbox_ptr[0], &events_vector,
				sizeof(events_vector));
		event = events_vector & mask;
		wl1251_mem_read(wl, wl->mbox_ptr[1], &events_vector,
				sizeof(events_vector));
		event |= events_vector & mask;
	} while (!event);

	return 0;
}

int wl1251_event_unmask(struct wl1251 *wl)
{
	int ret;

	ret = wl1251_acx_event_mbox_mask(wl, ~(wl->event_mask));
	if (ret < 0)
		return ret;

	return 0;
}

void wl1251_event_mbox_config(struct wl1251 *wl)
{
	wl->mbox_ptr[0] = wl1251_reg_read32(wl, REG_EVENT_MAILBOX_PTR);
	wl->mbox_ptr[1] = wl->mbox_ptr[0] + sizeof(struct event_mailbox);

	wl1251_debug(DEBUG_EVENT, "MBOX ptrs: 0x%x 0x%x",
		     wl->mbox_ptr[0], wl->mbox_ptr[1]);
}

int wl1251_event_handle(struct wl1251 *wl, u8 mbox_num)
{
	struct event_mailbox mbox;
	int ret;

	wl1251_debug(DEBUG_EVENT, "EVENT on mbox %d", mbox_num);

	if (mbox_num > 1)
		return -EINVAL;

	/* first we read the mbox descriptor */
	wl1251_mem_read(wl, wl->mbox_ptr[mbox_num], &mbox,
			    sizeof(struct event_mailbox));

	/* process the descriptor */
	ret = wl1251_event_process(wl, &mbox);
	if (ret < 0)
		return ret;

	/* then we let the firmware know it can go on...*/
	wl1251_reg_write32(wl, ACX_REG_INTERRUPT_TRIG, INTR_TRIG_EVENT_ACK);

	return 0;
}
