// SPDX-License-Identifier: GPL-2.0
/*
 * sc3336 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x01)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define SC3336_LANES			2
#define SC3336_BITS_PER_SAMPLE		10
#define SC3336_LINK_FREQ_255		255000000

#define PIXEL_RATE_WITH_255M_10BIT	(SC3336_LINK_FREQ_255 * 2 * \
					SC3336_LANES / SC3336_BITS_PER_SAMPLE)
#define SC3336_XVCLK_FREQ		24000000

#define CHIP_ID				0xcc41
#define SC3336_REG_CHIP_ID		0x3107

#define SC3336_REG_CTRL_MODE		0x0100
#define SC3336_MODE_SW_STANDBY		0x0
#define SC3336_MODE_STREAMING		BIT(0)

#define SC3336_REG_EXPOSURE_H		0x3e00
#define SC3336_REG_EXPOSURE_M		0x3e01
#define SC3336_REG_EXPOSURE_L		0x3e02
#define	SC3336_EXPOSURE_MIN		1
#define	SC3336_EXPOSURE_STEP		1
#define SC3336_VTS_MAX			0x7fff

#define SC3336_REG_DIG_GAIN		0x3e06
#define SC3336_REG_DIG_FINE_GAIN	0x3e07
#define SC3336_REG_ANA_GAIN		0x3e09
#define SC3336_GAIN_MIN			0x0080
#define SC3336_GAIN_MAX			(99614)	//48.64*16*128
#define SC3336_GAIN_STEP		1
#define SC3336_GAIN_DEFAULT		0x80


#define SC3336_REG_GROUP_HOLD		0x3812
#define SC3336_GROUP_HOLD_START		0x00
#define SC3336_GROUP_HOLD_END		0x30

#define SC3336_REG_TEST_PATTERN		0x4501
#define SC3336_TEST_PATTERN_BIT_MASK	BIT(3)

#define SC3336_REG_VTS_H		0x320e
#define SC3336_REG_VTS_L		0x320f

#define SC3336_FLIP_MIRROR_REG		0x3221

#define SC3336_FETCH_EXP_H(VAL)		(((VAL) >> 12) & 0xF)
#define SC3336_FETCH_EXP_M(VAL)		(((VAL) >> 4) & 0xFF)
#define SC3336_FETCH_EXP_L(VAL)		(((VAL) & 0xF) << 4)

#define SC3336_FETCH_AGAIN_H(VAL)	(((VAL) >> 8) & 0x03)
#define SC3336_FETCH_AGAIN_L(VAL)	((VAL) & 0xFF)

#define SC3336_FETCH_MIRROR(VAL, ENABLE)	(ENABLE ? VAL | 0x06 : VAL & 0xf9)
#define SC3336_FETCH_FLIP(VAL, ENABLE)		(ENABLE ? VAL | 0x60 : VAL & 0x9f)

#define REG_DELAY			0xFFFE
#define REG_NULL			0xFFFF

#define SC3336_REG_VALUE_08BIT		1
#define SC3336_REG_VALUE_16BIT		2
#define SC3336_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define SC3336_NAME			"sc3336"

static const char * const sc3336_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define SC3336_NUM_SUPPLIES ARRAY_SIZE(sc3336_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct sc3336_mode {
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

struct sc3336 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[SC3336_NUM_SUPPLIES];

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
	const struct sc3336_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
};

#define to_sc3336(sd) container_of(sd, struct sc3336, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval sc3336_global_regs[] = {
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 510Mbps, 2lane
 */
static const struct regval sc3336_linear_10_2304x1296_regs[] = {
	{0x0103, 0x01},
	{0x36e9, 0x80},
	{0x37f9, 0x80},
	{0x301f, 0x02},
	{0x30b8, 0x33},
	{0x320e, 0x05},
	{0x320f, 0x50},
	{0x3253, 0x10},
	{0x325f, 0x20},
	{0x3301, 0x04},
	{0x3306, 0x50},
	{0x330a, 0x00},
	{0x330b, 0xd8},
	{0x3314, 0x13},
	{0x3333, 0x10},
	{0x3334, 0x40},
	{0x335e, 0x06},
	{0x335f, 0x0a},
	{0x3364, 0x5e},
	{0x337c, 0x02},
	{0x337d, 0x0e},
	{0x3390, 0x01},
	{0x3391, 0x03},
	{0x3392, 0x07},
	{0x3393, 0x04},
	{0x3394, 0x04},
	{0x3395, 0x04},
	{0x3396, 0x08},
	{0x3397, 0x0b},
	{0x3398, 0x1f},
	{0x3399, 0x04},
	{0x339a, 0x0a},
	{0x339b, 0x3a},
	{0x339c, 0xc4},
	{0x33a2, 0x04},
	{0x33ac, 0x08},
	{0x33ad, 0x1c},
	{0x33ae, 0x10},
	{0x33af, 0x30},
	{0x33b1, 0x80},
	{0x33b3, 0x48},
	{0x33f9, 0x60},
	{0x33fb, 0x74},
	{0x33fc, 0x4b},
	{0x33fd, 0x5f},
	{0x349f, 0x03},
	{0x34a6, 0x4b},
	{0x34a7, 0x5f},
	{0x34a8, 0x20},
	{0x34a9, 0x18},
	{0x34ab, 0xe8},
	{0x34ac, 0x01},
	{0x34ad, 0x00},
	{0x34f8, 0x5f},
	{0x34f9, 0x18},
	{0x3630, 0xc0},
	{0x3631, 0x84},
	{0x3632, 0x64},
	{0x3633, 0x32},
	{0x363b, 0x03},
	{0x363c, 0x08},
	{0x3641, 0x38},
	{0x3670, 0x4e},
	{0x3674, 0xc0},
	{0x3675, 0xc0},
	{0x3676, 0xc0},
	{0x3677, 0x84},
	{0x3678, 0x8a},
	{0x3679, 0x8c},
	{0x367c, 0x48},
	{0x367d, 0x49},
	{0x367e, 0x4b},
	{0x367f, 0x5f},
	{0x3690, 0x33},
	{0x3691, 0x33},
	{0x3692, 0x44},
	{0x369c, 0x4b},
	{0x369d, 0x5f},
	{0x36b0, 0x87},
	{0x36b1, 0x90},
	{0x36b2, 0xa1},
	{0x36b3, 0xd8},
	{0x36b4, 0x49},
	{0x36b5, 0x4b},
	{0x36b6, 0x4f},
	{0x36ea, 0x11},
	{0x36eb, 0x0d},
	{0x36ec, 0x1c},
	{0x36ed, 0x26},
	{0x370f, 0x01},
	{0x3722, 0x09},
	{0x3724, 0x41},
	{0x3725, 0xc1},
	{0x3771, 0x09},
	{0x3772, 0x09},
	{0x3773, 0x05},
	{0x377a, 0x48},
	{0x377b, 0x5f},
	{0x37fa, 0x11},
	{0x37fb, 0x33},
	{0x37fc, 0x11},
	{0x37fd, 0x08},
	{0x3904, 0x04},
	{0x3905, 0x8c},
	{0x391d, 0x04},
	{0x3921, 0x20},
	{0x3926, 0x21},
	{0x3933, 0x80},
	{0x3934, 0x0a},
	{0x3935, 0x00},
	{0x3936, 0x2a},
	{0x3937, 0x6a},
	{0x3938, 0x6a},
	{0x39dc, 0x02},
	{0x3e01, 0x54},
	{0x3e02, 0x80},
	{0x3e09, 0x00},
	{0x440e, 0x02},
	{0x4509, 0x20},
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
	{0x36e9, 0x54},
	{0x37f9, 0x47},
	{REG_NULL, 0x00},
};

static const struct sc3336_mode supported_modes[] = {
	{
		.width = 2304,
		.height = 1296,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0080,
		.hts_def = 0x0578 * 2,
		.vts_def = 0x0550,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc3336_linear_10_2304x1296_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}
};

static const s64 link_freq_menu_items[] = {
	SC3336_LINK_FREQ_255
};

static const char * const sc3336_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int sc3336_write_reg(struct i2c_client *client, u16 reg,
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

static int sc3336_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = sc3336_write_reg(client, regs[i].addr,
					SC3336_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int sc3336_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static int sc3336_set_gain_reg(struct sc3336 *sc3336, u32 gain)
{
	u32 coarse_again = 0, coarse_dgian = 0, fine_dgian = 0;
	u32 gain_factor;
	int ret = 0;

	if (gain < 128)
		gain = 128;
	else if (gain > SC3336_GAIN_MAX)
		gain = SC3336_GAIN_MAX;

	gain_factor = gain * 1000 / 128;
	if (gain_factor < 1520) {
		coarse_again = 0x00;
		coarse_dgian = 0x00;
		fine_dgian = gain_factor * 128 / 1000;
	} else if (gain_factor < 3040) {
		coarse_again = 0x40;
		coarse_dgian = 0x00;
		fine_dgian = gain_factor * 128 / 1520;
	} else if (gain_factor < 6080) {
		coarse_again = 0x48;
		coarse_dgian = 0x00;
		fine_dgian = gain_factor * 128 / 3040;
	} else if (gain_factor < 12160) {
		coarse_again = 0x49;
		coarse_dgian = 0x00;
		fine_dgian = gain_factor * 128 / 6080;
	} else if (gain_factor < 24320) {
		coarse_again = 0x4b;
		coarse_dgian = 0x00;
		fine_dgian = gain_factor * 128 / 12160;
	} else if (gain_factor < 48640) {
		coarse_again = 0x4f;
		coarse_dgian = 0x00;
		fine_dgian = gain_factor * 128 / 24320;
	} else if (gain_factor < 48640 * 2) {
		//open dgain begin  max digital gain 4X
		coarse_again = 0x5f;
		coarse_dgian = 0x00;
		fine_dgian = gain_factor * 128 / 48640;
	} else if (gain_factor < 48640 * 4) {
		coarse_again = 0x5f;
		coarse_dgian = 0x01;
		fine_dgian = gain_factor * 128 / 48640 / 2;
	} else if (gain_factor < 48640 * 8) {
		coarse_again = 0x5f;
		coarse_dgian = 0x03;
		fine_dgian = gain_factor * 128 / 48640 / 4;
	} else if (gain_factor < 48640 * 16) {
		coarse_again = 0x5f;
		coarse_dgian = 0x07;
		fine_dgian = gain_factor * 128 / 48640 / 8;
	}

	ret = sc3336_write_reg(sc3336->client,
				SC3336_REG_DIG_GAIN,
				SC3336_REG_VALUE_08BIT,
				coarse_dgian);
	ret |= sc3336_write_reg(sc3336->client,
				 SC3336_REG_DIG_FINE_GAIN,
				 SC3336_REG_VALUE_08BIT,
				 fine_dgian);
	ret |= sc3336_write_reg(sc3336->client,
				 SC3336_REG_ANA_GAIN,
				 SC3336_REG_VALUE_08BIT,
				 coarse_again);

	return ret;
}

static int sc3336_get_reso_dist(const struct sc3336_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc3336_mode *
sc3336_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = sc3336_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int sc3336_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc3336 *sc3336 = to_sc3336(sd);
	const struct sc3336_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&sc3336->mutex);

	mode = sc3336_find_best_fit(fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc3336->mutex);
		return -ENOTTY;
#endif
	} else {
		sc3336->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc3336->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc3336->vblank, vblank_def,
					 SC3336_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&sc3336->mutex);

	return 0;
}

static int sc3336_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc3336 *sc3336 = to_sc3336(sd);
	const struct sc3336_mode *mode = sc3336->cur_mode;

	mutex_lock(&sc3336->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc3336->mutex);
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
	mutex_unlock(&sc3336->mutex);

	return 0;
}

static int sc3336_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct sc3336 *sc3336 = to_sc3336(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = sc3336->cur_mode->bus_fmt;

	return 0;
}

static int sc3336_enum_frame_sizes(struct v4l2_subdev *sd,
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

static int sc3336_enable_test_pattern(struct sc3336 *sc3336, u32 pattern)
{
	u32 val = 0;
	int ret = 0;

	ret = sc3336_read_reg(sc3336->client, SC3336_REG_TEST_PATTERN,
			       SC3336_REG_VALUE_08BIT, &val);
	if (pattern)
		val |= SC3336_TEST_PATTERN_BIT_MASK;
	else
		val &= ~SC3336_TEST_PATTERN_BIT_MASK;

	ret |= sc3336_write_reg(sc3336->client, SC3336_REG_TEST_PATTERN,
				 SC3336_REG_VALUE_08BIT, val);
	return ret;
}

static int sc3336_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct sc3336 *sc3336 = to_sc3336(sd);
	const struct sc3336_mode *mode = sc3336->cur_mode;

	mutex_lock(&sc3336->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&sc3336->mutex);

	return 0;
}

static int sc3336_g_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct sc3336 *sc3336 = to_sc3336(sd);
	const struct sc3336_mode *mode = sc3336->cur_mode;
	u32 val = 1 << (SC3336_LANES - 1) |
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

static void sc3336_get_module_inf(struct sc3336 *sc3336,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, SC3336_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, sc3336->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, sc3336->len_name, sizeof(inf->base.lens));
}

static long sc3336_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc3336 *sc3336 = to_sc3336(sd);
	struct rkmodule_hdr_cfg *hdr;
	u32 i, h, w;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		sc3336_get_module_inf(sc3336, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = sc3336->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = sc3336->cur_mode->width;
		h = sc3336->cur_mode->height;
		for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				sc3336->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == ARRAY_SIZE(supported_modes)) {
			dev_err(&sc3336->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = sc3336->cur_mode->hts_def - sc3336->cur_mode->width;
			h = sc3336->cur_mode->vts_def - sc3336->cur_mode->height;
			__v4l2_ctrl_modify_range(sc3336->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(sc3336->vblank, h,
						 SC3336_VTS_MAX - sc3336->cur_mode->height, 1, h);
		}
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = sc3336_write_reg(sc3336->client, SC3336_REG_CTRL_MODE,
				 SC3336_REG_VALUE_08BIT, SC3336_MODE_STREAMING);
		else
			ret = sc3336_write_reg(sc3336->client, SC3336_REG_CTRL_MODE,
				 SC3336_REG_VALUE_08BIT, SC3336_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc3336_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = sc3336_ioctl(sd, cmd, inf);
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

		ret = sc3336_ioctl(sd, cmd, hdr);
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
			ret = sc3336_ioctl(sd, cmd, hdr);
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
			ret = sc3336_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = sc3336_ioctl(sd, cmd, &stream);
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

static int __sc3336_start_stream(struct sc3336 *sc3336)
{
	int ret;

	ret = sc3336_write_array(sc3336->client, sc3336->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&sc3336->ctrl_handler);
	if (ret)
		return ret;

	return sc3336_write_reg(sc3336->client, SC3336_REG_CTRL_MODE,
				 SC3336_REG_VALUE_08BIT, SC3336_MODE_STREAMING);
}

static int __sc3336_stop_stream(struct sc3336 *sc3336)
{
	return sc3336_write_reg(sc3336->client, SC3336_REG_CTRL_MODE,
				 SC3336_REG_VALUE_08BIT, SC3336_MODE_SW_STANDBY);
}

static int sc3336_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc3336 *sc3336 = to_sc3336(sd);
	struct i2c_client *client = sc3336->client;
	int ret = 0;

	mutex_lock(&sc3336->mutex);
	on = !!on;
	if (on == sc3336->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __sc3336_start_stream(sc3336);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc3336_stop_stream(sc3336);
		pm_runtime_put(&client->dev);
	}

	sc3336->streaming = on;

unlock_and_return:
	mutex_unlock(&sc3336->mutex);

	return ret;
}

static int sc3336_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc3336 *sc3336 = to_sc3336(sd);
	struct i2c_client *client = sc3336->client;
	int ret = 0;

	mutex_lock(&sc3336->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc3336->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = sc3336_write_array(sc3336->client, sc3336_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		sc3336->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc3336->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc3336->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 sc3336_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, SC3336_XVCLK_FREQ / 1000 / 1000);
}

static int __sc3336_power_on(struct sc3336 *sc3336)
{
	int ret;
	u32 delay_us;
	struct device *dev = &sc3336->client->dev;

	if (!IS_ERR_OR_NULL(sc3336->pins_default)) {
		ret = pinctrl_select_state(sc3336->pinctrl,
					   sc3336->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(sc3336->xvclk, SC3336_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(sc3336->xvclk) != SC3336_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc3336->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(sc3336->reset_gpio))
		gpiod_set_value_cansleep(sc3336->reset_gpio, 0);

	ret = regulator_bulk_enable(SC3336_NUM_SUPPLIES, sc3336->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(sc3336->reset_gpio))
		gpiod_set_value_cansleep(sc3336->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(sc3336->pwdn_gpio))
		gpiod_set_value_cansleep(sc3336->pwdn_gpio, 1);

	if (!IS_ERR(sc3336->reset_gpio))
		usleep_range(6000, 8000);
	else
		usleep_range(12000, 16000);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = sc3336_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(sc3336->xvclk);

	return ret;
}

static void __sc3336_power_off(struct sc3336 *sc3336)
{
	int ret;
	struct device *dev = &sc3336->client->dev;

	if (!IS_ERR(sc3336->pwdn_gpio))
		gpiod_set_value_cansleep(sc3336->pwdn_gpio, 0);
	clk_disable_unprepare(sc3336->xvclk);
	if (!IS_ERR(sc3336->reset_gpio))
		gpiod_set_value_cansleep(sc3336->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(sc3336->pins_sleep)) {
		ret = pinctrl_select_state(sc3336->pinctrl,
					   sc3336->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(SC3336_NUM_SUPPLIES, sc3336->supplies);
}

static int sc3336_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc3336 *sc3336 = to_sc3336(sd);

	return __sc3336_power_on(sc3336);
}

static int sc3336_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc3336 *sc3336 = to_sc3336(sd);

	__sc3336_power_off(sc3336);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc3336_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc3336 *sc3336 = to_sc3336(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc3336_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc3336->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc3336->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sc3336_enum_frame_interval(struct v4l2_subdev *sd,
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

static const struct dev_pm_ops sc3336_pm_ops = {
	SET_RUNTIME_PM_OPS(sc3336_runtime_suspend,
			   sc3336_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc3336_internal_ops = {
	.open = sc3336_open,
};
#endif

static const struct v4l2_subdev_core_ops sc3336_core_ops = {
	.s_power = sc3336_s_power,
	.ioctl = sc3336_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc3336_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc3336_video_ops = {
	.s_stream = sc3336_s_stream,
	.g_frame_interval = sc3336_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops sc3336_pad_ops = {
	.enum_mbus_code = sc3336_enum_mbus_code,
	.enum_frame_size = sc3336_enum_frame_sizes,
	.enum_frame_interval = sc3336_enum_frame_interval,
	.get_fmt = sc3336_get_fmt,
	.set_fmt = sc3336_set_fmt,
	.get_mbus_config = sc3336_g_mbus_config,
};

static const struct v4l2_subdev_ops sc3336_subdev_ops = {
	.core	= &sc3336_core_ops,
	.video	= &sc3336_video_ops,
	.pad	= &sc3336_pad_ops,
};

static int sc3336_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc3336 *sc3336 = container_of(ctrl->handler,
					       struct sc3336, ctrl_handler);
	struct i2c_client *client = sc3336->client;
	s64 max;
	int ret = 0;
	u32 val = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc3336->cur_mode->height + ctrl->val - 8;
		__v4l2_ctrl_modify_range(sc3336->exposure,
					 sc3336->exposure->minimum, max,
					 sc3336->exposure->step,
					 sc3336->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dev_dbg(&client->dev, "set exposure 0x%x\n", ctrl->val);
		if (sc3336->cur_mode->hdr_mode == NO_HDR) {
			val = ctrl->val;
			/* 4 least significant bits of expsoure are fractional part */
			ret = sc3336_write_reg(sc3336->client,
						SC3336_REG_EXPOSURE_H,
						SC3336_REG_VALUE_08BIT,
						SC3336_FETCH_EXP_H(val));
			ret |= sc3336_write_reg(sc3336->client,
						 SC3336_REG_EXPOSURE_M,
						 SC3336_REG_VALUE_08BIT,
						 SC3336_FETCH_EXP_M(val));
			ret |= sc3336_write_reg(sc3336->client,
						 SC3336_REG_EXPOSURE_L,
						 SC3336_REG_VALUE_08BIT,
						 SC3336_FETCH_EXP_L(val));
		}
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		dev_dbg(&client->dev, "set gain 0x%x\n", ctrl->val);
		if (sc3336->cur_mode->hdr_mode == NO_HDR)
			ret = sc3336_set_gain_reg(sc3336, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		dev_dbg(&client->dev, "set vblank 0x%x\n", ctrl->val);
		ret = sc3336_write_reg(sc3336->client,
					SC3336_REG_VTS_H,
					SC3336_REG_VALUE_08BIT,
					(ctrl->val + sc3336->cur_mode->height)
					>> 8);
		ret |= sc3336_write_reg(sc3336->client,
					 SC3336_REG_VTS_L,
					 SC3336_REG_VALUE_08BIT,
					 (ctrl->val + sc3336->cur_mode->height)
					 & 0xff);
		sc3336->cur_vts = ctrl->val + sc3336->cur_mode->height;
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = sc3336_enable_test_pattern(sc3336, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = sc3336_read_reg(sc3336->client, SC3336_FLIP_MIRROR_REG,
				       SC3336_REG_VALUE_08BIT, &val);
		ret |= sc3336_write_reg(sc3336->client, SC3336_FLIP_MIRROR_REG,
					 SC3336_REG_VALUE_08BIT,
					 SC3336_FETCH_MIRROR(val, ctrl->val));
		break;
	case V4L2_CID_VFLIP:
		ret = sc3336_read_reg(sc3336->client, SC3336_FLIP_MIRROR_REG,
				       SC3336_REG_VALUE_08BIT, &val);
		ret |= sc3336_write_reg(sc3336->client, SC3336_FLIP_MIRROR_REG,
					 SC3336_REG_VALUE_08BIT,
					 SC3336_FETCH_FLIP(val, ctrl->val));
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc3336_ctrl_ops = {
	.s_ctrl = sc3336_set_ctrl,
};

static int sc3336_initialize_controls(struct sc3336 *sc3336)
{
	const struct sc3336_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &sc3336->ctrl_handler;
	mode = sc3336->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &sc3336->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, PIXEL_RATE_WITH_255M_10BIT, 1, PIXEL_RATE_WITH_255M_10BIT);

	h_blank = mode->hts_def - mode->width;
	sc3336->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (sc3336->hblank)
		sc3336->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	vblank_def = mode->vts_def - mode->height;
	sc3336->vblank = v4l2_ctrl_new_std(handler, &sc3336_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    SC3336_VTS_MAX - mode->height,
					    1, vblank_def);
	exposure_max = mode->vts_def - 8;
	sc3336->exposure = v4l2_ctrl_new_std(handler, &sc3336_ctrl_ops,
					      V4L2_CID_EXPOSURE, SC3336_EXPOSURE_MIN,
					      exposure_max, SC3336_EXPOSURE_STEP,
					      mode->exp_def);
	sc3336->anal_gain = v4l2_ctrl_new_std(handler, &sc3336_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN, SC3336_GAIN_MIN,
					       SC3336_GAIN_MAX, SC3336_GAIN_STEP,
					       SC3336_GAIN_DEFAULT);
	sc3336->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
							    &sc3336_ctrl_ops,
					V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(sc3336_test_pattern_menu) - 1,
					0, 0, sc3336_test_pattern_menu);
	v4l2_ctrl_new_std(handler, &sc3336_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &sc3336_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (handler->error) {
		ret = handler->error;
		dev_err(&sc3336->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	sc3336->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int sc3336_check_sensor_id(struct sc3336 *sc3336,
				   struct i2c_client *client)
{
	struct device *dev = &sc3336->client->dev;
	u32 id = 0;
	int ret;

	ret = sc3336_read_reg(client, SC3336_REG_CHIP_ID,
			       SC3336_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int sc3336_configure_regulators(struct sc3336 *sc3336)
{
	unsigned int i;

	for (i = 0; i < SC3336_NUM_SUPPLIES; i++)
		sc3336->supplies[i].supply = sc3336_supply_names[i];

	return devm_regulator_bulk_get(&sc3336->client->dev,
				       SC3336_NUM_SUPPLIES,
				       sc3336->supplies);
}

static int sc3336_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc3336 *sc3336;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	sc3336 = devm_kzalloc(dev, sizeof(*sc3336), GFP_KERNEL);
	if (!sc3336)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc3336->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc3336->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc3336->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc3336->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	sc3336->client = client;
	sc3336->cur_mode = &supported_modes[0];

	sc3336->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc3336->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	sc3336->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sc3336->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	sc3336->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(sc3336->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	sc3336->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc3336->pinctrl)) {
		sc3336->pins_default =
			pinctrl_lookup_state(sc3336->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc3336->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		sc3336->pins_sleep =
			pinctrl_lookup_state(sc3336->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc3336->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = sc3336_configure_regulators(sc3336);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&sc3336->mutex);

	sd = &sc3336->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc3336_subdev_ops);
	ret = sc3336_initialize_controls(sc3336);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc3336_power_on(sc3336);
	if (ret)
		goto err_free_handler;

	ret = sc3336_check_sensor_id(sc3336, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc3336_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc3336->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc3336->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc3336->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sc3336->module_index, facing,
		 SC3336_NAME, dev_name(sd->dev));
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
	__sc3336_power_off(sc3336);
err_free_handler:
	v4l2_ctrl_handler_free(&sc3336->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc3336->mutex);

	return ret;
}

static int sc3336_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc3336 *sc3336 = to_sc3336(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc3336->ctrl_handler);
	mutex_destroy(&sc3336->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc3336_power_off(sc3336);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc3336_of_match[] = {
	{ .compatible = "smartsens,sc3336" },
	{},
};
MODULE_DEVICE_TABLE(of, sc3336_of_match);
#endif

static const struct i2c_device_id sc3336_match_id[] = {
	{ "smartsens,sc3336", 0 },
	{ },
};

static struct i2c_driver sc3336_i2c_driver = {
	.driver = {
		.name = SC3336_NAME,
		.pm = &sc3336_pm_ops,
		.of_match_table = of_match_ptr(sc3336_of_match),
	},
	.probe		= &sc3336_probe,
	.remove		= &sc3336_remove,
	.id_table	= sc3336_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc3336_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc3336_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("smartsens sc3336 sensor driver");
MODULE_LICENSE("GPL v2");
