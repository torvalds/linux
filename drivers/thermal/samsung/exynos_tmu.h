/*
 * exynos_tmu.h - Samsung EXYNOS TMU (Thermal Management Unit)
 *
 *  Copyright (C) 2011 Samsung Electronics
 *  Donggeun Kim <dg77.kim@samsung.com>
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
 */

#ifndef _EXYNOS_TMU_H
#define _EXYNOS_TMU_H
#include <linux/cpu_cooling.h>

#include "exynos_thermal_common.h"

enum calibration_type {
	TYPE_ONE_POINT_TRIMMING,
	TYPE_ONE_POINT_TRIMMING_25,
	TYPE_ONE_POINT_TRIMMING_85,
	TYPE_TWO_POINT_TRIMMING,
	TYPE_NONE,
};

enum soc_type {
	SOC_ARCH_EXYNOS3250 = 1,
	SOC_ARCH_EXYNOS4210,
	SOC_ARCH_EXYNOS4412,
	SOC_ARCH_EXYNOS5250,
	SOC_ARCH_EXYNOS5260,
	SOC_ARCH_EXYNOS5420,
	SOC_ARCH_EXYNOS5420_TRIMINFO,
	SOC_ARCH_EXYNOS5440,
};

/**
 * struct exynos_tmu_platform_data
 * @threshold: basic temperature for generating interrupt
 *	       25 <= threshold <= 125 [unit: degree Celsius]
 * @threshold_falling: differntial value for setting threshold
 *		       of temperature falling interrupt.
 * @trigger_levels: array for each interrupt levels
 *	[unit: degree Celsius]
 *	0: temperature for trigger_level0 interrupt
 *	   condition for trigger_level0 interrupt:
 *		current temperature > threshold + trigger_levels[0]
 *	1: temperature for trigger_level1 interrupt
 *	   condition for trigger_level1 interrupt:
 *		current temperature > threshold + trigger_levels[1]
 *	2: temperature for trigger_level2 interrupt
 *	   condition for trigger_level2 interrupt:
 *		current temperature > threshold + trigger_levels[2]
 *	3: temperature for trigger_level3 interrupt
 *	   condition for trigger_level3 interrupt:
 *		current temperature > threshold + trigger_levels[3]
 * @trigger_type: defines the type of trigger. Possible values are,
 *	THROTTLE_ACTIVE trigger type
 *	THROTTLE_PASSIVE trigger type
 *	SW_TRIP trigger type
 *	HW_TRIP
 * @trigger_enable[]: array to denote which trigger levels are enabled.
 *	1 = enable trigger_level[] interrupt,
 *	0 = disable trigger_level[] interrupt
 * @max_trigger_level: max trigger level supported by the TMU
 * @non_hw_trigger_levels: number of defined non-hardware trigger levels
 * @gain: gain of amplifier in the positive-TC generator block
 *	0 < gain <= 15
 * @reference_voltage: reference voltage of amplifier
 *	in the positive-TC generator block
 *	0 < reference_voltage <= 31
 * @noise_cancel_mode: noise cancellation mode
 *	000, 100, 101, 110 and 111 can be different modes
 * @type: determines the type of SOC
 * @efuse_value: platform defined fuse value
 * @min_efuse_value: minimum valid trimming data
 * @max_efuse_value: maximum valid trimming data
 * @first_point_trim: temp value of the first point trimming
 * @second_point_trim: temp value of the second point trimming
 * @default_temp_offset: default temperature offset in case of no trimming
 * @cal_type: calibration type for temperature
 * @freq_clip_table: Table representing frequency reduction percentage.
 * @freq_tab_count: Count of the above table as frequency reduction may
 *	applicable to only some of the trigger levels.
 *
 * This structure is required for configuration of exynos_tmu driver.
 */
struct exynos_tmu_platform_data {
	u8 threshold;
	u8 threshold_falling;
	u8 trigger_levels[MAX_TRIP_COUNT];
	enum trigger_type trigger_type[MAX_TRIP_COUNT];
	bool trigger_enable[MAX_TRIP_COUNT];
	u8 max_trigger_level;
	u8 non_hw_trigger_levels;
	u8 gain;
	u8 reference_voltage;
	u8 noise_cancel_mode;

	u32 efuse_value;
	u32 min_efuse_value;
	u32 max_efuse_value;
	u8 first_point_trim;
	u8 second_point_trim;
	u8 default_temp_offset;

	enum calibration_type cal_type;
	enum soc_type type;
	struct freq_clip_table freq_tab[4];
	unsigned int freq_tab_count;
};

/**
 * struct exynos_tmu_init_data
 * @tmu_count: number of TMU instances.
 * @tmu_data: platform data of all TMU instances.
 * This structure is required to store data for multi-instance exynos tmu
 * driver.
 */
struct exynos_tmu_init_data {
	int tmu_count;
	struct exynos_tmu_platform_data tmu_data[];
};

extern struct exynos_tmu_init_data const exynos3250_default_tmu_data;
extern struct exynos_tmu_init_data const exynos4210_default_tmu_data;
extern struct exynos_tmu_init_data const exynos4412_default_tmu_data;
extern struct exynos_tmu_init_data const exynos5250_default_tmu_data;
extern struct exynos_tmu_init_data const exynos5260_default_tmu_data;
extern struct exynos_tmu_init_data const exynos5420_default_tmu_data;
extern struct exynos_tmu_init_data const exynos5440_default_tmu_data;

#endif /* _EXYNOS_TMU_H */
