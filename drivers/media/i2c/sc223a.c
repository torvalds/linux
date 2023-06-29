// SPDX-License-Identifier: GPL-2.0
/*
 * sc223a driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 first version
 */

//#define DEBUG
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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x01)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define SC223A_LANES			2
#define SC223A_BITS_PER_SAMPLE		10
#define SC223A_LINK_FREQ_405		202500000

#define PIXEL_RATE_WITH_405M_10BIT	(SC223A_LINK_FREQ_405 * 2 * \
					SC223A_LANES / SC223A_BITS_PER_SAMPLE)
#define SC223A_XVCLK_FREQ		24000000

#define CHIP_ID				0xcb3e
#define SC223A_REG_CHIP_ID		0x3107

#define SC223A_REG_CTRL_MODE		0x0100
#define SC223A_MODE_SW_STANDBY		0x0
#define SC223A_MODE_STREAMING		BIT(0)

#define SC223A_REG_EXPOSURE_H		0x3e00
#define SC223A_REG_EXPOSURE_M		0x3e01
#define SC223A_REG_EXPOSURE_L		0x3e02
#define	SC223A_EXPOSURE_MIN		3
#define	SC223A_EXPOSURE_STEP		1
#define SC223A_VTS_MAX			0x7fff

#define SC223A_REG_DIG_GAIN		0x3e06
#define SC223A_REG_DIG_FINE_GAIN	0x3e07
#define SC223A_REG_ANA_GAIN		0x3e09
#define SC223A_GAIN_MIN			0x0080
#define SC223A_GAIN_MAX			(29656)	//57.92*4*128
#define SC223A_GAIN_STEP		1
#define SC223A_GAIN_DEFAULT		0x80

#define SC223A_REG_GROUP_HOLD		0x3812
#define SC223A_GROUP_HOLD_START		0x00
#define SC223A_GROUP_HOLD_END		0x30

#define SC223A_REG_TEST_PATTERN		0x4501
#define SC223A_TEST_PATTERN_BIT_MASK	BIT(3)

#define SC223A_REG_VTS_H		0x320e
#define SC223A_REG_VTS_L		0x320f

#define SC223A_FLIP_MIRROR_REG		0x3221

#define SC223A_FETCH_EXP_H(VAL)		(((VAL) >> 12) & 0xF)
#define SC223A_FETCH_EXP_M(VAL)		(((VAL) >> 4) & 0xFF)
#define SC223A_FETCH_EXP_L(VAL)		(((VAL) & 0xF) << 4)

#define SC223A_FETCH_AGAIN_H(VAL)	(((VAL) >> 8) & 0x03)
#define SC223A_FETCH_AGAIN_L(VAL)	((VAL) & 0xFF)

#define SC223A_FETCH_MIRROR(VAL, ENABLE)	(ENABLE ? VAL | 0x06 : VAL & 0xf9)
#define SC223A_FETCH_FLIP(VAL, ENABLE)		(ENABLE ? VAL | 0x60 : VAL & 0x9f)

#define REG_DELAY			0xFFFE
#define REG_NULL			0xFFFF

#define SC223A_REG_VALUE_08BIT		1
#define SC223A_REG_VALUE_16BIT		2
#define SC223A_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define SC223A_NAME			"sc223a"

static const char * const sc223a_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define SC223A_NUM_SUPPLIES ARRAY_SIZE(sc223a_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct sc223a_mode {
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

struct sc223a {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[SC223A_NUM_SUPPLIES];

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
	struct v4l2_fract	cur_fps;
	bool			streaming;
	bool			power_on;
	const struct sc223a_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
	bool			has_init_exp;
	bool			is_thunderboot;
	bool			is_first_streamoff;
	struct preisp_hdrae_exp_s init_hdrae_exp;
};

#define to_sc223a(sd) container_of(sd, struct sc223a, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval sc223a_global_regs[] = {
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 405Mbps, 2lane
 */
static const struct regval sc223a_linear_10_1920x1080_30fps_regs[] = {
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x37f9, 0x80},
	{0x301f, 0x08},
	{0x30b8, 0x44},
	{0x320c, 0x08},
	{0x320d, 0xca},
	{0x320e, 0x04},
	{0x320f, 0xb0},
	{0x3253, 0x0c},
	{0x3281, 0x80},
	{0x3301, 0x06},
	{0x3302, 0x12},
	{0x3306, 0x84},
	{0x3309, 0x60},
	{0x330a, 0x00},
	{0x330b, 0xe0},
	{0x330d, 0x20},
	{0x3314, 0x15},
	{0x331e, 0x41},
	{0x331f, 0x51},
	{0x3320, 0x0a},
	{0x3326, 0x0e},
	{0x3333, 0x10},
	{0x3334, 0x40},
	{0x335d, 0x60},
	{0x335e, 0x06},
	{0x335f, 0x08},
	{0x3364, 0x56},
	{0x337a, 0x06},
	{0x337b, 0x0e},
	{0x337c, 0x02},
	{0x337d, 0x0a},
	{0x3390, 0x03},
	{0x3391, 0x0f},
	{0x3392, 0x1f},
	{0x3393, 0x06},
	{0x3394, 0x06},
	{0x3395, 0x06},
	{0x3396, 0x48},
	{0x3397, 0x4b},
	{0x3398, 0x5f},
	{0x3399, 0x06},
	{0x339a, 0x06},
	{0x339b, 0x9c},
	{0x339c, 0x9c},
	{0x33a2, 0x04},
	{0x33a3, 0x0a},
	{0x33ad, 0x1c},
	{0x33af, 0x40},
	{0x33b1, 0x80},
	{0x33b3, 0x20},
	{0x349f, 0x02},
	{0x34a6, 0x48},
	{0x34a7, 0x4b},
	{0x34a8, 0x20},
	{0x34a9, 0x20},
	{0x34f8, 0x5f},
	{0x34f9, 0x10},
	{0x3616, 0xac},
	{0x3630, 0xc0},
	{0x3631, 0x86},
	{0x3632, 0x26},
	{0x3633, 0x32},
	{0x3637, 0x29},
	{0x363a, 0x84},
	{0x363b, 0x04},
	{0x363c, 0x08},
	{0x3641, 0x3a},
	{0x364f, 0x39},
	{0x3670, 0xce},
	{0x3674, 0xc0},
	{0x3675, 0xc0},
	{0x3676, 0xc0},
	{0x3677, 0x86},
	{0x3678, 0x8b},
	{0x3679, 0x8c},
	{0x367c, 0x4b},
	{0x367d, 0x5f},
	{0x367e, 0x4b},
	{0x367f, 0x5f},
	{0x3690, 0x62},
	{0x3691, 0x63},
	{0x3692, 0x63},
	{0x3699, 0x86},
	{0x369a, 0x92},
	{0x369b, 0xa4},
	{0x369c, 0x48},
	{0x369d, 0x4b},
	{0x36a2, 0x4b},
	{0x36a3, 0x4f},
	{0x36ea, 0x09},
	{0x36eb, 0x0c},
	{0x36ec, 0x1c},
	{0x36ed, 0x28},
	{0x370f, 0x01},
	{0x3721, 0x6c},
	{0x3722, 0x09},
	{0x3724, 0x41},
	{0x3725, 0xc4},
	{0x37b0, 0x09},
	{0x37b1, 0x09},
	{0x37b2, 0x09},
	{0x37b3, 0x48},
	{0x37b4, 0x5f},
	{0x37fa, 0x09},
	{0x37fb, 0x32},
	{0x37fc, 0x10},
	{0x37fd, 0x37},
	{0x3900, 0x19},
	{0x3901, 0x02},
	{0x3905, 0xb8},
	{0x391b, 0x82},
	{0x391c, 0x00},
	{0x391f, 0x04},
	{0x3933, 0x81},
	{0x3934, 0x4c},
	{0x393f, 0xff},
	{0x3940, 0x73},
	{0x3942, 0x01},
	{0x3943, 0x4d},
	{0x3946, 0x20},
	{0x3957, 0x86},
	{0x3e01, 0x95},
	{0x3e02, 0x60},
	{0x3e28, 0xc4},
	{0x440e, 0x02},
	{0x4501, 0xc0},
	{0x4509, 0x14},
	{0x450d, 0x11},
	{0x4518, 0x00},
	{0x451b, 0x0a},
	{0x4819, 0x07},
	{0x481b, 0x04},
	{0x481d, 0x0e},
	{0x481f, 0x03},
	{0x4821, 0x09},
	{0x4823, 0x04},
	{0x4825, 0x03},
	{0x4827, 0x03},
	{0x4829, 0x06},
	{0x501c, 0x00},
	{0x501d, 0x60},
	{0x501e, 0x00},
	{0x501f, 0x40},
	{0x5799, 0x06},
	{0x5ae0, 0xfe},
	{0x5ae1, 0x40},
	{0x5ae2, 0x38},
	{0x5ae3, 0x30},
	{0x5ae4, 0x28},
	{0x5ae5, 0x38},
	{0x5ae6, 0x30},
	{0x5ae7, 0x28},
	{0x5ae8, 0x3f},
	{0x5ae9, 0x34},
	{0x5aea, 0x2c},
	{0x5aeb, 0x3f},
	{0x5aec, 0x34},
	{0x5aed, 0x2c},
	{0x5aee, 0xfe},
	{0x5aef, 0x40},
	{0x5af4, 0x38},
	{0x5af5, 0x30},
	{0x5af6, 0x28},
	{0x5af7, 0x38},
	{0x5af8, 0x30},
	{0x5af9, 0x28},
	{0x5afa, 0x3f},
	{0x5afb, 0x34},
	{0x5afc, 0x2c},
	{0x5afd, 0x3f},
	{0x5afe, 0x34},
	{0x5aff, 0x2c},
	{0x36e9, 0x53},
	{0x37f9, 0x53},
	{REG_NULL, 0x00},
};

static const struct sc223a_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0080,
		.hts_def = 0x08ca,
		.vts_def = 0x04b0,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc223a_linear_10_1920x1080_30fps_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}
};

static const s64 link_freq_menu_items[] = {
	SC223A_LINK_FREQ_405
};

static const char * const sc223a_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int sc223a_write_reg(struct i2c_client *client, u16 reg,
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

static int sc223a_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = sc223a_write_reg(client, regs[i].addr,
					SC223A_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int sc223a_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static int sc223a_set_gain_reg(struct sc223a *sc223a, u32 gain)
{
	struct i2c_client *client = sc223a->client;
	u32 coarse_again = 0, coarse_dgain = 0, fine_dgain = 0;
	int ret = 0, gain_factor;

	if (gain < 128)
		gain = 128;
	else if (gain > SC223A_GAIN_MAX)
		gain = SC223A_GAIN_MAX;

	gain_factor = gain * 1000 / 128;
	if (gain_factor < 1810) {
		coarse_again = 0x00;
		coarse_dgain = 0x00;
		fine_dgain = gain_factor * 128 / 1000;
	} else if (gain_factor < 1810 * 2) {
		coarse_again = 0x40;
		coarse_dgain = 0x00;
		fine_dgain = gain_factor * 128 / 1810;
	} else if (gain_factor < 1810 * 4) {
		coarse_again = 0x48;
		coarse_dgain = 0x00;
		fine_dgain = gain_factor * 128 / 1810 / 2;
	} else if (gain_factor < 1810 * 8) {
		coarse_again = 0x49;
		coarse_dgain = 0x00;
		fine_dgain = gain_factor * 128 / 1810 / 4;
	} else if (gain_factor < 1810 * 16) {
		coarse_again = 0x4b;
		coarse_dgain = 0x00;
		fine_dgain = gain_factor * 128 / 1810 / 8;
	} else if (gain_factor < 1810 * 32) {
		coarse_again = 0x4f;
		coarse_dgain = 0x00;
		fine_dgain = gain_factor * 128 / 1810 / 16;
	} else if (gain_factor < 1810 * 64) {
		//open dgain begin  max digital gain 4X
		coarse_again = 0x5f;
		coarse_dgain = 0x00;
		fine_dgain = gain_factor * 128 / 1810 / 32;
	} else if (gain_factor < 1810 * 128) {
		coarse_again = 0x5f;
		coarse_dgain = 0x01;
		fine_dgain = gain_factor * 128 / 1810 / 64;
	} else {
		coarse_again = 0x5f;
		coarse_dgain = 0x03;
		fine_dgain = 0x80;
	}
	dev_dbg(&client->dev, "c_again: 0x%x, c_dgain: 0x%x, f_dgain: 0x%0x\n",
		    coarse_again, coarse_dgain, fine_dgain);

	ret = sc223a_write_reg(sc223a->client,
				SC223A_REG_DIG_GAIN,
				SC223A_REG_VALUE_08BIT,
				coarse_dgain);
	ret |= sc223a_write_reg(sc223a->client,
				 SC223A_REG_DIG_FINE_GAIN,
				 SC223A_REG_VALUE_08BIT,
				 fine_dgain);
	ret |= sc223a_write_reg(sc223a->client,
				 SC223A_REG_ANA_GAIN,
				 SC223A_REG_VALUE_08BIT,
				 coarse_again);

	return ret;
}

static int sc223a_get_reso_dist(const struct sc223a_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc223a_mode *
sc223a_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = sc223a_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int sc223a_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc223a *sc223a = to_sc223a(sd);
	const struct sc223a_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&sc223a->mutex);

	mode = sc223a_find_best_fit(fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc223a->mutex);
		return -ENOTTY;
#endif
	} else {
		sc223a->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc223a->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc223a->vblank, vblank_def,
					 SC223A_VTS_MAX - mode->height,
					 1, vblank_def);
		sc223a->cur_fps = mode->max_fps;
	}

	mutex_unlock(&sc223a->mutex);

	return 0;
}

static int sc223a_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc223a *sc223a = to_sc223a(sd);
	const struct sc223a_mode *mode = sc223a->cur_mode;

	mutex_lock(&sc223a->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc223a->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
		/* format info: width/height/data type/virctual channel */
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&sc223a->mutex);

	return 0;
}

static int sc223a_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct sc223a *sc223a = to_sc223a(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = sc223a->cur_mode->bus_fmt;

	return 0;
}

static int sc223a_enum_frame_sizes(struct v4l2_subdev *sd,
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

static int sc223a_enable_test_pattern(struct sc223a *sc223a, u32 pattern)
{
	u32 val = 0;
	int ret = 0;

	ret = sc223a_read_reg(sc223a->client, SC223A_REG_TEST_PATTERN,
			       SC223A_REG_VALUE_08BIT, &val);
	if (pattern)
		val |= SC223A_TEST_PATTERN_BIT_MASK;
	else
		val &= ~SC223A_TEST_PATTERN_BIT_MASK;

	ret |= sc223a_write_reg(sc223a->client, SC223A_REG_TEST_PATTERN,
				 SC223A_REG_VALUE_08BIT, val);
	return ret;
}

static int sc223a_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct sc223a *sc223a = to_sc223a(sd);
	const struct sc223a_mode *mode = sc223a->cur_mode;

	if (sc223a->streaming)
		fi->interval = sc223a->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

static int sc223a_g_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct sc223a *sc223a = to_sc223a(sd);
	const struct sc223a_mode *mode = sc223a->cur_mode;

	u32 val = 1 << (SC223A_LANES - 1) |
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

static void sc223a_get_module_inf(struct sc223a *sc223a,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, SC223A_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, sc223a->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, sc223a->len_name, sizeof(inf->base.lens));
}

static long sc223a_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc223a *sc223a = to_sc223a(sd);
	struct rkmodule_hdr_cfg *hdr;
	u32 i, h, w;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		sc223a_get_module_inf(sc223a, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = sc223a->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = sc223a->cur_mode->width;
		h = sc223a->cur_mode->height;
		for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				sc223a->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == ARRAY_SIZE(supported_modes)) {
			dev_err(&sc223a->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = sc223a->cur_mode->hts_def - sc223a->cur_mode->width;
			h = sc223a->cur_mode->vts_def - sc223a->cur_mode->height;
			__v4l2_ctrl_modify_range(sc223a->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(sc223a->vblank, h,
						 SC223A_VTS_MAX - sc223a->cur_mode->height, 1, h);
			sc223a->cur_fps = sc223a->cur_mode->max_fps;
		}
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = sc223a_write_reg(sc223a->client, SC223A_REG_CTRL_MODE,
				 SC223A_REG_VALUE_08BIT, SC223A_MODE_STREAMING);
		else
			ret = sc223a_write_reg(sc223a->client, SC223A_REG_CTRL_MODE,
				 SC223A_REG_VALUE_08BIT, SC223A_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc223a_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = sc223a_ioctl(sd, cmd, inf);
		if (!ret) {
			if (copy_to_user(up, inf, sizeof(*inf)))
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

		ret = sc223a_ioctl(sd, cmd, hdr);
		if (!ret) {
			if (copy_to_user(up, hdr, sizeof(*hdr)))
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
			ret = sc223a_ioctl(sd, cmd, hdr);
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
			ret = sc223a_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = sc223a_ioctl(sd, cmd, &stream);
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

static int __sc223a_start_stream(struct sc223a *sc223a)
{
	int ret;

	if (!sc223a->is_thunderboot) {
		ret = sc223a_write_array(sc223a->client, sc223a->cur_mode->reg_list);
		if (ret)
			return ret;
		/* In case these controls are set before streaming */
		ret = __v4l2_ctrl_handler_setup(&sc223a->ctrl_handler);
		if (ret)
			return ret;
		if (sc223a->has_init_exp && sc223a->cur_mode->hdr_mode != NO_HDR) {
			ret = sc223a_ioctl(&sc223a->subdev, PREISP_CMD_SET_HDRAE_EXP,
				&sc223a->init_hdrae_exp);
			if (ret) {
				dev_err(&sc223a->client->dev,
					"init exp fail in hdr mode\n");
				return ret;
			}
		}
	}
	return sc223a_write_reg(sc223a->client, SC223A_REG_CTRL_MODE,
				SC223A_REG_VALUE_08BIT, SC223A_MODE_STREAMING);
}

static int __sc223a_stop_stream(struct sc223a *sc223a)
{
	sc223a->has_init_exp = false;
	if (sc223a->is_thunderboot) {
		sc223a->is_first_streamoff = true;
		pm_runtime_put(&sc223a->client->dev);
	}
	return sc223a_write_reg(sc223a->client, SC223A_REG_CTRL_MODE,
				 SC223A_REG_VALUE_08BIT, SC223A_MODE_SW_STANDBY);
}

static int __sc223a_power_on(struct sc223a *sc223a);
static int sc223a_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc223a *sc223a = to_sc223a(sd);
	struct i2c_client *client = sc223a->client;
	int ret = 0;

	mutex_lock(&sc223a->mutex);
	on = !!on;
	if (on == sc223a->streaming)
		goto unlock_and_return;
	if (on) {
		if (sc223a->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
			sc223a->is_thunderboot = false;
			__sc223a_power_on(sc223a);
		}
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		ret = __sc223a_start_stream(sc223a);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc223a_stop_stream(sc223a);
		pm_runtime_put(&client->dev);
	}

	sc223a->streaming = on;
unlock_and_return:
	mutex_unlock(&sc223a->mutex);
	return ret;
}

static int sc223a_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc223a *sc223a = to_sc223a(sd);
	struct i2c_client *client = sc223a->client;
	int ret = 0;

	mutex_lock(&sc223a->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc223a->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		if (!sc223a->is_thunderboot) {
			ret = sc223a_write_array(sc223a->client, sc223a_global_regs);
			if (ret) {
				v4l2_err(sd, "could not set init registers\n");
				pm_runtime_put_noidle(&client->dev);
				goto unlock_and_return;
			}
		}

		sc223a->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc223a->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc223a->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 sc223a_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, SC223A_XVCLK_FREQ / 1000 / 1000);
}

static int __sc223a_power_on(struct sc223a *sc223a)
{
	int ret;
	u32 delay_us;
	struct device *dev = &sc223a->client->dev;

	if (!IS_ERR_OR_NULL(sc223a->pins_default)) {
		ret = pinctrl_select_state(sc223a->pinctrl,
					   sc223a->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(sc223a->xvclk, SC223A_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(sc223a->xvclk) != SC223A_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc223a->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (sc223a->is_thunderboot)
		return 0;

	if (!IS_ERR(sc223a->reset_gpio))
		gpiod_set_value_cansleep(sc223a->reset_gpio, 0);

	ret = regulator_bulk_enable(SC223A_NUM_SUPPLIES, sc223a->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(sc223a->reset_gpio))
		gpiod_set_value_cansleep(sc223a->reset_gpio, 1);

	usleep_range(500, 1000);

	if (!IS_ERR(sc223a->pwdn_gpio))
		gpiod_set_value_cansleep(sc223a->pwdn_gpio, 1);

	if (!IS_ERR(sc223a->reset_gpio))
		usleep_range(6000, 8000);
	else
		usleep_range(12000, 16000);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = sc223a_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(sc223a->xvclk);

	return ret;
}

static void __sc223a_power_off(struct sc223a *sc223a)
{
	int ret;
	struct device *dev = &sc223a->client->dev;

	clk_disable_unprepare(sc223a->xvclk);
	if (sc223a->is_thunderboot) {
		if (sc223a->is_first_streamoff) {
			sc223a->is_thunderboot = false;
			sc223a->is_first_streamoff = false;
		} else {
			return;
		}
	}

	if (!IS_ERR(sc223a->pwdn_gpio))
		gpiod_set_value_cansleep(sc223a->pwdn_gpio, 0);
	clk_disable_unprepare(sc223a->xvclk);
	if (!IS_ERR(sc223a->reset_gpio))
		gpiod_set_value_cansleep(sc223a->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(sc223a->pins_sleep)) {
		ret = pinctrl_select_state(sc223a->pinctrl,
					   sc223a->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(SC223A_NUM_SUPPLIES, sc223a->supplies);
}

static int sc223a_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc223a *sc223a = to_sc223a(sd);

	return __sc223a_power_on(sc223a);
}

static int sc223a_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc223a *sc223a = to_sc223a(sd);

	__sc223a_power_off(sc223a);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc223a_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc223a *sc223a = to_sc223a(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc223a_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc223a->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc223a->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sc223a_enum_frame_interval(struct v4l2_subdev *sd,
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

static const struct dev_pm_ops sc223a_pm_ops = {
	SET_RUNTIME_PM_OPS(sc223a_runtime_suspend,
			   sc223a_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc223a_internal_ops = {
	.open = sc223a_open,
};
#endif

static const struct v4l2_subdev_core_ops sc223a_core_ops = {
	.s_power = sc223a_s_power,
	.ioctl = sc223a_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc223a_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc223a_video_ops = {
	.s_stream = sc223a_s_stream,
	.g_frame_interval = sc223a_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops sc223a_pad_ops = {
	.enum_mbus_code = sc223a_enum_mbus_code,
	.enum_frame_size = sc223a_enum_frame_sizes,
	.enum_frame_interval = sc223a_enum_frame_interval,
	.get_fmt = sc223a_get_fmt,
	.set_fmt = sc223a_set_fmt,
	.get_mbus_config = sc223a_g_mbus_config,
};

static const struct v4l2_subdev_ops sc223a_subdev_ops = {
	.core	= &sc223a_core_ops,
	.video	= &sc223a_video_ops,
	.pad	= &sc223a_pad_ops,
};

static void sc223a_modify_fps_info(struct sc223a *sc223a)
{
	const struct sc223a_mode *mode = sc223a->cur_mode;

	sc223a->cur_fps.denominator = mode->max_fps.denominator * mode->vts_def /
				      sc223a->cur_vts;
}

static int sc223a_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc223a *sc223a = container_of(ctrl->handler,
					       struct sc223a, ctrl_handler);
	struct i2c_client *client = sc223a->client;
	s64 max;
	int ret = 0;
	u32 val = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc223a->cur_mode->height + ctrl->val - 10;
		__v4l2_ctrl_modify_range(sc223a->exposure,
					 sc223a->exposure->minimum, max,
					 sc223a->exposure->step,
					 sc223a->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dev_dbg(&client->dev, "set exposure 0x%x\n", ctrl->val);
		if (sc223a->cur_mode->hdr_mode == NO_HDR) {
			val = ctrl->val * 2;
			/* 4 least significant bits of expsoure are fractional part */
			ret = sc223a_write_reg(sc223a->client,
						SC223A_REG_EXPOSURE_H,
						SC223A_REG_VALUE_08BIT,
						SC223A_FETCH_EXP_H(val));
			ret |= sc223a_write_reg(sc223a->client,
						 SC223A_REG_EXPOSURE_M,
						 SC223A_REG_VALUE_08BIT,
						 SC223A_FETCH_EXP_M(val));
			ret |= sc223a_write_reg(sc223a->client,
						 SC223A_REG_EXPOSURE_L,
						 SC223A_REG_VALUE_08BIT,
						 SC223A_FETCH_EXP_L(val));
		}
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		dev_dbg(&client->dev, "set gain 0x%x\n", ctrl->val);
		if (sc223a->cur_mode->hdr_mode == NO_HDR)
			ret = sc223a_set_gain_reg(sc223a, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		dev_dbg(&client->dev, "set vblank 0x%x\n", ctrl->val);
		ret = sc223a_write_reg(sc223a->client,
					SC223A_REG_VTS_H,
					SC223A_REG_VALUE_08BIT,
					(ctrl->val + sc223a->cur_mode->height)
					>> 8);
		ret |= sc223a_write_reg(sc223a->client,
					 SC223A_REG_VTS_L,
					 SC223A_REG_VALUE_08BIT,
					 (ctrl->val + sc223a->cur_mode->height)
					 & 0xff);
		sc223a->cur_vts = ctrl->val + sc223a->cur_mode->height;
		sc223a_modify_fps_info(sc223a);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = sc223a_enable_test_pattern(sc223a, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = sc223a_read_reg(sc223a->client, SC223A_FLIP_MIRROR_REG,
				       SC223A_REG_VALUE_08BIT, &val);
		ret |= sc223a_write_reg(sc223a->client, SC223A_FLIP_MIRROR_REG,
					 SC223A_REG_VALUE_08BIT,
					 SC223A_FETCH_MIRROR(val, ctrl->val));
		break;
	case V4L2_CID_VFLIP:
		ret = sc223a_read_reg(sc223a->client, SC223A_FLIP_MIRROR_REG,
				       SC223A_REG_VALUE_08BIT, &val);
		ret |= sc223a_write_reg(sc223a->client, SC223A_FLIP_MIRROR_REG,
					 SC223A_REG_VALUE_08BIT,
					 SC223A_FETCH_FLIP(val, ctrl->val));
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc223a_ctrl_ops = {
	.s_ctrl = sc223a_set_ctrl,
};

static int sc223a_initialize_controls(struct sc223a *sc223a)
{
	const struct sc223a_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &sc223a->ctrl_handler;
	mode = sc223a->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &sc223a->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, PIXEL_RATE_WITH_405M_10BIT, 1, PIXEL_RATE_WITH_405M_10BIT);

	h_blank = mode->hts_def - mode->width;
	sc223a->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (sc223a->hblank)
		sc223a->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	vblank_def = mode->vts_def - mode->height;
	sc223a->vblank = v4l2_ctrl_new_std(handler, &sc223a_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    SC223A_VTS_MAX - mode->height,
					    1, vblank_def);
	exposure_max = mode->vts_def - 10;
	sc223a->exposure = v4l2_ctrl_new_std(handler, &sc223a_ctrl_ops,
					      V4L2_CID_EXPOSURE, SC223A_EXPOSURE_MIN,
					      exposure_max, SC223A_EXPOSURE_STEP,
					      mode->exp_def);
	sc223a->anal_gain = v4l2_ctrl_new_std(handler, &sc223a_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN, SC223A_GAIN_MIN,
					       SC223A_GAIN_MAX, SC223A_GAIN_STEP,
					       SC223A_GAIN_DEFAULT);
	sc223a->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
							    &sc223a_ctrl_ops,
					V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(sc223a_test_pattern_menu) - 1,
					0, 0, sc223a_test_pattern_menu);
	v4l2_ctrl_new_std(handler, &sc223a_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &sc223a_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (handler->error) {
		ret = handler->error;
		dev_err(&sc223a->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	sc223a->subdev.ctrl_handler = handler;
	sc223a->has_init_exp = false;
	sc223a->cur_fps = mode->max_fps;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int sc223a_check_sensor_id(struct sc223a *sc223a,
				   struct i2c_client *client)
{
	struct device *dev = &sc223a->client->dev;
	u32 id = 0;
	int ret;

	if (sc223a->is_thunderboot) {
		dev_info(dev, "Enable thunderboot mode, skip sensor id check\n");
		return 0;
	}

	ret = sc223a_read_reg(client, SC223A_REG_CHIP_ID,
			       SC223A_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int sc223a_configure_regulators(struct sc223a *sc223a)
{
	unsigned int i;

	for (i = 0; i < SC223A_NUM_SUPPLIES; i++)
		sc223a->supplies[i].supply = sc223a_supply_names[i];

	return devm_regulator_bulk_get(&sc223a->client->dev,
				       SC223A_NUM_SUPPLIES,
				       sc223a->supplies);
}

static int sc223a_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc223a *sc223a;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	int i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	sc223a = devm_kzalloc(dev, sizeof(*sc223a), GFP_KERNEL);
	if (!sc223a)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc223a->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc223a->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc223a->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc223a->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	sc223a->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);

	sc223a->client = client;
	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			sc223a->cur_mode = &supported_modes[i];
			break;
		}
	}
	if (i == ARRAY_SIZE(supported_modes))
		sc223a->cur_mode = &supported_modes[0];

	sc223a->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc223a->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	if (sc223a->is_thunderboot) {
		sc223a->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
		if (IS_ERR(sc223a->reset_gpio))
			dev_warn(dev, "Failed to get reset-gpios\n");

		sc223a->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_ASIS);
		if (IS_ERR(sc223a->pwdn_gpio))
			dev_warn(dev, "Failed to get pwdn-gpios\n");
	} else {
		sc223a->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
		if (IS_ERR(sc223a->reset_gpio))
			dev_warn(dev, "Failed to get reset-gpios\n");

		sc223a->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
		if (IS_ERR(sc223a->pwdn_gpio))
			dev_warn(dev, "Failed to get pwdn-gpios\n");
	}

	sc223a->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc223a->pinctrl)) {
		sc223a->pins_default =
			pinctrl_lookup_state(sc223a->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc223a->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		sc223a->pins_sleep =
			pinctrl_lookup_state(sc223a->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc223a->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = sc223a_configure_regulators(sc223a);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&sc223a->mutex);

	sd = &sc223a->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc223a_subdev_ops);
	ret = sc223a_initialize_controls(sc223a);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc223a_power_on(sc223a);
	if (ret)
		goto err_free_handler;

	ret = sc223a_check_sensor_id(sc223a, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc223a_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc223a->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc223a->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc223a->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sc223a->module_index, facing,
		 SC223A_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	if (sc223a->is_thunderboot)
		pm_runtime_get_sync(dev);
	else
		pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__sc223a_power_off(sc223a);
err_free_handler:
	v4l2_ctrl_handler_free(&sc223a->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc223a->mutex);

	return ret;
}

static int sc223a_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc223a *sc223a = to_sc223a(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc223a->ctrl_handler);
	mutex_destroy(&sc223a->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc223a_power_off(sc223a);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc223a_of_match[] = {
	{ .compatible = "smartsens,sc223a" },
	{},
};
MODULE_DEVICE_TABLE(of, sc223a_of_match);
#endif

static const struct i2c_device_id sc223a_match_id[] = {
	{ "smartsens,sc223a", 0 },
	{ },
};

static struct i2c_driver sc223a_i2c_driver = {
	.driver = {
		.name = SC223A_NAME,
		.pm = &sc223a_pm_ops,
		.of_match_table = of_match_ptr(sc223a_of_match),
	},
	.probe		= &sc223a_probe,
	.remove		= &sc223a_remove,
	.id_table	= sc223a_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc223a_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc223a_i2c_driver);
}

#if defined(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP) && !defined(CONFIG_INITCALL_ASYNC)
subsys_initcall(sensor_mod_init);
#else
device_initcall_sync(sensor_mod_init);
#endif
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("smartsens sc223a sensor driver");
MODULE_LICENSE("GPL");
