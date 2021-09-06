// SPDX-License-Identifier: GPL-2.0
/*
 * imx335 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version
 * V0.0X01.0X01 support 10bit DOL3
 * V0.0X01.0X02 fix set sensor vertical invert failed
 * V0.0X01.0X03 add hdr_mode in enum frame interval
 * V0.0X01.0X04 fix hdr ae error
 * V0.0X01.0X05 add quick stream on/off
 * V0.0X01.0X06 Increase hdr exposure restrictions
 */

#define DEBUG
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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x06)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIPI_FREQ_594M			594000000

#define IMX335_4LANES			4

#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define IMX335_XVCLK_FREQ_37M		37125000

/* TODO: Get the real chip id from reg */
#define CHIP_ID				0x03
#define IMX335_REG_CHIP_ID		0x3A01

#define IMX335_REG_CTRL_MODE		0x3000
#define IMX335_MODE_SW_STANDBY		BIT(0)
#define IMX335_MODE_STREAMING		0x0

#define IMX335_LF_GAIN_REG_H		0x30E9
#define IMX335_LF_GAIN_REG_L		0x30E8

#define IMX335_SF1_GAIN_REG_H		0x30EB
#define IMX335_SF1_GAIN_REG_L		0x30EA

#define IMX335_SF2_GAIN_REG_H		0x30ED
#define IMX335_SF2_GAIN_REG_L		0x30EC

#define IMX335_LF_EXPO_REG_H		0x305A
#define IMX335_LF_EXPO_REG_M		0x3059
#define IMX335_LF_EXPO_REG_L		0x3058

#define IMX335_SF1_EXPO_REG_H		0x305E
#define IMX335_SF1_EXPO_REG_M		0x305D
#define IMX335_SF1_EXPO_REG_L		0x305C

#define IMX335_RHS1_REG_H		0x306A
#define IMX335_RHS1_REG_M		0x3069
#define IMX335_RHS1_REG_L		0x3068
#define IMX335_RHS1_DEFAULT		0x0122
#define IMX335_RHS1_X3_DEFAULT		0x012E

#define IMX335_SF2_EXPO_REG_H		0x3062
#define IMX335_SF2_EXPO_REG_M		0x3061
#define IMX335_SF2_EXPO_REG_L		0x3060

#define IMX335_RHS2_REG_H		0x306E
#define IMX335_RHS2_REG_M		0x306D
#define IMX335_RHS2_REG_L		0x306C
#define IMX335_RHS2_X3_DEFAULT		0x016C

/*
 * The linear shr0 shall be:
 *   9 <= shr0 <= VMAX - 1.
 *   1 <= expo = VMAX - shr0 <= VMAX - 9
 *                           == VMAX - SHR0_MIN
 *
 */
#define	IMX335_EXPOSURE_MIN		1
#define	IMX335_EXPOSURE_STEP		1
#define SHR0_MIN			9
#define IMX335_VTS_MAX			0x7fff

#define IMX335_GAIN_MIN			0x00
#define IMX335_GAIN_MAX			0xf0
#define IMX335_GAIN_STEP		1
#define IMX335_GAIN_DEFAULT		0x00

#define IMX335_FETCH_GAIN_H(VAL)	(((VAL) >> 8) & 0x07)
#define IMX335_FETCH_GAIN_L(VAL)	((VAL) & 0xFF)

#define IMX335_FETCH_EXP_H(VAL)		(((VAL) >> 16) & 0x0F)
#define IMX335_FETCH_EXP_M(VAL)		(((VAL) >> 8) & 0xFF)
#define IMX335_FETCH_EXP_L(VAL)		((VAL) & 0xFF)

#define IMX335_FETCH_RHS1_H(VAL)	(((VAL) >> 16) & 0x0F)
#define IMX335_FETCH_RHS1_M(VAL)	(((VAL) >> 8) & 0xFF)
#define IMX335_FETCH_RHS1_L(VAL)	((VAL) & 0xFF)

#define IMX335_FETCH_VTS_H(VAL)		(((VAL) >> 16) & 0x0F)
#define IMX335_FETCH_VTS_M(VAL)		(((VAL) >> 8) & 0xFF)
#define IMX335_FETCH_VTS_L(VAL)		((VAL) & 0xFF)

#define IMX335_VTS_REG_L		0x3030
#define IMX335_VTS_REG_M		0x3031
#define IMX335_VTS_REG_H		0x3032

#define IMX335_HREVERSE_REG		0x304E
#define IMX335_VREVERSE_REG		0x304F

#define REG_NULL			0xFFFF

#define IMX335_REG_VALUE_08BIT		1
#define IMX335_REG_VALUE_16BIT		2
#define IMX335_REG_VALUE_24BIT		3

#define IMX335_GROUP_HOLD_REG		0x3001
#define IMX335_GROUP_HOLD_START		0x01
#define IMX335_GROUP_HOLD_END		0x00

/* Basic Readout Lines. Number of necessary readout lines in sensor */
#define BRL				(1984u * 2)
#define RHS1_MAX			(BRL * 2 - 1)
#define SHR1_MIN			18u

/* Readout timing setting of SEF1(DOL3): RHS1 < 3 * BRL and should be 12n + 2 */
#define RHS1_MAX_X3			((BRL * 3 - 1) / 12 * 12 + 2)
#define SHR1_MIN_X3			26u

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define IMX335_NAME			"imx335"

static const char * const imx335_supply_names[] = {
	"dvdd",		/* Digital core power */
	"dovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
};

#define IMX335_NUM_SUPPLIES ARRAY_SIZE(imx335_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct imx335_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 bpp;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct imx335 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct regulator_bulk_data supplies[IMX335_NUM_SUPPLIES];

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
	const struct imx335_mode *cur_mode;
	u32			module_index;
	u32			cfg_num;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	u32			cur_vts;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
};

#define to_imx335(sd) container_of(sd, struct imx335, subdev)

/*
 * Xclk 37.125Mhz
 */
static const struct regval imx335_linear_10bit_2592x1944_regs[] = {
	{0x3002, 0x00},
	{0x300C, 0x5B},
	{0x300D, 0x40},
	{0x3034, 0x26},
	{0x3035, 0x02},
	{0x3048, 0x00},
	{0x3049, 0x00},
	{0x304A, 0x03},
	{0x304B, 0x01},
	{0x304C, 0x14},
	{0x3050, 0x00},
	{0x3058, 0x09},
	{0x3059, 0x00},
	{0x305C, 0x12},
	{0x3060, 0xE8},
	{0x3061, 0x00},
	{0x3068, 0xce},
	{0x3069, 0x00},
	{0x306C, 0x88},
	{0x306D, 0x06},
	{0x30E8, 0x00},
	{0x315A, 0x02},
	{0x316A, 0x7E},
	{0x319D, 0x00},
	{0x31A1, 0x00},
	{0x31D7, 0x00},
	{0x3200, 0x01}, /* Each frame gain adjustment disabed in linear mode */
	{0x3288, 0x21},
	{0x328A, 0x02},
	{0x3414, 0x05},
	{0x3416, 0x18},
	{0x341C, 0xFF},
	{0x341D, 0x01},
	{0x3648, 0x01},
	{0x364A, 0x04},
	{0x364C, 0x04},
	{0x3678, 0x01},
	{0x367C, 0x31},
	{0x367E, 0x31},
	{0x3706, 0x10},
	{0x3708, 0x03},
	{0x3714, 0x02},
	{0x3715, 0x02},
	{0x3716, 0x01},
	{0x3717, 0x03},
	{0x371C, 0x3D},
	{0x371D, 0x3F},
	{0x372C, 0x00},
	{0x372D, 0x00},
	{0x372E, 0x46},
	{0x372F, 0x00},
	{0x3730, 0x89},
	{0x3731, 0x00},
	{0x3732, 0x08},
	{0x3733, 0x01},
	{0x3734, 0xFE},
	{0x3735, 0x05},
	{0x3740, 0x02},
	{0x375D, 0x00},
	{0x375E, 0x00},
	{0x375F, 0x11},
	{0x3760, 0x01},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1B},
	{0x376B, 0x1B},
	{0x376C, 0x1A},
	{0x376D, 0x17},
	{0x376E, 0x0F},
	{0x3776, 0x00},
	{0x3777, 0x00},
	{0x3778, 0x46},
	{0x3779, 0x00},
	{0x377A, 0x89},
	{0x377B, 0x00},
	{0x377C, 0x08},
	{0x377D, 0x01},
	{0x377E, 0x23},
	{0x377F, 0x02},
	{0x3780, 0xD9},
	{0x3781, 0x03},
	{0x3782, 0xF5},
	{0x3783, 0x06},
	{0x3784, 0xA5},
	{0x3788, 0x0F},
	{0x378A, 0xD9},
	{0x378B, 0x03},
	{0x378C, 0xEB},
	{0x378D, 0x05},
	{0x378E, 0x87},
	{0x378F, 0x06},
	{0x3790, 0xF5},
	{0x3792, 0x43},
	{0x3794, 0x7A},
	{0x3796, 0xA1},
	{REG_NULL, 0x00},
};

static const struct regval imx335_hdr2_10bit_2592x1944_regs[] = {
	{0x3002, 0x00},
	{0x300C, 0x5B},
	{0x300D, 0x40},
	{0x3034, 0x13},
	{0x3035, 0x01},
	{0x3048, 0x01},
	{0x3049, 0x01},
	{0x304A, 0x04},
	{0x304B, 0x03},
	{0x304C, 0x13},
	{0x3050, 0x00},
	{0x3058, 0x48},
	{0x3059, 0x12},
	{0x305C, 0x12},
	{0x3060, 0xE8},
	{0x3061, 0x00},
	{0x3068, 0x22},
	{0x3069, 0x01},
	{0x306C, 0x68},
	{0x306D, 0x06},
	{0x30E8, 0x00},
	{0x315A, 0x02},
	{0x316A, 0x7E},
	{0x319D, 0x00},
	{0x31A1, 0x00},
	{0x31D7, 0x01},
	{0x3200, 0x00}, /* Each frame gain adjustment EN */
	{0x3288, 0x21},
	{0x328A, 0x02},
	{0x3414, 0x05},
	{0x3416, 0x18},
	{0x341C, 0xFF},
	{0x341D, 0x01},
	{0x3648, 0x01},
	{0x364A, 0x04},
	{0x364C, 0x04},
	{0x3678, 0x01},
	{0x367C, 0x31},
	{0x367E, 0x31},
	{0x3706, 0x10},
	{0x3708, 0x03},
	{0x3714, 0x02},
	{0x3715, 0x02},
	{0x3716, 0x01},
	{0x3717, 0x03},
	{0x371C, 0x3D},
	{0x371D, 0x3F},
	{0x372C, 0x00},
	{0x372D, 0x00},
	{0x372E, 0x46},
	{0x372F, 0x00},
	{0x3730, 0x89},
	{0x3731, 0x00},
	{0x3732, 0x08},
	{0x3733, 0x01},
	{0x3734, 0xFE},
	{0x3735, 0x05},
	{0x3740, 0x02},
	{0x375D, 0x00},
	{0x375E, 0x00},
	{0x375F, 0x11},
	{0x3760, 0x01},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1B},
	{0x376B, 0x1B},
	{0x376C, 0x1A},
	{0x376D, 0x17},
	{0x376E, 0x0F},
	{0x3776, 0x00},
	{0x3777, 0x00},
	{0x3778, 0x46},
	{0x3779, 0x00},
	{0x377A, 0x89},
	{0x377B, 0x00},
	{0x377C, 0x08},
	{0x377D, 0x01},
	{0x377E, 0x23},
	{0x377F, 0x02},
	{0x3780, 0xD9},
	{0x3781, 0x03},
	{0x3782, 0xF5},
	{0x3783, 0x06},
	{0x3784, 0xA5},
	{0x3788, 0x0F},
	{0x378A, 0xD9},
	{0x378B, 0x03},
	{0x378C, 0xEB},
	{0x378D, 0x05},
	{0x378E, 0x87},
	{0x378F, 0x06},
	{0x3790, 0xF5},
	{0x3792, 0x43},
	{0x3794, 0x7A},
	{0x3796, 0xA1},
	{REG_NULL, 0x00},
};

static const struct regval imx335_hdr3_10bit_2592x1944_regs[] = {
	{0x3002, 0x00},
	{0x300C, 0x5B},
	{0x300D, 0x40},
	{0x3034, 0x13},
	{0x3035, 0x01},
	{0x3048, 0x01},
	{0x3049, 0x02},
	{0x304A, 0x05},
	{0x304B, 0x03},
	{0x304C, 0x13},
	{0x3050, 0x00},
	{0x3058, 0xC4},
	{0x3059, 0x3B},
	{0x305C, 0x1A},
	{0x3060, 0x4E},
	{0x3061, 0x01},
	{0x3068, 0x2E},
	{0x3069, 0x01},
	{0x306C, 0x6C},
	{0x306D, 0x01},
	{0x30E8, 0x14},
	{0x315A, 0x02},
	{0x316A, 0x7E},
	{0x319D, 0x00},
	{0x31A1, 0x00},
	{0x31D7, 0x03},
	{0x3200, 0x00}, /* Each frame gain adjustment EN */
	{0x3288, 0x21},
	{0x328A, 0x02},
	{0x3414, 0x05},
	{0x3416, 0x18},
	{0x341C, 0xFF},
	{0x341D, 0x01},
	{0x3648, 0x01},
	{0x364A, 0x04},
	{0x364C, 0x04},
	{0x3678, 0x01},
	{0x367C, 0x31},
	{0x367E, 0x31},
	{0x3706, 0x10},
	{0x3708, 0x03},
	{0x3714, 0x02},
	{0x3715, 0x02},
	{0x3716, 0x01},
	{0x3717, 0x03},
	{0x371C, 0x3D},
	{0x371D, 0x3F},
	{0x372C, 0x00},
	{0x372D, 0x00},
	{0x372E, 0x46},
	{0x372F, 0x00},
	{0x3730, 0x89},
	{0x3731, 0x00},
	{0x3732, 0x08},
	{0x3733, 0x01},
	{0x3734, 0xFE},
	{0x3735, 0x05},
	{0x3740, 0x02},
	{0x375D, 0x00},
	{0x375E, 0x00},
	{0x375F, 0x11},
	{0x3760, 0x01},
	{0x3768, 0x1B},
	{0x3769, 0x1B},
	{0x376A, 0x1B},
	{0x376B, 0x1B},
	{0x376C, 0x1A},
	{0x376D, 0x17},
	{0x376E, 0x0F},
	{0x3776, 0x00},
	{0x3777, 0x00},
	{0x3778, 0x46},
	{0x3779, 0x00},
	{0x377A, 0x89},
	{0x377B, 0x00},
	{0x377C, 0x08},
	{0x377D, 0x01},
	{0x377E, 0x23},
	{0x377F, 0x02},
	{0x3780, 0xD9},
	{0x3781, 0x03},
	{0x3782, 0xF5},
	{0x3783, 0x06},
	{0x3784, 0xA5},
	{0x3788, 0x0F},
	{0x378A, 0xD9},
	{0x378B, 0x03},
	{0x378C, 0xEB},
	{0x378D, 0x05},
	{0x378E, 0x87},
	{0x378F, 0x06},
	{0x3790, 0xF5},
	{0x3792, 0x43},
	{0x3794, 0x7A},
	{0x3796, 0xA1},
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

static const struct imx335_mode supported_modes[] = {
	{
		/* 1H period = 7.4us */
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 2616,
		.height = 1964,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x1194 - 0x09,
		.hts_def = 0x0226 * IMX335_4LANES * 2,
		.vts_def = 0x1194,
		.reg_list = imx335_linear_10bit_2592x1944_regs,
		.hdr_mode = NO_HDR,
		.bpp = 10,
	},
	{
		/* 1H period = 3.70us */
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 2616,
		.height = 1964,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x1194 * 2 - 0x1248,
		.hts_def = 0x0113 * IMX335_4LANES * 2 * 2,
		/*
		 * IMX335 HDR mode T-line is a half of Linear mode,
		 * make vts double(that is FSC) to workaround.
		 */
		.vts_def = 0x1194 * 2,
		.reg_list = imx335_hdr2_10bit_2592x1944_regs,
		.hdr_mode = HDR_X2,
		.bpp = 10,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
	{
		/* 1H period = 3.70us */
		.bus_fmt = MEDIA_BUS_FMT_SRGGB10_1X10,
		.width = 2616,
		.height = 1964,
		.max_fps = {
			.numerator = 10000,
			.denominator = 150000,
		},
		.exp_def = 0x1194 * 2 - 0x1248,
		.hts_def = 0x0113 * IMX335_4LANES * 2 * 2,
		/*
		 * IMX335 HDR mode T-line is a half of Linear mode,
		 * make vts double(that is FSC) to workaround.
		 */
		.vts_def = 0x1194 * 4,
		.reg_list = imx335_hdr3_10bit_2592x1944_regs,
		.hdr_mode = HDR_X3,
		.bpp = 10,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_2,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_2,//S->csi wr2
	},
};

static const s64 link_freq_items[] = {
	MIPI_FREQ_594M,
};

/* Write registers up to 4 at a time */
static int imx335_write_reg(struct i2c_client *client, u16 reg,
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

static int imx335_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret = imx335_write_reg(client, regs[i].addr,
				       IMX335_REG_VALUE_08BIT, regs[i].val);
	}
	return ret;
}

/* Read registers up to 4 at a time */
static int imx335_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static int imx335_get_reso_dist(const struct imx335_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct imx335_mode *
imx335_find_best_fit(struct imx335 *imx335, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < imx335->cfg_num; i++) {
		dist = imx335_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
			supported_modes[i].bus_fmt == framefmt->code) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static void imx335_change_mode(struct imx335 *imx335, const struct imx335_mode *mode)
{
	imx335->cur_mode = mode;
	imx335->cur_vts = imx335->cur_mode->vts_def;
	dev_dbg(&imx335->client->dev, "set fmt: cur_mode: %dx%d, hdr: %d\n",
		mode->width, mode->height, mode->hdr_mode);
}

static int imx335_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx335 *imx335 = to_imx335(sd);
	const struct imx335_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&imx335->mutex);

	mode = imx335_find_best_fit(imx335, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;

	} else {
		imx335_change_mode(imx335, mode);
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(imx335->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(imx335->vblank, vblank_def,
					 IMX335_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&imx335->mutex);

	return 0;
}

static int imx335_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct imx335 *imx335 = to_imx335(sd);
	const struct imx335_mode *mode = imx335->cur_mode;

	mutex_lock(&imx335->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
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
	mutex_unlock(&imx335->mutex);

	return 0;
}

static int imx335_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct imx335 *imx335 = to_imx335(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = imx335->cur_mode->bus_fmt;

	return 0;
}

static int imx335_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct imx335 *imx335 = to_imx335(sd);

	if (fse->index >= imx335->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int imx335_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct imx335 *imx335 = to_imx335(sd);
	const struct imx335_mode *mode = imx335->cur_mode;

	mutex_lock(&imx335->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&imx335->mutex);

	return 0;
}

static int imx335_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	u32 val = 0;
	struct imx335 *imx335 = to_imx335(sd);
	const struct imx335_mode *mode = imx335->cur_mode;

	val = 1 << (IMX335_4LANES - 1) |
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

static void imx335_get_module_inf(struct imx335 *imx335,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, IMX335_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, imx335->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, imx335->len_name, sizeof(inf->base.lens));
}

static int imx335_set_hdrae(struct imx335 *imx335,
			    struct preisp_hdrae_exp_s *ae)
{
	struct i2c_client *client = imx335->client;
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_a_gain, m_a_gain, s_a_gain;
	int shr1, shr0, rhs1, rhs1_max, rhs1_min;
	static int rhs1_old = IMX335_RHS1_DEFAULT;
	int ret = 0;
	u32 fsc;

	if (!imx335->has_init_exp && !imx335->streaming) {
		imx335->init_hdrae_exp = *ae;
		imx335->has_init_exp = true;
		dev_dbg(&imx335->client->dev, "imx335 is not streaming, save hdr ae!\n");
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
		l_exp_time, l_a_gain, m_exp_time, m_a_gain, s_exp_time, s_a_gain);

	if (imx335->cur_mode->hdr_mode == HDR_X2) {
		l_a_gain = m_a_gain;
		l_exp_time = m_exp_time;
	}

	ret = imx335_write_reg(client, IMX335_GROUP_HOLD_REG,
		IMX335_REG_VALUE_08BIT, IMX335_GROUP_HOLD_START);
	/* gain effect n+1 */
	ret |= imx335_write_reg(client, IMX335_LF_GAIN_REG_H,
		IMX335_REG_VALUE_08BIT, IMX335_FETCH_GAIN_H(l_a_gain));
	ret |= imx335_write_reg(client, IMX335_LF_GAIN_REG_L,
		IMX335_REG_VALUE_08BIT, IMX335_FETCH_GAIN_L(l_a_gain));
	ret |= imx335_write_reg(client, IMX335_SF1_GAIN_REG_H,
		IMX335_REG_VALUE_08BIT, IMX335_FETCH_GAIN_H(s_a_gain));
	ret |= imx335_write_reg(client, IMX335_SF1_GAIN_REG_L,
		IMX335_REG_VALUE_08BIT, IMX335_FETCH_GAIN_L(s_a_gain));

	/* Restrictions
	 *     FSC = 2 * VMAX = 4n                   (4n, align with 4)
	 *   SHR1 + 18 <= SHR0 <= (FSC - 4)
	 *
	 *   exp_l = FSC - SHR0
	 *    SHR0 = FSC - exp_l                     (4n, align with 4)
	 *
	 *   exp_s = RHS1 - SHR1
	 *    SHR1 + 4 <= RHS1 < BRL * 2             (8n + 2)
	 *    SHR1 + 4 <= RHS1 <= SHR0 - 18
	 *          18 <= SHR1 <= RHS1 - 4           (4n + 2)
	 *
	 *    RHS1(n+1) >= (RHS1(n) + BRL * 2) - FSC + 2
	 *
	 *    RHS1 and SHR1 shall be even value.
	 *
	 *    T(l_exp) = FSC - SHR0,  unit: H
	 *    T(s_exp) = RHS1 - SHR1, unit: H
	 *    Exposure ratio: T(l_exp) / T(s_exp) >= 1
	 */

	/* The HDR mode vts is already double by default to workaround T-line */
	fsc = imx335->cur_vts;

	shr0 = fsc - l_exp_time;

	rhs1_max = min(RHS1_MAX, shr0 - SHR1_MIN);
	rhs1_max = (rhs1_max & ~0x7) + 2;
	rhs1_min = max(SHR1_MIN + 4u, rhs1_old + 2 * BRL - fsc + 2);
	rhs1_min = (rhs1_min + 7u) / 8 * 8 + 2;
	if (rhs1_max < rhs1_min) {
		dev_err(&client->dev,
			"The total exposure limit makes rhs1 max is %d,but old rhs1 limit makes rhs1 min is %d\n",
			rhs1_max, rhs1_min);
		return -EINVAL;
	}

	rhs1 = SHR1_MIN + s_exp_time;
	rhs1 = (rhs1 & ~0x7) + 2; /* shall be 8n + 2 */
	if (rhs1 > rhs1_max)
		rhs1 = rhs1_max;
	if (rhs1 < rhs1_min)
		rhs1 = rhs1_min;
	dev_dbg(&client->dev,
		"line(%d) rhs1 %d, short time %d rhs1_old %d, rhs1_new %d, rhs1_min %d rhs1_max %d\n",
		__LINE__, rhs1, s_exp_time, rhs1_old, rhs1, rhs1_min, rhs1_max);

	rhs1_old = rhs1;

	/* shr1 = rhs1 - s_exp_time */
	if (rhs1 - s_exp_time <= SHR1_MIN) {
		shr1 = SHR1_MIN;
		s_exp_time = rhs1 - shr1;
	} else {
		shr1 = rhs1 - s_exp_time;
	}
	shr1 = (shr1 & ~0x3) + 2; /* shall be 4n + 2 */

	if (shr0 < rhs1 + 18)
		shr0 = rhs1 + 18;
	else if (shr0 > fsc - 4)
		shr0 = fsc - 4;

	shr0 &= (~0x3);  /* align with 4 */

	dev_dbg(&client->dev,
		"fsc=%d,RHS1_MAX=%d,SHR1_MIN=%d,rhs1_max=%d\n",
		fsc, RHS1_MAX, SHR1_MIN, rhs1_max);
	dev_dbg(&client->dev,
		"l_exp_time=%d,s_exp_time=%d,shr0=%d,shr1=%d,rhs1=%d,l_a_gain=%d,s_a_gain=%d\n",
		l_exp_time, s_exp_time, shr0, shr1, rhs1, l_a_gain, s_a_gain);
	/* time effect n+2 */
	ret |= imx335_write_reg(client,
		IMX335_RHS1_REG_L,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_RHS1_L(rhs1));
	ret |= imx335_write_reg(client,
		IMX335_RHS1_REG_M,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_RHS1_M(rhs1));
	ret |= imx335_write_reg(client,
		IMX335_RHS1_REG_H,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_RHS1_H(rhs1));

	ret |= imx335_write_reg(client,
		IMX335_SF1_EXPO_REG_L,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_L(shr1));
	ret |= imx335_write_reg(client,
		IMX335_SF1_EXPO_REG_M,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_M(shr1));
	ret |= imx335_write_reg(client,
		IMX335_SF1_EXPO_REG_H,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_H(shr1));
	ret |= imx335_write_reg(client,
		IMX335_LF_EXPO_REG_L,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_L(shr0));
	ret |= imx335_write_reg(client,
		IMX335_LF_EXPO_REG_M,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_M(shr0));
	ret |= imx335_write_reg(client,
		IMX335_LF_EXPO_REG_H,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_H(shr0));

	ret |= imx335_write_reg(client, IMX335_GROUP_HOLD_REG,
		IMX335_REG_VALUE_08BIT, IMX335_GROUP_HOLD_END);
	return ret;
}

static int imx335_set_hdrae_3frame(struct imx335 *imx335,
				   struct preisp_hdrae_exp_s *ae)
{
	struct i2c_client *client = imx335->client;
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_a_gain, m_a_gain, s_a_gain;
	int shr2, shr1, shr0, rhs2, rhs1 = 0;
	int rhs1_change_limit, rhs2_change_limit = 0;
	static int rhs1_old = IMX335_RHS1_X3_DEFAULT;
	static int rhs2_old = IMX335_RHS2_X3_DEFAULT;
	int ret = 0;
	u32 fsc;
	int rhs1_max = 0;
	int shr2_min = 0;

	if (!imx335->has_init_exp && !imx335->streaming) {
		imx335->init_hdrae_exp = *ae;
		imx335->has_init_exp = true;
		dev_dbg(&imx335->client->dev, "imx335 is not streaming, save hdr ae!\n");
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
		l_exp_time, l_a_gain, m_exp_time, m_a_gain, s_exp_time, s_a_gain);

	ret = imx335_write_reg(client, IMX335_GROUP_HOLD_REG,
		IMX335_REG_VALUE_08BIT, IMX335_GROUP_HOLD_START);
	/* gain effect n+1 */
	ret |= imx335_write_reg(client, IMX335_LF_GAIN_REG_H,
		IMX335_REG_VALUE_08BIT, IMX335_FETCH_GAIN_H(l_a_gain));
	ret |= imx335_write_reg(client, IMX335_LF_GAIN_REG_L,
		IMX335_REG_VALUE_08BIT, IMX335_FETCH_GAIN_L(l_a_gain));
	ret |= imx335_write_reg(client, IMX335_SF1_GAIN_REG_H,
		IMX335_REG_VALUE_08BIT, IMX335_FETCH_GAIN_H(m_a_gain));
	ret |= imx335_write_reg(client, IMX335_SF1_GAIN_REG_L,
		IMX335_REG_VALUE_08BIT, IMX335_FETCH_GAIN_L(m_a_gain));
	ret |= imx335_write_reg(client, IMX335_SF2_GAIN_REG_H,
		IMX335_REG_VALUE_08BIT, IMX335_FETCH_GAIN_H(s_a_gain));
	ret |= imx335_write_reg(client, IMX335_SF2_GAIN_REG_L,
		IMX335_REG_VALUE_08BIT, IMX335_FETCH_GAIN_L(s_a_gain));

	/* Restrictions
	 *   FSC = 4 * VMAX and FSC should be 6n;
	 *   exp_l = FSC - SHR0 + Toffset;
	 *
	 *   SHR0 = FSC - exp_l + Toffset;
	 *   SHR0 <= (FSC -6);
	 *   SHR0 >= RHS2 + 26;
	 *   SHR0 should be 6n;
	 *
	 *   exp_m = RHS1 - SHR1 + Toffset;
	 *
	 *   RHS1 < BRL * 3;
	 *   RHS1 <= SHR2 - 26;
	 *   RHS1 >= SHR1 + 6;
	 *   SHR1 >= 26;
	 *   SHR1 <= RHS1 - 6;
	 *   RHS1(n+1) >= RHS1(n) + BRL * 3 -FSC + 3;
	 *
	 *   SHR1 should be 6n+2 and RHS1 should be 12n+2;
	 *
	 *   exp_s = RHS2 - SHR2 + Toffset;
	 *
	 *   RHS2 < BRL * 3 + RHS1;
	 *   RHS2 <= SHR0 - 26;
	 *   RHS2 >= SHR2 + 6;
	 *   SHR2 >= RHS1 + 26;
	 *   SHR2 <= RHS2 - 6;
	 *   RHS1(n+1) >= RHS1(n) + BRL * 3 -FSC + 3;
	 *
	 *   SHR2 should be 6n+4 and RHS2 should be 12n+4;
	 */

	/* The HDR mode vts is double by default to workaround T-line */
	fsc = imx335->cur_vts;
	fsc = fsc / 6 * 6;
	shr0 = fsc - l_exp_time;
	dev_dbg(&client->dev,
		"line(%d) shr0 %d, l_exp_time %d, fsc %d\n",
		__LINE__, shr0, l_exp_time, fsc);

	rhs1 = (SHR1_MIN_X3 + m_exp_time + 11) / 12 * 12 + 2;
	rhs1_max = RHS1_MAX_X3;
	if (rhs1 < 32)
		rhs1 = 32;
	else if (rhs1 > rhs1_max)
		rhs1 = rhs1_max;
	dev_dbg(&client->dev,
		"line(%d) rhs1 %d, m_exp_time %d rhs1_old %d\n",
		__LINE__, rhs1, m_exp_time, rhs1_old);

	//Dynamic adjustment rhs2 must meet the following conditions
	rhs1_change_limit = rhs1_old + 3 * BRL - fsc + 3;
	rhs1_change_limit = (rhs1_change_limit < 32) ? 32 : rhs1_change_limit;
	rhs1_change_limit = (rhs1_change_limit + 11) / 12 * 12 + 2;
	if (rhs1_max < rhs1_change_limit) {
		dev_err(&client->dev,
			"The total exposure limit makes rhs1 max is %d,but old rhs1 limit makes rhs1 min is %d\n",
			rhs1_max, rhs1_change_limit);
		return -EINVAL;
	}
	if (rhs1 < rhs1_change_limit)
		rhs1 = rhs1_change_limit;

	dev_dbg(&client->dev,
		"line(%d) m_exp_time %d rhs1_old %d, rhs1_new %d\n",
		__LINE__, m_exp_time, rhs1_old, rhs1);

	rhs1_old = rhs1;

	/* shr1 = rhs1 - s_exp_time */
	if (rhs1 - m_exp_time <= SHR1_MIN_X3) {
		shr1 = SHR1_MIN_X3;
		m_exp_time = rhs1 - shr1;
	} else {
		shr1 = rhs1 - m_exp_time;
	}

	shr2_min = rhs1 + 26;
	rhs2 = (shr2_min + s_exp_time + 11) / 12 * 12 + 4;
	if (rhs2 > (shr0 - 26))
		rhs2 = shr0 - 26;
	else if (rhs2 < 64)
		rhs2 = 64;
	dev_dbg(&client->dev,
		"line(%d) rhs2 %d, s_exp_time %d, rhs2_old %d\n",
		__LINE__, rhs2, s_exp_time, rhs2_old);

	//Dynamic adjustment rhs2 must meet the following conditions
	rhs2_change_limit = rhs2_old + 3 * BRL - fsc + 3;
	rhs2_change_limit = (rhs2_change_limit < 64) ?  64 : rhs2_change_limit;
	rhs2_change_limit = (rhs2_change_limit + 11) / 12 * 12 + 4;
	if ((shr0 - 26) < rhs2_change_limit) {
		dev_err(&client->dev,
			"The total exposure limit makes rhs2 max is %d,but old rhs1 limit makes rhs2 min is %d\n",
			shr0 - 26, rhs2_change_limit);
		return -EINVAL;
	}
	if (rhs2 < rhs2_change_limit)
		rhs2 = rhs2_change_limit;

	rhs2_old = rhs2;

	/* shr2 = rhs2 - s_exp_time */
	if (rhs2 - s_exp_time <= shr2_min) {
		shr2 = shr2_min;
		s_exp_time = rhs2 - shr2;
	} else {
		shr2 = rhs2 - s_exp_time;
	}
	dev_dbg(&client->dev,
		"line(%d) rhs2_new %d, s_exp_time %d shr2 %d, rhs2_change_limit %d\n",
		__LINE__, rhs2, s_exp_time, shr2, rhs2_change_limit);

	if (shr0 < rhs2 + 26)
		shr0 = rhs2 + 26;
	else if (shr0 > fsc - 6)
		shr0 = fsc - 6;

	dev_dbg(&client->dev,
		"long exposure: l_exp_time=%d, fsc=%d, shr0=%d, l_a_gain=%d\n",
		l_exp_time, fsc, shr0, l_a_gain);
	dev_dbg(&client->dev,
		"middle exposure(SEF1): m_exp_time=%d, rhs1=%d, shr1=%d, m_a_gain=%d\n",
		m_exp_time, rhs1, shr1, m_a_gain);
	dev_dbg(&client->dev,
		"short exposure(SEF2): s_exp_time=%d, rhs2=%d, shr2=%d, s_a_gain=%d\n",
		s_exp_time, rhs2, shr2, s_a_gain);
	/* time effect n+1 */
	/* write SEF2 exposure RHS2 regs*/
	ret |= imx335_write_reg(client,
		IMX335_RHS2_REG_L,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_RHS1_L(rhs2));
	ret |= imx335_write_reg(client,
		IMX335_RHS2_REG_M,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_RHS1_M(rhs2));
	ret |= imx335_write_reg(client,
		IMX335_RHS2_REG_H,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_RHS1_H(rhs2));
	/* write SEF2 exposure SHR2 regs*/
	ret |= imx335_write_reg(client,
		IMX335_SF2_EXPO_REG_L,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_L(shr2));
	ret |= imx335_write_reg(client,
		IMX335_SF2_EXPO_REG_M,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_M(shr2));
	ret |= imx335_write_reg(client,
		IMX335_SF2_EXPO_REG_H,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_H(shr2));
	/* write SEF1 exposure RHS1 regs*/
	ret |= imx335_write_reg(client,
		IMX335_RHS1_REG_L,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_RHS1_L(rhs1));
	ret |= imx335_write_reg(client,
		IMX335_RHS1_REG_M,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_RHS1_M(rhs1));
	ret |= imx335_write_reg(client,
		IMX335_RHS1_REG_H,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_RHS1_H(rhs1));
	/* write SEF1 exposure SHR1 regs*/
	ret |= imx335_write_reg(client,
		IMX335_SF1_EXPO_REG_L,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_L(shr1));
	ret |= imx335_write_reg(client,
		IMX335_SF1_EXPO_REG_M,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_M(shr1));
	ret |= imx335_write_reg(client,
		IMX335_SF1_EXPO_REG_H,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_H(shr1));
	/* write LF exposure SHR0 regs*/
	ret |= imx335_write_reg(client,
		IMX335_LF_EXPO_REG_L,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_L(shr0));
	ret |= imx335_write_reg(client,
		IMX335_LF_EXPO_REG_M,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_M(shr0));
	ret |= imx335_write_reg(client,
		IMX335_LF_EXPO_REG_H,
		IMX335_REG_VALUE_08BIT,
		IMX335_FETCH_EXP_H(shr0));

	ret |= imx335_write_reg(client, IMX335_GROUP_HOLD_REG,
		IMX335_REG_VALUE_08BIT, IMX335_GROUP_HOLD_END);
	return ret;
}

static long imx335_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct imx335 *imx335 = to_imx335(sd);
	struct rkmodule_hdr_cfg *hdr;
	u32 i, h, w;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		if (imx335->cur_mode->hdr_mode == HDR_X2)
			ret = imx335_set_hdrae(imx335, arg);
		else if (imx335->cur_mode->hdr_mode == HDR_X3)
			ret = imx335_set_hdrae_3frame(imx335, arg);
		break;
	case RKMODULE_GET_MODULE_INFO:
		imx335_get_module_inf(imx335, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = imx335->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = imx335->cur_mode->width;
		h = imx335->cur_mode->height;
		for (i = 0; i < imx335->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				imx335_change_mode(imx335, &supported_modes[i]);
				break;
			}
		}
		if (i == imx335->cfg_num) {
			dev_err(&imx335->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = imx335->cur_mode->hts_def - imx335->cur_mode->width;
			h = imx335->cur_mode->vts_def - imx335->cur_mode->height;
			__v4l2_ctrl_modify_range(imx335->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(imx335->vblank, h,
				IMX335_VTS_MAX - imx335->cur_mode->height,
				1, h);
		}
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			imx335_write_reg(imx335->client, IMX335_REG_CTRL_MODE,
				IMX335_REG_VALUE_08BIT, 0);
		else
			imx335_write_reg(imx335->client, IMX335_REG_CTRL_MODE,
				IMX335_REG_VALUE_08BIT, 1);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long imx335_compat_ioctl32(struct v4l2_subdev *sd,
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

		ret = imx335_ioctl(sd, cmd, inf);
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
			ret = imx335_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = imx335_ioctl(sd, cmd, hdr);
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
			ret = imx335_ioctl(sd, cmd, hdr);
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
			ret = imx335_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = imx335_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __imx335_start_stream(struct imx335 *imx335)
{
	int ret;

	ret = imx335_write_array(imx335->client, imx335->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&imx335->ctrl_handler);
	if (ret)
		return ret;

	if (imx335->has_init_exp && imx335->cur_mode->hdr_mode != NO_HDR) {
		ret = imx335_ioctl(&imx335->subdev, PREISP_CMD_SET_HDRAE_EXP,
			&imx335->init_hdrae_exp);
		if (ret) {
			dev_err(&imx335->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}
	return imx335_write_reg(imx335->client, IMX335_REG_CTRL_MODE,
				IMX335_REG_VALUE_08BIT, 0);
}

static int __imx335_stop_stream(struct imx335 *imx335)
{
	imx335->has_init_exp = false;
	return imx335_write_reg(imx335->client, IMX335_REG_CTRL_MODE,
				IMX335_REG_VALUE_08BIT, 1);
}

static int imx335_s_stream(struct v4l2_subdev *sd, int on)
{
	struct imx335 *imx335 = to_imx335(sd);
	struct i2c_client *client = imx335->client;
	int ret = 0;

	dev_dbg(&imx335->client->dev, "s_stream: %d. %dx%d, hdr: %d, bpp: %d\n",
	       on, imx335->cur_mode->width, imx335->cur_mode->height,
	       imx335->cur_mode->hdr_mode, imx335->cur_mode->bpp);

	mutex_lock(&imx335->mutex);
	on = !!on;
	if (on == imx335->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __imx335_start_stream(imx335);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__imx335_stop_stream(imx335);
		pm_runtime_put(&client->dev);
	}

	imx335->streaming = on;

unlock_and_return:
	mutex_unlock(&imx335->mutex);

	return ret;
}

static int imx335_s_power(struct v4l2_subdev *sd, int on)
{
	struct imx335 *imx335 = to_imx335(sd);
	struct i2c_client *client = imx335->client;
	int ret = 0;

	mutex_lock(&imx335->mutex);

	if (imx335->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}
		imx335->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		imx335->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&imx335->mutex);

	return ret;
}

static int __imx335_power_on(struct imx335 *imx335)
{
	int ret;
	struct device *dev = &imx335->client->dev;

	if (!IS_ERR_OR_NULL(imx335->pins_default)) {
		ret = pinctrl_select_state(imx335->pinctrl,
					   imx335->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	ret = regulator_bulk_enable(IMX335_NUM_SUPPLIES, imx335->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto err_pinctrl;
	}

	if (!IS_ERR(imx335->reset_gpio))
		gpiod_set_value_cansleep(imx335->reset_gpio, 1);

	/* At least 500ns between power raising and Reset */
	udelay(10);
	if (!IS_ERR(imx335->reset_gpio))
		gpiod_set_value_cansleep(imx335->reset_gpio, 0);

	ret = clk_set_rate(imx335->xvclk, IMX335_XVCLK_FREQ_37M);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate\n");
	if (clk_get_rate(imx335->xvclk) != IMX335_XVCLK_FREQ_37M)
		dev_warn(dev, "xvclk mismatched\n");
	ret = clk_prepare_enable(imx335->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		goto err_clk;
	}

	/* At least 20us between Reset and I2C communication */
	usleep_range(20, 30);

	return 0;

err_clk:
	if (!IS_ERR(imx335->reset_gpio))
		gpiod_set_value_cansleep(imx335->reset_gpio, 1);
	regulator_bulk_disable(IMX335_NUM_SUPPLIES, imx335->supplies);

err_pinctrl:
	if (!IS_ERR_OR_NULL(imx335->pins_sleep))
		pinctrl_select_state(imx335->pinctrl, imx335->pins_sleep);

	return ret;
}

static void __imx335_power_off(struct imx335 *imx335)
{
	int ret;
	struct device *dev = &imx335->client->dev;

	if (!IS_ERR(imx335->reset_gpio))
		gpiod_set_value_cansleep(imx335->reset_gpio, 1);
	clk_disable_unprepare(imx335->xvclk);
	if (!IS_ERR_OR_NULL(imx335->pins_sleep)) {
		ret = pinctrl_select_state(imx335->pinctrl,
					   imx335->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(IMX335_NUM_SUPPLIES, imx335->supplies);
}

static int imx335_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx335 *imx335 = to_imx335(sd);

	return __imx335_power_on(imx335);
}

static int imx335_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx335 *imx335 = to_imx335(sd);

	__imx335_power_off(imx335);

	return 0;
}

static int imx335_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct imx335 *imx335 = to_imx335(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct imx335_mode *def_mode = &supported_modes[0];

	mutex_lock(&imx335->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&imx335->mutex);
	/* No crop or compose */

	return 0;
}

static int imx335_enum_frame_interval(struct v4l2_subdev *sd,
	struct v4l2_subdev_pad_config *cfg,
	struct v4l2_subdev_frame_interval_enum *fie)
{
	struct imx335 *imx335 = to_imx335(sd);

	if (fie->index >= imx335->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

#define DST_WIDTH 2592
#define DST_HEIGHT 1944

/*
 * The resolution of the driver configuration needs to be exactly
 * the same as the current output resolution of the sensor,
 * the input width of the isp needs to be 16 aligned,
 * the input height of the isp needs to be 8 aligned.
 * Can be cropped to standard resolution by this function,
 * otherwise it will crop out strange resolution according
 * to the alignment rules.
 */
static int imx335_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	/*
	 * From "Pixel Array Image Drawing in All scan mode",
	 * there are 12 pixel offset on horizontal and vertical.
	 */
	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = 12;
		sel->r.width = DST_WIDTH;
		sel->r.top = 12;
		sel->r.height = DST_HEIGHT;
		return 0;
	}
	return -EINVAL;
}

static const struct dev_pm_ops imx335_pm_ops = {
	SET_RUNTIME_PM_OPS(imx335_runtime_suspend,
			   imx335_runtime_resume, NULL)
};

static const struct v4l2_subdev_internal_ops imx335_internal_ops = {
	.open = imx335_open,
};

static const struct v4l2_subdev_core_ops imx335_core_ops = {
	.s_power = imx335_s_power,
	.ioctl = imx335_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = imx335_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops imx335_video_ops = {
	.s_stream = imx335_s_stream,
	.g_frame_interval = imx335_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops imx335_pad_ops = {
	.enum_mbus_code = imx335_enum_mbus_code,
	.enum_frame_size = imx335_enum_frame_sizes,
	.enum_frame_interval = imx335_enum_frame_interval,
	.get_fmt = imx335_get_fmt,
	.set_fmt = imx335_set_fmt,
	.get_selection = imx335_get_selection,
	.get_mbus_config = imx335_g_mbus_config,
};

static const struct v4l2_subdev_ops imx335_subdev_ops = {
	.core	= &imx335_core_ops,
	.video	= &imx335_video_ops,
	.pad	= &imx335_pad_ops,
};

static int imx335_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx335 *imx335 = container_of(ctrl->handler,
					     struct imx335, ctrl_handler);
	struct i2c_client *client = imx335->client;
	s64 max;
	u32 vts = 0;
	int ret = 0;
	u32 shr0 = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		if (imx335->cur_mode->hdr_mode == NO_HDR) {
			/* Update max exposure while meeting expected vblanking */
			max = imx335->cur_mode->height + ctrl->val - SHR0_MIN;
			__v4l2_ctrl_modify_range(imx335->exposure,
					 imx335->exposure->minimum, max,
					 imx335->exposure->step,
					 imx335->exposure->default_value);
		}
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (imx335->cur_mode->hdr_mode != NO_HDR)
			return ret;
		shr0 = imx335->cur_vts - ctrl->val;
		ret = imx335_write_reg(imx335->client, IMX335_LF_EXPO_REG_L,
				       IMX335_REG_VALUE_08BIT,
				       IMX335_FETCH_EXP_L(shr0));
		ret |= imx335_write_reg(imx335->client, IMX335_LF_EXPO_REG_M,
				       IMX335_REG_VALUE_08BIT,
				       IMX335_FETCH_EXP_M(shr0));
		ret |= imx335_write_reg(imx335->client, IMX335_LF_EXPO_REG_H,
				       IMX335_REG_VALUE_08BIT,
				       IMX335_FETCH_EXP_H(shr0));
		dev_dbg(&client->dev, "set exposure(shr0) %d = cur_vts(%d) - val(%d)\n",
			shr0, imx335->cur_vts, ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		if (imx335->cur_mode->hdr_mode != NO_HDR)
			return ret;
		ret = imx335_write_reg(imx335->client, IMX335_LF_GAIN_REG_H,
				       IMX335_REG_VALUE_08BIT,
				       IMX335_FETCH_GAIN_H(ctrl->val));
		ret |= imx335_write_reg(imx335->client, IMX335_LF_GAIN_REG_L,
				       IMX335_REG_VALUE_08BIT,
				       IMX335_FETCH_GAIN_L(ctrl->val));
		dev_dbg(&client->dev, "set analog gain 0x%x\n", ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		vts = ctrl->val + imx335->cur_mode->height;
		/*
		 * vts of hdr mode is double to correct T-line calculation.
		 * Restore before write to reg.
		 */
		if (imx335->cur_mode->hdr_mode == HDR_X2) {
			vts = (vts + 3) / 4 * 4;
			imx335->cur_vts = vts;
			vts /= 2;
		} else if (imx335->cur_mode->hdr_mode == HDR_X3) {
			vts = (vts + 11) / 12 * 12;
			imx335->cur_vts = vts;
			vts /= 4;
		} else {
			imx335->cur_vts = vts;
		}
		ret = imx335_write_reg(imx335->client, IMX335_VTS_REG_L,
				       IMX335_REG_VALUE_08BIT,
				       IMX335_FETCH_VTS_L(vts));
		ret |= imx335_write_reg(imx335->client, IMX335_VTS_REG_M,
				       IMX335_REG_VALUE_08BIT,
				       IMX335_FETCH_VTS_M(vts));
		ret |= imx335_write_reg(imx335->client, IMX335_VTS_REG_H,
				       IMX335_REG_VALUE_08BIT,
				       IMX335_FETCH_VTS_H(vts));
		dev_dbg(&client->dev, "set vblank 0x%x\n", ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = imx335_write_reg(imx335->client, IMX335_HREVERSE_REG,
				       IMX335_REG_VALUE_08BIT, !!ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		if (ctrl->val) {
			ret = imx335_write_reg(imx335->client, IMX335_VREVERSE_REG,
					       IMX335_REG_VALUE_08BIT, !!ctrl->val);
			ret |= imx335_write_reg(imx335->client, 0x3081,
					       IMX335_REG_VALUE_08BIT, 0xfe);
			ret |= imx335_write_reg(imx335->client, 0x3083,
					       IMX335_REG_VALUE_08BIT, 0xfe);
			ret |= imx335_write_reg(imx335->client, 0x30b6,
					       IMX335_REG_VALUE_08BIT, 0xfa);
			ret |= imx335_write_reg(imx335->client, 0x30b7,
					       IMX335_REG_VALUE_08BIT, 0x01);
			ret |= imx335_write_reg(imx335->client, 0x3116,
					       IMX335_REG_VALUE_08BIT, 0x02);
			ret |= imx335_write_reg(imx335->client, 0x3117,
					       IMX335_REG_VALUE_08BIT, 0x00);
		} else {
			ret = imx335_write_reg(imx335->client, IMX335_VREVERSE_REG,
					       IMX335_REG_VALUE_08BIT, !!ctrl->val);
			ret |= imx335_write_reg(imx335->client, 0x3081,
					       IMX335_REG_VALUE_08BIT, 0x02);
			ret |= imx335_write_reg(imx335->client, 0x3083,
					       IMX335_REG_VALUE_08BIT, 0x02);
			ret |= imx335_write_reg(imx335->client, 0x30b6,
					       IMX335_REG_VALUE_08BIT, 0x00);
			ret |= imx335_write_reg(imx335->client, 0x30b7,
					       IMX335_REG_VALUE_08BIT, 0x00);
			ret |= imx335_write_reg(imx335->client, 0x3116,
					       IMX335_REG_VALUE_08BIT, 0x08);
			ret |= imx335_write_reg(imx335->client, 0x3117,
					       IMX335_REG_VALUE_08BIT, 0x00);
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

static const struct v4l2_ctrl_ops imx335_ctrl_ops = {
	.s_ctrl = imx335_set_ctrl,
};

static int imx335_initialize_controls(struct imx335 *imx335)
{
	const struct imx335_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u64 pixel_rate;
	u32 h_blank;
	int ret;

	handler = &imx335->ctrl_handler;
	mode = imx335->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &imx335->mutex;

	imx335->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ,
				ARRAY_SIZE(link_freq_items) - 1, 0,
				link_freq_items);

	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	pixel_rate = (u32)link_freq_items[0] / mode->bpp * 2 * IMX335_4LANES;
	imx335->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
		V4L2_CID_PIXEL_RATE, 0, pixel_rate, 1, pixel_rate);

	h_blank = mode->hts_def - mode->width;
	imx335->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (imx335->hblank)
		imx335->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	imx335->vblank = v4l2_ctrl_new_std(handler, &imx335_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				IMX335_VTS_MAX - mode->height,
				1, vblank_def);
	imx335->cur_vts = mode->vts_def;

	exposure_max = mode->vts_def - SHR0_MIN;
	imx335->exposure = v4l2_ctrl_new_std(handler, &imx335_ctrl_ops,
				V4L2_CID_EXPOSURE, IMX335_EXPOSURE_MIN,
				exposure_max, IMX335_EXPOSURE_STEP,
				mode->exp_def);

	imx335->anal_a_gain = v4l2_ctrl_new_std(handler, &imx335_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, IMX335_GAIN_MIN,
				IMX335_GAIN_MAX, IMX335_GAIN_STEP,
				IMX335_GAIN_DEFAULT);

	v4l2_ctrl_new_std(handler, &imx335_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &imx335_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&imx335->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	imx335->subdev.ctrl_handler = handler;
	imx335->has_init_exp = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int imx335_check_sensor_id(struct imx335 *imx335,
				  struct i2c_client *client)
{
	struct device *dev = &imx335->client->dev;
	u32 id = 0;
	int ret;

	ret = imx335_read_reg(client, IMX335_REG_CHIP_ID,
			      IMX335_REG_VALUE_08BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected imx335 id %06x\n", CHIP_ID);

	return 0;
}

static int imx335_configure_regulators(struct imx335 *imx335)
{
	unsigned int i;

	for (i = 0; i < IMX335_NUM_SUPPLIES; i++)
		imx335->supplies[i].supply = imx335_supply_names[i];

	return devm_regulator_bulk_get(&imx335->client->dev,
				       IMX335_NUM_SUPPLIES,
				       imx335->supplies);
}

static int imx335_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct imx335 *imx335;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	imx335 = devm_kzalloc(dev, sizeof(*imx335), GFP_KERNEL);
	if (!imx335)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &imx335->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &imx335->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &imx335->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &imx335->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}
	imx335->client = client;
	imx335->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < imx335->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			imx335->cur_mode = &supported_modes[i];
			break;
		}
	}

	imx335->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(imx335->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	imx335->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(imx335->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	imx335->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(imx335->pinctrl)) {
		imx335->pins_default =
			pinctrl_lookup_state(imx335->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(imx335->pins_default))
			dev_info(dev, "could not get default pinstate\n");

		imx335->pins_sleep =
			pinctrl_lookup_state(imx335->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(imx335->pins_sleep))
			dev_info(dev, "could not get sleep pinstate\n");
	} else {
		dev_info(dev, "no pinctrl\n");
	}

	ret = imx335_configure_regulators(imx335);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&imx335->mutex);

	sd = &imx335->subdev;
	v4l2_i2c_subdev_init(sd, client, &imx335_subdev_ops);
	ret = imx335_initialize_controls(imx335);
	if (ret)
		goto err_destroy_mutex;

	ret = __imx335_power_on(imx335);
	if (ret)
		goto err_free_handler;

	ret = imx335_check_sensor_id(imx335, client);
	if (ret)
		goto err_power_off;

	sd->internal_ops = &imx335_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;

#if defined(CONFIG_MEDIA_CONTROLLER)
	imx335->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &imx335->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(imx335->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 imx335->module_index, facing,
		 IMX335_NAME, dev_name(sd->dev));
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
	__imx335_power_off(imx335);
err_free_handler:
	v4l2_ctrl_handler_free(&imx335->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&imx335->mutex);

	return ret;
}

static int imx335_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx335 *imx335 = to_imx335(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&imx335->ctrl_handler);
	mutex_destroy(&imx335->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__imx335_power_off(imx335);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id imx335_of_match[] = {
	{ .compatible = "sony,imx335" },
	{},
};
MODULE_DEVICE_TABLE(of, imx335_of_match);
#endif

static const struct i2c_device_id imx335_match_id[] = {
	{ "sony,imx335", 0 },
	{ },
};

static struct i2c_driver imx335_i2c_driver = {
	.driver = {
		.name = IMX335_NAME,
		.pm = &imx335_pm_ops,
		.of_match_table = of_match_ptr(imx335_of_match),
	},
	.probe		= &imx335_probe,
	.remove		= &imx335_remove,
	.id_table	= imx335_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&imx335_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&imx335_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Sony imx335 sensor driver");
MODULE_LICENSE("GPL v2");
