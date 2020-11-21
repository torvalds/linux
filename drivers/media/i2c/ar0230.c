// SPDX-License-Identifier: GPL-2.0
/*
 * ar0230 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 * V0.0X01.0X01 add enum_frame_interval function.
 * V0.0X01.0X02 add quick stream on/off
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x02)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

/* 74.25Mhz */
#define AR0230_PIXEL_RATE			(74250000)
#define AR0230_XVCLK_FREQ			24000000

#define CHIP_ID						0x3020
#define AR0230_REG_CHIP_ID			0x31fc

#define AR0230_REG_CTRL_MODE		0x301A
#define AR0230_MODE_SW_STANDBY		0x10D8
#define AR0230_MODE_STREAMING		0x10DC

#define AR0230_REG_EXPOSURE			0x3012
#define AR0230_EXPOSURE_MIN			0
#define AR0230_EXPOSURE_STEP		1
#define AR0230_VTS_MAX				0x044A

#define AR0230_REG_ANALOG_GAIN		0x3060
#define ANALOG_GAIN_MIN				0x0
#define ANALOG_GAIN_MAX				0xFB7
#define ANALOG_GAIN_STEP			1
#define ANALOG_GAIN_DEFAULT			0xC0

#define AR0230_REG_VTS				0x300a

#define AR0230_REG_ORIENTATION		0x3040
#define AR0230_ORIENTATION_H		bit(14)
#define AR0230_ORIENTATION_V		bit(15)

#define REG_NULL					0xFFFF
#define REG_DELAY					0xFFFE

#define AR0230_REG_VALUE_08BIT		1
#define AR0230_REG_VALUE_16BIT		2
#define AR0230_REG_VALUE_24BIT		3

#define USE_HDR_MODE

/* h_offs 35 v_offs 14 */
#define PIX_FORMAT MEDIA_BUS_FMT_SGRBG12_1X12

#define AR0230_NAME			"ar0230"

struct cam_regulator {
	char name[32];
	int val;
};

static const struct cam_regulator ar0230_regulator[] = {
	{"avdd", 2800000},	/* Analog power */
	{"dovdd", 1800000},	/* Digital I/O power */
	{"dvdd", 1800000},	/* Digital core power */
};

#define AR0230_NUM_SUPPLIES ARRAY_SIZE(ar0230_regulator)

struct regval {
	u16 addr;
	u16 val;
};

struct ar0230_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct ar0230 {
	struct i2c_client	*client;
	struct clk			*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[AR0230_NUM_SUPPLIES];

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
	bool				streaming;
	bool				power_on;
	const struct ar0230_mode *cur_mode;
	u32					module_index;
	const char			*module_facing;
	const char			*module_name;
	const char			*len_name;
};

#define to_ar0230(sd) container_of(sd, struct ar0230, subdev)

/*
 * Xclk 24Mhz
 * Pclk 74.25Mhz
 * linelength 0x469
 * framelength 0x44a
 * grabwindow_width 1920
 * grabwindow_height 1080
 * max_framerate 30fps
 * dvp bt601 12bit
 */
static const struct regval ar0230_regs[] = {
#ifdef USE_HDR_MODE
	{0x301A, 0x0001},
	{REG_DELAY, 2000},
	{0x301A, 0x10D8},
	{REG_DELAY, 2000},
	{0x3088, 0x8000},
	{0x3086, 0x4558},
	{0x3086, 0x729B},
	{0x3086, 0x4A31},
	{0x3086, 0x4342},
	{0x3086, 0x8E03},
	{0x3086, 0x2A14},
	{0x3086, 0x4578},
	{0x3086, 0x7B3D},
	{0x3086, 0xFF3D},
	{0x3086, 0xFF3D},
	{0x3086, 0xEA2A},
	{0x3086, 0x043D},
	{0x3086, 0x102A},
	{0x3086, 0x052A},
	{0x3086, 0x1535},
	{0x3086, 0x2A05},
	{0x3086, 0x3D10},
	{0x3086, 0x4558},
	{0x3086, 0x2A04},
	{0x3086, 0x2A14},
	{0x3086, 0x3DFF},
	{0x3086, 0x3DFF},
	{0x3086, 0x3DEA},
	{0x3086, 0x2A04},
	{0x3086, 0x622A},
	{0x3086, 0x288E},
	{0x3086, 0x0036},
	{0x3086, 0x2A08},
	{0x3086, 0x3D64},
	{0x3086, 0x7A3D},
	{0x3086, 0x0444},
	{0x3086, 0x2C4B},
	{0x3086, 0x8F00},
	{0x3086, 0x430C},
	{0x3086, 0x2D63},
	{0x3086, 0x4316},
	{0x3086, 0x8E03},
	{0x3086, 0x2AFC},
	{0x3086, 0x5C1D},
	{0x3086, 0x5754},
	{0x3086, 0x495F},
	{0x3086, 0x5305},
	{0x3086, 0x5307},
	{0x3086, 0x4D2B},
	{0x3086, 0xF810},
	{0x3086, 0x164C},
	{0x3086, 0x0855},
	{0x3086, 0x562B},
	{0x3086, 0xB82B},
	{0x3086, 0x984E},
	{0x3086, 0x1129},
	{0x3086, 0x0429},
	{0x3086, 0x8429},
	{0x3086, 0x9460},
	{0x3086, 0x5C19},
	{0x3086, 0x5C1B},
	{0x3086, 0x4548},
	{0x3086, 0x4508},
	{0x3086, 0x4588},
	{0x3086, 0x29B6},
	{0x3086, 0x8E01},
	{0x3086, 0x2AF8},
	{0x3086, 0x3E02},
	{0x3086, 0x2AFA},
	{0x3086, 0x3F09},
	{0x3086, 0x5C1B},
	{0x3086, 0x29B2},
	{0x3086, 0x3F0C},
	{0x3086, 0x3E02},
	{0x3086, 0x3E13},
	{0x3086, 0x5C13},
	{0x3086, 0x3F11},
	{0x3086, 0x3E0B},
	{0x3086, 0x5F2B},
	{0x3086, 0x902A},
	{0x3086, 0xF22B},
	{0x3086, 0x803E},
	{0x3086, 0x043F},
	{0x3086, 0x0660},
	{0x3086, 0x29A2},
	{0x3086, 0x29A3},
	{0x3086, 0x5F4D},
	{0x3086, 0x192A},
	{0x3086, 0xFA29},
	{0x3086, 0x8345},
	{0x3086, 0xA83E},
	{0x3086, 0x072A},
	{0x3086, 0xFB3E},
	{0x3086, 0x2945},
	{0x3086, 0x8821},
	{0x3086, 0x3E08},
	{0x3086, 0x2AFA},
	{0x3086, 0x5D29},
	{0x3086, 0x9288},
	{0x3086, 0x102B},
	{0x3086, 0x048B},
	{0x3086, 0x1685},
	{0x3086, 0x8D48},
	{0x3086, 0x4D4E},
	{0x3086, 0x2B80},
	{0x3086, 0x4C0B},
	{0x3086, 0x603F},
	{0x3086, 0x282A},
	{0x3086, 0xF23F},
	{0x3086, 0x0F29},
	{0x3086, 0x8229},
	{0x3086, 0x8329},
	{0x3086, 0x435C},
	{0x3086, 0x155F},
	{0x3086, 0x4D19},
	{0x3086, 0x2AFA},
	{0x3086, 0x4558},
	{0x3086, 0x8E00},
	{0x3086, 0x2A98},
	{0x3086, 0x3F06},
	{0x3086, 0x1244},
	{0x3086, 0x4A04},
	{0x3086, 0x4316},
	{0x3086, 0x0543},
	{0x3086, 0x1658},
	{0x3086, 0x4316},
	{0x3086, 0x5A43},
	{0x3086, 0x1606},
	{0x3086, 0x4316},
	{0x3086, 0x0743},
	{0x3086, 0x168E},
	{0x3086, 0x032A},
	{0x3086, 0x9C45},
	{0x3086, 0x787B},
	{0x3086, 0x3F07},
	{0x3086, 0x2A9D},
	{0x3086, 0x3E2E},
	{0x3086, 0x4558},
	{0x3086, 0x253E},
	{0x3086, 0x068E},
	{0x3086, 0x012A},
	{0x3086, 0x988E},
	{0x3086, 0x0012},
	{0x3086, 0x444B},
	{0x3086, 0x0343},
	{0x3086, 0x2D46},
	{0x3086, 0x4316},
	{0x3086, 0xA343},
	{0x3086, 0x165D},
	{0x3086, 0x0D29},
	{0x3086, 0x4488},
	{0x3086, 0x102B},
	{0x3086, 0x0453},
	{0x3086, 0x0D8B},
	{0x3086, 0x1685},
	{0x3086, 0x448E},
	{0x3086, 0x032A},
	{0x3086, 0xFC5C},
	{0x3086, 0x1D8D},
	{0x3086, 0x6057},
	{0x3086, 0x5449},
	{0x3086, 0x5F53},
	{0x3086, 0x0553},
	{0x3086, 0x074D},
	{0x3086, 0x2BF8},
	{0x3086, 0x1016},
	{0x3086, 0x4C08},
	{0x3086, 0x5556},
	{0x3086, 0x2BB8},
	{0x3086, 0x2B98},
	{0x3086, 0x4E11},
	{0x3086, 0x2904},
	{0x3086, 0x2984},
	{0x3086, 0x2994},
	{0x3086, 0x605C},
	{0x3086, 0x195C},
	{0x3086, 0x1B45},
	{0x3086, 0x4845},
	{0x3086, 0x0845},
	{0x3086, 0x8829},
	{0x3086, 0xB68E},
	{0x3086, 0x012A},
	{0x3086, 0xF83E},
	{0x3086, 0x022A},
	{0x3086, 0xFA3F},
	{0x3086, 0x095C},
	{0x3086, 0x1B29},
	{0x3086, 0xB23F},
	{0x3086, 0x0C3E},
	{0x3086, 0x023E},
	{0x3086, 0x135C},
	{0x3086, 0x133F},
	{0x3086, 0x113E},
	{0x3086, 0x0B5F},
	{0x3086, 0x2B90},
	{0x3086, 0x2AF2},
	{0x3086, 0x2B80},
	{0x3086, 0x3E04},
	{0x3086, 0x3F06},
	{0x3086, 0x6029},
	{0x3086, 0xA229},
	{0x3086, 0xA35F},
	{0x3086, 0x4D1C},
	{0x3086, 0x2AFA},
	{0x3086, 0x2983},
	{0x3086, 0x45A8},
	{0x3086, 0x3E07},
	{0x3086, 0x2AFB},
	{0x3086, 0x3E29},
	{0x3086, 0x4588},
	{0x3086, 0x243E},
	{0x3086, 0x082A},
	{0x3086, 0xFA5D},
	{0x3086, 0x2992},
	{0x3086, 0x8810},
	{0x3086, 0x2B04},
	{0x3086, 0x8B16},
	{0x3086, 0x868D},
	{0x3086, 0x484D},
	{0x3086, 0x4E2B},
	{0x3086, 0x804C},
	{0x3086, 0x0B60},
	{0x3086, 0x3F28},
	{0x3086, 0x2AF2},
	{0x3086, 0x3F0F},
	{0x3086, 0x2982},
	{0x3086, 0x2983},
	{0x3086, 0x2943},
	{0x3086, 0x5C15},
	{0x3086, 0x5F4D},
	{0x3086, 0x1C2A},
	{0x3086, 0xFA45},
	{0x3086, 0x588E},
	{0x3086, 0x002A},
	{0x3086, 0x983F},
	{0x3086, 0x064A},
	{0x3086, 0x739D},
	{0x3086, 0x0A43},
	{0x3086, 0x160B},
	{0x3086, 0x4316},
	{0x3086, 0x8E03},
	{0x3086, 0x2A9C},
	{0x3086, 0x4578},
	{0x3086, 0x3F07},
	{0x3086, 0x2A9D},
	{0x3086, 0x3E12},
	{0x3086, 0x4558},
	{0x3086, 0x3F04},
	{0x3086, 0x8E01},
	{0x3086, 0x2A98},
	{0x3086, 0x8E00},
	{0x3086, 0x9176},
	{0x3086, 0x9C77},
	{0x3086, 0x9C46},
	{0x3086, 0x4416},
	{0x3086, 0x1690},
	{0x3086, 0x7A12},
	{0x3086, 0x444B},
	{0x3086, 0x4A00},
	{0x3086, 0x4316},
	{0x3086, 0x6343},
	{0x3086, 0x1608},
	{0x3086, 0x4316},
	{0x3086, 0x5043},
	{0x3086, 0x1665},
	{0x3086, 0x4316},
	{0x3086, 0x6643},
	{0x3086, 0x168E},
	{0x3086, 0x032A},
	{0x3086, 0x9C45},
	{0x3086, 0x783F},
	{0x3086, 0x072A},
	{0x3086, 0x9D5D},
	{0x3086, 0x0C29},
	{0x3086, 0x4488},
	{0x3086, 0x102B},
	{0x3086, 0x0453},
	{0x3086, 0x0D8B},
	{0x3086, 0x1686},
	{0x3086, 0x3E1F},
	{0x3086, 0x4558},
	{0x3086, 0x283E},
	{0x3086, 0x068E},
	{0x3086, 0x012A},
	{0x3086, 0x988E},
	{0x3086, 0x008D},
	{0x3086, 0x6012},
	{0x3086, 0x444B},
	{0x3086, 0x2C2C},
	{0x3086, 0x2C2C},
	{0x2436, 0x000E},
	{0x320C, 0x0180},
	{0x320E, 0x0300},
	{0x3210, 0x0500},
	{0x3204, 0x0B6D},
	{0x30FE, 0x0080},
	{0x3ED8, 0x7B99},
	{0x3EDC, 0x9BA8},
	{0x3EDA, 0x9B9B},
	{0x3092, 0x006F},
	{0x3EEC, 0x1C04},
	{0x30BA, 0x779C},
	{0x3EF6, 0xA70F},
	{0x3044, 0x0410},
	{0x3ED0, 0xFF44},
	{0x3ED4, 0x031F},
	{0x30FE, 0x0080},
	{0x3EE2, 0x8866},
	{0x3EE4, 0x6623},
	{0x3EE6, 0x2263},
	{0x30E0, 0x4283},
	{0x30F0, 0x1283},
	{0x30B0, 0x0118},
	{0x31AC, 0x100C},
	{0x3040, 0x0000},
	{0x31AE, 0x0301},
	{0x3082, 0x0008},
	{0x31E0, 0x0200},
	{0x2420, 0x0000},
	{0x2440, 0x0004},
	{0x2442, 0x0080},
	{0x301E, 0x0000},
	{0x2450, 0x0000},
	{0x320A, 0x0080},
	{0x31D0, 0x0000},
	{0x2400, 0x0002},
	{0x2410, 0x0005},
	{0x2412, 0x002D},
	{0x2444, 0xF400},
	{0x2446, 0x0001},
	{0x2438, 0x0010},
	{0x243A, 0x0012},
	{0x243C, 0xFFFF},
	{0x243E, 0x0100},
	{0x3206, 0x0B08},
	{0x3208, 0x1E13},
	{0x3202, 0x0080},
	{0x3200, 0x0002},
	{0x3190, 0x0000},
	{0x318A, 0x0E74},
	{0x318C, 0xC000},
	{0x3192, 0x0400},
	{0x3198, 0x183C},
	{0x3060, 0x000B},
	{0x3096, 0x0480},
	{0x3098, 0x0480},
	{0x3206, 0x0B08},
	{0x3208, 0x1E13},
	{0x3202, 0x0080},
	{0x3200, 0x0002},
	{0x3100, 0x0000},
	{0x30BA, 0x779C},
	{0x318E, 0x0200},
	{0x3064, 0x1982},
	{0x3064, 0x1802},
	{0x302A, 0x0008},
	{0x302C, 0x0001},
	{0x302E, 0x0008},
	{0x3030, 0x00C6},
	{0x3036, 0x0006},
	{0x3038, 0x0001},
	{0x31AE, 0x0301},
	{0x30BA, 0x769C},
	{0x3002, 0x0004},
	{0x3004, 0x000c},
	{0x3006, 0x043b},
	{0x3008, 0x078b},
	{0x300A, 0x044A},
	{0x300C, 0x0469},
	{0x3012, 0x0148},
	{0x3180, 0x0008},
	{0x3062, 0x2333},
	{0x30B0, 0x0118},
	{0x30A2, 0x0001},
	{0x30A6, 0x0001},
	{0x3082, 0x0008},
	{0x3040, 0x0000},
	{0x318E, 0x0000},
#else
	{0x301A, 0x0001},
	{REG_DELAY, 20000},
	{0x301A, 0x10D8},
	{REG_DELAY, 20000},
	{0x3088, 0x8242},
	{0x3086, 0x4558},
	{0x3086, 0x729B},
	{0x3086, 0x4A31},
	{0x3086, 0x4342},
	{0x3086, 0x8E03},
	{0x3086, 0x2A14},
	{0x3086, 0x4578},
	{0x3086, 0x7B3D},
	{0x3086, 0xFF3D},
	{0x3086, 0xFF3D},
	{0x3086, 0xEA2A},
	{0x3086, 0x043D},
	{0x3086, 0x102A},
	{0x3086, 0x052A},
	{0x3086, 0x1535},
	{0x3086, 0x2A05},
	{0x3086, 0x3D10},
	{0x3086, 0x4558},
	{0x3086, 0x2A04},
	{0x3086, 0x2A14},
	{0x3086, 0x3DFF},
	{0x3086, 0x3DFF},
	{0x3086, 0x3DEA},
	{0x3086, 0x2A04},
	{0x3086, 0x622A},
	{0x3086, 0x288E},
	{0x3086, 0x0036},
	{0x3086, 0x2A08},
	{0x3086, 0x3D64},
	{0x3086, 0x7A3D},
	{0x3086, 0x0444},
	{0x3086, 0x2C4B},
	{0x3086, 0x8F03},
	{0x3086, 0x430D},
	{0x3086, 0x2D46},
	{0x3086, 0x4316},
	{0x3086, 0x5F16},
	{0x3086, 0x530D},
	{0x3086, 0x1660},
	{0x3086, 0x3E4C},
	{0x3086, 0x2904},
	{0x3086, 0x2984},
	{0x3086, 0x8E03},
	{0x3086, 0x2AFC},
	{0x3086, 0x5C1D},
	{0x3086, 0x5754},
	{0x3086, 0x495F},
	{0x3086, 0x5305},
	{0x3086, 0x5307},
	{0x3086, 0x4D2B},
	{0x3086, 0xF810},
	{0x3086, 0x164C},
	{0x3086, 0x0955},
	{0x3086, 0x562B},
	{0x3086, 0xB82B},
	{0x3086, 0x984E},
	{0x3086, 0x1129},
	{0x3086, 0x9460},
	{0x3086, 0x5C19},
	{0x3086, 0x5C1B},
	{0x3086, 0x4548},
	{0x3086, 0x4508},
	{0x3086, 0x4588},
	{0x3086, 0x29B6},
	{0x3086, 0x8E01},
	{0x3086, 0x2AF8},
	{0x3086, 0x1702},
	{0x3086, 0x2AFA},
	{0x3086, 0x1709},
	{0x3086, 0x5C1B},
	{0x3086, 0x29B2},
	{0x3086, 0x170C},
	{0x3086, 0x1703},
	{0x3086, 0x1715},
	{0x3086, 0x5C13},
	{0x3086, 0x1711},
	{0x3086, 0x170F},
	{0x3086, 0x5F2B},
	{0x3086, 0x902A},
	{0x3086, 0xF22B},
	{0x3086, 0x8017},
	{0x3086, 0x0617},
	{0x3086, 0x0660},
	{0x3086, 0x29A2},
	{0x3086, 0x29A3},
	{0x3086, 0x5F4D},
	{0x3086, 0x1C2A},
	{0x3086, 0xFA29},
	{0x3086, 0x8345},
	{0x3086, 0xA817},
	{0x3086, 0x072A},
	{0x3086, 0xFB17},
	{0x3086, 0x2945},
	{0x3086, 0x8824},
	{0x3086, 0x1708},
	{0x3086, 0x2AFA},
	{0x3086, 0x5D29},
	{0x3086, 0x9288},
	{0x3086, 0x102B},
	{0x3086, 0x048B},
	{0x3086, 0x1686},
	{0x3086, 0x8D48},
	{0x3086, 0x4D4E},
	{0x3086, 0x2B80},
	{0x3086, 0x4C0B},
	{0x3086, 0x6017},
	{0x3086, 0x302A},
	{0x3086, 0xF217},
	{0x3086, 0x1017},
	{0x3086, 0x8F29},
	{0x3086, 0x8229},
	{0x3086, 0x8329},
	{0x3086, 0x435C},
	{0x3086, 0x155F},
	{0x3086, 0x4D1C},
	{0x3086, 0x2AFA},
	{0x3086, 0x4558},
	{0x3086, 0x8E00},
	{0x3086, 0x2A98},
	{0x3086, 0x170A},
	{0x3086, 0x4A0A},
	{0x3086, 0x4316},
	{0x3086, 0x0B43},
	{0x3086, 0x168E},
	{0x3086, 0x032A},
	{0x3086, 0x9C45},
	{0x3086, 0x7817},
	{0x3086, 0x072A},
	{0x3086, 0x9D17},
	{0x3086, 0x305D},
	{0x3086, 0x2944},
	{0x3086, 0x8810},
	{0x3086, 0x2B04},
	{0x3086, 0x530D},
	{0x3086, 0x4558},
	{0x3086, 0x1708},
	{0x3086, 0x8E01},
	{0x3086, 0x2A98},
	{0x3086, 0x8E00},
	{0x3086, 0x769C},
	{0x3086, 0x779C},
	{0x3086, 0x4644},
	{0x3086, 0x1616},
	{0x3086, 0x907A},
	{0x3086, 0x1244},
	{0x3086, 0x4B18},
	{0x3086, 0x4A04},
	{0x3086, 0x4316},
	{0x3086, 0x0643},
	{0x3086, 0x1605},
	{0x3086, 0x4316},
	{0x3086, 0x0743},
	{0x3086, 0x1658},
	{0x3086, 0x4316},
	{0x3086, 0x5A43},
	{0x3086, 0x1645},
	{0x3086, 0x588E},
	{0x3086, 0x032A},
	{0x3086, 0x9C45},
	{0x3086, 0x787B},
	{0x3086, 0x1707},
	{0x3086, 0x2A9D},
	{0x3086, 0x530D},
	{0x3086, 0x8B16},
	{0x3086, 0x8617},
	{0x3086, 0x2345},
	{0x3086, 0x5825},
	{0x3086, 0x1710},
	{0x3086, 0x8E01},
	{0x3086, 0x2A98},
	{0x3086, 0x8E00},
	{0x3086, 0x1710},
	{0x3086, 0x8D60},
	{0x3086, 0x1244},
	{0x3086, 0x4B2C},
	{0x3086, 0x2C2C},
	{0x2436, 0x000E},
	{0x320C, 0x0180},
	{0x320E, 0x0300},
	{0x3210, 0x0500},
	{0x3204, 0x0B6D},
	{0x30FE, 0x0080},
	{0x3ED8, 0x7B99},
	{0x3EDC, 0x9BA8},
	{0x3EDA, 0x9B9B},
	{0x3092, 0x006F},
	{0x3EEC, 0x1C04},
	{0x30BA, 0x779C},
	{0x3EF6, 0xA70F},
	{0x3044, 0x0410},
	{0x3ED0, 0xFF44},
	{0x3ED4, 0x031F},
	{0x30FE, 0x0080},
	{0x3EE2, 0x8866},
	{0x3EE4, 0x6623},
	{0x3EE6, 0x2263},
	{0x30E0, 0x4283},
	{0x30F0, 0x1283},
	{0x30B0, 0x0118},
	{0x31AC, 0x0C0C},
	{0x3040, 0x0000},
	{0x31AE, 0x0301},
	{0x3082, 0x0009},
	{0x30BA, 0x769C},
	{0x31E0, 0x0200},
	{0x318C, 0x0000},
	{0x3060, 0x000B},
	{0x3096, 0x0080},
	{0x3098, 0x0080},
	{0x3206, 0x0B08},
	{0x3208, 0x1E13},
	{0x3202, 0x0080},
	{0x3200, 0x0002},
	{0x3100, 0x0000},
	{0x3200, 0x0000},
	{0x31D0, 0x0000},
	{0x2400, 0x0003},
	{0x301E, 0x00A8},
	{0x2450, 0x0000},
	{0x320A, 0x0080},
	{0x3064, 0x1982},
	{0x3064, 0x1802},
	{0x302A, 0x0008},
	{0x302C, 0x0001},
	{0x302E, 0x0008},
	{0x3030, 0x00C6},
	{0x3036, 0x0006},
	{0x3038, 0x0001},
	{0x31AE, 0x0301},
	{0x30BA, 0x769C},
	{0x3002, 0x0004},
	{0x3004, 0x000C},
	{0x3006, 0x043B},
	{0x3008, 0x078B},
	{0x300A, 0x0448},
	{0x300C, 0x0469},
	{0x3012, 0x03DA},
	{0x3180, 0x0008},
	{0x3062, 0x2333},
	{0x30B0, 0x0118},
	{0x30A2, 0x0001},
	{0x30A6, 0x0001},
	{0x3082, 0x0009},
	{0x3040, 0x0000},
	{0x318E, 0x0000},
	{0x301A, 0x10D8},
#endif
	{REG_NULL, 0x00},
};

static const struct ar0230_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0100,
		.hts_def = 0x0469 * 2,
		.vts_def = 0x044a,
		.reg_list = ar0230_regs,
	}
};

static const char * const ar0230_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int ar0230_write_reg(struct i2c_client *client, u16 reg,
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
	//printk("czf reg = 0x%04x, value = 0x%04x\n", reg, val);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;
	usleep_range(10, 20);
	return 0;
}

static int ar0230_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		if (unlikely(regs[i].addr == REG_DELAY))
			usleep_range(regs[i].val, regs[i].val * 2);
		else
			ret = ar0230_write_reg(client, regs[i].addr,
					       AR0230_REG_VALUE_16BIT,
					       regs[i].val);
	}

	return ret;
}

/* Read registers up to 4 at a time */
static int ar0230_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
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

static int ar0230_get_reso_dist(const struct ar0230_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ar0230_mode *
ar0230_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = ar0230_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ar0230_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ar0230 *ar0230 = to_ar0230(sd);
	const struct ar0230_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&ar0230->mutex);

	mode = ar0230_find_best_fit(fmt);
	fmt->format.code = PIX_FORMAT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ar0230->mutex);
		return -ENOTTY;
#endif
	} else {
		ar0230->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ar0230->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ar0230->vblank, vblank_def,
					 AR0230_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&ar0230->mutex);

	return 0;
}

static int ar0230_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ar0230 *ar0230 = to_ar0230(sd);
	const struct ar0230_mode *mode = ar0230->cur_mode;

	mutex_lock(&ar0230->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ar0230->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = PIX_FORMAT;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&ar0230->mutex);

	return 0;
}

static int ar0230_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = PIX_FORMAT;

	return 0;
}

static int ar0230_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != PIX_FORMAT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int ar0230_enable_test_pattern(struct ar0230 *ar0230, u32 pattern)
{
	return 0;
}

static void ar0230_get_module_inf(struct ar0230 *ar0230,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, AR0230_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, ar0230->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, ar0230->len_name, sizeof(inf->base.lens));
}

static long ar0230_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ar0230 *ar0230 = to_ar0230(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		ar0230_get_module_inf(ar0230, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		stream = *((u32 *)arg);
		if (stream)
			ret = ar0230_write_reg(ar0230->client, AR0230_REG_CTRL_MODE,
				AR0230_REG_VALUE_16BIT, AR0230_MODE_STREAMING);
		else
			ret = ar0230_write_reg(ar0230->client, AR0230_REG_CTRL_MODE,
				AR0230_REG_VALUE_16BIT, AR0230_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ar0230_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = ar0230_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
		kfree(inf);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = ar0230_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __ar0230_start_stream(struct ar0230 *ar0230)
{
	int ret;

	ret = ar0230_write_array(ar0230->client, ar0230->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&ar0230->mutex);
	ret = v4l2_ctrl_handler_setup(&ar0230->ctrl_handler);
	mutex_lock(&ar0230->mutex);
	if (ret)
		return ret;

	return ar0230_write_reg(ar0230->client, AR0230_REG_CTRL_MODE,
				AR0230_REG_VALUE_16BIT, AR0230_MODE_STREAMING);
}

static int __ar0230_stop_stream(struct ar0230 *ar0230)
{
	return ar0230_write_reg(ar0230->client, AR0230_REG_CTRL_MODE,
				AR0230_REG_VALUE_16BIT, AR0230_MODE_SW_STANDBY);
}

static int ar0230_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ar0230 *ar0230 = to_ar0230(sd);
	struct i2c_client *client = ar0230->client;
	int ret = 0;

	mutex_lock(&ar0230->mutex);
	on = !!on;
	if (on == ar0230->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __ar0230_start_stream(ar0230);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__ar0230_stop_stream(ar0230);
		pm_runtime_put(&client->dev);
	}

	ar0230->streaming = on;
unlock_and_return:
	mutex_unlock(&ar0230->mutex);

	return ret;
}

static int ar0230_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct ar0230 *ar0230 = to_ar0230(sd);
	const struct ar0230_mode *mode = ar0230->cur_mode;

	mutex_lock(&ar0230->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&ar0230->mutex);

	return 0;
}

static int ar0230_s_power(struct v4l2_subdev *sd, int on)
{
	struct ar0230 *ar0230 = to_ar0230(sd);
	struct i2c_client *client = ar0230->client;
	int ret = 0;

	mutex_lock(&ar0230->mutex);

	/* If the power state is not modified - no work to do. */
	if (ar0230->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ar0230->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		ar0230->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&ar0230->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ar0230_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, AR0230_XVCLK_FREQ / 1000 / 1000);
}

static int __ar0230_power_on(struct ar0230 *ar0230)
{
	int ret;
	u32 i, delay_us;
	struct device *dev = &ar0230->client->dev;

	ret = clk_set_rate(ar0230->xvclk, AR0230_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (%d)\n",
			AR0230_XVCLK_FREQ);
		return ret;
	}
	if (clk_get_rate(ar0230->xvclk) != AR0230_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on %d\n",
			AR0230_XVCLK_FREQ);
	ret = clk_prepare_enable(ar0230->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(ar0230->reset_gpio))
		gpiod_set_value_cansleep(ar0230->reset_gpio, 0);

	for (i = 0; i < AR0230_NUM_SUPPLIES; i++)
		regulator_set_voltage(ar0230->supplies[i].consumer,
			ar0230_regulator[i].val,
			ar0230_regulator[i].val);

	ret = regulator_bulk_enable(AR0230_NUM_SUPPLIES, ar0230->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(ar0230->reset_gpio))
		gpiod_set_value_cansleep(ar0230->reset_gpio, 1);

	if (!IS_ERR(ar0230->pwdn_gpio))
		gpiod_set_value_cansleep(ar0230->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ar0230_cal_delay(92000);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(ar0230->xvclk);

	return ret;
}

static void __ar0230_power_off(struct ar0230 *ar0230)
{
	if (!IS_ERR(ar0230->pwdn_gpio))
		gpiod_set_value_cansleep(ar0230->pwdn_gpio, 0);
	clk_disable_unprepare(ar0230->xvclk);
	if (!IS_ERR(ar0230->reset_gpio))
		gpiod_set_value_cansleep(ar0230->reset_gpio, 0);
	regulator_bulk_disable(AR0230_NUM_SUPPLIES, ar0230->supplies);
}

static int ar0230_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0230 *ar0230 = to_ar0230(sd);

	return __ar0230_power_on(ar0230);
}

static int ar0230_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0230 *ar0230 = to_ar0230(sd);

	__ar0230_power_off(ar0230);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ar0230_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ar0230 *ar0230 = to_ar0230(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ar0230_mode *def_mode = &supported_modes[0];

	mutex_lock(&ar0230->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = PIX_FORMAT;
	try_fmt->field = V4L2_FIELD_NONE;
	mutex_unlock(&ar0230->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int ar0230_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *config)
{
	config->type = V4L2_MBUS_PARALLEL;
	config->flags = V4L2_MBUS_HSYNC_ACTIVE_HIGH |
			V4L2_MBUS_VSYNC_ACTIVE_HIGH |
			V4L2_MBUS_PCLK_SAMPLE_FALLING;
	return 0;
}

static int ar0230_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != PIX_FORMAT)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static const struct dev_pm_ops ar0230_pm_ops = {
	SET_RUNTIME_PM_OPS(ar0230_runtime_suspend,
			   ar0230_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ar0230_internal_ops = {
	.open = ar0230_open,
};
#endif

static const struct v4l2_subdev_core_ops ar0230_core_ops = {
	.s_power = ar0230_s_power,
	.ioctl = ar0230_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = ar0230_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops ar0230_video_ops = {
	.s_stream = ar0230_s_stream,
	.g_mbus_config = ar0230_g_mbus_config,
	.g_frame_interval = ar0230_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops ar0230_pad_ops = {
	.enum_mbus_code = ar0230_enum_mbus_code,
	.enum_frame_size = ar0230_enum_frame_sizes,
	.enum_frame_interval = ar0230_enum_frame_interval,
	.get_fmt = ar0230_get_fmt,
	.set_fmt = ar0230_set_fmt,
};

static const struct v4l2_subdev_ops ar0230_subdev_ops = {
	.core	= &ar0230_core_ops,
	.video	= &ar0230_video_ops,
	.pad	= &ar0230_pad_ops,
};

static int ar0230_set_gain(struct ar0230 *ar0230, int gain)
{
	int ret = 0;
	u32 again = 0;

	if (gain < 192)
		gain = 192;
	if (gain < 256) {
		again = (u32)(32 - (32 * 128 / gain));
		ret = ar0230_write_reg(ar0230->client, 0x3100,
				       AR0230_REG_VALUE_16BIT, 0);
		ret |= ar0230_write_reg(ar0230->client, AR0230_REG_ANALOG_GAIN,
				       AR0230_REG_VALUE_16BIT, again);
	} else if (gain >= 256 && gain < 345) {
		again = (u32)(32 - (64 * 128 / gain));
		again |= 0x0010;
		ret = ar0230_write_reg(ar0230->client, 0x3100,
				       AR0230_REG_VALUE_16BIT, 0);
		ret |= ar0230_write_reg(ar0230->client, AR0230_REG_ANALOG_GAIN,
				       AR0230_REG_VALUE_16BIT, again);
	} else if (gain >= 345 && gain < 691) {
		again = (u32)(32 - (32 * 345 / gain));
		ret = ar0230_write_reg(ar0230->client, 0x3100,
				       AR0230_REG_VALUE_16BIT, 4);
		ret |= ar0230_write_reg(ar0230->client, AR0230_REG_ANALOG_GAIN,
				       AR0230_REG_VALUE_16BIT, again);
	} else if (gain >= 691 && gain < 1382) {
		again = (u32)(32 - (64 * 345 / gain));
		again |= 0x0010;
		ret = ar0230_write_reg(ar0230->client, 0x3100,
				       AR0230_REG_VALUE_16BIT, 4);
		ret |= ar0230_write_reg(ar0230->client, AR0230_REG_ANALOG_GAIN,
				       AR0230_REG_VALUE_16BIT, again);
	} else if (gain >= 1382 && gain < 2764) {
		again = (u32)(32 - (128 * 345 / gain));
		again |= 0x0020;
		ret = ar0230_write_reg(ar0230->client, 0x3100,
				       AR0230_REG_VALUE_16BIT, 4);
		ret |= ar0230_write_reg(ar0230->client, AR0230_REG_ANALOG_GAIN,
				       AR0230_REG_VALUE_16BIT, again);
	} else if (gain >= 2764 && gain < 4023) {
		again = (u32)(32 - (256 * 345 / gain));
		again |= 0x0030;
		ret = ar0230_write_reg(ar0230->client, 0x3100,
				       AR0230_REG_VALUE_16BIT, 4);
		ret |= ar0230_write_reg(ar0230->client, AR0230_REG_ANALOG_GAIN,
				       AR0230_REG_VALUE_16BIT, again);
	}
	return ret;
}

static int ar0230_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ar0230 *ar0230 = container_of(ctrl->handler,
					     struct ar0230, ctrl_handler);
	struct i2c_client *client = ar0230->client;
	int ret = 0;

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = ar0230_write_reg(ar0230->client, AR0230_REG_EXPOSURE,
				       AR0230_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ar0230_set_gain(ar0230, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = ar0230_write_reg(ar0230->client, AR0230_REG_VTS,
				       AR0230_REG_VALUE_16BIT,
				       ctrl->val + ar0230->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ar0230_enable_test_pattern(ar0230, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ar0230_ctrl_ops = {
	.s_ctrl = ar0230_set_ctrl,
};

static int ar0230_initialize_controls(struct ar0230 *ar0230)
{
	const struct ar0230_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &ar0230->ctrl_handler;
	mode = ar0230->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &ar0230->mutex;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, AR0230_PIXEL_RATE, 1, AR0230_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	ar0230->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ar0230->hblank)
		ar0230->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ar0230->vblank = v4l2_ctrl_new_std(handler, &ar0230_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				AR0230_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 1;
	ar0230->exposure = v4l2_ctrl_new_std(handler, &ar0230_ctrl_ops,
				V4L2_CID_EXPOSURE, AR0230_EXPOSURE_MIN,
				exposure_max, AR0230_EXPOSURE_STEP,
				mode->exp_def);

	ar0230->anal_gain = v4l2_ctrl_new_std(handler, &ar0230_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	ar0230->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ar0230_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ar0230_test_pattern_menu) - 1,
				0, 0, ar0230_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&ar0230->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ar0230->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ar0230_check_sensor_id(struct ar0230 *ar0230,
				  struct i2c_client *client)
{
	struct device *dev = &ar0230->client->dev;
	u32 id = 0;
	int ret;

	ret = ar0230_read_reg(client, AR0230_REG_CHIP_ID,
			      AR0230_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected AR0230 sensor\n");

	return 0;
}

static int ar0230_configure_regulators(struct ar0230 *ar0230)
{
	u32 i;

	for (i = 0; i < AR0230_NUM_SUPPLIES; i++)
		ar0230->supplies[i].supply =
			ar0230_regulator[i].name;

	return devm_regulator_bulk_get(&ar0230->client->dev,
				       AR0230_NUM_SUPPLIES,
				       ar0230->supplies);
}

static int ar0230_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct ar0230 *ar0230;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	ar0230 = devm_kzalloc(dev, sizeof(*ar0230), GFP_KERNEL);
	if (!ar0230)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &ar0230->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &ar0230->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &ar0230->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &ar0230->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ar0230->client = client;
	ar0230->cur_mode = &supported_modes[0];

	ar0230->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ar0230->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	ar0230->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ar0230->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	ar0230->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ar0230->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = ar0230_configure_regulators(ar0230);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&ar0230->mutex);

	sd = &ar0230->subdev;
	v4l2_i2c_subdev_init(sd, client, &ar0230_subdev_ops);
	ret = ar0230_initialize_controls(ar0230);
	if (ret)
		goto err_destroy_mutex;

	ret = __ar0230_power_on(ar0230);
	if (ret)
		goto err_free_handler;

	ret = ar0230_check_sensor_id(ar0230, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ar0230_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ar0230->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &ar0230->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(ar0230->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 ar0230->module_index, facing,
		 AR0230_NAME, dev_name(sd->dev));
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
	__ar0230_power_off(ar0230);
err_free_handler:
	v4l2_ctrl_handler_free(&ar0230->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ar0230->mutex);

	return ret;
}

static int ar0230_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0230 *ar0230 = to_ar0230(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ar0230->ctrl_handler);
	mutex_destroy(&ar0230->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ar0230_power_off(ar0230);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ar0230_of_match[] = {
	{ .compatible = "aptina,ar0230" },
	{},
};
MODULE_DEVICE_TABLE(of, ar0230_of_match);
#endif

static const struct i2c_device_id ar0230_match_id[] = {
	{ "aptina,ar0230", 0 },
	{ },
};

static struct i2c_driver ar0230_i2c_driver = {
	.driver = {
		.name = AR0230_NAME,
		.pm = &ar0230_pm_ops,
		.of_match_table = of_match_ptr(ar0230_of_match),
	},
	.probe		= &ar0230_probe,
	.remove		= &ar0230_remove,
	.id_table	= ar0230_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ar0230_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ar0230_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Aptina ar0230 sensor driver");
MODULE_LICENSE("GPL v2");
