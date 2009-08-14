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

#include "wl1271_reg.h"
#include "wl1271_ps.h"
#include "wl1271_spi.h"

#define WL1271_WAKEUP_TIMEOUT 500

/* Routines to toggle sleep mode while in ELP */
void wl1271_ps_elp_sleep(struct wl1271 *wl)
{
	/*
	 * FIXME: due to a problem in the firmware (causing a firmware
	 * crash), ELP entry is prevented below. Remove the "true" to
	 * re-enable ELP entry.
	 */
	if (true || wl->elp || !wl->psm)
		return;

	/*
	 * Go to ELP unless there is work already pending - pending work
	 * will immediately wakeup the chipset anyway.
	 */
	if (!work_pending(&wl->irq_work) && !work_pending(&wl->tx_work)) {
		wl1271_debug(DEBUG_PSM, "chip to elp");
		wl1271_write32(wl, HW_ACCESS_ELP_CTRL_REG_ADDR, ELPCTRL_SLEEP);
		wl->elp = true;
	}
}

int wl1271_ps_elp_wakeup(struct wl1271 *wl, bool chip_awake)
{
	DECLARE_COMPLETION_ONSTACK(compl);
	unsigned long flags;
	int ret;
	u32 start_time = jiffies;
	bool pending = false;

	if (!wl->elp)
		return 0;

	wl1271_debug(DEBUG_PSM, "waking up chip from elp");

	/*
	 * The spinlock is required here to synchronize both the work and
	 * the completion variable in one entity.
	 */
	spin_lock_irqsave(&wl->wl_lock, flags);
	if (work_pending(&wl->irq_work) || chip_awake)
		pending = true;
	else
		wl->elp_compl = &compl;
	spin_unlock_irqrestore(&wl->wl_lock, flags);

	wl1271_write32(wl, HW_ACCESS_ELP_CTRL_REG_ADDR, ELPCTRL_WAKE_UP);

	if (!pending) {
		ret = wait_for_completion_timeout(
			&compl, msecs_to_jiffies(WL1271_WAKEUP_TIMEOUT));
		if (ret == 0) {
			wl1271_error("ELP wakeup timeout!");
			ret = -ETIMEDOUT;
			goto err;
		} else if (ret < 0) {
			wl1271_error("ELP wakeup completion error.");
			goto err;
		}
	}

	wl->elp = false;

	wl1271_debug(DEBUG_PSM, "wakeup time: %u ms",
		     jiffies_to_msecs(jiffies - start_time));
	goto out;

err:
	spin_lock_irqsave(&wl->wl_lock, flags);
	wl->elp_compl = NULL;
	spin_unlock_irqrestore(&wl->wl_lock, flags);
	return ret;

out:
	return 0;
}

int wl1271_ps_set_mode(struct wl1271 *wl, enum wl1271_cmd_ps_mode mode)
{
	int ret;

	switch (mode) {
	case STATION_POWER_SAVE_MODE:
		wl1271_debug(DEBUG_PSM, "entering psm");
		ret = wl1271_cmd_ps_mode(wl, STATION_POWER_SAVE_MODE);
		if (ret < 0)
			return ret;

		wl1271_ps_elp_sleep(wl);
		if (ret < 0)
			return ret;

		wl->psm = 1;
		break;
	case STATION_ACTIVE_MODE:
	default:
		wl1271_debug(DEBUG_PSM, "leaving psm");
		ret = wl1271_ps_elp_wakeup(wl, false);
		if (ret < 0)
			return ret;

		ret = wl1271_cmd_ps_mode(wl, STATION_ACTIVE_MODE);
		if (ret < 0)
			return ret;

		wl->psm = 0;
		break;
	}

	return ret;
}


