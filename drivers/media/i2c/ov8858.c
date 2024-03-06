// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Jacopo Mondi <jacopo.mondi@ideasonboard.com>
 * Copyright (C) 2022 Nicholas Roth <nicholas@rothemail.net>
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 */

#include <asm/unaligned.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#define OV8858_LINK_FREQ		360000000U
#define OV8858_XVCLK_FREQ		24000000

#define OV8858_REG_SIZE_SHIFT		16
#define OV8858_REG_ADDR_MASK		0xffff
#define OV8858_REG_8BIT(n)		((1U << OV8858_REG_SIZE_SHIFT) | (n))
#define OV8858_REG_16BIT(n)		((2U << OV8858_REG_SIZE_SHIFT) | (n))
#define OV8858_REG_24BIT(n)		((3U << OV8858_REG_SIZE_SHIFT) | (n))

#define OV8858_REG_SC_CTRL0100		OV8858_REG_8BIT(0x0100)
#define OV8858_MODE_SW_STANDBY		0x0
#define OV8858_MODE_STREAMING		0x1

#define OV8858_REG_CHIP_ID		OV8858_REG_24BIT(0x300a)
#define OV8858_CHIP_ID			0x008858

#define OV8858_REG_SUB_ID		OV8858_REG_8BIT(0x302a)
#define OV8858_R1A			0xb0
#define OV8858_R2A			0xb2

#define OV8858_REG_LONG_EXPO		OV8858_REG_24BIT(0x3500)
#define OV8858_EXPOSURE_MIN		4
#define OV8858_EXPOSURE_STEP		1
#define OV8858_EXPOSURE_MARGIN		4

#define OV8858_REG_LONG_GAIN		OV8858_REG_16BIT(0x3508)
#define OV8858_LONG_GAIN_MIN		0x0
#define OV8858_LONG_GAIN_MAX		0x7ff
#define OV8858_LONG_GAIN_STEP		1
#define OV8858_LONG_GAIN_DEFAULT	0x80

#define OV8858_REG_LONG_DIGIGAIN	OV8858_REG_16BIT(0x350a)
#define OV8858_LONG_DIGIGAIN_H_MASK	0x3fc0
#define OV8858_LONG_DIGIGAIN_L_MASK	0x3f
#define OV8858_LONG_DIGIGAIN_H_SHIFT	2
#define OV8858_LONG_DIGIGAIN_MIN	0x0
#define OV8858_LONG_DIGIGAIN_MAX	0x3fff
#define OV8858_LONG_DIGIGAIN_STEP	1
#define OV8858_LONG_DIGIGAIN_DEFAULT	0x200

#define OV8858_REG_VTS			OV8858_REG_16BIT(0x380e)
#define OV8858_VTS_MAX			0x7fff

#define OV8858_REG_TEST_PATTERN		OV8858_REG_8BIT(0x5e00)
#define OV8858_TEST_PATTERN_ENABLE	0x80
#define OV8858_TEST_PATTERN_DISABLE	0x0

#define REG_NULL			0xffff

static const char * const ov8858_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

struct regval {
	u16 addr;
	u8 val;
};

struct regval_modes {
	const struct regval *mode_2lanes;
	const struct regval *mode_4lanes;
};

struct ov8858_mode {
	u32 width;
	u32 height;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval_modes reg_modes;
};

struct ov8858 {
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[ARRAY_SIZE(ov8858_supply_names)];

	struct v4l2_subdev	subdev;
	struct media_pad	pad;

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;

	const struct regval	*global_regs;

	unsigned int		num_lanes;
};

static inline struct ov8858 *sd_to_ov8858(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov8858, subdev);
}

static const struct regval ov8858_global_regs_r1a[] = {
	{0x0100, 0x00},
	{0x0100, 0x00},
	{0x0100, 0x00},
	{0x0100, 0x00},
	{0x0302, 0x1e},
	{0x0303, 0x00},
	{0x0304, 0x03},
	{0x030e, 0x00},
	{0x030f, 0x09},
	{0x0312, 0x01},
	{0x031e, 0x0c},
	{0x3600, 0x00},
	{0x3601, 0x00},
	{0x3602, 0x00},
	{0x3603, 0x00},
	{0x3604, 0x22},
	{0x3605, 0x30},
	{0x3606, 0x00},
	{0x3607, 0x20},
	{0x3608, 0x11},
	{0x3609, 0x28},
	{0x360a, 0x00},
	{0x360b, 0x06},
	{0x360c, 0xdc},
	{0x360d, 0x40},
	{0x360e, 0x0c},
	{0x360f, 0x20},
	{0x3610, 0x07},
	{0x3611, 0x20},
	{0x3612, 0x88},
	{0x3613, 0x80},
	{0x3614, 0x58},
	{0x3615, 0x00},
	{0x3616, 0x4a},
	{0x3617, 0xb0},
	{0x3618, 0x56},
	{0x3619, 0x70},
	{0x361a, 0x99},
	{0x361b, 0x00},
	{0x361c, 0x07},
	{0x361d, 0x00},
	{0x361e, 0x00},
	{0x361f, 0x00},
	{0x3638, 0xff},
	{0x3633, 0x0c},
	{0x3634, 0x0c},
	{0x3635, 0x0c},
	{0x3636, 0x0c},
	{0x3645, 0x13},
	{0x3646, 0x83},
	{0x364a, 0x07},
	{0x3015, 0x01},
	{0x3018, 0x32},
	{0x3020, 0x93},
	{0x3022, 0x01},
	{0x3031, 0x0a},
	{0x3034, 0x00},
	{0x3106, 0x01},
	{0x3305, 0xf1},
	{0x3308, 0x00},
	{0x3309, 0x28},
	{0x330a, 0x00},
	{0x330b, 0x20},
	{0x330c, 0x00},
	{0x330d, 0x00},
	{0x330e, 0x00},
	{0x330f, 0x40},
	{0x3307, 0x04},
	{0x3500, 0x00},
	{0x3501, 0x4d},
	{0x3502, 0x40},
	{0x3503, 0x00},
	{0x3505, 0x80},
	{0x3508, 0x04},
	{0x3509, 0x00},
	{0x350c, 0x00},
	{0x350d, 0x80},
	{0x3510, 0x00},
	{0x3511, 0x02},
	{0x3512, 0x00},
	{0x3700, 0x18},
	{0x3701, 0x0c},
	{0x3702, 0x28},
	{0x3703, 0x19},
	{0x3704, 0x14},
	{0x3705, 0x00},
	{0x3706, 0x35},
	{0x3707, 0x04},
	{0x3708, 0x24},
	{0x3709, 0x33},
	{0x370a, 0x00},
	{0x370b, 0xb5},
	{0x370c, 0x04},
	{0x3718, 0x12},
	{0x3719, 0x31},
	{0x3712, 0x42},
	{0x3714, 0x24},
	{0x371e, 0x19},
	{0x371f, 0x40},
	{0x3720, 0x05},
	{0x3721, 0x05},
	{0x3724, 0x06},
	{0x3725, 0x01},
	{0x3726, 0x06},
	{0x3728, 0x05},
	{0x3729, 0x02},
	{0x372a, 0x03},
	{0x372b, 0x53},
	{0x372c, 0xa3},
	{0x372d, 0x53},
	{0x372e, 0x06},
	{0x372f, 0x10},
	{0x3730, 0x01},
	{0x3731, 0x06},
	{0x3732, 0x14},
	{0x3733, 0x10},
	{0x3734, 0x40},
	{0x3736, 0x20},
	{0x373a, 0x05},
	{0x373b, 0x06},
	{0x373c, 0x0a},
	{0x373e, 0x03},
	{0x3755, 0x10},
	{0x3758, 0x00},
	{0x3759, 0x4c},
	{0x375a, 0x06},
	{0x375b, 0x13},
	{0x375c, 0x20},
	{0x375d, 0x02},
	{0x375e, 0x00},
	{0x375f, 0x14},
	{0x3768, 0x22},
	{0x3769, 0x44},
	{0x376a, 0x44},
	{0x3761, 0x00},
	{0x3762, 0x00},
	{0x3763, 0x00},
	{0x3766, 0xff},
	{0x376b, 0x00},
	{0x3772, 0x23},
	{0x3773, 0x02},
	{0x3774, 0x16},
	{0x3775, 0x12},
	{0x3776, 0x04},
	{0x3777, 0x00},
	{0x3778, 0x1b},
	{0x37a0, 0x44},
	{0x37a1, 0x3d},
	{0x37a2, 0x3d},
	{0x37a3, 0x00},
	{0x37a4, 0x00},
	{0x37a5, 0x00},
	{0x37a6, 0x00},
	{0x37a7, 0x44},
	{0x37a8, 0x4c},
	{0x37a9, 0x4c},
	{0x3760, 0x00},
	{0x376f, 0x01},
	{0x37aa, 0x44},
	{0x37ab, 0x2e},
	{0x37ac, 0x2e},
	{0x37ad, 0x33},
	{0x37ae, 0x0d},
	{0x37af, 0x0d},
	{0x37b0, 0x00},
	{0x37b1, 0x00},
	{0x37b2, 0x00},
	{0x37b3, 0x42},
	{0x37b4, 0x42},
	{0x37b5, 0x33},
	{0x37b6, 0x00},
	{0x37b7, 0x00},
	{0x37b8, 0x00},
	{0x37b9, 0xff},
	{0x3800, 0x00},
	{0x3801, 0x0c},
	{0x3802, 0x00},
	{0x3803, 0x0c},
	{0x3804, 0x0c},
	{0x3805, 0xd3},
	{0x3806, 0x09},
	{0x3807, 0xa3},
	{0x3808, 0x06},
	{0x3809, 0x60},
	{0x380a, 0x04},
	{0x380b, 0xc8},
	{0x380c, 0x07},
	{0x380d, 0x88},
	{0x380e, 0x04},
	{0x380f, 0xdc},
	{0x3810, 0x00},
	{0x3811, 0x04},
	{0x3813, 0x02},
	{0x3814, 0x03},
	{0x3815, 0x01},
	{0x3820, 0x00},
	{0x3821, 0x67},
	{0x382a, 0x03},
	{0x382b, 0x01},
	{0x3830, 0x08},
	{0x3836, 0x02},
	{0x3837, 0x18},
	{0x3841, 0xff},
	{0x3846, 0x48},
	{0x3d85, 0x14},
	{0x3f08, 0x08},
	{0x3f0a, 0x80},
	{0x4000, 0xf1},
	{0x4001, 0x10},
	{0x4005, 0x10},
	{0x4002, 0x27},
	{0x4009, 0x81},
	{0x400b, 0x0c},
	{0x401b, 0x00},
	{0x401d, 0x00},
	{0x4020, 0x00},
	{0x4021, 0x04},
	{0x4022, 0x04},
	{0x4023, 0xb9},
	{0x4024, 0x05},
	{0x4025, 0x2a},
	{0x4026, 0x05},
	{0x4027, 0x2b},
	{0x4028, 0x00},
	{0x4029, 0x02},
	{0x402a, 0x04},
	{0x402b, 0x04},
	{0x402c, 0x02},
	{0x402d, 0x02},
	{0x402e, 0x08},
	{0x402f, 0x02},
	{0x401f, 0x00},
	{0x4034, 0x3f},
	{0x403d, 0x04},
	{0x4300, 0xff},
	{0x4301, 0x00},
	{0x4302, 0x0f},
	{0x4316, 0x00},
	{0x4500, 0x38},
	{0x4503, 0x18},
	{0x4600, 0x00},
	{0x4601, 0xcb},
	{0x481f, 0x32},
	{0x4837, 0x16},
	{0x4850, 0x10},
	{0x4851, 0x32},
	{0x4b00, 0x2a},
	{0x4b0d, 0x00},
	{0x4d00, 0x04},
	{0x4d01, 0x18},
	{0x4d02, 0xc3},
	{0x4d03, 0xff},
	{0x4d04, 0xff},
	{0x4d05, 0xff},
	{0x5000, 0x7e},
	{0x5001, 0x01},
	{0x5002, 0x08},
	{0x5003, 0x20},
	{0x5046, 0x12},
	{0x5901, 0x00},
	{0x5e00, 0x00},
	{0x5e01, 0x41},
	{0x382d, 0x7f},
	{0x4825, 0x3a},
	{0x4826, 0x40},
	{0x4808, 0x25},
	{REG_NULL, 0x00},
};

static const struct regval ov8858_global_regs_r2a_2lane[] = {
	/*
	 * MIPI=720Mbps, SysClk=144Mhz,Dac Clock=360Mhz.
	 * v00_01_00 (05/29/2014) : initial setting
	 * AM19 : 3617 <- 0xC0
	 * AM20 : change FWC_6K_EN to be default 0x3618=0x5a
	 */
	{0x0103, 0x01}, /* software reset */
	{0x0100, 0x00}, /* software standby */
	{0x0302, 0x1e}, /* pll1_multi */
	{0x0303, 0x00}, /* pll1_divm */
	{0x0304, 0x03}, /* pll1_div_mipi */
	{0x030e, 0x02}, /* pll2_rdiv */
	{0x030f, 0x04}, /* pll2_divsp */
	{0x0312, 0x03}, /* pll2_pre_div0, pll2_r_divdac */
	{0x031e, 0x0c}, /* pll1_no_lat */
	{0x3600, 0x00},
	{0x3601, 0x00},
	{0x3602, 0x00},
	{0x3603, 0x00},
	{0x3604, 0x22},
	{0x3605, 0x20},
	{0x3606, 0x00},
	{0x3607, 0x20},
	{0x3608, 0x11},
	{0x3609, 0x28},
	{0x360a, 0x00},
	{0x360b, 0x05},
	{0x360c, 0xd4},
	{0x360d, 0x40},
	{0x360e, 0x0c},
	{0x360f, 0x20},
	{0x3610, 0x07},
	{0x3611, 0x20},
	{0x3612, 0x88},
	{0x3613, 0x80},
	{0x3614, 0x58},
	{0x3615, 0x00},
	{0x3616, 0x4a},
	{0x3617, 0x90},
	{0x3618, 0x5a},
	{0x3619, 0x70},
	{0x361a, 0x99},
	{0x361b, 0x0a},
	{0x361c, 0x07},
	{0x361d, 0x00},
	{0x361e, 0x00},
	{0x361f, 0x00},
	{0x3638, 0xff},
	{0x3633, 0x0f},
	{0x3634, 0x0f},
	{0x3635, 0x0f},
	{0x3636, 0x12},
	{0x3645, 0x13},
	{0x3646, 0x83},
	{0x364a, 0x07},
	{0x3015, 0x00},
	{0x3018, 0x32}, /* MIPI 2 lane */
	{0x3020, 0x93}, /* Clock switch output normal, pclk_div =/1 */
	{0x3022, 0x01}, /* pd_mipi enable when rst_sync */
	{0x3031, 0x0a}, /* MIPI 10-bit mode */
	{0x3034, 0x00},
	{0x3106, 0x01}, /* sclk_div, sclk_pre_div */
	{0x3305, 0xf1},
	{0x3308, 0x00},
	{0x3309, 0x28},
	{0x330a, 0x00},
	{0x330b, 0x20},
	{0x330c, 0x00},
	{0x330d, 0x00},
	{0x330e, 0x00},
	{0x330f, 0x40},
	{0x3307, 0x04},
	{0x3500, 0x00}, /* exposure H */
	{0x3501, 0x4d}, /* exposure M */
	{0x3502, 0x40}, /* exposure L */
	{0x3503, 0x80}, /* gain delay ?, exposure delay 1 frame, real gain */
	{0x3505, 0x80}, /* gain option */
	{0x3508, 0x02}, /* gain H */
	{0x3509, 0x00}, /* gain L */
	{0x350c, 0x00}, /* short gain H */
	{0x350d, 0x80}, /* short gain L */
	{0x3510, 0x00}, /* short exposure H */
	{0x3511, 0x02}, /* short exposure M */
	{0x3512, 0x00}, /* short exposure L */
	{0x3700, 0x18},
	{0x3701, 0x0c},
	{0x3702, 0x28},
	{0x3703, 0x19},
	{0x3704, 0x14},
	{0x3705, 0x00},
	{0x3706, 0x82},
	{0x3707, 0x04},
	{0x3708, 0x24},
	{0x3709, 0x33},
	{0x370a, 0x01},
	{0x370b, 0x82},
	{0x370c, 0x04},
	{0x3718, 0x12},
	{0x3719, 0x31},
	{0x3712, 0x42},
	{0x3714, 0x24},
	{0x371e, 0x19},
	{0x371f, 0x40},
	{0x3720, 0x05},
	{0x3721, 0x05},
	{0x3724, 0x06},
	{0x3725, 0x01},
	{0x3726, 0x06},
	{0x3728, 0x05},
	{0x3729, 0x02},
	{0x372a, 0x03},
	{0x372b, 0x53},
	{0x372c, 0xa3},
	{0x372d, 0x53},
	{0x372e, 0x06},
	{0x372f, 0x10},
	{0x3730, 0x01},
	{0x3731, 0x06},
	{0x3732, 0x14},
	{0x3733, 0x10},
	{0x3734, 0x40},
	{0x3736, 0x20},
	{0x373a, 0x05},
	{0x373b, 0x06},
	{0x373c, 0x0a},
	{0x373e, 0x03},
	{0x3750, 0x0a},
	{0x3751, 0x0e},
	{0x3755, 0x10},
	{0x3758, 0x00},
	{0x3759, 0x4c},
	{0x375a, 0x06},
	{0x375b, 0x13},
	{0x375c, 0x20},
	{0x375d, 0x02},
	{0x375e, 0x00},
	{0x375f, 0x14},
	{0x3768, 0x22},
	{0x3769, 0x44},
	{0x376a, 0x44},
	{0x3761, 0x00},
	{0x3762, 0x00},
	{0x3763, 0x00},
	{0x3766, 0xff},
	{0x376b, 0x00},
	{0x3772, 0x23},
	{0x3773, 0x02},
	{0x3774, 0x16},
	{0x3775, 0x12},
	{0x3776, 0x04},
	{0x3777, 0x00},
	{0x3778, 0x17},
	{0x37a0, 0x44},
	{0x37a1, 0x3d},
	{0x37a2, 0x3d},
	{0x37a3, 0x00},
	{0x37a4, 0x00},
	{0x37a5, 0x00},
	{0x37a6, 0x00},
	{0x37a7, 0x44},
	{0x37a8, 0x4c},
	{0x37a9, 0x4c},
	{0x3760, 0x00},
	{0x376f, 0x01},
	{0x37aa, 0x44},
	{0x37ab, 0x2e},
	{0x37ac, 0x2e},
	{0x37ad, 0x33},
	{0x37ae, 0x0d},
	{0x37af, 0x0d},
	{0x37b0, 0x00},
	{0x37b1, 0x00},
	{0x37b2, 0x00},
	{0x37b3, 0x42},
	{0x37b4, 0x42},
	{0x37b5, 0x31},
	{0x37b6, 0x00},
	{0x37b7, 0x00},
	{0x37b8, 0x00},
	{0x37b9, 0xff},
	{0x3800, 0x00}, /* x start H */
	{0x3801, 0x0c}, /* x start L */
	{0x3802, 0x00}, /* y start H */
	{0x3803, 0x0c}, /* y start L */
	{0x3804, 0x0c}, /* x end H */
	{0x3805, 0xd3}, /* x end L */
	{0x3806, 0x09}, /* y end H */
	{0x3807, 0xa3}, /* y end L */
	{0x3808, 0x06}, /* x output size H */
	{0x3809, 0x60}, /* x output size L */
	{0x380a, 0x04}, /* y output size H */
	{0x380b, 0xc8}, /* y output size L */
	{0x380c, 0x07}, /* HTS H */
	{0x380d, 0x88}, /* HTS L */
	{0x380e, 0x04}, /* VTS H */
	{0x380f, 0xdc}, /* VTS L */
	{0x3810, 0x00}, /* ISP x win H */
	{0x3811, 0x04}, /* ISP x win L */
	{0x3813, 0x02}, /* ISP y win L */
	{0x3814, 0x03}, /* x odd inc */
	{0x3815, 0x01}, /* x even inc */
	{0x3820, 0x00}, /* vflip off */
	{0x3821, 0x67}, /* mirror on, bin on */
	{0x382a, 0x03}, /* y odd inc */
	{0x382b, 0x01}, /* y even inc */
	{0x3830, 0x08},
	{0x3836, 0x02},
	{0x3837, 0x18},
	{0x3841, 0xff}, /* window auto size enable */
	{0x3846, 0x48},
	{0x3d85, 0x16}, /* OTP power up load data enable with BIST */
	{0x3d8c, 0x73}, /* OTP setting start High */
	{0x3d8d, 0xde}, /* OTP setting start Low */
	{0x3f08, 0x08},
	{0x3f0a, 0x00},
	{0x4000, 0xf1}, /* out_range_trig, format_chg_trig */
	{0x4001, 0x10}, /* total 128 black column */
	{0x4005, 0x10}, /* BLC target L */
	{0x4002, 0x27}, /* value used to limit BLC offset */
	{0x4009, 0x81}, /* final BLC offset limitation enable */
	{0x400b, 0x0c}, /* DCBLC on, DCBLC manual mode on */
	{0x401b, 0x00}, /* zero line R coefficient */
	{0x401d, 0x00}, /* zoro line T coefficient */
	{0x4020, 0x00}, /* Anchor left start H */
	{0x4021, 0x04}, /* Anchor left start L */
	{0x4022, 0x06}, /* Anchor left end H */
	{0x4023, 0x00}, /* Anchor left end L */
	{0x4024, 0x0f}, /* Anchor right start H */
	{0x4025, 0x2a}, /* Anchor right start L */
	{0x4026, 0x0f}, /* Anchor right end H */
	{0x4027, 0x2b}, /* Anchor right end L */
	{0x4028, 0x00}, /* top zero line start */
	{0x4029, 0x02}, /* top zero line number */
	{0x402a, 0x04}, /* top black line start */
	{0x402b, 0x04}, /* top black line number */
	{0x402c, 0x00}, /* bottom zero line start */
	{0x402d, 0x02}, /* bottom zoro line number */
	{0x402e, 0x04}, /* bottom black line start */
	{0x402f, 0x04}, /* bottom black line number */
	{0x401f, 0x00}, /* interpolation x/y disable, Anchor one disable */
	{0x4034, 0x3f},
	{0x403d, 0x04}, /* md_precision_en */
	{0x4300, 0xff}, /* clip max H */
	{0x4301, 0x00}, /* clip min H */
	{0x4302, 0x0f}, /* clip min L, clip max L */
	{0x4316, 0x00},
	{0x4500, 0x58},
	{0x4503, 0x18},
	{0x4600, 0x00},
	{0x4601, 0xcb},
	{0x481f, 0x32}, /* clk prepare min */
	{0x4837, 0x16}, /* global timing */
	{0x4850, 0x10}, /* lane 1 = 1, lane 0 = 0 */
	{0x4851, 0x32}, /* lane 3 = 3, lane 2 = 2 */
	{0x4b00, 0x2a},
	{0x4b0d, 0x00},
	{0x4d00, 0x04}, /* temperature sensor */
	{0x4d01, 0x18},
	{0x4d02, 0xc3},
	{0x4d03, 0xff},
	{0x4d04, 0xff},
	{0x4d05, 0xff}, /* temperature sensor */
	{0x5000, 0xfe}, /* lenc on, slave/master AWB gain/statistics enable */
	{0x5001, 0x01}, /* BLC on */
	{0x5002, 0x08}, /* H scale off, WBMATCH off, OTP_DPC */
	{0x5003, 0x20}, /* DPC_DBC buffer control enable, WB */
	{0x501e, 0x93}, /* enable digital gain */
	{0x5046, 0x12},
	{0x5780, 0x3e}, /* DPC */
	{0x5781, 0x0f},
	{0x5782, 0x44},
	{0x5783, 0x02},
	{0x5784, 0x01},
	{0x5785, 0x00},
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
	{0x5794, 0xa3}, /* DPC */
	{0x5871, 0x0d}, /* Lenc */
	{0x5870, 0x18},
	{0x586e, 0x10},
	{0x586f, 0x08},
	{0x58f7, 0x01},
	{0x58f8, 0x3d}, /* Lenc */
	{0x5901, 0x00}, /* H skip off, V skip off */
	{0x5b00, 0x02}, /* OTP DPC start address */
	{0x5b01, 0x10}, /* OTP DPC start address */
	{0x5b02, 0x03}, /* OTP DPC end address */
	{0x5b03, 0xcf}, /* OTP DPC end address */
	{0x5b05, 0x6c}, /* recover method = 2b11, */
	{0x5e00, 0x00}, /* use 0x3ff to test pattern off */
	{0x5e01, 0x41}, /* window cut enable */
	{0x382d, 0x7f},
	{0x4825, 0x3a}, /* lpx_p_min */
	{0x4826, 0x40}, /* hs_prepare_min */
	{0x4808, 0x25}, /* wake up delay in 1/1024 s */
	{0x3763, 0x18},
	{0x3768, 0xcc},
	{0x470b, 0x28},
	{0x4202, 0x00},
	{0x400d, 0x10}, /* BLC offset trigger L */
	{0x4040, 0x04}, /* BLC gain th2 */
	{0x403e, 0x04}, /* BLC gain th1 */
	{0x4041, 0xc6}, /* BLC */
	{0x3007, 0x80},
	{0x400a, 0x01},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 720Mbps
 */
static const struct regval ov8858_1632x1224_regs_2lane[] = {
	/*
	 * MIPI=720Mbps, SysClk=144Mhz,Dac Clock=360Mhz.
	 * v00_01_00 (05/29/2014) : initial setting
	 * AM19 : 3617 <- 0xC0
	 * AM20 : change FWC_6K_EN to be default 0x3618=0x5a
	 */
	{0x0100, 0x00},
	{0x3501, 0x4d}, /* exposure M */
	{0x3502, 0x40}, /* exposure L */
	{0x3778, 0x17},
	{0x3808, 0x06}, /* x output size H */
	{0x3809, 0x60}, /* x output size L */
	{0x380a, 0x04}, /* y output size H */
	{0x380b, 0xc8}, /* y output size L */
	{0x380c, 0x07}, /* HTS H */
	{0x380d, 0x88}, /* HTS L */
	{0x380e, 0x04}, /* VTS H */
	{0x380f, 0xdc}, /* VTS L */
	{0x3814, 0x03}, /* x odd inc */
	{0x3821, 0x67}, /* mirror on, bin on */
	{0x382a, 0x03}, /* y odd inc */
	{0x3830, 0x08},
	{0x3836, 0x02},
	{0x3f0a, 0x00},
	{0x4001, 0x10}, /* total 128 black column */
	{0x4022, 0x06}, /* Anchor left end H */
	{0x4023, 0x00}, /* Anchor left end L */
	{0x4025, 0x2a}, /* Anchor right start L */
	{0x4027, 0x2b}, /* Anchor right end L */
	{0x402b, 0x04}, /* top black line number */
	{0x402f, 0x04}, /* bottom black line number */
	{0x4500, 0x58},
	{0x4600, 0x00},
	{0x4601, 0xcb},
	{0x382d, 0x7f},
	{0x0100, 0x01},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 15fps
 * mipi_datarate per lane 720Mbps
 */
static const struct regval ov8858_3264x2448_regs_2lane[] = {
	{0x0100, 0x00},
	{0x3501, 0x9a}, /* exposure M */
	{0x3502, 0x20}, /* exposure L */
	{0x3778, 0x1a},
	{0x3808, 0x0c}, /* x output size H */
	{0x3809, 0xc0}, /* x output size L */
	{0x380a, 0x09}, /* y output size H */
	{0x380b, 0x90}, /* y output size L */
	{0x380c, 0x07}, /* HTS H */
	{0x380d, 0x94}, /* HTS L */
	{0x380e, 0x09}, /* VTS H */
	{0x380f, 0xaa}, /* VTS L */
	{0x3814, 0x01}, /* x odd inc */
	{0x3821, 0x46}, /* mirror on, bin off */
	{0x382a, 0x01}, /* y odd inc */
	{0x3830, 0x06},
	{0x3836, 0x01},
	{0x3f0a, 0x00},
	{0x4001, 0x00}, /* total 256 black column */
	{0x4022, 0x0c}, /* Anchor left end H */
	{0x4023, 0x60}, /* Anchor left end L */
	{0x4025, 0x36}, /* Anchor right start L */
	{0x4027, 0x37}, /* Anchor right end L */
	{0x402b, 0x08}, /* top black line number */
	{0x402f, 0x08}, /* bottom black line number */
	{0x4500, 0x58},
	{0x4600, 0x01},
	{0x4601, 0x97},
	{0x382d, 0xff},
	{REG_NULL, 0x00},
};

static const struct regval ov8858_global_regs_r2a_4lane[] = {
	/*
	 * MIPI=720Mbps, SysClk=144Mhz,Dac Clock=360Mhz.
	 * v00_01_00 (05/29/2014) : initial setting
	 * AM19 : 3617 <- 0xC0
	 * AM20 : change FWC_6K_EN to be default 0x3618=0x5a
	 */
	{0x0103, 0x01}, /* software reset for OVTATool only */
	{0x0103, 0x01}, /* software reset */
	{0x0100, 0x00}, /* software standby */
	{0x0302, 0x1e}, /* pll1_multi */
	{0x0303, 0x00}, /* pll1_divm */
	{0x0304, 0x03}, /* pll1_div_mipi */
	{0x030e, 0x00}, /* pll2_rdiv */
	{0x030f, 0x04}, /* pll2_divsp */
	{0x0312, 0x01}, /* pll2_pre_div0, pll2_r_divdac */
	{0x031e, 0x0c}, /* pll1_no_lat */
	{0x3600, 0x00},
	{0x3601, 0x00},
	{0x3602, 0x00},
	{0x3603, 0x00},
	{0x3604, 0x22},
	{0x3605, 0x20},
	{0x3606, 0x00},
	{0x3607, 0x20},
	{0x3608, 0x11},
	{0x3609, 0x28},
	{0x360a, 0x00},
	{0x360b, 0x05},
	{0x360c, 0xd4},
	{0x360d, 0x40},
	{0x360e, 0x0c},
	{0x360f, 0x20},
	{0x3610, 0x07},
	{0x3611, 0x20},
	{0x3612, 0x88},
	{0x3613, 0x80},
	{0x3614, 0x58},
	{0x3615, 0x00},
	{0x3616, 0x4a},
	{0x3617, 0x90},
	{0x3618, 0x5a},
	{0x3619, 0x70},
	{0x361a, 0x99},
	{0x361b, 0x0a},
	{0x361c, 0x07},
	{0x361d, 0x00},
	{0x361e, 0x00},
	{0x361f, 0x00},
	{0x3638, 0xff},
	{0x3633, 0x0f},
	{0x3634, 0x0f},
	{0x3635, 0x0f},
	{0x3636, 0x12},
	{0x3645, 0x13},
	{0x3646, 0x83},
	{0x364a, 0x07},
	{0x3015, 0x01},
	{0x3018, 0x72}, /* MIPI 4 lane */
	{0x3020, 0x93}, /* Clock switch output normal, pclk_div =/1 */
	{0x3022, 0x01}, /* pd_mipi enable when rst_sync */
	{0x3031, 0x0a}, /* MIPI 10-bit mode */
	{0x3034, 0x00},
	{0x3106, 0x01}, /* sclk_div, sclk_pre_div */
	{0x3305, 0xf1},
	{0x3308, 0x00},
	{0x3309, 0x28},
	{0x330a, 0x00},
	{0x330b, 0x20},
	{0x330c, 0x00},
	{0x330d, 0x00},
	{0x330e, 0x00},
	{0x330f, 0x40},
	{0x3307, 0x04},
	{0x3500, 0x00}, /* exposure H */
	{0x3501, 0x4d}, /* exposure M */
	{0x3502, 0x40}, /* exposure L */
	{0x3503, 0x80}, /* gain delay ?, exposure delay 1 frame, real gain */
	{0x3505, 0x80}, /* gain option */
	{0x3508, 0x02}, /* gain H */
	{0x3509, 0x00}, /* gain L */
	{0x350c, 0x00}, /* short gain H */
	{0x350d, 0x80}, /* short gain L */
	{0x3510, 0x00}, /* short exposure H */
	{0x3511, 0x02}, /* short exposure M */
	{0x3512, 0x00}, /* short exposure L */
	{0x3700, 0x30},
	{0x3701, 0x18},
	{0x3702, 0x50},
	{0x3703, 0x32},
	{0x3704, 0x28},
	{0x3705, 0x00},
	{0x3706, 0x82},
	{0x3707, 0x08},
	{0x3708, 0x48},
	{0x3709, 0x66},
	{0x370a, 0x01},
	{0x370b, 0x82},
	{0x370c, 0x07},
	{0x3718, 0x14},
	{0x3719, 0x31},
	{0x3712, 0x44},
	{0x3714, 0x24},
	{0x371e, 0x31},
	{0x371f, 0x7f},
	{0x3720, 0x0a},
	{0x3721, 0x0a},
	{0x3724, 0x0c},
	{0x3725, 0x02},
	{0x3726, 0x0c},
	{0x3728, 0x0a},
	{0x3729, 0x03},
	{0x372a, 0x06},
	{0x372b, 0xa6},
	{0x372c, 0xa6},
	{0x372d, 0xa6},
	{0x372e, 0x0c},
	{0x372f, 0x20},
	{0x3730, 0x02},
	{0x3731, 0x0c},
	{0x3732, 0x28},
	{0x3733, 0x10},
	{0x3734, 0x40},
	{0x3736, 0x30},
	{0x373a, 0x0a},
	{0x373b, 0x0b},
	{0x373c, 0x14},
	{0x373e, 0x06},
	{0x3750, 0x0a},
	{0x3751, 0x0e},
	{0x3755, 0x10},
	{0x3758, 0x00},
	{0x3759, 0x4c},
	{0x375a, 0x0c},
	{0x375b, 0x26},
	{0x375c, 0x20},
	{0x375d, 0x04},
	{0x375e, 0x00},
	{0x375f, 0x28},
	{0x3768, 0x22},
	{0x3769, 0x44},
	{0x376a, 0x44},
	{0x3761, 0x00},
	{0x3762, 0x00},
	{0x3763, 0x00},
	{0x3766, 0xff},
	{0x376b, 0x00},
	{0x3772, 0x46},
	{0x3773, 0x04},
	{0x3774, 0x2c},
	{0x3775, 0x13},
	{0x3776, 0x08},
	{0x3777, 0x00},
	{0x3778, 0x17},
	{0x37a0, 0x88},
	{0x37a1, 0x7a},
	{0x37a2, 0x7a},
	{0x37a3, 0x00},
	{0x37a4, 0x00},
	{0x37a5, 0x00},
	{0x37a6, 0x00},
	{0x37a7, 0x88},
	{0x37a8, 0x98},
	{0x37a9, 0x98},
	{0x3760, 0x00},
	{0x376f, 0x01},
	{0x37aa, 0x88},
	{0x37ab, 0x5c},
	{0x37ac, 0x5c},
	{0x37ad, 0x55},
	{0x37ae, 0x19},
	{0x37af, 0x19},
	{0x37b0, 0x00},
	{0x37b1, 0x00},
	{0x37b2, 0x00},
	{0x37b3, 0x84},
	{0x37b4, 0x84},
	{0x37b5, 0x60},
	{0x37b6, 0x00},
	{0x37b7, 0x00},
	{0x37b8, 0x00},
	{0x37b9, 0xff},
	{0x3800, 0x00}, /* x start H */
	{0x3801, 0x0c}, /* x start L */
	{0x3802, 0x00}, /* y start H */
	{0x3803, 0x0c}, /* y start L */
	{0x3804, 0x0c}, /* x end H */
	{0x3805, 0xd3}, /* x end L */
	{0x3806, 0x09}, /* y end H */
	{0x3807, 0xa3}, /* y end L */
	{0x3808, 0x06}, /* x output size H */
	{0x3809, 0x60}, /* x output size L */
	{0x380a, 0x04}, /* y output size H */
	{0x380b, 0xc8}, /* y output size L */
	{0x380c, 0x07}, /* HTS H */
	{0x380d, 0x88}, /* HTS L */
	{0x380e, 0x04}, /* VTS H */
	{0x380f, 0xdc}, /* VTS L */
	{0x3810, 0x00}, /* ISP x win H */
	{0x3811, 0x04}, /* ISP x win L */
	{0x3813, 0x02}, /* ISP y win L */
	{0x3814, 0x03}, /* x odd inc */
	{0x3815, 0x01}, /* x even inc */
	{0x3820, 0x00}, /* vflip off */
	{0x3821, 0x67}, /* mirror on, bin o */
	{0x382a, 0x03}, /* y odd inc */
	{0x382b, 0x01}, /* y even inc */
	{0x3830, 0x08},
	{0x3836, 0x02},
	{0x3837, 0x18},
	{0x3841, 0xff}, /* window auto size enable */
	{0x3846, 0x48},
	{0x3d85, 0x16}, /* OTP power up load data/setting enable */
	{0x3d8c, 0x73}, /* OTP setting start High */
	{0x3d8d, 0xde}, /* OTP setting start Low */
	{0x3f08, 0x10},
	{0x3f0a, 0x00},
	{0x4000, 0xf1}, /* out_range/format_chg/gain/exp_chg trig enable */
	{0x4001, 0x10}, /* total 128 black column */
	{0x4005, 0x10}, /* BLC target L */
	{0x4002, 0x27}, /* value used to limit BLC offset */
	{0x4009, 0x81}, /* final BLC offset limitation enable */
	{0x400b, 0x0c}, /* DCBLC on, DCBLC manual mode on */
	{0x401b, 0x00}, /* zero line R coefficient */
	{0x401d, 0x00}, /* zoro line T coefficient */
	{0x4020, 0x00}, /* Anchor left start H */
	{0x4021, 0x04}, /* Anchor left start L */
	{0x4022, 0x06}, /* Anchor left end H */
	{0x4023, 0x00}, /* Anchor left end L */
	{0x4024, 0x0f}, /* Anchor right start H */
	{0x4025, 0x2a}, /* Anchor right start L */
	{0x4026, 0x0f}, /* Anchor right end H */
	{0x4027, 0x2b}, /* Anchor right end L */
	{0x4028, 0x00}, /* top zero line start */
	{0x4029, 0x02}, /* top zero line number */
	{0x402a, 0x04}, /* top black line start */
	{0x402b, 0x04}, /* top black line number */
	{0x402c, 0x00}, /* bottom zero line start */
	{0x402d, 0x02}, /* bottom zoro line number */
	{0x402e, 0x04}, /* bottom black line start */
	{0x402f, 0x04}, /* bottom black line number */
	{0x401f, 0x00}, /* interpolation x/y disable, Anchor one disable */
	{0x4034, 0x3f},
	{0x403d, 0x04}, /* md_precision_en */
	{0x4300, 0xff}, /* clip max H */
	{0x4301, 0x00}, /* clip min H */
	{0x4302, 0x0f}, /* clip min L, clip max L */
	{0x4316, 0x00},
	{0x4500, 0x58},
	{0x4503, 0x18},
	{0x4600, 0x00},
	{0x4601, 0xcb},
	{0x481f, 0x32}, /* clk prepare min */
	{0x4837, 0x16}, /* global timing */
	{0x4850, 0x10}, /* lane 1 = 1, lane 0 = 0 */
	{0x4851, 0x32}, /* lane 3 = 3, lane 2 = 2 */
	{0x4b00, 0x2a},
	{0x4b0d, 0x00},
	{0x4d00, 0x04}, /* temperature sensor */
	{0x4d01, 0x18},
	{0x4d02, 0xc3},
	{0x4d03, 0xff},
	{0x4d04, 0xff},
	{0x4d05, 0xff}, /* temperature sensor */
	{0x5000, 0xfe}, /* lenc on, slave/master AWB gain/statistics enable */
	{0x5001, 0x01}, /* BLC on */
	{0x5002, 0x08}, /* WBMATCH sensor's gain, H scale/WBMATCH/OTP_DPC off */
	{0x5003, 0x20}, /* DPC_DBC buffer control enable, WB */
	{0x501e, 0x93}, /* enable digital gain */
	{0x5046, 0x12},
	{0x5780, 0x3e}, /* DPC */
	{0x5781, 0x0f},
	{0x5782, 0x44},
	{0x5783, 0x02},
	{0x5784, 0x01},
	{0x5785, 0x00},
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
	{0x5794, 0xa3}, /* DPC */
	{0x5871, 0x0d}, /* Lenc */
	{0x5870, 0x18},
	{0x586e, 0x10},
	{0x586f, 0x08},
	{0x58f7, 0x01},
	{0x58f8, 0x3d}, /* Lenc */
	{0x5901, 0x00}, /* H skip off, V skip off */
	{0x5b00, 0x02}, /* OTP DPC start address */
	{0x5b01, 0x10}, /* OTP DPC start address */
	{0x5b02, 0x03}, /* OTP DPC end address */
	{0x5b03, 0xcf}, /* OTP DPC end address */
	{0x5b05, 0x6c}, /* recover method = 2b11 */
	{0x5e00, 0x00}, /* use 0x3ff to test pattern off */
	{0x5e01, 0x41}, /* window cut enable */
	{0x382d, 0x7f},
	{0x4825, 0x3a}, /* lpx_p_min */
	{0x4826, 0x40}, /* hs_prepare_min */
	{0x4808, 0x25}, /* wake up delay in 1/1024 s */
	{0x3763, 0x18},
	{0x3768, 0xcc},
	{0x470b, 0x28},
	{0x4202, 0x00},
	{0x400d, 0x10}, /* BLC offset trigger L */
	{0x4040, 0x04}, /* BLC gain th2 */
	{0x403e, 0x04}, /* BLC gain th1 */
	{0x4041, 0xc6}, /* BLC */
	{0x3007, 0x80},
	{0x400a, 0x01},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 60fps
 * mipi_datarate per lane 720Mbps
 */
static const struct regval ov8858_1632x1224_regs_4lane[] = {
	{0x0100, 0x00},
	{0x3501, 0x4d}, /* exposure M */
	{0x3502, 0x40}, /* exposure L */
	{0x3808, 0x06}, /* x output size H */
	{0x3809, 0x60}, /* x output size L */
	{0x380a, 0x04}, /* y output size H */
	{0x380b, 0xc8}, /* y output size L */
	{0x380c, 0x07}, /* HTS H */
	{0x380d, 0x88}, /* HTS L */
	{0x380e, 0x04}, /* VTS H */
	{0x380f, 0xdc}, /* VTS L */
	{0x3814, 0x03}, /* x odd inc */
	{0x3821, 0x67}, /* mirror on, bin on */
	{0x382a, 0x03}, /* y odd inc */
	{0x3830, 0x08},
	{0x3836, 0x02},
	{0x3f0a, 0x00},
	{0x4001, 0x10}, /* total 128 black column */
	{0x4022, 0x06}, /* Anchor left end H */
	{0x4023, 0x00}, /* Anchor left end L */
	{0x4025, 0x2a}, /* Anchor right start L */
	{0x4027, 0x2b}, /* Anchor right end L */
	{0x402b, 0x04}, /* top black line number */
	{0x402f, 0x04}, /* bottom black line number */
	{0x4500, 0x58},
	{0x4600, 0x00},
	{0x4601, 0xcb},
	{0x382d, 0x7f},
	{0x0100, 0x01},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 30fps
 * mipi_datarate per lane 720Mbps
 */
static const struct regval ov8858_3264x2448_regs_4lane[] = {
	{0x0100, 0x00},
	{0x3501, 0x9a}, /* exposure M */
	{0x3502, 0x20}, /* exposure L */
	{0x3808, 0x0c}, /* x output size H */
	{0x3809, 0xc0}, /* x output size L */
	{0x380a, 0x09}, /* y output size H */
	{0x380b, 0x90}, /* y output size L */
	{0x380c, 0x07}, /* HTS H */
	{0x380d, 0x94}, /* HTS L */
	{0x380e, 0x09}, /* VTS H */
	{0x380f, 0xaa}, /* VTS L */
	{0x3814, 0x01}, /* x odd inc */
	{0x3821, 0x46}, /* mirror on, bin off */
	{0x382a, 0x01}, /* y odd inc */
	{0x3830, 0x06},
	{0x3836, 0x01},
	{0x3f0a, 0x00},
	{0x4001, 0x00}, /* total 256 black column */
	{0x4022, 0x0c}, /* Anchor left end H */
	{0x4023, 0x60}, /* Anchor left end L */
	{0x4025, 0x36}, /* Anchor right start L */
	{0x4027, 0x37}, /* Anchor right end L */
	{0x402b, 0x08}, /* top black line number */
	{0x402f, 0x08}, /* interpolation x/y disable, Anchor one disable */
	{0x4500, 0x58},
	{0x4600, 0x01},
	{0x4601, 0x97},
	{0x382d, 0xff},
	{REG_NULL, 0x00},
};

static const struct ov8858_mode ov8858_modes[] = {
	{
		.width = 3264,
		.height = 2448,
		.exp_def = 2464,
		.hts_def = 1940 * 2,
		.vts_def = 2472,
		.reg_modes = {
			.mode_2lanes = ov8858_3264x2448_regs_2lane,
			.mode_4lanes = ov8858_3264x2448_regs_4lane,
		},
	},
	{
		.width = 1632,
		.height = 1224,
		.exp_def = 1232,
		.hts_def = 1928 * 2,
		.vts_def = 1244,
		.reg_modes = {
			.mode_2lanes = ov8858_1632x1224_regs_2lane,
			.mode_4lanes = ov8858_1632x1224_regs_4lane,
		},
	},
};

static const s64 link_freq_menu_items[] = {
	OV8858_LINK_FREQ
};

static const char * const ov8858_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* ----------------------------------------------------------------------------
 * HW access
 */

static int ov8858_write(struct ov8858 *ov8858, u32 reg, u32 val, int *err)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov8858->subdev);
	unsigned int len = (reg >> OV8858_REG_SIZE_SHIFT) & 3;
	u16 addr = reg & OV8858_REG_ADDR_MASK;
	u8 buf[6];
	int ret;

	if (err && *err)
		return *err;

	put_unaligned_be16(addr, buf);
	put_unaligned_be32(val << (8 * (4 - len)), buf + 2);

	ret = i2c_master_send(client, buf, len + 2);
	if (ret != len + 2) {
		ret = ret < 0 ? ret : -EIO;
		if (err)
			*err = ret;

		dev_err(&client->dev,
			"Failed to write reg %u: %d\n", addr, ret);
		return ret;
	}

	return 0;
}

static int ov8858_write_array(struct ov8858 *ov8858, const struct regval *regs)
{
	unsigned int i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; ++i) {
		ov8858_write(ov8858, OV8858_REG_8BIT(regs[i].addr),
			     regs[i].val, &ret);
	}

	return ret;
}

static int ov8858_read(struct ov8858 *ov8858, u32 reg, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov8858->subdev);
	__be16 reg_addr_be = cpu_to_be16(reg & OV8858_REG_ADDR_MASK);
	unsigned int len = (reg >> OV8858_REG_SIZE_SHIFT) & 3;
	struct i2c_msg msgs[2];
	__be32 data_be = 0;
	u8 *data_be_p;
	int ret;

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
	if (ret != ARRAY_SIZE(msgs)) {
		ret = ret < 0 ? ret : -EIO;
		dev_err(&client->dev,
			"Failed to read reg %u: %d\n", reg, ret);
		return ret;
	}

	*val = be32_to_cpu(data_be);

	return 0;
}

/* ----------------------------------------------------------------------------
 * Streaming
 */

static int ov8858_start_stream(struct ov8858 *ov8858,
			       struct v4l2_subdev_state *state)
{
	struct v4l2_mbus_framefmt *format;
	const struct ov8858_mode *mode;
	const struct regval *reg_list;
	int ret;

	ret = ov8858_write_array(ov8858, ov8858->global_regs);
	if (ret)
		return ret;

	format = v4l2_subdev_state_get_format(state, 0);
	mode = v4l2_find_nearest_size(ov8858_modes, ARRAY_SIZE(ov8858_modes),
				      width, height, format->width,
				      format->height);

	reg_list = ov8858->num_lanes == 4
		 ? mode->reg_modes.mode_4lanes
		 : mode->reg_modes.mode_2lanes;

	ret = ov8858_write_array(ov8858, reg_list);
	if (ret)
		return ret;

	/* 200 usec max to let PLL stabilize. */
	fsleep(200);

	ret = __v4l2_ctrl_handler_setup(&ov8858->ctrl_handler);
	if (ret)
		return ret;

	ret = ov8858_write(ov8858, OV8858_REG_SC_CTRL0100,
			   OV8858_MODE_STREAMING, NULL);
	if (ret)
		return ret;

	/* t5 (fixed) = 10msec before entering streaming state */
	fsleep(10000);

	return 0;
}

static int ov8858_stop_stream(struct ov8858 *ov8858)
{
	return ov8858_write(ov8858, OV8858_REG_SC_CTRL0100,
			    OV8858_MODE_SW_STANDBY, NULL);
}

static int ov8858_s_stream(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct ov8858 *ov8858 = sd_to_ov8858(sd);
	struct v4l2_subdev_state *state;
	int ret = 0;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	if (on) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			goto unlock_and_return;

		ret = ov8858_start_stream(ov8858, state);
		if (ret) {
			dev_err(&client->dev, "Failed to start streaming\n");
			pm_runtime_put_sync(&client->dev);
			goto unlock_and_return;
		}
	} else {
		ov8858_stop_stream(ov8858);
		pm_runtime_mark_last_busy(&client->dev);
		pm_runtime_put_autosuspend(&client->dev);
	}

unlock_and_return:
	v4l2_subdev_unlock_state(state);

	return ret;
}

static const struct v4l2_subdev_video_ops ov8858_video_ops = {
	.s_stream = ov8858_s_stream,
};

/* ----------------------------------------------------------------------------
 * Pad ops
 */

static int ov8858_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	struct ov8858 *ov8858 = sd_to_ov8858(sd);
	const struct ov8858_mode *mode;
	s64 h_blank, vblank_def;

	mode = v4l2_find_nearest_size(ov8858_modes, ARRAY_SIZE(ov8858_modes),
				      width, height, fmt->format.width,
				      fmt->format.height);

	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;

	/* Store the format in the current subdev state. */
	*v4l2_subdev_state_get_format(state, 0) =  fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	/* Adjust control limits when a new mode is applied. */
	h_blank = mode->hts_def - mode->width;
	__v4l2_ctrl_modify_range(ov8858->hblank, h_blank, h_blank, 1,
				 h_blank);

	vblank_def = mode->vts_def - mode->height;
	__v4l2_ctrl_modify_range(ov8858->vblank, vblank_def,
				 OV8858_VTS_MAX - mode->height, 1,
				 vblank_def);

	return 0;
}

static int ov8858_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(ov8858_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width  = ov8858_modes[fse->index].width;
	fse->max_width  = ov8858_modes[fse->index].width;
	fse->max_height = ov8858_modes[fse->index].height;
	fse->min_height = ov8858_modes[fse->index].height;

	return 0;
}

static int ov8858_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov8858_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	const struct ov8858_mode *def_mode = &ov8858_modes[0];
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.format = {
			.width = def_mode->width,
			.height = def_mode->height,
		},
	};

	ov8858_set_fmt(sd, sd_state, &fmt);

	return 0;
}

static const struct v4l2_subdev_pad_ops ov8858_pad_ops = {
	.enum_mbus_code = ov8858_enum_mbus_code,
	.enum_frame_size = ov8858_enum_frame_sizes,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ov8858_set_fmt,
};

static const struct v4l2_subdev_core_ops ov8858_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_ops ov8858_subdev_ops = {
	.core	= &ov8858_core_ops,
	.video	= &ov8858_video_ops,
	.pad	= &ov8858_pad_ops,
};

static const struct v4l2_subdev_internal_ops ov8858_internal_ops = {
	.init_state = ov8858_init_state,
};

/* ----------------------------------------------------------------------------
 * Controls handling
 */

static int ov8858_enable_test_pattern(struct ov8858 *ov8858, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | OV8858_TEST_PATTERN_ENABLE;
	else
		val = OV8858_TEST_PATTERN_DISABLE;

	return ov8858_write(ov8858, OV8858_REG_TEST_PATTERN, val, NULL);
}

static int ov8858_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov8858 *ov8858 = container_of(ctrl->handler,
					     struct ov8858, ctrl_handler);

	struct i2c_client *client = v4l2_get_subdevdata(&ov8858->subdev);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_subdev_state *state;
	u16 digi_gain;
	s64 max_exp;
	int ret;

	/*
	 * The control handler and the subdev state use the same mutex and the
	 * mutex is guaranteed to be locked:
	 * - by the core when s_ctrl is called int the VIDIOC_S_CTRL call path
	 * - by the driver when s_ctrl is called in the s_stream(1) call path
	 */
	state = v4l2_subdev_get_locked_active_state(&ov8858->subdev);
	format = v4l2_subdev_state_get_format(state, 0);

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max_exp = format->height + ctrl->val - OV8858_EXPOSURE_MARGIN;
		__v4l2_ctrl_modify_range(ov8858->exposure,
					 ov8858->exposure->minimum, max_exp,
					 ov8858->exposure->step,
					 ov8858->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of exposure are fractional part */
		ret = ov8858_write(ov8858, OV8858_REG_LONG_EXPO,
				   ctrl->val << 4, NULL);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov8858_write(ov8858, OV8858_REG_LONG_GAIN,
				   ctrl->val, NULL);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		/*
		 * Digital gain is assembled as:
		 * 0x350a[7:0] = dgain[13:6]
		 * 0x350b[5:0] = dgain[5:0]
		 * Reassemble the control value to write it in one go.
		 */
		digi_gain = (ctrl->val & OV8858_LONG_DIGIGAIN_L_MASK)
			  | ((ctrl->val & OV8858_LONG_DIGIGAIN_H_MASK) <<
			      OV8858_LONG_DIGIGAIN_H_SHIFT);
		ret = ov8858_write(ov8858, OV8858_REG_LONG_DIGIGAIN,
				   digi_gain, NULL);
		break;
	case V4L2_CID_VBLANK:
		ret = ov8858_write(ov8858, OV8858_REG_VTS,
				   ctrl->val + format->height, NULL);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov8858_enable_test_pattern(ov8858, ctrl->val);
		break;
	default:
		ret = -EINVAL;
		dev_warn(&client->dev, "%s Unhandled id: 0x%x\n",
			 __func__, ctrl->id);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov8858_ctrl_ops = {
	.s_ctrl = ov8858_set_ctrl,
};

/* ----------------------------------------------------------------------------
 * Power Management
 */

static int ov8858_power_on(struct ov8858 *ov8858)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov8858->subdev);
	struct device *dev = &client->dev;
	unsigned long delay_us;
	int ret;

	if (clk_get_rate(ov8858->xvclk) != OV8858_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");

	ret = clk_prepare_enable(ov8858->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(ov8858_supply_names),
				    ov8858->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	/*
	 * The chip manual only suggests 8192 cycles prior to first SCCB
	 * transaction, but a double sleep between the release of gpios
	 * helps with sporadic failures observed at probe time.
	 */
	delay_us = DIV_ROUND_UP(8192, OV8858_XVCLK_FREQ / 1000 / 1000);

	gpiod_set_value_cansleep(ov8858->reset_gpio, 0);
	fsleep(delay_us);
	gpiod_set_value_cansleep(ov8858->pwdn_gpio, 0);
	fsleep(delay_us);

	return 0;

disable_clk:
	clk_disable_unprepare(ov8858->xvclk);

	return ret;
}

static void ov8858_power_off(struct ov8858 *ov8858)
{
	gpiod_set_value_cansleep(ov8858->pwdn_gpio, 1);
	clk_disable_unprepare(ov8858->xvclk);
	gpiod_set_value_cansleep(ov8858->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(ov8858_supply_names),
			       ov8858->supplies);
}

static int ov8858_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov8858 *ov8858 = sd_to_ov8858(sd);

	return ov8858_power_on(ov8858);
}

static int ov8858_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov8858 *ov8858 = sd_to_ov8858(sd);

	ov8858_power_off(ov8858);

	return 0;
}

static const struct dev_pm_ops ov8858_pm_ops = {
	SET_RUNTIME_PM_OPS(ov8858_runtime_suspend,
			   ov8858_runtime_resume, NULL)
};

/* ----------------------------------------------------------------------------
 * Probe and initialization
 */

static int ov8858_init_ctrls(struct ov8858 *ov8858)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov8858->subdev);
	struct v4l2_ctrl_handler *handler = &ov8858->ctrl_handler;
	const struct ov8858_mode *mode = &ov8858_modes[0];
	struct v4l2_fwnode_device_properties props;
	s64 exposure_max, vblank_def;
	unsigned int pixel_rate;
	struct v4l2_ctrl *ctrl;
	u32 h_blank;
	int ret;

	ret = v4l2_ctrl_handler_init(handler, 10);
	if (ret)
		return ret;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	/* pixel rate = link frequency * 2 * lanes / bpp */
	pixel_rate = OV8858_LINK_FREQ * 2 * ov8858->num_lanes / 10;
	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, pixel_rate, 1, pixel_rate);

	h_blank = mode->hts_def - mode->width;
	ov8858->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					   h_blank, h_blank, 1, h_blank);
	if (ov8858->hblank)
		ov8858->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ov8858->vblank = v4l2_ctrl_new_std(handler, &ov8858_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_def,
					   OV8858_VTS_MAX - mode->height,
					   1, vblank_def);

	exposure_max = mode->vts_def - OV8858_EXPOSURE_MARGIN;
	ov8858->exposure = v4l2_ctrl_new_std(handler, &ov8858_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     OV8858_EXPOSURE_MIN,
					     exposure_max, OV8858_EXPOSURE_STEP,
					     mode->exp_def);

	v4l2_ctrl_new_std(handler, &ov8858_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV8858_LONG_GAIN_MIN, OV8858_LONG_GAIN_MAX,
			  OV8858_LONG_GAIN_STEP, OV8858_LONG_GAIN_DEFAULT);

	v4l2_ctrl_new_std(handler, &ov8858_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV8858_LONG_DIGIGAIN_MIN, OV8858_LONG_DIGIGAIN_MAX,
			  OV8858_LONG_DIGIGAIN_STEP,
			  OV8858_LONG_DIGIGAIN_DEFAULT);

	v4l2_ctrl_new_std_menu_items(handler, &ov8858_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov8858_test_pattern_menu) - 1,
				     0, 0, ov8858_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		goto err_free_handler;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto err_free_handler;

	ret = v4l2_ctrl_new_fwnode_properties(handler, &ov8858_ctrl_ops,
					      &props);
	if (ret)
		goto err_free_handler;

	ov8858->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	dev_err(&client->dev, "Failed to init controls: %d\n", ret);
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov8858_check_sensor_id(struct ov8858 *ov8858)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov8858->subdev);
	u32 id = 0;
	int ret;

	ret = ov8858_read(ov8858, OV8858_REG_CHIP_ID, &id);
	if (ret)
		return ret;

	if (id != OV8858_CHIP_ID) {
		dev_err(&client->dev, "Unexpected sensor id 0x%x\n", id);
		return -ENODEV;
	}

	ret = ov8858_read(ov8858, OV8858_REG_SUB_ID, &id);
	if (ret)
		return ret;

	dev_info(&client->dev, "Detected OV8858 sensor, revision 0x%x\n", id);

	if (id == OV8858_R2A) {
		/* R2A supports 2 and 4 lanes modes. */
		ov8858->global_regs = ov8858->num_lanes == 4
				    ? ov8858_global_regs_r2a_4lane
				    : ov8858_global_regs_r2a_2lane;
	} else if (ov8858->num_lanes == 2) {
		/*
		 * R1A only supports 2 lanes mode and it's only partially
		 * supported.
		 */
		ov8858->global_regs = ov8858_global_regs_r1a;
		dev_warn(&client->dev, "R1A may not work well!\n");
	} else {
		dev_err(&client->dev,
			"Unsupported number of data lanes for R1A revision.\n");
		return -EINVAL;
	}

	return 0;
}

static int ov8858_configure_regulators(struct ov8858 *ov8858)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov8858->subdev);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ov8858_supply_names); i++)
		ov8858->supplies[i].supply = ov8858_supply_names[i];

	return devm_regulator_bulk_get(&client->dev,
				       ARRAY_SIZE(ov8858_supply_names),
				       ov8858->supplies);
}

static int ov8858_parse_of(struct ov8858 *ov8858)
{
	struct v4l2_fwnode_endpoint vep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	struct i2c_client *client = v4l2_get_subdevdata(&ov8858->subdev);
	struct device *dev = &client->dev;
	struct fwnode_handle *endpoint;
	int ret;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(endpoint, &vep);
	fwnode_handle_put(endpoint);
	if (ret) {
		dev_err(dev, "Failed to parse endpoint: %d\n", ret);
		return ret;
	}

	ov8858->num_lanes = vep.bus.mipi_csi2.num_data_lanes;
	switch (ov8858->num_lanes) {
	case 4:
	case 2:
		break;
	default:
		dev_err(dev, "Unsupported number of data lanes %u\n",
			ov8858->num_lanes);
		return -EINVAL;
	}

	return 0;
}

static int ov8858_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct v4l2_subdev *sd;
	struct ov8858 *ov8858;
	int ret;

	ov8858 = devm_kzalloc(dev, sizeof(*ov8858), GFP_KERNEL);
	if (!ov8858)
		return -ENOMEM;

	ov8858->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov8858->xvclk))
		return dev_err_probe(dev, PTR_ERR(ov8858->xvclk),
				     "Failed to get xvclk\n");

	ov8858->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(ov8858->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ov8858->reset_gpio),
				     "Failed to get reset gpio\n");

	ov8858->pwdn_gpio = devm_gpiod_get_optional(dev, "powerdown",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(ov8858->pwdn_gpio))
		return dev_err_probe(dev, PTR_ERR(ov8858->pwdn_gpio),
				     "Failed to get powerdown gpio\n");

	v4l2_i2c_subdev_init(&ov8858->subdev, client, &ov8858_subdev_ops);
	ov8858->subdev.internal_ops = &ov8858_internal_ops;

	ret = ov8858_configure_regulators(ov8858);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ret = ov8858_parse_of(ov8858);
	if (ret)
		return ret;

	ret = ov8858_init_ctrls(ov8858);
	if (ret)
		return ret;

	sd = &ov8858->subdev;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	ov8858->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &ov8858->pad);
	if (ret < 0)
		goto err_free_handler;

	sd->state_lock = ov8858->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(sd);
	if (ret < 0) {
		dev_err(&client->dev, "Subdev initialization error %d\n", ret);
		goto err_clean_entity;
	}

	ret = ov8858_power_on(ov8858);
	if (ret)
		goto err_clean_entity;

	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);

	ret = ov8858_check_sensor_id(ov8858);
	if (ret)
		goto err_power_off;

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	ret = v4l2_async_register_subdev_sensor(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_power_off;
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

err_power_off:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	ov8858_power_off(ov8858);
err_clean_entity:
	media_entity_cleanup(&sd->entity);
err_free_handler:
	v4l2_ctrl_handler_free(&ov8858->ctrl_handler);

	return ret;
}

static void ov8858_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov8858 *ov8858 = sd_to_ov8858(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&ov8858->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		ov8858_power_off(ov8858);
	pm_runtime_set_suspended(&client->dev);
}

static const struct of_device_id ov8858_of_match[] = {
	{ .compatible = "ovti,ov8858" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ov8858_of_match);

static struct i2c_driver ov8858_i2c_driver = {
	.driver = {
		.name = "ov8858",
		.pm = &ov8858_pm_ops,
		.of_match_table = ov8858_of_match,
	},
	.probe		= ov8858_probe,
	.remove		= ov8858_remove,
};

module_i2c_driver(ov8858_i2c_driver);

MODULE_DESCRIPTION("OmniVision OV8858 sensor driver");
MODULE_LICENSE("GPL");
