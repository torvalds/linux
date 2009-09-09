/*
 * This file is part of wl12xx
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

#ifndef __WL12XX_INIT_H__
#define __WL12XX_INIT_H__

#include "wl12xx.h"

int wl12xx_hw_init_hwenc_config(struct wl12xx *wl);
int wl12xx_hw_init_templates_config(struct wl12xx *wl);
int wl12xx_hw_init_mem_config(struct wl12xx *wl);
int wl12xx_hw_init_rx_config(struct wl12xx *wl, u32 config, u32 filter);
int wl12xx_hw_init_phy_config(struct wl12xx *wl);
int wl12xx_hw_init_beacon_filter(struct wl12xx *wl);
int wl12xx_hw_init_pta(struct wl12xx *wl);
int wl12xx_hw_init_energy_detection(struct wl12xx *wl);
int wl12xx_hw_init_beacon_broadcast(struct wl12xx *wl);
int wl12xx_hw_init_power_auth(struct wl12xx *wl);

#endif
