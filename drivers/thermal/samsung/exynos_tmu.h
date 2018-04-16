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
#include <dt-bindings/thermal/thermal_exynos.h>

enum soc_type {
	SOC_ARCH_EXYNOS3250 = 1,
	SOC_ARCH_EXYNOS4210,
	SOC_ARCH_EXYNOS4412,
	SOC_ARCH_EXYNOS5250,
	SOC_ARCH_EXYNOS5260,
	SOC_ARCH_EXYNOS5420,
	SOC_ARCH_EXYNOS5420_TRIMINFO,
	SOC_ARCH_EXYNOS5433,
	SOC_ARCH_EXYNOS5440,
	SOC_ARCH_EXYNOS7,
};

/**
 * struct exynos_tmu_platform_data
 * @gain: gain of amplifier in the positive-TC generator block
 *	0 < gain <= 15
 * @reference_voltage: reference voltage of amplifier
 *	in the positive-TC generator block
 *	0 < reference_voltage <= 31
 * @noise_cancel_mode: noise cancellation mode
 *	000, 100, 101, 110 and 111 can be different modes
 * @efuse_value: platform defined fuse value
 * @min_efuse_value: minimum valid trimming data
 * @max_efuse_value: maximum valid trimming data
 * @default_temp_offset: default temperature offset in case of no trimming
 * @cal_type: calibration type for temperature
 *
 * This structure is required for configuration of exynos_tmu driver.
 */
struct exynos_tmu_platform_data {
	u8 gain;
	u8 reference_voltage;
	u8 noise_cancel_mode;

	u32 efuse_value;
	u32 min_efuse_value;
	u32 max_efuse_value;
	u8 first_point_trim;
	u8 second_point_trim;
	u8 default_temp_offset;

	u32 cal_type;
};

#endif /* _EXYNOS_TMU_H */
