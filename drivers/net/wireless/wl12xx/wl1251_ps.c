/*
 * This file is part of wl1251
 *
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

#include "wl1251_reg.h"
#include "wl1251_ps.h"
#include "wl1251_cmd.h"
#include "wl1251_io.h"

/* in ms */
#define WL1251_WAKEUP_TIMEOUT 100

void wl1251_elp_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wl1251 *wl;

	dwork = container_of(work, struct delayed_work, work);
	wl = container_of(dwork, struct wl1251, elp_work);

	wl1251_debug(DEBUG_PSM, "elp work");

	mutex_lock(&wl->mutex);

	if (wl->elp || !wl->psm)
		goto out;

	wl1251_debug(DEBUG_PSM, "chip to elp");
	wl1251_write_elp(wl, HW_ACCESS_ELP_CTRL_REG_ADDR, ELPCTRL_SLEEP);
	wl->elp = true;

out:
	mutex_unlock(&wl->mutex);
}

#define ELP_ENTRY_DELAY  5

/* Routines to toggle sleep mode while in ELP */
void wl1251_ps_elp_sleep(struct wl1251 *wl)
{
	unsigned long delay;

	if (wl->psm) {
		cancel_delayed_work(&wl->elp_work);
		delay = msecs_to_jiffies(ELP_ENTRY_DELAY);
		ieee80211_queue_delayed_work(wl->hw, &wl->elp_work, delay);
	}
}

int wl1251_ps_elp_wakeup(struct wl1251 *wl)
{
	unsigned long timeout, start;
	u32 elp_reg;

	if (!wl->elp)
		return 0;

	wl1251_debug(DEBUG_PSM, "waking up chip from elp");

	start = jiffies;
	timeout = jiffies + msecs_to_jiffies(WL1251_WAKEUP_TIMEOUT);

	wl1251_write_elp(wl, HW_ACCESS_ELP_CTRL_REG_ADDR, ELPCTRL_WAKE_UP);

	elp_reg = wl1251_read_elp(wl, HW_ACCESS_ELP_CTRL_REG_ADDR);

	/*
	 * FIXME: we should wait for irq from chip but, as a temporary
	 * solution to simplify locking, let's poll instead
	 */
	while (!(elp_reg & ELPCTRL_WLAN_READY)) {
		if (time_after(jiffies, timeout)) {
			wl1251_error("elp wakeup timeout");
			return -ETIMEDOUT;
		}
		msleep(1);
		elp_reg = wl1251_read_elp(wl, HW_ACCESS_ELP_CTRL_REG_ADDR);
	}

	wl1251_debug(DEBUG_PSM, "wakeup time: %u ms",
		     jiffies_to_msecs(jiffies - start));

	wl->elp = false;

	return 0;
}

static int wl1251_ps_set_elp(struct wl1251 *wl, bool enable)
{
	int ret;

	if (enable) {
		wl1251_debug(DEBUG_PSM, "sleep auth psm/elp");

		ret = wl1251_acx_sleep_auth(wl, WL1251_PSM_ELP);
		if (ret < 0)
			return ret;

		wl1251_ps_elp_sleep(wl);
	} else {
		wl1251_debug(DEBUG_PSM, "sleep auth cam");

		/*
		 * When the target is in ELP, we can only
		 * access the ELP control register. Thus,
		 * we have to wake the target up before
		 * changing the power authorization.
		 */

		wl1251_ps_elp_wakeup(wl);

		ret = wl1251_acx_sleep_auth(wl, WL1251_PSM_CAM);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int wl1251_ps_set_mode(struct wl1251 *wl, enum wl1251_cmd_ps_mode mode)
{
	int ret;

	switch (mode) {
	case STATION_POWER_SAVE_MODE:
		wl1251_debug(DEBUG_PSM, "entering psm");

		/* enable beacon filtering */
		ret = wl1251_acx_beacon_filter_opt(wl, true);
		if (ret < 0)
			return ret;

		ret = wl1251_acx_wake_up_conditions(wl,
						    WAKE_UP_EVENT_DTIM_BITMAP,
						    wl->listen_int);
		if (ret < 0)
			return ret;

		ret = wl1251_cmd_ps_mode(wl, STATION_POWER_SAVE_MODE);
		if (ret < 0)
			return ret;

		ret = wl1251_ps_set_elp(wl, true);
		if (ret < 0)
			return ret;

		wl->psm = 1;
		break;
	case STATION_ACTIVE_MODE:
	default:
		wl1251_debug(DEBUG_PSM, "leaving psm");
		ret = wl1251_ps_set_elp(wl, false);
		if (ret < 0)
			return ret;

		/* disable beacon filtering */
		ret = wl1251_acx_beacon_filter_opt(wl, false);
		if (ret < 0)
			return ret;

		ret = wl1251_acx_wake_up_conditions(wl,
						    WAKE_UP_EVENT_DTIM_BITMAP,
						    wl->listen_int);
		if (ret < 0)
			return ret;

		ret = wl1251_cmd_ps_mode(wl, STATION_ACTIVE_MODE);
		if (ret < 0)
			return ret;

		wl->psm = 0;
		break;
	}

	return ret;
}

