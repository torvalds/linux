// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Intel Corporation.

#include <asm/unaligned.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define OV9734_LINK_FREQ_180MHZ		180000000ULL
#define OV9734_SCLK			36000000LL
#define OV9734_MCLK			19200000
/* ov9734 only support 1-lane mipi output */
#define OV9734_DATA_LANES		1
#define OV9734_RGB_DEPTH		10

#define OV9734_REG_CHIP_ID		0x300a
#define OV9734_CHIP_ID			0x9734

#define OV9734_REG_MODE_SELECT		0x0100
#define OV9734_MODE_STANDBY		0x00
#define OV9734_MODE_STREAMING		0x01

/* vertical-timings from sensor */
#define OV9734_REG_VTS			0x380e
#define OV9734_VTS_30FPS		0x0322
#define OV9734_VTS_30FPS_MIN		0x0322
#define OV9734_VTS_MAX			0x7fff

/* horizontal-timings from sensor */
#define OV9734_REG_HTS			0x380c

/* Exposure controls from sensor */
#define OV9734_REG_EXPOSURE		0x3500
#define OV9734_EXPOSURE_MIN		4
#define OV9734_EXPOSURE_MAX_MARGIN	4
#define	OV9734_EXPOSURE_STEP		1

/* Analog gain controls from sensor */
#define OV9734_REG_ANALOG_GAIN		0x350a
#define OV9734_ANAL_GAIN_MIN		16
#define OV9734_ANAL_GAIN_MAX		248
#define OV9734_ANAL_GAIN_STEP		1

/* Digital gain controls from sensor */
#define OV9734_REG_MWB_R_GAIN		0x5180
#define OV9734_REG_MWB_G_GAIN		0x5182
#define OV9734_REG_MWB_B_GAIN		0x5184
#define OV9734_DGTL_GAIN_MIN		256
#define OV9734_DGTL_GAIN_MAX		1023
#define OV9734_DGTL_GAIN_STEP		1
#define OV9734_DGTL_GAIN_DEFAULT	256

/* Test Pattern Control */
#define OV9734_REG_TEST_PATTERN		0x5080
#define OV9734_TEST_PATTERN_ENABLE	BIT(7)
#define OV9734_TEST_PATTERN_BAR_SHIFT	2

/* Group Access */
#define OV9734_REG_GROUP_ACCESS		0x3208
#define OV9734_GROUP_HOLD_START		0x0
#define OV9734_GROUP_HOLD_END		0x10
#define OV9734_GROUP_HOLD_LAUNCH	0xa0

enum {
	OV9734_LINK_FREQ_180MHZ_INDEX,
};

struct ov9734_reg {
	u16 address;
	u8 val;
};

struct ov9734_reg_list {
	u32 num_of_regs;
	const struct ov9734_reg *regs;
};

struct ov9734_link_freq_config {
	const struct ov9734_reg_list reg_list;
};

struct ov9734_mode {
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
	const struct ov9734_reg_list reg_list;
};

static const struct ov9734_reg mipi_data_rate_360mbps[] = {
	{0x3030, 0x19},
	{0x3080, 0x02},
	{0x3081, 0x4b},
	{0x3082, 0x04},
	{0x3083, 0x00},
	{0x3084, 0x02},
	{0x3085, 0x01},
	{0x3086, 0x01},
	{0x3089, 0x01},
	{0x308a, 0x00},
	{0x301e, 0x15},
	{0x3103, 0x01},
};

static const struct ov9734_reg mode_1296x734_regs[] = {
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x3007, 0x00},
	{0x3010, 0x00},
	{0x3011, 0x08},
	{0x3014, 0x22},
	{0x3600, 0x55},
	{0x3601, 0x02},
	{0x3605, 0x22},
	{0x3611, 0xe7},
	{0x3654, 0x10},
	{0x3655, 0x77},
	{0x3656, 0x77},
	{0x3657, 0x07},
	{0x3658, 0x22},
	{0x3659, 0x22},
	{0x365a, 0x02},
	{0x3784, 0x05},
	{0x3785, 0x55},
	{0x37c0, 0x07},
	{0x3800, 0x00},
	{0x3801, 0x04},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x05},
	{0x3805, 0x0b},
	{0x3806, 0x02},
	{0x3807, 0xdb},
	{0x3808, 0x05},
	{0x3809, 0x00},
	{0x380a, 0x02},
	{0x380b, 0xd0},
	{0x380c, 0x05},
	{0x380d, 0xc6},
	{0x380e, 0x03},
	{0x380f, 0x22},
	{0x3810, 0x00},
	{0x3811, 0x04},
	{0x3812, 0x00},
	{0x3813, 0x04},
	{0x3816, 0x00},
	{0x3817, 0x00},
	{0x3818, 0x00},
	{0x3819, 0x04},
	{0x3820, 0x18},
	{0x3821, 0x00},
	{0x382c, 0x06},
	{0x3500, 0x00},
	{0x3501, 0x31},
	{0x3502, 0x00},
	{0x3503, 0x03},
	{0x3504, 0x00},
	{0x3505, 0x00},
	{0x3509, 0x10},
	{0x350a, 0x00},
	{0x350b, 0x40},
	{0x3d00, 0x00},
	{0x3d01, 0x00},
	{0x3d02, 0x00},
	{0x3d03, 0x00},
	{0x3d04, 0x00},
	{0x3d05, 0x00},
	{0x3d06, 0x00},
	{0x3d07, 0x00},
	{0x3d08, 0x00},
	{0x3d09, 0x00},
	{0x3d0a, 0x00},
	{0x3d0b, 0x00},
	{0x3d0c, 0x00},
	{0x3d0d, 0x00},
	{0x3d0e, 0x00},
	{0x3d0f, 0x00},
	{0x3d80, 0x00},
	{0x3d81, 0x00},
	{0x3d82, 0x38},
	{0x3d83, 0xa4},
	{0x3d84, 0x00},
	{0x3d85, 0x00},
	{0x3d86, 0x1f},
	{0x3d87, 0x03},
	{0x3d8b, 0x00},
	{0x3d8f, 0x00},
	{0x4001, 0xe0},
	{0x4009, 0x0b},
	{0x4300, 0x03},
	{0x4301, 0xff},
	{0x4304, 0x00},
	{0x4305, 0x00},
	{0x4309, 0x00},
	{0x4600, 0x00},
	{0x4601, 0x80},
	{0x4800, 0x00},
	{0x4805, 0x00},
	{0x4821, 0x50},
	{0x4823, 0x50},
	{0x4837, 0x2d},
	{0x4a00, 0x00},
	{0x4f00, 0x80},
	{0x4f01, 0x10},
	{0x4f02, 0x00},
	{0x4f03, 0x00},
	{0x4f04, 0x00},
	{0x4f05, 0x00},
	{0x4f06, 0x00},
	{0x4f07, 0x00},
	{0x4f08, 0x00},
	{0x4f09, 0x00},
	{0x5000, 0x2f},
	{0x500c, 0x00},
	{0x500d, 0x00},
	{0x500e, 0x00},
	{0x500f, 0x00},
	{0x5010, 0x00},
	{0x5011, 0x00},
	{0x5012, 0x00},
	{0x5013, 0x00},
	{0x5014, 0x00},
	{0x5015, 0x00},
	{0x5016, 0x00},
	{0x5017, 0x00},
	{0x5080, 0x00},
	{0x5180, 0x01},
	{0x5181, 0x00},
	{0x5182, 0x01},
	{0x5183, 0x00},
	{0x5184, 0x01},
	{0x5185, 0x00},
	{0x5708, 0x06},
	{0x380f, 0x2a},
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
	{0x5791, 0x04},
	{0x5792, 0x00},
	{0x5793, 0x52},
	{0x5794, 0xa3},
	{0x5000, 0x3f},
	{0x3801, 0x00},
	{0x3803, 0x00},
	{0x3805, 0x0f},
	{0x3807, 0xdf},
	{0x3809, 0x10},
	{0x380b, 0xde},
	{0x3811, 0x00},
	{0x3813, 0x01},
};

static const char * const ov9734_test_pattern_menu[] = {
	"Disabled",
	"Standard Color Bar",
	"Top-Bottom Darker Color Bar",
	"Right-Left Darker Color Bar",
	"Bottom-Top Darker Color Bar",
};

static const s64 link_freq_menu_items[] = {
	OV9734_LINK_FREQ_180MHZ,
};

static const struct ov9734_link_freq_config link_freq_configs[] = {
	[OV9734_LINK_FREQ_180MHZ_INDEX] = {
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_360mbps),
			.regs = mipi_data_rate_360mbps,
		}
	},
};

static const struct ov9734_mode supported_modes[] = {
	{
		.width = 1296,
		.height = 734,
		.hts = 0x5c6,
		.vts_def = OV9734_VTS_30FPS,
		.vts_min = OV9734_VTS_30FPS_MIN,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mode_1296x734_regs),
			.regs = mode_1296x734_regs,
		},
		.link_freq_index = OV9734_LINK_FREQ_180MHZ_INDEX,
	},
};

struct ov9734 {
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
	const struct ov9734_mode *cur_mode;

	/* To serialize asynchronus callbacks */
	struct mutex mutex;

	/* Streaming on/off */
	bool streaming;
};

static inline struct ov9734 *to_ov9734(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct ov9734, sd);
}

static u64 to_pixel_rate(u32 f_index)
{
	u64 pixel_rate = link_freq_menu_items[f_index] * 2 * OV9734_DATA_LANES;

	do_div(pixel_rate, OV9734_RGB_DEPTH);

	return pixel_rate;
}

static u64 to_pixels_per_line(u32 hts, u32 f_index)
{
	u64 ppl = hts * to_pixel_rate(f_index);

	do_div(ppl, OV9734_SCLK);

	return ppl;
}

static int ov9734_read_reg(struct ov9734 *ov9734, u16 reg, u16 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov9734->sd);
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

static int ov9734_write_reg(struct ov9734 *ov9734, u16 reg, u16 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov9734->sd);
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

static int ov9734_write_reg_list(struct ov9734 *ov9734,
				 const struct ov9734_reg_list *r_list)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov9734->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < r_list->num_of_regs; i++) {
		ret = ov9734_write_reg(ov9734, r_list->regs[i].address, 1,
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

static int ov9734_update_digital_gain(struct ov9734 *ov9734, u32 d_gain)
{
	int ret;

	ret = ov9734_write_reg(ov9734, OV9734_REG_GROUP_ACCESS, 1,
			       OV9734_GROUP_HOLD_START);
	if (ret)
		return ret;

	ret = ov9734_write_reg(ov9734, OV9734_REG_MWB_R_GAIN, 2, d_gain);
	if (ret)
		return ret;

	ret = ov9734_write_reg(ov9734, OV9734_REG_MWB_G_GAIN, 2, d_gain);
	if (ret)
		return ret;

	ret = ov9734_write_reg(ov9734, OV9734_REG_MWB_B_GAIN, 2, d_gain);
	if (ret)
		return ret;

	ret = ov9734_write_reg(ov9734, OV9734_REG_GROUP_ACCESS, 1,
			       OV9734_GROUP_HOLD_END);
	if (ret)
		return ret;

	ret = ov9734_write_reg(ov9734, OV9734_REG_GROUP_ACCESS, 1,
			       OV9734_GROUP_HOLD_LAUNCH);
	return ret;
}

static int ov9734_test_pattern(struct ov9734 *ov9734, u32 pattern)
{
	if (pattern)
		pattern = (pattern - 1) << OV9734_TEST_PATTERN_BAR_SHIFT |
			OV9734_TEST_PATTERN_ENABLE;

	return ov9734_write_reg(ov9734, OV9734_REG_TEST_PATTERN, 1, pattern);
}

static int ov9734_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov9734 *ov9734 = container_of(ctrl->handler,
					     struct ov9734, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&ov9734->sd);
	s64 exposure_max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Update max exposure while meeting expected vblanking */
		exposure_max = ov9734->cur_mode->height + ctrl->val -
			OV9734_EXPOSURE_MAX_MARGIN;
		__v4l2_ctrl_modify_range(ov9734->exposure,
					 ov9734->exposure->minimum,
					 exposure_max, ov9734->exposure->step,
					 exposure_max);
	}

	/* V4L2 controls values will be applied only when power is already up */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov9734_write_reg(ov9734, OV9734_REG_ANALOG_GAIN,
				       2, ctrl->val);
		break;

	case V4L2_CID_DIGITAL_GAIN:
		ret = ov9734_update_digital_gain(ov9734, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = ov9734_write_reg(ov9734, OV9734_REG_EXPOSURE,
				       3, ctrl->val << 4);
		break;

	case V4L2_CID_VBLANK:
		ret = ov9734_write_reg(ov9734, OV9734_REG_VTS, 2,
				       ov9734->cur_mode->height + ctrl->val);
		break;

	case V4L2_CID_TEST_PATTERN:
		ret = ov9734_test_pattern(ov9734, ctrl->val);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov9734_ctrl_ops = {
	.s_ctrl = ov9734_set_ctrl,
};

static int ov9734_init_controls(struct ov9734 *ov9734)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	const struct ov9734_mode *cur_mode;
	s64 exposure_max, h_blank, pixel_rate;
	u32 vblank_min, vblank_max, vblank_default;
	int ret, size;

	ctrl_hdlr = &ov9734->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	ctrl_hdlr->lock = &ov9734->mutex;
	cur_mode = ov9734->cur_mode;
	size = ARRAY_SIZE(link_freq_menu_items);
	ov9734->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &ov9734_ctrl_ops,
						   V4L2_CID_LINK_FREQ,
						   size - 1, 0,
						   link_freq_menu_items);
	if (ov9734->link_freq)
		ov9734->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate = to_pixel_rate(OV9734_LINK_FREQ_180MHZ_INDEX);
	ov9734->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ov9734_ctrl_ops,
					       V4L2_CID_PIXEL_RATE, 0,
					       pixel_rate, 1, pixel_rate);
	vblank_min = cur_mode->vts_min - cur_mode->height;
	vblank_max = OV9734_VTS_MAX - cur_mode->height;
	vblank_default = cur_mode->vts_def - cur_mode->height;
	ov9734->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov9734_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_min,
					   vblank_max, 1, vblank_default);
	h_blank = to_pixels_per_line(cur_mode->hts, cur_mode->link_freq_index);
	h_blank -= cur_mode->width;
	ov9734->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov9734_ctrl_ops,
					   V4L2_CID_HBLANK, h_blank, h_blank, 1,
					   h_blank);
	if (ov9734->hblank)
		ov9734->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &ov9734_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV9734_ANAL_GAIN_MIN, OV9734_ANAL_GAIN_MAX,
			  OV9734_ANAL_GAIN_STEP, OV9734_ANAL_GAIN_MIN);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov9734_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV9734_DGTL_GAIN_MIN, OV9734_DGTL_GAIN_MAX,
			  OV9734_DGTL_GAIN_STEP, OV9734_DGTL_GAIN_DEFAULT);
	exposure_max = ov9734->cur_mode->vts_def - OV9734_EXPOSURE_MAX_MARGIN;
	ov9734->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ov9734_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     OV9734_EXPOSURE_MIN, exposure_max,
					     OV9734_EXPOSURE_STEP,
					     exposure_max);
	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov9734_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov9734_test_pattern_menu) - 1,
				     0, 0, ov9734_test_pattern_menu);
	if (ctrl_hdlr->error)
		return ctrl_hdlr->error;

	ov9734->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

static void ov9734_update_pad_format(const struct ov9734_mode *mode,
				     struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	fmt->field = V4L2_FIELD_NONE;
}

static int ov9734_start_streaming(struct ov9734 *ov9734)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov9734->sd);
	const struct ov9734_reg_list *reg_list;
	int link_freq_index, ret;

	link_freq_index = ov9734->cur_mode->link_freq_index;
	reg_list = &link_freq_configs[link_freq_index].reg_list;
	ret = ov9734_write_reg_list(ov9734, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set plls");
		return ret;
	}

	reg_list = &ov9734->cur_mode->reg_list;
	ret = ov9734_write_reg_list(ov9734, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set mode");
		return ret;
	}

	ret = __v4l2_ctrl_handler_setup(ov9734->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = ov9734_write_reg(ov9734, OV9734_REG_MODE_SELECT,
			       1, OV9734_MODE_STREAMING);
	if (ret)
		dev_err(&client->dev, "failed to start stream");

	return ret;
}

static void ov9734_stop_streaming(struct ov9734 *ov9734)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov9734->sd);

	if (ov9734_write_reg(ov9734, OV9734_REG_MODE_SELECT,
			     1, OV9734_MODE_STANDBY))
		dev_err(&client->dev, "failed to stop stream");
}

static int ov9734_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov9734 *ov9734 = to_ov9734(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&ov9734->mutex);
	if (ov9734->streaming == enable) {
		mutex_unlock(&ov9734->mutex);
		return 0;
	}

	if (enable) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0) {
			mutex_unlock(&ov9734->mutex);
			return ret;
		}

		ret = ov9734_start_streaming(ov9734);
		if (ret) {
			enable = 0;
			ov9734_stop_streaming(ov9734);
			pm_runtime_put(&client->dev);
		}
	} else {
		ov9734_stop_streaming(ov9734);
		pm_runtime_put(&client->dev);
	}

	ov9734->streaming = enable;
	mutex_unlock(&ov9734->mutex);

	return ret;
}

static int __maybe_unused ov9734_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov9734 *ov9734 = to_ov9734(sd);

	mutex_lock(&ov9734->mutex);
	if (ov9734->streaming)
		ov9734_stop_streaming(ov9734);

	mutex_unlock(&ov9734->mutex);

	return 0;
}

static int __maybe_unused ov9734_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov9734 *ov9734 = to_ov9734(sd);
	int ret = 0;

	mutex_lock(&ov9734->mutex);
	if (!ov9734->streaming)
		goto exit;

	ret = ov9734_start_streaming(ov9734);
	if (ret) {
		ov9734->streaming = false;
		ov9734_stop_streaming(ov9734);
	}

exit:
	mutex_unlock(&ov9734->mutex);
	return ret;
}

static int ov9734_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *fmt)
{
	struct ov9734 *ov9734 = to_ov9734(sd);
	const struct ov9734_mode *mode;
	s32 vblank_def, h_blank;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes), width,
				      height, fmt->format.width,
				      fmt->format.height);

	mutex_lock(&ov9734->mutex);
	ov9734_update_pad_format(mode, &fmt->format);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_get_try_format(sd, sd_state, fmt->pad) = fmt->format;
	} else {
		ov9734->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(ov9734->link_freq, mode->link_freq_index);
		__v4l2_ctrl_s_ctrl_int64(ov9734->pixel_rate,
					 to_pixel_rate(mode->link_freq_index));

		/* Update limits and set FPS to default */
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov9734->vblank,
					 mode->vts_min - mode->height,
					 OV9734_VTS_MAX - mode->height, 1,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(ov9734->vblank, vblank_def);
		h_blank = to_pixels_per_line(mode->hts, mode->link_freq_index) -
			mode->width;
		__v4l2_ctrl_modify_range(ov9734->hblank, h_blank, h_blank, 1,
					 h_blank);
	}

	mutex_unlock(&ov9734->mutex);

	return 0;
}

static int ov9734_get_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *fmt)
{
	struct ov9734 *ov9734 = to_ov9734(sd);

	mutex_lock(&ov9734->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt->format = *v4l2_subdev_get_try_format(&ov9734->sd,
							  sd_state,
							  fmt->pad);
	else
		ov9734_update_pad_format(ov9734->cur_mode, &fmt->format);

	mutex_unlock(&ov9734->mutex);

	return 0;
}

static int ov9734_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int ov9734_enum_frame_size(struct v4l2_subdev *sd,
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

static int ov9734_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov9734 *ov9734 = to_ov9734(sd);

	mutex_lock(&ov9734->mutex);
	ov9734_update_pad_format(&supported_modes[0],
				 v4l2_subdev_get_try_format(sd, fh->state, 0));
	mutex_unlock(&ov9734->mutex);

	return 0;
}

static const struct v4l2_subdev_video_ops ov9734_video_ops = {
	.s_stream = ov9734_set_stream,
};

static const struct v4l2_subdev_pad_ops ov9734_pad_ops = {
	.set_fmt = ov9734_set_format,
	.get_fmt = ov9734_get_format,
	.enum_mbus_code = ov9734_enum_mbus_code,
	.enum_frame_size = ov9734_enum_frame_size,
};

static const struct v4l2_subdev_ops ov9734_subdev_ops = {
	.video = &ov9734_video_ops,
	.pad = &ov9734_pad_ops,
};

static const struct media_entity_operations ov9734_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops ov9734_internal_ops = {
	.open = ov9734_open,
};

static int ov9734_identify_module(struct ov9734 *ov9734)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov9734->sd);
	int ret;
	u32 val;

	ret = ov9734_read_reg(ov9734, OV9734_REG_CHIP_ID, 2, &val);
	if (ret)
		return ret;

	if (val != OV9734_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x",
			OV9734_CHIP_ID, val);
		return -ENXIO;
	}

	return 0;
}

static int ov9734_check_hwcfg(struct device *dev)
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

	if (mclk != OV9734_MCLK) {
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

static void ov9734_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov9734 *ov9734 = to_ov9734(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	mutex_destroy(&ov9734->mutex);
}

static int ov9734_probe(struct i2c_client *client)
{
	struct ov9734 *ov9734;
	int ret;

	ret = ov9734_check_hwcfg(&client->dev);
	if (ret) {
		dev_err(&client->dev, "failed to check HW configuration: %d",
			ret);
		return ret;
	}

	ov9734 = devm_kzalloc(&client->dev, sizeof(*ov9734), GFP_KERNEL);
	if (!ov9734)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&ov9734->sd, client, &ov9734_subdev_ops);
	ret = ov9734_identify_module(ov9734);
	if (ret) {
		dev_err(&client->dev, "failed to find sensor: %d", ret);
		return ret;
	}

	mutex_init(&ov9734->mutex);
	ov9734->cur_mode = &supported_modes[0];
	ret = ov9734_init_controls(ov9734);
	if (ret) {
		dev_err(&client->dev, "failed to init controls: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ov9734->sd.internal_ops = &ov9734_internal_ops;
	ov9734->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov9734->sd.entity.ops = &ov9734_subdev_entity_ops;
	ov9734->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ov9734->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&ov9734->sd.entity, 1, &ov9734->pad);
	if (ret) {
		dev_err(&client->dev, "failed to init entity pads: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	/*
	 * Device is already turned on by i2c-core with ACPI domain PM.
	 * Enable runtime PM and turn off the device.
	 */
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	ret = v4l2_async_register_subdev_sensor(&ov9734->sd);
	if (ret < 0) {
		dev_err(&client->dev, "failed to register V4L2 subdev: %d",
			ret);
		goto probe_error_media_entity_cleanup_pm;
	}

	return 0;

probe_error_media_entity_cleanup_pm:
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	media_entity_cleanup(&ov9734->sd.entity);

probe_error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(ov9734->sd.ctrl_handler);
	mutex_destroy(&ov9734->mutex);

	return ret;
}

static const struct dev_pm_ops ov9734_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ov9734_suspend, ov9734_resume)
};

static const struct acpi_device_id ov9734_acpi_ids[] = {
	{ "OVTI9734", },
	{}
};

MODULE_DEVICE_TABLE(acpi, ov9734_acpi_ids);

static struct i2c_driver ov9734_i2c_driver = {
	.driver = {
		.name = "ov9734",
		.pm = &ov9734_pm_ops,
		.acpi_match_table = ov9734_acpi_ids,
	},
	.probe = ov9734_probe,
	.remove = ov9734_remove,
};

module_i2c_driver(ov9734_i2c_driver);

MODULE_AUTHOR("Qiu, Tianshu <tian.shu.qiu@intel.com>");
MODULE_AUTHOR("Bingbu Cao <bingbu.cao@intel.com>");
MODULE_DESCRIPTION("OmniVision OV9734 sensor driver");
MODULE_LICENSE("GPL v2");
