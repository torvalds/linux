/*
 * Wireless USB Host Controller
 * UWB Protocol Adaptation Layer (PAL) glue.
 *
 * Copyright (C) 2008 Cambridge Silicon Radio Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "wusbhc.h"

static void wusbhc_channel_changed(struct uwb_pal *pal, int channel)
{
	struct wusbhc *wusbhc = container_of(pal, struct wusbhc, pal);

	if (channel < 0)
		wusbhc_stop(wusbhc);
	else
		wusbhc_start(wusbhc);
}

/**
 * wusbhc_pal_register - register the WUSB HC as a UWB PAL
 * @wusbhc: the WUSB HC
 */
int wusbhc_pal_register(struct wusbhc *wusbhc)
{
	uwb_pal_init(&wusbhc->pal);

	wusbhc->pal.name   = "wusbhc";
	wusbhc->pal.device = wusbhc->usb_hcd.self.controller;
	wusbhc->pal.rc     = wusbhc->uwb_rc;
	wusbhc->pal.channel_changed = wusbhc_channel_changed;

	return uwb_pal_register(&wusbhc->pal);
}

/**
 * wusbhc_pal_unregister - unregister the WUSB HC as a UWB PAL
 * @wusbhc: the WUSB HC
 */
void wusbhc_pal_unregister(struct wusbhc *wusbhc)
{
	if (wusbhc->uwb_rc)
		uwb_pal_unregister(&wusbhc->pal);
}
