/*
 * This file is part of wl12xx
 *
 * Copyright (c) 1998-2007 Texas Instruments Incorporated
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Kalle Valo <kalle.valo@nokia.com>
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

#include "wl12xx.h"
#include "reg.h"
#include "spi.h"
#include "event.h"
#include "ps.h"

static int wl12xx_event_scan_complete(struct wl12xx *wl,
				      struct event_mailbox *mbox)
{
	wl12xx_debug(DEBUG_EVENT, "status: 0x%x, channels: %d",
		     mbox->scheduled_scan_status,
		     mbox->scheduled_scan_channels);

	if (wl->scanning) {
		mutex_unlock(&wl->mutex);
		ieee80211_scan_completed(wl->hw, false);
		mutex_lock(&wl->mutex);
		wl->scanning = false;
	}

	return 0;
}

static void wl12xx_event_mbox_dump(struct event_mailbox *mbox)
{
	wl12xx_debug(DEBUG_EVENT, "MBOX DUMP:");
	wl12xx_debug(DEBUG_EVENT, "\tvector: 0x%x", mbox->events_vector);
	wl12xx_debug(DEBUG_EVENT, "\tmask: 0x%x", mbox->events_mask);
}

static int wl12xx_event_process(struct wl12xx *wl, struct event_mailbox *mbox)
{
	int ret;
	u32 vector;

	wl12xx_event_mbox_dump(mbox);

	vector = mbox->events_vector & ~(mbox->events_mask);
	wl12xx_debug(DEBUG_EVENT, "vector: 0x%x", vector);

	if (vector & SCAN_COMPLETE_EVENT_ID) {
		ret = wl12xx_event_scan_complete(wl, mbox);
		if (ret < 0)
			return ret;
	}

	if (vector & BSS_LOSE_EVENT_ID) {
		wl12xx_debug(DEBUG_EVENT, "BSS_LOSE_EVENT");

		if (wl->psm_requested && wl->psm) {
			ret = wl12xx_ps_set_mode(wl, STATION_ACTIVE_MODE);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

int wl12xx_event_unmask(struct wl12xx *wl)
{
	int ret;

	ret = wl12xx_acx_event_mbox_mask(wl, ~(wl->event_mask));
	if (ret < 0)
		return ret;

	return 0;
}

void wl12xx_event_mbox_config(struct wl12xx *wl)
{
	wl->mbox_ptr[0] = wl12xx_reg_read32(wl, REG_EVENT_MAILBOX_PTR);
	wl->mbox_ptr[1] = wl->mbox_ptr[0] + sizeof(struct event_mailbox);

	wl12xx_debug(DEBUG_EVENT, "MBOX ptrs: 0x%x 0x%x",
		     wl->mbox_ptr[0], wl->mbox_ptr[1]);
}

int wl12xx_event_handle(struct wl12xx *wl, u8 mbox_num)
{
	struct event_mailbox mbox;
	int ret;

	wl12xx_debug(DEBUG_EVENT, "EVENT on mbox %d", mbox_num);

	if (mbox_num > 1)
		return -EINVAL;

	/* first we read the mbox descriptor */
	wl12xx_spi_mem_read(wl, wl->mbox_ptr[mbox_num], &mbox,
			    sizeof(struct event_mailbox));

	/* process the descriptor */
	ret = wl12xx_event_process(wl, &mbox);
	if (ret < 0)
		return ret;

	/* then we let the firmware know it can go on...*/
	wl12xx_reg_write32(wl, ACX_REG_INTERRUPT_TRIG, INTR_TRIG_EVENT_ACK);

	return 0;
}
