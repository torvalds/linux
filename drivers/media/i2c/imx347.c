// SPDX-License-Identifier: GPL-2.0
/*
 * imx347 driver
 *
 * Copyright (C) 2020 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version
 * V0.0X01.0X01 add conversion gain control
 * V0.0X01.0X02 add debug interface for conversion gain control
 * V0.0X01.0X03 support enum sensor fmt
 * V0.0X01.0X04 fix setting flow error according to datasheet and fix hdr gain error
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
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include <linux/rk-preisp.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x04)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIPI_FREQ_360M			360000000
#define MIPI_FREQ_594M			594000000

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define IMX347_10BIT_LINEAR_PIXEL_RATE	(MIPI_FREQ_594M * 2 / 10 * 2)
#define IMX347_10BIT_HDR2_PIXEL_RATE	(MIPI_FREQ_594M * 2 / 10 * 4)
#define IMX347_12BIT_PIXEL_RATE		(MIPI_FREQ_360M * 2 / 12 * 4)
#define IMX347_XVCLK_FREQ_37M		37125000
#define IMX347_XVCLK_FREQ_24M		24000000

#define CHIP_ID				0x06
#define IMX347_REG_CHIP_ID		0x3057

#define IMX347_REG_CTRL_MODE		0x3000
#define IMX347_MODE_SW_STANDBY		BIT(0)
#define IMX347_MODE_STREAMING		0x0

#define IMX347_REG_MASTER_MODE		0x3002
#define IMX347_MASTER_MODE_STOP		BIT(0)
#define IMX347_MASTER_MODE_START	0x0

#define IMX347_REG_RESTART_MODE		0x3004
#define IMX347_RESTART_MODE_START	0x04
#define IMX347_RESTART_MODE_STOP	0x0

#define IMX347_GAIN_SWITCH_REG		0x3019

#define IMX347_LF_GAIN_REG_H		0x30E9
#define IMX347_LF_GAIN_REG_L		0x30E8

#define IMX347_SF1_GAIN_REG_H		0x30EB
#define IMX347_SF1_GAIN_REG_L		0x30EA

#define IMX347_LF_EXPO_REG_H		0x305A
#define IMX347_LF_EXPO_REG_M		0x3059
#define IMX347_LF_EXPO_REG_L		0x3058

#define IMX347_SF1_EXPO_REG_H		0x305E
#define IMX347_SF1_EXPO_REG_M		0x305D
#define IMX347_SF1_EXPO_REG_L		0x305C

#define IMX347_RHS1_REG_H		0x306a
#define IMX347_RHS1_REG_M		0x3069
#define IMX347_RHS1_REG_L		0x3068

#define	IMX347_EXPOSURE_MIN		2
#define	IMX347_EXPOSURE_STEP		1
#define IMX347_VTS_MAX			0x7fff

#define IMX347_GAIN_MIN			0x00
#define IMX347_GAIN_MAX			0xee
#define IMX347_GAIN_STEP		1
#define IMX347_GAIN_DEFAULT		0x00

#define IMX347_FETCH_GAIN_H(VAL)	(((VAL) >> 8) & 0x07)
#define IMX347_FETCH_GAIN_L(VAL)	((VAL) & 0xFF)

#define IMX347_FETCH_EXP_H(VAL)		(((VAL) >> 16) & 0x0F)
#define IMX347_FETCH_EXP_M(VAL)		(((VAL) >> 8) & 0xFF)
#define IMX347_FETCH_EXP_L(VAL)		((VAL) & 0xFF)

#define IMX347_FETCH_RHS1_H(VAL)	(((VAL) >> 16) & 0x0F)
#define IMX347_FETCH_RHS1_M(VAL)	(((VAL) >> 8) & 0xFF)
#define IMX347_FETCH_RHS1_L(VAL)	((VAL) & 0xFF)

#define IMX347_FETCH_VTS_H(VAL)		(((VAL) >> 16) & 0x0F)
#define IMX347_FETCH_VTS_M(VAL)		(((VAL) >> 8) & 0xFF)
#define IMX347_FETCH_VTS_L(VAL)		((VAL) & 0xFF)

#define IMX347_GROUP_HOLD_REG		0x3001
#define IMX347_GROUP_HOLD_START		0x01
#define IMX347_GROUP_HOLD_END		0x00

#define IMX347_VTS_REG_L		0x3030
#define IMX347_VTS_REG_M		0x3031
#define IMX347_VTS_REG_H		0x3032

#define REG_NULL			0xFFFF

#define IMX347_REG_VALUE_08BIT		1
#define IMX347_REG_VALUE_16BIT		2
#define IMX347_REG_VALUE_24BIT		3

#define IMX347_2LANES			2
#define IMX347_4LANES			4
#define IMX347_BITS_PER_SAMPLE		10

#define IMX347_VREVERSE_REG	0x304f
#define IMX347_HREVERSE_REG	0x304e

#define RHS1_MAX			3113 // <2*BRL=2*1556 && 4n+1
#define SHR1_MIN			9
#define BRL				1556

#define USED_SYS_DEBUG

static bool g_isHCG;

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define IMX347_NAME			"imx347"

static const char * const imx347_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define IMX347_NUM_SUPPLIES ARRAY_SIZE(imx347_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct imx347_mode {
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
	u8 bpp;
};

struct imx347 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[IMX347_NUM_SUPPLIES];
	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;
	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_a_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct imx347_mode *cur_mode;
	u32			module_index;
	u32			cfg_num;
	u32			cur_pixel_rate;
	u32			cur_link_freq;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
};

#define to_imx347(sd) container_of(sd, struct imx347, subdev)

/*
 * Xclk 37.125Mhz
 */
static const struct regval imx347_global_regs[] = {
	{REG_NULL, 0x00},
};

static const struct regval imx347_linear_10bit_2688x1520_regs[] = {
	{0x300C, 0x5B},
	{0x300D, 0x40},
	{0x3018, 0x00},
	{0x302C, 0x24},
	{0x302E, 0x98},
	{0x302F, 0x0A},
	{0x3030, 0xBC},
	{0x3031, 0x07},
	{0x3032, 0x00},
	{0x3034, 0xDC},
	{0x3035, 0x05},
	{0x3048, 0x00},
	{0x3049, 0x00},
	{0x304A, 0x03},
	{0x304B, 0x02},
	{0x304C, 0x14},
	{0x3050, 0x00},
	{0x3056, 0x00},
	{0x3057, 0x06},
	{0x3058, 0x03},
	{0x3059, 0x00},
	{0x3068, 0xc9},
	{0x3069, 0x00},
	{0x30BE, 0x5E},
	{0x30C6, 0x06},
	{0x30CE, 0x04},
	{0x30D8, 0x44},
	{0x30D9, 0x06},
	{0x3110, 0x02},
	{0x314C, 0x80},
	{0x315A, 0x02},
	{0x3168, 0x68},
	{0x316A, 0x7E},
	{0x319D, 0x00},
	{0x319E, 0x01},
	{0x31A1, 0x00},
	{0x31D7, 0x00},
	{0x3200, 0x11},/* Each frame gain adjustment disabed in linear mode */
	{0x3202, 0x02},
	{0x3288, 0x22},
	{0x328A, 0x02},
	{0x328C, 0xA2},
	{0x328E, 0x22},
	{0x3415, 0x27},
	{0x3418, 0x27},
	{0x3428, 0xFE},
	{0x349E, 0x6A},
	{0x34A2, 0x9A},
	{0x34A4, 0x8A},
	{0x34A6, 0x8E},
	{0x34AA, 0xD8},
	{0x3648, 0x01},
	{0x3678, 0x01},
	{0x367C, 0x69},
	{0x367E, 0x69},
	{0x3680, 0x69},
	{0x3682, 0x69},
	{0x371D, 0x05},
	{0x375D, 0x11},
	{0x375E, 0x43},
	{0x375F, 0x76},
	{0x3760, 0x07},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1A},
	{0x376B, 0x19},
	{0x376C, 0x17},
	{0x376D, 0x0F},
	{0x376E, 0x0B},
	{0x376F, 0x0B},
	{0x3770, 0x0B},
	{0x3776, 0x89},
	{0x3777, 0x00},
	{0x3778, 0xCA},
	{0x3779, 0x00},
	{0x377A, 0x45},
	{0x377B, 0x01},
	{0x377C, 0x56},
	{0x377D, 0x02},
	{0x377E, 0xFE},
	{0x377F, 0x03},
	{0x3780, 0xFE},
	{0x3781, 0x05},
	{0x3782, 0xFE},
	{0x3783, 0x06},
	{0x3784, 0x7F},
	{0x3788, 0x1F},
	{0x378A, 0xCA},
	{0x378B, 0x00},
	{0x378C, 0x45},
	{0x378D, 0x01},
	{0x378E, 0x56},
	{0x378F, 0x02},
	{0x3790, 0xFE},
	{0x3791, 0x03},
	{0x3792, 0xFE},
	{0x3793, 0x05},
	{0x3794, 0xFE},
	{0x3795, 0x06},
	{0x3796, 0x7F},
	{0x3798, 0xBF},
	{0x3A01, 0x01},
	{0x3A18, 0x8F},
	{0x3A1A, 0x4F},
	{0x3A1C, 0x47},
	{0x3A1E, 0x37},
	{0x3A1F, 0x01},
	{0x3A20, 0x4F},
	{0x3A22, 0x87},
	{0x3A24, 0x4F},
	{0x3A26, 0x7f},
	{0x3A28, 0x3f},
	{REG_NULL, 0x00},
};

static const struct regval imx347_hdr_2x_10bit_2688x1520_regs[] = {
	{0x300C, 0x5B},
	{0x300D, 0x40},
	{0x3018, 0x00},
	{0x302C, 0x24},
	{0x302E, 0x98},
	{0x302F, 0x0A},
	{0x3030, 0xbc},
	{0x3031, 0x07},
	{0x3032, 0x00},
	{0x3034, 0xEE},
	{0x3035, 0x02},
	{0x3048, 0x01},
	{0x3049, 0x01},
	{0x304A, 0x04},
	{0x304B, 0x04},
	{0x304C, 0x13},
	{0x3050, 0x00},
	{0x3056, 0x00},
	{0x3057, 0x06},
	{0x3058, 0x4A},
	{0x3059, 0x01},
	{0x3068, 0xD1},
	{0x3069, 0x00},
	{0x30BE, 0x5E},
	{0x30C6, 0x06},
	{0x30CE, 0x04},
	{0x30D8, 0x44},
	{0x30D9, 0x06},
	{0x3110, 0x02},
	{0x314C, 0x80},
	{0x315A, 0x02},
	{0x3168, 0x68},
	{0x316A, 0x7E},
	{0x319D, 0x00},
	{0x319E, 0x01},
	{0x31A1, 0x00},
	{0x31D7, 0x01},
	{0x3200, 0x10},/* Each frame gain adjustment EN in hdr mode */
	{0x3202, 0x02},
	{0x3288, 0x22},
	{0x328A, 0x02},
	{0x328C, 0xA2},
	{0x328E, 0x22},
	{0x3415, 0x27},
	{0x3418, 0x27},
	{0x3428, 0xFE},
	{0x349E, 0x6A},
	{0x34A2, 0x9A},
	{0x34A4, 0x8A},
	{0x34A6, 0x8E},
	{0x34AA, 0xD8},
	{0x3648, 0x01},
	{0x3678, 0x01},
	{0x367C, 0x69},
	{0x367E, 0x69},
	{0x3680, 0x69},
	{0x3682, 0x69},
	{0x371D, 0x05},
	{0x375D, 0x11},
	{0x375E, 0x43},
	{0x375F, 0x76},
	{0x3760, 0x07},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1A},
	{0x376B, 0x19},
	{0x376C, 0x17},
	{0x376D, 0x0F},
	{0x376E, 0x0B},
	{0x376F, 0x0B},
	{0x3770, 0x0B},
	{0x3776, 0x89},
	{0x3777, 0x00},
	{0x3778, 0xCA},
	{0x3779, 0x00},
	{0x377A, 0x45},
	{0x377B, 0x01},
	{0x377C, 0x56},
	{0x377D, 0x02},
	{0x377E, 0xFE},
	{0x377F, 0x03},
	{0x3780, 0xFE},
	{0x3781, 0x05},
	{0x3782, 0xFE},
	{0x3783, 0x06},
	{0x3784, 0x7F},
	{0x3788, 0x1F},
	{0x378A, 0xCA},
	{0x378B, 0x00},
	{0x378C, 0x45},
	{0x378D, 0x01},
	{0x378E, 0x56},
	{0x378F, 0x02},
	{0x3790, 0xFE},
	{0x3791, 0x03},
	{0x3792, 0xFE},
	{0x3793, 0x05},
	{0x3794, 0xFE},
	{0x3795, 0x06},
	{0x3796, 0x7F},
	{0x3798, 0xBF},
	{0x3A01, 0x03},
	{0x3A18, 0x8F},
	{0x3A1A, 0x4F},
	{0x3A1C, 0x47},
	{0x3A1E, 0x37},
	{0x3A1F, 0x01},
	{0x3A20, 0x4F},
	{0x3A22, 0x87},
	{0x3A24, 0x4F},
	{0x3A26, 0x7f},
	{0x3A28, 0x3f},
	{REG_NULL, 0x00},
};

static const struct regval imx347_linear_12bit_2688x1520_regs[] = {
	{0x300C, 0x3B},
	{0x300D, 0x2A},
	{0x3018, 0x04},
	{0x302C, 0x30},
	{0x302E, 0x80},
	{0x302F, 0x0A},
	{0x3030, 0x6B},
	{0x3031, 0x0A},
	{0x3032, 0x00},
	{0x3034, 0xee},
	{0x3035, 0x02},
	{0x3048, 0x00},
	{0x3049, 0x00},
	{0x304A, 0x03},
	{0x304B, 0x02},
	{0x304C, 0x14},
	{0x3050, 0x01},
	{0x3056, 0x02},
	{0x3057, 0x06},
	{0x3058, 0x03},
	{0x3059, 0x00},
	{0x3068, 0xc9},
	{0x3069, 0x00},
	{0x30BE, 0x5E},
	{0x30C6, 0x00},
	{0x30CE, 0x00},
	{0x30D8, 0x4F},
	{0x30D9, 0x64},
	{0x3110, 0x02},
	{0x314C, 0xF0},
	{0x315A, 0x06},
	{0x3168, 0x82},
	{0x316A, 0x7E},
	{0x319D, 0x01},
	{0x319E, 0x02},
	{0x31A1, 0x00},
	{0x31D7, 0x00},
	{0x3200, 0x11},/* Each frame gain adjustment disabed in linear mode */
	{0x3202, 0x02},
	{0x3288, 0x22},
	{0x328A, 0x02},
	{0x328C, 0xA2},
	{0x328E, 0x22},
	{0x3415, 0x27},
	{0x3418, 0x27},
	{0x3428, 0xFE},
	{0x349E, 0x6A},
	{0x34A2, 0x9A},
	{0x34A4, 0x8A},
	{0x34A6, 0x8E},
	{0x34AA, 0xD8},
	{0x3648, 0x01},
	{0x3678, 0x01},
	{0x367C, 0x69},
	{0x367E, 0x69},
	{0x3680, 0x69},
	{0x3682, 0x69},
	{0x371D, 0x05},
	{0x375D, 0x11},
	{0x375E, 0x43},
	{0x375F, 0x76},
	{0x3760, 0x07},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1A},
	{0x376B, 0x19},
	{0x376C, 0x17},
	{0x376D, 0x0F},
	{0x376E, 0x0B},
	{0x376F, 0x0B},
	{0x3770, 0x0B},
	{0x3776, 0x89},
	{0x3777, 0x00},
	{0x3778, 0xCA},
	{0x3779, 0x00},
	{0x377A, 0x45},
	{0x377B, 0x01},
	{0x377C, 0x56},
	{0x377D, 0x02},
	{0x377E, 0xFE},
	{0x377F, 0x03},
	{0x3780, 0xFE},
	{0x3781, 0x05},
	{0x3782, 0xFE},
	{0x3783, 0x06},
	{0x3784, 0x7F},
	{0x3788, 0x1F},
	{0x378A, 0xCA},
	{0x378B, 0x00},
	{0x378C, 0x45},
	{0x378D, 0x01},
	{0x378E, 0x56},
	{0x378F, 0x02},
	{0x3790, 0xFE},
	{0x3791, 0x03},
	{0x3792, 0xFE},
	{0x3793, 0x05},
	{0x3794, 0xFE},
	{0x3795, 0x06},
	{0x3796, 0x7F},
	{0x3798, 0xBF},
	{0x3A01, 0x03},
	{0x3A18, 0x6F},
	{0x3A1A, 0x2F},
	{0x3A1C, 0x2F},
	{0x3A1E, 0xBF},
	{0x3A1F, 0x00},
	{0x3A20, 0x2F},
	{0x3A22, 0x57},
	{0x3A24, 0x2F},
	{0x3A26, 0x4F},
	{0x3A28, 0x27},
	{REG_NULL, 0x00},
};

static const struct regval imx347_hdr_2x_12bit_2688x1520_regs[] = {
	{0x300C, 0x3B},
	{0x300D, 0x2A},
	{0x3018, 0x04},
	{0x302C, 0x30},
	{0x302E, 0x80},
	{0x302F, 0x0A},
	{0x3030, 0x40},
	{0x3031, 0x06},
	{0x3032, 0x00},
	{0x3034, 0xee},
	{0x3035, 0x02},
	{0x3048, 0x01},
	{0x3049, 0x01},
	{0x304A, 0x04},
	{0x304B, 0x04},
	{0x304C, 0x13},
	{0x3050, 0x01},
	{0x3056, 0x02},
	{0x3057, 0x06},
	{0x3058, 0x20},
	{0x3059, 0x03},
	{0x3068, 0xD9},
	{0x3069, 0x02},
	{0x30BE, 0x5E},
	{0x30C6, 0x00},
	{0x30CE, 0x00},
	{0x30D8, 0x4F},
	{0x30D9, 0x64},
	{0x3110, 0x02},
	{0x314C, 0xF0},
	{0x315A, 0x06},
	{0x3168, 0x82},
	{0x316A, 0x7E},
	{0x319D, 0x01},
	{0x319E, 0x02},
	{0x31A1, 0x00},
	{0x31D7, 0x01},
	{0x3200, 0x10},/* Each frame gain adjustment EN in hdr mode */
	{0x3202, 0x02},
	{0x3288, 0x22},
	{0x328A, 0x02},
	{0x328C, 0xA2},
	{0x328E, 0x22},
	{0x3415, 0x27},
	{0x3418, 0x27},
	{0x3428, 0xFE},
	{0x349E, 0x6A},
	{0x34A2, 0x9A},
	{0x34A4, 0x8A},
	{0x34A6, 0x8E},
	{0x34AA, 0xD8},
	{0x3648, 0x01},
	{0x3678, 0x01},
	{0x367C, 0x69},
	{0x367E, 0x69},
	{0x3680, 0x69},
	{0x3682, 0x69},
	{0x371D, 0x05},
	{0x375D, 0x11},
	{0x375E, 0x43},
	{0x375F, 0x76},
	{0x3760, 0x07},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1A},
	{0x376B, 0x19},
	{0x376C, 0x17},
	{0x376D, 0x0F},
	{0x376E, 0x0B},
	{0x376F, 0x0B},
	{0x3770, 0x0B},
	{0x3776, 0x89},
	{0x3777, 0x00},
	{0x3778, 0xCA},
	{0x3779, 0x00},
	{0x377A, 0x45},
	{0x377B, 0x01},
	{0x377C, 0x56},
	{0x377D, 0x02},
	{0x377E, 0xFE},
	{0x377F, 0x03},
	{0x3780, 0xFE},
	{0x3781, 0x05},
	{0x3782, 0xFE},
	{0x3783, 0x06},
	{0x3784, 0x7F},
	{0x3788, 0x1F},
	{0x378A, 0xCA},
	{0x378B, 0x00},
	{0x378C, 0x45},
	{0x378D, 0x01},
	{0x378E, 0x56},
	{0x378F, 0x02},
	{0x3790, 0xFE},
	{0x3791, 0x03},
	{0x3792, 0xFE},
	{0x3793, 0x05},
	{0x3794, 0xFE},
	{0x3795, 0x06},
	{0x3796, 0x7F},
	{0x3798, 0xBF},
	{0x3A01, 0x03},
	{0x3A18, 0x6F},
	{0x3A1A, 0x2F},
	{0x3A1C, 0x2F},
	{0x3A1E, 0xBF},
	{0x3A1F, 0x00},
	{0x3A20, 0x2F},
	{0x3A22, 0x57},
	{0x3A24, 0x2F},
	{0x3A26, 0x4F},
	{0x3A28, 0x27},
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
static const struct imx347_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 2712,
		.height = 1536,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.exp_def = 0x0240,
		.hts_def = 0x05dc * 2,
		.vts_def = 0x07bc,
		.reg_list = imx347_linear_10bit_2688x1520_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
		.bpp = 10,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 2712,
		.height = 1536,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.exp_def = 0x0240,
		.hts_def = 0x02ee * 4,
		.vts_def = 0x07bc * 2,
		.reg_list = imx347_hdr_2x_10bit_2688x1520_regs,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
		.bpp = 10,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB12_1X12,
		.width = 2688,
		.height = 1538,
		.max_fps = {
			.numerator = 10000,
			.denominator = 299960,
		},
		.exp_def = 0x0240,
		.hts_def = 0x02EE * 4,
		.vts_def = 0x0A6B,
		.reg_list = imx347_linear_12bit_2688x1520_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
		.bpp = 12,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_SRGGB12_1X12,
		.width = 2688,
		.height = 1538,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.exp_def = 0x0240,
		.hts_def = 0x02ee * 4,
		.vts_def = 0x0640 * 2,
		.reg_list = imx347_hdr_2x_12bit_2688x1520_regs,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
		.bpp = 12,
	},
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ_360M,
	MIPI_FREQ_594M,
};

/* Write registers up to 4 at a time */
static int imx347_write_reg(struct i2c_client *client, u16 reg,
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

static int imx347_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = imx347_write_reg(client, regs[i].addr,
				       IMX347_REG_VALUE_08BIT, regs[i].val);
	}
	return ret;
}

/* Read registers up to 4 at a time */
static int imx347_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static int imx347_get_reso_dist(const struct imx347_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct imx347_mode *
imx347_find_best_fit(struct imx347 *imx347, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < imx347->cfg_num; i++) {
		dist = imx347_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
			supported_modes[i].bus_fmt == framefmt->code) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int imx347_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx347 *imx347 = to_imx347(sd);
	const struct imx347_mode *mode;
	s64 h_blank, vblank_def;
	struct device *dev = &imx347->client->dev;
	int ret = 0;

	mutex_lock(&imx347->mutex);

	mode = imx347_find_best_fit(imx347, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&imx347->mutex);
		return -ENOTTY;
#endif
	} else {
		imx347->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(imx347->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(imx347->vblank, vblank_def,
					 IMX347_VTS_MAX - mode->height,
					 1, vblank_def);
		imx347->cur_vts = imx347->cur_mode->vts_def;
		if (mode->bus_fmt == MEDIA_BUS_FMT_SRGGB10_1X10) {
			if (mode->hdr_mode == NO_HDR)
				imx347->cur_pixel_rate = IMX347_10BIT_LINEAR_PIXEL_RATE;
			else if (mode->hdr_mode == HDR_X2)
				imx347->cur_pixel_rate = IMX347_10BIT_HDR2_PIXEL_RATE;
			imx347->cur_link_freq = 1;
			clk_disable_unprepare(imx347->xvclk);
			ret = clk_set_rate(imx347->xvclk, IMX347_XVCLK_FREQ_37M);
			if (ret < 0)
				dev_err(dev, "Failed to set xvclk rate\n");
			if (clk_get_rate(imx347->xvclk) != IMX347_XVCLK_FREQ_37M)
				dev_err(dev, "xvclk mismatched\n");
			ret = clk_prepare_enable(imx347->xvclk);
			if (ret < 0)
				dev_err(dev, "Failed to enable xvclk\n");
		} else {
			imx347->cur_pixel_rate = IMX347_12BIT_PIXEL_RATE;
			imx347->cur_link_freq = 0;
			clk_disable_unprepare(imx347->xvclk);
			ret = clk_set_rate(imx347->xvclk, IMX347_XVCLK_FREQ_24M);
			if (ret < 0)
				dev_err(dev, "Failed to set xvclk rate\n");
			if (clk_get_rate(imx347->xvclk) != IMX347_XVCLK_FREQ_24M)
				dev_err(dev, "xvclk mismatched\n");
			ret = clk_prepare_enable(imx347->xvclk);
			if (ret < 0)
				dev_err(dev, "Failed to enable xvclk\n");
		}
		__v4l2_ctrl_s_ctrl_int64(imx347->pixel_rate,
					 imx347->cur_pixel_rate);
		__v4l2_ctrl_s_ctrl(imx347->link_freq,
				   imx347->cur_link_freq);
	}

	mutex_unlock(&imx347->mutex);

	return 0;
}

static int imx347_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx347 *imx347 = to_imx347(sd);
	const struct imx347_mode *mode = imx347->cur_mode;

	mutex_lock(&imx347->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&imx347->mutex);
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
	mutex_unlock(&imx347->mutex);

	return 0;
}

static int imx347_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx347 *imx347 = to_imx347(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = imx347->cur_mode->bus_fmt;

	return 0;
}

static int imx347_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx347 *imx347 = to_imx347(sd);

	if (fse->index >= imx347->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int imx347_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct imx347 *imx347 = to_imx347(sd);
	const struct imx347_mode *mode = imx347->cur_mode;

	mutex_lock(&imx347->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&imx347->mutex);

	return 0;
}

static int imx347_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct imx347 *imx347 = to_imx347(sd);
	const struct imx347_mode *mode = imx347->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR) {
		if (mode->bus_fmt == MEDIA_BUS_FMT_SRGGB10_1X10)
			val = 1 << (IMX347_2LANES - 1) |
			V4L2_MBUS_CSI2_CHANNEL_0 |
			V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
		else
			val = 1 << (IMX347_4LANES - 1) |
			V4L2_MBUS_CSI2_CHANNEL_0 |
			V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	}
	if (mode->hdr_mode == HDR_X2)
		val = 1 << (IMX347_4LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		V4L2_MBUS_CSI2_CHANNEL_1;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void imx347_get_module_inf(struct imx347 *imx347,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, IMX347_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, imx347->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, imx347->len_name, sizeof(inf->base.lens));
}

static int imx347_set_hdrae(struct imx347 *imx347,
			    struct preisp_hdrae_exp_s *ae)
{
	struct i2c_client *client = imx347->client;
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_a_gain, m_a_gain, s_a_gain;
	u32 gain_switch = 0;
	u32 shr1 = 0;
	u32 shr0 = 0;
	u32 rhs1 = 0;
	u32 rhs1_max = 0;
	static int rhs1_old = 209;
	int rhs1_change_limit;
	int ret = 0;
	u32 fsc = imx347->cur_vts;
	u8 cg_mode = 0;

	if (!imx347->has_init_exp && !imx347->streaming) {
		imx347->init_hdrae_exp = *ae;
		imx347->has_init_exp = true;
		dev_dbg(&imx347->client->dev, "imx347 don't stream, record exp for hdr!\n");
		return ret;
	}
	l_exp_time = ae->long_exp_reg;
	m_exp_time = ae->middle_exp_reg;
	s_exp_time = ae->short_exp_reg;
	l_a_gain = ae->long_gain_reg;
	m_a_gain = ae->middle_gain_reg;
	s_a_gain = ae->short_gain_reg;
	dev_dbg(&client->dev,
		"rev exp req: L_exp: 0x%x, 0x%x, M_exp: 0x%x, 0x%x S_exp: 0x%x, 0x%x\n",
		l_exp_time, m_exp_time, s_exp_time,
		l_a_gain, m_a_gain, s_a_gain);

	if (imx347->cur_mode->hdr_mode == HDR_X2) {
		//2 stagger
		l_a_gain = m_a_gain;
		l_exp_time = m_exp_time;
		cg_mode = ae->middle_cg_mode;
	}
	if (!g_isHCG && cg_mode == GAIN_MODE_HCG) {
		gain_switch = 0x01 | 0x100;
		g_isHCG = true;
	} else if (g_isHCG && cg_mode == GAIN_MODE_LCG) {
		gain_switch = 0x00 | 0x100;
		g_isHCG = false;
	}
	ret = imx347_write_reg(client,
		IMX347_GROUP_HOLD_REG,
		IMX347_REG_VALUE_08BIT,
		IMX347_GROUP_HOLD_START);
	//gain effect n+1
	ret |= imx347_write_reg(client,
		IMX347_LF_GAIN_REG_H,
		IMX347_REG_VALUE_08BIT,
		IMX347_FETCH_GAIN_H(l_a_gain));
	ret |= imx347_write_reg(client,
		IMX347_LF_GAIN_REG_L,
		IMX347_REG_VALUE_08BIT,
		IMX347_FETCH_GAIN_L(l_a_gain));
	ret |= imx347_write_reg(client,
		IMX347_SF1_GAIN_REG_H,
		IMX347_REG_VALUE_08BIT,
		IMX347_FETCH_GAIN_H(s_a_gain));
	ret |= imx347_write_reg(client,
		IMX347_SF1_GAIN_REG_L,
		IMX347_REG_VALUE_08BIT,
		IMX347_FETCH_GAIN_L(s_a_gain));
	if (gain_switch & 0x100)
		ret |= imx347_write_reg(client,
			IMX347_GAIN_SWITCH_REG,
			IMX347_REG_VALUE_08BIT,
			gain_switch & 0xff);

	//long exposure and short exposure
	shr0 = fsc - l_exp_time;
	rhs1_max = (RHS1_MAX > (shr0 - 9)) ? (shr0 - 9) : RHS1_MAX;
	rhs1 = SHR1_MIN + s_exp_time;
	dev_dbg(&client->dev, "line(%d) rhs1 %d\n", __LINE__, rhs1);
	if (rhs1 < 13)
		rhs1 = 13;
	else if (rhs1 > rhs1_max)
		rhs1 = rhs1_max;
	dev_dbg(&client->dev, "line(%d) rhs1 %d\n", __LINE__, rhs1);

	//Dynamic adjustment rhs1 must meet the following conditions
	rhs1_change_limit = rhs1_old + 2 * BRL - fsc + 2;
	rhs1_change_limit = (rhs1_change_limit < 13) ?  13 : rhs1_change_limit;
	if (rhs1 < rhs1_change_limit)
		rhs1 = rhs1_change_limit;

	dev_dbg(&client->dev,
		"line(%d) rhs1 %d,short time %d rhs1_old %d test %d\n",
		__LINE__, rhs1, s_exp_time, rhs1_old,
		(rhs1_old + 2 * BRL - fsc + 2));

	rhs1 = (rhs1 >> 2) * 4 + 1;
	rhs1_old = rhs1;

	if (rhs1 < s_exp_time) {
		shr1 = 9;
		s_exp_time = rhs1 - shr1;
	} else {
		shr1 = rhs1 - s_exp_time;
	}

	if (shr1 < 9)
		shr1 = 9;
	else if (shr1 > (rhs1 - 2))
		shr1 = rhs1 - 2;

	if (shr0 < (rhs1 + 9))
		shr0 = rhs1 + 9;
	else if (shr0 > (fsc - 2))
		shr0 = fsc - 2;

	dev_dbg(&client->dev,
		"fsc=%d,RHS1_MAX=%d,SHR1_MIN=%d,rhs1_max=%d\n",
		fsc, RHS1_MAX, SHR1_MIN, rhs1_max);
	dev_dbg(&client->dev,
		"l_exp_time=%d,s_exp_time=%d,shr0=%d,shr1=%d,rhs1=%d,l_a_gain=%d,s_a_gain=%d\n",
		l_exp_time, s_exp_time, shr0, shr1, rhs1, l_a_gain, s_a_gain);
	//time effect n+2
	ret |= imx347_write_reg(client,
		IMX347_RHS1_REG_L,
		IMX347_REG_VALUE_08BIT,
		IMX347_FETCH_RHS1_L(rhs1));
	ret |= imx347_write_reg(client,
		IMX347_RHS1_REG_M,
		IMX347_REG_VALUE_08BIT,
		IMX347_FETCH_RHS1_M(rhs1));
	ret |= imx347_write_reg(client,
		IMX347_RHS1_REG_H,
		IMX347_REG_VALUE_08BIT,
		IMX347_FETCH_RHS1_H(rhs1));

	ret |= imx347_write_reg(client,
		IMX347_SF1_EXPO_REG_L,
		IMX347_REG_VALUE_08BIT,
		IMX347_FETCH_EXP_L(shr1));
	ret |= imx347_write_reg(client,
		IMX347_SF1_EXPO_REG_M,
		IMX347_REG_VALUE_08BIT,
		IMX347_FETCH_EXP_M(shr1));
	ret |= imx347_write_reg(client,
		IMX347_SF1_EXPO_REG_H,
		IMX347_REG_VALUE_08BIT,
		IMX347_FETCH_EXP_H(shr1));
	ret |= imx347_write_reg(client,
		IMX347_LF_EXPO_REG_L,
		IMX347_REG_VALUE_08BIT,
		IMX347_FETCH_EXP_L(shr0));
	ret |= imx347_write_reg(client,
		IMX347_LF_EXPO_REG_M,
		IMX347_REG_VALUE_08BIT,
		IMX347_FETCH_EXP_M(shr0));
	ret |= imx347_write_reg(client,
		IMX347_LF_EXPO_REG_H,
		IMX347_REG_VALUE_08BIT,
		IMX347_FETCH_EXP_H(shr0));
	ret |= imx347_write_reg(client,
		IMX347_GROUP_HOLD_REG,
		IMX347_REG_VALUE_08BIT,
		IMX347_GROUP_HOLD_END);
	return ret;
}

static int imx347_set_conversion_gain(struct imx347 *imx347, u32 *cg)
{
	int ret = 0;
	struct i2c_client *client = imx347->client;
	int cur_cg = *cg;
	u32 gain_switch = 0;

	if (g_isHCG && cur_cg == GAIN_MODE_LCG) {
		gain_switch = 0x00 | 0x100;
		g_isHCG = false;
	} else if (!g_isHCG && cur_cg == GAIN_MODE_HCG) {
		gain_switch = 0x01 | 0x100;
		g_isHCG = true;
	}
	ret = imx347_write_reg(client,
			IMX347_GROUP_HOLD_REG,
			IMX347_REG_VALUE_08BIT,
			IMX347_GROUP_HOLD_START);
	if (gain_switch & 0x100)
		ret |= imx347_write_reg(client,
			IMX347_GAIN_SWITCH_REG,
			IMX347_REG_VALUE_08BIT,
			gain_switch & 0xff);
	ret |= imx347_write_reg(client,
			IMX347_GROUP_HOLD_REG,
			IMX347_REG_VALUE_08BIT,
			IMX347_GROUP_HOLD_END);
	return ret;
}

#ifdef USED_SYS_DEBUG
//ag: echo 0 >  /sys/devices/platform/ff510000.i2c/i2c-1/1-0037/cam_s_cg
static ssize_t set_conversion_gain_status(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx347 *imx347 = to_imx347(sd);
	int status = 0;
	int ret = 0;

	ret = kstrtoint(buf, 0, &status);
	if (!ret && status >= 0 && status < 2)
		imx347_set_conversion_gain(imx347, &status);
	else
		dev_err(dev, "input 0 for LCG, 1 for HCG, cur %d\n", status);
	return count;
}

static struct device_attribute attributes[] = {
	__ATTR(cam_s_cg, S_IWUSR, NULL, set_conversion_gain_status),
};

static int add_sysfs_interfaces(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(attributes); i++)
		if (device_create_file(dev, attributes + i))
			goto undo;
	return 0;
undo:
	for (i--; i >= 0 ; i--)
		device_remove_file(dev, attributes + i);
	dev_err(dev, "%s: failed to create sysfs interface\n", __func__);
	return -ENODEV;
}
#endif

static long imx347_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct imx347 *imx347 = to_imx347(sd);
	struct rkmodule_hdr_cfg *hdr;
	u32 i, h, w, stream;
	long ret = 0;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		ret = imx347_set_hdrae(imx347, arg);
		break;
	case RKMODULE_GET_MODULE_INFO:
		imx347_get_module_inf(imx347, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = imx347->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = imx347->cur_mode->width;
		h = imx347->cur_mode->height;
		for (i = 0; i < imx347->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				imx347->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == imx347->cfg_num) {
			dev_err(&imx347->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = imx347->cur_mode->hts_def - imx347->cur_mode->width;
			h = imx347->cur_mode->vts_def - imx347->cur_mode->height;
			__v4l2_ctrl_modify_range(imx347->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(imx347->vblank, h,
				IMX347_VTS_MAX - imx347->cur_mode->height,
				1, h);
			imx347->cur_vts = imx347->cur_mode->vts_def;
			if (imx347->cur_mode->bus_fmt == MEDIA_BUS_FMT_SRGGB10_1X10) {
				if (imx347->cur_mode->hdr_mode == NO_HDR)
					imx347->cur_pixel_rate = IMX347_10BIT_LINEAR_PIXEL_RATE;
				else if (imx347->cur_mode->hdr_mode == HDR_X2)
					imx347->cur_pixel_rate = IMX347_10BIT_HDR2_PIXEL_RATE;
				__v4l2_ctrl_s_ctrl_int64(imx347->pixel_rate,
							 imx347->cur_pixel_rate);
			}
		}
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		ret = imx347_set_conversion_gain(imx347, (u32 *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = imx347_write_reg(imx347->client, IMX347_REG_CTRL_MODE,
				IMX347_REG_VALUE_08BIT, IMX347_MODE_STREAMING);
		else
			ret = imx347_write_reg(imx347->client, IMX347_REG_CTRL_MODE,
				IMX347_REG_VALUE_08BIT, IMX347_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long imx347_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	long ret;
	u32 cg = 0;
	u32  stream;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx347_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx347_ioctl(sd, cmd, hdr);
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

		if (copy_from_user(hdr, up, sizeof(*hdr))) {
			kfree(hdr);
			return -EFAULT;
		}

		ret = imx347_ioctl(sd, cmd, hdr);
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

		ret = imx347_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		if (copy_from_user(&cg, up, sizeof(cg)))
			return -EFAULT;

		ret = imx347_ioctl(sd, cmd, &cg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;

		ret = imx347_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int imx347_init_conversion_gain(struct imx347 *imx347)
{
	int ret = 0;
	struct i2c_client *client = imx347->client;

	ret = imx347_write_reg(client, IMX347_GAIN_SWITCH_REG,
			       IMX347_REG_VALUE_08BIT, 0x00);
	if (!ret)
		g_isHCG = false;
	return ret;
}

static int __imx347_start_stream(struct imx347 *imx347)
{
	int ret;

	ret = imx347_write_array(imx347->client, imx347->cur_mode->reg_list);
	if (ret)
		return ret;
	ret = imx347_init_conversion_gain(imx347);
	if (ret)
		return ret;
	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&imx347->ctrl_handler);
	if (ret)
		return ret;
	if (imx347->has_init_exp && imx347->cur_mode->hdr_mode != NO_HDR) {
		ret = imx347_ioctl(&imx347->subdev, PREISP_CMD_SET_HDRAE_EXP,
			&imx347->init_hdrae_exp);
		if (ret) {
			dev_err(&imx347->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}

	ret = imx347_write_reg(imx347->client, IMX347_REG_CTRL_MODE,
			       IMX347_REG_VALUE_08BIT, IMX347_MODE_STREAMING);
	ret |= imx347_write_reg(imx347->client, IMX347_REG_MASTER_MODE,
				IMX347_REG_VALUE_08BIT, IMX347_MASTER_MODE_START);
	return ret;
}

static int __imx347_stop_stream(struct imx347 *imx347)
{
	int ret = 0;
	u32 value = 0;

	imx347->has_init_exp = false;
	ret = imx347_write_reg(imx347->client, IMX347_REG_CTRL_MODE,
			       IMX347_REG_VALUE_08BIT, IMX347_MODE_SW_STANDBY);
	ret |= imx347_write_reg(imx347->client, IMX347_REG_MASTER_MODE,
				IMX347_REG_VALUE_08BIT, IMX347_MASTER_MODE_STOP);

	ret |= imx347_read_reg(imx347->client, IMX347_REG_RESTART_MODE,
			       IMX347_REG_VALUE_08BIT, &value);
	dev_dbg(&imx347->client->dev, "reg 0x3004 = 0x%x\n", value);
	if (value == 0x00) {
		ret |= imx347_write_reg(imx347->client, IMX347_REG_RESTART_MODE,
					IMX347_REG_VALUE_08BIT, IMX347_RESTART_MODE_START);
		ret |= imx347_write_reg(imx347->client, IMX347_REG_RESTART_MODE,
					IMX347_REG_VALUE_08BIT, IMX347_RESTART_MODE_STOP);
	}

	return ret;
}

static int imx347_s_stream(struct v4l2_subdev *sd, int on)
{
	struct imx347 *imx347 = to_imx347(sd);
	struct i2c_client *client = imx347->client;
	int ret = 0;

	dev_dbg(&imx347->client->dev, "s_stream: %d. %dx%d, hdr: %d, bpp: %d\n",
		on, imx347->cur_mode->width, imx347->cur_mode->height,
		imx347->cur_mode->hdr_mode, imx347->cur_mode->bpp);

	mutex_lock(&imx347->mutex);
	on = !!on;
	if (on == imx347->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __imx347_start_stream(imx347);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__imx347_stop_stream(imx347);
		pm_runtime_put(&client->dev);
	}

	imx347->streaming = on;

unlock_and_return:
	mutex_unlock(&imx347->mutex);

	return ret;
}

static int imx347_s_power(struct v4l2_subdev *sd, int on)
{
	struct imx347 *imx347 = to_imx347(sd);
	struct i2c_client *client = imx347->client;
	int ret = 0;

	mutex_lock(&imx347->mutex);

	/* If the power state is not modified - no work to do. */
	if (imx347->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = imx347_write_array(imx347->client, imx347_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		imx347->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		imx347->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&imx347->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 imx347_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, IMX347_XVCLK_FREQ_37M / 1000 / 1000);
}

static int __imx347_power_on(struct imx347 *imx347)
{
	int ret;
	u32 delay_us;
	struct device *dev = &imx347->client->dev;
	unsigned long mclk = 0;

	if (!IS_ERR_OR_NULL(imx347->pins_default)) {
		ret = pinctrl_select_state(imx347->pinctrl,
					   imx347->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	if (imx347->cur_mode->bus_fmt == MEDIA_BUS_FMT_SRGGB10_1X10)
		mclk = IMX347_XVCLK_FREQ_37M;
	else
		mclk = IMX347_XVCLK_FREQ_24M;
	ret = clk_set_rate(imx347->xvclk, mclk);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate\n");
	if (clk_get_rate(imx347->xvclk) != mclk)
		dev_warn(dev, "xvclk mismatched\n");
	ret = clk_prepare_enable(imx347->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(imx347->reset_gpio))
		gpiod_set_value_cansleep(imx347->reset_gpio, 0);

	ret = regulator_bulk_enable(IMX347_NUM_SUPPLIES, imx347->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(imx347->reset_gpio))
		gpiod_set_value_cansleep(imx347->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(imx347->pwdn_gpio))
		gpiod_set_value_cansleep(imx347->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = imx347_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(imx347->xvclk);

	return ret;
}

static void __imx347_power_off(struct imx347 *imx347)
{
	int ret;
	struct device *dev = &imx347->client->dev;

	if (!IS_ERR(imx347->pwdn_gpio))
		gpiod_set_value_cansleep(imx347->pwdn_gpio, 0);
	clk_disable_unprepare(imx347->xvclk);
	if (!IS_ERR(imx347->reset_gpio))
		gpiod_set_value_cansleep(imx347->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(imx347->pins_sleep)) {
		ret = pinctrl_select_state(imx347->pinctrl,
					   imx347->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(IMX347_NUM_SUPPLIES, imx347->supplies);
}

static int imx347_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx347 *imx347 = to_imx347(sd);

	return __imx347_power_on(imx347);
}

static int imx347_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx347 *imx347 = to_imx347(sd);

	__imx347_power_off(imx347);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int imx347_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx347 *imx347 = to_imx347(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct imx347_mode *def_mode = &supported_modes[0];

	mutex_lock(&imx347->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&imx347->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int imx347_enum_frame_interval(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_frame_interval_enum *fie)
{
	struct imx347 *imx347 = to_imx347(sd);

	if (fie->index >= imx347->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

#define CROP_START(SRC, DST) (((SRC) - (DST)) / 2 / 4 * 4)
#define DST_WIDTH 2688
#define DST_HEIGHT 1520

/*
 * The resolution of the driver configuration needs to be exactly
 * the same as the current output resolution of the sensor,
 * the input width of the isp needs to be 16 aligned,
 * the input height of the isp needs to be 8 aligned.
 * Can be cropped to standard resolution by this function,
 * otherwise it will crop out strange resolution according
 * to the alignment rules.
 */
static int imx347_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct imx347 *imx347 = to_imx347(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = CROP_START(imx347->cur_mode->width, DST_WIDTH);
		sel->r.width = DST_WIDTH;
		sel->r.top = CROP_START(imx347->cur_mode->height, DST_HEIGHT);
		sel->r.height = DST_HEIGHT;
		return 0;
	}
	return -EINVAL;
}

static const struct dev_pm_ops imx347_pm_ops = {
	SET_RUNTIME_PM_OPS(imx347_runtime_suspend,
			   imx347_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops imx347_internal_ops = {
	.open = imx347_open,
};
#endif

static const struct v4l2_subdev_core_ops imx347_core_ops = {
	.s_power = imx347_s_power,
	.ioctl = imx347_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = imx347_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops imx347_video_ops = {
	.s_stream = imx347_s_stream,
	.g_frame_interval = imx347_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops imx347_pad_ops = {
	.enum_mbus_code = imx347_enum_mbus_code,
	.enum_frame_size = imx347_enum_frame_sizes,
	.enum_frame_interval = imx347_enum_frame_interval,
	.get_fmt = imx347_get_fmt,
	.set_fmt = imx347_set_fmt,
	.get_selection = imx347_get_selection,
	.get_mbus_config = imx347_g_mbus_config,
};

static const struct v4l2_subdev_ops imx347_subdev_ops = {
	.core	= &imx347_core_ops,
	.video	= &imx347_video_ops,
	.pad	= &imx347_pad_ops,
};

static int imx347_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx347 *imx347 = container_of(ctrl->handler,
					     struct imx347, ctrl_handler);
	struct i2c_client *client = imx347->client;
	const struct imx347_mode *mode = imx347->cur_mode;
	s64 max;
	u32 vts = 0;
	int ret = 0;
	u32 shr0 = 0;
	u32 flip = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		if (mode->hdr_mode == NO_HDR) {
			max = imx347->cur_mode->height + ctrl->val - 3;
			__v4l2_ctrl_modify_range(imx347->exposure,
						 imx347->exposure->minimum, max,
						 imx347->exposure->step,
						 imx347->exposure->default_value);
		}
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (mode->hdr_mode == NO_HDR) {
			shr0 = imx347->cur_vts - ctrl->val;
			ret = imx347_write_reg(imx347->client, IMX347_LF_EXPO_REG_L,
					IMX347_REG_VALUE_08BIT,
					IMX347_FETCH_EXP_L(shr0));
			ret |= imx347_write_reg(imx347->client, IMX347_LF_EXPO_REG_M,
					IMX347_REG_VALUE_08BIT,
					IMX347_FETCH_EXP_M(shr0));
			ret |= imx347_write_reg(imx347->client, IMX347_LF_EXPO_REG_H,
					IMX347_REG_VALUE_08BIT,
					IMX347_FETCH_EXP_H(shr0));
			dev_dbg(&client->dev, "set exposure 0x%x\n",
				ctrl->val);
		}
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (mode->hdr_mode == NO_HDR) {
			ret = imx347_write_reg(imx347->client, IMX347_LF_GAIN_REG_H,
					IMX347_REG_VALUE_08BIT,
					IMX347_FETCH_GAIN_H(ctrl->val));
			ret |= imx347_write_reg(imx347->client, IMX347_LF_GAIN_REG_L,
					IMX347_REG_VALUE_08BIT,
					IMX347_FETCH_GAIN_L(ctrl->val));
			dev_dbg(&client->dev, "set analog gain 0x%x\n",
				ctrl->val);
		}
		break;
	case V4L2_CID_VBLANK:
		vts = ctrl->val + imx347->cur_mode->height;
		imx347->cur_vts = vts;
		if (imx347->cur_mode->hdr_mode == HDR_X2)
			vts /= 2;
		ret = imx347_write_reg(imx347->client, IMX347_VTS_REG_L,
				       IMX347_REG_VALUE_08BIT,
				       IMX347_FETCH_VTS_L(vts));
		ret |= imx347_write_reg(imx347->client, IMX347_VTS_REG_M,
				       IMX347_REG_VALUE_08BIT,
				       IMX347_FETCH_VTS_M(vts));
		ret |= imx347_write_reg(imx347->client, IMX347_VTS_REG_H,
				       IMX347_REG_VALUE_08BIT,
				       IMX347_FETCH_VTS_H(vts));

		dev_dbg(&client->dev, "set vblank 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = imx347_write_reg(imx347->client, IMX347_HREVERSE_REG,
				       IMX347_REG_VALUE_08BIT, !!ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		flip = ctrl->val;
		ret = imx347_write_reg(imx347->client, IMX347_VREVERSE_REG,
				IMX347_REG_VALUE_08BIT, !!flip);
		if (flip) {
			ret |= imx347_write_reg(imx347->client, 0x3074,
				IMX347_REG_VALUE_08BIT, 0x40);
			ret |= imx347_write_reg(imx347->client, 0x3075,
				IMX347_REG_VALUE_08BIT, 0x06);
			ret |= imx347_write_reg(imx347->client, 0x3080,
				IMX347_REG_VALUE_08BIT, 0xff);
			ret |= imx347_write_reg(imx347->client, 0x30ad,
				IMX347_REG_VALUE_08BIT, 0x7e);
			ret |= imx347_write_reg(imx347->client, 0x30b6,
				IMX347_REG_VALUE_08BIT, 0xff);
			ret |= imx347_write_reg(imx347->client, 0x30b7,
				IMX347_REG_VALUE_08BIT, 0x01);
			ret |= imx347_write_reg(imx347->client, 0x30d8,
				IMX347_REG_VALUE_08BIT, 0x45);
			ret |= imx347_write_reg(imx347->client, 0x3114,
				IMX347_REG_VALUE_08BIT, 0x01);
		} else {
			ret |= imx347_write_reg(imx347->client, 0x3074,
				IMX347_REG_VALUE_08BIT, 0x3c);
			ret |= imx347_write_reg(imx347->client, 0x3075,
				IMX347_REG_VALUE_08BIT, 0x00);
			ret |= imx347_write_reg(imx347->client, 0x3080,
				IMX347_REG_VALUE_08BIT, 0x01);
			ret |= imx347_write_reg(imx347->client, 0x30ad,
				IMX347_REG_VALUE_08BIT, 0x02);
			ret |= imx347_write_reg(imx347->client, 0x30b6,
				IMX347_REG_VALUE_08BIT, 0x00);
			ret |= imx347_write_reg(imx347->client, 0x30b7,
				IMX347_REG_VALUE_08BIT, 0x00);
			ret |= imx347_write_reg(imx347->client, 0x30d8,
				IMX347_REG_VALUE_08BIT, 0x44);
			ret |= imx347_write_reg(imx347->client, 0x3114,
				IMX347_REG_VALUE_08BIT, 0x02);
		}
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops imx347_ctrl_ops = {
	.s_ctrl = imx347_set_ctrl,
};

static int imx347_initialize_controls(struct imx347 *imx347)
{
	const struct imx347_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &imx347->ctrl_handler;
	mode = imx347->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &imx347->mutex;

	imx347->link_freq = v4l2_ctrl_new_int_menu(handler,
				NULL, V4L2_CID_LINK_FREQ,
				1, 0, link_freq_menu_items);
	if (imx347->cur_mode->bus_fmt == MEDIA_BUS_FMT_SRGGB10_1X10) {
		imx347->cur_link_freq = 1;
		if (imx347->cur_mode->hdr_mode == NO_HDR)
			imx347->cur_pixel_rate =
				IMX347_10BIT_LINEAR_PIXEL_RATE;
		else if (imx347->cur_mode->hdr_mode == HDR_X2)
			imx347->cur_pixel_rate =
				IMX347_10BIT_HDR2_PIXEL_RATE;
	} else {
		imx347->cur_link_freq = 0;
		imx347->cur_pixel_rate =
				IMX347_12BIT_PIXEL_RATE;
	}
	__v4l2_ctrl_s_ctrl(imx347->link_freq,
			 imx347->cur_link_freq);
	imx347->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
		V4L2_CID_PIXEL_RATE, 0, IMX347_10BIT_HDR2_PIXEL_RATE,
		1, imx347->cur_pixel_rate);

	h_blank = mode->hts_def - mode->width;
	imx347->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (imx347->hblank)
		imx347->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	imx347->vblank = v4l2_ctrl_new_std(handler, &imx347_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				IMX347_VTS_MAX - mode->height,
				1, vblank_def);
	imx347->cur_vts = mode->vts_def;

	exposure_max = mode->vts_def - 3;
	imx347->exposure = v4l2_ctrl_new_std(handler, &imx347_ctrl_ops,
				V4L2_CID_EXPOSURE, IMX347_EXPOSURE_MIN,
				exposure_max, IMX347_EXPOSURE_STEP,
				mode->exp_def);

	imx347->anal_a_gain = v4l2_ctrl_new_std(handler, &imx347_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, IMX347_GAIN_MIN,
				IMX347_GAIN_MAX, IMX347_GAIN_STEP,
				IMX347_GAIN_DEFAULT);
	v4l2_ctrl_new_std(handler, &imx347_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &imx347_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&imx347->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	imx347->subdev.ctrl_handler = handler;
	imx347->has_init_exp = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int imx347_check_sensor_id(struct imx347 *imx347,
				  struct i2c_client *client)
{
	struct device *dev = &imx347->client->dev;
	u32 id = 0;
	int ret;

	ret = imx347_read_reg(client, IMX347_REG_CHIP_ID,
			      IMX347_REG_VALUE_08BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected imx347 id %06x\n", CHIP_ID);

	return 0;
}

static int imx347_configure_regulators(struct imx347 *imx347)
{
	unsigned int i;

	for (i = 0; i < IMX347_NUM_SUPPLIES; i++)
		imx347->supplies[i].supply = imx347_supply_names[i];

	return devm_regulator_bulk_get(&imx347->client->dev,
				       IMX347_NUM_SUPPLIES,
				       imx347->supplies);
}

static int imx347_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct imx347 *imx347;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	imx347 = devm_kzalloc(dev, sizeof(*imx347), GFP_KERNEL);
	if (!imx347)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &imx347->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &imx347->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &imx347->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &imx347->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE,
			&hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}
	imx347->client = client;
	imx347->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < imx347->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			imx347->cur_mode = &supported_modes[i];
			break;
		}
	}

	imx347->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(imx347->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	imx347->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(imx347->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	imx347->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(imx347->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	imx347->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(imx347->pinctrl)) {
		imx347->pins_default =
			pinctrl_lookup_state(imx347->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(imx347->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		imx347->pins_sleep =
			pinctrl_lookup_state(imx347->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(imx347->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = imx347_configure_regulators(imx347);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&imx347->mutex);

	sd = &imx347->subdev;
	v4l2_i2c_subdev_init(sd, client, &imx347_subdev_ops);
	ret = imx347_initialize_controls(imx347);
	if (ret)
		goto err_destroy_mutex;

	ret = __imx347_power_on(imx347);
	if (ret)
		goto err_free_handler;

	ret = imx347_check_sensor_id(imx347, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &imx347_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	imx347->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &imx347->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(imx347->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 imx347->module_index, facing,
		 IMX347_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	g_isHCG = false;
#ifdef USED_SYS_DEBUG
	add_sysfs_interfaces(dev);
#endif
	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__imx347_power_off(imx347);
err_free_handler:
	v4l2_ctrl_handler_free(&imx347->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&imx347->mutex);

	return ret;
}

static int imx347_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx347 *imx347 = to_imx347(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&imx347->ctrl_handler);
	mutex_destroy(&imx347->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__imx347_power_off(imx347);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id imx347_of_match[] = {
	{ .compatible = "sony,imx347" },
	{},
};
MODULE_DEVICE_TABLE(of, imx347_of_match);
#endif

static const struct i2c_device_id imx347_match_id[] = {
	{ "sony,imx347", 0 },
	{ },
};

static struct i2c_driver imx347_i2c_driver = {
	.driver = {
		.name = IMX347_NAME,
		.pm = &imx347_pm_ops,
		.of_match_table = of_match_ptr(imx347_of_match),
	},
	.probe		= &imx347_probe,
	.remove		= &imx347_remove,
	.id_table	= imx347_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&imx347_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&imx347_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Sony imx347 sensor driver");
MODULE_LICENSE("GPL v2");
