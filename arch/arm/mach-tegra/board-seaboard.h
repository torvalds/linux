/*
 * arch/arm/mach-tegra/board-seaboard.h
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

#ifndef _MACH_TEGRA_BOARD_SEABOARD_H
#define _MACH_TEGRA_BOARD_SEABOARD_H

#define TEGRA_GPIO_SD2_CD		TEGRA_GPIO_PI5
#define TEGRA_GPIO_SD2_WP		TEGRA_GPIO_PH1
#define TEGRA_GPIO_SD2_POWER		TEGRA_GPIO_PI6
#define TEGRA_GPIO_LIDSWITCH		TEGRA_GPIO_PC7
#define TEGRA_GPIO_USB1			TEGRA_GPIO_PD0
#define TEGRA_GPIO_POWERKEY		TEGRA_GPIO_PV2
#define TEGRA_GPIO_BACKLIGHT		TEGRA_GPIO_PD4
#define TEGRA_GPIO_LVDS_SHUTDOWN	TEGRA_GPIO_PB2
#define TEGRA_GPIO_BACKLIGHT_PWM	TEGRA_GPIO_PU5
#define TEGRA_GPIO_BACKLIGHT_VDD	TEGRA_GPIO_PW0
#define TEGRA_GPIO_EN_VDD_PNL		TEGRA_GPIO_PC6
#define TEGRA_GPIO_MAGNETOMETER		TEGRA_GPIO_PN5
#define TEGRA_GPIO_ISL29018_IRQ		TEGRA_GPIO_PZ2
#define TEGRA_GPIO_AC_ONLINE		TEGRA_GPIO_PV3

#define TPS_GPIO_BASE			TEGRA_NR_GPIOS

#define TPS_GPIO_WWAN_PWR		(TPS_GPIO_BASE + 2)

void seaboard_pinmux_init(void);

#endif
