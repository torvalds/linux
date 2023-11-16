// SPDX-License-Identifier: GPL-2.0
/*
 * mis2031 driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 first version
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
#include "../platform/rockchip/isp/rkisp_tb_helper.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x01)

#define MIS2031_LANES			2
#define MIS2031_BITS_PER_SAMPLE		10
#define MIS2031_LINK_FREQ_185		185600000  //37.125mbps

#define PIXEL_RATE_WITH_315M_10BIT	(MIS2031_LINK_FREQ_185 * 2 * \
					MIS2031_LANES / MIS2031_BITS_PER_SAMPLE)
#define MIS2031_XVCLK_FREQ		27000000

#define CHIP_ID				0x2009
#define MIS2031_REG_CHIP_ID		0x3000

#define MIS2031_REG_CTRL_MODE		0x3006
#define MIS2031_MODE_SW_STANDBY		BIT(1)
#define MIS2031_MODE_STREAMING		0x0

#define MIS2031_REG_EXPOSURE_H		0x3100
#define MIS2031_REG_EXPOSURE_L		0x3101
#define	MIS2031_EXPOSURE_MIN		1
#define	MIS2031_EXPOSURE_STEP		1
#define MIS2031_FETCH_EXP_H(VAL)	(((VAL) >> 8) & 0xFF)
#define MIS2031_FETCH_EXP_L(VAL)	((VAL) & 0xFF)

#define MIS2031_REG_DIG_GAIN		0x3700
#define MIS2031_REG_DIG_FINE_GAIN	0x3701
#define MIS2031_REG_ANA_GAIN		0x3109
#define MIS2031_REG_ANA_FINE_GAIN       0x310a
#define MIS2031_GAIN_MIN		0x20
#define MIS2031_GAIN_MAX		(64 * 16 * 32)
#define MIS2031_GAIN_STEP		1
#define MIS2031_GAIN_DEFAULT		0x20
#define MIS2031_FETCH_AGAIN_H(VAL)	(((VAL) >> 8) & 0x03)
#define MIS2031_FETCH_AGAIN_L(VAL)	((VAL) & 0xFF)

#define MIS2031_REG_GAIN_EXP_VALID	0x300c
#define MIS2031_REG_GAIN_EXP_VALID_VAL	BIT(0)

#define MIS2031_VTS_MAX			0xffff
#define MIS2031_REG_VTS_H		0x3200
#define MIS2031_REG_VTS_L		0x3201

#define MIS2031_FLIP_MIRROR_REG		0x3007
#define MIRROR_BIT_MASK			BIT(0)
#define FLIP_BIT_MASK			BIT(1)
#define MIS2031_FETCH_MIRROR(VAL, ENABLE)	(ENABLE ? VAL | 0x01 : VAL & 0xfe)
#define MIS2031_FETCH_FLIP(VAL, ENABLE)		(ENABLE ? VAL | 0x10 : VAL & 0xfd)

#define MIS2031_REG_TEST_PATTERN	0x3500
#define MIS2031_TEST_PATTERN_BIT_MASK	BIT(0)

#define REG_DELAY			0xFFFE
#define REG_NULL			0xFFFF

#define MIS2031_REG_VALUE_08BIT		1
#define MIS2031_REG_VALUE_16BIT		2
#define MIS2031_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define MIS2031_NAME			"mis2031"

static const char * const mis2031_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define MIS2031_NUM_SUPPLIES ARRAY_SIZE(mis2031_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct mis2031_mode {
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

struct mis2031 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[MIS2031_NUM_SUPPLIES];

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
	const struct mis2031_mode *cur_mode;
	struct v4l2_fract	cur_fps;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
	bool			is_thunderboot;
	bool			is_first_streamoff;
};

#define to_mis2031(sd) container_of(sd, struct mis2031, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval mis2031_global_regs[] = {
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 630Mbps, 2lane
 */
static const struct regval mis2031_linear_10_1920x1080_regs[] = {
	{0x300b, 0x01},
	{0x3006, 0x02},
	{REG_DELAY, 0x50},
	{0x330c, 0x01},
	{0x3020, 0x01},
	{0x3021, 0x02},
	{0x3201, 0x65},
	{0x3200, 0x04},
	{0x3203, 0x98},
	{0x3202, 0x08},
	{0x3205, 0x00},
	{0x3204, 0x00},
	{0x3207, 0x3b},
	{0x3206, 0x04},
	{0x3209, 0x08},
	{0x3208, 0x00},
	{0x320b, 0x87},
	{0x320a, 0x07},
	{0x3102, 0x00},
	{0x3105, 0x00},
	{0x3108, 0x00},
	{0x3007, 0x00},
	{0x300a, 0x01},
	{0x330c, 0x01},
	{0x3300, 0x6e},
	{0x3301, 0x01},
	{0x3302, 0x02},
	{0x3303, 0x06},
	{0x3309, 0x01},
	{0x3307, 0x02},
	{0x330b, 0x0a},
	{0x3014, 0x00},
	{0x330f, 0x00},
	{0x310f, 0x00},
	{0x3986, 0x02},
	{0x3986, 0x02},
	{0x3900, 0x00},
	{0x3902, 0x11},
	{0x3901, 0x00},
	{0x3904, 0x44},
	{0x3903, 0x06},
	{0x3906, 0xff},
	{0x3905, 0x1f},
	{0x3908, 0xff},
	{0x3907, 0x1f},
	{0x390a, 0x42},
	{0x3909, 0x02},
	{0x390c, 0x19},
	{0x390b, 0x03},
	{0x390e, 0x30},
	{0x390d, 0x06},
	{0x3910, 0xff},
	{0x390f, 0x1f},
	{0x3911, 0x01},
	{0x3917, 0x00},
	{0x3916, 0x00},
	{0x3919, 0x90},
	{0x3918, 0x01},
	{0x3913, 0x11},
	{0x3912, 0x00},
	{0x3915, 0x52},
	{0x3914, 0x02},
	{0x391b, 0x00},
	{0x391a, 0x00},
	{0x391d, 0x41},
	{0x391c, 0x06},
	{0x391f, 0xff},
	{0x391e, 0x1f},
	{0x3921, 0xff},
	{0x3920, 0x1f},
	{0x3923, 0x00},
	{0x3922, 0x00},
	{0x3925, 0x46},
	{0x3924, 0x02},
	{0x394c, 0x00},
	{0x394e, 0x74},
	{0x394d, 0x00},
	{0x3950, 0x84},
	{0x394f, 0x00},
	{0x3952, 0x63},
	{0x3951, 0x00},
	{0x3954, 0x71},
	{0x3953, 0x02},
	{0x3927, 0x00},
	{0x3926, 0x00},
	{0x3929, 0xc6},
	{0x3928, 0x00},
	{0x392b, 0x9d},
	{0x392a, 0x01},
	{0x392d, 0x31},
	{0x392c, 0x02},
	{0x392f, 0xcc},
	{0x392e, 0x03},
	{0x3931, 0x60},
	{0x3930, 0x06},
	{0x3933, 0x60},
	{0x3932, 0x06},
	{0x3935, 0x60},
	{0x3934, 0x06},
	{0x3937, 0x60},
	{0x3936, 0x06},
	{0x3939, 0x60},
	{0x3938, 0x06},
	{0x393b, 0x60},
	{0x393a, 0x06},
	{0x3991, 0x40},
	{0x3990, 0x00},
	{0x3993, 0x80},
	{0x3992, 0x06},
	{0x3995, 0xff},
	{0x3994, 0x1f},
	{0x3997, 0x00},
	{0x3996, 0x00},
	{0x393d, 0x74},
	{0x393c, 0x00},
	{0x393f, 0x9d},
	{0x393e, 0x01},
	{0x3941, 0x4a},
	{0x3940, 0x03},
	{0x3943, 0x9c},
	{0x3942, 0x03},
	{0x3945, 0x00},
	{0x3944, 0x00},
	{0x3947, 0xe7},
	{0x3946, 0x00},
	{0x3949, 0xe7},
	{0x3948, 0x00},
	{0x394b, 0x35},
	{0x394a, 0x06},
	{0x395a, 0x00},
	{0x3959, 0x00},
	{0x395c, 0x09},
	{0x395b, 0x00},
	{0x395e, 0x2f},
	{0x395d, 0x02},
	{0x3960, 0x39},
	{0x395f, 0x03},
	{0x3956, 0x09},
	{0x3955, 0x00},
	{0x3958, 0x35},
	{0x3957, 0x06},
	{0x3962, 0x00},
	{0x3961, 0x00},
	{0x3964, 0x84},
	{0x3963, 0x00},
	{0x3966, 0x00},
	{0x3965, 0x00},
	{0x3968, 0x74},
	{0x3967, 0x00},
	{0x3989, 0x00},
	{0x3988, 0x00},
	{0x398b, 0xa5},
	{0x398a, 0x00},
	{0x398d, 0x00},
	{0x398c, 0x00},
	{0x398f, 0x84},
	{0x398e, 0x00},
	{0x396a, 0x62},
	{0x3969, 0x06},
	{0x396d, 0x00},
	{0x396c, 0x01},
	{0x396f, 0x60},
	{0x396e, 0x00},
	{0x3971, 0x60},
	{0x3970, 0x00},
	{0x3973, 0x60},
	{0x3972, 0x00},
	{0x3975, 0x60},
	{0x3974, 0x00},
	{0x3977, 0x60},
	{0x3976, 0x00},
	{0x3979, 0xa0},
	{0x3978, 0x01},
	{0x397b, 0xa0},
	{0x397a, 0x01},
	{0x397d, 0xa0},
	{0x397c, 0x01},
	{0x397f, 0xa0},
	{0x397e, 0x01},
	{0x3981, 0xa0},
	{0x3980, 0x01},
	{0x3983, 0xa0},
	{0x3982, 0x01},
	{0x3985, 0xa0},
	{0x3984, 0x05},
	{0x3c42, 0x03},
	{0x3012, 0x2b},
	{0x3205, 0x08},
	{0x3204, 0x00},
	{0x310f, 0x00},
	{0x3600, 0x63},
	{0x3630, 0x00},
	{0x3631, 0xFF},
	{0x3632, 0xFF},
	{0x364e, 0x63},
	{0x367e, 0x00},
	{0x367f, 0xFF},
	{0x3680, 0xFF},
	{0x369c, 0x63},
	{0x36cc, 0x00},
	{0x36cd, 0xFF},
	{0x36ce, 0xFF},
	{0x3706, 0x01},
	{0x3707, 0x00},
	{0x3708, 0x01},
	{0x3709, 0x00},
	{0x370a, 0x01},
	{0x370b, 0x00},
	{0x210b, 0x00},
	{0x3021, 0x00},
	{0x3a00, 0x00},
	{0x3a04, 0x03},
	{0x3a05, 0x78},
	{0x3a0a, 0x3a},
	{0x3a2a, 0x54},
	{0x3a2e, 0x10},
	{0x3a14, 0x04},
	{0x3a1c, 0x01},
	{0x3a36, 0x01},
	{0x3a07, 0x56},
	{0x3a35, 0x07},
	{0x3a30, 0x52},
	{0x3a31, 0x35},
	{0x3a19, 0x08},
	{0x3a1a, 0x08},
	{0x3a36, 0x01},
	{0x3006, 0x00},
	{0x3100, 0x00},
	{0x3101, 0x52},
	{0x3109, 0x00},
	{0x310a, 0x00},
	{REG_NULL, 0x00},
};

static const struct mis2031_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0052,
		.hts_def = 0x0898,
		.vts_def = 0x0465,
		.bus_fmt = MEDIA_BUS_FMT_SGRBG10_1X10,
		.reg_list = mis2031_linear_10_1920x1080_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}
};

static const s64 link_freq_menu_items[] = {
	MIS2031_LINK_FREQ_185
};

static const char * const mis2031_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int mis2031_write_reg(struct i2c_client *client, u16 reg,
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

static int mis2031_write_array(struct i2c_client *client,
					const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		if (regs[i].addr == REG_DELAY)
			mdelay(regs[i].val);
		else
			ret = mis2031_write_reg(client, regs[i].addr,
					MIS2031_REG_VALUE_08BIT, regs[i].val);
	}

	return ret;
}

/* Read registers up to 4 at a time */
static int mis2031_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static void mis2031_set_orientation_reg(struct mis2031 *mis2031, u32 en_flip_mir)
{
	switch (en_flip_mir) {
	case  0:
		mis2031_write_reg(mis2031->client, 0x3007, MIS2031_REG_VALUE_08BIT, 0x00);
		mis2031_write_reg(mis2031->client, 0x3205, MIS2031_REG_VALUE_08BIT, 0x00);
		mis2031_write_reg(mis2031->client, 0x3207, MIS2031_REG_VALUE_08BIT, 0x3b);
		mis2031_write_reg(mis2031->client, 0x3209, MIS2031_REG_VALUE_08BIT, 0x08);
		mis2031_write_reg(mis2031->client, 0x320b, MIS2031_REG_VALUE_08BIT, 0x87);
		break;
	case  1:
		mis2031_write_reg(mis2031->client, 0x3007, MIS2031_REG_VALUE_08BIT, 0x01);
		mis2031_write_reg(mis2031->client, 0x3205, MIS2031_REG_VALUE_08BIT, 0x00);
		mis2031_write_reg(mis2031->client, 0x3207, MIS2031_REG_VALUE_08BIT, 0x3b);
		mis2031_write_reg(mis2031->client, 0x3209, MIS2031_REG_VALUE_08BIT, 0x09);
		mis2031_write_reg(mis2031->client, 0x320b, MIS2031_REG_VALUE_08BIT, 0x88);
		break;
	case  2:
		mis2031_write_reg(mis2031->client, 0x3007, MIS2031_REG_VALUE_08BIT, 0x02);
		mis2031_write_reg(mis2031->client, 0x3205, MIS2031_REG_VALUE_08BIT, 0x01);
		mis2031_write_reg(mis2031->client, 0x3207, MIS2031_REG_VALUE_08BIT, 0x3c);
		mis2031_write_reg(mis2031->client, 0x3209, MIS2031_REG_VALUE_08BIT, 0x08);
		mis2031_write_reg(mis2031->client, 0x320b, MIS2031_REG_VALUE_08BIT, 0x87);
		break;
	case  3:
		mis2031_write_reg(mis2031->client, 0x3007, MIS2031_REG_VALUE_08BIT, 0x03);
		mis2031_write_reg(mis2031->client, 0x3205, MIS2031_REG_VALUE_08BIT, 0x01);
		mis2031_write_reg(mis2031->client, 0x3207, MIS2031_REG_VALUE_08BIT, 0x3c);
		mis2031_write_reg(mis2031->client, 0x3209, MIS2031_REG_VALUE_08BIT, 0x09);
		mis2031_write_reg(mis2031->client, 0x320b, MIS2031_REG_VALUE_08BIT, 0x88);
		break;
	default:
		break;
	}
}

static int mis2031_set_gain_reg(struct mis2031 *mis2031, u32 gain)
{
	u32 coarse_again = 0, coarse_dgian = 0, fine_dgian = 0;
	u32 dgain;
	int ret = 0;

	if (gain < 32)
		gain = 32;
	else if (gain > MIS2031_GAIN_MAX - 1)
		gain = MIS2031_GAIN_MAX - 1;

	if (gain < 32*64) {
		coarse_again = 1024 - (1024*32/gain);
		coarse_dgian = 0x1;
		fine_dgian = 0;
	} else if (gain < MIS2031_GAIN_MAX) {
		coarse_again = 1008;
		coarse_dgian = (gain/64)/32;
		fine_dgian = (gain/64)%32;
	}
	dgain = ((coarse_dgian << 5) | fine_dgian);
	ret = mis2031_write_reg(mis2031->client,
				MIS2031_REG_DIG_GAIN,
				MIS2031_REG_VALUE_08BIT,
				MIS2031_FETCH_AGAIN_H(dgain));
	ret |= mis2031_write_reg(mis2031->client,
				MIS2031_REG_DIG_FINE_GAIN,
				MIS2031_REG_VALUE_08BIT,
				MIS2031_FETCH_AGAIN_L(dgain));
	ret |= mis2031_write_reg(mis2031->client,
				 MIS2031_REG_ANA_GAIN,
				 MIS2031_REG_VALUE_08BIT,
				 MIS2031_FETCH_AGAIN_H(coarse_again));
	ret |= mis2031_write_reg(mis2031->client,
				 MIS2031_REG_ANA_FINE_GAIN,
				 MIS2031_REG_VALUE_08BIT,
				 MIS2031_FETCH_AGAIN_L(coarse_again));
	ret |= mis2031_write_reg(mis2031->client,
				 MIS2031_REG_GAIN_EXP_VALID,
				 MIS2031_REG_VALUE_08BIT,
				 MIS2031_REG_GAIN_EXP_VALID_VAL);
	return ret;
}

static int mis2031_get_reso_dist(const struct mis2031_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
			abs(mode->height - framefmt->height);
}

static const struct mis2031_mode *
mis2031_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = mis2031_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int mis2031_set_fmt(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_format *fmt)
{
	struct mis2031 *mis2031 = to_mis2031(sd);
	const struct mis2031_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&mis2031->mutex);

	mode = mis2031_find_best_fit(fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&mis2031->mutex);
		return -ENOTTY;
#endif
	} else {
		mis2031->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(mis2031->hblank, h_blank,
					h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(mis2031->vblank, vblank_def,
					MIS2031_VTS_MAX - mode->height,
					1, vblank_def);
		mis2031->cur_fps = mode->max_fps;
	}

	mutex_unlock(&mis2031->mutex);

	return 0;
}

static int mis2031_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct mis2031 *mis2031 = to_mis2031(sd);
	const struct mis2031_mode *mode = mis2031->cur_mode;

	mutex_lock(&mis2031->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&mis2031->mutex);
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
	mutex_unlock(&mis2031->mutex);

	return 0;
}

static int mis2031_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_mbus_code_enum *code)
{
	struct mis2031 *mis2031 = to_mis2031(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = mis2031->cur_mode->bus_fmt;

	return 0;
}

static int mis2031_enum_frame_sizes(struct v4l2_subdev *sd,
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

static int mis2031_enable_test_pattern(struct mis2031 *mis2031, u32 pattern)
{
	u32 val = 0;
	int ret = 0;

	ret = mis2031_read_reg(mis2031->client, MIS2031_REG_TEST_PATTERN,
					MIS2031_REG_VALUE_08BIT, &val);
	if (pattern)
		val |= MIS2031_TEST_PATTERN_BIT_MASK;
	else
		val &= ~MIS2031_TEST_PATTERN_BIT_MASK;

	ret |= mis2031_write_reg(mis2031->client, MIS2031_REG_TEST_PATTERN,
				MIS2031_REG_VALUE_08BIT, val);
	return ret;
}

static int mis2031_g_frame_interval(struct v4l2_subdev *sd,
					struct v4l2_subdev_frame_interval *fi)
{
	struct mis2031 *mis2031 = to_mis2031(sd);
	const struct mis2031_mode *mode = mis2031->cur_mode;

	if (mis2031->streaming)
		fi->interval = mis2031->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

static int mis2031_g_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct mis2031 *mis2031 = to_mis2031(sd);
	const struct mis2031_mode *mode = mis2031->cur_mode;
	u32 val = 1 << (MIS2031_LANES - 1) |
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

static void mis2031_get_module_inf(struct mis2031 *mis2031,
					struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, MIS2031_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, mis2031->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, mis2031->len_name, sizeof(inf->base.lens));
}

static long mis2031_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct mis2031 *mis2031 = to_mis2031(sd);
	struct rkmodule_hdr_cfg *hdr;
	u32 i, h, w;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		mis2031_get_module_inf(mis2031, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = mis2031->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = mis2031->cur_mode->width;
		h = mis2031->cur_mode->height;
		for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				mis2031->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == ARRAY_SIZE(supported_modes)) {
			dev_err(&mis2031->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = mis2031->cur_mode->hts_def - mis2031->cur_mode->width;
			h = mis2031->cur_mode->vts_def - mis2031->cur_mode->height;
			__v4l2_ctrl_modify_range(mis2031->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(mis2031->vblank, h,
						 MIS2031_VTS_MAX - mis2031->cur_mode->height, 1, h);
		}
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = mis2031_write_reg(mis2031->client, MIS2031_REG_CTRL_MODE,
				 MIS2031_REG_VALUE_08BIT, MIS2031_MODE_STREAMING);
		else
			ret = mis2031_write_reg(mis2031->client, MIS2031_REG_CTRL_MODE,
				 MIS2031_REG_VALUE_08BIT, MIS2031_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long mis2031_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = mis2031_ioctl(sd, cmd, inf);
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

		ret = mis2031_ioctl(sd, cmd, hdr);
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
			ret = mis2031_ioctl(sd, cmd, hdr);
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
			ret = mis2031_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = mis2031_ioctl(sd, cmd, &stream);
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

static int __mis2031_start_stream(struct mis2031 *mis2031)
{
	int ret;

	if (!mis2031->is_thunderboot) {
		ret = mis2031_write_array(mis2031->client, mis2031->cur_mode->reg_list);
		if (ret)
			return ret;
		/* In case these controls are set before streaming */
		ret = __v4l2_ctrl_handler_setup(&mis2031->ctrl_handler);
		if (ret)
			return ret;
	}

	ret = mis2031_write_reg(mis2031->client, MIS2031_REG_CTRL_MODE,
				 MIS2031_REG_VALUE_08BIT, MIS2031_MODE_STREAMING);
	return ret;
}

static int __mis2031_stop_stream(struct mis2031 *mis2031)
{
	if (mis2031->is_thunderboot) {
		mis2031->is_first_streamoff = true;
		pm_runtime_put(&mis2031->client->dev);
	}
	return mis2031_write_reg(mis2031->client, MIS2031_REG_CTRL_MODE,
				 MIS2031_REG_VALUE_08BIT, MIS2031_MODE_SW_STANDBY);
}

static int __mis2031_power_on(struct mis2031 *mis2031);
static int mis2031_s_stream(struct v4l2_subdev *sd, int on)
{
	struct mis2031 *mis2031 = to_mis2031(sd);
	struct i2c_client *client = mis2031->client;
	int ret = 0;

	mutex_lock(&mis2031->mutex);
	on = !!on;
	if (on == mis2031->streaming)
		goto unlock_and_return;

	if (on) {
		if (mis2031->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
			mis2031->is_thunderboot = false;
			__mis2031_power_on(mis2031);
		}

		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __mis2031_start_stream(mis2031);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__mis2031_stop_stream(mis2031);
		pm_runtime_put(&client->dev);
	}

	mis2031->streaming = on;

unlock_and_return:
	mutex_unlock(&mis2031->mutex);

	return ret;
}

static int mis2031_s_power(struct v4l2_subdev *sd, int on)
{
	struct mis2031 *mis2031 = to_mis2031(sd);
	struct i2c_client *client = mis2031->client;
	int ret = 0;

	mutex_lock(&mis2031->mutex);

	/* If the power state is not modified - no work to do. */
	if (mis2031->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		if (!mis2031->is_thunderboot) {
			ret = mis2031_write_array(mis2031->client, mis2031_global_regs);
			if (ret) {
				v4l2_err(sd, "could not set init registers\n");
				pm_runtime_put_noidle(&client->dev);
				goto unlock_and_return;
			}
		}

		mis2031->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		mis2031->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&mis2031->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 mis2031_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, MIS2031_XVCLK_FREQ / 1000 / 1000);
}

static int __mis2031_power_on(struct mis2031 *mis2031)
{
	int ret;
	u32 delay_us;
	struct device *dev = &mis2031->client->dev;

	if (!IS_ERR_OR_NULL(mis2031->pins_default)) {
		ret = pinctrl_select_state(mis2031->pinctrl,
						mis2031->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(mis2031->xvclk, MIS2031_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(mis2031->xvclk) != MIS2031_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(mis2031->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (mis2031->is_thunderboot)
		return 0;

	if (!IS_ERR(mis2031->reset_gpio))
		gpiod_set_value_cansleep(mis2031->reset_gpio, 0);

	ret = regulator_bulk_enable(MIS2031_NUM_SUPPLIES, mis2031->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(mis2031->reset_gpio))
		gpiod_set_value_cansleep(mis2031->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(mis2031->pwdn_gpio))
		gpiod_set_value_cansleep(mis2031->pwdn_gpio, 1);

	if (!IS_ERR(mis2031->reset_gpio))
		usleep_range(6000, 8000);
	else
		usleep_range(12000, 16000);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = mis2031_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);
	return 0;

disable_clk:
	clk_disable_unprepare(mis2031->xvclk);

	return ret;
}

static void __mis2031_power_off(struct mis2031 *mis2031)
{
	int ret;
	struct device *dev = &mis2031->client->dev;

	clk_disable_unprepare(mis2031->xvclk);
	if (mis2031->is_thunderboot) {
		if (mis2031->is_first_streamoff) {
			mis2031->is_thunderboot = false;
			mis2031->is_first_streamoff = false;
		} else {
			return;
		}
	}

	if (!IS_ERR(mis2031->pwdn_gpio))
		gpiod_set_value_cansleep(mis2031->pwdn_gpio, 0);
	if (!IS_ERR(mis2031->reset_gpio))
		gpiod_set_value_cansleep(mis2031->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(mis2031->pins_sleep)) {
		ret = pinctrl_select_state(mis2031->pinctrl,
					   mis2031->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(MIS2031_NUM_SUPPLIES, mis2031->supplies);
}

static int __maybe_unused mis2031_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct mis2031 *mis2031 = to_mis2031(sd);

	return __mis2031_power_on(mis2031);
}

static int __maybe_unused mis2031_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct mis2031 *mis2031 = to_mis2031(sd);

	__mis2031_power_off(mis2031);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int mis2031_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct mis2031 *mis2031 = to_mis2031(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct mis2031_mode *def_mode = &supported_modes[0];

	mutex_lock(&mis2031->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&mis2031->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int mis2031_enum_frame_interval(struct v4l2_subdev *sd,
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

static const struct dev_pm_ops mis2031_pm_ops = {
	SET_RUNTIME_PM_OPS(mis2031_runtime_suspend,
			   mis2031_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops mis2031_internal_ops = {
	.open = mis2031_open,
};
#endif

static const struct v4l2_subdev_core_ops mis2031_core_ops = {
	.s_power = mis2031_s_power,
	.ioctl = mis2031_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = mis2031_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops mis2031_video_ops = {
	.s_stream = mis2031_s_stream,
	.g_frame_interval = mis2031_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops mis2031_pad_ops = {
	.enum_mbus_code = mis2031_enum_mbus_code,
	.enum_frame_size = mis2031_enum_frame_sizes,
	.enum_frame_interval = mis2031_enum_frame_interval,
	.get_fmt = mis2031_get_fmt,
	.set_fmt = mis2031_set_fmt,
	.get_mbus_config = mis2031_g_mbus_config,
};

static const struct v4l2_subdev_ops mis2031_subdev_ops = {
	.core = &mis2031_core_ops,
	.video = &mis2031_video_ops,
	.pad = &mis2031_pad_ops,
};

static void mis2031_modify_fps_info(struct mis2031 *mis2031)
{
	const struct mis2031_mode *mode = mis2031->cur_mode;

	mis2031->cur_fps.denominator = mode->max_fps.denominator * mode->vts_def /
						mis2031->cur_vts;
}

static int mis2031_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mis2031 *mis2031 = container_of(ctrl->handler,
							struct mis2031, ctrl_handler);
	struct i2c_client *client = mis2031->client;
	s64 max;
	int ret = 0;
	u32 val = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = mis2031->cur_mode->height + ctrl->val - 8;
		__v4l2_ctrl_modify_range(mis2031->exposure,
					 mis2031->exposure->minimum, max,
					 mis2031->exposure->step,
					 mis2031->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dev_dbg(&client->dev, "set exposure 0x%x\n", ctrl->val);
		if (mis2031->cur_mode->hdr_mode == NO_HDR) {
			val = ctrl->val;
			/* 4 least significant bits of expsoure are fractional part */
			ret = mis2031_write_reg(mis2031->client,
						MIS2031_REG_EXPOSURE_H,
						MIS2031_REG_VALUE_08BIT,
						MIS2031_FETCH_EXP_H(val));
			ret |= mis2031_write_reg(mis2031->client,
						 MIS2031_REG_EXPOSURE_L,
						 MIS2031_REG_VALUE_08BIT,
						 MIS2031_FETCH_EXP_L(val));
			ret |= mis2031_write_reg(mis2031->client,
						 MIS2031_REG_GAIN_EXP_VALID,
						 MIS2031_REG_VALUE_08BIT,
						 MIS2031_REG_GAIN_EXP_VALID_VAL);
		}
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		dev_dbg(&client->dev, "set gain 0x%x\n", ctrl->val);
		if (mis2031->cur_mode->hdr_mode == NO_HDR)
			ret = mis2031_set_gain_reg(mis2031, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		dev_dbg(&client->dev, "set vblank 0x%x\n", ctrl->val);
		ret = mis2031_write_reg(mis2031->client,
					MIS2031_REG_VTS_H,
					MIS2031_REG_VALUE_08BIT,
					(ctrl->val + mis2031->cur_mode->height)
					>> 8);
		ret |= mis2031_write_reg(mis2031->client,
					 MIS2031_REG_VTS_L,
					 MIS2031_REG_VALUE_08BIT,
					 (ctrl->val + mis2031->cur_mode->height)
					 & 0xff);
		mis2031->cur_vts = ctrl->val + mis2031->cur_mode->height;
		if (mis2031->cur_vts != mis2031->cur_mode->vts_def)
			mis2031_modify_fps_info(mis2031);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = mis2031_enable_test_pattern(mis2031, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = mis2031_read_reg(mis2031->client, MIS2031_FLIP_MIRROR_REG,
				       MIS2031_REG_VALUE_08BIT, &val);
		if (ctrl->val)
			val |= MIRROR_BIT_MASK;
		else
			val &= ~MIRROR_BIT_MASK;
		mis2031_set_orientation_reg(mis2031, val);
		break;
	case V4L2_CID_VFLIP:
		ret = mis2031_read_reg(mis2031->client, MIS2031_FLIP_MIRROR_REG,
				       MIS2031_REG_VALUE_08BIT, &val);
		if (ctrl->val)
			val |= FLIP_BIT_MASK;
		else
			val &= ~FLIP_BIT_MASK;
		mis2031_set_orientation_reg(mis2031, val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops mis2031_ctrl_ops = {
	.s_ctrl = mis2031_set_ctrl,
};

static int mis2031_initialize_controls(struct mis2031 *mis2031)
{
	const struct mis2031_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &mis2031->ctrl_handler;
	mode = mis2031->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &mis2031->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, PIXEL_RATE_WITH_315M_10BIT, 1, PIXEL_RATE_WITH_315M_10BIT);

	h_blank = mode->hts_def - mode->width;
	mis2031->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (mis2031->hblank)
		mis2031->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	vblank_def = mode->vts_def - mode->height;
	mis2031->vblank = v4l2_ctrl_new_std(handler, &mis2031_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    MIS2031_VTS_MAX - mode->height,
					    1, vblank_def);
	mis2031->cur_fps = mode->max_fps;
	exposure_max = mode->vts_def - 8;
	mis2031->exposure = v4l2_ctrl_new_std(handler, &mis2031_ctrl_ops,
					      V4L2_CID_EXPOSURE, MIS2031_EXPOSURE_MIN,
					      exposure_max, MIS2031_EXPOSURE_STEP,
					      mode->exp_def);
	mis2031->anal_gain = v4l2_ctrl_new_std(handler, &mis2031_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN, MIS2031_GAIN_MIN,
					       MIS2031_GAIN_MAX, MIS2031_GAIN_STEP,
					       MIS2031_GAIN_DEFAULT);
	mis2031->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
							    &mis2031_ctrl_ops,
					V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(mis2031_test_pattern_menu) - 1,
					0, 0, mis2031_test_pattern_menu);
	v4l2_ctrl_new_std(handler, &mis2031_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &mis2031_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (handler->error) {
		ret = handler->error;
		dev_err(&mis2031->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	mis2031->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int mis2031_check_sensor_id(struct mis2031 *mis2031,
				   struct i2c_client *client)
{
	struct device *dev = &mis2031->client->dev;
	u32 id = 0;
	int ret;

	if (mis2031->is_thunderboot) {
		dev_info(dev, "Enable thunderboot mode, skip sensor id check\n");
		return 0;
	}

	ret = mis2031_read_reg(client, MIS2031_REG_CHIP_ID,
			       MIS2031_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int mis2031_configure_regulators(struct mis2031 *mis2031)
{
	unsigned int i;

	for (i = 0; i < MIS2031_NUM_SUPPLIES; i++)
		mis2031->supplies[i].supply = mis2031_supply_names[i];

	return devm_regulator_bulk_get(&mis2031->client->dev,
				       MIS2031_NUM_SUPPLIES,
				       mis2031->supplies);
}

static int mis2031_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct mis2031 *mis2031;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	mis2031 = devm_kzalloc(dev, sizeof(*mis2031), GFP_KERNEL);
	if (!mis2031)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &mis2031->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &mis2031->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &mis2031->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &mis2031->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	mis2031->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);
	mis2031->client = client;
	mis2031->cur_mode = &supported_modes[0];

	mis2031->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(mis2031->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	if (mis2031->is_thunderboot) {
		mis2031->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
		if (IS_ERR(mis2031->reset_gpio))
			dev_warn(dev, "Failed to get reset-gpios\n");

		mis2031->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_ASIS);
		if (IS_ERR(mis2031->pwdn_gpio))
			dev_warn(dev, "Failed to get pwdn-gpios\n");
	} else {
		mis2031->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
		if (IS_ERR(mis2031->reset_gpio))
			dev_warn(dev, "Failed to get reset-gpios\n");

		mis2031->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
		if (IS_ERR(mis2031->pwdn_gpio))
			dev_warn(dev, "Failed to get pwdn-gpios\n");
	}
	mis2031->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(mis2031->pinctrl)) {
		mis2031->pins_default =
			pinctrl_lookup_state(mis2031->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(mis2031->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		mis2031->pins_sleep =
			pinctrl_lookup_state(mis2031->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(mis2031->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = mis2031_configure_regulators(mis2031);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&mis2031->mutex);

	sd = &mis2031->subdev;
	v4l2_i2c_subdev_init(sd, client, &mis2031_subdev_ops);
	ret = mis2031_initialize_controls(mis2031);
	if (ret)
		goto err_destroy_mutex;

	ret = __mis2031_power_on(mis2031);
	if (ret)
		goto err_free_handler;

	ret = mis2031_check_sensor_id(mis2031, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &mis2031_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	mis2031->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &mis2031->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(mis2031->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 mis2031->module_index, facing,
		 MIS2031_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	if (mis2031->is_thunderboot)
		pm_runtime_get_sync(dev);
	else
		pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__mis2031_power_off(mis2031);
err_free_handler:
	v4l2_ctrl_handler_free(&mis2031->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&mis2031->mutex);

	return ret;
}

static int mis2031_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct mis2031 *mis2031 = to_mis2031(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&mis2031->ctrl_handler);
	mutex_destroy(&mis2031->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__mis2031_power_off(mis2031);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id mis2031_of_match[] = {
	{ .compatible = "imagedesign,mis2031" },
	{},
};
MODULE_DEVICE_TABLE(of, mis2031_of_match);
#endif

static const struct i2c_device_id mis2031_match_id[] = {
	{ "imagedesign,mis2031", 0 },
	{ },
};

static struct i2c_driver mis2031_i2c_driver = {
	.driver = {
		.name = MIS2031_NAME,
		.pm = &mis2031_pm_ops,
		.of_match_table = of_match_ptr(mis2031_of_match),
	},
	.probe = &mis2031_probe,
	.remove = &mis2031_remove,
	.id_table = mis2031_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&mis2031_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&mis2031_i2c_driver);
}

#if defined(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP) && !defined(CONFIG_INITCALL_ASYNC)
subsys_initcall(sensor_mod_init);
#else
device_initcall_sync(sensor_mod_init);
#endif
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("chengdu image design mis2031 sensor driver");
MODULE_LICENSE("GPL");
