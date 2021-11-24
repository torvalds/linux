// SPDX-License-Identifier: GPL-2.0
/*
 * ov13855 camera driver
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 fix some errors.
 * V0.0X01.0X02 add get_selection.
 *
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
#include <linux/compat.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x02)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define OV13855_LINK_FREQ_540MHZ	540000000U
#define OV13855_LINK_FREQ_270MHZ	270000000U
/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define OV13855_PIXEL_RATE		(OV13855_LINK_FREQ_540MHZ * 2LL * 4LL / 10LL)
#define OV13855_XVCLK_FREQ		24000000

#define CHIP_ID				0x00d855
#define OV13855_REG_CHIP_ID		0x300a

#define OV13855_REG_CTRL_MODE		0x0100
#define OV13855_MODE_SW_STANDBY		0x0
#define OV13855_MODE_STREAMING		BIT(0)

#define OV13855_REG_EXPOSURE		0x3500
#define	OV13855_EXPOSURE_MIN		4
#define	OV13855_EXPOSURE_STEP		1
#define OV13855_VTS_MAX			0x7fff

#define OV13855_REG_GAIN_H		0x3508
#define OV13855_REG_GAIN_L		0x3509
#define OV13855_GAIN_H_MASK		0x1f
#define OV13855_GAIN_H_SHIFT		8
#define OV13855_GAIN_L_MASK		0xff
#define OV13855_GAIN_MIN		0x80
#define OV13855_GAIN_MAX		0x7c0
#define OV13855_GAIN_STEP		1
#define OV13855_GAIN_DEFAULT		0x80

#define OV13855_REG_TEST_PATTERN	0x5e00
#define	OV13855_TEST_PATTERN_ENABLE	0x80
#define	OV13855_TEST_PATTERN_DISABLE	0x0

#define OV13855_REG_VTS			0x380e

#define REG_NULL			0xFFFF

#define OV13855_REG_VALUE_08BIT		1
#define OV13855_REG_VALUE_16BIT		2
#define OV13855_REG_VALUE_24BIT		3

#define OV13855_LANES			4
#define OV13855_BITS_PER_SAMPLE		10

#define OV13855_CHIP_REVISION_REG	0x302A

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define OV13855_NAME			"ov13855"
#define OV13855_MEDIA_BUS_FMT		MEDIA_BUS_FMT_SBGGR10_1X10

static const char * const ov13855_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV13855_NUM_SUPPLIES ARRAY_SIZE(ov13855_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct ov13855_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 link_freq_idx;
	u32 bpp;
	const struct regval *reg_list;
};

struct ov13855 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV13855_NUM_SUPPLIES];

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
	const struct ov13855_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_ov13855(sd) container_of(sd, struct ov13855, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval ov13855_global_regs[] = {
	{0x0103, 0x01},
	{0x0300, 0x02},
	{0x0301, 0x00},
	{0x0302, 0x5a},
	{0x0303, 0x00},
	{0x0304, 0x00},
	{0x0305, 0x01},
	{0x030b, 0x06},
	{0x030c, 0x02},
	{0x030d, 0x88},
	{0x0312, 0x11},
	{0x3022, 0x01},
	{0x3013, 0x32},
	{0x3016, 0x72},
	{0x301b, 0xF0},
	{0x301f, 0xd0},
	{0x3106, 0x15},
	{0x3107, 0x23},
	{0x3500, 0x00},
	{0x3501, 0x80},
	{0x3502, 0x00},
	{0x3508, 0x02},
	{0x3509, 0x00},
	{0x350a, 0x00},
	{0x350e, 0x00},
	{0x3510, 0x00},
	{0x3511, 0x02},
	{0x3512, 0x00},
	{0x3600, 0x2b},
	{0x3601, 0x52},
	{0x3602, 0x60},
	{0x3612, 0x05},
	{0x3613, 0xa4},
	{0x3620, 0x80},
	{0x3621, 0x10},
	{0x3622, 0x30},
	{0x3624, 0x1c},
	{0x3640, 0x10},
	{0x3661, 0x70},
	{0x3661, 0x80},
	{0x3662, 0x12},
	{0x3664, 0x73},
	{0x3665, 0xa7},
	{0x366e, 0xff},
	{0x366f, 0xf4},
	{0x3674, 0x00},
	{0x3679, 0x0c},
	{0x367f, 0x01},
	{0x3680, 0x0c},
	{0x3681, 0x50},
	{0x3682, 0x50},
	{0x3683, 0xa9},
	{0x3684, 0xa9},
	{0x3709, 0x5f},
	{0x3714, 0x24},
	{0x371a, 0x3e},
	{0x3737, 0x04},
	{0x3738, 0xcc},
	{0x3739, 0x12},
	{0x373d, 0x26},
	{0x3764, 0x20},
	{0x3765, 0x20},
	{0x37a1, 0x36},
	{0x37a8, 0x3b},
	{0x37ab, 0x31},
	{0x37c2, 0x04},
	{0x37c3, 0xf1},
	{0x37c5, 0x00},
	{0x37d8, 0x03},
	{0x37d9, 0x0c},
	{0x37da, 0xc2},
	{0x37dc, 0x02},
	{0x37e0, 0x00},
	{0x37e1, 0x0a},
	{0x37e2, 0x14},
	{0x37e3, 0x04},
	{0x37e4, 0x2a},
	{0x37e5, 0x03},
	{0x37e6, 0x04},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x08},
	{0x3804, 0x10},
	{0x3805, 0x9f},
	{0x3806, 0x0c},
	{0x3807, 0x57},
	{0x3808, 0x10},
	{0x3809, 0x80},
	{0x380a, 0x0c},
	{0x380b, 0x40},
	{0x380c, 0x04},
	{0x380d, 0x62},
	{0x380e, 0x0c},
	{0x380f, 0x8e},
	{0x3811, 0x10},
	{0x3813, 0x08},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3820, 0xa8},
	{0x3821, 0x00},
	{0x3822, 0xc2},
	{0x3823, 0x18},
	{0x3826, 0x11},
	{0x3827, 0x1c},
	{0x3829, 0x03},
	{0x3832, 0x00},
	{0x3c80, 0x00},
	{0x3c87, 0x01},
	{0x3c8c, 0x19},
	{0x3c8d, 0x1c},
	{0x3c90, 0x00},
	{0x3c91, 0x00},
	{0x3c92, 0x00},
	{0x3c93, 0x00},
	{0x3c94, 0x40},
	{0x3c95, 0x54},
	{0x3c96, 0x34},
	{0x3c97, 0x04},
	{0x3c98, 0x00},
	{0x3d8c, 0x73},
	{0x3d8d, 0xc0},
	{0x3f00, 0x0b},
	{0x3f03, 0x00},
	{0x4001, 0xe0},
	{0x4008, 0x00},
	{0x4009, 0x0f},
	{0x4011, 0xf0},
	{0x4050, 0x04},
	{0x4051, 0x0b},
	{0x4052, 0x00},
	{0x4053, 0x80},
	{0x4054, 0x00},
	{0x4055, 0x80},
	{0x4056, 0x00},
	{0x4057, 0x80},
	{0x4058, 0x00},
	{0x4059, 0x80},
	{0x405e, 0x00},
	{0x4500, 0x07},
	{0x4503, 0x00},
	{0x450a, 0x04},
	{0x4809, 0x04},
	{0x480c, 0x12},
	{0x481f, 0x30},
	{0x4833, 0x10},
	{0x4837, 0x0e},
	{0x4902, 0x01},
	{0x4d00, 0x03},
	{0x4d01, 0xc9},
	{0x4d02, 0xbc},
	{0x4d03, 0xd7},
	{0x4d04, 0xf0},
	{0x4d05, 0xa2},
	{0x5000, 0xff},
	{0x5001, 0x07},
	{0x5040, 0x39},
	{0x5041, 0x10},
	{0x5042, 0x10},
	{0x5043, 0x84},
	{0x5044, 0x62},
	{0x5180, 0x00},
	{0x5181, 0x10},
	{0x5182, 0x02},
	{0x5183, 0x0f},
	{0x5200, 0x1b},
	{0x520b, 0x07},
	{0x520c, 0x0f},
	{0x5300, 0x04},
	{0x5301, 0x0C},
	{0x5302, 0x0C},
	{0x5303, 0x0f},
	{0x5304, 0x00},
	{0x5305, 0x70},
	{0x5306, 0x00},
	{0x5307, 0x80},
	{0x5308, 0x00},
	{0x5309, 0xa5},
	{0x530a, 0x00},
	{0x530b, 0xd3},
	{0x530c, 0x00},
	{0x530d, 0xf0},
	{0x530e, 0x01},
	{0x530f, 0x10},
	{0x5310, 0x01},
	{0x5311, 0x20},
	{0x5312, 0x01},
	{0x5313, 0x20},
	{0x5314, 0x01},
	{0x5315, 0x20},
	{0x5316, 0x08},
	{0x5317, 0x08},
	{0x5318, 0x10},
	{0x5319, 0x88},
	{0x531a, 0x88},
	{0x531b, 0xa9},
	{0x531c, 0xaa},
	{0x531d, 0x0a},
	{0x5405, 0x02},
	{0x5406, 0x67},
	{0x5407, 0x01},
	{0x5408, 0x4a},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 540Mbps
 */
static const struct regval ov13855_2112x1568_30fps_regs[] = {
	{0x0103, 0x01},
	{0x0300, 0x02},
	{0x0301, 0x00},
	{0x0302, 0x5a},
	{0x0303, 0x01},
	{0x0304, 0x00},
	{0x0305, 0x01},
	{0x3022, 0x01},
	{0x3013, 0x32},
	{0x3016, 0x72},
	{0x301b, 0xF0},
	{0x301f, 0xd0},
	{0x3106, 0x15},
	{0x3107, 0x23},
	{0x3500, 0x00},
	{0x3501, 0x40},
	{0x3502, 0x00},
	{0x3508, 0x02},
	{0x3509, 0x00},
	{0x350a, 0x00},
	{0x350e, 0x00},
	{0x3510, 0x00},
	{0x3511, 0x02},
	{0x3512, 0x00},
	{0x3600, 0x2b},
	{0x3601, 0x52},
	{0x3602, 0x60},
	{0x3612, 0x05},
	{0x3613, 0xa4},
	{0x3620, 0x80},
	{0x3621, 0x08},
	{0x3622, 0x30},
	{0x3624, 0x1c},
	{0x3640, 0x08},
	{0x3641, 0x70},
	{0x3661, 0x80},
	{0x3662, 0x10},
	{0x3664, 0x73},
	{0x3665, 0xa7},
	{0x366e, 0xff},
	{0x366f, 0xf4},
	{0x3674, 0x00},
	{0x3679, 0x0c},
	{0x367f, 0x01},
	{0x3680, 0x0c},
	{0x3681, 0x60},
	{0x3682, 0x17},
	{0x3683, 0xa9},
	{0x3684, 0x9a},
	{0x3709, 0x68},
	{0x3714, 0x28},
	{0x371a, 0x3e},
	{0x3737, 0x08},
	{0x3738, 0xcc},
	{0x3739, 0x20},
	{0x373d, 0x26},
	{0x3764, 0x20},
	{0x3765, 0x20},
	{0x37a1, 0x36},
	{0x37a8, 0x3b},
	{0x37ab, 0x31},
	{0x37c2, 0x14},
	{0x37c3, 0xf1},
	{0x37c5, 0x00},
	{0x37d8, 0x03},
	{0x37d9, 0x0c},
	{0x37da, 0xc2},
	{0x37dc, 0x02},
	{0x37e0, 0x00},
	{0x37e1, 0x0a},
	{0x37e2, 0x14},
	{0x37e3, 0x08},
	{0x37e4, 0x38},
	{0x37e5, 0x03},
	{0x37e6, 0x08},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x08},
	{0x3804, 0x10},
	{0x3805, 0x9f},
	{0x3806, 0x0c},
	{0x3807, 0x4f},
	{0x3808, 0x08},
	{0x3809, 0x40},
	{0x380a, 0x06},
	{0x380b, 0x20},
	{0x380c, 0x08},
	{0x380d, 0xc4},
	{0x380e, 0x06},
	{0x380f, 0x48},
	{0x3811, 0x08},
	{0x3813, 0x02},
	{0x3814, 0x03},
	{0x3815, 0x01},
	{0x3816, 0x03},
	{0x3817, 0x01},
	{0x3820, 0xab},
	{0x3821, 0x00},
	{0x3822, 0xc2},
	{0x3823, 0x18},
	{0x3826, 0x04},
	{0x3827, 0x90},
	{0x3829, 0x07},
	{0x3832, 0x00},
	{0x3c80, 0x00},
	{0x3c87, 0x01},
	{0x3c8c, 0x19},
	{0x3c8d, 0x1c},
	{0x3c90, 0x00},
	{0x3c91, 0x00},
	{0x3c92, 0x00},
	{0x3c93, 0x00},
	{0x3c94, 0x40},
	{0x3c95, 0x54},
	{0x3c96, 0x34},
	{0x3c97, 0x04},
	{0x3c98, 0x00},
	{0x3d8c, 0x73},
	{0x3d8d, 0xc0},
	{0x3f00, 0x0b},
	{0x3f03, 0x00},
	{0x4001, 0xe0},
	{0x4008, 0x00},
	{0x4009, 0x0d},
	{0x4011, 0xf0},
	{0x4017, 0x08},
	{0x4050, 0x04},
	{0x4051, 0x0b},
	{0x4052, 0x00},
	{0x4053, 0x80},
	{0x4054, 0x00},
	{0x4055, 0x80},
	{0x4056, 0x00},
	{0x4057, 0x80},
	{0x4058, 0x00},
	{0x4059, 0x80},
	{0x405e, 0x20},
	{0x4500, 0x07},
	{0x4503, 0x00},
	{0x450a, 0x04},
	{0x4809, 0x04},
	{0x480c, 0x12},
	{0x481f, 0x30},
	{0x4833, 0x10},
	{0x4837, 0x1c},
	{0x4902, 0x01},
	{0x4d00, 0x03},
	{0x4d01, 0xc9},
	{0x4d02, 0xbc},
	{0x4d03, 0xd7},
	{0x4d04, 0xf0},
	{0x4d05, 0xa2},
	{0x5000, 0xfd},
	{0x5001, 0x01},
	{0x5040, 0x39},
	{0x5041, 0x10},
	{0x5042, 0x10},
	{0x5043, 0x84},
	{0x5044, 0x62},
	{0x5180, 0x00},
	{0x5181, 0x10},
	{0x5182, 0x02},
	{0x5183, 0x0f},
	{0x5200, 0x1b},
	{0x520b, 0x07},
	{0x520c, 0x0f},
	{0x5300, 0x04},
	{0x5301, 0x0C},
	{0x5302, 0x0C},
	{0x5303, 0x0f},
	{0x5304, 0x00},
	{0x5305, 0x70},
	{0x5306, 0x00},
	{0x5307, 0x80},
	{0x5308, 0x00},
	{0x5309, 0xa5},
	{0x530a, 0x00},
	{0x530b, 0xd3},
	{0x530c, 0x00},
	{0x530d, 0xf0},
	{0x530e, 0x01},
	{0x530f, 0x10},
	{0x5310, 0x01},
	{0x5311, 0x20},
	{0x5312, 0x01},
	{0x5313, 0x20},
	{0x5314, 0x01},
	{0x5315, 0x20},
	{0x5316, 0x08},
	{0x5317, 0x08},
	{0x5318, 0x10},
	{0x5319, 0x88},
	{0x531a, 0x88},
	{0x531b, 0xa9},
	{0x531c, 0xaa},
	{0x531d, 0x0a},
	{0x5405, 0x02},
	{0x5406, 0x67},
	{0x5407, 0x01},
	{0x5408, 0x4a},
	{0x0100, 0x01},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 1080Mbps
 */
static const struct regval ov13855_4224x3136_30fps_regs[] = {
	{0x0103, 0x01},
	{0x0300, 0x02},
	{0x0301, 0x00},
	{0x0302, 0x5a},
	{0x0303, 0x00},
	{0x0304, 0x00},
	{0x0305, 0x01},
	{0x030b, 0x06},
	{0x030c, 0x02},
	{0x030d, 0x88},
	{0x0312, 0x11},
	{0x3022, 0x01},
	{0x3012, 0x40},
	{0x3013, 0x72},
	{0x3016, 0x72},
	{0x301b, 0xF0},
	{0x301f, 0xd0},
	{0x3106, 0x15},
	{0x3107, 0x23},
	{0x3500, 0x00},
	{0x3501, 0x80},
	{0x3502, 0x00},
	{0x3508, 0x02},
	{0x3509, 0x00},
	{0x350a, 0x00},
	{0x350e, 0x00},
	{0x3510, 0x00},
	{0x3511, 0x02},
	{0x3512, 0x00},
	{0x3600, 0x2b},
	{0x3601, 0x52},
	{0x3602, 0x60},
	{0x3612, 0x05},
	{0x3613, 0xa4},
	{0x3620, 0x80},
	{0x3621, 0x10},
	{0x3622, 0x30},
	{0x3624, 0x1c},
	{0x3640, 0x10},
	{0x3641, 0x70},
	{0x3660, 0x04},
	{0x3661, 0x80},
	{0x3662, 0x12},
	{0x3664, 0x73},
	{0x3665, 0xa7},
	{0x366e, 0xff},
	{0x366f, 0xf4},
	{0x3674, 0x00},
	{0x3679, 0x0c},
	{0x367f, 0x01},
	{0x3680, 0x0c},
	{0x3681, 0x50},
	{0x3682, 0x50},
	{0x3683, 0xa9},
	{0x3684, 0xa9},
	{0x3706, 0x40},
	{0x3709, 0x5f},
	{0x3714, 0x24},
	{0x371a, 0x3e},
	{0x3737, 0x04},
	{0x3738, 0xcc},
	{0x3739, 0x12},
	{0x373d, 0x26},
	{0x3764, 0x20},
	{0x3765, 0x20},
	{0x37a1, 0x36},
	{0x37a8, 0x3b},
	{0x37ab, 0x31},
	{0x37c2, 0x04},
	{0x37c3, 0xf1},
	{0x37c5, 0x00},
	{0x37d8, 0x03},
	{0x37d9, 0x0c},
	{0x37da, 0xc2},
	{0x37dc, 0x02},
	{0x37e0, 0x00},
	{0x37e1, 0x0a},
	{0x37e2, 0x14},
	{0x37e3, 0x04},
	{0x37e4, 0x2A},
	{0x37e5, 0x03},
	{0x37e6, 0x04},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x08},
	{0x3804, 0x10},
	{0x3805, 0x9f},
	{0x3806, 0x0c},
	{0x3807, 0x57},
	{0x3808, 0x10},
	{0x3809, 0x80},
	{0x380a, 0x0c},
	{0x380b, 0x40},
	{0x380c, 0x04},
	{0x380d, 0x62},
	{0x380e, 0x0c},
	{0x380f, 0x8e},
	{0x3811, 0x10},
	{0x3813, 0x08},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3820, 0xa8},
	{0x3821, 0x00},
	{0x3822, 0xd2},
	{0x3823, 0x18},
	{0x3826, 0x11},
	{0x3827, 0x1c},
	{0x3829, 0x03},
	{0x3832, 0x00},
	{0x3c80, 0x00},
	{0x3c87, 0x01},
	{0x3c8c, 0x19},
	{0x3c8d, 0x1c},
	{0x3c90, 0x00},
	{0x3c91, 0x00},
	{0x3c92, 0x00},
	{0x3c93, 0x00},
	{0x3c94, 0x40},
	{0x3c95, 0x54},
	{0x3c96, 0x34},
	{0x3c97, 0x04},
	{0x3c98, 0x00},
	{0x3d8c, 0x73},
	{0x3d8d, 0xc0},
	{0x3f00, 0x0b},
	{0x3f03, 0x00},
	{0x4001, 0xe0},
	{0x4008, 0x00},
	{0x4009, 0x0f},
	{0x4011, 0xf0},
	{0x4017, 0x08},
	{0x4050, 0x04},
	{0x4051, 0x0b},
	{0x4052, 0x00},
	{0x4053, 0x80},
	{0x4054, 0x00},
	{0x4055, 0x80},
	{0x4056, 0x00},
	{0x4057, 0x80},
	{0x4058, 0x00},
	{0x4059, 0x80},
	{0x405e, 0x00},
	{0x4500, 0x07},
	{0x4503, 0x00},
	{0x450a, 0x04},
	{0x4800, 0x60},
	{0x4809, 0x04},
	{0x480c, 0x12},
	{0x481f, 0x30},
	{0x4833, 0x10},
	{0x4837, 0x0e},
	{0x4902, 0x01},
	{0x4d00, 0x03},
	{0x4d01, 0xc9},
	{0x4d02, 0xbc},
	{0x4d03, 0xd7},
	{0x4d04, 0xf0},
	{0x4d05, 0xa2},
	{0x5000, 0xff},
	{0x5001, 0x07},
	{0x5040, 0x39},
	{0x5041, 0x10},
	{0x5042, 0x10},
	{0x5043, 0x84},
	{0x5044, 0x62},
	{0x5180, 0x00},
	{0x5181, 0x10},
	{0x5182, 0x02},
	{0x5183, 0x0f},
	{0x5200, 0x1b},
	{0x520b, 0x07},
	{0x520c, 0x0f},
	{0x5300, 0x04},
	{0x5301, 0x0C},
	{0x5302, 0x0C},
	{0x5303, 0x0f},
	{0x5304, 0x00},
	{0x5305, 0x70},
	{0x5306, 0x00},
	{0x5307, 0x80},
	{0x5308, 0x00},
	{0x5309, 0xa5},
	{0x530a, 0x00},
	{0x530b, 0xd3},
	{0x530c, 0x00},
	{0x530d, 0xf0},
	{0x530e, 0x01},
	{0x530f, 0x10},
	{0x5310, 0x01},
	{0x5311, 0x20},
	{0x5312, 0x01},
	{0x5313, 0x20},
	{0x5314, 0x01},
	{0x5315, 0x20},
	{0x5316, 0x08},
	{0x5317, 0x08},
	{0x5318, 0x10},
	{0x5319, 0x88},
	{0x531a, 0x88},
	{0x531b, 0xa9},
	{0x531c, 0xaa},
	{0x531d, 0x0a},
	{0x5405, 0x02},
	{0x5406, 0x67},
	{0x5407, 0x01},
	{0x5408, 0x4a},
	{0x0100, 0x01},
	{0x0100, 0x00},
	{0x380c, 0x04},
	{0x380d, 0x62},
	{0x0303, 0x00},
	{0x4837, 0x0e},
	{0x0100, 0x01},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 15fps
 * mipi_datarate per lane 1080Mbps
 */
static const struct regval ov13855_4224x3136_15fps_regs[] = {
	{0x0103, 0x01},
	{0x0300, 0x02},
	{0x0301, 0x00},
	{0x0302, 0x5a},
	{0x0303, 0x00},
	{0x0304, 0x00},
	{0x0305, 0x01},
	{0x030b, 0x06},
	{0x030c, 0x02},
	{0x030d, 0x88},
	{0x0312, 0x11},
	{0x3022, 0x01},
	{0x3012, 0x40},
	{0x3013, 0x72},
	{0x3016, 0x72},
	{0x301b, 0xF0},
	{0x301f, 0xd0},
	{0x3106, 0x15},
	{0x3107, 0x23},
	{0x3500, 0x00},
	{0x3501, 0x80},
	{0x3502, 0x00},
	{0x3508, 0x02},
	{0x3509, 0x00},
	{0x350a, 0x00},
	{0x350e, 0x00},
	{0x3510, 0x00},
	{0x3511, 0x02},
	{0x3512, 0x00},
	{0x3600, 0x2b},
	{0x3601, 0x52},
	{0x3602, 0x60},
	{0x3612, 0x05},
	{0x3613, 0xa4},
	{0x3620, 0x80},
	{0x3621, 0x10},
	{0x3622, 0x30},
	{0x3624, 0x1c},
	{0x3640, 0x10},
	{0x3641, 0x70},
	{0x3660, 0x04},
	{0x3661, 0x80},
	{0x3662, 0x12},
	{0x3664, 0x73},
	{0x3665, 0xa7},
	{0x366e, 0xff},
	{0x366f, 0xf4},
	{0x3674, 0x00},
	{0x3679, 0x0c},
	{0x367f, 0x01},
	{0x3680, 0x0c},
	{0x3681, 0x50},
	{0x3682, 0x50},
	{0x3683, 0xa9},
	{0x3684, 0xa9},
	{0x3706, 0x40},
	{0x3709, 0x5f},
	{0x3714, 0x24},
	{0x371a, 0x3e},
	{0x3737, 0x04},
	{0x3738, 0xcc},
	{0x3739, 0x12},
	{0x373d, 0x26},
	{0x3764, 0x20},
	{0x3765, 0x20},
	{0x37a1, 0x36},
	{0x37a8, 0x3b},
	{0x37ab, 0x31},
	{0x37c2, 0x04},
	{0x37c3, 0xf1},
	{0x37c5, 0x00},
	{0x37d8, 0x03},
	{0x37d9, 0x0c},
	{0x37da, 0xc2},
	{0x37dc, 0x02},
	{0x37e0, 0x00},
	{0x37e1, 0x0a},
	{0x37e2, 0x14},
	{0x37e3, 0x04},
	{0x37e4, 0x2A},
	{0x37e5, 0x03},
	{0x37e6, 0x04},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x08},
	{0x3804, 0x10},
	{0x3805, 0x9f},
	{0x3806, 0x0c},
	{0x3807, 0x57},
	{0x3808, 0x10},
	{0x3809, 0x80},
	{0x380a, 0x0c},
	{0x380b, 0x40},
	{0x380c, 0x04},
	{0x380d, 0x62},
	{0x380e, 0x0c},
	{0x380f, 0x8e},
	{0x3811, 0x10},
	{0x3813, 0x08},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3820, 0xa8},
	{0x3821, 0x00},
	{0x3822, 0xd2},
	{0x3823, 0x18},
	{0x3826, 0x11},
	{0x3827, 0x1c},
	{0x3829, 0x03},
	{0x3832, 0x00},
	{0x3c80, 0x00},
	{0x3c87, 0x01},
	{0x3c8c, 0x19},
	{0x3c8d, 0x1c},
	{0x3c90, 0x00},
	{0x3c91, 0x00},
	{0x3c92, 0x00},
	{0x3c93, 0x00},
	{0x3c94, 0x40},
	{0x3c95, 0x54},
	{0x3c96, 0x34},
	{0x3c97, 0x04},
	{0x3c98, 0x00},
	{0x3d8c, 0x73},
	{0x3d8d, 0xc0},
	{0x3f00, 0x0b},
	{0x3f03, 0x00},
	{0x4001, 0xe0},
	{0x4008, 0x00},
	{0x4009, 0x0f},
	{0x4011, 0xf0},
	{0x4017, 0x08},
	{0x4050, 0x04},
	{0x4051, 0x0b},
	{0x4052, 0x00},
	{0x4053, 0x80},
	{0x4054, 0x00},
	{0x4055, 0x80},
	{0x4056, 0x00},
	{0x4057, 0x80},
	{0x4058, 0x00},
	{0x4059, 0x80},
	{0x405e, 0x00},
	{0x4500, 0x07},
	{0x4503, 0x00},
	{0x450a, 0x04},
	{0x4800, 0x60},
	{0x4809, 0x04},
	{0x480c, 0x12},
	{0x481f, 0x30},
	{0x4833, 0x10},
	{0x4837, 0x0e},
	{0x4902, 0x01},
	{0x4d00, 0x03},
	{0x4d01, 0xc9},
	{0x4d02, 0xbc},
	{0x4d03, 0xd7},
	{0x4d04, 0xf0},
	{0x4d05, 0xa2},
	{0x5000, 0xff},
	{0x5001, 0x07},
	{0x5040, 0x39},
	{0x5041, 0x10},
	{0x5042, 0x10},
	{0x5043, 0x84},
	{0x5044, 0x62},
	{0x5180, 0x00},
	{0x5181, 0x10},
	{0x5182, 0x02},
	{0x5183, 0x0f},
	{0x5200, 0x1b},
	{0x520b, 0x07},
	{0x520c, 0x0f},
	{0x5300, 0x04},
	{0x5301, 0x0C},
	{0x5302, 0x0C},
	{0x5303, 0x0f},
	{0x5304, 0x00},
	{0x5305, 0x70},
	{0x5306, 0x00},
	{0x5307, 0x80},
	{0x5308, 0x00},
	{0x5309, 0xa5},
	{0x530a, 0x00},
	{0x530b, 0xd3},
	{0x530c, 0x00},
	{0x530d, 0xf0},
	{0x530e, 0x01},
	{0x530f, 0x10},
	{0x5310, 0x01},
	{0x5311, 0x20},
	{0x5312, 0x01},
	{0x5313, 0x20},
	{0x5314, 0x01},
	{0x5315, 0x20},
	{0x5316, 0x08},
	{0x5317, 0x08},
	{0x5318, 0x10},
	{0x5319, 0x88},
	{0x531a, 0x88},
	{0x531b, 0xa9},
	{0x531c, 0xaa},
	{0x531d, 0x0a},
	{0x5405, 0x02},
	{0x5406, 0x67},
	{0x5407, 0x01},
	{0x5408, 0x4a},
	{0x0100, 0x01},
	{0x0100, 0x00},
	{0x380c, 0x08},
	{0x380d, 0xC4},
	{0x0303, 0x01},
	{0x4837, 0x1c},
	{0x0100, 0x01},
	{REG_NULL, 0x00},
};

static const struct ov13855_mode supported_modes[] = {
	{
		.width = 4224,
		.height = 3136,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0c8a,
		.hts_def = 0x0462,
		.vts_def = 0x0c8e,
		.bpp = 10,
		.reg_list = ov13855_4224x3136_30fps_regs,
		.link_freq_idx = 0,
	},
	{
		.width = 4224,
		.height = 3136,
		.max_fps = {
			.numerator = 10000,
			.denominator = 150000,
		},
		.exp_def = 0x0c8a,
		.hts_def = 0x0462,
		.vts_def = 0x0c8e,
		.bpp = 10,
		.reg_list = ov13855_4224x3136_15fps_regs,
		.link_freq_idx = 0,
	},
	{
		.width = 2112,
		.height = 1568,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0644,
		.hts_def = 0x08c4,
		.vts_def = 0x0648,
		.bpp = 10,
		.reg_list = ov13855_2112x1568_30fps_regs,
		.link_freq_idx = 1,
	},
};

static const s64 link_freq_items[] = {
	OV13855_LINK_FREQ_540MHZ,
	OV13855_LINK_FREQ_270MHZ,
};

static const char * const ov13855_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int ov13855_write_reg(struct i2c_client *client, u16 reg,
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

static int ov13855_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = ov13855_write_reg(client, regs[i].addr,
					OV13855_REG_VALUE_08BIT,
					regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int ov13855_read_reg(struct i2c_client *client, u16 reg,
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

static int ov13855_get_reso_dist(const struct ov13855_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov13855_mode *
ov13855_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = ov13855_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov13855_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov13855 *ov13855 = to_ov13855(sd);
	const struct ov13855_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;
	u32 lane_num = OV13855_LANES;

	mutex_lock(&ov13855->mutex);

	mode = ov13855_find_best_fit(fmt);
	fmt->format.code = OV13855_MEDIA_BUS_FMT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov13855->mutex);
		return -ENOTTY;
#endif
	} else {
		ov13855->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov13855->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov13855->vblank, vblank_def,
					 OV13855_VTS_MAX - mode->height,
					 1, vblank_def);
		pixel_rate = (u32)link_freq_items[mode->link_freq_idx] / mode->bpp * 2 * lane_num;

		__v4l2_ctrl_s_ctrl_int64(ov13855->pixel_rate,
					 pixel_rate);
		__v4l2_ctrl_s_ctrl(ov13855->link_freq,
				   mode->link_freq_idx);
	}

	mutex_unlock(&ov13855->mutex);

	return 0;
}

static int ov13855_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct ov13855 *ov13855 = to_ov13855(sd);
	const struct ov13855_mode *mode = ov13855->cur_mode;

	mutex_lock(&ov13855->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov13855->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = OV13855_MEDIA_BUS_FMT;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&ov13855->mutex);

	return 0;
}

static int ov13855_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = OV13855_MEDIA_BUS_FMT;

	return 0;
}

static int ov13855_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != OV13855_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int ov13855_enable_test_pattern(struct ov13855 *ov13855, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | OV13855_TEST_PATTERN_ENABLE;
	else
		val = OV13855_TEST_PATTERN_DISABLE;

	return ov13855_write_reg(ov13855->client,
				 OV13855_REG_TEST_PATTERN,
				 OV13855_REG_VALUE_08BIT,
				 val);
}

static int ov13855_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct ov13855 *ov13855 = to_ov13855(sd);
	const struct ov13855_mode *mode = ov13855->cur_mode;

	mutex_lock(&ov13855->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&ov13855->mutex);

	return 0;
}

static void ov13855_get_module_inf(struct ov13855 *ov13855,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, OV13855_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, ov13855->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, ov13855->len_name, sizeof(inf->base.lens));
}

static long ov13855_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ov13855 *ov13855 = to_ov13855(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		ov13855_get_module_inf(ov13855, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = ov13855_write_reg(ov13855->client,
				 OV13855_REG_CTRL_MODE,
				 OV13855_REG_VALUE_08BIT,
				 OV13855_MODE_STREAMING);
		else
			ret = ov13855_write_reg(ov13855->client,
				 OV13855_REG_CTRL_MODE,
				 OV13855_REG_VALUE_08BIT,
				 OV13855_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ov13855_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = ov13855_ioctl(sd, cmd, inf);
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
			ret = ov13855_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = ov13855_ioctl(sd, cmd, &stream);
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

static int __ov13855_start_stream(struct ov13855 *ov13855)
{
	int ret;

	ret = ov13855_write_array(ov13855->client, ov13855->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&ov13855->mutex);
	ret = v4l2_ctrl_handler_setup(&ov13855->ctrl_handler);
	mutex_lock(&ov13855->mutex);
	if (ret)
		return ret;

	return ov13855_write_reg(ov13855->client,
				 OV13855_REG_CTRL_MODE,
				 OV13855_REG_VALUE_08BIT,
				 OV13855_MODE_STREAMING);
}

static int __ov13855_stop_stream(struct ov13855 *ov13855)
{
	return ov13855_write_reg(ov13855->client,
				 OV13855_REG_CTRL_MODE,
				 OV13855_REG_VALUE_08BIT,
				 OV13855_MODE_SW_STANDBY);
}

static int ov13855_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov13855 *ov13855 = to_ov13855(sd);
	struct i2c_client *client = ov13855->client;
	int ret = 0;

	mutex_lock(&ov13855->mutex);
	on = !!on;
	if (on == ov13855->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __ov13855_start_stream(ov13855);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__ov13855_stop_stream(ov13855);
		pm_runtime_put(&client->dev);
	}

	ov13855->streaming = on;

unlock_and_return:
	mutex_unlock(&ov13855->mutex);

	return ret;
}

static int ov13855_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov13855 *ov13855 = to_ov13855(sd);
	struct i2c_client *client = ov13855->client;
	int ret = 0;

	mutex_lock(&ov13855->mutex);

	/* If the power state is not modified - no work to do. */
	if (ov13855->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = ov13855_write_array(ov13855->client, ov13855_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ov13855->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		ov13855->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&ov13855->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov13855_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OV13855_XVCLK_FREQ / 1000 / 1000);
}

static int __ov13855_power_on(struct ov13855 *ov13855)
{
	int ret;
	u32 delay_us;
	struct device *dev = &ov13855->client->dev;

	if (!IS_ERR(ov13855->power_gpio))
		gpiod_set_value_cansleep(ov13855->power_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR_OR_NULL(ov13855->pins_default)) {
		ret = pinctrl_select_state(ov13855->pinctrl,
					   ov13855->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(ov13855->xvclk, OV13855_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(ov13855->xvclk) != OV13855_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(ov13855->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(ov13855->reset_gpio))
		gpiod_set_value_cansleep(ov13855->reset_gpio, 0);

	ret = regulator_bulk_enable(OV13855_NUM_SUPPLIES, ov13855->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(ov13855->reset_gpio))
		gpiod_set_value_cansleep(ov13855->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(ov13855->pwdn_gpio))
		gpiod_set_value_cansleep(ov13855->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ov13855_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(ov13855->xvclk);

	return ret;
}

static void __ov13855_power_off(struct ov13855 *ov13855)
{
	int ret;
	struct device *dev = &ov13855->client->dev;

	if (!IS_ERR(ov13855->pwdn_gpio))
		gpiod_set_value_cansleep(ov13855->pwdn_gpio, 0);
	clk_disable_unprepare(ov13855->xvclk);
	if (!IS_ERR(ov13855->reset_gpio))
		gpiod_set_value_cansleep(ov13855->reset_gpio, 0);

	if (!IS_ERR_OR_NULL(ov13855->pins_sleep)) {
		ret = pinctrl_select_state(ov13855->pinctrl,
					   ov13855->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	if (!IS_ERR(ov13855->power_gpio))
		gpiod_set_value_cansleep(ov13855->power_gpio, 0);

	regulator_bulk_disable(OV13855_NUM_SUPPLIES, ov13855->supplies);
}

static int ov13855_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov13855 *ov13855 = to_ov13855(sd);

	return __ov13855_power_on(ov13855);
}

static int ov13855_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov13855 *ov13855 = to_ov13855(sd);

	__ov13855_power_off(ov13855);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov13855_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov13855 *ov13855 = to_ov13855(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov13855_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov13855->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = OV13855_MEDIA_BUS_FMT;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov13855->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int ov13855_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != OV13855_MEDIA_BUS_FMT)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;

	return 0;
}

static int ov13855_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_config *config)
{
	if (2 == OV13855_LANES) {
		config->type = V4L2_MBUS_CSI2_DPHY;
		config->flags = V4L2_MBUS_CSI2_2_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	} else if (4 == OV13855_LANES) {
		config->type = V4L2_MBUS_CSI2_DPHY;
		config->flags = V4L2_MBUS_CSI2_4_LANE |
				V4L2_MBUS_CSI2_CHANNEL_0 |
				V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	}

	return 0;
}

static int ov13855_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct ov13855 *ov13855 = to_ov13855(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = 0;
		sel->r.width = ov13855->cur_mode->width;
		sel->r.top = 0;
		sel->r.height = ov13855->cur_mode->height;
		return 0;
	}

	return -EINVAL;
}

static const struct dev_pm_ops ov13855_pm_ops = {
	SET_RUNTIME_PM_OPS(ov13855_runtime_suspend,
			   ov13855_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov13855_internal_ops = {
	.open = ov13855_open,
};
#endif

static const struct v4l2_subdev_core_ops ov13855_core_ops = {
	.s_power = ov13855_s_power,
	.ioctl = ov13855_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = ov13855_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops ov13855_video_ops = {
	.s_stream = ov13855_s_stream,
	.g_frame_interval = ov13855_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops ov13855_pad_ops = {
	.enum_mbus_code = ov13855_enum_mbus_code,
	.enum_frame_size = ov13855_enum_frame_sizes,
	.enum_frame_interval = ov13855_enum_frame_interval,
	.get_fmt = ov13855_get_fmt,
	.set_fmt = ov13855_set_fmt,
	.get_selection = ov13855_get_selection,
	.get_mbus_config = ov13855_g_mbus_config,
};

static const struct v4l2_subdev_ops ov13855_subdev_ops = {
	.core	= &ov13855_core_ops,
	.video	= &ov13855_video_ops,
	.pad	= &ov13855_pad_ops,
};

static int ov13855_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov13855 *ov13855 = container_of(ctrl->handler,
					     struct ov13855, ctrl_handler);
	struct i2c_client *client = ov13855->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov13855->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(ov13855->exposure,
					 ov13855->exposure->minimum, max,
					 ov13855->exposure->step,
					 ov13855->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = ov13855_write_reg(ov13855->client,
					OV13855_REG_EXPOSURE,
					OV13855_REG_VALUE_24BIT,
					ctrl->val << 4);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov13855_write_reg(ov13855->client,
					OV13855_REG_GAIN_H,
					OV13855_REG_VALUE_08BIT,
					(ctrl->val >> OV13855_GAIN_H_SHIFT) &
					OV13855_GAIN_H_MASK);
		ret |= ov13855_write_reg(ov13855->client,
					 OV13855_REG_GAIN_L,
					 OV13855_REG_VALUE_08BIT,
					 ctrl->val & OV13855_GAIN_L_MASK);
		break;
	case V4L2_CID_VBLANK:
		ret = ov13855_write_reg(ov13855->client,
					OV13855_REG_VTS,
					OV13855_REG_VALUE_16BIT,
					ctrl->val + ov13855->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov13855_enable_test_pattern(ov13855, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov13855_ctrl_ops = {
	.s_ctrl = ov13855_set_ctrl,
};

static int ov13855_initialize_controls(struct ov13855 *ov13855)
{
	const struct ov13855_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	u64 dst_pixel_rate = 0;
	u32 lane_num = OV13855_LANES;

	handler = &ov13855->ctrl_handler;
	mode = ov13855->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &ov13855->mutex;

	ov13855->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
			V4L2_CID_LINK_FREQ,
			1, 0, link_freq_items);

	dst_pixel_rate = (u32)link_freq_items[mode->link_freq_idx] / mode->bpp * 2 * lane_num;

	ov13855->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
			V4L2_CID_PIXEL_RATE,
			0, OV13855_PIXEL_RATE,
			1, dst_pixel_rate);

	__v4l2_ctrl_s_ctrl(ov13855->link_freq,
			   mode->link_freq_idx);

	h_blank = mode->hts_def - mode->width;
	ov13855->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov13855->hblank)
		ov13855->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ov13855->vblank = v4l2_ctrl_new_std(handler, &ov13855_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OV13855_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	ov13855->exposure = v4l2_ctrl_new_std(handler, &ov13855_ctrl_ops,
				V4L2_CID_EXPOSURE, OV13855_EXPOSURE_MIN,
				exposure_max, OV13855_EXPOSURE_STEP,
				mode->exp_def);

	ov13855->anal_gain = v4l2_ctrl_new_std(handler, &ov13855_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, OV13855_GAIN_MIN,
				OV13855_GAIN_MAX, OV13855_GAIN_STEP,
				OV13855_GAIN_DEFAULT);

	ov13855->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ov13855_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov13855_test_pattern_menu) - 1,
				0, 0, ov13855_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&ov13855->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ov13855->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov13855_check_sensor_id(struct ov13855 *ov13855,
				   struct i2c_client *client)
{
	struct device *dev = &ov13855->client->dev;
	u32 id = 0;
	int ret;

	ret = ov13855_read_reg(client, OV13855_REG_CHIP_ID,
			       OV13855_REG_VALUE_24BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	ret = ov13855_read_reg(client, OV13855_CHIP_REVISION_REG,
			       OV13855_REG_VALUE_08BIT, &id);
	if (ret) {
		dev_err(dev, "Read chip revision register error\n");
		return ret;
	}

	dev_info(dev, "Detected OV%06x sensor, REVISION 0x%x\n", CHIP_ID, id);

	return 0;
}

static int ov13855_configure_regulators(struct ov13855 *ov13855)
{
	unsigned int i;

	for (i = 0; i < OV13855_NUM_SUPPLIES; i++)
		ov13855->supplies[i].supply = ov13855_supply_names[i];

	return devm_regulator_bulk_get(&ov13855->client->dev,
				       OV13855_NUM_SUPPLIES,
				       ov13855->supplies);
}

static int ov13855_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct ov13855 *ov13855;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	ov13855 = devm_kzalloc(dev, sizeof(*ov13855), GFP_KERNEL);
	if (!ov13855)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &ov13855->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &ov13855->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &ov13855->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &ov13855->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ov13855->client = client;
	ov13855->cur_mode = &supported_modes[0];

	ov13855->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov13855->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	ov13855->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(ov13855->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");

	ov13855->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov13855->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	ov13855->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov13855->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = ov13855_configure_regulators(ov13855);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	ov13855->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(ov13855->pinctrl)) {
		ov13855->pins_default =
			pinctrl_lookup_state(ov13855->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(ov13855->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		ov13855->pins_sleep =
			pinctrl_lookup_state(ov13855->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(ov13855->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&ov13855->mutex);

	sd = &ov13855->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov13855_subdev_ops);
	ret = ov13855_initialize_controls(ov13855);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov13855_power_on(ov13855);
	if (ret)
		goto err_free_handler;

	ret = ov13855_check_sensor_id(ov13855, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov13855_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov13855->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &ov13855->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(ov13855->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 ov13855->module_index, facing,
		 OV13855_NAME, dev_name(sd->dev));
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
	__ov13855_power_off(ov13855);
err_free_handler:
	v4l2_ctrl_handler_free(&ov13855->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov13855->mutex);

	return ret;
}

static int ov13855_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov13855 *ov13855 = to_ov13855(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov13855->ctrl_handler);
	mutex_destroy(&ov13855->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov13855_power_off(ov13855);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov13855_of_match[] = {
	{ .compatible = "ovti,ov13855" },
	{},
};
MODULE_DEVICE_TABLE(of, ov13855_of_match);
#endif

static const struct i2c_device_id ov13855_match_id[] = {
	{ "ovti,ov13855", 0 },
	{},
};

static struct i2c_driver ov13855_i2c_driver = {
	.driver = {
		.name = OV13855_NAME,
		.pm = &ov13855_pm_ops,
		.of_match_table = of_match_ptr(ov13855_of_match),
	},
	.probe		= &ov13855_probe,
	.remove		= &ov13855_remove,
	.id_table	= ov13855_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov13855_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov13855_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision ov13855 sensor driver");
MODULE_LICENSE("GPL v2");
