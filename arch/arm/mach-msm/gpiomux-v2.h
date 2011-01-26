/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
#ifndef __ARCH_ARM_MACH_MSM_GPIOMUX_V2_H
#define __ARCH_ARM_MACH_MSM_GPIOMUX_V2_H

#define GPIOMUX_NGPIOS 173

typedef u16 gpiomux_config_t;

enum {
	GPIOMUX_DRV_2MA  = 0UL << 6,
	GPIOMUX_DRV_4MA  = 1UL << 6,
	GPIOMUX_DRV_6MA  = 2UL << 6,
	GPIOMUX_DRV_8MA  = 3UL << 6,
	GPIOMUX_DRV_10MA = 4UL << 6,
	GPIOMUX_DRV_12MA = 5UL << 6,
	GPIOMUX_DRV_14MA = 6UL << 6,
	GPIOMUX_DRV_16MA = 7UL << 6,
};

enum {
	GPIOMUX_FUNC_GPIO = 0UL  << 2,
	GPIOMUX_FUNC_1    = 1UL  << 2,
	GPIOMUX_FUNC_2    = 2UL  << 2,
	GPIOMUX_FUNC_3    = 3UL  << 2,
	GPIOMUX_FUNC_4    = 4UL  << 2,
	GPIOMUX_FUNC_5    = 5UL  << 2,
	GPIOMUX_FUNC_6    = 6UL  << 2,
	GPIOMUX_FUNC_7    = 7UL  << 2,
	GPIOMUX_FUNC_8    = 8UL  << 2,
	GPIOMUX_FUNC_9    = 9UL  << 2,
	GPIOMUX_FUNC_A    = 10UL << 2,
	GPIOMUX_FUNC_B    = 11UL << 2,
	GPIOMUX_FUNC_C    = 12UL << 2,
	GPIOMUX_FUNC_D    = 13UL << 2,
	GPIOMUX_FUNC_E    = 14UL << 2,
	GPIOMUX_FUNC_F    = 15UL << 2,
};

enum {
	GPIOMUX_PULL_NONE   = 0UL,
	GPIOMUX_PULL_DOWN   = 1UL,
	GPIOMUX_PULL_KEEPER = 2UL,
	GPIOMUX_PULL_UP     = 3UL,
};

#endif
