// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 Intel Corporation.

#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>

#define OV13858_REG_VALUE_08BIT		1
#define OV13858_REG_VALUE_16BIT		2
#define OV13858_REG_VALUE_24BIT		3

#define OV13858_REG_MODE_SELECT		0x0100
#define OV13858_MODE_STANDBY		0x00
#define OV13858_MODE_STREAMING		0x01

#define OV13858_REG_SOFTWARE_RST	0x0103
#define OV13858_SOFTWARE_RST		0x01

/* PLL1 generates PCLK and MIPI_PHY_CLK */
#define OV13858_REG_PLL1_CTRL_0		0x0300
#define OV13858_REG_PLL1_CTRL_1		0x0301
#define OV13858_REG_PLL1_CTRL_2		0x0302
#define OV13858_REG_PLL1_CTRL_3		0x0303
#define OV13858_REG_PLL1_CTRL_4		0x0304
#define OV13858_REG_PLL1_CTRL_5		0x0305

/* PLL2 generates DAC_CLK, SCLK and SRAM_CLK */
#define OV13858_REG_PLL2_CTRL_B		0x030b
#define OV13858_REG_PLL2_CTRL_C		0x030c
#define OV13858_REG_PLL2_CTRL_D		0x030d
#define OV13858_REG_PLL2_CTRL_E		0x030e
#define OV13858_REG_PLL2_CTRL_F		0x030f
#define OV13858_REG_PLL2_CTRL_12	0x0312
#define OV13858_REG_MIPI_SC_CTRL0	0x3016
#define OV13858_REG_MIPI_SC_CTRL1	0x3022

/* Chip ID */
#define OV13858_REG_CHIP_ID		0x300a
#define OV13858_CHIP_ID			0x00d855

/* V_TIMING internal */
#define OV13858_REG_VTS			0x380e
#define OV13858_VTS_30FPS		0x0c8e /* 30 fps */
#define OV13858_VTS_60FPS		0x0648 /* 60 fps */
#define OV13858_VTS_MAX			0x7fff

/* HBLANK control - read only */
#define OV13858_PPL_270MHZ		2244
#define OV13858_PPL_540MHZ		4488

/* Exposure control */
#define OV13858_REG_EXPOSURE		0x3500
#define OV13858_EXPOSURE_MIN		4
#define OV13858_EXPOSURE_STEP		1
#define OV13858_EXPOSURE_DEFAULT	0x640

/* Analog gain control */
#define OV13858_REG_ANALOG_GAIN		0x3508
#define OV13858_ANA_GAIN_MIN		0
#define OV13858_ANA_GAIN_MAX		0x1fff
#define OV13858_ANA_GAIN_STEP		1
#define OV13858_ANA_GAIN_DEFAULT	0x80

/* Digital gain control */
#define OV13858_REG_B_MWB_GAIN		0x5100
#define OV13858_REG_G_MWB_GAIN		0x5102
#define OV13858_REG_R_MWB_GAIN		0x5104
#define OV13858_DGTL_GAIN_MIN		0
#define OV13858_DGTL_GAIN_MAX		16384	/* Max = 16 X */
#define OV13858_DGTL_GAIN_DEFAULT	1024	/* Default gain = 1 X */
#define OV13858_DGTL_GAIN_STEP		1	/* Each step = 1/1024 */

/* Test Pattern Control */
#define OV13858_REG_TEST_PATTERN	0x4503
#define OV13858_TEST_PATTERN_ENABLE	BIT(7)
#define OV13858_TEST_PATTERN_MASK	0xfc

/* Number of frames to skip */
#define OV13858_NUM_OF_SKIP_FRAMES	2

struct ov13858_reg {
	u16 address;
	u8 val;
};

struct ov13858_reg_list {
	u32 num_of_regs;
	const struct ov13858_reg *regs;
};

/* Link frequency config */
struct ov13858_link_freq_config {
	u32 pixels_per_line;

	/* PLL registers for this link frequency */
	struct ov13858_reg_list reg_list;
};

/* Mode : resolution and related config&values */
struct ov13858_mode {
	/* Frame width */
	u32 width;
	/* Frame height */
	u32 height;

	/* V-timing */
	u32 vts_def;
	u32 vts_min;

	/* Index of Link frequency config to be used */
	u32 link_freq_index;
	/* Default register values */
	struct ov13858_reg_list reg_list;
};

/* 4224x3136 needs 1080Mbps/lane, 4 lanes */
static const struct ov13858_reg mipi_data_rate_1080mbps[] = {
	/* PLL1 registers */
	{OV13858_REG_PLL1_CTRL_0, 0x07},
	{OV13858_REG_PLL1_CTRL_1, 0x01},
	{OV13858_REG_PLL1_CTRL_2, 0xc2},
	{OV13858_REG_PLL1_CTRL_3, 0x00},
	{OV13858_REG_PLL1_CTRL_4, 0x00},
	{OV13858_REG_PLL1_CTRL_5, 0x01},

	/* PLL2 registers */
	{OV13858_REG_PLL2_CTRL_B, 0x05},
	{OV13858_REG_PLL2_CTRL_C, 0x01},
	{OV13858_REG_PLL2_CTRL_D, 0x0e},
	{OV13858_REG_PLL2_CTRL_E, 0x05},
	{OV13858_REG_PLL2_CTRL_F, 0x01},
	{OV13858_REG_PLL2_CTRL_12, 0x01},
	{OV13858_REG_MIPI_SC_CTRL0, 0x72},
	{OV13858_REG_MIPI_SC_CTRL1, 0x01},
};

/*
 * 2112x1568, 2112x1188, 1056x784 need 540Mbps/lane,
 * 4 lanes
 */
static const struct ov13858_reg mipi_data_rate_540mbps[] = {
	/* PLL1 registers */
	{OV13858_REG_PLL1_CTRL_0, 0x07},
	{OV13858_REG_PLL1_CTRL_1, 0x01},
	{OV13858_REG_PLL1_CTRL_2, 0xc2},
	{OV13858_REG_PLL1_CTRL_3, 0x01},
	{OV13858_REG_PLL1_CTRL_4, 0x00},
	{OV13858_REG_PLL1_CTRL_5, 0x01},

	/* PLL2 registers */
	{OV13858_REG_PLL2_CTRL_B, 0x05},
	{OV13858_REG_PLL2_CTRL_C, 0x01},
	{OV13858_REG_PLL2_CTRL_D, 0x0e},
	{OV13858_REG_PLL2_CTRL_E, 0x05},
	{OV13858_REG_PLL2_CTRL_F, 0x01},
	{OV13858_REG_PLL2_CTRL_12, 0x01},
	{OV13858_REG_MIPI_SC_CTRL0, 0x72},
	{OV13858_REG_MIPI_SC_CTRL1, 0x01},
};

static const struct ov13858_reg mode_4224x3136_regs[] = {
	{0x3013, 0x32},
	{0x301b, 0xf0},
	{0x301f, 0xd0},
	{0x3106, 0x15},
	{0x3107, 0x23},
	{0x350a, 0x00},
	{0x350e, 0x00},
	{0x3510, 0x00},
	{0x3511, 0x02},
	{0x3512, 0x00},
	{0x3600, 0x2b},
	{0x3601, 0x52},
	{0x3602, 0x60},
	{0x3612, 0x05},
	{0x3613, 0xa4},
	{0x3620, 0x80},
	{0x3621, 0x10},
	{0x3622, 0x30},
	{0x3624, 0x1c},
	{0x3640, 0x10},
	{0x3641, 0x70},
	{0x3660, 0x04},
	{0x3661, 0x80},
	{0x3662, 0x12},
	{0x3664, 0x73},
	{0x3665, 0xa7},
	{0x366e, 0xff},
	{0x366f, 0xf4},
	{0x3674, 0x00},
	{0x3679, 0x0c},
	{0x367f, 0x01},
	{0x3680, 0x0c},
	{0x3681, 0x50},
	{0x3682, 0x50},
	{0x3683, 0xa9},
	{0x3684, 0xa9},
	{0x3709, 0x5f},
	{0x3714, 0x24},
	{0x371a, 0x3e},
	{0x3737, 0x04},
	{0x3738, 0xcc},
	{0x3739, 0x12},
	{0x373d, 0x26},
	{0x3764, 0x20},
	{0x3765, 0x20},
	{0x37a1, 0x36},
	{0x37a8, 0x3b},
	{0x37ab, 0x31},
	{0x37c2, 0x04},
	{0x37c3, 0xf1},
	{0x37c5, 0x00},
	{0x37d8, 0x03},
	{0x37d9, 0x0c},
	{0x37da, 0xc2},
	{0x37dc, 0x02},
	{0x37e0, 0x00},
	{0x37e1, 0x0a},
	{0x37e2, 0x14},
	{0x37e3, 0x04},
	{0x37e4, 0x2a},
	{0x37e5, 0x03},
	{0x37e6, 0x04},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x08},
	{0x3804, 0x10},
	{0x3805, 0x9f},
	{0x3806, 0x0c},
	{0x3807, 0x57},
	{0x3808, 0x10},
	{0x3809, 0x80},
	{0x380a, 0x0c},
	{0x380b, 0x40},
	{0x380c, 0x04},
	{0x380d, 0x62},
	{0x380e, 0x0c},
	{0x380f, 0x8e},
	{0x3811, 0x04},
	{0x3813, 0x05},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3820, 0xa8},
	{0x3821, 0x00},
	{0x3822, 0xc2},
	{0x3823, 0x18},
	{0x3826, 0x11},
	{0x3827, 0x1c},
	{0x3829, 0x03},
	{0x3832, 0x00},
	{0x3c80, 0x00},
	{0x3c87, 0x01},
	{0x3c8c, 0x19},
	{0x3c8d, 0x1c},
	{0x3c90, 0x00},
	{0x3c91, 0x00},
	{0x3c92, 0x00},
	{0x3c93, 0x00},
	{0x3c94, 0x40},
	{0x3c95, 0x54},
	{0x3c96, 0x34},
	{0x3c97, 0x04},
	{0x3c98, 0x00},
	{0x3d8c, 0x73},
	{0x3d8d, 0xc0},
	{0x3f00, 0x0b},
	{0x3f03, 0x00},
	{0x4001, 0xe0},
	{0x4008, 0x00},
	{0x4009, 0x0f},
	{0x4011, 0xf0},
	{0x4017, 0x08},
	{0x4050, 0x04},
	{0x4051, 0x0b},
	{0x4052, 0x00},
	{0x4053, 0x80},
	{0x4054, 0x00},
	{0x4055, 0x80},
	{0x4056, 0x00},
	{0x4057, 0x80},
	{0x4058, 0x00},
	{0x4059, 0x80},
	{0x405e, 0x20},
	{0x4500, 0x07},
	{0x4503, 0x00},
	{0x450a, 0x04},
	{0x4809, 0x04},
	{0x480c, 0x12},
	{0x481f, 0x30},
	{0x4833, 0x10},
	{0x4837, 0x0e},
	{0x4902, 0x01},
	{0x4d00, 0x03},
	{0x4d01, 0xc9},
	{0x4d02, 0xbc},
	{0x4d03, 0xd7},
	{0x4d04, 0xf0},
	{0x4d05, 0xa2},
	{0x5000, 0xfd},
	{0x5001, 0x01},
	{0x5040, 0x39},
	{0x5041, 0x10},
	{0x5042, 0x10},
	{0x5043, 0x84},
	{0x5044, 0x62},
	{0x5180, 0x00},
	{0x5181, 0x10},
	{0x5182, 0x02},
	{0x5183, 0x0f},
	{0x5200, 0x1b},
	{0x520b, 0x07},
	{0x520c, 0x0f},
	{0x5300, 0x04},
	{0x5301, 0x0c},
	{0x5302, 0x0c},
	{0x5303, 0x0f},
	{0x5304, 0x00},
	{0x5305, 0x70},
	{0x5306, 0x00},
	{0x5307, 0x80},
	{0x5308, 0x00},
	{0x5309, 0xa5},
	{0x530a, 0x00},
	{0x530b, 0xd3},
	{0x530c, 0x00},
	{0x530d, 0xf0},
	{0x530e, 0x01},
	{0x530f, 0x10},
	{0x5310, 0x01},
	{0x5311, 0x20},
	{0x5312, 0x01},
	{0x5313, 0x20},
	{0x5314, 0x01},
	{0x5315, 0x20},
	{0x5316, 0x08},
	{0x5317, 0x08},
	{0x5318, 0x10},
	{0x5319, 0x88},
	{0x531a, 0x88},
	{0x531b, 0xa9},
	{0x531c, 0xaa},
	{0x531d, 0x0a},
	{0x5405, 0x02},
	{0x5406, 0x67},
	{0x5407, 0x01},
	{0x5408, 0x4a},
};

static const struct ov13858_reg mode_2112x1568_regs[] = {
	{0x3013, 0x32},
	{0x301b, 0xf0},
	{0x301f, 0xd0},
	{0x3106, 0x15},
	{0x3107, 0x23},
	{0x350a, 0x00},
	{0x350e, 0x00},
	{0x3510, 0x00},
	{0x3511, 0x02},
	{0x3512, 0x00},
	{0x3600, 0x2b},
	{0x3601, 0x52},
	{0x3602, 0x60},
	{0x3612, 0x05},
	{0x3613, 0xa4},
	{0x3620, 0x80},
	{0x3621, 0x10},
	{0x3622, 0x30},
	{0x3624, 0x1c},
	{0x3640, 0x10},
	{0x3641, 0x70},
	{0x3660, 0x04},
	{0x3661, 0x80},
	{0x3662, 0x10},
	{0x3664, 0x73},
	{0x3665, 0xa7},
	{0x366e, 0xff},
	{0x366f, 0xf4},
	{0x3674, 0x00},
	{0x3679, 0x0c},
	{0x367f, 0x01},
	{0x3680, 0x0c},
	{0x3681, 0x50},
	{0x3682, 0x50},
	{0x3683, 0xa9},
	{0x3684, 0xa9},
	{0x3709, 0x5f},
	{0x3714, 0x28},
	{0x371a, 0x3e},
	{0x3737, 0x08},
	{0x3738, 0xcc},
	{0x3739, 0x20},
	{0x373d, 0x26},
	{0x3764, 0x20},
	{0x3765, 0x20},
	{0x37a1, 0x36},
	{0x37a8, 0x3b},
	{0x37ab, 0x31},
	{0x37c2, 0x14},
	{0x37c3, 0xf1},
	{0x37c5, 0x00},
	{0x37d8, 0x03},
	{0x37d9, 0x0c},
	{0x37da, 0xc2},
	{0x37dc, 0x02},
	{0x37e0, 0x00},
	{0x37e1, 0x0a},
	{0x37e2, 0x14},
	{0x37e3, 0x08},
	{0x37e4, 0x38},
	{0x37e5, 0x03},
	{0x37e6, 0x08},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x10},
	{0x3805, 0x9f},
	{0x3806, 0x0c},
	{0x3807, 0x5f},
	{0x3808, 0x08},
	{0x3809, 0x40},
	{0x380a, 0x06},
	{0x380b, 0x20},
	{0x380c, 0x04},
	{0x380d, 0x62},
	{0x380e, 0x0c},
	{0x380f, 0x8e},
	{0x3811, 0x04},
	{0x3813, 0x05},
	{0x3814, 0x03},
	{0x3815, 0x01},
	{0x3816, 0x03},
	{0x3817, 0x01},
	{0x3820, 0xab},
	{0x3821, 0x00},
	{0x3822, 0xc2},
	{0x3823, 0x18},
	{0x3826, 0x04},
	{0x3827, 0x90},
	{0x3829, 0x07},
	{0x3832, 0x00},
	{0x3c80, 0x00},
	{0x3c87, 0x01},
	{0x3c8c, 0x19},
	{0x3c8d, 0x1c},
	{0x3c90, 0x00},
	{0x3c91, 0x00},
	{0x3c92, 0x00},
	{0x3c93, 0x00},
	{0x3c94, 0x40},
	{0x3c95, 0x54},
	{0x3c96, 0x34},
	{0x3c97, 0x04},
	{0x3c98, 0x00},
	{0x3d8c, 0x73},
	{0x3d8d, 0xc0},
	{0x3f00, 0x0b},
	{0x3f03, 0x00},
	{0x4001, 0xe0},
	{0x4008, 0x00},
	{0x4009, 0x0d},
	{0x4011, 0xf0},
	{0x4017, 0x08},
	{0x4050, 0x04},
	{0x4051, 0x0b},
	{0x4052, 0x00},
	{0x4053, 0x80},
	{0x4054, 0x00},
	{0x4055, 0x80},
	{0x4056, 0x00},
	{0x4057, 0x80},
	{0x4058, 0x00},
	{0x4059, 0x80},
	{0x405e, 0x20},
	{0x4500, 0x07},
	{0x4503, 0x00},
	{0x450a, 0x04},
	{0x4809, 0x04},
	{0x480c, 0x12},
	{0x481f, 0x30},
	{0x4833, 0x10},
	{0x4837, 0x1c},
	{0x4902, 0x01},
	{0x4d00, 0x03},
	{0x4d01, 0xc9},
	{0x4d02, 0xbc},
	{0x4d03, 0xd7},
	{0x4d04, 0xf0},
	{0x4d05, 0xa2},
	{0x5000, 0xfd},
	{0x5001, 0x01},
	{0x5040, 0x39},
	{0x5041, 0x10},
	{0x5042, 0x10},
	{0x5043, 0x84},
	{0x5044, 0x62},
	{0x5180, 0x00},
	{0x5181, 0x10},
	{0x5182, 0x02},
	{0x5183, 0x0f},
	{0x5200, 0x1b},
	{0x520b, 0x07},
	{0x520c, 0x0f},
	{0x5300, 0x04},
	{0x5301, 0x0c},
	{0x5302, 0x0c},
	{0x5303, 0x0f},
	{0x5304, 0x00},
	{0x5305, 0x70},
	{0x5306, 0x00},
	{0x5307, 0x80},
	{0x5308, 0x00},
	{0x5309, 0xa5},
	{0x530a, 0x00},
	{0x530b, 0xd3},
	{0x530c, 0x00},
	{0x530d, 0xf0},
	{0x530e, 0x01},
	{0x530f, 0x10},
	{0x5310, 0x01},
	{0x5311, 0x20},
	{0x5312, 0x01},
	{0x5313, 0x20},
	{0x5314, 0x01},
	{0x5315, 0x20},
	{0x5316, 0x08},
	{0x5317, 0x08},
	{0x5318, 0x10},
	{0x5319, 0x88},
	{0x531a, 0x88},
	{0x531b, 0xa9},
	{0x531c, 0xaa},
	{0x531d, 0x0a},
	{0x5405, 0x02},
	{0x5406, 0x67},
	{0x5407, 0x01},
	{0x5408, 0x4a},
};

static const struct ov13858_reg mode_2112x1188_regs[] = {
	{0x3013, 0x32},
	{0x301b, 0xf0},
	{0x301f, 0xd0},
	{0x3106, 0x15},
	{0x3107, 0x23},
	{0x350a, 0x00},
	{0x350e, 0x00},
	{0x3510, 0x00},
	{0x3511, 0x02},
	{0x3512, 0x00},
	{0x3600, 0x2b},
	{0x3601, 0x52},
	{0x3602, 0x60},
	{0x3612, 0x05},
	{0x3613, 0xa4},
	{0x3620, 0x80},
	{0x3621, 0x10},
	{0x3622, 0x30},
	{0x3624, 0x1c},
	{0x3640, 0x10},
	{0x3641, 0x70},
	{0x3660, 0x04},
	{0x3661, 0x80},
	{0x3662, 0x10},
	{0x3664, 0x73},
	{0x3665, 0xa7},
	{0x366e, 0xff},
	{0x366f, 0xf4},
	{0x3674, 0x00},
	{0x3679, 0x0c},
	{0x367f, 0x01},
	{0x3680, 0x0c},
	{0x3681, 0x50},
	{0x3682, 0x50},
	{0x3683, 0xa9},
	{0x3684, 0xa9},
	{0x3709, 0x5f},
	{0x3714, 0x28},
	{0x371a, 0x3e},
	{0x3737, 0x08},
	{0x3738, 0xcc},
	{0x3739, 0x20},
	{0x373d, 0x26},
	{0x3764, 0x20},
	{0x3765, 0x20},
	{0x37a1, 0x36},
	{0x37a8, 0x3b},
	{0x37ab, 0x31},
	{0x37c2, 0x14},
	{0x37c3, 0xf1},
	{0x37c5, 0x00},
	{0x37d8, 0x03},
	{0x37d9, 0x0c},
	{0x37da, 0xc2},
	{0x37dc, 0x02},
	{0x37e0, 0x00},
	{0x37e1, 0x0a},
	{0x37e2, 0x14},
	{0x37e3, 0x08},
	{0x37e4, 0x38},
	{0x37e5, 0x03},
	{0x37e6, 0x08},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x01},
	{0x3803, 0x84},
	{0x3804, 0x10},
	{0x3805, 0x9f},
	{0x3806, 0x0a},
	{0x3807, 0xd3},
	{0x3808, 0x08},
	{0x3809, 0x40},
	{0x380a, 0x04},
	{0x380b, 0xa4},
	{0x380c, 0x04},
	{0x380d, 0x62},
	{0x380e, 0x0c},
	{0x380f, 0x8e},
	{0x3811, 0x08},
	{0x3813, 0x03},
	{0x3814, 0x03},
	{0x3815, 0x01},
	{0x3816, 0x03},
	{0x3817, 0x01},
	{0x3820, 0xab},
	{0x3821, 0x00},
	{0x3822, 0xc2},
	{0x3823, 0x18},
	{0x3826, 0x04},
	{0x3827, 0x90},
	{0x3829, 0x07},
	{0x3832, 0x00},
	{0x3c80, 0x00},
	{0x3c87, 0x01},
	{0x3c8c, 0x19},
	{0x3c8d, 0x1c},
	{0x3c90, 0x00},
	{0x3c91, 0x00},
	{0x3c92, 0x00},
	{0x3c93, 0x00},
	{0x3c94, 0x40},
	{0x3c95, 0x54},
	{0x3c96, 0x34},
	{0x3c97, 0x04},
	{0x3c98, 0x00},
	{0x3d8c, 0x73},
	{0x3d8d, 0xc0},
	{0x3f00, 0x0b},
	{0x3f03, 0x00},
	{0x4001, 0xe0},
	{0x4008, 0x00},
	{0x4009, 0x0d},
	{0x4011, 0xf0},
	{0x4017, 0x08},
	{0x4050, 0x04},
	{0x4051, 0x0b},
	{0x4052, 0x00},
	{0x4053, 0x80},
	{0x4054, 0x00},
	{0x4055, 0x80},
	{0x4056, 0x00},
	{0x4057, 0x80},
	{0x4058, 0x00},
	{0x4059, 0x80},
	{0x405e, 0x20},
	{0x4500, 0x07},
	{0x4503, 0x00},
	{0x450a, 0x04},
	{0x4809, 0x04},
	{0x480c, 0x12},
	{0x481f, 0x30},
	{0x4833, 0x10},
	{0x4837, 0x1c},
	{0x4902, 0x01},
	{0x4d00, 0x03},
	{0x4d01, 0xc9},
	{0x4d02, 0xbc},
	{0x4d03, 0xd7},
	{0x4d04, 0xf0},
	{0x4d05, 0xa2},
	{0x5000, 0xfd},
	{0x5001, 0x01},
	{0x5040, 0x39},
	{0x5041, 0x10},
	{0x5042, 0x10},
	{0x5043, 0x84},
	{0x5044, 0x62},
	{0x5180, 0x00},
	{0x5181, 0x10},
	{0x5182, 0x02},
	{0x5183, 0x0f},
	{0x5200, 0x1b},
	{0x520b, 0x07},
	{0x520c, 0x0f},
	{0x5300, 0x04},
	{0x5301, 0x0c},
	{0x5302, 0x0c},
	{0x5303, 0x0f},
	{0x5304, 0x00},
	{0x5305, 0x70},
	{0x5306, 0x00},
	{0x5307, 0x80},
	{0x5308, 0x00},
	{0x5309, 0xa5},
	{0x530a, 0x00},
	{0x530b, 0xd3},
	{0x530c, 0x00},
	{0x530d, 0xf0},
	{0x530e, 0x01},
	{0x530f, 0x10},
	{0x5310, 0x01},
	{0x5311, 0x20},
	{0x5312, 0x01},
	{0x5313, 0x20},
	{0x5314, 0x01},
	{0x5315, 0x20},
	{0x5316, 0x08},
	{0x5317, 0x08},
	{0x5318, 0x10},
	{0x5319, 0x88},
	{0x531a, 0x88},
	{0x531b, 0xa9},
	{0x531c, 0xaa},
	{0x531d, 0x0a},
	{0x5405, 0x02},
	{0x5406, 0x67},
	{0x5407, 0x01},
	{0x5408, 0x4a},
};

static const struct ov13858_reg mode_1056x784_regs[] = {
	{0x3013, 0x32},
	{0x301b, 0xf0},
	{0x301f, 0xd0},
	{0x3106, 0x15},
	{0x3107, 0x23},
	{0x350a, 0x00},
	{0x350e, 0x00},
	{0x3510, 0x00},
	{0x3511, 0x02},
	{0x3512, 0x00},
	{0x3600, 0x2b},
	{0x3601, 0x52},
	{0x3602, 0x60},
	{0x3612, 0x05},
	{0x3613, 0xa4},
	{0x3620, 0x80},
	{0x3621, 0x10},
	{0x3622, 0x30},
	{0x3624, 0x1c},
	{0x3640, 0x10},
	{0x3641, 0x70},
	{0x3660, 0x04},
	{0x3661, 0x80},
	{0x3662, 0x08},
	{0x3664, 0x73},
	{0x3665, 0xa7},
	{0x366e, 0xff},
	{0x366f, 0xf4},
	{0x3674, 0x00},
	{0x3679, 0x0c},
	{0x367f, 0x01},
	{0x3680, 0x0c},
	{0x3681, 0x50},
	{0x3682, 0x50},
	{0x3683, 0xa9},
	{0x3684, 0xa9},
	{0x3709, 0x5f},
	{0x3714, 0x30},
	{0x371a, 0x3e},
	{0x3737, 0x08},
	{0x3738, 0xcc},
	{0x3739, 0x20},
	{0x373d, 0x26},
	{0x3764, 0x20},
	{0x3765, 0x20},
	{0x37a1, 0x36},
	{0x37a8, 0x3b},
	{0x37ab, 0x31},
	{0x37c2, 0x2c},
	{0x37c3, 0xf1},
	{0x37c5, 0x00},
	{0x37d8, 0x03},
	{0x37d9, 0x06},
	{0x37da, 0xc2},
	{0x37dc, 0x02},
	{0x37e0, 0x00},
	{0x37e1, 0x0a},
	{0x37e2, 0x14},
	{0x37e3, 0x08},
	{0x37e4, 0x36},
	{0x37e5, 0x03},
	{0x37e6, 0x08},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x10},
	{0x3805, 0x9f},
	{0x3806, 0x0c},
	{0x3807, 0x5f},
	{0x3808, 0x04},
	{0x3809, 0x20},
	{0x380a, 0x03},
	{0x380b, 0x10},
	{0x380c, 0x04},
	{0x380d, 0x62},
	{0x380e, 0x0c},
	{0x380f, 0x8e},
	{0x3811, 0x04},
	{0x3813, 0x05},
	{0x3814, 0x07},
	{0x3815, 0x01},
	{0x3816, 0x07},
	{0x3817, 0x01},
	{0x3820, 0xac},
	{0x3821, 0x00},
	{0x3822, 0xc2},
	{0x3823, 0x18},
	{0x3826, 0x04},
	{0x3827, 0x48},
	{0x3829, 0x03},
	{0x3832, 0x00},
	{0x3c80, 0x00},
	{0x3c87, 0x01},
	{0x3c8c, 0x19},
	{0x3c8d, 0x1c},
	{0x3c90, 0x00},
	{0x3c91, 0x00},
	{0x3c92, 0x00},
	{0x3c93, 0x00},
	{0x3c94, 0x40},
	{0x3c95, 0x54},
	{0x3c96, 0x34},
	{0x3c97, 0x04},
	{0x3c98, 0x00},
	{0x3d8c, 0x73},
	{0x3d8d, 0xc0},
	{0x3f00, 0x0b},
	{0x3f03, 0x00},
	{0x4001, 0xe0},
	{0x4008, 0x00},
	{0x4009, 0x05},
	{0x4011, 0xf0},
	{0x4017, 0x08},
	{0x4050, 0x02},
	{0x4051, 0x05},
	{0x4052, 0x00},
	{0x4053, 0x80},
	{0x4054, 0x00},
	{0x4055, 0x80},
	{0x4056, 0x00},
	{0x4057, 0x80},
	{0x4058, 0x00},
	{0x4059, 0x80},
	{0x405e, 0x20},
	{0x4500, 0x07},
	{0x4503, 0x00},
	{0x450a, 0x04},
	{0x4809, 0x04},
	{0x480c, 0x12},
	{0x481f, 0x30},
	{0x4833, 0x10},
	{0x4837, 0x1e},
	{0x4902, 0x02},
	{0x4d00, 0x03},
	{0x4d01, 0xc9},
	{0x4d02, 0xbc},
	{0x4d03, 0xd7},
	{0x4d04, 0xf0},
	{0x4d05, 0xa2},
	{0x5000, 0xfd},
	{0x5001, 0x01},
	{0x5040, 0x39},
	{0x5041, 0x10},
	{0x5042, 0x10},
	{0x5043, 0x84},
	{0x5044, 0x62},
	{0x5180, 0x00},
	{0x5181, 0x10},
	{0x5182, 0x02},
	{0x5183, 0x0f},
	{0x5200, 0x1b},
	{0x520b, 0x07},
	{0x520c, 0x0f},
	{0x5300, 0x04},
	{0x5301, 0x0c},
	{0x5302, 0x0c},
	{0x5303, 0x0f},
	{0x5304, 0x00},
	{0x5305, 0x70},
	{0x5306, 0x00},
	{0x5307, 0x80},
	{0x5308, 0x00},
	{0x5309, 0xa5},
	{0x530a, 0x00},
	{0x530b, 0xd3},
	{0x530c, 0x00},
	{0x530d, 0xf0},
	{0x530e, 0x01},
	{0x530f, 0x10},
	{0x5310, 0x01},
	{0x5311, 0x20},
	{0x5312, 0x01},
	{0x5313, 0x20},
	{0x5314, 0x01},
	{0x5315, 0x20},
	{0x5316, 0x08},
	{0x5317, 0x08},
	{0x5318, 0x10},
	{0x5319, 0x88},
	{0x531a, 0x88},
	{0x531b, 0xa9},
	{0x531c, 0xaa},
	{0x531d, 0x0a},
	{0x5405, 0x02},
	{0x5406, 0x67},
	{0x5407, 0x01},
	{0x5408, 0x4a},
};

static const char * const ov13858_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Configurations for supported link frequencies */
#define OV13858_NUM_OF_LINK_FREQS	2
#define OV13858_LINK_FREQ_540MHZ	540000000ULL
#define OV13858_LINK_FREQ_270MHZ	270000000ULL
#define OV13858_LINK_FREQ_INDEX_0	0
#define OV13858_LINK_FREQ_INDEX_1	1

/*
 * pixel_rate = link_freq * data-rate * nr_of_lanes / bits_per_sample
 * data rate => double data rate; number of lanes => 4; bits per pixel => 10
 */
static u64 link_freq_to_pixel_rate(u64 f)
{
	f *= 2 * 4;
	do_div(f, 10);

	return f;
}

/* Menu items for LINK_FREQ V4L2 control */
static const s64 link_freq_menu_items[OV13858_NUM_OF_LINK_FREQS] = {
	OV13858_LINK_FREQ_540MHZ,
	OV13858_LINK_FREQ_270MHZ
};

/* Link frequency configs */
static const struct ov13858_link_freq_config
			link_freq_configs[OV13858_NUM_OF_LINK_FREQS] = {
	{
		.pixels_per_line = OV13858_PPL_540MHZ,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_1080mbps),
			.regs = mipi_data_rate_1080mbps,
		}
	},
	{
		.pixels_per_line = OV13858_PPL_270MHZ,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_540mbps),
			.regs = mipi_data_rate_540mbps,
		}
	}
};

/* Mode configs */
static const struct ov13858_mode supported_modes[] = {
	{
		.width = 4224,
		.height = 3136,
		.vts_def = OV13858_VTS_30FPS,
		.vts_min = OV13858_VTS_30FPS,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_4224x3136_regs),
			.regs = mode_4224x3136_regs,
		},
		.link_freq_index = OV13858_LINK_FREQ_INDEX_0,
	},
	{
		.width = 2112,
		.height = 1568,
		.vts_def = OV13858_VTS_30FPS,
		.vts_min = 1608,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2112x1568_regs),
			.regs = mode_2112x1568_regs,
		},
		.link_freq_index = OV13858_LINK_FREQ_INDEX_1,
	},
	{
		.width = 2112,
		.height = 1188,
		.vts_def = OV13858_VTS_30FPS,
		.vts_min = 1608,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2112x1188_regs),
			.regs = mode_2112x1188_regs,
		},
		.link_freq_index = OV13858_LINK_FREQ_INDEX_1,
	},
	{
		.width = 1056,
		.height = 784,
		.vts_def = OV13858_VTS_30FPS,
		.vts_min = 804,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1056x784_regs),
			.regs = mode_1056x784_regs,
		},
		.link_freq_index = OV13858_LINK_FREQ_INDEX_1,
	}
};

struct ov13858 {
	struct v4l2_subdev sd;
	struct media_pad pad;

	struct v4l2_ctrl_handler ctrl_handler;
	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;

	/* Current mode */
	const struct ov13858_mode *cur_mode;

	/* Mutex for serialized access */
	struct mutex mutex;

	/* Streaming on/off */
	bool streaming;
};

#define to_ov13858(_sd)	container_of(_sd, struct ov13858, sd)

/* Read registers up to 4 at a time */
static int ov13858_read_reg(struct ov13858 *ov13858, u16 reg, u32 len,
			    u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov13858->sd);
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	int ret;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);

	if (len > 4)
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

/* Write registers up to 4 at a time */
static int ov13858_write_reg(struct ov13858 *ov13858, u16 reg, u32 len,
			     u32 __val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov13858->sd);
	int buf_i, val_i;
	u8 buf[6], *val_p;
	__be32 val;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val = cpu_to_be32(__val);
	val_p = (u8 *)&val;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

/* Write a list of registers */
static int ov13858_write_regs(struct ov13858 *ov13858,
			      const struct ov13858_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov13858->sd);
	int ret;
	u32 i;

	for (i = 0; i < len; i++) {
		ret = ov13858_write_reg(ov13858, regs[i].address, 1,
					regs[i].val);
		if (ret) {
			dev_err_ratelimited(
				&client->dev,
				"Failed to write reg 0x%4.4x. error = %d\n",
				regs[i].address, ret);

			return ret;
		}
	}

	return 0;
}

static int ov13858_write_reg_list(struct ov13858 *ov13858,
				  const struct ov13858_reg_list *r_list)
{
	return ov13858_write_regs(ov13858, r_list->regs, r_list->num_of_regs);
}

/* Open sub-device */
static int ov13858_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov13858 *ov13858 = to_ov13858(sd);
	struct v4l2_mbus_framefmt *try_fmt = v4l2_subdev_get_try_format(sd,
									fh->state,
									0);

	mutex_lock(&ov13858->mutex);

	/* Initialize try_fmt */
	try_fmt->width = ov13858->cur_mode->width;
	try_fmt->height = ov13858->cur_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	/* No crop or compose */
	mutex_unlock(&ov13858->mutex);

	return 0;
}

static int ov13858_update_digital_gain(struct ov13858 *ov13858, u32 d_gain)
{
	int ret;

	ret = ov13858_write_reg(ov13858, OV13858_REG_B_MWB_GAIN,
				OV13858_REG_VALUE_16BIT, d_gain);
	if (ret)
		return ret;

	ret = ov13858_write_reg(ov13858, OV13858_REG_G_MWB_GAIN,
				OV13858_REG_VALUE_16BIT, d_gain);
	if (ret)
		return ret;

	ret = ov13858_write_reg(ov13858, OV13858_REG_R_MWB_GAIN,
				OV13858_REG_VALUE_16BIT, d_gain);

	return ret;
}

static int ov13858_enable_test_pattern(struct ov13858 *ov13858, u32 pattern)
{
	int ret;
	u32 val;

	ret = ov13858_read_reg(ov13858, OV13858_REG_TEST_PATTERN,
			       OV13858_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	if (pattern) {
		val &= OV13858_TEST_PATTERN_MASK;
		val |= (pattern - 1) | OV13858_TEST_PATTERN_ENABLE;
	} else {
		val &= ~OV13858_TEST_PATTERN_ENABLE;
	}

	return ov13858_write_reg(ov13858, OV13858_REG_TEST_PATTERN,
				 OV13858_REG_VALUE_08BIT, val);
}

static int ov13858_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov13858 *ov13858 = container_of(ctrl->handler,
					       struct ov13858, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&ov13858->sd);
	s64 max;
	int ret;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov13858->cur_mode->height + ctrl->val - 8;
		__v4l2_ctrl_modify_range(ov13858->exposure,
					 ov13858->exposure->minimum,
					 max, ov13858->exposure->step, max);
		break;
	}

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	ret = 0;
	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov13858_write_reg(ov13858, OV13858_REG_ANALOG_GAIN,
					OV13858_REG_VALUE_16BIT, ctrl->val);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = ov13858_update_digital_gain(ov13858, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		ret = ov13858_write_reg(ov13858, OV13858_REG_EXPOSURE,
					OV13858_REG_VALUE_24BIT,
					ctrl->val << 4);
		break;
	case V4L2_CID_VBLANK:
		/* Update VTS that meets expected vertical blanking */
		ret = ov13858_write_reg(ov13858, OV13858_REG_VTS,
					OV13858_REG_VALUE_16BIT,
					ov13858->cur_mode->height
					  + ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov13858_enable_test_pattern(ov13858, ctrl->val);
		break;
	default:
		dev_info(&client->dev,
			 "ctrl(id:0x%x,val:0x%x) is not handled\n",
			 ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov13858_ctrl_ops = {
	.s_ctrl = ov13858_set_ctrl,
};

static int ov13858_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	/* Only one bayer order(GRBG) is supported */
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int ov13858_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SGRBG10_1X10)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static void ov13858_update_pad_format(const struct ov13858_mode *mode,
				      struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = MEDIA_BUS_FMT_SGRBG10_1X10;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int ov13858_do_get_pad_format(struct ov13858 *ov13858,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt;
	struct v4l2_subdev *sd = &ov13858->sd;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		ov13858_update_pad_format(ov13858->cur_mode, fmt);
	}

	return 0;
}

static int ov13858_get_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_format *fmt)
{
	struct ov13858 *ov13858 = to_ov13858(sd);
	int ret;

	mutex_lock(&ov13858->mutex);
	ret = ov13858_do_get_pad_format(ov13858, sd_state, fmt);
	mutex_unlock(&ov13858->mutex);

	return ret;
}

static int
ov13858_set_pad_format(struct v4l2_subdev *sd,
		       struct v4l2_subdev_state *sd_state,
		       struct v4l2_subdev_format *fmt)
{
	struct ov13858 *ov13858 = to_ov13858(sd);
	const struct ov13858_mode *mode;
	struct v4l2_mbus_framefmt *framefmt;
	s32 vblank_def;
	s32 vblank_min;
	s64 h_blank;
	s64 pixel_rate;
	s64 link_freq;

	mutex_lock(&ov13858->mutex);

	/* Only one raw bayer(GRBG) order is supported */
	if (fmt->format.code != MEDIA_BUS_FMT_SGRBG10_1X10)
		fmt->format.code = MEDIA_BUS_FMT_SGRBG10_1X10;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);
	ov13858_update_pad_format(mode, fmt);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_get_try_format(sd, sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		ov13858->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(ov13858->link_freq, mode->link_freq_index);
		link_freq = link_freq_menu_items[mode->link_freq_index];
		pixel_rate = link_freq_to_pixel_rate(link_freq);
		__v4l2_ctrl_s_ctrl_int64(ov13858->pixel_rate, pixel_rate);

		/* Update limits and set FPS to default */
		vblank_def = ov13858->cur_mode->vts_def -
			     ov13858->cur_mode->height;
		vblank_min = ov13858->cur_mode->vts_min -
			     ov13858->cur_mode->height;
		__v4l2_ctrl_modify_range(
			ov13858->vblank, vblank_min,
			OV13858_VTS_MAX - ov13858->cur_mode->height, 1,
			vblank_def);
		__v4l2_ctrl_s_ctrl(ov13858->vblank, vblank_def);
		h_blank =
			link_freq_configs[mode->link_freq_index].pixels_per_line
			 - ov13858->cur_mode->width;
		__v4l2_ctrl_modify_range(ov13858->hblank, h_blank,
					 h_blank, 1, h_blank);
	}

	mutex_unlock(&ov13858->mutex);

	return 0;
}

static int ov13858_get_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	*frames = OV13858_NUM_OF_SKIP_FRAMES;

	return 0;
}

/* Start streaming */
static int ov13858_start_streaming(struct ov13858 *ov13858)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov13858->sd);
	const struct ov13858_reg_list *reg_list;
	int ret, link_freq_index;

	/* Get out of from software reset */
	ret = ov13858_write_reg(ov13858, OV13858_REG_SOFTWARE_RST,
				OV13858_REG_VALUE_08BIT, OV13858_SOFTWARE_RST);
	if (ret) {
		dev_err(&client->dev, "%s failed to set powerup registers\n",
			__func__);
		return ret;
	}

	/* Setup PLL */
	link_freq_index = ov13858->cur_mode->link_freq_index;
	reg_list = &link_freq_configs[link_freq_index].reg_list;
	ret = ov13858_write_reg_list(ov13858, reg_list);
	if (ret) {
		dev_err(&client->dev, "%s failed to set plls\n", __func__);
		return ret;
	}

	/* Apply default values of current mode */
	reg_list = &ov13858->cur_mode->reg_list;
	ret = ov13858_write_reg_list(ov13858, reg_list);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(ov13858->sd.ctrl_handler);
	if (ret)
		return ret;

	return ov13858_write_reg(ov13858, OV13858_REG_MODE_SELECT,
				 OV13858_REG_VALUE_08BIT,
				 OV13858_MODE_STREAMING);
}

/* Stop streaming */
static int ov13858_stop_streaming(struct ov13858 *ov13858)
{
	return ov13858_write_reg(ov13858, OV13858_REG_MODE_SELECT,
				 OV13858_REG_VALUE_08BIT, OV13858_MODE_STANDBY);
}

static int ov13858_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov13858 *ov13858 = to_ov13858(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&ov13858->mutex);
	if (ov13858->streaming == enable) {
		mutex_unlock(&ov13858->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			goto err_unlock;

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = ov13858_start_streaming(ov13858);
		if (ret)
			goto err_rpm_put;
	} else {
		ov13858_stop_streaming(ov13858);
		pm_runtime_put(&client->dev);
	}

	ov13858->streaming = enable;
	mutex_unlock(&ov13858->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&ov13858->mutex);

	return ret;
}

static int __maybe_unused ov13858_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov13858 *ov13858 = to_ov13858(sd);

	if (ov13858->streaming)
		ov13858_stop_streaming(ov13858);

	return 0;
}

static int __maybe_unused ov13858_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov13858 *ov13858 = to_ov13858(sd);
	int ret;

	if (ov13858->streaming) {
		ret = ov13858_start_streaming(ov13858);
		if (ret)
			goto error;
	}

	return 0;

error:
	ov13858_stop_streaming(ov13858);
	ov13858->streaming = false;
	return ret;
}

/* Verify chip ID */
static int ov13858_identify_module(struct ov13858 *ov13858)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov13858->sd);
	int ret;
	u32 val;

	ret = ov13858_read_reg(ov13858, OV13858_REG_CHIP_ID,
			       OV13858_REG_VALUE_24BIT, &val);
	if (ret)
		return ret;

	if (val != OV13858_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x\n",
			OV13858_CHIP_ID, val);
		return -EIO;
	}

	return 0;
}

static const struct v4l2_subdev_core_ops ov13858_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops ov13858_video_ops = {
	.s_stream = ov13858_set_stream,
};

static const struct v4l2_subdev_pad_ops ov13858_pad_ops = {
	.enum_mbus_code = ov13858_enum_mbus_code,
	.get_fmt = ov13858_get_pad_format,
	.set_fmt = ov13858_set_pad_format,
	.enum_frame_size = ov13858_enum_frame_size,
};

static const struct v4l2_subdev_sensor_ops ov13858_sensor_ops = {
	.g_skip_frames = ov13858_get_skip_frames,
};

static const struct v4l2_subdev_ops ov13858_subdev_ops = {
	.core = &ov13858_core_ops,
	.video = &ov13858_video_ops,
	.pad = &ov13858_pad_ops,
	.sensor = &ov13858_sensor_ops,
};

static const struct media_entity_operations ov13858_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops ov13858_internal_ops = {
	.open = ov13858_open,
};

/* Initialize control handlers */
static int ov13858_init_controls(struct ov13858 *ov13858)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov13858->sd);
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 exposure_max;
	s64 vblank_def;
	s64 vblank_min;
	s64 hblank;
	s64 pixel_rate_min;
	s64 pixel_rate_max;
	const struct ov13858_mode *mode;
	int ret;

	ctrl_hdlr = &ov13858->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 10);
	if (ret)
		return ret;

	mutex_init(&ov13858->mutex);
	ctrl_hdlr->lock = &ov13858->mutex;
	ov13858->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr,
				&ov13858_ctrl_ops,
				V4L2_CID_LINK_FREQ,
				OV13858_NUM_OF_LINK_FREQS - 1,
				0,
				link_freq_menu_items);
	if (ov13858->link_freq)
		ov13858->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate_max = link_freq_to_pixel_rate(link_freq_menu_items[0]);
	pixel_rate_min = link_freq_to_pixel_rate(link_freq_menu_items[1]);
	/* By default, PIXEL_RATE is read only */
	ov13858->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ov13858_ctrl_ops,
						V4L2_CID_PIXEL_RATE,
						pixel_rate_min, pixel_rate_max,
						1, pixel_rate_max);

	mode = ov13858->cur_mode;
	vblank_def = mode->vts_def - mode->height;
	vblank_min = mode->vts_min - mode->height;
	ov13858->vblank = v4l2_ctrl_new_std(
				ctrl_hdlr, &ov13858_ctrl_ops, V4L2_CID_VBLANK,
				vblank_min, OV13858_VTS_MAX - mode->height, 1,
				vblank_def);

	hblank = link_freq_configs[mode->link_freq_index].pixels_per_line -
		 mode->width;
	ov13858->hblank = v4l2_ctrl_new_std(
				ctrl_hdlr, &ov13858_ctrl_ops, V4L2_CID_HBLANK,
				hblank, hblank, 1, hblank);
	if (ov13858->hblank)
		ov13858->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	exposure_max = mode->vts_def - 8;
	ov13858->exposure = v4l2_ctrl_new_std(
				ctrl_hdlr, &ov13858_ctrl_ops,
				V4L2_CID_EXPOSURE, OV13858_EXPOSURE_MIN,
				exposure_max, OV13858_EXPOSURE_STEP,
				OV13858_EXPOSURE_DEFAULT);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov13858_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV13858_ANA_GAIN_MIN, OV13858_ANA_GAIN_MAX,
			  OV13858_ANA_GAIN_STEP, OV13858_ANA_GAIN_DEFAULT);

	/* Digital gain */
	v4l2_ctrl_new_std(ctrl_hdlr, &ov13858_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV13858_DGTL_GAIN_MIN, OV13858_DGTL_GAIN_MAX,
			  OV13858_DGTL_GAIN_STEP, OV13858_DGTL_GAIN_DEFAULT);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov13858_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov13858_test_pattern_menu) - 1,
				     0, 0, ov13858_test_pattern_menu);
	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &ov13858_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	ov13858->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&ov13858->mutex);

	return ret;
}

static void ov13858_free_controls(struct ov13858 *ov13858)
{
	v4l2_ctrl_handler_free(ov13858->sd.ctrl_handler);
	mutex_destroy(&ov13858->mutex);
}

static int ov13858_probe(struct i2c_client *client)
{
	struct ov13858 *ov13858;
	int ret;
	u32 val = 0;

	device_property_read_u32(&client->dev, "clock-frequency", &val);
	if (val != 19200000)
		return -EINVAL;

	ov13858 = devm_kzalloc(&client->dev, sizeof(*ov13858), GFP_KERNEL);
	if (!ov13858)
		return -ENOMEM;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&ov13858->sd, client, &ov13858_subdev_ops);

	/* Check module identity */
	ret = ov13858_identify_module(ov13858);
	if (ret) {
		dev_err(&client->dev, "failed to find sensor: %d\n", ret);
		return ret;
	}

	/* Set default mode to max resolution */
	ov13858->cur_mode = &supported_modes[0];

	ret = ov13858_init_controls(ov13858);
	if (ret)
		return ret;

	/* Initialize subdev */
	ov13858->sd.internal_ops = &ov13858_internal_ops;
	ov13858->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			     V4L2_SUBDEV_FL_HAS_EVENTS;
	ov13858->sd.entity.ops = &ov13858_subdev_entity_ops;
	ov13858->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	ov13858->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&ov13858->sd.entity, 1, &ov13858->pad);
	if (ret) {
		dev_err(&client->dev, "%s failed:%d\n", __func__, ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&ov13858->sd);
	if (ret < 0)
		goto error_media_entity;

	/*
	 * Device is already turned on by i2c-core with ACPI domain PM.
	 * Enable runtime PM and turn off the device.
	 */
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&ov13858->sd.entity);

error_handler_free:
	ov13858_free_controls(ov13858);
	dev_err(&client->dev, "%s failed:%d\n", __func__, ret);

	return ret;
}

static void ov13858_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov13858 *ov13858 = to_ov13858(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	ov13858_free_controls(ov13858);

	pm_runtime_disable(&client->dev);
}

static const struct i2c_device_id ov13858_id_table[] = {
	{"ov13858", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, ov13858_id_table);

static const struct dev_pm_ops ov13858_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ov13858_suspend, ov13858_resume)
};

#ifdef CONFIG_ACPI
static const struct acpi_device_id ov13858_acpi_ids[] = {
	{"OVTID858"},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(acpi, ov13858_acpi_ids);
#endif

static struct i2c_driver ov13858_i2c_driver = {
	.driver = {
		.name = "ov13858",
		.pm = &ov13858_pm_ops,
		.acpi_match_table = ACPI_PTR(ov13858_acpi_ids),
	},
	.probe = ov13858_probe,
	.remove = ov13858_remove,
	.id_table = ov13858_id_table,
};

module_i2c_driver(ov13858_i2c_driver);

MODULE_AUTHOR("Kan, Chris <chris.kan@intel.com>");
MODULE_AUTHOR("Rapolu, Chiranjeevi <chiranjeevi.rapolu@intel.com>");
MODULE_AUTHOR("Yang, Hyungwoo");
MODULE_DESCRIPTION("Omnivision ov13858 sensor driver");
MODULE_LICENSE("GPL v2");
