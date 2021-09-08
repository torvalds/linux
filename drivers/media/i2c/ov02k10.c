// SPDX-License-Identifier: GPL-2.0
/*
 * ov02k10 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version, only linear mode ready.
 * V0.0X01.0X01 both linear and HDR modes are ready.
 * V0.0X01.0X02 add quick stream on/off
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
#include <linux/rk-preisp.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x02)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIPI_FREQ_360M			360000000
#define MIPI_FREQ_480M			480000000

#define PIXEL_RATE_WITH_360M		(MIPI_FREQ_360M * 2 / 12 * 2)
#define PIXEL_RATE_WITH_480M		(MIPI_FREQ_480M * 2 / 12 * 2)
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define OV02K10_XVCLK_FREQ		24000000

#define CHIP_ID				0x530243
#define OV02K10_REG_CHIP_ID		0x300a

#define OV02K10_REG_CTRL_MODE		0x0100
#define OV02K10_MODE_SW_STANDBY		0x0
#define OV02K10_MODE_STREAMING		BIT(0)

#define	OV02K10_EXPOSURE_MIN		1
#define	OV02K10_EXPOSURE_STEP		1
#define OV02K10_VTS_MAX			0xffff

#define OV02K10_REG_EXP_LONG_H		0x3501
#define OV02K10_REG_EXP_MID_H		0x3541
#define OV02K10_REG_EXP_VS_H		0x3581

#define OV02K10_REG_HCG_SWITCH		0x376C
#define OV02K10_REG_AGAIN_LONG_H	0x3508
#define OV02K10_REG_AGAIN_MID_H		0x3548
#define OV02K10_REG_AGAIN_VS_H		0x3588
#define OV02K10_REG_DGAIN_LONG_H	0x350A
#define OV02K10_REG_DGAIN_MID_H		0x354A
#define OV02K10_REG_DGAIN_VS_H		0x358A
#define OV02K10_GAIN_MIN		0x10
#define OV02K10_GAIN_MAX		0xF7C
#define OV02K10_GAIN_STEP		1
#define OV02K10_GAIN_DEFAULT		0x10

#define OV02K10_GROUP_UPDATE_ADDRESS	0x3208
#define OV02K10_GROUP_UPDATE_START_DATA	0x00
#define OV02K10_GROUP_UPDATE_END_DATA	0x10
#define OV02K10_GROUP_UPDATE_LAUNCH	0xA0

#define OV02K10_SOFTWARE_RESET_REG	0x0103

#define OV02K10_FETCH_MSB_BYTE_EXP(VAL)	(((VAL) >> 8) & 0xFF)	/* 8 Bits */
#define OV02K10_FETCH_LSB_BYTE_EXP(VAL)	((VAL) & 0xFF)	/* 8 Bits */

#define OV02K10_FETCH_LSB_GAIN(VAL)	(((VAL) << 4) & 0xf0)
#define OV02K10_FETCH_MSB_GAIN(VAL)	(((VAL) >> 4) & 0x1f)

#define OV02K10_REG_TEST_PATTERN	0x50C0
#define OV02K10_TEST_PATTERN_ENABLE	0x80
#define OV02K10_TEST_PATTERN_DISABLE	0x0

#define OV02K10_REG_VTS			0x380e

#define REG_NULL			0xFFFF

#define OV02K10_REG_VALUE_08BIT		1
#define OV02K10_REG_VALUE_16BIT		2
#define OV02K10_REG_VALUE_24BIT		3

#define OV02K10_LANES			2

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define OV02K10_NAME			"ov02k10"

#define OV02K10_FLIP_REG		0x3820
#define MIRROR_BIT_MASK			BIT(1)
#define FLIP_BIT_MASK			(BIT(2) | BIT(3))

#define USED_SYS_DEBUG

static const char * const ov02k10_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV02K10_NUM_SUPPLIES ARRAY_SIZE(ov02k10_supply_names)

enum ov02k10_max_pad {
	PAD0,
	PAD1,
	PAD2,
	PAD3,
	PAD_MAX,
};

struct regval {
	u16 addr;
	u8 val;
};

struct ov02k10_mode {
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

struct ov02k10 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV02K10_NUM_SUPPLIES];

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
	struct v4l2_ctrl	*h_flip;
	struct v4l2_ctrl	*v_flip;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct ov02k10_mode *cur_mode;
	u32			cfg_num;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	bool			long_hcg;
	bool			middle_hcg;
	bool			short_hcg;
	u32			flip;
};

#define to_ov02k10(sd) container_of(sd, struct ov02k10, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval ov02k10_global_regs[] = {
	{0x302a, 0x00},
	{0x0103, 0x01},
	{0x0109, 0x01},
	{0x0104, 0x02},
	{0x0306, 0x00},
	{0x0307, 0x00},
	{0x032d, 0x02},
	{0x0317, 0x0a},
	{0x0323, 0x07},
	{0x0324, 0x01},
	{0x0325, 0xb0},
	{0x0327, 0x07},
	{0x300f, 0x11},
	{0x3012, 0x21},
	{0x302d, 0x24},
	{0x3400, 0x00},
	{0x3406, 0x08},
	{0x3504, 0x08},
	{0x3508, 0x01},
	{0x3509, 0x00},
	{0x3544, 0x08},
	{0x3548, 0x01},
	{0x3549, 0x00},
	{0x3584, 0x08},
	{0x3588, 0x01},
	{0x3589, 0x00},
	{0x3601, 0x70},
	{0x3604, 0xe3},
	{0x3608, 0xa8},
	{0x360a, 0xd0},
	{0x360b, 0x08},
	{0x360e, 0xc8},
	{0x360f, 0x66},
	{0x3610, 0x81},
	{0x3611, 0x89},
	{0x3612, 0x4e},
	{0x3613, 0xbd},
	{0x362a, 0x0e},
	{0x362b, 0x0e},
	{0x362c, 0x0e},
	{0x362d, 0x0e},
	{0x362e, 0x0c},
	{0x362f, 0x1a},
	{0x3630, 0x32},
	{0x3631, 0x64},
	{0x3638, 0x00},
	{0x3643, 0x00},
	{0x3644, 0x00},
	{0x3645, 0x00},
	{0x3646, 0x00},
	{0x3647, 0x00},
	{0x3648, 0x00},
	{0x3649, 0x00},
	{0x364a, 0x04},
	{0x364c, 0x0e},
	{0x364d, 0x0e},
	{0x364e, 0x0e},
	{0x364f, 0x0e},
	{0x3650, 0xff},
	{0x3651, 0xff},
	{0x3661, 0x07},
	{0x3662, 0x00},
	{0x3663, 0x20},
	{0x3665, 0x12},
	{0x3667, 0xd4},
	{0x3668, 0x80},
	{0x3681, 0x80},
	{0x3700, 0x26},
	{0x3701, 0x1e},
	{0x3702, 0x25},
	{0x3703, 0x28},
	{0x3790, 0x10},
	{0x3793, 0x04},
	{0x3794, 0x07},
	{0x3796, 0x00},
	{0x3797, 0x02},
	{0x37a1, 0x80},
	{0x37bb, 0x88},
	{0x37be, 0x01},
	{0x37bf, 0x00},
	{0x37c0, 0x01},
	{0x37c7, 0x56},
	{0x37ca, 0x21},
	{0x37cd, 0x90},
	{0x37cf, 0x02},
	{0x37da, 0x00},
	{0x37db, 0x00},
	{0x37dd, 0x00},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x07},
	{0x3805, 0x8f},
	{0x3806, 0x04},
	{0x3807, 0x43},
	{0x3808, 0x07},
	{0x3809, 0x80},
	{0x380a, 0x04},
	{0x380b, 0x38},
	{0x3811, 0x08},
	{0x3813, 0x04},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3821, 0x00},
	{0x3822, 0x14},
	{0x3865, 0x00},
	{0x3866, 0xc0},
	{0x3867, 0x00},
	{0x3868, 0xc0},
	{0x3900, 0x13},
	{0x3940, 0x13},
	{0x3980, 0x13},
	{0x3c01, 0x11},
	{0x3c05, 0x00},
	{0x3c0f, 0x1c},
	{0x3c12, 0x0d},
	{0x3c19, 0x01},
	{0x3c21, 0x40},
	{0x3c3b, 0x18},
	{0x3c3d, 0xc9},
	{0x3c55, 0xcb},
	{0x3ce0, 0x00},
	{0x3ce1, 0x00},
	{0x3ce2, 0x00},
	{0x3ce3, 0x00},
	{0x3d8c, 0x70},
	{0x3d8d, 0x10},
	{0x4033, 0x80},
	{0x4008, 0x02},
	{0x4009, 0x11},
	{0x4004, 0x01},
	{0x4005, 0x00},
	{0x410f, 0x01},
	{0x402e, 0x01},
	{0x402f, 0x00},
	{0x4030, 0x01},
	{0x4031, 0x00},
	{0x4032, 0x9f},
	{0x4050, 0x00},
	{0x4051, 0x07},
	{0x4289, 0x03},
	{0x428a, 0x46},
	{0x430b, 0xff},
	{0x430c, 0xff},
	{0x430d, 0x00},
	{0x430e, 0x00},
	{0x4500, 0x18},
	{0x4501, 0x18},
	{0x4504, 0x00},
	{0x4603, 0x00},
	{0x4640, 0x62},
	{0x4646, 0xaa},
	{0x4647, 0x55},
	{0x4648, 0x99},
	{0x4649, 0x66},
	{0x464d, 0x00},
	{0x4654, 0x11},
	{0x4655, 0x22},
	{0x4800, 0x04},
	{0x4810, 0xff},
	{0x4811, 0xff},
	{0x4837, 0x0c},
	{0x4d00, 0x4e},
	{0x4d01, 0x0c},
	{0x4d09, 0x4f},
	{0x5000, 0x1f},
	{0x5080, 0x00},
	{0x50c0, 0x00},
	{0x5100, 0x00},
	{0x5200, 0x00},
	{0x5201, 0x70},
	{0x5202, 0x03},
	{0x5203, 0x7f},
	{0x3707, 0x0a},
	{0x3714, 0x01},
	{0x371c, 0x00},
	{0x371d, 0x08},
	{0x3762, 0x1d},
	{0x3777, 0x22},
	{0x3779, 0x60},
	{0x377c, 0x48},
	{0x379c, 0x4d},
	{0x3784, 0x06},
	{0x3785, 0x0a},
	{0x37d8, 0x01},
	{0x37dc, 0x00},
	{REG_NULL, 0x00},
};

static const struct regval ov02k10_linear12bit_1920x1080_regs[] = {
	{0x0102, 0x00},
	{0x0305, 0x6c},
	{0x3026, 0x10},
	{0x3027, 0x08},
	{0x3103, 0x25},
	{0x3106, 0x10},
	{0x3408, 0x05},
	{0x340c, 0x05},
	{0x3425, 0x51},
	{0x3426, 0x10},
	{0x3427, 0x14},
	{0x3428, 0x50},
	{0x3429, 0x10},
	{0x342a, 0x10},
	{0x342b, 0x04},
	{0x3605, 0xff},
	{0x3606, 0x01},
	{0x366f, 0x00},
	{0x3670, 0x07},
	{0x3671, 0x08},
	{0x3673, 0x2a},
	{0x3706, 0xb1},
	{0x3708, 0x34},
	{0x3709, 0x50},
	{0x370a, 0x02},
	{0x370b, 0x21},
	{0x371b, 0x13},
	{0x3756, 0xe7},
	{0x3757, 0xe7},
	{0x376c, 0x00},
	{0x3776, 0x03},
	{0x37cc, 0x10},
	{0x37d1, 0xb1},
	{0x37d2, 0x02},
	{0x37d3, 0x21},
	{0x37d5, 0xb1},
	{0x37d6, 0x02},
	{0x37d7, 0x21},
	{0x380d, 0xc8},
	{0x380e, 0x05},
	{0x380f, 0xb4},
	{0x381c, 0x00},
	{0x3820, 0x00},
	{0x384d, 0xc8},
	{0x3858, 0x0d},
	{0x3c5d, 0xec},
	{0x3c5e, 0xec},
	{0x4001, 0x2f},
	{0x400a, 0x03},
	{0x400b, 0x40},
	{0x4011, 0xff},
	{0x4288, 0xcf},
	{0x4314, 0x00},
	{0x4507, 0x02},
	{0x480e, 0x00},
	{0x4813, 0x00},
	{0x484b, 0x27},
	{0x5780, 0x19},
	{0x5786, 0x02},
	{0x032e, 0x05},
	{0x032d, 0x02},
	{0x3501, 0x02},
	{0x380c, 0x04},
	{0x380d, 0xc8},
	{0x384c, 0x04},
	{0x384d, 0xc8},
	{0x380e, 0x0b},
	{0x380f, 0x7c},
	{0x3834, 0x00},
	{0x3832, 0x08},
	{0x3002, 0x00},
	{REG_NULL, 0x00},
};

static const struct regval ov02k10_hdr12bit_1920x1080_regs[] = {
	{0x0102, 0x01},
	{0x0305, 0x6d},
	{0x3026, 0x00},
	{0x3027, 0x00},
	{0x3103, 0x29},
	{0x3106, 0x11},
	{0x3408, 0x01},
	{0x340c, 0x10},
	{0x3425, 0x00},
	{0x3426, 0x00},
	{0x3427, 0x00},
	{0x3428, 0x00},
	{0x3429, 0x00},
	{0x342a, 0x00},
	{0x342b, 0x00},
	{0x3605, 0x7f},
	{0x3606, 0x00},
	{0x366f, 0xc4},
	{0x3670, 0xc7},
	{0x3671, 0x0b},
	{0x3673, 0x6a},
	{0x3706, 0x3e},
	{0x3708, 0x36},
	{0x3709, 0x55},
	{0x370a, 0x00},
	{0x370b, 0xa3},
	{0x371b, 0x16},
	{0x3756, 0x9b},
	{0x3757, 0x9b},
	{0x376c, 0x30},
	{0x3776, 0x05},
	{0x37cc, 0x13},
	{0x37d1, 0x3e},
	{0x37d2, 0x00},
	{0x37d3, 0xa3},
	{0x37d5, 0x3e},
	{0x37d6, 0x00},
	{0x37d7, 0xa3},
	{0x380d, 0x38},
	{0x380e, 0x04},
	{0x380f, 0xe2},
	{0x381c, 0x08},
	{0x3820, 0x01},
	{0x384d, 0x38},
	{0x3858, 0x01},
	{0x3c5d, 0xcf},
	{0x3c5e, 0xcf},
	{0x4001, 0xef},
	{0x400a, 0x04},
	{0x400b, 0xf0},
	{0x4011, 0xbb},
	{0x4288, 0xce},
	{0x4314, 0x04},
	{0x4507, 0x03},
	{0x4508, 0x1a},
	{0x480e, 0x04},
	{0x4813, 0x84},
	{0x484b, 0x67},
	{0x5780, 0x53},
	{0x5786, 0x01},
	{0x032e, 0x0c},
	{0x032d, 0x01},
	{0x3106, 0x10},
	{0x380c, 0x04},
	{0x380d, 0x20},
	{0x384c, 0x04},
	{0x384d, 0x20},
	{0x380e, 0x06},
	{0x380f, 0xa8},
	{0x3834, 0xf0},
	{0x3832, 0x28},
	{0x3002, 0x83},
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
static const struct ov02k10_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR12_1X12,
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x067a,
		.hts_def = 0x04c8 * 2,
		.vts_def = 0x0b7c,
		.reg_list = ov02k10_linear12bit_1920x1080_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR12_1X12,
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},

		.exp_def = 0x026c,
		.hts_def = 0x0420 * 2,
		.vts_def = 0x06a8,
		.reg_list = ov02k10_hdr12bit_1920x1080_regs,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ_360M,
	MIPI_FREQ_480M,
};

static const char * const ov02k10_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int ov02k10_write_reg(struct i2c_client *client, u16 reg,
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

static int ov02k10_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret |= ov02k10_write_reg(client, regs[i].addr,
			OV02K10_REG_VALUE_08BIT, regs[i].val);
	}
	return ret;
}

/* Read registers up to 4 at a time */
static int ov02k10_read_reg(struct i2c_client *client,
			    u16 reg,
			    unsigned int len,
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

static int ov02k10_get_reso_dist(const struct ov02k10_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov02k10_mode *
ov02k10_find_best_fit(struct ov02k10 *ov02k10, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ov02k10->cfg_num; i++) {
		dist = ov02k10_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
			(supported_modes[i].bus_fmt == framefmt->code)) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov02k10_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov02k10 *ov02k10 = to_ov02k10(sd);
	const struct ov02k10_mode *mode;
	s64 h_blank, vblank_def;
	u64 dst_link_freq = 0;
	u64 dst_pixel_rate = 0;

	mutex_lock(&ov02k10->mutex);

	mode = ov02k10_find_best_fit(ov02k10, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov02k10->mutex);
		return -ENOTTY;
#endif
	} else {
		ov02k10->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov02k10->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov02k10->vblank, vblank_def,
					 OV02K10_VTS_MAX - mode->height,
					 1, vblank_def);
		if (mode->hdr_mode == NO_HDR) {
			dst_link_freq = 0;
			dst_pixel_rate = PIXEL_RATE_WITH_360M;
		} else if (mode->hdr_mode == HDR_X2) {
			dst_link_freq = 1;
			dst_pixel_rate = PIXEL_RATE_WITH_480M;
		}
		__v4l2_ctrl_s_ctrl_int64(ov02k10->pixel_rate,
				       dst_pixel_rate);
		__v4l2_ctrl_s_ctrl(ov02k10->link_freq,
				 dst_link_freq);
	}

	mutex_unlock(&ov02k10->mutex);

	return 0;
}

static int ov02k10_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov02k10 *ov02k10 = to_ov02k10(sd);
	const struct ov02k10_mode *mode = ov02k10->cur_mode;

	mutex_lock(&ov02k10->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov02k10->mutex);
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
	mutex_unlock(&ov02k10->mutex);

	return 0;
}

static int ov02k10_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct ov02k10 *ov02k10 = to_ov02k10(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = ov02k10->cur_mode->bus_fmt;

	return 0;
}

static int ov02k10_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct ov02k10 *ov02k10 = to_ov02k10(sd);

	if (fse->index >= ov02k10->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int ov02k10_enable_test_pattern(struct ov02k10 *ov02k10, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | OV02K10_TEST_PATTERN_ENABLE;
	else
		val = OV02K10_TEST_PATTERN_DISABLE;

	return ov02k10_write_reg(ov02k10->client, OV02K10_REG_TEST_PATTERN,
				 OV02K10_REG_VALUE_08BIT, val);
}

static int ov02k10_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct ov02k10 *ov02k10 = to_ov02k10(sd);
	const struct ov02k10_mode *mode = ov02k10->cur_mode;

	mutex_lock(&ov02k10->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&ov02k10->mutex);

	return 0;
}

static int ov02k10_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				 struct v4l2_mbus_config *config)
{
	struct ov02k10 *ov02k10 = to_ov02k10(sd);
	const struct ov02k10_mode *mode = ov02k10->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (OV02K10_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	if (mode->hdr_mode == HDR_X2)
		val = 1 << (OV02K10_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		V4L2_MBUS_CSI2_CHANNEL_1;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void ov02k10_get_module_inf(struct ov02k10 *ov02k10,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, OV02K10_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, ov02k10->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, ov02k10->len_name, sizeof(inf->base.lens));
}


static int ov02k10_set_hdrae(struct ov02k10 *ov02k10,
			     struct preisp_hdrae_exp_s *ae)
{
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_a_gain, m_a_gain, s_a_gain;
	u32 l_d_gain = 1024;
	u32 m_d_gain = 1024;
	int ret = 0;
	u8 l_cg_mode = 0;
	u8 m_cg_mode = 0;
	u8 s_cg_mode = 0;
	u32 gain_switch = 0;
	u8 is_need_switch = 0;

	if (!ov02k10->has_init_exp && !ov02k10->streaming) {
		ov02k10->init_hdrae_exp = *ae;
		ov02k10->has_init_exp = true;
		dev_dbg(&ov02k10->client->dev, "ov02k10 don't stream, record exp for hdr!\n");
		return ret;
	}
	l_exp_time = ae->long_exp_reg;
	m_exp_time = ae->middle_exp_reg;
	s_exp_time = ae->short_exp_reg;
	l_a_gain = ae->long_gain_reg;
	m_a_gain = ae->middle_gain_reg;
	s_a_gain = ae->short_gain_reg;
	l_cg_mode = ae->long_cg_mode;
	m_cg_mode = ae->middle_cg_mode;
	s_cg_mode = ae->short_cg_mode;
	dev_dbg(&ov02k10->client->dev,
		"rev exp:M_exp:0x%x,0x%x,cg %d,S_exp:0x%x,0x%x,cg %d\n",
		m_exp_time, m_a_gain, m_cg_mode,
		s_exp_time, s_a_gain, s_cg_mode);

	if (ov02k10->cur_mode->hdr_mode == HDR_X2) {
		//2 stagger
		l_a_gain = m_a_gain;
		l_exp_time = m_exp_time;
		l_cg_mode = m_cg_mode;
		m_a_gain = s_a_gain;
		m_exp_time = s_exp_time;
		m_cg_mode = s_cg_mode;
	}
	ret = ov02k10_read_reg(ov02k10->client, OV02K10_REG_HCG_SWITCH,
			       OV02K10_REG_VALUE_08BIT, &gain_switch);

	if (ov02k10->long_hcg && l_cg_mode == GAIN_MODE_LCG) {
		gain_switch |= 0x10;
		ov02k10->long_hcg = false;
		is_need_switch++;
	} else if (!ov02k10->long_hcg && l_cg_mode == GAIN_MODE_HCG) {
		gain_switch &= 0xef;
		ov02k10->long_hcg = true;
		is_need_switch++;
	}
	if (ov02k10->middle_hcg && m_cg_mode == GAIN_MODE_LCG) {
		gain_switch |= 0x20;
		ov02k10->middle_hcg = false;
		is_need_switch++;
	} else if (!ov02k10->middle_hcg && m_cg_mode == GAIN_MODE_HCG) {
		gain_switch &= 0xdf;
		ov02k10->middle_hcg = true;
		is_need_switch++;
	}

	if (l_a_gain > 248) {
		l_d_gain = l_a_gain * 1024 / 248;
		l_a_gain = 248;
	}
	if (m_a_gain > 248) {
		m_d_gain = m_a_gain * 1024 / 248;
		m_a_gain = 248;
	}
	ret |= ov02k10_write_reg(ov02k10->client,
		OV02K10_REG_AGAIN_LONG_H,
		OV02K10_REG_VALUE_16BIT,
		(l_a_gain << 4) & 0xff0);
	ret |= ov02k10_write_reg(ov02k10->client,
		OV02K10_REG_DGAIN_LONG_H,
		OV02K10_REG_VALUE_24BIT,
		(l_d_gain << 6) & 0xfffc0);
	ret |= ov02k10_write_reg(ov02k10->client,
		OV02K10_REG_EXP_LONG_H,
		OV02K10_REG_VALUE_16BIT,
		l_exp_time);
	ret |= ov02k10_write_reg(ov02k10->client,
		OV02K10_REG_AGAIN_MID_H,
		OV02K10_REG_VALUE_16BIT,
		(m_a_gain << 4) & 0xff0);
	ret |= ov02k10_write_reg(ov02k10->client,
		OV02K10_REG_DGAIN_MID_H,
		OV02K10_REG_VALUE_24BIT,
		(m_d_gain << 6) & 0xfffc0);
	ret |= ov02k10_write_reg(ov02k10->client,
		OV02K10_REG_EXP_MID_H,
		OV02K10_REG_VALUE_16BIT,
		m_exp_time);
	if (is_need_switch) {
		ret |= ov02k10_write_reg(ov02k10->client,
			OV02K10_GROUP_UPDATE_ADDRESS,
			OV02K10_REG_VALUE_08BIT,
			OV02K10_GROUP_UPDATE_START_DATA);
		ret |= ov02k10_write_reg(ov02k10->client,
			OV02K10_REG_HCG_SWITCH,
			OV02K10_REG_VALUE_08BIT,
			gain_switch);
		ret |= ov02k10_write_reg(ov02k10->client,
			OV02K10_GROUP_UPDATE_ADDRESS,
			OV02K10_REG_VALUE_08BIT,
			OV02K10_GROUP_UPDATE_END_DATA);
		ret |= ov02k10_write_reg(ov02k10->client,
			OV02K10_GROUP_UPDATE_ADDRESS,
			OV02K10_REG_VALUE_08BIT,
			OV02K10_GROUP_UPDATE_LAUNCH);
	}
	return ret;
}

static int ov02k10_set_conversion_gain(struct ov02k10 *ov02k10, u32 *cg)
{
	int ret = 0;
	struct i2c_client *client = ov02k10->client;
	u32 cur_cg = *cg;
	u32 val = 0;
	s32 is_need_change = 0;

	dev_dbg(&ov02k10->client->dev, "set conversion gain %d\n", cur_cg);
	mutex_lock(&ov02k10->mutex);
	ret = ov02k10_read_reg(client,
		OV02K10_REG_HCG_SWITCH,
		OV02K10_REG_VALUE_08BIT,
		&val);
	if (ov02k10->long_hcg && cur_cg == GAIN_MODE_LCG) {
		val |= 0x10;
		is_need_change++;
		ov02k10->long_hcg = false;
	} else if (!ov02k10->long_hcg && cur_cg == GAIN_MODE_HCG) {
		val &= 0xef;
		is_need_change++;
		ov02k10->long_hcg = true;
	}
	if (is_need_change) {
		ret |= ov02k10_write_reg(client,
			OV02K10_GROUP_UPDATE_ADDRESS,
			OV02K10_REG_VALUE_08BIT,
			OV02K10_GROUP_UPDATE_START_DATA);
		ret |= ov02k10_write_reg(client,
			OV02K10_REG_HCG_SWITCH,
			OV02K10_REG_VALUE_08BIT,
			val);
		ret |= ov02k10_write_reg(client,
			OV02K10_GROUP_UPDATE_ADDRESS,
			OV02K10_REG_VALUE_08BIT,
			OV02K10_GROUP_UPDATE_END_DATA);
		ret |= ov02k10_write_reg(client,
			OV02K10_GROUP_UPDATE_ADDRESS,
			OV02K10_REG_VALUE_08BIT,
			OV02K10_GROUP_UPDATE_LAUNCH);
	}
	mutex_unlock(&ov02k10->mutex);
	dev_dbg(&client->dev, "set conversion gain %d, (reg,val)=(0x%x,0x%x)\n",
		cur_cg, OV02K10_REG_HCG_SWITCH, val);
	return ret;
}

#ifdef USED_SYS_DEBUG
//ag: echo 0 >  /sys/devices/platform/ff510000.i2c/i2c-1/1-0036-1/cam_s_cg
static ssize_t set_conversion_gain_status(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov02k10 *ov02k10 = to_ov02k10(sd);
	int status = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &status);
	if (!ret && status >= 0 && status < 2)
		ov02k10_set_conversion_gain(ov02k10, &status);
	else
		dev_err(dev, "input 0 for LCG, 1 for HCG, cur %d\n", status);
	return count;
}

static struct device_attribute attributes[] = {
	__ATTR(cam_s_cg, S_IWUSR, NULL, set_conversion_gain_status),
};

static int add_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		if (device_create_file(dev, attributes + i))
			goto undo;
	return 0;
undo:
	for (i--; i >= 0 ; i--)
		device_remove_file(dev, attributes + i);
	dev_err(dev, "%s: failed to create sysfs interface\n", __func__);
	return -ENODEV;
}
#endif

static long ov02k10_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ov02k10 *ov02k10 = to_ov02k10(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	long ret = 0;
	u32 i, h, w;
	u64 dst_link_freq = 0;
	u64 dst_pixel_rate = 0;
	u32 stream = 0;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		return ov02k10_set_hdrae(ov02k10, arg);
	case RKMODULE_SET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		w = ov02k10->cur_mode->width;
		h = ov02k10->cur_mode->height;
		for (i = 0; i < ov02k10->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			h == supported_modes[i].height &&
			supported_modes[i].hdr_mode == hdr_cfg->hdr_mode) {
				ov02k10->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == ov02k10->cfg_num) {
			dev_err(&ov02k10->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr_cfg->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = ov02k10->cur_mode->hts_def - ov02k10->cur_mode->width;
			h = ov02k10->cur_mode->vts_def - ov02k10->cur_mode->height;
			__v4l2_ctrl_modify_range(ov02k10->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(ov02k10->vblank, h,
				OV02K10_VTS_MAX - ov02k10->cur_mode->height,
				1, h);
			if (ov02k10->cur_mode->hdr_mode == NO_HDR) {
				dst_link_freq = 0;
				dst_pixel_rate = PIXEL_RATE_WITH_360M;
			} else if (ov02k10->cur_mode->hdr_mode == HDR_X2) {
				dst_link_freq = 1;
				dst_pixel_rate = PIXEL_RATE_WITH_480M;
			}

			__v4l2_ctrl_s_ctrl_int64(ov02k10->pixel_rate,
				       dst_pixel_rate);
			__v4l2_ctrl_s_ctrl(ov02k10->link_freq,
				 dst_link_freq);

			dev_info(&ov02k10->client->dev,
				"sensor mode: %d\n",
				ov02k10->cur_mode->hdr_mode);
		}
		break;
	case RKMODULE_GET_MODULE_INFO:
		ov02k10_get_module_inf(ov02k10, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = ov02k10->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		ret = ov02k10_set_conversion_gain(ov02k10, (u32 *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = ov02k10_write_reg(ov02k10->client, OV02K10_REG_CTRL_MODE,
				OV02K10_REG_VALUE_08BIT, OV02K10_MODE_STREAMING);
		else
			ret = ov02k10_write_reg(ov02k10->client, OV02K10_REG_CTRL_MODE,
				OV02K10_REG_VALUE_08BIT, OV02K10_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ov02k10_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	long ret;
	u32 cg = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = ov02k10_ioctl(sd, cmd, inf);
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
			ret = ov02k10_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = ov02k10_ioctl(sd, cmd, hdr);
		if (!ret)
			ret = copy_to_user(up, hdr, sizeof(*hdr));
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
			ret = ov02k10_ioctl(sd, cmd, hdr);
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
			ret = ov02k10_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		ret = copy_from_user(&cg, up, sizeof(cg));
		if (!ret)
			ret = ov02k10_ioctl(sd, cmd, &cg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = ov02k10_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int ov02k10_init_conversion_gain(struct ov02k10 *ov02k10)
{
	int ret = 0;
	struct i2c_client *client = ov02k10->client;
	u32 val = 0;

	ret = ov02k10_read_reg(client,
		OV02K10_REG_HCG_SWITCH,
		OV02K10_REG_VALUE_08BIT,
		&val);
	val |= 0x70;
	ret |= ov02k10_write_reg(client,
		OV02K10_REG_HCG_SWITCH,
		OV02K10_REG_VALUE_08BIT,
		val);
	ov02k10->long_hcg = false;
	ov02k10->middle_hcg = false;
	ov02k10->short_hcg = false;
	return ret;
}

static int __ov02k10_start_stream(struct ov02k10 *ov02k10)
{
	int ret;

	ret = ov02k10_write_array(ov02k10->client, ov02k10_global_regs);
	if (ret) {
		dev_err(&ov02k10->client->dev,
			 "could not set init registers\n");
		return ret;
	}
	ret = ov02k10_write_array(ov02k10->client, ov02k10->cur_mode->reg_list);
	if (ret)
		return ret;
	ret = ov02k10_init_conversion_gain(ov02k10);
	if (ret)
		return ret;
	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&ov02k10->ctrl_handler);
	if (ret)
		return ret;
	if (ov02k10->has_init_exp && ov02k10->cur_mode->hdr_mode != NO_HDR) {
		ret = ov02k10_ioctl(&ov02k10->subdev,
				    PREISP_CMD_SET_HDRAE_EXP,
				    &ov02k10->init_hdrae_exp);
		if (ret) {
			dev_err(&ov02k10->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}
	return ov02k10_write_reg(ov02k10->client, OV02K10_REG_CTRL_MODE,
		OV02K10_REG_VALUE_08BIT, OV02K10_MODE_STREAMING);
}

static int __ov02k10_stop_stream(struct ov02k10 *ov02k10)
{
	ov02k10->has_init_exp = false;
	return ov02k10_write_reg(ov02k10->client, OV02K10_REG_CTRL_MODE,
		OV02K10_REG_VALUE_08BIT, OV02K10_MODE_SW_STANDBY);
}

static int ov02k10_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov02k10 *ov02k10 = to_ov02k10(sd);
	struct i2c_client *client = ov02k10->client;
	int ret = 0;

	mutex_lock(&ov02k10->mutex);
	on = !!on;
	if (on == ov02k10->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __ov02k10_start_stream(ov02k10);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__ov02k10_stop_stream(ov02k10);
		pm_runtime_put(&client->dev);
	}

	ov02k10->streaming = on;

unlock_and_return:
	mutex_unlock(&ov02k10->mutex);

	return ret;
}

static int ov02k10_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov02k10 *ov02k10 = to_ov02k10(sd);
	struct i2c_client *client = ov02k10->client;
	int ret = 0;

	mutex_lock(&ov02k10->mutex);

	/* If the power state is not modified - no work to do. */
	if (ov02k10->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret |= ov02k10_write_reg(ov02k10->client,
			OV02K10_SOFTWARE_RESET_REG,
			OV02K10_REG_VALUE_08BIT,
			0x01);
		usleep_range(100, 200);

		ov02k10->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		ov02k10->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&ov02k10->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov02k10_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OV02K10_XVCLK_FREQ / 1000 / 1000);
}

static int __ov02k10_power_on(struct ov02k10 *ov02k10)
{
	int ret;
	u32 delay_us;
	struct device *dev = &ov02k10->client->dev;

	if (!IS_ERR_OR_NULL(ov02k10->pins_default)) {
		ret = pinctrl_select_state(ov02k10->pinctrl,
					   ov02k10->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(ov02k10->xvclk, OV02K10_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(ov02k10->xvclk) != OV02K10_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(ov02k10->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(ov02k10->reset_gpio))
		gpiod_set_value_cansleep(ov02k10->reset_gpio, 0);

	if (!IS_ERR(ov02k10->power_gpio)) {
		gpiod_set_value_cansleep(ov02k10->power_gpio, 1);
		usleep_range(5000, 5100);
	}

	ret = regulator_bulk_enable(OV02K10_NUM_SUPPLIES, ov02k10->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(ov02k10->reset_gpio))
		gpiod_set_value_cansleep(ov02k10->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(ov02k10->pwdn_gpio))
		gpiod_set_value_cansleep(ov02k10->pwdn_gpio, 1);
	usleep_range(12000, 16000);
	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ov02k10_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(ov02k10->xvclk);

	return ret;
}

static void __ov02k10_power_off(struct ov02k10 *ov02k10)
{
	int ret;
	struct device *dev = &ov02k10->client->dev;

	if (!IS_ERR(ov02k10->pwdn_gpio))
		gpiod_set_value_cansleep(ov02k10->pwdn_gpio, 0);
	clk_disable_unprepare(ov02k10->xvclk);
	if (!IS_ERR(ov02k10->reset_gpio))
		gpiod_set_value_cansleep(ov02k10->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(ov02k10->pins_sleep)) {
		ret = pinctrl_select_state(ov02k10->pinctrl,
					   ov02k10->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(OV02K10_NUM_SUPPLIES, ov02k10->supplies);
}

static int ov02k10_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov02k10 *ov02k10 = to_ov02k10(sd);

	return __ov02k10_power_on(ov02k10);
}

static int ov02k10_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov02k10 *ov02k10 = to_ov02k10(sd);

	__ov02k10_power_off(ov02k10);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov02k10_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov02k10 *ov02k10 = to_ov02k10(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov02k10_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov02k10->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov02k10->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int ov02k10_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct ov02k10 *ov02k10 = to_ov02k10(sd);

	if (fie->index >= ov02k10->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int ov02k10_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = 0;
		sel->r.width = 1920;
		sel->r.top = 0;
		sel->r.height = 1080;
		return 0;
	}
	return -EINVAL;
}

static const struct dev_pm_ops ov02k10_pm_ops = {
	SET_RUNTIME_PM_OPS(ov02k10_runtime_suspend,
			   ov02k10_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov02k10_internal_ops = {
	.open = ov02k10_open,
};
#endif

static const struct v4l2_subdev_core_ops ov02k10_core_ops = {
	.s_power = ov02k10_s_power,
	.ioctl = ov02k10_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = ov02k10_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops ov02k10_video_ops = {
	.s_stream = ov02k10_s_stream,
	.g_frame_interval = ov02k10_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops ov02k10_pad_ops = {
	.enum_mbus_code = ov02k10_enum_mbus_code,
	.enum_frame_size = ov02k10_enum_frame_sizes,
	.enum_frame_interval = ov02k10_enum_frame_interval,
	.get_fmt = ov02k10_get_fmt,
	.set_fmt = ov02k10_set_fmt,
	.get_selection = ov02k10_get_selection,
	.get_mbus_config = ov02k10_g_mbus_config,
};

static const struct v4l2_subdev_ops ov02k10_subdev_ops = {
	.core	= &ov02k10_core_ops,
	.video	= &ov02k10_video_ops,
	.pad	= &ov02k10_pad_ops,
};

static int ov02k10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov02k10 *ov02k10 = container_of(ctrl->handler,
					       struct ov02k10, ctrl_handler);
	struct i2c_client *client = ov02k10->client;
	s64 max;
	int ret = 0;
	u32 again, dgain;
	u32 val = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov02k10->cur_mode->height + ctrl->val - 8;
		__v4l2_ctrl_modify_range(ov02k10->exposure,
					 ov02k10->exposure->minimum, max,
					 ov02k10->exposure->step,
					 ov02k10->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = ov02k10_write_reg(ov02k10->client,
					OV02K10_REG_EXP_LONG_H,
					OV02K10_REG_VALUE_16BIT,
					ctrl->val);
		dev_dbg(&client->dev, "set exposure 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (ctrl->val > 248) {
			dgain = ctrl->val * 1024 / 248;
			again = 248;
		} else {
			dgain = 1024;
			again = ctrl->val;
		}
		ret = ov02k10_write_reg(ov02k10->client,
					OV02K10_REG_AGAIN_LONG_H,
					OV02K10_REG_VALUE_16BIT,
					(again << 4) & 0xff0);
		ret |= ov02k10_write_reg(ov02k10->client,
					OV02K10_REG_DGAIN_LONG_H,
					OV02K10_REG_VALUE_24BIT,
					(dgain << 6) & 0xfffc0);
		dev_dbg(&client->dev, "set analog gain 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = ov02k10_write_reg(ov02k10->client, OV02K10_REG_VTS,
					OV02K10_REG_VALUE_16BIT,
					ctrl->val + ov02k10->cur_mode->height);
		dev_dbg(&client->dev, "set vblank 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov02k10_enable_test_pattern(ov02k10, ctrl->val);
		dev_dbg(&client->dev, "set test pattern 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = ov02k10_read_reg(ov02k10->client, OV02K10_FLIP_REG,
				       OV02K10_REG_VALUE_08BIT,
				       &val);
		if (ctrl->val)
			val |= MIRROR_BIT_MASK;
		else
			val &= ~MIRROR_BIT_MASK;
		ret = ov02k10_write_reg(ov02k10->client, OV02K10_FLIP_REG,
					OV02K10_REG_VALUE_08BIT,
					val);
		if (ret == 0)
			ov02k10->flip = val;
		dev_dbg(&client->dev, "set hflip 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		ret = ov02k10_read_reg(ov02k10->client, OV02K10_FLIP_REG,
				       OV02K10_REG_VALUE_08BIT,
				       &val);
		if (ctrl->val)
			val |= FLIP_BIT_MASK;
		else
			val &= ~FLIP_BIT_MASK;
		ret = ov02k10_write_reg(ov02k10->client, OV02K10_FLIP_REG,
					OV02K10_REG_VALUE_08BIT,
					val);
		if (ret == 0)
			ov02k10->flip = val;
		dev_dbg(&client->dev, "set vflip 0x%x\n",
			ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov02k10_ctrl_ops = {
	.s_ctrl = ov02k10_set_ctrl,
};

static int ov02k10_initialize_controls(struct ov02k10 *ov02k10)
{
	const struct ov02k10_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	u64 dst_link_freq = 0;
	u64 dst_pixel_rate = 0;

	handler = &ov02k10->ctrl_handler;
	mode = ov02k10->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &ov02k10->mutex;

	ov02k10->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
			V4L2_CID_LINK_FREQ,
			1, 0, link_freq_menu_items);

	if (ov02k10->cur_mode->hdr_mode == NO_HDR) {
		dst_link_freq = 0;
		dst_pixel_rate = PIXEL_RATE_WITH_360M;
	} else {
		dst_link_freq = 1;
		dst_pixel_rate = PIXEL_RATE_WITH_480M;
	}
	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	ov02k10->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
			V4L2_CID_PIXEL_RATE,
			0, PIXEL_RATE_WITH_480M,
			1, dst_pixel_rate);

	__v4l2_ctrl_s_ctrl(ov02k10->link_freq,
			 dst_link_freq);

	h_blank = mode->hts_def - mode->width;
	ov02k10->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov02k10->hblank)
		ov02k10->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ov02k10->vblank = v4l2_ctrl_new_std(handler, &ov02k10_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OV02K10_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 8;
	ov02k10->exposure = v4l2_ctrl_new_std(handler, &ov02k10_ctrl_ops,
				V4L2_CID_EXPOSURE, OV02K10_EXPOSURE_MIN,
				exposure_max, OV02K10_EXPOSURE_STEP,
				mode->exp_def);

	ov02k10->anal_gain = v4l2_ctrl_new_std(handler, &ov02k10_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, OV02K10_GAIN_MIN,
				OV02K10_GAIN_MAX, OV02K10_GAIN_STEP,
				OV02K10_GAIN_DEFAULT);

	ov02k10->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ov02k10_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov02k10_test_pattern_menu) - 1,
				0, 0, ov02k10_test_pattern_menu);
	ov02k10->h_flip = v4l2_ctrl_new_std(handler, &ov02k10_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	ov02k10->v_flip = v4l2_ctrl_new_std(handler, &ov02k10_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	ov02k10->flip = 0;
	if (handler->error) {
		ret = handler->error;
		dev_err(&ov02k10->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ov02k10->subdev.ctrl_handler = handler;
	ov02k10->has_init_exp = false;
	ov02k10->long_hcg = false;
	ov02k10->middle_hcg = false;
	ov02k10->short_hcg = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov02k10_check_sensor_id(struct ov02k10 *ov02k10,
				  struct i2c_client *client)
{
	struct device *dev = &ov02k10->client->dev;
	u32 id = 0;
	int ret;

	ret = ov02k10_read_reg(client, OV02K10_REG_CHIP_ID,
			       OV02K10_REG_VALUE_24BIT, &id);

	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int ov02k10_configure_regulators(struct ov02k10 *ov02k10)
{
	unsigned int i;

	for (i = 0; i < OV02K10_NUM_SUPPLIES; i++)
		ov02k10->supplies[i].supply = ov02k10_supply_names[i];

	return devm_regulator_bulk_get(&ov02k10->client->dev,
				       OV02K10_NUM_SUPPLIES,
				       ov02k10->supplies);
}

static int ov02k10_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct ov02k10 *ov02k10;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	ov02k10 = devm_kzalloc(dev, sizeof(*ov02k10), GFP_KERNEL);
	if (!ov02k10)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &ov02k10->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &ov02k10->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &ov02k10->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &ov02k10->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE,
				   &hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}

	ov02k10->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < ov02k10->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			ov02k10->cur_mode = &supported_modes[i];
			break;
		}
	}
	ov02k10->client = client;

	ov02k10->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov02k10->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	ov02k10->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(ov02k10->power_gpio))
		dev_warn(dev, "Failed to get power-gpios\n");

	ov02k10->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov02k10->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	ov02k10->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov02k10->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ov02k10->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(ov02k10->pinctrl)) {
		ov02k10->pins_default =
			pinctrl_lookup_state(ov02k10->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(ov02k10->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		ov02k10->pins_sleep =
			pinctrl_lookup_state(ov02k10->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(ov02k10->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = ov02k10_configure_regulators(ov02k10);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&ov02k10->mutex);

	sd = &ov02k10->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov02k10_subdev_ops);
	ret = ov02k10_initialize_controls(ov02k10);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov02k10_power_on(ov02k10);
	if (ret)
		goto err_free_handler;

	ret = ov02k10_check_sensor_id(ov02k10, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov02k10_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov02k10->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &ov02k10->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(ov02k10->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 ov02k10->module_index, facing,
		 OV02K10_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);
#ifdef USED_SYS_DEBUG
	add_sysfs_interfaces(dev);
#endif
	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__ov02k10_power_off(ov02k10);
err_free_handler:
	v4l2_ctrl_handler_free(&ov02k10->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov02k10->mutex);

	return ret;
}

static int ov02k10_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov02k10 *ov02k10 = to_ov02k10(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov02k10->ctrl_handler);
	mutex_destroy(&ov02k10->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov02k10_power_off(ov02k10);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov02k10_of_match[] = {
	{ .compatible = "ovti,ov02k10" },
	{},
};
MODULE_DEVICE_TABLE(of, ov02k10_of_match);
#endif

static const struct i2c_device_id ov02k10_match_id[] = {
	{ "ovti,ov02k10", 0 },
	{ },
};

static struct i2c_driver ov02k10_i2c_driver = {
	.driver = {
		.name = OV02K10_NAME,
		.pm = &ov02k10_pm_ops,
		.of_match_table = of_match_ptr(ov02k10_of_match),
	},
	.probe		= &ov02k10_probe,
	.remove		= &ov02k10_remove,
	.id_table	= ov02k10_match_id,
};

#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT
module_i2c_driver(ov02k10_i2c_driver);
#else
static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov02k10_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov02k10_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);
#endif

MODULE_DESCRIPTION("OmniVision ov02k10 sensor driver");
MODULE_LICENSE("GPL v2");
