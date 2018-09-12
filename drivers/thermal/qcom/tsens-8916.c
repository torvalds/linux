// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/platform_device.h>
#include "tsens.h"

/* eeprom layout data for 8916 */
#define BASE0_MASK	0x0000007f
#define BASE1_MASK	0xfe000000
#define BASE0_SHIFT	0
#define BASE1_SHIFT	25

#define S0_P1_MASK	0x00000f80
#define S1_P1_MASK	0x003e0000
#define S2_P1_MASK	0xf8000000
#define S3_P1_MASK	0x000003e0
#define S4_P1_MASK	0x000f8000

#define S0_P2_MASK	0x0001f000
#define S1_P2_MASK	0x07c00000
#define S2_P2_MASK	0x0000001f
#define S3_P2_MASK	0x00007c00
#define S4_P2_MASK	0x01f00000

#define S0_P1_SHIFT	7
#define S1_P1_SHIFT	17
#define S2_P1_SHIFT	27
#define S3_P1_SHIFT	5
#define S4_P1_SHIFT	15

#define S0_P2_SHIFT	12
#define S1_P2_SHIFT	22
#define S2_P2_SHIFT	0
#define S3_P2_SHIFT	10
#define S4_P2_SHIFT	20

#define CAL_SEL_MASK	0xe0000000
#define CAL_SEL_SHIFT	29

static int calibrate_8916(struct tsens_device *tmdev)
{
	int base0 = 0, base1 = 0, i;
	u32 p1[5], p2[5];
	int mode = 0;
	u32 *qfprom_cdata, *qfprom_csel;

	qfprom_cdata = (u32 *)qfprom_read(tmdev->dev, "calib");
	if (IS_ERR(qfprom_cdata))
		return PTR_ERR(qfprom_cdata);

	qfprom_csel = (u32 *)qfprom_read(tmdev->dev, "calib_sel");
	if (IS_ERR(qfprom_csel))
		return PTR_ERR(qfprom_csel);

	mode = (qfprom_csel[0] & CAL_SEL_MASK) >> CAL_SEL_SHIFT;
	dev_dbg(tmdev->dev, "calibration mode is %d\n", mode);

	switch (mode) {
	case TWO_PT_CALIB:
		base1 = (qfprom_cdata[1] & BASE1_MASK) >> BASE1_SHIFT;
		p2[0] = (qfprom_cdata[0] & S0_P2_MASK) >> S0_P2_SHIFT;
		p2[1] = (qfprom_cdata[0] & S1_P2_MASK) >> S1_P2_SHIFT;
		p2[2] = (qfprom_cdata[1] & S2_P2_MASK) >> S2_P2_SHIFT;
		p2[3] = (qfprom_cdata[1] & S3_P2_MASK) >> S3_P2_SHIFT;
		p2[4] = (qfprom_cdata[1] & S4_P2_MASK) >> S4_P2_SHIFT;
		for (i = 0; i < tmdev->num_sensors; i++)
			p2[i] = ((base1 + p2[i]) << 3);
		/* Fall through */
	case ONE_PT_CALIB2:
		base0 = (qfprom_cdata[0] & BASE0_MASK);
		p1[0] = (qfprom_cdata[0] & S0_P1_MASK) >> S0_P1_SHIFT;
		p1[1] = (qfprom_cdata[0] & S1_P1_MASK) >> S1_P1_SHIFT;
		p1[2] = (qfprom_cdata[0] & S2_P1_MASK) >> S2_P1_SHIFT;
		p1[3] = (qfprom_cdata[1] & S3_P1_MASK) >> S3_P1_SHIFT;
		p1[4] = (qfprom_cdata[1] & S4_P1_MASK) >> S4_P1_SHIFT;
		for (i = 0; i < tmdev->num_sensors; i++)
			p1[i] = (((base0) + p1[i]) << 3);
		break;
	default:
		for (i = 0; i < tmdev->num_sensors; i++) {
			p1[i] = 500;
			p2[i] = 780;
		}
		break;
	}

	compute_intercept_slope(tmdev, p1, p2, mode);

	return 0;
}

static const struct tsens_ops ops_8916 = {
	.init		= init_common,
	.calibrate	= calibrate_8916,
	.get_temp	= get_temp_common,
};

const struct tsens_data data_8916 = {
	.num_sensors	= 5,
	.ops		= &ops_8916,
	.hw_ids		= (unsigned int []){0, 1, 2, 4, 5 },
};
