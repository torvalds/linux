// SPDX-License-Identifier: GPL-2.0-only
/*
 * V4L2 Support for the IMX576
 *
 * Copyright (C) 2026 Silicon Signals Pvt. Ltd.
 *
 * Copyright (C) 2024 Luca Weiss <luca.weiss@fairphone.com>
 */

#include <linux/array_size.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/device/devres.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/units.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>

#include "ccs/ccs-regs.h"
#include "ccs-pll.h"

#define IMX576_INCLK_RATE		(24 * HZ_PER_MHZ)

#define IMX576_CHIP_ID			0x0576

#define IMX576_EXPOSURE_MIN		8
#define IMX576_EXPOSURE_OFFSET		22
#define IMX576_EXPOSURE_STEP		1
#define IMX576_EXPOSURE_DEFAULT		0x0648

#define IMX576_ANA_GAIN_MIN		0
#define IMX576_ANA_GAIN_MAX		978
#define IMX576_ANA_GAIN_STEP		1
#define IMX576_ANA_GAIN_DEFAULT		0

#define IMX576_LINE_LENGTH		6144
#define IMX576_VBLANK_DEF		4387

/* FIXME: Exact VBLANK limit unknown (no datasheet). */
#define IMX576_VBLANK_MAX		32420

#define IMX576_PIXEL_RATE		813600000
#define IMX576_NUM_DATA_LANES		4

/* IMX576 native and active pixel array size */
static const struct v4l2_rect imx576_native_area = {
	.top = 0,
	.left = 0,
	.width = 5792,
	.height = 4464,
};

static const struct v4l2_rect imx576_active_area = {
	.top = 136,
	.left = 16,
	.width = 5760,
	.height = 4312,
};

static const char * const imx576_supply_names[] = {
	"vana",		/* Analog Power */
	"vif",		/* Interface Power */
	"vdig",		/* Digital Power */
};

static const struct cci_reg_sequence imx576_common_regs[] = {
	{ CCS_R_EXTCLK_FREQUENCY_MHZ, 0x1800 },
	{ CCI_REG8(0x3c7e), 0x05 },
	{ CCI_REG8(0x3c7f), 0x07 },
	{ CCI_REG8(0x380d), 0x80 },
	{ CCI_REG8(0x3c00), 0x1a },
	{ CCI_REG8(0x3c01), 0x1a },
	{ CCI_REG8(0x3c02), 0x1a },
	{ CCI_REG8(0x3c03), 0x1a },
	{ CCI_REG8(0x3c04), 0x1a },
	{ CCI_REG8(0x3c05), 0x01 },
	{ CCI_REG8(0x3c08), 0xff },
	{ CCI_REG8(0x3c09), 0xff },
	{ CCI_REG8(0x3c0a), 0x01 },
	{ CCI_REG8(0x3c0d), 0xff },
	{ CCI_REG8(0x3c0e), 0xff },
	{ CCI_REG8(0x3c0f), 0x20 },
	{ CCI_REG8(0x3f89), 0x01 },
	{ CCI_REG8(0x4b8e), 0x18 },
	{ CCI_REG8(0x4b8f), 0x10 },
	{ CCI_REG8(0x4ba8), 0x08 },
	{ CCI_REG8(0x4baa), 0x08 },
	{ CCI_REG8(0x4bab), 0x08 },
	{ CCI_REG8(0x4bc9), 0x10 },
	{ CCI_REG8(0x5511), 0x01 },
	{ CCI_REG8(0x560b), 0x5b },
	{ CCI_REG8(0x56a7), 0x60 },
	{ CCI_REG8(0x5b3b), 0x60 },
	{ CCI_REG8(0x5ba7), 0x60 },
	{ CCI_REG8(0x6002), 0x00 },
	{ CCI_REG8(0x6014), 0x01 },
	{ CCI_REG8(0x6118), 0x0a },
	{ CCI_REG8(0x6122), 0x0a },
	{ CCI_REG8(0x6128), 0x0a },
	{ CCI_REG8(0x6132), 0x0a },
	{ CCI_REG8(0x6138), 0x0a },
	{ CCI_REG8(0x6142), 0x0a },
	{ CCI_REG8(0x6148), 0x0a },
	{ CCI_REG8(0x6152), 0x0a },
	{ CCI_REG8(0x617b), 0x04 },
	{ CCI_REG8(0x617e), 0x04 },
	{ CCI_REG8(0x6187), 0x04 },
	{ CCI_REG8(0x618a), 0x04 },
	{ CCI_REG8(0x6193), 0x04 },
	{ CCI_REG8(0x6196), 0x04 },
	{ CCI_REG8(0x619f), 0x04 },
	{ CCI_REG8(0x61a2), 0x04 },
	{ CCI_REG8(0x61ab), 0x04 },
	{ CCI_REG8(0x61ae), 0x04 },
	{ CCI_REG8(0x61b7), 0x04 },
	{ CCI_REG8(0x61ba), 0x04 },
	{ CCI_REG8(0x61c3), 0x04 },
	{ CCI_REG8(0x61c6), 0x04 },
	{ CCI_REG8(0x61cf), 0x04 },
	{ CCI_REG8(0x61d2), 0x04 },
	{ CCI_REG8(0x61db), 0x04 },
	{ CCI_REG8(0x61de), 0x04 },
	{ CCI_REG8(0x61e7), 0x04 },
	{ CCI_REG8(0x61ea), 0x04 },
	{ CCI_REG8(0x61f3), 0x04 },
	{ CCI_REG8(0x61f6), 0x04 },
	{ CCI_REG8(0x61ff), 0x04 },
	{ CCI_REG8(0x6202), 0x04 },
	{ CCI_REG8(0x620b), 0x04 },
	{ CCI_REG8(0x620e), 0x04 },
	{ CCI_REG8(0x6217), 0x04 },
	{ CCI_REG8(0x621a), 0x04 },
	{ CCI_REG8(0x6223), 0x04 },
	{ CCI_REG8(0x6226), 0x04 },
	{ CCI_REG8(0x6b0b), 0x02 },
	{ CCI_REG8(0x6b0c), 0x01 },
	{ CCI_REG8(0x6b0d), 0x05 },
	{ CCI_REG8(0x6b0f), 0x04 },
	{ CCI_REG8(0x6b10), 0x02 },
	{ CCI_REG8(0x6b11), 0x06 },
	{ CCI_REG8(0x6b12), 0x03 },
	{ CCI_REG8(0x6b13), 0x07 },
	{ CCI_REG8(0x6b14), 0x0d },
	{ CCI_REG8(0x6b15), 0x09 },
	{ CCI_REG8(0x6b16), 0x0c },
	{ CCI_REG8(0x6b17), 0x08 },
	{ CCI_REG8(0x6b18), 0x0e },
	{ CCI_REG8(0x6b19), 0x0a },
	{ CCI_REG8(0x6b1a), 0x0f },
	{ CCI_REG8(0x6b1b), 0x0b },
	{ CCI_REG8(0x6b1c), 0x01 },
	{ CCI_REG8(0x6b1d), 0x05 },
	{ CCI_REG8(0x6b1f), 0x04 },
	{ CCI_REG8(0x6b20), 0x02 },
	{ CCI_REG8(0x6b21), 0x06 },
	{ CCI_REG8(0x6b22), 0x03 },
	{ CCI_REG8(0x6b23), 0x07 },
	{ CCI_REG8(0x6b24), 0x0d },
	{ CCI_REG8(0x6b25), 0x09 },
	{ CCI_REG8(0x6b26), 0x0c },
	{ CCI_REG8(0x6b27), 0x08 },
	{ CCI_REG8(0x6b28), 0x0e },
	{ CCI_REG8(0x6b29), 0x0a },
	{ CCI_REG8(0x6b2a), 0x0f },
	{ CCI_REG8(0x6b2b), 0x0b },
	{ CCI_REG8(0x7948), 0x01 },
	{ CCI_REG8(0x7949), 0x06 },
	{ CCI_REG8(0x794b), 0x04 },
	{ CCI_REG8(0x794c), 0x04 },
	{ CCI_REG8(0x794d), 0x3a },
	{ CCI_REG8(0x7951), 0x00 },
	{ CCI_REG8(0x7952), 0x01 },
	{ CCI_REG8(0x7955), 0x00 },
	{ CCI_REG8(0x9004), 0x10 },
	{ CCI_REG8(0x9200), 0xa0 },
	{ CCI_REG8(0x9201), 0xa7 },
	{ CCI_REG8(0x9202), 0xa0 },
	{ CCI_REG8(0x9203), 0xaa },
	{ CCI_REG8(0x9204), 0xa0 },
	{ CCI_REG8(0x9205), 0xad },
	{ CCI_REG8(0x9206), 0xa0 },
	{ CCI_REG8(0x9207), 0xb0 },
	{ CCI_REG8(0x9208), 0xa0 },
	{ CCI_REG8(0x9209), 0xb3 },
	{ CCI_REG8(0x920a), 0xb7 },
	{ CCI_REG8(0x920b), 0x34 },
	{ CCI_REG8(0x920c), 0xb7 },
	{ CCI_REG8(0x920d), 0x36 },
	{ CCI_REG8(0x920e), 0xb7 },
	{ CCI_REG8(0x920f), 0x37 },
	{ CCI_REG8(0x9210), 0xb7 },
	{ CCI_REG8(0x9211), 0x38 },
	{ CCI_REG8(0x9212), 0xb7 },
	{ CCI_REG8(0x9213), 0x39 },
	{ CCI_REG8(0x9214), 0xb7 },
	{ CCI_REG8(0x9215), 0x3a },
	{ CCI_REG8(0x9216), 0xb7 },
	{ CCI_REG8(0x9217), 0x3c },
	{ CCI_REG8(0x9218), 0xb7 },
	{ CCI_REG8(0x9219), 0x3d },
	{ CCI_REG8(0x921a), 0xb7 },
	{ CCI_REG8(0x921b), 0x3e },
	{ CCI_REG8(0x921c), 0xb7 },
	{ CCI_REG8(0x921d), 0x3f },
	{ CCI_REG8(0x921e), 0x7f },
	{ CCI_REG8(0x921f), 0x77 },
	{ CCI_REG8(0x99af), 0x0f },
	{ CCI_REG8(0x99b0), 0x0f },
	{ CCI_REG8(0x99b1), 0x0f },
	{ CCI_REG8(0x99b2), 0x0f },
	{ CCI_REG8(0x99b3), 0x0f },
	{ CCI_REG8(0x99e1), 0x0f },
	{ CCI_REG8(0x99e2), 0x0f },
	{ CCI_REG8(0x99e3), 0x0f },
	{ CCI_REG8(0x99e4), 0x0f },
	{ CCI_REG8(0x99e5), 0x0f },
	{ CCI_REG8(0x99e6), 0x0f },
	{ CCI_REG8(0x99e7), 0x0f },
	{ CCI_REG8(0x99e8), 0x0f },
	{ CCI_REG8(0x99e9), 0x0f },
	{ CCI_REG8(0x99ea), 0x0f },
	{ CCI_REG8(0xe286), 0x31 },
	{ CCI_REG8(0xe2a6), 0x32 },
	{ CCI_REG8(0xe2c6), 0x33 },
	{ CCI_REG8(0x4038), 0x00 },
	{ CCI_REG8(0x9856), 0xa0 },
	{ CCI_REG8(0x9857), 0x78 },
	{ CCI_REG8(0x9858), 0x64 },
	{ CCI_REG8(0x986e), 0x64 },
	{ CCI_REG8(0x9870), 0x3c },
	{ CCI_REG8(0x993a), 0x0e },
	{ CCI_REG8(0x993b), 0x0e },
	{ CCI_REG8(0x9953), 0x08 },
	{ CCI_REG8(0x9954), 0x08 },
	{ CCI_REG8(0x996b), 0x0f },
	{ CCI_REG8(0x996d), 0x0f },
	{ CCI_REG8(0x996f), 0x0f },
	{ CCI_REG8(0x998e), 0x0f },
	{ CCI_REG8(0xa101), 0x01 },
	{ CCI_REG8(0xa103), 0x01 },
	{ CCI_REG8(0xa105), 0x01 },
	{ CCI_REG8(0xa107), 0x01 },
	{ CCI_REG8(0xa109), 0x01 },
	{ CCI_REG8(0xa10b), 0x01 },
	{ CCI_REG8(0xa10d), 0x01 },
	{ CCI_REG8(0xa10f), 0x01 },
	{ CCI_REG8(0xa111), 0x01 },
	{ CCI_REG8(0xa113), 0x01 },
	{ CCI_REG8(0xa115), 0x01 },
	{ CCI_REG8(0xa117), 0x01 },
	{ CCI_REG8(0xa119), 0x01 },
	{ CCI_REG8(0xa11b), 0x01 },
	{ CCI_REG8(0xa11d), 0x01 },
	{ CCI_REG8(0xaa58), 0x00 },
	{ CCI_REG8(0xaa59), 0x01 },
	{ CCI_REG8(0xab03), 0x10 },
	{ CCI_REG8(0xab04), 0x10 },
	{ CCI_REG8(0xab05), 0x10 },
	{ CCI_REG8(0xad6a), 0x03 },
	{ CCI_REG8(0xad6b), 0xff },
	{ CCI_REG8(0xad77), 0x00 },
	{ CCI_REG8(0xad82), 0x03 },
	{ CCI_REG8(0xad83), 0xff },
	{ CCI_REG8(0xae06), 0x04 },
	{ CCI_REG8(0xae07), 0x16 },
	{ CCI_REG8(0xae08), 0xff },
	{ CCI_REG8(0xae09), 0x04 },
	{ CCI_REG8(0xae0a), 0x16 },
	{ CCI_REG8(0xae0b), 0xff },
	{ CCI_REG8(0xaf01), 0x04 },
	{ CCI_REG8(0xaf03), 0x0a },
	{ CCI_REG8(0xaf05), 0x18 },
	{ CCI_REG8(0xb048), 0x0a },
	{ CCS_R_CSI_DATA_FORMAT, 0x0a0a },
	{ CCS_R_CSI_LANE_MODE, 0x03 },
	{ CCS_R_HDR_MODE, 0x00 },
	{ CCI_REG8(0x3140), 0x00 },
	{ CCI_REG8(0x3246), 0x01 },
	{ CCI_REG8(0x3247), 0x01 },
	{ CCI_REG8(0x0401), 0x00 },
	{ CCS_R_SCALE_M, 0x0010 },
	{ CCS_R_SINGLE_DEFECT_CORRECT_EN, CCS_SINGLE_DEFECT_CORRECT_EN_ENABLE },
	{ CCI_REG8(0x3620), 0x01 },
	{ CCI_REG8(0x3f0c), 0x00 },
};

struct imx576 {
	struct device *dev;
	struct regmap *regmap;
	struct ccs_pll pll;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct gpio_desc *reset_gpio;
	struct clk *inclk;
	struct regulator_bulk_data supplies[ARRAY_SIZE(imx576_supply_names)];

	/* V4L2 Controls */
	struct v4l2_ctrl_handler handler;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *gain;

	u32 link_freq_index;
	u32 inclk_freq;
	unsigned long link_freq_bitmap;
};

struct imx576_reg_list {
	u32 num_of_regs;
	const struct cci_reg_sequence *regs;
};

enum {
	IMX576_LINK_FREQ_1011MHZ,
	IMX576_LINK_FREQ_1014MHZ,
	IMX576_LINK_FREQ_1017MHZ,
};

static const s64 link_freqs[] = {
	[IMX576_LINK_FREQ_1011MHZ] = 1011000000,
	[IMX576_LINK_FREQ_1014MHZ] = 1014000000,
	[IMX576_LINK_FREQ_1017MHZ] = 1017000000,
};

static const u32 imx576_mbus_codes[] = {
	MEDIA_BUS_FMT_SRGGB10_1X10,
};

static inline struct imx576 *to_imx576(struct v4l2_subdev *sd)
{
	return container_of_const(sd, struct imx576, sd);
}

static int imx576_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx576 *imx576 = container_of_const(ctrl->handler,
						   struct imx576, handler);
	struct v4l2_subdev_state *state;
	struct v4l2_mbus_framefmt *fmt;
	int ret = 0;

	state = v4l2_subdev_get_locked_active_state(&imx576->sd);
	fmt = v4l2_subdev_state_get_format(state, 0);

	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Honour the VBLANK limits when setting exposure */
		ret = __v4l2_ctrl_modify_range(imx576->exposure,
					       IMX576_EXPOSURE_MIN,
					       fmt->height + ctrl->val -
					       IMX576_EXPOSURE_OFFSET,
					       IMX576_EXPOSURE_STEP,
					       IMX576_EXPOSURE_DEFAULT);
		if (ret)
			return ret;
	}

	if (pm_runtime_get_if_active(imx576->dev) == 0)
		return 0;

	cci_write(imx576->regmap, CCS_R_GROUPED_PARAMETER_HOLD, 1, &ret);

	switch (ctrl->id) {
	case V4L2_CID_VBLANK: {
		u64 vmax = fmt->height + ctrl->val;

		cci_write(imx576->regmap, CCS_R_FRAME_LENGTH_LINES, vmax, &ret);
		break;
	}
	case V4L2_CID_EXPOSURE:
		cci_write(imx576->regmap, CCS_R_COARSE_INTEGRATION_TIME, ctrl->val, &ret);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		cci_write(imx576->regmap, CCS_R_ANALOG_GAIN_CODE_GLOBAL,
			  ctrl->val, &ret);
		break;
	default:
		dev_err(imx576->dev, "Invalid control %d\n", ctrl->id);
		ret = -EINVAL;
		break;
	}

	cci_write(imx576->regmap, CCS_R_GROUPED_PARAMETER_HOLD, 0, &ret);

	pm_runtime_put(imx576->dev);

	return ret;
}

static int imx576_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(imx576_mbus_codes))
		return -EINVAL;

	code->code = imx576_mbus_codes[code->index];

	return 0;
}

static int imx576_set_pad_format(struct v4l2_subdev *sd,
				 const struct v4l2_subdev_client_info *ci,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct imx576 *imx576 = to_imx576(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;
	int ret;

	fmt->format.width = imx576_active_area.width;
	fmt->format.height = imx576_active_area.height;
	fmt->format.code = imx576_mbus_codes[0];
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_RAW;
	fmt->format.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->format.quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt->format.xfer_func = V4L2_XFER_FUNC_NONE;

	format = v4l2_subdev_state_get_format(sd_state, 0);

	*format = fmt->format;

	crop = v4l2_subdev_state_get_crop(sd_state, 0);
	*crop = imx576_active_area;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		u32 vblank_def = IMX576_VBLANK_DEF - imx576_active_area.height;

		ret = __v4l2_ctrl_modify_range(imx576->vblank, vblank_def,
					       IMX576_VBLANK_MAX, 1, vblank_def);
		if (ret)
			return ret;
	}

	return 0;
}

static int imx576_get_selection(struct v4l2_subdev *sd,
				const struct v4l2_subdev_client_info *ci,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r = imx576_native_area;
		return 0;
	case V4L2_SEL_TGT_CROP: {
		struct v4l2_rect *crop;

		crop = v4l2_subdev_state_get_crop(sd_state, sel->pad);
		sel->r = *crop;

		return 0;
	}
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r = imx576_active_area;
		return 0;
	default:
		return -EINVAL;
	}
}

static int imx576_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fsize)
{
	if (fsize->index > 0)
		return -EINVAL;

	if (fsize->code != imx576_mbus_codes[0])
		return -EINVAL;

	fsize->min_width = imx576_active_area.width;
	fsize->max_width = fsize->min_width;
	fsize->min_height = imx576_active_area.height;
	fsize->max_height = fsize->min_height;

	return 0;
}

static int imx576_set_window(struct imx576 *imx576,
			     struct v4l2_subdev_state *state)
{
	const struct v4l2_rect *crop;
	const struct v4l2_mbus_framefmt *format;
	int ret = 0;
	s32 x_start, y_start;

	crop = v4l2_subdev_state_get_crop(state, 0);
	format = v4l2_subdev_state_get_format(state, 0);

	/* Line length */
	cci_write(imx576->regmap, CCS_R_LINE_LENGTH_PCK, IMX576_LINE_LENGTH,
		  &ret);

	/* Imaging area */
	x_start = crop->left - imx576_active_area.left;
	y_start = crop->top - imx576_active_area.top;
	cci_write(imx576->regmap, CCS_R_X_ADDR_START, x_start, &ret);
	cci_write(imx576->regmap, CCS_R_Y_ADDR_START, y_start, &ret);
	cci_write(imx576->regmap, CCS_R_X_ADDR_END, x_start + crop->width - 1,
		  &ret);
	cci_write(imx576->regmap, CCS_R_Y_ADDR_END, y_start + crop->height - 1,
		  &ret);

	/* Binning */
	cci_write(imx576->regmap, CCS_R_BINNING_MODE, 0x00, &ret);
	cci_write(imx576->regmap, CCS_R_BINNING_TYPE, 0x11, &ret);
	cci_write(imx576->regmap, CCS_R_BINNING_WEIGHTING, 0x0a, &ret);

	/* Digital crop */
	cci_write(imx576->regmap, CCS_R_DIGITAL_CROP_X_OFFSET, 0, &ret);
	cci_write(imx576->regmap, CCS_R_DIGITAL_CROP_Y_OFFSET, 0, &ret);
	cci_write(imx576->regmap, CCS_R_DIGITAL_CROP_IMAGE_WIDTH, format->width, &ret);
	cci_write(imx576->regmap, CCS_R_DIGITAL_CROP_IMAGE_HEIGHT, format->height, &ret);

	/* Output size */
	cci_write(imx576->regmap, CCS_R_X_OUTPUT_SIZE, format->width, &ret);
	cci_write(imx576->regmap, CCS_R_Y_OUTPUT_SIZE, format->height, &ret);

	return ret;
}

static int imx576_pll_calculate(struct imx576 *imx576, u64 link_freq)
{
	struct i2c_client *client = v4l2_get_subdevdata(&imx576->sd);
	struct ccs_pll *pll = &imx576->pll;
	static const struct ccs_pll_limits imx576_pll_limits = {
		.min_ext_clk_freq_hz = 6000000,
		.max_ext_clk_freq_hz = 27000000,

		.vt_fr = {
			.min_pre_pll_clk_div = 1,
			.max_pre_pll_clk_div = 4,
			.min_pll_ip_clk_freq_hz = 6000000,
			.max_pll_ip_clk_freq_hz = 12000000,
			.min_pll_multiplier = 87,
			.max_pll_multiplier = 350,
			.min_pll_op_clk_freq_hz = 1050000000,
			.max_pll_op_clk_freq_hz = 2100000000,
		},

		.vt_bk = {
			.min_sys_clk_div = 2,
			.max_sys_clk_div = 4,
			.min_sys_clk_freq_hz = 262500000,
			.max_sys_clk_freq_hz = 1050000000,
			/* divisior is Fixed to 5 */
			.min_pix_clk_div = 5,
			.max_pix_clk_div = 5,
			.min_pix_clk_freq_hz = 52500000,
			.max_pix_clk_freq_hz = 210000000,
		},
		.op_fr = {
			.min_pre_pll_clk_div = 1,
			.max_pre_pll_clk_div = 15,
			.min_pll_ip_clk_freq_hz = 6000000,
			.max_pll_ip_clk_freq_hz = 12000000,
			.min_pll_multiplier = 47,
			.max_pll_multiplier = 2300,
			.min_pll_op_clk_freq_hz = 1250000000,
			.max_pll_op_clk_freq_hz = 2300000000,
		},
		.op_bk = {
			.min_sys_clk_div = 1,
			.max_sys_clk_div = 2,
			.min_sys_clk_freq_hz = 625000000,
			.max_sys_clk_freq_hz = 2300000000,
			/* (RAW8 and RAW10) */
			.min_pix_clk_div = 8,
			.max_pix_clk_div = 10,
			.min_pix_clk_freq_hz = 62500000,
			.max_pix_clk_freq_hz = 287500000,
		},

		.min_line_length_pck_bin = IMX576_LINE_LENGTH,
		.min_line_length_pck = IMX576_LINE_LENGTH,
	};

	memset(pll, 0, sizeof(*pll));

	pll->bus_type = CCS_PLL_BUS_TYPE_CSI2_DPHY;
	pll->op_lanes = IMX576_NUM_DATA_LANES;
	pll->csi2.lanes = IMX576_NUM_DATA_LANES;
	pll->vt_lanes = 4;
	pll->binning_horizontal = 1;
	pll->binning_vertical = 1;
	pll->scale_m = 1;
	pll->bits_per_pixel = 10;
	pll->flags = CCS_PLL_FLAG_LANE_SPEED_MODEL | CCS_PLL_FLAG_DUAL_PLL;
	pll->link_freq = link_freq;
	pll->ext_clk_freq_hz = imx576->inclk_freq;
	pll->pixel_rate_pixel_array = IMX576_PIXEL_RATE;

	return ccs_pll_calculate(&client->dev, &imx576_pll_limits, pll);
}

static int imx576_set_pll(struct imx576 *imx576)
{
	int ret = 0;

	cci_write(imx576->regmap, CCS_R_VT_PIX_CLK_DIV,
		  imx576->pll.vt_bk.pix_clk_div, &ret);
	cci_write(imx576->regmap, CCS_R_VT_SYS_CLK_DIV,
		  imx576->pll.vt_bk.sys_clk_div, &ret);
	cci_write(imx576->regmap, CCS_R_PRE_PLL_CLK_DIV,
		  imx576->pll.vt_fr.pre_pll_clk_div, &ret);
	cci_write(imx576->regmap, CCS_R_PLL_MULTIPLIER,
		  imx576->pll.vt_fr.pll_multiplier, &ret);
	cci_write(imx576->regmap, CCS_R_OP_PIX_CLK_DIV,
		  imx576->pll.op_bk.pix_clk_div, &ret);
	cci_write(imx576->regmap, CCS_R_OP_SYS_CLK_DIV,
		  imx576->pll.op_bk.sys_clk_div, &ret);
	cci_write(imx576->regmap, CCS_R_OP_PRE_PLL_CLK_DIV,
		  imx576->pll.op_fr.pre_pll_clk_div, &ret);
	cci_write(imx576->regmap, CCS_R_OP_PLL_MULTIPLIER,
		  imx576->pll.op_fr.pll_multiplier, &ret);
	cci_write(imx576->regmap, CCS_R_PLL_MODE, CCS_PLL_MODE_DUAL,
		  &ret);

	return ret;
}

static int imx576_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 u32 pad, u64 streams_mask)
{
	struct imx576 *imx576 = to_imx576(sd);
	int ret;

	ret = pm_runtime_resume_and_get(imx576->dev);
	if (ret < 0)
		return ret;

	/* Write common registers */
	ret = cci_multi_reg_write(imx576->regmap, imx576_common_regs,
				  ARRAY_SIZE(imx576_common_regs), NULL);
	if (ret) {
		dev_err(imx576->dev, "failed to write common registers\n");
		goto err_rpm_put;
	}

	/* Set window registers */
	ret = imx576_set_window(imx576, state);
	if (ret) {
		dev_err(imx576->dev, "failed to program window\n");
		goto err_rpm_put;
	}

	/* Set link frequency from PLL calculation */
	ret = imx576_set_pll(imx576);
	if (ret) {
		dev_err(imx576->dev, "failed to configure PLL\n");
		goto err_rpm_put;
	}

	ret =  __v4l2_ctrl_handler_setup(imx576->sd.ctrl_handler);
	if (ret) {
		dev_err(imx576->dev, "fail to setup handler\n");
		goto err_rpm_put;
	}

	/* T7: delay before sending stream command */
	usleep_range(8000, 9000);

	/* Start streaming */
	ret = cci_write(imx576->regmap, CCS_R_MODE_SELECT,
			CCS_MODE_SELECT_STREAMING, NULL);
	if (ret) {
		dev_err(imx576->dev, "fail to start streaming\n");
		goto err_rpm_put;
	}

	return 0;

err_rpm_put:
	pm_runtime_put(imx576->dev);

	return ret;
}

static int imx576_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  u32 pad, u64 streams_mask)
{
	struct imx576 *imx576 = to_imx576(sd);
	int ret;

	ret = cci_write(imx576->regmap, CCS_R_MODE_SELECT,
			CCS_MODE_SELECT_SOFTWARE_STANDBY, NULL);
	if (ret)
		dev_err(imx576->dev, "failed to set stream off\n");

	pm_runtime_put(imx576->dev);

	return 0;
}

static int imx576_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.pad = 0,
		.format = {
			.code = imx576_mbus_codes[0],
			.width = imx576_active_area.width,
			.height = imx576_active_area.height,
		},
	};

	return imx576_set_pad_format(sd, NULL, sd_state, &fmt);
}

static const struct v4l2_subdev_video_ops imx576_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops imx576_pad_ops = {
	.enum_mbus_code = imx576_enum_mbus_code,
	.enum_frame_size = imx576_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = imx576_set_pad_format,
	.get_selection = imx576_get_selection,
	.enable_streams = imx576_enable_streams,
	.disable_streams = imx576_disable_streams,
};

static const struct v4l2_subdev_internal_ops imx576_internal_ops = {
	.init_state = imx576_init_state,
};

static const struct v4l2_subdev_ops imx576_subdev_ops = {
	.video = &imx576_video_ops,
	.pad = &imx576_pad_ops,
};

static const struct v4l2_ctrl_ops imx576_ctrl_ops = {
	.s_ctrl = imx576_set_ctrl,
};

static int imx576_detect(struct imx576 *imx576)
{
	int ret;
	u64 val;

	ret = cci_read(imx576->regmap, CCS_R_SENSOR_MODEL_ID, &val, NULL);
	if (ret)
		return dev_err_probe(imx576->dev, ret,
				     "failed to read chip id %x\n",
				     IMX576_CHIP_ID);

	if (val != IMX576_CHIP_ID)
		return dev_err_probe(imx576->dev, -EIO,
				     "chip id mismatch: %x!=%llx\n",
				     IMX576_CHIP_ID, val);

	return 0;
}

static int imx576_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx576 *imx576 = to_imx576(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(imx576_supply_names),
				    imx576->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to enable regulators\n");
		return ret;
	}

	ret = clk_prepare_enable(imx576->inclk);
	if (ret) {
		dev_err(imx576->dev, "fail to enable inclk\n");
		goto err_regulator_off;
	}

	gpiod_set_value_cansleep(imx576->reset_gpio, 0);

	/* T6: Wait for internal init before CCI access */
	usleep_range(1000, 1200);

	return 0;

err_regulator_off:
	regulator_bulk_disable(ARRAY_SIZE(imx576_supply_names),
			       imx576->supplies);

	return ret;
}

static int imx576_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct imx576 *imx576 = to_imx576(sd);

	clk_disable_unprepare(imx576->inclk);

	gpiod_set_value_cansleep(imx576->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(imx576_supply_names),
			       imx576->supplies);

	return 0;
}

static int imx576_parse_endpoint(struct imx576 *imx576)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *ep;
	int ret;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(imx576->dev), 0, 0, 0);
	if (!ep)
		return dev_err_probe(imx576->dev, -ENXIO,
				     "Failed to get next endpoint\n");

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != IMX576_NUM_DATA_LANES) {
		ret = dev_err_probe(imx576->dev, -EINVAL,
				    "only 4 data lanes are supported\n");
		goto error_out;
	}

	ret = v4l2_link_freq_to_bitmap(imx576->dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       link_freqs, ARRAY_SIZE(link_freqs),
				       &imx576->link_freq_bitmap);
	if (ret) {
		ret = dev_err_probe(imx576->dev, -EINVAL,
				    "only 1017MHz frequency is available\n");
		goto error_out;
	}

	imx576->link_freq_index = __ffs(imx576->link_freq_bitmap);

error_out:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int imx576_init_controls(struct imx576 *imx576)
{
	struct v4l2_fwnode_device_properties props;
	u64 vblank_def, hblank_def;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	u32 lpfr;
	int ret;

	ret = v4l2_fwnode_device_parse(imx576->dev, &props);
	if (ret)
		return ret;

	ctrl_hdlr = &imx576->handler;
	v4l2_ctrl_handler_init(ctrl_hdlr, 8);

	ret = imx576_pll_calculate(imx576,
				   link_freqs[__ffs(imx576->link_freq_bitmap)]);
	if (ret) {
		v4l2_ctrl_handler_free(ctrl_hdlr);
		dev_err(imx576->sd.dev, "PLL calculations failed: %d\n", ret);
		return ret;
	}

	vblank_def = IMX576_VBLANK_DEF - imx576_active_area.height;
	lpfr = vblank_def + imx576_active_area.height;
	imx576->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &imx576_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     IMX576_EXPOSURE_MIN,
					     lpfr - IMX576_EXPOSURE_OFFSET,
					     IMX576_EXPOSURE_STEP,
					     IMX576_EXPOSURE_DEFAULT);

	imx576->gain = v4l2_ctrl_new_std(ctrl_hdlr, &imx576_ctrl_ops,
					 V4L2_CID_ANALOGUE_GAIN,
					 IMX576_ANA_GAIN_MIN,
					 IMX576_ANA_GAIN_MAX,
					 IMX576_ANA_GAIN_STEP,
					 IMX576_ANA_GAIN_DEFAULT);

	imx576->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx576_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_def,
					   IMX576_VBLANK_MAX, 1, vblank_def);

	v4l2_ctrl_new_std(ctrl_hdlr, &imx576_ctrl_ops, V4L2_CID_PIXEL_RATE,
			  imx576->pll.pixel_rate_pixel_array,
			  imx576->pll.pixel_rate_pixel_array, 1,
			  imx576->pll.pixel_rate_pixel_array);

	imx576->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &imx576_ctrl_ops,
						   V4L2_CID_LINK_FREQ,
						   ARRAY_SIZE(link_freqs) - 1,
						   imx576->link_freq_index,
						   link_freqs);

	hblank_def =  IMX576_LINE_LENGTH - imx576_active_area.width;
	imx576->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &imx576_ctrl_ops,
					   V4L2_CID_HBLANK, hblank_def,
					   hblank_def, 1, hblank_def);

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &imx576_ctrl_ops, &props);
	if (ret)
		goto err_handler_free;

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		goto err_handler_free;
	}

	imx576->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	imx576->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	imx576->sd.ctrl_handler = ctrl_hdlr;

	return 0;

err_handler_free:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static int imx576_probe(struct i2c_client *client)
{
	struct imx576 *imx576;
	int ret;

	imx576 = devm_kzalloc(&client->dev, sizeof(*imx576), GFP_KERNEL);
	if (!imx576)
		return -ENOMEM;

	imx576->dev = &client->dev;

	/* Initialize subdev */
	v4l2_i2c_subdev_init(&imx576->sd, client, &imx576_subdev_ops);
	imx576->sd.internal_ops = &imx576_internal_ops;

	imx576->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(imx576->regmap))
		return dev_err_probe(imx576->dev, PTR_ERR(imx576->regmap),
				     "failed to initialize CCI\n");

	ret = imx576_parse_endpoint(imx576);
	if (ret)
		return dev_err_probe(imx576->dev, ret,
				     "failed to parse endpoint configuration\n");

	/* Get sensor input clock */
	imx576->inclk = devm_v4l2_sensor_clk_get(imx576->dev, NULL);
	if (IS_ERR(imx576->inclk))
		return dev_err_probe(imx576->dev, PTR_ERR(imx576->inclk),
				     "failed to get inclk\n");

	imx576->inclk_freq = clk_get_rate(imx576->inclk);
	if (imx576->inclk_freq != IMX576_INCLK_RATE)
		return dev_err_probe(imx576->dev, -EINVAL,
				     "inclk frequency not supported: %u Hz\n",
				     imx576->inclk_freq);

	for (unsigned int i = 0; i < ARRAY_SIZE(imx576_supply_names); i++)
		imx576->supplies[i].supply = imx576_supply_names[i];

	ret = devm_regulator_bulk_get(imx576->dev,
				      ARRAY_SIZE(imx576_supply_names),
				      imx576->supplies);
	if (ret)
		return dev_err_probe(imx576->dev, ret,
				     "failed to get regulators\n");

	imx576->reset_gpio = devm_gpiod_get_optional(imx576->dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(imx576->reset_gpio))
		return dev_err_probe(imx576->dev, PTR_ERR(imx576->reset_gpio),
				     "failed to get reset GPIO\n");

	ret = imx576_power_on(imx576->dev);
	if (ret)
		return ret;

	ret = imx576_detect(imx576);
	if (ret)
		goto error_power_off;

	ret = imx576_init_controls(imx576);
	if (ret) {
		dev_err_probe(imx576->dev, ret, "failed to init controls");
		goto error_power_off;
	}

	/* Initialize subdev */
	imx576->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	imx576->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	imx576->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&imx576->sd.entity, 1, &imx576->pad);
	if (ret) {
		dev_err_probe(imx576->dev, ret, "failed to init entity pads");
		goto error_handler_free;
	}

	imx576->sd.state_lock = imx576->handler.lock;
	ret = v4l2_subdev_init_finalize(&imx576->sd);
	if (ret < 0) {
		dev_err_probe(imx576->dev, ret, "subdev init error\n");
		goto error_media_entity;
	}

	pm_runtime_set_active(imx576->dev);
	pm_runtime_enable(imx576->dev);

	ret = v4l2_async_register_subdev_sensor(&imx576->sd);
	if (ret < 0) {
		dev_err_probe(imx576->dev, ret,
			      "failed to register imx576 sub-device\n");
		goto error_subdev_cleanup;
	}

	pm_runtime_idle(imx576->dev);

	return 0;

error_subdev_cleanup:
	v4l2_subdev_cleanup(&imx576->sd);
	pm_runtime_disable(imx576->dev);
	pm_runtime_set_suspended(imx576->dev);

error_media_entity:
	media_entity_cleanup(&imx576->sd.entity);

error_handler_free:
	v4l2_ctrl_handler_free(imx576->sd.ctrl_handler);

error_power_off:
	imx576_power_off(imx576->dev);

	return ret;
}

static void imx576_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct imx576 *imx576 = to_imx576(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(&imx576->sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(imx576->sd.ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev)) {
		imx576_power_off(&client->dev);
		pm_runtime_set_suspended(&client->dev);
	}
}

static DEFINE_RUNTIME_DEV_PM_OPS(imx576_pm_ops,
				 imx576_power_off, imx576_power_on, NULL);

static const struct of_device_id imx576_of_match[] = {
	{ .compatible = "sony,imx576" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx576_of_match);

static struct i2c_driver imx576_driver = {
	.driver = {
		.name = "imx576",
		.pm = &imx576_pm_ops,
		.of_match_table = imx576_of_match,
	},
	.probe = imx576_probe,
	.remove = imx576_remove,
};
module_i2c_driver(imx576_driver);

MODULE_DESCRIPTION("IMX576 Camera Sensor Driver");
MODULE_AUTHOR("Himanshu Bhavani <himanshu.bhavani@siliconsignals.io>");
MODULE_AUTHOR("Hardevsinh Palaniya <hardevsinh.palaniya@siliconsignals.io>");
MODULE_LICENSE("GPL");
