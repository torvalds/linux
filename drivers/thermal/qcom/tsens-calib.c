// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include "tsens2xxx.h"

/* eeprom layout data for 8937 */
#define BASE0_MASK_8937				0x000000ff
#define BASE1_MASK_8937				0xff000000
#define BASE1_SHIFT_8937			24

#define S0_P1_MASK_8937				0x000001f8
#define S1_P1_MASK_8937				0x001f8000
#define S2_P1_MASK_0_4_8937			0xf8000000
#define S2_P1_MASK_5_8937			0x00000001
#define S3_P1_MASK_8937				0x00001f80
#define S4_P1_MASK_8937				0x01f80000
#define S5_P1_MASK_8937				0x00003f00
#define S6_P1_MASK_8937				0x03f00000
#define S7_P1_MASK_8937				0x0000003f
#define S8_P1_MASK_8937				0x0003f000
#define S9_P1_MASK_8937				0x0000003f
#define S10_P1_MASK_8937			0x0003f000

#define S0_P2_MASK_8937				0x00007e00
#define S1_P2_MASK_8937				0x07e00000
#define S2_P2_MASK_8937				0x0000007e
#define S3_P2_MASK_8937				0x0007e000
#define S4_P2_MASK_8937				0x7e000000
#define S5_P2_MASK_8937				0x000fc000
#define S6_P2_MASK_8937				0xfc000000
#define S7_P2_MASK_8937				0x00000fc0
#define S8_P2_MASK_8937				0x00fc0000
#define S9_P2_MASK_8937				0x00000fc0
#define S10_P2_MASK_8937			0x00fc0000

#define S0_P1_SHIFT_8937			3
#define S1_P1_SHIFT_8937			15
#define S2_P1_SHIFT_0_4_8937			27
#define S2_P1_SHIFT_5_8937			5
#define S3_P1_SHIFT_8937			7
#define S4_P1_SHIFT_8937			19
#define S5_P1_SHIFT_8937			8
#define S6_P1_SHIFT_8937			20
#define S8_P1_SHIFT_8937			12
#define S10_P1_SHIFT_8937			12

#define S0_P2_SHIFT_8937			9
#define S1_P2_SHIFT_8937			21
#define S2_P2_SHIFT_8937			1
#define S3_P2_SHIFT_8937			13
#define S4_P2_SHIFT_8937			25
#define S5_P2_SHIFT_8937			14
#define S6_P2_SHIFT_8937			26
#define S7_P2_SHIFT_8937			6
#define S8_P2_SHIFT_8937			18
#define S9_P2_SHIFT_8937			6
#define S10_P2_SHIFT_8937			18

#define CAL_SEL_MASK_8937			0x00000007

/* eeprom layout data for qcs405 */
#define BASE0_MASK_405				0x000007F8
#define BASE1_MASK_405				0x0007F800
#define BASE0_SHIFT_405				0x3
#define BASE1_SHIFT_405				0xB

#define S0_P1_MASK_405				0x0000003F
#define S1_P1_MASK_405				0x0003F000
#define S2_P1_MASK_405				0x3F000000
#define S3_P1_MASK_405				0x000003F0
#define S4_P1_MASK_405				0x003F0000
#define S5_P1_MASK_405				0x0000003F
#define S6_P1_MASK_405				0x0003F000
#define S7_P1_MASK_405				0x3F000000
#define S8_P1_MASK_405				0x000003F0
#define S9_P1_MASK_405				0x003F0000

#define S0_P2_MASK_405				0x00000FC0
#define S1_P2_MASK_405				0x00FC0000
#define S2_P2_MASK_0_1_405			0xC0000000
#define S2_P2_MASK_2_5_405			0x0000000F
#define S3_P2_MASK_405				0x0000FC00
#define S4_P2_MASK_405				0x0FC00000
#define S5_P2_MASK_405				0x00000FC0
#define S6_P2_MASK_405				0x00FC0000
#define S7_P2_MASK_0_1_405			0xC0000000
#define S7_P2_MASK_2_5_405			0x0000000F
#define S8_P2_MASK_405				0x0000FC00
#define S9_P2_MASK_405				0x0FC00000

#define S0_P1_SHIFT_405				0x0
#define S1_P1_SHIFT_405				0xC
#define S2_P1_SHIFT_405				0x18
#define S3_P1_SHIFT_405				0x4
#define S4_P1_SHIFT_405				0x10
#define S5_P1_SHIFT_405				0x0
#define S6_P1_SHIFT_405				0xC
#define S7_P1_SHIFT_405				0x18
#define S8_P1_SHIFT_405				0x4
#define S9_P1_SHIFT_405				0x10

#define S0_P2_SHIFT_405				0x6
#define S1_P2_SHIFT_405				0x12
#define S2_P2_SHIFT_0_1_405			0x1E
#define S2_P2_SHIFT_2_5_405			0x2
#define S3_P2_SHIFT_405				0xA
#define S4_P2_SHIFT_405				0x16
#define S5_P2_SHIFT_405				0x6
#define S6_P2_SHIFT_405				0x12
#define S7_P2_SHIFT_0_1_405			0x1E
#define S7_P2_SHIFT_2_5_405			0x2
#define S8_P2_SHIFT_405				0xA
#define S9_P2_SHIFT_405				0x16

#define CAL_SEL_MASK_405			0x7

/* eeprom layout data for 9607 */
#define BASE0_MASK_9607				0x000000ff
#define BASE1_MASK_9607				0x000ff000
#define BASE1_SHIFT_9607			12

#define S0_P1_MASK_9607				0x00003f00
#define S1_P1_MASK_9607				0x03f00000
#define S2_P1_MASK_9607				0x0000003f
#define S3_P1_MASK_9607				0x0003f000
#define S4_P1_MASK_9607				0x0000003f

#define S0_P2_MASK_9607				0x000fc000
#define S1_P2_MASK_9607				0xfc000000
#define S2_P2_MASK_9607				0x00000fc0
#define S3_P2_MASK_9607				0x00fc0000
#define S4_P2_MASK_9607				0x00000fc0

#define S0_P1_SHIFT_9607			8
#define S1_P1_SHIFT_9607			20
#define S3_P1_SHIFT_9607			12

#define S0_P2_SHIFT_9607			14
#define S1_P2_SHIFT_9607			26
#define S2_P2_SHIFT_9607			6
#define S3_P2_SHIFT_9607			18
#define S4_P2_SHIFT_9607			6

#define CAL_SEL_MASK_9607			0x00700000
#define CAL_SEL_SHIFT_9607			20


#define CAL_DEGC_PT1				30
#define CAL_DEGC_PT2				120

/*
 * Use this function on devices where slope and offset calculations
 * depend on calibration data read from qfprom. On others the slope
 * and offset values are derived from tz->tzp->slope and tz->tzp->offset
 * resp.
 */
static void compute_intercept_slope(struct tsens_device *tmdev, u32 *p1,
	u32 *p2, u32 mode)
{
	int i;
	int num, den;

	for (i = 0; i < tmdev->ctrl_data->num_sensors; i++) {
		pr_debug(
			"sensor%d - data_point1:%#x data_point2:%#x\n",
			i, p1[i], p2[i]);

		tmdev->sensor[i].slope = SLOPE_DEFAULT;
		if (mode == TWO_PT_CALIB) {
			/*
			 * slope (m) = adc_code2 - adc_code1 (y2 - y1)/
			 *	temp_120_degc - temp_30_degc (x2 - x1)
			 */
			num = p2[i] - p1[i];
			num *= SLOPE_FACTOR;
			den = CAL_DEGC_PT2 - CAL_DEGC_PT1;
			tmdev->sensor[i].slope = num / den;
		}

		tmdev->sensor[i].offset = (p1[i] * SLOPE_FACTOR) -
			(CAL_DEGC_PT1 *
			tmdev->sensor[i].slope);
		pr_debug("offset:%d\n", tmdev->sensor[i].offset);
	}
}

int calibrate_8937(struct tsens_device *tmdev)
{
	int base0 = 0, base1 = 0, i;
	u32 p1[TSENS_NUM_SENSORS_8937], p2[TSENS_NUM_SENSORS_8937];
	int mode = 0, tmp = 0;
	u32 qfprom_cdata[5] = { 0, 0, 0, 0, 0 };

	qfprom_cdata[0] = readl_relaxed(tmdev->tsens_calib_addr + 0x1D8);
	qfprom_cdata[1] = readl_relaxed(tmdev->tsens_calib_addr + 0x1DC);
	qfprom_cdata[2] = readl_relaxed(tmdev->tsens_calib_addr + 0x210);
	qfprom_cdata[3] = readl_relaxed(tmdev->tsens_calib_addr + 0x214);
	qfprom_cdata[4] = readl_relaxed(tmdev->tsens_calib_addr + 0x230);

	mode = (qfprom_cdata[2] & CAL_SEL_MASK_8937);
	pr_debug("calibration mode is %d\n", mode);

	switch (mode) {
	case TWO_PT_CALIB:
		base1 = (qfprom_cdata[1] & BASE1_MASK_8937) >> BASE1_SHIFT_8937;
		p2[0] = (qfprom_cdata[2] & S0_P2_MASK_8937) >> S0_P2_SHIFT_8937;
		p2[1] = (qfprom_cdata[2] & S1_P2_MASK_8937) >> S1_P2_SHIFT_8937;
		p2[2] = (qfprom_cdata[3] & S2_P2_MASK_8937) >> S2_P2_SHIFT_8937;
		p2[3] = (qfprom_cdata[3] & S3_P2_MASK_8937) >> S3_P2_SHIFT_8937;
		p2[4] = (qfprom_cdata[3] & S4_P2_MASK_8937) >> S4_P2_SHIFT_8937;
		p2[5] = (qfprom_cdata[0] & S5_P2_MASK_8937) >> S5_P2_SHIFT_8937;
		p2[6] = (qfprom_cdata[0] & S6_P2_MASK_8937) >> S6_P2_SHIFT_8937;
		p2[7] = (qfprom_cdata[1] & S7_P2_MASK_8937) >> S7_P2_SHIFT_8937;
		p2[8] = (qfprom_cdata[1] & S8_P2_MASK_8937) >> S8_P2_SHIFT_8937;
		p2[9] = (qfprom_cdata[4] & S9_P2_MASK_8937) >> S9_P2_SHIFT_8937;
		p2[10] = ((qfprom_cdata[4] & S10_P2_MASK_8937)
					>> S10_P2_SHIFT_8937);

		for (i = 0; i < TSENS_NUM_SENSORS_8937; i++)
			p2[i] = ((base1 + p2[i]) << 2);
		fallthrough;
	case ONE_PT_CALIB2:
		base0 = (qfprom_cdata[0] & BASE0_MASK_8937);
		p1[0] = (qfprom_cdata[2] & S0_P1_MASK_8937) >> S0_P1_SHIFT_8937;
		p1[1] = (qfprom_cdata[2] & S1_P1_MASK_8937) >> S1_P1_SHIFT_8937;
		p1[2] = ((qfprom_cdata[2] & S2_P1_MASK_0_4_8937)
					>> S2_P1_SHIFT_0_4_8937);
		tmp = ((qfprom_cdata[3] & S2_P1_MASK_5_8937)
					<< S2_P1_SHIFT_5_8937);
		p1[2] |= tmp;
		p1[3] = (qfprom_cdata[3] & S3_P1_MASK_8937) >> S3_P1_SHIFT_8937;
		p1[4] = (qfprom_cdata[3] & S4_P1_MASK_8937) >> S4_P1_SHIFT_8937;
		p1[5] = (qfprom_cdata[0] & S5_P1_MASK_8937) >> S5_P1_SHIFT_8937;
		p1[6] = (qfprom_cdata[0] & S6_P1_MASK_8937) >> S6_P1_SHIFT_8937;
		p1[7] = (qfprom_cdata[1] & S7_P1_MASK_8937);
		p1[8] = (qfprom_cdata[1] & S8_P1_MASK_8937) >> S8_P1_SHIFT_8937;
		p1[9] = (qfprom_cdata[4] & S9_P1_MASK_8937);
		p1[10] = ((qfprom_cdata[4] & S10_P1_MASK_8937)
					>> S10_P1_SHIFT_8937);

		for (i = 0; i < TSENS_NUM_SENSORS_8937; i++)
			p1[i] = (((base0)+p1[i]) << 2);
		break;
	default:
		for (i = 0; i < TSENS_NUM_SENSORS_8937; i++) {
			p1[i] = 500;
			p2[i] = 780;
		}
		break;
	}

	compute_intercept_slope(tmdev, p1, p2, mode);

	return 0;
}

int calibrate_405(struct tsens_device *tmdev)
{
	int base0 = 0, base1 = 0, i;
	u32 p1[TSENS_NUM_SENSORS_405], p2[TSENS_NUM_SENSORS_405];
	int mode = 0, tmp = 0;
	u32 qfprom_cdata[5] = { 0, 0, 0, 0, 0 };

	qfprom_cdata[0] = readl_relaxed(tmdev->tsens_calib_addr + 0x1F8);
	qfprom_cdata[1] = readl_relaxed(tmdev->tsens_calib_addr + 0x1FC);
	qfprom_cdata[2] = readl_relaxed(tmdev->tsens_calib_addr + 0x200);
	qfprom_cdata[3] = readl_relaxed(tmdev->tsens_calib_addr + 0x204);
	qfprom_cdata[4] = readl_relaxed(tmdev->tsens_calib_addr + 0x208);

	mode = (qfprom_cdata[4] & CAL_SEL_MASK_405);
	pr_debug("calibration mode is %d\n", mode);

	switch (mode) {
	case TWO_PT_CALIB:
		base1 = (qfprom_cdata[4] & BASE1_MASK_405) >> BASE1_SHIFT_405;
		p2[0] = (qfprom_cdata[0] & S0_P2_MASK_405) >> S0_P2_SHIFT_405;
		p2[1] = (qfprom_cdata[0] & S1_P2_MASK_405) >> S1_P2_SHIFT_405;
		tmp = ((qfprom_cdata[0] & S2_P2_MASK_0_1_405)
				>> S2_P2_SHIFT_0_1_405);
		p2[2] = ((qfprom_cdata[1] & S2_P2_MASK_2_5_405)
				<< S2_P2_SHIFT_2_5_405) | tmp;
		p2[3] = (qfprom_cdata[1] & S3_P2_MASK_405) >> S3_P2_SHIFT_405;
		p2[4] = (qfprom_cdata[1] & S4_P2_MASK_405) >> S4_P2_SHIFT_405;
		p2[5] = (qfprom_cdata[2] & S5_P2_MASK_405) >> S5_P2_SHIFT_405;
		p2[6] = (qfprom_cdata[2] & S6_P2_MASK_405) >> S6_P2_SHIFT_405;
		tmp = ((qfprom_cdata[2] & S7_P2_MASK_0_1_405)
				>> S7_P2_SHIFT_0_1_405);
		p2[7] = ((qfprom_cdata[3] & S7_P2_MASK_2_5_405)
				<< S7_P2_SHIFT_2_5_405) | tmp;
		p2[8] = (qfprom_cdata[3] & S8_P2_MASK_405) >> S8_P2_SHIFT_405;
		p2[9] = (qfprom_cdata[3] & S9_P2_MASK_405) >> S9_P2_SHIFT_405;

		for (i = 0; i < TSENS_NUM_SENSORS_405; i++)
			p2[i] = ((base1 + p2[i]) << 2);
		fallthrough;
	case ONE_PT_CALIB2:
		base0 = (qfprom_cdata[4] & BASE0_MASK_405) >> BASE0_SHIFT_405;
		p1[0] = (qfprom_cdata[0] & S0_P1_MASK_405) >> S0_P1_SHIFT_405;
		p1[1] = (qfprom_cdata[0] & S1_P1_MASK_405) >> S1_P1_SHIFT_405;
		p1[2] = (qfprom_cdata[0] & S2_P1_MASK_405) >> S2_P1_SHIFT_405;
		p1[3] = (qfprom_cdata[1] & S3_P1_MASK_405) >> S3_P1_SHIFT_405;
		p1[4] = (qfprom_cdata[1] & S4_P1_MASK_405) >> S4_P1_SHIFT_405;
		p1[5] = (qfprom_cdata[2] & S5_P1_MASK_405) >> S5_P1_SHIFT_405;
		p1[6] = (qfprom_cdata[2] & S6_P1_MASK_405) >> S6_P1_SHIFT_405;
		p1[7] = (qfprom_cdata[2] & S7_P1_MASK_405) >> S7_P1_SHIFT_405;
		p1[8] = (qfprom_cdata[3] & S8_P1_MASK_405) >> S8_P1_SHIFT_405;
		p1[9] = (qfprom_cdata[3] & S9_P1_MASK_405) >> S9_P1_SHIFT_405;

		for (i = 0; i < TSENS_NUM_SENSORS_405; i++)
			p1[i] = (((base0)+p1[i]) << 2);
		break;
	default:
		for (i = 0; i < TSENS_NUM_SENSORS_405; i++) {
			p1[i] = 500;
			p2[i] = 780;
		}
		break;
	}

	compute_intercept_slope(tmdev, p1, p2, mode);

	return 0;
}

int calibrate_9607(struct tsens_device *tmdev)
{
	int base0 = 0, base1 = 0, i;
	u32 p1[TSENS_NUM_SENSORS_9607], p2[TSENS_NUM_SENSORS_9607];
	int mode = 0;
	u32 qfprom_cdata[3] = { 0, 0, 0};

	qfprom_cdata[0] = readl_relaxed(tmdev->tsens_calib_addr + 0x228);
	qfprom_cdata[1] = readl_relaxed(tmdev->tsens_calib_addr + 0x22C);
	qfprom_cdata[2] = readl_relaxed(tmdev->tsens_calib_addr + 0x230);

	mode = (qfprom_cdata[2] & CAL_SEL_MASK_9607) >> CAL_SEL_SHIFT_9607;
	pr_debug("calibration mode is %d\n", mode);

	switch (mode) {
	case TWO_PT_CALIB:
		base1 = (qfprom_cdata[2] & BASE1_MASK_9607) >> BASE1_SHIFT_9607;
		p2[0] = (qfprom_cdata[0] & S0_P2_MASK_9607) >> S0_P2_SHIFT_9607;
		p2[1] = (qfprom_cdata[0] & S1_P2_MASK_9607) >> S1_P2_SHIFT_9607;
		p2[2] = (qfprom_cdata[1] & S2_P2_MASK_9607) >> S2_P2_SHIFT_9607;
		p2[3] = (qfprom_cdata[1] & S3_P2_MASK_9607) >> S3_P2_SHIFT_9607;
		p2[4] = (qfprom_cdata[2] & S4_P2_MASK_9607) >> S4_P2_SHIFT_9607;

		for (i = 0; i < TSENS_NUM_SENSORS_9607; i++)
			p2[i] = ((base1 + p2[i]) << 2);
		fallthrough;
	case ONE_PT_CALIB2:
		base0 = (qfprom_cdata[0] & BASE0_MASK_9607);
		p1[0] = (qfprom_cdata[0] & S0_P1_MASK_9607) >> S0_P1_SHIFT_9607;
		p1[1] = (qfprom_cdata[0] & S1_P1_MASK_9607) >> S1_P1_SHIFT_9607;
		p1[2] = (qfprom_cdata[1] & S2_P1_MASK_9607);
		p1[3] = (qfprom_cdata[1] & S3_P1_MASK_9607) >> S3_P1_SHIFT_9607;
		p1[4] = (qfprom_cdata[2] & S4_P1_MASK_9607);

		for (i = 0; i < TSENS_NUM_SENSORS_9607; i++)
			p1[i] = (((base0) + p1[i]) << 2);
		break;
	default:
		for (i = 0; i < TSENS_NUM_SENSORS_9607; i++) {
			p1[i] = 500;
			p2[i] = 780;
		}
		break;
	}

	compute_intercept_slope(tmdev, p1, p2, mode);

	return 0;
}
