// SPDX-License-Identifier: GPL-2.0
/*
 * sc500ai driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 fix set vflip/hflip failed bug.
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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x01)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define SC500AI_LANES			4

#define SC500AI_LINK_FREQ_198M		198000000 // 396Mbps
#define SC500AI_LINK_FREQ_405M		405000000 // 810Mbps

#define SC500AI_MAX_PIXEL_RATE		(SC500AI_LINK_FREQ_405M / 10 * 2 * SC500AI_LANES)

#define SC500AI_XVCLK_FREQ		24000000

#define SC500AI_CHIP_ID 		0xce1f
#define SC500AI_REG_CHIP_ID		0x3107

#define SC500AI_REG_CTRL_MODE		0x0100
#define SC500AI_MODE_SW_STANDBY		0x0
#define SC500AI_MODE_STREAMING		BIT(0)

#define SC500AI_REG_EXPOSURE_H		0x3e00
#define SC500AI_REG_EXPOSURE_M		0x3e01
#define SC500AI_REG_EXPOSURE_L		0x3e02

#define	SC500AI_EXPOSURE_MIN		2
#define	sc500ai_EXPOSURE_STEP		1

#define SC500AI_REG_DIG_GAIN		0x3e06
#define SC500AI_REG_DIG_FINE_GAIN	0x3e07
#define SC500AI_REG_ANA_GAIN		0x3e08
#define SC500AI_REG_ANA_FINE_GAIN	0x3e09

#define SC500AI_GAIN_MIN		0x40
#define SC500AI_GAIN_MAX		0xc000
#define SC500AI_GAIN_STEP		1
#define SC500AI_GAIN_DEFAULT		0x40

#define SC500AI_REG_VTS_H		0x320e
#define SC500AI_REG_VTS_L		0x320f
#define SC500AI_VTS_MAX			0x7fff

#define SC500AI_SOFTWARE_RESET_REG	0x0103

// short frame exposure
#define SC500AI_REG_SHORT_EXPOSURE_H	0x3e22
#define SC500AI_REG_SHORT_EXPOSURE_M	0x3e04
#define SC500AI_REG_SHORT_EXPOSURE_L	0x3e05
#define SC500AI_REG_MAX_SHORT_EXP_H	0x3e23
#define SC500AI_REG_MAX_SHORT_EXP_L	0x3e24

#define SC500AI_HDR_EXPOSURE_MIN	5		// Half line exposure time
#define SC500AI_HDR_EXPOSURE_STEP	4		// Half line exposure time

#define SC500AI_MAX_SHORT_EXPOSURE	608

// short frame gain
#define SC500AI_REG_SDIG_GAIN		0x3e10
#define SC500AI_REG_SDIG_FINE_GAIN	0x3e11
#define SC500AI_REG_SANA_GAIN		0x3e12
#define SC500AI_REG_SANA_FINE_GAIN	0x3e13

//group hold
#define SC500AI_GROUP_UPDATE_ADDRESS	0x3812
#define SC500AI_GROUP_UPDATE_START_DATA	0x00
#define SC500AI_GROUP_UPDATE_LAUNCH	0x30

#define SC500AI_FLIP_MIRROR_REG		0x3221
#define SC500AI_FLIP_MASK		0x60
#define SC500AI_MIRROR_MASK		0x06

#define REG_NULL			0xFFFF

#define SC500AI_REG_VALUE_08BIT		1
#define SC500AI_REG_VALUE_16BIT		2
#define SC500AI_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define SC500AI_NAME			"sc500ai"

#define SC500AI_FETCH_EXP_H(VAL)		(((VAL) >> 12) & 0xF)
#define SC500AI_FETCH_EXP_M(VAL)		(((VAL) >> 4) & 0xFF)
#define SC500AI_FETCH_EXP_L(VAL)		(((VAL) & 0xF) << 4)

static const char * const sc500ai_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define sc500ai_NUM_SUPPLIES ARRAY_SIZE(sc500ai_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct sc500ai_mode {
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
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct sc500ai {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[sc500ai_NUM_SUPPLIES];

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
	struct v4l2_fract	cur_fps;
	bool			streaming;
	bool			power_on;
	const struct sc500ai_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	u32			cur_vts;
};

#define to_sc500ai(sd) container_of(sd, struct sc500ai, subdev)

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 1008Mbps, 4lane
 */
static const struct regval sc500ai_linear_10_2880x1620_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x301f, 0x3c},
	{0x3250, 0x40},
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
	{0x36ea, 0x35},
	{0x36eb, 0x0c},
	{0x36ec, 0x1a},
	{0x36ed, 0x34},
	{0x36fa, 0x35},
	{0x36fb, 0x35},
	{0x36fc, 0x10},
	{0x36fd, 0x34},
	{0x3904, 0x04},
	{0x3908, 0x41},
	{0x391d, 0x04},
	{0x39c2, 0x30},
	{0x3e01, 0xcd},
	{0x3e02, 0xa0},
	{0x3e16, 0x00},
	{0x3e17, 0x80},
	{0x4500, 0x88},
	{0x4509, 0x20},
	{0x4800, 0x04},
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
	{0x36e9, 0x20},
	{0x36f9, 0x53},
	{REG_NULL, 0x00},
};

static const struct regval sc500ai_hdr_10_2880x1620_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x301f, 0x3b},
	{0x3106, 0x01},
	{0x320e, 0x0d},
	{0x320f, 0x30},
	{0x3220, 0x53},
	{0x3250, 0xff},
	{0x3253, 0x0a},
	{0x3301, 0x0b},
	{0x3302, 0x20},
	{0x3303, 0x10},
	{0x3304, 0x70},
	{0x3306, 0x50},
	{0x3308, 0x18},
	{0x3309, 0x80},
	{0x330a, 0x00},
	{0x330b, 0xe8},
	{0x330d, 0x30},
	{0x330e, 0x30},
	{0x330f, 0x02},
	{0x3310, 0x02},
	{0x331c, 0x08},
	{0x331e, 0x61},
	{0x331f, 0x71},
	{0x3320, 0x11},
	{0x3333, 0x10},
	{0x334c, 0x10},
	{0x3356, 0x11},
	{0x3364, 0x17},
	{0x336d, 0x03},
	{0x3390, 0x08},
	{0x3391, 0x18},
	{0x3392, 0x38},
	{0x3393, 0x0a},
	{0x3394, 0x0a},
	{0x3395, 0x12},
	{0x3396, 0x08},
	{0x3397, 0x18},
	{0x3398, 0x38},
	{0x3399, 0x0a},
	{0x339a, 0x0a},
	{0x339b, 0x0a},
	{0x339c, 0x12},
	{0x33ac, 0x10},
	{0x33ae, 0x20},
	{0x33af, 0x21},
	{0x360f, 0x01},
	{0x3621, 0xe8},
	{0x3622, 0x06},
	{0x3630, 0x82},
	{0x3633, 0x33},
	{0x3634, 0x64},
	{0x3637, 0x50},
	{0x363a, 0x1f},
	{0x363c, 0x40},
	{0x3651, 0x7d},
	{0x3670, 0x0a},
	{0x3671, 0x06},
	{0x3672, 0x16},
	{0x3673, 0x17},
	{0x3674, 0x82},
	{0x3675, 0x62},
	{0x3676, 0x44},
	{0x367a, 0x48},
	{0x367b, 0x78},
	{0x367c, 0x48},
	{0x367d, 0x58},
	{0x3690, 0x34},
	{0x3691, 0x34},
	{0x3692, 0x54},
	{0x369c, 0x48},
	{0x369d, 0x78},
	{0x36ea, 0xf1},
	{0x36eb, 0x04},
	{0x36ec, 0x0a},
	{0x36ed, 0x04},
	{0x36fa, 0xf1},
	{0x36fb, 0x04},
	{0x36fc, 0x00},
	{0x36fd, 0x06},
	{0x3904, 0x04},
	{0x3908, 0x41},
	{0x391f, 0x10},
	{0x39c2, 0x30},
	{0x3e00, 0x01},
	{0x3e01, 0x8c},
	{0x3e02, 0x00},
	{0x3e04, 0x18},
	{0x3e05, 0xc0},
	{0x3e23, 0x01},
	{0x3e24, 0x37},
	{0x4500, 0x88},
	{0x4509, 0x20},
	{0x4800, 0x04},
	{0x4837, 0x14},
	{0x4853, 0xfd},
	{0x36e9, 0x53},
	{0x36f9, 0x53},
	{REG_NULL, 0x00},
};

static const struct sc500ai_mode supported_modes[] = {
	{
		.width = 2880,
		.height = 1620,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0xcda / 2,
		.hts_def = 0xb40,
		.vts_def = 0x0672,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc500ai_linear_10_2880x1620_regs,
		.mipi_freq_idx = 0,
		.bpp = 10,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 2880,
		.height = 1620,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x18c0 / 2,
		.hts_def = 0xb40,
		.vts_def = 0x0d30,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc500ai_hdr_10_2880x1620_regs,
		.mipi_freq_idx = 1,
		.bpp = 10,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
};

static const s64 link_freq_items[] = {
	SC500AI_LINK_FREQ_198M,
	SC500AI_LINK_FREQ_405M
};

/* Write registers up to 4 at a time */
static int sc500ai_write_reg(struct i2c_client *client, u16 reg,
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

static int sc500ai_write_array(struct i2c_client *client,
                               const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = sc500ai_write_reg(client, regs[i].addr,
		                        SC500AI_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int sc500ai_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static int sc500ai_get_reso_dist(const struct sc500ai_mode *mode,
                                 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc500ai_mode *
sc500ai_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = sc500ai_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int sc500ai_set_fmt(struct v4l2_subdev *sd,
                           struct v4l2_subdev_pad_config *cfg,
                           struct v4l2_subdev_format *fmt)
{
	struct sc500ai *sc500ai = to_sc500ai(sd);
	const struct sc500ai_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;

	mutex_lock(&sc500ai->mutex);

	mode = sc500ai_find_best_fit(fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc500ai->mutex);
		return -ENOTTY;
#endif
	} else {
		sc500ai->cur_mode = mode;

		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc500ai->hblank, h_blank,
		                         h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc500ai->vblank, vblank_def,
		                         SC500AI_VTS_MAX - mode->height,
		                         1, vblank_def);

		__v4l2_ctrl_s_ctrl(sc500ai->link_freq, mode->mipi_freq_idx);
		pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] / mode->bpp * 2 * SC500AI_LANES;
		__v4l2_ctrl_s_ctrl_int64(sc500ai->pixel_rate, pixel_rate);
		sc500ai->cur_fps = mode->max_fps;
		sc500ai->cur_vts = mode->vts_def;
	}

	mutex_unlock(&sc500ai->mutex);

	return 0;
}

static int sc500ai_get_fmt(struct v4l2_subdev *sd,
                           struct v4l2_subdev_pad_config *cfg,
                           struct v4l2_subdev_format *fmt)
{
	struct sc500ai *sc500ai = to_sc500ai(sd);
	const struct sc500ai_mode *mode = sc500ai->cur_mode;

	mutex_lock(&sc500ai->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc500ai->mutex);
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
	mutex_unlock(&sc500ai->mutex);

	return 0;
}

static int sc500ai_enum_mbus_code(struct v4l2_subdev *sd,
                                  struct v4l2_subdev_pad_config *cfg,
                                  struct v4l2_subdev_mbus_code_enum *code)
{
	struct sc500ai *sc500ai = to_sc500ai(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = sc500ai->cur_mode->bus_fmt;

	return 0;
}

static int sc500ai_enum_frame_sizes(struct v4l2_subdev *sd,
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

static int sc500ai_g_frame_interval(struct v4l2_subdev *sd,
                                    struct v4l2_subdev_frame_interval *fi)
{
	struct sc500ai *sc500ai = to_sc500ai(sd);
	const struct sc500ai_mode *mode = sc500ai->cur_mode;

	if (sc500ai->streaming)
		fi->interval = sc500ai->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

static int sc500ai_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
                                 struct v4l2_mbus_config *config)
{
	struct sc500ai *sc500ai = to_sc500ai(sd);
	const struct sc500ai_mode *mode = sc500ai->cur_mode;
	u32 val = 1 << (SC500AI_LANES - 1) |
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

static void sc500ai_get_module_inf(struct sc500ai *sc500ai,
                                   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, SC500AI_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, sc500ai->module_name,
	        sizeof(inf->base.module));
	strlcpy(inf->base.lens, sc500ai->len_name, sizeof(inf->base.lens));
}

static int sc500ai_set_hightemp_dpc(struct sc500ai *sc500ai, u32 total_gain)
{
	int ret = 0;
	if (total_gain <= 0x500) { // 20x gain
		ret = sc500ai_write_reg(sc500ai->client, 0x5799, SC500AI_REG_VALUE_08BIT, 0x00);
	} else if(total_gain >= 0x780) { // 30x gain
		ret = sc500ai_write_reg(sc500ai->client, 0x5799, SC500AI_REG_VALUE_08BIT, 0x07);
	}
	return ret;
}

static int sc500ai_get_gain_reg(u32 total_gain, u32* again, u32* again_fine, u32* dgain, u32* dgain_fine)
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

static int sc500ai_set_hdrae(struct sc500ai *sc500ai,
			   struct preisp_hdrae_exp_s *ae)
{
	int ret = 0;
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_t_gain, m_t_gain, s_t_gain;
	u32 l_again = 0 , l_again_fine = 0, l_dgain = 0, l_dgain_fine = 0;
	u32 s_again = 0, s_again_fine = 0, s_dgain = 0, s_dgain_fine = 0;

	if (!sc500ai->has_init_exp && !sc500ai->streaming) {
		sc500ai->init_hdrae_exp = *ae;
		sc500ai->has_init_exp = true;
		dev_dbg(&sc500ai->client->dev, "sc500ai don't stream, record exp for hdr!\n");
		return ret;
	}
	l_exp_time = ae->long_exp_reg;
	m_exp_time = ae->middle_exp_reg;
	s_exp_time = ae->short_exp_reg;
	l_t_gain = ae->long_gain_reg;
	m_t_gain = ae->middle_gain_reg;
	s_t_gain = ae->short_gain_reg;

	dev_dbg(&sc500ai->client->dev,
		"rev exp req: L_exp: 0x%x, M_exp: 0x%x, S_exp: 0x%x, L_tgain: 0x%x, M_tgain: 0x%x, S_tgain: 0x%x\n",
		l_exp_time, m_exp_time, s_exp_time,
		l_t_gain, m_t_gain, s_t_gain);

	if (sc500ai->cur_mode->hdr_mode == HDR_X2) {
		//2 stagger
		l_t_gain = m_t_gain;
		l_exp_time = m_exp_time;
	}

	l_exp_time = l_exp_time << 1;
	s_exp_time = s_exp_time << 1;

	if (s_t_gain != l_t_gain)
		dev_err(&sc500ai->client->dev,
			"line mode: Long and short frame gains must be equal, l_t_gain: 0x%x, s_t_gain: 0x%x\n",
			l_t_gain, s_t_gain);

	if (s_exp_time > SC500AI_MAX_SHORT_EXPOSURE)
		dev_err(&sc500ai->client->dev,
			"set short exp error, s_exp_time: 0x%x, max_short_exp: 0x%x\n",
			s_exp_time, SC500AI_MAX_SHORT_EXPOSURE);

	// set exposure reg
	ret |= sc500ai_write_reg(sc500ai->client,
				 SC500AI_REG_EXPOSURE_H,
				 SC500AI_REG_VALUE_08BIT,
				 SC500AI_FETCH_EXP_H(l_exp_time));
	ret |= sc500ai_write_reg(sc500ai->client,
	                         SC500AI_REG_EXPOSURE_M,
	                         SC500AI_REG_VALUE_08BIT,
	                         SC500AI_FETCH_EXP_M(l_exp_time));
	ret |= sc500ai_write_reg(sc500ai->client,
	                         SC500AI_REG_EXPOSURE_L,
	                         SC500AI_REG_VALUE_08BIT,
	                         SC500AI_FETCH_EXP_L(l_exp_time));
	ret |= sc500ai_write_reg(sc500ai->client,
	                         SC500AI_REG_SHORT_EXPOSURE_H,
	                         SC500AI_REG_VALUE_08BIT,
	                         SC500AI_FETCH_EXP_H(s_exp_time));
	ret |= sc500ai_write_reg(sc500ai->client,
	                         SC500AI_REG_SHORT_EXPOSURE_M,
	                         SC500AI_REG_VALUE_08BIT,
	                         SC500AI_FETCH_EXP_M(s_exp_time));
	ret |= sc500ai_write_reg(sc500ai->client,
	                         SC500AI_REG_SHORT_EXPOSURE_L,
	                         SC500AI_REG_VALUE_08BIT,
	                         SC500AI_FETCH_EXP_L(s_exp_time));

	// set gain reg
	sc500ai_get_gain_reg(l_t_gain, &l_again, &l_again_fine, &l_dgain, &l_dgain_fine);
	sc500ai_get_gain_reg(s_t_gain, &s_again, &s_again_fine, &s_dgain, &s_dgain_fine);

	ret |= sc500ai_write_reg(sc500ai->client,
				 SC500AI_REG_DIG_GAIN,
				 SC500AI_REG_VALUE_08BIT,
				 l_dgain);
	ret |= sc500ai_write_reg(sc500ai->client,
	                         SC500AI_REG_DIG_FINE_GAIN,
	                         SC500AI_REG_VALUE_08BIT,
	                         l_dgain_fine);
	ret |= sc500ai_write_reg(sc500ai->client,
	                         SC500AI_REG_ANA_GAIN,
	                         SC500AI_REG_VALUE_08BIT,
	                         l_again);
	ret |= sc500ai_write_reg(sc500ai->client,
	                         SC500AI_REG_ANA_FINE_GAIN,
	                         SC500AI_REG_VALUE_08BIT,
	                         l_again_fine);

	ret |= sc500ai_write_reg(sc500ai->client,
				 SC500AI_REG_SDIG_GAIN,
				 SC500AI_REG_VALUE_08BIT,
				 s_dgain);
	ret |= sc500ai_write_reg(sc500ai->client,
	                         SC500AI_REG_SDIG_FINE_GAIN,
	                         SC500AI_REG_VALUE_08BIT,
	                         s_dgain_fine);
	ret |= sc500ai_write_reg(sc500ai->client,
	                         SC500AI_REG_SANA_GAIN,
	                         SC500AI_REG_VALUE_08BIT,
	                         s_again);
	ret |= sc500ai_write_reg(sc500ai->client,
	                         SC500AI_REG_SANA_FINE_GAIN,
	                         SC500AI_REG_VALUE_08BIT,
	                         s_again_fine);
	sc500ai_set_hightemp_dpc(sc500ai, s_t_gain);
	return ret;
}

static int sc500ai_get_channel_info(struct sc500ai *sc500ai, struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = sc500ai->cur_mode->vc[ch_info->index];
	ch_info->width = sc500ai->cur_mode->width;
	ch_info->height = sc500ai->cur_mode->height;
	ch_info->bus_fmt = sc500ai->cur_mode->bus_fmt;
	return 0;
}

static long sc500ai_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc500ai *sc500ai = to_sc500ai(sd);
	struct rkmodule_hdr_cfg *hdr;
	const struct sc500ai_mode *mode;
	struct rkmodule_channel_info *ch_info;

	long ret = 0;
	u32 i, h, w;
	u32 stream = 0;
	u64 pixel_rate = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		sc500ai_get_module_inf(sc500ai, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = sc500ai->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = sc500ai->cur_mode->width;
		h = sc500ai->cur_mode->height;
		for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				sc500ai->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == ARRAY_SIZE(supported_modes)) {
			dev_err(&sc500ai->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			mode = sc500ai->cur_mode;
			w = sc500ai->cur_mode->hts_def - sc500ai->cur_mode->width;
			h = sc500ai->cur_mode->vts_def - sc500ai->cur_mode->height;
			__v4l2_ctrl_modify_range(sc500ai->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(sc500ai->vblank, h,
						 SC500AI_VTS_MAX - sc500ai->cur_mode->height, 1, h);

			__v4l2_ctrl_s_ctrl(sc500ai->link_freq, mode->mipi_freq_idx);
			pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] / mode->bpp * 2 * SC500AI_LANES;
			__v4l2_ctrl_s_ctrl_int64(sc500ai->pixel_rate, pixel_rate);
			sc500ai->cur_fps = mode->max_fps;
			sc500ai->cur_vts = mode->vts_def;
			dev_info(&sc500ai->client->dev, "sensor mode: %d\n", sc500ai->cur_mode->hdr_mode);
		}
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		if (sc500ai->cur_mode->hdr_mode == HDR_X2)
			ret = sc500ai_set_hdrae(sc500ai, arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);
		if (stream)
			ret = sc500ai_write_reg(sc500ai->client, SC500AI_REG_CTRL_MODE,
			                        SC500AI_REG_VALUE_08BIT, SC500AI_MODE_STREAMING);
		else
			ret = sc500ai_write_reg(sc500ai->client, SC500AI_REG_CTRL_MODE,
			                        SC500AI_REG_VALUE_08BIT, SC500AI_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = sc500ai_get_channel_info(sc500ai, ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc500ai_compat_ioctl32(struct v4l2_subdev *sd,
                                   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	struct rkmodule_channel_info *ch_info;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc500ai_ioctl(sd, cmd, inf);
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

		ret = sc500ai_ioctl(sd, cmd, hdr);
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

		ret = sc500ai_ioctl(sd, cmd, hdr);
		kfree(hdr);
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		hdrae = kzalloc(sizeof(*hdrae), GFP_KERNEL);
		if (!hdrae) {
			ret = -ENOMEM;
			return ret;
		}

		if (copy_from_user(hdrae, up, sizeof(*hdrae)))
			return -EFAULT;

		ret = sc500ai_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;

		ret = sc500ai_ioctl(sd, cmd, &stream);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc500ai_ioctl(sd, cmd, ch_info);
		if (!ret) {
			ret = copy_to_user(up, ch_info, sizeof(*ch_info));
			if (ret)
				ret = -EFAULT;
		}
		kfree(ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __sc500ai_start_stream(struct sc500ai *sc500ai)
{
	int ret;

	ret = sc500ai_write_array(sc500ai->client, sc500ai->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&sc500ai->ctrl_handler);
	if (ret)
		return ret;
	if (sc500ai->has_init_exp && sc500ai->cur_mode->hdr_mode != NO_HDR) {
		ret = sc500ai_ioctl(&sc500ai->subdev, PREISP_CMD_SET_HDRAE_EXP,
		                    &sc500ai->init_hdrae_exp);
		if (ret) {
			dev_err(&sc500ai->client->dev,
			        "init exp fail in hdr mode\n");
			return ret;
		}
	}

	return sc500ai_write_reg(sc500ai->client, SC500AI_REG_CTRL_MODE,
	                         SC500AI_REG_VALUE_08BIT, SC500AI_MODE_STREAMING);
}

static int __sc500ai_stop_stream(struct sc500ai *sc500ai)
{
	sc500ai->has_init_exp = false;
	return sc500ai_write_reg(sc500ai->client, SC500AI_REG_CTRL_MODE,
	                         SC500AI_REG_VALUE_08BIT, SC500AI_MODE_SW_STANDBY);
}

static int sc500ai_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc500ai *sc500ai = to_sc500ai(sd);
	struct i2c_client *client = sc500ai->client;
	int ret = 0;

	mutex_lock(&sc500ai->mutex);
	on = !!on;
	if (on == sc500ai->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __sc500ai_start_stream(sc500ai);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc500ai_stop_stream(sc500ai);
		pm_runtime_put(&client->dev);
	}

	sc500ai->streaming = on;

unlock_and_return:
	mutex_unlock(&sc500ai->mutex);

	return ret;
}

static int sc500ai_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc500ai *sc500ai = to_sc500ai(sd);
	struct i2c_client *client = sc500ai->client;
	int ret = 0;

	mutex_lock(&sc500ai->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc500ai->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret |= sc500ai_write_reg(sc500ai->client,
		                         SC500AI_SOFTWARE_RESET_REG,
		                         SC500AI_REG_VALUE_08BIT,
		                         0x01);
		usleep_range(100, 200);

		sc500ai->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc500ai->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc500ai->mutex);

	return ret;
}

static int __sc500ai_power_on(struct sc500ai *sc500ai)
{
	int ret;
	struct device *dev = &sc500ai->client->dev;

	if (!IS_ERR_OR_NULL(sc500ai->pins_default)) {
		ret = pinctrl_select_state(sc500ai->pinctrl,
		                           sc500ai->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(sc500ai->xvclk, SC500AI_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (27MHz)\n");
	if (clk_get_rate(sc500ai->xvclk) != SC500AI_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc500ai->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(sc500ai->reset_gpio))
		gpiod_set_value_cansleep(sc500ai->reset_gpio, 0);

	ret = regulator_bulk_enable(sc500ai_NUM_SUPPLIES, sc500ai->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(sc500ai->reset_gpio))
		gpiod_set_value_cansleep(sc500ai->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(sc500ai->pwdn_gpio))
		gpiod_set_value_cansleep(sc500ai->pwdn_gpio, 1);

	usleep_range(4000, 5000);
	return 0;

disable_clk:
	clk_disable_unprepare(sc500ai->xvclk);

	return ret;
}

static void __sc500ai_power_off(struct sc500ai *sc500ai)
{
	int ret;
	struct device *dev = &sc500ai->client->dev;

	if (!IS_ERR(sc500ai->pwdn_gpio))
		gpiod_set_value_cansleep(sc500ai->pwdn_gpio, 0);
	clk_disable_unprepare(sc500ai->xvclk);
	if (!IS_ERR(sc500ai->reset_gpio))
		gpiod_set_value_cansleep(sc500ai->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(sc500ai->pins_sleep)) {
		ret = pinctrl_select_state(sc500ai->pinctrl,
		                           sc500ai->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(sc500ai_NUM_SUPPLIES, sc500ai->supplies);
}

static int sc500ai_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc500ai *sc500ai = to_sc500ai(sd);

	return __sc500ai_power_on(sc500ai);
}

static int sc500ai_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc500ai *sc500ai = to_sc500ai(sd);

	__sc500ai_power_off(sc500ai);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc500ai_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc500ai *sc500ai = to_sc500ai(sd);
	struct v4l2_mbus_framefmt *try_fmt =
	        v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc500ai_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc500ai->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc500ai->mutex);
	/* No crop or compose */

	return 0;
}
#endif

#define DST_WIDTH 2880
#define DST_HEIGHT 1616

/*
 * The resolution of the driver configuration needs to be exactly
 * the same as the current output resolution of the sensor,
 * the input width of the isp needs to be 16 aligned,
 * the input height of the isp needs to be 8 aligned.
 * Can be cropped to standard resolution by this function,
 * otherwise it will crop out strange resolution according
 * to the alignment rules.
 */
static int sc500ai_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = 0;
		sel->r.width = DST_WIDTH;
		sel->r.top = 2;
		sel->r.height = DST_HEIGHT;
		return 0;
	}
	return -EINVAL;
}

static int sc500ai_enum_frame_interval(struct v4l2_subdev *sd,
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

static const struct dev_pm_ops sc500ai_pm_ops = {
	SET_RUNTIME_PM_OPS(sc500ai_runtime_suspend,
	sc500ai_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc500ai_internal_ops = {
	.open = sc500ai_open,
};
#endif

static const struct v4l2_subdev_core_ops sc500ai_core_ops = {
	.s_power = sc500ai_s_power,
	.ioctl = sc500ai_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc500ai_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc500ai_video_ops = {
	.s_stream = sc500ai_s_stream,
	.g_frame_interval = sc500ai_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops sc500ai_pad_ops = {
	.enum_mbus_code = sc500ai_enum_mbus_code,
	.enum_frame_size = sc500ai_enum_frame_sizes,
	.enum_frame_interval = sc500ai_enum_frame_interval,
	.get_fmt = sc500ai_get_fmt,
	.set_fmt = sc500ai_set_fmt,
	.get_selection = sc500ai_get_selection,
	.get_mbus_config = sc500ai_g_mbus_config,
};

static const struct v4l2_subdev_ops sc500ai_subdev_ops = {
	.core	= &sc500ai_core_ops,
	.video	= &sc500ai_video_ops,
	.pad	= &sc500ai_pad_ops,
};

static void sc500ai_modify_fps_info(struct sc500ai *sc500ai)
{
	const struct sc500ai_mode *mode = sc500ai->cur_mode;

	sc500ai->cur_fps.denominator = mode->max_fps.denominator * mode->vts_def /
				       sc500ai->cur_vts;
}

static int sc500ai_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc500ai *sc500ai = container_of(ctrl->handler,
	                                       struct sc500ai, ctrl_handler);
	struct i2c_client *client = sc500ai->client;
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
		max = sc500ai->cur_mode->height + ctrl->val - 5;
		__v4l2_ctrl_modify_range(sc500ai->exposure,
					 sc500ai->exposure->minimum, max,
					 sc500ai->exposure->step,
					 sc500ai->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (sc500ai->cur_mode->hdr_mode != NO_HDR)
			goto ctrl_end;
		val = ctrl->val << 1;
		ret = sc500ai_write_reg(sc500ai->client,
					SC500AI_REG_EXPOSURE_H,
					SC500AI_REG_VALUE_08BIT,
					SC500AI_FETCH_EXP_H(val));
		ret |= sc500ai_write_reg(sc500ai->client,
					SC500AI_REG_EXPOSURE_M,
					SC500AI_REG_VALUE_08BIT,
					SC500AI_FETCH_EXP_M(val));
		ret |= sc500ai_write_reg(sc500ai->client,
					 SC500AI_REG_EXPOSURE_L,
					 SC500AI_REG_VALUE_08BIT,
					 SC500AI_FETCH_EXP_L(val));

		dev_dbg(&client->dev, "set exposure 0x%x\n", val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (sc500ai->cur_mode->hdr_mode != NO_HDR)
			goto ctrl_end;

		sc500ai_get_gain_reg(ctrl->val, &again, &again_fine, &dgain, &dgain_fine);
		ret = sc500ai_write_reg(sc500ai->client,
					SC500AI_REG_DIG_GAIN,
					SC500AI_REG_VALUE_08BIT,
					dgain);
		ret |= sc500ai_write_reg(sc500ai->client,
					 SC500AI_REG_DIG_FINE_GAIN,
					 SC500AI_REG_VALUE_08BIT,
					 dgain_fine);
		ret |= sc500ai_write_reg(sc500ai->client,
					 SC500AI_REG_ANA_GAIN,
					 SC500AI_REG_VALUE_08BIT,
					 again);
		ret |= sc500ai_write_reg(sc500ai->client,
					 SC500AI_REG_ANA_FINE_GAIN,
					 SC500AI_REG_VALUE_08BIT,
					 again_fine);
		sc500ai_set_hightemp_dpc(sc500ai, ctrl->val);

		dev_dbg(&sc500ai->client->dev,
			"total_gain:%d again 0x%x, again_fine 0x%x, dgain 0x%x, dgain_fine 0x%x\n",
			ctrl->val, again, again_fine, dgain, dgain_fine);
		break;
	case V4L2_CID_VBLANK:
		vts = ctrl->val + sc500ai->cur_mode->height;
		ret = sc500ai_write_reg(sc500ai->client,
					SC500AI_REG_VTS_H,
					SC500AI_REG_VALUE_08BIT,
					(vts >> 8) & 0x7f);
		ret |= sc500ai_write_reg(sc500ai->client,
					 SC500AI_REG_VTS_L,
					 SC500AI_REG_VALUE_08BIT,
					 vts & 0xff);
		if (!ret)
			sc500ai->cur_vts = vts;
		if (sc500ai->cur_vts != sc500ai->cur_mode->vts_def)
			sc500ai_modify_fps_info(sc500ai);
		break;
	case V4L2_CID_HFLIP:
		ret = sc500ai_read_reg(sc500ai->client, SC500AI_FLIP_MIRROR_REG,
				       SC500AI_REG_VALUE_08BIT, &val);
		if (ret)
			break;
		if (ctrl->val)
			val |= SC500AI_MIRROR_MASK;
		else
			val &= ~SC500AI_MIRROR_MASK;
		ret |= sc500ai_write_reg(sc500ai->client, SC500AI_FLIP_MIRROR_REG,
					 SC500AI_REG_VALUE_08BIT, val);
		break;
	case V4L2_CID_VFLIP:
		ret = sc500ai_read_reg(sc500ai->client,
				       SC500AI_FLIP_MIRROR_REG,
				       SC500AI_REG_VALUE_08BIT, &val);
		if (ret)
			break;
		denominator = sc500ai->cur_mode->max_fps.denominator;
		numerator = sc500ai->cur_mode->max_fps.numerator;
		def_fps = denominator / numerator;
		cur_fps = def_fps * sc500ai->cur_mode->vts_def / sc500ai->cur_vts;
		if (cur_fps > 25) {
			vts = def_fps * sc500ai->cur_mode->vts_def / 25;
			ret = sc500ai_write_reg(sc500ai->client,
						SC500AI_REG_VTS_H,
						SC500AI_REG_VALUE_08BIT,
						(vts >> 8) & 0x7f);
			ret |= sc500ai_write_reg(sc500ai->client,
						SC500AI_REG_VTS_L,
						SC500AI_REG_VALUE_08BIT,
						vts & 0xff);
			delay_time = 1000000 / 25;//one frame interval
			delay_time *= 2;
			usleep_range(delay_time, delay_time + 1000);
		}

		if (ctrl->val)
			val |= SC500AI_FLIP_MASK;
		else
			val &= ~SC500AI_FLIP_MASK;

		ret |= sc500ai_write_reg(sc500ai->client,
					 SC500AI_FLIP_MIRROR_REG,
					 SC500AI_REG_VALUE_08BIT,
					 val);
		if (cur_fps > 25) {
			usleep_range(delay_time, delay_time + 1000);
			vts = sc500ai->cur_vts;
			ret = sc500ai_write_reg(sc500ai->client,
						SC500AI_REG_VTS_H,
						SC500AI_REG_VALUE_08BIT,
						(vts >> 8) & 0x7f);
			ret |= sc500ai_write_reg(sc500ai->client,
						SC500AI_REG_VTS_L,
						SC500AI_REG_VALUE_08BIT,
						vts & 0xff);
		}
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

ctrl_end:
	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc500ai_ctrl_ops = {
	.s_ctrl = sc500ai_set_ctrl,
};

static int sc500ai_initialize_controls(struct sc500ai *sc500ai)
{
	const struct sc500ai_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u64 pixel_rate;
	u32 h_blank;
	int ret;

	handler = &sc500ai->ctrl_handler;
	mode = sc500ai->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &sc500ai->mutex;

	sc500ai->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ,
				ARRAY_SIZE(link_freq_items) - 1, 0,
				link_freq_items);
	__v4l2_ctrl_s_ctrl(sc500ai->link_freq, mode->mipi_freq_idx);

	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] / mode->bpp * 2 * SC500AI_LANES ;
	sc500ai->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
			V4L2_CID_PIXEL_RATE, 0, SC500AI_MAX_PIXEL_RATE,
			1, pixel_rate);

	h_blank = mode->hts_def - mode->width;
	sc500ai->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
	                                    h_blank, h_blank, 1, h_blank);
	if (sc500ai->hblank)
		sc500ai->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	sc500ai->cur_vts = mode->vts_def;
	sc500ai->cur_fps = mode->max_fps;
	sc500ai->vblank = v4l2_ctrl_new_std(handler, &sc500ai_ctrl_ops,
	                                    V4L2_CID_VBLANK, vblank_def,
	                                    SC500AI_VTS_MAX - mode->height,
	                                    1, vblank_def);
	exposure_max = mode->vts_def - 5;
	sc500ai->exposure = v4l2_ctrl_new_std(handler, &sc500ai_ctrl_ops,
	                                      V4L2_CID_EXPOSURE, SC500AI_EXPOSURE_MIN,
	                                      exposure_max, sc500ai_EXPOSURE_STEP,
	                                      mode->exp_def);

	sc500ai->anal_gain = v4l2_ctrl_new_std(handler, &sc500ai_ctrl_ops,
	                                       V4L2_CID_ANALOGUE_GAIN, SC500AI_GAIN_MIN,
	                                       SC500AI_GAIN_MAX, SC500AI_GAIN_STEP,
	                                       SC500AI_GAIN_DEFAULT);

	v4l2_ctrl_new_std(handler, &sc500ai_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(handler, &sc500ai_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&sc500ai->client->dev,
		        "Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}
	sc500ai->subdev.ctrl_handler = handler;
	sc500ai->has_init_exp = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);
	return ret;
}

static int sc500ai_check_sensor_id(struct sc500ai *sc500ai,
                                   struct i2c_client *client)
{
	struct device *dev = &sc500ai->client->dev;
	u32 id = 0;
	int ret;

	ret = sc500ai_read_reg(client, SC500AI_REG_CHIP_ID,
	                       SC500AI_REG_VALUE_16BIT, &id);
	if (id != SC500AI_CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected SC%06x sensor\n", SC500AI_CHIP_ID);

	return 0;
}

static int sc500ai_configure_regulators(struct sc500ai *sc500ai)
{
	unsigned int i;

	for (i = 0; i < sc500ai_NUM_SUPPLIES; i++)
		sc500ai->supplies[i].supply = sc500ai_supply_names[i];

	return devm_regulator_bulk_get(&sc500ai->client->dev,
	                               sc500ai_NUM_SUPPLIES,
	                               sc500ai->supplies);
}

static int sc500ai_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc500ai *sc500ai;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
	         DRIVER_VERSION >> 16,
	         (DRIVER_VERSION & 0xff00) >> 8,
	         DRIVER_VERSION & 0x00ff);

	sc500ai = devm_kzalloc(dev, sizeof(*sc500ai), GFP_KERNEL);
	if (!sc500ai)
		return -ENOMEM;

	of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
	                           &sc500ai->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
	                               &sc500ai->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
	                               &sc500ai->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
	                               &sc500ai->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	sc500ai->client = client;
	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			sc500ai->cur_mode = &supported_modes[i];
			break;
		}
	}
	if (i == ARRAY_SIZE(supported_modes))
		sc500ai->cur_mode = &supported_modes[0];

	sc500ai->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc500ai->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	sc500ai->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sc500ai->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	sc500ai->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(sc500ai->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	sc500ai->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc500ai->pinctrl)) {
		sc500ai->pins_default =
		        pinctrl_lookup_state(sc500ai->pinctrl,
		                             OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc500ai->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		sc500ai->pins_sleep =
		        pinctrl_lookup_state(sc500ai->pinctrl,
		                             OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc500ai->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = sc500ai_configure_regulators(sc500ai);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&sc500ai->mutex);

	sd = &sc500ai->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc500ai_subdev_ops);
	ret = sc500ai_initialize_controls(sc500ai);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc500ai_power_on(sc500ai);
	if (ret)
		goto err_free_handler;

	ret = sc500ai_check_sensor_id(sc500ai, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc500ai_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
	             V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc500ai->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc500ai->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc500ai->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
	         sc500ai->module_index, facing,
	         SC500AI_NAME, dev_name(sd->dev));
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
	__sc500ai_power_off(sc500ai);
err_free_handler:
	v4l2_ctrl_handler_free(&sc500ai->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc500ai->mutex);

	return ret;
}

static int sc500ai_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc500ai *sc500ai = to_sc500ai(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc500ai->ctrl_handler);
	mutex_destroy(&sc500ai->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc500ai_power_off(sc500ai);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc500ai_of_match[] = {
	{ .compatible = "smartsens,sc500ai" },
	{},
};
MODULE_DEVICE_TABLE(of, sc500ai_of_match);
#endif

static const struct i2c_device_id sc500ai_match_id[] = {
	{ "smartsens,sc500ai", 0 },
	{ },
};

static struct i2c_driver sc500ai_i2c_driver = {
	.driver = {
		.name = SC500AI_NAME,
		.pm = &sc500ai_pm_ops,
		.of_match_table = of_match_ptr(sc500ai_of_match),
	},
	.probe		= &sc500ai_probe,
	.remove		= &sc500ai_remove,
	.id_table	= sc500ai_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc500ai_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc500ai_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("smartsens sc500ai sensor driver");
MODULE_LICENSE("GPL v2");
