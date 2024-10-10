// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 Intel Corporation.

#include <linux/unaligned.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define OV08X40_REG_VALUE_08BIT		1
#define OV08X40_REG_VALUE_16BIT		2
#define OV08X40_REG_VALUE_24BIT		3

#define OV08X40_REG_MODE_SELECT		0x0100
#define OV08X40_MODE_STANDBY		0x00
#define OV08X40_MODE_STREAMING		0x01

#define OV08X40_REG_AO_STANDBY		0x1000
#define OV08X40_AO_STREAMING		0x04

#define OV08X40_REG_MS_SELECT		0x1001
#define OV08X40_MS_STANDBY			0x00
#define OV08X40_MS_STREAMING		0x04

#define OV08X40_REG_SOFTWARE_RST	0x0103
#define OV08X40_SOFTWARE_RST		0x01

/* Chip ID */
#define OV08X40_REG_CHIP_ID		0x300a
#define OV08X40_CHIP_ID			0x560858

/* V_TIMING internal */
#define OV08X40_REG_VTS			0x380e
#define OV08X40_VTS_30FPS		0x09c4	/* the VTS need to be half in normal mode */
#define OV08X40_VTS_BIN_30FPS		0x115c
#define OV08X40_VTS_MAX			0x7fff

/* H TIMING internal */
#define OV08X40_REG_HTS			0x380c
#define OV08X40_HTS_30FPS		0x0280

/* Exposure control */
#define OV08X40_REG_EXPOSURE		0x3500
#define OV08X40_EXPOSURE_MAX_MARGIN	8
#define OV08X40_EXPOSURE_BIN_MAX_MARGIN	2
#define OV08X40_EXPOSURE_MIN		4
#define OV08X40_EXPOSURE_STEP		1
#define OV08X40_EXPOSURE_DEFAULT	0x40

/* Short Exposure control */
#define OV08X40_REG_SHORT_EXPOSURE	0x3540

/* Analog gain control */
#define OV08X40_REG_ANALOG_GAIN		0x3508
#define OV08X40_ANA_GAIN_MIN		0x80
#define OV08X40_ANA_GAIN_MAX		0x07c0
#define OV08X40_ANA_GAIN_STEP		1
#define OV08X40_ANA_GAIN_DEFAULT	0x80

/* Digital gain control */
#define OV08X40_REG_DGTL_GAIN_H		0x350a
#define OV08X40_REG_DGTL_GAIN_M		0x350b
#define OV08X40_REG_DGTL_GAIN_L		0x350c

#define OV08X40_DGTL_GAIN_MIN		1024	     /* Min = 1 X */
#define OV08X40_DGTL_GAIN_MAX		(4096 - 1)   /* Max = 4 X */
#define OV08X40_DGTL_GAIN_DEFAULT	2560	     /* Default gain = 2.5 X */
#define OV08X40_DGTL_GAIN_STEP		1            /* Each step = 1/1024 */

#define OV08X40_DGTL_GAIN_L_SHIFT	6
#define OV08X40_DGTL_GAIN_L_MASK	0x3
#define OV08X40_DGTL_GAIN_M_SHIFT	2
#define OV08X40_DGTL_GAIN_M_MASK	0xff
#define OV08X40_DGTL_GAIN_H_SHIFT	10
#define OV08X40_DGTL_GAIN_H_MASK	0x1F

/* Test Pattern Control */
#define OV08X40_REG_TEST_PATTERN	0x50C1
#define OV08X40_REG_ISP             0x5000
#define OV08X40_REG_SHORT_TEST_PATTERN  0x53C1
#define OV08X40_TEST_PATTERN_ENABLE	BIT(0)
#define OV08X40_TEST_PATTERN_MASK	0xcf
#define OV08X40_TEST_PATTERN_BAR_SHIFT	4

/* Flip Control */
#define OV08X40_REG_VFLIP		0x3820
#define OV08X40_REG_MIRROR		0x3821

/* Horizontal Window Offset */
#define OV08X40_REG_H_WIN_OFFSET	0x3811

/* Vertical Window Offset */
#define OV08X40_REG_V_WIN_OFFSET	0x3813

/* Burst Register */
#define OV08X40_REG_XTALK_FIRST_A	0x5a80
#define OV08X40_REG_XTALK_LAST_A	0x5b9f
#define OV08X40_REG_XTALK_FIRST_B	0x5bc0
#define OV08X40_REG_XTALK_LAST_B	0x5f1f

enum {
	OV08X40_LINK_FREQ_400MHZ_INDEX,
};

struct ov08x40_reg {
	u16 address;
	u8 val;
};

struct ov08x40_reg_list {
	u32 num_of_regs;
	const struct ov08x40_reg *regs;
};

/* Link frequency config */
struct ov08x40_link_freq_config {
	/* registers for this link frequency */
	struct ov08x40_reg_list reg_list;
};

/* Mode : resolution and related config&values */
struct ov08x40_mode {
	/* Frame width */
	u32 width;
	/* Frame height */
	u32 height;

	u32 lanes;
	/* V-timing */
	u32 vts_def;
	u32 vts_min;

	/* Line Length Pixels */
	u32 llp;

	/* Index of Link frequency config to be used */
	u32 link_freq_index;
	/* Default register values */
	struct ov08x40_reg_list reg_list;

	/* Exposure calculation */
	u16 exposure_margin;
	u16 exposure_shift;
};

static const struct ov08x40_reg mipi_data_rate_800mbps[] = {
	{0x0103, 0x01},
	{0x1000, 0x00},
	{0x1601, 0xd0},
	{0x1001, 0x04},
	{0x5004, 0x53},
	{0x5110, 0x00},
	{0x5111, 0x14},
	{0x5112, 0x01},
	{0x5113, 0x7b},
	{0x5114, 0x00},
	{0x5152, 0xa3},
	{0x5a52, 0x1f},
	{0x5a1a, 0x0e},
	{0x5a1b, 0x10},
	{0x5a1f, 0x0e},
	{0x5a27, 0x0e},
	{0x6002, 0x2e},
};

static const struct ov08x40_reg mode_3856x2416_regs[] = {
	{0x5000, 0x5d},
	{0x5001, 0x20},
	{0x5008, 0xb0},
	{0x50c1, 0x00},
	{0x53c1, 0x00},
	{0x5f40, 0x00},
	{0x5f41, 0x40},
	{0x0300, 0x3a},
	{0x0301, 0xc8},
	{0x0302, 0x31},
	{0x0303, 0x03},
	{0x0304, 0x01},
	{0x0305, 0xa1},
	{0x0306, 0x04},
	{0x0307, 0x01},
	{0x0308, 0x03},
	{0x0309, 0x03},
	{0x0310, 0x0a},
	{0x0311, 0x02},
	{0x0312, 0x01},
	{0x0313, 0x08},
	{0x0314, 0x66},
	{0x0315, 0x00},
	{0x0316, 0x34},
	{0x0320, 0x02},
	{0x0321, 0x03},
	{0x0323, 0x05},
	{0x0324, 0x01},
	{0x0325, 0xb8},
	{0x0326, 0x4a},
	{0x0327, 0x04},
	{0x0329, 0x00},
	{0x032a, 0x05},
	{0x032b, 0x00},
	{0x032c, 0x00},
	{0x032d, 0x00},
	{0x032e, 0x02},
	{0x032f, 0xa0},
	{0x0350, 0x00},
	{0x0360, 0x01},
	{0x1216, 0x60},
	{0x1217, 0x5b},
	{0x1218, 0x00},
	{0x1220, 0x24},
	{0x198a, 0x00},
	{0x198b, 0x01},
	{0x198e, 0x00},
	{0x198f, 0x01},
	{0x3009, 0x04},
	{0x3012, 0x41},
	{0x3015, 0x00},
	{0x3016, 0xb0},
	{0x3017, 0xf0},
	{0x3018, 0xf0},
	{0x3019, 0xd2},
	{0x301a, 0xb0},
	{0x301c, 0x81},
	{0x301d, 0x02},
	{0x301e, 0x80},
	{0x3022, 0xf0},
	{0x3025, 0x89},
	{0x3030, 0x03},
	{0x3044, 0xc2},
	{0x3050, 0x35},
	{0x3051, 0x60},
	{0x3052, 0x25},
	{0x3053, 0x00},
	{0x3054, 0x00},
	{0x3055, 0x02},
	{0x3056, 0x80},
	{0x3057, 0x80},
	{0x3058, 0x80},
	{0x3059, 0x00},
	{0x3107, 0x86},
	{0x3400, 0x1c},
	{0x3401, 0x80},
	{0x3402, 0x8c},
	{0x3419, 0x13},
	{0x341a, 0x89},
	{0x341b, 0x30},
	{0x3420, 0x00},
	{0x3421, 0x00},
	{0x3422, 0x00},
	{0x3423, 0x00},
	{0x3424, 0x00},
	{0x3425, 0x00},
	{0x3426, 0x00},
	{0x3427, 0x00},
	{0x3428, 0x0f},
	{0x3429, 0x00},
	{0x342a, 0x00},
	{0x342b, 0x00},
	{0x342c, 0x00},
	{0x342d, 0x00},
	{0x342e, 0x00},
	{0x342f, 0x11},
	{0x3430, 0x11},
	{0x3431, 0x10},
	{0x3432, 0x00},
	{0x3433, 0x00},
	{0x3434, 0x00},
	{0x3435, 0x00},
	{0x3436, 0x00},
	{0x3437, 0x00},
	{0x3442, 0x02},
	{0x3443, 0x02},
	{0x3444, 0x07},
	{0x3450, 0x00},
	{0x3451, 0x00},
	{0x3452, 0x18},
	{0x3453, 0x18},
	{0x3454, 0x00},
	{0x3455, 0x80},
	{0x3456, 0x08},
	{0x3500, 0x00},
	{0x3501, 0x02},
	{0x3502, 0x00},
	{0x3504, 0x4c},
	{0x3506, 0x30},
	{0x3507, 0x00},
	{0x3508, 0x01},
	{0x3509, 0x00},
	{0x350a, 0x01},
	{0x350b, 0x00},
	{0x350c, 0x00},
	{0x3540, 0x00},
	{0x3541, 0x01},
	{0x3542, 0x00},
	{0x3544, 0x4c},
	{0x3546, 0x30},
	{0x3547, 0x00},
	{0x3548, 0x01},
	{0x3549, 0x00},
	{0x354a, 0x01},
	{0x354b, 0x00},
	{0x354c, 0x00},
	{0x3688, 0x02},
	{0x368a, 0x2e},
	{0x368e, 0x71},
	{0x3696, 0xd1},
	{0x3699, 0x00},
	{0x369a, 0x00},
	{0x36a4, 0x00},
	{0x36a6, 0x00},
	{0x3711, 0x00},
	{0x3712, 0x51},
	{0x3713, 0x00},
	{0x3714, 0x24},
	{0x3716, 0x00},
	{0x3718, 0x07},
	{0x371a, 0x1c},
	{0x371b, 0x00},
	{0x3720, 0x08},
	{0x3725, 0x32},
	{0x3727, 0x05},
	{0x3760, 0x02},
	{0x3761, 0x17},
	{0x3762, 0x02},
	{0x3763, 0x02},
	{0x3764, 0x02},
	{0x3765, 0x2c},
	{0x3766, 0x04},
	{0x3767, 0x2c},
	{0x3768, 0x02},
	{0x3769, 0x00},
	{0x376b, 0x20},
	{0x376e, 0x03},
	{0x37b0, 0x00},
	{0x37b1, 0xab},
	{0x37b2, 0x01},
	{0x37b3, 0x82},
	{0x37b4, 0x00},
	{0x37b5, 0xe4},
	{0x37b6, 0x01},
	{0x37b7, 0xee},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x0f},
	{0x3805, 0x1f},
	{0x3806, 0x09},
	{0x3807, 0x7f},
	{0x3808, 0x0f},
	{0x3809, 0x10},
	{0x380a, 0x09},
	{0x380b, 0x70},
	{0x380c, 0x02},
	{0x380d, 0x80},
	{0x380e, 0x13},
	{0x380f, 0x88},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x07},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3820, 0x00},
	{0x3821, 0x04},
	{0x3822, 0x00},
	{0x3823, 0x04},
	{0x3828, 0x0f},
	{0x382a, 0x80},
	{0x382e, 0x41},
	{0x3837, 0x08},
	{0x383a, 0x81},
	{0x383b, 0x81},
	{0x383c, 0x11},
	{0x383d, 0x11},
	{0x383e, 0x00},
	{0x383f, 0x38},
	{0x3840, 0x00},
	{0x3847, 0x00},
	{0x384a, 0x00},
	{0x384c, 0x02},
	{0x384d, 0x80},
	{0x3856, 0x50},
	{0x3857, 0x30},
	{0x3858, 0x80},
	{0x3859, 0x40},
	{0x3860, 0x00},
	{0x3888, 0x00},
	{0x3889, 0x00},
	{0x388a, 0x00},
	{0x388b, 0x00},
	{0x388c, 0x00},
	{0x388d, 0x00},
	{0x388e, 0x00},
	{0x388f, 0x00},
	{0x3894, 0x00},
	{0x3895, 0x00},
	{0x3c84, 0x00},
	{0x3d85, 0x8b},
	{0x3daa, 0x80},
	{0x3dab, 0x14},
	{0x3dac, 0x80},
	{0x3dad, 0xc8},
	{0x3dae, 0x81},
	{0x3daf, 0x7b},
	{0x3f00, 0x10},
	{0x3f01, 0x11},
	{0x3f06, 0x0d},
	{0x3f07, 0x0b},
	{0x3f08, 0x0d},
	{0x3f09, 0x0b},
	{0x3f0a, 0x01},
	{0x3f0b, 0x11},
	{0x3f0c, 0x33},
	{0x4001, 0x07},
	{0x4007, 0x20},
	{0x4008, 0x00},
	{0x4009, 0x05},
	{0x400a, 0x00},
	{0x400b, 0x08},
	{0x400c, 0x00},
	{0x400d, 0x08},
	{0x400e, 0x14},
	{0x4010, 0xf4},
	{0x4011, 0x03},
	{0x4012, 0x55},
	{0x4015, 0x00},
	{0x4016, 0x2d},
	{0x4017, 0x00},
	{0x4018, 0x0f},
	{0x401b, 0x08},
	{0x401c, 0x00},
	{0x401d, 0x10},
	{0x401e, 0x02},
	{0x401f, 0x00},
	{0x4050, 0x06},
	{0x4051, 0xff},
	{0x4052, 0xff},
	{0x4053, 0xff},
	{0x4054, 0xff},
	{0x4055, 0xff},
	{0x4056, 0xff},
	{0x4057, 0x7f},
	{0x4058, 0x00},
	{0x4059, 0x00},
	{0x405a, 0x00},
	{0x405b, 0x00},
	{0x405c, 0x07},
	{0x405d, 0xff},
	{0x405e, 0x07},
	{0x405f, 0xff},
	{0x4080, 0x78},
	{0x4081, 0x78},
	{0x4082, 0x78},
	{0x4083, 0x78},
	{0x4019, 0x00},
	{0x401a, 0x40},
	{0x4020, 0x04},
	{0x4021, 0x00},
	{0x4022, 0x04},
	{0x4023, 0x00},
	{0x4024, 0x04},
	{0x4025, 0x00},
	{0x4026, 0x04},
	{0x4027, 0x00},
	{0x4030, 0x00},
	{0x4031, 0x00},
	{0x4032, 0x00},
	{0x4033, 0x00},
	{0x4034, 0x00},
	{0x4035, 0x00},
	{0x4036, 0x00},
	{0x4037, 0x00},
	{0x4040, 0x00},
	{0x4041, 0x80},
	{0x4042, 0x00},
	{0x4043, 0x80},
	{0x4044, 0x00},
	{0x4045, 0x80},
	{0x4046, 0x00},
	{0x4047, 0x80},
	{0x4060, 0x00},
	{0x4061, 0x00},
	{0x4062, 0x00},
	{0x4063, 0x00},
	{0x4064, 0x00},
	{0x4065, 0x00},
	{0x4066, 0x00},
	{0x4067, 0x00},
	{0x4068, 0x00},
	{0x4069, 0x00},
	{0x406a, 0x00},
	{0x406b, 0x00},
	{0x406c, 0x00},
	{0x406d, 0x00},
	{0x406e, 0x00},
	{0x406f, 0x00},
	{0x4070, 0x00},
	{0x4071, 0x00},
	{0x4072, 0x00},
	{0x4073, 0x00},
	{0x4074, 0x00},
	{0x4075, 0x00},
	{0x4076, 0x00},
	{0x4077, 0x00},
	{0x4078, 0x00},
	{0x4079, 0x00},
	{0x407a, 0x00},
	{0x407b, 0x00},
	{0x407c, 0x00},
	{0x407d, 0x00},
	{0x407e, 0x00},
	{0x407f, 0x00},
	{0x40e0, 0x00},
	{0x40e1, 0x00},
	{0x40e2, 0x00},
	{0x40e3, 0x00},
	{0x40e4, 0x00},
	{0x40e5, 0x00},
	{0x40e6, 0x00},
	{0x40e7, 0x00},
	{0x40e8, 0x00},
	{0x40e9, 0x80},
	{0x40ea, 0x00},
	{0x40eb, 0x80},
	{0x40ec, 0x00},
	{0x40ed, 0x80},
	{0x40ee, 0x00},
	{0x40ef, 0x80},
	{0x40f0, 0x02},
	{0x40f1, 0x04},
	{0x4300, 0x00},
	{0x4301, 0x00},
	{0x4302, 0x00},
	{0x4303, 0x00},
	{0x4304, 0x00},
	{0x4305, 0x00},
	{0x4306, 0x00},
	{0x4307, 0x00},
	{0x4308, 0x00},
	{0x4309, 0x00},
	{0x430a, 0x00},
	{0x430b, 0xff},
	{0x430c, 0xff},
	{0x430d, 0x00},
	{0x430e, 0x00},
	{0x4315, 0x00},
	{0x4316, 0x00},
	{0x4317, 0x00},
	{0x4318, 0x00},
	{0x4319, 0x00},
	{0x431a, 0x00},
	{0x431b, 0x00},
	{0x431c, 0x00},
	{0x4500, 0x07},
	{0x4501, 0x00},
	{0x4502, 0x00},
	{0x4503, 0x0f},
	{0x4504, 0x80},
	{0x4506, 0x01},
	{0x4509, 0x05},
	{0x450c, 0x00},
	{0x450d, 0x20},
	{0x450e, 0x00},
	{0x450f, 0x00},
	{0x4510, 0x00},
	{0x4523, 0x00},
	{0x4526, 0x00},
	{0x4542, 0x00},
	{0x4543, 0x00},
	{0x4544, 0x00},
	{0x4545, 0x00},
	{0x4546, 0x00},
	{0x4547, 0x10},
	{0x4602, 0x00},
	{0x4603, 0x15},
	{0x460b, 0x07},
	{0x4680, 0x11},
	{0x4686, 0x00},
	{0x4687, 0x00},
	{0x4700, 0x00},
	{0x4800, 0x64},
	{0x4806, 0x40},
	{0x480b, 0x10},
	{0x480c, 0x80},
	{0x480f, 0x32},
	{0x4813, 0xe4},
	{0x4837, 0x14},
	{0x4850, 0x42},
	{0x4884, 0x04},
	{0x4c00, 0xf8},
	{0x4c01, 0x44},
	{0x4c03, 0x00},
	{0x4d00, 0x00},
	{0x4d01, 0x16},
	{0x4d04, 0x10},
	{0x4d05, 0x00},
	{0x4d06, 0x0c},
	{0x4d07, 0x00},
	{0x3d84, 0x04},
	{0x3680, 0xa4},
	{0x3682, 0x80},
	{0x3601, 0x40},
	{0x3602, 0x90},
	{0x3608, 0x0a},
	{0x3938, 0x09},
	{0x3a74, 0x84},
	{0x3a99, 0x84},
	{0x3ab9, 0xa6},
	{0x3aba, 0xba},
	{0x3b12, 0x84},
	{0x3b14, 0xbb},
	{0x3b15, 0xbf},
	{0x3a29, 0x26},
	{0x3a1f, 0x8a},
	{0x3a22, 0x91},
	{0x3a25, 0x96},
	{0x3a28, 0xb4},
	{0x3a2b, 0xba},
	{0x3a2e, 0xbf},
	{0x3a31, 0xc1},
	{0x3a20, 0x00},
	{0x3939, 0x9d},
	{0x3902, 0x0e},
	{0x3903, 0x0e},
	{0x3904, 0x0e},
	{0x3905, 0x0e},
	{0x3906, 0x07},
	{0x3907, 0x0d},
	{0x3908, 0x11},
	{0x3909, 0x12},
	{0x360f, 0x99},
	{0x390c, 0x33},
	{0x390d, 0x66},
	{0x390e, 0xaa},
	{0x3911, 0x90},
	{0x3913, 0x90},
	{0x3915, 0x90},
	{0x3917, 0x90},
	{0x3b3f, 0x9d},
	{0x3b45, 0x9d},
	{0x3b1b, 0xc9},
	{0x3b21, 0xc9},
	{0x3440, 0xa4},
	{0x3a23, 0x15},
	{0x3a26, 0x1d},
	{0x3a2c, 0x4a},
	{0x3a2f, 0x18},
	{0x3a32, 0x55},
	{0x3b0a, 0x01},
	{0x3b0b, 0x00},
	{0x3b0e, 0x01},
	{0x3b0f, 0x00},
	{0x392c, 0x02},
	{0x392d, 0x02},
	{0x392e, 0x04},
	{0x392f, 0x03},
	{0x3930, 0x08},
	{0x3931, 0x07},
	{0x3932, 0x10},
	{0x3933, 0x0c},
	{0x3609, 0x08},
	{0x3921, 0x0f},
	{0x3928, 0x15},
	{0x3929, 0x2a},
	{0x392a, 0x54},
	{0x392b, 0xa8},
	{0x3426, 0x10},
	{0x3407, 0x01},
	{0x3404, 0x01},
	{0x3500, 0x00},
	{0x3501, 0x10},
	{0x3502, 0x10},
	{0x3508, 0x0f},
	{0x3509, 0x80},
};

static const struct ov08x40_reg mode_1928x1208_regs[] = {
	{0x5000, 0x55},
	{0x5001, 0x00},
	{0x5008, 0xb0},
	{0x50c1, 0x00},
	{0x53c1, 0x00},
	{0x5f40, 0x00},
	{0x5f41, 0x40},
	{0x0300, 0x3a},
	{0x0301, 0xc8},
	{0x0302, 0x31},
	{0x0303, 0x03},
	{0x0304, 0x01},
	{0x0305, 0xa1},
	{0x0306, 0x04},
	{0x0307, 0x01},
	{0x0308, 0x03},
	{0x0309, 0x03},
	{0x0310, 0x0a},
	{0x0311, 0x02},
	{0x0312, 0x01},
	{0x0313, 0x08},
	{0x0314, 0x66},
	{0x0315, 0x00},
	{0x0316, 0x34},
	{0x0320, 0x02},
	{0x0321, 0x03},
	{0x0323, 0x05},
	{0x0324, 0x01},
	{0x0325, 0xb8},
	{0x0326, 0x4a},
	{0x0327, 0x04},
	{0x0329, 0x00},
	{0x032a, 0x05},
	{0x032b, 0x00},
	{0x032c, 0x00},
	{0x032d, 0x00},
	{0x032e, 0x02},
	{0x032f, 0xa0},
	{0x0350, 0x00},
	{0x0360, 0x01},
	{0x1216, 0x60},
	{0x1217, 0x5b},
	{0x1218, 0x00},
	{0x1220, 0x24},
	{0x198a, 0x00},
	{0x198b, 0x01},
	{0x198e, 0x00},
	{0x198f, 0x01},
	{0x3009, 0x04},
	{0x3012, 0x41},
	{0x3015, 0x00},
	{0x3016, 0xb0},
	{0x3017, 0xf0},
	{0x3018, 0xf0},
	{0x3019, 0xd2},
	{0x301a, 0xb0},
	{0x301c, 0x81},
	{0x301d, 0x02},
	{0x301e, 0x80},
	{0x3022, 0xf0},
	{0x3025, 0x89},
	{0x3030, 0x03},
	{0x3044, 0xc2},
	{0x3050, 0x35},
	{0x3051, 0x60},
	{0x3052, 0x25},
	{0x3053, 0x00},
	{0x3054, 0x00},
	{0x3055, 0x02},
	{0x3056, 0x80},
	{0x3057, 0x80},
	{0x3058, 0x80},
	{0x3059, 0x00},
	{0x3107, 0x86},
	{0x3400, 0x1c},
	{0x3401, 0x80},
	{0x3402, 0x8c},
	{0x3419, 0x08},
	{0x341a, 0xaf},
	{0x341b, 0x30},
	{0x3420, 0x00},
	{0x3421, 0x00},
	{0x3422, 0x00},
	{0x3423, 0x00},
	{0x3424, 0x00},
	{0x3425, 0x00},
	{0x3426, 0x00},
	{0x3427, 0x00},
	{0x3428, 0x0f},
	{0x3429, 0x00},
	{0x342a, 0x00},
	{0x342b, 0x00},
	{0x342c, 0x00},
	{0x342d, 0x00},
	{0x342e, 0x00},
	{0x342f, 0x11},
	{0x3430, 0x11},
	{0x3431, 0x10},
	{0x3432, 0x00},
	{0x3433, 0x00},
	{0x3434, 0x00},
	{0x3435, 0x00},
	{0x3436, 0x00},
	{0x3437, 0x00},
	{0x3442, 0x02},
	{0x3443, 0x02},
	{0x3444, 0x07},
	{0x3450, 0x00},
	{0x3451, 0x00},
	{0x3452, 0x18},
	{0x3453, 0x18},
	{0x3454, 0x00},
	{0x3455, 0x80},
	{0x3456, 0x08},
	{0x3500, 0x00},
	{0x3501, 0x02},
	{0x3502, 0x00},
	{0x3504, 0x4c},
	{0x3506, 0x30},
	{0x3507, 0x00},
	{0x3508, 0x01},
	{0x3509, 0x00},
	{0x350a, 0x01},
	{0x350b, 0x00},
	{0x350c, 0x00},
	{0x3540, 0x00},
	{0x3541, 0x01},
	{0x3542, 0x00},
	{0x3544, 0x4c},
	{0x3546, 0x30},
	{0x3547, 0x00},
	{0x3548, 0x01},
	{0x3549, 0x00},
	{0x354a, 0x01},
	{0x354b, 0x00},
	{0x354c, 0x00},
	{0x3688, 0x02},
	{0x368a, 0x2e},
	{0x368e, 0x71},
	{0x3696, 0xd1},
	{0x3699, 0x00},
	{0x369a, 0x00},
	{0x36a4, 0x00},
	{0x36a6, 0x00},
	{0x3711, 0x00},
	{0x3712, 0x50},
	{0x3713, 0x00},
	{0x3714, 0x21},
	{0x3716, 0x00},
	{0x3718, 0x07},
	{0x371a, 0x1c},
	{0x371b, 0x00},
	{0x3720, 0x08},
	{0x3725, 0x32},
	{0x3727, 0x05},
	{0x3760, 0x02},
	{0x3761, 0x28},
	{0x3762, 0x02},
	{0x3763, 0x02},
	{0x3764, 0x02},
	{0x3765, 0x2c},
	{0x3766, 0x04},
	{0x3767, 0x2c},
	{0x3768, 0x02},
	{0x3769, 0x00},
	{0x376b, 0x20},
	{0x376e, 0x07},
	{0x37b0, 0x01},
	{0x37b1, 0x0f},
	{0x37b2, 0x01},
	{0x37b3, 0xd6},
	{0x37b4, 0x01},
	{0x37b5, 0x48},
	{0x37b6, 0x02},
	{0x37b7, 0x40},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x0f},
	{0x3805, 0x1f},
	{0x3806, 0x09},
	{0x3807, 0x7f},
	{0x3808, 0x07},
	{0x3809, 0x88},
	{0x380a, 0x04},
	{0x380b, 0xb8},
	{0x380c, 0x02},
	{0x380d, 0xd0},
	{0x380e, 0x11},
	{0x380f, 0x5c},
	{0x3810, 0x00},
	{0x3811, 0x04},
	{0x3812, 0x00},
	{0x3813, 0x03},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3820, 0x02},
	{0x3821, 0x14},
	{0x3822, 0x00},
	{0x3823, 0x04},
	{0x3828, 0x0f},
	{0x382a, 0x80},
	{0x382e, 0x41},
	{0x3837, 0x08},
	{0x383a, 0x81},
	{0x383b, 0x81},
	{0x383c, 0x11},
	{0x383d, 0x11},
	{0x383e, 0x00},
	{0x383f, 0x38},
	{0x3840, 0x00},
	{0x3847, 0x00},
	{0x384a, 0x00},
	{0x384c, 0x02},
	{0x384d, 0xd0},
	{0x3856, 0x50},
	{0x3857, 0x30},
	{0x3858, 0x80},
	{0x3859, 0x40},
	{0x3860, 0x00},
	{0x3888, 0x00},
	{0x3889, 0x00},
	{0x388a, 0x00},
	{0x388b, 0x00},
	{0x388c, 0x00},
	{0x388d, 0x00},
	{0x388e, 0x00},
	{0x388f, 0x00},
	{0x3894, 0x00},
	{0x3895, 0x00},
	{0x3c84, 0x00},
	{0x3d85, 0x8b},
	{0x3daa, 0x80},
	{0x3dab, 0x14},
	{0x3dac, 0x80},
	{0x3dad, 0xc8},
	{0x3dae, 0x81},
	{0x3daf, 0x7b},
	{0x3f00, 0x10},
	{0x3f01, 0x11},
	{0x3f06, 0x0d},
	{0x3f07, 0x0b},
	{0x3f08, 0x0d},
	{0x3f09, 0x0b},
	{0x3f0a, 0x01},
	{0x3f0b, 0x11},
	{0x3f0c, 0x33},
	{0x4001, 0x07},
	{0x4007, 0x20},
	{0x4008, 0x00},
	{0x4009, 0x05},
	{0x400a, 0x00},
	{0x400b, 0x04},
	{0x400c, 0x00},
	{0x400d, 0x04},
	{0x400e, 0x14},
	{0x4010, 0xf4},
	{0x4011, 0x03},
	{0x4012, 0x55},
	{0x4015, 0x00},
	{0x4016, 0x27},
	{0x4017, 0x00},
	{0x4018, 0x0f},
	{0x401b, 0x08},
	{0x401c, 0x00},
	{0x401d, 0x10},
	{0x401e, 0x02},
	{0x401f, 0x00},
	{0x4050, 0x06},
	{0x4051, 0xff},
	{0x4052, 0xff},
	{0x4053, 0xff},
	{0x4054, 0xff},
	{0x4055, 0xff},
	{0x4056, 0xff},
	{0x4057, 0x7f},
	{0x4058, 0x00},
	{0x4059, 0x00},
	{0x405a, 0x00},
	{0x405b, 0x00},
	{0x405c, 0x07},
	{0x405d, 0xff},
	{0x405e, 0x07},
	{0x405f, 0xff},
	{0x4080, 0x78},
	{0x4081, 0x78},
	{0x4082, 0x78},
	{0x4083, 0x78},
	{0x4019, 0x00},
	{0x401a, 0x40},
	{0x4020, 0x04},
	{0x4021, 0x00},
	{0x4022, 0x04},
	{0x4023, 0x00},
	{0x4024, 0x04},
	{0x4025, 0x00},
	{0x4026, 0x04},
	{0x4027, 0x00},
	{0x4030, 0x00},
	{0x4031, 0x00},
	{0x4032, 0x00},
	{0x4033, 0x00},
	{0x4034, 0x00},
	{0x4035, 0x00},
	{0x4036, 0x00},
	{0x4037, 0x00},
	{0x4040, 0x00},
	{0x4041, 0x80},
	{0x4042, 0x00},
	{0x4043, 0x80},
	{0x4044, 0x00},
	{0x4045, 0x80},
	{0x4046, 0x00},
	{0x4047, 0x80},
	{0x4060, 0x00},
	{0x4061, 0x00},
	{0x4062, 0x00},
	{0x4063, 0x00},
	{0x4064, 0x00},
	{0x4065, 0x00},
	{0x4066, 0x00},
	{0x4067, 0x00},
	{0x4068, 0x00},
	{0x4069, 0x00},
	{0x406a, 0x00},
	{0x406b, 0x00},
	{0x406c, 0x00},
	{0x406d, 0x00},
	{0x406e, 0x00},
	{0x406f, 0x00},
	{0x4070, 0x00},
	{0x4071, 0x00},
	{0x4072, 0x00},
	{0x4073, 0x00},
	{0x4074, 0x00},
	{0x4075, 0x00},
	{0x4076, 0x00},
	{0x4077, 0x00},
	{0x4078, 0x00},
	{0x4079, 0x00},
	{0x407a, 0x00},
	{0x407b, 0x00},
	{0x407c, 0x00},
	{0x407d, 0x00},
	{0x407e, 0x00},
	{0x407f, 0x00},
	{0x40e0, 0x00},
	{0x40e1, 0x00},
	{0x40e2, 0x00},
	{0x40e3, 0x00},
	{0x40e4, 0x00},
	{0x40e5, 0x00},
	{0x40e6, 0x00},
	{0x40e7, 0x00},
	{0x40e8, 0x00},
	{0x40e9, 0x80},
	{0x40ea, 0x00},
	{0x40eb, 0x80},
	{0x40ec, 0x00},
	{0x40ed, 0x80},
	{0x40ee, 0x00},
	{0x40ef, 0x80},
	{0x40f0, 0x02},
	{0x40f1, 0x04},
	{0x4300, 0x00},
	{0x4301, 0x00},
	{0x4302, 0x00},
	{0x4303, 0x00},
	{0x4304, 0x00},
	{0x4305, 0x00},
	{0x4306, 0x00},
	{0x4307, 0x00},
	{0x4308, 0x00},
	{0x4309, 0x00},
	{0x430a, 0x00},
	{0x430b, 0xff},
	{0x430c, 0xff},
	{0x430d, 0x00},
	{0x430e, 0x00},
	{0x4315, 0x00},
	{0x4316, 0x00},
	{0x4317, 0x00},
	{0x4318, 0x00},
	{0x4319, 0x00},
	{0x431a, 0x00},
	{0x431b, 0x00},
	{0x431c, 0x00},
	{0x4500, 0x07},
	{0x4501, 0x10},
	{0x4502, 0x00},
	{0x4503, 0x0f},
	{0x4504, 0x80},
	{0x4506, 0x01},
	{0x4509, 0x05},
	{0x450c, 0x00},
	{0x450d, 0x20},
	{0x450e, 0x00},
	{0x450f, 0x00},
	{0x4510, 0x00},
	{0x4523, 0x00},
	{0x4526, 0x00},
	{0x4542, 0x00},
	{0x4543, 0x00},
	{0x4544, 0x00},
	{0x4545, 0x00},
	{0x4546, 0x00},
	{0x4547, 0x10},
	{0x4602, 0x00},
	{0x4603, 0x15},
	{0x460b, 0x07},
	{0x4680, 0x11},
	{0x4686, 0x00},
	{0x4687, 0x00},
	{0x4700, 0x00},
	{0x4800, 0x64},
	{0x4806, 0x40},
	{0x480b, 0x10},
	{0x480c, 0x80},
	{0x480f, 0x32},
	{0x4813, 0xe4},
	{0x4837, 0x14},
	{0x4850, 0x42},
	{0x4884, 0x04},
	{0x4c00, 0xf8},
	{0x4c01, 0x44},
	{0x4c03, 0x00},
	{0x4d00, 0x00},
	{0x4d01, 0x16},
	{0x4d04, 0x10},
	{0x4d05, 0x00},
	{0x4d06, 0x0c},
	{0x4d07, 0x00},
	{0x3d84, 0x04},
	{0x3680, 0xa4},
	{0x3682, 0x80},
	{0x3601, 0x40},
	{0x3602, 0x90},
	{0x3608, 0x0a},
	{0x3938, 0x09},
	{0x3a74, 0x84},
	{0x3a99, 0x84},
	{0x3ab9, 0xa6},
	{0x3aba, 0xba},
	{0x3b12, 0x84},
	{0x3b14, 0xbb},
	{0x3b15, 0xbf},
	{0x3a29, 0x26},
	{0x3a1f, 0x8a},
	{0x3a22, 0x91},
	{0x3a25, 0x96},
	{0x3a28, 0xb4},
	{0x3a2b, 0xba},
	{0x3a2e, 0xbf},
	{0x3a31, 0xc1},
	{0x3a20, 0x05},
	{0x3939, 0x6b},
	{0x3902, 0x10},
	{0x3903, 0x10},
	{0x3904, 0x10},
	{0x3905, 0x10},
	{0x3906, 0x01},
	{0x3907, 0x0b},
	{0x3908, 0x10},
	{0x3909, 0x13},
	{0x360f, 0x99},
	{0x390b, 0x11},
	{0x390c, 0x21},
	{0x390d, 0x32},
	{0x390e, 0x76},
	{0x3911, 0x90},
	{0x3913, 0x90},
	{0x3b3f, 0x9d},
	{0x3b45, 0x9d},
	{0x3b1b, 0xc9},
	{0x3b21, 0xc9},
	{0x3a1a, 0x1c},
	{0x3a23, 0x15},
	{0x3a26, 0x17},
	{0x3a2c, 0x50},
	{0x3a2f, 0x18},
	{0x3a32, 0x4f},
	{0x3ace, 0x01},
	{0x3ad2, 0x01},
	{0x3ad6, 0x01},
	{0x3ada, 0x01},
	{0x3ade, 0x01},
	{0x3ae2, 0x01},
	{0x3aee, 0x01},
	{0x3af2, 0x01},
	{0x3af6, 0x01},
	{0x3afa, 0x01},
	{0x3afe, 0x01},
	{0x3b02, 0x01},
	{0x3b06, 0x01},
	{0x3b0a, 0x01},
	{0x3b0b, 0x00},
	{0x3b0e, 0x01},
	{0x3b0f, 0x00},
	{0x392c, 0x02},
	{0x392d, 0x01},
	{0x392e, 0x04},
	{0x392f, 0x03},
	{0x3930, 0x09},
	{0x3931, 0x07},
	{0x3932, 0x10},
	{0x3933, 0x0d},
	{0x3609, 0x08},
	{0x3921, 0x0f},
	{0x3928, 0x15},
	{0x3929, 0x2a},
	{0x392a, 0x52},
	{0x392b, 0xa3},
	{0x340b, 0x1b},
	{0x3426, 0x10},
	{0x3407, 0x01},
	{0x3404, 0x01},
	{0x3500, 0x00},
	{0x3501, 0x08},
	{0x3502, 0x10},
	{0x3508, 0x04},
	{0x3509, 0x00},
};

static const char * const ov08x40_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Configurations for supported link frequencies */
#define OV08X40_LINK_FREQ_400MHZ	400000000ULL
#define OV08X40_SCLK_96MHZ		96000000ULL
#define OV08X40_EXT_CLK			19200000
#define OV08X40_DATA_LANES		4

/*
 * pixel_rate = link_freq * data-rate * nr_of_lanes / bits_per_sample
 * data rate => double data rate; number of lanes => 4; bits per pixel => 10
 */
static u64 link_freq_to_pixel_rate(u64 f)
{
	f *= 2 * OV08X40_DATA_LANES;
	do_div(f, 10);

	return f;
}

/* Menu items for LINK_FREQ V4L2 control */
static const s64 link_freq_menu_items[] = {
	OV08X40_LINK_FREQ_400MHZ,
};

/* Link frequency configs */
static const struct ov08x40_link_freq_config link_freq_configs[] = {
	[OV08X40_LINK_FREQ_400MHZ_INDEX] = {
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_800mbps),
			.regs = mipi_data_rate_800mbps,
		}
	},
};

/* Mode configs */
static const struct ov08x40_mode supported_modes[] = {
	{
		.width = 3856,
		.height = 2416,
		.vts_def = OV08X40_VTS_30FPS,
		.vts_min = OV08X40_VTS_30FPS,
		.llp = 0x10aa, /* in normal mode, tline time = 2 * HTS / SCLK */
		.lanes = 4,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_3856x2416_regs),
			.regs = mode_3856x2416_regs,
		},
		.link_freq_index = OV08X40_LINK_FREQ_400MHZ_INDEX,
		.exposure_shift = 1,
		.exposure_margin = OV08X40_EXPOSURE_MAX_MARGIN,
	},
	{
		.width = 1928,
		.height = 1208,
		.vts_def = OV08X40_VTS_BIN_30FPS,
		.vts_min = OV08X40_VTS_BIN_30FPS,
		.llp = 0x960,
		.lanes = 4,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1928x1208_regs),
			.regs = mode_1928x1208_regs,
		},
		.link_freq_index = OV08X40_LINK_FREQ_400MHZ_INDEX,
		.exposure_shift = 0,
		.exposure_margin = OV08X40_EXPOSURE_BIN_MAX_MARGIN,
	},
};

struct ov08x40 {
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
	const struct ov08x40_mode *cur_mode;

	/* Mutex for serialized access */
	struct mutex mutex;

	/* True if the device has been identified */
	bool identified;
};

#define to_ov08x40(_sd)	container_of(_sd, struct ov08x40, sd)

/* Read registers up to 4 at a time */
static int ov08x40_read_reg(struct ov08x40 *ov08x,
			    u16 reg, u32 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08x->sd);
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

static int __ov08x40_burst_fill_regs(struct i2c_client *client, u16 first_reg,
				     u16 last_reg, size_t num_regs, u8 val)
{
	struct i2c_msg msgs;
	size_t i;
	int ret;

	msgs.addr = client->addr;
	msgs.flags = 0;
	msgs.len = 2 + num_regs;
	msgs.buf = kmalloc(msgs.len, GFP_KERNEL);

	if (!msgs.buf)
		return -ENOMEM;

	put_unaligned_be16(first_reg, msgs.buf);

	for (i = 0; i < num_regs; ++i)
		msgs.buf[2 + i] = val;

	ret = i2c_transfer(client->adapter, &msgs, 1);

	kfree(msgs.buf);

	if (ret != 1) {
		dev_err(&client->dev, "Failed regs transferred: %d\n", ret);
		return -EIO;
	}

	return 0;
}

static int ov08x40_burst_fill_regs(struct ov08x40 *ov08x, u16 first_reg,
				   u16 last_reg,  u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08x->sd);
	size_t num_regs, num_write_regs;
	int ret;

	num_regs = last_reg - first_reg + 1;
	num_write_regs = num_regs;

	if (client->adapter->quirks && client->adapter->quirks->max_write_len)
		num_write_regs = client->adapter->quirks->max_write_len - 2;

	while (first_reg < last_reg) {
		ret = __ov08x40_burst_fill_regs(client, first_reg, last_reg,
						num_write_regs, val);
		if (ret)
			return ret;

		first_reg += num_write_regs;
	}

	return 0;
}

/* Write registers up to 4 at a time */
static int ov08x40_write_reg(struct ov08x40 *ov08x,
			     u16 reg, u32 len, u32 __val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08x->sd);
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
static int ov08x40_write_regs(struct ov08x40 *ov08x,
			      const struct ov08x40_reg *regs, u32 len)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08x->sd);
	int ret;
	u32 i;

	for (i = 0; i < len; i++) {
		ret = ov08x40_write_reg(ov08x, regs[i].address, 1,
					regs[i].val);

		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "Failed to write reg 0x%4.4x. error = %d\n",
					    regs[i].address, ret);

			return ret;
		}
	}

	return 0;
}

static int ov08x40_write_reg_list(struct ov08x40 *ov08x,
				  const struct ov08x40_reg_list *r_list)
{
	return ov08x40_write_regs(ov08x, r_list->regs, r_list->num_of_regs);
}

static int ov08x40_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	const struct ov08x40_mode *default_mode = &supported_modes[0];
	struct ov08x40 *ov08x = to_ov08x40(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_state_get_format(fh->state, 0);

	mutex_lock(&ov08x->mutex);

	/* Initialize try_fmt */
	try_fmt->width = default_mode->width;
	try_fmt->height = default_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	/* No crop or compose */
	mutex_unlock(&ov08x->mutex);

	return 0;
}

static int ov08x40_update_digital_gain(struct ov08x40 *ov08x, u32 d_gain)
{
	int ret;
	u32 val;

	/*
	 * 0x350C[1:0], 0x350B[7:0], 0x350A[4:0]
	 */

	val = (d_gain & OV08X40_DGTL_GAIN_L_MASK) << OV08X40_DGTL_GAIN_L_SHIFT;
	ret = ov08x40_write_reg(ov08x, OV08X40_REG_DGTL_GAIN_L,
				OV08X40_REG_VALUE_08BIT, val);
	if (ret)
		return ret;

	val = (d_gain >> OV08X40_DGTL_GAIN_M_SHIFT) & OV08X40_DGTL_GAIN_M_MASK;
	ret = ov08x40_write_reg(ov08x, OV08X40_REG_DGTL_GAIN_M,
				OV08X40_REG_VALUE_08BIT, val);
	if (ret)
		return ret;

	val = (d_gain >> OV08X40_DGTL_GAIN_H_SHIFT) & OV08X40_DGTL_GAIN_H_MASK;

	return ov08x40_write_reg(ov08x, OV08X40_REG_DGTL_GAIN_H,
				 OV08X40_REG_VALUE_08BIT, val);
}

static int ov08x40_enable_test_pattern(struct ov08x40 *ov08x, u32 pattern)
{
	int ret;
	u32 val;

	ret = ov08x40_read_reg(ov08x, OV08X40_REG_TEST_PATTERN,
			       OV08X40_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	if (pattern) {
		ret = ov08x40_read_reg(ov08x, OV08X40_REG_ISP,
				       OV08X40_REG_VALUE_08BIT, &val);
		if (ret)
			return ret;

		ret = ov08x40_write_reg(ov08x, OV08X40_REG_ISP,
					OV08X40_REG_VALUE_08BIT,
					val | BIT(1));
		if (ret)
			return ret;

		ret = ov08x40_read_reg(ov08x, OV08X40_REG_SHORT_TEST_PATTERN,
				       OV08X40_REG_VALUE_08BIT, &val);
		if (ret)
			return ret;

		ret = ov08x40_write_reg(ov08x, OV08X40_REG_SHORT_TEST_PATTERN,
					OV08X40_REG_VALUE_08BIT,
					val | BIT(0));
		if (ret)
			return ret;

		ret = ov08x40_read_reg(ov08x, OV08X40_REG_TEST_PATTERN,
				       OV08X40_REG_VALUE_08BIT, &val);
		if (ret)
			return ret;

		val &= OV08X40_TEST_PATTERN_MASK;
		val |= ((pattern - 1) << OV08X40_TEST_PATTERN_BAR_SHIFT) |
			OV08X40_TEST_PATTERN_ENABLE;
	} else {
		val &= ~OV08X40_TEST_PATTERN_ENABLE;
	}

	return ov08x40_write_reg(ov08x, OV08X40_REG_TEST_PATTERN,
				 OV08X40_REG_VALUE_08BIT, val);
}

static int ov08x40_set_ctrl_hflip(struct ov08x40 *ov08x, u32 ctrl_val)
{
	int ret;
	u32 val;

	ret = ov08x40_read_reg(ov08x, OV08X40_REG_MIRROR,
			       OV08X40_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	return ov08x40_write_reg(ov08x, OV08X40_REG_MIRROR,
				 OV08X40_REG_VALUE_08BIT,
				 ctrl_val ? val | BIT(2) : val & ~BIT(2));
}

static int ov08x40_set_ctrl_vflip(struct ov08x40 *ov08x, u32 ctrl_val)
{
	int ret;
	u32 val;

	ret = ov08x40_read_reg(ov08x, OV08X40_REG_VFLIP,
			       OV08X40_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	return ov08x40_write_reg(ov08x, OV08X40_REG_VFLIP,
				 OV08X40_REG_VALUE_08BIT,
				 ctrl_val ? val | BIT(2) : val & ~BIT(2));
}

static int ov08x40_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov08x40 *ov08x = container_of(ctrl->handler,
					     struct ov08x40, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&ov08x->sd);
	s64 max;
	int exp;
	int fll;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		/*
		 * because in normal mode, 1 HTS = 0.5 tline
		 * fps = sclk / hts / vts
		 * so the vts value needs to be double
		 */
		max = ((ov08x->cur_mode->height + ctrl->val) <<
			ov08x->cur_mode->exposure_shift) -
			ov08x->cur_mode->exposure_margin;

		__v4l2_ctrl_modify_range(ov08x->exposure,
					 ov08x->exposure->minimum,
					 max, ov08x->exposure->step, max);
		break;
	}

	/*
	 * Applying V4L2 control value only happens
	 * when power is up for streaming
	 */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov08x40_write_reg(ov08x, OV08X40_REG_ANALOG_GAIN,
					OV08X40_REG_VALUE_16BIT,
					ctrl->val << 1);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = ov08x40_update_digital_gain(ov08x, ctrl->val);
		break;
	case V4L2_CID_EXPOSURE:
		exp = (ctrl->val << ov08x->cur_mode->exposure_shift) -
			ov08x->cur_mode->exposure_margin;

		ret = ov08x40_write_reg(ov08x, OV08X40_REG_EXPOSURE,
					OV08X40_REG_VALUE_24BIT,
					exp);
		break;
	case V4L2_CID_VBLANK:
		fll = ((ov08x->cur_mode->height + ctrl->val) <<
			   ov08x->cur_mode->exposure_shift);

		ret = ov08x40_write_reg(ov08x, OV08X40_REG_VTS,
					OV08X40_REG_VALUE_16BIT,
					fll);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov08x40_enable_test_pattern(ov08x, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ov08x40_set_ctrl_hflip(ov08x, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		ov08x40_set_ctrl_vflip(ov08x, ctrl->val);
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

static const struct v4l2_ctrl_ops ov08x40_ctrl_ops = {
	.s_ctrl = ov08x40_set_ctrl,
};

static int ov08x40_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	/* Only one bayer order(GRBG) is supported */
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int ov08x40_enum_frame_size(struct v4l2_subdev *sd,
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

static void ov08x40_update_pad_format(const struct ov08x40_mode *mode,
				      struct v4l2_subdev_format *fmt)
{
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.code = MEDIA_BUS_FMT_SGRBG10_1X10;
	fmt->format.field = V4L2_FIELD_NONE;
}

static int ov08x40_do_get_pad_format(struct ov08x40 *ov08x,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		fmt->format = *framefmt;
	} else {
		ov08x40_update_pad_format(ov08x->cur_mode, fmt);
	}

	return 0;
}

static int ov08x40_get_pad_format(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_format *fmt)
{
	struct ov08x40 *ov08x = to_ov08x40(sd);
	int ret;

	mutex_lock(&ov08x->mutex);
	ret = ov08x40_do_get_pad_format(ov08x, sd_state, fmt);
	mutex_unlock(&ov08x->mutex);

	return ret;
}

static int
ov08x40_set_pad_format(struct v4l2_subdev *sd,
		       struct v4l2_subdev_state *sd_state,
		       struct v4l2_subdev_format *fmt)
{
	struct ov08x40 *ov08x = to_ov08x40(sd);
	const struct ov08x40_mode *mode;
	struct v4l2_mbus_framefmt *framefmt;
	s32 vblank_def;
	s32 vblank_min;
	s64 h_blank;
	s64 pixel_rate;
	s64 link_freq;
	u64 steps;

	mutex_lock(&ov08x->mutex);

	/* Only one raw bayer(GRBG) order is supported */
	if (fmt->format.code != MEDIA_BUS_FMT_SGRBG10_1X10)
		fmt->format.code = MEDIA_BUS_FMT_SGRBG10_1X10;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width, fmt->format.height);
	ov08x40_update_pad_format(mode, fmt);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		framefmt = v4l2_subdev_state_get_format(sd_state, fmt->pad);
		*framefmt = fmt->format;
	} else {
		ov08x->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(ov08x->link_freq, mode->link_freq_index);
		link_freq = link_freq_menu_items[mode->link_freq_index];
		pixel_rate = link_freq_to_pixel_rate(link_freq);
		__v4l2_ctrl_s_ctrl_int64(ov08x->pixel_rate, pixel_rate);

		/* Update limits and set FPS to default */
		vblank_def = ov08x->cur_mode->vts_def -
			     ov08x->cur_mode->height;
		vblank_min = ov08x->cur_mode->vts_min -
			     ov08x->cur_mode->height;

		/*
		 * The frame length line should be aligned to a multiple of 4,
		 * as provided by the sensor vendor, in normal mode.
		 */
		steps = mode->exposure_shift == 1 ? 4 : 1;

		__v4l2_ctrl_modify_range(ov08x->vblank, vblank_min,
					 OV08X40_VTS_MAX
					 - ov08x->cur_mode->height,
					 steps,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(ov08x->vblank, vblank_def);

		h_blank = ov08x->cur_mode->llp - ov08x->cur_mode->width;

		__v4l2_ctrl_modify_range(ov08x->hblank, h_blank,
					 h_blank, 1, h_blank);
	}

	mutex_unlock(&ov08x->mutex);

	return 0;
}

static int ov08x40_start_streaming(struct ov08x40 *ov08x)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08x->sd);
	const struct ov08x40_reg_list *reg_list;
	int ret, link_freq_index;

	/* Get out of from software reset */
	ret = ov08x40_write_reg(ov08x, OV08X40_REG_SOFTWARE_RST,
				OV08X40_REG_VALUE_08BIT, OV08X40_SOFTWARE_RST);
	if (ret) {
		dev_err(&client->dev, "%s failed to set powerup registers\n",
			__func__);
		return ret;
	}

	link_freq_index = ov08x->cur_mode->link_freq_index;
	reg_list = &link_freq_configs[link_freq_index].reg_list;

	ret = ov08x40_write_reg_list(ov08x, reg_list);
	if (ret) {
		dev_err(&client->dev, "%s failed to set plls\n", __func__);
		return ret;
	}

	/* Apply default values of current mode */
	reg_list = &ov08x->cur_mode->reg_list;
	ret = ov08x40_write_reg_list(ov08x, reg_list);
	if (ret) {
		dev_err(&client->dev, "%s failed to set mode\n", __func__);
		return ret;
	}

	/* Use i2c burst to write register on full size registers */
	if (ov08x->cur_mode->exposure_shift == 1) {
		ret = ov08x40_burst_fill_regs(ov08x, OV08X40_REG_XTALK_FIRST_A,
					      OV08X40_REG_XTALK_LAST_A, 0x75);
		if (ret == 0)
			ret = ov08x40_burst_fill_regs(ov08x,
						      OV08X40_REG_XTALK_FIRST_B,
						      OV08X40_REG_XTALK_LAST_B,
						      0x75);
	}

	if (ret) {
		dev_err(&client->dev, "%s failed to set regs\n", __func__);
		return ret;
	}

	/* Apply customized values from user */
	ret =  __v4l2_ctrl_handler_setup(ov08x->sd.ctrl_handler);
	if (ret)
		return ret;

	return ov08x40_write_reg(ov08x, OV08X40_REG_MODE_SELECT,
				 OV08X40_REG_VALUE_08BIT,
				 OV08X40_MODE_STREAMING);
}

/* Stop streaming */
static int ov08x40_stop_streaming(struct ov08x40 *ov08x)
{
	return ov08x40_write_reg(ov08x, OV08X40_REG_MODE_SELECT,
				 OV08X40_REG_VALUE_08BIT, OV08X40_MODE_STANDBY);
}

static int ov08x40_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov08x40 *ov08x = to_ov08x40(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&ov08x->mutex);

	if (enable) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			goto err_unlock;

		/*
		 * Apply default & customized values
		 * and then start streaming.
		 */
		ret = ov08x40_start_streaming(ov08x);
		if (ret)
			goto err_rpm_put;
	} else {
		ov08x40_stop_streaming(ov08x);
		pm_runtime_put(&client->dev);
	}

	mutex_unlock(&ov08x->mutex);

	return ret;

err_rpm_put:
	pm_runtime_put(&client->dev);
err_unlock:
	mutex_unlock(&ov08x->mutex);

	return ret;
}

/* Verify chip ID */
static int ov08x40_identify_module(struct ov08x40 *ov08x)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08x->sd);
	int ret;
	u32 val;

	if (ov08x->identified)
		return 0;

	ret = ov08x40_read_reg(ov08x, OV08X40_REG_CHIP_ID,
			       OV08X40_REG_VALUE_24BIT, &val);
	if (ret)
		return ret;

	if (val != OV08X40_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x\n",
			OV08X40_CHIP_ID, val);
		return -ENXIO;
	}

	ov08x->identified = true;

	return 0;
}

static const struct v4l2_subdev_video_ops ov08x40_video_ops = {
	.s_stream = ov08x40_set_stream,
};

static const struct v4l2_subdev_pad_ops ov08x40_pad_ops = {
	.enum_mbus_code = ov08x40_enum_mbus_code,
	.get_fmt = ov08x40_get_pad_format,
	.set_fmt = ov08x40_set_pad_format,
	.enum_frame_size = ov08x40_enum_frame_size,
};

static const struct v4l2_subdev_ops ov08x40_subdev_ops = {
	.video = &ov08x40_video_ops,
	.pad = &ov08x40_pad_ops,
};

static const struct media_entity_operations ov08x40_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops ov08x40_internal_ops = {
	.open = ov08x40_open,
};

static int ov08x40_init_controls(struct ov08x40 *ov08x)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08x->sd);
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 exposure_max;
	s64 vblank_def;
	s64 vblank_min;
	s64 hblank;
	s64 pixel_rate_min;
	s64 pixel_rate_max;
	const struct ov08x40_mode *mode;
	u32 max;
	int ret;

	ctrl_hdlr = &ov08x->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 10);
	if (ret)
		return ret;

	mutex_init(&ov08x->mutex);
	ctrl_hdlr->lock = &ov08x->mutex;
	max = ARRAY_SIZE(link_freq_menu_items) - 1;
	ov08x->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr,
						  &ov08x40_ctrl_ops,
						  V4L2_CID_LINK_FREQ,
						  max,
						  0,
						  link_freq_menu_items);
	if (ov08x->link_freq)
		ov08x->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate_max = link_freq_to_pixel_rate(link_freq_menu_items[0]);
	pixel_rate_min = 0;
	/* By default, PIXEL_RATE is read only */
	ov08x->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ov08x40_ctrl_ops,
					      V4L2_CID_PIXEL_RATE,
					      pixel_rate_min, pixel_rate_max,
					      1, pixel_rate_max);

	mode = ov08x->cur_mode;
	vblank_def = mode->vts_def - mode->height;
	vblank_min = mode->vts_min - mode->height;
	ov08x->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov08x40_ctrl_ops,
					  V4L2_CID_VBLANK,
					  vblank_min,
					  OV08X40_VTS_MAX - mode->height, 1,
					  vblank_def);

	hblank = ov08x->cur_mode->llp - ov08x->cur_mode->width;

	ov08x->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov08x40_ctrl_ops,
					  V4L2_CID_HBLANK,
					  hblank, hblank, 1, hblank);
	if (ov08x->hblank)
		ov08x->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	exposure_max = mode->vts_def - OV08X40_EXPOSURE_MAX_MARGIN;
	ov08x->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ov08x40_ctrl_ops,
					    V4L2_CID_EXPOSURE,
					    OV08X40_EXPOSURE_MIN,
					    exposure_max, OV08X40_EXPOSURE_STEP,
					    exposure_max);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov08x40_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV08X40_ANA_GAIN_MIN, OV08X40_ANA_GAIN_MAX,
			  OV08X40_ANA_GAIN_STEP, OV08X40_ANA_GAIN_DEFAULT);

	/* Digital gain */
	v4l2_ctrl_new_std(ctrl_hdlr, &ov08x40_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV08X40_DGTL_GAIN_MIN, OV08X40_DGTL_GAIN_MAX,
			  OV08X40_DGTL_GAIN_STEP, OV08X40_DGTL_GAIN_DEFAULT);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov08x40_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov08x40_test_pattern_menu) - 1,
				     0, 0, ov08x40_test_pattern_menu);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov08x40_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov08x40_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		dev_err(&client->dev, "%s control init failed (%d)\n",
			__func__, ret);
		goto error;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto error;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &ov08x40_ctrl_ops,
					      &props);
	if (ret)
		goto error;

	ov08x->sd.ctrl_handler = ctrl_hdlr;

	return 0;

error:
	v4l2_ctrl_handler_free(ctrl_hdlr);
	mutex_destroy(&ov08x->mutex);

	return ret;
}

static void ov08x40_free_controls(struct ov08x40 *ov08x)
{
	v4l2_ctrl_handler_free(ov08x->sd.ctrl_handler);
	mutex_destroy(&ov08x->mutex);
}

static int ov08x40_check_hwcfg(struct device *dev)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct fwnode_handle *ep;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	unsigned int i, j;
	int ret;
	u32 ext_clk;

	if (!fwnode)
		return -ENXIO;

	ret = fwnode_property_read_u32(dev_fwnode(dev), "clock-frequency",
				       &ext_clk);
	if (ret) {
		dev_err(dev, "can't get clock frequency");
		return ret;
	}

	if (ext_clk != OV08X40_EXT_CLK) {
		dev_err(dev, "external clock %d is not supported",
			ext_clk);
		return -EINVAL;
	}

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != OV08X40_DATA_LANES) {
		dev_err(dev, "number of CSI2 data lanes %d is not supported",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto out_err;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(dev, "no link frequencies defined");
		ret = -EINVAL;
		goto out_err;
	}

	for (i = 0; i < ARRAY_SIZE(link_freq_menu_items); i++) {
		for (j = 0; j < bus_cfg.nr_of_link_frequencies; j++) {
			if (link_freq_menu_items[i] ==
				bus_cfg.link_frequencies[j])
				break;
		}

		if (j == bus_cfg.nr_of_link_frequencies) {
			dev_err(dev, "no link frequency %lld supported",
				link_freq_menu_items[i]);
			ret = -EINVAL;
			goto out_err;
		}
	}

out_err:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int ov08x40_probe(struct i2c_client *client)
{
	struct ov08x40 *ov08x;
	int ret;
	bool full_power;

	/* Check HW config */
	ret = ov08x40_check_hwcfg(&client->dev);
	if (ret) {
		dev_err(&client->dev, "failed to check hwcfg: %d", ret);
		return ret;
	}

	ov08x = devm_kzalloc(&client->dev, sizeof(*ov08x), GFP_KERNEL);
	if (!ov08x)
		return -ENOMEM;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&ov08x->sd, client, &ov08x40_subdev_ops);

	full_power = acpi_dev_state_d0(&client->dev);
	if (full_power) {
		/* Check module identity */
		ret = ov08x40_identify_module(ov08x);
		if (ret) {
			dev_err(&client->dev, "failed to find sensor: %d\n", ret);
			return ret;
		}
	}

	/* Set default mode to max resolution */
	ov08x->cur_mode = &supported_modes[0];

	ret = ov08x40_init_controls(ov08x);
	if (ret)
		return ret;

	/* Initialize subdev */
	ov08x->sd.internal_ops = &ov08x40_internal_ops;
	ov08x->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov08x->sd.entity.ops = &ov08x40_subdev_entity_ops;
	ov08x->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	/* Initialize source pad */
	ov08x->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&ov08x->sd.entity, 1, &ov08x->pad);
	if (ret) {
		dev_err(&client->dev, "%s failed:%d\n", __func__, ret);
		goto error_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&ov08x->sd);
	if (ret < 0)
		goto error_media_entity;

	if (full_power)
		pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

error_media_entity:
	media_entity_cleanup(&ov08x->sd.entity);

error_handler_free:
	ov08x40_free_controls(ov08x);

	return ret;
}

static void ov08x40_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov08x40 *ov08x = to_ov08x40(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	ov08x40_free_controls(ov08x);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id ov08x40_acpi_ids[] = {
	{"OVTI08F4"},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(acpi, ov08x40_acpi_ids);
#endif

static struct i2c_driver ov08x40_i2c_driver = {
	.driver = {
		.name = "ov08x40",
		.acpi_match_table = ACPI_PTR(ov08x40_acpi_ids),
	},
	.probe = ov08x40_probe,
	.remove = ov08x40_remove,
	.flags = I2C_DRV_ACPI_WAIVE_D0_PROBE,
};

module_i2c_driver(ov08x40_i2c_driver);

MODULE_AUTHOR("Jason Chen <jason.z.chen@intel.com>");
MODULE_AUTHOR("Qingwu Zhang <qingwu.zhang@intel.com>");
MODULE_AUTHOR("Shawn Tu");
MODULE_DESCRIPTION("OmniVision OV08X40 sensor driver");
MODULE_LICENSE("GPL");
