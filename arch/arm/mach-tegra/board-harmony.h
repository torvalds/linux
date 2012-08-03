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

#include <mach/gpio-tegra.h>

#define HARMONY_GPIO_TPS6586X(_x_)	(TEGRA_NR_GPIOS + (_x_))

#define TEGRA_GPIO_EN_VDD_1V05_GPIO	HARMONY_GPIO_TPS6586X(2)

int harmony_regulator_init(void);

#endif
