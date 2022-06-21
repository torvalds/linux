// SPDX-License-Identifier: GPL-2.0
/*
 * ov02b10 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 fix power on & off sequence
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

#define MIPI_FREQ_360M			360000000
#define PIXEL_RATE_WITH_360M		(MIPI_FREQ_360M * 2 / 10 * 4)

#define OV02B10_XVCLK_FREQ		24000000

#define OV02B10_CHIP_ID			0x2B
#define OV02B10_REG_CHIP_ID_H		0x02
#define OV02B10_REG_CHIP_ID_L		0x03

#define OV02B10_VTS_MAX			0xFFFF

#define OV02B10_GAIN_MIN		0x10
#define OV02B10_GAIN_MAX		0x3FF
#define OV02B10_GAIN_STEP		1
#define OV02B10_GAIN_DEFAULT		0x10

#define OV02B10_EXPOSURE_MIN		4
#define OV02B10_EXPOSURE_STEP		1

#define OV02B10_REG_PAGE_SELECT		0xFD

#define OV02B10_REG_EXP_H		0x0E
#define OV02B10_REG_EXP_L		0x0F

#define OV02B10_REG_AGAIN		0x22
#define OV02B10_REG_DGAIN		0x9B
#define OV02B10_REG_RESTART		0xFE

#define OV02B10_REG_HTS_H		0x25
#define OV02B10_REG_HTS_L		0x26
#define OV02B10_REG_VTS_H		0x27
#define OV02B10_REG_VTS_L		0x28
#define OV02B10_REG_VBLANK_H		0x14
#define OV02B10_REG_VBLANK_L		0x15

#define OV02B10_REG_CTRL_MODE		0xFB
#define OV02B10_MODE_SW_STANDBY		0x0
#define OV02B10_MODE_STREAMING		BIT(0)

#define OV02B10_REG_SOFTWARE_RESET	0xFC
#define OV02B10_SOFTWARE_RESET_VAL	0x1

#define OV02B10_FLIP_REG		0x12
#define MIRROR_BIT_MASK			BIT(0)
#define FLIP_BIT_MASK			BIT(1)

#define OV02B10_LANES			1
#define OV02B10_NAME			"ov02b10"

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"
#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define REG_NULL			0xFF

#define SENSOR_ID(_msb, _lsb)   ((_msb) << 8 | (_lsb))

static const char * const OV02B10_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
};

#define OV02B10_NUM_SUPPLIES ARRAY_SIZE(OV02B10_supply_names)

struct regval {
	u8 addr;
	u8 val;
};

struct ov02b10_mode {
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

struct ov02b10 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV02B10_NUM_SUPPLIES];

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
	struct v4l2_ctrl	*h_flip;
	struct v4l2_ctrl	*v_flip;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct ov02b10_mode *cur_mode;
	u32			cfg_num;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	u8			flip;
};

#define to_ov02b10(sd) container_of(sd, struct ov02b10, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval ov02b10_linear10bit_1600x1200_regs[] = {
	{0xfc, 0x01},
	{0xfd, 0x00},
	{0xfd, 0x00},
	{0x24, 0x02},
	{0x25, 0x06},
	{0x29, 0x03},
	{0x2a, 0x34},
	{0x1e, 0x17},
	{0x33, 0x07},
	{0x35, 0x07},
	{0x4a, 0x0c},
	{0x3a, 0x05},
	{0x3b, 0x02},
	{0x3e, 0x00},
	{0x46, 0x01},
	{0x6d, 0x03},
	{0xfd, 0x01},
	{0x0e, 0x02},
	{0x0f, 0x1a},
	{0x18, 0x00},
	{0x22, 0xff},
	{0x23, 0x02},
	{0x17, 0x2c},
	{0x19, 0x20},
	{0x1b, 0x06},
	{0x1c, 0x04},
	{0x20, 0x03},
	{0x30, 0x01},
	{0x33, 0x01},
	{0x31, 0x0a},
	{0x32, 0x09},
	{0x38, 0x01},
	{0x39, 0x01},
	{0x3a, 0x01},
	{0x3b, 0x01},
	{0x4f, 0x04},
	{0x4e, 0x05},
	{0x50, 0x01},
	{0x35, 0x0c},
	{0x45, 0x2a},
	{0x46, 0x2a},
	{0x47, 0x2a},
	{0x48, 0x2a},
	{0x4a, 0x2c},
	{0x4b, 0x2c},
	{0x4c, 0x2c},
	{0x4d, 0x2c},
	{0x56, 0x3a},
	{0x57, 0x0a},
	{0x58, 0x24},
	{0x59, 0x20},
	{0x5a, 0x0a},
	{0x5b, 0xff},
	{0x37, 0x0a},
	{0x42, 0x0e},
	{0x68, 0x90},
	{0x69, 0xcd},
	{0x6a, 0x8f},
	{0x7c, 0x0a},
	{0x7d, 0x0a},
	{0x7e, 0x0a},
	{0x7f, 0x08},
	{0x83, 0x14},
	{0x84, 0x14},
	{0x86, 0x14},
	{0x87, 0x07},
	{0x88, 0x0f},
	{0x94, 0x02},
	{0x98, 0xd1},
	{0xfe, 0x02},
	{0xfd, 0x03},
	{0x97, 0x6c},
	{0x98, 0x60},
	{0x99, 0x60},
	{0x9a, 0x6c},
	{0xa1, 0x40},
	{0xaf, 0x04},
	{0xb1, 0x40},
	{0xae, 0x0d},
	{0x88, 0x5b},
	{0x89, 0x7c},
	{0xb4, 0x05},
	{0x8c, 0x40},
	{0x8e, 0x40},
	{0x90, 0x40},
	{0x92, 0x40},
	{0x9b, 0x46},
	{0xac, 0x40},
	{0xfd, 0x00},
	{0x5a, 0x15},
	{0x74, 0x01},
	{0xfd, 0x00},
	{0x50, 0x40},
	{0x52, 0xb0},
	{0xfd, 0x01},
	{0x03, 0x70},
	{0x05, 0x10},
	{0x07, 0x20},
	{0x09, 0xb0},
	{0xfd, 0x03},
	{0xc2, 0x01},
	{0xfb, 0x01},
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
static const struct ov02b10_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.width = 1600,
		.height = 1200,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x02ea,
		.hts_def = 0x06ac,
		.vts_def = 0x04c4,
		.reg_list = ov02b10_linear10bit_1600x1200_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ_360M,
};

static int __ov02b10_power_on(struct ov02b10 *ov02b10);

static int ov02b10_check_sensor_id(struct ov02b10 *ov02b10,
				  struct i2c_client *client);

/* sensor register write */
static int ov02b10_write_reg(struct i2c_client *client, u8 reg, u8 val)
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
		"ov02b10 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int ov02b10_write_array(struct i2c_client *client,
				  const struct regval *regs)
{
	int i, ret = 0;

	i = 0;
	while (regs[i].addr != REG_NULL) {
		ret = ov02b10_write_reg(client, regs[i].addr, regs[i].val);
		if (ret) {
			dev_err(&client->dev, "%s failed !\n", __func__);
			break;
		}
		i++;
	}

	return ret;
}

/* sensor register read */
static int ov02b10_read_reg(struct i2c_client *client, u8 reg, u8 *val)
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
		"ov02b10 read reg(0x%x val:0x%x) failed !\n", reg, *val);

	return ret;
}

static int ov02b10_get_reso_dist(const struct ov02b10_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov02b10_mode *
ov02b10_find_best_fit(struct ov02b10 *ov02b10, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ov02b10->cfg_num; i++) {
		dist = ov02b10_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
			(supported_modes[i].bus_fmt == framefmt->code)) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov02b10_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov02b10 *ov02b10 = to_ov02b10(sd);
	const struct ov02b10_mode *mode;
	s64 h_blank, vblank_def;
	u64 dst_link_freq = 0;
	u64 dst_pixel_rate = 0;

	mutex_lock(&ov02b10->mutex);

	mode = ov02b10_find_best_fit(ov02b10, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov02b10->mutex);
		return -ENOTTY;
#endif
	} else {
		ov02b10->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov02b10->hblank, h_blank,
					 h_blank, 1, h_blank);

		/* From spec: vstart is 0xc by default */
		vblank_def = mode->vts_def - mode->height - 0xc;
		__v4l2_ctrl_modify_range(ov02b10->vblank, vblank_def,
					 OV02B10_VTS_MAX - mode->height,
					 1, vblank_def);
		if (mode->hdr_mode == NO_HDR) {
			if (mode->bus_fmt == MEDIA_BUS_FMT_SBGGR10_1X10) {
				dst_link_freq = 0;
				dst_pixel_rate = PIXEL_RATE_WITH_360M;
			}
		}
		__v4l2_ctrl_s_ctrl_int64(ov02b10->pixel_rate,
					 dst_pixel_rate);
		__v4l2_ctrl_s_ctrl(ov02b10->link_freq,
				   dst_link_freq);
	}

	mutex_unlock(&ov02b10->mutex);

	return 0;
}

static int ov02b10_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov02b10 *ov02b10 = to_ov02b10(sd);
	const struct ov02b10_mode *mode = ov02b10->cur_mode;

	mutex_lock(&ov02b10->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov02b10->mutex);
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
	mutex_unlock(&ov02b10->mutex);

	return 0;
}

static int ov02b10_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct ov02b10 *ov02b10 = to_ov02b10(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = ov02b10->cur_mode->bus_fmt;

	return 0;
}

static int ov02b10_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct ov02b10 *ov02b10 = to_ov02b10(sd);

	if (fse->index >= ov02b10->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int ov02b10_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct ov02b10 *ov02b10 = to_ov02b10(sd);
	const struct ov02b10_mode *mode = ov02b10->cur_mode;

	mutex_lock(&ov02b10->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&ov02b10->mutex);

	return 0;
}

static int ov02b10_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct ov02b10 *ov02b10 = to_ov02b10(sd);
	const struct ov02b10_mode *mode = ov02b10->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (OV02B10_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	if (mode->hdr_mode == HDR_X2)
		val = 1 << (OV02B10_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		V4L2_MBUS_CSI2_CHANNEL_1;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void ov02b10_get_module_inf(struct ov02b10 *ov02b10,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, OV02B10_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, ov02b10->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, ov02b10->len_name, sizeof(inf->base.lens));
}

static long ov02b10_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ov02b10 *ov02b10 = to_ov02b10(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		ret = -1;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		if (hdr_cfg->hdr_mode != 0)
			ret = -1;
		break;
	case RKMODULE_GET_MODULE_INFO:
		ov02b10_get_module_inf(ov02b10, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = ov02b10->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);
		if (stream)
			ret = ov02b10_write_reg(ov02b10->client, OV02B10_REG_CTRL_MODE,
						OV02B10_MODE_STREAMING);
		else
			ret = ov02b10_write_reg(ov02b10->client, OV02B10_REG_CTRL_MODE,
						OV02B10_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ov02b10_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	long ret;
	u32 cg = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = ov02b10_ioctl(sd, cmd, inf);
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
			ret = ov02b10_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = ov02b10_ioctl(sd, cmd, hdr);
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

		ret = copy_from_user(hdr, up, sizeof(*hdr));
		if (!ret)
			ret = ov02b10_ioctl(sd, cmd, hdr);
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
			ret = ov02b10_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		ret = copy_from_user(&cg, up, sizeof(cg));
		if (!ret)
			ret = ov02b10_ioctl(sd, cmd, &cg);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = ov02b10_ioctl(sd, cmd, &stream);
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

static int __ov02b10_start_stream(struct ov02b10 *ov02b10)
{
	int ret;

	ret = ov02b10_write_array(ov02b10->client, ov02b10->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&ov02b10->ctrl_handler);
	if (ret)
		return ret;
	if (ov02b10->has_init_exp && ov02b10->cur_mode->hdr_mode != NO_HDR) {
		ret = ov02b10_ioctl(&ov02b10->subdev, PREISP_CMD_SET_HDRAE_EXP,
				    &ov02b10->init_hdrae_exp);
		if (ret) {
			dev_err(&ov02b10->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}
	return ov02b10_write_reg(ov02b10->client, OV02B10_REG_CTRL_MODE, OV02B10_MODE_STREAMING);
}

static int __ov02b10_stop_stream(struct ov02b10 *ov02b10)
{
	ov02b10->has_init_exp = false;
	return ov02b10_write_reg(ov02b10->client, OV02B10_REG_CTRL_MODE, OV02B10_MODE_SW_STANDBY);
}

static int ov02b10_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov02b10 *ov02b10 = to_ov02b10(sd);
	struct i2c_client *client = ov02b10->client;
	int ret = 0;

	mutex_lock(&ov02b10->mutex);
	on = !!on;
	if (on == ov02b10->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __ov02b10_start_stream(ov02b10);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__ov02b10_stop_stream(ov02b10);
		pm_runtime_put(&client->dev);
	}

	ov02b10->streaming = on;

unlock_and_return:
	mutex_unlock(&ov02b10->mutex);

	return ret;
}

static int ov02b10_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov02b10 *ov02b10 = to_ov02b10(sd);
	struct i2c_client *client = ov02b10->client;
	int ret = 0;

	mutex_lock(&ov02b10->mutex);

	/* If the power state is not modified - no work to do. */
	if (ov02b10->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_REG_SOFTWARE_RESET,
					 OV02B10_SOFTWARE_RESET_VAL);
		usleep_range(100, 200);

		ov02b10->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		ov02b10->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&ov02b10->mutex);

	return ret;
}

static int ov02b10_enable_regulators(struct ov02b10 *ov02b10,
				    struct regulator_bulk_data *consumers)
{
	int i, j;
	int ret = 0;
	struct device *dev = &ov02b10->client->dev;
	int num_consumers = OV02B10_NUM_SUPPLIES;

	for (i = 0; i < num_consumers; i++) {

		ret = regulator_enable(consumers[i].consumer);
		if (ret < 0) {
			dev_err(dev, "Failed to enable regulator: %s\n",
				consumers[i].supply);
			goto err;
		}
	}
	return 0;
err:
	for (j = 0; j < i; j++)
		regulator_disable(consumers[j].consumer);

	return ret;
}

static int __ov02b10_power_on(struct ov02b10 *ov02b10)
{
	int ret;
	struct device *dev = &ov02b10->client->dev;

	if (!IS_ERR_OR_NULL(ov02b10->pins_default)) {
		ret = pinctrl_select_state(ov02b10->pinctrl,
					   ov02b10->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(ov02b10->xvclk, OV02B10_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(ov02b10->xvclk) != OV02B10_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");

	if (!IS_ERR(ov02b10->pwdn_gpio))
		gpiod_direction_output(ov02b10->pwdn_gpio, 1);

	if (!IS_ERR(ov02b10->reset_gpio))
		gpiod_direction_output(ov02b10->reset_gpio, 1);

	ret = ov02b10_enable_regulators(ov02b10, ov02b10->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}
	usleep_range(100, 110);
	ret = clk_prepare_enable(ov02b10->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	/* From spec: delay from power stable to pwdn off: 5ms */
	usleep_range(5000, 6000);
	if (!IS_ERR(ov02b10->pwdn_gpio))
		gpiod_direction_output(ov02b10->pwdn_gpio, 0);

	/* From spec: delay from pwdn off to reset off */
	usleep_range(4000, 5000);
	if (!IS_ERR(ov02b10->reset_gpio))
		gpiod_direction_output(ov02b10->reset_gpio, 0);

	/* From spec: 5ms for SCCB initialization */
	usleep_range(5000, 6000);
	return 0;

disable_clk:
	clk_disable_unprepare(ov02b10->xvclk);

	return ret;
}

static void __ov02b10_power_off(struct ov02b10 *ov02b10)
{
	int ret;
	struct device *dev = &ov02b10->client->dev;

	if (!IS_ERR(ov02b10->reset_gpio))
		gpiod_direction_output(ov02b10->reset_gpio, 1);

	clk_disable_unprepare(ov02b10->xvclk);

	if (!IS_ERR(ov02b10->pwdn_gpio))
		gpiod_direction_output(ov02b10->pwdn_gpio, 1);

	if (!IS_ERR_OR_NULL(ov02b10->pins_sleep)) {
		ret = pinctrl_select_state(ov02b10->pinctrl,
					   ov02b10->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(OV02B10_NUM_SUPPLIES, ov02b10->supplies);
}

static int ov02b10_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov02b10 *ov02b10 = to_ov02b10(sd);

	return __ov02b10_power_on(ov02b10);
}

static int ov02b10_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov02b10 *ov02b10 = to_ov02b10(sd);

	__ov02b10_power_off(ov02b10);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov02b10_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov02b10 *ov02b10 = to_ov02b10(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov02b10_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov02b10->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov02b10->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int ov02b10_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct ov02b10 *ov02b10 = to_ov02b10(sd);

	if (fie->index >= ov02b10->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops ov02b10_pm_ops = {
	SET_RUNTIME_PM_OPS(ov02b10_runtime_suspend,
			   ov02b10_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov02b10_internal_ops = {
	.open = ov02b10_open,
};
#endif

static const struct v4l2_subdev_core_ops ov02b10_core_ops = {
	.s_power = ov02b10_s_power,
	.ioctl = ov02b10_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = ov02b10_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops ov02b10_video_ops = {
	.s_stream = ov02b10_s_stream,
	.g_frame_interval = ov02b10_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops ov02b10_pad_ops = {
	.enum_mbus_code = ov02b10_enum_mbus_code,
	.enum_frame_size = ov02b10_enum_frame_sizes,
	.enum_frame_interval = ov02b10_enum_frame_interval,
	.get_fmt = ov02b10_get_fmt,
	.set_fmt = ov02b10_set_fmt,
	.get_mbus_config = ov02b10_g_mbus_config,
};

static const struct v4l2_subdev_ops ov02b10_subdev_ops = {
	.core	= &ov02b10_core_ops,
	.video	= &ov02b10_video_ops,
	.pad	= &ov02b10_pad_ops,
};

static int ov02b10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov02b10 *ov02b10 = container_of(ctrl->handler,
					     struct ov02b10, ctrl_handler);
	struct i2c_client *client = ov02b10->client;
	s64 max;
	int ret = 0;
	u8 again = 0, dgain = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov02b10->cur_mode->height + ctrl->val - 7;
		__v4l2_ctrl_modify_range(ov02b10->exposure,
					 ov02b10->exposure->minimum, max,
					 ov02b10->exposure->step,
					 ov02b10->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = ov02b10_write_reg(ov02b10->client,
					OV02B10_REG_PAGE_SELECT, 0x1);
		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_REG_EXP_H, (ctrl->val >> 8) & 0xFF);
		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_REG_EXP_L, ctrl->val & 0xFF);
		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_REG_RESTART, 0x02);
		dev_dbg(&client->dev, "set exposure 0x%x\n", ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (ctrl->val > 248) {
			again = 248;
			dgain = (ctrl->val * 64 / 248 > 0xff) ? 0xff : ctrl->val * 64 / 248;
		} else {
			dgain = 64;
			again = ctrl->val;
		}
		ret = ov02b10_write_reg(ov02b10->client,
					OV02B10_REG_PAGE_SELECT, 0x01);
		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_REG_AGAIN, again);
		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_REG_PAGE_SELECT, 0x03);
		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_REG_DGAIN, dgain);
		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_REG_RESTART, 0x02);

		dev_dbg(&client->dev, "set gain 0x%x, again = %#x(%u), dgain = %#x(%u)\n",
			ctrl->val, again, again, dgain, dgain);
		break;
	case V4L2_CID_VBLANK:
		ret = ov02b10_write_reg(ov02b10->client,
					OV02B10_REG_PAGE_SELECT, 0x01);
		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_REG_VBLANK_H, (ctrl->val >> 8) & 0xFF);
		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_REG_VBLANK_L, ctrl->val & 0xFF);
		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_REG_RESTART, 0x02);
		dev_dbg(&client->dev, "set vblank 0x%x\n", ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		break;
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			ov02b10->flip |= MIRROR_BIT_MASK;
		else
			ov02b10->flip &= ~MIRROR_BIT_MASK;

		ret = ov02b10_write_reg(ov02b10->client,
					OV02B10_REG_PAGE_SELECT, 0x01);
		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_FLIP_REG, ov02b10->flip);
		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_REG_RESTART, 0x02);
		dev_dbg(&client->dev, "set hflip 0x%x\n", ov02b10->flip);
		break;
	case V4L2_CID_VFLIP:
		if (ctrl->val)
			ov02b10->flip |= FLIP_BIT_MASK;
		else
			ov02b10->flip &= ~FLIP_BIT_MASK;

		ret = ov02b10_write_reg(ov02b10->client,
					OV02B10_REG_PAGE_SELECT, 0x01);
		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_FLIP_REG, ov02b10->flip);
		ret |= ov02b10_write_reg(ov02b10->client,
					 OV02B10_REG_RESTART, 0x02);
		dev_dbg(&client->dev, "set vflip 0x%x\n", ov02b10->flip);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}
	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov02b10_ctrl_ops = {
	.s_ctrl = ov02b10_set_ctrl,
};

static int ov02b10_initialize_controls(struct ov02b10 *ov02b10)
{
	const struct ov02b10_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	u64 dst_link_freq = 0;
	u64 dst_pixel_rate = 0;

	handler = &ov02b10->ctrl_handler;
	mode = ov02b10->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &ov02b10->mutex;

	ov02b10->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
			V4L2_CID_LINK_FREQ,
			1, 0, link_freq_menu_items);

	if (ov02b10->cur_mode->bus_fmt == MEDIA_BUS_FMT_SBGGR10_1X10) {
		dst_link_freq = 0;
		dst_pixel_rate = PIXEL_RATE_WITH_360M;
	}
	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	ov02b10->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
			V4L2_CID_PIXEL_RATE,
			0, PIXEL_RATE_WITH_360M,
			1, dst_pixel_rate);

	__v4l2_ctrl_s_ctrl(ov02b10->link_freq,
			   dst_link_freq);

	h_blank = mode->hts_def - mode->width;
	ov02b10->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov02b10->hblank)
		ov02b10->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	/* From spec: vstart is 0xc by default */
	vblank_def = mode->vts_def - mode->height - 0xc;
	ov02b10->vblank = v4l2_ctrl_new_std(handler, &ov02b10_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OV02B10_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 7;
	ov02b10->exposure = v4l2_ctrl_new_std(handler, &ov02b10_ctrl_ops,
				V4L2_CID_EXPOSURE, OV02B10_EXPOSURE_MIN,
				exposure_max, OV02B10_EXPOSURE_STEP,
				mode->exp_def);

	ov02b10->anal_gain = v4l2_ctrl_new_std(handler, &ov02b10_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, OV02B10_GAIN_MIN,
				OV02B10_GAIN_MAX, OV02B10_GAIN_STEP,
				OV02B10_GAIN_DEFAULT);

	ov02b10->h_flip = v4l2_ctrl_new_std(handler, &ov02b10_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	ov02b10->v_flip = v4l2_ctrl_new_std(handler, &ov02b10_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	ov02b10->flip = 0;
	if (handler->error) {
		ret = handler->error;
		dev_err(&ov02b10->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ov02b10->subdev.ctrl_handler = handler;
	ov02b10->has_init_exp = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov02b10_check_sensor_id(struct ov02b10 *ov02b10,
				  struct i2c_client *client)
{
	struct device *dev = &ov02b10->client->dev;
	u8 id_h = 0, id_l = 0, id = 0;
	int ret;

	ret = ov02b10_read_reg(client, OV02B10_REG_CHIP_ID_H, &id_h);
	ret |= ov02b10_read_reg(client, OV02B10_REG_CHIP_ID_L, &id_l);

	id = SENSOR_ID(id_h, id_l);
	if (id != OV02B10_CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	dev_info(dev, "Detected OV%06x sensor\n", OV02B10_CHIP_ID);
	return 0;
}

static int ov02b10_configure_regulators(struct ov02b10 *ov02b10)
{
	unsigned int i;

	for (i = 0; i < OV02B10_NUM_SUPPLIES; i++)
		ov02b10->supplies[i].supply = OV02B10_supply_names[i];

	return devm_regulator_bulk_get(&ov02b10->client->dev,
				       OV02B10_NUM_SUPPLIES,
				       ov02b10->supplies);
}

static int ov02b10_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct ov02b10 *ov02b10;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	ov02b10 = devm_kzalloc(dev, sizeof(*ov02b10), GFP_KERNEL);
	if (!ov02b10)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &ov02b10->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &ov02b10->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &ov02b10->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &ov02b10->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE,
			&hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}
	ov02b10->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < ov02b10->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			ov02b10->cur_mode = &supported_modes[i];
			break;
		}
	}
	ov02b10->client = client;

	ov02b10->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov02b10->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	ov02b10->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(ov02b10->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	ov02b10->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_ASIS);
	if (IS_ERR(ov02b10->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ov02b10->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(ov02b10->pinctrl)) {
		ov02b10->pins_default =
			pinctrl_lookup_state(ov02b10->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(ov02b10->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		ov02b10->pins_sleep =
			pinctrl_lookup_state(ov02b10->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(ov02b10->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = ov02b10_configure_regulators(ov02b10);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&ov02b10->mutex);

	sd = &ov02b10->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov02b10_subdev_ops);
	ret = ov02b10_initialize_controls(ov02b10);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov02b10_power_on(ov02b10);
	if (ret)
		goto err_free_handler;

	ret = ov02b10_check_sensor_id(ov02b10, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov02b10_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov02b10->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &ov02b10->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(ov02b10->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 ov02b10->module_index, facing,
		 OV02B10_NAME, dev_name(sd->dev));
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
	__ov02b10_power_off(ov02b10);
err_free_handler:
	v4l2_ctrl_handler_free(&ov02b10->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov02b10->mutex);

	return ret;
}

static int ov02b10_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov02b10 *ov02b10 = to_ov02b10(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov02b10->ctrl_handler);
	mutex_destroy(&ov02b10->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov02b10_power_off(ov02b10);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov02b10_of_match[] = {
	{ .compatible = "ovti,ov02b10" },
	{},
};
MODULE_DEVICE_TABLE(of, ov02b10_of_match);
#endif

static const struct i2c_device_id ov02b10_match_id[] = {
	{ "ovti,ov02b10", 0 },
	{ },
};

static struct i2c_driver ov02b10_i2c_driver = {
	.driver = {
		.name = OV02B10_NAME,
		.pm = &ov02b10_pm_ops,
		.of_match_table = of_match_ptr(ov02b10_of_match),
	},
	.probe		= &ov02b10_probe,
	.remove		= &ov02b10_remove,
	.id_table	= ov02b10_match_id,
};

#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT
module_i2c_driver(ov02b10_i2c_driver);
#else
static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov02b10_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov02b10_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);
#endif

MODULE_DESCRIPTION("OmniVision ov02b10 sensor driver");
MODULE_LICENSE("GPL v2");
