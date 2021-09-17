// SPDX-License-Identifier: GPL-2.0
/*
 * GC4663 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 fix gain range.
 * V0.0X01.0X04 add enum_frame_interval function.
 * V0.0X01.0X05 support enum sensor fmt
 * V0.0X01.0X06 support mirror and flip
 * V0.0X01.0X07 add quick stream on/off
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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x07)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define GC4663_LANES			2
#define GC4663_BITS_PER_SAMPLE		10
#define GC4663_LINK_FREQ_LINEAR		324000000   //2560*1440
#define GC4663_LINK_FREQ_HDR		672000000

#define GC4663_PIXEL_RATE_LINEAR	(GC4663_LINK_FREQ_LINEAR * 2 / 10 * 2)
#define GC4663_PIXEL_RATE_HDR		(GC4663_LINK_FREQ_HDR * 2 / 10 * 2)

#define GC4663_XVCLK_FREQ		24000000

#define CHIP_ID				0x4653
#define GC4663_REG_CHIP_ID_H		0x03f0
#define GC4663_REG_CHIP_ID_L		0x03f1

#define GC4663_REG_CTRL_MODE		0x0100
#define GC4663_MODE_SW_STANDBY		0x00
#define GC4663_MODE_STREAMING		0x09

#define GC4663_REG_SEXPOSURE_H		0x0200
#define GC4663_REG_SEXPOSURE_L		0x0201
#define GC4663_REG_EXPOSURE_H		0x0202
#define GC4663_REG_EXPOSURE_L		0x0203
#define GC4663_EXPOSURE_MIN		4
#define GC4663_EXPOSURE_STEP		1
#define GC4663_VTS_MAX			0x7fff

#define GC4663_GAIN_MIN			64
#define GC4663_GAIN_MAX			0xffff
#define GC4663_GAIN_STEP		1
#define GC4663_GAIN_DEFAULT		256

#define GC4663_REG_TEST_PATTERN		0x008c
#define GC4663_TEST_PATTERN_ENABLE	0x11
#define GC4663_TEST_PATTERN_DISABLE	0x0

#define GC4663_REG_VTS_H		0x0340
#define GC4663_REG_VTS_L		0x0341

#define GC4663_FLIP_MIRROR_REG		0x0101
#define GC4663_MIRROR_BIT_MASK		BIT(0)
#define GC4663_FLIP_BIT_MASK		BIT(1)

#define REG_NULL			0xFFFF

#define GC4663_REG_VALUE_08BIT		1
#define GC4663_REG_VALUE_16BIT		2
#define GC4663_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"
#define GC4663_NAME			"gc4663"

static const char * const gc4663_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
	"avdd",		/* Analog power */
};

#define GC4663_NUM_SUPPLIES ARRAY_SIZE(gc4663_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct gc4663_mode {
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

struct gc4663 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct gpio_desc	*pwren_gpio;
	struct regulator_bulk_data supplies[GC4663_NUM_SUPPLIES];

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
	struct v4l2_ctrl	*h_flip;
	struct v4l2_ctrl	*v_flip;
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct gc4663_mode *cur_mode;
	u32			cfg_num;
	u32			module_index;
	u32			cur_vts;
	u32			cur_pixel_rate;
	u32			cur_link_freq;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	bool			has_init_exp;
};

#define to_gc4663(sd) container_of(sd, struct gc4663, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval gc4663_global_regs[] = {
	{REG_NULL, 0x00},
};

static const u32 reg_val_table_hdr[26][7] = {
	//2b3 2b4  2b8	 2b9 515  519  2d9
	{0x00, 0x00, 0x01, 0x00, 0x30, 0x28, 0x66},
	{0x20, 0x00, 0x01, 0x0B, 0x30, 0x2a, 0x68},
	{0x01, 0x00, 0x01, 0x19, 0x30, 0x27, 0x65},
	{0x21, 0x00, 0x01, 0x2A, 0x30, 0x29, 0x67},
	{0x02, 0x00, 0x02, 0x00, 0x30, 0x27, 0x65},
	{0x22, 0x00, 0x02, 0x17, 0x30, 0x29, 0x67},
	{0x03, 0x00, 0x02, 0x33, 0x30, 0x28, 0x66},
	{0x23, 0x00, 0x03, 0x14, 0x30, 0x2a, 0x68},
	{0x04, 0x00, 0x04, 0x00, 0x30, 0x2a, 0x68},
	{0x24, 0x00, 0x04, 0x2F, 0x30, 0x2b, 0x69},
	{0x05, 0x00, 0x05, 0x26, 0x30, 0x2c, 0x6A},
	{0x25, 0x00, 0x06, 0x28, 0x30, 0x2e, 0x6C},
	{0x06, 0x00, 0x08, 0x00, 0x30, 0x2f, 0x6D},
	{0x26, 0x00, 0x09, 0x1E, 0x30, 0x31, 0x6F},
	{0x46, 0x00, 0x0B, 0x0C, 0x30, 0x34, 0x72},
	{0x66, 0x00, 0x0D, 0x11, 0x30, 0x37, 0x75},
	{0x0e, 0x00, 0x10, 0x00, 0x30, 0x3a, 0x78},
	{0x2e, 0x00, 0x12, 0x3D, 0x30, 0x3e, 0x7C},
	{0x4e, 0x00, 0x16, 0x19, 0x30, 0x41, 0x7F},
	{0x6e, 0x00, 0x1A, 0x22, 0x30, 0x45, 0x83},
	{0x1e, 0x00, 0x20, 0x00, 0x30, 0x49, 0x87},
	{0x3e, 0x00, 0x25, 0x3A, 0x30, 0x4d, 0x8B},
	{0x5e, 0x00, 0x2C, 0x33, 0x30, 0x53, 0x91},
	{0x7e, 0x00, 0x35, 0x05, 0x30, 0x5a, 0x98},
	{0x9e, 0x00, 0x40, 0x00, 0x30, 0x60, 0x9E},
	{0xbe, 0x00, 0x4B, 0x35, 0x30, 0x67, 0xA5},

};

static const u32 reg_val_table_liner[26][7] = {
	//2b3 2b4  2b8  2b9  515  519  2d9
	{0x00, 0x00, 0x01, 0x00, 0x30, 0x1e, 0x5C},
	{0x20, 0x00, 0x01, 0x0B, 0x30, 0x1e, 0x5C},
	{0x01, 0x00, 0x01, 0x19, 0x30, 0x1d, 0x5B},
	{0x21, 0x00, 0x01, 0x2A, 0x30, 0x1e, 0x5C},
	{0x02, 0x00, 0x02, 0x00, 0x30, 0x1e, 0x5C},
	{0x22, 0x00, 0x02, 0x17, 0x30, 0x1d, 0x5B},
	{0x03, 0x00, 0x02, 0x33, 0x20, 0x16, 0x54},
	{0x23, 0x00, 0x03, 0x14, 0x20, 0x17, 0x55},
	{0x04, 0x00, 0x04, 0x00, 0x20, 0x17, 0x55},
	{0x24, 0x00, 0x04, 0x2F, 0x20, 0x19, 0x57},
	{0x05, 0x00, 0x05, 0x26, 0x20, 0x19, 0x57},
	{0x25, 0x00, 0x06, 0x28, 0x20, 0x1b, 0x59},
	{0x0c, 0x00, 0x08, 0x00, 0x20, 0x1d, 0x5B},
	{0x2C, 0x00, 0x09, 0x1E, 0x20, 0x1f, 0x5D},
	{0x0D, 0x00, 0x0B, 0x0C, 0x20, 0x21, 0x5F},
	{0x2D, 0x00, 0x0D, 0x11, 0x20, 0x24, 0x62},
	{0x1C, 0x00, 0x10, 0x00, 0x20, 0x26, 0x64},
	{0x3C, 0x00, 0x12, 0x3D, 0x18, 0x2a, 0x68},
	{0x5C, 0x00, 0x16, 0x19, 0x18, 0x2c, 0x6A},
	{0x7C, 0x00, 0x1A, 0x22, 0x18, 0x2e, 0x6C},
	{0x9C, 0x00, 0x20, 0x00, 0x18, 0x32, 0x70},
	{0xBC, 0x00, 0x25, 0x3A, 0x18, 0x35, 0x73},
	{0xDC, 0x00, 0x2C, 0x33, 0x10, 0x36, 0x74},
	{0xFC, 0x00, 0x35, 0x05, 0x10, 0x38, 0x76},
	{0x1C, 0x01, 0x40, 0x00, 0x10, 0x3c, 0x7A},
	{0x3C, 0x01, 0x4B, 0x35, 0x10, 0x42, 0x80},
};

static const u32 gain_level_table[27] = {
	64,
	75,
	89,
	106,
	128,
	151,
	179,
	212,
	256,
	303,
	358,
	424,
	512,
	606,
	716,
	849,
	1024,
	1213,
	1433,
	1698,
	2048,
	2426,
	2867,
	3397,
	4096,
	4853,
	0xffff,
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 648Mbps, 2lane
 */
static const struct regval gc4663_linear10bit_2560x1440_regs[] = {
	{0x03fe, 0xf0},
	{0x03fe, 0x00},
	{0x0317, 0x00},
	{0x0320, 0x77},
	{0x0324, 0xc8},
	{0x0325, 0x06},
	{0x0326, 0x60},
	{0x0327, 0x03},
	{0x0334, 0x40},
	{0x0336, 0x60},
	{0x0337, 0x82},
	{0x0315, 0x25},
	{0x031c, 0xc6},
	{0x0287, 0x18},
	{0x0084, 0x00},
	{0x0087, 0x50},
	{0x029d, 0x08},
	{0x0290, 0x00},
	{0x0340, 0x05},
	{0x0341, 0xdc},
	{0x0345, 0x06},
	{0x034b, 0xb0},
	{0x0352, 0x08},
	{0x0354, 0x08},
	{0x02d1, 0xe0},
	{0x0223, 0xf2},
	{0x0238, 0xb4},
	{0x02ce, 0x7f},
	{0x0232, 0xc4},
	{0x02d3, 0x05},
	{0x0243, 0x06},
	{0x02ee, 0x30},
	{0x026f, 0x70},
	{0x0257, 0x09},
	{0x0211, 0x02},
	{0x0219, 0x09},
	{0x023f, 0x2d},
	{0x0518, 0x00},
	{0x0519, 0x01},
	{0x0515, 0x08},
	{0x02d9, 0x3f},
	{0x02da, 0x02},
	{0x02db, 0xe8},
	{0x02e6, 0x20},
	{0x021b, 0x10},
	{0x0252, 0x22},
	{0x024e, 0x22},
	{0x02c4, 0x01},
	{0x021d, 0x17},
	{0x024a, 0x01},
	{0x02ca, 0x02},
	{0x0262, 0x10},
	{0x029a, 0x20},
	{0x021c, 0x0e},
	{0x0298, 0x03},
	{0x029c, 0x00},
	{0x027e, 0x14},
	{0x02c2, 0x10},
	{0x0540, 0x20},
	{0x0546, 0x01},
	{0x0548, 0x01},
	{0x0544, 0x01},
	{0x0242, 0x1b},
	{0x02c0, 0x1b},
	{0x02c3, 0x20},
	{0x02e4, 0x10},
	{0x022e, 0x00},
	{0x027b, 0x3f},
	{0x0269, 0x0f},
	{0x000f, 0x00},
	{0x02d2, 0x40},
	{0x027c, 0x08},
	{0x023a, 0x2e},
	{0x0245, 0xce},
	{0x0530, 0x20},
	{0x0531, 0x02},
	{0x0228, 0x50},
	{0x02ab, 0x00},
	{0x0250, 0x00},
	{0x0221, 0x50},
	{0x02ac, 0x00},
	{0x02a5, 0x02},
	{0x0260, 0x0b},
	{0x0216, 0x04},
	{0x0299, 0x1C},
	{0x02bb, 0x0d},
	{0x02a3, 0x02},
	{0x02a4, 0x02},
	{0x021e, 0x02},
	{0x024f, 0x08},
	{0x028c, 0x08},
	{0x0532, 0x3f},
	{0x0533, 0x02},
	{0x0277, 0xc0},
	{0x0276, 0xc0},
	{0x0239, 0xc0},
	{0x0202, 0x05},
	{0x0203, 0xd0},
	{0x0205, 0xc0},
	{0x02b0, 0x68},
	{0x0002, 0xa9},
	{0x0004, 0x01},
	{0x021a, 0x98},
	{0x0266, 0xa0},
	{0x0020, 0x01},
	{0x0021, 0x03},
	{0x0022, 0x00},
	{0x0023, 0x04},
	{0x0342, 0x06},
	{0x0343, 0x40},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{0x0106, 0x78},
	{0x0108, 0x0c},
	{0x0114, 0x01},
	{0x0115, 0x10},
	{0x0180, 0x46},
	{0x0181, 0x30},
	{0x0182, 0x05},
	{0x0185, 0x01},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{REG_NULL, 0x00},
};

static const struct regval gc4663_linear_global_regs[] = {
	{0x0080, 0x02},
	{0x0097, 0x0a},
	{0x0098, 0x10},
	{0x0099, 0x05},
	{0x009a, 0xb0},
	{0x0317, 0x08},
	{0x0a67, 0x80},
	{0x0a70, 0x03},
	{0x0a82, 0x00},
	{0x0a83, 0x10},
	{0x0a80, 0x2b},
	{0x05be, 0x00},
	{0x05a9, 0x01},
	{0x0313, 0x80},
	{0x05be, 0x01},
	{0x0317, 0x00},
	{0x0a67, 0x00},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipiclk 1309.5Mhz, 2lane
 */
static const struct regval gc4663_hdr10bit_2560x1440_regs[] = {
	{0x03fe, 0xf0},
	{0x03fe, 0x00},
	{0x0317, 0x00},
	{0x0320, 0x77},
	{0x0324, 0xc4},
	{0x0326, 0x42},
	{0x0327, 0x03},
	{0x0321, 0x10},
	{0x0314, 0x50},
	{0x0334, 0x40},
	{0x0335, 0xd1},
	{0x0336, 0x70},
	{0x0337, 0x82},
	{0x0315, 0x33},
	{0x031c, 0xce},
	{0x0287, 0x18},
	{0x0084, 0x00},
	{0x0087, 0x50},
	{0x029d, 0x08},
	{0x0290, 0x00},
	{0x0340, 0x06},
	{0x0341, 0x40},
	{0x0345, 0x06},
	{0x034b, 0xb0},
	{0x0352, 0x08},
	{0x0354, 0x08},
	{0x02d1, 0xc0},
	{0x023c, 0x04},
	{0x0238, 0xb4},//0xa4
	{0x0223, 0xfb},
	{0x0232, 0xc4},
	{0x0279, 0x53},
	{0x02d3, 0x01},
	{0x0243, 0x06},
	{0x02ce, 0xbf},
	{0x02ee, 0x30},
	{0x026f, 0x70},
	{0x0257, 0x09},
	{0x0211, 0x02},
	{0x0219, 0x09},
	{0x023f, 0x2d},
	{0x0518, 0x00},
	{0x0519, 0x14},
	{0x0515, 0x18},
	{0x02d9, 0x50},
	{0x02da, 0x02},
	{0x02db, 0xe8},
	{0x02e6, 0x20},
	{0x021b, 0x10},
	{0x0252, 0x22},
	{0x024e, 0x22},
	{0x02c4, 0x01},
	{0x021d, 0x17},
	{0x024a, 0x01},
	{0x02ca, 0x02},
	{0x0262, 0x10},
	{0x029a, 0x20},
	{0x021c, 0x0e},
	{0x0298, 0x03},
	{0x029c, 0x00},
	{0x027e, 0x14},
	{0x02c2, 0x10},
	{0x0540, 0x20},
	{0x0546, 0x01},
	{0x0548, 0x01},
	{0x0544, 0x01},
	{0x0242, 0x36},
	{0x02c0, 0x36},
	{0x02c3, 0x4d},
	{0x02e4, 0x10},
	{0x022e, 0x00},
	{0x027b, 0x3f},
	{0x0269, 0x0f},
	{0x02d2, 0x40},
	{0x027c, 0x08},
	{0x023a, 0x2e},
	{0x0245, 0xce},
	{0x0530, 0x3f},
	{0x0531, 0x02},
	{0x0228, 0x50},
	{0x02ab, 0x00},
	{0x0250, 0x00},
	{0x0221, 0x50},
	{0x02ac, 0x00},
	{0x02a5, 0x02},
	{0x0260, 0x0b},
	{0x0216, 0x04},
	{0x0299, 0x1C},
	{0x021a, 0x98},
	{0x0266, 0xd0},
	{0x0020, 0x01},
	{0x0021, 0x05},
	{0x0022, 0xc0},
	{0x0023, 0x08},
	{0x02bb, 0x0d},
	{0x02a3, 0x02},
	{0x02a4, 0x02},
	{0x021e, 0x02},
	{0x024f, 0x08},
	{0x028c, 0x08},
	{0x0532, 0x3f},
	{0x0533, 0x02},
	{0x0277, 0x70}, //tx_width
	{0x0276, 0xc0},
	{0x0239, 0xc0},
	{0x0200, 0x00},
	{0x0201, 0x5f},
	{0x0202, 0x05},
	{0x0203, 0xf0},
	{0x0205, 0xc0},
	{0x02b0, 0x68},
	{0x000f, 0x00},
	{0x0006, 0xe0},
	{0x0002, 0xa9},
	{0x0004, 0x01},
	{0x0060, 0x40},
	{0x0218, 0x12},
	{0x0342, 0x05},
	{0x0343, 0x5f},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{0x0106, 0x78},
	{0x0107, 0x89},
	{0x0108, 0x0c},
	{0x0114, 0x01},
	{0x0115, 0x10},
	{0x0180, 0x4f},
	{0x0181, 0x30},
	{0x0182, 0x05},
	{0x0185, 0x01},
	{0x03fe, 0x10},
	{0x03fe, 0x00},
	{REG_NULL, 0x00},
};
static const struct gc4663_mode supported_modes[] = {
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
		.bus_fmt = MEDIA_BUS_FMT_SGRBG10_1X10,
		.reg_list = gc4663_linear10bit_2560x1440_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}, {
		.width = 2560,
		.height = 1440,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0100,
		.hts_def = 0x0AA0,
		.vts_def = 0x0640,
		.bus_fmt = MEDIA_BUS_FMT_SGRBG10_1X10,
		.reg_list = gc4663_hdr10bit_2560x1440_regs,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
};

static const s64 link_freq_menu_items[] = {
	GC4663_LINK_FREQ_LINEAR,
	GC4663_LINK_FREQ_HDR,
};

static const char * const gc4663_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int gc4663_write_reg(struct i2c_client *client, u16 reg,
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

static int gc4663_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = gc4663_write_reg(client, regs[i].addr,
				       GC4663_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int gc4663_read_reg(struct i2c_client *client, u16 reg,
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

static int gc4663_get_reso_dist(const struct gc4663_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
			abs(mode->height - framefmt->height);
}

static const struct gc4663_mode *
gc4663_find_best_fit(struct gc4663 *gc4663, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < gc4663->cfg_num; i++) {
		dist = gc4663_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int gc4663_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc4663 *gc4663 = to_gc4663(sd);
	const struct gc4663_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&gc4663->mutex);

	mode = gc4663_find_best_fit(gc4663, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc4663->mutex);
		return -ENOTTY;
#endif
	} else {
		gc4663->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(gc4663->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(gc4663->vblank, vblank_def,
					 GC4663_VTS_MAX - mode->height,
					 1, vblank_def);

		if (mode->hdr_mode == HDR_X2) {
			gc4663->cur_link_freq = 1;
			gc4663->cur_pixel_rate = GC4663_PIXEL_RATE_HDR;
		} else {
			gc4663->cur_link_freq = 0;
			gc4663->cur_pixel_rate = GC4663_PIXEL_RATE_LINEAR;
		}

		__v4l2_ctrl_s_ctrl_int64(gc4663->pixel_rate,
					 gc4663->cur_pixel_rate);
		__v4l2_ctrl_s_ctrl(gc4663->link_freq,
				   gc4663->cur_link_freq);
		gc4663->cur_vts = mode->vts_def;
	}
	mutex_unlock(&gc4663->mutex);

	return 0;
}

static int gc4663_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct gc4663 *gc4663 = to_gc4663(sd);
	const struct gc4663_mode *mode = gc4663->cur_mode;

	mutex_lock(&gc4663->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc4663->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&gc4663->mutex);

	return 0;
}

static int gc4663_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct gc4663 *gc4663 = to_gc4663(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = gc4663->cur_mode->bus_fmt;

	return 0;
}

static int gc4663_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct gc4663 *gc4663 = to_gc4663(sd);

	if (fse->index >= gc4663->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[0].bus_fmt)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int gc4663_enable_test_pattern(struct gc4663 *gc4663, u32 pattern)
{
	u32 val;

	if (pattern)
		val = GC4663_TEST_PATTERN_ENABLE;
	else
		val = GC4663_TEST_PATTERN_DISABLE;

	return gc4663_write_reg(gc4663->client, GC4663_REG_TEST_PATTERN,
				GC4663_REG_VALUE_08BIT, val);
}

static int gc4663_set_gain_reg_hdr(struct gc4663 *gc4663, u32 gain)
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

	if (i >= total)
		i = total - 1;

	tol_dig_gain = gain * 64 / gain_level_table[i];

	gc4663_write_reg(gc4663->client, 0x2b3,
			 GC4663_REG_VALUE_08BIT, reg_val_table_hdr[i][0]);
	gc4663_write_reg(gc4663->client, 0x2b4,
			 GC4663_REG_VALUE_08BIT, reg_val_table_hdr[i][1]);
	gc4663_write_reg(gc4663->client, 0x2b8,
			 GC4663_REG_VALUE_08BIT, reg_val_table_hdr[i][2]);
	gc4663_write_reg(gc4663->client, 0x2b9,
			 GC4663_REG_VALUE_08BIT, reg_val_table_hdr[i][3]);
	gc4663_write_reg(gc4663->client, 0x515,
			 GC4663_REG_VALUE_08BIT, reg_val_table_hdr[i][4]);
	gc4663_write_reg(gc4663->client, 0x519,
			 GC4663_REG_VALUE_08BIT, reg_val_table_hdr[i][5]);
	gc4663_write_reg(gc4663->client, 0x2d9,
			 GC4663_REG_VALUE_08BIT, reg_val_table_hdr[i][6]);

	gc4663_write_reg(gc4663->client, 0x20e,
			 GC4663_REG_VALUE_08BIT, (tol_dig_gain >> 6));
	gc4663_write_reg(gc4663->client, 0x20f,
			 GC4663_REG_VALUE_08BIT, ((tol_dig_gain & 0x3f) << 2));
	return 0;
}

static int gc4663_set_gain_reg(struct gc4663 *gc4663, u32 gain)
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

	gc4663_write_reg(gc4663->client, 0x2b3,
			 GC4663_REG_VALUE_08BIT, reg_val_table_liner[i][0]);
	gc4663_write_reg(gc4663->client, 0x2b4,
			 GC4663_REG_VALUE_08BIT, reg_val_table_liner[i][1]);
	gc4663_write_reg(gc4663->client, 0x2b8,
			 GC4663_REG_VALUE_08BIT, reg_val_table_liner[i][2]);
	gc4663_write_reg(gc4663->client, 0x2b9,
			 GC4663_REG_VALUE_08BIT, reg_val_table_liner[i][3]);
	gc4663_write_reg(gc4663->client, 0x515,
			 GC4663_REG_VALUE_08BIT, reg_val_table_liner[i][4]);
	gc4663_write_reg(gc4663->client, 0x519,
			 GC4663_REG_VALUE_08BIT, reg_val_table_liner[i][5]);
	gc4663_write_reg(gc4663->client, 0x2d9,
			 GC4663_REG_VALUE_08BIT, reg_val_table_liner[i][6]);


	gc4663_write_reg(gc4663->client, 0x20e,
			 GC4663_REG_VALUE_08BIT, (tol_dig_gain >> 6));
	gc4663_write_reg(gc4663->client, 0x20f,
			 GC4663_REG_VALUE_08BIT, ((tol_dig_gain & 0x3f) << 2));
	return 0;
}

/* window_heigth = 1472
 * dummy = 20
 * frame_length = window_heigth + dummy + vb = 1492 + vb
 * s_exp_time < VB
 * s_exp_time + l_exp_time < frame_length
 */
static int gc4663_set_hdrae(struct gc4663 *gc4663,
			    struct preisp_hdrae_exp_s *ae)
{
	int ret = 0;
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_a_gain, m_a_gain, s_a_gain;
	u32 intt_long_l, intt_long_h;
	u32 intt_short_l, intt_short_h;
	u32 gain;

	if (!gc4663->has_init_exp && !gc4663->streaming) {
		gc4663->init_hdrae_exp = *ae;
		gc4663->has_init_exp = true;
		dev_dbg(&gc4663->client->dev, "gc4663 don't stream, record exp for hdr!\n");
		return ret;
	}
	l_exp_time = ae->long_exp_reg;
	m_exp_time = ae->middle_exp_reg;
	s_exp_time = ae->short_exp_reg;
	l_a_gain = ae->long_gain_reg;
	m_a_gain = ae->middle_gain_reg;
	s_a_gain = ae->short_gain_reg;

	dev_dbg(&gc4663->client->dev,
		"rev exp req: L_exp: 0x%x, M_exp: 0x%x, S_exp 0x%x,l_gain:0x%x, m_gain: 0x%x, s_gain: 0x%x\n",
		l_exp_time, m_exp_time, s_exp_time,
		l_a_gain, m_a_gain, s_a_gain);

	if (gc4663->cur_mode->hdr_mode == HDR_X2)
		l_exp_time = m_exp_time;

	gain = s_a_gain;

	if (l_exp_time <= 1)
		l_exp_time = 1;

	if (s_exp_time < 1)
		s_exp_time = 1;

	if (s_exp_time > gc4663->cur_vts - 1492) {
		dev_err(&gc4663->client->dev, "the s_exp_time is too large.\n");
		s_exp_time = gc4663->cur_vts - 1492;
	}

	if (l_exp_time > gc4663->cur_vts - s_exp_time) {
		dev_err(&gc4663->client->dev, "the l_exp_time is too large.\n");
		l_exp_time = gc4663->cur_vts - s_exp_time;
	}

	if (s_exp_time * 16 == l_exp_time) {
		if (s_exp_time > 94)
			s_exp_time = 94;
		if (l_exp_time > 1504)
			l_exp_time = 1504;
	}

	intt_long_l = l_exp_time & 0xff;
	intt_long_h = (l_exp_time >> 8) & 0x3f;

	intt_short_l = s_exp_time & 0xff;
	intt_short_h = (s_exp_time >> 8) & 0x3f;

	ret |= gc4663_write_reg(gc4663->client, GC4663_REG_EXPOSURE_H,
			GC4663_REG_VALUE_08BIT,
			intt_long_h);
	ret |= gc4663_write_reg(gc4663->client, GC4663_REG_EXPOSURE_L,
			GC4663_REG_VALUE_08BIT,
			intt_long_l);
	ret |= gc4663_write_reg(gc4663->client, GC4663_REG_SEXPOSURE_H,
			GC4663_REG_VALUE_08BIT,
			intt_short_h);
	ret |= gc4663_write_reg(gc4663->client, GC4663_REG_SEXPOSURE_L,
			GC4663_REG_VALUE_08BIT,
			intt_short_l);

	ret |= gc4663_set_gain_reg_hdr(gc4663, gain);
	return ret;
}

static int gc4663_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct gc4663 *gc4663 = to_gc4663(sd);
	const struct gc4663_mode *mode = gc4663->cur_mode;

	mutex_lock(&gc4663->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&gc4663->mutex);

	return 0;
}

static int gc4663_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct gc4663 *gc4663 = to_gc4663(sd);
	const struct gc4663_mode *mode = gc4663->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (GC4663_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	if (mode->hdr_mode == HDR_X2)
		val = 1 << (GC4663_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		V4L2_MBUS_CSI2_CHANNEL_1;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void gc4663_get_module_inf(struct gc4663 *gc4663,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, GC4663_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, gc4663->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, gc4663->len_name, sizeof(inf->base.lens));
}

static int gc4663_get_channel_info(struct gc4663 *gc4663, struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = gc4663->cur_mode->vc[ch_info->index];
	ch_info->width = gc4663->cur_mode->width;
	ch_info->height = gc4663->cur_mode->height;
	ch_info->bus_fmt = gc4663->cur_mode->bus_fmt;
	return 0;
}

static long gc4663_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc4663 *gc4663 = to_gc4663(sd);
	struct rkmodule_hdr_cfg *hdr;
	u32 i, h, w;
	long ret = 0;
	u32 stream = 0;
	struct rkmodule_channel_info *ch_info;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc4663_get_module_inf(gc4663, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = gc4663->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = gc4663->cur_mode->width;
		h = gc4663->cur_mode->height;
		for (i = 0; i < gc4663->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				gc4663->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == gc4663->cfg_num) {
			dev_err(&gc4663->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = gc4663->cur_mode->hts_def -
			    gc4663->cur_mode->width;
			h = gc4663->cur_mode->vts_def -
			    gc4663->cur_mode->height;
			__v4l2_ctrl_modify_range(gc4663->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(gc4663->vblank, h,
						 GC4663_VTS_MAX -
						 gc4663->cur_mode->height,
						 1, h);
			if (gc4663->cur_mode->hdr_mode == HDR_X2) {
				gc4663->cur_link_freq = 1;
				gc4663->cur_pixel_rate = GC4663_PIXEL_RATE_HDR;
			} else {
				gc4663->cur_link_freq = 0;
				gc4663->cur_pixel_rate = GC4663_PIXEL_RATE_LINEAR;
			}

		__v4l2_ctrl_s_ctrl_int64(gc4663->pixel_rate,
					 gc4663->cur_pixel_rate);
		__v4l2_ctrl_s_ctrl(gc4663->link_freq,
				   gc4663->cur_link_freq);
		gc4663->cur_vts = gc4663->cur_mode->vts_def;
		}
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		ret = gc4663_set_hdrae(gc4663, arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);
		if (stream)
			ret = gc4663_write_reg(gc4663->client, GC4663_REG_CTRL_MODE,
				GC4663_REG_VALUE_08BIT, GC4663_MODE_STREAMING);
		else
			ret = gc4663_write_reg(gc4663->client, GC4663_REG_CTRL_MODE,
				GC4663_REG_VALUE_08BIT, GC4663_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = gc4663_get_channel_info(gc4663, ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc4663_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	long ret;
	u32 stream = 0;
	struct rkmodule_channel_info *ch_info;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc4663_ioctl(sd, cmd, inf);
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
			ret = gc4663_ioctl(sd, cmd, cfg);
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

		ret = gc4663_ioctl(sd, cmd, hdr);
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

		ret = copy_from_user(hdr, up, sizeof(*hdr));
		if (!ret)
			ret = gc4663_ioctl(sd, cmd, hdr);
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
			ret = gc4663_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc4663_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc4663_ioctl(sd, cmd, ch_info);
		if (!ret) {
			ret = copy_to_user(up, ch_info, sizeof(*ch_info));
			if (ret)
				ret = -EFAULT;
		}
		kfree(ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __gc4663_start_stream(struct gc4663 *gc4663)
{
	int ret;

	ret = gc4663_write_array(gc4663->client, gc4663->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&gc4663->ctrl_handler);
	if (gc4663->has_init_exp && gc4663->cur_mode->hdr_mode != NO_HDR) {
		ret = gc4663_ioctl(&gc4663->subdev, PREISP_CMD_SET_HDRAE_EXP,
			&gc4663->init_hdrae_exp);
		if (ret) {
			dev_err(&gc4663->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}
	if (ret)
		return ret;

	ret |= gc4663_write_reg(gc4663->client, GC4663_REG_CTRL_MODE,
				GC4663_REG_VALUE_08BIT, GC4663_MODE_STREAMING);
	if (gc4663->cur_mode->hdr_mode == NO_HDR)
		ret |= gc4663_write_array(gc4663->client, gc4663_linear_global_regs);
	return ret;
}

static int __gc4663_stop_stream(struct gc4663 *gc4663)
{
	gc4663->has_init_exp = false;
	return gc4663_write_reg(gc4663->client, GC4663_REG_CTRL_MODE,
				GC4663_REG_VALUE_08BIT, GC4663_MODE_SW_STANDBY);
}

static int gc4663_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc4663 *gc4663 = to_gc4663(sd);
	struct i2c_client *client = gc4663->client;
	int ret = 0;

	mutex_lock(&gc4663->mutex);
	on = !!on;
	if (on == gc4663->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __gc4663_start_stream(gc4663);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__gc4663_stop_stream(gc4663);
		pm_runtime_put(&client->dev);
	}

	gc4663->streaming = on;

unlock_and_return:
	mutex_unlock(&gc4663->mutex);

	return ret;
}

static int gc4663_s_power(struct v4l2_subdev *sd, int on)
{
	struct gc4663 *gc4663 = to_gc4663(sd);
	struct i2c_client *client = gc4663->client;
	int ret = 0;

	mutex_lock(&gc4663->mutex);

	/* If the power state is not modified - no work to do. */
	if (gc4663->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = gc4663_write_array(gc4663->client, gc4663_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		gc4663->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		gc4663->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&gc4663->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 gc4663_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, GC4663_XVCLK_FREQ / 1000 / 1000);
}

static int __gc4663_power_on(struct gc4663 *gc4663)
{
	int ret;
	u32 delay_us;
	struct device *dev = &gc4663->client->dev;

	if (!IS_ERR_OR_NULL(gc4663->pins_default)) {
		ret = pinctrl_select_state(gc4663->pinctrl,
					   gc4663->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(gc4663->xvclk, GC4663_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(gc4663->xvclk) != GC4663_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(gc4663->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(gc4663->reset_gpio))
		gpiod_set_value_cansleep(gc4663->reset_gpio, 0);

	if (!IS_ERR(gc4663->pwdn_gpio))
		gpiod_set_value_cansleep(gc4663->pwdn_gpio, 0);

	usleep_range(500, 1000);
	ret = regulator_bulk_enable(GC4663_NUM_SUPPLIES, gc4663->supplies);

	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(gc4663->pwren_gpio))
		gpiod_set_value_cansleep(gc4663->pwren_gpio, 1);

	usleep_range(1000, 1100);
	if (!IS_ERR(gc4663->pwdn_gpio))
		gpiod_set_value_cansleep(gc4663->pwdn_gpio, 1);
	usleep_range(100, 150);
	if (!IS_ERR(gc4663->reset_gpio))
		gpiod_set_value_cansleep(gc4663->reset_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = gc4663_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(gc4663->xvclk);

	return ret;
}

static void __gc4663_power_off(struct gc4663 *gc4663)
{
	int ret;
	struct device *dev = &gc4663->client->dev;

	if (!IS_ERR(gc4663->pwdn_gpio))
		gpiod_set_value_cansleep(gc4663->pwdn_gpio, 0);
	clk_disable_unprepare(gc4663->xvclk);
	if (!IS_ERR(gc4663->reset_gpio))
		gpiod_set_value_cansleep(gc4663->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(gc4663->pins_sleep)) {
		ret = pinctrl_select_state(gc4663->pinctrl,
					   gc4663->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(GC4663_NUM_SUPPLIES, gc4663->supplies);
	if (!IS_ERR(gc4663->pwren_gpio))
		gpiod_set_value_cansleep(gc4663->pwren_gpio, 0);
}

static int gc4663_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc4663 *gc4663 = to_gc4663(sd);

	return __gc4663_power_on(gc4663);
}

static int gc4663_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc4663 *gc4663 = to_gc4663(sd);

	__gc4663_power_off(gc4663);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc4663_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc4663 *gc4663 = to_gc4663(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc4663_mode *def_mode = &supported_modes[0];

	mutex_lock(&gc4663->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&gc4663->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int gc4663_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_interval_enum *fie)
{
	struct gc4663 *gc4663 = to_gc4663(sd);

	if (fie->index >= gc4663->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops gc4663_pm_ops = {
	SET_RUNTIME_PM_OPS(gc4663_runtime_suspend,
			   gc4663_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc4663_internal_ops = {
	.open = gc4663_open,
};
#endif

static const struct v4l2_subdev_core_ops gc4663_core_ops = {
	.s_power = gc4663_s_power,
	.ioctl = gc4663_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc4663_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc4663_video_ops = {
	.s_stream = gc4663_s_stream,
	.g_frame_interval = gc4663_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc4663_pad_ops = {
	.enum_mbus_code = gc4663_enum_mbus_code,
	.enum_frame_size = gc4663_enum_frame_sizes,
	.enum_frame_interval = gc4663_enum_frame_interval,
	.get_fmt = gc4663_get_fmt,
	.set_fmt = gc4663_set_fmt,
	.get_mbus_config = gc4663_g_mbus_config,
};

static const struct v4l2_subdev_ops gc4663_subdev_ops = {
	.core	= &gc4663_core_ops,
	.video	= &gc4663_video_ops,
	.pad	= &gc4663_pad_ops,
};

static int gc4663_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc4663 *gc4663 = container_of(ctrl->handler,
					     struct gc4663, ctrl_handler);
	struct i2c_client *client = gc4663->client;
	s64 max;
	int ret = 0;
	int val = 0;

	/*Propagate change of current control to all related controls*/
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/*Update max exposure while meeting expected vblanking*/
		max = gc4663->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(gc4663->exposure,
					 gc4663->exposure->minimum,
					 max,
					 gc4663->exposure->step,
					 gc4663->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = gc4663_write_reg(gc4663->client, GC4663_REG_EXPOSURE_H,
				       GC4663_REG_VALUE_08BIT,
				       ctrl->val >> 8);
		ret |= gc4663_write_reg(gc4663->client, GC4663_REG_EXPOSURE_L,
					GC4663_REG_VALUE_08BIT,
					ctrl->val & 0xfe);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = gc4663_set_gain_reg(gc4663, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		gc4663->cur_vts = ctrl->val + gc4663->cur_mode->height;
		ret = gc4663_write_reg(gc4663->client, GC4663_REG_VTS_H,
				       GC4663_REG_VALUE_08BIT,
				       gc4663->cur_vts >> 8);
		ret |= gc4663_write_reg(gc4663->client, GC4663_REG_VTS_L,
					GC4663_REG_VALUE_08BIT,
					gc4663->cur_vts & 0xff);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = gc4663_enable_test_pattern(gc4663, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = gc4663_read_reg(gc4663->client, GC4663_FLIP_MIRROR_REG,
				      GC4663_REG_VALUE_08BIT, &val);
		if (ctrl->val)
			val |= GC4663_MIRROR_BIT_MASK;
		else
			val &= ~GC4663_MIRROR_BIT_MASK;
		ret |= gc4663_write_reg(gc4663->client, GC4663_FLIP_MIRROR_REG,
					GC4663_REG_VALUE_08BIT, val);
		break;
	case V4L2_CID_VFLIP:
		ret = gc4663_read_reg(gc4663->client, GC4663_FLIP_MIRROR_REG,
				      GC4663_REG_VALUE_08BIT, &val);
		if (ctrl->val)
			val |= GC4663_FLIP_BIT_MASK;
		else
			val &= ~GC4663_FLIP_BIT_MASK;
		ret |= gc4663_write_reg(gc4663->client, GC4663_FLIP_MIRROR_REG,
					GC4663_REG_VALUE_08BIT, val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc4663_ctrl_ops = {
	.s_ctrl = gc4663_set_ctrl,
};

static int gc4663_initialize_controls(struct gc4663 *gc4663)
{
	const struct gc4663_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &gc4663->ctrl_handler;
	mode = gc4663->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &gc4663->mutex;

	gc4663->link_freq = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
						   1, 0, link_freq_menu_items);
	if (mode->hdr_mode == HDR_X2) {
		gc4663->cur_link_freq = 1;
		gc4663->cur_pixel_rate = GC4663_PIXEL_RATE_HDR;
	} else {
		gc4663->cur_link_freq = 0;
		gc4663->cur_pixel_rate = GC4663_PIXEL_RATE_LINEAR;
	}

	__v4l2_ctrl_s_ctrl(gc4663->link_freq,
			   gc4663->cur_link_freq);

	gc4663->pixel_rate = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, GC4663_PIXEL_RATE_HDR, 1, GC4663_PIXEL_RATE_HDR);

	h_blank = mode->hts_def - mode->width;
	gc4663->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					   h_blank, h_blank, 1, h_blank);
	if (gc4663->hblank)
		gc4663->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc4663->cur_vts = mode->vts_def;
	gc4663->vblank = v4l2_ctrl_new_std(handler, &gc4663_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_def,
					   GC4663_VTS_MAX - mode->height,
					    1, vblank_def);

	exposure_max = mode->vts_def - 4;
	gc4663->exposure = v4l2_ctrl_new_std(handler, &gc4663_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     GC4663_EXPOSURE_MIN,
					     exposure_max,
					     GC4663_EXPOSURE_STEP,
					     mode->exp_def);

	gc4663->anal_gain = v4l2_ctrl_new_std(handler, &gc4663_ctrl_ops,
					      V4L2_CID_ANALOGUE_GAIN,
					      GC4663_GAIN_MIN,
					      GC4663_GAIN_MAX,
					      GC4663_GAIN_STEP,
					      GC4663_GAIN_DEFAULT);

	gc4663->test_pattern =
		v4l2_ctrl_new_std_menu_items(handler,
					     &gc4663_ctrl_ops,
				V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(gc4663_test_pattern_menu) - 1,
				0, 0, gc4663_test_pattern_menu);

	gc4663->h_flip = v4l2_ctrl_new_std(handler, &gc4663_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	gc4663->v_flip = v4l2_ctrl_new_std(handler, &gc4663_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (handler->error) {
		ret = handler->error;
		dev_err(&gc4663->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc4663->subdev.ctrl_handler = handler;
	gc4663->has_init_exp = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int gc4663_check_sensor_id(struct gc4663 *gc4663,
				  struct i2c_client *client)
{
	struct device *dev = &gc4663->client->dev;
	u16 id = 0;
	u32 reg_H = 0;
	u32 reg_L = 0;
	int ret;

	ret = gc4663_read_reg(client, GC4663_REG_CHIP_ID_H,
			      GC4663_REG_VALUE_08BIT, &reg_H);
	ret |= gc4663_read_reg(client, GC4663_REG_CHIP_ID_L,
			       GC4663_REG_VALUE_08BIT, &reg_L);

	id = ((reg_H << 8) & 0xff00) | (reg_L & 0xff);
	if (!(reg_H == (CHIP_ID >> 8) || reg_L == (CHIP_ID & 0xff))) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	dev_info(dev, "detected gc%04x sensor\n", id);
	return 0;
}

static int gc4663_configure_regulators(struct gc4663 *gc4663)
{
	unsigned int i;

	for (i = 0; i < GC4663_NUM_SUPPLIES; i++)
		gc4663->supplies[i].supply = gc4663_supply_names[i];

	return devm_regulator_bulk_get(&gc4663->client->dev,
				       GC4663_NUM_SUPPLIES,
				       gc4663->supplies);
}

static int gc4663_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gc4663 *gc4663;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	gc4663 = devm_kzalloc(dev, sizeof(*gc4663), GFP_KERNEL);
	if (!gc4663)
		return -ENOMEM;

	of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &gc4663->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &gc4663->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &gc4663->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &gc4663->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	gc4663->client = client;
	gc4663->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < gc4663->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			gc4663->cur_mode = &supported_modes[i];
			break;
		}
	}
	if (i == gc4663->cfg_num)
		gc4663->cur_mode = &supported_modes[0];

	gc4663->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(gc4663->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc4663->pwren_gpio = devm_gpiod_get(dev, "pwren", GPIOD_OUT_LOW);
	if (IS_ERR(gc4663->pwren_gpio))
		dev_warn(dev, "Failed to get pwren-gpios\n");

	gc4663->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gc4663->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc4663->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(gc4663->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	gc4663->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(gc4663->pinctrl)) {
		gc4663->pins_default =
			pinctrl_lookup_state(gc4663->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(gc4663->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		gc4663->pins_sleep =
			pinctrl_lookup_state(gc4663->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(gc4663->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = gc4663_configure_regulators(gc4663);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&gc4663->mutex);

	sd = &gc4663->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc4663_subdev_ops);
	ret = gc4663_initialize_controls(gc4663);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc4663_power_on(gc4663);
	if (ret)
		goto err_free_handler;

	usleep_range(3000, 4000);

	ret = gc4663_check_sensor_id(gc4663, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc4663_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	gc4663->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc4663->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc4663->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc4663->module_index, facing,
		 GC4663_NAME, dev_name(sd->dev));
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
	__gc4663_power_off(gc4663);
err_free_handler:
	v4l2_ctrl_handler_free(&gc4663->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc4663->mutex);

	return ret;
}

static int gc4663_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc4663 *gc4663 = to_gc4663(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc4663->ctrl_handler);
	mutex_destroy(&gc4663->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc4663_power_off(gc4663);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc4663_of_match[] = {
	{ .compatible = "galaxycore,gc4663" },
	{},
};
MODULE_DEVICE_TABLE(of, gc4663_of_match);
#endif

static const struct i2c_device_id gc4663_match_id[] = {
	{ "galaxycore,gc4663", 0 },
	{ },
};

static struct i2c_driver gc4663_i2c_driver = {
	.driver = {
		.name = GC4663_NAME,
		.pm = &gc4663_pm_ops,
		.of_match_table = of_match_ptr(gc4663_of_match),
	},
	.probe		= &gc4663_probe,
	.remove		= &gc4663_remove,
	.id_table	= gc4663_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc4663_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc4663_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("galaxycore gc4663 sensor driver");
MODULE_LICENSE("GPL v2");
