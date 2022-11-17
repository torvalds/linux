// SPDX-License-Identifier: GPL-2.0
/*
 * gc3003 driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
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
#include "../platform/rockchip/isp/rkisp_tb_helper.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x07)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define GC3003_LANES			2
#define GC3003_BITS_PER_SAMPLE		10
#define GC3003_LINK_FREQ_LINEAR		315000000

#define GC3003_PIXEL_RATE_LINEAR	(GC3003_LINK_FREQ_LINEAR * 2 * \
					GC3003_LANES / GC3003_BITS_PER_SAMPLE)
#define GC3003_XVCLK_FREQ		27000000

#define CHIP_ID				0x3003
#define GC3003_REG_CHIP_ID_H		0x03f0
#define GC3003_REG_CHIP_ID_L		0x03f1

#define GC3003_REG_CTRL_MODE		0x023e
#define GC3003_MODE_SW_STANDBY		0x00
#define GC3003_MODE_STREAMING		0x99

#define GC3003_REG_EXPOSURE_H		0x0d03
#define GC3003_REG_EXPOSURE_L		0x0d04
#define	GC3003_EXPOSURE_MIN		4
#define	GC3003_EXPOSURE_STEP		1
#define GC3003_VTS_MAX			0x7fff

#define GC3003_GAIN_MIN			64
#define GC3003_GAIN_MAX			0xffff
#define GC3003_GAIN_STEP		1
#define GC3003_GAIN_DEFAULT		64

#define GC3003_REG_TEST_PATTERN		0x018c
#define GC3003_TEST_PATTERN_ENABLE	0x17
#define GC3003_TEST_PATTERN_DISABLE	0x0

#define GC3003_REG_VTS_H		0x0d41	//0x0d0d
#define GC3003_REG_VTS_L		0x0d42  //0x0d0e  act w: d05 d06

#define GC3003_FLIP_MIRROR_REG		0x0015
#define GC3003_FLIP_MIRROR_REG_1	0x0d15
#define GC3003_MIRROR_BIT_MASK		BIT(0)
#define GC3003_FLIP_BIT_MASK		BIT(3)

#define REG_NULL			0xFFFF

#define GC3003_REG_VALUE_08BIT		1
#define GC3003_REG_VALUE_16BIT		2
#define GC3003_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"
#define GC3003_NAME			"gc3003"

static const char * const gc3003_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
	"avdd",		/* Analog power */
};

#define GC3003_NUM_SUPPLIES ARRAY_SIZE(gc3003_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct gc3003_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
	const struct regval *stream_on_reg_list;
	const struct regval *stand_by_reg_list;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct gc3003 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct gpio_desc	*pwren_gpio;
	struct regulator_bulk_data supplies[GC3003_NUM_SUPPLIES];

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
	const struct gc3003_mode *cur_mode;
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
	bool			is_thunderboot;
	bool			is_first_streamoff;
};

#define to_gc3003(sd) container_of(sd, struct gc3003, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval gc3003_global_regs[] = {
	{REG_NULL, 0x00},
};

static const u32 reg_val_table_liner[25][6] = {
	{0x00, 0x00, 0x05, 0x01, 0x01, 0x00},
	{0x0a, 0x00, 0x06, 0x01, 0x01, 0x0c},
	{0x00, 0x01, 0x06, 0x01, 0x01, 0x1a},
	{0x0a, 0x01, 0x08, 0x01, 0x01, 0x2a},
	{0x20, 0x00, 0x0a, 0x02, 0x02, 0x00},
	{0x25, 0x00, 0x0a, 0x03, 0x02, 0x18},
	{0x20, 0x01, 0x0a, 0x04, 0x02, 0x33},
	{0x25, 0x01, 0x0b, 0x05, 0x03, 0x14},
	{0x30, 0x00, 0x0b, 0x06, 0x04, 0x00},
	{0x32, 0x80, 0x0c, 0x09, 0x04, 0x2f},
	{0x30, 0x01, 0x0c, 0x0c, 0x05, 0x26},
	{0x32, 0x81, 0x0d, 0x0e, 0x06, 0x29},
	{0x38, 0x00, 0x0e, 0x10, 0x08, 0x00},
	{0x39, 0x40, 0x10, 0x12, 0x09, 0x1f},
	{0x38, 0x01, 0x12, 0x12, 0x0b, 0x0d},
	{0x39, 0x41, 0x14, 0x14, 0x0d, 0x12},
	{0x30, 0x08, 0x15, 0x16, 0x10, 0x00},
	{0x32, 0x88, 0x18, 0x1a, 0x12, 0x3e},
	{0x30, 0x09, 0x1a, 0x1d, 0x16, 0x1a},
	{0x32, 0x89, 0x1c, 0x22, 0x1a, 0x23},
	{0x38, 0x08, 0x1e, 0x26, 0x20, 0x00},
	{0x39, 0x48, 0x20, 0x2d, 0x25, 0x3b},
	{0x38, 0x09, 0x22, 0x32, 0x2c, 0x33},
	{0x39, 0x49, 0x24, 0x3a, 0x35, 0x06},
	{0x38, 0x0a, 0x26, 0x42, 0x3f, 0x3f},
};

static const u32 gain_level_table[26] = {
	64,
	76,
	90,
	106,
	128,
	152,
	179,
	212,
	256,
	303,
	358,
	425,
	512,
	607,
	716,
	848,
	1024,
	1214,
	1434,
	1699,
	2048,
	2427,
	2865,
	3393,
	4096,
	0xffffffff,
};

/*
 * Xclk 27Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 630Mbps, 2lane
 */
static const struct regval gc3003_linear_10_2304x1298_regs[] = {
	{0x03fe, 0xf0},
	{0x03fe, 0xf0},
	{0x03fe, 0xf0},
	{0x03fe, 0x00},
	{0x03f3, 0x00},
	{0x03f5, 0xc0},
	{0x03f6, 0x06},
	{0x03f7, 0x01},
	{0x03f8, 0x46},
	{0x03f9, 0x13},
	{0x03fa, 0x00},
	{0x03e0, 0x16},
	{0x03e1, 0x0d},
	{0x03e2, 0x30},
	{0x03e4, 0x08},
	{0x03fc, 0xce},
	{0x0d05, 0x05},
	{0x0d06, 0x40},
	{0x0d76, 0x00},
	{0x0d41, 0x05},
	{0x0d42, 0x3c},
	{0x0d0a, 0x02},
	{0x000c, 0x02},
	{0x0d0d, 0x05},
	{0x0d0e, 0x18},
	{0x000f, 0x09},
	{0x0010, 0x08},
	{0x0017, 0x0c},
	{0x0d53, 0x12},
	{0x0051, 0x03},
	{0x0082, 0x01},
	{0x008c, 0x05},
	{0x008d, 0xd0},
	{0x0db7, 0x01},
	{0x0db0, 0xad},
	{0x0db1, 0x00},
	{0x0db2, 0x8c},
	{0x0db3, 0xf4},
	{0x0db4, 0x00},
	{0x0db5, 0x97},
	{0x0db6, 0x08},
	{0x0d25, 0xcb},
	{0x0d4a, 0x04},
	{0x00d2, 0x70},
	{0x00d7, 0x19},
	{0x00d9, 0x1c},
	{0x00da, 0xc1},
	{0x0d55, 0x1b},
	{0x0d92, 0x17},
	{0x0dc2, 0x30},
	{0x0d2a, 0x30},
	{0x0d19, 0x51},
	{0x0d29, 0x30},
	{0x0d20, 0x30},
	{0x0d72, 0x12},
	{0x0d4e, 0x12},
	{0x0d43, 0x20},
	{0x0050, 0x0c},
	{0x006e, 0x03},
	{0x0153, 0x50},
	{0x0192, 0x04},
	{0x0194, 0x04},
	{0x0195, 0x05},
	{0x0196, 0x10},
	{0x0197, 0x09},
	{0x0198, 0x00},
	{0x0077, 0x01},
	{0x0078, 0x04},
	{0x0079, 0x65},
	{0x0067, 0xc0},
	{0x0054, 0xff},
	{0x0055, 0x02},
	{0x0056, 0x00},
	{0x0057, 0x04},
	{0x005a, 0xff},
	{0x005b, 0x07},
	{0x00d5, 0x03},
	{0x0102, 0x10},
	{0x0d4a, 0x04},
	{0x04e0, 0xff},
	{0x031e, 0x3e},
	{0x0159, 0x01},
	{0x014f, 0x28},
	{0x0150, 0x40},
	{0x0026, 0x00},
	{0x0d26, 0xa0},
	{0x0414, 0x76},
	{0x0415, 0x75},
	{0x0416, 0x75},
	{0x0417, 0x76},
	{0x0155, 0x01},
	{0x0170, 0x3f},
	{0x0171, 0x3f},
	{0x0172, 0x3f},
	{0x0173, 0x3f},
	{0x0428, 0x0b},
	{0x0429, 0x0b},
	{0x042a, 0x0b},
	{0x042b, 0x0b},
	{0x042c, 0x0b},
	{0x042d, 0x0b},
	{0x042e, 0x0b},
	{0x042f, 0x0b},
	{0x0430, 0x05},
	{0x0431, 0x05},
	{0x0432, 0x05},
	{0x0433, 0x05},
	{0x0434, 0x04},
	{0x0435, 0x04},
	{0x0436, 0x04},
	{0x0437, 0x04},
	{0x0438, 0x18},
	{0x0439, 0x18},
	{0x043a, 0x18},
	{0x043b, 0x18},
	{0x043c, 0x1d},
	{0x043d, 0x20},
	{0x043e, 0x22},
	{0x043f, 0x24},
	{0x0468, 0x04},
	{0x0469, 0x04},
	{0x046a, 0x04},
	{0x046b, 0x04},
	{0x046c, 0x04},
	{0x046d, 0x04},
	{0x046e, 0x04},
	{0x046f, 0x04},
	{0x0108, 0xf0},
	{0x0109, 0x80},
	{0x0d03, 0x05},
	{0x0d04, 0x00},
	{0x007a, 0x60},
	{0x00d0, 0x00},
	{0x0080, 0x05},
	{0x0291, 0x0f},
	{0x0292, 0xff},
	{0x0201, 0x27},
	{0x0202, 0x53},
	{0x0203, 0x4e},
	{0x0206, 0x03},
	{0x0212, 0x0b},
	{0x0213, 0x40},
	{0x0215, 0x12},
	{0x023e, 0x99},
	{0x03fe, 0x10},
	{0x0183, 0x09},
	{0x0187, 0x51},
	{0x0d22, 0x04},
	{0x0d21, 0x3C},
	{0x0d03, 0x01},
	{0x0d04, 0x28},
	{0x0d23, 0x0e},
	{0x03fe, 0x00},
	{REG_NULL, 0x00},
};

static const struct regval gc3003_linear_10_320x240_regs[] = {
	{0x03fe, 0xf0},
	{0x03fe, 0xf0},
	{0x03fe, 0xf0},
	{0x03fe, 0x00},
	{0x03f3, 0x00},
	{0x03f5, 0xc0},
	{0x03f6, 0x06},
	{0x03f7, 0x01},
	{0x03f8, 0x46},
	{0x03f9, 0x13},
	{0x03fa, 0x00},
	{0x03e0, 0x16},
	{0x03e1, 0x0d},
	{0x03e2, 0x30},
	{0x03e4, 0x08},
	{0x03fc, 0xce},
	{0x0d05, 0x05},
	{0x0d06, 0xdc},
	{0x0d76, 0x00},
	{0x0d41, 0x01},
	{0x0d42, 0x2c},
	{0x0d0a, 0x02},
	{0x000c, 0x02},
	{0x0d0d, 0x00},
	{0x0d0e, 0xf8},
	{0x000f, 0x01},
	{0x0010, 0x48},
	{0x0017, 0x0c},
	{0x0d53, 0x12},
	{0x0051, 0x03},
	{0x0082, 0x01},
	{0x0086, 0x20},
	{0x008a, 0x01},
	{0x008b, 0x1d},
	{0x008c, 0x05},
	{0x008d, 0xd0},
	{0x0db7, 0x01},
	{0x0db0, 0x05},
	{0x0db1, 0x00},
	{0x0db2, 0x04},
	{0x0db3, 0x54},
	{0x0db4, 0x00},
	{0x0db5, 0x17},
	{0x0db6, 0x08},
	{0x0d25, 0xcb},
	{0x0d4a, 0x04},
	{0x00d2, 0x70},
	{0x00d7, 0x19},
	{0x00d9, 0x10},
	{0x00da, 0xc1},
	{0x0d55, 0x1b},
	{0x0d92, 0x17},
	{0x0dc2, 0x30},
	{0x0d2a, 0x30},
	{0x0d19, 0x51},
	{0x0d29, 0x30},
	{0x0d20, 0x30},
	{0x0d72, 0x12},
	{0x0d4e, 0x12},
	{0x0d43, 0x20},
	{0x0050, 0x0c},
	{0x006e, 0x03},
	{0x0153, 0x50},
	{0x0192, 0x04},
	{0x0194, 0x04},
	{0x0195, 0x00},
	{0x0196, 0xf0},
	{0x0197, 0x01},
	{0x0198, 0x40},
	{0x0077, 0x01},
	{0x0078, 0x65},
	{0x0079, 0x04},
	{0x0067, 0xc0},
	{0x0054, 0xff},
	{0x0055, 0x02},
	{0x0056, 0x00},
	{0x0057, 0x04},
	{0x005a, 0xff},
	{0x005b, 0x07},
	{0x00d5, 0x03},
	{0x0102, 0x10},
	{0x0d4a, 0x04},
	{0x04e0, 0xff},
	{0x031e, 0x3e},
	{0x0159, 0x01},
	{0x014f, 0x28},
	{0x0150, 0x40},
	{0x0026, 0x00},
	{0x0d26, 0xa0},
	{0x0414, 0x77},
	{0x0415, 0x77},
	{0x0416, 0x77},
	{0x0417, 0x77},
	{0x0155, 0x00},
	{0x0170, 0x3e},
	{0x0171, 0x3e},
	{0x0172, 0x3e},
	{0x0173, 0x3e},
	{0x0428, 0x0b},
	{0x0429, 0x0b},
	{0x042a, 0x0b},
	{0x042b, 0x0b},
	{0x042c, 0x0b},
	{0x042d, 0x0b},
	{0x042e, 0x0b},
	{0x042f, 0x0b},
	{0x0430, 0x05},
	{0x0431, 0x05},
	{0x0432, 0x05},
	{0x0433, 0x05},
	{0x0434, 0x04},
	{0x0435, 0x04},
	{0x0436, 0x04},
	{0x0437, 0x04},
	{0x0438, 0x18},
	{0x0439, 0x18},
	{0x043a, 0x18},
	{0x043b, 0x18},
	{0x043c, 0x1d},
	{0x043d, 0x20},
	{0x043e, 0x22},
	{0x043f, 0x24},
	{0x0468, 0x04},
	{0x0469, 0x04},
	{0x046a, 0x04},
	{0x046b, 0x04},
	{0x046c, 0x04},
	{0x046d, 0x04},
	{0x046e, 0x04},
	{0x046f, 0x04},
	{0x0108, 0xf0},
	{0x0109, 0x80},
	{0x0d03, 0x05},
	{0x0d04, 0x00},
	{0x007a, 0x60},
	{0x00d0, 0x00},
	{0x0080, 0x09},
	{0x0291, 0x0f},
	{0x0292, 0xff},
	{0x0201, 0x27},
	{0x0202, 0x53},
	{0x0203, 0x4e},
	{0x0206, 0x03},
	{0x0212, 0x0b},
	{0x0213, 0x40},
	{0x0215, 0x10},
	{0x03fe, 0x10},
	{0x0183, 0x09},
	{0x0187, 0x51},
	{0x0d22, 0x01},
	{0x0d21, 0x2c},
	{0x0d03, 0x00},
	{0x0d04, 0x40},
	{0x0d23, 0x0e},
	{0x03fe, 0x00},
	{REG_NULL, 0x00},
};

static const struct regval gc3003_linear_10_1920x528_regs[] = {
	{0x03fe, 0xf0},
	{0x03fe, 0xf0},
	{0x03fe, 0xf0},
	{0x03fe, 0x00},
	{0x03f3, 0x00},
	{0x03f5, 0xc0},
	{0x03f6, 0x06},
	{0x03f7, 0x01},
	{0x03f8, 0x46},
	{0x03f9, 0x13},
	{0x03fa, 0x00},
	{0x03e0, 0x16},
	{0x03e1, 0x0d},
	{0x03e2, 0x30},
	{0x03e4, 0x08},
	{0x03fc, 0xce},
	{0x0d05, 0x05},
	{0x0d06, 0x40},
	{0x0d76, 0x00},
	{0x0d41, 0x02},
	{0x0d42, 0x3c},
	{0x0d09, 0x01},
	{0x0d0a, 0x7a},
	{0x000c, 0x02},
	{0x0d0d, 0x02},
	{0x0d0e, 0x10},//528
	{0x000f, 0x09},
	{0x0010, 0x08},
	{0x0017, 0x0c},
	{0x0d53, 0x12},
	{0x0051, 0x03},
	{0x0082, 0x01},
	{0x0086, 0x20},
	{0x008a, 0x01},
	{0x008b, 0x1d},
	{0x008c, 0x05},
	{0x008d, 0xd0},
	{0x0db7, 0x01},
	{0x0db0, 0x05},
	{0x0db1, 0x00},
	{0x0db2, 0x04},
	{0x0db3, 0x54},
	{0x0db4, 0x00},
	{0x0db5, 0x17},
	{0x0db6, 0x08},
	{0x0d25, 0xcb},
	{0x0d4a, 0x04},
	{0x00d2, 0x70},
	{0x00d7, 0x19},
	{0x00d9, 0x10},
	{0x00da, 0xc1},
	{0x0d55, 0x1b},
	{0x0d92, 0x17},
	{0x0dc2, 0x30},
	{0x0d2a, 0x30},
	{0x0d19, 0x51},
	{0x0d29, 0x30},
	{0x0d20, 0x30},
	{0x0d72, 0x12},
	{0x0d4e, 0x12},
	{0x0d43, 0x20},
	{0x0050, 0x0c},
	{0x006e, 0x03},
	{0x0153, 0x50},
	{0x0192, 0x00},
	{0x0193, 0x00},
	{0x0194, 0xc0},
	{0x0195, 0x02},
	{0x0196, 0x1c},
	{0x0197, 0x07},
	{0x0198, 0x80},
	{0x0077, 0x01},
	{0x0078, 0x65},
	{0x0079, 0x04},
	{0x0067, 0xc0},
	{0x0054, 0xff},
	{0x0055, 0x02},
	{0x0056, 0x00},
	{0x0057, 0x04},
	{0x005a, 0xff},
	{0x005b, 0x07},
	{0x00d5, 0x03},
	{0x0102, 0x10},
	{0x0d4a, 0x04},
	{0x04e0, 0xff},
	{0x031e, 0x3e},
	{0x0159, 0x01},
	{0x014f, 0x28},
	{0x0150, 0x40},
	{0x0026, 0x00},
	{0x0d26, 0xa0},
	{0x0414, 0x77},
	{0x0415, 0x77},
	{0x0416, 0x77},
	{0x0417, 0x77},
	{0x0155, 0x00},
	{0x0170, 0x3e},
	{0x0171, 0x3e},
	{0x0172, 0x3e},
	{0x0173, 0x3e},
	{0x0428, 0x0b},
	{0x0429, 0x0b},
	{0x042a, 0x0b},
	{0x042b, 0x0b},
	{0x042c, 0x0b},
	{0x042d, 0x0b},
	{0x042e, 0x0b},
	{0x042f, 0x0b},
	{0x0430, 0x05},
	{0x0431, 0x05},
	{0x0432, 0x05},
	{0x0433, 0x05},
	{0x0434, 0x04},
	{0x0435, 0x04},
	{0x0436, 0x04},
	{0x0437, 0x04},
	{0x0438, 0x18},
	{0x0439, 0x18},
	{0x043a, 0x18},
	{0x043b, 0x18},
	{0x043c, 0x1d},
	{0x043d, 0x20},
	{0x043e, 0x22},
	{0x043f, 0x24},
	{0x0468, 0x04},
	{0x0469, 0x04},
	{0x046a, 0x04},
	{0x046b, 0x04},
	{0x046c, 0x04},
	{0x046d, 0x04},
	{0x046e, 0x04},
	{0x046f, 0x04},
	{0x0108, 0xf0},
	{0x0109, 0x80},
	{0x0d03, 0x05},
	{0x0d04, 0x00},
	{0x007a, 0x60},
	{0x00d0, 0x00},
	{0x0080, 0x09},
	{0x0291, 0x0f},
	{0x0292, 0xff},
	{0x0201, 0x27},
	{0x0202, 0x53},
	{0x0203, 0x4e},
	{0x0206, 0x03},
	{0x0212, 0x0b},
	{0x0213, 0x40},
	{0x0215, 0x12},
	{0x023e, 0x99},
	{0x03fe, 0x10},
	{0x0183, 0x09},
	{0x0187, 0x51},
	{0x0d22, 0x01},
	{0x0d21, 0x2c},
	{0x0d03, 0x01},
	{0x0d04, 0x00},
	{0x0d23, 0x0e},
	{0x03fe, 0x00},
	{REG_NULL, 0x00},
};

static const struct regval gc3003_stream_on_regs[] = {
	{0x0201, 0x27},
	{0x0202, 0x53},
	{0x0203, 0x4e},
	{0x0206, 0x03},
	{0x0212, 0x0b},
	{0x0213, 0x40},
	{0x0215, 0x10},
	{0x023e, 0x99},
	{0x03fe, 0x00},
	{REG_NULL, 0x00},
};

static const struct regval gc3003_stand_by_regs[] = {
	{0x023e, 0x00},
	{0x03f7, 0x00},
	{0x03fc, 0x01},
	{0x03f9, 0x01},
	{REG_NULL, 0x00},
};

static const struct gc3003_mode supported_modes[] = {
	{
		.width = 2304,
		.height = 1298,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0500,
		.hts_def = 0x540 * 2,
		.vts_def = 0x053c,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.reg_list = gc3003_linear_10_2304x1298_regs,
		.stream_on_reg_list = gc3003_stream_on_regs,
		.stand_by_reg_list = gc3003_stand_by_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 320,
		.height = 240,
		.max_fps = {
			.numerator = 10000,
			.denominator = 1200000,
		},
		.exp_def = 0x0100,
		.hts_def = 0x05dc * 2,
		.vts_def = 0x12c,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.reg_list = gc3003_linear_10_320x240_regs,
		.stream_on_reg_list = gc3003_stream_on_regs,
		.stand_by_reg_list = gc3003_stand_by_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 1920,
		.height = 528,
		.max_fps = {
			.numerator = 10000,
			.denominator = 700000,
		},
		.exp_def = 0x0100,
		.hts_def = 0x05dc * 2,
		.vts_def = 0x23c,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.reg_list = gc3003_linear_10_1920x528_regs,
		.stream_on_reg_list = gc3003_stream_on_regs,
		.stand_by_reg_list = gc3003_stand_by_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	GC3003_LINK_FREQ_LINEAR,
};

static const char * const gc3003_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int gc3003_write_reg(struct i2c_client *client, u16 reg,
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

static int gc3003_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = gc3003_write_reg(client, regs[i].addr,
					GC3003_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int gc3003_read_reg(struct i2c_client *client, u16 reg,
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

static int gc3003_get_reso_dist(const struct gc3003_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct gc3003_mode *
gc3003_find_best_fit(struct gc3003 *gc3003, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < gc3003->cfg_num; i++) {
		dist = gc3003_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int gc3003_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct gc3003 *gc3003 = to_gc3003(sd);
	const struct gc3003_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&gc3003->mutex);

	mode = gc3003_find_best_fit(gc3003, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&gc3003->mutex);
		return -ENOTTY;
#endif
	} else {
		gc3003->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(gc3003->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(gc3003->vblank, vblank_def,
					 GC3003_VTS_MAX - mode->height,
					 1, vblank_def);

		gc3003->cur_link_freq = 0;
		gc3003->cur_pixel_rate = GC3003_PIXEL_RATE_LINEAR;

		__v4l2_ctrl_s_ctrl_int64(gc3003->pixel_rate,
					 gc3003->cur_pixel_rate);
		__v4l2_ctrl_s_ctrl(gc3003->link_freq,
				   gc3003->cur_link_freq);
		gc3003->cur_vts = mode->vts_def;
	}

	mutex_unlock(&gc3003->mutex);

	return 0;
}

static int gc3003_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct gc3003 *gc3003 = to_gc3003(sd);
	const struct gc3003_mode *mode = gc3003->cur_mode;

	mutex_lock(&gc3003->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&gc3003->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&gc3003->mutex);

	return 0;
}

static int gc3003_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct gc3003 *gc3003 = to_gc3003(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = gc3003->cur_mode->bus_fmt;

	return 0;
}

static int gc3003_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	struct gc3003 *gc3003 = to_gc3003(sd);

	if (fse->index >= gc3003->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[0].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int gc3003_enable_test_pattern(struct gc3003 *gc3003, u32 pattern)
{
	u32 val;

	if (pattern)
		val = GC3003_TEST_PATTERN_ENABLE;
	else
		val = GC3003_TEST_PATTERN_DISABLE;

	return gc3003_write_reg(gc3003->client, GC3003_REG_TEST_PATTERN,
				GC3003_REG_VALUE_08BIT, val);
}
static int gc3003_set_gain_reg(struct gc3003 *gc3003, u32 gain)
{
	int i;
	int total;
	u32 tol_dig_gain = 0;

	if (gain < 64)
		gain = 64;
	total = sizeof(gain_level_table) / sizeof(u32) - 1; // why -1
	for (i = 0; i < total; i++) {
		if (gain_level_table[i] <= gain &&
		    gain < gain_level_table[i + 1])
			break;
	}
	tol_dig_gain = gain * 64 / gain_level_table[i];
	if (i >= total)
		i = total - 1;

	gc3003_write_reg(gc3003->client, 0x0d1,
			 GC3003_REG_VALUE_08BIT, reg_val_table_liner[i][0]);
	gc3003_write_reg(gc3003->client, 0x0d0,
			 GC3003_REG_VALUE_08BIT, reg_val_table_liner[i][1]);
	gc3003_write_reg(gc3003->client, 0x080,
			 GC3003_REG_VALUE_08BIT, reg_val_table_liner[i][2]);
	gc3003_write_reg(gc3003->client, 0x155,
			 GC3003_REG_VALUE_08BIT, reg_val_table_liner[i][3]);
	gc3003_write_reg(gc3003->client, 0x0b8,
			 GC3003_REG_VALUE_08BIT, reg_val_table_liner[i][4]);
	gc3003_write_reg(gc3003->client, 0x0b9,
			 GC3003_REG_VALUE_08BIT, reg_val_table_liner[i][5]);
	gc3003_write_reg(gc3003->client, 0x0b1,
			 GC3003_REG_VALUE_08BIT, (tol_dig_gain >> 6));
	gc3003_write_reg(gc3003->client, 0x0b2,
			 GC3003_REG_VALUE_08BIT, ((tol_dig_gain & 0x3f)<<2));

	return 0;
}
static int gc3003_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct gc3003 *gc3003 = to_gc3003(sd);
	const struct gc3003_mode *mode = gc3003->cur_mode;

	mutex_lock(&gc3003->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&gc3003->mutex);

	return 0;
}

static int gc3003_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct gc3003 *gc3003 = to_gc3003(sd);
	const struct gc3003_mode *mode = gc3003->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (GC3003_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void gc3003_get_module_inf(struct gc3003 *gc3003,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, GC3003_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, gc3003->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, gc3003->len_name, sizeof(inf->base.lens));
}

static int gc3003_get_channel_info(struct gc3003 *gc3003, struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = gc3003->cur_mode->vc[ch_info->index];
	ch_info->width = gc3003->cur_mode->width;
	ch_info->height = gc3003->cur_mode->height;
	ch_info->bus_fmt = gc3003->cur_mode->bus_fmt;
	return 0;
}

static long gc3003_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct gc3003 *gc3003 = to_gc3003(sd);
	struct rkmodule_hdr_cfg *hdr;
	u32 i, h, w;
	long ret = 0;
	u32 stream = 0;
	struct rkmodule_channel_info *ch_info;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		gc3003_get_module_inf(gc3003, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = gc3003->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = gc3003->cur_mode->width;
		h = gc3003->cur_mode->height;
		for (i = 0; i < gc3003->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				gc3003->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == gc3003->cfg_num) {
			dev_err(&gc3003->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = gc3003->cur_mode->hts_def -
			    gc3003->cur_mode->width;
			h = gc3003->cur_mode->vts_def -
			    gc3003->cur_mode->height;
			__v4l2_ctrl_modify_range(gc3003->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(gc3003->vblank, h,
						 GC3003_VTS_MAX -
						 gc3003->cur_mode->height,
						 1, h);
			gc3003->cur_link_freq = 0;
			gc3003->cur_pixel_rate = GC3003_PIXEL_RATE_LINEAR;

		__v4l2_ctrl_s_ctrl_int64(gc3003->pixel_rate,
					 gc3003->cur_pixel_rate);
		__v4l2_ctrl_s_ctrl(gc3003->link_freq,
				   gc3003->cur_link_freq);
		gc3003->cur_vts = gc3003->cur_mode->vts_def;
		}
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = gc3003_write_reg(gc3003->client, GC3003_REG_CTRL_MODE,
				GC3003_REG_VALUE_08BIT, GC3003_MODE_STREAMING);
		else
			ret = gc3003_write_reg(gc3003->client, GC3003_REG_CTRL_MODE,
				GC3003_REG_VALUE_08BIT, GC3003_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = gc3003_get_channel_info(gc3003, ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long gc3003_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = gc3003_ioctl(sd, cmd, inf);
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
			ret = gc3003_ioctl(sd, cmd, cfg);
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

		ret = gc3003_ioctl(sd, cmd, hdr);
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
			ret = gc3003_ioctl(sd, cmd, hdr);
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
			ret = gc3003_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = gc3003_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = gc3003_ioctl(sd, cmd, ch_info);
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

static int __gc3003_start_stream(struct gc3003 *gc3003)
{
	int ret;

	if (!gc3003->is_thunderboot) {
		ret = gc3003_write_array(gc3003->client, gc3003->cur_mode->reg_list);
		if (ret)
			return ret;

		/* In case these controls are set before streaming */
		ret = __v4l2_ctrl_handler_setup(&gc3003->ctrl_handler);
		if (ret)
			return ret;
		if (gc3003->has_init_exp && gc3003->cur_mode->hdr_mode != NO_HDR) {
			ret = gc3003_ioctl(&gc3003->subdev, PREISP_CMD_SET_HDRAE_EXP,
				&gc3003->init_hdrae_exp);
			if (ret) {
				dev_err(&gc3003->client->dev,
					"init exp fail in hdr mode\n");
				return ret;
			}
		}
	}

	ret = gc3003_write_array(gc3003->client, gc3003->cur_mode->stream_on_reg_list);

	return ret;
}

static int __gc3003_stop_stream(struct gc3003 *gc3003)
{
	int ret;

	gc3003->has_init_exp = false;
	if (gc3003->is_thunderboot) {
		gc3003->is_first_streamoff = true;
		pm_runtime_put(&gc3003->client->dev);
	}
	ret = gc3003_write_array(gc3003->client, gc3003->cur_mode->stand_by_reg_list);

	return ret;
}

static int __gc3003_power_on(struct gc3003 *gc3003);
static int gc3003_s_stream(struct v4l2_subdev *sd, int on)
{
	struct gc3003 *gc3003 = to_gc3003(sd);
	struct i2c_client *client = gc3003->client;
	int ret = 0;

	mutex_lock(&gc3003->mutex);
	on = !!on;
	if (on == gc3003->streaming)
		goto unlock_and_return;

	if (on) {
		if (gc3003->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
			gc3003->is_thunderboot = false;
			__gc3003_power_on(gc3003);
		}
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __gc3003_start_stream(gc3003);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__gc3003_stop_stream(gc3003);
		pm_runtime_put(&client->dev);
	}

	gc3003->streaming = on;

unlock_and_return:
	mutex_unlock(&gc3003->mutex);

	return ret;
}

static int gc3003_s_power(struct v4l2_subdev *sd, int on)
{
	struct gc3003 *gc3003 = to_gc3003(sd);
	struct i2c_client *client = gc3003->client;
	int ret = 0;

	mutex_lock(&gc3003->mutex);

	/* If the power state is not modified - no work to do. */
	if (gc3003->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		if (!gc3003->is_thunderboot) {
			ret = gc3003_write_array(gc3003->client, gc3003_global_regs);
			if (ret) {
				v4l2_err(sd, "could not set init registers\n");
				pm_runtime_put_noidle(&client->dev);
				goto unlock_and_return;
			}
		}

		gc3003->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		gc3003->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&gc3003->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 gc3003_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, GC3003_XVCLK_FREQ / 1000 / 1000);
}

static int __gc3003_power_on(struct gc3003 *gc3003)
{
	int ret;
	u32 delay_us;
	struct device *dev = &gc3003->client->dev;

	if (!IS_ERR_OR_NULL(gc3003->pins_default)) {
		ret = pinctrl_select_state(gc3003->pinctrl,
					   gc3003->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(gc3003->xvclk, GC3003_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(gc3003->xvclk) != GC3003_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(gc3003->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (gc3003->is_thunderboot)
		return 0;

	if (!IS_ERR(gc3003->reset_gpio))
		gpiod_set_value_cansleep(gc3003->reset_gpio, 0);

	if (!IS_ERR(gc3003->pwdn_gpio))
		gpiod_set_value_cansleep(gc3003->pwdn_gpio, 0);

	usleep_range(500, 1000);
	ret = regulator_bulk_enable(GC3003_NUM_SUPPLIES, gc3003->supplies);

	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(gc3003->pwren_gpio))
		gpiod_set_value_cansleep(gc3003->pwren_gpio, 1);

	usleep_range(1000, 1100);
	if (!IS_ERR(gc3003->pwdn_gpio))
		gpiod_set_value_cansleep(gc3003->pwdn_gpio, 1);
	usleep_range(100, 150);
	if (!IS_ERR(gc3003->reset_gpio))
		gpiod_set_value_cansleep(gc3003->reset_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = gc3003_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(gc3003->xvclk);

	return ret;
}

static void __gc3003_power_off(struct gc3003 *gc3003)
{
	int ret;
	struct device *dev = &gc3003->client->dev;

	clk_disable_unprepare(gc3003->xvclk);
	if (gc3003->is_thunderboot) {
		if (gc3003->is_first_streamoff) {
			gc3003->is_thunderboot = false;
			gc3003->is_first_streamoff = false;
		} else {
			return;
		}
	}

	if (!IS_ERR(gc3003->pwdn_gpio))
		gpiod_set_value_cansleep(gc3003->pwdn_gpio, 0);
	clk_disable_unprepare(gc3003->xvclk);
	if (!IS_ERR(gc3003->reset_gpio))
		gpiod_set_value_cansleep(gc3003->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(gc3003->pins_sleep)) {
		ret = pinctrl_select_state(gc3003->pinctrl,
					   gc3003->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(GC3003_NUM_SUPPLIES, gc3003->supplies);
}

static int gc3003_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc3003 *gc3003 = to_gc3003(sd);

	return __gc3003_power_on(gc3003);
}

static int gc3003_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc3003 *gc3003 = to_gc3003(sd);

	__gc3003_power_off(gc3003);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int gc3003_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct gc3003 *gc3003 = to_gc3003(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct gc3003_mode *def_mode = &supported_modes[0];

	mutex_lock(&gc3003->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&gc3003->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int gc3003_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_interval_enum *fie)
{
	struct gc3003 *gc3003 = to_gc3003(sd);

	if (fie->index >= gc3003->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

#define DST_WIDTH 2304
#define DST_HEIGHT 1296

/*
 * The resolution of the driver configuration needs to be exactly
 * the same as the current output resolution of the sensor,
 * the input width of the isp needs to be 16 aligned,
 * the input height of the isp needs to be 8 aligned.
 * Can be cropped to standard resolution by this function,
 * otherwise it will crop out strange resolution according
 * to the alignment rules.
 */
static int gc3003_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	/*
	 * From "Pixel Array Image Drawing in All scan mode",
	 * there are 12 pixel offset on horizontal and vertical.
	 */
	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = 0;
		sel->r.width = DST_WIDTH;
		sel->r.top = 0;
		sel->r.height = DST_HEIGHT;
		return 0;
	}
	return -EINVAL;
}

static const struct dev_pm_ops gc3003_pm_ops = {
	SET_RUNTIME_PM_OPS(gc3003_runtime_suspend,
			   gc3003_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops gc3003_internal_ops = {
	.open = gc3003_open,
};
#endif

static const struct v4l2_subdev_core_ops gc3003_core_ops = {
	.s_power = gc3003_s_power,
	.ioctl = gc3003_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = gc3003_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops gc3003_video_ops = {
	.s_stream = gc3003_s_stream,
	.g_frame_interval = gc3003_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops gc3003_pad_ops = {
	.enum_mbus_code = gc3003_enum_mbus_code,
	.enum_frame_size = gc3003_enum_frame_sizes,
	.enum_frame_interval = gc3003_enum_frame_interval,
	.get_fmt = gc3003_get_fmt,
	.set_fmt = gc3003_set_fmt,
	.get_selection = gc3003_get_selection,
	.get_mbus_config = gc3003_g_mbus_config,
};

static const struct v4l2_subdev_ops gc3003_subdev_ops = {
	.core	= &gc3003_core_ops,
	.video	= &gc3003_video_ops,
	.pad	= &gc3003_pad_ops,
};

static int gc3003_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc3003 *gc3003 = container_of(ctrl->handler,
					     struct gc3003, ctrl_handler);
	struct i2c_client *client = gc3003->client;
	s64 max;
	int ret = 0;
	int val = 0;

	/*Propagate change of current control to all related controls*/
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/*Update max exposure while meeting expected vblanking*/
		max = gc3003->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(gc3003->exposure,
					 gc3003->exposure->minimum,
					 max,
					 gc3003->exposure->step,
					 gc3003->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = gc3003_write_reg(gc3003->client, GC3003_REG_EXPOSURE_H,
				       GC3003_REG_VALUE_08BIT,
				       ctrl->val >> 8);
		ret |= gc3003_write_reg(gc3003->client, GC3003_REG_EXPOSURE_L,
					GC3003_REG_VALUE_08BIT,
					ctrl->val & 0xff);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = gc3003_set_gain_reg(gc3003, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		gc3003->cur_vts = ctrl->val + gc3003->cur_mode->height;
		ret = gc3003_write_reg(gc3003->client, GC3003_REG_VTS_H,
				       GC3003_REG_VALUE_08BIT,
				       gc3003->cur_vts >> 8);
		ret |= gc3003_write_reg(gc3003->client, GC3003_REG_VTS_L,
					GC3003_REG_VALUE_08BIT,
					gc3003->cur_vts & 0xff);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = gc3003_enable_test_pattern(gc3003, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = gc3003_read_reg(gc3003->client, GC3003_FLIP_MIRROR_REG,
				      GC3003_REG_VALUE_08BIT, &val);
		if (ctrl->val)
			val |= GC3003_MIRROR_BIT_MASK;
		else
			val &= ~GC3003_MIRROR_BIT_MASK;
		ret |= gc3003_write_reg(gc3003->client, GC3003_FLIP_MIRROR_REG,
					GC3003_REG_VALUE_08BIT, val);
		break;
	case V4L2_CID_VFLIP:
		ret = gc3003_read_reg(gc3003->client, GC3003_FLIP_MIRROR_REG,
				      GC3003_REG_VALUE_08BIT, &val);
		if (ctrl->val)
			val |= GC3003_FLIP_BIT_MASK;
		else
			val &= ~GC3003_FLIP_BIT_MASK;
		ret |= gc3003_write_reg(gc3003->client, GC3003_FLIP_MIRROR_REG,
					GC3003_REG_VALUE_08BIT, val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc3003_ctrl_ops = {
	.s_ctrl = gc3003_set_ctrl,
};

static int gc3003_initialize_controls(struct gc3003 *gc3003)
{
	const struct gc3003_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &gc3003->ctrl_handler;
	mode = gc3003->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &gc3003->mutex;

	gc3003->link_freq = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
						   0, 0, link_freq_menu_items);
	gc3003->cur_link_freq = 0;
	gc3003->cur_pixel_rate = GC3003_PIXEL_RATE_LINEAR;

	__v4l2_ctrl_s_ctrl(gc3003->link_freq,
			   gc3003->cur_link_freq);

	gc3003->pixel_rate = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, GC3003_PIXEL_RATE_LINEAR, 1, GC3003_PIXEL_RATE_LINEAR);

	h_blank = mode->hts_def - mode->width;
	gc3003->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					   h_blank, h_blank, 1, h_blank);
	if (gc3003->hblank)
		gc3003->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	gc3003->cur_vts = mode->vts_def;
	gc3003->vblank = v4l2_ctrl_new_std(handler, &gc3003_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_def,
					   GC3003_VTS_MAX - mode->height,
					    1, vblank_def);

	exposure_max = mode->vts_def - 4;
	gc3003->exposure = v4l2_ctrl_new_std(handler, &gc3003_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     GC3003_EXPOSURE_MIN,
					     exposure_max,
					     GC3003_EXPOSURE_STEP,
					     mode->exp_def);

	gc3003->anal_gain = v4l2_ctrl_new_std(handler, &gc3003_ctrl_ops,
					      V4L2_CID_ANALOGUE_GAIN,
					      GC3003_GAIN_MIN,
					      GC3003_GAIN_MAX,
					      GC3003_GAIN_STEP,
					      GC3003_GAIN_DEFAULT);

	gc3003->test_pattern =
		v4l2_ctrl_new_std_menu_items(handler,
					     &gc3003_ctrl_ops,
				V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(gc3003_test_pattern_menu) - 1,
				0, 0, gc3003_test_pattern_menu);

	gc3003->h_flip = v4l2_ctrl_new_std(handler, &gc3003_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	gc3003->v_flip = v4l2_ctrl_new_std(handler, &gc3003_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (handler->error) {
		ret = handler->error;
		dev_err(&gc3003->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	gc3003->subdev.ctrl_handler = handler;
	gc3003->has_init_exp = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int gc3003_check_sensor_id(struct gc3003 *gc3003,
				  struct i2c_client *client)
{
	struct device *dev = &gc3003->client->dev;
	u16 id = 0;
	u32 reg_H = 0;
	u32 reg_L = 0;
	int ret;

	if (gc3003->is_thunderboot) {
		dev_info(dev, "Enable thunderboot mode, skip sensor id check\n");
		return 0;
	}

	ret = gc3003_read_reg(client, GC3003_REG_CHIP_ID_H,
			      GC3003_REG_VALUE_08BIT, &reg_H);
	ret |= gc3003_read_reg(client, GC3003_REG_CHIP_ID_L,
			       GC3003_REG_VALUE_08BIT, &reg_L);

	id = ((reg_H << 8) & 0xff00) | (reg_L & 0xff);
	if (!(reg_H == (CHIP_ID >> 8) || reg_L == (CHIP_ID & 0xff))) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	dev_info(dev, "detected gc%04x sensor\n", id);
	return 0;
}

static int gc3003_configure_regulators(struct gc3003 *gc3003)
{
	unsigned int i;

	for (i = 0; i < GC3003_NUM_SUPPLIES; i++)
		gc3003->supplies[i].supply = gc3003_supply_names[i];

	return devm_regulator_bulk_get(&gc3003->client->dev,
				       GC3003_NUM_SUPPLIES,
				       gc3003->supplies);
}

static int gc3003_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct gc3003 *gc3003;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	gc3003 = devm_kzalloc(dev, sizeof(*gc3003), GFP_KERNEL);
	if (!gc3003)
		return -ENOMEM;

	of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &gc3003->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &gc3003->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &gc3003->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &gc3003->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	gc3003->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);

	gc3003->client = client;
	gc3003->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < gc3003->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			gc3003->cur_mode = &supported_modes[i];
			break;
		}
	}
	if (i == gc3003->cfg_num)
		gc3003->cur_mode = &supported_modes[0];

	gc3003->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(gc3003->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	gc3003->pwren_gpio = devm_gpiod_get(dev, "pwren", GPIOD_ASIS);
	if (IS_ERR(gc3003->pwren_gpio))
		dev_warn(dev, "Failed to get pwren-gpios\n");

	gc3003->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(gc3003->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	gc3003->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_ASIS);
	if (IS_ERR(gc3003->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	gc3003->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(gc3003->pinctrl)) {
		gc3003->pins_default =
			pinctrl_lookup_state(gc3003->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(gc3003->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		gc3003->pins_sleep =
			pinctrl_lookup_state(gc3003->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(gc3003->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = gc3003_configure_regulators(gc3003);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&gc3003->mutex);

	sd = &gc3003->subdev;
	v4l2_i2c_subdev_init(sd, client, &gc3003_subdev_ops);
	ret = gc3003_initialize_controls(gc3003);
	if (ret)
		goto err_destroy_mutex;

	ret = __gc3003_power_on(gc3003);
	if (ret)
		goto err_free_handler;

	usleep_range(3000, 4000);

	ret = gc3003_check_sensor_id(gc3003, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &gc3003_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	gc3003->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &gc3003->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(gc3003->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 gc3003->module_index, facing,
		 GC3003_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	if (gc3003->is_thunderboot)
		pm_runtime_get_sync(dev);
	else
		pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__gc3003_power_off(gc3003);
err_free_handler:
	v4l2_ctrl_handler_free(&gc3003->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&gc3003->mutex);

	return ret;
}

static int gc3003_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc3003 *gc3003 = to_gc3003(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&gc3003->ctrl_handler);
	mutex_destroy(&gc3003->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__gc3003_power_off(gc3003);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id gc3003_of_match[] = {
	{ .compatible = "galaxycore,gc3003" },
	{},
};
MODULE_DEVICE_TABLE(of, gc3003_of_match);
#endif

static const struct i2c_device_id gc3003_match_id[] = {
	{ "galaxycore,gc3003", 0 },
	{ },
};

static struct i2c_driver gc3003_i2c_driver = {
	.driver = {
		.name = GC3003_NAME,
		.pm = &gc3003_pm_ops,
		.of_match_table = of_match_ptr(gc3003_of_match),
	},
	.probe		= &gc3003_probe,
	.remove		= &gc3003_remove,
	.id_table	= gc3003_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&gc3003_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&gc3003_i2c_driver);
}

#if defined(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP) && !defined(CONFIG_INITCALL_ASYNC)
subsys_initcall(sensor_mod_init);
#else
device_initcall_sync(sensor_mod_init);
#endif
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("galaxycore gc3003 sensor driver");
MODULE_LICENSE("GPL");
