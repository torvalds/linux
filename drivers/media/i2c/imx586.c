// SPDX-License-Identifier: GPL-2.0
/*
 * imx586 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
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
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/mfd/syscon.h>
#include <linux/rk-preisp.h>
#include "otp_eeprom.h"

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define IMX586_LINK_FREQ_400		400000000	// 800Mbps per lane
#define IMX586_LINK_FREQ_625		625000000	// 1250Mbps per lane

#define IMX586_LANES			4

#define PIXEL_RATE_WITH_848M_10BIT	(IMX586_LINK_FREQ_400 * 2 / 10 * 4)
#define PIXEL_RATE_WITH_848M_12BIT	(IMX586_LINK_FREQ_400 * 2 / 12 * 4)

#define IMX586_XVCLK_FREQ		24000000

#define CHIP_ID				0x0586
#define IMX586_REG_CHIP_ID_H		0x0016
#define IMX586_REG_CHIP_ID_L		0x0017

#define IMX586_REG_CTRL_MODE		0x0100
#define IMX586_MODE_SW_STANDBY		0x0
#define IMX586_MODE_STREAMING		0x1

#define IMX586_REG_EXPOSURE_H		0x0202
#define IMX586_REG_EXPOSURE_L		0x0203
#define IMX586_EXPOSURE_MIN		2
#define IMX586_EXPOSURE_STEP		1
#define IMX586_VTS_MAX			0x7fff

#define IMX586_REG_GAIN_H		0x0204
#define IMX586_REG_GAIN_L		0x0205
#define IMX586_GAIN_MIN			0x10
#define IMX586_GAIN_MAX			0x400
#define IMX586_GAIN_STEP		1
#define IMX586_GAIN_DEFAULT		0x80

#define IMX586_REG_DGAIN		0x3130
#define IMX586_DGAIN_MODE		BIT(0)
#define IMX586_REG_DGAINGR_H		0x020e
#define IMX586_REG_DGAINGR_L		0x020f
#define IMX586_REG_DGAINR_H		0x0210
#define IMX586_REG_DGAINR_L		0x0211
#define IMX586_REG_DGAINB_H		0x0212
#define IMX586_REG_DGAINB_L		0x0213
#define IMX586_REG_DGAINGB_H		0x0214
#define IMX586_REG_DGAINGB_L		0x0215
#define IMX586_REG_GAIN_GLOBAL_H	0x3ffc
#define IMX586_REG_GAIN_GLOBAL_L	0x3ffd

//#define IMX586_REG_TEST_PATTERN_H	0x0600
#define IMX586_REG_TEST_PATTERN	0x0601
#define IMX586_TEST_PATTERN_ENABLE	0x1
#define IMX586_TEST_PATTERN_DISABLE	0x0

#define IMX586_REG_VTS_H		0x0340
#define IMX586_REG_VTS_L		0x0341

#define IMX586_FLIP_MIRROR_REG		0x0101
#define IMX586_MIRROR_BIT_MASK		BIT(0)
#define IMX586_FLIP_BIT_MASK		BIT(1)

#define IMX586_FETCH_EXP_H(VAL)		(((VAL) >> 8) & 0xFF)
#define IMX586_FETCH_EXP_L(VAL)		((VAL) & 0xFF)

#define IMX586_FETCH_AGAIN_H(VAL)		(((VAL) >> 8) & 0x03)
#define IMX586_FETCH_AGAIN_L(VAL)		((VAL) & 0xFF)

#define IMX586_FETCH_DGAIN_H(VAL)		(((VAL) >> 8) & 0x0F)
#define IMX586_FETCH_DGAIN_L(VAL)		((VAL) & 0xFF)

#define IMX586_FETCH_RHS1_H(VAL)	(((VAL) >> 16) & 0x0F)
#define IMX586_FETCH_RHS1_M(VAL)	(((VAL) >> 8) & 0xFF)
#define IMX586_FETCH_RHS1_L(VAL)	((VAL) & 0xFF)

#define REG_DELAY			0xFFFE
#define REG_NULL			0xFFFF

#define IMX586_REG_VALUE_08BIT		1
#define IMX586_REG_VALUE_16BIT		2
#define IMX586_REG_VALUE_24BIT		3

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define IMX586_NAME			"imx586"

static const char * const imx586_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define IMX586_NUM_SUPPLIES ARRAY_SIZE(imx586_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct other_data {
	u32 width;
	u32 height;
	u32 bus_fmt;
	u32 data_type;
	u32 data_bit;
};

struct imx586_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *global_reg_list;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 mipi_freq_idx;
	const struct other_data *spd;
	u32 vc[PAD_MAX];
};

struct imx586 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[IMX586_NUM_SUPPLIES];

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
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct imx586_mode *cur_mode;
	u32			cfg_num;
	u32			cur_pixel_rate;
	u32			cur_link_freq;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	u8			flip;
	struct otp_info		*otp;
	u32			spd_id;
};

#define to_imx586(sd) container_of(sd, struct imx586, subdev)

/*
 *IMX586LQR All-pixel scan CSI-2_4lane 24Mhz
 *AD:10bit Output:10bit 1696Mbps Master Mode 30fps
 *
 */
static const struct regval imx586_linear_10bit_global_regs[] = {
	/* External Clock Setting */
	{0x0136, 0x18},
	{0x0137, 0x00},
	/* Register version */
	{0x3C7E, 0x01},
	{0x3C7F, 0x08},

	/* Signaling mode setting */
	{0x0111, 0x02},

	/*Global Setting*/
	{0x380C, 0x00},
	{0x3C00, 0x10},
	{0x3C01, 0x10},
	{0x3C02, 0x10},
	{0x3C03, 0x10},
	{0x3C04, 0x10},
	{0x3C05, 0x01},
	{0x3C06, 0x00},
	{0x3C07, 0x00},
	{0x3C08, 0x03},
	{0x3C09, 0xFF},
	{0x3C0A, 0x01},
	{0x3C0B, 0x00},
	{0x3C0C, 0x00},
	{0x3C0D, 0x03},
	{0x3C0E, 0xFF},
	{0x3C0F, 0x20},
	{0x3F88, 0x00},
	{0x3F8E, 0x00},
	{0x5282, 0x01},
	{0x9004, 0x14},
	{0x9200, 0xF4},
	{0x9201, 0xA7},
	{0x9202, 0xF4},
	{0x9203, 0xAA},
	{0x9204, 0xF4},
	{0x9205, 0xAD},
	{0x9206, 0xF4},
	{0x9207, 0xB0},
	{0x9208, 0xF4},
	{0x9209, 0xB3},
	{0x920A, 0xB7},
	{0x920B, 0x34},
	{0x920C, 0xB7},
	{0x920D, 0x36},
	{0x920E, 0xB7},
	{0x920F, 0x37},
	{0x9210, 0xB7},
	{0x9211, 0x38},
	{0x9212, 0xB7},
	{0x9213, 0x39},
	{0x9214, 0xB7},
	{0x9215, 0x3A},
	{0x9216, 0xB7},
	{0x9217, 0x3C},
	{0x9218, 0xB7},
	{0x9219, 0x3D},
	{0x921A, 0xB7},
	{0x921B, 0x3E},
	{0x921C, 0xB7},
	{0x921D, 0x3F},
	{0x921E, 0x77},
	{0x921F, 0x77},
	{0x9222, 0xC4},
	{0x9223, 0x4B},
	{0x9224, 0xC4},
	{0x9225, 0x4C},
	{0x9226, 0xC4},
	{0x9227, 0x4D},
	{0x9810, 0x14},
	{0x9814, 0x14},
	{0x99B2, 0x20},
	{0x99B3, 0x0F},
	{0x99B4, 0x0F},
	{0x99B5, 0x0F},
	{0x99B6, 0x0F},
	{0x99E4, 0x0F},
	{0x99E5, 0x0F},
	{0x99E6, 0x0F},
	{0x99E7, 0x0F},
	{0x99E8, 0x0F},
	{0x99E9, 0x0F},
	{0x99EA, 0x0F},
	{0x99EB, 0x0F},
	{0x99EC, 0x0F},
	{0x99ED, 0x0F},
	{0xA569, 0x06},
	{0xA679, 0x20},
	{0xC020, 0x01},
	{0xC61D, 0x00},
	{0xC625, 0x00},
	{0xC638, 0x03},
	{0xC63B, 0x01},
	{0xE286, 0x31},
	{0xE2A6, 0x32},
	{0xE2C6, 0x33},
	{0xBCF1, 0x00},

	/*Image Quality adjustment setting */
	{0x9852, 0x00},
	{0x9954, 0x0F},
	{0xA7AD, 0x01},
	{0xA7CB, 0x01},
	{0xAE09, 0xFF},
	{0xAE0A, 0xFF},
	{0xAE12, 0x58},
	{0xAE13, 0x58},
	{0xAE15, 0x10},
	{0xAE16, 0x10},
	{0xAF05, 0x48},
	{0xB07C, 0x02},

	{REG_NULL, 0x00},
};

static const struct regval imx586_linear_10bit_4000x3000_30fps_nopd_regs[] = {
	/* MIPI output setting */
	{0x0112, 0x0A},
	{0x0113, 0x0A},
	{0x0114, 0x03},

	/* Line Length PCK Setting */
	{0x0342, 0x23},  // 8976
	{0x0343, 0x10},

	/* Frame Length Lines Setting */
	{0x0340, 0x0B},  // 3064
	{0x0341, 0xF8},

	/* ROI Setting */
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x1F},
	{0x0349, 0x3F},
	{0x034A, 0x17},
	{0x034B, 0x6F},

	/* Mode Setting */
	{0x0220, 0x62},
	{0x0222, 0x01},
	{0x0900, 0x01},
	{0x0901, 0x22},
	{0x0902, 0x08},
	{0x3140, 0x00},
	{0x3246, 0x81},
	{0x3247, 0x81},
	{0x3F15, 0x00},

	/* Digital Crop & Scaling */
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040A, 0x00},
	{0x040B, 0x00},
	{0x040C, 0x0F},
	{0x040D, 0xA0},
	{0x040E, 0x0B},
	{0x040F, 0xB8},

	/* Output Size Setting */
	{0x034C, 0x0F},
	{0x034D, 0xA0},
	{0x034E, 0x0B},
	{0x034F, 0xB8},

	/* Clock Setting */
	{0x0301, 0x05},
	{0x0303, 0x04},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x58},
	{0x030B, 0x02},
	{0x030D, 0x03},
	{0x030E, 0x01},
	{0x030F, 0x1F},
	{0x0310, 0x01},

	/* Other Setting */
	{0x3620, 0x00},
	{0x3621, 0x00},
	{0x3C11, 0x04},
	{0x3C12, 0x03},
	{0x3C13, 0x2D},
	{0x3F0C, 0x00},
	{0x3F14, 0x00},
	{0x3F80, 0x01},
	{0x3F81, 0x90},
	{0x3F8C, 0x00},
	{0x3F8D, 0x14},
	{0x3FF8, 0x01},
	{0x3FF9, 0x2A},
	{0x3FFE, 0x00},
	{0x3FFF, 0x6C},

	/* Integration Setting */
	{0x0202, 0x0B},
	{0x0203, 0xC4},
	{0x0224, 0x01},
	{0x0225, 0xF4},
	{0x3FE0, 0x01},
	{0x3FE1, 0xF4},

	/* Gain Setting */
	{0x0204, 0x00},
	{0x0205, 0x70},
	{0x0216, 0x00},
	{0x0217, 0x70},
	{0x0218, 0x01},
	{0x0219, 0x00},
	{0x020E, 0x01},
	{0x020F, 0x00},
	{0x0210, 0x01},
	{0x0211, 0x00},
	{0x0212, 0x01},
	{0x0213, 0x00},
	{0x0214, 0x01},
	{0x0215, 0x00},
	{0x3FE2, 0x00},
	{0x3FE3, 0x70},
	{0x3FE4, 0x01},
	{0x3FE5, 0x00},

	/* PDAF TYPE1 Setting */
	{0x3E20, 0x01},
	{0x3E37, 0x01},

	{REG_NULL, 0x00},
};

static const struct regval imx586_linear_10bit_full_raw_6fps_regs[] = {
	/* MIPI output setting */
	{0x0112, 0x0A},
	{0x0113, 0x0A},
	{0x0114, 0x03},

	/* Line Length PCK Setting */
	{0x0342, 0x39},
	{0x0343, 0x70},

	/* Frame Length Lines Setting */
	{0x0340, 0x17},
	{0x0341, 0xAC},

	/* ROI Setting */
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x1F},
	{0x0349, 0x3F},
	{0x034A, 0x17},
	{0x034B, 0x6F},

	/* Mode Setting */
	{0x0220, 0x62},
	{0x0222, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x0A},
	{0x3140, 0x00},
	{0x3246, 0x01},
	{0x3247, 0x01},
	{0x3F15, 0x00},

	/* Digital Crop & Scaling */
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040A, 0x00},
	{0x040B, 0x00},
	{0x040C, 0x1F},
	{0x040D, 0x40},
	{0x040E, 0x17},
	{0x040F, 0x70},

	/* Output Size Setting */
	{0x034C, 0x1F},
	{0x034D, 0x40},
	{0x034E, 0x17},
	{0x034F, 0x70},

	/* Clock Setting */
	{0x0301, 0x05},
	{0x0303, 0x04},
	{0x0305, 0x04},
	{0x0306, 0x00},
	{0x0307, 0xEE},
	{0x030B, 0x02},
	{0x030D, 0x06},
	{0x030E, 0x01},
	{0x030F, 0x90},
	{0x0310, 0x01},

	/* Other Setting */
	{0x3620, 0x00},
	{0x3621, 0x01},
	{0x3C11, 0x08},
	{0x3C12, 0x08},
	{0x3C13, 0x2A},
	{0x3F0C, 0x00},
	{0x3F14, 0x01},
	{0x3F80, 0x00},
	{0x3F81, 0x00},
	{0x3F8C, 0x00},
	{0x3F8D, 0x00},
	{0x3FF8, 0x00},
	{0x3FF9, 0x00},
	{0x3FFE, 0x03},
	{0x3FFF, 0x84},

	/* Integration Setting */
	{0x0202, 0x17},
	{0x0203, 0x7C},
	{0x0224, 0x01},
	{0x0225, 0xF4},
	{0x3FE0, 0x01},
	{0x3FE1, 0xF4},

	/* Gain Setting */
	{0x0204, 0x00},
	{0x0205, 0x70},
	{0x0216, 0x00},
	{0x0217, 0x70},
	{0x0218, 0x01},
	{0x0219, 0x00},
	{0x020E, 0x01},
	{0x020F, 0x00},
	{0x0210, 0x01},
	{0x0211, 0x00},
	{0x0212, 0x01},
	{0x0213, 0x00},
	{0x0214, 0x01},
	{0x0215, 0x00},
	{0x3FE2, 0x00},
	{0x3FE3, 0x70},
	{0x3FE4, 0x01},
	{0x3FE5, 0x00},

	/* PDAF TYPE1 Setting */
	{0x3E20, 0x01},
	{0x3E37, 0x01},

	{REG_NULL, 0x00},
};

static const struct regval imx586_linear_10bit_full_remosaic_6fps_regs[] = {
	/* MIPI output setting */
	{0x0112, 0x0A},
	{0x0113, 0x0A},
	{0x0114, 0x03},

	/* Line Length PCK Setting */
	{0x0342, 0x39},
	{0x0343, 0x70},

	/* Frame Length Lines Setting */
	{0x0340, 0x17},
	{0x0341, 0xAC},

	/* ROI Setting */
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x1F},
	{0x0349, 0x3F},
	{0x034A, 0x17},
	{0x034B, 0x6F},

	/* Mode Setting */
	{0x0220, 0x62},
	{0x0222, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x0A},
	{0x3140, 0x00},
	{0x3246, 0x01},
	{0x3247, 0x01},
	{0x3F15, 0x00},

	/* Digital Crop & Scaling */
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040A, 0x00},
	{0x040B, 0x00},
	{0x040C, 0x1F},
	{0x040D, 0x40},
	{0x040E, 0x17},
	{0x040F, 0x70},

	/* Output Size Setting */
	{0x034C, 0x1F},
	{0x034D, 0x40},
	{0x034E, 0x17},
	{0x034F, 0x70},

	/* Clock Setting */
	{0x0301, 0x05},
	{0x0303, 0x04},
	{0x0305, 0x04},
	{0x0306, 0x00},
	{0x0307, 0xEE},
	{0x030B, 0x02},
	{0x030D, 0x06},
	{0x030E, 0x01},
	{0x030F, 0x90},
	{0x0310, 0x01},

	/* Other Setting */
	{0x3620, 0x01},
	{0x3621, 0x01},
	{0x3C11, 0x08},
	{0x3C12, 0x08},
	{0x3C13, 0x2A},
	{0x3F0C, 0x00},
	{0x3F14, 0x01},
	{0x3F80, 0x00},
	{0x3F81, 0x14},
	{0x3F8C, 0x00},
	{0x3F8D, 0x14},
	{0x3FF8, 0x00},
	{0x3FF9, 0x00},
	{0x3FFE, 0x03},
	{0x3FFF, 0x52},

	/* Integration Setting */
	{0x0202, 0x17},
	{0x0203, 0x7C},
	{0x0224, 0x01},
	{0x0225, 0xF4},
	{0x3FE0, 0x01},
	{0x3FE1, 0xF4},

	/* Gain Setting */
	{0x0204, 0x00},
	{0x0205, 0x70},
	{0x0216, 0x00},
	{0x0217, 0x70},
	{0x0218, 0x01},
	{0x0219, 0x00},
	{0x020E, 0x01},
	{0x020F, 0x00},
	{0x0210, 0x01},
	{0x0211, 0x00},
	{0x0212, 0x01},
	{0x0213, 0x00},
	{0x0214, 0x01},
	{0x0215, 0x00},
	{0x3FE2, 0x00},
	{0x3FE3, 0x70},
	{0x3FE4, 0x01},
	{0x3FE5, 0x00},

	/* PDAF TYPE1 Setting */
	{0x3E20, 0x01},
	{0x3E37, 0x01},

	{REG_NULL, 0x00},
};

static const struct regval imx586_linear_10bit_full_remosaic_10fps_regs[] = {
	/* MIPI output setting */
	{0x0112, 0x0A},
	{0x0113, 0x0A},
	{0x0114, 0x03},

	/* Line Length PCK Setting */
	{0x0342, 0x39},
	{0x0343, 0x70},

	/* Frame Length Lines Setting */
	{0x0340, 0x17},
	{0x0341, 0xAC},

	/* ROI Setting */
	{0x0344, 0x00},
	{0x0345, 0x00},
	{0x0346, 0x00},
	{0x0347, 0x00},
	{0x0348, 0x1F},
	{0x0349, 0x3F},
	{0x034A, 0x17},
	{0x034B, 0x6F},

	/* Mode Setting */
	{0x0220, 0x62},
	{0x0222, 0x01},
	{0x0900, 0x00},
	{0x0901, 0x11},
	{0x0902, 0x0A},
	{0x3140, 0x00},
	{0x3246, 0x01},
	{0x3247, 0x01},
	{0x3F15, 0x00},

	/* Digital Crop & Scaling */
	{0x0401, 0x00},
	{0x0404, 0x00},
	{0x0405, 0x10},
	{0x0408, 0x00},
	{0x0409, 0x00},
	{0x040A, 0x00},
	{0x040B, 0x00},
	{0x040C, 0x1F},
	{0x040D, 0x40},
	{0x040E, 0x17},
	{0x040F, 0x70},

	/* Output Size Setting */
	{0x034C, 0x1F},
	{0x034D, 0x40},
	{0x034E, 0x17},
	{0x034F, 0x70},

	/* Clock Setting */
	{0x0301, 0x05},
	{0x0303, 0x04},
	{0x0305, 0x04},
	{0x0306, 0x01},
	{0x0307, 0x68},
	{0x030B, 0x02},
	{0x030D, 0x06},
	{0x030E, 0x02},
	{0x030F, 0x71},
	{0x0310, 0x01},

	/* Other Setting */
	{0x3620, 0x01},
	{0x3621, 0x01},
	{0x3C11, 0x08},
	{0x3C12, 0x08},
	{0x3C13, 0x2A},
	{0x3F0C, 0x00},
	{0x3F14, 0x01},
	{0x3F80, 0x00},
	{0x3F81, 0x14},
	{0x3F8C, 0x00},
	{0x3F8D, 0x14},
	{0x3FF8, 0x00},
	{0x3FF9, 0x00},
	{0x3FFE, 0x03},
	{0x3FFF, 0x52},

	/* Integration Setting */
	{0x0202, 0x17},
	{0x0203, 0x7C},
	{0x0224, 0x01},
	{0x0225, 0xF4},
	{0x3FE0, 0x01},
	{0x3FE1, 0xF4},

	/* Gain Setting */
	{0x0204, 0x00},
	{0x0205, 0x70},
	{0x0216, 0x00},
	{0x0217, 0x70},
	{0x0218, 0x01},
	{0x0219, 0x00},
	{0x020E, 0x01},
	{0x020F, 0x00},
	{0x0210, 0x01},
	{0x0211, 0x00},
	{0x0212, 0x01},
	{0x0213, 0x00},
	{0x0214, 0x01},
	{0x0215, 0x00},
	{0x3FE2, 0x00},
	{0x3FE3, 0x70},
	{0x3FE4, 0x01},
	{0x3FE5, 0x00},

	/* PDAF TYPE1 Setting */
	{0x3E20, 0x01},
	{0x3E37, 0x01},

	{REG_NULL, 0x00},
};

static const struct imx586_mode supported_modes[] = {
	{
		.width = 4000,
		.height = 3000,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0B00,
		.hts_def = 0x2310,
		.vts_def = 0x0BF8,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.global_reg_list = imx586_linear_10bit_global_regs,
		.reg_list = imx586_linear_10bit_4000x3000_30fps_nopd_regs,
		.hdr_mode = NO_HDR,
		.mipi_freq_idx = 0,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 8000,
		.height = 6000,
		.max_fps = {
			.numerator = 10000,
			.denominator = 64100,
		},
		.exp_def = 0x0B00,
		.hts_def = 0x3970,
		.vts_def = 0x17AC,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.global_reg_list = imx586_linear_10bit_global_regs,
		.reg_list = imx586_linear_10bit_full_raw_6fps_regs,
		.hdr_mode = NO_HDR,
		.mipi_freq_idx = 0,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 8000,
		.height = 6000,
		.max_fps = {
			.numerator = 10000,
			.denominator = 64100,
		},
		.exp_def = 0x0B00,
		.hts_def = 0x3970,
		.vts_def = 0x17AC,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.global_reg_list = imx586_linear_10bit_global_regs,
		.reg_list = imx586_linear_10bit_full_remosaic_6fps_regs,
		.hdr_mode = NO_HDR,
		.mipi_freq_idx = 0,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		.width = 8000,
		.height = 6000,
		.max_fps = {
			.numerator = 10000,
			.denominator = 97000,
		},
		.exp_def = 0x0B00,
		.hts_def = 0x3970,
		.vts_def = 0x17AC,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.global_reg_list = imx586_linear_10bit_global_regs,
		.reg_list = imx586_linear_10bit_full_remosaic_10fps_regs,
		.hdr_mode = NO_HDR,
		.mipi_freq_idx = 1,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_items[] = {
	IMX586_LINK_FREQ_400,
	IMX586_LINK_FREQ_625,
};

static const char * const imx586_test_pattern_menu[] = {
	"Disabled",
	"Solid color",
	"100% color bars",
	"Fade to grey color bars",
	"PN9"
};

/* Write registers up to 4 at a time */
static int imx586_write_reg(struct i2c_client *client, u16 reg,
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

static int imx586_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		if (unlikely(regs[i].addr == REG_DELAY))
			usleep_range(regs[i].val, regs[i].val * 2);
		else
			ret = imx586_write_reg(client, regs[i].addr,
					       IMX586_REG_VALUE_08BIT,
					       regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int imx586_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
			   u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret, i;

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

	for (i = 0; i < 3; i++) {
		ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (ret == ARRAY_SIZE(msgs))
			break;
	}
	if (ret != ARRAY_SIZE(msgs) && i == 3)
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

static int imx586_get_reso_dist(const struct imx586_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
		   abs(mode->height - framefmt->height);
}

static const struct imx586_mode *
imx586_find_best_fit(struct imx586 *imx586, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < imx586->cfg_num; i++) {
		dist = imx586_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int imx586_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx586 *imx586 = to_imx586(sd);
	const struct imx586_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;

	mutex_lock(&imx586->mutex);

	mode = imx586_find_best_fit(imx586, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&imx586->mutex);
		return -ENOTTY;
#endif
	} else {
		imx586->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(imx586->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(imx586->vblank, vblank_def,
					 IMX586_VTS_MAX - mode->height,
					 1, vblank_def);

		__v4l2_ctrl_s_ctrl(imx586->vblank, vblank_def);
		__v4l2_ctrl_s_ctrl(imx586->link_freq, mode->mipi_freq_idx);
		pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] / 10 * 2 * IMX586_LANES;
		__v4l2_ctrl_s_ctrl_int64(imx586->pixel_rate,
					 pixel_rate);
	}

	dev_info(&imx586->client->dev, "%s: mode->mipi_freq_idx(%d)",
		 __func__, mode->mipi_freq_idx);

	mutex_unlock(&imx586->mutex);

	return 0;
}

static int imx586_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx586 *imx586 = to_imx586(sd);
	const struct imx586_mode *mode = imx586->cur_mode;

	mutex_lock(&imx586->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&imx586->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		if (imx586->flip & IMX586_MIRROR_BIT_MASK) {
			fmt->format.code = MEDIA_BUS_FMT_SGRBG10_1X10;
			if (imx586->flip & IMX586_FLIP_BIT_MASK)
				fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		} else if (imx586->flip & IMX586_FLIP_BIT_MASK) {
			fmt->format.code = MEDIA_BUS_FMT_SGBRG10_1X10;
		} else {
			fmt->format.code = mode->bus_fmt;
		}
		fmt->format.field = V4L2_FIELD_NONE;
		/* format info: width/height/data type/virctual channel */
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&imx586->mutex);

	return 0;
}

static int imx586_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx586 *imx586 = to_imx586(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = imx586->cur_mode->bus_fmt;

	return 0;
}

static int imx586_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx586 *imx586 = to_imx586(sd);

	if (fse->index >= imx586->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[0].bus_fmt)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int imx586_enable_test_pattern(struct imx586 *imx586, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | IMX586_TEST_PATTERN_ENABLE;
	else
		val = IMX586_TEST_PATTERN_DISABLE;

	return imx586_write_reg(imx586->client,
				IMX586_REG_TEST_PATTERN,
				IMX586_REG_VALUE_08BIT,
				val);
}

static int imx586_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct imx586 *imx586 = to_imx586(sd);
	const struct imx586_mode *mode = imx586->cur_mode;

	mutex_lock(&imx586->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&imx586->mutex);

	return 0;
}

static int imx586_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct imx586 *imx586 = to_imx586(sd);
	const struct imx586_mode *mode = imx586->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (IMX586_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	if (mode->hdr_mode == HDR_X2)
		val = 1 << (IMX586_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		V4L2_MBUS_CSI2_CHANNEL_1;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void imx586_get_otp(struct otp_info *otp,
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

static void imx586_get_module_inf(struct imx586 *imx586,
				  struct rkmodule_inf *inf)
{
	struct otp_info *otp = imx586->otp;

	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, IMX586_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, imx586->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, imx586->len_name, sizeof(inf->base.lens));
	if (otp)
		imx586_get_otp(otp, inf);

}

static int imx586_get_channel_info(struct imx586 *imx586, struct rkmodule_channel_info *ch_info)
{
	const struct imx586_mode *mode = imx586->cur_mode;

	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;

	if (ch_info->index == imx586->spd_id && mode->spd) {
		ch_info->vc = V4L2_MBUS_CSI2_CHANNEL_0;
		ch_info->width = mode->spd->width;
		ch_info->height = mode->spd->height;
		ch_info->bus_fmt = mode->spd->bus_fmt;
		ch_info->data_type = mode->spd->data_type;
		ch_info->data_bit = mode->spd->data_bit;
	} else {
		ch_info->vc = imx586->cur_mode->vc[ch_info->index];
		ch_info->width = imx586->cur_mode->width;
		ch_info->height = imx586->cur_mode->height;
		ch_info->bus_fmt = imx586->cur_mode->bus_fmt;
	}
	return 0;
}

static long imx586_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct imx586 *imx586 = to_imx586(sd);
	struct rkmodule_hdr_cfg *hdr;
	struct rkmodule_channel_info *ch_info;
	long ret = 0;
	u32 i, h, w;
	u32 stream = 0;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		break;
	case RKMODULE_GET_MODULE_INFO:
		imx586_get_module_inf(imx586, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = imx586->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = imx586->cur_mode->width;
		h = imx586->cur_mode->height;
		for (i = 0; i < imx586->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				imx586->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == imx586->cfg_num) {
			dev_err(&imx586->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = imx586->cur_mode->hts_def -
			    imx586->cur_mode->width;
			h = imx586->cur_mode->vts_def -
			    imx586->cur_mode->height;
			__v4l2_ctrl_modify_range(imx586->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(imx586->vblank, h,
						 IMX586_VTS_MAX -
						 imx586->cur_mode->height,
						 1, h);

			if (imx586->cur_mode->bus_fmt ==
			    MEDIA_BUS_FMT_SRGGB10_1X10) {
				imx586->cur_link_freq = 0;
				imx586->cur_pixel_rate =
				PIXEL_RATE_WITH_848M_10BIT;
			} else if (imx586->cur_mode->bus_fmt ==
				   MEDIA_BUS_FMT_SRGGB12_1X12) {
				imx586->cur_link_freq = 0;
				imx586->cur_pixel_rate =
				PIXEL_RATE_WITH_848M_12BIT;
			}

			__v4l2_ctrl_s_ctrl_int64(imx586->pixel_rate,
						 imx586->cur_pixel_rate);
			__v4l2_ctrl_s_ctrl(imx586->link_freq,
					   imx586->cur_link_freq);
		}
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = imx586_write_reg(imx586->client, IMX586_REG_CTRL_MODE,
				IMX586_REG_VALUE_08BIT, IMX586_MODE_STREAMING);
		else
			ret = imx586_write_reg(imx586->client, IMX586_REG_CTRL_MODE,
				IMX586_REG_VALUE_08BIT, IMX586_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = imx586_get_channel_info(imx586, ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long imx586_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = imx586_ioctl(sd, cmd, inf);
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
			ret = imx586_ioctl(sd, cmd, cfg);
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

		ret = imx586_ioctl(sd, cmd, hdr);
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
			ret = imx586_ioctl(sd, cmd, hdr);
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
			ret = imx586_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = imx586_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}
		ret = imx586_ioctl(sd, cmd, ch_info);
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

static int imx586_set_flip(struct imx586 *imx586)
{
	int ret = 0;
	u32 val = 0;

	ret = imx586_read_reg(imx586->client, IMX586_FLIP_MIRROR_REG,
			      IMX586_REG_VALUE_08BIT, &val);
	if (imx586->flip & IMX586_MIRROR_BIT_MASK)
		val |= IMX586_MIRROR_BIT_MASK;
	else
		val &= ~IMX586_MIRROR_BIT_MASK;
	if (imx586->flip & IMX586_FLIP_BIT_MASK)
		val |= IMX586_FLIP_BIT_MASK;
	else
		val &= ~IMX586_FLIP_BIT_MASK;
	ret |= imx586_write_reg(imx586->client, IMX586_FLIP_MIRROR_REG,
				IMX586_REG_VALUE_08BIT, val);

	return ret;
}

static int __imx586_start_stream(struct imx586 *imx586)
{
	int ret;

	ret = imx586_write_array(imx586->client, imx586->cur_mode->global_reg_list);
	if (ret)
		return ret;

	ret = imx586_write_array(imx586->client, imx586->cur_mode->reg_list);
	if (ret)
		return ret;
	imx586->cur_vts = imx586->cur_mode->vts_def;
	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&imx586->ctrl_handler);
	if (ret)
		return ret;
	if (imx586->has_init_exp && imx586->cur_mode->hdr_mode != NO_HDR) {
		ret = imx586_ioctl(&imx586->subdev, PREISP_CMD_SET_HDRAE_EXP,
			&imx586->init_hdrae_exp);
		if (ret) {
			dev_err(&imx586->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}

	imx586_set_flip(imx586);

	return imx586_write_reg(imx586->client, IMX586_REG_CTRL_MODE,
				IMX586_REG_VALUE_08BIT, IMX586_MODE_STREAMING);
}

static int __imx586_stop_stream(struct imx586 *imx586)
{
	return imx586_write_reg(imx586->client, IMX586_REG_CTRL_MODE,
				IMX586_REG_VALUE_08BIT, IMX586_MODE_SW_STANDBY);
}

static int imx586_s_stream(struct v4l2_subdev *sd, int on)
{
	struct imx586 *imx586 = to_imx586(sd);
	struct i2c_client *client = imx586->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				imx586->cur_mode->width,
				imx586->cur_mode->height,
		DIV_ROUND_CLOSEST(imx586->cur_mode->max_fps.denominator,
				  imx586->cur_mode->max_fps.numerator));

	mutex_lock(&imx586->mutex);
	on = !!on;
	if (on == imx586->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __imx586_start_stream(imx586);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__imx586_stop_stream(imx586);
		pm_runtime_put(&client->dev);
	}

	imx586->streaming = on;

unlock_and_return:
	mutex_unlock(&imx586->mutex);

	return ret;
}

static int imx586_s_power(struct v4l2_subdev *sd, int on)
{
	struct imx586 *imx586 = to_imx586(sd);
	struct i2c_client *client = imx586->client;
	int ret = 0;

	mutex_lock(&imx586->mutex);

	/* If the power state is not modified - no work to do. */
	if (imx586->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		imx586->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		imx586->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&imx586->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 imx586_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, IMX586_XVCLK_FREQ / 1000 / 1000);
}

static int __imx586_power_on(struct imx586 *imx586)
{
	int ret;
	u32 delay_us;
	struct device *dev = &imx586->client->dev;

	ret = clk_set_rate(imx586->xvclk, IMX586_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(imx586->xvclk) != IMX586_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 37.125MHz\n");
	ret = clk_prepare_enable(imx586->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(imx586->reset_gpio))
		gpiod_set_value_cansleep(imx586->reset_gpio, 0);

	ret = regulator_bulk_enable(IMX586_NUM_SUPPLIES, imx586->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(imx586->reset_gpio))
		gpiod_set_value_cansleep(imx586->reset_gpio, 1);

	/* need wait 8ms to set register */
	usleep_range(8000, 10000);

	if (!IS_ERR(imx586->pwdn_gpio))
		gpiod_set_value_cansleep(imx586->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = imx586_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(imx586->xvclk);

	return ret;
}

static void __imx586_power_off(struct imx586 *imx586)
{

	if (!IS_ERR(imx586->pwdn_gpio))
		gpiod_set_value_cansleep(imx586->pwdn_gpio, 0);
	clk_disable_unprepare(imx586->xvclk);
	if (!IS_ERR(imx586->reset_gpio))
		gpiod_set_value_cansleep(imx586->reset_gpio, 0);
	regulator_bulk_disable(IMX586_NUM_SUPPLIES, imx586->supplies);
}

static int imx586_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx586 *imx586 = to_imx586(sd);

	return __imx586_power_on(imx586);
}

static int imx586_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx586 *imx586 = to_imx586(sd);

	__imx586_power_off(imx586);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int imx586_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx586 *imx586 = to_imx586(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct imx586_mode *def_mode = &supported_modes[0];

	mutex_lock(&imx586->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&imx586->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int imx586_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_frame_interval_enum *fie)
{
	struct imx586 *imx586 = to_imx586(sd);

	if (fie->index >= imx586->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops imx586_pm_ops = {
	SET_RUNTIME_PM_OPS(imx586_runtime_suspend,
			   imx586_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops imx586_internal_ops = {
	.open = imx586_open,
};
#endif

static const struct v4l2_subdev_core_ops imx586_core_ops = {
	.s_power = imx586_s_power,
	.ioctl = imx586_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = imx586_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops imx586_video_ops = {
	.s_stream = imx586_s_stream,
	.g_frame_interval = imx586_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops imx586_pad_ops = {
	.enum_mbus_code = imx586_enum_mbus_code,
	.enum_frame_size = imx586_enum_frame_sizes,
	.enum_frame_interval = imx586_enum_frame_interval,
	.get_fmt = imx586_get_fmt,
	.set_fmt = imx586_set_fmt,
	.get_mbus_config = imx586_g_mbus_config,
};

static const struct v4l2_subdev_ops imx586_subdev_ops = {
	.core	= &imx586_core_ops,
	.video	= &imx586_video_ops,
	.pad	= &imx586_pad_ops,
};

static int imx586_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx586 *imx586 = container_of(ctrl->handler,
					     struct imx586, ctrl_handler);
	struct i2c_client *client = imx586->client;
	s64 max;
	int ret = 0;
	u32 again = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = imx586->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(imx586->exposure,
					 imx586->exposure->minimum, max,
					 imx586->exposure->step,
					 imx586->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = imx586_write_reg(imx586->client,
				       IMX586_REG_EXPOSURE_H,
				       IMX586_REG_VALUE_08BIT,
				       IMX586_FETCH_EXP_H(ctrl->val));
		ret |= imx586_write_reg(imx586->client,
					IMX586_REG_EXPOSURE_L,
					IMX586_REG_VALUE_08BIT,
					IMX586_FETCH_EXP_L(ctrl->val));
		dev_dbg(&client->dev, "set exposure 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		/* gain_reg = 1024 - 1024 / gain_ana
		 * manual multiple 16 to add accuracy:
		 * then formula change to:
		 * gain_reg = 1024 - 1024 * 16 / (gain_ana * 16)
		 */
		if (ctrl->val > 0x400)
			ctrl->val = 0x400;
		if (ctrl->val < 0x10)
			ctrl->val = 0x10;

		again = 1024 - 1024 * 16 / ctrl->val;
		ret = imx586_write_reg(imx586->client, IMX586_REG_GAIN_H,
				       IMX586_REG_VALUE_08BIT,
				       IMX586_FETCH_AGAIN_H(again));
		ret |= imx586_write_reg(imx586->client, IMX586_REG_GAIN_L,
					IMX586_REG_VALUE_08BIT,
					IMX586_FETCH_AGAIN_L(again));

		dev_dbg(&client->dev, "set analog gain 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = imx586_write_reg(imx586->client,
				       IMX586_REG_VTS_H,
				       IMX586_REG_VALUE_08BIT,
				       (ctrl->val + imx586->cur_mode->height)
				       >> 8);
		ret |= imx586_write_reg(imx586->client,
					IMX586_REG_VTS_L,
					IMX586_REG_VALUE_08BIT,
					(ctrl->val + imx586->cur_mode->height)
					& 0xff);
		imx586->cur_vts = ctrl->val + imx586->cur_mode->height;

		dev_dbg(&client->dev, "set vblank 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			imx586->flip |= IMX586_MIRROR_BIT_MASK;
		else
			imx586->flip &= ~IMX586_MIRROR_BIT_MASK;
		dev_dbg(&client->dev, "set hflip 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		if (ctrl->val)
			imx586->flip |= IMX586_FLIP_BIT_MASK;
		else
			imx586->flip &= ~IMX586_FLIP_BIT_MASK;
		dev_dbg(&client->dev, "set vflip 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		dev_dbg(&client->dev, "set testpattern 0x%x\n",
			ctrl->val);
		ret = imx586_enable_test_pattern(imx586, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx586_ctrl_ops = {
	.s_ctrl = imx586_set_ctrl,
};

static int imx586_initialize_controls(struct imx586 *imx586)
{
	const struct imx586_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &imx586->ctrl_handler;
	mode = imx586->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &imx586->mutex;

	imx586->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ,
				ARRAY_SIZE(link_freq_items) - 1, 0,
				link_freq_items);

	if (imx586->cur_mode->bus_fmt == MEDIA_BUS_FMT_SRGGB10_1X10) {
		imx586->cur_link_freq = 0;
		imx586->cur_pixel_rate = PIXEL_RATE_WITH_848M_10BIT;
	} else if (imx586->cur_mode->bus_fmt == MEDIA_BUS_FMT_SRGGB12_1X12) {
		imx586->cur_link_freq = 0;
		imx586->cur_pixel_rate = PIXEL_RATE_WITH_848M_12BIT;
	}

	imx586->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
					       V4L2_CID_PIXEL_RATE,
					       0, PIXEL_RATE_WITH_848M_10BIT,
					       1, imx586->cur_pixel_rate);
	v4l2_ctrl_s_ctrl(imx586->link_freq,
			   imx586->cur_link_freq);

	h_blank = mode->hts_def - mode->width;
	imx586->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					   h_blank, h_blank, 1, h_blank);
	if (imx586->hblank)
		imx586->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	imx586->vblank = v4l2_ctrl_new_std(handler, &imx586_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_def,
					   IMX586_VTS_MAX - mode->height,
					   1, vblank_def);
	imx586->cur_vts = mode->vts_def;
	exposure_max = mode->vts_def - 4;
	imx586->exposure = v4l2_ctrl_new_std(handler, &imx586_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX586_EXPOSURE_MIN,
					     exposure_max,
					     IMX586_EXPOSURE_STEP,
					     mode->exp_def);
	imx586->anal_gain = v4l2_ctrl_new_std(handler, &imx586_ctrl_ops,
					      V4L2_CID_ANALOGUE_GAIN,
					      IMX586_GAIN_MIN,
					      IMX586_GAIN_MAX,
					      IMX586_GAIN_STEP,
					      IMX586_GAIN_DEFAULT);
	imx586->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
							    &imx586_ctrl_ops,
				V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(imx586_test_pattern_menu) - 1,
				0, 0, imx586_test_pattern_menu);

	imx586->h_flip = v4l2_ctrl_new_std(handler, &imx586_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	imx586->v_flip = v4l2_ctrl_new_std(handler, &imx586_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);
	imx586->flip = 0;

	if (handler->error) {
		ret = handler->error;
		dev_err(&imx586->client->dev,
			"Failed to init controls(  %d  )\n", ret);
		goto err_free_handler;
	}

	imx586->subdev.ctrl_handler = handler;
	imx586->has_init_exp = false;
	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int imx586_check_sensor_id(struct imx586 *imx586,
				  struct i2c_client *client)
{
	struct device *dev = &imx586->client->dev;
	u16 id = 0;
	u32 reg_H = 0;
	u32 reg_L = 0;
	int ret;

	ret = imx586_read_reg(client, IMX586_REG_CHIP_ID_H,
			      IMX586_REG_VALUE_08BIT, &reg_H);
	ret |= imx586_read_reg(client, IMX586_REG_CHIP_ID_L,
			       IMX586_REG_VALUE_08BIT, &reg_L);
	id = ((reg_H << 8) & 0xff00) | (reg_L & 0xff);
	if (!(reg_H == (CHIP_ID >> 8) || reg_L == (CHIP_ID & 0xff))) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}
	dev_info(dev, "detected imx586 %04x sensor\n", id);
	return 0;
}

static int imx586_configure_regulators(struct imx586 *imx586)
{
	unsigned int i;

	for (i = 0; i < IMX586_NUM_SUPPLIES; i++)
		imx586->supplies[i].supply = imx586_supply_names[i];

	return devm_regulator_bulk_get(&imx586->client->dev,
				       IMX586_NUM_SUPPLIES,
				       imx586->supplies);
}

static int imx586_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct imx586 *imx586;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;
	struct device_node *eeprom_ctrl_node;
	struct i2c_client *eeprom_ctrl_client;
	struct v4l2_subdev *eeprom_ctrl;
	struct otp_info *otp_ptr;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	imx586 = devm_kzalloc(dev, sizeof(*imx586), GFP_KERNEL);
	if (!imx586)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &imx586->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &imx586->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &imx586->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &imx586->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}

	imx586->client = client;
	imx586->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < imx586->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			imx586->cur_mode = &supported_modes[i];
			break;
		}
	}

	if (i == imx586->cfg_num)
		imx586->cur_mode = &supported_modes[0];

	imx586->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(imx586->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	imx586->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(imx586->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	imx586->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(imx586->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = of_property_read_u32(node,
				   "rockchip,spd-id",
				   &imx586->spd_id);
	if (ret != 0) {
		imx586->spd_id = PAD_MAX;
		dev_err(dev,
			"failed get spd_id, will not to use spd\n");
	}

	ret = imx586_configure_regulators(imx586);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&imx586->mutex);

	sd = &imx586->subdev;
	v4l2_i2c_subdev_init(sd, client, &imx586_subdev_ops);

	ret = imx586_initialize_controls(imx586);
	if (ret)
		goto err_destroy_mutex;

	ret = __imx586_power_on(imx586);
	if (ret)
		goto err_free_handler;

	ret = imx586_check_sensor_id(imx586, client);
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
				imx586->otp = otp_ptr;
			} else {
				imx586->otp = NULL;
				devm_kfree(dev, otp_ptr);
			}
		}
	}
continue_probe:

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &imx586_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	imx586->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &imx586->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(imx586->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 imx586->module_index, facing,
		 IMX586_NAME, dev_name(sd->dev));
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
	__imx586_power_off(imx586);
err_free_handler:
	v4l2_ctrl_handler_free(&imx586->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&imx586->mutex);

	return ret;
}

static int imx586_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx586 *imx586 = to_imx586(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&imx586->ctrl_handler);
	mutex_destroy(&imx586->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__imx586_power_off(imx586);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id imx586_of_match[] = {
	{ .compatible = "sony,imx586" },
	{},
};
MODULE_DEVICE_TABLE(of, imx586_of_match);
#endif

static const struct i2c_device_id imx586_match_id[] = {
	{ "sony,imx586", 0 },
	{ },
};

static struct i2c_driver imx586_i2c_driver = {
	.driver = {
		.name = IMX586_NAME,
		.pm = &imx586_pm_ops,
		.of_match_table = of_match_ptr(imx586_of_match),
	},
	.probe		= &imx586_probe,
	.remove		= &imx586_remove,
	.id_table	= imx586_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&imx586_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&imx586_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Sony imx586 sensor driver");
MODULE_LICENSE("GPL");
