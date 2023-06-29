// SPDX-License-Identifier: GPL-2.0
/*
 * sc850sl driver
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

#define MIPI_FREQ_540M			540000000

#define SC850SL_MAX_PIXEL_RATE	(MIPI_FREQ_540M / 10 * 2 * SC850SL_4LANES)
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define SC850SL_XVCLK_FREQ_24M		24000000

/* TODO: Get the real chip id from reg */
#define CHIP_ID				0x9D1E
#define SC850SL_REG_CHIP_ID		0x3107

#define SC850SL_REG_CTRL_MODE		0x0100
#define SC850SL_MODE_SW_STANDBY		0x0
#define SC850SL_MODE_STREAMING		BIT(0)

/*expo*/
#define	SC850SL_EXPOSURE_MIN		2    /*okay*/
#define	SC850SL_EXPOSURE_STEP		1    /*okay*/
#define SC850SL_VTS_MAX			0xffff   /*okay*/

//long exposure
#define SC850SL_REG_EXP_LONG_H		0x3e00    //[3:0]
#define SC850SL_REG_EXP_LONG_M		0x3e01    //[7:0]
#define SC850SL_REG_EXP_LONG_L		0x3e02    //[7:4]

//short exposure  //for hdr
#define SC850SL_REG_EXP_SF_H		0x3e22
#define SC850SL_REG_EXP_SF_M		0x3e04    //[7:0]
#define SC850SL_REG_EXP_SF_L		0x3e05    //[7:4]

#define SC850SL_FETCH_EXP_H(VAL)		(((VAL) >> 12) & 0xF)
#define SC850SL_FETCH_EXP_M(VAL)		(((VAL) >> 4) & 0xFF)
#define SC850SL_FETCH_EXP_L(VAL)		(((VAL) & 0xF) << 4)

/*gain*/
//long frame and normal gain reg
#define SC850SL_REG_DGAIN		0x3e06
#define SC850SL_REG_AGAIN		0x3e08
#define SC850SL_REG_AGAIN_FINE		0x3e09
//#define SC850SL_REG_DGAIN_FINE		0x3e07

//short fram gain reg
#define SC850SL_SF_REG_AGAIN		0x3e12
#define SC850SL_SF_REG_AGAIN_FINE	0x3e13
#define SC850SL_SF_REG_DGAIN		0x3e10

#define SC850SL_GAIN_MIN		0x40	//1.000 = 64 * 1/64
#define SC850SL_GAIN_MAX		(8 * 50 * 64)   /*need_view   8*50*64=25600  */
#define SC850SL_GAIN_STEP		1
#define SC850SL_GAIN_DEFAULT		0x40

#define SC850SL_REG_VTS			0x320e

//group hold
#define SC850SL_GROUP_UPDATE_ADDRESS	0x3800
#define SC850SL_GROUP_UPDATE_START_DATA	0x00
#define SC850SL_GROUP_UPDATE_LAUNCH	0x30

#define SC850SL_SOFTWARE_RESET_REG	0x0103
#define SC850SL_REG_TEST_PATTERN	0x4501
#define SC850SL_TEST_PATTERN_ENABLE	0x08

#define SC850SL_FLIP_REG		0x3221
#define SC850SL_FLIP_MASK		0x60
#define SC850SL_MIRROR_MASK		0x06

#define REG_NULL			0xFFFF

#define SC850SL_REG_VALUE_08BIT		1
#define SC850SL_REG_VALUE_16BIT		2
#define SC850SL_REG_VALUE_24BIT		3

#define SC850SL_4LANES			4

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define SC850SL_NAME			"sc850sl"


static const char * const sc850sl_supply_names[] = {
	"dvdd",		// Digital core power
	"dovdd",	// Digital I/O power
	"avdd",		// Analog power
};
#define SC850SL_NUM_SUPPLIES ARRAY_SIZE(sc850sl_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct sc850sl_mode {
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

struct sc850sl {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*power_gpio;
	struct regulator_bulk_data supplies[SC850SL_NUM_SUPPLIES];

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_a_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*test_pattern;
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct mutex		mutex;
	struct v4l2_fract	cur_fps;
	bool			streaming;
	bool			power_on;
	bool			is_first_streamoff;
	const struct sc850sl_mode *cur_mode;
	u32			module_index;
	u32			cfg_num;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
};


#define to_sc850sl(sd) container_of(sd, struct sc850sl, subdev)

//cleaned_0x20_SC850SL_MIPI_24Minput_1C4D_1080Mbps_10bit_3840x2160_30fps_one_expo.ini
static __maybe_unused const struct regval sc850sl_linear10bit_3840x2160_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x36ea, 0x09},
	{0x36eb, 0x0c},
	{0x36ec, 0x4a},
	{0x36ed, 0x24},
	{0x36fa, 0x0b},
	{0x36fb, 0x33},
	{0x36fc, 0x10},
	{0x36fd, 0x37},
	{0x36e9, 0x24},
	{0x36f9, 0x53},
	{0x3018, 0x7a},
	{0x3019, 0xf0},
	{0x301a, 0x30},
	{0x301e, 0x3c},
	{0x301f, 0x20},
	{0x302a, 0x00},
	{0x3031, 0x0a},
	{0x3032, 0x20},
	{0x3033, 0x22},
	{0x3037, 0x00},
	{0x303e, 0xb4},
	{0x320c, 0x04},
	{0x320d, 0x4c},
	{0x3226, 0x00},
	{0x3227, 0x03},
	{0x3250, 0x40},
	{0x3253, 0x08},
	{0x327e, 0x00},
	{0x3280, 0x00},
	{0x3281, 0x00},
	{0x3301, 0x3c},
	{0x3304, 0x30},
	{0x3306, 0xe8},
	{0x3308, 0x10},
	{0x3309, 0x70},
	{0x330a, 0x01},
	{0x330b, 0xe0},
	{0x330d, 0x10},
	{0x3314, 0x92},
	{0x331e, 0x29},
	{0x331f, 0x69},
	{0x3333, 0x10},
	{0x3347, 0x05},
	{0x3348, 0xd0},
	{0x3352, 0x01},
	{0x3356, 0x38},
	{0x335d, 0x60},
	{0x3362, 0x70},
	{0x338f, 0x80},
	{0x33af, 0x48},
	{0x33fe, 0x00},
	{0x3400, 0x12},
	{0x3406, 0x04},
	{0x3410, 0x12},
	{0x3416, 0x06},
	{0x3433, 0x01},
	{0x3440, 0x12},
	{0x3446, 0x08},
	{0x3478, 0x01},
	{0x3479, 0x01},
	{0x347a, 0x02},
	{0x347b, 0x01},
	{0x347c, 0x04},
	{0x347d, 0x01},
	{0x3616, 0x0c},
	{0x3620, 0x92},
	{0x3622, 0x74},
	{0x3629, 0x74},
	{0x362a, 0xf0},
	{0x362b, 0x0f},
	{0x362d, 0x00},
	{0x3630, 0x68},
	{0x3633, 0x22},
	{0x3634, 0x22},
	{0x3635, 0x20},
	{0x3637, 0x06},
	{0x3638, 0x26},
	{0x363b, 0x06},
	{0x363c, 0x08},
	{0x363d, 0x05},
	{0x363e, 0x8f},
	{0x3648, 0xe0},
	{0x3649, 0x0a},
	{0x364a, 0x06},
	{0x364c, 0x6a},
	{0x3650, 0x3d},
	{0x3654, 0x40},
	{0x3656, 0x68},
	{0x3657, 0x0f},
	{0x3658, 0x3d},
	{0x365c, 0x40},
	{0x365e, 0x68},
	{0x3901, 0x04},
	{0x3904, 0x20},
	{0x3905, 0x91},
	{0x391e, 0x83},
	{0x3928, 0x04},
	{0x3933, 0xa0},
	{0x3934, 0x0a},
	{0x3935, 0x68},
	{0x3936, 0x00},
	{0x3937, 0x20},
	{0x3938, 0x0a},
	{0x3946, 0x20},
	{0x3961, 0x40},
	{0x3962, 0x40},
	{0x3963, 0xc8},
	{0x3964, 0xc8},
	{0x3965, 0x40},
	{0x3966, 0x40},
	{0x3967, 0x00},
	{0x39cd, 0xc8},
	{0x39ce, 0xc8},
	{0x3e01, 0x82},
	{0x3e02, 0x00},
	{0x3e0e, 0x02},
	{0x3e0f, 0x00},
	{0x3e1c, 0x0f},
	{0x3e23, 0x00},
	{0x3e24, 0x00},
	{0x3e53, 0x00},
	{0x3e54, 0x00},
	{0x3e68, 0x00},
	{0x3e69, 0x80},
	{0x3e73, 0x00},
	{0x3e74, 0x00},
	{0x3e86, 0x03},
	{0x3e87, 0x40},
	{0x3f02, 0x24},
	{0x4424, 0x02},
	{0x4501, 0xc4},
	{0x4509, 0x20},
	{0x4561, 0x12},
	{0x4800, 0x24},
	{0x4837, 0x0f},
	{0x4900, 0x24},
	{0x4937, 0x0f},
	{0x5000, 0x0e},
	{0x500f, 0x35},
	{0x5020, 0x00},
	{0x5787, 0x10},
	{0x5788, 0x06},
	{0x5789, 0x00},
	{0x578a, 0x18},
	{0x578b, 0x0c},
	{0x578c, 0x00},
	{0x5790, 0x10},
	{0x5791, 0x06},
	{0x5792, 0x01},
	{0x5793, 0x18},
	{0x5794, 0x0c},
	{0x5795, 0x01},
	{0x5799, 0x06},
	{0x57a2, 0x60},
	{0x59e0, 0xfe},
	{0x59e1, 0x40},
	{0x59e2, 0x38},
	{0x59e3, 0x30},
	{0x59e4, 0x20},
	{0x59e5, 0x38},
	{0x59e6, 0x30},
	{0x59e7, 0x20},
	{0x59e8, 0x3f},
	{0x59e9, 0x38},
	{0x59ea, 0x30},
	{0x59eb, 0x3f},
	{0x59ec, 0x38},
	{0x59ed, 0x30},
	{0x59ee, 0xfe},
	{0x59ef, 0x40},
	{0x59f4, 0x38},
	{0x59f5, 0x30},
	{0x59f6, 0x20},
	{0x59f7, 0x38},
	{0x59f8, 0x30},
	{0x59f9, 0x20},
	{0x59fa, 0x3f},
	{0x59fb, 0x38},
	{0x59fc, 0x30},
	{0x59fd, 0x3f},
	{0x59fe, 0x38},
	{0x59ff, 0x30},
	{0x0100, 0x01},
	/*
	 * [gain < 2x] {0x363c, 0x05},
	 * [gain >=2x] {0x363c, 0x07},
	 */
	{0x363c, 0x07},
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
static const struct sc850sl_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.width = 3840,
		.height = 2160,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x08c0,
		.hts_def = 0x0226*5-0x180,
		.vts_def = 0x08ca,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc850sl_linear10bit_3840x2160_regs,
		.hdr_mode = NO_HDR,
		.mipi_freq_idx = 0,
		.bpp = 10,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};



static const char * const sc850sl_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

static const s64 link_freq_items[] = {
	MIPI_FREQ_540M,
};

/* Write registers up to 4 at a time */
static int sc850sl_write_reg(struct i2c_client *client, u16 reg,
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

static int sc850sl_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = sc850sl_write_reg(client, regs[i].addr,
				       SC850SL_REG_VALUE_08BIT, regs[i].val);
	}
	return ret;
}

/* Read registers up to 4 at a time */
static int sc850sl_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static int sc850sl_get_reso_dist(const struct sc850sl_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc850sl_mode *
sc850sl_find_best_fit(struct sc850sl *sc850sl, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = sc850sl_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist < cur_best_fit_dist) &&
			supported_modes[i].bus_fmt == framefmt->code) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}
	dev_info(&sc850sl->client->dev, "%s: cur_best_fit(%d)",
		 __func__, cur_best_fit);

	return &supported_modes[cur_best_fit];
}

static void sc850sl_change_mode(struct sc850sl *sc850sl, const struct sc850sl_mode *mode)
{
	sc850sl->cur_mode = mode;
	sc850sl->cur_vts = sc850sl->cur_mode->vts_def;
	dev_info(&sc850sl->client->dev, "set fmt: cur_mode: %dx%d, hdr: %d\n",
		mode->width, mode->height, mode->hdr_mode);
}

static int sc850sl_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sc850sl *sc850sl = to_sc850sl(sd);
	const struct sc850sl_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;

	mutex_lock(&sc850sl->mutex);

	mode = sc850sl_find_best_fit(sc850sl, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc850sl->mutex);
		return -ENOTTY;
#endif
	} else {
		sc850sl_change_mode(sc850sl, mode);
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc850sl->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc850sl->vblank, vblank_def,
					 SC850SL_VTS_MAX - mode->height,
					 1, vblank_def);
		__v4l2_ctrl_s_ctrl(sc850sl->link_freq, mode->mipi_freq_idx);
		pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] /
			mode->bpp * 2 * SC850SL_4LANES;
		__v4l2_ctrl_s_ctrl_int64(sc850sl->pixel_rate, pixel_rate);
		sc850sl->cur_fps = mode->max_fps;
		sc850sl->cur_vts = mode->vts_def;
	}

	mutex_unlock(&sc850sl->mutex);

	return 0;
}

static int sc850sl_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sc850sl *sc850sl = to_sc850sl(sd);
	const struct sc850sl_mode *mode = sc850sl->cur_mode;

	mutex_lock(&sc850sl->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc850sl->mutex);
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
	mutex_unlock(&sc850sl->mutex);

	return 0;
}

static int sc850sl_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct sc850sl *sc850sl = to_sc850sl(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = sc850sl->cur_mode->bus_fmt;

	return 0;
}

static int sc850sl_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct sc850sl *sc850sl = to_sc850sl(sd);

	if (fse->index >= sc850sl->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int sc850sl_enable_test_pattern(struct sc850sl *sc850sl, u32 pattern)
{
	u32 val = 0;
	int ret = 0;

	ret = sc850sl_read_reg(sc850sl->client, SC850SL_REG_TEST_PATTERN,
			      SC850SL_REG_VALUE_08BIT, &val);
	if (pattern)
		val |= SC850SL_TEST_PATTERN_ENABLE;
	else
		val &= ~SC850SL_TEST_PATTERN_ENABLE;
	ret |= sc850sl_write_reg(sc850sl->client, SC850SL_REG_TEST_PATTERN,
				SC850SL_REG_VALUE_08BIT, val);
	return ret;
}

static int sc850sl_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct sc850sl *sc850sl = to_sc850sl(sd);
	const struct sc850sl_mode *mode = sc850sl->cur_mode;

	if (sc850sl->streaming)
		fi->interval = sc850sl->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

static int sc850sl_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct sc850sl *sc850sl = to_sc850sl(sd);
	const struct sc850sl_mode *mode = sc850sl->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (SC850SL_4LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	if (mode->hdr_mode == HDR_X2)
		val = 1 << (SC850SL_4LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		V4L2_MBUS_CSI2_CHANNEL_1;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void sc850sl_get_module_inf(struct sc850sl *sc850sl,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, SC850SL_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, sc850sl->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, sc850sl->len_name, sizeof(inf->base.lens));
}

static void sc850sl_get_gain_reg(u32 val, u32 *again_reg, u32 *again_fine_reg,
				 u32 *dgain_reg)
{
	u8 u8Reg0x3e09 = 0x40, u8Reg0x3e08 = 0x03;
	u32 aCoarseGain = 0;
	u32 aFineGain = 0;
	u32 again = 0;
	u32 dgain = 0;

	if (val < 64)
		val = 64;
	else if (val > SC850SL_GAIN_MAX)
		val = SC850SL_GAIN_MAX;

	if (val <= 3199) {
		again = val;
		dgain = 1;
	} else {
		again = 3199;
		dgain = val / again;
	}

	//again
	if (again <= 200) {
		//a_gain < 3.125x
		for (aCoarseGain = 1; aCoarseGain <= 2; aCoarseGain = aCoarseGain * 2) {
			//1,2,4,8,16
			if (again < (64 * 2 * aCoarseGain))
				break;
		}
		aFineGain = again / aCoarseGain;
	} else {
		for (aCoarseGain = 1; aCoarseGain <= 8; aCoarseGain = aCoarseGain * 2) {
			//1,2,4,8
			if (again < (64 * 2 * aCoarseGain * 3125 / 1000))
				break;
		}
		aFineGain = 1000 * again / aCoarseGain / 3125;
	}
	for ( ; aCoarseGain >= 2; aCoarseGain = aCoarseGain / 2)
		u8Reg0x3e08 = (u8Reg0x3e08 << 1) | 0x01;

	u8Reg0x3e09 = aFineGain;
	//dcg = 2.72  -->  2.72*1024=2785.28
	u8Reg0x3e08 = (again > 200) ? (u8Reg0x3e08 | 0x20) : (u8Reg0x3e08 & 0x1f);

	//dgain
	if (dgain <= 1) { /*1x ~ 2x*/
		*dgain_reg = 0x00;
	} else if (dgain <= 2) { /*2x ~ 4x*/
		*dgain_reg = 0x01;
	} else if (dgain <= 4) { /*4x ~ 8x*/
		*dgain_reg = 0x03;
	} else {
		*dgain_reg = 0x07;
	}

	*again_reg = u8Reg0x3e08;
	*again_fine_reg = u8Reg0x3e09;
}

static int sc850sl_get_channel_info(struct sc850sl *sc850sl, struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = sc850sl->cur_mode->vc[ch_info->index];
	ch_info->width = sc850sl->cur_mode->width;
	ch_info->height = sc850sl->cur_mode->height;
	ch_info->bus_fmt = sc850sl->cur_mode->bus_fmt;
	return 0;
}

static long sc850sl_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc850sl *sc850sl = to_sc850sl(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	const struct sc850sl_mode *mode;
	struct rkmodule_channel_info *ch_info;
	long ret = 0;
	u64 pixel_rate = 0;
	u32 i, h, w, stream;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		/*
		 * ret = sc850sl_set_hdrae(sc850sl, arg);
		 */
		break;

	case RKMODULE_SET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		if (sc850sl->streaming) {
			ret = sc850sl_write_array(sc850sl->client, sc850sl->cur_mode->reg_list);
			if (ret)
				return ret;
		}
		w = sc850sl->cur_mode->width;
		h = sc850sl->cur_mode->height;
		for (i = 0; i < sc850sl->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			h == supported_modes[i].height &&
			supported_modes[i].hdr_mode == hdr_cfg->hdr_mode) {
				sc850sl_change_mode(sc850sl, &supported_modes[i]);
				break;
			}
		}
		if (i == sc850sl->cfg_num) {
			dev_err(&sc850sl->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr_cfg->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			mode = sc850sl->cur_mode;
			w = mode->hts_def - mode->width;
			h = mode->vts_def - mode->height;
			__v4l2_ctrl_modify_range(sc850sl->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(sc850sl->vblank, h,
				SC850SL_VTS_MAX - mode->height,
				1, h);
			__v4l2_ctrl_s_ctrl(sc850sl->link_freq, mode->mipi_freq_idx);
			pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] /
				mode->bpp * 2 * SC850SL_4LANES;
			__v4l2_ctrl_s_ctrl_int64(sc850sl->pixel_rate,
						 pixel_rate);
			sc850sl->cur_fps = mode->max_fps;
			sc850sl->cur_vts = mode->vts_def;
			dev_info(&sc850sl->client->dev,
				"sensor mode: %d\n", mode->hdr_mode);
		}
		break;
	case RKMODULE_GET_MODULE_INFO:
		sc850sl_get_module_inf(sc850sl, (struct rkmodule_inf *)arg);
		break;

	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = sc850sl->cur_mode->hdr_mode;
		break;

	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);

		if (stream)
			ret = sc850sl_write_reg(sc850sl->client, SC850SL_REG_CTRL_MODE,
				SC850SL_REG_VALUE_08BIT, SC850SL_MODE_STREAMING);
		else
			ret = sc850sl_write_reg(sc850sl->client, SC850SL_REG_CTRL_MODE,
				SC850SL_REG_VALUE_08BIT, SC850SL_MODE_SW_STANDBY);
		break;

	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = sc850sl_get_channel_info(sc850sl, ch_info);
		break;

	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc850sl_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	struct rkmodule_channel_info *ch_info;
	long ret;
	u32  stream;
	u32 brl = 0;
	struct rkmodule_csi_dphy_param *dphy_param;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc850sl_ioctl(sd, cmd, inf);
		if (!ret) {
			if (copy_to_user(up, inf, sizeof(*inf))) {
				kfree(inf);
				return -EFAULT;
			}
		}
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		if (copy_from_user(cfg, up, sizeof(*cfg))) {
			kfree(cfg);
			return -EFAULT;
		}
		ret = sc850sl_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc850sl_ioctl(sd, cmd, hdr);
		if (!ret) {
			if (copy_to_user(up, hdr, sizeof(*hdr))) {
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

		if (copy_from_user(hdr, up, sizeof(*hdr))) {
			kfree(hdr);
			return -EFAULT;
		}
		ret = sc850sl_ioctl(sd, cmd, hdr);
		kfree(hdr);
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		hdrae = kzalloc(sizeof(*hdrae), GFP_KERNEL);
		if (!hdrae) {
			ret = -ENOMEM;
			return ret;
		}

		if (copy_from_user(hdrae, up, sizeof(*hdrae))) {
			kfree(hdrae);
			return -EFAULT;
		}
		ret = sc850sl_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;
		ret = sc850sl_ioctl(sd, cmd, &stream);
		break;
	case RKMODULE_GET_SONY_BRL:
		ret = sc850sl_ioctl(sd, cmd, &brl);
		if (!ret) {
			if (copy_to_user(up, &brl, sizeof(u32)))
				return -EFAULT;
		}
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc850sl_ioctl(sd, cmd, ch_info);
		if (!ret) {
			ret = copy_to_user(up, ch_info, sizeof(*ch_info));
			if (ret)
				ret = -EFAULT;
		}
		kfree(ch_info);
		break;
	case RKMODULE_GET_CSI_DPHY_PARAM:
		dphy_param = kzalloc(sizeof(*dphy_param), GFP_KERNEL);
		if (!dphy_param) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc850sl_ioctl(sd, cmd, dphy_param);
		if (!ret) {
			ret = copy_to_user(up, dphy_param, sizeof(*dphy_param));
			if (ret)
				ret = -EFAULT;
		}
		kfree(dphy_param);
		break;

	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif


static int __sc850sl_start_stream(struct sc850sl *sc850sl)
{
	int ret;

	ret = sc850sl_write_array(sc850sl->client, sc850sl->cur_mode->reg_list);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_handler_setup(&sc850sl->ctrl_handler);
	if (ret)
		return ret;
	/* In case these controls are set before streaming */
	if (sc850sl->has_init_exp && sc850sl->cur_mode->hdr_mode != NO_HDR) {
		ret = sc850sl_ioctl(&sc850sl->subdev, PREISP_CMD_SET_HDRAE_EXP,
			&sc850sl->init_hdrae_exp);
		if (ret) {
			dev_err(&sc850sl->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}
	return sc850sl_write_reg(sc850sl->client, SC850SL_REG_CTRL_MODE,
				SC850SL_REG_VALUE_08BIT, SC850SL_MODE_STREAMING);
}

static int __sc850sl_stop_stream(struct sc850sl *sc850sl)
{
	sc850sl->has_init_exp = false;
	return sc850sl_write_reg(sc850sl->client, SC850SL_REG_CTRL_MODE,
		SC850SL_REG_VALUE_08BIT, SC850SL_MODE_SW_STANDBY);
}

static int sc850sl_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc850sl *sc850sl = to_sc850sl(sd);
	struct i2c_client *client = sc850sl->client;
	int ret = 0;

	dev_info(&sc850sl->client->dev, "s_stream: %d. %dx%d, hdr: %d, bpp: %d\n",
	       on, sc850sl->cur_mode->width, sc850sl->cur_mode->height,
	       sc850sl->cur_mode->hdr_mode, sc850sl->cur_mode->bpp);

	mutex_lock(&sc850sl->mutex);
	on = !!on;
	if (on == sc850sl->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		ret = __sc850sl_start_stream(sc850sl);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc850sl_stop_stream(sc850sl);
		pm_runtime_put(&client->dev);
	}

	sc850sl->streaming = on;

unlock_and_return:
	mutex_unlock(&sc850sl->mutex);
	return ret;
}

static int sc850sl_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc850sl *sc850sl = to_sc850sl(sd);
	struct i2c_client *client = sc850sl->client;
	int ret = 0;

	mutex_lock(&sc850sl->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc850sl->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret |= sc850sl_write_reg(sc850sl->client,
			SC850SL_SOFTWARE_RESET_REG,
			SC850SL_REG_VALUE_08BIT,
			0x01);
		/*
		 * usleep_range(100, 200);
		 * ret |= sc850sl_write_reg(sc2310->client,
		 *	0x303f,
		 *	SC850SL_REG_VALUE_08BIT,
		 *	0x01);
		 */
		sc850sl->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc850sl->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc850sl->mutex);

	return ret;
}

static int __sc850sl_power_on(struct sc850sl *sc850sl)
{
	int ret;
	struct device *dev = &sc850sl->client->dev;

	if (!IS_ERR_OR_NULL(sc850sl->pins_default)) {
		ret = pinctrl_select_state(sc850sl->pinctrl, sc850sl->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	if (!IS_ERR(sc850sl->power_gpio))
		gpiod_direction_output(sc850sl->power_gpio, 1);

	usleep_range(4000, 6000);
	if (!IS_ERR(sc850sl->reset_gpio))
		gpiod_direction_output(sc850sl->reset_gpio, 0);

	usleep_range(4000, 6000);
	ret = clk_set_rate(sc850sl->xvclk, SC850SL_XVCLK_FREQ_24M);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate 24MHz\n");
	if (clk_get_rate(sc850sl->xvclk) != SC850SL_XVCLK_FREQ_24M)
		dev_warn(dev, "xvclk mismatched\n");
	ret = clk_prepare_enable(sc850sl->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		goto err_clk;
	}

	ret = regulator_bulk_enable(SC850SL_NUM_SUPPLIES, sc850sl->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	usleep_range(4000, 6000);
	return 0;
err_clk:
	if (!IS_ERR(sc850sl->reset_gpio))
		gpiod_direction_output(sc850sl->reset_gpio, 1);
disable_clk:
	clk_disable_unprepare(sc850sl->xvclk);

	return ret;
}

static void __sc850sl_power_off(struct sc850sl *sc850sl)
{
	int ret;
	struct device *dev = &sc850sl->client->dev;

	if (!IS_ERR(sc850sl->reset_gpio))
		gpiod_direction_output(sc850sl->reset_gpio, 1);
	clk_disable_unprepare(sc850sl->xvclk);
	if (!IS_ERR_OR_NULL(sc850sl->pins_sleep)) {
		ret = pinctrl_select_state(sc850sl->pinctrl,
					   sc850sl->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	if (!IS_ERR(sc850sl->power_gpio))
		gpiod_direction_output(sc850sl->power_gpio, 0);
	regulator_bulk_disable(SC850SL_NUM_SUPPLIES, sc850sl->supplies);
}

static int sc850sl_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc850sl *sc850sl = to_sc850sl(sd);

	return __sc850sl_power_on(sc850sl);
}

static int sc850sl_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc850sl *sc850sl = to_sc850sl(sd);

	__sc850sl_power_off(sc850sl);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc850sl_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc850sl *sc850sl = to_sc850sl(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc850sl_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc850sl->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc850sl->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sc850sl_enum_frame_interval(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_frame_interval_enum *fie)
{
	struct sc850sl *sc850sl = to_sc850sl(sd);

	if (fie->index >= sc850sl->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

#define CROP_START(SRC, DST) (((SRC) - (DST)) / 2 / 4 * 4)
#define DST_WIDTH_3840 3840
#define DST_HEIGHT_2160 2160
#define DST_WIDTH_1920 1920
#define DST_HEIGHT_1080 1080

static int sc850sl_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_selection *sel)
{
	struct sc850sl *sc850sl = to_sc850sl(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		if (sc850sl->cur_mode->width == 3856) {
			sel->r.left = CROP_START(sc850sl->cur_mode->width, DST_WIDTH_3840);
			sel->r.width = DST_WIDTH_3840;
			sel->r.top = CROP_START(sc850sl->cur_mode->height, DST_HEIGHT_2160);
			sel->r.height = DST_HEIGHT_2160;
		} else if (sc850sl->cur_mode->width == 1944) {
			sel->r.left = CROP_START(sc850sl->cur_mode->width, DST_WIDTH_1920);
			sel->r.width = DST_WIDTH_1920;
			sel->r.top = CROP_START(sc850sl->cur_mode->height, DST_HEIGHT_1080);
			sel->r.height = DST_HEIGHT_1080;
		} else {
			sel->r.left = CROP_START(sc850sl->cur_mode->width,
						 sc850sl->cur_mode->width);
			sel->r.width = sc850sl->cur_mode->width;
			sel->r.top = CROP_START(sc850sl->cur_mode->height,
						sc850sl->cur_mode->height);
			sel->r.height = sc850sl->cur_mode->height;
		}
		return 0;
	}
	return -EINVAL;
}

static const struct dev_pm_ops sc850sl_pm_ops = {
	SET_RUNTIME_PM_OPS(sc850sl_runtime_suspend,
			   sc850sl_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc850sl_internal_ops = {
	.open = sc850sl_open,
};
#endif

static const struct v4l2_subdev_core_ops sc850sl_core_ops = {
	.s_power = sc850sl_s_power,
	.ioctl = sc850sl_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc850sl_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc850sl_video_ops = {
	.s_stream = sc850sl_s_stream,
	.g_frame_interval = sc850sl_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops sc850sl_pad_ops = {
	.enum_mbus_code = sc850sl_enum_mbus_code,
	.enum_frame_size = sc850sl_enum_frame_sizes,
	.enum_frame_interval = sc850sl_enum_frame_interval,
	.get_fmt = sc850sl_get_fmt,
	.set_fmt = sc850sl_set_fmt,
	.get_selection = sc850sl_get_selection,
	.get_mbus_config = sc850sl_g_mbus_config,
};

static const struct v4l2_subdev_ops sc850sl_subdev_ops = {
	.core	= &sc850sl_core_ops,
	.video	= &sc850sl_video_ops,
	.pad	= &sc850sl_pad_ops,
};

static void sc850sl_modify_fps_info(struct sc850sl *sc850sl)
{
	const struct sc850sl_mode *mode = sc850sl->cur_mode;

	sc850sl->cur_fps.denominator = mode->max_fps.denominator * sc850sl->cur_vts /
				       mode->vts_def;
}

static int sc850sl_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc850sl *sc850sl = container_of(ctrl->handler,
					     struct sc850sl, ctrl_handler);
	struct i2c_client *client = sc850sl->client;
	s64 max;
	u32 again, again_fine, dgain;
	int ret = 0;
	u32 val;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc850sl->cur_mode->height + ctrl->val - 8;
		__v4l2_ctrl_modify_range(sc850sl->exposure,
					 sc850sl->exposure->minimum, max,
					 sc850sl->exposure->step,
					 sc850sl->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (sc850sl->cur_mode->hdr_mode != NO_HDR)
			goto out_ctrl;
		ret = sc850sl_write_reg(sc850sl->client,
					SC850SL_REG_EXP_LONG_H,
					SC850SL_REG_VALUE_08BIT,
					SC850SL_FETCH_EXP_H(ctrl->val));
		ret |= sc850sl_write_reg(sc850sl->client,
					SC850SL_REG_EXP_LONG_M,
					SC850SL_REG_VALUE_08BIT,
					SC850SL_FETCH_EXP_M(ctrl->val));
		ret |= sc850sl_write_reg(sc850sl->client,
					 SC850SL_REG_EXP_LONG_L,
					 SC850SL_REG_VALUE_08BIT,
					 SC850SL_FETCH_EXP_L(ctrl->val));

		dev_dbg(&client->dev, "set exposure 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (sc850sl->cur_mode->hdr_mode != NO_HDR)
			goto out_ctrl;
		sc850sl_get_gain_reg(ctrl->val, &again, &again_fine, &dgain);
		dev_dbg(&client->dev, "recv_gain:%d set again 0x%x, again_fine 0x%x, set dgain 0x%x\n",
			ctrl->val, again, again_fine, dgain);

		ret |= sc850sl_write_reg(sc850sl->client,
					SC850SL_REG_AGAIN,
					SC850SL_REG_VALUE_08BIT,
					again);
		ret |= sc850sl_write_reg(sc850sl->client,
					SC850SL_REG_AGAIN_FINE,
					SC850SL_REG_VALUE_08BIT,
					again_fine);
		ret |= sc850sl_write_reg(sc850sl->client,
					SC850SL_REG_DGAIN,
					SC850SL_REG_VALUE_08BIT,
					dgain);
		break;
	case V4L2_CID_VBLANK:
		ret = sc850sl_write_reg(sc850sl->client, SC850SL_REG_VTS,
					SC850SL_REG_VALUE_16BIT,
					ctrl->val + sc850sl->cur_mode->height);
		if (!ret)
			sc850sl->cur_vts = ctrl->val + sc850sl->cur_mode->height;
		sc850sl_modify_fps_info(sc850sl);
		dev_dbg(&client->dev, "set vblank 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = sc850sl_enable_test_pattern(sc850sl, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = sc850sl_read_reg(sc850sl->client, SC850SL_FLIP_REG,
				      SC850SL_REG_VALUE_08BIT, &val);
		if (ret)
			break;
		if (ctrl->val)
			val |= SC850SL_MIRROR_MASK;
		else
			val &= ~SC850SL_MIRROR_MASK;
		ret |= sc850sl_write_reg(sc850sl->client, SC850SL_FLIP_REG,
					SC850SL_REG_VALUE_08BIT, val);
		break;
	case V4L2_CID_VFLIP:
		ret = sc850sl_read_reg(sc850sl->client, SC850SL_FLIP_REG,
				      SC850SL_REG_VALUE_08BIT, &val);
		if (ret)
			break;
		if (ctrl->val)
			val |= SC850SL_FLIP_MASK;
		else
			val &= ~SC850SL_FLIP_MASK;
		ret |= sc850sl_write_reg(sc850sl->client, SC850SL_FLIP_REG,
					SC850SL_REG_VALUE_08BIT, val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

out_ctrl:
	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc850sl_ctrl_ops = {
	.s_ctrl = sc850sl_set_ctrl,
};

static int sc850sl_initialize_controls(struct sc850sl *sc850sl)
{
	const struct sc850sl_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u64 pixel_rate = 0;
	u32 h_blank;
	int ret;

	handler = &sc850sl->ctrl_handler;
	mode = sc850sl->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &sc850sl->mutex;

	sc850sl->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ, 0, 0, link_freq_items);
	v4l2_ctrl_s_ctrl(sc850sl->link_freq, mode->mipi_freq_idx);

	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] / mode->bpp * 2 * SC850SL_4LANES;
	sc850sl->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
		V4L2_CID_PIXEL_RATE, 0, SC850SL_MAX_PIXEL_RATE,
		1, pixel_rate);

	h_blank = mode->hts_def - mode->width;
	sc850sl->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (sc850sl->hblank)
		sc850sl->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	sc850sl->vblank = v4l2_ctrl_new_std(handler, &sc850sl_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				SC850SL_VTS_MAX - mode->height,
				1, vblank_def);

	 exposure_max = mode->vts_def - 4;	/*vts_def  0x08ca=2250*/
	 sc850sl->exposure = v4l2_ctrl_new_std(handler, &sc850sl_ctrl_ops,
				V4L2_CID_EXPOSURE, SC850SL_EXPOSURE_MIN,
				exposure_max, SC850SL_EXPOSURE_STEP,
				mode->exp_def);	/*exp_def 0x08c0=2240*/

	sc850sl->anal_a_gain = v4l2_ctrl_new_std(handler, &sc850sl_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, SC850SL_GAIN_MIN,
				SC850SL_GAIN_MAX, SC850SL_GAIN_STEP,
				SC850SL_GAIN_DEFAULT);

	sc850sl->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&sc850sl_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(sc850sl_test_pattern_menu) - 1,
				0, 0, sc850sl_test_pattern_menu);

	v4l2_ctrl_new_std(handler, &sc850sl_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &sc850sl_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&sc850sl->client->dev,
		"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	sc850sl->subdev.ctrl_handler = handler;
	sc850sl->has_init_exp = false;
	sc850sl->cur_fps = mode->max_fps;
	sc850sl->cur_vts = mode->vts_def;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int sc850sl_check_sensor_id(struct sc850sl *sc850sl,
				  struct i2c_client *client)
{
	struct device *dev = &sc850sl->client->dev;
	u32 id = 0;
	int ret;

	ret = sc850sl_read_reg(client, SC850SL_REG_CHIP_ID,
		SC850SL_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected sc850sl id %06x\n", CHIP_ID);

	return 0;
}

static int sc850sl_configure_regulators(struct sc850sl *sc850sl)
{
	unsigned int i;

	for (i = 0; i < SC850SL_NUM_SUPPLIES; i++)
		sc850sl->supplies[i].supply = sc850sl_supply_names[i];

	return devm_regulator_bulk_get(&sc850sl->client->dev,
				       SC850SL_NUM_SUPPLIES,
				       sc850sl->supplies);
}

static int sc850sl_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc850sl *sc850sl;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	sc850sl = devm_kzalloc(dev, sizeof(*sc850sl), GFP_KERNEL);
	if (!sc850sl)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc850sl->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc850sl->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc850sl->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc850sl->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}

	sc850sl->client = client;
	sc850sl->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < sc850sl->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			sc850sl->cur_mode = &supported_modes[i];
			break;
		}
	}

	sc850sl->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc850sl->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	sc850sl->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(sc850sl->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");
	sc850sl->power_gpio = devm_gpiod_get(dev, "power", GPIOD_ASIS);
	if (IS_ERR(sc850sl->power_gpio))
		dev_warn(dev, "Failed to get power_gpios\n");

	sc850sl->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc850sl->pinctrl)) {
		sc850sl->pins_default =
			pinctrl_lookup_state(sc850sl->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc850sl->pins_default))
			dev_info(dev, "could not get default pinstate\n");

		sc850sl->pins_sleep =
			pinctrl_lookup_state(sc850sl->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc850sl->pins_sleep))
			dev_info(dev, "could not get sleep pinstate\n");
	} else {
		dev_info(dev, "no pinctrl\n");
	}

	ret = sc850sl_configure_regulators(sc850sl);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&sc850sl->mutex);

	sd = &sc850sl->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc850sl_subdev_ops);
	ret = sc850sl_initialize_controls(sc850sl);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc850sl_power_on(sc850sl);
	if (ret)
		goto err_free_handler;

	ret = sc850sl_check_sensor_id(sc850sl, client);
	if (ret)
		goto err_power_off;
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc850sl_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc850sl->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc850sl->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc850sl->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';
	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sc850sl->module_index, facing,
		 SC850SL_NAME, dev_name(sd->dev));

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
	__sc850sl_power_off(sc850sl);
err_free_handler:
	v4l2_ctrl_handler_free(&sc850sl->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc850sl->mutex);

	return ret;
}

static int sc850sl_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc850sl *sc850sl = to_sc850sl(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc850sl->ctrl_handler);
	mutex_destroy(&sc850sl->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc850sl_power_off(sc850sl);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc850sl_of_match[] = {
	{ .compatible = "smartsens,sc850sl" },
	{},
};
MODULE_DEVICE_TABLE(of, sc850sl_of_match);
#endif

static const struct i2c_device_id sc850sl_match_id[] = {
	{ "smartsens,sc850sl", 0 },
	{ },
};

static struct i2c_driver sc850sl_i2c_driver = {
	.driver = {
		.name = SC850SL_NAME,
		.pm = &sc850sl_pm_ops,
		.of_match_table = of_match_ptr(sc850sl_of_match),
	},
	.probe		= &sc850sl_probe,
	.remove		= &sc850sl_remove,
	.id_table	= sc850sl_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc850sl_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc850sl_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("smartsens,sc850sl sensor driver");
MODULE_LICENSE("GPL");
