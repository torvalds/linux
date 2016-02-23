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

#ifndef __PS_H__
#define __PS_H__

#include "wlcore.h"
#include "acx.h"

int wl1271_ps_set_mode(struct wl1271 *wl, struct wl12xx_vif *wlvif,
		       enum wl1271_cmd_ps_mode mode);
void wl1271_ps_elp_sleep(struct wl1271 *wl);
int wl1271_ps_elp_wakeup(struct wl1271 *wl);
void wl1271_elp_work(struct work_struct *work);
void wl12xx_ps_link_start(struct wl1271 *wl, struct wl12xx_vif *wlvif,
			  u8 hlid, bool clean_queues);
void wl12xx_ps_link_end(struct wl1271 *wl, struct wl12xx_vif *wlvif, u8 hlid);

#define WL1271_PS_COMPLETE_TIMEOUT 500

#endif /* __WL1271_PS_H__ */
