// SPDX-License-Identifier: GPL-2.0
/*
 * ov2680 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 add enum_frame_interval function.
 * V0.0X01.0X04 add quick stream on/off
 * V0.0X01.0X05 add function g_mbus_config
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/pinctrl/consumer.h>
#include <linux/version.h>
#include <media/v4l2-async.h>
#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#include <linux/rk-camera-module.h>

/* verify default register values */
//#define CHECK_REG_VALUE

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x5)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define MIPI_FREQ	330000000U
#define OV2680_PIXEL_RATE		(330000000LL * 1LL * 2LL / 10)
#define OV2680_XVCLK_FREQ		24000000

#define CHIP_ID				0x2680
#define OV2680_REG_CHIP_ID		0x300a

#define OV2680_REG_CTRL_MODE		0x0100
#define OV2680_MODE_SW_STANDBY		0x00
#define OV2680_MODE_STREAMING		0x01

#define OV2680_REG_EXPOSURE		0x3500
#define	OV2680_EXPOSURE_MIN		4
#define	OV2680_EXPOSURE_STEP		1
#define OV2680_VTS_MAX			0x7fff

#define OV2680_REG_ANALOG_GAIN		0x350a
#define	ANALOG_GAIN_MIN			0x10
#define	ANALOG_GAIN_MAX			0xf8
#define	ANALOG_GAIN_STEP		1
#define	ANALOG_GAIN_DEFAULT		0xf8
#define OV2680_REG_GAIN_H		0x350a
#define OV2680_REG_GAIN_L		0x350b
#define OV2680_GAIN_L_MASK		0xff
#define OV2680_GAIN_H_MASK		0x07
#define OV2680_DIGI_GAIN_H_SHIFT	8

#define OV2680_DIGI_GAIN_MIN		0
#define OV2680_DIGI_GAIN_MAX		(0x4000 - 1)
#define OV2680_DIGI_GAIN_STEP		1
#define OV2680_DIGI_GAIN_DEFAULT	1024

#define OV2680_REG_TEST_PATTERN		0x5080
#define	OV2680_TEST_PATTERN_ENABLE	0x80
#define	OV2680_TEST_PATTERN_DISABLE	0x0

#define OV2680_REG_VTS			0x380e

#define REG_NULL			0xFFFF

#define OV2680_REG_VALUE_08BIT		1
#define OV2680_REG_VALUE_16BIT		2
#define OV2680_REG_VALUE_24BIT		3

#define OV2680_LANES			1
#define OV2680_BITS_PER_SAMPLE		10

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define OV2680_NAME			"ov2680"

static const char * const ov2680_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV2680_NUM_SUPPLIES ARRAY_SIZE(ov2680_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct ov2680_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct ov2680 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV2680_NUM_SUPPLIES];

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
	const struct ov2680_mode *cur_mode;
	unsigned int lane_num;
	unsigned int cfg_num;
	unsigned int pixel_rate;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_ov2680(sd) container_of(sd, struct ov2680, subdev)

/*
 * Xclk 24Mhz
 * Pclk 66Mhz
 * linelength 1700(0x6a4)
 * framelength 1294(0x50e)
 * grabwindow_width 1600
 * grabwindow_height 1200
 * max_framerate 30fps
 * mipi_datarate per lane 660Mbps
 */
static const struct regval ov2680_global_regs[] = {
	{0x0100, 0x00},
	{0x0103, 0x01},
	{0x3002, 0x00},
	{0x3016, 0x1c},
	{0x3018, 0x44},
	{0x3020, 0x00},
	{0x3080, 0x02},
	{0x3082, 0x37},
	{0x3084, 0x09},
	{0x3085, 0x04},
	{0x3086, 0x01},
	{0x3501, 0x26},
	{0x3502, 0x40},
	{0x3503, 0x03},
	{0x350b, 0x36},
	{0x3600, 0xb4},
	{0x3603, 0x35},
	{0x3604, 0x24},
	{0x3605, 0x00},
	{0x3620, 0x26},
	{0x3621, 0x37},
	{0x3622, 0x04},
	{0x3628, 0x00},
	{0x3705, 0x3c},
	{0x370c, 0x50},
	{0x370d, 0xc0},
	{0x3718, 0x88},
	{0x3720, 0x00},
	{0x3721, 0x00},
	{0x3722, 0x00},
	{0x3723, 0x00},
	{0x3738, 0x00},
	{0x370a, 0x23},
	{0x3717, 0x58},
	{0x3781, 0x80},
	{0x3784, 0x0c},
	{0x3789, 0x60},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x06},
	{0x3805, 0x4f},
	{0x3806, 0x04},
	{0x3807, 0xbf},
	{0x3808, 0x03},
	{0x3809, 0x20},
	{0x380a, 0x02},
	{0x380b, 0x58},
	{0x380c, 0x06},
	{0x380d, 0xac},
	{0x380e, 0x02},
	{0x380f, 0x84},
	{0x3810, 0x00},
	{0x3811, 0x04},
	{0x3812, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3819, 0x04},
	{0x3820, 0xc2},
	{0x3821, 0x01},
	{0x4000, 0x81},
	{0x4001, 0x40},
	{0x4008, 0x00},
	{0x4009, 0x03},
	{0x4602, 0x02},
	{0x481f, 0x36},
	{0x4825, 0x36},
	{0x4837, 0x30},
	{0x5002, 0x30},
	{0x5080, 0x00},
	{0x5081, 0x41},
	{REG_NULL, 0x00},
	//{0x0100, 0x01},

	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * Pclk 66Mhz
 * linelength 1700(0x6a4)
 * framelength 1294(0x50e)
 * grabwindow_width 1600
 * grabwindow_height 1200
 * max_framerate 30fps
 * mipi_datarate per lane 660Mbps
 */
static const struct regval ov2680_1600x1200_regs[] = {
	// 1600x1200 30fps 1 lane MIPI 660Mbps/lane
	{0x3086, 0x00},
	{0x3620, 0x26},
	{0x3621, 0x37},
	{0x3622, 0x04},
	{0x370a, 0x21},
	{0x370d, 0xc0},
	{0x3718, 0x88},
	{0x3721, 0x00},
	{0x3722, 0x00},
	{0x3723, 0x00},
	{0x3738, 0x00},
	{0x3803, 0x00},
	{0x3807, 0xbf},
	{0x3808, 0x06},
	{0x3809, 0x40},
	{0x380a, 0x04},
	{0x380b, 0xb0},
	{0x380c, 0x06},
	{0x380d, 0xa4},
	{0x380e, 0x05},
	{0x380f, 0x0e},
	{0x3811, 0x08},
	{0x3813, 0x08},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3820, 0xc0},
	{0x3821, 0x00},
	{0x4008, 0x02},
	{0x4009, 0x09},
	{0x4837, 0x18},
	//{0x0100, 0x01},

	{REG_NULL, 0x00},
};

static const struct ov2680_mode supported_modes_1lane[] = {
	{
		.width = 1600,
		.height = 1200,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0500,
		.hts_def = 0x06a4,
		.vts_def = 0x050e,
		.reg_list = ov2680_1600x1200_regs,
	},
};

static const struct ov2680_mode *supported_modes;

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ
};

static const char * const ov2680_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int ov2680_write_reg(struct i2c_client *client, u16 reg,
			    u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	dev_dbg(&client->dev, "write reg(0x%x val:0x%06x)!\n", reg, val);

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

	if (i2c_master_send(client, buf, len + 2) != len + 2) {
		dev_err(&client->dev,
			   "write reg(0x%x val:0x%x)failed !\n", reg, val);
		return -EIO;
	}
	return 0;
}

static int ov2680_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = ov2680_write_reg(client, regs[i].addr,
				       OV2680_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int ov2680_read_reg(struct i2c_client *client, u16 reg,
					unsigned int len, u32 *val)
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

/* Check Register value */
#ifdef CHECK_REG_VALUE
static int ov2680_reg_verify(struct i2c_client *client,
				const struct regval *regs)
{
	u32 i;
	int ret = 0;
	u32 value;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = ov2680_read_reg(client, regs[i].addr,
			  OV2680_REG_VALUE_08BIT, &value);
		if (value != regs[i].val) {
			dev_info(&client->dev, "%s:0x%04x is 0x%08x instead of 0x%08x\n",
				 __func__, regs[i].addr, value, regs[i].val);
		}
	}
	return ret;
}
#endif

static int ov2680_get_reso_dist(const struct ov2680_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov2680_mode *
ov2680_find_best_fit(struct ov2680 *ov2680,
			struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ov2680->cfg_num; i++) {
		dist = ov2680_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov2680_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov2680 *ov2680 = to_ov2680(sd);
	const struct ov2680_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&ov2680->mutex);

	mode = ov2680_find_best_fit(ov2680, fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov2680->mutex);
		return -ENOTTY;
#endif
	} else {
		ov2680->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov2680->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov2680->vblank, vblank_def,
					 OV2680_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&ov2680->mutex);

	return 0;
}

static int ov2680_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov2680 *ov2680 = to_ov2680(sd);
	const struct ov2680_mode *mode = ov2680->cur_mode;

	mutex_lock(&ov2680->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov2680->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&ov2680->mutex);

	return 0;
}

static int ov2680_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov2680_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct ov2680 *ov2680 = to_ov2680(sd);

	if (fse->index >= ov2680->cfg_num)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int ov2680_enable_test_pattern(struct ov2680 *ov2680, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | OV2680_TEST_PATTERN_ENABLE;
	else
		val = OV2680_TEST_PATTERN_DISABLE;

	return ov2680_write_reg(ov2680->client, OV2680_REG_TEST_PATTERN,
				OV2680_REG_VALUE_08BIT, val);
}

static int ov2680_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct ov2680 *ov2680 = to_ov2680(sd);
	const struct ov2680_mode *mode = ov2680->cur_mode;

	mutex_lock(&ov2680->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&ov2680->mutex);

	return 0;
}

static void ov2680_get_module_inf(struct ov2680 *ov2680,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, OV2680_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, ov2680->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, ov2680->len_name, sizeof(inf->base.lens));
}

static long ov2680_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ov2680 *ov2680 = to_ov2680(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		ov2680_get_module_inf(ov2680, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = ov2680_write_reg(ov2680->client, OV2680_REG_CTRL_MODE,
				OV2680_REG_VALUE_08BIT, OV2680_MODE_STREAMING);
		else
			ret = ov2680_write_reg(ov2680->client, OV2680_REG_CTRL_MODE,
				OV2680_REG_VALUE_08BIT, OV2680_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ov2680_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = ov2680_ioctl(sd, cmd, inf);
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
			ret = ov2680_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = ov2680_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __ov2680_start_stream(struct ov2680 *ov2680)
{
	int ret;

	ret = ov2680_write_array(ov2680->client, ov2680->cur_mode->reg_list);
	if (ret)
		return ret;

#ifdef CHECK_REG_VALUE
	usleep_range(10000, 20000);
	/*  verify default values to make sure everything has */
	/*  been written correctly as expected */
	dev_info(&ov2680->client->dev, "%s:Check register value!\n",
				__func__);
	ret = ov2680_reg_verify(ov2680->client, ov2680_global_regs);
	if (ret)
		return ret;

	ret = ov2680_reg_verify(ov2680->client, ov2680->cur_mode->reg_list);
	if (ret)
		return ret;
#endif

	/* In case these controls are set before streaming */
	mutex_unlock(&ov2680->mutex);
	ret = v4l2_ctrl_handler_setup(&ov2680->ctrl_handler);
	mutex_lock(&ov2680->mutex);
	if (ret)
		return ret;
	ret = ov2680_write_reg(ov2680->client, OV2680_REG_CTRL_MODE,
				OV2680_REG_VALUE_08BIT, OV2680_MODE_STREAMING);
	return ret;
}

static int __ov2680_stop_stream(struct ov2680 *ov2680)
{
	return ov2680_write_reg(ov2680->client, OV2680_REG_CTRL_MODE,
				OV2680_REG_VALUE_08BIT, OV2680_MODE_SW_STANDBY);
}

static int ov2680_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov2680 *ov2680 = to_ov2680(sd);
	struct i2c_client *client = ov2680->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				ov2680->cur_mode->width,
				ov2680->cur_mode->height,
		DIV_ROUND_CLOSEST(ov2680->cur_mode->max_fps.denominator,
		ov2680->cur_mode->max_fps.numerator));

	mutex_lock(&ov2680->mutex);
	on = !!on;
	if (on == ov2680->streaming)
		goto unlock_and_return;

	if (on) {
		dev_info(&client->dev, "stream on!!!\n");
		ret = __ov2680_start_stream(ov2680);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		dev_info(&client->dev, "stream off!!!\n");
		__ov2680_stop_stream(ov2680);
	}

	ov2680->streaming = on;

unlock_and_return:
	mutex_unlock(&ov2680->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov2680_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OV2680_XVCLK_FREQ / 1000 / 1000);
}

static int __ov2680_power_on(struct ov2680 *ov2680)
{
	int ret;
	u32 delay_us;
	struct device *dev = &ov2680->client->dev;

	if (!IS_ERR(ov2680->power_gpio))
		gpiod_set_value_cansleep(ov2680->power_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR_OR_NULL(ov2680->pins_default)) {
		ret = pinctrl_select_state(ov2680->pinctrl,
					   ov2680->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(ov2680->xvclk, OV2680_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(ov2680->xvclk) != OV2680_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(ov2680->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(ov2680->reset_gpio))
		gpiod_set_value_cansleep(ov2680->reset_gpio, 1);

	ret = regulator_bulk_enable(OV2680_NUM_SUPPLIES, ov2680->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(ov2680->reset_gpio))
		gpiod_set_value_cansleep(ov2680->reset_gpio, 0);

	if (!IS_ERR(ov2680->pwdn_gpio))
		gpiod_set_value_cansleep(ov2680->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ov2680_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(ov2680->xvclk);

	return ret;
}

static void __ov2680_power_off(struct ov2680 *ov2680)
{
	int ret;
	struct device *dev = &ov2680->client->dev;

	if (!IS_ERR(ov2680->pwdn_gpio))
		gpiod_set_value_cansleep(ov2680->pwdn_gpio, 0);
	clk_disable_unprepare(ov2680->xvclk);
	if (!IS_ERR(ov2680->reset_gpio))
		gpiod_set_value_cansleep(ov2680->reset_gpio, 1);
	if (!IS_ERR_OR_NULL(ov2680->pins_sleep)) {
		ret = pinctrl_select_state(ov2680->pinctrl,
					   ov2680->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	if (!IS_ERR(ov2680->power_gpio))
		gpiod_set_value_cansleep(ov2680->power_gpio, 0);

	regulator_bulk_disable(OV2680_NUM_SUPPLIES, ov2680->supplies);
}

static int ov2680_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2680 *ov2680 = to_ov2680(sd);

	return __ov2680_power_on(ov2680);
}

static int ov2680_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2680 *ov2680 = to_ov2680(sd);

	__ov2680_power_off(ov2680);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov2680_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov2680 *ov2680 = to_ov2680(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov2680_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov2680->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov2680->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int ov2680_init(struct v4l2_subdev *sd)
{
	int ret;
	struct ov2680 *ov2680 = to_ov2680(sd);
	struct i2c_client *client = ov2680->client;

	dev_info(&client->dev, "%s(%d)\n", __func__, __LINE__);
	ret = ov2680_write_array(ov2680->client, ov2680_global_regs);
	return ret;
}

static int ov2680_power(struct v4l2_subdev *sd, int on)
{
	int ret;
	struct ov2680 *ov2680 = to_ov2680(sd);
	struct i2c_client *client = ov2680->client;
	struct device *dev = &ov2680->client->dev;

	dev_info(&client->dev, "%s(%d) on(%d)\n", __func__, __LINE__, on);
	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			return ret;
		}
		ret = ov2680_init(sd);
		usleep_range(5000, 10000);
		if (ret)
			dev_err(dev, "init error\n");
	} else {
		pm_runtime_put(&client->dev);
	}
	return 0;
}

static int ov2680_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_frame_interval_enum *fie)
{
	struct ov2680 *ov2680 = to_ov2680(sd);

	if (fie->index >= ov2680->cfg_num)
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int ov2680_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	u32 val = 0;

	val = 1 << (OV2680_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops ov2680_pm_ops = {
	SET_RUNTIME_PM_OPS(ov2680_runtime_suspend,
			   ov2680_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov2680_internal_ops = {
	.open = ov2680_open,
};
#endif

static const struct v4l2_subdev_core_ops ov2680_core_ops = {
	.ioctl = ov2680_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = ov2680_compat_ioctl32,
#endif
	.s_power = ov2680_power,
};

static const struct v4l2_subdev_video_ops ov2680_video_ops = {
	.s_stream = ov2680_s_stream,
	.g_frame_interval = ov2680_g_frame_interval,
	.g_mbus_config = ov2680_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops ov2680_pad_ops = {
	.enum_mbus_code = ov2680_enum_mbus_code,
	.enum_frame_size = ov2680_enum_frame_sizes,
	.enum_frame_interval = ov2680_enum_frame_interval,
	.get_fmt = ov2680_get_fmt,
	.set_fmt = ov2680_set_fmt,
};

static const struct v4l2_subdev_ops ov2680_subdev_ops = {
	.core	= &ov2680_core_ops,
	.video	= &ov2680_video_ops,
	.pad	= &ov2680_pad_ops,
};

static int ov2680_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov2680 *ov2680 = container_of(ctrl->handler,
					     struct ov2680, ctrl_handler);
	struct i2c_client *client = ov2680->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov2680->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(ov2680->exposure,
					 ov2680->exposure->minimum, max,
					 ov2680->exposure->step,
					 ov2680->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */

		ret = ov2680_write_reg(ov2680->client, OV2680_REG_EXPOSURE,
				       OV2680_REG_VALUE_24BIT, ctrl->val << 4);

		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov2680_write_reg(ov2680->client, OV2680_REG_GAIN_L,
				       OV2680_REG_VALUE_08BIT,
				       ctrl->val & OV2680_GAIN_L_MASK);
		ret |= ov2680_write_reg(ov2680->client, OV2680_REG_GAIN_H,
				       OV2680_REG_VALUE_08BIT,
				       (ctrl->val >> OV2680_DIGI_GAIN_H_SHIFT) &
				       OV2680_GAIN_H_MASK);
		break;
	case V4L2_CID_VBLANK:

		ret = ov2680_write_reg(ov2680->client, OV2680_REG_VTS,
				       OV2680_REG_VALUE_16BIT,
				       ctrl->val + ov2680->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov2680_enable_test_pattern(ov2680, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov2680_ctrl_ops = {
	.s_ctrl = ov2680_set_ctrl,
};

static int ov2680_initialize_controls(struct ov2680 *ov2680)
{
	const struct ov2680_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &ov2680->ctrl_handler;
	mode = ov2680->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &ov2680->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, ov2680->pixel_rate, 1, ov2680->pixel_rate);

	h_blank = mode->hts_def - mode->width;
	ov2680->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov2680->hblank)
		ov2680->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ov2680->vblank = v4l2_ctrl_new_std(handler, &ov2680_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OV2680_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	ov2680->exposure = v4l2_ctrl_new_std(handler, &ov2680_ctrl_ops,
				V4L2_CID_EXPOSURE, OV2680_EXPOSURE_MIN,
				exposure_max, OV2680_EXPOSURE_STEP,
				mode->exp_def);

	ov2680->anal_gain = v4l2_ctrl_new_std(handler, &ov2680_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	/* Digital gain */
	ov2680->digi_gain = v4l2_ctrl_new_std(handler, &ov2680_ctrl_ops,
				V4L2_CID_DIGITAL_GAIN, OV2680_DIGI_GAIN_MIN,
				OV2680_DIGI_GAIN_MAX, OV2680_DIGI_GAIN_STEP,
				OV2680_DIGI_GAIN_DEFAULT);

	ov2680->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ov2680_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov2680_test_pattern_menu) - 1,
				0, 0, ov2680_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&ov2680->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ov2680->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov2680_check_sensor_id(struct ov2680 *ov2680,
				  struct i2c_client *client)
{
	struct device *dev = &ov2680->client->dev;
	u32 id = 0;
	int ret;

	ret = ov2680_read_reg(client, OV2680_REG_CHIP_ID,
			      OV2680_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int ov2680_configure_regulators(struct ov2680 *ov2680)
{
	unsigned int i;

	for (i = 0; i < OV2680_NUM_SUPPLIES; i++)
		ov2680->supplies[i].supply = ov2680_supply_names[i];

	return devm_regulator_bulk_get(&ov2680->client->dev,
				       OV2680_NUM_SUPPLIES,
				       ov2680->supplies);
}

static int ov2680_parse_of(struct ov2680 *ov2680)
{
	struct device *dev = &ov2680->client->dev;
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
	if (rval <= 0) {
		dev_warn(dev, " Get mipi lane num failed!\n");
		return -1;
	}

	ov2680->lane_num = rval;
	if (1 == ov2680->lane_num) {
		ov2680->cur_mode = &supported_modes_1lane[0];
		supported_modes = supported_modes_1lane;
		ov2680->cfg_num = ARRAY_SIZE(supported_modes_1lane);

		/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
		ov2680->pixel_rate = MIPI_FREQ * 2U * ov2680->lane_num / 10U;
		dev_info(dev, "lane_num(%d)  pixel_rate(%u)\n",
				 ov2680->lane_num, ov2680->pixel_rate);
	} else {
		dev_err(dev, "unsupported lane_num(%d)\n", ov2680->lane_num);
		return -1;
	}
	return 0;
}

static int ov2680_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct ov2680 *ov2680;
	struct v4l2_subdev *sd;
	char facing[2] = "b";
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	ov2680 = devm_kzalloc(dev, sizeof(*ov2680), GFP_KERNEL);
	if (!ov2680)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &ov2680->module_index);
	if (ret) {
		dev_warn(dev, "could not get module index!\n");
		ov2680->module_index = 0;
	}

	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &ov2680->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &ov2680->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &ov2680->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ov2680->client = client;

	ov2680->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov2680->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	ov2680->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(ov2680->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");

	ov2680->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov2680->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios, maybe no use\n");

	ov2680->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov2680->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = ov2680_configure_regulators(ov2680);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}
	ret = ov2680_parse_of(ov2680);
	if (ret != 0)
		return -EINVAL;

	ov2680->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(ov2680->pinctrl)) {
		ov2680->pins_default =
			pinctrl_lookup_state(ov2680->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(ov2680->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		ov2680->pins_sleep =
			pinctrl_lookup_state(ov2680->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(ov2680->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&ov2680->mutex);

	sd = &ov2680->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov2680_subdev_ops);
	ret = ov2680_initialize_controls(ov2680);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov2680_power_on(ov2680);
	if (ret)
		goto err_free_handler;

	ret = ov2680_check_sensor_id(ov2680, client);
	if (ret < 0) {
		dev_info(&client->dev, "%s(%d) Check id  failed\n"
				  "check following information:\n"
				  "Power/PowerDown/Reset/Mclk/I2cBus !!\n",
				  __func__, __LINE__);
		goto err_power_off;
	}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov2680_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov2680->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &ov2680->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(ov2680->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 ov2680->module_index, facing,
		 OV2680_NAME, dev_name(sd->dev));

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
	__ov2680_power_off(ov2680);
err_free_handler:
	v4l2_ctrl_handler_free(&ov2680->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov2680->mutex);

	return ret;
}

static int ov2680_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2680 *ov2680 = to_ov2680(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov2680->ctrl_handler);
	mutex_destroy(&ov2680->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov2680_power_off(ov2680);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov2680_of_match[] = {
	{ .compatible = "ovti,ov2680" },
	{},
};
MODULE_DEVICE_TABLE(of, ov2680_of_match);
#endif

static const struct i2c_device_id ov2680_match_id[] = {
	{ "ovti,ov2680", 0 },
	{ },
};

static struct i2c_driver ov2680_i2c_driver = {
	.driver = {
		.name = OV2680_NAME,
		.pm = &ov2680_pm_ops,
		.of_match_table = of_match_ptr(ov2680_of_match),
	},
	.probe		= &ov2680_probe,
	.remove		= &ov2680_remove,
	.id_table	= ov2680_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov2680_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov2680_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision ov2680 sensor driver");
MODULE_LICENSE("GPL v2");
