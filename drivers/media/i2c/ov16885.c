// SPDX-License-Identifier: GPL-2.0
/*
 * ov16885 camera driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
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
#include <linux/compat.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)
#define OV16885_MAJOR_I2C_ADDR		0x36
#define OV16885_MINOR_I2C_ADDR		0x10

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define OV16885_LINK_FREQ_736MHZ	736000000U

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define OV16885_PIXEL_RATE		(OV16885_LINK_FREQ_736MHZ * 2LL * 4LL / 10LL)
#define OV16885_XVCLK_FREQ		24000000

#define CHIP_ID				0x016885
#define OV16885_REG_CHIP_ID		0x300a

#define OV16885_REG_CTRL_MODE		0x0100
#define OV16885_MODE_SW_STANDBY		0x0
#define OV16885_MODE_STREAMING		BIT(0)

#define OV16885_REG_EXPOSURE_H		0x3500
#define OV16885_REG_EXPOSURE_M		0x3501
#define OV16885_REG_EXPOSURE_L		0x3502
#define	OV16885_EXPOSURE_MIN		8
#define	OV16885_EXPOSURE_STEP		1
#define OV16885_VTS_MAX			0x7fff

#define OV16885_LF_AGAIN_REG_H		0x3508
#define OV16885_LF_AGAIN_REG_L		0x3509
#define OV16885_SF_AGAIN_REG_H		0x350C
#define OV16885_SF_AGAIN_REG_L		0x350D
#define OV16885_GAIN_H_MASK		0x1f
#define OV16885_GAIN_H_SHIFT		8


#define OV16885_GAIN_MIN		0x80
#define OV16885_GAIN_MAX		0x07FF
#define OV16885_GAIN_STEP		1
#define OV16885_GAIN_DEFAULT		0x80

#define OV16885_SOFTWARE_RESET_REG	0x0103
#define OV16885_REG_ISP_X_WIN		0x3810

#define OV16885_GROUP_UPDATE_ADDRESS	0x3208
#define OV16885_GROUP_UPDATE_START_DATA	0x00
#define OV16885_GROUP_UPDATE_END_DATA	0x10
#define OV16885_GROUP_UPDATE_LAUNCH	0xA0

#define OV16885_REG_TEST_PATTERN	0x5081
#define	OV16885_TEST_PATTERN_ENABLE	0x01
#define	OV16885_TEST_PATTERN_DISABLE	0x0

#define OV16885_REG_VTS_H		0x380e
#define OV16885_REG_VTS_L		0x380f

#define OV16885_FLIP_REG		0x3820
#define OV16885_MIRROR_REG		0x3674
#define MIRROR_BIT_MASK			BIT(2)
#define FLIP_BIT_MASK			BIT(2)

#define OV16885_FETCH_EXP_H(VAL)	(((VAL) >> 16) & 0x7F)
#define OV16885_FETCH_EXP_M(VAL)	(((VAL) >> 8) & 0xFF)
#define OV16885_FETCH_EXP_L(VAL)	((VAL) & 0xFF)

#define OV16885_FETCH_AGAIN_H(VAL)	(((VAL) >> 8) & 0x7F)
#define OV16885_FETCH_AGAIN_L(VAL)	((VAL) & 0xFE)

#define OV16885_FETCH_DGAIN_H(VAL)	(((VAL) >> 16) & 0x0F)
#define OV16885_FETCH_DGAIN_M(VAL)	(((VAL) >> 8) & 0xFF)
#define OV16885_FETCH_DGAIN_L(VAL)	((VAL) & 0xC0)

#define OV16885_FETCH_VTS_H(VAL)	(((VAL) >> 8) & 0x7F)
#define OV16885_FETCH_VTS_L(VAL)	((VAL) & 0xFF)

#define REG_NULL			0xFFFF

#define OV16885_REG_VALUE_08BIT		1
#define OV16885_REG_VALUE_16BIT		2
#define OV16885_REG_VALUE_24BIT		3

#define OV16885_LANES			4
#define OV16885_BITS_PER_SAMPLE		10

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define OV16885_NAME			"ov16885"
#define OV16885_MEDIA_BUS_FMT		MEDIA_BUS_FMT_SBGGR10_1X10

static const char * const ov16885_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV16885_NUM_SUPPLIES ARRAY_SIZE(ov16885_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct ov16885_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 link_freq_idx;
	u32 bpp;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct ov16885 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV16885_NUM_SUPPLIES];

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
	struct v4l2_ctrl	*h_flip;
	struct v4l2_ctrl	*v_flip;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct ov16885_mode *cur_mode;
	u32			cfg_num;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_ov16885(sd) container_of(sd, struct ov16885, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval ov16885_global_regs[] = {
	// init_setting
	{0x0103,0x01},
	{0x0102,0x01},
	{0x0300,0xf3},
	{0x0301,0xa5},
	{0x0302,0x10},
	{0x0304,0x4b},
	{0x0314,0x02},
	{0x0316,0xa0},
	{0x0319,0x00},
	{0x031a,0x01},
	{0x031e,0x09},
	{0x0320,0x0f},
	{0x300d,0x11},
	{0x3011,0xfc},
	{0x3012,0x41},
	{0x3016,0xf0},
	{0x3018,0xf0},
	{0x301b,0x3c},
	{0x3025,0x03},
	{0x3026,0x10},
	{0x3027,0x08},
	{0x301e,0x98},
	{0x3031,0x88},
	{0x3400,0x00},
	{0x3406,0x08},
	{0x3408,0x03},
	{0x3410,0x00},
	{0x3412,0x00},
	{0x3413,0x00},
	{0x3414,0x00},
	{0x3415,0x00},
	{0x3500,0x00},
	{0x3501,0xec},
	{0x3502,0x00},
	{0x3503,0x08},
	{0x3505,0x8c},
	{0x3507,0x00},
	{0x3508,0x02},
	{0x3509,0x00},
	{0x350c,0x02},
	{0x350d,0x00},
	{0x3510,0x00},
	{0x3511,0xec},
	{0x3512,0x00},
	{0x3600,0x00},
	{0x3601,0x00},
	{0x3602,0x86},
	{0x3608,0xc7},
	{0x3609,0xd0},
	{0x360a,0xff},
	{0x360b,0x6c},
	{0x360c,0x00},
	{0x3611,0x00},
	{0x3612,0x00},
	{0x3613,0x8e},
	{0x3618,0x00},
	{0x3619,0x90},
	{0x361a,0x00},
	{0x361b,0x01},
	{0x361c,0xc5},
	{0x3620,0x50},
	{0x3621,0x88},
	{0x3622,0x88},
	{0x3623,0x88},
	{0x3624,0x88},
	{0x3625,0x88},
	{0x3626,0x03},
	{0x3627,0x88},
	{0x3628,0x1c},
	{0x3629,0x00},
	{0x362a,0x00},
	{0x3632,0x00},
	{0x3633,0x10},
	{0x3634,0x10},
	{0x3635,0x10},
	{0x3636,0x10},
	{0x3637,0x77},
	{0x3638,0x77},
	{0x3639,0x66},
	{0x363a,0x66},
	{0x3652,0x00},
	{0x3653,0x00},
	{0x3654,0x77},
	{0x3655,0x77},
	{0x3656,0x77},
	{0x3657,0x77},
	{0x3658,0x00},
	{0x3659,0x84},
	{0x365a,0x81},
	{0x365b,0x8e},
	{0x365c,0x1c},
	{0x3660,0x40},
	{0x3661,0x0c},
	{0x3662,0x00},
	{0x3663,0x40},
	{0x3664,0x03},
	{0x3666,0xac},
	{0x3668,0xf0},
	{0x3669,0x0e},
	{0x366a,0x10},
	{0x366b,0x42},
	{0x366c,0x53},
	{0x366d,0x05},
	{0x366e,0x05},
	{0x3674,0x04},
	{0x3675,0x16},
	{0x3680,0x00},
	{0x3681,0x33},
	{0x3682,0x33},
	{0x3683,0x33},
	{0x368a,0x04},
	{0x368b,0x04},
	{0x368c,0x04},
	{0x368d,0x04},
	{0x368e,0x04},
	{0x368f,0x04},
	{0x3694,0x10},
	{0x3696,0x30},
	{0x3698,0x30},
	{0x3699,0x00},
	{0x369a,0x44},
	{0x369c,0x28},
	{0x369e,0x28},
	{0x36a0,0x28},
	{0x36a2,0x30},
	{0x36a4,0x3b},
	{0x36a5,0x00},
	{0x36a6,0x43},
	{0x36a7,0x00},
	{0x36a8,0x48},
	{0x36a9,0x00},
	{0x36aa,0x48},
	{0x36ab,0x00},
	{0x36ac,0x48},
	{0x36c1,0x33},
	{0x36c3,0x33},
	{0x36ca,0x04},
	{0x36cb,0x04},
	{0x36cc,0x04},
	{0x36cd,0x04},
	{0x36ce,0x04},
	{0x36cf,0x04},
	{0x3700,0x13},
	{0x3701,0x0e},
	{0x3702,0x12},
	{0x3704,0x0e},
	{0x3706,0x23},
	{0x3708,0x17},
	{0x3709,0x30},
	{0x370b,0x63},
	{0x3713,0x00},
	{0x3714,0x64},
	{0x371d,0x10},
	{0x371f,0x05},
	{0x3726,0x20},
	{0x3727,0x23},
	{0x373b,0x06},
	{0x373d,0x07},
	{0x374f,0x0d},
	{0x3754,0x88},
	{0x375a,0x08},
	{0x3764,0x12},
	{0x3765,0x0b},
	{0x3767,0x0c},
	{0x3768,0x18},
	{0x3769,0x08},
	{0x376a,0x0c},
	{0x376b,0x80},
	{0x37a2,0x04},
	{0x37b1,0x40},
	{0x37d0,0x06},
	{0x37d9,0x88},
	{0x37f4,0x00},
	{0x37fc,0x05},
	{0x37fd,0x00},
	{0x37fe,0x0b},
	{0x37ff,0x00},
	{0x3800,0x00},
	{0x3801,0x00},
	{0x3802,0x00},
	{0x3803,0x00},
	{0x3804,0x12},
	{0x3805,0x5f},
	{0x3806,0x0d},
	{0x3807,0xcf},
	{0x3808,0x12},
	{0x3809,0x40},
	{0x380a,0x0d},
	{0x380b,0xb0},
	{0x380c,0x05},
	{0x380d,0x78},
	{0x380e,0x0e},
	{0x380f,0xe0},
	{0x3810,0x00},
	{0x3811,0x11},
	{0x3812,0x00},
	{0x3813,0x08},
	{0x3814,0x11},
	{0x3815,0x11},
	{0x3820,0x00},
	{0x3821,0x00},
	{0x382b,0x08},
	{0x3834,0xf0},
	{0x3836,0x28},
	{0x383d,0x80},
	{0x3841,0x20},
	{0x3883,0x02},
	{0x3886,0x02},
	{0x3889,0x02},
	{0x3891,0x0f},
	{0x38a0,0x04},
	{0x38a1,0x00},
	{0x38a2,0x04},
	{0x38a3,0x04},
	{0x38b0,0x02},
	{0x38b1,0x02},
	{0x3b8e,0x00},
	{0x3d84,0x80},
	{0x3d85,0x1b},
	{0x3d8c,0x67},
	{0x3d8d,0xc0},
	{0x3f00,0xca},
	{0x3f03,0x10},
	{0x3f05,0x66},
	{0x4008,0x00},
	{0x4009,0x02},
	{0x400e,0x00},
	{0x4010,0x28},
	{0x4011,0x01},
	{0x4012,0x6d},
	{0x4013,0x28},
	{0x4014,0x10},
	{0x4015,0x02},
	{0x4016,0x25},
	{0x4017,0x00},
	{0x4018,0x0f},
	{0x4019,0x00},
	{0x401a,0x40},
	{0x4020,0x04},
	{0x4021,0x00},
	{0x4022,0x04},
	{0x4023,0x00},
	{0x4024,0x04},
	{0x4025,0x00},
	{0x4026,0x04},
	{0x4027,0x00},
	{0x4056,0x05},
	{0x4202,0x00},
	{0x4500,0x00},
	{0x4501,0x05},
	{0x4502,0x80},
	{0x4503,0x31},
	{0x450c,0x05},
	{0x450e,0x16},
	{0x450f,0x80},
	{0x4540,0x99},
	{0x4541,0x1b},
	{0x4542,0x18},
	{0x4543,0x1a},
	{0x4544,0x1d},
	{0x4545,0x1f},
	{0x4546,0x1c},
	{0x4547,0x1e},
	{0x4548,0x09},
	{0x4549,0x0b},
	{0x454a,0x08},
	{0x454b,0x0a},
	{0x454c,0x0d},
	{0x454d,0x0f},
	{0x454e,0x0c},
	{0x454f,0x0e},
	{0x4550,0x09},
	{0x4551,0x0b},
	{0x4552,0x08},
	{0x4553,0x0a},
	{0x4554,0x0d},
	{0x4555,0x0f},
	{0x4556,0x0c},
	{0x4557,0x0e},
	{0x4558,0x19},
	{0x4559,0x1b},
	{0x455a,0x18},
	{0x455b,0x1a},
	{0x455c,0x1d},
	{0x455d,0x1f},
	{0x455e,0x1c},
	{0x455f,0x1e},
	{0x4640,0x01},
	{0x4641,0x04},
	{0x4642,0x02},
	{0x4643,0x00},
	{0x4645,0x03},
	{0x4800,0x00},
	{0x4809,0x2b},
	{0x480e,0x02},
	{0x4813,0x90},
	{0x4817,0x00},
	{0x481f,0x36},
	{0x4837,0x0b},
	{0x484b,0x01},
	{0x4850,0x7c},
	{0x4852,0x03},
	{0x4853,0x12},
	{0x4856,0x58},
	{0x4857,0x02},
	{0x4d00,0x04},
	{0x4d01,0x5a},
	{0x4d02,0xb3},
	{0x4d03,0xf1},
	{0x4d04,0xaa},
	{0x4d05,0xc9},
	{0x5080,0x00},
	{0x5084,0x00},
	{0x5085,0x00},
	{0x5086,0x00},
	{0x5087,0x00},
	{0x5000,0x8b},
	{0x5001,0x52},
	{0x5002,0x01},
	{0x5004,0x00},
	{0x5020,0x00},
	{0x5021,0x10},
	{0x5022,0x12},
	{0x5023,0x50},
	{0x5024,0x00},
	{0x5025,0x08},
	{0x5026,0x0d},
	{0x5027,0xb8},
	{0x5028,0x00},
	{0x5081,0x00},
	{0x5180,0x03},
	{0x5181,0xb0},
	{0x5184,0x03},
	{0x5185,0x07},
	{0x518c,0x01},
	{0x518d,0x01},
	{0x518e,0x01},
	{0x518f,0x01},
	{0x5190,0x00},
	{0x5191,0x00},
	{0x5192,0x12},
	{0x5193,0x5f},
	{0x5194,0x00},
	{0x5195,0x00},
	{0x5200,0xbf},
	{0x5201,0xf3},
	{0x5202,0x09},
	{0x5203,0x1b},
	{0x5204,0xe0},
	{0x5205,0x10},
	{0x5206,0x3f},
	{0x5207,0x3c},
	{0x5208,0x24},
	{0x5209,0x0f},
	{0x520a,0x43},
	{0x520b,0x3b},
	{0x520c,0x33},
	{0x520d,0x33},
	{0x520e,0x63},
	{0x5210,0x06},
	{0x5211,0x03},
	{0x5212,0x08},
	{0x5213,0x08},
	{0x5217,0x04},
	{0x5218,0x02},
	{0x5219,0x01},
	{0x521a,0x04},
	{0x521b,0x02},
	{0x521c,0x01},
	{0x5297,0x04},
	{0x5298,0x02},
	{0x5299,0x01},
	{0x529a,0x04},
	{0x529b,0x02},
	{0x529c,0x01},
	{0x5404,0x00},
	{0x5405,0x00},
	{0x5406,0x01},
	{0x5407,0xe1},
	{0x5408,0x01},
	{0x5409,0x41},
	{0x5410,0x02},
	{0x5413,0xa0},
	{0x5820,0x18},
	{0x5821,0x08},
	{0x5822,0x08},
	{0x5823,0x18},
	{0x5824,0x18},
	{0x5825,0x08},
	{0x5826,0x08},
	{0x5827,0x18},
	{0x582c,0x08},
	{0x582d,0x18},
	{0x582e,0x00},
	{0x582f,0x00},
	{0x5830,0x08},
	{0x5831,0x18},
	{0x5836,0x08},
	{0x5837,0x18},
	{0x5838,0x00},
	{0x5839,0x00},
	{0x583a,0x08},
	{0x583b,0x18},
	{0x583c,0x55},
	{0x583e,0x03},
	{0x5860,0x02},
	{0x58a1,0x04},
	{0x58a2,0x00},
	{0x58a3,0x00},
	{0x58a4,0x02},
	{0x58a5,0x00},
	{0x58a6,0x02},
	{0x58a7,0x00},
	{0x58a8,0x00},
	{0x58a9,0x00},
	{0x58aa,0x00},
	{0x58ab,0x00},
	{0x58ac,0x14},
	{0x58ad,0x60},
	{0x58ae,0x0f},
	{0x58af,0x50},
	{0x58c4,0x12},
	{0x58c5,0x60},
	{0x58c6,0x0d},
	{0x58c7,0xd0},
	{0x5900,0x3e},
	{0x5901,0x3e},
	{0x5902,0x3e},
	{0x5903,0x3e},
	{0x5904,0x3e},
	{0x5905,0x3e},
	{0x5906,0x3e},
	{0x5907,0x3e},
	{0x5908,0x3e},
	{0x5909,0x3e},
	{0x590a,0x3e},
	{0x590b,0x3e},
	{0x590c,0x3e},
	{0x590d,0x3e},
	{0x590e,0x3e},
	{0x590f,0x3e},
	{0x5910,0x3e},
	{0x5911,0x3e},
	{0x5912,0x3e},
	{0x5913,0x3e},
	{0x5914,0x3e},
	{0x5915,0x3e},
	{0x5916,0x3e},
	{0x5917,0x3e},
	{0x5918,0x3e},
	{0x5919,0x3e},
	{0x591a,0x3e},
	{0x591b,0x3e},
	{0x591c,0x3e},
	{0x591d,0x3e},
	{0x591e,0x3e},
	{0x591f,0x3e},
	{0x5920,0x3e},
	{0x5921,0x3e},
	{0x5922,0x3e},
	{0x5923,0x3e},
	{0x5924,0x3e},
	{0x5925,0x3e},
	{0x5926,0x3e},
	{0x5927,0x3e},
	{0x5928,0x3e},
	{0x5929,0x3e},
	{0x592a,0x3e},
	{0x592b,0x3e},
	{0x592c,0x3e},
	{0x592d,0x40},
	{0x592e,0x40},
	{0x592f,0x40},
	{0x5930,0x40},
	{0x5931,0x40},
	{0x5932,0x40},
	{0x5933,0x40},
	{0x5934,0x40},
	{0x5935,0x40},
	{0x5936,0x40},
	{0x5937,0x40},
	{0x5938,0x40},
	{0x5939,0x40},
	{0x593a,0x40},
	{0x593b,0x40},
	{0x593c,0x40},
	{0x593d,0x40},
	{0x593e,0x40},
	{0x593f,0x40},
	{0x5940,0x40},
	{0x5941,0x40},
	{0x5942,0x40},
	{0x5943,0x40},
	{0x5944,0x40},
	{0x5945,0x40},
	{0x5946,0x40},
	{0x5947,0x40},
	{0x5948,0x40},
	{0x5949,0x40},
	{0x594a,0x40},
	{0x594b,0x40},
	{0x594c,0x40},
	{0x594d,0x40},
	{0x594e,0x40},
	{0x594f,0x40},
	{0x5950,0x40},
	{0x5951,0x40},
	{0x5952,0x40},
	{0x5953,0x40},
	{0x5954,0x40},
	{0x5955,0x40},
	{0x5956,0x40},
	{0x5957,0x40},
	{0x5958,0x40},
	{0x5959,0x40},
	{0x595a,0x40},
	{0x595b,0x40},
	{0x595c,0x40},
	{0x595d,0x40},
	{0x595e,0x40},
	{0x595f,0x40},
	{0x5960,0x40},
	{0x5961,0x40},
	{0x5962,0x40},
	{0x5963,0x40},
	{0x5964,0x40},
	{0x5965,0x40},
	{0x5966,0x40},
	{0x5967,0x40},
	{0x5968,0x40},
	{0x5969,0x40},
	{0x596a,0x40},
	{0x596b,0x40},
	{0x596c,0x40},
	{0x596d,0x40},
	{0x596e,0x40},
	{0x596f,0x40},
	{0x5970,0x40},
	{0x5971,0x40},
	{0x5972,0x40},
	{0x5973,0x40},
	{0x5974,0x40},
	{0x5975,0x40},
	{0x5976,0x40},
	{0x5977,0x40},
	{0x5978,0x40},
	{0x5979,0x40},
	{0x597a,0x40},
	{0x597b,0x40},
	{0x597c,0x40},
	{0x597d,0x40},
	{0x597e,0x40},
	{0x597f,0x40},
	{0x5980,0x40},
	{0x5981,0x40},
	{0x5982,0x40},
	{0x5983,0x40},
	{0x5984,0x40},
	{0x5985,0x40},
	{0x5986,0x40},
	{0x5987,0x40},
	{0x5988,0x40},
	{0x5989,0x40},
	{0x598a,0x40},
	{0x598b,0x40},
	{0x598c,0x40},
	{0x598d,0x40},
	{0x598e,0x40},
	{0x598f,0x40},
	{0x5990,0x40},
	{0x5991,0x40},
	{0x5992,0x40},
	{0x5993,0x40},
	{0x5994,0x40},
	{0x5995,0x40},
	{0x5996,0x40},
	{0x5997,0x40},
	{0x5998,0x40},
	{0x5999,0x40},
	{0x599a,0x40},
	{0x599b,0x40},
	{0x599c,0x40},
	{0x599d,0x40},
	{0x599e,0x40},
	{0x599f,0x40},
	{0x59a0,0x40},
	{0x59a1,0x40},
	{0x59a2,0x40},
	{0x59a3,0x40},
	{0x59a4,0x40},
	{0x59a5,0x40},
	{0x59a6,0x40},
	{0x59a7,0x40},
	{0x59a8,0x40},
	{0x59a9,0x40},
	{0x59aa,0x40},
	{0x59ab,0x40},
	{0x59ac,0x40},
	{0x59ad,0x40},
	{0x59ae,0x40},
	{0x59af,0x40},
	{0x59b0,0x40},
	{0x59b1,0x40},
	{0x59b2,0x40},
	{0x59b3,0x40},
	{0x59b4,0x01},
	{0x59b5,0x02},
	{0x59b8,0x00},
	{0x59b9,0x7c},
	{0x59ba,0x00},
	{0x59bb,0xa8},
	{0x59bc,0x12},
	{0x59bd,0x60},
	{0x59be,0x0d},
	{0x59bf,0xd0},
	{0x59c4,0x00},
	{0x59c5,0x10},
	{0x59c6,0x12},
	{0x59c7,0x50},
	{0x59c8,0x00},
	{0x59c9,0x08},
	{0x59ca,0x0d},
	{0x59cb,0xb8},
	{0x59dc,0x20},
	{0x59de,0x20},
	{0x59ec,0x20},
	{0x59ee,0x20},
	{0x59fd,0x20},
	{0x59ff,0x20},
	{0x5a0d,0x20},
	{0x5a0f,0x20},
	{0x5a1c,0x20},
	{0x5a1e,0x20},
	{0x5a2c,0x20},
	{0x5a2e,0x20},
	{0x5a3d,0x20},
	{0x5a3f,0x20},
	{0x5a4d,0x20},
	{0x5a4f,0x20},
	{0x5a64,0x08},
	{0x5a68,0x08},
	{0x5a84,0x0c},
	{0x5a88,0x0c},
	{0x5aa6,0x08},
	{0x5aaa,0x08},
	{0x5ac6,0x0c},
	{0x5aca,0x0c},
	{0x5ae4,0x08},
	{0x5ae8,0x08},
	{0x5b04,0x0c},
	{0x5b08,0x0c},
	{0x5b26,0x08},
	{0x5b2a,0x08},
	{0x5b46,0x0c},
	{0x5b4a,0x0c},
	{0x5b56,0x00},
	{0x5b57,0x0b},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * resolution = 4672*3504
 * bayer pattern = BGGR"
*/
static const struct regval ov16885_4672x3504_30fps_regs[] = {
	{0x0304,0x4b},
	{0x3501,0xec},
	{0x3511,0xec},
	{0x3600,0x00},
	{0x3602,0x86},
	{0x3621,0x88},
	{0x366c,0x53},
	{0x3701,0x0e},
	{0x3726,0x20},
	{0x3709,0x30},
	{0x3800,0x00},
	{0x3801,0x00},
	{0x3802,0x00},
	{0x3803,0x00},
	{0x3804,0x12},
	{0x3805,0x5f},
	{0x3806,0x0d},
	{0x3807,0xcf},
	{0x3808,0x12},
	{0x3809,0x40},
	{0x380a,0x0d},
	{0x380b,0xb0},
	{0x380c,0x05},
	{0x380d,0x78},
	{0x380e,0x0e},
	{0x380f,0xe0},
	{0x3811,0x11},
	{0x3813,0x08},
	{0x3815,0x11},
	{0x3820,0x00},
	{0x3834,0xf0},
	{0x3f03,0x10},
	{0x3f05,0x66},
	{0x4013,0x28},
	{0x4014,0x10},
	{0x4016,0x25},
	{0x4018,0x0f},
	{0x4500,0x00},
	{0x4501,0x05},
	{0x4503,0x31},
	{0x4837,0x0b},
	{0x5000,0x8b},
	{0x5001,0x52},
	{0x583e,0x03},
	{REG_NULL, 0x00},
};

static const struct ov16885_mode supported_modes[] = {
	{
		.width = 4672,
		.height = 3504,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0e40,
		.hts_def = 0x0578 * 4,
		.vts_def = 0x0ee0,
		.bpp = 10,
		.reg_list = ov16885_4672x3504_30fps_regs,
		.link_freq_idx = 0,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_items[] = {
	OV16885_LINK_FREQ_736MHZ,
};

static const char * const ov16885_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int ov16885_write_reg(struct i2c_client *client, u16 reg,
			     u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	dev_dbg(&client->dev, "write reg(0x%x val:0x%x)!\n", reg, val);

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

static int ov16885_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = ov16885_write_reg(client, regs[i].addr,
					OV16885_REG_VALUE_08BIT,
					regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int ov16885_read_reg(struct i2c_client *client, u16 reg,
			    unsigned int len, u32 *val)
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

static int ov16885_get_reso_dist(const struct ov16885_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov16885_mode *
ov16885_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = ov16885_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov16885_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov16885 *ov16885 = to_ov16885(sd);
	const struct ov16885_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;
	u32 lane_num = OV16885_LANES;

	mutex_lock(&ov16885->mutex);

	mode = ov16885_find_best_fit(fmt);
	fmt->format.code = OV16885_MEDIA_BUS_FMT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov16885->mutex);
		return -ENOTTY;
#endif
	} else {
		ov16885->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov16885->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov16885->vblank, vblank_def,
					 OV16885_VTS_MAX - mode->height,
					 1, vblank_def);
		__v4l2_ctrl_s_ctrl(ov16885->vblank, vblank_def);
		pixel_rate = (u32)link_freq_items[mode->link_freq_idx] / mode->bpp * 2 * lane_num;

		__v4l2_ctrl_s_ctrl_int64(ov16885->pixel_rate,
					 pixel_rate);
		__v4l2_ctrl_s_ctrl(ov16885->link_freq,
				   mode->link_freq_idx);
	}
	dev_info(&ov16885->client->dev, "%s: mode->link_freq_idx(%d)",
		 __func__, mode->link_freq_idx);

	mutex_unlock(&ov16885->mutex);

	return 0;
}

static int ov16885_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct ov16885 *ov16885 = to_ov16885(sd);
	const struct ov16885_mode *mode = ov16885->cur_mode;

	mutex_lock(&ov16885->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov16885->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = OV16885_MEDIA_BUS_FMT;
		fmt->format.field = V4L2_FIELD_NONE;
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&ov16885->mutex);

	return 0;
}

static int ov16885_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = OV16885_MEDIA_BUS_FMT;

	return 0;
}

static int ov16885_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct ov16885 *ov16885 = to_ov16885(sd);

	if (fse->index >= ov16885->cfg_num)
		return -EINVAL;

	if (fse->code != OV16885_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int ov16885_enable_test_pattern(struct ov16885 *ov16885, u32 pattern)
{
	u32 val;

	if (pattern)
		val = ((pattern - 1) << 4) | OV16885_TEST_PATTERN_ENABLE;
	else
		val = OV16885_TEST_PATTERN_DISABLE;

	return ov16885_write_reg(ov16885->client,
				 OV16885_REG_TEST_PATTERN,
				 OV16885_REG_VALUE_08BIT,
				 val);
}

static int ov16885_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct ov16885 *ov16885 = to_ov16885(sd);
	const struct ov16885_mode *mode = ov16885->cur_mode;

	fi->interval = mode->max_fps;

	return 0;
}

static void ov16885_get_module_inf(struct ov16885 *ov16885,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, OV16885_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, ov16885->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, ov16885->len_name, sizeof(inf->base.lens));
}

static long ov16885_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ov16885 *ov16885 = to_ov16885(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	long ret = 0;
	u32 i, h, w;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_SET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		w = ov16885->cur_mode->width;
		h = ov16885->cur_mode->height;
		for (i = 0; i < ov16885->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			h == supported_modes[i].height &&
			supported_modes[i].hdr_mode == hdr_cfg->hdr_mode) {
				ov16885->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == ov16885->cfg_num) {
			dev_err(&ov16885->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr_cfg->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = ov16885->cur_mode->hts_def - ov16885->cur_mode->width;
			h = ov16885->cur_mode->vts_def - ov16885->cur_mode->height;
			mutex_lock(&ov16885->mutex);
			__v4l2_ctrl_modify_range(ov16885->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(ov16885->vblank, h,
						 OV16885_VTS_MAX - ov16885->cur_mode->height,
						 1, h);
			mutex_unlock(&ov16885->mutex);
			dev_info(&ov16885->client->dev,
				"sensor mode: %d\n",
				ov16885->cur_mode->hdr_mode);
		}
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = ov16885->cur_mode->hdr_mode;
		break;
	case RKMODULE_GET_MODULE_INFO:
		ov16885_get_module_inf(ov16885, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = ov16885_write_reg(ov16885->client,
				 OV16885_REG_CTRL_MODE,
				 OV16885_REG_VALUE_08BIT,
				 OV16885_MODE_STREAMING);
		else
			ret = ov16885_write_reg(ov16885->client,
				 OV16885_REG_CTRL_MODE,
				 OV16885_REG_VALUE_08BIT,
				 OV16885_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ov16885_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = ov16885_ioctl(sd, cmd, inf);
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

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = ov16885_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = ov16885_ioctl(sd, cmd, hdr);
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
		ret = ov16885_ioctl(sd, cmd, hdr);
		kfree(hdr);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = ov16885_ioctl(sd, cmd, &stream);
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

static int __ov16885_start_stream(struct ov16885 *ov16885)
{
	int ret;

	ret = ov16885_write_array(ov16885->client, ov16885->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&ov16885->mutex);
	ret = v4l2_ctrl_handler_setup(&ov16885->ctrl_handler);
	mutex_lock(&ov16885->mutex);
	if (ret)
		return ret;

	return ov16885_write_reg(ov16885->client,
				 OV16885_REG_CTRL_MODE,
				 OV16885_REG_VALUE_08BIT,
				 OV16885_MODE_STREAMING);
}

static int __ov16885_stop_stream(struct ov16885 *ov16885)
{
	return ov16885_write_reg(ov16885->client,
				 OV16885_REG_CTRL_MODE,
				 OV16885_REG_VALUE_08BIT,
				 OV16885_MODE_SW_STANDBY);
}

static int ov16885_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov16885 *ov16885 = to_ov16885(sd);
	struct i2c_client *client = ov16885->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				ov16885->cur_mode->width,
				ov16885->cur_mode->height,
		DIV_ROUND_CLOSEST(ov16885->cur_mode->max_fps.denominator,
				  ov16885->cur_mode->max_fps.numerator));

	mutex_lock(&ov16885->mutex);
	on = !!on;
	if (on == ov16885->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __ov16885_start_stream(ov16885);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__ov16885_stop_stream(ov16885);
		pm_runtime_put(&client->dev);
	}

	ov16885->streaming = on;

unlock_and_return:
	mutex_unlock(&ov16885->mutex);

	return ret;
}

static int ov16885_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov16885 *ov16885 = to_ov16885(sd);
	struct i2c_client *client = ov16885->client;
	int ret = 0;

	mutex_lock(&ov16885->mutex);

	/* If the power state is not modified - no work to do. */
	if (ov16885->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = ov16885_write_array(ov16885->client, ov16885_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ov16885->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		ov16885->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&ov16885->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov16885_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OV16885_XVCLK_FREQ / 1000 / 1000);
}

static int __ov16885_power_on(struct ov16885 *ov16885)
{
	int ret;
	u32 delay_us;
	struct device *dev = &ov16885->client->dev;

	if (!IS_ERR(ov16885->power_gpio))
		gpiod_set_value_cansleep(ov16885->power_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR_OR_NULL(ov16885->pins_default)) {
		ret = pinctrl_select_state(ov16885->pinctrl,
					   ov16885->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(ov16885->xvclk, OV16885_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(ov16885->xvclk) != OV16885_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(ov16885->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(ov16885->reset_gpio))
		gpiod_set_value_cansleep(ov16885->reset_gpio, 0);

	ret = regulator_bulk_enable(OV16885_NUM_SUPPLIES, ov16885->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(ov16885->reset_gpio))
		gpiod_set_value_cansleep(ov16885->reset_gpio, 1);

	usleep_range(5000, 6000);
	if (!IS_ERR(ov16885->pwdn_gpio))
		gpiod_set_value_cansleep(ov16885->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ov16885_cal_delay(8192);
	usleep_range(delay_us * 2, delay_us * 3);

	return 0;

disable_clk:
	clk_disable_unprepare(ov16885->xvclk);

	return ret;
}

static void __ov16885_power_off(struct ov16885 *ov16885)
{
	int ret;
	struct device *dev = &ov16885->client->dev;

	if (!IS_ERR(ov16885->pwdn_gpio))
		gpiod_set_value_cansleep(ov16885->pwdn_gpio, 0);
	clk_disable_unprepare(ov16885->xvclk);
	if (!IS_ERR(ov16885->reset_gpio))
		gpiod_set_value_cansleep(ov16885->reset_gpio, 0);

	if (!IS_ERR_OR_NULL(ov16885->pins_sleep)) {
		ret = pinctrl_select_state(ov16885->pinctrl,
					   ov16885->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	if (!IS_ERR(ov16885->power_gpio))
		gpiod_set_value_cansleep(ov16885->power_gpio, 0);

	regulator_bulk_disable(OV16885_NUM_SUPPLIES, ov16885->supplies);
}

static int ov16885_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov16885 *ov16885 = to_ov16885(sd);

	return __ov16885_power_on(ov16885);
}

static int ov16885_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov16885 *ov16885 = to_ov16885(sd);

	__ov16885_power_off(ov16885);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov16885_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov16885 *ov16885 = to_ov16885(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov16885_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov16885->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = OV16885_MEDIA_BUS_FMT;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov16885->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int ov16885_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fie->code = OV16885_MEDIA_BUS_FMT;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;

	return 0;
}

static int ov16885_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_config *config)
{
	if (2 == OV16885_LANES) {
		config->type = V4L2_MBUS_CSI2_DPHY;
		config->flags = V4L2_MBUS_CSI2_2_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	} else if (4 == OV16885_LANES) {
		config->type = V4L2_MBUS_CSI2_DPHY;
		config->flags = V4L2_MBUS_CSI2_4_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	}

	return 0;
}

#define CROP_START(SRC, DST) (((SRC) - (DST)) / 2 / 4 * 4)
#define DST_WIDTH_2320 2320
#define DST_HEIGHT_1744 1744
/*
 * The resolution of the driver configuration needs to be exactly
 * the same as the current output resolution of the sensor,
 * the input width of the isp needs to be 16 aligned,
 * the input height of the isp needs to be 8 aligned.
 * Can be cropped to standard resolution by this function,
 * otherwise it will crop out strange resolution according
 * to the alignment rules.
 */
static int ov16885_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct ov16885 *ov16885 = to_ov16885(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		if (ov16885->cur_mode->width == 2328) {
			sel->r.left = CROP_START(ov16885->cur_mode->width, DST_WIDTH_2320);
			sel->r.width = DST_WIDTH_2320;
			sel->r.top = CROP_START(ov16885->cur_mode->height, DST_HEIGHT_1744);
			sel->r.height = DST_HEIGHT_1744;
		} else {
			sel->r.left = 0;
			sel->r.width = ov16885->cur_mode->width;
			sel->r.top = 0;
			sel->r.height = ov16885->cur_mode->height;
		}
		return 0;
	}

	return -EINVAL;
}

static const struct dev_pm_ops ov16885_pm_ops = {
	SET_RUNTIME_PM_OPS(ov16885_runtime_suspend,
			   ov16885_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov16885_internal_ops = {
	.open = ov16885_open,
};
#endif

static const struct v4l2_subdev_core_ops ov16885_core_ops = {
	.s_power = ov16885_s_power,
	.ioctl = ov16885_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = ov16885_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops ov16885_video_ops = {
	.s_stream = ov16885_s_stream,
	.g_frame_interval = ov16885_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops ov16885_pad_ops = {
	.enum_mbus_code = ov16885_enum_mbus_code,
	.enum_frame_size = ov16885_enum_frame_sizes,
	.enum_frame_interval = ov16885_enum_frame_interval,
	.get_fmt = ov16885_get_fmt,
	.set_fmt = ov16885_set_fmt,
	.get_selection = ov16885_get_selection,
	.get_mbus_config = ov16885_g_mbus_config,
};

static const struct v4l2_subdev_ops ov16885_subdev_ops = {
	.core	= &ov16885_core_ops,
	.video	= &ov16885_video_ops,
	.pad	= &ov16885_pad_ops,
};

static int ov16885_set_gain_reg(struct ov16885 *ov16885, u32 a_gain)
{
	int ret = 0;

	ret = ov16885_write_reg(ov16885->client,
				 OV16885_GROUP_UPDATE_ADDRESS,
				 OV16885_REG_VALUE_08BIT,
				 OV16885_GROUP_UPDATE_START_DATA);

	ret |= ov16885_write_reg(ov16885->client,
				 OV16885_SF_AGAIN_REG_H,
				 OV16885_REG_VALUE_16BIT,
				 a_gain & 0x1fff);
	ret |= ov16885_write_reg(ov16885->client,
				 OV16885_LF_AGAIN_REG_H,
				 OV16885_REG_VALUE_16BIT,
				 a_gain & 0x1fff);
	ret |= ov16885_write_reg(ov16885->client,
				 OV16885_GROUP_UPDATE_ADDRESS,
				 OV16885_REG_VALUE_08BIT,
				 OV16885_GROUP_UPDATE_END_DATA);
	ret |= ov16885_write_reg(ov16885->client,
				 OV16885_GROUP_UPDATE_ADDRESS,
				 OV16885_REG_VALUE_08BIT,
				 OV16885_GROUP_UPDATE_LAUNCH);
	return ret;

};

static int ov16885_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov16885 *ov16885 = container_of(ctrl->handler,
					     struct ov16885, ctrl_handler);
	struct i2c_client *client = ov16885->client;
	s64 max;
	int ret = 0;
	u32 val = 0, x_win = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov16885->cur_mode->height + ctrl->val - 12;
		__v4l2_ctrl_modify_range(ov16885->exposure,
					 ov16885->exposure->minimum, max,
					 ov16885->exposure->step,
					 ov16885->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = ov16885_write_reg(ov16885->client,
					OV16885_REG_EXPOSURE_H,
					OV16885_REG_VALUE_24BIT,
					ctrl->val << 4);
		dev_dbg(&client->dev, "set exposure 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov16885_set_gain_reg(ov16885, ctrl->val);
		dev_dbg(&client->dev, "set analog gain value 0x%x\n", ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = ov16885_write_reg(ov16885->client,
					OV16885_REG_VTS_H,
					OV16885_REG_VALUE_16BIT,
					ctrl->val + ov16885->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov16885_enable_test_pattern(ov16885, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = ov16885_read_reg(ov16885->client, OV16885_MIRROR_REG,
				       OV16885_REG_VALUE_08BIT,
				       &val);
		if (ctrl->val)
			val |= MIRROR_BIT_MASK;
		else
			val &= ~MIRROR_BIT_MASK;

		ret |= ov16885_read_reg(ov16885->client, OV16885_REG_ISP_X_WIN,
					OV16885_REG_VALUE_16BIT,
					&x_win);

		if ((x_win == 0x0010) && (val & 0x04))
			x_win = 0x0011;
		else if ((x_win == 0x0011) && (!(val & 0x04)))
			x_win = 0x0010;

		ret |= ov16885_write_reg(ov16885->client,
					 OV16885_GROUP_UPDATE_ADDRESS,
					 OV16885_REG_VALUE_08BIT,
					 OV16885_GROUP_UPDATE_START_DATA);

		ret |= ov16885_write_reg(ov16885->client, OV16885_MIRROR_REG,
					 OV16885_REG_VALUE_08BIT,
					 val);
		ret |= ov16885_write_reg(ov16885->client, OV16885_REG_ISP_X_WIN,
					 OV16885_REG_VALUE_16BIT,
					 x_win);

		ret |= ov16885_write_reg(ov16885->client,
					 OV16885_GROUP_UPDATE_ADDRESS,
					 OV16885_REG_VALUE_08BIT,
					 OV16885_GROUP_UPDATE_END_DATA);
		ret |= ov16885_write_reg(ov16885->client,
					 OV16885_GROUP_UPDATE_ADDRESS,
					 OV16885_REG_VALUE_08BIT,
					 OV16885_GROUP_UPDATE_LAUNCH);
		break;
	case V4L2_CID_VFLIP:
		ret = ov16885_read_reg(ov16885->client, OV16885_FLIP_REG,
				       OV16885_REG_VALUE_08BIT,
				       &val);
		if (ctrl->val)
			val |= FLIP_BIT_MASK;
		else
			val &= ~FLIP_BIT_MASK;
		ret |= ov16885_write_reg(ov16885->client, OV16885_FLIP_REG,
					 OV16885_REG_VALUE_08BIT,
					 val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov16885_ctrl_ops = {
	.s_ctrl = ov16885_set_ctrl,
};

static int ov16885_initialize_controls(struct ov16885 *ov16885)
{
	const struct ov16885_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	u64 dst_pixel_rate = 0;
	u32 lane_num = OV16885_LANES;

	handler = &ov16885->ctrl_handler;
	mode = ov16885->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &ov16885->mutex;

	ov16885->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
			V4L2_CID_LINK_FREQ,
			0, 0, link_freq_items);

	dst_pixel_rate = (u32)link_freq_items[mode->link_freq_idx] / mode->bpp * 2 * lane_num;

	ov16885->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
			V4L2_CID_PIXEL_RATE,
			0, OV16885_PIXEL_RATE,
			1, dst_pixel_rate);

	__v4l2_ctrl_s_ctrl(ov16885->link_freq,
			   mode->link_freq_idx);

	h_blank = mode->hts_def - mode->width;
	ov16885->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov16885->hblank)
		ov16885->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ov16885->vblank = v4l2_ctrl_new_std(handler, &ov16885_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OV16885_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 12;
	ov16885->exposure = v4l2_ctrl_new_std(handler, &ov16885_ctrl_ops,
				V4L2_CID_EXPOSURE, OV16885_EXPOSURE_MIN,
				exposure_max, OV16885_EXPOSURE_STEP,
				mode->exp_def);

	ov16885->anal_gain = v4l2_ctrl_new_std(handler, &ov16885_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, OV16885_GAIN_MIN,
				OV16885_GAIN_MAX, OV16885_GAIN_STEP,
				OV16885_GAIN_DEFAULT);

	ov16885->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ov16885_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov16885_test_pattern_menu) - 1,
				0, 0, ov16885_test_pattern_menu);

	ov16885->h_flip = v4l2_ctrl_new_std(handler, &ov16885_ctrl_ops,
					    V4L2_CID_HFLIP, 0, 1, 1, 0);

	ov16885->v_flip = v4l2_ctrl_new_std(handler, &ov16885_ctrl_ops,
					    V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&ov16885->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ov16885->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov16885_check_sensor_id(struct ov16885 *ov16885,
				   struct i2c_client *client)
{
	struct device *dev = &ov16885->client->dev;
	u32 id = 0;
	int ret;

	client->addr = OV16885_MAJOR_I2C_ADDR;

	ret = ov16885_read_reg(client, OV16885_REG_CHIP_ID,
			       OV16885_REG_VALUE_24BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		client->addr = OV16885_MINOR_I2C_ADDR;
		ret = ov16885_read_reg(client, OV16885_REG_CHIP_ID,
				       OV16885_REG_VALUE_24BIT, &id);
		if (id != CHIP_ID) {
			dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
			return -ENODEV;
		}
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int ov16885_configure_regulators(struct ov16885 *ov16885)
{
	unsigned int i;

	for (i = 0; i < OV16885_NUM_SUPPLIES; i++)
		ov16885->supplies[i].supply = ov16885_supply_names[i];

	return devm_regulator_bulk_get(&ov16885->client->dev,
				       OV16885_NUM_SUPPLIES,
				       ov16885->supplies);
}

static int ov16885_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct ov16885 *ov16885;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	ov16885 = devm_kzalloc(dev, sizeof(*ov16885), GFP_KERNEL);
	if (!ov16885)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &ov16885->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &ov16885->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &ov16885->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &ov16885->len_name);
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
	ov16885->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < ov16885->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			ov16885->cur_mode = &supported_modes[i];
			break;
		}
	}

	ov16885->client = client;

	ov16885->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov16885->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	ov16885->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(ov16885->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");

	ov16885->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov16885->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	ov16885->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov16885->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = ov16885_configure_regulators(ov16885);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	ov16885->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(ov16885->pinctrl)) {
		ov16885->pins_default =
			pinctrl_lookup_state(ov16885->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(ov16885->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		ov16885->pins_sleep =
			pinctrl_lookup_state(ov16885->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(ov16885->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&ov16885->mutex);

	sd = &ov16885->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov16885_subdev_ops);
	ret = ov16885_initialize_controls(ov16885);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov16885_power_on(ov16885);
	if (ret)
		goto err_free_handler;

	ret = ov16885_check_sensor_id(ov16885, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov16885_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov16885->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &ov16885->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(ov16885->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 ov16885->module_index, facing,
		 OV16885_NAME, dev_name(sd->dev));
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
	__ov16885_power_off(ov16885);
err_free_handler:
	v4l2_ctrl_handler_free(&ov16885->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov16885->mutex);

	return ret;
}

static int ov16885_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov16885 *ov16885 = to_ov16885(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov16885->ctrl_handler);
	mutex_destroy(&ov16885->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov16885_power_off(ov16885);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov16885_of_match[] = {
	{ .compatible = "ovti,ov16885" },
	{},
};
MODULE_DEVICE_TABLE(of, ov16885_of_match);
#endif

static const struct i2c_device_id ov16885_match_id[] = {
	{ "ovti,ov16885", 0 },
	{},
};

static struct i2c_driver ov16885_i2c_driver = {
	.driver = {
		.name = OV16885_NAME,
		.pm = &ov16885_pm_ops,
		.of_match_table = of_match_ptr(ov16885_of_match),
	},
	.probe		= &ov16885_probe,
	.remove		= &ov16885_remove,
	.id_table	= ov16885_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov16885_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov16885_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision ov16885 sensor driver");
MODULE_LICENSE("GPL");
