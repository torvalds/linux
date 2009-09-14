/*
 * This file is part of wl1251
 *
 * Copyright (C) 2009 Nokia Corporation
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

#ifndef __WL1251_INIT_H__
#define __WL1251_INIT_H__

#include "wl1251.h"

int wl1251_hw_init_hwenc_config(struct wl1251 *wl);
int wl1251_hw_init_templates_config(struct wl1251 *wl);
int wl1251_hw_init_rx_config(struct wl1251 *wl, u32 config, u32 filter);
int wl1251_hw_init_phy_config(struct wl1251 *wl);
int wl1251_hw_init_beacon_filter(struct wl1251 *wl);
int wl1251_hw_init_pta(struct wl1251 *wl);
int wl1251_hw_init_energy_detection(struct wl1251 *wl);
int wl1251_hw_init_beacon_broadcast(struct wl1251 *wl);
int wl1251_hw_init_power_auth(struct wl1251 *wl);
int wl1251_hw_init_mem_config(struct wl1251 *wl);
int wl1251_hw_init(struct wl1251 *wl);

#endif
