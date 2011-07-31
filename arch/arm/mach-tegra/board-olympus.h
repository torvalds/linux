/*
 * arch/arm/mach-tegra/board-olympus.h
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

#ifndef _MACH_TEGRA_BOARD_OLYMPUS_H
#define _MACH_TEGRA_BOARD_OLYMPUS_H

void olympus_pinmux_init(void);
int olympus_keypad_init(void);
void olympus_i2c_init(void);
int olympus_panel_init(void);
int olympus_wlan_init(void);

#endif
