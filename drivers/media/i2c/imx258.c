// SPDX-License-Identifier: GPL-2.0
/*
 * imx258 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 add enum_frame_interval function.
 * V0.0X01.0X04 add quick stream on/off
 * V0.0X01.0X05 add function g_mbus_config
 * V0.0X01.0X06 support capture spd data and embedded data
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
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include "imx258_eeprom_head.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x06)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define IMX258_LANES			4
#define IMX258_BITS_PER_SAMPLE		10
#define IMX258_LINK_FREQ_498MHZ		498000000
#define IMX258_LINK_FREQ_399MHZ		399000000
/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define IMX258_PIXEL_RATE_FULL_SIZE	398400000
#define IMX258_PIXEL_RATE_BINNING	319200000
#define IMX258_XVCLK_FREQ		24000000

#define CHIP_ID				0x0258
#define IMX258_REG_CHIP_ID		0x0016

#define IMX258_REG_CTRL_MODE		0x0100
#define IMX258_MODE_SW_STANDBY		0x0
#define IMX258_MODE_STREAMING		BIT(0)

#define IMX258_REG_EXPOSURE		0x0202
#define	IMX258_EXPOSURE_MIN		4
#define	IMX258_EXPOSURE_STEP		1
#define IMX258_VTS_MAX			0xffff

#define IMX258_REG_GAIN_H		0x0204
#define IMX258_REG_GAIN_L		0x0205
#define IMX258_GAIN_MIN			0
#define IMX258_GAIN_MAX			0x1fff
#define IMX258_GAIN_STEP		1
#define IMX258_GAIN_DEFAULT		0x0

#define IMX258_REG_TEST_PATTERN		0x0600
#define	IMX258_TEST_PATTERN_ENABLE	0x80
#define	IMX258_TEST_PATTERN_DISABLE	0x0

#define IMX258_REG_VTS			0x0340

#define REG_NULL			0xFFFF

#define IMX258_REG_VALUE_08BIT		1
#define IMX258_REG_VALUE_16BIT		2
#define IMX258_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define IMX258_NAME			"imx258"

static const char * const imx258_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define IMX258_NUM_SUPPLIES ARRAY_SIZE(imx258_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct other_data {
	u32 width;
	u32 height;
	u32 bus_fmt;
};

struct imx258_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
	/* Shield Pix Data */
	const struct other_data *spd;
	/* embedded Data */
	const struct other_data *ebd;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct imx258 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[IMX258_NUM_SUPPLIES];

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
	const struct imx258_mode *cur_mode;
	u32			cfg_num;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct imx258_otp_info *otp;
	struct rkmodule_inf	module_inf;
	struct rkmodule_awb_cfg	awb_cfg;
	struct rkmodule_lsc_cfg	lsc_cfg;
	u32 spd_id;
	u32 ebd_id;
};

#define to_imx258(sd) container_of(sd, struct imx258, subdev)

struct imx258_id_name {
	u32 id;
	char name[RKMODULE_NAME_LEN];
};

static const struct imx258_id_name imx258_module_info[] = {
	{0x36, "GuangDongLiteArray"},
	{0x0d, "CameraKing"},
	{0x00, "Unknown"}
};

static const struct imx258_id_name imx258_lens_info[] = {
	{0x47, "Sunny 3923C"},
	{0x07, "Largen 9611A6"},
	{0x00, "Unknown"}
};

/*
 * Xclk 24Mhz
 */
static const struct regval imx258_global_regs[] = {
	{0x0136, 0x18},
	{0x0137, 0x00},
	{0x3051, 0x00},
	{0x6b11, 0xcf},
	{0x7ff0, 0x08},
	{0x7ff1, 0x0f},
	{0x7ff2, 0x08},
	{0x7ff3, 0x1b},
	{0x7ff4, 0x23},
	{0x7ff5, 0x60},
	{0x7ff6, 0x00},
	{0x7ff7, 0x01},
	{0x7ff8, 0x00},
	{0x7ff9, 0x78},
	{0x7ffa, 0x01},
	{0x7ffb, 0x00},
	{0x7ffc, 0x00},
	{0x7ffd, 0x00},
	{0x7ffe, 0x00},
	{0x7fff, 0x03},
	{0x7f76, 0x03},
	{0x7f77, 0xfe},
	{0x7fa8, 0x03},
	{0x7fa9, 0xfe},
	{0x7b24, 0x81},
	{0x7b25, 0x01},
	{0x6564, 0x07},
	{0x6b0d, 0x41},
	{0x653d, 0x04},
	{0x6b05, 0x8c},
	{0x6b06, 0xf9},
	{0x6b08, 0x65},
	{0x6b09, 0xfc},
	{0x6b0a, 0xcf},
	{0x6b0b, 0xd2},
	{0x6700, 0x0e},
	{0x6707, 0x0e},
	{0x5f04, 0x00},
	{0x5f05, 0xed},
	{0x94c7, 0xff},
	{0x94c8, 0xff},
	{0x94c9, 0xff},
	{0x95c7, 0xff},
	{0x95c8, 0xff},
	{0x95c9, 0xff},
	{0x94c4, 0x3f},
	{0x94c5, 0x3f},
	{0x94c6, 0x3f},
	{0x95c4, 0x3f},
	{0x95c5, 0x3f},
	{0x95c6, 0x3f},
	{0x94c1, 0x02},
	{0x94c2, 0x02},
	{0x94c3, 0x02},
	{0x95c1, 0x02},
	{0x95c2, 0x02},
	{0x95c3, 0x02},
	{0x94be, 0x0c},
	{0x94bf, 0x0c},
	{0x94c0, 0x0c},
	{0x95be, 0x0c},
	{0x95bf, 0x0c},
	{0x95c0, 0x0c},
	{0x94d0, 0x74},
	{0x94d1, 0x74},
	{0x94d2, 0x74},
	{0x95d0, 0x74},
	{0x95d1, 0x74},
	{0x95d2, 0x74},
	{0x94cd, 0x2e},
	{0x94ce, 0x2e},
	{0x94cf, 0x2e},
	{0x95cd, 0x2e},
	{0x95ce, 0x2e},
	{0x95cf, 0x2e},
	{0x94ca, 0x4c},
	{0x94cb, 0x4c},
	{0x94cc, 0x4c},
	{0x95ca, 0x4c},
	{0x95cb, 0x4c},
	{0x95cc, 0x4c},
	{0x900e, 0x32},
	{0x94e2, 0xff},
	{0x94e3, 0xff},
	{0x94e4, 0xff},
	{0x95e2, 0xff},
	{0x95e3, 0xff},
	{0x95e4, 0xff},
	{0x94df, 0x6e},
	{0x94e0, 0x6e},
	{0x94e1, 0x6e},
	{0x95df, 0x6e},
	{0x95e0, 0x6e},
	{0x95e1, 0x6e},
	{0x7fcc, 0x01},
	{0x7b78, 0x00},
	{0x9401, 0x35},
	{0x9403, 0x23},
	{0x9405, 0x23},
	{0x9406, 0x00},
	{0x9407, 0x31},
	{0x9408, 0x00},
	{0x9409, 0x1b},
	{0x940a, 0x00},
	{0x940b, 0x15},
	{0x940d, 0x3f},
	{0x940f, 0x3f},
	{0x9411, 0x3f},
	{0x9413, 0x64},
	{0x9415, 0x64},
	{0x9417, 0x64},
	{0x941d, 0x34},
	{0x941f, 0x01},
	{0x9421, 0x01},
	{0x9423, 0x01},
	{0x9425, 0x23},
	{0x9427, 0x23},
	{0x9429, 0x23},
	{0x942b, 0x2f},
	{0x942d, 0x1a},
	{0x942f, 0x14},
	{0x9431, 0x3f},
	{0x9433, 0x3f},
	{0x9435, 0x3f},
	{0x9437, 0x6b},
	{0x9439, 0x7c},
	{0x943b, 0x81},
	{0x9443, 0x0f},
	{0x9445, 0x0f},
	{0x9447, 0x0f},
	{0x9449, 0x0f},
	{0x944b, 0x0f},
	{0x944d, 0x0f},
	{0x944f, 0x1e},
	{0x9451, 0x0f},
	{0x9453, 0x0b},
	{0x9455, 0x28},
	{0x9457, 0x13},
	{0x9459, 0x0c},
	{0x945d, 0x00},
	{0x945e, 0x00},
	{0x945f, 0x00},
	{0x946d, 0x00},
	{0x946f, 0x10},
	{0x9471, 0x10},
	{0x9473, 0x40},
	{0x9475, 0x2e},
	{0x9477, 0x10},
	{0x9478, 0x0a},
	{0x947b, 0xe0},
	{0x947c, 0xe0},
	{0x947d, 0xe0},
	{0x947e, 0xe0},
	{0x947f, 0xe0},
	{0x9480, 0xe0},
	{0x9483, 0x14},
	{0x9485, 0x14},
	{0x9487, 0x14},
	{0x9501, 0x35},
	{0x9503, 0x14},
	{0x9505, 0x14},
	{0x9507, 0x31},
	{0x9509, 0x1b},
	{0x950b, 0x15},
	{0x950d, 0x1e},
	{0x950f, 0x1e},
	{0x9511, 0x1e},
	{0x9513, 0x64},
	{0x9515, 0x64},
	{0x9517, 0x64},
	{0x951d, 0x34},
	{0x951f, 0x01},
	{0x9521, 0x01},
	{0x9523, 0x01},
	{0x9525, 0x14},
	{0x9527, 0x14},
	{0x9529, 0x14},
	{0x952b, 0x2f},
	{0x952d, 0x1a},
	{0x952f, 0x14},
	{0x9531, 0x1e},
	{0x9533, 0x1e},
	{0x9535, 0x1e},
	{0x9537, 0x6b},
	{0x9539, 0x7c},
	{0x953b, 0x81},
	{0x9543, 0x0f},
	{0x9545, 0x0f},
	{0x9547, 0x0f},
	{0x9549, 0x0f},
	{0x954b, 0x0f},
	{0x954d, 0x0f},
	{0x954f, 0x15},
	{0x9551, 0x0b},
	{0x9553, 0x08},
	{0x9555, 0x1c},
	{0x9557, 0x0d},
	{0x9559, 0x08},
	{0x955d, 0x00},
	{0x955e, 0x00},
	{0x955f, 0x00},
	{0x956d, 0x00},
	{0x956f, 0x10},
	{0x9571, 0x10},
	{0x9573, 0x40},
	{0x9575, 0x2e},
	{0x9577, 0x10},
	{0x9578, 0x0a},
	{0x957b, 0xe0},
	{0x957c, 0xe0},
	{0x957d, 0xe0},
	{0x957e, 0xe0},
	{0x957f, 0xe0},
	{0x9580, 0xe0},
	{0x9583, 0x14},
	{0x9585, 0x14},
	{0x9587, 0x14},
	{0x7f78, 0x00},
	{0x7f89, 0x00},
	{0x7f93, 0x00},
	{0x924b, 0x1b},
	{0x924c, 0x0a},
	{0x9304, 0x04},
	{0x9315, 0x04},
	{0x9250, 0x50},
	{0x9251, 0x3c},
	{0x9252, 0x14},
	{0x0112, 0x0a},
	{0x0113, 0x0a},
	{0x0114, 0x03},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x04},
	{0x0306, 0x00},
	{0x0307, 0xa6},
	{0x0309, 0x0a},
	{0x030b, 0x01},
	{0x030d, 0x02},
	{0x030e, 0x00},
	{0x030f, 0xd8},
	{0x0310, 0x00},
	{0x0820, 0x0f},
	{0x0821, 0x90},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x4648, 0x7f},
	{0x7420, 0x00},
	{0x7421, 0x1c},
	{0x7422, 0x00},
	{0x7423, 0xd7},
	{0x9104, 0x00},
	{0x0342, 0x14},
	{0x0343, 0xe8},
	{0x0340, 0x0e},
	{0x0341, 0x88},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x10},
	{0x0349, 0x6f},
	{0x034a, 0x0c},
	{0x034b, 0x2f},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x10},
	{0x040d, 0x70},
	{0x040e, 0x0c},
	{0x040f, 0x30},
	{0x3038, 0x00},
	{0x303a, 0x00},
	{0x303b, 0x10},
	{0x300d, 0x00},
	{0x034c, 0x10},
	{0x034d, 0x70},
	{0x034e, 0x0c},
	{0x034f, 0x30},
	{0x0202, 0x0e},
	{0x0203, 0x7e},
	{0x0204, 0x00},
	{0x0205, 0x00},
	{0x020e, 0x01},
	{0x020f, 0x00},
	{0x0210, 0x01},
	{0x0211, 0x00},
	{0x0212, 0x01},
	{0x0213, 0x00},
	{0x0214, 0x01},
	{0x0215, 0x00},
	{0x7bcd, 0x00},
	{0x94dc, 0x20},
	{0x94dd, 0x20},
	{0x94de, 0x20},
	{0x95dc, 0x20},
	{0x95dd, 0x20},
	{0x95de, 0x20},
	{0x7fb0, 0x00},
	{0x9010, 0x3e},
	{0x9419, 0x50},
	{0x941b, 0x50},
	{0x9519, 0x50},
	{0x951b, 0x50},
	{0x3030, 0x00},
	{0x3032, 0x00},
	{0x0220, 0x00},
	{0x0100, 0x00},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 600Mbps
 */
static const struct regval imx258_2096x1560_regs[] = {
	{0x0112, 0x0a},
	{0x0113, 0x0a},
	{0x0114, 0x03},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x04},
	{0x0306, 0x00},
	{0x0307, 0x85},
	{0x0309, 0x0a},
	{0x030b, 0x01},
	{0x030d, 0x02},
	{0x030e, 0x00},
	{0x030f, 0xd8},
	{0x0310, 0x00},
	{0x0820, 0x0c},
	{0x0821, 0x78},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x4648, 0x7f},
	{0x7420, 0x00},
	{0x7421, 0x1c},
	{0x7422, 0x00},
	{0x7423, 0xd7},
	{0x9104, 0x00},
	{0x0342, 0x14},
	{0x0343, 0xe8},
	{0x0340, 0x07},
	{0x0341, 0xc4},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x10},
	{0x0349, 0x6f},
	{0x034a, 0x0c},
	{0x034b, 0x2f},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x01},
	{0x0901, 0x12},
	{0x0401, 0x01},
	{0x0404, 0x00},
	{0x0405, 0x20},
	{0x0408, 0x00},
	{0x0409, 0x06},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x10},
	{0x040d, 0x62},
	{0x040e, 0x06},
	{0x040f, 0x18},
	{0x3038, 0x00},
	{0x303a, 0x00},
	{0x303b, 0x10},
	{0x300d, 0x00},
	{0x034c, 0x08},
	{0x034d, 0x30},
	{0x034e, 0x06},
	{0x034f, 0x18},
	{0x0100, 0x00},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 7fps
 * mipi_datarate per lane 600Mbps
 */
static const struct regval imx258_4208x3120_regs[] = {
	{0x0112, 0x0a},
	{0x0113, 0x0a},
	{0x0114, 0x03},
	{0x0301, 0x05},
	{0x0303, 0x02},
	{0x0305, 0x04},
	{0x0306, 0x00},
	{0x0307, 0xa6},
	{0x0309, 0x0a},
	{0x030b, 0x01},
	{0x030d, 0x02},
	{0x030e, 0x00},
	{0x030f, 0xd8},
	{0x0310, 0x00},
	{0x0820, 0x0f},
	{0x0821, 0x90},
	{0x0822, 0x00},
	{0x0823, 0x00},
	{0x4648, 0x7f},
	{0x7420, 0x00},
	{0x7421, 0x1c},
	{0x7422, 0x00},
	{0x7423, 0xd7},
	{0x9104, 0x00},
	{0x0342, 0x14},
	{0x0343, 0xe8},
	{0x0340, 0x0e},
	{0x0341, 0x88},
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x10},
	{0x0349, 0x6f},
	{0x034a, 0x0c},
	{0x034b, 0x2f},
	{0x0381, 0x01},
	{0x0383, 0x01},
	{0x0385, 0x01},
	{0x0387, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040a, 0x00},
	{0x040b, 0x00},
	{0x040c, 0x10},
	{0x040d, 0x70},
	{0x040e, 0x0c},
	{0x040f, 0x30},
	{0x3038, 0x00},
	{0x303a, 0x00},
	{0x303b, 0x10},
	{0x300d, 0x00},
	{0x034c, 0x10},
	{0x034d, 0x70},
	{0x034e, 0x0c},
	{0x034f, 0x30},
	{0x0100, 0x00},
	{REG_NULL, 0x00},
};

static const struct regval imx258_4208_3120_spd_reg[] = {
	{0x3030, 0x01},//shield output size:80x1920
	{0x3032, 0x01},//shield BYTE2
#ifdef SPD_DEBUG
	/*DEBUG mode,spd data output with active pixel*/
	{0x7bcd, 0x00},
	{0x0b00, 0x00},
	{0x3051, 0x00},
	{0x3052, 0x00},
	{0x7bca, 0x00},
	{0x7bcb, 0x00},
	{0x7bc8, 0x00},
#endif
};

static const struct other_data imx258_full_spd = {
	.width = 80,
	.height = 1920,
	.bus_fmt = MEDIA_BUS_FMT_SPD_2X8,
};

static const struct other_data imx258_full_ebd = {
	.width = 320,
	.height = 2,
	.bus_fmt = MEDIA_BUS_FMT_EBD_1X8,
};

static const struct imx258_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.width = 4208,
		.height = 3120,
		.max_fps = {
			.numerator = 10000,
			.denominator = 200000,
		},
		.exp_def = 0x0E7E,
		.hts_def = 0x14E8,
		.vts_def = 0x0E88,
		.reg_list = imx258_4208x3120_regs,
		.spd = &imx258_full_spd,
		.ebd = &imx258_full_ebd,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.width = 2096,
		.height = 1560,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x07BA,
		.hts_def = 0x14E8,
		.vts_def = 0x07C4,
		.reg_list = imx258_2096x1560_regs,
		.spd = NULL,
		.ebd = NULL,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	IMX258_LINK_FREQ_498MHZ,
	IMX258_LINK_FREQ_399MHZ
};

static const char * const imx258_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int imx258_write_reg(struct i2c_client *client, u16 reg,
	int len, u32 val)
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

static int imx258_write_array(struct i2c_client *client,
	const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = imx258_write_reg(client, regs[i].addr,
			IMX258_REG_VALUE_08BIT,
			regs[i].val);
	return ret;
}

/* Read registers up to 4 at a time */
static int imx258_read_reg(struct i2c_client *client, u16 reg,
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

static int imx258_get_reso_dist(const struct imx258_mode *mode,
	struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct imx258_mode *
	imx258_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = imx258_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int imx258_set_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *fmt)
{
	struct imx258 *imx258 = to_imx258(sd);
	const struct imx258_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&imx258->mutex);

	mode = imx258_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&imx258->mutex);
		return -ENOTTY;
#endif
	} else {
		imx258->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(imx258->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(imx258->vblank, vblank_def,
					 IMX258_VTS_MAX - mode->height,
					 1, vblank_def);
		if (mode->width == 2096 && mode->height == 1560) {
			__v4l2_ctrl_s_ctrl(imx258->link_freq,
				link_freq_menu_items[1]);
			__v4l2_ctrl_s_ctrl_int64(imx258->pixel_rate,
				IMX258_PIXEL_RATE_BINNING);
		} else {
			__v4l2_ctrl_s_ctrl(imx258->link_freq,
				link_freq_menu_items[0]);
			__v4l2_ctrl_s_ctrl_int64(imx258->pixel_rate,
				IMX258_PIXEL_RATE_FULL_SIZE);
		}
	}
	mutex_unlock(&imx258->mutex);

	return 0;
}

static int imx258_get_fmt(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_format *fmt)
{
	struct imx258 *imx258 = to_imx258(sd);
	const struct imx258_mode *mode = imx258->cur_mode;

	mutex_lock(&imx258->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&imx258->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
		/* to csi rawwr3, other rawwr also can use */
		if (fmt->pad == imx258->spd_id && mode->spd) {
			fmt->format.width = mode->spd->width;
			fmt->format.height = mode->spd->height;
			fmt->format.code = mode->spd->bus_fmt;
			//Set the vc channel to be consistent with the valid data
			fmt->reserved[0] = V4L2_MBUS_CSI2_CHANNEL_0;
		} else if (fmt->pad == imx258->ebd_id && mode->ebd) {
			fmt->format.width = mode->ebd->width;
			fmt->format.height = mode->ebd->height;
			fmt->format.code = mode->ebd->bus_fmt;
			//Set the vc channel to be consistent with the valid data
			fmt->reserved[0] = V4L2_MBUS_CSI2_CHANNEL_0;
		}
	}
	mutex_unlock(&imx258->mutex);

	return 0;
}

static int imx258_enum_mbus_code(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SRGGB10_1X10;

	return 0;
}

static int imx258_enum_frame_sizes(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SRGGB10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int imx258_enable_test_pattern(struct imx258 *imx258, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | IMX258_TEST_PATTERN_ENABLE;
	else
		val = IMX258_TEST_PATTERN_DISABLE;

	return imx258_write_reg(imx258->client,
			IMX258_REG_TEST_PATTERN,
			IMX258_REG_VALUE_08BIT,
			val);
}

static int imx258_g_frame_interval(struct v4l2_subdev *sd,
	struct v4l2_subdev_frame_interval *fi)
{
	struct imx258 *imx258 = to_imx258(sd);
	const struct imx258_mode *mode = imx258->cur_mode;

	mutex_lock(&imx258->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&imx258->mutex);

	return 0;
}

static void imx258_get_otp(struct imx258_otp_info *otp,
			       struct rkmodule_inf *inf)
{
	u32 i;

	/* fac */
	if (otp->flag & 0x80) {
		inf->fac.flag = 1;
		inf->fac.year = otp->year;
		inf->fac.month = otp->month;
		inf->fac.day = otp->day;
		for (i = 0; i < ARRAY_SIZE(imx258_module_info) - 1; i++) {
			if (imx258_module_info[i].id == otp->module_id)
				break;
		}
		strscpy(inf->fac.module, imx258_module_info[i].name,
			sizeof(inf->fac.module));

		for (i = 0; i < ARRAY_SIZE(imx258_lens_info) - 1; i++) {
			if (imx258_lens_info[i].id == otp->lens_id)
				break;
		}
		strscpy(inf->fac.lens, imx258_lens_info[i].name,
			sizeof(inf->fac.lens));
	}
	/* awb */
	if (otp->flag & 0x40) {
		inf->awb.flag = 1;
		inf->awb.r_value = otp->rg_ratio;
		inf->awb.b_value = otp->bg_ratio;
		inf->awb.gr_value = 0x400;
		inf->awb.gb_value = 0x400;

		inf->awb.golden_r_value = 0;
		inf->awb.golden_b_value = 0;
		inf->awb.golden_gr_value = 0;
		inf->awb.golden_gb_value = 0;
	}
	/* af */
	if (otp->flag & 0x20) {
		inf->af.flag = 1;
		inf->af.dir_cnt = 1;
		inf->af.af_otp[0].vcm_start = otp->vcm_start;
		inf->af.af_otp[0].vcm_end = otp->vcm_end;
		inf->af.af_otp[0].vcm_dir = otp->vcm_dir;
	}
	/* lsc */
	if (otp->flag & 0x10) {
		inf->lsc.flag = 1;
		inf->lsc.decimal_bits = 0;
		inf->lsc.lsc_w = 9;
		inf->lsc.lsc_h = 14;

		for (i = 0; i < 126; i++) {
			inf->lsc.lsc_r[i] = otp->lenc[i];
			inf->lsc.lsc_gr[i] = otp->lenc[i + 126];
			inf->lsc.lsc_gb[i] = otp->lenc[i + 252];
			inf->lsc.lsc_b[i] = otp->lenc[i + 378];
		}
	}
}

static void imx258_get_module_inf(struct imx258 *imx258,
	struct rkmodule_inf *inf)
{
	struct imx258_otp_info *otp = imx258->otp;

	strscpy(inf->base.sensor, IMX258_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module,
		imx258->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, imx258->len_name, sizeof(inf->base.lens));
	if (otp)
		imx258_get_otp(otp, inf);
}

static void imx258_set_awb_cfg(struct imx258 *imx258,
			       struct rkmodule_awb_cfg *cfg)
{
	mutex_lock(&imx258->mutex);
	memcpy(&imx258->awb_cfg, cfg, sizeof(*cfg));
	mutex_unlock(&imx258->mutex);
}

static void imx258_set_lsc_cfg(struct imx258 *imx258,
			       struct rkmodule_lsc_cfg *cfg)
{
	mutex_lock(&imx258->mutex);
	memcpy(&imx258->lsc_cfg, cfg, sizeof(*cfg));
	mutex_unlock(&imx258->mutex);
}

static long imx258_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct imx258 *imx258 = to_imx258(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	long ret = 0;
	u32 stream = 0;
	u32 i, h, w;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		imx258_get_module_inf(imx258, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_AWB_CFG:
		imx258_set_awb_cfg(imx258, (struct rkmodule_awb_cfg *)arg);
		break;
	case RKMODULE_LSC_CFG:
		imx258_set_lsc_cfg(imx258, (struct rkmodule_lsc_cfg *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = imx258_write_reg(imx258->client,
					       IMX258_REG_CTRL_MODE,
					       IMX258_REG_VALUE_08BIT,
					       IMX258_MODE_STREAMING);
		else
			ret = imx258_write_reg(imx258->client,
					       IMX258_REG_CTRL_MODE,
					       IMX258_REG_VALUE_08BIT,
					       IMX258_MODE_SW_STANDBY);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		w = imx258->cur_mode->width;
		h = imx258->cur_mode->height;
		for (i = 0; i < imx258->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr_cfg->hdr_mode) {
				imx258->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == imx258->cfg_num) {
			dev_err(&imx258->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr_cfg->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = imx258->cur_mode->hts_def - imx258->cur_mode->width;
			h = imx258->cur_mode->vts_def - imx258->cur_mode->height;
			__v4l2_ctrl_modify_range(imx258->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(imx258->vblank, h,
						 IMX258_VTS_MAX - imx258->cur_mode->height,
						 1, h);
			dev_info(&imx258->client->dev,
				"sensor mode: %d\n",
				imx258->cur_mode->hdr_mode);
		}
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = imx258->cur_mode->hdr_mode;
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long imx258_compat_ioctl32(struct v4l2_subdev *sd,
	unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *awb_cfg;
	struct rkmodule_lsc_cfg *lsc_cfg;
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

		ret = imx258_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		awb_cfg = kzalloc(sizeof(*awb_cfg), GFP_KERNEL);
		if (!awb_cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(awb_cfg, up, sizeof(*awb_cfg));
		if (ret) {
			kfree(awb_cfg);
			return -EFAULT;
		}
		ret = imx258_ioctl(sd, cmd, awb_cfg);
		kfree(awb_cfg);
		break;
	case RKMODULE_LSC_CFG:
		lsc_cfg = kzalloc(sizeof(*lsc_cfg), GFP_KERNEL);
		if (!lsc_cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(lsc_cfg, up, sizeof(*lsc_cfg));
		if (ret) {
			kfree(lsc_cfg);
			return -EFAULT;
		}
		ret = imx258_ioctl(sd, cmd, lsc_cfg);
		kfree(lsc_cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx258_ioctl(sd, cmd, hdr);
		if (!ret) {
			ret = copy_to_user(up, hdr, sizeof(*hdr));
			if (ret) {
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

		ret = copy_from_user(hdr, up, sizeof(*hdr));
		if (ret) {
			kfree(hdr);
			return -EFAULT;
		}
		ret = imx258_ioctl(sd, cmd, hdr);
		kfree(hdr);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (ret)
			return -EFAULT;
		ret = imx258_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOTTY;
		break;
	}
	return ret;
}
#endif

/*--------------------------------------------------------------------------*/
static int imx258_apply_otp(struct imx258 *imx258)
{
	int R_gain, G_gain, B_gain, base_gain;
	struct i2c_client *client = imx258->client;
	struct imx258_otp_info *otp_ptr = imx258->otp;
	struct rkmodule_awb_cfg *awb_cfg = &imx258->awb_cfg;
	struct rkmodule_lsc_cfg *lsc_cfg = &imx258->lsc_cfg;
	u32 golden_bg_ratio = 0;
	u32 golden_rg_ratio = 0;
	u32 golden_g_value = 0;
	u32 bg_ratio;
	u32 rg_ratio;
	//u32 g_value;
	u32 i;

	if (awb_cfg->enable) {
		golden_g_value = (awb_cfg->golden_gb_value +
			awb_cfg->golden_gr_value) / 2;
		golden_bg_ratio = awb_cfg->golden_b_value * 0x400 / golden_g_value;
		golden_rg_ratio = awb_cfg->golden_r_value * 0x400 / golden_g_value;
	}
	/* apply OTP WB Calibration */
	if ((otp_ptr->flag & 0x40) && golden_bg_ratio && golden_rg_ratio) {
		rg_ratio = otp_ptr->rg_ratio;
		bg_ratio = otp_ptr->bg_ratio;
		dev_dbg(&client->dev, "rg:0x%x,bg:0x%x,gol rg:0x%x,bg:0x%x\n",
			rg_ratio, bg_ratio, golden_rg_ratio, golden_bg_ratio);
		/* calculate G gain */
		R_gain = golden_rg_ratio * 1000 / rg_ratio;
		B_gain = golden_bg_ratio * 1000 / bg_ratio;
		G_gain = 1000;
		if (R_gain < 1000 || B_gain < 1000) {
			if (R_gain < B_gain)
				base_gain = R_gain;
			else
				base_gain = B_gain;
		} else {
			base_gain = G_gain;
		}
		R_gain = 0x100 * R_gain / (base_gain);
		B_gain = 0x100 * B_gain / (base_gain);
		G_gain = 0x100 * G_gain / (base_gain);
		/* update sensor WB gain */
		if (R_gain > 0x100) {
			imx258_write_reg(client, 0x0210,
				IMX258_REG_VALUE_08BIT, R_gain >> 8);
			imx258_write_reg(client, 0x0211,
				IMX258_REG_VALUE_08BIT, R_gain & 0x00ff);
		}
		if (G_gain > 0x100) {
			imx258_write_reg(client, 0x020e,
				IMX258_REG_VALUE_08BIT, G_gain >> 8);
			imx258_write_reg(client, 0x020f,
				IMX258_REG_VALUE_08BIT, G_gain & 0x00ff);
			imx258_write_reg(client, 0x0214,
				IMX258_REG_VALUE_08BIT, G_gain >> 8);
			imx258_write_reg(client, 0x0215,
				IMX258_REG_VALUE_08BIT, G_gain & 0x00ff);
		}
		if (B_gain > 0x100) {
			imx258_write_reg(client, 0x0212,
				IMX258_REG_VALUE_08BIT, B_gain >> 8);
			imx258_write_reg(client, 0x0213,
				IMX258_REG_VALUE_08BIT, B_gain & 0x00ff);
		}
		dev_dbg(&client->dev, "apply awb gain: 0x%x, 0x%x, 0x%x\n",
			R_gain, G_gain, B_gain);
	}

	/* apply OTP Lenc Calibration */
	if ((otp_ptr->flag & 0x10) && lsc_cfg->enable) {
		for (i = 0; i < 504; i++) {
			imx258_write_reg(client, 0xA300 + i,
				IMX258_REG_VALUE_08BIT, otp_ptr->lenc[i]);
			dev_dbg(&client->dev, "apply lenc[%d]: 0x%x\n",
				i, otp_ptr->lenc[i]);
		}
		usleep_range(1000, 2000);
		//choose lsc table 1
		imx258_write_reg(client, 0x3021,
			IMX258_REG_VALUE_08BIT, 0x01);
		//enable lsc
		imx258_write_reg(client, 0x0B00,
			IMX258_REG_VALUE_08BIT, 0x01);
	}

	/* apply OTP SPC Calibration */
	if (otp_ptr->flag & 0x08) {
		for (i = 0; i < 63; i++) {
			imx258_write_reg(client, 0xD04C + i,
				IMX258_REG_VALUE_08BIT, otp_ptr->spc[i]);
			dev_dbg(&client->dev, "apply spc[%d]: 0x%x\n",
				i, otp_ptr->spc[i]);
			imx258_write_reg(client, 0xD08C + i,
				IMX258_REG_VALUE_08BIT, otp_ptr->spc[i + 63]);
			dev_dbg(&client->dev, "apply spc[%d]: 0x%x\n",
				i + 63, otp_ptr->spc[i + 63]);
		}
		//enable spc
		imx258_write_reg(client, 0x7BC8,
			IMX258_REG_VALUE_08BIT, 0x01);
	}
	return 0;
}

static int __imx258_start_stream(struct imx258 *imx258)
{
	int ret;

	ret = imx258_write_array(imx258->client, imx258->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&imx258->mutex);
	ret = v4l2_ctrl_handler_setup(&imx258->ctrl_handler);
	mutex_lock(&imx258->mutex);
	if (ret)
		return ret;
	if (imx258->otp) {
		ret = imx258_apply_otp(imx258);
		if (ret)
			return ret;
	}
	if (imx258->cur_mode->width == 4208 &&
	    imx258->cur_mode->height == 3120 &&
	    imx258->cur_mode->spd != NULL &&
	    imx258->spd_id < PAD_MAX) {
		ret = imx258_write_array(imx258->client, imx258_4208_3120_spd_reg);
		if (ret)
			return ret;
	}
	return imx258_write_reg(imx258->client,
		IMX258_REG_CTRL_MODE,
		IMX258_REG_VALUE_08BIT,
		IMX258_MODE_STREAMING);
}

static int __imx258_stop_stream(struct imx258 *imx258)
{
	return imx258_write_reg(imx258->client,
		IMX258_REG_CTRL_MODE,
		IMX258_REG_VALUE_08BIT,
		IMX258_MODE_SW_STANDBY);
}

static int imx258_s_stream(struct v4l2_subdev *sd, int on)
{
	struct imx258 *imx258 = to_imx258(sd);
	struct i2c_client *client = imx258->client;
	int ret = 0;

	mutex_lock(&imx258->mutex);
	on = !!on;
	if (on == imx258->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __imx258_start_stream(imx258);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__imx258_stop_stream(imx258);
		pm_runtime_put(&client->dev);
	}

	imx258->streaming = on;

unlock_and_return:
	mutex_unlock(&imx258->mutex);

	return ret;
}

static int imx258_s_power(struct v4l2_subdev *sd, int on)
{
	struct imx258 *imx258 = to_imx258(sd);
	struct i2c_client *client = imx258->client;
	int ret = 0;

	mutex_lock(&imx258->mutex);

	/* If the power state is not modified - no work to do. */
	if (imx258->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = imx258_write_array(imx258->client, imx258_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		imx258->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		imx258->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&imx258->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 imx258_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, IMX258_XVCLK_FREQ / 1000 / 1000);
}

static int __imx258_power_on(struct imx258 *imx258)
{
	int ret;
	u32 delay_us;
	struct device *dev = &imx258->client->dev;

	if (!IS_ERR_OR_NULL(imx258->pins_default)) {
		ret = pinctrl_select_state(imx258->pinctrl,
			imx258->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(imx258->xvclk, IMX258_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(imx258->xvclk) != IMX258_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(imx258->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(imx258->reset_gpio))
		gpiod_set_value_cansleep(imx258->reset_gpio, 1);

	ret = regulator_bulk_enable(IMX258_NUM_SUPPLIES, imx258->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(imx258->reset_gpio))
		gpiod_set_value_cansleep(imx258->reset_gpio, 0);

	usleep_range(500, 1000);
	if (!IS_ERR(imx258->pwdn_gpio))
		gpiod_set_value_cansleep(imx258->pwdn_gpio, 0);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = imx258_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(imx258->xvclk);

	return ret;
}

static void __imx258_power_off(struct imx258 *imx258)
{
	int ret;

	if (!IS_ERR(imx258->pwdn_gpio))
		gpiod_set_value_cansleep(imx258->pwdn_gpio, 1);
	clk_disable_unprepare(imx258->xvclk);
	if (!IS_ERR(imx258->reset_gpio))
		gpiod_set_value_cansleep(imx258->reset_gpio, 1);
	if (!IS_ERR_OR_NULL(imx258->pins_sleep)) {
		ret = pinctrl_select_state(imx258->pinctrl,
			imx258->pins_sleep);
		if (ret < 0)
			dev_dbg(&imx258->client->dev, "could not set pins\n");
	}
	regulator_bulk_disable(IMX258_NUM_SUPPLIES, imx258->supplies);
}

static int imx258_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx258 *imx258 = to_imx258(sd);

	return __imx258_power_on(imx258);
}

static int imx258_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx258 *imx258 = to_imx258(sd);

	__imx258_power_off(imx258);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int imx258_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx258 *imx258 = to_imx258(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct imx258_mode *def_mode = &supported_modes[0];

	mutex_lock(&imx258->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&imx258->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int imx258_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static int imx258_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	u32 val = 0;

	val = 1 << (IMX258_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops imx258_pm_ops = {
	SET_RUNTIME_PM_OPS(imx258_runtime_suspend,
		imx258_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops imx258_internal_ops = {
	.open = imx258_open,
};
#endif

static const struct v4l2_subdev_core_ops imx258_core_ops = {
	.s_power = imx258_s_power,
	.ioctl = imx258_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = imx258_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops imx258_video_ops = {
	.s_stream = imx258_s_stream,
	.g_frame_interval = imx258_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops imx258_pad_ops = {
	.enum_mbus_code = imx258_enum_mbus_code,
	.enum_frame_size = imx258_enum_frame_sizes,
	.enum_frame_interval = imx258_enum_frame_interval,
	.get_fmt = imx258_get_fmt,
	.set_fmt = imx258_set_fmt,
	.get_mbus_config = imx258_g_mbus_config,
};

static const struct v4l2_subdev_ops imx258_subdev_ops = {
	.core	= &imx258_core_ops,
	.video	= &imx258_video_ops,
	.pad	= &imx258_pad_ops,
};

static int imx258_set_gain_reg(struct imx258 *imx258, u32 a_gain)
{
	int ret = 0;
	u32 gain_reg = 0;

	gain_reg = (512 - (512 * 512 / a_gain));
	if (gain_reg > 480)
		gain_reg = 480;

	ret = imx258_write_reg(imx258->client,
		IMX258_REG_GAIN_H,
		IMX258_REG_VALUE_08BIT,
		((gain_reg & 0x100) >> 8));
	ret |= imx258_write_reg(imx258->client,
		IMX258_REG_GAIN_L,
		IMX258_REG_VALUE_08BIT,
		(gain_reg & 0xff));
	return ret;
}

static int imx258_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx258 *imx258 = container_of(ctrl->handler,
					     struct imx258, ctrl_handler);
	struct i2c_client *client = imx258->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = imx258->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(imx258->exposure,
			imx258->exposure->minimum, max,
			imx258->exposure->step,
			imx258->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = imx258_write_reg(imx258->client,
			IMX258_REG_EXPOSURE,
			IMX258_REG_VALUE_16BIT,
			ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = imx258_set_gain_reg(imx258, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = imx258_write_reg(imx258->client,
			IMX258_REG_VTS,
			IMX258_REG_VALUE_16BIT,
			ctrl->val + imx258->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = imx258_enable_test_pattern(imx258, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx258_ctrl_ops = {
	.s_ctrl = imx258_set_ctrl,
};

static int imx258_initialize_controls(struct imx258 *imx258)
{
	const struct imx258_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &imx258->ctrl_handler;
	mode = imx258->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &imx258->mutex;

	imx258->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
		V4L2_CID_LINK_FREQ, 1, 0,
		link_freq_menu_items);

	imx258->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
		V4L2_CID_PIXEL_RATE, 0, IMX258_PIXEL_RATE_FULL_SIZE,
		1, IMX258_PIXEL_RATE_FULL_SIZE);

	h_blank = mode->hts_def - mode->width;
	imx258->hblank = v4l2_ctrl_new_std(handler, NULL,
		V4L2_CID_HBLANK, h_blank, h_blank, 1, h_blank);
	if (imx258->hblank)
		imx258->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	imx258->vblank = v4l2_ctrl_new_std(handler, &imx258_ctrl_ops,
		V4L2_CID_VBLANK, vblank_def,
		IMX258_VTS_MAX - mode->height,
		1, vblank_def);

	exposure_max = mode->vts_def - 4;
	imx258->exposure = v4l2_ctrl_new_std(handler, &imx258_ctrl_ops,
		V4L2_CID_EXPOSURE, IMX258_EXPOSURE_MIN,
		exposure_max, IMX258_EXPOSURE_STEP,
		mode->exp_def);

	imx258->anal_gain = v4l2_ctrl_new_std(handler, &imx258_ctrl_ops,
		V4L2_CID_ANALOGUE_GAIN, IMX258_GAIN_MIN,
		IMX258_GAIN_MAX, IMX258_GAIN_STEP,
		IMX258_GAIN_DEFAULT);

	imx258->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
		&imx258_ctrl_ops, V4L2_CID_TEST_PATTERN,
		ARRAY_SIZE(imx258_test_pattern_menu) - 1,
		0, 0, imx258_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&imx258->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	imx258->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int imx258_check_sensor_id(struct imx258 *imx258,
				   struct i2c_client *client)
{
	struct device *dev = &imx258->client->dev;
	int ret = 0;
	u32 id = 0;

	ret = imx258_read_reg(client, IMX258_REG_CHIP_ID,
			       IMX258_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	return 0;
}

static int imx258_configure_regulators(struct imx258 *imx258)
{
	unsigned int i;

	for (i = 0; i < IMX258_NUM_SUPPLIES; i++)
		imx258->supplies[i].supply = imx258_supply_names[i];

	return devm_regulator_bulk_get(&imx258->client->dev,
		IMX258_NUM_SUPPLIES,
		imx258->supplies);
}

static int imx258_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct imx258 *imx258;
	struct v4l2_subdev *sd;
	char facing[2];
	struct device_node *eeprom_ctrl_node;
	struct i2c_client *eeprom_ctrl_client;
	struct v4l2_subdev *eeprom_ctrl;
	struct imx258_otp_info *otp_ptr;
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	imx258 = devm_kzalloc(dev, sizeof(*imx258), GFP_KERNEL);
	if (!imx258)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
		&imx258->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
		&imx258->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
		&imx258->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
		&imx258->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	imx258->client = client;
	imx258->cfg_num = ARRAY_SIZE(supported_modes);
	imx258->cur_mode = &supported_modes[0];

	imx258->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(imx258->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	imx258->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(imx258->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	imx258->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(imx258->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = of_property_read_u32(node,
				   "rockchip,spd-id",
				   &imx258->spd_id);
	if (ret != 0) {
		imx258->spd_id = PAD_MAX;
		dev_err(dev,
			"failed get spd_id, will not to use spd\n");
	}
	ret = of_property_read_u32(node,
				   "rockchip,ebd-id",
				   &imx258->ebd_id);
	if (ret != 0) {
		imx258->ebd_id = PAD_MAX;
		dev_err(dev,
			"failed get ebd_id, will not to use ebd\n");
	}

	ret = imx258_configure_regulators(imx258);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	imx258->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(imx258->pinctrl)) {
		imx258->pins_default =
			pinctrl_lookup_state(imx258->pinctrl,
				OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(imx258->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		imx258->pins_sleep =
			pinctrl_lookup_state(imx258->pinctrl,
				OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(imx258->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&imx258->mutex);

	sd = &imx258->subdev;
	v4l2_i2c_subdev_init(sd, client, &imx258_subdev_ops);
	ret = imx258_initialize_controls(imx258);
	if (ret)
		goto err_destroy_mutex;

	ret = __imx258_power_on(imx258);
	if (ret)
		goto err_free_handler;

	ret = imx258_check_sensor_id(imx258, client);
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
				imx258->otp = otp_ptr;
			} else {
				imx258->otp = NULL;
				devm_kfree(dev, otp_ptr);
			}
		}
	}

continue_probe:

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &imx258_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	imx258->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &imx258->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(imx258->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 imx258->module_index, facing,
		 IMX258_NAME, dev_name(sd->dev));
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
	__imx258_power_off(imx258);
err_free_handler:
	v4l2_ctrl_handler_free(&imx258->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&imx258->mutex);

	return ret;
}

static int imx258_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx258 *imx258 = to_imx258(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&imx258->ctrl_handler);
	mutex_destroy(&imx258->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__imx258_power_off(imx258);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id imx258_of_match[] = {
	{ .compatible = "sony,imx258" },
	{},
};
MODULE_DEVICE_TABLE(of, imx258_of_match);
#endif

static const struct i2c_device_id imx258_match_id[] = {
	{ "sony,imx258", 0 },
	{ },
};

static struct i2c_driver imx258_i2c_driver = {
	.driver = {
		.name = IMX258_NAME,
		.pm = &imx258_pm_ops,
		.of_match_table = of_match_ptr(imx258_of_match),
	},
	.probe		= &imx258_probe,
	.remove		= &imx258_remove,
	.id_table	= imx258_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&imx258_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&imx258_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Sony imx258 sensor driver");
MODULE_LICENSE("GPL v2");
