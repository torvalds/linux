// SPDX-License-Identifier: GPL-2.0
/*
 * jx_h65 driver
 *
 * Copyright (C) 2019 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 add enum_frame_interval function.
 * V0.0X01.0X03 add quick stream on/off
 * V0.0X01.0X04 add function g_mbus_config
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/rk-camera-module.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/version.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x04)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define JX_H65_XVCLK_FREQ		24000000

#define CHIP_ID_H			0x0A
#define CHIP_ID_L			0x65
#define JX_H65_PIDH_ADDR     0x0a
#define JX_H65_PIDL_ADDR     0x0b

#define JX_H65_REG_CTRL_MODE		0x12
#define JX_H65_MODE_SW_STANDBY		0x40
#define JX_H65_MODE_STREAMING		0x00

#define JX_H65_AEC_PK_LONG_EXPO_HIGH_REG 0x02	/* Exposure Bits 8-15 */
#define JX_H65_AEC_PK_LONG_EXPO_LOW_REG 0x01	/* Exposure Bits 0-7 */
#define JX_H65_FETCH_HIGH_BYTE_EXP(VAL) (((VAL) >> 8) & 0xFF)	/* 8-15 Bits */
#define JX_H65_FETCH_LOW_BYTE_EXP(VAL) ((VAL) & 0xFF)	/* 0-7 Bits */
#define	JX_H65_EXPOSURE_MIN		4
#define	JX_H65_EXPOSURE_STEP		1
#define JX_H65_VTS_MAX			0xffff

#define JX_H65_AEC_PK_LONG_GAIN_REG	 0x00	/* Bits 0 -7 */
#define	ANALOG_GAIN_MIN			0x00
#define	ANALOG_GAIN_MAX			0x7f
#define	ANALOG_GAIN_STEP		1
#define	ANALOG_GAIN_DEFAULT		0x0

#define JX_H65_DIGI_GAIN_L_MASK		0x3f
#define JX_H65_DIGI_GAIN_H_SHIFT	6
#define JX_H65_DIGI_GAIN_MIN		0
#define JX_H65_DIGI_GAIN_MAX		(0x4000 - 1)
#define JX_H65_DIGI_GAIN_STEP		1
#define JX_H65_DIGI_GAIN_DEFAULT	1024

#define JX_H65_REG_TEST_PATTERN		0x0c
#define	JX_H65_TEST_PATTERN_ENABLE	0x80
#define	JX_H65_TEST_PATTERN_DISABLE	0x0

#define JX_H65_REG_HIGH_VTS			0x23
#define JX_H65_REG_LOW_VTS			0X22
#define JX_H65_FETCH_HIGH_BYTE_VTS(VAL) (((VAL) >> 8) & 0xFF)	/* 8-15 Bits */
#define JX_H65_FETCH_LOW_BYTE_VTS(VAL) ((VAL) & 0xFF)	/* 0-7 Bits */

#define REG_NULL			0xFF
#define REG_DELAY			0xFE

#define JX_H65_NAME			"jx_h65"

#define JX_H65_LANES			1

static const char * const jx_h65_supply_names[] = {
	"vcc2v8_dvp",		/* Analog power */
	"vcc1v8_dvp",		/* Digital I/O power */
	"vdd1v5_dvp",		/* Digital core power */
};

#define JX_H65_NUM_SUPPLIES ARRAY_SIZE(jx_h65_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct jx_h65_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct jx_h65 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[JX_H65_NUM_SUPPLIES];
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
	const struct jx_h65_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_jx_h65(sd) container_of(sd, struct jx_h65, subdev)

/*
 * Xclk 24Mhz
 * Pclk 45Mhz
 * linelength 672(0x2a0)
 * framelength 2232(0x8b8)
 * grabwindow_width 1280
 * grabwindow_height 720
 * max_framerate 30fps
 * mipi_datarate per lane 216Mbps
 */

static const struct regval jx_h65_1280x720_regs[] = {
	{0x12, 0x40},
	{0x0E, 0x11},
	{0x0F, 0x04},
	{0x10, 0x24},
	{0x11, 0x80},
	{0x5F, 0x01},
	{0x60, 0x10},
	{0x19, 0x64},
	{0x48, 0x25},
	{0x20, 0xD0},
	{0x21, 0x02},
	{0x22, 0xE8},
	{0x23, 0x03},
	{0x24, 0x80},
	{0x25, 0xD0},
	{0x26, 0x22},
	{0x27, 0x5C},
	{0x28, 0x1A},
	{0x29, 0x01},
	{0x2A, 0x48},
	{0x2B, 0x25},
	{0x2C, 0x00},
	{0x2D, 0x1F},
	{0x2E, 0xF9},
	{0x2F, 0x40},
	{0x41, 0x90},
	{0x42, 0x12},
	{0x39, 0x90},
	{0x1D, 0x00},
	{0x1E, 0x04},
	{0x6C, 0x40},
	{0x70, 0x89},
	{0x71, 0x8A},
	{0x72, 0x68},
	{0x73, 0x33},
	{0x74, 0x52},
	{0x75, 0x2B},
	{0x76, 0x40},
	{0x77, 0x06},
	{0x78, 0x0E},
	{0x6E, 0x2C},
	{0x1F, 0x10},
	{0x31, 0x0C},
	{0x32, 0x20},
	{0x33, 0x0C},
	{0x34, 0x4F},
	{0x36, 0x06},
	{0x38, 0x39},
	{0x3A, 0x08},
	{0x3B, 0x50},
	{0x3C, 0xA0},
	{0x3D, 0x00},
	{0x3E, 0x01},
	{0x3F, 0x00},
	{0x40, 0x00},
	{0x0D, 0x50},
	{0x5A, 0x43},
	{0x5B, 0xB3},
	{0x5C, 0x0C},
	{0x5D, 0x7E},
	{0x5E, 0x24},
	{0x62, 0x40},
	{0x67, 0x48},
	{0x6A, 0x11},
	{0x68, 0x00},
	{0x8F, 0x9F},
	{0x0C, 0x00},
	{0x59, 0x97},
	{0x4A, 0x05},
	{0x50, 0x03},
	{0x47, 0x62},
	{0x7E, 0xCD},
	{0x8D, 0x87},
	{0x49, 0x10},
	{0x7F, 0x52},
	{0x8E, 0x00},
	{0x8C, 0xFF},
	{0x8B, 0x01},
	{0x57, 0x02},
	{0x94, 0x00},
	{0x95, 0x00},
	{0x63, 0x80},
	{0x7B, 0x46},
	{0x7C, 0x2D},
	{0x90, 0x00},
	{0x79, 0x00},
	{0x13, 0x81},
	{0x45, 0x89},
	{0x93, 0x68},
	{REG_DELAY, 0x00},
	{0x45, 0x19},
	{0x1F, 0x11},
	{0x17, 0x00},
	{0x16, 0x77},
	{REG_NULL, 0x00}
};

static const struct regval jx_h65_1280x960_regs[] = {
	{0x12, 0x40},
	{0x0E, 0x11},
	{0x0F, 0x04},
	{0x10, 0x24},
	{0x11, 0x80},
	{0x5F, 0x01},
	{0x60, 0x10},
	{0x19, 0x64},
	{0x48, 0x25},
	{0x20, 0xD0},
	{0x21, 0x02},
	{0x22, 0xE8},
	{0x23, 0x03},
	{0x24, 0x80},
	{0x25, 0xC0},
	{0x26, 0x32},
	{0x27, 0x5C},
	{0x28, 0x1C},
	{0x29, 0x01},
	{0x2A, 0x48},
	{0x2B, 0x25},
	{0x2C, 0x00},
	{0x2D, 0x00},
	{0x2E, 0xF9},
	{0x2F, 0x40},
	{0x41, 0x90},
	{0x42, 0x12},
	{0x39, 0x90},
	{0x1D, 0x00},
	{0x1E, 0x04},
	{0x6C, 0x40},
	{0x70, 0x89},
	{0x71, 0x8A},
	{0x72, 0x68},
	{0x73, 0x53},
	{0x74, 0x52},
	{0x75, 0x2B},
	{0x76, 0x40},
	{0x77, 0x06},
	{0x78, 0x0E},
	{0x6E, 0x2C},
	{0x1F, 0x10},
	{0x31, 0x0C},
	{0x32, 0x20},
	{0x33, 0x0C},
	{0x34, 0x4F},
	{0x36, 0x06},
	{0x38, 0x39},
	{0x3A, 0x08},
	{0x3B, 0x50},
	{0x3C, 0xA0},
	{0x3D, 0x00},
	{0x3E, 0x01},
	{0x3F, 0x00},
	{0x40, 0x00},
	{0x0D, 0x50},
	{0x5A, 0x43},
	{0x5B, 0xB3},
	{0x5C, 0x0C},
	{0x5D, 0x7E},
	{0x5E, 0x24},
	{0x62, 0x40},
	{0x67, 0x48},
	{0x6A, 0x11},
	{0x68, 0x00},
	{0x8F, 0x9F},
	{0x0C, 0x00},
	{0x59, 0x97},
	{0x4A, 0x05},
	{0x50, 0x03},
	{0x47, 0x62},
	{0x7E, 0xCD},
	{0x8D, 0x87},
	{0x49, 0x10},
	{0x7F, 0x52},
	{0x8E, 0x00},
	{0x8C, 0xFF},
	{0x8B, 0x01},
	{0x57, 0x02},
	{0x94, 0x00},
	{0x95, 0x00},
	{0x63, 0x80},
	{0x7B, 0x46},
	{0x7C, 0x2D},
	{0x90, 0x00},
	{0x79, 0x00},
	{0x13, 0x81},
	{0x12, 0x00},
	{0x45, 0x89},
	{0x93, 0x68},
	{REG_DELAY, 0x00},
	{0x45, 0x19},
	{0x1F, 0x01},
	{REG_NULL, 0x00}
};

static const struct jx_h65_mode supported_modes[] = {
	{
		.width = 1280,
		.height = 960,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0384,
		.hts_def = 0x02d0,
		.vts_def = 0x03e8,
		.reg_list = jx_h65_1280x960_regs,
	},
	{
		.width = 1280,
		.height = 720,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0384,
		.hts_def = 0x02d0,
		.vts_def = 0x03e8,
		.reg_list = jx_h65_1280x720_regs,
	}
};

#define JX_H65_LINK_FREQ_420MHZ		216000000
#define JX_H65_PIXEL_RATE		(JX_H65_LINK_FREQ_420MHZ * 2 * 1 / 10)
static const s64 link_freq_menu_items[] = {
	JX_H65_LINK_FREQ_420MHZ
};

static const char * const jx_h65_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 jx_h65_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, JX_H65_XVCLK_FREQ / 1000 / 1000);
}

static int jx_h65_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	buf[0] = reg & 0xFF;
	buf[1] = val;

	msg.addr =  client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0)
		return 0;

	dev_err(&client->dev,
		"jx_h65 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int jx_h65_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i, delay_us;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		if (regs[i].addr == REG_DELAY) {
			delay_us = jx_h65_cal_delay(500 * 1000);
			usleep_range(delay_us, delay_us * 2);
		} else {
			ret = jx_h65_write_reg(client,
				regs[i].addr, regs[i].val);
		}
	}

	return ret;
}

static int jx_h65_read_reg(struct i2c_client *client, u8 reg, u8 *val)
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
		"jx_h65 read reg:0x%x failed !\n", reg);

	return ret;
}

static int jx_h65_get_reso_dist(const struct jx_h65_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct jx_h65_mode *
jx_h65_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = jx_h65_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int jx_h65_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct jx_h65 *jx_h65 = to_jx_h65(sd);
	const struct jx_h65_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&jx_h65->mutex);

	mode = jx_h65_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&jx_h65->mutex);
		return -ENOTTY;
#endif
	} else {
		jx_h65->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(jx_h65->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(jx_h65->vblank, vblank_def,
					 JX_H65_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&jx_h65->mutex);

	return 0;
}

static int jx_h65_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct jx_h65 *jx_h65 = to_jx_h65(sd);
	const struct jx_h65_mode *mode = jx_h65->cur_mode;

	mutex_lock(&jx_h65->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&jx_h65->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&jx_h65->mutex);

	return 0;
}

static int jx_h65_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int jx_h65_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int jx_h65_enable_test_pattern(struct jx_h65 *jx_h65, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | JX_H65_TEST_PATTERN_ENABLE;
	else
		val = JX_H65_TEST_PATTERN_DISABLE;

	return jx_h65_write_reg(jx_h65->client, JX_H65_REG_TEST_PATTERN, val);
}

static void jx_h65_get_module_inf(struct jx_h65 *jx_h65,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, JX_H65_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, jx_h65->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, jx_h65->len_name, sizeof(inf->base.lens));
}

static long jx_h65_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct jx_h65 *jx_h65 = to_jx_h65(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		jx_h65_get_module_inf(jx_h65, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = jx_h65_write_reg(jx_h65->client, JX_H65_REG_CTRL_MODE,
				JX_H65_MODE_STREAMING);
		else
			ret = jx_h65_write_reg(jx_h65->client, JX_H65_REG_CTRL_MODE,
				JX_H65_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long jx_h65_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = jx_h65_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
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
			ret = jx_h65_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = jx_h65_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int jx_h65_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct jx_h65 *jx_h65 = to_jx_h65(sd);
	const struct jx_h65_mode *mode = jx_h65->cur_mode;

	mutex_lock(&jx_h65->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&jx_h65->mutex);

	return 0;
}

static int __jx_h65_start_stream(struct jx_h65 *jx_h65)
{
	return jx_h65_write_reg(jx_h65->client, JX_H65_REG_CTRL_MODE,
				JX_H65_MODE_STREAMING);
}

static int __jx_h65_stop_stream(struct jx_h65 *jx_h65)
{
	return jx_h65_write_reg(jx_h65->client, JX_H65_REG_CTRL_MODE,
				JX_H65_MODE_SW_STANDBY);
}

static int jx_h65_s_stream(struct v4l2_subdev *sd, int on)
{
	struct jx_h65 *jx_h65 = to_jx_h65(sd);
	struct i2c_client *client = jx_h65->client;
	int ret = 0;

	mutex_lock(&jx_h65->mutex);
	on = !!on;
	if (on == jx_h65->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __jx_h65_start_stream(jx_h65);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__jx_h65_stop_stream(jx_h65);
		pm_runtime_put(&client->dev);
	}

	jx_h65->streaming = on;

unlock_and_return:
	mutex_unlock(&jx_h65->mutex);

	return ret;
}

static int jx_h65_s_power(struct v4l2_subdev *sd, int on)
{
	struct jx_h65 *jx_h65 = to_jx_h65(sd);
	struct i2c_client *client = jx_h65->client;
	int ret = 0;

	mutex_lock(&jx_h65->mutex);

	/* If the power state is not modified - no work to do. */
	if (jx_h65->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = jx_h65_write_array(jx_h65->client,
					 jx_h65->cur_mode->reg_list);
		if (ret)
			goto unlock_and_return;

		/*
		 * Enter sleep state to make sure not mipi output
		 * during rkisp init.
		 */
		__jx_h65_stop_stream(jx_h65);

		mutex_unlock(&jx_h65->mutex);
		/* In case these controls are set before streaming */
		ret = v4l2_ctrl_handler_setup(&jx_h65->ctrl_handler);
		if (ret)
			return ret;
		mutex_lock(&jx_h65->mutex);

		jx_h65->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		jx_h65->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&jx_h65->mutex);

	return ret;
}

static int __jx_h65_power_on(struct jx_h65 *jx_h65)
{
	int ret;
	u32 delay_us;
	struct device *dev = &jx_h65->client->dev;

	ret = clk_set_rate(jx_h65->xvclk, JX_H65_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(jx_h65->xvclk) != JX_H65_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(jx_h65->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(jx_h65->reset_gpio))
		gpiod_set_value_cansleep(jx_h65->reset_gpio, 1);

	ret = regulator_bulk_enable(JX_H65_NUM_SUPPLIES, jx_h65->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	/* According to datasheet, at least 10ms for reset duration */
	usleep_range(10 * 1000, 15 * 1000);

	if (!IS_ERR(jx_h65->reset_gpio))
		gpiod_set_value_cansleep(jx_h65->reset_gpio, 0);

	if (!IS_ERR(jx_h65->pwdn_gpio))
		gpiod_set_value_cansleep(jx_h65->pwdn_gpio, 0);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = jx_h65_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(jx_h65->xvclk);

	return ret;
}

static void __jx_h65_power_off(struct jx_h65 *jx_h65)
{
	if (!IS_ERR(jx_h65->pwdn_gpio))
		gpiod_set_value_cansleep(jx_h65->pwdn_gpio, 1);
	clk_disable_unprepare(jx_h65->xvclk);
	if (!IS_ERR(jx_h65->reset_gpio))
		gpiod_set_value_cansleep(jx_h65->reset_gpio, 1);
	regulator_bulk_disable(JX_H65_NUM_SUPPLIES, jx_h65->supplies);
}

static int jx_h65_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct jx_h65 *jx_h65 = to_jx_h65(sd);

	return __jx_h65_power_on(jx_h65);
}

static int jx_h65_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct jx_h65 *jx_h65 = to_jx_h65(sd);

	__jx_h65_power_off(jx_h65);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int jx_h65_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct jx_h65 *jx_h65 = to_jx_h65(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct jx_h65_mode *def_mode = &supported_modes[0];

	mutex_lock(&jx_h65->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&jx_h65->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int jx_h65_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int jx_h65_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	u32 val = 0;

	val = 1 << (JX_H65_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops jx_h65_pm_ops = {
	SET_RUNTIME_PM_OPS(jx_h65_runtime_suspend,
			   jx_h65_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops jx_h65_internal_ops = {
	.open = jx_h65_open,
};
#endif

static const struct v4l2_subdev_core_ops jx_h65_core_ops = {
	.s_power = jx_h65_s_power,
	.ioctl = jx_h65_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = jx_h65_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops jx_h65_video_ops = {
	.s_stream = jx_h65_s_stream,
	.g_frame_interval = jx_h65_g_frame_interval,
	.g_mbus_config = jx_h65_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops jx_h65_pad_ops = {
	.enum_mbus_code = jx_h65_enum_mbus_code,
	.enum_frame_size = jx_h65_enum_frame_sizes,
	.enum_frame_interval = jx_h65_enum_frame_interval,
	.get_fmt = jx_h65_get_fmt,
	.set_fmt = jx_h65_set_fmt,
};

static const struct v4l2_subdev_ops jx_h65_subdev_ops = {
	.core	= &jx_h65_core_ops,
	.video	= &jx_h65_video_ops,
	.pad	= &jx_h65_pad_ops,
};

static int jx_h65_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct jx_h65 *jx_h65 = container_of(ctrl->handler,
					     struct jx_h65, ctrl_handler);
	struct i2c_client *client = jx_h65->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = jx_h65->cur_mode->height + ctrl->val;
		__v4l2_ctrl_modify_range(jx_h65->exposure,
					 jx_h65->exposure->minimum, max,
					 jx_h65->exposure->step,
					 jx_h65->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dev_dbg(&client->dev, "set expo: val: %d\n", ctrl->val);
		/* 4 least significant bits of expsoure are fractional part */
		ret = jx_h65_write_reg(jx_h65->client,
				JX_H65_AEC_PK_LONG_EXPO_HIGH_REG,
				JX_H65_FETCH_HIGH_BYTE_EXP(ctrl->val));
		ret |= jx_h65_write_reg(jx_h65->client,
				JX_H65_AEC_PK_LONG_EXPO_LOW_REG,
				JX_H65_FETCH_LOW_BYTE_EXP(ctrl->val));
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		dev_dbg(&client->dev, "set a-gain: val: %d\n", ctrl->val);
		ret |= jx_h65_write_reg(jx_h65->client,
			JX_H65_AEC_PK_LONG_GAIN_REG, ctrl->val);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		break;
	case V4L2_CID_VBLANK:
		dev_dbg(&client->dev, "set vblank: val: %d\n", ctrl->val);
		ret |= jx_h65_write_reg(jx_h65->client, JX_H65_REG_HIGH_VTS,
			JX_H65_FETCH_HIGH_BYTE_VTS((ctrl->val + jx_h65->cur_mode->height)));
		ret |= jx_h65_write_reg(jx_h65->client, JX_H65_REG_LOW_VTS,
			JX_H65_FETCH_LOW_BYTE_VTS((ctrl->val + jx_h65->cur_mode->height)));
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = jx_h65_enable_test_pattern(jx_h65, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops jx_h65_ctrl_ops = {
	.s_ctrl = jx_h65_set_ctrl,
};

static int jx_h65_initialize_controls(struct jx_h65 *jx_h65)
{
	const struct jx_h65_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &jx_h65->ctrl_handler;
	mode = jx_h65->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &jx_h65->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, JX_H65_PIXEL_RATE, 1, JX_H65_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	jx_h65->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (jx_h65->hblank)
		jx_h65->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	jx_h65->vblank = v4l2_ctrl_new_std(handler, &jx_h65_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				JX_H65_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def;
	jx_h65->exposure = v4l2_ctrl_new_std(handler, &jx_h65_ctrl_ops,
				V4L2_CID_EXPOSURE, JX_H65_EXPOSURE_MIN,
				exposure_max, JX_H65_EXPOSURE_STEP,
				mode->exp_def);

	jx_h65->anal_gain = v4l2_ctrl_new_std(handler, &jx_h65_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	/* Digital gain */
	jx_h65->digi_gain = v4l2_ctrl_new_std(handler, &jx_h65_ctrl_ops,
				V4L2_CID_DIGITAL_GAIN, JX_H65_DIGI_GAIN_MIN,
				JX_H65_DIGI_GAIN_MAX, JX_H65_DIGI_GAIN_STEP,
				JX_H65_DIGI_GAIN_DEFAULT);

	jx_h65->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&jx_h65_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(jx_h65_test_pattern_menu) - 1,
				0, 0, jx_h65_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&jx_h65->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	jx_h65->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int jx_h65_check_sensor_id(struct jx_h65 *jx_h65,
				  struct i2c_client *client)
{
	struct device *dev = &jx_h65->client->dev;
	u8 id_h = 0;
	u8 id_l = 0;
	int ret;

	ret = jx_h65_read_reg(client, JX_H65_PIDH_ADDR, &id_h);
	ret |= jx_h65_read_reg(client, JX_H65_PIDL_ADDR, &id_l);
	if (id_h != CHIP_ID_H && id_l != CHIP_ID_L) {
		dev_err(dev, "Wrong camera sensor id(0x%02x%02x)\n",
			id_h, id_l);
		return -EINVAL;
	}

	dev_info(dev, "Detected jx_h65 (0x%02x%02x) sensor\n",
		id_h, id_l);

	return ret;
}

static int jx_h65_configure_regulators(struct jx_h65 *jx_h65)
{
	unsigned int i;

	for (i = 0; i < JX_H65_NUM_SUPPLIES; i++)
		jx_h65->supplies[i].supply = jx_h65_supply_names[i];

	return devm_regulator_bulk_get(&jx_h65->client->dev,
				       JX_H65_NUM_SUPPLIES,
				       jx_h65->supplies);
}

static int jx_h65_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct jx_h65 *jx_h65;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	jx_h65 = devm_kzalloc(dev, sizeof(*jx_h65), GFP_KERNEL);
	if (!jx_h65)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &jx_h65->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &jx_h65->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &jx_h65->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &jx_h65->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	jx_h65->client = client;
	jx_h65->cur_mode = &supported_modes[0];

	jx_h65->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(jx_h65->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	jx_h65->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(jx_h65->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	jx_h65->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(jx_h65->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = jx_h65_configure_regulators(jx_h65);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&jx_h65->mutex);

	sd = &jx_h65->subdev;
	v4l2_i2c_subdev_init(sd, client, &jx_h65_subdev_ops);
	ret = jx_h65_initialize_controls(jx_h65);
	if (ret)
		goto err_destroy_mutex;

	ret = __jx_h65_power_on(jx_h65);
	if (ret)
		goto err_free_handler;

	ret = jx_h65_check_sensor_id(jx_h65, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &jx_h65_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	jx_h65->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &jx_h65->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(jx_h65->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 jx_h65->module_index, facing,
		 JX_H65_NAME, dev_name(sd->dev));

	ret = v4l2_async_register_subdev(sd);
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
	__jx_h65_power_off(jx_h65);
err_free_handler:
	v4l2_ctrl_handler_free(&jx_h65->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&jx_h65->mutex);

	return ret;
}

static int jx_h65_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct jx_h65 *jx_h65 = to_jx_h65(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&jx_h65->ctrl_handler);
	mutex_destroy(&jx_h65->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__jx_h65_power_off(jx_h65);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id jx_h65_of_match[] = {
	{ .compatible = "soi,jx_h65" },
	{},
};
MODULE_DEVICE_TABLE(of, jx_h65_of_match);
#endif

static const struct i2c_device_id jx_h65_match_id[] = {
	{ "soi,jx_h65", 0 },
	{ },
};

static struct i2c_driver jx_h65_i2c_driver = {
	.driver = {
		.name = JX_H65_NAME,
		.pm = &jx_h65_pm_ops,
		.of_match_table = of_match_ptr(jx_h65_of_match),
	},
	.probe		= &jx_h65_probe,
	.remove		= &jx_h65_remove,
	.id_table	= jx_h65_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&jx_h65_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&jx_h65_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("SOI jx_h65 sensor driver");
MODULE_LICENSE("GPL v2");
