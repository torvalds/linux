/*
 * arch/arm/mach-tegra/board-stingray.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _MACH_TEGRA_BOARD_STINGRAY_H
#define _MACH_TEGRA_BOARD_STINGRAY_H

void stingray_pinmux_init(void);
int stingray_panel_init(void);
int stingray_keypad_init(void);
int stingray_i2c_init(void);
int stingray_wlan_init(void);
int stingray_sensors_init(void);
int stingray_touch_init(void);
int stingray_spi_init(void);
int stingray_revision(void);

enum {
	STINGRAY_REVISION_UNKNOWN,
	STINGRAY_REVISION_M1,
	STINGRAY_REVISION_P0,
	STINGRAY_REVISION_P1,
	STINGRAY_REVISION_P2,
};

#endif
