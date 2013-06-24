/*
 * exynos_tmu_data.c - Samsung EXYNOS tmu data file
 *
 *  Copyright (C) 2013 Samsung Electronics
 *  Amit Daniel Kachhap <amit.daniel@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "exynos_thermal_common.h"
#include "exynos_tmu.h"
#include "exynos_tmu_data.h"

#if defined(CONFIG_CPU_EXYNOS4210)
struct exynos_tmu_platform_data const exynos4210_default_tmu_data = {
	.threshold = 80,
	.trigger_levels[0] = 5,
	.trigger_levels[1] = 20,
	.trigger_levels[2] = 30,
	.trigger_enable[0] = true,
	.trigger_enable[1] = true,
	.trigger_enable[2] = true,
	.trigger_enable[3] = false,
	.trigger_type[0] = THROTTLE_ACTIVE,
	.trigger_type[1] = THROTTLE_ACTIVE,
	.trigger_type[2] = SW_TRIP,
	.max_trigger_level = 4,
	.gain = 15,
	.reference_voltage = 7,
	.cal_type = TYPE_ONE_POINT_TRIMMING,
	.min_efuse_value = 40,
	.max_efuse_value = 100,
	.first_point_trim = 25,
	.second_point_trim = 85,
	.default_temp_offset = 50,
	.freq_tab[0] = {
		.freq_clip_max = 800 * 1000,
		.temp_level = 85,
	},
	.freq_tab[1] = {
		.freq_clip_max = 200 * 1000,
		.temp_level = 100,
	},
	.freq_tab_count = 2,
	.type = SOC_ARCH_EXYNOS4210,
};
#endif

#if defined(CONFIG_SOC_EXYNOS5250) || defined(CONFIG_SOC_EXYNOS4412)
struct exynos_tmu_platform_data const exynos5250_default_tmu_data = {
	.threshold_falling = 10,
	.trigger_levels[0] = 85,
	.trigger_levels[1] = 103,
	.trigger_levels[2] = 110,
	.trigger_enable[0] = true,
	.trigger_enable[1] = true,
	.trigger_enable[2] = true,
	.trigger_enable[3] = false,
	.trigger_type[0] = THROTTLE_ACTIVE,
	.trigger_type[1] = THROTTLE_ACTIVE,
	.trigger_type[2] = SW_TRIP,
	.max_trigger_level = 4,
	.gain = 8,
	.reference_voltage = 16,
	.noise_cancel_mode = 4,
	.cal_type = TYPE_ONE_POINT_TRIMMING,
	.efuse_value = 55,
	.min_efuse_value = 40,
	.max_efuse_value = 100,
	.first_point_trim = 25,
	.second_point_trim = 85,
	.default_temp_offset = 50,
	.freq_tab[0] = {
		.freq_clip_max = 800 * 1000,
		.temp_level = 85,
	},
	.freq_tab[1] = {
		.freq_clip_max = 200 * 1000,
		.temp_level = 103,
	},
	.freq_tab_count = 2,
	.type = SOC_ARCH_EXYNOS,
};
#endif
