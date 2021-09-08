// SPDX-License-Identifier: GPL-2.0
/*
 * imx334 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 * V0.0X01.0X03 add enum_frame_interval function.
 * V0.0X01.0X04
 *	1.add parse mclk pinctrl.
 *	2.add set flip ctrl.
 * V0.0X01.0X05 add quick stream on/off
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
#include <media/v4l2-fwnode.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/mfd/syscon.h>
#include <linux/rk-preisp.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x05)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define IMX334_LINK_FREQ_445		445500000// 891Mbps
#define IMX334_LINK_FREQ_594		594000000// 1188Mbps
#define IMX334_LINK_FREQ_891		891000000// 1782Mbps

#define IMX334_LANES			4

#define PIXEL_RATE_WITH_445M_10BIT	(IMX334_LINK_FREQ_445 * 2 / 10 * 4)
#define PIXEL_RATE_WITH_594M_12BIT	(IMX334_LINK_FREQ_594 * 2 / 12 * 4)
#define PIXEL_RATE_WITH_891M_10BIT	(IMX334_LINK_FREQ_891 * 2 / 10 * 4)
#define PIXEL_RATE_WITH_891M_12BIT	(IMX334_LINK_FREQ_891 * 2 / 12 * 4)

#define IMX334_XVCLK_FREQ_37		37125000
#define IMX334_XVCLK_FREQ_74		74250000

#define CHIP_ID				0x30
#define IMX334_REG_CHIP_ID		0x302c

#define IMX334_REG_CTRL_MODE		0x3000
#define IMX334_MODE_SW_STANDBY		0x1
#define IMX334_MODE_STREAMING		0x0

#define IMX334_LF_GAIN_REG_L		0x30E8

#define IMX334_SF1_GAIN_REG_L		0x30EA

#define IMX334_LF_EXPO_REG_H		0x305A
#define IMX334_LF_EXPO_REG_M		0x3059
#define IMX334_LF_EXPO_REG_L		0x3058

#define IMX334_SF1_EXPO_REG_H		0x305E
#define IMX334_SF1_EXPO_REG_M		0x305D
#define IMX334_SF1_EXPO_REG_L		0x305C

#define IMX334_RHS1_REG_H		0x306a
#define IMX334_RHS1_REG_M		0x3069
#define IMX334_RHS1_REG_L		0x3068

#define	IMX334_EXPOSURE_MIN		5
#define	IMX334_EXPOSURE_STEP		1
#define IMX334_VTS_MAX			0xfffff

#define IMX334_REG_GAIN			0x30e8
#define IMX334_GAIN_MIN			0x00
#define IMX334_GAIN_MAX			0xf0
#define IMX334_GAIN_STEP		1
#define IMX334_GAIN_DEFAULT		0x30

#define IMX334_REG_TEST_PATTERN	0x5e00
#define	IMX334_TEST_PATTERN_ENABLE	0x80
#define	IMX334_TEST_PATTERN_DISABLE	0x0

#define IMX334_REG_VTS_H		0x3032
#define IMX334_REG_VTS_M		0x3031
#define IMX334_REG_VTS_L		0x3030

#define IMX334_FETCH_EXP_H(VAL)		(((VAL) >> 16) & 0x0F)
#define IMX334_FETCH_EXP_M(VAL)		(((VAL) >> 8) & 0xFF)
#define IMX334_FETCH_EXP_L(VAL)		((VAL) & 0xFF)

#define IMX334_FETCH_RHS1_H(VAL)	(((VAL) >> 16) & 0x0F)
#define IMX334_FETCH_RHS1_M(VAL)	(((VAL) >> 8) & 0xFF)
#define IMX334_FETCH_RHS1_L(VAL)	((VAL) & 0xFF)

#define IMX334_FETCH_VTS_H(VAL)		(((VAL) >> 16) & 0x0F)
#define IMX334_FETCH_VTS_M(VAL)		(((VAL) >> 8) & 0xFF)
#define IMX334_FETCH_VTS_L(VAL)		((VAL) & 0xFF)

#define IMX334_VREVERSE_REG	0x304f
#define IMX334_HREVERSE_REG	0x304e

#define REG_DELAY			0xFFFE
#define REG_NULL			0xFFFF

#define IMX334_REG_VALUE_08BIT		1
#define IMX334_REG_VALUE_16BIT		2
#define IMX334_REG_VALUE_24BIT		3

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"
#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define IMX334_NAME			"imx334"

#define BRL				2200
#define RHS1_MAX			4397 // <2*BRL && 4n+1
#define SHR1_MIN			9

static const char * const imx334_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define IMX334_NUM_SUPPLIES ARRAY_SIZE(imx334_supply_names)

enum imx334_max_pad {
	PAD0, /* link to isp */
	PAD1, /* link to csi wr0 | hdr x2:L x3:M */
	PAD2, /* link to csi wr1 | hdr      x3:L */
	PAD3, /* link to csi wr2 | hdr x2:M x3:S */
	PAD_MAX,
};

struct regval {
	u16 addr;
	u8 val;
};

struct imx334_mode {
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
	u32 vclk_freq;
	u32 bpp;
	u32 mipi_freq_idx;
	u32 vc[PAD_MAX];
};

struct imx334 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[IMX334_NUM_SUPPLIES];

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
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct imx334_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
	u32			cur_vclk_freq;
	u32			cur_mipi_freq_idx;
};

#define to_imx334(sd) container_of(sd, struct imx334, subdev)

static const struct regval imx334_10_3840x2160_global_regs[] = {
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x300C, 0x5B},// BCWAIT_TIME[7:0]
	{0x300D, 0x40},// CPWAIT_TIME[7:0]
	{0x3050, 0x00},// ADBIT[0]
	{0x316A, 0x7E},// INCKSEL4[1:0]
	{0x319D, 0x00},// MDBIT
	{0x31A1, 0x00},// XVS_DRV[1:0]
	{0x3288, 0x21},// -
	{0x328A, 0x02},// -
	{0x3414, 0x05},// -
	{0x3416, 0x18},// -
	{0x341D, 0x01},//
	{0x35AC, 0x0E},// -
	{0x3648, 0x01},// -
	{0x364A, 0x04},// -
	{0x364C, 0x04},// -
	{0x3678, 0x01},// -
	{0x367C, 0x31},// -
	{0x367E, 0x31},// -
	{0x3708, 0x02},// -
	{0x3714, 0x01},// -
	{0x3715, 0x02},// -
	{0x3716, 0x02},// -
	{0x3717, 0x02},// -
	{0x371C, 0x3D},// -
	{0x371D, 0x3F},// -
	{0x372C, 0x00},// -
	{0x372D, 0x00},// -
	{0x372E, 0x46},// -
	{0x372F, 0x00},// -
	{0x3730, 0x89},// -
	{0x3731, 0x00},// -
	{0x3732, 0x08},// -
	{0x3733, 0x01},// -
	{0x3734, 0xFE},// -
	{0x3735, 0x05},// -
	{0x375D, 0x00},// -
	{0x375E, 0x00},// -
	{0x375F, 0x61},// -
	{0x3760, 0x06},// -
	{0x3768, 0x1B},// -
	{0x3769, 0x1B},// -
	{0x376A, 0x1A},// -
	{0x376B, 0x19},// -
	{0x376C, 0x18},// -
	{0x376D, 0x14},// -
	{0x376E, 0x0F},// -
	{0x3776, 0x00},// -
	{0x3777, 0x00},// -
	{0x3778, 0x46},// -
	{0x3779, 0x00},// -
	{0x377A, 0x08},// -
	{0x377B, 0x01},// -
	{0x377C, 0x45},// -
	{0x377D, 0x01},// -
	{0x377E, 0x23},// -
	{0x377F, 0x02},// -
	{0x3780, 0xD9},// -
	{0x3781, 0x03},// -
	{0x3782, 0xF5},// -
	{0x3783, 0x06},// -
	{0x3784, 0xA5},// -
	{0x3788, 0x0F},// -
	{0x378A, 0xD9},// -
	{0x378B, 0x03},// -
	{0x378C, 0xEB},// -
	{0x378D, 0x05},// -
	{0x378E, 0x87},// -
	{0x378F, 0x06},// -
	{0x3790, 0xF5},// -
	{0x3792, 0x43},// -
	{0x3794, 0x7A},// -
	{0x3796, 0xA1},// -
	{0x3E04, 0x0E},// -
	{REG_NULL, 0x00},
};

/*
 *IMX334LQR All-pixel scan CSI-2_4lane 37.125Mhz
 *AD:10bit Output:10bit 891Mbps Master Mode 30fps
 *Tool ver : Ver4.0
 */
static const struct regval imx334_linear_10_3840x2160_regs[] = {
	{0x302E, 0x18},
	{0x302F, 0x0f},
	{0x3030, 0xCA},// VMAX[19:0]
	{0x3031, 0x08},//
	{0x3034, 0x4c},
	{0x3035, 0x04},
	{0x3048, 0x00},// WDMODE[0]
	{0x3049, 0x00},// WDSEL[1:0]
	{0x304A, 0x00},// WD_SET1[2:0]
	{0x304B, 0x01},// WD_SET2[3:0]
	{0x304C, 0x14},// OPB_SIZE_V[5:0]
	{0x3058, 0x05},// SHR0[19:0]
	{0x3059, 0x00},//
	{0x3068, 0x8B},// RHS1[19:0]
	{0x3069, 0x00},//{
	{0x3076, 0x84},
	{0x3077, 0x08},
	{0x315a, 0x06},
	{0x319e, 0x02},
	{0x31D7, 0x00},// XVSMSKCNT_INT[1:0]
	{0x3200, 0x11},// FGAINEN[0]
	{0x341C, 0x47},// ADBIT1[8:0]
	{0x3a18, 0x7f},
	{0x3a1a, 0x37},
	{0x3a1c, 0x37},
	{0x3a1e, 0xf7},
	{0x3a1f, 0x00},
	{0x3a20, 0x3f},
	{0x3a22, 0x6f},
	{0x3a24, 0x3f},
	{0x3a26, 0x5f},
	{0x3a28, 0x2f},
	{REG_NULL, 0x00},
};

/*
 *All-pixel scan CSI-2_4lane 37.125Mhz
 *AD:10bit Output:10bit 1782Mbps Master Mode DOL HDR 2frame VC
 *Tool ver : Ver3.0
 */
static const struct regval imx334_hdr_10_3840x2160_regs[] = {
	{0x302E, 0x18},
	{0x302F, 0x0f},
	{0x3030, 0xC4},// VMAX[19:0]
	{0x3031, 0x09},//
	{0x3034, 0xEF},// HMAX[15:0]
	{0x3035, 0x01},//
	{0x3048, 0x01},// WDMODE[0]
	{0x3049, 0x01},// WDSEL[1:0]
	{0x304A, 0x01},// WD_SET1[2:0]
	{0x304B, 0x02},// WD_SET2[3:0]
	{0x304C, 0x13},// OPB_SIZE_V[5:0]
	{0x3058, 0xD0},// SHR0[19:0]
	{0x3059, 0x07},//
	{0x3068, 0x51},// RHS1[19:0]
	{0x3069, 0x05},//{
	{0x3076, 0x84},
	{0x3077, 0x08},
	{0x315A, 0x02},// INCKSEL2[1:0]
	{0x319E, 0x00},
	{0x31D7, 0x01},// XVSMSKCNT_INT[1:0]
	{0x3200, 0x10},// FGAINEN[0]
	{0x341C, 0xFF},// ADBIT1[8:0]
	{0x3a18, 0xB7},
	{0x3a1a, 0x67},
	{0x3a1c, 0x6F},
	{0x3a1e, 0xf7},
	{0x3a1f, 0xDF},
	{0x3a20, 0x6F},
	{0x3a22, 0xCF},
	{0x3a24, 0x6F},
	{0x3a26, 0xB7},
	{0x3a28, 0x5F},
	{REG_NULL, 0x00},
};

static const struct regval imx334_12_3840x2160_global_regs[] = {
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x31A1, 0x00},// XVS_DRV[1:0]
	{0x3288, 0x21},// -
	{0x328A, 0x02},// -
	{0x3414, 0x05},// -
	{0x3416, 0x18},// -
	{0x35AC, 0x0E},// -
	{0x3648, 0x01},// -
	{0x364A, 0x04},// -
	{0x364C, 0x04},// -
	{0x3678, 0x01},// -
	{0x367C, 0x31},// -
	{0x367E, 0x31},// -
	{0x3708, 0x02},// -
	{0x3714, 0x01},// -
	{0x3715, 0x02},// -
	{0x3716, 0x02},// -
	{0x3717, 0x02},// -
	{0x371C, 0x3D},// -
	{0x371D, 0x3F},// -
	{0x372C, 0x00},// -
	{0x372D, 0x00},// -
	{0x372E, 0x46},// -
	{0x372F, 0x00},// -
	{0x3730, 0x89},// -
	{0x3731, 0x00},// -
	{0x3732, 0x08},// -
	{0x3733, 0x01},// -
	{0x3734, 0xFE},// -
	{0x3735, 0x05},// -
	{0x375D, 0x00},// -
	{0x375E, 0x00},// -
	{0x375F, 0x61},// -
	{0x3760, 0x06},// -
	{0x3768, 0x1B},// -
	{0x3769, 0x1B},// -
	{0x376A, 0x1A},// -
	{0x376B, 0x19},// -
	{0x376C, 0x18},// -
	{0x376D, 0x14},// -
	{0x376E, 0x0F},// -
	{0x3776, 0x00},// -
	{0x3777, 0x00},// -
	{0x3778, 0x46},// -
	{0x3779, 0x00},// -
	{0x377A, 0x08},// -
	{0x377B, 0x01},// -
	{0x377C, 0x45},// -
	{0x377D, 0x01},// -
	{0x377E, 0x23},// -
	{0x377F, 0x02},// -
	{0x3780, 0xD9},// -
	{0x3781, 0x03},// -
	{0x3782, 0xF5},// -
	{0x3783, 0x06},// -
	{0x3784, 0xA5},// -
	{0x3788, 0x0F},// -
	{0x378A, 0xD9},// -
	{0x378B, 0x03},// -
	{0x378C, 0xEB},// -
	{0x378D, 0x05},// -
	{0x378E, 0x87},// -
	{0x378F, 0x06},// -
	{0x3790, 0xF5},// -
	{0x3792, 0x43},// -
	{0x3794, 0x7A},// -
	{0x3796, 0xA1},// -
	{0x3E04, 0x0E},// -
	{REG_NULL, 0x00},
};
/*
 *IMX334LQR All-pixel scan CSI-2_4lane 37.125Mhz
 *AD:12bit Output:12bit 1188Mbps Master Mode 30fps
 *Tool ver : Ver4.0
 */
static const struct regval imx334_linear_12_3840x2160_regs[] = {
	{0x302E, 0x18},
	{0x302F, 0x0f},
	{0x3030, 0xCA},// VMAX[19:0]
	{0x3031, 0x08},//
	{0x300C, 0x5B},// BCWAIT_TIME[7:0]
	{0x300D, 0x40},// CPWAIT_TIME[7:0]
	{0x3034, 0x4C},// HMAX[15:0]
	{0x3035, 0x04},//
	{0x3048, 0x00},// WDMODE[0]
	{0x3049, 0x00},// WDSEL[1:0]
	{0x304A, 0x00},// WD_SET1[2:0]
	{0x304B, 0x01},// WD_SET2[3:0]
	{0x304C, 0x14},// OPB_SIZE_V[5:0]
	{0x3058, 0x17},// SHR0[19:0]
	{0x3059, 0x00},//
	{0x3068, 0x8B},// RHS1[19:0]
	{0x3069, 0x00},//
	{0x3076, 0x84},
	{0x3077, 0x08},
	{0x314C, 0x80},// INCKSEL 1[8:0]
	{0x315A, 0x02},// INCKSEL2[1:0]
	{0x316A, 0x7E},// INCKSEL4[1:0]
	{0x319E, 0x01},// SYS_MODE
	{0x31D7, 0x00},// XVSMSKCNT_INT[1:0]
	{0x3200, 0x11},// FGAINEN[0]
	{0x3A18, 0x8F},// TCLKPOST[15:0]
	{0x3A1A, 0x4F},// TCLKPREPARE[15:0]
	{0x3A1C, 0x47},// TCLKTRAIL[15:0]
	{0x3A1E, 0x37},// TCLKZERO[15:0]
	{0x3A20, 0x4F},// THSPREPARE[15:0]
	{0x3A22, 0x87},// THSZERO[15:0]
	{0x3A24, 0x4F},// THSTRAIL[15:0]
	{0x3A26, 0x7F},// THSEXIT[15:0]
	{0x3A28, 0x3F},// TLPX[15:0]
	{REG_NULL, 0x00},
};

/*
 *All-pixel scan CSI-2_4lane 74.25Mhz
 *AD:12bit Output:12bit 1782Mbps Master Mode DOL HDR 2frame VC
 *Tool ver : Ver3.0
 */
static const struct regval imx334_hdr_12_74M_3840x2160_regs[] = {
	{0x302E, 0x18},
	{0x302F, 0x0f},
	{0x3030, 0xC8},// VMAX[19:0]
	{0x3031, 0x08},//
	{0x300C, 0xB6},// BCWAIT_TIME[7:0]
	{0x300D, 0x7F},// CPWAIT_TIME[7:0]
	{0x3034, 0x26},// HMAX[15:0]
	{0x3035, 0x02},//
	{0x3048, 0x01},// WDMODE[0]
	{0x3049, 0x01},// WDSEL[1:0]
	{0x304A, 0x01},// WD_SET1[2:0]
	{0x304B, 0x02},// WD_SET2[3:0]
	{0x304C, 0x13},// OPB_SIZE_V[5:0]
	{0x3058, 0xC2},// SHR0[19:0]
	{0x3059, 0x01},//
	{0x3068, 0x19},// RHS1[19:0]
	{0x3069, 0x01},//
	{0x3076, 0x84},
	{0x3077, 0x08},
	{0x314C, 0xC0},// INCKSEL 1[8:0]
	{0x315A, 0x03},// INCKSEL2[1:0]
	{0x316A, 0x7F},// INCKSEL4[1:0]
	{0x319E, 0x00},// SYS_MODE
	{0x31D7, 0x01},// XVSMSKCNT_INT[1:0]
	{0x3200, 0x10},// FGAINEN[0]
	{0x3A18, 0xB7},// TCLKPOST[15:0]
	{0x3A1A, 0x67},// TCLKPREPARE[15:0]
	{0x3A1C, 0x6F},// TCLKTRAIL[15:0]
	{0x3A1E, 0xDF},// TCLKZERO[15:0]
	{0x3A20, 0x6F},// THSPREPARE[15:0]
	{0x3A22, 0xCF},// THSZERO[15:0]
	{0x3A24, 0x6F},// THSTRAIL[15:0]
	{0x3A26, 0xB7},// THSEXIT[15:0]
	{0x3A28, 0x5F},// TLPX[15:0]
	{REG_NULL, 0x00},
};

static const struct imx334_mode supported_modes[] = {
	{
		.width = 3864,
		.height = 2180,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0600,
		.hts_def = 0x044C * 4,
		.vts_def = 0x08CA,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.global_reg_list = imx334_10_3840x2160_global_regs,
		.reg_list = imx334_linear_10_3840x2160_regs,
		.hdr_mode = NO_HDR,
		.vclk_freq = IMX334_XVCLK_FREQ_37,
		.bpp = 10,
		.mipi_freq_idx = 0,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}, {
		.width = 3864,
		.height = 2180,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0080,
		.hts_def = 0x01EF * 8,
		.vts_def = 0x09C4 * 2,
		.global_reg_list = imx334_10_3840x2160_global_regs,
		.reg_list = imx334_hdr_10_3840x2160_regs,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.hdr_mode = HDR_X2,
		.vclk_freq = IMX334_XVCLK_FREQ_37,
		.bpp = 10,
		.mipi_freq_idx = 2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	}, {
		.width = 3864,
		.height = 2180,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0600,
		.hts_def = 0x044C * 4,
		.vts_def = 0x08CA,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB12_1X12,
		.global_reg_list = imx334_12_3840x2160_global_regs,
		.reg_list = imx334_linear_12_3840x2160_regs,
		.hdr_mode = NO_HDR,
		.vclk_freq = IMX334_XVCLK_FREQ_37,
		.bpp = 12,
		.mipi_freq_idx = 1,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}, {
		.width = 3864,
		.height = 2180,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0080,
		.hts_def = 0x0226 * 8,
		.vts_def = 0x08C8 * 2,
		.global_reg_list = imx334_12_3840x2160_global_regs,
		.reg_list = imx334_hdr_12_74M_3840x2160_regs,
		.bus_fmt = MEDIA_BUS_FMT_SRGGB12_1X12,
		.hdr_mode = HDR_X2,
		.vclk_freq = IMX334_XVCLK_FREQ_74,
		.bpp = 12,
		.mipi_freq_idx = 2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
};

static const s64 link_freq_menu_items[] = {
	IMX334_LINK_FREQ_445,
	IMX334_LINK_FREQ_594,
	IMX334_LINK_FREQ_891,
};

static const char * const imx334_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int imx334_write_reg(struct i2c_client *client, u16 reg,
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

static int imx334_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		if (unlikely(regs[i].addr == REG_DELAY))
			usleep_range(regs[i].val, regs[i].val * 2);
		else
			ret = imx334_write_reg(client, regs[i].addr,
					       IMX334_REG_VALUE_08BIT,
					       regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int imx334_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static int imx334_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx334 *imx334 = to_imx334(sd);
	const struct imx334_mode *mode;
	s64 h_blank, vblank_def;
	s64 dst_pixel_rate = 0;
	int ret = 0;

	mutex_lock(&imx334->mutex);

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&imx334->mutex);
		return -ENOTTY;
#endif
	} else {
		imx334->cur_mode = mode;
		imx334->cur_vts = imx334->cur_mode->vts_def;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(imx334->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(imx334->vblank, vblank_def,
					 IMX334_VTS_MAX - mode->height,
					 1, vblank_def);
		if (imx334->cur_vclk_freq != mode->vclk_freq) {
			clk_disable_unprepare(imx334->xvclk);
			ret = clk_set_rate(imx334->xvclk, mode->vclk_freq);
			ret |= clk_prepare_enable(imx334->xvclk);
			if (ret < 0) {
				dev_err(&imx334->client->dev, "Failed to enable xvclk\n");
				mutex_unlock(&imx334->mutex);
				return ret;
			}
			imx334->cur_vclk_freq = mode->vclk_freq;
		}
		if (imx334->cur_mipi_freq_idx != mode->mipi_freq_idx) {
			dst_pixel_rate = ((u32)link_freq_menu_items[mode->mipi_freq_idx]) /
				mode->bpp * 2 * IMX334_LANES;
			__v4l2_ctrl_s_ctrl_int64(imx334->pixel_rate,
						 dst_pixel_rate);
			__v4l2_ctrl_s_ctrl(imx334->link_freq,
					   mode->mipi_freq_idx);
			imx334->cur_mipi_freq_idx = mode->mipi_freq_idx;
		}
	}
	mutex_unlock(&imx334->mutex);
	return 0;
}

static int imx334_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx334 *imx334 = to_imx334(sd);
	const struct imx334_mode *mode = imx334->cur_mode;

	mutex_lock(&imx334->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&imx334->mutex);
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
	mutex_unlock(&imx334->mutex);

	return 0;
}

static int imx334_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx334 *imx334 = to_imx334(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = imx334->cur_mode->bus_fmt;

	return 0;
}

static int imx334_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != supported_modes[0].bus_fmt)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int imx334_enable_test_pattern(struct imx334 *imx334, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | IMX334_TEST_PATTERN_ENABLE;
	else
		val = IMX334_TEST_PATTERN_DISABLE;

	return imx334_write_reg(imx334->client,
				IMX334_REG_TEST_PATTERN,
				IMX334_REG_VALUE_08BIT,
				val);
}

static int imx334_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct imx334 *imx334 = to_imx334(sd);
	const struct imx334_mode *mode = imx334->cur_mode;

	mutex_lock(&imx334->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&imx334->mutex);

	return 0;
}

static int imx334_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct imx334 *imx334 = to_imx334(sd);
	const struct imx334_mode *mode = imx334->cur_mode;
	u32 val = 0;

	val = 1 << (IMX334_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	config->flags = (mode->hdr_mode == NO_HDR) ? val : (val | V4L2_MBUS_CSI2_CHANNEL_1);
	config->type = V4L2_MBUS_CSI2_DPHY;
	return 0;
}

static void imx334_get_module_inf(struct imx334 *imx334,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, IMX334_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, imx334->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, imx334->len_name, sizeof(inf->base.lens));
}

static int imx334_set_hdrae(struct imx334 *imx334,
			    struct preisp_hdrae_exp_s *ae)
{
	struct i2c_client *client = imx334->client;
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_a_gain, m_a_gain, s_a_gain;
	u32 shr1 = 0;
	u32 shr0 = 0;
	u32 rhs1 = 0;
	u32 rhs1_max = 0;
	static int rhs1_old = 225;
	int rhs1_change_limit;
	int ret = 0;
	u32 fsc = imx334->cur_vts;

	if (!imx334->has_init_exp && !imx334->streaming) {
		imx334->init_hdrae_exp = *ae;
		imx334->has_init_exp = true;
		dev_dbg(&imx334->client->dev, "imx334 don't stream, record exp for hdr!\n");
		return ret;
	}
	l_exp_time = ae->long_exp_reg;
	m_exp_time = ae->middle_exp_reg;
	s_exp_time = ae->short_exp_reg;
	l_a_gain = ae->long_gain_reg;
	m_a_gain = ae->middle_gain_reg;
	s_a_gain = ae->short_gain_reg;
	dev_dbg(&client->dev,
		"rev exp: L_exp:0x%x,0x%x, M_exp:0x%x,0x%x S_exp:0x%x,0x%x\n",
		l_exp_time, l_a_gain,
		m_exp_time, m_a_gain,
		s_exp_time, s_a_gain);

	if (imx334->cur_mode->hdr_mode == HDR_X2) {
		//2 stagger
		l_a_gain = m_a_gain;
		l_exp_time = m_exp_time;
	}
	//gain effect n+1
	ret |= imx334_write_reg(client,
		IMX334_LF_GAIN_REG_L,
		IMX334_REG_VALUE_08BIT,
		l_a_gain & 0xff);
	ret |= imx334_write_reg(client,
		IMX334_SF1_GAIN_REG_L,
		IMX334_REG_VALUE_08BIT,
		s_a_gain & 0xff);

	//long exposure and short exposure
	shr0 = fsc - l_exp_time;
	rhs1_max = (RHS1_MAX > (shr0 - 9)) ? (shr0 - 9) : RHS1_MAX;
	rhs1_max = (rhs1_max >> 2) * 4 + 1;
	rhs1 = ((SHR1_MIN + s_exp_time + 3) >> 2) * 4 + 1;
	dev_dbg(&client->dev, "line(%d) rhs1 %d\n", __LINE__, rhs1);
	if (rhs1 < 13)
		rhs1 = 13;
	else if (rhs1 > rhs1_max)
		rhs1 = rhs1_max;
	dev_dbg(&client->dev, "line(%d) rhs1 %d\n", __LINE__, rhs1);

	//Dynamic adjustment rhs1 must meet the following conditions
	rhs1_change_limit = rhs1_old + 2 * BRL - fsc + 2;
	rhs1_change_limit = (rhs1_change_limit < 13) ?  13 : rhs1_change_limit;
	rhs1_change_limit = ((rhs1_change_limit + 3) >> 2) * 4 + 1;
	if (rhs1 < rhs1_change_limit)
		rhs1 = rhs1_change_limit;

	dev_dbg(&client->dev,
		"line(%d) rhs1 %d,short time %d rhs1_old %d test %d\n",
		__LINE__, rhs1, s_exp_time, rhs1_old,
		(rhs1_old + 2 * BRL - fsc + 2));

	rhs1_old = rhs1;
	shr1 = rhs1 - s_exp_time;

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
	ret |= imx334_write_reg(client,
		IMX334_RHS1_REG_L,
		IMX334_REG_VALUE_08BIT,
		IMX334_FETCH_RHS1_L(rhs1));
	ret |= imx334_write_reg(client,
		IMX334_RHS1_REG_M,
		IMX334_REG_VALUE_08BIT,
		IMX334_FETCH_RHS1_M(rhs1));
	ret |= imx334_write_reg(client,
		IMX334_RHS1_REG_H,
		IMX334_REG_VALUE_08BIT,
		IMX334_FETCH_RHS1_H(rhs1));

	ret |= imx334_write_reg(client,
		IMX334_SF1_EXPO_REG_L,
		IMX334_REG_VALUE_08BIT,
		IMX334_FETCH_EXP_L(shr1));
	ret |= imx334_write_reg(client,
		IMX334_SF1_EXPO_REG_M,
		IMX334_REG_VALUE_08BIT,
		IMX334_FETCH_EXP_M(shr1));
	ret |= imx334_write_reg(client,
		IMX334_SF1_EXPO_REG_H,
		IMX334_REG_VALUE_08BIT,
		IMX334_FETCH_EXP_H(shr1));
	ret |= imx334_write_reg(client,
		IMX334_LF_EXPO_REG_L,
		IMX334_REG_VALUE_08BIT,
		IMX334_FETCH_EXP_L(shr0));
	ret |= imx334_write_reg(client,
		IMX334_LF_EXPO_REG_M,
		IMX334_REG_VALUE_08BIT,
		IMX334_FETCH_EXP_M(shr0));
	ret |= imx334_write_reg(client,
		IMX334_LF_EXPO_REG_H,
		IMX334_REG_VALUE_08BIT,
		IMX334_FETCH_EXP_H(shr0));
	return ret;
}

static long imx334_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct imx334 *imx334 = to_imx334(sd);
	struct rkmodule_hdr_cfg *hdr;
	long ret = 0;
	u32 i, h, w;
	s64 dst_pixel_rate = 0;
	const struct imx334_mode *mode;
	u32 stream = 0;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		return imx334_set_hdrae(imx334, arg);
	case RKMODULE_GET_MODULE_INFO:
		imx334_get_module_inf(imx334, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = imx334->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = imx334->cur_mode->width;
		h = imx334->cur_mode->height;
		for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				imx334->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == ARRAY_SIZE(supported_modes)) {
			dev_err(&imx334->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			mode = imx334->cur_mode;
			imx334->cur_vts = mode->vts_def;
			w = mode->hts_def - mode->width;
			h = mode->vts_def - mode->height;
			__v4l2_ctrl_modify_range(imx334->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(imx334->vblank, h,
						 IMX334_VTS_MAX -
						 mode->height,
						 1, h);
			if (imx334->cur_vclk_freq != mode->vclk_freq) {
				clk_disable_unprepare(imx334->xvclk);
				ret = clk_set_rate(imx334->xvclk, mode->vclk_freq);
				ret |= clk_prepare_enable(imx334->xvclk);
				if (ret < 0) {
					dev_err(&imx334->client->dev, "Failed to enable xvclk\n");
					return ret;
				}
				imx334->cur_vclk_freq = mode->vclk_freq;
			}
			if (imx334->cur_mipi_freq_idx != mode->mipi_freq_idx) {
				dst_pixel_rate = ((u32)link_freq_menu_items[mode->mipi_freq_idx]) /
						 mode->bpp * 2 * IMX334_LANES;
				__v4l2_ctrl_s_ctrl_int64(imx334->pixel_rate,
							 dst_pixel_rate);
				__v4l2_ctrl_s_ctrl(imx334->link_freq,
						   mode->mipi_freq_idx);
				imx334->cur_mipi_freq_idx = mode->mipi_freq_idx;
			}
		}
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = imx334_write_reg(imx334->client, IMX334_REG_CTRL_MODE,
				IMX334_REG_VALUE_08BIT, 0);
		else
			ret = imx334_write_reg(imx334->client, IMX334_REG_CTRL_MODE,
				IMX334_REG_VALUE_08BIT, 1);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long imx334_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx334_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
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
			ret = imx334_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx334_ioctl(sd, cmd, hdr);
		if (!ret)
			ret = copy_to_user(up, hdr, sizeof(*hdr));
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
			ret = imx334_ioctl(sd, cmd, hdr);
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
			ret = imx334_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = imx334_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __imx334_start_stream(struct imx334 *imx334)
{
	int ret;

	ret = imx334_write_array(imx334->client, imx334->cur_mode->global_reg_list);
	if (ret)
		return ret;
	ret = imx334_write_array(imx334->client, imx334->cur_mode->reg_list);
	if (ret)
		return ret;
	/* In case these controls are set before streaming */
	if (imx334->has_init_exp && imx334->cur_mode->hdr_mode != NO_HDR) {
		ret = imx334_ioctl(&imx334->subdev, PREISP_CMD_SET_HDRAE_EXP,
			&imx334->init_hdrae_exp);
		if (ret) {
			dev_err(&imx334->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	} else {
		mutex_unlock(&imx334->mutex);
		ret = v4l2_ctrl_handler_setup(&imx334->ctrl_handler);
		mutex_lock(&imx334->mutex);
		if (ret)
			return ret;
	}
	return imx334_write_reg(imx334->client, IMX334_REG_CTRL_MODE,
				IMX334_REG_VALUE_08BIT, 0);
}

static int __imx334_stop_stream(struct imx334 *imx334)
{
	return imx334_write_reg(imx334->client, IMX334_REG_CTRL_MODE,
				IMX334_REG_VALUE_08BIT, 1);
}

static int imx334_s_stream(struct v4l2_subdev *sd, int on)
{
	struct imx334 *imx334 = to_imx334(sd);
	struct i2c_client *client = imx334->client;
	int ret = 0;

	mutex_lock(&imx334->mutex);
	on = !!on;
	if (on == imx334->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __imx334_start_stream(imx334);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__imx334_stop_stream(imx334);
		pm_runtime_put(&client->dev);
	}

	imx334->streaming = on;

unlock_and_return:
	mutex_unlock(&imx334->mutex);

	return ret;
}

static int imx334_s_power(struct v4l2_subdev *sd, int on)
{
	struct imx334 *imx334 = to_imx334(sd);
	struct i2c_client *client = imx334->client;
	int ret = 0;

	mutex_lock(&imx334->mutex);

	/* If the power state is not modified - no work to do. */
	if (imx334->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		imx334->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		imx334->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&imx334->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 imx334_cal_delay(u32 cycles, struct imx334 *imx334)
{
	if (imx334->cur_mode->vclk_freq == IMX334_XVCLK_FREQ_37)
		return DIV_ROUND_UP(cycles, IMX334_XVCLK_FREQ_37 / 1000 / 1000);
	else
		return DIV_ROUND_UP(cycles, IMX334_XVCLK_FREQ_74 / 1000 / 1000);
}

static int __imx334_power_on(struct imx334 *imx334)
{
	int ret;
	u32 delay_us;
	s64 vclk_freq;
	struct device *dev = &imx334->client->dev;

	if (!IS_ERR_OR_NULL(imx334->pins_default)) {
		ret = pinctrl_select_state(imx334->pinctrl,
					   imx334->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	if (imx334->cur_mode->vclk_freq == IMX334_XVCLK_FREQ_37)
		vclk_freq = IMX334_XVCLK_FREQ_37;
	else
		vclk_freq = IMX334_XVCLK_FREQ_74;

	ret = clk_set_rate(imx334->xvclk, vclk_freq);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(imx334->xvclk) != vclk_freq)
		dev_warn(dev, "xvclk mismatched, modes are based on 37.125MHz\n");
	ret = clk_prepare_enable(imx334->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(imx334->reset_gpio))
		gpiod_set_value_cansleep(imx334->reset_gpio, 0);

	ret = regulator_bulk_enable(IMX334_NUM_SUPPLIES, imx334->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(imx334->reset_gpio))
		gpiod_set_value_cansleep(imx334->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(imx334->pwdn_gpio))
		gpiod_set_value_cansleep(imx334->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = imx334_cal_delay(8192, imx334);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(imx334->xvclk);

	return ret;
}

static void __imx334_power_off(struct imx334 *imx334)
{
	if (!IS_ERR(imx334->pwdn_gpio))
		gpiod_set_value_cansleep(imx334->pwdn_gpio, 0);
	clk_disable_unprepare(imx334->xvclk);
	if (!IS_ERR(imx334->reset_gpio))
		gpiod_set_value_cansleep(imx334->reset_gpio, 0);
	regulator_bulk_disable(IMX334_NUM_SUPPLIES, imx334->supplies);
}

static int imx334_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx334 *imx334 = to_imx334(sd);

	return __imx334_power_on(imx334);
}

static int imx334_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx334 *imx334 = to_imx334(sd);

	__imx334_power_off(imx334);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int imx334_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx334 *imx334 = to_imx334(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct imx334_mode *def_mode = &supported_modes[0];

	mutex_lock(&imx334->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&imx334->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int imx334_enum_frame_interval(struct v4l2_subdev *sd,
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

#define CROP_START(SRC, DST) (((SRC) - (DST)) / 2 / 4 * 4)
#define DST_WIDTH 3840
#define DST_HEIGHT 2160

static int imx334_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct imx334 *imx334 = to_imx334(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = CROP_START(imx334->cur_mode->width, DST_WIDTH);
		sel->r.width = DST_WIDTH;
		sel->r.top = CROP_START(imx334->cur_mode->height, DST_HEIGHT);
		sel->r.height = DST_HEIGHT;
		return 0;
	}
	return -EINVAL;
}

static const struct dev_pm_ops imx334_pm_ops = {
	SET_RUNTIME_PM_OPS(imx334_runtime_suspend,
			   imx334_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops imx334_internal_ops = {
	.open = imx334_open,
};
#endif

static const struct v4l2_subdev_core_ops imx334_core_ops = {
	.s_power = imx334_s_power,
	.ioctl = imx334_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = imx334_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops imx334_video_ops = {
	.s_stream = imx334_s_stream,
	.g_frame_interval = imx334_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops imx334_pad_ops = {
	.enum_mbus_code = imx334_enum_mbus_code,
	.enum_frame_size = imx334_enum_frame_sizes,
	.enum_frame_interval = imx334_enum_frame_interval,
	.get_fmt = imx334_get_fmt,
	.set_fmt = imx334_set_fmt,
	.get_selection = imx334_get_selection,
	.get_mbus_config = imx334_g_mbus_config,
};

static const struct v4l2_subdev_ops imx334_subdev_ops = {
	.core	= &imx334_core_ops,
	.video	= &imx334_video_ops,
	.pad	= &imx334_pad_ops,
};

static int imx334_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx334 *imx334 = container_of(ctrl->handler,
					     struct imx334, ctrl_handler);
	struct i2c_client *client = imx334->client;
	s64 max;
	int ret = 0;
	u32 shr0 = 0;
	u32 vts = 0;
	u32 flip = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = imx334->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(imx334->exposure,
					 imx334->exposure->minimum, max,
					 imx334->exposure->step,
					 imx334->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		shr0 = imx334->cur_vts - ctrl->val;
		/* 4 least significant bits of expsoure are fractional part */
		ret = imx334_write_reg(imx334->client,
				       IMX334_LF_EXPO_REG_H,
				       IMX334_REG_VALUE_08BIT,
				       IMX334_FETCH_EXP_H(shr0));
		ret |= imx334_write_reg(imx334->client,
					IMX334_LF_EXPO_REG_M,
					IMX334_REG_VALUE_08BIT,
					IMX334_FETCH_EXP_M(shr0));
		ret |= imx334_write_reg(imx334->client,
					IMX334_LF_EXPO_REG_L,
					IMX334_REG_VALUE_08BIT,
					IMX334_FETCH_EXP_L(shr0));
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = imx334_write_reg(imx334->client,
				       IMX334_REG_GAIN,
				       IMX334_REG_VALUE_08BIT, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		vts = ctrl->val + imx334->cur_mode->height;
		/*
		 * vts of hdr mode is double to correct T-line calculation.
		 * Restore before write to reg.
		 */
		if (imx334->cur_mode->hdr_mode == HDR_X2) {
			vts = ((vts + 3) >> 2) * 4;
			imx334->cur_vts = vts;
			vts = vts >> 1;
		} else {
			imx334->cur_vts = vts;
		}
		ret = imx334_write_reg(imx334->client,
				       IMX334_REG_VTS_H,
				       IMX334_REG_VALUE_08BIT,
				       IMX334_FETCH_VTS_H(vts));
		ret |= imx334_write_reg(imx334->client,
					IMX334_REG_VTS_M,
					IMX334_REG_VALUE_08BIT,
					IMX334_FETCH_VTS_M(vts));
		ret |= imx334_write_reg(imx334->client,
					IMX334_REG_VTS_L,
					IMX334_REG_VALUE_08BIT,
					IMX334_FETCH_VTS_L(vts));
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = imx334_enable_test_pattern(imx334, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = imx334_write_reg(imx334->client, IMX334_HREVERSE_REG,
				       IMX334_REG_VALUE_08BIT, !!ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		flip = ctrl->val;
		if (flip) {
			ret = imx334_write_reg(imx334->client, IMX334_VREVERSE_REG,
				IMX334_REG_VALUE_08BIT, !!flip);
			ret |= imx334_write_reg(imx334->client, 0x3080,
				IMX334_REG_VALUE_08BIT, 0xfe);
			ret |= imx334_write_reg(imx334->client, 0x309b,
				IMX334_REG_VALUE_08BIT, 0xfe);
		} else {
			ret = imx334_write_reg(imx334->client, IMX334_VREVERSE_REG,
				IMX334_REG_VALUE_08BIT, !!flip);
			ret |= imx334_write_reg(imx334->client, 0x3080,
				IMX334_REG_VALUE_08BIT, 0x02);
			ret |= imx334_write_reg(imx334->client, 0x309b,
				IMX334_REG_VALUE_08BIT, 0x02);
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

static const struct v4l2_ctrl_ops imx334_ctrl_ops = {
	.s_ctrl = imx334_set_ctrl,
};

static int imx334_initialize_controls(struct imx334 *imx334)
{
	const struct imx334_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	s64 dst_pixel_rate = 0;

	handler = &imx334->ctrl_handler;
	mode = imx334->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &imx334->mutex;

	imx334->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
						   V4L2_CID_LINK_FREQ,
						   2, 0, link_freq_menu_items);

	dst_pixel_rate = ((u32)link_freq_menu_items[mode->mipi_freq_idx]) /
		mode->bpp * 2 * IMX334_LANES;

	imx334->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
					       V4L2_CID_PIXEL_RATE,
					       0, PIXEL_RATE_WITH_891M_10BIT,
					       1, dst_pixel_rate);
	v4l2_ctrl_s_ctrl(imx334->link_freq,
			 mode->mipi_freq_idx);
	imx334->cur_mipi_freq_idx = mode->mipi_freq_idx;
	imx334->cur_vclk_freq = mode->vclk_freq;

	h_blank = mode->hts_def - mode->width;
	imx334->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					   h_blank, h_blank, 1, h_blank);
	if (imx334->hblank)
		imx334->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	imx334->vblank = v4l2_ctrl_new_std(handler, &imx334_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_def,
					   IMX334_VTS_MAX - mode->height,
					   1, vblank_def);
	imx334->cur_vts = mode->vts_def;
	exposure_max = mode->vts_def - 4;
	imx334->exposure = v4l2_ctrl_new_std(handler, &imx334_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX334_EXPOSURE_MIN,
					     exposure_max,
					     IMX334_EXPOSURE_STEP,
					     mode->exp_def);

	imx334->anal_gain = v4l2_ctrl_new_std(handler, &imx334_ctrl_ops,
					      V4L2_CID_ANALOGUE_GAIN,
					      IMX334_GAIN_MIN,
					      IMX334_GAIN_MAX,
					      IMX334_GAIN_STEP,
					      IMX334_GAIN_DEFAULT);

	imx334->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
							    &imx334_ctrl_ops,
				V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(imx334_test_pattern_menu) - 1,
				0, 0, imx334_test_pattern_menu);

	v4l2_ctrl_new_std(handler, &imx334_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &imx334_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&imx334->client->dev,
			"Failed to init controls(  %d  )\n", ret);
		goto err_free_handler;
	}

	imx334->subdev.ctrl_handler = handler;
	imx334->has_init_exp = false;
	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int imx334_check_sensor_id(struct imx334 *imx334,
				  struct i2c_client *client)
{
	struct device *dev = &imx334->client->dev;
	u32 id = 0;
	int ret, i;

	for (i = 0; i < 10; i++) {
		ret = imx334_read_reg(client, IMX334_REG_CHIP_ID,
				      IMX334_REG_VALUE_08BIT, &id);
		if (id == CHIP_ID)
			break;
	}

	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		usleep_range(2000, 4000);
		return -ENODEV;
	}

	dev_info(dev, "Detected imx334 id:%06x\n", CHIP_ID);

	return 0;
}

static int imx334_configure_regulators(struct imx334 *imx334)
{
	unsigned int i;

	for (i = 0; i < IMX334_NUM_SUPPLIES; i++)
		imx334->supplies[i].supply = imx334_supply_names[i];

	return devm_regulator_bulk_get(&imx334->client->dev,
				       IMX334_NUM_SUPPLIES,
				       imx334->supplies);
}

static int imx334_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct imx334 *imx334;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	imx334 = devm_kzalloc(dev, sizeof(*imx334), GFP_KERNEL);
	if (!imx334)
		return -ENOMEM;

	of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &imx334->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &imx334->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &imx334->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &imx334->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	imx334->client = client;
	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			imx334->cur_mode = &supported_modes[i];
			break;
		}
	}
	if (i == ARRAY_SIZE(supported_modes))
		imx334->cur_mode = &supported_modes[0];

	imx334->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(imx334->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	imx334->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(imx334->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	imx334->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(imx334->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	imx334->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(imx334->pinctrl)) {
		imx334->pins_default =
			pinctrl_lookup_state(imx334->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(imx334->pins_default))
			dev_info(dev, "could not get default pinstate\n");

		imx334->pins_sleep =
			pinctrl_lookup_state(imx334->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(imx334->pins_sleep))
			dev_info(dev, "could not get sleep pinstate\n");
	} else {
		dev_info(dev, "no pinctrl\n");
	}

	ret = imx334_configure_regulators(imx334);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&imx334->mutex);

	sd = &imx334->subdev;
	v4l2_i2c_subdev_init(sd, client, &imx334_subdev_ops);

	ret = imx334_initialize_controls(imx334);
	if (ret)
		goto err_destroy_mutex;

	ret = __imx334_power_on(imx334);
	if (ret)
		goto err_free_handler;

	ret = imx334_check_sensor_id(imx334, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &imx334_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	imx334->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &imx334->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(imx334->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 imx334->module_index, facing,
		 IMX334_NAME, dev_name(sd->dev));
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
	__imx334_power_off(imx334);
err_free_handler:
	v4l2_ctrl_handler_free(&imx334->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&imx334->mutex);

	return ret;
}

static int imx334_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx334 *imx334 = to_imx334(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&imx334->ctrl_handler);
	mutex_destroy(&imx334->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__imx334_power_off(imx334);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id imx334_of_match[] = {
	{ .compatible = "sony,imx334" },
	{},
};
MODULE_DEVICE_TABLE(of, imx334_of_match);
#endif

static const struct i2c_device_id imx334_match_id[] = {
	{ "sony,imx334", 0 },
	{ },
};

static struct i2c_driver imx334_i2c_driver = {
	.driver = {
		.name = IMX334_NAME,
		.pm = &imx334_pm_ops,
		.of_match_table = of_match_ptr(imx334_of_match),
	},
	.probe		= &imx334_probe,
	.remove		= &imx334_remove,
	.id_table	= imx334_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&imx334_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&imx334_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Sony imx334 sensor driver");
MODULE_LICENSE("GPL v2");
