// SPDX-License-Identifier: GPL-2.0
/*
 * sc401ai driver
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 fix gain range.
 * V0.0X01.0X04 add enum_frame_interval function.
 * V0.0X01.0X05 add quick stream on/off
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
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/rk-camera-module.h>
#include <linux/rk-preisp.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x05)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define SC401AI_BITS_PER_SAMPLE		10

#define SC401AI_LINK_FREQ_315		157500000// 315Mbps
#define SC401AI_LINK_FREQ_630		315000000// 630Mbps

#define PIXEL_RATE_WITH_315M_10BIT		(SC401AI_LINK_FREQ_315 * 2 * \
					4 / SC401AI_BITS_PER_SAMPLE)
#define PIXEL_RATE_WITH_630M_10BIT		(SC401AI_LINK_FREQ_630 * 2 * \
					2 / SC401AI_BITS_PER_SAMPLE)
#define PIXEL_RATE_WITH_MAX			(SC401AI_LINK_FREQ_630 * 2 * \
					2 / SC401AI_BITS_PER_SAMPLE)

#define SC401AI_XVCLK_FREQ		27000000

#define CHIP_ID				0xcd2e
#define SC401AI_REG_CHIP_ID		0x3107

#define SC401AI_REG_CTRL_MODE		0x0100
#define SC401AI_MODE_SW_STANDBY		0x0
#define SC401AI_MODE_STREAMING		BIT(0)

#define SC401AI_REG_EXPOSURE_H		0x3e00
#define SC401AI_REG_EXPOSURE_M		0x3e01
#define SC401AI_REG_EXPOSURE_L		0x3e02
#define	SC401AI_EXPOSURE_MIN		1
#define	SC401AI_EXPOSURE_STEP		1
#define SC401AI_VTS_MAX			0x7fff

#define SC401AI_REG_DIG_GAIN		0x3e06
#define SC401AI_REG_DIG_FINE_GAIN	0x3e07
#define SC401AI_REG_ANA_GAIN		0x3e08
#define SC401AI_REG_ANA_FINE_GAIN	0x3e09
#define SC401AI_GAIN_MIN		0x0040
#define SC401AI_GAIN_MAX		(24 * 32 * 64)    //23.32*31.75*64
#define SC401AI_GAIN_STEP		1
#define SC401AI_GAIN_DEFAULT		0x0800

#define SC401AI_REG_GROUP_HOLD		0x3812
#define SC401AI_GROUP_HOLD_START	0x00
#define SC401AI_GROUP_HOLD_END		0x30

#define SC401AI_REG_HIGH_TEMP_H		0x3974
#define SC401AI_REG_HIGH_TEMP_L		0x3975

#define SC401AI_REG_TEST_PATTERN	0x4501
#define SC401AI_TEST_PATTERN_BIT_MASK	BIT(3)

#define SC401AI_REG_VTS_H		0x320e
#define SC401AI_REG_VTS_L		0x320f

#define SC401AI_FLIP_MIRROR_REG		0x3221

#define SC401AI_FETCH_EXP_H(VAL)		(((VAL) >> 12) & 0xF)
#define SC401AI_FETCH_EXP_M(VAL)		(((VAL) >> 4) & 0xFF)
#define SC401AI_FETCH_EXP_L(VAL)		(((VAL) & 0xF) << 4)

#define SC401AI_FETCH_AGAIN_H(VAL)		(((VAL) >> 8) & 0x03)
#define SC401AI_FETCH_AGAIN_L(VAL)		((VAL) & 0xFF)

#define SC401AI_FETCH_MIRROR(VAL, ENABLE)	(ENABLE ? VAL | 0x06 : VAL & 0xf9)
#define SC401AI_FETCH_FLIP(VAL, ENABLE)		(ENABLE ? VAL | 0x60 : VAL & 0x9f)

#define REG_DELAY			0xFFFE
#define REG_NULL			0xFFFF

#define SC401AI_REG_VALUE_08BIT		1
#define SC401AI_REG_VALUE_16BIT		2
#define SC401AI_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"
#define SC401AI_NAME			"sc401ai"

static const char * const sc401ai_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define SC401AI_NUM_SUPPLIES ARRAY_SIZE(sc401ai_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct sc401ai_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 mipi_freq_idx;
	u32 vc[PAD_MAX];
};

struct sc401ai {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[SC401AI_NUM_SUPPLIES];

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
	bool			streaming;
	bool			power_on;
	unsigned int		lane_num;
	unsigned int		cfg_num;
	const struct sc401ai_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
	struct preisp_hdrae_exp_s init_hdrae_exp;
};

#define to_sc401ai(sd) container_of(sd, struct sc401ai, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval sc401ai_global_regs[] = {
	{REG_NULL, 0x00},
};

/*
 * Xclk 27Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 315Mbps, 4lane
 */
static const struct regval sc401ai_linear_10_2560x1440_4lane_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x301c, 0x78},
	{0x301f, 0x01},
	{0x3208, 0x0a},
	{0x3209, 0x00},
	{0x320a, 0x05},
	{0x320b, 0xa0},
	{0x320e, 0x05},
	{0x320f, 0xdc},
	{0x3214, 0x11},
	{0x3215, 0x11},
	{0x3223, 0x80},
	{0x3250, 0x00},
	{0x3253, 0x08},
	{0x3274, 0x01},
	{0x3301, 0x20},
	{0x3302, 0x18},
	{0x3303, 0x10},
	{0x3304, 0x50},
	{0x3306, 0x38},
	{0x3308, 0x18},
	{0x3309, 0x60},
	{0x330b, 0xc0},
	{0x330d, 0x10},
	{0x330e, 0x18},
	{0x330f, 0x04},
	{0x3310, 0x02},
	{0x331c, 0x04},
	{0x331e, 0x41},
	{0x331f, 0x51},
	{0x3320, 0x09},
	{0x3333, 0x10},
	{0x334c, 0x08},
	{0x3356, 0x09},
	{0x3364, 0x17},
	{0x338e, 0xfd},
	{0x3390, 0x08},
	{0x3391, 0x18},
	{0x3392, 0x38},
	{0x3393, 0x20},
	{0x3394, 0x20},
	{0x3395, 0x20},
	{0x3396, 0x08},
	{0x3397, 0x18},
	{0x3398, 0x38},
	{0x3399, 0x20},
	{0x339a, 0x20},
	{0x339b, 0x20},
	{0x339c, 0x20},
	{0x33ac, 0x10},
	{0x33ae, 0x18},
	{0x33af, 0x19},
	{0x360f, 0x01},
	{0x3620, 0x08},
	{0x3637, 0x25},
	{0x363a, 0x12},
	{0x3670, 0x0a},
	{0x3671, 0x07},
	{0x3672, 0x57},
	{0x3673, 0x5e},
	{0x3674, 0x84},
	{0x3675, 0x88},
	{0x3676, 0x8a},
	{0x367a, 0x58},
	{0x367b, 0x78},
	{0x367c, 0x58},
	{0x367d, 0x78},
	{0x3690, 0x33},
	{0x3691, 0x43},
	{0x3692, 0x34},
	{0x369c, 0x40},
	{0x369d, 0x78},
	{0x36ea, 0x39},
	{0x36eb, 0x0d},
	{0x36ec, 0x2c},
	{0x36ed, 0x24},
	{0x36fa, 0x39},
	{0x36fb, 0x33},
	{0x36fc, 0x10},
	{0x36fd, 0x14},
	{0x3908, 0x41},
	{0x396c, 0x0e},
	{0x3e00, 0x00},
	{0x3e01, 0xb6},
	{0x3e02, 0x00},
	{0x3e03, 0x0b},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3e1b, 0x2a},
	{0x4509, 0x30},
	{0x57a8, 0xd0},
	{0x36e9, 0x14},
	{0x36f9, 0x14},
	{REG_NULL, 0x00},
};

/*
 * Xclk 27Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 630Mbps, 2lane
 */
static const struct regval sc401ai_linear_10_2560x1440_2lane_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x3018, 0x3a},
	{0x3019, 0x0c},
	{0x301c, 0x78},
	{0x301f, 0x05},
	{0x3208, 0x0a},
	{0x3209, 0x00},
	{0x320a, 0x05},
	{0x320b, 0xa0},
	{0x320e, 0x05},
	{0x320f, 0xdc},
	{0x3214, 0x11},
	{0x3215, 0x11},
	{0x3223, 0x80},
	{0x3250, 0x00},
	{0x3253, 0x08},
	{0x3274, 0x01},
	{0x3301, 0x20},
	{0x3302, 0x18},
	{0x3303, 0x10},
	{0x3304, 0x50},
	{0x3306, 0x38},
	{0x3308, 0x18},
	{0x3309, 0x60},
	{0x330b, 0xc0},
	{0x330d, 0x10},
	{0x330e, 0x18},
	{0x330f, 0x04},
	{0x3310, 0x02},
	{0x331c, 0x04},
	{0x331e, 0x41},
	{0x331f, 0x51},
	{0x3320, 0x09},
	{0x3333, 0x10},
	{0x334c, 0x08},
	{0x3356, 0x09},
	{0x3364, 0x17},
	{0x338e, 0xfd},
	{0x3390, 0x08},
	{0x3391, 0x18},
	{0x3392, 0x38},
	{0x3393, 0x20},
	{0x3394, 0x20},
	{0x3395, 0x20},
	{0x3396, 0x08},
	{0x3397, 0x18},
	{0x3398, 0x38},
	{0x3399, 0x20},
	{0x339a, 0x20},
	{0x339b, 0x20},
	{0x339c, 0x20},
	{0x33ac, 0x10},
	{0x33ae, 0x18},
	{0x33af, 0x19},
	{0x360f, 0x01},
	{0x3620, 0x08},
	{0x3637, 0x25},
	{0x363a, 0x12},
	{0x3670, 0x0a},
	{0x3671, 0x07},
	{0x3672, 0x57},
	{0x3673, 0x5e},
	{0x3674, 0x84},
	{0x3675, 0x88},
	{0x3676, 0x8a},
	{0x367a, 0x58},
	{0x367b, 0x78},
	{0x367c, 0x58},
	{0x367d, 0x78},
	{0x3690, 0x33},
	{0x3691, 0x43},
	{0x3692, 0x34},
	{0x369c, 0x40},
	{0x369d, 0x78},
	{0x36ea, 0x39},
	{0x36eb, 0x0d},
	{0x36ec, 0x1c},
	{0x36ed, 0x24},
	{0x36fa, 0x39},
	{0x36fb, 0x33},
	{0x36fc, 0x10},
	{0x36fd, 0x14},
	{0x3908, 0x41},
	{0x396c, 0x0e},
	{0x3e00, 0x00},
	{0x3e01, 0xb6},
	{0x3e02, 0x00},
	{0x3e03, 0x0b},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3e1b, 0x2a},
	{0x4509, 0x30},
	{0x4819, 0x08},
	{0x481b, 0x05},
	{0x481d, 0x11},
	{0x481f, 0x04},
	{0x4821, 0x09},
	{0x4823, 0x04},
	{0x4825, 0x04},
	{0x4827, 0x04},
	{0x4829, 0x07},
	{0x57a8, 0xd0},
	{0x36e9, 0x14},
	{0x36f9, 0x14},
	//{0x0100, 0x01},
	{REG_NULL, 0x00},
};

static const struct sc401ai_mode supported_modes[] = {
	{
		.width = 2560,
		.height = 1440,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0080,
		.hts_def = 0x0578 * 2,
		.vts_def = 0x05dc,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc401ai_linear_10_2560x1440_4lane_regs,
		.hdr_mode = NO_HDR,
		.mipi_freq_idx = 0,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 2560,
		.height = 1440,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0080,
		.hts_def = 0x0578 * 2,
		.vts_def = 0x05dc,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc401ai_linear_10_2560x1440_2lane_regs,
		.hdr_mode = NO_HDR,
		.mipi_freq_idx = 1,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	SC401AI_LINK_FREQ_315,
	SC401AI_LINK_FREQ_630,
};

/* Write registers up to 4 at a time */
static int sc401ai_write_reg(struct i2c_client *client, u16 reg,
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

static int sc401ai_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = sc401ai_write_reg(client, regs[i].addr,
					SC401AI_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int sc401ai_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static int sc401ai_set_gain_reg(struct sc401ai *sc401ai, u32 gain)
{
	u8 Coarse_gain = 1, DIG_gain = 1;
	u32 Dcg_gainx100 = 1, ANA_Fine_gainx64 = 1;
	u8 Coarse_gain_reg = 0, DIG_gain_reg = 0;
	u8 ANA_Fine_gain_reg = 0x20, DIG_Fine_gain_reg = 0x80;
	int ret = 0;

	gain = gain * 16;
	if (gain <= 1024)
		gain = 1024;
	else if (gain > SC401AI_GAIN_MAX * 16)
		gain = SC401AI_GAIN_MAX * 16;

	if (gain < 1504) {               // start again
		Dcg_gainx100 = 100;
		Coarse_gain = 1;
		DIG_gain = 1;
		Coarse_gain_reg = 0x03;
		DIG_gain_reg = 0x0;
		DIG_Fine_gain_reg = 0x80;
	} else if (gain <= 3008) {
		Dcg_gainx100 = 147;
		Coarse_gain = 1;
		DIG_gain = 1;
		Coarse_gain_reg = 0x23;
		DIG_gain_reg = 0x0;
		DIG_Fine_gain_reg = 0x80;
	} else if (gain <= 6017) {
		Dcg_gainx100 = 147;
		Coarse_gain = 2;
		DIG_gain = 1;
		Coarse_gain_reg = 0x27;
		DIG_gain_reg = 0x0;
		DIG_Fine_gain_reg = 0x80;
	} else if (gain <= 12034) {
		Dcg_gainx100 = 147;
		Coarse_gain = 4;
		DIG_gain = 1;
		Coarse_gain_reg = 0x2f;
		DIG_gain_reg = 0x0;
		DIG_Fine_gain_reg = 0x80;
	} else if (gain <= 23879) {           // end again
		Dcg_gainx100 = 147;
		Coarse_gain = 8;
		DIG_gain = 1;
		Coarse_gain_reg = 0x3f;
		DIG_gain_reg = 0x0;
		DIG_Fine_gain_reg = 0x80;
	} else if (gain < 23879 * 2) {         // start dgain
		Dcg_gainx100 = 147;
		Coarse_gain = 8;
		DIG_gain = 1;
		ANA_Fine_gainx64 = 127;
		Coarse_gain_reg = 0x3f;
		DIG_gain_reg = 0x0;
		ANA_Fine_gain_reg = 0x7f;
	} else if (gain < 23879 * 4) {
		Dcg_gainx100 = 147;
		Coarse_gain = 8;
		DIG_gain = 2;
		ANA_Fine_gainx64 = 127;
		Coarse_gain_reg = 0x3f;
		DIG_gain_reg = 0x1;
		ANA_Fine_gain_reg = 0x7f;
	} else if (gain < 23879 * 8) {
		Dcg_gainx100 = 147;
		Coarse_gain = 8;
		DIG_gain = 4;
		ANA_Fine_gainx64 = 127;
		Coarse_gain_reg = 0x3f;
		DIG_gain_reg = 0x3;
		ANA_Fine_gain_reg = 0x7f;
	} else if (gain < 23879 * 16) {
		Dcg_gainx100 = 147;
		Coarse_gain = 8;
		DIG_gain = 8;
		ANA_Fine_gainx64 = 127;
		Coarse_gain_reg = 0x3f;
		DIG_gain_reg = 0x7;
		ANA_Fine_gain_reg = 0x7f;
	} else if (gain <= 1754822) {
		Dcg_gainx100 = 147;
		Coarse_gain = 8;
		DIG_gain = 16;
		ANA_Fine_gainx64 = 127;
		Coarse_gain_reg = 0x3f;
		DIG_gain_reg = 0xF;
		ANA_Fine_gain_reg = 0x7f;
	}

	if (gain < 1504)
		ANA_Fine_gain_reg = abs(100 * gain / (Dcg_gainx100 * Coarse_gain) / 16);
	else if (gain == 1504)
		ANA_Fine_gain_reg = 0x40;
	else if (gain < 23879)
		ANA_Fine_gain_reg = abs(100 * gain / (Dcg_gainx100 * Coarse_gain) / 16);
	else
		DIG_Fine_gain_reg = abs(800 * gain / (Dcg_gainx100 * Coarse_gain *
							DIG_gain) / ANA_Fine_gainx64);

	ret = sc401ai_write_reg(sc401ai->client,
				SC401AI_REG_DIG_GAIN,
				SC401AI_REG_VALUE_08BIT,
				DIG_gain_reg & 0xF);
	ret |= sc401ai_write_reg(sc401ai->client,
				 SC401AI_REG_DIG_FINE_GAIN,
				 SC401AI_REG_VALUE_08BIT,
				 DIG_Fine_gain_reg);
	ret |= sc401ai_write_reg(sc401ai->client,
				 SC401AI_REG_ANA_GAIN,
				 SC401AI_REG_VALUE_08BIT,
				 Coarse_gain_reg);
	ret |= sc401ai_write_reg(sc401ai->client,
				 SC401AI_REG_ANA_FINE_GAIN,
				 SC401AI_REG_VALUE_08BIT,
				 ANA_Fine_gain_reg);

	return ret;
}

static int sc401ai_get_reso_dist(const struct sc401ai_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc401ai_mode *
sc401ai_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = sc401ai_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int sc401ai_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc401ai *sc401ai = to_sc401ai(sd);
	const struct sc401ai_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&sc401ai->mutex);

	mode = sc401ai_find_best_fit(fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc401ai->mutex);
		return -ENOTTY;
#endif
	} else {
		sc401ai->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc401ai->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc401ai->vblank, vblank_def,
					 SC401AI_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&sc401ai->mutex);

	return 0;
}

static int sc401ai_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc401ai *sc401ai = to_sc401ai(sd);
	const struct sc401ai_mode *mode = sc401ai->cur_mode;

	mutex_lock(&sc401ai->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc401ai->mutex);
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
	mutex_unlock(&sc401ai->mutex);

	return 0;
}

static int sc401ai_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct sc401ai *sc401ai = to_sc401ai(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = sc401ai->cur_mode->bus_fmt;

	return 0;
}

static int sc401ai_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != supported_modes[1].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int sc401ai_enable_test_pattern(struct sc401ai *sc401ai, u32 pattern)
{
	u32 val = 0;
	int ret = 0;

	ret = sc401ai_read_reg(sc401ai->client, SC401AI_REG_TEST_PATTERN,
			       SC401AI_REG_VALUE_08BIT, &val);
	if (pattern)
		val |= SC401AI_TEST_PATTERN_BIT_MASK;
	else
		val &= ~SC401AI_TEST_PATTERN_BIT_MASK;
	ret |= sc401ai_write_reg(sc401ai->client, SC401AI_REG_TEST_PATTERN,
				 SC401AI_REG_VALUE_08BIT, val);
	return ret;
}

static int sc401ai_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct sc401ai *sc401ai = to_sc401ai(sd);
	const struct sc401ai_mode *mode = sc401ai->cur_mode;

	mutex_lock(&sc401ai->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&sc401ai->mutex);

	return 0;
}

static int sc401ai_g_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct sc401ai *sc401ai = to_sc401ai(sd);
	const struct sc401ai_mode *mode = sc401ai->cur_mode;
	u32 val = 0;

	val = 1 << (sc401ai->lane_num - 1) |
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

static void sc401ai_get_module_inf(struct sc401ai *sc401ai,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, SC401AI_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, sc401ai->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, sc401ai->len_name, sizeof(inf->base.lens));
}

static long sc401ai_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc401ai *sc401ai = to_sc401ai(sd);
	struct rkmodule_hdr_cfg *hdr;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		sc401ai_get_module_inf(sc401ai, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = sc401ai->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		if (hdr->hdr_mode != 0)
			ret = -1;
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = sc401ai_write_reg(sc401ai->client,
						SC401AI_REG_CTRL_MODE,
						SC401AI_REG_VALUE_08BIT,
						SC401AI_MODE_STREAMING);
		else
			ret = sc401ai_write_reg(sc401ai->client,
						SC401AI_REG_CTRL_MODE,
						SC401AI_REG_VALUE_08BIT,
						SC401AI_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc401ai_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc401ai_ioctl(sd, cmd, inf);
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

		if (copy_from_user(cfg, up, sizeof(*cfg))) {
			kfree(cfg);
			return -EFAULT;
		}

		sc401ai_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc401ai_ioctl(sd, cmd, hdr);
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

		if (copy_from_user(hdr, up, sizeof(*hdr))) {
			kfree(hdr);
			return -EFAULT;
		}

		sc401ai_ioctl(sd, cmd, hdr);
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

		sc401ai_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;

		sc401ai_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __sc401ai_start_stream(struct sc401ai *sc401ai)
{
	int ret;

	ret = sc401ai_write_array(sc401ai->client, sc401ai->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&sc401ai->ctrl_handler);
	if (ret)
		return ret;

	return sc401ai_write_reg(sc401ai->client,
				 SC401AI_REG_CTRL_MODE,
				 SC401AI_REG_VALUE_08BIT,
				 SC401AI_MODE_STREAMING);
}

static int __sc401ai_stop_stream(struct sc401ai *sc401ai)
{
	return sc401ai_write_reg(sc401ai->client,
				 SC401AI_REG_CTRL_MODE,
				 SC401AI_REG_VALUE_08BIT,
				 SC401AI_MODE_SW_STANDBY);
}

static int sc401ai_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc401ai *sc401ai = to_sc401ai(sd);
	struct i2c_client *client = sc401ai->client;
	int ret = 0;

	mutex_lock(&sc401ai->mutex);
	on = !!on;
	if (on == sc401ai->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __sc401ai_start_stream(sc401ai);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc401ai_stop_stream(sc401ai);
		pm_runtime_put(&client->dev);
	}

	sc401ai->streaming = on;

unlock_and_return:
	mutex_unlock(&sc401ai->mutex);

	return ret;
}

static int sc401ai_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc401ai *sc401ai = to_sc401ai(sd);
	struct i2c_client *client = sc401ai->client;
	int ret = 0;

	mutex_lock(&sc401ai->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc401ai->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = sc401ai_write_array(sc401ai->client, sc401ai_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		sc401ai->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc401ai->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc401ai->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 sc401ai_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, SC401AI_XVCLK_FREQ / 1000 / 1000);
}

static int __sc401ai_power_on(struct sc401ai *sc401ai)
{
	int ret;
	u32 delay_us;
	struct device *dev = &sc401ai->client->dev;

	if (!IS_ERR_OR_NULL(sc401ai->pins_default)) {
		ret = pinctrl_select_state(sc401ai->pinctrl,
					   sc401ai->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(sc401ai->xvclk, SC401AI_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(sc401ai->xvclk) != SC401AI_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc401ai->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(sc401ai->reset_gpio))
		gpiod_set_value_cansleep(sc401ai->reset_gpio, 0);

	ret = regulator_bulk_enable(SC401AI_NUM_SUPPLIES, sc401ai->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(sc401ai->reset_gpio))
		gpiod_set_value_cansleep(sc401ai->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(sc401ai->pwdn_gpio))
		gpiod_set_value_cansleep(sc401ai->pwdn_gpio, 1);

	if (!IS_ERR(sc401ai->reset_gpio))
		usleep_range(6000, 8000);
	else
		usleep_range(12000, 16000);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = sc401ai_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(sc401ai->xvclk);

	return ret;
}

static void __sc401ai_power_off(struct sc401ai *sc401ai)
{
	int ret;
	struct device *dev = &sc401ai->client->dev;

	if (!IS_ERR(sc401ai->pwdn_gpio))
		gpiod_set_value_cansleep(sc401ai->pwdn_gpio, 0);
	clk_disable_unprepare(sc401ai->xvclk);
	if (!IS_ERR(sc401ai->reset_gpio))
		gpiod_set_value_cansleep(sc401ai->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(sc401ai->pins_sleep)) {
		ret = pinctrl_select_state(sc401ai->pinctrl,
					   sc401ai->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(SC401AI_NUM_SUPPLIES, sc401ai->supplies);
}

static int sc401ai_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc401ai *sc401ai = to_sc401ai(sd);

	return __sc401ai_power_on(sc401ai);
}

static int sc401ai_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc401ai *sc401ai = to_sc401ai(sd);

	__sc401ai_power_off(sc401ai);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc401ai_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc401ai *sc401ai = to_sc401ai(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc401ai_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc401ai->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc401ai->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sc401ai_enum_frame_interval(struct v4l2_subdev *sd,
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

static const struct dev_pm_ops sc401ai_pm_ops = {
	SET_RUNTIME_PM_OPS(sc401ai_runtime_suspend,
			   sc401ai_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc401ai_internal_ops = {
	.open = sc401ai_open,
};
#endif

static const struct v4l2_subdev_core_ops sc401ai_core_ops = {
	.s_power = sc401ai_s_power,
	.ioctl = sc401ai_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc401ai_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc401ai_video_ops = {
	.s_stream = sc401ai_s_stream,
	.g_frame_interval = sc401ai_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops sc401ai_pad_ops = {
	.enum_mbus_code = sc401ai_enum_mbus_code,
	.enum_frame_size = sc401ai_enum_frame_sizes,
	.enum_frame_interval = sc401ai_enum_frame_interval,
	.get_fmt = sc401ai_get_fmt,
	.set_fmt = sc401ai_set_fmt,
	.get_mbus_config = sc401ai_g_mbus_config,
};

static const struct v4l2_subdev_ops sc401ai_subdev_ops = {
	.core	= &sc401ai_core_ops,
	.video	= &sc401ai_video_ops,
	.pad	= &sc401ai_pad_ops,
};

static int sc401ai_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc401ai *sc401ai = container_of(ctrl->handler,
					       struct sc401ai, ctrl_handler);
	struct i2c_client *client = sc401ai->client;
	s64 max;
	int ret = 0;
	u32 val = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc401ai->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(sc401ai->exposure,
					 sc401ai->exposure->minimum, max,
					 sc401ai->exposure->step,
					 sc401ai->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (sc401ai->cur_mode->hdr_mode == NO_HDR) {
			val = ctrl->val << 1;
			/* 4 least significant bits of expsoure are fractional part */
			ret = sc401ai_write_reg(sc401ai->client,
						SC401AI_REG_EXPOSURE_H,
						SC401AI_REG_VALUE_08BIT,
						SC401AI_FETCH_EXP_H(val));
			ret |= sc401ai_write_reg(sc401ai->client,
						 SC401AI_REG_EXPOSURE_M,
						 SC401AI_REG_VALUE_08BIT,
						 SC401AI_FETCH_EXP_M(val));
			ret |= sc401ai_write_reg(sc401ai->client,
						 SC401AI_REG_EXPOSURE_L,
						 SC401AI_REG_VALUE_08BIT,
						 SC401AI_FETCH_EXP_L(val));
		}
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (sc401ai->cur_mode->hdr_mode == NO_HDR)
			ret = sc401ai_set_gain_reg(sc401ai, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = sc401ai_write_reg(sc401ai->client,
					SC401AI_REG_VTS_H,
					SC401AI_REG_VALUE_08BIT,
					(ctrl->val + sc401ai->cur_mode->height)
					>> 8);
		ret |= sc401ai_write_reg(sc401ai->client,
					 SC401AI_REG_VTS_L,
					 SC401AI_REG_VALUE_08BIT,
					 (ctrl->val + sc401ai->cur_mode->height)
					 & 0xff);
		sc401ai->cur_vts = ctrl->val + sc401ai->cur_mode->height;
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = sc401ai_enable_test_pattern(sc401ai, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = sc401ai_read_reg(sc401ai->client, SC401AI_FLIP_MIRROR_REG,
				       SC401AI_REG_VALUE_08BIT, &val);
		ret |= sc401ai_write_reg(sc401ai->client,
					 SC401AI_FLIP_MIRROR_REG,
					 SC401AI_REG_VALUE_08BIT,
					 SC401AI_FETCH_MIRROR(val, ctrl->val));
		break;
	case V4L2_CID_VFLIP:
		ret = sc401ai_read_reg(sc401ai->client, SC401AI_FLIP_MIRROR_REG,
				       SC401AI_REG_VALUE_08BIT, &val);
		ret |= sc401ai_write_reg(sc401ai->client,
					 SC401AI_FLIP_MIRROR_REG,
					 SC401AI_REG_VALUE_08BIT,
					 SC401AI_FETCH_FLIP(val, ctrl->val));
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc401ai_ctrl_ops = {
	.s_ctrl = sc401ai_set_ctrl,
};

static int sc401ai_parse_of(struct sc401ai *sc401ai)
{
	struct device *dev = &sc401ai->client->dev;
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

	sc401ai->lane_num = rval;

	if (sc401ai->lane_num == 2) {
		sc401ai->cur_mode = &supported_modes[1];
		dev_info(dev, "lane_num(%d)\n", sc401ai->lane_num);
	} else if (sc401ai->lane_num == 4) {
		sc401ai->cur_mode = &supported_modes[0];
		dev_info(dev, "lane_num(%d)\n", sc401ai->lane_num);
	} else {
		dev_err(dev, "unsupported lane_num(%d)\n", sc401ai->lane_num);
		return -1;
	}
	return 0;
}

static int sc401ai_initialize_controls(struct sc401ai *sc401ai)
{
	const struct sc401ai_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct device *dev = &sc401ai->client->dev;

	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	int dst_pixel_rate = 0;

	handler = &sc401ai->ctrl_handler;
	mode = sc401ai->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &sc401ai->mutex;

	sc401ai->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ,
				ARRAY_SIZE(link_freq_menu_items) - 1, 0,
				link_freq_menu_items);
	__v4l2_ctrl_s_ctrl(sc401ai->link_freq, mode->mipi_freq_idx);

	if (ret < 0)
		dev_err(dev, "get data num failed");

	if (mode->mipi_freq_idx == 0)
		dst_pixel_rate = PIXEL_RATE_WITH_315M_10BIT;
	else if (mode->mipi_freq_idx == 1)
		dst_pixel_rate = PIXEL_RATE_WITH_630M_10BIT;

	sc401ai->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
						V4L2_CID_PIXEL_RATE, 0,
						PIXEL_RATE_WITH_MAX,
						1, dst_pixel_rate);

	h_blank = mode->hts_def - mode->width;
	sc401ai->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (sc401ai->hblank)
		sc401ai->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	vblank_def = mode->vts_def - mode->height;
	sc401ai->vblank = v4l2_ctrl_new_std(handler, &sc401ai_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    SC401AI_VTS_MAX - mode->height,
					    1, vblank_def);
	exposure_max = mode->vts_def - 4;
	sc401ai->exposure = v4l2_ctrl_new_std(handler, &sc401ai_ctrl_ops,
					      V4L2_CID_EXPOSURE,
					      SC401AI_EXPOSURE_MIN,
					      exposure_max,
					      SC401AI_EXPOSURE_STEP,
					      mode->exp_def);
	sc401ai->anal_gain = v4l2_ctrl_new_std(handler, &sc401ai_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN,
					       SC401AI_GAIN_MIN,
					       SC401AI_GAIN_MAX,
					       SC401AI_GAIN_STEP,
					       SC401AI_GAIN_DEFAULT);
	v4l2_ctrl_new_std(handler, &sc401ai_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &sc401ai_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (handler->error) {
		ret = handler->error;
		dev_err(&sc401ai->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	sc401ai->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int sc401ai_check_sensor_id(struct sc401ai *sc401ai,
				   struct i2c_client *client)
{
	struct device *dev = &sc401ai->client->dev;
	u32 id = 0;
	int ret;

	ret = sc401ai_read_reg(client, SC401AI_REG_CHIP_ID,
			       SC401AI_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int sc401ai_configure_regulators(struct sc401ai *sc401ai)
{
	unsigned int i;

	for (i = 0; i < SC401AI_NUM_SUPPLIES; i++)
		sc401ai->supplies[i].supply = sc401ai_supply_names[i];

	return devm_regulator_bulk_get(&sc401ai->client->dev,
				       SC401AI_NUM_SUPPLIES,
				       sc401ai->supplies);
}

static int sc401ai_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc401ai *sc401ai;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	sc401ai = devm_kzalloc(dev, sizeof(*sc401ai), GFP_KERNEL);
	if (!sc401ai)
		return -ENOMEM;

	of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc401ai->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc401ai->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc401ai->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc401ai->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	sc401ai->client = client;
	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			sc401ai->cur_mode = &supported_modes[i];
			break;
		}
	}
	if (i == ARRAY_SIZE(supported_modes))
		sc401ai->cur_mode = &supported_modes[0];

	sc401ai->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc401ai->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	sc401ai->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sc401ai->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	sc401ai->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(sc401ai->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	sc401ai->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc401ai->pinctrl)) {
		sc401ai->pins_default =
			pinctrl_lookup_state(sc401ai->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc401ai->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		sc401ai->pins_sleep =
			pinctrl_lookup_state(sc401ai->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc401ai->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = sc401ai_configure_regulators(sc401ai);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	ret = sc401ai_parse_of(sc401ai);
	if (ret != 0)
		return -EINVAL;

	mutex_init(&sc401ai->mutex);

	sd = &sc401ai->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc401ai_subdev_ops);
	ret = sc401ai_initialize_controls(sc401ai);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc401ai_power_on(sc401ai);
	if (ret)
		goto err_free_handler;

	ret = sc401ai_check_sensor_id(sc401ai, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc401ai_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc401ai->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc401ai->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc401ai->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sc401ai->module_index, facing,
		 SC401AI_NAME, dev_name(sd->dev));
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
	__sc401ai_power_off(sc401ai);
err_free_handler:
	v4l2_ctrl_handler_free(&sc401ai->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc401ai->mutex);

	return ret;
}

static int sc401ai_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc401ai *sc401ai = to_sc401ai(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc401ai->ctrl_handler);
	mutex_destroy(&sc401ai->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc401ai_power_off(sc401ai);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc401ai_of_match[] = {
	{ .compatible = "smartsens,sc401ai" },
	{},
};
MODULE_DEVICE_TABLE(of, sc401ai_of_match);
#endif

static const struct i2c_device_id sc401ai_match_id[] = {
	{ "smartsens,sc401ai", 0 },
	{ },
};

static struct i2c_driver sc401ai_i2c_driver = {
	.driver = {
		.name = SC401AI_NAME,
		.pm = &sc401ai_pm_ops,
		.of_match_table = of_match_ptr(sc401ai_of_match),
	},
	.probe		= &sc401ai_probe,
	.remove		= &sc401ai_remove,
	.id_table	= sc401ai_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc401ai_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc401ai_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("smartsens sc401ai sensor driver");
MODULE_LICENSE("GPL");
