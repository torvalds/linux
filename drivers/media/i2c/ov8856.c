// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Intel Corporation.

#include <asm/unaligned.h>
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define OV8856_REG_VALUE_08BIT		1
#define OV8856_REG_VALUE_16BIT		2
#define OV8856_REG_VALUE_24BIT		3

#define OV8856_SCLK			144000000ULL
#define OV8856_XVCLK_19_2		19200000
#define OV8856_DATA_LANES		4
#define OV8856_RGB_DEPTH		10

#define OV8856_REG_CHIP_ID		0x300a
#define OV8856_CHIP_ID			0x00885a

#define OV8856_REG_MODE_SELECT		0x0100
#define OV8856_MODE_STANDBY		0x00
#define OV8856_MODE_STREAMING		0x01

/* module revisions */
#define OV8856_2A_MODULE		0x01
#define OV8856_1B_MODULE		0x02

/* the OTP read-out buffer is at 0x7000 and 0xf is the offset
 * of the byte in the OTP that means the module revision
 */
#define OV8856_MODULE_REVISION		0x700f
#define OV8856_OTP_MODE_CTRL		0x3d84
#define OV8856_OTP_LOAD_CTRL		0x3d81
#define OV8856_OTP_MODE_AUTO		0x00
#define OV8856_OTP_LOAD_CTRL_ENABLE	BIT(0)

/* vertical-timings from sensor */
#define OV8856_REG_VTS			0x380e
#define OV8856_VTS_MAX			0x7fff

/* horizontal-timings from sensor */
#define OV8856_REG_HTS			0x380c

/* Exposure controls from sensor */
#define OV8856_REG_EXPOSURE		0x3500
#define	OV8856_EXPOSURE_MIN		6
#define OV8856_EXPOSURE_MAX_MARGIN	6
#define	OV8856_EXPOSURE_STEP		1

/* Analog gain controls from sensor */
#define OV8856_REG_ANALOG_GAIN		0x3508
#define	OV8856_ANAL_GAIN_MIN		128
#define	OV8856_ANAL_GAIN_MAX		2047
#define	OV8856_ANAL_GAIN_STEP		1

/* Digital gain controls from sensor */
#define OV8856_REG_DIGITAL_GAIN		0x350a
#define OV8856_REG_MWB_R_GAIN		0x5019
#define OV8856_REG_MWB_G_GAIN		0x501b
#define OV8856_REG_MWB_B_GAIN		0x501d
#define OV8856_DGTL_GAIN_MIN		0
#define OV8856_DGTL_GAIN_MAX		4095
#define OV8856_DGTL_GAIN_STEP		1
#define OV8856_DGTL_GAIN_DEFAULT	1024

/* Test Pattern Control */
#define OV8856_REG_TEST_PATTERN		0x5e00
#define OV8856_TEST_PATTERN_ENABLE	BIT(7)
#define OV8856_TEST_PATTERN_BAR_SHIFT	2

#define NUM_REGS				7
#define NUM_MODE_REGS				187
#define NUM_MODE_REGS_2				200

/* Flip Mirror Controls from sensor */
#define OV8856_REG_FORMAT1			0x3820
#define OV8856_REG_FORMAT2			0x3821
#define OV8856_REG_FORMAT1_OP_1			BIT(1)
#define OV8856_REG_FORMAT1_OP_2			BIT(2)
#define OV8856_REG_FORMAT1_OP_3			BIT(6)
#define OV8856_REG_FORMAT2_OP_1			BIT(1)
#define OV8856_REG_FORMAT2_OP_2			BIT(2)
#define OV8856_REG_FORMAT2_OP_3			BIT(6)
#define OV8856_REG_FLIP_OPT_1			0x376b
#define OV8856_REG_FLIP_OPT_2			0x5001
#define OV8856_REG_FLIP_OPT_3			0x502e
#define OV8856_REG_MIRROR_OPT_1			0x5004
#define OV8856_REG_FLIP_OP_0			BIT(0)
#define OV8856_REG_FLIP_OP_1			BIT(1)
#define OV8856_REG_FLIP_OP_2			BIT(2)
#define OV8856_REG_MIRROR_OP_1			BIT(1)
#define OV8856_REG_MIRROR_OP_2			BIT(2)

#define to_ov8856(_sd)			container_of(_sd, struct ov8856, sd)

static const char * const ov8856_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
	"dvdd",		/* Digital core power */
};

enum {
	OV8856_MEDIA_BUS_FMT_SBGGR10_1X10,
	OV8856_MEDIA_BUS_FMT_SGRBG10_1X10,
};

struct ov8856_reg {
	u16 address;
	u8 val;
};

struct ov8856_reg_list {
	u32 num_of_regs;
	const struct ov8856_reg *regs;
};

struct ov8856_link_freq_config {
	const struct ov8856_reg_list reg_list;
};

struct ov8856_mode {
	/* Frame width in pixels */
	u32 width;

	/* Frame height in pixels */
	u32 height;

	/* Horizontal timining size */
	u32 hts;

	/* Default vertical timining size */
	u32 vts_def;

	/* Min vertical timining size */
	u32 vts_min;

	/* Link frequency needed for this resolution */
	u32 link_freq_index;

	/* Sensor register settings for this resolution */
	const struct ov8856_reg_list reg_list;

	/* Number of data lanes */
	u8 data_lanes;

	/* Default MEDIA_BUS_FMT for this mode */
	u32 default_mbus_index;
};

struct ov8856_mipi_data_rates {
	const struct ov8856_reg regs_0[NUM_REGS];
	const struct ov8856_reg regs_1[NUM_REGS];
};

static const struct ov8856_mipi_data_rates mipi_data_rate_lane_2 = {
	//mipi_data_rate_1440mbps
	{
		{0x0103, 0x01},
		{0x0100, 0x00},
		{0x0302, 0x43},
		{0x0303, 0x00},
		{0x030b, 0x02},
		{0x030d, 0x4b},
		{0x031e, 0x0c}
	},
	//mipi_data_rate_720mbps
	{
		{0x0103, 0x01},
		{0x0100, 0x00},
		{0x0302, 0x4b},
		{0x0303, 0x01},
		{0x030b, 0x02},
		{0x030d, 0x4b},
		{0x031e, 0x0c}
	}
};

static const struct ov8856_mipi_data_rates mipi_data_rate_lane_4 = {
	//mipi_data_rate_720mbps
	{
		{0x0103, 0x01},
		{0x0100, 0x00},
		{0x0302, 0x4b},
		{0x0303, 0x01},
		{0x030b, 0x02},
		{0x030d, 0x4b},
		{0x031e, 0x0c}
	},
	//mipi_data_rate_360mbps
	{
		{0x0103, 0x01},
		{0x0100, 0x00},
		{0x0302, 0x4b},
		{0x0303, 0x03},
		{0x030b, 0x02},
		{0x030d, 0x4b},
		{0x031e, 0x0c}
	}
};

static const struct ov8856_reg lane_2_mode_3280x2464[] = {
	/* 3280x2464 resolution */
		{0x3000, 0x20},
		{0x3003, 0x08},
		{0x300e, 0x20},
		{0x3010, 0x00},
		{0x3015, 0x84},
		{0x3018, 0x32},
		{0x3021, 0x23},
		{0x3033, 0x24},
		{0x3500, 0x00},
		{0x3501, 0x9a},
		{0x3502, 0x20},
		{0x3503, 0x08},
		{0x3505, 0x83},
		{0x3508, 0x01},
		{0x3509, 0x80},
		{0x350c, 0x00},
		{0x350d, 0x80},
		{0x350e, 0x04},
		{0x350f, 0x00},
		{0x3510, 0x00},
		{0x3511, 0x02},
		{0x3512, 0x00},
		{0x3600, 0x72},
		{0x3601, 0x40},
		{0x3602, 0x30},
		{0x3610, 0xc5},
		{0x3611, 0x58},
		{0x3612, 0x5c},
		{0x3613, 0xca},
		{0x3614, 0x50},
		{0x3628, 0xff},
		{0x3629, 0xff},
		{0x362a, 0xff},
		{0x3633, 0x10},
		{0x3634, 0x10},
		{0x3635, 0x10},
		{0x3636, 0x10},
		{0x3663, 0x08},
		{0x3669, 0x34},
		{0x366e, 0x10},
		{0x3706, 0x86},
		{0x370b, 0x7e},
		{0x3714, 0x23},
		{0x3730, 0x12},
		{0x3733, 0x10},
		{0x3764, 0x00},
		{0x3765, 0x00},
		{0x3769, 0x62},
		{0x376a, 0x2a},
		{0x376b, 0x30},
		{0x3780, 0x00},
		{0x3781, 0x24},
		{0x3782, 0x00},
		{0x3783, 0x23},
		{0x3798, 0x2f},
		{0x37a1, 0x60},
		{0x37a8, 0x6a},
		{0x37ab, 0x3f},
		{0x37c2, 0x04},
		{0x37c3, 0xf1},
		{0x37c9, 0x80},
		{0x37cb, 0x16},
		{0x37cc, 0x16},
		{0x37cd, 0x16},
		{0x37ce, 0x16},
		{0x3800, 0x00},
		{0x3801, 0x00},
		{0x3802, 0x00},
		{0x3803, 0x06},
		{0x3804, 0x0c},
		{0x3805, 0xdf},
		{0x3806, 0x09},
		{0x3807, 0xa7},
		{0x3808, 0x0c},
		{0x3809, 0xd0},
		{0x380a, 0x09},
		{0x380b, 0xa0},
		{0x380c, 0x07},
		{0x380d, 0x88},
		{0x380e, 0x09},
		{0x380f, 0xb8},
		{0x3810, 0x00},
		{0x3811, 0x00},
		{0x3812, 0x00},
		{0x3813, 0x01},
		{0x3814, 0x01},
		{0x3815, 0x01},
		{0x3816, 0x00},
		{0x3817, 0x00},
		{0x3818, 0x00},
		{0x3819, 0x00},
		{0x3820, 0x80},
		{0x3821, 0x46},
		{0x382a, 0x01},
		{0x382b, 0x01},
		{0x3830, 0x06},
		{0x3836, 0x02},
		{0x3837, 0x10},
		{0x3862, 0x04},
		{0x3863, 0x08},
		{0x3cc0, 0x33},
		{0x3d85, 0x14},
		{0x3d8c, 0x73},
		{0x3d8d, 0xde},
		{0x4001, 0xe0},
		{0x4003, 0x40},
		{0x4008, 0x00},
		{0x4009, 0x0b},
		{0x400a, 0x00},
		{0x400b, 0x84},
		{0x400f, 0x80},
		{0x4010, 0xf0},
		{0x4011, 0xff},
		{0x4012, 0x02},
		{0x4013, 0x01},
		{0x4014, 0x01},
		{0x4015, 0x01},
		{0x4042, 0x00},
		{0x4043, 0x80},
		{0x4044, 0x00},
		{0x4045, 0x80},
		{0x4046, 0x00},
		{0x4047, 0x80},
		{0x4048, 0x00},
		{0x4049, 0x80},
		{0x4041, 0x03},
		{0x404c, 0x20},
		{0x404d, 0x00},
		{0x404e, 0x20},
		{0x4203, 0x80},
		{0x4307, 0x30},
		{0x4317, 0x00},
		{0x4503, 0x08},
		{0x4601, 0x80},
		{0x4800, 0x44},
		{0x4816, 0x53},
		{0x481b, 0x58},
		{0x481f, 0x27},
		{0x4837, 0x0c},
		{0x483c, 0x0f},
		{0x484b, 0x05},
		{0x5000, 0x57},
		{0x5001, 0x0a},
		{0x5004, 0x06},
		{0x502e, 0x03},
		{0x5030, 0x41},
		{0x5795, 0x02},
		{0x5796, 0x20},
		{0x5797, 0x20},
		{0x5798, 0xd5},
		{0x5799, 0xd5},
		{0x579a, 0x00},
		{0x579b, 0x50},
		{0x579c, 0x00},
		{0x579d, 0x2c},
		{0x579e, 0x0c},
		{0x579f, 0x40},
		{0x57a0, 0x09},
		{0x57a1, 0x40},
		{0x5780, 0x14},
		{0x5781, 0x0f},
		{0x5782, 0x44},
		{0x5783, 0x02},
		{0x5784, 0x01},
		{0x5785, 0x01},
		{0x5786, 0x00},
		{0x5787, 0x04},
		{0x5788, 0x02},
		{0x5789, 0x0f},
		{0x578a, 0xfd},
		{0x578b, 0xf5},
		{0x578c, 0xf5},
		{0x578d, 0x03},
		{0x578e, 0x08},
		{0x578f, 0x0c},
		{0x5790, 0x08},
		{0x5791, 0x04},
		{0x5792, 0x00},
		{0x5793, 0x52},
		{0x5794, 0xa3},
		{0x59f8, 0x3d},
		{0x5a08, 0x02},
		{0x5b00, 0x02},
		{0x5b01, 0x10},
		{0x5b02, 0x03},
		{0x5b03, 0xcf},
		{0x5b05, 0x6c},
		{0x5e00, 0x00}
};

static const struct ov8856_reg lane_2_mode_1640x1232[] = {
	/* 1640x1232 resolution */
		{0x3000, 0x20},
		{0x3003, 0x08},
		{0x300e, 0x20},
		{0x3010, 0x00},
		{0x3015, 0x84},
		{0x3018, 0x32},
		{0x3021, 0x23},
		{0x3033, 0x24},
		{0x3500, 0x00},
		{0x3501, 0x4c},
		{0x3502, 0xe0},
		{0x3503, 0x08},
		{0x3505, 0x83},
		{0x3508, 0x01},
		{0x3509, 0x80},
		{0x350c, 0x00},
		{0x350d, 0x80},
		{0x350e, 0x04},
		{0x350f, 0x00},
		{0x3510, 0x00},
		{0x3511, 0x02},
		{0x3512, 0x00},
		{0x3600, 0x72},
		{0x3601, 0x40},
		{0x3602, 0x30},
		{0x3610, 0xc5},
		{0x3611, 0x58},
		{0x3612, 0x5c},
		{0x3613, 0xca},
		{0x3614, 0x50},
		{0x3628, 0xff},
		{0x3629, 0xff},
		{0x362a, 0xff},
		{0x3633, 0x10},
		{0x3634, 0x10},
		{0x3635, 0x10},
		{0x3636, 0x10},
		{0x3663, 0x08},
		{0x3669, 0x34},
		{0x366e, 0x08},
		{0x3706, 0x86},
		{0x370b, 0x7e},
		{0x3714, 0x27},
		{0x3730, 0x12},
		{0x3733, 0x10},
		{0x3764, 0x00},
		{0x3765, 0x00},
		{0x3769, 0x62},
		{0x376a, 0x2a},
		{0x376b, 0x30},
		{0x3780, 0x00},
		{0x3781, 0x24},
		{0x3782, 0x00},
		{0x3783, 0x23},
		{0x3798, 0x2f},
		{0x37a1, 0x60},
		{0x37a8, 0x6a},
		{0x37ab, 0x3f},
		{0x37c2, 0x14},
		{0x37c3, 0xf1},
		{0x37c9, 0x80},
		{0x37cb, 0x16},
		{0x37cc, 0x16},
		{0x37cd, 0x16},
		{0x37ce, 0x16},
		{0x3800, 0x00},
		{0x3801, 0x00},
		{0x3802, 0x00},
		{0x3803, 0x00},
		{0x3804, 0x0c},
		{0x3805, 0xdf},
		{0x3806, 0x09},
		{0x3807, 0xaf},
		{0x3808, 0x06},
		{0x3809, 0x68},
		{0x380a, 0x04},
		{0x380b, 0xd0},
		{0x380c, 0x0c},
		{0x380d, 0x60},
		{0x380e, 0x05},
		{0x380f, 0xea},
		{0x3810, 0x00},
		{0x3811, 0x04},
		{0x3812, 0x00},
		{0x3813, 0x05},
		{0x3814, 0x03},
		{0x3815, 0x01},
		{0x3816, 0x00},
		{0x3817, 0x00},
		{0x3818, 0x00},
		{0x3819, 0x00},
		{0x3820, 0x90},
		{0x3821, 0x67},
		{0x382a, 0x03},
		{0x382b, 0x01},
		{0x3830, 0x06},
		{0x3836, 0x02},
		{0x3837, 0x10},
		{0x3862, 0x04},
		{0x3863, 0x08},
		{0x3cc0, 0x33},
		{0x3d85, 0x14},
		{0x3d8c, 0x73},
		{0x3d8d, 0xde},
		{0x4001, 0xe0},
		{0x4003, 0x40},
		{0x4008, 0x00},
		{0x4009, 0x05},
		{0x400a, 0x00},
		{0x400b, 0x84},
		{0x400f, 0x80},
		{0x4010, 0xf0},
		{0x4011, 0xff},
		{0x4012, 0x02},
		{0x4013, 0x01},
		{0x4014, 0x01},
		{0x4015, 0x01},
		{0x4042, 0x00},
		{0x4043, 0x80},
		{0x4044, 0x00},
		{0x4045, 0x80},
		{0x4046, 0x00},
		{0x4047, 0x80},
		{0x4048, 0x00},
		{0x4049, 0x80},
		{0x4041, 0x03},
		{0x404c, 0x20},
		{0x404d, 0x00},
		{0x404e, 0x20},
		{0x4203, 0x80},
		{0x4307, 0x30},
		{0x4317, 0x00},
		{0x4503, 0x08},
		{0x4601, 0x80},
		{0x4800, 0x44},
		{0x4816, 0x53},
		{0x481b, 0x58},
		{0x481f, 0x27},
		{0x4837, 0x16},
		{0x483c, 0x0f},
		{0x484b, 0x05},
		{0x5000, 0x57},
		{0x5001, 0x0a},
		{0x5004, 0x06},
		{0x502e, 0x03},
		{0x5030, 0x41},
		{0x5795, 0x00},
		{0x5796, 0x10},
		{0x5797, 0x10},
		{0x5798, 0x73},
		{0x5799, 0x73},
		{0x579a, 0x00},
		{0x579b, 0x28},
		{0x579c, 0x00},
		{0x579d, 0x16},
		{0x579e, 0x06},
		{0x579f, 0x20},
		{0x57a0, 0x04},
		{0x57a1, 0xa0},
		{0x5780, 0x14},
		{0x5781, 0x0f},
		{0x5782, 0x44},
		{0x5783, 0x02},
		{0x5784, 0x01},
		{0x5785, 0x01},
		{0x5786, 0x00},
		{0x5787, 0x04},
		{0x5788, 0x02},
		{0x5789, 0x0f},
		{0x578a, 0xfd},
		{0x578b, 0xf5},
		{0x578c, 0xf5},
		{0x578d, 0x03},
		{0x578e, 0x08},
		{0x578f, 0x0c},
		{0x5790, 0x08},
		{0x5791, 0x04},
		{0x5792, 0x00},
		{0x5793, 0x52},
		{0x5794, 0xa3},
		{0x59f8, 0x3d},
		{0x5a08, 0x02},
		{0x5b00, 0x02},
		{0x5b01, 0x10},
		{0x5b02, 0x03},
		{0x5b03, 0xcf},
		{0x5b05, 0x6c},
		{0x5e00, 0x00}
};

static const struct ov8856_reg lane_4_mode_3280x2464[] = {
	/* 3280x2464 resolution */
		{0x3000, 0x20},
		{0x3003, 0x08},
		{0x300e, 0x20},
		{0x3010, 0x00},
		{0x3015, 0x84},
		{0x3018, 0x72},
		{0x3021, 0x23},
		{0x3033, 0x24},
		{0x3500, 0x00},
		{0x3501, 0x9a},
		{0x3502, 0x20},
		{0x3503, 0x08},
		{0x3505, 0x83},
		{0x3508, 0x01},
		{0x3509, 0x80},
		{0x350c, 0x00},
		{0x350d, 0x80},
		{0x350e, 0x04},
		{0x350f, 0x00},
		{0x3510, 0x00},
		{0x3511, 0x02},
		{0x3512, 0x00},
		{0x3600, 0x72},
		{0x3601, 0x40},
		{0x3602, 0x30},
		{0x3610, 0xc5},
		{0x3611, 0x58},
		{0x3612, 0x5c},
		{0x3613, 0xca},
		{0x3614, 0x20},
		{0x3628, 0xff},
		{0x3629, 0xff},
		{0x362a, 0xff},
		{0x3633, 0x10},
		{0x3634, 0x10},
		{0x3635, 0x10},
		{0x3636, 0x10},
		{0x3663, 0x08},
		{0x3669, 0x34},
		{0x366e, 0x10},
		{0x3706, 0x86},
		{0x370b, 0x7e},
		{0x3714, 0x23},
		{0x3730, 0x12},
		{0x3733, 0x10},
		{0x3764, 0x00},
		{0x3765, 0x00},
		{0x3769, 0x62},
		{0x376a, 0x2a},
		{0x376b, 0x30},
		{0x3780, 0x00},
		{0x3781, 0x24},
		{0x3782, 0x00},
		{0x3783, 0x23},
		{0x3798, 0x2f},
		{0x37a1, 0x60},
		{0x37a8, 0x6a},
		{0x37ab, 0x3f},
		{0x37c2, 0x04},
		{0x37c3, 0xf1},
		{0x37c9, 0x80},
		{0x37cb, 0x16},
		{0x37cc, 0x16},
		{0x37cd, 0x16},
		{0x37ce, 0x16},
		{0x3800, 0x00},
		{0x3801, 0x00},
		{0x3802, 0x00},
		{0x3803, 0x06},
		{0x3804, 0x0c},
		{0x3805, 0xdf},
		{0x3806, 0x09},
		{0x3807, 0xa7},
		{0x3808, 0x0c},
		{0x3809, 0xd0},
		{0x380a, 0x09},
		{0x380b, 0xa0},
		{0x380c, 0x07},
		{0x380d, 0x88},
		{0x380e, 0x09},
		{0x380f, 0xb8},
		{0x3810, 0x00},
		{0x3811, 0x00},
		{0x3812, 0x00},
		{0x3813, 0x01},
		{0x3814, 0x01},
		{0x3815, 0x01},
		{0x3816, 0x00},
		{0x3817, 0x00},
		{0x3818, 0x00},
		{0x3819, 0x10},
		{0x3820, 0x80},
		{0x3821, 0x46},
		{0x382a, 0x01},
		{0x382b, 0x01},
		{0x3830, 0x06},
		{0x3836, 0x02},
		{0x3862, 0x04},
		{0x3863, 0x08},
		{0x3cc0, 0x33},
		{0x3d85, 0x17},
		{0x3d8c, 0x73},
		{0x3d8d, 0xde},
		{0x4001, 0xe0},
		{0x4003, 0x40},
		{0x4008, 0x00},
		{0x4009, 0x0b},
		{0x400a, 0x00},
		{0x400b, 0x84},
		{0x400f, 0x80},
		{0x4010, 0xf0},
		{0x4011, 0xff},
		{0x4012, 0x02},
		{0x4013, 0x01},
		{0x4014, 0x01},
		{0x4015, 0x01},
		{0x4042, 0x00},
		{0x4043, 0x80},
		{0x4044, 0x00},
		{0x4045, 0x80},
		{0x4046, 0x00},
		{0x4047, 0x80},
		{0x4048, 0x00},
		{0x4049, 0x80},
		{0x4041, 0x03},
		{0x404c, 0x20},
		{0x404d, 0x00},
		{0x404e, 0x20},
		{0x4203, 0x80},
		{0x4307, 0x30},
		{0x4317, 0x00},
		{0x4503, 0x08},
		{0x4601, 0x80},
		{0x4800, 0x44},
		{0x4816, 0x53},
		{0x481b, 0x58},
		{0x481f, 0x27},
		{0x4837, 0x16},
		{0x483c, 0x0f},
		{0x484b, 0x05},
		{0x5000, 0x57},
		{0x5001, 0x0a},
		{0x5004, 0x06},
		{0x502e, 0x03},
		{0x5030, 0x41},
		{0x5780, 0x14},
		{0x5781, 0x0f},
		{0x5782, 0x44},
		{0x5783, 0x02},
		{0x5784, 0x01},
		{0x5785, 0x01},
		{0x5786, 0x00},
		{0x5787, 0x04},
		{0x5788, 0x02},
		{0x5789, 0x0f},
		{0x578a, 0xfd},
		{0x578b, 0xf5},
		{0x578c, 0xf5},
		{0x578d, 0x03},
		{0x578e, 0x08},
		{0x578f, 0x0c},
		{0x5790, 0x08},
		{0x5791, 0x04},
		{0x5792, 0x00},
		{0x5793, 0x52},
		{0x5794, 0xa3},
		{0x5795, 0x02},
		{0x5796, 0x20},
		{0x5797, 0x20},
		{0x5798, 0xd5},
		{0x5799, 0xd5},
		{0x579a, 0x00},
		{0x579b, 0x50},
		{0x579c, 0x00},
		{0x579d, 0x2c},
		{0x579e, 0x0c},
		{0x579f, 0x40},
		{0x57a0, 0x09},
		{0x57a1, 0x40},
		{0x59f8, 0x3d},
		{0x5a08, 0x02},
		{0x5b00, 0x02},
		{0x5b01, 0x10},
		{0x5b02, 0x03},
		{0x5b03, 0xcf},
		{0x5b05, 0x6c},
		{0x5e00, 0x00}
};

static const struct ov8856_reg lane_4_mode_1640x1232[] = {
	/* 1640x1232 resolution */
		{0x3000, 0x20},
		{0x3003, 0x08},
		{0x300e, 0x20},
		{0x3010, 0x00},
		{0x3015, 0x84},
		{0x3018, 0x72},
		{0x3021, 0x23},
		{0x3033, 0x24},
		{0x3500, 0x00},
		{0x3501, 0x4c},
		{0x3502, 0xe0},
		{0x3503, 0x08},
		{0x3505, 0x83},
		{0x3508, 0x01},
		{0x3509, 0x80},
		{0x350c, 0x00},
		{0x350d, 0x80},
		{0x350e, 0x04},
		{0x350f, 0x00},
		{0x3510, 0x00},
		{0x3511, 0x02},
		{0x3512, 0x00},
		{0x3600, 0x72},
		{0x3601, 0x40},
		{0x3602, 0x30},
		{0x3610, 0xc5},
		{0x3611, 0x58},
		{0x3612, 0x5c},
		{0x3613, 0xca},
		{0x3614, 0x20},
		{0x3628, 0xff},
		{0x3629, 0xff},
		{0x362a, 0xff},
		{0x3633, 0x10},
		{0x3634, 0x10},
		{0x3635, 0x10},
		{0x3636, 0x10},
		{0x3663, 0x08},
		{0x3669, 0x34},
		{0x366e, 0x08},
		{0x3706, 0x86},
		{0x370b, 0x7e},
		{0x3714, 0x27},
		{0x3730, 0x12},
		{0x3733, 0x10},
		{0x3764, 0x00},
		{0x3765, 0x00},
		{0x3769, 0x62},
		{0x376a, 0x2a},
		{0x376b, 0x30},
		{0x3780, 0x00},
		{0x3781, 0x24},
		{0x3782, 0x00},
		{0x3783, 0x23},
		{0x3798, 0x2f},
		{0x37a1, 0x60},
		{0x37a8, 0x6a},
		{0x37ab, 0x3f},
		{0x37c2, 0x14},
		{0x37c3, 0xf1},
		{0x37c9, 0x80},
		{0x37cb, 0x16},
		{0x37cc, 0x16},
		{0x37cd, 0x16},
		{0x37ce, 0x16},
		{0x3800, 0x00},
		{0x3801, 0x00},
		{0x3802, 0x00},
		{0x3803, 0x00},
		{0x3804, 0x0c},
		{0x3805, 0xdf},
		{0x3806, 0x09},
		{0x3807, 0xaf},
		{0x3808, 0x06},
		{0x3809, 0x68},
		{0x380a, 0x04},
		{0x380b, 0xd0},
		{0x380c, 0x0e},
		{0x380d, 0xec},
		{0x380e, 0x04},
		{0x380f, 0xe8},
		{0x3810, 0x00},
		{0x3811, 0x04},
		{0x3812, 0x00},
		{0x3813, 0x05},
		{0x3814, 0x03},
		{0x3815, 0x01},
		{0x3816, 0x00},
		{0x3817, 0x00},
		{0x3818, 0x00},
		{0x3819, 0x10},
		{0x3820, 0x90},
		{0x3821, 0x67},
		{0x382a, 0x03},
		{0x382b, 0x01},
		{0x3830, 0x06},
		{0x3836, 0x02},
		{0x3862, 0x04},
		{0x3863, 0x08},
		{0x3cc0, 0x33},
		{0x3d85, 0x17},
		{0x3d8c, 0x73},
		{0x3d8d, 0xde},
		{0x4001, 0xe0},
		{0x4003, 0x40},
		{0x4008, 0x00},
		{0x4009, 0x05},
		{0x400a, 0x00},
		{0x400b, 0x84},
		{0x400f, 0x80},
		{0x4010, 0xf0},
		{0x4011, 0xff},
		{0x4012, 0x02},
		{0x4013, 0x01},
		{0x4014, 0x01},
		{0x4015, 0x01},
		{0x4042, 0x00},
		{0x4043, 0x80},
		{0x4044, 0x00},
		{0x4045, 0x80},
		{0x4046, 0x00},
		{0x4047, 0x80},
		{0x4048, 0x00},
		{0x4049, 0x80},
		{0x4041, 0x03},
		{0x404c, 0x20},
		{0x404d, 0x00},
		{0x404e, 0x20},
		{0x4203, 0x80},
		{0x4307, 0x30},
		{0x4317, 0x00},
		{0x4503, 0x08},
		{0x4601, 0x80},
		{0x4800, 0x44},
		{0x4816, 0x53},
		{0x481b, 0x58},
		{0x481f, 0x27},
		{0x4837, 0x16},
		{0x483c, 0x0f},
		{0x484b, 0x05},
		{0x5000, 0x57},
		{0x5001, 0x0a},
		{0x5004, 0x06},
		{0x502e, 0x03},
		{0x5030, 0x41},
		{0x5780, 0x14},
		{0x5781, 0x0f},
		{0x5782, 0x44},
		{0x5783, 0x02},
		{0x5784, 0x01},
		{0x5785, 0x01},
		{0x5786, 0x00},
		{0x5787, 0x04},
		{0x5788, 0x02},
		{0x5789, 0x0f},
		{0x578a, 0xfd},
		{0x578b, 0xf5},
		{0x578c, 0xf5},
		{0x578d, 0x03},
		{0x578e, 0x08},
		{0x578f, 0x0c},
		{0x5790, 0x08},
		{0x5791, 0x04},
		{0x5792, 0x00},
		{0x5793, 0x52},
		{0x5794, 0xa3},
		{0x5795, 0x00},
		{0x5796, 0x10},
		{0x5797, 0x10},
		{0x5798, 0x73},
		{0x5799, 0x73},
		{0x579a, 0x00},
		{0x579b, 0x28},
		{0x579c, 0x00},
		{0x579d, 0x16},
		{0x579e, 0x06},
		{0x579f, 0x20},
		{0x57a0, 0x04},
		{0x57a1, 0xa0},
		{0x59f8, 0x3d},
		{0x5a08, 0x02},
		{0x5b00, 0x02},
		{0x5b01, 0x10},
		{0x5b02, 0x03},
		{0x5b03, 0xcf},
		{0x5b05, 0x6c},
		{0x5e00, 0x00}
};

static const struct ov8856_reg lane_4_mode_3264x2448[] = {
	/* 3264x2448 resolution */
		{0x0103, 0x01},
		{0x0302, 0x3c},
		{0x0303, 0x01},
		{0x031e, 0x0c},
		{0x3000, 0x20},
		{0x3003, 0x08},
		{0x300e, 0x20},
		{0x3010, 0x00},
		{0x3015, 0x84},
		{0x3018, 0x72},
		{0x3021, 0x23},
		{0x3033, 0x24},
		{0x3500, 0x00},
		{0x3501, 0x9a},
		{0x3502, 0x20},
		{0x3503, 0x08},
		{0x3505, 0x83},
		{0x3508, 0x01},
		{0x3509, 0x80},
		{0x350c, 0x00},
		{0x350d, 0x80},
		{0x350e, 0x04},
		{0x350f, 0x00},
		{0x3510, 0x00},
		{0x3511, 0x02},
		{0x3512, 0x00},
		{0x3600, 0x72},
		{0x3601, 0x40},
		{0x3602, 0x30},
		{0x3610, 0xc5},
		{0x3611, 0x58},
		{0x3612, 0x5c},
		{0x3613, 0xca},
		{0x3614, 0x60},
		{0x3628, 0xff},
		{0x3629, 0xff},
		{0x362a, 0xff},
		{0x3633, 0x10},
		{0x3634, 0x10},
		{0x3635, 0x10},
		{0x3636, 0x10},
		{0x3663, 0x08},
		{0x3669, 0x34},
		{0x366d, 0x00},
		{0x366e, 0x10},
		{0x3706, 0x86},
		{0x370b, 0x7e},
		{0x3714, 0x23},
		{0x3730, 0x12},
		{0x3733, 0x10},
		{0x3764, 0x00},
		{0x3765, 0x00},
		{0x3769, 0x62},
		{0x376a, 0x2a},
		{0x376b, 0x30},
		{0x3780, 0x00},
		{0x3781, 0x24},
		{0x3782, 0x00},
		{0x3783, 0x23},
		{0x3798, 0x2f},
		{0x37a1, 0x60},
		{0x37a8, 0x6a},
		{0x37ab, 0x3f},
		{0x37c2, 0x04},
		{0x37c3, 0xf1},
		{0x37c9, 0x80},
		{0x37cb, 0x16},
		{0x37cc, 0x16},
		{0x37cd, 0x16},
		{0x37ce, 0x16},
		{0x3800, 0x00},
		{0x3801, 0x00},
		{0x3802, 0x00},
		{0x3803, 0x0c},
		{0x3804, 0x0c},
		{0x3805, 0xdf},
		{0x3806, 0x09},
		{0x3807, 0xa3},
		{0x3808, 0x0c},
		{0x3809, 0xc0},
		{0x380a, 0x09},
		{0x380b, 0x90},
		{0x380c, 0x07},
		{0x380d, 0x8c},
		{0x380e, 0x09},
		{0x380f, 0xb2},
		{0x3810, 0x00},
		{0x3811, 0x04},
		{0x3812, 0x00},
		{0x3813, 0x02},
		{0x3814, 0x01},
		{0x3815, 0x01},
		{0x3816, 0x00},
		{0x3817, 0x00},
		{0x3818, 0x00},
		{0x3819, 0x10},
		{0x3820, 0x80},
		{0x3821, 0x46},
		{0x382a, 0x01},
		{0x382b, 0x01},
		{0x3830, 0x06},
		{0x3836, 0x02},
		{0x3862, 0x04},
		{0x3863, 0x08},
		{0x3cc0, 0x33},
		{0x3d85, 0x17},
		{0x3d8c, 0x73},
		{0x3d8d, 0xde},
		{0x4001, 0xe0},
		{0x4003, 0x40},
		{0x4008, 0x00},
		{0x4009, 0x0b},
		{0x400a, 0x00},
		{0x400b, 0x84},
		{0x400f, 0x80},
		{0x4010, 0xf0},
		{0x4011, 0xff},
		{0x4012, 0x02},
		{0x4013, 0x01},
		{0x4014, 0x01},
		{0x4015, 0x01},
		{0x4042, 0x00},
		{0x4043, 0x80},
		{0x4044, 0x00},
		{0x4045, 0x80},
		{0x4046, 0x00},
		{0x4047, 0x80},
		{0x4048, 0x00},
		{0x4049, 0x80},
		{0x4041, 0x03},
		{0x404c, 0x20},
		{0x404d, 0x00},
		{0x404e, 0x20},
		{0x4203, 0x80},
		{0x4307, 0x30},
		{0x4317, 0x00},
		{0x4502, 0x50},
		{0x4503, 0x08},
		{0x4601, 0x80},
		{0x4800, 0x44},
		{0x4816, 0x53},
		{0x481b, 0x50},
		{0x481f, 0x27},
		{0x4823, 0x3c},
		{0x482b, 0x00},
		{0x4831, 0x66},
		{0x4837, 0x16},
		{0x483c, 0x0f},
		{0x484b, 0x05},
		{0x5000, 0x77},
		{0x5001, 0x0a},
		{0x5003, 0xc8},
		{0x5004, 0x04},
		{0x5006, 0x00},
		{0x5007, 0x00},
		{0x502e, 0x03},
		{0x5030, 0x41},
		{0x5780, 0x14},
		{0x5781, 0x0f},
		{0x5782, 0x44},
		{0x5783, 0x02},
		{0x5784, 0x01},
		{0x5785, 0x01},
		{0x5786, 0x00},
		{0x5787, 0x04},
		{0x5788, 0x02},
		{0x5789, 0x0f},
		{0x578a, 0xfd},
		{0x578b, 0xf5},
		{0x578c, 0xf5},
		{0x578d, 0x03},
		{0x578e, 0x08},
		{0x578f, 0x0c},
		{0x5790, 0x08},
		{0x5791, 0x04},
		{0x5792, 0x00},
		{0x5793, 0x52},
		{0x5794, 0xa3},
		{0x5795, 0x02},
		{0x5796, 0x20},
		{0x5797, 0x20},
		{0x5798, 0xd5},
		{0x5799, 0xd5},
		{0x579a, 0x00},
		{0x579b, 0x50},
		{0x579c, 0x00},
		{0x579d, 0x2c},
		{0x579e, 0x0c},
		{0x579f, 0x40},
		{0x57a0, 0x09},
		{0x57a1, 0x40},
		{0x59f8, 0x3d},
		{0x5a08, 0x02},
		{0x5b00, 0x02},
		{0x5b01, 0x10},
		{0x5b02, 0x03},
		{0x5b03, 0xcf},
		{0x5b05, 0x6c},
		{0x5e00, 0x00},
		{0x5e10, 0xfc}
};

static const struct ov8856_reg lane_4_mode_1632x1224[] = {
	/* 1632x1224 resolution */
		{0x0103, 0x01},
		{0x0302, 0x3c},
		{0x0303, 0x01},
		{0x031e, 0x0c},
		{0x3000, 0x20},
		{0x3003, 0x08},
		{0x300e, 0x20},
		{0x3010, 0x00},
		{0x3015, 0x84},
		{0x3018, 0x72},
		{0x3021, 0x23},
		{0x3033, 0x24},
		{0x3500, 0x00},
		{0x3501, 0x4c},
		{0x3502, 0xe0},
		{0x3503, 0x08},
		{0x3505, 0x83},
		{0x3508, 0x01},
		{0x3509, 0x80},
		{0x350c, 0x00},
		{0x350d, 0x80},
		{0x350e, 0x04},
		{0x350f, 0x00},
		{0x3510, 0x00},
		{0x3511, 0x02},
		{0x3512, 0x00},
		{0x3600, 0x72},
		{0x3601, 0x40},
		{0x3602, 0x30},
		{0x3610, 0xc5},
		{0x3611, 0x58},
		{0x3612, 0x5c},
		{0x3613, 0xca},
		{0x3614, 0x60},
		{0x3628, 0xff},
		{0x3629, 0xff},
		{0x362a, 0xff},
		{0x3633, 0x10},
		{0x3634, 0x10},
		{0x3635, 0x10},
		{0x3636, 0x10},
		{0x3663, 0x08},
		{0x3669, 0x34},
		{0x366d, 0x00},
		{0x366e, 0x08},
		{0x3706, 0x86},
		{0x370b, 0x7e},
		{0x3714, 0x27},
		{0x3730, 0x12},
		{0x3733, 0x10},
		{0x3764, 0x00},
		{0x3765, 0x00},
		{0x3769, 0x62},
		{0x376a, 0x2a},
		{0x376b, 0x30},
		{0x3780, 0x00},
		{0x3781, 0x24},
		{0x3782, 0x00},
		{0x3783, 0x23},
		{0x3798, 0x2f},
		{0x37a1, 0x60},
		{0x37a8, 0x6a},
		{0x37ab, 0x3f},
		{0x37c2, 0x14},
		{0x37c3, 0xf1},
		{0x37c9, 0x80},
		{0x37cb, 0x16},
		{0x37cc, 0x16},
		{0x37cd, 0x16},
		{0x37ce, 0x16},
		{0x3800, 0x00},
		{0x3801, 0x00},
		{0x3802, 0x00},
		{0x3803, 0x0c},
		{0x3804, 0x0c},
		{0x3805, 0xdf},
		{0x3806, 0x09},
		{0x3807, 0xa3},
		{0x3808, 0x06},
		{0x3809, 0x60},
		{0x380a, 0x04},
		{0x380b, 0xc8},
		{0x380c, 0x07},
		{0x380d, 0x8c},
		{0x380e, 0x09},
		{0x380f, 0xb2},
		{0x3810, 0x00},
		{0x3811, 0x02},
		{0x3812, 0x00},
		{0x3813, 0x02},
		{0x3814, 0x03},
		{0x3815, 0x01},
		{0x3816, 0x00},
		{0x3817, 0x00},
		{0x3818, 0x00},
		{0x3819, 0x10},
		{0x3820, 0x80},
		{0x3821, 0x47},
		{0x382a, 0x03},
		{0x382b, 0x01},
		{0x3830, 0x06},
		{0x3836, 0x02},
		{0x3862, 0x04},
		{0x3863, 0x08},
		{0x3cc0, 0x33},
		{0x3d85, 0x17},
		{0x3d8c, 0x73},
		{0x3d8d, 0xde},
		{0x4001, 0xe0},
		{0x4003, 0x40},
		{0x4008, 0x00},
		{0x4009, 0x05},
		{0x400a, 0x00},
		{0x400b, 0x84},
		{0x400f, 0x80},
		{0x4010, 0xf0},
		{0x4011, 0xff},
		{0x4012, 0x02},
		{0x4013, 0x01},
		{0x4014, 0x01},
		{0x4015, 0x01},
		{0x4042, 0x00},
		{0x4043, 0x80},
		{0x4044, 0x00},
		{0x4045, 0x80},
		{0x4046, 0x00},
		{0x4047, 0x80},
		{0x4048, 0x00},
		{0x4049, 0x80},
		{0x4041, 0x03},
		{0x404c, 0x20},
		{0x404d, 0x00},
		{0x404e, 0x20},
		{0x4203, 0x80},
		{0x4307, 0x30},
		{0x4317, 0x00},
		{0x4502, 0x50},
		{0x4503, 0x08},
		{0x4601, 0x80},
		{0x4800, 0x44},
		{0x4816, 0x53},
		{0x481b, 0x50},
		{0x481f, 0x27},
		{0x4823, 0x3c},
		{0x482b, 0x00},
		{0x4831, 0x66},
		{0x4837, 0x16},
		{0x483c, 0x0f},
		{0x484b, 0x05},
		{0x5000, 0x77},
		{0x5001, 0x0a},
		{0x5003, 0xc8},
		{0x5004, 0x04},
		{0x5006, 0x00},
		{0x5007, 0x00},
		{0x502e, 0x03},
		{0x5030, 0x41},
		{0x5795, 0x00},
		{0x5796, 0x10},
		{0x5797, 0x10},
		{0x5798, 0x73},
		{0x5799, 0x73},
		{0x579a, 0x00},
		{0x579b, 0x28},
		{0x579c, 0x00},
		{0x579d, 0x16},
		{0x579e, 0x06},
		{0x579f, 0x20},
		{0x57a0, 0x04},
		{0x57a1, 0xa0},
		{0x5780, 0x14},
		{0x5781, 0x0f},
		{0x5782, 0x44},
		{0x5783, 0x02},
		{0x5784, 0x01},
		{0x5785, 0x01},
		{0x5786, 0x00},
		{0x5787, 0x04},
		{0x5788, 0x02},
		{0x5789, 0x0f},
		{0x578a, 0xfd},
		{0x578b, 0xf5},
		{0x578c, 0xf5},
		{0x578d, 0x03},
		{0x578e, 0x08},
		{0x578f, 0x0c},
		{0x5790, 0x08},
		{0x5791, 0x04},
		{0x5792, 0x00},
		{0x5793, 0x52},
		{0x5794, 0xa3},
		{0x59f8, 0x3d},
		{0x5a08, 0x02},
		{0x5b00, 0x02},
		{0x5b01, 0x10},
		{0x5b02, 0x03},
		{0x5b03, 0xcf},
		{0x5b05, 0x6c},
		{0x5e00, 0x00},
		{0x5e10, 0xfc}
};

static const struct ov8856_reg mipi_data_mbus_sbggr10_1x10[] = {
	{0x3813, 0x02},
};

static const struct ov8856_reg mipi_data_mbus_sgrbg10_1x10[] = {
	{0x3813, 0x01},
};

static const u32 ov8856_mbus_codes[] = {
	MEDIA_BUS_FMT_SBGGR10_1X10,
	MEDIA_BUS_FMT_SGRBG10_1X10
};

static const char * const ov8856_test_pattern_menu[] = {
	"Disabled",
	"Standard Color Bar",
	"Top-Bottom Darker Color Bar",
	"Right-Left Darker Color Bar",
	"Bottom-Top Darker Color Bar"
};

static const struct ov8856_reg_list bayer_offset_configs[] = {
	[OV8856_MEDIA_BUS_FMT_SBGGR10_1X10] = {
		.num_of_regs = ARRAY_SIZE(mipi_data_mbus_sbggr10_1x10),
		.regs = mipi_data_mbus_sbggr10_1x10,
	},
	[OV8856_MEDIA_BUS_FMT_SGRBG10_1X10] = {
		.num_of_regs = ARRAY_SIZE(mipi_data_mbus_sgrbg10_1x10),
		.regs = mipi_data_mbus_sgrbg10_1x10,
	}
};

struct ov8856 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;

	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct regulator_bulk_data supplies[ARRAY_SIZE(ov8856_supply_names)];

	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;

	/* Current mode */
	const struct ov8856_mode *cur_mode;

	/* Application specified mbus format */
	u32 cur_mbus_index;

	/* To serialize asynchronus callbacks */
	struct mutex mutex;

	/* Streaming on/off */
	bool streaming;

	/* lanes index */
	u8 nlanes;

	const struct ov8856_lane_cfg *priv_lane;
	u8 modes_size;

	/* True if the device has been identified */
	bool identified;
};

struct ov8856_lane_cfg {
	const s64 link_freq_menu_items[2];
	const struct ov8856_link_freq_config link_freq_configs[2];
	const struct ov8856_mode supported_modes[4];
};

static const struct ov8856_lane_cfg lane_cfg_2 = {
	{
		720000000,
		360000000,
	},
	{{
		.reg_list = {
			.num_of_regs =
			ARRAY_SIZE(mipi_data_rate_lane_2.regs_0),
			.regs = mipi_data_rate_lane_2.regs_0,
		}
	},
	{
		.reg_list = {
			.num_of_regs =
			ARRAY_SIZE(mipi_data_rate_lane_2.regs_1),
			.regs = mipi_data_rate_lane_2.regs_1,
		}
	}},
	{{
		.width = 3280,
		.height = 2464,
		.hts = 1928,
		.vts_def = 2488,
		.vts_min = 2488,
		.reg_list = {
			.num_of_regs =
			ARRAY_SIZE(lane_2_mode_3280x2464),
			.regs = lane_2_mode_3280x2464,
		},
		.link_freq_index = 0,
		.data_lanes = 2,
		.default_mbus_index = OV8856_MEDIA_BUS_FMT_SGRBG10_1X10,
	},
	{
		.width = 1640,
		.height = 1232,
		.hts = 3168,
		.vts_def = 1514,
		.vts_min = 1514,
		.reg_list = {
			.num_of_regs =
			ARRAY_SIZE(lane_2_mode_1640x1232),
			.regs = lane_2_mode_1640x1232,
		},
		.link_freq_index = 1,
		.data_lanes = 2,
		.default_mbus_index = OV8856_MEDIA_BUS_FMT_SGRBG10_1X10,
	}}
};

static const struct ov8856_lane_cfg lane_cfg_4 = {
		{
			360000000,
			180000000,
		},
		{{
			.reg_list = {
				.num_of_regs =
				 ARRAY_SIZE(mipi_data_rate_lane_4.regs_0),
				.regs = mipi_data_rate_lane_4.regs_0,
			}
		},
		{
			.reg_list = {
				.num_of_regs =
				 ARRAY_SIZE(mipi_data_rate_lane_4.regs_1),
				.regs = mipi_data_rate_lane_4.regs_1,
			}
		}},
		{{
			.width = 3280,
			.height = 2464,
			.hts = 1928,
			.vts_def = 2488,
			.vts_min = 2488,
			.reg_list = {
				.num_of_regs =
				 ARRAY_SIZE(lane_4_mode_3280x2464),
				.regs = lane_4_mode_3280x2464,
			},
			.link_freq_index = 0,
			.data_lanes = 4,
			.default_mbus_index = OV8856_MEDIA_BUS_FMT_SGRBG10_1X10,
		},
		{
			.width = 1640,
			.height = 1232,
			.hts = 3820,
			.vts_def = 1256,
			.vts_min = 1256,
			.reg_list = {
				.num_of_regs =
				 ARRAY_SIZE(lane_4_mode_1640x1232),
				.regs = lane_4_mode_1640x1232,
			},
			.link_freq_index = 1,
			.data_lanes = 4,
			.default_mbus_index = OV8856_MEDIA_BUS_FMT_SGRBG10_1X10,
		},
		{
			.width = 3264,
			.height = 2448,
			.hts = 1932,
			.vts_def = 2482,
			.vts_min = 2482,
			.reg_list = {
				.num_of_regs =
				 ARRAY_SIZE(lane_4_mode_3264x2448),
				.regs = lane_4_mode_3264x2448,
			},
			.link_freq_index = 0,
			.data_lanes = 4,
			.default_mbus_index = OV8856_MEDIA_BUS_FMT_SBGGR10_1X10,
		},
		{
			.width = 1632,
			.height = 1224,
			.hts = 1932,
			.vts_def = 2482,
			.vts_min = 2482,
			.reg_list = {
				.num_of_regs =
				 ARRAY_SIZE(lane_4_mode_1632x1224),
				.regs = lane_4_mode_1632x1224,
			},
			.link_freq_index = 1,
			.data_lanes = 4,
			.default_mbus_index = OV8856_MEDIA_BUS_FMT_SBGGR10_1X10,
		}}
};

static unsigned int ov8856_modes_num(const struct ov8856 *ov8856)
{
	unsigned int i, count = 0;

	for (i = 0; i < ARRAY_SIZE(ov8856->priv_lane->supported_modes); i++) {
		if (ov8856->priv_lane->supported_modes[i].width == 0)
			break;
		count++;
	}

	return count;
}

static u64 to_rate(const s64 *link_freq_menu_items,
		   u32 f_index, u8 nlanes)
{
	u64 pixel_rate = link_freq_menu_items[f_index] * 2 * nlanes;

	do_div(pixel_rate, OV8856_RGB_DEPTH);

	return pixel_rate;
}

static u64 to_pixels_per_line(const s64 *link_freq_menu_items, u32 hts,
			      u32 f_index, u8 nlanes)
{
	u64 ppl = hts * to_rate(link_freq_menu_items, f_index, nlanes);

	do_div(ppl, OV8856_SCLK);

	return ppl;
}

static int ov8856_read_reg(struct ov8856 *ov8856, u16 reg, u16 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov8856->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2];
	u8 data_buf[4] = {0};
	int ret;

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, addr_buf);
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(addr_buf);
	msgs[0].buf = addr_buf;
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = get_unaligned_be32(data_buf);

	return 0;
}

static int ov8856_write_reg(struct ov8856 *ov8856, u16 reg, u16 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov8856->sd);
	u8 buf[6];

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << 8 * (4 - len), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int ov8856_write_reg_list(struct ov8856 *ov8856,
				 const struct ov8856_reg_list *r_list)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov8856->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < r_list->num_of_regs; i++) {
		ret = ov8856_write_reg(ov8856, r_list->regs[i].address, 1,
				       r_list->regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
				    "failed to write reg 0x%4.4x. error = %d",
				    r_list->regs[i].address, ret);
			return ret;
		}
	}

	return 0;
}

static int ov8856_identify_module(struct ov8856 *ov8856)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov8856->sd);
	int ret;
	u32 val;

	if (ov8856->identified)
		return 0;

	ret = ov8856_read_reg(ov8856, OV8856_REG_CHIP_ID,
			      OV8856_REG_VALUE_24BIT, &val);
	if (ret)
		return ret;

	if (val != OV8856_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x",
			OV8856_CHIP_ID, val);
		return -ENXIO;
	}

	ret = ov8856_write_reg(ov8856, OV8856_REG_MODE_SELECT,
			       OV8856_REG_VALUE_08BIT, OV8856_MODE_STREAMING);
	if (ret)
		return ret;

	ret = ov8856_write_reg(ov8856, OV8856_OTP_MODE_CTRL,
			       OV8856_REG_VALUE_08BIT, OV8856_OTP_MODE_AUTO);
	if (ret) {
		dev_err(&client->dev, "failed to set otp mode");
		return ret;
	}

	ret = ov8856_write_reg(ov8856, OV8856_OTP_LOAD_CTRL,
			       OV8856_REG_VALUE_08BIT,
			       OV8856_OTP_LOAD_CTRL_ENABLE);
	if (ret) {
		dev_err(&client->dev, "failed to enable load control");
		return ret;
	}

	ret = ov8856_read_reg(ov8856, OV8856_MODULE_REVISION,
			      OV8856_REG_VALUE_08BIT, &val);
	if (ret) {
		dev_err(&client->dev, "failed to read module revision");
		return ret;
	}

	dev_info(&client->dev, "OV8856 revision %x (%s) at address 0x%02x\n",
		 val,
		 val == OV8856_2A_MODULE ? "2A" :
		 val == OV8856_1B_MODULE ? "1B" : "unknown revision",
		 client->addr);

	ret = ov8856_write_reg(ov8856, OV8856_REG_MODE_SELECT,
			       OV8856_REG_VALUE_08BIT, OV8856_MODE_STANDBY);
	if (ret) {
		dev_err(&client->dev, "failed to exit streaming mode");
		return ret;
	}

	ov8856->identified = true;

	return 0;
}

static int ov8856_update_digital_gain(struct ov8856 *ov8856, u32 d_gain)
{
	return ov8856_write_reg(ov8856, OV8856_REG_DIGITAL_GAIN,
				OV8856_REG_VALUE_16BIT, d_gain);
}

static int ov8856_test_pattern(struct ov8856 *ov8856, u32 pattern)
{
	if (pattern)
		pattern = (pattern - 1) << OV8856_TEST_PATTERN_BAR_SHIFT |
			  OV8856_TEST_PATTERN_ENABLE;

	return ov8856_write_reg(ov8856, OV8856_REG_TEST_PATTERN,
				OV8856_REG_VALUE_08BIT, pattern);
}

static int ov8856_set_ctrl_hflip(struct ov8856 *ov8856, u32 ctrl_val)
{
	int ret;
	u32 val;

	ret = ov8856_read_reg(ov8856, OV8856_REG_MIRROR_OPT_1,
			      OV8856_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	ret = ov8856_write_reg(ov8856, OV8856_REG_MIRROR_OPT_1,
			       OV8856_REG_VALUE_08BIT,
			       ctrl_val ? val & ~OV8856_REG_MIRROR_OP_2 :
			       val | OV8856_REG_MIRROR_OP_2);

	if (ret)
		return ret;

	ret = ov8856_read_reg(ov8856, OV8856_REG_FORMAT2,
			      OV8856_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	return ov8856_write_reg(ov8856, OV8856_REG_FORMAT2,
				OV8856_REG_VALUE_08BIT,
				ctrl_val ? val & ~OV8856_REG_FORMAT2_OP_1 &
				~OV8856_REG_FORMAT2_OP_2 &
				~OV8856_REG_FORMAT2_OP_3 :
				val | OV8856_REG_FORMAT2_OP_1 |
				OV8856_REG_FORMAT2_OP_2 |
				OV8856_REG_FORMAT2_OP_3);
}

static int ov8856_set_ctrl_vflip(struct ov8856 *ov8856, u8 ctrl_val)
{
	int ret;
	u32 val;

	ret = ov8856_read_reg(ov8856, OV8856_REG_FLIP_OPT_1,
			      OV8856_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	ret = ov8856_write_reg(ov8856, OV8856_REG_FLIP_OPT_1,
			       OV8856_REG_VALUE_08BIT,
			       ctrl_val ? val | OV8856_REG_FLIP_OP_1 |
			       OV8856_REG_FLIP_OP_2 :
			       val & ~OV8856_REG_FLIP_OP_1 &
			       ~OV8856_REG_FLIP_OP_2);

	ret = ov8856_read_reg(ov8856, OV8856_REG_FLIP_OPT_2,
			      OV8856_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	ret = ov8856_write_reg(ov8856, OV8856_REG_FLIP_OPT_2,
			       OV8856_REG_VALUE_08BIT,
			       ctrl_val ? val | OV8856_REG_FLIP_OP_2 :
			       val & ~OV8856_REG_FLIP_OP_2);

	ret = ov8856_read_reg(ov8856, OV8856_REG_FLIP_OPT_3,
			      OV8856_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	ret = ov8856_write_reg(ov8856, OV8856_REG_FLIP_OPT_3,
			       OV8856_REG_VALUE_08BIT,
			       ctrl_val ? val & ~OV8856_REG_FLIP_OP_0 &
			       ~OV8856_REG_FLIP_OP_1 :
			       val | OV8856_REG_FLIP_OP_0 |
			       OV8856_REG_FLIP_OP_1);

	ret = ov8856_read_reg(ov8856, OV8856_REG_FORMAT1,
			      OV8856_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	return ov8856_write_reg(ov8856, OV8856_REG_FORMAT1,
			       OV8856_REG_VALUE_08BIT,
			       ctrl_val ? val | OV8856_REG_FORMAT1_OP_1 |
			       OV8856_REG_FORMAT1_OP_3 |
			       OV8856_REG_FORMAT1_OP_2 :
			       val & ~OV8856_REG_FORMAT1_OP_1 &
			       ~OV8856_REG_FORMAT1_OP_3 &
			       ~OV8856_REG_FORMAT1_OP_2);
}

static int ov8856_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov8856 *ov8856 = container_of(ctrl->handler,
					     struct ov8856, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&ov8856->sd);
	s64 exposure_max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Update max exposure while meeting expected vblanking */
		exposure_max = ov8856->cur_mode->height + ctrl->val -
			       OV8856_EXPOSURE_MAX_MARGIN;
		__v4l2_ctrl_modify_range(ov8856->exposure,
					 ov8856->exposure->minimum,
					 exposure_max, ov8856->exposure->step,
					 exposure_max);
	}

	/* V4L2 controls values will be applied only when power is already up */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov8856_write_reg(ov8856, OV8856_REG_ANALOG_GAIN,
				       OV8856_REG_VALUE_16BIT, ctrl->val);
		break;

	case V4L2_CID_DIGITAL_GAIN:
		ret = ov8856_update_digital_gain(ov8856, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = ov8856_write_reg(ov8856, OV8856_REG_EXPOSURE,
				       OV8856_REG_VALUE_24BIT, ctrl->val << 4);
		break;

	case V4L2_CID_VBLANK:
		ret = ov8856_write_reg(ov8856, OV8856_REG_VTS,
				       OV8856_REG_VALUE_16BIT,
				       ov8856->cur_mode->height + ctrl->val);
		break;

	case V4L2_CID_TEST_PATTERN:
		ret = ov8856_test_pattern(ov8856, ctrl->val);
		break;

	case V4L2_CID_HFLIP:
		ret = ov8856_set_ctrl_hflip(ov8856, ctrl->val);
		break;

	case V4L2_CID_VFLIP:
		ret = ov8856_set_ctrl_vflip(ov8856, ctrl->val);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov8856_ctrl_ops = {
	.s_ctrl = ov8856_set_ctrl,
};

static int ov8856_init_controls(struct ov8856 *ov8856)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 exposure_max, h_blank;
	int ret;

	ctrl_hdlr = &ov8856->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	ctrl_hdlr->lock = &ov8856->mutex;
	ov8856->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &ov8856_ctrl_ops,
					   V4L2_CID_LINK_FREQ,
					   ARRAY_SIZE
					   (ov8856->priv_lane->link_freq_menu_items)
					   - 1,
					   0, ov8856->priv_lane->link_freq_menu_items);
	if (ov8856->link_freq)
		ov8856->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ov8856->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ov8856_ctrl_ops,
				       V4L2_CID_PIXEL_RATE, 0,
				       to_rate(ov8856->priv_lane->link_freq_menu_items,
					       0,
					       ov8856->cur_mode->data_lanes), 1,
				       to_rate(ov8856->priv_lane->link_freq_menu_items,
					       0,
					       ov8856->cur_mode->data_lanes));
	ov8856->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov8856_ctrl_ops,
			  V4L2_CID_VBLANK,
			  ov8856->cur_mode->vts_min - ov8856->cur_mode->height,
			  OV8856_VTS_MAX - ov8856->cur_mode->height, 1,
			  ov8856->cur_mode->vts_def -
			  ov8856->cur_mode->height);
	h_blank = to_pixels_per_line(ov8856->priv_lane->link_freq_menu_items,
				     ov8856->cur_mode->hts,
				     ov8856->cur_mode->link_freq_index,
				     ov8856->cur_mode->data_lanes) -
				     ov8856->cur_mode->width;
	ov8856->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov8856_ctrl_ops,
					   V4L2_CID_HBLANK, h_blank, h_blank, 1,
					   h_blank);
	if (ov8856->hblank)
		ov8856->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &ov8856_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV8856_ANAL_GAIN_MIN, OV8856_ANAL_GAIN_MAX,
			  OV8856_ANAL_GAIN_STEP, OV8856_ANAL_GAIN_MIN);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov8856_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV8856_DGTL_GAIN_MIN, OV8856_DGTL_GAIN_MAX,
			  OV8856_DGTL_GAIN_STEP, OV8856_DGTL_GAIN_DEFAULT);
	exposure_max = ov8856->cur_mode->vts_def - OV8856_EXPOSURE_MAX_MARGIN;
	ov8856->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ov8856_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     OV8856_EXPOSURE_MIN, exposure_max,
					     OV8856_EXPOSURE_STEP,
					     exposure_max);
	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov8856_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov8856_test_pattern_menu) - 1,
				     0, 0, ov8856_test_pattern_menu);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov8856_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov8856_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (ctrl_hdlr->error)
		return ctrl_hdlr->error;

	ov8856->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

static void ov8856_update_pad_format(struct ov8856 *ov8856,
				     const struct ov8856_mode *mode,
				     struct v4l2_mbus_framefmt *fmt)
{
	int index;

	fmt->width = mode->width;
	fmt->height = mode->height;
	for (index = 0; index < ARRAY_SIZE(ov8856_mbus_codes); ++index)
		if (ov8856_mbus_codes[index] == fmt->code)
			break;
	if (index == ARRAY_SIZE(ov8856_mbus_codes))
		index = mode->default_mbus_index;
	fmt->code = ov8856_mbus_codes[index];
	ov8856->cur_mbus_index = index;
	fmt->field = V4L2_FIELD_NONE;
}

static int ov8856_start_streaming(struct ov8856 *ov8856)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov8856->sd);
	const struct ov8856_reg_list *reg_list;
	int link_freq_index, ret;

	ret = ov8856_identify_module(ov8856);
	if (ret)
		return ret;

	link_freq_index = ov8856->cur_mode->link_freq_index;
	reg_list = &ov8856->priv_lane->link_freq_configs[link_freq_index].reg_list;

	ret = ov8856_write_reg_list(ov8856, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set plls");
		return ret;
	}

	reg_list = &ov8856->cur_mode->reg_list;
	ret = ov8856_write_reg_list(ov8856, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set mode");
		return ret;
	}

	reg_list = &bayer_offset_configs[ov8856->cur_mbus_index];
	ret = ov8856_write_reg_list(ov8856, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set mbus format");
		return ret;
	}

	ret = __v4l2_ctrl_handler_setup(ov8856->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = ov8856_write_reg(ov8856, OV8856_REG_MODE_SELECT,
			       OV8856_REG_VALUE_08BIT, OV8856_MODE_STREAMING);
	if (ret) {
		dev_err(&client->dev, "failed to set stream");
		return ret;
	}

	return 0;
}

static void ov8856_stop_streaming(struct ov8856 *ov8856)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov8856->sd);

	if (ov8856_write_reg(ov8856, OV8856_REG_MODE_SELECT,
			     OV8856_REG_VALUE_08BIT, OV8856_MODE_STANDBY))
		dev_err(&client->dev, "failed to set stream");
}

static int ov8856_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov8856 *ov8856 = to_ov8856(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (ov8856->streaming == enable)
		return 0;

	mutex_lock(&ov8856->mutex);
	if (enable) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0) {
			mutex_unlock(&ov8856->mutex);
			return ret;
		}

		ret = ov8856_start_streaming(ov8856);
		if (ret) {
			enable = 0;
			ov8856_stop_streaming(ov8856);
			pm_runtime_put(&client->dev);
		}
	} else {
		ov8856_stop_streaming(ov8856);
		pm_runtime_put(&client->dev);
	}

	ov8856->streaming = enable;
	mutex_unlock(&ov8856->mutex);

	return ret;
}

static int __ov8856_power_on(struct ov8856 *ov8856)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov8856->sd);
	int ret;

	if (is_acpi_node(dev_fwnode(&client->dev)))
		return 0;

	ret = clk_prepare_enable(ov8856->xvclk);
	if (ret < 0) {
		dev_err(&client->dev, "failed to enable xvclk\n");
		return ret;
	}

	if (ov8856->reset_gpio) {
		gpiod_set_value_cansleep(ov8856->reset_gpio, 1);
		usleep_range(1000, 2000);
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(ov8856_supply_names),
				    ov8856->supplies);
	if (ret < 0) {
		dev_err(&client->dev, "failed to enable regulators\n");
		goto disable_clk;
	}

	gpiod_set_value_cansleep(ov8856->reset_gpio, 0);
	usleep_range(1500, 1800);

	return 0;

disable_clk:
	gpiod_set_value_cansleep(ov8856->reset_gpio, 1);
	clk_disable_unprepare(ov8856->xvclk);

	return ret;
}

static void __ov8856_power_off(struct ov8856 *ov8856)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov8856->sd);

	if (is_acpi_node(dev_fwnode(&client->dev)))
		return;

	gpiod_set_value_cansleep(ov8856->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ov8856_supply_names),
			       ov8856->supplies);
	clk_disable_unprepare(ov8856->xvclk);
}

static int __maybe_unused ov8856_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov8856 *ov8856 = to_ov8856(sd);

	mutex_lock(&ov8856->mutex);
	if (ov8856->streaming)
		ov8856_stop_streaming(ov8856);

	__ov8856_power_off(ov8856);
	mutex_unlock(&ov8856->mutex);

	return 0;
}

static int __maybe_unused ov8856_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov8856 *ov8856 = to_ov8856(sd);
	int ret;

	mutex_lock(&ov8856->mutex);

	__ov8856_power_on(ov8856);
	if (ov8856->streaming) {
		ret = ov8856_start_streaming(ov8856);
		if (ret) {
			ov8856->streaming = false;
			ov8856_stop_streaming(ov8856);
			mutex_unlock(&ov8856->mutex);
			return ret;
		}
	}

	mutex_unlock(&ov8856->mutex);

	return 0;
}

static int ov8856_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *fmt)
{
	struct ov8856 *ov8856 = to_ov8856(sd);
	const struct ov8856_mode *mode;
	s32 vblank_def, h_blank;

	mode = v4l2_find_nearest_size(ov8856->priv_lane->supported_modes,
				      ov8856->modes_size,
				      width, height, fmt->format.width,
				      fmt->format.height);

	mutex_lock(&ov8856->mutex);
	ov8856_update_pad_format(ov8856, mode, &fmt->format);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_get_try_format(sd, sd_state, fmt->pad) = fmt->format;
	} else {
		ov8856->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(ov8856->link_freq, mode->link_freq_index);
		__v4l2_ctrl_s_ctrl_int64(ov8856->pixel_rate,
					 to_rate(ov8856->priv_lane->link_freq_menu_items,
						 mode->link_freq_index,
						 ov8856->cur_mode->data_lanes));

		/* Update limits and set FPS to default */
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov8856->vblank,
					 mode->vts_min - mode->height,
					 OV8856_VTS_MAX - mode->height, 1,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(ov8856->vblank, vblank_def);
		h_blank = to_pixels_per_line(ov8856->priv_lane->link_freq_menu_items,
					     mode->hts,
					     mode->link_freq_index,
					     ov8856->cur_mode->data_lanes)
					     - mode->width;
		__v4l2_ctrl_modify_range(ov8856->hblank, h_blank, h_blank, 1,
					 h_blank);
	}

	mutex_unlock(&ov8856->mutex);

	return 0;
}

static int ov8856_get_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *fmt)
{
	struct ov8856 *ov8856 = to_ov8856(sd);

	mutex_lock(&ov8856->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt->format = *v4l2_subdev_get_try_format(&ov8856->sd,
							  sd_state,
							  fmt->pad);
	else
		ov8856_update_pad_format(ov8856, ov8856->cur_mode, &fmt->format);

	mutex_unlock(&ov8856->mutex);

	return 0;
}

static int ov8856_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(ov8856_mbus_codes))
		return -EINVAL;

	code->code = ov8856_mbus_codes[code->index];

	return 0;
}

static int ov8856_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct ov8856 *ov8856 = to_ov8856(sd);
	int index;

	if (fse->index >= ov8856->modes_size)
		return -EINVAL;

	for (index = 0; index < ARRAY_SIZE(ov8856_mbus_codes); ++index)
		if (fse->code == ov8856_mbus_codes[index])
			break;
	if (index == ARRAY_SIZE(ov8856_mbus_codes))
		return -EINVAL;

	fse->min_width = ov8856->priv_lane->supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = ov8856->priv_lane->supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int ov8856_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov8856 *ov8856 = to_ov8856(sd);

	mutex_lock(&ov8856->mutex);
	ov8856_update_pad_format(ov8856, &ov8856->priv_lane->supported_modes[0],
				 v4l2_subdev_get_try_format(sd, fh->state, 0));
	mutex_unlock(&ov8856->mutex);

	return 0;
}

static const struct v4l2_subdev_video_ops ov8856_video_ops = {
	.s_stream = ov8856_set_stream,
};

static const struct v4l2_subdev_pad_ops ov8856_pad_ops = {
	.set_fmt = ov8856_set_format,
	.get_fmt = ov8856_get_format,
	.enum_mbus_code = ov8856_enum_mbus_code,
	.enum_frame_size = ov8856_enum_frame_size,
};

static const struct v4l2_subdev_ops ov8856_subdev_ops = {
	.video = &ov8856_video_ops,
	.pad = &ov8856_pad_ops,
};

static const struct media_entity_operations ov8856_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops ov8856_internal_ops = {
	.open = ov8856_open,
};


static int ov8856_get_hwcfg(struct ov8856 *ov8856, struct device *dev)
{
	struct fwnode_handle *ep;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	u32 xvclk_rate;
	int ret;
	unsigned int i, j;

	if (!fwnode)
		return -ENXIO;

	ret = fwnode_property_read_u32(fwnode, "clock-frequency", &xvclk_rate);
	if (ret)
		return ret;

	if (!is_acpi_node(fwnode)) {
		ov8856->xvclk = devm_clk_get(dev, "xvclk");
		if (IS_ERR(ov8856->xvclk)) {
			dev_err(dev, "could not get xvclk clock (%pe)\n",
				ov8856->xvclk);
			return PTR_ERR(ov8856->xvclk);
		}

		clk_set_rate(ov8856->xvclk, xvclk_rate);
		xvclk_rate = clk_get_rate(ov8856->xvclk);

		ov8856->reset_gpio = devm_gpiod_get_optional(dev, "reset",
							     GPIOD_OUT_LOW);
		if (IS_ERR(ov8856->reset_gpio))
			return PTR_ERR(ov8856->reset_gpio);

		for (i = 0; i < ARRAY_SIZE(ov8856_supply_names); i++)
			ov8856->supplies[i].supply = ov8856_supply_names[i];

		ret = devm_regulator_bulk_get(dev,
					      ARRAY_SIZE(ov8856_supply_names),
					      ov8856->supplies);
		if (ret)
			return ret;
	}

	if (xvclk_rate != OV8856_XVCLK_19_2)
		dev_warn(dev, "external clock rate %u is unsupported",
			 xvclk_rate);

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	/* Get number of data lanes */
	if (bus_cfg.bus.mipi_csi2.num_data_lanes != 2 &&
	    bus_cfg.bus.mipi_csi2.num_data_lanes != 4) {
		dev_err(dev, "number of CSI2 data lanes %d is not supported",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto check_hwcfg_error;
	}

	dev_dbg(dev, "Using %u data lanes\n", ov8856->cur_mode->data_lanes);

	if (bus_cfg.bus.mipi_csi2.num_data_lanes == 2)
		ov8856->priv_lane = &lane_cfg_2;
	else
		ov8856->priv_lane = &lane_cfg_4;

	ov8856->modes_size = ov8856_modes_num(ov8856);

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(dev, "no link frequencies defined");
		ret = -EINVAL;
		goto check_hwcfg_error;
	}

	for (i = 0; i < ARRAY_SIZE(ov8856->priv_lane->link_freq_menu_items); i++) {
		for (j = 0; j < bus_cfg.nr_of_link_frequencies; j++) {
			if (ov8856->priv_lane->link_freq_menu_items[i] ==
			    bus_cfg.link_frequencies[j])
				break;
		}

		if (j == bus_cfg.nr_of_link_frequencies) {
			dev_err(dev, "no link frequency %lld supported",
				ov8856->priv_lane->link_freq_menu_items[i]);
			ret = -EINVAL;
			goto check_hwcfg_error;
		}
	}

check_hwcfg_error:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static void ov8856_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov8856 *ov8856 = to_ov8856(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(&client->dev);
	mutex_destroy(&ov8856->mutex);

	__ov8856_power_off(ov8856);
}

static int ov8856_probe(struct i2c_client *client)
{
	struct ov8856 *ov8856;
	int ret;
	bool full_power;

	ov8856 = devm_kzalloc(&client->dev, sizeof(*ov8856), GFP_KERNEL);
	if (!ov8856)
		return -ENOMEM;

	ret = ov8856_get_hwcfg(ov8856, &client->dev);
	if (ret) {
		dev_err(&client->dev, "failed to get HW configuration: %d",
			ret);
		return ret;
	}

	v4l2_i2c_subdev_init(&ov8856->sd, client, &ov8856_subdev_ops);

	full_power = acpi_dev_state_d0(&client->dev);
	if (full_power) {
		ret = __ov8856_power_on(ov8856);
		if (ret) {
			dev_err(&client->dev, "failed to power on\n");
			return ret;
		}

		ret = ov8856_identify_module(ov8856);
		if (ret) {
			dev_err(&client->dev, "failed to find sensor: %d", ret);
			goto probe_power_off;
		}
	}

	mutex_init(&ov8856->mutex);
	ov8856->cur_mode = &ov8856->priv_lane->supported_modes[0];
	ov8856->cur_mbus_index = ov8856->cur_mode->default_mbus_index;
	ret = ov8856_init_controls(ov8856);
	if (ret) {
		dev_err(&client->dev, "failed to init controls: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ov8856->sd.internal_ops = &ov8856_internal_ops;
	ov8856->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov8856->sd.entity.ops = &ov8856_subdev_entity_ops;
	ov8856->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ov8856->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&ov8856->sd.entity, 1, &ov8856->pad);
	if (ret) {
		dev_err(&client->dev, "failed to init entity pads: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&ov8856->sd);
	if (ret < 0) {
		dev_err(&client->dev, "failed to register V4L2 subdev: %d",
			ret);
		goto probe_error_media_entity_cleanup;
	}

	/* Set the device's state to active if it's in D0 state. */
	if (full_power)
		pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

probe_error_media_entity_cleanup:
	media_entity_cleanup(&ov8856->sd.entity);

probe_error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(ov8856->sd.ctrl_handler);
	mutex_destroy(&ov8856->mutex);

probe_power_off:
	__ov8856_power_off(ov8856);

	return ret;
}

static const struct dev_pm_ops ov8856_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ov8856_suspend, ov8856_resume)
};

#ifdef CONFIG_ACPI
static const struct acpi_device_id ov8856_acpi_ids[] = {
	{"OVTI8856"},
	{}
};

MODULE_DEVICE_TABLE(acpi, ov8856_acpi_ids);
#endif

static const struct of_device_id ov8856_of_match[] = {
	{ .compatible = "ovti,ov8856" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ov8856_of_match);

static struct i2c_driver ov8856_i2c_driver = {
	.driver = {
		.name = "ov8856",
		.pm = &ov8856_pm_ops,
		.acpi_match_table = ACPI_PTR(ov8856_acpi_ids),
		.of_match_table = ov8856_of_match,
	},
	.probe_new = ov8856_probe,
	.remove = ov8856_remove,
	.flags = I2C_DRV_ACPI_WAIVE_D0_PROBE,
};

module_i2c_driver(ov8856_i2c_driver);

MODULE_AUTHOR("Ben Kao <ben.kao@intel.com>");
MODULE_DESCRIPTION("OmniVision OV8856 sensor driver");
MODULE_LICENSE("GPL v2");
