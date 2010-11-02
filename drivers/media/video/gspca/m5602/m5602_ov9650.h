/*
 * Driver for the ov9650 sensor
 *
 * Copyright (C) 2008 Erik Andrén
 * Copyright (C) 2007 Ilyes Gouta. Based on the m5603x Linux Driver Project.
 * Copyright (C) 2005 m5603x Linux Driver Project <m5602@x3ng.com.br>
 *
 * Portions of code to USB interface and ALi driver software,
 * Copyright (c) 2006 Willem Duinker
 * v4l2 interface modeled after the V4L2 driver
 * for SN9C10x PC Camera Controllers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 *
 */

#ifndef M5602_OV9650_H_
#define M5602_OV9650_H_

#include <linux/dmi.h>
#include "m5602_sensor.h"

/*****************************************************************************/

#define OV9650_GAIN			0x00
#define OV9650_BLUE			0x01
#define OV9650_RED			0x02
#define OV9650_VREF			0x03
#define OV9650_COM1			0x04
#define OV9650_BAVE			0x05
#define OV9650_GEAVE			0x06
#define OV9650_RSVD7			0x07
#define OV9650_COM2			0x09
#define OV9650_PID			0x0a
#define OV9650_VER			0x0b
#define OV9650_COM3			0x0c
#define OV9650_COM4			0x0d
#define OV9650_COM5			0x0e
#define OV9650_COM6			0x0f
#define OV9650_AECH			0x10
#define OV9650_CLKRC			0x11
#define OV9650_COM7			0x12
#define OV9650_COM8			0x13
#define OV9650_COM9			0x14
#define OV9650_COM10			0x15
#define OV9650_RSVD16			0x16
#define OV9650_HSTART			0x17
#define OV9650_HSTOP			0x18
#define OV9650_VSTRT			0x19
#define OV9650_VSTOP			0x1a
#define OV9650_PSHFT			0x1b
#define OV9650_MVFP			0x1e
#define OV9650_AEW			0x24
#define OV9650_AEB			0x25
#define OV9650_VPT			0x26
#define OV9650_BBIAS			0x27
#define OV9650_GbBIAS			0x28
#define OV9650_Gr_COM			0x29
#define OV9650_RBIAS			0x2c
#define OV9650_HREF			0x32
#define OV9650_CHLF			0x33
#define OV9650_ARBLM			0x34
#define OV9650_RSVD35			0x35
#define OV9650_RSVD36			0x36
#define OV9650_ADC			0x37
#define OV9650_ACOM38			0x38
#define OV9650_OFON			0x39
#define OV9650_TSLB			0x3a
#define OV9650_COM12			0x3c
#define OV9650_COM13			0x3d
#define OV9650_COM15			0x40
#define OV9650_COM16			0x41
#define OV9650_LCC1			0x62
#define OV9650_LCC2			0x63
#define OV9650_LCC3			0x64
#define OV9650_LCC4			0x65
#define OV9650_LCC5			0x66
#define OV9650_HV			0x69
#define OV9650_DBLV			0x6b
#define OV9650_COM21			0x8b
#define OV9650_COM22			0x8c
#define OV9650_COM24			0x8e
#define OV9650_DBLC1			0x8f
#define OV9650_RSVD94			0x94
#define OV9650_RSVD95			0x95
#define OV9650_RSVD96			0x96
#define OV9650_LCCFB			0x9d
#define OV9650_LCCFR			0x9e
#define OV9650_AECHM			0xa1
#define OV9650_COM26			0xa5
#define OV9650_ACOMA8			0xa8
#define OV9650_ACOMA9			0xa9

#define OV9650_REGISTER_RESET		(1 << 7)
#define OV9650_VGA_SELECT		(1 << 6)
#define OV9650_CIF_SELECT		(1 << 5)
#define OV9650_QVGA_SELECT		(1 << 4)
#define OV9650_QCIF_SELECT		(1 << 3)
#define OV9650_RGB_SELECT		(1 << 2)
#define OV9650_RAW_RGB_SELECT		(1 << 0)

#define OV9650_FAST_AGC_AEC		(1 << 7)
#define OV9650_AEC_UNLIM_STEP_SIZE	(1 << 6)
#define OV9650_BANDING			(1 << 5)
#define OV9650_AGC_EN			(1 << 2)
#define OV9650_AWB_EN			(1 << 1)
#define OV9650_AEC_EN			(1 << 0)

#define OV9650_VARIOPIXEL		(1 << 2)
#define OV9650_SYSTEM_CLK_SEL		(1 << 7)
#define OV9650_SLAM_MODE		(1 << 4)

#define OV9650_QVGA_VARIOPIXEL		(1 << 7)

#define OV9650_VFLIP			(1 << 4)
#define OV9650_HFLIP			(1 << 5)

#define OV9650_SOFT_SLEEP		(1 << 4)
#define OV9650_OUTPUT_DRIVE_2X		(1 << 0)

#define OV9650_DENOISE_ENABLE		(1 << 5)
#define OV9650_WHITE_PIXEL_ENABLE	(1 << 1)
#define OV9650_WHITE_PIXEL_OPTION	(1 << 0)

#define OV9650_LEFT_OFFSET		0x62

#define GAIN_DEFAULT			0x14
#define RED_GAIN_DEFAULT		0x70
#define BLUE_GAIN_DEFAULT		0x20
#define EXPOSURE_DEFAULT		0x1ff

/*****************************************************************************/

/* Kernel module parameters */
extern int force_sensor;
extern int dump_sensor;

int ov9650_probe(struct sd *sd);
int ov9650_init(struct sd *sd);
int ov9650_start(struct sd *sd);
int ov9650_stop(struct sd *sd);
void ov9650_disconnect(struct sd *sd);

static const struct m5602_sensor ov9650 = {
	.name = "OV9650",
	.i2c_slave_id = 0x60,
	.i2c_regW = 1,
	.probe = ov9650_probe,
	.init = ov9650_init,
	.start = ov9650_start,
	.stop = ov9650_stop,
	.disconnect = ov9650_disconnect,
};

static const unsigned char preinit_ov9650[][3] = {
	/* [INITCAM] */
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0},
	{BRIDGE, M5602_XB_SENSOR_CTRL, 0x00},

	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x08},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x05},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x04},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x00},
	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x0a},
	/* Reset chip */
	{SENSOR, OV9650_COM7, OV9650_REGISTER_RESET},
	/* Enable double clock */
	{SENSOR, OV9650_CLKRC, 0x80},
	/* Do something out of spec with the power */
	{SENSOR, OV9650_OFON, 0x40}
};

static const unsigned char init_ov9650[][3] = {
	/* [INITCAM] */
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0},
	{BRIDGE, M5602_XB_SENSOR_CTRL, 0x00},

	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x08},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x05},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x04},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x00},
	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x0a},

	/* Reset chip */
	{SENSOR, OV9650_COM7, OV9650_REGISTER_RESET},
	/* One extra reset is needed in order to make the sensor behave
	   properly when resuming from ram, could be a timing issue */
	{SENSOR, OV9650_COM7, OV9650_REGISTER_RESET},

	/* Enable double clock */
	{SENSOR, OV9650_CLKRC, 0x80},
	/* Do something out of spec with the power */
	{SENSOR, OV9650_OFON, 0x40},

	/* Set fast AGC/AEC algorithm with unlimited step size */
	{SENSOR, OV9650_COM8, OV9650_FAST_AGC_AEC |
			      OV9650_AEC_UNLIM_STEP_SIZE},

	{SENSOR, OV9650_CHLF, 0x10},
	{SENSOR, OV9650_ARBLM, 0xbf},
	{SENSOR, OV9650_ACOM38, 0x81},
	/* Turn off color matrix coefficient double option */
	{SENSOR, OV9650_COM16, 0x00},
	/* Enable color matrix for RGB/YUV, Delay Y channel,
	set output Y/UV delay to 1 */
	{SENSOR, OV9650_COM13, 0x19},
	/* Enable digital BLC, Set output mode to U Y V Y */
	{SENSOR, OV9650_TSLB, 0x0c},
	/* Limit the AGC/AEC stable upper region */
	{SENSOR, OV9650_COM24, 0x00},
	/* Enable HREF and some out of spec things */
	{SENSOR, OV9650_COM12, 0x73},
	/* Set all DBLC offset signs to positive and
	do some out of spec stuff */
	{SENSOR, OV9650_DBLC1, 0xdf},
	{SENSOR, OV9650_COM21, 0x06},
	{SENSOR, OV9650_RSVD35, 0x91},
	/* Necessary, no camera stream without it */
	{SENSOR, OV9650_RSVD16, 0x06},
	{SENSOR, OV9650_RSVD94, 0x99},
	{SENSOR, OV9650_RSVD95, 0x99},
	{SENSOR, OV9650_RSVD96, 0x04},
	/* Enable full range output */
	{SENSOR, OV9650_COM15, 0x0},
	/* Enable HREF at optical black, enable ADBLC bias,
	enable ADBLC, reset timings at format change */
	{SENSOR, OV9650_COM6, 0x4b},
	/* Subtract 32 from the B channel bias */
	{SENSOR, OV9650_BBIAS, 0xa0},
	/* Subtract 32 from the Gb channel bias */
	{SENSOR, OV9650_GbBIAS, 0xa0},
	/* Do not bypass the analog BLC and to some out of spec stuff */
	{SENSOR, OV9650_Gr_COM, 0x00},
	/* Subtract 32 from the R channel bias */
	{SENSOR, OV9650_RBIAS, 0xa0},
	/* Subtract 32 from the R channel bias */
	{SENSOR, OV9650_RBIAS, 0x0},
	{SENSOR, OV9650_COM26, 0x80},
	{SENSOR, OV9650_ACOMA9, 0x98},
	/* Set the AGC/AEC stable region upper limit */
	{SENSOR, OV9650_AEW, 0x68},
	/* Set the AGC/AEC stable region lower limit */
	{SENSOR, OV9650_AEB, 0x5c},
	/* Set the high and low limit nibbles to 3 */
	{SENSOR, OV9650_VPT, 0xc3},
	/* Set the Automatic Gain Ceiling (AGC) to 128x,
	drop VSYNC at frame drop,
	limit exposure timing,
	drop frame when the AEC step is larger than the exposure gap */
	{SENSOR, OV9650_COM9, 0x6e},
	/* Set VSYNC negative, Set RESET to SLHS (slave mode horizontal sync)
	and set PWDN to SLVS (slave mode vertical sync) */
	{SENSOR, OV9650_COM10, 0x42},
	/* Set horizontal column start high to default value */
	{SENSOR, OV9650_HSTART, 0x1a}, /* 210 */
	/* Set horizontal column end */
	{SENSOR, OV9650_HSTOP, 0xbf}, /* 1534 */
	/* Complementing register to the two writes above */
	{SENSOR, OV9650_HREF, 0xb2},
	/* Set vertical row start high bits */
	{SENSOR, OV9650_VSTRT, 0x02},
	/* Set vertical row end low bits */
	{SENSOR, OV9650_VSTOP, 0x7e},
	/* Set complementing vertical frame control */
	{SENSOR, OV9650_VREF, 0x10},
	{SENSOR, OV9650_ADC, 0x04},
	{SENSOR, OV9650_HV, 0x40},

	/* Enable denoise, and white-pixel erase */
	{SENSOR, OV9650_COM22, OV9650_DENOISE_ENABLE |
		 OV9650_WHITE_PIXEL_ENABLE |
		 OV9650_WHITE_PIXEL_OPTION},

	/* Enable VARIOPIXEL */
	{SENSOR, OV9650_COM3, OV9650_VARIOPIXEL},
	{SENSOR, OV9650_COM4, OV9650_QVGA_VARIOPIXEL},

	/* Put the sensor in soft sleep mode */
	{SENSOR, OV9650_COM2, OV9650_SOFT_SLEEP | OV9650_OUTPUT_DRIVE_2X},
};

static const unsigned char res_init_ov9650[][3] = {
	{SENSOR, OV9650_COM2, OV9650_OUTPUT_DRIVE_2X},

	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x82},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_L, 0x00},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82},
	{BRIDGE, M5602_XB_PIX_OF_LINE_L, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x01}
};
#endif
