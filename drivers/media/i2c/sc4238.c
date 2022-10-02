// SPDX-License-Identifier: GPL-2.0
/*
 * sc4238 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 add quick stream on/off
 * V0.0X01.0X02 support digital gain
 * V0.0X01.0X03 support 2688x1520@30fps 10bit linear mode
 * V0.0X01.0X04 fixed hdr exposure issue
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
#include <linux/pinctrl/consumer.h>
#include <linux/rk-preisp.h>
#include "../platform/rockchip/isp/rkisp_tb_helper.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x04)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIPI_FREQ_360M			360000000
#define MIPI_FREQ_200M			200000000

#define PIXEL_RATE_WITH_360M		(MIPI_FREQ_360M * 2 / 10 * 4)
#define PIXEL_RATE_WITH_200M		(MIPI_FREQ_200M * 2 / 12 * 4)

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define SC4238_XVCLK_FREQ		24000000

#define CHIP_ID				0x4235
#define SC4238_REG_CHIP_ID		0x3107

#define SC4238_REG_CTRL_MODE		0x0100
#define SC4238_MODE_SW_STANDBY		0x0
#define SC4238_MODE_STREAMING		BIT(0)

#define	SC4238_EXPOSURE_MIN		3
#define	SC4238_EXPOSURE_STEP		1
#define SC4238_VTS_MAX			0xffff

#define SC4238_REG_EXP_LONG_H		0x3e00
#define SC4238_REG_EXP_MID_H		0x3e04
#define SC4238_REG_EXP_MAX_MID_H	0x3e23

#define SC4238_REG_COARSE_AGAIN_L	0x3e08
#define SC4238_REG_FINE_AGAIN_L		0x3e09
#define SC4238_REG_COARSE_AGAIN_S	0x3e12
#define SC4238_REG_FINE_AGAIN_S		0x3e13
#define SC4238_REG_COARSE_DGAIN_L	0x3e06
#define SC4238_REG_FINE_DGAIN_L		0x3e07
#define SC4238_REG_COARSE_DGAIN_S	0x3e10
#define SC4238_REG_FINE_DGAIN_S		0x3e11
#define SC4238_GAIN_MIN			0x40
#define SC4238_GAIN_MAX			0x7D04
#define SC4238_GAIN_STEP		1
#define SC4238_GAIN_DEFAULT		0x80

#define SC4238_GROUP_UPDATE_ADDRESS	0x3812
#define SC4238_GROUP_UPDATE_START_DATA	0x00
#define SC4238_GROUP_UPDATE_END_DATA	0x30
#define SC4238_GROUP_UPDATE_DELAY	0x3802

#define SC4238_SOFTWARE_RESET_REG	0x0103

#define SC4238_REG_TEST_PATTERN		0x4501
#define SC4238_TEST_PATTERN_BIT_MASK	BIT(3)

#define SC4238_REG_VTS_H		0x320e
#define SC4238_REG_VTS_L		0x320f

#define REG_NULL			0xFFFF

#define SC4238_REG_VALUE_08BIT		1
#define SC4238_REG_VALUE_16BIT		2
#define SC4238_REG_VALUE_24BIT		3

#define SC4238_LANES			V4L2_MBUS_CSI2_4_LANE

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define SC4238_NAME			"sc4238"

static const char * const sc4238_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define SC4238_NUM_SUPPLIES ARRAY_SIZE(sc4238_supply_names)

#define SC4238_FLIP_REG		0x3221

#define MIRROR_BIT_MASK			(BIT(1) | BIT(2))
#define FLIP_BIT_MASK			(BIT(6) | BIT(5))

struct regval {
	u16 addr;
	u8 val;
};

struct sc4238_mode {
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
	u32 link_freq;
	u32 pixel_rate;
};

struct sc4238 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[SC4238_NUM_SUPPLIES];

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
	struct v4l2_fract	cur_fps;
	bool			streaming;
	bool			power_on;
	const struct sc4238_mode *cur_mode;
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
	u8			flip;
	u32			cur_vts;
};

#define to_sc4238(sd) container_of(sd, struct sc4238, subdev)

static const struct regval sc4238_global_regs[] = {

	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 337.5Mbps
 */
static const struct regval sc4238_linear10bit_2688x1520_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x3018, 0x72},
	{0x301f, 0x9a},
	{0x3031, 0x0a},
	{0x3037, 0x20},
	{0x3038, 0x22},
	{0x3106, 0x81},
	{0x3200, 0x00},
	{0x3201, 0x00},
	{0x3202, 0x00},
	{0x3203, 0x00},
	{0x3204, 0x0a},
	{0x3205, 0x87},
	{0x3206, 0x05},
	{0x3207, 0xf7},
	{0x3208, 0x0a},
	{0x3209, 0x80},
	{0x320a, 0x05},
	{0x320b, 0xf0},
	{0x320c, 0x05},
	{0x320d, 0xa0},
	{0x320e, 0x07},
	{0x320f, 0x52},
	{0x3211, 0x04},
	{0x3213, 0x04},
	{0x3251, 0x88},
	{0x3253, 0x0a},
	{0x325f, 0x0c},
	{0x3273, 0x01},
	{0x3301, 0x30},
	{0x3304, 0x30},
	{0x3306, 0x70},
	{0x3308, 0x10},
	{0x3309, 0x50},
	{0x330b, 0xf0},
	{0x330e, 0x14},
	{0x3314, 0x94},
	{0x331e, 0x29},
	{0x331f, 0x49},
	{0x3320, 0x09},
	{0x334c, 0x10},
	{0x3352, 0x02},
	{0x3356, 0x1f},
	{0x335e, 0x02},
	{0x335f, 0x04},
	{0x3363, 0x00},
	{0x3364, 0x1e},
	{0x3366, 0x92},
	{0x336d, 0x03},
	{0x337a, 0x08},
	{0x337b, 0x10},
	{0x337c, 0x06},
	{0x337d, 0x0a},
	{0x337f, 0x2d},
	{0x3390, 0x08},
	{0x3391, 0x18},
	{0x3392, 0x38},
	{0x3393, 0x30},
	{0x3394, 0x30},
	{0x3395, 0x30},
	{0x3399, 0xff},
	{0x33a2, 0x08},
	{0x33a3, 0x0c},
	{0x33e0, 0xa0},
	{0x33e1, 0x08},
	{0x33e2, 0x00},
	{0x33e3, 0x10},
	{0x33e4, 0x10},
	{0x33e5, 0x00},
	{0x33e6, 0x10},
	{0x33e7, 0x10},
	{0x33e8, 0x00},
	{0x33e9, 0x10},
	{0x33ea, 0x16},
	{0x33eb, 0x00},
	{0x33ec, 0x10},
	{0x33ed, 0x18},
	{0x33ee, 0xa0},
	{0x33ef, 0x08},
	{0x33f4, 0x00},
	{0x33f5, 0x10},
	{0x33f6, 0x10},
	{0x33f7, 0x00},
	{0x33f8, 0x10},
	{0x33f9, 0x10},
	{0x33fa, 0x00},
	{0x33fb, 0x10},
	{0x33fc, 0x16},
	{0x33fd, 0x00},
	{0x33fe, 0x10},
	{0x33ff, 0x18},
	{0x360f, 0x05},
	{0x3622, 0xee},
	{0x3625, 0x0a},
	{0x3630, 0xa8},
	{0x3631, 0x80},
	{0x3633, 0x44},
	{0x3634, 0x34},
	{0x3635, 0x60},
	{0x3636, 0x20},
	{0x3637, 0x11},
	{0x3638, 0x2a},
	{0x363a, 0x1f},
	{0x363b, 0x03},
	{0x366e, 0x04},
	{0x3670, 0x4a},
	{0x3671, 0xee},
	{0x3672, 0x0e},
	{0x3673, 0x0e},
	{0x3674, 0x70},
	{0x3675, 0x40},
	{0x3676, 0x45},
	{0x367a, 0x08},
	{0x367b, 0x38},
	{0x367c, 0x08},
	{0x367d, 0x38},
	{0x3690, 0x43},
	{0x3691, 0x63},
	{0x3692, 0x63},
	{0x3699, 0x80},
	{0x369a, 0x9f},
	{0x369b, 0x9f},
	{0x369c, 0x08},
	{0x369d, 0x38},
	{0x36a2, 0x08},
	{0x36a3, 0x38},
	{0x36ea, 0x31},
	{0x36eb, 0x14},
	{0x36ec, 0x0c},
	{0x36ed, 0x24},
	{0x36fa, 0x31},
	{0x36fb, 0x09},
	{0x36fc, 0x00},
	{0x36fd, 0x24},
	{0x3902, 0xc5},
	{0x3905, 0xd8},
	{0x3908, 0x11},
	{0x391b, 0x80},
	{0x391c, 0x0f},
	{0x391d, 0x24},
	{0x3933, 0x28},
	{0x3934, 0x20},
	{0x3940, 0x6c},
	{0x3942, 0x08},
	{0x3943, 0x28},
	{0x3980, 0x00},
	{0x3981, 0x00},
	{0x3982, 0x00},
	{0x3983, 0x00},
	{0x3984, 0x00},
	{0x3985, 0x00},
	{0x3986, 0x00},
	{0x3987, 0x00},
	{0x3988, 0x00},
	{0x3989, 0x00},
	{0x398a, 0x00},
	{0x398b, 0x04},
	{0x398c, 0x00},
	{0x398d, 0x04},
	{0x398e, 0x00},
	{0x398f, 0x08},
	{0x3990, 0x00},
	{0x3991, 0x10},
	{0x3992, 0x03},
	{0x3993, 0xd8},
	{0x3994, 0x03},
	{0x3995, 0xe0},
	{0x3996, 0x03},
	{0x3997, 0xf0},
	{0x3998, 0x03},
	{0x3999, 0xf8},
	{0x399a, 0x00},
	{0x399b, 0x00},
	{0x399c, 0x00},
	{0x399d, 0x08},
	{0x399e, 0x00},
	{0x399f, 0x10},
	{0x39a0, 0x00},
	{0x39a1, 0x18},
	{0x39a2, 0x00},
	{0x39a3, 0x28},
	{0x39af, 0x58},
	{0x39b5, 0x30},
	{0x39b6, 0x00},
	{0x39b7, 0x34},
	{0x39b8, 0x00},
	{0x39b9, 0x00},
	{0x39ba, 0x34},
	{0x39bb, 0x00},
	{0x39bc, 0x00},
	{0x39bd, 0x00},
	{0x39be, 0x00},
	{0x39bf, 0x00},
	{0x39c0, 0x00},
	{0x39c1, 0x00},
	{0x39c5, 0x21},
	{0x39c8, 0x00},
	{0x39db, 0x20},
	{0x39dc, 0x00},
	{0x39de, 0x20},
	{0x39df, 0x00},
	{0x39e0, 0x00},
	{0x39e1, 0x00},
	{0x39e2, 0x00},
	{0x39e3, 0x00},
	{0x3e00, 0x00},
	{0x3e01, 0xc2},
	{0x3e02, 0xa0},
	{0x3e03, 0x0b},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3e14, 0xb1},
	{0x3e25, 0x03},
	{0x3e26, 0x40},
	{0x4501, 0xb4},
	{0x4509, 0x20},
	{0x4800, 0x64},
	{0x4818, 0x00},
	{0x4819, 0x30},
	{0x481a, 0x00},
	{0x481b, 0x0b},
	{0x481c, 0x00},
	{0x481d, 0xc8},
	{0x4821, 0x02},
	{0x4822, 0x00},
	{0x4823, 0x03},
	{0x4828, 0x00},
	{0x4829, 0x02},
	{0x4837, 0x3b},
	{0x5784, 0x10},
	{0x5785, 0x08},
	{0x5787, 0x06},
	{0x5788, 0x06},
	{0x5789, 0x00},
	{0x578a, 0x06},
	{0x578b, 0x06},
	{0x578c, 0x00},
	{0x5790, 0x10},
	{0x5791, 0x10},
	{0x5792, 0x00},
	{0x5793, 0x10},
	{0x5794, 0x10},
	{0x5795, 0x00},
	{0x57c4, 0x10},
	{0x57c5, 0x08},
	{0x57c7, 0x06},
	{0x57c8, 0x06},
	{0x57c9, 0x00},
	{0x57ca, 0x06},
	{0x57cb, 0x06},
	{0x57cc, 0x00},
	{0x57d0, 0x10},
	{0x57d1, 0x10},
	{0x57d2, 0x00},
	{0x57d3, 0x10},
	{0x57d4, 0x10},
	{0x57d5, 0x00},
	{0x5988, 0x86},
	{0x598e, 0x05},
	{0x598f, 0x6c},
	{0x36e9, 0x51},
	{0x36f9, 0x51},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 720Mbps
 */
static const struct regval sc4238_hdr10bit_2688x1520_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x3018, 0x72},
	{0x301f, 0x93},
	{0x3031, 0x0a},
	{0x3037, 0x20},
	{0x3038, 0x22},
	{0x3106, 0x81},
	{0x3200, 0x00},
	{0x3201, 0x00},
	{0x3202, 0x00},
	{0x3203, 0x00},
	{0x3204, 0x0a},
	{0x3205, 0x87},
	{0x3206, 0x05},
	{0x3207, 0xf7},
	{0x3208, 0x0a},
	{0x3209, 0x80},
	{0x320a, 0x05},
	{0x320b, 0xf0},
	{0x320c, 0x06},
	{0x320d, 0x0e},
	{0x320e, 0x0c},
	{0x320f, 0x18},
	{0x3211, 0x04},
	{0x3213, 0x04},
	{0x3220, 0x53},
	{0x3225, 0x02},
	{0x3235, 0x18},
	{0x3236, 0x2f},
	{0x3237, 0x02},
	{0x3238, 0xc7},
	{0x3250, 0x3f},
	{0x3251, 0x88},
	{0x3253, 0x0a},
	{0x325f, 0x0c},
	{0x3273, 0x01},
	{0x3301, 0x12},
	{0x3304, 0x38},
	{0x3305, 0x00},
	{0x3306, 0x68},
	{0x3307, 0x06},
	{0x3308, 0x10},
	{0x3309, 0x68},
	{0x330a, 0x00},
	{0x330b, 0xe8},
	{0x330d, 0x20},
	{0x330e, 0x1a},
	{0x3314, 0x94},
	{0x3317, 0x04},
	{0x3318, 0x02},
	{0x331e, 0x29},
	{0x331f, 0x59},
	{0x3320, 0x09},
	{0x3332, 0x20},
	{0x334c, 0x10},
	{0x3350, 0x20},
	{0x3352, 0x02},
	{0x3356, 0x1f},
	{0x3358, 0x20},
	{0x335c, 0x20},
	{0x335e, 0x02},
	{0x335f, 0x04},
	{0x3363, 0x00},
	{0x3364, 0x1e},
	{0x3366, 0x92},
	{0x336d, 0x03},
	{0x337a, 0x08},
	{0x337b, 0x10},
	{0x337c, 0x06},
	{0x337d, 0x0a},
	{0x337f, 0x2d},
	{0x3390, 0x04},
	{0x3391, 0x08},
	{0x3392, 0x38},
	{0x3393, 0x20},
	{0x3394, 0x30},
	{0x3395, 0x30},
	{0x3399, 0xff},
	{0x339e, 0x20},
	{0x33a0, 0x20},
	{0x33a2, 0x08},
	{0x33a3, 0x0c},
	{0x33a4, 0x20},
	{0x33a8, 0x20},
	{0x33aa, 0x20},
	{0x33e0, 0xa0},
	{0x33e1, 0x08},
	{0x33e2, 0x00},
	{0x33e3, 0x10},
	{0x33e4, 0x10},
	{0x33e5, 0x00},
	{0x33e6, 0x10},
	{0x33e7, 0x10},
	{0x33e8, 0x00},
	{0x33e9, 0x10},
	{0x33ea, 0x16},
	{0x33eb, 0x00},
	{0x33ec, 0x10},
	{0x33ed, 0x18},
	{0x33ee, 0xa0},
	{0x33ef, 0x08},
	{0x33f4, 0x00},
	{0x33f5, 0x10},
	{0x33f6, 0x10},
	{0x33f7, 0x00},
	{0x33f8, 0x10},
	{0x33f9, 0x10},
	{0x33fa, 0x00},
	{0x33fb, 0x10},
	{0x33fc, 0x16},
	{0x33fd, 0x00},
	{0x33fe, 0x10},
	{0x33ff, 0x18},
	{0x360f, 0x05},
	{0x3622, 0xee},
	{0x3625, 0x0a},
	{0x3630, 0xa8},
	{0x3631, 0x80},
	{0x3633, 0x44},
	{0x3634, 0x54},
	{0x3635, 0x60},
	{0x3636, 0x20},
	{0x3637, 0x22},
	{0x3638, 0x2a},
	{0x363a, 0x1f},
	{0x363b, 0x03},
	{0x3641, 0x00},
	{0x366e, 0x04},
	{0x3670, 0x4a},
	{0x3671, 0xee},
	{0x3672, 0x6e},
	{0x3673, 0x6e},
	{0x3674, 0x70},
	{0x3675, 0x40},
	{0x3676, 0x45},
	{0x367a, 0x08},
	{0x367b, 0x38},
	{0x367c, 0x08},
	{0x367d, 0x38},
	{0x3690, 0x43},
	{0x3691, 0x64},
	{0x3692, 0x65},
	{0x3699, 0x9f},
	{0x369a, 0x9f},
	{0x369b, 0x9f},
	{0x369c, 0x08},
	{0x369d, 0x18},
	{0x36a2, 0x08},
	{0x36a3, 0x08},
	{0x36ea, 0x34},
	{0x36eb, 0x04},
	{0x36ec, 0x0c},
	{0x36ed, 0x24},
	{0x36fa, 0x34},
	{0x36fb, 0x04},
	{0x36fc, 0x00},
	{0x36fd, 0x24},
	{0x3902, 0xc5},
	{0x3905, 0xd8},
	{0x3908, 0x11},
	{0x391b, 0x80},
	{0x391c, 0x0f},
	{0x391d, 0x21},
	{0x3933, 0x28},
	{0x3934, 0x20},
	{0x3940, 0x68},
	{0x3942, 0x08},
	{0x3943, 0x28},
	{0x3980, 0x00},
	{0x3981, 0x00},
	{0x3982, 0x00},
	{0x3983, 0x00},
	{0x3984, 0x00},
	{0x3985, 0x00},
	{0x3986, 0x00},
	{0x3987, 0x00},
	{0x3988, 0x00},
	{0x3989, 0x00},
	{0x398a, 0x00},
	{0x398b, 0x08},
	{0x398c, 0x00},
	{0x398d, 0x10},
	{0x398e, 0x00},
	{0x398f, 0x18},
	{0x3990, 0x00},
	{0x3991, 0x20},
	{0x3992, 0x03},
	{0x3993, 0xd8},
	{0x3994, 0x03},
	{0x3995, 0xe0},
	{0x3996, 0x03},
	{0x3997, 0xf0},
	{0x3998, 0x03},
	{0x3999, 0xf8},
	{0x399a, 0x00},
	{0x399b, 0x00},
	{0x399c, 0x00},
	{0x399d, 0x08},
	{0x399e, 0x00},
	{0x399f, 0x10},
	{0x39a0, 0x00},
	{0x39a1, 0x18},
	{0x39a2, 0x00},
	{0x39a3, 0x28},
	{0x39af, 0x58},
	{0x39b5, 0x30},
	{0x39b6, 0x00},
	{0x39b7, 0x34},
	{0x39b8, 0x00},
	{0x39b9, 0x00},
	{0x39ba, 0x34},
	{0x39bb, 0x00},
	{0x39bc, 0x00},
	{0x39bd, 0x00},
	{0x39be, 0x00},
	{0x39bf, 0x00},
	{0x39c0, 0x00},
	{0x39c1, 0x00},
	{0x39c5, 0x21},
	{0x39c8, 0x00},
	{0x39db, 0x20},
	{0x39dc, 0x00},
	{0x39de, 0x20},
	{0x39df, 0x00},
	{0x39e0, 0x00},
	{0x39e1, 0x00},
	{0x39e2, 0x00},
	{0x39e3, 0x00},
	{0x39e8, 0x03},
	{0x3e00, 0x00},
	{0x3e01, 0x6a},
	{0x3e02, 0x00},
	{0x3e03, 0x0b},
	{0x3e04, 0x08},
	{0x3e05, 0x00},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3e10, 0x00},
	{0x3e11, 0x80},
	{0x3e12, 0x03},
	{0x3e13, 0x40},
	{0x3e14, 0xb1},
	{0x3e23, 0x00},
	{0x3e24, 0xba},
	{0x3e25, 0x03},
	{0x3e26, 0x40},
	{0x4500, 0x08},
	{0x4501, 0xa4},
	{0x4506, 0x3e},
	{0x4509, 0x10},
	{0x4800, 0x64},
	{0x4816, 0x51},
	{0x4818, 0x00},
	{0x4819, 0x30},
	{0x481a, 0x00},
	{0x481b, 0x28},
	{0x481c, 0x00},
	{0x481d, 0xe8},
	{0x4821, 0x02},
	{0x4822, 0x00},
	{0x4823, 0x28},
	{0x4828, 0x00},
	{0x4829, 0x10},
	{0x4837, 0x1b},
	{0x5784, 0x10},
	{0x5785, 0x08},
	{0x5787, 0x00},
	{0x5788, 0x00},
	{0x5789, 0x00},
	{0x578a, 0x06},
	{0x578b, 0x06},
	{0x578c, 0x00},
	{0x5790, 0x10},
	{0x5791, 0x10},
	{0x5792, 0x00},
	{0x5793, 0x10},
	{0x5794, 0x10},
	{0x5795, 0x00},
	{0x57c4, 0x10},
	{0x57c5, 0x08},
	{0x57c7, 0x00},
	{0x57c8, 0x00},
	{0x57c9, 0x00},
	{0x57ca, 0x06},
	{0x57cb, 0x06},
	{0x57cc, 0x00},
	{0x57d0, 0x10},
	{0x57d1, 0x10},
	{0x57d2, 0x00},
	{0x57d3, 0x10},
	{0x57d4, 0x10},
	{0x57d5, 0x00},
	{0x5988, 0x86},
	{0x598e, 0x0b},
	{0x598f, 0xc6},
	{0x5a88, 0x86},
	{0x5a8e, 0x0b},
	{0x5a8f, 0xc6},
	{0x36e9, 0x20},
	{0x36f9, 0x20},
	{0x0100, 0x00},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 405Mbps
 */
static const struct regval sc4238_linear12bit_2688x1520_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x3018, 0x72},
	{0x301f, 0x96},
	{0x3031, 0x0c},
	{0x3037, 0x40},
	{0x3038, 0x22},
	{0x3106, 0x81},
	{0x3200, 0x00},
	{0x3201, 0x00},
	{0x3202, 0x00},
	{0x3203, 0x00},
	{0x3204, 0x0a},
	{0x3205, 0x87},
	{0x3206, 0x05},
	{0x3207, 0xf7},
	{0x3208, 0x0a},
	{0x3209, 0x80},
	{0x320a, 0x05},
	{0x320b, 0xf0},
	{0x320c, 0x05},
	{0x320d, 0xa0},
	{0x320e, 0x06},
	{0x320f, 0x1a},
	{0x3211, 0x04},
	{0x3213, 0x04},
	{0x3251, 0x88},
	{0x3253, 0x0a},
	{0x325f, 0x0c},
	{0x3273, 0x01},
	{0x3301, 0x30},
	{0x3304, 0x30},
	{0x3306, 0x70},
	{0x3308, 0x10},
	{0x3309, 0x50},
	{0x330b, 0xf0},
	{0x330e, 0x14},
	{0x3314, 0x94},
	{0x331e, 0x29},
	{0x331f, 0x49},
	{0x3320, 0x09},
	{0x334c, 0x10},
	{0x3352, 0x02},
	{0x3356, 0x1f},
	{0x335e, 0x02},
	{0x335f, 0x04},
	{0x3363, 0x00},
	{0x3364, 0x1e},
	{0x3366, 0x92},
	{0x336d, 0x03},
	{0x337a, 0x08},
	{0x337b, 0x10},
	{0x337c, 0x06},
	{0x337d, 0x0a},
	{0x337f, 0x2d},
	{0x3390, 0x08},
	{0x3391, 0x18},
	{0x3392, 0x38},
	{0x3393, 0x30},
	{0x3394, 0x30},
	{0x3395, 0x30},
	{0x3399, 0xff},
	{0x33a2, 0x08},
	{0x33a3, 0x0c},
	{0x33e0, 0xa0},
	{0x33e1, 0x08},
	{0x33e2, 0x00},
	{0x33e3, 0x10},
	{0x33e4, 0x10},
	{0x33e5, 0x00},
	{0x33e6, 0x10},
	{0x33e7, 0x10},
	{0x33e8, 0x00},
	{0x33e9, 0x10},
	{0x33ea, 0x16},
	{0x33eb, 0x00},
	{0x33ec, 0x10},
	{0x33ed, 0x18},
	{0x33ee, 0xa0},
	{0x33ef, 0x08},
	{0x33f4, 0x00},
	{0x33f5, 0x10},
	{0x33f6, 0x10},
	{0x33f7, 0x00},
	{0x33f8, 0x10},
	{0x33f9, 0x10},
	{0x33fa, 0x00},
	{0x33fb, 0x10},
	{0x33fc, 0x16},
	{0x33fd, 0x00},
	{0x33fe, 0x10},
	{0x33ff, 0x18},
	{0x360f, 0x05},
	{0x3622, 0xee},
	{0x3625, 0x0a},
	{0x3630, 0xa8},
	{0x3631, 0x80},
	{0x3633, 0x44},
	{0x3634, 0x34},
	{0x3635, 0x60},
	{0x3636, 0x20},
	{0x3637, 0x11},
	{0x3638, 0x2a},
	{0x363a, 0x1f},
	{0x363b, 0x03},
	{0x366e, 0x04},
	{0x3670, 0x4a},
	{0x3671, 0xee},
	{0x3672, 0x0e},
	{0x3673, 0x0e},
	{0x3674, 0x70},
	{0x3675, 0x40},
	{0x3676, 0x45},
	{0x367a, 0x08},
	{0x367b, 0x38},
	{0x367c, 0x08},
	{0x367d, 0x38},
	{0x3690, 0x43},
	{0x3691, 0x63},
	{0x3692, 0x63},
	{0x3699, 0x80},
	{0x369a, 0x9f},
	{0x369b, 0x9f},
	{0x369c, 0x08},
	{0x369d, 0x38},
	{0x36a2, 0x08},
	{0x36a3, 0x38},
	{0x36ea, 0xf1},
	{0x36eb, 0x16},
	{0x36ec, 0x0e},
	{0x36ed, 0x04},
	{0x36fa, 0x31},
	{0x36fb, 0x09},
	{0x36fc, 0x00},
	{0x36fd, 0x24},
	{0x3902, 0xc5},
	{0x3905, 0xd8},
	{0x3908, 0x11},
	{0x391b, 0x80},
	{0x391c, 0x0f},
	{0x3933, 0x28},
	{0x3934, 0x20},
	{0x3940, 0x6c},
	{0x3942, 0x08},
	{0x3943, 0x28},
	{0x3980, 0x00},
	{0x3981, 0x00},
	{0x3982, 0x00},
	{0x3983, 0x00},
	{0x3984, 0x00},
	{0x3985, 0x00},
	{0x3986, 0x00},
	{0x3987, 0x00},
	{0x3988, 0x00},
	{0x3989, 0x00},
	{0x398a, 0x00},
	{0x398b, 0x04},
	{0x398c, 0x00},
	{0x398d, 0x04},
	{0x398e, 0x00},
	{0x398f, 0x08},
	{0x3990, 0x00},
	{0x3991, 0x10},
	{0x3992, 0x03},
	{0x3993, 0xd8},
	{0x3994, 0x03},
	{0x3995, 0xe0},
	{0x3996, 0x03},
	{0x3997, 0xf0},
	{0x3998, 0x03},
	{0x3999, 0xf8},
	{0x399a, 0x00},
	{0x399b, 0x00},
	{0x399c, 0x00},
	{0x399d, 0x08},
	{0x399e, 0x00},
	{0x399f, 0x10},
	{0x39a0, 0x00},
	{0x39a1, 0x18},
	{0x39a2, 0x00},
	{0x39a3, 0x28},
	{0x39af, 0x58},
	{0x39b5, 0x30},
	{0x39b6, 0x00},
	{0x39b7, 0x34},
	{0x39b8, 0x00},
	{0x39b9, 0x00},
	{0x39ba, 0x34},
	{0x39bb, 0x00},
	{0x39bc, 0x00},
	{0x39bd, 0x00},
	{0x39be, 0x00},
	{0x39bf, 0x00},
	{0x39c0, 0x00},
	{0x39c1, 0x00},
	{0x39c5, 0x21},
	{0x39c8, 0x00},
	{0x39db, 0x20},
	{0x39dc, 0x00},
	{0x39de, 0x20},
	{0x39df, 0x00},
	{0x39e0, 0x00},
	{0x39e1, 0x00},
	{0x39e2, 0x00},
	{0x39e3, 0x00},
	{0x3e00, 0x00},
	{0x3e01, 0xc2},
	{0x3e02, 0xa0},
	{0x3e03, 0x0b},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3e14, 0xb1},
	{0x3e25, 0x03},
	{0x3e26, 0x40},
	{0x4501, 0xb4},
	{0x4509, 0x20},
	{0x4800, 0x64},
	{0x4818, 0x00},
	{0x4819, 0x30},
	{0x481a, 0x00},
	{0x481b, 0x0b},
	{0x481c, 0x00},
	{0x481d, 0xc8},
	{0x4821, 0x02},
	{0x4822, 0x00},
	{0x4823, 0x03},
	{0x4828, 0x00},
	{0x4829, 0x02},
	{0x4837, 0x3b},
	{0x5784, 0x10},
	{0x5785, 0x08},
	{0x5787, 0x06},
	{0x5788, 0x06},
	{0x5789, 0x00},
	{0x578a, 0x06},
	{0x578b, 0x06},
	{0x578c, 0x00},
	{0x5790, 0x10},
	{0x5791, 0x10},
	{0x5792, 0x00},
	{0x5793, 0x10},
	{0x5794, 0x10},
	{0x5795, 0x00},
	{0x57c4, 0x10},
	{0x57c5, 0x08},
	{0x57c7, 0x06},
	{0x57c8, 0x06},
	{0x57c9, 0x00},
	{0x57ca, 0x06},
	{0x57cb, 0x06},
	{0x57cc, 0x00},
	{0x57d0, 0x10},
	{0x57d1, 0x10},
	{0x57d2, 0x00},
	{0x57d3, 0x10},
	{0x57d4, 0x10},
	{0x57d5, 0x00},
	{0x5988, 0x86},
	{0x598e, 0x05},
	{0x598f, 0x6c},
	{0x36e9, 0x53},
	{0x36f9, 0x51},
	{0x0100, 0x00},
	{REG_NULL, 0x00},
};

/*
 * Xclk 27Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 405Mbps
 */
static const struct regval sc4238_linear12bit_2560x1440_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x3018, 0x72},
	{0x301f, 0x91},
	{0x3031, 0x0c},
	{0x3037, 0x40},
	{0x3038, 0x22},
	{0x3106, 0x81},
	{0x3200, 0x00},
	{0x3201, 0x40},
	{0x3202, 0x00},
	{0x3203, 0x28},
	{0x3204, 0x0a},
	{0x3205, 0x47},
	{0x3206, 0x05},
	{0x3207, 0xcf},
	{0x3208, 0x0a},
	{0x3209, 0x00},
	{0x320a, 0x05},
	{0x320b, 0xa0},
	{0x320c, 0x05},
	{0x320d, 0xa0},
	{0x320e, 0x06},
	{0x320f, 0x1a},
	{0x3210, 0x00},
	{0x3211, 0x04},
	{0x3212, 0x00},
	{0x3213, 0x04},
	{0x3251, 0x88},
	{0x3253, 0x0a},
	{0x325f, 0x0c},
	{0x3273, 0x01},
	{0x3301, 0x30},
	{0x3304, 0x30},
	{0x3306, 0x70},
	{0x3308, 0x10},
	{0x3309, 0x50},
	{0x330b, 0xf0},
	{0x330e, 0x14},
	{0x3314, 0x94},
	{0x331e, 0x29},
	{0x331f, 0x49},
	{0x3320, 0x09},
	{0x334c, 0x10},
	{0x3352, 0x02},
	{0x3356, 0x1f},
	{0x335e, 0x02},
	{0x335f, 0x04},
	{0x3363, 0x00},
	{0x3364, 0x1e},
	{0x3366, 0x92},
	{0x336d, 0x03},
	{0x337a, 0x08},
	{0x337b, 0x10},
	{0x337c, 0x06},
	{0x337d, 0x0a},
	{0x337f, 0x2d},
	{0x3390, 0x08},
	{0x3391, 0x18},
	{0x3392, 0x38},
	{0x3393, 0x30},
	{0x3394, 0x30},
	{0x3395, 0x30},
	{0x3399, 0xff},
	{0x33a2, 0x08},
	{0x33a3, 0x0c},
	{0x33e0, 0xa0},
	{0x33e1, 0x08},
	{0x33e2, 0x00},
	{0x33e3, 0x10},
	{0x33e4, 0x10},
	{0x33e5, 0x00},
	{0x33e6, 0x10},
	{0x33e7, 0x10},
	{0x33e8, 0x00},
	{0x33e9, 0x10},
	{0x33ea, 0x16},
	{0x33eb, 0x00},
	{0x33ec, 0x10},
	{0x33ed, 0x18},
	{0x33ee, 0xa0},
	{0x33ef, 0x08},
	{0x33f4, 0x00},
	{0x33f5, 0x10},
	{0x33f6, 0x10},
	{0x33f7, 0x00},
	{0x33f8, 0x10},
	{0x33f9, 0x10},
	{0x33fa, 0x00},
	{0x33fb, 0x10},
	{0x33fc, 0x16},
	{0x33fd, 0x00},
	{0x33fe, 0x10},
	{0x33ff, 0x18},
	{0x360f, 0x05},
	{0x3622, 0xee},
	{0x3625, 0x0a},
	{0x3630, 0xa8},
	{0x3631, 0x80},
	{0x3633, 0x44},
	{0x3634, 0x34},
	{0x3635, 0x60},
	{0x3636, 0x20},
	{0x3637, 0x11},
	{0x3638, 0x2a},
	{0x363a, 0x1f},
	{0x363b, 0x03},
	{0x366e, 0x04},
	{0x3670, 0x4a},
	{0x3671, 0xee},
	{0x3672, 0x0e},
	{0x3673, 0x0e},
	{0x3674, 0x70},
	{0x3675, 0x40},
	{0x3676, 0x45},
	{0x367a, 0x08},
	{0x367b, 0x38},
	{0x367c, 0x08},
	{0x367d, 0x38},
	{0x3690, 0x43},
	{0x3691, 0x63},
	{0x3692, 0x63},
	{0x3699, 0x80},
	{0x369a, 0x9f},
	{0x369b, 0x9f},
	{0x369c, 0x08},
	{0x369d, 0x38},
	{0x36a2, 0x08},
	{0x36a3, 0x38},
	{0x36ea, 0x3b},
	{0x36eb, 0x16},
	{0x36ec, 0x0e},
	{0x36ed, 0x14},
	{0x36fa, 0x36},
	{0x36fb, 0x09},
	{0x36fc, 0x00},
	{0x36fd, 0x24},
	{0x3902, 0xc5},
	{0x3905, 0xd8},
	{0x3908, 0x11},
	{0x391b, 0x80},
	{0x391c, 0x0f},
	{0x3933, 0x28},
	{0x3934, 0x20},
	{0x3940, 0x6c},
	{0x3942, 0x08},
	{0x3943, 0x28},
	{0x3980, 0x00},
	{0x3981, 0x00},
	{0x3982, 0x00},
	{0x3983, 0x00},
	{0x3984, 0x00},
	{0x3985, 0x00},
	{0x3986, 0x00},
	{0x3987, 0x00},
	{0x3988, 0x00},
	{0x3989, 0x00},
	{0x398a, 0x00},
	{0x398b, 0x04},
	{0x398c, 0x00},
	{0x398d, 0x04},
	{0x398e, 0x00},
	{0x398f, 0x08},
	{0x3990, 0x00},
	{0x3991, 0x10},
	{0x3992, 0x03},
	{0x3993, 0xd8},
	{0x3994, 0x03},
	{0x3995, 0xe0},
	{0x3996, 0x03},
	{0x3997, 0xf0},
	{0x3998, 0x03},
	{0x3999, 0xf8},
	{0x399a, 0x00},
	{0x399b, 0x00},
	{0x399c, 0x00},
	{0x399d, 0x08},
	{0x399e, 0x00},
	{0x399f, 0x10},
	{0x39a0, 0x00},
	{0x39a1, 0x18},
	{0x39a2, 0x00},
	{0x39a3, 0x28},
	{0x39af, 0x58},
	{0x39b5, 0x30},
	{0x39b6, 0x00},
	{0x39b7, 0x34},
	{0x39b8, 0x00},
	{0x39b9, 0x00},
	{0x39ba, 0x34},
	{0x39bb, 0x00},
	{0x39bc, 0x00},
	{0x39bd, 0x00},
	{0x39be, 0x00},
	{0x39bf, 0x00},
	{0x39c0, 0x00},
	{0x39c1, 0x00},
	{0x39c5, 0x21},
	{0x39c8, 0x00},
	{0x39db, 0x20},
	{0x39dc, 0x00},
	{0x39de, 0x20},
	{0x39df, 0x00},
	{0x39e0, 0x00},
	{0x39e1, 0x00},
	{0x39e2, 0x00},
	{0x39e3, 0x00},
	{0x3e00, 0x00},
	{0x3e01, 0xc2},
	{0x3e02, 0xa0},
	{0x3e03, 0x0b},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3e14, 0xb1},
	{0x3e25, 0x03},
	{0x3e26, 0x40},
	{0x4501, 0xb4},
	{0x4509, 0x20},
	{0x4800, 0x64},
	{0x4818, 0x00},
	{0x4819, 0x30},
	{0x481a, 0x00},
	{0x481b, 0x0b},
	{0x481c, 0x00},
	{0x481d, 0xc8},
	{0x4821, 0x02},
	{0x4822, 0x00},
	{0x4823, 0x03},
	{0x4828, 0x00},
	{0x4829, 0x02},
	{0x4837, 0x3b},
	{0x5784, 0x10},
	{0x5785, 0x08},
	{0x5787, 0x06},
	{0x5788, 0x06},
	{0x5789, 0x00},
	{0x578a, 0x06},
	{0x578b, 0x06},
	{0x578c, 0x00},
	{0x5790, 0x10},
	{0x5791, 0x10},
	{0x5792, 0x00},
	{0x5793, 0x10},
	{0x5794, 0x10},
	{0x5795, 0x00},
	{0x57c4, 0x10},
	{0x57c5, 0x08},
	{0x57c7, 0x06},
	{0x57c8, 0x06},
	{0x57c9, 0x00},
	{0x57ca, 0x06},
	{0x57cb, 0x06},
	{0x57cc, 0x00},
	{0x57d0, 0x10},
	{0x57d1, 0x10},
	{0x57d2, 0x00},
	{0x57d3, 0x10},
	{0x57d4, 0x10},
	{0x57d5, 0x00},
	{0x5988, 0x86},
	{0x598e, 0x05},
	{0x598f, 0x6c},
	{0x36e9, 0x25},
	{0x36f9, 0x54},
	{0x0100, 0x00},
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
static const struct sc4238_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.width = 2688,
		.height = 1520,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.exp_def = 0x0600,
		.hts_def = 0x05A0 * 2,
		.vts_def = 0x0752,
		.reg_list = sc4238_linear10bit_2688x1520_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
		.link_freq = 0, /* an index in link_freq[] */
		.pixel_rate = PIXEL_RATE_WITH_200M,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.width = 2688,
		.height = 1520,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
			/*.denominator = 300000,*/
		},
		.exp_def = 0x0c00,
		.hts_def = 0x060e * 2,
		.vts_def = 0x0e83,
		/*.vts_def = 0x0c18,*/
		.reg_list = sc4238_hdr10bit_2688x1520_regs,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
		.link_freq = 1, /* an index in link_freq[] */
		.pixel_rate = PIXEL_RATE_WITH_360M,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR12_1X12,
		.width = 2688,
		.height = 1520,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0600,
		.hts_def = 0x05a0 * 2,
		.vts_def = 0x061a,
		.reg_list = sc4238_linear12bit_2688x1520_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
		.link_freq = 0, /* an index in link_freq[] */
		.pixel_rate = PIXEL_RATE_WITH_200M,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR12_1X12,
		.width = 2560,
		.height = 1440,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0500,
		.hts_def = 0x05a0 * 2,
		.vts_def = 0x061a,
		.reg_list = sc4238_linear12bit_2560x1440_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
		.link_freq = 0, /* an index in link_freq[] */
		.pixel_rate = PIXEL_RATE_WITH_200M,
	},
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ_200M,
	MIPI_FREQ_360M,
};

static const char * const sc4238_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

static int __sc4238_power_on(struct sc4238 *sc4238);

/* Write registers up to 4 at a time */
static int sc4238_write_reg(struct i2c_client *client, u16 reg,
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

static int sc4238_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret |= sc4238_write_reg(client, regs[i].addr,
			SC4238_REG_VALUE_08BIT, regs[i].val);
	}
	return ret;
}

/* Read registers up to 4 at a time */
static int sc4238_read_reg(struct i2c_client *client,
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

static int sc4238_get_reso_dist(const struct sc4238_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc4238_mode *
sc4238_find_best_fit(struct sc4238 *sc4238, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < sc4238->cfg_num; i++) {
		dist = sc4238_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
			(supported_modes[i].bus_fmt == framefmt->code)) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int sc4238_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sc4238 *sc4238 = to_sc4238(sd);
	const struct sc4238_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&sc4238->mutex);

	mode = sc4238_find_best_fit(sc4238, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc4238->mutex);
		return -ENOTTY;
#endif
	} else {
		sc4238->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc4238->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc4238->vblank, vblank_def,
					 SC4238_VTS_MAX - mode->height,
					 1, vblank_def);
		__v4l2_ctrl_s_ctrl_int64(sc4238->pixel_rate,
					 mode->pixel_rate);
		__v4l2_ctrl_s_ctrl(sc4238->link_freq,
					 mode->link_freq);
		sc4238->cur_fps = mode->max_fps;
		sc4238->cur_vts = mode->vts_def;
	}

	mutex_unlock(&sc4238->mutex);

	return 0;
}

static int sc4238_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sc4238 *sc4238 = to_sc4238(sd);
	const struct sc4238_mode *mode = sc4238->cur_mode;

	mutex_lock(&sc4238->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc4238->mutex);
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
	mutex_unlock(&sc4238->mutex);

	return 0;
}

static int sc4238_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct sc4238 *sc4238 = to_sc4238(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = sc4238->cur_mode->bus_fmt;

	return 0;
}

static int sc4238_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct sc4238 *sc4238 = to_sc4238(sd);

	if (fse->index >= sc4238->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int sc4238_enable_test_pattern(struct sc4238 *sc4238, u32 pattern)
{
	u32 val = 0;
	int ret = 0;

	ret = sc4238_read_reg(sc4238->client, SC4238_REG_TEST_PATTERN,
			       SC4238_REG_VALUE_08BIT, &val);
	if (pattern)
		val |= SC4238_TEST_PATTERN_BIT_MASK;
	else
		val &= ~SC4238_TEST_PATTERN_BIT_MASK;

	ret |= sc4238_write_reg(sc4238->client, SC4238_REG_TEST_PATTERN,
				SC4238_REG_VALUE_08BIT, val);
	return ret;
}

static int sc4238_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct sc4238 *sc4238 = to_sc4238(sd);
	const struct sc4238_mode *mode = sc4238->cur_mode;

	if (sc4238->streaming)
		fi->interval = sc4238->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

static int sc4238_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct sc4238 *sc4238 = to_sc4238(sd);
	const struct sc4238_mode *mode = sc4238->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = SC4238_LANES |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	if (mode->hdr_mode == HDR_X2)
		val = SC4238_LANES |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		V4L2_MBUS_CSI2_CHANNEL_1;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void sc4238_get_module_inf(struct sc4238 *sc4238,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, SC4238_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, sc4238->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, sc4238->len_name, sizeof(inf->base.lens));
}

static int sc4238_get_gain_reg(struct sc4238 *sc4238, u32 total_gain,
			       u32 *again_coarse_reg, u32 *again_fine_reg,
			       u32 *dgain_coarse_reg, u32 *dgain_fine_reg)
{
	u32 again, dgain;

	if (total_gain > 32004) {
		dev_err(&sc4238->client->dev,
			"total_gain max is 15.875*31.5*64, current total_gain is %d\n",
			total_gain);
		return -EINVAL;
	}

	if (total_gain > 1016) {/*15.875*/
		again = 1016;
		dgain = total_gain * 128 / 1016;
	} else {
		again = total_gain;
		dgain = 128;
	}

	if (again < 0x80) { /*1x ~ 2x*/
		*again_fine_reg = again & 0x7f;
		*again_coarse_reg = 0x03;
	} else if (again < 0x100) { /*2x ~ 4x*/
		*again_fine_reg = (again >> 1) & 0x7f;
		*again_coarse_reg = 0x07;
	} else if (again < 0x200) { /*4x ~ 8x*/
		*again_fine_reg = (again >> 2) & 0x7f;
		*again_coarse_reg = 0x0f;
	} else { /*8x ~ 16x*/
		*again_fine_reg = (again >> 3) & 0x7f;
		*again_coarse_reg = 0x1f;
	}

	if (dgain < 0x100) { /*1x ~ 2x*/
		*dgain_fine_reg = dgain & 0xff;
		*dgain_coarse_reg = 0x00;
	} else if (dgain < 0x200) { /*2x ~ 4x*/
		*dgain_fine_reg = (dgain >> 1) & 0xff;
		*dgain_coarse_reg = 0x01;
	} else if (dgain < 0x400) { /*4x ~ 8x*/
		*dgain_fine_reg = (dgain >> 2) & 0xff;
		*dgain_coarse_reg = 0x03;
	} else if (dgain < 0x800) { /*8x ~ 16x*/
		*dgain_fine_reg = (dgain >> 3) & 0xff;
		*dgain_coarse_reg = 0x07;
	} else { /*16x ~ 31.5x*/
		*dgain_fine_reg = (dgain >> 4) & 0xff;
		*dgain_coarse_reg = 0x0f;
	}
	dev_dbg(&sc4238->client->dev,
		"total_gain 0x%x again_coarse 0x%x, again_fine 0x%x, dgain_coarse 0x%x, dgain_fine 0x%x\n",
		total_gain, *again_coarse_reg, *again_fine_reg, *dgain_coarse_reg, *dgain_fine_reg);
	return 0;
}

static int sc4238_set_hdrae(struct sc4238 *sc4238,
			     struct preisp_hdrae_exp_s *ae)
{
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_a_gain, m_a_gain, s_a_gain;
	u32 again_coarse_reg, again_fine_reg;
	u32 dgain_coarse_reg, dgain_fine_reg;
	u32 max_exp_l, max_exp_s;
	int ret = 0;
	u32 rhs1 = 0;

	if (!sc4238->has_init_exp && !sc4238->streaming) {
		sc4238->init_hdrae_exp = *ae;
		sc4238->has_init_exp = true;
		dev_info(&sc4238->client->dev, "sc4238 don't stream, record exp for hdr!\n");
		return ret;
	}
	l_exp_time = ae->long_exp_reg;
	m_exp_time = ae->middle_exp_reg;
	s_exp_time = ae->short_exp_reg;
	l_a_gain = ae->long_gain_reg;
	m_a_gain = ae->middle_gain_reg;
	s_a_gain = ae->short_gain_reg;

	dev_dbg(&sc4238->client->dev,
		"rev exp req: L_exp: 0x%x, 0x%x, M_exp: 0x%x, 0x%x S_exp: 0x%x, 0x%x\n",
		l_exp_time, l_a_gain,
		m_exp_time, m_a_gain,
		s_exp_time, s_a_gain);

	if (sc4238->cur_mode->hdr_mode == HDR_X2) {
		//2 stagger
		l_a_gain = m_a_gain;
		l_exp_time = m_exp_time;
		m_a_gain = s_a_gain;
		m_exp_time = s_exp_time;
	}

	if (l_a_gain != m_a_gain) {
		dev_err(&sc4238->client->dev,
			"gain of long frame must same with short frame, 0x%x != 0x%x\n",
			l_a_gain, m_a_gain);
	}
	/* long frame exp max = 2*({320e,320f} -{3e23,3e24} -9) ,unit 1/2 line */
	/* short frame exp max = 2*({3e23,3e24} - 8) ,unit 1/2 line */
	//max short exposure limit to 3 ms
	rhs1 = 286;
	max_exp_l = sc4238->cur_vts - rhs1 - 9;
	max_exp_s = rhs1 - 8;
	if (l_exp_time > max_exp_l || m_exp_time > max_exp_s || l_exp_time <= m_exp_time) {
		dev_err(&sc4238->client->dev,
			"max_exp_long %d, max_exp_short %d, cur_exp_long %d, cur_exp_short %d\n",
			max_exp_l, max_exp_s, l_exp_time, m_exp_time);
	}

	ret = sc4238_get_gain_reg(sc4238, l_a_gain,
				  &again_coarse_reg, &again_fine_reg,
				  &dgain_coarse_reg, &dgain_fine_reg);
	if (ret != 0)
		return -EINVAL;

	dev_dbg(&sc4238->client->dev,
		"max exposure reg limit 0x%x-8 line\n", rhs1);
	ret |= sc4238_write_reg(sc4238->client,
				SC4238_REG_EXP_MAX_MID_H,
				SC4238_REG_VALUE_16BIT,
				rhs1);

	ret |= sc4238_write_reg(sc4238->client,
				SC4238_REG_EXP_LONG_H,
				SC4238_REG_VALUE_24BIT,
				l_exp_time << 5);
	ret |= sc4238_write_reg(sc4238->client,
				SC4238_REG_EXP_MID_H,
				SC4238_REG_VALUE_16BIT,
				m_exp_time << 5);

	ret |= sc4238_write_reg(sc4238->client,
				SC4238_REG_COARSE_AGAIN_L,
				SC4238_REG_VALUE_08BIT,
				again_coarse_reg);
	ret |= sc4238_write_reg(sc4238->client,
				SC4238_REG_FINE_AGAIN_L,
				SC4238_REG_VALUE_08BIT,
				again_fine_reg);
	ret |= sc4238_write_reg(sc4238->client,
				SC4238_REG_COARSE_AGAIN_S,
				SC4238_REG_VALUE_08BIT,
				again_coarse_reg);
	ret |= sc4238_write_reg(sc4238->client,
				SC4238_REG_FINE_AGAIN_S,
				SC4238_REG_VALUE_08BIT,
				again_fine_reg);
	ret |= sc4238_write_reg(sc4238->client,
				SC4238_REG_COARSE_DGAIN_L,
				SC4238_REG_VALUE_08BIT,
				dgain_coarse_reg);
	ret |= sc4238_write_reg(sc4238->client,
				SC4238_REG_FINE_DGAIN_L,
				SC4238_REG_VALUE_08BIT,
				dgain_fine_reg);
	ret |= sc4238_write_reg(sc4238->client,
				SC4238_REG_COARSE_DGAIN_S,
				SC4238_REG_VALUE_08BIT,
				dgain_coarse_reg);
	ret |= sc4238_write_reg(sc4238->client,
				SC4238_REG_FINE_DGAIN_S,
				SC4238_REG_VALUE_08BIT,
				dgain_fine_reg);

	return ret;
}

static int sc4238_get_channel_info(struct sc4238 *sc4238, struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = sc4238->cur_mode->vc[ch_info->index];
	ch_info->width = sc4238->cur_mode->width;
	ch_info->height = sc4238->cur_mode->height;
	ch_info->bus_fmt = sc4238->cur_mode->bus_fmt;
	return 0;
}

static long sc4238_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc4238 *sc4238 = to_sc4238(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	struct rkmodule_channel_info *ch_info;
	long ret = 0;
	u32 i, h, w;
	u32 stream = 0;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		return sc4238_set_hdrae(sc4238, arg);
	case RKMODULE_SET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		w = sc4238->cur_mode->width;
		h = sc4238->cur_mode->height;

		dev_info(&sc4238->client->dev,
			"%s config hdr mode: %d\n",
			__func__, hdr_cfg->hdr_mode);
		for (i = 0; i < sc4238->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			h == supported_modes[i].height &&
			supported_modes[i].hdr_mode == hdr_cfg->hdr_mode) {
				sc4238->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == sc4238->cfg_num) {
			dev_err(&sc4238->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr_cfg->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = sc4238->cur_mode->hts_def - sc4238->cur_mode->width;
			h = sc4238->cur_mode->vts_def - sc4238->cur_mode->height;
			__v4l2_ctrl_modify_range(sc4238->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(sc4238->vblank, h,
				SC4238_VTS_MAX - sc4238->cur_mode->height,
				1, h);
			sc4238->cur_fps = sc4238->cur_mode->max_fps;
			sc4238->cur_vts = sc4238->cur_mode->vts_def;
			dev_info(&sc4238->client->dev,
				"sensor mode: %d\n",
				sc4238->cur_mode->hdr_mode);
		}
		break;
	case RKMODULE_GET_MODULE_INFO:
		sc4238_get_module_inf(sc4238, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = sc4238->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = sc4238_write_reg(sc4238->client, SC4238_REG_CTRL_MODE,
				SC4238_REG_VALUE_08BIT, SC4238_MODE_STREAMING);
		else
			ret = sc4238_write_reg(sc4238->client, SC4238_REG_CTRL_MODE,
				SC4238_REG_VALUE_08BIT, SC4238_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = sc4238_get_channel_info(sc4238, ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc4238_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
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

		ret = sc4238_ioctl(sd, cmd, inf);
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
			ret = sc4238_ioctl(sd, cmd, cfg);
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

		ret = sc4238_ioctl(sd, cmd, hdr);
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
			ret = sc4238_ioctl(sd, cmd, hdr);
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
			ret = sc4238_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = sc4238_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc4238_ioctl(sd, cmd, ch_info);
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

static int __sc4238_start_stream(struct sc4238 *sc4238)
{
	int ret;

	ret = sc4238_write_array(sc4238->client, sc4238->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&sc4238->ctrl_handler);
	if (ret)
		return ret;
	if (sc4238->has_init_exp && sc4238->cur_mode->hdr_mode != NO_HDR) {
		ret = sc4238_ioctl(&sc4238->subdev, PREISP_CMD_SET_HDRAE_EXP,
				   &sc4238->init_hdrae_exp);
		if (ret) {
			dev_err(&sc4238->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}
	return sc4238_write_reg(sc4238->client, SC4238_REG_CTRL_MODE,
		SC4238_REG_VALUE_08BIT, SC4238_MODE_STREAMING);
}

static int __sc4238_stop_stream(struct sc4238 *sc4238)
{
	sc4238->has_init_exp = false;
	if (sc4238->is_thunderboot)
		sc4238->is_first_streamoff = true;
	return sc4238_write_reg(sc4238->client, SC4238_REG_CTRL_MODE,
		SC4238_REG_VALUE_08BIT, SC4238_MODE_SW_STANDBY);
}

static int sc4238_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc4238 *sc4238 = to_sc4238(sd);
	struct i2c_client *client = sc4238->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d hdr mode(%d)\n",
				__func__, on,
				sc4238->cur_mode->width,
				sc4238->cur_mode->height,
		DIV_ROUND_CLOSEST(sc4238->cur_mode->max_fps.denominator,
				  sc4238->cur_mode->max_fps.numerator),
				sc4238->cur_mode->hdr_mode);

	mutex_lock(&sc4238->mutex);
	on = !!on;
	if (on == sc4238->streaming)
		goto unlock_and_return;

	if (on) {
		if (sc4238->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
			sc4238->is_thunderboot = false;
			__sc4238_power_on(sc4238);
		}
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __sc4238_start_stream(sc4238);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc4238_stop_stream(sc4238);
		pm_runtime_put(&client->dev);
	}

	sc4238->streaming = on;

unlock_and_return:
	mutex_unlock(&sc4238->mutex);

	return ret;
}

static int sc4238_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc4238 *sc4238 = to_sc4238(sd);
	struct i2c_client *client = sc4238->client;
	int ret = 0;

	mutex_lock(&sc4238->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc4238->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = sc4238_write_array(sc4238->client, sc4238_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		sc4238->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc4238->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc4238->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 sc4238_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, SC4238_XVCLK_FREQ / 1000 / 1000);
}

static int __sc4238_power_on(struct sc4238 *sc4238)
{
	int ret;
	u32 delay_us;
	struct device *dev = &sc4238->client->dev;

	if (sc4238->is_thunderboot)
		return 0;

	if (!IS_ERR_OR_NULL(sc4238->pins_default)) {
		ret = pinctrl_select_state(sc4238->pinctrl,
					   sc4238->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(sc4238->xvclk, SC4238_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(sc4238->xvclk) != SC4238_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc4238->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(sc4238->reset_gpio))
		gpiod_set_value_cansleep(sc4238->reset_gpio, 1);

	ret = regulator_bulk_enable(SC4238_NUM_SUPPLIES, sc4238->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(sc4238->reset_gpio))
		gpiod_set_value_cansleep(sc4238->reset_gpio, 0);

	usleep_range(500, 1000);
	if (!IS_ERR(sc4238->pwdn_gpio))
		gpiod_set_value_cansleep(sc4238->pwdn_gpio, 1);
	/*
	 * There is no need to wait for the delay of RC circuit
	 * if the reset signal is directly controlled by GPIO.
	 */
	if (!IS_ERR(sc4238->reset_gpio))
		usleep_range(6000, 8000);
	else
		usleep_range(12000, 16000);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = sc4238_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(sc4238->xvclk);

	return ret;
}

static void __sc4238_power_off(struct sc4238 *sc4238)
{
	int ret;
	struct device *dev = &sc4238->client->dev;

	if (sc4238->is_thunderboot) {
		if (sc4238->is_first_streamoff) {
			sc4238->is_thunderboot = false;
			sc4238->is_first_streamoff = false;
		} else {
			return;
		}
	}

	if (!IS_ERR(sc4238->pwdn_gpio))
		gpiod_set_value_cansleep(sc4238->pwdn_gpio, 0);

	clk_disable_unprepare(sc4238->xvclk);

	if (!IS_ERR(sc4238->reset_gpio))
		gpiod_set_value_cansleep(sc4238->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(sc4238->pins_sleep)) {
		ret = pinctrl_select_state(sc4238->pinctrl,
					   sc4238->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}

	if (sc4238->is_thunderboot_ng) {
		sc4238->is_thunderboot_ng = false;
		regulator_bulk_disable(SC4238_NUM_SUPPLIES, sc4238->supplies);
	}
}

static int sc4238_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc4238 *sc4238 = to_sc4238(sd);

	return __sc4238_power_on(sc4238);
}

static int sc4238_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc4238 *sc4238 = to_sc4238(sd);

	__sc4238_power_off(sc4238);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc4238_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc4238 *sc4238 = to_sc4238(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc4238_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc4238->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc4238->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sc4238_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct sc4238 *sc4238 = to_sc4238(sd);

	if (fie->index >= sc4238->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops sc4238_pm_ops = {
	SET_RUNTIME_PM_OPS(sc4238_runtime_suspend,
			   sc4238_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc4238_internal_ops = {
	.open = sc4238_open,
};
#endif

static const struct v4l2_subdev_core_ops sc4238_core_ops = {
	.s_power = sc4238_s_power,
	.ioctl = sc4238_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc4238_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc4238_video_ops = {
	.s_stream = sc4238_s_stream,
	.g_frame_interval = sc4238_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops sc4238_pad_ops = {
	.enum_mbus_code = sc4238_enum_mbus_code,
	.enum_frame_size = sc4238_enum_frame_sizes,
	.enum_frame_interval = sc4238_enum_frame_interval,
	.get_fmt = sc4238_get_fmt,
	.set_fmt = sc4238_set_fmt,
	.get_mbus_config = sc4238_g_mbus_config,
};

static const struct v4l2_subdev_ops sc4238_subdev_ops = {
	.core	= &sc4238_core_ops,
	.video	= &sc4238_video_ops,
	.pad	= &sc4238_pad_ops,
};

static void sc4238_modify_fps_info(struct sc4238 *sc4238)
{
	const struct sc4238_mode *mode = sc4238->cur_mode;

	sc4238->cur_fps.denominator = mode->max_fps.denominator * mode->vts_def /
				      sc4238->cur_vts;
}

static int sc4238_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc4238 *sc4238 = container_of(ctrl->handler,
					     struct sc4238, ctrl_handler);
	struct i2c_client *client = sc4238->client;
	s64 max;
	int ret = 0;
	u32 val = 0;
	u32 again_coarse_reg = 0;
	u32 again_fine_reg = 0;
	u32 dgain_coarse_reg = 0;
	u32 dgain_fine_reg = 0;

	dev_dbg(&client->dev, "ctrl->id(0x%x) val 0x%x\n",
		ctrl->id, ctrl->val);

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc4238->cur_mode->height + ctrl->val - 5;
		__v4l2_ctrl_modify_range(sc4238->exposure,
					 sc4238->exposure->minimum, max,
					 sc4238->exposure->step,
					 sc4238->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = sc4238_write_reg(sc4238->client,
					SC4238_REG_EXP_LONG_H,
					SC4238_REG_VALUE_24BIT,
					ctrl->val << 5);
		dev_dbg(&client->dev, "set exposure 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = sc4238_get_gain_reg(sc4238, ctrl->val,
					  &again_coarse_reg, &again_fine_reg,
					  &dgain_coarse_reg, &dgain_fine_reg);
		ret |= sc4238_write_reg(sc4238->client,
					SC4238_REG_COARSE_AGAIN_L,
					SC4238_REG_VALUE_08BIT,
					again_coarse_reg);
		ret |= sc4238_write_reg(sc4238->client,
					SC4238_REG_FINE_AGAIN_L,
					SC4238_REG_VALUE_08BIT,
					again_fine_reg);
		ret |= sc4238_write_reg(sc4238->client,
					SC4238_REG_COARSE_DGAIN_L,
					SC4238_REG_VALUE_08BIT,
					dgain_coarse_reg);
		ret |= sc4238_write_reg(sc4238->client,
					SC4238_REG_FINE_DGAIN_L,
					SC4238_REG_VALUE_08BIT,
					dgain_fine_reg);
		dev_dbg(&client->dev, "set analog gain 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = sc4238_write_reg(sc4238->client, SC4238_REG_VTS_H,
					SC4238_REG_VALUE_16BIT,
					ctrl->val + sc4238->cur_mode->height);
		if (ret == 0)
			sc4238->cur_vts = ctrl->val + sc4238->cur_mode->height;
		if (sc4238->cur_vts != sc4238->cur_mode->vts_def)
			sc4238_modify_fps_info(sc4238);
		dev_dbg(&client->dev, "set vblank 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = sc4238_enable_test_pattern(sc4238, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = sc4238_read_reg(sc4238->client, SC4238_FLIP_REG,
				       SC4238_REG_VALUE_08BIT,
				       &val);
		if (ctrl->val)
			val |= MIRROR_BIT_MASK;
		else
			val &= ~MIRROR_BIT_MASK;
		ret |= sc4238_write_reg(sc4238->client, SC4238_FLIP_REG,
					SC4238_REG_VALUE_08BIT,
					val);
		if (ret == 0)
			sc4238->flip = val;
		break;
	case V4L2_CID_VFLIP:
		ret = sc4238_read_reg(sc4238->client, SC4238_FLIP_REG,
				       SC4238_REG_VALUE_08BIT,
				       &val);
		if (ctrl->val)
			val |= FLIP_BIT_MASK;
		else
			val &= ~FLIP_BIT_MASK;
		ret |= sc4238_write_reg(sc4238->client, SC4238_FLIP_REG,
					SC4238_REG_VALUE_08BIT,
					val);
		if (ret == 0)
			sc4238->flip = val;
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc4238_ctrl_ops = {
	.s_ctrl = sc4238_set_ctrl,
};

static int sc4238_initialize_controls(struct sc4238 *sc4238)
{
	const struct sc4238_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	u64 dst_link_freq = 0;
	u64 dst_pixel_rate = 0;

	dev_info(&sc4238->client->dev, "%s(%d)", __func__, __LINE__);

	handler = &sc4238->ctrl_handler;
	mode = sc4238->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &sc4238->mutex;

	sc4238->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
			V4L2_CID_LINK_FREQ,
			1, 0, link_freq_menu_items);

	if (sc4238->cur_mode->bus_fmt == MEDIA_BUS_FMT_SBGGR10_1X10) {
		dst_link_freq = 0;
		dst_pixel_rate = PIXEL_RATE_WITH_360M;
	} else {
		dst_link_freq = 1;
		dst_pixel_rate = PIXEL_RATE_WITH_200M;
	}

	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	sc4238->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
			V4L2_CID_PIXEL_RATE,
			0, PIXEL_RATE_WITH_360M,
			1, dst_pixel_rate);

	__v4l2_ctrl_s_ctrl(sc4238->link_freq,
			   dst_link_freq);

	h_blank = mode->hts_def - mode->width;
	sc4238->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (sc4238->hblank)
		sc4238->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	sc4238->vblank = v4l2_ctrl_new_std(handler, &sc4238_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				SC4238_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 5;

	sc4238->exposure = v4l2_ctrl_new_std(handler, &sc4238_ctrl_ops,
				V4L2_CID_EXPOSURE, SC4238_EXPOSURE_MIN,
				exposure_max, SC4238_EXPOSURE_STEP,
				mode->exp_def);

	sc4238->anal_gain = v4l2_ctrl_new_std(handler, &sc4238_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, SC4238_GAIN_MIN,
				SC4238_GAIN_MAX, SC4238_GAIN_STEP,
				SC4238_GAIN_DEFAULT);

	sc4238->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&sc4238_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(sc4238_test_pattern_menu) - 1,
				0, 0, sc4238_test_pattern_menu);

	sc4238->h_flip = v4l2_ctrl_new_std(handler, &sc4238_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	sc4238->v_flip = v4l2_ctrl_new_std(handler, &sc4238_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	sc4238->flip = 0;
	if (handler->error) {
		ret = handler->error;
		dev_err(&sc4238->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	sc4238->subdev.ctrl_handler = handler;
	sc4238->has_init_exp = false;
	sc4238->cur_fps = mode->max_fps;
	sc4238->cur_vts = mode->vts_def;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int sc4238_check_sensor_id(struct sc4238 *sc4238,
				  struct i2c_client *client)
{
	struct device *dev = &sc4238->client->dev;
	u32 id = 0;
	int ret;

	if (sc4238->is_thunderboot) {
		dev_info(dev, "Enable thunderboot mode, skip sensor id check\n");
		return 0;
	}

	ret = sc4238_read_reg(client, SC4238_REG_CHIP_ID,
			       SC4238_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected SC%06x sensor\n", CHIP_ID);

	return 0;
}

static int sc4238_configure_regulators(struct sc4238 *sc4238)
{
	unsigned int i;

	for (i = 0; i < SC4238_NUM_SUPPLIES; i++)
		sc4238->supplies[i].supply = sc4238_supply_names[i];

	return devm_regulator_bulk_get(&sc4238->client->dev,
				       SC4238_NUM_SUPPLIES,
				       sc4238->supplies);
}

static int sc4238_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc4238 *sc4238;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	sc4238 = devm_kzalloc(dev, sizeof(*sc4238), GFP_KERNEL);
	if (!sc4238)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc4238->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc4238->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc4238->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc4238->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	sc4238->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);
	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE,
			&hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}
	sc4238->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < sc4238->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			sc4238->cur_mode = &supported_modes[i];
			break;
		}
	}
	if (sc4238->cur_mode == NULL)
		sc4238->cur_mode = &supported_modes[0];
	sc4238->client = client;

	sc4238->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc4238->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	sc4238->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sc4238->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	sc4238->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(sc4238->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	sc4238->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc4238->pinctrl)) {
		sc4238->pins_default =
			pinctrl_lookup_state(sc4238->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc4238->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		sc4238->pins_sleep =
			pinctrl_lookup_state(sc4238->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc4238->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = sc4238_configure_regulators(sc4238);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&sc4238->mutex);

	sd = &sc4238->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc4238_subdev_ops);
	ret = sc4238_initialize_controls(sc4238);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc4238_power_on(sc4238);
	if (ret)
		goto err_free_handler;

	ret = sc4238_check_sensor_id(sc4238, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc4238_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc4238->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc4238->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc4238->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sc4238->module_index, facing,
		 SC4238_NAME, dev_name(sd->dev));
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
	__sc4238_power_off(sc4238);
err_free_handler:
	v4l2_ctrl_handler_free(&sc4238->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc4238->mutex);

	return ret;
}

static int sc4238_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc4238 *sc4238 = to_sc4238(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc4238->ctrl_handler);
	mutex_destroy(&sc4238->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc4238_power_off(sc4238);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc4238_of_match[] = {
	{ .compatible = "smartsens,sc4238" },
	{},
};
MODULE_DEVICE_TABLE(of, sc4238_of_match);
#endif

static const struct i2c_device_id sc4238_match_id[] = {
	{ "smartsens,sc4238", 0 },
	{ },
};

static struct i2c_driver sc4238_i2c_driver = {
	.driver = {
		.name = SC4238_NAME,
		.pm = &sc4238_pm_ops,
		.of_match_table = of_match_ptr(sc4238_of_match),
	},
	.probe		= &sc4238_probe,
	.remove		= &sc4238_remove,
	.id_table	= sc4238_match_id,
};

#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT
module_i2c_driver(sc4238_i2c_driver);
#else
static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc4238_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc4238_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);
#endif

MODULE_DESCRIPTION("Smartsens sc4238 sensor driver");
MODULE_LICENSE("GPL v2");
