// SPDX-License-Identifier: GPL-2.0
/*
 * sc031gs driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 add enum_frame_interval function.
 * V0.0X01.0X04 add quick stream on/off
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
#include <linux/rk-camera-module.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x04)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define SC031GS_PIXEL_RATE		(72 * 1000 * 1000)
#define SC031GS_XVCLK_FREQ		24000000

#define CHIP_ID				0x0031
#define SC031GS_REG_CHIP_ID		0x3107

#define SC031GS_REG_CTRL_MODE		0x0100
#define SC031GS_MODE_SW_STANDBY		0x0
#define SC031GS_MODE_STREAMING		BIT(0)

#define SC031GS_REG_EXPOSURE		0x3e01
#define	SC031GS_EXPOSURE_MIN		6
#define	SC031GS_EXPOSURE_STEP		1
#define SC031GS_VTS_MAX			0xffff

#define SC031GS_REG_COARSE_AGAIN		0x3e08
#define SC031GS_REG_FINE_AGAIN          0x3e09
#define	ANALOG_GAIN_MIN			0x01
#define	ANALOG_GAIN_MAX			0xF8
#define	ANALOG_GAIN_STEP		1
#define	ANALOG_GAIN_DEFAULT		0x1f

#define SC031GS_REG_TEST_PATTERN		0x4501
#define	SC031GS_TEST_PATTERN_ENABLE	    0xcc
#define	SC031GS_TEST_PATTERN_DISABLE	0xc4

#define SC031GS_REG_VTS			0x320e

#define REG_NULL			0xFFFF

#define SC031GS_REG_VALUE_08BIT		1
#define SC031GS_REG_VALUE_16BIT		2
#define SC031GS_REG_VALUE_24BIT		3
//#define DVP_INTERFACE

#ifdef DVP_INTERFACE
#define PIX_FORMAT MEDIA_BUS_FMT_Y8_1X8
#else
#define PIX_FORMAT MEDIA_BUS_FMT_Y10_1X10
#define SC031GS_LANES			1
#define SC031GS_BITS_PER_SAMPLE		10
#endif

#define SC031GS_NAME			"sc031gs"

static const char * const sc031gs_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define SC031GS_NUM_SUPPLIES ARRAY_SIZE(sc031gs_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct sc031gs_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct sc031gs {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[SC031GS_NUM_SUPPLIES];
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
	const struct sc031gs_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_sc031gs(sd) container_of(sd, struct sc031gs, subdev)

/*
 * Xclk 24Mhz
 * Pclk 45Mhz
 * linelength 683(0x2ab)
 * framelength 878(0x36e)
 * grabwindow_width 640
 * grabwindow_height 480
 * max_framerate 120fps
 * mipi_datarate per lane 720Mbps
 */
static const struct regval sc031gs_global_regs[] = {
#ifdef DVP_INTERFACE
	{0x0100, 0x00},
	{0x300f, 0x0f},
	{0x3018, 0x1f},
	{0x3019, 0xff},
	{0x301c, 0xb4},
	{0x3028, 0x82},
	{0x320c, 0x03},
	{0x320d, 0x6e},
//	{0x320e, 0x02},	//120fps
//	{0x320f, 0xab},
	{0x320e, 0x0a},	//30fps
	{0x320f, 0xac},
	{0x3250, 0xf0},
	{0x3251, 0x02},
	{0x3252, 0x02},
	{0x3253, 0xa6},
	{0x3254, 0x02},
	{0x3255, 0x07},
	{0x3304, 0x48},
	{0x3306, 0x38},
	{0x3309, 0x68},
	{0x330b, 0xe0},
	{0x330c, 0x18},
	{0x330f, 0x20},
	{0x3310, 0x10},
	{0x3314, 0x3a},
	{0x3315, 0x38},
	{0x3316, 0x48},
	{0x3317, 0x20},
	{0x3329, 0x3c},
	{0x332d, 0x3c},
	{0x332f, 0x40},
	{0x3335, 0x44},
	{0x3344, 0x44},
	{0x335b, 0x80},
	{0x335f, 0x80},
	{0x3366, 0x06},
	{0x3385, 0x31},
	{0x3387, 0x51},
	{0x3389, 0x01},
	{0x33b1, 0x03},
	{0x33b2, 0x06},
	{0x3621, 0xa4},
	{0x3622, 0x05},
	{0x3624, 0x47},
	{0x3630, 0x46},
	{0x3631, 0x48},
	{0x3633, 0x52},
	{0x3636, 0x25},
	{0x3637, 0x89},
	{0x3638, 0x0f},
	{0x3639, 0x08},
	{0x363a, 0x00},
	{0x363b, 0x48},
	{0x363c, 0x06},
	{0x363d, 0x00},
	{0x363e, 0xf8},
	{0x3640, 0x02},
	{0x3641, 0x01},
	{0x36e9, 0x00},
	{0x36ea, 0x3b},
	{0x36eb, 0x1a},
	{0x36ec, 0x0a},
	{0x36ed, 0x33},
	{0x36f9, 0x00},
	{0x36fa, 0x3a},
	{0x36fc, 0x01},
	{0x3908, 0x91},
	{0x3d08, 0x00},//0x01
	{0x3e01, 0xd0},
	{0x3e02, 0xff},
	{0x3e06, 0x0c},
	{0x4500, 0x59},
	{0x4501, 0xc4},
	{0x5011, 0x00},
	{0x0100, 0x01},
	{0x4418, 0x08},
	{0x4419, 0x8e},
	{0x0100, 0x00},
//	test pattern
//	{0x4501, 0xac},
//	{0x5011, 0x01},
	{REG_NULL, 0x00},
#else
	{0x0100, 0x00},
	{0x3000, 0x00},
	{0x3001, 0x00},
	{0x300f, 0x0f},
	{0x3018, 0x13},
	{0x3019, 0xfe},
	{0x301c, 0x78},
	{0x3031, 0x0a},
	{0x3037, 0x20},
	{0x303f, 0x01},
	{0x320c, 0x03},
	{0x320d, 0x6e},
	{0x320e, 0x06},
	{0x320f, 0x67},
	{0x3250, 0xc0},
	{0x3251, 0x02},
	{0x3252, 0x02},
	{0x3253, 0xa6},
	{0x3254, 0x02},
	{0x3255, 0x07},
	{0x3304, 0x48},
	{0x3306, 0x38},
	{0x3309, 0x68},
	{0x330b, 0xe0},
	{0x330c, 0x18},
	{0x330f, 0x20},
	{0x3310, 0x10},
	{0x3314, 0x3a},
	{0x3315, 0x38},
	{0x3316, 0x48},
	{0x3317, 0x20},
	{0x3329, 0x3c},
	{0x332d, 0x3c},
	{0x332f, 0x40},
	{0x3335, 0x44},
	{0x3344, 0x44},
	{0x335b, 0x80},
	{0x335f, 0x80},
	{0x3366, 0x06},
	{0x3385, 0x31},
	{0x3387, 0x51},
	{0x3389, 0x01},
	{0x33b1, 0x03},
	{0x33b2, 0x06},
	{0x3621, 0xa4},
	{0x3622, 0x05},
	{0x3630, 0x46},
	{0x3631, 0x48},
	{0x3633, 0x52},
	{0x3636, 0x25},
	{0x3637, 0x89},
	{0x3638, 0x0f},
	{0x3639, 0x08},
	{0x363a, 0x00},
	{0x363b, 0x48},
	{0x363c, 0x06},
	{0x363d, 0x00},
	{0x363e, 0xf8},
	{0x3640, 0x00},
	{0x3641, 0x01},
	{0x36e9, 0x00},
	{0x36ea, 0x3b},
	{0x36eb, 0x0e},
	{0x36ec, 0x0e},
	{0x36ed, 0x33},
	{0x36f9, 0x00},
	{0x36fa, 0x3a},
	{0x36fc, 0x01},
	{0x3908, 0x91},
	{0x3d08, 0x01},
	{0x3e01, 0x14},
	{0x3e02, 0x80},
	{0x3e06, 0x0c},
	{0x4500, 0x59},
	{0x4501, 0xc4},
	{0x4603, 0x00},
	{0x5011, 0x00},
	{0x0100, 0x01},
	{0x4418, 0x08},
	{0x4419, 0x8e},
	{0x0100, 0x00},
	{REG_NULL, 0x00},
#endif
};

static const struct sc031gs_mode supported_modes[] = {
	{
		.width = 640,
		.height = 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0148,
		.hts_def = 0x036e,
		.vts_def = 0x0aac,
		.reg_list = sc031gs_global_regs,
	},
};

static const char * const sc031gs_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

#define SC031GS_LINK_FREQ_360MHZ	(360 * 1000 * 1000)
static const s64 link_freq_menu_items[] = {
	SC031GS_LINK_FREQ_360MHZ
};

/* Write registers up to 4 at a time */
static int sc031gs_write_reg(struct i2c_client *client,
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

static int sc031gs_write_array(struct i2c_client *client,
	const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = sc031gs_write_reg(client, regs[i].addr,
				       SC031GS_REG_VALUE_08BIT, regs[i].val);
		if (regs[i].addr == 0x0100 && regs[i].val == 0x01)
			msleep(10);
	}

	return ret;
}

/* Read registers up to 4 at a time */
static int sc031gs_read_reg(struct i2c_client *client,
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

static int sc031gs_get_reso_dist(const struct sc031gs_mode *mode,
	struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc031gs_mode *
sc031gs_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = sc031gs_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int sc031gs_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sc031gs *sc031gs = to_sc031gs(sd);
	const struct sc031gs_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&sc031gs->mutex);

	mode = sc031gs_find_best_fit(fmt);
	fmt->format.code = PIX_FORMAT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc031gs->mutex);
		return -ENOTTY;
#endif
	} else {
		sc031gs->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc031gs->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc031gs->vblank, vblank_def,
					 SC031GS_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&sc031gs->mutex);

	return 0;
}

static int sc031gs_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sc031gs *sc031gs = to_sc031gs(sd);
	const struct sc031gs_mode *mode = sc031gs->cur_mode;

	mutex_lock(&sc031gs->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc031gs->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = PIX_FORMAT;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&sc031gs->mutex);

	return 0;
}

static int sc031gs_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = PIX_FORMAT;

	return 0;
}

static int sc031gs_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != PIX_FORMAT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int sc031gs_enable_test_pattern(struct sc031gs *sc031gs, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | SC031GS_TEST_PATTERN_ENABLE;
	else
		val = SC031GS_TEST_PATTERN_DISABLE;

	return sc031gs_write_reg(sc031gs->client, SC031GS_REG_TEST_PATTERN,
				SC031GS_REG_VALUE_08BIT, val);
}

static void sc031gs_get_module_inf(struct sc031gs *sc031gs,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, SC031GS_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, sc031gs->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, sc031gs->len_name, sizeof(inf->base.lens));
}

static long sc031gs_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc031gs *sc031gs = to_sc031gs(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		sc031gs_get_module_inf(sc031gs, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = sc031gs_write_reg(sc031gs->client, SC031GS_REG_CTRL_MODE,
				SC031GS_REG_VALUE_08BIT, SC031GS_MODE_STREAMING);
		else
			ret = sc031gs_write_reg(sc031gs->client, SC031GS_REG_CTRL_MODE,
				SC031GS_REG_VALUE_08BIT, SC031GS_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc031gs_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = sc031gs_ioctl(sd, cmd, inf);
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
			ret = sc031gs_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = sc031gs_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int sc031gs_set_ctrl_gain(struct sc031gs *sc031gs, u32 a_gain)
{
	int ret = 0;
	u32 coarse_again, fine_again, fine_again_reg, coarse_again_reg;

		if (a_gain < 0x20) { /*1x ~ 2x*/
			fine_again = a_gain - 16;
			coarse_again = 0x03;
			fine_again_reg = ((0x01 << 4) & 0x10) |
				(fine_again & 0x0f);
			coarse_again_reg = coarse_again  & 0x1F;
		} else if (a_gain < 0x40) { /*2x ~ 4x*/
			fine_again = (a_gain >> 1) - 16;
			coarse_again = 0x7;
			fine_again_reg = ((0x01 << 4) & 0x10) |
				(fine_again & 0x0f);
			coarse_again_reg = coarse_again  & 0x1F;
		} else if (a_gain < 0x80) { /*4x ~ 8x*/
			fine_again = (a_gain >> 2) - 16;
			coarse_again = 0xf;
			fine_again_reg = ((0x01 << 4) & 0x10) |
				(fine_again & 0x0f);
			coarse_again_reg = coarse_again  & 0x1F;
		} else { /*8x ~ 16x*/
			fine_again = (a_gain >> 3) - 16;
			coarse_again = 0x1f;
			fine_again_reg = ((0x01 << 4) & 0x10) |
				(fine_again & 0x0f);
			coarse_again_reg = coarse_again  & 0x1F;
		}

		if (a_gain < 0x20) {
			ret |= sc031gs_write_reg(sc031gs->client, 0x3314,
				SC031GS_REG_VALUE_08BIT, 0x42);
			ret |= sc031gs_write_reg(sc031gs->client, 0x3317,
				SC031GS_REG_VALUE_08BIT, 0x20);
		} else {
			ret |= sc031gs_write_reg(sc031gs->client, 0x3314,
				SC031GS_REG_VALUE_08BIT, 0x4f);
			ret |= sc031gs_write_reg(sc031gs->client, 0x3317,
				SC031GS_REG_VALUE_08BIT, 0x0f);
		}
		ret |= sc031gs_write_reg(sc031gs->client,
			SC031GS_REG_COARSE_AGAIN,
			SC031GS_REG_VALUE_08BIT,
			coarse_again_reg);
		ret |= sc031gs_write_reg(sc031gs->client,
			SC031GS_REG_FINE_AGAIN,
			SC031GS_REG_VALUE_08BIT,
			fine_again_reg);

	return ret;
}

static int __sc031gs_start_stream(struct sc031gs *sc031gs)
{
	int ret;

//	ret = sc031gs_write_array(sc031gs->client, sc031gs_global_regs);
//	if (ret)
//		return ret;
	ret = sc031gs_write_array(sc031gs->client, sc031gs->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&sc031gs->mutex);
	ret = v4l2_ctrl_handler_setup(&sc031gs->ctrl_handler);
	mutex_lock(&sc031gs->mutex);
	if (ret)
		return ret;

	return sc031gs_write_reg(sc031gs->client, SC031GS_REG_CTRL_MODE,
			SC031GS_REG_VALUE_08BIT, SC031GS_MODE_STREAMING);
}

static int __sc031gs_stop_stream(struct sc031gs *sc031gs)
{
	return sc031gs_write_reg(sc031gs->client, SC031GS_REG_CTRL_MODE,
			SC031GS_REG_VALUE_08BIT, SC031GS_MODE_SW_STANDBY);
}

static int sc031gs_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc031gs *sc031gs = to_sc031gs(sd);
	struct i2c_client *client = sc031gs->client;
	int ret = 0;

	mutex_lock(&sc031gs->mutex);
	on = !!on;
	if (on == sc031gs->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __sc031gs_start_stream(sc031gs);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc031gs_stop_stream(sc031gs);
		pm_runtime_put(&client->dev);
	}

	sc031gs->streaming = on;

unlock_and_return:
	mutex_unlock(&sc031gs->mutex);

	return ret;
}

static int sc031gs_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct sc031gs *sc031gs = to_sc031gs(sd);
	const struct sc031gs_mode *mode = sc031gs->cur_mode;

	mutex_lock(&sc031gs->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&sc031gs->mutex);

	return 0;
}

static int sc031gs_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc031gs *sc031gs = to_sc031gs(sd);
	struct i2c_client *client = sc031gs->client;
	int ret = 0;

	mutex_lock(&sc031gs->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc031gs->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		sc031gs->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc031gs->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc031gs->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 sc031gs_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, SC031GS_XVCLK_FREQ / 1000 / 1000);
}

static int __sc031gs_power_on(struct sc031gs *sc031gs)
{
	int ret;
	u32 delay_us;
	struct device *dev = &sc031gs->client->dev;

	ret = clk_set_rate(sc031gs->xvclk, SC031GS_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(sc031gs->xvclk) != SC031GS_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc031gs->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	ret = regulator_bulk_enable(SC031GS_NUM_SUPPLIES, sc031gs->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(sc031gs->pwdn_gpio))
		gpiod_set_value_cansleep(sc031gs->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = sc031gs_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(sc031gs->xvclk);

	return ret;
}

static void __sc031gs_power_off(struct sc031gs *sc031gs)
{
	if (!IS_ERR(sc031gs->pwdn_gpio))
		gpiod_set_value_cansleep(sc031gs->pwdn_gpio, 0);
	clk_disable_unprepare(sc031gs->xvclk);

	regulator_bulk_disable(SC031GS_NUM_SUPPLIES, sc031gs->supplies);
}

static int sc031gs_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc031gs *sc031gs = to_sc031gs(sd);

	return __sc031gs_power_on(sc031gs);
}

static int sc031gs_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc031gs *sc031gs = to_sc031gs(sd);

	__sc031gs_power_off(sc031gs);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc031gs_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc031gs *sc031gs = to_sc031gs(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc031gs_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc031gs->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = PIX_FORMAT;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc031gs->mutex);
	/* No crop or compose */

	return 0;
}
#endif

#ifdef DVP_INTERFACE
static int sc031gs_g_mbus_config(struct v4l2_subdev *sd,
	struct v4l2_mbus_config *config)
{
	config->type = V4L2_MBUS_PARALLEL;
	config->flags = V4L2_MBUS_HSYNC_ACTIVE_HIGH |
			V4L2_MBUS_VSYNC_ACTIVE_LOW |
			V4L2_MBUS_PCLK_SAMPLE_FALLING;
	return 0;
}
#endif

static int sc031gs_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != PIX_FORMAT)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static const struct dev_pm_ops sc031gs_pm_ops = {
	SET_RUNTIME_PM_OPS(sc031gs_runtime_suspend,
			   sc031gs_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc031gs_internal_ops = {
	.open = sc031gs_open,
};
#endif

static const struct v4l2_subdev_core_ops sc031gs_core_ops = {
	.s_power = sc031gs_s_power,
	.ioctl = sc031gs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc031gs_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc031gs_video_ops = {
	.s_stream = sc031gs_s_stream,
	.g_frame_interval = sc031gs_g_frame_interval,
	#ifdef DVP_INTERFACE
	.g_mbus_config = sc031gs_g_mbus_config,
	#endif
};

static const struct v4l2_subdev_pad_ops sc031gs_pad_ops = {
	.enum_mbus_code = sc031gs_enum_mbus_code,
	.enum_frame_size = sc031gs_enum_frame_sizes,
	.enum_frame_interval = sc031gs_enum_frame_interval,
	.get_fmt = sc031gs_get_fmt,
	.set_fmt = sc031gs_set_fmt,
};

static const struct v4l2_subdev_ops sc031gs_subdev_ops = {
	.core	= &sc031gs_core_ops,
	.video	= &sc031gs_video_ops,
	.pad	= &sc031gs_pad_ops,
};

static int sc031gs_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc031gs *sc031gs = container_of(ctrl->handler,
					     struct sc031gs, ctrl_handler);
	struct i2c_client *client = sc031gs->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc031gs->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(sc031gs->exposure,
					 sc031gs->exposure->minimum, max,
					 sc031gs->exposure->step,
					 sc031gs->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = sc031gs_write_reg(sc031gs->client, SC031GS_REG_EXPOSURE,
				       SC031GS_REG_VALUE_16BIT, ctrl->val << 4);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = sc031gs_set_ctrl_gain(sc031gs, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = sc031gs_write_reg(sc031gs->client, SC031GS_REG_VTS,
				       SC031GS_REG_VALUE_16BIT,
				       ctrl->val + sc031gs->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = sc031gs_enable_test_pattern(sc031gs, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc031gs_ctrl_ops = {
	.s_ctrl = sc031gs_set_ctrl,
};

static int sc031gs_initialize_controls(struct sc031gs *sc031gs)
{
	const struct sc031gs_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &sc031gs->ctrl_handler;
	mode = sc031gs->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &sc031gs->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, SC031GS_PIXEL_RATE, 1, SC031GS_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	sc031gs->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (sc031gs->hblank)
		sc031gs->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	sc031gs->vblank = v4l2_ctrl_new_std(handler, &sc031gs_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				SC031GS_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 6;
	sc031gs->exposure = v4l2_ctrl_new_std(handler, &sc031gs_ctrl_ops,
				V4L2_CID_EXPOSURE, SC031GS_EXPOSURE_MIN,
				exposure_max, SC031GS_EXPOSURE_STEP,
				mode->exp_def);

	sc031gs->anal_gain = v4l2_ctrl_new_std(handler, &sc031gs_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	sc031gs->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&sc031gs_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(sc031gs_test_pattern_menu) - 1,
				0, 0, sc031gs_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&sc031gs->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	sc031gs->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int sc031gs_check_sensor_id(struct sc031gs *sc031gs,
				  struct i2c_client *client)
{
	struct device *dev = &sc031gs->client->dev;
	u32 id = 0;
	int ret;

	ret = sc031gs_read_reg(client, SC031GS_REG_CHIP_ID,
			      SC031GS_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%04x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected SC031GS CHIP ID = 0x%04x sensor\n", CHIP_ID);

	return 0;
}

static int sc031gs_configure_regulators(struct sc031gs *sc031gs)
{
	unsigned int i;

	for (i = 0; i < SC031GS_NUM_SUPPLIES; i++)
		sc031gs->supplies[i].supply = sc031gs_supply_names[i];

	return devm_regulator_bulk_get(&sc031gs->client->dev,
				       SC031GS_NUM_SUPPLIES,
				       sc031gs->supplies);
}

static int sc031gs_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc031gs *sc031gs;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	sc031gs = devm_kzalloc(dev, sizeof(*sc031gs), GFP_KERNEL);
	if (!sc031gs)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc031gs->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc031gs->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc031gs->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc031gs->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	sc031gs->client = client;
	sc031gs->cur_mode = &supported_modes[0];

	sc031gs->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc031gs->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	sc031gs->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sc031gs->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	sc031gs->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(sc031gs->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");
	ret = sc031gs_configure_regulators(sc031gs);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&sc031gs->mutex);

	sd = &sc031gs->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc031gs_subdev_ops);
	ret = sc031gs_initialize_controls(sc031gs);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc031gs_power_on(sc031gs);
	if (ret)
		goto err_free_handler;

	ret = sc031gs_check_sensor_id(sc031gs, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc031gs_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc031gs->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc031gs->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc031gs->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sc031gs->module_index, facing,
		 SC031GS_NAME, dev_name(sd->dev));
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
	__sc031gs_power_off(sc031gs);
err_free_handler:
	v4l2_ctrl_handler_free(&sc031gs->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc031gs->mutex);

	return ret;
}

static int sc031gs_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc031gs *sc031gs = to_sc031gs(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc031gs->ctrl_handler);
	mutex_destroy(&sc031gs->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc031gs_power_off(sc031gs);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc031gs_of_match[] = {
	{ .compatible = "smartsens,sc031gs" },
	{},
};
MODULE_DEVICE_TABLE(of, sc031gs_of_match);
#endif

static const struct i2c_device_id sc031gs_match_id[] = {
	{ "smartsens,sc031gs", 0 },
	{ },
};

static struct i2c_driver sc031gs_i2c_driver = {
	.driver = {
		.name = SC031GS_NAME,
		.pm = &sc031gs_pm_ops,
		.of_match_table = of_match_ptr(sc031gs_of_match),
	},
	.probe		= &sc031gs_probe,
	.remove		= &sc031gs_remove,
	.id_table	= sc031gs_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc031gs_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc031gs_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Smartsens sc031gs sensor driver");
MODULE_AUTHOR("zack.zeng");
MODULE_LICENSE("GPL v2");
