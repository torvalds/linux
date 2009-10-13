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

void wl1271_elp_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wl1271 *wl;

	dwork = container_of(work, struct delayed_work, work);
	wl = container_of(dwork, struct wl1271, elp_work);

	wl1271_debug(DEBUG_PSM, "elp work");

	mutex_lock(&wl->mutex);

	if (wl->elp || !wl->psm)
		goto out;

	wl1271_debug(DEBUG_PSM, "chip to elp");
	wl1271_raw_write32(wl, HW_ACCESS_ELP_CTRL_REG_ADDR, ELPCTRL_SLEEP);
	wl->elp = true;

out:
	mutex_unlock(&wl->mutex);
}

#define ELP_ENTRY_DELAY  5

/* Routines to toggle sleep mode while in ELP */
void wl1271_ps_elp_sleep(struct wl1271 *wl)
{
	if (wl->psm) {
		cancel_delayed_work(&wl->elp_work);
		ieee80211_queue_delayed_work(wl->hw, &wl->elp_work,
					msecs_to_jiffies(ELP_ENTRY_DELAY));
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

	wl1271_raw_write32(wl, HW_ACCESS_ELP_CTRL_REG_ADDR, ELPCTRL_WAKE_UP);

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

		/* enable beacon filtering */
		ret = wl1271_acx_beacon_filter_opt(wl, true);
		if (ret < 0)
			return ret;

		/* enable beacon early termination */
		ret = wl1271_acx_bet_enable(wl, true);
		if (ret < 0)
			return ret;

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

		/* disable beacon early termination */
		ret = wl1271_acx_bet_enable(wl, false);
		if (ret < 0)
			return ret;

		/* disable beacon filtering */
		ret = wl1271_acx_beacon_filter_opt(wl, false);
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


