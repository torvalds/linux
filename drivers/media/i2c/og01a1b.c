// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 Intel Corporation.

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/unaligned.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define OG01A1B_REG_VALUE_08BIT		1
#define OG01A1B_REG_VALUE_16BIT		2
#define OG01A1B_REG_VALUE_24BIT		3

#define OG01A1B_LINK_FREQ_500MHZ	500000000ULL
#define OG01A1B_SCLK			120000000LL
#define OG01A1B_MCLK			19200000
#define OG01A1B_DATA_LANES		2
#define OG01A1B_RGB_DEPTH		10

#define OG01A1B_REG_CHIP_ID		0x300a
#define OG01A1B_CHIP_ID			0x470141

#define OG01A1B_REG_MODE_SELECT		0x0100
#define OG01A1B_MODE_STANDBY		0x00
#define OG01A1B_MODE_STREAMING		0x01

/* vertical-timings from sensor */
#define OG01A1B_REG_VTS			0x380e
#define OG01A1B_VTS_120FPS		0x0498
#define OG01A1B_VTS_120FPS_MIN		0x0498
#define OG01A1B_VTS_MAX			0x7fff

/* horizontal-timings from sensor */
#define OG01A1B_REG_HTS			0x380c

/* Exposure controls from sensor */
#define OG01A1B_REG_EXPOSURE		0x3501
#define	OG01A1B_EXPOSURE_MIN		1
#define OG01A1B_EXPOSURE_MAX_MARGIN	14
#define	OG01A1B_EXPOSURE_STEP		1

/* Analog gain controls from sensor */
#define OG01A1B_REG_ANALOG_GAIN		0x3508
#define	OG01A1B_ANAL_GAIN_MIN		16
#define	OG01A1B_ANAL_GAIN_MAX		248 /* Max = 15.5x */
#define	OG01A1B_ANAL_GAIN_STEP		1

/* Digital gain controls from sensor */
#define OG01A1B_REG_DIG_GAIN		0x350a
#define OG01A1B_DGTL_GAIN_MIN		1024
#define OG01A1B_DGTL_GAIN_MAX		16384 /* Max = 16x */
#define OG01A1B_DGTL_GAIN_STEP		1
#define OG01A1B_DGTL_GAIN_DEFAULT	1024

/* Group Access */
#define OG01A1B_REG_GROUP_ACCESS	0x3208
#define OG01A1B_GROUP_HOLD_START	0x0
#define OG01A1B_GROUP_HOLD_END		0x10
#define OG01A1B_GROUP_HOLD_LAUNCH	0xa0

/* Test Pattern Control */
#define OG01A1B_REG_TEST_PATTERN	0x5100
#define OG01A1B_TEST_PATTERN_ENABLE	BIT(7)
#define OG01A1B_TEST_PATTERN_BAR_SHIFT	2

#define to_og01a1b(_sd)			container_of(_sd, struct og01a1b, sd)

enum {
	OG01A1B_LINK_FREQ_1000MBPS,
};

struct og01a1b_reg {
	u16 address;
	u8 val;
};

struct og01a1b_reg_list {
	u32 num_of_regs;
	const struct og01a1b_reg *regs;
};

struct og01a1b_link_freq_config {
	const struct og01a1b_reg_list reg_list;
};

struct og01a1b_mode {
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
	const struct og01a1b_reg_list reg_list;
};

static const struct og01a1b_reg mipi_data_rate_1000mbps[] = {
	{0x0103, 0x01},
	{0x0303, 0x02},
	{0x0304, 0x00},
	{0x0305, 0xd2},
	{0x0323, 0x02},
	{0x0324, 0x01},
	{0x0325, 0x77},
};

static const struct og01a1b_reg mode_1280x1024_regs[] = {
	{0x0300, 0x0a},
	{0x0301, 0x29},
	{0x0302, 0x31},
	{0x0303, 0x02},
	{0x0304, 0x00},
	{0x0305, 0xd2},
	{0x0306, 0x00},
	{0x0307, 0x01},
	{0x0308, 0x02},
	{0x0309, 0x00},
	{0x0310, 0x00},
	{0x0311, 0x00},
	{0x0312, 0x07},
	{0x0313, 0x00},
	{0x0314, 0x00},
	{0x0315, 0x00},
	{0x0320, 0x02},
	{0x0321, 0x01},
	{0x0322, 0x01},
	{0x0323, 0x02},
	{0x0324, 0x01},
	{0x0325, 0x77},
	{0x0326, 0xce},
	{0x0327, 0x04},
	{0x0329, 0x02},
	{0x032a, 0x04},
	{0x032b, 0x04},
	{0x032c, 0x02},
	{0x032d, 0x01},
	{0x032e, 0x00},
	{0x300d, 0x02},
	{0x300e, 0x04},
	{0x3021, 0x08},
	{0x301e, 0x03},
	{0x3103, 0x00},
	{0x3106, 0x08},
	{0x3107, 0x40},
	{0x3216, 0x01},
	{0x3217, 0x00},
	{0x3218, 0xc0},
	{0x3219, 0x55},
	{0x3500, 0x00},
	{0x3501, 0x04},
	{0x3502, 0x8a},
	{0x3506, 0x01},
	{0x3507, 0x72},
	{0x3508, 0x01},
	{0x3509, 0x00},
	{0x350a, 0x01},
	{0x350b, 0x00},
	{0x350c, 0x00},
	{0x3541, 0x00},
	{0x3542, 0x40},
	{0x3605, 0xe0},
	{0x3606, 0x41},
	{0x3614, 0x20},
	{0x3620, 0x0b},
	{0x3630, 0x07},
	{0x3636, 0xa0},
	{0x3637, 0xf9},
	{0x3638, 0x09},
	{0x3639, 0x38},
	{0x363f, 0x09},
	{0x3640, 0x17},
	{0x3662, 0x04},
	{0x3665, 0x80},
	{0x3670, 0x68},
	{0x3674, 0x00},
	{0x3677, 0x3f},
	{0x3679, 0x00},
	{0x369f, 0x19},
	{0x36a0, 0x03},
	{0x36a2, 0x19},
	{0x36a3, 0x03},
	{0x370d, 0x66},
	{0x370f, 0x00},
	{0x3710, 0x03},
	{0x3715, 0x03},
	{0x3716, 0x03},
	{0x3717, 0x06},
	{0x3733, 0x00},
	{0x3778, 0x00},
	{0x37a8, 0x0f},
	{0x37a9, 0x01},
	{0x37aa, 0x07},
	{0x37bd, 0x1c},
	{0x37c1, 0x2f},
	{0x37c3, 0x09},
	{0x37c8, 0x1d},
	{0x37ca, 0x30},
	{0x37df, 0x00},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x05},
	{0x3805, 0x0f},
	{0x3806, 0x04},
	{0x3807, 0x0f},
	{0x3808, 0x05},
	{0x3809, 0x00},
	{0x380a, 0x04},
	{0x380b, 0x00},
	{0x380c, 0x03},
	{0x380d, 0x50},
	{0x380e, 0x04},
	{0x380f, 0x98},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x08},
	{0x3814, 0x11},
	{0x3815, 0x11},
	{0x3820, 0x40},
	{0x3821, 0x04},
	{0x3826, 0x00},
	{0x3827, 0x00},
	{0x382a, 0x08},
	{0x382b, 0x52},
	{0x382d, 0xba},
	{0x383d, 0x14},
	{0x384a, 0xa2},
	{0x3866, 0x0e},
	{0x3867, 0x07},
	{0x3884, 0x00},
	{0x3885, 0x08},
	{0x3893, 0x68},
	{0x3894, 0x2a},
	{0x3898, 0x00},
	{0x3899, 0x31},
	{0x389a, 0x04},
	{0x389b, 0x00},
	{0x389c, 0x0b},
	{0x389d, 0xad},
	{0x389f, 0x08},
	{0x38a0, 0x00},
	{0x38a1, 0x00},
	{0x38a8, 0x70},
	{0x38ac, 0xea},
	{0x38b2, 0x00},
	{0x38b3, 0x08},
	{0x38bc, 0x20},
	{0x38c4, 0x0c},
	{0x38c5, 0x3a},
	{0x38c7, 0x3a},
	{0x38e1, 0xc0},
	{0x38ec, 0x3c},
	{0x38f0, 0x09},
	{0x38f1, 0x6f},
	{0x38fe, 0x3c},
	{0x391e, 0x00},
	{0x391f, 0x00},
	{0x3920, 0xa5},
	{0x3921, 0x00},
	{0x3922, 0x00},
	{0x3923, 0x00},
	{0x3924, 0x05},
	{0x3925, 0x00},
	{0x3926, 0x00},
	{0x3927, 0x00},
	{0x3928, 0x1a},
	{0x3929, 0x01},
	{0x392a, 0xb4},
	{0x392b, 0x00},
	{0x392c, 0x10},
	{0x392f, 0x40},
	{0x4000, 0xcf},
	{0x4003, 0x40},
	{0x4008, 0x00},
	{0x4009, 0x07},
	{0x400a, 0x02},
	{0x400b, 0x54},
	{0x400c, 0x00},
	{0x400d, 0x07},
	{0x4010, 0xc0},
	{0x4012, 0x02},
	{0x4014, 0x04},
	{0x4015, 0x04},
	{0x4017, 0x02},
	{0x4042, 0x01},
	{0x4306, 0x04},
	{0x4307, 0x12},
	{0x4509, 0x00},
	{0x450b, 0x83},
	{0x4604, 0x68},
	{0x4608, 0x0a},
	{0x4700, 0x06},
	{0x4800, 0x64},
	{0x481b, 0x3c},
	{0x4825, 0x32},
	{0x4833, 0x18},
	{0x4837, 0x0f},
	{0x4850, 0x40},
	{0x4860, 0x00},
	{0x4861, 0xec},
	{0x4864, 0x00},
	{0x4883, 0x00},
	{0x4888, 0x90},
	{0x4889, 0x05},
	{0x488b, 0x04},
	{0x4f00, 0x04},
	{0x4f10, 0x04},
	{0x4f21, 0x01},
	{0x4f22, 0x40},
	{0x4f23, 0x44},
	{0x4f24, 0x51},
	{0x4f25, 0x41},
	{0x5000, 0x1f},
	{0x500a, 0x00},
	{0x5100, 0x00},
	{0x5111, 0x20},
	{0x3020, 0x20},
	{0x3613, 0x03},
	{0x38c9, 0x02},
	{0x5304, 0x01},
	{0x3620, 0x08},
	{0x3639, 0x58},
	{0x363a, 0x10},
	{0x3674, 0x04},
	{0x3780, 0xff},
	{0x3781, 0xff},
	{0x3782, 0x00},
	{0x3783, 0x01},
	{0x3798, 0xa3},
	{0x37aa, 0x10},
	{0x38a8, 0xf0},
	{0x38c4, 0x09},
	{0x38c5, 0xb0},
	{0x38df, 0x80},
	{0x38ff, 0x05},
	{0x4010, 0xf1},
	{0x4011, 0x70},
	{0x3667, 0x80},
	{0x4d00, 0x4a},
	{0x4d01, 0x18},
	{0x4d02, 0xbb},
	{0x4d03, 0xde},
	{0x4d04, 0x93},
	{0x4d05, 0xff},
	{0x4d09, 0x0a},
	{0x37aa, 0x16},
	{0x3606, 0x42},
	{0x3605, 0x00},
	{0x36a2, 0x17},
	{0x300d, 0x0a},
	{0x4d00, 0x4d},
	{0x4d01, 0x95},
	{0x3d8C, 0x70},
	{0x3d8d, 0xE9},
	{0x5300, 0x00},
	{0x5301, 0x10},
	{0x5302, 0x00},
	{0x5303, 0xE3},
	{0x3d88, 0x00},
	{0x3d89, 0x10},
	{0x3d8a, 0x00},
	{0x3d8b, 0xE3},
	{0x4f22, 0x00},
};

static const char * const og01a1b_test_pattern_menu[] = {
	"Disabled",
	"Standard Color Bar",
	"Top-Bottom Darker Color Bar",
	"Right-Left Darker Color Bar",
	"Bottom-Top Darker Color Bar"
};

static const s64 link_freq_menu_items[] = {
	OG01A1B_LINK_FREQ_500MHZ,
};

static const struct og01a1b_link_freq_config link_freq_configs[] = {
	[OG01A1B_LINK_FREQ_1000MBPS] = {
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_1000mbps),
			.regs = mipi_data_rate_1000mbps,
		}
	}
};

static const struct og01a1b_mode supported_modes[] = {
	{
		.width = 1280,
		.height = 1024,
		.hts = 848,
		.vts_def = OG01A1B_VTS_120FPS,
		.vts_min = OG01A1B_VTS_120FPS_MIN,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1280x1024_regs),
			.regs = mode_1280x1024_regs,
		},
		.link_freq_index = OG01A1B_LINK_FREQ_1000MBPS,
	},
};

struct og01a1b {
	struct device *dev;
	struct clk *xvclk;
	struct gpio_desc *reset_gpio;
	struct regulator *avdd;
	struct regulator *dovdd;
	struct regulator *dvdd;

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
	const struct og01a1b_mode *cur_mode;

	/* To serialize asynchronus callbacks */
	struct mutex mutex;
};

static u64 to_pixel_rate(u32 f_index)
{
	u64 pixel_rate = link_freq_menu_items[f_index] * 2 * OG01A1B_DATA_LANES;

	do_div(pixel_rate, OG01A1B_RGB_DEPTH);

	return pixel_rate;
}

static u64 to_pixels_per_line(u32 hts, u32 f_index)
{
	u64 ppl = hts * to_pixel_rate(f_index);

	do_div(ppl, OG01A1B_SCLK);

	return ppl;
}

static int og01a1b_read_reg(struct og01a1b *og01a1b, u16 reg, u16 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&og01a1b->sd);
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

static int og01a1b_write_reg(struct og01a1b *og01a1b, u16 reg, u16 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&og01a1b->sd);
	u8 buf[6];

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << 8 * (4 - len), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int og01a1b_write_reg_list(struct og01a1b *og01a1b,
				  const struct og01a1b_reg_list *r_list)
{
	unsigned int i;
	int ret;

	for (i = 0; i < r_list->num_of_regs; i++) {
		ret = og01a1b_write_reg(og01a1b, r_list->regs[i].address, 1,
					r_list->regs[i].val);
		if (ret) {
			dev_err_ratelimited(og01a1b->dev,
					    "failed to write reg 0x%4.4x. error = %d",
					    r_list->regs[i].address, ret);
			return ret;
		}
	}

	return 0;
}

static int og01a1b_test_pattern(struct og01a1b *og01a1b, u32 pattern)
{
	if (pattern)
		pattern = (pattern - 1) << OG01A1B_TEST_PATTERN_BAR_SHIFT |
			  OG01A1B_TEST_PATTERN_ENABLE;

	return og01a1b_write_reg(og01a1b, OG01A1B_REG_TEST_PATTERN,
				 OG01A1B_REG_VALUE_08BIT, pattern);
}

static int og01a1b_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct og01a1b *og01a1b = container_of(ctrl->handler,
					       struct og01a1b, ctrl_handler);
	s64 exposure_max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Update max exposure while meeting expected vblanking */
		exposure_max = og01a1b->cur_mode->height + ctrl->val -
			       OG01A1B_EXPOSURE_MAX_MARGIN;
		__v4l2_ctrl_modify_range(og01a1b->exposure,
					 og01a1b->exposure->minimum,
					 exposure_max, og01a1b->exposure->step,
					 exposure_max);
	}

	/* V4L2 controls values will be applied only when power is already up */
	if (!pm_runtime_get_if_in_use(og01a1b->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = og01a1b_write_reg(og01a1b, OG01A1B_REG_ANALOG_GAIN,
					OG01A1B_REG_VALUE_16BIT,
					ctrl->val << 4);
		break;

	case V4L2_CID_DIGITAL_GAIN:
		ret = og01a1b_write_reg(og01a1b, OG01A1B_REG_DIG_GAIN,
					OG01A1B_REG_VALUE_24BIT,
					ctrl->val << 6);
		break;

	case V4L2_CID_EXPOSURE:
		ret = og01a1b_write_reg(og01a1b, OG01A1B_REG_EXPOSURE,
					OG01A1B_REG_VALUE_16BIT, ctrl->val);
		break;

	case V4L2_CID_VBLANK:
		ret = og01a1b_write_reg(og01a1b, OG01A1B_REG_VTS,
					OG01A1B_REG_VALUE_16BIT,
					og01a1b->cur_mode->height + ctrl->val);
		break;

	case V4L2_CID_TEST_PATTERN:
		ret = og01a1b_test_pattern(og01a1b, ctrl->val);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(og01a1b->dev);

	return ret;
}

static const struct v4l2_ctrl_ops og01a1b_ctrl_ops = {
	.s_ctrl = og01a1b_set_ctrl,
};

static int og01a1b_init_controls(struct og01a1b *og01a1b)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 exposure_max, h_blank;
	int ret;

	ctrl_hdlr = &og01a1b->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	ctrl_hdlr->lock = &og01a1b->mutex;
	og01a1b->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr,
						    &og01a1b_ctrl_ops,
						    V4L2_CID_LINK_FREQ,
						    ARRAY_SIZE
						    (link_freq_menu_items) - 1,
						    0, link_freq_menu_items);
	if (og01a1b->link_freq)
		og01a1b->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	og01a1b->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &og01a1b_ctrl_ops,
						V4L2_CID_PIXEL_RATE, 0,
						to_pixel_rate
						(OG01A1B_LINK_FREQ_1000MBPS),
						1,
						to_pixel_rate
						(OG01A1B_LINK_FREQ_1000MBPS));
	og01a1b->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &og01a1b_ctrl_ops,
					    V4L2_CID_VBLANK,
					    og01a1b->cur_mode->vts_min -
					    og01a1b->cur_mode->height,
					    OG01A1B_VTS_MAX -
					    og01a1b->cur_mode->height, 1,
					    og01a1b->cur_mode->vts_def -
					    og01a1b->cur_mode->height);
	h_blank = to_pixels_per_line(og01a1b->cur_mode->hts,
				     og01a1b->cur_mode->link_freq_index) -
				     og01a1b->cur_mode->width;
	og01a1b->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &og01a1b_ctrl_ops,
					    V4L2_CID_HBLANK, h_blank, h_blank,
					    1, h_blank);
	if (og01a1b->hblank)
		og01a1b->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &og01a1b_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OG01A1B_ANAL_GAIN_MIN, OG01A1B_ANAL_GAIN_MAX,
			  OG01A1B_ANAL_GAIN_STEP, OG01A1B_ANAL_GAIN_MIN);
	v4l2_ctrl_new_std(ctrl_hdlr, &og01a1b_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OG01A1B_DGTL_GAIN_MIN, OG01A1B_DGTL_GAIN_MAX,
			  OG01A1B_DGTL_GAIN_STEP, OG01A1B_DGTL_GAIN_DEFAULT);
	exposure_max = (og01a1b->cur_mode->vts_def -
			OG01A1B_EXPOSURE_MAX_MARGIN);
	og01a1b->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &og01a1b_ctrl_ops,
					      V4L2_CID_EXPOSURE,
					      OG01A1B_EXPOSURE_MIN,
					      exposure_max,
					      OG01A1B_EXPOSURE_STEP,
					      exposure_max);
	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &og01a1b_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(og01a1b_test_pattern_menu) - 1,
				     0, 0, og01a1b_test_pattern_menu);

	if (ctrl_hdlr->error)
		return ctrl_hdlr->error;

	og01a1b->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

static void og01a1b_update_pad_format(const struct og01a1b_mode *mode,
				      struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = MEDIA_BUS_FMT_Y10_1X10;
	fmt->field = V4L2_FIELD_NONE;
}

static int og01a1b_start_streaming(struct og01a1b *og01a1b)
{
	const struct og01a1b_reg_list *reg_list;
	int link_freq_index, ret;

	link_freq_index = og01a1b->cur_mode->link_freq_index;
	reg_list = &link_freq_configs[link_freq_index].reg_list;

	ret = og01a1b_write_reg_list(og01a1b, reg_list);
	if (ret) {
		dev_err(og01a1b->dev, "failed to set plls");
		return ret;
	}

	reg_list = &og01a1b->cur_mode->reg_list;
	ret = og01a1b_write_reg_list(og01a1b, reg_list);
	if (ret) {
		dev_err(og01a1b->dev, "failed to set mode");
		return ret;
	}

	ret = __v4l2_ctrl_handler_setup(og01a1b->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = og01a1b_write_reg(og01a1b, OG01A1B_REG_MODE_SELECT,
				OG01A1B_REG_VALUE_08BIT,
				OG01A1B_MODE_STREAMING);
	if (ret) {
		dev_err(og01a1b->dev, "failed to set stream");
		return ret;
	}

	return 0;
}

static void og01a1b_stop_streaming(struct og01a1b *og01a1b)
{
	if (og01a1b_write_reg(og01a1b, OG01A1B_REG_MODE_SELECT,
			      OG01A1B_REG_VALUE_08BIT, OG01A1B_MODE_STANDBY))
		dev_err(og01a1b->dev, "failed to set stream");
}

static int og01a1b_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct og01a1b *og01a1b = to_og01a1b(sd);
	int ret = 0;

	mutex_lock(&og01a1b->mutex);
	if (enable) {
		ret = pm_runtime_resume_and_get(og01a1b->dev);
		if (ret) {
			mutex_unlock(&og01a1b->mutex);
			return ret;
		}

		ret = og01a1b_start_streaming(og01a1b);
		if (ret) {
			enable = 0;
			og01a1b_stop_streaming(og01a1b);
			pm_runtime_put(og01a1b->dev);
		}
	} else {
		og01a1b_stop_streaming(og01a1b);
		pm_runtime_put(og01a1b->dev);
	}

	mutex_unlock(&og01a1b->mutex);

	return ret;
}

static int og01a1b_set_format(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *fmt)
{
	struct og01a1b *og01a1b = to_og01a1b(sd);
	const struct og01a1b_mode *mode;
	s32 vblank_def, h_blank;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes), width,
				      height, fmt->format.width,
				      fmt->format.height);

	mutex_lock(&og01a1b->mutex);
	og01a1b_update_pad_format(mode, &fmt->format);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_state_get_format(sd_state, fmt->pad) = fmt->format;
	} else {
		og01a1b->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(og01a1b->link_freq, mode->link_freq_index);
		__v4l2_ctrl_s_ctrl_int64(og01a1b->pixel_rate,
					 to_pixel_rate(mode->link_freq_index));

		/* Update limits and set FPS to default */
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(og01a1b->vblank,
					 mode->vts_min - mode->height,
					 OG01A1B_VTS_MAX - mode->height, 1,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(og01a1b->vblank, vblank_def);
		h_blank = to_pixels_per_line(mode->hts, mode->link_freq_index) -
			  mode->width;
		__v4l2_ctrl_modify_range(og01a1b->hblank, h_blank, h_blank, 1,
					 h_blank);
	}

	mutex_unlock(&og01a1b->mutex);

	return 0;
}

static int og01a1b_get_format(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *fmt)
{
	struct og01a1b *og01a1b = to_og01a1b(sd);

	mutex_lock(&og01a1b->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt->format = *v4l2_subdev_state_get_format(sd_state,
							    fmt->pad);
	else
		og01a1b_update_pad_format(og01a1b->cur_mode, &fmt->format);

	mutex_unlock(&og01a1b->mutex);

	return 0;
}

static int og01a1b_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_Y10_1X10;

	return 0;
}

static int og01a1b_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_Y10_1X10)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int og01a1b_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct og01a1b *og01a1b = to_og01a1b(sd);

	mutex_lock(&og01a1b->mutex);
	og01a1b_update_pad_format(&supported_modes[0],
				  v4l2_subdev_state_get_format(fh->state, 0));
	mutex_unlock(&og01a1b->mutex);

	return 0;
}

static const struct v4l2_subdev_video_ops og01a1b_video_ops = {
	.s_stream = og01a1b_set_stream,
};

static const struct v4l2_subdev_pad_ops og01a1b_pad_ops = {
	.set_fmt = og01a1b_set_format,
	.get_fmt = og01a1b_get_format,
	.enum_mbus_code = og01a1b_enum_mbus_code,
	.enum_frame_size = og01a1b_enum_frame_size,
};

static const struct v4l2_subdev_ops og01a1b_subdev_ops = {
	.video = &og01a1b_video_ops,
	.pad = &og01a1b_pad_ops,
};

static const struct media_entity_operations og01a1b_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops og01a1b_internal_ops = {
	.open = og01a1b_open,
};

static int og01a1b_identify_module(struct og01a1b *og01a1b)
{
	int ret;
	u32 val;

	ret = og01a1b_read_reg(og01a1b, OG01A1B_REG_CHIP_ID,
			       OG01A1B_REG_VALUE_24BIT, &val);
	if (ret)
		return ret;

	if (val != OG01A1B_CHIP_ID) {
		dev_err(og01a1b->dev, "chip id mismatch: %x!=%x",
			OG01A1B_CHIP_ID, val);
		return -ENXIO;
	}

	return 0;
}

static int og01a1b_check_hwcfg(struct og01a1b *og01a1b)
{
	struct device *dev = og01a1b->dev;
	struct fwnode_handle *ep;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int ret;
	unsigned int i, j;

	if (!fwnode)
		return -ENXIO;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != OG01A1B_DATA_LANES) {
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

/* Power/clock management functions */
static int og01a1b_power_on(struct device *dev)
{
	unsigned long delay = DIV_ROUND_UP(8192UL * USEC_PER_SEC, OG01A1B_MCLK);
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct og01a1b *og01a1b = to_og01a1b(sd);
	int ret;

	if (og01a1b->avdd) {
		ret = regulator_enable(og01a1b->avdd);
		if (ret)
			return ret;
	}

	if (og01a1b->dovdd) {
		ret = regulator_enable(og01a1b->dovdd);
		if (ret)
			goto avdd_disable;
	}

	if (og01a1b->dvdd) {
		ret = regulator_enable(og01a1b->dvdd);
		if (ret)
			goto dovdd_disable;
	}

	ret = clk_prepare_enable(og01a1b->xvclk);
	if (ret)
		goto dvdd_disable;

	gpiod_set_value_cansleep(og01a1b->reset_gpio, 0);

	if (og01a1b->reset_gpio)
		usleep_range(5 * USEC_PER_MSEC, 6 * USEC_PER_MSEC);
	else if (og01a1b->xvclk)
		usleep_range(delay, 2 * delay);

	return 0;

dvdd_disable:
	if (og01a1b->dvdd)
		regulator_disable(og01a1b->dvdd);
dovdd_disable:
	if (og01a1b->dovdd)
		regulator_disable(og01a1b->dovdd);
avdd_disable:
	if (og01a1b->avdd)
		regulator_disable(og01a1b->avdd);

	return ret;
}

static int og01a1b_power_off(struct device *dev)
{
	unsigned long delay = DIV_ROUND_UP(512 * USEC_PER_SEC, OG01A1B_MCLK);
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct og01a1b *og01a1b = to_og01a1b(sd);

	if (og01a1b->xvclk)
		usleep_range(delay, 2 * delay);

	clk_disable_unprepare(og01a1b->xvclk);

	gpiod_set_value_cansleep(og01a1b->reset_gpio, 1);

	if (og01a1b->dvdd)
		regulator_disable(og01a1b->dvdd);

	if (og01a1b->dovdd)
		regulator_disable(og01a1b->dovdd);

	if (og01a1b->avdd)
		regulator_disable(og01a1b->avdd);

	return 0;
}

static void og01a1b_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct og01a1b *og01a1b = to_og01a1b(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(og01a1b->dev);
	mutex_destroy(&og01a1b->mutex);
}

static int og01a1b_probe(struct i2c_client *client)
{
	struct og01a1b *og01a1b;
	unsigned long freq;
	int ret;

	og01a1b = devm_kzalloc(&client->dev, sizeof(*og01a1b), GFP_KERNEL);
	if (!og01a1b)
		return -ENOMEM;

	og01a1b->dev = &client->dev;

	v4l2_i2c_subdev_init(&og01a1b->sd, client, &og01a1b_subdev_ops);

	og01a1b->xvclk = devm_v4l2_sensor_clk_get(og01a1b->dev, NULL);
	if (IS_ERR(og01a1b->xvclk))
		return dev_err_probe(og01a1b->dev, PTR_ERR(og01a1b->xvclk),
				     "failed to get xvclk clock\n");

	freq = clk_get_rate(og01a1b->xvclk);
	if (freq != OG01A1B_MCLK)
		return dev_err_probe(og01a1b->dev, -EINVAL,
				     "external clock %lu is not supported",
				     freq);

	ret = og01a1b_check_hwcfg(og01a1b);
	if (ret) {
		dev_err(og01a1b->dev, "failed to check HW configuration: %d",
			ret);
		return ret;
	}

	og01a1b->reset_gpio = devm_gpiod_get_optional(og01a1b->dev, "reset",
						      GPIOD_OUT_LOW);
	if (IS_ERR(og01a1b->reset_gpio)) {
		dev_err(og01a1b->dev, "cannot get reset GPIO\n");
		return PTR_ERR(og01a1b->reset_gpio);
	}

	og01a1b->avdd = devm_regulator_get_optional(og01a1b->dev, "avdd");
	if (IS_ERR(og01a1b->avdd)) {
		ret = PTR_ERR(og01a1b->avdd);
		if (ret != -ENODEV) {
			dev_err_probe(og01a1b->dev, ret,
				      "Failed to get 'avdd' regulator\n");
			return ret;
		}

		og01a1b->avdd = NULL;
	}

	og01a1b->dovdd = devm_regulator_get_optional(og01a1b->dev, "dovdd");
	if (IS_ERR(og01a1b->dovdd)) {
		ret = PTR_ERR(og01a1b->dovdd);
		if (ret != -ENODEV) {
			dev_err_probe(og01a1b->dev, ret,
				      "Failed to get 'dovdd' regulator\n");
			return ret;
		}

		og01a1b->dovdd = NULL;
	}

	og01a1b->dvdd = devm_regulator_get_optional(og01a1b->dev, "dvdd");
	if (IS_ERR(og01a1b->dvdd)) {
		ret = PTR_ERR(og01a1b->dvdd);
		if (ret != -ENODEV) {
			dev_err_probe(og01a1b->dev, ret,
				      "Failed to get 'dvdd' regulator\n");
			return ret;
		}

		og01a1b->dvdd = NULL;
	}

	/* The sensor must be powered on to read the CHIP_ID register */
	ret = og01a1b_power_on(og01a1b->dev);
	if (ret)
		return ret;

	ret = og01a1b_identify_module(og01a1b);
	if (ret) {
		dev_err(og01a1b->dev, "failed to find sensor: %d", ret);
		goto power_off;
	}

	mutex_init(&og01a1b->mutex);
	og01a1b->cur_mode = &supported_modes[0];
	ret = og01a1b_init_controls(og01a1b);
	if (ret) {
		dev_err(og01a1b->dev, "failed to init controls: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	og01a1b->sd.internal_ops = &og01a1b_internal_ops;
	og01a1b->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	og01a1b->sd.entity.ops = &og01a1b_subdev_entity_ops;
	og01a1b->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	og01a1b->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&og01a1b->sd.entity, 1, &og01a1b->pad);
	if (ret) {
		dev_err(og01a1b->dev, "failed to init entity pads: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&og01a1b->sd);
	if (ret < 0) {
		dev_err(og01a1b->dev, "failed to register V4L2 subdev: %d",
			ret);
		goto probe_error_media_entity_cleanup;
	}

	/* Enable runtime PM and turn off the device */
	pm_runtime_set_active(og01a1b->dev);
	pm_runtime_enable(og01a1b->dev);
	pm_runtime_idle(og01a1b->dev);

	return 0;

probe_error_media_entity_cleanup:
	media_entity_cleanup(&og01a1b->sd.entity);

probe_error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(og01a1b->sd.ctrl_handler);
	mutex_destroy(&og01a1b->mutex);

power_off:
	og01a1b_power_off(og01a1b->dev);

	return ret;
}

static const struct dev_pm_ops og01a1b_pm_ops = {
	SET_RUNTIME_PM_OPS(og01a1b_power_off, og01a1b_power_on, NULL)
};

#ifdef CONFIG_ACPI
static const struct acpi_device_id og01a1b_acpi_ids[] = {
	{"OVTI01AC"},
	{}
};

MODULE_DEVICE_TABLE(acpi, og01a1b_acpi_ids);
#endif

static const struct of_device_id og01a1b_of_match[] = {
	{ .compatible = "ovti,og01a1b" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, og01a1b_of_match);

static struct i2c_driver og01a1b_i2c_driver = {
	.driver = {
		.name = "og01a1b",
		.pm = &og01a1b_pm_ops,
		.acpi_match_table = ACPI_PTR(og01a1b_acpi_ids),
		.of_match_table = og01a1b_of_match,
	},
	.probe = og01a1b_probe,
	.remove = og01a1b_remove,
};

module_i2c_driver(og01a1b_i2c_driver);

MODULE_AUTHOR("Shawn Tu");
MODULE_DESCRIPTION("OmniVision OG01A1B sensor driver");
MODULE_LICENSE("GPL v2");
