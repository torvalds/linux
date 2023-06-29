// SPDX-License-Identifier: GPL-2.0
/*
 * sc4210 driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 */

//#define DEBUG
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <linux/rk-preisp.h>
#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-async.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include <stdarg.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/printk.h>

#include <linux/rk-camera-module.h>
#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define SC4210_LINK_FREQ_2LANE_LINEAR	303000000 // 607.5Mbps
#define SC4210_LINK_FREQ_2LANE_HDR2		540000000 // 1080Mbps
#define SC4210_LINK_FREQ_4LANE_LINEAR	202500000 // 405Mbps
#define SC4210_LINK_FREQ_4LANE_HDR2		364500000 // 729Mbps

#define SC4210_PIXEL_RATES_2LANE_LINEAR	(SC4210_LINK_FREQ_2LANE_LINEAR / 10 * 2 * 2)
#define SC4210_PIXEL_RATES_2LANE_HDR2	(SC4210_LINK_FREQ_2LANE_HDR2 / 10 * 2 * 2)
#define SC4210_PIXEL_RATES_4LANE_LINEAR	(SC4210_LINK_FREQ_4LANE_LINEAR / 10 * 4 * 2)
#define SC4210_PIXEL_RATES_4LANE_HDR2	(SC4210_LINK_FREQ_4LANE_HDR2 / 10 * 4 * 2)
#define SC4210_MAX_PIXEL_RATE			(SC4210_LINK_FREQ_4LANE_HDR2 / 10 * 4 * 2)

#define SC4210_XVCLK_FREQ			27000000

#define SC4210_CHIP_ID				0x4210
#define SC4210_REG_CHIP_ID			0x3107

#define SC4210_REG_CTRL_MODE		0x0100
#define SC4210_MODE_SW_STANDBY		0x0
#define SC4210_MODE_STREAMING		BIT(0)

#define SC4210_REG_EXPOSURE_H		0x3e00
#define SC4210_REG_EXPOSURE_M		0x3e01
#define SC4210_REG_EXPOSURE_L		0x3e02
#define	SC4210_EXPOSURE_MIN			2
#define	SC4210_EXPOSURE_STEP		1

#define SC4210_REG_DIG_GAIN			0x3e06
#define SC4210_REG_DIG_FINE_GAIN	0x3e07
#define SC4210_REG_ANA_GAIN			0x3e08
#define SC4210_REG_ANA_FINE_GAIN	0x3e09
#define SC4210_GAIN_MIN				1000
#define SC4210_GAIN_MAX				(43.65 * 32 * 1000)
#define SC4210_GAIN_STEP			1
#define SC4210_GAIN_DEFAULT			1000

#define SC4210_REG_VTS_H			0x320e
#define SC4210_REG_VTS_L			0x320f
#define SC4210_VTS_MAX				0x7fff

#define SC4210_SOFTWARE_RESET_REG	0x0103

// short frame exposure
#define SC4210_REG_SHORT_EXPOSURE_H	0x3e22
#define SC4210_REG_SHORT_EXPOSURE_M	0x3e04
#define SC4210_REG_SHORT_EXPOSURE_L	0x3e05
#define SC4210_REG_MAX_SHORT_EXP_H	0x3e23
#define SC4210_REG_MAX_SHORT_EXP_L	0x3e24
#define SC4210_HDR_EXPOSURE_MIN		5
#define SC4210_HDR_EXPOSURE_STEP	4
#define SC4210_MAX_SHORT_EXPOSURE	608

// short frame gain
#define SC4210_REG_SDIG_GAIN		0x3e10
#define SC4210_REG_SDIG_FINE_GAIN	0x3e11
#define SC4210_REG_SANA_GAIN		0x3e12
#define SC4210_REG_SANA_FINE_GAIN	0x3e13

//group hold
#define SC4210_GROUP_UPDATE_ADDRESS	0x3800
#define SC4210_GROUP_UPDATE_START_DATA	0x00
#define SC4210_GROUP_UPDATE_END_DATA	0x10
#define SC4210_GROUP_UPDATE_LAUNCH	0x40

#define SC4210_FLIP_MIRROR_REG		0x3221
#define SC4210_FLIP_MASK			0x60
#define SC4210_MIRROR_MASK			0x06

#define REG_NULL			0xFFFF

#define SC4210_REG_VALUE_08BIT		1
#define SC4210_REG_VALUE_16BIT		2
#define SC4210_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define SC4210_NAME			"sc4210"

#define SC4210_FETCH_EXP_H(VAL)		(((VAL) >> 12) & 0xF)
#define SC4210_FETCH_EXP_M(VAL)		(((VAL) >> 4) & 0xFF)
#define SC4210_FETCH_EXP_L(VAL)		(((VAL) & 0xF) << 4)

static const char * const sc4210_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define sc4210_NUM_SUPPLIES ARRAY_SIZE(sc4210_supply_names)
struct regval {
	u16 addr;
	u8 val;
};

struct sc4210_mode {
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
	u32 vc[PAD_MAX];
};

struct sc4210 {
		struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[sc4210_NUM_SUPPLIES];
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
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct sc4210_mode *support_modes;
	const struct sc4210_mode *cur_mode;
	struct v4l2_fract	cur_fps;
	u32			support_modes_num;
	unsigned int		lane_num;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	u32			cur_vts;
};

#define to_sc4210(sd) container_of(sd, struct sc4210, subdev)

/*
 * sc4210 27Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 405Mbps, 4lane
 */
static const struct regval sc4210_linear_10_30fps_2560x1440_4lane_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x3001, 0x07},
	{0x3002, 0xc0},
	{0x300a, 0x2c},
	{0x300f, 0x00},
	{0x3018, 0x73},
	{0x301f, 0x3b},
	{0x3031, 0x0a},
	{0x3038, 0x22},
	{0x320c, 0x05},
	{0x320d, 0x46},
	{0x3220, 0x10},
	{0x3225, 0x01},
	{0x3227, 0x03},
	{0x3229, 0x08},
	{0x3231, 0x01},
	{0x3241, 0x02},
	{0x3243, 0x03},
	{0x3249, 0x17},
	{0x3251, 0x08},
	{0x3253, 0x08},
	{0x325e, 0x00},
	{0x325f, 0x00},
	{0x3273, 0x01},
	{0x3301, 0x28},
	{0x3302, 0x18},
	{0x3000, 0x00},
	{0x3304, 0x20},
	{0x3305, 0x00},
	{0x3306, 0x74},
	{0x3308, 0x10},
	{0x3309, 0x40},
	{0x330a, 0x00},
	{0x330b, 0xe8},
	{0x330e, 0x18},
	{0x3312, 0x02},
	{0x3314, 0x84},
	{0x331e, 0x19},
	{0x331f, 0x39},
	{0x3320, 0x05},
	{0x3338, 0x10},
	{0x334c, 0x10},
	{0x335d, 0x20},
	{0x3366, 0x92},
	{0x3367, 0x08},
	{0x3368, 0x05},
	{0x3369, 0xdc},
	{0x336a, 0x0b},
	{0x336b, 0xb8},
	{0x336c, 0xc2},
	{0x337a, 0x08},
	{0x337b, 0x10},
	{0x337e, 0x40},
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
	{0x3622, 0xff},
	{0x3624, 0x07},
	{0x3625, 0x02},
	{0x3630, 0xc4},
	{0x3631, 0x80},
	{0x3632, 0x88},
	{0x3633, 0x22},
	{0x3634, 0x64},
	{0x3635, 0x40},
	{0x3636, 0x20},
	{0x3638, 0x28},
	{0x363b, 0x03},
	{0x363c, 0x08},
	{0x363d, 0x08},
	{0x366e, 0x04},
	{0x3670, 0x48},
	{0x3671, 0xff},
	{0x3672, 0x1f},
	{0x3673, 0x1f},
	{0x367a, 0x40},
	{0x367b, 0x40},
	{0x3690, 0x42},
	{0x3691, 0x44},
	{0x3692, 0x44},
	{0x3699, 0x80},
	{0x369a, 0x9f},
	{0x369b, 0x9f},
	{0x369c, 0x40},
	{0x369d, 0x40},
	{0x36a2, 0x40},
	{0x36a3, 0x40},
	{0x36cc, 0x2c},
	{0x36cd, 0x30},
	{0x36ce, 0x30},
	{0x36d0, 0x20},
	{0x36d1, 0x40},
	{0x36d2, 0x40},
	{0x36ea, 0x36},
	{0x36eb, 0x16},
	{0x36ec, 0x03},
	{0x36ed, 0x0c},
	{0x36fa, 0x37},
	{0x36fb, 0x14},
	{0x36fc, 0x00},
	{0x36fd, 0x2c},
	{0x3817, 0x20},
	{0x3905, 0xd8},
	{0x3908, 0x11},
	{0x391b, 0x80},
	{0x391c, 0x0f},
	{0x391d, 0x21},
	{0x3933, 0x27},
	{0x3934, 0xf5},
	{0x3935, 0x80},
	{0x3936, 0x1f},
	{0x3940, 0x6e},
	{0x3942, 0x07},
	{0x3943, 0xf6},
	{0x3980, 0x00},
	{0x3981, 0x12},
	{0x3982, 0x00},
	{0x3983, 0x07},
	{0x3984, 0x00},
	{0x3985, 0x03},
	{0x3986, 0x00},
	{0x3987, 0x04},
	{0x3988, 0x00},
	{0x3989, 0x01},
	{0x398a, 0x00},
	{0x398b, 0x03},
	{0x398c, 0x00},
	{0x398d, 0x06},
	{0x398e, 0x00},
	{0x398f, 0x0d},
	{0x3990, 0x00},
	{0x3991, 0x12},
	{0x3992, 0x00},
	{0x3993, 0x09},
	{0x3994, 0x00},
	{0x3995, 0x02},
	{0x3996, 0x00},
	{0x3997, 0x04},
	{0x3998, 0x00},
	{0x3999, 0x0a},
	{0x399a, 0x00},
	{0x399b, 0x10},
	{0x399c, 0x00},
	{0x399d, 0x16},
	{0x399e, 0x00},
	{0x399f, 0x1f},
	{0x39a0, 0x02},
	{0x39a1, 0x04},
	{0x39a2, 0x10},
	{0x39a3, 0x13},
	{0x39a4, 0x97},
	{0x39a5, 0x43},
	{0x39a6, 0x20},
	{0x39a7, 0x05},
	{0x39a8, 0x23},
	{0x39a9, 0x43},
	{0x39aa, 0x85},
	{0x39ab, 0x95},
	{0x39ac, 0x24},
	{0x39ad, 0x18},
	{0x39ae, 0x11},
	{0x39af, 0x04},
	{0x39b9, 0x00},
	{0x39ba, 0x19},
	{0x39bb, 0xba},
	{0x39bc, 0x00},
	{0x39bd, 0x05},
	{0x39be, 0x99},
	{0x39bf, 0x00},
	{0x39c0, 0x00},
	{0x39c5, 0x71},
	{0x3e00, 0x00},
	{0x3e01, 0xbb},
	{0x3e02, 0x40},
	{0x3e03, 0x0b},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3e0e, 0x6a},
	{0x3e26, 0x40},
	{0x4407, 0xb0},
	{0x4418, 0x0b},
	{0x4501, 0xb4},
	{0x4509, 0x20},
	{0x4603, 0x00},
	{0x4800, 0x24},
	{0x4837, 0x28},
	{0x5000, 0x0e},
	{0x550f, 0x20},
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
	{0x36e9, 0x27},
	{0x36f9, 0x20},
	//{0x0100, 0x01},
	{REG_NULL, 0x0},
};

/*
 * sc4210 27Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 729Mbps, 4lane
 */
static const struct regval sc4210_hdr_10_30fps_2560x1440_4lane_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x3001, 0x07},
	{0x3002, 0xc0},
	{0x300a, 0x2c},
	{0x300f, 0x00},
	{0x3018, 0x73},
	{0x301f, 0x3e},
	{0x3031, 0x0a},
	{0x3038, 0x22},
	{0x3207, 0xa7},
	{0x320c, 0x05},
	{0x320d, 0x58},
	{0x320e, 0x0b},
	{0x320f, 0x90},
	{0x3213, 0x04},
	{0x3220, 0x50},
	{0x3225, 0x01},
	{0x3227, 0x03},
	{0x3229, 0x08},
	{0x3231, 0x01},
	{0x3241, 0x02},
	{0x3243, 0x03},
	{0x3249, 0x17},
	{0x3250, 0x3f},
	{0x3251, 0x08},
	{0x3253, 0x10},
	{0x325e, 0x00},
	{0x3000, 0x00},
	{0x325f, 0x00},
	{0x3273, 0x01},
	{0x3301, 0x15},
	{0x3302, 0x18},
	{0x3304, 0x20},
	{0x3305, 0x00},
	{0x3306, 0x78},
	{0x3308, 0x10},
	{0x3309, 0x50},
	{0x330a, 0x00},
	{0x330b, 0xe8},
	{0x330e, 0x20},
	{0x3312, 0x02},
	{0x3314, 0x84},
	{0x331e, 0x19},
	{0x331f, 0x49},
	{0x3320, 0x05},
	{0x3338, 0x10},
	{0x334c, 0x10},
	{0x335d, 0x20},
	{0x335e, 0x02},
	{0x335f, 0x04},
	{0x3360, 0x20},
	{0x3362, 0x72},
	{0x3364, 0x1e},
	{0x3366, 0x92},
	{0x3367, 0x08},
	{0x3368, 0x0a},
	{0x3369, 0xd4},
	{0x336a, 0x15},
	{0x336b, 0xa8},
	{0x336c, 0xc2},
	{0x337a, 0x08},
	{0x337b, 0x10},
	{0x337c, 0x06},
	{0x337d, 0x0a},
	{0x337e, 0x40},
	{0x3390, 0x08},
	{0x3391, 0x18},
	{0x3392, 0x38},
	{0x3393, 0x13},
	{0x3394, 0x24},
	{0x3395, 0x24},
	{0x3396, 0x08},
	{0x3397, 0x18},
	{0x3398, 0x38},
	{0x3399, 0x11},
	{0x339a, 0x14},
	{0x339b, 0x24},
	{0x339c, 0x24},
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
	{0x3622, 0xff},
	{0x3624, 0x07},
	{0x3625, 0x0a},
	{0x3630, 0xc4},
	{0x3631, 0x80},
	{0x3632, 0x88},
	{0x3633, 0x42},
	{0x3634, 0x64},
	{0x3635, 0x20},
	{0x3636, 0x20},
	{0x3638, 0x28},
	{0x363b, 0x03},
	{0x363c, 0x06},
	{0x363d, 0x06},
	{0x366e, 0x04},
	{0x3670, 0x4a},
	{0x3671, 0xff},
	{0x3672, 0x9f},
	{0x3673, 0x9f},
	{0x3674, 0xc4},
	{0x3675, 0xc4},
	{0x3676, 0xb8},
	{0x367a, 0x40},
	{0x367b, 0x48},
	{0x367c, 0x40},
	{0x367d, 0x48},
	{0x3690, 0x43},
	{0x3691, 0x55},
	{0x3692, 0x66},
	{0x3699, 0x8c},
	{0x369a, 0x96},
	{0x369b, 0x9f},
	{0x369c, 0x40},
	{0x369d, 0x48},
	{0x36a2, 0x40},
	{0x36a3, 0x48},
	{0x36cc, 0x2c},
	{0x36cd, 0x30},
	{0x36ce, 0x30},
	{0x36d0, 0x20},
	{0x36d1, 0x40},
	{0x36d2, 0x40},
	{0x36ea, 0x37},
	{0x36eb, 0x06},
	{0x36ec, 0x03},
	{0x36ed, 0x0c},
	{0x36fa, 0x37},
	{0x36fb, 0x04},
	{0x36fc, 0x00},
	{0x36fd, 0x2c},
	{0x3817, 0x20},
	{0x3905, 0x98},
	{0x3908, 0x11},
	{0x391b, 0x80},
	{0x391c, 0x0f},
	{0x391d, 0x21},
	{0x3933, 0x1f},
	{0x3934, 0xff},
	{0x3935, 0x80},
	{0x3936, 0x1f},
	{0x393e, 0x01},
	{0x3940, 0x60},
	{0x3942, 0x04},
	{0x3943, 0xd0},
	{0x3980, 0x00},
	{0x3981, 0x30},
	{0x3982, 0x00},
	{0x3983, 0x2c},
	{0x3984, 0x00},
	{0x3985, 0x15},
	{0x3986, 0x00},
	{0x3987, 0x10},
	{0x3988, 0x00},
	{0x3989, 0x30},
	{0x398a, 0x00},
	{0x398b, 0x28},
	{0x398c, 0x00},
	{0x398d, 0x30},
	{0x398e, 0x00},
	{0x398f, 0x70},
	{0x3990, 0x0a},
	{0x3991, 0x00},
	{0x3992, 0x00},
	{0x3993, 0x60},
	{0x3994, 0x00},
	{0x3995, 0x30},
	{0x3996, 0x00},
	{0x3997, 0x10},
	{0x3998, 0x00},
	{0x3999, 0x1c},
	{0x399a, 0x00},
	{0x399b, 0x48},
	{0x399c, 0x00},
	{0x399d, 0x90},
	{0x399e, 0x00},
	{0x399f, 0xc0},
	{0x39a0, 0x14},
	{0x39a1, 0x28},
	{0x39a2, 0x48},
	{0x39a3, 0x70},
	{0x39a4, 0x18},
	{0x39a5, 0x04},
	{0x39a6, 0x08},
	{0x39a7, 0x04},
	{0x39a8, 0x01},
	{0x39a9, 0x14},
	{0x39aa, 0x28},
	{0x39ab, 0x50},
	{0x39ac, 0x30},
	{0x39ad, 0x20},
	{0x39ae, 0x10},
	{0x39af, 0x08},
	{0x39b9, 0x00},
	{0x39ba, 0x00},
	{0x39bb, 0x00},
	{0x39bc, 0x00},
	{0x39bd, 0x58},
	{0x39be, 0xc0},
	{0x39bf, 0x00},
	{0x39c0, 0x00},
	{0x39c5, 0x41},
	{0x3c09, 0x4a},
	{0x3c10, 0x04},
	{0x3c11, 0x20},
	{0x3c12, 0x04},
	{0x3c13, 0x20},
	{0x3c14, 0x0f},
	{0x3e00, 0x01},
	{0x3e01, 0x5a},
	{0x3e02, 0x00},
	{0x3e03, 0x0b},
	{0x3e04, 0x15},
	{0x3e05, 0xa0},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3e0e, 0x6a},
	{0x3e10, 0x00},
	{0x3e11, 0x80},
	{0x3e12, 0x03},
	{0x3e13, 0x40},
	{0x3e23, 0x00},
	{0x3e24, 0xbc},
	{0x3e26, 0x40},
	{0x4401, 0x0b},
	{0x4407, 0xb0},
	{0x4418, 0x16},
	{0x4501, 0xa4},
	{0x4509, 0x08},
	{0x4603, 0x00},
	{0x4800, 0x24},
	{0x4816, 0x11},
	{0x4819, 0x40},
	{0x4829, 0x01},
	{0x4837, 0x16},
	{0x5000, 0x0e},
	{0x550f, 0x20},
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
	{0x36e9, 0x27},
	{0x36f9, 0x20},
	//{0x0100, 0x01},
	{REG_NULL, 0x00},
};

/*
 * Xclk 27Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 607.5Mbps, 2lane
 */
static const struct regval sc4210_liner_10_30fps_2560x1440_2lane_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0xd1},
	{0x36f9, 0xd1},
	{0x3001, 0x07},
	{0x3002, 0xc0},
	{0x300a, 0x2c},
	{0x300f, 0x00},
	{0x3018, 0x33},
	{0x301f, 0x21},
	{0x3031, 0x0a},
	{0x3038, 0x22},
	{0x320c, 0x05},
	{0x320d, 0x46},
	{0x3220, 0x10},
	{0x3225, 0x01},
	{0x3227, 0x03},
	{0x3229, 0x08},
	{0x3231, 0x01},
	{0x3241, 0x02},
	{0x3243, 0x03},
	{0x3249, 0x17},
	{0x3251, 0x08},
	{0x3253, 0x08},
	{0x325e, 0x00},
	{0x325f, 0x00},
	{0x3273, 0x01},
	{0x3301, 0x28},
	{0x3302, 0x18},
	{0x3304, 0x20},
	{0x3000, 0x00},
	{0x3305, 0x00},
	{0x3306, 0x74},
	{0x3308, 0x10},
	{0x3309, 0x40},
	{0x330a, 0x00},
	{0x330b, 0xe8},
	{0x330e, 0x18},
	{0x3312, 0x02},
	{0x3314, 0x84},
	{0x331e, 0x19},
	{0x331f, 0x39},
	{0x3320, 0x05},
	{0x3338, 0x10},
	{0x334c, 0x10},
	{0x335d, 0x20},
	{0x3366, 0x92},
	{0x3367, 0x08},
	{0x3368, 0x05},
	{0x3369, 0xdc},
	{0x336a, 0x0b},
	{0x336b, 0xb8},
	{0x336c, 0xc2},
	{0x337a, 0x08},
	{0x337b, 0x10},
	{0x337e, 0x40},
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
	{0x3622, 0xff},
	{0x3624, 0x07},
	{0x3625, 0x02},
	{0x3630, 0xc4},
	{0x3631, 0x80},
	{0x3632, 0x88},
	{0x3633, 0x22},
	{0x3634, 0x64},
	{0x3635, 0x20},
	{0x3636, 0x20},
	{0x3638, 0x28},
	{0x363b, 0x03},
	{0x363c, 0x06},
	{0x363d, 0x06},
	{0x366e, 0x04},
	{0x3670, 0x48},
	{0x3671, 0xff},
	{0x3672, 0x1f},
	{0x3673, 0x1f},
	{0x367a, 0x40},
	{0x367b, 0x40},
	{0x3690, 0x42},
	{0x3691, 0x44},
	{0x3692, 0x44},
	{0x3699, 0x80},
	{0x369a, 0x9f},
	{0x369b, 0x9f},
	{0x369c, 0x40},
	{0x369d, 0x40},
	{0x36a2, 0x40},
	{0x36a3, 0x40},
	{0x36cc, 0x2c},
	{0x36cd, 0x30},
	{0x36ce, 0x30},
	{0x36d0, 0x20},
	{0x36d1, 0x40},
	{0x36d2, 0x40},
	{0x36ea, 0xa8},
	{0x36eb, 0x04},
	{0x36ec, 0x03},
	{0x36ed, 0x0c},
	{0x36fa, 0x78},
	{0x36fb, 0x14},
	{0x36fc, 0x00},
	{0x36fd, 0x2c},
	{0x3817, 0x20},
	{0x3905, 0xd8},
	{0x3908, 0x11},
	{0x391b, 0x80},
	{0x391c, 0x0f},
	{0x391d, 0x21},
	{0x3933, 0x24},
	{0x3934, 0xb0},
	{0x3935, 0x80},
	{0x3936, 0x1f},
	{0x3940, 0x68},
	{0x3942, 0x04},
	{0x3943, 0xc0},
	{0x3980, 0x00},
	{0x3981, 0x50},
	{0x3982, 0x00},
	{0x3983, 0x40},
	{0x3984, 0x00},
	{0x3985, 0x20},
	{0x3986, 0x00},
	{0x3987, 0x10},
	{0x3988, 0x00},
	{0x3989, 0x20},
	{0x398a, 0x00},
	{0x398b, 0x30},
	{0x398c, 0x00},
	{0x398d, 0x50},
	{0x398e, 0x00},
	{0x398f, 0x60},
	{0x3990, 0x00},
	{0x3991, 0x70},
	{0x3992, 0x00},
	{0x3993, 0x36},
	{0x3994, 0x00},
	{0x3995, 0x20},
	{0x3996, 0x00},
	{0x3997, 0x14},
	{0x3998, 0x00},
	{0x3999, 0x20},
	{0x399a, 0x00},
	{0x399b, 0x50},
	{0x399c, 0x00},
	{0x399d, 0x90},
	{0x399e, 0x00},
	{0x399f, 0xf0},
	{0x39a0, 0x08},
	{0x39a1, 0x10},
	{0x39a2, 0x20},
	{0x39a3, 0x40},
	{0x39a4, 0x20},
	{0x39a5, 0x10},
	{0x39a6, 0x08},
	{0x39a7, 0x04},
	{0x39a8, 0x18},
	{0x39a9, 0x30},
	{0x39aa, 0x40},
	{0x39ab, 0x60},
	{0x39ac, 0x38},
	{0x39ad, 0x20},
	{0x39ae, 0x10},
	{0x39af, 0x08},
	{0x39b9, 0x00},
	{0x39ba, 0xa0},
	{0x39bb, 0x80},
	{0x39bc, 0x00},
	{0x39bd, 0x44},
	{0x39be, 0x00},
	{0x39bf, 0x00},
	{0x39c0, 0x00},
	{0x39c5, 0x41},
	{0x3e00, 0x00},
	{0x3e01, 0xbb},
	{0x3e02, 0x40},
	{0x3e03, 0x0b},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3e0e, 0x6a},
	{0x3e26, 0x40},
	{0x4401, 0x0b},
	{0x4407, 0xb0},
	{0x4418, 0x0b},
	{0x4501, 0xb4},
	{0x4509, 0x10},
	{0x4603, 0x00},
	{0x4837, 0x15},
	{0x5000, 0x0e},
	{0x550f, 0x20},
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
	{0x36e9, 0x51},
	{0x36f9, 0x51},
	//{0x0100, 0x01},
	{REG_NULL, 0x00},
};


/*
 * Xclk 27Mhz
 * max_framerate 25fps
 * mipi_datarate per lane 1080Mbps, 2lane
 */
static const struct regval sc4210_hdr_10_25fps_2560x1440_2lane_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x3001, 0x07},
	{0x3002, 0xc0},
	{0x300a, 0x2c},
	{0x300f, 0x00},
	{0x3018, 0x33},
	{0x3019, 0x0c},
	{0x301f, 0x45},
	{0x3031, 0x0a},
	{0x3038, 0x22},
	{0x3207, 0xa7},
	{0x320c, 0x06},
	{0x320d, 0x68},
	{0x320e, 0x0b},
	{0x320f, 0x90},
	{0x3213, 0x04},
	{0x3220, 0x50},
	{0x3225, 0x01},
	{0x3227, 0x03},
	{0x3229, 0x08},
	{0x3231, 0x01},
	{0x3241, 0x02},
	{0x3243, 0x03},
	{0x3249, 0x17},
	{0x3250, 0x3f},
	{0x3251, 0x08},
	{0x3000, 0x00},
	{0x3253, 0x10},
	{0x325e, 0x00},
	{0x325f, 0x00},
	{0x3273, 0x01},
	{0x3301, 0x15},
	{0x3302, 0x18},
	{0x3304, 0x20},
	{0x3305, 0x00},
	{0x3306, 0x78},
	{0x3308, 0x10},
	{0x3309, 0x50},
	{0x330a, 0x00},
	{0x330b, 0xe8},
	{0x330e, 0x20},
	{0x3312, 0x02},
	{0x3314, 0x84},
	{0x331e, 0x19},
	{0x331f, 0x49},
	{0x3320, 0x05},
	{0x3338, 0x10},
	{0x334c, 0x10},
	{0x335d, 0x20},
	{0x335e, 0x02},
	{0x335f, 0x04},
	{0x3360, 0x20},
	{0x3362, 0x72},
	{0x3364, 0x1e},
	{0x3366, 0x92},
	{0x3367, 0x08},
	{0x3368, 0x0a},
	{0x3369, 0xd4},
	{0x336a, 0x15},
	{0x336b, 0xa8},
	{0x336c, 0xc2},
	{0x337a, 0x08},
	{0x337b, 0x10},
	{0x337c, 0x06},
	{0x337d, 0x0a},
	{0x337e, 0x40},
	{0x3390, 0x08},
	{0x3391, 0x18},
	{0x3392, 0x38},
	{0x3393, 0x13},
	{0x3394, 0x24},
	{0x3395, 0x24},
	{0x3396, 0x08},
	{0x3397, 0x18},
	{0x3398, 0x38},
	{0x3399, 0x11},
	{0x339a, 0x14},
	{0x339b, 0x24},
	{0x339c, 0x24},
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
	{0x3622, 0xff},
	{0x3624, 0x07},
	{0x3625, 0x0a},
	{0x3630, 0xc4},
	{0x3631, 0x80},
	{0x3632, 0x88},
	{0x3633, 0x42},
	{0x3634, 0x64},
	{0x3635, 0x20},
	{0x3636, 0x20},
	{0x3638, 0x28},
	{0x363b, 0x03},
	{0x363c, 0x06},
	{0x363d, 0x06},
	{0x366e, 0x04},
	{0x3670, 0x4a},
	{0x3671, 0xff},
	{0x3672, 0x9f},
	{0x3673, 0x9f},
	{0x3674, 0xc4},
	{0x3675, 0xc4},
	{0x3676, 0xb8},
	{0x367a, 0x40},
	{0x367b, 0x48},
	{0x367c, 0x40},
	{0x367d, 0x48},
	{0x3690, 0x43},
	{0x3691, 0x55},
	{0x3692, 0x66},
	{0x3699, 0x8c},
	{0x369a, 0x96},
	{0x369b, 0x9f},
	{0x369c, 0x40},
	{0x369d, 0x48},
	{0x36a2, 0x40},
	{0x36a3, 0x48},
	{0x36cc, 0x2c},
	{0x36cd, 0x30},
	{0x36ce, 0x30},
	{0x36d0, 0x20},
	{0x36d1, 0x40},
	{0x36d2, 0x40},
	{0x36ea, 0x34},
	{0x36eb, 0x06},
	{0x36ec, 0x03},
	{0x36ed, 0x2c},
	{0x36fa, 0x37},
	{0x36fb, 0x04},
	{0x36fc, 0x00},
	{0x36fd, 0x2c},
	{0x3817, 0x20},
	{0x3905, 0x98},
	{0x3908, 0x11},
	{0x391b, 0x80},
	{0x391c, 0x0f},
	{0x391d, 0x21},
	{0x3933, 0x1f},
	{0x3934, 0xff},
	{0x3935, 0x80},
	{0x3936, 0x1f},
	{0x393e, 0x01},
	{0x3940, 0x60},
	{0x3942, 0x04},
	{0x3943, 0xd0},
	{0x3980, 0x00},
	{0x3981, 0x30},
	{0x3982, 0x00},
	{0x3983, 0x2c},
	{0x3984, 0x00},
	{0x3985, 0x15},
	{0x3986, 0x00},
	{0x3987, 0x10},
	{0x3988, 0x00},
	{0x3989, 0x30},
	{0x398a, 0x00},
	{0x398b, 0x28},
	{0x398c, 0x00},
	{0x398d, 0x30},
	{0x398e, 0x00},
	{0x398f, 0x70},
	{0x3990, 0x0a},
	{0x3991, 0x00},
	{0x3992, 0x00},
	{0x3993, 0x60},
	{0x3994, 0x00},
	{0x3995, 0x30},
	{0x3996, 0x00},
	{0x3997, 0x10},
	{0x3998, 0x00},
	{0x3999, 0x1c},
	{0x399a, 0x00},
	{0x399b, 0x48},
	{0x399c, 0x00},
	{0x399d, 0x90},
	{0x399e, 0x00},
	{0x399f, 0xc0},
	{0x39a0, 0x14},
	{0x39a1, 0x28},
	{0x39a2, 0x48},
	{0x39a3, 0x70},
	{0x39a4, 0x18},
	{0x39a5, 0x04},
	{0x39a6, 0x08},
	{0x39a7, 0x04},
	{0x39a8, 0x01},
	{0x39a9, 0x14},
	{0x39aa, 0x28},
	{0x39ab, 0x50},
	{0x39ac, 0x30},
	{0x39ad, 0x20},
	{0x39ae, 0x10},
	{0x39af, 0x08},
	{0x39b9, 0x00},
	{0x39ba, 0x00},
	{0x39bb, 0x00},
	{0x39bc, 0x00},
	{0x39bd, 0x58},
	{0x39be, 0xc0},
	{0x39bf, 0x00},
	{0x39c0, 0x00},
	{0x39c5, 0x41},
	{0x3c09, 0x4a},
	{0x3c10, 0x07},
	{0x3c11, 0x20},
	{0x3c12, 0x07},
	{0x3c13, 0x20},
	{0x3c14, 0x0f},
	{0x3e00, 0x01},
	{0x3e01, 0x5a},
	{0x3e02, 0x00},
	{0x3e03, 0x0b},
	{0x3e04, 0x15},
	{0x3e05, 0xa0},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3e0e, 0x6a},
	{0x3e10, 0x00},
	{0x3e11, 0x80},
	{0x3e12, 0x03},
	{0x3e13, 0x40},
	{0x3e23, 0x00},
	{0x3e24, 0xbc},
	{0x3e26, 0x40},
	{0x4401, 0x0b},
	{0x4407, 0xb0},
	{0x4418, 0x16},
	{0x4501, 0xa4},
	{0x4509, 0x08},
	{0x4603, 0x00},
	{0x4800, 0x24},
	{0x4816, 0x11},
	{0x4819, 0x40},
	{0x4829, 0x01},
	{0x4837, 0x0f},
	{0x5000, 0x0e},
	{0x550f, 0x20},
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
	{0x36e9, 0x44},
	{0x36f9, 0x20},
	//{0x0100, 0x01},
	{REG_NULL, 0x00},
};

static const struct sc4210_mode supported_modes_2lane[] = {
	{
		.width = 2560,
		.height = 1440,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0100,
		.hts_def = 0x0546,
		.vts_def = 0x05dc,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc4210_liner_10_30fps_2560x1440_2lane_regs,
		.mipi_freq_idx = 0,
		.bpp = 10,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 2560,
		.height = 1440,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.exp_def = 0x015a,
		.hts_def = 0x0668,
		.vts_def = 0x0b90,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc4210_hdr_10_25fps_2560x1440_2lane_regs,
		.mipi_freq_idx = 1,
		.bpp = 10,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
};

static const struct sc4210_mode supported_modes_4lane[] = {
	{
		.width = 2560,
		.height = 1440,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0100,
		.hts_def = 0x0546,
		.vts_def = 0x05dc,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc4210_linear_10_30fps_2560x1440_4lane_regs,
		.mipi_freq_idx = 2,
		.bpp = 10,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 2560,
		.height = 1440,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x05a1,
		.hts_def = 0x0558,
		.vts_def = 0x0b90,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc4210_hdr_10_30fps_2560x1440_4lane_regs,
		.mipi_freq_idx = 3,
		.bpp = 10,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
};

static const s64 link_freq_items[] = {
	SC4210_LINK_FREQ_2LANE_LINEAR,
	SC4210_LINK_FREQ_2LANE_HDR2,
	SC4210_LINK_FREQ_4LANE_LINEAR,
	SC4210_LINK_FREQ_4LANE_HDR2,
};

/* Write registers up to 4 at a time */
static int sc4210_write_reg(struct i2c_client *client, u16 reg,
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

static int sc4210_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = sc4210_write_reg(client, regs[i].addr,
					SC4210_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int sc4210_read_reg(struct i2c_client *client,
			    u16 reg, unsigned int len, u32 *val)
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

static int sc4210_get_reso_dist(const struct sc4210_mode *mode,
		struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc4210_mode *
sc4210_find_best_fit(struct sc4210 *sc4210, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < sc4210->support_modes_num; i++) {
		dist = sc4210_get_reso_dist(&sc4210->support_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &sc4210->support_modes[cur_best_fit];
}

static int sc4210_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc4210 *sc4210 = to_sc4210(sd);
	const struct sc4210_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;

	mutex_lock(&sc4210->mutex);

	mode = sc4210_find_best_fit(sc4210, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc4210->mutex);
		return -ENOTTY;
#endif
	} else {
		sc4210->cur_mode = mode;

		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc4210->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc4210->vblank, vblank_def,
					 SC4210_VTS_MAX - mode->height,
					 1, vblank_def);

		__v4l2_ctrl_s_ctrl(sc4210->link_freq, mode->mipi_freq_idx);
		pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] /
			     mode->bpp * 2 * sc4210->lane_num;
		__v4l2_ctrl_s_ctrl_int64(sc4210->pixel_rate, pixel_rate);
		sc4210->cur_fps = mode->max_fps;
	}

	mutex_unlock(&sc4210->mutex);

	return 0;
}

static int sc4210_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc4210 *sc4210 = to_sc4210(sd);
	const struct sc4210_mode *mode = sc4210->cur_mode;

	mutex_lock(&sc4210->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc4210->mutex);
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
	mutex_unlock(&sc4210->mutex);

	return 0;
}

static int sc4210_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct sc4210 *sc4210 = to_sc4210(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = sc4210->cur_mode->bus_fmt;

	return 0;
}

static int sc4210_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	struct sc4210 *sc4210 = to_sc4210(sd);

	if (fse->index >= sc4210->support_modes_num)
		return -EINVAL;

	if (fse->code != sc4210->support_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = sc4210->support_modes[fse->index].width;
	fse->max_width  = sc4210->support_modes[fse->index].width;
	fse->max_height = sc4210->support_modes[fse->index].height;
	fse->min_height = sc4210->support_modes[fse->index].height;

	return 0;
}

static int sc4210_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct sc4210 *sc4210 = to_sc4210(sd);
	const struct sc4210_mode *mode = sc4210->cur_mode;

	if (sc4210->streaming)
		fi->interval = sc4210->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

static int sc4210_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				 struct v4l2_mbus_config *config)
{
	struct sc4210 *sc4210 = to_sc4210(sd);
	const struct sc4210_mode *mode = sc4210->cur_mode;
	u32 val = 1 << (sc4210->lane_num - 1) |
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

static void sc4210_get_module_inf(struct sc4210 *sc4210,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, SC4210_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, sc4210->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, sc4210->len_name, sizeof(inf->base.lens));
}

static void sc4210_get_gain_reg(u32 total_gain, u32 *again, u32 *again_fine,
					u32 *dgain, u32 *dgain_fine)
{
	u32 dgain_total = 0;

	if (total_gain < SC4210_GAIN_MIN)
		total_gain = SC4210_GAIN_MIN;
	else if (total_gain > SC4210_GAIN_MAX)
		total_gain = SC4210_GAIN_MAX;

	dgain_total = total_gain * 1000 / 43656;

		if (total_gain < 2000) { /* 1 - 2x gain */
			*again = 0x03;
			*again_fine = total_gain*64/1000;
			*dgain = 0x00;
			*dgain_fine = 0x80;
		} else if (total_gain < 2750) { /* 2x - 2.75x gain */
			*again = 0x07;
			*again_fine = total_gain*64/2000;
			*dgain = 0x00;
			*dgain_fine = 0x80;
		} else if (total_gain < 2750 * 2) { /* 2.75xx - 5.5x gain */
			*again = 0x23;
			*again_fine = total_gain*64/2750;
			*dgain = 0x00;
			*dgain_fine = 0x80;
		} else if (total_gain < 2750 * 4) { /* 5.5x - 11.0x gain */
			*again = 0x27;
			*again_fine = total_gain*64/5500;
			*dgain = 0x00;
			*dgain_fine = 0x80;
		} else if (total_gain < 2750 * 8) { /* 11.0x - 22.0x gain */
			*again = 0x2f;
			*again_fine = total_gain*64/11000;
			*dgain = 0x00;
			*dgain_fine = 0x80;
		} else if (total_gain < 2750 * 16) { /* 22.0x - 43.656x gain */
			*again = 0x3f;
			*again_fine = total_gain*64/22000;
			*dgain = 0x00;
			*dgain_fine = 0x80;
		} else if (total_gain < 43656 * 2) { /* 43.656x - 87.312x gain */
			*again = 0x3f;
			*again_fine = 0x7f;
			*dgain = 0x00;
			*dgain_fine = dgain_total*128/1000;
		} else if (total_gain < 43656 * 4) { /* 87.312x - 174.624x gain */
			*again = 0x3f;
			*again_fine = 0x7f;
			*dgain = 0x01;
			*dgain_fine = dgain_total*128/2000;
		} else if (total_gain < 43656 * 8) { /* 174.624x - 349.248x gain */
			*again = 0x3f;
			*again_fine = 0x7f;
			*dgain = 0x03;
			*dgain_fine = dgain_total*128/4000;
		} else if (total_gain < 43656 * 16) { /* 349.248x - 698.496x gain */
			*again = 0x3f;
			*again_fine = 0x7f;
			*dgain = 0x07;
			*dgain_fine = dgain_total*128/8000;
		} else if (total_gain < 43656 * 32) { /* 698.496x - 1375.164x gain */
			*again = 0x3f;
			*again_fine = 0x7f;
			*dgain = 0x0f;
			*dgain_fine = dgain_total*128/16000;
		}
}

static int sc4210_set_hdrae(struct sc4210 *sc4210,
			     struct preisp_hdrae_exp_s *ae)
{
	int ret = 0;
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_t_gain, m_t_gain, s_t_gain;
	u32 l_again = 0, l_again_fine = 0, l_dgain = 0, l_dgain_fine = 0;
	u32 s_again = 0, s_again_fine = 0, s_dgain = 0, s_dgain_fine = 0;

	if (!sc4210->has_init_exp && !sc4210->streaming) {
		sc4210->init_hdrae_exp = *ae;
		sc4210->has_init_exp = true;
		dev_dbg(&sc4210->client->dev,
			"sc4210 don't stream, record exp for hdr!\n");
		return ret;
	}

	l_exp_time = ae->long_exp_reg;
	m_exp_time = ae->middle_exp_reg;
	s_exp_time = ae->short_exp_reg;
	l_t_gain = ae->long_gain_reg;
	m_t_gain = ae->middle_gain_reg;
	s_t_gain = ae->short_gain_reg;

	if (sc4210->cur_mode->hdr_mode == HDR_X2) {
		//2 stagger
		l_t_gain = m_t_gain;
		l_exp_time = m_exp_time;
	}

	l_exp_time = l_exp_time << 1;
	s_exp_time = s_exp_time << 1;

	// set exposure reg
	ret |= sc4210_write_reg(sc4210->client,
				 SC4210_REG_EXPOSURE_H,
				 SC4210_REG_VALUE_08BIT,
				 SC4210_FETCH_EXP_H(l_exp_time));
	ret |= sc4210_write_reg(sc4210->client,
				 SC4210_REG_EXPOSURE_M,
				 SC4210_REG_VALUE_08BIT,
				 SC4210_FETCH_EXP_M(l_exp_time));
	ret |= sc4210_write_reg(sc4210->client,
				 SC4210_REG_EXPOSURE_L,
				 SC4210_REG_VALUE_08BIT,
				 SC4210_FETCH_EXP_L(l_exp_time));
	//ret |= sc4210_write_reg(sc4210->client,
	//			 SC4210_REG_SHORT_EXPOSURE_H,
	//			 SC4210_REG_VALUE_08BIT,
	//			 SC4210_FETCH_EXP_H(s_exp_time));
	ret |= sc4210_write_reg(sc4210->client,
				 SC4210_REG_SHORT_EXPOSURE_M,
				 SC4210_REG_VALUE_08BIT,
				 SC4210_FETCH_EXP_M(s_exp_time));
	ret |= sc4210_write_reg(sc4210->client,
				 SC4210_REG_SHORT_EXPOSURE_L,
				 SC4210_REG_VALUE_08BIT,
				 SC4210_FETCH_EXP_L(s_exp_time));

	// set gain reg
	sc4210_get_gain_reg(l_t_gain, &l_again, &l_again_fine, &l_dgain, &l_dgain_fine);
	sc4210_get_gain_reg(s_t_gain, &s_again, &s_again_fine, &s_dgain, &s_dgain_fine);

	ret |= sc4210_write_reg(sc4210->client,
				 SC4210_REG_DIG_GAIN,
				 SC4210_REG_VALUE_08BIT,
				 l_dgain);
	ret |= sc4210_write_reg(sc4210->client,
				 SC4210_REG_DIG_FINE_GAIN,
				 SC4210_REG_VALUE_08BIT,
				 l_dgain_fine);
	ret |= sc4210_write_reg(sc4210->client,
				 SC4210_REG_ANA_GAIN,
				 SC4210_REG_VALUE_08BIT,
				 l_again);
	ret |= sc4210_write_reg(sc4210->client,
				 SC4210_REG_ANA_FINE_GAIN,
				 SC4210_REG_VALUE_08BIT,
				 l_again_fine);

	ret |= sc4210_write_reg(sc4210->client,
				 SC4210_REG_SDIG_GAIN,
				 SC4210_REG_VALUE_08BIT,
				 s_dgain);
	ret |= sc4210_write_reg(sc4210->client,
				 SC4210_REG_SDIG_FINE_GAIN,
				 SC4210_REG_VALUE_08BIT,
				 s_dgain_fine);
	ret |= sc4210_write_reg(sc4210->client,
				 SC4210_REG_SANA_GAIN,
				 SC4210_REG_VALUE_08BIT,
				 s_again);
	ret |= sc4210_write_reg(sc4210->client,
				 SC4210_REG_SANA_FINE_GAIN,
				 SC4210_REG_VALUE_08BIT,
				 s_again_fine);
	return ret;
}

static int sc4210_get_channel_info(struct sc4210 *sc4210, struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = sc4210->cur_mode->vc[ch_info->index];
	ch_info->width = sc4210->cur_mode->width;
	ch_info->height = sc4210->cur_mode->height;
	ch_info->bus_fmt = sc4210->cur_mode->bus_fmt;
	return 0;
}

static long sc4210_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc4210 *sc4210 = to_sc4210(sd);
	struct rkmodule_hdr_cfg *hdr;
	const struct sc4210_mode *mode;
	struct rkmodule_channel_info *ch_info;
	long ret = 0;
	u32 i, h = 0, w;
	u32 stream = 0;
	int pixel_rate = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		sc4210_get_module_inf(sc4210, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = sc4210->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = sc4210->cur_mode->width;
		h = sc4210->cur_mode->height;
		for (i = 0; i < sc4210->support_modes_num; i++) {
			if (w == sc4210->support_modes[i].width &&
				h == sc4210->support_modes[i].height &&
				sc4210->support_modes[i].hdr_mode == hdr->hdr_mode) {
				sc4210->cur_mode = &sc4210->support_modes[i];
				break;
			}
		}
		if (i == sc4210->support_modes_num) {
			dev_err(&sc4210->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			mode = sc4210->cur_mode;
			w = sc4210->cur_mode->hts_def -
					sc4210->cur_mode->width;
			h = sc4210->cur_mode->vts_def -
					sc4210->cur_mode->height;
			__v4l2_ctrl_modify_range(sc4210->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(sc4210->vblank, h,
						 SC4210_VTS_MAX -
						 sc4210->cur_mode->height,
						 1, h);

			__v4l2_ctrl_s_ctrl(sc4210->link_freq,
					   mode->mipi_freq_idx);

			pixel_rate = (int)link_freq_items[mode->mipi_freq_idx]
				     / mode->bpp * 2 * sc4210->lane_num;

			__v4l2_ctrl_s_ctrl_int64(sc4210->pixel_rate,
						 pixel_rate);

			dev_info(&sc4210->client->dev, "sensor mode: %d\n",
				 sc4210->cur_mode->hdr_mode);
		}
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		if (sc4210->cur_mode->hdr_mode == HDR_X2)
			ret = sc4210_set_hdrae(sc4210, arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);
		if (stream)
			ret = sc4210_write_reg(sc4210->client,
						SC4210_REG_CTRL_MODE,
						SC4210_REG_VALUE_08BIT,
						SC4210_MODE_STREAMING);
		else
			ret = sc4210_write_reg(sc4210->client,
						SC4210_REG_CTRL_MODE,
						SC4210_REG_VALUE_08BIT,
						SC4210_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = sc4210_get_channel_info(sc4210, ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc4210_compat_ioctl32(struct v4l2_subdev *sd,
					unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	struct rkmodule_channel_info *ch_info;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc4210_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				return -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc4210_ioctl(sd, cmd, hdr);
		if (!ret) {
			ret = copy_to_user(up, hdr, sizeof(*hdr));
			if (ret)
				return -EFAULT;
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

		ret = sc4210_ioctl(sd, cmd, hdr);
		kfree(hdr);
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		hdrae = kzalloc(sizeof(*hdrae), GFP_KERNEL);
		if (!hdrae) {
			ret = -ENOMEM;
			return ret;
		}

		if (copy_from_user(hdrae, up, sizeof(*hdrae))) {
			kfree(hdrae);
			return -EFAULT;
		}

			ret = sc4210_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;

		ret = sc4210_ioctl(sd, cmd, &stream);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc4210_ioctl(sd, cmd, ch_info);
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

static int __sc4210_start_stream(struct sc4210 *sc4210)
{
	int ret;

	ret = sc4210_write_array(sc4210->client, sc4210->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&sc4210->ctrl_handler);
	if (ret)
		return ret;
	if (sc4210->has_init_exp && sc4210->cur_mode->hdr_mode != NO_HDR) {
		ret = sc4210_ioctl(&sc4210->subdev, PREISP_CMD_SET_HDRAE_EXP,
				    &sc4210->init_hdrae_exp);
		if (ret) {
			dev_err(&sc4210->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}
	return sc4210_write_reg(sc4210->client, SC4210_REG_CTRL_MODE,
				 SC4210_REG_VALUE_08BIT,
				 SC4210_MODE_STREAMING);
}

static int __sc4210_stop_stream(struct sc4210 *sc4210)
{
	sc4210->has_init_exp = false;
	return sc4210_write_reg(sc4210->client, SC4210_REG_CTRL_MODE,
				 SC4210_REG_VALUE_08BIT,
				 SC4210_MODE_SW_STANDBY);
}

static int sc4210_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc4210 *sc4210 = to_sc4210(sd);
	struct i2c_client *client = sc4210->client;
	int ret = 0;

	mutex_lock(&sc4210->mutex);
	on = !!on;
	if (on == sc4210->streaming)
		goto unlock_and_return;
	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		ret = __sc4210_start_stream(sc4210);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc4210_stop_stream(sc4210);
		pm_runtime_put(&client->dev);
	}

	sc4210->streaming = on;

unlock_and_return:
	mutex_unlock(&sc4210->mutex);

	return ret;
}

static int sc4210_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc4210 *sc4210 = to_sc4210(sd);
	struct i2c_client *client = sc4210->client;
	int ret = 0;

	mutex_lock(&sc4210->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc4210->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		sc4210->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc4210->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc4210->mutex);

	return ret;
}

static int __sc4210_power_on(struct sc4210 *sc4210)
{
	int ret;
	struct device *dev = &sc4210->client->dev;

	if (!IS_ERR_OR_NULL(sc4210->pins_default)) {
		ret = pinctrl_select_state(sc4210->pinctrl,
					   sc4210->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(sc4210->xvclk, SC4210_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (27MHz)\n");
	if (clk_get_rate(sc4210->xvclk) != SC4210_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc4210->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(sc4210->reset_gpio))
		gpiod_set_value_cansleep(sc4210->reset_gpio, 0);

	ret = regulator_bulk_enable(sc4210_NUM_SUPPLIES, sc4210->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(sc4210->reset_gpio))
		gpiod_set_value_cansleep(sc4210->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(sc4210->pwdn_gpio))
		gpiod_set_value_cansleep(sc4210->pwdn_gpio, 1);

	usleep_range(4000, 5000);
	return 0;

disable_clk:
	clk_disable_unprepare(sc4210->xvclk);

	return ret;
}

static void __sc4210_power_off(struct sc4210 *sc4210)
{
	int ret;
	struct device *dev = &sc4210->client->dev;

	if (!IS_ERR(sc4210->pwdn_gpio))
		gpiod_set_value_cansleep(sc4210->pwdn_gpio, 0);
	clk_disable_unprepare(sc4210->xvclk);
	if (!IS_ERR(sc4210->reset_gpio))
		gpiod_set_value_cansleep(sc4210->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(sc4210->pins_sleep)) {
		ret = pinctrl_select_state(sc4210->pinctrl,
					   sc4210->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(sc4210_NUM_SUPPLIES, sc4210->supplies);
}

static int sc4210_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc4210 *sc4210 = to_sc4210(sd);

	return __sc4210_power_on(sc4210);
}

static int sc4210_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc4210 *sc4210 = to_sc4210(sd);

	__sc4210_power_off(sc4210);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc4210_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc4210 *sc4210 = to_sc4210(sd);
	struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc4210_mode *def_mode = &sc4210->support_modes[0];

	mutex_lock(&sc4210->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc4210->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sc4210_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct sc4210 *sc4210 = to_sc4210(sd);

	if (fie->index >= sc4210->support_modes_num)
		return -EINVAL;

	fie->code = sc4210->support_modes[fie->index].bus_fmt;
	fie->width = sc4210->support_modes[fie->index].width;
	fie->height = sc4210->support_modes[fie->index].height;
	fie->interval = sc4210->support_modes[fie->index].max_fps;
	fie->reserved[0] = sc4210->support_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops sc4210_pm_ops = {
	SET_RUNTIME_PM_OPS(sc4210_runtime_suspend,
	sc4210_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc4210_internal_ops = {
	.open = sc4210_open,
};
#endif

static const struct v4l2_subdev_core_ops sc4210_core_ops = {
	.s_power = sc4210_s_power,
	.ioctl = sc4210_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc4210_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc4210_video_ops = {
	.s_stream = sc4210_s_stream,
	.g_frame_interval = sc4210_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops sc4210_pad_ops = {
	.enum_mbus_code = sc4210_enum_mbus_code,
	.enum_frame_size = sc4210_enum_frame_sizes,
	.enum_frame_interval = sc4210_enum_frame_interval,
	.get_fmt = sc4210_get_fmt,
	.set_fmt = sc4210_set_fmt,
	.get_mbus_config = sc4210_g_mbus_config,
};

static const struct v4l2_subdev_ops sc4210_subdev_ops = {
	.core	= &sc4210_core_ops,
	.video	= &sc4210_video_ops,
	.pad	= &sc4210_pad_ops,
};

static void sc4210_modify_fps_info(struct sc4210 *sc4210)
{
	const struct sc4210_mode *mode = sc4210->cur_mode;

	sc4210->cur_fps.denominator = mode->max_fps.denominator * mode->vts_def /
				      sc4210->cur_vts;
}

static int sc4210_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc4210 *sc4210 = container_of(ctrl->handler,
					       struct sc4210, ctrl_handler);
	struct i2c_client *client = sc4210->client;
	s64 max;
	u32 again = 0, again_fine = 0, dgain = 0, dgain_fine = 0;
	int ret = 0;
	u32 val = 0, vts = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc4210->cur_mode->height + ctrl->val - 2;
		__v4l2_ctrl_modify_range(sc4210->exposure,
					 sc4210->exposure->minimum, max,
					 sc4210->exposure->step,
					 sc4210->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (sc4210->cur_mode->hdr_mode != NO_HDR)
			goto ctrl_end;
		val = ctrl->val << 1;
		ret = sc4210_write_reg(sc4210->client,
					SC4210_REG_EXPOSURE_H,
					SC4210_REG_VALUE_08BIT,
					SC4210_FETCH_EXP_H(val));
		ret |= sc4210_write_reg(sc4210->client,
					 SC4210_REG_EXPOSURE_M,
					 SC4210_REG_VALUE_08BIT,
					 SC4210_FETCH_EXP_M(val));
		ret |= sc4210_write_reg(sc4210->client,
					 SC4210_REG_EXPOSURE_L,
					 SC4210_REG_VALUE_08BIT,
					 SC4210_FETCH_EXP_L(val));

		dev_dbg(&client->dev, "set exposure 0x%x\n", val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (sc4210->cur_mode->hdr_mode != NO_HDR)
			goto ctrl_end;

		sc4210_get_gain_reg(ctrl->val, &again, &again_fine, &dgain, &dgain_fine);
		ret = sc4210_write_reg(sc4210->client,
					SC4210_REG_DIG_GAIN,
					SC4210_REG_VALUE_08BIT,
					dgain);
		ret |= sc4210_write_reg(sc4210->client,
					 SC4210_REG_DIG_FINE_GAIN,
					 SC4210_REG_VALUE_08BIT,
					 dgain_fine);
		ret |= sc4210_write_reg(sc4210->client,
					 SC4210_REG_ANA_GAIN,
					SC4210_REG_VALUE_08BIT,
					 again);
		ret |= sc4210_write_reg(sc4210->client,
					 SC4210_REG_ANA_FINE_GAIN,
					SC4210_REG_VALUE_08BIT,
					 again_fine);
		dev_dbg(&client->dev, "set gain 0x%x\n", ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		vts = ctrl->val + sc4210->cur_mode->height;
		ret = sc4210_write_reg(sc4210->client,
					SC4210_REG_VTS_H,
					SC4210_REG_VALUE_08BIT,
					(vts >> 8) & 0x7f);
		ret |= sc4210_write_reg(sc4210->client,
					 SC4210_REG_VTS_L,
					 SC4210_REG_VALUE_08BIT,
					 vts & 0xff);
		sc4210->cur_vts = ctrl->val + sc4210->cur_mode->height;
		sc4210_modify_fps_info(sc4210);
		dev_dbg(&client->dev, "set vblank 0x%x\n", ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = sc4210_read_reg(sc4210->client, SC4210_FLIP_MIRROR_REG,
				       SC4210_REG_VALUE_08BIT, &val);
		if (ret)
			break;

		if (ctrl->val)
			val |= SC4210_MIRROR_MASK;
		else
			val &= ~SC4210_MIRROR_MASK;
		ret |= sc4210_write_reg(sc4210->client,
					 SC4210_FLIP_MIRROR_REG,
					 SC4210_REG_VALUE_08BIT, val);
		break;
	case V4L2_CID_VFLIP:
		ret = sc4210_read_reg(sc4210->client,
				       SC4210_FLIP_MIRROR_REG,
				       SC4210_REG_VALUE_08BIT, &val);
		if (ret)
			break;

		if (ctrl->val)
			val |= SC4210_FLIP_MASK;
		else
			val &= ~SC4210_FLIP_MASK;
		ret |= sc4210_write_reg(sc4210->client,
					 SC4210_FLIP_MIRROR_REG,
					 SC4210_REG_VALUE_08BIT,
					 val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

ctrl_end:
	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc4210_ctrl_ops = {
	.s_ctrl = sc4210_set_ctrl,
};

static int sc4210_parse_of(struct sc4210 *sc4210)
{
	struct device *dev = &sc4210->client->dev;
	struct device_node *endpoint;
	struct fwnode_handle *fwnode;
	int rval;

	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}
	fwnode = of_fwnode_handle(endpoint);
	rval = fwnode_property_read_u32_array(fwnode, "data-lanes", NULL, 0);
	if (rval <= 0) {
		dev_err(dev, " Get mipi lane num failed!\n");
		return -EINVAL;
	}

	sc4210->lane_num = rval;
	dev_info(dev, "lane_num = %d\n", sc4210->lane_num);

	if (sc4210->lane_num == 2) {
		sc4210->support_modes = supported_modes_2lane;
		sc4210->support_modes_num = ARRAY_SIZE(supported_modes_2lane);
	} else if (sc4210->lane_num == 4) {
		sc4210->support_modes = supported_modes_4lane;
		sc4210->support_modes_num = ARRAY_SIZE(supported_modes_4lane);
	}

	sc4210->cur_mode = &sc4210->support_modes[0];

	return 0;
}

static int sc4210_initialize_controls(struct sc4210 *sc4210)
{
	const struct sc4210_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u64 dst_pixel_rate = 0;
	u32 h_blank;
	int ret;

	handler = &sc4210->ctrl_handler;
	mode = sc4210->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &sc4210->mutex;

	sc4210->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ,
				ARRAY_SIZE(link_freq_items) - 1, 0,
				link_freq_items);
	__v4l2_ctrl_s_ctrl(sc4210->link_freq, mode->mipi_freq_idx);

	if (mode->mipi_freq_idx == 0)
		dst_pixel_rate = SC4210_PIXEL_RATES_2LANE_LINEAR;
	else if (mode->mipi_freq_idx == 1)
		dst_pixel_rate = SC4210_PIXEL_RATES_2LANE_HDR2;
	else if (mode->mipi_freq_idx == 2)
		dst_pixel_rate = SC4210_PIXEL_RATES_4LANE_LINEAR;
	else if (mode->mipi_freq_idx == 2)
		dst_pixel_rate = SC4210_PIXEL_RATES_4LANE_HDR2;

	sc4210->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
						V4L2_CID_PIXEL_RATE, 0,
						SC4210_MAX_PIXEL_RATE,
						1, dst_pixel_rate);

	h_blank = mode->hts_def - mode->width;
	sc4210->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (sc4210->hblank)
		sc4210->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	sc4210->vblank = v4l2_ctrl_new_std(handler, &sc4210_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    SC4210_VTS_MAX - mode->height,
					    1, vblank_def);
	sc4210->cur_fps = mode->max_fps;
	exposure_max = (mode->vts_def << 1) - 2;
	sc4210->exposure = v4l2_ctrl_new_std(handler, &sc4210_ctrl_ops,
					      V4L2_CID_EXPOSURE,
					      SC4210_EXPOSURE_MIN,
					      exposure_max,
					      SC4210_EXPOSURE_STEP,
					      mode->exp_def);

	sc4210->anal_gain = v4l2_ctrl_new_std(handler, &sc4210_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN,
					       SC4210_GAIN_MIN,
					       SC4210_GAIN_MAX,
					       SC4210_GAIN_STEP,
					       SC4210_GAIN_DEFAULT);

	v4l2_ctrl_new_std(handler, &sc4210_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(handler, &sc4210_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&sc4210->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}
	sc4210->subdev.ctrl_handler = handler;
	sc4210->has_init_exp = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);
	return ret;
}

static int sc4210_check_sensor_id(struct sc4210 *sc4210,
				   struct i2c_client *client)
{
	struct device *dev = &sc4210->client->dev;
	u32 id = 0;
	int ret;

	ret = sc4210_read_reg(client, SC4210_REG_CHIP_ID,
			       SC4210_REG_VALUE_16BIT, &id);
	if (id != SC4210_CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected SC%06x sensor\n", SC4210_CHIP_ID);

	return 0;
}

static int sc4210_configure_regulators(struct sc4210 *sc4210)
{
	unsigned int i;

	for (i = 0; i < sc4210_NUM_SUPPLIES; i++)
		sc4210->supplies[i].supply = sc4210_supply_names[i];

	return devm_regulator_bulk_get(&sc4210->client->dev,
				       sc4210_NUM_SUPPLIES,
				       sc4210->supplies);
}

static int sc4210_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc4210 *sc4210;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	sc4210 = devm_kzalloc(dev, sizeof(*sc4210), GFP_KERNEL);
	if (!sc4210)
		return -ENOMEM;

	of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc4210->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc4210->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc4210->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc4210->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	sc4210->client = client;

	ret = sc4210_parse_of(sc4210);
	if (ret)
		return -EINVAL;

	sc4210->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc4210->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	sc4210->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sc4210->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	sc4210->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(sc4210->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	sc4210->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc4210->pinctrl)) {
		sc4210->pins_default =
			pinctrl_lookup_state(sc4210->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc4210->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		sc4210->pins_sleep =
			pinctrl_lookup_state(sc4210->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc4210->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = sc4210_configure_regulators(sc4210);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&sc4210->mutex);

	sd = &sc4210->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc4210_subdev_ops);
	ret = sc4210_initialize_controls(sc4210);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc4210_power_on(sc4210);
	if (ret)
		goto err_free_handler;

	ret = sc4210_check_sensor_id(sc4210, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc4210_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc4210->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc4210->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc4210->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		sc4210->module_index, facing,
		SC4210_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(&sc4210->client->dev,
			"v4l2 async register subdev failed\n");
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
	__sc4210_power_off(sc4210);
err_free_handler:
	v4l2_ctrl_handler_free(&sc4210->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc4210->mutex);

	return ret;
}

static int sc4210_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc4210 *sc4210 = to_sc4210(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc4210->ctrl_handler);
	mutex_destroy(&sc4210->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc4210_power_off(sc4210);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc4210_of_match[] = {
	{ .compatible = "smartsens,sc4210" },
	{ },
};
MODULE_DEVICE_TABLE(of, sc4210_of_match);
#endif

static const struct i2c_device_id sc4210_match_id[] = {
	{ "smartsens,sc4210", 0 },
	{ },
};

static struct i2c_driver sc4210_i2c_driver = {
	.driver = {
		.name = SC4210_NAME,
		.pm = &sc4210_pm_ops,
		.of_match_table = of_match_ptr(sc4210_of_match),
	},
	.probe		= &sc4210_probe,
	.remove		= &sc4210_remove,
	.id_table	= sc4210_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc4210_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc4210_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("smartsens sc4210 sensor driver");
MODULE_LICENSE("GPL");
