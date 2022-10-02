// SPDX-License-Identifier: GPL-2.0
/*
 * sc2310 driver
 *
 * Copyright (C) 2020 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version,adjust sc2310.
 * V0.0X01.0X01 add set flip ctrl.
 * V0.0X01.0X02 1.fixed time limit error
 *		2.fixed gain conversion function
 *		3.fixed test pattern error
 *		4.add quick stream on/off
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

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x02)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MIPI_FREQ_186M			186000000 //371.25Mbps/lane
#define MIPI_FREQ_380M			380000000 //760Mbps/lane

#define SC2310_MAX_PIXEL_RATE		(MIPI_FREQ_380M * 2 / 10 * 2)
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"

#define SC2310_XVCLK_FREQ		24000000

#define CHIP_ID				0x2311
#define SC2310_REG_CHIP_ID		0x3107

#define SC2310_REG_CTRL_MODE		0x0100
#define SC2310_MODE_SW_STANDBY		0x0
#define SC2310_MODE_STREAMING		BIT(0)

#define	SC2310_EXPOSURE_MIN		2// two lines long exp min
#define	SC2310_EXPOSURE_STEP	1
#define SC2310_VTS_MAX			0xffff

//long exposure
#define SC2310_REG_EXP_LONG_H		0x3e00    //[3:0]
#define SC2310_REG_EXP_LONG_M		0x3e01    //[7:0]
#define SC2310_REG_EXP_LONG_L		0x3e02    //[7:4]

//short exposure
#define SC2310_REG_EXP_SF_H		0x3e04    //[7:0]
#define SC2310_REG_EXP_SF_L		0x3e05    //[7:4]

//long frame and normal gain reg
#define SC2310_REG_AGAIN		0x3e08
#define SC2310_REG_AGAIN_FINE		0x3e09

#define SC2310_REG_DGAIN		0x3e06
#define SC2310_REG_DGAIN_FINE		0x3e07

//short fram gain reg
#define SC2310_SF_REG_AGAIN		0x3e12
#define SC2310_SF_REG_AGAIN_FINE	0x3e13

#define SC2310_SF_REG_DGAIN		0x3e10
#define SC2310_SF_REG_DGAIN_FINE	0x3e11

#define SC2310_GAIN_MIN			0x40
#define SC2310_GAIN_MAX			(44 * 32 * 64)
#define SC2310_GAIN_STEP		1
#define SC2310_GAIN_DEFAULT		0x40

//group hold
#define SC2310_GROUP_UPDATE_ADDRESS	0x3812
#define SC2310_GROUP_UPDATE_START_DATA	0x00
#define SC2310_GROUP_UPDATE_LAUNCH	0x30

#define SC2310_SOFTWARE_RESET_REG	0x0103
#define SC2310_REG_TEST_PATTERN		0x4501
#define SC2310_TEST_PATTERN_ENABLE	0x08

#define SC2310_REG_VTS			0x320e
#define SC2310_FLIP_REG			0x3221
#define SC2310_FLIP_MASK		0x60
#define SC2310_MIRROR_MASK		0x06
#define REG_NULL			0xFFFF

#define SC2310_REG_VALUE_08BIT		1
#define SC2310_REG_VALUE_16BIT		2
#define SC2310_REG_VALUE_24BIT		3

#define SC2310_LANES			2
#define LONG_FRAME_MAX_EXP		4297
#define SHORT_FRAME_MAX_EXP		260

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define SC2310_NAME			"sc2310"

static const char * const sc2310_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define SC2310_NUM_SUPPLIES ARRAY_SIZE(sc2310_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct sc2310_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 mipi_freq_idx;
	u32 bpp;
	u32 vc[PAD_MAX];
};

struct sc2310 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[SC2310_NUM_SUPPLIES];
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
	struct v4l2_fract	cur_fps;
	bool			streaming;
	bool			power_on;
	const struct sc2310_mode *cur_mode;
	u32			cfg_num;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	bool			has_init_exp;
	u32			cur_vts;
	struct preisp_hdrae_exp_s init_hdrae_exp;
};

#define to_sc2310(sd) container_of(sd, struct sc2310, subdev)

/*
 * Xclk 24Mhz linear 1920*1080 30fps 37.125Mbps/lane
 */
static const struct regval sc2310_linear10bit_1920x1080_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0x80},
	{0x36f9, 0x80},
	{0x3001, 0xfe},
	{0x3018, 0x33},
	{0x3019, 0x0c},
	{0x301c, 0x78},
	{0x301f, 0x40},
	{0x3031, 0x0a},
	{0x3037, 0x22},
	{0x3038, 0x22},
	{0x303f, 0x01},
	{0x3200, 0x00},
	{0x3201, 0x04},
	{0x3202, 0x00},
	{0x3203, 0x04},
	{0x3204, 0x07},
	{0x3205, 0x8b},
	{0x3206, 0x04},
	{0x3207, 0x43},
	{0x3208, 0x07},
	{0x3209, 0x80},
	{0x320a, 0x04},
	{0x320b, 0x38},
	{0x320c, 0x04},
	{0x320d, 0x4c},
	{0x3210, 0x00},
	{0x3211, 0x04},
	{0x3212, 0x00},
	{0x3213, 0x04},
	{0x3301, 0x10},
	{0x3302, 0x10},
	{0x3303, 0x18},
	{0x3306, 0x60},
	{0x3308, 0x08},
	{0x3309, 0x30},
	{0x330a, 0x00},
	{0x330b, 0xc8},
	{0x330e, 0x28},
	{0x3314, 0x04},
	{0x331b, 0x83},
	{0x331e, 0x11},
	{0x331f, 0x29},
	{0x3320, 0x01},
	{0x3324, 0x02},
	{0x3325, 0x02},
	{0x3326, 0x00},
	{0x3333, 0x30},
	{0x3334, 0x40},
	{0x333d, 0x08},
	{0x3341, 0x07},
	{0x3343, 0x03},
	{0x3364, 0x1d},
	{0x3366, 0x80},
	{0x3367, 0x08},
	{0x3368, 0x04},
	{0x3369, 0x00},
	{0x336a, 0x00},
	{0x336b, 0x00},
	{0x336c, 0x42},
	{0x337f, 0x03},
	{0x3380, 0x1b},
	{0x33aa, 0x00},
	{0x33b6, 0x07},
	{0x33b7, 0x07},
	{0x33b8, 0x10},
	{0x33b9, 0x10},
	{0x33ba, 0x10},
	{0x33bb, 0x07},
	{0x33bc, 0x07},
	{0x33bd, 0x20},
	{0x33be, 0x20},
	{0x33bf, 0x20},
	{0x360f, 0x05},
	{0x3621, 0xac},
	{0x3622, 0xe6},
	{0x3623, 0x18},
	{0x3624, 0x47},
	{0x3630, 0xc8},
	{0x3631, 0x88},
	{0x3632, 0x18},
	{0x3633, 0x22},
	{0x3634, 0x44},
	{0x3635, 0x40},
	{0x3636, 0x65},
	{0x3637, 0x17},
	{0x3638, 0x25},
	{0x363b, 0x08},
	{0x363c, 0x05},
	{0x363d, 0x05},
	{0x3640, 0x00},
	{0x366e, 0x04},
	{0x3670, 0x4a},
	{0x3671, 0xf6},
	{0x3672, 0x16},
	{0x3673, 0x16},
	{0x3674, 0xc8},
	{0x3675, 0x54},
	{0x3676, 0x18},
	{0x3677, 0x22},
	{0x3678, 0x33},
	{0x3679, 0x44},
	{0x367a, 0x40},
	{0x367b, 0x40},
	{0x367c, 0x40},
	{0x367d, 0x58},
	{0x367e, 0x40},
	{0x367f, 0x58},
	{0x3696, 0x83},
	{0x3697, 0x87},
	{0x3698, 0x9f},
	{0x36a0, 0x58},
	{0x36a1, 0x78},
	{0x36ea, 0x9f},
	{0x36eb, 0x0e},
	{0x36ec, 0x1e},
	{0x36ed, 0x03},
	{0x36fa, 0xf8},
	{0x36fb, 0x10},
	{0x3802, 0x00},
	{0x3907, 0x01},
	{0x3908, 0x01},
	{0x391e, 0x00},
	{0x391f, 0xc0},
	{0x3933, 0x28},
	{0x3934, 0x0a},
	{0x3940, 0x1b},
	{0x3941, 0x40},
	{0x3942, 0x08},
	{0x3943, 0x0e},
	{0x3e00, 0x00},
	{0x3e01, 0x8c},
	{0x3e02, 0x40},
	{0x3e03, 0x0b},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3e0e, 0x66},
	{0x3e14, 0xb0},
	{0x3e1e, 0x35},
	{0x3e25, 0x03},
	{0x3e26, 0x40},
	{0x3f00, 0x0d},
	{0x3f04, 0x02},
	{0x3f05, 0x1e},
	{0x3f08, 0x04},
	{0x4500, 0x59},
	{0x4501, 0xb4},
	{0x4509, 0x20},
	{0x4603, 0x00},
	{0x4809, 0x01},
	{0x4837, 0x35},
	{0x5000, 0x06},
	{0x5780, 0x7f},
	{0x5781, 0x06},
	{0x5782, 0x04},
	{0x5783, 0x02},
	{0x5784, 0x01},
	{0x5785, 0x16},
	{0x5786, 0x12},
	{0x5787, 0x08},
	{0x5788, 0x02},
	{0x57a0, 0x00},
	{0x57a1, 0x74},
	{0x57a2, 0x01},
	{0x57a3, 0xf4},
	{0x57a4, 0xf0},
	{0x6000, 0x00},
	{0x6002, 0x00},
	{0x36e9, 0x51},
	{0x36f9, 0x04},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz hdr 2to1 STAGGER 1920*1080 30fps 760Mbps/lane
 */
static __maybe_unused const struct regval sc2310_hdr10bit_1920x1080_regs[] = {
	//{0x0103, 0x01},
	{0x303f, 0x01},
	{0x0100, 0x00},
	{0x36e9, 0xa6},
	{0x36f9, 0x85},
	{0x4509, 0x10},
	{0x337f, 0x03},
	{0x3368, 0x04},
	{0x3369, 0x00},
	{0x336a, 0x00},
	{0x336b, 0x00},
	{0x3367, 0x08},
	{0x3326, 0x00},
	{0x3631, 0x88},
	{0x3018, 0x33},
	{0x3031, 0x0a},
	{0x3001, 0xfe},
	{0x4603, 0x00},
	{0x3640, 0x00},
	{0x3907, 0x01},
	{0x3908, 0x01},
	{0x3320, 0x01},
	{0x57a4, 0xf0},
	{0x3333, 0x30},
	{0x331b, 0x83},
	{0x3334, 0x40},
	{0x3302, 0x10},
	{0x36eb, 0x0a},
	{0x36ec, 0x0e},
	{0x3f08, 0x04},
	{0x4501, 0xa4},
	{0x3309, 0x48},
	{0x331f, 0x39},
	{0x330a, 0x00},
	{0x3308, 0x10},
	{0x3366, 0xc0},
	{0x33aa, 0x00},
	{0x391e, 0x00},
	{0x391f, 0xc0},
	{0x3634, 0x44},
	{0x4500, 0x59},
	{0x3623, 0x18},
	{0x3f00, 0x0d},
	{0x336c, 0x42},
	{0x3933, 0x28},
	{0x3934, 0x0a},
	{0x3940, 0x1b},
	{0x3941, 0x40},
	{0x3942, 0x08},
	{0x3943, 0x0e},
	{0x3624, 0x47},
	{0x3621, 0xac},
	{0x3222, 0x29},
	{0x3901, 0x02},
	{0x363b, 0x08},
	{0x363c, 0x05},
	{0x363d, 0x05},
	{0x3324, 0x02},
	{0x3325, 0x02},
	{0x333d, 0x08},
	{0x3314, 0x04},
	{0x3802, 0x00},
	{0x3e14, 0xb0},
	{0x3e1e, 0x35},
	{0x3e0e, 0x66},
	{0x3364, 0x1d},
	{0x33b6, 0x07},
	{0x33b7, 0x07},
	{0x33b8, 0x10},
	{0x33b9, 0x10},
	{0x33ba, 0x10},
	{0x33bb, 0x07},
	{0x33bc, 0x07},
	{0x33bd, 0x18},
	{0x33be, 0x18},
	{0x33bf, 0x18},
	{0x360f, 0x05},
	{0x367a, 0x40},
	{0x367b, 0x40},
	{0x3671, 0xf6},
	{0x3672, 0x16},
	{0x3673, 0x16},
	{0x366e, 0x04},
	{0x367c, 0x40},
	{0x367d, 0x58},
	{0x3674, 0xc8},
	{0x3675, 0x54},
	{0x3676, 0x18},
	{0x367e, 0x40},
	{0x367f, 0x58},
	{0x3677, 0x22},
	{0x3678, 0x53},
	{0x3679, 0x55},
	{0x36a0, 0x58},
	{0x36a1, 0x78},
	{0x3696, 0x9f},
	{0x3697, 0x9f},
	{0x3698, 0x9f},
	{0x301c, 0x78},
	{0x3037, 0x24},
	{0x3038, 0x44},
	{0x3632, 0x18},
	{0x4809, 0x01},
	{0x3625, 0x01},
	{0x3670, 0x6a},
	{0x369e, 0x40},
	{0x369f, 0x40},
	{0x3693, 0x20},
	{0x3694, 0x40},
	{0x3695, 0x40},
	{0x5000, 0x06},
	{0x5780, 0x7f},
	{0x57a0, 0x00},
	{0x57a1, 0x74},
	{0x57a2, 0x01},
	{0x57a3, 0xf4},
	{0x5781, 0x06},
	{0x5782, 0x04},
	{0x5783, 0x02},
	{0x5784, 0x01},
	{0x5785, 0x16},
	{0x5786, 0x12},
	{0x5787, 0x08},
	{0x5788, 0x02},
	{0x3637, 0x0c},
	{0x3638, 0x24},
	{0x3200, 0x00},
	{0x3201, 0x04},
	{0x3202, 0x00},
	{0x3203, 0x00},
	{0x3204, 0x07},
	{0x3205, 0x8b},
	{0x3206, 0x04},
	{0x3207, 0x3f},
	{0x3208, 0x07},
	{0x3209, 0x80},
	{0x320a, 0x04},
	{0x320b, 0x38},
	{0x3211, 0x04},
	{0x3213, 0x04},
	{0x3380, 0x1b},
	{0x3341, 0x07},
	{0x3343, 0x03},
	{0x3e25, 0x03},
	{0x3e26, 0x40},
	{0x391d, 0x24},
	{0x36ea, 0x2d},
	{0x36ed, 0x23},
	{0x36fa, 0x6a},
	{0x36fb, 0x20},
	{0x320c, 0x04},
	{0x320d, 0x76},
	{0x3636, 0xa8},
	{0x3f04, 0x02},
	{0x3f05, 0x33},
	{0x4837, 0x1a},
	{0x331e, 0x21},
	{0x3303, 0x30},
	{0x330b, 0xb8},
	{0x3306, 0x5c},
	{0x330e, 0x30},
	{0x4816, 0x51},
	{0x3220, 0x51},
	{0x4602, 0x0f},
	{0x33c0, 0x05},
	{0x6000, 0x06},
	{0x6002, 0x06},
	{0x320e, 0x0a},//{0x320e, 0x08},
	{0x320f, 0x66},//{0x320f, 0xaa},
	{0x3e00, 0x01},
	{0x3e01, 0x03},
	{0x3e02, 0xe0},
	{0x3e04, 0x10},
	{0x3e05, 0x40},
	{0x3e23, 0x00},
	{0x3e24, 0x86},
	{0x3e03, 0x0b},
	{0x3e06, 0x00},
	{0x3e07, 0x80},
	{0x3e08, 0x03},
	{0x3e09, 0x40},
	{0x3622, 0xf6},
	{0x3633, 0x22},
	{0x3630, 0xc8},
	{0x3301, 0x10},
	{0x363a, 0x83},
	{0x3635, 0x20},
	{0x36e9, 0x40},
	{0x36f9, 0x05},
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
static const struct sc2310_mode supported_modes[] = {
	{
		/* linear modes */
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x048c / 2,
		.hts_def = 0x044c * 2,
		.vts_def = 0x0465,
		.reg_list = sc2310_linear10bit_1920x1080_regs,
		.hdr_mode = NO_HDR,
		.mipi_freq_idx = 0,
		.bpp = 10,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
	{
		/* 2 to 1 hdr */
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.exp_def = 0x103e / 2,
		.hts_def = 0x0476 * 2,
		.vts_def = 0x0a66,//0x08aa
		.reg_list = sc2310_hdr10bit_1920x1080_regs,
		.hdr_mode = HDR_X2,
		.mipi_freq_idx = 1,
		.bpp = 10,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
};

static const s64 link_freq_items[] = {
	MIPI_FREQ_186M,
	MIPI_FREQ_380M,
};

static const char * const sc2310_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1"
};

/* Write registers up to 4 at a time */
static int sc2310_write_reg(struct i2c_client *client, u16 reg,
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

static int sc2310_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		ret |= sc2310_write_reg(client, regs[i].addr,
			SC2310_REG_VALUE_08BIT, regs[i].val);
	}
	return ret;
}

/* Read registers up to 4 at a time */
static int sc2310_read_reg(struct i2c_client *client,
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

static int sc2310_get_reso_dist(const struct sc2310_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct sc2310_mode *
sc2310_find_best_fit(struct sc2310 *sc2310, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < sc2310->cfg_num; i++) {
		dist = sc2310_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
			(supported_modes[i].bus_fmt == framefmt->code)) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static void sc2310_change_mode(struct sc2310 *sc2310, const struct sc2310_mode *mode)
{
	sc2310->cur_mode = mode;
	sc2310->cur_vts = sc2310->cur_mode->vts_def;
	dev_info(&sc2310->client->dev, "set fmt: cur_mode: %dx%d, hdr: %d\n",
		mode->width, mode->height, mode->hdr_mode);
}

static int sc2310_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sc2310 *sc2310 = to_sc2310(sd);
	const struct sc2310_mode *mode;
	s64 h_blank, vblank_def;
	u64 pixel_rate = 0;

	mutex_lock(&sc2310->mutex);

	mode = sc2310_find_best_fit(sc2310, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&sc2310->mutex);
		return -ENOTTY;
#endif
	} else {
		sc2310_change_mode(sc2310, mode);
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(sc2310->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(sc2310->vblank, vblank_def,
					 SC2310_VTS_MAX - mode->height,
					 1, vblank_def);
		__v4l2_ctrl_s_ctrl(sc2310->link_freq, mode->mipi_freq_idx);
		pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] /
			mode->bpp * 2 * SC2310_LANES;
		__v4l2_ctrl_s_ctrl_int64(sc2310->pixel_rate, pixel_rate);
		sc2310->cur_fps = mode->max_fps;
		sc2310->cur_vts = mode->vts_def;
	}

	mutex_unlock(&sc2310->mutex);

	return 0;
}

static int sc2310_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct sc2310 *sc2310 = to_sc2310(sd);
	const struct sc2310_mode *mode = sc2310->cur_mode;

	mutex_lock(&sc2310->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&sc2310->mutex);
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
	mutex_unlock(&sc2310->mutex);

	return 0;
}

static int sc2310_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct sc2310 *sc2310 = to_sc2310(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = sc2310->cur_mode->bus_fmt;

	return 0;
}

static int sc2310_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct sc2310 *sc2310 = to_sc2310(sd);

	if (fse->index >= sc2310->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int sc2310_enable_test_pattern(struct sc2310 *sc2310, u32 pattern)
{
	u32 val = 0;
	int ret = 0;

	ret = sc2310_read_reg(sc2310->client, SC2310_REG_TEST_PATTERN,
			      SC2310_REG_VALUE_08BIT, &val);
	if (pattern)
		val |= SC2310_TEST_PATTERN_ENABLE;
	else
		val &= ~SC2310_TEST_PATTERN_ENABLE;
	ret |= sc2310_write_reg(sc2310->client, SC2310_REG_TEST_PATTERN,
				SC2310_REG_VALUE_08BIT, val);
	return ret;
}

static int sc2310_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct sc2310 *sc2310 = to_sc2310(sd);
	const struct sc2310_mode *mode = sc2310->cur_mode;

	if (sc2310->streaming)
		fi->interval = sc2310->cur_fps;
	else
		fi->interval = mode->max_fps;

	return 0;
}

static int sc2310_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct sc2310 *sc2310 = to_sc2310(sd);
	const struct sc2310_mode *mode = sc2310->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (SC2310_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	if (mode->hdr_mode == HDR_X2)
		val = 1 << (SC2310_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		V4L2_MBUS_CSI2_CHANNEL_1;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void sc2310_get_module_inf(struct sc2310 *sc2310,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, SC2310_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, sc2310->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, sc2310->len_name, sizeof(inf->base.lens));
}

static void sc2310_get_gain_reg(u32 val, u32 *again_reg, u32 *again_fine_reg,
	u32 *dgain_reg, u32 *dgain_fine_reg)
{
	u8 u8Reg0x3e09 = 0x40, u8Reg0x3e08 = 0x03, u8Reg0x3e07 = 0x80, u8Reg0x3e06 = 0x00;
	u32 aCoarseGain = 0;
	u32 aFineGain = 0;
	u32 dCoarseGain = 0;
	u32 dFineGain = 0;
	u32 again = 0;
	u32 dgain = 0;

	if (val <= 2764) {
		again = val;
		dgain = 128;
	} else {
		again = 2764;
		dgain = val * 128 / again;
	}

	//again
	if (again <= 174) {
		//a_gain < 2.72x
		for (aCoarseGain = 1; aCoarseGain <= 2; aCoarseGain = aCoarseGain * 2) {
			//1,2,4,8,16
			if (again < (64 * 2 * aCoarseGain))
				break;
		}

		aFineGain = again / aCoarseGain;
	} else {
		for (aCoarseGain = 1; aCoarseGain <= 8; aCoarseGain = aCoarseGain * 2) {
			//1,2,4,8
			if (again < (64 * 2 * aCoarseGain * 272 / 100))
				break;
		}
		aFineGain = 100 * again / aCoarseGain / 272;
	}
	for ( ; aCoarseGain >= 2; aCoarseGain = aCoarseGain / 2)
		u8Reg0x3e08 = (u8Reg0x3e08 << 1) | 0x01;

	u8Reg0x3e09 = aFineGain;

	//dcg = 2.72	-->		2.72*1024=2785.28
	u8Reg0x3e08 = (again > 174) ? (u8Reg0x3e08 | 0x20) : (u8Reg0x3e08 & 0x1f);

	//------------------------------------------------------
	//dgain
	for (dCoarseGain = 1; dCoarseGain <= 16; dCoarseGain = dCoarseGain * 2) {
		//1,2,4,8,16
		if (dgain < (256 * dCoarseGain))
			break;
	}
	dFineGain = dgain / dCoarseGain;

	for ( ; dCoarseGain >= 2; dCoarseGain = dCoarseGain / 2)
		u8Reg0x3e06 = (u8Reg0x3e06 << 1) | 0x01;

	u8Reg0x3e07 = dFineGain;

	*again_reg = u8Reg0x3e08;
	*again_fine_reg = u8Reg0x3e09;
	*dgain_reg = u8Reg0x3e06;
	*dgain_fine_reg = u8Reg0x3e07;

}

static int sc2310_set_hdrae(struct sc2310 *sc2310,
			     struct preisp_hdrae_exp_s *ae)
{
	struct i2c_client *client = sc2310->client;
	u32 l_exp_time, m_exp_time, s_exp_time;
	u32 l_a_gain, m_a_gain, s_a_gain;
	u32 l_again, l_again_fine, l_dgain, l_dgain_fine;
	u32 s_again, s_again_fine, s_dgain, s_dgain_fine;
	int ret = 0;

	if (!sc2310->has_init_exp && !sc2310->streaming) {
		sc2310->init_hdrae_exp = *ae;
		sc2310->has_init_exp = true;
		dev_dbg(&client->dev, "sc2310 is not streaming, save hdr ae!\n");
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

	if (sc2310->cur_mode->hdr_mode == HDR_X2) {
		l_a_gain = m_a_gain;
		l_exp_time = m_exp_time;
	}
	sc2310_get_gain_reg(l_a_gain, &l_again, &l_again_fine, &l_dgain, &l_dgain_fine);
	sc2310_get_gain_reg(s_a_gain, &s_again, &s_again_fine, &s_dgain, &s_dgain_fine);

	l_exp_time = l_exp_time << 1;
	s_exp_time = s_exp_time << 1;

	if (l_exp_time > LONG_FRAME_MAX_EXP)
		l_exp_time = LONG_FRAME_MAX_EXP;

	if (s_exp_time > SHORT_FRAME_MAX_EXP)
		s_exp_time = SHORT_FRAME_MAX_EXP;

	ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_EXP_LONG_L,
					SC2310_REG_VALUE_08BIT,
					(l_exp_time << 4 & 0XF0));
	ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_EXP_LONG_M,
					SC2310_REG_VALUE_08BIT,
					(l_exp_time >> 4 & 0XFF));
	ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_EXP_LONG_H,
					SC2310_REG_VALUE_08BIT,
					(l_exp_time >> 12 & 0X0F));

	ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_AGAIN,
					SC2310_REG_VALUE_08BIT,
					l_again);
	ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_AGAIN_FINE,
					SC2310_REG_VALUE_08BIT,
					l_again_fine);
	ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_DGAIN,
					SC2310_REG_VALUE_08BIT,
					l_dgain);
	ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_DGAIN_FINE,
					SC2310_REG_VALUE_08BIT,
					l_dgain_fine);

	ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_EXP_SF_L,
					SC2310_REG_VALUE_08BIT,
					(s_exp_time << 4 & 0XF0));
	ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_EXP_SF_H,
					SC2310_REG_VALUE_08BIT,
					(s_exp_time >> 4 & 0XFF));

	ret |= sc2310_write_reg(sc2310->client,
					SC2310_SF_REG_AGAIN,
					SC2310_REG_VALUE_08BIT,
					s_again);
	ret |= sc2310_write_reg(sc2310->client,
					SC2310_SF_REG_AGAIN_FINE,
					SC2310_REG_VALUE_08BIT,
					s_again_fine);
	ret |= sc2310_write_reg(sc2310->client,
					SC2310_SF_REG_DGAIN,
					SC2310_REG_VALUE_08BIT,
					s_dgain);
	ret |= sc2310_write_reg(sc2310->client,
					SC2310_SF_REG_DGAIN_FINE,
					SC2310_REG_VALUE_08BIT,
					s_dgain_fine);
	if (ret)
		return ret;
	return 0;
}

static int sc2310_get_channel_info(struct sc2310 *sc2310, struct rkmodule_channel_info *ch_info)
{
	if (ch_info->index < PAD0 || ch_info->index >= PAD_MAX)
		return -EINVAL;
	ch_info->vc = sc2310->cur_mode->vc[ch_info->index];
	ch_info->width = sc2310->cur_mode->width;
	ch_info->height = sc2310->cur_mode->height;
	ch_info->bus_fmt = sc2310->cur_mode->bus_fmt;
	return 0;
}

static long sc2310_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct sc2310 *sc2310 = to_sc2310(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	const struct sc2310_mode *mode;
	struct rkmodule_channel_info *ch_info;
	long ret = 0;
	u64 pixel_rate = 0;
	u32 i, h, w, stream;

	switch (cmd) {
	case PREISP_CMD_SET_HDRAE_EXP:
		ret = sc2310_set_hdrae(sc2310, arg);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		if (sc2310->streaming) {
			ret = sc2310_write_array(sc2310->client, sc2310->cur_mode->reg_list);
			if (ret)
				return ret;
		}
		w = sc2310->cur_mode->width;
		h = sc2310->cur_mode->height;
		for (i = 0; i < sc2310->cfg_num; i++) {
			if (w == supported_modes[i].width &&
			h == supported_modes[i].height &&
			supported_modes[i].hdr_mode == hdr_cfg->hdr_mode) {
				sc2310_change_mode(sc2310, &supported_modes[i]);
				break;
			}
		}

		if (i == sc2310->cfg_num) {
			dev_err(&sc2310->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr_cfg->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			mode = sc2310->cur_mode;
			w = mode->hts_def - mode->width;
			h = mode->vts_def - mode->height;
			__v4l2_ctrl_modify_range(sc2310->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(sc2310->vblank, h,
				SC2310_VTS_MAX - mode->height,
				1, h);
			__v4l2_ctrl_s_ctrl(sc2310->link_freq, mode->mipi_freq_idx);
			pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] /
				mode->bpp * 2 * SC2310_LANES;
			__v4l2_ctrl_s_ctrl_int64(sc2310->pixel_rate,
						 pixel_rate);
			sc2310->cur_fps = mode->max_fps;
			sc2310->cur_vts = mode->vts_def;
			dev_info(&sc2310->client->dev,
				"sensor mode: %d\n", mode->hdr_mode);
		}
		break;
	case RKMODULE_GET_MODULE_INFO:
		sc2310_get_module_inf(sc2310, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		hdr_cfg->hdr_mode = sc2310->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = sc2310_write_reg(sc2310->client, SC2310_REG_CTRL_MODE,
				SC2310_REG_VALUE_08BIT, SC2310_MODE_STREAMING);
		else
			ret = sc2310_write_reg(sc2310->client, SC2310_REG_CTRL_MODE,
				SC2310_REG_VALUE_08BIT, SC2310_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = (struct rkmodule_channel_info *)arg;
		ret = sc2310_get_channel_info(sc2310, ch_info);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long sc2310_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	struct rkmodule_channel_info *ch_info;
	long ret = 0;
	u32 cg = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc2310_ioctl(sd, cmd, inf);
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
		ret = sc2310_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc2310_ioctl(sd, cmd, hdr);
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
		ret = sc2310_ioctl(sd, cmd, hdr);
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
		ret = sc2310_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		if (copy_from_user(&cg, up, sizeof(cg)))
			return -EFAULT;
		ret = sc2310_ioctl(sd, cmd, &cg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		if (copy_from_user(&stream, up, sizeof(u32)))
			return -EFAULT;
		ret = sc2310_ioctl(sd, cmd, &stream);
		break;
	case RKMODULE_GET_CHANNEL_INFO:
		ch_info = kzalloc(sizeof(*ch_info), GFP_KERNEL);
		if (!ch_info) {
			ret = -ENOMEM;
			return ret;
		}

		ret = sc2310_ioctl(sd, cmd, ch_info);
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

static int __sc2310_start_stream(struct sc2310 *sc2310)
{
	int ret;

	ret = sc2310_write_array(sc2310->client, sc2310->cur_mode->reg_list);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_handler_setup(&sc2310->ctrl_handler);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	if (sc2310->has_init_exp && sc2310->cur_mode->hdr_mode != NO_HDR) {
		ret = sc2310_ioctl(&sc2310->subdev, PREISP_CMD_SET_HDRAE_EXP,
			&sc2310->init_hdrae_exp);
		if (ret) {
			dev_err(&sc2310->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}

	return sc2310_write_reg(sc2310->client, SC2310_REG_CTRL_MODE,
		SC2310_REG_VALUE_08BIT, SC2310_MODE_STREAMING);
}

static int __sc2310_stop_stream(struct sc2310 *sc2310)
{
	sc2310->has_init_exp = false;
	return sc2310_write_reg(sc2310->client, SC2310_REG_CTRL_MODE,
		SC2310_REG_VALUE_08BIT, SC2310_MODE_SW_STANDBY);
}

static int sc2310_s_stream(struct v4l2_subdev *sd, int on)
{
	struct sc2310 *sc2310 = to_sc2310(sd);
	struct i2c_client *client = sc2310->client;
	int ret = 0;

	mutex_lock(&sc2310->mutex);
	on = !!on;
	if (on == sc2310->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __sc2310_start_stream(sc2310);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__sc2310_stop_stream(sc2310);
		pm_runtime_put(&client->dev);
	}

	sc2310->streaming = on;

unlock_and_return:
	mutex_unlock(&sc2310->mutex);

	return ret;
}

static int sc2310_s_power(struct v4l2_subdev *sd, int on)
{
	struct sc2310 *sc2310 = to_sc2310(sd);
	struct i2c_client *client = sc2310->client;
	int ret = 0;

	mutex_lock(&sc2310->mutex);

	/* If the power state is not modified - no work to do. */
	if (sc2310->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret |= sc2310_write_reg(sc2310->client,
			SC2310_SOFTWARE_RESET_REG,
			SC2310_REG_VALUE_08BIT,
			0x01);
		usleep_range(100, 200);
		ret |= sc2310_write_reg(sc2310->client,
			0x303f,
			SC2310_REG_VALUE_08BIT,
			0x01);

		sc2310->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		sc2310->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&sc2310->mutex);

	return ret;
}

static int __sc2310_power_on(struct sc2310 *sc2310)
{
	int ret;
	struct device *dev = &sc2310->client->dev;

	if (!IS_ERR_OR_NULL(sc2310->pins_default)) {
		ret = pinctrl_select_state(sc2310->pinctrl,
					   sc2310->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(sc2310->xvclk, SC2310_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(sc2310->xvclk) != SC2310_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(sc2310->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(sc2310->reset_gpio))
		gpiod_set_value_cansleep(sc2310->reset_gpio, 1);

	ret = regulator_bulk_enable(SC2310_NUM_SUPPLIES, sc2310->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(sc2310->reset_gpio))
		gpiod_set_value_cansleep(sc2310->reset_gpio, 0);

	usleep_range(500, 1000);
	if (!IS_ERR(sc2310->pwdn_gpio))
		gpiod_set_value_cansleep(sc2310->pwdn_gpio, 0);
	usleep_range(2000, 4000);

	return 0;

disable_clk:
	clk_disable_unprepare(sc2310->xvclk);

	return ret;
}

static void __sc2310_power_off(struct sc2310 *sc2310)
{
	int ret;
	struct device *dev = &sc2310->client->dev;

	if (!IS_ERR(sc2310->pwdn_gpio))
		gpiod_set_value_cansleep(sc2310->pwdn_gpio, 1);
	clk_disable_unprepare(sc2310->xvclk);
	if (!IS_ERR(sc2310->reset_gpio))
		gpiod_set_value_cansleep(sc2310->reset_gpio, 1);
	if (!IS_ERR_OR_NULL(sc2310->pins_sleep)) {
		ret = pinctrl_select_state(sc2310->pinctrl,
					   sc2310->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(SC2310_NUM_SUPPLIES, sc2310->supplies);
}

static int sc2310_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc2310 *sc2310 = to_sc2310(sd);

	return __sc2310_power_on(sc2310);
}

static int sc2310_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc2310 *sc2310 = to_sc2310(sd);

	__sc2310_power_off(sc2310);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int sc2310_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct sc2310 *sc2310 = to_sc2310(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct sc2310_mode *def_mode = &supported_modes[0];

	mutex_lock(&sc2310->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&sc2310->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int sc2310_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct sc2310 *sc2310 = to_sc2310(sd);

	if (fie->index >= sc2310->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops sc2310_pm_ops = {
	SET_RUNTIME_PM_OPS(sc2310_runtime_suspend,
			   sc2310_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops sc2310_internal_ops = {
	.open = sc2310_open,
};
#endif

static const struct v4l2_subdev_core_ops sc2310_core_ops = {
	.s_power = sc2310_s_power,
	.ioctl = sc2310_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = sc2310_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops sc2310_video_ops = {
	.s_stream = sc2310_s_stream,
	.g_frame_interval = sc2310_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops sc2310_pad_ops = {
	.enum_mbus_code = sc2310_enum_mbus_code,
	.enum_frame_size = sc2310_enum_frame_sizes,
	.enum_frame_interval = sc2310_enum_frame_interval,
	.get_fmt = sc2310_get_fmt,
	.set_fmt = sc2310_set_fmt,
	.get_mbus_config = sc2310_g_mbus_config,
};

static const struct v4l2_subdev_ops sc2310_subdev_ops = {
	.core	= &sc2310_core_ops,   /* v4l2_subdev_core_ops sc2310_core_ops */
	.video	= &sc2310_video_ops,  /* */
	.pad	= &sc2310_pad_ops,    /* */
};

static void sc2310_modify_fps_info(struct sc2310 *sc2310)
{
	const struct sc2310_mode *mode = sc2310->cur_mode;

	sc2310->cur_fps.denominator = mode->max_fps.denominator * mode->vts_def /
				      sc2310->cur_vts;
}

static int sc2310_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc2310 *sc2310 = container_of(ctrl->handler,
					     struct sc2310, ctrl_handler);
	struct i2c_client *client = sc2310->client;
	s64 max;
	u32 again, again_fine, dgain, dgain_fine;
	int ret = 0;
	u32 val;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = sc2310->cur_mode->height + ctrl->val - 3;
		__v4l2_ctrl_modify_range(sc2310->exposure,
					 sc2310->exposure->minimum, max,
					 sc2310->exposure->step,
					 sc2310->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		if (sc2310->cur_mode->hdr_mode != NO_HDR)
			goto out_ctrl;
		val = ctrl->val << 1;
		ret = sc2310_write_reg(sc2310->client,
					SC2310_REG_EXP_LONG_L,
					SC2310_REG_VALUE_08BIT,
					(val << 4 & 0XF0));
		ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_EXP_LONG_M,
					SC2310_REG_VALUE_08BIT,
					(val >> 4 & 0XFF));
		ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_EXP_LONG_H,
					SC2310_REG_VALUE_08BIT,
					(val >> 12 & 0X0F));
		dev_dbg(&client->dev, "set exposure 0x%x\n", val);
		break;

	case V4L2_CID_ANALOGUE_GAIN:
		if (sc2310->cur_mode->hdr_mode != NO_HDR)
			goto out_ctrl;
		sc2310_get_gain_reg(ctrl->val, &again, &again_fine, &dgain, &dgain_fine);
		dev_dbg(&client->dev, "recv:%d set again 0x%x, again_fine 0x%x, set dgain 0x%x, dgain_fine 0x%x\n",
			ctrl->val, again, again_fine, dgain, dgain_fine);

		ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_AGAIN,
					SC2310_REG_VALUE_08BIT,
					again);
		ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_AGAIN_FINE,
					SC2310_REG_VALUE_08BIT,
					again_fine);
		ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_DGAIN,
					SC2310_REG_VALUE_08BIT,
					dgain);
		ret |= sc2310_write_reg(sc2310->client,
					SC2310_REG_DGAIN_FINE,
					SC2310_REG_VALUE_08BIT,
					dgain_fine);
		break;
	case V4L2_CID_VBLANK:
		ret = sc2310_write_reg(sc2310->client, SC2310_REG_VTS,
					SC2310_REG_VALUE_16BIT,
					ctrl->val + sc2310->cur_mode->height);
		if (!ret)
			sc2310->cur_vts = ctrl->val + sc2310->cur_mode->height;
		if (sc2310->cur_vts != sc2310->cur_mode->vts_def)
			sc2310_modify_fps_info(sc2310);
		dev_dbg(&client->dev, "set vblank 0x%x\n",
			ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = sc2310_enable_test_pattern(sc2310, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = sc2310_read_reg(sc2310->client, SC2310_FLIP_REG,
				      SC2310_REG_VALUE_08BIT, &val);
		if (ret)
			break;
		if (ctrl->val)
			val |= SC2310_MIRROR_MASK;
		else
			val &= ~SC2310_MIRROR_MASK;
		ret |= sc2310_write_reg(sc2310->client, SC2310_FLIP_REG,
					SC2310_REG_VALUE_08BIT, val);
		break;
	case V4L2_CID_VFLIP:
		ret = sc2310_read_reg(sc2310->client, SC2310_FLIP_REG,
				      SC2310_REG_VALUE_08BIT, &val);
		if (ret)
			break;
		if (ctrl->val)
			val |= SC2310_FLIP_MASK;
		else
			val &= ~SC2310_FLIP_MASK;
		ret |= sc2310_write_reg(sc2310->client, SC2310_FLIP_REG,
					SC2310_REG_VALUE_08BIT, val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

out_ctrl:
	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops sc2310_ctrl_ops = {
	.s_ctrl = sc2310_set_ctrl,
};

static int sc2310_initialize_controls(struct sc2310 *sc2310)
{
	const struct sc2310_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	u64 pixel_rate = 0;

	handler = &sc2310->ctrl_handler;
	mode = sc2310->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &sc2310->mutex;

	sc2310->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
			V4L2_CID_LINK_FREQ,
			ARRAY_SIZE(link_freq_items) - 1, 0,
			link_freq_items);
	__v4l2_ctrl_s_ctrl(sc2310->link_freq, mode->mipi_freq_idx);

	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] / mode->bpp * 2 * SC2310_LANES;
	sc2310->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
		V4L2_CID_PIXEL_RATE, 0, SC2310_MAX_PIXEL_RATE,
		1, pixel_rate);

	h_blank = mode->hts_def - mode->width;
	sc2310->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (sc2310->hblank)
		sc2310->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	sc2310->vblank = v4l2_ctrl_new_std(handler, &sc2310_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				SC2310_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 3;
	sc2310->exposure = v4l2_ctrl_new_std(handler, &sc2310_ctrl_ops,
				V4L2_CID_EXPOSURE, SC2310_EXPOSURE_MIN,
				exposure_max, SC2310_EXPOSURE_STEP,
				mode->exp_def);

	sc2310->anal_gain = v4l2_ctrl_new_std(handler, &sc2310_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, SC2310_GAIN_MIN,
				SC2310_GAIN_MAX, SC2310_GAIN_STEP,
				SC2310_GAIN_DEFAULT);

	sc2310->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&sc2310_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(sc2310_test_pattern_menu) - 1,
				0, 0, sc2310_test_pattern_menu);

	v4l2_ctrl_new_std(handler, &sc2310_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &sc2310_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&sc2310->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	sc2310->subdev.ctrl_handler = handler;
	sc2310->has_init_exp = false;
	sc2310->cur_fps = mode->max_fps;
	sc2310->cur_vts = mode->vts_def;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int sc2310_check_sensor_id(struct sc2310 *sc2310,
				  struct i2c_client *client)
{
	struct device *dev = &sc2310->client->dev;
	u32 id = 0;
	int ret;

	ret = sc2310_read_reg(client, SC2310_REG_CHIP_ID,
		SC2310_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%04x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected SC%04x sensor\n", CHIP_ID);

	return 0;
}

static int sc2310_configure_regulators(struct sc2310 *sc2310)
{
	unsigned int i;

	for (i = 0; i < SC2310_NUM_SUPPLIES; i++)
		sc2310->supplies[i].supply = sc2310_supply_names[i];

	return devm_regulator_bulk_get(&sc2310->client->dev,
				       SC2310_NUM_SUPPLIES,
				       sc2310->supplies);
}

static int sc2310_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct sc2310 *sc2310;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	sc2310 = devm_kzalloc(dev, sizeof(*sc2310), GFP_KERNEL);
	if (!sc2310)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &sc2310->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &sc2310->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &sc2310->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &sc2310->len_name);
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

	sc2310->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < sc2310->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			sc2310->cur_mode = &supported_modes[i];
			break;
		}
	}
	sc2310->client = client;

	sc2310->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(sc2310->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	sc2310->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sc2310->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	sc2310->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(sc2310->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	sc2310->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(sc2310->pinctrl)) {
		sc2310->pins_default =
			pinctrl_lookup_state(sc2310->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sc2310->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		sc2310->pins_sleep =
			pinctrl_lookup_state(sc2310->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(sc2310->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = sc2310_configure_regulators(sc2310);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&sc2310->mutex);

	sd = &sc2310->subdev;
	v4l2_i2c_subdev_init(sd, client, &sc2310_subdev_ops);
	ret = sc2310_initialize_controls(sc2310);
	if (ret)
		goto err_destroy_mutex;

	ret = __sc2310_power_on(sc2310);
	if (ret)
		goto err_free_handler;

	ret = sc2310_check_sensor_id(sc2310, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &sc2310_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	sc2310->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &sc2310->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(sc2310->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 sc2310->module_index, facing,
		 SC2310_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);
#ifdef USED_SYS_DEBUG
	add_sysfs_interfaces(dev);
#endif
	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__sc2310_power_off(sc2310);
err_free_handler:
	v4l2_ctrl_handler_free(&sc2310->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&sc2310->mutex);

	return ret;
}

static int sc2310_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct sc2310 *sc2310 = to_sc2310(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&sc2310->ctrl_handler);
	mutex_destroy(&sc2310->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__sc2310_power_off(sc2310);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id sc2310_of_match[] = {
	{ .compatible = "smartsens,sc2310" },
	{ },
};
MODULE_DEVICE_TABLE(of, sc2310_of_match);
#endif

static const struct i2c_device_id sc2310_match_id[] = {
	{ "smartsens,sc2310", 0 },
	{ },
};

static struct i2c_driver sc2310_i2c_driver = {
	.driver = {
		.name = SC2310_NAME,
		.pm = &sc2310_pm_ops,
		.of_match_table = of_match_ptr(sc2310_of_match),
	},
	.probe		= &sc2310_probe,
	.remove		= &sc2310_remove,
	.id_table	= sc2310_match_id,
};

#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT
module_i2c_driver(sc2310_i2c_driver);
#else
static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&sc2310_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&sc2310_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);
#endif

MODULE_DESCRIPTION("Smartsens sc2310 sensor driver");
MODULE_LICENSE("GPL v2");
