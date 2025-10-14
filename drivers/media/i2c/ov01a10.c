// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Intel Corporation.
 */

#include <linux/unaligned.h>

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define OV01A10_LINK_FREQ_400MHZ	400000000ULL
#define OV01A10_SCLK			80000000LL
#define OV01A10_DATA_LANES		1
#define OV01A10_MCLK			19200000

#define OV01A10_REG_CHIP_ID		CCI_REG24(0x300a)
#define OV01A10_CHIP_ID			0x560141

#define OV01A10_REG_MODE_SELECT		CCI_REG8(0x0100)
#define OV01A10_MODE_STANDBY		0x00
#define OV01A10_MODE_STREAMING		0x01

/* pixel array */
#define OV01A10_NATIVE_WIDTH		1296
#define OV01A10_NATIVE_HEIGHT		816
#define OV01A10_DEFAULT_WIDTH		1280
#define OV01A10_DEFAULT_HEIGHT		800

/* vertical and horizontal timings */
#define OV01A10_REG_VTS			CCI_REG16(0x380e)
#define OV01A10_VTS_DEF			0x0700
#define OV01A10_VTS_MIN			0x0380
#define OV01A10_VTS_MAX			0xffff
#define OV01A10_HTS_DEF			1488

/* exposure controls */
#define OV01A10_REG_EXPOSURE		CCI_REG16(0x3501)
#define OV01A10_EXPOSURE_MIN		4
#define OV01A10_EXPOSURE_MAX_MARGIN	8
#define OV01A10_EXPOSURE_STEP		1

/* analog gain controls */
#define OV01A10_REG_ANALOG_GAIN		CCI_REG16(0x3508)
#define OV01A10_ANAL_GAIN_MIN		0x100
#define OV01A10_ANAL_GAIN_MAX		0x3fff
#define OV01A10_ANAL_GAIN_STEP		1

/* digital gain controls */
#define OV01A10_REG_DIGITAL_GAIN_B	CCI_REG24(0x350a)
#define OV01A10_REG_DIGITAL_GAIN_GB	CCI_REG24(0x3510)
#define OV01A10_REG_DIGITAL_GAIN_GR	CCI_REG24(0x3513)
#define OV01A10_REG_DIGITAL_GAIN_R	CCI_REG24(0x3516)
#define OV01A10_DGTL_GAIN_MIN		0
#define OV01A10_DGTL_GAIN_MAX		0x3ffff
#define OV01A10_DGTL_GAIN_STEP		1
#define OV01A10_DGTL_GAIN_DEFAULT	1024

/* test pattern control */
#define OV01A10_REG_TEST_PATTERN	CCI_REG8(0x4503)
#define OV01A10_TEST_PATTERN_ENABLE	BIT(7)
#define OV01A10_LINK_FREQ_400MHZ_INDEX	0

/* flip and mirror control */
#define OV01A10_REG_FORMAT1		CCI_REG8(0x3820)
#define OV01A10_VFLIP_MASK		BIT(4)
#define OV01A10_HFLIP_MASK		BIT(3)

/* window offset */
#define OV01A10_REG_X_WIN		CCI_REG16(0x3810)
#define OV01A10_REG_Y_WIN		CCI_REG16(0x3812)

/*
 * The native ov01a10 bayer-pattern is GBRG, but there was a driver bug enabling
 * hflip/mirroring by default resulting in BGGR. Because of this bug Intel's
 * proprietary IPU6 userspace stack expects BGGR. So we report BGGR to not break
 * userspace and fix things up by shifting the crop window-x coordinate by 1
 * when hflip is *disabled*.
 */
#define OV01A10_MEDIA_BUS_FMT		MEDIA_BUS_FMT_SBGGR10_1X10

struct ov01a10_reg_list {
	u32 num_of_regs;
	const struct reg_sequence *regs;
};

struct ov01a10_link_freq_config {
	const struct ov01a10_reg_list reg_list;
};

struct ov01a10_mode {
	u32 width;
	u32 height;
	u32 hts;
	u32 vts_def;
	u32 vts_min;
	u32 link_freq_index;

	const struct ov01a10_reg_list reg_list;
};

static const struct reg_sequence mipi_data_rate_720mbps[] = {
	{0x0103, 0x01},
	{0x0302, 0x00},
	{0x0303, 0x06},
	{0x0304, 0x01},
	{0x0305, 0xe0},
	{0x0306, 0x00},
	{0x0308, 0x01},
	{0x0309, 0x00},
	{0x030c, 0x01},
	{0x0322, 0x01},
	{0x0323, 0x06},
	{0x0324, 0x01},
	{0x0325, 0x68},
};

static const struct reg_sequence sensor_1280x800_setting[] = {
	{0x3002, 0xa1},
	{0x301e, 0xf0},
	{0x3022, 0x01},
	{0x3501, 0x03},
	{0x3502, 0x78},
	{0x3504, 0x0c},
	{0x3508, 0x01},
	{0x3509, 0x00},
	{0x3601, 0xc0},
	{0x3603, 0x71},
	{0x3610, 0x68},
	{0x3611, 0x86},
	{0x3640, 0x10},
	{0x3641, 0x80},
	{0x3642, 0xdc},
	{0x3646, 0x55},
	{0x3647, 0x57},
	{0x364b, 0x00},
	{0x3653, 0x10},
	{0x3655, 0x00},
	{0x3656, 0x00},
	{0x365f, 0x0f},
	{0x3661, 0x45},
	{0x3662, 0x24},
	{0x3663, 0x11},
	{0x3664, 0x07},
	{0x3709, 0x34},
	{0x370b, 0x6f},
	{0x3714, 0x22},
	{0x371b, 0x27},
	{0x371c, 0x67},
	{0x371d, 0xa7},
	{0x371e, 0xe7},
	{0x3730, 0x81},
	{0x3733, 0x10},
	{0x3734, 0x40},
	{0x3737, 0x04},
	{0x3739, 0x1c},
	{0x3767, 0x00},
	{0x376c, 0x81},
	{0x3772, 0x14},
	{0x37c2, 0x04},
	{0x37d8, 0x03},
	{0x37d9, 0x0c},
	{0x37e0, 0x00},
	{0x37e1, 0x08},
	{0x37e2, 0x10},
	{0x37e3, 0x04},
	{0x37e4, 0x04},
	{0x37e5, 0x03},
	{0x37e6, 0x04},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x05},
	{0x3805, 0x0f},
	{0x3806, 0x03},
	{0x3807, 0x2f},
	{0x3808, 0x05},
	{0x3809, 0x00},
	{0x380a, 0x03},
	{0x380b, 0x20},
	{0x380c, 0x02},
	{0x380d, 0xe8},
	{0x380e, 0x07},
	{0x380f, 0x00},
	{0x3810, 0x00},
	{0x3811, 0x09},
	{0x3812, 0x00},
	{0x3813, 0x08},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3820, 0xa8},
	{0x3822, 0x13},
	{0x3832, 0x28},
	{0x3833, 0x10},
	{0x3b00, 0x00},
	{0x3c80, 0x00},
	{0x3c88, 0x02},
	{0x3c8c, 0x07},
	{0x3c8d, 0x40},
	{0x3cc7, 0x80},
	{0x4000, 0xc3},
	{0x4001, 0xe0},
	{0x4003, 0x40},
	{0x4008, 0x02},
	{0x4009, 0x19},
	{0x400a, 0x01},
	{0x400b, 0x6c},
	{0x4011, 0x00},
	{0x4041, 0x00},
	{0x4300, 0xff},
	{0x4301, 0x00},
	{0x4302, 0x0f},
	{0x4503, 0x00},
	{0x4601, 0x50},
	{0x4800, 0x64},
	{0x481f, 0x34},
	{0x4825, 0x33},
	{0x4837, 0x11},
	{0x4881, 0x40},
	{0x4883, 0x01},
	{0x4890, 0x00},
	{0x4901, 0x00},
	{0x4902, 0x00},
	{0x4b00, 0x2a},
	{0x4b0d, 0x00},
	{0x450a, 0x04},
	{0x450b, 0x00},
	{0x5000, 0x65},
	{0x5200, 0x18},
	{0x5004, 0x00},
	{0x5080, 0x40},
	{0x0305, 0xf4},
	{0x0325, 0xc2},
};

static const char * const ov01a10_test_pattern_menu[] = {
	"Disabled",
	"Color Bar",
	"Left-Right Darker Color Bar",
	"Bottom-Top Darker Color Bar",
};

static const s64 link_freq_menu_items[] = {
	OV01A10_LINK_FREQ_400MHZ,
};

static const struct ov01a10_link_freq_config link_freq_configs[] = {
	[OV01A10_LINK_FREQ_400MHZ_INDEX] = {
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_720mbps),
			.regs = mipi_data_rate_720mbps,
		}
	},
};

static const struct ov01a10_mode supported_modes[] = {
	{
		.width = OV01A10_DEFAULT_WIDTH,
		.height = OV01A10_DEFAULT_HEIGHT,
		.hts = OV01A10_HTS_DEF,
		.vts_def = OV01A10_VTS_DEF,
		.vts_min = OV01A10_VTS_MIN,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(sensor_1280x800_setting),
			.regs = sensor_1280x800_setting,
		},
		.link_freq_index = OV01A10_LINK_FREQ_400MHZ_INDEX,
	},
};

static const char * const ov01a10_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
	"dvdd",		/* Digital core power */
};

struct ov01a10 {
	struct device *dev;
	struct regmap *regmap;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;

	/* v4l2 controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;

	const struct ov01a10_mode *cur_mode;
	u32 link_freq_index;

	struct clk *clk;
	struct gpio_desc *reset;
	struct gpio_desc *powerdown;
	struct regulator_bulk_data supplies[ARRAY_SIZE(ov01a10_supply_names)];
};

static inline struct ov01a10 *to_ov01a10(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct ov01a10, sd);
}

static int ov01a10_update_digital_gain(struct ov01a10 *ov01a10, u32 d_gain)
{
	u32 real = d_gain << 6;
	int ret = 0;

	cci_write(ov01a10->regmap, OV01A10_REG_DIGITAL_GAIN_B, real, &ret);
	cci_write(ov01a10->regmap, OV01A10_REG_DIGITAL_GAIN_GB, real, &ret);
	cci_write(ov01a10->regmap, OV01A10_REG_DIGITAL_GAIN_GR, real, &ret);
	cci_write(ov01a10->regmap, OV01A10_REG_DIGITAL_GAIN_R, real, &ret);

	return ret;
}

static int ov01a10_test_pattern(struct ov01a10 *ov01a10, u32 pattern)
{
	if (pattern)
		pattern |= OV01A10_TEST_PATTERN_ENABLE;

	return cci_write(ov01a10->regmap, OV01A10_REG_TEST_PATTERN, pattern,
			 NULL);
}

/* for vflip and hflip, use 0x9 as window offset to keep the bayer */
static int ov01a10_set_hflip(struct ov01a10 *ov01a10, u32 hflip)
{
	u32 val, offset;
	int ret = 0;

	offset = hflip ? 0x8 : 0x9;
	val = hflip ? 0 : FIELD_PREP(OV01A10_HFLIP_MASK, 0x1);

	cci_write(ov01a10->regmap, OV01A10_REG_X_WIN, offset, &ret);
	cci_update_bits(ov01a10->regmap, OV01A10_REG_FORMAT1,
			OV01A10_HFLIP_MASK, val, &ret);

	return ret;
}

static int ov01a10_set_vflip(struct ov01a10 *ov01a10, u32 vflip)
{
	u32 val, offset;
	int ret = 0;

	offset = vflip ? 0x9 : 0x8;
	val = vflip ? FIELD_PREP(OV01A10_VFLIP_MASK, 0x1) : 0;

	cci_write(ov01a10->regmap, OV01A10_REG_Y_WIN, offset, &ret);
	cci_update_bits(ov01a10->regmap, OV01A10_REG_FORMAT1,
			OV01A10_VFLIP_MASK, val, &ret);

	return ret;
}

static int ov01a10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov01a10 *ov01a10 = container_of(ctrl->handler,
					       struct ov01a10, ctrl_handler);
	s64 exposure_max;
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		exposure_max = ov01a10->cur_mode->height + ctrl->val -
			OV01A10_EXPOSURE_MAX_MARGIN;
		__v4l2_ctrl_modify_range(ov01a10->exposure,
					 ov01a10->exposure->minimum,
					 exposure_max, ov01a10->exposure->step,
					 exposure_max);
	}

	if (!pm_runtime_get_if_in_use(ov01a10->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(ov01a10->regmap, OV01A10_REG_ANALOG_GAIN,
				ctrl->val, NULL);
		break;

	case V4L2_CID_DIGITAL_GAIN:
		ret = ov01a10_update_digital_gain(ov01a10, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE:
		ret = cci_write(ov01a10->regmap, OV01A10_REG_EXPOSURE,
				ctrl->val, NULL);
		break;

	case V4L2_CID_VBLANK:
		ret = cci_write(ov01a10->regmap, OV01A10_REG_VTS,
				ov01a10->cur_mode->height + ctrl->val, NULL);
		break;

	case V4L2_CID_TEST_PATTERN:
		ret = ov01a10_test_pattern(ov01a10, ctrl->val);
		break;

	case V4L2_CID_HFLIP:
		ov01a10_set_hflip(ov01a10, ctrl->val);
		break;

	case V4L2_CID_VFLIP:
		ov01a10_set_vflip(ov01a10, ctrl->val);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(ov01a10->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov01a10_ctrl_ops = {
	.s_ctrl = ov01a10_set_ctrl,
};

static int ov01a10_init_controls(struct ov01a10 *ov01a10)
{
	struct v4l2_fwnode_device_properties props;
	u32 vblank_min, vblank_max, vblank_default;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	const struct ov01a10_mode *cur_mode;
	s64 exposure_max, h_blank;
	int ret = 0;

	ret = v4l2_fwnode_device_parse(ov01a10->dev, &props);
	if (ret)
		return ret;

	ctrl_hdlr = &ov01a10->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 12);
	if (ret)
		return ret;

	cur_mode = ov01a10->cur_mode;

	ov01a10->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr,
						    &ov01a10_ctrl_ops,
						    V4L2_CID_LINK_FREQ,
						    ov01a10->link_freq_index, 0,
						    link_freq_menu_items);

	ov01a10->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops,
						V4L2_CID_PIXEL_RATE, 0,
						OV01A10_SCLK, 1, OV01A10_SCLK);

	vblank_min = cur_mode->vts_min - cur_mode->height;
	vblank_max = OV01A10_VTS_MAX - cur_mode->height;
	vblank_default = cur_mode->vts_def - cur_mode->height;
	ov01a10->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_min,
					    vblank_max, 1, vblank_default);

	h_blank = cur_mode->hts - cur_mode->width;
	ov01a10->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops,
					    V4L2_CID_HBLANK, h_blank, h_blank,
					    1, h_blank);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV01A10_ANAL_GAIN_MIN, OV01A10_ANAL_GAIN_MAX,
			  OV01A10_ANAL_GAIN_STEP, OV01A10_ANAL_GAIN_MIN);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV01A10_DGTL_GAIN_MIN, OV01A10_DGTL_GAIN_MAX,
			  OV01A10_DGTL_GAIN_STEP, OV01A10_DGTL_GAIN_DEFAULT);

	exposure_max = cur_mode->vts_def - OV01A10_EXPOSURE_MAX_MARGIN;
	ov01a10->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops,
					      V4L2_CID_EXPOSURE,
					      OV01A10_EXPOSURE_MIN,
					      exposure_max,
					      OV01A10_EXPOSURE_STEP,
					      exposure_max);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov01a10_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov01a10_test_pattern_menu) - 1,
				     0, 0, ov01a10_test_pattern_menu);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops, V4L2_CID_HFLIP,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops, V4L2_CID_VFLIP,
			  0, 1, 1, 0);

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &ov01a10_ctrl_ops,
					      &props);
	if (ret)
		goto fail;

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		goto fail;
	}

	ov01a10->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	ov01a10->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ov01a10->sd.ctrl_handler = ctrl_hdlr;

	return 0;
fail:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static void ov01a10_update_pad_format(const struct ov01a10_mode *mode,
				      struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = OV01A10_MEDIA_BUS_FMT;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
}

static int ov01a10_start_streaming(struct ov01a10 *ov01a10)
{
	const struct ov01a10_reg_list *reg_list;
	int link_freq_index;
	int ret = 0;

	link_freq_index = ov01a10->cur_mode->link_freq_index;
	reg_list = &link_freq_configs[link_freq_index].reg_list;
	ret = regmap_multi_reg_write(ov01a10->regmap, reg_list->regs,
				     reg_list->num_of_regs);
	if (ret) {
		dev_err(ov01a10->dev, "failed to set plls\n");
		return ret;
	}

	reg_list = &ov01a10->cur_mode->reg_list;
	ret = regmap_multi_reg_write(ov01a10->regmap, reg_list->regs,
				     reg_list->num_of_regs);
	if (ret) {
		dev_err(ov01a10->dev, "failed to set mode\n");
		return ret;
	}

	ret = __v4l2_ctrl_handler_setup(ov01a10->sd.ctrl_handler);
	if (ret)
		return ret;

	return cci_write(ov01a10->regmap, OV01A10_REG_MODE_SELECT,
			 OV01A10_MODE_STREAMING, NULL);
}

static void ov01a10_stop_streaming(struct ov01a10 *ov01a10)
{
	cci_write(ov01a10->regmap, OV01A10_REG_MODE_SELECT,
		  OV01A10_MODE_STANDBY, NULL);
}

static int ov01a10_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov01a10 *ov01a10 = to_ov01a10(sd);
	struct v4l2_subdev_state *state;
	int ret = 0;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	if (enable) {
		ret = pm_runtime_resume_and_get(ov01a10->dev);
		if (ret < 0)
			goto unlock;

		ret = ov01a10_start_streaming(ov01a10);
		if (ret) {
			pm_runtime_put(ov01a10->dev);
			goto unlock;
		}
	} else {
		ov01a10_stop_streaming(ov01a10);
		pm_runtime_put(ov01a10->dev);
	}

unlock:
	v4l2_subdev_unlock_state(state);

	return ret;
}

static int ov01a10_set_format(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *fmt)
{
	struct ov01a10 *ov01a10 = to_ov01a10(sd);
	const struct ov01a10_mode *mode;
	struct v4l2_mbus_framefmt *format;
	s32 vblank_def, h_blank;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes), width,
				      height, fmt->format.width,
				      fmt->format.height);

	ov01a10_update_pad_format(mode, &fmt->format);

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		ov01a10->cur_mode = mode;

		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov01a10->vblank,
					 mode->vts_min - mode->height,
					 OV01A10_VTS_MAX - mode->height, 1,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(ov01a10->vblank, vblank_def);
		h_blank = mode->hts - mode->width;
		__v4l2_ctrl_modify_range(ov01a10->hblank, h_blank, h_blank, 1,
					 h_blank);
	}

	format = v4l2_subdev_state_get_format(sd_state, fmt->pad);
	*format = fmt->format;

	return 0;
}

static int ov01a10_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.format = {
			.width = OV01A10_DEFAULT_WIDTH,
			.height = OV01A10_DEFAULT_HEIGHT,
		},
	};

	ov01a10_set_format(sd, state, &fmt);

	return 0;
}

static int ov01a10_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = OV01A10_MEDIA_BUS_FMT;

	return 0;
}

static int ov01a10_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes) ||
	    fse->code != OV01A10_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int ov01a10_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_selection *sel)
{
	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = OV01A10_NATIVE_WIDTH;
		sel->r.height = OV01A10_NATIVE_HEIGHT;
		return 0;
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.top = (OV01A10_NATIVE_HEIGHT -
			      OV01A10_DEFAULT_HEIGHT) / 2;
		sel->r.left = (OV01A10_NATIVE_WIDTH -
			       OV01A10_DEFAULT_WIDTH) / 2;
		sel->r.width = OV01A10_DEFAULT_WIDTH;
		sel->r.height = OV01A10_DEFAULT_HEIGHT;
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_subdev_core_ops ov01a10_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
};

static const struct v4l2_subdev_video_ops ov01a10_video_ops = {
	.s_stream = ov01a10_set_stream,
};

static const struct v4l2_subdev_pad_ops ov01a10_pad_ops = {
	.set_fmt = ov01a10_set_format,
	.get_fmt = v4l2_subdev_get_fmt,
	.get_selection = ov01a10_get_selection,
	.enum_mbus_code = ov01a10_enum_mbus_code,
	.enum_frame_size = ov01a10_enum_frame_size,
};

static const struct v4l2_subdev_ops ov01a10_subdev_ops = {
	.core = &ov01a10_core_ops,
	.video = &ov01a10_video_ops,
	.pad = &ov01a10_pad_ops,
};

static const struct v4l2_subdev_internal_ops ov01a10_internal_ops = {
	.init_state = ov01a10_init_state,
};

static const struct media_entity_operations ov01a10_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int ov01a10_get_pm_resources(struct ov01a10 *ov01a10)
{
	unsigned long freq;
	int i, ret;

	ov01a10->clk = devm_v4l2_sensor_clk_get(ov01a10->dev, NULL);
	if (IS_ERR(ov01a10->clk))
		return dev_err_probe(ov01a10->dev, PTR_ERR(ov01a10->clk),
				     "getting clock\n");

	freq = clk_get_rate(ov01a10->clk);
	if (freq != OV01A10_MCLK)
		return dev_err_probe(ov01a10->dev, -EINVAL,
				     "external clock %lu is not supported",
				     freq);

	ov01a10->reset = devm_gpiod_get_optional(ov01a10->dev, "reset",
						 GPIOD_OUT_HIGH);
	if (IS_ERR(ov01a10->reset))
		return dev_err_probe(ov01a10->dev, PTR_ERR(ov01a10->reset),
				     "getting reset gpio\n");

	ov01a10->powerdown = devm_gpiod_get_optional(ov01a10->dev, "powerdown",
						 GPIOD_OUT_HIGH);
	if (IS_ERR(ov01a10->powerdown))
		return dev_err_probe(ov01a10->dev, PTR_ERR(ov01a10->powerdown),
				     "getting powerdown gpio\n");

	for (i = 0; i < ARRAY_SIZE(ov01a10_supply_names); i++)
		ov01a10->supplies[i].supply = ov01a10_supply_names[i];

	ret = devm_regulator_bulk_get(ov01a10->dev,
				      ARRAY_SIZE(ov01a10_supply_names),
				      ov01a10->supplies);
	if (ret)
		return dev_err_probe(ov01a10->dev, ret, "getting regulators\n");

	return 0;
}

static int ov01a10_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov01a10 *ov01a10 = to_ov01a10(sd);
	int ret;

	ret = clk_prepare_enable(ov01a10->clk);
	if (ret) {
		dev_err(dev, "Error enabling clk: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(ov01a10_supply_names),
				    ov01a10->supplies);
	if (ret) {
		dev_err(dev, "Error enabling regulators: %d\n", ret);
		clk_disable_unprepare(ov01a10->clk);
		return ret;
	}

	if (ov01a10->reset || ov01a10->powerdown) {
		/* Assert reset/powerdown for at least 2ms on back to back off-on */
		fsleep(2000);
		gpiod_set_value_cansleep(ov01a10->powerdown, 0);
		gpiod_set_value_cansleep(ov01a10->reset, 0);
		fsleep(20000);
	}

	return 0;
}

static int ov01a10_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov01a10 *ov01a10 = to_ov01a10(sd);

	gpiod_set_value_cansleep(ov01a10->reset, 1);
	gpiod_set_value_cansleep(ov01a10->powerdown, 1);

	regulator_bulk_disable(ARRAY_SIZE(ov01a10_supply_names),
			       ov01a10->supplies);

	clk_disable_unprepare(ov01a10->clk);
	return 0;
}

static int ov01a10_identify_module(struct ov01a10 *ov01a10)
{
	int ret;
	u64 val;

	ret = cci_read(ov01a10->regmap, OV01A10_REG_CHIP_ID, &val, NULL);
	if (ret)
		return ret;

	if (val != OV01A10_CHIP_ID) {
		dev_err(ov01a10->dev, "chip id mismatch: %x!=%llx\n",
			OV01A10_CHIP_ID, val);
		return -EIO;
	}

	return 0;
}

static int ov01a10_check_hwcfg(struct ov01a10 *ov01a10)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	struct fwnode_handle *ep, *fwnode = dev_fwnode(ov01a10->dev);
	unsigned long link_freq_bitmap;
	int ret;

	/*
	 * Sometimes the fwnode graph is initialized by the bridge driver,
	 * wait for this.
	 */
	ep = fwnode_graph_get_endpoint_by_id(fwnode, 0, 0, 0);
	if (!ep)
		return dev_err_probe(ov01a10->dev, -EPROBE_DEFER,
				     "waiting for fwnode graph endpoint\n");

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return dev_err_probe(ov01a10->dev, ret, "parsing endpoint\n");

	ret = v4l2_link_freq_to_bitmap(ov01a10->dev,
				       bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       link_freq_menu_items,
				       ARRAY_SIZE(link_freq_menu_items),
				       &link_freq_bitmap);
	if (ret)
		goto check_hwcfg_error;

	/* v4l2_link_freq_to_bitmap() guarantees at least 1 bit is set */
	ov01a10->link_freq_index = ffs(link_freq_bitmap) - 1;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != OV01A10_DATA_LANES) {
		ret = dev_err_probe(ov01a10->dev, -EINVAL,
				    "number of CSI2 data lanes %u is not supported\n",
				    bus_cfg.bus.mipi_csi2.num_data_lanes);
		goto check_hwcfg_error;
	}

check_hwcfg_error:
	v4l2_fwnode_endpoint_free(&bus_cfg);
	return ret;
}

static void ov01a10_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev)) {
		ov01a10_power_off(&client->dev);
		pm_runtime_set_suspended(&client->dev);
	}
}

static int ov01a10_probe(struct i2c_client *client)
{
	struct ov01a10 *ov01a10;
	int ret;

	ov01a10 = devm_kzalloc(&client->dev, sizeof(*ov01a10), GFP_KERNEL);
	if (!ov01a10)
		return -ENOMEM;

	ov01a10->dev = &client->dev;

	ov01a10->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(ov01a10->regmap))
		return PTR_ERR(ov01a10->regmap);

	v4l2_i2c_subdev_init(&ov01a10->sd, client, &ov01a10_subdev_ops);
	ov01a10->sd.internal_ops = &ov01a10_internal_ops;

	ret = ov01a10_check_hwcfg(ov01a10);
	if (ret)
		return ret;

	ret = ov01a10_get_pm_resources(ov01a10);
	if (ret)
		return ret;

	ret = ov01a10_power_on(&client->dev);
	if (ret)
		return ret;

	ret = ov01a10_identify_module(ov01a10);
	if (ret)
		goto err_power_off;

	ov01a10->cur_mode = &supported_modes[0];

	ret = ov01a10_init_controls(ov01a10);
	if (ret)
		goto err_power_off;

	ov01a10->sd.state_lock = ov01a10->ctrl_handler.lock;
	ov01a10->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov01a10->sd.entity.ops = &ov01a10_subdev_entity_ops;
	ov01a10->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ov01a10->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&ov01a10->sd.entity, 1, &ov01a10->pad);
	if (ret)
		goto err_handler_free;

	ret = v4l2_subdev_init_finalize(&ov01a10->sd);
	if (ret)
		goto err_media_entity_cleanup;

	/*
	 * Device is already turned on by i2c-core with ACPI domain PM.
	 * Enable runtime PM and turn off the device.
	 */
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	ret = v4l2_async_register_subdev_sensor(&ov01a10->sd);
	if (ret)
		goto err_pm_disable;

	return 0;

err_pm_disable:
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	v4l2_subdev_cleanup(&ov01a10->sd);

err_media_entity_cleanup:
	media_entity_cleanup(&ov01a10->sd.entity);

err_handler_free:
	v4l2_ctrl_handler_free(ov01a10->sd.ctrl_handler);

err_power_off:
	ov01a10_power_off(&client->dev);

	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(ov01a10_pm_ops, ov01a10_power_off,
				 ov01a10_power_on, NULL);

#ifdef CONFIG_ACPI
static const struct acpi_device_id ov01a10_acpi_ids[] = {
	{ "OVTI01A0" },
	{ }
};

MODULE_DEVICE_TABLE(acpi, ov01a10_acpi_ids);
#endif

static struct i2c_driver ov01a10_i2c_driver = {
	.driver = {
		.name = "ov01a10",
		.pm = pm_sleep_ptr(&ov01a10_pm_ops),
		.acpi_match_table = ACPI_PTR(ov01a10_acpi_ids),
	},
	.probe = ov01a10_probe,
	.remove = ov01a10_remove,
};

module_i2c_driver(ov01a10_i2c_driver);

MODULE_AUTHOR("Bingbu Cao <bingbu.cao@intel.com>");
MODULE_AUTHOR("Wang Yating <yating.wang@intel.com>");
MODULE_DESCRIPTION("OmniVision OV01A10 sensor driver");
MODULE_LICENSE("GPL");
