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
#define OV01A10_DGTL_GAIN_MAX		0x3fff
#define OV01A10_DGTL_GAIN_STEP		1
#define OV01A10_DGTL_GAIN_DEFAULT	1024

/* timing control */
#define OV01A10_REG_X_ADDR_START	CCI_REG16(0x3800)
#define OV01A10_REG_Y_ADDR_START	CCI_REG16(0x3802)
#define OV01A10_REG_X_ADDR_END		CCI_REG16(0x3804)
#define OV01A10_REG_Y_ADDR_END		CCI_REG16(0x3806)
#define OV01A10_REG_X_OUTPUT_SIZE	CCI_REG16(0x3808)
#define OV01A10_REG_Y_OUTPUT_SIZE	CCI_REG16(0x380a)
#define OV01A10_REG_HTS			CCI_REG16(0x380c) /* in units of 2 pixels */
#define OV01A10_REG_VTS			CCI_REG16(0x380e)
#define OV01A10_REG_X_WIN		CCI_REG16(0x3810)
#define OV01A10_REG_Y_WIN		CCI_REG16(0x3812)

/* flip and mirror control */
#define OV01A10_REG_FORMAT1		CCI_REG8(0x3820)
#define OV01A10_VFLIP_MASK		BIT(4)
#define OV01A10_HFLIP_MASK		BIT(3)

/* test pattern control */
#define OV01A10_REG_TEST_PATTERN	CCI_REG8(0x4503)
#define OV01A10_TEST_PATTERN_ENABLE	BIT(7)

struct ov01a10_link_freq_config {
	const struct reg_sequence *regs;
	int regs_len;
};

static const struct reg_sequence mipi_data_rate_720mbps[] = {
	{0x0103, 0x01},
	{0x0302, 0x00},
	{0x0303, 0x06},
	{0x0304, 0x01},
	{0x0305, 0xf4},
	{0x0306, 0x00},
	{0x0308, 0x01},
	{0x0309, 0x00},
	{0x030c, 0x01},
	{0x0322, 0x01},
	{0x0323, 0x06},
	{0x0324, 0x01},
	{0x0325, 0x68},
};

static const struct reg_sequence ov01a10_global_setting[] = {
	{0x3002, 0xa1},
	{0x301e, 0xf0},
	{0x3022, 0x01},
	{0x3504, 0x0c},
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
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
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
	{
		.regs = mipi_data_rate_720mbps,
		.regs_len = ARRAY_SIZE(mipi_data_rate_720mbps),
	},
};

static const struct v4l2_rect ov01a10_default_crop = {
	.left = (OV01A10_NATIVE_WIDTH - OV01A10_DEFAULT_WIDTH) / 2,
	.top = (OV01A10_NATIVE_HEIGHT - OV01A10_DEFAULT_HEIGHT) / 2,
	.width = OV01A10_DEFAULT_WIDTH,
	.height = OV01A10_DEFAULT_HEIGHT,
};

static const char * const ov01a10_supply_names[] = {
	"dovdd",	/* Digital I/O power */
	"avdd",		/* Analog power */
	"dvdd",		/* Digital core power */
};

struct ov01a10_sensor_cfg {
	const char *model;
	u32 bus_fmt;
	int pattern_size;
	int border_size;
	u8 format1_base_val;
	bool invert_hflip_shift;
	bool invert_vflip_shift;
};

struct ov01a10 {
	struct device *dev;
	struct regmap *regmap;
	const struct ov01a10_sensor_cfg *cfg;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;

	/* v4l2 controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;

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

static struct v4l2_mbus_framefmt *ov01a10_get_active_format(struct ov01a10 *ov01a10)
{
	struct v4l2_subdev_state *active_state =
		v4l2_subdev_get_locked_active_state(&ov01a10->sd);

	return v4l2_subdev_state_get_format(active_state, 0);
}

static struct v4l2_rect *ov01a10_get_active_crop(struct ov01a10 *ov01a10)
{
	struct v4l2_subdev_state *active_state =
		v4l2_subdev_get_locked_active_state(&ov01a10->sd);

	return v4l2_subdev_state_get_crop(active_state, 0);
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

static void ov01a10_set_format1(struct ov01a10 *ov01a10, int *ret)
{
	u8 val = ov01a10->cfg->format1_base_val;

	/* hflip register bit is inverted */
	if (!ov01a10->hflip->val)
		val |= FIELD_PREP(OV01A10_HFLIP_MASK, 0x1);

	if (ov01a10->vflip->val)
		val |= FIELD_PREP(OV01A10_VFLIP_MASK, 0x1);

	cci_write(ov01a10->regmap, OV01A10_REG_FORMAT1, val, ret);
}

static int ov01a10_set_hflip(struct ov01a10 *ov01a10, bool hflip)
{
	struct v4l2_rect *crop = ov01a10_get_active_crop(ov01a10);
	const struct ov01a10_sensor_cfg *cfg = ov01a10->cfg;
	u32 offset;
	int ret = 0;

	offset = crop->left;
	if ((hflip ^ cfg->invert_hflip_shift) && cfg->border_size)
		offset++;

	cci_write(ov01a10->regmap, OV01A10_REG_X_WIN, offset, &ret);
	ov01a10_set_format1(ov01a10, &ret);

	return ret;
}

static int ov01a10_set_vflip(struct ov01a10 *ov01a10, bool vflip)
{
	struct v4l2_rect *crop = ov01a10_get_active_crop(ov01a10);
	const struct ov01a10_sensor_cfg *cfg = ov01a10->cfg;
	u32 offset;
	int ret = 0;

	offset = crop->top;
	if ((vflip ^ cfg->invert_vflip_shift) && cfg->border_size)
		offset++;

	cci_write(ov01a10->regmap, OV01A10_REG_Y_WIN, offset, &ret);
	ov01a10_set_format1(ov01a10, &ret);

	return ret;
}

static int ov01a10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov01a10 *ov01a10 = container_of(ctrl->handler,
					       struct ov01a10, ctrl_handler);
	struct v4l2_mbus_framefmt *fmt = ov01a10_get_active_format(ov01a10);
	s64 exposure_max;
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		exposure_max = fmt->height + ctrl->val -
			       OV01A10_EXPOSURE_MAX_MARGIN;
		__v4l2_ctrl_modify_range(ov01a10->exposure,
					 OV01A10_EXPOSURE_MIN, exposure_max,
					 OV01A10_EXPOSURE_STEP, exposure_max);
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
				fmt->height + ctrl->val, NULL);
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
	s64 exposure_max, h_blank;
	int ret = 0;

	ret = v4l2_fwnode_device_parse(ov01a10->dev, &props);
	if (ret)
		return ret;

	ctrl_hdlr = &ov01a10->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 12);
	if (ret)
		return ret;

	ov01a10->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr,
						    &ov01a10_ctrl_ops,
						    V4L2_CID_LINK_FREQ,
						    ov01a10->link_freq_index, 0,
						    link_freq_menu_items);

	ov01a10->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops,
						V4L2_CID_PIXEL_RATE, 0,
						OV01A10_SCLK, 1, OV01A10_SCLK);

	vblank_min = OV01A10_VTS_MIN - OV01A10_DEFAULT_HEIGHT;
	vblank_max = OV01A10_VTS_MAX - OV01A10_DEFAULT_HEIGHT;
	vblank_default = OV01A10_VTS_DEF - OV01A10_DEFAULT_HEIGHT;
	ov01a10->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_min,
					    vblank_max, 1, vblank_default);

	h_blank = OV01A10_HTS_DEF - OV01A10_DEFAULT_WIDTH;
	ov01a10->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops,
					    V4L2_CID_HBLANK, h_blank, h_blank,
					    1, h_blank);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV01A10_ANAL_GAIN_MIN, OV01A10_ANAL_GAIN_MAX,
			  OV01A10_ANAL_GAIN_STEP, OV01A10_ANAL_GAIN_MIN);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV01A10_DGTL_GAIN_MIN, OV01A10_DGTL_GAIN_MAX,
			  OV01A10_DGTL_GAIN_STEP, OV01A10_DGTL_GAIN_DEFAULT);

	exposure_max = OV01A10_VTS_DEF - OV01A10_EXPOSURE_MAX_MARGIN;
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

	ov01a10->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops,
					   V4L2_CID_HFLIP, 0, 1, 1, 0);
	ov01a10->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops,
					   V4L2_CID_VFLIP, 0, 1, 1, 0);

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

static void ov01a10_fill_format(struct ov01a10 *ov01a10,
				struct v4l2_mbus_framefmt *fmt,
				unsigned int width, unsigned int height)
{
	memset(fmt, 0, sizeof(*fmt));
	fmt->width = width;
	fmt->height = height;
	fmt->code = ov01a10->cfg->bus_fmt;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
}

static int ov01a10_set_mode(struct ov01a10 *ov01a10)
{
	struct v4l2_mbus_framefmt *fmt = ov01a10_get_active_format(ov01a10);
	int ret = 0;

	cci_write(ov01a10->regmap, OV01A10_REG_X_ADDR_START, 0, &ret);
	cci_write(ov01a10->regmap, OV01A10_REG_Y_ADDR_START, 0, &ret);
	cci_write(ov01a10->regmap, OV01A10_REG_X_ADDR_END,
		  OV01A10_NATIVE_WIDTH - 1, &ret);
	cci_write(ov01a10->regmap, OV01A10_REG_Y_ADDR_END,
		  OV01A10_NATIVE_HEIGHT - 1, &ret);
	cci_write(ov01a10->regmap, OV01A10_REG_X_OUTPUT_SIZE,
		  fmt->width, &ret);
	cci_write(ov01a10->regmap, OV01A10_REG_Y_OUTPUT_SIZE,
		  fmt->height, &ret);
	/* HTS register is in units of 2 pixels */
	cci_write(ov01a10->regmap, OV01A10_REG_HTS,
		  OV01A10_HTS_DEF / 2, &ret);
	/* OV01A10_REG_VTS is set by vblank control */
	/* OV01A10_REG_X_WIN is set by hlip control */
	/* OV01A10_REG_Y_WIN is set by vflip control */

	return ret;
}

static int ov01a10_start_streaming(struct ov01a10 *ov01a10)
{
	const struct ov01a10_link_freq_config *freq_cfg;
	int ret;

	freq_cfg = &link_freq_configs[ov01a10->link_freq_index];
	ret = regmap_multi_reg_write(ov01a10->regmap, freq_cfg->regs,
				     freq_cfg->regs_len);
	if (ret) {
		dev_err(ov01a10->dev, "failed to set plls\n");
		return ret;
	}

	ret = regmap_multi_reg_write(ov01a10->regmap, ov01a10_global_setting,
				     ARRAY_SIZE(ov01a10_global_setting));
	if (ret) {
		dev_err(ov01a10->dev, "failed to initialize sensor\n");
		return ret;
	}

	ret = ov01a10_set_mode(ov01a10);
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

static void ov01a10_update_blank_ctrls(struct ov01a10 *ov01a10,
				       unsigned int width, unsigned int height)
{
	s32 hblank, vblank_def;

	vblank_def = OV01A10_VTS_DEF - height;
	__v4l2_ctrl_modify_range(ov01a10->vblank,
				 OV01A10_VTS_MIN - height,
				 OV01A10_VTS_MAX - height, 1,
				 vblank_def);
	__v4l2_ctrl_s_ctrl(ov01a10->vblank, vblank_def);

	hblank = OV01A10_HTS_DEF - width;
	__v4l2_ctrl_modify_range(ov01a10->hblank, hblank, hblank, 1, hblank);
}

static int ov01a10_set_format(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *fmt)
{
	struct v4l2_rect *crop = v4l2_subdev_state_get_crop(sd_state, fmt->pad);
	struct ov01a10 *ov01a10 = to_ov01a10(sd);
	const int pattern_size = ov01a10->cfg->pattern_size;
	const int border_size = ov01a10->cfg->border_size;
	unsigned int width, height;

	width = clamp_val(ALIGN(fmt->format.width, pattern_size),
			  pattern_size,
			  OV01A10_NATIVE_WIDTH - 2 * border_size);
	height = clamp_val(ALIGN(fmt->format.height, pattern_size),
			   pattern_size,
			   OV01A10_NATIVE_HEIGHT - 2 * border_size);

	/* Center image for userspace which does not set the crop first */
	if (width != crop->width || height != crop->height) {
		crop->left = ALIGN((OV01A10_NATIVE_WIDTH - width) / 2,
				   pattern_size);
		crop->top = ALIGN((OV01A10_NATIVE_HEIGHT - height) / 2,
				  pattern_size);
		crop->width = width;
		crop->height = height;
	}

	ov01a10_fill_format(ov01a10, &fmt->format, width, height);
	*v4l2_subdev_state_get_format(sd_state, fmt->pad) = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		ov01a10_update_blank_ctrls(ov01a10, width, height);

	return 0;
}

static int ov01a10_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state)
{
	struct ov01a10 *ov01a10 = to_ov01a10(sd);

	*v4l2_subdev_state_get_crop(sd_state, 0) = ov01a10_default_crop;
	ov01a10_fill_format(ov01a10, v4l2_subdev_state_get_format(sd_state, 0),
			    OV01A10_DEFAULT_WIDTH, OV01A10_DEFAULT_HEIGHT);

	return 0;
}

static int ov01a10_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct ov01a10 *ov01a10 = to_ov01a10(sd);

	if (code->index > 0)
		return -EINVAL;

	code->code = ov01a10->cfg->bus_fmt;

	return 0;
}

static int ov01a10_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct ov01a10 *ov01a10 = to_ov01a10(sd);
	const int pattern_size = ov01a10->cfg->pattern_size;
	const int border_size = ov01a10->cfg->border_size;

	if (fse->index)
		return -EINVAL;

	fse->min_width = pattern_size;
	fse->max_width = OV01A10_NATIVE_WIDTH - 2 * border_size;
	fse->min_height = pattern_size;
	fse->max_height = OV01A10_NATIVE_HEIGHT - 2 * border_size;

	return 0;
}

static int ov01a10_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_selection *sel)
{
	struct ov01a10 *ov01a10 = to_ov01a10(sd);
	const int border_size = ov01a10->cfg->border_size;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(state, sel->pad);
		return 0;
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r = ov01a10_default_crop;
		return 0;
	case V4L2_SEL_TGT_CROP_BOUNDS:
		/* Keep a border for hvflip shift to preserve bayer-pattern */
		sel->r.left = border_size;
		sel->r.top = border_size;
		sel->r.width = OV01A10_NATIVE_WIDTH - 2 * border_size;
		sel->r.height = OV01A10_NATIVE_HEIGHT - 2 * border_size;
		return 0;
	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = OV01A10_NATIVE_WIDTH;
		sel->r.height = OV01A10_NATIVE_HEIGHT;
		return 0;
	}

	return -EINVAL;
}

static int ov01a10_set_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_selection *sel)
{
	struct ov01a10 *ov01a10 = to_ov01a10(sd);
	const int pattern_size = ov01a10->cfg->pattern_size;
	const int border_size = ov01a10->cfg->border_size;
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;
	struct v4l2_rect rect;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	/*
	 * Clamp the boundaries of the crop rectangle to the size of the sensor
	 * pixel array. Align to pattern-size to ensure pattern isn't disrupted.
	 */
	rect.left = clamp_val(ALIGN(sel->r.left, pattern_size), border_size,
			      OV01A10_NATIVE_WIDTH - 2 * border_size);
	rect.top = clamp_val(ALIGN(sel->r.top, pattern_size), border_size,
			     OV01A10_NATIVE_HEIGHT - 2 * border_size);
	rect.width = clamp_val(ALIGN(sel->r.width, pattern_size), pattern_size,
			       OV01A10_NATIVE_WIDTH - rect.left - border_size);
	rect.height = clamp_val(ALIGN(sel->r.height, pattern_size), pattern_size,
				OV01A10_NATIVE_HEIGHT - rect.top - border_size);

	crop = v4l2_subdev_state_get_crop(sd_state, sel->pad);

	/* Reset the output size if the crop rectangle size has changed */
	if (rect.width != crop->width || rect.height != crop->height) {
		format = v4l2_subdev_state_get_format(sd_state, sel->pad);
		format->width = rect.width;
		format->height = rect.height;

		if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE)
			ov01a10_update_blank_ctrls(ov01a10, rect.width,
						   rect.height);
	}

	*crop = rect;
	sel->r = rect;

	return 0;
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
	.set_selection = ov01a10_set_selection,
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
	const struct ov01a10_sensor_cfg *cfg;
	struct ov01a10 *ov01a10;
	int ret;

	cfg = device_get_match_data(&client->dev);
	if (!cfg)
		return -EINVAL;

	ov01a10 = devm_kzalloc(&client->dev, sizeof(*ov01a10), GFP_KERNEL);
	if (!ov01a10)
		return -ENOMEM;

	ov01a10->dev = &client->dev;
	ov01a10->cfg = cfg;

	ov01a10->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(ov01a10->regmap))
		return PTR_ERR(ov01a10->regmap);

	v4l2_i2c_subdev_init(&ov01a10->sd, client, &ov01a10_subdev_ops);
	/* Override driver->name with actual sensor model */
	v4l2_i2c_subdev_set_name(&ov01a10->sd, client, cfg->model, NULL);
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
/*
 * The native ov01a10 bayer-pattern is GBRG, but there was a driver bug enabling
 * hflip/mirroring by default resulting in BGGR. Because of this bug Intel's
 * proprietary IPU6 userspace stack expects BGGR. So we report BGGR to not break
 * userspace and fix things up by shifting the crop window-x coordinate by 1
 * when hflip is *disabled*.
 */
static const struct ov01a10_sensor_cfg ov01a10_cfg = {
	.model = "ov01a10",
	.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
	.pattern_size = 2, /* 2x2 */
	.border_size = 2,
	.format1_base_val = 0xa0,
	.invert_hflip_shift = true,
	.invert_vflip_shift = false,
};

static const struct ov01a10_sensor_cfg ov01a1b_cfg = {
	.model = "ov01a1b",
	.bus_fmt = MEDIA_BUS_FMT_Y10_1X10,
	.pattern_size = 2, /* Keep coordinates aligned to a multiple of 2 */
	.border_size = 0,
	.format1_base_val = 0xa0,
};

static const struct acpi_device_id ov01a10_acpi_ids[] = {
	{ "OVTI01A0", (uintptr_t)&ov01a10_cfg },
	{ "OVTI01AB", (uintptr_t)&ov01a1b_cfg },
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
