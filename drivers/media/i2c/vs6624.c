/*
 * vs6624.c ST VS6624 CMOS image sensor driver
 *
 * Copyright (c) 2011 Analog Devices Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-image-sizes.h>

#include "vs6624_regs.h"

#define MAX_FRAME_RATE  30

struct vs6624 {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_fract frame_rate;
	struct v4l2_mbus_framefmt fmt;
	unsigned ce_pin;
};

static const struct vs6624_format {
	u32 mbus_code;
	enum v4l2_colorspace colorspace;
} vs6624_formats[] = {
	{
		.mbus_code      = MEDIA_BUS_FMT_UYVY8_2X8,
		.colorspace     = V4L2_COLORSPACE_JPEG,
	},
	{
		.mbus_code      = MEDIA_BUS_FMT_YUYV8_2X8,
		.colorspace     = V4L2_COLORSPACE_JPEG,
	},
	{
		.mbus_code      = MEDIA_BUS_FMT_RGB565_2X8_LE,
		.colorspace     = V4L2_COLORSPACE_SRGB,
	},
};

static const struct v4l2_mbus_framefmt vs6624_default_fmt = {
	.width = VGA_WIDTH,
	.height = VGA_HEIGHT,
	.code = MEDIA_BUS_FMT_UYVY8_2X8,
	.field = V4L2_FIELD_NONE,
	.colorspace = V4L2_COLORSPACE_JPEG,
};

static const u16 vs6624_p1[] = {
	0x8104, 0x03,
	0x8105, 0x01,
	0xc900, 0x03,
	0xc904, 0x47,
	0xc905, 0x10,
	0xc906, 0x80,
	0xc907, 0x3a,
	0x903a, 0x02,
	0x903b, 0x47,
	0x903c, 0x15,
	0xc908, 0x31,
	0xc909, 0xdc,
	0xc90a, 0x80,
	0xc90b, 0x44,
	0x9044, 0x02,
	0x9045, 0x31,
	0x9046, 0xe2,
	0xc90c, 0x07,
	0xc90d, 0xe0,
	0xc90e, 0x80,
	0xc90f, 0x47,
	0x9047, 0x90,
	0x9048, 0x83,
	0x9049, 0x81,
	0x904a, 0xe0,
	0x904b, 0x60,
	0x904c, 0x08,
	0x904d, 0x90,
	0x904e, 0xc0,
	0x904f, 0x43,
	0x9050, 0x74,
	0x9051, 0x01,
	0x9052, 0xf0,
	0x9053, 0x80,
	0x9054, 0x05,
	0x9055, 0xE4,
	0x9056, 0x90,
	0x9057, 0xc0,
	0x9058, 0x43,
	0x9059, 0xf0,
	0x905a, 0x02,
	0x905b, 0x07,
	0x905c, 0xec,
	0xc910, 0x5d,
	0xc911, 0xca,
	0xc912, 0x80,
	0xc913, 0x5d,
	0x905d, 0xa3,
	0x905e, 0x04,
	0x905f, 0xf0,
	0x9060, 0xa3,
	0x9061, 0x04,
	0x9062, 0xf0,
	0x9063, 0x22,
	0xc914, 0x72,
	0xc915, 0x92,
	0xc916, 0x80,
	0xc917, 0x64,
	0x9064, 0x74,
	0x9065, 0x01,
	0x9066, 0x02,
	0x9067, 0x72,
	0x9068, 0x95,
	0xc918, 0x47,
	0xc919, 0xf2,
	0xc91a, 0x81,
	0xc91b, 0x69,
	0x9169, 0x74,
	0x916a, 0x02,
	0x916b, 0xf0,
	0x916c, 0xec,
	0x916d, 0xb4,
	0x916e, 0x10,
	0x916f, 0x0a,
	0x9170, 0x90,
	0x9171, 0x80,
	0x9172, 0x16,
	0x9173, 0xe0,
	0x9174, 0x70,
	0x9175, 0x04,
	0x9176, 0x90,
	0x9177, 0xd3,
	0x9178, 0xc4,
	0x9179, 0xf0,
	0x917a, 0x22,
	0xc91c, 0x0a,
	0xc91d, 0xbe,
	0xc91e, 0x80,
	0xc91f, 0x73,
	0x9073, 0xfc,
	0x9074, 0xa3,
	0x9075, 0xe0,
	0x9076, 0xf5,
	0x9077, 0x82,
	0x9078, 0x8c,
	0x9079, 0x83,
	0x907a, 0xa3,
	0x907b, 0xa3,
	0x907c, 0xe0,
	0x907d, 0xfc,
	0x907e, 0xa3,
	0x907f, 0xe0,
	0x9080, 0xc3,
	0x9081, 0x9f,
	0x9082, 0xff,
	0x9083, 0xec,
	0x9084, 0x9e,
	0x9085, 0xfe,
	0x9086, 0x02,
	0x9087, 0x0a,
	0x9088, 0xea,
	0xc920, 0x47,
	0xc921, 0x38,
	0xc922, 0x80,
	0xc923, 0x89,
	0x9089, 0xec,
	0x908a, 0xd3,
	0x908b, 0x94,
	0x908c, 0x20,
	0x908d, 0x40,
	0x908e, 0x01,
	0x908f, 0x1c,
	0x9090, 0x90,
	0x9091, 0xd3,
	0x9092, 0xd4,
	0x9093, 0xec,
	0x9094, 0xf0,
	0x9095, 0x02,
	0x9096, 0x47,
	0x9097, 0x3d,
	0xc924, 0x45,
	0xc925, 0xca,
	0xc926, 0x80,
	0xc927, 0x98,
	0x9098, 0x12,
	0x9099, 0x77,
	0x909a, 0xd6,
	0x909b, 0x02,
	0x909c, 0x45,
	0x909d, 0xcd,
	0xc928, 0x20,
	0xc929, 0xd5,
	0xc92a, 0x80,
	0xc92b, 0x9e,
	0x909e, 0x90,
	0x909f, 0x82,
	0x90a0, 0x18,
	0x90a1, 0xe0,
	0x90a2, 0xb4,
	0x90a3, 0x03,
	0x90a4, 0x0e,
	0x90a5, 0x90,
	0x90a6, 0x83,
	0x90a7, 0xbf,
	0x90a8, 0xe0,
	0x90a9, 0x60,
	0x90aa, 0x08,
	0x90ab, 0x90,
	0x90ac, 0x81,
	0x90ad, 0xfc,
	0x90ae, 0xe0,
	0x90af, 0xff,
	0x90b0, 0xc3,
	0x90b1, 0x13,
	0x90b2, 0xf0,
	0x90b3, 0x90,
	0x90b4, 0x81,
	0x90b5, 0xfc,
	0x90b6, 0xe0,
	0x90b7, 0xff,
	0x90b8, 0x02,
	0x90b9, 0x20,
	0x90ba, 0xda,
	0xc92c, 0x70,
	0xc92d, 0xbc,
	0xc92e, 0x80,
	0xc92f, 0xbb,
	0x90bb, 0x90,
	0x90bc, 0x82,
	0x90bd, 0x18,
	0x90be, 0xe0,
	0x90bf, 0xb4,
	0x90c0, 0x03,
	0x90c1, 0x06,
	0x90c2, 0x90,
	0x90c3, 0xc1,
	0x90c4, 0x06,
	0x90c5, 0x74,
	0x90c6, 0x05,
	0x90c7, 0xf0,
	0x90c8, 0x90,
	0x90c9, 0xd3,
	0x90ca, 0xa0,
	0x90cb, 0x02,
	0x90cc, 0x70,
	0x90cd, 0xbf,
	0xc930, 0x72,
	0xc931, 0x21,
	0xc932, 0x81,
	0xc933, 0x3b,
	0x913b, 0x7d,
	0x913c, 0x02,
	0x913d, 0x7f,
	0x913e, 0x7b,
	0x913f, 0x02,
	0x9140, 0x72,
	0x9141, 0x25,
	0xc934, 0x28,
	0xc935, 0xae,
	0xc936, 0x80,
	0xc937, 0xd2,
	0x90d2, 0xf0,
	0x90d3, 0x90,
	0x90d4, 0xd2,
	0x90d5, 0x0a,
	0x90d6, 0x02,
	0x90d7, 0x28,
	0x90d8, 0xb4,
	0xc938, 0x28,
	0xc939, 0xb1,
	0xc93a, 0x80,
	0xc93b, 0xd9,
	0x90d9, 0x90,
	0x90da, 0x83,
	0x90db, 0xba,
	0x90dc, 0xe0,
	0x90dd, 0xff,
	0x90de, 0x90,
	0x90df, 0xd2,
	0x90e0, 0x08,
	0x90e1, 0xe0,
	0x90e2, 0xe4,
	0x90e3, 0xef,
	0x90e4, 0xf0,
	0x90e5, 0xa3,
	0x90e6, 0xe0,
	0x90e7, 0x74,
	0x90e8, 0xff,
	0x90e9, 0xf0,
	0x90ea, 0x90,
	0x90eb, 0xd2,
	0x90ec, 0x0a,
	0x90ed, 0x02,
	0x90ee, 0x28,
	0x90ef, 0xb4,
	0xc93c, 0x29,
	0xc93d, 0x79,
	0xc93e, 0x80,
	0xc93f, 0xf0,
	0x90f0, 0xf0,
	0x90f1, 0x90,
	0x90f2, 0xd2,
	0x90f3, 0x0e,
	0x90f4, 0x02,
	0x90f5, 0x29,
	0x90f6, 0x7f,
	0xc940, 0x29,
	0xc941, 0x7c,
	0xc942, 0x80,
	0xc943, 0xf7,
	0x90f7, 0x90,
	0x90f8, 0x83,
	0x90f9, 0xba,
	0x90fa, 0xe0,
	0x90fb, 0xff,
	0x90fc, 0x90,
	0x90fd, 0xd2,
	0x90fe, 0x0c,
	0x90ff, 0xe0,
	0x9100, 0xe4,
	0x9101, 0xef,
	0x9102, 0xf0,
	0x9103, 0xa3,
	0x9104, 0xe0,
	0x9105, 0x74,
	0x9106, 0xff,
	0x9107, 0xf0,
	0x9108, 0x90,
	0x9109, 0xd2,
	0x910a, 0x0e,
	0x910b, 0x02,
	0x910c, 0x29,
	0x910d, 0x7f,
	0xc944, 0x2a,
	0xc945, 0x42,
	0xc946, 0x81,
	0xc947, 0x0e,
	0x910e, 0xf0,
	0x910f, 0x90,
	0x9110, 0xd2,
	0x9111, 0x12,
	0x9112, 0x02,
	0x9113, 0x2a,
	0x9114, 0x48,
	0xc948, 0x2a,
	0xc949, 0x45,
	0xc94a, 0x81,
	0xc94b, 0x15,
	0x9115, 0x90,
	0x9116, 0x83,
	0x9117, 0xba,
	0x9118, 0xe0,
	0x9119, 0xff,
	0x911a, 0x90,
	0x911b, 0xd2,
	0x911c, 0x10,
	0x911d, 0xe0,
	0x911e, 0xe4,
	0x911f, 0xef,
	0x9120, 0xf0,
	0x9121, 0xa3,
	0x9122, 0xe0,
	0x9123, 0x74,
	0x9124, 0xff,
	0x9125, 0xf0,
	0x9126, 0x90,
	0x9127, 0xd2,
	0x9128, 0x12,
	0x9129, 0x02,
	0x912a, 0x2a,
	0x912b, 0x48,
	0xc900, 0x01,
	0x0000, 0x00,
};

static const u16 vs6624_p2[] = {
	0x806f, 0x01,
	0x058c, 0x01,
	0x0000, 0x00,
};

static const u16 vs6624_run_setup[] = {
	0x1d18, 0x00,				/* Enableconstrainedwhitebalance */
	VS6624_PEAK_MIN_OUT_G_MSB, 0x3c,	/* Damper PeakGain Output MSB */
	VS6624_PEAK_MIN_OUT_G_LSB, 0x66,	/* Damper PeakGain Output LSB */
	VS6624_CM_LOW_THR_MSB, 0x65,		/* Damper Low MSB */
	VS6624_CM_LOW_THR_LSB, 0xd1,		/* Damper Low LSB */
	VS6624_CM_HIGH_THR_MSB, 0x66,		/* Damper High MSB */
	VS6624_CM_HIGH_THR_LSB, 0x62,		/* Damper High LSB */
	VS6624_CM_MIN_OUT_MSB, 0x00,		/* Damper Min output MSB */
	VS6624_CM_MIN_OUT_LSB, 0x00,		/* Damper Min output LSB */
	VS6624_NORA_DISABLE, 0x00,		/* Nora fDisable */
	VS6624_NORA_USAGE, 0x04,		/* Nora usage */
	VS6624_NORA_LOW_THR_MSB, 0x63,		/* Damper Low MSB Changed 0x63 to 0x65 */
	VS6624_NORA_LOW_THR_LSB, 0xd1,		/* Damper Low LSB */
	VS6624_NORA_HIGH_THR_MSB, 0x68,		/* Damper High MSB */
	VS6624_NORA_HIGH_THR_LSB, 0xdd,		/* Damper High LSB */
	VS6624_NORA_MIN_OUT_MSB, 0x3a,		/* Damper Min output MSB */
	VS6624_NORA_MIN_OUT_LSB, 0x00,		/* Damper Min output LSB */
	VS6624_F2B_DISABLE, 0x00,		/* Disable */
	0x1d8a, 0x30,				/* MAXWeightHigh */
	0x1d91, 0x62,				/* fpDamperLowThresholdHigh MSB */
	0x1d92, 0x4a,				/* fpDamperLowThresholdHigh LSB */
	0x1d95, 0x65,				/* fpDamperHighThresholdHigh MSB */
	0x1d96, 0x0e,				/* fpDamperHighThresholdHigh LSB */
	0x1da1, 0x3a,				/* fpMinimumDamperOutputLow MSB */
	0x1da2, 0xb8,				/* fpMinimumDamperOutputLow LSB */
	0x1e08, 0x06,				/* MAXWeightLow */
	0x1e0a, 0x0a,				/* MAXWeightHigh */
	0x1601, 0x3a,				/* Red A MSB */
	0x1602, 0x14,				/* Red A LSB */
	0x1605, 0x3b,				/* Blue A MSB */
	0x1606, 0x85,				/* BLue A LSB */
	0x1609, 0x3b,				/* RED B MSB */
	0x160a, 0x85,				/* RED B LSB */
	0x160d, 0x3a,				/* Blue B MSB */
	0x160e, 0x14,				/* Blue B LSB */
	0x1611, 0x30,				/* Max Distance from Locus MSB */
	0x1612, 0x8f,				/* Max Distance from Locus MSB */
	0x1614, 0x01,				/* Enable constrainer */
	0x0000, 0x00,
};

static const u16 vs6624_default[] = {
	VS6624_CONTRAST0, 0x84,
	VS6624_SATURATION0, 0x75,
	VS6624_GAMMA0, 0x11,
	VS6624_CONTRAST1, 0x84,
	VS6624_SATURATION1, 0x75,
	VS6624_GAMMA1, 0x11,
	VS6624_MAN_RG, 0x80,
	VS6624_MAN_GG, 0x80,
	VS6624_MAN_BG, 0x80,
	VS6624_WB_MODE, 0x1,
	VS6624_EXPO_COMPENSATION, 0xfe,
	VS6624_EXPO_METER, 0x0,
	VS6624_LIGHT_FREQ, 0x64,
	VS6624_PEAK_GAIN, 0xe,
	VS6624_PEAK_LOW_THR, 0x28,
	VS6624_HMIRROR0, 0x0,
	VS6624_VFLIP0, 0x0,
	VS6624_ZOOM_HSTEP0_MSB, 0x0,
	VS6624_ZOOM_HSTEP0_LSB, 0x1,
	VS6624_ZOOM_VSTEP0_MSB, 0x0,
	VS6624_ZOOM_VSTEP0_LSB, 0x1,
	VS6624_PAN_HSTEP0_MSB, 0x0,
	VS6624_PAN_HSTEP0_LSB, 0xf,
	VS6624_PAN_VSTEP0_MSB, 0x0,
	VS6624_PAN_VSTEP0_LSB, 0xf,
	VS6624_SENSOR_MODE, 0x1,
	VS6624_SYNC_CODE_SETUP, 0x21,
	VS6624_DISABLE_FR_DAMPER, 0x0,
	VS6624_FR_DEN, 0x1,
	VS6624_FR_NUM_LSB, 0xf,
	VS6624_INIT_PIPE_SETUP, 0x0,
	VS6624_IMG_FMT0, 0x0,
	VS6624_YUV_SETUP, 0x1,
	VS6624_IMAGE_SIZE0, 0x2,
	0x0000, 0x00,
};

static inline struct vs6624 *to_vs6624(struct v4l2_subdev *sd)
{
	return container_of(sd, struct vs6624, sd);
}
static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct vs6624, hdl)->sd;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vs6624_read(struct v4l2_subdev *sd, u16 index)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 buf[2];

	buf[0] = index >> 8;
	buf[1] = index;
	i2c_master_send(client, buf, 2);
	i2c_master_recv(client, buf, 1);

	return buf[0];
}
#endif

static int vs6624_write(struct v4l2_subdev *sd, u16 index,
				u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 buf[3];

	buf[0] = index >> 8;
	buf[1] = index;
	buf[2] = value;

	return i2c_master_send(client, buf, 3);
}

static int vs6624_writeregs(struct v4l2_subdev *sd, const u16 *regs)
{
	u16 reg;
	u8 data;

	while (*regs != 0x00) {
		reg = *regs++;
		data = *regs++;

		vs6624_write(sd, reg, data);
	}
	return 0;
}

static int vs6624_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);

	switch (ctrl->id) {
	case V4L2_CID_CONTRAST:
		vs6624_write(sd, VS6624_CONTRAST0, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		vs6624_write(sd, VS6624_SATURATION0, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		vs6624_write(sd, VS6624_HMIRROR0, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		vs6624_write(sd, VS6624_VFLIP0, ctrl->val);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vs6624_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(vs6624_formats))
		return -EINVAL;

	code->code = vs6624_formats[code->index].mbus_code;
	return 0;
}

static int vs6624_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct vs6624 *sensor = to_vs6624(sd);
	int index;

	if (format->pad)
		return -EINVAL;

	for (index = 0; index < ARRAY_SIZE(vs6624_formats); index++)
		if (vs6624_formats[index].mbus_code == fmt->code)
			break;
	if (index >= ARRAY_SIZE(vs6624_formats)) {
		/* default to first format */
		index = 0;
		fmt->code = vs6624_formats[0].mbus_code;
	}

	/* sensor mode is VGA */
	if (fmt->width > VGA_WIDTH)
		fmt->width = VGA_WIDTH;
	if (fmt->height > VGA_HEIGHT)
		fmt->height = VGA_HEIGHT;
	fmt->width = fmt->width & (~3);
	fmt->height = fmt->height & (~3);
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = vs6624_formats[index].colorspace;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		cfg->try_fmt = *fmt;
		return 0;
	}

	/* set image format */
	switch (fmt->code) {
	case MEDIA_BUS_FMT_UYVY8_2X8:
		vs6624_write(sd, VS6624_IMG_FMT0, 0x0);
		vs6624_write(sd, VS6624_YUV_SETUP, 0x1);
		break;
	case MEDIA_BUS_FMT_YUYV8_2X8:
		vs6624_write(sd, VS6624_IMG_FMT0, 0x0);
		vs6624_write(sd, VS6624_YUV_SETUP, 0x3);
		break;
	case MEDIA_BUS_FMT_RGB565_2X8_LE:
		vs6624_write(sd, VS6624_IMG_FMT0, 0x4);
		vs6624_write(sd, VS6624_RGB_SETUP, 0x0);
		break;
	default:
		return -EINVAL;
	}

	/* set image size */
	if ((fmt->width == VGA_WIDTH) && (fmt->height == VGA_HEIGHT))
		vs6624_write(sd, VS6624_IMAGE_SIZE0, 0x2);
	else if ((fmt->width == QVGA_WIDTH) && (fmt->height == QVGA_HEIGHT))
		vs6624_write(sd, VS6624_IMAGE_SIZE0, 0x4);
	else if ((fmt->width == QQVGA_WIDTH) && (fmt->height == QQVGA_HEIGHT))
		vs6624_write(sd, VS6624_IMAGE_SIZE0, 0x6);
	else if ((fmt->width == CIF_WIDTH) && (fmt->height == CIF_HEIGHT))
		vs6624_write(sd, VS6624_IMAGE_SIZE0, 0x3);
	else if ((fmt->width == QCIF_WIDTH) && (fmt->height == QCIF_HEIGHT))
		vs6624_write(sd, VS6624_IMAGE_SIZE0, 0x5);
	else if ((fmt->width == QQCIF_WIDTH) && (fmt->height == QQCIF_HEIGHT))
		vs6624_write(sd, VS6624_IMAGE_SIZE0, 0x7);
	else {
		vs6624_write(sd, VS6624_IMAGE_SIZE0, 0x8);
		vs6624_write(sd, VS6624_MAN_HSIZE0_MSB, fmt->width >> 8);
		vs6624_write(sd, VS6624_MAN_HSIZE0_LSB, fmt->width & 0xFF);
		vs6624_write(sd, VS6624_MAN_VSIZE0_MSB, fmt->height >> 8);
		vs6624_write(sd, VS6624_MAN_VSIZE0_LSB, fmt->height & 0xFF);
		vs6624_write(sd, VS6624_CROP_CTRL0, 0x1);
	}

	sensor->fmt = *fmt;

	return 0;
}

static int vs6624_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct vs6624 *sensor = to_vs6624(sd);

	if (format->pad)
		return -EINVAL;

	format->format = sensor->fmt;
	return 0;
}

static int vs6624_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct vs6624 *sensor = to_vs6624(sd);
	struct v4l2_captureparm *cp = &parms->parm.capture;

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(cp, 0, sizeof(*cp));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	cp->timeperframe.numerator = sensor->frame_rate.denominator;
	cp->timeperframe.denominator = sensor->frame_rate.numerator;
	return 0;
}

static int vs6624_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct vs6624 *sensor = to_vs6624(sd);
	struct v4l2_captureparm *cp = &parms->parm.capture;
	struct v4l2_fract *tpf = &cp->timeperframe;

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (cp->extendedmode != 0)
		return -EINVAL;

	if (tpf->numerator == 0 || tpf->denominator == 0
		|| (tpf->denominator > tpf->numerator * MAX_FRAME_RATE)) {
		/* reset to max frame rate */
		tpf->numerator = 1;
		tpf->denominator = MAX_FRAME_RATE;
	}
	sensor->frame_rate.numerator = tpf->denominator;
	sensor->frame_rate.denominator = tpf->numerator;
	vs6624_write(sd, VS6624_DISABLE_FR_DAMPER, 0x0);
	vs6624_write(sd, VS6624_FR_NUM_MSB,
			sensor->frame_rate.numerator >> 8);
	vs6624_write(sd, VS6624_FR_NUM_LSB,
			sensor->frame_rate.numerator & 0xFF);
	vs6624_write(sd, VS6624_FR_DEN,
			sensor->frame_rate.denominator & 0xFF);
	return 0;
}

static int vs6624_s_stream(struct v4l2_subdev *sd, int enable)
{
	if (enable)
		vs6624_write(sd, VS6624_USER_CMD, 0x2);
	else
		vs6624_write(sd, VS6624_USER_CMD, 0x4);
	udelay(100);
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int vs6624_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	reg->val = vs6624_read(sd, reg->reg & 0xffff);
	reg->size = 1;
	return 0;
}

static int vs6624_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	vs6624_write(sd, reg->reg & 0xffff, reg->val & 0xff);
	return 0;
}
#endif

static const struct v4l2_ctrl_ops vs6624_ctrl_ops = {
	.s_ctrl = vs6624_s_ctrl,
};

static const struct v4l2_subdev_core_ops vs6624_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = vs6624_g_register,
	.s_register = vs6624_s_register,
#endif
};

static const struct v4l2_subdev_video_ops vs6624_video_ops = {
	.s_parm = vs6624_s_parm,
	.g_parm = vs6624_g_parm,
	.s_stream = vs6624_s_stream,
};

static const struct v4l2_subdev_pad_ops vs6624_pad_ops = {
	.enum_mbus_code = vs6624_enum_mbus_code,
	.get_fmt = vs6624_get_fmt,
	.set_fmt = vs6624_set_fmt,
};

static const struct v4l2_subdev_ops vs6624_ops = {
	.core = &vs6624_core_ops,
	.video = &vs6624_video_ops,
	.pad = &vs6624_pad_ops,
};

static int vs6624_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct vs6624 *sensor;
	struct v4l2_subdev *sd;
	struct v4l2_ctrl_handler *hdl;
	const unsigned *ce;
	int ret;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;

	ce = client->dev.platform_data;
	if (ce == NULL)
		return -EINVAL;

	ret = devm_gpio_request_one(&client->dev, *ce, GPIOF_OUT_INIT_HIGH,
				    "VS6624 Chip Enable");
	if (ret) {
		v4l_err(client, "failed to request GPIO %d\n", *ce);
		return ret;
	}
	/* wait 100ms before any further i2c writes are performed */
	mdelay(100);

	sensor = devm_kzalloc(&client->dev, sizeof(*sensor), GFP_KERNEL);
	if (sensor == NULL)
		return -ENOMEM;

	sd = &sensor->sd;
	v4l2_i2c_subdev_init(sd, client, &vs6624_ops);

	vs6624_writeregs(sd, vs6624_p1);
	vs6624_write(sd, VS6624_MICRO_EN, 0x2);
	vs6624_write(sd, VS6624_DIO_EN, 0x1);
	mdelay(10);
	vs6624_writeregs(sd, vs6624_p2);

	vs6624_writeregs(sd, vs6624_default);
	vs6624_write(sd, VS6624_HSYNC_SETUP, 0xF);
	vs6624_writeregs(sd, vs6624_run_setup);

	/* set frame rate */
	sensor->frame_rate.numerator = MAX_FRAME_RATE;
	sensor->frame_rate.denominator = 1;
	vs6624_write(sd, VS6624_DISABLE_FR_DAMPER, 0x0);
	vs6624_write(sd, VS6624_FR_NUM_MSB,
			sensor->frame_rate.numerator >> 8);
	vs6624_write(sd, VS6624_FR_NUM_LSB,
			sensor->frame_rate.numerator & 0xFF);
	vs6624_write(sd, VS6624_FR_DEN,
			sensor->frame_rate.denominator & 0xFF);

	sensor->fmt = vs6624_default_fmt;
	sensor->ce_pin = *ce;

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	hdl = &sensor->hdl;
	v4l2_ctrl_handler_init(hdl, 4);
	v4l2_ctrl_new_std(hdl, &vs6624_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 0xFF, 1, 0x87);
	v4l2_ctrl_new_std(hdl, &vs6624_ctrl_ops,
			V4L2_CID_SATURATION, 0, 0xFF, 1, 0x78);
	v4l2_ctrl_new_std(hdl, &vs6624_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(hdl, &vs6624_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	/* hook the control handler into the driver */
	sd->ctrl_handler = hdl;
	if (hdl->error) {
		int err = hdl->error;

		v4l2_ctrl_handler_free(hdl);
		return err;
	}

	/* initialize the hardware to the default control values */
	ret = v4l2_ctrl_handler_setup(hdl);
	if (ret)
		v4l2_ctrl_handler_free(hdl);
	return ret;
}

static int vs6624_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	return 0;
}

static const struct i2c_device_id vs6624_id[] = {
	{"vs6624", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, vs6624_id);

static struct i2c_driver vs6624_driver = {
	.driver = {
		.name   = "vs6624",
	},
	.probe          = vs6624_probe,
	.remove         = vs6624_remove,
	.id_table       = vs6624_id,
};

module_i2c_driver(vs6624_driver);

MODULE_DESCRIPTION("VS6624 sensor driver");
MODULE_AUTHOR("Scott Jiang <Scott.Jiang.Linux@gmail.com>");
MODULE_LICENSE("GPL v2");
