/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009 Nokia Corporation
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

#ifndef __INIT_H__
#define __INIT_H__

#include "wl12xx.h"

int wl1271_hw_init_power_auth(struct wl1271 *wl);
int wl1271_init_templates_config(struct wl1271 *wl);
int wl1271_init_pta(struct wl1271 *wl);
int wl1271_init_energy_detection(struct wl1271 *wl);
int wl1271_chip_specific_init(struct wl1271 *wl);
int wl1271_hw_init(struct wl1271 *wl);
int wl1271_init_vif_specific(struct wl1271 *wl, struct ieee80211_vif *vif);
int wl1271_init_ap_rates(struct wl1271 *wl, struct wl12xx_vif *wlvif);
int wl1271_ap_init_templates(struct wl1271 *wl, struct ieee80211_vif *vif);

#endif
