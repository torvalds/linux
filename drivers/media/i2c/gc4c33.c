// SPDX-License-Identifier: GPL-2.0
/*
 * gc4c33 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 fix gain range.
 * V0.0X01.0X04 add enum_frame_interval function.
 * V0.0X01.0X05 fix gain reg, add otp and dpc.
 * V0.0X01.0X06 add set dpc cfg.
 * V0.0X01.0X07 support enum sensor fmt
 * V0.0X01.0X08 support mirror and flip
 * V0.0X01.0X09 add quick stream on/off
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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x09)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define GC4C33_LANES			2
#define GC4C33_BITS_PER_SAMPLE		10
#define GC4C33_LINK_FREQ		315000000   //2560*1440
//#define GC4C33_LINK_FREQ		157500000   //1920*1080
//#define GC4C33_LINK_FREQ		261000000   //1280*720
/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define GC4C33_PIXEL_RATE		(GC4C33_LINK_FREQ * 2 * \
					 GC4C33_LANES / GC4C33_BITS_PER_SAMPLE)
#define GC4C33_XVCLK_FREQ		 27000000

#define CHIP_ID				0x46c3
#define GC4C33_REG_CHIP_ID_H		0x03f0
#define GC4C33_REG_CHIP_ID_L		0x03f1

#define GC4C33_REG_CTRL_MODE		0x0100
#define GC4C33_MODE_SW_STANDBY		0x00
#define GC4C33_MODE_STREAMING		0x09

#define GC4C33_REG_EXPOSURE_H	0x0202
#define GC4C33_REG_EXPOSURE_L	0x0203
#define	GC4C33_EXPOSURE_MIN		4
#define	GC4C33_EXPOSURE_STEP		1
#define GC4C33_VTS_MAX			0x7fff

#define GC4C33_GAIN_MIN			64
#define GC4C33_GAIN_MAX			0xffff
#define GC4C33_GAIN_STEP		1
#define GC4C33_GAIN_DEFAULT		256

#define GC4C33_REG_TEST_PATTERN		0x008c
#define GC4C33_TEST_PATTERN_ENABLE	0x11
#define GC4C33_TEST_PATTERN_DISABLE	0x0

#define GC4C33_REG_VTS_H			0x0340
#define GC4C33_REG_VTS_L			0x0341

#define GC4C33_REG_DPCC_ENABLE		0x00aa
#define GC4C33_REG_DPCC_SINGLE		0x00a1
#define GC4C33_REG_DPCC_DOUBLE		0x00a2

#define GC4C33_FLIP_MIRROR_REG		0x0101
#define GC4C33_MIRROR_BIT_MASK		BIT(0)
#define GC4C33_FLIP_BIT_MASK		BIT(1)

#define REG_NULL			0xFFFF

#define GC4C33_REG_VALUE_08BIT		1
#define GC4C33_REG_VALUE_16BIT		2
#define GC4C33_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"
#define GC4C33_NAME			"gc4c33"

#define GC4C33_ENABLE_DPCC
#define GC4C33_ENABLE_OTP
//#define GC4C33_ENABLE_HIGHLIGHT

static const char * const gc4c33_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
	"avdd",		/* Analog power */
};

#define GC4C33_NUM_SUPPLIES ARRAY_SIZE(gc4c33_supply_names)

enum gc4c33_max_pad {
	PAD0, /* link to isp */
	PAD1, /* link to csi wr0 | hdr x2:L x3:M */
	PAD2, /* link to csi wr1 | hdr      x3:L */
	PAD3, /* link to csi wr2 | hdr x2:M x3:S */
	PAD_MAX,
};

struct regval {
	u16 addr;
	u8 val;
};

struct gc4c33_mode {
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

struct gc4c33 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct gpio_desc	*pwren_gpio;
	struct regulator_bulk_data supplies[GC4C33_NUM_SUPPLIES];

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
	struct v4l2_ctrl	*h_flip;
	struct v4l2_ctrl	*v_flip;
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct gc4c33_mode *cur_mode;
	u32			cfg_num;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u8			flip;
};

#define to_gc4c33(sd) container_of(sd, struct gc4c33, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval gc4c33_global_regs[] = {
	{REG_NULL, 0x00},
};

static const u32 reg_val_table[43][9] = {
	{0x00, 0x39, 0x00, 0x39, 0x00, 0x00, 0x01, 0x00, 0x20},
	{0x00, 0x39, 0x00, 0x39, 0x08, 0x00, 0x01, 0x0B, 0x20},
	{0x00, 0x39, 0x00, 0x39, 0x01, 0x00, 0x01, 0x1B, 0x1e},
	{0x00, 0x39, 0x00, 0x39, 0x09, 0x00, 0x01, 0x2A, 0x1c},
	{0x00, 0x39, 0x00, 0x39, 0x10, 0x00, 0x01, 0x3E, 0x1a},
	{0x00, 0x39, 0x00, 0x39, 0x18, 0x00, 0x02, 0x13, 0x18},
	{0x00, 0x39, 0x00, 0x39, 0x11, 0x00, 0x02, 0x33, 0x18},
	{0x00, 0x39, 0x00, 0x39, 0x19, 0x00, 0x03, 0x11, 0x16},
	{0x00, 0x39, 0x00, 0x39, 0x30, 0x00, 0x03, 0x3B, 0x16},
	{0x00, 0x39, 0x00, 0x39, 0x38, 0x00, 0x04, 0x26, 0x14},
	{0x00, 0x39, 0x00, 0x39, 0x31, 0x00, 0x05, 0x24, 0x14},
	{0x00, 0x39, 0x00, 0x39, 0x39, 0x00, 0x06, 0x21, 0x12},
	{0x00, 0x39, 0x00, 0x39, 0x32, 0x00, 0x07, 0x28, 0x12},
	{0x00, 0x39, 0x00, 0x39, 0x3a, 0x00, 0x08, 0x3C, 0x12},
	{0x00, 0x39, 0x00, 0x39, 0x33, 0x00, 0x0A, 0x3F, 0x10},
	{0x00, 0x39, 0x00, 0x39, 0x3b, 0x00, 0x0C, 0x38, 0x10},
	{0x00, 0x39, 0x00, 0x39, 0x34, 0x00, 0x0F, 0x17, 0x0e},
	{0x00, 0x39, 0x00, 0x39, 0x3c, 0x00, 0x11, 0x3F, 0x0c},
	{0x00, 0x39, 0x00, 0x39, 0xb4, 0x00, 0x15, 0x34, 0x0a},
	{0x00, 0x39, 0x00, 0x39, 0xbc, 0x00, 0x19, 0x22, 0x08},
	{0x00, 0x39, 0x00, 0x39, 0x34, 0x01, 0x1E, 0x09, 0x06},
	{0x00, 0x39, 0x00, 0x39, 0x3c, 0x11, 0x1A, 0x31, 0x14},
	{0x00, 0x32, 0x00, 0x32, 0x3c, 0x11, 0x20, 0x12, 0x13},
	{0x00, 0x3a, 0x00, 0x3a, 0x3c, 0x11, 0x25, 0x28, 0x12},
	{0x00, 0x33, 0x00, 0x33, 0x3c, 0x11, 0x2D, 0x28, 0x11},
	{0x00, 0x3b, 0x00, 0x3b, 0x3c, 0x11, 0x35, 0x0A, 0x10},
	{0x00, 0x34, 0x00, 0x34, 0x3c, 0x11, 0x3F, 0x22, 0x0e},
	{0x00, 0x3c, 0x00, 0x3c, 0x3c, 0x11, 0x4A, 0x02, 0x0c},
	{0x00, 0xb4, 0x00, 0xb4, 0x3c, 0x11, 0x5A, 0x36, 0x0a},
	{0x00, 0xbc, 0x00, 0xbc, 0x3c, 0x11, 0x69, 0x37, 0x0a},
	{0x01, 0x34, 0x10, 0x34, 0x3c, 0x11, 0x7E, 0x13, 0x08},
	{0x01, 0x3c, 0x10, 0x3c, 0x3c, 0x11, 0x93, 0x0B, 0x06},
	{0x01, 0xb4, 0x10, 0xb4, 0x3c, 0x11, 0xB4, 0x19, 0x04},
	{0x01, 0xbc, 0x10, 0xbc, 0x3c, 0x11, 0xD2, 0x0E, 0x02},
	{0x02, 0x34, 0x20, 0x34, 0x3c, 0x11, 0xFC, 0x0B, 0x02},
	{0x02, 0x3c, 0x20, 0x3c, 0x3c, 0x11, 0xff, 0xff, 0x02},
	{0x01, 0xf4, 0x10, 0xf4, 0x3c, 0x11, 0xff, 0xff, 0x02},
	{0x01, 0xfc, 0x10, 0xfc, 0x3c, 0x11, 0xff, 0xff, 0x02},
	{0x02, 0x74, 0x20, 0x74, 0x3c, 0x11, 0xff, 0xff, 0x02},
	{0x02, 0x7c, 0x20, 0x7c, 0x3c, 0x11, 0xff, 0xff, 0x02},
	{0x02, 0x75, 0x20, 0x75, 0x3c, 0x11, 0xff, 0xff, 0x02},
	{0x02, 0x7d, 0x20, 0x7d, 0x3c, 0x11, 0xff, 0xff, 0x02},
	{0x02, 0xf5, 0x20, 0xf5, 0x3c, 0x11, 0xff, 0xff, 0x02},
};

static const u32 gain_level_table[44] = {
	64,
	75,
	91,
	106,
	126,
	147,
	179,
	209,
	251,
	294,
	356,
	417,
	488,
	572,
	703,
	824,
	983,
	1151,
	1396,
	1634,
	1929,
	1702,
	2066,
	2377,
	2957,
	3402,
	4096,
	4738,
	5814,
	6775,
	8083,
	9419,
	11545,
	13454,
	16139,
	18808,
	22695,
	26447,
	31725,
	36971,
	44784,
	52188,
	62977,
	0xffffffff
};

static const u32 reg_Val_Table_720P[32][5] = {
	{0x00, 0x00, 0x01, 0x00, 0x20},
	{0x08, 0x00, 0x01, 0x0A, 0x20},
	{0x01, 0x00, 0x01, 0x19, 0x1E},
	{0x09, 0x00, 0x01, 0x26, 0x1C},
	{0x10, 0x00, 0x01, 0x3F, 0x1A},
	{0x18, 0x00, 0x02, 0x13, 0x18},
	{0x11, 0x00, 0x02, 0x31, 0x18},
	{0x19, 0x00, 0x03, 0x0B, 0x16},
	{0x30, 0x00, 0x04, 0x04, 0x16},
	{0x38, 0x00, 0x04, 0x2C, 0x14},
	{0x31, 0x00, 0x05, 0x29, 0x13},
	{0x39, 0x00, 0x06, 0x1F, 0x12},
	{0x32, 0x00, 0x07, 0x38, 0x12},
	{0x3a, 0x00, 0x09, 0x05, 0x12},
	{0x33, 0x00, 0x0B, 0x12, 0x10},
	{0x3b, 0x00, 0x0D, 0x00, 0x10},
	{0x34, 0x00, 0x10, 0x03, 0x0e},
	{0x3c, 0x00, 0x12, 0x1E, 0x0c},
	{0xb4, 0x00, 0x16, 0x00, 0x0a},
	{0xbc, 0x00, 0x19, 0x15, 0x08},
	{0x34, 0x01, 0x1F, 0x06, 0x06},
	{0x3c, 0x01, 0x23, 0x33, 0x04},
	{0xb4, 0x01, 0x2C, 0x22, 0x02},
	{0xbc, 0x01, 0x33, 0x12, 0x02},
	{0x34, 0x02, 0x3F, 0x10, 0x02},
	{0x3c, 0x02, 0x48, 0x34, 0x02},
	{0xf4, 0x01, 0x5F, 0x06, 0x02},
	{0xfc, 0x01, 0x6D, 0x1E, 0x02},
	{0x74, 0x02, 0x87, 0x00, 0x02},
	{0x7c, 0x02, 0x9B, 0x19, 0x02},
	{0x75, 0x02, 0xC7, 0x07, 0x02},
	{0x7d, 0x02, 0xE5, 0x0B, 0x02},
};

static const u32 gain_Level_Table_720P[32] = {
	64,
	74,
	89,
	102,
	127,
	147,
	177,
	203,
	260,
	300,
	361,
	415,
	504,
	581,
	722,
	832,
	1027,
	1182,
	1408,
	1621,
	1990,
	2291,
	2850,
	3282,
	4048,
	4660,
	6086,
	7006,
	8640,
	9945,
	12743,
	14667,
};

/*
 * Xclk 27Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 630Mbps, 2lane
 */
static const struct regval gc4c33_linear10bit_2560x1440_regs[] = {
	{0x03fe, 0xf0},
	{0x03fe, 0xf0},
	{0x03fe, 0xf0},
	{0x03fe, 0xf0},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x03fe, 0x00},
	{0x031c, 0x01},
	{0x0317, 0x24},
	{0x0320, 0x77},
	{0x0106, 0x78},
	{0x0324, 0x84},
	{0x0327, 0x05},
	{0x0325, 0x08},
	{0x0326, 0x2d},
	{0x031a, 0x00},
	{0x0314, 0x30},
	{0x0315, 0x23},
	{0x0334, 0x00},
	{0x0337, 0x02},
	{0x0335, 0x02},
	{0x0336, 0x1f},
	{0x0324, 0xc4},
	{0x0334, 0x40},
	{0x031c, 0x03},
	{0x031c, 0xd2},
	{0x0180, 0x26},
	{0x031c, 0xd6},
	{0x0287, 0x18},
	{0x02ee, 0x70},
	{0x0202, 0x05},
	{0x0203, 0xd0},
	{0x0213, 0x1c},
	{0x0214, 0x04},
	{0x0290, 0x00},
	{0x029d, 0x08},
	{0x0340, 0x05},
	{0x0341, 0xdc},
	{0x0342, 0x01},
	{0x0343, 0xfe},
	{0x00f2, 0x04},
	{0x00f1, 0x0a},
	{0x00f0, 0xa0},
	{0x00c1, 0x05},
	{0x00c2, 0xa0},
	{0x00c3, 0x0a},
	{0x00c4, 0x00},
	{0x00da, 0x05},
	{0x00db, 0xa0},//1440
	{0x00d8, 0x0a},
	{0x00d9, 0x00},//2560
	{0x00c5, 0x0a},
	{0x00c6, 0xa0},
	{0x00bf, 0x17},
	{0x00ce, 0x0a},
	{0x00cd, 0x01},
	{0x00cf, 0x89},
	{0x023c, 0x06},
	{0x02d1, 0xc2},
	{0x027d, 0xcc},
	{0x0238, 0xa4},
	{0x02ce, 0x1f},
	{0x02f9, 0x00},
	{0x0227, 0x74},
	{0x0232, 0xc8},
	{0x0245, 0xa8},
	{0x027d, 0xcc},
	{0x02fa, 0xb0},
	{0x02e7, 0x23},
	{0x02e8, 0x50},
	{0x021d, 0x03},
	{0x0220, 0x43},
	{0x0228, 0x10},
	{0x022c, 0x2c},
	{0x024b, 0x11},
	{0x024e, 0x11},
	{0x024d, 0x11},
	{0x0255, 0x11},
	{0x025b, 0x11},
	{0x0262, 0x01},
	{0x02d4, 0x10},
	{0x0540, 0x10},
	{0x0239, 0x00},
	{0x0231, 0xc4},
	{0x024f, 0x11},
	{0x028c, 0x1a},
	{0x02d3, 0x01},
	{0x02da, 0x35},
	{0x02db, 0xd0},
	{0x02e6, 0x30},
	{0x0512, 0x00},
	{0x0513, 0x00},
	{0x0515, 0x20},
	{0x0518, 0x00},
	{0x0519, 0x00},
	{0x051d, 0x50},
	{0x0211, 0x00},
	{0x0216, 0x00},
	{0x0221, 0x50},
	{0x0223, 0xcc},
	{0x0225, 0x07},
	{0x0229, 0x36},
	{0x022b, 0x0c},
	{0x022e, 0x0c},
	{0x0230, 0x03},
	{0x023a, 0x38},
	{0x027b, 0x3c},
	{0x027c, 0x0c},
	{0x0298, 0x13},
	{0x02a4, 0x07},
	{0x02ab, 0x00},
	{0x02ac, 0x00},
	{0x02ad, 0x07},
	{0x02af, 0x01},
	{0x02cd, 0x3c},
	{0x02d2, 0xe8},
	{0x02e4, 0x00},
	{0x0530, 0x04},
	{0x0531, 0x04},
	{0x0243, 0x36},
	{0x0219, 0x07},
	{0x02e5, 0x28},
	{0x0338, 0xaa},
	{0x0339, 0xaa},
	{0x033a, 0x02},
	{0x023b, 0x20},
	{0x0212, 0x48},
	{0x0523, 0x02},
	{0x0347, 0x06},
	{0x0348, 0x0a},
	{0x0349, 0x10},
	{0x034a, 0x05},
	{0x034b, 0xb4},
	{0x0097, 0x0a},
	{0x0098, 0x10},
	{0x0099, 0x05},
	{0x009a, 0xb0},
	{0x034c, 0x0a},
	{0x034d, 0x00},
	{0x034e, 0x05},
	{0x034f, 0xa0},
	{0x0354, 0x03},
	{0x0352, 0x02},
	{0x0295, 0xff},
	{0x0296, 0xff},
	{0x02f0, 0x22},
	{0x02f1, 0x22},
	{0x02f2, 0xff},
	{0x02f4, 0x32},
	{0x02f5, 0x20},
	{0x02f6, 0x1c},
	{0x02f7, 0x1f},
	{0x02f8, 0x00},
	{0x0291, 0x04},
	{0x0292, 0x22},
	{0x0297, 0x22},
	{0x02d5, 0xfe},
	{0x02d6, 0xd0},
	{0x02d7, 0x35},
	{0x0268, 0x3b},
	{0x0269, 0x3b},
	{0x0272, 0x80},
	{0x0273, 0x80},
	{0x0274, 0x80},
	{0x0275, 0x80},
	{0x0276, 0x80},
	{0x0277, 0x80},
	{0x0278, 0x80},
	{0x0279, 0x80},
	{0x0555, 0x50},
	{0x0556, 0x23},
	{0x0557, 0x50},
	{0x0558, 0x23},
	{0x0559, 0x50},
	{0x055a, 0x23},
	{0x055b, 0x50},
	{0x055c, 0x23},
	{0x055d, 0x50},
	{0x055e, 0x23},
	{0x0550, 0x28},
	{0x0551, 0x28},
	{0x0552, 0x28},
	{0x0553, 0x28},
	{0x0554, 0x28},
	{0x0220, 0x43},
	{0x021f, 0x03},
	{0x0233, 0x01},
	{0x0234, 0x80},
	{0x02be, 0x81},
	{0x00a0, 0x5d},
	{0x00c7, 0x94},
	{0x00c8, 0x15},
	{0x00df, 0x0a},
	{0x00de, 0xfe},
	{0x00c0, 0x0a},
	{0x031c, 0x80},
	{0x031f, 0x10},
	{0x031f, 0x00},
	{0x031c, 0xd2},
	{0x031c, 0xd2},
	{0x031c, 0xd2},
	{0x031c, 0xd2},
	{0x031c, 0x80},
	{0x031f, 0x10},
	{0x031f, 0x00},
	{0x031c, 0xd6},
	{0x0053, 0x00},
	{0x008e, 0x55},
	{0x0205, 0xc0},
	{0x02b0, 0xe0},
	{0x02b1, 0xe0},
	{0x02b3, 0x00},
	{0x02b4, 0x00},
	{0x02fc, 0x00},
	{0x02fd, 0x00},
	{0x0263, 0x00},
	{0x0267, 0x00},
	{0x0451, 0x21},
	{0x0455, 0x05},
	{0x0452, 0xE6},
	{0x0456, 0x04},
	{0x0450, 0xAB},
	{0x0454, 0x02},
	{0x0453, 0xAB},
	{0x0457, 0x02},
	{0x0226, 0x30},
	{0x0042, 0x20},
	{0x0458, 0x01},
	{0x0459, 0x01},
	{0x045a, 0x01},
	{0x045b, 0x01},
	{0x044c, 0x80},
	{0x044d, 0x80},
	{0x044e, 0x80},
	{0x044f, 0x80},
	{0x0060, 0x40},
	{0x00e1, 0x81},
	{0x00e2, 0x1c},
	{0x00e4, 0x01},
	{0x00e5, 0x01},
	{0x00e6, 0x01},
	{0x00e7, 0x00},
	{0x00e8, 0x00},
	{0x00e9, 0x00},
	{0x00ea, 0xf0},
	{0x00ef, 0x04},
	{0x00a9, 0x20},
	{0x00b3, 0x00},
	{0x00b4, 0x10},
	{0x00b5, 0x20},
	{0x00b6, 0x30},
	{0x00b7, 0x40},
	{0x00d1, 0x06},
	{0x00d2, 0x04},
	{0x00d4, 0x02},
	{0x00d5, 0x04},
	{0x0089, 0x03},
	{0x008c, 0x10},
	{0x0080, 0x04},
	{0x0180, 0x66},
	{0x0181, 0x30},
	{0x0182, 0x55},
	{0x0185, 0x01},
	{0x0114, 0x01},
	{0x0115, 0x12},
	{0x0103, 0x00},
	{0x0104, 0x20},
	{0x00aa, 0x38},
	{0x00a7, 0x18},
	{0x00a8, 0x10},
	{0x00a1, 0xFF},
	{0x00a2, 0xFF},
	{REG_NULL, 0x00},
};

/*
 * Xclk 27Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 522Mbps, 2lane
 */
static const struct regval gc4c33_linear10bit_1280x720_regs[] = {
	{0x031c, 0x01},
	{0x0317, 0x24},
	{0x0320, 0x77},
	{0x0106, 0x78},
	{0x0324, 0x04},
	{0x0327, 0x03},
	{0x0325, 0x00},
	{0x0326, 0x20},
	{0x031a, 0x00},
	{0x0314, 0x30},
	{0x0315, 0x32},
	{0x0334, 0x40},
	{0x0337, 0x03},
	{0x0335, 0x05},
	{0x0336, 0x3a},
	{0x0324, 0x44},
	{0x0334, 0x40},
	{0x031c, 0x03},
	{0x031c, 0xd2},
	{0x0180, 0x26},
	{0x031c, 0xd6},
	{0x0287, 0x18},
	{0x02ee, 0x70},
	{0x0202, 0x02},
	{0x0203, 0xa6},
	{0x0213, 0x1c},
	{0x0214, 0x04},
	{0x0290, 0x00},
	{0x029d, 0x08},
	{0x0340, 0x05},
	{0x0341, 0xdc},
	{0x0342, 0x03},
	{0x0343, 0x20},
	{0x023c, 0x06},
	{0x02d1, 0xe2},
	{0x027d, 0xcc},
	{0x0238, 0xa4},
	{0x02ce, 0x1f},
	{0x02f9, 0x00},
	{0x0227, 0x74},
	{0x0232, 0xc8},
	{0x0245, 0xa8},
	{0x027d, 0xcc},
	{0x02fa, 0xb0},
	{0x02e7, 0x23},
	{0x02e8, 0x50},
	{0x021d, 0x13},
	{0x0220, 0x43},
	{0x0228, 0x10},
	{0x022c, 0x2c},
	{0x02c0, 0x11},
	{0x024b, 0x11},
	{0x024e, 0x11},
	{0x024d, 0x11},
	{0x0255, 0x11},
	{0x025b, 0x11},
	{0x0262, 0x01},
	{0x02d4, 0x10},
	{0x0540, 0x10},
	{0x0239, 0x00},
	{0x0231, 0xc4},
	{0x024f, 0x11},
	{0x028c, 0x1a},
	{0x02d3, 0x01},
	{0x02da, 0x35},
	{0x02db, 0xd0},
	{0x02e6, 0x30},
	{0x0512, 0x00},
	{0x0513, 0x00},
	{0x0515, 0x20},
	{0x0518, 0x00},
	{0x0519, 0x00},
	{0x051d, 0x50},
	{0x0211, 0x00},
	{0x0216, 0x00},
	{0x0221, 0x20},
	{0x0223, 0xcc},
	{0x0225, 0x07},
	{0x0229, 0x36},
	{0x022b, 0x0c},
	{0x022e, 0x0c},
	{0x0230, 0x03},
	{0x023a, 0x38},
	{0x027b, 0x3c},
	{0x027c, 0x0c},
	{0x0298, 0x13},
	{0x02a4, 0x07},
	{0x02ab, 0x00},
	{0x02ac, 0x00},
	{0x02ad, 0x07},
	{0x02af, 0x01},
	{0x02cd, 0x3c},
	{0x02d2, 0xe8},
	{0x02e4, 0x00},
	{0x0530, 0x04},
	{0x0531, 0x04},
	{0x0243, 0x36},
	{0x0219, 0x07},
	{0x02e5, 0x28},
	{0x0338, 0xaa},
	{0x0339, 0xaa},
	{0x033a, 0x02},
	{0x023b, 0x20},
	{0x0212, 0x48},
	{0x0523, 0x02},
	{0x0347, 0x06},
	{0x0348, 0x0a},
	{0x0349, 0x10},
	{0x034a, 0x05},
	{0x034b, 0xb0},
	{0x034c, 0x05},
	{0x034d, 0x00},
	{0x034e, 0x02},
	{0x034f, 0xd0},
	{0x0354, 0x01},
	{0x0295, 0xff},
	{0x0296, 0xff},
	{0x02f0, 0x22},
	{0x02f1, 0x22},
	{0x02f2, 0xff},
	{0x02f4, 0x32},
	{0x02f5, 0x20},
	{0x02f6, 0x1c},
	{0x02f7, 0x1f},
	{0x02f8, 0x00},
	{0x0291, 0x04},
	{0x0292, 0x22},
	{0x0297, 0x22},
	{0x02d5, 0xfe},
	{0x02d6, 0xd0},
	{0x02d7, 0x35},
	{0x021f, 0x10},
	{0x0233, 0x01},
	{0x0234, 0x03},
	{0x0224, 0x01},
	{0x031c, 0x80},
	{0x031f, 0x10},
	{0x031f, 0x00},
	{0x031c, 0xd2},
	{0x031c, 0xd2},
	{0x031c, 0xd2},
	{0x031c, 0xd2},
	{0x031c, 0x80},
	{0x031f, 0x10},
	{0x031f, 0x00},
	{0x031c, 0xd6},
	{0x0053, 0x00},
	{0x008e, 0x55},
	{0x0205, 0xc0},
	{0x02b0, 0xf2},
	{0x02b1, 0xf2},
	{0x02b3, 0x00},
	{0x02b4, 0x00},
	{0x0451, 0x21},
	{0x0455, 0x05},
	{0x0452, 0xE6},
	{0x0456, 0x04},
	{0x0450, 0xAB},
	{0x0454, 0x02},
	{0x0453, 0xAB},
	{0x0457, 0x02},
	{0x0226, 0x30},
	{0x0042, 0x20},
	{0x0458, 0x01},
	{0x0459, 0x01},
	{0x045a, 0x01},
	{0x045b, 0x01},
	{0x044c, 0x80},
	{0x044d, 0x80},
	{0x044e, 0x80},
	{0x044f, 0x80},
	{0x0060, 0x40},
	{0x00a0, 0x15},
	{0x00c7, 0x90},
	{0x00c8, 0x15},
	{0x00e1, 0x81},
	{0x00e2, 0x1c},
	{0x00e4, 0x01},
	{0x00e5, 0x01},
	{0x00e6, 0x01},
	{0x00e7, 0x00},
	{0x00e8, 0x00},
	{0x00e9, 0x00},
	{0x00ea, 0xf0},
	{0x00ef, 0x04},
	{0x0089, 0x03},
	{0x008c, 0x10},
	{0x0080, 0x04},
	{0x0180, 0x66},
	{0x0181, 0x30},
	{0x0182, 0x55},
	{0x0185, 0x01},
	{0x0114, 0x01},
	{0x0115, 0x12},
	{0x0103, 0x00},
	{0x0104, 0x20},
	{0x00aa, 0x3a},
	{0x00a7, 0x18},
	{0x00a8, 0x10},
	{0x00a1, 0xFF},
	{0x00a2, 0xFF},
	{REG_NULL, 0x00},
};

/*
 * Xclk 27Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 630Mbps, 2lane
 */
static const struct regval gc4c33_linear10bit_1920x1080_regs[] = {
	{0x031c, 0x01},
	{0x0317, 0x24},
	{0x0320, 0x77},
	{0x0106, 0x78},
	{0x0324, 0x84},
	{0x0327, 0x30},
	{0x0325, 0x04},
	{0x0326, 0x22},
	{0x031a, 0x00},
	{0x0314, 0x30},
	{0x0315, 0x23},
	{0x0334, 0x00},
	{0x0337, 0x03},
	{0x0335, 0x01},
	{0x0336, 0x46},
	{0x0324, 0xc4},
	{0x0334, 0x40},
	{0x031c, 0x03},
	{0x031c, 0xd2},
	{0x0180, 0x26},
	{0x031c, 0xd6},
	{0x0287, 0x18},
	{0x02ee, 0x70},
	{0x0202, 0x05},
	{0x0203, 0xd0},
	{0x0213, 0x1c},
	{0x0214, 0x04},
	{0x0290, 0x00},
	{0x029d, 0x08},
	{0x0340, 0x05},
	{0x0341, 0xdc},
	{0x0342, 0x01},
	{0x0343, 0xfe},
	{0x00f2, 0x03},
	{0x00f1, 0x0e},
	{0x00f0, 0x2c},
	{0x00c5, 0x0e},
	{0x00c6, 0x2a},
	{0x00bf, 0x16},
	{0x00ce, 0x00},
	{0x00cd, 0x01},
	{0x00cf, 0xe9},
	{0x023c, 0x06},
	{0x02d1, 0xc2},
	{0x027d, 0xcc},
	{0x0238, 0xa4},
	{0x02ce, 0x1f},
	{0x02f9, 0x00},
	{0x0227, 0x74},
	{0x0232, 0xc8},
	{0x0245, 0xa8},
	{0x027d, 0xcc},
	{0x02fa, 0xb0},
	{0x02e7, 0x23},
	{0x02e8, 0x50},
	{0x021d, 0x03},
	{0x0220, 0x43},
	{0x0228, 0x10},
	{0x022c, 0x2c},
	{0x024b, 0x11},
	{0x024e, 0x11},
	{0x024d, 0x11},
	{0x0255, 0x11},
	{0x025b, 0x11},
	{0x0262, 0x01},
	{0x02d4, 0x10},
	{0x0540, 0x10},
	{0x0239, 0x00},
	{0x0231, 0xc4},
	{0x024f, 0x11},
	{0x028c, 0x1a},
	{0x02d3, 0x01},
	{0x02da, 0x35},
	{0x02db, 0xd0},
	{0x02e6, 0x30},
	{0x0512, 0x00},
	{0x0513, 0x00},
	{0x0515, 0x02},
	{0x0518, 0x00},
	{0x0519, 0x00},
	{0x051d, 0x50},
	{0x0211, 0x00},
	{0x0216, 0x00},
	{0x0221, 0x50},
	{0x0223, 0xcc},
	{0x0225, 0x07},
	{0x0229, 0x36},
	{0x022b, 0x0c},
	{0x022e, 0x0c},
	{0x0230, 0x03},
	{0x023a, 0x38},
	{0x027b, 0x3c},
	{0x027c, 0x0c},
	{0x0298, 0x13},
	{0x02a4, 0x07},
	{0x02ab, 0x00},
	{0x02ac, 0x00},
	{0x02ad, 0x07},
	{0x02af, 0x01},
	{0x02cd, 0x3c},
	{0x02d2, 0xe8},
	{0x02e4, 0x00},
	{0x0530, 0x04},
	{0x0531, 0x04},
	{0x0243, 0x36},
	{0x0219, 0x07},
	{0x02e5, 0x28},
	{0x0338, 0xaa},
	{0x0339, 0xaa},
	{0x033a, 0x02},
	{0x023b, 0x20},
	{0x0212, 0x48},
	{0x0523, 0x02},
	{0x0347, 0x06},
	{0x0348, 0x0a},
	{0x0349, 0x10},
	{0x034a, 0x05},
	{0x034b, 0xb0},
	{0x034c, 0x07},
	{0x034d, 0x80},
	{0x034e, 0x04},
	{0x034f, 0x38},
	{0x0354, 0x01},
	{0x0295, 0xff},
	{0x0296, 0xff},
	{0x02f0, 0x22},
	{0x02f1, 0x22},
	{0x02f2, 0xff},
	{0x02f4, 0x32},
	{0x02f5, 0x20},
	{0x02f6, 0x1c},
	{0x02f7, 0x1f},
	{0x02f8, 0x00},
	{0x0291, 0x04},
	{0x0292, 0x22},
	{0x0297, 0x22},
	{0x02d5, 0xfe},
	{0x02d6, 0xd0},
	{0x02d7, 0x35},
	{0x0268, 0x3b},
	{0x0269, 0x3b},
	{0x0272, 0x80},
	{0x0273, 0x80},
	{0x0274, 0x80},
	{0x0275, 0x80},
	{0x0276, 0x80},
	{0x0277, 0x80},
	{0x0278, 0x80},
	{0x0279, 0x80},
	{0x0555, 0x50},
	{0x0556, 0x23},
	{0x0557, 0x50},
	{0x0558, 0x23},
	{0x0559, 0x50},
	{0x055a, 0x23},
	{0x055b, 0x50},
	{0x055c, 0x23},
	{0x055d, 0x50},
	{0x055e, 0x23},
	{0x0550, 0x28},
	{0x0551, 0x28},
	{0x0552, 0x28},
	{0x0553, 0x28},
	{0x0554, 0x28},
	{0x0220, 0x43},
	{0x021f, 0x03},
	{0x0233, 0x01},
	{0x0234, 0x80},
	{0x02be, 0x81},
	{0x00a0, 0x5d},
	{0x00c7, 0x12},
	{0x00c8, 0x15},
	{0x00df, 0x0a},
	{0x00de, 0xfe},
	{0x00aa, 0x3a},
	{0x00c0, 0x0a},
	{0x00c1, 0x04},
	{0x00c2, 0x38},
	{0x00c3, 0x07},
	{0x00c4, 0x80},
	{0x031c, 0x80},
	{0x031f, 0x10},
	{0x031f, 0x00},
	{0x031c, 0xd2},
	{0x031c, 0xd2},
	{0x031c, 0xd2},
	{0x031c, 0xd2},
	{0x031c, 0x80},
	{0x031f, 0x10},
	{0x031f, 0x00},
	{0x031c, 0xd6},
	{0x0053, 0x00},
	{0x008e, 0x55},
	{0x0205, 0xc0},
	{0x02b0, 0xe0},
	{0x02b1, 0xe0},
	{0x02b3, 0x00},
	{0x02b4, 0x00},
	{0x02fc, 0x00},
	{0x02fd, 0x00},
	{0x0263, 0x00},
	{0x0267, 0x00},
	{0x0451, 0x00},
	{0x0455, 0x04},
	{0x0452, 0x00},
	{0x0456, 0x04},
	{0x0450, 0x00},
	{0x0454, 0x04},
	{0x0453, 0x20},
	{0x0457, 0x04},
	{0x0226, 0x30},
	{0x0042, 0x20},
	{0x0458, 0x01},
	{0x0459, 0x01},
	{0x045a, 0x01},
	{0x045b, 0x01},
	{0x044c, 0x80},
	{0x044d, 0x80},
	{0x044e, 0x80},
	{0x044f, 0x80},
	{0x0060, 0x40},
	{0x00e1, 0x81},
	{0x00e2, 0x1c},
	{0x00e4, 0x01},
	{0x00e5, 0x01},
	{0x00e6, 0x01},
	{0x00e7, 0x00},
	{0x00e8, 0x00},
	{0x00e9, 0x00},
	{0x00ea, 0xf0},
	{0x00ef, 0x04},
	{0x00a1, 0x05},
	{0x00a2, 0x05},
	{0x00a7, 0x00},
	{0x00a8, 0x20},
	{0x00a9, 0x20},
	{0x00b3, 0x00},
	{0x00b4, 0x10},
	{0x00b5, 0x20},
	{0x00b6, 0x30},
	{0x00b7, 0x40},
	{0x00d1, 0x06},
	{0x00d2, 0x04},
	{0x00d4, 0x02},
	{0x00d5, 0x04},
	{0x0089, 0x03},
	{0x008c, 0x10},
	{0x0080, 0x04},
	{0x0180, 0x66},
	{0x0181, 0x30},
	{0x0182, 0x55},
	{0x0185, 0x01},
	{0x0114, 0x01},
	{0x0115, 0x12},
	{0x0103, 0x00},
	{0x0104, 0x20},
	{0x00aa, 0x3a},
	{0x00a7, 0x18},
	{0x00a8, 0x10},
	{0x00a1, 0xFF},
	{0x00a2, 0xFF},
	{REG_NULL, 0x00},
};

static const struct gc4c33_mode supported_modes[] = {
	{
		.width = 2560,
		.height = 1440,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0100,
		.hts_def = 0x0AA0,
		.vts_def = 0x05DC,
		.reg_list = gc4c33_linear10bit_2560x1440_regs,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}, {
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0400,
		.hts_def = 0x0E2B,
		.vts_def = 0x0465,
		.reg_list = gc4c33_linear10bit_1920x1080_regs,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}, {
		.width = 1280,
		.height = 720,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0200,
		.hts_def = 0x0855,
		.vts_def = 0x02EE,
		.reg_list = gc4c33_linear10bit_1280x720_regs,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	GC4C33_LINK_FREQ
};

static const char * const gc4c33_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int gc4c33_write_reg(struct i2c_client *client, u16 reg,
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

static int gc4c33_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = gc4c33_write_reg(client, regs[i].addr,
				       GC4C33_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int gc4c33_read_reg(struct i2c_client *client, u16 reg,
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

static int gc4c33_get_reso_dist(const struct gc4c33_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
			abs(mode->height - framefmt->height);
}

static const struct gc4c33_mode *
gc4c33_find_best_fit(struct gc4c33 *gc4c33, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < gc4c33->cfg_num; i++) {
		dist = gc4c33_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int gc4c33_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc4c33 *gc4c33 = to_gc4c33(sd);
	const struct gc4c33_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&gc4c33->mutex);

	mode = gc4c33_find_best_fit(gc4c33, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc4c33->mutex);
		return -ENOTTY;
#endif
	} else {
		gc4c33->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(gc4c33->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(gc4c33->vblank, vblank_def,
					 GC4C33_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&gc4c33->mutex);

	return 0;
}

static int gc4c33_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc4c33 *gc4c33 = to_gc4c33(sd);
	const struct gc4c33_mode *mode = gc4c33->cur_mode;

	mutex_lock(&gc4c33->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc4c33->mutex);
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
	mutex_unlock(&gc4c33->mutex);

	return 0;
}

static int gc4c33_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct gc4c33 *gc4c33 = to_gc4c33(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = gc4c33->cur_mode->bus_fmt;

	return 0;
}

static int gc4c33_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct gc4c33 *gc4c33 = to_gc4c33(sd);

	if (fse->index >= gc4c33->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[0].bus_fmt)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int gc4c33_enable_test_pattern(struct gc4c33 *gc4c33, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | GC4C33_TEST_PATTERN_ENABLE;
	else
		val = GC4C33_TEST_PATTERN_DISABLE;

	return gc4c33_write_reg(gc4c33->client, GC4C33_REG_TEST_PATTERN,
				GC4C33_REG_VALUE_08BIT, val);
}

static int gc4c33_set_gain_reg(struct gc4c33 *gc4c33, u32 gain)
{
	int i;
	int total;
	u32 tol_dig_gain = 0;

	if (gain < 64)
		gain = 64;
	total = sizeof(gain_level_table) / sizeof(u32) - 1;
	for (i = 0; i < total; i++) {
		if (gain_level_table[i] <= gain &&
		    gain < gain_level_table[i + 1])
			break;
	}
	tol_dig_gain = gain * 64 / gain_level_table[i];
	if (i >= total)
		i = total - 1;
	gc4c33_write_reg(gc4c33->client, 0x31d, GC4C33_REG_VALUE_08BIT, 0x2a);
	gc4c33_write_reg(gc4c33->client, 0x2fd,
			 GC4C33_REG_VALUE_08BIT, reg_val_table[i][0]);
	gc4c33_write_reg(gc4c33->client, 0x2fc,
			 GC4C33_REG_VALUE_08BIT, reg_val_table[i][1]);
	gc4c33_write_reg(gc4c33->client, 0x263,
			 GC4C33_REG_VALUE_08BIT, reg_val_table[i][2]);
	gc4c33_write_reg(gc4c33->client, 0x267,
			 GC4C33_REG_VALUE_08BIT, reg_val_table[i][3]);

	gc4c33_write_reg(gc4c33->client, 0x31d, GC4C33_REG_VALUE_08BIT, 0x28);

	gc4c33_write_reg(gc4c33->client, 0x2b3,
			 GC4C33_REG_VALUE_08BIT, reg_val_table[i][4]);
	gc4c33_write_reg(gc4c33->client, 0x2b4,
			 GC4C33_REG_VALUE_08BIT, reg_val_table[i][5]);
	gc4c33_write_reg(gc4c33->client, 0x2b8,
			 GC4C33_REG_VALUE_08BIT, reg_val_table[i][6]);
	gc4c33_write_reg(gc4c33->client, 0x2b9,
			 GC4C33_REG_VALUE_08BIT, reg_val_table[i][7]);
	gc4c33_write_reg(gc4c33->client, 0x515,
			 GC4C33_REG_VALUE_08BIT, reg_val_table[i][8]);

	gc4c33_write_reg(gc4c33->client, 0x20e,
			 GC4C33_REG_VALUE_08BIT, (tol_dig_gain >> 6));
	gc4c33_write_reg(gc4c33->client, 0x20f,
			 GC4C33_REG_VALUE_08BIT, ((tol_dig_gain & 0x3f) << 2));
	return 0;
}

static int gc4c33_set_gain_reg_720P(struct gc4c33 *gc4c33, u32 gain)
{
	int i;
	int total;
	u32 tol_dig_gain = 0;

	total = sizeof(gain_Level_Table_720P) / sizeof(u32) - 1;
	for (i = 0; i < total; i++) {
		if (gain_Level_Table_720P[i] <= gain &&
		    gain < gain_Level_Table_720P[i + 1])
			break;
	}
	if (gain == gain_Level_Table_720P[total])
		i = total;
	tol_dig_gain = gain * 64 / gain_Level_Table_720P[i];
	gc4c33_write_reg(gc4c33->client, 0x2b3,
			 GC4C33_REG_VALUE_08BIT, reg_Val_Table_720P[i][0]);
	gc4c33_write_reg(gc4c33->client, 0x2b4,
			 GC4C33_REG_VALUE_08BIT, reg_Val_Table_720P[i][1]);
	gc4c33_write_reg(gc4c33->client, 0x2b8,
			 GC4C33_REG_VALUE_08BIT, reg_Val_Table_720P[i][2]);
	gc4c33_write_reg(gc4c33->client, 0x2b9,
			 GC4C33_REG_VALUE_08BIT, reg_Val_Table_720P[i][3]);
	gc4c33_write_reg(gc4c33->client, 0x515,
			 GC4C33_REG_VALUE_08BIT, reg_Val_Table_720P[i][4]);
	gc4c33_write_reg(gc4c33->client, 0x20e,
			 GC4C33_REG_VALUE_08BIT, (tol_dig_gain >> 6));
	gc4c33_write_reg(gc4c33->client, 0x20f,
			 GC4C33_REG_VALUE_08BIT, ((tol_dig_gain & 0x3f) << 2));
	return 0;
}

static int gc4c33_set_dpcc_cfg(struct gc4c33 *gc4c33,
			       struct rkmodule_dpcc_cfg *dpcc)
{
	int ret = 0;

#ifdef GC4C33_ENABLE_DPCC
	if (dpcc->enable) {
		ret = gc4c33_write_reg(gc4c33->client,
				       GC4C33_REG_DPCC_ENABLE,
				       GC4C33_REG_VALUE_08BIT,
				       0x38 | (dpcc->enable & 0x03));

		ret |= gc4c33_write_reg(gc4c33->client,
					GC4C33_REG_DPCC_SINGLE,
					GC4C33_REG_VALUE_08BIT,
					255 - dpcc->cur_single_dpcc *
					255 / dpcc->total_dpcc);

		ret |= gc4c33_write_reg(gc4c33->client,
					GC4C33_REG_DPCC_DOUBLE,
					GC4C33_REG_VALUE_08BIT,
					255 - dpcc->cur_multiple_dpcc *
					255 / dpcc->total_dpcc);
	} else {
		ret = gc4c33_write_reg(gc4c33->client,
				       GC4C33_REG_DPCC_ENABLE,
				       GC4C33_REG_VALUE_08BIT,
				       0x38);

		ret |= gc4c33_write_reg(gc4c33->client,
					GC4C33_REG_DPCC_SINGLE,
					GC4C33_REG_VALUE_08BIT,
					0xff);

		ret |= gc4c33_write_reg(gc4c33->client,
					GC4C33_REG_DPCC_DOUBLE,
					GC4C33_REG_VALUE_08BIT,
					0xff);
	}
#else
	ret = gc4c33_write_reg(gc4c33->client,
			       GC4C33_REG_DPCC_ENABLE,
			       GC4C33_REG_VALUE_08BIT,
			       0x38);

	ret |= gc4c33_write_reg(gc4c33->client,
				GC4C33_REG_DPCC_SINGLE,
				GC4C33_REG_VALUE_08BIT,
				0xff);

	ret |= gc4c33_write_reg(gc4c33->client,
				GC4C33_REG_DPCC_DOUBLE,
				GC4C33_REG_VALUE_08BIT,
				0xff);
#endif

	return ret;
}

static int gc4c33_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct gc4c33 *gc4c33 = to_gc4c33(sd);
	const struct gc4c33_mode *mode = gc4c33->cur_mode;

	mutex_lock(&gc4c33->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&gc4c33->mutex);

	return 0;
}

static int gc4c33_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct gc4c33 *gc4c33 = to_gc4c33(sd);
	const struct gc4c33_mode *mode = gc4c33->cur_mode;
	u32 val = 1 << (GC4C33_LANES - 1) |
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

static void gc4c33_get_module_inf(struct gc4c33 *gc4c33,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, GC4C33_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, gc4c33->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, gc4c33->len_name, sizeof(inf->base.lens));
}

static long gc4c33_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc4c33 *gc4c33 = to_gc4c33(sd);
	struct rkmodule_hdr_cfg *hdr;
	struct rkmodule_nr_switch_threshold *nr_switch;
	u32 i, h, w;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc4c33_get_module_inf(gc4c33, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = gc4c33->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = gc4c33->cur_mode->width;
		h = gc4c33->cur_mode->height;
		for (i = 0; i < gc4c33->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				gc4c33->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == gc4c33->cfg_num) {
			dev_err(&gc4c33->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = gc4c33->cur_mode->hts_def -
			    gc4c33->cur_mode->width;
			h = gc4c33->cur_mode->vts_def -
			    gc4c33->cur_mode->height;
			__v4l2_ctrl_modify_range(gc4c33->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(gc4c33->vblank, h,
						 GC4C33_VTS_MAX -
						 gc4c33->cur_mode->height,
						 1, h);
		}
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		break;
	case RKMODULE_SET_DPCC_CFG:
		ret = gc4c33_set_dpcc_cfg(gc4c33, (struct rkmodule_dpcc_cfg *)arg);
		break;
	case RKMODULE_GET_NR_SWITCH_THRESHOLD:
		nr_switch = (struct rkmodule_nr_switch_threshold *)arg;
		nr_switch->direct = 0;
		nr_switch->up_thres = 3014;
		nr_switch->down_thres = 3014;
		nr_switch->div_coeff = 100;
		ret = 0;
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);
		if (stream)
			ret = gc4c33_write_reg(gc4c33->client, GC4C33_REG_CTRL_MODE,
				GC4C33_REG_VALUE_08BIT, GC4C33_MODE_STREAMING);
		else
			ret = gc4c33_write_reg(gc4c33->client, GC4C33_REG_CTRL_MODE,
				GC4C33_REG_VALUE_08BIT, GC4C33_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc4c33_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct rkmodule_dpcc_cfg *dpcc;
	struct preisp_hdrae_exp_s *hdrae;
	struct rkmodule_nr_switch_threshold *nr_switch;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc4c33_ioctl(sd, cmd, inf);
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
			ret = gc4c33_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc4c33_ioctl(sd, cmd, hdr);
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
			ret = gc4c33_ioctl(sd, cmd, hdr);
		kfree(hdr);
		break;
	case RKMODULE_SET_DPCC_CFG:
		dpcc = kzalloc(sizeof(*dpcc), GFP_KERNEL);
		if (!dpcc) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(dpcc, up, sizeof(*dpcc));
		if (!ret)
			ret = gc4c33_ioctl(sd, cmd, dpcc);
		kfree(dpcc);
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		hdrae = kzalloc(sizeof(*hdrae), GFP_KERNEL);
		if (!hdrae) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(hdrae, up, sizeof(*hdrae));
		if (!ret)
			ret = gc4c33_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_GET_NR_SWITCH_THRESHOLD:
		nr_switch = kzalloc(sizeof(*nr_switch), GFP_KERNEL);
		if (!nr_switch) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc4c33_ioctl(sd, cmd, nr_switch);
		if (!ret)
			ret = copy_to_user(up, nr_switch, sizeof(*nr_switch));
		kfree(nr_switch);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc4c33_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

#ifdef GC4C33_ENABLE_OTP
static int gc4c33_sensor_dpc_otp_dd(struct gc4c33 *gc4c33)
{
	u32 num = 0;
	int ret;

	ret = gc4c33_write_reg(gc4c33->client, 0x0a70,
			       GC4C33_REG_VALUE_08BIT, 0x00);
	ret |= gc4c33_write_reg(gc4c33->client, 0x0317,
				GC4C33_REG_VALUE_08BIT, 0x2c);
	ret |= gc4c33_write_reg(gc4c33->client, 0x0a67,
				GC4C33_REG_VALUE_08BIT, 0x80);
	ret |= gc4c33_write_reg(gc4c33->client, 0x0a4f,
				GC4C33_REG_VALUE_08BIT, 0x00);
	ret |= gc4c33_write_reg(gc4c33->client, 0x0a54,
				GC4C33_REG_VALUE_08BIT, 0x80);
	ret |= gc4c33_write_reg(gc4c33->client, 0x0a66,
				GC4C33_REG_VALUE_08BIT, 0x03);
	ret |= gc4c33_write_reg(gc4c33->client, 0x0a69,
				GC4C33_REG_VALUE_08BIT, 0x00);
	ret |= gc4c33_write_reg(gc4c33->client, 0x0a6a,
				GC4C33_REG_VALUE_08BIT, 0x70);
	ret |= gc4c33_write_reg(gc4c33->client, 0x0313,
				GC4C33_REG_VALUE_08BIT, 0x20);
	ret |= gc4c33_read_reg(gc4c33->client, 0x0a6c,
			       GC4C33_REG_VALUE_08BIT, &num);
	ret |= gc4c33_write_reg(gc4c33->client, 0x0a69,
				GC4C33_REG_VALUE_08BIT, 0x00);
	ret |= gc4c33_write_reg(gc4c33->client, 0x0a6a,
				GC4C33_REG_VALUE_08BIT, 0x10);
	ret |= gc4c33_write_reg(gc4c33->client, 0x0313,
				GC4C33_REG_VALUE_08BIT, 0x20);

	if (num != 0) {
		ret |= gc4c33_write_reg(gc4c33->client, 0x0317,
					GC4C33_REG_VALUE_08BIT, 0x2c);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a67,
					GC4C33_REG_VALUE_08BIT, 0x80);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a66,
					GC4C33_REG_VALUE_08BIT, 0x03);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a70,
					GC4C33_REG_VALUE_08BIT, 0x05);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a71,
					GC4C33_REG_VALUE_08BIT, 0x00);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a72,
					GC4C33_REG_VALUE_08BIT, 0x08);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a73,
					GC4C33_REG_VALUE_08BIT, num);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a74,
					GC4C33_REG_VALUE_08BIT, 0x00);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a75,
					GC4C33_REG_VALUE_08BIT, 0x80);
		ret |= gc4c33_write_reg(gc4c33->client, 0x05be,
					GC4C33_REG_VALUE_08BIT, 0x00);
		ret |= gc4c33_write_reg(gc4c33->client, 0x05a9,
					GC4C33_REG_VALUE_08BIT, 0x01);
		usleep_range(30 * 1000, 30 * 1000 * 2);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0313,
					GC4C33_REG_VALUE_08BIT, 0x80);
		usleep_range(120 * 1000, 120 * 1000 * 2);

		ret |= gc4c33_write_reg(gc4c33->client, 0x0080,
					GC4C33_REG_VALUE_08BIT, 0x06);
		ret |= gc4c33_write_reg(gc4c33->client, 0x05be,
					GC4C33_REG_VALUE_08BIT, 0x01);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a70,
					GC4C33_REG_VALUE_08BIT, 0x00);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a69,
					GC4C33_REG_VALUE_08BIT, 0x00);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a6a,
					GC4C33_REG_VALUE_08BIT, 0x10);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0313,
					GC4C33_REG_VALUE_08BIT, 0x20);
	} else {
		ret |= gc4c33_write_reg(gc4c33->client, 0x0317,
					GC4C33_REG_VALUE_08BIT, 0x2c);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a67,
					GC4C33_REG_VALUE_08BIT, 0x80);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a4f,
					GC4C33_REG_VALUE_08BIT, 0x00);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a54,
					GC4C33_REG_VALUE_08BIT, 0x80);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a66,
					GC4C33_REG_VALUE_08BIT, 0x03);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a69,
					GC4C33_REG_VALUE_08BIT, 0x00);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0a6a,
					GC4C33_REG_VALUE_08BIT, 0x10);
		ret |= gc4c33_write_reg(gc4c33->client, 0x0313,
					GC4C33_REG_VALUE_08BIT, 0x20);
	}

#ifdef GC4C33_ENABLE_HIGHLIGHT
	ret |= gc4c33_write_reg(gc4c33->client, 0x0080,
				GC4C33_REG_VALUE_08BIT, 0x04);
	ret |= gc4c33_write_reg(gc4c33->client, 0x0090,
				GC4C33_REG_VALUE_08BIT, 0x49);
	ret |= gc4c33_write_reg(gc4c33->client, 0x05be,
				GC4C33_REG_VALUE_08BIT, 0x01);
#endif

	return ret;
}
#endif

static int __gc4c33_start_stream(struct gc4c33 *gc4c33)
{
	int ret;

	ret = gc4c33_write_array(gc4c33->client, gc4c33->cur_mode->reg_list);
	if (ret)
		return ret;

#ifdef GC4C33_ENABLE_OTP
	ret = gc4c33_sensor_dpc_otp_dd(gc4c33);
	if (ret)
		return ret;
#endif

	/* In case these controls are set before streaming */
	mutex_unlock(&gc4c33->mutex);
	ret = v4l2_ctrl_handler_setup(&gc4c33->ctrl_handler);
	mutex_lock(&gc4c33->mutex);
	if (ret)
		return ret;

	return gc4c33_write_reg(gc4c33->client, GC4C33_REG_CTRL_MODE,
				GC4C33_REG_VALUE_08BIT, GC4C33_MODE_STREAMING);
}

static int __gc4c33_stop_stream(struct gc4c33 *gc4c33)
{
	return gc4c33_write_reg(gc4c33->client, GC4C33_REG_CTRL_MODE,
				GC4C33_REG_VALUE_08BIT, GC4C33_MODE_SW_STANDBY);
}

static int gc4c33_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc4c33 *gc4c33 = to_gc4c33(sd);
	struct i2c_client *client = gc4c33->client;
	int ret = 0;

	mutex_lock(&gc4c33->mutex);
	on = !!on;
	if (on == gc4c33->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __gc4c33_start_stream(gc4c33);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__gc4c33_stop_stream(gc4c33);
		pm_runtime_put(&client->dev);
	}

	gc4c33->streaming = on;

unlock_and_return:
	mutex_unlock(&gc4c33->mutex);

	return ret;
}

static int gc4c33_s_power(struct v4l2_subdev *sd, int on)
{
	struct gc4c33 *gc4c33 = to_gc4c33(sd);
	struct i2c_client *client = gc4c33->client;
	int ret = 0;

	mutex_lock(&gc4c33->mutex);

	/* If the power state is not modified - no work to do. */
	if (gc4c33->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = gc4c33_write_array(gc4c33->client, gc4c33_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		gc4c33->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		gc4c33->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&gc4c33->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 gc4c33_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, GC4C33_XVCLK_FREQ / 1000 / 1000);
}

static int __gc4c33_power_on(struct gc4c33 *gc4c33)
{
	int ret;
	u32 delay_us;
	struct device *dev = &gc4c33->client->dev;

	if (!IS_ERR_OR_NULL(gc4c33->pins_default)) {
		ret = pinctrl_select_state(gc4c33->pinctrl,
					   gc4c33->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(gc4c33->xvclk, GC4C33_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(gc4c33->xvclk) != GC4C33_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(gc4c33->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(gc4c33->reset_gpio))
		gpiod_set_value_cansleep(gc4c33->reset_gpio, 0);

	if (!IS_ERR(gc4c33->pwdn_gpio))
		gpiod_set_value_cansleep(gc4c33->pwdn_gpio, 0);

	usleep_range(500, 1000);
	ret = regulator_bulk_enable(GC4C33_NUM_SUPPLIES, gc4c33->supplies);

	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(gc4c33->pwren_gpio))
		gpiod_set_value_cansleep(gc4c33->pwren_gpio, 1);

	usleep_range(1000, 1100);
	if (!IS_ERR(gc4c33->pwdn_gpio))
		gpiod_set_value_cansleep(gc4c33->pwdn_gpio, 1);
	usleep_range(100, 150);
	if (!IS_ERR(gc4c33->reset_gpio))
		gpiod_set_value_cansleep(gc4c33->reset_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = gc4c33_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(gc4c33->xvclk);

	return ret;
}

static void __gc4c33_power_off(struct gc4c33 *gc4c33)
{
	int ret;
	struct device *dev = &gc4c33->client->dev;

	if (!IS_ERR(gc4c33->pwdn_gpio))
		gpiod_set_value_cansleep(gc4c33->pwdn_gpio, 0);
	clk_disable_unprepare(gc4c33->xvclk);
	if (!IS_ERR(gc4c33->reset_gpio))
		gpiod_set_value_cansleep(gc4c33->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(gc4c33->pins_sleep)) {
		ret = pinctrl_select_state(gc4c33->pinctrl,
					   gc4c33->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(GC4C33_NUM_SUPPLIES, gc4c33->supplies);
	if (!IS_ERR(gc4c33->pwren_gpio))
		gpiod_set_value_cansleep(gc4c33->pwren_gpio, 0);
}

static int gc4c33_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc4c33 *gc4c33 = to_gc4c33(sd);

	return __gc4c33_power_on(gc4c33);
}

static int gc4c33_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc4c33 *gc4c33 = to_gc4c33(sd);

	__gc4c33_power_off(gc4c33);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc4c33_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc4c33 *gc4c33 = to_gc4c33(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc4c33_mode *def_mode = &supported_modes[0];

	mutex_lock(&gc4c33->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&gc4c33->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int gc4c33_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_interval_enum *fie)
{
	struct gc4c33 *gc4c33 = to_gc4c33(sd);

	if (fie->index >= gc4c33->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops gc4c33_pm_ops = {
	SET_RUNTIME_PM_OPS(gc4c33_runtime_suspend,
			   gc4c33_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc4c33_internal_ops = {
	.open = gc4c33_open,
};
#endif

static const struct v4l2_subdev_core_ops gc4c33_core_ops = {
	.s_power = gc4c33_s_power,
	.ioctl = gc4c33_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc4c33_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc4c33_video_ops = {
	.s_stream = gc4c33_s_stream,
	.g_frame_interval = gc4c33_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc4c33_pad_ops = {
	.enum_mbus_code = gc4c33_enum_mbus_code,
	.enum_frame_size = gc4c33_enum_frame_sizes,
	.enum_frame_interval = gc4c33_enum_frame_interval,
	.get_fmt = gc4c33_get_fmt,
	.set_fmt = gc4c33_set_fmt,
	.get_mbus_config = gc4c33_g_mbus_config,
};

static const struct v4l2_subdev_ops gc4c33_subdev_ops = {
	.core	= &gc4c33_core_ops,
	.video	= &gc4c33_video_ops,
	.pad	= &gc4c33_pad_ops,
};

static int gc4c33_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc4c33 *gc4c33 = container_of(ctrl->handler,
					     struct gc4c33, ctrl_handler);
	struct i2c_client *client = gc4c33->client;
	s64 max;
	int ret = 0;
	u32 val = 0;

	/*Propagate change of current control to all related controls*/
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/*Update max exposure while meeting expected vblanking*/
		max = gc4c33->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(gc4c33->exposure,
					 gc4c33->exposure->minimum,
					 max,
					 gc4c33->exposure->step,
					 gc4c33->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = gc4c33_write_reg(gc4c33->client, GC4C33_REG_EXPOSURE_H,
				       GC4C33_REG_VALUE_08BIT,
				       ctrl->val >> 8);
		ret |= gc4c33_write_reg(gc4c33->client, GC4C33_REG_EXPOSURE_L,
					GC4C33_REG_VALUE_08BIT,
					ctrl->val & 0xfe);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (gc4c33->cur_mode->height == 720)
			ret = gc4c33_set_gain_reg_720P(gc4c33, ctrl->val);
		else
			ret = gc4c33_set_gain_reg(gc4c33, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = gc4c33_write_reg(gc4c33->client, GC4C33_REG_VTS_H,
				       GC4C33_REG_VALUE_08BIT,
				       (ctrl->val + gc4c33->cur_mode->height)
					>> 8);
		ret |= gc4c33_write_reg(gc4c33->client, GC4C33_REG_VTS_L,
					GC4C33_REG_VALUE_08BIT,
					(ctrl->val + gc4c33->cur_mode->height)
					& 0xff);
		break;
	case V4L2_CID_HFLIP:
		ret = gc4c33_read_reg(gc4c33->client, GC4C33_FLIP_MIRROR_REG,
				      GC4C33_REG_VALUE_08BIT, &val);
		if (ctrl->val)
			val |= GC4C33_MIRROR_BIT_MASK;
		else
			val &= ~GC4C33_MIRROR_BIT_MASK;
		ret |= gc4c33_write_reg(gc4c33->client, GC4C33_FLIP_MIRROR_REG,
					GC4C33_REG_VALUE_08BIT, val);
		if (ret == 0)
			gc4c33->flip = val;
		break;
	case V4L2_CID_VFLIP:
		ret = gc4c33_read_reg(gc4c33->client, GC4C33_FLIP_MIRROR_REG,
				      GC4C33_REG_VALUE_08BIT, &val);
		if (ctrl->val)
			val |= GC4C33_FLIP_BIT_MASK;
		else
			val &= ~GC4C33_FLIP_BIT_MASK;
		ret |= gc4c33_write_reg(gc4c33->client, GC4C33_FLIP_MIRROR_REG,
					GC4C33_REG_VALUE_08BIT, val);
		if (ret == 0)
			gc4c33->flip = val;
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = gc4c33_enable_test_pattern(gc4c33, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc4c33_ctrl_ops = {
	.s_ctrl = gc4c33_set_ctrl,
};

static int gc4c33_initialize_controls(struct gc4c33 *gc4c33)
{
	const struct gc4c33_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &gc4c33->ctrl_handler;
	mode = gc4c33->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &gc4c33->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, GC4C33_PIXEL_RATE, 1, GC4C33_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	gc4c33->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					   h_blank, h_blank, 1, h_blank);
	if (gc4c33->hblank)
		gc4c33->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc4c33->vblank = v4l2_ctrl_new_std(handler, &gc4c33_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_def,
					   GC4C33_VTS_MAX - mode->height,
					    1, vblank_def);

	exposure_max = mode->vts_def - 4;
	gc4c33->exposure = v4l2_ctrl_new_std(handler, &gc4c33_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     GC4C33_EXPOSURE_MIN,
					     exposure_max,
					     GC4C33_EXPOSURE_STEP,
					     mode->exp_def);

	gc4c33->anal_gain = v4l2_ctrl_new_std(handler, &gc4c33_ctrl_ops,
					      V4L2_CID_ANALOGUE_GAIN,
					      GC4C33_GAIN_MIN,
					      GC4C33_GAIN_MAX,
					      GC4C33_GAIN_STEP,
					      GC4C33_GAIN_DEFAULT);

	gc4c33->test_pattern =
		v4l2_ctrl_new_std_menu_items(handler,
					     &gc4c33_ctrl_ops,
				V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(gc4c33_test_pattern_menu) - 1,
				0, 0, gc4c33_test_pattern_menu);
	gc4c33->h_flip = v4l2_ctrl_new_std(handler, &gc4c33_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	gc4c33->v_flip = v4l2_ctrl_new_std(handler, &gc4c33_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	gc4c33->flip = 0;

	if (handler->error) {
		ret = handler->error;
		dev_err(&gc4c33->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc4c33->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int gc4c33_check_sensor_id(struct gc4c33 *gc4c33,
				  struct i2c_client *client)
{
	struct device *dev = &gc4c33->client->dev;
	u16 id = 0;
	u32 reg_H = 0;
	u32 reg_L = 0;
	int ret;

	ret = gc4c33_read_reg(client, GC4C33_REG_CHIP_ID_H,
			      GC4C33_REG_VALUE_08BIT, &reg_H);
	ret |= gc4c33_read_reg(client, GC4C33_REG_CHIP_ID_L,
			       GC4C33_REG_VALUE_08BIT, &reg_L);
	id = ((reg_H << 8) & 0xff00) | (reg_L & 0xff);
	if (!(reg_H == (CHIP_ID >> 8) || reg_L == (CHIP_ID & 0xff))) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	dev_info(dev, "detected gc%04x sensor\n", id);
	return 0;
}

static int gc4c33_configure_regulators(struct gc4c33 *gc4c33)
{
	unsigned int i;

	for (i = 0; i < GC4C33_NUM_SUPPLIES; i++)
		gc4c33->supplies[i].supply = gc4c33_supply_names[i];

	return devm_regulator_bulk_get(&gc4c33->client->dev,
				       GC4C33_NUM_SUPPLIES,
				       gc4c33->supplies);
}

static int gc4c33_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gc4c33 *gc4c33;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	gc4c33 = devm_kzalloc(dev, sizeof(*gc4c33), GFP_KERNEL);
	if (!gc4c33)
		return -ENOMEM;

	of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &gc4c33->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &gc4c33->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &gc4c33->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &gc4c33->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	gc4c33->client = client;
	gc4c33->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < gc4c33->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			gc4c33->cur_mode = &supported_modes[i];
			break;
		}
	}
	if (i == gc4c33->cfg_num)
		gc4c33->cur_mode = &supported_modes[0];

	gc4c33->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(gc4c33->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc4c33->pwren_gpio = devm_gpiod_get(dev, "pwren", GPIOD_OUT_LOW);
	if (IS_ERR(gc4c33->pwren_gpio))
		dev_warn(dev, "Failed to get pwren-gpios\n");

	gc4c33->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc4c33->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc4c33->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc4c33->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	gc4c33->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(gc4c33->pinctrl)) {
		gc4c33->pins_default =
			pinctrl_lookup_state(gc4c33->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(gc4c33->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		gc4c33->pins_sleep =
			pinctrl_lookup_state(gc4c33->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(gc4c33->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = gc4c33_configure_regulators(gc4c33);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&gc4c33->mutex);

	sd = &gc4c33->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc4c33_subdev_ops);
	ret = gc4c33_initialize_controls(gc4c33);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc4c33_power_on(gc4c33);
	if (ret)
		goto err_free_handler;

	ret = gc4c33_check_sensor_id(gc4c33, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc4c33_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	gc4c33->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc4c33->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc4c33->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc4c33->module_index, facing,
		 GC4C33_NAME, dev_name(sd->dev));
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
	__gc4c33_power_off(gc4c33);
err_free_handler:
	v4l2_ctrl_handler_free(&gc4c33->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc4c33->mutex);

	return ret;
}

static int gc4c33_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc4c33 *gc4c33 = to_gc4c33(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc4c33->ctrl_handler);
	mutex_destroy(&gc4c33->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc4c33_power_off(gc4c33);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc4c33_of_match[] = {
	{ .compatible = "galaxycore,gc4c33" },
	{},
};
MODULE_DEVICE_TABLE(of, gc4c33_of_match);
#endif

static const struct i2c_device_id gc4c33_match_id[] = {
	{ "galaxycore,gc4c33", 0 },
	{ },
};

static struct i2c_driver gc4c33_i2c_driver = {
	.driver = {
		.name = GC4C33_NAME,
		.pm = &gc4c33_pm_ops,
		.of_match_table = of_match_ptr(gc4c33_of_match),
	},
	.probe		= &gc4c33_probe,
	.remove		= &gc4c33_remove,
	.id_table	= gc4c33_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc4c33_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc4c33_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("galaxycore gc4c33 sensor driver");
MODULE_LICENSE("GPL v2");
