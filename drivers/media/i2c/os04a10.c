// SPDX-License-Identifier: GPL-2.0
/*
 * os04a10 driver
 *
 * Copyright (C) 2020 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 support conversion gain switch.
 * V0.0X01.0X02 add debug interface for conversion gain switch.
 * V0.0X01.0X03 support enum sensor fmt
 * V0.0X01.0X04 add quick stream on/off
 * V0.0X01.0X05 support get dcg ratio from sensor
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
#include "../platform/rockchip/isp/rkisp_tb_helper.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x05)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIPI_FREQ_360M			360000000
#define MIPI_FREQ_648M			648000000

#define PIXEL_RATE_WITH_360M		(MIPI_FREQ_360M * 2 / 10 * 4)
#define PIXEL_RATE_WITH_648M		(MIPI_FREQ_648M * 2 / 12 * 4)

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define OS04A10_XVCLK_FREQ		24000000

#define CHIP_ID				0x530441
#define OS04A10_REG_CHIP_ID		0x300a

#define OS04A10_REG_CTRL_MODE		0x0100
#define OS04A10_MODE_SW_STANDBY		0x0
#define OS04A10_MODE_STREAMING		BIT(0)

#define	OS04A10_EXPOSURE_MIN		2
#define	OS04A10_EXPOSURE_STEP		1
#define OS04A10_VTS_MAX			0xffff

#define OS04A10_REG_EXP_LONG_H		0x3501
#define OS04A10_REG_EXP_MID_H		0x3541
#define OS04A10_REG_EXP_VS_H		0x3581

#define OS04A10_REG_HCG_SWITCH		0x376C
#define OS04A10_REG_AGAIN_LONG_H	0x3508
#define OS04A10_REG_AGAIN_MID_H		0x3548
#define OS04A10_REG_AGAIN_VS_H		0x3588
#define OS04A10_REG_DGAIN_LONG_H	0x350A
#define OS04A10_REG_DGAIN_MID_H		0x354A
#define OS04A10_REG_DGAIN_VS_H		0x358A
#define OS04A10_GAIN_MIN		0x10
#define OS04A10_GAIN_MAX		0xF7C
#define OS04A10_GAIN_STEP		1
#define OS04A10_GAIN_DEFAULT		0x10

#define OS04A10_GROUP_UPDATE_ADDRESS	0x3208
#define OS04A10_GROUP_UPDATE_START_DATA	0x00
#define OS04A10_GROUP_UPDATE_END_DATA	0x10
#define OS04A10_GROUP_UPDATE_END_LAUNCH	0xA0

#define OS04A10_SOFTWARE_RESET_REG	0x0103

#define OS04A10_FETCH_MSB_BYTE_EXP(VAL)	(((VAL) >> 8) & 0xFF)	/* 8 Bits */
#define OS04A10_FETCH_LSB_BYTE_EXP(VAL)	((VAL) & 0xFF)	/* 8 Bits */

#define OS04A10_FETCH_LSB_GAIN(VAL)	(((VAL) << 4) & 0xf0)
#define OS04A10_FETCH_MSB_GAIN(VAL)	(((VAL) >> 4) & 0x1f)

#define OS04A10_REG_TEST_PATTERN	0x5080
#define OS04A10_TEST_PATTERN_ENABLE	0x80
#define OS04A10_TEST_PATTERN_DISABLE	0x0

#define OS04A10_REG_VTS			0x380e

#define REG_NULL			0xFFFF

#define OS04A10_REG_VALUE_08BIT		1
#define OS04A10_REG_VALUE_16BIT		2
#define OS04A10_REG_VALUE_24BIT		3

#define OS04A10_LANES			4

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define OS04A10_NAME			"os04a10"

#define USED_SYS_DEBUG

struct preisp_hdrae_exp_s init_hdrae_exp;

static const char * const os04a10_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OS04A10_NUM_SUPPLIES ARRAY_SIZE(os04a10_supply_names)

#define OS04A10_FLIP_REG		0x3820
#define MIRROR_BIT_MASK			BIT(1)
#define FLIP_BIT_MASK			BIT(2)

enum os04a10_max_pad {
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

struct os04a10_mode {
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

struct os04a10 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OS04A10_NUM_SUPPLIES];

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
	const struct os04a10_mode *cur_mode;
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
	bool			is_thunderboot;
	bool			is_thunderboot_ng;
	bool			is_first_streamoff;
	u8			flip;
	u32			dcg_ratio;
};

#define to_os04a10(sd) container_of(sd, struct os04a10, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval os04a10_global_regs[] = {
	{0x0109, 0x01},
	{0x0104, 0x02},
	{0x0102, 0x00},
	{0x0306, 0x00},
	{0x0307, 0x00},
	{0x030a, 0x01},
	{0x0322, 0x01},
	{0x0323, 0x02},
	{0x0324, 0x00},
	{0x0327, 0x05},
	{0x0329, 0x02},
	{0x032c, 0x02},
	{0x032d, 0x02},
	{0x300f, 0x11},
	{0x3012, 0x41},
	{0x3026, 0x10},
	{0x3027, 0x08},
	{0x302d, 0x24},
	{0x3104, 0x01},
	{0x3106, 0x11},
	{0x3400, 0x00},
	{0x3408, 0x05},
	{0x340c, 0x0c},
	{0x340d, 0xb0},
	{0x3425, 0x51},
	{0x3426, 0x50},
	{0x3427, 0x15},
	{0x3428, 0x50},
	{0x3429, 0x10},
	{0x342a, 0x10},
	{0x342b, 0x04},
	{0x3501, 0x02},
	{0x3504, 0x08},
	{0x3508, 0x01},
	{0x3509, 0x00},
	{0x350a, 0x01},
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
	{0x3610, 0x89},
	{0x3611, 0x8a},
	{0x3612, 0x4e},
	{0x3613, 0xbd},
	{0x3614, 0x9b},
	{0x362a, 0x0e},
	{0x362b, 0x0e},
	{0x362c, 0x0e},
	{0x362e, 0x1a},
	{0x362f, 0x34},
	{0x3630, 0x67},
	{0x3631, 0x7f},
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
	{0x365a, 0x00},
	{0x365b, 0x00},
	{0x365c, 0x00},
	{0x365d, 0x00},
	{0x3661, 0x07},
	{0x3663, 0x20},
	{0x3665, 0x12},
	{0x3668, 0x80},
	{0x366c, 0x00},
	{0x366d, 0x00},
	{0x366e, 0x00},
	{0x366f, 0x00},
	{0x3673, 0x2a},
	{0x3681, 0x80},
	{0x3700, 0x2d},
	{0x3701, 0x22},
	{0x3702, 0x25},
	{0x3705, 0x00},
	{0x3707, 0x0a},
	{0x3708, 0x36},
	{0x3709, 0x57},
	{0x3714, 0x01},
	{0x371c, 0x00},
	{0x371d, 0x08},
	{0x373f, 0x63},
	{0x3740, 0x63},
	{0x3741, 0x63},
	{0x3742, 0x63},
	{0x3762, 0x1c},
	{0x3776, 0x05},
	{0x3777, 0x22},
	{0x3779, 0x60},
	{0x377c, 0x48},
	{0x3784, 0x06},
	{0x3785, 0x0a},
	{0x3790, 0x10},
	{0x3793, 0x04},
	{0x3794, 0x07},
	{0x3796, 0x00},
	{0x3797, 0x02},
	{0x379c, 0x4d},
	{0x37a1, 0x80},
	{0x37bb, 0x88},
	{0x37be, 0x48},
	{0x37bf, 0x01},
	{0x37c0, 0x01},
	{0x37c4, 0x72},
	{0x37c5, 0x72},
	{0x37c6, 0x72},
	{0x37ca, 0x21},
	{0x37cd, 0x90},
	{0x37cf, 0x02},
	{0x37d0, 0x00},
	{0x37d8, 0x01},
	{0x37dc, 0x00},
	{0x37dd, 0x00},
	{0x37da, 0x00},
	{0x37db, 0x00},
	{0x3800, 0x00},
	{0x3802, 0x00},
	{0x3804, 0x0a},
	{0x3806, 0x05},
	{0x3808, 0x0a},
	{0x380a, 0x05},
	{0x3811, 0x08},
	{0x3813, 0x08},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3821, 0x00},
	{0x3822, 0x14},
	{0x3823, 0x18},
	{0x3826, 0x00},
	{0x3827, 0x00},
	{0x3858, 0x3c},
	{0x3865, 0x02},
	{0x3866, 0x00},
	{0x3867, 0x00},
	{0x3868, 0x02},
	{0x3900, 0x13},
	{0x3940, 0x13},
	{0x3980, 0x13},
	{0x3c01, 0x11},
	{0x3c05, 0x00},
	{0x3c0f, 0x1c},
	{0x3c12, 0x0d},
	{0x3c19, 0x00},
	{0x3c21, 0x00},
	{0x3c3a, 0x10},
	{0x3c3b, 0x18},
	{0x3c3d, 0xc6},
	{0x3c55, 0xcb},
	{0x3c5d, 0xcf},
	{0x3c5e, 0xcf},
	{0x3d8c, 0x70},
	{0x3d8d, 0x10},
	{0x4000, 0xf9},
	{0x4008, 0x02},
	{0x4009, 0x11},
	{0x400e, 0x40},
	{0x4030, 0x00},
	{0x4033, 0x80},
	{0x4050, 0x00},
	{0x4051, 0x07},
	{0x4011, 0xbb},
	{0x410f, 0x01},
	{0x4289, 0x00},
	{0x428a, 0x46},
	{0x430d, 0x00},
	{0x430e, 0x00},
	{0x4314, 0x04},
	{0x4500, 0x18},
	{0x4501, 0x18},
	{0x4503, 0x10},
	{0x4504, 0x00},
	{0x4506, 0x32},
	{0x4601, 0x30},
	{0x4603, 0x00},
	{0x460a, 0x50},
	{0x460c, 0x60},
	{0x4640, 0x62},
	{0x4646, 0xaa},
	{0x4647, 0x55},
	{0x4648, 0x99},
	{0x4649, 0x66},
	{0x464d, 0x00},
	{0x4654, 0x11},
	{0x4655, 0x22},
	{0x4800, 0x44},
	{0x4810, 0xff},
	{0x4811, 0xff},
	{0x481f, 0x30},
	{0x4d00, 0x4d},
	{0x4d01, 0x9d},
	{0x4d02, 0xb9},
	{0x4d03, 0x2e},
	{0x4d04, 0x4a},
	{0x4d05, 0x3d},
	{0x4d09, 0x4f},
	{0x5080, 0x00},
	{0x50c0, 0x00},
	{0x5100, 0x00},
	{0x5200, 0x00},
	{0x5201, 0x00},
	{0x5202, 0x03},
	{0x5203, 0xff},
	{0x5780, 0x53},
	{0x5786, 0x01},
	{0x5792, 0x11},
	{0x5793, 0x33},
	{0x5857, 0xff},
	{0x5858, 0xff},
	{0x5859, 0xff},
	{0x58d7, 0xff},
	{0x58d8, 0xff},
	{0x58d9, 0xff},
	{REG_NULL, 0x00},
};

static const struct regval os04a10_linear10bit_2688x1520_regs[] = {
	{0x0305, 0x3c},
	{0x0308, 0x04},
	{0x0317, 0x09},
	{0x0325, 0x90},
	{0x032e, 0x02},
	{0x3605, 0x7f},
	{0x3606, 0x80},
	{0x362d, 0x0e},
	{0x3662, 0x02},
	{0x3667, 0xd4},
	{0x3671, 0x08},
	{0x3703, 0x20},
	{0x3706, 0x72},
	{0x370a, 0x01},
	{0x370b, 0x14},
	{0x3719, 0x1f},
	{0x371b, 0x16},
	{0x3756, 0x9d},
	{0x3757, 0x9d},
	{0x376c, 0x04},
	{0x37cc, 0x13},
	{0x37d1, 0x72},
	{0x37d2, 0x01},
	{0x37d3, 0x14},
	{0x37d4, 0x00},
	{0x37d5, 0x6c},
	{0x37d6, 0x00},
	{0x37d7, 0xf7},
	{0x3801, 0x00},
	{0x3803, 0x00},
	{0x3805, 0x8f},
	{0x3807, 0xff},
	{0x3809, 0x80},
	{0x380b, 0xf0},
	{0x380c, 0x02},
	{0x380d, 0xdc},
	{0x380e, 0x0c},
	{0x380f, 0xb0},
	{0x381c, 0x00},
	{0x3820, 0x00},
	{0x3833, 0x40},
	{0x384c, 0x02},
	{0x384d, 0xdc},
	{0x3c5a, 0x55},
	{0x4004, 0x00},
	{0x4001, 0x2f},
	{0x4005, 0x40},
	{0x400a, 0x06},
	{0x400b, 0x40},
	{0x402e, 0x00},
	{0x402f, 0x40},
	{0x4031, 0x40},
	{0x4032, 0x0f},
	{0x4288, 0xcf},
	{0x430b, 0x0f},
	{0x430c, 0xfc},
	{0x4507, 0x02},
	{0x480e, 0x00},
	{0x4813, 0x00},
	{0x4837, 0x0e},
	{0x484b, 0x27},
	{0x5000, 0x1f},
	{0x5001, 0x0d},
	{0x5782, 0x18},
	{0x5783, 0x3c},
	{0x5788, 0x18},
	{0x5789, 0x3c},
	{REG_NULL, 0x00},
};

static const struct regval os04a10_linear12bit_2688x1520_regs[] = {
	{0x0305, 0x6c},
	{0x0308, 0x05},
	{0x0317, 0x0a},
	{0x0325, 0xd8},
	{0x032e, 0x02},
	{0x3605, 0xff},
	{0x3606, 0x01},
	{0x362d, 0x09},
	{0x3662, 0x00},
	{0x3667, 0xd4},
	{0x3671, 0x08},
	{0x3703, 0x28},
	{0x3706, 0xf0},
	{0x370a, 0x03},
	{0x370b, 0x15},
	{0x3719, 0x24},
	{0x371b, 0x1f},
	{0x3756, 0xe7},
	{0x3757, 0xe7},
	{0x376c, 0x00},
	{0x37cc, 0x15},
	{0x37d1, 0xf0},
	{0x37d2, 0x03},
	{0x37d3, 0x15},
	{0x37d4, 0x01},
	{0x37d5, 0x00},
	{0x37d6, 0x03},
	{0x37d7, 0x15},
	{0x3801, 0x00},
	{0x3803, 0x00},
	{0x3805, 0x8f},
	{0x3807, 0xff},
	{0x3809, 0x80},
	{0x380b, 0xf0},
	{0x380c, 0x05},
	{0x380d, 0xc4},
	{0x380e, 0x09},
	{0x380f, 0x84},
	{0x381c, 0x00},
	{0x3820, 0x00},
	{0x3833, 0x40},
	{0x384c, 0x05},
	{0x384d, 0xc4},
	{0x3c5a, 0xe5},
	{0x4001, 0x2f},
	{0x4004, 0x01},
	{0x4005, 0x00},
	{0x400a, 0x03},
	{0x400b, 0x27},
	{0x402e, 0x01},
	{0x402f, 0x00},
	{0x4031, 0x80},
	{0x4032, 0x9f},
	{0x4288, 0xcf},
	{0x430b, 0xff},
	{0x430c, 0xff},
	{0x4507, 0x02},
	{0x480e, 0x00},
	{0x4813, 0x00},
	{0x4837, 0x0c},
	{0x484b, 0x27},
	{0x5000, 0x1f},
	{0x5001, 0x0d},
	{0x5782, 0x60},
	{0x5783, 0xf0},
	{0x5788, 0x60},
	{0x5789, 0xf0},
	{REG_NULL, 0x00},
};

static const struct regval os04a10_hdr10bit_2688x1520_regs[] = {
	{0x0305, 0x3c},
	{0x0308, 0x04},
	{0x0317, 0x09},
	{0x0325, 0x90},
	{0x032e, 0x02},
	{0x3605, 0x7f},
	{0x3606, 0x80},
	{0x362d, 0x0e},
	{0x3662, 0x02},
	{0x3667, 0x54},
	{0x3671, 0x09},
	{0x3703, 0x20},
	{0x3706, 0x72},
	{0x370a, 0x01},
	{0x370b, 0x14},
	{0x3719, 0x1f},
	{0x371b, 0x16},
	{0x3756, 0x9d},
	{0x3757, 0x9d},
	{0x376c, 0x04},
	{0x37cc, 0x13},
	{0x37d1, 0x72},
	{0x37d2, 0x01},
	{0x37d3, 0x14},
	{0x37d4, 0x00},
	{0x37d5, 0x6c},
	{0x37d6, 0x00},
	{0x37d7, 0xf7},
	{0x3801, 0x00},
	{0x3803, 0x00},
	{0x3805, 0x8f},
	{0x3807, 0xff},
	{0x3809, 0x80},
	{0x380b, 0xf0},
	{0x380c, 0x02},
	{0x380d, 0xdc},
	{0x380e, 0x06},
	{0x380f, 0x58},
	//{0x380e, 0x0c},
	//{0x380f, 0xb0},
	{0x381c, 0x08},
	{0x3820, 0x01},
	{0x3833, 0x41},
	{0x384c, 0x02},
	{0x384d, 0xdc},
	{0x3c5a, 0x55},
	{0x4001, 0xef},
	{0x4004, 0x00},
	{0x4005, 0x40},
	{0x400a, 0x06},
	{0x400b, 0x40},
	{0x402e, 0x00},
	{0x402f, 0x40},
	{0x4031, 0x40},
	{0x4032, 0x0f},
	{0x4288, 0xce},
	{0x430b, 0x0f},
	{0x430c, 0xfc},
	{0x4507, 0x03},
	{0x480e, 0x04},
	{0x4813, 0x84},
	{0x4837, 0x0e},
	{0x484b, 0x67},
	{0x5000, 0x1f},
	{0x5001, 0x0c},
	{0x5782, 0x18},
	{0x5783, 0x3c},
	{0x5788, 0x18},
	{0x5789, 0x3c},
	{REG_NULL, 0x00},
};

static const struct regval os04a10_hdr12bit_2688x1520_regs[] = {
	{0x0305, 0x6c},
	{0x0308, 0x05},
	{0x0317, 0x0a},
	{0x0325, 0xd8},
	{0x032e, 0x05},
	{0x3605, 0xff},
	{0x3606, 0x01},
	{0x362d, 0x09},
	{0x3662, 0x00},
	{0x3667, 0x54},
	{0x3671, 0x09},
	{0x3703, 0x28},
	{0x3706, 0xf0},
	{0x370a, 0x03},
	{0x370b, 0x15},
	{0x3719, 0x24},
	{0x371b, 0x1f},
	{0x3756, 0xe7},
	{0x3757, 0xe7},
	{0x376c, 0x00},
	{0x37cc, 0x15},
	{0x37d1, 0xf0},
	{0x37d2, 0x03},
	{0x37d3, 0x15},
	{0x37d4, 0x01},
	{0x37d5, 0x00},
	{0x37d6, 0x03},
	{0x37d7, 0x15},
	{0x3801, 0x00},
	{0x3803, 0x00},
	{0x3805, 0x8f},
	{0x3807, 0xff},
	{0x3809, 0x80},
	{0x380b, 0xf0},
	{0x380c, 0x05},
	{0x380d, 0xc4},
	{0x380e, 0x06},
	{0x380f, 0x58},
	{0x381c, 0x08},
	{0x3820, 0x01},
	{0x3833, 0x41},
	{0x384c, 0x05},
	{0x384d, 0xc4},
	{0x3c5a, 0xe5},
	{0x4001, 0xef},
	{0x4004, 0x01},
	{0x4005, 0x00},
	{0x400a, 0x03},
	{0x400b, 0x27},
	{0x402e, 0x01},
	{0x402f, 0x00},
	{0x4031, 0x80},
	{0x4032, 0x9f},
	{0x4288, 0xce},
	{0x430b, 0xff},
	{0x430c, 0xff},
	{0x4507, 0x03},
	{0x480e, 0x04},
	{0x4813, 0x84},
	{0x4837, 0x0c},
	{0x484b, 0x67},
	{0x5000, 0x7f},
	{0x5001, 0x0c},
	{0x5782, 0x60},
	{0x5783, 0xf0},
	{0x5788, 0x60},
	{0x5789, 0xf0},
	{REG_NULL, 0x00},
};

static const struct regval os04a10_hdr12bit_2560x1440_regs[] = {
	{0x0305, 0x6c},
	{0x0308, 0x05},
	{0x0317, 0x0a},
	{0x0325, 0xd8},
	{0x032e, 0x05},
	{0x3605, 0xff},
	{0x3606, 0x01},
	{0x362d, 0x09},
	{0x3662, 0x00},
	{0x3667, 0x54},
	{0x3671, 0x09},
	{0x3703, 0x28},
	{0x3706, 0xf0},
	{0x370a, 0x03},
	{0x370b, 0x15},
	{0x3719, 0x24},
	{0x371b, 0x1f},
	{0x3756, 0xe7},
	{0x3757, 0xe7},
	{0x376c, 0x00},
	{0x37cc, 0x15},
	{0x37d1, 0xf0},
	{0x37d2, 0x03},
	{0x37d3, 0x15},
	{0x37d4, 0x01},
	{0x37d5, 0x00},
	{0x37d6, 0x03},
	{0x37d7, 0x15},
	{0x3801, 0x40},
	{0x3803, 0x28},
	{0x3805, 0x4f},
	{0x3807, 0xd7},
	{0x3809, 0x00},
	{0x380b, 0xa0},
	{0x380c, 0x05},
	{0x380d, 0xa0},
	{0x380e, 0x05},
	{0x380f, 0xdc},
	{0x381c, 0x08},
	{0x3820, 0x01},
	{0x3833, 0x41},
	{0x384c, 0x05},
	{0x384d, 0xa0},
	{0x3c5a, 0xe5},
	{0x4001, 0xef},
	{0x4004, 0x00},
	{0x4005, 0x80},
	{0x400a, 0x03},
	{0x400b, 0x27},
	{0x402e, 0x00},
	{0x402f, 0x80},
	{0x4031, 0x80},
	{0x4032, 0x9f},
	{0x4288, 0xce},
	{0x430b, 0xff},
	{0x430c, 0xff},
	{0x4507, 0x03},
	{0x480e, 0x04},
	{0x4813, 0x84},
	{0x4837, 0x0c},
	{0x484b, 0x67},
	{0x5000, 0x7f},
	{0x5001, 0x0c},
	{0x5782, 0x60},
	{0x5783, 0xf0},
	{0x5788, 0x60},
	{0x5789, 0xf0},
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
static const struct os04a10_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.width = 2688,
		.height = 1520,
		.max_fps = {
			.numerator = 10000,
			.denominator = 302834,
		},
		.exp_def = 0x0240,
		.hts_def = 0x02dc * 4,
		.vts_def = 0x0cb0,
		.reg_list = os04a10_linear10bit_2688x1520_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.width = 2688,
		.height = 1520,
		.max_fps = {
			.numerator = 10000,
			.denominator = 302834,
			/*.denominator = 151417,*/
		},
		.exp_def = 0x0240,
		.hts_def = 0x02dc * 4,
		.vts_def = 0x0658,
		/*.vts_def = 0x0cb0,*/
		.reg_list = os04a10_hdr10bit_2688x1520_regs,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR12_1X12,
		.width = 2688,
		.height = 1520,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300372,
		},
		.exp_def = 0x0240,
		.hts_def = 0x05c4 * 2,
		.vts_def = 0x0984,
		.reg_list = os04a10_linear12bit_2688x1520_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR12_1X12,
		.width = 2688,
		.height = 1520,
		.max_fps = {
			.numerator = 10000,
			.denominator = 225000,
		},
		.exp_def = 0x0240,
		.hts_def = 0x05c4 * 2,
		.vts_def = 0x0658,
		.reg_list = os04a10_hdr12bit_2688x1520_regs,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR12_1X12,
		.width = 2560,
		.height = 1440,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.exp_def = 0x0200,
		.hts_def = 0x05a0 * 2,
		.vts_def = 0x05dc,
		.reg_list = os04a10_hdr12bit_2560x1440_regs,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ_360M,
	MIPI_FREQ_648M,
};

static const char * const os04a10_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

static int __os04a10_power_on(struct os04a10 *os04a10);

/* Write registers up to 4 at a time */
static int os04a10_write_reg(struct i2c_client *client, u16 reg,
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

static int os04a10_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret |= os04a10_write_reg(client, regs[i].addr,
			OS04A10_REG_VALUE_08BIT, regs[i].val);
	}
	return ret;
}

/* Read registers up to 4 at a time */
static int os04a10_read_reg(struct i2c_client *client,
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

static int os04a10_get_reso_dist(const struct os04a10_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct os04a10_mode *
os04a10_find_best_fit(struct os04a10 *os04a10, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < os04a10->cfg_num; i++) {
		dist = os04a10_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
			(supported_modes[i].bus_fmt == framefmt->code)) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int os04a10_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct os04a10 *os04a10 = to_os04a10(sd);
	const struct os04a10_mode *mode;
	s64 h_blank, vblank_def;
	u64 dst_link_freq = 0;
	u64 dst_pixel_rate = 0;

	mutex_lock(&os04a10->mutex);

	mode = os04a10_find_best_fit(os04a10, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&os04a10->mutex);
		return -ENOTTY;
#endif
	} else {
		os04a10->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(os04a10->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(os04a10->vblank, vblank_def,
					 OS04A10_VTS_MAX - mode->height,
					 1, vblank_def);
		if (mode->hdr_mode == NO_HDR) {
			if (mode->bus_fmt == MEDIA_BUS_FMT_SBGGR10_1X10) {
				dst_link_freq = 0;
				dst_pixel_rate = PIXEL_RATE_WITH_360M;
			} else {
				dst_link_freq = 1;
				dst_pixel_rate = PIXEL_RATE_WITH_648M;
			}
		} else if (mode->hdr_mode == HDR_X2) {
			if (mode->width == 2560 && mode->height == 1440) {
				dst_link_freq = 1;
				dst_pixel_rate = PIXEL_RATE_WITH_648M;
			} else {
				if (mode->bus_fmt == MEDIA_BUS_FMT_SBGGR10_1X10) {
					dst_link_freq = 0;
					dst_pixel_rate = PIXEL_RATE_WITH_360M;
				} else {
					dst_link_freq = 1;
					dst_pixel_rate = PIXEL_RATE_WITH_648M;
				}
			}
		}
		__v4l2_ctrl_s_ctrl_int64(os04a10->pixel_rate,
					 dst_pixel_rate);
		__v4l2_ctrl_s_ctrl(os04a10->link_freq,
				   dst_link_freq);
	}

	mutex_unlock(&os04a10->mutex);

	return 0;
}

static int os04a10_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct os04a10 *os04a10 = to_os04a10(sd);
	const struct os04a10_mode *mode = os04a10->cur_mode;

	mutex_lock(&os04a10->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&os04a10->mutex);
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
	mutex_unlock(&os04a10->mutex);

	return 0;
}

static int os04a10_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct os04a10 *os04a10 = to_os04a10(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = os04a10->cur_mode->bus_fmt;

	return 0;
}

static int os04a10_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct os04a10 *os04a10 = to_os04a10(sd);

	if (fse->index >= os04a10->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int os04a10_enable_test_pattern(struct os04a10 *os04a10, u32 pattern)
{
	u32 val;
	int ret = 0;

	if (pattern)
		val = ((pattern - 1) << 2) | OS04A10_TEST_PATTERN_ENABLE;
	else
		val = OS04A10_TEST_PATTERN_DISABLE;
	ret = os04a10_write_reg(os04a10->client, OS04A10_REG_TEST_PATTERN,
				OS04A10_REG_VALUE_08BIT, val);
	ret |= os04a10_write_reg(os04a10->client, OS04A10_REG_TEST_PATTERN + 0x40,
				OS04A10_REG_VALUE_08BIT, val);
	return ret;
}

static int os04a10_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct os04a10 *os04a10 = to_os04a10(sd);
	const struct os04a10_mode *mode = os04a10->cur_mode;

	mutex_lock(&os04a10->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&os04a10->mutex);

	return 0;
}

static int os04a10_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct os04a10 *os04a10 = to_os04a10(sd);
	const struct os04a10_mode *mode = os04a10->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (OS04A10_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	if (mode->hdr_mode == HDR_X2)
		val = 1 << (OS04A10_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		V4L2_MBUS_CSI2_CHANNEL_1;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void os04a10_get_module_inf(struct os04a10 *os04a10,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, OS04A10_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, os04a10->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, os04a10->len_name, sizeof(inf->base.lens));
}

static int os04a10_set_hdrae(struct os04a10 *os04a10,
			     struct preisp_hdrae_exp_s *ae)
{
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_a_gain, m_a_gain, s_a_gain;
	u32 l_d_gain = 1024;
	u32 m_d_gain = 1024;
	u32 s_d_gain = 1024;
	int ret = 0;
	u8 l_cg_mode = 0;
	u8 m_cg_mode = 0;
	u8 s_cg_mode = 0;
	u32 gain_switch = 0;
	u8 is_need_switch = 0;

	if (!os04a10->has_init_exp && !os04a10->streaming) {
		os04a10->init_hdrae_exp = *ae;
		os04a10->has_init_exp = true;
		if (os04a10->init_hdrae_exp.short_exp_reg >= 0x90) {
			dev_err(&os04a10->client->dev, "short exposure must less than 0x90 before start stream!\n");
			return -EINVAL;
		}
		dev_dbg(&os04a10->client->dev, "os04a10 don't stream, record exp for hdr!\n");
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
	dev_dbg(&os04a10->client->dev,
		"rev exp req: L_exp: 0x%x, 0x%x, M_exp: 0x%x, 0x%x S_exp: 0x%x, 0x%x\n",
		l_exp_time, l_a_gain,
		m_exp_time, m_a_gain,
		s_exp_time, s_a_gain);

	if (os04a10->cur_mode->hdr_mode == HDR_X2) {
		//2 stagger
		l_a_gain = m_a_gain;
		l_exp_time = m_exp_time;
		l_cg_mode = m_cg_mode;
		m_a_gain = s_a_gain;
		m_exp_time = s_exp_time;
		m_cg_mode = s_cg_mode;
	}
	ret = os04a10_read_reg(os04a10->client, OS04A10_REG_HCG_SWITCH,
			       OS04A10_REG_VALUE_08BIT, &gain_switch);

	if (os04a10->long_hcg && l_cg_mode == GAIN_MODE_LCG) {
		gain_switch |= 0x10;
		os04a10->long_hcg = false;
		is_need_switch++;
	} else if (!os04a10->long_hcg && l_cg_mode == GAIN_MODE_HCG) {
		gain_switch &= 0xef;
		os04a10->long_hcg = true;
		is_need_switch++;
	}
	if (os04a10->middle_hcg && m_cg_mode == GAIN_MODE_LCG) {
		gain_switch |= 0x20;
		os04a10->middle_hcg = false;
		is_need_switch++;
	} else if (!os04a10->middle_hcg && m_cg_mode == GAIN_MODE_HCG) {
		gain_switch &= 0xdf;
		os04a10->middle_hcg = true;
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
	if (os04a10->cur_mode->hdr_mode == HDR_X3 && s_a_gain > 248) {
		s_d_gain = s_a_gain * 1024 / 248;
		s_a_gain = 248;
	}

	ret |= os04a10_write_reg(os04a10->client,
		OS04A10_GROUP_UPDATE_ADDRESS,
		OS04A10_REG_VALUE_08BIT,
		OS04A10_GROUP_UPDATE_START_DATA);
	ret |= os04a10_write_reg(os04a10->client,
		OS04A10_REG_AGAIN_LONG_H,
		OS04A10_REG_VALUE_16BIT,
		(l_a_gain << 4) & 0x1ff0);
	ret |= os04a10_write_reg(os04a10->client,
		OS04A10_REG_DGAIN_LONG_H,
		OS04A10_REG_VALUE_24BIT,
		(l_d_gain << 6) & 0xfffc0);
	ret |= os04a10_write_reg(os04a10->client,
		OS04A10_REG_EXP_LONG_H,
		OS04A10_REG_VALUE_16BIT,
		l_exp_time);
	ret |= os04a10_write_reg(os04a10->client,
		OS04A10_REG_AGAIN_MID_H,
		OS04A10_REG_VALUE_16BIT,
		(m_a_gain << 4) & 0x1ff0);
	ret |= os04a10_write_reg(os04a10->client,
		OS04A10_REG_DGAIN_MID_H,
		OS04A10_REG_VALUE_24BIT,
		(m_d_gain << 6) & 0xfffc0);
	ret |= os04a10_write_reg(os04a10->client,
		OS04A10_REG_EXP_MID_H,
		OS04A10_REG_VALUE_16BIT,
		m_exp_time);
	if (os04a10->cur_mode->hdr_mode == HDR_X3) {
		//3 stagger
		ret |= os04a10_write_reg(os04a10->client,
			OS04A10_REG_AGAIN_VS_H,
			OS04A10_REG_VALUE_16BIT,
			(s_a_gain << 4) & 0x1ff0);
		ret |= os04a10_write_reg(os04a10->client,
			OS04A10_REG_EXP_VS_H,
			OS04A10_REG_VALUE_16BIT,
			s_exp_time);
		ret |= os04a10_write_reg(os04a10->client,
			OS04A10_REG_DGAIN_VS_H,
			OS04A10_REG_VALUE_24BIT,
			(s_d_gain << 6) & 0xfffc0);
		if (os04a10->short_hcg && s_cg_mode == GAIN_MODE_LCG) {
			gain_switch |= 0x40;
			os04a10->short_hcg = false;
			is_need_switch++;
		} else if (!os04a10->short_hcg && s_cg_mode == GAIN_MODE_HCG) {
			gain_switch &= 0xbf;
			os04a10->short_hcg = true;
			is_need_switch++;
		}
	}
	if (is_need_switch)
		ret |= os04a10_write_reg(os04a10->client,
			OS04A10_REG_HCG_SWITCH,
			OS04A10_REG_VALUE_08BIT,
			gain_switch);
	ret |= os04a10_write_reg(os04a10->client,
		OS04A10_GROUP_UPDATE_ADDRESS,
		OS04A10_REG_VALUE_08BIT,
		OS04A10_GROUP_UPDATE_END_DATA);
	ret |= os04a10_write_reg(os04a10->client,
		OS04A10_GROUP_UPDATE_ADDRESS,
		OS04A10_REG_VALUE_08BIT,
		OS04A10_GROUP_UPDATE_END_LAUNCH);
	return ret;
}

static int os04a10_set_conversion_gain(struct os04a10 *os04a10, u32 *cg)
{
	int ret = 0;
	struct i2c_client *client = os04a10->client;
	u32 cur_cg = *cg;
	u32 val = 0;
	s32 is_need_change = 0;

	dev_dbg(&os04a10->client->dev, "set conversion gain %d\n", cur_cg);
	if (os04a10->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
		os04a10->is_thunderboot = false;
		os04a10->is_thunderboot_ng = true;
		__os04a10_power_on(os04a10);
	}

	ret = os04a10_read_reg(client,
		OS04A10_REG_HCG_SWITCH,
		OS04A10_REG_VALUE_08BIT,
		&val);
	if (os04a10->long_hcg && cur_cg == GAIN_MODE_LCG) {
		val |= 0x10;
		is_need_change++;
		os04a10->long_hcg = false;
	} else if (!os04a10->long_hcg && cur_cg == GAIN_MODE_HCG) {
		val &= 0xef;
		is_need_change++;
		os04a10->long_hcg = true;
	}
	ret |= os04a10_write_reg(client,
		OS04A10_GROUP_UPDATE_ADDRESS,
		OS04A10_REG_VALUE_08BIT,
		OS04A10_GROUP_UPDATE_START_DATA);
	if (is_need_change)
		ret |= os04a10_write_reg(client,
			OS04A10_REG_HCG_SWITCH,
			OS04A10_REG_VALUE_08BIT,
			val);
	ret |= os04a10_write_reg(client,
		OS04A10_GROUP_UPDATE_ADDRESS,
		OS04A10_REG_VALUE_08BIT,
		OS04A10_GROUP_UPDATE_END_DATA);
	ret |= os04a10_write_reg(client,
		OS04A10_GROUP_UPDATE_ADDRESS,
		OS04A10_REG_VALUE_08BIT,
		OS04A10_GROUP_UPDATE_END_LAUNCH);
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
	struct os04a10 *os04a10 = to_os04a10(sd);
	int status = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &status);
	if (!ret && status >= 0 && status < 2)
		os04a10_set_conversion_gain(os04a10, &status);
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

static long os04a10_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct os04a10 *os04a10 = to_os04a10(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	struct rkmodule_dcg_ratio *dcg;
	long ret = 0;
	u32 i, h, w;
	u32 stream = 0;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		return os04a10_set_hdrae(os04a10, arg);
	case RKMODULE_SET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		w = os04a10->cur_mode->width;
		h = os04a10->cur_mode->height;
		for (i = 0; i < os04a10->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			h == supported_modes[i].height &&
			supported_modes[i].hdr_mode == hdr_cfg->hdr_mode) {
				os04a10->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == os04a10->cfg_num) {
			dev_err(&os04a10->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr_cfg->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = os04a10->cur_mode->hts_def - os04a10->cur_mode->width;
			h = os04a10->cur_mode->vts_def - os04a10->cur_mode->height;
			__v4l2_ctrl_modify_range(os04a10->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(os04a10->vblank, h,
				OS04A10_VTS_MAX - os04a10->cur_mode->height,
				1, h);
			dev_info(&os04a10->client->dev,
				"sensor mode: %d\n",
				os04a10->cur_mode->hdr_mode);
		}
		break;
	case RKMODULE_GET_MODULE_INFO:
		os04a10_get_module_inf(os04a10, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = os04a10->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		ret = os04a10_set_conversion_gain(os04a10, (u32 *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = os04a10_write_reg(os04a10->client, OS04A10_REG_CTRL_MODE,
				OS04A10_REG_VALUE_08BIT, OS04A10_MODE_STREAMING);
		else
			ret = os04a10_write_reg(os04a10->client, OS04A10_REG_CTRL_MODE,
				OS04A10_REG_VALUE_08BIT, OS04A10_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_DCG_RATIO:
		if (os04a10->dcg_ratio == 0)
			return -EINVAL;
		dcg = (struct rkmodule_dcg_ratio *)arg;
		dcg->integer = (os04a10->dcg_ratio >> 8) & 0xff;
		dcg->decimal = os04a10->dcg_ratio & 0xff;
		dcg->div_coeff = 256;
		dev_info(&os04a10->client->dev,
			 "get dcg ratio integer %d, decimal %d div_coeff %d\n",
			 dcg->integer, dcg->decimal, dcg->div_coeff);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long os04a10_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	struct rkmodule_dcg_ratio *dcg;
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

		ret = os04a10_ioctl(sd, cmd, inf);
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
			ret = os04a10_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = os04a10_ioctl(sd, cmd, hdr);
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
			ret = os04a10_ioctl(sd, cmd, hdr);
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
			ret = os04a10_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		ret = copy_from_user(&cg, up, sizeof(cg));
		if (!ret)
			ret = os04a10_ioctl(sd, cmd, &cg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = os04a10_ioctl(sd, cmd, &stream);
		break;
	case RKMODULE_GET_DCG_RATIO:
		dcg = kzalloc(sizeof(*dcg), GFP_KERNEL);
		if (!dcg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = os04a10_ioctl(sd, cmd, dcg);
		if (!ret)
			ret = copy_to_user(up, dcg, sizeof(*dcg));
		kfree(dcg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int os04a10_init_conversion_gain(struct os04a10 *os04a10)
{
	int ret = 0;
	struct i2c_client *client = os04a10->client;
	u32 val = 0;

	ret = os04a10_read_reg(client,
		OS04A10_REG_HCG_SWITCH,
		OS04A10_REG_VALUE_08BIT,
		&val);
	val |= 0x70;
	ret |= os04a10_write_reg(client,
		OS04A10_REG_HCG_SWITCH,
		OS04A10_REG_VALUE_08BIT,
		val);
	os04a10->long_hcg = false;
	os04a10->middle_hcg = false;
	os04a10->short_hcg = false;
	return ret;
}

static int __os04a10_start_stream(struct os04a10 *os04a10)
{
	int ret;

	if (!os04a10->is_thunderboot) {
		ret = os04a10_write_array(os04a10->client, os04a10_global_regs);
		if (ret) {
			dev_err(&os04a10->client->dev,
				"could not set init registers\n");
			return ret;
		}

		ret = os04a10_write_array(os04a10->client, os04a10->cur_mode->reg_list);
		if (ret)
			return ret;
	}

	ret = os04a10_init_conversion_gain(os04a10);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&os04a10->ctrl_handler);
	if (ret)
		return ret;
	if (os04a10->has_init_exp && os04a10->cur_mode->hdr_mode != NO_HDR) {
		ret = os04a10_ioctl(&os04a10->subdev, PREISP_CMD_SET_HDRAE_EXP, &os04a10->init_hdrae_exp);
		if (ret) {
			dev_err(&os04a10->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}
	return os04a10_write_reg(os04a10->client, OS04A10_REG_CTRL_MODE,
		OS04A10_REG_VALUE_08BIT, OS04A10_MODE_STREAMING);
}

static int __os04a10_stop_stream(struct os04a10 *os04a10)
{
	os04a10->has_init_exp = false;
	if (os04a10->is_thunderboot)
		os04a10->is_first_streamoff = true;
	return os04a10_write_reg(os04a10->client, OS04A10_REG_CTRL_MODE,
		OS04A10_REG_VALUE_08BIT, OS04A10_MODE_SW_STANDBY);
}

static int os04a10_s_stream(struct v4l2_subdev *sd, int on)
{
	struct os04a10 *os04a10 = to_os04a10(sd);
	struct i2c_client *client = os04a10->client;
	int ret = 0;

	mutex_lock(&os04a10->mutex);
	on = !!on;
	if (on == os04a10->streaming)
		goto unlock_and_return;

	if (on) {
		if (os04a10->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
			os04a10->is_thunderboot = false;
			__os04a10_power_on(os04a10);
		}
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __os04a10_start_stream(os04a10);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__os04a10_stop_stream(os04a10);
		pm_runtime_put(&client->dev);
	}

	os04a10->streaming = on;

unlock_and_return:
	mutex_unlock(&os04a10->mutex);

	return ret;
}

static int os04a10_s_power(struct v4l2_subdev *sd, int on)
{
	struct os04a10 *os04a10 = to_os04a10(sd);
	struct i2c_client *client = os04a10->client;
	int ret = 0;

	mutex_lock(&os04a10->mutex);

	/* If the power state is not modified - no work to do. */
	if (os04a10->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		if (!os04a10->is_thunderboot) {
			ret |= os04a10_write_reg(os04a10->client,
						 OS04A10_SOFTWARE_RESET_REG,
						 OS04A10_REG_VALUE_08BIT,
						 0x01);
			usleep_range(100, 200);
		}

		os04a10->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		os04a10->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&os04a10->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 os04a10_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OS04A10_XVCLK_FREQ / 1000 / 1000);
}

static int __os04a10_power_on(struct os04a10 *os04a10)
{
	int ret;
	u32 delay_us;
	struct device *dev = &os04a10->client->dev;

	if (os04a10->is_thunderboot)
		return 0;

	if (!IS_ERR_OR_NULL(os04a10->pins_default)) {
		ret = pinctrl_select_state(os04a10->pinctrl,
					   os04a10->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(os04a10->xvclk, OS04A10_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(os04a10->xvclk) != OS04A10_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(os04a10->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(os04a10->reset_gpio))
		gpiod_direction_output(os04a10->reset_gpio, 1);

	ret = regulator_bulk_enable(OS04A10_NUM_SUPPLIES, os04a10->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(os04a10->reset_gpio))
		gpiod_direction_output(os04a10->reset_gpio, 0);

	usleep_range(500, 1000);
	if (!IS_ERR(os04a10->pwdn_gpio))
		gpiod_direction_output(os04a10->pwdn_gpio, 1);
	/*
	 * There is no need to wait for the delay of RC circuit
	 * if the reset signal is directly controlled by GPIO.
	 */
	if (!IS_ERR(os04a10->reset_gpio))
		usleep_range(6000, 8000);
	else
		usleep_range(12000, 16000);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = os04a10_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(os04a10->xvclk);

	return ret;
}

static void __os04a10_power_off(struct os04a10 *os04a10)
{
	int ret;
	struct device *dev = &os04a10->client->dev;

	if (os04a10->is_thunderboot) {
		if (os04a10->is_first_streamoff) {
			os04a10->is_thunderboot = false;
			os04a10->is_first_streamoff = false;
		} else {
			return;
		}
	}

	if (!IS_ERR(os04a10->pwdn_gpio))
		gpiod_direction_output(os04a10->pwdn_gpio, 0);

	clk_disable_unprepare(os04a10->xvclk);

	if (!IS_ERR(os04a10->reset_gpio))
		gpiod_direction_output(os04a10->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(os04a10->pins_sleep)) {
		ret = pinctrl_select_state(os04a10->pinctrl,
					   os04a10->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}

	if (os04a10->is_thunderboot_ng) {
		os04a10->is_thunderboot_ng = false;
		regulator_bulk_disable(OS04A10_NUM_SUPPLIES, os04a10->supplies);
	}
}

static int os04a10_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os04a10 *os04a10 = to_os04a10(sd);

	return __os04a10_power_on(os04a10);
}

static int os04a10_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os04a10 *os04a10 = to_os04a10(sd);

	__os04a10_power_off(os04a10);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int os04a10_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct os04a10 *os04a10 = to_os04a10(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct os04a10_mode *def_mode = &supported_modes[0];

	mutex_lock(&os04a10->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&os04a10->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int os04a10_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct os04a10 *os04a10 = to_os04a10(sd);

	if (fie->index >= os04a10->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops os04a10_pm_ops = {
	SET_RUNTIME_PM_OPS(os04a10_runtime_suspend,
			   os04a10_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops os04a10_internal_ops = {
	.open = os04a10_open,
};
#endif

static const struct v4l2_subdev_core_ops os04a10_core_ops = {
	.s_power = os04a10_s_power,
	.ioctl = os04a10_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = os04a10_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops os04a10_video_ops = {
	.s_stream = os04a10_s_stream,
	.g_frame_interval = os04a10_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops os04a10_pad_ops = {
	.enum_mbus_code = os04a10_enum_mbus_code,
	.enum_frame_size = os04a10_enum_frame_sizes,
	.enum_frame_interval = os04a10_enum_frame_interval,
	.get_fmt = os04a10_get_fmt,
	.set_fmt = os04a10_set_fmt,
	.get_mbus_config = os04a10_g_mbus_config,
};

static const struct v4l2_subdev_ops os04a10_subdev_ops = {
	.core	= &os04a10_core_ops,
	.video	= &os04a10_video_ops,
	.pad	= &os04a10_pad_ops,
};

static int os04a10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct os04a10 *os04a10 = container_of(ctrl->handler,
					     struct os04a10, ctrl_handler);
	struct i2c_client *client = os04a10->client;
	s64 max;
	int ret = 0;
	u32 again, dgain;
	u32 val = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = os04a10->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(os04a10->exposure,
					 os04a10->exposure->minimum, max,
					 os04a10->exposure->step,
					 os04a10->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = os04a10_write_reg(os04a10->client,
					OS04A10_REG_EXP_LONG_H,
					OS04A10_REG_VALUE_16BIT,
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
		ret = os04a10_write_reg(os04a10->client,
					OS04A10_REG_AGAIN_LONG_H,
					OS04A10_REG_VALUE_16BIT,
					(again << 4) & 0x1ff0);
		ret |= os04a10_write_reg(os04a10->client,
					OS04A10_REG_DGAIN_LONG_H,
					OS04A10_REG_VALUE_24BIT,
					(dgain << 6) & 0xfffc0);
		dev_dbg(&client->dev, "set analog gain 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = os04a10_write_reg(os04a10->client, OS04A10_REG_VTS,
					OS04A10_REG_VALUE_16BIT,
					ctrl->val + os04a10->cur_mode->height);
		dev_dbg(&client->dev, "set vblank 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = os04a10_enable_test_pattern(os04a10, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = os04a10_read_reg(os04a10->client, OS04A10_FLIP_REG,
				       OS04A10_REG_VALUE_08BIT,
				       &val);
		if (ctrl->val)
			val |= MIRROR_BIT_MASK;
		else
			val &= ~MIRROR_BIT_MASK;
		ret = os04a10_write_reg(os04a10->client, OS04A10_FLIP_REG,
					OS04A10_REG_VALUE_08BIT,
					val);
		if (ret == 0)
			os04a10->flip = val;
		break;
	case V4L2_CID_VFLIP:
		ret = os04a10_read_reg(os04a10->client, OS04A10_FLIP_REG,
				       OS04A10_REG_VALUE_08BIT,
				       &val);
		if (ctrl->val)
			val |= FLIP_BIT_MASK;
		else
			val &= ~FLIP_BIT_MASK;
		ret = os04a10_write_reg(os04a10->client, OS04A10_FLIP_REG,
					OS04A10_REG_VALUE_08BIT,
					val);
		if (ret == 0)
			os04a10->flip = val;
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops os04a10_ctrl_ops = {
	.s_ctrl = os04a10_set_ctrl,
};

static int os04a10_initialize_controls(struct os04a10 *os04a10)
{
	const struct os04a10_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	u64 dst_link_freq = 0;
	u64 dst_pixel_rate = 0;

	handler = &os04a10->ctrl_handler;
	mode = os04a10->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &os04a10->mutex;

	os04a10->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
			V4L2_CID_LINK_FREQ,
			1, 0, link_freq_menu_items);

	if (os04a10->cur_mode->bus_fmt == MEDIA_BUS_FMT_SBGGR10_1X10) {
		dst_link_freq = 0;
		dst_pixel_rate = PIXEL_RATE_WITH_360M;
	} else {
		dst_link_freq = 1;
		dst_pixel_rate = PIXEL_RATE_WITH_648M;
	}
	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	os04a10->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
			V4L2_CID_PIXEL_RATE,
			0, PIXEL_RATE_WITH_648M,
			1, dst_pixel_rate);

	__v4l2_ctrl_s_ctrl(os04a10->link_freq,
			   dst_link_freq);

	h_blank = mode->hts_def - mode->width;
	os04a10->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (os04a10->hblank)
		os04a10->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	os04a10->vblank = v4l2_ctrl_new_std(handler, &os04a10_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OS04A10_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	os04a10->exposure = v4l2_ctrl_new_std(handler, &os04a10_ctrl_ops,
				V4L2_CID_EXPOSURE, OS04A10_EXPOSURE_MIN,
				exposure_max, OS04A10_EXPOSURE_STEP,
				mode->exp_def);

	os04a10->anal_gain = v4l2_ctrl_new_std(handler, &os04a10_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, OS04A10_GAIN_MIN,
				OS04A10_GAIN_MAX, OS04A10_GAIN_STEP,
				OS04A10_GAIN_DEFAULT);

	os04a10->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&os04a10_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(os04a10_test_pattern_menu) - 1,
				0, 0, os04a10_test_pattern_menu);

	os04a10->h_flip = v4l2_ctrl_new_std(handler, &os04a10_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	os04a10->v_flip = v4l2_ctrl_new_std(handler, &os04a10_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	os04a10->flip = 0;
	if (handler->error) {
		ret = handler->error;
		dev_err(&os04a10->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	os04a10->subdev.ctrl_handler = handler;
	os04a10->has_init_exp = false;
	os04a10->long_hcg = false;
	os04a10->middle_hcg = false;
	os04a10->short_hcg = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int os04a10_check_sensor_id(struct os04a10 *os04a10,
				  struct i2c_client *client)
{
	struct device *dev = &os04a10->client->dev;
	u32 id = 0;
	int ret;

	if (os04a10->is_thunderboot) {
		dev_info(dev, "Enable thunderboot mode, skip sensor id check\n");
		return 0;
	}

	ret = os04a10_read_reg(client, OS04A10_REG_CHIP_ID,
			       OS04A10_REG_VALUE_24BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int os04a10_configure_regulators(struct os04a10 *os04a10)
{
	unsigned int i;

	for (i = 0; i < OS04A10_NUM_SUPPLIES; i++)
		os04a10->supplies[i].supply = os04a10_supply_names[i];

	return devm_regulator_bulk_get(&os04a10->client->dev,
				       OS04A10_NUM_SUPPLIES,
				       os04a10->supplies);
}

static int os04a10_get_dcg_ratio(struct os04a10 *os04a10)
{
	struct device *dev = &os04a10->client->dev;
	u32 val = 0;
	int ret = 0;

	if (os04a10->is_thunderboot) {
		ret = os04a10_read_reg(os04a10->client, 0x77fe,
					OS04A10_REG_VALUE_16BIT, &val);
	} else {
		ret = os04a10_write_reg(os04a10->client, OS04A10_REG_CTRL_MODE,
					OS04A10_REG_VALUE_08BIT, OS04A10_MODE_STREAMING);
		usleep_range(5000, 6000);
		ret |= os04a10_read_reg(os04a10->client, 0x77fe,
					OS04A10_REG_VALUE_16BIT, &val);
		ret |= os04a10_write_reg(os04a10->client, OS04A10_REG_CTRL_MODE,
					OS04A10_REG_VALUE_08BIT, OS04A10_MODE_SW_STANDBY);
	}

	if (ret != 0 || val == 0) {
		os04a10->dcg_ratio = 0;
		dev_err(dev, "get dcg ratio fail, ret %d, dcg ratio %d\n", ret, val);
	} else {
		os04a10->dcg_ratio = val;
		dev_info(dev, "get dcg ratio reg val 0x%04x\n", val);
	}

	return ret;
}

static int os04a10_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct os04a10 *os04a10;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	os04a10 = devm_kzalloc(dev, sizeof(*os04a10), GFP_KERNEL);
	if (!os04a10)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &os04a10->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &os04a10->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &os04a10->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &os04a10->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	os04a10->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);
	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE,
			&hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}
	os04a10->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < os04a10->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			os04a10->cur_mode = &supported_modes[i];
			break;
		}
	}
	os04a10->client = client;

	os04a10->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(os04a10->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	os04a10->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(os04a10->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	os04a10->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_ASIS);
	if (IS_ERR(os04a10->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	os04a10->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(os04a10->pinctrl)) {
		os04a10->pins_default =
			pinctrl_lookup_state(os04a10->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(os04a10->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		os04a10->pins_sleep =
			pinctrl_lookup_state(os04a10->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(os04a10->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = os04a10_configure_regulators(os04a10);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&os04a10->mutex);

	sd = &os04a10->subdev;
	v4l2_i2c_subdev_init(sd, client, &os04a10_subdev_ops);
	ret = os04a10_initialize_controls(os04a10);
	if (ret)
		goto err_destroy_mutex;

	ret = __os04a10_power_on(os04a10);
	if (ret)
		goto err_free_handler;

	ret = os04a10_check_sensor_id(os04a10, client);
	if (ret)
		goto err_power_off;
	ret = os04a10_get_dcg_ratio(os04a10);

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &os04a10_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	os04a10->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &os04a10->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(os04a10->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 os04a10->module_index, facing,
		 OS04A10_NAME, dev_name(sd->dev));
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
	__os04a10_power_off(os04a10);
err_free_handler:
	v4l2_ctrl_handler_free(&os04a10->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&os04a10->mutex);

	return ret;
}

static int os04a10_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os04a10 *os04a10 = to_os04a10(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&os04a10->ctrl_handler);
	mutex_destroy(&os04a10->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__os04a10_power_off(os04a10);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id os04a10_of_match[] = {
	{ .compatible = "ovti,os04a10" },
	{},
};
MODULE_DEVICE_TABLE(of, os04a10_of_match);
#endif

static const struct i2c_device_id os04a10_match_id[] = {
	{ "ovti,os04a10", 0 },
	{ },
};

static struct i2c_driver os04a10_i2c_driver = {
	.driver = {
		.name = OS04A10_NAME,
		.pm = &os04a10_pm_ops,
		.of_match_table = of_match_ptr(os04a10_of_match),
	},
	.probe		= &os04a10_probe,
	.remove		= &os04a10_remove,
	.id_table	= os04a10_match_id,
};

#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT
module_i2c_driver(os04a10_i2c_driver);
#else
static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&os04a10_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&os04a10_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);
#endif

MODULE_DESCRIPTION("OmniVision os04a10 sensor driver");
MODULE_LICENSE("GPL v2");
