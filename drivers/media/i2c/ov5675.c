// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Intel Corporation.

#include <asm/unaligned.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define OV5675_REG_VALUE_08BIT		1
#define OV5675_REG_VALUE_16BIT		2
#define OV5675_REG_VALUE_24BIT		3

#define OV5675_LINK_FREQ_450MHZ		450000000ULL
#define OV5675_SCLK			90000000LL
#define OV5675_MCLK			19200000
#define OV5675_DATA_LANES		2
#define OV5675_RGB_DEPTH		10

#define OV5675_REG_CHIP_ID		0x300a
#define OV5675_CHIP_ID			0x5675

#define OV5675_REG_MODE_SELECT		0x0100
#define OV5675_MODE_STANDBY		0x00
#define OV5675_MODE_STREAMING		0x01

/* vertical-timings from sensor */
#define OV5675_REG_VTS			0x380e
#define OV5675_VTS_30FPS		0x07e4
#define OV5675_VTS_30FPS_MIN		0x07e4
#define OV5675_VTS_MAX			0x7fff

/* horizontal-timings from sensor */
#define OV5675_REG_HTS			0x380c

/* Exposure controls from sensor */
#define OV5675_REG_EXPOSURE		0x3500
#define	OV5675_EXPOSURE_MIN		4
#define OV5675_EXPOSURE_MAX_MARGIN	4
#define	OV5675_EXPOSURE_STEP		1

/* Analog gain controls from sensor */
#define OV5675_REG_ANALOG_GAIN		0x3508
#define	OV5675_ANAL_GAIN_MIN		128
#define	OV5675_ANAL_GAIN_MAX		2047
#define	OV5675_ANAL_GAIN_STEP		1

/* Digital gain controls from sensor */
#define OV5675_REG_MWB_R_GAIN		0x5019
#define OV5675_REG_MWB_G_GAIN		0x501b
#define OV5675_REG_MWB_B_GAIN		0x501d
#define OV5675_DGTL_GAIN_MIN		0
#define OV5675_DGTL_GAIN_MAX		4095
#define OV5675_DGTL_GAIN_STEP		1
#define OV5675_DGTL_GAIN_DEFAULT	1024

/* Test Pattern Control */
#define OV5675_REG_TEST_PATTERN		0x4503
#define OV5675_TEST_PATTERN_ENABLE	BIT(7)
#define OV5675_TEST_PATTERN_BAR_SHIFT	2

/* Flip Mirror Controls from sensor */
#define OV5675_REG_FORMAT1		0x3820
#define OV5675_REG_FORMAT2		0x373d

#define to_ov5675(_sd)			container_of(_sd, struct ov5675, sd)

enum {
	OV5675_LINK_FREQ_900MBPS,
};

struct ov5675_reg {
	u16 address;
	u8 val;
};

struct ov5675_reg_list {
	u32 num_of_regs;
	const struct ov5675_reg *regs;
};

struct ov5675_link_freq_config {
	const struct ov5675_reg_list reg_list;
};

struct ov5675_mode {
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
	const struct ov5675_reg_list reg_list;
};

static const struct ov5675_reg mipi_data_rate_900mbps[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x0300, 0x04},
	{0x0302, 0x8d},
	{0x0303, 0x00},
	{0x030d, 0x26},
};

static const struct ov5675_reg mode_2592x1944_regs[] = {
	{0x3002, 0x21},
	{0x3107, 0x23},
	{0x3501, 0x20},
	{0x3503, 0x0c},
	{0x3508, 0x03},
	{0x3509, 0x00},
	{0x3600, 0x66},
	{0x3602, 0x30},
	{0x3610, 0xa5},
	{0x3612, 0x93},
	{0x3620, 0x80},
	{0x3642, 0x0e},
	{0x3661, 0x00},
	{0x3662, 0x10},
	{0x3664, 0xf3},
	{0x3665, 0x9e},
	{0x3667, 0xa5},
	{0x366e, 0x55},
	{0x366f, 0x55},
	{0x3670, 0x11},
	{0x3671, 0x11},
	{0x3672, 0x11},
	{0x3673, 0x11},
	{0x3714, 0x24},
	{0x371a, 0x3e},
	{0x3733, 0x10},
	{0x3734, 0x00},
	{0x373d, 0x24},
	{0x3764, 0x20},
	{0x3765, 0x20},
	{0x3766, 0x12},
	{0x37a1, 0x14},
	{0x37a8, 0x1c},
	{0x37ab, 0x0f},
	{0x37c2, 0x04},
	{0x37cb, 0x00},
	{0x37cc, 0x00},
	{0x37cd, 0x00},
	{0x37ce, 0x00},
	{0x37d8, 0x02},
	{0x37d9, 0x08},
	{0x37dc, 0x04},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0xb3},
	{0x3808, 0x0a},
	{0x3809, 0x20},
	{0x380a, 0x07},
	{0x380b, 0x98},
	{0x380c, 0x02},
	{0x380d, 0xee},
	{0x380e, 0x07},
	{0x380f, 0xe4},
	{0x3811, 0x10},
	{0x3813, 0x0d},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x381e, 0x02},
	{0x3820, 0x88},
	{0x3821, 0x01},
	{0x3832, 0x04},
	{0x3c80, 0x01},
	{0x3c82, 0x00},
	{0x3c83, 0xc8},
	{0x3c8c, 0x0f},
	{0x3c8d, 0xa0},
	{0x3c90, 0x07},
	{0x3c91, 0x00},
	{0x3c92, 0x00},
	{0x3c93, 0x00},
	{0x3c94, 0xd0},
	{0x3c95, 0x50},
	{0x3c96, 0x35},
	{0x3c97, 0x00},
	{0x4001, 0xe0},
	{0x4008, 0x02},
	{0x4009, 0x0d},
	{0x400f, 0x80},
	{0x4013, 0x02},
	{0x4040, 0x00},
	{0x4041, 0x07},
	{0x404c, 0x50},
	{0x404e, 0x20},
	{0x4500, 0x06},
	{0x4503, 0x00},
	{0x450a, 0x04},
	{0x4809, 0x04},
	{0x480c, 0x12},
	{0x4819, 0x70},
	{0x4825, 0x32},
	{0x4826, 0x32},
	{0x482a, 0x06},
	{0x4833, 0x08},
	{0x4837, 0x0d},
	{0x5000, 0x77},
	{0x5b00, 0x01},
	{0x5b01, 0x10},
	{0x5b02, 0x01},
	{0x5b03, 0xdb},
	{0x5b05, 0x6c},
	{0x5e10, 0xfc},
	{0x3500, 0x00},
	{0x3501, 0x3E},
	{0x3502, 0x60},
	{0x3503, 0x08},
	{0x3508, 0x04},
	{0x3509, 0x00},
	{0x3832, 0x48},
	{0x5780, 0x3e},
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
	{0x5791, 0x06},
	{0x5792, 0x00},
	{0x5793, 0x52},
	{0x5794, 0xa3},
	{0x4003, 0x40},
	{0x3107, 0x01},
	{0x3c80, 0x08},
	{0x3c83, 0xb1},
	{0x3c8c, 0x10},
	{0x3c8d, 0x00},
	{0x3c90, 0x00},
	{0x3c94, 0x00},
	{0x3c95, 0x00},
	{0x3c96, 0x00},
	{0x37cb, 0x09},
	{0x37cc, 0x15},
	{0x37cd, 0x1f},
	{0x37ce, 0x1f},
};

static const struct ov5675_reg mode_1296x972_regs[] = {
	{0x3002, 0x21},
	{0x3107, 0x23},
	{0x3501, 0x20},
	{0x3503, 0x0c},
	{0x3508, 0x03},
	{0x3509, 0x00},
	{0x3600, 0x66},
	{0x3602, 0x30},
	{0x3610, 0xa5},
	{0x3612, 0x93},
	{0x3620, 0x80},
	{0x3642, 0x0e},
	{0x3661, 0x00},
	{0x3662, 0x08},
	{0x3664, 0xf3},
	{0x3665, 0x9e},
	{0x3667, 0xa5},
	{0x366e, 0x55},
	{0x366f, 0x55},
	{0x3670, 0x11},
	{0x3671, 0x11},
	{0x3672, 0x11},
	{0x3673, 0x11},
	{0x3714, 0x28},
	{0x371a, 0x3e},
	{0x3733, 0x10},
	{0x3734, 0x00},
	{0x373d, 0x24},
	{0x3764, 0x20},
	{0x3765, 0x20},
	{0x3766, 0x12},
	{0x37a1, 0x14},
	{0x37a8, 0x1c},
	{0x37ab, 0x0f},
	{0x37c2, 0x14},
	{0x37cb, 0x00},
	{0x37cc, 0x00},
	{0x37cd, 0x00},
	{0x37ce, 0x00},
	{0x37d8, 0x02},
	{0x37d9, 0x04},
	{0x37dc, 0x04},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0xb7},
	{0x3808, 0x05},
	{0x3809, 0x10},
	{0x380a, 0x03},
	{0x380b, 0xcc},
	{0x380c, 0x02},
	{0x380d, 0xee},
	{0x380e, 0x07},
	{0x380f, 0xd0},
	{0x3811, 0x08},
	{0x3813, 0x0d},
	{0x3814, 0x03},
	{0x3815, 0x01},
	{0x3816, 0x03},
	{0x3817, 0x01},
	{0x381e, 0x02},
	{0x3820, 0x8b},
	{0x3821, 0x01},
	{0x3832, 0x04},
	{0x3c80, 0x01},
	{0x3c82, 0x00},
	{0x3c83, 0xc8},
	{0x3c8c, 0x0f},
	{0x3c8d, 0xa0},
	{0x3c90, 0x07},
	{0x3c91, 0x00},
	{0x3c92, 0x00},
	{0x3c93, 0x00},
	{0x3c94, 0xd0},
	{0x3c95, 0x50},
	{0x3c96, 0x35},
	{0x3c97, 0x00},
	{0x4001, 0xe0},
	{0x4008, 0x00},
	{0x4009, 0x07},
	{0x400f, 0x80},
	{0x4013, 0x02},
	{0x4040, 0x00},
	{0x4041, 0x03},
	{0x404c, 0x50},
	{0x404e, 0x20},
	{0x4500, 0x06},
	{0x4503, 0x00},
	{0x450a, 0x04},
	{0x4809, 0x04},
	{0x480c, 0x12},
	{0x4819, 0x70},
	{0x4825, 0x32},
	{0x4826, 0x32},
	{0x482a, 0x06},
	{0x4833, 0x08},
	{0x4837, 0x0d},
	{0x5000, 0x77},
	{0x5b00, 0x01},
	{0x5b01, 0x10},
	{0x5b02, 0x01},
	{0x5b03, 0xdb},
	{0x5b05, 0x6c},
	{0x5e10, 0xfc},
	{0x3500, 0x00},
	{0x3501, 0x1F},
	{0x3502, 0x20},
	{0x3503, 0x08},
	{0x3508, 0x04},
	{0x3509, 0x00},
	{0x3832, 0x48},
	{0x5780, 0x3e},
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
	{0x5791, 0x06},
	{0x5792, 0x00},
	{0x5793, 0x52},
	{0x5794, 0xa3},
	{0x4003, 0x40},
	{0x3107, 0x01},
	{0x3c80, 0x08},
	{0x3c83, 0xb1},
	{0x3c8c, 0x10},
	{0x3c8d, 0x00},
	{0x3c90, 0x00},
	{0x3c94, 0x00},
	{0x3c95, 0x00},
	{0x3c96, 0x00},
	{0x37cb, 0x09},
	{0x37cc, 0x15},
	{0x37cd, 0x1f},
	{0x37ce, 0x1f},
};

static const char * const ov5675_test_pattern_menu[] = {
	"Disabled",
	"Standard Color Bar",
	"Top-Bottom Darker Color Bar",
	"Right-Left Darker Color Bar",
	"Bottom-Top Darker Color Bar"
};

static const s64 link_freq_menu_items[] = {
	OV5675_LINK_FREQ_450MHZ,
};

static const struct ov5675_link_freq_config link_freq_configs[] = {
	[OV5675_LINK_FREQ_900MBPS] = {
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_900mbps),
			.regs = mipi_data_rate_900mbps,
		}
	}
};

static const struct ov5675_mode supported_modes[] = {
	{
		.width = 2592,
		.height = 1944,
		.hts = 1500,
		.vts_def = OV5675_VTS_30FPS,
		.vts_min = OV5675_VTS_30FPS_MIN,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_2592x1944_regs),
			.regs = mode_2592x1944_regs,
		},
		.link_freq_index = OV5675_LINK_FREQ_900MBPS,
	},
	{
		.width = 1296,
		.height = 972,
		.hts = 1500,
		.vts_def = OV5675_VTS_30FPS,
		.vts_min = OV5675_VTS_30FPS_MIN,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1296x972_regs),
			.regs = mode_1296x972_regs,
		},
		.link_freq_index = OV5675_LINK_FREQ_900MBPS,
	}
};

struct ov5675 {
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
	const struct ov5675_mode *cur_mode;

	/* To serialize asynchronus callbacks */
	struct mutex mutex;

	/* Streaming on/off */
	bool streaming;
};

static u64 to_pixel_rate(u32 f_index)
{
	u64 pixel_rate = link_freq_menu_items[f_index] * 2 * OV5675_DATA_LANES;

	do_div(pixel_rate, OV5675_RGB_DEPTH);

	return pixel_rate;
}

static u64 to_pixels_per_line(u32 hts, u32 f_index)
{
	u64 ppl = hts * to_pixel_rate(f_index);

	do_div(ppl, OV5675_SCLK);

	return ppl;
}

static int ov5675_read_reg(struct ov5675 *ov5675, u16 reg, u16 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov5675->sd);
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

static int ov5675_write_reg(struct ov5675 *ov5675, u16 reg, u16 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov5675->sd);
	u8 buf[6];

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << 8 * (4 - len), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int ov5675_write_reg_list(struct ov5675 *ov5675,
				 const struct ov5675_reg_list *r_list)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov5675->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < r_list->num_of_regs; i++) {
		ret = ov5675_write_reg(ov5675, r_list->regs[i].address, 1,
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

static int ov5675_update_digital_gain(struct ov5675 *ov5675, u32 d_gain)
{
	int ret;

	ret = ov5675_write_reg(ov5675, OV5675_REG_MWB_R_GAIN,
			       OV5675_REG_VALUE_16BIT, d_gain);
	if (ret)
		return ret;

	ret = ov5675_write_reg(ov5675, OV5675_REG_MWB_G_GAIN,
			       OV5675_REG_VALUE_16BIT, d_gain);
	if (ret)
		return ret;

	return ov5675_write_reg(ov5675, OV5675_REG_MWB_B_GAIN,
				OV5675_REG_VALUE_16BIT, d_gain);
}

static int ov5675_test_pattern(struct ov5675 *ov5675, u32 pattern)
{
	if (pattern)
		pattern = (pattern - 1) << OV5675_TEST_PATTERN_BAR_SHIFT |
			  OV5675_TEST_PATTERN_ENABLE;

	return ov5675_write_reg(ov5675, OV5675_REG_TEST_PATTERN,
				OV5675_REG_VALUE_08BIT, pattern);
}

/*
 * OV5675 supports keeping the pixel order by mirror and flip function
 * The Bayer order isn't affected by the flip controls
 */
static int ov5675_set_ctrl_hflip(struct ov5675 *ov5675, u32 ctrl_val)
{
	int ret;
	u32 val;

	ret = ov5675_read_reg(ov5675, OV5675_REG_FORMAT1,
			      OV5675_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	return ov5675_write_reg(ov5675, OV5675_REG_FORMAT1,
				OV5675_REG_VALUE_08BIT,
				ctrl_val ? val & ~BIT(3) : val);
}

static int ov5675_set_ctrl_vflip(struct ov5675 *ov5675, u8 ctrl_val)
{
	int ret;
	u32 val;

	ret = ov5675_read_reg(ov5675, OV5675_REG_FORMAT1,
			      OV5675_REG_VALUE_08BIT, &val);
	if (ret)
		return ret;

	ret = ov5675_write_reg(ov5675, OV5675_REG_FORMAT1,
			       OV5675_REG_VALUE_08BIT,
			       ctrl_val ? val | BIT(4) | BIT(5)  : val);

	if (ret)
		return ret;

	ret = ov5675_read_reg(ov5675, OV5675_REG_FORMAT2,
			      OV5675_REG_VALUE_08BIT, &val);

	if (ret)
		return ret;

	return ov5675_write_reg(ov5675, OV5675_REG_FORMAT2,
				OV5675_REG_VALUE_08BIT,
				ctrl_val ? val | BIT(1) : val);
}

static int ov5675_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov5675 *ov5675 = container_of(ctrl->handler,
					     struct ov5675, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&ov5675->sd);
	s64 exposure_max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Update max exposure while meeting expected vblanking */
		exposure_max = ov5675->cur_mode->height + ctrl->val -
			OV5675_EXPOSURE_MAX_MARGIN;
		__v4l2_ctrl_modify_range(ov5675->exposure,
					 ov5675->exposure->minimum,
					 exposure_max, ov5675->exposure->step,
					 exposure_max);
	}

	/* V4L2 controls values will be applied only when power is already up */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov5675_write_reg(ov5675, OV5675_REG_ANALOG_GAIN,
				       OV5675_REG_VALUE_16BIT, ctrl->val);
		break;

	case V4L2_CID_DIGITAL_GAIN:
		ret = ov5675_update_digital_gain(ov5675, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part
		 * val = val << 4
		 * for ov5675, the unit of exposure is differnt from other
		 * OmniVision sensors, its exposure value is twice of the
		 * register value, the exposure should be divided by 2 before
		 * set register, e.g. val << 3.
		 */
		ret = ov5675_write_reg(ov5675, OV5675_REG_EXPOSURE,
				       OV5675_REG_VALUE_24BIT, ctrl->val << 3);
		break;

	case V4L2_CID_VBLANK:
		ret = ov5675_write_reg(ov5675, OV5675_REG_VTS,
				       OV5675_REG_VALUE_16BIT,
				       ov5675->cur_mode->height + ctrl->val +
				       10);
		break;

	case V4L2_CID_TEST_PATTERN:
		ret = ov5675_test_pattern(ov5675, ctrl->val);
		break;

	case V4L2_CID_HFLIP:
		ov5675_set_ctrl_hflip(ov5675, ctrl->val);
		break;

	case V4L2_CID_VFLIP:
		ov5675_set_ctrl_vflip(ov5675, ctrl->val);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov5675_ctrl_ops = {
	.s_ctrl = ov5675_set_ctrl,
};

static int ov5675_init_controls(struct ov5675 *ov5675)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 exposure_max, h_blank;
	int ret;

	ctrl_hdlr = &ov5675->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	ctrl_hdlr->lock = &ov5675->mutex;
	ov5675->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &ov5675_ctrl_ops,
					   V4L2_CID_LINK_FREQ,
					   ARRAY_SIZE(link_freq_menu_items) - 1,
					   0, link_freq_menu_items);
	if (ov5675->link_freq)
		ov5675->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ov5675->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ov5675_ctrl_ops,
				       V4L2_CID_PIXEL_RATE, 0,
				       to_pixel_rate(OV5675_LINK_FREQ_900MBPS),
				       1,
				       to_pixel_rate(OV5675_LINK_FREQ_900MBPS));
	ov5675->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov5675_ctrl_ops,
			  V4L2_CID_VBLANK,
			  ov5675->cur_mode->vts_min - ov5675->cur_mode->height,
			  OV5675_VTS_MAX - ov5675->cur_mode->height, 1,
			  ov5675->cur_mode->vts_def - ov5675->cur_mode->height);
	h_blank = to_pixels_per_line(ov5675->cur_mode->hts,
		  ov5675->cur_mode->link_freq_index) - ov5675->cur_mode->width;
	ov5675->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov5675_ctrl_ops,
					   V4L2_CID_HBLANK, h_blank, h_blank, 1,
					   h_blank);
	if (ov5675->hblank)
		ov5675->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &ov5675_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV5675_ANAL_GAIN_MIN, OV5675_ANAL_GAIN_MAX,
			  OV5675_ANAL_GAIN_STEP, OV5675_ANAL_GAIN_MIN);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov5675_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV5675_DGTL_GAIN_MIN, OV5675_DGTL_GAIN_MAX,
			  OV5675_DGTL_GAIN_STEP, OV5675_DGTL_GAIN_DEFAULT);
	exposure_max = (ov5675->cur_mode->vts_def - OV5675_EXPOSURE_MAX_MARGIN);
	ov5675->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ov5675_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     OV5675_EXPOSURE_MIN, exposure_max,
					     OV5675_EXPOSURE_STEP,
					     exposure_max);
	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov5675_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov5675_test_pattern_menu) - 1,
				     0, 0, ov5675_test_pattern_menu);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov5675_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov5675_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (ctrl_hdlr->error)
		return ctrl_hdlr->error;

	ov5675->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

static void ov5675_update_pad_format(const struct ov5675_mode *mode,
				     struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	fmt->field = V4L2_FIELD_NONE;
}

static int ov5675_start_streaming(struct ov5675 *ov5675)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov5675->sd);
	const struct ov5675_reg_list *reg_list;
	int link_freq_index, ret;

	link_freq_index = ov5675->cur_mode->link_freq_index;
	reg_list = &link_freq_configs[link_freq_index].reg_list;
	ret = ov5675_write_reg_list(ov5675, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set plls");
		return ret;
	}

	reg_list = &ov5675->cur_mode->reg_list;
	ret = ov5675_write_reg_list(ov5675, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set mode");
		return ret;
	}

	ret = __v4l2_ctrl_handler_setup(ov5675->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = ov5675_write_reg(ov5675, OV5675_REG_MODE_SELECT,
			       OV5675_REG_VALUE_08BIT, OV5675_MODE_STREAMING);
	if (ret) {
		dev_err(&client->dev, "failed to set stream");
		return ret;
	}

	return 0;
}

static void ov5675_stop_streaming(struct ov5675 *ov5675)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov5675->sd);

	if (ov5675_write_reg(ov5675, OV5675_REG_MODE_SELECT,
			     OV5675_REG_VALUE_08BIT, OV5675_MODE_STANDBY))
		dev_err(&client->dev, "failed to set stream");
}

static int ov5675_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov5675 *ov5675 = to_ov5675(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (ov5675->streaming == enable)
		return 0;

	mutex_lock(&ov5675->mutex);
	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			mutex_unlock(&ov5675->mutex);
			return ret;
		}

		ret = ov5675_start_streaming(ov5675);
		if (ret) {
			enable = 0;
			ov5675_stop_streaming(ov5675);
			pm_runtime_put(&client->dev);
		}
	} else {
		ov5675_stop_streaming(ov5675);
		pm_runtime_put(&client->dev);
	}

	ov5675->streaming = enable;
	mutex_unlock(&ov5675->mutex);

	return ret;
}

static int __maybe_unused ov5675_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov5675 *ov5675 = to_ov5675(sd);

	mutex_lock(&ov5675->mutex);
	if (ov5675->streaming)
		ov5675_stop_streaming(ov5675);

	mutex_unlock(&ov5675->mutex);

	return 0;
}

static int __maybe_unused ov5675_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov5675 *ov5675 = to_ov5675(sd);
	int ret;

	mutex_lock(&ov5675->mutex);
	if (ov5675->streaming) {
		ret = ov5675_start_streaming(ov5675);
		if (ret) {
			ov5675->streaming = false;
			ov5675_stop_streaming(ov5675);
			mutex_unlock(&ov5675->mutex);
			return ret;
		}
	}

	mutex_unlock(&ov5675->mutex);

	return 0;
}

static int ov5675_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct ov5675 *ov5675 = to_ov5675(sd);
	const struct ov5675_mode *mode;
	s32 vblank_def, h_blank;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes), width,
				      height, fmt->format.width,
				      fmt->format.height);

	mutex_lock(&ov5675->mutex);
	ov5675_update_pad_format(mode, &fmt->format);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
	} else {
		ov5675->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(ov5675->link_freq, mode->link_freq_index);
		__v4l2_ctrl_s_ctrl_int64(ov5675->pixel_rate,
					 to_pixel_rate(mode->link_freq_index));

		/* Update limits and set FPS to default */
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov5675->vblank,
					 mode->vts_min - mode->height,
					 OV5675_VTS_MAX - mode->height, 1,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(ov5675->vblank, vblank_def);
		h_blank = to_pixels_per_line(mode->hts, mode->link_freq_index) -
			  mode->width;
		__v4l2_ctrl_modify_range(ov5675->hblank, h_blank, h_blank, 1,
					 h_blank);
	}

	mutex_unlock(&ov5675->mutex);

	return 0;
}

static int ov5675_get_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct ov5675 *ov5675 = to_ov5675(sd);

	mutex_lock(&ov5675->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt->format = *v4l2_subdev_get_try_format(&ov5675->sd, cfg,
							  fmt->pad);
	else
		ov5675_update_pad_format(ov5675->cur_mode, &fmt->format);

	mutex_unlock(&ov5675->mutex);

	return 0;
}

static int ov5675_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int ov5675_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
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

static int ov5675_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov5675 *ov5675 = to_ov5675(sd);

	mutex_lock(&ov5675->mutex);
	ov5675_update_pad_format(&supported_modes[0],
				 v4l2_subdev_get_try_format(sd, fh->pad, 0));
	mutex_unlock(&ov5675->mutex);

	return 0;
}

static const struct v4l2_subdev_video_ops ov5675_video_ops = {
	.s_stream = ov5675_set_stream,
};

static const struct v4l2_subdev_pad_ops ov5675_pad_ops = {
	.set_fmt = ov5675_set_format,
	.get_fmt = ov5675_get_format,
	.enum_mbus_code = ov5675_enum_mbus_code,
	.enum_frame_size = ov5675_enum_frame_size,
};

static const struct v4l2_subdev_ops ov5675_subdev_ops = {
	.video = &ov5675_video_ops,
	.pad = &ov5675_pad_ops,
};

static const struct media_entity_operations ov5675_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops ov5675_internal_ops = {
	.open = ov5675_open,
};

static int ov5675_identify_module(struct ov5675 *ov5675)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov5675->sd);
	int ret;
	u32 val;

	ret = ov5675_read_reg(ov5675, OV5675_REG_CHIP_ID,
			      OV5675_REG_VALUE_24BIT, &val);
	if (ret)
		return ret;

	if (val != OV5675_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x",
			OV5675_CHIP_ID, val);
		return -ENXIO;
	}

	return 0;
}

static int ov5675_check_hwcfg(struct device *dev)
{
	struct fwnode_handle *ep;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	u32 mclk;
	int ret;
	unsigned int i, j;

	if (!fwnode)
		return -ENXIO;

	ret = fwnode_property_read_u32(fwnode, "clock-frequency", &mclk);

	if (ret) {
		dev_err(dev, "can't get clock frequency");
		return ret;
	}

	if (mclk != OV5675_MCLK) {
		dev_err(dev, "external clock %d is not supported", mclk);
		return -EINVAL;
	}

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != OV5675_DATA_LANES) {
		dev_err(dev, "number of CSI2 data lanes %d is not supported",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto check_hwcfg_error;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(dev, "no link frequencies defined");
		ret = -EINVAL;
		goto check_hwcfg_error;
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
			goto check_hwcfg_error;
		}
	}

check_hwcfg_error:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int ov5675_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5675 *ov5675 = to_ov5675(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(&client->dev);
	mutex_destroy(&ov5675->mutex);

	return 0;
}

static int ov5675_probe(struct i2c_client *client)
{
	struct ov5675 *ov5675;
	int ret;

	ret = ov5675_check_hwcfg(&client->dev);
	if (ret) {
		dev_err(&client->dev, "failed to check HW configuration: %d",
			ret);
		return ret;
	}

	ov5675 = devm_kzalloc(&client->dev, sizeof(*ov5675), GFP_KERNEL);
	if (!ov5675)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&ov5675->sd, client, &ov5675_subdev_ops);
	ret = ov5675_identify_module(ov5675);
	if (ret) {
		dev_err(&client->dev, "failed to find sensor: %d", ret);
		return ret;
	}

	mutex_init(&ov5675->mutex);
	ov5675->cur_mode = &supported_modes[0];
	ret = ov5675_init_controls(ov5675);
	if (ret) {
		dev_err(&client->dev, "failed to init controls: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ov5675->sd.internal_ops = &ov5675_internal_ops;
	ov5675->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov5675->sd.entity.ops = &ov5675_subdev_entity_ops;
	ov5675->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ov5675->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&ov5675->sd.entity, 1, &ov5675->pad);
	if (ret) {
		dev_err(&client->dev, "failed to init entity pads: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor_common(&ov5675->sd);
	if (ret < 0) {
		dev_err(&client->dev, "failed to register V4L2 subdev: %d",
			ret);
		goto probe_error_media_entity_cleanup;
	}

	/*
	 * Device is already turned on by i2c-core with ACPI domain PM.
	 * Enable runtime PM and turn off the device.
	 */
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

probe_error_media_entity_cleanup:
	media_entity_cleanup(&ov5675->sd.entity);

probe_error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(ov5675->sd.ctrl_handler);
	mutex_destroy(&ov5675->mutex);

	return ret;
}

static const struct dev_pm_ops ov5675_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ov5675_suspend, ov5675_resume)
};

#ifdef CONFIG_ACPI
static const struct acpi_device_id ov5675_acpi_ids[] = {
	{"OVTI5675"},
	{}
};

MODULE_DEVICE_TABLE(acpi, ov5675_acpi_ids);
#endif

static struct i2c_driver ov5675_i2c_driver = {
	.driver = {
		.name = "ov5675",
		.pm = &ov5675_pm_ops,
		.acpi_match_table = ACPI_PTR(ov5675_acpi_ids),
	},
	.probe_new = ov5675_probe,
	.remove = ov5675_remove,
};

module_i2c_driver(ov5675_i2c_driver);

MODULE_AUTHOR("Shawn Tu <shawnx.tu@intel.com>");
MODULE_DESCRIPTION("OmniVision OV5675 sensor driver");
MODULE_LICENSE("GPL v2");
