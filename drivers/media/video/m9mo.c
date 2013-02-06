/*
 * driver for Fusitju M9MO LS 8MP camera
 *
 * Copyright (c) 2010, Samsung Electronics. All rights reserved
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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/videodev2.h>

#include <mach/dev.h>
#include <plat/cpu.h>

#ifdef CONFIG_VIDEO_SAMSUNG_V4L2
#include <linux/videodev2_exynos_camera.h>
#include <linux/videodev2_exynos_media.h>
#endif

#include <linux/regulator/machine.h>

#include <media/m9mo_platform.h>
#include "m9mo.h"

/* #define M9MO_ISP_DEBUG  //ISP Debug */

#define M9MO_DRIVER_NAME	"M9MO"
#if 1
#define M9MO_BUS_FREQ_LOCK
#endif
#if 0
#define HOLD_LENS_SUPPORT
#endif

extern struct class *camera_class;
struct device *m9mo_dev;
#ifdef HOLD_LENS_SUPPORT
static bool leave_power;
#endif
#ifdef M9MO_BUS_FREQ_LOCK
struct device *bus_dev;
#endif

#if 0
#define M9MO_FW_PATH		"/data/RS_M9MO.bin"
#define FW_INFO_PATH		"/data/FW_INFO.bin"
#endif

#define M9MO_FW_PATH		"/sdcard/RS_M9MO.bin"

#define M9MO_FW_REQ_PATH	"RS_M9MO.bin"
#define M9MO_EVT31_FW_REQ_PATH	"RS_M9MO_EVT3.1.bin"

#define FW_INFO_PATH		"/sdcard/FW_INFO.bin"


#define M9MO_FW_DUMP_PATH	"/sdcard/M9MO_dump.bin"

#if 0
#define M9MO_FACTORY_CSV_PATH	"/data/FACTORY_CSV_RAW.bin"
#endif
#define M9MO_FACTORY_CSV_PATH	"/mnt/sdcard/FACTORY_CSV_RAW.bin"

#define M9MOTB_FW_PATH "RS_M9LS_TB.bin" /* TECHWIN - SONY */
/* #define M9MOON_FW_PATH "RS_M9LS_ON.bin" */ /* FIBEROPTICS - SONY */
/* #define M9MOOM_FW_PATH "RS_M9LS_OM.bin" */ /* FIBEROPTICS - S.LSI */
#if defined(CONFIG_MACH_U1_KOR_LGT)
#define M9MOSB_FW_PATH "RS_M9LS_SB.bin" /* ELECTRO-MECHANICS - SONY */
#endif
/* #define M9MOSC_FW_PATH "RS_M9LS_SC.bin" */ /* ELECTRO-MECHANICS - S.LSI */
/* #define M9MOCB_FW_PATH "RS_M9LS_CB.bin" */ /* CAMSYS - SONY */
#if defined(CONFIG_TARGET_LOCALE_NA)
/* #define M9MOOE_FW_PATH "RS_M9LS_OE.bin" */ /* FIBEROPTICS - SONY */
#endif
#if defined(CONFIG_MACH_Q1_BD)
#define M9MOOO_FW_PATH "RS_M9LS_OO.bin" /* FIBEROPTICS - SONY */
#endif

#if 0
#define M9MO_FW_VER_LEN		22
#define M9MO_FW_VER_FILE_CUR	0x16FF00
#define M9MO_FW_VER_NUM		0x000018
#else
#define M9MO_FW_VER_LEN	20
#define M9MO_SEN_FW_VER_LEN	30
#define M9MO_FW_VER_FILE_CUR	0x1FF080
#define M9MO_FW_VER_NUM		0x1FF080
#endif

#define FACTORY_RESOL_WIDE 106
#define FACTORY_RESOL_TELE 107
#define FACTORY_RESOL_WIDE_INSIDE 131
#define FACTORY_RESOL_TELE_INSIDE 132
#define FACTORY_TILT_TEST_INSIDE 133

#define M9MO_FLASH_BASE_ADDR	0x00000000

#define M9MO_FLASH_READ_BASE_ADDR	0x000000

#define M9MO_FLASH_BASE_ADDR_1	0x001FF000

u32 M9MO_FLASH_FACTORY_OIS[] = {0x27E031A2, 0x27E031C7};
u32 M9MO_FLASH_FACTORY_VIB[] = {0x27E031C8, 0x27E031D1};
u32 M9MO_FLASH_FACTORY_GYRO[] = {0x27E031D2, 0x27E031D7};
u32 M9MO_FLASH_FACTORY_TELE_RESOL[] = {0x27E03298, 0x27E0329F};
u32 M9MO_FLASH_FACTORY_WIDE_RESOL[] = {0x27E032A0, 0x27E032A7};
u32 M9MO_FLASH_FACTORY_AF_FCS[] = {0x27E0323A, 0x27E03275};
u32 M9MO_FLASH_FACTORY_PUNT[] = {0x27E031D8, 0x27E03239};
u32 M9MO_FLASH_FACTORY_DECENTER[] = {0x27E032EC, 0x27E03303};

u32 M9MO_FLASH_FACTORY_BACKLASH[] = {0x27E03276, 0x27E03279};

u32 M9MO_FLASH_FACTORY_AF_LED[] = {0x27E032A8, 0x27E032AD};
u32 M9MO_FLASH_FACTORY_IRIS[] = {0x27E030E8, 0x27E03107};
u32 M9MO_FLASH_FACTORY_LIVEVIEW[] = {0x27E03108, 0x27E0310F};
u32 M9MO_FLASH_FACTORY_GAIN_CAPTURE[] = {0x27E03110, 0x27E03117};
u32 M9MO_FLASH_FACTORY_SH_CLOSE[] = {0x27E0327A, 0x27E03297};
u32 M9MO_FLASH_FACTORY_FLASH_CHECK[] = {0x27E032AE, 0x27E032B0};
u32 M9MO_FLASH_FACTORY_WB_ADJ[] = {0x27E03000, 0x27E03059};
u32 M9MO_FLASH_FACTORY_FLASH_WB[] = {0x27E032B4, 0x27E032C3};
u32 M9MO_FLASH_FACTORY_ADJ_FLASH_WB[] = {0x27E032D0, 0x27E032EB};

u32 M9MO_FLASH_FACTORY_IR_CHECK[] = {0x27E032C4, 0x27E032CD};

u32 M9MO_FLASH_FACTORY_RESULT = 0x27E03128;

#define M9MO_INT_RAM_BASE_ADDR	0x01100000

#define M9MO_I2C_RETRY		5
#define M9MO_I2C_VERIFY		100
/* TODO
   Timeout delay is changed to 35 sec to support large shutter speed.
   This value must be set according to shutter speed.
*/
#define M9MO_ISP_TIMEOUT		5000
#define M9MO_ISP_CAPTURE_TIMEOUT	35000
#define M9MO_SOUND_TIMEOUT		35000
#define M9MO_ISP_AFB_TIMEOUT	15000 /* FIXME */
#define M9MO_ISP_ESD_TIMEOUT	1000

#define M9MO_JPEG_MAXSIZE	0x17E8000 /* 25M 4K align */
#define M9MO_THUMB_MAXSIZE	0x0
#define M9MO_POST_MAXSIZE	0x0

#define M9MO_DEF_APEX_DEN	100
#define EXIF_ONE_THIRD_STOP_STEP
/*
#define EXIF_ONE_HALF_STOP_STEP
*/

#define m9mo_readb(sd, g, b, v) m9mo_read(__LINE__, sd, 1, g, b, v, true)
#define m9mo_readw(sd, g, b, v) m9mo_read(__LINE__, sd, 2, g, b, v, true)
#define m9mo_readl(sd, g, b, v) m9mo_read(__LINE__, sd, 4, g, b, v, true)

#define m9mo_writeb(sd, g, b, v) m9mo_write(__LINE__, sd, 1, g, b, v, true)
#define m9mo_writew(sd, g, b, v) m9mo_write(__LINE__, sd, 2, g, b, v, true)
#define m9mo_writel(sd, g, b, v) m9mo_write(__LINE__, sd, 4, g, b, v, true)

#define m9mo_readb2(sd, g, b, v) m9mo_read(__LINE__, sd, 1, g, b, v, false)
#define m9mo_readw2(sd, g, b, v) m9mo_read(__LINE__, sd, 2, g, b, v, false)
#define m9mo_readl2(sd, g, b, v) m9mo_read(__LINE__, sd, 4, g, b, v, false)

#define m9mo_writeb2(sd, g, b, v) m9mo_write(__LINE__, sd, 1, g, b, v, false)
#define m9mo_writew2(sd, g, b, v) m9mo_write(__LINE__, sd, 2, g, b, v, false)
#define m9mo_writel2(sd, g, b, v) m9mo_write(__LINE__, sd, 4, g, b, v, false)

#define CHECK_ERR(x)	if ((x) <= 0) { \
				cam_err("i2c failed, err %d\n", x); \
				return x; \
			}

#define NELEMS(array) (sizeof(array) / sizeof(array[0]))

#if 1
#define FAST_CAPTURE
#endif

static const struct m9mo_frmsizeenum preview_frmsizes[] = {
	{ M9MO_PREVIEW_QCIF,	176,	144,	0x05 },	/* 176 x 144 */
	{ M9MO_PREVIEW_QVGA,	320,	240,	0x09 },
	{ M9MO_PREVIEW_VGA,	640,	480,	0x17 },
	{ M9MO_PREVIEW_D1,	768,	512,	0x33 }, /* High speed */
	{ M9MO_PREVIEW_960_720,	960,	720,	0x34 },
	{ M9MO_PREVIEW_1080_720,	1056,	704,	0x35 },
	{ M9MO_PREVIEW_720P,	1280,	720,	0x21 },
	{ M9MO_PREVIEW_1080P,	1920,	1080,	0x28 },
	{ M9MO_PREVIEW_HDR,	3264,	2448,	0x27 },
	{ M9MO_PREVIEW_720P_60FPS,	1280, 720,	   0x25 },
	{ M9MO_PREVIEW_VGA_60FPS,	640, 480,	   0x2F },
	{ M9MO_PREVIEW_1080P_DUAL,	1920,	1080,	0x2C },
	{ M9MO_PREVIEW_720P_DUAL,	1280,	720,	0x2D },
	{ M9MO_PREVIEW_VGA_DUAL,	640,	480,	0x2E },
	{ M9MO_PREVIEW_QVGA_DUAL,	320,	240,	0x36 },
	{ M9MO_PREVIEW_1440_1080,	1440,	1080,	0x37 },
};

static const struct m9mo_frmsizeenum capture_frmsizes[] = {
	{ M9MO_CAPTURE_HD,	960,	720,	0x34 },
	{ M9MO_CAPTURE_1MP,	1024,	768,	0x0F },
	{ M9MO_CAPTURE_2MPW,	1920,	1080,	0x19 },
	{ M9MO_CAPTURE_3MP,	1984,	1488,	0x2F },
	{ M9MO_CAPTURE_4MP,	2304,	1728,	0x1E },
	{ M9MO_CAPTURE_5MP,	2592,	1944,	0x20 },
	{ M9MO_CAPTURE_8MP,	3264,	2448,	0x25 },
	{ M9MO_CAPTURE_10MP,	3648,	2736,	0x30 },
	{ M9MO_CAPTURE_12MPW,	4608,	2592,	0x31 },
	{ M9MO_CAPTURE_14MP,	4608,	3072,	0x32 },
	{ M9MO_CAPTURE_16MP,	4608,	3456,	0x33 },
};

static const struct m9mo_frmsizeenum postview_frmsizes[] = {
	{ M9MO_CAPTURE_POSTQVGA,	320,	240,	0x01 },
	{ M9MO_CAPTURE_POSTVGA,		640,	480,	0x08 },
	{ M9MO_CAPTURE_POSTWVGA,	800,	480,	0x09 },
	{ M9MO_CAPTURE_POSTHD,		960,	720,	0x13 },
	{ M9MO_CAPTURE_POSTP,		1056,	704,	0x14 },
	{ M9MO_CAPTURE_POSTWHD,		1280,	720,	0x0F },
};

static struct m9mo_control m9mo_ctrls[] = {
	{
		.id = V4L2_CID_CAMERA_ISO,
		.minimum = ISO_AUTO,
		.maximum = ISO_3200,
		.step = 1,
		.value = ISO_AUTO,
		.default_value = ISO_AUTO,
	}, {
		.id = V4L2_CID_CAMERA_BRIGHTNESS,
		.minimum = EXPOSURE_MINUS_6,
		.maximum = EXPOSURE_PLUS_6,
		.step = 1,
		.value = EXPOSURE_DEFAULT,
		.default_value = EXPOSURE_DEFAULT,
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
		.id = V4L2_CID_CAMERA_CONTRAST,
		.minimum = CONTRAST_MINUS_2,
		.maximum = CONTRAST_MAX - 1,
		.step = 1,
		.value = CONTRAST_DEFAULT,
		.default_value = CONTRAST_DEFAULT,
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
	}, {
		.id = V4L2_CID_CAMERA_ANTI_BANDING,
		.minimum = ANTI_BANDING_AUTO,
		.maximum = ANTI_BANDING_OFF,
		.step = 1,
		.value = ANTI_BANDING_AUTO,
		.default_value = ANTI_BANDING_AUTO,
	},
};

static u8 sysfs_sensor_fw[7] = {0,};
static u8 sysfs_phone_fw[7] = {0,};
static u8 sysfs_sensor_type[25] = {0,};

static int m9mo_init(struct v4l2_subdev *sd, u32 val);
static int m9mo_post_init(struct v4l2_subdev *sd, u32 val);

static inline struct m9mo_state *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct m9mo_state, sd);
}

static int m9mo_read(int _line, struct v4l2_subdev *sd,
	u8 len, u8 category, u8 byte, int *val, bool log)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg;
	unsigned char data[5];
	unsigned char recv_data[len + 1];
	int i, err = 0;
	int retry = 3;

	if (!client->adapter)
		return -ENODEV;

	if (len != 0x01 && len != 0x02 && len != 0x04)
		return -EINVAL;

i2c_retry:
	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = sizeof(data);
	msg.buf = data;

	/* high byte goes out first */
	data[0] = msg.len;
	data[1] = 0x01;			/* Read category parameters */
	data[2] = category;
	data[3] = byte;
	data[4] = len;

	for (i = M9MO_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	if (err != 1) {
		cam_err("category %#x, byte %#x, err %d\n",
			category, byte, err);
		return err;
	}

	msg.flags = I2C_M_RD;
	msg.len = sizeof(recv_data);
	msg.buf = recv_data;
	for (i = M9MO_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	if (err != 1) {
		cam_err("RD category %#x, byte %#x, err %d\n",
			category, byte, err);
		return err;
	}

	if (recv_data[0] != sizeof(recv_data)) {
#if 0
		cam_i2c_dbg("expected length %d, but return length %d\n",
				 sizeof(recv_data), recv_data[0]);
#endif
		if (retry > 0) {
			retry--;
			msleep(20);
			goto i2c_retry;
		} else {
			cam_err("Retry all failed for expected length error.");
			return -1;
		}
	}

	if (len == 0x01)
		*val = recv_data[1];
	else if (len == 0x02)
		*val = recv_data[1] << 8 | recv_data[2];
	else
		*val = recv_data[1] << 24 | recv_data[2] << 16 |
				recv_data[3] << 8 | recv_data[4];

	if (log)
		cam_i2c_dbg("[ %4d ] Read %s %#02x, byte %#x, value %#x\n",
			_line, (len == 4 ? "L" : (len == 2 ? "W" : "B")),
			category, byte, *val);

	return err;
}

static int m9mo_write(int _line, struct v4l2_subdev *sd,
	u8 len, u8 category, u8 byte, int val, bool log)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg;
	unsigned char data[len + 4];
	int i, err;

	if (!client->adapter)
		return -ENODEV;

	if (len != 0x01 && len != 0x02 && len != 0x04)
		return -EINVAL;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = sizeof(data);
	msg.buf = data;

	data[0] = msg.len;
	data[1] = 0x02;			/* Write category parameters */
	data[2] = category;
	data[3] = byte;
	if (len == 0x01) {
		data[4] = val & 0xFF;
	} else if (len == 0x02) {
		data[4] = (val >> 8) & 0xFF;
		data[5] = val & 0xFF;
	} else {
		data[4] = (val >> 24) & 0xFF;
		data[5] = (val >> 16) & 0xFF;
		data[6] = (val >> 8) & 0xFF;
		data[7] = val & 0xFF;
	}

	if (log)
		cam_i2c_dbg("[ %4d ] Write %s %#x, byte %#x, value %#x\n",
			_line, (len == 4 ? "L" : (len == 2 ? "W" : "B")),
			category, byte, val);

	for (i = M9MO_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	return err;
}
static int m9mo_mem_dump(struct v4l2_subdev *sd, u16 len, u32 addr, u8 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg;
	unsigned char data[8];
	unsigned char recv_data[len + 3];
	int i, err = 0;

	if (!client->adapter)
		return -ENODEV;

	if (len <= 0)
		return -EINVAL;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = sizeof(data);
	msg.buf = data;

	/* high byte goes out first */
	data[0] = 0x00;
	data[1] = 0x03;
	data[2] = 0x18;
	data[3] = (addr >> 16) & 0xFF;
	data[4] = (addr >> 8) & 0xFF;
	data[5] = addr & 0xFF;
	data[6] = (len >> 8) & 0xFF;
	data[7] = len & 0xFF;

	for (i = M9MO_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	if (err != 1)
		return err;

	msg.flags = I2C_M_RD;
	msg.len = sizeof(recv_data);
	msg.buf = recv_data;
	for (i = M9MO_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	if (err != 1)
		return err;

	if (len != (recv_data[1] << 8 | recv_data[2]))
		cam_i2c_dbg("expected length %d, but return length %d\n",
			len, recv_data[1] << 8 | recv_data[2]);

	memcpy(val, recv_data + 3, len);

	cam_i2c_dbg("address %#x, length %d\n", addr, len);
	return err;
}
static int m9mo_mem_read(struct v4l2_subdev *sd, u16 len, u32 addr, u8 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg;
	unsigned char data[8];
	unsigned char recv_data[len + 3];
	int i, err = 0;

	if (!client->adapter)
		return -ENODEV;

	if (len <= 0)
		return -EINVAL;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = sizeof(data);
	msg.buf = data;

	/* high byte goes out first */
	data[0] = 0x00;
	data[1] = 0x03;
	data[2] = (addr >> 24) & 0xFF;
	data[3] = (addr >> 16) & 0xFF;
	data[4] = (addr >> 8) & 0xFF;
	data[5] = addr & 0xFF;
	data[6] = (len >> 8) & 0xFF;
	data[7] = len & 0xFF;

	for (i = M9MO_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	if (err != 1)
		return err;

	msg.flags = I2C_M_RD;
	msg.len = sizeof(recv_data);
	msg.buf = recv_data;
	for (i = M9MO_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	if (err != 1)
		return err;

	if (len != (recv_data[1] << 8 | recv_data[2]))
		cam_i2c_dbg("expected length %d, but return length %d\n",
			len, recv_data[1] << 8 | recv_data[2]);

	memcpy(val, recv_data + 3, len);

	cam_i2c_dbg("address %#x, length %d\n", addr, len);
	return err;
}

static int m9mo_mem_write(struct v4l2_subdev *sd, u8 cmd,
		u16 len, u32 addr, u8 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct i2c_msg msg;
	unsigned char data[len + 8];
	int i, err = 0;

	if (!client->adapter)
		return -ENODEV;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = sizeof(data);
	msg.buf = data;

	/* high byte goes out first */
	data[0] = 0x00;
	data[1] = cmd;
	data[2] = (addr >> 24) & 0xFF;
	data[3] = (addr >> 16) & 0xFF;
	data[4] = (addr >> 8) & 0xFF;
	data[5] = addr & 0xFF;
	data[6] = (len >> 8) & 0xFF;
	data[7] = len & 0xFF;
	memcpy(data + 2 + sizeof(addr) + sizeof(len), val, len);

	cam_i2c_dbg("address %#x, length %d\n", addr, len);

	for (i = M9MO_I2C_RETRY; i; i--) {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		msleep(20);
	}

	return err;
}

static irqreturn_t m9mo_isp_isr(int irq, void *dev_id)
{
	struct v4l2_subdev *sd = (struct v4l2_subdev *)dev_id;
	struct m9mo_state *state = to_state(sd);

	cam_trace("**************** interrupt ****************\n");
	state->isp.issued = 1;
	wake_up(&state->isp.wait);

	return IRQ_HANDLED;
}

static u32 m9mo_wait_interrupt(struct v4l2_subdev *sd,
	unsigned int timeout)
{
	struct m9mo_state *state = to_state(sd);
	int try_cnt = 60;
	cam_trace("E\n");

#if 0
	if (wait_event_interruptible_timeout(state->isp.wait,
		state->isp.issued == 1,
		msecs_to_jiffies(timeout)) == 0) {
		cam_err("timeout ~~~~~~~~~~~~~~~~~~~~~\n");
		return 0;
	}
#else
	if (wait_event_timeout(state->isp.wait,
				state->isp.issued == 1,
				msecs_to_jiffies(timeout)) == 0) {
		cam_err("timeout ~~~~~~~~~~~~~~~~~~~~~~~\n");
		return 0;
	}
#endif

	state->isp.issued = 0;

	do {
		m9mo_readw(sd, M9MO_CATEGORY_SYS,
			M9MO_SYS_INT_FACTOR, &state->isp.int_factor);
		cam_err(": state->isp.int_factor = %x\n",
					state->isp.int_factor);
		if (state->isp.int_factor == 0xFFFF) {
			try_cnt--;
			msleep(10);
		} else
			try_cnt = 0;
	} while (try_cnt);

	cam_trace("X %s\n",
		(state->isp.int_factor == 0xFFFF ? "fail(0xFFFF)" : ""));
	return state->isp.int_factor;
}

static int m9mo_wait_framesync(struct v4l2_subdev *sd)
{
	int i, frame_sync_count = 0;
	u32 int_factor;
	s32 read_val = 0;
	struct m9mo_state *state = to_state(sd);

	 if (state->running_capture_mode == RUNNING_MODE_AE_BRACKET
		|| state->running_capture_mode == RUNNING_MODE_LOWLIGHT
		|| state->running_capture_mode == RUNNING_MODE_HDR) {
		cam_dbg("Start AE Bracket or HDR capture\n");
		frame_sync_count = 3;
	} else if (state->running_capture_mode == RUNNING_MODE_BLINK) {
		cam_dbg("Start FaceDetect EyeBlink capture\n");
		frame_sync_count = 3;
	}

	/* Clear Interrupt factor */
	for (i = frame_sync_count; i; i--) {
		int_factor = m9mo_wait_interrupt(sd,
				M9MO_SOUND_TIMEOUT);
		if (!(int_factor & M9MO_INT_FRAME_SYNC)) {
			cam_warn("M9MO_INT_FRAME_SYNC isn't issued, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
		m9mo_readb(sd,
				M9MO_CATEGORY_SYS,
				M9MO_SYS_FRAMESYNC_CNT,
				&read_val);
		cam_dbg("Frame interrupt FRAME_SYNC cnt[%d]\n",
				read_val);
	}

	return 0;
}

static int m9mo_set_smart_auto_default_value(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err, value;

	cam_trace("E %d\n", val);

	if (val == 1) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_EDGE_CTRL, state->sharpness);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_CHROMA_LVL, state->sharpness);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);
	} else {
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x05);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_MODE_CAP, 0x10);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_STROBE_EN, state->strobe_en);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_INDEX, 0x1E);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_WDR_EN, 0x0);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_EDGE_CTRL, 0x02);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_CHROMA_LVL, 0x03);
		CHECK_ERR(err);

		err = m9mo_readb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, &value);
		CHECK_ERR(err);

		if (value == 0x11 || value == 0x21) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
				M9MO_MON_COLOR_EFFECT, state->color_effect);
			CHECK_ERR(err);
		}

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			0xAE, 0x0);
		CHECK_ERR(err);
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_mode(struct v4l2_subdev *sd, u32 mode)
{
	int i, err;
	u32 old_mode, val;
	u32 int_factor, int_en;
	int retry_mode_change = 1;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E\n");

	err = m9mo_readb(sd, M9MO_CATEGORY_SYS, M9MO_SYS_MODE, &old_mode);
	CHECK_ERR(err);

	if (state->samsung_app) {
		/* don't change mode when cap -> param */
		if (old_mode == M9MO_STILLCAP_MODE && mode == M9MO_PARMSET_MODE)
			return 10;
	}

	/* Dual Capture */
	if (state->dual_capture_start && mode == M9MO_STILLCAP_MODE)
		mode = M9MO_PARMSET_MODE;

	if (old_mode == mode) {
		cam_dbg("%#x -> %#x\n", old_mode, mode);
		return old_mode;
	}

	cam_dbg("%#x -> %#x\n", old_mode, mode);

retry_mode_set:
	switch (old_mode) {
	case M9MO_SYSINIT_MODE:
		cam_warn("sensor is initializing\n");
		err = -EBUSY;
		break;

	case M9MO_PARMSET_MODE:
		if (mode == M9MO_STILLCAP_MODE) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_SYS,
				M9MO_SYS_MODE, M9MO_MONITOR_MODE);
			if (err <= 0)
				break;
			for (i = M9MO_I2C_VERIFY; i; i--) {
				err = m9mo_readb(sd, M9MO_CATEGORY_SYS,
					M9MO_SYS_MODE, &val);
				if (val == M9MO_MONITOR_MODE)
					break;
				msleep(20);
			}
		}
	case M9MO_MONITOR_MODE:
	case M9MO_STILLCAP_MODE:
		err = m9mo_writeb(sd, M9MO_CATEGORY_SYS, M9MO_SYS_MODE, mode);
		break;

	default:
		cam_warn("current mode is unknown, %d\n", old_mode);
		err = 1;/* -EINVAL; */
	}

	if (err <= 0)
		return err;

	for (i = M9MO_I2C_VERIFY; i; i--) {
		err = m9mo_readb(sd, M9MO_CATEGORY_SYS, M9MO_SYS_MODE, &val);
		if (val == mode)
			break;
		msleep(20);
	}

	if (val != mode) {
		if (retry_mode_change) {
			retry_mode_change = 0;
			goto retry_mode_set;
		} else {
			cam_warn("ISP mode not change, %d -> %d\n", val, mode);
			return -ETIMEDOUT;
		}
	}

	if (mode == M9MO_STILLCAP_MODE
		&& state->running_capture_mode != RUNNING_MODE_AE_BRACKET
		&& state->running_capture_mode != RUNNING_MODE_LOWLIGHT
		&& state->running_capture_mode != RUNNING_MODE_HDR
		&& state->running_capture_mode != RUNNING_MODE_BLINK) {

		m9mo_wait_framesync(sd);

		if (state->running_capture_mode == RUNNING_MODE_WB_BRACKET
			|| state->running_capture_mode == RUNNING_MODE_RAW) {
			err = m9mo_readw(sd, M9MO_CATEGORY_SYS,
						M9MO_SYS_INT_EN, &int_en);
			CHECK_ERR(err);

			if (int_en & M9MO_INT_SOUND) {
				/* Clear Interrupt factor */
				int_factor = m9mo_wait_interrupt(sd,
						M9MO_SOUND_TIMEOUT);
				if (!(int_factor & M9MO_INT_SOUND)) {
					cam_warn("M9MO_INT_SOUND isn't issued, %#x\n",
							int_factor);
					return -ETIMEDOUT;
				}
			}
		}
		/* Clear Interrupt factor */
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_CAPTURE_TIMEOUT);
		if (!(int_factor & M9MO_INT_CAPTURE)) {
			cam_warn("M9MO_INT_CAPTURE isn't issued, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
	}

	if (state->mode == MODE_SMART_AUTO) {
		if (old_mode == M9MO_STILLCAP_MODE && mode == M9MO_MONITOR_MODE)
			m9mo_set_smart_auto_default_value(sd, 0);
	}

	state->isp_mode = mode;

	cam_trace("X\n");
	return old_mode;
}

static int m9mo_set_mode_part1(struct v4l2_subdev *sd, u32 mode)
{
	int i, err;
	u32 old_mode, val;
	u32 int_factor;
	u32 int_en;
	int retry_mode_change = 1;
	struct m9mo_state *state = to_state(sd);
	state->stream_on_part2 = false;

	cam_trace("E\n");

	err = m9mo_readb(sd, M9MO_CATEGORY_SYS, M9MO_SYS_MODE, &old_mode);
	CHECK_ERR(err);

	if (state->samsung_app) {
		/* don't change mode when cap -> param */
		if (old_mode == M9MO_STILLCAP_MODE && mode == M9MO_PARMSET_MODE)
			return 10;
	}

	/* Dual Capture */
	if (state->dual_capture_start && mode == M9MO_STILLCAP_MODE)
		mode = M9MO_PARMSET_MODE;

	if (old_mode == mode) {
		cam_dbg("%#x -> %#x\n", old_mode, mode);
		return old_mode;
	}

	cam_dbg("%#x -> %#x\n", old_mode, mode);

retry_mode_set:
	switch (old_mode) {
	case M9MO_SYSINIT_MODE:
		cam_warn("sensor is initializing\n");
		err = -EBUSY;
		break;

	case M9MO_PARMSET_MODE:
		if (mode == M9MO_STILLCAP_MODE) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_SYS,
				M9MO_SYS_MODE, M9MO_MONITOR_MODE);
			if (err <= 0)
				break;
			for (i = M9MO_I2C_VERIFY; i; i--) {
				err = m9mo_readb(sd, M9MO_CATEGORY_SYS,
					M9MO_SYS_MODE, &val);
				if (val == M9MO_MONITOR_MODE)
					break;
				msleep(20);
			}
		}
	case M9MO_MONITOR_MODE:
	case M9MO_STILLCAP_MODE:
		err = m9mo_writeb(sd, M9MO_CATEGORY_SYS, M9MO_SYS_MODE, mode);
		break;

	default:
		cam_warn("current mode is unknown, %d\n", old_mode);
		err = 1;/* -EINVAL; */
	}

	if (err <= 0)
		return err;

	for (i = M9MO_I2C_VERIFY; i; i--) {
		err = m9mo_readb(sd, M9MO_CATEGORY_SYS, M9MO_SYS_MODE, &val);
		if (val == mode)
			break;
		msleep(20);
	}

	if (val != mode) {
		if (retry_mode_change) {
			retry_mode_change = 0;
			goto retry_mode_set;
		} else {
			cam_warn("ISP mode not change, %d -> %d\n", val, mode);
			return -ETIMEDOUT;
		}
	}

	state->isp_mode = mode;

	if (mode == M9MO_STILLCAP_MODE
		&& state->running_capture_mode != RUNNING_MODE_AE_BRACKET
		&& state->running_capture_mode != RUNNING_MODE_LOWLIGHT) {

		err = m9mo_readw(sd, M9MO_CATEGORY_SYS,
						M9MO_SYS_INT_EN, &int_en);
		CHECK_ERR(err);

		if (int_en & M9MO_INT_SOUND) {
			/* Clear Interrupt factor */
			int_factor = m9mo_wait_interrupt(sd,
					M9MO_SOUND_TIMEOUT);
			if (!(int_factor & M9MO_INT_SOUND)) {
				cam_warn("M9MO_INT_SOUND isn't issued, %#x\n",
						int_factor);
				return -ETIMEDOUT;
			}
		}
	}

	state->stream_on_part2 = true;

	cam_trace("X\n");
	return old_mode;
}

static int m9mo_set_mode_part2(struct v4l2_subdev *sd, u32 mode)
{
	u32 int_factor;
	struct m9mo_state *state = to_state(sd);

	if (state->running_capture_mode != RUNNING_MODE_SINGLE)
		return 0;

	if (state->stream_on_part2 == false)
		return 0;

	cam_trace("E, %d\n", mode);

	/* Dual Capture */
	if (state->dual_capture_start && mode == M9MO_STILLCAP_MODE)
		mode = M9MO_PARMSET_MODE;

	if (mode == M9MO_STILLCAP_MODE
		&& state->running_capture_mode != RUNNING_MODE_AE_BRACKET
		&& state->running_capture_mode != RUNNING_MODE_LOWLIGHT) {

		/* m9mo_wait_framesync(sd); */

		/* Clear Interrupt factor */
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_CAPTURE_TIMEOUT);
		if (!(int_factor & M9MO_INT_CAPTURE)) {
			cam_warn("M9MO_INT_CAPTURE isn't issued, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
	}

	state->stream_on_part2 = false;

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_cap_rec_end_mode(struct v4l2_subdev *sd, u32 mode)
{
	u32 int_factor, old_mode;

	cam_trace("E, %d\n", mode);

	/* not use stop recording cmd */
	if (mode == 100)
		return 0;

	old_mode = m9mo_set_mode(sd, M9MO_MONITOR_MODE);
	if (old_mode <= 0) {
		cam_err("failed to set mode\n");
		return old_mode;
	}

	if (old_mode != M9MO_MONITOR_MODE) {
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_MODE)) {
			cam_err("M9MO_INT_MODE isn't issued!!!\n");
			return -ETIMEDOUT;
		}
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_OIS_cap_mode(struct v4l2_subdev *sd)
{
	int err;
	int set_ois_cap_mode, read_ois_cap_mode;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E\n");

	err = m9mo_readb(sd, M9MO_CATEGORY_NEW,
		M9MO_NEW_OIS_CUR_MODE, &read_ois_cap_mode);
	CHECK_ERR(err);

	switch (state->running_capture_mode) {
	case RUNNING_MODE_CONTINUOUS:
	case RUNNING_MODE_BEST:
	case RUNNING_MODE_LOWLIGHT:
	case RUNNING_MODE_AE_BRACKET:
	case RUNNING_MODE_HDR:
	case RUNNING_MODE_BLINK:
	case RUNNING_MODE_BURST:
		set_ois_cap_mode = 0x05;
		break;

	case RUNNING_MODE_SINGLE:
	case RUNNING_MODE_WB_BRACKET:
	default:
		set_ois_cap_mode = 0x04;
		break;
	}

	if (state->recording) {
		if (state->fps <= 30)
			set_ois_cap_mode = 0x01;
		else
			set_ois_cap_mode = 0x02;
	} else if (state->mode == MODE_PANORAMA)
		set_ois_cap_mode = 0x03;

	if (set_ois_cap_mode != read_ois_cap_mode) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			M9MO_NEW_OIS_CUR_MODE, set_ois_cap_mode);
		CHECK_ERR(err);
	}

	cam_trace("X set mode : %d\n", set_ois_cap_mode);

	return 0;
}


static int m9mo_set_capture_mode(struct v4l2_subdev *sd, int val)
{
	int err, capture_val, framecount, raw_enable, int_en;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E capture_mode=%d\n", val);

	state->running_capture_mode = val;

	err = m9mo_readb(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_CAP_MODE, &capture_val);
	CHECK_ERR(err);

	switch (state->running_capture_mode) {
	case RUNNING_MODE_CONTINUOUS:
	case RUNNING_MODE_BEST:
		cam_dbg("m9mo_set_capture_mode() CONTINUOUS fps=%d\n",
					state->continueFps);

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
		M9MO_CAPCTRL_CAP_MODE, M9MO_CAP_MODE_MULTI_CAPTURE);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_CAP_FRM_INTERVAL,
					state->continueFps);
		CHECK_ERR(err); /* 0:7.5, 1:5, 2:3fps */

		framecount = 0x0A;
		if (state->running_capture_mode == RUNNING_MODE_BEST)
			framecount = 0x08;

		cam_dbg("m9mo_set_capture_mode() framecount=%d\n",
					framecount);

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_CAP_FRM_COUNT, framecount+1);
		CHECK_ERR(err);  /* frame count : A */

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL, 0x03, 0x01);
		CHECK_ERR(err);  /* Enable limited framerate*/

		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x18, 0x03);
		CHECK_ERR(err); /* OIS */

		/* AF proc  */
#if 0
		err = m9mo_writeb(sd, M9MO_CATEGORY_SYS, M9MO_SYS_INT_EN, 0x99);
		CHECK_ERR(err);
#endif
		err = m9mo_writeb(sd, M9MO_CATEGORY_SYS,
					M9MO_SYS_MODE, M9MO_STILLCAP_MODE);
		CHECK_ERR(err);

		cam_dbg("continue image Start ==========================\n");

		err = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(err & M9MO_INT_FRAME_SYNC)) {
			cam_err("m9mo_set_capture_mode() FRAME_SYNC error\n");
			return -ETIMEDOUT;
		}
		break;

	case RUNNING_MODE_AE_BRACKET:
	case RUNNING_MODE_LOWLIGHT:
		cam_trace("~~~~~~ AutoBracket AEB ~~~~~~\n");
		if (capture_val != M9MO_CAP_MODE_BRACKET_CAPTURE) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
					M9MO_CAPCTRL_CAP_MODE,
					M9MO_CAP_MODE_BRACKET_CAPTURE);
			CHECK_ERR(err);
		}

		if (state->running_capture_mode == RUNNING_MODE_LOWLIGHT) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_AUTO_BRACKET_EV, 0x0); /* EV 0.0 */
			err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EP_MODE_CAP, 0x05);
		}

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_CAP_FRM_INTERVAL, 0x00);
		CHECK_ERR(err); /* 0:7.5, 1:5, 2:3fps */
	break;

	case RUNNING_MODE_WB_BRACKET:
		cam_trace("~~~~~~ AutoBracket WBB ~~~~~~\n");
		if (capture_val != M9MO_CAP_MODE_SINGLE_CAPTURE) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
					M9MO_CAPCTRL_CAP_MODE,
					M9MO_CAP_MODE_SINGLE_CAPTURE);
			CHECK_ERR(err);
		}
	break;

	case RUNNING_MODE_HDR:
		cam_trace("~~~~~~ HDRmode capture ~~~~~~\n");
		if (capture_val != M9MO_CAP_MODE_BRACKET_CAPTURE) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
					M9MO_CAPCTRL_CAP_MODE,
					M9MO_CAP_MODE_BRACKET_CAPTURE);
			CHECK_ERR(err);
		}

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_CAP_FRM_INTERVAL, 0x00);
		CHECK_ERR(err); /* 0:7.5, 1:5, 2:3fps */
		break;

	case RUNNING_MODE_BLINK:
		cam_trace("~~~~~~ EyeBlink capture ~~~~~~\n");
		if (capture_val != M9MO_CAP_MODE_BLINK_CAPTURE) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
					M9MO_CAPCTRL_CAP_MODE,
					M9MO_CAP_MODE_BLINK_CAPTURE);
			CHECK_ERR(err);
		}
		break;

	case RUNNING_MODE_RAW:
		cam_trace("~~~~~~ raw capture ~~~~~~\n");
		err = m9mo_readb(sd, M9MO_CATEGORY_CAPPARM,
				0x78, &raw_enable);
		CHECK_ERR(err);

		/* if (capture_val != M9MO_CAP_MODE_RAW) always run */
		if (raw_enable != 0x01) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
					0x78, 0x01);
			CHECK_ERR(err);
		}
		break;

	case RUNNING_MODE_BURST:
		cam_trace("~~~~~~ burst capture mode ~~~~~~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_CAP_MODE, 0x0D);
		CHECK_ERR(err);
		state->mburst_start = false;
		break;

	case RUNNING_MODE_SINGLE:
	default:
		cam_trace("~~~~~~ Single capture ~~~~~~\n");
		if (capture_val != M9MO_CAP_MODE_SINGLE_CAPTURE) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
					M9MO_CAPCTRL_CAP_MODE,
					M9MO_CAP_MODE_SINGLE_CAPTURE);
			CHECK_ERR(err);
		}
		break;
	}

	/* set low light shot flag for ISP */
	if (state->running_capture_mode == RUNNING_MODE_LOWLIGHT) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x2E, 0x01);
		CHECK_ERR(err);
	} else {
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x2E, 0x00);
		CHECK_ERR(err);
	}

	err = m9mo_readw(sd, M9MO_CATEGORY_SYS,
		M9MO_SYS_INT_EN, &int_en);
	CHECK_ERR(err);

	if (state->running_capture_mode == RUNNING_MODE_LOWLIGHT
		|| state->running_capture_mode == RUNNING_MODE_AE_BRACKET
		||  state->running_capture_mode == RUNNING_MODE_HDR
		||  state->running_capture_mode == RUNNING_MODE_BLINK) {
		int_en &= ~M9MO_INT_FRAME_SYNC;
	} else {
		int_en |= M9MO_INT_FRAME_SYNC;
	}

	err = m9mo_writew(sd, M9MO_CATEGORY_SYS, M9MO_SYS_INT_EN, int_en);
	CHECK_ERR(err);

	m9mo_set_OIS_cap_mode(sd);

	cam_trace("X\n");
	return state->running_capture_mode;
}


/*
 * v4l2_subdev_core_ops
 */
static int m9mo_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(m9mo_ctrls); i++) {
		if (qc->id == m9mo_ctrls[i].id) {
			qc->maximum = m9mo_ctrls[i].maximum;
			qc->minimum = m9mo_ctrls[i].minimum;
			qc->step = m9mo_ctrls[i].step;
			qc->default_value = m9mo_ctrls[i].default_value;
			return 0;
		}
	}

	return -EINVAL;
}

static int m9mo_set_lock(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err ;
#if 0
	int status;
	int cnt = 100;
#endif

	cam_trace("%s\n", val ? "on" : "off");

#if 1
	if (val == 0) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AF_AE_LOCK, val);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AE_LOCK, val);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB, M9MO_AWB_LOCK, val);
		CHECK_ERR(err);
	}
#else
	err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AF_AE_LOCK, val);
	CHECK_ERR(err);

	/* check AE stability before AE,AWB lock */
	if (val == 1) {
		err = m9mo_readb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_STABILITY, &status);

		while (!status && cnt) {
			msleep(10);
			err = m9mo_readb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_STABILITY, &status);
			CHECK_ERR(err);
			cnt--;
		}
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AE_LOCK, val);
	CHECK_ERR(err);
	err = m9mo_writeb(sd, M9MO_CATEGORY_WB, M9MO_AWB_LOCK, val);
	CHECK_ERR(err);
#endif

	state->focus.lock = val;

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_CAF(struct v4l2_subdev *sd, int val)
{
	int err, range_status, af_range, zoom_status, mode_status;
	struct m9mo_state *state = to_state(sd);

	if (state->fps == 120) {
		cam_info("not supported on 120 fps !!!\n");
		return 0;
	}

	err = m9mo_readb2(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_ZOOM_STATUS, &zoom_status);
	CHECK_ERR(err);

	if (zoom_status == 1 && val == 1) {
		cam_info("zoom moving !!! val : %d\n", val);
		return 0;
	}

	state->caf_state = val;

	if (val == 1) {
		if (state->focus.status != 0x1000) {
			/* Set LOCK OFF */
			if (state->focus.lock && state->focus.status != 0x1000)
				m9mo_set_lock(sd, 0);

			/* Set mode to Continuous */
			err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
				M9MO_LENS_AF_MODE, &mode_status);

			if (mode_status != 1) {
				err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
					M9MO_LENS_AF_MODE, 0x01);
			CHECK_ERR(err);
			}

			err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
				M9MO_LENS_AF_SCAN_RANGE, &range_status);

			/* Set range to auto-macro */
				af_range = 0x02;

			if (range_status != af_range) {
				err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
					M9MO_LENS_AF_SCAN_RANGE, af_range);
				CHECK_ERR(err);
#if 0
				/* Set Zone REQ */
				err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
						M9MO_LENS_AF_INITIAL, 0x04);
				CHECK_ERR(err);
#endif
			}

			/* Start Continuous AF */
			err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
				M9MO_LENS_AF_START_STOP, 0x01);
			CHECK_ERR(err);
		}
	} else {
		/* Stop Continuous AF */
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_AF_START_STOP, 0x02);
		CHECK_ERR(err);
	}

	cam_trace("X val : %d %d\n", val, state->focus.mode);
	return 0;
}

static int m9mo_get_af_result(struct v4l2_subdev *sd,
		struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	int status, err;
	static int get_cnt;

	cam_trace("E, cnt: %d, status: 0x%x\n", get_cnt, state->focus.status);

	get_cnt++;

	err = m9mo_readw2(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_AF_RESULT, &status);
	CHECK_ERR(err);

	if ((status != 0x1000) && (status != 0x0)) {
		cam_trace("~~~ success !!!~~~\n");
		msleep(33);
		get_cnt = 0;
	} else if (status == 0x0) {
		cam_trace("~~~ fail !!!~~~\n");
		state->af_running = 0;
		msleep(33);
		get_cnt = 0;
	} else if (status == 0x1000) {
		cam_dbg("~~~ focusing !!!~~~\n");
		state->af_running = 0;
	}

	if (state->focus.mode == FOCUS_MODE_TOUCH
		&& state->focus.touch && status != 0x1000)
		m9mo_set_lock(sd, 0);

	if (state->focus.lock && !(state->focus.start) && status != 0x1000)
		m9mo_set_lock(sd, 0);

	state->focus.status = status;

	if (state->caf_state && !(state->focus.start) && status != 0x1000)
		m9mo_set_CAF(sd, 1);

	ctrl->value = state->focus.status;

	cam_trace("X, value 0x%04x\n", ctrl->value);

	return ctrl->value;
}

static int m9mo_get_scene_mode(struct v4l2_subdev *sd,
		struct v4l2_control *ctrl)
{
	int err;

	err = m9mo_readb2(sd, M9MO_CATEGORY_NEW,
			M9MO_NEW_DETECT_SCENE, &ctrl->value);

#if 0
	cam_trace("mode : %d\n", ctrl->value);
#endif

	return ctrl->value;
}

static int m9mo_get_scene_sub_mode(struct v4l2_subdev *sd,
		struct v4l2_control *ctrl)
{
	int err;

	err = m9mo_readb2(sd, M9MO_CATEGORY_NEW, 0x0C, &ctrl->value);

	return ctrl->value;
}

static int m9mo_get_zoom_level(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	int err;
	int zoom_level, zoom_status, zoom_lens_status;

	err = m9mo_readb2(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_ZOOM_LEVEL_INFO, &zoom_level);
	CHECK_ERR(err);

	err = m9mo_readb2(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_ZOOM_STATUS, &zoom_status);
	CHECK_ERR(err);

	err = m9mo_readb2(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_ZOOM_LENS_STATUS, &zoom_lens_status);
	CHECK_ERR(err);

	if (state->zoom <= 0xF && (zoom_level & 0xF) < 0xF)
		state->zoom = zoom_level & 0xF;
	ctrl->value = ((0x1 & zoom_status) << 4)
		| ((0x1 & zoom_lens_status) << 5)
		| (0xF & zoom_level);

	return 0;
}

static int m9mo_get_zoom_status(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	int err;
	int curr_zoom_info;

	err = m9mo_readb2(sd, M9MO_CATEGORY_PRO_MODE,
		M9MO_PRO_SMART_READ3, &curr_zoom_info);
	CHECK_ERR(err);

	if (state->zoom <= 0xF && (curr_zoom_info & 0xF) < 0xF)
		state->zoom = curr_zoom_info & 0xF;
	ctrl->value = curr_zoom_info & 0x7F;

	return 0;
}

static int m9mo_get_smart_read1(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	int value, err;

	err = m9mo_readl2(sd, M9MO_CATEGORY_PRO_MODE,
		M9MO_PRO_SMART_READ1, &value);
	CHECK_ERR(err);

	ctrl->value = value;

	return ctrl->value;
}

static int m9mo_get_smart_read2(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	int value, err;

	err = m9mo_readl2(sd, M9MO_CATEGORY_PRO_MODE,
		M9MO_PRO_SMART_READ2, &value);
	CHECK_ERR(err);

	ctrl->value = value;

	return ctrl->value;
}

static int m9mo_get_lens_status(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	int value, err;

	err = m9mo_readb2(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_LENS_STATUS, &value);
	CHECK_ERR(err);

	ctrl->value = value;

	return ctrl->value;
}

static int m9mo_get_flash_status(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	int err;
	int strobe_charge;
#if 0
	int strobe_up_down;
#endif

	err = m9mo_readb2(sd, M9MO_CATEGORY_CAPPARM,
		M9MO_CAPPARM_STROBE_CHARGE, &strobe_charge);
	CHECK_ERR(err);

	ctrl->value = strobe_charge;
#if 0
	err = m9mo_readb2(sd, M9MO_CATEGORY_CAPPARM,
		M9MO_CAPPARM_STROBE_UP_DOWN, &strobe_up_down);
	CHECK_ERR(err);

	strobe_charge &= 0xFF;
	strobe_up_down &= 0xFF;

	ctrl->value = strobe_charge | (strobe_up_down << 8);

	cam_trace(": strobe_charge %d  up_down %d\n",
		strobe_charge, strobe_up_down);
#endif

	return ctrl->value;
}

static int m9mo_get_object_tracking(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	int err;
#if 0
	int ot_ready, cnt = 30;
#endif

	err = m9mo_readb2(sd, M9MO_CATEGORY_OT,
		M9MO_OT_TRACKING_STATUS, &state->ot_status);
	CHECK_ERR(err);

#if 0
	if (state->ot_status != OBJECT_TRACKING_STATUS_SUCCESS)
		return 0;

	err = m9mo_readb2(sd, M9MO_CATEGORY_OT,
		M9MO_OT_INFO_READY, &ot_ready);
	CHECK_ERR(err);
	while (ot_ready && cnt) {
		msleep(20);
		err = m9mo_readb(sd, M9MO_CATEGORY_OT,
			M9MO_OT_INFO_READY, &ot_ready);
		CHECK_ERR(err);
		cnt--;
	}

	err = m9mo_readw(sd, M9MO_CATEGORY_OT,
		M9MO_OT_TRACKING_X_LOCATION,
		&state->ot_x_loc);
	CHECK_ERR(err);
	err = m9mo_readw(sd, M9MO_CATEGORY_OT,
		M9MO_OT_TRACKING_Y_LOCATION,
		&state->ot_y_loc);
	CHECK_ERR(err);
	err = m9mo_readw(sd, M9MO_CATEGORY_OT,
		M9MO_OT_TRACKING_FRAME_WIDTH,
		&state->ot_width);
	CHECK_ERR(err);
	err = m9mo_readw(sd, M9MO_CATEGORY_OT,
		M9MO_OT_TRACKING_FRAME_HEIGHT,
		&state->ot_height);
	CHECK_ERR(err);
	cam_dbg("OT pos x: %d, y: %d, w: %d, h: %d\n",
		state->ot_x_loc, state->ot_y_loc,
		state->ot_width, state->ot_height);

	cam_trace("X status : %d\n", state->ot_status);
#endif
	return 0;
}

static int m9mo_get_warning_condition(struct v4l2_subdev *sd,
		struct v4l2_control *ctrl)
{
	int value, err;

	err = m9mo_readw2(sd, M9MO_CATEGORY_PRO_MODE,
		0x03, &value);
	CHECK_ERR(err);

	ctrl->value = value;

	return ctrl->value;
}

static int m9mo_get_av(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	int value, err;

	err = m9mo_readl2(sd, M9MO_CATEGORY_AE,
		M9MO_AE_NOW_AV, &value);
	CHECK_ERR(err);

	ctrl->value = value;
	state->AV = value;

	return ctrl->value;
}

static int m9mo_get_tv(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	int value, err;

	err = m9mo_readl2(sd, M9MO_CATEGORY_AE,
		M9MO_AE_NOW_TV, &value);
	CHECK_ERR(err);

	ctrl->value = value;
	state->TV = value;

	return ctrl->value;
}

static int m9mo_get_sv(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	int value, err;

	err = m9mo_readl2(sd, M9MO_CATEGORY_AE,
		M9MO_AE_NOW_SV, &value);
	CHECK_ERR(err);

	ctrl->value = value;
	state->SV = ctrl->value;

	return ctrl->value;
}

static int m9mo_get_ev(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);

	state->EV = state->AV + state->TV;

	return state->EV;
}

static int m9mo_get_lv(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	int value, err;

	err = m9mo_readb2(sd, M9MO_CATEGORY_AE,
		M9MO_AE_NOW_LV, &value);
	CHECK_ERR(err);

	ctrl->value = value;
	state->LV = ctrl->value;

	return ctrl->value;
}


static int m9mo_get_WBcustomX(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	int value, value2, err, int_factor, int_en;
	int changed_capture_mode = false;

	if (state->running_capture_mode != RUNNING_MODE_SINGLE) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_CAP_MODE,
				M9MO_CAP_MODE_SINGLE_CAPTURE);
		CHECK_ERR(err);
		changed_capture_mode = true;
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
		M9MO_WB_AWB_MODE, 0x02);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
		M9MO_WB_AWB_MANUAL, 0x08);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
		M9MO_WB_CWB_MODE, 0x02);
	CHECK_ERR(err);

	msleep(100);

	err = m9mo_writeb(sd, M9MO_CATEGORY_SYS,
		M9MO_SYS_MODE, M9MO_STILLCAP_MODE);
	CHECK_ERR(err);

	err = m9mo_readw(sd, M9MO_CATEGORY_SYS,
				M9MO_SYS_INT_EN, &int_en);
	CHECK_ERR(err);

	if (int_en & M9MO_INT_SOUND) {
		/* Clear Interrupt factor */
		int_factor = m9mo_wait_interrupt(sd, M9MO_SOUND_TIMEOUT);
		if (!(int_factor & M9MO_INT_SOUND)) {
			cam_warn("M9MO_INT_SOUND isn't issued, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
	}

	int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
	if (!(int_factor & M9MO_INT_CAPTURE)) {
		cam_warn("M9MO_INT_CAPTURE isn't issued, %#x\n",
				int_factor);
		return -ETIMEDOUT;
	}

	err = m9mo_set_mode(sd, M9MO_MONITOR_MODE);
	if (err <= 0) {
		cam_err("failed to set mode\n");
		return err;
	}

	err = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
	if (!(err & M9MO_INT_MODE)) {
		cam_err("firmware was erased?\n");
		return -ETIMEDOUT;
	}

	err = m9mo_readw(sd, M9MO_CATEGORY_WB,
		M9MO_WB_GET_CUSTOM_RG, &value);
	CHECK_ERR(err);

	err = m9mo_readw(sd, M9MO_CATEGORY_WB,
		M9MO_WB_GET_CUSTOM_BG, &value2);
	CHECK_ERR(err);

	/* prevent abnormal value, to be fixed by ISP */
	if (value == 0)
		value = 424;
	if (value2 == 0)
		value2 = 452;

	state->wb_custom_rg = value;
	state->wb_custom_bg = value2;

	err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
		M9MO_WB_CWB_MODE, 0x02);
	CHECK_ERR(err);

	err = m9mo_writew(sd, M9MO_CATEGORY_WB,
		M9MO_WB_SET_CUSTOM_RG, state->wb_custom_rg);
	CHECK_ERR(err);
	err = m9mo_writew(sd, M9MO_CATEGORY_WB,
		M9MO_WB_SET_CUSTOM_BG, state->wb_custom_bg);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
		M9MO_WB_CWB_MODE, 0x01);
	CHECK_ERR(err);

	if (changed_capture_mode)
		m9mo_set_capture_mode(sd, state->running_capture_mode);

	cam_trace("X value : %d value2 : %d\n", value, value2);

	return value;
}

static int m9mo_get_WBcustomY(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);

	return state->wb_custom_bg;
}

static int m9mo_get_face_detect_number(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	int value, err;

	err = m9mo_readb2(sd, M9MO_CATEGORY_FD, 0x0A, &value);
	CHECK_ERR(err);

	state->fd_num = value;

#if 0
	cam_trace("X %d\n", value);
#endif

	return value;
}

static int m9mo_get_factory_FW_info(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	int err = 0;
	u32 read_val1, read_val2;
	u32 ver = 0;

	m9mo_readb(sd, M9MO_CATEGORY_SYS,
			0x02, &read_val1);
	CHECK_ERR(err);

	m9mo_readb(sd, M9MO_CATEGORY_SYS,
		0x03, &read_val2);
	CHECK_ERR(err);

	ver = 0;
	ver = (read_val1 << 8) | (read_val2);

	cam_trace("m9mo_get_factory_FW : 0x%x\n", ver);
	ctrl->value = ver;

	return 0;
}

static int m9mo_get_factory_OIS_info(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	int err;
	u32 ver = 0;

	err = m9mo_readl(sd, M9MO_CATEGORY_NEW,
			0x1B, &ver);
	CHECK_ERR(err);

	cam_trace("m9mo_get_factory_FW : 0x%x\n", ver);
	ctrl->value = ver;

	return 0;
}

static int m9mo_get_factory_flash_charge(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	int err, val;

	cam_trace("E\n");

	err = m9mo_readb(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_STROBE_CHARGE, &val);
	CHECK_ERR(err);

	cam_trace("X : %d\n", val);

	return val;
}

static int m9mo_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	int err = 0;
	u32 val = 0;

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_AUTO_FOCUS_RESULT:
		m9mo_get_af_result(sd, ctrl);
		break;

	case V4L2_CID_CAM_JPEG_MEMSIZE:
		ctrl->value = M9MO_JPEG_MAXSIZE +
			M9MO_THUMB_MAXSIZE + M9MO_POST_MAXSIZE;
		break;

	case V4L2_CID_CAM_JPEG_MAIN_SIZE:
		ctrl->value = state->jpeg.main_size;
		break;

	case V4L2_CID_CAM_JPEG_MAIN_OFFSET:
		ctrl->value = state->jpeg.main_offset;
		break;

	case V4L2_CID_CAM_JPEG_THUMB_SIZE:
		ctrl->value = state->jpeg.thumb_size;
		break;

	case V4L2_CID_CAM_JPEG_THUMB_OFFSET:
		ctrl->value = state->jpeg.thumb_offset;
		break;

	case V4L2_CID_CAM_JPEG_POSTVIEW_OFFSET:
		ctrl->value = state->jpeg.postview_offset;
		break;

	case V4L2_CID_CAMERA_EXIF_FLASH:
		ctrl->value = state->exif.flash;
		break;

	case V4L2_CID_CAMERA_EXIF_ISO:
		ctrl->value = state->exif.iso;
		break;

	case V4L2_CID_CAMERA_EXIF_TV:
		ctrl->value = state->exif.tv;
		break;

	case V4L2_CID_CAMERA_EXIF_BV:
		ctrl->value = state->exif.bv;
		break;

	case V4L2_CID_CAMERA_EXIF_AV:
		ctrl->value = state->exif.av;
		break;

	case V4L2_CID_CAMERA_EXIF_EBV:
		ctrl->value = state->exif.ebv;
		break;

	case V4L2_CID_CAMERA_EXIF_FL:
		ctrl->value = state->exif.focal_length;
		break;

	case V4L2_CID_CAMERA_EXIF_FL_35mm:
		ctrl->value = state->exif.focal_35mm_length;
		break;

	case V4L2_CID_CAMERA_FD_EYE_BLINK_RESULT:
		ctrl->value = state->fd_eyeblink_cap;
		break;

	case V4L2_CID_CAMERA_RED_EYE_FIX_RESULT:
		ctrl->value = state->fd_red_eye_status;
		break;

	case V4L2_CID_CAMERA_SCENE_MODE:
		err = m9mo_get_scene_mode(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_SCENE_SUB_MODE:
		err = m9mo_get_scene_sub_mode(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_FACTORY_DOWN_RESULT:
		ctrl->value = state->factory_down_check;
		break;

	case V4L2_CID_CAMERA_FACTORY_AF_INT_RESULT:
		ctrl->value = state->factory_result_check;
		break;

	case V4L2_CID_CAMERA_FACTORY_END_RESULT:
		ctrl->value = state->factory_end_check;
		if (0 != ctrl->value) {
			cam_trace("leesm test ----- factory_end_check %d\n",
				ctrl->value);
		}
		break;

	case V4L2_CID_CAMERA_ZOOM:
		err = m9mo_get_zoom_status(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_OPTICAL_ZOOM_CTRL:
		err = m9mo_get_zoom_level(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_FLASH_MODE:
		err = m9mo_get_flash_status(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_OBJ_TRACKING_STATUS:
		err = m9mo_get_object_tracking(sd, ctrl);
		ctrl->value = state->ot_status;
		break;

	case V4L2_CID_CAMERA_AV:
		ctrl->value = m9mo_get_av(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_TV:
		ctrl->value = m9mo_get_tv(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_SV:
		ctrl->value = m9mo_get_sv(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_EV:
		ctrl->value = m9mo_get_ev(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_LV:
		ctrl->value = m9mo_get_lv(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_WB_CUSTOM_X:
		ctrl->value = m9mo_get_WBcustomX(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_WB_CUSTOM_Y:
		ctrl->value = m9mo_get_WBcustomY(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_GET_MODE:
		err = m9mo_readb(sd, M9MO_CATEGORY_SYS,
					M9MO_SYS_MODE, &val);
		if (err < 0)
			ctrl->value = -1;
		else
			ctrl->value = val;
		break;

	case V4L2_CID_CAMERA_SMART_READ1:
		m9mo_get_smart_read1(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_SMART_READ2:
		m9mo_get_smart_read2(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_LENS_STATUS:
		m9mo_get_lens_status(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_WARNING_CONDITION:
		ctrl->value = m9mo_get_warning_condition(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_FACTORY_ISP_FW_CHECK:
		err = m9mo_get_factory_FW_info(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_FACTORY_OIS_VER_CHECK:
		err = m9mo_get_factory_OIS_info(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_FACE_DETECT_NUMBER:
		ctrl->value = m9mo_get_face_detect_number(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_FACTORY_FLASH_CHARGE:
		ctrl->value = m9mo_get_factory_flash_charge(sd, ctrl);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FW_CHECKSUM_VAL:
		ctrl->value = state->fw_checksum_val;
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

static int m9mo_set_antibanding(struct v4l2_subdev *sd,
		struct v4l2_control *ctrl)
{
	struct v4l2_queryctrl qc = {0,};
	struct m9mo_state *state = to_state(sd);
	int val = ctrl->value, err;
	u32 antibanding[] = {0x00, 0x01, 0x02, 0x03, 0x04};

	if (state->anti_banding == val)
		return 0;

	cam_dbg("E, value %d\n", val);

	qc.id = ctrl->id;
	m9mo_queryctrl(sd, &qc);

	if (val < qc.minimum || val > qc.maximum) {
		cam_warn("invalied value, %d\n", val);
		val = qc.default_value;
	}

	val -= qc.minimum;

	/* Auto flickering is always used */
	err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
					M9MO_AE_FLICKER, antibanding[val]);
	CHECK_ERR(err);

	state->anti_banding = val;

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_lens_off(struct v4l2_subdev *sd)
{
	struct m9mo_state *state = to_state(sd);
#if 0
	u32 int_factor = 0;
#endif
	int err = 0;
	int value;
	int cnt = 3,  cnt2 = 500;

	cam_trace("E\n");

	if (unlikely(state->isp.bad_fw)) {
		cam_err("\"Unknown\" state, please update F/W");
		return -ENOSYS;
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
		0x01, 0x00);
	CHECK_ERR(err);

#if 1 /* use polling method instead of ISR check */
	msleep(200);

	err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
		0x28, &value);
	CHECK_ERR(err);

	while (value != 4 && cnt) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x01, 0x00);
		CHECK_ERR(err);

		msleep(200);

		err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
			0x28, &value);
		CHECK_ERR(err);

		if (value == 4)
			break;

		cnt--;
	}

	while (value == 4 && cnt2) {
		msleep(20);
		err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
			0x28, &value);
		CHECK_ERR(err);

		if (value != 4 && value == 0)
			break;

		cnt2--;
	}

	if (value != 0)
		return -1;
#else
	int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);

	if (!(int_factor & M9MO_INT_LENS_INIT)) {
		cam_err("M9MO_INT_LENS_INIT isn't issued, %#x\n",
				int_factor);
		return -ETIMEDOUT;
	}
#endif
	cam_trace("X\n");
	return err;
}

static int m9mo_dump_fw(struct v4l2_subdev *sd)
{
	struct file *fp;
	mm_segment_t old_fs;
	u8 *buf/*, val*/;
	u32 addr, unit, count, intram_unit = 0x1000;
	int i, /*j,*/ err;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(M9MO_FW_DUMP_PATH,
		O_WRONLY|O_CREAT|O_TRUNC, S_IRUGO|S_IWUGO|S_IXUSR);
	if (IS_ERR(fp)) {
		cam_err("failed to open %s, err %ld\n",
			M9MO_FW_DUMP_PATH, PTR_ERR(fp));
		err = -ENOENT;
		goto file_out;
	}

	buf = kmalloc(intram_unit, GFP_KERNEL);
	if (!buf) {
		cam_err("failed to allocate memory\n");
		err = -ENOMEM;
		goto out;
	}

	cam_dbg("start, file path %s\n", M9MO_FW_DUMP_PATH);


/*
	val = 0x7E;
	err = m9mo_mem_write(sd, 0x04, sizeof(val), 0x50000308, &val);
	if (err < 0) {
		cam_err("failed to write memory\n");
		goto out;
	}
*/


	err = m9mo_mem_write(sd, 0x04, SZ_64,
				0x90001200 , buf_port_seting0);
			CHECK_ERR(err);
			mdelay(10);

	err = m9mo_mem_write(sd, 0x04, SZ_64,
				0x90001000 , buf_port_seting1);
			CHECK_ERR(err);
			mdelay(10);

	err = m9mo_mem_write(sd, 0x04, SZ_64,
				0x90001100 , buf_port_seting2);
			CHECK_ERR(err);
			mdelay(10);

	err = m9mo_writel(sd, M9MO_CATEGORY_FLASH,
				0x1C, 0x0247036D);

	err = m9mo_writeb(sd, M9MO_CATEGORY_FLASH,
				0x57, 01);

	CHECK_ERR(err);


	addr = M9MO_FLASH_READ_BASE_ADDR;
	unit = SZ_4K;
	count = 1024;
	for (i = 0; i < count; i++) {

			err = m9mo_mem_dump(sd,
				unit, addr + (i * unit), buf);
			cam_err("dump ~~ %d\n", i);
			if (err < 0) {
				cam_err("i2c falied, err %d\n", err);
				goto out;
			}
			vfs_write(fp, buf, unit, &fp->f_pos);
	}
/*
	addr = M9MO_FLASH_BASE_ADDR + SZ_64K * count;
	unit = SZ_8K;
	count = 4;
	for (i = 0; i < count; i++) {
		for (j = 0; j < unit; j += intram_unit) {
			err = m9mo_mem_read(sd,
				intram_unit, addr + (i * unit) + j, buf);
			if (err < 0) {
				cam_err("i2c falied, err %d\n", err);
				goto out;
			}
			vfs_write(fp, buf, intram_unit, &fp->f_pos);
		}
	}
*/
	cam_dbg("end\n");

out:
	kfree(buf);
	if (!IS_ERR(fp))
		filp_close(fp, current->files);
file_out:
	set_fs(old_fs);

	return err;
}

static int m9mo_get_sensor_fw_version(struct v4l2_subdev *sd)
{
	struct m9mo_state *state = to_state(sd);
	int err = 0;
	int fw_ver = 0x00;
	int awb_ver = 0x00;
	int af_ver = 0x00;
	int ois_ver = 0x00;
	int parm_ver = 0x00;
	int user_ver_temp;
	char user_ver[20];
	char sensor_ver[7];
	int i = 0;

	cam_err("E\n");

	/* read F/W version */
	err = m9mo_readw(sd, M9MO_CATEGORY_SYS,
		M9MO_SYS_VER_FW, &fw_ver);
	CHECK_ERR(err);

	err = m9mo_readw(sd, M9MO_CATEGORY_SYS,
		M9MO_SYS_VER_AWB, &awb_ver);
	CHECK_ERR(err);

	err = m9mo_readw(sd, M9MO_CATEGORY_SYS,
		M9MO_SYS_VER_PARAM, &parm_ver);
	CHECK_ERR(err);

	err = m9mo_readl(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_AF_VERSION, &af_ver);
	CHECK_ERR(err);

	err = m9mo_readl(sd, M9MO_CATEGORY_NEW,
		M9MO_NEW_OIS_VERSION, &ois_ver);
	CHECK_ERR(err);


	for (i = 0; i < M9MO_FW_VER_LEN; i++) {
		err = m9mo_readb(sd, M9MO_CATEGORY_SYS,
				M9MO_SYS_USER_VER, &user_ver_temp);
		CHECK_ERR(err);

		if ((char)user_ver_temp == '\0')
			break;

		user_ver[i] = (char)user_ver_temp;
		/*cam_info("user temp version = %c\n", (char)user_ver_temp);*/

	}

	user_ver[i] = '\0';

	if (user_ver[0] == 'F' && user_ver[1] == 'C') {
		for (i = 0; i < M9MO_FW_VER_LEN; i++) {
			if (user_ver[i] == 0x20) {
				sensor_ver[i] = '\0';
				break;
			}
			sensor_ver[i] = user_ver[i];
		}
	} else {
		sprintf(sensor_ver, "%s", "Invalid version");
	}

	cam_info("f/w version = %x\n", fw_ver);
	cam_info("awb version = %x\n", awb_ver);
	cam_info("af version = %x\n", af_ver);
	cam_info("ois version = %x\n", ois_ver);
	cam_info("parm version = %x\n", parm_ver);
	cam_info("user version = %s\n", user_ver);
	cam_info("sensor version = %s\n", sensor_ver);

	sprintf(state->sensor_ver, "%s", sensor_ver);
	sprintf(state->sensor_type, "%d %d %d %x",
			awb_ver, af_ver, ois_ver, parm_ver);
	memcpy(sysfs_sensor_fw, state->sensor_ver,
			sizeof(state->sensor_ver));
	memcpy(sysfs_sensor_type, state->sensor_type,
			sizeof(state->sensor_type));
	state->isp_fw_ver = fw_ver;

	cam_info("sensor fw : %s\n", sysfs_sensor_fw);
	cam_info("sensor type : %s\n", sysfs_sensor_type);
	return 0;
}

static int m9mo_get_phone_fw_version(struct v4l2_subdev *sd)
{
	struct m9mo_state *state = to_state(sd);
	struct device *dev = sd->v4l2_dev->dev;
	const struct firmware *fw;
	int err = 0;

	struct file *fp;
	mm_segment_t old_fs;
	long nread;
	int fw_requested = 1;
	char ver_tmp[20];
	char phone_ver[7];
	int i = 0;

	cam_info("E\n");

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(M9MO_FW_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		cam_trace("failed to open %s, err %ld\n", M9MO_FW_PATH,
			  PTR_ERR(fp));
		if (PTR_ERR(fp) == -4) {
			cam_err("%s: file open I/O is interrupted\n", __func__);
			return -EIO;
		}
		goto request_fw;
	} else {
		cam_info("FW File(phone) opened.\n");
	}

	fw_requested = 0;

	err = vfs_llseek(fp, M9MO_FW_VER_NUM, SEEK_SET);
	if (err < 0) {
		cam_warn("failed to fseek, %d\n", err);
		goto out;
	}

	/*nread = vfs_read(fp, (char __user *)state->phone_ver,*/
	nread = vfs_read(fp, (char __user *)ver_tmp,
			M9MO_FW_VER_LEN, &fp->f_pos);
	if (nread != M9MO_FW_VER_LEN) {
		cam_err("failed to read firmware file, %ld Bytes\n", nread);
		err = -EIO;
		goto out;
	}

request_fw:
	if (fw_requested) {
		set_fs(old_fs);

#if 0
		m9mo_get_sensor_fw_version(sd, sensor_ver);

		if (sensor_ver[0] == 'T' && sensor_ver[1] == 'B') {
			err = request_firmware(&fw, M9MOTB_FW_PATH, dev);
#if defined(CONFIG_MACH_Q1_BD)
		} else if (sensor_ver[0] == 'O' && sensor_ver[1] == 'O') {
			err = request_firmware(&fw, M9MOOO_FW_PATH, dev);
#endif
#if defined(CONFIG_MACH_U1_KOR_LGT)
		} else if (sensor_ver[0] == 'S' && sensor_ver[1] == 'B') {
			err = request_firmware(&fw, M9MOSB_FW_PATH, dev);
#endif
		} else {
			cam_warn("cannot find the matched F/W file\n");
#if defined(CONFIG_MACH_Q1_BD)
			err = request_firmware(&fw, M9MOOO_FW_PATH, dev);
#elif defined(CONFIG_MACH_U1_KOR_LGT)
			err = request_firmware(&fw, M9MOSB_FW_PATH, dev);
#else
			err = request_firmware(&fw, M9MOTB_FW_PATH, dev);
#endif
		}
#else
		if (system_rev > 1) {
			cam_info("Firmware Path = %s\n",
					M9MO_EVT31_FW_REQ_PATH);
			err = request_firmware(&fw,
					M9MO_EVT31_FW_REQ_PATH, dev);
		} else {
			cam_info("Firmware Path = %s\n", M9MO_FW_REQ_PATH);
			err = request_firmware(&fw, M9MO_FW_REQ_PATH, dev);
		}
#endif

		if (err != 0) {
			cam_err("request_firmware falied\n");
			err = -EINVAL;
			goto out;
		}
#if 0
		sprintf(state->phone_ver, "%x%x",
				(u32)&fw->data[M9MO_FW_VER_NUM],
				(u32)&fw->data[M9MO_FW_VER_NUM + 1]);
		cam_info("%s: fw->data[0] = %x, fw->data[1] = %x\n", __func__,
				(int)fw->data[M9MO_FW_VER_NUM],
				(int)fw->data[M9MO_FW_VER_NUM + 1]);
		ver_tmp = (int)fw->data[M9MO_FW_VER_NUM] * 16 * 16;
		ver_tmp += (int)fw->data[M9MO_FW_VER_NUM + 1];
		cam_info("ver_tmp = %x\n", ver_tmp);
		sprintf(state->phone_ver, "FU%x", ver_tmp);
#endif

		for (i = 0; i < M9MO_FW_VER_LEN; i++) {
			if ((int)fw->data[M9MO_FW_VER_NUM+i] == 0x00)
				break;

			ver_tmp[i] = (int)fw->data[M9MO_FW_VER_NUM+i];
		}
	}
out:

	ver_tmp[M9MO_FW_VER_LEN-1] = '\0';

	for (i = 0; i < M9MO_FW_VER_LEN; i++) {
		if (ver_tmp[i] == 0x20) {
			phone_ver[i] = '\0';
			/*cam_info("phone_ver = %s\n", phone_ver);*/
			break;
		}
		phone_ver[i] = ver_tmp[i];
	}

	cam_info("ver_tmp = %s\n", ver_tmp);
	cam_info("phone_ver = %s\n", phone_ver);
	sprintf(state->phone_ver, "%s", phone_ver);
	memcpy(sysfs_phone_fw, state->phone_ver,
				sizeof(state->phone_ver));

	if (!fw_requested) {
		filp_close(fp, current->files);
		set_fs(old_fs);
	} else {
		release_firmware(fw);
	}

	cam_dbg("phone ver : %s\n", sysfs_phone_fw);
	return 0;
}

static int m9mo_check_checksum(struct v4l2_subdev *sd)
{
	struct m9mo_state *state = to_state(sd);
	int checksum_value, value, err, init_value;
	int cnt = 100;

	cam_trace("E\n");

	err = m9mo_readl(sd, M9MO_CATEGORY_FLASH,
			0x00, &init_value);
	CHECK_ERR(err);

	err = m9mo_writel(sd, M9MO_CATEGORY_FLASH,
			0x00, 0x00);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_FLASH,
			0x09, 0x04);
	CHECK_ERR(err);

	err = m9mo_readb(sd, M9MO_CATEGORY_FLASH,
			0x09, &value);
	CHECK_ERR(err);

	while (value == 4 && cnt) {
		msleep(100);
		err = m9mo_readb(sd, M9MO_CATEGORY_FLASH,
				0x09, &value);
		CHECK_ERR(err);

		if (value == 0)
			break;

		cnt--;
	}

	err = m9mo_readw(sd, M9MO_CATEGORY_FLASH,
			0x0A, &checksum_value);
	CHECK_ERR(err);

	cam_trace("X %d\n", checksum_value);

	if (checksum_value == 0x0) {
		state->fw_checksum_val = 1;
		return 1;
	} else {
		state->fw_checksum_val = 0;
		return 0;
	}
}

static int m9mo_check_fw(struct v4l2_subdev *sd)
{
#if 0
	struct m9mo_state *state = to_state(sd);
#endif
	int /*af_cal_h = 0,*/ af_cal_l = 0;
	int rg_cal_h = 0, rg_cal_l = 0;
	int bg_cal_h = 0, bg_cal_l = 0;
#if 0
	int update_count = 0;
#endif
	u32 int_factor;
	int err = 0;

	cam_trace("E\n");

	/* F/W version */
	err = m9mo_get_phone_fw_version(sd);
	if (err == -EIO)
		return err;

#if 0
	if (state->isp.bad_fw)
		goto out;
#endif

	m9mo_get_sensor_fw_version(sd);

	goto out;

	err = m9mo_writeb(sd, M9MO_CATEGORY_FLASH, M9MO_FLASH_CAM_START, 0x01);
	CHECK_ERR(err);

	int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
	if (!(int_factor & M9MO_INT_MODE)) {
		cam_err("firmware was erased?\n");
		return -ETIMEDOUT;
	}

	err = m9mo_readb(sd, M9MO_CATEGORY_LENS, M9MO_LENS_AF_CAL, &af_cal_l);
	CHECK_ERR(err);

	err = m9mo_readb(sd, M9MO_CATEGORY_ADJST,
			M9MO_ADJST_AWB_RG_H, &rg_cal_h);
	CHECK_ERR(err);
	err = m9mo_readb(sd, M9MO_CATEGORY_ADJST,
			M9MO_ADJST_AWB_RG_L, &rg_cal_l);
	CHECK_ERR(err);

	err = m9mo_readb(sd, M9MO_CATEGORY_ADJST,
			M9MO_ADJST_AWB_BG_H, &bg_cal_h);
	CHECK_ERR(err);
	err = m9mo_readb(sd, M9MO_CATEGORY_ADJST,
			M9MO_ADJST_AWB_BG_L, &bg_cal_l);
	CHECK_ERR(err);

out:
#if 0
	if (!state->sensor_type) {
		state->sensor_type = kzalloc(50, GFP_KERNEL);
		if (!state->sensor_type) {
			cam_err("no memory for F/W version\n");
			return -ENOMEM;
		}
	}
#endif

#if 0
	sprintf(state->sensor_type, "%s %s %d %x %x %x %x %x %x",
		sensor_ver, phone_ver, update_count,
		af_cal_h, af_cal_l, rg_cal_h, rg_cal_l, bg_cal_h, bg_cal_l);
#endif
	cam_info("phone ver = %s, sensor_ver = %s\n",
			sysfs_phone_fw, sysfs_sensor_fw);

	cam_trace("X\n");
	return err;
}


static int m9mo_make_CSV_rawdata(struct v4l2_subdev *sd,
	u32 *address, bool bAddResult)
{
	struct file *fp;
	mm_segment_t old_fs;
	u8 *buf;
	u32 addr, unit, intram_unit = 0x1000;
	int err;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(M9MO_FACTORY_CSV_PATH,
		O_WRONLY|O_CREAT|O_TRUNC, S_IRUGO|S_IWUGO|S_IXUSR);
	if (IS_ERR(fp)) {
		cam_err("failed to open %s, err %ld\n",
			M9MO_FACTORY_CSV_PATH, PTR_ERR(fp));
		err = -ENOENT;
		goto file_out;
	}

	buf = kmalloc(intram_unit, GFP_KERNEL);
	if (!buf) {
		cam_err("failed to allocate memory\n");
		err = -ENOMEM;
		goto out;
	}

	cam_dbg("start, file path %s\n", M9MO_FACTORY_CSV_PATH);

	addr = address[0];
	unit = address[1]-address[0]+1;

	cam_trace("m9mo_make_CSV_rawdata() addr[0x%0x] size=%d\n",
		addr, unit);

	err = m9mo_mem_read(sd, unit, addr, buf);
	if (err < 0) {
		cam_err("i2c falied, err %d\n", err);
		goto out;
	}

/*"Result27E03128(0:OK, 1:NG)"Bit 0 : IRIS
Bit 1 : Liveview Gain
Bit 2 : ShutterClose
Bit 3 : CaptureGain
Bit 5 : DefectPixel*/
	if (bAddResult) {
		m9mo_mem_read(sd, 0x2,
			M9MO_FLASH_FACTORY_RESULT, buf+unit);
		cam_trace("m9mo_make_CSV_rawdata() size=%d  result=%x\n",
			unit, *(u16 *)(buf+unit));
		unit += 2;
	}

	vfs_write(fp, buf, unit, &fp->f_pos);
	msleep(20);

out:
	kfree(buf);
	if (!IS_ERR(fp))
		filp_close(fp, current->files);
file_out:
	set_fs(old_fs);

	return err;
}

static int m9mo_make_CSV_rawdata_direct(struct v4l2_subdev *sd, int nkind)
{
	struct file *fp;
	mm_segment_t old_fs;
	u8 *buf;
	int val;
	u32 unit_default, unit_movie;
	u32 intram_unit = 0x1000;
	int i, err, start, end;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(M9MO_FACTORY_CSV_PATH,
		O_WRONLY|O_CREAT|O_TRUNC, S_IRUGO|S_IWUGO|S_IXUSR);
	if (IS_ERR(fp)) {
		cam_err("failed to open %s, err %ld\n",
			M9MO_FACTORY_CSV_PATH, PTR_ERR(fp));
		err = -ENOENT;
		goto file_out;
	}

	buf = kmalloc(intram_unit, GFP_KERNEL);
	if (!buf) {
		cam_err("failed to allocate memory\n");
		err = -ENOMEM;
		goto out;
	}

	if (V4L2_CID_CAMERA_FACTORY_DEFECTPIXEL) {
		cam_dbg("start, file path %s\n", M9MO_FACTORY_CSV_PATH);

		start = 0x69;
		end = 0x8C;
		unit_default = end - start + 1;

		for (i = start; i <= end; i++) {
			err = m9mo_readb(sd, M9MO_CATEGORY_MON, i, &val);
			CHECK_ERR(err);

			buf[i-start] = (u8)val;
		}

		start = 0xA0;
		end = 0xA5;
		unit_movie = end - start + 1;

		for (i = start; i <= end; i++) {
			err = m9mo_readb(sd, M9MO_CATEGORY_MON, i, &val);
			CHECK_ERR(err);

			buf[unit_default + (i - start)] = (u8)val;
		}
	}
	vfs_write(fp, buf, (unit_default + unit_movie), &fp->f_pos);

out:
	kfree(buf);

	if (!IS_ERR(fp))
		filp_close(fp, current->files);

file_out:
	set_fs(old_fs);

	return err;
}

#ifdef FAST_CAPTURE
static int m9mo_set_fast_capture(struct v4l2_subdev *sd)
{
	struct m9mo_state *state = to_state(sd);
	int err;
	cam_info("E\n");
	if (state->running_capture_mode == RUNNING_MODE_SINGLE) {
		err = m9mo_set_mode_part1(sd, M9MO_STILLCAP_MODE);
		if (err <= 0) {
			cam_err("Mode change is failed to STILLCAP for fast capture\n");
			return err;
		} else {
			cam_info("Fast capture is issued. mode change start.\n");
		}

		state->fast_capture_set = 1;
	}
	return 0;
}
#endif

static int m9mo_set_sensor_mode(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
#if 0
	int err;
	int set_shutter_mode;
#endif
	cam_dbg("E, value %d\n", val);

#if 0
	err = m9mo_set_mode(sd, M9MO_PARMSET_MODE);
	if (err <= 0) {
		cam_err("failed to set mode\n");
		return err;
	}

	if (val == SENSOR_MOVIE)
		set_shutter_mode = 0;  /* Rolling Shutter */
	else
		set_shutter_mode = 1;  /* Mechanical Shutter */
	err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
		M9MO_ADJST_SHUTTER_MODE, set_shutter_mode);
	CHECK_ERR(err);
#endif

	state->sensor_mode = val;

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_flash_evc_step(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E, value %d\n", val);

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_STROBE_EVC, val);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_flash(struct v4l2_subdev *sd, int val, int force)
{
	struct m9mo_state *state = to_state(sd);
	int strobe_en = 0;
	int err;
	cam_trace("E, value %d\n", val);

	if (!force)
		state->flash_mode = val;

retry:
	switch (val) {
	case FLASH_MODE_OFF:
		strobe_en = 0;
		break;

	case FLASH_MODE_AUTO:
		strobe_en = 0x02;
		break;

	case FLASH_MODE_ON:
		strobe_en = 0x01;
		break;

	case FLASH_MODE_RED_EYE:
		strobe_en = 0x12;
		break;

	case FLASH_MODE_FILL_IN:
		strobe_en = 0x01;
		break;

	case FLASH_MODE_SLOW_SYNC:
		strobe_en = 0x03;
		break;

	case FLASH_MODE_RED_EYE_FIX:
		strobe_en = 0x02;
		err = m9mo_writeb(sd, M9MO_CATEGORY_FD,
				M9MO_FD_RED_EYE, 0x01);
		CHECK_ERR(err);
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = FLASH_MODE_OFF;
		goto retry;
	}

	state->strobe_en = strobe_en;

	if (val !=  FLASH_MODE_RED_EYE_FIX) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_FD,
				M9MO_FD_RED_EYE, 0x00);
		CHECK_ERR(err);
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_STROBE_EN, strobe_en);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_flash_batt_info(struct v4l2_subdev *sd, int val)
{
	int err;
	int set_strobe_batt;

	cam_trace("E, value %d\n", val);

	if (val)
		set_strobe_batt = 1;
	else
		set_strobe_batt = 0;

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_STROBE_BATT_INFO, set_strobe_batt);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_iso(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	struct v4l2_queryctrl qc = {0,};
	int val = ctrl->value, err, current_state;
	u32 iso[] = {0x00, 0x01, 0x64, 0xC8, 0x190, 0x320, 0x640, 0xC80};

	if (state->scene_mode != SCENE_MODE_NONE) {
		/* sensor will set internally */
		return 0;
	}

	cam_dbg("E, value %d\n", val);

	qc.id = ctrl->id;
	m9mo_queryctrl(sd, &qc);

	if (val < qc.minimum || val > qc.maximum) {
		cam_warn("invalied value, %d\n", val);
		val = qc.default_value;
	}

	val -= qc.minimum;

	switch (val) {
	case 0:
		state->iso = 0;
		break;

	case 1:
		state->iso = 50;
		break;

	case 2:
		state->iso = 100;
		break;

	case 3:
		state->iso = 200;
		break;

	case 4:
		state->iso = 400;
		break;

	case 5:
		state->iso = 800;
		break;

	case 6:
		state->iso = 1600;
		break;

	case 7:
		state->iso = 3200;
		break;

	default:
		break;
	}

	err = m9mo_readb(sd, M9MO_CATEGORY_AE,
		M9MO_AE_EV_PRG_MODE_CAP, &current_state);

	/* ISO AUTO */
	if (val == 0) {
		switch (state->mode) {
		case MODE_PROGRAM:
			err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EV_PRG_MODE_CAP, 0x00);
			CHECK_ERR(err);
			break;

		case MODE_A:
			err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EV_PRG_MODE_CAP, 0x01);
			CHECK_ERR(err);
			break;

		case MODE_S:
			err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EV_PRG_MODE_CAP, 0x02);
			CHECK_ERR(err);
			break;

		default:
			break;
		}
	} else {
		switch (state->mode) {
		case MODE_PROGRAM:
			if (current_state != 0x04) {
				err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
					M9MO_AE_EV_PRG_MODE_CAP, 0x04);
				CHECK_ERR(err);
			}
			break;

		case MODE_A:
			if (current_state != 0x05) {
				err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
					M9MO_AE_EV_PRG_MODE_CAP, 0x05);
				CHECK_ERR(err);
			}
			break;

		case MODE_S:
			if (current_state != 0x06) {
				err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
					M9MO_AE_EV_PRG_MODE_CAP, 0x06);
				CHECK_ERR(err);
			}
			break;

		default:
			break;
		}
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_ISO_VALUE, iso[val]);
		CHECK_ERR(err);
	}
	cam_trace("X\n");
	return 0;
}

static int m9mo_set_metering(struct v4l2_subdev *sd, int val)
{
	int err;
	cam_dbg("E, value %d\n", val);

retry:
	switch (val) {
	case METERING_CENTER:
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AE_MODE, 0x03);
		CHECK_ERR(err);
		break;
	case METERING_SPOT:
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AE_MODE, 0x05);
		CHECK_ERR(err);
		break;
	case METERING_MATRIX:
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AE_MODE, 0x01);
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

static int m9mo_set_exposure(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	struct v4l2_queryctrl qc = {0,};
	int val = ctrl->value, err;
	/*
	   -2.0, -1.7, -1.3, -1.0 -0.7 -0.3
	   0
	   +0.3 +0.7 +1.0 +1.3 +1.7 +2.0
	*/
	u32 exposure[] = {0x0A, 0x0D, 0x11, 0x14, 0x17, 0x1B,
		0x1E,
		0x21, 0x25, 0x28, 0x2B, 0x2F, 0x32};
	cam_dbg("E, value %d\n", val);

	qc.id = ctrl->id;
	m9mo_queryctrl(sd, &qc);

	if (val < qc.minimum || val > qc.maximum) {
		cam_warn("invalied value, %d\n", val);
		val = qc.default_value;
	}

	val -= qc.minimum;

	err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
		M9MO_AE_INDEX, exposure[val]);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_whitebalance(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);

	int err;
	cam_dbg("E, value %d\n", val);

retry:
	switch (val) {
	case WHITE_BALANCE_AUTO:
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_CWB_MODE, 0x00);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MODE, 0x01);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MANUAL, 0x01);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_SUNNY:
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_CWB_MODE, 0x00);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MANUAL, 0x04);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_CLOUDY:
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_CWB_MODE, 0x00);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MANUAL, 0x05);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_TUNGSTEN:
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_CWB_MODE, 0x00);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MANUAL, 0x01);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_FLUORESCENT:
	case WHITE_BALANCE_FLUORESCENT_H:
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_CWB_MODE, 0x00);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MANUAL, 0x02);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_FLUORESCENT_L:
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_CWB_MODE, 0x00);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MANUAL, 0x03);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_K:
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_CWB_MODE, 0x00);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MANUAL, 0x0A);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_INCANDESCENT:
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_CWB_MODE, 0x00);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MANUAL, 0x01);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_PROHIBITION:
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_CWB_MODE, 0x00);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MANUAL, 0x00);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_HORIZON:
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_CWB_MODE, 0x00);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MANUAL, 0x07);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_LEDLIGHT:
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_CWB_MODE, 0x00);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MANUAL, 0x09);
		CHECK_ERR(err);
		break;

	case WHITE_BALANCE_CUSTOM:
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MODE, 0x02);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_AWB_MANUAL, 0x08);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_CWB_MODE, 0x02);
		CHECK_ERR(err);

		err = m9mo_writew(sd, M9MO_CATEGORY_WB,
			M9MO_WB_SET_CUSTOM_RG, state->wb_custom_rg);
		CHECK_ERR(err);

		err = m9mo_writew(sd, M9MO_CATEGORY_WB,
			M9MO_WB_SET_CUSTOM_BG, state->wb_custom_bg);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
			M9MO_WB_CWB_MODE, 0x01);
		CHECK_ERR(err);
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = WHITE_BALANCE_AUTO;
		goto retry;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_sharpness(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	struct v4l2_queryctrl qc = {0,};
	int val = ctrl->value, err;
	u32 sharpness[] = {0x01, 0x02, 0x03, 0x04, 0x05};
	cam_dbg("E, value %d\n", val);

	qc.id = ctrl->id;
	m9mo_queryctrl(sd, &qc);

	if (val < qc.minimum || val > qc.maximum) {
		cam_warn("invalied value, %d\n", val);
		val = qc.default_value;
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
		M9MO_MON_EDGE_CTRL, sharpness[val]);
	CHECK_ERR(err);

	state->sharpness = sharpness[val];

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_contrast(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct v4l2_queryctrl qc = {0,};
	int val = ctrl->value, err;
	u32 contrast[] = {0x01, 0x02, 0x03, 0x04, 0x05};
	cam_dbg("E, value %d\n", val);

	qc.id = ctrl->id;
	m9mo_queryctrl(sd, &qc);

	if (val < qc.minimum || val > qc.maximum) {
		cam_warn("invalied value, %d\n", val);
		val = qc.default_value;
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
		M9MO_MON_TONE_CTRL, contrast[val]);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_saturation(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	struct v4l2_queryctrl qc = {0,};
	int val = ctrl->value, err;
	u32 saturation[] = {0x01, 0x02, 0x03, 0x04, 0x05};
	cam_dbg("E, value %d\n", val);

	qc.id = ctrl->id;
	m9mo_queryctrl(sd, &qc);

	if (val < qc.minimum || val > qc.maximum) {
		cam_warn("invalied value, %d\n", val);
		val = qc.default_value;
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
		M9MO_MON_CHROMA_LVL, saturation[val]);
	CHECK_ERR(err);

	state->saturation = saturation[val];

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_scene_mode(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	struct v4l2_control ctrl;
	int evp, sharpness, saturation;
	int err;
	cam_dbg("E, value %d\n", val);

	sharpness = SHARPNESS_DEFAULT;
	saturation = CONTRAST_DEFAULT;

retry:
	switch (val) {
	case SCENE_MODE_NONE:
		evp = 0x00;
		break;

	case SCENE_MODE_PORTRAIT:
		evp = 0x01;
		sharpness = SHARPNESS_MINUS_1;
		break;

	case SCENE_MODE_LANDSCAPE:
		evp = 0x02;
		sharpness = SHARPNESS_PLUS_1;
		saturation = SATURATION_PLUS_1;
		break;

	case SCENE_MODE_SPORTS:
		evp = 0x03;
		break;

	case SCENE_MODE_PARTY_INDOOR:
		evp = 0x04;
		saturation = SATURATION_PLUS_1;
		break;

	case SCENE_MODE_BEACH_SNOW:
		evp = 0x05;
		saturation = SATURATION_PLUS_1;
		break;

	case SCENE_MODE_SUNSET:
		evp = 0x06;
		break;

	case SCENE_MODE_DUSK_DAWN:
		evp = 0x07;
		break;

	case SCENE_MODE_FALL_COLOR:
		evp = 0x08;
		saturation = SATURATION_PLUS_2;
		break;

	case SCENE_MODE_NIGHTSHOT:
		evp = 0x09;
		break;

	case SCENE_MODE_BACK_LIGHT:
		evp = 0x0A;
		break;

	case SCENE_MODE_FIREWORKS:
		evp = 0x0B;
		break;

	case SCENE_MODE_TEXT:
		evp = 0x0C;
		sharpness = SHARPNESS_PLUS_2;
		break;

	case SCENE_MODE_CANDLE_LIGHT:
		evp = 0x0D;
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = SCENE_MODE_NONE;
		goto retry;
	}

	/* EV-P */
	err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AE_EP_MODE_MON, evp);
	CHECK_ERR(err);
	err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AE_EP_MODE_CAP, evp);
	CHECK_ERR(err);

	/* Chroma Saturation */
	ctrl.id = V4L2_CID_CAMERA_SATURATION;
	ctrl.value = saturation;
	m9mo_set_saturation(sd, &ctrl);

	/* Sharpness */
	ctrl.id = V4L2_CID_CAMERA_SHARPNESS;
	ctrl.value = sharpness;
	m9mo_set_sharpness(sd, &ctrl);

	/* Emotional Color */
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
		M9MO_CAPPARM_MCC_MODE, val == SCENE_MODE_NONE ? 0x01 : 0x00);
	CHECK_ERR(err);

	state->scene_mode = val;

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_effect_color(struct v4l2_subdev *sd, int val)
{
	int cb = 0, cr = 0;
	int err;

	switch (val) {
	case IMAGE_EFFECT_SEPIA:
		cb = 0xD8;
		cr = 0x18;
		break;

	case IMAGE_EFFECT_BNW:
		cb = 0x00;
		cr = 0x00;
		break;

	case IMAGE_EFFECT_ANTIQUE:
		cb = 0xD0;
		cr = 0x30;
		break;

	default:
		return 0;
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_MON, M9MO_MON_COLOR_EFFECT, 0x01);
	CHECK_ERR(err);
	err = m9mo_writeb(sd, M9MO_CATEGORY_MON, M9MO_MON_CFIXB, cb);
	CHECK_ERR(err);
	err = m9mo_writeb(sd, M9MO_CATEGORY_MON, M9MO_MON_CFIXR, cr);
	CHECK_ERR(err);

	return 0;
}

static int m9mo_set_effect_point(struct v4l2_subdev *sd, int val)
{
	int point = 0;
	int err;

	switch (val) {
	case IMAGE_EFFECT_POINT_BLUE:
		point = 0;
		break;

	case IMAGE_EFFECT_POINT_RED:
		point = 1;
		break;

	case IMAGE_EFFECT_POINT_YELLOW:
		point = 2;
		break;

	default:
		return 0;
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
		M9MO_MON_COLOR_EFFECT, 0x03);
	err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
		M9MO_MON_POINT_COLOR, point);
	CHECK_ERR(err);

	return 0;
}

static int m9mo_set_effect(struct v4l2_subdev *sd, int val)
{
	int set_effect = 0;
	int err;
	struct m9mo_state *state = to_state(sd);
	cam_dbg("E, value %d\n", val);

retry:
	switch (val) {
	case IMAGE_EFFECT_NONE:
		set_effect = 0;
		break;

	case IMAGE_EFFECT_NEGATIVE:
		set_effect = 2;
		break;

	case IMAGE_EFFECT_BNW:
	case IMAGE_EFFECT_SEPIA:
	case IMAGE_EFFECT_ANTIQUE:
		err = m9mo_set_effect_color(sd, val);
		CHECK_ERR(err);
		set_effect = 1;
		break;

	case IMAGE_EFFECT_POINT_BLUE:
	case IMAGE_EFFECT_POINT_RED:
	case IMAGE_EFFECT_POINT_YELLOW:
		err = m9mo_set_effect_point(sd, val);
		CHECK_ERR(err);
		set_effect = 3;
		break;

	case IMAGE_EFFECT_VINTAGE_WARM:
		set_effect = 4;
		break;

	case IMAGE_EFFECT_VINTAGE_COLD:
		set_effect = 5;
		break;

	case IMAGE_EFFECT_WASHED:
		set_effect = 6;
		break;

	default:
		cam_warn("invalid value, %d\n", val);
		val = IMAGE_EFFECT_NONE;
		goto retry;
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
		M9MO_MON_COLOR_EFFECT, set_effect);
	CHECK_ERR(err);

	state->color_effect = set_effect;

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_wdr(struct v4l2_subdev *sd, int val)
{
	int wdr, err;

	cam_dbg("%s\n", val ? "on" : "off");

	wdr = (val == 1 ? 0x01 : 0x00);

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_WDR_EN, wdr);
		CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_antishake(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int ahs, err;

	if (state->scene_mode != SCENE_MODE_NONE) {
		cam_warn("Should not be set with scene mode");
		return 0;
	}

	cam_dbg("%s\n", val ? "on" : "off");

	ahs = (val == 1 ? 0x0E : 0x00);

	err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AE_EP_MODE_MON, ahs);
		CHECK_ERR(err);
	err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AE_EP_MODE_CAP, ahs);
		CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_face_beauty(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err;

	cam_dbg("%s\n", val ? "on" : "off");

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
		M9MO_CAPPARM_AFB_CAP_EN, val ? 0x01 : 0x00);
	CHECK_ERR(err);

	state->face_beauty = val;

	cam_trace("X\n");
	return 0;
}

static unsigned int m9mo_set_cal_rect_pos(struct v4l2_subdev *sd,
	unsigned int pos_val)
{
	struct m9mo_state *state = to_state(sd);
	unsigned int set_val;

	if (pos_val <= 40)
		set_val = 40;
	else if (pos_val > (state->preview->width - 40))
		set_val = state->preview->width - 40;
	else
		set_val = pos_val;

	return set_val;
}

static int m9mo_set_object_tracking(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err = 0;
	unsigned int set_x, set_y;

	err = m9mo_writeb(sd, M9MO_CATEGORY_OT,
		M9MO_OT_TRACKING_CTL, 0x10);
	CHECK_ERR(err);

	cam_trace("E val : %d\n", val);

	if (val == OT_START) {
		set_x = m9mo_set_cal_rect_pos(sd, state->focus.pos_x);
		set_y = m9mo_set_cal_rect_pos(sd, state->focus.pos_y);

		cam_dbg("idx[%d] w[%d] h[%d]", state->preview->index,
			state->preview->width, state->preview->height);
		cam_dbg("pos_x[%d] pos_y[%d] x[%d] y[%d]",
			state->focus.pos_x, state->focus.pos_y,
			set_x, set_y);

		err = m9mo_writeb(sd, M9MO_CATEGORY_OT,
			M9MO_OT_FRAME_WIDTH, 0x02);
		CHECK_ERR(err);
		err = m9mo_writew(sd, M9MO_CATEGORY_OT,
			M9MO_OT_X_START_LOCATION,
			set_x - 40);
		CHECK_ERR(err);
		err = m9mo_writew(sd, M9MO_CATEGORY_OT,
			M9MO_OT_Y_START_LOCATION,
			set_y - 40);
		CHECK_ERR(err);
		err = m9mo_writew(sd, M9MO_CATEGORY_OT,
			M9MO_OT_X_END_LOCATION,
			set_x + 40);
		CHECK_ERR(err);
		err = m9mo_writew(sd, M9MO_CATEGORY_OT,
			M9MO_OT_Y_END_LOCATION,
			set_y + 40);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_OT,
			M9MO_OT_TRACKING_CTL, 0x11);
		CHECK_ERR(err);
	}

	return 0;
}

static int m9mo_set_image_stabilizer_OIS(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err, int_factor, set_ois, int_en;
	int wait_int_ois = 0;

	if (state->image_stabilizer_mode != V4L2_IMAGE_STABILIZER_OIS
		|| state->mode == MODE_PANORAMA)
		return 0;

	cam_trace("E: mode %d\n", val);

retry:
	switch (val) {
	case V4L2_IS_OIS_NONE:
		cam_warn("OIS_NONE and OIS End");
		return 0;

	case V4L2_IS_OIS_MOVIE:
		set_ois = 0x01;
		wait_int_ois = 1;
		break;

	case V4L2_IS_OIS_STILL:
		set_ois = 0x02;
		wait_int_ois = 0;
		break;

#if 0
	case V4L2_IS_OIS_MULTI:
		set_ois = 0x03;
		wait_int_ois = 0;
		break;

	case V4L2_IS_OIS_VSS:
		set_ois = 0x04;
		wait_int_ois = 1;
		break;
#endif

	default:
		cam_warn("invalid value, %d", val);
		val = V4L2_IS_OIS_STILL;
		goto retry;
	}

	/* set movie mode when waterfall */
	if (state->mode == MODE_WATERFALL) {
		set_ois = 0x01;
		wait_int_ois = 1;
	}

	if (wait_int_ois) {
		err = m9mo_readw(sd, M9MO_CATEGORY_SYS,
			M9MO_SYS_INT_EN, &int_en);
		CHECK_ERR(err);

		/* enable OIS_SET interrupt */
		int_en |= M9MO_INT_OIS_SET;

		err = m9mo_writew(sd, M9MO_CATEGORY_SYS,
			M9MO_SYS_INT_EN, int_en);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x18, set_ois);
		CHECK_ERR(err);

		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_OIS_SET)) {
			cam_err("M9MO_INT_OIS_SET isn't issued, %#x\n",
				int_factor);
			return -ETIMEDOUT;
		}

		/* enable OIS_SET interrupt */
		int_en &= ~M9MO_INT_OIS_SET;

		err = m9mo_writew(sd, M9MO_CATEGORY_SYS,
			M9MO_SYS_INT_EN, int_en);
		CHECK_ERR(err);
	} else {
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x18, set_ois);
		CHECK_ERR(err);
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_af_sensor_mode(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	u32 cancel;
	int err;
	int af_mode, af_window, af_range;
	int range_status, mode_status, window_status;

	cancel = val & FOCUS_MODE_DEFAULT;
	val &= 0xFF;
	af_range = state->focus_range;

	cam_dbg("E, value %d\n", val);

retry:
	switch (val) {
	case FOCUS_MODE_AUTO:
		af_mode = 0x00;
		af_window = state->focus_area_mode;
		break;

	case FOCUS_MODE_MULTI:
		af_mode = 0x00;
		af_window = 0x01;
		break;

	case FOCUS_MODE_CONTINOUS:
		af_mode = 0x01;
		af_range = 0x02;
		af_window = 0x00;
		break;

	case FOCUS_MODE_FACEDETECT:
		af_mode = 0x00;
		af_window = 0x02;
		break;

	case FOCUS_MODE_TOUCH:
		af_mode = 0x00;
		af_window = 0x02;
		break;

	case FOCUS_MODE_MACRO:
		af_mode = 0x00;
		af_range = 0x01;
		af_window = state->focus_area_mode;
		break;

	case FOCUS_MODE_MANUAL:
		af_mode = 0x02;
		af_window = state->focus_area_mode;
		af_range = 0x02;
		cancel = 0;
		break;

	case FOCUS_MODE_OBJECT_TRACKING:
		af_mode = 0x00;
		af_window = 0x02;
		break;

	default:
		cam_warn("invalid value, %d", val);
		val = FOCUS_MODE_AUTO;
		goto retry;
	}

	if (cancel && state->focus.lock)
		m9mo_set_lock(sd, 0);

	state->focus.mode = val;

	/* Set AF Mode */
	err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_AF_MODE, &mode_status);

	if (mode_status != af_mode) {
		if (state->focus.mode != FOCUS_MODE_TOUCH) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
				M9MO_LENS_AF_MODE, af_mode);
			CHECK_ERR(err);
		}
	}

	/* fix range to auto-macro when SMART AUTO mode */
	if (state->mode == MODE_SMART_AUTO)
		af_range = 0x02;

	/* fix range to auto-macro when MOVIE mode */
	if (state->mode == MODE_VIDEO)
		af_range = 0x02;

	/* fix range to macro when CLOSE_UP mode */
	if (state->mode == MODE_CLOSE_UP)
		af_range = 0x01;

	/* fix window to center */
	if ((state->focus.mode == 0 || state->focus.mode == 1)
		&&  state->focus_area_mode == 2)
		af_window = 0x00;

	/* Set AF Scan Range */
	err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_AF_SCAN_RANGE, &range_status);

	if (range_status != af_range) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_AF_SCAN_RANGE, af_range);
		CHECK_ERR(err);
	}
#if 0
	/* Set Zone REQ */
	if (range_status != af_range) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_AF_INITIAL, 0x04);
		CHECK_ERR(err);
	}
#endif
	/* Set AF Window Mode */
	err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_AF_WINDOW_MODE, &window_status);

	if (window_status != af_window) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_AF_WINDOW_MODE, af_window);
		CHECK_ERR(err);
	}
	cam_trace("X\n");
	return 0;
}

static int m9mo_set_af(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	const struct m9mo_platform_data *pdata = client->dev.platform_data;
	int err = 0;

	cam_info("%s, mode %d\n", val ? "start" : "stop", state->focus.mode);

	state->focus.start = val;

	if (val == 1) {
		/* AF LED regulator on */
		pdata->af_led_power(1);

		if (state->facedetect_mode == FACE_DETECTION_NORMAL
			&& state->mode == MODE_SMART_AUTO) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_FD,
				M9MO_FD_CTL, 0x11);
			CHECK_ERR(err);
		}

		m9mo_set_af_sensor_mode(sd, state->focus.mode);

		if (state->focus.mode != FOCUS_MODE_CONTINOUS) {
			m9mo_set_lock(sd, 1);
#if 0
			/* Single AF Start */
			err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
					M9MO_LENS_AF_START_STOP, 0x00);
			CHECK_ERR(err);
#else
			/* AF start */
			err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x5C, 0x10);
			CHECK_ERR(err);
			state->af_running = 1;
#endif
		}
	} else {
		if (state->facedetect_mode == FACE_DETECTION_NORMAL
			&& state->mode == MODE_SMART_AUTO) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_FD,
				M9MO_FD_CTL, 0x01);
			CHECK_ERR(err);
		}

		if (state->focus.lock && state->focus.status != 0x1000
			&& !state->af_running)
			m9mo_set_lock(sd, 0);

		/* AF LED regulator off */
		pdata->af_led_power(0);
	}

	cam_dbg("X\n");
	return err;
}

static int m9mo_set_af_mode(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err, af_mode, mode_status;

	if (val == FOCUS_MODE_CONTINOUS)
		af_mode = 0x01;
	else
		af_mode = 0x00;

	/* Set AF Mode */
	err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_AF_MODE, &mode_status);

	if (mode_status != af_mode) {
		if (state->focus.mode != FOCUS_MODE_TOUCH) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
				M9MO_LENS_AF_MODE, af_mode);
			CHECK_ERR(err);
		}
	}

	state->focus.mode = val;

	cam_trace("X val : %d\n", val);
	return 0;
}

static int m9mo_set_focus_range(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err, range_status;

	if (state->mode == MODE_SMART_AUTO || state->mode == MODE_VIDEO) {
		cam_trace("don't set !!!\n");
		return 0;
	}

	/* Set AF Scan Range */
	err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_AF_SCAN_RANGE, &range_status);

	if (range_status != val) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_AF_SCAN_RANGE, val);
		CHECK_ERR(err);
#if 0
		/* Set Zone REQ */
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_AF_INITIAL, 0x04);
		CHECK_ERR(err);
#endif
	}

	state->focus_range = val;

	cam_trace("X val : %d\n", val);
	return 0;
}

static int m9mo_set_focus_area_mode(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err, window_status;

	/* Set AF Window Mode */
	err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_AF_WINDOW_MODE, &window_status);

	if (window_status != val) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_AF_WINDOW_MODE, val);
		CHECK_ERR(err);
	}

	state->focus_area_mode = val;

	cam_trace("X val : %d\n", val);
	return 0;
}

static int m9mo_set_touch_auto_focus(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err = 0;
	cam_info("%s\n", val ? "start" : "stop");

	state->focus.touch = val;

	if (val) {
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
				M9MO_LENS_AF_TOUCH_POSX, state->focus.pos_x);
		CHECK_ERR(err);
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
				M9MO_LENS_AF_TOUCH_POSY, state->focus.pos_y);
		CHECK_ERR(err);

		if (state->facedetect_mode == FACE_DETECTION_BLINK) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_FD,
				M9MO_FD_CTL, 0x01);
			CHECK_ERR(err);
		}
	} else {
		if (state->facedetect_mode == FACE_DETECTION_BLINK) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_FD,
				M9MO_FD_CTL, 0x11);
			CHECK_ERR(err);
		}
	}

	cam_trace("X\n");
	return err;
}

static int m9mo_set_AF_LED(struct v4l2_subdev *sd, int val)
{
	int err;
	int set_AF_LED_On;

	cam_trace("E, value %d\n", val);

	if (val)
		set_AF_LED_On = 1;
	else
		set_AF_LED_On = 0;

	err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_AF_LED, set_AF_LED_On);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_timer_Mode(struct v4l2_subdev *sd, int val)
{
	int err;
	int set_OIS_timer;

	cam_trace("E for OIS, value %d\n", val);

	if (val == 0)
		set_OIS_timer = 0;
	else
		set_OIS_timer = 1;

	err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
		M9MO_NEW_OIS_TIMER, set_OIS_timer);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_timer_LED(struct v4l2_subdev *sd, int val)
{
	int err;
	int set_timer_LED_On;

	cam_trace("E, value %d\n", val);

	switch (val) {
	case V4L2_TIMER_LED_OFF:
		set_timer_LED_On = 0;
		break;

	case V4L2_TIMER_LED_2_SEC:
		set_timer_LED_On = 0x1;
		break;

	case V4L2_TIMER_LED_5_SEC:
		set_timer_LED_On = 0x2;
		break;

	case V4L2_TIMER_LED_10_SEC:
		set_timer_LED_On = 0x3;
		break;

	default:
		cam_warn("invalid value, %d", val);
		return 0;
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_TIMER_LED, set_timer_LED_On);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_zoom(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	struct v4l2_queryctrl qc = {0,};
	int val = ctrl->value, err;
	int opti_val, digi_val;
	int opti_max = 15;
	int optical_zoom_val[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
		11, 12, 13, 14, 15};
	int zoom_val[] = { 0x01,
		0x0E, 0x17, 0x1D, 0x22, 0x26,
		0x29, 0x2C, 0x2E, 0x30, 0x32,
		0x34, 0x35, 0x36, 0x37, 0x38 };
	cam_trace("E, value %d\n", val);

	qc.id = ctrl->id;
	m9mo_queryctrl(sd, &qc);

	if (val < qc.minimum) {
		cam_warn("invalied min value, %d\n", val);
		val = qc.default_value;
	}

	if (val > qc.maximum) {
		cam_warn("invalied max value, %d\n", val);
		val = qc.maximum;
	}

	if (val <= opti_max) {
		opti_val = val;
		digi_val = 0;
	} else {
		opti_val = opti_max;
		digi_val = val - opti_max;
	}

	if (state->recording) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_ZOOM_SPEED, 0x00);
		CHECK_ERR(err);
	} else {
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_ZOOM_SPEED, 0x01);
		CHECK_ERR(err);
	}

	/* AF CANCEL */
	err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_AF_START_STOP, 0x05);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_AF_ZOOM_LEVEL, optical_zoom_val[opti_val]);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
		M9MO_MON_ZOOM, zoom_val[digi_val]);
	CHECK_ERR(err);

	state->zoom = val;

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_zoom_ctrl(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);

	int err, curr_zoom_info;
	int zoom_ctrl, zoom_speed;
	int read_ctrl, read_speed;

	cam_trace("E, value %d\n", val);

	switch (val) {
	case V4L2_OPTICAL_ZOOM_TELE_START:
		zoom_ctrl = 0;
		zoom_speed = 1;
		break;

	case V4L2_OPTICAL_ZOOM_WIDE_START:
		zoom_ctrl = 1;
		zoom_speed = 1;
		break;

	case V4L2_OPTICAL_ZOOM_SLOW_TELE_START:
		zoom_ctrl = 0;
		zoom_speed = 0;
		break;

	case V4L2_OPTICAL_ZOOM_SLOW_WIDE_START:
		zoom_ctrl = 1;
		zoom_speed = 0;
		break;

	case V4L2_OPTICAL_ZOOM_STOP:
		zoom_ctrl = 2;
		zoom_speed = 0x0F;
		break;

	default:
		cam_warn("invalid value, %d", val);
		return 0;
	}

	if (state->recording)
		zoom_speed = 0;

	err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_ZOOM_SPEED, &read_speed);
	CHECK_ERR(err);

	err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
		M9MO_LENS_AF_ZOOM_CTRL, &read_ctrl);
	CHECK_ERR(err);

	if (read_speed != zoom_speed && val != V4L2_OPTICAL_ZOOM_STOP) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_ZOOM_SPEED, zoom_speed);
		CHECK_ERR(err);
	}

	err = m9mo_readb2(sd, M9MO_CATEGORY_PRO_MODE,
		M9MO_PRO_SMART_READ3, &curr_zoom_info);
	CHECK_ERR(err);

	if ((read_ctrl != zoom_ctrl) || (curr_zoom_info & 0x40)) {
		if (val != V4L2_OPTICAL_ZOOM_STOP) {
			/* AF CANCEL */
			err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
				M9MO_LENS_AF_START_STOP, 0x05);
			CHECK_ERR(err);
		}

		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_AF_ZOOM_CTRL, zoom_ctrl);
		CHECK_ERR(err);
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_smart_zoom(struct v4l2_subdev *sd, int val)
{
	int err;
	int smart_zoom;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E, value %d\n", val);

	if (val)
		smart_zoom = 0x5B;
	else
		smart_zoom = 0;

	/* Off:0x00, On: 0x01 ~ 0x5B */
	err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
		M9MO_MON_HR_ZOOM, smart_zoom);
	CHECK_ERR(err);

	state->smart_zoom_mode = val;

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_jpeg_quality(struct v4l2_subdev *sd,
	struct v4l2_control *ctrl)
{
	struct v4l2_queryctrl qc = {0,};
	int val = ctrl->value, ratio, err;
	cam_dbg("E, value %d\n", val);

	qc.id = ctrl->id;
	m9mo_queryctrl(sd, &qc);

	if (val < qc.minimum || val > qc.maximum) {
		cam_warn("invalied value, %d\n", val);
		val = qc.default_value;
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
		M9MO_CAPPARM_JPEG_RATIO, 0x62);
	CHECK_ERR(err);

	/* m9mo */
	if (val <= 65)		/* Normal */
		ratio = 0x14;
	else if (val <= 75)	/* Fine */
		ratio = 0x09;
	else			/* Superfine */
		ratio = 0x02;

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
		M9MO_CAPPARM_JPEG_RATIO_OFS, ratio);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_get_exif(struct v4l2_subdev *sd)
{
	struct m9mo_state *state = to_state(sd);
#if 0 /* legacy */
	/* standard values */
	u16 iso_std_values[] = { 10, 12, 16, 20, 25, 32, 40, 50, 64, 80,
		100, 125, 160, 200, 250, 320, 400, 500, 640, 800,
		1000, 1250, 1600, 2000, 2500, 3200, 4000, 5000, 6400, 8000};
	/* quantization table */
	u16 iso_qtable[] = { 11, 14, 17, 22, 28, 35, 44, 56, 71, 89,
		112, 141, 178, 224, 282, 356, 449, 565, 712, 890,
		1122, 1414, 1782, 2245, 2828, 3564, 4490, 5657, 7127, 8909};
#endif
	/* standard values : M9MO */
	u16 iso_std_values[] = {
		64, 80, 100, 125, 160,
		200, 250, 320, 400, 500,
		640, 800, 1000, 1250, 1600,
		2000, 2500, 3200, 4000, 5000,
		6400
	};
	/* quantization table */
	u16 iso_qtable[] = {
		72, 89, 112, 141, 179,
		224, 283, 358, 447, 566,
		716, 894, 1118, 1414, 1789,
		2236, 2828, 3578, 4472, 5657,
		7155
	};

#ifdef EXIF_ONE_HALF_STOP_STEP
	s16 ss_std_values[] = {
		-400, -358, -300, -258, -200,
		-158, -100, -58, 0, 51,
		100, 158, 200, 258, 300,
		332, 391, 432, 491, 549,
		591, 649, 697, 749, 797,
		845, 897, 955, 997, 1055,
	};

	s16 ss_qtable[] = {
		-375, -325, -275, -225, -175,
		-125, -75, -25, 25, 75,
		125, 175, 225, 275, 325,
		375, 425, 475, 525, 575,
		625, 675, 725, 775, 825,
		875, 925, 975, 1025, 1075,
	};
#endif
#ifdef EXIF_ONE_THIRD_STOP_STEP
	s16 ss_std_values[] = {
		-400, -370, -332, -300, -258,
		-232, -200, -168, -132, -100,
		-68, -38, 0, 32, 74,
		100, 132, 158, 200, 232,
		258, 300, 332, 370, 390,
		432, 464, 490, 532, 564,
		590, 632, 664, 697, 732,
		764, 797, 832, 864, 897,
		932, 964, 997, 1029, 1064,
	};

	s16 ss_qtable[] = {
		-383, -350, -317, -283, -250,
		-217, -183, -150, -117, -83,
		-50, -17, 17, 50, 83,
		117, 150, 183, 217, 250,
		283, 317, 350, 383, 417,
		450, 483, 517, 550, 583,
		617, 650, 683, 717,	750,
		783, 817, 850, 883, 917,
		950, 983, 1017, 1050, 1083,
	};
#endif

	int num, den, i, err;

	cam_trace("E\n");

	/* exposure time */
	err = m9mo_readl(sd, M9MO_CATEGORY_EXIF,
		M9MO_EXIF_EXPTIME_NUM, &num);
	CHECK_ERR(err);
	err = m9mo_readl(sd, M9MO_CATEGORY_EXIF,
		M9MO_EXIF_EXPTIME_DEN, &den);
	CHECK_ERR(err);
	if (den)
		state->exif.exptime = (u32)num*1000/den;
	else
		state->exif.exptime = 0;

	/* flash */
	err = m9mo_readw(sd, M9MO_CATEGORY_EXIF, M9MO_EXIF_FLASH, &num);
	CHECK_ERR(err);
	state->exif.flash = (u16)num;

	/* iso */
	err = m9mo_readw(sd, M9MO_CATEGORY_EXIF, M9MO_EXIF_ISO, &num);
	CHECK_ERR(err);
	for (i = 0; i < NELEMS(iso_qtable); i++) {
		if (num <= iso_qtable[i]) {
			state->exif.iso = iso_std_values[i];
			break;
		}
	}
	if (i == NELEMS(iso_qtable))
			state->exif.iso = 8000;

	cam_info("%s: real iso = %d, qtable_iso = %d, stored iso = %d\n",
			__func__, num, iso_qtable[i], state->exif.iso);

	/* shutter speed */
	err = m9mo_readl(sd, M9MO_CATEGORY_EXIF, M9MO_EXIF_TV_NUM, &num);
	CHECK_ERR(err);
	err = m9mo_readl(sd, M9MO_CATEGORY_EXIF, M9MO_EXIF_TV_DEN, &den);
	CHECK_ERR(err);
#if 0
	if (den)
		state->exif.tv = num*M9MO_DEF_APEX_DEN/den;
	else
		state->exif.tv = 0;
#endif

	if (den) {
		for (i = 0; i < NELEMS(ss_qtable); i++) {
			if (num*M9MO_DEF_APEX_DEN/den <= ss_qtable[i]) {
				state->exif.tv = ss_std_values[i];
				break;
			}
		}
		if (i == NELEMS(ss_qtable))
			state->exif.tv = 1097;
		cam_info("%s: real TV = %d, stored TV = %d\n", __func__,
				num*M9MO_DEF_APEX_DEN/den, state->exif.tv);
	} else
		state->exif.tv = 0;

	/* brightness */
	err = m9mo_readl(sd, M9MO_CATEGORY_EXIF, M9MO_EXIF_BV_NUM, &num);
	CHECK_ERR(err);
	err = m9mo_readl(sd, M9MO_CATEGORY_EXIF, M9MO_EXIF_BV_DEN, &den);
	CHECK_ERR(err);
	if (den)
		state->exif.bv = num*M9MO_DEF_APEX_DEN/den;
	else
		state->exif.bv = 0;

	/* exposure bias value */
#if 0
	err = m9mo_readl(sd, M9MO_CATEGORY_EXIF, M9MO_EXIF_EBV_NUM, &num);
	CHECK_ERR(err);
	err = m9mo_readl(sd, M9MO_CATEGORY_EXIF, M9MO_EXIF_EBV_DEN, &den);
	CHECK_ERR(err);
	if (den)
		state->exif.ebv = num*M9MO_DEF_APEX_DEN/den;
	else
		state->exif.ebv = 0;
#else
	err = m9mo_readb(sd, M9MO_CATEGORY_AE, M9MO_AE_INDEX, &num);
	CHECK_ERR(err);
	cam_info("%s: EV index = %d", __func__, num);
	state->exif.ebv = (num - 30) * 10;
#endif

	/* Aperture */
	err = m9mo_readl(sd, M9MO_CATEGORY_EXIF, M9MO_EXIF_AV_NUM, &num);
	CHECK_ERR(err);
	err = m9mo_readl(sd, M9MO_CATEGORY_EXIF, M9MO_EXIF_AV_DEN, &den);
	CHECK_ERR(err);
	if (den)
		state->exif.av = num*M9MO_DEF_APEX_DEN/den;
	else
		state->exif.av = 0;
	cam_info("%s: AV num = %d, AV den = %d\n", __func__, num, den);

	/* Focal length */
	err = m9mo_readw(sd, M9MO_CATEGORY_LENS, M9MO_EXIF_FL, &num);
	CHECK_ERR(err);
	state->exif.focal_length = num * M9MO_DEF_APEX_DEN;
	cam_info("%s: FL = %d\n", __func__, num);

	/* Focal length 35m */
	err = m9mo_readw(sd, M9MO_CATEGORY_LENS, M9MO_EXIF_FL_35, &num);
	CHECK_ERR(err);
	state->exif.focal_35mm_length = num * M9MO_DEF_APEX_DEN;
	cam_info("%s: FL_35 = %d\n", __func__, num);

	cam_trace("X\n");

	return err;
}

static int m9mo_get_fd_eye_blink_result(struct v4l2_subdev *sd)
{
	struct m9mo_state *state = to_state(sd);
	int err;
	s32 val_no = 1, val_level = 0;

	/* EyeBlink error check FRAME No, Level */
	err = m9mo_readb(sd, M9MO_CATEGORY_FD,
			M9MO_FD_BLINK_FRAMENO, &val_no);
	CHECK_ERR(err);
	if (val_no < 1 || val_no > 3) {
		val_no = 1;
		cam_warn("Read Error FD_BLINK_FRAMENO [0x%x]\n", val_no);
	}
	err = m9mo_readb(sd, M9MO_CATEGORY_FD,
			M9MO_FD_BLINK_LEVEL_1+val_no-1, &val_level);
	CHECK_ERR(err);

	if ((val_level == 0xFF) || (val_level <= 0x3C))
		state->fd_eyeblink_cap = 1;
	else
		state->fd_eyeblink_cap = 0;
	cam_dbg("blink no[%d] level[0x%x]\n", val_no, val_level);

	return err;
}

static int m9mo_get_red_eye_fix_result(struct v4l2_subdev *sd)
{
	struct m9mo_state *state = to_state(sd);
	int err;
	s32 red_eye_status;

	if (state->flash_mode != FLASH_MODE_RED_EYE_FIX)
		return 0;

	err = m9mo_readb(sd, M9MO_CATEGORY_FD,
			M9MO_FD_RED_DET_STATUS, &red_eye_status);
	CHECK_ERR(err);

	state->fd_red_eye_status = red_eye_status;

	cam_dbg("red eye status [0x%x]\n", red_eye_status);

	return err;
}

static int m9mo_start_dual_postview(struct v4l2_subdev *sd, int frame_num)
{
#if 0
	struct m9mo_state *state = to_state(sd);
#endif
	int err, int_factor;
	cam_trace("E : %d frame\n", frame_num);

	/* Select image number of frame Preview image */
	err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
		M9MO_PARM_SEL_FRAME_VIDEO_SNAP, frame_num);
	CHECK_ERR(err);

	/* Select main image format */
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
		M9MO_CAPPARM_YUVOUT_PREVIEW, 0x00);
	CHECK_ERR(err);

#if 0
	/* Select preview image size */
#if 0
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
	M9MO_CAPPARM_PREVIEW_IMG_SIZE, 0x08);
	CHECK_ERR(err);
#else
	if (FRM_RATIO(state->preview) == CAM_FRMRATIO_VGA) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
		M9MO_CAPPARM_PREVIEW_IMG_SIZE, 0x08);
		CHECK_ERR(err);
	} else if (FRM_RATIO(state->preview) == CAM_FRMRATIO_HD) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
		M9MO_CAPPARM_PREVIEW_IMG_SIZE, 0x0F);
		CHECK_ERR(err);
	}
#endif
#endif

	/* Get Video Snap Shot data */
	err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
		M9MO_PARM_VIDEO_SNAP_IMG_TRANSFER_START, 0x02);
	CHECK_ERR(err);

	/* Clear Interrupt factor */
	int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
	if (!(int_factor & M9MO_INT_FRAME_SYNC)) {
		cam_err("M9MO_INT_FRAME_SYNC isn't issued, %#x\n", int_factor);
		return -ETIMEDOUT;
	}

	cam_trace("X\n");
	return err;
}

static int m9mo_start_dual_capture(struct v4l2_subdev *sd, int frame_num)
{
	struct m9mo_state *state = to_state(sd);
	int err, int_factor;
	cam_trace("E : %d frame\n", frame_num);

	/* Select image number of frame For Video Snap Shot image */
	err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
		M9MO_PARM_SEL_FRAME_VIDEO_SNAP, frame_num);
	CHECK_ERR(err);

	/* Select main image format */
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
		M9MO_CAPPARM_YUVOUT_MAIN, 0x01);
	CHECK_ERR(err);

	/* Select main image size - 4M */
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
		M9MO_CAPPARM_MAIN_IMG_SIZE, 0x1E);
	CHECK_ERR(err);

	/* Get Video Snap Shot data */
	err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
		M9MO_PARM_VIDEO_SNAP_IMG_TRANSFER_START, 0x01);
	CHECK_ERR(err);

	/* Clear Interrupt factor */
	int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
	if (!(int_factor & M9MO_INT_FRAME_SYNC)) {
		cam_err("M9MO_INT_FRAME_SYNC isn't issued, %#x\n", int_factor);
		return -ETIMEDOUT;
	}

	/* Get main image JPEG size */
	err = m9mo_readl(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_IMG_SIZE, &state->jpeg.main_size);
	CHECK_ERR(err);
	cam_trace("~~~~~~ main_size : 0x%x ~~~~~~\n", state->jpeg.main_size);
#if 1
	state->jpeg.main_offset = 0;
	state->jpeg.thumb_offset = M9MO_JPEG_MAXSIZE;
	state->jpeg.postview_offset = M9MO_JPEG_MAXSIZE + M9MO_THUMB_MAXSIZE;

	/* Read Exif information */
	m9mo_get_exif(sd);
#endif

	if (frame_num == state->dual_capture_frame)
		state->dual_capture_start = 0;

	cam_trace("X\n");
	return err;
}

static int m9mo_start_postview_capture(struct v4l2_subdev *sd, int frame_num)
{
	struct m9mo_state *state = to_state(sd);
	int err, int_factor;
	cam_trace("E : %d frame\n", frame_num);

	state->fast_capture_set = 0;

	if (state->dual_capture_start)
		return m9mo_start_dual_postview(sd, frame_num);

	if (state->running_capture_mode == RUNNING_MODE_CONTINUOUS
		|| state->running_capture_mode == RUNNING_MODE_BEST) {

		cam_dbg("m9mo_start_postview_capture (%d)\n", frame_num);

		/* Select image number of frame */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_FRM_PRV_SEL, frame_num);
		CHECK_ERR(err);

		/* Clear Interrupt factor */
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_CAPTURE)) {
			cam_warn("M9MO_INT_CAPTURE isn't issued on frame select, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
	} else if (state->running_capture_mode == RUNNING_MODE_AE_BRACKET
		|| state->running_capture_mode == RUNNING_MODE_LOWLIGHT) {
		/* Select image number of frame */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_FRM_PRV_SEL, frame_num);
		CHECK_ERR(err);

		/* Clear Interrupt factor */
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_CAPTURE)) {
			cam_warn("M9MO_INT_CAPTURE isn't issued on frame select, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
	} else if (state->running_capture_mode == RUNNING_MODE_WB_BRACKET) {
		/* Select image number of frame */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_FRM_PRV_SEL, frame_num);
		CHECK_ERR(err);
	} else if (state->running_capture_mode == RUNNING_MODE_HDR) {
		cam_warn("HDR have no PostView\n");
		return 0;
	} else if (state->running_capture_mode == RUNNING_MODE_BLINK) {
		/* Select image number of frame */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_FRM_PRV_SEL, 0xFF);
		CHECK_ERR(err);

		/* Clear Interrupt factor */
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_CAPTURE)) {
			cam_warn("M9MO_INT_CAPTURE isn't issued on frame select, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
	} else if (state->running_capture_mode == RUNNING_MODE_BURST) {
		int i;

		/* Get Preview data */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_TRANSFER, 0x02);
		CHECK_ERR(err);

		for (i = 0;  i < 3; i++) { /*wait M9MO_INT_FRAME_SYNC*/
			/* Clear Interrupt factor */
			int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
			if (int_factor & (M9MO_INT_CAPTURE|M9MO_INT_SOUND)) {
				cam_trace("----skip interrupt=%x", int_factor);
				continue;
			}

			if (!(int_factor & M9MO_INT_FRAME_SYNC)) {
				cam_warn("M9MO_INT_FRAME_SYNC isn't issued on transfer, %#x\n",
						int_factor);
				return -ETIMEDOUT;
			}
			break;
		}
	} else {
		/* Select image number of frame */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_FRM_PRV_SEL, 0x01);
		CHECK_ERR(err);
	}

	if (state->running_capture_mode != RUNNING_MODE_BURST) {
		/* Set YUV out for Preview */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
				M9MO_CAPPARM_YUVOUT_PREVIEW, 0x00);
		CHECK_ERR(err);

#if 0
		/* Set Preview(Postview) Image size */
		if (FRM_RATIO(state->capture) == CAM_FRMRATIO_HD) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
					M9MO_CAPPARM_PREVIEW_IMG_SIZE, 0x0F);
			CHECK_ERR(err);
		} else if (FRM_RATIO(state->capture) == CAM_FRMRATIO_D1) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
					M9MO_CAPPARM_PREVIEW_IMG_SIZE, 0x14);
			CHECK_ERR(err);
		} else {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
					M9MO_CAPPARM_PREVIEW_IMG_SIZE, 0x13);
			CHECK_ERR(err);
		}
#endif

		/* Get Preview data */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_TRANSFER, 0x02);
		CHECK_ERR(err);

		/* Clear Interrupt factor */
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_CAPTURE)) {
			cam_warn("M9MO_INT_CAPTURE isn't issued on transfer, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
	}

/*
	err = m9mo_readl(sd, M9MO_CATEGORY_CAPCTRL, M9MO_CAPCTRL_IMG_SIZE,
				&state->jpeg.main_size);
	CHECK_ERR(err);

	err = m9mo_readl(sd, M9MO_CATEGORY_CAPCTRL, M9MO_CAPCTRL_THUMB_SIZE,
				&state->jpeg.thumb_size);
	CHECK_ERR(err);

	state->jpeg.main_offset = 0;
	state->jpeg.thumb_offset = M9MO_JPEG_MAXSIZE;
	state->jpeg.postview_offset = M9MO_JPEG_MAXSIZE + M9MO_THUMB_MAXSIZE;

	m9mo_get_exif(sd);
*/
	cam_trace("X\n");
	return err;
}

static int m9mo_start_YUV_capture(struct v4l2_subdev *sd, int frame_num)
{
	struct m9mo_state *state = to_state(sd);
	int err, int_factor;
	cam_trace("E : %d frame\n", frame_num);

	/* Select image number of frame */
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
	M9MO_CAPCTRL_FRM_SEL, frame_num);
	CHECK_ERR(err);

	/* Clear Interrupt factor */
	int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
	if (!(int_factor & M9MO_INT_CAPTURE)) {
		cam_warn("M9MO_INT_CAPTURE isn't issued on frame select, %#x\n",
				int_factor);
		return -ETIMEDOUT;
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_YUVOUT_MAIN, 0x00);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
		M9MO_CAPPARM_MAIN_IMG_SIZE, state->capture->reg_val);
	CHECK_ERR(err);
	if (state->smart_zoom_mode)
		m9mo_set_smart_zoom(sd, state->smart_zoom_mode);
	cam_trace("Select image size [ w=%d, h=%d ]\n",
			state->capture->width, state->capture->height);

	/* Get main YUV data */
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_TRANSFER, 0x01);
	CHECK_ERR(err);

	/* Clear Interrupt factor */
	int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
	if (!(int_factor & M9MO_INT_CAPTURE)) {
		cam_warn("M9MO_INT_CAPTURE isn't issued on transfer, %#x\n",
				int_factor);
		return -ETIMEDOUT;
	}

	err = m9mo_readl(sd, M9MO_CATEGORY_CAPCTRL, M9MO_CAPCTRL_IMG_SIZE,
				&state->jpeg.main_size);
	CHECK_ERR(err);
/*
	err = m9mo_readl(sd, M9MO_CATEGORY_CAPCTRL, M9MO_CAPCTRL_THUMB_SIZE,
				&state->jpeg.thumb_size);
	CHECK_ERR(err);

	state->jpeg.main_offset = 0;
	state->jpeg.thumb_offset = M9MO_JPEG_MAXSIZE;
	state->jpeg.postview_offset = M9MO_JPEG_MAXSIZE + M9MO_THUMB_MAXSIZE;

	m9mo_get_exif(sd);
*/
	cam_trace("X\n");
	return err;
}

static int m9mo_start_YUV_one_capture(struct v4l2_subdev *sd, int frame_num)
{
	struct m9mo_state *state = to_state(sd);
	int err, int_factor;
	cam_trace("E : %d frame\n", frame_num);

	/* Select image number of frame */
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_FRM_SEL, 0x01);
	CHECK_ERR(err);

	/* Select main image format */
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_YUVOUT_MAIN, 0x00);
	CHECK_ERR(err);

	/* Select main image size */
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
		M9MO_CAPPARM_MAIN_IMG_SIZE, state->capture->reg_val);
	CHECK_ERR(err);

	/* Get main YUV data */
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_TRANSFER, 0x01);

	/* Clear Interrupt factor */
	int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
	if (!(int_factor & M9MO_INT_CAPTURE)) {
		cam_warn("M9MO_INT_CAPTURE isn't issued on transfer, %#x\n",
				int_factor);
		return -ETIMEDOUT;
	}

	err = m9mo_readl(sd, M9MO_CATEGORY_CAPCTRL, M9MO_CAPCTRL_IMG_SIZE,
				&state->jpeg.main_size);
	CHECK_ERR(err);
	cam_dbg("   ==> main image size=%d\n", state->jpeg.main_size);

	state->jpeg.main_offset = 0;
	state->jpeg.thumb_offset = M9MO_JPEG_MAXSIZE;
	state->jpeg.postview_offset = M9MO_JPEG_MAXSIZE + M9MO_THUMB_MAXSIZE;

	m9mo_get_exif(sd);

	cam_trace("X\n");
	return err;
}


static int m9mo_start_capture(struct v4l2_subdev *sd, int frame_num)
{
	struct m9mo_state *state = to_state(sd);
	int err, int_factor;
	cam_trace("E : %d frame\n", frame_num);

	state->fast_capture_set = 0;

	if (state->dual_capture_start)
		return m9mo_start_dual_capture(sd, frame_num);

	if (state->running_capture_mode == RUNNING_MODE_CONTINUOUS
		|| state->running_capture_mode == RUNNING_MODE_BEST) {

		cam_dbg("m9mo_start_capture() num=%d\n", frame_num);

		/* Select image number of frame */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_FRM_SEL, frame_num);
		CHECK_ERR(err);

		/* Clear Interrupt factor */
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_CAPTURE)) {
			cam_warn("M9MO_INT_CAPTURE isn't issued on frame select, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
	} else if (state->running_capture_mode == RUNNING_MODE_AE_BRACKET
		|| state->running_capture_mode == RUNNING_MODE_LOWLIGHT) {

		/* Select image number of frame */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_FRM_SEL, frame_num);
		CHECK_ERR(err);

		/* Clear Interrupt factor */
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_CAPTURE)) {
			cam_warn("M9MO_INT_CAPTURE isn't issued on frame select, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
	} else if (state->running_capture_mode == RUNNING_MODE_WB_BRACKET) {
		/* Select image number of frame */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_FRM_SEL, frame_num);
		CHECK_ERR(err);
	} else if (state->running_capture_mode == RUNNING_MODE_BLINK) {
		/* Select image number of frame */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_FRM_SEL, 0xFF);
		CHECK_ERR(err);

		/* Clear Interrupt factor */
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_CAPTURE)) {
			cam_warn("M9MO_INT_CAPTURE isn't issued on frame select, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}

		err = m9mo_get_fd_eye_blink_result(sd);
		CHECK_ERR(err);
	} else if (state->running_capture_mode == RUNNING_MODE_RAW) {
		/* Select Main Image Format */
		if (frame_num == 0) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
					M9MO_CAPPARM_YUVOUT_MAIN, 0x05);
		} else {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
					M9MO_CAPPARM_YUVOUT_MAIN, 0x01);
		}
		CHECK_ERR(err);

		/* Select image number of frame */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_FRM_SEL, 0x01);
		CHECK_ERR(err);

		if (frame_num == 1) {
			/* Set Size */
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
				M9MO_CAPPARM_MAIN_IMG_SIZE, 0x33);
			CHECK_ERR(err);
		}
	} else if (state->running_capture_mode == RUNNING_MODE_BURST) {
		err = 0;

	} else {
		/* Select image number of frame */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_FRM_SEL, 0x01);
		CHECK_ERR(err);
	}

	m9mo_get_red_eye_fix_result(sd);

#if 0
	/* Set main image JPEG fime max size */
	err = m9mo_writel(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_JPEG_SIZE_MAX, 0x01000000);
	CHECK_ERR(err);

	/* Set main image JPEG fime min size */
	err = m9mo_writel(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_JPEG_SIZE_MIN, 0x00100000);
	CHECK_ERR(err);
#endif
	 if (state->running_capture_mode == RUNNING_MODE_LOWLIGHT) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
				M9MO_CAPPARM_YUVOUT_MAIN, 0x0);
		CHECK_ERR(err);
	} else {
		if (state->running_capture_mode != RUNNING_MODE_RAW
		    && state->running_capture_mode != RUNNING_MODE_BURST) {
			/* Select main image format */
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
					M9MO_CAPPARM_YUVOUT_MAIN, 0x01);
			CHECK_ERR(err);
		}
	}

	/* Get main JPEG data */
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_TRANSFER, 0x01);

	if (state->running_capture_mode == RUNNING_MODE_BURST) {
		int i;
		for (i = 0;  i < 3; i++) { /*wait M9MO_INT_CAPTURE*/

			int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
			if (int_factor & (M9MO_INT_FRAME_SYNC|M9MO_INT_SOUND)) {
				cam_trace("----skip interrupt=%x", int_factor);
				continue;
			}

			if (!(int_factor & M9MO_INT_CAPTURE)) {
				cam_warn("M9MO_INT_CAPTURE isn't issued on transfer, %#x\n",
						int_factor);
				return -ETIMEDOUT;
			}
			break;
		}
	} else {
		/* Clear Interrupt factor */
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_CAPTURE)) {
			cam_warn("M9MO_INT_CAPTURE isn't issued on transfer, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
	}

	err = m9mo_readl(sd, M9MO_CATEGORY_CAPCTRL, M9MO_CAPCTRL_IMG_SIZE,
				&state->jpeg.main_size);
	CHECK_ERR(err);
	cam_dbg("   ==> jpeg size=%d\n", state->jpeg.main_size);

	state->jpeg.main_offset = 0;
	state->jpeg.thumb_offset = M9MO_JPEG_MAXSIZE;
	state->jpeg.postview_offset = M9MO_JPEG_MAXSIZE + M9MO_THUMB_MAXSIZE;

	if (state->running_capture_mode != RUNNING_MODE_RAW) {
		if (state->running_capture_mode != RUNNING_MODE_LOWLIGHT)
			m9mo_get_exif(sd);
	} else {
		if (frame_num == 1) {
			m9mo_get_exif(sd);

			err = m9mo_set_mode(sd, M9MO_MONITOR_MODE);
			if (err <= 0) {
				cam_err("failed to set mode\n");
				return err;
			}

			err = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
			if (!(err & M9MO_INT_MODE)) {
				cam_err("m9mo_start_capture() MONITOR_MODE error\n");
				return -ETIMEDOUT;
			}

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
				0x78, 0x00);
		CHECK_ERR(err);
		}
	}

	cam_trace("X\n");
	return err;
}

/*static int m9mo_set_hdr(struct v4l2_subdev *sd, int val)
{
	cam_trace("E val : %d\n", val);
	cam_trace("X\n");
	return 0;
}*/

static int m9mo_start_capture_thumb(struct v4l2_subdev *sd, int frame_num)
{
	struct m9mo_state *state = to_state(sd);
	int err, int_factor;
	cam_trace("E : %d frame\n", frame_num);

	cam_dbg("m9mo_start_capture_thumb() num=%d\n", frame_num);

	/* Select image number of frame */
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_FRM_THUMB_SEL, frame_num);
	CHECK_ERR(err);

	/* Clear Interrupt factor */
	int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
	if (!(int_factor & M9MO_INT_CAPTURE)) {
		cam_warn("M9MO_INT_CAPTURE isn't issued on frame select, %#x\n",
				int_factor);
		return -ETIMEDOUT;
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_YUVOUT_THUMB, 0x01);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_THUMB_IMG_SIZE, 0x04);  /* 320 x 240 */
	CHECK_ERR(err);

	/* Get main thumb data */
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_TRANSFER, 0x03);

	/* Clear Interrupt factor */
	int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
	if (!(int_factor & M9MO_INT_CAPTURE)) {
		cam_warn("M9MO_INT_CAPTURE isn't issued on transfer, %#x\n",
				int_factor);
		return -ETIMEDOUT;
	}

	err = m9mo_readl(sd, M9MO_CATEGORY_CAPCTRL, M9MO_CAPCTRL_THUMB_SIZE,
				&state->jpeg.thumb_size);
	CHECK_ERR(err);

	return err;
}

static int m9mo_set_facedetect(struct v4l2_subdev *sd, int val)
{
	int err;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E val : %d\n", val);

	state->facedetect_mode = val;

	switch (state->facedetect_mode) {
	case FACE_DETECTION_NORMAL:
	case FACE_DETECTION_BLINK:
		cam_dbg("~~~~~~ face detect on ~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_FD, M9MO_FD_SIZE, 0x04);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_FD, M9MO_FD_MAX, 0x07);
		CHECK_ERR(err);
		if (state->mode == MODE_SMART_AUTO) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_FD,
				M9MO_FD_CTL, 0x01);
		} else {
			err = m9mo_writeb(sd, M9MO_CATEGORY_FD,
				M9MO_FD_CTL, 0x11);
		}
		CHECK_ERR(err);

		if (state->isp_mode == M9MO_MONITOR_MODE)
			msleep(30);

		break;

	case FACE_DETECTION_SMILE_SHOT:
		cam_dbg("~~~~~~ fd smile shot ~~~~~~ val : %d\n", val);
		break;

	case FACE_DETECTION_OFF:
	default:
		cam_dbg("~~~~~~ face detect off ~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_FD, M9MO_FD_CTL, 0x00);
		CHECK_ERR(err);
		break;
	}
	cam_trace("X\n");
	return 0;
}

static int m9mo_set_bracket_aeb(struct v4l2_subdev *sd, int val)
{
	int err;
	cam_trace("E val : %d\n", val);

	switch (val) {
	case BRACKET_AEB_VALUE0:
		cam_dbg("~~~~~~ AEB value0 ~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_AUTO_BRACKET_EV, 0x00); /* EV 0.0 */
		CHECK_ERR(err);
		break;

	case BRACKET_AEB_VALUE1:
		cam_dbg("~~~~~~ AEB value1 ~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_AUTO_BRACKET_EV, 0x1E); /* EV 0.3 */
		CHECK_ERR(err);
		break;

	case BRACKET_AEB_VALUE2:
		cam_dbg("~~~~~~ AEB value2 ~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_AUTO_BRACKET_EV, 0x3C); /* EV 0.6 */
		CHECK_ERR(err);
		break;

	case BRACKET_AEB_VALUE3:
		cam_dbg("~~~~~~ AEB value3 ~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_AUTO_BRACKET_EV, 0x64); /* EV 1.0 */
		CHECK_ERR(err);
		break;

	case BRACKET_AEB_VALUE4:
		cam_dbg("~~~~~~ AEB value4 ~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_AUTO_BRACKET_EV, 0x82); /* EV 1.3 */
		CHECK_ERR(err);
		break;

	case BRACKET_AEB_VALUE5:
		cam_dbg("~~~~~~ AEB value5 ~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_AUTO_BRACKET_EV, 0xA0); /* EV 1.6 */
		CHECK_ERR(err);
		break;

	case BRACKET_AEB_VALUE6:
		cam_dbg("~~~~~~ AEB value6 ~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_AUTO_BRACKET_EV, 0xC8); /* EV 2.0 */
		CHECK_ERR(err);
		break;

	default:
		cam_err("~~~~ TBD ~~~~ val : %d", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_AUTO_BRACKET_EV, 0x64); /* Ev 1.0 */
		CHECK_ERR(err);
		break;
	}
	cam_trace("X\n");
	return 0;
}

static int m9mo_set_bracket_wbb(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err;
	cam_trace("E val : %d\n", val);

	switch (val) {
	case BRACKET_WBB_VALUE1:
		cam_trace("~~~~~~ WBB value1  AB 3~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_WBB_MODE, 0x01);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_WBB_AB, 0x30);
		CHECK_ERR(err);
		break;

	case BRACKET_WBB_VALUE2:
		cam_trace("~~~~~~ WBB value2  AB 2~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_WBB_MODE, 0x01);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_WBB_AB, 0x20);
		CHECK_ERR(err);
		break;

	case BRACKET_WBB_VALUE3:
		cam_trace("~~~~~~ WBB value3  AB 1~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_WBB_MODE, 0x01);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_WBB_AB, 0x0F);
		CHECK_ERR(err);
		break;

	case BRACKET_WBB_VALUE4:
		cam_trace("~~~~~~ WBB value4  GM 3~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_WBB_MODE, 0x02);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_WBB_GM, 0x30);
		CHECK_ERR(err);
		break;

	case BRACKET_WBB_VALUE5:
		cam_trace("~~~~~~ WBB value5  GM 2~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_WBB_MODE, 0x02);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_WBB_GM, 0x20);
		CHECK_ERR(err);
		break;

	case BRACKET_WBB_VALUE6:
		cam_trace("~~~~~~ WBB value6  GM 1~~~~~~ val : %d\n", val);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_WBB_MODE, 0x02);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_WBB_GM, 0x0F);
		CHECK_ERR(err);
		break;

	case BRACKET_WBB_OFF:
		cam_trace("~~~~~~ WBB Off ~~~~~~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_WBB_MODE, 0x00);
		CHECK_ERR(err);
		break;

	default:
		val = 0xFF;
		cam_err("~~~~ TBD ~~~~ val : %d", val);
		break;
	}

	if (val != 0xFF)
		state->bracket_wbb_val = val;

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_bracket(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	cam_trace("E val : %d\n", val);

	switch (val) {
	case BRACKET_MODE_OFF:
	case BRACKET_MODE_AEB:
		cam_dbg("~~~~~~ bracket aeb on ~~~~~~ val : %d\n", val);
		m9mo_set_bracket_wbb(sd, BRACKET_WBB_OFF);
		break;

	case BRACKET_MODE_WBB:
		cam_dbg("~~~~~~ bracket wbb on ~~~~~~ val : %d\n", val);
		if (state->bracket_wbb_val == BRACKET_WBB_OFF)
			state->bracket_wbb_val = BRACKET_WBB_VALUE3;
		m9mo_set_bracket_wbb(sd, state->bracket_wbb_val);
		break;

	default:
		cam_err("~~~~ TBD ~~~~ val : %d", val);
		break;
	}
	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_cam_sys_mode(struct v4l2_subdev *sd, int val)
{
	int old_mode;

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_SYSMODE_CAPTURE:
		cam_trace("~ FACTORY_SYSMODE_CAPTURE ~\n");
#if 0
		old_mode = m9mo_set_mode(sd, M9MO_STILLCAP_MODE);
#else
		old_mode = m9mo_set_mode_part1(sd, M9MO_STILLCAP_MODE);
		old_mode = m9mo_set_mode_part2(sd, M9MO_STILLCAP_MODE);
#endif
		break;

	case FACTORY_SYSMODE_MONITOR:
		break;

	case FACTORY_SYSMODE_PARAM:
		cam_trace("~ FACTORY_SYSMODE_PARAM ~\n");
		old_mode = m9mo_set_mode(sd, M9MO_PARMSET_MODE);
		break;

	default:
		cam_trace("~ FACTORY_SYSMODE_DEFAULT ~\n");
		break;
	}
	cam_trace("X\n");
	return 0;

}

static int m9mo_set_fps(struct v4l2_subdev *sd, int val)
{
	int err;

	struct m9mo_state *state = to_state(sd);
	cam_trace("E val : %d\n", val);

	if (val == state->fps) {
		cam_info("same fps. skip\n");
		return 0;
	}
	if (val <= 0 || val > 120) {
		cam_err("invalid frame rate %d\n", val);
		val = 0; /* set to auto(default) */
	}

	cam_info("set AE EP to %d\n", val);

	switch (val) {
	case 120:
		cam_trace("~~~~~~ 120 fps ~~~~~~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EP_MODE_MON, 0x1C);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EP_MODE_CAP, 0x1C);
		CHECK_ERR(err);
		break;

	case 60:
		cam_trace("~~~~~~ 60 fps ~~~~~~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EP_MODE_MON, 0x1A);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EP_MODE_CAP, 0x1A);
		CHECK_ERR(err);
		break;

	case 30:
		cam_trace("~~~~~~ 30 fps ~~~~~~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EP_MODE_MON, 0x19);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EP_MODE_CAP, 0x19);
		CHECK_ERR(err);
		break;

#if 0	/* after ISP update */
	case 15:
		cam_trace("~~~~~~ 15 fps ~~~~~~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
				M9MO_PARM_MON_FPS, 0x03);
		CHECK_ERR(err);
		break;
#endif

	default:
		cam_trace("~~~~~~ default : auto fps ~~~~~~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EP_MODE_MON, 0x09);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EP_MODE_CAP, 0x09);
		CHECK_ERR(err);
		break;
	}

#if 0	/* after ISP update */
	if (state->fps == 15 && val != 15) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
				M9MO_PARM_MON_FPS, 0x01);
		CHECK_ERR(err);
	}
#endif

	state->fps = val;

	m9mo_set_OIS_cap_mode(sd);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_time_info(struct v4l2_subdev *sd, int val)
{
	int err;
#if 0
	int read_hour, read_min;
#endif

	cam_trace("E val : %02d:%02d\n", ((val >> 8) & 0xFF), (val & 0xFF));

	err = m9mo_writew(sd, M9MO_CATEGORY_NEW,
		M9MO_NEW_TIME_INFO, val);
	CHECK_ERR(err);

#if 0	/* for check time */
	err = m9mo_readb(sd, M9MO_CATEGORY_NEW,
		M9MO_NEW_TIME_INFO, &read_hour);
	CHECK_ERR(err);

	err = m9mo_readb(sd, M9MO_CATEGORY_NEW,
		M9MO_NEW_TIME_INFO+1, &read_min);
	CHECK_ERR(err);

	cam_dbg("time %02d:%02d\n", read_hour, read_min);
#endif

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_lens_off_timer(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);
#if 1
	cam_trace("Lens off timer is disabled.\n");
	return 1;
#endif

	if (val > 0xFF) {
		cam_warn("Can not set over 0xFF, but set 0x%x", val);
		val = 0xFF;
	}

	err = m9mo_writeb2(sd, M9MO_CATEGORY_SYS,
		M9MO_SYS_LENS_TIMER, val);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 1;
}

static int m9mo_set_widget_mode_level(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err;
	int denominator = 500, numerator = 8;
	u32 f_number = 0x45;

	/* 3 step -> 2 step, low level is not used */
	if (val == 1)
		val = 2;

	/* valid values are 0, 2, 4 */
	state->widget_mode_level = val * 2 - 2;

	/* LIKE A PRO STEP SET */
	err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE,
		0x02, state->widget_mode_level);
	CHECK_ERR(err);

	if (state->mode == MODE_SILHOUETTE) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			0x41, 0x0);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			0x42, 0x0D + state->widget_mode_level);
		CHECK_ERR(err);
	} else if (state->mode == MODE_BLUE_SKY) {
		/* COLOR EFFECT SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, 0x11 + state->widget_mode_level);
		CHECK_ERR(err);
	} else if (state->mode == MODE_NATURAL_GREEN) {
		/* COLOR EFFECT SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, 0x21 + state->widget_mode_level);
		CHECK_ERR(err);
	} else if (state->mode == MODE_FIREWORKS) {
		/* Set Capture Shutter Speed Time */
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_NUMERATOR, 32);
		CHECK_ERR(err);
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_DENOMINATOR, 10);
		CHECK_ERR(err);

		/* Set Still Capture F-Number Value */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_F_NUMBER, 0x80);
		CHECK_ERR(err);

	} else if (state->mode == MODE_LIGHT_TRAIL_SHOT) {
		/* Set Capture Shutter Speed Time */
		if (state->widget_mode_level == 0)
			numerator = 3;
		else if (state->widget_mode_level == 2)
			numerator = 5;
		else if (state->widget_mode_level == 4)
			numerator = 10;

		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_NUMERATOR, numerator);
		CHECK_ERR(err);
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_DENOMINATOR, 1);
		CHECK_ERR(err);
	} else if (state->mode == MODE_HIGH_SPEED) {
		/* Set Capture Shutter Speed Time */
		if (state->widget_mode_level == 0)
			denominator = 100;
		else if (state->widget_mode_level == 2)
			denominator = 125;
		else if (state->widget_mode_level == 4)
			denominator = 2000;

		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_NUMERATOR, 1);
		CHECK_ERR(err);
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_DENOMINATOR, denominator);
		CHECK_ERR(err);
	} else if (state->mode == MODE_CLOSE_UP) {
		/* Set Still Capture F-Number Value */
		if (state->widget_mode_level == 0)
			f_number = 0x80;
		else if (state->widget_mode_level == 2)
			f_number = 0x45;
		else if (state->widget_mode_level == 4)
			f_number = 0x28;

		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_F_NUMBER, f_number);
		CHECK_ERR(err);
	}

	cam_dbg("X %d %d\n", val, state->mode);
	return 0;
}

static int m9mo_set_LDC(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_dbg("%s\n", val ? "on" : "off");

	if (val == 1) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
			0x1B, 0x01);
		CHECK_ERR(err);
	} else {
		err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
			0x1B, 0x00);
		CHECK_ERR(err);
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_LSC(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_dbg("%s\n", val ? "on" : "off");

	if (val == 1) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
			0x07, 0x01);
		CHECK_ERR(err);
	} else {
		err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
			0x07, 0x00);
		CHECK_ERR(err);
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_aperture_preview(struct v4l2_subdev *sd, int val)
{
	int err, temp, i;
	unsigned char convert = 0x00;

	cam_trace("E val : %d\n", val);

	if (val < 28)
		val = 28;

	temp = val / 10;

	for (i = 0; i < temp; i++)
		convert += 0x10;

	temp = val % 10;
	convert += temp;

	cam_trace("check val : %d\n", convert);
	err = m9mo_writeb(sd, M9MO_CATEGORY_AE, 0x3D, convert);

	CHECK_ERR(err);
	cam_trace("X\n");
	return 0;
}

static int m9mo_set_aperture_capture(struct v4l2_subdev *sd, int val)
{
	int err, temp, i;
	unsigned char convert = 0x00;

	cam_trace("E val : %d\n", val);

	if (val < 28)
		val = 28;

	temp = val / 10;

	for (i = 0; i < temp; i++)
		convert += 0x10;

	temp = val % 10;
	convert += temp;

	cam_trace("check val : %d\n", convert);
	err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			0x36, convert);
	CHECK_ERR(err);
	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_OIS(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_OIS_RETURN_TO_CENTER:
		cam_trace("~ FACTORY_OIS_RETURN_TO_CENTER   ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			0x15, 0x30);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			0x16, 0x11);
		CHECK_ERR(err);
		break;

	case FACTORY_OIS_RUN:
		cam_trace("~ FACTORY_OIS_RUN ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			0x11, 0x01);
		CHECK_ERR(err);
		break;

	case FACTORY_OIS_START:
		cam_trace("~ FACTORY_OIS_START ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			0x20, 0x01);
		CHECK_ERR(err);
		break;

	case FACTORY_OIS_STOP:
		cam_trace("~ FACTORY_OIS_STOP ~\n");
		break;

	case FACTORY_OIS_MODE_ON:
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			0x11, 0x02);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_OIS_MODE_ON ~\n");
		break;

	case FACTORY_OIS_MODE_OFF:
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			0x10, 0x00);
		cam_trace("~ FACTORY_OIS_MODE_OFF ~\n");
		break;
	case FACTORY_OIS_LOG:
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			0x19, 0x01);
		CHECK_ERR(err);
		msleep(40);
		err = m9mo_make_CSV_rawdata(sd, M9MO_FLASH_FACTORY_OIS, false);
		CHECK_ERR(err);
		cam_trace("~FACTORY_OIS_LOG ~\n");
		break;

	case FACTORY_OIS_ON:
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			0x11, 0x02);
		CHECK_ERR(err);
		break;

	case FACTORY_OIS_DECENTER_LOG:
		err = m9mo_make_CSV_rawdata(sd,
			M9MO_FLASH_FACTORY_DECENTER, false);
		CHECK_ERR(err);
		cam_trace("~FACTORY_OIS_DECENTER_LOG ~\n");
		break;

	default:
		cam_err("~ m9mo_set_factory_OIS ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_OIS_shift(struct v4l2_subdev *sd, int val)
{
	int err;
	cam_trace("E val : 0x%x\n", val);
	err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			0x15, val);
	CHECK_ERR(err);
	err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			0x14, 0);
	CHECK_ERR(err);
	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_punt(struct v4l2_subdev *sd, int val)
{
	int err;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_PUNT_RANGE_START:
		cam_trace("~ FACTORY_PUNT_RANGE_START ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0E, 0x00);
		CHECK_ERR(err);
		break;

	case FACTORY_PUNT_RANGE_STOP:
		cam_trace("~ FACTORY_PUNT_RANGE_STOP ~\n");
		break;

	case FACTORY_PUNT_SHORT_SCAN_DATA:
		cam_trace("~ FACTORY_PUNT_SHORT_SCAN_DATA ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, 0x00);
		CHECK_ERR(err);
		break;

	case FACTORY_PUNT_SHORT_SCAN_START:
		cam_trace("~ FACTORY_PUNT_SHORT_SCAN_START ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0E, 0x01);
		CHECK_ERR(err);
		break;

	case FACTORY_PUNT_SHORT_SCAN_STOP:
		cam_trace("~ FACTORY_PUNT_SHORT_SCAN_STOP ~\n");
		break;

	case FACTORY_PUNT_LONG_SCAN_DATA:
		cam_trace("~ FACTORY_PUNT_LONG_SCAN_DATA ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, 0x00);
		CHECK_ERR(err);
		break;

	case FACTORY_PUNT_LONG_SCAN_START:
		cam_trace("~ FACTORY_PUNT_LONG_SCAN_START ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0E, 0x02);
		CHECK_ERR(err);
		break;

	case FACTORY_PUNT_LONG_SCAN_STOP:
		cam_trace("~FACTORY_PUNT_LONG_SCAN_STOP ~\n");
		break;

	case FACTORY_PUNT_LOG:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0E, 0x04);
		CHECK_ERR(err);
		msleep(40);
		err = m9mo_make_CSV_rawdata(sd, M9MO_FLASH_FACTORY_PUNT, false);
		CHECK_ERR(err);
		cam_trace("~FACTORY_PUNT_LOG ~\n");
		break;

	case FACTORY_PUNT_SET_RANGE_DATA:
		cam_trace("~FACTORY_PUNT_SET_RANGE_DATA ~\n");
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x17, (unsigned short)(state->f_punt_data.min));
		CHECK_ERR(err);

		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x19, (unsigned short)(state->f_punt_data.max));
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, (unsigned char)(state->f_punt_data.num));
		CHECK_ERR(err);

		cam_trace("~ FACTORY_PUNT_RANGE_START ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0E, 0x00);
		CHECK_ERR(err);
		break;

	case FACTORY_PUNT_EEP_WRITE:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0E, 0x05);
		CHECK_ERR(err);
		break;

	default:
		cam_err("~ m9mo_set_factory_punt ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_zoom(struct v4l2_subdev *sd, int val)
{
	int err;
	int end_check = 0;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_ZOOM_MOVE_STEP:
		cam_trace("~ FACTORY_ZOOM_MOVE_STEP ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0F, 0x00);
		CHECK_ERR(err);
		break;

	case FACTORY_ZOOM_RANGE_CHECK_START:
		cam_trace("~ FACTORY_ZOOM_RANGE_CHECK_START ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0F, 0x05);
		CHECK_ERR(err);
		break;

	case FACTORY_ZOOM_RANGE_CHECK_STOP:
		cam_trace("~ FACTORY_ZOOM_RANGE_CHECK_STOP ~\n");
		break;

	case FACTORY_ZOOM_SLOPE_CHECK_START:
		cam_trace("~ FACTORY_ZOOM_SLOPE_CHECK_START ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0E, 0x03);
		CHECK_ERR(err);
		break;

	case FACTORY_ZOOM_SLOPE_CHECK_STOP:
		cam_trace("~ FACTORY_ZOOM_SLOPE_CHECK_STOP ~\n");
		break;

	case FACTORY_ZOOM_SET_RANGE_CHECK_DATA:
		cam_trace("~ FACTORY_ZOOM_SET_RANGE_CHECK_DATA ~\n");
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x18, (unsigned short)(state->f_zoom_data.range_min));
		CHECK_ERR(err);

		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x1A, (unsigned short)(state->f_zoom_data.range_max));
		CHECK_ERR(err);
		break;

	case FACTORY_ZOOM_SET_SLOPE_CHECK_DATA:
		cam_trace("~ FACTORY_ZOOM_SET_SLOPE_CHECK_DATA ~\n");
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x18, (unsigned short)(state->f_zoom_data.slope_min));
		CHECK_ERR(err);

		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x1A, (unsigned short)(state->f_zoom_data.slope_max));
		CHECK_ERR(err);
		break;

	case FACTORY_ZOOM_STEP_TELE:
		cam_trace("~ FACTORY_ZOOM_STEP_TELE ~\n");
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x1A, 0x0F);
		CHECK_ERR(err);
		break;

	case FACTORY_ZOOM_STEP_WIDE:
		cam_trace("~ FACTORY_ZOOM_STEP_WIDE ~\n");
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x1A, 0x00);
		CHECK_ERR(err);
		break;

	case FACTORY_ZOOM_MOVE_END_CHECK:
		err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
			0x26, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		cam_trace("~ FACTORY_ZOOM_MOVE_CHECK ~\n");
		break;

	default:
		cam_err("~ m9mo_set_factory_zoom ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_zoom_step(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);
#if 1
	if (val >= 0 && val < 16) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1A, val);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, 0x00);
		CHECK_ERR(err);
	}
	cam_trace("~ FACTORY_ZOOM_MOVE_STEP ~\n");
	err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
		0x0F, 0x00);
	CHECK_ERR(err);
#else
	err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
		0x06, val);
	CHECK_ERR(err);
	msleep(500);
#endif
	return 0;
}

static int m9mo_set_factory_fail_stop(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_FAIL_STOP_ON:
		cam_trace("~ FACTORY_FAIL_STOP_ON ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, 0x01);
		CHECK_ERR(err);
		break;

	case FACTORY_FAIL_STOP_OFF:
		cam_trace("~ FACTORY_FAIL_STOP_OFF ~\n");
		break;

	case FACTORY_FAIL_STOP_RUN:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0D, 0x0C);
		CHECK_ERR(err);

		cam_trace("~ FACTORY_FAIL_STOP_RUN ~\n");
		break;

	case FACTORY_FAIL_STOP_STOP:
		cam_trace("~ FACTORY_FAIL_STOP_STOP ~\n");
		break;

	default:
		cam_err("~ m9mo_set_factory_fail_stop ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_nodefocus(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_NODEFOCUSYES_ON:
		cam_trace("~ FACTORY_NODEFOCUSYES_ON ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, 0x01);
		CHECK_ERR(err);
		break;

	case FACTORY_NODEFOCUSYES_OFF:
		cam_trace("~ FACTORY_NODEFOCUSYES_OFF ~\n");
		break;

	case FACTORY_NODEFOCUSYES_RUN:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0D, 0x09);
		CHECK_ERR(err);

		cam_trace("~ FACTORY_NODEFOCUSYES_RUN ~\n");
		break;

	case FACTORY_NODEFOCUSYES_STOP:
		cam_trace("~ FACTORY_NODEFOCUSYES_STOP ~\n");
		break;

	default:
		cam_err("~ m9mo_set_factory_defocus ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_interpolation(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_INTERPOLATION_USE:
		cam_trace("~ FACTORY_INTERPOLATION_USE ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0E, 0x0A);
		CHECK_ERR(err);
		break;

	case FACTORY_INTERPOLATION_RELEASE:
		cam_trace("~ FACTORY_INTERPOLATION_RELEASE ~\n");
		break;

	default:
		cam_err("~ m9mo_set_factory_interpolation ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_common(struct v4l2_subdev *sd, int val)
{
	int err, down_check = 1, end_check = 0;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_FIRMWARE_DOWNLOAD:
		cam_trace("~ FACTORY_FIRMWARE_DOWNLOAD ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			0x11, 0x00);
		CHECK_ERR(err);
		break;

	case FACTORY_DOWNLOAD_CHECK:
		cam_trace("~ FACTORY_DOWNLOAD_CHECK ~\n");
		err = m9mo_readb(sd, M9MO_CATEGORY_NEW,
			0x11, &down_check);
		CHECK_ERR(err);
		state->factory_down_check = down_check;
		break;

	case FACTORY_END_CHECK:
		cam_trace("~ FACTORY_END_CHECK ~\n");
		err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
			0x40, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		break;

	case FACTORY_COMMON_SET_FOCUS_ZONE_MACRO:
		cam_trace("~ FACTORY_COMMON_SET_FOCUS_ZONE_MACRO ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x07, 0x02);
		CHECK_ERR(err);
		break;

	case FACTORY_FPS30_ON:
		cam_trace("~ FACTORY_FPS30_ON ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x3F, 0x01);
		CHECK_ERR(err);
		break;

	case FACTORY_FPS30_OFF:
		cam_trace("~ FACTORY_FPS30_OFF ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x3F, 0x00);
		CHECK_ERR(err);
		break;

	default:
		cam_err("~ m9mo_set_factory_common ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_vib(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("m9mo_set_factory_vib E val : %d\n", val);

	switch (val) {
	case FACTORY_VIB_START:
		cam_trace("~ FACTORY_VIB_START ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			0x20, 0x02);
		CHECK_ERR(err);
		break;

	case FACTORY_VIB_STOP:
		cam_trace("~ FACTORY_VIB_STOP ~\n");
		break;

	case FACTORY_VIB_LOG:
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			0x19, 0x02);
		CHECK_ERR(err);
		msleep(40);
		err = m9mo_make_CSV_rawdata(sd, M9MO_FLASH_FACTORY_VIB, false);
		CHECK_ERR(err);
		cam_trace("~FACTORY_VIB_LOG ~\n");
		break;

	default:
		cam_err("~m9mo_set_factory_vib~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_gyro(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_GYRO_START:
		cam_trace("~ FACTORY_GYRO_START ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			0x20, 0x03);
		CHECK_ERR(err);
		break;

	case FACTORY_GYRO_STOP:
		cam_trace("~ FACTORY_GYRO_STOP ~\n");
		break;

	case FACTORY_GYRO_LOG:
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			0x19, 0x03);
		CHECK_ERR(err);
		msleep(40);
		err = m9mo_make_CSV_rawdata(sd, M9MO_FLASH_FACTORY_GYRO, false);
		CHECK_ERR(err);
		cam_trace("~FACTORY_PUNT_LOG ~\n");
		break;
	default:
		cam_err("~ m9mo_set_factory_gyro ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_backlash(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_BACKLASH_INPUT:
		cam_trace("~ FACTORY_BACKLASH_INPUT ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0A, 0x01);
		CHECK_ERR(err);
		break;

	case FACTORY_BACKLASH_MAX_THR:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0A, 0x00);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_BACKLASH_MAX_THR ~\n");
		break;

	case FACTORY_BACKLASH_WIDE_RUN:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0A, 0x03);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_BACKLASH_WIDE_RUN ~\n");
		break;

	case FACTORY_BACKLASH_LOG:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0A, 0x05);
		CHECK_ERR(err);
		msleep(40);
		err = m9mo_make_CSV_rawdata(sd,
			M9MO_FLASH_FACTORY_BACKLASH, false);
		CHECK_ERR(err);
		cam_trace("~FACTORY_BACKLASH_LOG ~\n");
		break;

	default:
		cam_err("~ m9mo_set_factory_backlash ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_backlash_count(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
		0x1B, val);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_af(struct v4l2_subdev *sd, int val)
{
	int err, end_check = 0;
	int result_check = 0;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_AF_LOCK_ON_SET:
		cam_trace("~ FACTORY_AF_LOCK_ON_SET ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, 0x01);
		CHECK_ERR(err);
		break;

	case FACTORY_AF_LOCK_OFF_SET:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, 0x00);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_AF_LOCK_OFF_SET ~\n");
		break;

	case FACTORY_AF_MOVE:
		cam_trace("~ FACTORY_AF_MOVE ~\n");
		break;

	case FACTORY_AF_STEP_LOG:
		if ((state->factory_test_num ==
					FACTORY_RESOL_WIDE) ||
			(state->factory_test_num ==
			 FACTORY_RESOL_WIDE_INSIDE)) {
			cam_trace("~ FACTORY_AF_STEP_LOG WIDE ~\n");
			err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
				0x0D, 0x1A);
			CHECK_ERR(err);
			msleep(40);
			err = m9mo_make_CSV_rawdata(sd,
				M9MO_FLASH_FACTORY_WIDE_RESOL, false);
			CHECK_ERR(err);
		} else if ((state->factory_test_num ==
					FACTORY_RESOL_TELE) ||
			(state->factory_test_num ==
			 FACTORY_RESOL_TELE_INSIDE)) {
			cam_trace("~ FACTORY_AF_STEP_LOG TELE ~\n");
			err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
				0x0D, 0x19);
			CHECK_ERR(err);
			msleep(40);
			err = m9mo_make_CSV_rawdata(sd,
				M9MO_FLASH_FACTORY_TELE_RESOL, false);
			CHECK_ERR(err);
		} else {
			cam_trace("~ FACTORY NUMBER ERROR ~\n");
		}
		break;

	case FACTORY_AF_LOCK_START:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0D, 0x01);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_AF_LOCK_START ~\n");
		break;

	case FACTORY_AF_LOCK_STOP:
		cam_trace("~ FACTORY_AF_LOCK_STOP ~\n");
		break;

	case FACTORY_AF_FOCUS_LOG:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0D, 0x0B);
		CHECK_ERR(err);
		msleep(40);
		err = m9mo_make_CSV_rawdata(sd,
			M9MO_FLASH_FACTORY_AF_FCS, false);
		CHECK_ERR(err);

		cam_trace("~ FACTORY_AF_FOCUS_LOG ~\n");
		break;

	case FACTORY_AF_INT_SET:
		cam_trace("~ FACTORY_AF_INT_SET ~\n");
		err = m9mo_readw(sd, M9MO_CATEGORY_LENS,
			0x23, &result_check);
		CHECK_ERR(err);
		state->factory_result_check = result_check;
		break;

	case FACTORY_AF_STEP_SAVE:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0D, 0x0A);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_AF_SETP_SAVE ~\n");
		break;

	case FACTORY_AF_SCAN_LIMIT_START:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0D, 0x06);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_AF_SCAN_LIMIT_START ~\n");
		break;

	case FACTORY_AF_SCAN_LIMIT_STOP:
		cam_trace("~ FACTORY_AF_SCAN_LIMIT_STOP ~\n");
		break;

	case FACTORY_AF_SCAN_RANGE_START:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0D, 0x07);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_AF_SCAN_RANGE_START ~\n");
		break;

	case FACTORY_AF_SCAN_RANGE_STOP:
		cam_trace("~ FACTORY_AF_SCAN_RANGE_STOP ~\n");
		break;

	case FACTORY_AF_LED_END_CHECK:
		cam_trace("~ FACTORY_AF_LED_END_CHECK ~\n");
		err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
			0x40, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		break;

	case FACTORY_AF_LED_LOG:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x4D, 0x02);
		CHECK_ERR(err);
		msleep(40);
		err = m9mo_make_CSV_rawdata(sd,
			M9MO_FLASH_FACTORY_AF_LED, false);
		CHECK_ERR(err);

		cam_trace("~ FACTORY_AF_LED_LOG ~\n");
		break;

	case FACTORY_AF_MOVE_END_CHECK:
		cam_trace("~ FACTORY_AF_MOVE_END_CHECK ~\n");
		err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
			0x29, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		break;

	case FACTORY_AF_SCAN_END_CHECK:
		cam_trace("~ FACTORY_AF_SCAN_END_CHECK ~\n");
		err = m9mo_readw(sd, M9MO_CATEGORY_LENS,
			0x20, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		break;

	default:
		cam_err("~ m9mo_set_factory_af ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_af_step(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
		0x1A, val);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_af_position(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0B, val);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_defocus(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_DEFOCUS_RUN:
		cam_trace("~ FACTORY_DEFOCUS_RUN ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
				0x0D, 0x18);
		CHECK_ERR(err);
		break;

	case FACTORY_DEFOCUS_STOP:
		cam_trace("~ FACTORY_DEFOCUS_STOP ~\n");
		break;

	default:
		cam_err("~ m9mo_set_factory_defocus ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_defocus_wide(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1A, val);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_defocus_tele(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, val);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_resol_cap(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_CAP_COMP_ON:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, 0x01);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_CAP_COMP_ON ~\n");
		break;

	case FACTORY_CAP_COMP_OFF:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, 0x00);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_CAP_COMP_OFF ~\n");
		break;

	case FACTORY_CAP_COMP_START:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0D, 0x04);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_CAP_COMP_START ~\n");
		break;

	case FACTORY_CAP_COMP_STOP:
		cam_trace("~ FACTORY_CAP_COMP_STOP ~\n");
		break;

	case FACTORY_CAP_BARREL_ON:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, 0x01);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_CAP_BARREL_ON ~\n");
		break;

	case FACTORY_CAP_BARREL_OFF:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, 0x00);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_CAP_BARREL_OFF ~\n");
		break;

	case FACTORY_CAP_BARREL_START:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0D, 0x05);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_CAP_BARREL_START ~\n");
		break;

	case FACTORY_CAP_BARREL_STOP:
		cam_trace("~ FACTORY_CAP_BARREL_STOP ~\n");
		break;

	default:
		cam_err("~ m9mo_set_factory_resol_cap ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_af_zone(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_AFZONE_NORMAL:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x07, 0x00);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_AFZONE_NORMAL ~\n");
		break;

	case FACTORY_AFZONE_MACRO:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x07, 0x01);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_AFZONE_MACRO ~\n");
		break;

	case FACTORY_AFZONE_AUTO:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x07, 0x02);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_AFZONE_AUTO ~\n");
		break;

	default:
		cam_err("~ m9mo_set_factory_resol_cap ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_af_lens(struct v4l2_subdev *sd, int val)
{
	int err;
	u32 int_factor;

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_AFLENS_OPEN:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x00, 0x00);
		CHECK_ERR(err);

		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);

		if (!(int_factor & M9MO_INT_LENS_INIT)) {
			cam_err("M9MO_INT_LENS_INIT isn't issued, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}


		cam_trace("~ FACTORY_AFLENS_OPEN ~\n");
		break;

	case FACTORY_AFLENS_CLOSE:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x01, 0x00);
		CHECK_ERR(err);
		cam_trace("~ FACTORY_AFLENS_CLOSE ~\n");
		break;

	default:
		cam_err("~ m9mo_set_factory_af_lens ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_adj_iris(struct v4l2_subdev *sd, int val)
{
	int err, end_check = 0;
	int int_factor;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_ADJ_IRIS_RUN:
		cam_trace("~ FACTORY_ADJ_IRIS_RUN ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x53, 0x01);
		CHECK_ERR(err);
		break;

	case FACTORY_ADJ_IRIS_STOP:
		cam_trace("~ FACTORY_ADJ_IRIS_STOP ~\n");
		break;

	case FACTORY_ADJ_IRIS_END_CHECK:
		err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
			0x40, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		cam_trace("FACTORY_ADJ_IRIS_END_CHECK=%d\n",
			end_check);

		if (end_check == 2) {
					/* Clear Interrupt factor */
			int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
			if (!(int_factor & M9MO_INT_CAPTURE)) {
				cam_warn("M9MO_INT_CAPTURE isn't issued, %#x\n",
					int_factor);
				return -ETIMEDOUT;
			}
		}
		break;

	case FACTORY_ADJ_IRIS_LOG:
		err = m9mo_make_CSV_rawdata(sd,
			M9MO_FLASH_FACTORY_IRIS, true);
		CHECK_ERR(err);
		break;

	default:
		cam_err("~ m9mo_set_factory_adj_iris ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_sh_close(struct v4l2_subdev *sd, int val)
{
	int err, end_check = 0;
	int int_factor;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_SH_CLOSE_RUN:
		cam_trace("~ FACTORY_SH_CLOSE_RUN ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x53, 0x05);
		CHECK_ERR(err);
		break;

	case FACTORY_SH_CLOSE_STOP:
		cam_trace("~ FACTORY_SH_CLOSE_STOP ~\n");
		break;

	case FACTORY_SH_CLOSE_END_CHECK:
		cam_trace("~ FACTORY_SH_CLOSE_END_CHECK ~\n");
		err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
			0x40, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		if (end_check == 2) {
			int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
			if (!(int_factor)) {
				cam_warn("M9MO_INT_MODE isn't issued, %#x\n",
					int_factor);
				return -ETIMEDOUT;
			}
		}
		break;

	case FACTORY_SH_CLOSE_LOG:
		cam_trace("~ FACTORY_SH_CLOSE_LOG ~\n");
		err = m9mo_make_CSV_rawdata(sd,
			M9MO_FLASH_FACTORY_SH_CLOSE, true);
		CHECK_ERR(err);

		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor)) {
			cam_warn("M9MO_INT_CAPTURE isn't issued, %#x\n",
				int_factor);
			return -ETIMEDOUT;
		}

		break;

	default:
		cam_err("~ m9mo_set_factory_adj_iris ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_adj_gain_liveview(struct v4l2_subdev *sd, int val)
{
	int err, end_check = 0;
	int int_factor;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_ADJ_GAIN_LIVEVIEW_RUN:
		cam_trace("~ FACTORY_ADJ_GAIN_LIVEVIEW_RUN ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x53, 0x03);
		CHECK_ERR(err);
		break;

	case FACTORY_ADJ_GAIN_LIVEVIEW_STOP:
		cam_trace("~ FACTORY_ADJ_GAIN_LIVEVIEW_STOP ~\n");
		break;

	case FACTORY_ADJ_GAIN_LIVEVIEW_END_CHECK:
		cam_trace("~ FACTORY_ADJ_GAIN_LIVEVIEW_END_CHECK ~\n");
		err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
			0x40, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = 0;

		if (end_check == 2) {
			state->factory_end_check = 4;
			/* Clear Interrupt factor */
			int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
			if (!(int_factor & M9MO_INT_CAPTURE)) {
				cam_warn("M9MO_INT_CAPTURE isn't issued, %#x\n",
					int_factor);
				return -ETIMEDOUT;
			}
		}
		break;

	case FACTORY_ADJ_GAIN_LIVEVIEW_LOG:
		cam_trace("~ FACTORY_ADJ_GAIN_LIVEVIEW_END_CHECK ~\n");
		err = m9mo_make_CSV_rawdata(sd,
			M9MO_FLASH_FACTORY_LIVEVIEW, true);
		CHECK_ERR(err);
		break;

	default:
		cam_err("~ m9mo_set_factory_adj_iris ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_flicker(struct v4l2_subdev *sd, int val)
{
	int err;
	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_FLICKER_AUTO:
		cam_trace("~ FACTORY_FLICKER_AUTO ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_FLICKER, 0x00);
		CHECK_ERR(err);
		break;

	case FACTORY_FLICKER_50HZ:
		cam_trace("~ FACTORY_FLICKER_50HZ ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_FLICKER, 0x01);
		CHECK_ERR(err);
		break;

	case FACTORY_FLICKER_60HZ:
		cam_trace("~ FACTORY_FLICKER_60HZ ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_FLICKER, 0x02);
		break;

	case FACTORY_FLICKER_50_60:
		cam_trace("~ FACTORY_FLICKER_50_60 ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_FLICKER, 0x03);
		break;

	case FACTORY_FLICKER_OFF:
		cam_trace("~ FACTORY_FLICKER_OFF ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_FLICKER, 0x04);
		CHECK_ERR(err);
		break;

	default:
		cam_err("~ m9mo_set_factory_adj_iris ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_capture_gain(struct v4l2_subdev *sd, int val)
{
	int err, end_check = 0;
	int int_factor;
	struct m9mo_state *state = to_state(sd);
	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_CAPTURE_GAIN_RUN:
		cam_trace("~ FACTORY_CAPTURE_GAIN_RUN ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x53, 0x07);
		CHECK_ERR(err);
		break;

	case FACTORY_CAPTURE_GAIN_STOP:
		cam_trace("~ FACTORY_CAPTURE_GAIN_STOP ~\n");
		break;

	case FACTORY_CAPTURE_GAIN_END_CHECK:
		cam_trace("~ FACTORY_CAPTURE_GAIN_END_CHECK ~\n");
		err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
			0x40, &end_check);
		CHECK_ERR(err);

		state->factory_end_check = 0;
		if (end_check == 2) {
			state->factory_end_check = 8;
					/* Clear Interrupt factor */
			int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
			if (!(int_factor & M9MO_INT_CAPTURE)) {
				cam_warn("M9MO_INT_CAPTURE isn't issued, %#x\n",
					int_factor);
				return -ETIMEDOUT;
			}
		}

		break;

	case FACTORY_CAPTURE_GAIN_LOG:
		err = m9mo_make_CSV_rawdata(sd,
			M9MO_FLASH_FACTORY_GAIN_CAPTURE, true);
		CHECK_ERR(err);
		cam_trace("~FACTORY_CAPTURE_GAIN_LOG ~\n");
		break;

	default:
		cam_err("~ m9mo_set_factory_capture_gain ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_image_stabilizer_mode(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err;
	int cnt = 30;
	s32 ois_stability = 1;
	cam_trace("E: mode %d\n", val);

retry:
	switch (val) {
	case V4L2_IMAGE_STABILIZER_OFF:
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x1A, 0x01);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x11, 0x01);
		CHECK_ERR(err);

		err = m9mo_readb(sd, M9MO_CATEGORY_NEW,
				0x1A, &ois_stability);
		CHECK_ERR(err);
		while (ois_stability && cnt) {
			msleep(20);
			err = m9mo_readb(sd, M9MO_CATEGORY_NEW,
					0x1A, &ois_stability);
			CHECK_ERR(err);
			cnt--;
		}
		break;

	case V4L2_IMAGE_STABILIZER_OIS:
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x1A, 0x01);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x11, 0x02);
		CHECK_ERR(err);

		err = m9mo_readb(sd, M9MO_CATEGORY_NEW,
				0x1A, &ois_stability);
		CHECK_ERR(err);
		while (ois_stability && cnt) {
			msleep(20);
			err = m9mo_readb(sd, M9MO_CATEGORY_NEW,
					0x1A, &ois_stability);
			CHECK_ERR(err);
			cnt--;
		}
		break;

	case V4L2_IMAGE_STABILIZER_DUALIS:
		/*break;*/

	default:
		val = V4L2_IMAGE_STABILIZER_OFF;
		goto retry;
		break;
	}

	state->image_stabilizer_mode = val;

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_capture_ctrl(struct v4l2_subdev *sd, int val)
{
	int err = 0;
	int int_factor;

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_STILL_CAP_NORMAL:
		cam_trace("~ FACTORY_STILL_CAP_NORMAL ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_CAP_MODE, 0x00);
		CHECK_ERR(err);
		break;

	case FACTORY_STILL_CAP_DUALCAP:
		cam_trace("~ FACTORY_STILL_CAP_DUALCAP ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_CAP_MODE, 0x05);
		CHECK_ERR(err);
		break;

	case FACTORY_DUAL_CAP_ON:
		cam_trace("~ FACTORY_DUAL_CAP_ON ~\n");
#if 0
		err = m9mo_readw(sd, M9MO_CATEGORY_SYS,
				M9MO_SYS_INT_EN, &int_factor);
		CHECK_ERR(err);
		int_factor &= ~M9MO_INT_FRAME_SYNC;
		err = m9mo_writew(sd, M9MO_CATEGORY_SYS,
				M9MO_SYS_INT_EN, int_factor);
		CHECK_ERR(err);
#endif

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_START_DUALCAP, 0x01);
		CHECK_ERR(err);
		break;

	case FACTORY_DUAL_CAP_OFF:
		cam_trace("~ FACTORY_DUAL_CAP_OFF ~\n");
#if 1
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor/* & M9MO_INT_SCENARIO_FIN*/)) {
			cam_warn(
				"M9MO_INT_SCENARIO_FIN isn't issued, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
#endif
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_START_DUALCAP, 0x02);
		CHECK_ERR(err);
#if 1
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor/* & M9MO_INT_SCENARIO_FIN*/)) {
			cam_warn(
				"M9MO_INT_SCENARIO_FIN isn't issued, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
#endif

		break;

	default:
		cam_err("~ m9mo_set_factory_capture_ctrl ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_flash(struct v4l2_subdev *sd, int val)
{
	int err, end_check = 0;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_FLASH_STROBE_CHECK_ON:
		cam_trace("~ FACTORY_FLASH_STROBE_CHECK_ON ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x70, 0x01);
		CHECK_ERR(err);
		break;

	case FACTORY_FLASH_STROBE_CHECK_OFF:
		cam_trace("~ FACTORY_FLASH_STROBE_CHECK_OFF ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x70, 0x00);
		CHECK_ERR(err);
		break;

	case FACTORY_FLASH_CHARGE:
		cam_trace("~ FACTORY_FLASH_CHARGE ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			0x2A, 0x01);
		CHECK_ERR(err);
		break;

	case FACTORY_FLASH_LOG:
		err = m9mo_make_CSV_rawdata(sd,
				M9MO_FLASH_FACTORY_FLASH_CHECK, false);
		cam_trace("~ FACTORY_FLASH_LOG ~\n");
		break;

	case FACTORY_FLASH_CHARGE_END_CHECK:
		cam_trace("~ FACTORY_FLASH_CHARGE_END_CHECK ~\n");
		err = m9mo_readb(sd, M9MO_CATEGORY_ADJST,
			0x40, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		break;

	case FACTORY_FLASH_STROBE_CHARGE_END_CHECK:
		cam_trace("~ FLASH_STROBE_CHARGE_END_CHECK ~\n");
		err = m9mo_readb(sd, M9MO_CATEGORY_CAPPARM,
			0x27, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		break;

	case FACTORY_ADJ_FLASH_WB_END_CHECK:
		cam_trace("~ ADJ_FLASH_WB_END_CHECK ~\n");
		err = m9mo_readb(sd, M9MO_CATEGORY_ADJST,
			0x14, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		break;

	case FACTORY_FLASH_WB_LOG:
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
			0x31, 0x01);
		msleep(40);
		err = m9mo_make_CSV_rawdata(sd,
				M9MO_FLASH_FACTORY_FLASH_WB, false);
		cam_trace("~ FACTORY_FLASH_WB_LOG ~\n");
		break;

	case FACTORY_ADJ_FLASH_WB_LOG:
		err = m9mo_make_CSV_rawdata(sd,
				M9MO_FLASH_FACTORY_ADJ_FLASH_WB, false);
		cam_trace("~ FACTORY_ADJ_FLASH_WB_LOG ~\n");
		break;

	default:
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_wb(struct v4l2_subdev *sd, int val)
{
	int err, end_check = 0;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_WB_INDOOR_RUN:
		cam_trace("~ FACTORY_WB_INDOOR_RUN ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x14, 0x01);
		CHECK_ERR(err);
		break;

	case FACTORY_WB_OUTDOOR_RUN:
		cam_trace("~ FACTORY_WB_OUTDOOR_RUN ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x14, 0x21);
		CHECK_ERR(err);
		break;

	case FACTORY_WB_INDOOR_END_CHECK:
	case FACTORY_WB_OUTDOOR_END_CHECK:
		cam_trace("~ FACTORY_WB_END_CHECK ~\n");
		err = m9mo_readw(sd, M9MO_CATEGORY_ADJST,
			0x14, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		break;

	case FACTORY_WB_LOG:
		err = m9mo_make_CSV_rawdata(sd,
				M9MO_FLASH_FACTORY_WB_ADJ, false);
		cam_trace("~ FACTORY_WB_LOG ~\n");
		break;

	default:
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_defectpixel(struct v4l2_subdev *sd, int val)
{
	int err;
	int int_factor;
	int end_check = 0;
	bool go_end = false;
#if 0
	int i;
#endif
	struct m9mo_state *state = to_state(sd);

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_DEFECTPIXEL_SCENARIO_6:
		cam_trace("~ FACTORY_DEFECTPIXEL_SCENARIO_6 ~\n");

		/*Interrupt Enable*/
		err = m9mo_readw(sd, M9MO_CATEGORY_SYS,
			M9MO_SYS_INT_EN, &int_factor);
		CHECK_ERR(err);
		int_factor |= M9MO_INT_SCENARIO_FIN;
		err = m9mo_writew(sd, M9MO_CATEGORY_SYS,
				M9MO_SYS_INT_EN, int_factor);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x40, 0x00);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			0x5C, 0x06);
		CHECK_ERR(err);
		break;

	case FACTORY_DEFECTPIXEL_RUN:
		cam_trace("~ FACTORY_DEFECTPIXEL_RUN ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x40, 0x00);
		CHECK_ERR(err);
		state->factory_end_interrupt = 0x0;
		/*Interrrupt Disable*/
#if 0
		err = m9mo_readw(sd, M9MO_CATEGORY_SYS,
				M9MO_SYS_INT_EN, &int_factor);
		CHECK_ERR(err);
		int_factor &= ~M9MO_INT_STNW_DETECT;
		int_factor &= ~M9MO_INT_SCENARIO_FIN;
		err = m9mo_writew(sd, M9MO_CATEGORY_SYS,
				M9MO_SYS_INT_EN, int_factor);
		CHECK_ERR(err);
#endif

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			0x5C, 0x07);
		CHECK_ERR(err);
#if 0
		for (i = 0; i < 5; i++) {
			int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
			if (!(int_factor & M9MO_INT_STNW_DETECT)) {
				cam_warn(
					"M9MO_INT_STNW_DETECT isn't issued, %#x\n",
						int_factor);
				return -ETIMEDOUT;
			}
		}

		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_SCENARIO_FIN)) {
			cam_warn(
				"M9MO_INT_SCENARIO_FIN isn't issued, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
#endif
		break;

	case FACTORY_DEFECTPIXEL_END_CHECK:
		cam_trace("~ FACTORY_DEFECTPIXEL_END_CHECK ~\n");
		err = m9mo_readb(sd, M9MO_CATEGORY_LENS,
			0x40, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
#if 0
		if (0) { /*end_check != 0) {*/
			msleep(100);
			int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
			if (!(int_factor & M9MO_INT_SCENARIO_FIN)) {
				cam_warn(
					"M9MO_INT_SCENARIO_FIN isn't issued, %#x\n",
						int_factor);
				return -ETIMEDOUT;
			}
		}
#endif
		if (state->factory_end_interrupt == 0x4)
			go_end = true;

		m9mo_readw(sd, M9MO_CATEGORY_SYS,
			M9MO_SYS_INT_FACTOR, &state->isp.int_factor);
		cam_err("m9mo_wait_interrupt : state->isp.int_factor = %x\n",
						state->isp.int_factor);
		if (state->isp.int_factor != 0x00)
			state->factory_end_interrupt = state->isp.int_factor;

		if ((go_end == true) && (state->isp.int_factor == 0x02))
			state->factory_end_check = 0x02;

		cam_trace("X\n");
		break;

	case FACTORY_DEFECTPIXEL_CID_WRITE:
		cam_trace("~ FACTORY_DEFECTPIXEL_CID_WRITE ~\n");
		m9mo_writeb(sd, M9MO_CATEGORY_SYS,
			0x29, 0x01);
		cam_trace("X\n");
		break;

	case FACTORY_DEFECTPIXEL_CID_1:
		cam_trace("~ FACTORY_DEFECTPIXEL_CID_1 ~\n");
		m9mo_readw(sd, M9MO_CATEGORY_SYS,
			0x2A, &end_check);
		cam_err("CID_1 : %x\n", end_check);
		state->factory_end_check = end_check;
		cam_trace("X\n");
		break;

	case FACTORY_DEFECTPIXEL_CID_2:
		cam_trace("~ FACTORY_DEFECTPIXEL_CID_2 ~\n");
		m9mo_readw(sd, M9MO_CATEGORY_SYS,
			0x2C, &end_check);
		cam_err("CID_2 : %x\n", end_check);
		state->factory_end_check = end_check;
		cam_trace("X\n");
		break;

	case FACTORY_DEFECTPIXEL_CID_3:
		cam_trace("~ FACTORY_DEFECTPIXEL_CID_3 ~\n");
		m9mo_readw(sd, M9MO_CATEGORY_SYS,
			0x2E, &end_check);
		cam_err("CID_3 : %x\n", end_check);
		state->factory_end_check = end_check;
		cam_trace("X\n");
		break;

	case FACTORY_DEFECTPIXEL_LOG:
		cam_trace("~ FACTORY_DEFECTPIXEL_LOG ~\n");

#if 0
		msleep(300);
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_SCENARIO_FIN)) {
			cam_warn(
				"M9MO_INT_SCENARIO_FIN isn't issued, %#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
#endif
		m9mo_make_CSV_rawdata_direct(sd,
			V4L2_CID_CAMERA_FACTORY_DEFECTPIXEL);

			err = m9mo_writew(sd, M9MO_CATEGORY_SYS,
				M9MO_SYS_INT_EN,
				M9MO_INT_MODE | M9MO_INT_CAPTURE |
				M9MO_INT_FRAME_SYNC | M9MO_INT_ATSCENE_UPDATE |
				M9MO_INT_LENS_INIT | M9MO_INT_SOUND);
			CHECK_ERR(err);

		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		cam_trace("~ Event clear ~\n");
		break;

	case FACTORY_DEFECTPIXEL_DOT_WRITE_CHECK:
	case FACTORY_DEFECTPIXEL_FLASH_MERGE:
		err = m9mo_readb(sd, M9MO_CATEGORY_ADJST,
			0x90, &end_check);
		CHECK_ERR(err);
		cam_trace("DOT DATA END CHECK : %d\n", end_check);
		state->factory_end_check = end_check;
		break;

	default:
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_tilt(struct v4l2_subdev *sd, int val)
{
	int err, end_check = 0;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_TILT_ONE_SCRIPT_RUN:
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0C, 0x06);
		CHECK_ERR(err);
		cam_trace("FACTORY_TILT_ONE_SCRIPT_RUN\n");
		break;

	case FACTORY_TILT_ONE_SCRIPT_DISP1:
		err = m9mo_readw(sd, M9MO_CATEGORY_LENS,
			0x34, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		cam_trace("FACTORY_TILT_ONE_SCRIPT_DISP1 %d\n", end_check);
		break;

	case FACTORY_TILT_ONE_SCRIPT_DISP2:
		err = m9mo_readw(sd, M9MO_CATEGORY_LENS,
			0x36, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		cam_trace("FACTORY_TILT_ONE_SCRIPT_DISP2 %d\n", end_check);
		break;

	case FACTORY_TILT_ONE_SCRIPT_DISP3:
		err = m9mo_readw(sd, M9MO_CATEGORY_LENS,
			0x38, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		cam_trace("FACTORY_TILT_ONE_SCRIPT_DISP3 %d\n", end_check);
		break;

	case FACTORY_TILT_ONE_SCRIPT_DISP4:
		err = m9mo_readw(sd, M9MO_CATEGORY_LENS,
			0x3A, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		cam_trace("FACTORY_TILT_ONE_SCRIPT_DISP4 %d\n", end_check);
		break;

	case FACTORY_TILT_ONE_SCRIPT_DISP5:
		err = m9mo_readw(sd, M9MO_CATEGORY_LENS,
			0x3C, &end_check);
		CHECK_ERR(err);
		state->factory_end_check = end_check;
		cam_trace("FACTORY_TILT_ONE_SCRIPT_DISP5 %d\n", end_check);
		break;

	default:
		cam_err("~ m9mo_set_factory_common ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_factory_IR_Check(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_trace("E val : %d\n", val);

	switch (val) {
	case FACTORY_IR_CHECK_LOG:
		cam_trace("~ FACTORY_IR_CHECK_LOG ~\n");
		msleep(40);
		err = m9mo_make_CSV_rawdata(sd,
			M9MO_FLASH_FACTORY_IR_CHECK, false);
		CHECK_ERR(err);
		break;

	default:
		cam_err("~ m9mo_set_factory_common ~ val : %d", val);
		break;
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_send_factory_command_value(struct v4l2_subdev *sd)
{
	int err;
	int category = 0, byte = 0, value = 0, size = 0;
	struct m9mo_state *state = to_state(sd);

	category = state->factory_category;
	byte = state->factory_byte;
	value = state->factory_value;
	size = state->factory_value_size;

	cam_trace("category : 0x%x, byte : 0x%x, value : 0x%x\n",
		category, byte, value);

	if ((size == 4) || (value > 0xFFFF)) {
		cam_trace("write long");
		err = m9mo_writel(sd, category, byte, value);
		CHECK_ERR(err);
		return err;
	}

	if ((size == 2) || (value > 0xFF)) {
		cam_trace("write word");
		err = m9mo_writew(sd, category, byte, value);
		CHECK_ERR(err);
		return err;
	}

	cam_trace("write byte");
	err = m9mo_writeb(sd, category, byte, value);
	CHECK_ERR(err);
	return err;
}

static int m9mo_set_aeawblock(struct v4l2_subdev *sd, int val)
{
	int err;

	cam_err("%d\n", val);
	switch (val) {
	case AE_UNLOCK_AWB_UNLOCK:
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AE_LOCK, 0);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB, M9MO_AWB_LOCK, 0);
		CHECK_ERR(err);
		break;

	case AE_LOCK_AWB_UNLOCK:
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AE_LOCK, 1);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB, M9MO_AWB_LOCK, 0);
		CHECK_ERR(err);
		break;

	case AE_UNLOCK_AWB_LOCK:
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AE_LOCK, 0);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB, M9MO_AWB_LOCK, 1);
		CHECK_ERR(err);
		break;

	case AE_LOCK_AWB_LOCK:
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE, M9MO_AE_LOCK, 1);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB, M9MO_AWB_LOCK, 1);
		CHECK_ERR(err);
		break;
	}
	cam_err("X\n");
	return 0;
}

static int m9mo_set_GBAM(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err = 0;

	if (state->wb_g_value == 0 && state->wb_b_value == 0
		&& state->wb_a_value == 0 && state->wb_m_value == 0)
		val = 0;

	cam_trace("E, val = %d\n", val);

	if (val) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_GBAM_MODE, 0x01);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_G_VALUE, state->wb_g_value);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_B_VALUE, state->wb_b_value);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_A_VALUE, state->wb_a_value);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_M_VALUE, state->wb_m_value);
		CHECK_ERR(err);
	} else {
		err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
				M9MO_WB_GBAM_MODE, 0x00);
		CHECK_ERR(err);
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_K(struct v4l2_subdev *sd, int val)
{
	int err = 0;

	cam_trace("E %02X\n", val);

	err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
		M9MO_WB_K_VALUE, val);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_dual_capture_mode(struct v4l2_subdev *sd, int val)
{
	int err = 0;
	int old_mode;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E, val = %d\n", val);

	if (val == state->vss_mode) {
		cam_err("same vss_mode: %d\n", state->vss_mode);
		return err;
	}

	old_mode = m9mo_set_mode(sd, M9MO_PARMSET_MODE);
	if (old_mode <= 0) {
		cam_err("failed to set mode\n");
		return old_mode;
	}

	switch (val) {
	case 0:
		/* Normal Video Snap Shot */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
				M9MO_PARM_VSS_MODE, 0x00);
		CHECK_ERR(err);
		break;

	case 1:
		/* 4M Video Snap Shot */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
				M9MO_PARM_VSS_MODE, 0x01);
		CHECK_ERR(err);
		break;
	default:
		val = 0;
		break;
	}

	state->vss_mode = val;

	cam_trace("X\n");
	return err;
}

static int m9mo_start_set_dual_capture(struct v4l2_subdev *sd, int frame_num)
{
	struct m9mo_state *state = to_state(sd);
	int err, int_factor;

	cam_trace("E, vss mode: %d, frm[%d]\n", state->vss_mode, frame_num);

	if (!state->vss_mode)
		return 0;

	/* Start video snap shot */
	err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
		M9MO_MON_START_VIDEO_SNAP_SHOT, 0x01);
	CHECK_ERR(err);

	/* Clear Interrupt factor */
	int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
	if (!(int_factor & M9MO_INT_FRAME_SYNC)) {
		cam_err("M9MO_INT_FRAME_SYNC isn't issued, %#x\n", int_factor);
		return -ETIMEDOUT;
	}

	state->dual_capture_start = 1;
	state->dual_capture_frame = frame_num;

	cam_trace("X\n");
	return err;
}

static int m9mo_set_smart_moving_recording(struct v4l2_subdev *sd, int val)
{
	int err = 0, read_mon_size;
	u32 size_val = 0, value;
	struct m9mo_state *state = to_state(sd);

	cam_dbg("E val=%d\n", val);

	/* add recording check for zoom move */
	if (val == 1) {  /* recording start */
		err = m9mo_set_mode(sd, M9MO_PARMSET_MODE);
		CHECK_ERR(err);

		if (state->sensor_mode == SENSOR_MOVIE && state->fps == 30) {
			err = m9mo_readb(sd, M9MO_CATEGORY_PARM,
				M9MO_PARM_MON_SIZE, &read_mon_size);
			CHECK_ERR(err);

			if (state->preview->height == 1080)
				size_val = 0x2C;
			else if (state->preview->height == 720)
				size_val = 0x2D;
			else if (state->preview->width == 640
				&& state->preview->height == 480)
				size_val = 0x2E;
			else if (state->preview->width == 320
				&& state->preview->height == 240)
				size_val = 0x36;

			if (read_mon_size != size_val) {
				err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
					M9MO_PARM_MON_SIZE, size_val);
				CHECK_ERR(err);
			}

			err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
					M9MO_PARM_VSS_MODE, 0x01);

			err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
				M9MO_ADJST_SHUTTER_MODE, 0);
			CHECK_ERR(err);

			state->vss_mode = 1;
		}

		err = m9mo_readb(sd, M9MO_CATEGORY_PARM,
			M9MO_PARM_MON_MOVIE_SELECT, &value);

		if (value != 0x1) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
				M9MO_PARM_MON_MOVIE_SELECT, 0x1);
			CHECK_ERR(err);
		}

		err = m9mo_set_mode(sd, M9MO_MONITOR_MODE);
		CHECK_ERR(err);

		err = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(err & M9MO_INT_MODE)) {
			cam_err("M9MO_INT_MODE isn't issued!!!\n");
			return -ETIMEDOUT;
		}

		if (state->smart_scene_detect_mode == 1) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
				0x0A, 0x02);
			CHECK_ERR(err);
		}

		state->recording = 1;
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS, 0x27, 0x01);
		CHECK_ERR(err);
	} else if (val == 2) {  /* record end */
		if (state->smart_scene_detect_mode == 1) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
				0x0A, 0x01);
			CHECK_ERR(err);

			m9mo_set_smart_auto_default_value(sd, 1);
		}

		state->recording = 0;
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS, 0x27, 0x00);
		CHECK_ERR(err);

		err = m9mo_readb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_VIDEO_SNAP_SHOT_FRAME_COUNT, &value);

		if (value == 0)  {
			err = m9mo_set_mode(sd, M9MO_PARMSET_MODE);
			CHECK_ERR(err);

			if (state->vss_mode) {
				cam_dbg(" movimode disable");

			err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
				M9MO_ADJST_SHUTTER_MODE, 1);
			CHECK_ERR(err);

			if (state->preview_height == 1080)
				size_val = 0x28;
			else if (state->preview_height == 720)
				size_val = 0x21;
			else if (state->preview_height == 480)
				size_val = 0x17;
			else if (state->preview_height == 240)
				size_val = 0x09;

			err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
				M9MO_PARM_MON_SIZE, size_val);
			CHECK_ERR(err);

			err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
					M9MO_PARM_VSS_MODE, 0x00);
			CHECK_ERR(err);

			state->vss_mode = 0;
		}

		err = m9mo_readb(sd, M9MO_CATEGORY_PARM,
			M9MO_PARM_MON_MOVIE_SELECT, &value);

		if (value != 0x0) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
				M9MO_PARM_MON_MOVIE_SELECT, 0x0);
			CHECK_ERR(err);
		}

			err = m9mo_set_mode(sd, M9MO_MONITOR_MODE);
			CHECK_ERR(err);

			err = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
			if (!(err & M9MO_INT_MODE)) {
				cam_err("M9MO_INT_MODE isn't issued!!!\n");
				return -ETIMEDOUT;
			}
		}
	} else {
		if (state->vss_mode) {
			cam_dbg(" movimode disable");

			err = m9mo_set_mode(sd, M9MO_PARMSET_MODE);
			CHECK_ERR(err);

			err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
				M9MO_ADJST_SHUTTER_MODE, 1);
			CHECK_ERR(err);

			if (state->preview_height == 1080)
				size_val = 0x28;
			else if (state->preview_height == 720)
				size_val = 0x21;
			else if (state->preview_height == 480)
				size_val = 0x17;
			else if (state->preview_height == 240)
				size_val = 0x09;

			err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
				M9MO_PARM_MON_SIZE, size_val);
			CHECK_ERR(err);

			err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
					M9MO_PARM_VSS_MODE, 0x00);
			state->vss_mode = 0;

			err = m9mo_readb(sd, M9MO_CATEGORY_PARM,
				M9MO_PARM_MON_MOVIE_SELECT, &value);

			if (value != 0x0) {
				err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
					M9MO_PARM_MON_MOVIE_SELECT, 0x0);
				CHECK_ERR(err);
			}
		}
	}

	m9mo_set_OIS_cap_mode(sd);

	return err;
}

static int m9mo_continue_proc(struct v4l2_subdev *sd, int val)
{
	int err = 1, int_factor;

	cam_trace("E\n");

	switch (val) {
	case V4L2_INT_STATE_FRAME_SYNC:
		int_factor = m9mo_wait_interrupt(sd, M9MO_SOUND_TIMEOUT);
			if (!(int_factor & M9MO_INT_SOUND)) {
				cam_dbg("m9mo_continue_proc() INT_FRAME_SOUND error%#x\n",
						int_factor);
				return -ETIMEDOUT;
			}
		break;

	case V4L2_INT_STATE_CAPTURE_SYNC:
		/* continue : cancel CAPTURE or postview end CAPTURE*/
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_CAPTURE_TIMEOUT);
		if (!(int_factor & M9MO_INT_CAPTURE)) {
			cam_dbg("m9mo_continue_proc() INT_STATE_CAPTURE_SYNC error%#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
		break;

	case V4L2_INT_STATE_CONTINUE_CANCEL:
		/* continue cancel */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
				M9MO_CAPCTRL_START_DUALCAP, 0x02);
			CHECK_ERR(err);
		cam_dbg("-------------V4L2_INT_STATE_CONTINUE_CANCEL-------------\n");
		/* CAPTURE wait interrupt -> V4L2_INT_STATE_CAPTURE_SYNC */
		break;

	case V4L2_INT_STATE_CONTINUE_END:
		err = m9mo_set_mode(sd, M9MO_MONITOR_MODE);
		if (err <= 0) {
			cam_err("failed to set mode\n");
			return err;
		}

		err = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(err & M9MO_INT_MODE)) {
			cam_err("m9mo_continue_proc() INT_STATE_CONTINUE_END error\n");
			return -ETIMEDOUT;
		}
		break;

	case V4L2_INT_STATE_START_CAPTURE:
		err = m9mo_set_mode(sd, M9MO_STILLCAP_MODE);
		if (err <= 0) {
			cam_err("failed to set mode\n");
			return err;
		}
		break;
	}

	cam_dbg("m9mo_continue_proc : 0x%x  err=%d\n",
		val, err);

	cam_trace("X\n");
	return err;
}

static int m9mo_burst_set_postview_size(struct v4l2_subdev *sd, int val)
{
	int err = 1, i, num_entries;
	struct m9mo_state *state = to_state(sd);
	const struct m9mo_frmsizeenum **frmsize;

	int width = val >> 16;
	int height = val & 0xFFFF;

	cam_trace("E\n");
	cam_trace("size = (%d x %d)\n", width, height);

	frmsize = &state->postview;

	num_entries = ARRAY_SIZE(postview_frmsizes);
	*frmsize = &postview_frmsizes[num_entries-1];

	for (i = 0; i < num_entries; i++) {
		if (width == postview_frmsizes[i].width &&
			height == postview_frmsizes[i].height) {
			*frmsize = &postview_frmsizes[i];
			break;
		}
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_PREVIEW_IMG_SIZE,
			state->postview->reg_val);

	cam_trace("X\n");

	return err;
}

static int m9mo_burst_set_snapshot_size(struct v4l2_subdev *sd, int val)
{
	int err = 1, i, num_entries;
	struct m9mo_state *state = to_state(sd);
	const struct m9mo_frmsizeenum **frmsize;

	int width = val >> 16;
	int height = val & 0xFFFF;

	cam_trace("E\n");
	cam_trace("size = (%d x %d)\n", width, height);

	frmsize = &state->capture;

	num_entries = ARRAY_SIZE(capture_frmsizes);
	*frmsize = &capture_frmsizes[num_entries-1];

	for (i = 0; i < num_entries; i++) {
		if (width == capture_frmsizes[i].width &&
			height == capture_frmsizes[i].height) {
			*frmsize = &capture_frmsizes[i];
			break;
		}
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_MAIN_IMG_SIZE,
			state->capture->reg_val);

	cam_trace("X\n");

	return err;
}

static int m9mo_burst_proc(struct v4l2_subdev *sd, int val)
{
	int err = 1, int_factor;
	struct m9mo_state *state = to_state(sd);

	cam_trace("E\n");

	switch (val) {
	case V4L2_INT_STATE_BURST_START:
		cam_trace("Burstshot  Capture  START ~~~~~~\n");

		state->mburst_start = true;

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
				0x0F, 0x0);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
				0x10, 0x30);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
				0x11, 0x0);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
				M9MO_CAPPARM_YUVOUT_MAIN, 0x01);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
				0x12, 0x0);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_START_DUALCAP,
			M9MO_CAP_MODE_MULTI_CAPTURE);
		CHECK_ERR(err);

		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_SOUND)) {
			cam_dbg("m9mo_continue_proc() INT_FRAME_SYNC error%#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
		break;

	case V4L2_INT_STATE_BURST_SYNC:
		cam_trace("Burstshot  Page SYNC~~~\n");
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_SOUND)) {
			cam_dbg("m9mo_continue_proc() INT_FRAME_SYNC error%#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
		break;

	case V4L2_INT_STATE_BURST_SOUND:
		cam_trace("Burstshot  Page SOUND~~~\n");
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_SOUND)) {
			cam_dbg("m9mo_continue_proc() INT_FRAME_SOUND error%#x\n",
					int_factor);
			return -ETIMEDOUT;
		}
		break;

	case V4L2_INT_STATE_BURST_STOP_REQ:
		/* continue cancel */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_START_DUALCAP, 0x03);
		CHECK_ERR(err);

		cam_trace("Burstshot  Capture  Shot Stop ~~~~~~\n");
		break;


	case V4L2_INT_STATE_BURST_STOP:
		state->mburst_start = false;

		err = m9mo_writel(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_JPEG_SIZE_MAX, 0x00A00000);
		CHECK_ERR(err);

		/* continue cancel */
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
			M9MO_CAPCTRL_START_DUALCAP, 0x02);
		CHECK_ERR(err);

		/* CAPTURE wait interrupt -> V4L2_INT_STATE_CAPTURE_SYNC */
		err = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(err & M9MO_INT_MODE)) {
			cam_err("m9mo_burst_proc() INT_STATE_CONTINUE_END error\n");
			return -ETIMEDOUT;
		}

		cam_trace("Burstshot  Capture  STOP ~~~~~~\n");
		break;
	}

	cam_trace("X\n");
	return err;
}

static int m9mo_set_iqgrp(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err, current_val, current_mode;
	u32 iqgrp_val = 0x01;

	cam_trace("E\n");

	err = m9mo_readb(sd, M9MO_CATEGORY_SYS, M9MO_SYS_MODE, &current_mode);

	if (current_mode != M9MO_PARMSET_MODE) {
		cam_trace("~ return !!! %d\n", current_mode);
		return 0;
	}

	if (state->fps == 60) {
		if (state->preview_height == 480)
			iqgrp_val = 0x68;
		else if (state->preview_height == 720)
			iqgrp_val = 0x65;
	} else if (state->sensor_mode == SENSOR_MOVIE
		&& state->fps == 30) {
		if (state->preview_height == 1080)
			iqgrp_val = 0x64;
		else if (state->preview_height == 720)
			iqgrp_val = 0x66;
		else if (state->preview_height == 480)
			iqgrp_val = 0x69;
		else if (state->preview_height == 240)
			iqgrp_val = 0x69;
	} else {
		if (state->preview_width == 768)
			iqgrp_val = 0x67;
		else
			iqgrp_val = 0x01;
	}

	if (val == 1080)
		iqgrp_val = 0x64;

	err = m9mo_readb(sd, M9MO_CATEGORY_MON,
		0x59, &current_val);
	CHECK_ERR(err);

	if (current_val != iqgrp_val) {
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			0x59, iqgrp_val);
		CHECK_ERR(err);
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_gamma(struct v4l2_subdev *sd)
{
	struct m9mo_state *state = to_state(sd);
	int err = 0;
	int cap_gamma, gamma_rgb_cap;
	int current_mode;

	cam_trace("E, mode %d\n", state->mode);

	err = m9mo_readb(sd, M9MO_CATEGORY_SYS, M9MO_SYS_MODE, &current_mode);

	if (current_mode != M9MO_PARMSET_MODE) {
		cam_trace("~ return !!! %d\n", current_mode);
		return 0;
	}

	/* Set Gamma value */
	err = m9mo_readb(sd, M9MO_CATEGORY_CAPPARM, 0x41, &cap_gamma);
	CHECK_ERR(err);
	err = m9mo_readb(sd, M9MO_CATEGORY_CAPPARM, 0x42, &gamma_rgb_cap);
	CHECK_ERR(err);

	if (gamma_rgb_cap < 0xD) {
		state->gamma_rgb_cap = cap_gamma;
		state->gamma_tbl_rgb_cap = gamma_rgb_cap;
	}

	if (state->mode == MODE_SILHOUETTE) {
		if (cap_gamma != 0x00) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
				0x41, 0x00);
			CHECK_ERR(err);
		}
		if (gamma_rgb_cap != 0x0D + state->widget_mode_level) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
				0x42, 0x0D + state->widget_mode_level);
			CHECK_ERR(err);
		}
	} else {
		if (cap_gamma != state->gamma_rgb_cap) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
				0x41, state->gamma_rgb_cap);
			CHECK_ERR(err);
		}
		if (gamma_rgb_cap != state->gamma_tbl_rgb_cap) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
				0x42, state->gamma_tbl_rgb_cap);
			CHECK_ERR(err);
		}
	}

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_PASM_mode(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err = 0;
	int color_effect, current_mode;
	int denominator = 500, numerator = 8;
	u32 f_number = 0x45;

	cam_dbg("E, value %d\n", val);

	state->mode = val;

	err = m9mo_readb(sd, M9MO_CATEGORY_SYS, M9MO_SYS_MODE, &current_mode);

	switch (val) {
	case MODE_SMART_AUTO:
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Disable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x00);
		CHECK_ERR(err);

		/* Set Still Mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x01);
		CHECK_ERR(err);

		/* Set HISTOGRAM ON */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x01);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x05);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x05);
		CHECK_ERR(err);

		/* SMART AUTO CAP */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_MODE_CAP, 0x10);
		CHECK_ERR(err);

		/* Set AF range to AUTO-MACRO */
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_AF_SCAN_RANGE, 0x02);
			CHECK_ERR(err);
#if 0
		/* Lens boot */
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
				M9MO_LENS_AF_INITIAL, 0x04);
		CHECK_ERR(err);
#endif
		break;

	case MODE_PANORAMA:
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Disable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x00);
		CHECK_ERR(err);

		/* Set CATE_408 to None */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM OFF */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x00);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x10);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x10);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		if (state->iso == 0) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EV_PRG_MODE_CAP, 0x00);
			CHECK_ERR(err);
		} else {
			err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EV_PRG_MODE_CAP, 0x04);
			CHECK_ERR(err);
		}
		break;

	case MODE_PROGRAM:
	case MODE_BEST_GROUP_POSE:
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Disable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x00);
		CHECK_ERR(err);

		/* Set CATE_408 to None */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM OFF */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x00);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x00);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x00);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		if (state->iso == 0) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EV_PRG_MODE_CAP, 0x00);
			CHECK_ERR(err);
		} else {
			err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EV_PRG_MODE_CAP, 0x04);
			CHECK_ERR(err);
		}
		break;

	case MODE_A:
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Disable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x00);
		CHECK_ERR(err);

		/* Set CATE_408 to None */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM OFF */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x00);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x02);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x02);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		if (state->iso == 0) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EV_PRG_MODE_CAP, 0x01);
			CHECK_ERR(err);
		} else {
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EV_PRG_MODE_CAP, 0x05);
		CHECK_ERR(err);
		}

		/* Set Still Capture F-Number Value */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_F_NUMBER, state->f_number);
		CHECK_ERR(err);
		break;

	case MODE_S:
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Disable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x00);
		CHECK_ERR(err);

		/* Set CATE_408 to None */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM OFF */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x00);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x04);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x04);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		if (state->iso == 0) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EV_PRG_MODE_CAP, 0x02);
			CHECK_ERR(err);
		} else {
			err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
				M9MO_AE_EV_PRG_MODE_CAP, 0x06);
			CHECK_ERR(err);
		}

		/* Set Capture Shutter Speed Time */
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_NUMERATOR, state->numerator);
		CHECK_ERR(err);
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_DENOMINATOR, state->denominator);
		CHECK_ERR(err);
		break;

	case MODE_M:
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Disable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x00);
		CHECK_ERR(err);

		/* Set CATE_408 to None */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM OFF */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x00);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x00);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x00);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_MODE_CAP, 0x07);
		CHECK_ERR(err);

		/* Set Still Capture F-Number Value */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_F_NUMBER, state->f_number);
		CHECK_ERR(err);

		/* Set Capture Shutter Speed Time */
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_NUMERATOR, state->numerator);
		CHECK_ERR(err);
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_DENOMINATOR, state->denominator);
		CHECK_ERR(err);

		/* Set Still Capture ISO Value */
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_ISO_VALUE, state->iso);
		CHECK_ERR(err);
		break;

	case MODE_VIDEO:
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);

		if (state->smart_scene_detect_mode) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x02);
			CHECK_ERR(err);
		} else {
			err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
			CHECK_ERR(err);
		}

		/* Set LIKE_PRO_EN Disable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM OFF */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x00);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_MODE_CAP, 0x00);
		CHECK_ERR(err);

		/* Set AF range to AUTO-MACRO */
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_AF_SCAN_RANGE, 0x02);
		CHECK_ERR(err);
#if 0
		/* Lens boot */
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
				M9MO_LENS_AF_INITIAL, 0x04);
		CHECK_ERR(err);
#endif
		break;

	case MODE_HIGH_SPEED:
		/* Set CATE_408 to None */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM ON */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x01);
		CHECK_ERR(err);

		/* LIKE A PRO MODE SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x01, 0x01);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_MODE_CAP, 0x02);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x04);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x11);
		CHECK_ERR(err);

		/* LIKE A PRO STEP SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE,
			0x02, state->widget_mode_level);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);

		/* Set Capture Shutter Speed Time */
		if (state->widget_mode_level == 0)
			denominator = 100;
		else if (state->widget_mode_level == 2)
			denominator = 125;
		else if (state->widget_mode_level == 4)
			denominator = 2000;

		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_NUMERATOR, 1);
		CHECK_ERR(err);
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_DENOMINATOR, denominator);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Enable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x01);
		CHECK_ERR(err);

		/* Set CATE_409 to 1(PREVIEW) */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x09, 0x01);
		CHECK_ERR(err);
		break;

	case MODE_LIGHT_TRAIL_SHOT:
		/* Set CATE_408 to None */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM ON */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x01);
		CHECK_ERR(err);

		/* LIKE A PRO MODE SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x01, 0x02);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_MODE_CAP, 0x06);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x04);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x04);
		CHECK_ERR(err);

		/* LIKE A PRO STEP SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE,
			0x02, state->widget_mode_level);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);

		/* Set Capture Shutter Speed Time */
		if (state->widget_mode_level == 0)
			numerator = 3;
		else if (state->widget_mode_level == 2)
			numerator = 5;
		else if (state->widget_mode_level == 4)
			numerator = 10;

		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_NUMERATOR, numerator);
		CHECK_ERR(err);
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_DENOMINATOR, 1);
		CHECK_ERR(err);

		/* Set Still Capture ISO Value */
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_ISO_VALUE, 0x64);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Enable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x01);
		CHECK_ERR(err);

		/* Set CATE_409 to 1(PREVIEW) */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x09, 0x01);
		CHECK_ERR(err);
		break;

	case MODE_WATERFALL:
		/* Set CATE_408 to None */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM ON */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x01);
		CHECK_ERR(err);

		/* LIKE A PRO MODE SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x01, 0x03);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_MODE_CAP, 0x00);
		CHECK_ERR(err);

		/* LIKE A PRO STEP SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE,
			0x02, state->widget_mode_level);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x00);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x0E);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Enable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x01);
		CHECK_ERR(err);

		/* Set CATE_409 to 1(PREVIEW) */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x09, 0x01);
		CHECK_ERR(err);
		break;

	case MODE_SILHOUETTE:
		/* Set CATE_408 to None */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM ON */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x01);
		CHECK_ERR(err);

		/* LIKE A PRO MODE SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x01, 0x04);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x00);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x00);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_MODE_CAP, 0x00);
		CHECK_ERR(err);

		/* LIKE A PRO STEP SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE,
			0x02, state->widget_mode_level);
		CHECK_ERR(err);

		/* Set Color effect */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Enable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x01);
		CHECK_ERR(err);

		/* Set CATE_409 to 1(PREVIEW) */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x09, 0x01);
		CHECK_ERR(err);
		break;

	case MODE_SUNSET:
		/* Set CATE_408 to None */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM ON */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x01);
		CHECK_ERR(err);

		/* LIKE A PRO MODE SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x01, 0x05);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_MODE_CAP, 0x00);
		CHECK_ERR(err);

		/* LIKE A PRO STEP SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE,
			0x02, state->widget_mode_level);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x00);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x00);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Enable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x01);
		CHECK_ERR(err);

		/* Set CATE_409 to 1(PREVIEW) */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x09, 0x01);
		CHECK_ERR(err);
		break;

	case MODE_CLOSE_UP:
		/* Set CATE_408 to None */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM ON */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x01);
		CHECK_ERR(err);

		/* LIKE A PRO MODE SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x01, 0x06);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_MODE_CAP, 0x01);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x02);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x02);
		CHECK_ERR(err);

		/* LIKE A PRO STEP SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE,
			0x02, state->widget_mode_level);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);

		/* Set Still Capture F-Number Value */
		if (state->widget_mode_level == 0)
			f_number = 0x80;
		else if (state->widget_mode_level == 2)
			f_number = 0x45;
		else if (state->widget_mode_level == 4)
			f_number = 0x28;

		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_F_NUMBER, f_number);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Enable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x01);
		CHECK_ERR(err);

		/* Set CATE_409 to 1(PREVIEW) */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x09, 0x01);
		CHECK_ERR(err);

		/* Set AF range to MACRO */
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_AF_SCAN_RANGE, 0x01);
		CHECK_ERR(err);
#if 0
		/* Lens boot */
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
				M9MO_LENS_AF_INITIAL, 0x04);
		CHECK_ERR(err);
#endif
		break;

	case MODE_FIREWORKS:
		/* Set CATE_408 to None */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM ON */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x01);
		CHECK_ERR(err);

		/* LIKE A PRO MODE SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x01, 0x07);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_MODE_CAP, 0x07);
		CHECK_ERR(err);

		/* LIKE A PRO STEP SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE,
			0x02, state->widget_mode_level);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, state->color_effect);
		CHECK_ERR(err);

		/* Set Capture Shutter Speed Time */
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_NUMERATOR, 32);
		CHECK_ERR(err);
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_SS_DENOMINATOR, 10);
		CHECK_ERR(err);

		/* Set Still Capture F-Number Value */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_F_NUMBER, 0x80);
		CHECK_ERR(err);

		/* Set Still Capture ISO Value */
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_ISO_VALUE, 0x64);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x00);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x04);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Enable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x01);
		CHECK_ERR(err);

		/* Set CATE_409 to 1(PREVIEW) */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x09, 0x01);
		CHECK_ERR(err);
		break;

	case MODE_BLUE_SKY:
		/* Set CATE_408 to None */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM ON */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x01);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x00);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x00);
		CHECK_ERR(err);

		/* LIKE A PRO MODE SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x01, 0x08);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_MODE_CAP, 0x00);
		CHECK_ERR(err);

		/* LIKE A PRO STEP SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE,
			0x02, state->widget_mode_level);
		CHECK_ERR(err);

		/* COLOR EFFECT SET */
		err = m9mo_readb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, &color_effect);
		CHECK_ERR(err);

		if (color_effect < 0x11)
			state->color_effect = color_effect;

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, 0x11 + state->widget_mode_level);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Enable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x01);
		CHECK_ERR(err);

		/* Set CATE_409 to 1(PREVIEW) */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x09, 0x01);
		CHECK_ERR(err);
		break;

	case MODE_NATURAL_GREEN:
		/* Set CATE_408 to None */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
		CHECK_ERR(err);

		/* Set HISTOGRAM ON */
		err = m9mo_writeb(sd, M9MO_CATEGORY_MON, 0x58, 0x01);
		CHECK_ERR(err);

		/* Set Monitor EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_MON, 0x00);
		CHECK_ERR(err);

		/* Set Still Capture EV program mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EP_MODE_CAP, 0x00);
		CHECK_ERR(err);

		/* LIKE A PRO MODE SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x01, 0x09);
		CHECK_ERR(err);

		/* Still Capture EVP Set Parameter Mode */
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			M9MO_AE_EV_PRG_MODE_CAP, 0x00);
		CHECK_ERR(err);

		/* LIKE A PRO STEP SET */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE,
			0x02, state->widget_mode_level);
		CHECK_ERR(err);

		/* COLOR EFFECT SET */
		err = m9mo_readb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, &color_effect);
		CHECK_ERR(err);

		if (color_effect < 0x11)
			state->color_effect = color_effect;

		err = m9mo_writeb(sd, M9MO_CATEGORY_MON,
			M9MO_MON_COLOR_EFFECT, 0x21 + state->widget_mode_level);
		CHECK_ERR(err);

		/* Set LIKE_PRO_EN Enable */
		err = m9mo_writeb(sd, M9MO_CATEGORY_PRO_MODE, 0x00, 0x01);
		CHECK_ERR(err);

		/* Set CATE_409 to 1(PREVIEW) */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x09, 0x01);
		CHECK_ERR(err);
		break;

	default:
		break;
	}

	if (state->facedetect_mode == FACE_DETECTION_NORMAL
		|| state->facedetect_mode == FACE_DETECTION_BLINK) {
		if (state->mode == MODE_SMART_AUTO) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_FD,
				M9MO_FD_CTL, 0x01);
		} else {
			err = m9mo_writeb(sd, M9MO_CATEGORY_FD,
				M9MO_FD_CTL, 0x11);
		}
		CHECK_ERR(err);
	}

	m9mo_set_gamma(sd);
	m9mo_set_iqgrp(sd, 0);

	m9mo_set_OIS_cap_mode(sd);

	cam_trace("X\n");

	return 0;
}

static int m9mo_set_shutter_speed(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err = 0;
	int numerator = 1, denominator = 30;
	cam_dbg("E, value %d\n", val);

	if (state->mode > MODE_VIDEO) {
		cam_trace("Don't set when like a pro !!!\n");
		return 0;
	}

	switch (val) {
	case 1:
		numerator = 16;
		denominator = 1;
		break;

	case 2:
		numerator = 13;
		denominator = 1;
		break;

	case 3:
		numerator = 10;
		denominator = 1;
		break;

	case 4:
		numerator = 8;
		denominator = 1;
		break;

	case 5:
		numerator = 6;
		denominator = 1;
		break;

	case 6:
		numerator = 5;
		denominator = 1;
		break;

	case 7:
		numerator = 4;
		denominator = 1;
		break;

	case 8:
		numerator = 32;
		denominator = 10;
		break;

	case 9:
		numerator = 25;
		denominator = 10;
		break;

	case 10:
		numerator = 2;
		denominator = 1;
		break;

	case 11:
		numerator = 16;
		denominator = 10;
		break;

	case 12:
		numerator = 13;
		denominator = 10;
		break;

	case 13:
		numerator = 1;
		denominator = 1;
		break;

	case 14:
		numerator = 8;
		denominator = 10;
		break;

	case 15:
		numerator = 6;
		denominator = 10;
		break;

	case 16:
		numerator = 5;
		denominator = 10;
		break;

	case 17:
		numerator = 4;
		denominator = 10;
		break;

	case 18:
		numerator = 1;
		denominator = 3;
		break;

	case 19:
		numerator = 1;
		denominator = 4;
		break;

	case 20:
		numerator = 1;
		denominator = 5;
		break;

	case 21:
		numerator = 1;
		denominator = 6;
		break;

	case 22:
		numerator = 1;
		denominator = 8;
		break;

	case 23:
		numerator = 1;
		denominator = 10;
		break;

	case 24:
		numerator = 1;
		denominator = 13;
		break;

	case 25:
		numerator = 1;
		denominator = 16;
		break;

	case 26:
		numerator = 1;
		denominator = 20;
		break;

	case 27:
		numerator = 1;
		denominator = 25;
		break;

	case 28:
		numerator = 1;
		denominator = 30;
		break;

	case 29:
		numerator = 1;
		denominator = 40;
		break;

	case 30:
		numerator = 1;
		denominator = 50;
		break;

	case 31:
		numerator = 1;
		denominator = 60;
		break;

	case 32:
		numerator = 1;
		denominator = 80;
		break;

	case 33:
		numerator = 1;
		denominator = 100;
		break;

	case 34:
		numerator = 1;
		denominator = 125;
		break;

	case 35:
		numerator = 1;
		denominator = 160;
		break;

	case 36:
		numerator = 1;
		denominator = 200;
		break;

	case 37:
		numerator = 1;
		denominator = 250;
		break;

	case 38:
		numerator = 1;
		denominator = 320;
		break;

	case 39:
		numerator = 1;
		denominator = 400;
		break;

	case 40:
		numerator = 1;
		denominator = 500;
		break;

	case 41:
		numerator = 1;
		denominator = 640;
		break;

	case 42:
		numerator = 1;
		denominator = 800;
		break;

	case 43:
		numerator = 1;
		denominator = 1000;
		break;

	case 44:
		numerator = 1;
		denominator = 1250;
		break;

	case 45:
		numerator = 1;
		denominator = 1600;
		break;

	case 46:
		numerator = 1;
		denominator = 2000;
		break;

	default:
		break;
	}

	state->numerator = numerator;
	state->denominator = denominator;

	/* Set Capture Shutter Speed Time */
	err = m9mo_writew(sd, M9MO_CATEGORY_AE,
		M9MO_AE_EV_PRG_SS_NUMERATOR, numerator);
	CHECK_ERR(err);
	err = m9mo_writew(sd, M9MO_CATEGORY_AE,
		M9MO_AE_EV_PRG_SS_DENOMINATOR, denominator);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_f_number(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err = 0;

	cam_dbg("E, value %d\n", val);

	/* Max value : 15.9 */
	if (val > 159)
		val = 159;

	val = (val / 10) * 16 + (val % 10);
	state->f_number = val;

	/* Set Still Capture F-Number Value */
	err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
		M9MO_AE_EV_PRG_F_NUMBER, val);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_wb_custom(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err = 0;

	cam_dbg("E\n");

	err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
		M9MO_WB_AWB_MODE, 0x02);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
		M9MO_WB_AWB_MANUAL, 0x08);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
		M9MO_WB_CWB_MODE, 0x02);
	CHECK_ERR(err);

	err = m9mo_writew(sd, M9MO_CATEGORY_WB,
		M9MO_WB_SET_CUSTOM_RG, state->wb_custom_rg);
	CHECK_ERR(err);

	err = m9mo_writew(sd, M9MO_CATEGORY_WB,
		M9MO_WB_SET_CUSTOM_BG, state->wb_custom_bg);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_WB,
		M9MO_WB_CWB_MODE, 0x01);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_set_smart_auto_s1_push(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int int_factor, err;

	cam_dbg("E val : %d\n", val);

	if (state->mode == MODE_SMART_AUTO ||
		(state->mode >= MODE_BACKGROUND_BLUR &&
		state->mode <= MODE_NATURAL_GREEN)) {
		if (val == 1) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x09, 0x02);
			CHECK_ERR(err);
		} else if (val == 2) {
			if (state->facedetect_mode == FACE_DETECTION_NORMAL
				&& state->mode == MODE_SMART_AUTO) {
				err = m9mo_writeb(sd, M9MO_CATEGORY_FD,
					M9MO_FD_CTL, 0x01);
				CHECK_ERR(err);
			}

			err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x09, 0x04);
			CHECK_ERR(err);

			int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
			if (!(int_factor & M9MO_INT_ATSCENE_UPDATE)) {
				cam_err("M9MO_INT_ATSCENE_UPDATE isn't issued, %#x\n",
					int_factor);
				return -ETIMEDOUT;
			}
			CHECK_ERR(err);
		} else {
			if (state->mode == MODE_SMART_AUTO)
				m9mo_set_smart_auto_default_value(sd, 0);

			err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x09, 0x03);
			CHECK_ERR(err);
		}
	}
	cam_trace("X\n");
	return 0;
}

static int m9mo_set_mon_size(struct v4l2_subdev *sd, int val)
{
	struct m9mo_state *state = to_state(sd);
	int err, vss_val, current_mode;
	u32 size_val;

	if (state->isp_fw_ver < 0xA02B) {
		cam_dbg("%x firmware cannot working quick monitor mode\n",
			state->isp_fw_ver);
		return 0;
	}

	err = m9mo_readb(sd, M9MO_CATEGORY_SYS, M9MO_SYS_MODE, &current_mode);
	CHECK_ERR(err);

	if (current_mode != M9MO_PARMSET_MODE) {
		cam_trace("only param mode !!!\n");
		return 0;
	}

	cam_dbg("E\n");

	if (state->fps == 60) {
		if (state->preview_height == 480)
			size_val = 0x2F;
		else if (state->preview_height == 720)
			size_val = 0x25;
		vss_val = 0;
	} else if (state->fps == 30) {
		if (state->preview_height == 1080)
			size_val = 0x28;
		else if (state->preview_height == 720)
			size_val = 0x21;
		else if (state->preview_height == 480)
			size_val = 0x17;
		else if (state->preview_height == 240)
			size_val = 0x09;
		vss_val = 0;
	} else {
		if (state->preview_width == 640)
			size_val = 0x17;
		else if (state->preview_width == 768)
			size_val = 0x33;
		else if (state->preview_width == 960)
			size_val = 0x34;
		else if (state->preview_width == 1056)
			size_val = 0x35;
		else if (state->preview_width == 1280)
			size_val = 0x21;
		else if (state->preview_width == 1920)
			size_val = 0x28;
		vss_val = 0;
	}

	if (val == 1080) {
		if (state->factory_test_num)
			size_val = 0x37;
	}

	err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
		M9MO_PARM_MON_SIZE, size_val);
	CHECK_ERR(err);

	m9mo_set_iqgrp(sd, val);

	m9mo_set_dual_capture_mode(sd, vss_val);

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL, 0x25, 0x01);
	CHECK_ERR(err);

	cam_trace("start quick monitor mode !!!\n");
	err = m9mo_writeb(sd, M9MO_CATEGORY_PARM, 0x7C, 0x01);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_check_dataline(struct v4l2_subdev *sd, int val)
{
	int err = 0;

	cam_dbg("E, value %d\n", val);

	err = m9mo_writeb(sd, M9MO_CATEGORY_TEST,
		M9MO_TEST_OUTPUT_YCO_TEST_DATA, val ? 0x01 : 0x00);
	CHECK_ERR(err);

	cam_trace("X\n");
	return 0;
}

static int m9mo_check_esd(struct v4l2_subdev *sd)
{
	s32 val = 0;
	int err = 0;

	struct m9mo_state *state = to_state(sd);

	if (state->factory_test_num != 0) {
		cam_dbg("factory test mode !!! ignore esd check\n");
		return 0;
	}

	/* check ISP */
#if 0	/* TO DO */
	err = m9mo_readb(sd, M9MO_CATEGORY_TEST, M9MO_TEST_ISP_PROCESS, &val);
	CHECK_ERR(err);
	cam_dbg("progress %#x\n", val);
#else
	val = 0x80;
#endif

	if (val != 0x80) {
		goto esd_occur;
	} else {
		m9mo_wait_interrupt(sd, M9MO_ISP_ESD_TIMEOUT);

		err = m9mo_readb(sd, M9MO_CATEGORY_SYS, M9MO_SYS_ESD_INT, &val);
		CHECK_ERR(err);

		if (val & M9MO_INT_ESD)
			goto esd_occur;
	}

	cam_warn("ESD is not detected\n");
	return 0;

esd_occur:
	cam_warn("ESD shock is detected\n");
	return -EIO;
}

static int m9mo_g_ext_ctrl(struct v4l2_subdev *sd,
		struct v4l2_ext_control *ctrl)
{
	int err = 0, i = 0;

	char *buf = NULL;

	int size = 0, rtn = 0, cmd_size = 0;
	u8 category = 0, sub = 0;
	u32 addr = 0;

	size = ctrl->size;

	cam_dbg("ISPD m9mo_g_ext_ctrl()  id=%d, size=%d\n",
				ctrl->id, ctrl->size);

	if (size > 4096)
		return -1;

	switch (ctrl->id) {
	case V4L2_CID_ISP_DEBUG_READ:

		cmd_size = 2;
		if (size < cmd_size+1)  /* category:1, sub:1, data:>1 */
			return -2;

		buf = kmalloc(ctrl->size, GFP_KERNEL);
		if (copy_from_user(buf,  (void __user *)ctrl->string, size)) {
			err = -1;
			break;
		}

		category = buf[0];
		sub = buf[1];

		memset(buf, 0, size-cmd_size);
		for (i = 0; i < size-cmd_size; i++) {
			err = m9mo_readb(sd, category, sub+i, &rtn);
			buf[i] = rtn;
			cam_dbg("ISPD m9mo_readb(sd, %x, %x, %x\n",
						category, sub+i, buf[i]);
			CHECK_ERR(err);
		}

		if (copy_to_user((void __user *)ctrl->string,
					buf, size-cmd_size))
			err = -1;

		kfree(buf);
		break;

	case V4L2_CID_ISP_DEBUG_READ_MEM:

		cmd_size = 4;
		if (size < cmd_size+1)  /* cmd size : 4, data : >1 */
			return -2;

		buf = kmalloc(ctrl->size, GFP_KERNEL);
		if (copy_from_user(buf,  (void __user *)ctrl->string, size)) {
			err = -1;
			break;
		}

		addr = buf[0]<<24|buf[1]<<16|buf[2]<<8|buf[3];

		memset(buf, 0, size-cmd_size);
		err = m9mo_mem_read(sd, size-cmd_size, addr, buf);
		cam_dbg("ISPD m9mo_mem_read7(sd, %x, %d)\n",
					addr, size-cmd_size);
		if (err < 0)
			cam_err("i2c falied, err %d\n", err);

		if (copy_to_user((void __user *)ctrl->string,
					buf, size-cmd_size))
			err = -1;

		kfree(buf);
		break;

	default:
		cam_err("no such control id %d\n",
				ctrl->id - V4L2_CID_CAMERA_CLASS_BASE);
		/*err = -ENOIOCTLCMD*/
		/*err = 0;*/
		break;
	}

	/* FIXME
	 * if (err < 0 && err != -ENOIOCTLCMD)
	 *	cam_err("failed, id %d\n",
		ctrl->id - V4L2_CID_CAMERA_CLASS_BASE);
	 */

	return err;
}

static int m9mo_g_ext_ctrls(struct v4l2_subdev *sd,
		struct v4l2_ext_controls *ctrls)
{
	struct v4l2_ext_control *ctrl = ctrls->controls;
	int i, err = 0;

	for (i = 0; i < ctrls->count; i++, ctrl++) {
		err = m9mo_g_ext_ctrl(sd, ctrl);
		if (err) {
			ctrls->error_idx = i;
			break;
		}
	}
	return err;
}

static int m9mo_makeLog(struct v4l2_subdev *sd, char *filename)
{
	int addr = 0, len = 0xff; /* init */
	int err = 0;
	int i = 0, no = 0;
	char buf[256];

	struct file *fp;
	mm_segment_t old_fs;
	char filepath[256];

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	sprintf(filepath, "/sdcard/ISPD/%s%c", filename, 0);

	fp = filp_open(filepath,
		O_WRONLY|O_CREAT|O_TRUNC, S_IRUGO|S_IWUGO|S_IXUSR);
	if (IS_ERR(fp)) {
		cam_err("failed to open %s, err %ld\n",
			filepath, PTR_ERR(fp));
		return  -1;
	}

#ifdef M9MO_ISP_DEBUG
	cam_dbg("%s\n", filepath);
#endif
	err = m9mo_writeb2(sd, 0x0d, 0x06, 0x0);
	CHECK_ERR(err);

	err = m9mo_readl2(sd, 0x0d, 0x08, &addr);
	CHECK_ERR(err);

	err = m9mo_writeb2(sd, 0x0d, 0x0e, 0x2);
	CHECK_ERR(err);

	while (no < 10000) { /* max log count : 10000 */
		err = m9mo_writew2(sd, 0x0d, 0x0c, no);
		CHECK_ERR(err);

		err = m9mo_writeb2(sd, 0x0d, 0x0e, 0x3);
		CHECK_ERR(err);

		while (len == 0xff) {
			err = m9mo_readb2(sd, 0x0d, 0x07, &len);
			CHECK_ERR(err);

			if (i++ > 3000)  /* only delay code */
				break;
		}

		if (len == 0 || len == 0xff) {
			err = m9mo_writeb2(sd, 0x0d, 0x0e, 0x1);
			CHECK_ERR(err);
			break;
		}

		i = 0;
		len += 1;
		if (len > sizeof(buf))
			len = sizeof(buf);
		err = m9mo_mem_read(sd,  len, addr, buf);
		if (err < 0)
			cam_err("ISPD i2c falied, err %d\n", err);

		buf[len-1] = '\n';

		vfs_write(fp, buf, len,  &fp->f_pos);
#if 0
		cam_dbg("ISPD Log : %x[%d], %d, %32s)\n",
					addr, no, len, buf);
#endif
		len = 0xff; /* init */
		no++;
	}

	if (!IS_ERR(fp))
		filp_close(fp, current->files);

	set_fs(old_fs);

	return 0;
}

static int m9mo_s_ext_ctrl(struct v4l2_subdev *sd,
		struct v4l2_ext_control *ctrl)
{
	int err = 0, i = 0;
	char *buf = NULL;

	int size = 0, cmd_size = 0;
	u8 category = 0, sub = 0;
	u32 addr = 0;

	size = ctrl->size;

	if (size > 1024)
		return -1;

	switch (ctrl->id) {
	case V4L2_CID_ISP_DEBUG_WRITE:

		cmd_size = 2;
		if (size < cmd_size+1)
			return -2;

		buf = kmalloc(ctrl->size, GFP_KERNEL);
		if (copy_from_user(buf,  (void __user *)ctrl->string, size)) {
			err = -1;
			break;
		}

		category = buf[0];
		sub = buf[1];

		cam_dbg("ISP_DBG write() %x%x%x\n", buf[0], buf[1], buf[2]);

		for (i = 0; i < size-cmd_size; i++) {
			err = m9mo_writeb(sd, category, sub+i, buf[i+cmd_size]);
			cam_dbg("ISPD m9mo_writeb(sd, %x, %x, %x\n",
				category, sub+i, buf[i+cmd_size]);
			CHECK_ERR(err);
		}

		kfree(buf);
		break;

	case V4L2_CID_ISP_DEBUG_WRITE_MEM:

		cmd_size = 4;
		if (size < cmd_size+1)
			return -2;

		buf = kmalloc(ctrl->size, GFP_KERNEL);
		if (copy_from_user(buf,
				(void __user *)ctrl->string, size)) {
			err = -1;
			break;
		}

		addr = buf[0]<<24|buf[1]<<16|buf[2]<<8|buf[3];

		cam_dbg("ISP_DBG write_mem() 0x%08x, size=%d\n",
					addr, size);

		err = m9mo_mem_write(sd, 0x04,
				size-cmd_size, addr, buf+cmd_size);
		cam_dbg("ISPD m9mo_mem_write(sd, %x, %d)\n",
				addr, size-cmd_size);
		if (err < 0)
			cam_err("i2c falied, err %d\n", err);

		kfree(buf);
		break;

	case V4L2_CID_ISP_DEBUG_LOGV:

		if (size > 0) {
			buf = kmalloc(ctrl->size+1, GFP_KERNEL);
			if (copy_from_user(buf,
					(void __user *)ctrl->string, size)) {
				err = -1;
				break;
			}
			buf[size] = 0;

			m9mo_makeLog(sd, buf);

			kfree(buf);
		} else {
			m9mo_makeLog(sd, "default.log");
		}

		break;

	default:
		cam_err("no such control id %d\n",
				ctrl->id - V4L2_CID_CAMERA_CLASS_BASE);
		break;
	}

	return err;
}


static int m9mo_s_ext_ctrls(struct v4l2_subdev *sd,
		struct v4l2_ext_controls *ctrls)
{
	struct v4l2_ext_control *ctrl = ctrls->controls;
	int i, err = 0;

	for (i = 0; i < ctrls->count; i++, ctrl++) {
		err = m9mo_s_ext_ctrl(sd, ctrl);
		if (err) {
			ctrls->error_idx = i;
			break;
		}
	}
	return err;
}

static int m9mo_check_manufacturer_id(struct v4l2_subdev *sd)
{
	int i, err;
	u8 id;
	u32 addr[] = {0x1000AAAA, 0x10005554, 0x1000AAAA};
	u8 val[3][2] = {
		[0] = {0x00, 0xAA},
		[1] = {0x00, 0x55},
		[2] = {0x00, 0x90},
	};
	u8 reset[] = {0x00, 0xF0};

	/* set manufacturer's ID read-mode */
	for (i = 0; i < 3; i++) {
		err = m9mo_mem_write(sd, 0x06, 2, addr[i], val[i]);
		CHECK_ERR(err);
	}

	/* read manufacturer's ID */
	err = m9mo_mem_read(sd, sizeof(id), 0x10000001, &id);
	CHECK_ERR(err);

	/* reset manufacturer's ID read-mode */
	err = m9mo_mem_write(sd, 0x06, sizeof(reset), 0x10000000, reset);
	CHECK_ERR(err);

	cam_dbg("%#x\n", id);

	return id;
}

static int m9mo_program_fw(struct v4l2_subdev *sd,
	u8 *buf, u32 addr, u32 unit, u32 count)
{
	u32 val;
	int i, err = 0;
	int erase = 0x01;
	int test_count = 0;
	int retries = 0;
	int checksum = 0;

	for (i = 0; i < unit*count; i += unit) {
		/* Set Flash ROM memory address */
		err = m9mo_writel(sd, M9MO_CATEGORY_FLASH,
			M9MO_FLASH_ADDR, addr + i);
		CHECK_ERR(err);

		/* Erase FLASH ROM entire memory */
		err = m9mo_writeb(sd, M9MO_CATEGORY_FLASH,
			M9MO_FLASH_ERASE, erase);
		CHECK_ERR(err);
		/* Response while sector-erase is operating */
		retries = 0;
		do {
			mdelay(30);
			err = m9mo_readb(sd, M9MO_CATEGORY_FLASH,
				M9MO_FLASH_ERASE, &val);
			CHECK_ERR(err);
		} while (val == erase && retries++ < M9MO_I2C_VERIFY);

		if (val != 0) {
			cam_err("failed to erase sector\n");
			return -1;
		}

		/* Set FLASH ROM programming size */
		err = m9mo_writew(sd, M9MO_CATEGORY_FLASH,
				M9MO_FLASH_BYTE, unit);
		CHECK_ERR(err);

		err = m9mo_mem_write(sd, 0x04, unit,
				M9MO_INT_RAM_BASE_ADDR, buf + i);
			CHECK_ERR(err);
		cam_err("fw Send = %x count = %d\n", i, test_count++);

	/* Start Programming */
		err = m9mo_writeb(sd, M9MO_CATEGORY_FLASH, M9MO_FLASH_WR, 0x01);
		CHECK_ERR(err);

		/* Confirm programming has been completed */
		retries = 0;
		do {
			mdelay(30);
			err = m9mo_readb(sd, M9MO_CATEGORY_FLASH,
				M9MO_FLASH_WR, &val);
			CHECK_ERR(err);
		} while (val && retries++ < M9MO_I2C_VERIFY);

		if (val != 0) {
			cam_err("failed to program~~~~\n");
			return -1;
		}
	}

	checksum = m9mo_check_checksum(sd);

	cam_err("m9mo_program_fw out ~~~~~~~~~~~\n");

	if (checksum == 1)
		return 0;
	else
		return -1;
}

static int m9mo_load_fw_main(struct v4l2_subdev *sd)
{
	struct m9mo_state *state = to_state(sd);
	struct device *dev = sd->v4l2_dev->dev;
	const struct firmware *fw = NULL;
	u8 *buf_m9mo = NULL;
	unsigned int count = 0;
	/*int offset;*/
	int err = 0;

	struct file *fp;
	mm_segment_t old_fs;
	long fsize, nread;
	int fw_requested = 1;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(M9MO_FW_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		cam_trace("failed to open %s, err %ld\n",
			M9MO_FW_PATH, PTR_ERR(fp));
		if (PTR_ERR(fp) == -4) {
			cam_err("%s: file open I/O is interrupted\n", __func__);
			return -EIO;
		}
		goto request_fw;
	}

	fw_requested = 0;
	fsize = fp->f_path.dentry->d_inode->i_size;
	count = fsize / SZ_4K;

	cam_err("start, file path %s, size %ld Bytes, count %d\n",
		M9MO_FW_PATH, fsize, count);

	buf_m9mo = vmalloc(fsize);
	if (!buf_m9mo) {
		cam_err("failed to allocate memory\n");
		err = -ENOMEM;
		goto out;
	}

	nread = vfs_read(fp, (char __user *)buf_m9mo, fsize, &fp->f_pos);
	if (nread != fsize) {
		cam_err("failed to read firmware file, %ld Bytes\n", nread);
		err = -EIO;
		goto out;
	}

	filp_close(fp, current->files);

request_fw:
	if (fw_requested) {
		set_fs(old_fs);
		if (system_rev > 1) {
			cam_info("Firmware Path = %s\n",
					M9MO_EVT31_FW_REQ_PATH);
			err = request_firmware(&fw,
					M9MO_EVT31_FW_REQ_PATH, dev);
		} else {
			cam_info("Firmware Path = %s\n", M9MO_FW_REQ_PATH);
			err = request_firmware(&fw, M9MO_FW_REQ_PATH, dev);
		}


		if (err != 0) {
			cam_err("request_firmware failed\n");
			err = -EINVAL;
			goto out;
		}
		count = fw->size / SZ_4K;
		cam_err("start, size %d Bytes  count = %d\n", fw->size, count);
		buf_m9mo = (u8 *)fw->data;
	}

	err = m9mo_mem_write(sd, 0x04, SZ_64,
			0x90001200 , buf_port_seting0);
	CHECK_ERR(err);
	mdelay(10);

	err = m9mo_mem_write(sd, 0x04, SZ_64,
			0x90001000 , buf_port_seting1);
	CHECK_ERR(err);
	mdelay(10);

	err = m9mo_mem_write(sd, 0x04, SZ_64,
			0x90001100 , buf_port_seting2);
	CHECK_ERR(err);
	mdelay(10);

	err = m9mo_writel(sd, M9MO_CATEGORY_FLASH,
			0x1C, 0x0247036D);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_FLASH,
			0x4A, 0x01);
	CHECK_ERR(err);
	mdelay(10);

	/* program FLASH ROM */
	err = m9mo_program_fw(sd, buf_m9mo, M9MO_FLASH_BASE_ADDR, SZ_4K, count);
	if (err < 0)
		goto out;


#if 0
	offset = SZ_64K * 31;
	if (id == 0x01) {
		err = m9mo_program_fw(sd, buf + offset,
				M9MO_FLASH_BASE_ADDR + offset, SZ_8K, 4, id);
	} else {
		err = m9mo_program_fw(sd, buf + offset,
				M9MO_FLASH_BASE_ADDR + offset, SZ_4K, 8, id);
	}
#endif
	cam_err("end\n");
	state->isp.bad_fw = 0;

out:
	if (!fw_requested) {
		vfree(buf_m9mo);

		filp_close(fp, current->files);
		set_fs(old_fs);
	} else {
		release_firmware(fw);
	}

	return err;
}

static int m9mo_load_fw_info(struct v4l2_subdev *sd)
{
	const struct firmware *fw = NULL;
	u8 *buf_m9mo = NULL;
	/*int offset;*/
	int err = 0;

	struct file *fp;
	mm_segment_t old_fs;
	long fsize, nread;
	int fw_requested = 1;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(FW_INFO_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		cam_trace("failed to open %s, err %ld\n",
			M9MO_FW_PATH, PTR_ERR(fp));
		if (PTR_ERR(fp) == -4) {
			cam_err("%s: file open I/O is interrupted\n", __func__);
			return -EIO;
		}
	}
	fsize = fp->f_path.dentry->d_inode->i_size;

	cam_err("start, file path %s, size %ld Bytes\n", FW_INFO_PATH, fsize);

	buf_m9mo = vmalloc(fsize);
	if (!buf_m9mo) {
		cam_err("failed to allocate memory\n");
		err = -ENOMEM;
		goto out;
	}

	nread = vfs_read(fp, (char __user *)buf_m9mo, fsize, &fp->f_pos);
	if (nread != fsize) {
		cam_err("failed to read firmware file, %ld Bytes\n", nread);
		err = -EIO;
		goto out;
	}

	err = m9mo_mem_write(sd, 0x04, SZ_64,
				0x90001200 , buf_port_seting0);
			CHECK_ERR(err);
			mdelay(10);

	err = m9mo_mem_write(sd, 0x04, SZ_64,
				0x90001000 , buf_port_seting1);
			CHECK_ERR(err);
			mdelay(10);

	err = m9mo_mem_write(sd, 0x04, SZ_64,
				0x90001100 , buf_port_seting2);
			CHECK_ERR(err);
			mdelay(10);

	err = m9mo_writel(sd, M9MO_CATEGORY_FLASH,
				0x1C, 0x0247036D);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_FLASH,
				0x4A, 0x01);
	CHECK_ERR(err);
			mdelay(10);

	/* program FLSH ROM */
	err = m9mo_program_fw(sd, buf_m9mo, M9MO_FLASH_BASE_ADDR_1, SZ_4K, 1);
	if (err < 0)
		goto out;


#if 0
	offset = SZ_64K * 31;
	if (id == 0x01) {
		err = m9mo_program_fw(sd, buf + offset,
				M9MO_FLASH_BASE_ADDR + offset, SZ_8K, 4, id);
	} else {
		err = m9mo_program_fw(sd, buf + offset,
				M9MO_FLASH_BASE_ADDR + offset, SZ_4K, 8, id);
	}
#endif
	cam_err("end\n");

out:
	if (!fw_requested) {
		vfree(buf_m9mo);

		filp_close(fp, current->files);
		set_fs(old_fs);
	} else {
		release_firmware(fw);
	}

	return err;
}



static int m9mo_load_fw(struct v4l2_subdev *sd)
{
	struct device *dev = sd->v4l2_dev->dev;
	const struct firmware *fw = NULL;
	u8 sensor_ver[M9MO_FW_VER_LEN] = {0, };
	u8 *buf_m9mo = NULL, *buf_fw_info = NULL;
	/*int offset;*/
	int err = 0;

	struct file *fp;
	mm_segment_t old_fs;
	long fsize, nread;
	int fw_requested = 1;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(M9MO_FW_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		cam_err("failed to open %s, err %ld\n",
			M9MO_FW_PATH, PTR_ERR(fp));
		if (PTR_ERR(fp) == -4) {
			cam_err("%s: file open I/O is interrupted\n", __func__);
			return -EIO;
		}
		goto request_fw;
	}

	fw_requested = 0;
	fsize = fp->f_path.dentry->d_inode->i_size;

	cam_err("start, file path %s, size %ld Bytes\n", M9MO_FW_PATH, fsize);

	buf_m9mo = vmalloc(fsize);
	if (!buf_m9mo) {
		cam_err("failed to allocate memory\n");
		err = -ENOMEM;
		goto out;
	}

	nread = vfs_read(fp, (char __user *)buf_m9mo, fsize, &fp->f_pos);
	if (nread != fsize) {
		cam_err("failed to read firmware file, %ld Bytes\n", nread);
		err = -EIO;
		goto out;
	}

	filp_close(fp, current->files);

	fp = filp_open(FW_INFO_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		cam_trace("failed to open %s, err %ld\n",
			FW_INFO_PATH, PTR_ERR(fp));
		if (PTR_ERR(fp) == -4) {
			cam_err("%s: file open I/O is interrupted\n", __func__);
			return -EIO;
		}
		goto request_fw;
	}

	fw_requested = 0;
	fsize = fp->f_path.dentry->d_inode->i_size;

	cam_err("start, file path %s, size %ld Bytes\n", FW_INFO_PATH, fsize);

	buf_fw_info = vmalloc(fsize);
	if (!buf_fw_info) {
		cam_err("failed to allocate memory\n");
		err = -ENOMEM;
		goto out;
	}

	nread = vfs_read(fp, (char __user *)buf_fw_info, fsize, &fp->f_pos);
	if (nread != fsize) {
		cam_err("failed to read firmware file, %ld Bytes\n", nread);
		err = -EIO;
		goto out;
	}


request_fw:
	if (fw_requested) {
		set_fs(old_fs);

	m9mo_get_sensor_fw_version(sd);

	if (sensor_ver[0] == 'T' && sensor_ver[1] == 'B') {
		err = request_firmware(&fw, M9MOTB_FW_PATH, dev);
#if defined(CONFIG_MACH_Q1_BD)
	} else if (sensor_ver[0] == 'O' && sensor_ver[1] == 'O') {
		err = request_firmware(&fw, M9MOOO_FW_PATH, dev);
#endif
#if defined(CONFIG_MACH_U1_KOR_LGT)
	} else if (sensor_ver[0] == 'S' && sensor_ver[1] == 'B') {
		err = request_firmware(&fw, M9MOSB_FW_PATH, dev);
#endif
	} else {
		cam_err("cannot find the matched F/W file\n");
		err = -EINVAL;
	}

	if (err != 0) {
		cam_err("request_firmware falied\n");
			err = -EINVAL;
			goto out;
	}
		cam_dbg("start, size %d Bytes\n", fw->size);
		buf_m9mo = (u8 *)fw->data;
	}


	err = m9mo_mem_write(sd, 0x04, SZ_64,
				0x90001200 , buf_port_seting0);
			CHECK_ERR(err);
			mdelay(10);

	err = m9mo_mem_write(sd, 0x04, SZ_64,
				0x90001000 , buf_port_seting1);
			CHECK_ERR(err);
			mdelay(10);

	err = m9mo_mem_write(sd, 0x04, SZ_64,
				0x90001100 , buf_port_seting2);
			CHECK_ERR(err);
			mdelay(10);

	err = m9mo_writel(sd, M9MO_CATEGORY_FLASH,
				0x1C, 0x0247036D);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_FLASH,
				0x4A, 0x01);
	CHECK_ERR(err);
			mdelay(10);

	/* program FLSH ROM */
	err = m9mo_program_fw(sd, buf_m9mo, M9MO_FLASH_BASE_ADDR, SZ_4K, 504);
	if (err < 0)
		goto out;

	err = m9mo_program_fw(sd, buf_fw_info,
			M9MO_FLASH_BASE_ADDR_1, SZ_4K, 1);
	if (err < 0)
		goto out;

#if 0
	offset = SZ_64K * 31;
	if (id == 0x01) {
		err = m9mo_program_fw(sd, buf + offset,
				M9MO_FLASH_BASE_ADDR + offset, SZ_8K, 4, id);
	} else {
		err = m9mo_program_fw(sd, buf + offset,
				M9MO_FLASH_BASE_ADDR + offset, SZ_4K, 8, id);
	}
#endif
	cam_err("end\n");

out:
	if (!fw_requested) {
		vfree(buf_m9mo);
		vfree(buf_fw_info);

		filp_close(fp, current->files);
		set_fs(old_fs);
	} else {
		release_firmware(fw);
	}

	return err;
}


static int m9mo_set_factory_af_led_onoff(struct v4l2_subdev *sd, bool on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	const struct m9mo_platform_data *pdata = client->dev.platform_data;

	if (on == true) {
		/* AF LED regulator on */
		pdata->af_led_power(1);
	} else {
		/* AF LED regulator off */
		pdata->af_led_power(0);
	}
	return 0;
}

static int m9mo_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct m9mo_state *state = to_state(sd);
	int err = 0;
	s16 temp;

	if (ctrl->id != V4L2_CID_CAMERA_LENS_TIMER) {
		cam_trace(" id %d, value %d\n",
		ctrl->id - V4L2_CID_PRIVATE_BASE, ctrl->value);
	}

	if (unlikely(state->isp.bad_fw && ctrl->id != V4L2_CID_CAM_UPDATE_FW)) {
		cam_err("\"Unknown\" state, please update F/W");
		return 0;
	}

	switch (ctrl->id) {
#ifdef HOLD_LENS_SUPPORT
	case V4L2_CID_CAMERA_HOLD_LENS:
		leave_power = true;
		break;
#endif
	case V4L2_CID_CAM_UPDATE_FW:
		if (ctrl->value == FW_MODE_DUMP)
			err = m9mo_dump_fw(sd);
		else
			err = m9mo_check_fw(sd);
		break;

	case V4L2_CID_CAMERA_SENSOR_MODE:
#ifdef FAST_CAPTURE
		if (ctrl->value == 2)
			err = m9mo_set_fast_capture(sd);
		else
			err = m9mo_set_sensor_mode(sd, ctrl->value);
#else
		err = m9mo_set_sensor_mode(sd, ctrl->value);
#endif
		break;

	case V4L2_CID_CAMERA_FLASH_MODE:
		err = m9mo_set_flash(sd, ctrl->value, 0);
		break;

	case V4L2_CID_CAMERA_ISO:
		err = m9mo_set_iso(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_METERING:
		err = m9mo_set_metering(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_BRIGHTNESS:
		err = m9mo_set_exposure(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_SHARPNESS:
		err = m9mo_set_sharpness(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_CONTRAST:
		err = m9mo_set_contrast(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_SATURATION:
		err = m9mo_set_saturation(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_WHITE_BALANCE:
		err = m9mo_set_whitebalance(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_SCENE_MODE:
		err = m9mo_set_scene_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_EFFECT:
		err = m9mo_set_effect(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_WDR:
		err = m9mo_set_wdr(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_ANTI_SHAKE:
		err = m9mo_set_antishake(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_BEAUTY_SHOT:
		err = m9mo_set_face_beauty(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FOCUS_MODE:
		err = m9mo_set_af_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_SET_AUTO_FOCUS:
		err = m9mo_set_af(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_OBJECT_POSITION_X:
		state->focus.pos_x = ctrl->value;
		#if 0
		/* FIXME - It should be fixed on F/W (touch AF offset) */
		if (state->preview != NULL) {
			if (state->exif.unique_id[0] == 'T') {
				if (state->preview->index == M9MO_PREVIEW_VGA)
					state->focus.pos_x -= 40;
				else if (state->preview->index ==
						M9MO_PREVIEW_WVGA)
					state->focus.pos_x -= 50;
			}
		}
		#endif
		break;

	case V4L2_CID_CAMERA_OBJECT_POSITION_Y:
		state->focus.pos_y = ctrl->value;
		#if 0
		/* FIXME - It should be fixed on F/W (touch AF offset) */
		if (state->preview != NULL) {
			if (state->preview->index == M9MO_PREVIEW_VGA) {
				if (state->exif.unique_id[0] == 'T')
					state->focus.pos_y -= 50;
			} else if (state->preview->index == M9MO_PREVIEW_WVGA) {
				if (state->exif.unique_id[0] == 'T')
					state->focus.pos_y -= 2;
				else
					state->focus.pos_y += 60;
			}
		}
		#endif
		break;

	case V4L2_CID_CAMERA_TOUCH_AF_START_STOP:
		err = m9mo_set_touch_auto_focus(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_AF_LED:
		err = m9mo_set_AF_LED(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_TIMER_LED:
		err = m9mo_set_timer_LED(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_TIMER_MODE:
		err = m9mo_set_timer_Mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_ZOOM:
		err = m9mo_set_zoom(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_OPTICAL_ZOOM_CTRL:
		err = m9mo_set_zoom_ctrl(sd, ctrl->value);
		break;

	case V4L2_CID_CAM_JPEG_QUALITY:
		err = m9mo_set_jpeg_quality(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_CAPTURE:
		err = m9mo_start_capture(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_CAPTURE_THUMB:
		err = m9mo_start_capture_thumb(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_YUV_CAPTURE:
		if ((state->factory_test_num
					== FACTORY_RESOL_WIDE_INSIDE)
			|| (state->factory_test_num
				== FACTORY_RESOL_TELE_INSIDE)
			|| (state->factory_test_num
				== FACTORY_TILT_TEST_INSIDE)) {
			err = m9mo_start_YUV_one_capture(sd, ctrl->value);
		} else {
			err = m9mo_start_YUV_capture(sd, ctrl->value);
		}
		break;

	case V4L2_CID_CAMERA_POSTVIEW_CAPTURE:
		err = m9mo_start_postview_capture(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_CAPTURE_MODE:
		err = m9mo_set_capture_mode(sd, ctrl->value);
		break;

	/*case V4L2_CID_CAMERA_HDR:
		err = m9mo_set_hdr(sd, ctrl->value);
		break;*/

	case V4L2_CID_CAMERA_VT_MODE:
		state->vt_mode = ctrl->value;
		break;

	case V4L2_CID_CAMERA_SAMSUNG_APP:
		state->samsung_app = ctrl->value;
		break;

	case V4L2_CID_CAMERA_CHECK_DATALINE:
		state->check_dataline = ctrl->value;
		break;

	case V4L2_CID_CAMERA_ANTI_BANDING:
		err = m9mo_set_antibanding(sd, ctrl);
		break;

	case V4L2_CID_CAMERA_CHECK_ESD:
		err = m9mo_check_esd(sd);
		break;

	case V4L2_CID_CAMERA_AEAWB_LOCK_UNLOCK:
		err = m9mo_set_aeawblock(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACE_DETECTION:
		err = m9mo_set_facedetect(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_BRACKET:
		err = m9mo_set_bracket(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_BRACKET_AEB:
		err = m9mo_set_bracket_aeb(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_BRACKET_WBB:
		err = m9mo_set_bracket_wbb(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_IMAGE_STABILIZER:
		err = m9mo_set_image_stabilizer_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_IS_OIS_MODE:
		err = m9mo_set_image_stabilizer_OIS(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FOCUS_AREA_MODE:
		err = m9mo_set_focus_area_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_OBJ_TRACKING_START_STOP:
		err = m9mo_set_object_tracking(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FRAME_RATE:
		err = m9mo_set_fps(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_SMART_ZOOM:
		err = m9mo_set_smart_zoom(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_LDC:
		err = m9mo_set_LDC(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_LSC:
		err = m9mo_set_LSC(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_WIDGET_MODE_LEVEL:
		err = m9mo_set_widget_mode_level(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_PREVIEW_WIDTH:
		state->preview_width = ctrl->value;
		break;

	case V4L2_CID_CAMERA_PREVIEW_HEIGHT:
		state->preview_height = ctrl->value;
		break;

	case V4L2_CID_CAMERA_PREVIEW_SIZE:
		err = m9mo_set_mon_size(sd, ctrl->value);
		break;

	case V4L2_CID_CAM_APERTURE_PREVIEW:
		err = m9mo_set_aperture_preview(sd, ctrl->value);
		break;

	case V4L2_CID_CAM_APERTURE_CAPTURE:
		err = m9mo_set_aperture_capture(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_OIS:
		err = m9mo_set_factory_OIS(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_OIS_SHIFT:
		err = m9mo_set_factory_OIS_shift(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_PUNT:
		err = m9mo_set_factory_punt(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_PUNT_SHORT_SCAN_DATA:
		cam_trace("FACTORY_PUNT_SHORT_SCAN_DATA : 0x%x\n", ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, (unsigned char)(ctrl->value));
		CHECK_ERR(err);

		cam_trace("~ FACTORY_PUNT_SHORT_SCAN_START ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0E, 0x01);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_PUNT_LONG_SCAN_DATA:
		cam_trace("FACTORY_PUNT_LONG_SCAN_DATA : 0x%x\n", ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, (unsigned char)(ctrl->value));
		CHECK_ERR(err);

		cam_trace("~ FACTORY_PUNT_LONG_SCAN_START ~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0E, 0x02);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_ZOOM:
		err = m9mo_set_factory_zoom(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_ZOOM_STEP:
		err = m9mo_set_factory_zoom_step(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_PUNT_RANGE_DATA_MIN:
		state->f_punt_data.min = ctrl->value;
		break;

	case V4L2_CID_CAMERA_FACTORY_PUNT_RANGE_DATA_MAX:
		state->f_punt_data.max = ctrl->value;
		break;

	case V4L2_CID_CAMERA_FACTORY_PUNT_RANGE_DATA_NUM:
		state->f_punt_data.num = ctrl->value;
		break;

	case V4L2_CID_CAMERA_FACTORY_FAIL_STOP:
		err = m9mo_set_factory_fail_stop(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_NODEFOCUS:
		err = m9mo_set_factory_nodefocus(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_INTERPOLATION:
		err = m9mo_set_factory_interpolation(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_COMMON:
		err = m9mo_set_factory_common(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_VIB:
		err = m9mo_set_factory_vib(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_GYRO:
		err = m9mo_set_factory_gyro(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_BACKLASH:
		err = m9mo_set_factory_backlash(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_BACKLASH_COUNT:
		err = m9mo_set_factory_backlash_count(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_BACKLASH_MAXTHRESHOLD:
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x1A, ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_ZOOM_RANGE_CHECK_DATA_MIN:
		state->f_zoom_data.range_min = ctrl->value;
		break;

	case V4L2_CID_CAMERA_FACTORY_ZOOM_RANGE_CHECK_DATA_MAX:
		state->f_zoom_data.range_max = ctrl->value;
		break;

	case V4L2_CID_CAMERA_FACTORY_ZOOM_SLOPE_CHECK_DATA_MIN:
		state->f_zoom_data.slope_min = ctrl->value;
		break;

	case V4L2_CID_CAMERA_FACTORY_ZOOM_SLOPE_CHECK_DATA_MAX:
		state->f_zoom_data.slope_max = ctrl->value;
		break;

	case V4L2_CID_CAMERA_FACTORY_AF:
		err = m9mo_set_factory_af(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_AF_STEP_SET:
		err = m9mo_set_factory_af_step(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_AF_POSITION:
		err = m9mo_set_factory_af_position(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_DEFOCUS:
		err = m9mo_set_factory_defocus(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_DEFOCUS_WIDE:
		err = m9mo_set_factory_defocus_wide(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_DEFOCUS_TELE:
		err = m9mo_set_factory_defocus_tele(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_RESOL_CAP:
		err = m9mo_set_factory_resol_cap(sd, ctrl->value);
		break;
	case V4L2_CID_CAMERA_SET_G_VALUE:
		state->wb_g_value = ctrl->value;
		break;

	case V4L2_CID_CAMERA_SET_B_VALUE:
		state->wb_b_value = ctrl->value;
		break;

	case V4L2_CID_CAMERA_SET_A_VALUE:
		state->wb_a_value = ctrl->value;
		break;

	case V4L2_CID_CAMERA_SET_M_VALUE:
		state->wb_m_value = ctrl->value;
		break;

	case V4L2_CID_CAMERA_SET_GBAM:
		err = m9mo_set_GBAM(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_SET_K_VALUE:
		err = m9mo_set_K(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_SET_FLASH_EVC_STEP:
		err = m9mo_set_flash_evc_step(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FLASH_BATT_INFO:
		err = m9mo_set_flash_batt_info(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_OIS_RANGE_DATA_X_MIN:
		cam_trace("==========Range X min Data : 0x%x, %d\n",
			(short)ctrl->value, (short)ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_NEW,
			0x21, (short)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_VIB_RANGE_DATA_X_MIN:
	case V4L2_CID_CAMERA_FACTORY_GYRO_RANGE_DATA_X_MIN:
		cam_trace("==========Range X min Data : 0x%x, %d\n",
			(u16)ctrl->value, (u16)ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_NEW,
			0x21, (short)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_OIS_RANGE_DATA_X_MAX:
		cam_trace("==========Range X max Data : 0x%x, %d\n",
			(short)ctrl->value, (short)ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_NEW,
			0x23, (short)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_VIB_RANGE_DATA_X_MAX:
	case V4L2_CID_CAMERA_FACTORY_GYRO_RANGE_DATA_X_MAX:
		cam_trace("==========Range X max Data : 0x%x, %d\n",
			(u16)ctrl->value, (u16)ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_NEW,
			0x23, (u16)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_OIS_RANGE_DATA_Y_MIN:
		err = m9mo_writew(sd, M9MO_CATEGORY_NEW,
			0x25, (short)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_VIB_RANGE_DATA_Y_MIN:
	case V4L2_CID_CAMERA_FACTORY_GYRO_RANGE_DATA_Y_MIN:
		err = m9mo_writew(sd, M9MO_CATEGORY_NEW,
			0x25, (u16)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_OIS_RANGE_DATA_Y_MAX:
		err = m9mo_writew(sd, M9MO_CATEGORY_NEW,
			0x27, (short)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_VIB_RANGE_DATA_Y_MAX:
	case V4L2_CID_CAMERA_FACTORY_GYRO_RANGE_DATA_Y_MAX:
		err = m9mo_writew(sd, M9MO_CATEGORY_NEW,
			0x27, (u16)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_OIS_RANGE_DATA_X_GAIN:
		err = m9mo_writew(sd, M9MO_CATEGORY_NEW,
			0x29, (short)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_VIB_RANGE_DATA_PEAK_X:
		err = m9mo_writew(sd, M9MO_CATEGORY_NEW,
			0x29, (u16)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_OIS_RANGE_DATA_PEAK_X:
		err = m9mo_writew(sd, M9MO_CATEGORY_NEW,
			0x2B, (short)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_VIB_RANGE_DATA_PEAK_Y:
		err = m9mo_writew(sd, M9MO_CATEGORY_NEW,
			0x2B, (u16)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_OIS_RANGE_DATA_PEAK_Y:
		err = m9mo_writew(sd, M9MO_CATEGORY_NEW,
			0x2D, (u16)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_TEST_NUMBER:
		cam_trace("==========FACTORY_TEST_NUMBER : 0x%x\n",
			ctrl->value);

		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x41, ctrl->value);
		state->factory_test_num = ctrl->value;

		/* AF LED on/off */
		if (ctrl->value == 120)
			m9mo_set_factory_af_led_onoff(sd, true);
		else
			m9mo_set_factory_af_led_onoff(sd, false);

		CHECK_ERR(err);
		break;

	case V4L2_CID_SET_CONTINUE_FPS:
		state->continueFps = ctrl->value;
		break;

	case V4L2_CID_CONTINUESHOT_PROC:
		err = m9mo_continue_proc(sd, ctrl->value);
		break;

	case V4L2_CID_BURSTSHOT_PROC:
		err = m9mo_burst_proc(sd, ctrl->value);
		break;

	case V4L2_CID_BURSTSHOT_SET_POSTVIEW_SIZE:
		err = m9mo_burst_set_postview_size(sd, ctrl->value);
		break;

	case V4L2_CID_BURSTSHOT_SET_SNAPSHOT_SIZE:
		err = m9mo_burst_set_snapshot_size(sd, ctrl->value);
		break;

#if 0
	case V4L2_CID_CAMERA_DUAL_POSTVIEW:
		err = m9mo_start_dual_postview(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_DUAL_CAPTURE:
		err = m9mo_start_dual_capture(sd, ctrl->value);
		break;
#endif

	case V4L2_CID_CAMERA_SET_DUAL_CAPTURE:
		err = m9mo_start_set_dual_capture(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_DUAL_CAPTURE_MODE:
		/*err = m9mo_set_dual_capture_mode(sd, ctrl->value);*/
		break;

	case V4L2_CID_CAMERA_FACTORY_AF_SCAN_LIMIT_MIN:
	case V4L2_CID_CAMERA_FACTORY_AF_SCAN_RANGE_MIN:
		temp = (short)((ctrl->value) & 0x0000FFFF);
		cam_trace("==========Range    min Data : 0x%x, %d\n",
			temp, temp);
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x18, temp);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_AF_SCAN_LIMIT_MAX:
	case V4L2_CID_CAMERA_FACTORY_AF_SCAN_RANGE_MAX:
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x1A, (u16)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_AF_LENS:
		err = m9mo_set_factory_af_lens(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_AF_ZONE:
		err = m9mo_set_factory_af_zone(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_LV_TARGET:
		cam_trace("FACTORY_LV_TARGET : 0x%x\n", ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x52, ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_ADJ_IRIS:
		err = m9mo_set_factory_adj_iris(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_ADJ_IRIS_RANGE_MIN:
		cam_trace("FACTORY_ADJ_IRIS_RANGE_MIN : 0x%x\n", ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x54, (unsigned char)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_ADJ_IRIS_RANGE_MAX:
		cam_trace("FACTORY_ADJ_IRIS_RANGE_MAX : 0x%x\n", ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x55, (unsigned char)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_ADJ_GAIN_LIVEVIEW:
		err = m9mo_set_factory_adj_gain_liveview(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_LIVEVIEW_OFFSET_MARK:
		cam_trace("FACTORY_LIVEVIEW_OFFSET_MARK : 0x%x\n",
				ctrl->value);
		err = m9mo_writel(sd, M9MO_CATEGORY_ADJST,
			0x3A, (u32)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_LIVEVIEW_OFFSET_VAL:
		cam_trace("FACTORY_LIVEVIEW_OFFSET_VAL : 0x%x\n",
				ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x3E, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_ADJ_GAIN_LIVEVIEW_RANGE_MIN:
		cam_trace("FACTORY_ADJ_GAIN_LIVEVIEW_RANGE_MIN : 0x%x\n",
				ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_ADJST,
			0x56, (u16)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_ADJ_GAIN_LIVEVIEW_RANGE_MAX:
		cam_trace("FACTORY_ADJ_GAIN_LIVEVIEW_RANGE_MAX : 0x%x\n",
				ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_ADJST,
			0x58, (u16)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_SH_CLOSE:
		err = m9mo_set_factory_sh_close(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_SH_CLOSE_IRIS_NUM:
		cam_trace("FACTORY_SH_CLOSE_IRIS_NUM : 0x%x\n", ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x51, ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_SH_CLOSE_SET_IRIS:
		cam_trace("FACTORY_SH_CLOSE_SET_IRIS : 0x%x\n", ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x6E, ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_SH_CLOSE_RANGE:
		cam_trace("FACTORY_SH_CLOSE_RANGE : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_ADJST,
			0x5A, (u16)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_SH_CLOSE_ISO:
		cam_trace("FACTORY_SH_CLOSE_ISO : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			0x3B, ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_SH_CLOSE_SPEEDTIME_X:
		cam_trace("FACTORY_SH_CLOSE_SPEEDTIME_X : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			0x37, (u16)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_SH_CLOSE_SPEEDTIME_Y:
		cam_trace("FACTORY_SH_CLOSE_SPEEDTIME_Y : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_AE,
			0x39, (u16)(ctrl->value));
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_FLICKER:
		err = m9mo_set_factory_flicker(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_CAPTURE_GAIN:
		err = m9mo_set_factory_capture_gain(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_CAPTURE_GAIN_OFFSET_MARK:
		cam_trace("FACTORY_CAPTURE_GAIN_OFFSET_MARK : 0x%x\n",
				ctrl->value);
		err = m9mo_writel(sd, M9MO_CATEGORY_ADJST,
			0x3A, (u32)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_CAPTURE_GAIN_OFFSET_VAL:
		cam_trace("FACTORY_CAPTURE_GAIN_OFFSET_VAL : 0x%x\n",
				ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x3E, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_CAPTURE_GAIN_RANGE_MIN:
		cam_trace("FACTORY_CAPTURE_GAIN_RANGE_MIN : 0x%x\n",
				ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_ADJST,
			0x56, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_CAPTURE_GAIN_RANGE_MAX:
		cam_trace("FACTORY_CAPTURE_GAIN_RANGE_MAX : 0x%x\n",
				ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_ADJST,
			0x58, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_LSC_TABLE:
		cam_trace("FACTORY_LSC_TABLE : 0x%x\n", ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x30, ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_LSC_REFERENCE:
		cam_trace("FACTORY_LSC_REFERENCE : 0x%x\n", ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x31, ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_CAPTURE_CTRL:
		err = m9mo_set_factory_capture_ctrl(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_AE_TARGET:
		cam_trace("V4L2_CID_CAMERA_FACTORY_AE_TARGET : 0x%x\n",
				ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
			0x02, (unsigned char)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_FLASH:
		err = m9mo_set_factory_flash(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_FLASH_CHR_CHK_TM:
		cam_trace("FLASH_CHR_CHK_TM : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_CAPPARM,
			0x3B, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_FLASH_RANGE_X:
		cam_trace("FLASH_RANGE_X : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_ADJST,
			0x71, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_FLASH_RANGE_Y:
		cam_trace("FLASH_RANGE_Y : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_ADJST,
			0x73, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_WB:
		err = m9mo_set_factory_wb(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_WB_IN_RG_VALUE:
		cam_trace("WB_IN_RG_VALUE : 0x%x\n",
				ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_ADJST,
			0x27, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_WB_IN_BG_VALUE:
		cam_trace("WB_IN_BG_VALUE : 0x%x\n",
				ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_ADJST,
			0x29, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_WB_OUT_RG_VALUE:
		cam_trace("WB_OUT_RG_VALUE : 0x%x\n",
				ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_ADJST,
			0x2B, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_WB_OUT_BG_VALUE:
		cam_trace("WB_OUT_RG_VALUE : 0x%x\n",
				ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_ADJST,
			0x2D, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_WB_RANGE:
		cam_trace("WB_RANGE_PERCENT : 0x%x\n",
				ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x1F, (u8)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_WB_RANGE_FLASH_WRITE:
		err = m9mo_writeb(sd, M9MO_CATEGORY_ADJST,
			0x26, 0x01);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_AFLED_RANGE_DATA_START_X:
		cam_trace("AFLED_RANGE_DATA_START_X : 0x%x\n",
				ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x43, (unsigned char)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_AFLED_RANGE_DATA_END_X:
		cam_trace("AFLED_RANGE_DATA_END_X : 0x%x\n",
				ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x44, (unsigned char)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_AFLED_RANGE_DATA_START_Y:
		cam_trace("AFLED_RANGE_DATA_START_Y : 0x%x\n",
				ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x45, (unsigned char)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_AFLED_RANGE_DATA_END_Y:
		cam_trace("AFLED_RANGE_DATA_END_Y : 0x%x\n",
				ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x46, (unsigned char)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_AF_LED_TIME:
		cam_trace("AF_LED_TIME : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x4B, ctrl->value);
		CHECK_ERR(err);

		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x4D, 0x01);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_AF_DIFF_CHECK_MIN:
		cam_trace("AF_DIFF_CHECK_MIN : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x18, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_AF_DIFF_CHECK_MAX:
		cam_trace("AF_DIFF_CHECK_MAX : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x1A, (u16)ctrl->value);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0xD, 0x11);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_DEFECTPIXEL:
		err = m9mo_set_factory_defectpixel(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_DFPX_NLV_CAP:
		cam_trace("DFPX_NLV_CAP : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_PARM,
			0x70, (u16) ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_DFPX_NLV_DR1_HD:
		cam_trace("DFPX_NLV_DR1_HD : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_PARM,
			0x7A, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_DFPX_NLV_DR0:
		cam_trace("DFPX_NLV_DR0 : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_PARM,
			0x76, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_DFPX_NLV_DR1:
		cam_trace("DFPX_NLV_D1 : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_PARM,
			0x72, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_DFPX_NLV_DR2:
		cam_trace("DFPX_NLV_DR2 : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_PARM,
			0x78, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_DFPX_NLV_DR_HS:
		cam_trace("DFPX_NLV_DR_HS : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_PARM,
			0x74, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_AF_LED_LV_MIN:
		cam_trace("AF_LED_LV_MIN : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x47, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_AF_LED_LV_MAX:
		cam_trace("AF_LED_LV_MIN : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x49, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_CAM_SYS_MODE:
		err = m9mo_set_factory_cam_sys_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_PASM_MODE:
		err = m9mo_set_PASM_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_SHUTTER_SPEED:
		err = m9mo_set_shutter_speed(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_F_NUMBER:
		err = m9mo_set_f_number(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_WB_CUSTOM_X:
		state->wb_custom_rg = ctrl->value;
		break;

	case V4L2_CID_CAMERA_WB_CUSTOM_Y:
		state->wb_custom_bg = ctrl->value;
		break;

	case V4L2_CID_CAMERA_WB_CUSTOM_VALUE:
		err = m9mo_set_wb_custom(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_SMART_SCENE_DETECT:
		if (ctrl->value == 1) {
			state->smart_scene_detect_mode = 1;
			err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x02);
			CHECK_ERR(err);
			err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x0A, 0x01);
			CHECK_ERR(err);
		} else {
			state->smart_scene_detect_mode = 0;
			err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x08, 0x00);
			CHECK_ERR(err);
			err = m9mo_writeb(sd, M9MO_CATEGORY_NEW, 0x0A, 0x00);
			CHECK_ERR(err);
		}
		break;

	case V4L2_CID_CAMERA_SMART_MOVIE_RECORDING:
		err = m9mo_set_smart_moving_recording(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_SMART_AUTO_S1_PUSH:
		err = m9mo_set_smart_auto_s1_push(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_CAF:
		err = m9mo_set_CAF(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FOCUS_RANGE:
		err = m9mo_set_focus_range(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_TIME_INFO:
		err = m9mo_set_time_info(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_LENS_TIMER:
		err = m9mo_set_lens_off_timer(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_STREAM_PART2: /* for shutter sound */
		err = m9mo_set_mode_part2(sd, M9MO_STILLCAP_MODE);
		break;

	case V4L2_CID_CAMERA_CAPTURE_END:
		err = m9mo_set_cap_rec_end_mode(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_SEND_SETTING:
		state->factory_category = (ctrl->value) / 1000;
		state->factory_byte = (ctrl->value) % 1000;
		break;

	case V4L2_CID_CAMERA_FACTORY_SEND_VALUE:
		state->factory_value_size = 1;
		state->factory_value = ctrl->value;
		m9mo_send_factory_command_value(sd);
		break;

	case V4L2_CID_CAMERA_FACTORY_SEND_WORD_VALUE:
		state->factory_value_size = 2;
		state->factory_value = ctrl->value;
		m9mo_send_factory_command_value(sd);
		break;

	case V4L2_CID_CAMERA_FACTORY_SEND_LONG_VALUE:
		state->factory_value_size = 4;
		state->factory_value = ctrl->value;
		m9mo_send_factory_command_value(sd);
		break;

	case V4L2_CID_CAMERA_FACTORY_TILT:
		cam_trace("TILT_ONE_SCRIPT_RUN : 0x%x\n", ctrl->value);
		m9mo_set_factory_tilt(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_IR_CHECK:
		cam_trace("IR_CHECK : 0x%x\n", ctrl->value);
		m9mo_set_factory_IR_Check(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_TILT_SCAN_MIN:
		cam_trace("TILT_SCAN_MIN : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x18, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_TILT_SCAN_MAX:
		cam_trace("TILT_SCAN_MAX : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x1A, (u16)ctrl->value);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0D, 0x06);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_TILT_FIELD:
		cam_trace("TILT_FIELD : 0x%x\n", ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, (u8)ctrl->value);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0C, 0x00);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_TILT_AF_RANGE_MIN:
		cam_trace("TILT_AF_RANGE_MIN : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x18, (u16)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_TILT_AF_RANGE_MAX:
		cam_trace("TILT_AF_RANGE_MAX : 0x%x\n", ctrl->value);
		err = m9mo_writew(sd, M9MO_CATEGORY_LENS,
			0x1A, (u16)ctrl->value);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0C, 0x01);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_TILT_DIFF_RANGE_MIN:
		cam_trace("TILT_DIFF_MIN : 0x%x\n", ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1A, (u8)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_TILT_DIFF_RANGE_MAX:
		cam_trace("TILT_DIFF_MAX : 0x%x\n", ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x1B, (u8)ctrl->value);
		CHECK_ERR(err);
		err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			0x0C, 0x02);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_IR_B_GAIN_MIN:
		cam_trace("IR_B_GAIN_MIN : 0x%x\n", ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_IR_B_GAIN_MAX:
		cam_trace("IR_B_GAIN_MAX : 0x%x\n", ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_IR_R_GAIN_MIN:
		cam_trace("IR_R_GAIN_MIN : 0x%x\n", ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_IR_R_GAIN_MAX:
		cam_trace("IR_R_GAIN_MAX : 0x%x\n", ctrl->value);
		break;

	case V4L2_CID_CAMERA_FACTORY_FLASH_MAN_CHARGE:
		cam_trace("FLASH_MAN_CHARGE : 0x%x\n", ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			0x2A, (u8)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_CAMERA_FACTORY_FLASH_MAN_EN:
		cam_trace("FLASH_MAN_EN : 0x%x\n", ctrl->value);
		err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			0x3D, (u8)ctrl->value);
		CHECK_ERR(err);
		break;

	case V4L2_CID_START_CAPTURE_KIND:
		cam_trace("START_CAP_KIND : 0x%x\n", ctrl->value);
		state->start_cap_kind = ctrl->value;
		break;

	case V4L2_CID_CAMERA_INIT:
		cam_trace("MANUAL INIT launched.");
		err = m9mo_init(sd, ctrl->value);
		break;

	case V4L2_CID_CAMERA_POST_INIT:
		cam_trace("MANUAL OIS INIT launched.");
		err = m9mo_post_init(sd, ctrl->value);
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

static bool m9mo_check_postview(struct v4l2_subdev *sd)
{
	struct m9mo_state *state = to_state(sd);

	if (state->pixelformat == V4L2_COLORSPACE_JPEG
		|| state->running_capture_mode == RUNNING_MODE_LOWLIGHT
		|| state->running_capture_mode == RUNNING_MODE_HDR) {
		/* capture */
		return false;
	} else {
		/* New capture condition for resolution factory test.
		   This condition is necessary if you captured
		   YUV postview + YUV main image. */
		if (state->start_cap_kind != START_CAPTURE_POSTVIEW) {
			/* capture */
			return false;
		}
	}
	/* postview */
	return true;
}

/*
 * v4l2_subdev_video_ops
 */
static const struct m9mo_frmsizeenum *m9mo_get_frmsize
	(const struct m9mo_frmsizeenum *frmsizes, int num_entries, int index)
{
	int i;

	for (i = 0; i < num_entries; i++) {
		if (frmsizes[i].index == index)
			return &frmsizes[i];
	}

	return NULL;
}

static int m9mo_set_frmsize(struct v4l2_subdev *sd)
{
	struct m9mo_state *state = to_state(sd);
	int err;
	int read_mon_size;
	u32 size_val;

	cam_trace("E\n");

	if (state->format_mode == V4L2_PIX_FMT_MODE_PREVIEW) {
		err = m9mo_set_mode(sd, M9MO_PARMSET_MODE);
		if (err <= 0) {
			cam_err("failed to set mode\n");
			return err;
		}

		/* don't set frmsize when returning preivew after capture */
		if (err == 10)
			cam_trace("~~~~ return when CAP->PAR ~~~~\n");
		else {
		err = m9mo_readb(sd, M9MO_CATEGORY_PARM,
			M9MO_PARM_MON_SIZE, &read_mon_size);
		CHECK_ERR(err);

		if (state->fps == 60) {
			if (state->preview->height == 480)
				size_val = 0x2F;
			else if (state->preview->height == 720)
				size_val = 0x25;

			if (read_mon_size != size_val) {
				err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
					M9MO_PARM_MON_SIZE, size_val);
				CHECK_ERR(err);
			}
		} else if (state->fps == 30) {
			if (state->preview->height == 1080)
				size_val = 0x28;
			else if (state->preview->height == 720)
				size_val = 0x21;
			else if (state->preview->height == 480)
				size_val = 0x17;
			else if (state->preview->height == 240)
				size_val = 0x09;

			if (read_mon_size != size_val) {
				err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
					M9MO_PARM_MON_SIZE, size_val);
				CHECK_ERR(err);
			}
		} else {
			if (read_mon_size != state->preview->reg_val) {
				err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
					M9MO_PARM_MON_SIZE,
					state->preview->reg_val);
				CHECK_ERR(err);
			}
		}

		m9mo_set_gamma(sd);
		m9mo_set_iqgrp(sd, 0);

		m9mo_set_dual_capture_mode(sd, 0);

		}
		cam_err("preview frame size %dx%d\n",
			state->preview->width, state->preview->height);
	} else {
		if (!m9mo_check_postview(sd)) {
			if (!state->dual_capture_start) {
				err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
						M9MO_CAPPARM_MAIN_IMG_SIZE,
						state->capture->reg_val);
				CHECK_ERR(err);
				if (state->smart_zoom_mode)
					m9mo_set_smart_zoom(sd,
						state->smart_zoom_mode);
			}
			cam_info("capture frame size %dx%d\n",
					state->capture->width,
					state->capture->height);
		} else {
			if (!state->fast_capture_set) {
				err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
						M9MO_CAPPARM_PREVIEW_IMG_SIZE,
						state->postview->reg_val);
				CHECK_ERR(err);
				cam_info("postview frame size %dx%d\n",
						state->postview->width,
						state->postview->height);
			}
		}
	}
	cam_trace("X\n");
	return 0;
}

static int m9mo_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *ffmt)
{
	struct m9mo_state *state = to_state(sd);
	const struct m9mo_frmsizeenum **frmsize;

	u32 width = ffmt->width;
	u32 height = ffmt->height;
	u32 old_index;
	int i, num_entries;

	cam_trace("E\n");

	if (unlikely(state->isp.bad_fw)) {
		cam_err("\"Unknown\" state, please update F/W");
		return -ENOSYS;
	}

	state->format_mode = ffmt->field;
	state->pixelformat = ffmt->colorspace;

	if (state->format_mode == V4L2_PIX_FMT_MODE_PREVIEW)
		frmsize = &state->preview;
	else if (!m9mo_check_postview(sd))
		frmsize = &state->capture;
	else
		frmsize = &state->postview;

	old_index = *frmsize ? (*frmsize)->index : -1;
	*frmsize = NULL;

	if (state->format_mode == V4L2_PIX_FMT_MODE_PREVIEW) {
		num_entries = ARRAY_SIZE(preview_frmsizes);
		for (i = 0; i < num_entries; i++) {
			if (width == preview_frmsizes[i].width &&
				height == preview_frmsizes[i].height) {
				*frmsize = &preview_frmsizes[i];
				break;
			}
		}
	} else {
		if (!m9mo_check_postview(sd)) {
			num_entries = ARRAY_SIZE(capture_frmsizes);
			for (i = 0; i < num_entries; i++) {
				if (width == capture_frmsizes[i].width &&
					height == capture_frmsizes[i].height) {
					*frmsize = &capture_frmsizes[i];
					break;
				}
			}
		} else {
			num_entries = ARRAY_SIZE(postview_frmsizes);
			for (i = 0; i < num_entries; i++) {
				if (width == postview_frmsizes[i].width &&
					height == postview_frmsizes[i].height) {
					*frmsize = &postview_frmsizes[i];
					break;
				}
			}
		}
	}

	if (*frmsize == NULL) {
		cam_warn("invalid frame size %dx%d\n", width, height);
		if (state->format_mode == V4L2_PIX_FMT_MODE_PREVIEW)
			*frmsize = m9mo_get_frmsize(preview_frmsizes,
				num_entries, M9MO_PREVIEW_720P);
		else if (!m9mo_check_postview(sd))
			*frmsize = m9mo_get_frmsize(capture_frmsizes,
				num_entries, M9MO_CAPTURE_12MPW);
		else
			*frmsize = m9mo_get_frmsize(postview_frmsizes,
				num_entries, M9MO_CAPTURE_POSTWHD);
	}

	cam_err("%dx%d\n", (*frmsize)->width, (*frmsize)->height);
	m9mo_set_frmsize(sd);

	cam_trace("X\n");
	return 0;
}

static int m9mo_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct m9mo_state *state = to_state(sd);

	a->parm.capture.timeperframe.numerator = 1;
	a->parm.capture.timeperframe.denominator = state->fps;

	return 0;
}

static int m9mo_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *a)
{
	struct m9mo_state *state = to_state(sd);
	/*int err;*/

	u32 fps = a->parm.capture.timeperframe.denominator /
					a->parm.capture.timeperframe.numerator;

	if (unlikely(state->isp.bad_fw)) {
		cam_err("\"Unknown\" state, please update F/W");
		return -ENOSYS;
	}

	if (fps != state->fps) {
		if (fps <= 0 || fps > 120) {
			cam_err("invalid frame rate %d\n", fps);
			fps = 0; /* set to auto(default) */
		}
	}

	cam_info("%s: X, fps = %d\n", __func__, fps);
	return 0;
}

static int m9mo_enum_framesizes(struct v4l2_subdev *sd,
	struct v4l2_frmsizeenum *fsize)
{
	struct m9mo_state *state = to_state(sd);

	/*
	* The camera interface should read this value, this is the resolution
	* at which the sensor would provide framedata to the camera i/f
	* In case of image capture,
	* this returns the default camera resolution (VGA)
	*/
	if (state->format_mode == V4L2_PIX_FMT_MODE_PREVIEW) {
		if (state->preview == NULL
				/* FIXME || state->preview->index < 0 */)
			return -EINVAL;

		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = state->preview->width;
		fsize->discrete.height = state->preview->height;
	} else if (!m9mo_check_postview(sd)) {
		if (state->capture == NULL
				/* FIXME || state->capture->index < 0 */)
			return -EINVAL;

		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = state->capture->width;
		fsize->discrete.height = state->capture->height;
	} else {
		if (state->postview == NULL
				/* FIXME || state->postview->index < 0 */)
			return -EINVAL;

		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = state->postview->width;
		fsize->discrete.height = state->postview->height;
	}

	return 0;
}

static int m9mo_s_stream_preview(struct v4l2_subdev *sd, int enable)
{
	struct m9mo_state *state = to_state(sd);
	struct v4l2_control ctrl;
	u32 old_mode, int_factor;
	int err;

	if (enable) {
		if (state->vt_mode) {
			err = m9mo_writeb(sd, M9MO_CATEGORY_AE,
					M9MO_AE_EP_MODE_MON, 0x11);
			CHECK_ERR(err);
		}

		old_mode = m9mo_set_mode(sd, M9MO_MONITOR_MODE);
		if (old_mode <= 0) {
			cam_err("failed to set mode\n");
			return old_mode;
		}

		if (old_mode != M9MO_MONITOR_MODE) {
			int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
			if (!(int_factor & M9MO_INT_MODE)) {
				cam_err("M9MO_INT_MODE isn't issued, %#x\n",
					int_factor);
				return -ETIMEDOUT;
			}
		}

		if (state->zoom >= 0x0F) {
			/* Zoom position returns to 1
			   when the monitor size is changed. */
			ctrl.id = V4L2_CID_CAMERA_ZOOM;
			ctrl.value = state->zoom;
			m9mo_set_zoom(sd, &ctrl);
		}
		if (state->smart_zoom_mode)
			m9mo_set_smart_zoom(sd, state->smart_zoom_mode);

		m9mo_set_lock(sd, 0);
	} else {
	}

	return 0;
}

static int m9mo_s_stream_capture(struct v4l2_subdev *sd, int enable)
{
	/*u32 int_factor;*/
	int err;
	struct m9mo_state *state = to_state(sd);

	if (enable) {
		if (state->running_capture_mode == RUNNING_MODE_SINGLE) {
#ifndef FAST_CAPTURE
			m9mo_set_mode_part1(sd, M9MO_STILLCAP_MODE);
#else
			if (state->factory_test_num != 0)
				m9mo_set_mode_part1(sd, M9MO_STILLCAP_MODE);
			state->fast_capture_set = 0;
#endif
		} else {
			err = m9mo_set_mode(sd, M9MO_STILLCAP_MODE);
			if (err <= 0) {
				cam_err("failed to set mode\n");
				return err;
			}
		}
/*
		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_SOUND)) {
			cam_err("M9MO_INT_SOUND isn't issued, %#x\n",
				int_factor);
			return -ETIMEDOUT;
		}
*/
	}
	return 0;
}

static int m9mo_s_stream_hdr(struct v4l2_subdev *sd, int enable)
{
	struct m9mo_state *state = to_state(sd);
	int int_en, int_factor, i, err;

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPCTRL,
		M9MO_CAPCTRL_CAP_MODE, enable ? 0x06 : 0x00);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
		M9MO_CAPPARM_YUVOUT_MAIN, enable ? 0x00 : 0x21);
		CHECK_ERR(err);

	err = m9mo_readw(sd, M9MO_CATEGORY_SYS, M9MO_SYS_INT_EN, &int_en);
		CHECK_ERR(err);

	if (enable)
		int_en |= M9MO_INT_FRAME_SYNC;
	else
		int_en &= ~M9MO_INT_FRAME_SYNC;

	err = m9mo_writew(sd, M9MO_CATEGORY_SYS, M9MO_SYS_INT_EN, int_en);
		CHECK_ERR(err);

	if (enable) {
		err = m9mo_set_mode(sd, M9MO_STILLCAP_MODE);
		if (err <= 0) {
			cam_err("failed to set mode\n");
			return err;
		}

		/* convert raw to jpeg by the image data processing and
		   store memory on ISP and
		   receive preview jpeg image from ISP */
		for (i = 0; i < 3; i++) {
			int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
			if (!(int_factor & M9MO_INT_FRAME_SYNC)) {
				cam_err("M9MO_INT_FRAME_SYNC isn't issued, %#x\n",
					int_factor);
				return -ETIMEDOUT;
			}
		}

		/* stop ring-buffer */
		if (!(state->isp.int_factor & M9MO_INT_CAPTURE)) {
			/* FIXME - M9MO_INT_FRAME_SYNC interrupt
			   should be issued just three times */
			for (i = 0; i < 9; i++) {
				int_factor = m9mo_wait_interrupt(sd,
						M9MO_ISP_TIMEOUT);
				if (int_factor & M9MO_INT_CAPTURE)
					break;

				cam_err("M9MO_INT_CAPTURE isn't issued, %#x\n",
						int_factor);
			}
		}
	} else {
	}
	return 0;
}

static int m9mo_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct m9mo_state *state = to_state(sd);
	int err = 0;

	cam_trace("E\n");

	if (unlikely(state->isp.bad_fw)) {
		cam_err("\"Unknown\" state, please update F/W");
		return -ENOSYS;
	}

	cam_info("state->format_mode=%d\n", state->format_mode);

	if (state->running_capture_mode == RUNNING_MODE_BURST
			&& state->mburst_start) {
		cam_trace("X\n");
		return 0;
	}

	switch (enable) {
	case STREAM_MODE_CAM_ON:
	case STREAM_MODE_CAM_OFF:
		switch (state->format_mode) {
		case V4L2_PIX_FMT_MODE_CAPTURE:
			cam_info("capture %s",
				enable == STREAM_MODE_CAM_ON ? "on" : "off");
			err = m9mo_s_stream_capture(sd,
					enable == STREAM_MODE_CAM_ON);
			break;
		case V4L2_PIX_FMT_MODE_HDR:
			err = m9mo_s_stream_hdr(sd,
					enable == STREAM_MODE_CAM_ON);
			break;
		default:
			cam_err("preview %s",
				enable == STREAM_MODE_CAM_ON ? "on" : "off");
			err = m9mo_s_stream_preview(sd,
					enable == STREAM_MODE_CAM_ON);
			break;
		}
		break;

	case STREAM_MODE_MOVIE_ON:
		state->recording = 1;
#if 0	/* Not use S project */
		if (state->flash_mode != FLASH_MODE_OFF)
			err = m9mo_set_flash(sd, state->flash_mode, 1);
#endif

		if (state->preview->index == M9MO_PREVIEW_720P ||
				state->preview->index == M9MO_PREVIEW_1080P)
			err = m9mo_set_af(sd, 1);
		break;

	case STREAM_MODE_MOVIE_OFF:
		if (state->preview->index == M9MO_PREVIEW_720P ||
				state->preview->index == M9MO_PREVIEW_1080P)
			err = m9mo_set_af(sd, 0);

#if 0	/* Not use S project */
		m9mo_set_flash(sd, FLASH_MODE_OFF, 1);
#endif

		state->recording = 0;
		break;

	default:
		cam_err("invalid stream option, %d\n", enable);
		break;
	}

	cam_trace("X\n");
	return err;
}

static int m9mo_check_version(struct v4l2_subdev *sd)
{
	struct m9mo_state *state = to_state(sd);
	int i, val;

	for (i = 0; i < 6; i++) {
		m9mo_readb(sd, M9MO_CATEGORY_SYS, M9MO_SYS_USER_VER, &val);
		state->exif.unique_id[i] = (char)val;
	}
	state->exif.unique_id[i] = '\0';

	cam_info("*************************************\n");
	cam_info("F/W Version: %s\n", state->exif.unique_id);
	cam_dbg("Binary Released: %s %s\n", __DATE__, __TIME__);
	cam_info("*************************************\n");

	return 0;
}

static int m9mo_init_param(struct v4l2_subdev *sd)
{
	struct m9mo_state *state = to_state(sd);
	int err;
	cam_trace("E\n");

	err = m9mo_writew(sd, M9MO_CATEGORY_SYS, M9MO_SYS_INT_EN,
			M9MO_INT_MODE | M9MO_INT_CAPTURE | M9MO_INT_FRAME_SYNC
			| M9MO_INT_ATSCENE_UPDATE
			| M9MO_INT_SOUND);
	CHECK_ERR(err);

	err = m9mo_writeb(sd, M9MO_CATEGORY_PARM,
			M9MO_PARM_OUT_SEL, 0x02);
	CHECK_ERR(err);

	/* Capture */
	err = m9mo_writeb(sd, M9MO_CATEGORY_CAPPARM,
			M9MO_CAPPARM_YUVOUT_MAIN, 0x01);
	CHECK_ERR(err);

	m9mo_set_sensor_mode(sd, state->sensor_mode);

	cam_trace("X\n");
	return 0;
}

static int m9mo_ois_init(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	const struct m9mo_platform_data *pdata = client->dev.platform_data;
	struct m9mo_state *state = to_state(sd);
	u32 int_factor, int_en, err, ois_result;
	int try_cnt = 2;

	cam_dbg("E\n");

	err = m9mo_readw(sd, M9MO_CATEGORY_SYS, M9MO_SYS_INT_EN, &int_en);
	CHECK_ERR(err);

	/* enable OIS_INIT interrupt */
	int_en |= M9MO_INT_OIS_INIT;
	/* enable LENS_INIT interrupt */
	int_en |= M9MO_INT_LENS_INIT;

	err = m9mo_writew(sd, M9MO_CATEGORY_SYS, M9MO_SYS_INT_EN, int_en);
	CHECK_ERR(err);

	do {
		/* OIS on set */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
				0x10, 0x01);
		CHECK_ERR(err);

		/* OIS F/W download, boot */
		err = m9mo_writeb(sd, M9MO_CATEGORY_NEW,
				0x11, 0x00);

		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_OIS_INIT)) {
			cam_err("OIS interrupt not issued\n");
			if (try_cnt > 1) {
				try_cnt--;
				pdata->config_sambaz(0);
				msleep(20);
				if (try_cnt == 1)
					pdata->config_sambaz(1);
				continue;
			}
			state->isp.bad_fw = 1;
			return -ENOSYS;
		}
		cam_info("OIS init complete\n");

		/* Read OIS result */
		m9mo_readb(sd, M9MO_CATEGORY_NEW, 0x17, &ois_result);
		cam_info("ois result = %d", ois_result);
		if (ois_result != 0x02) {
			try_cnt--;
			pdata->config_sambaz(0);
			msleep(20);
			if (try_cnt == 1)
				pdata->config_sambaz(1);
		} else
			try_cnt = 0;
	} while (try_cnt);

	/* Lens boot */
	err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_AF_INITIAL, 0x00);
	int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
	if (!(int_factor & M9MO_INT_LENS_INIT)) {
		cam_err("M9MO_INT_LENS_INIT isn't issued, %#x\n",
				int_factor);
		return -ETIMEDOUT;
	}

	cam_dbg("X\n");

	return err;
}

static int m9mo_init(struct v4l2_subdev *sd, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	const struct m9mo_platform_data *pdata = client->dev.platform_data;
	struct m9mo_state *state = to_state(sd);
	u32 int_factor;
	/*u32 value;*/
	int err;

	cam_dbg("E : val = %d\n", val);

	/* Default state values */
	state->preview = NULL;
	state->capture = NULL;
	state->postview = NULL;

	state->format_mode = V4L2_PIX_FMT_MODE_PREVIEW;
	state->sensor_mode = SENSOR_CAMERA;
	state->flash_mode = FLASH_MODE_OFF;
	state->scene_mode = SCENE_MODE_NONE;

	state->face_beauty = 0;

	state->fps = 0;			/* auto */

	state->isp.bad_fw = 0;
	state->isp.issued = 0;

	state->zoom = 0;
	state->smart_zoom_mode = 0;

	state->fast_capture_set = 0;

	state->vss_mode = 0;
	state->dual_capture_start = 0;
	state->dual_capture_frame = 1;
	state->focus_area_mode = V4L2_FOCUS_AREA_CENTER;

	state->bracket_wbb_val = BRACKET_WBB_VALUE3;  /* AB -+1 */
	state->wb_custom_rg = 424; /* 1A8 */
	state->wb_custom_bg = 452; /* 1C4 */

	state->color_effect = 0;
	state->gamma_rgb_mon = 2;
	state->gamma_rgb_cap = 2;
	state->gamma_tbl_rgb_cap = 1;
	state->gamma_tbl_rgb_mon = 1;

	state->mburst_start = false;

	memset(&state->focus, 0, sizeof(state->focus));

#ifdef HOLD_LENS_SUPPORT
	if (!leave_power) {
		/* SambaZ PLL enable */
		cam_dbg("SambaZ On start ~~~\n");
		pdata->config_sambaz(1);
		cam_dbg("SambaZ On finish ~~~\n");

		if (system_rev > 0) {
			err = m9mo_writel(sd, M9MO_CATEGORY_FLASH,
					0x0C, 0x27c00020);
		}

		/* start camera program(parallel FLASH ROM) */
		cam_info("write 0x0f, 0x12~~~\n");
		err = m9mo_writeb(sd, M9MO_CATEGORY_FLASH,
				M9MO_FLASH_CAM_START, 0x01);
		CHECK_ERR(err);

		int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
		if (!(int_factor & M9MO_INT_MODE)) {
			cam_err("firmware was erased?\n");
			state->isp.bad_fw = 1;
			return -ENOSYS;
		}
		cam_info("ISP boot complete\n");
	}
#else
	/* SambaZ PLL enable */
	cam_dbg("SambaZ On start ~~~\n");
	pdata->config_sambaz(1);
	cam_dbg("SambaZ On finish ~~~\n");

	if (system_rev > 0) {
		err = m9mo_writel(sd, M9MO_CATEGORY_FLASH,
				0x0C, 0x27c00020);
	}

	/* start camera program(parallel FLASH ROM) */
	cam_info("write 0x0f, 0x12~~~\n");
	err = m9mo_writeb(sd, M9MO_CATEGORY_FLASH,
			M9MO_FLASH_CAM_START, 0x01);
	CHECK_ERR(err);

	int_factor = m9mo_wait_interrupt(sd, M9MO_ISP_TIMEOUT);
	if (!(int_factor & M9MO_INT_MODE)) {
		cam_err("firmware was erased?\n");
		state->isp.bad_fw = 1;
		return -ENOSYS;
	}
	cam_info("ISP boot complete\n");
#endif

	/* check up F/W version */
	err = m9mo_check_fw(sd);
	cam_info("M9MO init complete\n");

	return 0;
}

static int m9mo_post_init(struct v4l2_subdev *sd, u32 val)
{
	int err;

	cam_info("post init E");
	cam_info("Thermistor val: True(0~40C) or False = %d\n", val);

	err = m9mo_writeb(sd, M9MO_CATEGORY_LENS,
			M9MO_LENS_AF_TEMP_INDICATE, val);
	CHECK_ERR(err);

#ifdef HOLD_LENS_SUPPORT
	if (!leave_power) {
		m9mo_init_param(sd);
		m9mo_ois_init(sd);
	}
#else
	m9mo_init_param(sd);
	m9mo_ois_init(sd);
#endif

#ifdef HOLD_LENS_SUPPORT
	leave_power = false;
#endif

	cam_info("Lens boot complete - M9MO post init complete\n");

	return 0;
}

static const struct v4l2_subdev_core_ops m9mo_core_ops = {
	.init = m9mo_init,		/* initializing API */
	.load_fw = m9mo_load_fw_main,
	.queryctrl = m9mo_queryctrl,
	.g_ctrl = m9mo_g_ctrl,
	.s_ctrl = m9mo_s_ctrl,
	.g_ext_ctrls = m9mo_g_ext_ctrls,
	.s_ext_ctrls = m9mo_s_ext_ctrls,
};

static const struct v4l2_subdev_video_ops m9mo_video_ops = {
	.s_mbus_fmt = m9mo_s_fmt,
	.g_parm = m9mo_g_parm,
	.s_parm = m9mo_s_parm,
	.enum_framesizes = m9mo_enum_framesizes,
	.s_stream = m9mo_s_stream,
};

static const struct v4l2_subdev_ops m9mo_ops = {
	.core = &m9mo_core_ops,
	.video = &m9mo_video_ops,
};

static ssize_t m9mo_camera_type_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", sysfs_sensor_type);
}

static ssize_t m9mo_camera_fw_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s %s\n", sysfs_phone_fw, sysfs_sensor_fw);
}

static DEVICE_ATTR(rear_camtype, S_IRUGO, m9mo_camera_type_show, NULL);
static DEVICE_ATTR(rear_camfw, S_IRUGO, m9mo_camera_fw_show, NULL);

/*
 * m9mo_probe
 * Fetching platform data is being done with s_config subdev call.
 * In probe routine, we just register subdev device
 */
static int __devinit m9mo_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct m9mo_state *state;
	struct v4l2_subdev *sd;

	const struct m9mo_platform_data *pdata = client->dev.platform_data;
	int err = 0;

	state = kzalloc(sizeof(struct m9mo_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	sd = &state->sd;
	strcpy(sd->name, M9MO_DRIVER_NAME);

	/* Registering subdev */
	v4l2_i2c_subdev_init(sd, client, &m9mo_ops);

#ifdef CAM_DEBUG
	state->dbg_level = CAM_TRACE | CAM_DEBUG | CAM_I2C;
#endif

#ifdef M9MO_ISP_DEBUG
	state->dbg_level = CAM_TRACE | CAM_DEBUG | CAM_I2C;
#endif

#ifdef M9MO_BUS_FREQ_LOCK
	dev_lock(bus_dev, m9mo_dev, 400200);
#endif

	/* wait queue initialize */
	init_waitqueue_head(&state->isp.wait);

	if (pdata->config_isp_irq)
		pdata->config_isp_irq();

	err = request_irq(pdata->irq,
		m9mo_isp_isr, IRQF_TRIGGER_RISING, "m9mo isp", sd);
	if (err) {
		cam_err("failed to request irq ~~~~~~~~~~~~~\n");
		return err;
	}
	state->isp.irq = pdata->irq;
	state->isp.issued = 0;

	cam_dbg("%s\n", __func__);

	return 0;
}

#ifdef M9MO_ISP_DEBUG
static int m9mo_LogNo;
#endif
static int __devexit m9mo_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct m9mo_state *state = to_state(sd);
	int err = 0;
	/*int err;*/

#ifdef HOLD_LENS_SUPPORT
	if (!leave_power) {
#ifdef M9MO_ISP_DEBUG
		char filename[32];
		sprintf(filename, "_ISP_%06d.LOG%c", ++m9mo_LogNo, 0);
		m9mo_makeLog(sd, filename);
#endif
		if (m9mo_set_lens_off(sd) < 0)
			cam_err("failed to set m9mo_set_lens_off~~~~~\n");
	} else {
		m9mo_set_capture_mode(sd, RUNNING_MODE_SINGLE);
	}
#else
#ifdef M9MO_ISP_DEBUG
	char filename[32];
	sprintf(filename, "_ISP_%06d.LOG%c", ++m9mo_LogNo, 0);
	m9mo_makeLog(sd, filename);
#endif
	if (m9mo_set_lens_off(sd) < 0)
		cam_err("failed to set m9mo_set_lens_off~~~~~\n");
#endif

#ifdef HOLD_LENS_SUPPORT
	if (leave_power) {
		err = m9mo_set_lens_off_timer(sd, 0);
		CHECK_ERR(err);

		/*err = m9mo_set_mode(sd, M9MO_PARMSET_MODE);
		CHECK_ERR(err);*/
	}
#else
	err = m9mo_set_lens_off_timer(sd, 0);
	CHECK_ERR(err);
#endif

	if (state->isp.irq > 0)
		free_irq(state->isp.irq, sd);

	v4l2_device_unregister_subdev(sd);

#if 0
	kfree(state->sensor_type);
#endif
#ifdef M9MO_BUS_FREQ_LOCK
	dev_unlock(bus_dev, m9mo_dev);
#endif
	kfree(state);

	return 0;
}

static const struct i2c_device_id m9mo_id[] = {
	{ M9MO_DRIVER_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, m9mo_id);

static struct i2c_driver m9mo_i2c_driver = {
	.driver = {
		.name	= M9MO_DRIVER_NAME,
	},
	.probe		= m9mo_probe,
	.remove		= __devexit_p(m9mo_remove),
	.id_table	= m9mo_id,
};

static int __init m9mo_mod_init(void)
{
#ifdef M9MO_BUS_FREQ_LOCK
	bus_dev = dev_get("exynos-busfreq");
#endif
	if (!m9mo_dev) {
		m9mo_dev =
		device_create(camera_class, NULL, 0, NULL, "rear");
		if (IS_ERR(m9mo_dev)) {
			cam_err("failed to create device m9mo_dev!\n");
			return 0;
		}
		if (device_create_file
		(m9mo_dev, &dev_attr_rear_camtype) < 0) {
			cam_err("failed to create device file, %s\n",
			dev_attr_rear_camtype.attr.name);
		}
		if (device_create_file
		(m9mo_dev, &dev_attr_rear_camfw) < 0) {
			cam_err("failed to create device file, %s\n",
			dev_attr_rear_camfw.attr.name);
		}
	}
	return i2c_add_driver(&m9mo_i2c_driver);
}

static void __exit m9mo_mod_exit(void)
{
	i2c_del_driver(&m9mo_i2c_driver);
}
module_init(m9mo_mod_init);
module_exit(m9mo_mod_exit);

MODULE_DESCRIPTION("driver for Fusitju M9MO LS 16MP camera");
MODULE_LICENSE("GPL");
