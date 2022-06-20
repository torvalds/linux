// SPDX-License-Identifier: GPL-2.0
/*
 * s5kjn1 driver
 *
 * Copyright (C) 2022 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 init version.
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
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>
#include <linux/pinctrl/consumer.h>
#include <linux/rk-preisp.h>
#include "../platform/rockchip/isp/rkisp_tb_helper.h"
#include <linux/of_graph.h>
#include "otp_eeprom.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIPI_FREQ_696M			696000000
#define MIPI_FREQ_828M			828000000

#define PIXEL_RATE_WITH_828M		(MIPI_FREQ_828M / 10 * 4 * 2)

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define S5KJN1_XVCLK_FREQ		24000000

#define CHIP_ID				0x38E1
#define S5KJN1_REG_CHIP_ID		0x0000

#define S5KJN1_REG_CTRL_MODE		0x0100
#define S5KJN1_MODE_SW_STANDBY		0x0
#define S5KJN1_MODE_STREAMING		BIT(0)

#define	S5KJN1_EXPOSURE_MIN		8
#define	S5KJN1_EXPOSURE_STEP		1
#define S5KJN1_VTS_MAX			0xffff

#define S5KJN1_REG_EXP_LONG_H		0x0202

#define S5KJN1_REG_AGAIN_LONG_H		0x0204
#define S5KJN1_GAIN_MIN			0x20
#define S5KJN1_GAIN_MAX			0x800
#define S5KJN1_GAIN_STEP		1
#define S5KJN1_GAIN_DEFAULT		0xc0

#define S5KJN1_GROUP_UPDATE_ADDRESS	0x0104
#define S5KJN1_GROUP_UPDATE_START_DATA	0x01
#define S5KJN1_GROUP_UPDATE_END_LAUNCH	0x00

#define S5KJN1_SOFTWARE_RESET_REG	0x0103

#define S5KJN1_REG_TEST_PATTERN		0x0600
#define S5KJN1_TEST_PATTERN_ENABLE	0x01
#define S5KJN1_TEST_PATTERN_DISABLE	0x0

#define S5KJN1_REG_VTS			0x0340

#define REG_NULL			0xFFFF
#define DELAY_MS			0xEEEE	/* Array delay token */

#define S5KJN1_REG_VALUE_08BIT		1
#define S5KJN1_REG_VALUE_16BIT		2
#define S5KJN1_REG_VALUE_24BIT		3

#define S5KJN1_LANES			4

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define S5KJN1_NAME			"s5kjn1"

static const char * const s5kjn1_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define S5KJN1_NUM_SUPPLIES ARRAY_SIZE(s5kjn1_supply_names)

struct regval {
	u16 addr;
	u16 val;
};

struct other_data {
	u32 width;
	u32 height;
	u32 bus_fmt;
	u32 data_type;
	u32 data_bit;
};

struct s5kjn1_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 mipi_freq_idx;
	u32 bpp;
	const struct regval *reg_list;
	u32 hdr_mode;
	const struct other_data *spd;
	u32 vc[PAD_MAX];
};

struct s5kjn1 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[S5KJN1_NUM_SUPPLIES];

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
	const struct s5kjn1_mode *cur_mode;
	const struct s5kjn1_mode *support_modes;
	u32			cfg_num;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	struct v4l2_fwnode_endpoint bus_cfg;
	bool			is_thunderboot;
	bool			is_thunderboot_ng;
	bool			is_first_streamoff;
	struct otp_info		*otp;
	u32			spd_id;
};

#define to_s5kjn1(sd) container_of(sd, struct s5kjn1, subdev)

static const struct regval s5kjn1_10bit_4080x3072_dphy_30fps_regs[] = {
	{0x6028, 0x4000}, // Page pointer HW
	{0x0000, 0x0003}, // Setfile Version
	{0x0000, 0x38E1}, // JN1( Sensor ID)
	{0x001E, 0x0007}, // V07

	{0x6028, 0x4000}, // Init setting
	{0x6010, 0x0001},
	//p5
	{0x6226, 0x0001},
	//p10

	{0x6028, 0x2400}, //Global, Analog setting
	{0x602A, 0x1354},
	{0x6F12, 0x0100},
	{0x6F12, 0x7017},
	{0x602A, 0x13B2},
	{0x6F12, 0x0000},
	{0x602A, 0x1236},
	{0x6F12, 0x0000},
	{0x602A, 0x1A0A},
	{0x6F12, 0x4C0A},
	{0x602A, 0x2210},
	{0x6F12, 0x3401},
	{0x602A, 0x2176},
	{0x6F12, 0x6400},
	{0x602A, 0x222E},
	{0x6F12, 0x0001},
	{0x602A, 0x06B6},
	{0x6F12, 0x0A00},
	{0x602A, 0x06BC},
	{0x6F12, 0x1001},
	{0x602A, 0x2140},
	{0x6F12, 0x0101},
	{0x602A, 0x1A0E},
	{0x6F12, 0x9600},
	{0x6028, 0x4000},
	{0xF44E, 0x0011},
	{0xF44C, 0x0B0B},
	{0xF44A, 0x0006},
	{0x0118, 0x0002},
	{0x011A, 0x0001},

	{0x6028, 0x2400}, // Mode setting
	{0x602A, 0x1A28},
	{0x6F12, 0x4C00},
	{0x602A, 0x065A},
	{0x6F12, 0x0000},
	{0x602A, 0x139E},
	{0x6F12, 0x0100},
	{0x602A, 0x139C},
	{0x6F12, 0x0000},
	{0x602A, 0x13A0},
	{0x6F12, 0x0A00},
	{0x6F12, 0x0120},
	{0x602A, 0x2072},
	{0x6F12, 0x0000},
	{0x602A, 0x1A64},
	{0x6F12, 0x0301},
	{0x6F12, 0xFF00},
	{0x602A, 0x19E6},
	{0x6F12, 0x0200},
	{0x602A, 0x1A30},
	{0x6F12, 0x3401},
	{0x602A, 0x19FC},
	{0x6F12, 0x0B00},
	{0x602A, 0x19F4},
	{0x6F12, 0x0606},
	{0x602A, 0x19F8},
	{0x6F12, 0x1010},
	{0x602A, 0x1B26},
	{0x6F12, 0x6F80},
	{0x6F12, 0xA060},
	{0x602A, 0x1A3C},
	{0x6F12, 0x6207},
	{0x602A, 0x1A48},
	{0x6F12, 0x6207},
	{0x602A, 0x1444},
	{0x6F12, 0x2000},
	{0x6F12, 0x2000},
	{0x602A, 0x144C},
	{0x6F12, 0x3F00},
	{0x6F12, 0x3F00},
	{0x602A, 0x7F6C},
	{0x6F12, 0x0100},
	{0x6F12, 0x2F00},
	{0x6F12, 0xFA00},
	{0x6F12, 0x2400},
	{0x6F12, 0xE500},
	{0x602A, 0x0650},
	{0x6F12, 0x0600},
	{0x602A, 0x0654},
	{0x6F12, 0x0000},
	{0x602A, 0x1A46},
	{0x6F12, 0x8A00},
	{0x602A, 0x1A52},
	{0x6F12, 0xBF00},
	{0x602A, 0x0674},
	{0x6F12, 0x0500},
	{0x6F12, 0x0500},
	{0x6F12, 0x0500},
	{0x6F12, 0x0500},
	{0x602A, 0x0668},
	{0x6F12, 0x0800},
	{0x6F12, 0x0800},
	{0x6F12, 0x0800},
	{0x6F12, 0x0800},
	{0x602A, 0x0684},
	{0x6F12, 0x4001},
	{0x602A, 0x0688},
	{0x6F12, 0x4001},
	{0x602A, 0x147C},
	{0x6F12, 0x1000},
	{0x602A, 0x1480},
	{0x6F12, 0x1000},
	{0x602A, 0x19F6},
	{0x6F12, 0x0904},
	{0x602A, 0x0812},
	{0x6F12, 0x0000},
	{0x602A, 0x1A02},
	{0x6F12, 0x1800},
	{0x602A, 0x2148},
	{0x6F12, 0x0100},
	{0x602A, 0x2042},
	{0x6F12, 0x1A00},
	{0x602A, 0x0874},
	{0x6F12, 0x0100},
	{0x602A, 0x09C0},
	{0x6F12, 0x2008},
	{0x602A, 0x09C4},
	{0x6F12, 0x2000},
	{0x602A, 0x19FE},
	{0x6F12, 0x0E1C},
	{0x602A, 0x4D92},
	{0x6F12, 0x0100},
	{0x602A, 0x84C8},
	{0x6F12, 0x0100},
	{0x602A, 0x4D94},
	{0x6F12, 0x0005},
	{0x6F12, 0x000A},
	{0x6F12, 0x0010},
	{0x6F12, 0x0810},
	{0x6F12, 0x000A},
	{0x6F12, 0x0040},
	{0x6F12, 0x0810},
	{0x6F12, 0x0810},
	{0x6F12, 0x8002},
	{0x6F12, 0xFD03},
	{0x6F12, 0x0010},
	{0x6F12, 0x1510},
	{0x602A, 0x3570},
	{0x6F12, 0x0000},
	{0x602A, 0x3574},
	{0x6F12, 0x1201},
	{0x602A, 0x21E4},
	{0x6F12, 0x0400},
	{0x602A, 0x21EC},
	{0x6F12, 0x1F04},
	{0x602A, 0x2080},
	{0x6F12, 0x0101},
	{0x6F12, 0xFF00},
	{0x6F12, 0x7F01},
	{0x6F12, 0x0001},
	{0x6F12, 0x8001},
	{0x6F12, 0xD244},
	{0x6F12, 0xD244},
	{0x6F12, 0x14F4},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x602A, 0x20BA},
	{0x6F12, 0x141C},
	{0x6F12, 0x111C},
	{0x6F12, 0x54F4},
	{0x602A, 0x120E},
	{0x6F12, 0x1000},
	{0x602A, 0x212E},
	{0x6F12, 0x0200},
	{0x602A, 0x13AE},
	{0x6F12, 0x0101},
	{0x602A, 0x0718},
	{0x6F12, 0x0001},
	{0x602A, 0x0710},
	{0x6F12, 0x0002},
	{0x6F12, 0x0804},
	{0x6F12, 0x0100},
	{0x602A, 0x1B5C},
	{0x6F12, 0x0000},
	{0x602A, 0x0786},
	{0x6F12, 0x7701},
	{0x602A, 0x2022},
	{0x6F12, 0x0500},
	{0x6F12, 0x0500},
	{0x602A, 0x1360},
	{0x6F12, 0x0100},
	{0x602A, 0x1376},
	{0x6F12, 0x0100},
	{0x6F12, 0x6038},
	{0x6F12, 0x7038},
	{0x6F12, 0x8038},
	{0x602A, 0x1386},
	{0x6F12, 0x0B00},
	{0x602A, 0x06FA},
	{0x6F12, 0x1000},
	{0x602A, 0x4A94},
	{0x6F12, 0x0900},
	{0x6F12, 0x0000},
	{0x6F12, 0x0300},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0300},
	{0x6F12, 0x0000},
	{0x6F12, 0x0900},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x602A, 0x0A76},
	{0x6F12, 0x1000},
	{0x602A, 0x0AEE},
	{0x6F12, 0x1000},
	{0x602A, 0x0B66},
	{0x6F12, 0x1000},
	{0x602A, 0x0BDE},
	{0x6F12, 0x1000},
	{0x602A, 0x0BE8},
	{0x6F12, 0x3000},
	{0x6F12, 0x3000},
	{0x602A, 0x0C56},
	{0x6F12, 0x1000},
	{0x602A, 0x0C60},
	{0x6F12, 0x3000},
	{0x6F12, 0x3000},
	{0x602A, 0x0CB6},
	{0x6F12, 0x0100},
	{0x602A, 0x0CF2},
	{0x6F12, 0x0001},
	{0x602A, 0x0CF0},
	{0x6F12, 0x0101},
	{0x602A, 0x11B8},
	{0x6F12, 0x0100},
	{0x602A, 0x11F6},
	{0x6F12, 0x0020},
	{0x602A, 0x4A74},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0xD8FF},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0xD8FF},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x602A, 0x218E},
	{0x6F12, 0x0000},
	{0x602A, 0x2268},
	{0x6F12, 0xF279},
	{0x602A, 0x5006},
	{0x6F12, 0x0000},
	{0x602A, 0x500E},
	{0x6F12, 0x0100},
	{0x602A, 0x4E70},
	{0x6F12, 0x2062},
	{0x6F12, 0x5501},
	{0x602A, 0x06DC},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6028, 0x4000},
	{0xF46A, 0xAE80},
	{0x0344, 0x0000}, //x_addr_start
	{0x0346, 0x0000}, //y_addr_start
	{0x0348, 0x1FFF}, //x_addr_end
	{0x034A, 0x181F}, //y_addr_end
	{0x034C, 0x0FF0}, //output width
	{0x034E, 0x0C00}, //output height
	{0x0350, 0x0008},
	{0x0352, 0x0008},
	{0x0900, 0x0122},
	{0x0380, 0x0002},
	{0x0382, 0x0002},
	{0x0384, 0x0002},
	{0x0386, 0x0002},
	{0x0110, 0x1002},
	{0x0114, 0x0301},
	{0x0116, 0x3000},
	{0x0136, 0x1800},
	{0x013E, 0x0000},
	{0x0300, 0x0006},
	{0x0302, 0x0001},
	{0x0304, 0x0004},
	{0x0306, 0x008C},
	{0x0308, 0x0008},
	{0x030A, 0x0001},
	{0x030C, 0x0000},
	{0x030E, 0x0004},
	{0x0310, 0x008A},
	{0x0312, 0x0000},
	{0x080E, 0x0000},
	{0x0340, 0x0FD6},
	{0x0342, 0x11E8},
	{0x0702, 0x0000},
	{0x0202, 0x0f00},
	{0x0200, 0x0100},
	{0x0D00, 0x0101},
	{0x0D02, 0x0101},
	{0x0D04, 0x0102},
	{0x6226, 0x0000},

	//{0x0100, 0x0100},	// Streaming on
	{REG_NULL, 0x00},
};

/* Attention: only support Tetra-Bayer pattern  output*/
static const struct regval s5kjn1_10bit_8128x6144_dphy_10fps_regs[] = {
	{0x6028, 0x4000}, // Page pointer HW
	{0x0000, 0x0003}, // Setfile Version
	{0x0000, 0x38E1}, // JN1( Sensor ID)
	{0x001E, 0x0007}, // V07

	{0x6028, 0x4000}, // Init setting
	{0x6010, 0x0001},
	//p5
	{0x6226, 0x0001},
	//p10
	{0x6028, 0x2400}, //Global, Analog setting
	{0x11d2, 0x00FF}, //Global, Analog setting

	{0x602A, 0x1354},
	{0x6F12, 0x0100},
	{0x6F12, 0x7017},
	{0x602A, 0x13B2},
	{0x6F12, 0x0000},
	{0x602A, 0x1236},
	{0x6F12, 0x0000},
	{0x602A, 0x1A0A},
	{0x6F12, 0x4C0A},
	{0x602A, 0x2210},
	{0x6F12, 0x3401},
	{0x602A, 0x2176},
	{0x6F12, 0x6400},
	{0x602A, 0x222E},
	{0x6F12, 0x0001},
	{0x602A, 0x06B6},
	{0x6F12, 0x0A00},
	{0x602A, 0x06BC},
	{0x6F12, 0x1001},
	{0x602A, 0x2140},
	{0x6F12, 0x0101},
	{0x602A, 0x1A0E},
	{0x6F12, 0x9600},
	{0x6028, 0x4000},
	{0xF44E, 0x0011},
	{0xF44C, 0x0B0B},
	{0xF44A, 0x0006},
	{0x0118, 0x0002},
	{0x011A, 0x0001},

	{0x6028, 0x2400}, // Mode setting
	{0x602A, 0x1A28},
	{0x6F12, 0x4C00},
	{0x602A, 0x065A},
	{0x6F12, 0x0000},
	{0x602A, 0x139E},
	{0x6F12, 0x0400},
	{0x602A, 0x139C},
	{0x6F12, 0x0100},
	{0x602A, 0x13A0},
	{0x6F12, 0x0500},
	{0x6F12, 0x0120},
	{0x602A, 0x2072},
	{0x6F12, 0x0101},
	{0x602A, 0x1A64},
	{0x6F12, 0x0001},
	{0x6F12, 0x0000},
	{0x602A, 0x19E6},
	{0x6F12, 0x0200},
	{0x602A, 0x1A30},
	{0x6F12, 0x3403},
	{0x602A, 0x19FC},
	{0x6F12, 0x0700},
	{0x602A, 0x19F4},
	{0x6F12, 0x0707},
	{0x602A, 0x19F8},
	{0x6F12, 0x0B0B},
	{0x602A, 0x1B26},
	{0x6F12, 0x6F80},
	{0x6F12, 0xA060},
	{0x602A, 0x1A3C},
	{0x6F12, 0x8207},
	{0x602A, 0x1A48},
	{0x6F12, 0x8207},
	{0x602A, 0x1444},
	{0x6F12, 0x2000},
	{0x6F12, 0x2000},
	{0x602A, 0x144C},
	{0x6F12, 0x3F00},
	{0x6F12, 0x3F00},
	{0x602A, 0x7F6C},
	{0x6F12, 0x0100},
	{0x6F12, 0x2F00},
	{0x6F12, 0xFA00},
	{0x6F12, 0x2400},
	{0x6F12, 0xE500},
	{0x602A, 0x0650},
	{0x6F12, 0x0600},
	{0x602A, 0x0654},
	{0x6F12, 0x0000},
	{0x602A, 0x1A46},
	{0x6F12, 0x8500},
	{0x602A, 0x1A52},
	{0x6F12, 0x9800},
	{0x602A, 0x0674},
	{0x6F12, 0x0500},
	{0x6F12, 0x0500},
	{0x6F12, 0x0500},
	{0x6F12, 0x0500},
	{0x602A, 0x0668},
	{0x6F12, 0x0800},
	{0x6F12, 0x0800},
	{0x6F12, 0x0800},
	{0x6F12, 0x0800},
	{0x602A, 0x0684},
	{0x6F12, 0x4001},
	{0x602A, 0x0688},
	{0x6F12, 0x4001},
	{0x602A, 0x147C},
	{0x6F12, 0x0400},
	{0x602A, 0x1480},
	{0x6F12, 0x0400},
	{0x602A, 0x19F6},
	{0x6F12, 0x0404},
	{0x602A, 0x0812},
	{0x6F12, 0x0000},
	{0x602A, 0x1A02},
	{0x6F12, 0x1800},
	{0x602A, 0x2148},
	{0x6F12, 0x0100},
	{0x602A, 0x2042},
	{0x6F12, 0x1A00},
	{0x602A, 0x0874},
	{0x6F12, 0x0106},
	{0x602A, 0x09C0},
	{0x6F12, 0x4000},
	{0x602A, 0x09C4},
	{0x6F12, 0x4000},
	{0x602A, 0x19FE},
	{0x6F12, 0x0C1C},
	{0x602A, 0x4D92},
	{0x6F12, 0x0000},
	{0x602A, 0x84C8},
	{0x6F12, 0x0000},
	{0x602A, 0x4D94},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x602A, 0x3570},
	{0x6F12, 0x0000},
	{0x602A, 0x3574},
	{0x6F12, 0x7306},
	{0x602A, 0x21E4},
	{0x6F12, 0x0400},
	{0x602A, 0x21EC},
	{0x6F12, 0x6902},
	{0x602A, 0x2080},
	{0x6F12, 0x0100},
	{0x6F12, 0xFF00},
	{0x6F12, 0x0002},
	{0x6F12, 0x0001},
	{0x6F12, 0x0002},
	{0x6F12, 0xD244},
	{0x6F12, 0xD244},
	{0x6F12, 0x14F4},
	{0x6F12, 0x101C},
	{0x6F12, 0x0D1C},
	{0x6F12, 0x54F4},
	{0x602A, 0x20BA},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x602A, 0x120E},
	{0x6F12, 0x1000},
	{0x602A, 0x212E},
	{0x6F12, 0x0200},
	{0x602A, 0x13AE},
	{0x6F12, 0x0100},
	{0x602A, 0x0718},
	{0x6F12, 0x0000},
	{0x602A, 0x0710},
	{0x6F12, 0x0010},
	{0x6F12, 0x0201},
	{0x6F12, 0x0800},
	{0x602A, 0x1B5C},
	{0x6F12, 0x0000},
	{0x602A, 0x0786},
	{0x6F12, 0x1401},
	{0x602A, 0x2022},
	{0x6F12, 0x0500},
	{0x6F12, 0x0500},
	{0x602A, 0x1360},
	{0x6F12, 0x0000},
	{0x602A, 0x1376},
	{0x6F12, 0x0000},
	{0x6F12, 0x6038},
	{0x6F12, 0x7038},
	{0x6F12, 0x8038},
	{0x602A, 0x1386},
	{0x6F12, 0x0B00},
	{0x602A, 0x06FA},
	{0x6F12, 0x1000},
	{0x602A, 0x4A94},
	{0x6F12, 0x0400},
	{0x6F12, 0x0400},
	{0x6F12, 0x0400},
	{0x6F12, 0x0400},
	{0x6F12, 0x0800},
	{0x6F12, 0x0800},
	{0x6F12, 0x0800},
	{0x6F12, 0x0800},
	{0x6F12, 0x0400},
	{0x6F12, 0x0400},
	{0x6F12, 0x0400},
	{0x6F12, 0x0400},
	{0x6F12, 0x0800},
	{0x6F12, 0x0800},
	{0x6F12, 0x0800},
	{0x6F12, 0x0800},
	{0x602A, 0x0A76},
	{0x6F12, 0x1000},
	{0x602A, 0x0AEE},
	{0x6F12, 0x1000},
	{0x602A, 0x0B66},
	{0x6F12, 0x1000},
	{0x602A, 0x0BDE},
	{0x6F12, 0x1000},
	{0x602A, 0x0BE8},
	{0x6F12, 0x5000},
	{0x6F12, 0x5000},
	{0x602A, 0x0C56},
	{0x6F12, 0x1000},
	{0x602A, 0x0C60},
	{0x6F12, 0x5000},
	{0x6F12, 0x5000},
	{0x602A, 0x0CB6},
	{0x6F12, 0x0000},
	{0x602A, 0x0CF2},
	{0x6F12, 0x0001},
	{0x602A, 0x0CF0},
	{0x6F12, 0x0101},
	{0x602A, 0x11B8},
	{0x6F12, 0x0000},
	{0x602A, 0x11F6},
	{0x6F12, 0x0010},
	{0x602A, 0x4A74},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x602A, 0x218E},
	{0x6F12, 0x0000},
	{0x602A, 0x2268},
	{0x6F12, 0xF279},
	{0x602A, 0x5006},
	{0x6F12, 0x0000},
	{0x602A, 0x500E},
	{0x6F12, 0x0100},
	{0x602A, 0x4E70},
	{0x6F12, 0x2062},
	{0x6F12, 0x5501},
	{0x602A, 0x06DC},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6F12, 0x0000},
	{0x6028, 0x4000},
	{0xF46A, 0xAE80},
	{0x0344, 0x0000}, //x_addr_start
	{0x0346, 0x0000}, //y_addr_start
	{0x0348, 0x1FFF}, //x_addr_end
	{0x034A, 0x181F}, //y_addr_end
	{0x034C, 0x1FC0}, //output width
	{0x034E, 0x1800}, //output height
	{0x0350, 0x0010},
	{0x0352, 0x0010},
	{0x0900, 0x0111},
	{0x0380, 0x0001},
	{0x0382, 0x0001},
	{0x0384, 0x0001},
	{0x0386, 0x0001},
	{0x0110, 0x1002},
	{0x0114, 0x0300},
	{0x0116, 0x3000},
	{0x0136, 0x1800},
	{0x013E, 0x0000},
	{0x0300, 0x0006},
	{0x0302, 0x0001},
	{0x0304, 0x0004},
	{0x0306, 0x008C},
	{0x0308, 0x0008},
	{0x030A, 0x0001},
	{0x030C, 0x0000},
	{0x030E, 0x0004},
	{0x0310, 0x0074},
	{0x0312, 0x0000},
	{0x080E, 0x0000},
	{0x0340, 0x1900},
	{0x0342, 0x21F0},
	{0x0702, 0x0000},
	{0x0202, 0x1800},
	{0x0200, 0x0100},
	{0x0D00, 0x0100},
	{0x0D02, 0x0001},
	{0x0D04, 0x0002},
	{0x6226, 0x0000},

	{REG_NULL, 0x00},
};

static const struct other_data s5kjn1_spd = {
	.width = 508,
	.height = 3056,
	.bus_fmt = MEDIA_BUS_FMT_SPD_2X8,
	.data_type = 0x30,
	.data_bit = 10,
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
static const struct s5kjn1_mode supported_modes_dphy[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SGRBG10_1X10,
		.width = 4080,
		.height = 3072,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0f00,
		.hts_def = 0x11e8,
		.vts_def = 0x0fd6,
		.mipi_freq_idx = 1,
		.bpp = 10,
		.reg_list = s5kjn1_10bit_4080x3072_dphy_30fps_regs,
		.hdr_mode = NO_HDR,
		.spd = &s5kjn1_spd,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SGRBG10_1X10,
		.width = 8128,
		.height = 6144,
		.max_fps = {
			.numerator = 10000,
			.denominator = 100000,
		},
		.exp_def = 0x1800,
		.hts_def = 0x21f0,
		.vts_def = 0x1900,
		.mipi_freq_idx = 0,
		.bpp = 10,
		.reg_list = s5kjn1_10bit_8128x6144_dphy_10fps_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_items[] = {
	MIPI_FREQ_696M,
	MIPI_FREQ_828M
};

static const char * const s5kjn1_test_pattern_menu[] = {
	"Disabled",
	"solid colour",
	"100% colour bars",
	"fade to grey colour bars",
	"PN9"
};

static int __s5kjn1_power_on(struct s5kjn1 *s5kjn1);

/* Write registers up to 4 at a time */
static int s5kjn1_write_reg(struct i2c_client *client, u16 reg,
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

	if (i2c_master_send(client, buf, len + 2) != len + 2) {
		dev_err(&client->dev, "Failed to write 0x%04x,0x%x\n", reg, val);
		return -EIO;
	}
	return 0;
}

static int s5kjn1_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	int i, delay_ms, ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		if (regs[i].addr == DELAY_MS) {
			delay_ms = regs[i].val;
			dev_info(&client->dev, "delay(%d) ms !\n", delay_ms);
			usleep_range(1000 * delay_ms, 1000 * delay_ms + 100);
			continue;
		}
		ret = s5kjn1_write_reg(client, regs[i].addr,
				       S5KJN1_REG_VALUE_16BIT, regs[i].val);
		if (ret)
			dev_err(&client->dev, "%s failed !\n", __func__);
	}
	return ret;
}

/* Read registers up to 4 at a time */
static int s5kjn1_read_reg(struct i2c_client *client,
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

static int s5kjn1_get_reso_dist(const struct s5kjn1_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct s5kjn1_mode *
s5kjn1_find_best_fit(struct s5kjn1 *s5kjn1, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < s5kjn1->cfg_num; i++) {
		dist = s5kjn1_get_reso_dist(&s5kjn1->support_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist < cur_best_fit_dist) &&
			(s5kjn1->support_modes[i].bus_fmt == framefmt->code)) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}
	dev_info(&s5kjn1->client->dev, "%s: cur_best_fit(%d)",
		 __func__, cur_best_fit);
	return &s5kjn1->support_modes[cur_best_fit];
}

static int s5kjn1_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);
	const struct s5kjn1_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;
	u32 lane_num = s5kjn1->bus_cfg.bus.mipi_csi2.num_data_lanes;

	mutex_lock(&s5kjn1->mutex);

	mode = s5kjn1_find_best_fit(s5kjn1, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&s5kjn1->mutex);
		return -ENOTTY;
#endif
	} else {
		s5kjn1->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(s5kjn1->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(s5kjn1->vblank, vblank_def,
					 S5KJN1_VTS_MAX - mode->height,
					 1, vblank_def);
		__v4l2_ctrl_s_ctrl(s5kjn1->vblank, vblank_def);
		pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] / mode->bpp * 2 * lane_num;
		__v4l2_ctrl_s_ctrl_int64(s5kjn1->pixel_rate,
					 pixel_rate);
		__v4l2_ctrl_s_ctrl(s5kjn1->link_freq,
				   mode->mipi_freq_idx);
	}
	dev_info(&s5kjn1->client->dev, "%s: mode->mipi_freq_idx(%d)",
		 __func__, mode->mipi_freq_idx);

	mutex_unlock(&s5kjn1->mutex);

	return 0;
}

static int s5kjn1_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);
	const struct s5kjn1_mode *mode = s5kjn1->cur_mode;

	mutex_lock(&s5kjn1->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&s5kjn1->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&s5kjn1->mutex);

	return 0;
}

static int s5kjn1_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = s5kjn1->cur_mode->bus_fmt;

	return 0;
}

static int s5kjn1_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);

	if (fse->index >= s5kjn1->cfg_num)
		return -EINVAL;

	if (fse->code != s5kjn1->support_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = s5kjn1->support_modes[fse->index].width;
	fse->max_width  = s5kjn1->support_modes[fse->index].width;
	fse->max_height = s5kjn1->support_modes[fse->index].height;
	fse->min_height = s5kjn1->support_modes[fse->index].height;

	return 0;
}

static int s5kjn1_enable_test_pattern(struct s5kjn1 *s5kjn1, u32 pattern)
{
	u32 val;
	int ret = 0;

	if (pattern)
		val = (pattern - 1) | S5KJN1_TEST_PATTERN_ENABLE;
	else
		val = S5KJN1_TEST_PATTERN_DISABLE;
	ret = s5kjn1_write_reg(s5kjn1->client, S5KJN1_REG_TEST_PATTERN,
				S5KJN1_REG_VALUE_08BIT, val);
	return ret;
}

static int s5kjn1_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);
	const struct s5kjn1_mode *mode = s5kjn1->cur_mode;

	mutex_lock(&s5kjn1->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&s5kjn1->mutex);

	return 0;
}

static int s5kjn1_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);
	u32 lane_num = s5kjn1->bus_cfg.bus.mipi_csi2.num_data_lanes;
	u32 val = 0;

	val = 1 << (lane_num - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	config->type = s5kjn1->bus_cfg.bus_type;
	config->flags = val;

	return 0;
}

static void s5kjn1_get_otp(struct otp_info *otp,
			       struct rkmodule_inf *inf)
{
	u32 i, j;
	u32 w, h;

	/* awb */
	if (otp->awb_data.flag) {
		inf->awb.flag = 1;
		inf->awb.r_value = otp->awb_data.r_ratio;
		inf->awb.b_value = otp->awb_data.b_ratio;
		inf->awb.gr_value = otp->awb_data.g_ratio;
		inf->awb.gb_value = 0x0;

		inf->awb.golden_r_value = otp->awb_data.r_golden;
		inf->awb.golden_b_value = otp->awb_data.b_golden;
		inf->awb.golden_gr_value = otp->awb_data.g_golden;
		inf->awb.golden_gb_value = 0x0;
	}

	/* lsc */
	if (otp->lsc_data.flag) {
		inf->lsc.flag = 1;
		inf->lsc.width = otp->basic_data.size.width;
		inf->lsc.height = otp->basic_data.size.height;
		inf->lsc.table_size = otp->lsc_data.table_size;

		for (i = 0; i < 289; i++) {
			inf->lsc.lsc_r[i] = (otp->lsc_data.data[i * 2] << 8) |
					     otp->lsc_data.data[i * 2 + 1];
			inf->lsc.lsc_gr[i] = (otp->lsc_data.data[i * 2 + 578] << 8) |
					      otp->lsc_data.data[i * 2 + 579];
			inf->lsc.lsc_gb[i] = (otp->lsc_data.data[i * 2 + 1156] << 8) |
					      otp->lsc_data.data[i * 2 + 1157];
			inf->lsc.lsc_b[i] = (otp->lsc_data.data[i * 2 + 1734] << 8) |
					     otp->lsc_data.data[i * 2 + 1735];
		}
	}

	/* pdaf */
	if (otp->pdaf_data.flag) {
		inf->pdaf.flag = 1;
		inf->pdaf.gainmap_width = otp->pdaf_data.gainmap_width;
		inf->pdaf.gainmap_height = otp->pdaf_data.gainmap_height;
		inf->pdaf.dcc_mode = otp->pdaf_data.dcc_mode;
		inf->pdaf.dcc_dir = otp->pdaf_data.dcc_dir;
		inf->pdaf.dccmap_width = otp->pdaf_data.dccmap_width;
		inf->pdaf.dccmap_height = otp->pdaf_data.dccmap_height;
		w = otp->pdaf_data.gainmap_width;
		h = otp->pdaf_data.gainmap_height;
		for (i = 0; i < h; i++) {
			for (j = 0; j < w; j++) {
				inf->pdaf.gainmap[i * w + j] =
					(otp->pdaf_data.gainmap[(i * w + j) * 2] << 8) |
					otp->pdaf_data.gainmap[(i * w + j) * 2 + 1];
			}
		}
		w = otp->pdaf_data.dccmap_width;
		h = otp->pdaf_data.dccmap_height;
		for (i = 0; i < h; i++) {
			for (j = 0; j < w; j++) {
				inf->pdaf.dccmap[i * w + j] =
					(otp->pdaf_data.dccmap[(i * w + j) * 2] << 8) |
					otp->pdaf_data.dccmap[(i * w + j) * 2 + 1];
			}
		}
	}

	/* af */
	if (otp->af_data.flag) {
		inf->af.flag = 1;
		inf->af.dir_cnt = 1;
		inf->af.af_otp[0].vcm_start = otp->af_data.af_inf;
		inf->af.af_otp[0].vcm_end = otp->af_data.af_macro;
		inf->af.af_otp[0].vcm_dir = 0;
	}

}

static void s5kjn1_get_module_inf(struct s5kjn1 *s5kjn1,
				  struct rkmodule_inf *inf)
{
	struct otp_info *otp = s5kjn1->otp;

	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, S5KJN1_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, s5kjn1->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, s5kjn1->len_name, sizeof(inf->base.lens));
	if (otp)
		s5kjn1_get_otp(otp, inf);
}

static int s5kjn1_get_channel_info(struct s5kjn1 *s5kjn1, struct rkmodule_channel_info *ch_info)
{
	const struct s5kjn1_mode *mode = s5kjn1->cur_mode;

	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;

	if (ch_info->index == s5kjn1->spd_id && mode->spd) {
		ch_info->vc = V4L2_MBUS_CSI2_CHANNEL_1;
		ch_info->width = mode->spd->width;
		ch_info->height = mode->spd->height;
		ch_info->bus_fmt = mode->spd->bus_fmt;
		ch_info->data_type = mode->spd->data_type;
		ch_info->data_bit = mode->spd->data_bit;
	} else {
		ch_info->vc = s5kjn1->cur_mode->vc[ch_info->index];
		ch_info->width = s5kjn1->cur_mode->width;
		ch_info->height = s5kjn1->cur_mode->height;
		ch_info->bus_fmt = s5kjn1->cur_mode->bus_fmt;
	}
	return 0;
}

static long s5kjn1_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	struct rkmodule_channel_info *ch_info;
	long ret = 0;
	u32 i, h, w;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_SET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		w = s5kjn1->cur_mode->width;
		h = s5kjn1->cur_mode->height;
		for (i = 0; i < s5kjn1->cfg_num; i++) {
			if (w == s5kjn1->support_modes[i].width &&
			h == s5kjn1->support_modes[i].height &&
			s5kjn1->support_modes[i].hdr_mode == hdr_cfg->hdr_mode) {
				s5kjn1->cur_mode = &s5kjn1->support_modes[i];
				break;
			}
		}
		if (i == s5kjn1->cfg_num) {
			dev_err(&s5kjn1->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr_cfg->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = s5kjn1->cur_mode->hts_def - s5kjn1->cur_mode->width;
			h = s5kjn1->cur_mode->vts_def - s5kjn1->cur_mode->height;
			__v4l2_ctrl_modify_range(s5kjn1->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(s5kjn1->vblank, h,
				S5KJN1_VTS_MAX - s5kjn1->cur_mode->height,
				1, h);
			dev_info(&s5kjn1->client->dev,
				"sensor mode: %d\n",
				s5kjn1->cur_mode->hdr_mode);
		}
		dev_info(&s5kjn1->client->dev, "%s: matched mode index(%d)",
			 __func__, i);
		break;
	case RKMODULE_GET_MODULE_INFO:
		s5kjn1_get_module_inf(s5kjn1, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = s5kjn1->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = s5kjn1_write_reg(s5kjn1->client, S5KJN1_REG_CTRL_MODE,
				S5KJN1_REG_VALUE_08BIT, S5KJN1_MODE_STREAMING);
		else
			ret = s5kjn1_write_reg(s5kjn1->client, S5KJN1_REG_CTRL_MODE,
				S5KJN1_REG_VALUE_08BIT, S5KJN1_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = s5kjn1_get_channel_info(s5kjn1, ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long s5kjn1_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct rkmodule_channel_info *ch_info;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = s5kjn1_ioctl(sd, cmd, inf);
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
			ret = s5kjn1_ioctl(sd, cmd, cfg);
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

		ret = s5kjn1_ioctl(sd, cmd, hdr);
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
			ret = s5kjn1_ioctl(sd, cmd, hdr);
		else
			ret = -EFAULT;
		kfree(hdr);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = s5kjn1_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = s5kjn1_ioctl(sd, cmd, ch_info);
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

static int __s5kjn1_start_stream(struct s5kjn1 *s5kjn1)
{
	int ret;

	if (!s5kjn1->is_thunderboot) {
		ret = s5kjn1_write_array(s5kjn1->client, s5kjn1->cur_mode->reg_list);
		if (ret)
			return ret;
	}

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&s5kjn1->ctrl_handler);
	if (ret)
		return ret;

	return s5kjn1_write_reg(s5kjn1->client, S5KJN1_REG_CTRL_MODE,
		S5KJN1_REG_VALUE_08BIT, S5KJN1_MODE_STREAMING);
}

static int __s5kjn1_stop_stream(struct s5kjn1 *s5kjn1)
{
	if (s5kjn1->is_thunderboot)
		s5kjn1->is_first_streamoff = true;
	return s5kjn1_write_reg(s5kjn1->client, S5KJN1_REG_CTRL_MODE,
		S5KJN1_REG_VALUE_08BIT, S5KJN1_MODE_SW_STANDBY);
}

static int s5kjn1_s_stream(struct v4l2_subdev *sd, int on)
{
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);
	struct i2c_client *client = s5kjn1->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				s5kjn1->cur_mode->width,
				s5kjn1->cur_mode->height,
		DIV_ROUND_CLOSEST(s5kjn1->cur_mode->max_fps.denominator,
				  s5kjn1->cur_mode->max_fps.numerator));

	mutex_lock(&s5kjn1->mutex);
	on = !!on;
	if (on == s5kjn1->streaming)
		goto unlock_and_return;

	if (on) {
		if (s5kjn1->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
			s5kjn1->is_thunderboot = false;
			__s5kjn1_power_on(s5kjn1);
		}
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __s5kjn1_start_stream(s5kjn1);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__s5kjn1_stop_stream(s5kjn1);
		pm_runtime_put(&client->dev);
	}

	s5kjn1->streaming = on;

unlock_and_return:
	mutex_unlock(&s5kjn1->mutex);

	return ret;
}

static int s5kjn1_s_power(struct v4l2_subdev *sd, int on)
{
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);
	struct i2c_client *client = s5kjn1->client;
	int ret = 0;

	mutex_lock(&s5kjn1->mutex);

	/* If the power state is not modified - no work to do. */
	if (s5kjn1->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		if (!s5kjn1->is_thunderboot) {
			ret |= s5kjn1_write_reg(s5kjn1->client,
						 S5KJN1_SOFTWARE_RESET_REG,
						 S5KJN1_REG_VALUE_08BIT,
						 0x01);
			usleep_range(100, 200);
		}

		s5kjn1->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		s5kjn1->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&s5kjn1->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 s5kjn1_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, S5KJN1_XVCLK_FREQ / 1000 / 1000);
}

static int __s5kjn1_power_on(struct s5kjn1 *s5kjn1)
{
	int ret;
	u32 delay_us;
	struct device *dev = &s5kjn1->client->dev;

	if (s5kjn1->is_thunderboot)
		return 0;

	if (!IS_ERR_OR_NULL(s5kjn1->pins_default)) {
		ret = pinctrl_select_state(s5kjn1->pinctrl,
					   s5kjn1->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(s5kjn1->xvclk, S5KJN1_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(s5kjn1->xvclk) != S5KJN1_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(s5kjn1->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(s5kjn1->reset_gpio))
		gpiod_direction_output(s5kjn1->reset_gpio, 0);

	ret = regulator_bulk_enable(S5KJN1_NUM_SUPPLIES, s5kjn1->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(s5kjn1->reset_gpio))
		gpiod_direction_output(s5kjn1->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(s5kjn1->pwdn_gpio))
		gpiod_direction_output(s5kjn1->pwdn_gpio, 1);
	/*
	 * There is no need to wait for the delay of RC circuit
	 * if the reset signal is directly controlled by GPIO.
	 */
	if (!IS_ERR(s5kjn1->reset_gpio))
		usleep_range(8000, 10000);
	else
		usleep_range(12000, 16000);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = s5kjn1_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(s5kjn1->xvclk);

	return ret;
}

static void __s5kjn1_power_off(struct s5kjn1 *s5kjn1)
{
	int ret;
	struct device *dev = &s5kjn1->client->dev;

	if (s5kjn1->is_thunderboot) {
		if (s5kjn1->is_first_streamoff) {
			s5kjn1->is_thunderboot = false;
			s5kjn1->is_first_streamoff = false;
		} else {
			return;
		}
	}

	if (!IS_ERR(s5kjn1->pwdn_gpio))
		gpiod_direction_output(s5kjn1->pwdn_gpio, 0);

	clk_disable_unprepare(s5kjn1->xvclk);

	if (!IS_ERR(s5kjn1->reset_gpio))
		gpiod_direction_output(s5kjn1->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(s5kjn1->pins_sleep)) {
		ret = pinctrl_select_state(s5kjn1->pinctrl,
					   s5kjn1->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}

	if (s5kjn1->is_thunderboot_ng)
		s5kjn1->is_thunderboot_ng = false;

	regulator_bulk_disable(S5KJN1_NUM_SUPPLIES, s5kjn1->supplies);
}

static int s5kjn1_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);

	return __s5kjn1_power_on(s5kjn1);
}

static int s5kjn1_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);

	__s5kjn1_power_off(s5kjn1);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int s5kjn1_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct s5kjn1_mode *def_mode = &s5kjn1->support_modes[0];

	mutex_lock(&s5kjn1->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&s5kjn1->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int s5kjn1_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);

	if (fie->index >= s5kjn1->cfg_num)
		return -EINVAL;

	fie->code = s5kjn1->support_modes[fie->index].bus_fmt;
	fie->width = s5kjn1->support_modes[fie->index].width;
	fie->height = s5kjn1->support_modes[fie->index].height;
	fie->interval = s5kjn1->support_modes[fie->index].max_fps;
	fie->reserved[0] = s5kjn1->support_modes[fie->index].hdr_mode;
	return 0;
}
//#define RK356X_TEST
#ifdef RK356X_TEST
#define CROP_START(SRC, DST) (((SRC) - (DST)) / 2 / 4 * 4)
#define DST_WIDTH 4096
#define DST_HEIGHT 2304
static int s5kjn1_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = CROP_START(s5kjn1->cur_mode->width, DST_WIDTH);
		sel->r.width = DST_WIDTH;
		sel->r.top = CROP_START(s5kjn1->cur_mode->height, DST_HEIGHT);
		sel->r.height = DST_HEIGHT;
		return 0;
	}
	return -EINVAL;
}
#endif

static const struct dev_pm_ops s5kjn1_pm_ops = {
	SET_RUNTIME_PM_OPS(s5kjn1_runtime_suspend,
			   s5kjn1_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops s5kjn1_internal_ops = {
	.open = s5kjn1_open,
};
#endif

static const struct v4l2_subdev_core_ops s5kjn1_core_ops = {
	.s_power = s5kjn1_s_power,
	.ioctl = s5kjn1_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = s5kjn1_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops s5kjn1_video_ops = {
	.s_stream = s5kjn1_s_stream,
	.g_frame_interval = s5kjn1_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops s5kjn1_pad_ops = {
	.enum_mbus_code = s5kjn1_enum_mbus_code,
	.enum_frame_size = s5kjn1_enum_frame_sizes,
	.enum_frame_interval = s5kjn1_enum_frame_interval,
	.get_fmt = s5kjn1_get_fmt,
	.set_fmt = s5kjn1_set_fmt,
#ifdef RK356X_TEST
	.get_selection = s5kjn1_get_selection,
#endif
	.get_mbus_config = s5kjn1_g_mbus_config,
};

static const struct v4l2_subdev_ops s5kjn1_subdev_ops = {
	.core	= &s5kjn1_core_ops,
	.video	= &s5kjn1_video_ops,
	.pad	= &s5kjn1_pad_ops,
};

static int s5kjn1_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5kjn1 *s5kjn1 = container_of(ctrl->handler,
					     struct s5kjn1, ctrl_handler);
	struct i2c_client *client = s5kjn1->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = s5kjn1->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(s5kjn1->exposure,
					 s5kjn1->exposure->minimum, max,
					 s5kjn1->exposure->step,
					 s5kjn1->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = s5kjn1_write_reg(s5kjn1->client,
					S5KJN1_REG_EXP_LONG_H,
					S5KJN1_REG_VALUE_16BIT,
					ctrl->val);
		dev_dbg(&client->dev, "set exposure 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = s5kjn1_write_reg(s5kjn1->client,
					S5KJN1_REG_AGAIN_LONG_H,
					S5KJN1_REG_VALUE_16BIT,
					ctrl->val);
		dev_dbg(&client->dev, "set analog gain 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = s5kjn1_write_reg(s5kjn1->client, S5KJN1_REG_VTS,
					S5KJN1_REG_VALUE_16BIT,
					ctrl->val + s5kjn1->cur_mode->height);
		dev_dbg(&client->dev, "set vblank 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = s5kjn1_enable_test_pattern(s5kjn1, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops s5kjn1_ctrl_ops = {
	.s_ctrl = s5kjn1_set_ctrl,
};

static int s5kjn1_initialize_controls(struct s5kjn1 *s5kjn1)
{
	const struct s5kjn1_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	u64 dst_pixel_rate = 0;
	u32 lane_num = s5kjn1->bus_cfg.bus.mipi_csi2.num_data_lanes;

	handler = &s5kjn1->ctrl_handler;
	mode = s5kjn1->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &s5kjn1->mutex;

	s5kjn1->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
			V4L2_CID_LINK_FREQ,
			1, 0, link_freq_items);

	dst_pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] / mode->bpp * 2 * lane_num;
	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	s5kjn1->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
			V4L2_CID_PIXEL_RATE,
			0, PIXEL_RATE_WITH_828M,
			1, dst_pixel_rate);

	__v4l2_ctrl_s_ctrl(s5kjn1->link_freq,
			   mode->mipi_freq_idx);

	h_blank = mode->hts_def - mode->width;
	s5kjn1->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (s5kjn1->hblank)
		s5kjn1->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	s5kjn1->vblank = v4l2_ctrl_new_std(handler, &s5kjn1_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				S5KJN1_VTS_MAX - mode->height,
				1, vblank_def);
	if (mode->height == 6144)
		exposure_max = mode->vts_def - 44;
	else
		exposure_max = mode->vts_def - 22;

	s5kjn1->exposure = v4l2_ctrl_new_std(handler, &s5kjn1_ctrl_ops,
				V4L2_CID_EXPOSURE, S5KJN1_EXPOSURE_MIN,
				exposure_max, S5KJN1_EXPOSURE_STEP,
				mode->exp_def);

	s5kjn1->anal_gain = v4l2_ctrl_new_std(handler, &s5kjn1_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, S5KJN1_GAIN_MIN,
				S5KJN1_GAIN_MAX, S5KJN1_GAIN_STEP,
				S5KJN1_GAIN_DEFAULT);

	s5kjn1->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&s5kjn1_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(s5kjn1_test_pattern_menu) - 1,
				0, 0, s5kjn1_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&s5kjn1->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	s5kjn1->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int s5kjn1_check_sensor_id(struct s5kjn1 *s5kjn1,
				  struct i2c_client *client)
{
	struct device *dev = &s5kjn1->client->dev;
	u32 id = 0;
	int ret;

	if (s5kjn1->is_thunderboot) {
		dev_info(dev, "Enable thunderboot mode, skip sensor id check\n");
		return 0;
	}

	ret = s5kjn1_read_reg(client, S5KJN1_REG_CHIP_ID,
			       S5KJN1_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int s5kjn1_configure_regulators(struct s5kjn1 *s5kjn1)
{
	unsigned int i;

	for (i = 0; i < S5KJN1_NUM_SUPPLIES; i++)
		s5kjn1->supplies[i].supply = s5kjn1_supply_names[i];

	return devm_regulator_bulk_get(&s5kjn1->client->dev,
				       S5KJN1_NUM_SUPPLIES,
				       s5kjn1->supplies);
}

static int s5kjn1_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct s5kjn1 *s5kjn1;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	struct device_node *endpoint;
	struct device_node *eeprom_ctrl_node;
	struct i2c_client *eeprom_ctrl_client;
	struct v4l2_subdev *eeprom_ctrl;
	struct otp_info *otp_ptr;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	s5kjn1 = devm_kzalloc(dev, sizeof(*s5kjn1), GFP_KERNEL);
	if (!s5kjn1)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &s5kjn1->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &s5kjn1->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &s5kjn1->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &s5kjn1->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	s5kjn1->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}
	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(endpoint),
					 &s5kjn1->bus_cfg);
	if (ret) {
		dev_err(dev, "Failed to parse endpoint!\n");
		return ret;
	}
	if (s5kjn1->bus_cfg.bus_type == V4L2_MBUS_CSI2_DPHY) {
		s5kjn1->support_modes = supported_modes_dphy;
		s5kjn1->cfg_num = ARRAY_SIZE(supported_modes_dphy);
	} else {
		dev_err(dev, "not support!\n");
	}

	s5kjn1->client = client;
	s5kjn1->cur_mode = &s5kjn1->support_modes[0];

	s5kjn1->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(s5kjn1->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	s5kjn1->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(s5kjn1->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	s5kjn1->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_ASIS);
	if (IS_ERR(s5kjn1->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = of_property_read_u32(node,
				   "rockchip,spd-id",
				   &s5kjn1->spd_id);
	if (ret != 0) {
		s5kjn1->spd_id = PAD_MAX;
		dev_err(dev,
			"failed get spd_id, will not to use spd\n");
	}

	s5kjn1->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(s5kjn1->pinctrl)) {
		s5kjn1->pins_default =
			pinctrl_lookup_state(s5kjn1->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(s5kjn1->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		s5kjn1->pins_sleep =
			pinctrl_lookup_state(s5kjn1->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(s5kjn1->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = s5kjn1_configure_regulators(s5kjn1);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&s5kjn1->mutex);

	sd = &s5kjn1->subdev;
	v4l2_i2c_subdev_init(sd, client, &s5kjn1_subdev_ops);
	ret = s5kjn1_initialize_controls(s5kjn1);
	if (ret)
		goto err_destroy_mutex;
	ret = __s5kjn1_power_on(s5kjn1);
	if (ret)
		goto err_free_handler;

	ret = s5kjn1_check_sensor_id(s5kjn1, client);
	if (ret)
		goto err_power_off;
	eeprom_ctrl_node = of_parse_phandle(node, "eeprom-ctrl", 0);
	if (eeprom_ctrl_node) {
		eeprom_ctrl_client =
			of_find_i2c_device_by_node(eeprom_ctrl_node);
		of_node_put(eeprom_ctrl_node);
		if (IS_ERR_OR_NULL(eeprom_ctrl_client)) {
			dev_err(dev, "can not get node\n");
			goto continue_probe;
		}
		eeprom_ctrl = i2c_get_clientdata(eeprom_ctrl_client);
		if (IS_ERR_OR_NULL(eeprom_ctrl)) {
			dev_err(dev, "can not get eeprom i2c client\n");
		} else {
			otp_ptr = devm_kzalloc(dev, sizeof(*otp_ptr), GFP_KERNEL);
			if (!otp_ptr)
				return -ENOMEM;
			ret = v4l2_subdev_call(eeprom_ctrl,
				core, ioctl, 0, otp_ptr);
			if (!ret) {
				s5kjn1->otp = otp_ptr;
			} else {
				s5kjn1->otp = NULL;
				devm_kfree(dev, otp_ptr);
			}
		}
	}
continue_probe:

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &s5kjn1_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	s5kjn1->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &s5kjn1->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(s5kjn1->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 s5kjn1->module_index, facing,
		 S5KJN1_NAME, dev_name(sd->dev));
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
	__s5kjn1_power_off(s5kjn1);
err_free_handler:
	v4l2_ctrl_handler_free(&s5kjn1->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&s5kjn1->mutex);

	return ret;
}

static int s5kjn1_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5kjn1 *s5kjn1 = to_s5kjn1(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&s5kjn1->ctrl_handler);
	mutex_destroy(&s5kjn1->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__s5kjn1_power_off(s5kjn1);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id s5kjn1_of_match[] = {
	{ .compatible = "samsung,s5kjn1" },
	{},
};
MODULE_DEVICE_TABLE(of, s5kjn1_of_match);
#endif

static const struct i2c_device_id s5kjn1_match_id[] = {
	{ "samsung,s5kjn1", 0 },
	{ },
};

static struct i2c_driver s5kjn1_i2c_driver = {
	.driver = {
		.name = S5KJN1_NAME,
		.pm = &s5kjn1_pm_ops,
		.of_match_table = of_match_ptr(s5kjn1_of_match),
	},
	.probe		= &s5kjn1_probe,
	.remove		= &s5kjn1_remove,
	.id_table	= s5kjn1_match_id,
};

#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT
module_i2c_driver(s5kjn1_i2c_driver);
#else
static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&s5kjn1_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&s5kjn1_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);
#endif

MODULE_DESCRIPTION("Samsung s5kjn1 sensor driver");
MODULE_LICENSE("GPL");
