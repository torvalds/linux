// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Intel Corporation.

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/unaligned.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define OV2740_LINK_FREQ_360MHZ		360000000ULL
#define OV2740_LINK_FREQ_180MHZ		180000000ULL
#define OV2740_SCLK			72000000LL
#define OV2740_MCLK			19200000
#define OV2740_DATA_LANES		2
#define OV2740_RGB_DEPTH		10

#define OV2740_REG_CHIP_ID		0x300a
#define OV2740_CHIP_ID			0x2740

#define OV2740_REG_MODE_SELECT		0x0100
#define OV2740_MODE_STANDBY		0x00
#define OV2740_MODE_STREAMING		0x01

/* vertical-timings from sensor */
#define OV2740_REG_VTS			0x380e

/* horizontal-timings from sensor */
#define OV2740_REG_HTS			0x380c

/* Exposure controls from sensor */
#define OV2740_REG_EXPOSURE		0x3500
#define OV2740_EXPOSURE_MIN		4
#define OV2740_EXPOSURE_MAX_MARGIN	8
#define OV2740_EXPOSURE_STEP		1

/* Analog gain controls from sensor */
#define OV2740_REG_ANALOG_GAIN		0x3508
#define OV2740_ANAL_GAIN_MIN		128
#define OV2740_ANAL_GAIN_MAX		1983
#define OV2740_ANAL_GAIN_STEP		1

/* Digital gain controls from sensor */
#define OV2740_REG_MWB_R_GAIN		0x500a
#define OV2740_REG_MWB_G_GAIN		0x500c
#define OV2740_REG_MWB_B_GAIN		0x500e
#define OV2740_DGTL_GAIN_MIN		1024
#define OV2740_DGTL_GAIN_MAX		4095
#define OV2740_DGTL_GAIN_STEP		1
#define OV2740_DGTL_GAIN_DEFAULT	1024

/* Test Pattern Control */
#define OV2740_REG_TEST_PATTERN		0x5040
#define OV2740_TEST_PATTERN_ENABLE	BIT(7)
#define OV2740_TEST_PATTERN_BAR_SHIFT	2

/* Group Access */
#define OV2740_REG_GROUP_ACCESS		0x3208
#define OV2740_GROUP_HOLD_START		0x0
#define OV2740_GROUP_HOLD_END		0x10
#define OV2740_GROUP_HOLD_LAUNCH	0xa0

/* ISP CTRL00 */
#define OV2740_REG_ISP_CTRL00		0x5000
/* ISP CTRL01 */
#define OV2740_REG_ISP_CTRL01		0x5001
/* Customer Addresses: 0x7010 - 0x710F */
#define CUSTOMER_USE_OTP_SIZE		0x100
/* OTP registers from sensor */
#define OV2740_REG_OTP_CUSTOMER		0x7010

static const char * const ov2740_supply_name[] = {
	"AVDD",
	"DOVDD",
	"DVDD",
};

struct nvm_data {
	struct nvmem_device *nvmem;
	struct regmap *regmap;
	char *nvm_buffer;
};

enum {
	OV2740_LINK_FREQ_360MHZ_INDEX,
	OV2740_LINK_FREQ_180MHZ_INDEX,
};

struct ov2740_reg {
	u16 address;
	u8 val;
};

struct ov2740_reg_list {
	u32 num_of_regs;
	const struct ov2740_reg *regs;
};

struct ov2740_link_freq_config {
	const struct ov2740_reg_list reg_list;
};

struct ov2740_mode {
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

	/* Max vertical timining size */
	u32 vts_max;

	/* Link frequency needed for this resolution */
	u32 link_freq_index;

	/* Sensor register settings for this resolution */
	const struct ov2740_reg_list reg_list;
};

static const struct ov2740_reg mipi_data_rate_720mbps[] = {
	{0x0302, 0x4b},
	{0x030d, 0x4b},
	{0x030e, 0x02},
	{0x030a, 0x01},
	{0x0312, 0x11},
};

static const struct ov2740_reg mipi_data_rate_360mbps[] = {
	{0x0302, 0x4b},
	{0x0303, 0x01},
	{0x030d, 0x4b},
	{0x030e, 0x02},
	{0x030a, 0x01},
	{0x0312, 0x11},
	{0x4837, 0x2c},
};

static const struct ov2740_reg mode_1932x1092_regs_360mhz[] = {
	{0x3000, 0x00},
	{0x3018, 0x32},
	{0x3031, 0x0a},
	{0x3080, 0x08},
	{0x3083, 0xB4},
	{0x3103, 0x00},
	{0x3104, 0x01},
	{0x3106, 0x01},
	{0x3500, 0x00},
	{0x3501, 0x44},
	{0x3502, 0x40},
	{0x3503, 0x88},
	{0x3507, 0x00},
	{0x3508, 0x00},
	{0x3509, 0x80},
	{0x350c, 0x00},
	{0x350d, 0x80},
	{0x3510, 0x00},
	{0x3511, 0x00},
	{0x3512, 0x20},
	{0x3632, 0x00},
	{0x3633, 0x10},
	{0x3634, 0x10},
	{0x3635, 0x10},
	{0x3645, 0x13},
	{0x3646, 0x81},
	{0x3636, 0x10},
	{0x3651, 0x0a},
	{0x3656, 0x02},
	{0x3659, 0x04},
	{0x365a, 0xda},
	{0x365b, 0xa2},
	{0x365c, 0x04},
	{0x365d, 0x1d},
	{0x365e, 0x1a},
	{0x3662, 0xd7},
	{0x3667, 0x78},
	{0x3669, 0x0a},
	{0x366a, 0x92},
	{0x3700, 0x54},
	{0x3702, 0x10},
	{0x3706, 0x42},
	{0x3709, 0x30},
	{0x370b, 0xc2},
	{0x3714, 0x63},
	{0x3715, 0x01},
	{0x3716, 0x00},
	{0x371a, 0x3e},
	{0x3732, 0x0e},
	{0x3733, 0x10},
	{0x375f, 0x0e},
	{0x3768, 0x30},
	{0x3769, 0x44},
	{0x376a, 0x22},
	{0x377b, 0x20},
	{0x377c, 0x00},
	{0x377d, 0x0c},
	{0x3798, 0x00},
	{0x37a1, 0x55},
	{0x37a8, 0x6d},
	{0x37c2, 0x04},
	{0x37c5, 0x00},
	{0x37c8, 0x00},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x07},
	{0x3805, 0x8f},
	{0x3806, 0x04},
	{0x3807, 0x47},
	{0x3808, 0x07},
	{0x3809, 0x88},
	{0x380a, 0x04},
	{0x380b, 0x40},
	{0x380c, 0x04},
	{0x380d, 0x38},
	{0x380e, 0x04},
	{0x380f, 0x60},
	{0x3810, 0x00},
	{0x3811, 0x04},
	{0x3812, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3820, 0x80},
	{0x3821, 0x46},
	{0x3822, 0x84},
	{0x3829, 0x00},
	{0x382a, 0x01},
	{0x382b, 0x01},
	{0x3830, 0x04},
	{0x3836, 0x01},
	{0x3837, 0x08},
	{0x3839, 0x01},
	{0x383a, 0x00},
	{0x383b, 0x08},
	{0x383c, 0x00},
	{0x3f0b, 0x00},
	{0x4001, 0x20},
	{0x4009, 0x07},
	{0x4003, 0x10},
	{0x4010, 0xe0},
	{0x4016, 0x00},
	{0x4017, 0x10},
	{0x4044, 0x02},
	{0x4304, 0x08},
	{0x4307, 0x30},
	{0x4320, 0x80},
	{0x4322, 0x00},
	{0x4323, 0x00},
	{0x4324, 0x00},
	{0x4325, 0x00},
	{0x4326, 0x00},
	{0x4327, 0x00},
	{0x4328, 0x00},
	{0x4329, 0x00},
	{0x432c, 0x03},
	{0x432d, 0x81},
	{0x4501, 0x84},
	{0x4502, 0x40},
	{0x4503, 0x18},
	{0x4504, 0x04},
	{0x4508, 0x02},
	{0x4601, 0x10},
	{0x4800, 0x00},
	{0x4816, 0x52},
	{0x4837, 0x16},
	{0x5000, 0x7f},
	{0x5001, 0x00},
	{0x5005, 0x38},
	{0x501e, 0x0d},
	{0x5040, 0x00},
	{0x5901, 0x00},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x07},
	{0x3805, 0x8f},
	{0x3806, 0x04},
	{0x3807, 0x47},
	{0x3808, 0x07},
	{0x3809, 0x8c},
	{0x380a, 0x04},
	{0x380b, 0x44},
	{0x3810, 0x00},
	{0x3811, 0x00},
	{0x3812, 0x00},
	{0x3813, 0x01},
};

static const struct ov2740_reg mode_1932x1092_regs_180mhz[] = {
	{0x3000, 0x00},
	{0x3018, 0x32},	/* 0x32 for 2 lanes, 0x12 for 1 lane */
	{0x3031, 0x0a},
	{0x3080, 0x08},
	{0x3083, 0xB4},
	{0x3103, 0x00},
	{0x3104, 0x01},
	{0x3106, 0x01},
	{0x3500, 0x00},
	{0x3501, 0x44},
	{0x3502, 0x40},
	{0x3503, 0x88},
	{0x3507, 0x00},
	{0x3508, 0x00},
	{0x3509, 0x80},
	{0x350c, 0x00},
	{0x350d, 0x80},
	{0x3510, 0x00},
	{0x3511, 0x00},
	{0x3512, 0x20},
	{0x3632, 0x00},
	{0x3633, 0x10},
	{0x3634, 0x10},
	{0x3635, 0x10},
	{0x3645, 0x13},
	{0x3646, 0x81},
	{0x3636, 0x10},
	{0x3651, 0x0a},
	{0x3656, 0x02},
	{0x3659, 0x04},
	{0x365a, 0xda},
	{0x365b, 0xa2},
	{0x365c, 0x04},
	{0x365d, 0x1d},
	{0x365e, 0x1a},
	{0x3662, 0xd7},
	{0x3667, 0x78},
	{0x3669, 0x0a},
	{0x366a, 0x92},
	{0x3700, 0x54},
	{0x3702, 0x10},
	{0x3706, 0x42},
	{0x3709, 0x30},
	{0x370b, 0xc2},
	{0x3714, 0x63},
	{0x3715, 0x01},
	{0x3716, 0x00},
	{0x371a, 0x3e},
	{0x3732, 0x0e},
	{0x3733, 0x10},
	{0x375f, 0x0e},
	{0x3768, 0x30},
	{0x3769, 0x44},
	{0x376a, 0x22},
	{0x377b, 0x20},
	{0x377c, 0x00},
	{0x377d, 0x0c},
	{0x3798, 0x00},
	{0x37a1, 0x55},
	{0x37a8, 0x6d},
	{0x37c2, 0x04},
	{0x37c5, 0x00},
	{0x37c8, 0x00},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x07},
	{0x3805, 0x8f},
	{0x3806, 0x04},
	{0x3807, 0x47},
	{0x3808, 0x07},
	{0x3809, 0x88},
	{0x380a, 0x04},
	{0x380b, 0x40},
	{0x380c, 0x08},
	{0x380d, 0x70},
	{0x380e, 0x04},
	{0x380f, 0x56},
	{0x3810, 0x00},
	{0x3811, 0x04},
	{0x3812, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3820, 0x80},
	{0x3821, 0x46},
	{0x3822, 0x84},
	{0x3829, 0x00},
	{0x382a, 0x01},
	{0x382b, 0x01},
	{0x3830, 0x04},
	{0x3836, 0x01},
	{0x3837, 0x08},
	{0x3839, 0x01},
	{0x383a, 0x00},
	{0x383b, 0x08},
	{0x383c, 0x00},
	{0x3f0b, 0x00},
	{0x4001, 0x20},
	{0x4009, 0x07},
	{0x4003, 0x10},
	{0x4010, 0xe0},
	{0x4016, 0x00},
	{0x4017, 0x10},
	{0x4044, 0x02},
	{0x4304, 0x08},
	{0x4307, 0x30},
	{0x4320, 0x80},
	{0x4322, 0x00},
	{0x4323, 0x00},
	{0x4324, 0x00},
	{0x4325, 0x00},
	{0x4326, 0x00},
	{0x4327, 0x00},
	{0x4328, 0x00},
	{0x4329, 0x00},
	{0x432c, 0x03},
	{0x432d, 0x81},
	{0x4501, 0x84},
	{0x4502, 0x40},
	{0x4503, 0x18},
	{0x4504, 0x04},
	{0x4508, 0x02},
	{0x4601, 0x10},
	{0x4800, 0x00},
	{0x4816, 0x52},
	{0x5000, 0x73},	/* 0x7f enable DPC */
	{0x5001, 0x00},
	{0x5005, 0x38},
	{0x501e, 0x0d},
	{0x5040, 0x00},
	{0x5901, 0x00},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x07},
	{0x3805, 0x8f},
	{0x3806, 0x04},
	{0x3807, 0x47},
	{0x3808, 0x07},
	{0x3809, 0x8c},
	{0x380a, 0x04},
	{0x380b, 0x44},
	{0x3810, 0x00},
	{0x3811, 0x00},
	{0x3812, 0x00},
	{0x3813, 0x01},
	{0x4003, 0x40},	/* set Black level to 0x40 */
};

static const char * const ov2740_test_pattern_menu[] = {
	"Disabled",
	"Color Bar",
	"Top-Bottom Darker Color Bar",
	"Right-Left Darker Color Bar",
	"Bottom-Top Darker Color Bar",
};

static const s64 link_freq_menu_items[] = {
	OV2740_LINK_FREQ_360MHZ,
	OV2740_LINK_FREQ_180MHZ,
};

static const struct ov2740_link_freq_config link_freq_configs[] = {
	[OV2740_LINK_FREQ_360MHZ_INDEX] = {
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_720mbps),
			.regs = mipi_data_rate_720mbps,
		}
	},
	[OV2740_LINK_FREQ_180MHZ_INDEX] = {
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_360mbps),
			.regs = mipi_data_rate_360mbps,
		}
	},
};

static const struct ov2740_mode supported_modes_360mhz[] = {
	{
		.width = 1932,
		.height = 1092,
		.hts = 2160,
		.vts_min = 1120,
		.vts_def = 2186,
		.vts_max = 32767,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1932x1092_regs_360mhz),
			.regs = mode_1932x1092_regs_360mhz,
		},
		.link_freq_index = OV2740_LINK_FREQ_360MHZ_INDEX,
	},
};

static const struct ov2740_mode supported_modes_180mhz[] = {
	{
		.width = 1932,
		.height = 1092,
		.hts = 2160,
		.vts_min = 1110,
		.vts_def = 1110,
		.vts_max = 2047,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1932x1092_regs_180mhz),
			.regs = mode_1932x1092_regs_180mhz,
		},
		.link_freq_index = OV2740_LINK_FREQ_180MHZ_INDEX,
	},
};

struct ov2740 {
	struct device *dev;

	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;

	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;

	/* GPIOs, clocks, regulators */
	struct gpio_desc *reset_gpio;
	struct gpio_desc *powerdown_gpio;
	struct clk *clk;
	struct regulator_bulk_data supplies[ARRAY_SIZE(ov2740_supply_name)];

	/* Current mode */
	const struct ov2740_mode *cur_mode;

	/* NVM data information */
	struct nvm_data *nvm;

	/* Supported modes */
	const struct ov2740_mode *supported_modes;
	int supported_modes_count;

	/* True if the device has been identified */
	bool identified;
};

static inline struct ov2740 *to_ov2740(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct ov2740, sd);
}

static u64 to_pixel_rate(u32 f_index)
{
	u64 pixel_rate = link_freq_menu_items[f_index] * 2 * OV2740_DATA_LANES;

	do_div(pixel_rate, OV2740_RGB_DEPTH);

	return pixel_rate;
}

static int ov2740_read_reg(struct ov2740 *ov2740, u16 reg, u16 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov2740->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2];
	u8 data_buf[4] = {0};
	int ret;

	if (len > sizeof(data_buf))
		return -EINVAL;

	put_unaligned_be16(reg, addr_buf);
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(addr_buf);
	msgs[0].buf = addr_buf;
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[sizeof(data_buf) - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return ret < 0 ? ret : -EIO;

	*val = get_unaligned_be32(data_buf);

	return 0;
}

static int ov2740_write_reg(struct ov2740 *ov2740, u16 reg, u16 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov2740->sd);
	u8 buf[6];
	int ret;

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << 8 * (4 - len), buf + 2);

	ret = i2c_master_send(client, buf, len + 2);
	if (ret != len + 2)
		return ret < 0 ? ret : -EIO;

	return 0;
}

static int ov2740_write_reg_list(struct ov2740 *ov2740,
				 const struct ov2740_reg_list *r_list)
{
	unsigned int i;
	int ret;

	for (i = 0; i < r_list->num_of_regs; i++) {
		ret = ov2740_write_reg(ov2740, r_list->regs[i].address, 1,
				       r_list->regs[i].val);
		if (ret) {
			dev_err_ratelimited(ov2740->dev,
					    "write reg 0x%4.4x return err = %d\n",
					    r_list->regs[i].address, ret);
			return ret;
		}
	}

	return 0;
}

static int ov2740_identify_module(struct ov2740 *ov2740)
{
	int ret;
	u32 val;

	if (ov2740->identified)
		return 0;

	ret = ov2740_read_reg(ov2740, OV2740_REG_CHIP_ID, 3, &val);
	if (ret)
		return ret;

	if (val != OV2740_CHIP_ID) {
		dev_err(ov2740->dev, "chip id mismatch: %x != %x\n",
			OV2740_CHIP_ID, val);
		return -ENXIO;
	}

	dev_dbg(ov2740->dev, "chip id: 0x%x\n", val);

	ov2740->identified = true;

	return 0;
}

static int ov2740_update_digital_gain(struct ov2740 *ov2740, u32 d_gain)
{
	int ret;

	ret = ov2740_write_reg(ov2740, OV2740_REG_GROUP_ACCESS, 1,
			       OV2740_GROUP_HOLD_START);
	if (ret)
		return ret;

	ret = ov2740_write_reg(ov2740, OV2740_REG_MWB_R_GAIN, 2, d_gain);
	if (ret)
		return ret;

	ret = ov2740_write_reg(ov2740, OV2740_REG_MWB_G_GAIN, 2, d_gain);
	if (ret)
		return ret;

	ret = ov2740_write_reg(ov2740, OV2740_REG_MWB_B_GAIN, 2, d_gain);
	if (ret)
		return ret;

	ret = ov2740_write_reg(ov2740, OV2740_REG_GROUP_ACCESS, 1,
			       OV2740_GROUP_HOLD_END);
	if (ret)
		return ret;

	ret = ov2740_write_reg(ov2740, OV2740_REG_GROUP_ACCESS, 1,
			       OV2740_GROUP_HOLD_LAUNCH);
	return ret;
}

static int ov2740_test_pattern(struct ov2740 *ov2740, u32 pattern)
{
	if (pattern)
		pattern = (pattern - 1) << OV2740_TEST_PATTERN_BAR_SHIFT |
			  OV2740_TEST_PATTERN_ENABLE;

	return ov2740_write_reg(ov2740, OV2740_REG_TEST_PATTERN, 1, pattern);
}

static int ov2740_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov2740 *ov2740 = container_of(ctrl->handler,
					     struct ov2740, ctrl_handler);
	s64 exposure_max;
	int ret;

	/* Propagate change of current control to all related controls */
	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Update max exposure while meeting expected vblanking */
		exposure_max = ov2740->cur_mode->height + ctrl->val -
			       OV2740_EXPOSURE_MAX_MARGIN;
		__v4l2_ctrl_modify_range(ov2740->exposure,
					 ov2740->exposure->minimum,
					 exposure_max, ov2740->exposure->step,
					 exposure_max);
	}

	/* V4L2 controls values will be applied only when power is already up */
	if (!pm_runtime_get_if_in_use(ov2740->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov2740_write_reg(ov2740, OV2740_REG_ANALOG_GAIN, 2,
				       ctrl->val);
		break;

	case V4L2_CID_DIGITAL_GAIN:
		ret = ov2740_update_digital_gain(ov2740, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = ov2740_write_reg(ov2740, OV2740_REG_EXPOSURE, 3,
				       ctrl->val << 4);
		break;

	case V4L2_CID_VBLANK:
		ret = ov2740_write_reg(ov2740, OV2740_REG_VTS, 2,
				       ov2740->cur_mode->height + ctrl->val);
		break;

	case V4L2_CID_TEST_PATTERN:
		ret = ov2740_test_pattern(ov2740, ctrl->val);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(ov2740->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov2740_ctrl_ops = {
	.s_ctrl = ov2740_set_ctrl,
};

static int ov2740_init_controls(struct ov2740 *ov2740)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 exposure_max, h_blank, pixel_rate;
	u32 vblank_min, vblank_max, vblank_default;
	struct v4l2_fwnode_device_properties props;
	int ret;

	ctrl_hdlr = &ov2740->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 10);
	if (ret)
		return ret;

	ov2740->link_freq =
		v4l2_ctrl_new_int_menu(ctrl_hdlr, &ov2740_ctrl_ops,
				       V4L2_CID_LINK_FREQ,
				       ARRAY_SIZE(link_freq_menu_items) - 1,
				       ov2740->supported_modes->link_freq_index,
				       link_freq_menu_items);
	if (ov2740->link_freq)
		ov2740->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate = to_pixel_rate(ov2740->supported_modes->link_freq_index);
	ov2740->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ov2740_ctrl_ops,
					       V4L2_CID_PIXEL_RATE, 0,
					       pixel_rate, 1, pixel_rate);

	vblank_min = ov2740->cur_mode->vts_min - ov2740->cur_mode->height;
	vblank_max = ov2740->cur_mode->vts_max - ov2740->cur_mode->height;
	vblank_default = ov2740->cur_mode->vts_def - ov2740->cur_mode->height;
	ov2740->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov2740_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_min,
					   vblank_max, 1, vblank_default);

	h_blank = ov2740->cur_mode->hts - ov2740->cur_mode->width;
	ov2740->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov2740_ctrl_ops,
					   V4L2_CID_HBLANK, h_blank, h_blank, 1,
					   h_blank);
	if (ov2740->hblank)
		ov2740->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &ov2740_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV2740_ANAL_GAIN_MIN, OV2740_ANAL_GAIN_MAX,
			  OV2740_ANAL_GAIN_STEP, OV2740_ANAL_GAIN_MIN);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov2740_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV2740_DGTL_GAIN_MIN, OV2740_DGTL_GAIN_MAX,
			  OV2740_DGTL_GAIN_STEP, OV2740_DGTL_GAIN_DEFAULT);
	exposure_max = ov2740->cur_mode->vts_def - OV2740_EXPOSURE_MAX_MARGIN;
	ov2740->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ov2740_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     OV2740_EXPOSURE_MIN, exposure_max,
					     OV2740_EXPOSURE_STEP,
					     exposure_max);
	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov2740_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov2740_test_pattern_menu) - 1,
				     0, 0, ov2740_test_pattern_menu);

	ret = v4l2_fwnode_device_parse(ov2740->dev, &props);
	if (ret) {
		v4l2_ctrl_handler_free(ctrl_hdlr);
		return ret;
	}

	v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &ov2740_ctrl_ops, &props);

	if (ctrl_hdlr->error) {
		v4l2_ctrl_handler_free(ctrl_hdlr);
		return ctrl_hdlr->error;
	}

	ov2740->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

static void ov2740_update_pad_format(const struct ov2740_mode *mode,
				     struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	fmt->field = V4L2_FIELD_NONE;
}

static int ov2740_load_otp_data(struct nvm_data *nvm)
{
	struct device *dev = regmap_get_device(nvm->regmap);
	struct ov2740 *ov2740 = to_ov2740(dev_get_drvdata(dev));
	u32 isp_ctrl00 = 0;
	u32 isp_ctrl01 = 0;
	int ret;

	if (nvm->nvm_buffer)
		return 0;

	nvm->nvm_buffer = kzalloc(CUSTOMER_USE_OTP_SIZE, GFP_KERNEL);
	if (!nvm->nvm_buffer)
		return -ENOMEM;

	ret = ov2740_read_reg(ov2740, OV2740_REG_ISP_CTRL00, 1, &isp_ctrl00);
	if (ret) {
		dev_err(dev, "failed to read ISP CTRL00\n");
		goto err;
	}

	ret = ov2740_read_reg(ov2740, OV2740_REG_ISP_CTRL01, 1, &isp_ctrl01);
	if (ret) {
		dev_err(dev, "failed to read ISP CTRL01\n");
		goto err;
	}

	/* Clear bit 5 of ISP CTRL00 */
	ret = ov2740_write_reg(ov2740, OV2740_REG_ISP_CTRL00, 1,
			       isp_ctrl00 & ~BIT(5));
	if (ret) {
		dev_err(dev, "failed to set ISP CTRL00\n");
		goto err;
	}

	/* Clear bit 7 of ISP CTRL01 */
	ret = ov2740_write_reg(ov2740, OV2740_REG_ISP_CTRL01, 1,
			       isp_ctrl01 & ~BIT(7));
	if (ret) {
		dev_err(dev, "failed to set ISP CTRL01\n");
		goto err;
	}

	ret = ov2740_write_reg(ov2740, OV2740_REG_MODE_SELECT, 1,
			       OV2740_MODE_STREAMING);
	if (ret) {
		dev_err(dev, "failed to set streaming mode\n");
		goto err;
	}

	/*
	 * Users are not allowed to access OTP-related registers and memory
	 * during the 20 ms period after streaming starts (0x100 = 0x01).
	 */
	msleep(20);

	ret = regmap_bulk_read(nvm->regmap, OV2740_REG_OTP_CUSTOMER,
			       nvm->nvm_buffer, CUSTOMER_USE_OTP_SIZE);
	if (ret) {
		dev_err(dev, "failed to read OTP data, ret %d\n", ret);
		goto err;
	}

	ret = ov2740_write_reg(ov2740, OV2740_REG_MODE_SELECT, 1,
			       OV2740_MODE_STANDBY);
	if (ret) {
		dev_err(dev, "failed to set streaming mode\n");
		goto err;
	}

	ret = ov2740_write_reg(ov2740, OV2740_REG_ISP_CTRL01, 1, isp_ctrl01);
	if (ret) {
		dev_err(dev, "failed to set ISP CTRL01\n");
		goto err;
	}

	ret = ov2740_write_reg(ov2740, OV2740_REG_ISP_CTRL00, 1, isp_ctrl00);
	if (ret) {
		dev_err(dev, "failed to set ISP CTRL00\n");
		goto err;
	}

	return 0;
err:
	kfree(nvm->nvm_buffer);
	nvm->nvm_buffer = NULL;

	return ret;
}

static int ov2740_start_streaming(struct ov2740 *ov2740)
{
	const struct ov2740_reg_list *reg_list;
	int link_freq_index;
	int ret;

	ret = ov2740_identify_module(ov2740);
	if (ret)
		return ret;

	if (ov2740->nvm)
		ov2740_load_otp_data(ov2740->nvm);

	/* Reset the sensor */
	ret = ov2740_write_reg(ov2740, 0x0103, 1, 0x01);
	if (ret) {
		dev_err(ov2740->dev, "failed to reset\n");
		return ret;
	}

	usleep_range(10000, 15000);

	link_freq_index = ov2740->cur_mode->link_freq_index;
	reg_list = &link_freq_configs[link_freq_index].reg_list;
	ret = ov2740_write_reg_list(ov2740, reg_list);
	if (ret) {
		dev_err(ov2740->dev, "failed to set plls\n");
		return ret;
	}

	reg_list = &ov2740->cur_mode->reg_list;
	ret = ov2740_write_reg_list(ov2740, reg_list);
	if (ret) {
		dev_err(ov2740->dev, "failed to set mode\n");
		return ret;
	}

	ret = __v4l2_ctrl_handler_setup(ov2740->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = ov2740_write_reg(ov2740, OV2740_REG_MODE_SELECT, 1,
			       OV2740_MODE_STREAMING);
	if (ret)
		dev_err(ov2740->dev, "failed to start streaming\n");

	return ret;
}

static void ov2740_stop_streaming(struct ov2740 *ov2740)
{
	if (ov2740_write_reg(ov2740, OV2740_REG_MODE_SELECT, 1,
			     OV2740_MODE_STANDBY))
		dev_err(ov2740->dev, "failed to stop streaming\n");
}

static int ov2740_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov2740 *ov2740 = to_ov2740(sd);
	struct v4l2_subdev_state *sd_state;
	int ret = 0;

	sd_state = v4l2_subdev_lock_and_get_active_state(&ov2740->sd);

	if (enable) {
		ret = pm_runtime_resume_and_get(ov2740->dev);
		if (ret < 0)
			goto out_unlock;

		ret = ov2740_start_streaming(ov2740);
		if (ret) {
			enable = 0;
			ov2740_stop_streaming(ov2740);
			pm_runtime_put(ov2740->dev);
		}
	} else {
		ov2740_stop_streaming(ov2740);
		pm_runtime_put(ov2740->dev);
	}

out_unlock:
	v4l2_subdev_unlock_state(sd_state);

	return ret;
}

static int ov2740_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *fmt)
{
	struct ov2740 *ov2740 = to_ov2740(sd);
	const struct ov2740_mode *mode;
	s32 vblank_def, h_blank;

	mode = v4l2_find_nearest_size(ov2740->supported_modes,
				      ov2740->supported_modes_count,
				      width, height,
				      fmt->format.width, fmt->format.height);

	ov2740_update_pad_format(mode, &fmt->format);
	*v4l2_subdev_state_get_format(sd_state, fmt->pad) = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	ov2740->cur_mode = mode;
	__v4l2_ctrl_s_ctrl(ov2740->link_freq, mode->link_freq_index);
	__v4l2_ctrl_s_ctrl_int64(ov2740->pixel_rate,
				 to_pixel_rate(mode->link_freq_index));

	/* Update limits and set FPS to default */
	vblank_def = mode->vts_def - mode->height;
	__v4l2_ctrl_modify_range(ov2740->vblank,
				 mode->vts_min - mode->height,
				 mode->vts_max - mode->height, 1, vblank_def);
	__v4l2_ctrl_s_ctrl(ov2740->vblank, vblank_def);
	h_blank = mode->hts - mode->width;
	__v4l2_ctrl_modify_range(ov2740->hblank, h_blank, h_blank, 1, h_blank);

	return 0;
}

static int ov2740_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int ov2740_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct ov2740 *ov2740 = to_ov2740(sd);
	const struct ov2740_mode *supported_modes = ov2740->supported_modes;

	if (fse->index >= ov2740->supported_modes_count)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SGRBG10_1X10)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int ov2740_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct ov2740 *ov2740 = to_ov2740(sd);

	ov2740_update_pad_format(&ov2740->supported_modes[0],
				 v4l2_subdev_state_get_format(sd_state, 0));
	return 0;
}

static const struct v4l2_subdev_video_ops ov2740_video_ops = {
	.s_stream = ov2740_set_stream,
};

static const struct v4l2_subdev_pad_ops ov2740_pad_ops = {
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ov2740_set_format,
	.enum_mbus_code = ov2740_enum_mbus_code,
	.enum_frame_size = ov2740_enum_frame_size,
};

static const struct v4l2_subdev_ops ov2740_subdev_ops = {
	.video = &ov2740_video_ops,
	.pad = &ov2740_pad_ops,
};

static const struct v4l2_subdev_internal_ops ov2740_internal_ops = {
	.init_state = ov2740_init_state,
};

static const struct media_entity_operations ov2740_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int ov2740_check_hwcfg(struct ov2740 *ov2740)
{
	struct device *dev = ov2740->dev;
	struct fwnode_handle *ep;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int ret;
	unsigned int i, j;

	/*
	 * Sometimes the fwnode graph is initialized by the bridge driver,
	 * wait for this.
	 */
	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "waiting for fwnode graph endpoint\n");

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return dev_err_probe(dev, ret, "parsing endpoint failed\n");

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != OV2740_DATA_LANES) {
		ret = dev_err_probe(dev, -EINVAL,
				    "number of CSI2 data lanes %d is not supported\n",
				    bus_cfg.bus.mipi_csi2.num_data_lanes);
		goto check_hwcfg_error;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		ret = dev_err_probe(dev, -EINVAL, "no link frequencies defined\n");
		goto check_hwcfg_error;
	}

	for (i = 0; i < ARRAY_SIZE(link_freq_menu_items); i++) {
		for (j = 0; j < bus_cfg.nr_of_link_frequencies; j++) {
			if (link_freq_menu_items[i] ==
				bus_cfg.link_frequencies[j])
				break;
		}

		if (j == bus_cfg.nr_of_link_frequencies)
			continue;

		switch (i) {
		case OV2740_LINK_FREQ_360MHZ_INDEX:
			ov2740->supported_modes = supported_modes_360mhz;
			ov2740->supported_modes_count =
				ARRAY_SIZE(supported_modes_360mhz);
			break;
		case OV2740_LINK_FREQ_180MHZ_INDEX:
			ov2740->supported_modes = supported_modes_180mhz;
			ov2740->supported_modes_count =
				ARRAY_SIZE(supported_modes_180mhz);
			break;
		}

		break; /* Prefer modes from first available link-freq */
	}

	if (!ov2740->supported_modes)
		ret = dev_err_probe(dev, -EINVAL,
				    "no supported link frequencies\n");

check_hwcfg_error:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static void ov2740_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_subdev_cleanup(sd);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(&client->dev);
}

static int ov2740_nvmem_read(void *priv, unsigned int off, void *val,
			     size_t count)
{
	struct nvm_data *nvm = priv;
	struct device *dev = regmap_get_device(nvm->regmap);
	struct ov2740 *ov2740 = to_ov2740(dev_get_drvdata(dev));
	struct v4l2_subdev_state *sd_state;
	int ret = 0;

	/* Serialise sensor access */
	sd_state = v4l2_subdev_lock_and_get_active_state(&ov2740->sd);

	if (nvm->nvm_buffer) {
		memcpy(val, nvm->nvm_buffer + off, count);
		goto exit;
	}

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		goto exit;
	}

	ret = ov2740_load_otp_data(nvm);
	if (!ret)
		memcpy(val, nvm->nvm_buffer + off, count);

	pm_runtime_put(dev);
exit:
	v4l2_subdev_unlock_state(sd_state);
	return ret;
}

static int ov2740_register_nvmem(struct i2c_client *client,
				 struct ov2740 *ov2740)
{
	struct nvm_data *nvm;
	struct regmap_config regmap_config = { };
	struct nvmem_config nvmem_config = { };
	struct regmap *regmap;
	struct device *dev = ov2740->dev;

	nvm = devm_kzalloc(dev, sizeof(*nvm), GFP_KERNEL);
	if (!nvm)
		return -ENOMEM;

	regmap_config.val_bits = 8;
	regmap_config.reg_bits = 16;
	regmap_config.disable_locking = true;
	regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	nvm->regmap = regmap;

	nvmem_config.name = dev_name(dev);
	nvmem_config.dev = dev;
	nvmem_config.read_only = true;
	nvmem_config.root_only = true;
	nvmem_config.owner = THIS_MODULE;
	nvmem_config.compat = true;
	nvmem_config.base_dev = dev;
	nvmem_config.reg_read = ov2740_nvmem_read;
	nvmem_config.reg_write = NULL;
	nvmem_config.priv = nvm;
	nvmem_config.stride = 1;
	nvmem_config.word_size = 1;
	nvmem_config.size = CUSTOMER_USE_OTP_SIZE;

	nvm->nvmem = devm_nvmem_register(dev, &nvmem_config);
	if (IS_ERR(nvm->nvmem))
		return PTR_ERR(nvm->nvmem);

	ov2740->nvm = nvm;
	return 0;
}

static int ov2740_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov2740 *ov2740 = to_ov2740(sd);

	gpiod_set_value_cansleep(ov2740->reset_gpio, 1);
	gpiod_set_value_cansleep(ov2740->powerdown_gpio, 1);
	clk_disable_unprepare(ov2740->clk);
	regulator_bulk_disable(ARRAY_SIZE(ov2740_supply_name),
			       ov2740->supplies);
	return 0;
}

static int ov2740_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov2740 *ov2740 = to_ov2740(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ov2740_supply_name),
				    ov2740->supplies);
	if (ret)
		return ret;

	ret = clk_prepare_enable(ov2740->clk);
	if (ret) {
		regulator_bulk_disable(ARRAY_SIZE(ov2740_supply_name),
				       ov2740->supplies);
		return ret;
	}

	gpiod_set_value_cansleep(ov2740->powerdown_gpio, 0);
	gpiod_set_value_cansleep(ov2740->reset_gpio, 0);
	msleep(20);

	return 0;
}

static int ov2740_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ov2740 *ov2740;
	unsigned long freq;
	bool full_power;
	unsigned int i;
	int ret;

	ov2740 = devm_kzalloc(&client->dev, sizeof(*ov2740), GFP_KERNEL);
	if (!ov2740)
		return -ENOMEM;

	ov2740->dev = &client->dev;

	v4l2_i2c_subdev_init(&ov2740->sd, client, &ov2740_subdev_ops);
	ov2740->sd.internal_ops = &ov2740_internal_ops;

	ret = ov2740_check_hwcfg(ov2740);
	if (ret)
		return ret;

	ov2740->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ov2740->reset_gpio)) {
		return dev_err_probe(dev, PTR_ERR(ov2740->reset_gpio),
				     "failed to get reset GPIO\n");
	}

	ov2740->powerdown_gpio = devm_gpiod_get_optional(dev, "powerdown", GPIOD_OUT_HIGH);
	if (IS_ERR(ov2740->powerdown_gpio)) {
		return dev_err_probe(dev, PTR_ERR(ov2740->powerdown_gpio),
				     "failed to get powerdown GPIO\n");
	}

	if (ov2740->reset_gpio || ov2740->powerdown_gpio) {
		/*
		 * Ensure reset/powerdown is asserted for at least 20 ms before
		 * ov2740_resume() deasserts it.
		 */
		msleep(20);
	}

	ov2740->clk = devm_v4l2_sensor_clk_get(dev, "clk");
	if (IS_ERR(ov2740->clk))
		return dev_err_probe(dev, PTR_ERR(ov2740->clk),
				     "failed to get clock\n");

	freq = clk_get_rate(ov2740->clk);
	if (freq != OV2740_MCLK)
		return dev_err_probe(dev, -EINVAL,
				     "external clock %lu is not supported\n",
				     freq);

	for (i = 0; i < ARRAY_SIZE(ov2740_supply_name); i++)
		ov2740->supplies[i].supply = ov2740_supply_name[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ov2740_supply_name),
				      ov2740->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	full_power = acpi_dev_state_d0(ov2740->dev);
	if (full_power) {
		/* ACPI does not always clear the reset GPIO / enable the clock */
		ret = ov2740_resume(dev);
		if (ret)
			return dev_err_probe(dev, ret, "failed to power on sensor\n");

		ret = ov2740_identify_module(ov2740);
		if (ret) {
			dev_err_probe(dev, ret, "failed to find sensor\n");
			goto probe_error_power_off;
		}
	}

	ov2740->cur_mode = &ov2740->supported_modes[0];
	ret = ov2740_init_controls(ov2740);
	if (ret) {
		dev_err_probe(dev, ret, "failed to init controls\n");
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ov2740->sd.state_lock = ov2740->ctrl_handler.lock;
	ov2740->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov2740->sd.entity.ops = &ov2740_subdev_entity_ops;
	ov2740->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ov2740->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&ov2740->sd.entity, 1, &ov2740->pad);
	if (ret) {
		dev_err_probe(dev, ret, "failed to init entity pads\n");
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ret = v4l2_subdev_init_finalize(&ov2740->sd);
	if (ret)
		goto probe_error_media_entity_cleanup;

	/* Set the device's state to active if it's in D0 state. */
	if (full_power)
		pm_runtime_set_active(ov2740->dev);
	pm_runtime_enable(ov2740->dev);
	pm_runtime_idle(ov2740->dev);

	ret = v4l2_async_register_subdev_sensor(&ov2740->sd);
	if (ret < 0) {
		dev_err_probe(dev, ret, "failed to register V4L2 subdev\n");
		goto probe_error_v4l2_subdev_cleanup;
	}

	ret = ov2740_register_nvmem(client, ov2740);
	if (ret)
		dev_warn(ov2740->dev, "register nvmem failed, ret %d\n", ret);

	return 0;

probe_error_v4l2_subdev_cleanup:
	pm_runtime_disable(ov2740->dev);
	pm_runtime_set_suspended(ov2740->dev);
	v4l2_subdev_cleanup(&ov2740->sd);

probe_error_media_entity_cleanup:
	media_entity_cleanup(&ov2740->sd.entity);

probe_error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(ov2740->sd.ctrl_handler);

probe_error_power_off:
	if (full_power)
		ov2740_suspend(dev);

	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(ov2740_pm_ops, ov2740_suspend, ov2740_resume,
				 NULL);

static const struct acpi_device_id ov2740_acpi_ids[] = {
	{"INT3474"},
	{}
};

MODULE_DEVICE_TABLE(acpi, ov2740_acpi_ids);

static struct i2c_driver ov2740_i2c_driver = {
	.driver = {
		.name = "ov2740",
		.acpi_match_table = ov2740_acpi_ids,
		.pm = pm_sleep_ptr(&ov2740_pm_ops),
	},
	.probe = ov2740_probe,
	.remove = ov2740_remove,
	.flags = I2C_DRV_ACPI_WAIVE_D0_PROBE,
};

module_i2c_driver(ov2740_i2c_driver);

MODULE_AUTHOR("Qiu, Tianshu <tian.shu.qiu@intel.com>");
MODULE_AUTHOR("Shawn Tu");
MODULE_AUTHOR("Bingbu Cao <bingbu.cao@intel.com>");
MODULE_DESCRIPTION("OmniVision OV2740 sensor driver");
MODULE_LICENSE("GPL v2");
