// SPDX-License-Identifier: GPL-2.0
/*
 * mis4001 driver
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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x02)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIS4001_LANES			2
#define MIS4001_BITS_PER_SAMPLE		10
#define MIS4001_LINK_FREQ		337500000
#define PIXEL_RATE_WITH_337M_10BIT	(MIS4001_LINK_FREQ * 2 * \
                            MIS4001_LANES / MIS4001_BITS_PER_SAMPLE)
#define MIS4001_XVCLK_FREQ		    27000000

#define MIS4001_CHIP_ID			0x1311
#define MIS4001_REG_CHIP_ID		0x3000

#define MIS4001_REG_CTRL_MODE		0x3006
#define MIS4001_MODE_SW_STANDBY		0x3
#define MIS4001_MODE_STREAMING		0x0

#define MIS4001_REG_EXPOSURE_H		0x3100
#define MIS4001_REG_EXPOSURE_L		0x3101
#define	MIS4001_EXPOSURE_MIN		2
#define	MIS4001_EXPOSURE_STEP		1
#define MIS4001_VTS_MAX			0x7fff

#define MIS4001_REG_DIG_GAIN		0x3A00
#define MIS4001_REG_DIG_FINE_GAIN	0x3A01
#define MIS4001_REG_ANA_GAIN		0x3102
#define MIS4001_GAIN_MIN		0x0080
#define MIS4001_GAIN_MAX		32768
#define MIS4001_GAIN_STEP		1
#define MIS4001_GAIN_DEFAULT		0x80

#define MIS4001_REG_TEST_PATTERN	0x3400
#define MIS4001_TEST_PATTERN_BIT_MASK	BIT(3)

#define MIS4001_REG_VTS_H		0x310c
#define MIS4001_REG_VTS_L		0x310d

#define MIS4001_FLIP_MIRROR_REG		0x3007
#define MIRROR_BIT_MASK			BIT(0)
#define FLIP_BIT_MASK			BIT(1)


#define MIS4001_FETCH_EXP_H(VAL)	(((VAL) >> 8) & 0xFF)
#define MIS4001_FETCH_EXP_L(VAL)	((VAL) & 0xFF)

#define MIS4001_FETCH_AGAIN_H(VAL)	(((VAL) >> 8) & 0xFF)
#define MIS4001_FETCH_AGAIN_L(VAL)	((VAL) & 0xFF)

#define MIS4001_FETCH_MIRROR(VAL, ENABLE)	(ENABLE ? VAL | 0x01 : VAL & 0xFE)
#define MIS4001_FETCH_FLIP(VAL, ENABLE)		(ENABLE ? VAL | 0x02 : VAL & 0xFD)

#define REG_NULL			0xFFFF

#define MIS4001_REG_VALUE_08BIT		1
#define MIS4001_REG_VALUE_16BIT		2
#define MIS4001_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define MIS4001_NAME			"mis4001"


static const char * const mis4001_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define MIS4001_NUM_SUPPLIES ARRAY_SIZE(mis4001_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct mis4001_mode {
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

struct mis4001 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[MIS4001_NUM_SUPPLIES];

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
	const struct mis4001_mode *cur_mode;
	struct v4l2_fract	cur_fps;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
};

#define to_mis4001(sd) container_of(sd, struct mis4001, subdev)

/*
 * Xclk 27Mhz
 */
static const struct regval mis4001_global_regs[] = {
	{REG_NULL, 0x00},
};

/*
 * Xclk 27Mhz
 * max_framerate 25fps
 * mipi_datarate per lane 337.5Mbps, 2lane
 */
static const struct regval mis4001_linear_10_2560x1440_regs[] = {
	{0x300a, 0x01},
	{0x3006, 0x02},
	{0x4220, 0x2b},
	{0x4221, 0x6b},
	{0x4222, 0xab},
	{0x4223, 0xeb},
	{0x3011, 0x2b},
	{0x3302, 0x02},
	{0x3307, 0x64},
	{0x3306, 0x01},
	{0x3309, 0x01},
	{0x3308, 0x03},
	{0x330a, 0x04},
	{0x330b, 0x09},
	{0x310f, 0x68},
	{0x310e, 0x0d},
	{0x310d, 0x25},
	{0x310c, 0x06},
	{0x3115, 0x10},
	{0x3114, 0x00},
	{0x3117, 0x0f},
	{0x3116, 0x0a},
	{0x3111, 0xfc},
	{0x3110, 0x00},
	{0x3113, 0x9d},
	{0x3112, 0x06},
	{0x3128, 0x0f},//FW<4096 FFF
	{0x3129, 0xff},
	{0x3012, 0x03},
	{0x3f00, 0x01},
	{0x3f02, 0x07},
	{0x3f01, 0x00},
	{0x3f04, 0x2a},
	{0x3f03, 0x00},
	{0x3f06, 0xa5},
	{0x3f05, 0x04},
	{0x3f08, 0xff},
	{0x3f07, 0x1f},
	{0x3f0a, 0xa4},
	{0x3f09, 0x01},
	{0x3f0c, 0x38},
	{0x3f0b, 0x00},
	{0x3f0e, 0xff},
	{0x3f0d, 0x1f},
	{0x3f10, 0xff},
	{0x3f0f, 0x1f},
	{0x3f13, 0x07},
	{0x3f12, 0x00},
	{0x3f15, 0x9d},
	{0x3f14, 0x01},
	{0x3f17, 0x31},
	{0x3f16, 0x00},
	{0x3f19, 0x73},
	{0x3f18, 0x01},
	{0x3f1b, 0x00},
	{0x3f1a, 0x00},
	{0x3f1d, 0xa9},
	{0x3f1c, 0x04},
	{0x3f1f, 0xff},
	{0x3f1e, 0x1f},
	{0x3f21, 0xff},
	{0x3f20, 0x1f},
	{0x3f23, 0x85},
	{0x3f22, 0x00},
	{0x3f25, 0x27},
	{0x3f24, 0x01},
	{0x3f28, 0x46},
	{0x3f27, 0x00},
	{0x3f2a, 0x07},
	{0x3f29, 0x00},
	{0x3f2c, 0x3f},
	{0x3f2b, 0x00},
	{0x3f2e, 0x70},
	{0x3f2d, 0x01},
	{0x3f30, 0x38},
	{0x3f2f, 0x00},
	{0x3f32, 0x3f},
	{0x3f31, 0x00},
	{0x3f34, 0xd1},
	{0x3f33, 0x00},
	{0x3f36, 0xc0},
	{0x3f35, 0x00},
	{0x3f38, 0x2f},
	{0x3f37, 0x02},
	{0x3f3a, 0x5d},
	{0x3f39, 0x02},
	{0x3f4f, 0x5d},
	{0x3f4e, 0x02},
	{0x3f51, 0x5d},
	{0x3f50, 0x02},
	{0x3f53, 0x5d},
	{0x3f52, 0x02},
	{0x3f55, 0x5d},
	{0x3f54, 0x02},
	{0x3f3c, 0x9a},
	{0x3f3b, 0x00},
	{0x3f3e, 0x09},
	{0x3f3d, 0x04},
	{0x3f40, 0x93},
	{0x3f3f, 0x01},
	{0x3f42, 0x8f},
	{0x3f41, 0x00},
	{0x3f44, 0xb0},
	{0x3f43, 0x04},
	{0x3129, 0x45},
	{0x3128, 0x00},
	{0x312b, 0x4a},
	{0x312a, 0x00},
	{0x312f, 0xb2},
	{0x312e, 0x00},
	{0x3124, 0x09},
	{0x4200, 0x09},
	{0x4201, 0x00},
	{0x4214, 0x60},
	{0x420E, 0x94},
	{0x4240, 0x8d},
	{0x4242, 0x03},
	{0x4224, 0x00},
	{0x4225, 0x0a},
	{0x4226, 0xa0},
	{0x4227, 0x05},
	{0x4228, 0x00},
	{0x4229, 0x0a},
	{0x422a, 0xa0},
	{0x422b, 0x05},
	{0x422c, 0x00},
	{0x422d, 0x0a},
	{0x422e, 0xa0},
	{0x422f, 0x05},
	{0x4230, 0x00},
	{0x4231, 0x0a},
	{0x4232, 0xa0},
	{0x4233, 0x05},
	{0x4509, 0x0f},
	{0x4505, 0x00},
	{0x4501, 0xff},
	{0x4502, 0x33},
	{0x4503, 0x11},
	{0x4501, 0xf0},
	{0x4502, 0x30},
	{0x4503, 0x10},
	{0x3A01, 0xA0},
	{0x401E, 0x3C},
	{0x401d, 0xa0},
	{0x3012, 0x03},
	{0x3E00, 0x00},
	{0x3E01, 0x10},
	{0x400D, 0x30},
	{0x3500, 0x1b},  //1b/13
	{0x3501, 0x03},
	{0x3508, 0x0a},
	{0x3508, 0x04},
	{0x3513, 0x01},
	{0x3514, 0x09},
	{0x3515, 0x0b},
	{0x3702, 0x80},
	{0x3704, 0x80},
	{0x3706, 0x80},
	{0x3708, 0x80},
	{0x400D, 0x30},  //优化奇偶行及行噪
	{0x4004, 0x20},  //RCS CM电流最小优化横带
	{0x4005, 0x0c},
	{0x4009, 0x09},
	{0x400a, 0x48},
	{0x4006, 0x86},
	{0x4019, 0x08},
	{0x4003, 0x0a},
	{0x3f42, 0x58},
	{0x3f49, 0x60},
	{0x3f38, 0x4d},
	{REG_NULL, 0x00},
};

static const struct mis4001_mode supported_modes[] = {
	{
		.width = 2560,
		.height = 1440,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.exp_def = 0x0040,
		.hts_def = 3432,
		.vts_def = 1573,
		.bus_fmt = MEDIA_BUS_FMT_SGRBG10_1X10,
		.reg_list = mis4001_linear_10_2560x1440_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}
};

static const s64 link_freq_menu_items[] = {
	MIS4001_LINK_FREQ
};

static const char * const mis4001_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

static int mis4001_write_reg(struct i2c_client *client, u16 reg,
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

static int mis4001_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = mis4001_write_reg(client, regs[i].addr,
					MIS4001_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int mis4001_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static void mis4001_set_orientation_reg(struct mis4001 *mis4001, u32 en_flip_mir)
{
	switch (en_flip_mir) {
	case  0:
		mis4001_write_reg(mis4001->client, 0x3007, MIS4001_REG_VALUE_08BIT, 0x00);
		mis4001_write_reg(mis4001->client, 0x3111, MIS4001_REG_VALUE_08BIT, 0xFC);
		mis4001_write_reg(mis4001->client, 0x3113, MIS4001_REG_VALUE_08BIT, 0x9D);
		mis4001_write_reg(mis4001->client, 0x3115, MIS4001_REG_VALUE_08BIT, 0x10);
		mis4001_write_reg(mis4001->client, 0x3117, MIS4001_REG_VALUE_08BIT, 0x0F);
		break;
	case  1:
		mis4001_write_reg(mis4001->client, 0x3007, MIS4001_REG_VALUE_08BIT, 0x01);
		mis4001_write_reg(mis4001->client, 0x3111, MIS4001_REG_VALUE_08BIT, 0xFC);
		mis4001_write_reg(mis4001->client, 0x3113, MIS4001_REG_VALUE_08BIT, 0x9D);
		mis4001_write_reg(mis4001->client, 0x3115, MIS4001_REG_VALUE_08BIT, 0x11);
		mis4001_write_reg(mis4001->client, 0x3117, MIS4001_REG_VALUE_08BIT, 0x10);
		break;
	case  2:
		mis4001_write_reg(mis4001->client, 0x3007, MIS4001_REG_VALUE_08BIT, 0x01);
		mis4001_write_reg(mis4001->client, 0x3111, MIS4001_REG_VALUE_08BIT, 0xFD);
		mis4001_write_reg(mis4001->client, 0x3113, MIS4001_REG_VALUE_08BIT, 0x9E);
		mis4001_write_reg(mis4001->client, 0x3115, MIS4001_REG_VALUE_08BIT, 0x10);
		mis4001_write_reg(mis4001->client, 0x3117, MIS4001_REG_VALUE_08BIT, 0x0F);
		break;
	case  3:
		mis4001_write_reg(mis4001->client, 0x3007, MIS4001_REG_VALUE_08BIT, 0x03);
		mis4001_write_reg(mis4001->client, 0x3113, MIS4001_REG_VALUE_08BIT, 0XFD);
		mis4001_write_reg(mis4001->client, 0x3207, MIS4001_REG_VALUE_08BIT, 0x9E);
		mis4001_write_reg(mis4001->client, 0x3115, MIS4001_REG_VALUE_08BIT, 0x11);
		mis4001_write_reg(mis4001->client, 0x3117, MIS4001_REG_VALUE_08BIT, 0x0F);
		break;
	default:
		break;
	}
}

static int mis4001_set_gain_reg(struct mis4001 *mis4001, u32 gain)
{
	u8 gain_h, gain_l, u8Reg0x3102, u8Reg0x3a00, u8Reg0x3a01, u8Reg0x4003;
	int ret = 0;
	u8 u8Reg0x401d = 0;

	if (gain < 128)
		gain = 128;
	else if (gain > MIS4001_GAIN_MAX)
		gain = MIS4001_GAIN_MAX;

	if (128 <= gain && gain < 256) {//128 * 2
		gain_h = 0;
		gain_l = (gain - 128) / 4;
		u8Reg0x3a00 = 0;
		u8Reg0x3a01 = 160;  //128->160, 高亮偏粉
	} else if (gain >= 256 && gain < 512) {//128 * 4
		gain_h = 1;
		gain_l = (gain - 256) / 8;
		u8Reg0x3a00 = 0;
		u8Reg0x3a01 = 160;
	} else if (gain >= 512 && gain < 1024) {//128 * 8
		gain_h = 2;
		gain_l = (gain - 512) / 16;
		u8Reg0x3a00 = 0;
		u8Reg0x3a01 = 160;
	} else if (gain >= 1024 && gain < 2048) {//128 * 16
		gain_h = 3;
		gain_l = (gain - 1024) / 32;
		u8Reg0x3a00 = 0;
		u8Reg0x3a01 = 160;
	} else if (gain >= 2048 && gain < 4096) {//128 * 32  Dgain
		gain_h = 3;
		gain_l = 31;
		u8Reg0x3a00 = 0;
		u8Reg0x3a01 = ((gain - 2048) / 16 + 128);
		u8Reg0x3a01 = (u8Reg0x3a01 < 160) ? 160 : u8Reg0x3a01;
	} else if (gain >= 4096 && gain < 8192) {//128 * 64
		gain_h = 3;
		gain_l = 31;
		u8Reg0x3a00 = 1;
		u8Reg0x3a01 = ((gain - 4096) / 16);
	} else if (gain >= 8192 && gain < 12288) {//128 * 96
		gain_h = 3;
		gain_l = 31;
		u8Reg0x3a00 = 2;
		u8Reg0x3a01 = ((gain - 8192) / 16);
	} else if (gain >= 12288 && gain < 16384) {//128 * 128
		gain_h = 3;
		gain_l = 31;
		u8Reg0x3a00 = 3;
		u8Reg0x3a01 = ((gain - 12288) / 16);
	} else if (gain >= 16384 && gain < 20480) {//128 * 160
		gain_h = 3;
		gain_l = 31;
		u8Reg0x3a00 = 4;
		u8Reg0x3a01 = ((gain - 16384) / 16);
	} else if (gain >= 20480 && gain < 24576) {//128 * 192
		gain_h = 3;
		gain_l = 31;
		u8Reg0x3a00 = 5;
		u8Reg0x3a01 = ((gain - 20480) / 16);
	} else if (gain >= 24576 && gain < 28672) {//128 * 224
		gain_h = 3;
		gain_l = 31;
		u8Reg0x3a00 = 6;
		u8Reg0x3a01 = ((gain - 24576) / 16);
	} else if (gain >= 28672 && gain < 32768) {//128 * 256
		gain_h = 3;
		gain_l = 31;
		u8Reg0x3a00 = 7;
		u8Reg0x3a01 = ((gain - 28672) / 16);
	} else {
		gain_h = 3;
		gain_l = 31;
		u8Reg0x3a00 = 7;
		u8Reg0x3a01 = 255;
	}

	u8Reg0x3102 = ((gain_h << 5) | gain_l);

	// 竖条纹优化，但低增益下需添加防止太阳黑子逻辑
	if (gain >= 128 && gain <= 256) {
		u8Reg0x4003 = 0xb;
		u8Reg0x401d = 0xa0;
	} else {
		u8Reg0x4003 = 0xa;
		u8Reg0x401d = 0xa7;
	}

	ret = mis4001_write_reg(mis4001->client,
				0x401d,
				MIS4001_REG_VALUE_08BIT,
				u8Reg0x401d);

	ret |= mis4001_write_reg(mis4001->client,
				 0x4003,
				 MIS4001_REG_VALUE_08BIT,
				 u8Reg0x4003);
	ret |= mis4001_write_reg(mis4001->client,
				 MIS4001_REG_DIG_GAIN,
				 MIS4001_REG_VALUE_08BIT,
				 u8Reg0x3a00);
	ret |= mis4001_write_reg(mis4001->client,
				 MIS4001_REG_DIG_FINE_GAIN,
				 MIS4001_REG_VALUE_08BIT,
				 u8Reg0x3a01);
	ret |= mis4001_write_reg(mis4001->client,
				 MIS4001_REG_ANA_GAIN,
				 MIS4001_REG_VALUE_08BIT,
				 u8Reg0x3102);

	return ret;
}


static int mis4001_get_reso_dist(const struct mis4001_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct mis4001_mode *
mis4001_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = mis4001_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int mis4001_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct mis4001 *mis4001 = to_mis4001(sd);
	const struct mis4001_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&mis4001->mutex);

	mode = mis4001_find_best_fit(fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&mis4001->mutex);
		return -ENOTTY;
#endif
	} else {
		mis4001->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(mis4001->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(mis4001->vblank, vblank_def,
					 MIS4001_VTS_MAX - mode->height,
					 1, vblank_def);
		mis4001->cur_fps = mode->max_fps;
	}

	mutex_unlock(&mis4001->mutex);

	return 0;
}

static int mis4001_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct mis4001 *mis4001 = to_mis4001(sd);
	const struct mis4001_mode *mode = mis4001->cur_mode;

	mutex_lock(&mis4001->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&mis4001->mutex);
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
	mutex_unlock(&mis4001->mutex);

	return 0;
}

static int mis4001_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct mis4001 *mis4001 = to_mis4001(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = mis4001->cur_mode->bus_fmt;

	return 0;
}

static int mis4001_enum_frame_sizes(struct v4l2_subdev *sd,
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

static int mis4001_enable_test_pattern(struct mis4001 *mis4001, u32 pattern)
{
	u32 val = 0;
	int ret = 0;

	ret = mis4001_read_reg(mis4001->client, MIS4001_REG_TEST_PATTERN,
			       MIS4001_REG_VALUE_08BIT, &val);
	if (pattern)
		val |= MIS4001_TEST_PATTERN_BIT_MASK;
	else
		val &= ~MIS4001_TEST_PATTERN_BIT_MASK;

	ret |= mis4001_write_reg(mis4001->client, MIS4001_REG_TEST_PATTERN,
				 MIS4001_REG_VALUE_08BIT, val);
	return ret;
}

static int mis4001_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct mis4001 *mis4001 = to_mis4001(sd);
	const struct mis4001_mode *mode = mis4001->cur_mode;

	if (mis4001->streaming)
		fi->interval = mis4001->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

static int mis4001_g_mbus_config(struct v4l2_subdev *sd,
				unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct mis4001 *mis4001 = to_mis4001(sd);
	const struct mis4001_mode *mode = mis4001->cur_mode;
	u32 val = 1 << (MIS4001_LANES - 1) |
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

static void mis4001_get_module_inf(struct mis4001 *mis4001,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, MIS4001_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, mis4001->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, mis4001->len_name, sizeof(inf->base.lens));
}

static long mis4001_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct mis4001 *mis4001 = to_mis4001(sd);
	struct rkmodule_hdr_cfg *hdr;
	u32 i, h, w;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		mis4001_get_module_inf(mis4001, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = mis4001->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = mis4001->cur_mode->width;
		h = mis4001->cur_mode->height;
		for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				mis4001->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == ARRAY_SIZE(supported_modes)) {
			dev_err(&mis4001->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = mis4001->cur_mode->hts_def - mis4001->cur_mode->width;
			h = mis4001->cur_mode->vts_def - mis4001->cur_mode->height;
			__v4l2_ctrl_modify_range(mis4001->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(mis4001->vblank, h,
						 MIS4001_VTS_MAX - mis4001->cur_mode->height, 1, h);
		}
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = mis4001_write_reg(mis4001->client, MIS4001_REG_CTRL_MODE,
				 MIS4001_REG_VALUE_08BIT, MIS4001_MODE_STREAMING);
		else
			ret = mis4001_write_reg(mis4001->client, MIS4001_REG_CTRL_MODE,
				 MIS4001_REG_VALUE_08BIT, MIS4001_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long mis4001_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = mis4001_ioctl(sd, cmd, inf);
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

		ret = mis4001_ioctl(sd, cmd, hdr);
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
			ret = mis4001_ioctl(sd, cmd, hdr);
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
			ret = mis4001_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = mis4001_ioctl(sd, cmd, &stream);
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

static int __mis4001_start_stream(struct mis4001 *mis4001)
{
	int ret;

	ret = mis4001_write_array(mis4001->client, mis4001->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&mis4001->ctrl_handler);
	if (ret)
		return ret;

	return mis4001_write_reg(mis4001->client, MIS4001_REG_CTRL_MODE,
				 MIS4001_REG_VALUE_08BIT, MIS4001_MODE_STREAMING);
}

static int __mis4001_stop_stream(struct mis4001 *mis4001)
{
	return mis4001_write_reg(mis4001->client, MIS4001_REG_CTRL_MODE,
				 MIS4001_REG_VALUE_08BIT, MIS4001_MODE_SW_STANDBY);
}

static int mis4001_s_stream(struct v4l2_subdev *sd, int on)
{
	struct mis4001 *mis4001 = to_mis4001(sd);
	struct i2c_client *client = mis4001->client;
	int ret = 0;

	mutex_lock(&mis4001->mutex);
	on = !!on;
	if (on == mis4001->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __mis4001_start_stream(mis4001);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__mis4001_stop_stream(mis4001);
		pm_runtime_put(&client->dev);
	}

	mis4001->streaming = on;

unlock_and_return:
	mutex_unlock(&mis4001->mutex);

	return ret;
}

static int mis4001_s_power(struct v4l2_subdev *sd, int on)
{
	struct mis4001 *mis4001 = to_mis4001(sd);
	struct i2c_client *client = mis4001->client;
	int ret = 0;

	mutex_lock(&mis4001->mutex);

	/* If the power state is not modified - no work to do. */
	if (mis4001->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = mis4001_write_array(mis4001->client, mis4001_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		mis4001->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		mis4001->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&mis4001->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 mis4001_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, MIS4001_XVCLK_FREQ / 1000 / 1000);
}

static int __mis4001_power_on(struct mis4001 *mis4001)
{
	int ret;
	u32 delay_us;
	struct device *dev = &mis4001->client->dev;

	if (!IS_ERR_OR_NULL(mis4001->pins_default)) {
		ret = pinctrl_select_state(mis4001->pinctrl,
					   mis4001->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(mis4001->xvclk, MIS4001_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(mis4001->xvclk) != MIS4001_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(mis4001->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(mis4001->reset_gpio))
		gpiod_set_value_cansleep(mis4001->reset_gpio, 0);

	ret = regulator_bulk_enable(MIS4001_NUM_SUPPLIES, mis4001->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(mis4001->reset_gpio))
		gpiod_set_value_cansleep(mis4001->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(mis4001->pwdn_gpio))
		gpiod_set_value_cansleep(mis4001->pwdn_gpio, 1);

	if (!IS_ERR(mis4001->reset_gpio))
		usleep_range(6000, 8000);
	else
		usleep_range(12000, 16000);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = mis4001_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(mis4001->xvclk);

	return ret;
}

static void __mis4001_power_off(struct mis4001 *mis4001)
{
	int ret;
	struct device *dev = &mis4001->client->dev;

	if (!IS_ERR(mis4001->pwdn_gpio))
		gpiod_set_value_cansleep(mis4001->pwdn_gpio, 0);
	clk_disable_unprepare(mis4001->xvclk);
	if (!IS_ERR(mis4001->reset_gpio))
		gpiod_set_value_cansleep(mis4001->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(mis4001->pins_sleep)) {
		ret = pinctrl_select_state(mis4001->pinctrl,
					   mis4001->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(MIS4001_NUM_SUPPLIES, mis4001->supplies);
}

static int mis4001_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct mis4001 *mis4001 = to_mis4001(sd);

	return __mis4001_power_on(mis4001);
}

static int mis4001_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct mis4001 *mis4001 = to_mis4001(sd);

	__mis4001_power_off(mis4001);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int mis4001_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct mis4001 *mis4001 = to_mis4001(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct mis4001_mode *def_mode = &supported_modes[0];

	mutex_lock(&mis4001->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&mis4001->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int mis4001_enum_frame_interval(struct v4l2_subdev *sd,
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

static const struct dev_pm_ops mis4001_pm_ops = {
	SET_RUNTIME_PM_OPS(mis4001_runtime_suspend,
			   mis4001_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops mis4001_internal_ops = {
	.open = mis4001_open,
};
#endif

static const struct v4l2_subdev_core_ops mis4001_core_ops = {
	.s_power = mis4001_s_power,
	.ioctl = mis4001_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = mis4001_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops mis4001_video_ops = {
	.s_stream = mis4001_s_stream,
	.g_frame_interval = mis4001_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops mis4001_pad_ops = {
	.enum_mbus_code = mis4001_enum_mbus_code,
	.enum_frame_size = mis4001_enum_frame_sizes,
	.enum_frame_interval = mis4001_enum_frame_interval,
	.get_fmt = mis4001_get_fmt,
	.set_fmt = mis4001_set_fmt,
	.get_mbus_config = mis4001_g_mbus_config,
};

static const struct v4l2_subdev_ops mis4001_subdev_ops = {
	.core	= &mis4001_core_ops,
	.video	= &mis4001_video_ops,
	.pad	= &mis4001_pad_ops,
};

static void mis4001_modify_fps_info(struct mis4001 *mis4001)
{
	const struct mis4001_mode *mode = mis4001->cur_mode;

	mis4001->cur_fps.denominator = mode->max_fps.denominator * mode->vts_def /
				      mis4001->cur_vts;
}

static int mis4001_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mis4001 *mis4001 = container_of(ctrl->handler,
					       struct mis4001, ctrl_handler);
	struct i2c_client *client = mis4001->client;
	s64 max;
	int ret = 0;
	u32 val = 0;
	u32 u32Reg0x4007, expmin, expmax;
	u64 sleep_time = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = mis4001->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(mis4001->exposure,
					 mis4001->exposure->minimum, max,
					 mis4001->exposure->step,
					 mis4001->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dev_dbg(&client->dev, "set exposure 0x%x\n", ctrl->val);
		if (mis4001->cur_mode->hdr_mode == NO_HDR) {
			val = ctrl->val;
			/* 4 least significant bits of expsoure are fractional part */
			ret = mis4001_write_reg(mis4001->client,
						MIS4001_REG_EXPOSURE_H,
						MIS4001_REG_VALUE_08BIT,
						MIS4001_FETCH_EXP_H(val));
			ret |= mis4001_write_reg(mis4001->client,
						MIS4001_REG_EXPOSURE_L,
						MIS4001_REG_VALUE_08BIT,
						MIS4001_FETCH_EXP_L(val));

			/* Special strategy: To solve the problem of exposure layering:
			 * When the exposure is not fully filled, the gain will be increased,
			 * resulting in layering phenomenon
			 */

			//When the exposure time is 1/200 (0.005) s, the exp register is 196
			expmin = (mis4001->cur_mode->max_fps.denominator / mis4001->cur_mode->max_fps.numerator)
				 * mis4001->cur_mode->vts_def * 1 / 200;
			expmax = mis4001->cur_mode->vts_def - expmin;
			if (((expmin <= val) && (expmax >= val)))
				u32Reg0x4007 = 0x78;
			else
				u32Reg0x4007 = 0xc4;

			ret |= mis4001_write_reg(mis4001->client,
						 0x4007,
						 MIS4001_REG_VALUE_08BIT,
						 u32Reg0x4007);
		}
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		dev_dbg(&client->dev, "set gain 0x%x\n", ctrl->val);
		if (mis4001->cur_mode->hdr_mode == NO_HDR)
			ret = mis4001_set_gain_reg(mis4001, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		dev_dbg(&client->dev, "set vblank 0x%x\n", ctrl->val);
		mis4001->cur_vts = ctrl->val + mis4001->cur_mode->height;
		ret = mis4001_write_reg(mis4001->client,
					MIS4001_REG_CTRL_MODE,
					MIS4001_REG_VALUE_08BIT,
					0x02);
		sleep_time = mis4001->cur_mode->max_fps.denominator / mis4001->cur_mode->max_fps.numerator *
			     mis4001->cur_mode->vts_def / mis4001->cur_vts;
		sleep_time = div_u64(1000000, sleep_time);
		usleep_range(sleep_time, sleep_time + 1000);
		ret |= mis4001_write_reg(mis4001->client,
					 MIS4001_REG_VTS_H,
					 MIS4001_REG_VALUE_08BIT,
					 (ctrl->val + mis4001->cur_mode->height)
					 >> 8);
		ret |= mis4001_write_reg(mis4001->client,
					 MIS4001_REG_VTS_L,
					 MIS4001_REG_VALUE_08BIT,
					 (ctrl->val + mis4001->cur_mode->height)
					 & 0xff);
		ret |= mis4001_write_reg(mis4001->client,
					 MIS4001_REG_CTRL_MODE,
					 MIS4001_REG_VALUE_08BIT,
					 0x00);
		if (mis4001->cur_vts != mis4001->cur_mode->vts_def)
			mis4001_modify_fps_info(mis4001);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = mis4001_enable_test_pattern(mis4001, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = mis4001_read_reg(mis4001->client, MIS4001_FLIP_MIRROR_REG,
				       MIS4001_REG_VALUE_08BIT, &val);
		if (ctrl->val)
			val |= MIRROR_BIT_MASK;
		else
			val &= ~MIRROR_BIT_MASK;
		mis4001_set_orientation_reg(mis4001, val);
		break;
	case V4L2_CID_VFLIP:
		ret = mis4001_read_reg(mis4001->client, MIS4001_FLIP_MIRROR_REG,
				       MIS4001_REG_VALUE_08BIT, &val);
		if (ctrl->val)
			val |= FLIP_BIT_MASK;
		else
			val &= ~FLIP_BIT_MASK;
		mis4001_set_orientation_reg(mis4001, val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;

}

static const struct v4l2_ctrl_ops mis4001_ctrl_ops = {
	.s_ctrl = mis4001_set_ctrl,
};

static int mis4001_initialize_controls(struct mis4001 *mis4001)
{
	const struct mis4001_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &mis4001->ctrl_handler;
	mode = mis4001->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &mis4001->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, PIXEL_RATE_WITH_337M_10BIT, 1, PIXEL_RATE_WITH_337M_10BIT);

	h_blank = mode->hts_def - mode->width;
	mis4001->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (mis4001->hblank)
		mis4001->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	vblank_def = mode->vts_def - mode->height;
	mis4001->vblank = v4l2_ctrl_new_std(handler, &mis4001_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    MIS4001_VTS_MAX - mode->height,
					    1, vblank_def);
	mis4001->cur_fps = mode->max_fps;
	exposure_max = mode->vts_def - 4;
	mis4001->exposure = v4l2_ctrl_new_std(handler, &mis4001_ctrl_ops,
					      V4L2_CID_EXPOSURE, MIS4001_EXPOSURE_MIN,
					      exposure_max, MIS4001_EXPOSURE_STEP,
					      mode->exp_def);
	mis4001->anal_gain = v4l2_ctrl_new_std(handler, &mis4001_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN, MIS4001_GAIN_MIN,
					       MIS4001_GAIN_MAX, MIS4001_GAIN_STEP,
					       MIS4001_GAIN_DEFAULT);
	mis4001->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
							    &mis4001_ctrl_ops,
					V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(mis4001_test_pattern_menu) - 1,
					0, 0, mis4001_test_pattern_menu);
	v4l2_ctrl_new_std(handler, &mis4001_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &mis4001_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (handler->error) {
		ret = handler->error;
		dev_err(&mis4001->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	mis4001->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

/* sensor id check */
static int mis4001_check_sensor_id(struct mis4001 *mis4001,
				   struct i2c_client *client)
{
	struct device *dev = &mis4001->client->dev;
	u32 id = 0;
	int ret;

	ret = mis4001_read_reg(client, MIS4001_REG_CHIP_ID,
					MIS4001_REG_VALUE_16BIT, &id);

	if (id != MIS4001_CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected 0x%04x sensor\n", MIS4001_CHIP_ID);

	return 0;
}

static int mis4001_configure_regulators(struct mis4001 *mis4001)
{
	unsigned int i;

	for (i = 0; i < MIS4001_NUM_SUPPLIES; i++)
		mis4001->supplies[i].supply = mis4001_supply_names[i];

	return devm_regulator_bulk_get(&mis4001->client->dev,
				       MIS4001_NUM_SUPPLIES,
				       mis4001->supplies);
}

static int mis4001_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct mis4001 *mis4001;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	mis4001 = devm_kzalloc(dev, sizeof(*mis4001), GFP_KERNEL);
	if (!mis4001)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &mis4001->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &mis4001->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &mis4001->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &mis4001->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	mis4001->client = client;
	mis4001->cur_mode = &supported_modes[0];

	mis4001->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(mis4001->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	mis4001->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(mis4001->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	mis4001->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(mis4001->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	mis4001->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(mis4001->pinctrl)) {
		mis4001->pins_default =
			pinctrl_lookup_state(mis4001->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(mis4001->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		mis4001->pins_sleep =
			pinctrl_lookup_state(mis4001->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(mis4001->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = mis4001_configure_regulators(mis4001);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&mis4001->mutex);

	sd = &mis4001->subdev;
	v4l2_i2c_subdev_init(sd, client, &mis4001_subdev_ops);
	ret = mis4001_initialize_controls(mis4001);
	if (ret)
		goto err_destroy_mutex;

	ret = __mis4001_power_on(mis4001);
	if (ret)
		goto err_free_handler;

	ret = mis4001_check_sensor_id(mis4001, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &mis4001_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	mis4001->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &mis4001->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(mis4001->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 mis4001->module_index, facing,
		 MIS4001_NAME, dev_name(sd->dev));
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
	__mis4001_power_off(mis4001);
err_free_handler:
	v4l2_ctrl_handler_free(&mis4001->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&mis4001->mutex);

	return ret;
}

static int mis4001_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct mis4001 *mis4001 = to_mis4001(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&mis4001->ctrl_handler);
	mutex_destroy(&mis4001->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__mis4001_power_off(mis4001);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id mis4001_of_match[] = {
	{ .compatible = "imagedesign,mis4001" },
	{},
};
MODULE_DEVICE_TABLE(of, mis4001_of_match);
#endif

static const struct i2c_device_id mis4001_match_id[] = {
	{ "imagedesign,mis4001", 0 },
	{ },
};

static struct i2c_driver mis4001_i2c_driver = {
	.driver = {
		.name = MIS4001_NAME,
		.pm = &mis4001_pm_ops,
		.of_match_table = of_match_ptr(mis4001_of_match),
	},
	.probe = &mis4001_probe,
	.remove = &mis4001_remove,
	.id_table = mis4001_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&mis4001_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&mis4001_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("chengdu image design mis4001 sensor driver");
MODULE_LICENSE("GPL");
