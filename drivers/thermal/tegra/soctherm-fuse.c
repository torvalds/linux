/*
 * Copyright (c) 2014-2016, NVIDIA CORPORATION.  All rights reserved.
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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <soc/tegra/fuse.h>

#include "soctherm.h"

#define NOMINAL_CALIB_FT			105
#define NOMINAL_CALIB_CP			25

#define FUSE_TSENSOR_CALIB_CP_TS_BASE_MASK	0x1fff
#define FUSE_TSENSOR_CALIB_FT_TS_BASE_MASK	(0x1fff << 13)
#define FUSE_TSENSOR_CALIB_FT_TS_BASE_SHIFT	13

#define FUSE_TSENSOR_COMMON			0x180

/*
 * Tegra12x, etc:
 * FUSE_TSENSOR_COMMON:
 *    3                   2                   1                   0
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |-----------| SHFT_FT |       BASE_FT       |      BASE_CP      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * FUSE_SPARE_REALIGNMENT_REG:
 *    3                   2                   1                   0
 *  1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |---------------------------------------------------| SHIFT_CP  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

#define CALIB_COEFFICIENT 1000000LL

/**
 * div64_s64_precise() - wrapper for div64_s64()
 * @a:  the dividend
 * @b:  the divisor
 *
 * Implements division with fairly accurate rounding instead of truncation by
 * shifting the dividend to the left by 16 so that the quotient has a
 * much higher precision.
 *
 * Return: the quotient of a / b.
 */
static s64 div64_s64_precise(s64 a, s32 b)
{
	s64 r, al;

	/* Scale up for increased precision division */
	al = a << 16;

	r = div64_s64(al * 2 + 1, 2 * b);
	return r >> 16;
}

int tegra_calc_shared_calib(const struct tegra_soctherm_fuse *tfuse,
			    struct tsensor_shared_calib *shared)
{
	u32 val;
	s32 shifted_cp, shifted_ft;
	int err;

	err = tegra_fuse_readl(FUSE_TSENSOR_COMMON, &val);
	if (err)
		return err;

	shared->base_cp = (val & tfuse->fuse_base_cp_mask) >>
			  tfuse->fuse_base_cp_shift;
	shared->base_ft = (val & tfuse->fuse_base_ft_mask) >>
			  tfuse->fuse_base_ft_shift;

	shifted_ft = (val & tfuse->fuse_shift_ft_mask) >>
		     tfuse->fuse_shift_ft_shift;
	shifted_ft = sign_extend32(shifted_ft, 4);

	if (tfuse->fuse_spare_realignment) {
		err = tegra_fuse_readl(tfuse->fuse_spare_realignment, &val);
		if (err)
			return err;
	}

	shifted_cp = sign_extend32(val, 5);

	shared->actual_temp_cp = 2 * NOMINAL_CALIB_CP + shifted_cp;
	shared->actual_temp_ft = 2 * NOMINAL_CALIB_FT + shifted_ft;

	return 0;
}

int tegra_calc_tsensor_calib(const struct tegra_tsensor *sensor,
			     const struct tsensor_shared_calib *shared,
			     u32 *calibration)
{
	const struct tegra_tsensor_group *sensor_group;
	u32 val, calib;
	s32 actual_tsensor_ft, actual_tsensor_cp;
	s32 delta_sens, delta_temp;
	s32 mult, div;
	s16 therma, thermb;
	s64 temp;
	int err;

	sensor_group = sensor->group;

	err = tegra_fuse_readl(sensor->calib_fuse_offset, &val);
	if (err)
		return err;

	actual_tsensor_cp = (shared->base_cp * 64) + sign_extend32(val, 12);
	val = (val & FUSE_TSENSOR_CALIB_FT_TS_BASE_MASK) >>
	      FUSE_TSENSOR_CALIB_FT_TS_BASE_SHIFT;
	actual_tsensor_ft = (shared->base_ft * 32) + sign_extend32(val, 12);

	delta_sens = actual_tsensor_ft - actual_tsensor_cp;
	delta_temp = shared->actual_temp_ft - shared->actual_temp_cp;

	mult = sensor_group->pdiv * sensor->config->tsample_ate;
	div = sensor->config->tsample * sensor_group->pdiv_ate;

	temp = (s64)delta_temp * (1LL << 13) * mult;
	therma = div64_s64_precise(temp, (s64)delta_sens * div);

	temp = ((s64)actual_tsensor_ft * shared->actual_temp_cp) -
		((s64)actual_tsensor_cp * shared->actual_temp_ft);
	thermb = div64_s64_precise(temp, delta_sens);

	temp = (s64)therma * sensor->fuse_corr_alpha;
	therma = div64_s64_precise(temp, CALIB_COEFFICIENT);

	temp = (s64)thermb * sensor->fuse_corr_alpha + sensor->fuse_corr_beta;
	thermb = div64_s64_precise(temp, CALIB_COEFFICIENT);

	calib = ((u16)therma << SENSOR_CONFIG2_THERMA_SHIFT) |
		((u16)thermb << SENSOR_CONFIG2_THERMB_SHIFT);

	*calibration = calib;

	return 0;
}

MODULE_AUTHOR("Wei Ni <wni@nvidia.com>");
MODULE_DESCRIPTION("Tegra SOCTHERM fuse management");
MODULE_LICENSE("GPL v2");
