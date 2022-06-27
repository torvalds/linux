// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/media/i2c/ccs/ccs-core.c
 *
 * Generic driver for MIPI CCS/SMIA/SMIA++ compliant camera sensors
 *
 * Copyright (C) 2020 Intel Corporation
 * Copyright (C) 2010--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@linux.intel.com>
 *
 * Based on smiapp driver by Vimarsh Zutshi
 * Based on jt8ev1.c by Vimarsh Zutshi
 * Based on smia-sensor.c by Tuukka Toivonen <tuukkat76@gmail.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/smiapp.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-device.h>
#include <uapi/linux/ccs.h>

#include "ccs.h"

#define CCS_ALIGN_DIM(dim, flags)	\
	((flags) & V4L2_SEL_FLAG_GE	\
	 ? ALIGN((dim), 2)		\
	 : (dim) & ~1)

static struct ccs_limit_offset {
	u16	lim;
	u16	info;
} ccs_limit_offsets[CCS_L_LAST + 1];

/*
 * ccs_module_idents - supported camera modules
 */
static const struct ccs_module_ident ccs_module_idents[] = {
	CCS_IDENT_L(0x01, 0x022b, -1, "vs6555"),
	CCS_IDENT_L(0x01, 0x022e, -1, "vw6558"),
	CCS_IDENT_L(0x07, 0x7698, -1, "ovm7698"),
	CCS_IDENT_L(0x0b, 0x4242, -1, "smiapp-003"),
	CCS_IDENT_L(0x0c, 0x208a, -1, "tcm8330md"),
	CCS_IDENT_LQ(0x0c, 0x2134, -1, "tcm8500md", &smiapp_tcm8500md_quirk),
	CCS_IDENT_L(0x0c, 0x213e, -1, "et8en2"),
	CCS_IDENT_L(0x0c, 0x2184, -1, "tcm8580md"),
	CCS_IDENT_LQ(0x0c, 0x560f, -1, "jt8ew9", &smiapp_jt8ew9_quirk),
	CCS_IDENT_LQ(0x10, 0x4141, -1, "jt8ev1", &smiapp_jt8ev1_quirk),
	CCS_IDENT_LQ(0x10, 0x4241, -1, "imx125es", &smiapp_imx125es_quirk),
};

#define CCS_DEVICE_FLAG_IS_SMIA		BIT(0)

struct ccs_device {
	unsigned char flags;
};

static const char * const ccs_regulators[] = { "vcore", "vio", "vana" };

/*
 *
 * Dynamic Capability Identification
 *
 */

static void ccs_assign_limit(void *ptr, unsigned int width, u32 val)
{
	switch (width) {
	case sizeof(u8):
		*(u8 *)ptr = val;
		break;
	case sizeof(u16):
		*(u16 *)ptr = val;
		break;
	case sizeof(u32):
		*(u32 *)ptr = val;
		break;
	}
}

static int ccs_limit_ptr(struct ccs_sensor *sensor, unsigned int limit,
			 unsigned int offset, void **__ptr)
{
	const struct ccs_limit *linfo;

	if (WARN_ON(limit >= CCS_L_LAST))
		return -EINVAL;

	linfo = &ccs_limits[ccs_limit_offsets[limit].info];

	if (WARN_ON(!sensor->ccs_limits) ||
	    WARN_ON(offset + ccs_reg_width(linfo->reg) >
		    ccs_limit_offsets[limit + 1].lim))
		return -EINVAL;

	*__ptr = sensor->ccs_limits + ccs_limit_offsets[limit].lim + offset;

	return 0;
}

void ccs_replace_limit(struct ccs_sensor *sensor,
		       unsigned int limit, unsigned int offset, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	const struct ccs_limit *linfo;
	void *ptr;
	int ret;

	ret = ccs_limit_ptr(sensor, limit, offset, &ptr);
	if (ret)
		return;

	linfo = &ccs_limits[ccs_limit_offsets[limit].info];

	dev_dbg(&client->dev, "quirk: 0x%8.8x \"%s\" %u = %d, 0x%x\n",
		linfo->reg, linfo->name, offset, val, val);

	ccs_assign_limit(ptr, ccs_reg_width(linfo->reg), val);
}

u32 ccs_get_limit(struct ccs_sensor *sensor, unsigned int limit,
		  unsigned int offset)
{
	void *ptr;
	u32 val;
	int ret;

	ret = ccs_limit_ptr(sensor, limit, offset, &ptr);
	if (ret)
		return 0;

	switch (ccs_reg_width(ccs_limits[ccs_limit_offsets[limit].info].reg)) {
	case sizeof(u8):
		val = *(u8 *)ptr;
		break;
	case sizeof(u16):
		val = *(u16 *)ptr;
		break;
	case sizeof(u32):
		val = *(u32 *)ptr;
		break;
	default:
		WARN_ON(1);
		return 0;
	}

	return ccs_reg_conv(sensor, ccs_limits[limit].reg, val);
}

static int ccs_read_all_limits(struct ccs_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	void *ptr, *alloc, *end;
	unsigned int i, l;
	int ret;

	kfree(sensor->ccs_limits);
	sensor->ccs_limits = NULL;

	alloc = kzalloc(ccs_limit_offsets[CCS_L_LAST].lim, GFP_KERNEL);
	if (!alloc)
		return -ENOMEM;

	end = alloc + ccs_limit_offsets[CCS_L_LAST].lim;

	for (i = 0, l = 0, ptr = alloc; ccs_limits[i].size; i++) {
		u32 reg = ccs_limits[i].reg;
		unsigned int width = ccs_reg_width(reg);
		unsigned int j;

		if (l == CCS_L_LAST) {
			dev_err(&client->dev,
				"internal error --- end of limit array\n");
			ret = -EINVAL;
			goto out_err;
		}

		for (j = 0; j < ccs_limits[i].size / width;
		     j++, reg += width, ptr += width) {
			u32 val;

			ret = ccs_read_addr_noconv(sensor, reg, &val);
			if (ret)
				goto out_err;

			if (ptr + width > end) {
				dev_err(&client->dev,
					"internal error --- no room for regs\n");
				ret = -EINVAL;
				goto out_err;
			}

			if (!val && j)
				break;

			ccs_assign_limit(ptr, width, val);

			dev_dbg(&client->dev, "0x%8.8x \"%s\" = %u, 0x%x\n",
				reg, ccs_limits[i].name, val, val);
		}

		if (ccs_limits[i].flags & CCS_L_FL_SAME_REG)
			continue;

		l++;
		ptr = alloc + ccs_limit_offsets[l].lim;
	}

	if (l != CCS_L_LAST) {
		dev_err(&client->dev,
			"internal error --- insufficient limits\n");
		ret = -EINVAL;
		goto out_err;
	}

	sensor->ccs_limits = alloc;

	if (CCS_LIM(sensor, SCALER_N_MIN) < 16)
		ccs_replace_limit(sensor, CCS_L_SCALER_N_MIN, 0, 16);

	return 0;

out_err:
	kfree(alloc);

	return ret;
}

static int ccs_read_frame_fmt(struct ccs_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	u8 fmt_model_type, fmt_model_subtype, ncol_desc, nrow_desc;
	unsigned int i;
	int pixel_count = 0;
	int line_count = 0;

	fmt_model_type = CCS_LIM(sensor, FRAME_FORMAT_MODEL_TYPE);
	fmt_model_subtype = CCS_LIM(sensor, FRAME_FORMAT_MODEL_SUBTYPE);

	ncol_desc = (fmt_model_subtype
		     & CCS_FRAME_FORMAT_MODEL_SUBTYPE_COLUMNS_MASK)
		>> CCS_FRAME_FORMAT_MODEL_SUBTYPE_COLUMNS_SHIFT;
	nrow_desc = fmt_model_subtype
		& CCS_FRAME_FORMAT_MODEL_SUBTYPE_ROWS_MASK;

	dev_dbg(&client->dev, "format_model_type %s\n",
		fmt_model_type == CCS_FRAME_FORMAT_MODEL_TYPE_2_BYTE
		? "2 byte" :
		fmt_model_type == CCS_FRAME_FORMAT_MODEL_TYPE_4_BYTE
		? "4 byte" : "is simply bad");

	dev_dbg(&client->dev, "%u column and %u row descriptors\n",
		ncol_desc, nrow_desc);

	for (i = 0; i < ncol_desc + nrow_desc; i++) {
		u32 desc;
		u32 pixelcode;
		u32 pixels;
		char *which;
		char *what;

		if (fmt_model_type == CCS_FRAME_FORMAT_MODEL_TYPE_2_BYTE) {
			desc = CCS_LIM_AT(sensor, FRAME_FORMAT_DESCRIPTOR, i);

			pixelcode =
				(desc
				 & CCS_FRAME_FORMAT_DESCRIPTOR_PCODE_MASK)
				>> CCS_FRAME_FORMAT_DESCRIPTOR_PCODE_SHIFT;
			pixels = desc & CCS_FRAME_FORMAT_DESCRIPTOR_PIXELS_MASK;
		} else if (fmt_model_type
			   == CCS_FRAME_FORMAT_MODEL_TYPE_4_BYTE) {
			desc = CCS_LIM_AT(sensor, FRAME_FORMAT_DESCRIPTOR_4, i);

			pixelcode =
				(desc
				 & CCS_FRAME_FORMAT_DESCRIPTOR_4_PCODE_MASK)
				>> CCS_FRAME_FORMAT_DESCRIPTOR_4_PCODE_SHIFT;
			pixels = desc &
				CCS_FRAME_FORMAT_DESCRIPTOR_4_PIXELS_MASK;
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
		case CCS_FRAME_FORMAT_DESCRIPTOR_PCODE_EMBEDDED:
			what = "embedded";
			break;
		case CCS_FRAME_FORMAT_DESCRIPTOR_PCODE_DUMMY_PIXEL:
			what = "dummy";
			break;
		case CCS_FRAME_FORMAT_DESCRIPTOR_PCODE_BLACK_PIXEL:
			what = "black";
			break;
		case CCS_FRAME_FORMAT_DESCRIPTOR_PCODE_DARK_PIXEL:
			what = "dark";
			break;
		case CCS_FRAME_FORMAT_DESCRIPTOR_PCODE_VISIBLE_PIXEL:
			what = "visible";
			break;
		default:
			what = "invalid";
			break;
		}

		dev_dbg(&client->dev,
			"%s pixels: %d %s (pixelcode %u)\n",
			what, pixels, which, pixelcode);

		if (i < ncol_desc) {
			if (pixelcode ==
			    CCS_FRAME_FORMAT_DESCRIPTOR_PCODE_VISIBLE_PIXEL)
				sensor->visible_pixel_start = pixel_count;
			pixel_count += pixels;
			continue;
		}

		/* Handle row descriptors */
		switch (pixelcode) {
		case CCS_FRAME_FORMAT_DESCRIPTOR_PCODE_EMBEDDED:
			if (sensor->embedded_end)
				break;
			sensor->embedded_start = line_count;
			sensor->embedded_end = line_count + pixels;
			break;
		case CCS_FRAME_FORMAT_DESCRIPTOR_PCODE_VISIBLE_PIXEL:
			sensor->image_start = line_count;
			break;
		}
		line_count += pixels;
	}

	if (sensor->embedded_end > sensor->image_start) {
		dev_dbg(&client->dev,
			"adjusting image start line to %u (was %u)\n",
			sensor->embedded_end, sensor->image_start);
		sensor->image_start = sensor->embedded_end;
	}

	dev_dbg(&client->dev, "embedded data from lines %d to %d\n",
		sensor->embedded_start, sensor->embedded_end);
	dev_dbg(&client->dev, "image data starts at line %d\n",
		sensor->image_start);

	return 0;
}

static int ccs_pll_configure(struct ccs_sensor *sensor)
{
	struct ccs_pll *pll = &sensor->pll;
	int rval;

	rval = ccs_write(sensor, VT_PIX_CLK_DIV, pll->vt_bk.pix_clk_div);
	if (rval < 0)
		return rval;

	rval = ccs_write(sensor, VT_SYS_CLK_DIV, pll->vt_bk.sys_clk_div);
	if (rval < 0)
		return rval;

	rval = ccs_write(sensor, PRE_PLL_CLK_DIV, pll->vt_fr.pre_pll_clk_div);
	if (rval < 0)
		return rval;

	rval = ccs_write(sensor, PLL_MULTIPLIER, pll->vt_fr.pll_multiplier);
	if (rval < 0)
		return rval;

	if (!(CCS_LIM(sensor, PHY_CTRL_CAPABILITY) &
	      CCS_PHY_CTRL_CAPABILITY_AUTO_PHY_CTL)) {
		/* Lane op clock ratio does not apply here. */
		rval = ccs_write(sensor, REQUESTED_LINK_RATE,
				 DIV_ROUND_UP(pll->op_bk.sys_clk_freq_hz,
					      1000000 / 256 / 256) *
				 (pll->flags & CCS_PLL_FLAG_LANE_SPEED_MODEL ?
				  sensor->pll.csi2.lanes : 1) <<
				 (pll->flags & CCS_PLL_FLAG_OP_SYS_DDR ?
				  1 : 0));
		if (rval < 0)
			return rval;
	}

	if (sensor->pll.flags & CCS_PLL_FLAG_NO_OP_CLOCKS)
		return 0;

	rval = ccs_write(sensor, OP_PIX_CLK_DIV, pll->op_bk.pix_clk_div);
	if (rval < 0)
		return rval;

	rval = ccs_write(sensor, OP_SYS_CLK_DIV, pll->op_bk.sys_clk_div);
	if (rval < 0)
		return rval;

	if (!(pll->flags & CCS_PLL_FLAG_DUAL_PLL))
		return 0;

	rval = ccs_write(sensor, PLL_MODE, CCS_PLL_MODE_DUAL);
	if (rval < 0)
		return rval;

	rval = ccs_write(sensor, OP_PRE_PLL_CLK_DIV,
			 pll->op_fr.pre_pll_clk_div);
	if (rval < 0)
		return rval;

	return ccs_write(sensor, OP_PLL_MULTIPLIER, pll->op_fr.pll_multiplier);
}

static int ccs_pll_try(struct ccs_sensor *sensor, struct ccs_pll *pll)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	struct ccs_pll_limits lim = {
		.vt_fr = {
			.min_pre_pll_clk_div = CCS_LIM(sensor, MIN_PRE_PLL_CLK_DIV),
			.max_pre_pll_clk_div = CCS_LIM(sensor, MAX_PRE_PLL_CLK_DIV),
			.min_pll_ip_clk_freq_hz = CCS_LIM(sensor, MIN_PLL_IP_CLK_FREQ_MHZ),
			.max_pll_ip_clk_freq_hz = CCS_LIM(sensor, MAX_PLL_IP_CLK_FREQ_MHZ),
			.min_pll_multiplier = CCS_LIM(sensor, MIN_PLL_MULTIPLIER),
			.max_pll_multiplier = CCS_LIM(sensor, MAX_PLL_MULTIPLIER),
			.min_pll_op_clk_freq_hz = CCS_LIM(sensor, MIN_PLL_OP_CLK_FREQ_MHZ),
			.max_pll_op_clk_freq_hz = CCS_LIM(sensor, MAX_PLL_OP_CLK_FREQ_MHZ),
		},
		.op_fr = {
			.min_pre_pll_clk_div = CCS_LIM(sensor, MIN_OP_PRE_PLL_CLK_DIV),
			.max_pre_pll_clk_div = CCS_LIM(sensor, MAX_OP_PRE_PLL_CLK_DIV),
			.min_pll_ip_clk_freq_hz = CCS_LIM(sensor, MIN_OP_PLL_IP_CLK_FREQ_MHZ),
			.max_pll_ip_clk_freq_hz = CCS_LIM(sensor, MAX_OP_PLL_IP_CLK_FREQ_MHZ),
			.min_pll_multiplier = CCS_LIM(sensor, MIN_OP_PLL_MULTIPLIER),
			.max_pll_multiplier = CCS_LIM(sensor, MAX_OP_PLL_MULTIPLIER),
			.min_pll_op_clk_freq_hz = CCS_LIM(sensor, MIN_OP_PLL_OP_CLK_FREQ_MHZ),
			.max_pll_op_clk_freq_hz = CCS_LIM(sensor, MAX_OP_PLL_OP_CLK_FREQ_MHZ),
		},
		.op_bk = {
			 .min_sys_clk_div = CCS_LIM(sensor, MIN_OP_SYS_CLK_DIV),
			 .max_sys_clk_div = CCS_LIM(sensor, MAX_OP_SYS_CLK_DIV),
			 .min_pix_clk_div = CCS_LIM(sensor, MIN_OP_PIX_CLK_DIV),
			 .max_pix_clk_div = CCS_LIM(sensor, MAX_OP_PIX_CLK_DIV),
			 .min_sys_clk_freq_hz = CCS_LIM(sensor, MIN_OP_SYS_CLK_FREQ_MHZ),
			 .max_sys_clk_freq_hz = CCS_LIM(sensor, MAX_OP_SYS_CLK_FREQ_MHZ),
			 .min_pix_clk_freq_hz = CCS_LIM(sensor, MIN_OP_PIX_CLK_FREQ_MHZ),
			 .max_pix_clk_freq_hz = CCS_LIM(sensor, MAX_OP_PIX_CLK_FREQ_MHZ),
		 },
		.vt_bk = {
			 .min_sys_clk_div = CCS_LIM(sensor, MIN_VT_SYS_CLK_DIV),
			 .max_sys_clk_div = CCS_LIM(sensor, MAX_VT_SYS_CLK_DIV),
			 .min_pix_clk_div = CCS_LIM(sensor, MIN_VT_PIX_CLK_DIV),
			 .max_pix_clk_div = CCS_LIM(sensor, MAX_VT_PIX_CLK_DIV),
			 .min_sys_clk_freq_hz = CCS_LIM(sensor, MIN_VT_SYS_CLK_FREQ_MHZ),
			 .max_sys_clk_freq_hz = CCS_LIM(sensor, MAX_VT_SYS_CLK_FREQ_MHZ),
			 .min_pix_clk_freq_hz = CCS_LIM(sensor, MIN_VT_PIX_CLK_FREQ_MHZ),
			 .max_pix_clk_freq_hz = CCS_LIM(sensor, MAX_VT_PIX_CLK_FREQ_MHZ),
		 },
		.min_line_length_pck_bin = CCS_LIM(sensor, MIN_LINE_LENGTH_PCK_BIN),
		.min_line_length_pck = CCS_LIM(sensor, MIN_LINE_LENGTH_PCK),
	};

	return ccs_pll_calculate(&client->dev, &lim, pll);
}

static int ccs_pll_update(struct ccs_sensor *sensor)
{
	struct ccs_pll *pll = &sensor->pll;
	int rval;

	pll->binning_horizontal = sensor->binning_horizontal;
	pll->binning_vertical = sensor->binning_vertical;
	pll->link_freq =
		sensor->link_freq->qmenu_int[sensor->link_freq->val];
	pll->scale_m = sensor->scale_m;
	pll->bits_per_pixel = sensor->csi_format->compressed;

	rval = ccs_pll_try(sensor, pll);
	if (rval < 0)
		return rval;

	__v4l2_ctrl_s_ctrl_int64(sensor->pixel_rate_parray,
				 pll->pixel_rate_pixel_array);
	__v4l2_ctrl_s_ctrl_int64(sensor->pixel_rate_csi, pll->pixel_rate_csi);

	return 0;
}


/*
 *
 * V4L2 Controls handling
 *
 */

static void __ccs_update_exposure_limits(struct ccs_sensor *sensor)
{
	struct v4l2_ctrl *ctrl = sensor->exposure;
	int max;

	max = sensor->pixel_array->crop[CCS_PA_PAD_SRC].height
		+ sensor->vblank->val
		- CCS_LIM(sensor, COARSE_INTEGRATION_TIME_MAX_MARGIN);

	__v4l2_ctrl_modify_range(ctrl, ctrl->minimum, max, ctrl->step, max);
}

/*
 * Order matters.
 *
 * 1. Bits-per-pixel, descending.
 * 2. Bits-per-pixel compressed, descending.
 * 3. Pixel order, same as in pixel_order_str. Formats for all four pixel
 *    orders must be defined.
 */
static const struct ccs_csi_data_format ccs_csi_data_formats[] = {
	{ MEDIA_BUS_FMT_SGRBG16_1X16, 16, 16, CCS_PIXEL_ORDER_GRBG, },
	{ MEDIA_BUS_FMT_SRGGB16_1X16, 16, 16, CCS_PIXEL_ORDER_RGGB, },
	{ MEDIA_BUS_FMT_SBGGR16_1X16, 16, 16, CCS_PIXEL_ORDER_BGGR, },
	{ MEDIA_BUS_FMT_SGBRG16_1X16, 16, 16, CCS_PIXEL_ORDER_GBRG, },
	{ MEDIA_BUS_FMT_SGRBG14_1X14, 14, 14, CCS_PIXEL_ORDER_GRBG, },
	{ MEDIA_BUS_FMT_SRGGB14_1X14, 14, 14, CCS_PIXEL_ORDER_RGGB, },
	{ MEDIA_BUS_FMT_SBGGR14_1X14, 14, 14, CCS_PIXEL_ORDER_BGGR, },
	{ MEDIA_BUS_FMT_SGBRG14_1X14, 14, 14, CCS_PIXEL_ORDER_GBRG, },
	{ MEDIA_BUS_FMT_SGRBG12_1X12, 12, 12, CCS_PIXEL_ORDER_GRBG, },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, 12, 12, CCS_PIXEL_ORDER_RGGB, },
	{ MEDIA_BUS_FMT_SBGGR12_1X12, 12, 12, CCS_PIXEL_ORDER_BGGR, },
	{ MEDIA_BUS_FMT_SGBRG12_1X12, 12, 12, CCS_PIXEL_ORDER_GBRG, },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, 10, 10, CCS_PIXEL_ORDER_GRBG, },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, 10, 10, CCS_PIXEL_ORDER_RGGB, },
	{ MEDIA_BUS_FMT_SBGGR10_1X10, 10, 10, CCS_PIXEL_ORDER_BGGR, },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, 10, 10, CCS_PIXEL_ORDER_GBRG, },
	{ MEDIA_BUS_FMT_SGRBG10_DPCM8_1X8, 10, 8, CCS_PIXEL_ORDER_GRBG, },
	{ MEDIA_BUS_FMT_SRGGB10_DPCM8_1X8, 10, 8, CCS_PIXEL_ORDER_RGGB, },
	{ MEDIA_BUS_FMT_SBGGR10_DPCM8_1X8, 10, 8, CCS_PIXEL_ORDER_BGGR, },
	{ MEDIA_BUS_FMT_SGBRG10_DPCM8_1X8, 10, 8, CCS_PIXEL_ORDER_GBRG, },
	{ MEDIA_BUS_FMT_SGRBG8_1X8, 8, 8, CCS_PIXEL_ORDER_GRBG, },
	{ MEDIA_BUS_FMT_SRGGB8_1X8, 8, 8, CCS_PIXEL_ORDER_RGGB, },
	{ MEDIA_BUS_FMT_SBGGR8_1X8, 8, 8, CCS_PIXEL_ORDER_BGGR, },
	{ MEDIA_BUS_FMT_SGBRG8_1X8, 8, 8, CCS_PIXEL_ORDER_GBRG, },
};

static const char *pixel_order_str[] = { "GRBG", "RGGB", "BGGR", "GBRG" };

#define to_csi_format_idx(fmt) (((unsigned long)(fmt)			\
				 - (unsigned long)ccs_csi_data_formats) \
				/ sizeof(*ccs_csi_data_formats))

static u32 ccs_pixel_order(struct ccs_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int flip = 0;

	if (sensor->hflip) {
		if (sensor->hflip->val)
			flip |= CCS_IMAGE_ORIENTATION_HORIZONTAL_MIRROR;

		if (sensor->vflip->val)
			flip |= CCS_IMAGE_ORIENTATION_VERTICAL_FLIP;
	}

	flip ^= sensor->hvflip_inv_mask;

	dev_dbg(&client->dev, "flip %d\n", flip);
	return sensor->default_pixel_order ^ flip;
}

static void ccs_update_mbus_formats(struct ccs_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	unsigned int csi_format_idx =
		to_csi_format_idx(sensor->csi_format) & ~3;
	unsigned int internal_csi_format_idx =
		to_csi_format_idx(sensor->internal_csi_format) & ~3;
	unsigned int pixel_order = ccs_pixel_order(sensor);

	if (WARN_ON_ONCE(max(internal_csi_format_idx, csi_format_idx) +
			 pixel_order >= ARRAY_SIZE(ccs_csi_data_formats)))
		return;

	sensor->mbus_frame_fmts =
		sensor->default_mbus_frame_fmts << pixel_order;
	sensor->csi_format =
		&ccs_csi_data_formats[csi_format_idx + pixel_order];
	sensor->internal_csi_format =
		&ccs_csi_data_formats[internal_csi_format_idx
					 + pixel_order];

	dev_dbg(&client->dev, "new pixel order %s\n",
		pixel_order_str[pixel_order]);
}

static const char * const ccs_test_patterns[] = {
	"Disabled",
	"Solid Colour",
	"Eight Vertical Colour Bars",
	"Colour Bars With Fade to Grey",
	"Pseudorandom Sequence (PN9)",
};

static int ccs_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ccs_sensor *sensor =
		container_of(ctrl->handler, struct ccs_subdev, ctrl_handler)
			->sensor;
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int pm_status;
	u32 orient = 0;
	unsigned int i;
	int exposure;
	int rval;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		if (sensor->streaming)
			return -EBUSY;

		if (sensor->hflip->val)
			orient |= CCS_IMAGE_ORIENTATION_HORIZONTAL_MIRROR;

		if (sensor->vflip->val)
			orient |= CCS_IMAGE_ORIENTATION_VERTICAL_FLIP;

		orient ^= sensor->hvflip_inv_mask;

		ccs_update_mbus_formats(sensor);

		break;
	case V4L2_CID_VBLANK:
		exposure = sensor->exposure->val;

		__ccs_update_exposure_limits(sensor);

		if (exposure > sensor->exposure->maximum) {
			sensor->exposure->val =	sensor->exposure->maximum;
			rval = ccs_set_ctrl(sensor->exposure);
			if (rval < 0)
				return rval;
		}

		break;
	case V4L2_CID_LINK_FREQ:
		if (sensor->streaming)
			return -EBUSY;

		rval = ccs_pll_update(sensor);
		if (rval)
			return rval;

		return 0;
	case V4L2_CID_TEST_PATTERN:
		for (i = 0; i < ARRAY_SIZE(sensor->test_data); i++)
			v4l2_ctrl_activate(
				sensor->test_data[i],
				ctrl->val ==
				V4L2_SMIAPP_TEST_PATTERN_MODE_SOLID_COLOUR);

		break;
	}

	pm_status = pm_runtime_get_if_active(&client->dev, true);
	if (!pm_status)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		rval = ccs_write(sensor, ANALOG_GAIN_CODE_GLOBAL, ctrl->val);

		break;

	case V4L2_CID_CCS_ANALOGUE_LINEAR_GAIN:
		rval = ccs_write(sensor, ANALOG_LINEAR_GAIN_GLOBAL, ctrl->val);

		break;

	case V4L2_CID_CCS_ANALOGUE_EXPONENTIAL_GAIN:
		rval = ccs_write(sensor, ANALOG_EXPONENTIAL_GAIN_GLOBAL,
				 ctrl->val);

		break;

	case V4L2_CID_DIGITAL_GAIN:
		if (CCS_LIM(sensor, DIGITAL_GAIN_CAPABILITY) ==
		    CCS_DIGITAL_GAIN_CAPABILITY_GLOBAL) {
			rval = ccs_write(sensor, DIGITAL_GAIN_GLOBAL,
					 ctrl->val);
			break;
		}

		rval = ccs_write_addr(sensor,
				      SMIAPP_REG_U16_DIGITAL_GAIN_GREENR,
				      ctrl->val);
		if (rval)
			break;

		rval = ccs_write_addr(sensor,
				      SMIAPP_REG_U16_DIGITAL_GAIN_RED,
				      ctrl->val);
		if (rval)
			break;

		rval = ccs_write_addr(sensor,
				      SMIAPP_REG_U16_DIGITAL_GAIN_BLUE,
				      ctrl->val);
		if (rval)
			break;

		rval = ccs_write_addr(sensor,
				      SMIAPP_REG_U16_DIGITAL_GAIN_GREENB,
				      ctrl->val);

		break;
	case V4L2_CID_EXPOSURE:
		rval = ccs_write(sensor, COARSE_INTEGRATION_TIME, ctrl->val);

		break;
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		rval = ccs_write(sensor, IMAGE_ORIENTATION, orient);

		break;
	case V4L2_CID_VBLANK:
		rval = ccs_write(sensor, FRAME_LENGTH_LINES,
				 sensor->pixel_array->crop[
					 CCS_PA_PAD_SRC].height
				 + ctrl->val);

		break;
	case V4L2_CID_HBLANK:
		rval = ccs_write(sensor, LINE_LENGTH_PCK,
				 sensor->pixel_array->crop[CCS_PA_PAD_SRC].width
				 + ctrl->val);

		break;
	case V4L2_CID_TEST_PATTERN:
		rval = ccs_write(sensor, TEST_PATTERN_MODE, ctrl->val);

		break;
	case V4L2_CID_TEST_PATTERN_RED:
		rval = ccs_write(sensor, TEST_DATA_RED, ctrl->val);

		break;
	case V4L2_CID_TEST_PATTERN_GREENR:
		rval = ccs_write(sensor, TEST_DATA_GREENR, ctrl->val);

		break;
	case V4L2_CID_TEST_PATTERN_BLUE:
		rval = ccs_write(sensor, TEST_DATA_BLUE, ctrl->val);

		break;
	case V4L2_CID_TEST_PATTERN_GREENB:
		rval = ccs_write(sensor, TEST_DATA_GREENB, ctrl->val);

		break;
	case V4L2_CID_CCS_SHADING_CORRECTION:
		rval = ccs_write(sensor, SHADING_CORRECTION_EN,
				 ctrl->val ? CCS_SHADING_CORRECTION_EN_ENABLE :
				 0);

		if (!rval && sensor->luminance_level)
			v4l2_ctrl_activate(sensor->luminance_level, ctrl->val);

		break;
	case V4L2_CID_CCS_LUMINANCE_CORRECTION_LEVEL:
		rval = ccs_write(sensor, LUMINANCE_CORRECTION_LEVEL, ctrl->val);

		break;
	case V4L2_CID_PIXEL_RATE:
		/* For v4l2_ctrl_s_ctrl_int64() used internally. */
		rval = 0;

		break;
	default:
		rval = -EINVAL;
	}

	if (pm_status > 0) {
		pm_runtime_mark_last_busy(&client->dev);
		pm_runtime_put_autosuspend(&client->dev);
	}

	return rval;
}

static const struct v4l2_ctrl_ops ccs_ctrl_ops = {
	.s_ctrl = ccs_set_ctrl,
};

static int ccs_init_controls(struct ccs_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int rval;

	rval = v4l2_ctrl_handler_init(&sensor->pixel_array->ctrl_handler, 17);
	if (rval)
		return rval;

	sensor->pixel_array->ctrl_handler.lock = &sensor->mutex;

	switch (CCS_LIM(sensor, ANALOG_GAIN_CAPABILITY)) {
	case CCS_ANALOG_GAIN_CAPABILITY_GLOBAL: {
		struct {
			const char *name;
			u32 id;
			s32 value;
		} const gain_ctrls[] = {
			{ "Analogue Gain m0", V4L2_CID_CCS_ANALOGUE_GAIN_M0,
			  CCS_LIM(sensor, ANALOG_GAIN_M0), },
			{ "Analogue Gain c0", V4L2_CID_CCS_ANALOGUE_GAIN_C0,
			  CCS_LIM(sensor, ANALOG_GAIN_C0), },
			{ "Analogue Gain m1", V4L2_CID_CCS_ANALOGUE_GAIN_M1,
			  CCS_LIM(sensor, ANALOG_GAIN_M1), },
			{ "Analogue Gain c1", V4L2_CID_CCS_ANALOGUE_GAIN_C1,
			  CCS_LIM(sensor, ANALOG_GAIN_C1), },
		};
		struct v4l2_ctrl_config ctrl_cfg = {
			.type = V4L2_CTRL_TYPE_INTEGER,
			.ops = &ccs_ctrl_ops,
			.flags = V4L2_CTRL_FLAG_READ_ONLY,
			.step = 1,
		};
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(gain_ctrls); i++) {
			ctrl_cfg.name = gain_ctrls[i].name;
			ctrl_cfg.id = gain_ctrls[i].id;
			ctrl_cfg.min = ctrl_cfg.max = ctrl_cfg.def =
				gain_ctrls[i].value;

			v4l2_ctrl_new_custom(&sensor->pixel_array->ctrl_handler,
					     &ctrl_cfg, NULL);
		}

		v4l2_ctrl_new_std(&sensor->pixel_array->ctrl_handler,
				  &ccs_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
				  CCS_LIM(sensor, ANALOG_GAIN_CODE_MIN),
				  CCS_LIM(sensor, ANALOG_GAIN_CODE_MAX),
				  max(CCS_LIM(sensor, ANALOG_GAIN_CODE_STEP),
				      1U),
				  CCS_LIM(sensor, ANALOG_GAIN_CODE_MIN));
	}
		break;

	case CCS_ANALOG_GAIN_CAPABILITY_ALTERNATE_GLOBAL: {
		struct {
			const char *name;
			u32 id;
			u16 min, max, step;
		} const gain_ctrls[] = {
			{
				"Analogue Linear Gain",
				V4L2_CID_CCS_ANALOGUE_LINEAR_GAIN,
				CCS_LIM(sensor, ANALOG_LINEAR_GAIN_MIN),
				CCS_LIM(sensor, ANALOG_LINEAR_GAIN_MAX),
				max(CCS_LIM(sensor,
					    ANALOG_LINEAR_GAIN_STEP_SIZE),
				    1U),
			},
			{
				"Analogue Exponential Gain",
				V4L2_CID_CCS_ANALOGUE_EXPONENTIAL_GAIN,
				CCS_LIM(sensor, ANALOG_EXPONENTIAL_GAIN_MIN),
				CCS_LIM(sensor, ANALOG_EXPONENTIAL_GAIN_MAX),
				max(CCS_LIM(sensor,
					    ANALOG_EXPONENTIAL_GAIN_STEP_SIZE),
				    1U),
			},
		};
		struct v4l2_ctrl_config ctrl_cfg = {
			.type = V4L2_CTRL_TYPE_INTEGER,
			.ops = &ccs_ctrl_ops,
		};
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(gain_ctrls); i++) {
			ctrl_cfg.name = gain_ctrls[i].name;
			ctrl_cfg.min = ctrl_cfg.def = gain_ctrls[i].min;
			ctrl_cfg.max = gain_ctrls[i].max;
			ctrl_cfg.step = gain_ctrls[i].step;
			ctrl_cfg.id = gain_ctrls[i].id;

			v4l2_ctrl_new_custom(&sensor->pixel_array->ctrl_handler,
					     &ctrl_cfg, NULL);
		}
	}
	}

	if (CCS_LIM(sensor, SHADING_CORRECTION_CAPABILITY) &
	    (CCS_SHADING_CORRECTION_CAPABILITY_COLOR_SHADING |
	     CCS_SHADING_CORRECTION_CAPABILITY_LUMINANCE_CORRECTION)) {
		const struct v4l2_ctrl_config ctrl_cfg = {
			.name = "Shading Correction",
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.id = V4L2_CID_CCS_SHADING_CORRECTION,
			.ops = &ccs_ctrl_ops,
			.max = 1,
			.step = 1,
		};

		v4l2_ctrl_new_custom(&sensor->pixel_array->ctrl_handler,
				     &ctrl_cfg, NULL);
	}

	if (CCS_LIM(sensor, SHADING_CORRECTION_CAPABILITY) &
	    CCS_SHADING_CORRECTION_CAPABILITY_LUMINANCE_CORRECTION) {
		const struct v4l2_ctrl_config ctrl_cfg = {
			.name = "Luminance Correction Level",
			.type = V4L2_CTRL_TYPE_BOOLEAN,
			.id = V4L2_CID_CCS_LUMINANCE_CORRECTION_LEVEL,
			.ops = &ccs_ctrl_ops,
			.max = 255,
			.step = 1,
			.def = 128,
		};

		sensor->luminance_level =
			v4l2_ctrl_new_custom(&sensor->pixel_array->ctrl_handler,
					     &ctrl_cfg, NULL);
	}

	if (CCS_LIM(sensor, DIGITAL_GAIN_CAPABILITY) ==
	    CCS_DIGITAL_GAIN_CAPABILITY_GLOBAL ||
	    CCS_LIM(sensor, DIGITAL_GAIN_CAPABILITY) ==
	    SMIAPP_DIGITAL_GAIN_CAPABILITY_PER_CHANNEL)
		v4l2_ctrl_new_std(&sensor->pixel_array->ctrl_handler,
				  &ccs_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
				  CCS_LIM(sensor, DIGITAL_GAIN_MIN),
				  CCS_LIM(sensor, DIGITAL_GAIN_MAX),
				  max(CCS_LIM(sensor, DIGITAL_GAIN_STEP_SIZE),
				      1U),
				  0x100);

	/* Exposure limits will be updated soon, use just something here. */
	sensor->exposure = v4l2_ctrl_new_std(
		&sensor->pixel_array->ctrl_handler, &ccs_ctrl_ops,
		V4L2_CID_EXPOSURE, 0, 0, 1, 0);

	sensor->hflip = v4l2_ctrl_new_std(
		&sensor->pixel_array->ctrl_handler, &ccs_ctrl_ops,
		V4L2_CID_HFLIP, 0, 1, 1, 0);
	sensor->vflip = v4l2_ctrl_new_std(
		&sensor->pixel_array->ctrl_handler, &ccs_ctrl_ops,
		V4L2_CID_VFLIP, 0, 1, 1, 0);

	sensor->vblank = v4l2_ctrl_new_std(
		&sensor->pixel_array->ctrl_handler, &ccs_ctrl_ops,
		V4L2_CID_VBLANK, 0, 1, 1, 0);

	if (sensor->vblank)
		sensor->vblank->flags |= V4L2_CTRL_FLAG_UPDATE;

	sensor->hblank = v4l2_ctrl_new_std(
		&sensor->pixel_array->ctrl_handler, &ccs_ctrl_ops,
		V4L2_CID_HBLANK, 0, 1, 1, 0);

	if (sensor->hblank)
		sensor->hblank->flags |= V4L2_CTRL_FLAG_UPDATE;

	sensor->pixel_rate_parray = v4l2_ctrl_new_std(
		&sensor->pixel_array->ctrl_handler, &ccs_ctrl_ops,
		V4L2_CID_PIXEL_RATE, 1, INT_MAX, 1, 1);

	v4l2_ctrl_new_std_menu_items(&sensor->pixel_array->ctrl_handler,
				     &ccs_ctrl_ops, V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ccs_test_patterns) - 1,
				     0, 0, ccs_test_patterns);

	if (sensor->pixel_array->ctrl_handler.error) {
		dev_err(&client->dev,
			"pixel array controls initialization failed (%d)\n",
			sensor->pixel_array->ctrl_handler.error);
		return sensor->pixel_array->ctrl_handler.error;
	}

	sensor->pixel_array->sd.ctrl_handler =
		&sensor->pixel_array->ctrl_handler;

	v4l2_ctrl_cluster(2, &sensor->hflip);

	rval = v4l2_ctrl_handler_init(&sensor->src->ctrl_handler, 0);
	if (rval)
		return rval;

	sensor->src->ctrl_handler.lock = &sensor->mutex;

	sensor->pixel_rate_csi = v4l2_ctrl_new_std(
		&sensor->src->ctrl_handler, &ccs_ctrl_ops,
		V4L2_CID_PIXEL_RATE, 1, INT_MAX, 1, 1);

	if (sensor->src->ctrl_handler.error) {
		dev_err(&client->dev,
			"src controls initialization failed (%d)\n",
			sensor->src->ctrl_handler.error);
		return sensor->src->ctrl_handler.error;
	}

	sensor->src->sd.ctrl_handler = &sensor->src->ctrl_handler;

	return 0;
}

/*
 * For controls that require information on available media bus codes
 * and linke frequencies.
 */
static int ccs_init_late_controls(struct ccs_sensor *sensor)
{
	unsigned long *valid_link_freqs = &sensor->valid_link_freqs[
		sensor->csi_format->compressed - sensor->compressed_min_bpp];
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sensor->test_data); i++) {
		int max_value = (1 << sensor->csi_format->width) - 1;

		sensor->test_data[i] = v4l2_ctrl_new_std(
				&sensor->pixel_array->ctrl_handler,
				&ccs_ctrl_ops, V4L2_CID_TEST_PATTERN_RED + i,
				0, max_value, 1, max_value);
	}

	sensor->link_freq = v4l2_ctrl_new_int_menu(
		&sensor->src->ctrl_handler, &ccs_ctrl_ops,
		V4L2_CID_LINK_FREQ, __fls(*valid_link_freqs),
		__ffs(*valid_link_freqs), sensor->hwcfg.op_sys_clock);

	return sensor->src->ctrl_handler.error;
}

static void ccs_free_controls(struct ccs_sensor *sensor)
{
	unsigned int i;

	for (i = 0; i < sensor->ssds_used; i++)
		v4l2_ctrl_handler_free(&sensor->ssds[i].ctrl_handler);
}

static int ccs_get_mbus_formats(struct ccs_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	struct ccs_pll *pll = &sensor->pll;
	u8 compressed_max_bpp = 0;
	unsigned int type, n;
	unsigned int i, pixel_order;
	int rval;

	type = CCS_LIM(sensor, DATA_FORMAT_MODEL_TYPE);

	dev_dbg(&client->dev, "data_format_model_type %d\n", type);

	rval = ccs_read(sensor, PIXEL_ORDER, &pixel_order);
	if (rval)
		return rval;

	if (pixel_order >= ARRAY_SIZE(pixel_order_str)) {
		dev_dbg(&client->dev, "bad pixel order %d\n", pixel_order);
		return -EINVAL;
	}

	dev_dbg(&client->dev, "pixel order %d (%s)\n", pixel_order,
		pixel_order_str[pixel_order]);

	switch (type) {
	case CCS_DATA_FORMAT_MODEL_TYPE_NORMAL:
		n = SMIAPP_DATA_FORMAT_MODEL_TYPE_NORMAL_N;
		break;
	case CCS_DATA_FORMAT_MODEL_TYPE_EXTENDED:
		n = CCS_LIM_DATA_FORMAT_DESCRIPTOR_MAX_N + 1;
		break;
	default:
		return -EINVAL;
	}

	sensor->default_pixel_order = pixel_order;
	sensor->mbus_frame_fmts = 0;

	for (i = 0; i < n; i++) {
		unsigned int fmt, j;

		fmt = CCS_LIM_AT(sensor, DATA_FORMAT_DESCRIPTOR, i);

		dev_dbg(&client->dev, "%u: bpp %u, compressed %u\n",
			i, fmt >> 8, (u8)fmt);

		for (j = 0; j < ARRAY_SIZE(ccs_csi_data_formats); j++) {
			const struct ccs_csi_data_format *f =
				&ccs_csi_data_formats[j];

			if (f->pixel_order != CCS_PIXEL_ORDER_GRBG)
				continue;

			if (f->width != fmt >>
			    CCS_DATA_FORMAT_DESCRIPTOR_UNCOMPRESSED_SHIFT ||
			    f->compressed !=
			    (fmt & CCS_DATA_FORMAT_DESCRIPTOR_COMPRESSED_MASK))
				continue;

			dev_dbg(&client->dev, "jolly good! %d\n", j);

			sensor->default_mbus_frame_fmts |= 1 << j;
		}
	}

	/* Figure out which BPP values can be used with which formats. */
	pll->binning_horizontal = 1;
	pll->binning_vertical = 1;
	pll->scale_m = sensor->scale_m;

	for (i = 0; i < ARRAY_SIZE(ccs_csi_data_formats); i++) {
		sensor->compressed_min_bpp =
			min(ccs_csi_data_formats[i].compressed,
			    sensor->compressed_min_bpp);
		compressed_max_bpp =
			max(ccs_csi_data_formats[i].compressed,
			    compressed_max_bpp);
	}

	sensor->valid_link_freqs = devm_kcalloc(
		&client->dev,
		compressed_max_bpp - sensor->compressed_min_bpp + 1,
		sizeof(*sensor->valid_link_freqs), GFP_KERNEL);
	if (!sensor->valid_link_freqs)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(ccs_csi_data_formats); i++) {
		const struct ccs_csi_data_format *f =
			&ccs_csi_data_formats[i];
		unsigned long *valid_link_freqs =
			&sensor->valid_link_freqs[
				f->compressed - sensor->compressed_min_bpp];
		unsigned int j;

		if (!(sensor->default_mbus_frame_fmts & 1 << i))
			continue;

		pll->bits_per_pixel = f->compressed;

		for (j = 0; sensor->hwcfg.op_sys_clock[j]; j++) {
			pll->link_freq = sensor->hwcfg.op_sys_clock[j];

			rval = ccs_pll_try(sensor, pll);
			dev_dbg(&client->dev, "link freq %u Hz, bpp %u %s\n",
				pll->link_freq, pll->bits_per_pixel,
				rval ? "not ok" : "ok");
			if (rval)
				continue;

			set_bit(j, valid_link_freqs);
		}

		if (!*valid_link_freqs) {
			dev_info(&client->dev,
				 "no valid link frequencies for %u bpp\n",
				 f->compressed);
			sensor->default_mbus_frame_fmts &= ~BIT(i);
			continue;
		}

		if (!sensor->csi_format
		    || f->width > sensor->csi_format->width
		    || (f->width == sensor->csi_format->width
			&& f->compressed > sensor->csi_format->compressed)) {
			sensor->csi_format = f;
			sensor->internal_csi_format = f;
		}
	}

	if (!sensor->csi_format) {
		dev_err(&client->dev, "no supported mbus code found\n");
		return -EINVAL;
	}

	ccs_update_mbus_formats(sensor);

	return 0;
}

static void ccs_update_blanking(struct ccs_sensor *sensor)
{
	struct v4l2_ctrl *vblank = sensor->vblank;
	struct v4l2_ctrl *hblank = sensor->hblank;
	u16 min_fll, max_fll, min_llp, max_llp, min_lbp;
	int min, max;

	if (sensor->binning_vertical > 1 || sensor->binning_horizontal > 1) {
		min_fll = CCS_LIM(sensor, MIN_FRAME_LENGTH_LINES_BIN);
		max_fll = CCS_LIM(sensor, MAX_FRAME_LENGTH_LINES_BIN);
		min_llp = CCS_LIM(sensor, MIN_LINE_LENGTH_PCK_BIN);
		max_llp = CCS_LIM(sensor, MAX_LINE_LENGTH_PCK_BIN);
		min_lbp = CCS_LIM(sensor, MIN_LINE_BLANKING_PCK_BIN);
	} else {
		min_fll = CCS_LIM(sensor, MIN_FRAME_LENGTH_LINES);
		max_fll = CCS_LIM(sensor, MAX_FRAME_LENGTH_LINES);
		min_llp = CCS_LIM(sensor, MIN_LINE_LENGTH_PCK);
		max_llp = CCS_LIM(sensor, MAX_LINE_LENGTH_PCK);
		min_lbp = CCS_LIM(sensor, MIN_LINE_BLANKING_PCK);
	}

	min = max_t(int,
		    CCS_LIM(sensor, MIN_FRAME_BLANKING_LINES),
		    min_fll - sensor->pixel_array->crop[CCS_PA_PAD_SRC].height);
	max = max_fll -	sensor->pixel_array->crop[CCS_PA_PAD_SRC].height;

	__v4l2_ctrl_modify_range(vblank, min, max, vblank->step, min);

	min = max_t(int,
		    min_llp - sensor->pixel_array->crop[CCS_PA_PAD_SRC].width,
		    min_lbp);
	max = max_llp - sensor->pixel_array->crop[CCS_PA_PAD_SRC].width;

	__v4l2_ctrl_modify_range(hblank, min, max, hblank->step, min);

	__ccs_update_exposure_limits(sensor);
}

static int ccs_pll_blanking_update(struct ccs_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int rval;

	rval = ccs_pll_update(sensor);
	if (rval < 0)
		return rval;

	/* Output from pixel array, including blanking */
	ccs_update_blanking(sensor);

	dev_dbg(&client->dev, "vblank\t\t%d\n", sensor->vblank->val);
	dev_dbg(&client->dev, "hblank\t\t%d\n", sensor->hblank->val);

	dev_dbg(&client->dev, "real timeperframe\t100/%d\n",
		sensor->pll.pixel_rate_pixel_array /
		((sensor->pixel_array->crop[CCS_PA_PAD_SRC].width
		  + sensor->hblank->val) *
		 (sensor->pixel_array->crop[CCS_PA_PAD_SRC].height
		  + sensor->vblank->val) / 100));

	return 0;
}

/*
 *
 * SMIA++ NVM handling
 *
 */

static int ccs_read_nvm_page(struct ccs_sensor *sensor, u32 p, u8 *nvm,
			     u8 *status)
{
	unsigned int i;
	int rval;
	u32 s;

	*status = 0;

	rval = ccs_write(sensor, DATA_TRANSFER_IF_1_PAGE_SELECT, p);
	if (rval)
		return rval;

	rval = ccs_write(sensor, DATA_TRANSFER_IF_1_CTRL,
			 CCS_DATA_TRANSFER_IF_1_CTRL_ENABLE);
	if (rval)
		return rval;

	rval = ccs_read(sensor, DATA_TRANSFER_IF_1_STATUS, &s);
	if (rval)
		return rval;

	if (s & CCS_DATA_TRANSFER_IF_1_STATUS_IMPROPER_IF_USAGE) {
		*status = s;
		return -ENODATA;
	}

	if (CCS_LIM(sensor, DATA_TRANSFER_IF_CAPABILITY) &
	    CCS_DATA_TRANSFER_IF_CAPABILITY_POLLING) {
		for (i = 1000; i > 0; i--) {
			if (s & CCS_DATA_TRANSFER_IF_1_STATUS_READ_IF_READY)
				break;

			rval = ccs_read(sensor, DATA_TRANSFER_IF_1_STATUS, &s);
			if (rval)
				return rval;
		}

		if (!i)
			return -ETIMEDOUT;
	}

	for (i = 0; i <= CCS_LIM_DATA_TRANSFER_IF_1_DATA_MAX_P; i++) {
		u32 v;

		rval = ccs_read(sensor, DATA_TRANSFER_IF_1_DATA(i), &v);
		if (rval)
			return rval;

		*nvm++ = v;
	}

	return 0;
}

static int ccs_read_nvm(struct ccs_sensor *sensor, unsigned char *nvm,
			size_t nvm_size)
{
	u8 status = 0;
	u32 p;
	int rval = 0, rval2;

	for (p = 0; p < nvm_size / (CCS_LIM_DATA_TRANSFER_IF_1_DATA_MAX_P + 1)
		     && !rval; p++) {
		rval = ccs_read_nvm_page(sensor, p, nvm, &status);
		nvm += CCS_LIM_DATA_TRANSFER_IF_1_DATA_MAX_P + 1;
	}

	if (rval == -ENODATA &&
	    status & CCS_DATA_TRANSFER_IF_1_STATUS_IMPROPER_IF_USAGE)
		rval = 0;

	rval2 = ccs_write(sensor, DATA_TRANSFER_IF_1_CTRL, 0);
	if (rval < 0)
		return rval;
	else
		return rval2 ?: p * (CCS_LIM_DATA_TRANSFER_IF_1_DATA_MAX_P + 1);
}

/*
 *
 * SMIA++ CCI address control
 *
 */
static int ccs_change_cci_addr(struct ccs_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int rval;
	u32 val;

	client->addr = sensor->hwcfg.i2c_addr_dfl;

	rval = ccs_write(sensor, CCI_ADDRESS_CTRL,
			 sensor->hwcfg.i2c_addr_alt << 1);
	if (rval)
		return rval;

	client->addr = sensor->hwcfg.i2c_addr_alt;

	/* verify addr change went ok */
	rval = ccs_read(sensor, CCI_ADDRESS_CTRL, &val);
	if (rval)
		return rval;

	if (val != sensor->hwcfg.i2c_addr_alt << 1)
		return -ENODEV;

	return 0;
}

/*
 *
 * SMIA++ Mode Control
 *
 */
static int ccs_setup_flash_strobe(struct ccs_sensor *sensor)
{
	struct ccs_flash_strobe_parms *strobe_setup;
	unsigned int ext_freq = sensor->hwcfg.ext_clk;
	u32 tmp;
	u32 strobe_adjustment;
	u32 strobe_width_high_rs;
	int rval;

	strobe_setup = sensor->hwcfg.strobe_setup;

	/*
	 * How to calculate registers related to strobe length. Please
	 * do not change, or if you do at least know what you're
	 * doing. :-)
	 *
	 * Sakari Ailus <sakari.ailus@linux.intel.com> 2010-10-25
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

	rval = ccs_write(sensor, FLASH_MODE_RS, strobe_setup->mode);
	if (rval < 0)
		goto out;

	rval = ccs_write(sensor, FLASH_STROBE_ADJUSTMENT, strobe_adjustment);
	if (rval < 0)
		goto out;

	rval = ccs_write(sensor, TFLASH_STROBE_WIDTH_HIGH_RS_CTRL,
			 strobe_width_high_rs);
	if (rval < 0)
		goto out;

	rval = ccs_write(sensor, TFLASH_STROBE_DELAY_RS_CTRL,
			 strobe_setup->strobe_delay);
	if (rval < 0)
		goto out;

	rval = ccs_write(sensor, FLASH_STROBE_START_POINT,
			 strobe_setup->stobe_start_point);
	if (rval < 0)
		goto out;

	rval = ccs_write(sensor, FLASH_TRIGGER_RS, strobe_setup->trigger);

out:
	sensor->hwcfg.strobe_setup->trigger = 0;

	return rval;
}

/* -----------------------------------------------------------------------------
 * Power management
 */

static int ccs_write_msr_regs(struct ccs_sensor *sensor)
{
	int rval;

	rval = ccs_write_data_regs(sensor,
				   sensor->sdata.sensor_manufacturer_regs,
				   sensor->sdata.num_sensor_manufacturer_regs);
	if (rval)
		return rval;

	return ccs_write_data_regs(sensor,
				   sensor->mdata.module_manufacturer_regs,
				   sensor->mdata.num_module_manufacturer_regs);
}

static int ccs_update_phy_ctrl(struct ccs_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	u8 val;

	if (!sensor->ccs_limits)
		return 0;

	if (CCS_LIM(sensor, PHY_CTRL_CAPABILITY) &
	    CCS_PHY_CTRL_CAPABILITY_AUTO_PHY_CTL) {
		val = CCS_PHY_CTRL_AUTO;
	} else if (CCS_LIM(sensor, PHY_CTRL_CAPABILITY) &
		   CCS_PHY_CTRL_CAPABILITY_UI_PHY_CTL) {
		val = CCS_PHY_CTRL_UI;
	} else {
		dev_err(&client->dev, "manual PHY control not supported\n");
		return -EINVAL;
	}

	return ccs_write(sensor, PHY_CTRL, val);
}

static int ccs_power_on(struct device *dev)
{
	struct v4l2_subdev *subdev = dev_get_drvdata(dev);
	struct ccs_subdev *ssd = to_ccs_subdev(subdev);
	/*
	 * The sub-device related to the I2C device is always the
	 * source one, i.e. ssds[0].
	 */
	struct ccs_sensor *sensor =
		container_of(ssd, struct ccs_sensor, ssds[0]);
	const struct ccs_device *ccsdev = device_get_match_data(dev);
	int rval;

	rval = regulator_bulk_enable(ARRAY_SIZE(ccs_regulators),
				     sensor->regulators);
	if (rval) {
		dev_err(dev, "failed to enable vana regulator\n");
		return rval;
	}

	if (sensor->reset || sensor->xshutdown || sensor->ext_clk) {
		unsigned int sleep;

		rval = clk_prepare_enable(sensor->ext_clk);
		if (rval < 0) {
			dev_dbg(dev, "failed to enable xclk\n");
			goto out_xclk_fail;
		}

		gpiod_set_value(sensor->reset, 0);
		gpiod_set_value(sensor->xshutdown, 1);

		if (ccsdev->flags & CCS_DEVICE_FLAG_IS_SMIA)
			sleep = SMIAPP_RESET_DELAY(sensor->hwcfg.ext_clk);
		else
			sleep = 5000;

		usleep_range(sleep, sleep);
	}

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

	if (!sensor->reset && !sensor->xshutdown) {
		u8 retry = 100;
		u32 reset;

		rval = ccs_write(sensor, SOFTWARE_RESET, CCS_SOFTWARE_RESET_ON);
		if (rval < 0) {
			dev_err(dev, "software reset failed\n");
			goto out_cci_addr_fail;
		}

		do {
			rval = ccs_read(sensor, SOFTWARE_RESET, &reset);
			reset = !rval && reset == CCS_SOFTWARE_RESET_OFF;
			if (reset)
				break;

			usleep_range(1000, 2000);
		} while (--retry);

		if (!reset) {
			dev_err(dev, "software reset failed\n");
			rval = -EIO;
			goto out_cci_addr_fail;
		}
	}

	if (sensor->hwcfg.i2c_addr_alt) {
		rval = ccs_change_cci_addr(sensor);
		if (rval) {
			dev_err(dev, "cci address change error\n");
			goto out_cci_addr_fail;
		}
	}

	rval = ccs_write(sensor, COMPRESSION_MODE,
			 CCS_COMPRESSION_MODE_DPCM_PCM_SIMPLE);
	if (rval) {
		dev_err(dev, "compression mode set failed\n");
		goto out_cci_addr_fail;
	}

	rval = ccs_write(sensor, EXTCLK_FREQUENCY_MHZ,
			 sensor->hwcfg.ext_clk / (1000000 / (1 << 8)));
	if (rval) {
		dev_err(dev, "extclk frequency set failed\n");
		goto out_cci_addr_fail;
	}

	rval = ccs_write(sensor, CSI_LANE_MODE, sensor->hwcfg.lanes - 1);
	if (rval) {
		dev_err(dev, "csi lane mode set failed\n");
		goto out_cci_addr_fail;
	}

	rval = ccs_write(sensor, FAST_STANDBY_CTRL,
			 CCS_FAST_STANDBY_CTRL_FRAME_TRUNCATION);
	if (rval) {
		dev_err(dev, "fast standby set failed\n");
		goto out_cci_addr_fail;
	}

	rval = ccs_write(sensor, CSI_SIGNALING_MODE,
			 sensor->hwcfg.csi_signalling_mode);
	if (rval) {
		dev_err(dev, "csi signalling mode set failed\n");
		goto out_cci_addr_fail;
	}

	rval = ccs_update_phy_ctrl(sensor);
	if (rval < 0)
		goto out_cci_addr_fail;

	rval = ccs_write_msr_regs(sensor);
	if (rval)
		goto out_cci_addr_fail;

	rval = ccs_call_quirk(sensor, post_poweron);
	if (rval) {
		dev_err(dev, "post_poweron quirks failed\n");
		goto out_cci_addr_fail;
	}

	return 0;

out_cci_addr_fail:
	gpiod_set_value(sensor->reset, 1);
	gpiod_set_value(sensor->xshutdown, 0);
	clk_disable_unprepare(sensor->ext_clk);

out_xclk_fail:
	regulator_bulk_disable(ARRAY_SIZE(ccs_regulators),
			       sensor->regulators);

	return rval;
}

static int ccs_power_off(struct device *dev)
{
	struct v4l2_subdev *subdev = dev_get_drvdata(dev);
	struct ccs_subdev *ssd = to_ccs_subdev(subdev);
	struct ccs_sensor *sensor =
		container_of(ssd, struct ccs_sensor, ssds[0]);

	/*
	 * Currently power/clock to lens are enable/disabled separately
	 * but they are essentially the same signals. So if the sensor is
	 * powered off while the lens is powered on the sensor does not
	 * really see a power off and next time the cci address change
	 * will fail. So do a soft reset explicitly here.
	 */
	if (sensor->hwcfg.i2c_addr_alt)
		ccs_write(sensor, SOFTWARE_RESET, CCS_SOFTWARE_RESET_ON);

	gpiod_set_value(sensor->reset, 1);
	gpiod_set_value(sensor->xshutdown, 0);
	clk_disable_unprepare(sensor->ext_clk);
	usleep_range(5000, 5000);
	regulator_bulk_disable(ARRAY_SIZE(ccs_regulators),
			       sensor->regulators);
	sensor->streaming = false;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Video stream management
 */

static int ccs_start_streaming(struct ccs_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	unsigned int binning_mode;
	int rval;

	mutex_lock(&sensor->mutex);

	rval = ccs_write(sensor, CSI_DATA_FORMAT,
			 (sensor->csi_format->width << 8) |
			 sensor->csi_format->compressed);
	if (rval)
		goto out;

	/* Binning configuration */
	if (sensor->binning_horizontal == 1 &&
	    sensor->binning_vertical == 1) {
		binning_mode = 0;
	} else {
		u8 binning_type =
			(sensor->binning_horizontal << 4)
			| sensor->binning_vertical;

		rval = ccs_write(sensor, BINNING_TYPE, binning_type);
		if (rval < 0)
			goto out;

		binning_mode = 1;
	}
	rval = ccs_write(sensor, BINNING_MODE, binning_mode);
	if (rval < 0)
		goto out;

	/* Set up PLL */
	rval = ccs_pll_configure(sensor);
	if (rval)
		goto out;

	/* Analog crop start coordinates */
	rval = ccs_write(sensor, X_ADDR_START,
			 sensor->pixel_array->crop[CCS_PA_PAD_SRC].left);
	if (rval < 0)
		goto out;

	rval = ccs_write(sensor, Y_ADDR_START,
			 sensor->pixel_array->crop[CCS_PA_PAD_SRC].top);
	if (rval < 0)
		goto out;

	/* Analog crop end coordinates */
	rval = ccs_write(
		sensor, X_ADDR_END,
		sensor->pixel_array->crop[CCS_PA_PAD_SRC].left
		+ sensor->pixel_array->crop[CCS_PA_PAD_SRC].width - 1);
	if (rval < 0)
		goto out;

	rval = ccs_write(
		sensor, Y_ADDR_END,
		sensor->pixel_array->crop[CCS_PA_PAD_SRC].top
		+ sensor->pixel_array->crop[CCS_PA_PAD_SRC].height - 1);
	if (rval < 0)
		goto out;

	/*
	 * Output from pixel array, including blanking, is set using
	 * controls below. No need to set here.
	 */

	/* Digital crop */
	if (CCS_LIM(sensor, DIGITAL_CROP_CAPABILITY)
	    == CCS_DIGITAL_CROP_CAPABILITY_INPUT_CROP) {
		rval = ccs_write(
			sensor, DIGITAL_CROP_X_OFFSET,
			sensor->scaler->crop[CCS_PAD_SINK].left);
		if (rval < 0)
			goto out;

		rval = ccs_write(
			sensor, DIGITAL_CROP_Y_OFFSET,
			sensor->scaler->crop[CCS_PAD_SINK].top);
		if (rval < 0)
			goto out;

		rval = ccs_write(
			sensor, DIGITAL_CROP_IMAGE_WIDTH,
			sensor->scaler->crop[CCS_PAD_SINK].width);
		if (rval < 0)
			goto out;

		rval = ccs_write(
			sensor, DIGITAL_CROP_IMAGE_HEIGHT,
			sensor->scaler->crop[CCS_PAD_SINK].height);
		if (rval < 0)
			goto out;
	}

	/* Scaling */
	if (CCS_LIM(sensor, SCALING_CAPABILITY)
	    != CCS_SCALING_CAPABILITY_NONE) {
		rval = ccs_write(sensor, SCALING_MODE, sensor->scaling_mode);
		if (rval < 0)
			goto out;

		rval = ccs_write(sensor, SCALE_M, sensor->scale_m);
		if (rval < 0)
			goto out;
	}

	/* Output size from sensor */
	rval = ccs_write(sensor, X_OUTPUT_SIZE,
			 sensor->src->crop[CCS_PAD_SRC].width);
	if (rval < 0)
		goto out;
	rval = ccs_write(sensor, Y_OUTPUT_SIZE,
			 sensor->src->crop[CCS_PAD_SRC].height);
	if (rval < 0)
		goto out;

	if (CCS_LIM(sensor, FLASH_MODE_CAPABILITY) &
	    (CCS_FLASH_MODE_CAPABILITY_SINGLE_STROBE |
	     SMIAPP_FLASH_MODE_CAPABILITY_MULTIPLE_STROBE) &&
	    sensor->hwcfg.strobe_setup != NULL &&
	    sensor->hwcfg.strobe_setup->trigger != 0) {
		rval = ccs_setup_flash_strobe(sensor);
		if (rval)
			goto out;
	}

	rval = ccs_call_quirk(sensor, pre_streamon);
	if (rval) {
		dev_err(&client->dev, "pre_streamon quirks failed\n");
		goto out;
	}

	rval = ccs_write(sensor, MODE_SELECT, CCS_MODE_SELECT_STREAMING);

out:
	mutex_unlock(&sensor->mutex);

	return rval;
}

static int ccs_stop_streaming(struct ccs_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int rval;

	mutex_lock(&sensor->mutex);
	rval = ccs_write(sensor, MODE_SELECT, CCS_MODE_SELECT_SOFTWARE_STANDBY);
	if (rval)
		goto out;

	rval = ccs_call_quirk(sensor, post_streamoff);
	if (rval)
		dev_err(&client->dev, "post_streamoff quirks failed\n");

out:
	mutex_unlock(&sensor->mutex);
	return rval;
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev video operations
 */

static int ccs_pm_get_init(struct ccs_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int rval;

	/*
	 * It can't use pm_runtime_resume_and_get() here, as the driver
	 * relies at the returned value to detect if the device was already
	 * active or not.
	 */
	rval = pm_runtime_get_sync(&client->dev);
	if (rval < 0)
		goto error;

	/* Device was already active, so don't set controls */
	if (rval == 1)
		return 0;

	/* Restore V4L2 controls to the previously suspended device */
	rval = v4l2_ctrl_handler_setup(&sensor->pixel_array->ctrl_handler);
	if (rval)
		goto error;

	rval = v4l2_ctrl_handler_setup(&sensor->src->ctrl_handler);
	if (rval)
		goto error;

	/* Keep PM runtime usage_count incremented on success */
	return 0;
error:
	pm_runtime_put(&client->dev);
	return rval;
}

static int ccs_set_stream(struct v4l2_subdev *subdev, int enable)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int rval;

	if (sensor->streaming == enable)
		return 0;

	if (!enable) {
		ccs_stop_streaming(sensor);
		sensor->streaming = false;
		pm_runtime_mark_last_busy(&client->dev);
		pm_runtime_put_autosuspend(&client->dev);

		return 0;
	}

	rval = ccs_pm_get_init(sensor);
	if (rval)
		return rval;

	sensor->streaming = true;

	rval = ccs_start_streaming(sensor);
	if (rval < 0) {
		sensor->streaming = false;
		pm_runtime_mark_last_busy(&client->dev);
		pm_runtime_put_autosuspend(&client->dev);
	}

	return rval;
}

static int ccs_pre_streamon(struct v4l2_subdev *subdev, u32 flags)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int rval;

	if (flags & V4L2_SUBDEV_PRE_STREAMON_FL_MANUAL_LP) {
		switch (sensor->hwcfg.csi_signalling_mode) {
		case CCS_CSI_SIGNALING_MODE_CSI_2_DPHY:
			if (!(CCS_LIM(sensor, PHY_CTRL_CAPABILITY_2) &
			      CCS_PHY_CTRL_CAPABILITY_2_MANUAL_LP_DPHY))
				return -EACCES;
			break;
		case CCS_CSI_SIGNALING_MODE_CSI_2_CPHY:
			if (!(CCS_LIM(sensor, PHY_CTRL_CAPABILITY_2) &
			      CCS_PHY_CTRL_CAPABILITY_2_MANUAL_LP_CPHY))
				return -EACCES;
			break;
		default:
			return -EACCES;
		}
	}

	rval = ccs_pm_get_init(sensor);
	if (rval)
		return rval;

	if (flags & V4L2_SUBDEV_PRE_STREAMON_FL_MANUAL_LP) {
		rval = ccs_write(sensor, MANUAL_LP_CTRL,
				 CCS_MANUAL_LP_CTRL_ENABLE);
		if (rval)
			pm_runtime_put(&client->dev);
	}

	return rval;
}

static int ccs_post_streamoff(struct v4l2_subdev *subdev)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);

	return pm_runtime_put(&client->dev);
}

static int ccs_enum_mbus_code(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_mbus_code_enum *code)
{
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	unsigned int i;
	int idx = -1;
	int rval = -EINVAL;

	mutex_lock(&sensor->mutex);

	dev_err(&client->dev, "subdev %s, pad %d, index %d\n",
		subdev->name, code->pad, code->index);

	if (subdev != &sensor->src->sd || code->pad != CCS_PAD_SRC) {
		if (code->index)
			goto out;

		code->code = sensor->internal_csi_format->code;
		rval = 0;
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(ccs_csi_data_formats); i++) {
		if (sensor->mbus_frame_fmts & (1 << i))
			idx++;

		if (idx == code->index) {
			code->code = ccs_csi_data_formats[i].code;
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

static u32 __ccs_get_mbus_code(struct v4l2_subdev *subdev, unsigned int pad)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);

	if (subdev == &sensor->src->sd && pad == CCS_PAD_SRC)
		return sensor->csi_format->code;
	else
		return sensor->internal_csi_format->code;
}

static int __ccs_get_format(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_state *sd_state,
			    struct v4l2_subdev_format *fmt)
{
	struct ccs_subdev *ssd = to_ccs_subdev(subdev);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		fmt->format = *v4l2_subdev_get_try_format(subdev, sd_state,
							  fmt->pad);
	} else {
		struct v4l2_rect *r;

		if (fmt->pad == ssd->source_pad)
			r = &ssd->crop[ssd->source_pad];
		else
			r = &ssd->sink_fmt;

		fmt->format.code = __ccs_get_mbus_code(subdev, fmt->pad);
		fmt->format.width = r->width;
		fmt->format.height = r->height;
		fmt->format.field = V4L2_FIELD_NONE;
	}

	return 0;
}

static int ccs_get_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	int rval;

	mutex_lock(&sensor->mutex);
	rval = __ccs_get_format(subdev, sd_state, fmt);
	mutex_unlock(&sensor->mutex);

	return rval;
}

static void ccs_get_crop_compose(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_rect **crops,
				 struct v4l2_rect **comps, int which)
{
	struct ccs_subdev *ssd = to_ccs_subdev(subdev);
	unsigned int i;

	if (which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		if (crops)
			for (i = 0; i < subdev->entity.num_pads; i++)
				crops[i] = &ssd->crop[i];
		if (comps)
			*comps = &ssd->compose;
	} else {
		if (crops) {
			for (i = 0; i < subdev->entity.num_pads; i++)
				crops[i] = v4l2_subdev_get_try_crop(subdev,
								    sd_state,
								    i);
		}
		if (comps)
			*comps = v4l2_subdev_get_try_compose(subdev, sd_state,
							     CCS_PAD_SINK);
	}
}

/* Changes require propagation only on sink pad. */
static void ccs_propagate(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_state *sd_state, int which,
			  int target)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	struct ccs_subdev *ssd = to_ccs_subdev(subdev);
	struct v4l2_rect *comp, *crops[CCS_PADS];

	ccs_get_crop_compose(subdev, sd_state, crops, &comp, which);

	switch (target) {
	case V4L2_SEL_TGT_CROP:
		comp->width = crops[CCS_PAD_SINK]->width;
		comp->height = crops[CCS_PAD_SINK]->height;
		if (which == V4L2_SUBDEV_FORMAT_ACTIVE) {
			if (ssd == sensor->scaler) {
				sensor->scale_m = CCS_LIM(sensor, SCALER_N_MIN);
				sensor->scaling_mode =
					CCS_SCALING_MODE_NO_SCALING;
			} else if (ssd == sensor->binner) {
				sensor->binning_horizontal = 1;
				sensor->binning_vertical = 1;
			}
		}
		fallthrough;
	case V4L2_SEL_TGT_COMPOSE:
		*crops[CCS_PAD_SRC] = *comp;
		break;
	default:
		WARN_ON_ONCE(1);
	}
}

static const struct ccs_csi_data_format
*ccs_validate_csi_data_format(struct ccs_sensor *sensor, u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ccs_csi_data_formats); i++) {
		if (sensor->mbus_frame_fmts & (1 << i) &&
		    ccs_csi_data_formats[i].code == code)
			return &ccs_csi_data_formats[i];
	}

	return sensor->csi_format;
}

static int ccs_set_format_source(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	const struct ccs_csi_data_format *csi_format,
		*old_csi_format = sensor->csi_format;
	unsigned long *valid_link_freqs;
	u32 code = fmt->format.code;
	unsigned int i;
	int rval;

	rval = __ccs_get_format(subdev, sd_state, fmt);
	if (rval)
		return rval;

	/*
	 * Media bus code is changeable on src subdev's source pad. On
	 * other source pads we just get format here.
	 */
	if (subdev != &sensor->src->sd)
		return 0;

	csi_format = ccs_validate_csi_data_format(sensor, code);

	fmt->format.code = csi_format->code;

	if (fmt->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return 0;

	sensor->csi_format = csi_format;

	if (csi_format->width != old_csi_format->width)
		for (i = 0; i < ARRAY_SIZE(sensor->test_data); i++)
			__v4l2_ctrl_modify_range(
				sensor->test_data[i], 0,
				(1 << csi_format->width) - 1, 1, 0);

	if (csi_format->compressed == old_csi_format->compressed)
		return 0;

	valid_link_freqs =
		&sensor->valid_link_freqs[sensor->csi_format->compressed
					  - sensor->compressed_min_bpp];

	__v4l2_ctrl_modify_range(
		sensor->link_freq, 0,
		__fls(*valid_link_freqs), ~*valid_link_freqs,
		__ffs(*valid_link_freqs));

	return ccs_pll_update(sensor);
}

static int ccs_set_format(struct v4l2_subdev *subdev,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	struct ccs_subdev *ssd = to_ccs_subdev(subdev);
	struct v4l2_rect *crops[CCS_PADS];

	mutex_lock(&sensor->mutex);

	if (fmt->pad == ssd->source_pad) {
		int rval;

		rval = ccs_set_format_source(subdev, sd_state, fmt);

		mutex_unlock(&sensor->mutex);

		return rval;
	}

	/* Sink pad. Width and height are changeable here. */
	fmt->format.code = __ccs_get_mbus_code(subdev, fmt->pad);
	fmt->format.width &= ~1;
	fmt->format.height &= ~1;
	fmt->format.field = V4L2_FIELD_NONE;

	fmt->format.width =
		clamp(fmt->format.width,
		      CCS_LIM(sensor, MIN_X_OUTPUT_SIZE),
		      CCS_LIM(sensor, MAX_X_OUTPUT_SIZE));
	fmt->format.height =
		clamp(fmt->format.height,
		      CCS_LIM(sensor, MIN_Y_OUTPUT_SIZE),
		      CCS_LIM(sensor, MAX_Y_OUTPUT_SIZE));

	ccs_get_crop_compose(subdev, sd_state, crops, NULL, fmt->which);

	crops[ssd->sink_pad]->left = 0;
	crops[ssd->sink_pad]->top = 0;
	crops[ssd->sink_pad]->width = fmt->format.width;
	crops[ssd->sink_pad]->height = fmt->format.height;
	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		ssd->sink_fmt = *crops[ssd->sink_pad];
	ccs_propagate(subdev, sd_state, fmt->which, V4L2_SEL_TGT_CROP);

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
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	int val = 0;

	w &= ~1;
	ask_w &= ~1;
	h &= ~1;
	ask_h &= ~1;

	if (flags & V4L2_SEL_FLAG_GE) {
		if (w < ask_w)
			val -= SCALING_GOODNESS;
		if (h < ask_h)
			val -= SCALING_GOODNESS;
	}

	if (flags & V4L2_SEL_FLAG_LE) {
		if (w > ask_w)
			val -= SCALING_GOODNESS;
		if (h > ask_h)
			val -= SCALING_GOODNESS;
	}

	val -= abs(w - ask_w);
	val -= abs(h - ask_h);

	if (w < CCS_LIM(sensor, MIN_X_OUTPUT_SIZE))
		val -= SCALING_GOODNESS_EXTREME;

	dev_dbg(&client->dev, "w %d ask_w %d h %d ask_h %d goodness %d\n",
		w, ask_w, h, ask_h, val);

	return val;
}

static void ccs_set_compose_binner(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_selection *sel,
				   struct v4l2_rect **crops,
				   struct v4l2_rect *comp)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	unsigned int i;
	unsigned int binh = 1, binv = 1;
	int best = scaling_goodness(
		subdev,
		crops[CCS_PAD_SINK]->width, sel->r.width,
		crops[CCS_PAD_SINK]->height, sel->r.height, sel->flags);

	for (i = 0; i < sensor->nbinning_subtypes; i++) {
		int this = scaling_goodness(
			subdev,
			crops[CCS_PAD_SINK]->width
			/ sensor->binning_subtypes[i].horizontal,
			sel->r.width,
			crops[CCS_PAD_SINK]->height
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

	sel->r.width = (crops[CCS_PAD_SINK]->width / binh) & ~1;
	sel->r.height = (crops[CCS_PAD_SINK]->height / binv) & ~1;
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
static void ccs_set_compose_scaler(struct v4l2_subdev *subdev,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_selection *sel,
				   struct v4l2_rect **crops,
				   struct v4l2_rect *comp)
{
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	u32 min, max, a, b, max_m;
	u32 scale_m = CCS_LIM(sensor, SCALER_N_MIN);
	int mode = CCS_SCALING_MODE_HORIZONTAL;
	u32 try[4];
	u32 ntry = 0;
	unsigned int i;
	int best = INT_MIN;

	sel->r.width = min_t(unsigned int, sel->r.width,
			     crops[CCS_PAD_SINK]->width);
	sel->r.height = min_t(unsigned int, sel->r.height,
			      crops[CCS_PAD_SINK]->height);

	a = crops[CCS_PAD_SINK]->width
		* CCS_LIM(sensor, SCALER_N_MIN) / sel->r.width;
	b = crops[CCS_PAD_SINK]->height
		* CCS_LIM(sensor, SCALER_N_MIN) / sel->r.height;
	max_m = crops[CCS_PAD_SINK]->width
		* CCS_LIM(sensor, SCALER_N_MIN)
		/ CCS_LIM(sensor, MIN_X_OUTPUT_SIZE);

	a = clamp(a, CCS_LIM(sensor, SCALER_M_MIN),
		  CCS_LIM(sensor, SCALER_M_MAX));
	b = clamp(b, CCS_LIM(sensor, SCALER_M_MIN),
		  CCS_LIM(sensor, SCALER_M_MAX));
	max_m = clamp(max_m, CCS_LIM(sensor, SCALER_M_MIN),
		      CCS_LIM(sensor, SCALER_M_MAX));

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
			crops[CCS_PAD_SINK]->width
			/ try[i] * CCS_LIM(sensor, SCALER_N_MIN),
			sel->r.width,
			crops[CCS_PAD_SINK]->height,
			sel->r.height,
			sel->flags);

		dev_dbg(&client->dev, "trying factor %d (%d)\n", try[i], i);

		if (this > best) {
			scale_m = try[i];
			mode = CCS_SCALING_MODE_HORIZONTAL;
			best = this;
		}

		if (CCS_LIM(sensor, SCALING_CAPABILITY)
		    == CCS_SCALING_CAPABILITY_HORIZONTAL)
			continue;

		this = scaling_goodness(
			subdev, crops[CCS_PAD_SINK]->width
			/ try[i]
			* CCS_LIM(sensor, SCALER_N_MIN),
			sel->r.width,
			crops[CCS_PAD_SINK]->height
			/ try[i]
			* CCS_LIM(sensor, SCALER_N_MIN),
			sel->r.height,
			sel->flags);

		if (this > best) {
			scale_m = try[i];
			mode = SMIAPP_SCALING_MODE_BOTH;
			best = this;
		}
	}

	sel->r.width =
		(crops[CCS_PAD_SINK]->width
		 / scale_m
		 * CCS_LIM(sensor, SCALER_N_MIN)) & ~1;
	if (mode == SMIAPP_SCALING_MODE_BOTH)
		sel->r.height =
			(crops[CCS_PAD_SINK]->height
			 / scale_m
			 * CCS_LIM(sensor, SCALER_N_MIN))
			& ~1;
	else
		sel->r.height = crops[CCS_PAD_SINK]->height;

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		sensor->scale_m = scale_m;
		sensor->scaling_mode = mode;
	}
}
/* We're only called on source pads. This function sets scaling. */
static int ccs_set_compose(struct v4l2_subdev *subdev,
			   struct v4l2_subdev_state *sd_state,
			   struct v4l2_subdev_selection *sel)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	struct ccs_subdev *ssd = to_ccs_subdev(subdev);
	struct v4l2_rect *comp, *crops[CCS_PADS];

	ccs_get_crop_compose(subdev, sd_state, crops, &comp, sel->which);

	sel->r.top = 0;
	sel->r.left = 0;

	if (ssd == sensor->binner)
		ccs_set_compose_binner(subdev, sd_state, sel, crops, comp);
	else
		ccs_set_compose_scaler(subdev, sd_state, sel, crops, comp);

	*comp = sel->r;
	ccs_propagate(subdev, sd_state, sel->which, V4L2_SEL_TGT_COMPOSE);

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		return ccs_pll_blanking_update(sensor);

	return 0;
}

static int __ccs_sel_supported(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_selection *sel)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	struct ccs_subdev *ssd = to_ccs_subdev(subdev);

	/* We only implement crop in three places. */
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (ssd == sensor->pixel_array && sel->pad == CCS_PA_PAD_SRC)
			return 0;
		if (ssd == sensor->src && sel->pad == CCS_PAD_SRC)
			return 0;
		if (ssd == sensor->scaler && sel->pad == CCS_PAD_SINK &&
		    CCS_LIM(sensor, DIGITAL_CROP_CAPABILITY)
		    == CCS_DIGITAL_CROP_CAPABILITY_INPUT_CROP)
			return 0;
		return -EINVAL;
	case V4L2_SEL_TGT_NATIVE_SIZE:
		if (ssd == sensor->pixel_array && sel->pad == CCS_PA_PAD_SRC)
			return 0;
		return -EINVAL;
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		if (sel->pad == ssd->source_pad)
			return -EINVAL;
		if (ssd == sensor->binner)
			return 0;
		if (ssd == sensor->scaler && CCS_LIM(sensor, SCALING_CAPABILITY)
		    != CCS_SCALING_CAPABILITY_NONE)
			return 0;
		fallthrough;
	default:
		return -EINVAL;
	}
}

static int ccs_set_crop(struct v4l2_subdev *subdev,
			struct v4l2_subdev_state *sd_state,
			struct v4l2_subdev_selection *sel)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	struct ccs_subdev *ssd = to_ccs_subdev(subdev);
	struct v4l2_rect *src_size, *crops[CCS_PADS];
	struct v4l2_rect _r;

	ccs_get_crop_compose(subdev, sd_state, crops, NULL, sel->which);

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		if (sel->pad == ssd->sink_pad)
			src_size = &ssd->sink_fmt;
		else
			src_size = &ssd->compose;
	} else {
		if (sel->pad == ssd->sink_pad) {
			_r.left = 0;
			_r.top = 0;
			_r.width = v4l2_subdev_get_try_format(subdev,
							      sd_state,
							      sel->pad)
				->width;
			_r.height = v4l2_subdev_get_try_format(subdev,
							       sd_state,
							       sel->pad)
				->height;
			src_size = &_r;
		} else {
			src_size = v4l2_subdev_get_try_compose(
				subdev, sd_state, ssd->sink_pad);
		}
	}

	if (ssd == sensor->src && sel->pad == CCS_PAD_SRC) {
		sel->r.left = 0;
		sel->r.top = 0;
	}

	sel->r.width = min(sel->r.width, src_size->width);
	sel->r.height = min(sel->r.height, src_size->height);

	sel->r.left = min_t(int, sel->r.left, src_size->width - sel->r.width);
	sel->r.top = min_t(int, sel->r.top, src_size->height - sel->r.height);

	*crops[sel->pad] = sel->r;

	if (ssd != sensor->pixel_array && sel->pad == CCS_PAD_SINK)
		ccs_propagate(subdev, sd_state, sel->which, V4L2_SEL_TGT_CROP);

	return 0;
}

static void ccs_get_native_size(struct ccs_subdev *ssd, struct v4l2_rect *r)
{
	r->top = 0;
	r->left = 0;
	r->width = CCS_LIM(ssd->sensor, X_ADDR_MAX) + 1;
	r->height = CCS_LIM(ssd->sensor, Y_ADDR_MAX) + 1;
}

static int __ccs_get_selection(struct v4l2_subdev *subdev,
			       struct v4l2_subdev_state *sd_state,
			       struct v4l2_subdev_selection *sel)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	struct ccs_subdev *ssd = to_ccs_subdev(subdev);
	struct v4l2_rect *comp, *crops[CCS_PADS];
	struct v4l2_rect sink_fmt;
	int ret;

	ret = __ccs_sel_supported(subdev, sel);
	if (ret)
		return ret;

	ccs_get_crop_compose(subdev, sd_state, crops, &comp, sel->which);

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		sink_fmt = ssd->sink_fmt;
	} else {
		struct v4l2_mbus_framefmt *fmt =
			v4l2_subdev_get_try_format(subdev, sd_state,
						   ssd->sink_pad);

		sink_fmt.left = 0;
		sink_fmt.top = 0;
		sink_fmt.width = fmt->width;
		sink_fmt.height = fmt->height;
	}

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_NATIVE_SIZE:
		if (ssd == sensor->pixel_array)
			ccs_get_native_size(ssd, &sel->r);
		else if (sel->pad == ssd->sink_pad)
			sel->r = sink_fmt;
		else
			sel->r = *comp;
		break;
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		sel->r = *crops[sel->pad];
		break;
	case V4L2_SEL_TGT_COMPOSE:
		sel->r = *comp;
		break;
	}

	return 0;
}

static int ccs_get_selection(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_selection *sel)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	int rval;

	mutex_lock(&sensor->mutex);
	rval = __ccs_get_selection(subdev, sd_state, sel);
	mutex_unlock(&sensor->mutex);

	return rval;
}

static int ccs_set_selection(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_selection *sel)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	int ret;

	ret = __ccs_sel_supported(subdev, sel);
	if (ret)
		return ret;

	mutex_lock(&sensor->mutex);

	sel->r.left = max(0, sel->r.left & ~1);
	sel->r.top = max(0, sel->r.top & ~1);
	sel->r.width = CCS_ALIGN_DIM(sel->r.width, sel->flags);
	sel->r.height =	CCS_ALIGN_DIM(sel->r.height, sel->flags);

	sel->r.width = max_t(unsigned int, CCS_LIM(sensor, MIN_X_OUTPUT_SIZE),
			     sel->r.width);
	sel->r.height = max_t(unsigned int, CCS_LIM(sensor, MIN_Y_OUTPUT_SIZE),
			      sel->r.height);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		ret = ccs_set_crop(subdev, sd_state, sel);
		break;
	case V4L2_SEL_TGT_COMPOSE:
		ret = ccs_set_compose(subdev, sd_state, sel);
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&sensor->mutex);
	return ret;
}

static int ccs_get_skip_frames(struct v4l2_subdev *subdev, u32 *frames)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);

	*frames = sensor->frame_skip;
	return 0;
}

static int ccs_get_skip_top_lines(struct v4l2_subdev *subdev, u32 *lines)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);

	*lines = sensor->image_start;

	return 0;
}

/* -----------------------------------------------------------------------------
 * sysfs attributes
 */

static ssize_t
nvm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(to_i2c_client(dev));
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	int rval;

	if (!sensor->dev_init_done)
		return -EBUSY;

	rval = ccs_pm_get_init(sensor);
	if (rval < 0)
		return -ENODEV;

	rval = ccs_read_nvm(sensor, buf, PAGE_SIZE);
	if (rval < 0) {
		pm_runtime_put(&client->dev);
		dev_err(&client->dev, "nvm read failed\n");
		return -ENODEV;
	}

	pm_runtime_mark_last_busy(&client->dev);
	pm_runtime_put_autosuspend(&client->dev);

	/*
	 * NVM is still way below a PAGE_SIZE, so we can safely
	 * assume this for now.
	 */
	return rval;
}
static DEVICE_ATTR_RO(nvm);

static ssize_t
ident_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(to_i2c_client(dev));
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	struct ccs_module_info *minfo = &sensor->minfo;

	if (minfo->mipi_manufacturer_id)
		return snprintf(buf, PAGE_SIZE, "%4.4x%4.4x%2.2x\n",
				minfo->mipi_manufacturer_id, minfo->model_id,
				minfo->revision_number) + 1;
	else
		return snprintf(buf, PAGE_SIZE, "%2.2x%4.4x%2.2x\n",
				minfo->smia_manufacturer_id, minfo->model_id,
				minfo->revision_number) + 1;
}
static DEVICE_ATTR_RO(ident);

/* -----------------------------------------------------------------------------
 * V4L2 subdev core operations
 */

static int ccs_identify_module(struct ccs_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	struct ccs_module_info *minfo = &sensor->minfo;
	unsigned int i;
	u32 rev;
	int rval = 0;

	/* Module info */
	rval = ccs_read(sensor, MODULE_MANUFACTURER_ID,
			&minfo->mipi_manufacturer_id);
	if (!rval && !minfo->mipi_manufacturer_id)
		rval = ccs_read_addr_8only(sensor,
					   SMIAPP_REG_U8_MANUFACTURER_ID,
					   &minfo->smia_manufacturer_id);
	if (!rval)
		rval = ccs_read_addr_8only(sensor, CCS_R_MODULE_MODEL_ID,
					   &minfo->model_id);
	if (!rval)
		rval = ccs_read_addr_8only(sensor,
					   CCS_R_MODULE_REVISION_NUMBER_MAJOR,
					   &rev);
	if (!rval) {
		rval = ccs_read_addr_8only(sensor,
					   CCS_R_MODULE_REVISION_NUMBER_MINOR,
					   &minfo->revision_number);
		minfo->revision_number |= rev << 8;
	}
	if (!rval)
		rval = ccs_read_addr_8only(sensor, CCS_R_MODULE_DATE_YEAR,
					   &minfo->module_year);
	if (!rval)
		rval = ccs_read_addr_8only(sensor, CCS_R_MODULE_DATE_MONTH,
					   &minfo->module_month);
	if (!rval)
		rval = ccs_read_addr_8only(sensor, CCS_R_MODULE_DATE_DAY,
					   &minfo->module_day);

	/* Sensor info */
	if (!rval)
		rval = ccs_read(sensor, SENSOR_MANUFACTURER_ID,
				&minfo->sensor_mipi_manufacturer_id);
	if (!rval && !minfo->sensor_mipi_manufacturer_id)
		rval = ccs_read_addr_8only(sensor,
					   CCS_R_SENSOR_MANUFACTURER_ID,
					   &minfo->sensor_smia_manufacturer_id);
	if (!rval)
		rval = ccs_read_addr_8only(sensor,
					   CCS_R_SENSOR_MODEL_ID,
					   &minfo->sensor_model_id);
	if (!rval)
		rval = ccs_read_addr_8only(sensor,
					   CCS_R_SENSOR_REVISION_NUMBER,
					   &minfo->sensor_revision_number);
	if (!rval)
		rval = ccs_read_addr_8only(sensor,
					   CCS_R_SENSOR_FIRMWARE_VERSION,
					   &minfo->sensor_firmware_version);

	/* SMIA */
	if (!rval)
		rval = ccs_read(sensor, MIPI_CCS_VERSION, &minfo->ccs_version);
	if (!rval && !minfo->ccs_version)
		rval = ccs_read_addr_8only(sensor, SMIAPP_REG_U8_SMIA_VERSION,
					   &minfo->smia_version);
	if (!rval && !minfo->ccs_version)
		rval = ccs_read_addr_8only(sensor, SMIAPP_REG_U8_SMIAPP_VERSION,
					   &minfo->smiapp_version);

	if (rval) {
		dev_err(&client->dev, "sensor detection failed\n");
		return -ENODEV;
	}

	if (minfo->mipi_manufacturer_id)
		dev_dbg(&client->dev, "MIPI CCS module 0x%4.4x-0x%4.4x\n",
			minfo->mipi_manufacturer_id, minfo->model_id);
	else
		dev_dbg(&client->dev, "SMIA module 0x%2.2x-0x%4.4x\n",
			minfo->smia_manufacturer_id, minfo->model_id);

	dev_dbg(&client->dev,
		"module revision 0x%4.4x date %2.2d-%2.2d-%2.2d\n",
		minfo->revision_number, minfo->module_year, minfo->module_month,
		minfo->module_day);

	if (minfo->sensor_mipi_manufacturer_id)
		dev_dbg(&client->dev, "MIPI CCS sensor 0x%4.4x-0x%4.4x\n",
			minfo->sensor_mipi_manufacturer_id,
			minfo->sensor_model_id);
	else
		dev_dbg(&client->dev, "SMIA sensor 0x%2.2x-0x%4.4x\n",
			minfo->sensor_smia_manufacturer_id,
			minfo->sensor_model_id);

	dev_dbg(&client->dev,
		"sensor revision 0x%2.2x firmware version 0x%2.2x\n",
		minfo->sensor_revision_number, minfo->sensor_firmware_version);

	if (minfo->ccs_version) {
		dev_dbg(&client->dev, "MIPI CCS version %u.%u",
			(minfo->ccs_version & CCS_MIPI_CCS_VERSION_MAJOR_MASK)
			>> CCS_MIPI_CCS_VERSION_MAJOR_SHIFT,
			(minfo->ccs_version & CCS_MIPI_CCS_VERSION_MINOR_MASK));
		minfo->name = CCS_NAME;
	} else {
		dev_dbg(&client->dev,
			"smia version %2.2d smiapp version %2.2d\n",
			minfo->smia_version, minfo->smiapp_version);
		minfo->name = SMIAPP_NAME;
	}

	/*
	 * Some modules have bad data in the lvalues below. Hope the
	 * rvalues have better stuff. The lvalues are module
	 * parameters whereas the rvalues are sensor parameters.
	 */
	if (minfo->sensor_smia_manufacturer_id &&
	    !minfo->smia_manufacturer_id && !minfo->model_id) {
		minfo->smia_manufacturer_id =
			minfo->sensor_smia_manufacturer_id;
		minfo->model_id = minfo->sensor_model_id;
		minfo->revision_number = minfo->sensor_revision_number;
	}

	for (i = 0; i < ARRAY_SIZE(ccs_module_idents); i++) {
		if (ccs_module_idents[i].mipi_manufacturer_id &&
		    ccs_module_idents[i].mipi_manufacturer_id
		    != minfo->mipi_manufacturer_id)
			continue;
		if (ccs_module_idents[i].smia_manufacturer_id &&
		    ccs_module_idents[i].smia_manufacturer_id
		    != minfo->smia_manufacturer_id)
			continue;
		if (ccs_module_idents[i].model_id != minfo->model_id)
			continue;
		if (ccs_module_idents[i].flags
		    & CCS_MODULE_IDENT_FLAG_REV_LE) {
			if (ccs_module_idents[i].revision_number_major
			    < (minfo->revision_number >> 8))
				continue;
		} else {
			if (ccs_module_idents[i].revision_number_major
			    != (minfo->revision_number >> 8))
				continue;
		}

		minfo->name = ccs_module_idents[i].name;
		minfo->quirk = ccs_module_idents[i].quirk;
		break;
	}

	if (i >= ARRAY_SIZE(ccs_module_idents))
		dev_warn(&client->dev,
			 "no quirks for this module; let's hope it's fully compliant\n");

	dev_dbg(&client->dev, "the sensor is called %s\n", minfo->name);

	return 0;
}

static const struct v4l2_subdev_ops ccs_ops;
static const struct v4l2_subdev_internal_ops ccs_internal_ops;
static const struct media_entity_operations ccs_entity_ops;

static int ccs_register_subdev(struct ccs_sensor *sensor,
			       struct ccs_subdev *ssd,
			       struct ccs_subdev *sink_ssd,
			       u16 source_pad, u16 sink_pad, u32 link_flags)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	int rval;

	if (!sink_ssd)
		return 0;

	rval = media_entity_pads_init(&ssd->sd.entity, ssd->npads, ssd->pads);
	if (rval) {
		dev_err(&client->dev, "media_entity_pads_init failed\n");
		return rval;
	}

	rval = v4l2_device_register_subdev(sensor->src->sd.v4l2_dev, &ssd->sd);
	if (rval) {
		dev_err(&client->dev, "v4l2_device_register_subdev failed\n");
		return rval;
	}

	rval = media_create_pad_link(&ssd->sd.entity, source_pad,
				     &sink_ssd->sd.entity, sink_pad,
				     link_flags);
	if (rval) {
		dev_err(&client->dev, "media_create_pad_link failed\n");
		v4l2_device_unregister_subdev(&ssd->sd);
		return rval;
	}

	return 0;
}

static void ccs_unregistered(struct v4l2_subdev *subdev)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	unsigned int i;

	for (i = 1; i < sensor->ssds_used; i++)
		v4l2_device_unregister_subdev(&sensor->ssds[i].sd);
}

static int ccs_registered(struct v4l2_subdev *subdev)
{
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	int rval;

	if (sensor->scaler) {
		rval = ccs_register_subdev(sensor, sensor->binner,
					   sensor->scaler,
					   CCS_PAD_SRC, CCS_PAD_SINK,
					   MEDIA_LNK_FL_ENABLED |
					   MEDIA_LNK_FL_IMMUTABLE);
		if (rval < 0)
			return rval;
	}

	rval = ccs_register_subdev(sensor, sensor->pixel_array, sensor->binner,
				   CCS_PA_PAD_SRC, CCS_PAD_SINK,
				   MEDIA_LNK_FL_ENABLED |
				   MEDIA_LNK_FL_IMMUTABLE);
	if (rval)
		goto out_err;

	return 0;

out_err:
	ccs_unregistered(subdev);

	return rval;
}

static void ccs_cleanup(struct ccs_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);

	device_remove_file(&client->dev, &dev_attr_nvm);
	device_remove_file(&client->dev, &dev_attr_ident);

	ccs_free_controls(sensor);
}

static void ccs_create_subdev(struct ccs_sensor *sensor,
			      struct ccs_subdev *ssd, const char *name,
			      unsigned short num_pads, u32 function)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);

	if (!ssd)
		return;

	if (ssd != sensor->src)
		v4l2_subdev_init(&ssd->sd, &ccs_ops);

	ssd->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ssd->sd.entity.function = function;
	ssd->sensor = sensor;

	ssd->npads = num_pads;
	ssd->source_pad = num_pads - 1;

	v4l2_i2c_subdev_set_name(&ssd->sd, client, sensor->minfo.name, name);

	ccs_get_native_size(ssd, &ssd->sink_fmt);

	ssd->compose.width = ssd->sink_fmt.width;
	ssd->compose.height = ssd->sink_fmt.height;
	ssd->crop[ssd->source_pad] = ssd->compose;
	ssd->pads[ssd->source_pad].flags = MEDIA_PAD_FL_SOURCE;
	if (ssd != sensor->pixel_array) {
		ssd->crop[ssd->sink_pad] = ssd->compose;
		ssd->pads[ssd->sink_pad].flags = MEDIA_PAD_FL_SINK;
	}

	ssd->sd.entity.ops = &ccs_entity_ops;

	if (ssd == sensor->src)
		return;

	ssd->sd.internal_ops = &ccs_internal_ops;
	ssd->sd.owner = THIS_MODULE;
	ssd->sd.dev = &client->dev;
	v4l2_set_subdevdata(&ssd->sd, client);
}

static int ccs_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ccs_subdev *ssd = to_ccs_subdev(sd);
	struct ccs_sensor *sensor = ssd->sensor;
	unsigned int i;

	mutex_lock(&sensor->mutex);

	for (i = 0; i < ssd->npads; i++) {
		struct v4l2_mbus_framefmt *try_fmt =
			v4l2_subdev_get_try_format(sd, fh->state, i);
		struct v4l2_rect *try_crop =
			v4l2_subdev_get_try_crop(sd, fh->state, i);
		struct v4l2_rect *try_comp;

		ccs_get_native_size(ssd, try_crop);

		try_fmt->width = try_crop->width;
		try_fmt->height = try_crop->height;
		try_fmt->code = sensor->internal_csi_format->code;
		try_fmt->field = V4L2_FIELD_NONE;

		if (ssd != sensor->pixel_array)
			continue;

		try_comp = v4l2_subdev_get_try_compose(sd, fh->state, i);
		*try_comp = *try_crop;
	}

	mutex_unlock(&sensor->mutex);

	return 0;
}

static const struct v4l2_subdev_video_ops ccs_video_ops = {
	.s_stream = ccs_set_stream,
	.pre_streamon = ccs_pre_streamon,
	.post_streamoff = ccs_post_streamoff,
};

static const struct v4l2_subdev_pad_ops ccs_pad_ops = {
	.enum_mbus_code = ccs_enum_mbus_code,
	.get_fmt = ccs_get_format,
	.set_fmt = ccs_set_format,
	.get_selection = ccs_get_selection,
	.set_selection = ccs_set_selection,
};

static const struct v4l2_subdev_sensor_ops ccs_sensor_ops = {
	.g_skip_frames = ccs_get_skip_frames,
	.g_skip_top_lines = ccs_get_skip_top_lines,
};

static const struct v4l2_subdev_ops ccs_ops = {
	.video = &ccs_video_ops,
	.pad = &ccs_pad_ops,
	.sensor = &ccs_sensor_ops,
};

static const struct media_entity_operations ccs_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops ccs_internal_src_ops = {
	.registered = ccs_registered,
	.unregistered = ccs_unregistered,
	.open = ccs_open,
};

static const struct v4l2_subdev_internal_ops ccs_internal_ops = {
	.open = ccs_open,
};

/* -----------------------------------------------------------------------------
 * I2C Driver
 */

static int __maybe_unused ccs_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	bool streaming = sensor->streaming;
	int rval;

	rval = pm_runtime_resume_and_get(dev);
	if (rval < 0)
		return rval;

	if (sensor->streaming)
		ccs_stop_streaming(sensor);

	/* save state for resume */
	sensor->streaming = streaming;

	return 0;
}

static int __maybe_unused ccs_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	int rval = 0;

	pm_runtime_put(dev);

	if (sensor->streaming)
		rval = ccs_start_streaming(sensor);

	return rval;
}

static int ccs_get_hwconfig(struct ccs_sensor *sensor, struct device *dev)
{
	struct ccs_hwconfig *hwcfg = &sensor->hwcfg;
	struct v4l2_fwnode_endpoint bus_cfg = { .bus_type = V4L2_MBUS_UNKNOWN };
	struct fwnode_handle *ep;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	u32 rotation;
	int i;
	int rval;

	ep = fwnode_graph_get_endpoint_by_id(fwnode, 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!ep)
		return -ENODEV;

	/*
	 * Note that we do need to rely on detecting the bus type between CSI-2
	 * D-PHY and CCP2 as the old bindings did not require it.
	 */
	rval = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	if (rval)
		goto out_err;

	switch (bus_cfg.bus_type) {
	case V4L2_MBUS_CSI2_DPHY:
		hwcfg->csi_signalling_mode = CCS_CSI_SIGNALING_MODE_CSI_2_DPHY;
		hwcfg->lanes = bus_cfg.bus.mipi_csi2.num_data_lanes;
		break;
	case V4L2_MBUS_CSI2_CPHY:
		hwcfg->csi_signalling_mode = CCS_CSI_SIGNALING_MODE_CSI_2_CPHY;
		hwcfg->lanes = bus_cfg.bus.mipi_csi2.num_data_lanes;
		break;
	case V4L2_MBUS_CSI1:
	case V4L2_MBUS_CCP2:
		hwcfg->csi_signalling_mode = (bus_cfg.bus.mipi_csi1.strobe) ?
		SMIAPP_CSI_SIGNALLING_MODE_CCP2_DATA_STROBE :
		SMIAPP_CSI_SIGNALLING_MODE_CCP2_DATA_CLOCK;
		hwcfg->lanes = 1;
		break;
	default:
		dev_err(dev, "unsupported bus %u\n", bus_cfg.bus_type);
		rval = -EINVAL;
		goto out_err;
	}

	dev_dbg(dev, "lanes %u\n", hwcfg->lanes);

	rval = fwnode_property_read_u32(fwnode, "rotation", &rotation);
	if (!rval) {
		switch (rotation) {
		case 180:
			hwcfg->module_board_orient =
				CCS_MODULE_BOARD_ORIENT_180;
			fallthrough;
		case 0:
			break;
		default:
			dev_err(dev, "invalid rotation %u\n", rotation);
			rval = -EINVAL;
			goto out_err;
		}
	}

	rval = fwnode_property_read_u32(dev_fwnode(dev), "clock-frequency",
					&hwcfg->ext_clk);
	if (rval)
		dev_info(dev, "can't get clock-frequency\n");

	dev_dbg(dev, "clk %d, mode %d\n", hwcfg->ext_clk,
		hwcfg->csi_signalling_mode);

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_warn(dev, "no link frequencies defined\n");
		rval = -EINVAL;
		goto out_err;
	}

	hwcfg->op_sys_clock = devm_kcalloc(
		dev, bus_cfg.nr_of_link_frequencies + 1 /* guardian */,
		sizeof(*hwcfg->op_sys_clock), GFP_KERNEL);
	if (!hwcfg->op_sys_clock) {
		rval = -ENOMEM;
		goto out_err;
	}

	for (i = 0; i < bus_cfg.nr_of_link_frequencies; i++) {
		hwcfg->op_sys_clock[i] = bus_cfg.link_frequencies[i];
		dev_dbg(dev, "freq %d: %lld\n", i, hwcfg->op_sys_clock[i]);
	}

	v4l2_fwnode_endpoint_free(&bus_cfg);
	fwnode_handle_put(ep);

	return 0;

out_err:
	v4l2_fwnode_endpoint_free(&bus_cfg);
	fwnode_handle_put(ep);

	return rval;
}

static int ccs_probe(struct i2c_client *client)
{
	struct ccs_sensor *sensor;
	const struct firmware *fw;
	char filename[40];
	unsigned int i;
	int rval;

	sensor = devm_kzalloc(&client->dev, sizeof(*sensor), GFP_KERNEL);
	if (sensor == NULL)
		return -ENOMEM;

	rval = ccs_get_hwconfig(sensor, &client->dev);
	if (rval)
		return rval;

	sensor->src = &sensor->ssds[sensor->ssds_used];

	v4l2_i2c_subdev_init(&sensor->src->sd, client, &ccs_ops);
	sensor->src->sd.internal_ops = &ccs_internal_src_ops;

	sensor->regulators = devm_kcalloc(&client->dev,
					  ARRAY_SIZE(ccs_regulators),
					  sizeof(*sensor->regulators),
					  GFP_KERNEL);
	if (!sensor->regulators)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(ccs_regulators); i++)
		sensor->regulators[i].supply = ccs_regulators[i];

	rval = devm_regulator_bulk_get(&client->dev, ARRAY_SIZE(ccs_regulators),
				       sensor->regulators);
	if (rval) {
		dev_err(&client->dev, "could not get regulators\n");
		return rval;
	}

	sensor->ext_clk = devm_clk_get(&client->dev, NULL);
	if (PTR_ERR(sensor->ext_clk) == -ENOENT) {
		dev_info(&client->dev, "no clock defined, continuing...\n");
		sensor->ext_clk = NULL;
	} else if (IS_ERR(sensor->ext_clk)) {
		dev_err(&client->dev, "could not get clock (%ld)\n",
			PTR_ERR(sensor->ext_clk));
		return -EPROBE_DEFER;
	}

	if (sensor->ext_clk) {
		if (sensor->hwcfg.ext_clk) {
			unsigned long rate;

			rval = clk_set_rate(sensor->ext_clk,
					    sensor->hwcfg.ext_clk);
			if (rval < 0) {
				dev_err(&client->dev,
					"unable to set clock freq to %u\n",
					sensor->hwcfg.ext_clk);
				return rval;
			}

			rate = clk_get_rate(sensor->ext_clk);
			if (rate != sensor->hwcfg.ext_clk) {
				dev_err(&client->dev,
					"can't set clock freq, asked for %u but got %lu\n",
					sensor->hwcfg.ext_clk, rate);
				return -EINVAL;
			}
		} else {
			sensor->hwcfg.ext_clk = clk_get_rate(sensor->ext_clk);
			dev_dbg(&client->dev, "obtained clock freq %u\n",
				sensor->hwcfg.ext_clk);
		}
	} else if (sensor->hwcfg.ext_clk) {
		dev_dbg(&client->dev, "assuming clock freq %u\n",
			sensor->hwcfg.ext_clk);
	} else {
		dev_err(&client->dev, "unable to obtain clock freq\n");
		return -EINVAL;
	}

	if (!sensor->hwcfg.ext_clk) {
		dev_err(&client->dev, "cannot work with xclk frequency 0\n");
		return -EINVAL;
	}

	sensor->reset = devm_gpiod_get_optional(&client->dev, "reset",
						GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset))
		return PTR_ERR(sensor->reset);
	/* Support old users that may have used "xshutdown" property. */
	if (!sensor->reset)
		sensor->xshutdown = devm_gpiod_get_optional(&client->dev,
							    "xshutdown",
							    GPIOD_OUT_LOW);
	if (IS_ERR(sensor->xshutdown))
		return PTR_ERR(sensor->xshutdown);

	rval = ccs_power_on(&client->dev);
	if (rval < 0)
		return rval;

	mutex_init(&sensor->mutex);

	rval = ccs_identify_module(sensor);
	if (rval) {
		rval = -ENODEV;
		goto out_power_off;
	}

	rval = snprintf(filename, sizeof(filename),
			"ccs/ccs-sensor-%4.4x-%4.4x-%4.4x.fw",
			sensor->minfo.sensor_mipi_manufacturer_id,
			sensor->minfo.sensor_model_id,
			sensor->minfo.sensor_revision_number);
	if (rval >= sizeof(filename)) {
		rval = -ENOMEM;
		goto out_power_off;
	}

	rval = request_firmware(&fw, filename, &client->dev);
	if (!rval) {
		ccs_data_parse(&sensor->sdata, fw->data, fw->size, &client->dev,
			       true);
		release_firmware(fw);
	}

	rval = snprintf(filename, sizeof(filename),
			"ccs/ccs-module-%4.4x-%4.4x-%4.4x.fw",
			sensor->minfo.mipi_manufacturer_id,
			sensor->minfo.model_id,
			sensor->minfo.revision_number);
	if (rval >= sizeof(filename)) {
		rval = -ENOMEM;
		goto out_release_sdata;
	}

	rval = request_firmware(&fw, filename, &client->dev);
	if (!rval) {
		ccs_data_parse(&sensor->mdata, fw->data, fw->size, &client->dev,
			       true);
		release_firmware(fw);
	}

	rval = ccs_read_all_limits(sensor);
	if (rval)
		goto out_release_mdata;

	rval = ccs_read_frame_fmt(sensor);
	if (rval) {
		rval = -ENODEV;
		goto out_free_ccs_limits;
	}

	rval = ccs_update_phy_ctrl(sensor);
	if (rval < 0)
		goto out_free_ccs_limits;

	/*
	 * Handle Sensor Module orientation on the board.
	 *
	 * The application of H-FLIP and V-FLIP on the sensor is modified by
	 * the sensor orientation on the board.
	 *
	 * For CCS_BOARD_SENSOR_ORIENT_180 the default behaviour is to set
	 * both H-FLIP and V-FLIP for normal operation which also implies
	 * that a set/unset operation for user space HFLIP and VFLIP v4l2
	 * controls will need to be internally inverted.
	 *
	 * Rotation also changes the bayer pattern.
	 */
	if (sensor->hwcfg.module_board_orient ==
	    CCS_MODULE_BOARD_ORIENT_180)
		sensor->hvflip_inv_mask =
			CCS_IMAGE_ORIENTATION_HORIZONTAL_MIRROR |
			CCS_IMAGE_ORIENTATION_VERTICAL_FLIP;

	rval = ccs_call_quirk(sensor, limits);
	if (rval) {
		dev_err(&client->dev, "limits quirks failed\n");
		goto out_free_ccs_limits;
	}

	if (CCS_LIM(sensor, BINNING_CAPABILITY)) {
		sensor->nbinning_subtypes =
			min_t(u8, CCS_LIM(sensor, BINNING_SUB_TYPES),
			      CCS_LIM_BINNING_SUB_TYPE_MAX_N);

		for (i = 0; i < sensor->nbinning_subtypes; i++) {
			sensor->binning_subtypes[i].horizontal =
				CCS_LIM_AT(sensor, BINNING_SUB_TYPE, i) >>
				CCS_BINNING_SUB_TYPE_COLUMN_SHIFT;
			sensor->binning_subtypes[i].vertical =
				CCS_LIM_AT(sensor, BINNING_SUB_TYPE, i) &
				CCS_BINNING_SUB_TYPE_ROW_MASK;

			dev_dbg(&client->dev, "binning %xx%x\n",
				sensor->binning_subtypes[i].horizontal,
				sensor->binning_subtypes[i].vertical);
		}
	}
	sensor->binning_horizontal = 1;
	sensor->binning_vertical = 1;

	if (device_create_file(&client->dev, &dev_attr_ident) != 0) {
		dev_err(&client->dev, "sysfs ident entry creation failed\n");
		rval = -ENOENT;
		goto out_free_ccs_limits;
	}

	if (sensor->minfo.smiapp_version &&
	    CCS_LIM(sensor, DATA_TRANSFER_IF_CAPABILITY) &
	    CCS_DATA_TRANSFER_IF_CAPABILITY_SUPPORTED) {
		if (device_create_file(&client->dev, &dev_attr_nvm) != 0) {
			dev_err(&client->dev, "sysfs nvm entry failed\n");
			rval = -EBUSY;
			goto out_cleanup;
		}
	}

	if (!CCS_LIM(sensor, MIN_OP_SYS_CLK_DIV) ||
	    !CCS_LIM(sensor, MAX_OP_SYS_CLK_DIV) ||
	    !CCS_LIM(sensor, MIN_OP_PIX_CLK_DIV) ||
	    !CCS_LIM(sensor, MAX_OP_PIX_CLK_DIV)) {
		/* No OP clock branch */
		sensor->pll.flags |= CCS_PLL_FLAG_NO_OP_CLOCKS;
	} else if (CCS_LIM(sensor, SCALING_CAPABILITY)
		   != CCS_SCALING_CAPABILITY_NONE ||
		   CCS_LIM(sensor, DIGITAL_CROP_CAPABILITY)
		   == CCS_DIGITAL_CROP_CAPABILITY_INPUT_CROP) {
		/* We have a scaler or digital crop. */
		sensor->scaler = &sensor->ssds[sensor->ssds_used];
		sensor->ssds_used++;
	}
	sensor->binner = &sensor->ssds[sensor->ssds_used];
	sensor->ssds_used++;
	sensor->pixel_array = &sensor->ssds[sensor->ssds_used];
	sensor->ssds_used++;

	sensor->scale_m = CCS_LIM(sensor, SCALER_N_MIN);

	/* prepare PLL configuration input values */
	sensor->pll.bus_type = CCS_PLL_BUS_TYPE_CSI2_DPHY;
	sensor->pll.csi2.lanes = sensor->hwcfg.lanes;
	if (CCS_LIM(sensor, CLOCK_CALCULATION) &
	    CCS_CLOCK_CALCULATION_LANE_SPEED) {
		sensor->pll.flags |= CCS_PLL_FLAG_LANE_SPEED_MODEL;
		if (CCS_LIM(sensor, CLOCK_CALCULATION) &
		    CCS_CLOCK_CALCULATION_LINK_DECOUPLED) {
			sensor->pll.vt_lanes =
				CCS_LIM(sensor, NUM_OF_VT_LANES) + 1;
			sensor->pll.op_lanes =
				CCS_LIM(sensor, NUM_OF_OP_LANES) + 1;
			sensor->pll.flags |= CCS_PLL_FLAG_LINK_DECOUPLED;
		} else {
			sensor->pll.vt_lanes = sensor->pll.csi2.lanes;
			sensor->pll.op_lanes = sensor->pll.csi2.lanes;
		}
	}
	if (CCS_LIM(sensor, CLOCK_TREE_PLL_CAPABILITY) &
	    CCS_CLOCK_TREE_PLL_CAPABILITY_EXT_DIVIDER)
		sensor->pll.flags |= CCS_PLL_FLAG_EXT_IP_PLL_DIVIDER;
	if (CCS_LIM(sensor, CLOCK_TREE_PLL_CAPABILITY) &
	    CCS_CLOCK_TREE_PLL_CAPABILITY_FLEXIBLE_OP_PIX_CLK_DIV)
		sensor->pll.flags |= CCS_PLL_FLAG_FLEXIBLE_OP_PIX_CLK_DIV;
	if (CCS_LIM(sensor, FIFO_SUPPORT_CAPABILITY) &
	    CCS_FIFO_SUPPORT_CAPABILITY_DERATING)
		sensor->pll.flags |= CCS_PLL_FLAG_FIFO_DERATING;
	if (CCS_LIM(sensor, FIFO_SUPPORT_CAPABILITY) &
	    CCS_FIFO_SUPPORT_CAPABILITY_DERATING_OVERRATING)
		sensor->pll.flags |= CCS_PLL_FLAG_FIFO_DERATING |
				     CCS_PLL_FLAG_FIFO_OVERRATING;
	if (CCS_LIM(sensor, CLOCK_TREE_PLL_CAPABILITY) &
	    CCS_CLOCK_TREE_PLL_CAPABILITY_DUAL_PLL) {
		if (CCS_LIM(sensor, CLOCK_TREE_PLL_CAPABILITY) &
		    CCS_CLOCK_TREE_PLL_CAPABILITY_SINGLE_PLL) {
			u32 v;

			/* Use sensor default in PLL mode selection */
			rval = ccs_read(sensor, PLL_MODE, &v);
			if (rval)
				goto out_cleanup;

			if (v == CCS_PLL_MODE_DUAL)
				sensor->pll.flags |= CCS_PLL_FLAG_DUAL_PLL;
		} else {
			sensor->pll.flags |= CCS_PLL_FLAG_DUAL_PLL;
		}
		if (CCS_LIM(sensor, CLOCK_CALCULATION) &
		    CCS_CLOCK_CALCULATION_DUAL_PLL_OP_SYS_DDR)
			sensor->pll.flags |= CCS_PLL_FLAG_OP_SYS_DDR;
		if (CCS_LIM(sensor, CLOCK_CALCULATION) &
		    CCS_CLOCK_CALCULATION_DUAL_PLL_OP_PIX_DDR)
			sensor->pll.flags |= CCS_PLL_FLAG_OP_PIX_DDR;
	}
	sensor->pll.op_bits_per_lane = CCS_LIM(sensor, OP_BITS_PER_LANE);
	sensor->pll.ext_clk_freq_hz = sensor->hwcfg.ext_clk;
	sensor->pll.scale_n = CCS_LIM(sensor, SCALER_N_MIN);

	ccs_create_subdev(sensor, sensor->scaler, " scaler", 2,
			  MEDIA_ENT_F_PROC_VIDEO_SCALER);
	ccs_create_subdev(sensor, sensor->binner, " binner", 2,
			  MEDIA_ENT_F_PROC_VIDEO_SCALER);
	ccs_create_subdev(sensor, sensor->pixel_array, " pixel_array", 1,
			  MEDIA_ENT_F_CAM_SENSOR);

	rval = ccs_init_controls(sensor);
	if (rval < 0)
		goto out_cleanup;

	rval = ccs_call_quirk(sensor, init);
	if (rval)
		goto out_cleanup;

	rval = ccs_get_mbus_formats(sensor);
	if (rval) {
		rval = -ENODEV;
		goto out_cleanup;
	}

	rval = ccs_init_late_controls(sensor);
	if (rval) {
		rval = -ENODEV;
		goto out_cleanup;
	}

	mutex_lock(&sensor->mutex);
	rval = ccs_pll_blanking_update(sensor);
	mutex_unlock(&sensor->mutex);
	if (rval) {
		dev_err(&client->dev, "update mode failed\n");
		goto out_cleanup;
	}

	sensor->streaming = false;
	sensor->dev_init_done = true;

	rval = media_entity_pads_init(&sensor->src->sd.entity, 2,
				 sensor->src->pads);
	if (rval < 0)
		goto out_media_entity_cleanup;

	rval = ccs_write_msr_regs(sensor);
	if (rval)
		goto out_media_entity_cleanup;

	pm_runtime_set_active(&client->dev);
	pm_runtime_get_noresume(&client->dev);
	pm_runtime_enable(&client->dev);

	rval = v4l2_async_register_subdev_sensor(&sensor->src->sd);
	if (rval < 0)
		goto out_disable_runtime_pm;

	pm_runtime_set_autosuspend_delay(&client->dev, 1000);
	pm_runtime_use_autosuspend(&client->dev);
	pm_runtime_put_autosuspend(&client->dev);

	return 0;

out_disable_runtime_pm:
	pm_runtime_put_noidle(&client->dev);
	pm_runtime_disable(&client->dev);

out_media_entity_cleanup:
	media_entity_cleanup(&sensor->src->sd.entity);

out_cleanup:
	ccs_cleanup(sensor);

out_release_mdata:
	kvfree(sensor->mdata.backing);

out_release_sdata:
	kvfree(sensor->sdata.backing);

out_free_ccs_limits:
	kfree(sensor->ccs_limits);

out_power_off:
	ccs_power_off(&client->dev);
	mutex_destroy(&sensor->mutex);

	return rval;
}

static int ccs_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ccs_sensor *sensor = to_ccs_sensor(subdev);
	unsigned int i;

	v4l2_async_unregister_subdev(subdev);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		ccs_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	for (i = 0; i < sensor->ssds_used; i++) {
		v4l2_device_unregister_subdev(&sensor->ssds[i].sd);
		media_entity_cleanup(&sensor->ssds[i].sd.entity);
	}
	ccs_cleanup(sensor);
	mutex_destroy(&sensor->mutex);
	kfree(sensor->ccs_limits);
	kvfree(sensor->sdata.backing);
	kvfree(sensor->mdata.backing);

	return 0;
}

static const struct ccs_device smia_device = {
	.flags = CCS_DEVICE_FLAG_IS_SMIA,
};

static const struct ccs_device ccs_device = {};

static const struct acpi_device_id ccs_acpi_table[] = {
	{ .id = "MIPI0200", .driver_data = (unsigned long)&ccs_device },
	{ },
};
MODULE_DEVICE_TABLE(acpi, ccs_acpi_table);

static const struct of_device_id ccs_of_table[] = {
	{ .compatible = "mipi-ccs-1.1", .data = &ccs_device },
	{ .compatible = "mipi-ccs-1.0", .data = &ccs_device },
	{ .compatible = "mipi-ccs", .data = &ccs_device },
	{ .compatible = "nokia,smia", .data = &smia_device },
	{ },
};
MODULE_DEVICE_TABLE(of, ccs_of_table);

static const struct dev_pm_ops ccs_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ccs_suspend, ccs_resume)
	SET_RUNTIME_PM_OPS(ccs_power_off, ccs_power_on, NULL)
};

static struct i2c_driver ccs_i2c_driver = {
	.driver	= {
		.acpi_match_table = ccs_acpi_table,
		.of_match_table = ccs_of_table,
		.name = CCS_NAME,
		.pm = &ccs_pm_ops,
	},
	.probe_new = ccs_probe,
	.remove	= ccs_remove,
};

static int ccs_module_init(void)
{
	unsigned int i, l;

	for (i = 0, l = 0; ccs_limits[i].size && l < CCS_L_LAST; i++) {
		if (!(ccs_limits[i].flags & CCS_L_FL_SAME_REG)) {
			ccs_limit_offsets[l + 1].lim =
				ALIGN(ccs_limit_offsets[l].lim +
				      ccs_limits[i].size,
				      ccs_reg_width(ccs_limits[i + 1].reg));
			ccs_limit_offsets[l].info = i;
			l++;
		} else {
			ccs_limit_offsets[l].lim += ccs_limits[i].size;
		}
	}

	if (WARN_ON(ccs_limits[i].size))
		return -EINVAL;

	if (WARN_ON(l != CCS_L_LAST))
		return -EINVAL;

	return i2c_register_driver(THIS_MODULE, &ccs_i2c_driver);
}

static void ccs_module_cleanup(void)
{
	i2c_del_driver(&ccs_i2c_driver);
}

module_init(ccs_module_init);
module_exit(ccs_module_cleanup);

MODULE_AUTHOR("Sakari Ailus <sakari.ailus@linux.intel.com>");
MODULE_DESCRIPTION("Generic MIPI CCS/SMIA/SMIA++ camera sensor driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("smiapp");
