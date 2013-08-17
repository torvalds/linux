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
#ifndef __ARCH_ARM_MACH_MSM_GPIOMUX_V1_H
#define __ARCH_ARM_MACH_MSM_GPIOMUX_V1_H

#if defined(CONFIG_ARCH_MSM7X30)
#define GPIOMUX_NGPIOS 182
#elif defined(CONFIG_ARCH_QSD8X50)
#define GPIOMUX_NGPIOS 165
#else
#define GPIOMUX_NGPIOS 133
#endif

typedef u32 gpiomux_config_t;

enum {
	GPIOMUX_DRV_2MA  = 0UL << 17,
	GPIOMUX_DRV_4MA  = 1UL << 17,
	GPIOMUX_DRV_6MA  = 2UL << 17,
	GPIOMUX_DRV_8MA  = 3UL << 17,
	GPIOMUX_DRV_10MA = 4UL << 17,
	GPIOMUX_DRV_12MA = 5UL << 17,
	GPIOMUX_DRV_14MA = 6UL << 17,
	GPIOMUX_DRV_16MA = 7UL << 17,
};

enum {
	GPIOMUX_FUNC_GPIO = 0UL,
	GPIOMUX_FUNC_1    = 1UL,
	GPIOMUX_FUNC_2    = 2UL,
	GPIOMUX_FUNC_3    = 3UL,
	GPIOMUX_FUNC_4    = 4UL,
	GPIOMUX_FUNC_5    = 5UL,
	GPIOMUX_FUNC_6    = 6UL,
	GPIOMUX_FUNC_7    = 7UL,
	GPIOMUX_FUNC_8    = 8UL,
	GPIOMUX_FUNC_9    = 9UL,
	GPIOMUX_FUNC_A    = 10UL,
	GPIOMUX_FUNC_B    = 11UL,
	GPIOMUX_FUNC_C    = 12UL,
	GPIOMUX_FUNC_D    = 13UL,
	GPIOMUX_FUNC_E    = 14UL,
	GPIOMUX_FUNC_F    = 15UL,
};

enum {
	GPIOMUX_PULL_NONE   = 0UL << 15,
	GPIOMUX_PULL_DOWN   = 1UL << 15,
	GPIOMUX_PULL_KEEPER = 2UL << 15,
	GPIOMUX_PULL_UP     = 3UL << 15,
};

#endif
