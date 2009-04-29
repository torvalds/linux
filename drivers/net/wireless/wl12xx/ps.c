/*
 * This file is part of wl12xx
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

#include "reg.h"
#include "ps.h"
#include "spi.h"

#define WL12XX_WAKEUP_TIMEOUT 2000

/* Routines to toggle sleep mode while in ELP */
void wl12xx_ps_elp_sleep(struct wl12xx *wl)
{
	if (wl->elp || !wl->psm)
		return;

	wl12xx_debug(DEBUG_PSM, "chip to elp");

	wl12xx_write32(wl, HW_ACCESS_ELP_CTRL_REG_ADDR, ELPCTRL_SLEEP);

	wl->elp = true;
}

int wl12xx_ps_elp_wakeup(struct wl12xx *wl)
{
	unsigned long timeout;
	u32 elp_reg;

	if (!wl->elp)
		return 0;

	wl12xx_debug(DEBUG_PSM, "waking up chip from elp");

	timeout = jiffies + msecs_to_jiffies(WL12XX_WAKEUP_TIMEOUT);

	wl12xx_write32(wl, HW_ACCESS_ELP_CTRL_REG_ADDR, ELPCTRL_WAKE_UP);

	elp_reg = wl12xx_read32(wl, HW_ACCESS_ELP_CTRL_REG_ADDR);

	/*
	 * FIXME: we should wait for irq from chip but, as a temporary
	 * solution to simplify locking, let's poll instead
	 */
	while (!(elp_reg & ELPCTRL_WLAN_READY)) {
		if (time_after(jiffies, timeout)) {
			wl12xx_error("elp wakeup timeout");
			return -ETIMEDOUT;
		}
		msleep(1);
		elp_reg = wl12xx_read32(wl, HW_ACCESS_ELP_CTRL_REG_ADDR);
	}

	wl12xx_debug(DEBUG_PSM, "wakeup time: %u ms",
		     jiffies_to_msecs(jiffies) -
		     (jiffies_to_msecs(timeout) - WL12XX_WAKEUP_TIMEOUT));

	wl->elp = false;

	return 0;
}

static int wl12xx_ps_set_elp(struct wl12xx *wl, bool enable)
{
	int ret;

	if (enable) {
		wl12xx_debug(DEBUG_PSM, "sleep auth psm/elp");

		/*
		 * FIXME: we should PSM_ELP, but because of firmware wakeup
		 * problems let's use only PSM_PS
		 */
		ret = wl12xx_acx_sleep_auth(wl, WL12XX_PSM_PS);
		if (ret < 0)
			return ret;

		wl12xx_ps_elp_sleep(wl);
	} else {
		wl12xx_debug(DEBUG_PSM, "sleep auth cam");

		/*
		 * When the target is in ELP, we can only
		 * access the ELP control register. Thus,
		 * we have to wake the target up before
		 * changing the power authorization.
		 */

		wl12xx_ps_elp_wakeup(wl);

		ret = wl12xx_acx_sleep_auth(wl, WL12XX_PSM_CAM);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int wl12xx_ps_set_mode(struct wl12xx *wl, enum acx_ps_mode mode)
{
	int ret;

	switch (mode) {
	case STATION_POWER_SAVE_MODE:
		wl12xx_debug(DEBUG_PSM, "entering psm");
		ret = wl12xx_cmd_ps_mode(wl, STATION_POWER_SAVE_MODE);
		if (ret < 0)
			return ret;

		ret = wl12xx_ps_set_elp(wl, true);
		if (ret < 0)
			return ret;

		wl->psm = 1;
		break;
	case STATION_ACTIVE_MODE:
	default:
		wl12xx_debug(DEBUG_PSM, "leaving psm");
		ret = wl12xx_ps_set_elp(wl, false);
		if (ret < 0)
			return ret;

		ret = wl12xx_cmd_ps_mode(wl, STATION_ACTIVE_MODE);
		if (ret < 0)
			return ret;

		wl->psm = 0;
		break;
	}

	return ret;
}

