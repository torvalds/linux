// SPDX-License-Identifier: GPL-2.0
/*
 * sc230ai driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define SC230AI_LANES			2
#define SC230AI_BITS_PER_SAMPLE		10
#define SC230AI_LINK_FREQ_185		92812500// 185.625Mbps
#define SC230AI_LINK_FREQ_371		185625000// 371.25Mbps

#define PIXEL_RATE_WITH_185M_10BIT		(SC230AI_LINK_FREQ_185 * 2 * \
					SC230AI_LANES / SC230AI_BITS_PER_SAMPLE)

#define PIXEL_RATE_WITH_371M_10BIT		(SC230AI_LINK_FREQ_371 * 2 * \
					SC230AI_LANES / SC230AI_BITS_PER_SAMPLE)

#define SC230AI_XVCLK_FREQ		27000000

#define CHIP_ID				0xcb34
#define SC230AI_REG_CHIP_ID		0x3107

#define SC230AI_REG_CTRL_MODE		0x0100
#define SC230AI_MODE_SW_STANDBY		0x0
#define SC230AI_MODE_STREAMING		BIT(0)

#define SC230AI_REG_EXPOSURE_H		0x3e00
#define SC230AI_REG_EXPOSURE_M		0x3e01
#define SC230AI_REG_EXPOSURE_L		0x3e02
#define SC230AI_REG_SEXPOSURE_H		0x3e22
#define SC230AI_REG_SEXPOSURE_M		0x3e04
#define SC230AI_REG_SEXPOSURE_L		0x3e05
#define	SC230AI_EXPOSURE_MIN		1
#define	SC230AI_EXPOSURE_STEP		1
#define	SC230AI_EXPOSURE_LIN_MAX	(2 * 0x465 - 9)
#define	SC230AI_EXPOSURE_HDR_MAX_S	(2 * 0x465 - 9)
#define	SC230AI_EXPOSURE_HDR_MAX_L	(2 * 0x465 - 9)
#define SC230AI_VTS_MAX			0x7fff

#define SC230AI_REG_DIG_GAIN		0x3e06
#define SC230AI_REG_DIG_FINE_GAIN	0x3e07
#define SC230AI_REG_ANA_GAIN		0x3e09
#define SC230AI_REG_SDIG_GAIN		0x3e10
#define SC230AI_REG_SDIG_FINE_GAIN	0x3e11
#define SC230AI_REG_SANA_GAIN		0x3e12
#define SC230AI_REG_SANA_FINE_GAIN	0x3e13
#define SC230AI_GAIN_MIN		1000
#define SC230AI_GAIN_MAX		1722628       //108.512*15.875*1000
#define SC230AI_GAIN_STEP		1
#define SC230AI_GAIN_DEFAULT		1000
#define SC230AI_LGAIN			0
#define SC230AI_SGAIN			1

#define SC230AI_REG_GROUP_HOLD		0x3812
#define SC230AI_GROUP_HOLD_START	0x00
#define SC230AI_GROUP_HOLD_END		0x30

#define SC230AI_REG_HIGH_TEMP_H		0x3974
#define SC230AI_REG_HIGH_TEMP_L		0x3975

#define SC230AI_REG_TEST_PATTERN	0x4501
#define SC230AI_TEST_PATTERN_BIT_MASK	BIT(3)

#define SC230AI_REG_VTS_H		0x320e
#define SC230AI_REG_VTS_L		0x320f

#define SC230AI_FLIP_MIRROR_REG		0x3221

#define SC230AI_FETCH_EXP_H(VAL)		(((VAL) >> 12) & 0xF)
#define SC230AI_FETCH_EXP_M(VAL)		(((VAL) >> 4) & 0xFF)
#define SC230AI_FETCH_EXP_L(VAL)		(((VAL) & 0xF) << 4)

#define SC230AI_FETCH_AGAIN_H(VAL)		(((VAL) >> 8) & 0x03)
#define SC230AI_FETCH_AGAIN_L(VAL)		((VAL) & 0xFF)

#define SC230AI_FETCH_MIRROR(VAL, ENABLE)	(ENABLE ? VAL | 0x06 : VAL & 0xf9)
#define SC230AI_FETCH_FLIP(VAL, ENABLE)		(ENABLE ? VAL | 0x60 : VAL & 0x9f)

#define REG_DELAY			0xFFFE
#define REG_NULL			0xFFFF

#define SC230AI_REG_VALUE_08BIT		1
#define SC230AI_REG_VALUE_16BIT		2
#define SC230AI_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"
#define SC230AI_NAME			"sc230ai"

static const char * const sc230ai_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define SC230AI_NUM_SUPPLIES ARRAY_SIZE(sc230ai_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct sc230ai_mode {
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

struct sc230ai {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[SC230AI_NUM_SUPPLIES];

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
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct sc230ai_mode *cur_mode;
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

#define to_sc230ai(sd) container_of(sd, struct sc230ai, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval sc230ai_global_regs[] = {
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 15fps
 * mipi_datarate per lane 74.25Mbps, 2lane
 */
static const struct regval sc230ai_linear_10_1920x1080_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x37f9, 0x80},
	{0x301f, 0x0f},
	{0x3301, 0x07},
	{0x3304, 0x50},
	{0x3306, 0x70},
	{0x3308, 0x0c},
	{0x3309, 0x68},
	{0x330a, 0x01},
	{0x330b, 0x20},
	{0x330d, 0x16},
	{0x3314, 0x15},
	{0x331e, 0x41},
	{0x331f, 0x59},
	{0x3333, 0x10},
	{0x3334, 0x40},
	{0x335d, 0x60},
	{0x335e, 0x06},
	{0x335f, 0x08},
	{0x3364, 0x5e},
	{0x337c, 0x02},
	{0x337d, 0x0a},
	{0x3390, 0x01},
	{0x3391, 0x0b},
	{0x3392, 0x0f},
	{0x3393, 0x09},
	{0x3394, 0x0d},
	{0x3395, 0x60},
	{0x3396, 0x48},
	{0x3397, 0x49},
	{0x3398, 0x4b},
	{0x3399, 0x06},
	{0x339a, 0x0a},
	{0x339b, 0x0d},
	{0x339c, 0x60},
	{0x33a2, 0x04},
	{0x33ad, 0x2c},
	{0x33af, 0x40},
	{0x33b1, 0x80},
	{0x33b3, 0x40},
	{0x33b9, 0x0a},
	{0x33f9, 0x78},
	{0x33fb, 0xa0},
	{0x33fc, 0x4f},
	{0x33fd, 0x5f},
	{0x349f, 0x03},
	{0x34a6, 0x4b},
	{0x34a7, 0x5f},
	{0x34a8, 0x30},
	{0x34a9, 0x20},
	{0x34aa, 0x01},
	{0x34ab, 0x28},
	{0x34ac, 0x01},
	{0x34ad, 0x50},
	{0x34f8, 0x7f},
	{0x34f9, 0x10},
	{0x3630, 0xc0},
	{0x3632, 0x54},
	{0x3633, 0x44},
	{0x363b, 0x20},
	{0x363c, 0x08},
	{0x3670, 0x09},
	{0x3674, 0xb0},
	{0x3675, 0x80},
	{0x3676, 0x88},
	{0x367c, 0x40},
	{0x367d, 0x49},
	{0x3690, 0x33},
	{0x3691, 0x33},
	{0x3692, 0x43},
	{0x369c, 0x49},
	{0x369d, 0x4f},
	{0x36ae, 0x4b},
	{0x36af, 0x4f},
	{0x36b0, 0x87},
	{0x36b1, 0x9b},
	{0x36b2, 0xb7},
	{0x36d0, 0x01},
	{0x36eb, 0x1c},
	{0x3722, 0x97},
	{0x3724, 0x22},
	{0x3728, 0x90},
	{0x3901, 0x02},
	{0x3902, 0xc5},
	{0x3904, 0x04},
	{0x3907, 0x00},
	{0x3908, 0x41},
	{0x3909, 0x00},
	{0x390a, 0x00},
	{0x3933, 0x84},
	{0x3934, 0x0a},
	{0x3940, 0x64},
	{0x3941, 0x00},
	{0x3942, 0x04},
	{0x3943, 0x0b},
	{0x3e00, 0x00},
	{0x3e01, 0x8c},
	{0x3e02, 0x10},
	{0x440e, 0x02},
	{0x450d, 0x11},
	{0x4819, 0x03},
	{0x481b, 0x02},
	{0x481d, 0x05},
	{0x481f, 0x01},
	{0x4821, 0x07},
	{0x4823, 0x02},
	{0x4825, 0x01},
	{0x4827, 0x02},
	{0x4829, 0x02},
	{0x5010, 0x01},
	{0x5787, 0x08},
	{0x5788, 0x03},
	{0x5789, 0x00},
	{0x578a, 0x10},
	{0x578b, 0x08},
	{0x578c, 0x00},
	{0x5790, 0x08},
	{0x5791, 0x04},
	{0x5792, 0x00},
	{0x5793, 0x10},
	{0x5794, 0x08},
	{0x5795, 0x00},
	{0x5799, 0x06},
	{0x57ad, 0x00},
	{0x5ae0, 0xfe},
	{0x5ae1, 0x40},
	{0x5ae2, 0x3f},
	{0x5ae3, 0x38},
	{0x5ae4, 0x28},
	{0x5ae5, 0x3f},
	{0x5ae6, 0x38},
	{0x5ae7, 0x28},
	{0x5ae8, 0x3f},
	{0x5ae9, 0x3c},
	{0x5aea, 0x2c},
	{0x5aeb, 0x3f},
	{0x5aec, 0x3c},
	{0x5aed, 0x2c},
	{0x5af4, 0x3f},
	{0x5af5, 0x38},
	{0x5af6, 0x28},
	{0x5af7, 0x3f},
	{0x5af8, 0x38},
	{0x5af9, 0x28},
	{0x5afa, 0x3f},
	{0x5afb, 0x3c},
	{0x5afc, 0x2c},
	{0x5afd, 0x3f},
	{0x5afe, 0x3c},
	{0x5aff, 0x2c},
	{0x36e9, 0x20},
	{0x37f9, 0x57},
	{REG_NULL, 0x00},
};

static const struct regval sc230ai_linear_10_640x480_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x37f9, 0x80},
	{0x301f, 0x2d},
	{0x3200, 0x00},
	{0x3201, 0x00},
	{0x3202, 0x00},
	{0x3203, 0x3c},
	{0x3204, 0x07},
	{0x3205, 0x87},
	{0x3206, 0x04},
	{0x3207, 0x03},
	{0x3208, 0x02},
	{0x3209, 0x80},
	{0x320a, 0x01},
	{0x320b, 0xe0},
	{0x320e, 0x02},
	{0x320f, 0x32},
	{0x3210, 0x00},
	{0x3211, 0xa2},
	{0x3212, 0x00},
	{0x3213, 0x02},
	{0x3215, 0x31},
	{0x3220, 0x01},
	{0x3301, 0x09},
	{0x3304, 0x50},
	{0x3306, 0x48},
	{0x3308, 0x18},
	{0x3309, 0x68},
	{0x330a, 0x00},
	{0x330b, 0xc0},
	{0x331e, 0x41},
	{0x331f, 0x59},
	{0x3333, 0x10},
	{0x3334, 0x40},
	{0x335d, 0x60},
	{0x335e, 0x06},
	{0x335f, 0x08},
	{0x3364, 0x5e},
	{0x337c, 0x02},
	{0x337d, 0x0a},
	{0x3390, 0x01},
	{0x3391, 0x0b},
	{0x3392, 0x0f},
	{0x3393, 0x0c},
	{0x3394, 0x0d},
	{0x3395, 0x60},
	{0x3396, 0x48},
	{0x3397, 0x49},
	{0x3398, 0x4f},
	{0x3399, 0x0a},
	{0x339a, 0x0f},
	{0x339b, 0x14},
	{0x339c, 0x60},
	{0x33a2, 0x04},
	{0x33af, 0x40},
	{0x33b1, 0x80},
	{0x33b3, 0x40},
	{0x33b9, 0x0a},
	{0x33f9, 0x70},
	{0x33fb, 0x90},
	{0x33fc, 0x4b},
	{0x33fd, 0x5f},
	{0x349f, 0x03},
	{0x34a6, 0x4b},
	{0x34a7, 0x4f},
	{0x34a8, 0x30},
	{0x34a9, 0x20},
	{0x34aa, 0x00},
	{0x34ab, 0xe0},
	{0x34ac, 0x01},
	{0x34ad, 0x00},
	{0x34f8, 0x5f},
	{0x34f9, 0x10},
	{0x3630, 0xc0},
	{0x3633, 0x44},
	{0x3637, 0x29},
	{0x363b, 0x20},
	{0x3670, 0x09},
	{0x3674, 0xb0},
	{0x3675, 0x80},
	{0x3676, 0x88},
	{0x367c, 0x40},
	{0x367d, 0x49},
	{0x3690, 0x44},
	{0x3691, 0x44},
	{0x3692, 0x54},
	{0x369c, 0x49},
	{0x369d, 0x4f},
	{0x36ae, 0x4b},
	{0x36af, 0x4f},
	{0x36b0, 0x87},
	{0x36b1, 0x9b},
	{0x36b2, 0xb7},
	{0x36d0, 0x01},
	{0x36ea, 0x0b},
	{0x36eb, 0x04},
	{0x36ec, 0x1c},
	{0x36ed, 0x24},
	{0x370f, 0x01},
	{0x3722, 0x17},
	{0x3728, 0x90},
	{0x37b0, 0x17},
	{0x37b1, 0x17},
	{0x37b2, 0x97},
	{0x37b3, 0x4b},
	{0x37b4, 0x4f},
	{0x37fa, 0x0b},
	{0x37fb, 0x24},
	{0x37fc, 0x10},
	{0x37fd, 0x22},
	{0x3901, 0x02},
	{0x3902, 0xc5},
	{0x3904, 0x04},
	{0x3907, 0x00},
	{0x3908, 0x41},
	{0x3909, 0x00},
	{0x390a, 0x00},
	{0x391f, 0x04},
	{0x3933, 0x84},
	{0x3934, 0x02},
	{0x3940, 0x62},
	{0x3941, 0x00},
	{0x3942, 0x04},
	{0x3943, 0x03},
	{0x3e00, 0x00},
	{0x3e01, 0x45},
	{0x3e02, 0xb0},
	{0x440e, 0x02},
	{0x450d, 0x11},
	{0x4819, 0x05},
	{0x481b, 0x03},
	{0x481d, 0x0a},
	{0x481f, 0x02},
	{0x4821, 0x08},
	{0x4823, 0x03},
	{0x4825, 0x02},
	{0x4827, 0x03},
	{0x4829, 0x04},
	{0x5000, 0x46},
	{0x5010, 0x01},
	{0x5787, 0x08},
	{0x5788, 0x03},
	{0x5789, 0x00},
	{0x578a, 0x10},
	{0x578b, 0x08},
	{0x578c, 0x00},
	{0x5790, 0x08},
	{0x5791, 0x04},
	{0x5792, 0x00},
	{0x5793, 0x10},
	{0x5794, 0x08},
	{0x5795, 0x00},
	{0x5799, 0x06},
	{0x57ad, 0x00},
	{0x5900, 0xf1},
	{0x5901, 0x04},
	{0x5ae0, 0xfe},
	{0x5ae1, 0x40},
	{0x5ae2, 0x3f},
	{0x5ae3, 0x38},
	{0x5ae4, 0x28},
	{0x5ae5, 0x3f},
	{0x5ae6, 0x38},
	{0x5ae7, 0x28},
	{0x5ae8, 0x3f},
	{0x5ae9, 0x3c},
	{0x5aea, 0x2c},
	{0x5aeb, 0x3f},
	{0x5aec, 0x3c},
	{0x5aed, 0x2c},
	{0x5af4, 0x3f},
	{0x5af5, 0x38},
	{0x5af6, 0x28},
	{0x5af7, 0x3f},
	{0x5af8, 0x38},
	{0x5af9, 0x28},
	{0x5afa, 0x3f},
	{0x5afb, 0x3c},
	{0x5afc, 0x2c},
	{0x5afd, 0x3f},
	{0x5afe, 0x3c},
	{0x5aff, 0x2c},
	{0x36e9, 0x20},
	{0x37f9, 0x24},
	{REG_NULL, 0x00},
};

static const struct sc230ai_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 150000,
		},
		.exp_def = 0x0460,
		.hts_def = 0x44C * 2,
		.vts_def = 0x0465,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc230ai_linear_10_1920x1080_regs,
		.hdr_mode = NO_HDR,
		.bpp = 10,
		.mipi_freq_idx = 0,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}, {
		.width = 640,
		.height = 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 1200000,
		},
		.exp_def = 0x0232 - 9,
		.hts_def = 0x96 * 8,
		.vts_def = 0x0232,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc230ai_linear_10_640x480_regs,
		.hdr_mode = NO_HDR,
		.bpp = 10,
		.mipi_freq_idx = 1,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	SC230AI_LINK_FREQ_185,
	SC230AI_LINK_FREQ_371
};

static const char * const sc230ai_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int sc230ai_write_reg(struct i2c_client *client, u16 reg,
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

static int sc230ai_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = sc230ai_write_reg(client, regs[i].addr,
					SC230AI_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int sc230ai_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static int sc230ai_get_gain_reg(struct sc230ai *sc230ai, u32 *again, u32 *dgain,
				u32 *dgain_fine, u32 total_gain)
{
	int ret = 0;

	if (total_gain < SC230AI_GAIN_MIN)
		total_gain = SC230AI_GAIN_MIN;
	else if (total_gain > SC230AI_GAIN_MAX)
		total_gain = SC230AI_GAIN_MAX;

	if (total_gain < 2000) {	/* 1 ~ 2 gain*/
		*again = 0x00;
		*dgain = 0x00;
		*dgain_fine = total_gain * 128 / 1000;
	} else if (total_gain < 3391) {	/* 2 ~ 3.391 gain*/
		*again = 0x01;
		*dgain = 0x00;
		*dgain_fine = total_gain * 128 / 1000 / 2;
	} else if (total_gain < 3391 * 2) {	/* 3.391 ~ 6.782 gain*/
		*again = 0x40;
		*dgain = 0x00;
		*dgain_fine = total_gain * 128 / 3391;
	} else if (total_gain < 3391 * 4) {	/* 6.782 ~ 13.564 gain*/
		*again = 0x48;
		*dgain = 0x00;
		*dgain_fine = total_gain * 128 / 3391 / 2;
	} else if (total_gain < 3391 * 8) {	/* 13.564 ~ 27.128 gain*/
		*again = 0x49;
		*dgain = 0x00;
		*dgain_fine = total_gain * 128 / 3391 / 4;
	} else if (total_gain < 3391 * 16) {	/* 27.128 ~ 54.256 gain*/
		*again = 0x4b;
		*dgain = 0x00;
		*dgain_fine = total_gain * 128 / 3391 / 8;
	} else if (total_gain < 3391 * 32) {	/* 54.256 ~ 108.512 gain*/
		*again = 0x4f;
		*dgain = 0x00;
		*dgain_fine = total_gain * 128 / 3391 / 16;
	} else if (total_gain < 3391 * 64) {	/* 108.512 ~ 217.024 gain*/
		*again = 0x5f;
		*dgain = 0x00;
		*dgain_fine = total_gain * 128 / 3391 / 32;
	} else if (total_gain < 3391 * 128) {	/* 217.024 ~ 434.048 gain*/
		*again = 0x5f;
		*dgain = 0x01;
		*dgain_fine = total_gain * 128 / 3391 / 64;
	} else if (total_gain < 3391 * 256) {	/* 434.048 ~ 868.096 gain*/
		*again = 0x5f;
		*dgain = 0x03;
		*dgain_fine = total_gain * 128 / 3391 / 128;
	} else if (total_gain < 3391 * 512) {	/* 868.096 ~ 1736.192 gain*/
		*again = 0x5f;
		*dgain = 0x07;
		*dgain_fine = total_gain * 128 / 3391 / 128;
	}

	return ret;
}

static int sc230ai_set_hdrae(struct sc230ai *sc230ai,
			    struct preisp_hdrae_exp_s *ae)
{
	int ret = 0;

	return ret;
}

static int sc230ai_get_reso_dist(const struct sc230ai_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc230ai_mode *
sc230ai_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = sc230ai_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int sc230ai_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc230ai *sc230ai = to_sc230ai(sd);
	const struct sc230ai_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;

	mutex_lock(&sc230ai->mutex);

	mode = sc230ai_find_best_fit(fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc230ai->mutex);
		return -ENOTTY;
#endif
	} else {
		sc230ai->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc230ai->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc230ai->vblank, vblank_def,
					 SC230AI_VTS_MAX - mode->height,
					 1, vblank_def);

		__v4l2_ctrl_s_ctrl(sc230ai->link_freq, mode->mipi_freq_idx);
		pixel_rate = (u32)link_freq_menu_items[mode->mipi_freq_idx] /
			     mode->bpp * 2 * SC230AI_LANES;
		__v4l2_ctrl_s_ctrl_int64(sc230ai->pixel_rate, pixel_rate);
	}

	mutex_unlock(&sc230ai->mutex);

	return 0;
}

static int sc230ai_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc230ai *sc230ai = to_sc230ai(sd);
	const struct sc230ai_mode *mode = sc230ai->cur_mode;

	mutex_lock(&sc230ai->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc230ai->mutex);
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
	mutex_unlock(&sc230ai->mutex);

	return 0;
}

static int sc230ai_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct sc230ai *sc230ai = to_sc230ai(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = sc230ai->cur_mode->bus_fmt;

	return 0;
}

static int sc230ai_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int sc230ai_enable_test_pattern(struct sc230ai *sc230ai, u32 pattern)
{
	u32 val = 0;
	int ret = 0;

	ret = sc230ai_read_reg(sc230ai->client, SC230AI_REG_TEST_PATTERN,
			       SC230AI_REG_VALUE_08BIT, &val);
	if (pattern)
		val |= SC230AI_TEST_PATTERN_BIT_MASK;
	else
		val &= ~SC230AI_TEST_PATTERN_BIT_MASK;

	ret |= sc230ai_write_reg(sc230ai->client, SC230AI_REG_TEST_PATTERN,
				 SC230AI_REG_VALUE_08BIT, val);
	return ret;
}

static int sc230ai_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct sc230ai *sc230ai = to_sc230ai(sd);
	const struct sc230ai_mode *mode = sc230ai->cur_mode;

	mutex_lock(&sc230ai->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&sc230ai->mutex);

	return 0;
}

static int sc230ai_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				 struct v4l2_mbus_config *config)
{
	struct sc230ai *sc230ai = to_sc230ai(sd);
	const struct sc230ai_mode *mode = sc230ai->cur_mode;
	u32 val = 1 << (SC230AI_LANES - 1) |
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

static void sc230ai_get_module_inf(struct sc230ai *sc230ai,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, SC230AI_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, sc230ai->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, sc230ai->len_name, sizeof(inf->base.lens));
}

static long sc230ai_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc230ai *sc230ai = to_sc230ai(sd);
	struct rkmodule_hdr_cfg *hdr;
	u32 i, h, w;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		sc230ai_get_module_inf(sc230ai, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = sc230ai->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = sc230ai->cur_mode->width;
		h = sc230ai->cur_mode->height;
		for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				sc230ai->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == ARRAY_SIZE(supported_modes)) {
			dev_err(&sc230ai->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = sc230ai->cur_mode->hts_def - sc230ai->cur_mode->width;
			h = sc230ai->cur_mode->vts_def - sc230ai->cur_mode->height;
			__v4l2_ctrl_modify_range(sc230ai->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(sc230ai->vblank, h,
						 SC230AI_VTS_MAX - sc230ai->cur_mode->height, 1, h);
		}
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		sc230ai_set_hdrae(sc230ai, arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = sc230ai_write_reg(sc230ai->client, SC230AI_REG_CTRL_MODE,
				 SC230AI_REG_VALUE_08BIT, SC230AI_MODE_STREAMING);
		else
			ret = sc230ai_write_reg(sc230ai->client, SC230AI_REG_CTRL_MODE,
				 SC230AI_REG_VALUE_08BIT, SC230AI_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc230ai_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = sc230ai_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				return -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc230ai_ioctl(sd, cmd, hdr);
		if (!ret) {
			ret = copy_to_user(up, hdr, sizeof(*hdr));
			if (ret)
				return -EFAULT;
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

		ret = sc230ai_ioctl(sd, cmd, hdr);
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

		ret = sc230ai_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;

		ret = sc230ai_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __sc230ai_start_stream(struct sc230ai *sc230ai)
{
	int ret;

	if (!sc230ai->is_thunderboot) {
		ret = sc230ai_write_array(sc230ai->client, sc230ai->cur_mode->reg_list);
		if (ret)
			return ret;

		/* In case these controls are set before streaming */
		ret = __v4l2_ctrl_handler_setup(&sc230ai->ctrl_handler);
		if (ret)
			return ret;
		if (sc230ai->has_init_exp && sc230ai->cur_mode->hdr_mode != NO_HDR) {
			ret = sc230ai_ioctl(&sc230ai->subdev, PREISP_CMD_SET_HDRAE_EXP,
				&sc230ai->init_hdrae_exp);
			if (ret) {
				dev_err(&sc230ai->client->dev,
					"init exp fail in hdr mode\n");
				return ret;
			}
		}
	}
	return sc230ai_write_reg(sc230ai->client, SC230AI_REG_CTRL_MODE,
				 SC230AI_REG_VALUE_08BIT, SC230AI_MODE_STREAMING);
}

static int __sc230ai_stop_stream(struct sc230ai *sc230ai)
{
	sc230ai->has_init_exp = false;
	if (sc230ai->is_thunderboot)
		sc230ai->is_first_streamoff = true;
	return sc230ai_write_reg(sc230ai->client, SC230AI_REG_CTRL_MODE,
				 SC230AI_REG_VALUE_08BIT, SC230AI_MODE_SW_STANDBY);
}

static int __sc230ai_power_on(struct sc230ai *sc230ai);
static int sc230ai_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc230ai *sc230ai = to_sc230ai(sd);
	struct i2c_client *client = sc230ai->client;
	int ret = 0;

	mutex_lock(&sc230ai->mutex);
	on = !!on;
	if (on == sc230ai->streaming)
		goto unlock_and_return;

	if (on) {
		if (sc230ai->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
			sc230ai->is_thunderboot = false;
			__sc230ai_power_on(sc230ai);
		}
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __sc230ai_start_stream(sc230ai);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc230ai_stop_stream(sc230ai);
		pm_runtime_put(&client->dev);
	}

	sc230ai->streaming = on;

unlock_and_return:
	mutex_unlock(&sc230ai->mutex);

	return ret;
}

static int sc230ai_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc230ai *sc230ai = to_sc230ai(sd);
	struct i2c_client *client = sc230ai->client;
	int ret = 0;

	mutex_lock(&sc230ai->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc230ai->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		if (!sc230ai->is_thunderboot) {
			ret = sc230ai_write_array(sc230ai->client, sc230ai_global_regs);
			if (ret) {
				v4l2_err(sd, "could not set init registers\n");
				pm_runtime_put_noidle(&client->dev);
				goto unlock_and_return;
			}
		}

		sc230ai->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc230ai->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc230ai->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 sc230ai_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, SC230AI_XVCLK_FREQ / 1000 / 1000);
}

static int __sc230ai_power_on(struct sc230ai *sc230ai)
{
	int ret;
	u32 delay_us;
	struct device *dev = &sc230ai->client->dev;

	if (!IS_ERR_OR_NULL(sc230ai->pins_default)) {
		ret = pinctrl_select_state(sc230ai->pinctrl,
					   sc230ai->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(sc230ai->xvclk, SC230AI_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(sc230ai->xvclk) != SC230AI_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc230ai->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (sc230ai->is_thunderboot)
		return 0;

	if (!IS_ERR(sc230ai->reset_gpio))
		gpiod_set_value_cansleep(sc230ai->reset_gpio, 0);

	ret = regulator_bulk_enable(SC230AI_NUM_SUPPLIES, sc230ai->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(sc230ai->reset_gpio))
		gpiod_set_value_cansleep(sc230ai->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(sc230ai->pwdn_gpio))
		gpiod_set_value_cansleep(sc230ai->pwdn_gpio, 1);

	if (!IS_ERR(sc230ai->reset_gpio))
		usleep_range(6000, 8000);
	else
		usleep_range(12000, 16000);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = sc230ai_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(sc230ai->xvclk);

	return ret;
}

static void __sc230ai_power_off(struct sc230ai *sc230ai)
{
	int ret;
	struct device *dev = &sc230ai->client->dev;

	clk_disable_unprepare(sc230ai->xvclk);
	if (sc230ai->is_thunderboot) {
		if (sc230ai->is_first_streamoff) {
			sc230ai->is_thunderboot = false;
			sc230ai->is_first_streamoff = false;
		} else {
			return;
		}
	}
	if (!IS_ERR(sc230ai->pwdn_gpio))
		gpiod_set_value_cansleep(sc230ai->pwdn_gpio, 0);
	if (!IS_ERR(sc230ai->reset_gpio))
		gpiod_set_value_cansleep(sc230ai->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(sc230ai->pins_sleep)) {
		ret = pinctrl_select_state(sc230ai->pinctrl,
					   sc230ai->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(SC230AI_NUM_SUPPLIES, sc230ai->supplies);
}

static int sc230ai_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc230ai *sc230ai = to_sc230ai(sd);

	return __sc230ai_power_on(sc230ai);
}

static int sc230ai_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc230ai *sc230ai = to_sc230ai(sd);

	__sc230ai_power_off(sc230ai);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc230ai_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc230ai *sc230ai = to_sc230ai(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc230ai_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc230ai->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc230ai->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sc230ai_enum_frame_interval(struct v4l2_subdev *sd,
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

static const struct dev_pm_ops sc230ai_pm_ops = {
	SET_RUNTIME_PM_OPS(sc230ai_runtime_suspend,
			   sc230ai_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc230ai_internal_ops = {
	.open = sc230ai_open,
};
#endif

static const struct v4l2_subdev_core_ops sc230ai_core_ops = {
	.s_power = sc230ai_s_power,
	.ioctl = sc230ai_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc230ai_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc230ai_video_ops = {
	.s_stream = sc230ai_s_stream,
	.g_frame_interval = sc230ai_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops sc230ai_pad_ops = {
	.enum_mbus_code = sc230ai_enum_mbus_code,
	.enum_frame_size = sc230ai_enum_frame_sizes,
	.enum_frame_interval = sc230ai_enum_frame_interval,
	.get_fmt = sc230ai_get_fmt,
	.set_fmt = sc230ai_set_fmt,
	.get_mbus_config = sc230ai_g_mbus_config,
};

static const struct v4l2_subdev_ops sc230ai_subdev_ops = {
	.core	= &sc230ai_core_ops,
	.video	= &sc230ai_video_ops,
	.pad	= &sc230ai_pad_ops,
};

static int sc230ai_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc230ai *sc230ai = container_of(ctrl->handler,
					       struct sc230ai, ctrl_handler);
	struct i2c_client *client = sc230ai->client;
	u32 again = 0, dgain = 0, dgain_fine = 0x80;
	s64 max;
	int ret = 0;
	u32 val = 0;
	s32 temp = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc230ai->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(sc230ai->exposure,
					 sc230ai->exposure->minimum, max,
					 sc230ai->exposure->step,
					 sc230ai->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dev_dbg(&client->dev, "set exposure value 0x%x\n", ctrl->val);
		if (sc230ai->cur_mode->hdr_mode == NO_HDR) {
			temp = ctrl->val * 2;
			/* 4 least significant bits of expsoure are fractional part */
			ret = sc230ai_write_reg(sc230ai->client,
						SC230AI_REG_EXPOSURE_H,
						SC230AI_REG_VALUE_08BIT,
						SC230AI_FETCH_EXP_H(temp));
			ret |= sc230ai_write_reg(sc230ai->client,
						 SC230AI_REG_EXPOSURE_M,
						 SC230AI_REG_VALUE_08BIT,
						 SC230AI_FETCH_EXP_M(temp));
			ret |= sc230ai_write_reg(sc230ai->client,
						 SC230AI_REG_EXPOSURE_L,
						 SC230AI_REG_VALUE_08BIT,
						 SC230AI_FETCH_EXP_L(temp));
		}
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (sc230ai->cur_mode->hdr_mode == NO_HDR)
			sc230ai_get_gain_reg(sc230ai, &again, &dgain,
					     &dgain_fine, ctrl->val);
		dev_dbg(&client->dev, "gain %d, ag 0x%x, dg 0x%x, dg_f 0x%x\n",
			ctrl->val, again, dgain, dgain_fine);
		ret = sc230ai_write_reg(sc230ai->client,
					SC230AI_REG_ANA_GAIN,
					SC230AI_REG_VALUE_08BIT,
					again);
		ret |= sc230ai_write_reg(sc230ai->client,
					 SC230AI_REG_DIG_GAIN,
					 SC230AI_REG_VALUE_08BIT,
					 dgain);
		ret |= sc230ai_write_reg(sc230ai->client,
					 SC230AI_REG_DIG_FINE_GAIN,
					 SC230AI_REG_VALUE_08BIT,
					 dgain_fine);
		break;
	case V4L2_CID_VBLANK:
		dev_dbg(&client->dev, "set blank value 0x%x\n", ctrl->val);
		ret = sc230ai_write_reg(sc230ai->client,
					SC230AI_REG_VTS_H,
					SC230AI_REG_VALUE_08BIT,
					(ctrl->val + sc230ai->cur_mode->height)
					>> 8);
		ret |= sc230ai_write_reg(sc230ai->client,
					 SC230AI_REG_VTS_L,
					 SC230AI_REG_VALUE_08BIT,
					 (ctrl->val + sc230ai->cur_mode->height)
					 & 0xff);
		sc230ai->cur_vts = ctrl->val + sc230ai->cur_mode->height;
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = sc230ai_enable_test_pattern(sc230ai, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = sc230ai_read_reg(sc230ai->client, SC230AI_FLIP_MIRROR_REG,
				       SC230AI_REG_VALUE_08BIT, &val);
		ret |= sc230ai_write_reg(sc230ai->client, SC230AI_FLIP_MIRROR_REG,
					 SC230AI_REG_VALUE_08BIT,
					 SC230AI_FETCH_MIRROR(val, ctrl->val));
		break;
	case V4L2_CID_VFLIP:
		ret = sc230ai_read_reg(sc230ai->client, SC230AI_FLIP_MIRROR_REG,
				       SC230AI_REG_VALUE_08BIT, &val);
		ret |= sc230ai_write_reg(sc230ai->client, SC230AI_FLIP_MIRROR_REG,
					 SC230AI_REG_VALUE_08BIT,
					 SC230AI_FETCH_FLIP(val, ctrl->val));
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc230ai_ctrl_ops = {
	.s_ctrl = sc230ai_set_ctrl,
};

static int sc230ai_initialize_controls(struct sc230ai *sc230ai)
{
	const struct sc230ai_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u64 dst_pixel_rate = 0;
	u32 h_blank;
	int ret;

	handler = &sc230ai->ctrl_handler;
	mode = sc230ai->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &sc230ai->mutex;

	sc230ai->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ,
				ARRAY_SIZE(link_freq_menu_items) - 1, 0,
				link_freq_menu_items);
	__v4l2_ctrl_s_ctrl(sc230ai->link_freq, mode->mipi_freq_idx);

	if (mode->mipi_freq_idx == 0)
		dst_pixel_rate = PIXEL_RATE_WITH_185M_10BIT;
	else if (mode->mipi_freq_idx == 1)
		dst_pixel_rate = PIXEL_RATE_WITH_371M_10BIT;

	sc230ai->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
						V4L2_CID_PIXEL_RATE, 0,
						PIXEL_RATE_WITH_371M_10BIT,
						1, dst_pixel_rate);

	h_blank = mode->hts_def - mode->width;
	sc230ai->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (sc230ai->hblank)
		sc230ai->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	vblank_def = mode->vts_def - mode->height;
	sc230ai->vblank = v4l2_ctrl_new_std(handler, &sc230ai_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    SC230AI_VTS_MAX - mode->height,
					    1, vblank_def);
	exposure_max = SC230AI_EXPOSURE_LIN_MAX;
	sc230ai->exposure = v4l2_ctrl_new_std(handler, &sc230ai_ctrl_ops,
					      V4L2_CID_EXPOSURE, SC230AI_EXPOSURE_MIN,
					      exposure_max, SC230AI_EXPOSURE_STEP,
					      mode->exp_def);
	sc230ai->anal_gain = v4l2_ctrl_new_std(handler, &sc230ai_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN, SC230AI_GAIN_MIN,
					       SC230AI_GAIN_MAX, SC230AI_GAIN_STEP,
					       SC230AI_GAIN_DEFAULT);
	sc230ai->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
							    &sc230ai_ctrl_ops,
					V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(sc230ai_test_pattern_menu) - 1,
					0, 0, sc230ai_test_pattern_menu);
	v4l2_ctrl_new_std(handler, &sc230ai_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(handler, &sc230ai_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&sc230ai->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	sc230ai->subdev.ctrl_handler = handler;
	sc230ai->has_init_exp = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int sc230ai_check_sensor_id(struct sc230ai *sc230ai,
				   struct i2c_client *client)
{
	struct device *dev = &sc230ai->client->dev;
	u32 id = 0;
	int ret;

	if (sc230ai->is_thunderboot) {
		dev_info(dev, "Enable thunderboot mode, skip sensor id check\n");
		return 0;
	}
	ret = sc230ai_read_reg(client, SC230AI_REG_CHIP_ID,
			       SC230AI_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int sc230ai_configure_regulators(struct sc230ai *sc230ai)
{
	unsigned int i;

	for (i = 0; i < SC230AI_NUM_SUPPLIES; i++)
		sc230ai->supplies[i].supply = sc230ai_supply_names[i];

	return devm_regulator_bulk_get(&sc230ai->client->dev,
				       SC230AI_NUM_SUPPLIES,
				       sc230ai->supplies);
}

static int sc230ai_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc230ai *sc230ai;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	sc230ai = devm_kzalloc(dev, sizeof(*sc230ai), GFP_KERNEL);
	if (!sc230ai)
		return -ENOMEM;

	of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc230ai->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc230ai->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc230ai->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc230ai->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}
	sc230ai->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);
	sc230ai->client = client;
	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			sc230ai->cur_mode = &supported_modes[i];
			break;
		}
	}
	if (i == ARRAY_SIZE(supported_modes))
		sc230ai->cur_mode = &supported_modes[0];

	sc230ai->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc230ai->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	sc230ai->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(sc230ai->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	sc230ai->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_ASIS);
	if (IS_ERR(sc230ai->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	sc230ai->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc230ai->pinctrl)) {
		sc230ai->pins_default =
			pinctrl_lookup_state(sc230ai->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc230ai->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		sc230ai->pins_sleep =
			pinctrl_lookup_state(sc230ai->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc230ai->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = sc230ai_configure_regulators(sc230ai);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&sc230ai->mutex);

	sd = &sc230ai->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc230ai_subdev_ops);
	ret = sc230ai_initialize_controls(sc230ai);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc230ai_power_on(sc230ai);
	if (ret)
		goto err_free_handler;

	ret = sc230ai_check_sensor_id(sc230ai, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc230ai_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc230ai->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc230ai->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc230ai->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sc230ai->module_index, facing,
		 SC230AI_NAME, dev_name(sd->dev));
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
	__sc230ai_power_off(sc230ai);
err_free_handler:
	v4l2_ctrl_handler_free(&sc230ai->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc230ai->mutex);

	return ret;
}

static int sc230ai_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc230ai *sc230ai = to_sc230ai(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc230ai->ctrl_handler);
	mutex_destroy(&sc230ai->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc230ai_power_off(sc230ai);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc230ai_of_match[] = {
	{ .compatible = "smartsens,sc230ai" },
	{},
};
MODULE_DEVICE_TABLE(of, sc230ai_of_match);
#endif

static const struct i2c_device_id sc230ai_match_id[] = {
	{ "smartsens,sc230ai", 0 },
	{ },
};

static struct i2c_driver sc230ai_i2c_driver = {
	.driver = {
		.name = SC230AI_NAME,
		.pm = &sc230ai_pm_ops,
		.of_match_table = of_match_ptr(sc230ai_of_match),
	},
	.probe		= &sc230ai_probe,
	.remove		= &sc230ai_remove,
	.id_table	= sc230ai_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc230ai_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc230ai_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("smartsens sc230ai sensor driver");
MODULE_LICENSE("GPL");
