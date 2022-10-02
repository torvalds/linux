// SPDX-License-Identifier: GPL-2.0
/*
 * sc530ai driver
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 fix set vflip/hflip failed bug.
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
#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x01)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define SC530AI_LINK_FREQ_396M		198000000 // 396Mbps
#define SC530AI_LINK_FREQ_792M		396000000 // 792Mbps
#define SC530AI_LINK_FREQ_792M_2LANE	396000000 // 792Mbps

#define SC530AI_LINEAR_PIXEL_RATES	(SC530AI_LINK_FREQ_396M / 10 * 2 * 4)
#define SC530AI_HDR_PIXEL_RATES		(SC530AI_LINK_FREQ_792M / 10 * 2 * 4)
#define SC530AI_MAX_PIXEL_RATE		(SC530AI_LINK_FREQ_792M / 10 * 2 * 4)
#define SC530AI_LINEAR_PIXEL_RATES_2LAN (SC530AI_LINK_FREQ_792M / 10 * 2 * 2)

#define SC530AI_XVCLK_FREQ		27000000

#define SC530AI_CHIP_ID			0x8e39
#define SC530AI_REG_CHIP_ID		0x3107

#define SC530AI_REG_CTRL_MODE		0x0100
#define SC530AI_MODE_SW_STANDBY		0x0
#define SC530AI_MODE_STREAMING		BIT(0)

#define SC530AI_REG_EXPOSURE_H		0x3e00
#define SC530AI_REG_EXPOSURE_M		0x3e01
#define SC530AI_REG_EXPOSURE_L		0x3e02

#define	SC530AI_EXPOSURE_MIN		2
#define	SC530AI_EXPOSURE_STEP		1

#define SC530AI_REG_DIG_GAIN		0x3e06
#define SC530AI_REG_DIG_FINE_GAIN	0x3e07
#define SC530AI_REG_ANA_GAIN		0x3e09

#define SC530AI_GAIN_MIN		0x20
#define SC530AI_GAIN_MAX		(32 * 326)
#define SC530AI_GAIN_STEP		1
#define SC530AI_GAIN_DEFAULT		0x20

#define SC530AI_REG_VTS_H		0x320e
#define SC530AI_REG_VTS_L		0x320f
#define SC530AI_VTS_MAX			0x7fff

#define SC530AI_SOFTWARE_RESET_REG	0x0103

// short frame exposure
#define SC530AI_REG_SHORT_EXPOSURE_H	0x3e22
#define SC530AI_REG_SHORT_EXPOSURE_M	0x3e04
#define SC530AI_REG_SHORT_EXPOSURE_L	0x3e05

#define SC530AI_REG_MAX_SHORT_EXP_H	0x3e23
#define SC530AI_REG_MAX_SHORT_EXP_L	0x3e24

#define SC530AI_HDR_EXPOSURE_MIN	5		// Half line exposure time
#define SC530AI_HDR_EXPOSURE_STEP	4		// Half line exposure time

#define SC530AI_MAX_SHORT_EXPOSURE	608

// short frame gain
#define SC530AI_REG_SDIG_GAIN		0x3e10
#define SC530AI_REG_SDIG_FINE_GAIN	0x3e11
#define SC530AI_REG_SANA_GAIN		0x3e13

//group hold
#define SC530AI_GROUP_UPDATE_ADDRESS	0x3812
#define SC530AI_GROUP_UPDATE_START_DATA	0x00
#define SC530AI_GROUP_UPDATE_LAUNCH	0x30

#define SC530AI_FLIP_MIRROR_REG		0x3221
#define SC530AI_FLIP_MASK		0x60
#define SC530AI_MIRROR_MASK		0x06

#define REG_NULL			0xFFFF

#define SC530AI_REG_VALUE_08BIT		1
#define SC530AI_REG_VALUE_16BIT		2
#define SC530AI_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define SC530AI_NAME			"sc530ai"

#define SC530AI_FETCH_EXP_H(VAL)		(((VAL) >> 12) & 0xF)
#define SC530AI_FETCH_EXP_M(VAL)		(((VAL) >> 4) & 0xFF)
#define SC530AI_FETCH_EXP_L(VAL)		(((VAL) & 0xF) << 4)

static const char * const sc530ai_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define sc530ai_NUM_SUPPLIES ARRAY_SIZE(sc530ai_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct sc530ai_mode {
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

struct sc530ai {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[sc530ai_NUM_SUPPLIES];

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;
	struct v4l2_fract	cur_fps;
	u32			cur_vts;
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
	const struct sc530ai_mode *support_modes;
	const struct sc530ai_mode *cur_mode;
	u32			support_modes_num;
	unsigned int		lane_num;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
};

#define to_sc530ai(sd) container_of(sd, struct sc530ai, subdev)

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 1008Mbps, 4lane
 */
static const struct regval sc530ai_linear_10_30fps_2880x1620_4lane_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x37f9, 0x80},
	{0x301f, 0x01},
	{0x3250, 0x40},
	{0x3251, 0x98},
	{0x3253, 0x0c},
	{0x325f, 0x20},
	{0x3301, 0x08},
	{0x3304, 0x50},
	{0x3306, 0x78},
	{0x3308, 0x14},
	{0x3309, 0x70},
	{0x330a, 0x00},
	{0x330b, 0xd8},
	{0x330d, 0x10},
	{0x331e, 0x41},
	{0x331f, 0x61},
	{0x3333, 0x10},
	{0x335d, 0x60},
	{0x335e, 0x06},
	{0x335f, 0x08},
	{0x3364, 0x56},
	{0x3366, 0x01},
	{0x337c, 0x02},
	{0x337d, 0x0a},
	{0x3390, 0x01},
	{0x3391, 0x03},
	{0x3392, 0x07},
	{0x3393, 0x08},
	{0x3394, 0x08},
	{0x3395, 0x08},
	{0x3396, 0x40},
	{0x3397, 0x48},
	{0x3398, 0x4b},
	{0x3399, 0x08},
	{0x339a, 0x08},
	{0x339b, 0x08},
	{0x339c, 0x1d},
	{0x33a2, 0x04},
	{0x33ae, 0x30},
	{0x33af, 0x50},
	{0x33b1, 0x80},
	{0x33b2, 0x48},
	{0x33b3, 0x30},
	{0x349f, 0x02},
	{0x34a6, 0x48},
	{0x34a7, 0x49},
	{0x34a8, 0x40},
	{0x34a9, 0x30},
	{0x34f8, 0x4b},
	{0x34f9, 0x30},
	{0x3632, 0x48},
	{0x3633, 0x32},
	{0x3637, 0x29},
	{0x3638, 0xc1},
	{0x363b, 0x20},
	{0x363d, 0x02},
	{0x3670, 0x09},
	{0x3674, 0x8b},
	{0x3675, 0xc6},
	{0x3676, 0x8b},
	{0x367c, 0x40},
	{0x367d, 0x48},
	{0x3690, 0x32},
	{0x3691, 0x43},
	{0x3692, 0x33},
	{0x3693, 0x40},
	{0x3694, 0x4b},
	{0x3698, 0x85},
	{0x3699, 0x8f},
	{0x369a, 0xa0},
	{0x369b, 0xc3},
	{0x36a2, 0x49},
	{0x36a3, 0x4b},
	{0x36a4, 0x4f},
	{0x36d0, 0x01},
	{0x36ec, 0x13},
	{0x370f, 0x01},
	{0x3722, 0x00},
	{0x3728, 0x10},
	{0x37b0, 0x03},
	{0x37b1, 0x03},
	{0x37b2, 0x83},
	{0x37b3, 0x48},
	{0x37b4, 0x49},
	{0x37fb, 0x25},
	{0x37fc, 0x01},
	{0x3901, 0x00},
	{0x3902, 0xc5},
	{0x3904, 0x08},
	{0x3905, 0x8c},
	{0x3909, 0x00},
	{0x391d, 0x04},
	{0x391f, 0x44},
	{0x3926, 0x21},
	{0x3929, 0x18},
	{0x3933, 0x81},
	{0x3934, 0x81},
	{0x3937, 0x69},
	{0x3939, 0x00},
	{0x393a, 0x00},
	{0x39dc, 0x02},
	{0x3e01, 0xcd},
	{0x3e02, 0xa0},
	{0x440e, 0x02},
	{0x4509, 0x20},
	{0x4800, 0x04},
	{0x4837, 0x28},
	{0x5010, 0x10},
	{0x5799, 0x06},
	{0x57ad, 0x00},
	{0x5ae0, 0xfe},
	{0x5ae1, 0x40},
	{0x5ae2, 0x30},
	{0x5ae3, 0x2a},
	{0x5ae4, 0x24},
	{0x5ae5, 0x30},
	{0x5ae6, 0x2a},
	{0x5ae7, 0x24},
	{0x5ae8, 0x3c},
	{0x5ae9, 0x30},
	{0x5aea, 0x28},
	{0x5aeb, 0x3c},
	{0x5aec, 0x30},
	{0x5aed, 0x28},
	{0x5aee, 0xfe},
	{0x5aef, 0x40},
	{0x5af4, 0x30},
	{0x5af5, 0x2a},
	{0x5af6, 0x24},
	{0x5af7, 0x30},
	{0x5af8, 0x2a},
	{0x5af9, 0x24},
	{0x5afa, 0x3c},
	{0x5afb, 0x30},
	{0x5afc, 0x28},
	{0x5afd, 0x3c},
	{0x5afe, 0x30},
	{0x5aff, 0x28},
	{0x36e9, 0x44},
	{0x37f9, 0x34},
	//{0x0100, 0x01},

	{REG_NULL, 0x00},
};

static const struct regval sc530ai_hdr_10_30fps_2880x1620_4lane_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x37f9, 0x80},
	{0x301f, 0x03},
	{0x320e, 0x0c},
	{0x320f, 0xe4},
	{0x3250, 0xff},
	{0x3251, 0x98},
	{0x3253, 0x0c},
	{0x325f, 0x20},
	{0x3281, 0x01},
	{0x3301, 0x08},
	{0x3304, 0x58},
	{0x3306, 0xa0},
	{0x3308, 0x14},
	{0x3309, 0x50},
	{0x330a, 0x01},
	{0x330b, 0x10},
	{0x330d, 0x10},
	{0x331e, 0x49},
	{0x331f, 0x41},
	{0x3333, 0x10},
	{0x335d, 0x60},
	{0x335e, 0x06},
	{0x335f, 0x08},
	{0x3364, 0x56},
	{0x3366, 0x01},
	{0x337c, 0x02},
	{0x337d, 0x0a},
	{0x3390, 0x01},
	{0x3391, 0x03},
	{0x3392, 0x07},
	{0x3393, 0x08},
	{0x3394, 0x08},
	{0x3395, 0x08},
	{0x3396, 0x48},
	{0x3397, 0x4b},
	{0x3398, 0x4f},
	{0x3399, 0x0a},
	{0x339a, 0x0a},
	{0x339b, 0x10},
	{0x339c, 0x22},
	{0x33a2, 0x04},
	{0x33ad, 0x24},
	{0x33ae, 0x38},
	{0x33af, 0x38},
	{0x33b1, 0x80},
	{0x33b2, 0x48},
	{0x33b3, 0x30},
	{0x349f, 0x02},
	{0x34a6, 0x48},
	{0x34a7, 0x4b},
	{0x34a8, 0x20},
	{0x34a9, 0x18},
	{0x34f8, 0x5f},
	{0x34f9, 0x04},
	{0x3632, 0x48},
	{0x3633, 0x32},
	{0x3637, 0x29},
	{0x3638, 0xc1},
	{0x363b, 0x20},
	{0x363d, 0x02},
	{0x3670, 0x09},
	{0x3674, 0x88},
	{0x3675, 0x88},
	{0x3676, 0x88},
	{0x367c, 0x40},
	{0x367d, 0x48},
	{0x3690, 0x33},
	{0x3691, 0x34},
	{0x3692, 0x55},
	{0x3693, 0x4b},
	{0x3694, 0x4f},
	{0x3698, 0x85},
	{0x3699, 0x8f},
	{0x369a, 0xa0},
	{0x369b, 0xc3},
	{0x36a2, 0x49},
	{0x36a3, 0x4b},
	{0x36a4, 0x4f},
	{0x36d0, 0x01},
	{0x370f, 0x01},
	{0x3722, 0x00},
	{0x3728, 0x10},
	{0x37b0, 0x03},
	{0x37b1, 0x03},
	{0x37b2, 0x83},
	{0x37b3, 0x48},
	{0x37b4, 0x4f},
	{0x3901, 0x00},
	{0x3902, 0xc5},
	{0x3904, 0x08},
	{0x3905, 0x8d},
	{0x3909, 0x00},
	{0x391d, 0x04},
	{0x3926, 0x21},
	{0x3929, 0x18},
	{0x3933, 0x83},
	{0x3934, 0x02},
	{0x3937, 0x71},
	{0x3939, 0x00},
	{0x393a, 0x00},
	{0x39dc, 0x02},
	{0x3c0f, 0x00},
	{0x3e00, 0x01},
	{0x3e01, 0x82},
	{0x3e02, 0x00},
	{0x3e04, 0x18},
	{0x3e05, 0x20},
	{0x3e23, 0x00},
	{0x3e24, 0xc8},
	{0x440e, 0x02},
	{0x4509, 0x20},
	{0x4800, 0x04},
	{0x4816, 0x11},
	{0x5010, 0x10},
	{0x5799, 0x06},
	{0x57ad, 0x00},
	{0x5ae0, 0xfe},
	{0x5ae1, 0x40},
	{0x5ae2, 0x30},
	{0x5ae3, 0x2a},
	{0x5ae4, 0x24},
	{0x5ae5, 0x30},
	{0x5ae6, 0x2a},
	{0x5ae7, 0x24},
	{0x5ae8, 0x3c},
	{0x5ae9, 0x30},
	{0x5aea, 0x28},
	{0x5aeb, 0x3c},
	{0x5aec, 0x30},
	{0x5aed, 0x28},
	{0x5aee, 0xfe},
	{0x5aef, 0x40},
	{0x5af4, 0x30},
	{0x5af5, 0x2a},
	{0x5af6, 0x24},
	{0x5af7, 0x30},
	{0x5af8, 0x2a},
	{0x5af9, 0x24},
	{0x5afa, 0x3c},
	{0x5afb, 0x30},
	{0x5afc, 0x28},
	{0x5afd, 0x3c},
	{0x5afe, 0x30},
	{0x5aff, 0x28},
	{0x36e9, 0x44},
	{0x37f9, 0x44},
	//{0x0100, 0x01},

	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 1008Mbps, 2lane
 */
static const struct regval sc530ai_10_30fps_2880x1620_2lane_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x37f9, 0x80},
	{0x3018, 0x32},
	{0x3019, 0x0c},
	{0x301f, 0x18},
	{0x3250, 0x40},
	{0x3251, 0x98},
	{0x3253, 0x0c},
	{0x325f, 0x20},
	{0x3301, 0x08},
	{0x3304, 0x50},
	{0x3306, 0x78},
	{0x3308, 0x14},
	{0x3309, 0x70},
	{0x330a, 0x00},
	{0x330b, 0xd8},
	{0x330d, 0x10},
	{0x331e, 0x41},
	{0x331f, 0x61},
	{0x3333, 0x10},
	{0x335d, 0x60},
	{0x335e, 0x06},
	{0x335f, 0x08},
	{0x3364, 0x56},
	{0x3366, 0x01},
	{0x337c, 0x02},
	{0x337d, 0x0a},
	{0x3390, 0x01},
	{0x3391, 0x03},
	{0x3392, 0x07},
	{0x3393, 0x08},
	{0x3394, 0x08},
	{0x3395, 0x08},
	{0x3396, 0x40},
	{0x3397, 0x48},
	{0x3398, 0x4b},
	{0x3399, 0x08},
	{0x339a, 0x08},
	{0x339b, 0x08},
	{0x339c, 0x1d},
	{0x33a2, 0x04},
	{0x33ae, 0x30},
	{0x33af, 0x50},
	{0x33b1, 0x80},
	{0x33b2, 0x80},
	{0x33b3, 0x40},
	{0x349f, 0x02},
	{0x34a6, 0x48},
	{0x34a7, 0x49},
	{0x34a8, 0x40},
	{0x34a9, 0x30},
	{0x34f8, 0x4b},
	{0x34f9, 0x30},
	{0x3632, 0x48},
	{0x3633, 0x32},
	{0x3637, 0x2b},
	{0x3638, 0xc1},
	{0x363b, 0x20},
	{0x363d, 0x02},
	{0x3670, 0x09},
	{0x3674, 0x8b},
	{0x3675, 0xc6},
	{0x3676, 0x8b},
	{0x367c, 0x40},
	{0x367d, 0x48},
	{0x3690, 0x32},
	{0x3691, 0x32},
	{0x3692, 0x33},
	{0x3693, 0x40},
	{0x3694, 0x4b},
	{0x3698, 0x85},
	{0x3699, 0x8f},
	{0x369a, 0xa0},
	{0x369b, 0xc3},
	{0x36a2, 0x49},
	{0x36a3, 0x4b},
	{0x36a4, 0x4f},
	{0x36d0, 0x01},
	{0x36ec, 0x03},
	{0x370f, 0x01},
	{0x3722, 0x00},
	{0x3728, 0x10},
	{0x37b0, 0x03},
	{0x37b1, 0x03},
	{0x37b2, 0x83},
	{0x37b3, 0x48},
	{0x37b4, 0x49},
	{0x37fb, 0x25},
	{0x37fc, 0x01},
	{0x3901, 0x00},
	{0x3902, 0xc5},
	{0x3904, 0x08},
	{0x3905, 0x8c},
	{0x3909, 0x00},
	{0x391d, 0x04},
	{0x391f, 0x44},
	{0x3926, 0x21},
	{0x3929, 0x18},
	{0x3933, 0x81},
	{0x3934, 0x81},
	{0x3937, 0x69},
	{0x3939, 0x00},
	{0x393a, 0x00},
	{0x39dc, 0x02},
	{0x3e01, 0xcd},
	{0x3e02, 0xa0},
	{0x440e, 0x02},
	{0x4509, 0x20},
	{0x4800, 0x04},
	{0x4837, 0x14},
	{0x5010, 0x10},
	{0x5799, 0x06},
	{0x57ad, 0x00},
	{0x5ae0, 0xfe},
	{0x5ae1, 0x40},
	{0x5ae2, 0x30},
	{0x5ae3, 0x2a},
	{0x5ae4, 0x24},
	{0x5ae5, 0x30},
	{0x5ae6, 0x2a},
	{0x5ae7, 0x24},
	{0x5ae8, 0x3c},
	{0x5ae9, 0x30},
	{0x5aea, 0x28},
	{0x5aeb, 0x3c},
	{0x5aec, 0x30},
	{0x5aed, 0x28},
	{0x5aee, 0xfe},
	{0x5aef, 0x40},
	{0x5af4, 0x30},
	{0x5af5, 0x2a},
	{0x5af6, 0x24},
	{0x5af7, 0x30},
	{0x5af8, 0x2a},
	{0x5af9, 0x24},
	{0x5afa, 0x3c},
	{0x5afb, 0x30},
	{0x5afc, 0x28},
	{0x5afd, 0x3c},
	{0x5afe, 0x30},
	{0x5aff, 0x28},
	{0x36e9, 0x44},
	{0x37f9, 0x34},
//	{0x0100, 0x01},
	{REG_NULL, 0x00},
};

static const struct sc530ai_mode supported_modes_4lane[] = {
	{
		.width = 2880,
		.height = 1620,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0xcda / 2,
		.hts_def = 0xb40,
		.vts_def = 0x0672,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc530ai_linear_10_30fps_2880x1620_4lane_regs,
		.mipi_freq_idx = 0,
		.bpp = 10,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 2880,
		.height = 1620,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x1820 / 2,
		.hts_def = 0xb40,
		.vts_def = 0x0ce4,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc530ai_hdr_10_30fps_2880x1620_4lane_regs,
		.mipi_freq_idx = 1,
		.bpp = 10,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
};

static const struct sc530ai_mode supported_modes_2lane[] = {
{
		.width = 2880,
		.height = 1620,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0xcda / 2,
		.hts_def = 0xb40,
		.vts_def = 0x0672,
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.reg_list = sc530ai_10_30fps_2880x1620_2lane_regs,
		.mipi_freq_idx = 2,
		.bpp = 10,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_items[] = {
	SC530AI_LINK_FREQ_396M,
	SC530AI_LINK_FREQ_792M,
	SC530AI_LINK_FREQ_792M_2LANE,
};

/* Write registers up to 4 at a time */
static int sc530ai_write_reg(struct i2c_client *client, u16 reg,
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

static int sc530ai_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = sc530ai_write_reg(client, regs[i].addr,
					SC530AI_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int sc530ai_read_reg(struct i2c_client *client,
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

static int sc530ai_get_reso_dist(const struct sc530ai_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc530ai_mode *
sc530ai_find_best_fit(struct sc530ai *sc530ai, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < sc530ai->support_modes_num; i++) {
		dist = sc530ai_get_reso_dist(&sc530ai->support_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &sc530ai->support_modes[cur_best_fit];
}

static int sc530ai_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc530ai *sc530ai = to_sc530ai(sd);
	const struct sc530ai_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;

	mutex_lock(&sc530ai->mutex);

	mode = sc530ai_find_best_fit(sc530ai, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc530ai->mutex);
		return -ENOTTY;
#endif
	} else {
		sc530ai->cur_mode = mode;

		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc530ai->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc530ai->vblank, vblank_def,
					 SC530AI_VTS_MAX - mode->height,
					 1, vblank_def);

		__v4l2_ctrl_s_ctrl(sc530ai->link_freq, mode->mipi_freq_idx);
		pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] /
			     mode->bpp * 2 * sc530ai->lane_num;
		__v4l2_ctrl_s_ctrl_int64(sc530ai->pixel_rate, pixel_rate);
		sc530ai->cur_vts = mode->vts_def;
		sc530ai->cur_fps = mode->max_fps;
	}

	mutex_unlock(&sc530ai->mutex);

	return 0;
}

static int sc530ai_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct sc530ai *sc530ai = to_sc530ai(sd);
	const struct sc530ai_mode *mode = sc530ai->cur_mode;

	mutex_lock(&sc530ai->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc530ai->mutex);
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
	mutex_unlock(&sc530ai->mutex);

	return 0;
}

static int sc530ai_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct sc530ai *sc530ai = to_sc530ai(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = sc530ai->cur_mode->bus_fmt;

	return 0;
}

static int sc530ai_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	struct sc530ai *sc530ai = to_sc530ai(sd);

	if (fse->index >= sc530ai->support_modes_num)
		return -EINVAL;

	if (fse->code != sc530ai->support_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = sc530ai->support_modes[fse->index].width;
	fse->max_width  = sc530ai->support_modes[fse->index].width;
	fse->max_height = sc530ai->support_modes[fse->index].height;
	fse->min_height = sc530ai->support_modes[fse->index].height;

	return 0;
}

static int sc530ai_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct sc530ai *sc530ai = to_sc530ai(sd);
	const struct sc530ai_mode *mode = sc530ai->cur_mode;

	if (sc530ai->streaming)
		fi->interval = sc530ai->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

static int sc530ai_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				 struct v4l2_mbus_config *config)
{
	struct sc530ai *sc530ai = to_sc530ai(sd);
	const struct sc530ai_mode *mode = sc530ai->cur_mode;
	u32 val = 1 << (sc530ai->lane_num - 1) |
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

static void sc530ai_get_module_inf(struct sc530ai *sc530ai,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, SC530AI_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, sc530ai->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, sc530ai->len_name, sizeof(inf->base.lens));
}

static void sc530ai_get_gain_reg(u32 total_gain, u32 *again, u32 *dgain,
				u32 *dgain_fine)
{
	u32 gain_factor = 0;

	if (total_gain < SC530AI_GAIN_MIN)
		total_gain = SC530AI_GAIN_MIN;
	else if (total_gain > SC530AI_GAIN_MAX)
		total_gain = SC530AI_GAIN_MAX;

	gain_factor = total_gain * 1000 / 32;
	if (gain_factor < 2000) { /* 1 - 2x gain */
		*again = 0x00;
		*dgain = 0x00;
		*dgain_fine = gain_factor * 128 / 1000;
	} else if (gain_factor < 2550) { /* 2x - 2.55x gain */
		*again = 0x01;
		*dgain = 0x00;
		*dgain_fine = gain_factor * 128 / 2000;
	} else if (gain_factor < 2550 * 2) { /* 2.55x - 5.1x gain */
		*again = 0x40;
		*dgain = 0x00;
		*dgain_fine = gain_factor * 128 / 2550;
	} else if (gain_factor < 2550 * 4) { /* 5.1x - 10.2x gain */
		*again = 0x48;
		*dgain = 0x00;
		*dgain_fine = gain_factor * 128 / 5110;
	} else if (gain_factor < 2550 * 8) { /* 10.2x - 20.4x gain */
		*again = 0x49;
		*dgain = 0x00;
		*dgain_fine = gain_factor * 128 / 10200;
	} else if (gain_factor < 2550 * 16) { /* 20.4x - 40.8x gain */
		*again = 0x4B;
		*dgain = 0x00;
		*dgain_fine = gain_factor * 128 / 20400;
	} else if (gain_factor < 2550 * 32) { /* 40.8x - 81.6x gain */
		*again = 0x4f;
		*dgain = 0x00;
		*dgain_fine = gain_factor * 128 / 40800;
	} else if (gain_factor < 2550 * 64) { /* 81.6x - 163.2x gain */
		*again = 0x5f;
		*dgain = 0x00;
		*dgain_fine = gain_factor * 128 / 40800 / 2;
	} else if (gain_factor < 2550 * 128) { /* 163.2x - 326.4x gain */
		*again = 0x5f;
		*dgain = 0x01;
		*dgain_fine = gain_factor * 128 / 40800 / 4;
	}
}

static int sc530ai_set_hdrae(struct sc530ai *sc530ai,
			     struct preisp_hdrae_exp_s *ae)
{
	int ret = 0;
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_t_gain, m_t_gain, s_t_gain;
	u32 l_again = 0, l_dgain = 0, l_dgain_fine = 0;
	u32 s_again = 0, s_dgain = 0, s_dgain_fine = 0;

	if (!sc530ai->has_init_exp && !sc530ai->streaming) {
		sc530ai->init_hdrae_exp = *ae;
		sc530ai->has_init_exp = true;
		dev_dbg(&sc530ai->client->dev,
			"sc530ai don't stream, record exp for hdr!\n");
		return ret;
	}

	l_exp_time = ae->long_exp_reg;
	m_exp_time = ae->middle_exp_reg;
	s_exp_time = ae->short_exp_reg;
	l_t_gain = ae->long_gain_reg;
	m_t_gain = ae->middle_gain_reg;
	s_t_gain = ae->short_gain_reg;

	if (sc530ai->cur_mode->hdr_mode == HDR_X2) {
		//2 stagger
		l_t_gain = m_t_gain;
		l_exp_time = m_exp_time;
	}

	l_exp_time = l_exp_time << 1;
	s_exp_time = s_exp_time << 1;

	// set exposure reg
	ret |= sc530ai_write_reg(sc530ai->client,
				 SC530AI_REG_EXPOSURE_H,
				 SC530AI_REG_VALUE_08BIT,
				 SC530AI_FETCH_EXP_H(l_exp_time));
	ret |= sc530ai_write_reg(sc530ai->client,
				 SC530AI_REG_EXPOSURE_M,
				 SC530AI_REG_VALUE_08BIT,
				 SC530AI_FETCH_EXP_M(l_exp_time));
	ret |= sc530ai_write_reg(sc530ai->client,
				 SC530AI_REG_EXPOSURE_L,
				 SC530AI_REG_VALUE_08BIT,
				 SC530AI_FETCH_EXP_L(l_exp_time));
	ret |= sc530ai_write_reg(sc530ai->client,
				 SC530AI_REG_SHORT_EXPOSURE_H,
				 SC530AI_REG_VALUE_08BIT,
				 SC530AI_FETCH_EXP_H(s_exp_time));
	ret |= sc530ai_write_reg(sc530ai->client,
				 SC530AI_REG_SHORT_EXPOSURE_M,
				 SC530AI_REG_VALUE_08BIT,
				 SC530AI_FETCH_EXP_M(s_exp_time));
	ret |= sc530ai_write_reg(sc530ai->client,
				 SC530AI_REG_SHORT_EXPOSURE_L,
				 SC530AI_REG_VALUE_08BIT,
				 SC530AI_FETCH_EXP_L(s_exp_time));

	// set gain reg
	sc530ai_get_gain_reg(l_t_gain, &l_again, &l_dgain, &l_dgain_fine);
	sc530ai_get_gain_reg(s_t_gain, &s_again, &s_dgain, &s_dgain_fine);

	ret |= sc530ai_write_reg(sc530ai->client,
				 SC530AI_REG_DIG_GAIN,
				 SC530AI_REG_VALUE_08BIT,
				 l_dgain);
	ret |= sc530ai_write_reg(sc530ai->client,
				 SC530AI_REG_DIG_FINE_GAIN,
				 SC530AI_REG_VALUE_08BIT,
				 l_dgain_fine);
	ret |= sc530ai_write_reg(sc530ai->client,
				 SC530AI_REG_ANA_GAIN,
				 SC530AI_REG_VALUE_08BIT,
				 l_again);

	ret |= sc530ai_write_reg(sc530ai->client,
				 SC530AI_REG_SDIG_GAIN,
				 SC530AI_REG_VALUE_08BIT,
				 s_dgain);
	ret |= sc530ai_write_reg(sc530ai->client,
				 SC530AI_REG_SDIG_FINE_GAIN,
				 SC530AI_REG_VALUE_08BIT,
				 s_dgain_fine);
	ret |= sc530ai_write_reg(sc530ai->client,
				 SC530AI_REG_SANA_GAIN,
				 SC530AI_REG_VALUE_08BIT,
				 s_again);
	return ret;
}

static int sc530ai_get_channel_info(struct sc530ai *sc530ai, struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = sc530ai->cur_mode->vc[ch_info->index];
	ch_info->width = sc530ai->cur_mode->width;
	ch_info->height = sc530ai->cur_mode->height;
	ch_info->bus_fmt = sc530ai->cur_mode->bus_fmt;
	return 0;
}

static long sc530ai_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc530ai *sc530ai = to_sc530ai(sd);
	struct rkmodule_hdr_cfg *hdr;
	const struct sc530ai_mode *mode;
	struct rkmodule_channel_info *ch_info;

	long ret = 0;
	u32 i, h = 0, w;
	u32 stream = 0;
	int pixel_rate = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		sc530ai_get_module_inf(sc530ai, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = sc530ai->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = sc530ai->cur_mode->width;
		h = sc530ai->cur_mode->height;
		for (i = 0; i < sc530ai->support_modes_num; i++) {
			if (w == sc530ai->support_modes[i].width &&
				h == sc530ai->support_modes[i].height &&
				sc530ai->support_modes[i].hdr_mode == hdr->hdr_mode) {
				sc530ai->cur_mode = &sc530ai->support_modes[i];
				break;
			}
		}
		if (i == sc530ai->support_modes_num) {
			dev_err(&sc530ai->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			mode = sc530ai->cur_mode;
			w = sc530ai->cur_mode->hts_def -
					sc530ai->cur_mode->width;
			h = sc530ai->cur_mode->vts_def -
					sc530ai->cur_mode->height;
			__v4l2_ctrl_modify_range(sc530ai->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(sc530ai->vblank, h,
						 SC530AI_VTS_MAX -
						 sc530ai->cur_mode->height,
						 1, h);

			__v4l2_ctrl_s_ctrl(sc530ai->link_freq,
					   mode->mipi_freq_idx);

			pixel_rate = (int)link_freq_items[mode->mipi_freq_idx]
				     / mode->bpp * 2 * sc530ai->lane_num;

			__v4l2_ctrl_s_ctrl_int64(sc530ai->pixel_rate,
						 pixel_rate);
			sc530ai->cur_vts = mode->vts_def;
			sc530ai->cur_fps = mode->max_fps;
			dev_info(&sc530ai->client->dev, "sensor mode: %d\n",
				 sc530ai->cur_mode->hdr_mode);
		}
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		if (sc530ai->cur_mode->hdr_mode == HDR_X2)
			ret = sc530ai_set_hdrae(sc530ai, arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);
		if (stream)
			ret = sc530ai_write_reg(sc530ai->client,
						SC530AI_REG_CTRL_MODE,
						SC530AI_REG_VALUE_08BIT,
						SC530AI_MODE_STREAMING);
		else
			ret = sc530ai_write_reg(sc530ai->client,
						SC530AI_REG_CTRL_MODE,
						SC530AI_REG_VALUE_08BIT,
						SC530AI_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = sc530ai_get_channel_info(sc530ai, ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc530ai_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = sc530ai_ioctl(sd, cmd, inf);
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

		ret = sc530ai_ioctl(sd, cmd, hdr);
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

		ret = sc530ai_ioctl(sd, cmd, hdr);
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

			ret = sc530ai_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;

		ret = sc530ai_ioctl(sd, cmd, &stream);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc530ai_ioctl(sd, cmd, ch_info);
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

static int __sc530ai_start_stream(struct sc530ai *sc530ai)
{
	int ret;

	ret = sc530ai_write_array(sc530ai->client, sc530ai->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&sc530ai->ctrl_handler);
	if (ret)
		return ret;
	if (sc530ai->has_init_exp && sc530ai->cur_mode->hdr_mode != NO_HDR) {
		ret = sc530ai_ioctl(&sc530ai->subdev, PREISP_CMD_SET_HDRAE_EXP,
				    &sc530ai->init_hdrae_exp);
		if (ret) {
			dev_err(&sc530ai->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}
	return sc530ai_write_reg(sc530ai->client, SC530AI_REG_CTRL_MODE,
				 SC530AI_REG_VALUE_08BIT,
				 SC530AI_MODE_STREAMING);
}

static int __sc530ai_stop_stream(struct sc530ai *sc530ai)
{
	sc530ai->has_init_exp = false;
	return sc530ai_write_reg(sc530ai->client, SC530AI_REG_CTRL_MODE,
				 SC530AI_REG_VALUE_08BIT,
				 SC530AI_MODE_SW_STANDBY);
}

static int sc530ai_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc530ai *sc530ai = to_sc530ai(sd);
	struct i2c_client *client = sc530ai->client;
	int ret = 0;

	mutex_lock(&sc530ai->mutex);
	on = !!on;
	if (on == sc530ai->streaming)
		goto unlock_and_return;
	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		ret = __sc530ai_start_stream(sc530ai);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc530ai_stop_stream(sc530ai);
		pm_runtime_put(&client->dev);
	}

	sc530ai->streaming = on;

unlock_and_return:
	mutex_unlock(&sc530ai->mutex);

	return ret;
}

static int sc530ai_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc530ai *sc530ai = to_sc530ai(sd);
	struct i2c_client *client = sc530ai->client;
	int ret = 0;

	mutex_lock(&sc530ai->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc530ai->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret |= sc530ai_write_reg(sc530ai->client,
					 SC530AI_SOFTWARE_RESET_REG,
					 SC530AI_REG_VALUE_08BIT,
					 0x01);
		usleep_range(100, 200);

		sc530ai->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc530ai->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc530ai->mutex);

	return ret;
}

static int __sc530ai_power_on(struct sc530ai *sc530ai)
{
	int ret;
	struct device *dev = &sc530ai->client->dev;

	if (!IS_ERR_OR_NULL(sc530ai->pins_default)) {
		ret = pinctrl_select_state(sc530ai->pinctrl,
					   sc530ai->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(sc530ai->xvclk, SC530AI_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (27MHz)\n");
	if (clk_get_rate(sc530ai->xvclk) != SC530AI_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc530ai->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(sc530ai->reset_gpio))
		gpiod_set_value_cansleep(sc530ai->reset_gpio, 0);

	ret = regulator_bulk_enable(sc530ai_NUM_SUPPLIES, sc530ai->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(sc530ai->reset_gpio))
		gpiod_set_value_cansleep(sc530ai->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(sc530ai->pwdn_gpio))
		gpiod_set_value_cansleep(sc530ai->pwdn_gpio, 1);

	usleep_range(4000, 5000);
	return 0;

disable_clk:
	clk_disable_unprepare(sc530ai->xvclk);

	return ret;
}

static void __sc530ai_power_off(struct sc530ai *sc530ai)
{
	int ret;
	struct device *dev = &sc530ai->client->dev;

	if (!IS_ERR(sc530ai->pwdn_gpio))
		gpiod_set_value_cansleep(sc530ai->pwdn_gpio, 0);
	clk_disable_unprepare(sc530ai->xvclk);
	if (!IS_ERR(sc530ai->reset_gpio))
		gpiod_set_value_cansleep(sc530ai->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(sc530ai->pins_sleep)) {
		ret = pinctrl_select_state(sc530ai->pinctrl,
					   sc530ai->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(sc530ai_NUM_SUPPLIES, sc530ai->supplies);
}

static int sc530ai_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc530ai *sc530ai = to_sc530ai(sd);

	return __sc530ai_power_on(sc530ai);
}

static int sc530ai_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc530ai *sc530ai = to_sc530ai(sd);

	__sc530ai_power_off(sc530ai);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc530ai_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc530ai *sc530ai = to_sc530ai(sd);
	struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc530ai_mode *def_mode = &sc530ai->support_modes[0];

	mutex_lock(&sc530ai->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc530ai->mutex);
	/* No crop or compose */

	return 0;
}
#endif

#define DST_WIDTH 2880
#define DST_HEIGHT 1616

/*
 * The resolution of the driver configuration needs to be exactly
 * the same as the current output resolution of the sensor,
 * the input width of the isp needs to be 16 aligned,
 * the input height of the isp needs to be 8 aligned.
 * Can be cropped to standard resolution by this function,
 * otherwise it will crop out strange resolution according
 * to the alignment rules.
 */
static int sc530ai_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_selection *sel)
{
	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = 0;
		sel->r.width = DST_WIDTH;
		sel->r.top = 2;
		sel->r.height = DST_HEIGHT;
		return 0;
	}
	return -EINVAL;
}

static int sc530ai_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct sc530ai *sc530ai = to_sc530ai(sd);

	if (fie->index >= sc530ai->support_modes_num)
		return -EINVAL;

	fie->code = sc530ai->support_modes[fie->index].bus_fmt;
	fie->width = sc530ai->support_modes[fie->index].width;
	fie->height = sc530ai->support_modes[fie->index].height;
	fie->interval = sc530ai->support_modes[fie->index].max_fps;
	fie->reserved[0] = sc530ai->support_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops sc530ai_pm_ops = {
	SET_RUNTIME_PM_OPS(sc530ai_runtime_suspend,
	sc530ai_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc530ai_internal_ops = {
	.open = sc530ai_open,
};
#endif

static const struct v4l2_subdev_core_ops sc530ai_core_ops = {
	.s_power = sc530ai_s_power,
	.ioctl = sc530ai_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc530ai_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc530ai_video_ops = {
	.s_stream = sc530ai_s_stream,
	.g_frame_interval = sc530ai_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops sc530ai_pad_ops = {
	.enum_mbus_code = sc530ai_enum_mbus_code,
	.enum_frame_size = sc530ai_enum_frame_sizes,
	.enum_frame_interval = sc530ai_enum_frame_interval,
	.get_fmt = sc530ai_get_fmt,
	.set_fmt = sc530ai_set_fmt,
	.get_selection = sc530ai_get_selection,
	.get_mbus_config = sc530ai_g_mbus_config,
};

static const struct v4l2_subdev_ops sc530ai_subdev_ops = {
	.core	= &sc530ai_core_ops,
	.video	= &sc530ai_video_ops,
	.pad	= &sc530ai_pad_ops,
};

static void sc530ai_modify_fps_info(struct sc530ai *sc5330ai)
{
	const struct sc530ai_mode *mode = sc5330ai->cur_mode;

	sc5330ai->cur_fps.denominator = mode->max_fps.denominator * mode->vts_def /
					sc5330ai->cur_vts;
}

static int sc530ai_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc530ai *sc530ai = container_of(ctrl->handler,
					       struct sc530ai, ctrl_handler);
	struct i2c_client *client = sc530ai->client;
	s64 max;
	u32 again = 0, dgain = 0, dgain_fine = 0;
	int ret = 0;
	u32 val = 0, vts = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc530ai->cur_mode->height + ctrl->val - 5;
		__v4l2_ctrl_modify_range(sc530ai->exposure,
					 sc530ai->exposure->minimum, max,
					 sc530ai->exposure->step,
					 sc530ai->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (sc530ai->cur_mode->hdr_mode != NO_HDR)
			return ret;
		val = ctrl->val << 1;
		ret = sc530ai_write_reg(sc530ai->client,
					SC530AI_REG_EXPOSURE_H,
					SC530AI_REG_VALUE_08BIT,
					SC530AI_FETCH_EXP_H(val));
		ret |= sc530ai_write_reg(sc530ai->client,
					 SC530AI_REG_EXPOSURE_M,
					 SC530AI_REG_VALUE_08BIT,
					 SC530AI_FETCH_EXP_M(val));
		ret |= sc530ai_write_reg(sc530ai->client,
					 SC530AI_REG_EXPOSURE_L,
					 SC530AI_REG_VALUE_08BIT,
					 SC530AI_FETCH_EXP_L(val));

		dev_dbg(&client->dev, "set exposure 0x%x\n", val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (sc530ai->cur_mode->hdr_mode != NO_HDR)
			return ret;

		sc530ai_get_gain_reg(ctrl->val, &again, &dgain, &dgain_fine);
		ret = sc530ai_write_reg(sc530ai->client,
					SC530AI_REG_DIG_GAIN,
					SC530AI_REG_VALUE_08BIT,
					dgain);
		ret |= sc530ai_write_reg(sc530ai->client,
					 SC530AI_REG_DIG_FINE_GAIN,
					 SC530AI_REG_VALUE_08BIT,
					 dgain_fine);
		ret |= sc530ai_write_reg(sc530ai->client,
					 SC530AI_REG_ANA_GAIN,
					SC530AI_REG_VALUE_08BIT,
					 again);
		dev_dbg(&client->dev, "set gain 0x%x\n", ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		vts = ctrl->val + sc530ai->cur_mode->height;
		ret = sc530ai_write_reg(sc530ai->client,
					SC530AI_REG_VTS_H,
					SC530AI_REG_VALUE_08BIT,
					(vts >> 8) & 0x7f);
		ret |= sc530ai_write_reg(sc530ai->client,
					 SC530AI_REG_VTS_L,
					 SC530AI_REG_VALUE_08BIT,
					 vts & 0xff);
		if (!ret)
			sc530ai->cur_vts = vts;
		if (sc530ai->cur_vts != sc530ai->cur_mode->vts_def)
			sc530ai_modify_fps_info(sc530ai);
		dev_dbg(&client->dev, "set vblank 0x%x\n", ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = sc530ai_read_reg(sc530ai->client, SC530AI_FLIP_MIRROR_REG,
				       SC530AI_REG_VALUE_08BIT, &val);
		if (ret)
			break;

		if (ctrl->val)
			val |= SC530AI_MIRROR_MASK;
		else
			val &= ~SC530AI_MIRROR_MASK;
		ret |= sc530ai_write_reg(sc530ai->client,
					 SC530AI_FLIP_MIRROR_REG,
					 SC530AI_REG_VALUE_08BIT, val);
		break;
	case V4L2_CID_VFLIP:
		ret = sc530ai_read_reg(sc530ai->client,
				       SC530AI_FLIP_MIRROR_REG,
				       SC530AI_REG_VALUE_08BIT, &val);
		if (ret)
			break;

		if (ctrl->val)
			val |= SC530AI_FLIP_MASK;
		else
			val &= ~SC530AI_FLIP_MASK;
		ret |= sc530ai_write_reg(sc530ai->client,
					 SC530AI_FLIP_MIRROR_REG,
					 SC530AI_REG_VALUE_08BIT,
					 val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc530ai_ctrl_ops = {
	.s_ctrl = sc530ai_set_ctrl,
};

static int sc530ai_parse_of(struct sc530ai *sc530ai)
{
	struct device *dev = &sc530ai->client->dev;
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

	sc530ai->lane_num = rval;
	dev_info(dev, "lane_num = %d\n", sc530ai->lane_num);

	if (sc530ai->lane_num == 2) {
		sc530ai->support_modes = supported_modes_2lane;
		sc530ai->support_modes_num = ARRAY_SIZE(supported_modes_2lane);
	} else if (sc530ai->lane_num == 4) {
		sc530ai->support_modes = supported_modes_4lane;
		sc530ai->support_modes_num = ARRAY_SIZE(supported_modes_4lane);
	}

	sc530ai->cur_mode = &sc530ai->support_modes[0];

	return 0;
}

static int sc530ai_initialize_controls(struct sc530ai *sc530ai)
{
	const struct sc530ai_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u64 dst_pixel_rate = 0;
	u32 h_blank;
	int ret;

	handler = &sc530ai->ctrl_handler;
	mode = sc530ai->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &sc530ai->mutex;

	sc530ai->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ,
				ARRAY_SIZE(link_freq_items) - 1, 0,
				link_freq_items);
	__v4l2_ctrl_s_ctrl(sc530ai->link_freq, mode->mipi_freq_idx);

	if (mode->mipi_freq_idx == 0)
		dst_pixel_rate = SC530AI_LINEAR_PIXEL_RATES;
	else if (mode->mipi_freq_idx == 1)
		dst_pixel_rate = SC530AI_HDR_PIXEL_RATES;
	else if (mode->mipi_freq_idx == 2)
		dst_pixel_rate = SC530AI_LINEAR_PIXEL_RATES_2LAN;

	sc530ai->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
						V4L2_CID_PIXEL_RATE, 0,
						SC530AI_MAX_PIXEL_RATE,
						1, dst_pixel_rate);

	h_blank = mode->hts_def - mode->width;
	sc530ai->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (sc530ai->hblank)
		sc530ai->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	sc530ai->vblank = v4l2_ctrl_new_std(handler, &sc530ai_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    SC530AI_VTS_MAX - mode->height,
					    1, vblank_def);

	exposure_max = mode->vts_def - 5;
	sc530ai->exposure = v4l2_ctrl_new_std(handler, &sc530ai_ctrl_ops,
					      V4L2_CID_EXPOSURE,
					      SC530AI_EXPOSURE_MIN,
					      exposure_max,
					      SC530AI_EXPOSURE_STEP,
					      mode->exp_def);

	sc530ai->anal_gain = v4l2_ctrl_new_std(handler, &sc530ai_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN,
					       SC530AI_GAIN_MIN,
					       SC530AI_GAIN_MAX,
					       SC530AI_GAIN_STEP,
					       SC530AI_GAIN_DEFAULT);

	v4l2_ctrl_new_std(handler, &sc530ai_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(handler, &sc530ai_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&sc530ai->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}
	sc530ai->subdev.ctrl_handler = handler;
	sc530ai->has_init_exp = false;
	sc530ai->cur_vts = mode->vts_def;
	sc530ai->cur_fps = mode->max_fps;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);
	return ret;
}

static int sc530ai_check_sensor_id(struct sc530ai *sc530ai,
				   struct i2c_client *client)
{
	struct device *dev = &sc530ai->client->dev;
	u32 id = 0;
	int ret;

	ret = sc530ai_read_reg(client, SC530AI_REG_CHIP_ID,
			       SC530AI_REG_VALUE_16BIT, &id);
	if (id != SC530AI_CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected SC%06x sensor\n", SC530AI_CHIP_ID);

	return 0;
}

static int sc530ai_configure_regulators(struct sc530ai *sc530ai)
{
	unsigned int i;

	for (i = 0; i < sc530ai_NUM_SUPPLIES; i++)
		sc530ai->supplies[i].supply = sc530ai_supply_names[i];

	return devm_regulator_bulk_get(&sc530ai->client->dev,
				       sc530ai_NUM_SUPPLIES,
				       sc530ai->supplies);
}

static int sc530ai_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc530ai *sc530ai;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	sc530ai = devm_kzalloc(dev, sizeof(*sc530ai), GFP_KERNEL);
	if (!sc530ai)
		return -ENOMEM;

	of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc530ai->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc530ai->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc530ai->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc530ai->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	sc530ai->client = client;

	ret = sc530ai_parse_of(sc530ai);
	if (ret)
		return -EINVAL;

	sc530ai->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc530ai->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	sc530ai->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sc530ai->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	sc530ai->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(sc530ai->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	sc530ai->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc530ai->pinctrl)) {
		sc530ai->pins_default =
			pinctrl_lookup_state(sc530ai->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc530ai->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		sc530ai->pins_sleep =
			pinctrl_lookup_state(sc530ai->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc530ai->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = sc530ai_configure_regulators(sc530ai);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&sc530ai->mutex);

	sd = &sc530ai->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc530ai_subdev_ops);
	ret = sc530ai_initialize_controls(sc530ai);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc530ai_power_on(sc530ai);
	if (ret)
		goto err_free_handler;

	ret = sc530ai_check_sensor_id(sc530ai, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc530ai_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc530ai->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc530ai->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc530ai->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		sc530ai->module_index, facing,
		SC530AI_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(&sc530ai->client->dev,
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
	__sc530ai_power_off(sc530ai);
err_free_handler:
	v4l2_ctrl_handler_free(&sc530ai->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc530ai->mutex);

	return ret;
}

static int sc530ai_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc530ai *sc530ai = to_sc530ai(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc530ai->ctrl_handler);
	mutex_destroy(&sc530ai->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc530ai_power_off(sc530ai);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc530ai_of_match[] = {
	{ .compatible = "smartsens,sc530ai" },
	{ },
};
MODULE_DEVICE_TABLE(of, sc530ai_of_match);
#endif

static const struct i2c_device_id sc530ai_match_id[] = {
	{ "smartsens,sc530ai", 0 },
	{ },
};

static struct i2c_driver sc530ai_i2c_driver = {
	.driver = {
		.name = SC530AI_NAME,
		.pm = &sc530ai_pm_ops,
		.of_match_table = of_match_ptr(sc530ai_of_match),
	},
	.probe		= &sc530ai_probe,
	.remove		= &sc530ai_remove,
	.id_table	= sc530ai_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc530ai_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc530ai_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("smartsens sc530ai sensor driver");
MODULE_LICENSE("GPL v2");
