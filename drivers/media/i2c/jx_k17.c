// SPDX-License-Identifier: GPL-2.0
/*
 * jx_k17 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
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
#include <linux/rk-preisp.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/version.h>
#include <linux/pinctrl/consumer.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x04)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define JX_K17_LANES			2
#define JX_K17_LINK_FREQ		198000000

#define JX_K17_PIXEL_RATE		(JX_K17_LINK_FREQ * 2 * JX_K17_LANES / 10)

#define JX_K17_XVCLK_FREQ		24000000

#define CHIP_ID_H			0x0A
#define CHIP_ID_L			0x07
#define JX_K17_PIDH_ADDR		0x0a
#define JX_K17_PIDL_ADDR		0x0b

#define JX_K17_REG_CTRL_MODE		0x12
#define JX_K17_MODE_SW_STANDBY		0x40
#define JX_K17_MODE_STREAMING		0x00

#define JX_K17_AEC_PK_LONG_EXPO_HIGH_REG 0x02	/* Exposure Bits 8-15 */
#define JX_K17_AEC_PK_LONG_EXPO_LOW_REG 0x01	/* Exposure Bits 0-7 */
#define JX_K17_FETCH_HIGH_BYTE_EXP(VAL) (((VAL) >> 8) & 0xFF)	/* 8-15 Bits */
#define JX_K17_FETCH_LOW_BYTE_EXP(VAL) ((VAL) & 0xFF)	/* 0-7 Bits */
#define	JX_K17_EXPOSURE_MIN		4
#define	JX_K17_EXPOSURE_STEP		1
#define JX_K17_VTS_MAX			0xffff

#define JX_K17_AEC_PK_LONG_GAIN_REG	 0x00	/* Bits 0 -7 */
#define	ANALOG_GAIN_MIN			0x00
#define	ANALOG_GAIN_MAX			0x3f
#define	ANALOG_GAIN_STEP		1
#define	ANALOG_GAIN_DEFAULT		0x1f

#define JX_K17_DIGI_GAIN_L_MASK		0x3f
#define JX_K17_DIGI_GAIN_H_SHIFT	6
#define JX_K17_DIGI_GAIN_MIN		0
#define JX_K17_DIGI_GAIN_MAX		(0x4000 - 1)
#define JX_K17_DIGI_GAIN_STEP		1
#define JX_K17_DIGI_GAIN_DEFAULT	1024

#define JX_K17_REG_TEST_PATTERN		0x0c
#define	JX_K17_TEST_PATTERN_ENABLE	0x01
#define	JX_K17_TEST_PATTERN_DISABLE	0x0

#define JX_K17_REG_HIGH_VTS			0x23
#define JX_K17_REG_LOW_VTS			0X22
#define JX_K17_FETCH_HIGH_BYTE_VTS(VAL) (((VAL) >> 8) & 0xFF)	/* 8-15 Bits */
#define JX_K17_FETCH_LOW_BYTE_VTS(VAL) ((VAL) & 0xFF)	/* 0-7 Bits */

#define JX_K17_FLIP_MIRROR_REG		0x12

#define REG_NULL			0xFF
#define REG_DELAY			0xFE

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define JX_K17_NAME			"jx_k17"

static const char * const jx_k17_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define JX_K17_NUM_SUPPLIES ARRAY_SIZE(jx_k17_supply_names)

struct regval {
	u8 addr;
	u8 val;
};

struct jx_k17_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct jx_k17 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[JX_K17_NUM_SUPPLIES];
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
	const struct jx_k17_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
};

#define to_jx_k17(sd) container_of(sd, struct jx_k17, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval jx_k17_global_regs[] = {
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * lane 2
 * linelength 880(0x370)
 * framelength 1500(0x5dc)
 * grabwindow_width 2560
 * grabwindow_height 1440
 * max_framerate 30fps
 * mipi_datarate per lane 396Mbps
 */

static const struct regval jx_k17_2560x1440_2lane_regs[] = {
	{0x12, 0x40},
	{0x48, 0x8A},
	{0x48, 0x0A},
	{0x0E, 0x11},
	{0x0F, 0x04},
	{0x10, 0x42},
	{0x11, 0x80},
	{0x0D, 0x50},
	{0x57, 0xC0},
	{0x58, 0x36},
	{0x5F, 0x01},
	{0x60, 0x19},
	{0x61, 0x10},
	{0x07, 0x08},
	{0x20, 0x70},
	{0x21, 0x03},
	{0x22, 0xDC},
	{0x23, 0x05},
	{0x24, 0x80},
	{0x25, 0xA0},
	{0x26, 0x52},
	{0x27, 0x6C},
	{0x28, 0x15},
	{0x29, 0x03},
	{0x2A, 0x60},
	{0x2B, 0x13},
	{0x2C, 0x32},
	{0x2D, 0x1D},
	{0x2E, 0x8B},
	{0x2F, 0x44},
	{0x41, 0x84},
	{0x42, 0x02},
	{0x46, 0x18},
	{0x47, 0x42},
	{0x80, 0x03},
	{0xAF, 0x22},
	{0xBD, 0x00},
	{0xBE, 0x0A},
	{0x1D, 0x00},
	{0x1E, 0x04},
	{0x6C, 0x40},
	{0x70, 0xD1},
	{0x71, 0x8B},
	{0x72, 0x6D},
	{0x73, 0x49},
	{0x75, 0x1B},
	{0x74, 0x12},
	{0x89, 0x10},
	{0x0C, 0x20},
	{0x6B, 0x10},
	{0x86, 0x43},
	{0x9E, 0x80},
	{0x78, 0x14},
	{0x30, 0x90},
	{0x31, 0x18},
	{0x32, 0x2A},
	{0x33, 0xA8},
	{0x34, 0x80},
	{0x35, 0x70},
	{0x3A, 0xA0},
	{0x56, 0x12},
	{0x59, 0xAC},
	{0x85, 0x64},
	{0x8A, 0x04},
	{0x91, 0x22},
	{0x9F, 0x0F},
	{0xBB, 0x07},
	{0x5B, 0xA4},
	{0x5C, 0x82},
	{0x5D, 0xE4},
	{0x5E, 0x04},
	{0x64, 0xE0},
	{0x65, 0x07},
	{0x66, 0x04},
	{0x67, 0x61},
	{0x68, 0x00},
	{0x69, 0xF4},
	{0x6A, 0x42},
	{0x7A, 0x80},
	{0x82, 0x20},
	{0x8F, 0x90},
	{0x9D, 0x70},
	{0x97, 0xA2},
	{0x13, 0x81},
	{0x96, 0x04},
	{0x4A, 0x05},
	{0x7E, 0xC9},
	{0xA7, 0x04},
	{0x50, 0x02},
	{0x49, 0x10},
	{0x7B, 0x4A},
	{0x7C, 0x0F},
	{0x7F, 0x57},
	{0x62, 0x21},
	{0x90, 0x00},
	{0x8C, 0xFF},
	{0x8D, 0xC7},
	{0x8E, 0x00},
	{0x8B, 0x01},
	{0xBF, 0x01},
	{0x4E, 0x00},
	{0xBF, 0x00},
	{0xA3, 0x20},
	{0xA0, 0x01},
	{0xA2, 0x8D},
	{0x81, 0x70},
	{0x19, 0x20},
	{REG_NULL, 0x00},
};

static const struct jx_k17_mode supported_modes[] = {
	{
		.width = 2560,
		.height = 1440,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x001f,
		.hts_def = 0x0370 * 4,
		.vts_def = 0x05dc,
		.reg_list = jx_k17_2560x1440_2lane_regs,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	JX_K17_LINK_FREQ
};

static const char * const jx_k17_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 jx_k17_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, JX_K17_XVCLK_FREQ / 1000 / 1000);
}

static int jx_k17_write_reg(struct i2c_client *client, u8 reg, u8 val)
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
		"jx_k17 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int jx_k17_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i, delay_us;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		if (regs[i].addr == REG_DELAY) {
			delay_us = jx_k17_cal_delay(500 * 1000);
			usleep_range(delay_us, delay_us * 2);
		} else {
			ret = jx_k17_write_reg(client,
				regs[i].addr, regs[i].val);
		}
	}

	return ret;
}

static int jx_k17_read_reg(struct i2c_client *client, u8 reg, u8 *val)
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
		"jx_k17 read reg:0x%x failed !\n", reg);

	return ret;
}

static int jx_k17_get_reso_dist(const struct jx_k17_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct jx_k17_mode *
jx_k17_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = jx_k17_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int jx_k17_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct jx_k17 *jx_k17 = to_jx_k17(sd);
	const struct jx_k17_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&jx_k17->mutex);

	mode = jx_k17_find_best_fit(fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&jx_k17->mutex);
		return -ENOTTY;
#endif
	} else {
		jx_k17->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(jx_k17->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(jx_k17->vblank, vblank_def,
					 JX_K17_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&jx_k17->mutex);

	return 0;
}

static int jx_k17_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct jx_k17 *jx_k17 = to_jx_k17(sd);
	const struct jx_k17_mode *mode = jx_k17->cur_mode;

	mutex_lock(&jx_k17->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&jx_k17->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&jx_k17->mutex);

	return 0;
}

static int jx_k17_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct jx_k17 *jx_k17 = to_jx_k17(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = jx_k17->cur_mode->bus_fmt;

	return 0;
}

static int jx_k17_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != supported_modes[0].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int jx_k17_enable_test_pattern(struct jx_k17 *jx_k17, u32 pattern)
{
	u8 val = 0;

	jx_k17_read_reg(jx_k17->client, JX_K17_REG_TEST_PATTERN, &val);
	if (pattern)
		val |= (pattern - 1) | JX_K17_TEST_PATTERN_ENABLE;
	else
		val &= ~JX_K17_TEST_PATTERN_DISABLE;

	return jx_k17_write_reg(jx_k17->client, JX_K17_REG_TEST_PATTERN, val);
}

static int jx_k17_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct jx_k17 *jx_k17 = to_jx_k17(sd);
	const struct jx_k17_mode *mode = jx_k17->cur_mode;

	mutex_lock(&jx_k17->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&jx_k17->mutex);

	return 0;
}

static int jx_k17_g_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct jx_k17 *jx_k17 = to_jx_k17(sd);
	const struct jx_k17_mode *mode = jx_k17->cur_mode;
	u32 val;

	val = 1 << (JX_K17_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	if (mode->hdr_mode != NO_HDR)
		val |= V4L2_MBUS_CSI2_CHANNEL_1;
	if (mode->hdr_mode == HDR_X3)
		val |= V4L2_MBUS_CSI2_CHANNEL_2;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void jx_k17_get_module_inf(struct jx_k17 *jx_k17,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, JX_K17_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, jx_k17->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, jx_k17->len_name, sizeof(inf->base.lens));
}

static long jx_k17_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct jx_k17 *jx_k17 = to_jx_k17(sd);
	struct rkmodule_hdr_cfg *hdr;
	u32 i, h, w;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		jx_k17_get_module_inf(jx_k17, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = jx_k17->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = jx_k17->cur_mode->width;
		h = jx_k17->cur_mode->height;
		for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				jx_k17->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == ARRAY_SIZE(supported_modes)) {
			dev_err(&jx_k17->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = jx_k17->cur_mode->hts_def - jx_k17->cur_mode->width;
			h = jx_k17->cur_mode->vts_def - jx_k17->cur_mode->height;
			__v4l2_ctrl_modify_range(jx_k17->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(jx_k17->vblank, h,
						 JX_K17_VTS_MAX - jx_k17->cur_mode->height, 1, h);
		}
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = jx_k17_write_reg(jx_k17->client, JX_K17_REG_CTRL_MODE,
				JX_K17_MODE_STREAMING);
		else
			ret = jx_k17_write_reg(jx_k17->client, JX_K17_REG_CTRL_MODE,
				JX_K17_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long jx_k17_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = jx_k17_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = jx_k17_ioctl(sd, cmd, hdr);
		if (!ret) {
			ret = copy_to_user(up, hdr, sizeof(*hdr));
			if (ret) {
				kfree(hdr);
				return -EFAULT;
			}
		}
		kfree(hdr);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(hdr, up, sizeof(*hdr));
		if (!ret)
			ret = jx_k17_ioctl(sd, cmd, hdr);
		else
			ret = -EFAULT;
		kfree(hdr);
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		hdrae = kzalloc(sizeof(*hdrae), GFP_KERNEL);
		if (!hdrae) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(hdrae, up, sizeof(*hdrae));
		if (!ret)
			ret = jx_k17_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (ret)
			return -EFAULT;
		ret = jx_k17_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}
#endif

static int __jx_k17_start_stream(struct jx_k17 *jx_k17)
{
	int ret;

	ret = jx_k17_write_array(jx_k17->client, jx_k17->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&jx_k17->ctrl_handler);
	if (ret)
		return ret;

	ret = jx_k17_write_reg(jx_k17->client, JX_K17_REG_CTRL_MODE,
				JX_K17_MODE_STREAMING);
	return ret;
}

static int __jx_k17_stop_stream(struct jx_k17 *jx_k17)
{
	return jx_k17_write_reg(jx_k17->client, JX_K17_REG_CTRL_MODE,
				JX_K17_MODE_SW_STANDBY);
}

static int jx_k17_s_stream(struct v4l2_subdev *sd, int on)
{
	struct jx_k17 *jx_k17 = to_jx_k17(sd);
	struct i2c_client *client = jx_k17->client;
	int ret = 0;

	mutex_lock(&jx_k17->mutex);
	on = !!on;
	if (on == jx_k17->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __jx_k17_start_stream(jx_k17);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__jx_k17_stop_stream(jx_k17);
		pm_runtime_put(&client->dev);
	}

	jx_k17->streaming = on;

unlock_and_return:
	mutex_unlock(&jx_k17->mutex);

	return ret;
}

static int jx_k17_s_power(struct v4l2_subdev *sd, int on)
{
	struct jx_k17 *jx_k17 = to_jx_k17(sd);
	struct i2c_client *client = jx_k17->client;
	int ret = 0;

	mutex_lock(&jx_k17->mutex);

	/* If the power state is not modified - no work to do. */
	if (jx_k17->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = jx_k17_write_array(jx_k17->client,
					 jx_k17_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		jx_k17->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		jx_k17->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&jx_k17->mutex);

	return ret;
}

static int __jx_k17_power_on(struct jx_k17 *jx_k17)
{
	int ret;
	u32 delay_us;
	struct device *dev = &jx_k17->client->dev;

	if (!IS_ERR_OR_NULL(jx_k17->pins_default)) {
		ret = pinctrl_select_state(jx_k17->pinctrl,
					   jx_k17->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	ret = clk_set_rate(jx_k17->xvclk, JX_K17_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(jx_k17->xvclk) != JX_K17_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(jx_k17->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(jx_k17->pwdn_gpio))
		gpiod_set_value_cansleep(jx_k17->pwdn_gpio, 1);
	if (!IS_ERR(jx_k17->reset_gpio))
		gpiod_set_value_cansleep(jx_k17->reset_gpio, 1);
	usleep_range(2 * 1000, 3 * 1000);
	if (!IS_ERR(jx_k17->reset_gpio))
		gpiod_set_value_cansleep(jx_k17->reset_gpio, 0);

	ret = regulator_bulk_enable(JX_K17_NUM_SUPPLIES, jx_k17->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	/* According to datasheet, at least 10ms for reset duration */
	usleep_range(10 * 1000, 15 * 1000);

	if (!IS_ERR(jx_k17->reset_gpio))
		gpiod_set_value_cansleep(jx_k17->reset_gpio, 1);

	usleep_range(2000, 3000);
	if (!IS_ERR(jx_k17->pwdn_gpio))
		gpiod_set_value_cansleep(jx_k17->pwdn_gpio, 0);

	if (!IS_ERR(jx_k17->reset_gpio))
		usleep_range(6000, 8000);
	else
		usleep_range(12000, 16000);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = jx_k17_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(jx_k17->xvclk);

	return ret;
}

static void __jx_k17_power_off(struct jx_k17 *jx_k17)
{
	int ret;
	struct device *dev = &jx_k17->client->dev;

	if (!IS_ERR(jx_k17->pwdn_gpio))
		gpiod_set_value_cansleep(jx_k17->pwdn_gpio, 1);
	clk_disable_unprepare(jx_k17->xvclk);
	if (!IS_ERR(jx_k17->reset_gpio))
		gpiod_set_value_cansleep(jx_k17->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(jx_k17->pins_sleep)) {
		ret = pinctrl_select_state(jx_k17->pinctrl,
					   jx_k17->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(JX_K17_NUM_SUPPLIES, jx_k17->supplies);
}

static int jx_k17_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct jx_k17 *jx_k17 = to_jx_k17(sd);

	return __jx_k17_power_on(jx_k17);
}

static int jx_k17_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct jx_k17 *jx_k17 = to_jx_k17(sd);

	__jx_k17_power_off(jx_k17);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int jx_k17_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct jx_k17 *jx_k17 = to_jx_k17(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct jx_k17_mode *def_mode = &supported_modes[0];

	mutex_lock(&jx_k17->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&jx_k17->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int jx_k17_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops jx_k17_pm_ops = {
	SET_RUNTIME_PM_OPS(jx_k17_runtime_suspend,
			   jx_k17_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops jx_k17_internal_ops = {
	.open = jx_k17_open,
};
#endif

static const struct v4l2_subdev_core_ops jx_k17_core_ops = {
	.s_power = jx_k17_s_power,
	.ioctl = jx_k17_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = jx_k17_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops jx_k17_video_ops = {
	.s_stream = jx_k17_s_stream,
	.g_frame_interval = jx_k17_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops jx_k17_pad_ops = {
	.enum_mbus_code = jx_k17_enum_mbus_code,
	.enum_frame_size = jx_k17_enum_frame_sizes,
	.enum_frame_interval = jx_k17_enum_frame_interval,
	.get_fmt = jx_k17_get_fmt,
	.set_fmt = jx_k17_set_fmt,
	.get_mbus_config = jx_k17_g_mbus_config,
};

static const struct v4l2_subdev_ops jx_k17_subdev_ops = {
	.core	= &jx_k17_core_ops,
	.video	= &jx_k17_video_ops,
	.pad	= &jx_k17_pad_ops,
};

static int jx_k17_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct jx_k17 *jx_k17 = container_of(ctrl->handler,
					     struct jx_k17, ctrl_handler);
	struct i2c_client *client = jx_k17->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = jx_k17->cur_mode->height + ctrl->val - 9;
		__v4l2_ctrl_modify_range(jx_k17->exposure,
					 jx_k17->exposure->minimum, max,
					 jx_k17->exposure->step,
					 jx_k17->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dev_dbg(&client->dev, "set expo: val: %d\n", ctrl->val);
		/* 4 least significant bits of expsoure are fractional part */
		ret = jx_k17_write_reg(jx_k17->client,
				JX_K17_AEC_PK_LONG_EXPO_HIGH_REG,
				JX_K17_FETCH_HIGH_BYTE_EXP(ctrl->val));
		ret |= jx_k17_write_reg(jx_k17->client,
				JX_K17_AEC_PK_LONG_EXPO_LOW_REG,
				JX_K17_FETCH_LOW_BYTE_EXP(ctrl->val));
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		dev_dbg(&client->dev, "set a-gain: val: %d\n", ctrl->val);
		ret |= jx_k17_write_reg(jx_k17->client,
			JX_K17_AEC_PK_LONG_GAIN_REG, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		dev_dbg(&client->dev, "set vblank: val: %d\n", ctrl->val);
		ret |= jx_k17_write_reg(jx_k17->client, JX_K17_REG_HIGH_VTS,
			JX_K17_FETCH_HIGH_BYTE_VTS((ctrl->val + jx_k17->cur_mode->height)));
		ret |= jx_k17_write_reg(jx_k17->client, JX_K17_REG_LOW_VTS,
			JX_K17_FETCH_LOW_BYTE_VTS((ctrl->val + jx_k17->cur_mode->height)));
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = jx_k17_enable_test_pattern(jx_k17, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops jx_k17_ctrl_ops = {
	.s_ctrl = jx_k17_set_ctrl,
};

static int jx_k17_initialize_controls(struct jx_k17 *jx_k17)
{
	const struct jx_k17_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &jx_k17->ctrl_handler;
	mode = jx_k17->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 7);
	if (ret)
		return ret;
	handler->lock = &jx_k17->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, JX_K17_PIXEL_RATE, 1, JX_K17_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	jx_k17->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (jx_k17->hblank)
		jx_k17->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	jx_k17->vblank = v4l2_ctrl_new_std(handler, &jx_k17_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				JX_K17_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 9;
	jx_k17->exposure = v4l2_ctrl_new_std(handler, &jx_k17_ctrl_ops,
				V4L2_CID_EXPOSURE, JX_K17_EXPOSURE_MIN,
				exposure_max, JX_K17_EXPOSURE_STEP,
				mode->exp_def);

	jx_k17->anal_gain = v4l2_ctrl_new_std(handler, &jx_k17_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	jx_k17->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&jx_k17_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(jx_k17_test_pattern_menu) - 1,
				0, 0, jx_k17_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&jx_k17->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	jx_k17->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int jx_k17_check_sensor_id(struct jx_k17 *jx_k17,
				  struct i2c_client *client)
{
	struct device *dev = &jx_k17->client->dev;
	u8 id_h = 0;
	u8 id_l = 0;
	int ret;

	ret = jx_k17_read_reg(client, JX_K17_PIDH_ADDR, &id_h);
	ret |= jx_k17_read_reg(client, JX_K17_PIDL_ADDR, &id_l);
	if (id_h != CHIP_ID_H && id_l != CHIP_ID_L) {
		dev_err(dev, "Wrong camera sensor id(0x%02x%02x)\n",
			id_h, id_l);
		return -EINVAL;
	}

	dev_info(dev, "Detected jx_k17 (0x%02x%02x) sensor\n",
		id_h, id_l);

	return ret;
}

static int jx_k17_configure_regulators(struct jx_k17 *jx_k17)
{
	unsigned int i;

	for (i = 0; i < JX_K17_NUM_SUPPLIES; i++)
		jx_k17->supplies[i].supply = jx_k17_supply_names[i];

	return devm_regulator_bulk_get(&jx_k17->client->dev,
				       JX_K17_NUM_SUPPLIES,
				       jx_k17->supplies);
}

static int jx_k17_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct jx_k17 *jx_k17;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	jx_k17 = devm_kzalloc(dev, sizeof(*jx_k17), GFP_KERNEL);
	if (!jx_k17)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &jx_k17->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &jx_k17->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &jx_k17->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &jx_k17->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	jx_k17->client = client;
	jx_k17->cur_mode = &supported_modes[0];

	jx_k17->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(jx_k17->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	jx_k17->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(jx_k17->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	jx_k17->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(jx_k17->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	jx_k17->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(jx_k17->pinctrl)) {
		jx_k17->pins_default =
			pinctrl_lookup_state(jx_k17->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(jx_k17->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		jx_k17->pins_sleep =
			pinctrl_lookup_state(jx_k17->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(jx_k17->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}
	ret = jx_k17_configure_regulators(jx_k17);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&jx_k17->mutex);

	sd = &jx_k17->subdev;
	v4l2_i2c_subdev_init(sd, client, &jx_k17_subdev_ops);
	ret = jx_k17_initialize_controls(jx_k17);
	if (ret)
		goto err_destroy_mutex;

	ret = __jx_k17_power_on(jx_k17);
	if (ret)
		goto err_free_handler;

	ret = jx_k17_check_sensor_id(jx_k17, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &jx_k17_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	jx_k17->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &jx_k17->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(jx_k17->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 jx_k17->module_index, facing,
		 JX_K17_NAME, dev_name(sd->dev));

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
	__jx_k17_power_off(jx_k17);
err_free_handler:
	v4l2_ctrl_handler_free(&jx_k17->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&jx_k17->mutex);

	return ret;
}

static int jx_k17_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct jx_k17 *jx_k17 = to_jx_k17(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&jx_k17->ctrl_handler);
	mutex_destroy(&jx_k17->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__jx_k17_power_off(jx_k17);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id jx_k17_of_match[] = {
	{ .compatible = "soi,jx_k17" },
	{},
};
MODULE_DEVICE_TABLE(of, jx_k17_of_match);
#endif

static const struct i2c_device_id jx_k17_match_id[] = {
	{ "soi,jx_k17", 0 },
	{ },
};

static struct i2c_driver jx_k17_i2c_driver = {
	.driver = {
		.name = JX_K17_NAME,
		.pm = &jx_k17_pm_ops,
		.of_match_table = of_match_ptr(jx_k17_of_match),
	},
	.probe		= &jx_k17_probe,
	.remove		= &jx_k17_remove,
	.id_table	= jx_k17_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&jx_k17_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&jx_k17_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("SOI jx_k17 sensor driver");
MODULE_LICENSE("GPL");
