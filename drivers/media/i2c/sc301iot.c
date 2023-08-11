// SPDX-License-Identifier: GPL-2.0
/*
 * sc301iot driver
 *
 * Copyright (C) 2022 Fuzhou Rockchip Electronics Co., Ltd.
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
#include <linux/rk-camera-module.h>
#include <linux/rk-preisp.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include "../platform/rockchip/isp/rkisp_tb_helper.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define SC301IOT_LANES			2
#define SC301IOT_BITS_PER_SAMPLE		10
#define SC301IOT_LINK_FREQ_594		540000000// 540Mbps

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define PIXEL_RATE_WITH_594M_10BIT	(SC301IOT_LINK_FREQ_594 / SC301IOT_BITS_PER_SAMPLE * \
						2 * SC301IOT_LANES)
#define SC301IOT_XVCLK_FREQ		24000000


#define CHIP_ID				0xcc40
#define SC301IOT_REG_CHIP_ID		0x3107

#define SC301IOT_REG_CTRL_MODE		0x0100
#define SC301IOT_MODE_SW_STANDBY		0x0
#define SC301IOT_MODE_STREAMING		BIT(0)

#define SC301IOT_REG_EXPOSURE_H		0x3e00
#define SC301IOT_REG_EXPOSURE_M		0x3e01
#define SC301IOT_REG_EXPOSURE_L		0x3e02
#define SC301IOT_REG_SEXPOSURE_H		0x3e22
#define SC301IOT_REG_SEXPOSURE_M		0x3e04
#define SC301IOT_REG_SEXPOSURE_L		0x3e05
#define	SC301IOT_EXPOSURE_MIN		2
#define	SC301IOT_EXPOSURE_STEP		1
#define SC301IOT_VTS_MIN			0x640
#define SC301IOT_VTS_MAX			0x7fff

#define SC301IOT_REG_DIG_GAIN		0x3e06
#define SC301IOT_REG_DIG_FINE_GAIN	0x3e07
//#define SC301IOT_REG_ANA_GAIN		0x3e08
#define SC301IOT_REG_ANA_GAIN	0x3e09
#define SC301IOT_REG_SDIG_GAIN		0x3e10
#define SC301IOT_REG_SDIG_FINE_GAIN	0x3e11
//#define SC301IOT_REG_SANA_GAIN		0x3e12
#define SC301IOT_REG_SANA_GAIN	0x3e13
#define SC301IOT_GAIN_MIN		0x0040
#define SC301IOT_GAIN_MAX		(6426) //(100.416*64)
#define SC301IOT_GAIN_STEP		1
#define SC301IOT_GAIN_DEFAULT		0x0400
#define SC301IOT_LGAIN			0
#define SC301IOT_SGAIN			1

#define SC301IOT_REG_GROUP_HOLD		0x3812
#define SC301IOT_GROUP_HOLD_START	0x00
#define SC301IOT_GROUP_HOLD_END		0x30

//#define SC301IOT_REG_HIGH_TEMP_H		0x3974
//#define SC301IOT_REG_HIGH_TEMP_L		0x3975

#define SC301IOT_REG_TEST_PATTERN	0x4501
#define SC301IOT_TEST_PATTERN_BIT_MASK	BIT(3)

#define SC301IOT_REG_VTS_H		0x320e
#define SC301IOT_REG_VTS_L		0x320f

#define SC301IOT_FLIP_MIRROR_REG		0x3221

#define SC301IOT_FETCH_EXP_H(VAL)		(((VAL) >> 12) & 0xF)
#define SC301IOT_FETCH_EXP_M(VAL)		(((VAL) >> 4) & 0xFF)
#define SC301IOT_FETCH_EXP_L(VAL)		(((VAL) & 0xF) << 4)

#define SC301IOT_FETCH_AGAIN_H(VAL)		(((VAL) >> 8) & 0x03)
#define SC301IOT_FETCH_AGAIN_L(VAL)		((VAL) & 0xFF)

#define SC301IOT_FETCH_MIRROR(VAL, ENABLE)	(ENABLE ? VAL | 0x06 : VAL & 0xf9)
#define SC301IOT_FETCH_FLIP(VAL, ENABLE)		(ENABLE ? VAL | 0x60 : VAL & 0x9f)

#define REG_DELAY			0xFFFE
#define REG_NULL			0xFFFF

#define SC301IOT_REG_VALUE_08BIT		1
#define SC301IOT_REG_VALUE_16BIT		2
#define SC301IOT_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"
#define SC301IOT_NAME			"sc301iot"

static const char * const SC301IOT_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define SC301IOT_NUM_SUPPLIES ARRAY_SIZE(SC301IOT_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct sc301iot_mode {
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

struct sc301iot {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[SC301IOT_NUM_SUPPLIES];

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
	struct v4l2_fract	cur_fps;
	bool			streaming;
	bool			power_on;
	const struct sc301iot_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	bool			is_thunderboot;
	bool			is_first_streamoff;
	u32         sync_mode;
};

#define to_sc301iot(sd) container_of(sd, struct sc301iot, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval sc301iot_global_regs[] = {
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 540Mbps, 2lane
 */
static const struct regval sc301iot_linear_10_2048x1536_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x37f9, 0x80},
	{0x301c, 0x78},
	{0x301f, 0x11},
	{0x30b8, 0x44},
	{0x3208, 0x08},
	{0x3209, 0x00},
	{0x320a, 0x06},
	{0x320b, 0x00},
	{0x320c, 0x04},
	{0x320d, 0x65},
	{0x320e, 0x06},
	{0x320f, 0x40},
	{0x3214, 0x11},
	{0x3215, 0x11},
	// {0x3223, 0xc0},
	{0x3253, 0x0c},
	{0x3274, 0x09},
	{0x3301, 0x08},
	{0x3306, 0x58},
	{0x3308, 0x08},
	{0x330a, 0x00},
	{0x330b, 0xe0},
	{0x330e, 0x10},
	{0x3314, 0x14},
	{0x331e, 0x55},
	{0x331f, 0x7d},
	{0x3333, 0x10},
	{0x3334, 0x40},
	{0x335e, 0x06},
	{0x335f, 0x08},
	{0x3364, 0x5e},
	{0x337c, 0x02},
	{0x337d, 0x0a},
	{0x3390, 0x01},
	{0x3391, 0x03},
	{0x3392, 0x07},
	{0x3393, 0x08},
	{0x3394, 0x08},
	{0x3395, 0x08},
	{0x3396, 0x08},
	{0x3397, 0x09},
	{0x3398, 0x1f},
	{0x3399, 0x08},
	{0x339a, 0x0a},
	{0x339b, 0x40},
	{0x339c, 0x88},
	{0x33a2, 0x04},
	{0x33ad, 0x0c},
	{0x33b1, 0x80},
	{0x33b3, 0x30},
	{0x33f9, 0x68},
	{0x33fb, 0x80},
	{0x33fc, 0x48},
	{0x33fd, 0x5f},
	{0x349f, 0x03},
	{0x34a6, 0x48},
	{0x34a7, 0x5f},
	{0x34a8, 0x30},
	{0x34a9, 0x30},
	{0x34aa, 0x00},
	{0x34ab, 0xf0},
	{0x34ac, 0x01},
	{0x34ad, 0x08},
	{0x34f8, 0x5f},
	{0x34f9, 0x10},
	{0x3630, 0xf0},
	{0x3631, 0x85},
	{0x3632, 0x74},
	{0x3633, 0x22},
	{0x3637, 0x4d},
	{0x3638, 0xcb},
	{0x363a, 0x8b},
	{0x363c, 0x08},
	{0x3640, 0x00},
	{0x3641, 0x38},
	{0x3670, 0x4e},
	{0x3674, 0xc0},
	{0x3675, 0xb0},
	{0x3676, 0xa0},
	{0x3677, 0x83},
	{0x3678, 0x87},
	{0x3679, 0x8a},
	{0x367c, 0x49},
	{0x367d, 0x4f},
	{0x367e, 0x48},
	{0x367f, 0x4b},
	{0x3690, 0x33},
	{0x3691, 0x33},
	{0x3692, 0x44},
	{0x3699, 0x8a},
	{0x369a, 0xa1},
	{0x369b, 0xc2},
	{0x369c, 0x48},
	{0x369d, 0x4f},
	{0x36a2, 0x4b},
	{0x36a3, 0x4f},
	{0x36ea, 0x09},
	{0x36eb, 0x0d},
	{0x36ec, 0x1c},
	{0x36ed, 0x25},
	{0x370f, 0x01},
	{0x3714, 0x00},
	{0x3722, 0x09},
	{0x3724, 0x41},
	{0x3725, 0xc1},
	{0x3728, 0x00},
	{0x3771, 0x09},
	{0x3772, 0x05},
	{0x3773, 0x05},
	{0x377a, 0x48},
	{0x377b, 0x49},
	{0x37fa, 0x09},
	{0x37fb, 0x33},
	{0x37fc, 0x11},
	{0x37fd, 0x18},
	{0x3905, 0x8d},
	{0x391d, 0x08},
	{0x3922, 0x1a},
	{0x3926, 0x21},
	{0x3933, 0x80},
	{0x3934, 0x0d},
	{0x3937, 0x6a},
	{0x3939, 0x00},
	{0x393a, 0x0e},
	{0x39dc, 0x02},
	{0x3e00, 0x00},
	{0x3e01, 0x63},
	{0x3e02, 0x80},
	{0x3e03, 0x0b},
	{0x3e1b, 0x2a},
	{0x4407, 0x34},
	{0x440e, 0x02},
	{0x5001, 0x40},
	{0x5007, 0x80},
	{0x36e9, 0x24},
	{0x37f9, 0x24},
	{0x3251, 0x90},

	/* strong signal */
	{0x3650, 0x33},
	{0x3651, 0x7f},

	{0x3028, 0x05},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 1080Mbps, HDR 2lane
 */
static const struct regval sc301iot_hdr_10_2048x1536_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x37f9, 0x80},
	{0x301c, 0x78},
	{0x301f, 0x12},
	{0x30b8, 0x44},
	{0x3208, 0x08},
	{0x3209, 0x00},
	{0x320a, 0x06},
	{0x320b, 0x00},
	{0x320c, 0x04},
	{0x320d, 0x65},
	{0x320e, 0x0c},
	{0x320f, 0x80},
	{0x3214, 0x11},
	{0x3215, 0x11},
	// {0x3223, 0xc0},
	{0x3250, 0xff},
	{0x3253, 0x0c},
	{0x3274, 0x09},
	{0x3281, 0x01},
	{0x3301, 0x08},
	{0x3304, 0x80},
	{0x3306, 0x58},
	{0x3308, 0x08},
	{0x3309, 0xa0},
	{0x330a, 0x00},
	{0x330b, 0xe0},
	{0x330e, 0x10},
	{0x3314, 0x14},
	{0x331e, 0x71},
	{0x331f, 0x91},
	{0x3333, 0x10},
	{0x3334, 0x40},
	{0x335e, 0x06},
	{0x335f, 0x08},
	{0x3364, 0x5e},
	{0x337c, 0x02},
	{0x337d, 0x0a},
	{0x3390, 0x01},
	{0x3391, 0x03},
	{0x3392, 0x07},
	{0x3393, 0x08},
	{0x3394, 0x08},
	{0x3395, 0x08},
	{0x3396, 0x08},
	{0x3397, 0x09},
	{0x3398, 0x1f},
	{0x3399, 0x08},
	{0x339a, 0x14},
	{0x339b, 0x28},
	{0x339c, 0x78},
	{0x33a2, 0x04},
	{0x33ad, 0x0c},
	{0x33b1, 0x80},
	{0x33b3, 0x38},
	{0x33f9, 0x58},
	{0x33fb, 0x80},
	{0x33fc, 0x48},
	{0x33fd, 0x4f},
	{0x349f, 0x03},
	{0x34a6, 0x48},
	{0x34a7, 0x4f},
	{0x34a8, 0x38},
	{0x34a9, 0x28},
	{0x34aa, 0x00},
	{0x34ab, 0xe0},
	{0x34ac, 0x01},
	{0x34ad, 0x08},
	{0x34f8, 0x5f},
	{0x34f9, 0x18},
	{0x3630, 0xf0},
	{0x3631, 0x85},
	{0x3632, 0x74},
	{0x3633, 0x22},
	{0x3637, 0x4d},
	{0x3638, 0xcb},
	{0x363a, 0x8b},
	{0x363c, 0x08},
	{0x3641, 0x38},
	{0x3670, 0x4e},
	{0x3674, 0xc0},
	{0x3675, 0xa0},
	{0x3676, 0x90},
	{0x3677, 0x83},
	{0x3678, 0x86},
	{0x3679, 0x89},
	{0x367c, 0x48},
	{0x367d, 0x4f},
	{0x367e, 0x48},
	{0x367f, 0x4b},
	{0x3690, 0x33},
	{0x3691, 0x44},
	{0x3692, 0x55},
	{0x3699, 0x8a},
	{0x369a, 0xa1},
	{0x369b, 0xc2},
	{0x369c, 0x48},
	{0x369d, 0x4f},
	{0x36a2, 0x4b},
	{0x36a3, 0x4f},
	{0x36ea, 0x09},
	{0x36eb, 0x0d},
	{0x36ec, 0x0c},
	{0x36ed, 0x25},
	{0x370f, 0x01},
	{0x3714, 0x00},
	{0x3722, 0x01},
	{0x3724, 0x41},
	{0x3725, 0xc1},
	{0x3728, 0x00},
	{0x3771, 0x09},
	{0x3772, 0x09},
	{0x3773, 0x05},
	{0x377a, 0x48},
	{0x377b, 0x4f},
	{0x37fa, 0x09},
	{0x37fb, 0x31},
	{0x37fc, 0x10},
	{0x37fd, 0x18},
	{0x3905, 0x8d},
	{0x391d, 0x08},
	{0x3922, 0x1a},
	{0x3926, 0x21},
	{0x3933, 0x80},
	{0x3934, 0x0d},
	{0x3937, 0x6a},
	{0x3939, 0x00},
	{0x393a, 0x0e},
	{0x39dc, 0x02},
	{0x3e00, 0x00},
	{0x3e01, 0xb9},
	{0x3e02, 0xc0},
	{0x3e03, 0x0b},
	{0x3e04, 0x0b},
	{0x3e05, 0xa0},
	{0x3e1b, 0x2a},
	{0x3e23, 0x00},
	{0x3e24, 0xbf},
	{0x4407, 0x34},
	{0x440e, 0x02},
	{0x4509, 0x10},
	{0x4816, 0x71},
	{0x5001, 0x40},
	{0x5007, 0x80},
	{0x36e9, 0x24},
	{0x37f9, 0x24},
	{0x3251, 0x90},

	/* strong signal */
	{0x3650, 0x33},
	{0x3651, 0x7f},

	{0x3028, 0x05},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 540Mbps, 2lane
 */
static const struct regval sc301iot_linear_10_1536x1536_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x37f9, 0x80},
	{0x301c, 0x78},
	{0x301f, 0x11},
	{0x30b8, 0x44},
	{0x3208, 0x06},
	{0x3209, 0x00},
	{0x320a, 0x06},
	{0x320b, 0x00},
	{0x320c, 0x04},
	{0x320d, 0x65},
	{0x320e, 0x06},
	{0x320f, 0x40},
	{0x3210, 0x01},
	{0x3214, 0x11},
	{0x3215, 0x11},
	// {0x3223, 0xc0},
	{0x3253, 0x0c},
	{0x3274, 0x09},
	{0x3301, 0x08},
	{0x3306, 0x58},
	{0x3308, 0x08},
	{0x330a, 0x00},
	{0x330b, 0xe0},
	{0x330e, 0x10},
	{0x3314, 0x14},
	{0x331e, 0x55},
	{0x331f, 0x7d},
	{0x3333, 0x10},
	{0x3334, 0x40},
	{0x335e, 0x06},
	{0x335f, 0x08},
	{0x3364, 0x5e},
	{0x337c, 0x02},
	{0x337d, 0x0a},
	{0x3390, 0x01},
	{0x3391, 0x03},
	{0x3392, 0x07},
	{0x3393, 0x08},
	{0x3394, 0x08},
	{0x3395, 0x08},
	{0x3396, 0x08},
	{0x3397, 0x09},
	{0x3398, 0x1f},
	{0x3399, 0x08},
	{0x339a, 0x0a},
	{0x339b, 0x40},
	{0x339c, 0x88},
	{0x33a2, 0x04},
	{0x33ad, 0x0c},
	{0x33b1, 0x80},
	{0x33b3, 0x30},
	{0x33f9, 0x68},
	{0x33fb, 0x80},
	{0x33fc, 0x48},
	{0x33fd, 0x5f},
	{0x349f, 0x03},
	{0x34a6, 0x48},
	{0x34a7, 0x5f},
	{0x34a8, 0x30},
	{0x34a9, 0x30},
	{0x34aa, 0x00},
	{0x34ab, 0xf0},
	{0x34ac, 0x01},
	{0x34ad, 0x08},
	{0x34f8, 0x5f},
	{0x34f9, 0x10},
	{0x3630, 0xf0},
	{0x3631, 0x85},
	{0x3632, 0x74},
	{0x3633, 0x22},
	{0x3637, 0x4d},
	{0x3638, 0xcb},
	{0x363a, 0x8b},
	{0x363c, 0x08},
	{0x3640, 0x00},
	{0x3641, 0x38},
	{0x3670, 0x4e},
	{0x3674, 0xc0},
	{0x3675, 0xb0},
	{0x3676, 0xa0},
	{0x3677, 0x83},
	{0x3678, 0x87},
	{0x3679, 0x8a},
	{0x367c, 0x49},
	{0x367d, 0x4f},
	{0x367e, 0x48},
	{0x367f, 0x4b},
	{0x3690, 0x33},
	{0x3691, 0x33},
	{0x3692, 0x44},
	{0x3699, 0x8a},
	{0x369a, 0xa1},
	{0x369b, 0xc2},
	{0x369c, 0x48},
	{0x369d, 0x4f},
	{0x36a2, 0x4b},
	{0x36a3, 0x4f},
	{0x36ea, 0x09},
	{0x36eb, 0x0d},
	{0x36ec, 0x1c},
	{0x36ed, 0x25},
	{0x370f, 0x01},
	{0x3714, 0x00},
	{0x3722, 0x09},
	{0x3724, 0x41},
	{0x3725, 0xc1},
	{0x3728, 0x00},
	{0x3771, 0x09},
	{0x3772, 0x05},
	{0x3773, 0x05},
	{0x377a, 0x48},
	{0x377b, 0x49},
	{0x37fa, 0x09},
	{0x37fb, 0x33},
	{0x37fc, 0x11},
	{0x37fd, 0x18},
	{0x3905, 0x8d},
	{0x391d, 0x08},
	{0x3922, 0x1a},
	{0x3926, 0x21},
	{0x3933, 0x80},
	{0x3934, 0x0d},
	{0x3937, 0x6a},
	{0x3939, 0x00},
	{0x393a, 0x0e},
	{0x39dc, 0x02},
	{0x3e00, 0x00},
	{0x3e01, 0x63},
	{0x3e02, 0x80},
	{0x3e03, 0x0b},
	{0x3e1b, 0x2a},
	{0x4407, 0x34},
	{0x440e, 0x02},
	{0x5001, 0x40},
	{0x5007, 0x80},
	{0x36e9, 0x24},
	{0x37f9, 0x24},
	{0x3251, 0x90},

	/* strong signal */
	{0x3650, 0x33},
	{0x3651, 0x7f},

	{0x3028, 0x05},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 1080Mbps, HDR 2lane
 */
static const struct regval sc301iot_hdr_10_1536x1536_regs[] = {
	{0x0103,  0x01},
	{0x0100, 0x00},
	{0x36e9,  0x80},
	{0x37f9, 0x80},
	{0x301c, 0x78},
	{0x301f, 0x12},
	{0x30b8, 0x44},
	{0x3208, 0x06},
	{0x3209, 0x00},
	{0x320a, 0x06},
	{0x320b, 0x00},
	{0x320c, 0x04},
	{0x320d, 0x65},
	{0x320e, 0x0c},
	{0x320f, 0x80},
	{0x3210, 0x01},
	{0x3214, 0x11},
	{0x3215, 0x11},
	// {0x3223, 0xc0},
	{0x3250, 0xff},
	{0x3253, 0x0c},
	{0x3274, 0x09},
	{0x3281, 0x01},
	{0x3301, 0x08},
	{0x3304, 0x80},
	{0x3306, 0x58},
	{0x3308, 0x08},
	{0x3309, 0xa0},
	{0x330a, 0x00},
	{0x330b, 0xe0},
	{0x330e, 0x10},
	{0x3314, 0x14},
	{0x331e, 0x71},
	{0x331f, 0x91},
	{0x3333, 0x10},
	{0x3334, 0x40},
	{0x335e, 0x06},
	{0x335f, 0x08},
	{0x3364, 0x5e},
	{0x337c, 0x02},
	{0x337d, 0x0a},
	{0x3390, 0x01},
	{0x3391, 0x03},
	{0x3392, 0x07},
	{0x3393, 0x08},
	{0x3394, 0x08},
	{0x3395, 0x08},
	{0x3396, 0x08},
	{0x3397, 0x09},
	{0x3398, 0x1f},
	{0x3399, 0x08},
	{0x339a, 0x14},
	{0x339b, 0x28},
	{0x339c, 0x78},
	{0x33a2, 0x04},
	{0x33ad, 0x0c},
	{0x33b1, 0x80},
	{0x33b3, 0x38},
	{0x33f9, 0x58},
	{0x33fb, 0x80},
	{0x33fc, 0x48},
	{0x33fd, 0x4f},
	{0x349f, 0x03},
	{0x34a6, 0x48},
	{0x34a7, 0x4f},
	{0x34a8, 0x38},
	{0x34a9, 0x28},
	{0x34aa, 0x00},
	{0x34ab, 0xe0},
	{0x34ac, 0x01},
	{0x34ad, 0x08},
	{0x34f8, 0x5f},
	{0x34f9, 0x18},
	{0x3630, 0xf0},
	{0x3631, 0x85},
	{0x3632, 0x74},
	{0x3633, 0x22},
	{0x3637, 0x4d},
	{0x3638, 0xcb},
	{0x363a, 0x8b},
	{0x363c, 0x08},
	{0x3641, 0x38},
	{0x3670, 0x4e},
	{0x3674, 0xc0},
	{0x3675, 0xa0},
	{0x3676, 0x90},
	{0x3677, 0x83},
	{0x3678, 0x86},
	{0x3679, 0x89},
	{0x367c, 0x48},
	{0x367d, 0x4f},
	{0x367e, 0x48},
	{0x367f, 0x4b},
	{0x3690, 0x33},
	{0x3691, 0x44},
	{0x3692, 0x55},
	{0x3699, 0x8a},
	{0x369a, 0xa1},
	{0x369b, 0xc2},
	{0x369c, 0x48},
	{0x369d, 0x4f},
	{0x36a2, 0x4b},
	{0x36a3, 0x4f},
	{0x36ea, 0x09},
	{0x36eb, 0x0d},
	{0x36ec, 0x0c},
	{0x36ed, 0x25},
	{0x370f, 0x01},
	{0x3714, 0x00},
	{0x3722, 0x01},
	{0x3724, 0x41},
	{0x3725, 0xc1},
	{0x3728, 0x00},
	{0x3771, 0x09},
	{0x3772, 0x09},
	{0x3773, 0x05},
	{0x377a, 0x48},
	{0x377b, 0x4f},
	{0x37fa, 0x09},
	{0x37fb, 0x31},
	{0x37fc, 0x10},
	{0x37fd, 0x18},
	{0x3905, 0x8d},
	{0x391d, 0x08},
	{0x3922, 0x1a},
	{0x3926, 0x21},
	{0x3933, 0x80},
	{0x3934, 0x0d},
	{0x3937, 0x6a},
	{0x3939, 0x00},
	{0x393a, 0x0e},
	{0x39dc, 0x02},
	{0x3e00, 0x00},
	{0x3e01, 0xb9},
	{0x3e02, 0xc0},
	{0x3e03, 0x0b},
	{0x3e04, 0x0b},
	{0x3e05, 0xa0},
	{0x3e1b, 0x2a},
	{0x3e23, 0x00},
	{0x3e24, 0xbf},
	{0x4407, 0x34},
	{0x440e, 0x02},
	{0x4509, 0x10},
	{0x4816, 0x71},
	{0x5001, 0x40},
	{0x5007, 0x80},
	{0x36e9, 0x24},
	{0x37f9, 0x24},
	{0x3251, 0x90},

	/* strong signal */
	{0x3650, 0x33},
	{0x3651, 0x7f},

	{0x3028, 0x05},
	{REG_NULL, 0x00},
};

static const struct sc301iot_mode supported_modes[] = {
	{
		.width = 2048,
		.height = 1536,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x638,
		.hts_def = 0x8ca,
		.vts_def = 0x640,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc301iot_linear_10_2048x1536_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}, {
		.width = 2048,
		.height = 1536,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0xb9c,
		.hts_def = 0x8ca,
		.vts_def = 0xc80,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc301iot_hdr_10_2048x1536_regs,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	}, {
		.width = 1536,
		.height = 1536,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x96,
		.hts_def = 0x8ca,
		.vts_def = 0x640,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc301iot_linear_10_1536x1536_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}, {
		.width = 1536,
		.height = 1536,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0xb9c,
		.hts_def = 0x8ca,
		.vts_def = 0xc80,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc301iot_hdr_10_1536x1536_regs,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
};

static const s64 link_freq_menu_items[] = {
	SC301IOT_LINK_FREQ_594
};

static const char * const sc301iot_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int sc301iot_write_reg(struct i2c_client *client, u16 reg,
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

static int sc301iot_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = sc301iot_write_reg(client, regs[i].addr,
					SC301IOT_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int sc301iot_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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




/* mode: 0 = lgain  1 = sgain */
static int sc301iot_set_gain_reg(struct sc301iot *sc301iot, u32 gain, int mode)
{
	u8 ANA_Coarse_gain_reg = 0x00, DIG_Fine_gain_reg = 0x80;
	u32 ANA_Coarse_gain = 1024, DIG_gain_reg = 0x00;
	int ret = 0;


	gain = gain * 16;
	if (gain <= 1024)
		gain = 1024;
	else if (gain > SC301IOT_GAIN_MAX * 16)
		gain = SC301IOT_GAIN_MAX * 16;

	if (gain < 1606) {               // start Ana again
		ANA_Coarse_gain = 1024;
		ANA_Coarse_gain_reg = 0x00;
	} else if (gain < 3397) {
		ANA_Coarse_gain = 1606;
		ANA_Coarse_gain_reg = 0x40;
	} else if (gain < 6426) {
		ANA_Coarse_gain = 3397;
		ANA_Coarse_gain_reg = 0x48;
	} else if (gain < 12853) {
		ANA_Coarse_gain = 6426;
		ANA_Coarse_gain_reg = 0x49;
	} else if (gain < 25706) {
		ANA_Coarse_gain = 12853;
		ANA_Coarse_gain_reg = 0x4b;
	} else if (gain < 51412) {
		ANA_Coarse_gain = 25706;
		ANA_Coarse_gain_reg = 0x4f;
	} else {
		ANA_Coarse_gain = 51412;
		ANA_Coarse_gain_reg = 0x5f;
	}
	gain = gain * 1024 / ANA_Coarse_gain;	// start Dig again
	if (gain <= 1024)
		gain = 1024;
	else if (gain >= 2031)
		gain = 2031;
	DIG_Fine_gain_reg = gain/8;

	if (mode == SC301IOT_LGAIN) {
		ret = sc301iot_write_reg(sc301iot->client,
					SC301IOT_REG_DIG_GAIN,
					SC301IOT_REG_VALUE_08BIT,
					DIG_gain_reg & 0xF);
		ret |= sc301iot_write_reg(sc301iot->client,
					 SC301IOT_REG_DIG_FINE_GAIN,
					 SC301IOT_REG_VALUE_08BIT,
					 DIG_Fine_gain_reg);
		ret |= sc301iot_write_reg(sc301iot->client,
					 SC301IOT_REG_ANA_GAIN,
					 SC301IOT_REG_VALUE_08BIT,
					 ANA_Coarse_gain_reg);
	} else {
		ret = sc301iot_write_reg(sc301iot->client,
					SC301IOT_REG_SDIG_GAIN,
					SC301IOT_REG_VALUE_08BIT,
					DIG_gain_reg & 0xF);
		ret |= sc301iot_write_reg(sc301iot->client,
					 SC301IOT_REG_SDIG_FINE_GAIN,
					 SC301IOT_REG_VALUE_08BIT,
					 DIG_Fine_gain_reg);
		ret |= sc301iot_write_reg(sc301iot->client,
					 SC301IOT_REG_SANA_GAIN,
					 SC301IOT_REG_VALUE_08BIT,
					 ANA_Coarse_gain_reg);
	}
	return ret;
}

static int sc301iot_set_hdrae(struct sc301iot *sc301iot,
			    struct preisp_hdrae_exp_s *ae)
{
	int ret = 0;
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_a_gain, m_a_gain, s_a_gain;

	if (!sc301iot->has_init_exp && !sc301iot->streaming) {
		sc301iot->init_hdrae_exp = *ae;
		sc301iot->has_init_exp = true;
		dev_dbg(&sc301iot->client->dev, "sc301iot don't stream, record exp for hdr!\n");
		return ret;
	}
	l_exp_time = ae->long_exp_reg;
	m_exp_time = ae->middle_exp_reg;
	s_exp_time = ae->short_exp_reg;
	l_a_gain = ae->long_gain_reg;
	m_a_gain = ae->middle_gain_reg;
	s_a_gain = ae->short_gain_reg;

	dev_dbg(&sc301iot->client->dev,
		"rev exp req: L_exp: 0x%x, 0x%x, M_exp: 0x%x, 0x%x S_exp: 0x%x, 0x%x\n",
		l_exp_time, m_exp_time, s_exp_time,
		l_a_gain, m_a_gain, s_a_gain);

	if (sc301iot->cur_mode->hdr_mode == HDR_X2) {
		//2 stagger
		l_a_gain = m_a_gain;
		l_exp_time = m_exp_time;
	}

	//set exposure
	//l_exp_time = ae->long_exp_reg;
	//s_exp_time = ae->short_exp_reg;

	l_exp_time = l_exp_time * 2;
	s_exp_time = s_exp_time * 2;
	if (l_exp_time > 2998)                  //(3200 - 191 - 11)
		l_exp_time = 2998;
	if (s_exp_time > 182)                //(191 - 9)
		s_exp_time = 182;

	ret = sc301iot_write_reg(sc301iot->client,
				SC301IOT_REG_EXPOSURE_H,
				SC301IOT_REG_VALUE_08BIT,
				SC301IOT_FETCH_EXP_H(l_exp_time));
	ret |= sc301iot_write_reg(sc301iot->client,
				 SC301IOT_REG_EXPOSURE_M,
				 SC301IOT_REG_VALUE_08BIT,
				 SC301IOT_FETCH_EXP_M(l_exp_time));
	ret |= sc301iot_write_reg(sc301iot->client,
				 SC301IOT_REG_EXPOSURE_L,
				 SC301IOT_REG_VALUE_08BIT,
				 SC301IOT_FETCH_EXP_L(l_exp_time));
	ret |= sc301iot_write_reg(sc301iot->client,
				 SC301IOT_REG_SEXPOSURE_M,
				 SC301IOT_REG_VALUE_08BIT,
				 SC301IOT_FETCH_EXP_M(s_exp_time));
	ret |= sc301iot_write_reg(sc301iot->client,
				 SC301IOT_REG_SEXPOSURE_L,
				 SC301IOT_REG_VALUE_08BIT,
				 SC301IOT_FETCH_EXP_L(s_exp_time));


	ret |= sc301iot_set_gain_reg(sc301iot, l_a_gain, SC301IOT_LGAIN);
	ret |= sc301iot_set_gain_reg(sc301iot, s_a_gain, SC301IOT_SGAIN);
	return ret;
}

static int sc301iot_get_reso_dist(const struct sc301iot_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc301iot_mode *
sc301iot_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = sc301iot_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int sc301iot_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc301iot *sc301iot = to_sc301iot(sd);
	const struct sc301iot_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&sc301iot->mutex);

	mode = sc301iot_find_best_fit(fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc301iot->mutex);
		return -ENOTTY;
#endif
	} else {
		sc301iot->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc301iot->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc301iot->vblank, vblank_def,
					 SC301IOT_VTS_MAX - mode->height,
					 1, vblank_def);
		sc301iot->cur_fps = mode->max_fps;
		sc301iot->cur_vts = mode->vts_def;
	}

	mutex_unlock(&sc301iot->mutex);

	return 0;
}

static int sc301iot_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc301iot *sc301iot = to_sc301iot(sd);
	const struct sc301iot_mode *mode = sc301iot->cur_mode;

	mutex_lock(&sc301iot->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc301iot->mutex);
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
	mutex_unlock(&sc301iot->mutex);
	return 0;
}

static int sc301iot_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct sc301iot *sc301iot = to_sc301iot(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = sc301iot->cur_mode->bus_fmt;

	return 0;
}

static int sc301iot_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != supported_modes[0].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int sc301iot_enable_test_pattern(struct sc301iot *sc301iot, u32 pattern)
{
	u32 val = 0;
	int ret = 0;

	ret = sc301iot_read_reg(sc301iot->client, SC301IOT_REG_TEST_PATTERN,
			       SC301IOT_REG_VALUE_08BIT, &val);
	if (pattern)
		val |= SC301IOT_TEST_PATTERN_BIT_MASK;
	else
		val &= ~SC301IOT_TEST_PATTERN_BIT_MASK;

	ret |= sc301iot_write_reg(sc301iot->client, SC301IOT_REG_TEST_PATTERN,
				 SC301IOT_REG_VALUE_08BIT, val);
	return ret;
}

static int sc301iot_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct sc301iot *sc301iot = to_sc301iot(sd);
	const struct sc301iot_mode *mode = sc301iot->cur_mode;

	if (sc301iot->streaming)
		fi->interval = sc301iot->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

static int sc301iot_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				 struct v4l2_mbus_config *config)
{
	struct sc301iot *sc301iot = to_sc301iot(sd);
	const struct sc301iot_mode *mode = sc301iot->cur_mode;
	u32 val = 1 << (SC301IOT_LANES - 1) |
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

static void sc301iot_get_module_inf(struct sc301iot *sc301iot,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, SC301IOT_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, sc301iot->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, sc301iot->len_name, sizeof(inf->base.lens));
}

static int sc301iot_get_channel_info(struct sc301iot *sc301iot,
					    struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = sc301iot->cur_mode->vc[ch_info->index];
	ch_info->width = sc301iot->cur_mode->width;
	ch_info->height = sc301iot->cur_mode->height;
	ch_info->bus_fmt = sc301iot->cur_mode->bus_fmt;
	return 0;
}

static long sc301iot_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc301iot *sc301iot = to_sc301iot(sd);
	struct rkmodule_hdr_cfg *hdr;
	struct rkmodule_channel_info *ch_info;
	u32 i, h, w;
	long ret = 0;
	u32 stream = 0;
	u32 sync_mode = 4;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		sc301iot_get_module_inf(sc301iot, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = sc301iot->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = sc301iot->cur_mode->width;
		h = sc301iot->cur_mode->height;
		for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				sc301iot->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == ARRAY_SIZE(supported_modes)) {
			dev_err(&sc301iot->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = sc301iot->cur_mode->hts_def - sc301iot->cur_mode->width;
			h = sc301iot->cur_mode->vts_def - sc301iot->cur_mode->height;
			__v4l2_ctrl_modify_range(sc301iot->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(sc301iot->vblank,
				h,
				SC301IOT_VTS_MAX - sc301iot->cur_mode->height, 1, h);
			sc301iot->cur_fps = sc301iot->cur_mode->max_fps;
			sc301iot->cur_vts = sc301iot->cur_mode->vts_def;
		}
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		sc301iot_set_hdrae(sc301iot, arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = sc301iot_write_reg(sc301iot->client, SC301IOT_REG_CTRL_MODE,
				 SC301IOT_REG_VALUE_08BIT, SC301IOT_MODE_STREAMING);
		else
			ret = sc301iot_write_reg(sc301iot->client, SC301IOT_REG_CTRL_MODE,
				 SC301IOT_REG_VALUE_08BIT, SC301IOT_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_SYNC_MODE:
		*((u32 *)arg) = sc301iot->sync_mode;
		break;
	case RKMODULE_SET_SYNC_MODE:
		sync_mode = *((u32 *)arg);
		if (sync_mode > 3)
			break;
		sc301iot->sync_mode = sync_mode;
		dev_info(&sc301iot->client->dev, "sync_mode = [%u]\n", sc301iot->sync_mode);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = sc301iot_get_channel_info(sc301iot, ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc301iot_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = sc301iot_ioctl(sd, cmd, inf);
		if (!ret) {
			if (copy_to_user(up, inf, sizeof(*inf))) {
				kfree(inf);
				return -EFAULT;
			}
		}
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}
		if (copy_from_user(cfg, up, sizeof(*cfg))) {
			kfree(cfg);
			return -EFAULT;
		}
		ret = sc301iot_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc301iot_ioctl(sd, cmd, hdr);
		if (!ret) {
			if (copy_to_user(up, hdr, sizeof(*hdr))) {
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
		if (copy_from_user(hdr, up, sizeof(*hdr))) {
			kfree(hdr);
			return -EFAULT;
		}
		ret = sc301iot_ioctl(sd, cmd, hdr);
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
		ret = sc301iot_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;
		ret = sc301iot_ioctl(sd, cmd, &stream);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc301iot_ioctl(sd, cmd, ch_info);
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

static int sc301iot_s_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct sc301iot *sc301iot = to_sc301iot(sd);
	struct device *dev = sd->dev;
	int ret = -1;
	s64 vblank_def;
	u32 fps_set, current_fps;

	fps_set = DIV_ROUND_CLOSEST(fi->interval.denominator, fi->interval.numerator);
	dev_info(dev, "%s set fps = %u\n", __func__, fps_set);

	mutex_lock(&sc301iot->mutex);

	current_fps = DIV_ROUND_CLOSEST(sc301iot->cur_mode->max_fps.denominator,
			sc301iot->cur_mode->max_fps.numerator);
	vblank_def = sc301iot->cur_mode->vts_def * current_fps / fps_set -
						sc301iot->cur_mode->height;
	if (sc301iot->sync_mode == SLAVE_MODE)
		vblank_def -= 3;  // adjust vts
	ret = __v4l2_ctrl_s_ctrl(sc301iot->vblank, vblank_def);
	mutex_unlock(&sc301iot->mutex);
	if (ret < 0)
		dev_err(dev, "%s __v4l2_ctrl_s_ctrl error - %d\n", __func__, ret);
	return ret;
}

static int __sc301iot_start_stream(struct sc301iot *sc301iot)
{
	int ret;

	if (!sc301iot->is_thunderboot) {
		ret = sc301iot_write_array(sc301iot->client, sc301iot->cur_mode->reg_list);
		if (ret)
			return ret;

		/* In case these controls are set before streaming */
		ret = __v4l2_ctrl_handler_setup(&sc301iot->ctrl_handler);
		if (ret)
			return ret;
		if (sc301iot->has_init_exp && sc301iot->cur_mode->hdr_mode != NO_HDR) {
			ret = sc301iot_ioctl(&sc301iot->subdev, PREISP_CMD_SET_HDRAE_EXP,
					&sc301iot->init_hdrae_exp);
			if (ret) {
				dev_err(&sc301iot->client->dev,
					"init exp fail in hdr mode\n");
				return ret;
			}
		}

		if (sc301iot->sync_mode == SLAVE_MODE) {
			sc301iot_write_reg(sc301iot->client, 0x3222,
					SC301IOT_REG_VALUE_08BIT, 0x01);
			sc301iot_write_reg(sc301iot->client, 0x3223,
					SC301IOT_REG_VALUE_08BIT, 0xc8);
			sc301iot_write_reg(sc301iot->client, 0x3225,
					SC301IOT_REG_VALUE_08BIT, 0x10);
			sc301iot_write_reg(sc301iot->client, 0x322e,
					SC301IOT_REG_VALUE_08BIT, (sc301iot->cur_vts - 4) >> 8);
			sc301iot_write_reg(sc301iot->client, 0x322f,
					SC301IOT_REG_VALUE_08BIT, (sc301iot->cur_vts - 4) & 0xff);
		} else if (sc301iot->sync_mode == NO_SYNC_MODE) {
			sc301iot_write_reg(sc301iot->client, 0x3222,
						SC301IOT_REG_VALUE_08BIT, 0x00);
			sc301iot_write_reg(sc301iot->client, 0x3223,
						SC301IOT_REG_VALUE_08BIT, 0xd0);
			sc301iot_write_reg(sc301iot->client, 0x3225,
						SC301IOT_REG_VALUE_08BIT, 0x00);
			sc301iot_write_reg(sc301iot->client, 0x322e,
						SC301IOT_REG_VALUE_08BIT, 0x00);
			sc301iot_write_reg(sc301iot->client, 0x322f,
						SC301IOT_REG_VALUE_08BIT, 0x02);
		}
	}

	dev_dbg(&sc301iot->client->dev, "start stream\n");
	return sc301iot_write_reg(sc301iot->client, SC301IOT_REG_CTRL_MODE,
				 SC301IOT_REG_VALUE_08BIT, SC301IOT_MODE_STREAMING);
}

static int __sc301iot_stop_stream(struct sc301iot *sc301iot)
{
	sc301iot->has_init_exp = false;
	dev_dbg(&sc301iot->client->dev, "stop stream\n");
	if (sc301iot->is_thunderboot) {
		sc301iot->is_first_streamoff = true;
		pm_runtime_put(&sc301iot->client->dev);
	}
	return sc301iot_write_reg(sc301iot->client, SC301IOT_REG_CTRL_MODE,
				 SC301IOT_REG_VALUE_08BIT, SC301IOT_MODE_SW_STANDBY);
}

static int __sc301iot_power_on(struct sc301iot *sc301iot);
static int sc301iot_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc301iot *sc301iot = to_sc301iot(sd);
	struct i2c_client *client = sc301iot->client;
	int ret = 0;

	mutex_lock(&sc301iot->mutex);
	on = !!on;
	if (on == sc301iot->streaming)
		goto unlock_and_return;

	if (on) {
		if (sc301iot->is_thunderboot && rkisp_tb_get_state() == RKISP_TB_NG) {
			sc301iot->is_thunderboot = false;
			__sc301iot_power_on(sc301iot);
		}

		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __sc301iot_start_stream(sc301iot);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc301iot_stop_stream(sc301iot);
		pm_runtime_put(&client->dev);
	}

	sc301iot->streaming = on;

unlock_and_return:
	mutex_unlock(&sc301iot->mutex);

	return ret;
}

static int sc301iot_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc301iot *sc301iot = to_sc301iot(sd);
	struct i2c_client *client = sc301iot->client;
	int ret = 0;

	mutex_lock(&sc301iot->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc301iot->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		if (!sc301iot->is_thunderboot) {
			ret = sc301iot_write_array(sc301iot->client, sc301iot_global_regs);
			if (ret) {
				v4l2_err(sd, "could not set init registers\n");
				pm_runtime_put_noidle(&client->dev);
				goto unlock_and_return;
			}
		}

		sc301iot->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc301iot->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc301iot->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 sc301iot_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, SC301IOT_XVCLK_FREQ / 1000 / 1000);
}

static int __sc301iot_power_on(struct sc301iot *sc301iot)
{
	int ret;
	u32 delay_us;
	struct device *dev = &sc301iot->client->dev;

	if (!IS_ERR_OR_NULL(sc301iot->pins_default)) {
		ret = pinctrl_select_state(sc301iot->pinctrl,
					   sc301iot->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(sc301iot->xvclk, SC301IOT_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(sc301iot->xvclk) != SC301IOT_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc301iot->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		goto disable_clk;
	}
	if (sc301iot->is_thunderboot)
		return 0;
	if (!IS_ERR(sc301iot->reset_gpio))
		gpiod_set_value_cansleep(sc301iot->reset_gpio, 0);

	ret = regulator_bulk_enable(SC301IOT_NUM_SUPPLIES, sc301iot->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}
	if (!IS_ERR(sc301iot->reset_gpio))
		gpiod_set_value_cansleep(sc301iot->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(sc301iot->pwdn_gpio))
		gpiod_set_value_cansleep(sc301iot->pwdn_gpio, 1);
	usleep_range(4500, 5000);

	if (!IS_ERR(sc301iot->reset_gpio))
		usleep_range(6000, 8000);
	else
		usleep_range(12000, 16000);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = sc301iot_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(sc301iot->xvclk);

	return ret;
}

static void __sc301iot_power_off(struct sc301iot *sc301iot)
{
	int ret;
	struct device *dev = &sc301iot->client->dev;

	clk_disable_unprepare(sc301iot->xvclk);
	if (sc301iot->is_thunderboot) {
		if (sc301iot->is_first_streamoff) {
			sc301iot->is_thunderboot = false;
			sc301iot->is_first_streamoff = false;
		} else {
			return;
		}
	}

	if (!IS_ERR(sc301iot->pwdn_gpio))
		gpiod_set_value_cansleep(sc301iot->pwdn_gpio, 0);
	if (!IS_ERR(sc301iot->reset_gpio))
		gpiod_set_value_cansleep(sc301iot->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(sc301iot->pins_sleep)) {
		ret = pinctrl_select_state(sc301iot->pinctrl,
					   sc301iot->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(SC301IOT_NUM_SUPPLIES, sc301iot->supplies);
}

static int sc301iot_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc301iot *sc301iot = to_sc301iot(sd);

	return __sc301iot_power_on(sc301iot);
}

static int sc301iot_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc301iot *sc301iot = to_sc301iot(sd);

	__sc301iot_power_off(sc301iot);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc301iot_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc301iot *sc301iot = to_sc301iot(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc301iot_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc301iot->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc301iot->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sc301iot_enum_frame_interval(struct v4l2_subdev *sd,
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

static const struct dev_pm_ops sc301iot_pm_ops = {
	SET_RUNTIME_PM_OPS(sc301iot_runtime_suspend,
			   sc301iot_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc301iot_internal_ops = {
	.open = sc301iot_open,
};
#endif

static const struct v4l2_subdev_core_ops sc301iot_core_ops = {
	.s_power = sc301iot_s_power,
	.ioctl = sc301iot_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc301iot_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc301iot_video_ops = {
	.s_stream = sc301iot_s_stream,
	.g_frame_interval = sc301iot_g_frame_interval,
	.s_frame_interval = sc301iot_s_frame_interval,
};

static const struct v4l2_subdev_pad_ops sc301iot_pad_ops = {
	.enum_mbus_code = sc301iot_enum_mbus_code,
	.enum_frame_size = sc301iot_enum_frame_sizes,
	.enum_frame_interval = sc301iot_enum_frame_interval,
	.get_fmt = sc301iot_get_fmt,
	.set_fmt = sc301iot_set_fmt,
	.get_mbus_config = sc301iot_g_mbus_config,
};

static const struct v4l2_subdev_ops sc301iot_subdev_ops = {
	.core	= &sc301iot_core_ops,
	.video	= &sc301iot_video_ops,
	.pad	= &sc301iot_pad_ops,
};

static void sc301iot_modify_fps_info(struct sc301iot *sc301iot)
{
	const struct sc301iot_mode *mode = sc301iot->cur_mode;

	sc301iot->cur_fps.denominator = mode->max_fps.denominator * mode->vts_def /
					sc301iot->cur_vts;
}

static int sc301iot_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc301iot *sc301iot = container_of(ctrl->handler,
					       struct sc301iot, ctrl_handler);
	struct i2c_client *client = sc301iot->client;
	s64 max;
	int ret = 0;
	u32 val = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc301iot->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(sc301iot->exposure,
					 sc301iot->exposure->minimum, max,
					 sc301iot->exposure->step,
					 sc301iot->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (sc301iot->cur_mode->hdr_mode == NO_HDR) {
			ctrl->val = ctrl->val;
			/* 4 least significant bits of expsoure are fractional part */
			ret = sc301iot_write_reg(sc301iot->client,
						SC301IOT_REG_EXPOSURE_H,
						SC301IOT_REG_VALUE_08BIT,
						SC301IOT_FETCH_EXP_H(ctrl->val));
			ret |= sc301iot_write_reg(sc301iot->client,
						 SC301IOT_REG_EXPOSURE_M,
						 SC301IOT_REG_VALUE_08BIT,
						 SC301IOT_FETCH_EXP_M(ctrl->val));
			ret |= sc301iot_write_reg(sc301iot->client,
						 SC301IOT_REG_EXPOSURE_L,
						 SC301IOT_REG_VALUE_08BIT,
						 SC301IOT_FETCH_EXP_L(ctrl->val));
		}
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (sc301iot->cur_mode->hdr_mode == NO_HDR)
			ret = sc301iot_set_gain_reg(sc301iot, ctrl->val, SC301IOT_LGAIN);
		break;
	case V4L2_CID_VBLANK:
		ret = sc301iot_write_reg(sc301iot->client,
					SC301IOT_REG_VTS_H,
					SC301IOT_REG_VALUE_08BIT,
					(ctrl->val + sc301iot->cur_mode->height)
					>> 8);
		ret |= sc301iot_write_reg(sc301iot->client,
					 SC301IOT_REG_VTS_L,
					 SC301IOT_REG_VALUE_08BIT,
					 (ctrl->val + sc301iot->cur_mode->height)
					 & 0xff);
		if (!ret)
			sc301iot->cur_vts = ctrl->val + sc301iot->cur_mode->height;
		sc301iot_modify_fps_info(sc301iot);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = sc301iot_enable_test_pattern(sc301iot, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = sc301iot_read_reg(sc301iot->client, SC301IOT_FLIP_MIRROR_REG,
				       SC301IOT_REG_VALUE_08BIT, &val);
		ret |= sc301iot_write_reg(sc301iot->client, SC301IOT_FLIP_MIRROR_REG,
					 SC301IOT_REG_VALUE_08BIT,
					 SC301IOT_FETCH_MIRROR(val, ctrl->val));
		break;
	case V4L2_CID_VFLIP:
		ret = sc301iot_read_reg(sc301iot->client, SC301IOT_FLIP_MIRROR_REG,
				       SC301IOT_REG_VALUE_08BIT, &val);
		ret |= sc301iot_write_reg(sc301iot->client, SC301IOT_FLIP_MIRROR_REG,
					 SC301IOT_REG_VALUE_08BIT,
					 SC301IOT_FETCH_FLIP(val, ctrl->val));
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc301iot_ctrl_ops = {
	.s_ctrl = sc301iot_set_ctrl,
};

static int sc301iot_initialize_controls(struct sc301iot *sc301iot)
{
	const struct sc301iot_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &sc301iot->ctrl_handler;
	mode = sc301iot->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &sc301iot->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, PIXEL_RATE_WITH_594M_10BIT, 1, PIXEL_RATE_WITH_594M_10BIT);

	h_blank = mode->hts_def - mode->width;
	sc301iot->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (sc301iot->hblank)
		sc301iot->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	vblank_def = mode->vts_def - mode->height;
	sc301iot->vblank = v4l2_ctrl_new_std(handler, &sc301iot_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    SC301IOT_VTS_MAX - mode->height,
					    1, vblank_def);
	exposure_max = mode->vts_def - 8;
	sc301iot->exposure = v4l2_ctrl_new_std(handler, &sc301iot_ctrl_ops,
					      V4L2_CID_EXPOSURE, SC301IOT_EXPOSURE_MIN,
					      exposure_max, SC301IOT_EXPOSURE_STEP,
					      mode->exp_def);
	sc301iot->anal_gain = v4l2_ctrl_new_std(handler, &sc301iot_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN, SC301IOT_GAIN_MIN,
					       SC301IOT_GAIN_MAX, SC301IOT_GAIN_STEP,
					       SC301IOT_GAIN_DEFAULT);
	sc301iot->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
							    &sc301iot_ctrl_ops,
					V4L2_CID_TEST_PATTERN,
					ARRAY_SIZE(sc301iot_test_pattern_menu) - 1,
					0, 0, sc301iot_test_pattern_menu);
	v4l2_ctrl_new_std(handler, &sc301iot_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(handler, &sc301iot_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&sc301iot->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	sc301iot->subdev.ctrl_handler = handler;
	sc301iot->has_init_exp = false;
	sc301iot->cur_fps = mode->max_fps;
	sc301iot->cur_vts = mode->vts_def;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int sc301iot_check_sensor_id(struct sc301iot *sc301iot,
				   struct i2c_client *client)
{
	struct device *dev = &sc301iot->client->dev;
	u32 id = 0;
	int ret;

	if (sc301iot->is_thunderboot) {
		dev_info(dev, "Enable thunderboot mode, skip sensor id check\n");
		return 0;
	}

	ret = sc301iot_read_reg(client, SC301IOT_REG_CHIP_ID,
			       SC301IOT_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected chip id(0x%04x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected chip id (0x%04x)\n", id);

	return 0;
}

static int sc301iot_configure_regulators(struct sc301iot *sc301iot)
{
	unsigned int i;

	for (i = 0; i < SC301IOT_NUM_SUPPLIES; i++)
		sc301iot->supplies[i].supply = SC301IOT_supply_names[i];

	return devm_regulator_bulk_get(&sc301iot->client->dev,
				       SC301IOT_NUM_SUPPLIES,
				       sc301iot->supplies);
}

static int sc301iot_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc301iot *sc301iot;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;
	const char *sync_mode_name = NULL;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	sc301iot = devm_kzalloc(dev, sizeof(*sc301iot), GFP_KERNEL);
	if (!sc301iot)
		return -ENOMEM;

	of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc301iot->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc301iot->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc301iot->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc301iot->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	sc301iot->is_thunderboot = IS_ENABLED(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP);
	sc301iot->sync_mode = NO_SYNC_MODE;
	ret = of_property_read_string(node, RKMODULE_CAMERA_SYNC_MODE, &sync_mode_name);
	if (!ret) {
		if (strcmp(sync_mode_name, RKMODULE_EXTERNAL_MASTER_MODE) == 0)
			sc301iot->sync_mode = EXTERNAL_MASTER_MODE;
		else if (strcmp(sync_mode_name, RKMODULE_INTERNAL_MASTER_MODE) == 0)
			sc301iot->sync_mode = INTERNAL_MASTER_MODE;
		else if (strcmp(sync_mode_name, RKMODULE_SLAVE_MODE) == 0)
			sc301iot->sync_mode = SLAVE_MODE;
	}

	switch (sc301iot->sync_mode) {
	default:
		sc301iot->sync_mode = NO_SYNC_MODE; break;
	case NO_SYNC_MODE:
		dev_info(dev, "sync_mode = [NO_SYNC_MODE]\n"); break;
	case EXTERNAL_MASTER_MODE:
	case INTERNAL_MASTER_MODE:
		dev_info(dev, "sync_mode = [MASTER_MODE]\n"); break;
	case SLAVE_MODE:
		dev_info(dev, "sync_mode = [SLAVE_MODE]\n"); break;
	}

	sc301iot->client = client;
	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			sc301iot->cur_mode = &supported_modes[i];
			break;
		}
	}
	if (i == ARRAY_SIZE(supported_modes))
		sc301iot->cur_mode = &supported_modes[0];

	sc301iot->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc301iot->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	if (sc301iot->is_thunderboot) {
		sc301iot->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
		if (IS_ERR(sc301iot->reset_gpio))
			dev_warn(dev, "Failed to get reset-gpios\n");

		sc301iot->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_ASIS);
		if (IS_ERR(sc301iot->pwdn_gpio))
			dev_warn(dev, "Failed to get pwdn-gpios\n");
	} else {
		sc301iot->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
		if (IS_ERR(sc301iot->reset_gpio))
			dev_warn(dev, "Failed to get reset-gpios\n");

		sc301iot->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
		if (IS_ERR(sc301iot->pwdn_gpio))
			dev_warn(dev, "Failed to get pwdn-gpios\n");
	}

	sc301iot->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc301iot->pinctrl)) {
		sc301iot->pins_default =
			pinctrl_lookup_state(sc301iot->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc301iot->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		sc301iot->pins_sleep =
			pinctrl_lookup_state(sc301iot->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc301iot->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = sc301iot_configure_regulators(sc301iot);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&sc301iot->mutex);

	sd = &sc301iot->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc301iot_subdev_ops);
	ret = sc301iot_initialize_controls(sc301iot);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc301iot_power_on(sc301iot);
	if (ret)
		goto err_free_handler;

	ret = sc301iot_check_sensor_id(sc301iot, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc301iot_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc301iot->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc301iot->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc301iot->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sc301iot->module_index, facing,
		 SC301IOT_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	if (sc301iot->is_thunderboot)
		pm_runtime_get_sync(dev);
	else
		pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__sc301iot_power_off(sc301iot);
err_free_handler:
	v4l2_ctrl_handler_free(&sc301iot->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc301iot->mutex);

	return ret;
}

static int sc301iot_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc301iot *sc301iot = to_sc301iot(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc301iot->ctrl_handler);
	mutex_destroy(&sc301iot->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc301iot_power_off(sc301iot);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc301iot_of_match[] = {
	{ .compatible = "smartsens,sc301iot" },
	{},
};
MODULE_DEVICE_TABLE(of, sc301iot_of_match);
#endif

static const struct i2c_device_id sc301iot_match_id[] = {
	{ "smartsens,sc301iot", 0 },
	{ },
};

static struct i2c_driver sc301iot_i2c_driver = {
	.driver = {
		.name = SC301IOT_NAME,
		.pm = &sc301iot_pm_ops,
		.of_match_table = of_match_ptr(sc301iot_of_match),
	},
	.probe		= &sc301iot_probe,
	.remove		= &sc301iot_remove,
	.id_table	= sc301iot_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc301iot_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc301iot_i2c_driver);
}

#if defined(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP) && !defined(CONFIG_INITCALL_ASYNC)
subsys_initcall(sensor_mod_init);
#else
device_initcall_sync(sensor_mod_init);
#endif
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("smartsens sc301iot sensor driver");
MODULE_LICENSE("GPL");
