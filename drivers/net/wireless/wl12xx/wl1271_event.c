/*
 * This file is part of wl1271
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
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

#include "wl1271.h"
#include "wl1271_reg.h"
#include "wl1271_spi.h"
#include "wl1271_event.h"
#include "wl1271_ps.h"
#include "wl12xx_80211.h"

static int wl1271_event_scan_complete(struct wl1271 *wl,
				      struct event_mailbox *mbox)
{
	int size = sizeof(struct wl12xx_probe_req_template);
	wl1271_debug(DEBUG_EVENT, "status: 0x%x",
		     mbox->scheduled_scan_status);

	if (wl->scanning) {
		if (wl->scan.state == WL1271_SCAN_BAND_DUAL) {
			wl1271_cmd_template_set(wl, CMD_TEMPL_CFG_PROBE_REQ_2_4,
						NULL, size);
			/* 2.4 GHz band scanned, scan 5 GHz band, pretend
			 * to the wl1271_cmd_scan function that we are not
			 * scanning as it checks that.
			 */
			wl->scanning = false;
			wl1271_cmd_scan(wl, wl->scan.ssid, wl->scan.ssid_len,
						wl->scan.active,
						wl->scan.high_prio,
						WL1271_SCAN_BAND_5_GHZ,
						wl->scan.probe_requests);
		} else {
			if (wl->scan.state == WL1271_SCAN_BAND_2_4_GHZ)
				wl1271_cmd_template_set(wl,
						CMD_TEMPL_CFG_PROBE_REQ_2_4,
						NULL, size);
			else
				wl1271_cmd_template_set(wl,
						CMD_TEMPL_CFG_PROBE_REQ_5,
						NULL, size);

			mutex_unlock(&wl->mutex);
			ieee80211_scan_completed(wl->hw, false);
			mutex_lock(&wl->mutex);
			wl->scanning = false;
		}
	}
	return 0;
}

static int wl1271_event_ps_report(struct wl1271 *wl,
				  struct event_mailbox *mbox,
				  bool *beacon_loss)
{
	int ret = 0;

	wl1271_debug(DEBUG_EVENT, "ps_status: 0x%x", mbox->ps_status);

	switch (mbox->ps_status) {
	case EVENT_ENTER_POWER_SAVE_FAIL:
		if (!wl->psm) {
			wl->psm_entry_retry = 0;
			break;
		}

		if (wl->psm_entry_retry < wl->conf.conn.psm_entry_retries) {
			wl->psm_entry_retry++;
			ret = wl1271_ps_set_mode(wl, STATION_POWER_SAVE_MODE);
		} else {
			wl1271_error("PSM entry failed, giving up.\n");
			wl->psm_entry_retry = 0;
			*beacon_loss = true;
		}
		break;
	case EVENT_ENTER_POWER_SAVE_SUCCESS:
		wl->psm_entry_retry = 0;
		break;
	case EVENT_EXIT_POWER_SAVE_FAIL:
		wl1271_info("PSM exit failed");
		break;
	case EVENT_EXIT_POWER_SAVE_SUCCESS:
	default:
		break;
	}

	return ret;
}

static void wl1271_event_mbox_dump(struct event_mailbox *mbox)
{
	wl1271_debug(DEBUG_EVENT, "MBOX DUMP:");
	wl1271_debug(DEBUG_EVENT, "\tvector: 0x%x", mbox->events_vector);
	wl1271_debug(DEBUG_EVENT, "\tmask: 0x%x", mbox->events_mask);
}

static int wl1271_event_process(struct wl1271 *wl, struct event_mailbox *mbox)
{
	int ret;
	u32 vector;
	bool beacon_loss = false;

	wl1271_event_mbox_dump(mbox);

	vector = le32_to_cpu(mbox->events_vector);
	vector &= ~(le32_to_cpu(mbox->events_mask));
	wl1271_debug(DEBUG_EVENT, "vector: 0x%x", vector);

	if (vector & SCAN_COMPLETE_EVENT_ID) {
		ret = wl1271_event_scan_complete(wl, mbox);
		if (ret < 0)
			return ret;
	}

	/*
	 * The BSS_LOSE_EVENT_ID is only needed while psm (and hence beacon
	 * filtering) is enabled. Without PSM, the stack will receive all
	 * beacons and can detect beacon loss by itself.
	 */
	if (vector & BSS_LOSE_EVENT_ID && wl->psm) {
		wl1271_debug(DEBUG_EVENT, "BSS_LOSE_EVENT");

		/* indicate to the stack, that beacons have been lost */
		beacon_loss = true;
	}

	if (vector & PS_REPORT_EVENT_ID) {
		wl1271_debug(DEBUG_EVENT, "PS_REPORT_EVENT");
		ret = wl1271_event_ps_report(wl, mbox, &beacon_loss);
		if (ret < 0)
			return ret;
	}

	if (beacon_loss) {
		/* Obviously, it's dangerous to release the mutex while
		   we are holding many of the variables in the wl struct.
		   That's why it's done last in the function, and care must
		   be taken that nothing more is done after this function
		   returns. */
		mutex_unlock(&wl->mutex);
		ieee80211_beacon_loss(wl->vif);
		mutex_lock(&wl->mutex);
	}

	return 0;
}

int wl1271_event_unmask(struct wl1271 *wl)
{
	int ret;

	ret = wl1271_acx_event_mbox_mask(wl, ~(wl->event_mask));
	if (ret < 0)
		return ret;

	return 0;
}

void wl1271_event_mbox_config(struct wl1271 *wl)
{
	wl->mbox_ptr[0] = wl1271_spi_read32(wl, REG_EVENT_MAILBOX_PTR);
	wl->mbox_ptr[1] = wl->mbox_ptr[0] + sizeof(struct event_mailbox);

	wl1271_debug(DEBUG_EVENT, "MBOX ptrs: 0x%x 0x%x",
		     wl->mbox_ptr[0], wl->mbox_ptr[1]);
}

int wl1271_event_handle(struct wl1271 *wl, u8 mbox_num, bool do_ack)
{
	struct event_mailbox mbox;
	int ret;

	wl1271_debug(DEBUG_EVENT, "EVENT on mbox %d", mbox_num);

	if (mbox_num > 1)
		return -EINVAL;

	/* first we read the mbox descriptor */
	wl1271_spi_read(wl, wl->mbox_ptr[mbox_num], &mbox,
			sizeof(struct event_mailbox), false);

	/* process the descriptor */
	ret = wl1271_event_process(wl, &mbox);
	if (ret < 0)
		return ret;

	/* then we let the firmware know it can go on...*/
	if (do_ack)
		wl1271_spi_write32(wl, ACX_REG_INTERRUPT_TRIG,
				   INTR_TRIG_EVENT_ACK);

	return 0;
}
