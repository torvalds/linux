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

enum {
	/* best effort/legacy */
	AC_BE = 0,

	/* background */
	AC_BK = 1,

	/* video */
	AC_VI = 2,

	/* voice */
	AC_VO = 3,

	/* broadcast dummy access category */
	AC_BCAST = 4,

	NUM_ACCESS_CATEGORIES = 4
};

/* following are defult values for the IE fields*/
#define CWMIN_BK  15
#define CWMIN_BE  15
#define CWMIN_VI  7
#define CWMIN_VO  3
#define CWMAX_BK  1023
#define CWMAX_BE  63
#define CWMAX_VI  15
#define CWMAX_VO  7

/* slot number setting to start transmission at PIFS interval */
#define AIFS_PIFS 1

/*
 * slot number setting to start transmission at DIFS interval - normal DCF
 * access
 */
#define AIFS_DIFS 2

#define AIFSN_BK  7
#define AIFSN_BE  3
#define AIFSN_VI  AIFS_PIFS
#define AIFSN_VO  AIFS_PIFS
#define TXOP_BK   0
#define TXOP_BE   0
#define TXOP_VI   3008
#define TXOP_VO   1504

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
