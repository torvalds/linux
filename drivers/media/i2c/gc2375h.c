// SPDX-License-Identifier: GPL-2.0
/*
 * gc2375h driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 init driver.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 add enum_frame_interval function.
 * TODO: add OTP function.
 * V0.0X01.0X04 add quick stream on/off
 * V0.0X01.0X05 add function g_mbus_config
 * V0.0X01.0X06 fix vblank set issue
 */

//#define DEBUG 1
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_gpio.h>

#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x6)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIPI_FREQ		338000000
/* pixel rate = link frequency * 1 * lanes / BITS_PER_SAMPLE */
#define GC2375H_PIXEL_RATE		(MIPI_FREQ * 2LL * 1LL / 10)
#define GC2375H_XVCLK_FREQ		24000000

#define CHIP_ID				0x2375
#define GC2375H_REG_CHIP_ID_H		0xf0
#define GC2375H_REG_CHIP_ID_L		0xf1
#define SENSOR_ID(_msb, _lsb)		((_msb) << 8 | (_lsb))

#define GC2375H_REG_SET_PAGE     0xfe
#define GC2375H_SET_PAGE_ONE     0x00

#define GC2375H_PAGE_SELECT		0xfe
#define GC2375H_MODE_SELECT		0xef
#define GC2375H_MODE_SW_STANDBY		0x00
#define GC2375H_MODE_STREAMING		0x90

#define GC2375H_REG_EXPOSURE_H		0x03
#define GC2375H_REG_EXPOSURE_L		0x04
#define	GC2375H_EXPOSURE_MIN		4
#define	GC2375H_EXPOSURE_STEP		1
#define GC2375H_VTS_MAX			0x7fff

#define GC2375H_ANALOG_GAIN_1 64    /*1.00x*/
#define GC2375H_ANALOG_GAIN_2 88    /*1.375x*/
#define GC2375H_ANALOG_GAIN_3 122   /*1.90x*/
#define GC2375H_ANALOG_GAIN_4 168   /*2.625x*/
#define GC2375H_ANALOG_GAIN_5 239   /*3.738x*/
#define GC2375H_ANALOG_GAIN_6 330   /*5.163x*/
#define GC2375H_ANALOG_GAIN_7 470   /*7.350x*/
#define GC2375H_ANALOG_GAIN_8 725  // 11.34x
#define GC2375H_ANALOG_GAIN_9 1038 // 16.23x

#define GC2375H_ANALOG_GAIN_REG		0xb6
#define GC2375H_PREGAIN_H_REG		0xb1
#define GC2375H_PREGAIN_L_REG		0xb2

#define GC2375H_GAIN_MIN			0x40
#define GC2375H_GAIN_MAX			0x200
#define GC2375H_GAIN_STEP		1
#define GC2375H_GAIN_DEFAULT		0x80

#define GC2375H_REG_VTS_H			0x07
#define GC2375H_REG_VTS_L			0x08

#define GC2375_MIRROR_NORMAL
//#define GC2375_MIRROR_H
//#define GC2375_MIRROR_V
//#define GC2375_MIRROR_HV

#if defined(GC2375_MIRROR_NORMAL)
#define MIRROR 0xd4
#define BLK_Select1_H 0x00
#define BLK_Select1_L 0x3c
#define BLK_Select2_H 0x00
#define BLK_Select2_L 0x03

#elif defined(GC2375_MIRROR_H)
#define MIRROR 0xd5
#define BLK_Select1_H 0x00
#define BLK_Select1_L 0x3c
#define BLK_Select2_H 0x00
#define BLK_Select2_L 0x03

#elif defined(GC2375_MIRROR_V)
#define MIRROR 0xd6
#define BLK_Select1_H 0x3c
#define BLK_Select1_L 0x00
#define BLK_Select2_H 0xc0
#define BLK_Select2_L 0x00

#elif defined(GC2375_MIRROR_HV)
#define MIRROR 0xd7
#define BLK_Select1_H 0x3c
#define BLK_Select1_L 0x00
#define BLK_Select2_H 0xc0
#define BLK_Select2_L 0x00

#else
#define MIRROR 0xd4
#define BLK_Select1_H 0x00
#define BLK_Select1_L 0x3c
#define BLK_Select2_H 0x00
#define BLK_Select2_L 0x03

#endif

//#define GC2375_OLD_SETTING

#define REG_NULL			0xFFFF

#define GC2375H_LANES			1
#define GC2375H_BITS_PER_SAMPLE		10

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define GC2375H_NAME			"gc2375h"

static const char * const gc2375h_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define GC2375H_NUM_SUPPLIES ARRAY_SIZE(gc2375h_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct gc2375h_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct gc2375h {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[GC2375H_NUM_SUPPLIES];

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct gc2375h_mode *cur_mode;
	unsigned int	lane_num;
	unsigned int	cfg_num;
	unsigned int	pixel_rate;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_gc2375h(sd) container_of(sd, struct gc2375h, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval gc2375h_global_regs[] = {
#ifdef GC2375_OLD_SETTING
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xf7, 0x01},
	{0xf8, 0x0c},
	{0xf9, 0x42},
	{0xfa, 0x88},
	{0xfc, 0x8e},
	{0xfe, 0x00},
	{0x88, 0x03},
	{0x03, 0x04},
	{0x04, 0x65},
	{0x05, 0x02},
	{0x06, 0x5a},
	{0x07, 0x00},
	{0x08, 0x10},
	{0x09, 0x00},
	{0x0a, 0x08},
	{0x0b, 0x00},
	{0x0c, 0x18},
	{0x0d, 0x04},
	{0x0e, 0xb8},
	{0x0f, 0x06},
	{0x10, 0x48},
	{0x17, 0xd4},
	{0x1c, 0x10},
	{0x1d, 0x13},
	{0x20, 0x0b},
	{0x21, 0x6d},
	{0x22, 0x0c},
	{0x25, 0xc1},
	{0x26, 0x0e},
	{0x27, 0x22},
	{0x29, 0x5f},
	{0x2b, 0x88},
	{0x2f, 0x12},
	{0x38, 0x86},
	{0x3d, 0x00},
	{0xcd, 0xa3},
	{0xce, 0x57},
	{0xd0, 0x09},
	{0xd1, 0xca},
	{0xd2, 0x34},
	{0xd3, 0xbb},
	{0xd8, 0x60},
	{0xe0, 0x08},
	{0xe1, 0x1f},
	{0xe4, 0xf8},
	{0xe5, 0x0c},
	{0xe6, 0x10},
	{0xe7, 0xcc},
	{0xe8, 0x02},
	{0xe9, 0x01},
	{0xea, 0x02},
	{0xeb, 0x01},
	{0x90, 0x01},
	{0x92, 0x02},
	{0x94, 0x00},
	{0x95, 0x04},
	{0x96, 0xb0},
	{0x97, 0x06},
	{0x98, 0x40},
	{0x18, 0x02},
	{0x1a, 0x18},
	{0x28, 0x00},
	{0x3f, 0x40},
	{0x40, 0x26},
	{0x41, 0x00},
	{0x43, 0x03},
	{0x4a, 0x00},
	{0x4e, 0x3c},
	{0x4f, 0x00},
	{0x66, 0xc0},
	{0x67, 0x00},
	{0x68, 0x00},
	{0xb0, 0x58},
	{0xb1, 0x01},
	{0xb2, 0x00},
	{0xb6, 0x00},
	{0xef, 0x90},
	{0xfe, 0x03},
	{0x01, 0x03},
	{0x02, 0x33},
	{0x03, 0x90},
	{0x04, 0x04},
	{0x05, 0x00},
	{0x06, 0x80},
	{0x11, 0x2b},
	{0x12, 0xd0},
	{0x13, 0x07},
	{0x15, 0x00},
	{0x21, 0x08},
	{0x22, 0x05},
	{0x23, 0x13},
	{0x24, 0x02},
	{0x25, 0x13},
	{0x26, 0x08},
	{0x29, 0x06},
	{0x2a, 0x08},
	{0x2b, 0x08},
	{0xfe, 0x00},
	{0x20, 0x0b},
	{0x22, 0x0c},
	{0x26, 0x0e},
	{0xb6, 0x00},
#else
	/*System*/
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xfe, 0x00},
	{0xf7, 0x01},
	{0xf8, 0x0c},
	{0xf9, 0x42},
	{0xfa, 0x88},
	{0xfc, 0x8e},
	{0xfe, 0x00},
	{0x88, 0x03},
	/*Analog*/
	{0xfe, 0x00},
	{0x03, 0x04},
	{0x04, 0x65},
	{0x05, 0x02},
	{0x06, 0x5a},
	{0x07, 0x00},
	{0x08, 0x10},
	{0x09, 0x00},
	{0x0a, 0x04},
	{0x0b, 0x00},
	{0x0c, 0x14},
	{0x0d, 0x04},
	{0x0e, 0xb8},
	{0x0f, 0x06},
	{0x10, 0x48},
	{0x17, MIRROR},
	{0x1c, 0x10},
	{0x1d, 0x13},
	{0x20, 0x0b},
	{0x21, 0x6d},
	{0x22, 0x0c},
	{0x25, 0xc1},
	{0x26, 0x0e},
	{0x27, 0x22},
	{0x29, 0x5f},
	{0x2b, 0x88},
	{0x2f, 0x12},
	{0x38, 0x86},
	{0x3d, 0x00},
	{0xcd, 0xa3},
	{0xce, 0x57},
	{0xd0, 0x09},
	{0xd1, 0xca},
	{0xd2, 0x34},
	{0xd3, 0xbb},
	{0xd8, 0x60},
	{0xe0, 0x08},
	{0xe1, 0x1f},
	{0xe4, 0xf8},
	{0xe5, 0x0c},
	{0xe6, 0x10},
	{0xe7, 0xcc},
	{0xe8, 0x02},
	{0xe9, 0x01},
	{0xea, 0x02},
	{0xeb, 0x01},
	/*Crop*/
	{0x90, 0x01},
	{0x92, 0x04},
	{0x94, 0x04},
	{0x95, 0x04},
	{0x96, 0xb0},
	{0x97, 0x06},
	{0x98, 0x40},
	/*BLK*/
	{0x18, 0x02},
	{0x1a, 0x18},
	{0x28, 0x00},
	{0x3f, 0x40},
	{0x40, 0x26},
	{0x41, 0x00},
	{0x43, 0x03},
	{0x4a, 0x00},
	{0x4e, BLK_Select1_H},
	{0x4f, BLK_Select1_L},
	{0x66, BLK_Select2_H},
	{0x67, BLK_Select2_L},
	/*Dark sun*/
	{0x68, 0x00},
	/*Gain*/
	{0xb0, 0x58},
	{0xb1, 0x01},
	{0xb2, 0x00},
	{0xb6, 0x00},
	/*MIPI*/
	{0xef, 0x00},
	{0xfe, 0x03},
	{0x01, 0x03},
	{0x02, 0x33},
	{0x03, 0x90},
	{0x04, 0x04},
	{0x05, 0x00},
	{0x06, 0x80},
	{0x11, 0x2b},
	{0x12, 0xd0},
	{0x13, 0x07},
	{0x15, 0x00},
	{0x21, 0x08},
	{0x22, 0x05},
	{0x23, 0x13},
	{0x24, 0x02},
	{0x25, 0x13},
	{0x26, 0x08},
	{0x29, 0x06},
	{0x2a, 0x08},
	{0x2b, 0x08},
	{0xfe, 0x00},
#endif
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 1008Mbps
 */
static const struct regval gc2375h_1600x1200_regs[] = {
	{REG_NULL, 0x00},
};

static const struct gc2375h_mode supported_modes_1lane[] = {
	{
		.width = 1600,
		.height = 1200,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x04d0,
		.hts_def = 0x0820,
		.vts_def = 0x04d9,
		.reg_list = gc2375h_1600x1200_regs,
	},
};

static const struct gc2375h_mode *supported_modes;

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ
};

/* sensor register write */
static int gc2375h_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	dev_dbg(&client->dev, "%s(%d) enter!\n", __func__, __LINE__);
	dev_dbg(&client->dev, "gc2375h write reg(0x%x val:0x%x)!\n", reg, val);
	buf[0] = reg & 0xFF;
	buf[1] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0)
		return 0;

	dev_err(&client->dev,
		"gc2375h write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

/* sensor register read */
static int gc2375h_read_reg(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[1];
	int ret;

	buf[0] = reg & 0xFF;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret >= 0) {
		*val = buf[0];
		return 0;
	}

	dev_err(&client->dev,
		"gc2375h read reg:0x%x failed !\n", reg);

	return ret;
}

static int gc2375h_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = gc2375h_write_reg(client, regs[i].addr, regs[i].val);

	return ret;
}

static int gc2375h_get_reso_dist(const struct gc2375h_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct gc2375h_mode *
gc2375h_find_best_fit(struct gc2375h *gc2375h,
			struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < gc2375h->cfg_num; i++) {
		dist = gc2375h_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int gc2375h_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc2375h *gc2375h = to_gc2375h(sd);
	const struct gc2375h_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&gc2375h->mutex);

	mode = gc2375h_find_best_fit(gc2375h, fmt);
	fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc2375h->mutex);
		return -ENOTTY;
#endif
	} else {
		gc2375h->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(gc2375h->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(gc2375h->vblank, vblank_def,
					 GC2375H_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&gc2375h->mutex);

	return 0;
}

static int gc2375h_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc2375h *gc2375h = to_gc2375h(sd);
	const struct gc2375h_mode *mode = gc2375h->cur_mode;

	mutex_lock(&gc2375h->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc2375h->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&gc2375h->mutex);

	return 0;
}

static int gc2375h_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SRGGB10_1X10;

	return 0;
}

static int gc2375h_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct gc2375h *gc2375h = to_gc2375h(sd);

	if (fse->index >= gc2375h->cfg_num)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SRGGB10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int gc2375h_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct gc2375h *gc2375h = to_gc2375h(sd);
	const struct gc2375h_mode *mode = gc2375h->cur_mode;

	mutex_lock(&gc2375h->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&gc2375h->mutex);

	return 0;
}

static void gc2375h_get_module_inf(struct gc2375h *gc2375h,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, GC2375H_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, gc2375h->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, gc2375h->len_name, sizeof(inf->base.lens));
}

static long gc2375h_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc2375h *gc2375h = to_gc2375h(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc2375h_get_module_inf(gc2375h, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream) {
			ret = gc2375h_write_reg(gc2375h->client, GC2375H_PAGE_SELECT, 0x00);
			ret |= gc2375h_write_reg(gc2375h->client, GC2375H_MODE_SELECT,
						 GC2375H_MODE_STREAMING);
			ret |= gc2375h_write_reg(gc2375h->client, GC2375H_PAGE_SELECT, 0x00);
		} else {
			ret = gc2375h_write_reg(gc2375h->client, GC2375H_PAGE_SELECT, 0x00);
			ret |= gc2375h_write_reg(gc2375h->client, GC2375H_MODE_SELECT,
						 GC2375H_MODE_SW_STANDBY);
			ret |= gc2375h_write_reg(gc2375h->client, GC2375H_PAGE_SELECT, 0x00);
		}
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc2375h_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc2375h_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = gc2375h_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc2375h_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __gc2375h_start_stream(struct gc2375h *gc2375h)
{
	int ret;

	ret = gc2375h_write_array(gc2375h->client, gc2375h->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&gc2375h->mutex);
	ret = v4l2_ctrl_handler_setup(&gc2375h->ctrl_handler);
	mutex_lock(&gc2375h->mutex);
	if (ret)
		return ret;
	//add mark
	ret = gc2375h_write_reg(gc2375h->client, GC2375H_PAGE_SELECT, 0x00);
	ret |= gc2375h_write_reg(gc2375h->client, GC2375H_MODE_SELECT,
				 GC2375H_MODE_STREAMING);
	ret |= gc2375h_write_reg(gc2375h->client, GC2375H_PAGE_SELECT, 0x00);

	return ret;
}

static int __gc2375h_stop_stream(struct gc2375h *gc2375h)
{
	int ret;

	ret = gc2375h_write_reg(gc2375h->client, GC2375H_PAGE_SELECT, 0x00);
	ret |= gc2375h_write_reg(gc2375h->client, GC2375H_MODE_SELECT,
				 GC2375H_MODE_SW_STANDBY);
	ret |= gc2375h_write_reg(gc2375h->client, GC2375H_PAGE_SELECT, 0x00);

	return ret;
//        return 0;
}

static int gc2375h_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc2375h *gc2375h = to_gc2375h(sd);
	struct i2c_client *client = gc2375h->client;
	int ret = 0;

	mutex_lock(&gc2375h->mutex);
	on = !!on;
	if (on == gc2375h->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __gc2375h_start_stream(gc2375h);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__gc2375h_stop_stream(gc2375h);
		pm_runtime_put(&client->dev);
	}

	gc2375h->streaming = on;

unlock_and_return:
	mutex_unlock(&gc2375h->mutex);

	return ret;
}

static int gc2375h_s_power(struct v4l2_subdev *sd, int on)
{
	struct gc2375h *gc2375h = to_gc2375h(sd);
	struct i2c_client *client = gc2375h->client;
	int ret = 0;

	mutex_lock(&gc2375h->mutex);

	/* If the power state is not modified - no work to do. */
	if (gc2375h->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = gc2375h_write_array(gc2375h->client, gc2375h_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		gc2375h->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		gc2375h->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&gc2375h->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 gc2375h_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, GC2375H_XVCLK_FREQ / 1000 / 1000);
}

static int __gc2375h_power_on(struct gc2375h *gc2375h)
{
	int ret;
	u32 delay_us;
	struct device *dev = &gc2375h->client->dev;

	if (!IS_ERR_OR_NULL(gc2375h->pins_default)) {
		ret = pinctrl_select_state(gc2375h->pinctrl,
					   gc2375h->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(gc2375h->xvclk, GC2375H_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(gc2375h->xvclk) != GC2375H_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(gc2375h->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(gc2375h->pwdn_gpio))
		gpiod_set_value_cansleep(gc2375h->pwdn_gpio, 0);

	if (!IS_ERR(gc2375h->reset_gpio))
		gpiod_set_value_cansleep(gc2375h->reset_gpio, 0);

	usleep_range(500, 1000);

	ret = regulator_bulk_enable(GC2375H_NUM_SUPPLIES, gc2375h->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(gc2375h->reset_gpio))
		gpiod_set_value_cansleep(gc2375h->reset_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = gc2375h_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);
	gc2375h->power_on = true;
	return 0;

disable_clk:
	clk_disable_unprepare(gc2375h->xvclk);

	return ret;
}

static void __gc2375h_power_off(struct gc2375h *gc2375h)
{
	int ret;
	struct device *dev = &gc2375h->client->dev;

	if (!IS_ERR(gc2375h->pwdn_gpio))
		gpiod_set_value_cansleep(gc2375h->pwdn_gpio, 1);
	clk_disable_unprepare(gc2375h->xvclk);
	if (!IS_ERR(gc2375h->reset_gpio))
		gpiod_set_value_cansleep(gc2375h->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(gc2375h->pins_sleep)) {
		ret = pinctrl_select_state(gc2375h->pinctrl,
					   gc2375h->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(GC2375H_NUM_SUPPLIES, gc2375h->supplies);
	gc2375h->power_on = false;
}

static int gc2375h_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2375h *gc2375h = to_gc2375h(sd);

	return __gc2375h_power_on(gc2375h);
}

static int gc2375h_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2375h *gc2375h = to_gc2375h(sd);

	__gc2375h_power_off(gc2375h);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc2375h_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc2375h *gc2375h = to_gc2375h(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc2375h_mode *def_mode = &supported_modes[0];

	mutex_lock(&gc2375h->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&gc2375h->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int gc2375h_enum_frame_interval(struct v4l2_subdev *sd,
					struct v4l2_subdev_pad_config *cfg,
					struct v4l2_subdev_frame_interval_enum *fie)
{
	struct gc2375h *gc2375h = to_gc2375h(sd);

	if (fie->index >= gc2375h->cfg_num)
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_SRGGB10_1X10)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int gc2375h_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	u32 val = 0;

	val = 1 << (GC2375H_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops gc2375h_pm_ops = {
	SET_RUNTIME_PM_OPS(gc2375h_runtime_suspend,
			   gc2375h_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc2375h_internal_ops = {
	.open = gc2375h_open,
};
#endif

static const struct v4l2_subdev_core_ops gc2375h_core_ops = {
	.s_power = gc2375h_s_power,
	.ioctl = gc2375h_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc2375h_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc2375h_video_ops = {
	.s_stream = gc2375h_s_stream,
	.g_frame_interval = gc2375h_g_frame_interval,
	.g_mbus_config = gc2375h_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops gc2375h_pad_ops = {
	.enum_mbus_code = gc2375h_enum_mbus_code,
	.enum_frame_size = gc2375h_enum_frame_sizes,
	.enum_frame_interval = gc2375h_enum_frame_interval,
	.get_fmt = gc2375h_get_fmt,
	.set_fmt = gc2375h_set_fmt,
};

static const struct v4l2_subdev_ops gc2375h_subdev_ops = {
	.core	= &gc2375h_core_ops,
	.video	= &gc2375h_video_ops,
	.pad	= &gc2375h_pad_ops,
};

static int gc2375h_set_gain_reg(struct gc2375h *gc2375h, u32 a_gain)
{
	int ret = 0;
	u32 temp = 0;

	ret = gc2375h_write_reg(gc2375h->client,
				 GC2375H_PAGE_SELECT,
				 0x00);
	if (a_gain >= GC2375H_ANALOG_GAIN_1 &&
		 a_gain < GC2375H_ANALOG_GAIN_2) {
		ret |= gc2375h_write_reg(gc2375h->client, 0x20, 0x0b);
		ret |= gc2375h_write_reg(gc2375h->client, 0x22, 0x0c);
		ret |= gc2375h_write_reg(gc2375h->client, 0x26, 0x0e);

		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_ANALOG_GAIN_REG,
			 0x00);
		temp = a_gain;
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	} else if (a_gain >= GC2375H_ANALOG_GAIN_2 &&
		 a_gain < GC2375H_ANALOG_GAIN_3) {
		ret |= gc2375h_write_reg(gc2375h->client, 0x20, 0x0c);
		ret |= gc2375h_write_reg(gc2375h->client, 0x22, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client, 0x26, 0x0e);

		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_ANALOG_GAIN_REG,
			 0x01);
		temp = 64 * a_gain / GC2375H_ANALOG_GAIN_2;
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	} else if (a_gain >= GC2375H_ANALOG_GAIN_3 &&
		 a_gain < GC2375H_ANALOG_GAIN_4) {
		ret |= gc2375h_write_reg(gc2375h->client, 0x20, 0x0c);
		ret |= gc2375h_write_reg(gc2375h->client, 0x22, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client, 0x26, 0x0e);

		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_ANALOG_GAIN_REG,
			 0x02);
		temp = 64 * a_gain / GC2375H_ANALOG_GAIN_3;
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	} else if (a_gain >= GC2375H_ANALOG_GAIN_4 &&
		 a_gain < GC2375H_ANALOG_GAIN_5) {
		ret |= gc2375h_write_reg(gc2375h->client, 0x20, 0x0c);
		ret |= gc2375h_write_reg(gc2375h->client, 0x22, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client, 0x26, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_ANALOG_GAIN_REG,
			 0x03);
		temp = 64 * a_gain / GC2375H_ANALOG_GAIN_4;
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	} else if (a_gain >= GC2375H_ANALOG_GAIN_5 &&
		 a_gain < GC2375H_ANALOG_GAIN_6) {
		ret |= gc2375h_write_reg(gc2375h->client, 0x20, 0x0c);
		ret |= gc2375h_write_reg(gc2375h->client, 0x22, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client, 0x26, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_ANALOG_GAIN_REG,
			 0x04);
		temp = 64 * a_gain / GC2375H_ANALOG_GAIN_5;
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	} else if (a_gain >= GC2375H_ANALOG_GAIN_6 &&
		 a_gain < GC2375H_ANALOG_GAIN_7) {
		ret |= gc2375h_write_reg(gc2375h->client, 0x20, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client, 0x22, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client, 0x26, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_ANALOG_GAIN_REG,
			 0x05);
		temp = 64 * a_gain / GC2375H_ANALOG_GAIN_6;
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	} else if (a_gain >= GC2375H_ANALOG_GAIN_7 &&
		 a_gain < GC2375H_ANALOG_GAIN_8) {
		ret |= gc2375h_write_reg(gc2375h->client, 0x20, 0x0c);
		ret |= gc2375h_write_reg(gc2375h->client, 0x22, 0x0c);
		ret |= gc2375h_write_reg(gc2375h->client, 0x26, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_ANALOG_GAIN_REG,
			 0x06);
		temp = 64 * a_gain / GC2375H_ANALOG_GAIN_7;
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	} else if (a_gain >= GC2375H_ANALOG_GAIN_8 &&
		 a_gain < GC2375H_ANALOG_GAIN_9) {
		ret |= gc2375h_write_reg(gc2375h->client, 0x20, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client, 0x22, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client, 0x26, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_ANALOG_GAIN_REG,
			 0x07);
		temp = 64 * a_gain / GC2375H_ANALOG_GAIN_8;
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	} else {
		ret |= gc2375h_write_reg(gc2375h->client, 0x20, 0x0c);
		ret |= gc2375h_write_reg(gc2375h->client, 0x22, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client, 0x26, 0x0e);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_ANALOG_GAIN_REG,
			 0x08);
		temp = 64 * a_gain / GC2375H_ANALOG_GAIN_9;
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_H_REG,
			 temp >> 6);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PREGAIN_L_REG,
			 (temp << 2) & 0xfc);
	}
	return ret;
}

static int gc2375h_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc2375h *gc2375h = container_of(ctrl->handler,
					     struct gc2375h, ctrl_handler);
	struct i2c_client *client = gc2375h->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = gc2375h->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(gc2375h->exposure,
					 gc2375h->exposure->minimum, max,
					 gc2375h->exposure->step,
					 gc2375h->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret |= gc2375h_write_reg(gc2375h->client,
					 GC2375H_PAGE_SELECT,
					0x00);
		ret |= gc2375h_write_reg(gc2375h->client,
					 GC2375H_REG_EXPOSURE_H,
					 (ctrl->val >> 8) & 0x3f);
		ret |= gc2375h_write_reg(gc2375h->client,
					 GC2375H_REG_EXPOSURE_L,
					 ctrl->val & 0xff);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = gc2375h_set_gain_reg(gc2375h, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_PAGE_SELECT,
			 0x00);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_REG_VTS_H,
			 ((ctrl->val) >> 8) & 0x1f);
		ret |= gc2375h_write_reg(gc2375h->client,
			 GC2375H_REG_VTS_L,
			 (ctrl->val & 0xff));
		break;

	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc2375h_ctrl_ops = {
	.s_ctrl = gc2375h_set_ctrl,
};

static int gc2375h_initialize_controls(struct gc2375h *gc2375h)
{
	const struct gc2375h_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	struct device *dev = &gc2375h->client->dev;

	dev_info(dev, "Enter %s(%d) !\n", __func__, __LINE__);
	handler = &gc2375h->ctrl_handler;
	mode = gc2375h->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &gc2375h->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, GC2375H_PIXEL_RATE, 1, GC2375H_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	gc2375h->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (gc2375h->hblank)
		gc2375h->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc2375h->vblank = v4l2_ctrl_new_std(handler, &gc2375h_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				GC2375H_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	gc2375h->exposure = v4l2_ctrl_new_std(handler, &gc2375h_ctrl_ops,
				V4L2_CID_EXPOSURE, GC2375H_EXPOSURE_MIN,
				exposure_max, GC2375H_EXPOSURE_STEP,
				mode->exp_def);

	gc2375h->anal_gain = v4l2_ctrl_new_std(handler, &gc2375h_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, GC2375H_GAIN_MIN,
				GC2375H_GAIN_MAX, GC2375H_GAIN_STEP,
				GC2375H_GAIN_DEFAULT);

	if (handler->error) {
		ret = handler->error;
		dev_err(&gc2375h->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc2375h->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int gc2375h_check_sensor_id(struct gc2375h *gc2375h,
				  struct i2c_client *client)
{
	struct device *dev = &gc2375h->client->dev;
	u8 pid, ver = 0x00;
	int ret;
	unsigned short id;

	ret = gc2375h_read_reg(client, GC2375H_REG_CHIP_ID_H, &pid);
	if (ret) {
		dev_err(dev, "Read chip ID H register error\n");
		return ret;
	}

	ret = gc2375h_read_reg(client, GC2375H_REG_CHIP_ID_L, &ver);
	if (ret) {
		dev_err(dev, "Read chip ID L register error\n");
		return ret;
	}

	id = SENSOR_ID(pid, ver);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return ret;
	}

	dev_info(dev, "detected gc%04x sensor\n", id);

	return 0;
}

static int gc2375h_configure_regulators(struct gc2375h *gc2375h)
{
	unsigned int i;

	for (i = 0; i < GC2375H_NUM_SUPPLIES; i++)
		gc2375h->supplies[i].supply = gc2375h_supply_names[i];

	return devm_regulator_bulk_get(&gc2375h->client->dev,
				       GC2375H_NUM_SUPPLIES,
				       gc2375h->supplies);
}

static int gc2375h_parse_of(struct gc2375h *gc2375h)
{
	struct device *dev = &gc2375h->client->dev;
	struct device_node *endpoint;
	struct fwnode_handle *fwnode;
	int rval;

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}
	fwnode = of_fwnode_handle(endpoint);
	rval = fwnode_property_read_u32_array(fwnode, "data-lanes", NULL, 0);
	of_node_put(endpoint);
	if (rval <= 0) {
		dev_warn(dev, " Get mipi lane num failed!\n");
		return -1;
	}

	gc2375h->lane_num = rval;
	if (1 == gc2375h->lane_num) {
		gc2375h->cur_mode = &supported_modes_1lane[0];
		supported_modes = supported_modes_1lane;
		gc2375h->cfg_num = ARRAY_SIZE(supported_modes_1lane);

		/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
		gc2375h->pixel_rate = MIPI_FREQ * 2U * gc2375h->lane_num / 10U;
		dev_info(dev, "lane_num(%d)  pixel_rate(%u)\n",
				 gc2375h->lane_num, gc2375h->pixel_rate);
	} else {
		dev_err(dev, "unsupported lane_num(%d)\n", gc2375h->lane_num);
		return -1;
	}
	return 0;
}

static int gc2375h_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gc2375h *gc2375h;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	gc2375h = devm_kzalloc(dev, sizeof(*gc2375h), GFP_KERNEL);
	if (!gc2375h)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &gc2375h->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &gc2375h->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &gc2375h->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &gc2375h->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	gc2375h->client = client;
	gc2375h->cur_mode = &supported_modes[0];

	gc2375h->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(gc2375h->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc2375h->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc2375h->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc2375h->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_HIGH);
	if (IS_ERR(gc2375h->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = gc2375h_parse_of(gc2375h);
	if (ret != 0)
		return -EINVAL;

	gc2375h->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(gc2375h->pinctrl)) {
		gc2375h->pins_default =
			pinctrl_lookup_state(gc2375h->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(gc2375h->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		gc2375h->pins_sleep =
			pinctrl_lookup_state(gc2375h->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(gc2375h->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = gc2375h_configure_regulators(gc2375h);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&gc2375h->mutex);

	sd = &gc2375h->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc2375h_subdev_ops);
	ret = gc2375h_initialize_controls(gc2375h);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc2375h_power_on(gc2375h);
	if (ret)
		goto err_free_handler;

	ret = gc2375h_check_sensor_id(gc2375h, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc2375h_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	gc2375h->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc2375h->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc2375h->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc2375h->module_index, facing,
		 GC2375H_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__gc2375h_power_off(gc2375h);
err_free_handler:
	v4l2_ctrl_handler_free(&gc2375h->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc2375h->mutex);

	return ret;
}

static int gc2375h_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc2375h *gc2375h = to_gc2375h(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc2375h->ctrl_handler);
	mutex_destroy(&gc2375h->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc2375h_power_off(gc2375h);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc2375h_of_match[] = {
	{ .compatible = "galaxycore,gc2375h" },
	{},
};
MODULE_DEVICE_TABLE(of, gc2375h_of_match);
#endif

static const struct i2c_device_id gc2375h_match_id[] = {
	{ "galaxycore,gc2375h", 0 },
	{ },
};

static struct i2c_driver gc2375h_i2c_driver = {
	.driver = {
		.name = GC2375H_NAME,
		.pm = &gc2375h_pm_ops,
		.of_match_table = of_match_ptr(gc2375h_of_match),
	},
	.probe		= &gc2375h_probe,
	.remove		= &gc2375h_remove,
	.id_table	= gc2375h_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc2375h_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc2375h_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("GC2375H CMOS Image Sensor driver");
MODULE_LICENSE("GPL v2");
