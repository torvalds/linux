// SPDX-License-Identifier: GPL-2.0
/*
 * os03b10 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 first version.
 */

//define DEBUG
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
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include <linux/rk-preisp.h>
#include "../platform/rockchip/isp/rkisp_tb_helper.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x01)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define OS03B10_CHIP_ID			0x5303
#define OS03B10_REG_CHIP_ID_H		0x02
#define OS03B10_REG_CHIP_ID_L		0x03

#define OS03B10_XVCLK_FREQ		24000000
#define BITS_PER_SAMPLE			10
#define MIPI_FREQ_270M			270000000
#define OS03B10_LANES			2
#define PIXEL_RATE_WITH_270M	(MIPI_FREQ_270M * OS03B10_LANES * 2 / BITS_PER_SAMPLE)

#define OS03B10_REG_PAGE_SELECT		0xfd

#define OS03B10_REG_EXP_H		0x03
#define OS03B10_REG_EXP_L		0x04
#define OS03B10_EXPOSURE_MIN		4
#define OS03B10_EXPOSURE_STEP		1

#define OS03B10_REG_AGAIN		0x24
#define OS03B10_REG_DGAIN_H		0x37
#define OS03B10_REG_DGAIN_L		0x39
#define OS03B10_GAIN_MIN		0x10
#define OS03B10_GAIN_MAX		0x2000
#define OS03B10_GAIN_STEP		1
#define OS03B10_GAIN_DEFAULT	0x10

#define OS03B10_REG_HTS_H		0x41
#define OS03B10_REG_HTS_L		0x42
#define OS03B10_REG_VTS_H		0x4e
#define OS03B10_REG_VTS_L		0x4f

#define OS03B10_REG_VBLANK_H		0x05
#define OS03B10_REG_VBLANK_L		0x06

#define OS03B10_VTS_MAX			0xffff
#define OS03B10_REG_RESTART		0x01

#define OS03B10_REG_CTRL_MODE		0xb1
#define OS03B10_MODE_SW_STANDBY		0x0
#define OS03B10_MODE_STREAMING		0x03

#define OS03B10_REG_SOFTWARE_RESET	0x20
#define OS03B10_SOFTWARE_RESET_VAL	0x1

#define OS03B10_FLIP_REG		0x3f
#define MIRROR_BIT_MASK			BIT(0)
#define FLIP_BIT_MASK			BIT(1)
#define OS03B10_REG_BAYER_ORDER		0x5e

#define OS03B10_FETCH_EXP_H(VAL)	(((VAL) >> 8) & 0xFF)
#define OS03B10_FETCH_EXP_L(VAL)	((VAL) & 0xFF)

#define OS03B10_FETCH_AGAIN(VAL)	((VAL) & 0xFF)
#define OS03B10_FETCH_DGAIN_H(VAL)	(((VAL) >> 8) & 0xFF)
#define OS03B10_FETCH_DGAIN_L(VAL)	((VAL) & 0xFF)

#define OS03B10_NAME			"os03b10"

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"
#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define REG_DELAY			0xEE
#define REG_NULL			0xEF

#define SENSOR_ID(_msb, _lsb)   ((_msb) << 8 | (_lsb))

static const char * const OS03B10_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",         /* Digital core power */
};

#define OS03B10_NUM_SUPPLIES ARRAY_SIZE(OS03B10_supply_names)

struct regval {
	u8 addr;
	u8 val;
};

struct os03b10_mode {
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

struct os03b10 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OS03B10_NUM_SUPPLIES];
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
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct os03b10_mode *cur_mode;
	u32			cfg_num;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	u8			flip;
};

#define to_os03b10(sd) container_of(sd, struct os03b10, subdev)

static const struct regval os03b10_linear10bit_2304x1296_regs[] = {
	{0xfd, 0x00},
	{0x20, 0x00},
	{0xfd, 0x00},
	{0x36, 0x01},
	{0xfd, 0x00},
	{0x2e, 0x2d},
	{0x2f, 0x01},
	{0x41, 0x12},
	{0x36, 0x00},
	{0xfd, 0x00},
	{0xfd, 0x00},
	{0xfd, 0x00},
	{0xfd, 0x00},
	{REG_DELAY, 0x05},
	{0xfd, 0x00},
	{0xfd, 0x00},
	{0x44, 0x40},
	{0x38, 0x21},
	{0x45, 0x04},
	{0xfd, 0x01},
	{0x03, 0x00},
	{0x04, 0x04},
	{0x06, 0x00},
	{0x24, 0xff},
	{0x01, 0x01},
	{0x18, 0x2f},
	{0x1a, 0x06},
	{0x19, 0x58},
	{0x1b, 0x3c},
	{0x2e, 0x03},
	{0x2f, 0x02},
	{0x30, 0x52},
	{0x3c, 0xca},
	{0xfd, 0x03},
	{0x01, 0x0e},
	{0xfd, 0x01},
	{0x51, 0x0e},
	{0x52, 0x0b},
	{0x57, 0x0b},
	{0x5a, 0xe0},
	{0x66, 0xd0},
	{0x6e, 0x26},
	{0x71, 0x80},
	{0x73, 0x2b},
	{0xb8, 0x1c},
	{0xd0, 0x20},
	{0xd2, 0x8e},
	{0xd3, 0x1a},
	{0xfd, 0x01},
	{0xbd, 0x00},
	{0xd7, 0xbe},
	{0xd8, 0xef},
	{0xe8, 0x09},
	{0xe9, 0x05},
	{0xea, 0x08},
	{0xeb, 0x06},
	{0xfd, 0x03},
	{0x00, 0x5c},
	{0x03, 0xcd},
	{0x06, 0x07},
	{0x07, 0x78},
	{0x08, 0x36},
	{0x09, 0x28},
	{0x0a, 0x0c},
	{0x0b, 0x06},
	{0x0f, 0x13},
	{0xfd, 0x01},
	{0x1d, 0x08},
	{0x1e, 0x18},
	{0x1f, 0x30},
	{0x20, 0x5c},
	{0xbc, 0x00},
	{0xfd, 0x03},
	{0x02, 0x00},
	{0x05, 0x18},
	{0xfd, 0x02},
	{0x5e, 0x22},
	{0x34, 0x80},
	{0xfd, 0x01},
	{0xf0, 0x40},
	{0xf1, 0x40},
	{0xf2, 0x40},
	{0xf3, 0x40},
	{0xfa, 0x5c},
	{0xfb, 0x6b},
	{0xf6, 0x00},
	{0xf7, 0xc0},
	{0xfc, 0x00},
	{0xfe, 0xc0},
	{0xff, 0x88},
	{0xc4, 0x70},
	{0xc5, 0x70},
	{0xc6, 0x70},
	{0xc7, 0x70},
	{0xfd, 0x01},
	{0xce, 0x7c},
	{0x8f, 0x00},
	{0x91, 0x10},
	{0x92, 0x19},
	{0x94, 0x44},
	{0x95, 0x44},
	{0x98, 0x55},
	{0x9d, 0x03},
	{0x9e, 0x5f},
	{0xa4, 0x13},
	{0xa5, 0xff},
	{0xa6, 0xff},
	{0x01, 0x02},
	{0x14, 0x03},
	{REG_NULL, 0x00},
};

/*
 * The width and height must be configured to be
 * the same as the current output resolution of the sensor.
 * The input width of the isp needs to be 16 aligned.
 * The input height of the isp needs to be 8 aligned.
 * If the width or height does not meet the alignment rules,
 * you can configure the cropping parameters with the following function to
 * crop out the appropriate resolution.
 * struct v4l2_subdev_pad_ops {
 *	.get_selection
 * }
 */
static const struct os03b10_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.width = 2304,
		.height = 1296,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x044c,
		.hts_def = 0x054e * 2,
		.vts_def = 0x052d,
		.reg_list = os03b10_linear10bit_2304x1296_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ_270M,
};

/* sensor register write */
static int os03b10_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

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
		"write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int os03b10_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	int i, ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {

		ret = os03b10_write_reg(client, regs[i].addr, regs[i].val);
		if (regs[i].addr == REG_DELAY)
			usleep_range(5 * 1000, 6 * 1000);

		if (ret) {
			dev_err(&client->dev, "%s failed !\n", __func__);
			break;
		}
	}

	return ret;
}

/* sensor register read */
static int os03b10_read_reg(struct i2c_client *client, u8 reg, u8 *val)
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
		"os03b10 read reg(0x%x val:0x%x) failed !\n", reg, *val);

	return ret;
}

static int os03b10_get_reso_dist(const struct os03b10_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct os03b10_mode *
os03b10_find_best_fit(struct os03b10 *os03b10, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < os03b10->cfg_num; i++) {
		dist = os03b10_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
			(supported_modes[i].bus_fmt == framefmt->code)) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int os03b10_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct os03b10 *os03b10 = to_os03b10(sd);
	const struct os03b10_mode *mode;
	s64 h_blank, vblank_def;
	u64 dst_link_freq = 0;
	u64 dst_pixel_rate = 0;

	mutex_lock(&os03b10->mutex);

	mode = os03b10_find_best_fit(os03b10, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&os03b10->mutex);
		return -ENOTTY;
#endif
	} else {
		os03b10->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(os03b10->hblank, h_blank,
					 h_blank, 1, h_blank);

		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(os03b10->vblank, vblank_def,
					 OS03B10_VTS_MAX - mode->height,
					 1, vblank_def);
		if (mode->hdr_mode == NO_HDR) {
			if (mode->bus_fmt == MEDIA_BUS_FMT_SBGGR10_1X10) {
				dst_link_freq = 0;
				dst_pixel_rate = PIXEL_RATE_WITH_270M;
			}
		}
		__v4l2_ctrl_s_ctrl_int64(os03b10->pixel_rate,
					 dst_pixel_rate);
		__v4l2_ctrl_s_ctrl(os03b10->link_freq,
					   dst_link_freq);
	}

	mutex_unlock(&os03b10->mutex);

	return 0;
}

static int os03b10_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct os03b10 *os03b10 = to_os03b10(sd);
	const struct os03b10_mode *mode = os03b10->cur_mode;

	mutex_lock(&os03b10->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&os03b10->mutex);
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
	mutex_unlock(&os03b10->mutex);

	return 0;
}

static int os03b10_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct os03b10 *os03b10 = to_os03b10(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = os03b10->cur_mode->bus_fmt;

	return 0;
}

static int os03b10_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	struct os03b10 *os03b10 = to_os03b10(sd);

	if (fse->index >= os03b10->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int os03b10_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct os03b10 *os03b10 = to_os03b10(sd);
	const struct os03b10_mode *mode = os03b10->cur_mode;

	mutex_lock(&os03b10->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&os03b10->mutex);

	return 0;
}

static int os03b10_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				 struct v4l2_mbus_config *config)
{
	struct os03b10 *os03b10 = to_os03b10(sd);
	const struct os03b10_mode *mode = os03b10->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (OS03B10_LANES - 1) |
		      V4L2_MBUS_CSI2_CHANNEL_0 |
		      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void os03b10_get_module_inf(struct os03b10 *os03b10,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, OS03B10_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, os03b10->module_name, sizeof(inf->base.module));
	strscpy(inf->base.lens, os03b10->len_name, sizeof(inf->base.lens));
}

static long os03b10_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct os03b10 *os03b10 = to_os03b10(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		os03b10_get_module_inf(os03b10, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		if (hdr_cfg->hdr_mode != 0)
			ret = -1;
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = os03b10->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);
		if (stream)
			ret = os03b10_write_reg(os03b10->client, OS03B10_REG_CTRL_MODE,
						OS03B10_MODE_STREAMING);
		else
			ret = os03b10_write_reg(os03b10->client, OS03B10_REG_CTRL_MODE,
						OS03B10_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long os03b10_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_hdr_cfg *hdr;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = os03b10_ioctl(sd, cmd, inf);
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

		ret = os03b10_ioctl(sd, cmd, hdr);
		if (!ret) {
			ret = copy_to_user(up, hdr, sizeof(*hdr));
			if (ret)
				ret = -EFAULT;
		}
		kfree(hdr);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		if (copy_from_user(hdr, up, sizeof(*hdr)))
			return -EFAULT;

		ret = os03b10_ioctl(sd, cmd, hdr);
		kfree(hdr);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;

		ret = os03b10_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __os03b10_start_stream(struct os03b10 *os03b10)
{
	int ret = 0;

	ret |= os03b10_write_array(os03b10->client, os03b10->cur_mode->reg_list);
	if (ret) {
		dev_err(&os03b10->client->dev,
			"write array failed in start stream\n");
		return ret;
	}

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&os03b10->ctrl_handler);
	if (ret)
		return ret;
	if (os03b10->has_init_exp && os03b10->cur_mode->hdr_mode != NO_HDR) {
		ret = os03b10_ioctl(&os03b10->subdev, PREISP_CMD_SET_HDRAE_EXP,
				    &os03b10->init_hdrae_exp);
		if (ret) {
			dev_err(&os03b10->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}

	ret |= os03b10_write_reg(os03b10->client, OS03B10_REG_PAGE_SELECT, 0x01);
	ret |= os03b10_write_reg(os03b10->client, OS03B10_REG_CTRL_MODE,
				 OS03B10_MODE_STREAMING);

	return ret;
}

static int __os03b10_stop_stream(struct os03b10 *os03b10)
{
	os03b10->has_init_exp = false;
	return os03b10_write_reg(os03b10->client, OS03B10_REG_CTRL_MODE,
				 OS03B10_MODE_SW_STANDBY);
}

static int os03b10_s_stream(struct v4l2_subdev *sd, int on)
{
	struct os03b10 *os03b10 = to_os03b10(sd);
	struct i2c_client *client = os03b10->client;
	int ret = 0;

	mutex_lock(&os03b10->mutex);
	on = !!on;
	if (on == os03b10->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __os03b10_start_stream(os03b10);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__os03b10_stop_stream(os03b10);
		pm_runtime_put(&client->dev);
	}

	os03b10->streaming = on;

unlock_and_return:
	mutex_unlock(&os03b10->mutex);

	return ret;
}

static int os03b10_s_power(struct v4l2_subdev *sd, int on)
{
	struct os03b10 *os03b10 = to_os03b10(sd);
	struct i2c_client *client = os03b10->client;
	int ret = 0;

	mutex_lock(&os03b10->mutex);

	/* If the power state is not modified - no work to do. */
	if (os03b10->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_SOFTWARE_RESET,
					 OS03B10_SOFTWARE_RESET_VAL);
		usleep_range(100, 200);

		os03b10->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		os03b10->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&os03b10->mutex);

	return ret;
}

static int __os03b10_power_on(struct os03b10 *os03b10)
{
	int ret;
	struct device *dev = &os03b10->client->dev;

	if (!IS_ERR_OR_NULL(os03b10->pins_default)) {
		ret = pinctrl_select_state(os03b10->pinctrl,
					   os03b10->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(os03b10->xvclk, OS03B10_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(os03b10->xvclk) != OS03B10_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(os03b10->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(os03b10->pwdn_gpio))
		gpiod_direction_output(os03b10->pwdn_gpio, 0);

	if (!IS_ERR(os03b10->reset_gpio))
		gpiod_direction_output(os03b10->reset_gpio, 0);

	ret = regulator_bulk_enable(OS03B10_NUM_SUPPLIES, os03b10->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	/* From spec: delay from power stable to pwdn off: 5ms */
	usleep_range(5000, 6000);
	if (!IS_ERR(os03b10->pwdn_gpio))
		gpiod_direction_output(os03b10->pwdn_gpio, 1);

	/* From spec: delay from pwdn off to reset off */
	usleep_range(4000, 5000);
	if (!IS_ERR(os03b10->reset_gpio))
		gpiod_direction_output(os03b10->reset_gpio, 1);

	/* From spec: 5ms for SCCB initialization */
	usleep_range(9000, 10000);
	return 0;

disable_clk:
	clk_disable_unprepare(os03b10->xvclk);

	return ret;
}

static void __os03b10_power_off(struct os03b10 *os03b10)
{
	int ret;
	struct device *dev = &os03b10->client->dev;

	if (!IS_ERR(os03b10->pwdn_gpio))
		gpiod_direction_output(os03b10->pwdn_gpio, 0);

	clk_disable_unprepare(os03b10->xvclk);

	if (!IS_ERR(os03b10->reset_gpio))
		gpiod_direction_output(os03b10->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(os03b10->pins_sleep)) {
		ret = pinctrl_select_state(os03b10->pinctrl,
					   os03b10->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(OS03B10_NUM_SUPPLIES, os03b10->supplies);
}

static int os03b10_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os03b10 *os03b10 = to_os03b10(sd);

	return __os03b10_power_on(os03b10);
}

static int os03b10_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os03b10 *os03b10 = to_os03b10(sd);

	__os03b10_power_off(os03b10);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int os03b10_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct os03b10 *os03b10 = to_os03b10(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct os03b10_mode *def_mode = &supported_modes[0];

	mutex_lock(&os03b10->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&os03b10->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int os03b10_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct os03b10 *os03b10 = to_os03b10(sd);

	if (fie->index >= os03b10->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops os03b10_pm_ops = {
	SET_RUNTIME_PM_OPS(os03b10_runtime_suspend,
	os03b10_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops os03b10_internal_ops = {
	.open = os03b10_open,
};
#endif

static const struct v4l2_subdev_core_ops os03b10_core_ops = {
	.s_power = os03b10_s_power,
	.ioctl = os03b10_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = os03b10_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops os03b10_video_ops = {
	.s_stream = os03b10_s_stream,
	.g_frame_interval = os03b10_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops os03b10_pad_ops = {
	.enum_mbus_code = os03b10_enum_mbus_code,
	.enum_frame_size = os03b10_enum_frame_sizes,
	.enum_frame_interval = os03b10_enum_frame_interval,
	.get_fmt = os03b10_get_fmt,
	.set_fmt = os03b10_set_fmt,
	.get_mbus_config = os03b10_g_mbus_config,
};

static const struct v4l2_subdev_ops os03b10_subdev_ops = {
	.core	= &os03b10_core_ops,
	.video	= &os03b10_video_ops,
	.pad	= &os03b10_pad_ops,
};


static void os03b10_get_gain_reg(u32 total_gain, u32 *again, u32 *dgain)
{
	u32 step = 0;

	if (total_gain < 256) {/* 1x gain ~ 16x gain*/
		*again = total_gain;
		*dgain = 0x40;
	} else if (total_gain < 512) {/* 16x gain ~ 32x gain */
		step = (total_gain - 256) * 0x40 / 256;
		*again = 0xff;
		*dgain = 0x40 + step;
	} else if (total_gain < 1024) {/* 32x gain ~ 64x gain */
		step = (total_gain - 512) * 0x80 / 512;
		*again = 0xff;
		*dgain = 0x80 + step;
	} else if (total_gain < 2048) {/* 64x gain ~ 128x gain */
		step = (total_gain - 1024) * 0x100 / 1024;
		*again = 0xff;
		*dgain = 0x100 + step;
	} else if (total_gain < 4096) {/* 128x gain ~ 256x gain */
		step = (total_gain - 2048) *  0x200 / 2048;
		*again = 0xff;
		*dgain = 0x200 + step;
	} else if (total_gain <= 8192) {/* 256x gain ~ 512x gain */
		step = (total_gain - 4096) * 0x400 / 4096;
		*again = 0xff;
		*dgain = (0x400 + step) > 0x7ff ? 0x7ff : (0x400 + step);
	}
}

static int os03b10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct os03b10 *os03b10 = container_of(ctrl->handler,
				  struct os03b10, ctrl_handler);
	struct i2c_client *client = os03b10->client;
	s64 max;
	int ret = 0;
	u32 again = 0, dgain = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = os03b10->cur_mode->height + ctrl->val - 9;
		__v4l2_ctrl_modify_range(os03b10->exposure,
					 os03b10->exposure->minimum, max,
					 os03b10->exposure->step,
					 os03b10->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = os03b10_write_reg(os03b10->client,
					OS03B10_REG_PAGE_SELECT, 0x1);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_EXP_H,
					 OS03B10_FETCH_EXP_H(ctrl->val));
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_EXP_L,
					 OS03B10_FETCH_EXP_L(ctrl->val));
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_RESTART, 0x01);
		dev_dbg(&client->dev, "set exposure 0x%x\n", ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		os03b10_get_gain_reg(ctrl->val, &again, &dgain);

		ret = os03b10_write_reg(os03b10->client,
					OS03B10_REG_PAGE_SELECT, 0x01);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_AGAIN,
					 OS03B10_FETCH_AGAIN(again));
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_DGAIN_H,
					 OS03B10_FETCH_DGAIN_H(dgain));
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_DGAIN_L,
					 OS03B10_FETCH_DGAIN_L(dgain));
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_RESTART, 0x01);

		dev_dbg(&client->dev, "set gain 0x%x, again 0x%x, dgain 0x%x\n",
				ctrl->val, again, dgain);
		break;
	case V4L2_CID_VBLANK:
		ret = os03b10_write_reg(os03b10->client,
					OS03B10_REG_PAGE_SELECT, 0x01);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_VBLANK_H,
					 (ctrl->val >> 8) & 0xFF);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_VBLANK_L,
					 ctrl->val & 0xFF);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_RESTART, 0x01);
		dev_dbg(&client->dev, "set vblank 0x%x\n", ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		break;
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			os03b10->flip |= MIRROR_BIT_MASK;
		else
			os03b10->flip &= ~MIRROR_BIT_MASK;

		ret = os03b10_write_reg(os03b10->client,
					OS03B10_REG_PAGE_SELECT, 0x01);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_FLIP_REG, os03b10->flip);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_PAGE_SELECT, 0x02);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_BAYER_ORDER, 0x32);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_PAGE_SELECT, 0x01);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_RESTART, 0x01);
		dev_dbg(&client->dev, "set hflip 0x%x\n", os03b10->flip);
		break;
	case V4L2_CID_VFLIP:
		if (ctrl->val)
			os03b10->flip |= FLIP_BIT_MASK;
		else
			os03b10->flip &= ~FLIP_BIT_MASK;

		ret = os03b10_write_reg(os03b10->client,
					OS03B10_REG_PAGE_SELECT, 0x01);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_FLIP_REG, os03b10->flip);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_PAGE_SELECT, 0x02);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_BAYER_ORDER, 0x32);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_PAGE_SELECT, 0x01);
		ret |= os03b10_write_reg(os03b10->client,
					 OS03B10_REG_RESTART, 0x01);
		dev_dbg(&client->dev, "set vflip 0x%x\n", os03b10->flip);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
				 __func__, ctrl->id, ctrl->val);
		break;
	}
	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops os03b10_ctrl_ops = {
	.s_ctrl = os03b10_set_ctrl,
};

static int os03b10_initialize_controls(struct os03b10 *os03b10)
{
	const struct os03b10_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	u64 dst_link_freq = 0;
	u64 dst_pixel_rate = 0;

	handler = &os03b10->ctrl_handler;
	mode = os03b10->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &os03b10->mutex;

	os03b10->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
						    V4L2_CID_LINK_FREQ,
						    1, 0,
						    link_freq_menu_items);

	if (os03b10->cur_mode->bus_fmt == MEDIA_BUS_FMT_SBGGR10_1X10) {
		dst_link_freq = 0;
		dst_pixel_rate = PIXEL_RATE_WITH_270M;
	}
	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	os03b10->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
						V4L2_CID_PIXEL_RATE,
						0, PIXEL_RATE_WITH_270M,
						1, dst_pixel_rate);

	__v4l2_ctrl_s_ctrl(os03b10->link_freq, dst_link_freq);

	h_blank = mode->hts_def - mode->width;
	os03b10->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (os03b10->hblank)
		os03b10->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = 0;
	os03b10->vblank = v4l2_ctrl_new_std(handler, &os03b10_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    OS03B10_VTS_MAX - mode->height,
					    1, vblank_def);

	exposure_max = mode->vts_def - 9;
	os03b10->exposure = v4l2_ctrl_new_std(handler, &os03b10_ctrl_ops,
					      V4L2_CID_EXPOSURE, OS03B10_EXPOSURE_MIN,
					      exposure_max, OS03B10_EXPOSURE_STEP,
					      mode->exp_def);

	os03b10->anal_gain = v4l2_ctrl_new_std(handler, &os03b10_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN, OS03B10_GAIN_MIN,
					       OS03B10_GAIN_MAX, OS03B10_GAIN_STEP,
					       OS03B10_GAIN_DEFAULT);

	v4l2_ctrl_new_std(handler, &os03b10_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(handler, &os03b10_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	os03b10->flip = 0;

	if (handler->error) {
		ret = handler->error;
		dev_err(&os03b10->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	os03b10->subdev.ctrl_handler = handler;
	os03b10->has_init_exp = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int os03b10_check_sensor_id(struct os03b10 *os03b10,
				   struct i2c_client *client)
{
	struct device *dev = &os03b10->client->dev;
	u8 id_h = 0, id_l = 0;
	u32 id = 0;
	int ret;

	ret = os03b10_read_reg(client, OS03B10_REG_CHIP_ID_H, &id_h);
	ret |= os03b10_read_reg(client, OS03B10_REG_CHIP_ID_L, &id_l);

	id = SENSOR_ID(id_h, id_l);
	if (id != OS03B10_CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	dev_info(dev, "Detected OV%06x sensor\n", OS03B10_CHIP_ID);
	return 0;
}

static int os03b10_configure_regulators(struct os03b10 *os03b10)
{
	unsigned int i;

	for (i = 0; i < OS03B10_NUM_SUPPLIES; i++)
		os03b10->supplies[i].supply = OS03B10_supply_names[i];

	return devm_regulator_bulk_get(&os03b10->client->dev,
					OS03B10_NUM_SUPPLIES,
					os03b10->supplies);
}

static int os03b10_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct os03b10 *os03b10;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	os03b10 = devm_kzalloc(dev, sizeof(*os03b10), GFP_KERNEL);
	if (!os03b10)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &os03b10->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &os03b10->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &os03b10->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &os03b10->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}
	os03b10->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < os03b10->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			os03b10->cur_mode = &supported_modes[i];
			break;
		}
	}
	os03b10->client = client;

	os03b10->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(os03b10->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	os03b10->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(os03b10->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	os03b10->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_ASIS);
	if (IS_ERR(os03b10->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	os03b10->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(os03b10->pinctrl)) {
		os03b10->pins_default =
			pinctrl_lookup_state(os03b10->pinctrl,
				 OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(os03b10->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		os03b10->pins_sleep =
			pinctrl_lookup_state(os03b10->pinctrl,
				 OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(os03b10->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = os03b10_configure_regulators(os03b10);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&os03b10->mutex);

	sd = &os03b10->subdev;
	v4l2_i2c_subdev_init(sd, client, &os03b10_subdev_ops);
	ret = os03b10_initialize_controls(os03b10);
	if (ret)
		goto err_destroy_mutex;

	ret = __os03b10_power_on(os03b10);
	if (ret)
		goto err_free_handler;

	ret = os03b10_check_sensor_id(os03b10, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &os03b10_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	os03b10->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &os03b10->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(os03b10->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 os03b10->module_index, facing,
		 OS03B10_NAME, dev_name(sd->dev));
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
	__os03b10_power_off(os03b10);
err_free_handler:
	v4l2_ctrl_handler_free(&os03b10->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&os03b10->mutex);

	return ret;
}

static int os03b10_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os03b10 *os03b10 = to_os03b10(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&os03b10->ctrl_handler);
	mutex_destroy(&os03b10->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__os03b10_power_off(os03b10);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id os03b10_of_match[] = {
	{ .compatible = "ovti,os03b10" },
	{},
};
MODULE_DEVICE_TABLE(of, os03b10_of_match);
#endif

static const struct i2c_device_id os03b10_match_id[] = {
	{ "ovti,os03b10", 0 },
	{ },
};

static struct i2c_driver os03b10_i2c_driver = {
	.driver = {
		.name = OS03B10_NAME,
		.pm = &os03b10_pm_ops,
		.of_match_table = of_match_ptr(os03b10_of_match),
	},
	.probe		= &os03b10_probe,
	.remove		= &os03b10_remove,
	.id_table	= os03b10_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&os03b10_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&os03b10_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision os03b10 sensor driver");
MODULE_LICENSE("GPL");
