/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
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
 */

#include <linux/platform_device.h>
#include "tsens.h"

/* eeprom layout data for 8974 */
#define BASE1_MASK		0xff
#define S0_P1_MASK		0x3f00
#define S1_P1_MASK		0xfc000
#define S2_P1_MASK		0x3f00000
#define S3_P1_MASK		0xfc000000
#define S4_P1_MASK		0x3f
#define S5_P1_MASK		0xfc0
#define S6_P1_MASK		0x3f000
#define S7_P1_MASK		0xfc0000
#define S8_P1_MASK		0x3f000000
#define S8_P1_MASK_BKP		0x3f
#define S9_P1_MASK		0x3f
#define S9_P1_MASK_BKP		0xfc0
#define S10_P1_MASK		0xfc0
#define S10_P1_MASK_BKP		0x3f000
#define CAL_SEL_0_1		0xc0000000
#define CAL_SEL_2		0x40000000
#define CAL_SEL_SHIFT		30
#define CAL_SEL_SHIFT_2		28

#define S0_P1_SHIFT		8
#define S1_P1_SHIFT		14
#define S2_P1_SHIFT		20
#define S3_P1_SHIFT		26
#define S5_P1_SHIFT		6
#define S6_P1_SHIFT		12
#define S7_P1_SHIFT		18
#define S8_P1_SHIFT		24
#define S9_P1_BKP_SHIFT		6
#define S10_P1_SHIFT		6
#define S10_P1_BKP_SHIFT	12

#define BASE2_SHIFT		12
#define BASE2_BKP_SHIFT		18
#define S0_P2_SHIFT		20
#define S0_P2_BKP_SHIFT		26
#define S1_P2_SHIFT		26
#define S2_P2_BKP_SHIFT		6
#define S3_P2_SHIFT		6
#define S3_P2_BKP_SHIFT		12
#define S4_P2_SHIFT		12
#define S4_P2_BKP_SHIFT		18
#define S5_P2_SHIFT		18
#define S5_P2_BKP_SHIFT		24
#define S6_P2_SHIFT		24
#define S7_P2_BKP_SHIFT		6
#define S8_P2_SHIFT		6
#define S8_P2_BKP_SHIFT		12
#define S9_P2_SHIFT		12
#define S9_P2_BKP_SHIFT		18
#define S10_P2_SHIFT		18
#define S10_P2_BKP_SHIFT	24

#define BASE2_MASK		0xff000
#define BASE2_BKP_MASK		0xfc0000
#define S0_P2_MASK		0x3f00000
#define S0_P2_BKP_MASK		0xfc000000
#define S1_P2_MASK		0xfc000000
#define S1_P2_BKP_MASK		0x3f
#define S2_P2_MASK		0x3f
#define S2_P2_BKP_MASK		0xfc0
#define S3_P2_MASK		0xfc0
#define S3_P2_BKP_MASK		0x3f000
#define S4_P2_MASK		0x3f000
#define S4_P2_BKP_MASK		0xfc0000
#define S5_P2_MASK		0xfc0000
#define S5_P2_BKP_MASK		0x3f000000
#define S6_P2_MASK		0x3f000000
#define S6_P2_BKP_MASK		0x3f
#define S7_P2_MASK		0x3f
#define S7_P2_BKP_MASK		0xfc0
#define S8_P2_MASK		0xfc0
#define S8_P2_BKP_MASK		0x3f000
#define S9_P2_MASK		0x3f000
#define S9_P2_BKP_MASK		0xfc0000
#define S10_P2_MASK		0xfc0000
#define S10_P2_BKP_MASK		0x3f000000

#define BKP_SEL			0x3
#define BKP_REDUN_SEL		0xe0000000
#define BKP_REDUN_SHIFT		29

#define BIT_APPEND		0x3

static int calibrate_8974(struct tsens_device *tmdev)
{
	int base1 = 0, base2 = 0, i;
	u32 p1[11], p2[11];
	int mode = 0;
	u32 *calib, *bkp;
	u32 calib_redun_sel;

	calib = (u32 *)qfprom_read(tmdev->dev, "calib");
	if (IS_ERR(calib))
		return PTR_ERR(calib);

	bkp = (u32 *)qfprom_read(tmdev->dev, "calib_backup");
	if (IS_ERR(bkp))
		return PTR_ERR(bkp);

	calib_redun_sel =  bkp[1] & BKP_REDUN_SEL;
	calib_redun_sel >>= BKP_REDUN_SHIFT;

	if (calib_redun_sel == BKP_SEL) {
		mode = (calib[4] & CAL_SEL_0_1) >> CAL_SEL_SHIFT;
		mode |= (calib[5] & CAL_SEL_2) >> CAL_SEL_SHIFT_2;

		switch (mode) {
		case TWO_PT_CALIB:
			base2 = (bkp[2] & BASE2_BKP_MASK) >> BASE2_BKP_SHIFT;
			p2[0] = (bkp[2] & S0_P2_BKP_MASK) >> S0_P2_BKP_SHIFT;
			p2[1] = (bkp[3] & S1_P2_BKP_MASK);
			p2[2] = (bkp[3] & S2_P2_BKP_MASK) >> S2_P2_BKP_SHIFT;
			p2[3] = (bkp[3] & S3_P2_BKP_MASK) >> S3_P2_BKP_SHIFT;
			p2[4] = (bkp[3] & S4_P2_BKP_MASK) >> S4_P2_BKP_SHIFT;
			p2[5] = (calib[4] & S5_P2_BKP_MASK) >> S5_P2_BKP_SHIFT;
			p2[6] = (calib[5] & S6_P2_BKP_MASK);
			p2[7] = (calib[5] & S7_P2_BKP_MASK) >> S7_P2_BKP_SHIFT;
			p2[8] = (calib[5] & S8_P2_BKP_MASK) >> S8_P2_BKP_SHIFT;
			p2[9] = (calib[5] & S9_P2_BKP_MASK) >> S9_P2_BKP_SHIFT;
			p2[10] = (calib[5] & S10_P2_BKP_MASK) >> S10_P2_BKP_SHIFT;
			/* Fall through */
		case ONE_PT_CALIB:
		case ONE_PT_CALIB2:
			base1 = bkp[0] & BASE1_MASK;
			p1[0] = (bkp[0] & S0_P1_MASK) >> S0_P1_SHIFT;
			p1[1] = (bkp[0] & S1_P1_MASK) >> S1_P1_SHIFT;
			p1[2] = (bkp[0] & S2_P1_MASK) >> S2_P1_SHIFT;
			p1[3] = (bkp[0] & S3_P1_MASK) >> S3_P1_SHIFT;
			p1[4] = (bkp[1] & S4_P1_MASK);
			p1[5] = (bkp[1] & S5_P1_MASK) >> S5_P1_SHIFT;
			p1[6] = (bkp[1] & S6_P1_MASK) >> S6_P1_SHIFT;
			p1[7] = (bkp[1] & S7_P1_MASK) >> S7_P1_SHIFT;
			p1[8] = (bkp[2] & S8_P1_MASK_BKP) >> S8_P1_SHIFT;
			p1[9] = (bkp[2] & S9_P1_MASK_BKP) >> S9_P1_BKP_SHIFT;
			p1[10] = (bkp[2] & S10_P1_MASK_BKP) >> S10_P1_BKP_SHIFT;
			break;
		}
	} else {
		mode = (calib[1] & CAL_SEL_0_1) >> CAL_SEL_SHIFT;
		mode |= (calib[3] & CAL_SEL_2) >> CAL_SEL_SHIFT_2;

		switch (mode) {
		case TWO_PT_CALIB:
			base2 = (calib[2] & BASE2_MASK) >> BASE2_SHIFT;
			p2[0] = (calib[2] & S0_P2_MASK) >> S0_P2_SHIFT;
			p2[1] = (calib[2] & S1_P2_MASK) >> S1_P2_SHIFT;
			p2[2] = (calib[3] & S2_P2_MASK);
			p2[3] = (calib[3] & S3_P2_MASK) >> S3_P2_SHIFT;
			p2[4] = (calib[3] & S4_P2_MASK) >> S4_P2_SHIFT;
			p2[5] = (calib[3] & S5_P2_MASK) >> S5_P2_SHIFT;
			p2[6] = (calib[3] & S6_P2_MASK) >> S6_P2_SHIFT;
			p2[7] = (calib[4] & S7_P2_MASK);
			p2[8] = (calib[4] & S8_P2_MASK) >> S8_P2_SHIFT;
			p2[9] = (calib[4] & S9_P2_MASK) >> S9_P2_SHIFT;
			p2[10] = (calib[4] & S10_P2_MASK) >> S10_P2_SHIFT;
			/* Fall through */
		case ONE_PT_CALIB:
		case ONE_PT_CALIB2:
			base1 = calib[0] & BASE1_MASK;
			p1[0] = (calib[0] & S0_P1_MASK) >> S0_P1_SHIFT;
			p1[1] = (calib[0] & S1_P1_MASK) >> S1_P1_SHIFT;
			p1[2] = (calib[0] & S2_P1_MASK) >> S2_P1_SHIFT;
			p1[3] = (calib[0] & S3_P1_MASK) >> S3_P1_SHIFT;
			p1[4] = (calib[1] & S4_P1_MASK);
			p1[5] = (calib[1] & S5_P1_MASK) >> S5_P1_SHIFT;
			p1[6] = (calib[1] & S6_P1_MASK) >> S6_P1_SHIFT;
			p1[7] = (calib[1] & S7_P1_MASK) >> S7_P1_SHIFT;
			p1[8] = (calib[1] & S8_P1_MASK) >> S8_P1_SHIFT;
			p1[9] = (calib[2] & S9_P1_MASK);
			p1[10] = (calib[2] & S10_P1_MASK) >> S10_P1_SHIFT;
			break;
		}
	}

	switch (mode) {
	case ONE_PT_CALIB:
		for (i = 0; i < tmdev->num_sensors; i++)
			p1[i] += (base1 << 2) | BIT_APPEND;
		break;
	case TWO_PT_CALIB:
		for (i = 0; i < tmdev->num_sensors; i++) {
			p2[i] += base2;
			p2[i] <<= 2;
			p2[i] |= BIT_APPEND;
		}
		/* Fall through */
	case ONE_PT_CALIB2:
		for (i = 0; i < tmdev->num_sensors; i++) {
			p1[i] += base1;
			p1[i] <<= 2;
			p1[i] |= BIT_APPEND;
		}
		break;
	default:
		for (i = 0; i < tmdev->num_sensors; i++)
			p2[i] = 780;
		p1[0] = 502;
		p1[1] = 509;
		p1[2] = 503;
		p1[3] = 509;
		p1[4] = 505;
		p1[5] = 509;
		p1[6] = 507;
		p1[7] = 510;
		p1[8] = 508;
		p1[9] = 509;
		p1[10] = 508;
		break;
	}

	compute_intercept_slope(tmdev, p1, p2, mode);

	return 0;
}

const struct tsens_ops ops_8974 = {
	.init		= init_common,
	.calibrate	= calibrate_8974,
	.get_temp	= get_temp_common,
};

const struct tsens_data data_8974 = {
	.num_sensors	= 11,
	.ops		= &ops_8974,
};
