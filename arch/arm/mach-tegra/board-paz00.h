/*
 * arch/arm/mach-tegra/board-paz00.h
 *
 * Copyright (C) 2010 Marc Dietrich <marvin24@gmx.de>
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

#ifndef _MACH_TEGRA_BOARD_PAZ00_H
#define _MACH_TEGRA_BOARD_PAZ00_H

#define TEGRA_GPIO_SD1_CD		TEGRA_GPIO_PV5
#define TEGRA_GPIO_SD1_WP		TEGRA_GPIO_PH1
#define TEGRA_GPIO_SD1_POWER		TEGRA_GPIO_PT3
#define TEGRA_ULPI_RST			TEGRA_GPIO_PV0

void paz00_pinmux_init(void);

#endif
