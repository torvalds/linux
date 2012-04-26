/*
 * drivers/media/video/smiapp/smiapp-core.c
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2010--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@maxwell.research.nokia.com>
 *
 * Based on smiapp driver by Vimarsh Zutshi
 * Based on jt8ev1.c by Vimarsh Zutshi
 * Based on smia-sensor.c by Tuukka Toivonen <tuukkat76@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-device.h>

#include "smiapp.h"

#define SMIAPP_ALIGN_DIM(dim, flags)		\
	((flags) & V4L2_SUBDEV_SEL_FLAG_SIZE_GE	\
	 ? ALIGN((dim), 2)			\
	 : (dim) & ~1)

/*
 * smiapp_module_idents - supported camera modules
 */
static const struct smiapp_module_ident smiapp_module_idents[] = {
	SMIAPP_IDENT_L(0x01, 0x022b, -1, "vs6555"),
	SMIAPP_IDENT_L(0x01, 0x022e, -1, "vw6558"),
	SMIAPP_IDENT_L(0x07, 0x7698, -1, "ovm7698"),
	SMIAPP_IDENT_L(0x0b, 0x4242, -1, "smiapp-003"),
	SMIAPP_IDENT_L(0x0c, 0x208a, -1, "tcm8330md"),
	SMIAPP_IDENT_LQ(0x0c, 0x2134, -1, "tcm8500md", &smiapp_tcm8500md_quirk),
	SMIAPP_IDENT_L(0x0c, 0x213e, -1, "et8en2"),
	SMIAPP_IDENT_L(0x0c, 0x2184, -1, "tcm8580md"),
	SMIAPP_IDENT_LQ(0x0c, 0x560f, -1, "jt8ew9", &smiapp_jt8ew9_quirk),
	SMIAPP_IDENT_LQ(0x10, 0x4141, -1, "jt8ev1", &smiapp_jt8ev1_quirk),
	SMIAPP_IDENT_LQ(0x10, 0x4241, -1, "imx125es", &smiapp_imx125es_quirk),
};

/*
 *
 * Dynamic Capability Identification
 *
 */

static int smiapp_read_frame_fmt(struct smiapp_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	u32 fmt_model_type, fmt_model_subtype, ncol_desc, nrow_desc;
	unsigned int i;
	int rval;
	int line_count = 0;
	int embedded_start = -1, embedded_end = -1;
	int image_start = 0;

	rval = smiapp_read(sensor, SMIAPP_REG_U8_FRAME_FORMAT_MODEL_TYPE,
			   &fmt_model_type);
	if (rval)
		return rval;

	rval = smiapp_read(sensor, SMIAPP_REG_U8_FRAME_FORMAT_MODEL_SUBTYPE,
			   &fmt_model_subtype);
	if (rval)
		return rval;

	ncol_desc = (fmt_model_subtype
		     & SMIAPP_FRAME_FORMAT_MODEL_SUBTYPE_NCOLS_MASK)
		>> SMIAPP_FRAME_FORMAT_MODEL_SUBTYPE_NCOLS_SHIFT;
	nrow_desc = fmt_model_subtype
		& SMIAPP_FRAME_FORMAT_MODEL_SUBTYPE_NROWS_MASK;

	dev_dbg(&client->dev, "format_model_type %s\n",
		fmt_model_type == SMIAPP_FRAME_FORMAT_MODEL_TYPE_2BYTE
		? "2 byte" :
		fmt_model_type == SMIAPP_FRAME_FORMAT_MODEL_TYPE_4BYTE
		? "4 byte" : "is simply bad");

	for (i = 0; i < ncol_desc + nrow_desc; i++) {
		u32 desc;
		u32 pixelcode;
		u32 pixels;
		char *which;
		char *what;

		if (fmt_model_type == SMIAPP_FRAME_FORMAT_MODEL_TYPE_2BYTE) {
			rval = smiapp_read(
				sensor,
				SMIAPP_REG_U16_FRAME_FORMAT_DESCRIPTOR_2(i),
				&desc);
			if (rval)
				return rval;

			pixelcode =
				(desc
				 & SMIAPP_FRAME_FORMAT_DESC_2_PIXELCODE_MASK)
				>> SMIAPP_FRAME_FORMAT_DESC_2_PIXELCODE_SHIFT;
			pixels = desc & SMIAPP_FRAME_FORMAT_DESC_2_PIXELS_MASK;
		} else if (fmt_model_type
			   == SMIAPP_FRAME_FORMAT_MODEL_TYPE_4BYTE) {
			rval = smiapp_read(
				sensor,
				SMIAPP_REG_U32_FRAME_FORMAT_DESCRIPTOR_4(i),
				&desc);
			if (rval)
				return rval;

			pixelcode =
				(desc
				 & SMIAPP_FRAME_FORMAT_DESC_4_PIXELCODE_MASK)
				>> SMIAPP_FRAME_FORMAT_DESC_4_PIXELCODE_SHIFT;
			pixels = desc & SMIAPP_FRAME_FORMAT_DESC_4_PIXELS_MASK;
		} else {
			dev_dbg(&client->dev,
				"invalid frame format model type %d\n",
				fmt_model_type);
			return -EINVAL;
		}

		if (i < ncol_desc)
			which = "columns";
		else
			which = "rows";

		switch (pixelcode) {
		case SMIAPP_FRAME_FORMAT_DESC_PIXELCODE_EMBEDDED:
			what = "embedded";
			break;
		case SMIAPP_FRAME_FORMAT_DESC_PIXELCODE_DUMMY:
			what = "dummy";
			break;
		case SMIAPP_FRAME_FORMAT_DESC_PIXELCODE_BLACK:
			what = "black";
			break;
		case SMIAPP_FRAME_FORMAT_DESC_PIXELCODE_DARK:
			what = "dark";
			break;
		case SMIAPP_FRAME_FORMAT_DESC_PIXELCODE_VISIBLE:
			what = "visible";
			break;
		default:
			what = "invalid";
			dev_dbg(&client->dev, "pixelcode %d\n", pixelcode);
			break;
		}

		dev_dbg(&client->dev, "%s pixels: %d %s\n",
			what, pixels, which);

		if (i < ncol_desc)
			continue;

		/* Handle row descriptors */
		if (pixelcode
		    == SMIAPP_FRAME_FORMAT_DESC_PIXELCODE_EMBEDDED) {
			embedded_start = line_count;
		} else {
			if (pixelcode == SMIAPP_FRAME_FORMAT_DESC_PIXELCODE_VISIBLE
			    || pixels >= sensor->limits[SMIAPP_LIMIT_MIN_FRAME_LENGTH_LINES] / 2)
				image_start = line_count;
			if (embedded_start != -1 && embedded_end == -1)
				embedded_end = line_count;
		}
		line_count += pixels;
	}

	if (embedded_start == -1 || embedded_end == -1) {
		embedded_start = 0;
		embedded_end = 0;
	}

	dev_dbg(&client->dev, "embedded data from lines %d to %d\n",
		embedded_start, embedded_end);
	dev_dbg(&client->dev, "image data starts at line %d\n", image_start);

	return 0;
}

static int smiapp_pll_configure(struct smiapp_sensor *sensor)
{
	struct smiapp_pll *pll = &sensor->pll;
	int rval;

	rval = smiapp_write(
		sensor, SMIAPP_REG_U16_VT_PIX_CLK_DIV, pll->vt_pix_clk_div);
	if (rval < 0)
		return rval;

	rval = smiapp_write(
		sensor, SMIAPP_REG_U16_VT_SYS_CLK_DIV, pll->vt_sys_clk_div);
	if (rval < 0)
		return rval;

	rval = smiapp_write(
		sensor, SMIAPP_REG_U16_PRE_PLL_CLK_DIV, pll->pre_pll_clk_div);
	if (rval < 0)
		return rval;

	rval = smiapp_write(
		sensor, SMIAPP_REG_U16_PLL_MULTIPLIER, pll->pll_multiplier);
	if (rval < 0)
		return rval;

	/* Lane op clock ratio does not apply here. */
	rval = smiapp_write(
		sensor, SMIAPP_REG_U32_REQUESTED_LINK_BIT_RATE_MBPS,
		DIV_ROUND_UP(pll->op_sys_clk_freq_hz, 1000000 / 256 / 256));
	if (rval < 0 || sensor->minfo.smiapp_profile == SMIAPP_PROFILE_0)
		return rval;

	rval = smiapp_write(
		sensor, SMIAPP_REG_U16_OP_PIX_CLK_DIV, pll->op_pix_clk_div);
	if (rval < 0)
		return rval;

	return smiapp_write(
		sensor, SMIAPP_REG_U16_OP_SYS_CLK_DIV, pll->op_sys_clk_div);
}

static int smiapp_pll_update(struct smiapp_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	struct smiapp_pll_limits lim = {
		.min_pre_pll_clk_div = sensor->limits[SMIAPP_LIMIT_MIN_PRE_PLL_CLK_DIV],
		.max_pre_pll_clk_div = sensor->limits[SMIAPP_LIMIT_MAX_PRE_PLL_CLK_DIV],
		.min_pll_ip_freq_hz = sensor->limits[SMIAPP_LIMIT_MIN_PLL_IP_FREQ_HZ],
		.max_pll_ip_freq_hz = sensor->limits[SMIAPP_LIMIT_MAX_PLL_IP_FREQ_HZ],
		.min_pll_multiplier = sensor->limits[SMIAPP_LIMIT_MIN_PLL_MULTIPLIER],
		.max_pll_multiplier = sensor->limits[SMIAPP_LIMIT_MAX_PLL_MULTIPLIER],
		.min_pll_op_freq_hz = sensor->limits[SMIAPP_LIMIT_MIN_PLL_OP_FREQ_HZ],
		.max_pll_op_freq_hz = sensor->limits[SMIAPP_LIMIT_MAX_PLL_OP_FREQ_HZ],

		.min_op_sys_clk_div = sensor->limits[SMIAPP_LIMIT_MIN_OP_SYS_CLK_DIV],
		.max_op_sys_clk_div = sensor->limits[SMIAPP_LIMIT_MAX_OP_SYS_CLK_DIV],
		.min_op_pix_clk_div = sensor->limits[SMIAPP_LIMIT_MIN_OP_PIX_CLK_DIV],
		.max_op_pix_clk_div = sensor->limits[SMIAPP_LIMIT_MAX_OP_PIX_CLK_DIV],
		.min_op_sys_clk_freq_hz = sensor->limits[SMIAPP_LIMIT_MIN_OP_SYS_CLK_FREQ_HZ],
		.max_op_sys_clk_freq_hz = sensor->limits[SMIAPP_LIMIT_MAX_OP_SYS_CLK_FREQ_HZ],
		.min_op_pix_clk_freq_hz = sensor->limits[SMIAPP_LIMIT_MIN_OP_PIX_CLK_FREQ_HZ],
		.max_op_pix_clk_freq_hz = sensor->limits[SMIAPP_LIMIT_MAX_OP_PIX_CLK_FREQ_HZ],

		.min_vt_sys_clk_div = sensor->limits[SMIAPP_LIMIT_MIN_VT_SYS_CLK_DIV],
		.max_vt_sys_clk_div = sensor->limits[SMIAPP_LIMIT_MAX_VT_SYS_CLK_DIV],
		.min_vt_pix_clk_div = sensor->limits[SMIAPP_LIMIT_MIN_VT_PIX_CLK_DIV],
		.max_vt_pix_clk_div = sensor->limits[SMIAPP_LIMIT_MAX_VT_PIX_CLK_DIV],
		.min_vt_sys_clk_freq_hz = sensor->limits[SMIAPP_LIMIT_MIN_VT_SYS_CLK_FREQ_HZ],
		.max_vt_sys_clk_freq_hz = sensor->limits[SMIAPP_LIMIT_MAX_VT_SYS_CLK_FREQ_HZ],
		.min_vt_pix_clk_freq_hz = sensor->limits[SMIAPP_LIMIT_MIN_VT_PIX_CLK_FREQ_HZ],
		.max_vt_pix_clk_freq_hz = sensor->limits[SMIAPP_LIMIT_MAX_VT_PIX_CLK_FREQ_HZ],

		.min_line_length_pck_bin = sensor->limits[SMIAPP_LIMIT_MIN_LINE_LENGTH_PCK_BIN],
		.min_line_length_pck = sensor->limits[SMIAPP_LIMIT_MIN_LINE_LENGTH_PCK],
	};
	struct smiapp_pll *pll = &sensor->pll;
	int rval;

	memset(&sensor->pll, 0, sizeof(sensor->pll));

	pll->lanes = sensor->platform_data->lanes;
	pll->ext_clk_freq_hz = sensor->platform_data->ext_clk;

	if (sensor->minfo.smiapp_profile == SMIAPP_PROFILE_0) {
		/*
		 * Fill in operational clock divisors limits from the
		 * video timing ones. On profile 0 sensors the
		 * requirements regarding them are essentially the
		 * same as on VT ones.
		 */
		lim.min_op_sys_clk_div = lim.min_vt_sys_clk_div;
		lim.max_op_sys_clk_div = lim.max_vt_sys_clk_div;
		lim.min_op_pix_clk_div = lim.min_vt_pix_clk_div;
		lim.max_op_pix_clk_div = lim.max_vt_pix_clk_div;
		lim.min_op_sys_clk_freq_hz = lim.min_vt_sys_clk_freq_hz;
		lim.max_op_sys_clk_freq_hz = lim.max_vt_sys_clk_freq_hz;
		lim.min_op_pix_clk_freq_hz = lim.min_vt_pix_clk_freq_hz;
		lim.max_op_pix_clk_freq_hz = lim.max_vt_pix_clk_freq_hz;
		/* Profile 0 sensors have no separate OP clock branch. */
		pll->flags |= SMIAPP_PLL_FLAG_NO_OP_CLOCKS;
	}

	if (smiapp_needs_quirk(sensor,
			       SMIAPP_QUIRK_FLAG_OP_PIX_CLOCK_PER_LANE))
		pll->flags |= SMIAPP_PLL_FLAG_OP_PIX_CLOCK_PER_LANE;

	pll->binning_horizontal = sensor->binning_horizontal;
	pll->binning_vertical = sensor->binning_vertical;
	pll->link_freq =
		sensor->link_freq->qmenu_int[sensor->link_freq->val];
	pll->scale_m = sensor->scale_m;
	pll->scale_n = sensor->limits[SMIAPP_LIMIT_SCALER_N_MIN];
	pll->bits_per_pixel = sensor->csi_format->compressed;

	rval = smiapp_pll_calculate(&client->dev, &lim, pll);
	if (rval < 0)
		return rval;

	sensor->pixel_rate_parray->cur.val64 = pll->vt_pix_clk_freq_hz;
	sensor->pixel_rate_csi->cur.val64 = pll->pixel_rate_csi;

	return 0;
}


/*
 *
 * V4L2 Controls handling
 *
 */

static void __smiapp_update_exposure_limits(struct smiapp_sensor *sensor)
{
	struct v4l2_ctrl *ctrl = sensor->exposure;
	int max;

	max = sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].height
		+ sensor->vblank->val
		- sensor->limits[SMIAPP_LIMIT_COARSE_INTEGRATION_TIME_MAX_MARGIN];

	ctrl->maximum = max;
	if (ctrl->default_value > max)
		ctrl->default_value = max;
	if (ctrl->val > max)
		ctrl->val = max;
	if (ctrl->cur.val > max)
		ctrl->cur.val = max;
}

/*
 * Order matters.
 *
 * 1. Bits-per-pixel, descending.
 * 2. Bits-per-pixel compressed, descending.
 * 3. Pixel order, same as in pixel_order_str. Formats for all four pixel
 *    orders must be defined.
 */
static const struct smiapp_csi_data_format smiapp_csi_data_formats[] = {
	{ V4L2_MBUS_FMT_SGRBG12_1X12, 12, 12, SMIAPP_PIXEL_ORDER_GRBG, },
	{ V4L2_MBUS_FMT_SRGGB12_1X12, 12, 12, SMIAPP_PIXEL_ORDER_RGGB, },
	{ V4L2_MBUS_FMT_SBGGR12_1X12, 12, 12, SMIAPP_PIXEL_ORDER_BGGR, },
	{ V4L2_MBUS_FMT_SGBRG12_1X12, 12, 12, SMIAPP_PIXEL_ORDER_GBRG, },
	{ V4L2_MBUS_FMT_SGRBG10_1X10, 10, 10, SMIAPP_PIXEL_ORDER_GRBG, },
	{ V4L2_MBUS_FMT_SRGGB10_1X10, 10, 10, SMIAPP_PIXEL_ORDER_RGGB, },
	{ V4L2_MBUS_FMT_SBGGR10_1X10, 10, 10, SMIAPP_PIXEL_ORDER_BGGR, },
	{ V4L2_MBUS_FMT_SGBRG10_1X10, 10, 10, SMIAPP_PIXEL_ORDER_GBRG, },
	{ V4L2_MBUS_FMT_SGRBG10_DPCM8_1X8, 10, 8, SMIAPP_PIXEL_ORDER_GRBG, },
	{ V4L2_MBUS_FMT_SRGGB10_DPCM8_1X8, 10, 8, SMIAPP_PIXEL_ORDER_RGGB, },
	{ V4L2_MBUS_FMT_SBGGR10_DPCM8_1X8, 10, 8, SMIAPP_PIXEL_ORDER_BGGR, },
	{ V4L2_MBUS_FMT_SGBRG10_DPCM8_1X8, 10, 8, SMIAPP_PIXEL_ORDER_GBRG, },
};

const char *pixel_order_str[] = { "GRBG", "RGGB", "BGGR", "GBRG" };

#define to_csi_format_idx(fmt) (((unsigned long)(fmt)			\
				 - (unsigned long)smiapp_csi_data_formats) \
				/ sizeof(*smiapp_csi_data_formats))

static u32 smiapp_pixel_order(struct smiapp_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int flip = 0;

	if (sensor->hflip) {
		if (sensor->hflip->val)
			flip |= SMIAPP_IMAGE_ORIENTATION_HFLIP;

		if (sensor->vflip->val)
			flip |= SMIAPP_IMAGE_ORIENTATION_VFLIP;
	}

	flip ^= sensor->hvflip_inv_mask;

	dev_dbg(&client->dev, "flip %d\n", flip);
	return sensor->default_pixel_order ^ flip;
}

static void smiapp_update_mbus_formats(struct smiapp_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	unsigned int csi_format_idx =
		to_csi_format_idx(sensor->csi_format) & ~3;
	unsigned int internal_csi_format_idx =
		to_csi_format_idx(sensor->internal_csi_format) & ~3;
	unsigned int pixel_order = smiapp_pixel_order(sensor);

	sensor->mbus_frame_fmts =
		sensor->default_mbus_frame_fmts << pixel_order;
	sensor->csi_format =
		&smiapp_csi_data_formats[csi_format_idx + pixel_order];
	sensor->internal_csi_format =
		&smiapp_csi_data_formats[internal_csi_format_idx
					 + pixel_order];

	BUG_ON(max(internal_csi_format_idx, csi_format_idx) + pixel_order
	       >= ARRAY_SIZE(smiapp_csi_data_formats));
	BUG_ON(min(internal_csi_format_idx, csi_format_idx) < 0);

	dev_dbg(&client->dev, "new pixel order %s\n",
		pixel_order_str[pixel_order]);
}

static int smiapp_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct smiapp_sensor *sensor =
		container_of(ctrl->handler, struct smiapp_subdev, ctrl_handler)
			->sensor;
	u32 orient = 0;
	int exposure;
	int rval;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		return smiapp_write(
			sensor,
			SMIAPP_REG_U16_ANALOGUE_GAIN_CODE_GLOBAL, ctrl->val);

	case V4L2_CID_EXPOSURE:
		return smiapp_write(
			sensor,
			SMIAPP_REG_U16_COARSE_INTEGRATION_TIME, ctrl->val);

	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		if (sensor->streaming)
			return -EBUSY;

		if (sensor->hflip->val)
			orient |= SMIAPP_IMAGE_ORIENTATION_HFLIP;

		if (sensor->vflip->val)
			orient |= SMIAPP_IMAGE_ORIENTATION_VFLIP;

		orient ^= sensor->hvflip_inv_mask;
		rval = smiapp_write(sensor,
				    SMIAPP_REG_U8_IMAGE_ORIENTATION,
				    orient);
		if (rval < 0)
			return rval;

		smiapp_update_mbus_formats(sensor);

		return 0;

	case V4L2_CID_VBLANK:
		exposure = sensor->exposure->val;

		__smiapp_update_exposure_limits(sensor);

		if (exposure > sensor->exposure->maximum) {
			sensor->exposure->val =
				sensor->exposure->maximum;
			rval = smiapp_set_ctrl(
				sensor->exposure);
			if (rval < 0)
				return rval;
		}

		return smiapp_write(
			sensor, SMIAPP_REG_U16_FRAME_LENGTH_LINES,
			sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].height
			+ ctrl->val);

	case V4L2_CID_HBLANK:
		return smiapp_write(
			sensor, SMIAPP_REG_U16_LINE_LENGTH_PCK,
			sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].width
			+ ctrl->val);

	case V4L2_CID_LINK_FREQ:
		if (sensor->streaming)
			return -EBUSY;

		return smiapp_pll_update(sensor);

	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops smiapp_ctrl_ops = {
	.s_ctrl = smiapp_set_ctrl,
};

static int smiapp_init_controls(struct smiapp_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	struct v4l2_ctrl_config cfg;
	int rval;

	rval = v4l2_ctrl_handler_init(&sensor->pixel_array->ctrl_handler, 7);
	if (rval)
		return rval;
	sensor->pixel_array->ctrl_handler.lock = &sensor->mutex;

	sensor->analog_gain = v4l2_ctrl_new_std(
		&sensor->pixel_array->ctrl_handler, &smiapp_ctrl_ops,
		V4L2_CID_ANALOGUE_GAIN,
		sensor->limits[SMIAPP_LIMIT_ANALOGUE_GAIN_CODE_MIN],
		sensor->limits[SMIAPP_LIMIT_ANALOGUE_GAIN_CODE_MAX],
		max(sensor->limits[SMIAPP_LIMIT_ANALOGUE_GAIN_CODE_STEP], 1U),
		sensor->limits[SMIAPP_LIMIT_ANALOGUE_GAIN_CODE_MIN]);

	/* Exposure limits will be updated soon, use just something here. */
	sensor->exposure = v4l2_ctrl_new_std(
		&sensor->pixel_array->ctrl_handler, &smiapp_ctrl_ops,
		V4L2_CID_EXPOSURE, 0, 0, 1, 0);

	sensor->hflip = v4l2_ctrl_new_std(
		&sensor->pixel_array->ctrl_handler, &smiapp_ctrl_ops,
		V4L2_CID_HFLIP, 0, 1, 1, 0);
	sensor->vflip = v4l2_ctrl_new_std(
		&sensor->pixel_array->ctrl_handler, &smiapp_ctrl_ops,
		V4L2_CID_VFLIP, 0, 1, 1, 0);

	sensor->vblank = v4l2_ctrl_new_std(
		&sensor->pixel_array->ctrl_handler, &smiapp_ctrl_ops,
		V4L2_CID_VBLANK, 0, 1, 1, 0);

	if (sensor->vblank)
		sensor->vblank->flags |= V4L2_CTRL_FLAG_UPDATE;

	sensor->hblank = v4l2_ctrl_new_std(
		&sensor->pixel_array->ctrl_handler, &smiapp_ctrl_ops,
		V4L2_CID_HBLANK, 0, 1, 1, 0);

	if (sensor->hblank)
		sensor->hblank->flags |= V4L2_CTRL_FLAG_UPDATE;

	sensor->pixel_rate_parray = v4l2_ctrl_new_std(
		&sensor->pixel_array->ctrl_handler, &smiapp_ctrl_ops,
		V4L2_CID_PIXEL_RATE, 0, 0, 1, 0);

	if (sensor->pixel_array->ctrl_handler.error) {
		dev_err(&client->dev,
			"pixel array controls initialization failed (%d)\n",
			sensor->pixel_array->ctrl_handler.error);
		rval = sensor->pixel_array->ctrl_handler.error;
		goto error;
	}

	sensor->pixel_array->sd.ctrl_handler =
		&sensor->pixel_array->ctrl_handler;

	v4l2_ctrl_cluster(2, &sensor->hflip);

	rval = v4l2_ctrl_handler_init(&sensor->src->ctrl_handler, 0);
	if (rval)
		goto error;
	sensor->src->ctrl_handler.lock = &sensor->mutex;

	memset(&cfg, 0, sizeof(cfg));

	cfg.ops = &smiapp_ctrl_ops;
	cfg.id = V4L2_CID_LINK_FREQ;
	cfg.type = V4L2_CTRL_TYPE_INTEGER_MENU;
	while (sensor->platform_data->op_sys_clock[cfg.max + 1])
		cfg.max++;
	cfg.qmenu_int = sensor->platform_data->op_sys_clock;

	sensor->link_freq = v4l2_ctrl_new_custom(
		&sensor->src->ctrl_handler, &cfg, NULL);

	sensor->pixel_rate_csi = v4l2_ctrl_new_std(
		&sensor->src->ctrl_handler, &smiapp_ctrl_ops,
		V4L2_CID_PIXEL_RATE, 0, 0, 1, 0);

	if (sensor->src->ctrl_handler.error) {
		dev_err(&client->dev,
			"src controls initialization failed (%d)\n",
			sensor->src->ctrl_handler.error);
		rval = sensor->src->ctrl_handler.error;
		goto error;
	}

	sensor->src->sd.ctrl_handler =
		&sensor->src->ctrl_handler;

	return 0;

error:
	v4l2_ctrl_handler_free(&sensor->pixel_array->ctrl_handler);
	v4l2_ctrl_handler_free(&sensor->src->ctrl_handler);

	return rval;
}

static void smiapp_free_controls(struct smiapp_sensor *sensor)
{
	unsigned int i;

	for (i = 0; i < sensor->ssds_used; i++)
		v4l2_ctrl_handler_free(&sensor->ssds[i].ctrl_handler);
}

static int smiapp_get_limits(struct smiapp_sensor *sensor, int const *limit,
			     unsigned int n)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	unsigned int i;
	u32 val;
	int rval;

	for (i = 0; i < n; i++) {
		rval = smiapp_read(
			sensor, smiapp_reg_limits[limit[i]].addr, &val);
		if (rval)
			return rval;
		sensor->limits[limit[i]] = val;
		dev_dbg(&client->dev, "0x%8.8x \"%s\" = %d, 0x%x\n",
			smiapp_reg_limits[limit[i]].addr,
			smiapp_reg_limits[limit[i]].what, val, val);
	}

	return 0;
}

static int smiapp_get_all_limits(struct smiapp_sensor *sensor)
{
	unsigned int i;
	int rval;

	for (i = 0; i < SMIAPP_LIMIT_LAST; i++) {
		rval = smiapp_get_limits(sensor, &i, 1);
		if (rval < 0)
			return rval;
	}

	if (sensor->limits[SMIAPP_LIMIT_SCALER_N_MIN] == 0)
		smiapp_replace_limit(sensor, SMIAPP_LIMIT_SCALER_N_MIN, 16);

	return 0;
}

static int smiapp_get_limits_binning(struct smiapp_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	static u32 const limits[] = {
		SMIAPP_LIMIT_MIN_FRAME_LENGTH_LINES_BIN,
		SMIAPP_LIMIT_MAX_FRAME_LENGTH_LINES_BIN,
		SMIAPP_LIMIT_MIN_LINE_LENGTH_PCK_BIN,
		SMIAPP_LIMIT_MAX_LINE_LENGTH_PCK_BIN,
		SMIAPP_LIMIT_MIN_LINE_BLANKING_PCK_BIN,
		SMIAPP_LIMIT_FINE_INTEGRATION_TIME_MIN_BIN,
		SMIAPP_LIMIT_FINE_INTEGRATION_TIME_MAX_MARGIN_BIN,
	};
	static u32 const limits_replace[] = {
		SMIAPP_LIMIT_MIN_FRAME_LENGTH_LINES,
		SMIAPP_LIMIT_MAX_FRAME_LENGTH_LINES,
		SMIAPP_LIMIT_MIN_LINE_LENGTH_PCK,
		SMIAPP_LIMIT_MAX_LINE_LENGTH_PCK,
		SMIAPP_LIMIT_MIN_LINE_BLANKING_PCK,
		SMIAPP_LIMIT_FINE_INTEGRATION_TIME_MIN,
		SMIAPP_LIMIT_FINE_INTEGRATION_TIME_MAX_MARGIN,
	};
	unsigned int i;
	int rval;

	if (sensor->limits[SMIAPP_LIMIT_BINNING_CAPABILITY] ==
	    SMIAPP_BINNING_CAPABILITY_NO) {
		for (i = 0; i < ARRAY_SIZE(limits); i++)
			sensor->limits[limits[i]] =
				sensor->limits[limits_replace[i]];

		return 0;
	}

	rval = smiapp_get_limits(sensor, limits, ARRAY_SIZE(limits));
	if (rval < 0)
		return rval;

	/*
	 * Sanity check whether the binning limits are valid. If not,
	 * use the non-binning ones.
	 */
	if (sensor->limits[SMIAPP_LIMIT_MIN_FRAME_LENGTH_LINES_BIN]
	    && sensor->limits[SMIAPP_LIMIT_MIN_LINE_LENGTH_PCK_BIN]
	    && sensor->limits[SMIAPP_LIMIT_MIN_LINE_BLANKING_PCK_BIN])
		return 0;

	for (i = 0; i < ARRAY_SIZE(limits); i++) {
		dev_dbg(&client->dev,
			"replace limit 0x%8.8x \"%s\" = %d, 0x%x\n",
			smiapp_reg_limits[limits[i]].addr,
			smiapp_reg_limits[limits[i]].what,
			sensor->limits[limits_replace[i]],
			sensor->limits[limits_replace[i]]);
		sensor->limits[limits[i]] =
			sensor->limits[limits_replace[i]];
	}

	return 0;
}

static int smiapp_get_mbus_formats(struct smiapp_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	unsigned int type, n;
	unsigned int i, pixel_order;
	int rval;

	rval = smiapp_read(
		sensor, SMIAPP_REG_U8_DATA_FORMAT_MODEL_TYPE, &type);
	if (rval)
		return rval;

	dev_dbg(&client->dev, "data_format_model_type %d\n", type);

	rval = smiapp_read(sensor, SMIAPP_REG_U8_PIXEL_ORDER,
			   &pixel_order);
	if (rval)
		return rval;

	if (pixel_order >= ARRAY_SIZE(pixel_order_str)) {
		dev_dbg(&client->dev, "bad pixel order %d\n", pixel_order);
		return -EINVAL;
	}

	dev_dbg(&client->dev, "pixel order %d (%s)\n", pixel_order,
		pixel_order_str[pixel_order]);

	switch (type) {
	case SMIAPP_DATA_FORMAT_MODEL_TYPE_NORMAL:
		n = SMIAPP_DATA_FORMAT_MODEL_TYPE_NORMAL_N;
		break;
	case SMIAPP_DATA_FORMAT_MODEL_TYPE_EXTENDED:
		n = SMIAPP_DATA_FORMAT_MODEL_TYPE_EXTENDED_N;
		break;
	default:
		return -EINVAL;
	}

	sensor->default_pixel_order = pixel_order;
	sensor->mbus_frame_fmts = 0;

	for (i = 0; i < n; i++) {
		unsigned int fmt, j;

		rval = smiapp_read(
			sensor,
			SMIAPP_REG_U16_DATA_FORMAT_DESCRIPTOR(i), &fmt);
		if (rval)
			return rval;

		dev_dbg(&client->dev, "bpp %d, compressed %d\n",
			fmt >> 8, (u8)fmt);

		for (j = 0; j < ARRAY_SIZE(smiapp_csi_data_formats); j++) {
			const struct smiapp_csi_data_format *f =
				&smiapp_csi_data_formats[j];

			if (f->pixel_order != SMIAPP_PIXEL_ORDER_GRBG)
				continue;

			if (f->width != fmt >> 8 || f->compressed != (u8)fmt)
				continue;

			dev_dbg(&client->dev, "jolly good! %d\n", j);

			sensor->default_mbus_frame_fmts |= 1 << j;
			if (!sensor->csi_format) {
				sensor->csi_format = f;
				sensor->internal_csi_format = f;
			}
		}
	}

	if (!sensor->csi_format) {
		dev_err(&client->dev, "no supported mbus code found\n");
		return -EINVAL;
	}

	smiapp_update_mbus_formats(sensor);

	return 0;
}

static void smiapp_update_blanking(struct smiapp_sensor *sensor)
{
	struct v4l2_ctrl *vblank = sensor->vblank;
	struct v4l2_ctrl *hblank = sensor->hblank;

	vblank->minimum =
		max_t(int,
		      sensor->limits[SMIAPP_LIMIT_MIN_FRAME_BLANKING_LINES],
		      sensor->limits[SMIAPP_LIMIT_MIN_FRAME_LENGTH_LINES_BIN] -
		      sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].height);
	vblank->maximum =
		sensor->limits[SMIAPP_LIMIT_MAX_FRAME_LENGTH_LINES_BIN] -
		sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].height;

	vblank->val = clamp_t(int, vblank->val,
			      vblank->minimum, vblank->maximum);
	vblank->default_value = vblank->minimum;
	vblank->val = vblank->val;
	vblank->cur.val = vblank->val;

	hblank->minimum =
		max_t(int,
		      sensor->limits[SMIAPP_LIMIT_MIN_LINE_LENGTH_PCK_BIN] -
		      sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].width,
		      sensor->limits[SMIAPP_LIMIT_MIN_LINE_BLANKING_PCK_BIN]);
	hblank->maximum =
		sensor->limits[SMIAPP_LIMIT_MAX_LINE_LENGTH_PCK_BIN] -
		sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].width;

	hblank->val = clamp_t(int, hblank->val,
			      hblank->minimum, hblank->maximum);
	hblank->default_value = hblank->minimum;
	hblank->val = hblank->val;
	hblank->cur.val = hblank->val;

	__smiapp_update_exposure_limits(sensor);
}

static int smiapp_update_mode(struct smiapp_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	unsigned int binning_mode;
	int rval;

	dev_dbg(&client->dev, "frame size: %dx%d\n",
		sensor->src->crop[SMIAPP_PAD_SRC].width,
		sensor->src->crop[SMIAPP_PAD_SRC].height);
	dev_dbg(&client->dev, "csi format width: %d\n",
		sensor->csi_format->width);

	/* Binning has to be set up here; it affects limits */
	if (sensor->binning_horizontal == 1 &&
	    sensor->binning_vertical == 1) {
		binning_mode = 0;
	} else {
		u8 binning_type =
			(sensor->binning_horizontal << 4)
			| sensor->binning_vertical;

		rval = smiapp_write(
			sensor, SMIAPP_REG_U8_BINNING_TYPE, binning_type);
		if (rval < 0)
			return rval;

		binning_mode = 1;
	}
	rval = smiapp_write(sensor, SMIAPP_REG_U8_BINNING_MODE, binning_mode);
	if (rval < 0)
		return rval;

	/* Get updated limits due to binning */
	rval = smiapp_get_limits_binning(sensor);
	if (rval < 0)
		return rval;

	rval = smiapp_pll_update(sensor);
	if (rval < 0)
		return rval;

	/* Output from pixel array, including blanking */
	smiapp_update_blanking(sensor);

	dev_dbg(&client->dev, "vblank\t\t%d\n", sensor->vblank->val);
	dev_dbg(&client->dev, "hblank\t\t%d\n", sensor->hblank->val);

	dev_dbg(&client->dev, "real timeperframe\t100/%d\n",
		sensor->pll.vt_pix_clk_freq_hz /
		((sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].width
		  + sensor->hblank->val) *
		 (sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].height
		  + sensor->vblank->val) / 100));

	return 0;
}

/*
 *
 * SMIA++ NVM handling
 *
 */
static int smiapp_read_nvm(struct smiapp_sensor *sensor,
			   unsigned char *nvm)
{
	u32 i, s, p, np, v;
	int rval = 0, rval2;

	np = sensor->nvm_size / SMIAPP_NVM_PAGE_SIZE;
	for (p = 0; p < np; p++) {
		rval = smiapp_write(
			sensor,
			SMIAPP_REG_U8_DATA_TRANSFER_IF_1_PAGE_SELECT, p);
		if (rval)
			goto out;

		rval = smiapp_write(sensor,
				    SMIAPP_REG_U8_DATA_TRANSFER_IF_1_CTRL,
				    SMIAPP_DATA_TRANSFER_IF_1_CTRL_EN |
				    SMIAPP_DATA_TRANSFER_IF_1_CTRL_RD_EN);
		if (rval)
			goto out;

		for (i = 0; i < 1000; i++) {
			rval = smiapp_read(
				sensor,
				SMIAPP_REG_U8_DATA_TRANSFER_IF_1_STATUS, &s);

			if (rval)
				goto out;

			if (s & SMIAPP_DATA_TRANSFER_IF_1_STATUS_RD_READY)
				break;

			if (--i == 0) {
				rval = -ETIMEDOUT;
				goto out;
			}

		}

		for (i = 0; i < SMIAPP_NVM_PAGE_SIZE; i++) {
			rval = smiapp_read(
				sensor,
				SMIAPP_REG_U8_DATA_TRANSFER_IF_1_DATA_0 + i,
				&v);
			if (rval)
				goto out;

			*nvm++ = v;
		}
	}

out:
	rval2 = smiapp_write(sensor, SMIAPP_REG_U8_DATA_TRANSFER_IF_1_CTRL, 0);
	if (rval < 0)
		return rval;
	else
		return rval2;
}

/*
 *
 * SMIA++ CCI address control
 *
 */
static int smiapp_change_cci_addr(struct smiapp_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int rval;
	u32 val;

	client->addr = sensor->platform_data->i2c_addr_dfl;

	rval = smiapp_write(sensor,
			    SMIAPP_REG_U8_CCI_ADDRESS_CONTROL,
			    sensor->platform_data->i2c_addr_alt << 1);
	if (rval)
		return rval;

	client->addr = sensor->platform_data->i2c_addr_alt;

	/* verify addr change went ok */
	rval = smiapp_read(sensor, SMIAPP_REG_U8_CCI_ADDRESS_CONTROL, &val);
	if (rval)
		return rval;

	if (val != sensor->platform_data->i2c_addr_alt << 1)
		return -ENODEV;

	return 0;
}

/*
 *
 * SMIA++ Mode Control
 *
 */
static int smiapp_setup_flash_strobe(struct smiapp_sensor *sensor)
{
	struct smiapp_flash_strobe_parms *strobe_setup;
	unsigned int ext_freq = sensor->platform_data->ext_clk;
	u32 tmp;
	u32 strobe_adjustment;
	u32 strobe_width_high_rs;
	int rval;

	strobe_setup = sensor->platform_data->strobe_setup;

	/*
	 * How to calculate registers related to strobe length. Please
	 * do not change, or if you do at least know what you're
	 * doing. :-)
	 *
	 * Sakari Ailus <sakari.ailus@maxwell.research.nokia.com> 2010-10-25
	 *
	 * flash_strobe_length [us] / 10^6 = (tFlash_strobe_width_ctrl
	 *	/ EXTCLK freq [Hz]) * flash_strobe_adjustment
	 *
	 * tFlash_strobe_width_ctrl E N, [1 - 0xffff]
	 * flash_strobe_adjustment E N, [1 - 0xff]
	 *
	 * The formula above is written as below to keep it on one
	 * line:
	 *
	 * l / 10^6 = w / e * a
	 *
	 * Let's mark w * a by x:
	 *
	 * x = w * a
	 *
	 * Thus, we get:
	 *
	 * x = l * e / 10^6
	 *
	 * The strobe width must be at least as long as requested,
	 * thus rounding upwards is needed.
	 *
	 * x = (l * e + 10^6 - 1) / 10^6
	 * -----------------------------
	 *
	 * Maximum possible accuracy is wanted at all times. Thus keep
	 * a as small as possible.
	 *
	 * Calculate a, assuming maximum w, with rounding upwards:
	 *
	 * a = (x + (2^16 - 1) - 1) / (2^16 - 1)
	 * -------------------------------------
	 *
	 * Thus, we also get w, with that a, with rounding upwards:
	 *
	 * w = (x + a - 1) / a
	 * -------------------
	 *
	 * To get limits:
	 *
	 * x E [1, (2^16 - 1) * (2^8 - 1)]
	 *
	 * Substituting maximum x to the original formula (with rounding),
	 * the maximum l is thus
	 *
	 * (2^16 - 1) * (2^8 - 1) * 10^6 = l * e + 10^6 - 1
	 *
	 * l = (10^6 * (2^16 - 1) * (2^8 - 1) - 10^6 + 1) / e
	 * --------------------------------------------------
	 *
	 * flash_strobe_length must be clamped between 1 and
	 * (10^6 * (2^16 - 1) * (2^8 - 1) - 10^6 + 1) / EXTCLK freq.
	 *
	 * Then,
	 *
	 * flash_strobe_adjustment = ((flash_strobe_length *
	 *	EXTCLK freq + 10^6 - 1) / 10^6 + (2^16 - 1) - 1) / (2^16 - 1)
	 *
	 * tFlash_strobe_width_ctrl = ((flash_strobe_length *
	 *	EXTCLK freq + 10^6 - 1) / 10^6 +
	 *	flash_strobe_adjustment - 1) / flash_strobe_adjustment
	 */
	tmp = div_u64(1000000ULL * ((1 << 16) - 1) * ((1 << 8) - 1) -
		      1000000 + 1, ext_freq);
	strobe_setup->strobe_width_high_us =
		clamp_t(u32, strobe_setup->strobe_width_high_us, 1, tmp);

	tmp = div_u64(((u64)strobe_setup->strobe_width_high_us * (u64)ext_freq +
			1000000 - 1), 1000000ULL);
	strobe_adjustment = (tmp + (1 << 16) - 1 - 1) / ((1 << 16) - 1);
	strobe_width_high_rs = (tmp + strobe_adjustment - 1) /
				strobe_adjustment;

	rval = smiapp_write(sensor, SMIAPP_REG_U8_FLASH_MODE_RS,
			    strobe_setup->mode);
	if (rval < 0)
		goto out;

	rval = smiapp_write(sensor, SMIAPP_REG_U8_FLASH_STROBE_ADJUSTMENT,
			    strobe_adjustment);
	if (rval < 0)
		goto out;

	rval = smiapp_write(
		sensor, SMIAPP_REG_U16_TFLASH_STROBE_WIDTH_HIGH_RS_CTRL,
		strobe_width_high_rs);
	if (rval < 0)
		goto out;

	rval = smiapp_write(sensor, SMIAPP_REG_U16_TFLASH_STROBE_DELAY_RS_CTRL,
			    strobe_setup->strobe_delay);
	if (rval < 0)
		goto out;

	rval = smiapp_write(sensor, SMIAPP_REG_U16_FLASH_STROBE_START_POINT,
			    strobe_setup->stobe_start_point);
	if (rval < 0)
		goto out;

	rval = smiapp_write(sensor, SMIAPP_REG_U8_FLASH_TRIGGER_RS,
			    strobe_setup->trigger);

out:
	sensor->platform_data->strobe_setup->trigger = 0;

	return rval;
}

/* -----------------------------------------------------------------------------
 * Power management
 */

static int smiapp_power_on(struct smiapp_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	unsigned int sleep;
	int rval;

	rval = regulator_enable(sensor->vana);
	if (rval) {
		dev_err(&client->dev, "failed to enable vana regulator\n");
		return rval;
	}
	usleep_range(1000, 1000);

	if (sensor->platform_data->set_xclk)
		rval = sensor->platform_data->set_xclk(
			&sensor->src->sd, sensor->platform_data->ext_clk);
	else
		rval = clk_enable(sensor->ext_clk);
	if (rval < 0) {
		dev_dbg(&client->dev, "failed to set xclk\n");
		goto out_xclk_fail;
	}
	usleep_range(1000, 1000);

	if (sensor->platform_data->xshutdown != SMIAPP_NO_XSHUTDOWN)
		gpio_set_value(sensor->platform_data->xshutdown, 1);

	sleep = SMIAPP_RESET_DELAY(sensor->platform_data->ext_clk);
	usleep_range(sleep, sleep);

	/*
	 * Failures to respond to the address change command have been noticed.
	 * Those failures seem to be caused by the sensor requiring a longer
	 * boot time than advertised. An additional 10ms delay seems to work
	 * around the issue, but the SMIA++ I2C write retry hack makes the delay
	 * unnecessary. The failures need to be investigated to find a proper
	 * fix, and a delay will likely need to be added here if the I2C write
	 * retry hack is reverted before the root cause of the boot time issue
	 * is found.
	 */

	if (sensor->platform_data->i2c_addr_alt) {
		rval = smiapp_change_cci_addr(sensor);
		if (rval) {
			dev_err(&client->dev, "cci address change error\n");
			goto out_cci_addr_fail;
		}
	}

	rval = smiapp_write(sensor, SMIAPP_REG_U8_SOFTWARE_RESET,
			    SMIAPP_SOFTWARE_RESET);
	if (rval < 0) {
		dev_err(&client->dev, "software reset failed\n");
		goto out_cci_addr_fail;
	}

	if (sensor->platform_data->i2c_addr_alt) {
		rval = smiapp_change_cci_addr(sensor);
		if (rval) {
			dev_err(&client->dev, "cci address change error\n");
			goto out_cci_addr_fail;
		}
	}

	rval = smiapp_write(sensor, SMIAPP_REG_U16_COMPRESSION_MODE,
			    SMIAPP_COMPRESSION_MODE_SIMPLE_PREDICTOR);
	if (rval) {
		dev_err(&client->dev, "compression mode set failed\n");
		goto out_cci_addr_fail;
	}

	rval = smiapp_write(
		sensor, SMIAPP_REG_U16_EXTCLK_FREQUENCY_MHZ,
		sensor->platform_data->ext_clk / (1000000 / (1 << 8)));
	if (rval) {
		dev_err(&client->dev, "extclk frequency set failed\n");
		goto out_cci_addr_fail;
	}

	rval = smiapp_write(sensor, SMIAPP_REG_U8_CSI_LANE_MODE,
			    sensor->platform_data->lanes - 1);
	if (rval) {
		dev_err(&client->dev, "csi lane mode set failed\n");
		goto out_cci_addr_fail;
	}

	rval = smiapp_write(sensor, SMIAPP_REG_U8_FAST_STANDBY_CTRL,
			    SMIAPP_FAST_STANDBY_CTRL_IMMEDIATE);
	if (rval) {
		dev_err(&client->dev, "fast standby set failed\n");
		goto out_cci_addr_fail;
	}

	rval = smiapp_write(sensor, SMIAPP_REG_U8_CSI_SIGNALLING_MODE,
			    sensor->platform_data->csi_signalling_mode);
	if (rval) {
		dev_err(&client->dev, "csi signalling mode set failed\n");
		goto out_cci_addr_fail;
	}

	/* DPHY control done by sensor based on requested link rate */
	rval = smiapp_write(sensor, SMIAPP_REG_U8_DPHY_CTRL,
			    SMIAPP_DPHY_CTRL_UI);
	if (rval < 0)
		return rval;

	rval = smiapp_call_quirk(sensor, post_poweron);
	if (rval) {
		dev_err(&client->dev, "post_poweron quirks failed\n");
		goto out_cci_addr_fail;
	}

	/* Are we still initialising...? If yes, return here. */
	if (!sensor->pixel_array)
		return 0;

	rval = v4l2_ctrl_handler_setup(
		&sensor->pixel_array->ctrl_handler);
	if (rval)
		goto out_cci_addr_fail;

	rval = v4l2_ctrl_handler_setup(&sensor->src->ctrl_handler);
	if (rval)
		goto out_cci_addr_fail;

	mutex_lock(&sensor->mutex);
	rval = smiapp_update_mode(sensor);
	mutex_unlock(&sensor->mutex);
	if (rval < 0)
		goto out_cci_addr_fail;

	return 0;

out_cci_addr_fail:
	if (sensor->platform_data->xshutdown != SMIAPP_NO_XSHUTDOWN)
		gpio_set_value(sensor->platform_data->xshutdown, 0);
	if (sensor->platform_data->set_xclk)
		sensor->platform_data->set_xclk(&sensor->src->sd, 0);
	else
		clk_disable(sensor->ext_clk);

out_xclk_fail:
	regulator_disable(sensor->vana);
	return rval;
}

static void smiapp_power_off(struct smiapp_sensor *sensor)
{
	/*
	 * Currently power/clock to lens are enable/disabled separately
	 * but they are essentially the same signals. So if the sensor is
	 * powered off while the lens is powered on the sensor does not
	 * really see a power off and next time the cci address change
	 * will fail. So do a soft reset explicitly here.
	 */
	if (sensor->platform_data->i2c_addr_alt)
		smiapp_write(sensor,
			     SMIAPP_REG_U8_SOFTWARE_RESET,
			     SMIAPP_SOFTWARE_RESET);

	if (sensor->platform_data->xshutdown != SMIAPP_NO_XSHUTDOWN)
		gpio_set_value(sensor->platform_data->xshutdown, 0);
	if (sensor->platform_data->set_xclk)
		sensor->platform_data->set_xclk(&sensor->src->sd, 0);
	else
		clk_disable(sensor->ext_clk);
	usleep_range(5000, 5000);
	regulator_disable(sensor->vana);
	sensor->streaming = 0;
}

static int smiapp_set_power(struct v4l2_subdev *subdev, int on)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	int ret = 0;

	mutex_lock(&sensor->power_mutex);

	/*
	 * If the power count is modified from 0 to != 0 or from != 0
	 * to 0, update the power state.
	 */
	if (!sensor->power_count == !on)
		goto out;

	if (on) {
		/* Power on and perform initialisation. */
		ret = smiapp_power_on(sensor);
		if (ret < 0)
			goto out;
	} else {
		smiapp_power_off(sensor);
	}

	/* Update the power count. */
	sensor->power_count += on ? 1 : -1;
	WARN_ON(sensor->power_count < 0);

out:
	mutex_unlock(&sensor->power_mutex);
	return ret;
}

/* -----------------------------------------------------------------------------
 * Video stream management
 */

static int smiapp_start_streaming(struct smiapp_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int rval;

	mutex_lock(&sensor->mutex);

	rval = smiapp_write(sensor, SMIAPP_REG_U16_CSI_DATA_FORMAT,
			    (sensor->csi_format->width << 8) |
			    sensor->csi_format->compressed);
	if (rval)
		goto out;

	rval = smiapp_pll_configure(sensor);
	if (rval)
		goto out;

	/* Analog crop start coordinates */
	rval = smiapp_write(sensor, SMIAPP_REG_U16_X_ADDR_START,
			    sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].left);
	if (rval < 0)
		goto out;

	rval = smiapp_write(sensor, SMIAPP_REG_U16_Y_ADDR_START,
			    sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].top);
	if (rval < 0)
		goto out;

	/* Analog crop end coordinates */
	rval = smiapp_write(
		sensor, SMIAPP_REG_U16_X_ADDR_END,
		sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].left
		+ sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].width - 1);
	if (rval < 0)
		goto out;

	rval = smiapp_write(
		sensor, SMIAPP_REG_U16_Y_ADDR_END,
		sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].top
		+ sensor->pixel_array->crop[SMIAPP_PA_PAD_SRC].height - 1);
	if (rval < 0)
		goto out;

	/*
	 * Output from pixel array, including blanking, is set using
	 * controls below. No need to set here.
	 */

	/* Digital crop */
	if (sensor->limits[SMIAPP_LIMIT_DIGITAL_CROP_CAPABILITY]
	    == SMIAPP_DIGITAL_CROP_CAPABILITY_INPUT_CROP) {
		rval = smiapp_write(
			sensor, SMIAPP_REG_U16_DIGITAL_CROP_X_OFFSET,
			sensor->scaler->crop[SMIAPP_PAD_SINK].left);
		if (rval < 0)
			goto out;

		rval = smiapp_write(
			sensor, SMIAPP_REG_U16_DIGITAL_CROP_Y_OFFSET,
			sensor->scaler->crop[SMIAPP_PAD_SINK].top);
		if (rval < 0)
			goto out;

		rval = smiapp_write(
			sensor, SMIAPP_REG_U16_DIGITAL_CROP_IMAGE_WIDTH,
			sensor->scaler->crop[SMIAPP_PAD_SINK].width);
		if (rval < 0)
			goto out;

		rval = smiapp_write(
			sensor, SMIAPP_REG_U16_DIGITAL_CROP_IMAGE_HEIGHT,
			sensor->scaler->crop[SMIAPP_PAD_SINK].height);
		if (rval < 0)
			goto out;
	}

	/* Scaling */
	if (sensor->limits[SMIAPP_LIMIT_SCALING_CAPABILITY]
	    != SMIAPP_SCALING_CAPABILITY_NONE) {
		rval = smiapp_write(sensor, SMIAPP_REG_U16_SCALING_MODE,
				    sensor->scaling_mode);
		if (rval < 0)
			goto out;

		rval = smiapp_write(sensor, SMIAPP_REG_U16_SCALE_M,
				    sensor->scale_m);
		if (rval < 0)
			goto out;
	}

	/* Output size from sensor */
	rval = smiapp_write(sensor, SMIAPP_REG_U16_X_OUTPUT_SIZE,
			    sensor->src->crop[SMIAPP_PAD_SRC].width);
	if (rval < 0)
		goto out;
	rval = smiapp_write(sensor, SMIAPP_REG_U16_Y_OUTPUT_SIZE,
			    sensor->src->crop[SMIAPP_PAD_SRC].height);
	if (rval < 0)
		goto out;

	if ((sensor->flash_capability &
	     (SMIAPP_FLASH_MODE_CAPABILITY_SINGLE_STROBE |
	      SMIAPP_FLASH_MODE_CAPABILITY_MULTIPLE_STROBE)) &&
	    sensor->platform_data->strobe_setup != NULL &&
	    sensor->platform_data->strobe_setup->trigger != 0) {
		rval = smiapp_setup_flash_strobe(sensor);
		if (rval)
			goto out;
	}

	rval = smiapp_call_quirk(sensor, pre_streamon);
	if (rval) {
		dev_err(&client->dev, "pre_streamon quirks failed\n");
		goto out;
	}

	rval = smiapp_write(sensor, SMIAPP_REG_U8_MODE_SELECT,
			    SMIAPP_MODE_SELECT_STREAMING);

out:
	mutex_unlock(&sensor->mutex);

	return rval;
}

static int smiapp_stop_streaming(struct smiapp_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int rval;

	mutex_lock(&sensor->mutex);
	rval = smiapp_write(sensor, SMIAPP_REG_U8_MODE_SELECT,
			    SMIAPP_MODE_SELECT_SOFTWARE_STANDBY);
	if (rval)
		goto out;

	rval = smiapp_call_quirk(sensor, post_streamoff);
	if (rval)
		dev_err(&client->dev, "post_streamoff quirks failed\n");

out:
	mutex_unlock(&sensor->mutex);
	return rval;
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev video operations
 */

static int smiapp_set_stream(struct v4l2_subdev *subdev, int enable)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	int rval;

	if (sensor->streaming == enable)
		return 0;

	if (enable) {
		sensor->streaming = 1;
		rval = smiapp_start_streaming(sensor);
		if (rval < 0)
			sensor->streaming = 0;
	} else {
		rval = smiapp_stop_streaming(sensor);
		sensor->streaming = 0;
	}

	return rval;
}

static int smiapp_enum_mbus_code(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	unsigned int i;
	int idx = -1;
	int rval = -EINVAL;

	mutex_lock(&sensor->mutex);

	dev_err(&client->dev, "subdev %s, pad %d, index %d\n",
		subdev->name, code->pad, code->index);

	if (subdev != &sensor->src->sd || code->pad != SMIAPP_PAD_SRC) {
		if (code->index)
			goto out;

		code->code = sensor->internal_csi_format->code;
		rval = 0;
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(smiapp_csi_data_formats); i++) {
		if (sensor->mbus_frame_fmts & (1 << i))
			idx++;

		if (idx == code->index) {
			code->code = smiapp_csi_data_formats[i].code;
			dev_err(&client->dev, "found index %d, i %d, code %x\n",
				code->index, i, code->code);
			rval = 0;
			break;
		}
	}

out:
	mutex_unlock(&sensor->mutex);

	return rval;
}

static u32 __smiapp_get_mbus_code(struct v4l2_subdev *subdev,
				  unsigned int pad)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);

	if (subdev == &sensor->src->sd && pad == SMIAPP_PAD_SRC)
		return sensor->csi_format->code;
	else
		return sensor->internal_csi_format->code;
}

static int __smiapp_get_format(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_fh *fh,
			       struct v4l2_subdev_format *fmt)
{
	struct smiapp_subdev *ssd = to_smiapp_subdev(subdev);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		fmt->format = *v4l2_subdev_get_try_format(fh, fmt->pad);
	} else {
		struct v4l2_rect *r;

		if (fmt->pad == ssd->source_pad)
			r = &ssd->crop[ssd->source_pad];
		else
			r = &ssd->sink_fmt;

		fmt->format.code = __smiapp_get_mbus_code(subdev, fmt->pad);
		fmt->format.width = r->width;
		fmt->format.height = r->height;
	}

	return 0;
}

static int smiapp_get_format(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh,
			     struct v4l2_subdev_format *fmt)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	int rval;

	mutex_lock(&sensor->mutex);
	rval = __smiapp_get_format(subdev, fh, fmt);
	mutex_unlock(&sensor->mutex);

	return rval;
}

static void smiapp_get_crop_compose(struct v4l2_subdev *subdev,
				    struct v4l2_subdev_fh *fh,
				    struct v4l2_rect **crops,
				    struct v4l2_rect **comps, int which)
{
	struct smiapp_subdev *ssd = to_smiapp_subdev(subdev);
	unsigned int i;

	if (which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		if (crops)
			for (i = 0; i < subdev->entity.num_pads; i++)
				crops[i] = &ssd->crop[i];
		if (comps)
			*comps = &ssd->compose;
	} else {
		if (crops) {
			for (i = 0; i < subdev->entity.num_pads; i++) {
				crops[i] = v4l2_subdev_get_try_crop(fh, i);
				BUG_ON(!crops[i]);
			}
		}
		if (comps) {
			*comps = v4l2_subdev_get_try_compose(fh,
							     SMIAPP_PAD_SINK);
			BUG_ON(!*comps);
		}
	}
}

/* Changes require propagation only on sink pad. */
static void smiapp_propagate(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh, int which,
			     int target)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	struct smiapp_subdev *ssd = to_smiapp_subdev(subdev);
	struct v4l2_rect *comp, *crops[SMIAPP_PADS];

	smiapp_get_crop_compose(subdev, fh, crops, &comp, which);

	switch (target) {
	case V4L2_SUBDEV_SEL_TGT_CROP_ACTUAL:
		comp->width = crops[SMIAPP_PAD_SINK]->width;
		comp->height = crops[SMIAPP_PAD_SINK]->height;
		if (which == V4L2_SUBDEV_FORMAT_ACTIVE) {
			if (ssd == sensor->scaler) {
				sensor->scale_m =
					sensor->limits[
						SMIAPP_LIMIT_SCALER_N_MIN];
				sensor->scaling_mode =
					SMIAPP_SCALING_MODE_NONE;
			} else if (ssd == sensor->binner) {
				sensor->binning_horizontal = 1;
				sensor->binning_vertical = 1;
			}
		}
		/* Fall through */
	case V4L2_SUBDEV_SEL_TGT_COMPOSE_ACTUAL:
		*crops[SMIAPP_PAD_SRC] = *comp;
		break;
	default:
		BUG();
	}
}

static const struct smiapp_csi_data_format
*smiapp_validate_csi_data_format(struct smiapp_sensor *sensor, u32 code)
{
	const struct smiapp_csi_data_format *csi_format = sensor->csi_format;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(smiapp_csi_data_formats); i++) {
		if (sensor->mbus_frame_fmts & (1 << i)
		    && smiapp_csi_data_formats[i].code == code)
			return &smiapp_csi_data_formats[i];
	}

	return csi_format;
}

static int smiapp_set_format(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_fh *fh,
			     struct v4l2_subdev_format *fmt)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	struct smiapp_subdev *ssd = to_smiapp_subdev(subdev);
	struct v4l2_rect *crops[SMIAPP_PADS];

	mutex_lock(&sensor->mutex);

	/*
	 * Media bus code is changeable on src subdev's source pad. On
	 * other source pads we just get format here.
	 */
	if (fmt->pad == ssd->source_pad) {
		u32 code = fmt->format.code;
		int rval = __smiapp_get_format(subdev, fh, fmt);

		if (!rval && subdev == &sensor->src->sd) {
			const struct smiapp_csi_data_format *csi_format =
				smiapp_validate_csi_data_format(sensor, code);
			if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
				sensor->csi_format = csi_format;
			fmt->format.code = csi_format->code;
		}

		mutex_unlock(&sensor->mutex);
		return rval;
	}

	/* Sink pad. Width and height are changeable here. */
	fmt->format.code = __smiapp_get_mbus_code(subdev, fmt->pad);
	fmt->format.width &= ~1;
	fmt->format.height &= ~1;

	fmt->format.width =
		clamp(fmt->format.width,
		      sensor->limits[SMIAPP_LIMIT_MIN_X_OUTPUT_SIZE],
		      sensor->limits[SMIAPP_LIMIT_MAX_X_OUTPUT_SIZE]);
	fmt->format.height =
		clamp(fmt->format.height,
		      sensor->limits[SMIAPP_LIMIT_MIN_Y_OUTPUT_SIZE],
		      sensor->limits[SMIAPP_LIMIT_MAX_Y_OUTPUT_SIZE]);

	smiapp_get_crop_compose(subdev, fh, crops, NULL, fmt->which);

	crops[ssd->sink_pad]->left = 0;
	crops[ssd->sink_pad]->top = 0;
	crops[ssd->sink_pad]->width = fmt->format.width;
	crops[ssd->sink_pad]->height = fmt->format.height;
	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		ssd->sink_fmt = *crops[ssd->sink_pad];
	smiapp_propagate(subdev, fh, fmt->which,
			 V4L2_SUBDEV_SEL_TGT_CROP_ACTUAL);

	mutex_unlock(&sensor->mutex);

	return 0;
}

/*
 * Calculate goodness of scaled image size compared to expected image
 * size and flags provided.
 */
#define SCALING_GOODNESS		100000
#define SCALING_GOODNESS_EXTREME	100000000
static int scaling_goodness(struct v4l2_subdev *subdev, int w, int ask_w,
			    int h, int ask_h, u32 flags)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	int val = 0;

	w &= ~1;
	ask_w &= ~1;
	h &= ~1;
	ask_h &= ~1;

	if (flags & V4L2_SUBDEV_SEL_FLAG_SIZE_GE) {
		if (w < ask_w)
			val -= SCALING_GOODNESS;
		if (h < ask_h)
			val -= SCALING_GOODNESS;
	}

	if (flags & V4L2_SUBDEV_SEL_FLAG_SIZE_LE) {
		if (w > ask_w)
			val -= SCALING_GOODNESS;
		if (h > ask_h)
			val -= SCALING_GOODNESS;
	}

	val -= abs(w - ask_w);
	val -= abs(h - ask_h);

	if (w < sensor->limits[SMIAPP_LIMIT_MIN_X_OUTPUT_SIZE])
		val -= SCALING_GOODNESS_EXTREME;

	dev_dbg(&client->dev, "w %d ask_w %d h %d ask_h %d goodness %d\n",
		w, ask_h, h, ask_h, val);

	return val;
}

static void smiapp_set_compose_binner(struct v4l2_subdev *subdev,
				      struct v4l2_subdev_fh *fh,
				      struct v4l2_subdev_selection *sel,
				      struct v4l2_rect **crops,
				      struct v4l2_rect *comp)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	unsigned int i;
	unsigned int binh = 1, binv = 1;
	unsigned int best = scaling_goodness(
		subdev,
		crops[SMIAPP_PAD_SINK]->width, sel->r.width,
		crops[SMIAPP_PAD_SINK]->height, sel->r.height, sel->flags);

	for (i = 0; i < sensor->nbinning_subtypes; i++) {
		int this = scaling_goodness(
			subdev,
			crops[SMIAPP_PAD_SINK]->width
			/ sensor->binning_subtypes[i].horizontal,
			sel->r.width,
			crops[SMIAPP_PAD_SINK]->height
			/ sensor->binning_subtypes[i].vertical,
			sel->r.height, sel->flags);

		if (this > best) {
			binh = sensor->binning_subtypes[i].horizontal;
			binv = sensor->binning_subtypes[i].vertical;
			best = this;
		}
	}
	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		sensor->binning_vertical = binv;
		sensor->binning_horizontal = binh;
	}

	sel->r.width = (crops[SMIAPP_PAD_SINK]->width / binh) & ~1;
	sel->r.height = (crops[SMIAPP_PAD_SINK]->height / binv) & ~1;
}

/*
 * Calculate best scaling ratio and mode for given output resolution.
 *
 * Try all of these: horizontal ratio, vertical ratio and smallest
 * size possible (horizontally).
 *
 * Also try whether horizontal scaler or full scaler gives a better
 * result.
 */
static void smiapp_set_compose_scaler(struct v4l2_subdev *subdev,
				      struct v4l2_subdev_fh *fh,
				      struct v4l2_subdev_selection *sel,
				      struct v4l2_rect **crops,
				      struct v4l2_rect *comp)
{
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	u32 min, max, a, b, max_m;
	u32 scale_m = sensor->limits[SMIAPP_LIMIT_SCALER_N_MIN];
	int mode = SMIAPP_SCALING_MODE_HORIZONTAL;
	u32 try[4];
	u32 ntry = 0;
	unsigned int i;
	int best = INT_MIN;

	sel->r.width = min_t(unsigned int, sel->r.width,
			     crops[SMIAPP_PAD_SINK]->width);
	sel->r.height = min_t(unsigned int, sel->r.height,
			      crops[SMIAPP_PAD_SINK]->height);

	a = crops[SMIAPP_PAD_SINK]->width
		* sensor->limits[SMIAPP_LIMIT_SCALER_N_MIN] / sel->r.width;
	b = crops[SMIAPP_PAD_SINK]->height
		* sensor->limits[SMIAPP_LIMIT_SCALER_N_MIN] / sel->r.height;
	max_m = crops[SMIAPP_PAD_SINK]->width
		* sensor->limits[SMIAPP_LIMIT_SCALER_N_MIN]
		/ sensor->limits[SMIAPP_LIMIT_MIN_X_OUTPUT_SIZE];

	a = min(sensor->limits[SMIAPP_LIMIT_SCALER_M_MAX],
		max(a, sensor->limits[SMIAPP_LIMIT_SCALER_M_MIN]));
	b = min(sensor->limits[SMIAPP_LIMIT_SCALER_M_MAX],
		max(b, sensor->limits[SMIAPP_LIMIT_SCALER_M_MIN]));
	max_m = min(sensor->limits[SMIAPP_LIMIT_SCALER_M_MAX],
		    max(max_m, sensor->limits[SMIAPP_LIMIT_SCALER_M_MIN]));

	dev_dbg(&client->dev, "scaling: a %d b %d max_m %d\n", a, b, max_m);

	min = min(max_m, min(a, b));
	max = min(max_m, max(a, b));

	try[ntry] = min;
	ntry++;
	if (min != max) {
		try[ntry] = max;
		ntry++;
	}
	if (max != max_m) {
		try[ntry] = min + 1;
		ntry++;
		if (min != max) {
			try[ntry] = max + 1;
			ntry++;
		}
	}

	for (i = 0; i < ntry; i++) {
		int this = scaling_goodness(
			subdev,
			crops[SMIAPP_PAD_SINK]->width
			/ try[i]
			* sensor->limits[SMIAPP_LIMIT_SCALER_N_MIN],
			sel->r.width,
			crops[SMIAPP_PAD_SINK]->height,
			sel->r.height,
			sel->flags);

		dev_dbg(&client->dev, "trying factor %d (%d)\n", try[i], i);

		if (this > best) {
			scale_m = try[i];
			mode = SMIAPP_SCALING_MODE_HORIZONTAL;
			best = this;
		}

		if (sensor->limits[SMIAPP_LIMIT_SCALING_CAPABILITY]
		    == SMIAPP_SCALING_CAPABILITY_HORIZONTAL)
			continue;

		this = scaling_goodness(
			subdev, crops[SMIAPP_PAD_SINK]->width
			/ try[i]
			* sensor->limits[SMIAPP_LIMIT_SCALER_N_MIN],
			sel->r.width,
			crops[SMIAPP_PAD_SINK]->height
			/ try[i]
			* sensor->limits[SMIAPP_LIMIT_SCALER_N_MIN],
			sel->r.height,
			sel->flags);

		if (this > best) {
			scale_m = try[i];
			mode = SMIAPP_SCALING_MODE_BOTH;
			best = this;
		}
	}

	sel->r.width =
		(crops[SMIAPP_PAD_SINK]->width
		 / scale_m
		 * sensor->limits[SMIAPP_LIMIT_SCALER_N_MIN]) & ~1;
	if (mode == SMIAPP_SCALING_MODE_BOTH)
		sel->r.height =
			(crops[SMIAPP_PAD_SINK]->height
			 / scale_m
			 * sensor->limits[SMIAPP_LIMIT_SCALER_N_MIN])
			& ~1;
	else
		sel->r.height = crops[SMIAPP_PAD_SINK]->height;

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		sensor->scale_m = scale_m;
		sensor->scaling_mode = mode;
	}
}
/* We're only called on source pads. This function sets scaling. */
static int smiapp_set_compose(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_selection *sel)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	struct smiapp_subdev *ssd = to_smiapp_subdev(subdev);
	struct v4l2_rect *comp, *crops[SMIAPP_PADS];

	smiapp_get_crop_compose(subdev, fh, crops, &comp, sel->which);

	sel->r.top = 0;
	sel->r.left = 0;

	if (ssd == sensor->binner)
		smiapp_set_compose_binner(subdev, fh, sel, crops, comp);
	else
		smiapp_set_compose_scaler(subdev, fh, sel, crops, comp);

	*comp = sel->r;
	smiapp_propagate(subdev, fh, sel->which,
			 V4L2_SUBDEV_SEL_TGT_COMPOSE_ACTUAL);

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		return smiapp_update_mode(sensor);

	return 0;
}

static int __smiapp_sel_supported(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_selection *sel)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	struct smiapp_subdev *ssd = to_smiapp_subdev(subdev);

	/* We only implement crop in three places. */
	switch (sel->target) {
	case V4L2_SUBDEV_SEL_TGT_CROP_ACTUAL:
	case V4L2_SUBDEV_SEL_TGT_CROP_BOUNDS:
		if (ssd == sensor->pixel_array
		    && sel->pad == SMIAPP_PA_PAD_SRC)
			return 0;
		if (ssd == sensor->src
		    && sel->pad == SMIAPP_PAD_SRC)
			return 0;
		if (ssd == sensor->scaler
		    && sel->pad == SMIAPP_PAD_SINK
		    && sensor->limits[SMIAPP_LIMIT_DIGITAL_CROP_CAPABILITY]
		    == SMIAPP_DIGITAL_CROP_CAPABILITY_INPUT_CROP)
			return 0;
		return -EINVAL;
	case V4L2_SUBDEV_SEL_TGT_COMPOSE_ACTUAL:
	case V4L2_SUBDEV_SEL_TGT_COMPOSE_BOUNDS:
		if (sel->pad == ssd->source_pad)
			return -EINVAL;
		if (ssd == sensor->binner)
			return 0;
		if (ssd == sensor->scaler
		    && sensor->limits[SMIAPP_LIMIT_SCALING_CAPABILITY]
		    != SMIAPP_SCALING_CAPABILITY_NONE)
			return 0;
		/* Fall through */
	default:
		return -EINVAL;
	}
}

static int smiapp_set_crop(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_fh *fh,
			   struct v4l2_subdev_selection *sel)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	struct smiapp_subdev *ssd = to_smiapp_subdev(subdev);
	struct v4l2_rect *src_size, *crops[SMIAPP_PADS];
	struct v4l2_rect _r;

	smiapp_get_crop_compose(subdev, fh, crops, NULL, sel->which);

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		if (sel->pad == ssd->sink_pad)
			src_size = &ssd->sink_fmt;
		else
			src_size = &ssd->compose;
	} else {
		if (sel->pad == ssd->sink_pad) {
			_r.left = 0;
			_r.top = 0;
			_r.width = v4l2_subdev_get_try_format(fh, sel->pad)
				->width;
			_r.height = v4l2_subdev_get_try_format(fh, sel->pad)
				->height;
			src_size = &_r;
		} else {
			src_size =
				v4l2_subdev_get_try_compose(
					fh, ssd->sink_pad);
		}
	}

	if (ssd == sensor->src && sel->pad == SMIAPP_PAD_SRC) {
		sel->r.left = 0;
		sel->r.top = 0;
	}

	sel->r.width = min(sel->r.width, src_size->width);
	sel->r.height = min(sel->r.height, src_size->height);

	sel->r.left = min(sel->r.left, src_size->width - sel->r.width);
	sel->r.top = min(sel->r.top, src_size->height - sel->r.height);

	*crops[sel->pad] = sel->r;

	if (ssd != sensor->pixel_array && sel->pad == SMIAPP_PAD_SINK)
		smiapp_propagate(subdev, fh, sel->which,
				 V4L2_SUBDEV_SEL_TGT_CROP_ACTUAL);

	return 0;
}

static int __smiapp_get_selection(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_selection *sel)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	struct smiapp_subdev *ssd = to_smiapp_subdev(subdev);
	struct v4l2_rect *comp, *crops[SMIAPP_PADS];
	struct v4l2_rect sink_fmt;
	int ret;

	ret = __smiapp_sel_supported(subdev, sel);
	if (ret)
		return ret;

	smiapp_get_crop_compose(subdev, fh, crops, &comp, sel->which);

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		sink_fmt = ssd->sink_fmt;
	} else {
		struct v4l2_mbus_framefmt *fmt =
			v4l2_subdev_get_try_format(fh, ssd->sink_pad);

		sink_fmt.left = 0;
		sink_fmt.top = 0;
		sink_fmt.width = fmt->width;
		sink_fmt.height = fmt->height;
	}

	switch (sel->target) {
	case V4L2_SUBDEV_SEL_TGT_CROP_BOUNDS:
		if (ssd == sensor->pixel_array) {
			sel->r.width =
				sensor->limits[SMIAPP_LIMIT_X_ADDR_MAX] + 1;
			sel->r.height =
				sensor->limits[SMIAPP_LIMIT_Y_ADDR_MAX] + 1;
		} else if (sel->pad == ssd->sink_pad) {
			sel->r = sink_fmt;
		} else {
			sel->r = *comp;
		}
		break;
	case V4L2_SUBDEV_SEL_TGT_CROP_ACTUAL:
	case V4L2_SUBDEV_SEL_TGT_COMPOSE_BOUNDS:
		sel->r = *crops[sel->pad];
		break;
	case V4L2_SUBDEV_SEL_TGT_COMPOSE_ACTUAL:
		sel->r = *comp;
		break;
	}

	return 0;
}

static int smiapp_get_selection(struct v4l2_subdev *subdev,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_selection *sel)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	int rval;

	mutex_lock(&sensor->mutex);
	rval = __smiapp_get_selection(subdev, fh, sel);
	mutex_unlock(&sensor->mutex);

	return rval;
}
static int smiapp_set_selection(struct v4l2_subdev *subdev,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_selection *sel)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	int ret;

	ret = __smiapp_sel_supported(subdev, sel);
	if (ret)
		return ret;

	mutex_lock(&sensor->mutex);

	sel->r.left = max(0, sel->r.left & ~1);
	sel->r.top = max(0, sel->r.top & ~1);
	sel->r.width = max(0, SMIAPP_ALIGN_DIM(sel->r.width, sel->flags));
	sel->r.height = max(0, SMIAPP_ALIGN_DIM(sel->r.height, sel->flags));

	sel->r.width = max_t(unsigned int,
			     sensor->limits[SMIAPP_LIMIT_MIN_X_OUTPUT_SIZE],
			     sel->r.width);
	sel->r.height = max_t(unsigned int,
			      sensor->limits[SMIAPP_LIMIT_MIN_Y_OUTPUT_SIZE],
			      sel->r.height);

	switch (sel->target) {
	case V4L2_SUBDEV_SEL_TGT_CROP_ACTUAL:
		ret = smiapp_set_crop(subdev, fh, sel);
		break;
	case V4L2_SUBDEV_SEL_TGT_COMPOSE_ACTUAL:
		ret = smiapp_set_compose(subdev, fh, sel);
		break;
	default:
		BUG();
	}

	mutex_unlock(&sensor->mutex);
	return ret;
}

static int smiapp_get_skip_frames(struct v4l2_subdev *subdev, u32 *frames)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);

	*frames = sensor->frame_skip;
	return 0;
}

/* -----------------------------------------------------------------------------
 * sysfs attributes
 */

static ssize_t
smiapp_sysfs_nvm_read(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(to_i2c_client(dev));
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	unsigned int nbytes;

	if (!sensor->dev_init_done)
		return -EBUSY;

	if (!sensor->nvm_size) {
		/* NVM not read yet - read it now */
		sensor->nvm_size = sensor->platform_data->nvm_size;
		if (smiapp_set_power(subdev, 1) < 0)
			return -ENODEV;
		if (smiapp_read_nvm(sensor, sensor->nvm)) {
			dev_err(&client->dev, "nvm read failed\n");
			return -ENODEV;
		}
		smiapp_set_power(subdev, 0);
	}
	/*
	 * NVM is still way below a PAGE_SIZE, so we can safely
	 * assume this for now.
	 */
	nbytes = min_t(unsigned int, sensor->nvm_size, PAGE_SIZE);
	memcpy(buf, sensor->nvm, nbytes);

	return nbytes;
}
static DEVICE_ATTR(nvm, S_IRUGO, smiapp_sysfs_nvm_read, NULL);

/* -----------------------------------------------------------------------------
 * V4L2 subdev core operations
 */

static int smiapp_identify_module(struct v4l2_subdev *subdev)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct smiapp_module_info *minfo = &sensor->minfo;
	unsigned int i;
	int rval = 0;

	minfo->name = SMIAPP_NAME;

	/* Module info */
	rval = smiapp_read_8only(sensor, SMIAPP_REG_U8_MANUFACTURER_ID,
				 &minfo->manufacturer_id);
	if (!rval)
		rval = smiapp_read_8only(sensor, SMIAPP_REG_U16_MODEL_ID,
					 &minfo->model_id);
	if (!rval)
		rval = smiapp_read_8only(sensor,
					 SMIAPP_REG_U8_REVISION_NUMBER_MAJOR,
					 &minfo->revision_number_major);
	if (!rval)
		rval = smiapp_read_8only(sensor,
					 SMIAPP_REG_U8_REVISION_NUMBER_MINOR,
					 &minfo->revision_number_minor);
	if (!rval)
		rval = smiapp_read_8only(sensor,
					 SMIAPP_REG_U8_MODULE_DATE_YEAR,
					 &minfo->module_year);
	if (!rval)
		rval = smiapp_read_8only(sensor,
					 SMIAPP_REG_U8_MODULE_DATE_MONTH,
					 &minfo->module_month);
	if (!rval)
		rval = smiapp_read_8only(sensor, SMIAPP_REG_U8_MODULE_DATE_DAY,
					 &minfo->module_day);

	/* Sensor info */
	if (!rval)
		rval = smiapp_read_8only(sensor,
					 SMIAPP_REG_U8_SENSOR_MANUFACTURER_ID,
					 &minfo->sensor_manufacturer_id);
	if (!rval)
		rval = smiapp_read_8only(sensor,
					 SMIAPP_REG_U16_SENSOR_MODEL_ID,
					 &minfo->sensor_model_id);
	if (!rval)
		rval = smiapp_read_8only(sensor,
					 SMIAPP_REG_U8_SENSOR_REVISION_NUMBER,
					 &minfo->sensor_revision_number);
	if (!rval)
		rval = smiapp_read_8only(sensor,
					 SMIAPP_REG_U8_SENSOR_FIRMWARE_VERSION,
					 &minfo->sensor_firmware_version);

	/* SMIA */
	if (!rval)
		rval = smiapp_read_8only(sensor, SMIAPP_REG_U8_SMIA_VERSION,
					 &minfo->smia_version);
	if (!rval)
		rval = smiapp_read_8only(sensor, SMIAPP_REG_U8_SMIAPP_VERSION,
					 &minfo->smiapp_version);

	if (rval) {
		dev_err(&client->dev, "sensor detection failed\n");
		return -ENODEV;
	}

	dev_dbg(&client->dev, "module 0x%2.2x-0x%4.4x\n",
		minfo->manufacturer_id, minfo->model_id);

	dev_dbg(&client->dev,
		"module revision 0x%2.2x-0x%2.2x date %2.2d-%2.2d-%2.2d\n",
		minfo->revision_number_major, minfo->revision_number_minor,
		minfo->module_year, minfo->module_month, minfo->module_day);

	dev_dbg(&client->dev, "sensor 0x%2.2x-0x%4.4x\n",
		minfo->sensor_manufacturer_id, minfo->sensor_model_id);

	dev_dbg(&client->dev,
		"sensor revision 0x%2.2x firmware version 0x%2.2x\n",
		minfo->sensor_revision_number, minfo->sensor_firmware_version);

	dev_dbg(&client->dev, "smia version %2.2d smiapp version %2.2d\n",
		minfo->smia_version, minfo->smiapp_version);

	/*
	 * Some modules have bad data in the lvalues below. Hope the
	 * rvalues have better stuff. The lvalues are module
	 * parameters whereas the rvalues are sensor parameters.
	 */
	if (!minfo->manufacturer_id && !minfo->model_id) {
		minfo->manufacturer_id = minfo->sensor_manufacturer_id;
		minfo->model_id = minfo->sensor_model_id;
		minfo->revision_number_major = minfo->sensor_revision_number;
	}

	for (i = 0; i < ARRAY_SIZE(smiapp_module_idents); i++) {
		if (smiapp_module_idents[i].manufacturer_id
		    != minfo->manufacturer_id)
			continue;
		if (smiapp_module_idents[i].model_id != minfo->model_id)
			continue;
		if (smiapp_module_idents[i].flags
		    & SMIAPP_MODULE_IDENT_FLAG_REV_LE) {
			if (smiapp_module_idents[i].revision_number_major
			    < minfo->revision_number_major)
				continue;
		} else {
			if (smiapp_module_idents[i].revision_number_major
			    != minfo->revision_number_major)
				continue;
		}

		minfo->name = smiapp_module_idents[i].name;
		minfo->quirk = smiapp_module_idents[i].quirk;
		break;
	}

	if (i >= ARRAY_SIZE(smiapp_module_idents))
		dev_warn(&client->dev,
			 "no quirks for this module; let's hope it's fully compliant\n");

	dev_dbg(&client->dev, "the sensor is called %s, ident %2.2x%4.4x%2.2x\n",
		minfo->name, minfo->manufacturer_id, minfo->model_id,
		minfo->revision_number_major);

	strlcpy(subdev->name, sensor->minfo.name, sizeof(subdev->name));

	return 0;
}

static const struct v4l2_subdev_ops smiapp_ops;
static const struct v4l2_subdev_internal_ops smiapp_internal_ops;
static const struct media_entity_operations smiapp_entity_ops;

static int smiapp_registered(struct v4l2_subdev *subdev)
{
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct smiapp_subdev *last = NULL;
	u32 tmp;
	unsigned int i;
	int rval;

	sensor->vana = regulator_get(&client->dev, "VANA");
	if (IS_ERR(sensor->vana)) {
		dev_err(&client->dev, "could not get regulator for vana\n");
		return -ENODEV;
	}

	if (!sensor->platform_data->set_xclk) {
		sensor->ext_clk = clk_get(&client->dev,
					  sensor->platform_data->ext_clk_name);
		if (IS_ERR(sensor->ext_clk)) {
			dev_err(&client->dev, "could not get clock %s\n",
				sensor->platform_data->ext_clk_name);
			rval = -ENODEV;
			goto out_clk_get;
		}

		rval = clk_set_rate(sensor->ext_clk,
				    sensor->platform_data->ext_clk);
		if (rval < 0) {
			dev_err(&client->dev,
				"unable to set clock %s freq to %u\n",
				sensor->platform_data->ext_clk_name,
				sensor->platform_data->ext_clk);
			rval = -ENODEV;
			goto out_clk_set_rate;
		}
	}

	if (sensor->platform_data->xshutdown != SMIAPP_NO_XSHUTDOWN) {
		if (gpio_request_one(sensor->platform_data->xshutdown, 0,
				     "SMIA++ xshutdown") != 0) {
			dev_err(&client->dev,
				"unable to acquire reset gpio %d\n",
				sensor->platform_data->xshutdown);
			rval = -ENODEV;
			goto out_clk_set_rate;
		}
	}

	rval = smiapp_power_on(sensor);
	if (rval) {
		rval = -ENODEV;
		goto out_smiapp_power_on;
	}

	rval = smiapp_identify_module(subdev);
	if (rval) {
		rval = -ENODEV;
		goto out_power_off;
	}

	rval = smiapp_get_all_limits(sensor);
	if (rval) {
		rval = -ENODEV;
		goto out_power_off;
	}

	/*
	 * Handle Sensor Module orientation on the board.
	 *
	 * The application of H-FLIP and V-FLIP on the sensor is modified by
	 * the sensor orientation on the board.
	 *
	 * For SMIAPP_BOARD_SENSOR_ORIENT_180 the default behaviour is to set
	 * both H-FLIP and V-FLIP for normal operation which also implies
	 * that a set/unset operation for user space HFLIP and VFLIP v4l2
	 * controls will need to be internally inverted.
	 *
	 * Rotation also changes the bayer pattern.
	 */
	if (sensor->platform_data->module_board_orient ==
	    SMIAPP_MODULE_BOARD_ORIENT_180)
		sensor->hvflip_inv_mask = SMIAPP_IMAGE_ORIENTATION_HFLIP |
					  SMIAPP_IMAGE_ORIENTATION_VFLIP;

	rval = smiapp_get_mbus_formats(sensor);
	if (rval) {
		rval = -ENODEV;
		goto out_power_off;
	}

	if (sensor->limits[SMIAPP_LIMIT_BINNING_CAPABILITY]) {
		u32 val;

		rval = smiapp_read(sensor,
				   SMIAPP_REG_U8_BINNING_SUBTYPES, &val);
		if (rval < 0) {
			rval = -ENODEV;
			goto out_power_off;
		}
		sensor->nbinning_subtypes = min_t(u8, val,
						  SMIAPP_BINNING_SUBTYPES);

		for (i = 0; i < sensor->nbinning_subtypes; i++) {
			rval = smiapp_read(
				sensor, SMIAPP_REG_U8_BINNING_TYPE_n(i), &val);
			if (rval < 0) {
				rval = -ENODEV;
				goto out_power_off;
			}
			sensor->binning_subtypes[i] =
				*(struct smiapp_binning_subtype *)&val;

			dev_dbg(&client->dev, "binning %xx%x\n",
				sensor->binning_subtypes[i].horizontal,
				sensor->binning_subtypes[i].vertical);
		}
	}
	sensor->binning_horizontal = 1;
	sensor->binning_vertical = 1;

	/* SMIA++ NVM initialization - it will be read from the sensor
	 * when it is first requested by userspace.
	 */
	if (sensor->minfo.smiapp_version && sensor->platform_data->nvm_size) {
		sensor->nvm = kzalloc(sensor->platform_data->nvm_size,
				      GFP_KERNEL);
		if (sensor->nvm == NULL) {
			dev_err(&client->dev, "nvm buf allocation failed\n");
			rval = -ENOMEM;
			goto out_power_off;
		}

		if (device_create_file(&client->dev, &dev_attr_nvm) != 0) {
			dev_err(&client->dev, "sysfs nvm entry failed\n");
			rval = -EBUSY;
			goto out_power_off;
		}
	}

	rval = smiapp_call_quirk(sensor, limits);
	if (rval) {
		dev_err(&client->dev, "limits quirks failed\n");
		goto out_nvm_release;
	}

	/* We consider this as profile 0 sensor if any of these are zero. */
	if (!sensor->limits[SMIAPP_LIMIT_MIN_OP_SYS_CLK_DIV] ||
	    !sensor->limits[SMIAPP_LIMIT_MAX_OP_SYS_CLK_DIV] ||
	    !sensor->limits[SMIAPP_LIMIT_MIN_OP_PIX_CLK_DIV] ||
	    !sensor->limits[SMIAPP_LIMIT_MAX_OP_PIX_CLK_DIV]) {
		sensor->minfo.smiapp_profile = SMIAPP_PROFILE_0;
	} else if (sensor->limits[SMIAPP_LIMIT_SCALING_CAPABILITY]
		   != SMIAPP_SCALING_CAPABILITY_NONE) {
		if (sensor->limits[SMIAPP_LIMIT_SCALING_CAPABILITY]
		    == SMIAPP_SCALING_CAPABILITY_HORIZONTAL)
			sensor->minfo.smiapp_profile = SMIAPP_PROFILE_1;
		else
			sensor->minfo.smiapp_profile = SMIAPP_PROFILE_2;
		sensor->scaler = &sensor->ssds[sensor->ssds_used];
		sensor->ssds_used++;
	} else if (sensor->limits[SMIAPP_LIMIT_DIGITAL_CROP_CAPABILITY]
		   == SMIAPP_DIGITAL_CROP_CAPABILITY_INPUT_CROP) {
		sensor->scaler = &sensor->ssds[sensor->ssds_used];
		sensor->ssds_used++;
	}
	sensor->binner = &sensor->ssds[sensor->ssds_used];
	sensor->ssds_used++;
	sensor->pixel_array = &sensor->ssds[sensor->ssds_used];
	sensor->ssds_used++;

	sensor->scale_m = sensor->limits[SMIAPP_LIMIT_SCALER_N_MIN];

	for (i = 0; i < SMIAPP_SUBDEVS; i++) {
		struct {
			struct smiapp_subdev *ssd;
			char *name;
		} const __this[] = {
			{ sensor->scaler, "scaler", },
			{ sensor->binner, "binner", },
			{ sensor->pixel_array, "pixel array", },
		}, *_this = &__this[i];
		struct smiapp_subdev *this = _this->ssd;

		if (!this)
			continue;

		if (this != sensor->src)
			v4l2_subdev_init(&this->sd, &smiapp_ops);

		this->sensor = sensor;

		if (this == sensor->pixel_array) {
			this->npads = 1;
		} else {
			this->npads = 2;
			this->source_pad = 1;
		}

		snprintf(this->sd.name,
			 sizeof(this->sd.name), "%s %s",
			 sensor->minfo.name, _this->name);

		this->sink_fmt.width =
			sensor->limits[SMIAPP_LIMIT_X_ADDR_MAX] + 1;
		this->sink_fmt.height =
			sensor->limits[SMIAPP_LIMIT_Y_ADDR_MAX] + 1;
		this->compose.width = this->sink_fmt.width;
		this->compose.height = this->sink_fmt.height;
		this->crop[this->source_pad] = this->compose;
		this->pads[this->source_pad].flags = MEDIA_PAD_FL_SOURCE;
		if (this != sensor->pixel_array) {
			this->crop[this->sink_pad] = this->compose;
			this->pads[this->sink_pad].flags = MEDIA_PAD_FL_SINK;
		}

		this->sd.entity.ops = &smiapp_entity_ops;

		if (last == NULL) {
			last = this;
			continue;
		}

		this->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
		this->sd.internal_ops = &smiapp_internal_ops;
		this->sd.owner = NULL;
		v4l2_set_subdevdata(&this->sd, client);

		rval = media_entity_init(&this->sd.entity,
					 this->npads, this->pads, 0);
		if (rval) {
			dev_err(&client->dev,
				"media_entity_init failed\n");
			goto out_nvm_release;
		}

		rval = media_entity_create_link(&this->sd.entity,
						this->source_pad,
						&last->sd.entity,
						last->sink_pad,
						MEDIA_LNK_FL_ENABLED |
						MEDIA_LNK_FL_IMMUTABLE);
		if (rval) {
			dev_err(&client->dev,
				"media_entity_create_link failed\n");
			goto out_nvm_release;
		}

		rval = v4l2_device_register_subdev(sensor->src->sd.v4l2_dev,
						   &this->sd);
		if (rval) {
			dev_err(&client->dev,
				"v4l2_device_register_subdev failed\n");
			goto out_nvm_release;
		}

		last = this;
	}

	dev_dbg(&client->dev, "profile %d\n", sensor->minfo.smiapp_profile);

	sensor->pixel_array->sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;

	/* final steps */
	smiapp_read_frame_fmt(sensor);
	rval = smiapp_init_controls(sensor);
	if (rval < 0)
		goto out_nvm_release;

	rval = smiapp_update_mode(sensor);
	if (rval) {
		dev_err(&client->dev, "update mode failed\n");
		goto out_nvm_release;
	}

	sensor->streaming = false;
	sensor->dev_init_done = true;

	/* check flash capability */
	rval = smiapp_read(sensor, SMIAPP_REG_U8_FLASH_MODE_CAPABILITY, &tmp);
	sensor->flash_capability = tmp;
	if (rval)
		goto out_nvm_release;

	smiapp_power_off(sensor);

	return 0;

out_nvm_release:
	device_remove_file(&client->dev, &dev_attr_nvm);

out_power_off:
	kfree(sensor->nvm);
	sensor->nvm = NULL;
	smiapp_power_off(sensor);

out_smiapp_power_on:
	if (sensor->platform_data->xshutdown != SMIAPP_NO_XSHUTDOWN)
		gpio_free(sensor->platform_data->xshutdown);

out_clk_set_rate:
	clk_put(sensor->ext_clk);
	sensor->ext_clk = NULL;

out_clk_get:
	regulator_put(sensor->vana);
	sensor->vana = NULL;
	return rval;
}

static int smiapp_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct smiapp_subdev *ssd = to_smiapp_subdev(sd);
	struct smiapp_sensor *sensor = ssd->sensor;
	u32 mbus_code =
		smiapp_csi_data_formats[smiapp_pixel_order(sensor)].code;
	unsigned int i;

	mutex_lock(&sensor->mutex);

	for (i = 0; i < ssd->npads; i++) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_get_try_format(fh, i);
		struct v4l2_rect *try_crop = v4l2_subdev_get_try_crop(fh, i);
		struct v4l2_rect *try_comp;

		try_fmt->width = sensor->limits[SMIAPP_LIMIT_X_ADDR_MAX] + 1;
		try_fmt->height = sensor->limits[SMIAPP_LIMIT_Y_ADDR_MAX] + 1;
		try_fmt->code = mbus_code;

		try_crop->top = 0;
		try_crop->left = 0;
		try_crop->width = try_fmt->width;
		try_crop->height = try_fmt->height;

		if (ssd != sensor->pixel_array)
			continue;

		try_comp = v4l2_subdev_get_try_compose(fh, i);
		*try_comp = *try_crop;
	}

	mutex_unlock(&sensor->mutex);

	return smiapp_set_power(sd, 1);
}

static int smiapp_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return smiapp_set_power(sd, 0);
}

static const struct v4l2_subdev_video_ops smiapp_video_ops = {
	.s_stream = smiapp_set_stream,
};

static const struct v4l2_subdev_core_ops smiapp_core_ops = {
	.s_power = smiapp_set_power,
};

static const struct v4l2_subdev_pad_ops smiapp_pad_ops = {
	.enum_mbus_code = smiapp_enum_mbus_code,
	.get_fmt = smiapp_get_format,
	.set_fmt = smiapp_set_format,
	.get_selection = smiapp_get_selection,
	.set_selection = smiapp_set_selection,
};

static const struct v4l2_subdev_sensor_ops smiapp_sensor_ops = {
	.g_skip_frames = smiapp_get_skip_frames,
};

static const struct v4l2_subdev_ops smiapp_ops = {
	.core = &smiapp_core_ops,
	.video = &smiapp_video_ops,
	.pad = &smiapp_pad_ops,
	.sensor = &smiapp_sensor_ops,
};

static const struct media_entity_operations smiapp_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops smiapp_internal_src_ops = {
	.registered = smiapp_registered,
	.open = smiapp_open,
	.close = smiapp_close,
};

static const struct v4l2_subdev_internal_ops smiapp_internal_ops = {
	.open = smiapp_open,
	.close = smiapp_close,
};

/* -----------------------------------------------------------------------------
 * I2C Driver
 */

#ifdef CONFIG_PM

static int smiapp_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	bool streaming;

	BUG_ON(mutex_is_locked(&sensor->mutex));

	if (sensor->power_count == 0)
		return 0;

	if (sensor->streaming)
		smiapp_stop_streaming(sensor);

	streaming = sensor->streaming;

	smiapp_power_off(sensor);

	/* save state for resume */
	sensor->streaming = streaming;

	return 0;
}

static int smiapp_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	int rval;

	if (sensor->power_count == 0)
		return 0;

	rval = smiapp_power_on(sensor);
	if (rval)
		return rval;

	if (sensor->streaming)
		rval = smiapp_start_streaming(sensor);

	return rval;
}

#else

#define smiapp_suspend	NULL
#define smiapp_resume	NULL

#endif /* CONFIG_PM */

static int smiapp_probe(struct i2c_client *client,
			const struct i2c_device_id *devid)
{
	struct smiapp_sensor *sensor;
	int rval;

	if (client->dev.platform_data == NULL)
		return -ENODEV;

	sensor = kzalloc(sizeof(*sensor), GFP_KERNEL);
	if (sensor == NULL)
		return -ENOMEM;

	sensor->platform_data = client->dev.platform_data;
	mutex_init(&sensor->mutex);
	mutex_init(&sensor->power_mutex);
	sensor->src = &sensor->ssds[sensor->ssds_used];

	v4l2_i2c_subdev_init(&sensor->src->sd, client, &smiapp_ops);
	sensor->src->sd.internal_ops = &smiapp_internal_src_ops;
	sensor->src->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->src->sensor = sensor;

	sensor->src->pads[0].flags = MEDIA_PAD_FL_SOURCE;
	rval = media_entity_init(&sensor->src->sd.entity, 2,
				 sensor->src->pads, 0);
	if (rval < 0)
		kfree(sensor);

	return rval;
}

static int __exit smiapp_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct smiapp_sensor *sensor = to_smiapp_sensor(subdev);
	unsigned int i;

	if (sensor->power_count) {
		if (sensor->platform_data->xshutdown != SMIAPP_NO_XSHUTDOWN)
			gpio_set_value(sensor->platform_data->xshutdown, 0);
		if (sensor->platform_data->set_xclk)
			sensor->platform_data->set_xclk(&sensor->src->sd, 0);
		else
			clk_disable(sensor->ext_clk);
		sensor->power_count = 0;
	}

	if (sensor->nvm) {
		device_remove_file(&client->dev, &dev_attr_nvm);
		kfree(sensor->nvm);
	}

	for (i = 0; i < sensor->ssds_used; i++) {
		media_entity_cleanup(&sensor->ssds[i].sd.entity);
		v4l2_device_unregister_subdev(&sensor->ssds[i].sd);
	}
	smiapp_free_controls(sensor);
	if (sensor->platform_data->xshutdown != SMIAPP_NO_XSHUTDOWN)
		gpio_free(sensor->platform_data->xshutdown);
	if (sensor->ext_clk)
		clk_put(sensor->ext_clk);
	if (sensor->vana)
		regulator_put(sensor->vana);

	kfree(sensor);

	return 0;
}

static const struct i2c_device_id smiapp_id_table[] = {
	{ SMIAPP_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, smiapp_id_table);

static const struct dev_pm_ops smiapp_pm_ops = {
	.suspend	= smiapp_suspend,
	.resume		= smiapp_resume,
};

static struct i2c_driver smiapp_i2c_driver = {
	.driver	= {
		.name = SMIAPP_NAME,
		.pm = &smiapp_pm_ops,
	},
	.probe	= smiapp_probe,
	.remove	= __exit_p(smiapp_remove),
	.id_table = smiapp_id_table,
};

module_i2c_driver(smiapp_i2c_driver);

MODULE_AUTHOR("Sakari Ailus <sakari.ailus@maxwell.research.nokia.com>");
MODULE_DESCRIPTION("Generic SMIA/SMIA++ camera module driver");
MODULE_LICENSE("GPL");
