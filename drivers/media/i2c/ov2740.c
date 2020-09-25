// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Intel Corporation.

#include <asm/unaligned.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/nvmem-provider.h>
#include <linux/regmap.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define OV2740_LINK_FREQ_360MHZ		360000000ULL
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
#define OV2740_VTS_DEF			0x088a
#define OV2740_VTS_MIN			0x0460
#define OV2740_VTS_MAX			0x7fff

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
#define OV2740_DGTL_GAIN_MIN		0
#define OV2740_DGTL_GAIN_MAX		4095
#define OV2740_DGTL_GAIN_STEP		1
#define OV2740_DGTL_GAIN_DEFAULT	1024

/* Test Pattern Control */
#define OV2740_REG_TEST_PATTERN		0x5040
#define OV2740_TEST_PATTERN_ENABLE	BIT(7)
#define OV2740_TEST_PATTERN_BAR_SHIFT	2

/* ISP CTRL00 */
#define OV2740_REG_ISP_CTRL00		0x5000
/* ISP CTRL01 */
#define OV2740_REG_ISP_CTRL01		0x5001
/* Customer Addresses: 0x7010 - 0x710F */
#define CUSTOMER_USE_OTP_SIZE		0x100
/* OTP registers from sensor */
#define OV2740_REG_OTP_CUSTOMER		0x7010

struct nvm_data {
	char *nvm_buffer;
	struct nvmem_device *nvmem;
	struct regmap *regmap;
};

enum {
	OV2740_LINK_FREQ_360MHZ_INDEX,
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

	/* Link frequency needed for this resolution */
	u32 link_freq_index;

	/* Sensor register settings for this resolution */
	const struct ov2740_reg_list reg_list;
};

static const struct ov2740_reg mipi_data_rate_720mbps[] = {
	{0x0103, 0x01},
	{0x0302, 0x4b},
	{0x030d, 0x4b},
	{0x030e, 0x02},
	{0x030a, 0x01},
	{0x0312, 0x11},
};

static const struct ov2740_reg mode_1932x1092_regs[] = {
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

static const char * const ov2740_test_pattern_menu[] = {
	"Disabled",
	"Color Bar",
	"Top-Bottom Darker Color Bar",
	"Right-Left Darker Color Bar",
	"Bottom-Top Darker Color Bar",
};

static const s64 link_freq_menu_items[] = {
	OV2740_LINK_FREQ_360MHZ,
};

static const struct ov2740_link_freq_config link_freq_configs[] = {
	[OV2740_LINK_FREQ_360MHZ_INDEX] = {
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_720mbps),
			.regs = mipi_data_rate_720mbps,
		}
	},
};

static const struct ov2740_mode supported_modes[] = {
	{
		.width = 1932,
		.height = 1092,
		.hts = 1080,
		.vts_def = OV2740_VTS_DEF,
		.vts_min = OV2740_VTS_MIN,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1932x1092_regs),
			.regs = mode_1932x1092_regs,
		},
		.link_freq_index = OV2740_LINK_FREQ_360MHZ_INDEX,
	},
};

struct ov2740 {
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
	const struct ov2740_mode *cur_mode;

	/* To serialize asynchronus callbacks */
	struct mutex mutex;

	/* Streaming on/off */
	bool streaming;
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

static u64 to_pixels_per_line(u32 hts, u32 f_index)
{
	u64 ppl = hts * to_pixel_rate(f_index);

	do_div(ppl, OV2740_SCLK);

	return ppl;
}

static int ov2740_read_reg(struct ov2740 *ov2740, u16 reg, u16 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov2740->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2];
	u8 data_buf[4] = {0};
	int ret = 0;

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
	int ret = 0;

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
	struct i2c_client *client = v4l2_get_subdevdata(&ov2740->sd);
	unsigned int i;
	int ret = 0;

	for (i = 0; i < r_list->num_of_regs; i++) {
		ret = ov2740_write_reg(ov2740, r_list->regs[i].address, 1,
				       r_list->regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "write reg 0x%4.4x return err = %d",
					    r_list->regs[i].address, ret);
			return ret;
		}
	}

	return 0;
}

static int ov2740_update_digital_gain(struct ov2740 *ov2740, u32 d_gain)
{
	int ret = 0;

	ret = ov2740_write_reg(ov2740, OV2740_REG_MWB_R_GAIN, 2, d_gain);
	if (ret)
		return ret;

	ret = ov2740_write_reg(ov2740, OV2740_REG_MWB_G_GAIN, 2, d_gain);
	if (ret)
		return ret;

	return ov2740_write_reg(ov2740, OV2740_REG_MWB_B_GAIN, 2, d_gain);
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
	struct i2c_client *client = v4l2_get_subdevdata(&ov2740->sd);
	s64 exposure_max;
	int ret = 0;

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
	if (!pm_runtime_get_if_in_use(&client->dev))
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

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov2740_ctrl_ops = {
	.s_ctrl = ov2740_set_ctrl,
};

static int ov2740_init_controls(struct ov2740 *ov2740)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	const struct ov2740_mode *cur_mode;
	s64 exposure_max, h_blank, pixel_rate;
	u32 vblank_min, vblank_max, vblank_default;
	int size;
	int ret = 0;

	ctrl_hdlr = &ov2740->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	ctrl_hdlr->lock = &ov2740->mutex;
	cur_mode = ov2740->cur_mode;
	size = ARRAY_SIZE(link_freq_menu_items);

	ov2740->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &ov2740_ctrl_ops,
						   V4L2_CID_LINK_FREQ,
						   size - 1, 0,
						   link_freq_menu_items);
	if (ov2740->link_freq)
		ov2740->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate = to_pixel_rate(OV2740_LINK_FREQ_360MHZ_INDEX);
	ov2740->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ov2740_ctrl_ops,
					       V4L2_CID_PIXEL_RATE, 0,
					       pixel_rate, 1, pixel_rate);

	vblank_min = cur_mode->vts_min - cur_mode->height;
	vblank_max = OV2740_VTS_MAX - cur_mode->height;
	vblank_default = cur_mode->vts_def - cur_mode->height;
	ov2740->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov2740_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_min,
					   vblank_max, 1, vblank_default);

	h_blank = to_pixels_per_line(cur_mode->hts, cur_mode->link_freq_index);
	h_blank -= cur_mode->width;
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
	exposure_max = cur_mode->vts_def - OV2740_EXPOSURE_MAX_MARGIN;
	ov2740->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ov2740_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     OV2740_EXPOSURE_MIN, exposure_max,
					     OV2740_EXPOSURE_STEP,
					     exposure_max);
	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov2740_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov2740_test_pattern_menu) - 1,
				     0, 0, ov2740_test_pattern_menu);
	if (ctrl_hdlr->error)
		return ctrl_hdlr->error;

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

static int ov2740_start_streaming(struct ov2740 *ov2740)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov2740->sd);
	const struct ov2740_reg_list *reg_list;
	int link_freq_index;
	int ret = 0;

	link_freq_index = ov2740->cur_mode->link_freq_index;
	reg_list = &link_freq_configs[link_freq_index].reg_list;
	ret = ov2740_write_reg_list(ov2740, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set plls");
		return ret;
	}

	reg_list = &ov2740->cur_mode->reg_list;
	ret = ov2740_write_reg_list(ov2740, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set mode");
		return ret;
	}

	ret = __v4l2_ctrl_handler_setup(ov2740->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = ov2740_write_reg(ov2740, OV2740_REG_MODE_SELECT, 1,
			       OV2740_MODE_STREAMING);
	if (ret)
		dev_err(&client->dev, "failed to start streaming");

	return ret;
}

static void ov2740_stop_streaming(struct ov2740 *ov2740)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov2740->sd);

	if (ov2740_write_reg(ov2740, OV2740_REG_MODE_SELECT, 1,
			     OV2740_MODE_STANDBY))
		dev_err(&client->dev, "failed to stop streaming");
}

static int ov2740_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov2740 *ov2740 = to_ov2740(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (ov2740->streaming == enable)
		return 0;

	mutex_lock(&ov2740->mutex);
	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			mutex_unlock(&ov2740->mutex);
			return ret;
		}

		ret = ov2740_start_streaming(ov2740);
		if (ret) {
			enable = 0;
			ov2740_stop_streaming(ov2740);
			pm_runtime_put(&client->dev);
		}
	} else {
		ov2740_stop_streaming(ov2740);
		pm_runtime_put(&client->dev);
	}

	ov2740->streaming = enable;
	mutex_unlock(&ov2740->mutex);

	return ret;
}

static int __maybe_unused ov2740_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov2740 *ov2740 = to_ov2740(sd);

	mutex_lock(&ov2740->mutex);
	if (ov2740->streaming)
		ov2740_stop_streaming(ov2740);

	mutex_unlock(&ov2740->mutex);

	return 0;
}

static int __maybe_unused ov2740_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov2740 *ov2740 = to_ov2740(sd);
	int ret = 0;

	mutex_lock(&ov2740->mutex);
	if (!ov2740->streaming)
		goto exit;

	ret = ov2740_start_streaming(ov2740);
	if (ret) {
		ov2740->streaming = false;
		ov2740_stop_streaming(ov2740);
	}

exit:
	mutex_unlock(&ov2740->mutex);
	return ret;
}

static int ov2740_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct ov2740 *ov2740 = to_ov2740(sd);
	const struct ov2740_mode *mode;
	s32 vblank_def, h_blank;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes), width,
				      height, fmt->format.width,
				      fmt->format.height);

	mutex_lock(&ov2740->mutex);
	ov2740_update_pad_format(mode, &fmt->format);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
	} else {
		ov2740->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(ov2740->link_freq, mode->link_freq_index);
		__v4l2_ctrl_s_ctrl_int64(ov2740->pixel_rate,
					 to_pixel_rate(mode->link_freq_index));

		/* Update limits and set FPS to default */
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov2740->vblank,
					 mode->vts_min - mode->height,
					 OV2740_VTS_MAX - mode->height, 1,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(ov2740->vblank, vblank_def);
		h_blank = to_pixels_per_line(mode->hts, mode->link_freq_index) -
			  mode->width;
		__v4l2_ctrl_modify_range(ov2740->hblank, h_blank, h_blank, 1,
					 h_blank);
	}
	mutex_unlock(&ov2740->mutex);

	return 0;
}

static int ov2740_get_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *fmt)
{
	struct ov2740 *ov2740 = to_ov2740(sd);

	mutex_lock(&ov2740->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt->format = *v4l2_subdev_get_try_format(&ov2740->sd, cfg,
							  fmt->pad);
	else
		ov2740_update_pad_format(ov2740->cur_mode, &fmt->format);

	mutex_unlock(&ov2740->mutex);

	return 0;
}

static int ov2740_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int ov2740_enum_frame_size(struct v4l2_subdev *sd,
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

static int ov2740_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov2740 *ov2740 = to_ov2740(sd);

	mutex_lock(&ov2740->mutex);
	ov2740_update_pad_format(&supported_modes[0],
				 v4l2_subdev_get_try_format(sd, fh->pad, 0));
	mutex_unlock(&ov2740->mutex);

	return 0;
}

static const struct v4l2_subdev_video_ops ov2740_video_ops = {
	.s_stream = ov2740_set_stream,
};

static const struct v4l2_subdev_pad_ops ov2740_pad_ops = {
	.set_fmt = ov2740_set_format,
	.get_fmt = ov2740_get_format,
	.enum_mbus_code = ov2740_enum_mbus_code,
	.enum_frame_size = ov2740_enum_frame_size,
};

static const struct v4l2_subdev_ops ov2740_subdev_ops = {
	.video = &ov2740_video_ops,
	.pad = &ov2740_pad_ops,
};

static const struct media_entity_operations ov2740_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops ov2740_internal_ops = {
	.open = ov2740_open,
};

static int ov2740_identify_module(struct ov2740 *ov2740)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov2740->sd);
	int ret;
	u32 val;

	ret = ov2740_read_reg(ov2740, OV2740_REG_CHIP_ID, 3, &val);
	if (ret)
		return ret;

	if (val != OV2740_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x",
			OV2740_CHIP_ID, val);
		return -ENXIO;
	}

	return 0;
}

static int ov2740_check_hwcfg(struct device *dev)
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
	if (ret)
		return ret;

	if (mclk != OV2740_MCLK) {
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

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != OV2740_DATA_LANES) {
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

static int ov2740_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov2740 *ov2740 = to_ov2740(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(&client->dev);
	mutex_destroy(&ov2740->mutex);

	return 0;
}

static int ov2740_load_otp_data(struct i2c_client *client, struct nvm_data *nvm)
{
	struct ov2740 *ov2740 = to_ov2740(i2c_get_clientdata(client));
	u32 isp_ctrl00 = 0;
	u32 isp_ctrl01 = 0;
	int ret;

	ret = ov2740_read_reg(ov2740, OV2740_REG_ISP_CTRL00, 1, &isp_ctrl00);
	if (ret) {
		dev_err(&client->dev, "failed to read ISP CTRL00\n");
		goto exit;
	}
	ret = ov2740_read_reg(ov2740, OV2740_REG_ISP_CTRL01, 1, &isp_ctrl01);
	if (ret) {
		dev_err(&client->dev, "failed to read ISP CTRL01\n");
		goto exit;
	}

	/* Clear bit 5 of ISP CTRL00 */
	ret = ov2740_write_reg(ov2740, OV2740_REG_ISP_CTRL00, 1,
			       isp_ctrl00 & ~BIT(5));
	if (ret) {
		dev_err(&client->dev, "failed to write ISP CTRL00\n");
		goto exit;
	}

	/* Clear bit 7 of ISP CTRL01 */
	ret = ov2740_write_reg(ov2740, OV2740_REG_ISP_CTRL01, 1,
			       isp_ctrl01 & ~BIT(7));
	if (ret) {
		dev_err(&client->dev, "failed to write ISP CTRL01\n");
		goto exit;
	}

	ret = ov2740_write_reg(ov2740, OV2740_REG_MODE_SELECT, 1,
			       OV2740_MODE_STREAMING);
	if (ret) {
		dev_err(&client->dev, "failed to start streaming\n");
		goto exit;
	}

	/*
	 * Users are not allowed to access OTP-related registers and memory
	 * during the 20 ms period after streaming starts (0x100 = 0x01).
	 */
	msleep(20);

	ret = regmap_bulk_read(nvm->regmap, OV2740_REG_OTP_CUSTOMER,
			       nvm->nvm_buffer, CUSTOMER_USE_OTP_SIZE);
	if (ret) {
		dev_err(&client->dev, "failed to read OTP data, ret %d\n", ret);
		goto exit;
	}

	ov2740_write_reg(ov2740, OV2740_REG_MODE_SELECT, 1,
			 OV2740_MODE_STANDBY);
	ov2740_write_reg(ov2740, OV2740_REG_ISP_CTRL01, 1, isp_ctrl01);
	ov2740_write_reg(ov2740, OV2740_REG_ISP_CTRL00, 1, isp_ctrl00);

exit:
	return ret;
}

static int ov2740_nvmem_read(void *priv, unsigned int off, void *val,
			     size_t count)
{
	struct nvm_data *nvm = priv;

	memcpy(val, nvm->nvm_buffer + off, count);

	return 0;
}

static int ov2740_register_nvmem(struct i2c_client *client)
{
	struct nvm_data *nvm;
	struct regmap_config regmap_config = { };
	struct nvmem_config nvmem_config = { };
	struct regmap *regmap;
	struct device *dev = &client->dev;
	int ret = 0;

	nvm = devm_kzalloc(dev, sizeof(*nvm), GFP_KERNEL);
	if (!nvm)
		return -ENOMEM;

	nvm->nvm_buffer = devm_kzalloc(dev, CUSTOMER_USE_OTP_SIZE, GFP_KERNEL);
	if (!nvm->nvm_buffer)
		return -ENOMEM;

	regmap_config.val_bits = 8;
	regmap_config.reg_bits = 16;
	regmap_config.disable_locking = true;
	regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	nvm->regmap = regmap;

	ret = ov2740_load_otp_data(client, nvm);
	if (ret) {
		dev_err(dev, "failed to load OTP data, ret %d\n", ret);
		return ret;
	}

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

	return PTR_ERR_OR_ZERO(nvm->nvmem);
}

static int ov2740_probe(struct i2c_client *client)
{
	struct ov2740 *ov2740;
	int ret = 0;

	ret = ov2740_check_hwcfg(&client->dev);
	if (ret) {
		dev_err(&client->dev, "failed to check HW configuration: %d",
			ret);
		return ret;
	}

	ov2740 = devm_kzalloc(&client->dev, sizeof(*ov2740), GFP_KERNEL);
	if (!ov2740)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&ov2740->sd, client, &ov2740_subdev_ops);
	ret = ov2740_identify_module(ov2740);
	if (ret) {
		dev_err(&client->dev, "failed to find sensor: %d", ret);
		return ret;
	}

	mutex_init(&ov2740->mutex);
	ov2740->cur_mode = &supported_modes[0];
	ret = ov2740_init_controls(ov2740);
	if (ret) {
		dev_err(&client->dev, "failed to init controls: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ov2740->sd.internal_ops = &ov2740_internal_ops;
	ov2740->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov2740->sd.entity.ops = &ov2740_subdev_entity_ops;
	ov2740->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ov2740->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&ov2740->sd.entity, 1, &ov2740->pad);
	if (ret) {
		dev_err(&client->dev, "failed to init entity pads: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor_common(&ov2740->sd);
	if (ret < 0) {
		dev_err(&client->dev, "failed to register V4L2 subdev: %d",
			ret);
		goto probe_error_media_entity_cleanup;
	}

	ret = ov2740_register_nvmem(client);
	if (ret)
		dev_warn(&client->dev, "register nvmem failed, ret %d\n", ret);

	/*
	 * Device is already turned on by i2c-core with ACPI domain PM.
	 * Enable runtime PM and turn off the device.
	 */
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

probe_error_media_entity_cleanup:
	media_entity_cleanup(&ov2740->sd.entity);

probe_error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(ov2740->sd.ctrl_handler);
	mutex_destroy(&ov2740->mutex);

	return ret;
}

static const struct dev_pm_ops ov2740_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ov2740_suspend, ov2740_resume)
};

static const struct acpi_device_id ov2740_acpi_ids[] = {
	{"INT3474"},
	{}
};

MODULE_DEVICE_TABLE(acpi, ov2740_acpi_ids);

static struct i2c_driver ov2740_i2c_driver = {
	.driver = {
		.name = "ov2740",
		.pm = &ov2740_pm_ops,
		.acpi_match_table = ov2740_acpi_ids,
	},
	.probe_new = ov2740_probe,
	.remove = ov2740_remove,
};

module_i2c_driver(ov2740_i2c_driver);

MODULE_AUTHOR("Qiu, Tianshu <tian.shu.qiu@intel.com>");
MODULE_AUTHOR("Shawn Tu <shawnx.tu@intel.com>");
MODULE_AUTHOR("Bingbu Cao <bingbu.cao@intel.com>");
MODULE_DESCRIPTION("OmniVision OV2740 sensor driver");
MODULE_LICENSE("GPL v2");
