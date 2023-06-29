// SPDX-License-Identifier: GPL-2.0
/*
 * sc501ai driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <linux/rk-preisp.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include "../platform/rockchip/isp/rkisp_tb_helper.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define SC501AI_LANES			2

#define SC501AI_LINK_FREQ_396M		396000000 // 792Mbps

#define SC501AI_PIXEL_RATE_396M_10BIT	(SC501AI_LINK_FREQ_396M * 2 * SC501AI_LANES / 10)

#define SC501AI_XVCLK_FREQ		27000000

#define SC501AI_CHIP_ID			0xce1f
#define SC501AI_REG_CHIP_ID		0x3107

#define SC501AI_REG_CTRL_MODE		0x0100
#define SC501AI_MODE_SW_STANDBY		0x0
#define SC501AI_MODE_STREAMING		BIT(0)

#define SC501AI_REG_EXPOSURE_H		0x3e00
#define SC501AI_REG_EXPOSURE_M		0x3e01
#define SC501AI_REG_EXPOSURE_L		0x3e02

#define	SC501AI_EXPOSURE_MIN		3
#define	SC501AI_EXPOSURE_STEP		1

#define SC501AI_REG_DIG_GAIN		0x3e06
#define SC501AI_REG_DIG_FINE_GAIN	0x3e07
#define SC501AI_REG_ANA_GAIN		0x3e08
#define SC501AI_REG_ANA_FINE_GAIN	0x3e09

#define SC501AI_GAIN_MIN		0x40
#define SC501AI_GAIN_MAX		0xc000
#define SC501AI_GAIN_STEP		1
#define SC501AI_GAIN_DEFAULT		0x40

#define SC501AI_REG_VTS_H		0x320e
#define SC501AI_REG_VTS_L		0x320f
#define SC501AI_VTS_MAX			0x7fff

#define SC501AI_FLIP_MIRROR_REG		0x3221
#define SC501AI_FLIP_MASK		0x60
#define SC501AI_MIRROR_MASK		0x06

#define REG_NULL			0xFFFF

#define SC501AI_REG_VALUE_08BIT		1
#define SC501AI_REG_VALUE_16BIT		2
#define SC501AI_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define SC501AI_NAME			"sc501ai"

#define SC501AI_FETCH_EXP_H(VAL)		(((VAL) >> 12) & 0xF)
#define SC501AI_FETCH_EXP_M(VAL)		(((VAL) >> 4) & 0xFF)
#define SC501AI_FETCH_EXP_L(VAL)		(((VAL) & 0xF) << 4)

static const char * const sc501ai_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define sc501ai_NUM_SUPPLIES ARRAY_SIZE(sc501ai_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct sc501ai_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 mipi_freq_idx;
	u32 bpp;
	const struct regval *reg_list;
	u32 vc[PAD_MAX];
};

struct sc501ai {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[sc501ai_NUM_SUPPLIES];

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
	struct v4l2_fract	cur_fps;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct sc501ai_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	bool			has_init_exp;
	u32			cur_vts;
	bool			is_thunderboot;
	bool			is_first_streamoff;
};

#define to_sc501ai(sd) container_of(sd, struct sc501ai, subdev)

/*
 * Xclk 27Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 792Mbps, 2lane
 */
static const struct regval sc501ai_linear_10_2880x1616_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x3018, 0x32},
	{0x3019, 0x0c},
	{0x301f, 0x0b},
	{0x3203, 0x02},
	{0x3207, 0x59},
	{0x320b, 0x50},
	{0x3253, 0x0a},
	{0x3301, 0x0a},
	{0x3302, 0x18},
	{0x3303, 0x10},
	{0x3304, 0x60},
	{0x3306, 0x60},
	{0x3308, 0x10},
	{0x3309, 0x70},
	{0x330a, 0x00},
	{0x330b, 0xf0},
	{0x330d, 0x18},
	{0x330e, 0x20},
	{0x330f, 0x02},
	{0x3310, 0x02},
	{0x331c, 0x04},
	{0x331e, 0x51},
	{0x331f, 0x61},
	{0x3320, 0x09},
	{0x3333, 0x10},
	{0x334c, 0x08},
	{0x3356, 0x09},
	{0x3364, 0x17},
	{0x336d, 0x03},
	{0x3390, 0x08},
	{0x3391, 0x18},
	{0x3392, 0x38},
	{0x3393, 0x0a},
	{0x3394, 0x20},
	{0x3395, 0x20},
	{0x3396, 0x08},
	{0x3397, 0x18},
	{0x3398, 0x38},
	{0x3399, 0x0a},
	{0x339a, 0x20},
	{0x339b, 0x20},
	{0x339c, 0x20},
	{0x33ac, 0x10},
	{0x33ae, 0x10},
	{0x33af, 0x19},
	{0x360f, 0x01},
	{0x3622, 0x03},
	{0x363a, 0x1f},
	{0x363c, 0x40},
	{0x3651, 0x7d},
	{0x3670, 0x0a},
	{0x3671, 0x07},
	{0x3672, 0x17},
	{0x3673, 0x1e},
	{0x3674, 0x82},
	{0x3675, 0x64},
	{0x3676, 0x66},
	{0x367a, 0x48},
	{0x367b, 0x78},
	{0x367c, 0x58},
	{0x367d, 0x78},
	{0x3690, 0x34},
	{0x3691, 0x34},
	{0x3692, 0x54},
	{0x369c, 0x48},
	{0x369d, 0x78},
	{0x36ec, 0x0a},
	{0x3904, 0x04},
	{0x3908, 0x41},
	{0x391d, 0x04},
	{0x39c2, 0x30},
	{0x3e01, 0xcd},
	{0x3e02, 0xc0},
	{0x3e16, 0x00},
	{0x3e17, 0x80},
	{0x4500, 0x88},
	{0x4509, 0x20},
	{0x4837, 0x14},
	{0x5799, 0x00},
	{0x59e0, 0x60},
	{0x59e1, 0x08},
	{0x59e2, 0x3f},
	{0x59e3, 0x18},
	{0x59e4, 0x18},
	{0x59e5, 0x3f},
	{0x59e7, 0x02},
	{0x59e8, 0x38},
	{0x59e9, 0x20},
	{0x59ea, 0x0c},
	{0x59ec, 0x08},
	{0x59ed, 0x02},
	{0x59ee, 0xa0},
	{0x59ef, 0x08},
	{0x59f4, 0x18},
	{0x59f5, 0x10},
	{0x59f6, 0x0c},
	{0x59f9, 0x02},
	{0x59fa, 0x18},
	{0x59fb, 0x10},
	{0x59fc, 0x0c},
	{0x59ff, 0x02},
	{0x36e9, 0x1c},
	{0x36f9, 0x24},
	{REG_NULL, 0x00},
};

static const struct sc501ai_mode supported_modes[] = {
	{
		.width = 2880,
		.height = 1616,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x80,
		.hts_def = 0xc80,
		.vts_def = 0x0672,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc501ai_linear_10_2880x1616_regs,
		.mipi_freq_idx = 0,
		.bpp = 10,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	SC501AI_LINK_FREQ_396M
};

/* Write registers up to 4 at a time */
static int sc501ai_write_reg(struct i2c_client *client, u16 reg,
			     u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int sc501ai_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = sc501ai_write_reg(client, regs[i].addr,
					SC501AI_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int sc501ai_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
			    u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret;

	if (len > 4 || !len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

static int sc501ai_get_reso_dist(const struct sc501ai_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc501ai_mode *
sc501ai_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = sc501ai_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int sc501ai_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc501ai *sc501ai = to_sc501ai(sd);
	const struct sc501ai_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&sc501ai->mutex);

	mode = sc501ai_find_best_fit(fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc501ai->mutex);
		return -ENOTTY;
#endif
	} else {
		sc501ai->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc501ai->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc501ai->vblank, vblank_def,
					 SC501AI_VTS_MAX - mode->height,
					 1, vblank_def);
		sc501ai->cur_fps = mode->max_fps;
	}

	mutex_unlock(&sc501ai->mutex);

	return 0;
}

static int sc501ai_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc501ai *sc501ai = to_sc501ai(sd);
	const struct sc501ai_mode *mode = sc501ai->cur_mode;

	mutex_lock(&sc501ai->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc501ai->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
		/* format info: width/height/data type/virctual channel */
		fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&sc501ai->mutex);

	return 0;
}

static int sc501ai_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct sc501ai *sc501ai = to_sc501ai(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = sc501ai->cur_mode->bus_fmt;

	return 0;
}

static int sc501ai_enum_frame_sizes(struct v4l2_subdev *sd,
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

static int sc501ai_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct sc501ai *sc501ai = to_sc501ai(sd);
	const struct sc501ai_mode *mode = sc501ai->cur_mode;

	if (sc501ai->streaming)
		fi->interval = sc501ai->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

static int sc501ai_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				 struct v4l2_mbus_config *config)
{
	u32 val = 1 << (SC501AI_LANES - 1) |
		  V4L2_MBUS_CSI2_CHANNEL_0 |
		  V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void sc501ai_get_module_inf(struct sc501ai *sc501ai,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, SC501AI_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, sc501ai->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, sc501ai->len_name, sizeof(inf->base.lens));
}

static int sc501ai_get_gain_reg(u32 total_gain, u32 *again, u32 *again_fine,
				u32 *dgain, u32 *dgain_fine)
{
	int ret = 0;
	u32 step = 0;

	if (total_gain <= 0x60) { /* 1 - 1.5x gain */
		step = total_gain - 0x40;

		*again = 0x03;
		*again_fine = step + 0x40;
		*dgain = 0x00;
		*dgain_fine = 0x80;
	} else if (total_gain <= 0xc0) { /* 1.5x - 3x gain */
		step = (total_gain - 0x60) * 64 / 0x60 - 1;

		*again = 0x23;
		*again_fine = step + 0x40;
		*dgain = 0x00;
		*dgain_fine = 0x80;
	} else if (total_gain <= 0x180) { /* 3x - 6x gain */
		step = (total_gain - 0xc0) * 64 / 0xc0 - 1;

		*again = 0x27;
		*again_fine = step + 0x40;
		*dgain = 0x00;
		*dgain_fine = 0x80;
	} else if (total_gain <= 0x300) { /* 6x - 12x gain */
		step = (total_gain - 0x180) * 64 / 0x180 - 1;

		*again = 0x2f;
		*again_fine = step + 0x40;
		*dgain = 0x00;
		*dgain_fine = 0x80;
	} else if (total_gain <= 0x600) { /* 12x - 24x gain */
		step = (total_gain - 0x300) * 64 / 0x300 - 1;

		*again = 0x3f;
		*again_fine = step + 0x40;
		*dgain = 0x00;
		*dgain_fine = 0x80;
	} else if (total_gain <= 0xc00) { /* 24x - 48x gain */
		step = (total_gain - 0x600) * 128 / 0x600 - 1;

		*again = 0x3f;
		*again_fine = 0x7f;
		*dgain = 0x00;
		*dgain_fine = 0x80 + step;
	} else if (total_gain <= 0x1800) { /* 48x - 96x gain */
		step = (total_gain - 0xc00) * 128 / 0xc00 - 1;

		*again = 0x3f;
		*again_fine = 0x7f;
		*dgain = 0x01;
		*dgain_fine = 0x80 + step;
	} else if (total_gain <= 0x3000) { /* 96x - 192x gain */
		step  = (total_gain - 0x1800) * 128 / 0x1800 - 1;

		*again = 0x3f;
		*again_fine = 0x7f;
		*dgain = 0x03;
		*dgain_fine = 0x80 + step;
	} else if (total_gain <= 0x6000) { /* 192x - 384x gain */
		step  = (total_gain - 0x3000) * 128 / 0x3000 - 1;

		*again = 0x3f;
		*again_fine = 0x7f;
		*dgain = 0x07;
		*dgain_fine = 0x80 + step;
	} else if (total_gain <= 0xc000) { /* 384x - 768x gain */
		step  = (total_gain - 0x6000) * 128 / 0x6000 - 1;

		*again = 0x3f;
		*again_fine = 0x7f;
		*dgain = 0x0f;
		*dgain_fine = 0x80 + step;
	}
	return ret;
}

static long sc501ai_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc501ai *sc501ai = to_sc501ai(sd);

	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		sc501ai_get_module_inf(sc501ai, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);
		if (stream)
			ret = sc501ai_write_reg(sc501ai->client, SC501AI_REG_CTRL_MODE,
						SC501AI_REG_VALUE_08BIT, SC501AI_MODE_STREAMING);
		else
			ret = sc501ai_write_reg(sc501ai->client, SC501AI_REG_CTRL_MODE,
						SC501AI_REG_VALUE_08BIT, SC501AI_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc501ai_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc501ai_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;

		ret = sc501ai_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __sc501ai_start_stream(struct sc501ai *sc501ai)
{
	int ret;

	if (!sc501ai->is_thunderboot) {
		ret = sc501ai_write_array(sc501ai->client, sc501ai->cur_mode->reg_list);
		if (ret)
			return ret;

		/* In case these controls are set before streaming */
		ret = __v4l2_ctrl_handler_setup(&sc501ai->ctrl_handler);
		if (ret)
			return ret;
	}

	return sc501ai_write_reg(sc501ai->client, SC501AI_REG_CTRL_MODE,
				 SC501AI_REG_VALUE_08BIT, SC501AI_MODE_STREAMING);
}

static int __sc501ai_stop_stream(struct sc501ai *sc501ai)
{
	sc501ai->has_init_exp = false;
	if (sc501ai->is_thunderboot) {
		sc501ai->is_first_streamoff = true;
		pm_runtime_put(&sc501ai->client->dev);
	}
	return sc501ai_write_reg(sc501ai->client, SC501AI_REG_CTRL_MODE,
				 SC501AI_REG_VALUE_08BIT, SC501AI_MODE_SW_STANDBY);
}

static int __sc501ai_power_on(struct sc501ai *sc501ai);
static int sc501ai_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc501ai *sc501ai = to_sc501ai(sd);
	struct i2c_client *client = sc501ai->client;
	int ret = 0;

	mutex_lock(&sc501ai->mutex);
	on = !!on;
	if (on == sc501ai->streaming)
		goto unlock_and_return;

	if (on) {
		if (sc501ai->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
			sc501ai->is_thunderboot = false;
			__sc501ai_power_on(sc501ai);
		}
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __sc501ai_start_stream(sc501ai);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc501ai_stop_stream(sc501ai);
		pm_runtime_put(&client->dev);
	}

	sc501ai->streaming = on;

unlock_and_return:
	mutex_unlock(&sc501ai->mutex);

	return ret;
}

static int sc501ai_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc501ai *sc501ai = to_sc501ai(sd);
	struct i2c_client *client = sc501ai->client;
	int ret = 0;

	mutex_lock(&sc501ai->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc501ai->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		sc501ai->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc501ai->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc501ai->mutex);

	return ret;
}

static int __sc501ai_power_on(struct sc501ai *sc501ai)
{
	int ret;
	struct device *dev = &sc501ai->client->dev;

	if (!IS_ERR_OR_NULL(sc501ai->pins_default)) {
		ret = pinctrl_select_state(sc501ai->pinctrl,
					   sc501ai->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(sc501ai->xvclk, SC501AI_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (27MHz)\n");
	if (clk_get_rate(sc501ai->xvclk) != SC501AI_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc501ai->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (sc501ai->is_thunderboot)
		return 0;

	if (!IS_ERR(sc501ai->reset_gpio))
		gpiod_set_value_cansleep(sc501ai->reset_gpio, 0);

	ret = regulator_bulk_enable(sc501ai_NUM_SUPPLIES, sc501ai->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(sc501ai->reset_gpio))
		gpiod_set_value_cansleep(sc501ai->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(sc501ai->pwdn_gpio))
		gpiod_set_value_cansleep(sc501ai->pwdn_gpio, 1);

	usleep_range(4000, 5000);
	return 0;

disable_clk:
	clk_disable_unprepare(sc501ai->xvclk);

	return ret;
}

static void __sc501ai_power_off(struct sc501ai *sc501ai)
{
	int ret;
	struct device *dev = &sc501ai->client->dev;

	clk_disable_unprepare(sc501ai->xvclk);
	if (sc501ai->is_thunderboot) {
		if (sc501ai->is_first_streamoff) {
			sc501ai->is_thunderboot = false;
			sc501ai->is_first_streamoff = false;
		} else {
			return;
		}
	}

	if (!IS_ERR(sc501ai->pwdn_gpio))
		gpiod_set_value_cansleep(sc501ai->pwdn_gpio, 0);
	clk_disable_unprepare(sc501ai->xvclk);
	if (!IS_ERR(sc501ai->reset_gpio))
		gpiod_set_value_cansleep(sc501ai->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(sc501ai->pins_sleep)) {
		ret = pinctrl_select_state(sc501ai->pinctrl,
					   sc501ai->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(sc501ai_NUM_SUPPLIES, sc501ai->supplies);
}

static int sc501ai_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc501ai *sc501ai = to_sc501ai(sd);

	return __sc501ai_power_on(sc501ai);
}

static int sc501ai_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc501ai *sc501ai = to_sc501ai(sd);

	__sc501ai_power_off(sc501ai);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc501ai_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc501ai *sc501ai = to_sc501ai(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc501ai_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc501ai->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc501ai->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sc501ai_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = NO_HDR;
	return 0;
}

static const struct dev_pm_ops sc501ai_pm_ops = {
	SET_RUNTIME_PM_OPS(sc501ai_runtime_suspend,
	sc501ai_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc501ai_internal_ops = {
	.open = sc501ai_open,
};
#endif

static const struct v4l2_subdev_core_ops sc501ai_core_ops = {
	.s_power = sc501ai_s_power,
	.ioctl = sc501ai_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc501ai_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc501ai_video_ops = {
	.s_stream = sc501ai_s_stream,
	.g_frame_interval = sc501ai_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops sc501ai_pad_ops = {
	.enum_mbus_code = sc501ai_enum_mbus_code,
	.enum_frame_size = sc501ai_enum_frame_sizes,
	.enum_frame_interval = sc501ai_enum_frame_interval,
	.get_fmt = sc501ai_get_fmt,
	.set_fmt = sc501ai_set_fmt,
	.get_mbus_config = sc501ai_g_mbus_config,
};

static const struct v4l2_subdev_ops sc501ai_subdev_ops = {
	.core	= &sc501ai_core_ops,
	.video	= &sc501ai_video_ops,
	.pad	= &sc501ai_pad_ops,
};

static void sc501ai_modify_fps_info(struct sc501ai *sc501ai)
{
	const struct sc501ai_mode *mode = sc501ai->cur_mode;

	sc501ai->cur_fps.denominator = mode->max_fps.denominator * sc501ai->cur_vts /
				       mode->vts_def;
}
static int sc501ai_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc501ai *sc501ai = container_of(ctrl->handler,
					       struct sc501ai, ctrl_handler);
	struct i2c_client *client = sc501ai->client;
	s64 max;
	u32 again = 0, again_fine = 0, dgain = 0, dgain_fine = 0;
	int ret = 0;
	u32 val = 0, vts = 0;
	u64 delay_time = 0;
	u32 cur_fps = 0;
	u32 def_fps = 0;
	u32 denominator = 0;
	u32 numerator = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc501ai->cur_mode->height + ctrl->val - 10;
		__v4l2_ctrl_modify_range(sc501ai->exposure,
					 sc501ai->exposure->minimum, max,
					 sc501ai->exposure->step,
					 sc501ai->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		val = ctrl->val << 1;
		ret = sc501ai_write_reg(sc501ai->client,
					SC501AI_REG_EXPOSURE_H,
					SC501AI_REG_VALUE_08BIT,
					SC501AI_FETCH_EXP_H(val));
		ret |= sc501ai_write_reg(sc501ai->client,
					 SC501AI_REG_EXPOSURE_M,
					 SC501AI_REG_VALUE_08BIT,
					 SC501AI_FETCH_EXP_M(val));
		ret |= sc501ai_write_reg(sc501ai->client,
					 SC501AI_REG_EXPOSURE_L,
					 SC501AI_REG_VALUE_08BIT,
					 SC501AI_FETCH_EXP_L(val));

		dev_dbg(&client->dev, "set exposure 0x%x\n", val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		sc501ai_get_gain_reg(ctrl->val, &again, &again_fine, &dgain, &dgain_fine);
		ret = sc501ai_write_reg(sc501ai->client,
					SC501AI_REG_DIG_GAIN,
					SC501AI_REG_VALUE_08BIT,
					dgain);
		ret |= sc501ai_write_reg(sc501ai->client,
					 SC501AI_REG_DIG_FINE_GAIN,
					 SC501AI_REG_VALUE_08BIT,
					 dgain_fine);
		ret |= sc501ai_write_reg(sc501ai->client,
					 SC501AI_REG_ANA_GAIN,
					 SC501AI_REG_VALUE_08BIT,
					 again);
		ret |= sc501ai_write_reg(sc501ai->client,
					 SC501AI_REG_ANA_FINE_GAIN,
					 SC501AI_REG_VALUE_08BIT,
					 again_fine);

		dev_dbg(&sc501ai->client->dev,
			"total_gain:%d again 0x%x, again_fine 0x%x, dgain 0x%x, dgain_fine 0x%x\n",
			ctrl->val, again, again_fine, dgain, dgain_fine);
		break;
	case V4L2_CID_VBLANK:
		vts = ctrl->val + sc501ai->cur_mode->height;
		ret = sc501ai_write_reg(sc501ai->client,
					SC501AI_REG_VTS_H,
					SC501AI_REG_VALUE_08BIT,
					(vts >> 8) & 0x7f);
		ret |= sc501ai_write_reg(sc501ai->client,
					 SC501AI_REG_VTS_L,
					 SC501AI_REG_VALUE_08BIT,
					 vts & 0xff);
		sc501ai->cur_vts = vts;
		sc501ai_modify_fps_info(sc501ai);
		break;
	case V4L2_CID_HFLIP:
		ret = sc501ai_read_reg(sc501ai->client, SC501AI_FLIP_MIRROR_REG,
				       SC501AI_REG_VALUE_08BIT, &val);
		if (ret)
			break;
		if (ctrl->val)
			val |= SC501AI_MIRROR_MASK;
		else
			val &= ~SC501AI_MIRROR_MASK;
		ret |= sc501ai_write_reg(sc501ai->client, SC501AI_FLIP_MIRROR_REG,
					 SC501AI_REG_VALUE_08BIT, val);
		break;
	case V4L2_CID_VFLIP:
		ret = sc501ai_read_reg(sc501ai->client,
				       SC501AI_FLIP_MIRROR_REG,
				       SC501AI_REG_VALUE_08BIT, &val);
		if (ret)
			break;
		denominator = sc501ai->cur_mode->max_fps.denominator;
		numerator = sc501ai->cur_mode->max_fps.numerator;
		def_fps = denominator / numerator;
		cur_fps = def_fps * sc501ai->cur_mode->vts_def / sc501ai->cur_vts;
		if (cur_fps > 25) {
			vts = def_fps * sc501ai->cur_mode->vts_def / 25;
			ret = sc501ai_write_reg(sc501ai->client,
						SC501AI_REG_VTS_H,
						SC501AI_REG_VALUE_08BIT,
						(vts >> 8) & 0x7f);
			ret |= sc501ai_write_reg(sc501ai->client,
						SC501AI_REG_VTS_L,
						SC501AI_REG_VALUE_08BIT,
						vts & 0xff);
			delay_time = 1000000 / 25;//one frame interval
			delay_time *= 2;
			usleep_range(delay_time, delay_time + 1000);
		}

		if (ctrl->val)
			val |= SC501AI_FLIP_MASK;
		else
			val &= ~SC501AI_FLIP_MASK;

		ret |= sc501ai_write_reg(sc501ai->client,
					 SC501AI_FLIP_MIRROR_REG,
					 SC501AI_REG_VALUE_08BIT,
					 val);
		if (cur_fps > 25) {
			usleep_range(delay_time, delay_time + 1000);
			vts = sc501ai->cur_vts;
			ret = sc501ai_write_reg(sc501ai->client,
						SC501AI_REG_VTS_H,
						SC501AI_REG_VALUE_08BIT,
						(vts >> 8) & 0x7f);
			ret |= sc501ai_write_reg(sc501ai->client,
						SC501AI_REG_VTS_L,
						SC501AI_REG_VALUE_08BIT,
						vts & 0xff);
		}
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc501ai_ctrl_ops = {
	.s_ctrl = sc501ai_set_ctrl,
};

static int sc501ai_initialize_controls(struct sc501ai *sc501ai)
{
	const struct sc501ai_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &sc501ai->ctrl_handler;
	mode = sc501ai->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &sc501ai->mutex;
	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, SC501AI_PIXEL_RATE_396M_10BIT, 1, SC501AI_PIXEL_RATE_396M_10BIT);
	h_blank = mode->hts_def - mode->width;

	sc501ai->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (sc501ai->hblank)
		sc501ai->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	vblank_def = mode->vts_def - mode->height;
	sc501ai->cur_vts = mode->vts_def;

	sc501ai->vblank = v4l2_ctrl_new_std(handler, &sc501ai_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    SC501AI_VTS_MAX - mode->height,
					    1, vblank_def);
	exposure_max = mode->vts_def - 10;
	sc501ai->exposure = v4l2_ctrl_new_std(handler, &sc501ai_ctrl_ops,
					      V4L2_CID_EXPOSURE, SC501AI_EXPOSURE_MIN,
					      exposure_max, SC501AI_EXPOSURE_STEP,
					      mode->exp_def);
	sc501ai->anal_gain = v4l2_ctrl_new_std(handler, &sc501ai_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN, SC501AI_GAIN_MIN,
					       SC501AI_GAIN_MAX, SC501AI_GAIN_STEP,
					       SC501AI_GAIN_DEFAULT);

	v4l2_ctrl_new_std(handler, &sc501ai_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(handler, &sc501ai_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (handler->error) {
		ret = handler->error;
		dev_err(&sc501ai->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}
	sc501ai->subdev.ctrl_handler = handler;
	sc501ai->has_init_exp = false;
	sc501ai->cur_fps = mode->max_fps;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);
	return ret;
}

static int sc501ai_check_sensor_id(struct sc501ai *sc501ai,
				   struct i2c_client *client)
{
	struct device *dev = &sc501ai->client->dev;
	u32 id = 0;
	int ret;

	if (sc501ai->is_thunderboot) {
		dev_info(dev, "Enable thunderboot mode, skip sensor id check\n");
		return 0;
	}

	ret = sc501ai_read_reg(client, SC501AI_REG_CHIP_ID,
			       SC501AI_REG_VALUE_16BIT, &id);
	if (id != SC501AI_CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected SC%06x sensor\n", SC501AI_CHIP_ID);

	return 0;
}

static int sc501ai_configure_regulators(struct sc501ai *sc501ai)
{
	unsigned int i;

	for (i = 0; i < sc501ai_NUM_SUPPLIES; i++)
		sc501ai->supplies[i].supply = sc501ai_supply_names[i];

	return devm_regulator_bulk_get(&sc501ai->client->dev,
				       sc501ai_NUM_SUPPLIES,
				       sc501ai->supplies);
}

static int sc501ai_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc501ai *sc501ai;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	sc501ai = devm_kzalloc(dev, sizeof(*sc501ai), GFP_KERNEL);
	if (!sc501ai)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc501ai->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc501ai->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc501ai->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc501ai->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	sc501ai->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);

	sc501ai->client = client;
	sc501ai->cur_mode = &supported_modes[0];

	sc501ai->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc501ai->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	if (sc501ai->is_thunderboot) {
		sc501ai->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
		if (IS_ERR(sc501ai->reset_gpio))
			dev_warn(dev, "Failed to get reset-gpios\n");

		sc501ai->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_ASIS);
		if (IS_ERR(sc501ai->pwdn_gpio))
			dev_warn(dev, "Failed to get pwdn-gpios\n");
	} else {
		sc501ai->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
		if (IS_ERR(sc501ai->reset_gpio))
			dev_warn(dev, "Failed to get reset-gpios\n");

		sc501ai->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
		if (IS_ERR(sc501ai->pwdn_gpio))
			dev_warn(dev, "Failed to get pwdn-gpios\n");
	}

	sc501ai->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc501ai->pinctrl)) {
		sc501ai->pins_default =
			pinctrl_lookup_state(sc501ai->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc501ai->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		sc501ai->pins_sleep =
			pinctrl_lookup_state(sc501ai->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc501ai->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = sc501ai_configure_regulators(sc501ai);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&sc501ai->mutex);

	sd = &sc501ai->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc501ai_subdev_ops);
	ret = sc501ai_initialize_controls(sc501ai);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc501ai_power_on(sc501ai);
	if (ret)
		goto err_free_handler;

	ret = sc501ai_check_sensor_id(sc501ai, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc501ai_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc501ai->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc501ai->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc501ai->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sc501ai->module_index, facing,
		 SC501AI_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	if (sc501ai->is_thunderboot)
		pm_runtime_get_sync(dev);
	else
		pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__sc501ai_power_off(sc501ai);
err_free_handler:
	v4l2_ctrl_handler_free(&sc501ai->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc501ai->mutex);

	return ret;
}

static int sc501ai_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc501ai *sc501ai = to_sc501ai(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc501ai->ctrl_handler);
	mutex_destroy(&sc501ai->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc501ai_power_off(sc501ai);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc501ai_of_match[] = {
	{ .compatible = "smartsens,sc501ai" },
	{},
};
MODULE_DEVICE_TABLE(of, sc501ai_of_match);
#endif

static const struct i2c_device_id sc501ai_match_id[] = {
	{ "smartsens,sc501ai", 0 },
	{ },
};

static struct i2c_driver sc501ai_i2c_driver = {
	.driver = {
		.name = SC501AI_NAME,
		.pm = &sc501ai_pm_ops,
		.of_match_table = of_match_ptr(sc501ai_of_match),
	},
	.probe		= &sc501ai_probe,
	.remove		= &sc501ai_remove,
	.id_table	= sc501ai_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc501ai_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc501ai_i2c_driver);
}

#if defined(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP) && !defined(CONFIG_INITCALL_ASYNC)
subsys_initcall(sensor_mod_init);
#else
device_initcall_sync(sensor_mod_init);
#endif
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("smartsens sc501ai sensor driver");
MODULE_LICENSE("GPL");
