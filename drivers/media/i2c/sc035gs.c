// SPDX-License-Identifier: GPL-2.0
/*
 * sc035gs driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 * V0.1.0: MIPI is ok.
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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)
#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIPI_FREQ_180M			180000000
#define MIPI_FREQ_300M			300000000

#define PIXEL_RATE_WITH_180M		(MIPI_FREQ_180M * 2 / 10 * 2)
#define PIXEL_RATE_WITH_300M		(MIPI_FREQ_300M * 2 / 8 * 1)

#define SC035GS_XVCLK_FREQ		24000000

#define CHIP_ID				0x0108
#define SC132GS_REG_CHIP_ID		0x300A

#define SC035GS_REG_CTRL_MODE		0x0100
#define SC035GS_MODE_SW_STANDBY		0x0
#define SC035GS_MODE_STREAMING		BIT(0)

#define SC035GS_REG_EXPOSURE		0x3e01
#define	SC035GS_EXPOSURE_MIN		6
#define	SC035GS_EXPOSURE_STEP		1
#define SC035GS_VTS_MAX			0xffff

#define SC035GS_REG_COARSE_AGAIN	0x3e08
#define SC035GS_REG_FINE_AGAIN		0x3e09
#define	ANALOG_GAIN_MIN			0x01
#define	ANALOG_GAIN_MAX			0xF8
#define	ANALOG_GAIN_STEP		1
#define	ANALOG_GAIN_DEFAULT		0x1f

#define SC035GS_REG_TEST_PATTERN	0x4501
#define	SC035GS_TEST_PATTERN_ENABLE	0xcc
#define	SC035GS_TEST_PATTERN_DISABLE	0xc4

#define SC035GS_REG_VTS			0x320e

#define REG_NULL			0xFFFF

#define SC035GS_REG_VALUE_08BIT		1
#define SC035GS_REG_VALUE_16BIT		2
#define SC035GS_REG_VALUE_24BIT		3

#define SC035GS_NAME			"sc035gs"

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

static const char *const sc035gs_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define SC035GS_NUM_SUPPLIES ARRAY_SIZE(sc035gs_supply_names)

enum {
	LINK_FREQ_180M_INDEX,
	LINK_FREQ_300M_INDEX,
};

struct regval {
	u16 addr;
	u8 val;
};

struct sc035gs_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 link_freq_index;
	u64 pixel_rate;
	const struct regval *reg_list;
	u32 lanes;
	u32 bus_fmt;
};

struct sc035gs {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[SC035GS_NUM_SUPPLIES];
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
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct mutex		mutex;
	struct v4l2_fract	cur_fps;
	u32			cur_vts;
	bool			streaming;
	bool			power_on;
	const struct sc035gs_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_sc035gs(sd) container_of(sd, struct sc035gs, subdev)

/*
 * Xclk 24Mhz
 * Pclk 72Mhz
 * linelength 1600(0x06a0)
 * framelength 1250(0x04e2)
 * grabwindow_width 640
 * grabwindow_height 480
 * mipi 2 lane
 * max_framerate 30fps
 * mipi_datarate per lane 360Mbps
 */
static const struct regval sc035gs_2lane_10bit_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x300f, 0x0f},
	{0x3018, 0x33},
	{0x3019, 0xfc},
	{0x301c, 0x78},
	{0x301f, 0x9c},
	{0x3031, 0x0a},
	{0x3037, 0x20},
	{0x303f, 0x01},
	{0x320c, 0x06},
	{0x320d, 0x40},
	{0x320e, 0x04},
	{0x320f, 0xe2},
	{0x3217, 0x00},
	{0x3218, 0x00},
	{0x3220, 0x10},
	{0x3223, 0x48},
	{0x3226, 0x74},
	{0x3227, 0x07},
	{0x323b, 0x00},
	{0x3250, 0xf0},
	{0x3251, 0x02},
	{0x3252, 0x02},
	{0x3253, 0x08},
	{0x3254, 0x02},
	{0x3255, 0x07},
	{0x3304, 0x48},
	{0x3305, 0x00},
	{0x3306, 0x98},
	{0x3309, 0x50},
	{0x330a, 0x01},
	{0x330b, 0x18},
	{0x330c, 0x18},
	{0x330f, 0x40},
	{0x3310, 0x10},
	{0x3314, 0x68},
	{0x3315, 0x30},
	{0x3316, 0x68},
	{0x3317, 0x14},
	{0x3329, 0x5c},
	{0x332d, 0x5c},
	{0x332f, 0x60},
	{0x3335, 0x64},
	{0x3344, 0x64},
	{0x335b, 0x80},
	{0x335f, 0x80},
	{0x3366, 0x06},
	{0x3385, 0x41},
	{0x3387, 0x49},
	{0x3389, 0x01},
	{0x33b1, 0x03},
	{0x33b2, 0x06},
	{0x33bd, 0xe0},
	{0x33bf, 0x10},
	{0x3621, 0xa4},
	{0x3622, 0x05},
	{0x3624, 0x47},
	{0x3630, 0x4a},
	{0x3631, 0x58},
	{0x3633, 0x52},
	{0x3635, 0x03},
	{0x3636, 0x25},
	{0x3637, 0x8a},
	{0x3638, 0x0f},
	{0x3639, 0x08},
	{0x363a, 0x00},
	{0x363b, 0x48},
	{0x363c, 0x86},
	{0x363e, 0xf8},
	{0x3640, 0x00},
	{0x3641, 0x01},
	{0x36ea, 0x3b},
	{0x36eb, 0x0e},
	{0x36ec, 0x1e},
	{0x36ed, 0x20},
	{0x36fa, 0x3b},
	{0x36fb, 0x10},
	{0x36fc, 0x02},
	{0x36fd, 0x00},
	{0x3908, 0x91},
	{0x391b, 0x81},
	{0x3d08, 0x01},
	{0x3e01, 0x18},
	{0x3e02, 0xf0},

	{0x3f04, 0x06},
	{0x3f05, 0x20},
	{0x4500, 0x59},
	{0x4501, 0xc4},
	{0x4603, 0x00},
	{0x4800, 0x64},
	{0x4809, 0x01},
	{0x4810, 0x00},
	{0x4811, 0x01},
	{0x4837, 0x42},
	{0x5011, 0x00},
	{0x5988, 0x02},
	{0x598e, 0x06},
	{0x598f, 0x08},
	{0x36e9, 0x24},
	{0x36f9, 0x24},

	//again adjust
	{0x4418, 0x0a},
	{0x363d, 0x10},
	{0x4419, 0x80},

	//mirror & flip
	{0x3221, (0x03 << 1)},

	//exposure 5ms
	{0x3e01, 0x13},
	{0x3e02, 0xc0},

	//dgain 1
	{0x3e06, 0x0c},
	{0x3e07, 0x80},

	//gain < 2
	{0x3631, 0x58},
	{0x3630, 0x4a},

	//again 1
	{0x3e08, 0x03},
	{0x3e09, 0x10},

	{REG_NULL, 0x00},
};

static const struct sc035gs_mode supported_modes[] = {
	{
		.width = 640,
		.height = 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0bb,
		.hts_def = 0x640,
		.vts_def = 0x4e2,
		.link_freq_index = LINK_FREQ_300M_INDEX,
		.pixel_rate      = PIXEL_RATE_WITH_300M,
		.reg_list = sc035gs_2lane_10bit_regs,
		.lanes    = 2,
		.bus_fmt  = MEDIA_BUS_FMT_Y10_1X10,
	},
};

static const char *const sc035gs_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ_180M,
	MIPI_FREQ_300M,
};

/* Write registers up to 4 at a time */
static int sc035gs_write_reg(struct i2c_client *client,
			     u16 reg, u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;
	u32 ret;

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

	ret = i2c_master_send(client, buf, len + 2);
	if (ret != len + 2)
		return -EIO;

	return 0;
}

static int sc035gs_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = sc035gs_write_reg(client, regs[i].addr,
					SC035GS_REG_VALUE_08BIT, regs[i].val);
	}

	return ret;
}

/* Read registers up to 4 at a time */
static int sc035gs_read_reg(struct i2c_client *client,
			    u16 reg, unsigned int len, u32 *val)
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

static int sc035gs_get_reso_dist(const struct sc035gs_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc035gs_mode *
sc035gs_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = sc035gs_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist < cur_best_fit_dist) &&
		    (supported_modes[i].bus_fmt == framefmt->code)) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}
	return &supported_modes[cur_best_fit];
}

static int sc035gs_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc035gs *sc035gs = to_sc035gs(sd);
	const struct sc035gs_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&sc035gs->mutex);

	mode = sc035gs_find_best_fit(fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc035gs->mutex);
		return -ENOTTY;
#endif
	} else {
		sc035gs->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc035gs->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc035gs->vblank, vblank_def,
					 SC035GS_VTS_MAX - mode->height,
					 1, vblank_def);
		__v4l2_ctrl_s_ctrl_int64(sc035gs->pixel_rate, mode->pixel_rate);
		__v4l2_ctrl_s_ctrl(sc035gs->link_freq, mode->link_freq_index);
		sc035gs->cur_vts = mode->vts_def;
		sc035gs->cur_fps = mode->max_fps;
	}

	mutex_unlock(&sc035gs->mutex);

	return 0;
}

static int sc035gs_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc035gs *sc035gs = to_sc035gs(sd);
	const struct sc035gs_mode *mode = sc035gs->cur_mode;

	mutex_lock(&sc035gs->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc035gs->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&sc035gs->mutex);

	return 0;
}

static int sc035gs_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct sc035gs *sc035gs = to_sc035gs(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = sc035gs->cur_mode->bus_fmt;

	return 0;
}

static int sc035gs_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int sc035gs_enable_test_pattern(struct sc035gs *sc035gs, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | SC035GS_TEST_PATTERN_ENABLE;
	else
		val = SC035GS_TEST_PATTERN_DISABLE;

	return sc035gs_write_reg(sc035gs->client, SC035GS_REG_TEST_PATTERN,
				 SC035GS_REG_VALUE_08BIT, val);
}

static void sc035gs_get_module_inf(struct sc035gs *sc035gs,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, SC035GS_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, sc035gs->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, sc035gs->len_name, sizeof(inf->base.lens));
}

static long sc035gs_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc035gs *sc035gs = to_sc035gs(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		sc035gs_get_module_inf(sc035gs, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = sc035gs_write_reg(sc035gs->client, SC035GS_REG_CTRL_MODE,
						SC035GS_REG_VALUE_08BIT, SC035GS_MODE_STREAMING);
		else
			ret = sc035gs_write_reg(sc035gs->client, SC035GS_REG_CTRL_MODE,
						SC035GS_REG_VALUE_08BIT, SC035GS_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc035gs_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = sc035gs_ioctl(sd, cmd, inf);
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

		ret = sc035gs_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int sc035gs_set_ctrl_gain(struct sc035gs *sc035gs, u32 a_gain)
{
	int ret = 0;
	u32 coarse_again, fine_again, fine_again_reg, coarse_again_reg;

	/* (1.0 - 15.5) * 0x10 (fix point) */
	if (a_gain < 0x10)
		a_gain = 0x10;
	if (a_gain > 0xf8)
		a_gain = 0xf8;

	if (a_gain < 0x20) { /*1x ~ 2x*/
		coarse_again = 0x3;
		fine_again = a_gain * 16 / 0x10;
	} else if (a_gain < 0x40) { /*2x ~ 4x*/
		coarse_again = 0x7;
		fine_again = a_gain * 8 / 0x10;
	} else if (a_gain < 0x80) { /*4x ~ 8x*/
		coarse_again = 0xf;
		fine_again = a_gain * 4 / 0x10;
	} else { /*8x ~ 16x*/
		coarse_again = 0x1f;
		fine_again = a_gain * 2 / 0x10;
	}

	fine_again_reg = fine_again & 0x1F;
	coarse_again_reg = coarse_again  & 0x1F;

	if (a_gain < 0x20) {
		ret |= sc035gs_write_reg(sc035gs->client, 0x3631,
				SC035GS_REG_VALUE_08BIT, 0x58);
		ret |= sc035gs_write_reg(sc035gs->client, 0x3630,
				SC035GS_REG_VALUE_08BIT, 0x4a);
	} else {
		ret |= sc035gs_write_reg(sc035gs->client, 0x3631,
				SC035GS_REG_VALUE_08BIT, 0x48);
		ret |= sc035gs_write_reg(sc035gs->client, 0x3630,
				SC035GS_REG_VALUE_08BIT, 0x4c);
	}

	ret |= sc035gs_write_reg(sc035gs->client,
				 SC035GS_REG_COARSE_AGAIN,
				 SC035GS_REG_VALUE_08BIT,
				 coarse_again_reg);
	ret |= sc035gs_write_reg(sc035gs->client,
				 SC035GS_REG_FINE_AGAIN,
				 SC035GS_REG_VALUE_08BIT,
				 fine_again_reg);

	return ret;
}


static int __sc035gs_start_stream(struct sc035gs *sc035gs)
{
	int ret;

	ret = sc035gs_write_array(sc035gs->client, sc035gs->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&sc035gs->mutex);
	ret = v4l2_ctrl_handler_setup(&sc035gs->ctrl_handler);
	mutex_lock(&sc035gs->mutex);
	if (ret)
		return ret;

	ret = sc035gs_write_reg(sc035gs->client, SC035GS_REG_CTRL_MODE,
			SC035GS_REG_VALUE_08BIT, SC035GS_MODE_STREAMING);

	usleep_range(10000, 12000);

	ret |= sc035gs_write_reg(sc035gs->client, 0x4418,
			SC035GS_REG_VALUE_08BIT, 0x0a);
	ret |= sc035gs_write_reg(sc035gs->client, 0x4419,
			SC035GS_REG_VALUE_08BIT, 0x80);
	return ret;
}

static int __sc035gs_stop_stream(struct sc035gs *sc035gs)
{
	return sc035gs_write_reg(sc035gs->client, SC035GS_REG_CTRL_MODE,
				 SC035GS_REG_VALUE_08BIT, SC035GS_MODE_SW_STANDBY);
}

static int sc035gs_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc035gs *sc035gs = to_sc035gs(sd);
	struct i2c_client *client = sc035gs->client;
	unsigned int fps;
	int ret = 0;

	mutex_lock(&sc035gs->mutex);
	on = !!on;
	if (on == sc035gs->streaming)
		goto unlock_and_return;

	fps = DIV_ROUND_CLOSEST(sc035gs->cur_mode->max_fps.denominator,
				sc035gs->cur_mode->max_fps.numerator);

	dev_info(&sc035gs->client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
		 sc035gs->cur_mode->width,
		 sc035gs->cur_mode->height,
		 fps);

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __sc035gs_start_stream(sc035gs);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc035gs_stop_stream(sc035gs);
		pm_runtime_put(&client->dev);
	}

	sc035gs->streaming = on;

unlock_and_return:
	mutex_unlock(&sc035gs->mutex);

	return ret;
}

static int sc035gs_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc035gs *sc035gs = to_sc035gs(sd);
	struct i2c_client *client = sc035gs->client;
	int ret = 0;

	mutex_lock(&sc035gs->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc035gs->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		sc035gs->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc035gs->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc035gs->mutex);

	return ret;
}

static int sc035gs_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct sc035gs *sc035gs = to_sc035gs(sd);
	const struct sc035gs_mode *mode = sc035gs->cur_mode;

	if (sc035gs->streaming)
		fi->interval = sc035gs->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 sc035gs_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, SC035GS_XVCLK_FREQ / 1000 / 1000);
}

static int __sc035gs_power_on(struct sc035gs *sc035gs)
{
	int ret;
	u32 delay_us;
	struct device *dev = &sc035gs->client->dev;

	if (!IS_ERR_OR_NULL(sc035gs->pins_default)) {
		ret = pinctrl_select_state(sc035gs->pinctrl,
					   sc035gs->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	ret = clk_set_rate(sc035gs->xvclk, SC035GS_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(sc035gs->xvclk) != SC035GS_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc035gs->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	ret = regulator_bulk_enable(SC035GS_NUM_SUPPLIES, sc035gs->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(sc035gs->reset_gpio))
		gpiod_set_value_cansleep(sc035gs->reset_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR(sc035gs->pwdn_gpio))
		gpiod_set_value_cansleep(sc035gs->pwdn_gpio, 1);

	if (!IS_ERR(sc035gs->reset_gpio))
		gpiod_set_value_cansleep(sc035gs->reset_gpio, 0);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = sc035gs_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(sc035gs->xvclk);

	return ret;
}

static void __sc035gs_power_off(struct sc035gs *sc035gs)
{
	int ret;

	if (!IS_ERR(sc035gs->reset_gpio))
		gpiod_set_value_cansleep(sc035gs->reset_gpio, 1);

	if (!IS_ERR(sc035gs->pwdn_gpio))
		gpiod_set_value_cansleep(sc035gs->pwdn_gpio, 0);
	clk_disable_unprepare(sc035gs->xvclk);
	if (!IS_ERR_OR_NULL(sc035gs->pins_sleep)) {
		ret = pinctrl_select_state(sc035gs->pinctrl,
					   sc035gs->pins_sleep);
		if (ret < 0)
			dev_dbg(&sc035gs->client->dev, "could not set pins\n");
	}
	regulator_bulk_disable(SC035GS_NUM_SUPPLIES, sc035gs->supplies);
}

static int sc035gs_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc035gs *sc035gs = to_sc035gs(sd);

	return __sc035gs_power_on(sc035gs);
}

static int sc035gs_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc035gs *sc035gs = to_sc035gs(sd);

	__sc035gs_power_off(sc035gs);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc035gs_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc035gs *sc035gs = to_sc035gs(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc035gs_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc035gs->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc035gs->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sc035gs_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int sc035gs_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				 struct v4l2_mbus_config *config)
{
	u32 val = 0;
	struct sc035gs *sc035gs = to_sc035gs(sd);

	val = 1 << (sc035gs->cur_mode->lanes - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops sc035gs_pm_ops = {
	SET_RUNTIME_PM_OPS(sc035gs_runtime_suspend,
			   sc035gs_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc035gs_internal_ops = {
	.open = sc035gs_open,
};
#endif

static const struct v4l2_subdev_core_ops sc035gs_core_ops = {
	.s_power = sc035gs_s_power,
	.ioctl = sc035gs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc035gs_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc035gs_video_ops = {
	.s_stream = sc035gs_s_stream,
	.g_frame_interval = sc035gs_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops sc035gs_pad_ops = {
	.enum_mbus_code = sc035gs_enum_mbus_code,
	.enum_frame_size = sc035gs_enum_frame_sizes,
	.enum_frame_interval = sc035gs_enum_frame_interval,
	.get_fmt = sc035gs_get_fmt,
	.set_fmt = sc035gs_set_fmt,
	.get_mbus_config = sc035gs_g_mbus_config,
};

static const struct v4l2_subdev_ops sc035gs_subdev_ops = {
	.core	= &sc035gs_core_ops,
	.video	= &sc035gs_video_ops,
	.pad	= &sc035gs_pad_ops,
};

static void sc035gs_modify_fps_info(struct sc035gs *sc035gs)
{
	const struct sc035gs_mode *mode = sc035gs->cur_mode;

	sc035gs->cur_fps.denominator = mode->max_fps.denominator * mode->vts_def /
				       sc035gs->cur_vts;
}

static int sc035gs_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc035gs *sc035gs = container_of(ctrl->handler,
					       struct sc035gs, ctrl_handler);
	struct i2c_client *client = sc035gs->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc035gs->cur_mode->height + ctrl->val - 6;
		__v4l2_ctrl_modify_range(sc035gs->exposure,
					 sc035gs->exposure->minimum, max,
					 sc035gs->exposure->step,
					 sc035gs->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = sc035gs_write_reg(sc035gs->client, SC035GS_REG_EXPOSURE,
					SC035GS_REG_VALUE_16BIT, ctrl->val << 4);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = sc035gs_set_ctrl_gain(sc035gs, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = sc035gs_write_reg(sc035gs->client, SC035GS_REG_VTS,
					SC035GS_REG_VALUE_16BIT,
					ctrl->val + sc035gs->cur_mode->height);
		if (!ret)
			sc035gs->cur_vts = ctrl->val + sc035gs->cur_mode->height;
		sc035gs_modify_fps_info(sc035gs);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = sc035gs_enable_test_pattern(sc035gs, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc035gs_ctrl_ops = {
	.s_ctrl = sc035gs_set_ctrl,
};

static int sc035gs_initialize_controls(struct sc035gs *sc035gs)
{
	const struct sc035gs_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &sc035gs->ctrl_handler;
	mode = sc035gs->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &sc035gs->mutex;

	sc035gs->link_freq = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
						    ARRAY_SIZE(link_freq_menu_items) - 1, 0,
						    link_freq_menu_items);

	sc035gs->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
						V4L2_CID_PIXEL_RATE,
						0, PIXEL_RATE_WITH_300M,
						1, mode->pixel_rate);

	__v4l2_ctrl_s_ctrl(sc035gs->link_freq, mode->pixel_rate);

	h_blank = mode->hts_def - mode->width;
	sc035gs->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (sc035gs->hblank)
		sc035gs->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	sc035gs->cur_vts = mode->vts_def;
	sc035gs->cur_fps = mode->max_fps;
	sc035gs->vblank = v4l2_ctrl_new_std(handler, &sc035gs_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    SC035GS_VTS_MAX - mode->height,
					    1, vblank_def);

	exposure_max = mode->vts_def - 6;
	sc035gs->exposure = v4l2_ctrl_new_std(handler, &sc035gs_ctrl_ops,
					      V4L2_CID_EXPOSURE, SC035GS_EXPOSURE_MIN,
					      exposure_max, SC035GS_EXPOSURE_STEP,
					      mode->exp_def);

	sc035gs->anal_gain = v4l2_ctrl_new_std(handler, &sc035gs_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
					       ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
					       ANALOG_GAIN_DEFAULT);

	sc035gs->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
							     &sc035gs_ctrl_ops, V4L2_CID_TEST_PATTERN,
							     ARRAY_SIZE(sc035gs_test_pattern_menu) - 1,
							     0, 0, sc035gs_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&sc035gs->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	sc035gs->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int sc035gs_check_sensor_id(struct sc035gs *sc035gs,
				   struct i2c_client *client)
{
	struct device *dev = &sc035gs->client->dev;
	u32 id = 0;
	int ret;

	ret = sc035gs_read_reg(client, SC132GS_REG_CHIP_ID,
			       SC035GS_REG_VALUE_16BIT, &id);
	if (ret || id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%04x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected SC035GS CHIP ID = 0x%04x sensor\n", CHIP_ID);

	return 0;
}

static int sc035gs_configure_regulators(struct sc035gs *sc035gs)
{
	unsigned int i;

	for (i = 0; i < SC035GS_NUM_SUPPLIES; i++)
		sc035gs->supplies[i].supply = sc035gs_supply_names[i];

	return devm_regulator_bulk_get(&sc035gs->client->dev,
				       SC035GS_NUM_SUPPLIES,
				       sc035gs->supplies);
}

static int sc035gs_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc035gs *sc035gs;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	sc035gs = devm_kzalloc(dev, sizeof(*sc035gs), GFP_KERNEL);
	if (!sc035gs)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc035gs->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc035gs->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc035gs->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc035gs->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}
	sc035gs->client = client;
	sc035gs->cur_mode = &supported_modes[0];

	sc035gs->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc035gs->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	sc035gs->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sc035gs->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	sc035gs->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(sc035gs->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");
	ret = sc035gs_configure_regulators(sc035gs);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	sc035gs->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc035gs->pinctrl)) {
		sc035gs->pins_default =
			pinctrl_lookup_state(sc035gs->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc035gs->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		sc035gs->pins_sleep =
			pinctrl_lookup_state(sc035gs->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc035gs->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}
	mutex_init(&sc035gs->mutex);

	sd = &sc035gs->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc035gs_subdev_ops);
	ret = sc035gs_initialize_controls(sc035gs);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc035gs_power_on(sc035gs);
	if (ret)
		goto err_free_handler;

	ret = sc035gs_check_sensor_id(sc035gs, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc035gs_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc035gs->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc035gs->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc035gs->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sc035gs->module_index, facing,
		 SC035GS_NAME, dev_name(sd->dev));
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
	__sc035gs_power_off(sc035gs);
err_free_handler:
	v4l2_ctrl_handler_free(&sc035gs->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc035gs->mutex);

	return ret;
}

static int sc035gs_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc035gs *sc035gs = to_sc035gs(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc035gs->ctrl_handler);
	mutex_destroy(&sc035gs->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc035gs_power_off(sc035gs);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc035gs_of_match[] = {
	{ .compatible = "smartsens,sc035gs" },
	{},
};
MODULE_DEVICE_TABLE(of, sc035gs_of_match);
#endif

static const struct i2c_device_id sc035gs_match_id[] = {
	{ "smartsens,sc035gs", 0 },
	{ },
};

static struct i2c_driver sc035gs_i2c_driver = {
	.driver = {
		.name = SC035GS_NAME,
		.pm = &sc035gs_pm_ops,
		.of_match_table = of_match_ptr(sc035gs_of_match),
	},
	.probe		= &sc035gs_probe,
	.remove		= &sc035gs_remove,
	.id_table	= sc035gs_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc035gs_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc035gs_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Smartsens sc035gs sensor driver");
MODULE_LICENSE("GPL");
