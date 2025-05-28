// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2024 Amlogic, Inc. All rights reserved
 */

#include <linux/media/amlogic/c3-isp-config.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-event.h>

#include "c3-isp-common.h"
#include "c3-isp-regs.h"

#define C3_ISP_CORE_SUBDEV_NAME		"c3-isp-core"

#define C3_ISP_PHASE_OFFSET_0		0
#define C3_ISP_PHASE_OFFSET_1		1
#define C3_ISP_PHASE_OFFSET_NONE	0xff

#define C3_ISP_CORE_DEF_SINK_PAD_FMT	MEDIA_BUS_FMT_SRGGB10_1X10
#define C3_ISP_CORE_DEF_SRC_PAD_FMT	MEDIA_BUS_FMT_YUV10_1X30

/*
 * struct c3_isp_core_format_info - ISP core format information
 *
 * @mbus_code: the mbus code
 * @pads: bitmask detailing valid pads for this mbus_code
 * @xofst: horizontal phase offset of hardware
 * @yofst: vertical phase offset of hardware
 * @is_raw: the raw format flag of mbus code
 */
struct c3_isp_core_format_info {
	u32 mbus_code;
	u32 pads;
	u8 xofst;
	u8 yofst;
	bool is_raw;
};

static const struct c3_isp_core_format_info c3_isp_core_fmts[] = {
	/* RAW formats */
	{
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.pads		= BIT(C3_ISP_CORE_PAD_SINK_VIDEO),
		.xofst		= C3_ISP_PHASE_OFFSET_0,
		.yofst		= C3_ISP_PHASE_OFFSET_1,
		.is_raw		= true,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG10_1X10,
		.pads		= BIT(C3_ISP_CORE_PAD_SINK_VIDEO),
		.xofst		= C3_ISP_PHASE_OFFSET_1,
		.yofst		= C3_ISP_PHASE_OFFSET_1,
		.is_raw		= true,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG10_1X10,
		.pads		= BIT(C3_ISP_CORE_PAD_SINK_VIDEO),
		.xofst		= C3_ISP_PHASE_OFFSET_0,
		.yofst		= C3_ISP_PHASE_OFFSET_0,
		.is_raw		= true,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.pads		= BIT(C3_ISP_CORE_PAD_SINK_VIDEO),
		.xofst		= C3_ISP_PHASE_OFFSET_1,
		.yofst		= C3_ISP_PHASE_OFFSET_0,
		.is_raw		= true,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR12_1X12,
		.pads		= BIT(C3_ISP_CORE_PAD_SINK_VIDEO),
		.xofst		= C3_ISP_PHASE_OFFSET_0,
		.yofst		= C3_ISP_PHASE_OFFSET_1,
		.is_raw		= true,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG12_1X12,
		.pads		= BIT(C3_ISP_CORE_PAD_SINK_VIDEO),
		.xofst		= C3_ISP_PHASE_OFFSET_1,
		.yofst		= C3_ISP_PHASE_OFFSET_1,
		.is_raw		= true,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG12_1X12,
		.pads		= BIT(C3_ISP_CORE_PAD_SINK_VIDEO),
		.xofst		= C3_ISP_PHASE_OFFSET_0,
		.yofst		= C3_ISP_PHASE_OFFSET_0,
		.is_raw		= true,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB12_1X12,
		.pads		= BIT(C3_ISP_CORE_PAD_SINK_VIDEO),
		.xofst		= C3_ISP_PHASE_OFFSET_1,
		.yofst		= C3_ISP_PHASE_OFFSET_0,
		.is_raw		= true,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SRGGB16_1X16,
		.pads		= BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_0)
				| BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_1)
				| BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_2),
		.xofst		= C3_ISP_PHASE_OFFSET_NONE,
		.yofst		= C3_ISP_PHASE_OFFSET_NONE,
		.is_raw		= true,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SBGGR16_1X16,
		.pads		= BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_0)
				| BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_1)
				| BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_2),
		.xofst		= C3_ISP_PHASE_OFFSET_NONE,
		.yofst		= C3_ISP_PHASE_OFFSET_NONE,
		.is_raw		= true,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGRBG16_1X16,
		.pads		= BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_0)
				| BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_1)
				| BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_2),
		.xofst		= C3_ISP_PHASE_OFFSET_NONE,
		.yofst		= C3_ISP_PHASE_OFFSET_NONE,
		.is_raw		= true,
	}, {
		.mbus_code	= MEDIA_BUS_FMT_SGBRG16_1X16,
		.pads		= BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_0)
				| BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_1)
				| BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_2),
		.xofst		= C3_ISP_PHASE_OFFSET_NONE,
		.yofst		= C3_ISP_PHASE_OFFSET_NONE,
		.is_raw		= true,
	},
	/* YUV formats */
	{
		.mbus_code	= MEDIA_BUS_FMT_YUV10_1X30,
		.pads		= BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_0) |
				  BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_1) |
				  BIT(C3_ISP_CORE_PAD_SOURCE_VIDEO_2),
		.xofst		= C3_ISP_PHASE_OFFSET_NONE,
		.yofst		= C3_ISP_PHASE_OFFSET_NONE,
		.is_raw		= false,
	},
};

static const struct c3_isp_core_format_info
*core_find_format_by_code(u32 code, u32 pad)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(c3_isp_core_fmts); i++) {
		const struct c3_isp_core_format_info *info =
			&c3_isp_core_fmts[i];

		if (info->mbus_code == code && info->pads & BIT(pad))
			return info;
	}

	return NULL;
}

static const struct c3_isp_core_format_info
*core_find_format_by_index(u32 index, u32 pad)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(c3_isp_core_fmts); i++) {
		const struct c3_isp_core_format_info *info =
			&c3_isp_core_fmts[i];

		if (!(info->pads & BIT(pad)))
			continue;

		if (!index)
			return info;

		index--;
	}

	return NULL;
}

static void c3_isp_core_enable(struct c3_isp_device *isp)
{
	c3_isp_update_bits(isp, ISP_TOP_IRQ_EN, ISP_TOP_IRQ_EN_FRM_END_MASK,
			   ISP_TOP_IRQ_EN_FRM_END_EN);
	c3_isp_update_bits(isp, ISP_TOP_IRQ_EN, ISP_TOP_IRQ_EN_FRM_RST_MASK,
			   ISP_TOP_IRQ_EN_FRM_RST_EN);

	/* Enable image data to ISP core */
	c3_isp_update_bits(isp, ISP_TOP_PATH_SEL, ISP_TOP_PATH_SEL_CORE_MASK,
			   ISP_TOP_PATH_SEL_CORE_MIPI_CORE);
}

static void c3_isp_core_disable(struct c3_isp_device *isp)
{
	/* Disable image data to ISP core */
	c3_isp_update_bits(isp, ISP_TOP_PATH_SEL, ISP_TOP_PATH_SEL_CORE_MASK,
			   ISP_TOP_PATH_SEL_CORE_CORE_DIS);

	c3_isp_update_bits(isp, ISP_TOP_IRQ_EN, ISP_TOP_IRQ_EN_FRM_END_MASK,
			   ISP_TOP_IRQ_EN_FRM_END_DIS);
	c3_isp_update_bits(isp, ISP_TOP_IRQ_EN, ISP_TOP_IRQ_EN_FRM_RST_MASK,
			   ISP_TOP_IRQ_EN_FRM_RST_DIS);
}

/* Set the phase offset of blc, wb and lns */
static void c3_isp_core_lswb_ofst(struct c3_isp_device *isp,
				  u8 xofst, u8 yofst)
{
	c3_isp_update_bits(isp, ISP_LSWB_BLC_PHSOFST,
			   ISP_LSWB_BLC_PHSOFST_HORIZ_OFST_MASK,
			   ISP_LSWB_BLC_PHSOFST_HORIZ_OFST(xofst));
	c3_isp_update_bits(isp, ISP_LSWB_BLC_PHSOFST,
			   ISP_LSWB_BLC_PHSOFST_VERT_OFST_MASK,
			   ISP_LSWB_BLC_PHSOFST_VERT_OFST(yofst));

	c3_isp_update_bits(isp, ISP_LSWB_WB_PHSOFST,
			   ISP_LSWB_WB_PHSOFST_HORIZ_OFST_MASK,
			   ISP_LSWB_WB_PHSOFST_HORIZ_OFST(xofst));
	c3_isp_update_bits(isp, ISP_LSWB_WB_PHSOFST,
			   ISP_LSWB_WB_PHSOFST_VERT_OFST_MASK,
			   ISP_LSWB_WB_PHSOFST_VERT_OFST(yofst));

	c3_isp_update_bits(isp, ISP_LSWB_LNS_PHSOFST,
			   ISP_LSWB_LNS_PHSOFST_HORIZ_OFST_MASK,
			   ISP_LSWB_LNS_PHSOFST_HORIZ_OFST(xofst));
	c3_isp_update_bits(isp, ISP_LSWB_LNS_PHSOFST,
			   ISP_LSWB_LNS_PHSOFST_VERT_OFST_MASK,
			   ISP_LSWB_LNS_PHSOFST_VERT_OFST(yofst));
}

/* Set the phase offset of af, ae and awb */
static void c3_isp_core_3a_ofst(struct c3_isp_device *isp,
				u8 xofst, u8 yofst)
{
	c3_isp_update_bits(isp, ISP_AF_CTRL, ISP_AF_CTRL_HORIZ_OFST_MASK,
			   ISP_AF_CTRL_HORIZ_OFST(xofst));
	c3_isp_update_bits(isp, ISP_AF_CTRL, ISP_AF_CTRL_VERT_OFST_MASK,
			   ISP_AF_CTRL_VERT_OFST(yofst));

	c3_isp_update_bits(isp, ISP_AE_CTRL, ISP_AE_CTRL_HORIZ_OFST_MASK,
			   ISP_AE_CTRL_HORIZ_OFST(xofst));
	c3_isp_update_bits(isp, ISP_AE_CTRL, ISP_AE_CTRL_VERT_OFST_MASK,
			   ISP_AE_CTRL_VERT_OFST(yofst));

	c3_isp_update_bits(isp, ISP_AWB_CTRL, ISP_AWB_CTRL_HORIZ_OFST_MASK,
			   ISP_AWB_CTRL_HORIZ_OFST(xofst));
	c3_isp_update_bits(isp, ISP_AWB_CTRL, ISP_AWB_CTRL_VERT_OFST_MASK,
			   ISP_AWB_CTRL_VERT_OFST(yofst));
}

/* Set the phase offset of demosaic */
static void c3_isp_core_dms_ofst(struct c3_isp_device *isp,
				 u8 xofst, u8 yofst)
{
	c3_isp_update_bits(isp, ISP_DMS_COMMON_PARAM0,
			   ISP_DMS_COMMON_PARAM0_HORIZ_PHS_OFST_MASK,
			   ISP_DMS_COMMON_PARAM0_HORIZ_PHS_OFST(xofst));
	c3_isp_update_bits(isp, ISP_DMS_COMMON_PARAM0,
			   ISP_DMS_COMMON_PARAM0_VERT_PHS_OFST_MASK,
			   ISP_DMS_COMMON_PARAM0_VERT_PHS_OFST(yofst));
}

static void c3_isp_core_cfg_format(struct c3_isp_device *isp,
				   struct v4l2_subdev_state *state)
{
	struct v4l2_mbus_framefmt *fmt;
	const struct c3_isp_core_format_info *isp_fmt;

	fmt = v4l2_subdev_state_get_format(state, C3_ISP_CORE_PAD_SINK_VIDEO);
	isp_fmt = core_find_format_by_code(fmt->code,
					   C3_ISP_CORE_PAD_SINK_VIDEO);

	c3_isp_write(isp, ISP_TOP_INPUT_SIZE,
		     ISP_TOP_INPUT_SIZE_HORIZ_SIZE(fmt->width) |
		     ISP_TOP_INPUT_SIZE_VERT_SIZE(fmt->height));
	c3_isp_write(isp, ISP_TOP_FRM_SIZE,
		     ISP_TOP_FRM_SIZE_CORE_HORIZ_SIZE(fmt->width) |
		     ISP_TOP_FRM_SIZE_CORE_VERT_SIZE(fmt->height));

	c3_isp_update_bits(isp, ISP_TOP_HOLD_SIZE,
			   ISP_TOP_HOLD_SIZE_CORE_HORIZ_SIZE_MASK,
			   ISP_TOP_HOLD_SIZE_CORE_HORIZ_SIZE(fmt->width));

	c3_isp_write(isp, ISP_AF_HV_SIZE,
		     ISP_AF_HV_SIZE_GLB_WIN_XSIZE(fmt->width) |
		     ISP_AF_HV_SIZE_GLB_WIN_YSIZE(fmt->height));
	c3_isp_write(isp, ISP_AE_HV_SIZE,
		     ISP_AE_HV_SIZE_HORIZ_SIZE(fmt->width) |
		     ISP_AE_HV_SIZE_VERT_SIZE(fmt->height));
	c3_isp_write(isp, ISP_AWB_HV_SIZE,
		     ISP_AWB_HV_SIZE_HORIZ_SIZE(fmt->width) |
		     ISP_AWB_HV_SIZE_VERT_SIZE(fmt->height));

	c3_isp_core_lswb_ofst(isp, isp_fmt->xofst, isp_fmt->yofst);
	c3_isp_core_3a_ofst(isp, isp_fmt->xofst, isp_fmt->yofst);
	c3_isp_core_dms_ofst(isp, isp_fmt->xofst, isp_fmt->yofst);
}

static bool c3_isp_core_streams_ready(struct c3_isp_core *core)
{
	unsigned int n_links = 0;
	struct media_link *link;

	for_each_media_entity_data_link(&core->sd.entity, link) {
		if ((link->source->index == C3_ISP_CORE_PAD_SOURCE_VIDEO_0 ||
		     link->source->index == C3_ISP_CORE_PAD_SOURCE_VIDEO_1 ||
		     link->source->index == C3_ISP_CORE_PAD_SOURCE_VIDEO_2) &&
		    link->flags == MEDIA_LNK_FL_ENABLED)
			n_links++;
	}

	return n_links == core->isp->pipe.start_count;
}

static int c3_isp_core_enable_streams(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      u32 pad, u64 streams_mask)
{
	struct c3_isp_core *core = v4l2_get_subdevdata(sd);
	struct media_pad *sink_pad;
	struct v4l2_subdev *src_sd;
	int ret;

	if (!c3_isp_core_streams_ready(core))
		return 0;

	core->isp->frm_sequence = 0;
	c3_isp_core_cfg_format(core->isp, state);
	c3_isp_core_enable(core->isp);

	sink_pad = &core->pads[C3_ISP_CORE_PAD_SINK_VIDEO];
	core->src_pad = media_pad_remote_pad_unique(sink_pad);
	if (IS_ERR(core->src_pad)) {
		dev_dbg(core->isp->dev,
			"Failed to get source pad for ISP core\n");
		return -EPIPE;
	}

	src_sd = media_entity_to_v4l2_subdev(core->src_pad->entity);

	ret = v4l2_subdev_enable_streams(src_sd, core->src_pad->index, BIT(0));
	if (ret) {
		c3_isp_core_disable(core->isp);
		return ret;
	}

	return 0;
}

static int c3_isp_core_disable_streams(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       u32 pad, u64 streams_mask)
{
	struct c3_isp_core *core = v4l2_get_subdevdata(sd);
	struct v4l2_subdev *src_sd;

	if (core->isp->pipe.start_count != 1)
		return 0;

	if (core->src_pad) {
		src_sd = media_entity_to_v4l2_subdev(core->src_pad->entity);
		v4l2_subdev_disable_streams(src_sd, core->src_pad->index,
					    BIT(0));
	}
	core->src_pad = NULL;

	c3_isp_core_disable(core->isp);

	return 0;
}

static int c3_isp_core_enum_mbus_code(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_mbus_code_enum *code)
{
	const struct c3_isp_core_format_info *info;

	switch (code->pad) {
	case C3_ISP_CORE_PAD_SINK_VIDEO:
	case C3_ISP_CORE_PAD_SOURCE_VIDEO_0:
	case C3_ISP_CORE_PAD_SOURCE_VIDEO_1:
	case C3_ISP_CORE_PAD_SOURCE_VIDEO_2:
		info = core_find_format_by_index(code->index, code->pad);
		if (!info)
			return -EINVAL;

		code->code = info->mbus_code;

		break;
	case C3_ISP_CORE_PAD_SINK_PARAMS:
	case C3_ISP_CORE_PAD_SOURCE_STATS:
		if (code->index)
			return -EINVAL;

		code->code = MEDIA_BUS_FMT_METADATA_FIXED;

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void c3_isp_core_set_sink_fmt(struct v4l2_subdev_state *state,
				     struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *sink_fmt;
	struct v4l2_mbus_framefmt *src_fmt;
	const struct c3_isp_core_format_info *isp_fmt;

	sink_fmt = v4l2_subdev_state_get_format(state, format->pad);

	isp_fmt = core_find_format_by_code(format->format.code, format->pad);
	if (!isp_fmt)
		sink_fmt->code = C3_ISP_CORE_DEF_SINK_PAD_FMT;
	else
		sink_fmt->code = format->format.code;

	sink_fmt->width = clamp_t(u32, format->format.width,
				  C3_ISP_MIN_WIDTH, C3_ISP_MAX_WIDTH);
	sink_fmt->height = clamp_t(u32, format->format.height,
				   C3_ISP_MIN_HEIGHT, C3_ISP_MAX_HEIGHT);
	sink_fmt->field = V4L2_FIELD_NONE;
	sink_fmt->colorspace = V4L2_COLORSPACE_RAW;
	sink_fmt->xfer_func = V4L2_XFER_FUNC_NONE;
	sink_fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	sink_fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	for (unsigned int i = C3_ISP_CORE_PAD_SOURCE_VIDEO_0;
	     i < C3_ISP_CORE_PAD_MAX; i++) {
		src_fmt = v4l2_subdev_state_get_format(state, i);

		src_fmt->width  = sink_fmt->width;
		src_fmt->height = sink_fmt->height;
	}

	format->format = *sink_fmt;
}

static void c3_isp_core_set_source_fmt(struct v4l2_subdev_state *state,
				       struct v4l2_subdev_format *format)
{
	const struct c3_isp_core_format_info *isp_fmt;
	struct v4l2_mbus_framefmt *src_fmt;
	struct v4l2_mbus_framefmt *sink_fmt;

	sink_fmt = v4l2_subdev_state_get_format(state,
						C3_ISP_CORE_PAD_SINK_VIDEO);
	src_fmt = v4l2_subdev_state_get_format(state, format->pad);

	isp_fmt = core_find_format_by_code(format->format.code, format->pad);
	if (!isp_fmt)
		src_fmt->code = C3_ISP_CORE_DEF_SRC_PAD_FMT;
	else
		src_fmt->code = format->format.code;

	src_fmt->width = sink_fmt->width;
	src_fmt->height = sink_fmt->height;
	src_fmt->field = V4L2_FIELD_NONE;
	src_fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;

	if (isp_fmt && isp_fmt->is_raw) {
		src_fmt->colorspace = V4L2_COLORSPACE_RAW;
		src_fmt->xfer_func = V4L2_XFER_FUNC_NONE;
		src_fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	} else {
		src_fmt->colorspace = V4L2_COLORSPACE_SRGB;
		src_fmt->xfer_func = V4L2_XFER_FUNC_SRGB;
		src_fmt->quantization = V4L2_QUANTIZATION_LIM_RANGE;
	}

	format->format = *src_fmt;
}

static int c3_isp_core_set_fmt(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_format *format)
{
	if (format->pad == C3_ISP_CORE_PAD_SINK_VIDEO)
		c3_isp_core_set_sink_fmt(state, format);
	else if (format->pad == C3_ISP_CORE_PAD_SOURCE_VIDEO_0 ||
		 format->pad == C3_ISP_CORE_PAD_SOURCE_VIDEO_1 ||
		 format->pad == C3_ISP_CORE_PAD_SOURCE_VIDEO_2)
		c3_isp_core_set_source_fmt(state, format);
	else
		format->format =
			*v4l2_subdev_state_get_format(state, format->pad);

	return 0;
}

static int c3_isp_core_init_state(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state)
{
	struct v4l2_mbus_framefmt *fmt;

	/* Video sink pad */
	fmt = v4l2_subdev_state_get_format(state, C3_ISP_CORE_PAD_SINK_VIDEO);
	fmt->width = C3_ISP_DEFAULT_WIDTH;
	fmt->height = C3_ISP_DEFAULT_HEIGHT;
	fmt->field = V4L2_FIELD_NONE;
	fmt->code = C3_ISP_CORE_DEF_SINK_PAD_FMT;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;

	/* Video source pad */
	for (unsigned int i = C3_ISP_CORE_PAD_SOURCE_VIDEO_0;
	     i < C3_ISP_CORE_PAD_MAX; i++) {
		fmt = v4l2_subdev_state_get_format(state, i);
		fmt->width = C3_ISP_DEFAULT_WIDTH;
		fmt->height = C3_ISP_DEFAULT_HEIGHT;
		fmt->field = V4L2_FIELD_NONE;
		fmt->code = C3_ISP_CORE_DEF_SRC_PAD_FMT;
		fmt->colorspace = V4L2_COLORSPACE_SRGB;
		fmt->xfer_func = V4L2_XFER_FUNC_SRGB;
		fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
		fmt->quantization = V4L2_QUANTIZATION_LIM_RANGE;
	}

	/* Parameters pad */
	fmt = v4l2_subdev_state_get_format(state, C3_ISP_CORE_PAD_SINK_PARAMS);
	fmt->width = 0;
	fmt->height = 0;
	fmt->field = V4L2_FIELD_NONE;
	fmt->code = MEDIA_BUS_FMT_METADATA_FIXED;

	/* Statistics pad */
	fmt = v4l2_subdev_state_get_format(state, C3_ISP_CORE_PAD_SOURCE_STATS);
	fmt->width = 0;
	fmt->height = 0;
	fmt->field = V4L2_FIELD_NONE;
	fmt->code = MEDIA_BUS_FMT_METADATA_FIXED;

	return 0;
}

static int c3_isp_core_subscribe_event(struct v4l2_subdev *sd,
				       struct v4l2_fh *fh,
				       struct v4l2_event_subscription *sub)
{
	if (sub->type != V4L2_EVENT_FRAME_SYNC)
		return -EINVAL;

	/* V4L2_EVENT_FRAME_SYNC doesn't need id, so should set 0 */
	if (sub->id != 0)
		return -EINVAL;

	return v4l2_event_subscribe(fh, sub, 0, NULL);
}

static const struct v4l2_subdev_pad_ops c3_isp_core_pad_ops = {
	.enum_mbus_code = c3_isp_core_enum_mbus_code,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = c3_isp_core_set_fmt,
	.enable_streams = c3_isp_core_enable_streams,
	.disable_streams = c3_isp_core_disable_streams,
};

static const struct v4l2_subdev_core_ops c3_isp_core_core_ops = {
	.subscribe_event = c3_isp_core_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_ops c3_isp_core_subdev_ops = {
	.core = &c3_isp_core_core_ops,
	.pad = &c3_isp_core_pad_ops,
};

static const struct v4l2_subdev_internal_ops c3_isp_core_internal_ops = {
	.init_state = c3_isp_core_init_state,
};

static int c3_isp_core_link_validate(struct media_link *link)
{
	if (link->sink->index == C3_ISP_CORE_PAD_SINK_PARAMS)
		return 0;

	return v4l2_subdev_link_validate(link);
}

/* Media entity operations */
static const struct media_entity_operations c3_isp_core_entity_ops = {
	.link_validate = c3_isp_core_link_validate,
};

void c3_isp_core_queue_sof(struct c3_isp_device *isp)
{
	struct v4l2_event event = {
		.type = V4L2_EVENT_FRAME_SYNC,
	};

	event.u.frame_sync.frame_sequence = isp->frm_sequence;
	v4l2_event_queue(isp->core.sd.devnode, &event);
}

int c3_isp_core_register(struct c3_isp_device *isp)
{
	struct c3_isp_core *core = &isp->core;
	struct v4l2_subdev *sd = &core->sd;
	int ret;

	v4l2_subdev_init(sd, &c3_isp_core_subdev_ops);
	sd->owner = THIS_MODULE;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	sd->internal_ops = &c3_isp_core_internal_ops;
	snprintf(sd->name, sizeof(sd->name), "%s", C3_ISP_CORE_SUBDEV_NAME);

	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	sd->entity.ops = &c3_isp_core_entity_ops;

	core->isp = isp;
	sd->dev = isp->dev;
	v4l2_set_subdevdata(sd, core);

	core->pads[C3_ISP_CORE_PAD_SINK_VIDEO].flags = MEDIA_PAD_FL_SINK;
	core->pads[C3_ISP_CORE_PAD_SINK_PARAMS].flags = MEDIA_PAD_FL_SINK;
	core->pads[C3_ISP_CORE_PAD_SOURCE_STATS].flags = MEDIA_PAD_FL_SOURCE;
	core->pads[C3_ISP_CORE_PAD_SOURCE_VIDEO_0].flags = MEDIA_PAD_FL_SOURCE;
	core->pads[C3_ISP_CORE_PAD_SOURCE_VIDEO_1].flags = MEDIA_PAD_FL_SOURCE;
	core->pads[C3_ISP_CORE_PAD_SOURCE_VIDEO_2].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, C3_ISP_CORE_PAD_MAX,
				     core->pads);
	if (ret)
		return ret;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret)
		goto err_entity_cleanup;

	ret = v4l2_device_register_subdev(&isp->v4l2_dev, sd);
	if (ret)
		goto err_subdev_cleanup;

	return 0;

err_subdev_cleanup:
	v4l2_subdev_cleanup(sd);
err_entity_cleanup:
	media_entity_cleanup(&sd->entity);
	return ret;
}

void c3_isp_core_unregister(struct c3_isp_device *isp)
{
	struct c3_isp_core *core = &isp->core;
	struct v4l2_subdev *sd = &core->sd;

	v4l2_device_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
}
