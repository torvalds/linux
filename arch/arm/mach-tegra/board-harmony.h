/*
 * arch/arm/mach-tegra/board-harmony.h
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

#ifndef _MACH_TEGRA_BOARD_HARMONY_H
#define _MACH_TEGRA_BOARD_HARMONY_H

#define HARMONY_GPIO_TPS6586X(_x_)	(TEGRA_NR_GPIOS + (_x_))
#define HARMONY_GPIO_WM8903(_x_)	(HARMONY_GPIO_TPS6586X(4) + (_x_))

#define TEGRA_GPIO_SD2_CD		TEGRA_GPIO_PI5
#define TEGRA_GPIO_SD2_WP		TEGRA_GPIO_PH1
#define TEGRA_GPIO_SD2_POWER		TEGRA_GPIO_PT3
#define TEGRA_GPIO_SD4_CD		TEGRA_GPIO_PH2
#define TEGRA_GPIO_SD4_WP		TEGRA_GPIO_PH3
#define TEGRA_GPIO_SD4_POWER		TEGRA_GPIO_PI6
#define TEGRA_GPIO_CDC_IRQ		TEGRA_GPIO_PX3
#define TEGRA_GPIO_SPKR_EN		HARMONY_GPIO_WM8903(2)
#define TEGRA_GPIO_HP_DET		TEGRA_GPIO_PW2
#define TEGRA_GPIO_INT_MIC_EN		TEGRA_GPIO_PX0
#define TEGRA_GPIO_EXT_MIC_EN		TEGRA_GPIO_PX1

void harmony_pinmux_init(void);
int harmony_regulator_init(void);

#endif
