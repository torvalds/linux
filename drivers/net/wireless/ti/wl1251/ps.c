// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of wl1251
 *
 * Copyright (C) 2008 Nokia Corporation
 */

#include "reg.h"
#include "ps.h"
#include "cmd.h"
#include "io.h"

/* in ms */
#define WL1251_WAKEUP_TIMEOUT 100

void wl1251_elp_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wl1251 *wl;

	dwork = to_delayed_work(work);
	wl = container_of(dwork, struct wl1251, elp_work);

	wl1251_debug(DEBUG_PSM, "elp work");

	mutex_lock(&wl->mutex);

	if (wl->elp || wl->station_mode == STATION_ACTIVE_MODE)
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

	if (wl->station_mode != STATION_ACTIVE_MODE) {
		delay = msecs_to_jiffies(ELP_ENTRY_DELAY);
		ieee80211_queue_delayed_work(wl->hw, &wl->elp_work, delay);
	}
}

int wl1251_ps_elp_wakeup(struct wl1251 *wl)
{
	unsigned long timeout, start;
	u32 elp_reg;

	cancel_delayed_work(&wl->elp_work);

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

int wl1251_ps_set_mode(struct wl1251 *wl, enum wl1251_station_mode mode)
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

		ret = wl1251_acx_bet_enable(wl, WL1251_ACX_BET_ENABLE,
					    WL1251_DEFAULT_BET_CONSECUTIVE);
		if (ret < 0)
			return ret;

		ret = wl1251_cmd_ps_mode(wl, CHIP_POWER_SAVE_MODE);
		if (ret < 0)
			return ret;

		ret = wl1251_acx_sleep_auth(wl, WL1251_PSM_ELP);
		if (ret < 0)
			return ret;
		break;
	case STATION_IDLE:
		wl1251_debug(DEBUG_PSM, "entering idle");

		ret = wl1251_acx_sleep_auth(wl, WL1251_PSM_ELP);
		if (ret < 0)
			return ret;

		ret = wl1251_cmd_template_set(wl, CMD_DISCONNECT, NULL, 0);
		if (ret < 0)
			return ret;
		break;
	case STATION_ACTIVE_MODE:
	default:
		wl1251_debug(DEBUG_PSM, "leaving psm");

		ret = wl1251_acx_sleep_auth(wl, WL1251_PSM_CAM);
		if (ret < 0)
			return ret;

		/* disable BET */
		ret = wl1251_acx_bet_enable(wl, WL1251_ACX_BET_DISABLE,
					    WL1251_DEFAULT_BET_CONSECUTIVE);
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

		ret = wl1251_cmd_ps_mode(wl, CHIP_ACTIVE_MODE);
		if (ret < 0)
			return ret;

		break;
	}
	wl->station_mode = mode;

	return ret;
}

