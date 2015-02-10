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

struct exynos_tmu_init_data const exynos4210_default_tmu_data = {
	.tmu_data = {
		{
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
		.non_hw_trigger_levels = 3,
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
		},
	},
	.tmu_count = 1,
};

#define EXYNOS3250_TMU_DATA \
	.threshold_falling = 10, \
	.trigger_levels[0] = 70, \
	.trigger_levels[1] = 95, \
	.trigger_levels[2] = 110, \
	.trigger_levels[3] = 120, \
	.trigger_enable[0] = true, \
	.trigger_enable[1] = true, \
	.trigger_enable[2] = true, \
	.trigger_enable[3] = false, \
	.trigger_type[0] = THROTTLE_ACTIVE, \
	.trigger_type[1] = THROTTLE_ACTIVE, \
	.trigger_type[2] = SW_TRIP, \
	.trigger_type[3] = HW_TRIP, \
	.max_trigger_level = 4, \
	.non_hw_trigger_levels = 3, \
	.gain = 8, \
	.reference_voltage = 16, \
	.noise_cancel_mode = 4, \
	.cal_type = TYPE_TWO_POINT_TRIMMING, \
	.efuse_value = 55, \
	.min_efuse_value = 40, \
	.max_efuse_value = 100, \
	.first_point_trim = 25, \
	.second_point_trim = 85, \
	.default_temp_offset = 50, \
	.freq_tab[0] = { \
		.freq_clip_max = 800 * 1000, \
		.temp_level = 70, \
	}, \
	.freq_tab[1] = { \
		.freq_clip_max = 400 * 1000, \
		.temp_level = 95, \
	}, \
	.freq_tab_count = 2

struct exynos_tmu_init_data const exynos3250_default_tmu_data = {
	.tmu_data = {
		{
			EXYNOS3250_TMU_DATA,
			.type = SOC_ARCH_EXYNOS3250,
		},
	},
	.tmu_count = 1,
};

#define EXYNOS4412_TMU_DATA \
	.threshold_falling = 10, \
	.trigger_levels[0] = 70, \
	.trigger_levels[1] = 95, \
	.trigger_levels[2] = 110, \
	.trigger_levels[3] = 120, \
	.trigger_enable[0] = true, \
	.trigger_enable[1] = true, \
	.trigger_enable[2] = true, \
	.trigger_enable[3] = false, \
	.trigger_type[0] = THROTTLE_ACTIVE, \
	.trigger_type[1] = THROTTLE_ACTIVE, \
	.trigger_type[2] = SW_TRIP, \
	.trigger_type[3] = HW_TRIP, \
	.max_trigger_level = 4, \
	.non_hw_trigger_levels = 3, \
	.gain = 8, \
	.reference_voltage = 16, \
	.noise_cancel_mode = 4, \
	.cal_type = TYPE_ONE_POINT_TRIMMING, \
	.efuse_value = 55, \
	.min_efuse_value = 40, \
	.max_efuse_value = 100, \
	.first_point_trim = 25, \
	.second_point_trim = 85, \
	.default_temp_offset = 50, \
	.freq_tab[0] = { \
		.freq_clip_max = 1400 * 1000, \
		.temp_level = 70, \
	}, \
	.freq_tab[1] = { \
		.freq_clip_max = 400 * 1000, \
		.temp_level = 95, \
	}, \
	.freq_tab_count = 2

struct exynos_tmu_init_data const exynos4412_default_tmu_data = {
	.tmu_data = {
		{
			EXYNOS4412_TMU_DATA,
			.type = SOC_ARCH_EXYNOS4412,
		},
	},
	.tmu_count = 1,
};

struct exynos_tmu_init_data const exynos5250_default_tmu_data = {
	.tmu_data = {
		{
			EXYNOS4412_TMU_DATA,
			.type = SOC_ARCH_EXYNOS5250,
		},
	},
	.tmu_count = 1,
};

#define __EXYNOS5260_TMU_DATA	\
	.threshold_falling = 10, \
	.trigger_levels[0] = 85, \
	.trigger_levels[1] = 103, \
	.trigger_levels[2] = 110, \
	.trigger_levels[3] = 120, \
	.trigger_enable[0] = true, \
	.trigger_enable[1] = true, \
	.trigger_enable[2] = true, \
	.trigger_enable[3] = false, \
	.trigger_type[0] = THROTTLE_ACTIVE, \
	.trigger_type[1] = THROTTLE_ACTIVE, \
	.trigger_type[2] = SW_TRIP, \
	.trigger_type[3] = HW_TRIP, \
	.max_trigger_level = 4, \
	.non_hw_trigger_levels = 3, \
	.gain = 8, \
	.reference_voltage = 16, \
	.noise_cancel_mode = 4, \
	.cal_type = TYPE_ONE_POINT_TRIMMING, \
	.efuse_value = 55, \
	.min_efuse_value = 40, \
	.max_efuse_value = 100, \
	.first_point_trim = 25, \
	.second_point_trim = 85, \
	.default_temp_offset = 50, \
	.freq_tab[0] = { \
		.freq_clip_max = 800 * 1000, \
		.temp_level = 85, \
	}, \
	.freq_tab[1] = { \
		.freq_clip_max = 200 * 1000, \
		.temp_level = 103, \
	}, \
	.freq_tab_count = 2, \

#define EXYNOS5260_TMU_DATA \
	__EXYNOS5260_TMU_DATA \
	.type = SOC_ARCH_EXYNOS5260

struct exynos_tmu_init_data const exynos5260_default_tmu_data = {
	.tmu_data = {
		{ EXYNOS5260_TMU_DATA },
		{ EXYNOS5260_TMU_DATA },
		{ EXYNOS5260_TMU_DATA },
		{ EXYNOS5260_TMU_DATA },
		{ EXYNOS5260_TMU_DATA },
	},
	.tmu_count = 5,
};

#define EXYNOS5420_TMU_DATA \
	__EXYNOS5260_TMU_DATA \
	.type = SOC_ARCH_EXYNOS5420

#define EXYNOS5420_TMU_DATA_SHARED \
	__EXYNOS5260_TMU_DATA \
	.type = SOC_ARCH_EXYNOS5420_TRIMINFO

struct exynos_tmu_init_data const exynos5420_default_tmu_data = {
	.tmu_data = {
		{ EXYNOS5420_TMU_DATA },
		{ EXYNOS5420_TMU_DATA },
		{ EXYNOS5420_TMU_DATA_SHARED },
		{ EXYNOS5420_TMU_DATA_SHARED },
		{ EXYNOS5420_TMU_DATA_SHARED },
	},
	.tmu_count = 5,
};

#define EXYNOS5440_TMU_DATA \
	.trigger_levels[0] = 100, \
	.trigger_levels[4] = 105, \
	.trigger_enable[0] = 1, \
	.trigger_type[0] = SW_TRIP, \
	.trigger_type[4] = HW_TRIP, \
	.max_trigger_level = 5, \
	.non_hw_trigger_levels = 1, \
	.gain = 5, \
	.reference_voltage = 16, \
	.noise_cancel_mode = 4, \
	.cal_type = TYPE_ONE_POINT_TRIMMING, \
	.efuse_value = 0x5b2d, \
	.min_efuse_value = 16, \
	.max_efuse_value = 76, \
	.first_point_trim = 25, \
	.second_point_trim = 70, \
	.default_temp_offset = 25, \
	.type = SOC_ARCH_EXYNOS5440

struct exynos_tmu_init_data const exynos5440_default_tmu_data = {
	.tmu_data = {
		{ EXYNOS5440_TMU_DATA } ,
		{ EXYNOS5440_TMU_DATA } ,
		{ EXYNOS5440_TMU_DATA } ,
	},
	.tmu_count = 3,
};
