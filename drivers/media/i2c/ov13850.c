// SPDX-License-Identifier: GPL-2.0
/*
 * ov13850 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 add enum_frame_interval function.
 * V0.0X01.0X04 add quick stream on/off
 * V0.0X01.0X05 add function g_mbus_config
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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x05)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define OV13850_LINK_FREQ_300MHZ	300000000
/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define OV13850_PIXEL_RATE		(OV13850_LINK_FREQ_300MHZ * 2 * 2 / 10)
#define OV13850_XVCLK_FREQ		24000000

#define CHIP_ID				0x00d850
#define OV13850_REG_CHIP_ID		0x300a

#define OV13850_REG_CTRL_MODE		0x0100
#define OV13850_MODE_SW_STANDBY		0x0
#define OV13850_MODE_STREAMING		BIT(0)

#define OV13850_REG_EXPOSURE		0x3500
#define	OV13850_EXPOSURE_MIN		4
#define	OV13850_EXPOSURE_STEP		1
#define OV13850_VTS_MAX			0x7fff

#define OV13850_REG_GAIN_H		0x350a
#define OV13850_REG_GAIN_L		0x350b
#define OV13850_GAIN_H_MASK		0x07
#define OV13850_GAIN_H_SHIFT		8
#define OV13850_GAIN_L_MASK		0xff
#define OV13850_GAIN_MIN		0x10
#define OV13850_GAIN_MAX		0xf8
#define OV13850_GAIN_STEP		1
#define OV13850_GAIN_DEFAULT		0x10

#define OV13850_REG_TEST_PATTERN	0x5e00
#define	OV13850_TEST_PATTERN_ENABLE	0x80
#define	OV13850_TEST_PATTERN_DISABLE	0x0

#define OV13850_REG_VTS			0x380e

#define REG_NULL			0xFFFF

#define OV13850_REG_VALUE_08BIT		1
#define OV13850_REG_VALUE_16BIT		2
#define OV13850_REG_VALUE_24BIT		3

#define OV13850_LANES			2
#define OV13850_BITS_PER_SAMPLE		10

#define OV13850_CHIP_REVISION_REG	0x302A
#define OV13850_R1A			0xb1
#define OV13850_R2A			0xb2

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define OV13850_NAME			"ov13850"

static const struct regval *ov13850_global_regs;

static const char * const ov13850_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV13850_NUM_SUPPLIES ARRAY_SIZE(ov13850_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct ov13850_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct ov13850 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV13850_NUM_SUPPLIES];

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
	const struct ov13850_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_ov13850(sd) container_of(sd, struct ov13850, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval ov13850_global_regs_r1a[] = {
	{0x0103, 0x01},
	{0x0300, 0x00},
	{0x0301, 0x00},
	{0x0302, 0x32},
	{0x0303, 0x01},
	{0x030a, 0x00},
	{0x300f, 0x11},
	{0x3010, 0x01},
	{0x3011, 0x76},
	{0x3012, 0x21},
	{0x3013, 0x12},
	{0x3014, 0x11},
	{0x3015, 0xc0},
	{0x301f, 0x03},
	{0x3106, 0x00},
	{0x3210, 0x47},
	{0x3500, 0x00},
	{0x3501, 0x60},
	{0x3502, 0x00},
	{0x3506, 0x00},
	{0x3507, 0x02},
	{0x3508, 0x00},
	{0x350a, 0x00},
	{0x350b, 0x80},
	{0x350e, 0x00},
	{0x350f, 0x10},
	{0x3600, 0x40},
	{0x3601, 0xfc},
	{0x3602, 0x02},
	{0x3603, 0x48},
	{0x3604, 0xa5},
	{0x3605, 0x9f},
	{0x3607, 0x00},
	{0x360a, 0x40},
	{0x360b, 0x91},
	{0x360c, 0x49},
	{0x360f, 0x8a},
	{0x3611, 0x10},
	{0x3612, 0x27},
	{0x3613, 0x33},
	{0x3615, 0x08},
	{0x3641, 0x02},
	{0x3660, 0x82},
	{0x3668, 0x54},
	{0x3669, 0x40},
	{0x3667, 0xa0},
	{0x3702, 0x40},
	{0x3703, 0x44},
	{0x3704, 0x2c},
	{0x3705, 0x24},
	{0x3706, 0x50},
	{0x3707, 0x44},
	{0x3708, 0x3c},
	{0x3709, 0x1f},
	{0x370a, 0x26},
	{0x370b, 0x3c},
	{0x3720, 0x66},
	{0x3722, 0x84},
	{0x3728, 0x40},
	{0x372a, 0x00},
	{0x372f, 0x90},
	{0x3710, 0x28},
	{0x3716, 0x03},
	{0x3718, 0x10},
	{0x3719, 0x08},
	{0x371c, 0xfc},
	{0x3760, 0x13},
	{0x3761, 0x34},
	{0x3767, 0x24},
	{0x3768, 0x06},
	{0x3769, 0x45},
	{0x376c, 0x23},
	{0x3d84, 0x00},
	{0x3d85, 0x17},
	{0x3d8c, 0x73},
	{0x3d8d, 0xbf},
	{0x3800, 0x00},
	{0x3801, 0x08},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x10},
	{0x3805, 0x97},
	{0x3806, 0x0c},
	{0x3807, 0x4b},
	{0x3808, 0x08},
	{0x3809, 0x40},
	{0x380a, 0x06},
	{0x380b, 0x20},
	{0x380c, 0x12},
	{0x380d, 0xc0},
	{0x380e, 0x06},
	{0x380f, 0x80},
	{0x3810, 0x00},
	{0x3811, 0x04},
	{0x3812, 0x00},
	{0x3813, 0x02},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3820, 0x02},
	{0x3821, 0x05},
	{0x3834, 0x00},
	{0x3835, 0x1c},
	{0x3836, 0x08},
	{0x3837, 0x02},
	{0x4000, 0xf1},
	{0x4001, 0x00},
	{0x400b, 0x0c},
	{0x4011, 0x00},
	{0x401a, 0x00},
	{0x401b, 0x00},
	{0x401c, 0x00},
	{0x401d, 0x00},
	{0x4020, 0x00},
	{0x4021, 0xE4},
	{0x4022, 0x07},
	{0x4023, 0x5F},
	{0x4024, 0x08},
	{0x4025, 0x44},
	{0x4026, 0x08},
	{0x4027, 0x47},
	{0x4028, 0x00},
	{0x4029, 0x02},
	{0x402a, 0x04},
	{0x402b, 0x08},
	{0x402c, 0x02},
	{0x402d, 0x02},
	{0x402e, 0x0c},
	{0x402f, 0x08},
	{0x403d, 0x2c},
	{0x403f, 0x7f},
	{0x4500, 0x82},
	{0x4501, 0x38},
	{0x4601, 0x04},
	{0x4602, 0x22},
	{0x4603, 0x01},
	{0x4800, 0x24}, //MIPI CLK control
	{0x4837, 0x1b},
	{0x4d00, 0x04},
	{0x4d01, 0x42},
	{0x4d02, 0xd1},
	{0x4d03, 0x90},
	{0x4d04, 0x66},
	{0x4d05, 0x65},
	{0x5000, 0x0e},
	{0x5001, 0x01},
	{0x5002, 0x07},
	{0x5013, 0x40},
	{0x501c, 0x00},
	{0x501d, 0x10},
	{0x5242, 0x00},
	{0x5243, 0xb8},
	{0x5244, 0x00},
	{0x5245, 0xf9},
	{0x5246, 0x00},
	{0x5247, 0xf6},
	{0x5248, 0x00},
	{0x5249, 0xa6},
	{0x5300, 0xfc},
	{0x5301, 0xdf},
	{0x5302, 0x3f},
	{0x5303, 0x08},
	{0x5304, 0x0c},
	{0x5305, 0x10},
	{0x5306, 0x20},
	{0x5307, 0x40},
	{0x5308, 0x08},
	{0x5309, 0x08},
	{0x530a, 0x02},
	{0x530b, 0x01},
	{0x530c, 0x01},
	{0x530d, 0x0c},
	{0x530e, 0x02},
	{0x530f, 0x01},
	{0x5310, 0x01},
	{0x5400, 0x00},
	{0x5401, 0x61},
	{0x5402, 0x00},
	{0x5403, 0x00},
	{0x5404, 0x00},
	{0x5405, 0x40},
	{0x540c, 0x05},
	{0x5b00, 0x00},
	{0x5b01, 0x00},
	{0x5b02, 0x01},
	{0x5b03, 0xff},
	{0x5b04, 0x02},
	{0x5b05, 0x6c},
	{0x5b09, 0x02},
	{0x5e00, 0x00},
	{0x5e10, 0x1c},
	{0x0102, 0x01}, //Fast standby enable
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 */
static const struct regval ov13850_global_regs_r2a[] = {
	{0x0300, 0x01},
	{0x0301, 0x00},
	{0x0302, 0x28},
	{0x0303, 0x00},
	{0x030a, 0x00},
	{0x300f, 0x11},
	{0x3010, 0x01},
	{0x3011, 0x76},
	{0x3012, 0x21},
	{0x3013, 0x12},
	{0x3014, 0x11},
	{0x301f, 0x03},
	{0x3106, 0x00},
	{0x3210, 0x47},
	{0x3500, 0x00},
	{0x3501, 0x60},
	{0x3502, 0x00},
	{0x3506, 0x00},
	{0x3507, 0x02},
	{0x3508, 0x00},
	{0x350a, 0x00},
	{0x350b, 0x80},
	{0x350e, 0x00},
	{0x350f, 0x10},
	{0x351a, 0x00},
	{0x351b, 0x10},
	{0x351c, 0x00},
	{0x351d, 0x20},
	{0x351e, 0x00},
	{0x351f, 0x40},
	{0x3520, 0x00},
	{0x3521, 0x80},
	{0x3600, 0xc0},
	{0x3601, 0xfc},
	{0x3602, 0x02},
	{0x3603, 0x78},
	{0x3604, 0xb1},
	{0x3605, 0xb5},
	{0x3606, 0x73},
	{0x3607, 0x07},
	{0x3609, 0x40},
	{0x360a, 0x30},
	{0x360b, 0x91},
	{0x360c, 0x09},
	{0x360f, 0x02},
	{0x3611, 0x10},
	{0x3612, 0x27},
	{0x3613, 0x33},
	{0x3615, 0x0c},
	{0x3616, 0x0e},
	{0x3641, 0x02},
	{0x3660, 0x82},
	{0x3668, 0x54},
	{0x3669, 0x00},
	{0x366a, 0x3f},
	{0x3667, 0xa0},
	{0x3702, 0x40},
	{0x3703, 0x44},
	{0x3704, 0x2c},
	{0x3705, 0x01},
	{0x3706, 0x15},
	{0x3707, 0x44},
	{0x3708, 0x3c},
	{0x3709, 0x1f},
	{0x370a, 0x27},
	{0x370b, 0x3c},
	{0x3720, 0x55},
	{0x3722, 0x84},
	{0x3728, 0x40},
	{0x372a, 0x00},
	{0x372b, 0x02},
	{0x372e, 0x22},
	{0x372f, 0x90},
	{0x3730, 0x00},
	{0x3731, 0x00},
	{0x3732, 0x00},
	{0x3733, 0x00},
	{0x3710, 0x28},
	{0x3716, 0x03},
	{0x3718, 0x10},
	{0x3719, 0x0c},
	{0x371a, 0x08},
	{0x371c, 0xfc},
	{0x3748, 0x00},
	{0x3760, 0x13},
	{0x3761, 0x33},
	{0x3762, 0x86},
	{0x3763, 0x16},
	{0x3767, 0x24},
	{0x3768, 0x06},
	{0x3769, 0x45},
	{0x376c, 0x23},
	{0x376f, 0x80},
	{0x3773, 0x06},
	{0x3d84, 0x00},
	{0x3d85, 0x17},
	{0x3d8c, 0x73},
	{0x3d8d, 0xbf},
	{0x3800, 0x00},
	{0x3801, 0x08},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x10},
	{0x3805, 0x97},
	{0x3806, 0x0c},
	{0x3807, 0x4b},
	{0x3808, 0x08},
	{0x3809, 0x40},
	{0x380a, 0x06},
	{0x380b, 0x20},
	{0x380c, 0x12},
	{0x380d, 0xc0},
	{0x380e, 0x06},
	{0x380f, 0x80},
	{0x3810, 0x00},
	{0x3811, 0x04},
	{0x3812, 0x00},
	{0x3813, 0x02},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3820, 0x02},
	{0x3821, 0x06},
	{0x3823, 0x00},
	{0x3826, 0x00},
	{0x3827, 0x02},
	{0x3834, 0x00},
	{0x3835, 0x1c},
	{0x3836, 0x08},
	{0x3837, 0x02},
	{0x4000, 0xf1},
	{0x4001, 0x00},
	{0x4006, 0x04},
	{0x4007, 0x04},
	{0x400b, 0x0c},
	{0x4011, 0x00},
	{0x401a, 0x00},
	{0x401b, 0x00},
	{0x401c, 0x00},
	{0x401d, 0x00},
	{0x4020, 0x00},
	{0x4021, 0xe4},
	{0x4022, 0x04},
	{0x4023, 0xd7},
	{0x4024, 0x05},
	{0x4025, 0xbc},
	{0x4026, 0x05},
	{0x4027, 0xbf},
	{0x4028, 0x00},
	{0x4029, 0x02},
	{0x402a, 0x04},
	{0x402b, 0x08},
	{0x402c, 0x02},
	{0x402d, 0x02},
	{0x402e, 0x0c},
	{0x402f, 0x08},
	{0x403d, 0x2c},
	{0x403f, 0x7f},
	{0x4041, 0x07},
	{0x4500, 0x82},
	{0x4501, 0x3c},
	{0x458b, 0x00},
	{0x459c, 0x00},
	{0x459d, 0x00},
	{0x459e, 0x00},
	{0x4601, 0x83},
	{0x4602, 0x22},
	{0x4603, 0x01},
	{0x4800, 0x24}, //MIPI CLK control
	{0x4837, 0x19},
	{0x4d00, 0x04},
	{0x4d01, 0x42},
	{0x4d02, 0xd1},
	{0x4d03, 0x90},
	{0x4d04, 0x66},
	{0x4d05, 0x65},
	{0x4d0b, 0x00},
	{0x5000, 0x0e},
	{0x5001, 0x01},
	{0x5002, 0x07},
	{0x5013, 0x40},
	{0x501c, 0x00},
	{0x501d, 0x10},
	{0x510f, 0xfc},
	{0x5110, 0xf0},
	{0x5111, 0x10},
	{0x536d, 0x02},
	{0x536e, 0x67},
	{0x536f, 0x01},
	{0x5370, 0x4c},
	{0x5400, 0x00},
	{0x5400, 0x00},
	{0x5401, 0x61},
	{0x5402, 0x00},
	{0x5403, 0x00},
	{0x5404, 0x00},
	{0x5405, 0x40},
	{0x540c, 0x05},
	{0x5501, 0x00},
	{0x5b00, 0x00},
	{0x5b01, 0x00},
	{0x5b02, 0x01},
	{0x5b03, 0xff},
	{0x5b04, 0x02},
	{0x5b05, 0x6c},
	{0x5b09, 0x02},
	{0x5e00, 0x00},
	{0x5e10, 0x1c},
	{0x0102, 0x01}, //Fast standby enable
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 600Mbps
 */
static const struct regval ov13850_2112x1568_regs[] = {
	{0x3612, 0x27},
	{0x370a, 0x26},
	{0x372a, 0x00},
	{0x372f, 0x90},
	{0x3801, 0x08},
	{0x3805, 0x97},
	{0x3807, 0x4b},
	{0x3808, 0x08},
	{0x3809, 0x40},
	{0x380a, 0x06},
	{0x380b, 0x20},
	{0x380c, 0x12},
	{0x380d, 0xc0},
	{0x380e, 0x06},
	{0x380f, 0x80},
	{0x3813, 0x02},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3820, 0x02},
	{0x3821, 0x05},
	{0x3836, 0x08},
	{0x3837, 0x02},
	{0x4601, 0x04},
	{0x4603, 0x00},
	{0x4020, 0x00},
	{0x4021, 0xE4},
	{0x4022, 0x07},
	{0x4023, 0x5F},
	{0x4024, 0x08},
	{0x4025, 0x44},
	{0x4026, 0x08},
	{0x4027, 0x47},
	{0x4603, 0x01},
	{0x5401, 0x61},
	{0x5405, 0x40},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 7fps
 * mipi_datarate per lane 600Mbps
 */
static const struct regval ov13850_4224x3136_regs[] = {
	{0x3612, 0x2f},
	{0x370a, 0x24},
	{0x372a, 0x04},
	{0x372f, 0xa0},
	{0x3801, 0x0C},
	{0x3805, 0x93},
	{0x3807, 0x4B},
	{0x3808, 0x10},
	{0x3809, 0x80},
	{0x380a, 0x0c},
	{0x380b, 0x40},
	{0x380e, 0x0d},
	{0x380f, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3820, 0x00},
	{0x3821, 0x04},
	{0x3836, 0x04},
	{0x3837, 0x01},
	{0x4601, 0x87},
	{0x4603, 0x01},
	{0x4020, 0x02},
	{0x4021, 0x4C},
	{0x4022, 0x0E},
	{0x4023, 0x37},
	{0x4024, 0x0F},
	{0x4025, 0x1C},
	{0x4026, 0x0F},
	{0x4027, 0x1F},
	{0x4603, 0x00},
	{0x5401, 0x71},
	{0x5405, 0x80},
	{REG_NULL, 0x00},
};

static const struct ov13850_mode supported_modes[] = {
	{
		.width = 2112,
		.height = 1568,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0600,
		.hts_def = 0x12c0,
		.vts_def = 0x0680,
		.reg_list = ov13850_2112x1568_regs,
	},{
		.width = 4224,
		.height = 3136,
		.max_fps = {
			.numerator = 20000,
			.denominator = 150000,
		},
		.exp_def = 0x0600,
		.hts_def = 0x12c0,
		.vts_def = 0x0d00,
		.reg_list = ov13850_4224x3136_regs,
	},
};

static const s64 link_freq_menu_items[] = {
	OV13850_LINK_FREQ_300MHZ
};

static const char * const ov13850_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int ov13850_write_reg(struct i2c_client *client, u16 reg,
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

static int ov13850_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = ov13850_write_reg(client, regs[i].addr,
					OV13850_REG_VALUE_08BIT,
					regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int ov13850_read_reg(struct i2c_client *client, u16 reg,
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

static int ov13850_get_reso_dist(const struct ov13850_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov13850_mode *
ov13850_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = ov13850_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov13850_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov13850 *ov13850 = to_ov13850(sd);
	const struct ov13850_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&ov13850->mutex);

	mode = ov13850_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov13850->mutex);
		return -ENOTTY;
#endif
	} else {
		ov13850->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov13850->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov13850->vblank, vblank_def,
					 OV13850_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&ov13850->mutex);

	return 0;
}

static int ov13850_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct ov13850 *ov13850 = to_ov13850(sd);
	const struct ov13850_mode *mode = ov13850->cur_mode;

	mutex_lock(&ov13850->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov13850->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&ov13850->mutex);

	return 0;
}

static int ov13850_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov13850_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int ov13850_enable_test_pattern(struct ov13850 *ov13850, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | OV13850_TEST_PATTERN_ENABLE;
	else
		val = OV13850_TEST_PATTERN_DISABLE;

	return ov13850_write_reg(ov13850->client,
				 OV13850_REG_TEST_PATTERN,
				 OV13850_REG_VALUE_08BIT,
				 val);
}

static int ov13850_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct ov13850 *ov13850 = to_ov13850(sd);
	const struct ov13850_mode *mode = ov13850->cur_mode;

	mutex_lock(&ov13850->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&ov13850->mutex);

	return 0;
}

static void ov13850_get_module_inf(struct ov13850 *ov13850,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, OV13850_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, ov13850->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, ov13850->len_name, sizeof(inf->base.lens));
}

static long ov13850_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ov13850 *ov13850 = to_ov13850(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		ov13850_get_module_inf(ov13850, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = ov13850_write_reg(ov13850->client,
				 OV13850_REG_CTRL_MODE,
				 OV13850_REG_VALUE_08BIT,
				 OV13850_MODE_STREAMING);
		else
			ret = ov13850_write_reg(ov13850->client,
				 OV13850_REG_CTRL_MODE,
				 OV13850_REG_VALUE_08BIT,
				 OV13850_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ov13850_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = ov13850_ioctl(sd, cmd, inf);
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
			ret = ov13850_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = ov13850_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __ov13850_start_stream(struct ov13850 *ov13850)
{
	int ret;

	ret = ov13850_write_array(ov13850->client, ov13850->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&ov13850->mutex);
	ret = v4l2_ctrl_handler_setup(&ov13850->ctrl_handler);
	mutex_lock(&ov13850->mutex);
	if (ret)
		return ret;

	return ov13850_write_reg(ov13850->client,
				 OV13850_REG_CTRL_MODE,
				 OV13850_REG_VALUE_08BIT,
				 OV13850_MODE_STREAMING);
}

static int __ov13850_stop_stream(struct ov13850 *ov13850)
{
	return ov13850_write_reg(ov13850->client,
				 OV13850_REG_CTRL_MODE,
				 OV13850_REG_VALUE_08BIT,
				 OV13850_MODE_SW_STANDBY);
}

static int ov13850_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov13850 *ov13850 = to_ov13850(sd);
	struct i2c_client *client = ov13850->client;
	int ret = 0;

	mutex_lock(&ov13850->mutex);
	on = !!on;
	if (on == ov13850->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __ov13850_start_stream(ov13850);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__ov13850_stop_stream(ov13850);
		pm_runtime_put(&client->dev);
	}

	ov13850->streaming = on;

unlock_and_return:
	mutex_unlock(&ov13850->mutex);

	return ret;
}

static int ov13850_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov13850 *ov13850 = to_ov13850(sd);
	struct i2c_client *client = ov13850->client;
	int ret = 0;

	mutex_lock(&ov13850->mutex);

	/* If the power state is not modified - no work to do. */
	if (ov13850->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = ov13850_write_array(ov13850->client, ov13850_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ov13850->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		ov13850->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&ov13850->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov13850_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OV13850_XVCLK_FREQ / 1000 / 1000);
}

static int __ov13850_power_on(struct ov13850 *ov13850)
{
	int ret;
	u32 delay_us;
	struct device *dev = &ov13850->client->dev;

	if (!IS_ERR(ov13850->power_gpio))
		gpiod_set_value_cansleep(ov13850->power_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR_OR_NULL(ov13850->pins_default)) {
		ret = pinctrl_select_state(ov13850->pinctrl,
					   ov13850->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(ov13850->xvclk, OV13850_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(ov13850->xvclk) != OV13850_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(ov13850->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(ov13850->reset_gpio))
		gpiod_set_value_cansleep(ov13850->reset_gpio, 0);

	ret = regulator_bulk_enable(OV13850_NUM_SUPPLIES, ov13850->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(ov13850->reset_gpio))
		gpiod_set_value_cansleep(ov13850->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(ov13850->pwdn_gpio))
		gpiod_set_value_cansleep(ov13850->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ov13850_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(ov13850->xvclk);

	return ret;
}

static void __ov13850_power_off(struct ov13850 *ov13850)
{
	int ret;
	struct device *dev = &ov13850->client->dev;

	if (!IS_ERR(ov13850->pwdn_gpio))
		gpiod_set_value_cansleep(ov13850->pwdn_gpio, 0);
	clk_disable_unprepare(ov13850->xvclk);
	if (!IS_ERR(ov13850->reset_gpio))
		gpiod_set_value_cansleep(ov13850->reset_gpio, 0);

	if (!IS_ERR_OR_NULL(ov13850->pins_sleep)) {
		ret = pinctrl_select_state(ov13850->pinctrl,
					   ov13850->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	if (!IS_ERR(ov13850->power_gpio))
		gpiod_set_value_cansleep(ov13850->power_gpio, 0);

	regulator_bulk_disable(OV13850_NUM_SUPPLIES, ov13850->supplies);
}

static int ov13850_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov13850 *ov13850 = to_ov13850(sd);

	return __ov13850_power_on(ov13850);
}

static int ov13850_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov13850 *ov13850 = to_ov13850(sd);

	__ov13850_power_off(ov13850);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov13850_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov13850 *ov13850 = to_ov13850(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov13850_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov13850->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov13850->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int ov13850_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int ov13850_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	u32 val = 0;

	val = 1 << (OV13850_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops ov13850_pm_ops = {
	SET_RUNTIME_PM_OPS(ov13850_runtime_suspend,
			   ov13850_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov13850_internal_ops = {
	.open = ov13850_open,
};
#endif

static const struct v4l2_subdev_core_ops ov13850_core_ops = {
	.s_power = ov13850_s_power,
	.ioctl = ov13850_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = ov13850_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops ov13850_video_ops = {
	.s_stream = ov13850_s_stream,
	.g_frame_interval = ov13850_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops ov13850_pad_ops = {
	.enum_mbus_code = ov13850_enum_mbus_code,
	.enum_frame_size = ov13850_enum_frame_sizes,
	.enum_frame_interval = ov13850_enum_frame_interval,
	.get_fmt = ov13850_get_fmt,
	.set_fmt = ov13850_set_fmt,
	.get_mbus_config = ov13850_g_mbus_config,
};

static const struct v4l2_subdev_ops ov13850_subdev_ops = {
	.core	= &ov13850_core_ops,
	.video	= &ov13850_video_ops,
	.pad	= &ov13850_pad_ops,
};

static int ov13850_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov13850 *ov13850 = container_of(ctrl->handler,
					     struct ov13850, ctrl_handler);
	struct i2c_client *client = ov13850->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov13850->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(ov13850->exposure,
					 ov13850->exposure->minimum, max,
					 ov13850->exposure->step,
					 ov13850->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = ov13850_write_reg(ov13850->client,
					OV13850_REG_EXPOSURE,
					OV13850_REG_VALUE_24BIT,
					ctrl->val << 4);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov13850_write_reg(ov13850->client,
					OV13850_REG_GAIN_H,
					OV13850_REG_VALUE_08BIT,
					(ctrl->val >> OV13850_GAIN_H_SHIFT) &
					OV13850_GAIN_H_MASK);
		ret |= ov13850_write_reg(ov13850->client,
					 OV13850_REG_GAIN_L,
					 OV13850_REG_VALUE_08BIT,
					 ctrl->val & OV13850_GAIN_L_MASK);
		break;
	case V4L2_CID_VBLANK:
		ret = ov13850_write_reg(ov13850->client,
					OV13850_REG_VTS,
					OV13850_REG_VALUE_16BIT,
					ctrl->val + ov13850->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov13850_enable_test_pattern(ov13850, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov13850_ctrl_ops = {
	.s_ctrl = ov13850_set_ctrl,
};

static int ov13850_initialize_controls(struct ov13850 *ov13850)
{
	const struct ov13850_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &ov13850->ctrl_handler;
	mode = ov13850->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &ov13850->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, OV13850_PIXEL_RATE, 1, OV13850_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	ov13850->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov13850->hblank)
		ov13850->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ov13850->vblank = v4l2_ctrl_new_std(handler, &ov13850_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OV13850_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	ov13850->exposure = v4l2_ctrl_new_std(handler, &ov13850_ctrl_ops,
				V4L2_CID_EXPOSURE, OV13850_EXPOSURE_MIN,
				exposure_max, OV13850_EXPOSURE_STEP,
				mode->exp_def);

	ov13850->anal_gain = v4l2_ctrl_new_std(handler, &ov13850_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, OV13850_GAIN_MIN,
				OV13850_GAIN_MAX, OV13850_GAIN_STEP,
				OV13850_GAIN_DEFAULT);

	ov13850->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ov13850_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov13850_test_pattern_menu) - 1,
				0, 0, ov13850_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&ov13850->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ov13850->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov13850_check_sensor_id(struct ov13850 *ov13850,
				   struct i2c_client *client)
{
	struct device *dev = &ov13850->client->dev;
	u32 id = 0;
	int ret;

	ret = ov13850_read_reg(client, OV13850_REG_CHIP_ID,
			       OV13850_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	ret = ov13850_read_reg(client, OV13850_CHIP_REVISION_REG,
			       OV13850_REG_VALUE_08BIT, &id);
	if (ret) {
		dev_err(dev, "Read chip revision register error\n");
		return ret;
	}

	if (id == OV13850_R2A)
		ov13850_global_regs = ov13850_global_regs_r2a;
	else
		ov13850_global_regs = ov13850_global_regs_r1a;
	dev_info(dev, "Detected OV%06x sensor, REVISION 0x%x\n", CHIP_ID, id);

	return 0;
}

static int ov13850_configure_regulators(struct ov13850 *ov13850)
{
	unsigned int i;

	for (i = 0; i < OV13850_NUM_SUPPLIES; i++)
		ov13850->supplies[i].supply = ov13850_supply_names[i];

	return devm_regulator_bulk_get(&ov13850->client->dev,
				       OV13850_NUM_SUPPLIES,
				       ov13850->supplies);
}

static int ov13850_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct ov13850 *ov13850;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	ov13850 = devm_kzalloc(dev, sizeof(*ov13850), GFP_KERNEL);
	if (!ov13850)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &ov13850->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &ov13850->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &ov13850->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &ov13850->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ov13850->client = client;
	ov13850->cur_mode = &supported_modes[0];

	ov13850->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov13850->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	ov13850->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(ov13850->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");

	ov13850->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov13850->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	ov13850->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov13850->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = ov13850_configure_regulators(ov13850);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	ov13850->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(ov13850->pinctrl)) {
		ov13850->pins_default =
			pinctrl_lookup_state(ov13850->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(ov13850->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		ov13850->pins_sleep =
			pinctrl_lookup_state(ov13850->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(ov13850->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&ov13850->mutex);

	sd = &ov13850->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov13850_subdev_ops);
	ret = ov13850_initialize_controls(ov13850);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov13850_power_on(ov13850);
	if (ret)
		goto err_free_handler;

	ret = ov13850_check_sensor_id(ov13850, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov13850_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov13850->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &ov13850->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(ov13850->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 ov13850->module_index, facing,
		 OV13850_NAME, dev_name(sd->dev));
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
	__ov13850_power_off(ov13850);
err_free_handler:
	v4l2_ctrl_handler_free(&ov13850->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov13850->mutex);

	return ret;
}

static int ov13850_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov13850 *ov13850 = to_ov13850(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov13850->ctrl_handler);
	mutex_destroy(&ov13850->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov13850_power_off(ov13850);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov13850_of_match[] = {
	{ .compatible = "ovti,ov13850" },
	{},
};
MODULE_DEVICE_TABLE(of, ov13850_of_match);
#endif

static const struct i2c_device_id ov13850_match_id[] = {
	{ "ovti,ov13850", 0 },
	{ },
};

static struct i2c_driver ov13850_i2c_driver = {
	.driver = {
		.name = OV13850_NAME,
		.pm = &ov13850_pm_ops,
		.of_match_table = of_match_ptr(ov13850_of_match),
	},
	.probe		= &ov13850_probe,
	.remove		= &ov13850_remove,
	.id_table	= ov13850_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov13850_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov13850_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision ov13850 sensor driver");
MODULE_LICENSE("GPL v2");
