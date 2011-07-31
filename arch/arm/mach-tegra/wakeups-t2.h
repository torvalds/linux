/*
 * arch/arm/mach-tegra/wakeups-t2.h
 *
 * Declarations of Tegra 2 LP0 wakeup sources
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MACH_TEGRA_WAKEUPS_T2_H
#define __MACH_TEGRA_WAKEUPS_T2_H

#define TEGRA_WAKE_GPIO_PO5	(1 << 0)
#define TEGRA_WAKE_GPIO_PV3	(1 << 1)
#define TEGRA_WAKE_GPIO_PL1	(1 << 2)
#define TEGRA_WAKE_GPIO_PB6	(1 << 3)
#define TEGRA_WAKE_GPIO_PN7	(1 << 4)
#define TEGRA_WAKE_GPIO_PA0	(1 << 5)
#define TEGRA_WAKE_GPIO_PU5	(1 << 6)
#define TEGRA_WAKE_GPIO_PU6	(1 << 7)
#define TEGRA_WAKE_GPIO_PC7	(1 << 8)
#define TEGRA_WAKE_GPIO_PS2	(1 << 9)
#define TEGRA_WAKE_GPIO_PAA1	(1 << 10)
#define TEGRA_WAKE_GPIO_PW3	(1 << 11)
#define TEGRA_WAKE_GPIO_PW2	(1 << 12)
#define TEGRA_WAKE_GPIO_PY6	(1 << 13)
#define TEGRA_WAKE_GPIO_PV6	(1 << 14)
#define TEGRA_WAKE_GPIO_PJ7	(1 << 15)
#define TEGRA_WAKE_RTC_ALARM	(1 << 16)
#define TEGRA_WAKE_KBC_EVENT	(1 << 17)
#define TEGRA_WAKE_PWR_INT	(1 << 18)
#define TEGRA_WAKE_USB1_VBUS	(1 << 19)
#define TEGRA_WAKE_USB3_VBUS	(1 << 20)
#define TEGRA_WAKE_USB1_ID	(1 << 21)
#define TEGRA_WAKE_USB3_ID	(1 << 22)
#define TEGRA_WAKE_GPIO_PI5	(1 << 23)
#define TEGRA_WAKE_GPIO_PV2	(1 << 24)
#define TEGRA_WAKE_GPIO_PS4	(1 << 25)
#define TEGRA_WAKE_GPIO_PS5	(1 << 26)
#define TEGRA_WAKE_GPIO_PS0	(1 << 27)
#define TEGRA_WAKE_GPIO_PQ6	(1 << 28)
#define TEGRA_WAKE_GPIO_PQ7	(1 << 29)
#define TEGRA_WAKE_GPIO_PN2	(1 << 30)

#endif
