// SPDX-License-Identifier: GPL-2.0
/*
 * os05a20 driver
 *
 * Copyright (C) 2020 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x01)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIPI_FREQ_750M			750000000

#define PIXEL_RATE_WITH_750M		(MIPI_FREQ_750M * 2 / 12 * 4)

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define OS05A20_XVCLK_FREQ		24000000

#define CHIP_ID				0x530541
#define OS05A20_REG_CHIP_ID		0x300a

#define OS05A20_REG_CTRL_MODE		0x0100
#define OS05A20_MODE_SW_STANDBY		0x0
#define OS05A20_MODE_STREAMING		BIT(0)

#define	OS05A20_EXPOSURE_MIN		4
#define	OS05A20_EXPOSURE_STEP		1
#define OS05A20_VTS_MAX			0xffff

#define OS05A20_REG_EXP_LONG_H		0x3501
#define OS05A20_REG_EXP_VS_H		0x3511

#define OS05A20_REG_AGAIN_LONG_H	0x3508
#define OS05A20_REG_AGAIN_VS_H		0x350c

#define OS05A20_REG_DGAIN_LONG_H	0x350a
#define OS05A20_REG_DGAIN_VS_H		0x350e
#define OS05A20_GAIN_MIN		0x80
#define OS05A20_GAIN_MAX		0x3D9CC
#define OS05A20_GAIN_STEP		1
#define OS05A20_GAIN_DEFAULT		0x80

#define OS05A20_GROUP_UPDATE_ADDRESS	0x3208
#define OS05A20_GROUP_UPDATE_START_DATA	0x00
#define OS05A20_GROUP_UPDATE_END_DATA	0x10
#define OS05A20_GROUP_UPDATE_LAUNCH	0xA0

#define OS05A20_SOFTWARE_RESET_REG	0x0103

#define OS05A20_REG_TEST_PATTERN	0x5081
#define OS05A20_TEST_PATTERN_ENABLE	0x80
#define OS05A20_TEST_PATTERN_DISABLE	0x0

#define OS05A20_REG_VTS			0x380e

#define REG_NULL			0xFFFF

#define OS05A20_REG_VALUE_08BIT		1
#define OS05A20_REG_VALUE_16BIT		2
#define OS05A20_REG_VALUE_24BIT		3

#define OS05A20_LANES			4

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define OS05A20_NAME			"os05a20"

#define USED_SYS_DEBUG

static const char * const os05a20_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OS05A20_NUM_SUPPLIES ARRAY_SIZE(os05a20_supply_names)

#define OS05A20_FLIP_REG		0x3820
#define OS05A20_MIRROR_REG		0x3821
#define MIRROR_BIT_MASK			BIT(2)
#define FLIP_BIT_MASK			BIT(2)

enum os05a20_max_pad {
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

struct os05a20_mode {
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

struct os05a20 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OS05A20_NUM_SUPPLIES];

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
	const struct os05a20_mode *cur_mode;
	u32			cfg_num;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	bool			is_thunderboot;
	bool			is_thunderboot_ng;
	bool			is_first_streamoff;
};

#define to_os05a20(sd) container_of(sd, struct os05a20, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval os05a20_global_regs[] = {
	{REG_NULL, 0x00},
};

static const struct regval os05a20_linear12bit_2688x1944_regs[] = {
	{0x0103,  0x01},
	{0x0303,  0x01},
	{0x0305,  0x5e},
	{0x0306,  0x00},
	{0x0307,  0x00},
	{0x0308,  0x03},
	{0x0309,  0x04},
	{0x032a,  0x00},
	{0x031e,  0x0a},
	{0x0325,  0x48},
	{0x0328,  0x07},
	{0x300d,  0x11},
	{0x300e,  0x11},
	{0x300f,  0x11},
	{0x3010,  0x01},
	{0x3012,  0x41},
	{0x3016,  0xf0},
	{0x3018,  0xf0},
	{0x3028,  0xf0},
	{0x301e,  0x98},
	{0x3010,  0x04},
	{0x3011,  0x06},
	{0x3031,  0xa9},
	{0x3103,  0x48},
	{0x3104,  0x01},
	{0x3106,  0x10},
	{0x3400,  0x04},
	{0x3025,  0x03},
	{0x3425,  0x51},
	{0x3428,  0x01},
	{0x3406,  0x08},
	{0x3408,  0x03},
	{0x3501,  0x09},
	{0x3502,  0xa0},
	{0x3505,  0x83},
	{0x3508,  0x00},
	{0x3509,  0x80},
	{0x350a,  0x04},
	{0x350b,  0x00},
	{0x350c,  0x00},
	{0x350d,  0x80},
	{0x350e,  0x04},
	{0x350f,  0x00},
	{0x3600,  0x00},
	{0x3626,  0xff},
	{0x3605,  0x50},
	{0x3609,  0xdb},
	{0x3610,  0x69},
	{0x360c,  0x01},
	{0x3628,  0xa4},
	{0x3629,  0x6a},
	{0x362d,  0x10},
	{0x3660,  0xd3},
	{0x3661,  0x06},
	{0x3662,  0x00},
	{0x3663,  0x28},
	{0x3664,  0x0d},
	{0x366a,  0x38},
	{0x366b,  0xa0},
	{0x366d,  0x00},
	{0x366e,  0x00},
	{0x3680,  0x00},
	{0x36c0,  0x00},
	{0x3621,  0x81},
	{0x3634,  0x31},
	{0x3620,  0x00},
	{0x3622,  0x00},
	{0x362a,  0xd0},
	{0x362e,  0x8c},
	{0x362f,  0x98},
	{0x3630,  0xb0},
	{0x3631,  0xd7},
	{0x3701,  0x0f},
	{0x3737,  0x02},
	{0x3741,  0x04},
	{0x373c,  0x0f},
	{0x373b,  0x02},
	{0x3705,  0x00},
	{0x3706,  0xa0},
	{0x370a,  0x01},
	{0x370b,  0xc8},
	{0x3709,  0x4a},
	{0x3714,  0x21},
	{0x371c,  0x00},
	{0x371d,  0x08},
	{0x375e,  0x0e},
	{0x3760,  0x13},
	{0x3776,  0x10},
	{0x3781,  0x02},
	{0x3782,  0x04},
	{0x3783,  0x02},
	{0x3784,  0x08},
	{0x3785,  0x08},
	{0x3788,  0x01},
	{0x3789,  0x01},
	{0x3797,  0x84},
	{0x3798,  0x01},
	{0x3799,  0x00},
	{0x3761,  0x02},
	{0x3762,  0x0d},
	{0x3800,  0x00},
	{0x3801,  0x00},
	{0x3802,  0x00},
	{0x3803,  0x0c},
	{0x3804,  0x0e},
	{0x3805,  0xff},
	{0x3806,  0x08},
	{0x3807,  0x6f},
	{0x3808,  0x0a},
	{0x3809,  0x80},
	{0x380a,  0x07},
	{0x380b,  0x98},
	{0x380c,  0x02},
	{0x380d,  0xd0},
	{0x380e,  0x09},
	{0x380f,  0xc0},
	{0x3813,  0x04},
	{0x3814,  0x01},
	{0x3815,  0x01},
	{0x3816,  0x01},
	{0x3817,  0x01},
	{0x381c,  0x00},
	{0x3820,  0x00},
	{0x3821,  0x04},
	{0x3823,  0x18},
	{0x3826,  0x00},
	{0x3827,  0x01},
	{0x3832,  0x02},
	{0x383c,  0x48},
	{0x383d,  0xff},
	{0x3843,  0x20},
	{0x382d,  0x08},
	{0x3d85,  0x0b},
	{0x3d84,  0x40},
	{0x3d8c,  0x63},
	{0x3d8d,  0x00},
	{0x4000,  0x78},
	{0x4001,  0x2b},
	{0x4005,  0x40},
	{0x4028,  0x2f},
	{0x400a,  0x01},
	{0x4010,  0x12},
	{0x4008,  0x02},
	{0x4009,  0x0d},
	{0x401a,  0x58},
	{0x4050,  0x00},
	{0x4051,  0x01},
	{0x4052,  0x00},
	{0x4053,  0x80},
	{0x4054,  0x00},
	{0x4055,  0x80},
	{0x4056,  0x00},
	{0x4057,  0x80},
	{0x4058,  0x00},
	{0x4059,  0x80},
	{0x430b,  0xff},
	{0x430c,  0xff},
	{0x430d,  0x00},
	{0x430e,  0x00},
	{0x4501,  0x18},
	{0x4502,  0x00},
	{0x4600,  0x00},
	{0x4601,  0x10},
	{0x4603,  0x01},
	{0x4643,  0x00},
	{0x4640,  0x01},
	{0x4641,  0x04},
	{0x480e,  0x00},
	{0x4813,  0x00},
	{0x4815,  0x2b},
	{0x486e,  0x36},
	{0x486f,  0x84},
	{0x4860,  0x00},
	{0x4861,  0xa0},
	{0x484b,  0x05},
	{0x4850,  0x00},
	{0x4851,  0xaa},
	{0x4852,  0xff},
	{0x4853,  0x8a},
	{0x4854,  0x08},
	{0x4855,  0x30},
	{0x4800,  0x00},
	{0x4837,  0x0a},
	{0x484a,  0x3f},
	{0x5000,  0xc9},
	{0x5001,  0x43},
	{0x5002,  0x00},
	{0x5211,  0x03},
	{0x5291,  0x03},
	{0x520d,  0x0f},
	{0x520e,  0xfd},
	{0x520f,  0xa5},
	{0x5210,  0xa5},
	{0x528d,  0x0f},
	{0x528e,  0xfd},
	{0x528f,  0xa5},
	{0x5290,  0xa5},
	{0x5004,  0x40},
	{0x5005,  0x00},
	{0x5180,  0x00},
	{0x5181,  0x10},
	{0x5182,  0x0f},
	{0x5183,  0xff},
	{0x580b,  0x03},
	{0x4d00,  0x03},
	{0x4d01,  0xe9},
	{0x4d02,  0xba},
	{0x4d03,  0x66},
	{0x4d04,  0x46},
	{0x4d05,  0xa5},
	{0x3603,  0x3c},
	{0x3703,  0x26},
	{0x3709,  0x49},
	{0x3708,  0x2d},
	{0x3719,  0x1c},
	{0x371a,  0x06},
	{0x4000,  0x79},
	{0x380c,  0x04},
	{0x380d,  0x04},
	{0x380e,  0x0d},
	{0x380f,  0xad},
	{0x3501,  0x0d},
	{0x3502,  0xa5},
	{0x4603,  0x00},
	{REG_NULL, 0x00},
};

static const struct regval os05a20_hdr12bit_2688x1944_regs[] = {
	{0x0100,  0x00},
	{0x0103,  0x01},
	{0x0303,  0x01},
	{0x0305,  0x5e},
	{0x0306,  0x00},
	{0x0307,  0x00},
	{0x0308,  0x03},
	{0x0309,  0x04},
	{0x032a,  0x00},
	{0x031e,  0x0a},
	{0x0325,  0x48},
	{0x0328,  0x07},
	{0x300d,  0x11},
	{0x300e,  0x11},
	{0x300f,  0x11},
	{0x3026,  0x00},
	{0x3027,  0x00},
	{0x3010,  0x01},
	{0x3012,  0x41},
	{0x3016,  0xf0},
	{0x3018,  0xf0},
	{0x3028,  0xf0},
	{0x301e,  0x98},
	{0x3010,  0x01},
	{0x3011,  0x04},
	{0x3031,  0xa9},
	{0x3103,  0x48},
	{0x3104,  0x01},
	{0x3106,  0x10},
	{0x3501,  0x09},
	{0x3502,  0xa0},
	{0x3505,  0x83},
	{0x3508,  0x00},
	{0x3509,  0x80},
	{0x350a,  0x04},
	{0x350b,  0x00},
	{0x350c,  0x00},
	{0x350d,  0x80},
	{0x350e,  0x04},
	{0x350f,  0x00},
	{0x3600,  0x00},
	{0x3626,  0xff},
	{0x3605,  0x50},
	{0x3609,  0xb5},
	{0x3610,  0x69},
	{0x360c,  0x01},
	{0x3628,  0xa4},
	{0x3629,  0x6a},
	{0x362d,  0x10},
	{0x3660,  0x42},
	{0x3661,  0x07},
	{0x3662,  0x00},
	{0x3663,  0x28},
	{0x3664,  0x0d},
	{0x366a,  0x38},
	{0x366b,  0xa0},
	{0x366d,  0x00},
	{0x366e,  0x00},
	{0x3680,  0x00},
	{0x36c0,  0x00},
	{0x3621,  0x81},
	{0x3634,  0x31},
	{0x3620,  0x00},
	{0x3622,  0x00},
	{0x362a,  0xd0},
	{0x362e,  0x8c},
	{0x362f,  0x98},
	{0x3630,  0xb0},
	{0x3631,  0xd7},
	{0x3701,  0x0f},
	{0x3737,  0x02},
	{0x3740,  0x18},
	{0x3741,  0x04},
	{0x373c,  0x0f},
	{0x373b,  0x02},
	{0x3705,  0x00},
	{0x3706,  0x50},
	{0x370a,  0x00},
	{0x370b,  0xe4},
	{0x3709,  0x4a},
	{0x3714,  0x21},
	{0x371c,  0x00},
	{0x371d,  0x08},
	{0x375e,  0x0e},
	{0x3760,  0x13},
	{0x3776,  0x10},
	{0x3781,  0x02},
	{0x3782,  0x04},
	{0x3783,  0x02},
	{0x3784,  0x08},
	{0x3785,  0x08},
	{0x3788,  0x01},
	{0x3789,  0x01},
	{0x3797,  0x04},
	{0x3798,  0x01},
	{0x3799,  0x00},
	{0x3761,  0x02},
	{0x3762,  0x0d},
	{0x3800,  0x00},
	{0x3801,  0x00},
	{0x3802,  0x00},
	{0x3803,  0x0c},
	{0x3804,  0x0e},
	{0x3805,  0xff},
	{0x3806,  0x08},
	{0x3807,  0x6f},
	{0x3808,  0x0a},
	{0x3809,  0x80},
	{0x380a,  0x07},
	{0x380b,  0x98},
	{0x380c,  0x02},
	{0x380d,  0xd0},
	{0x380e,  0x09},
	{0x380f,  0xc0},
	{0x3811,  0x10},
	{0x3813,  0x04},
	{0x3814,  0x01},
	{0x3815,  0x01},
	{0x3816,  0x01},
	{0x3817,  0x01},
	{0x381c,  0x08},
	{0x3820,  0x00},
	{0x3821,  0x24},
	{0x3822,  0x54},
	{0x3823,  0x08},
	{0x3826,  0x00},
	{0x3827,  0x01},
	{0x3833,  0x01},
	{0x3832,  0x02},
	{0x383c,  0x48},
	{0x383d,  0xff},
	{0x3843,  0x20},
	{0x382d,  0x08},
	{0x3d85,  0x0b},
	{0x3d84,  0x40},
	{0x3d8c,  0x63},
	{0x3d8d,  0x00},
	{0x4000,  0x78},
	{0x4001,  0x2b},
	{0x4004,  0x00},
	{0x4005,  0x40},
	{0x4028,  0x2f},
	{0x400a,  0x01},
	{0x4010,  0x12},
	{0x4008,  0x02},
	{0x4009,  0x0d},
	{0x401a,  0x58},
	{0x4050,  0x00},
	{0x4051,  0x01},
	{0x4052,  0x00},
	{0x4053,  0x80},
	{0x4054,  0x00},
	{0x4055,  0x80},
	{0x4056,  0x00},
	{0x4057,  0x80},
	{0x4058,  0x00},
	{0x4059,  0x80},
	{0x430b,  0xff},
	{0x430c,  0xff},
	{0x430d,  0x00},
	{0x430e,  0x00},
	{0x4501,  0x18},
	{0x4502,  0x00},
	{0x4643,  0x00},
	{0x4640,  0x01},
	{0x4641,  0x04},
	{0x480e,  0x04},
	{0x4813,  0x98},
	{0x4815,  0x2b},
	{0x486e,  0x36},
	{0x486f,  0x84},
	{0x4860,  0x00},
	{0x4861,  0xa0},
	{0x484b,  0x05},
	{0x4850,  0x00},
	{0x4851,  0xaa},
	{0x4852,  0xff},
	{0x4853,  0x8a},
	{0x4854,  0x08},
	{0x4855,  0x30},
	{0x4800,  0x60},
	{0x4837,  0x0a},
	{0x484a,  0x3f},
	{0x5000,  0xc9},
	{0x5001,  0x43},
	{0x5002,  0x00},
	{0x5211,  0x03},
	{0x5291,  0x03},
	{0x520d,  0x0f},
	{0x520e,  0xfd},
	{0x520f,  0xa5},
	{0x5210,  0xa5},
	{0x528d,  0x0f},
	{0x528e,  0xfd},
	{0x528f,  0xa5},
	{0x5290,  0xa5},
	{0x5004,  0x40},
	{0x5005,  0x00},
	{0x5180,  0x00},
	{0x5181,  0x10},
	{0x5182,  0x0f},
	{0x5183,  0xff},
	{0x580b,  0x03},
	{0x4d00,  0x03},
	{0x4d01,  0xe9},
	{0x4d02,  0xba},
	{0x4d03,  0x66},
	{0x4d04,  0x46},
	{0x4d05,  0xa5},
	{0x3603,  0x3c},
	{0x3703,  0x26},
	{0x3709,  0x49},
	{0x3708,  0x2d},
	{0x3719,  0x1c},
	{0x371a,  0x06},
	{0x4000,  0x79},
	{0x380c,  0x02},
	{0x380d,  0xd0},
	{0x380e,  0x09},
	{0x380f,  0xc4},
	{0x3501,  0x08},
	{0x3502,  0xc4},
	{0x3511,  0x00},
	{0x3512,  0x20},
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
static const struct os05a20_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR12_1X12,
		.width = 2688,
		.height = 1944,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x09a0,
		.hts_def = 0x02d0 * 4,
		.vts_def = 0x0dad,
		.reg_list = os05a20_linear12bit_2688x1944_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR12_1X12,
		.width = 2688,
		.height = 1944,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x09a0,
		.hts_def = 0x02d0 * 4,
		.vts_def = 0x09c4,
		.reg_list = os05a20_hdr12bit_2688x1944_regs,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2 //Á½Õë¹Ì¶¨¶ÌÖ¡
	},
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ_750M,
};

static const char * const os05a20_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

static int __os05a20_power_on(struct os05a20 *os05a20);

/* Write registers up to 4 at a time */
static int os05a20_write_reg(struct i2c_client *client, u16 reg,
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

static int os05a20_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret |= os05a20_write_reg(client, regs[i].addr,
			OS05A20_REG_VALUE_08BIT, regs[i].val);
	}
	return ret;
}

/* Read registers up to 4 at a time */
static int os05a20_read_reg(struct i2c_client *client,
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

static int os05a20_get_reso_dist(const struct os05a20_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct os05a20_mode *
os05a20_find_best_fit(struct os05a20 *os05a20, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < os05a20->cfg_num; i++) {
		dist = os05a20_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
			(supported_modes[i].bus_fmt == framefmt->code)) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int os05a20_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct os05a20 *os05a20 = to_os05a20(sd);
	const struct os05a20_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&os05a20->mutex);

	mode = os05a20_find_best_fit(os05a20, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&os05a20->mutex);
		return -ENOTTY;
#endif
	} else {
		os05a20->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(os05a20->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(os05a20->vblank, vblank_def,
					 OS05A20_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&os05a20->mutex);

	return 0;
}

static int os05a20_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct os05a20 *os05a20 = to_os05a20(sd);
	const struct os05a20_mode *mode = os05a20->cur_mode;

	mutex_lock(&os05a20->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&os05a20->mutex);
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
	mutex_unlock(&os05a20->mutex);

	return 0;
}

static int os05a20_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct os05a20 *os05a20 = to_os05a20(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = os05a20->cur_mode->bus_fmt;

	return 0;
}

static int os05a20_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct os05a20 *os05a20 = to_os05a20(sd);

	if (fse->index >= os05a20->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int os05a20_enable_test_pattern(struct os05a20 *os05a20, u32 pattern)
{
	u32 val;
	int ret = 0;

	if (pattern)
		val = ((pattern - 1) << 2) | OS05A20_TEST_PATTERN_ENABLE;
	else
		val = OS05A20_TEST_PATTERN_DISABLE;
	ret = os05a20_write_reg(os05a20->client, OS05A20_REG_TEST_PATTERN,
				OS05A20_REG_VALUE_08BIT, val);
	return ret;
}

static int os05a20_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct os05a20 *os05a20 = to_os05a20(sd);
	const struct os05a20_mode *mode = os05a20->cur_mode;

	mutex_lock(&os05a20->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&os05a20->mutex);

	return 0;
}

static int os05a20_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	struct os05a20 *os05a20 = to_os05a20(sd);
	const struct os05a20_mode *mode = os05a20->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (OS05A20_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	if (mode->hdr_mode == HDR_X2)
		val = 1 << (OS05A20_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		V4L2_MBUS_CSI2_CHANNEL_1;

	config->type = V4L2_MBUS_CSI2;
	config->flags = val;

	return 0;
}

static void os05a20_get_module_inf(struct os05a20 *os05a20,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, OS05A20_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, os05a20->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, os05a20->len_name, sizeof(inf->base.lens));
}

static int os05a20_set_hdrae(struct os05a20 *os05a20,
			     struct preisp_hdrae_exp_s *ae)
{
	u32 m_exp_time, s_exp_time;
	u32 m_gain, s_gain;
	u32 m_d_gain = 1024;
	u32 s_d_gain = 1024;
	int ret = 0;

	if (!os05a20->has_init_exp && !os05a20->streaming) {
		os05a20->init_hdrae_exp = *ae;
		os05a20->has_init_exp = true;
		dev_dbg(&os05a20->client->dev, "os05a20 don't stream, record exp for hdr!\n");
		return ret;
	}
	m_exp_time = ae->middle_exp_reg;
	s_exp_time = ae->short_exp_reg;
	m_gain = ae->middle_gain_reg;
	s_gain = ae->short_gain_reg;
	dev_dbg(&os05a20->client->dev,
		"rev exp req: L_exp: 0x%x, 0x%x, S_exp: 0x%x, 0x%x\n",
		m_exp_time, m_gain,
		s_exp_time, s_gain);

	if (m_exp_time <= s_exp_time || m_exp_time < 4 || s_exp_time < 4) {
		dev_err(&os05a20->client->dev,
			"long exposure must bigger than short exposure,min exposure is 4 line\n");
		return -EINVAL;
	}

	if (m_gain > 1984) {// >15.5x
		m_d_gain = m_gain * 10 / 155;
		m_gain = 1984;
	}

	if (s_gain > 1984) {// >15.5x
		s_d_gain = s_gain * 10 / 155;
		s_gain = 1984;
	}
	dev_dbg(&os05a20->client->dev,
		"set exp: L_exp: 0x%x, 0x%x 0x%x, S_exp: 0x%x, 0x%x, 0x%x\n",
		m_exp_time, m_gain, m_d_gain,
		s_exp_time, s_gain, s_d_gain);

	ret = os05a20_write_reg(os05a20->client,
				OS05A20_GROUP_UPDATE_ADDRESS,
				OS05A20_REG_VALUE_08BIT,
				OS05A20_GROUP_UPDATE_START_DATA);

	ret |= os05a20_write_reg(os05a20->client,
				OS05A20_REG_EXP_LONG_H,
				OS05A20_REG_VALUE_16BIT,
				m_exp_time);
	ret |= os05a20_write_reg(os05a20->client,
				OS05A20_REG_EXP_VS_H,
				OS05A20_REG_VALUE_16BIT,
				s_exp_time);

	ret |= os05a20_write_reg(os05a20->client,
				OS05A20_REG_AGAIN_LONG_H,
				OS05A20_REG_VALUE_16BIT,
				m_gain & 0x7ff);
	ret |= os05a20_write_reg(os05a20->client,
				OS05A20_REG_DGAIN_LONG_H,
				OS05A20_REG_VALUE_16BIT,
				m_d_gain & 0x3fff);

	ret |= os05a20_write_reg(os05a20->client,
				OS05A20_REG_AGAIN_VS_H,
				OS05A20_REG_VALUE_16BIT,
				s_gain & 0x7ff);
	ret |= os05a20_write_reg(os05a20->client,
				OS05A20_REG_DGAIN_VS_H,
				OS05A20_REG_VALUE_16BIT,
				s_d_gain & 0x3fff);
	ret |= os05a20_write_reg(os05a20->client,
				OS05A20_GROUP_UPDATE_ADDRESS,
				OS05A20_REG_VALUE_08BIT,
				OS05A20_GROUP_UPDATE_END_DATA);
	ret |= os05a20_write_reg(os05a20->client,
				OS05A20_GROUP_UPDATE_ADDRESS,
				OS05A20_REG_VALUE_08BIT,
				OS05A20_GROUP_UPDATE_LAUNCH);
	return ret;
}

static long os05a20_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct os05a20 *os05a20 = to_os05a20(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	long ret = 0;
	u32 i, h, w;
	u32 stream = 0;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		return os05a20_set_hdrae(os05a20, arg);
	case RKMODULE_SET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		w = os05a20->cur_mode->width;
		h = os05a20->cur_mode->height;
		for (i = 0; i < os05a20->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			h == supported_modes[i].height &&
			supported_modes[i].hdr_mode == hdr_cfg->hdr_mode) {
				os05a20->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == os05a20->cfg_num) {
			dev_err(&os05a20->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr_cfg->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = os05a20->cur_mode->hts_def - os05a20->cur_mode->width;
			h = os05a20->cur_mode->vts_def - os05a20->cur_mode->height;
			__v4l2_ctrl_modify_range(os05a20->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(os05a20->vblank, h,
				OS05A20_VTS_MAX - os05a20->cur_mode->height,
				1, h);
			dev_info(&os05a20->client->dev,
				"sensor mode: %d\n",
				os05a20->cur_mode->hdr_mode);
		}
		break;
	case RKMODULE_GET_MODULE_INFO:
		os05a20_get_module_inf(os05a20, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = os05a20->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		ret = -EINVAL;
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = os05a20_write_reg(os05a20->client, OS05A20_REG_CTRL_MODE,
				OS05A20_REG_VALUE_08BIT, OS05A20_MODE_STREAMING);
		else
			ret = os05a20_write_reg(os05a20->client, OS05A20_REG_CTRL_MODE,
				OS05A20_REG_VALUE_08BIT, OS05A20_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long os05a20_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
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

		ret = os05a20_ioctl(sd, cmd, inf);
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
			ret = os05a20_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = os05a20_ioctl(sd, cmd, hdr);
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
			ret = os05a20_ioctl(sd, cmd, hdr);
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
			ret = os05a20_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		ret = -EINVAL;
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = os05a20_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __os05a20_start_stream(struct os05a20 *os05a20)
{
	int ret;

	if (!os05a20->is_thunderboot) {
		ret = os05a20_write_array(os05a20->client, os05a20_global_regs);
		if (ret) {
			dev_err(&os05a20->client->dev,
				"could not set init registers\n");
			return ret;
		}

		ret = os05a20_write_array(os05a20->client, os05a20->cur_mode->reg_list);
		if (ret)
			return ret;
	}

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&os05a20->ctrl_handler);
	if (ret)
		return ret;
	if (os05a20->has_init_exp && os05a20->cur_mode->hdr_mode != NO_HDR) {
		ret = os05a20_ioctl(&os05a20->subdev,
				    PREISP_CMD_SET_HDRAE_EXP,
				    &os05a20->init_hdrae_exp);
		if (ret) {
			dev_err(&os05a20->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}
	return	os05a20_write_reg(os05a20->client, OS05A20_REG_CTRL_MODE,
		OS05A20_REG_VALUE_08BIT, OS05A20_MODE_STREAMING);
}

static int __os05a20_stop_stream(struct os05a20 *os05a20)
{
	os05a20->has_init_exp = false;
	if (os05a20->is_thunderboot)
		os05a20->is_first_streamoff = true;
	return os05a20_write_reg(os05a20->client, OS05A20_REG_CTRL_MODE,
		OS05A20_REG_VALUE_08BIT, OS05A20_MODE_SW_STANDBY);
}

static int os05a20_s_stream(struct v4l2_subdev *sd, int on)
{
	struct os05a20 *os05a20 = to_os05a20(sd);
	struct i2c_client *client = os05a20->client;
	int ret = 0;

	mutex_lock(&os05a20->mutex);
	on = !!on;
	if (on == os05a20->streaming)
		goto unlock_and_return;

	if (on) {
		if (os05a20->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
			os05a20->is_thunderboot = false;
			__os05a20_power_on(os05a20);
		}
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __os05a20_start_stream(os05a20);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__os05a20_stop_stream(os05a20);
		pm_runtime_put(&client->dev);
	}

	os05a20->streaming = on;

unlock_and_return:
	mutex_unlock(&os05a20->mutex);

	return ret;
}

static int os05a20_s_power(struct v4l2_subdev *sd, int on)
{
	struct os05a20 *os05a20 = to_os05a20(sd);
	struct i2c_client *client = os05a20->client;
	int ret = 0;

	mutex_lock(&os05a20->mutex);

	/* If the power state is not modified - no work to do. */
	if (os05a20->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		if (!os05a20->is_thunderboot) {
			ret |= os05a20_write_reg(os05a20->client,
						 OS05A20_SOFTWARE_RESET_REG,
						 OS05A20_REG_VALUE_08BIT,
						 0x01);
			usleep_range(100, 200);
		}

		os05a20->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		os05a20->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&os05a20->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 os05a20_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OS05A20_XVCLK_FREQ / 1000 / 1000);
}

static int __os05a20_power_on(struct os05a20 *os05a20)
{
	int ret;
	u32 delay_us;
	struct device *dev = &os05a20->client->dev;

	if (os05a20->is_thunderboot)
		return 0;

	if (!IS_ERR_OR_NULL(os05a20->pins_default)) {
		ret = pinctrl_select_state(os05a20->pinctrl,
					   os05a20->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(os05a20->xvclk, OS05A20_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(os05a20->xvclk) != OS05A20_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(os05a20->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(os05a20->power_gpio)) {
		gpiod_direction_output(os05a20->power_gpio, 1);
		usleep_range(6000, 8000);
	}
	if (!IS_ERR(os05a20->reset_gpio))
		gpiod_direction_output(os05a20->reset_gpio, 1);

	ret = regulator_bulk_enable(OS05A20_NUM_SUPPLIES, os05a20->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(os05a20->reset_gpio))
		gpiod_direction_output(os05a20->reset_gpio, 0);

	usleep_range(500, 1000);
	if (!IS_ERR(os05a20->pwdn_gpio))
		gpiod_direction_output(os05a20->pwdn_gpio, 1);
	/*
	 * There is no need to wait for the delay of RC circuit
	 * if the reset signal is directly controlled by GPIO.
	 */
	if (!IS_ERR(os05a20->reset_gpio))
		usleep_range(6000, 8000);
	else
		usleep_range(12000, 16000);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = os05a20_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(os05a20->xvclk);

	return ret;
}

static void __os05a20_power_off(struct os05a20 *os05a20)
{
	int ret;
	struct device *dev = &os05a20->client->dev;

	if (os05a20->is_thunderboot) {
		if (os05a20->is_first_streamoff) {
			os05a20->is_thunderboot = false;
			os05a20->is_first_streamoff = false;
		} else {
			return;
		}
	}

	if (!IS_ERR(os05a20->pwdn_gpio))
		gpiod_direction_output(os05a20->pwdn_gpio, 0);

	clk_disable_unprepare(os05a20->xvclk);

	if (!IS_ERR(os05a20->reset_gpio))
		gpiod_direction_output(os05a20->reset_gpio, 0);
	if (!IS_ERR(os05a20->power_gpio))
		gpiod_direction_output(os05a20->power_gpio, 0);
	if (!IS_ERR_OR_NULL(os05a20->pins_sleep)) {
		ret = pinctrl_select_state(os05a20->pinctrl,
					   os05a20->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}

	if (os05a20->is_thunderboot_ng) {
		os05a20->is_thunderboot_ng = false;
		regulator_bulk_disable(OS05A20_NUM_SUPPLIES, os05a20->supplies);
	}
}

static int os05a20_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os05a20 *os05a20 = to_os05a20(sd);

	return __os05a20_power_on(os05a20);
}

static int os05a20_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os05a20 *os05a20 = to_os05a20(sd);

	__os05a20_power_off(os05a20);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int os05a20_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct os05a20 *os05a20 = to_os05a20(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct os05a20_mode *def_mode = &supported_modes[0];

	mutex_lock(&os05a20->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&os05a20->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int os05a20_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct os05a20 *os05a20 = to_os05a20(sd);

	if (fie->index >= os05a20->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops os05a20_pm_ops = {
	SET_RUNTIME_PM_OPS(os05a20_runtime_suspend,
			   os05a20_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops os05a20_internal_ops = {
	.open = os05a20_open,
};
#endif

static const struct v4l2_subdev_core_ops os05a20_core_ops = {
	.s_power = os05a20_s_power,
	.ioctl = os05a20_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = os05a20_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops os05a20_video_ops = {
	.s_stream = os05a20_s_stream,
	.g_frame_interval = os05a20_g_frame_interval,
	.g_mbus_config = os05a20_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops os05a20_pad_ops = {
	.enum_mbus_code = os05a20_enum_mbus_code,
	.enum_frame_size = os05a20_enum_frame_sizes,
	.enum_frame_interval = os05a20_enum_frame_interval,
	.get_fmt = os05a20_get_fmt,
	.set_fmt = os05a20_set_fmt,
};

static const struct v4l2_subdev_ops os05a20_subdev_ops = {
	.core	= &os05a20_core_ops,
	.video	= &os05a20_video_ops,
	.pad	= &os05a20_pad_ops,
};

static int os05a20_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct os05a20 *os05a20 = container_of(ctrl->handler,
					     struct os05a20, ctrl_handler);
	struct i2c_client *client = os05a20->client;
	s64 max;
	int ret = 0;
	u32 again, dgain;
	u32 val = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		if (os05a20->cur_mode->hdr_mode == NO_HDR) {
			/* Update max exposure while meeting expected vblanking */
			max = os05a20->cur_mode->height + ctrl->val - 8;
			__v4l2_ctrl_modify_range(os05a20->exposure,
						os05a20->exposure->minimum, max,
						os05a20->exposure->step,
						os05a20->exposure->default_value);
			break;
		}
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (os05a20->cur_mode->hdr_mode != NO_HDR)
			return 0;
		ret = os05a20_write_reg(os05a20->client,
					OS05A20_REG_EXP_LONG_H,
					OS05A20_REG_VALUE_16BIT,
					ctrl->val);
		dev_dbg(&client->dev, "set exposure 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (os05a20->cur_mode->hdr_mode != NO_HDR)
			return 0;
		if (ctrl->val > 1984) {// >15.5x
			dgain = ctrl->val * 10 / 155;
			again = 1984;
		} else {
			dgain = 1024;
			again = ctrl->val;
		}
		ret = os05a20_write_reg(os05a20->client,
					OS05A20_REG_AGAIN_LONG_H,
					OS05A20_REG_VALUE_16BIT,
					again & 0x7ff);
		ret |= os05a20_write_reg(os05a20->client,
					OS05A20_REG_DGAIN_LONG_H,
					OS05A20_REG_VALUE_16BIT,
					dgain & 0x3fff);
		dev_dbg(&client->dev, "set analog gain 0x%x digital gain 0x%x\n",
			again, dgain);
		break;
	case V4L2_CID_VBLANK:
		ret = os05a20_write_reg(os05a20->client, OS05A20_REG_VTS,
					OS05A20_REG_VALUE_16BIT,
					ctrl->val + os05a20->cur_mode->height);
		dev_dbg(&client->dev, "set vblank 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = os05a20_enable_test_pattern(os05a20, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = os05a20_read_reg(os05a20->client, OS05A20_MIRROR_REG,
				       OS05A20_REG_VALUE_08BIT,
				       &val);
		if (ctrl->val)
			val |= MIRROR_BIT_MASK;
		else
			val &= ~MIRROR_BIT_MASK;
		ret = os05a20_write_reg(os05a20->client, OS05A20_MIRROR_REG,
					OS05A20_REG_VALUE_08BIT,
					val);
		break;
	case V4L2_CID_VFLIP:
		ret = os05a20_read_reg(os05a20->client, OS05A20_FLIP_REG,
				       OS05A20_REG_VALUE_08BIT,
				       &val);
		if (ctrl->val)
			val |= FLIP_BIT_MASK;
		else
			val &= ~FLIP_BIT_MASK;
		ret = os05a20_write_reg(os05a20->client, OS05A20_FLIP_REG,
					OS05A20_REG_VALUE_08BIT,
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

static const struct v4l2_ctrl_ops os05a20_ctrl_ops = {
	.s_ctrl = os05a20_set_ctrl,
};

static int os05a20_initialize_controls(struct os05a20 *os05a20)
{
	const struct os05a20_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &os05a20->ctrl_handler;
	mode = os05a20->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &os05a20->mutex;

	os05a20->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
			V4L2_CID_LINK_FREQ,
			1, 0, link_freq_menu_items);
	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	os05a20->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
			V4L2_CID_PIXEL_RATE,
			0, PIXEL_RATE_WITH_750M,
			1, PIXEL_RATE_WITH_750M);

	h_blank = mode->hts_def - mode->width;
	os05a20->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (os05a20->hblank)
		os05a20->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	os05a20->vblank = v4l2_ctrl_new_std(handler, &os05a20_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    OS05A20_VTS_MAX - mode->height,
					    1, vblank_def);

	exposure_max = mode->vts_def - 8;
	os05a20->exposure = v4l2_ctrl_new_std(handler, &os05a20_ctrl_ops,
					      V4L2_CID_EXPOSURE, OS05A20_EXPOSURE_MIN,
					      exposure_max, OS05A20_EXPOSURE_STEP,
					      mode->exp_def);

	os05a20->anal_gain = v4l2_ctrl_new_std(handler, &os05a20_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN, OS05A20_GAIN_MIN,
					       OS05A20_GAIN_MAX, OS05A20_GAIN_STEP,
					       OS05A20_GAIN_DEFAULT);

	os05a20->test_pattern =
		v4l2_ctrl_new_std_menu_items(handler,
					     &os05a20_ctrl_ops, V4L2_CID_TEST_PATTERN,
					     ARRAY_SIZE(os05a20_test_pattern_menu) - 1,
					     0, 0, os05a20_test_pattern_menu);

	os05a20->h_flip = v4l2_ctrl_new_std(handler, &os05a20_ctrl_ops,
					    V4L2_CID_HFLIP, 0, 1, 1, 0);

	os05a20->v_flip = v4l2_ctrl_new_std(handler, &os05a20_ctrl_ops,
					    V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (handler->error) {
		ret = handler->error;
		dev_err(&os05a20->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	os05a20->subdev.ctrl_handler = handler;
	os05a20->has_init_exp = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int os05a20_check_sensor_id(struct os05a20 *os05a20,
				  struct i2c_client *client)
{
	struct device *dev = &os05a20->client->dev;
	u32 id = 0;
	int ret;

	if (os05a20->is_thunderboot) {
		dev_info(dev, "Enable thunderboot mode, skip sensor id check\n");
		return 0;
	}

	ret = os05a20_read_reg(client, OS05A20_REG_CHIP_ID,
			       OS05A20_REG_VALUE_24BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int os05a20_configure_regulators(struct os05a20 *os05a20)
{
	unsigned int i;

	for (i = 0; i < OS05A20_NUM_SUPPLIES; i++)
		os05a20->supplies[i].supply = os05a20_supply_names[i];

	return devm_regulator_bulk_get(&os05a20->client->dev,
				       OS05A20_NUM_SUPPLIES,
				       os05a20->supplies);
}

static int os05a20_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct os05a20 *os05a20;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	os05a20 = devm_kzalloc(dev, sizeof(*os05a20), GFP_KERNEL);
	if (!os05a20)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &os05a20->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &os05a20->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &os05a20->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &os05a20->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	os05a20->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);
	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE,
				   &hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}
	os05a20->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < os05a20->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			os05a20->cur_mode = &supported_modes[i];
			break;
		}
	}
	os05a20->client = client;

	os05a20->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(os05a20->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	os05a20->power_gpio = devm_gpiod_get(dev, "power", GPIOD_ASIS);
	if (IS_ERR(os05a20->power_gpio))
		dev_warn(dev, "Failed to get power-gpios\n");

	os05a20->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(os05a20->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	os05a20->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_ASIS);
	if (IS_ERR(os05a20->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	os05a20->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(os05a20->pinctrl)) {
		os05a20->pins_default =
			pinctrl_lookup_state(os05a20->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(os05a20->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		os05a20->pins_sleep =
			pinctrl_lookup_state(os05a20->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(os05a20->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = os05a20_configure_regulators(os05a20);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&os05a20->mutex);

	sd = &os05a20->subdev;
	v4l2_i2c_subdev_init(sd, client, &os05a20_subdev_ops);
	ret = os05a20_initialize_controls(os05a20);
	if (ret)
		goto err_destroy_mutex;

	ret = __os05a20_power_on(os05a20);
	if (ret)
		goto err_free_handler;

	ret = os05a20_check_sensor_id(os05a20, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &os05a20_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	os05a20->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &os05a20->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(os05a20->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 os05a20->module_index, facing,
		 OS05A20_NAME, dev_name(sd->dev));
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
	__os05a20_power_off(os05a20);
err_free_handler:
	v4l2_ctrl_handler_free(&os05a20->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&os05a20->mutex);

	return ret;
}

static int os05a20_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct os05a20 *os05a20 = to_os05a20(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&os05a20->ctrl_handler);
	mutex_destroy(&os05a20->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__os05a20_power_off(os05a20);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id os05a20_of_match[] = {
	{ .compatible = "ovti,os05a20" },
	{},
};
MODULE_DEVICE_TABLE(of, os05a20_of_match);
#endif

static const struct i2c_device_id os05a20_match_id[] = {
	{ "ovti,os05a20", 0 },
	{ },
};

static struct i2c_driver os05a20_i2c_driver = {
	.driver = {
		.name = OS05A20_NAME,
		.pm = &os05a20_pm_ops,
		.of_match_table = of_match_ptr(os05a20_of_match),
	},
	.probe		= &os05a20_probe,
	.remove		= &os05a20_remove,
	.id_table	= os05a20_match_id,
};

#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT
module_i2c_driver(os05a20_i2c_driver);
#else
static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&os05a20_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&os05a20_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);
#endif

MODULE_DESCRIPTION("OmniVision os05a20 sensor driver");
MODULE_LICENSE("GPL v2");
