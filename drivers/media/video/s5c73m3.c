/*
 * driver for LSI S5C73M3 (ISP for 8MP Camera)
 *
 * Copyright (c) 2011, Samsung Electronics. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <media/v4l2-device.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/videodev2.h>
#include <linux/unistd.h>

#include <plat/gpio-cfg.h>
#include <linux/gpio.h>

#define S5C73M3_BUSFREQ_OPP

#ifdef S5C73M3_BUSFREQ_OPP
#include <mach/dev.h>
#include <plat/cpu.h>
#endif

#ifdef CONFIG_VIDEO_SAMSUNG_V4L2
#include <linux/videodev2_exynos_media.h>
#include <linux/videodev2_exynos_camera.h>
#endif

#include <linux/regulator/machine.h>

#ifdef CONFIG_LEDS_AAT1290A
#include <linux/leds-aat1290a.h>
#endif

#include <media/s5c73m3_platform.h>
#include "s5c73m3.h"

#define S5C73M3_DRIVER_NAME	"S5C73M3"

extern struct class *camera_class; /*sys/class/camera*/
struct device *s5c73m3_dev; /*sys/class/camera/rear*/
struct v4l2_subdev *sd_internal;

#ifdef S5C73M3_BUSFREQ_OPP
struct device *bus_dev;
#endif

/*#define S5C73M3_FROM_BOOTING*/
#define S5C73M3_CORE_VDD	"/data/ISP_CV"
#define S5C73M3_FW_PATH		"/sdcard/SlimISP.bin"
#define S5C73M3_FW_VER_LEN		6
#define S5C73M3_FW_VER_FILE_CUR	0x60

#define S5C73M3_I2C_RETRY		5

#define CHECK_ERR(x)	if ((x) < 0) { \
				cam_err("i2c failed, err %d\n", x); \
				return x; \
			}
struct s5c73m3_fw_version camfw_info[S5C73M3_PATH_MAX];

#if defined(CONFIG_MACH_BAFFIN) && !defined(CONFIG_MACH_SUPERIOR_KOR_SKT)
/* for 5:3 WIDE RATIO */
static const struct s5c73m3_frmsizeenum preview_frmsizes[] = {
	{ S5C73M3_PREVIEW_QVGA,	320,	240,	0x01 },
	{ S5C73M3_PREVIEW_VGA,	640,	480,	0x02 },
	{ S5C73M3_PREVIEW_528X432,	528,	432,	0x03 },
	{ S5C73M3_PREVIEW_960X720,	960,	720,	0x04 },
	{ S5C73M3_PREVIEW_WVGA,	800,	480,	0x05 },
	{ S5C73M3_PREVIEW_720P,	1280,	720,	0x06 },
	{ S5C73M3_VDIS_720P,	1536,	864,	0x07 },
	{ S5C73M3_PREVIEW_800X600,	800,	600,	0x09 },
	{ S5C73M3_PREVIEW_1080P,	1920,	1080,	0x0A},
	{ S5C73M3_PREVIEW_D1,	720,	480,	0x0B },
	{ S5C73M3_VDIS_1080P,	2304,	1296,	0x0C},
	{ S5C73M3_PREVIEW_CIF,	352,	288,	0x0E },
	{ S5C73M3_PREVIEW_1008X672,	1008,	672,	0x0F },
};

static const struct s5c73m3_frmsizeenum capture_frmsizes[] = {
	{ S5C73M3_CAPTURE_VGA,	640,	480,	0x10 },
	{ S5C73M3_CAPTURE_WVGA,	800,	480,	0x20 },
	{ S5C73M3_CAPTURE_XGA,	1024,	768,	0x30 },
	{ S5C73M3_CAPTURE_WXGA,	1280,	768,	0x40 },
	{ S5C73M3_CAPTURE_1280X960,	1280,	960,	0x50 },
	{ S5C73M3_CAPTURE_W1MP,	1600,	960,	0x60 },
	{ S5C73M3_CAPTURE_2MP,	1600,	1200,	0x70 },
	{ S5C73M3_CAPTURE_2000X1200,	2000,	1200,	0x80 },
	{ S5C73M3_CAPTURE_2000X1500,	2000,	1500,	0x90 },
	{ S5C73M3_CAPTURE_W4MP,	2560,	1536,	0xA0 },
	{ S5C73M3_CAPTURE_5MP,	2560,	1920,	0xB0 },
	{ S5C73M3_CAPTURE_3264X2176,	3264,	2176,	0xC0 },
	{ S5C73M3_CAPTURE_3264X1960,	3264,	1960,	0xD0 },
	{ S5C73M3_CAPTURE_W6MP,	3264,	1836,	0xE0 },
	{ S5C73M3_CAPTURE_8MP,	3264,	2448,	0xF0 },
};
#else
static const struct s5c73m3_frmsizeenum preview_frmsizes[] = {
	{ S5C73M3_PREVIEW_QVGA,	320,	240,	0x01 },
	{ S5C73M3_PREVIEW_CIF,	352,	288,	0x0E },
	{ S5C73M3_PREVIEW_VGA,	640,	480,	0x02 },
	{ S5C73M3_PREVIEW_880X720,	880,	720,	0x03 },
	{ S5C73M3_PREVIEW_960X720,	960,	720,	0x04 },
	{ S5C73M3_PREVIEW_1008X672,	1008,	672,	0x0F },
	{ S5C73M3_PREVIEW_1184X666,	1184,	666,	0x05 },
	{ S5C73M3_PREVIEW_720P,	1280,	720,	0x06 },
#if defined(CONFIG_MACH_T0)
	{ S5C73M3_PREVIEW_1280X960,	1280,	960,	0x09 },
#else
	{ S5C73M3_PREVIEW_800X600,	800,	600,	0x09 },
#endif
	{ S5C73M3_VDIS_720P,	1536,	864,	0x07 },
	{ S5C73M3_PREVIEW_1080P,	1920,	1080,	0x0A},
	{ S5C73M3_VDIS_1080P,	2304,	1296,	0x0C},
	{ S5C73M3_PREVIEW_D1,	720,	480,	0x0B },
};

static const struct s5c73m3_frmsizeenum capture_frmsizes[] = {
	{ S5C73M3_CAPTURE_VGA,	640,	480,	0x10 },
	{ S5C73M3_CAPTURE_960x540,	960,	540,	0x20 },
	{ S5C73M3_CAPTURE_960x720,	960,	720,	0x30 },
	{ S5C73M3_CAPTURE_1024X768,	1024,	768,	0xD0 },
	{ S5C73M3_CAPTURE_HD,	1280,	720,	0x40 },
	{ S5C73M3_CAPTURE_2MP,	1600,	1200,	0x70 },
	{ S5C73M3_CAPTURE_W2MP,	2048,	1152,	0x80 },
	{ S5C73M3_CAPTURE_3MP,	2048,	1536,	0x90 },
	{ S5C73M3_CAPTURE_5MP,	2560,	1920,	0xB0 },
	{ S5C73M3_CAPTURE_W6MP,	3264,	1836,	0xE0 },
	{ S5C73M3_CAPTURE_3264X2176,	3264,	2176,	0xC0 },
	{ S5C73M3_CAPTURE_8MP,	3264,	2448,	0xF0 },
};
#endif

static const struct s5c73m3_effectenum s5c73m3_effects[] = {
	{IMAGE_EFFECT_NONE, S5C73M3_IMAGE_EFFECT_NONE},
	{IMAGE_EFFECT_NEGATIVE, S5C73M3_IMAGE_EFFECT_NEGATIVE},
	{IMAGE_EFFECT_AQUA, S5C73M3_IMAGE_EFFECT_AQUA},
	{IMAGE_EFFECT_SEPIA, S5C73M3_IMAGE_EFFECT_SEPIA},
	{IMAGE_EFFECT_BNW, S5C73M3_IMAGE_EFFECT_MONO},
	{IMAGE_EFFECT_SKETCH, S5C73M3_IMAGE_EFFECT_SKETCH},
	{IMAGE_EFFECT_WASHED, S5C73M3_IMAGE_EFFECT_WASHED},
	{IMAGE_EFFECT_VINTAGE_WARM, S5C73M3_IMAGE_EFFECT_VINTAGE_WARM},
	{IMAGE_EFFECT_VINTAGE_COLD, S5C73M3_IMAGE_EFFECT_VINTAGE_COLD},
	{IMAGE_EFFECT_SOLARIZE, S5C73M3_IMAGE_EFFECT_SOLARIZE},
	{IMAGE_EFFECT_POSTERIZE, S5C73M3_IMAGE_EFFECT_POSTERIZE},
	{IMAGE_EFFECT_POINT_BLUE, S5C73M3_IMAGE_EFFECT_POINT_BLUE},
	{IMAGE_EFFECT_POINT_RED_YELLOW, S5C73M3_IMAGE_EFFECT_POINT_RED_YELLOW},
	{IMAGE_EFFECT_POINT_COLOR_3, S5C73M3_IMAGE_EFFECT_POINT_COLOR_3},
	{IMAGE_EFFECT_POINT_GREEN, S5C73M3_IMAGE_EFFECT_POINT_GREEN},
	{IMAGE_EFFECT_CARTOONIZE, S5C73M3_IMAGE_EFFECT_CARTOONIZE},
};

static struct s5c73m3_control s5c73m3_ctrls[] = {
	{
		.id = V4L2_CID_CAMERA_ISO,
		.minimum = ISO_AUTO,
		.maximum = ISO_800,
		.step = 1,
		.value = ISO_AUTO,
		.default_value = ISO_AUTO,
	}, {
		.id = V4L2_CID_CAMERA_BRIGHTNESS,
		.minimum = EV_MINUS_4,
		.maximum = EV_MAX - 1,
		.step = 1,
		.value = EV_DEFAULT,
		.default_value = EV_DEFAULT,
	}, {
		.id = V4L2_CID_CAMERA_SATURATION,
		.minimum = SATURATION_MINUS_2,
		.maximum = SATURATION_MAX - 1,
		.step = 1,
		.value = SATURATION_DEFAULT,
		.default_value = SATURATION_DEFAULT,
	}, {
		.id = V4L2_CID_CAMERA_SHARPNESS,
		.minimum = SHARPNESS_MINUS_2,
		.maximum = SHARPNESS_MAX - 1,
		.step = 1,
		.value = SHARPNESS_DEFAULT,
		.default_value = SHARPNESS_DEFAULT,
	}, {
		.id = V4L2_CID_CAMERA_ZOOM,
		.minimum = ZOOM_LEVEL_0,
		.maximum = ZOOM_LEVEL_MAX - 1,
		.step = 1,
		.value = ZOOM_LEVEL_0,
		.default_value = ZOOM_LEVEL_0,
	}, {
		.id = V4L2_CID_CAM_JPEG_QUALITY,
		.minimum = 1,
		.maximum = 100,
		.step = 1,
		.value = 100,
		.default_value = 100,
	},
};

static u8 sysfs_sensor_fw[10] = {0,};
static u8 sysfs_phone_fw[10] = {0,};
static u8 sysfs_isp_core[10] = {0,};
static u8 data_memory[500000] = {0,};
static u32 crc_table[256] = {0,};
static int copied_fw_binary;

static u16 isp_chip_info1;
static u16 isp_chip_info2;
static u16 isp_chip_info3;

static int s5c73m3_s_stream_sensor(struct v4l2_subdev *sd, int onoff);
static int s5c73m3_set_touch_auto_focus(struct v4l2_subdev *sd);
static int s5c73m3_SPI_booting(struct v4l2_subdev *sd);
static int s5c73m3_get_af_cal_version(struct v4l2_subdev *sd);
static int s5c73m3_set_timing_register_for_vdd(struct v4l2_subdev *sd);

static inline struct s5c73m3_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5c73m3_state, sd);
}

static int s5c73m3_i2c_write(struct v4l2_subdev *sd,
	unsigned short addr, unsigned short data)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg;
	unsigned char buf[4];
	int i, err;

	if (!client->adapter)
		return -ENODEV;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	buf[0] = addr >> 8;
	buf[1] = addr & 0xff;
	buf[2] = data >> 8;
	buf[3] = data & 0xff;

	cam_i2c_dbg("addr %#x, data %#x\n", addr, data);

	for (i = S5C73M3_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	return err;
}

static int s5c73m3_i2c_write_block(struct v4l2_subdev *sd,
	const u32 regs[], int size)
{
	int i, err = 0;

	for (i = 0; i < size; i++) {
		err = s5c73m3_i2c_write(sd, (regs[i]>>16), regs[i]);
		CHECK_ERR(err);
	}

	return err;
}

static int s5c73m3_i2c_read(struct v4l2_subdev *sd,
	unsigned short addr, unsigned short *data)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg;
	unsigned char buf[2];
	int i, err;

	if (!client->adapter)
		return -ENODEV;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = sizeof(buf);
	msg.buf = buf;

	buf[0] = addr >> 8;
	buf[1] = addr & 0xff;

	for (i = S5C73M3_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	if (err != 1) {
		cam_err("addr %#x\n", addr);
		return err;
	}

	msg.flags = I2C_M_RD;

	for (i = S5C73M3_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	if (err != 1) {
		cam_err("addr %#x\n", addr);
		return err;
	}

	*data = ((buf[0] << 8) | buf[1]);

	return err;
}

static int s5c73m3_write(struct v4l2_subdev *sd,
	unsigned short addr1, unsigned short addr2, unsigned short data)
{
	int err;

	err = s5c73m3_i2c_write(sd, 0x0050, addr1);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0054, addr2);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, data);
	CHECK_ERR(err);

	return err;
}

static int s5c73m3_read(struct v4l2_subdev *sd,
	unsigned short addr1, unsigned short addr2, unsigned short *data)
{
	int err;

	err = s5c73m3_i2c_write(sd, 0xfcfc, 0x3310);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0058, addr1);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x005C, addr2);
	CHECK_ERR(err);

	err = s5c73m3_i2c_read(sd, 0x0F14, data);
	CHECK_ERR(err);

	return err;
}

static int s5c73m3_i2c_check_status_with_CRC(struct v4l2_subdev *sd)
{
	int err = 0;
	int index = 0;
	u16 status = 0;
	u16 i2c_status = 0;
	u16 i2c_seq_status = 0;

	do {
		err = s5c73m3_read(sd, 0x0009, S5C73M3_STATUS, &status);
		err = s5c73m3_read(sd, 0x0009,
			S5C73M3_I2C_ERR_STATUS, &i2c_status);
		if (i2c_status & ERROR_STATUS_CHECK_BIN_CRC) {
			cam_dbg("failed to check CRC value of ISP Ram\n");
			err = -1;
			break;
		}

		if (status == 0xffff)
			break;

		index++;
		udelay(500);
	} while (index < 2000);	/* 1 sec */

	if (index >= 2000) {
		err = s5c73m3_read(sd, 0x0009,
			S5C73M3_I2C_ERR_STATUS, &i2c_status);
		err = s5c73m3_read(sd, 0x0009,
			S5C73M3_I2C_SEQ_STATUS, &i2c_seq_status);
		cam_dbg("TimeOut!! index:%d,status:%#x,i2c_stauts:%#x,i2c_seq_status:%#x\n",
			index,
			status,
			i2c_status,
			i2c_seq_status);

		err = -1;
	}

	return err;
}

static int s5c73m3_i2c_check_status(struct v4l2_subdev *sd)
{
	int err = 0;
	int index = 0;
	u16 status = 0;
	u16 i2c_status = 0;
	u16 i2c_seq_status = 0;

	do {
		err = s5c73m3_read(sd, 0x0009, S5C73M3_STATUS, &status);
		if (status == 0xffff)
			break;

		index++;
		udelay(500);
	} while (index < 2000);	/* 1 sec */

	if (index >= 2000) {
		err = s5c73m3_read(sd, 0x0009,
			S5C73M3_I2C_ERR_STATUS, &i2c_status);
		err = s5c73m3_read(sd, 0x0009,
			S5C73M3_I2C_SEQ_STATUS, &i2c_seq_status);
		cam_dbg("TimeOut!! index:%d,status:%#x,i2c_stauts:%#x,i2c_seq_status:%#x\n",
			index,
			status,
			i2c_status,
			i2c_seq_status);

		err = -1;
	}

	return err;
}

void s5c73m3_make_CRC_table(u32 *table, u32 id)
{
	u32 i, j, k;

	for (i = 0; i < 256; ++i) {
		k = i;
		for (j = 0; j < 8; ++j) {
			if (k & 1)
				k = (k >> 1) ^ id;
			else
				k >>= 1;
		}
		table[i] = k;
	}
}

static int s5c73m3_reset_module(struct v4l2_subdev *sd, bool powerReset)
{
	struct s5c73m3_state *state = to_state(sd);
	int err = 0;

	cam_trace("E\n");

	if (powerReset) {
		err = state->pdata->power_on_off(0);
		CHECK_ERR(err);
		err = state->pdata->power_on_off(1);
		CHECK_ERR(err);
	} else {
		err = state->pdata->is_isp_reset();
		CHECK_ERR(err);
	}
	err = s5c73m3_set_timing_register_for_vdd(sd);
	CHECK_ERR(err);

	cam_trace("X\n");

	return err;
}

static int s5c73m3_writeb(struct v4l2_subdev *sd,
	unsigned short addr, unsigned short data)
{
	int err;
	err = s5c73m3_i2c_check_status(sd);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0050, 0x0009);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0054, 0x5000);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, addr);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, data);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0054, 0x5080);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, 0x0001);
	CHECK_ERR(err);

	return err;
}

static int s5c73m3_set_mode(struct v4l2_subdev *sd)
{
	struct s5c73m3_state *state = to_state(sd);
	int err;
	cam_trace("E\n");

	if (state->format_mode != V4L2_PIX_FMT_MODE_CAPTURE) {
		if (state->hdr_mode || state->yuv_snapshot) {
			err = s5c73m3_writeb(sd, S5C73M3_IMG_OUTPUT,
				S5C73M3_HDR_OUTPUT);
			CHECK_ERR(err);
			cam_dbg("hdr ouput mode\n");
		} else {
			err = s5c73m3_writeb(sd, S5C73M3_IMG_OUTPUT,
				S5C73M3_YUV_OUTPUT);
			CHECK_ERR(err);
			cam_dbg("yuv ouput mode\n");
		}
	} else {
		if (state->hybrid_mode) {
			err = s5c73m3_writeb(sd, S5C73M3_IMG_OUTPUT,
				S5C73M3_HYBRID_OUTPUT);
			CHECK_ERR(err);
			cam_dbg("hybrid ouput mode\n");
		} else {
			err = s5c73m3_writeb(sd, S5C73M3_IMG_OUTPUT,
				S5C73M3_INTERLEAVED_OUTPUT);
			CHECK_ERR(err);
			cam_dbg("interleaved ouput mode\n");
		}
	}

	cam_trace("X\n");
	return 0;
}

/*
 * v4l2_subdev_core_ops
 */
static int s5c73m3_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(s5c73m3_ctrls); i++) {
		if (qc->id == s5c73m3_ctrls[i].id) {
			qc->maximum = s5c73m3_ctrls[i].maximum;
			qc->minimum = s5c73m3_ctrls[i].minimum;
			qc->step = s5c73m3_ctrls[i].step;
			qc->default_value = s5c73m3_ctrls[i].default_value;
			return 0;
		}
	}

	return -EINVAL;
}

static int s5c73m3_set_antibanding(struct v4l2_subdev *sd, int val)
{
	int err = 0;
	int antibanding_mode = 0;

	switch (val) {
	case ANTI_BANDING_OFF:
		antibanding_mode = S5C73M3_FLICKER_NONE;
		break;
	case ANTI_BANDING_50HZ:
		antibanding_mode = S5C73M3_FLICKER_AUTO_50HZ;
		break;
	case ANTI_BANDING_60HZ:
		antibanding_mode = S5C73M3_FLICKER_AUTO_60HZ;
		break;
	case ANTI_BANDING_AUTO:
	default:
		antibanding_mode = S5C73M3_FLICKER_AUTO;
		break;

	}

	err = s5c73m3_writeb(sd, S5C73M3_FLICKER_MODE, antibanding_mode);
	CHECK_ERR(err);

	return err;
}

static int s5c73m3_set_af_softlanding(struct v4l2_subdev *sd)
{
	int err = 0;

	cam_trace("E\n");

	err = s5c73m3_writeb(sd, S5C73M3_AF_SOFTLANDING,
		S5C73M3_AF_SOFTLANDING_ON);
	CHECK_ERR(err);
	cam_trace("X\n");

	return 0;
}

static int s5c73m3_dump_fw(struct v4l2_subdev *sd)
{
	return 0;
}

static int s5c73m3_get_sensor_fw_binary(struct v4l2_subdev *sd)
{
	struct s5c73m3_state *state = to_state(sd);
	u16 read_val;
	int i, rxSize;
	int err = 0;
	struct file *fp = NULL;
	mm_segment_t old_fs;
	long ret = 0;
	char fw_path[25] = {0,};
	u8 mem0 = 0, mem1 = 0;
	u32 CRC = 0;
	u32 DataCRC = 0;
	u32 IntOriginalCRC = 0;
	u32 crc_index = 0;
	int retryCnt = 2;

#if defined(CONFIG_MACH_T0)
	if (state->sensor_fw[1] == 'D') {
		sprintf(fw_path, "/data/cfw/SlimISP_%cK.bin",
			state->sensor_fw[0]);
	} else {
		sprintf(fw_path, "/data/cfw/SlimISP_%c%c.bin",
			state->sensor_fw[0],
			state->sensor_fw[1]);
	}
#elif defined(CONFIG_MACH_BAFFIN)
	if (state->sensor_fw[1] == 'D') {
		sprintf(fw_path, "/data/cfw/SlimISP_%cK.bin",
			state->sensor_fw[0]);
	} else if (state->sensor_fw[1] == 'H') {
		sprintf(fw_path, "/data/cfw/SlimISP_%cM.bin",
			state->sensor_fw[0]);
	} else {
		sprintf(fw_path, "/data/cfw/SlimISP_%c%c.bin",
			state->sensor_fw[0],
			state->sensor_fw[1]);
	}
#else
	if (state->sensor_fw[0] == 'O') {
		sprintf(fw_path, "/data/cfw/SlimISP_G%c.bin",
			state->sensor_fw[1]);
	} else if (state->sensor_fw[0] == 'S') {
		sprintf(fw_path, "/data/cfw/SlimISP_Z%c.bin",
			state->sensor_fw[1]);
	} else {
	sprintf(fw_path, "/data/cfw/SlimISP_%c%c.bin",
		state->sensor_fw[0],
		state->sensor_fw[1]);
	}
#endif

	/* Make CRC Table */
	s5c73m3_make_CRC_table((u32 *)&crc_table, 0xEDB88320);

	/*ARM go*/
	err = s5c73m3_write(sd, 0x3000, 0x0004, 0xFFFF);
	CHECK_ERR(err);

	udelay(400);

	/*Check boot done*/
	for (i = 0; i < 3; i++) {
		err = s5c73m3_read(sd, 0x3010, 0x0010, &read_val);
		CHECK_ERR(err);

		if (read_val == 0x0C)
			break;

		udelay(100);
	}

	if (read_val != 0x0C) {
		cam_err("boot fail, read_val %#x\n", read_val);
		return -1;
	}

	/* Change I/O Driver Current in order to read from F-ROM */
	err = s5c73m3_write(sd, 0x3010, 0x0120, 0x0820);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3010, 0x0124, 0x0820);
	CHECK_ERR(err);

	/*P,M,S and Boot Mode*/
	err = s5c73m3_write(sd, 0x3010, 0x0014, 0x2146);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3010, 0x0010, 0x230C);
	CHECK_ERR(err);

	udelay(200);

	/*Check SPI ready*/
	for (i = 0; i < 300; i++) {
		err = s5c73m3_read(sd, 0x3010, 0x0010, &read_val);
		CHECK_ERR(err);

		if (read_val == 0x230E)
			break;

		udelay(100);
	}

	if (read_val != 0x230E) {
		cam_err("SPI not ready, read_val %#x\n", read_val);
		return -1;
	}

	/*ARM reset*/
	err = s5c73m3_write(sd, 0x3000, 0x0004, 0xFFFD);
	CHECK_ERR(err);

	/*remap*/
	err = s5c73m3_write(sd, 0x3010, 0x00A4, 0x0183);
	CHECK_ERR(err);

	/*ARM go again*/
	err = s5c73m3_write(sd, 0x3000, 0x0004, 0xFFFF);
	CHECK_ERR(err);

	mdelay(200);

retry:
	memset(data_memory, 0, sizeof(data_memory));
	mem0 = 0, mem1 = 0;
	CRC = 0;
	DataCRC = 0;
	IntOriginalCRC = 0;
	crc_index = 0;

	/* SPI Copy mode ready I2C CMD */
	err = s5c73m3_writeb(sd, 0x0924, 0x0000);
	CHECK_ERR(err);
	cam_dbg("sent SPI ready CMD\n");

	rxSize = 64*1024;
	mdelay(10);
	s5c73m3_i2c_check_status(sd);

	err = s5c73m3_spi_read((char *)&data_memory,
		state->sensor_size, rxSize);
	CHECK_ERR(err);

	CRC = ~CRC;
	for (crc_index = 0; crc_index < (state->sensor_size-4)/2; crc_index++) {
		/*low byte*/
		mem0 = (unsigned char)(data_memory[crc_index*2] & 0x00ff);
		/*high byte*/
		mem1 = (unsigned char)(data_memory[crc_index*2+1] & 0x00ff);
		CRC = crc_table[(CRC ^ (mem0)) & 0xFF] ^ (CRC >> 8);
		CRC = crc_table[(CRC ^ (mem1)) & 0xFF] ^ (CRC >> 8);
	}
	CRC = ~CRC;

	DataCRC = (CRC&0x000000ff)<<24;
	DataCRC += (CRC&0x0000ff00)<<8;
	DataCRC += (CRC&0x00ff0000)>>8;
	DataCRC += (CRC&0xff000000)>>24;
	cam_err("made CSC value by S/W = 0x%x\n", DataCRC);

	IntOriginalCRC = (data_memory[state->sensor_size-4]&0x00ff)<<24;
	IntOriginalCRC += (data_memory[state->sensor_size-3]&0x00ff)<<16;
	IntOriginalCRC += (data_memory[state->sensor_size-2]&0x00ff)<<8;
	IntOriginalCRC += (data_memory[state->sensor_size-1]&0x00ff);
	cam_err("Original CRC Int = 0x%x\n", IntOriginalCRC);

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	if (IntOriginalCRC == DataCRC) {
		fp = filp_open(fw_path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
		if (IS_ERR(fp) || fp == NULL) {
			cam_err("failed to open %s, err %ld\n",
				fw_path, PTR_ERR(fp));
			err = -EINVAL;
			goto out;
		}

		ret = vfs_write(fp, (char __user *)data_memory,
			state->sensor_size, &fp->f_pos);

		if (camfw_info[S5C73M3_SD_CARD].opened == 0) {
			memcpy(state->phone_fw,
				state->sensor_fw,
				S5C73M3_FW_VER_LEN);
			state->phone_fw[S5C73M3_FW_VER_LEN+1] = ' ';

			memcpy(sysfs_phone_fw,
				state->phone_fw,
				sizeof(state->phone_fw));
			cam_dbg("Changed to Phone_version = %s\n",
				state->phone_fw);
		}
	} else {
		if (retryCnt > 0) {
			set_fs(old_fs);
			retryCnt--;
			goto retry;
		}
	}

	if (fp != NULL)
		filp_close(fp, current->files);

out:
	set_fs(old_fs);
	return err;
}

static int s5c73m3_get_sensor_fw_version(struct v4l2_subdev *sd)
{
	struct s5c73m3_state *state = to_state(sd);
	u16 read_val;
	u16 sensor_fw;
	u16 sensor_type;
	u16 temp_buf;
	int i;
	int err = 0;

	/*ARM go*/
	err = s5c73m3_write(sd, 0x3000, 0x0004, 0xFFFF);
	CHECK_ERR(err);

	udelay(400);

	/*Check boot done*/
	for (i = 0; i < 3; i++) {
		err = s5c73m3_read(sd, 0x3010, 0x0010, &read_val);
		CHECK_ERR(err);

		if (read_val == 0x0C)
			break;

		udelay(100);
	}

	if (read_val != 0x0C) {
		cam_err("boot fail, read_val %#x\n", read_val);
		return -1;
	}

	/* Change I/O Driver Current in order to read from F-ROM */
	err = s5c73m3_write(sd, 0x3010, 0x0120, 0x0820);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3010, 0x0124, 0x0820);
	CHECK_ERR(err);

	/* Offset Setting */
	err = s5c73m3_write(sd, 0x0001, 0x0418, 0x0008);
	CHECK_ERR(err);

	/*P,M,S and Boot Mode*/
	err = s5c73m3_write(sd, 0x3010, 0x0014, 0x2146);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3010, 0x0010, 0x230C);
	CHECK_ERR(err);

	udelay(200);

	/*Check SPI ready*/
	for (i = 0; i < 300; i++) {
		err = s5c73m3_read(sd, 0x3010, 0x0010, &read_val);
		CHECK_ERR(err);

		if (read_val == 0x230E)
			break;

		udelay(100);
	}

	if (read_val != 0x230E) {
		cam_err("SPI not ready, read_val %#x\n", read_val);
		return -1;
	}

	/*ARM reset*/
	err = s5c73m3_write(sd, 0x3000, 0x0004, 0xFFFD);
	CHECK_ERR(err);

	/*remap*/
	err = s5c73m3_write(sd, 0x3010, 0x00A4, 0x0183);
	CHECK_ERR(err);

	for (i = 0; i < 3; i++) {
		err = s5c73m3_read(sd, 0x0000, i*2, &sensor_fw);
		CHECK_ERR(err);
		state->sensor_fw[i*2] = sensor_fw&0x00ff;
		state->sensor_fw[i*2+1] = (sensor_fw&0xff00)>>8;
#ifdef FEATURE_DEBUG_DUMP
		cam_err("0x%x\n", sensor_fw);
#endif
	}
	state->sensor_fw[i*2+2] = ' ';

	state->sensor_size = 0;
	for (i = 0; i < 2; i++) {
		err = s5c73m3_read(sd, 0x0000, 0x0014+i*2, &temp_buf);
		state->sensor_size += temp_buf<<(i*16);
		CHECK_ERR(err);
	}

	memcpy(sysfs_sensor_fw, state->sensor_fw,
		sizeof(state->sensor_fw));

	cam_dbg("Sensor_version = %s\n", state->sensor_fw);

	if ((state->sensor_fw[0] < 'A') || state->sensor_fw[0] > 'Z') {
		cam_dbg("Sensor Version is invalid data\n");
#ifdef FEATURE_DEBUG_DUMP
		cam_err("0000h : ");
		for (i = 0; i < 0x20; i++) {
			err = s5c73m3_read(sd, 0x0000, i*2, &sensor_fw);
			cam_err("%x", sensor_fw);

			if (i == 0x10)
				cam_err("\n 0010h : ");
		}
		mdelay(50);
		memcpy(sysfs_sensor_fw,
			state->sensor_fw,
			0x100000); /* for kernel panic */
#endif
		err = -1;
	}

	return err;
}

static int s5c73m3_open_firmware_file(struct v4l2_subdev *sd,
	const char *filename, u8 *buf, u16 offset, u16 size) {
	struct file *fp;
	int err = 0;
	mm_segment_t old_fs;
	long nread;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(filename, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		err = -ENOENT;
		goto out;
	} else {
		cam_dbg("%s is opened\n", filename);
	}

	err = vfs_llseek(fp, offset, SEEK_SET);
	if (err < 0) {
		cam_warn("failed to fseek, %d\n", err);
		goto out;
	}

	nread = vfs_read(fp, (char __user *)buf, size, &fp->f_pos);

	if (nread != size) {
		cam_err("failed to read firmware file, %ld Bytes\n", nread);
		err = -EIO;
		goto out;
	}
out:
	if (!IS_ERR(fp))
		filp_close(fp, current->files);

	set_fs(old_fs);

	return err;
}

static int s5c73m3_compare_date(struct v4l2_subdev *sd,
	int index1, int index2)
{
	u8 date1[5] = {0,};
	u8 date2[5] = {0,};

	strncpy((char *)&date1, &camfw_info[index1].ver[2], 4);
	strncpy((char *)&date2, &camfw_info[index2].ver[2], 4);
	cam_dbg("date1 = %s, date2 = %s\n, compare result = %d",
		date1,
		date2,
		strcmp((char *)&date1, (char *)&date2));

	return strcmp((char *)&date1, (char *)&date2);
}

static int s5c73m3_get_phone_fw_version(struct v4l2_subdev *sd)
{
	struct device *dev = sd->v4l2_dev->dev;
	struct s5c73m3_state *state = to_state(sd);
	const struct firmware *fw = {0, };
	char fw_path[20] = {0,};
	char fw_path_in_data[25] = {0,};
	u8 *buf = NULL;
	int err = 0;
	int retVal = 0;
	int fw_requested = 1;

#if defined(CONFIG_MACH_T0)
	if (state->sensor_fw[1] == 'D') {
		sprintf(fw_path, "SlimISP_%cK.bin",
			state->sensor_fw[0]);
	} else {
		sprintf(fw_path, "SlimISP_%c%c.bin",
			state->sensor_fw[0],
			state->sensor_fw[1]);
	}
#elif defined(CONFIG_MACH_BAFFIN)
	if (state->sensor_fw[1] == 'D') {
		sprintf(fw_path, "SlimISP_%cK.bin",
			state->sensor_fw[0]);
	} else if (state->sensor_fw[1] == 'H') {
		sprintf(fw_path, "SlimISP_%cM.bin",
			state->sensor_fw[0]);
	} else {
		sprintf(fw_path, "SlimISP_%c%c.bin",
			state->sensor_fw[0],
			state->sensor_fw[1]);
	}
#else
	if (state->sensor_fw[0] == 'O') {
		sprintf(fw_path, "SlimISP_G%c.bin",
			state->sensor_fw[1]);
	} else if (state->sensor_fw[0] == 'S') {
		sprintf(fw_path, "SlimISP_Z%c.bin",
			state->sensor_fw[1]);
	} else {
		sprintf(fw_path, "SlimISP_%c%c.bin",
			state->sensor_fw[0],
			state->sensor_fw[1]);
	}
#endif

	sprintf(fw_path_in_data, "/data/cfw/%s",
		fw_path);

	buf = vmalloc(S5C73M3_FW_VER_LEN+1);
	if (!buf) {
		cam_err("failed to allocate memory\n");
		err = -ENOMEM;
		goto out;
	}

	retVal = s5c73m3_open_firmware_file(sd,
		S5C73M3_FW_PATH,
		buf,
		S5C73M3_FW_VER_FILE_CUR,
		S5C73M3_FW_VER_LEN);
	if (retVal >= 0) {
		camfw_info[S5C73M3_SD_CARD].opened = 1;
		memcpy(camfw_info[S5C73M3_SD_CARD].ver,
			buf,
			S5C73M3_FW_VER_LEN);
		camfw_info[S5C73M3_SD_CARD]
			.ver[S5C73M3_FW_VER_LEN+1] = ' ';
		state->fw_index = S5C73M3_SD_CARD;
		fw_requested = 0;
	}
request_fw:
	if (fw_requested) {
		/* check fw in data folder */
		retVal = s5c73m3_open_firmware_file(sd,
			fw_path_in_data,
			buf,
			S5C73M3_FW_VER_FILE_CUR,
			S5C73M3_FW_VER_LEN);
		if (retVal >= 0) {
			camfw_info[S5C73M3_IN_DATA].opened = 1;
			memcpy(camfw_info[S5C73M3_IN_DATA].ver,
				buf,
				S5C73M3_FW_VER_LEN);
			camfw_info[S5C73M3_IN_DATA]
				.ver[S5C73M3_FW_VER_LEN+1] = ' ';
		}

		/* check fw in system folder */
		retVal = request_firmware(&fw, fw_path, dev);
		if (retVal == 0) {
			camfw_info[S5C73M3_IN_SYSTEM].opened = 1;
			memcpy(camfw_info[S5C73M3_IN_SYSTEM].ver,
				(u8 *)&fw->data[S5C73M3_FW_VER_FILE_CUR],
				S5C73M3_FW_VER_LEN);
		}

		/* compare */
		if (camfw_info[S5C73M3_IN_DATA].opened == 0 &&
			camfw_info[S5C73M3_IN_SYSTEM].opened == 1)  {
			state->fw_index = S5C73M3_IN_SYSTEM;
		} else if (camfw_info[S5C73M3_IN_DATA].opened == 1 &&
			camfw_info[S5C73M3_IN_SYSTEM].opened == 0) {
			state->fw_index = S5C73M3_IN_DATA;
		} else if (camfw_info[S5C73M3_IN_DATA].opened == 1 &&
			camfw_info[S5C73M3_IN_SYSTEM].opened == 1) {
			retVal = s5c73m3_compare_date(sd,
				S5C73M3_IN_DATA,
				S5C73M3_IN_SYSTEM);
			if (retVal <= 0) {
				/*unlink(&fw_path_in_data);*/
				state->fw_index = S5C73M3_IN_SYSTEM;
			} else {
				state->fw_index = S5C73M3_IN_DATA;
			}
		} else {
			cam_dbg("can't open %s Ver. Firmware. so, download from F-ROM\n",
				state->sensor_fw);
			if (fw != NULL)
				release_firmware(fw);

			retVal = s5c73m3_reset_module(sd, true);
			CHECK_ERR(retVal);
			retVal = s5c73m3_get_sensor_fw_binary(sd);
			CHECK_ERR(retVal);
			copied_fw_binary = 1;
			goto request_fw;
		}
	}

	if (!copied_fw_binary) {
		memcpy(state->phone_fw,
			camfw_info[state->fw_index].ver,
			S5C73M3_FW_VER_LEN);
		state->phone_fw[S5C73M3_FW_VER_LEN+1] = ' ';
	}

	memcpy(sysfs_phone_fw,
		state->phone_fw,
		sizeof(state->phone_fw));
	cam_dbg("Phone_version = %s(index=%d)\n",
		state->phone_fw, state->fw_index);

out:
	if (buf != NULL)
		vfree(buf);

	if (fw_requested)
		release_firmware(fw);

	return err;
}

static int s5c73m3_update_camerafw_to_FROM(struct v4l2_subdev *sd)
{
	int err;
	int index = 0;
	u16 status = 0;

	do {
		/* stauts 0 : not ready ISP */
		if (status == 0) {
			err = s5c73m3_writeb(sd, 0x0906, 0x0000);
			CHECK_ERR(err);
		}

		err = s5c73m3_read(sd, 0x0009, 0x5906, &status);
		/* Success : 0x05, Fail : 0x07 , Progressing : 0xFFFF*/
		if (status == 0x0005 ||
			status == 0x0007)
			break;

		index++;
		msleep(20);
	} while (index < 500);	/* 10 sec */


	if (status == 0x0007)
		return -1;
	else
		return 0;
}

static int s5c73m3_SPI_booting_by_ISP(struct v4l2_subdev *sd)
{
	u16 read_val;
	int i;
	int err = 0;

	/*ARM go*/
	err = s5c73m3_write(sd, 0x3000, 0x0004, 0xFFFF);
	CHECK_ERR(err);

	udelay(400);

	/*Check boot done*/
	for (i = 0; i < 3; i++) {
		err = s5c73m3_read(sd, 0x3010, 0x0010, &read_val);
		CHECK_ERR(err);

		if (read_val == 0x0C)
			break;

		udelay(100);
	}

	if (read_val != 0x0C) {
		cam_err("boot fail, read_val %#x\n", read_val);
		return -1;
	}

	/* Change I/O Driver Current in order to read from F-ROM */
	err = s5c73m3_write(sd, 0x3010, 0x0120, 0x0820);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3010, 0x0124, 0x0820);
	CHECK_ERR(err);

	/*P,M,S and Boot Mode*/
	err = s5c73m3_write(sd, 0x3010, 0x0014, 0x2146);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3010, 0x0010, 0x230C);
	CHECK_ERR(err);

	udelay(200);

	/*Check SPI ready*/
	for (i = 0; i < 300; i++) {
		err = s5c73m3_read(sd, 0x3010, 0x0010, &read_val);
		CHECK_ERR(err);

		if (read_val == 0x230E)
			break;

		udelay(100);
	}

	if (read_val != 0x230E) {
		cam_err("SPI not ready, read_val %#x\n", read_val);
		return -1;
	}

	/*ARM reset*/
	err = s5c73m3_write(sd, 0x3000, 0x0004, 0xFFFD);
	CHECK_ERR(err);

	/*remap*/
	err = s5c73m3_write(sd, 0x3010, 0x00A4, 0x0183);
	CHECK_ERR(err);

	/*ARM go*/
	err = s5c73m3_write(sd, 0x3000, 0x0004, 0xFFFF);
	CHECK_ERR(err);

	return err;
}

static int s5c73m3_check_fw_date(struct v4l2_subdev *sd)
{
	struct s5c73m3_state *state = to_state(sd);
	u8 sensor_date[5] = {0,};
	u8 phone_date[5] = {0,};

	strncpy((char *)&sensor_date, &state->sensor_fw[2], 4);
	strncpy((char *)&phone_date, (const char *)&state->phone_fw[2], 4);
	cam_dbg("Sensor_date = %s, Phone_date = %s\n, compare result = %d",
		sensor_date,
		phone_date,
		strcmp((char *)&sensor_date, (char *)&phone_date));

#if defined(CONFIG_MACH_T0)
	if (state->sensor_fw[1] == 'D')
		return -1;
	else
		return strcmp((char *)&sensor_date, (char *)&phone_date);
#elif defined(CONFIG_MACH_BAFFIN)
	if (state->sensor_fw[1] == 'D' || state->sensor_fw[1] == 'H')
		return -1;
	else
		return strcmp((char *)&sensor_date, (char *)&phone_date);
#else
	return strcmp((char *)&sensor_date, (char *)&phone_date);
#endif
}

static int s5c73m3_check_fw(struct v4l2_subdev *sd, int download)
{
	int err, i;
	int retVal;

	copied_fw_binary = 0;

	if (!download) {
		for (i = 0; i < S5C73M3_PATH_MAX; i++)
			camfw_info[i].opened = 0;

		err = s5c73m3_get_sensor_fw_version(sd);
		CHECK_ERR(err);
		s5c73m3_get_af_cal_version(sd);
		err = s5c73m3_get_phone_fw_version(sd);
		CHECK_ERR(err);
	}

	retVal = s5c73m3_check_fw_date(sd);

	/* retVal = 0 : Same Version
	retVal < 0 : Phone Version is latest Version than sensorFW.
	retVal > 0 : Sensor Version is latest version than phoenFW. */
	if (retVal <= 0 || download) {
		cam_dbg("Loading From PhoneFW......\n");

		/* In case that there is no FW in phone and FW needs to be
		downloaded from F-ROM, ISP power reset is required before
		loading FW to ISP for F-ROM to work properly.*/
		if (copied_fw_binary)
			err = s5c73m3_reset_module(sd, true);
		else
			err = s5c73m3_reset_module(sd, false);
		CHECK_ERR(err);

		err = s5c73m3_SPI_booting(sd);
		CHECK_ERR(err);

		if (download) {
			err = s5c73m3_update_camerafw_to_FROM(sd);
			CHECK_ERR(err);
		}
	} else {
		cam_dbg("Loading From SensorFW......\n");
		err = s5c73m3_reset_module(sd, true);
		CHECK_ERR(err);
		err = s5c73m3_get_sensor_fw_binary(sd);
		CHECK_ERR(err);
	}


	return 0;
}

static int s5c73m3_set_sensor_mode(struct v4l2_subdev *sd, int val)
{
	struct s5c73m3_state *state = to_state(sd);
	int err;
	cam_dbg("E, value %d\n", val);

retry:
	switch (val) {
	case SENSOR_CAMERA:
		err = s5c73m3_writeb(sd, S5C73M3_AE_MODE,
			S5C73M3_AUTO_MODE_AE_SET);
		CHECK_ERR(err);
		break;

	case SENSOR_MOVIE:
		err = s5c73m3_writeb(sd, S5C73M3_AE_MODE,
			S5C73M3_FIXED_30FPS);
		CHECK_ERR(err);
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = SENSOR_CAMERA;
		goto retry;
	}
	state->sensor_mode = val;

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_set_flash(struct v4l2_subdev *sd, int val, int recording)
{
	struct s5c73m3_state *state = to_state(sd);
	int err;
	u16 pre_flash = false;
	cam_dbg("E, value %d\n", val);

	s5c73m3_read(sd, 0x0009, S5C73M3_STILL_PRE_FLASH | 0x5000, &pre_flash);
	if (pre_flash) {
		err = s5c73m3_writeb(sd, S5C73M3_STILL_MAIN_FLASH
			, S5C73M3_STILL_MAIN_FLASH_CANCEL);
		CHECK_ERR(err);
		state->isflash = S5C73M3_ISNEED_FLASH_UNDEFINED;
	}

retry:
	switch (val) {
	case FLASH_MODE_OFF:
		err = s5c73m3_writeb(sd, S5C73M3_FLASH_MODE,
			S5C73M3_FLASH_MODE_OFF);
		CHECK_ERR(err);
		err = s5c73m3_writeb(sd, S5C73M3_FLASH_TORCH,
			S5C73M3_FLASH_TORCH_OFF);
		CHECK_ERR(err);
		break;

	case FLASH_MODE_AUTO:
		err = s5c73m3_writeb(sd, S5C73M3_FLASH_TORCH,
			S5C73M3_FLASH_TORCH_OFF);
		CHECK_ERR(err);
		err = s5c73m3_writeb(sd, S5C73M3_FLASH_MODE,
			S5C73M3_FLASH_MODE_AUTO);
		CHECK_ERR(err);
		break;

	case FLASH_MODE_ON:
		err = s5c73m3_writeb(sd, S5C73M3_FLASH_TORCH,
			S5C73M3_FLASH_TORCH_OFF);
		CHECK_ERR(err);
		err = s5c73m3_writeb(sd, S5C73M3_FLASH_MODE,
			S5C73M3_FLASH_MODE_ON);
		CHECK_ERR(err);
		break;

	case FLASH_MODE_TORCH:
		err = s5c73m3_writeb(sd, S5C73M3_FLASH_MODE,
			S5C73M3_FLASH_MODE_OFF);
		CHECK_ERR(err);
		err = s5c73m3_writeb(sd, S5C73M3_FLASH_TORCH,
			S5C73M3_FLASH_TORCH_ON);
		CHECK_ERR(err);
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = FLASH_MODE_OFF;
		goto retry;
	}
	state->flash_mode = val;

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_set_iso(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int err;
	cam_dbg("E, value %d\n", ctrl->value);

retry:
	switch (ctrl->value) {
	case ISO_AUTO:
		err = s5c73m3_writeb(sd, S5C73M3_ISO,
			S5C73M3_ISO_AUTO);
		CHECK_ERR(err);
		break;

	case ISO_100:
		err = s5c73m3_writeb(sd, S5C73M3_ISO,
			S5C73M3_ISO_100);
		CHECK_ERR(err);
		break;

	case ISO_200:
		err = s5c73m3_writeb(sd, S5C73M3_ISO,
			S5C73M3_ISO_200);
		CHECK_ERR(err);
		break;

	case ISO_400:
		err = s5c73m3_writeb(sd, S5C73M3_ISO,
			S5C73M3_ISO_400);
		CHECK_ERR(err);
		break;

	case ISO_800:
		err = s5c73m3_writeb(sd, S5C73M3_ISO,
			S5C73M3_ISO_800);
		CHECK_ERR(err);
		break;

	default:
		cam_warn("invalid value, %d\n", ctrl->value);
		ctrl->value = ISO_AUTO;
		goto retry;
	}

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_set_metering(struct v4l2_subdev *sd, int val)
{
	int err;
	cam_dbg("E, value %d\n", val);

retry:
	switch (val) {
	case METERING_CENTER:
		err = s5c73m3_writeb(sd, S5C73M3_METER,
			S5C73M3_METER_CENTER);
		CHECK_ERR(err);
		break;

	case METERING_SPOT:
		err = s5c73m3_writeb(sd, S5C73M3_METER,
			S5C73M3_METER_SPOT);
		CHECK_ERR(err);
		break;

	case METERING_MATRIX:
		err = s5c73m3_writeb(sd, S5C73M3_METER,
			S5C73M3_METER_AVERAGE);
		CHECK_ERR(err);
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = METERING_CENTER;
		goto retry;
	}

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_set_exposure(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	int err;
	cam_dbg("E, value %d\n", ctrl->value);

	if (ctrl->value < -4 || ctrl->value > 4) {
		cam_warn("invalid value, %d\n", ctrl->value);
		ctrl->value = 0;
	}
	err = s5c73m3_writeb(sd, S5C73M3_EV,
		ctrl->value + 4);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_set_contrast(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	int err;
	int contrast = 0;
	int temp_contrast = 0;
	cam_dbg("E, value %d\n", ctrl->value);

	if (ctrl->value < 0 || ctrl->value > 4) {
		cam_warn("invalid value, %d\n", ctrl->value);
		ctrl->value = 2;
	}
	temp_contrast = ctrl->value - 2;
	if (temp_contrast < 0)
		contrast = (temp_contrast * (-1)) + 2;
	else
		contrast = temp_contrast;
	err = s5c73m3_writeb(sd, S5C73M3_CONTRAST,
		contrast);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_set_sharpness(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	int err;
	int sharpness = 0;
	int temp_sharpness = 0;
	cam_dbg("E, value %d\n", ctrl->value);

	if (ctrl->value < 0 || ctrl->value > 4) {
		cam_warn("invalid value, %d\n", ctrl->value);
		ctrl->value = 2;
	}
	temp_sharpness = ctrl->value - 2;
	if (temp_sharpness < 0)
		sharpness = (temp_sharpness * (-1)) + 2;
	else
		sharpness = temp_sharpness;
	err = s5c73m3_writeb(sd, S5C73M3_SHARPNESS,
		sharpness);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_set_whitebalance(struct v4l2_subdev *sd, int val)
{
	struct s5c73m3_state *state = to_state(sd);
	int err;
	cam_dbg("E, value %d\n", val);

retry:
	switch (val) {
	case WHITE_BALANCE_AUTO:
		err = s5c73m3_writeb(sd, S5C73M3_AWB_MODE,
			S5C73M3_AWB_MODE_AUTO);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_SUNNY:
		err = s5c73m3_writeb(sd, S5C73M3_AWB_MODE,
			S5C73M3_AWB_MODE_DAYLIGHT);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_CLOUDY:
		err = s5c73m3_writeb(sd, S5C73M3_AWB_MODE,
			S5C73M3_AWB_MODE_CLOUDY);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_TUNGSTEN:
		err = s5c73m3_writeb(sd, S5C73M3_AWB_MODE,
			S5C73M3_AWB_MODE_INCANDESCENT);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_FLUORESCENT:
		err = s5c73m3_writeb(sd, S5C73M3_AWB_MODE,
			S5C73M3_AWB_MODE_FLUORESCENT1);
		CHECK_ERR(err);
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = WHITE_BALANCE_AUTO;
		goto retry;
	}

	state->wb_mode = val;

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_set_scene_mode(struct v4l2_subdev *sd, int val)
{
	struct s5c73m3_state *state = to_state(sd);
	int err;
	cam_dbg("E, value %d\n", val);

retry:
	switch (val) {
	case SCENE_MODE_NONE:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_NONE);
		CHECK_ERR(err);
		break;

	case SCENE_MODE_PORTRAIT:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_PORTRAIT);
		CHECK_ERR(err);
		break;

	case SCENE_MODE_LANDSCAPE:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_LANDSCAPE);
		CHECK_ERR(err);
		break;

	case SCENE_MODE_SPORTS:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_SPORTS);
		CHECK_ERR(err);
		break;

	case SCENE_MODE_PARTY_INDOOR:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_INDOOR);
		CHECK_ERR(err);
		break;

	case SCENE_MODE_BEACH_SNOW:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_BEACH);
		CHECK_ERR(err);
		break;

	case SCENE_MODE_SUNSET:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_SUNSET);
		CHECK_ERR(err);
		break;

	case SCENE_MODE_DUSK_DAWN:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_DAWN);
		CHECK_ERR(err);
		break;

	case SCENE_MODE_FALL_COLOR:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_FALL);
		CHECK_ERR(err);
		break;

	case SCENE_MODE_NIGHTSHOT:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_NIGHT);
		CHECK_ERR(err);
		break;

	case SCENE_MODE_BACK_LIGHT:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_AGAINSTLIGHT);
		CHECK_ERR(err);
		break;

	case SCENE_MODE_FIREWORKS:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_FIRE);
		CHECK_ERR(err);
		break;

	case SCENE_MODE_TEXT:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_TEXT);
		CHECK_ERR(err);
		break;

	case SCENE_MODE_CANDLE_LIGHT:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_CANDLE);
		CHECK_ERR(err);
		break;

	case SCENE_MODE_LOW_LIGHT:
		err = s5c73m3_writeb(sd, S5C73M3_SCENE_MODE,
			S5C73M3_SCENE_MODE_LOW_LIGHT);
		CHECK_ERR(err);
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = SCENE_MODE_NONE;
		goto retry;
	}

	state->scene_mode = val;
	cam_trace("X\n");
	return 0;
}

static int s5c73m3_capture_firework(struct v4l2_subdev *sd)
{
	int err = 0;

	cam_dbg("E, capture_firework\n");

	err = s5c73m3_writeb(sd, S5C73M3_FIREWORK_CAPTURE, 0x0001);
	CHECK_ERR(err);

	return err;
}

static int s5c73m3_capture_nightshot(struct v4l2_subdev *sd)
{
	int err = 0;

	cam_dbg("E, capture_nightshot\n");

	err = s5c73m3_writeb(sd, S5C73M3_NIGHTSHOT_CAPTURE, 0x0001);
	CHECK_ERR(err);

	return err;
}

static int s5c73m3_set_effect(struct v4l2_subdev *sd, int val)
{
	int err = 0;
	int num_entries = 0;
	int i = 0;
	cam_dbg("E, value %d\n", val);

	if (val < IMAGE_EFFECT_BASE || val > IMAGE_EFFECT_MAX)
		val = IMAGE_EFFECT_NONE;

	num_entries = ARRAY_SIZE(s5c73m3_effects);
	for (i = 0; i < num_entries; i++) {
		if (val == s5c73m3_effects[i].index) {
			err = s5c73m3_writeb(sd, S5C73M3_IMAGE_EFFECT,
				s5c73m3_effects[i].reg_val);
			CHECK_ERR(err);
			break;
		}
	}

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_set_wdr(struct v4l2_subdev *sd, int val)
{
	int err;
	cam_dbg("E, value %d\n", val);

retry:
	switch (val) {
	case WDR_OFF:
		err = s5c73m3_writeb(sd, S5C73M3_WDR,
			S5C73M3_WDR_OFF);
		CHECK_ERR(err);
		break;

	case WDR_ON:
		err = s5c73m3_writeb(sd, S5C73M3_WDR,
			S5C73M3_WDR_ON);
		CHECK_ERR(err);
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = WDR_OFF;
		goto retry;
	}

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_set_antishake(struct v4l2_subdev *sd, int val)
{
	int err = 0;
	cam_dbg("E, value %d\n", val);

	if (val) {
		err = s5c73m3_writeb(sd, S5C73M3_AE_MODE,
			S5C73M3_ANTI_SHAKE);
		CHECK_ERR(err);
	} else {
		err = s5c73m3_writeb(sd, S5C73M3_AE_MODE,
			S5C73M3_AUTO_MODE_AE_SET);
		CHECK_ERR(err);
	}
	return err;
}

static int s5c73m3_get_af_cal_version(struct v4l2_subdev *sd)
{
	struct s5c73m3_state *state = to_state(sd);
	u32 data = 0;
	u16 status = 0;
	int err = 0;

	/* Calibration Device */
	err = s5c73m3_read(sd, 0x0009, 0x300C, &status);
	CHECK_ERR(err);
	data = status;

	status = 0;
	err = s5c73m3_read(sd, 0x0009, 0x300E, &status);
	CHECK_ERR(err);
	data += status<<16;
	state->cal_device = data;

	/* Calibration DLL Version */
	status = 0;
	data = 0;
	err = s5c73m3_read(sd, 0x0009, 0x4FF8, &status);
	CHECK_ERR(err);
	data = status;

	status = 0;
	err = s5c73m3_read(sd, 0x0009, 0x4FFA, &status);
	CHECK_ERR(err);
	data += status<<16;
	state->cal_dll = data;

	cam_dbg("Cal_Device = 0x%x, Cal_DLL = 0x%x\n",
		state->cal_device, state->cal_dll);
	return 0;
}

static int s5c73m3_stop_af_lens(struct v4l2_subdev *sd, int val)
{
	struct s5c73m3_state *state = to_state(sd);
	int err;
	cam_dbg("E, value %d\n", val);

	if (val == CAF_START) {
		if (state->focus.mode == FOCUS_MODE_CONTINOUS_VIDEO) {
			err = s5c73m3_writeb(sd, S5C73M3_AF_MODE,
				S5C73M3_AF_MODE_MOVIE_CAF_START);

		} else {
			err = s5c73m3_writeb(sd, S5C73M3_AF_MODE,
				S5C73M3_AF_MODE_PREVIEW_CAF_START);
		}
	} else {
		err = s5c73m3_writeb(sd, S5C73M3_AF_CON,
			S5C73M3_AF_CON_STOP);
	}
	CHECK_ERR(err);

	cam_trace("X\n");

	return err;
}

static int s5c73m3_set_af(struct v4l2_subdev *sd, int val)
{
	struct s5c73m3_state *state = to_state(sd);
	int err = 0;

	cam_info("%s, mode %#x\n", val ? "start" : "stop", state->focus.mode);

	state->focus.status = 0;

	if (val) {
		state->isflash = S5C73M3_ISNEED_FLASH_ON;

		if (state->focus.mode == FOCUS_MODE_TOUCH)
			err = s5c73m3_set_touch_auto_focus(sd);
		else
			err = s5c73m3_writeb(sd, S5C73M3_AF_CON,
				S5C73M3_AF_CON_START);
	} else {
		err = s5c73m3_writeb(sd, S5C73M3_STILL_MAIN_FLASH
				, S5C73M3_STILL_MAIN_FLASH_CANCEL);
		err = s5c73m3_writeb(sd, S5C73M3_AF_CON,
			S5C73M3_AF_CON_STOP);
		state->isflash = S5C73M3_ISNEED_FLASH_UNDEFINED;
	}

	CHECK_ERR(err);

	cam_trace("X\n");
	return err;
}

static int s5c73m3_get_pre_flash(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	int err = 0;
	u16 pre_flash = false;

	s5c73m3_read(sd, 0x0009, S5C73M3_STILL_PRE_FLASH | 0x5000, &pre_flash);
	ctrl->value = pre_flash;
	return err;
}

static int s5c73m3_get_af_result(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	struct s5c73m3_state *state = to_state(sd);
	int err = 0;
	u16 af_status = S5C73M3_AF_STATUS_UNFOCUSED;
	/*u16 temp_status = 0;*/

	err = s5c73m3_read(sd, 0x0009, S5C73M3_AF_STATUS, &af_status);

	/*err = s5c73m3_read(sd, 0x0009, 0x5840, &temp_status);*/

	switch (af_status) {
	case S5C73M3_AF_STATUS_FOCUSING:
	case S5C73M3_CAF_STATUS_FOCUSING:
	case S5C73M3_CAF_STATUS_FIND_SEARCHING_DIR:
	case S5C73M3_AF_STATUS_INVALID:
		ctrl->value = CAMERA_AF_STATUS_IN_PROGRESS;
		break;

	case S5C73M3_AF_STATUS_FOCUSED:
	case S5C73M3_CAF_STATUS_FOCUSED:
		ctrl->value = CAMERA_AF_STATUS_SUCCESS;
		break;

	case S5C73M3_CAF_STATUS_UNFOCUSED:
	case S5C73M3_AF_STATUS_UNFOCUSED:
	default:
		ctrl->value = CAMERA_AF_STATUS_FAIL;
		break;
	}

	state->focus.status = af_status;

	/*cam_dbg("af_status = %d, frame_cnt = %d\n",
		state->focus.status, temp_status);*/
	return err;
}

static int s5c73m3_set_af_mode(struct v4l2_subdev *sd, int val)
{
	struct s5c73m3_state *state = to_state(sd);
	int err;
	cam_dbg("E, value %d\n", val);

retry:
	switch (val) {
	case FOCUS_MODE_AUTO:
	case FOCUS_MODE_INFINITY:
		if (state->focus.mode != FOCUS_MODE_CONTINOUS_PICTURE) {
			err = s5c73m3_writeb(sd, S5C73M3_AF_MODE,
				S5C73M3_AF_MODE_NORMAL);
			CHECK_ERR(err);
		} else {
			err = s5c73m3_writeb(sd, S5C73M3_AF_CON,
				S5C73M3_AF_CON_STOP);
			CHECK_ERR(err);
		}

		state->focus.mode = val;
		state->caf_mode = S5C73M3_AF_MODE_NORMAL;
		break;

	case FOCUS_MODE_MACRO:
		if (state->focus.mode != FOCUS_MODE_CONTINOUS_PICTURE_MACRO) {
			err = s5c73m3_writeb(sd, S5C73M3_AF_MODE,
				S5C73M3_AF_MODE_MACRO);
			CHECK_ERR(err);
		} else {
			err = s5c73m3_writeb(sd, S5C73M3_AF_CON,
				S5C73M3_AF_CON_STOP);
			CHECK_ERR(err);
		}

		state->focus.mode = val;
		state->caf_mode = S5C73M3_AF_MODE_MACRO;
		break;

	case FOCUS_MODE_CONTINOUS_PICTURE:
		state->isflash = S5C73M3_ISNEED_FLASH_UNDEFINED;

		if (val != state->focus.mode &&
			state->caf_mode != S5C73M3_AF_MODE_NORMAL) {
			state->focus.mode = val;

			err = s5c73m3_writeb(sd, S5C73M3_AF_MODE,
				S5C73M3_AF_MODE_NORMAL);
			CHECK_ERR(err);
			state->caf_mode = S5C73M3_AF_MODE_NORMAL;
		}

		err = s5c73m3_writeb(sd, S5C73M3_AF_MODE,
		S5C73M3_AF_MODE_PREVIEW_CAF_START);
		CHECK_ERR(err);
		break;

	case FOCUS_MODE_CONTINOUS_PICTURE_MACRO:
		state->isflash = S5C73M3_ISNEED_FLASH_UNDEFINED;
		if (val != state->focus.mode &&
			state->caf_mode != S5C73M3_AF_MODE_MACRO) {
			state->focus.mode = val;

			err = s5c73m3_writeb(sd, S5C73M3_AF_MODE,
				S5C73M3_AF_MODE_MACRO);
			state->caf_mode = S5C73M3_AF_MODE_MACRO;
			CHECK_ERR(err);
		}

		err = s5c73m3_writeb(sd, S5C73M3_AF_MODE,
		S5C73M3_AF_MODE_PREVIEW_CAF_START);
		CHECK_ERR(err);
		break;

	case FOCUS_MODE_CONTINOUS_VIDEO:
		state->focus.mode = val;

		err = s5c73m3_writeb(sd, S5C73M3_AF_MODE,
			S5C73M3_AF_MODE_MOVIE_CAF_START);
		CHECK_ERR(err);
		break;

	case FOCUS_MODE_FACEDETECT:
		state->focus.mode = val;
		break;

	case FOCUS_MODE_TOUCH:
		state->focus.mode = val;
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = FOCUS_MODE_AUTO;
		goto retry;
	}

	state->focus.mode = val;

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_set_touch_auto_focus(struct v4l2_subdev *sd)
{
	struct s5c73m3_state *state = to_state(sd);
	int err;

	err = s5c73m3_i2c_write(sd, 0xfcfc, 0x3310);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0050, 0x0009);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0054, S5C73M3_AF_TOUCH_POSITION);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, state->focus.pos_x);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, state->focus.pos_y);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, state->real_preview_width);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, state->real_preview_height);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0050, 0x0009);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0054, 0x5000);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, 0x0E0A);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, 0x0000);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0054, 0x5080);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, 0x0001);
	CHECK_ERR(err);

	return 0;
}

static int s5c73m3_set_zoom(struct v4l2_subdev *sd, int value)
{
	int err;
	cam_dbg("E, value %d\n", value);

retry:
	if (value < 0 || value > 30) {
		cam_warn("invalid value, %d\n", value);
		value = 0;
		goto retry;
	}
	err = s5c73m3_writeb(sd, S5C73M3_ZOOM_STEP, value);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_set_jpeg_quality(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	int val = ctrl->value, err;
	cam_dbg("E, value %d\n", val);

	if (val <= 65)		/* Normal */
		err = s5c73m3_writeb(sd, S5C73M3_IMAGE_QUALITY,
			S5C73M3_IMAGE_QUALITY_NORMAL);
	else if (val <= 75)	/* Fine */
		err = s5c73m3_writeb(sd, S5C73M3_IMAGE_QUALITY,
			S5C73M3_IMAGE_QUALITY_FINE);
	else			/* Superfine */
		err = s5c73m3_writeb(sd, S5C73M3_IMAGE_QUALITY,
			S5C73M3_IMAGE_QUALITY_SUPERFINE);

	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_aeawb_lock_unlock(struct v4l2_subdev *sd, int val)
{
	struct s5c73m3_state *state = to_state(sd);
	int err = 0;
	int ae_lock = val & 0x1;
	int awb_lock = (val & 0x2) >> 1;
	int ae_lock_changed =
		~(ae_lock & state->ae_lock) & (ae_lock | state->ae_lock);
	int awb_lock_changed =
		~(awb_lock & state->awb_lock) & (awb_lock | state->awb_lock);

	if (ae_lock_changed) {
		cam_dbg("ae lock - %s\n", ae_lock ? "true" : "false");
		err = s5c73m3_writeb(sd, S5C73M3_AE_CON,
				ae_lock ? S5C73M3_AE_STOP : S5C73M3_AE_START);
		CHECK_ERR(err);
		state->ae_lock = ae_lock;
	}
	if (awb_lock_changed &&
		state->wb_mode == WHITE_BALANCE_AUTO) {
		cam_dbg("awb lock - %s\n", awb_lock ? "true" : "false");
		err = s5c73m3_writeb(sd, S5C73M3_AWB_CON,
			awb_lock ? S5C73M3_AWB_STOP : S5C73M3_AWB_START);
		CHECK_ERR(err);
		state->awb_lock = awb_lock;
	}

	return 0;
}

static void s5c73m3_wait_for_preflash_fire(struct v4l2_subdev *sd)
{
	u16 pre_flash = false;
	u16 timeout_cnt = 0;

	do {
		s5c73m3_read(sd, 0x0009,
			S5C73M3_STILL_PRE_FLASH | 0x5000, &pre_flash);
		if (pre_flash || timeout_cnt > 20) {
			if (!pre_flash) {
				cam_dbg("pre_Flash = %d, timeout_cnt = %d\n",
					pre_flash, timeout_cnt);
			}
			break;
		} else
			timeout_cnt++;

		mdelay(15);
	} while (1);
}

static int s5c73m3_start_capture(struct v4l2_subdev *sd, int val)
{
	struct s5c73m3_state *state = to_state(sd);
	int err = 0;
	u16 isneed_flash = false;
	u16 pre_flash = false;

	s5c73m3_read(sd, 0x0009, S5C73M3_STILL_PRE_FLASH | 0x5000, &pre_flash);

	if (state->flash_mode == FLASH_MODE_ON) {
		if (!pre_flash) {
			err = s5c73m3_writeb(sd, S5C73M3_STILL_PRE_FLASH
					, S5C73M3_STILL_PRE_FLASH_FIRE);
			s5c73m3_wait_for_preflash_fire(sd);
		}
		err = s5c73m3_writeb(sd, S5C73M3_STILL_MAIN_FLASH
			, S5C73M3_STILL_MAIN_FLASH_FIRE);
	} else if (state->flash_mode == FLASH_MODE_AUTO) {
		if (pre_flash) {
			err = s5c73m3_writeb(sd, S5C73M3_STILL_MAIN_FLASH
					, S5C73M3_STILL_MAIN_FLASH_FIRE);
		} else if (state->isflash != S5C73M3_ISNEED_FLASH_ON) {
			err = s5c73m3_read(sd, 0x0009,
				S5C73M3_AE_ISNEEDFLASH | 0x5000, &isneed_flash);
			if (isneed_flash) {
				err = s5c73m3_writeb(sd, S5C73M3_STILL_PRE_FLASH
						, S5C73M3_STILL_PRE_FLASH_FIRE);
				s5c73m3_wait_for_preflash_fire(sd);
				err = s5c73m3_writeb(sd,
						S5C73M3_STILL_MAIN_FLASH,
						S5C73M3_STILL_MAIN_FLASH_FIRE);
			}
		}
	}

	state->isflash = S5C73M3_ISNEED_FLASH_UNDEFINED;

	return 0;
}

static int s5c73m3_set_auto_bracket_mode(struct v4l2_subdev *sd)
{
	int err = 0;

		err = s5c73m3_writeb(sd, S5C73M3_AE_AUTO_BRAKET,
		S5C73M3_AE_AUTO_BRAKET_EV20);
		CHECK_ERR(err);

	return err;
}

static int s5c73m3_set_frame_rate(struct v4l2_subdev *sd, int fps)
{
	int err = 0;
	struct s5c73m3_state *state = to_state(sd);

	if (!state->stream_enable) {
		state->fps = fps;
		return 0;
	}

	cam_dbg("E, value %d\n", fps);

	switch (fps) {
	case 120:
		err = s5c73m3_writeb(sd, S5C73M3_AE_MODE,
			S5C73M3_FIXED_120FPS); /* 120fps */
		break;
	case 90:
		err = s5c73m3_writeb(sd, S5C73M3_AE_MODE,
			S5C73M3_FIXED_90FPS); /* 90fps */
		break;
	case 60:
		err = s5c73m3_writeb(sd, S5C73M3_AE_MODE,
			S5C73M3_FIXED_60FPS); /* 60fps */
		break;
	case 30:
		err = s5c73m3_writeb(sd, S5C73M3_AE_MODE,
			S5C73M3_FIXED_30FPS); /* 30fps */
		break;
	case 20:
		err = s5c73m3_writeb(sd, S5C73M3_AE_MODE,
			S5C73M3_FIXED_20FPS); /* 20fps */
		break;
	case 15:
		err = s5c73m3_writeb(sd, S5C73M3_AE_MODE,
			S5C73M3_FIXED_15FPS); /* 15fps */
		break;
	case 10:
		err = s5c73m3_writeb(sd, S5C73M3_AE_MODE,
			S5C73M3_FIXED_10FPS); /* 10fps */
		break;
	case 7:
		err = s5c73m3_writeb(sd, S5C73M3_AE_MODE,
			S5C73M3_FIXED_7FPS); /* 7fps */
		break;
	default:
		err = s5c73m3_writeb(sd, S5C73M3_AE_MODE,
			S5C73M3_AUTO_MODE_AE_SET); /* auto */
		break;
	}
	return err;
}

static int s5c73m3_set_face_zoom(struct v4l2_subdev *sd, int val)
{
	struct s5c73m3_state *state = to_state(sd);
	int err;

	cam_dbg("s5c73m3_set_face_zoom\n");

	err = s5c73m3_writeb(sd, S5C73M3_AF_CON,
		S5C73M3_AF_CON_STOP);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0xfcfc, 0x3310);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0050, 0x0009);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0054, S5C73M3_AF_TOUCH_POSITION);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, state->focus.pos_x);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, state->focus.pos_y);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, state->preview->width);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, state->preview->height);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0050, 0x0009);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0054, 0x5000);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, S5C73M3_AF_FACE_ZOOM);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, val); /*0:reset, 1:Start*/
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0054, 0x5080);
	CHECK_ERR(err);

	err = s5c73m3_i2c_write(sd, 0x0F14, 0x0001);
	CHECK_ERR(err);

	udelay(400);
	err = s5c73m3_writeb(sd, S5C73M3_AF_MODE,
		S5C73M3_AF_MODE_PREVIEW_CAF_START);
	CHECK_ERR(err);

	return 0;
}


static int s5c73m3_set_face_detection(struct v4l2_subdev *sd, int val)
{
	int err;
	cam_dbg("E, value %d\n", val);

retry:
	switch (val) {
	case FACE_DETECTION_ON:
		err = s5c73m3_writeb(sd, S5C73M3_FACE_DET,
			S5C73M3_FACE_DET_ON);
		CHECK_ERR(err);

		err = s5c73m3_writeb(sd, S5C73M3_AF_MODE,
		S5C73M3_AF_MODE_PREVIEW_CAF_START);
		CHECK_ERR(err);

		break;

	case FACE_DETECTION_OFF:
		err = s5c73m3_writeb(sd, S5C73M3_FACE_DET,
			S5C73M3_FACE_DET_OFF);
		CHECK_ERR(err);

		err = s5c73m3_writeb(sd, S5C73M3_AF_MODE,
		S5C73M3_AF_MODE_PREVIEW_CAF_START);
		CHECK_ERR(err);

		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = FACE_DETECTION_OFF;
		goto retry;
	}

	cam_trace("X\n");
	return 0;

}

static int s5c73m3_set_hybrid_capture(struct v4l2_subdev *sd)
{
	int err;
	cam_trace("E\n");

	err = s5c73m3_writeb(sd, S5C73M3_HYBRID_CAPTURE, 1);

	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_get_lux(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	int err = 0;
	u16 lux_val = 0;

	err = s5c73m3_read(sd, 0x0009, 0x5C88, &lux_val);
	ctrl->value = lux_val;

	return err;
}

static int s5c73m3_set_low_light_mode(struct v4l2_subdev *sd, int val)
{
	int err;
	cam_dbg("E, value %d\n", val);

	err = s5c73m3_writeb(sd, S5C73M3_AE_LOW_LIGHT_MODE, val);

	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct s5c73m3_state *state = to_state(sd);
	int err = 0;

	printk(KERN_INFO "id %d, value %d\n",
		ctrl->id - V4L2_CID_PRIVATE_BASE, ctrl->value);

	if (unlikely(state->isp.bad_fw && ctrl->id != V4L2_CID_CAM_UPDATE_FW)) {
		cam_err("\"Unknown\" state, please update F/W");
		return -ENOSYS;
	}

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_FRAME_RATE:
		err = s5c73m3_set_frame_rate(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACE_DETECTION:
		err = s5c73m3_set_face_detection(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACE_ZOOM:
		err = s5c73m3_set_face_zoom(sd, ctrl->value);
		break;

	case V4L2_CID_CAM_UPDATE_FW:
		if (ctrl->value == FW_MODE_DUMP)
			err = s5c73m3_dump_fw(sd);
		else if (ctrl->value == FW_MODE_UPDATE)
			err = s5c73m3_check_fw(sd, 1);
		else
			err = 0;

		break;

	case V4L2_CID_CAMERA_SENSOR_MODE:
		err = s5c73m3_set_sensor_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FLASH_MODE:
		err = s5c73m3_set_flash(sd, ctrl->value, 0);
		break;

	case V4L2_CID_CAMERA_ANTI_BANDING:
		err = s5c73m3_set_antibanding(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_ISO:
		err = s5c73m3_set_iso(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_METERING:
		err = s5c73m3_set_metering(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_BRIGHTNESS:
		err = s5c73m3_set_exposure(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_CONTRAST:
		err = s5c73m3_set_contrast(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_SHARPNESS:
		err = s5c73m3_set_sharpness(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_WHITE_BALANCE:
		err = s5c73m3_set_whitebalance(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_SCENE_MODE:
		err = s5c73m3_set_scene_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_EFFECT:
		err = s5c73m3_set_effect(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_WDR:
		err = s5c73m3_set_wdr(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_ANTI_SHAKE:
		if (state->sensor_mode == SENSOR_CAMERA)
			err = s5c73m3_set_antishake(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_DEFAULT_FOCUS_POSITION:
		/*err = s5c73m3_set_af_mode(sd, state->focus.mode);*/
		err = 0;
		break;

	case V4L2_CID_CAMERA_FOCUS_MODE:
		/*state->focus.mode = ctrl->value;*/
		err = s5c73m3_set_af_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_SET_AUTO_FOCUS:
		state->real_preview_width = ((u32)ctrl->value >> 20) & 0xFFF;
		state->real_preview_height = ((u32)ctrl->value >> 8) & 0xFFF;

		err = s5c73m3_set_af(sd, (u32)ctrl->value & 0x000F);
		break;

	case V4L2_CID_CAMERA_OBJECT_POSITION_X:
		state->focus.pos_x = ctrl->value;
		break;

	case V4L2_CID_CAMERA_OBJECT_POSITION_Y:
		state->focus.pos_y = ctrl->value;
		break;

	case V4L2_CID_CAMERA_ZOOM:
		err = s5c73m3_set_zoom(sd, ctrl->value);
		break;

	case V4L2_CID_CAM_JPEG_QUALITY:
		err = s5c73m3_set_jpeg_quality(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_CAPTURE:
		err = s5c73m3_start_capture(sd, ctrl->value);

		if (state->scene_mode == SCENE_MODE_FIREWORKS)
			err = s5c73m3_capture_firework(sd);
		else if (state->scene_mode == SCENE_MODE_NIGHTSHOT)
			err = s5c73m3_capture_nightshot(sd);
		break;

	case V4L2_CID_CAMERA_HDR:
		state->hdr_mode = ctrl->value;
		err = 0;
		break;

	case V4L2_CID_CAMERA_HYBRID:
		state->hybrid_mode = ctrl->value;
		err = 0;
		break;

	case V4L2_CID_CAMERA_FAST_MODE:
		state->fast_mode = ctrl->value;
		err = 0;
		break;

	case V4L2_CID_CAMERA_YUV_SNAPSHOT:
		state->yuv_snapshot = ctrl->value;
		err = 0;
		break;

	case V4L2_CID_CAMERA_HYBRID_CAPTURE:
		err = s5c73m3_set_hybrid_capture(sd);
		break;

	case V4L2_CID_CAMERA_VT_MODE:
		state->vt_mode = ctrl->value;
		break;

	case V4L2_CID_CAMERA_JPEG_RESOLUTION:
		state->jpeg_width = (u32)ctrl->value >> 16;
		state->jpeg_height = (u32)ctrl->value & 0x0FFFF;
		break;

	case V4L2_CID_CAMERA_AEAWB_LOCK_UNLOCK:
		err = s5c73m3_aeawb_lock_unlock(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_CAF_START_STOP:
		err = s5c73m3_stop_af_lens(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_LOW_LIGHT_MODE:
		err = s5c73m3_set_low_light_mode(sd, ctrl->value);
		break;

	default:
		cam_err("no such control id %d, value %d\n",
				ctrl->id - V4L2_CID_PRIVATE_BASE, ctrl->value);
		/*err = -ENOIOCTLCMD;*/
		err = 0;
		break;
	}

	if (err < 0 && err != -ENOIOCTLCMD)
		cam_err("failed, id %d, value %d\n",
				ctrl->id - V4L2_CID_PRIVATE_BASE, ctrl->value);
	return err;
}

static int s5c73m3_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_CAPTURE:
		err = s5c73m3_get_pre_flash(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_AUTO_FOCUS_RESULT:
		err = s5c73m3_get_af_result(sd, ctrl);
		break;

	case V4L2_CID_CAM_JPEG_MEMSIZE:
		ctrl->value = 0xA00000; /*interleaved data size*/
		break;

	case V4L2_CID_CAMERA_GET_LUX:
		err = s5c73m3_get_lux(sd, ctrl);
		break;

	default:
		cam_err("no such control id %d\n",
				ctrl->id - V4L2_CID_PRIVATE_BASE);
		/*err = -ENOIOCTLCMD*/
		err = 0;
		break;
	}

	if (err < 0 && err != -ENOIOCTLCMD)
		cam_err("failed, id %d\n", ctrl->id - V4L2_CID_PRIVATE_BASE);

	return err;
}


static int s5c73m3_g_ext_ctrl(struct v4l2_subdev *sd,
	struct v4l2_ext_control *ctrl)
{
	struct s5c73m3_state *state = to_state(sd);
	int err = 0;

	switch (ctrl->id) {
	case V4L2_CID_CAM_SENSOR_FW_VER:
		strcpy(ctrl->string, state->phone_fw);
		break;

	default:
		cam_err("no such control id %d\n",
			ctrl->id - V4L2_CID_CAMERA_CLASS_BASE);
		/*err = -ENOIOCTLCMD*/
		break;
	}

	return err;
}

static int s5c73m3_g_ext_ctrls(struct v4l2_subdev *sd,
	struct v4l2_ext_controls *ctrls)
{
	struct v4l2_ext_control *ctrl = ctrls->controls;
	int i, err = 0;

	for (i = 0; i < ctrls->count; i++, ctrl++) {
		err = s5c73m3_g_ext_ctrl(sd, ctrl);
		if (err) {
			ctrls->error_idx = i;
			break;
		}
	}
	return err;
}

#ifndef CONFIG_VIDEO_S5C73M3_SPI
int s5c73m3_spi_write(const u8 *addr, const int len, const int txSize)
{ return 0; }
#endif

static int s5c73m3_load_fw(struct v4l2_subdev *sd)
{
	struct device *dev = sd->v4l2_dev->dev;
	struct s5c73m3_state *state = to_state(sd);
	const struct firmware *fw;
	char fw_path[20] = {0,};
	char fw_path_in_data[25] = {0,};
	u8 *buf = NULL;
	int err = 0;
	int txSize = 0;

	struct file *fp = NULL;
	mm_segment_t old_fs;
	long fsize = 0, nread;

#if defined(CONFIG_MACH_T0)
	if (state->sensor_fw[1] == 'D') {
		sprintf(fw_path, "SlimISP_%cK.bin",
			state->sensor_fw[0]);
	} else {
		sprintf(fw_path, "SlimISP_%c%c.bin",
			state->sensor_fw[0],
			state->sensor_fw[1]);
	}
#elif defined(CONFIG_MACH_BAFFIN)
	if (state->sensor_fw[1] == 'D') {
		sprintf(fw_path, "SlimISP_%cK.bin",
			state->sensor_fw[0]);
	} else if (state->sensor_fw[1] == 'H') {
		sprintf(fw_path, "SlimISP_%cM.bin",
			state->sensor_fw[0]);
	} else {
		sprintf(fw_path, "SlimISP_%c%c.bin",
			state->sensor_fw[0],
			state->sensor_fw[1]);
	}
#else
	if (state->sensor_fw[0] == 'O') {
		sprintf(fw_path, "SlimISP_G%c.bin",
			state->sensor_fw[1]);
	} else if (state->sensor_fw[0] == 'S') {
		sprintf(fw_path, "SlimISP_Z%c.bin",
			state->sensor_fw[1]);
	} else {
		sprintf(fw_path, "SlimISP_%c%c.bin",
			state->sensor_fw[0],
			state->sensor_fw[1]);
	}
#endif

	sprintf(fw_path_in_data, "/data/cfw/%s",
		fw_path);

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	if (state->fw_index == S5C73M3_SD_CARD ||
		state->fw_index == S5C73M3_IN_DATA) {

		if (state->fw_index == S5C73M3_SD_CARD)
			fp = filp_open(S5C73M3_FW_PATH, O_RDONLY, 0);
		else
			fp = filp_open(fw_path_in_data, O_RDONLY, 0);
		if (IS_ERR(fp))
			goto out;
		else
			cam_dbg("%s is opened\n",
			state->fw_index == S5C73M3_SD_CARD ?
			S5C73M3_FW_PATH : fw_path_in_data);

		fsize = fp->f_path.dentry->d_inode->i_size;

		nread = vfs_read(fp, (char __user *)data_memory,
			fsize, &fp->f_pos);
		if (nread != fsize) {
			cam_err("failed to read firmware file_2\n");
			err = -EIO;
			goto out;
		}
		set_fs(old_fs);
	} else {
		set_fs(old_fs);
		err = request_firmware(&fw, fw_path, dev);
		if (err != 0) {
			/*cam_err("request_firmware falied\n");*/
			err = -EINVAL;
			goto out;
		}

		/*cam_dbg("start, size %d Bytes\n", fw->size);*/
		buf = (u8 *)fw->data;
		fsize = fw->size;
	}

	txSize = 60*1024; /*60KB*/

	if (state->fw_index != S5C73M3_IN_SYSTEM) {
		err = s5c73m3_spi_write((char *)&data_memory,
			fsize, txSize);
		if (err < 0) {
			cam_err("s5c73m3_spi_write falied\n");
			goto out;
		}
	} else {
		err = s5c73m3_spi_write((char *)buf, fsize, txSize);
	}
out:
	if (state->fw_index == S5C73M3_SD_CARD ||
		state->fw_index == S5C73M3_IN_DATA) {
		if (!IS_ERR(fp) && fp != NULL)
			filp_close(fp, current->files);

		vfree(buf);

		set_fs(old_fs);
	} else {
		release_firmware(fw);
	}

	return err;
}

/*
 * v4l2_subdev_video_ops
 */
static const struct s5c73m3_frmsizeenum *s5c73m3_get_frmsize
	(const struct s5c73m3_frmsizeenum *frmsizes, int num_entries, int index)
{
	int i;

	for (i = 0; i < num_entries; i++) {
		if (frmsizes[i].index == index)
			return &frmsizes[i];
	}

	return NULL;
}

static int s5c73m3_set_frmsize(struct v4l2_subdev *sd)
{
	struct s5c73m3_state *state = to_state(sd);
	int err ;
	cam_trace("E\n");

	if (state->fast_mode == FAST_MODE_SUBSAMPLING_HALF) {
		cam_dbg("S5C73M3_FAST_MODE_SUBSAMPLING_HALF\n");
		err = s5c73m3_writeb(sd, S5C73M3_CHG_MODE,
			S5C73M3_FAST_MODE_SUBSAMPLING_HALF
			| state->preview->reg_val | (state->sensor_mode<<8));
		CHECK_ERR(err);
	} else if (state->fast_mode == FAST_MODE_SUBSAMPLING_QUARTER) {
		cam_dbg("S5C73M3_FAST_MODE_SUBSAMPLING_QUARTER\n");
		err = s5c73m3_writeb(sd, S5C73M3_CHG_MODE,
			S5C73M3_FAST_MODE_SUBSAMPLING_QUARTER
			| state->preview->reg_val | (state->sensor_mode<<8));
		CHECK_ERR(err);
	} else {
		cam_dbg("S5C73M3_DEFAULT_MODE\n");
		err = s5c73m3_writeb(sd, S5C73M3_CHG_MODE,
			S5C73M3_DEFAULT_MODE
			| state->capture->reg_val | state->preview->reg_val
			|(state->sensor_mode<<8));
		CHECK_ERR(err);
	}

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_s_fmt(struct v4l2_subdev *sd,
	struct v4l2_mbus_framefmt *ffmt)
{
	struct s5c73m3_state *state = to_state(sd);
	const struct s5c73m3_frmsizeenum **frmsize;
	const struct s5c73m3_frmsizeenum **capfrmsize;

	u32 width = ffmt->width;
	u32 height = ffmt->height;
	u32 tmp_width;
	u32 old_index, old_index_cap;
	int i, num_entries;
	cam_trace("E\n");

	if (unlikely(state->isp.bad_fw)) {
		cam_err("\"Unknown\" state, please update F/W");
		return -ENOSYS;
	}
	if (ffmt->width < ffmt->height) {
		tmp_width = ffmt->height;
		height = ffmt->width;
		width = tmp_width;
	}

	if (ffmt->colorspace == V4L2_COLORSPACE_JPEG)
		state->format_mode = V4L2_PIX_FMT_MODE_CAPTURE;
	else
		state->format_mode = V4L2_PIX_FMT_MODE_PREVIEW;

	s5c73m3_set_mode(sd);

	/*set frame size for preview(yuv)*/
	frmsize = &state->preview;
	old_index = *frmsize ? (*frmsize)->index : -1;
	*frmsize = NULL;

	num_entries = ARRAY_SIZE(preview_frmsizes);
	for (i = 0; i < num_entries; i++) {
		if (width == preview_frmsizes[i].width &&
			height == preview_frmsizes[i].height) {
			*frmsize = &preview_frmsizes[i];
			break;
		}
	}

	if (*frmsize == NULL) {
		cam_warn("invalid yuv frame size %dx%d\n", width, height);
		*frmsize = s5c73m3_get_frmsize(preview_frmsizes,
				num_entries,
				S5C73M3_PREVIEW_960X720);
	}

	/*set frame size for capture(jpeg)*/
	/*it's meaningful for interleaved mode*/
	capfrmsize = &state->capture;
	old_index_cap = *capfrmsize ? (*capfrmsize)->index : -1;
	*capfrmsize = NULL;

	width = state->jpeg_width;
	height = state->jpeg_height;

	num_entries = ARRAY_SIZE(capture_frmsizes);
	for (i = 0; i < num_entries; i++) {
		if (width == capture_frmsizes[i].width &&
			height == capture_frmsizes[i].height) {
			*capfrmsize = &capture_frmsizes[i];
			break;
		}
	}

	if (*capfrmsize == NULL) {
		cam_warn("invalid jpeg frame size %dx%d\n", width, height);
		*capfrmsize = s5c73m3_get_frmsize(capture_frmsizes, num_entries,
				S5C73M3_CAPTURE_VGA);
	}

	cam_dbg("yuv %dx%d\n", (*frmsize)->width, (*frmsize)->height);
	cam_dbg("jpeg %dx%d\n", (*capfrmsize)->width, (*capfrmsize)->height);
	if (state->stream_enable) {
		if (ffmt->colorspace == V4L2_COLORSPACE_JPEG) {
			if ((old_index != (*frmsize)->index)
				|| (old_index_cap != (*capfrmsize)->index))
				s5c73m3_set_frmsize(sd);
		} else {
			if (old_index != (*frmsize)->index)
				s5c73m3_set_frmsize(sd);
		}
	} else
		s5c73m3_set_frmsize(sd);

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct s5c73m3_state *state = to_state(sd);

	a->parm.capture.timeperframe.numerator = 1;
	a->parm.capture.timeperframe.denominator = state->fps;

	return 0;
}

static int s5c73m3_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct s5c73m3_state *state = to_state(sd);
	int err = 0;

	u32 fps = a->parm.capture.timeperframe.denominator /
					a->parm.capture.timeperframe.numerator;

	if (unlikely(state->isp.bad_fw)) {
		cam_err("\"Unknown\" state, please update F/W");
		return -ENOSYS;
	}

	if (fps != state->fps) {
		if (fps <= 0 || fps > 30) {
			cam_err("invalid frame rate %d\n", fps);
			fps = 30;
		}
		state->fps = fps;
	}
	cam_err("Frame rate = %d(%d)\n", fps, state->fps);

	err = s5c73m3_set_frame_rate(sd, state->fps);
	CHECK_ERR(err);

	return 0;
}

static int s5c73m3_enum_framesizes(struct v4l2_subdev *sd,
	struct v4l2_frmsizeenum *fsize)
{
	struct s5c73m3_state *state = to_state(sd);

	/*
	* The camera interface should read this value, this is the resolution
	* at which the sensor would provide framedata to the camera i/f
	* In case of image capture,
	* this returns the default camera resolution (VGA)
	*/
	if (state->preview == NULL)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	if (state->hdr_mode || state->yuv_snapshot) {
		fsize->discrete.width = state->capture->width;
		fsize->discrete.height = state->capture->height;
	} else {
		fsize->discrete.width = state->preview->width;
		fsize->discrete.height = state->preview->height;
	}
	return 0;
}

static int s5c73m3_s_stream_sensor(struct v4l2_subdev *sd, int onoff)
{
	int err = 0;
	int index = 0;
	u16 status = 0;
	u16 i2c_status = 0;
	u16 i2c_seq_status = 0;

	cam_info("onoff=%d\n", onoff);
	err = s5c73m3_writeb(sd, S5C73M3_SENSOR_STREAMING,
		onoff ? S5C73M3_SENSOR_STREAMING_ON :
		S5C73M3_SENSOR_STREAMING_OFF);
	CHECK_ERR(err);

	do {
		err = s5c73m3_read(sd, 0x0009, S5C73M3_STATUS, &status);
		if (status == 0xffff)
			break;

		index++;
		msleep(20);
	} while (index < 30);

	if (index >= 30) {
		err = s5c73m3_read(sd, 0x0009,
			S5C73M3_I2C_ERR_STATUS, &i2c_status);
		err = s5c73m3_read(sd, 0x0009,
			S5C73M3_I2C_SEQ_STATUS, &i2c_seq_status);
		cam_dbg("TimeOut!! index:%d,status:%#x,i2c_stauts:%#x,i2c_seq_status:%#x\n",
			index,
			status,
			i2c_status,
			i2c_seq_status);

		err = -1;
	}

	return err;
}

static int s5c73m3_s_stream_hdr(struct v4l2_subdev *sd, int enable)
{
	struct s5c73m3_state *state = to_state(sd);
	int err = 0;
	cam_trace("E\n");

	if (enable) {
		err = s5c73m3_i2c_write(sd, 0x0050, 0x0009);
		CHECK_ERR(err);

		err = s5c73m3_i2c_write(sd, 0x0054, 0x5000);
		CHECK_ERR(err);

		err = s5c73m3_i2c_write(sd, 0x0F14, 0x0902);
		CHECK_ERR(err);

		err = s5c73m3_i2c_write(sd, 0x0F14, 0x0008);
		CHECK_ERR(err);

		err = s5c73m3_i2c_write(sd, 0x0F14, 0x091A);
		CHECK_ERR(err);

		err = s5c73m3_i2c_write(sd, 0x0F14, 0x0002);
		CHECK_ERR(err);

		err = s5c73m3_i2c_write(sd, 0x0F14, 0x0B10);
		CHECK_ERR(err);

		err = s5c73m3_i2c_write(sd, 0x0F14, 0x8000 |
				state->capture->reg_val |
				state->preview->reg_val);
		CHECK_ERR(err);

		err = s5c73m3_i2c_write(sd, 0x0054, 0x5080);
		CHECK_ERR(err);

		err = s5c73m3_i2c_write(sd, 0x0F14, 0x0003);
		CHECK_ERR(err);

		err = s5c73m3_s_stream_sensor(sd, enable);
		err = s5c73m3_set_auto_bracket_mode(sd);
	} else {
		  err = s5c73m3_s_stream_sensor(sd, enable);
	}

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct s5c73m3_state *state = to_state(sd);
	int err;

	cam_trace("E\n");

	if (unlikely(state->isp.bad_fw)) {
		cam_err("\"Unknown\" state, please update F/W");
		return -ENOSYS;
	}

	switch (enable) {
	case STREAM_MODE_CAM_ON:
	case STREAM_MODE_CAM_OFF:
		switch (state->format_mode) {
		case V4L2_PIX_FMT_MODE_CAPTURE:
			cam_info("capture %s",
				enable == STREAM_MODE_CAM_ON ? "on" : "off");

			s5c73m3_s_stream_sensor(sd, enable);
			if (enable == STREAM_MODE_CAM_ON &&
				(state->focus.mode >=
					FOCUS_MODE_CONTINOUS &&
				state->focus.mode <=
					FOCUS_MODE_CONTINOUS_VIDEO)) {
				s5c73m3_set_af_mode(sd,
					state->focus.mode);
			}
			break;

		default:
			cam_info("preview %s",
				enable == STREAM_MODE_CAM_ON ? "on" : "off");

			if (state->hdr_mode) {
				err = s5c73m3_set_flash(sd, FLASH_MODE_OFF, 0);
				err = s5c73m3_s_stream_hdr(sd, enable);
			} else {
				err = s5c73m3_s_stream_sensor(sd, enable);
				if (enable == STREAM_MODE_CAM_ON &&
					(state->focus.mode >=
						FOCUS_MODE_CONTINOUS &&
					state->focus.mode <=
						FOCUS_MODE_CONTINOUS_VIDEO)) {
					s5c73m3_set_af_mode(sd,
						state->focus.mode);
				}
			}
			break;
		}
		break;

	case STREAM_MODE_MOVIE_ON:
		if (state->flash_mode != FLASH_MODE_OFF)
			err = s5c73m3_set_flash(sd, state->flash_mode, 1);

		if (state->preview->index == S5C73M3_PREVIEW_720P ||
				state->preview->index == S5C73M3_PREVIEW_1080P)
			err = s5c73m3_set_af(sd, 1);
		break;

	case STREAM_MODE_MOVIE_OFF:
		if (state->preview->index == S5C73M3_PREVIEW_720P ||
				state->preview->index == S5C73M3_PREVIEW_1080P)
			err = s5c73m3_set_af(sd, 0);

		s5c73m3_set_flash(sd, FLASH_MODE_OFF, 1);
		break;

	default:
		cam_err("invalid stream option, %d\n", enable);
		break;
	}

#if 0
		err = s5c73m3_writeb(sd, S5C73M3_AF_CAL, 0);
		CHECK_ERR(err);
#endif
	state->stream_enable = enable;
	if (state->stream_enable && state->hdr_mode == 0) {
		if (state->fps)
			s5c73m3_set_frame_rate(sd, state->fps);
	}

	cam_trace("X\n");
	return 0;
}

static int s5c73m3_init_param(struct v4l2_subdev *sd)
{
	s5c73m3_set_flash(sd, FLASH_MODE_OFF, 0);
	return 0;
}

static int s5c73m3_FROM_booting(struct v4l2_subdev *sd)
{
	u16 read_val;
	int i, err;

	cam_trace("E\n");

	/*ARM go*/
	err = s5c73m3_write(sd, 0x3000, 0x0004, 0xFFFF);
	CHECK_ERR(err);

	udelay(400);

	/*Check boot done*/
	for (i = 0; i < 4; i++) {
		err = s5c73m3_read(sd, 0x3010, 0x0010, &read_val);
		CHECK_ERR(err);

		if (read_val == 0x0C)
			break;

		udelay(100);
	}

	if (read_val != 0x0C) {
		cam_err("boot fail, read_val %#x\n", read_val);
		return -1;
	}

       /*P,M,S and Boot Mode*/
	err = s5c73m3_write(sd, 0x3100, 0x010C, 0x0044);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3100, 0x0108, 0x000D);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3100, 0x0304, 0x0001);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x0001, 0x0000, 0x5800);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x0001, 0x0002, 0x0002);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3100, 0x0000, 0x0001);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3010, 0x0014, 0x1B85);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3010, 0x0010, 0x230C);
	CHECK_ERR(err);

	mdelay(300);

	/*Check binary read done*/
	for (i = 0; i < 3; i++) {
		err = s5c73m3_read(sd, 0x3010, 0x0010, &read_val);
		CHECK_ERR(err);

		if (read_val == 0x230E)
			break;

		udelay(100);
	}

	if (read_val != 0x230E) {
		cam_err("binary read fail, read_val %#x\n", read_val);
		return -1;
	}

	/*ARM reset*/
	err = s5c73m3_write(sd, 0x3000, 0x0004, 0xFFFD);
	CHECK_ERR(err);

	/*remap*/
	err = s5c73m3_write(sd, 0x3010, 0x00A4, 0x0183);
	CHECK_ERR(err);

	/*ARM go again*/
	err = s5c73m3_write(sd, 0x3000, 0x0004, 0xFFFF);
	CHECK_ERR(err);

	cam_trace("X\n");

	return 0;
}

static int s5c73m3_SPI_booting(struct v4l2_subdev *sd)
{
	u16 read_val;
	int i, err;

	/*ARM go*/
	err = s5c73m3_write(sd, 0x3000, 0x0004, 0xFFFF);
	CHECK_ERR(err);

	udelay(400);

	/*Check boot done*/
	for (i = 0; i < 3; i++) {
		err = s5c73m3_read(sd, 0x3010, 0x0010, &read_val);
		CHECK_ERR(err);

		if (read_val == 0x0C)
			break;

		udelay(100);
	}

	if (read_val != 0x0C) {
		cam_err("boot fail, read_val %#x\n", read_val);
		return -1;
	}

       /*P,M,S and Boot Mode*/
	err = s5c73m3_write(sd, 0x3010, 0x0014, 0x2146);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3010, 0x0010, 0x210C);
	CHECK_ERR(err);

	udelay(200);

	/*Check SPI ready*/
	for (i = 0; i < 3; i++) {
		err = s5c73m3_read(sd, 0x3010, 0x0010, &read_val);
		CHECK_ERR(err);

		if (read_val == 0x210D)
			break;

		udelay(100);
	}

	if (read_val != 0x210D) {
		cam_err("SPI not ready, read_val %#x\n", read_val);
		return -1;
	}

	/*download fw by SPI*/
	s5c73m3_load_fw(sd);

	/*ARM reset*/
	err = s5c73m3_write(sd, 0x3000, 0x0004, 0xFFFD);
	CHECK_ERR(err);

	/*remap*/
	err = s5c73m3_write(sd, 0x3010, 0x00A4, 0x0183);
	CHECK_ERR(err);

	/*ARM go again*/
	err = s5c73m3_write(sd, 0x3000, 0x0004, 0xFFFF);
	CHECK_ERR(err);

	return 0;
}

static int s5c73m3_read_vdd_core(struct v4l2_subdev *sd)
{
	struct s5c73m3_state *state = to_state(sd);
	u8 *buf = NULL;
	u16 read_val;
	u32 vdd_core_val = 0;
	int err;
	struct file *fp;
	mm_segment_t old_fs;

	cam_trace("E\n");

	/*Initialize OTP Controller*/
	err = s5c73m3_write(sd, 0x3800, 0xA004, 0x0000);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3800, 0xA000, 0x0004);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3800, 0xA0D8, 0x0000);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3800, 0xA0DC, 0x0004);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3800, 0xA0C4, 0x4000);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3800, 0xA0D4, 0x0015);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3800, 0xA000, 0x0001);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3800, 0xA0B4, 0x9F90);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3800, 0xA09C, 0x9A95);
	CHECK_ERR(err);

	/*Page Select*/
	err = s5c73m3_write(sd, 0x3800, 0xA0C4, 0x4800);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3800, 0xA0C4, 0x4400);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3800, 0xA0C4, 0x4200);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3800, 0xA004, 0x00C0);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3800, 0xA000, 0x0001);
	CHECK_ERR(err);

#if 0 /*read_val should be 0x7383*/
	err = s5c73m3_read(sd, 0x0000, 0x131C, &read_val);
	CHECK_ERR(err);

	cam_dbg("read_val %#x\n", read_val);
#endif

	/*Read Data*/
	err = s5c73m3_read(sd, 0x3800, 0xA034, &read_val);
	CHECK_ERR(err);

	cam_dbg("read_val %#x\n", read_val);

	err = s5c73m3_read(sd, 0x3800, 0xA040, &isp_chip_info1);
	CHECK_ERR(err);
	err = s5c73m3_read(sd, 0x3800, 0xA044, &isp_chip_info2);
	CHECK_ERR(err);
	err = s5c73m3_read(sd, 0x3800, 0xA048, &isp_chip_info3);
	CHECK_ERR(err);

	/*Read Data End*/
	err = s5c73m3_write(sd, 0x3800, 0xA000, 0x0000);
	CHECK_ERR(err);

	if (read_val & 0x200) {
		strcpy(sysfs_isp_core, "1.15V");
		state->pdata->set_vdd_core(1150000);
		vdd_core_val = 1150000;
	} else if (read_val & 0x800) {
		strcpy(sysfs_isp_core, "1.10V");
#if defined(CONFIG_MACH_M3) || defined(CONFIG_MACH_M0_DUOSCTC)
		state->pdata->set_vdd_core(1150000);
		vdd_core_val = 1150000;
#else
		state->pdata->set_vdd_core(1100000);
		vdd_core_val = 1100000;
#endif
	} else if (read_val & 0x2000) {
		strcpy(sysfs_isp_core, "1.05V");
		state->pdata->set_vdd_core(1100000);
		vdd_core_val = 1100000;
	} else if (read_val & 0x8000) {
		strcpy(sysfs_isp_core, "1.00V");
		state->pdata->set_vdd_core(1000000);
		vdd_core_val = 1000000;
	} else {
		strcpy(sysfs_isp_core, "1.15V");
		state->pdata->set_vdd_core(1150000);
		vdd_core_val = 1150000;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(S5C73M3_CORE_VDD,
		O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (IS_ERR(fp))
		goto out;

	buf = vmalloc(10);
	if (!buf) {
		cam_err("failed to allocate memory\n");
		err = -ENOMEM;
		goto out;
	}

	sprintf(buf, "%d\n", vdd_core_val);

	err = vfs_write(fp, (char __user *)buf, 10, &fp->f_pos);
	/*cam_dbg("return value of vfs_write = %d\n", err);*/
out:
	if (buf != NULL)
		vfree(buf);

	if (fp !=  NULL)
		filp_close(fp, current->files);

	set_fs(old_fs);
	cam_trace("X\n");

	return 0;
}

static int s5c73m3_set_timing_register_for_vdd(struct v4l2_subdev *sd)
{
	int err = 0;

	err = s5c73m3_write(sd, 0x3010, 0x0018, 0x0618);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3010, 0x001C, 0x10C1);
	CHECK_ERR(err);
	err = s5c73m3_write(sd, 0x3010, 0x0020, 0x249E);
	CHECK_ERR(err);

	return err;
}

static int s5c73m3_init(struct v4l2_subdev *sd, u32 val)
{
	struct s5c73m3_state *state = to_state(sd);
	int err = 0;
	int retVal = 0;
	sd_internal = sd;

	/* Default state values */
	state->isp.bad_fw = 1;

	state->preview = NULL;
	state->capture = NULL;
	state->fw_index = S5C73M3_PATH_MAX;

	state->format_mode = V4L2_PIX_FMT_MODE_PREVIEW;
	state->sensor_mode = SENSOR_CAMERA;
	state->flash_mode = FLASH_MODE_OFF;
	state->wb_mode = WHITE_BALANCE_AUTO;
	state->focus.mode = FOCUS_MODE_CONTINOUS_PICTURE;
	state->focus.touch = 0;

	state->fps = 0;			/* auto */

	memset(&state->focus, 0, sizeof(state->focus));

	if (!state->pdata->is_vdd_core_set())
		s5c73m3_read_vdd_core(sd);

	cam_dbg("vdd core value from OTP : %s", sysfs_isp_core);
	cam_dbg("chip info from OTP : %#x, %#x, %#x\n",
		isp_chip_info1, isp_chip_info2, isp_chip_info3);

#ifdef S5C73M3_FROM_BOOTING
	err = s5c73m3_FROM_booting(sd);
#else
	err = s5c73m3_set_timing_register_for_vdd(sd);
	CHECK_ERR(err);

	err = s5c73m3_check_fw(sd, 0);
	if (err < 0) {
		cam_dbg("isp.bad_fw is true\n");
		state->isp.bad_fw = 1;
	}
#endif
	CHECK_ERR(err);

	err = s5c73m3_i2c_check_status_with_CRC(sd);
	if (err < 0) {
		cam_err("ISP is not ready. retry loading fw!!\n");
		/* retry */
		retVal = s5c73m3_check_fw_date(sd);

		/* retVal = 0 : Same Version
		retVal < 0 : Phone Version is latest Version than sensorFW.
		retVal > 0 : Sensor Version is latest version than phoenFW. */
		if (retVal <= 0) {
			cam_dbg("Loading From PhoneFW......\n");
			err = s5c73m3_reset_module(sd, false);
			CHECK_ERR(err);
			err = s5c73m3_SPI_booting(sd);
			CHECK_ERR(err);
		} else {
			cam_dbg("Loading From SensorFW......\n");
			err = s5c73m3_reset_module(sd, true);
			CHECK_ERR(err);
			err = s5c73m3_get_sensor_fw_binary(sd);
			CHECK_ERR(err);
		}
	}

#if defined(CONFIG_MACH_BAFFIN) && !defined(CONFIG_MACH_SUPERIOR_KOR_SKT)
	/* send command to change resolution table */
	/* 0:1280x720 TABLE(16:9), 1:800x480 TABLE(5:3) */
	err = s5c73m3_writeb(sd, 0x0B1A, 0x0001);
	CHECK_ERR(err);
#endif

	state->isp.bad_fw = 0;
	s5c73m3_init_param(sd);

	return 0;
}

static const struct v4l2_subdev_core_ops s5c73m3_core_ops = {
	.init = s5c73m3_init,		/* initializing API */
	.load_fw = s5c73m3_load_fw,
	.queryctrl = s5c73m3_queryctrl,
	.g_ctrl = s5c73m3_g_ctrl,
	.s_ctrl = s5c73m3_s_ctrl,
	.g_ext_ctrls = s5c73m3_g_ext_ctrls,
};

static const struct v4l2_subdev_video_ops s5c73m3_video_ops = {
	.s_mbus_fmt = s5c73m3_s_fmt,
	.g_parm = s5c73m3_g_parm,
	.s_parm = s5c73m3_s_parm,
	.enum_framesizes = s5c73m3_enum_framesizes,
	.s_stream = s5c73m3_s_stream,
};

static const struct v4l2_subdev_ops s5c73m3_ops = {
	.core = &s5c73m3_core_ops,
	.video = &s5c73m3_video_ops,
};

static ssize_t s5c73m3_camera_rear_camtype_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char type[] = "CML0801";
	return sprintf(buf, "%s\n", type);
}

static ssize_t s5c73m3_camera_rear_camfw_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s %s\n", sysfs_sensor_fw, sysfs_phone_fw);
}

static ssize_t s5c73m3_camera_rear_flash(struct device *dev,
	struct device_attribute *attr, const char *buf,
	size_t count)
{
#ifdef CONFIG_LEDS_AAT1290A
	return aat1290a_power(dev, attr, buf, count);
#else
	return count;
#endif
}

static ssize_t s5c73m3_camera_isp_core_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	char core[10];

	strcpy(core, sysfs_isp_core);
	return sprintf(buf, "%s\n", core);
}

static DEVICE_ATTR(rear_camtype, S_IRUGO,
		s5c73m3_camera_rear_camtype_show, NULL);
static DEVICE_ATTR(rear_camfw, S_IRUGO, s5c73m3_camera_rear_camfw_show, NULL);
static DEVICE_ATTR(rear_flash, S_IWUSR|S_IWGRP|S_IROTH,
	NULL, s5c73m3_camera_rear_flash);
static DEVICE_ATTR(isp_core, S_IRUGO, s5c73m3_camera_isp_core_show, NULL);

/*
 * s5c73m3_probe
 * Fetching platform data is being done with s_config subdev call.
 * In probe routine, we just register subdev device
 */
static int __devinit s5c73m3_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct s5c73m3_state *state;
	struct v4l2_subdev *sd;

	state = kzalloc(sizeof(struct s5c73m3_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	sd = &state->sd;
	strcpy(sd->name, S5C73M3_DRIVER_NAME);

	state->pdata = client->dev.platform_data;

	/* Registering subdev */
	v4l2_i2c_subdev_init(sd, client, &s5c73m3_ops);

#ifdef CAM_DEBUG
	state->dbg_level = CAM_DEBUG;
#endif

#ifdef S5C73M3_BUSFREQ_OPP
	/* lock bus frequency */
	if (samsung_rev() >= EXYNOS4412_REV_2_0)
		dev_lock(bus_dev, s5c73m3_dev, 440220);
	else
		dev_lock(bus_dev, s5c73m3_dev, 400200);
#endif

	if (s5c73m3_dev)
		dev_set_drvdata(s5c73m3_dev, state);

	printk(KERN_DEBUG "%s\n", __func__);

	return 0;
}

static int __devexit s5c73m3_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5c73m3_state *state = to_state(sd);

	if (unlikely(state->isp.bad_fw)) {
		cam_err("camera is not ready!!\n");
	} else {
		if (s5c73m3_set_af_softlanding(sd) < 0)
			cam_err("failed to set soft landing\n");
	}
	v4l2_device_unregister_subdev(sd);

#ifdef S5C73M3_BUSFREQ_OPP
	/* Unlock bus frequency */
	dev_unlock(bus_dev, s5c73m3_dev);
#endif

	kfree(state);

	return 0;
}

static const struct i2c_device_id s5c73m3_id[] = {
	{ S5C73M3_DRIVER_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, s5c73m3_id);

static struct i2c_driver s5c73m3_i2c_driver = {
	.driver = {
		.name	= S5C73M3_DRIVER_NAME,
	},
	.probe		= s5c73m3_probe,
	.remove		= __devexit_p(s5c73m3_remove),
	.id_table	= s5c73m3_id,
};

static int __init s5c73m3_mod_init(void)
{
#ifdef S5C73M3_BUSFREQ_OPP
	/* To lock bus frequency in OPP mode */
	bus_dev = dev_get("exynos-busfreq");
#endif

	if (!s5c73m3_dev) {
		s5c73m3_dev = device_create(camera_class,
				NULL, 0, NULL, "rear");
		if (IS_ERR(s5c73m3_dev)) {
			cam_warn("failed to create device!\n");
			return 0;
		}

		if (device_create_file(s5c73m3_dev, &dev_attr_rear_camtype)
				< 0) {
			cam_warn("failed to create device file, %s\n",
					dev_attr_rear_camtype.attr.name);
		}

		if (device_create_file(s5c73m3_dev, &dev_attr_rear_camfw) < 0) {
			cam_warn("failed to create device file, %s\n",
					dev_attr_rear_camfw.attr.name);
		}

		if (device_create_file(s5c73m3_dev, &dev_attr_rear_flash) < 0) {
			cam_warn("failed to create device file, %s\n",
					dev_attr_rear_flash.attr.name);
		}

		if (device_create_file(s5c73m3_dev, &dev_attr_isp_core) < 0) {
			cam_warn("failed to create device file, %s\n",
					dev_attr_isp_core.attr.name);
		}
	}

	return i2c_add_driver(&s5c73m3_i2c_driver);
}

static void __exit s5c73m3_mod_exit(void)
{
	i2c_del_driver(&s5c73m3_i2c_driver);
}
module_init(s5c73m3_mod_init);
module_exit(s5c73m3_mod_exit);


MODULE_DESCRIPTION("driver for LSI S5C73M3");
MODULE_LICENSE("GPL");
