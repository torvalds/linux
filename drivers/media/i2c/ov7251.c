// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the OV7251 camera sensor.
 *
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2018, Linaro Ltd.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define OV7251_SC_MODE_SELECT		0x0100
#define OV7251_SC_MODE_SELECT_SW_STANDBY	0x0
#define OV7251_SC_MODE_SELECT_STREAMING		0x1

#define OV7251_CHIP_ID_HIGH		0x300a
#define OV7251_CHIP_ID_HIGH_BYTE	0x77
#define OV7251_CHIP_ID_LOW		0x300b
#define OV7251_CHIP_ID_LOW_BYTE		0x50
#define OV7251_SC_GP_IO_IN1		0x3029
#define OV7251_AEC_EXPO_0		0x3500
#define OV7251_AEC_EXPO_1		0x3501
#define OV7251_AEC_EXPO_2		0x3502
#define OV7251_AEC_AGC_ADJ_0		0x350a
#define OV7251_AEC_AGC_ADJ_1		0x350b
#define OV7251_TIMING_FORMAT1		0x3820
#define OV7251_TIMING_FORMAT1_VFLIP	BIT(2)
#define OV7251_TIMING_FORMAT2		0x3821
#define OV7251_TIMING_FORMAT2_MIRROR	BIT(2)
#define OV7251_PRE_ISP_00		0x5e00
#define OV7251_PRE_ISP_00_TEST_PATTERN	BIT(7)
#define OV7251_PLL1_PRE_DIV_REG		0x30b4
#define OV7251_PLL1_MULT_REG		0x30b3
#define OV7251_PLL1_DIVIDER_REG		0x30b1
#define OV7251_PLL1_PIX_DIV_REG		0x30b0
#define OV7251_PLL1_MIPI_DIV_REG	0x30b5
#define OV7251_PLL2_PRE_DIV_REG		0x3098
#define OV7251_PLL2_MULT_REG		0x3099
#define OV7251_PLL2_DIVIDER_REG		0x309d
#define OV7251_PLL2_SYS_DIV_REG		0x309a
#define OV7251_PLL2_ADC_DIV_REG		0x309b

#define OV7251_NATIVE_WIDTH		656
#define OV7251_NATIVE_HEIGHT		496
#define OV7251_ACTIVE_START_LEFT	4
#define OV7251_ACTIVE_START_TOP		4
#define OV7251_ACTIVE_WIDTH		648
#define OV7251_ACTIVE_HEIGHT		488

#define OV7251_FIXED_PPL		928
#define OV7251_TIMING_VTS_REG		0x380e
#define OV7251_TIMING_MIN_VTS		1
#define OV7251_TIMING_MAX_VTS		0xffff
#define OV7251_INTEGRATION_MARGIN	20

struct reg_value {
	u16 reg;
	u8 val;
};

struct ov7251_mode_info {
	u32 width;
	u32 height;
	u32 vts;
	const struct reg_value *data;
	u32 data_size;
	u32 pixel_clock;
	u32 link_freq;
	u16 exposure_max;
	u16 exposure_def;
	struct v4l2_fract timeperframe;
};

struct ov7251_pll1_cfg {
	unsigned int pre_div;
	unsigned int mult;
	unsigned int div;
	unsigned int pix_div;
	unsigned int mipi_div;
};

struct ov7251_pll2_cfg {
	unsigned int pre_div;
	unsigned int mult;
	unsigned int div;
	unsigned int sys_div;
	unsigned int adc_div;
};

/*
 * Rubbish ordering, but only PLL1 needs to have a separate configuration per
 * link frequency and the array member needs to be last.
 */
struct ov7251_pll_cfgs {
	const struct ov7251_pll2_cfg *pll2;
	const struct ov7251_pll1_cfg *pll1[];
};

enum xclk_rate {
	OV7251_19_2_MHZ,
	OV7251_24_MHZ,
	OV7251_NUM_SUPPORTED_RATES
};

enum supported_link_freqs {
	OV7251_LINK_FREQ_240_MHZ,
	OV7251_LINK_FREQ_319_2_MHZ,
	OV7251_NUM_SUPPORTED_LINK_FREQS
};

struct ov7251 {
	struct i2c_client *i2c_client;
	struct device *dev;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_fwnode_endpoint ep;
	struct v4l2_mbus_framefmt fmt;
	struct v4l2_rect crop;
	struct clk *xclk;
	u32 xclk_freq;

	struct regulator *io_regulator;
	struct regulator *core_regulator;
	struct regulator *analog_regulator;

	const struct ov7251_pll_cfgs *pll_cfgs;
	enum supported_link_freqs link_freq_idx;
	const struct ov7251_mode_info *current_mode;

	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *pixel_clock;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;

	/* Cached register values */
	u8 aec_pk_manual;
	u8 pre_isp_00;
	u8 timing_format1;
	u8 timing_format2;

	struct mutex lock; /* lock to protect power state, ctrls and mode */
	bool power_on;

	struct gpio_desc *enable_gpio;
};

static inline struct ov7251 *to_ov7251(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov7251, sd);
}

static const struct ov7251_pll1_cfg ov7251_pll1_cfg_19_2_mhz_240_mhz = {
	.pre_div = 0x03,
	.mult = 0x4b,
	.div = 0x01,
	.pix_div = 0x0a,
	.mipi_div = 0x05,
};

static const struct ov7251_pll1_cfg ov7251_pll1_cfg_19_2_mhz_319_2_mhz = {
	.pre_div = 0x01,
	.mult = 0x85,
	.div = 0x04,
	.pix_div = 0x0a,
	.mipi_div = 0x05,
};

static const struct ov7251_pll1_cfg ov7251_pll1_cfg_24_mhz_240_mhz = {
	.pre_div = 0x03,
	.mult = 0x64,
	.div = 0x01,
	.pix_div = 0x0a,
	.mipi_div = 0x05,
};

static const struct ov7251_pll1_cfg ov7251_pll1_cfg_24_mhz_319_2_mhz = {
	.pre_div = 0x05,
	.mult = 0x85,
	.div = 0x02,
	.pix_div = 0x0a,
	.mipi_div = 0x05,
};

static const struct ov7251_pll2_cfg ov7251_pll2_cfg_19_2_mhz = {
	.pre_div = 0x04,
	.mult = 0x32,
	.div = 0x00,
	.sys_div = 0x05,
	.adc_div = 0x04,
};

static const struct ov7251_pll2_cfg ov7251_pll2_cfg_24_mhz = {
	.pre_div = 0x04,
	.mult = 0x28,
	.div = 0x00,
	.sys_div = 0x05,
	.adc_div = 0x04,
};

static const struct ov7251_pll_cfgs ov7251_pll_cfgs_19_2_mhz = {
	.pll2 = &ov7251_pll2_cfg_19_2_mhz,
	.pll1 = {
		[OV7251_LINK_FREQ_240_MHZ] = &ov7251_pll1_cfg_19_2_mhz_240_mhz,
		[OV7251_LINK_FREQ_319_2_MHZ] = &ov7251_pll1_cfg_19_2_mhz_319_2_mhz,
	},
};

static const struct ov7251_pll_cfgs ov7251_pll_cfgs_24_mhz = {
	.pll2 = &ov7251_pll2_cfg_24_mhz,
	.pll1 = {
		[OV7251_LINK_FREQ_240_MHZ] = &ov7251_pll1_cfg_24_mhz_240_mhz,
		[OV7251_LINK_FREQ_319_2_MHZ] = &ov7251_pll1_cfg_24_mhz_319_2_mhz,
	},
};

static const struct ov7251_pll_cfgs *ov7251_pll_cfgs[] = {
	[OV7251_19_2_MHZ] = &ov7251_pll_cfgs_19_2_mhz,
	[OV7251_24_MHZ] = &ov7251_pll_cfgs_24_mhz,
};

static const struct reg_value ov7251_global_init_setting[] = {
	{ 0x0103, 0x01 },
	{ 0x303b, 0x02 },
};

static const struct reg_value ov7251_setting_vga_30fps[] = {
	{ 0x3005, 0x00 },
	{ 0x3012, 0xc0 },
	{ 0x3013, 0xd2 },
	{ 0x3014, 0x04 },
	{ 0x3016, 0xf0 },
	{ 0x3017, 0xf0 },
	{ 0x3018, 0xf0 },
	{ 0x301a, 0xf0 },
	{ 0x301b, 0xf0 },
	{ 0x301c, 0xf0 },
	{ 0x3023, 0x05 },
	{ 0x3037, 0xf0 },
	{ 0x3106, 0xda },
	{ 0x3503, 0x07 },
	{ 0x3509, 0x10 },
	{ 0x3600, 0x1c },
	{ 0x3602, 0x62 },
	{ 0x3620, 0xb7 },
	{ 0x3622, 0x04 },
	{ 0x3626, 0x21 },
	{ 0x3627, 0x30 },
	{ 0x3630, 0x44 },
	{ 0x3631, 0x35 },
	{ 0x3634, 0x60 },
	{ 0x3636, 0x00 },
	{ 0x3662, 0x01 },
	{ 0x3663, 0x70 },
	{ 0x3664, 0x50 },
	{ 0x3666, 0x0a },
	{ 0x3669, 0x1a },
	{ 0x366a, 0x00 },
	{ 0x366b, 0x50 },
	{ 0x3673, 0x01 },
	{ 0x3674, 0xff },
	{ 0x3675, 0x03 },
	{ 0x3705, 0xc1 },
	{ 0x3709, 0x40 },
	{ 0x373c, 0x08 },
	{ 0x3742, 0x00 },
	{ 0x3757, 0xb3 },
	{ 0x3788, 0x00 },
	{ 0x37a8, 0x01 },
	{ 0x37a9, 0xc0 },
	{ 0x3800, 0x00 },
	{ 0x3801, 0x04 },
	{ 0x3802, 0x00 },
	{ 0x3803, 0x04 },
	{ 0x3804, 0x02 },
	{ 0x3805, 0x8b },
	{ 0x3806, 0x01 },
	{ 0x3807, 0xeb },
	{ 0x3808, 0x02 }, /* width high */
	{ 0x3809, 0x80 }, /* width low */
	{ 0x380a, 0x01 }, /* height high */
	{ 0x380b, 0xe0 }, /* height low */
	{ 0x380c, 0x03 }, /* total horiz timing high */
	{ 0x380d, 0xa0 }, /* total horiz timing low */
	{ 0x380e, 0x06 }, /* total vertical timing high */
	{ 0x380f, 0xbc }, /* total vertical timing low */
	{ 0x3810, 0x00 },
	{ 0x3811, 0x04 },
	{ 0x3812, 0x00 },
	{ 0x3813, 0x05 },
	{ 0x3814, 0x11 },
	{ 0x3815, 0x11 },
	{ 0x3820, 0x40 },
	{ 0x3821, 0x00 },
	{ 0x382f, 0x0e },
	{ 0x3832, 0x00 },
	{ 0x3833, 0x05 },
	{ 0x3834, 0x00 },
	{ 0x3835, 0x0c },
	{ 0x3837, 0x00 },
	{ 0x3b80, 0x00 },
	{ 0x3b81, 0xa5 },
	{ 0x3b82, 0x10 },
	{ 0x3b83, 0x00 },
	{ 0x3b84, 0x08 },
	{ 0x3b85, 0x00 },
	{ 0x3b86, 0x01 },
	{ 0x3b87, 0x00 },
	{ 0x3b88, 0x00 },
	{ 0x3b89, 0x00 },
	{ 0x3b8a, 0x00 },
	{ 0x3b8b, 0x05 },
	{ 0x3b8c, 0x00 },
	{ 0x3b8d, 0x00 },
	{ 0x3b8e, 0x00 },
	{ 0x3b8f, 0x1a },
	{ 0x3b94, 0x05 },
	{ 0x3b95, 0xf2 },
	{ 0x3b96, 0x40 },
	{ 0x3c00, 0x89 },
	{ 0x3c01, 0x63 },
	{ 0x3c02, 0x01 },
	{ 0x3c03, 0x00 },
	{ 0x3c04, 0x00 },
	{ 0x3c05, 0x03 },
	{ 0x3c06, 0x00 },
	{ 0x3c07, 0x06 },
	{ 0x3c0c, 0x01 },
	{ 0x3c0d, 0xd0 },
	{ 0x3c0e, 0x02 },
	{ 0x3c0f, 0x0a },
	{ 0x4001, 0x42 },
	{ 0x4004, 0x04 },
	{ 0x4005, 0x00 },
	{ 0x404e, 0x01 },
	{ 0x4300, 0xff },
	{ 0x4301, 0x00 },
	{ 0x4315, 0x00 },
	{ 0x4501, 0x48 },
	{ 0x4600, 0x00 },
	{ 0x4601, 0x4e },
	{ 0x4801, 0x0f },
	{ 0x4806, 0x0f },
	{ 0x4819, 0xaa },
	{ 0x4823, 0x3e },
	{ 0x4837, 0x19 },
	{ 0x4a0d, 0x00 },
	{ 0x4a47, 0x7f },
	{ 0x4a49, 0xf0 },
	{ 0x4a4b, 0x30 },
	{ 0x5000, 0x85 },
	{ 0x5001, 0x80 },
};

static const struct reg_value ov7251_setting_vga_60fps[] = {
	{ 0x3005, 0x00 },
	{ 0x3012, 0xc0 },
	{ 0x3013, 0xd2 },
	{ 0x3014, 0x04 },
	{ 0x3016, 0x10 },
	{ 0x3017, 0x00 },
	{ 0x3018, 0x00 },
	{ 0x301a, 0x00 },
	{ 0x301b, 0x00 },
	{ 0x301c, 0x00 },
	{ 0x3023, 0x05 },
	{ 0x3037, 0xf0 },
	{ 0x3106, 0xda },
	{ 0x3503, 0x07 },
	{ 0x3509, 0x10 },
	{ 0x3600, 0x1c },
	{ 0x3602, 0x62 },
	{ 0x3620, 0xb7 },
	{ 0x3622, 0x04 },
	{ 0x3626, 0x21 },
	{ 0x3627, 0x30 },
	{ 0x3630, 0x44 },
	{ 0x3631, 0x35 },
	{ 0x3634, 0x60 },
	{ 0x3636, 0x00 },
	{ 0x3662, 0x01 },
	{ 0x3663, 0x70 },
	{ 0x3664, 0x50 },
	{ 0x3666, 0x0a },
	{ 0x3669, 0x1a },
	{ 0x366a, 0x00 },
	{ 0x366b, 0x50 },
	{ 0x3673, 0x01 },
	{ 0x3674, 0xff },
	{ 0x3675, 0x03 },
	{ 0x3705, 0xc1 },
	{ 0x3709, 0x40 },
	{ 0x373c, 0x08 },
	{ 0x3742, 0x00 },
	{ 0x3757, 0xb3 },
	{ 0x3788, 0x00 },
	{ 0x37a8, 0x01 },
	{ 0x37a9, 0xc0 },
	{ 0x3800, 0x00 },
	{ 0x3801, 0x04 },
	{ 0x3802, 0x00 },
	{ 0x3803, 0x04 },
	{ 0x3804, 0x02 },
	{ 0x3805, 0x8b },
	{ 0x3806, 0x01 },
	{ 0x3807, 0xeb },
	{ 0x3808, 0x02 }, /* width high */
	{ 0x3809, 0x80 }, /* width low */
	{ 0x380a, 0x01 }, /* height high */
	{ 0x380b, 0xe0 }, /* height low */
	{ 0x380c, 0x03 }, /* total horiz timing high */
	{ 0x380d, 0xa0 }, /* total horiz timing low */
	{ 0x380e, 0x03 }, /* total vertical timing high */
	{ 0x380f, 0x5c }, /* total vertical timing low */
	{ 0x3810, 0x00 },
	{ 0x3811, 0x04 },
	{ 0x3812, 0x00 },
	{ 0x3813, 0x05 },
	{ 0x3814, 0x11 },
	{ 0x3815, 0x11 },
	{ 0x3820, 0x40 },
	{ 0x3821, 0x00 },
	{ 0x382f, 0x0e },
	{ 0x3832, 0x00 },
	{ 0x3833, 0x05 },
	{ 0x3834, 0x00 },
	{ 0x3835, 0x0c },
	{ 0x3837, 0x00 },
	{ 0x3b80, 0x00 },
	{ 0x3b81, 0xa5 },
	{ 0x3b82, 0x10 },
	{ 0x3b83, 0x00 },
	{ 0x3b84, 0x08 },
	{ 0x3b85, 0x00 },
	{ 0x3b86, 0x01 },
	{ 0x3b87, 0x00 },
	{ 0x3b88, 0x00 },
	{ 0x3b89, 0x00 },
	{ 0x3b8a, 0x00 },
	{ 0x3b8b, 0x05 },
	{ 0x3b8c, 0x00 },
	{ 0x3b8d, 0x00 },
	{ 0x3b8e, 0x00 },
	{ 0x3b8f, 0x1a },
	{ 0x3b94, 0x05 },
	{ 0x3b95, 0xf2 },
	{ 0x3b96, 0x40 },
	{ 0x3c00, 0x89 },
	{ 0x3c01, 0x63 },
	{ 0x3c02, 0x01 },
	{ 0x3c03, 0x00 },
	{ 0x3c04, 0x00 },
	{ 0x3c05, 0x03 },
	{ 0x3c06, 0x00 },
	{ 0x3c07, 0x06 },
	{ 0x3c0c, 0x01 },
	{ 0x3c0d, 0xd0 },
	{ 0x3c0e, 0x02 },
	{ 0x3c0f, 0x0a },
	{ 0x4001, 0x42 },
	{ 0x4004, 0x04 },
	{ 0x4005, 0x00 },
	{ 0x404e, 0x01 },
	{ 0x4300, 0xff },
	{ 0x4301, 0x00 },
	{ 0x4315, 0x00 },
	{ 0x4501, 0x48 },
	{ 0x4600, 0x00 },
	{ 0x4601, 0x4e },
	{ 0x4801, 0x0f },
	{ 0x4806, 0x0f },
	{ 0x4819, 0xaa },
	{ 0x4823, 0x3e },
	{ 0x4837, 0x19 },
	{ 0x4a0d, 0x00 },
	{ 0x4a47, 0x7f },
	{ 0x4a49, 0xf0 },
	{ 0x4a4b, 0x30 },
	{ 0x5000, 0x85 },
	{ 0x5001, 0x80 },
};

static const struct reg_value ov7251_setting_vga_90fps[] = {
	{ 0x3005, 0x00 },
	{ 0x3012, 0xc0 },
	{ 0x3013, 0xd2 },
	{ 0x3014, 0x04 },
	{ 0x3016, 0x10 },
	{ 0x3017, 0x00 },
	{ 0x3018, 0x00 },
	{ 0x301a, 0x00 },
	{ 0x301b, 0x00 },
	{ 0x301c, 0x00 },
	{ 0x3023, 0x05 },
	{ 0x3037, 0xf0 },
	{ 0x3106, 0xda },
	{ 0x3503, 0x07 },
	{ 0x3509, 0x10 },
	{ 0x3600, 0x1c },
	{ 0x3602, 0x62 },
	{ 0x3620, 0xb7 },
	{ 0x3622, 0x04 },
	{ 0x3626, 0x21 },
	{ 0x3627, 0x30 },
	{ 0x3630, 0x44 },
	{ 0x3631, 0x35 },
	{ 0x3634, 0x60 },
	{ 0x3636, 0x00 },
	{ 0x3662, 0x01 },
	{ 0x3663, 0x70 },
	{ 0x3664, 0x50 },
	{ 0x3666, 0x0a },
	{ 0x3669, 0x1a },
	{ 0x366a, 0x00 },
	{ 0x366b, 0x50 },
	{ 0x3673, 0x01 },
	{ 0x3674, 0xff },
	{ 0x3675, 0x03 },
	{ 0x3705, 0xc1 },
	{ 0x3709, 0x40 },
	{ 0x373c, 0x08 },
	{ 0x3742, 0x00 },
	{ 0x3757, 0xb3 },
	{ 0x3788, 0x00 },
	{ 0x37a8, 0x01 },
	{ 0x37a9, 0xc0 },
	{ 0x3800, 0x00 },
	{ 0x3801, 0x04 },
	{ 0x3802, 0x00 },
	{ 0x3803, 0x04 },
	{ 0x3804, 0x02 },
	{ 0x3805, 0x8b },
	{ 0x3806, 0x01 },
	{ 0x3807, 0xeb },
	{ 0x3808, 0x02 }, /* width high */
	{ 0x3809, 0x80 }, /* width low */
	{ 0x380a, 0x01 }, /* height high */
	{ 0x380b, 0xe0 }, /* height low */
	{ 0x380c, 0x03 }, /* total horiz timing high */
	{ 0x380d, 0xa0 }, /* total horiz timing low */
	{ 0x380e, 0x02 }, /* total vertical timing high */
	{ 0x380f, 0x3c }, /* total vertical timing low */
	{ 0x3810, 0x00 },
	{ 0x3811, 0x04 },
	{ 0x3812, 0x00 },
	{ 0x3813, 0x05 },
	{ 0x3814, 0x11 },
	{ 0x3815, 0x11 },
	{ 0x3820, 0x40 },
	{ 0x3821, 0x00 },
	{ 0x382f, 0x0e },
	{ 0x3832, 0x00 },
	{ 0x3833, 0x05 },
	{ 0x3834, 0x00 },
	{ 0x3835, 0x0c },
	{ 0x3837, 0x00 },
	{ 0x3b80, 0x00 },
	{ 0x3b81, 0xa5 },
	{ 0x3b82, 0x10 },
	{ 0x3b83, 0x00 },
	{ 0x3b84, 0x08 },
	{ 0x3b85, 0x00 },
	{ 0x3b86, 0x01 },
	{ 0x3b87, 0x00 },
	{ 0x3b88, 0x00 },
	{ 0x3b89, 0x00 },
	{ 0x3b8a, 0x00 },
	{ 0x3b8b, 0x05 },
	{ 0x3b8c, 0x00 },
	{ 0x3b8d, 0x00 },
	{ 0x3b8e, 0x00 },
	{ 0x3b8f, 0x1a },
	{ 0x3b94, 0x05 },
	{ 0x3b95, 0xf2 },
	{ 0x3b96, 0x40 },
	{ 0x3c00, 0x89 },
	{ 0x3c01, 0x63 },
	{ 0x3c02, 0x01 },
	{ 0x3c03, 0x00 },
	{ 0x3c04, 0x00 },
	{ 0x3c05, 0x03 },
	{ 0x3c06, 0x00 },
	{ 0x3c07, 0x06 },
	{ 0x3c0c, 0x01 },
	{ 0x3c0d, 0xd0 },
	{ 0x3c0e, 0x02 },
	{ 0x3c0f, 0x0a },
	{ 0x4001, 0x42 },
	{ 0x4004, 0x04 },
	{ 0x4005, 0x00 },
	{ 0x404e, 0x01 },
	{ 0x4300, 0xff },
	{ 0x4301, 0x00 },
	{ 0x4315, 0x00 },
	{ 0x4501, 0x48 },
	{ 0x4600, 0x00 },
	{ 0x4601, 0x4e },
	{ 0x4801, 0x0f },
	{ 0x4806, 0x0f },
	{ 0x4819, 0xaa },
	{ 0x4823, 0x3e },
	{ 0x4837, 0x19 },
	{ 0x4a0d, 0x00 },
	{ 0x4a47, 0x7f },
	{ 0x4a49, 0xf0 },
	{ 0x4a4b, 0x30 },
	{ 0x5000, 0x85 },
	{ 0x5001, 0x80 },
};

static const unsigned long supported_xclk_rates[] = {
	[OV7251_19_2_MHZ] = 19200000,
	[OV7251_24_MHZ] = 24000000,
};

static const s64 link_freq[] = {
	[OV7251_LINK_FREQ_240_MHZ] = 240000000,
	[OV7251_LINK_FREQ_319_2_MHZ] = 319200000,
};

static const s64 pixel_rates[] = {
	[OV7251_LINK_FREQ_240_MHZ] = 48000000,
	[OV7251_LINK_FREQ_319_2_MHZ] = 63840000,
};

static const struct ov7251_mode_info ov7251_mode_info_data[] = {
	{
		.width = 640,
		.height = 480,
		.vts = 1724,
		.data = ov7251_setting_vga_30fps,
		.data_size = ARRAY_SIZE(ov7251_setting_vga_30fps),
		.exposure_max = 1704,
		.exposure_def = 504,
		.timeperframe = {
			.numerator = 100,
			.denominator = 3000
		}
	},
	{
		.width = 640,
		.height = 480,
		.vts = 860,
		.data = ov7251_setting_vga_60fps,
		.data_size = ARRAY_SIZE(ov7251_setting_vga_60fps),
		.exposure_max = 840,
		.exposure_def = 504,
		.timeperframe = {
			.numerator = 100,
			.denominator = 6014
		}
	},
	{
		.width = 640,
		.height = 480,
		.vts = 572,
		.data = ov7251_setting_vga_90fps,
		.data_size = ARRAY_SIZE(ov7251_setting_vga_90fps),
		.exposure_max = 552,
		.exposure_def = 504,
		.timeperframe = {
			.numerator = 100,
			.denominator = 9043
		}
	},
};

static int ov7251_regulators_enable(struct ov7251 *ov7251)
{
	int ret;

	/* OV7251 power up sequence requires core regulator
	 * to be enabled not earlier than io regulator
	 */

	ret = regulator_enable(ov7251->io_regulator);
	if (ret < 0) {
		dev_err(ov7251->dev, "set io voltage failed\n");
		return ret;
	}

	ret = regulator_enable(ov7251->analog_regulator);
	if (ret) {
		dev_err(ov7251->dev, "set analog voltage failed\n");
		goto err_disable_io;
	}

	ret = regulator_enable(ov7251->core_regulator);
	if (ret) {
		dev_err(ov7251->dev, "set core voltage failed\n");
		goto err_disable_analog;
	}

	return 0;

err_disable_analog:
	regulator_disable(ov7251->analog_regulator);

err_disable_io:
	regulator_disable(ov7251->io_regulator);

	return ret;
}

static void ov7251_regulators_disable(struct ov7251 *ov7251)
{
	int ret;

	ret = regulator_disable(ov7251->core_regulator);
	if (ret < 0)
		dev_err(ov7251->dev, "core regulator disable failed\n");

	ret = regulator_disable(ov7251->analog_regulator);
	if (ret < 0)
		dev_err(ov7251->dev, "analog regulator disable failed\n");

	ret = regulator_disable(ov7251->io_regulator);
	if (ret < 0)
		dev_err(ov7251->dev, "io regulator disable failed\n");
}

static int ov7251_write_reg(struct ov7251 *ov7251, u16 reg, u8 val)
{
	u8 regbuf[3];
	int ret;

	regbuf[0] = reg >> 8;
	regbuf[1] = reg & 0xff;
	regbuf[2] = val;

	ret = i2c_master_send(ov7251->i2c_client, regbuf, 3);
	if (ret < 0) {
		dev_err(ov7251->dev, "%s: write reg error %d: reg=%x, val=%x\n",
			__func__, ret, reg, val);
		return ret;
	}

	return 0;
}

static int ov7251_write_seq_regs(struct ov7251 *ov7251, u16 reg, u8 *val,
				 u8 num)
{
	u8 regbuf[5];
	u8 nregbuf = sizeof(reg) + num * sizeof(*val);
	int ret = 0;

	if (nregbuf > sizeof(regbuf))
		return -EINVAL;

	regbuf[0] = reg >> 8;
	regbuf[1] = reg & 0xff;

	memcpy(regbuf + 2, val, num);

	ret = i2c_master_send(ov7251->i2c_client, regbuf, nregbuf);
	if (ret < 0) {
		dev_err(ov7251->dev,
			"%s: write seq regs error %d: first reg=%x\n",
			__func__, ret, reg);
		return ret;
	}

	return 0;
}

static int ov7251_read_reg(struct ov7251 *ov7251, u16 reg, u8 *val)
{
	u8 regbuf[2];
	int ret;

	regbuf[0] = reg >> 8;
	regbuf[1] = reg & 0xff;

	ret = i2c_master_send(ov7251->i2c_client, regbuf, 2);
	if (ret < 0) {
		dev_err(ov7251->dev, "%s: write reg error %d: reg=%x\n",
			__func__, ret, reg);
		return ret;
	}

	ret = i2c_master_recv(ov7251->i2c_client, val, 1);
	if (ret < 0) {
		dev_err(ov7251->dev, "%s: read reg error %d: reg=%x\n",
			__func__, ret, reg);
		return ret;
	}

	return 0;
}

static int ov7251_pll_configure(struct ov7251 *ov7251)
{
	const struct ov7251_pll_cfgs *configs;
	int ret;

	configs = ov7251->pll_cfgs;

	ret = ov7251_write_reg(ov7251, OV7251_PLL1_PRE_DIV_REG,
			       configs->pll1[ov7251->link_freq_idx]->pre_div);
	if (ret < 0)
		return ret;

	ret = ov7251_write_reg(ov7251, OV7251_PLL1_MULT_REG,
			       configs->pll1[ov7251->link_freq_idx]->mult);
	if (ret < 0)
		return ret;
	ret = ov7251_write_reg(ov7251, OV7251_PLL1_DIVIDER_REG,
			       configs->pll1[ov7251->link_freq_idx]->div);
	if (ret < 0)
		return ret;

	ret = ov7251_write_reg(ov7251, OV7251_PLL1_PIX_DIV_REG,
			       configs->pll1[ov7251->link_freq_idx]->pix_div);
	if (ret < 0)
		return ret;

	ret = ov7251_write_reg(ov7251, OV7251_PLL1_MIPI_DIV_REG,
			       configs->pll1[ov7251->link_freq_idx]->mipi_div);
	if (ret < 0)
		return ret;

	ret = ov7251_write_reg(ov7251, OV7251_PLL2_PRE_DIV_REG,
			       configs->pll2->pre_div);
	if (ret < 0)
		return ret;

	ret = ov7251_write_reg(ov7251, OV7251_PLL2_MULT_REG,
			       configs->pll2->mult);
	if (ret < 0)
		return ret;

	ret = ov7251_write_reg(ov7251, OV7251_PLL2_DIVIDER_REG,
			       configs->pll2->div);
	if (ret < 0)
		return ret;

	ret = ov7251_write_reg(ov7251, OV7251_PLL2_SYS_DIV_REG,
			       configs->pll2->sys_div);
	if (ret < 0)
		return ret;

	ret = ov7251_write_reg(ov7251, OV7251_PLL2_ADC_DIV_REG,
			       configs->pll2->adc_div);

	return ret;
}

static int ov7251_set_exposure(struct ov7251 *ov7251, s32 exposure)
{
	u16 reg;
	u8 val[3];

	reg = OV7251_AEC_EXPO_0;
	val[0] = (exposure & 0xf000) >> 12; /* goes to OV7251_AEC_EXPO_0 */
	val[1] = (exposure & 0x0ff0) >> 4;  /* goes to OV7251_AEC_EXPO_1 */
	val[2] = (exposure & 0x000f) << 4;  /* goes to OV7251_AEC_EXPO_2 */

	return ov7251_write_seq_regs(ov7251, reg, val, 3);
}

static int ov7251_set_gain(struct ov7251 *ov7251, s32 gain)
{
	u16 reg;
	u8 val[2];

	reg = OV7251_AEC_AGC_ADJ_0;
	val[0] = (gain & 0x0300) >> 8; /* goes to OV7251_AEC_AGC_ADJ_0 */
	val[1] = gain & 0xff;          /* goes to OV7251_AEC_AGC_ADJ_1 */

	return ov7251_write_seq_regs(ov7251, reg, val, 2);
}

static int ov7251_set_register_array(struct ov7251 *ov7251,
				     const struct reg_value *settings,
				     unsigned int num_settings)
{
	unsigned int i;
	int ret;

	for (i = 0; i < num_settings; ++i, ++settings) {
		ret = ov7251_write_reg(ov7251, settings->reg, settings->val);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int ov7251_set_power_on(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov7251 *ov7251 = to_ov7251(sd);
	int ret;
	u32 wait_us;

	ret = ov7251_regulators_enable(ov7251);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(ov7251->xclk);
	if (ret < 0) {
		dev_err(ov7251->dev, "clk prepare enable failed\n");
		ov7251_regulators_disable(ov7251);
		return ret;
	}

	gpiod_set_value_cansleep(ov7251->enable_gpio, 1);

	/* wait at least 65536 external clock cycles */
	wait_us = DIV_ROUND_UP(65536 * 1000,
			       DIV_ROUND_UP(ov7251->xclk_freq, 1000));
	usleep_range(wait_us, wait_us + 1000);

	ret = ov7251_set_register_array(ov7251,
					ov7251_global_init_setting,
					ARRAY_SIZE(ov7251_global_init_setting));
	if (ret < 0) {
		dev_err(ov7251->dev, "error during global init\n");
		gpiod_set_value_cansleep(ov7251->enable_gpio, 0);
		clk_disable_unprepare(ov7251->xclk);
		ov7251_regulators_disable(ov7251);
		return ret;
	}

	return ret;
}

static int ov7251_set_power_off(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov7251 *ov7251 = to_ov7251(sd);

	clk_disable_unprepare(ov7251->xclk);
	gpiod_set_value_cansleep(ov7251->enable_gpio, 0);
	ov7251_regulators_disable(ov7251);

	return 0;
}

static int ov7251_set_hflip(struct ov7251 *ov7251, s32 value)
{
	u8 val = ov7251->timing_format2;
	int ret;

	if (value)
		val |= OV7251_TIMING_FORMAT2_MIRROR;
	else
		val &= ~OV7251_TIMING_FORMAT2_MIRROR;

	ret = ov7251_write_reg(ov7251, OV7251_TIMING_FORMAT2, val);
	if (!ret)
		ov7251->timing_format2 = val;

	return ret;
}

static int ov7251_set_vflip(struct ov7251 *ov7251, s32 value)
{
	u8 val = ov7251->timing_format1;
	int ret;

	if (value)
		val |= OV7251_TIMING_FORMAT1_VFLIP;
	else
		val &= ~OV7251_TIMING_FORMAT1_VFLIP;

	ret = ov7251_write_reg(ov7251, OV7251_TIMING_FORMAT1, val);
	if (!ret)
		ov7251->timing_format1 = val;

	return ret;
}

static int ov7251_set_test_pattern(struct ov7251 *ov7251, s32 value)
{
	u8 val = ov7251->pre_isp_00;
	int ret;

	if (value)
		val |= OV7251_PRE_ISP_00_TEST_PATTERN;
	else
		val &= ~OV7251_PRE_ISP_00_TEST_PATTERN;

	ret = ov7251_write_reg(ov7251, OV7251_PRE_ISP_00, val);
	if (!ret)
		ov7251->pre_isp_00 = val;

	return ret;
}

static const char * const ov7251_test_pattern_menu[] = {
	"Disabled",
	"Vertical Pattern Bars",
};

static int ov7251_vts_configure(struct ov7251 *ov7251, s32 vblank)
{
	u8 vts[2];

	vts[0] = ((ov7251->current_mode->height + vblank) & 0xff00) >> 8;
	vts[1] = ((ov7251->current_mode->height + vblank) & 0x00ff);

	return ov7251_write_seq_regs(ov7251, OV7251_TIMING_VTS_REG, vts, 2);
}

static int ov7251_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov7251 *ov7251 = container_of(ctrl->handler,
					     struct ov7251, ctrls);
	int ret;

	/* If VBLANK is altered we need to update exposure to compensate */
	if (ctrl->id == V4L2_CID_VBLANK) {
		int exposure_max;

		exposure_max = ov7251->current_mode->height + ctrl->val -
			       OV7251_INTEGRATION_MARGIN;
		__v4l2_ctrl_modify_range(ov7251->exposure,
					 ov7251->exposure->minimum,
					 exposure_max,
					 ov7251->exposure->step,
					 min(ov7251->exposure->val,
					     exposure_max));
	}

	/* v4l2_ctrl_lock() locks our mutex */

	if (!pm_runtime_get_if_in_use(ov7251->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = ov7251_set_exposure(ov7251, ctrl->val);
		break;
	case V4L2_CID_GAIN:
		ret = ov7251_set_gain(ov7251, ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov7251_set_test_pattern(ov7251, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = ov7251_set_hflip(ov7251, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		ret = ov7251_set_vflip(ov7251, ctrl->val);
		break;
	case V4L2_CID_VBLANK:
		ret = ov7251_vts_configure(ov7251, ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(ov7251->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov7251_ctrl_ops = {
	.s_ctrl = ov7251_s_ctrl,
};

static int ov7251_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_Y10_1X10;

	return 0;
}

static int ov7251_enum_frame_size(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->code != MEDIA_BUS_FMT_Y10_1X10)
		return -EINVAL;

	if (fse->index >= ARRAY_SIZE(ov7251_mode_info_data))
		return -EINVAL;

	fse->min_width = ov7251_mode_info_data[fse->index].width;
	fse->max_width = ov7251_mode_info_data[fse->index].width;
	fse->min_height = ov7251_mode_info_data[fse->index].height;
	fse->max_height = ov7251_mode_info_data[fse->index].height;

	return 0;
}

static int ov7251_enum_frame_ival(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_interval_enum *fie)
{
	unsigned int index = fie->index;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ov7251_mode_info_data); i++) {
		if (fie->width != ov7251_mode_info_data[i].width ||
		    fie->height != ov7251_mode_info_data[i].height)
			continue;

		if (index-- == 0) {
			fie->interval = ov7251_mode_info_data[i].timeperframe;
			return 0;
		}
	}

	return -EINVAL;
}

static struct v4l2_mbus_framefmt *
__ov7251_get_pad_format(struct ov7251 *ov7251,
			struct v4l2_subdev_state *sd_state,
			unsigned int pad,
			enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&ov7251->sd, sd_state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &ov7251->fmt;
	default:
		return NULL;
	}
}

static int ov7251_get_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *format)
{
	struct ov7251 *ov7251 = to_ov7251(sd);

	mutex_lock(&ov7251->lock);
	format->format = *__ov7251_get_pad_format(ov7251, sd_state,
						  format->pad,
						  format->which);
	mutex_unlock(&ov7251->lock);

	return 0;
}

static struct v4l2_rect *
__ov7251_get_pad_crop(struct ov7251 *ov7251,
		      struct v4l2_subdev_state *sd_state,
		      unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_crop(&ov7251->sd, sd_state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &ov7251->crop;
	default:
		return NULL;
	}
}

static inline u32 avg_fps(const struct v4l2_fract *t)
{
	return (t->denominator + (t->numerator >> 1)) / t->numerator;
}

static const struct ov7251_mode_info *
ov7251_find_mode_by_ival(struct ov7251 *ov7251, struct v4l2_fract *timeperframe)
{
	const struct ov7251_mode_info *mode = ov7251->current_mode;
	unsigned int fps_req = avg_fps(timeperframe);
	unsigned int max_dist_match = (unsigned int) -1;
	unsigned int i, n = 0;

	for (i = 0; i < ARRAY_SIZE(ov7251_mode_info_data); i++) {
		unsigned int dist;
		unsigned int fps_tmp;

		if (mode->width != ov7251_mode_info_data[i].width ||
		    mode->height != ov7251_mode_info_data[i].height)
			continue;

		fps_tmp = avg_fps(&ov7251_mode_info_data[i].timeperframe);

		dist = abs(fps_req - fps_tmp);

		if (dist < max_dist_match) {
			n = i;
			max_dist_match = dist;
		}
	}

	return &ov7251_mode_info_data[n];
}

static int ov7251_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *format)
{
	struct ov7251 *ov7251 = to_ov7251(sd);
	struct v4l2_mbus_framefmt *__format;
	int vblank_max, vblank_def;
	struct v4l2_rect *__crop;
	const struct ov7251_mode_info *new_mode;
	int ret = 0;

	mutex_lock(&ov7251->lock);

	__crop = __ov7251_get_pad_crop(ov7251, sd_state, format->pad,
				       format->which);

	new_mode = v4l2_find_nearest_size(ov7251_mode_info_data,
				ARRAY_SIZE(ov7251_mode_info_data),
				width, height,
				format->format.width, format->format.height);

	__crop->width = new_mode->width;
	__crop->height = new_mode->height;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		ret = __v4l2_ctrl_modify_range(ov7251->exposure,
					       1, new_mode->exposure_max,
					       1, new_mode->exposure_def);
		if (ret < 0)
			goto exit;

		ret = __v4l2_ctrl_s_ctrl(ov7251->exposure,
					 new_mode->exposure_def);
		if (ret < 0)
			goto exit;

		ret = __v4l2_ctrl_s_ctrl(ov7251->gain, 16);
		if (ret < 0)
			goto exit;

		vblank_max = OV7251_TIMING_MAX_VTS - new_mode->height;
		vblank_def = new_mode->vts - new_mode->height;
		ret = __v4l2_ctrl_modify_range(ov7251->vblank,
					       OV7251_TIMING_MIN_VTS,
					       vblank_max, 1, vblank_def);
		if (ret < 0)
			goto exit;

		ov7251->current_mode = new_mode;
	}

	__format = __ov7251_get_pad_format(ov7251, sd_state, format->pad,
					   format->which);
	__format->width = __crop->width;
	__format->height = __crop->height;
	__format->code = MEDIA_BUS_FMT_Y10_1X10;
	__format->field = V4L2_FIELD_NONE;
	__format->colorspace = V4L2_COLORSPACE_SRGB;
	__format->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(__format->colorspace);
	__format->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
				__format->colorspace, __format->ycbcr_enc);
	__format->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(__format->colorspace);

	format->format = *__format;

exit:
	mutex_unlock(&ov7251->lock);

	return ret;
}

static int ov7251_entity_init_cfg(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *sd_state)
{
	struct v4l2_subdev_format fmt = {
		.which = sd_state ? V4L2_SUBDEV_FORMAT_TRY
		: V4L2_SUBDEV_FORMAT_ACTIVE,
		.format = {
			.width = 640,
			.height = 480
		}
	};

	ov7251_set_format(subdev, sd_state, &fmt);

	return 0;
}

static int ov7251_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	struct ov7251 *ov7251 = to_ov7251(sd);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP:
	mutex_lock(&ov7251->lock);
		sel->r = *__ov7251_get_pad_crop(ov7251, sd_state, sel->pad,
						sel->which);
		mutex_unlock(&ov7251->lock);
		break;
	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = OV7251_NATIVE_WIDTH;
		sel->r.height = OV7251_NATIVE_HEIGHT;
		break;
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = OV7251_ACTIVE_START_TOP;
		sel->r.left = OV7251_ACTIVE_START_LEFT;
		sel->r.width = OV7251_ACTIVE_WIDTH;
		sel->r.height = OV7251_ACTIVE_HEIGHT;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ov7251_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct ov7251 *ov7251 = to_ov7251(subdev);
	int ret;

	mutex_lock(&ov7251->lock);

	if (enable) {
		ret = pm_runtime_get_sync(ov7251->dev);
		if (ret < 0)
			goto err_power_down;

		ret = ov7251_pll_configure(ov7251);
		if (ret) {
			dev_err(ov7251->dev, "error configuring PLLs\n");
			goto err_power_down;
		}

		ret = ov7251_set_register_array(ov7251,
					ov7251->current_mode->data,
					ov7251->current_mode->data_size);
		if (ret < 0) {
			dev_err(ov7251->dev, "could not set mode %dx%d\n",
				ov7251->current_mode->width,
				ov7251->current_mode->height);
			goto err_power_down;
		}
		ret = __v4l2_ctrl_handler_setup(&ov7251->ctrls);
		if (ret < 0) {
			dev_err(ov7251->dev, "could not sync v4l2 controls\n");
			goto err_power_down;
		}
		ret = ov7251_write_reg(ov7251, OV7251_SC_MODE_SELECT,
				       OV7251_SC_MODE_SELECT_STREAMING);
		if (ret)
			goto err_power_down;
	} else {
		ret = ov7251_write_reg(ov7251, OV7251_SC_MODE_SELECT,
				       OV7251_SC_MODE_SELECT_SW_STANDBY);
		pm_runtime_put(ov7251->dev);
	}

	mutex_unlock(&ov7251->lock);
	return ret;

err_power_down:
	pm_runtime_put(ov7251->dev);
	mutex_unlock(&ov7251->lock);
	return ret;
}

static int ov7251_get_frame_interval(struct v4l2_subdev *subdev,
				     struct v4l2_subdev_frame_interval *fi)
{
	struct ov7251 *ov7251 = to_ov7251(subdev);

	mutex_lock(&ov7251->lock);
	fi->interval = ov7251->current_mode->timeperframe;
	mutex_unlock(&ov7251->lock);

	return 0;
}

static int ov7251_set_frame_interval(struct v4l2_subdev *subdev,
				     struct v4l2_subdev_frame_interval *fi)
{
	struct ov7251 *ov7251 = to_ov7251(subdev);
	const struct ov7251_mode_info *new_mode;
	int ret = 0;

	mutex_lock(&ov7251->lock);
	new_mode = ov7251_find_mode_by_ival(ov7251, &fi->interval);

	if (new_mode != ov7251->current_mode) {
		ret = __v4l2_ctrl_modify_range(ov7251->exposure,
					       1, new_mode->exposure_max,
					       1, new_mode->exposure_def);
		if (ret < 0)
			goto exit;

		ret = __v4l2_ctrl_s_ctrl(ov7251->exposure,
					 new_mode->exposure_def);
		if (ret < 0)
			goto exit;

		ret = __v4l2_ctrl_s_ctrl(ov7251->gain, 16);
		if (ret < 0)
			goto exit;

		ov7251->current_mode = new_mode;
	}

	fi->interval = ov7251->current_mode->timeperframe;

exit:
	mutex_unlock(&ov7251->lock);

	return ret;
}

static const struct v4l2_subdev_video_ops ov7251_video_ops = {
	.s_stream = ov7251_s_stream,
	.g_frame_interval = ov7251_get_frame_interval,
	.s_frame_interval = ov7251_set_frame_interval,
};

static const struct v4l2_subdev_pad_ops ov7251_subdev_pad_ops = {
	.init_cfg = ov7251_entity_init_cfg,
	.enum_mbus_code = ov7251_enum_mbus_code,
	.enum_frame_size = ov7251_enum_frame_size,
	.enum_frame_interval = ov7251_enum_frame_ival,
	.get_fmt = ov7251_get_format,
	.set_fmt = ov7251_set_format,
	.get_selection = ov7251_get_selection,
};

static const struct v4l2_subdev_ops ov7251_subdev_ops = {
	.video = &ov7251_video_ops,
	.pad = &ov7251_subdev_pad_ops,
};

static int ov7251_check_hwcfg(struct ov7251 *ov7251)
{
	struct fwnode_handle *fwnode = dev_fwnode(ov7251->dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *endpoint;
	unsigned int i, j;
	int ret;

	endpoint = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!endpoint)
		return -EPROBE_DEFER; /* could be provided by cio2-bridge */

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &bus_cfg);
	fwnode_handle_put(endpoint);
	if (ret)
		return dev_err_probe(ov7251->dev, ret,
				     "parsing endpoint node failed\n");

	if (!bus_cfg.nr_of_link_frequencies) {
		ret = dev_err_probe(ov7251->dev, -EINVAL,
				    "no link frequencies defined\n");
		goto out_free_bus_cfg;
	}

	for (i = 0; i < bus_cfg.nr_of_link_frequencies; i++) {
		for (j = 0; j < ARRAY_SIZE(link_freq); j++)
			if (bus_cfg.link_frequencies[i] == link_freq[j])
				break;

		if (j < ARRAY_SIZE(link_freq))
			break;
	}

	if (i == bus_cfg.nr_of_link_frequencies) {
		ret = dev_err_probe(ov7251->dev, -EINVAL,
				    "no supported link freq found\n");
		goto out_free_bus_cfg;
	}

	ov7251->link_freq_idx = i;

out_free_bus_cfg:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int ov7251_detect_chip(struct ov7251 *ov7251)
{
	u8 chip_id_high, chip_id_low, chip_rev;
	int ret;

	ret = ov7251_read_reg(ov7251, OV7251_CHIP_ID_HIGH, &chip_id_high);
	if (ret < 0 || chip_id_high != OV7251_CHIP_ID_HIGH_BYTE)
		return dev_err_probe(ov7251->dev, -ENODEV,
				     "could not read ID high\n");

	ret = ov7251_read_reg(ov7251, OV7251_CHIP_ID_LOW, &chip_id_low);
	if (ret < 0 || chip_id_low != OV7251_CHIP_ID_LOW_BYTE)
		return dev_err_probe(ov7251->dev, -ENODEV,
				     "could not read ID low\n");

	ret = ov7251_read_reg(ov7251, OV7251_SC_GP_IO_IN1, &chip_rev);
	if (ret < 0)
		return dev_err_probe(ov7251->dev, -ENODEV,
				     "could not read revision\n");
	chip_rev >>= 4;

	dev_info(ov7251->dev,
		 "OV7251 revision %x (%s) detected at address 0x%02x\n",
		 chip_rev,
		 chip_rev == 0x4 ? "1A / 1B" :
		 chip_rev == 0x5 ? "1C / 1D" :
		 chip_rev == 0x6 ? "1E" :
		 chip_rev == 0x7 ? "1F" : "unknown",
		 ov7251->i2c_client->addr);

	return 0;
}

static int ov7251_init_ctrls(struct ov7251 *ov7251)
{
	int vblank_max, vblank_def;
	s64 pixel_rate;
	int hblank;

	v4l2_ctrl_handler_init(&ov7251->ctrls, 7);
	ov7251->ctrls.lock = &ov7251->lock;

	v4l2_ctrl_new_std(&ov7251->ctrls, &ov7251_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&ov7251->ctrls, &ov7251_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);
	ov7251->exposure = v4l2_ctrl_new_std(&ov7251->ctrls, &ov7251_ctrl_ops,
					     V4L2_CID_EXPOSURE, 1, 32, 1, 32);
	ov7251->gain = v4l2_ctrl_new_std(&ov7251->ctrls, &ov7251_ctrl_ops,
					 V4L2_CID_GAIN, 16, 1023, 1, 16);
	v4l2_ctrl_new_std_menu_items(&ov7251->ctrls, &ov7251_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov7251_test_pattern_menu) - 1,
				     0, 0, ov7251_test_pattern_menu);

	pixel_rate = pixel_rates[ov7251->link_freq_idx];
	ov7251->pixel_clock = v4l2_ctrl_new_std(&ov7251->ctrls,
						&ov7251_ctrl_ops,
						V4L2_CID_PIXEL_RATE,
						pixel_rate, INT_MAX,
						pixel_rate, pixel_rate);
	ov7251->link_freq = v4l2_ctrl_new_int_menu(&ov7251->ctrls,
						   &ov7251_ctrl_ops,
						   V4L2_CID_LINK_FREQ,
						   ARRAY_SIZE(link_freq) - 1,
						   ov7251->link_freq_idx,
						   link_freq);
	if (ov7251->link_freq)
		ov7251->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	if (ov7251->pixel_clock)
		ov7251->pixel_clock->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	hblank = OV7251_FIXED_PPL - ov7251->current_mode->width;
	ov7251->hblank = v4l2_ctrl_new_std(&ov7251->ctrls, &ov7251_ctrl_ops,
					   V4L2_CID_HBLANK, hblank, hblank, 1,
					   hblank);
	if (ov7251->hblank)
		ov7251->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_max = OV7251_TIMING_MAX_VTS - ov7251->current_mode->height;
	vblank_def = ov7251->current_mode->vts - ov7251->current_mode->height;
	ov7251->vblank = v4l2_ctrl_new_std(&ov7251->ctrls, &ov7251_ctrl_ops,
					   V4L2_CID_VBLANK,
					   OV7251_TIMING_MIN_VTS, vblank_max, 1,
					   vblank_def);

	ov7251->sd.ctrl_handler = &ov7251->ctrls;

	if (ov7251->ctrls.error) {
		v4l2_ctrl_handler_free(&ov7251->ctrls);
		return ov7251->ctrls.error;
	}

	return 0;
}

static int ov7251_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ov7251 *ov7251;
	unsigned int rate = 0, clk_rate = 0;
	int ret;
	int i;

	ov7251 = devm_kzalloc(dev, sizeof(struct ov7251), GFP_KERNEL);
	if (!ov7251)
		return -ENOMEM;

	ov7251->i2c_client = client;
	ov7251->dev = dev;

	ret = ov7251_check_hwcfg(ov7251);
	if (ret)
		return ret;

	/* get system clock (xclk) */
	ov7251->xclk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(ov7251->xclk))
		return dev_err_probe(dev, PTR_ERR(ov7251->xclk),
				     "could not get xclk");

	/*
	 * We could have either a 24MHz or 19.2MHz clock rate from either DT or
	 * ACPI. We also need to support the IPU3 case which will have both an
	 * external clock AND a clock-frequency property.
	 */
	ret = fwnode_property_read_u32(dev_fwnode(dev), "clock-frequency",
				       &rate);
	if (ret && !ov7251->xclk)
		return dev_err_probe(dev, ret, "invalid clock config\n");

	clk_rate = clk_get_rate(ov7251->xclk);
	ov7251->xclk_freq = clk_rate ? clk_rate : rate;

	if (ov7251->xclk_freq == 0)
		return dev_err_probe(dev, -EINVAL, "invalid clock frequency\n");

	if (!ret && ov7251->xclk) {
		ret = clk_set_rate(ov7251->xclk, rate);
		if (ret)
			return dev_err_probe(dev, ret,
					     "failed to set clock rate\n");
	}

	for (i = 0; i < ARRAY_SIZE(supported_xclk_rates); i++)
		if (ov7251->xclk_freq == supported_xclk_rates[i])
			break;

	if (i == ARRAY_SIZE(supported_xclk_rates))
		return dev_err_probe(dev, -EINVAL,
				     "clock rate %u Hz is unsupported\n",
				     ov7251->xclk_freq);

	ov7251->pll_cfgs = ov7251_pll_cfgs[i];

	ov7251->io_regulator = devm_regulator_get(dev, "vdddo");
	if (IS_ERR(ov7251->io_regulator)) {
		dev_err(dev, "cannot get io regulator\n");
		return PTR_ERR(ov7251->io_regulator);
	}

	ov7251->core_regulator = devm_regulator_get(dev, "vddd");
	if (IS_ERR(ov7251->core_regulator)) {
		dev_err(dev, "cannot get core regulator\n");
		return PTR_ERR(ov7251->core_regulator);
	}

	ov7251->analog_regulator = devm_regulator_get(dev, "vdda");
	if (IS_ERR(ov7251->analog_regulator)) {
		dev_err(dev, "cannot get analog regulator\n");
		return PTR_ERR(ov7251->analog_regulator);
	}

	ov7251->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(ov7251->enable_gpio)) {
		dev_err(dev, "cannot get enable gpio\n");
		return PTR_ERR(ov7251->enable_gpio);
	}

	mutex_init(&ov7251->lock);

	ov7251->current_mode = &ov7251_mode_info_data[0];
	ret = ov7251_init_ctrls(ov7251);
	if (ret) {
		dev_err_probe(dev, ret, "error during v4l2 ctrl init\n");
		goto destroy_mutex;
	}

	v4l2_i2c_subdev_init(&ov7251->sd, client, &ov7251_subdev_ops);
	ov7251->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov7251->pad.flags = MEDIA_PAD_FL_SOURCE;
	ov7251->sd.dev = &client->dev;
	ov7251->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = media_entity_pads_init(&ov7251->sd.entity, 1, &ov7251->pad);
	if (ret < 0) {
		dev_err(dev, "could not register media entity\n");
		goto free_ctrl;
	}

	ret = ov7251_set_power_on(ov7251->dev);
	if (ret)
		goto free_entity;

	ret = ov7251_detect_chip(ov7251);
	if (ret)
		goto power_down;

	pm_runtime_set_active(&client->dev);
	pm_runtime_get_noresume(&client->dev);
	pm_runtime_enable(&client->dev);

	ret = ov7251_read_reg(ov7251, OV7251_PRE_ISP_00,
			      &ov7251->pre_isp_00);
	if (ret < 0) {
		dev_err(dev, "could not read test pattern value\n");
		ret = -ENODEV;
		goto err_pm_runtime;
	}

	ret = ov7251_read_reg(ov7251, OV7251_TIMING_FORMAT1,
			      &ov7251->timing_format1);
	if (ret < 0) {
		dev_err(dev, "could not read vflip value\n");
		ret = -ENODEV;
		goto err_pm_runtime;
	}

	ret = ov7251_read_reg(ov7251, OV7251_TIMING_FORMAT2,
			      &ov7251->timing_format2);
	if (ret < 0) {
		dev_err(dev, "could not read hflip value\n");
		ret = -ENODEV;
		goto err_pm_runtime;
	}

	pm_runtime_set_autosuspend_delay(&client->dev, 1000);
	pm_runtime_use_autosuspend(&client->dev);
	pm_runtime_put_autosuspend(&client->dev);

	ret = v4l2_async_register_subdev(&ov7251->sd);
	if (ret < 0) {
		dev_err(dev, "could not register v4l2 device\n");
		goto free_entity;
	}

	ov7251_entity_init_cfg(&ov7251->sd, NULL);

	return 0;

err_pm_runtime:
	pm_runtime_disable(ov7251->dev);
	pm_runtime_put_noidle(ov7251->dev);
power_down:
	ov7251_set_power_off(ov7251->dev);
free_entity:
	media_entity_cleanup(&ov7251->sd.entity);
free_ctrl:
	v4l2_ctrl_handler_free(&ov7251->ctrls);
destroy_mutex:
	mutex_destroy(&ov7251->lock);

	return ret;
}

static void ov7251_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov7251 *ov7251 = to_ov7251(sd);

	v4l2_async_unregister_subdev(&ov7251->sd);
	media_entity_cleanup(&ov7251->sd.entity);
	v4l2_ctrl_handler_free(&ov7251->ctrls);
	mutex_destroy(&ov7251->lock);

	pm_runtime_disable(ov7251->dev);
	if (!pm_runtime_status_suspended(ov7251->dev))
		ov7251_set_power_off(ov7251->dev);
	pm_runtime_set_suspended(ov7251->dev);
}

static const struct dev_pm_ops ov7251_pm_ops = {
	SET_RUNTIME_PM_OPS(ov7251_set_power_off, ov7251_set_power_on, NULL)
};

static const struct of_device_id ov7251_of_match[] = {
	{ .compatible = "ovti,ov7251" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ov7251_of_match);

static const struct acpi_device_id ov7251_acpi_match[] = {
	{ "INT347E" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, ov7251_acpi_match);

static struct i2c_driver ov7251_i2c_driver = {
	.driver = {
		.of_match_table = ov7251_of_match,
		.acpi_match_table = ov7251_acpi_match,
		.name  = "ov7251",
		.pm = &ov7251_pm_ops,
	},
	.probe = ov7251_probe,
	.remove = ov7251_remove,
};

module_i2c_driver(ov7251_i2c_driver);

MODULE_DESCRIPTION("Omnivision OV7251 Camera Driver");
MODULE_AUTHOR("Todor Tomov <todor.tomov@linaro.org>");
MODULE_LICENSE("GPL v2");
