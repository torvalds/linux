// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Video Decoder driver
 *
 * Copyright (C) 2019 Collabora, Ltd.
 *
 * Based on rkvdec driver by Google LLC. (Tomasz Figa <tfiga@chromium.org>)
 * Based on s5p-mfc driver by Samsung Electronics Co., Ltd.
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 */

#include <linux/hw_bitfield.h>
#include <linux/clk.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>

#include "rkvdec.h"
#include "rkvdec-regs.h"
#include "rkvdec-vdpu381-regs.h"
#include "rkvdec-vdpu383-regs.h"
#include "rkvdec-rcb.h"

static bool rkvdec_image_fmt_match(enum rkvdec_image_fmt fmt1,
				   enum rkvdec_image_fmt fmt2)
{
	return fmt1 == fmt2 || fmt2 == RKVDEC_IMG_FMT_ANY ||
	       fmt1 == RKVDEC_IMG_FMT_ANY;
}

static bool rkvdec_image_fmt_changed(struct rkvdec_ctx *ctx,
				     enum rkvdec_image_fmt image_fmt)
{
	if (image_fmt == RKVDEC_IMG_FMT_ANY)
		return false;

	return ctx->image_fmt != image_fmt;
}

static u32 rkvdec_enum_decoded_fmt(struct rkvdec_ctx *ctx, int index,
				   enum rkvdec_image_fmt image_fmt)
{
	const struct rkvdec_coded_fmt_desc *desc = ctx->coded_fmt_desc;
	int fmt_idx = -1;
	unsigned int i;

	if (WARN_ON(!desc))
		return 0;

	for (i = 0; i < desc->num_decoded_fmts; i++) {
		if (!rkvdec_image_fmt_match(desc->decoded_fmts[i].image_fmt,
					    image_fmt))
			continue;
		fmt_idx++;
		if (index == fmt_idx)
			return desc->decoded_fmts[i].fourcc;
	}

	return 0;
}

static bool rkvdec_is_valid_fmt(struct rkvdec_ctx *ctx, u32 fourcc,
				enum rkvdec_image_fmt image_fmt)
{
	const struct rkvdec_coded_fmt_desc *desc = ctx->coded_fmt_desc;
	unsigned int i;

	for (i = 0; i < desc->num_decoded_fmts; i++) {
		if (rkvdec_image_fmt_match(desc->decoded_fmts[i].image_fmt,
					   image_fmt) &&
		    desc->decoded_fmts[i].fourcc == fourcc)
			return true;
	}

	return false;
}

static u32 rkvdec_colmv_size(u16 width, u16 height)
{
	return 128 * DIV_ROUND_UP(width, 16) * DIV_ROUND_UP(height, 16);
}

static u32 vdpu383_colmv_size(u16 width, u16 height)
{
	return ALIGN(width, 64) * ALIGN(height, 16);
}

static void rkvdec_fill_decoded_pixfmt(struct rkvdec_ctx *ctx,
				       struct v4l2_pix_format_mplane *pix_mp)
{
	const struct rkvdec_variant *variant = ctx->dev->variant;

	v4l2_fill_pixfmt_mp(pix_mp, pix_mp->pixelformat, pix_mp->width, pix_mp->height);

	ctx->colmv_offset = pix_mp->plane_fmt[0].sizeimage;

	pix_mp->plane_fmt[0].sizeimage += variant->ops->colmv_size(pix_mp->width, pix_mp->height);
}

static void rkvdec_reset_fmt(struct rkvdec_ctx *ctx, struct v4l2_format *f,
			     u32 fourcc)
{
	memset(f, 0, sizeof(*f));
	f->fmt.pix_mp.pixelformat = fourcc;
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709;
	f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
	f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static void rkvdec_reset_decoded_fmt(struct rkvdec_ctx *ctx)
{
	struct v4l2_format *f = &ctx->decoded_fmt;
	u32 fourcc;

	fourcc = rkvdec_enum_decoded_fmt(ctx, 0, ctx->image_fmt);
	rkvdec_reset_fmt(ctx, f, fourcc);
	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f->fmt.pix_mp.width = ctx->coded_fmt.fmt.pix_mp.width;
	f->fmt.pix_mp.height = ctx->coded_fmt.fmt.pix_mp.height;
	rkvdec_fill_decoded_pixfmt(ctx, &f->fmt.pix_mp);
}

static int rkvdec_try_ctrl(struct v4l2_ctrl *ctrl)
{
	struct rkvdec_ctx *ctx = container_of(ctrl->handler, struct rkvdec_ctx, ctrl_hdl);
	const struct rkvdec_coded_fmt_desc *desc = ctx->coded_fmt_desc;

	if (desc->ops->try_ctrl)
		return desc->ops->try_ctrl(ctx, ctrl);

	return 0;
}

static int rkvdec_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct rkvdec_ctx *ctx = container_of(ctrl->handler, struct rkvdec_ctx, ctrl_hdl);
	const struct rkvdec_coded_fmt_desc *desc = ctx->coded_fmt_desc;
	enum rkvdec_image_fmt image_fmt;
	struct vb2_queue *vq;

	if (ctrl->id == V4L2_CID_STATELESS_HEVC_EXT_SPS_ST_RPS) {
		ctx->has_sps_st_rps |= !!(ctrl->has_changed);
		return 0;
	}

	if (ctrl->id == V4L2_CID_STATELESS_HEVC_EXT_SPS_LT_RPS) {
		ctx->has_sps_lt_rps |= !!(ctrl->has_changed);
		return 0;
	}

	/* Check if this change requires a capture format reset */
	if (!desc->ops->get_image_fmt)
		return 0;

	image_fmt = desc->ops->get_image_fmt(ctx, ctrl);
	if (rkvdec_image_fmt_changed(ctx, image_fmt)) {
		vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
				     V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
		if (vb2_is_busy(vq))
			return -EBUSY;

		ctx->image_fmt = image_fmt;
		rkvdec_reset_decoded_fmt(ctx);
	}

	return 0;
}

static const struct v4l2_ctrl_ops rkvdec_ctrl_ops = {
	.try_ctrl = rkvdec_try_ctrl,
	.s_ctrl = rkvdec_s_ctrl,
};

static const struct rkvdec_ctrl_desc rkvdec_hevc_ctrl_descs[] = {
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_SLICE_PARAMS,
		.cfg.flags = V4L2_CTRL_FLAG_DYNAMIC_ARRAY,
		.cfg.type = V4L2_CTRL_TYPE_HEVC_SLICE_PARAMS,
		.cfg.dims = { 600 },
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_SPS,
		.cfg.ops = &rkvdec_ctrl_ops,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_PPS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_SCALING_MATRIX,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_DECODE_PARAMS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_DECODE_MODE,
		.cfg.min = V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED,
		.cfg.max = V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED,
		.cfg.def = V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_START_CODE,
		.cfg.min = V4L2_STATELESS_HEVC_START_CODE_ANNEX_B,
		.cfg.def = V4L2_STATELESS_HEVC_START_CODE_ANNEX_B,
		.cfg.max = V4L2_STATELESS_HEVC_START_CODE_ANNEX_B,
	},
	{
		.cfg.id = V4L2_CID_MPEG_VIDEO_HEVC_PROFILE,
		.cfg.min = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		.cfg.max = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10,
		.cfg.def = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
	},
	{
		.cfg.id = V4L2_CID_MPEG_VIDEO_HEVC_LEVEL,
		.cfg.min = V4L2_MPEG_VIDEO_HEVC_LEVEL_1,
		.cfg.max = V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1,
	},
};

static const struct rkvdec_ctrls rkvdec_hevc_ctrls = {
	.ctrls = rkvdec_hevc_ctrl_descs,
	.num_ctrls = ARRAY_SIZE(rkvdec_hevc_ctrl_descs),
};

static const struct rkvdec_ctrl_desc vdpu38x_hevc_ctrl_descs[] = {
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_DECODE_PARAMS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_SPS,
		.cfg.ops = &rkvdec_ctrl_ops,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_PPS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_SCALING_MATRIX,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_DECODE_MODE,
		.cfg.min = V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED,
		.cfg.max = V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED,
		.cfg.def = V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_START_CODE,
		.cfg.min = V4L2_STATELESS_HEVC_START_CODE_ANNEX_B,
		.cfg.def = V4L2_STATELESS_HEVC_START_CODE_ANNEX_B,
		.cfg.max = V4L2_STATELESS_HEVC_START_CODE_ANNEX_B,
	},
	{
		.cfg.id = V4L2_CID_MPEG_VIDEO_HEVC_PROFILE,
		.cfg.min = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		.cfg.max = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10,
		.cfg.menu_skip_mask =
			BIT(V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE),
		.cfg.def = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
	},
	{
		.cfg.id = V4L2_CID_MPEG_VIDEO_HEVC_LEVEL,
		.cfg.min = V4L2_MPEG_VIDEO_HEVC_LEVEL_1,
		.cfg.max = V4L2_MPEG_VIDEO_HEVC_LEVEL_6_1,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_EXT_SPS_ST_RPS,
		.cfg.ops = &rkvdec_ctrl_ops,
		.cfg.dims = { 65 },
	},
	{
		.cfg.id = V4L2_CID_STATELESS_HEVC_EXT_SPS_LT_RPS,
		.cfg.ops = &rkvdec_ctrl_ops,
		.cfg.dims = { 65 },
	},
};

static const struct rkvdec_ctrls vdpu38x_hevc_ctrls = {
	.ctrls = vdpu38x_hevc_ctrl_descs,
	.num_ctrls = ARRAY_SIZE(vdpu38x_hevc_ctrl_descs),
};

static const struct rkvdec_decoded_fmt_desc rkvdec_hevc_decoded_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.image_fmt = RKVDEC_IMG_FMT_420_8BIT,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV15,
		.image_fmt = RKVDEC_IMG_FMT_420_10BIT,
	},
};

static const struct rkvdec_ctrl_desc rkvdec_h264_ctrl_descs[] = {
	{
		.cfg.id = V4L2_CID_STATELESS_H264_DECODE_PARAMS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_SPS,
		.cfg.ops = &rkvdec_ctrl_ops,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_PPS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_SCALING_MATRIX,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_DECODE_MODE,
		.cfg.min = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
		.cfg.max = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
		.cfg.def = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_START_CODE,
		.cfg.min = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
		.cfg.def = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
		.cfg.max = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
	},
	{
		.cfg.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		.cfg.min = V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE,
		.cfg.max = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422_INTRA,
		.cfg.menu_skip_mask =
			BIT(V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED) |
			BIT(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE),
		.cfg.def = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN,
	},
	{
		.cfg.id = V4L2_CID_MPEG_VIDEO_H264_LEVEL,
		.cfg.min = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		.cfg.max = V4L2_MPEG_VIDEO_H264_LEVEL_5_1,
	},
};

static const struct rkvdec_ctrls rkvdec_h264_ctrls = {
	.ctrls = rkvdec_h264_ctrl_descs,
	.num_ctrls = ARRAY_SIZE(rkvdec_h264_ctrl_descs),
};

static const struct rkvdec_ctrl_desc vdpu38x_h264_ctrl_descs[] = {
	{
		.cfg.id = V4L2_CID_STATELESS_H264_DECODE_PARAMS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_SPS,
		.cfg.ops = &rkvdec_ctrl_ops,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_PPS,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_SCALING_MATRIX,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_DECODE_MODE,
		.cfg.min = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
		.cfg.max = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
		.cfg.def = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_H264_START_CODE,
		.cfg.min = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
		.cfg.def = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
		.cfg.max = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
	},
	{
		.cfg.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		.cfg.min = V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE,
		.cfg.max = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422_INTRA,
		.cfg.menu_skip_mask =
			BIT(V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED) |
			BIT(V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE),
		.cfg.def = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN,
	},
	{
		.cfg.id = V4L2_CID_MPEG_VIDEO_H264_LEVEL,
		.cfg.min = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		.cfg.max = V4L2_MPEG_VIDEO_H264_LEVEL_6_0,
	},
};

static const struct rkvdec_ctrls vdpu38x_h264_ctrls = {
	.ctrls = vdpu38x_h264_ctrl_descs,
	.num_ctrls = ARRAY_SIZE(vdpu38x_h264_ctrl_descs),
};

static const struct rkvdec_decoded_fmt_desc rkvdec_h264_decoded_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.image_fmt = RKVDEC_IMG_FMT_420_8BIT,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV15,
		.image_fmt = RKVDEC_IMG_FMT_420_10BIT,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV16,
		.image_fmt = RKVDEC_IMG_FMT_422_8BIT,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV20,
		.image_fmt = RKVDEC_IMG_FMT_422_10BIT,
	},
};

static const struct rkvdec_ctrl_desc rkvdec_vp9_ctrl_descs[] = {
	{
		.cfg.id = V4L2_CID_STATELESS_VP9_FRAME,
	},
	{
		.cfg.id = V4L2_CID_STATELESS_VP9_COMPRESSED_HDR,
	},
	{
		.cfg.id = V4L2_CID_MPEG_VIDEO_VP9_PROFILE,
		.cfg.min = V4L2_MPEG_VIDEO_VP9_PROFILE_0,
		.cfg.max = V4L2_MPEG_VIDEO_VP9_PROFILE_0,
		.cfg.def = V4L2_MPEG_VIDEO_VP9_PROFILE_0,
	},
};

static const struct rkvdec_ctrls rkvdec_vp9_ctrls = {
	.ctrls = rkvdec_vp9_ctrl_descs,
	.num_ctrls = ARRAY_SIZE(rkvdec_vp9_ctrl_descs),
};

static const struct rkvdec_decoded_fmt_desc rkvdec_vp9_decoded_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.image_fmt = RKVDEC_IMG_FMT_420_8BIT,
	},
};

static const struct rkvdec_coded_fmt_desc rkvdec_coded_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_HEVC_SLICE,
		.frmsize = {
			.min_width = 64,
			.max_width = 4096,
			.step_width = 64,
			.min_height = 64,
			.max_height = 2304,
			.step_height = 16,
		},
		.ctrls = &rkvdec_hevc_ctrls,
		.ops = &rkvdec_hevc_fmt_ops,
		.num_decoded_fmts = ARRAY_SIZE(rkvdec_hevc_decoded_fmts),
		.decoded_fmts = rkvdec_hevc_decoded_fmts,
	},
	{
		.fourcc = V4L2_PIX_FMT_H264_SLICE,
		.frmsize = {
			.min_width = 64,
			.max_width = 4096,
			.step_width = 64,
			.min_height = 48,
			.max_height = 2560,
			.step_height = 16,
		},
		.ctrls = &rkvdec_h264_ctrls,
		.ops = &rkvdec_h264_fmt_ops,
		.num_decoded_fmts = ARRAY_SIZE(rkvdec_h264_decoded_fmts),
		.decoded_fmts = rkvdec_h264_decoded_fmts,
		.subsystem_flags = VB2_V4L2_FL_SUPPORTS_M2M_HOLD_CAPTURE_BUF,
	},
	{
		.fourcc = V4L2_PIX_FMT_VP9_FRAME,
		.frmsize = {
			.min_width = 64,
			.max_width = 4096,
			.step_width = 64,
			.min_height = 64,
			.max_height = 2304,
			.step_height = 64,
		},
		.ctrls = &rkvdec_vp9_ctrls,
		.ops = &rkvdec_vp9_fmt_ops,
		.num_decoded_fmts = ARRAY_SIZE(rkvdec_vp9_decoded_fmts),
		.decoded_fmts = rkvdec_vp9_decoded_fmts,
	}
};

static const struct rkvdec_coded_fmt_desc rk3288_coded_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_HEVC_SLICE,
		.frmsize = {
			.min_width = 64,
			.max_width = 4096,
			.step_width = 64,
			.min_height = 64,
			.max_height = 2304,
			.step_height = 16,
		},
		.ctrls = &rkvdec_hevc_ctrls,
		.ops = &rkvdec_hevc_fmt_ops,
		.num_decoded_fmts = ARRAY_SIZE(rkvdec_hevc_decoded_fmts),
		.decoded_fmts = rkvdec_hevc_decoded_fmts,
	}
};

static const struct rkvdec_coded_fmt_desc vdpu381_coded_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_HEVC_SLICE,
		.frmsize = {
			.min_width = 64,
			.max_width = 65472,
			.step_width = 64,
			.min_height = 64,
			.max_height = 65472,
			.step_height = 16,
		},
		.ctrls = &vdpu38x_hevc_ctrls,
		.ops = &rkvdec_vdpu381_hevc_fmt_ops,
		.num_decoded_fmts = ARRAY_SIZE(rkvdec_hevc_decoded_fmts),
		.decoded_fmts = rkvdec_hevc_decoded_fmts,
		.subsystem_flags = VB2_V4L2_FL_SUPPORTS_M2M_HOLD_CAPTURE_BUF,
	},
	{
		.fourcc = V4L2_PIX_FMT_H264_SLICE,
		.frmsize = {
			.min_width = 64,
			.max_width =  65520,
			.step_width = 64,
			.min_height = 64,
			.max_height =  65520,
			.step_height = 16,
		},
		.ctrls = &vdpu38x_h264_ctrls,
		.ops = &rkvdec_vdpu381_h264_fmt_ops,
		.num_decoded_fmts = ARRAY_SIZE(rkvdec_h264_decoded_fmts),
		.decoded_fmts = rkvdec_h264_decoded_fmts,
		.subsystem_flags = VB2_V4L2_FL_SUPPORTS_M2M_HOLD_CAPTURE_BUF,
	},
};

static const struct rkvdec_coded_fmt_desc vdpu383_coded_fmts[] = {
	{
		.fourcc = V4L2_PIX_FMT_HEVC_SLICE,
		.frmsize = {
			.min_width = 64,
			.max_width = 65472,
			.step_width = 64,
			.min_height = 64,
			.max_height = 65472,
			.step_height = 16,
		},
		.ctrls = &vdpu38x_hevc_ctrls,
		.ops = &rkvdec_vdpu383_hevc_fmt_ops,
		.num_decoded_fmts = ARRAY_SIZE(rkvdec_hevc_decoded_fmts),
		.decoded_fmts = rkvdec_hevc_decoded_fmts,
		.subsystem_flags = VB2_V4L2_FL_SUPPORTS_M2M_HOLD_CAPTURE_BUF,
	},
	{
		.fourcc = V4L2_PIX_FMT_H264_SLICE,
		.frmsize = {
			.min_width = 64,
			.max_width =  65520,
			.step_width = 64,
			.min_height = 64,
			.max_height =  65520,
			.step_height = 16,
		},
		.ctrls = &vdpu38x_h264_ctrls,
		.ops = &rkvdec_vdpu383_h264_fmt_ops,
		.num_decoded_fmts = ARRAY_SIZE(rkvdec_h264_decoded_fmts),
		.decoded_fmts = rkvdec_h264_decoded_fmts,
		.subsystem_flags = VB2_V4L2_FL_SUPPORTS_M2M_HOLD_CAPTURE_BUF,
	},
};

static const struct rkvdec_coded_fmt_desc *
rkvdec_enum_coded_fmt_desc(struct rkvdec_ctx *ctx, int index)
{
	const struct rkvdec_variant *variant = ctx->dev->variant;
	int fmt_idx = -1;
	unsigned int i;

	for (i = 0; i < variant->num_coded_fmts; i++) {
		fmt_idx++;
		if (index == fmt_idx)
			return &variant->coded_fmts[i];
	}

	return NULL;
}

static const struct rkvdec_coded_fmt_desc *
rkvdec_find_coded_fmt_desc(struct rkvdec_ctx *ctx, u32 fourcc)
{
	const struct rkvdec_variant *variant = ctx->dev->variant;
	unsigned int i;

	for (i = 0; i < variant->num_coded_fmts; i++) {
		if (variant->coded_fmts[i].fourcc == fourcc)
			return &variant->coded_fmts[i];
	}

	return NULL;
}

static void rkvdec_reset_coded_fmt(struct rkvdec_ctx *ctx)
{
	struct v4l2_format *f = &ctx->coded_fmt;

	ctx->coded_fmt_desc = rkvdec_enum_coded_fmt_desc(ctx, 0);
	rkvdec_reset_fmt(ctx, f, ctx->coded_fmt_desc->fourcc);

	f->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	f->fmt.pix_mp.width = ctx->coded_fmt_desc->frmsize.min_width;
	f->fmt.pix_mp.height = ctx->coded_fmt_desc->frmsize.min_height;

	if (ctx->coded_fmt_desc->ops->adjust_fmt)
		ctx->coded_fmt_desc->ops->adjust_fmt(ctx, f);
}

static int rkvdec_enum_framesizes(struct file *file, void *priv,
				  struct v4l2_frmsizeenum *fsize)
{
	struct rkvdec_ctx *ctx = file_to_rkvdec_ctx(file);
	const struct rkvdec_coded_fmt_desc *desc;

	if (fsize->index != 0)
		return -EINVAL;

	desc = rkvdec_find_coded_fmt_desc(ctx, fsize->pixel_format);
	if (!desc)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width = 1;
	fsize->stepwise.max_width = desc->frmsize.max_width;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.min_height = 1;
	fsize->stepwise.max_height = desc->frmsize.max_height;
	fsize->stepwise.step_height = 1;

	return 0;
}

static int rkvdec_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct rkvdec_dev *rkvdec = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);

	strscpy(cap->driver, rkvdec->dev->driver->name,
		sizeof(cap->driver));
	strscpy(cap->card, vdev->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 rkvdec->dev->driver->name);
	return 0;
}

static int rkvdec_try_capture_fmt(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct rkvdec_ctx *ctx = file_to_rkvdec_ctx(file);
	const struct rkvdec_coded_fmt_desc *coded_desc;

	/*
	 * The codec context should point to a coded format desc, if the format
	 * on the coded end has not been set yet, it should point to the
	 * default value.
	 */
	coded_desc = ctx->coded_fmt_desc;
	if (WARN_ON(!coded_desc))
		return -EINVAL;

	if (!rkvdec_is_valid_fmt(ctx, pix_mp->pixelformat, ctx->image_fmt))
		pix_mp->pixelformat = rkvdec_enum_decoded_fmt(ctx, 0,
							      ctx->image_fmt);

	/* Always apply the frmsize constraint of the coded end. */
	pix_mp->width = max(pix_mp->width, ctx->coded_fmt.fmt.pix_mp.width);
	pix_mp->height = max(pix_mp->height, ctx->coded_fmt.fmt.pix_mp.height);
	v4l2_apply_frmsize_constraints(&pix_mp->width,
				       &pix_mp->height,
				       &coded_desc->frmsize);

	rkvdec_fill_decoded_pixfmt(ctx, pix_mp);
	pix_mp->field = V4L2_FIELD_NONE;

	return 0;
}

static int rkvdec_try_output_fmt(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct rkvdec_ctx *ctx = file_to_rkvdec_ctx(file);
	const struct rkvdec_coded_fmt_desc *desc;

	desc = rkvdec_find_coded_fmt_desc(ctx, pix_mp->pixelformat);
	if (!desc) {
		desc = rkvdec_enum_coded_fmt_desc(ctx, 0);
		pix_mp->pixelformat = desc->fourcc;
	}

	v4l2_apply_frmsize_constraints(&pix_mp->width,
				       &pix_mp->height,
				       &desc->frmsize);

	pix_mp->field = V4L2_FIELD_NONE;
	/* All coded formats are considered single planar for now. */
	pix_mp->num_planes = 1;

	if (desc->ops->adjust_fmt) {
		int ret;

		ret = desc->ops->adjust_fmt(ctx, f);
		if (ret)
			return ret;
	}

	return 0;
}

static int rkvdec_s_capture_fmt(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct rkvdec_ctx *ctx = file_to_rkvdec_ctx(file);
	struct vb2_queue *vq;
	int ret;

	/* Change not allowed if queue is busy */
	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
			     V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (vb2_is_busy(vq))
		return -EBUSY;

	ret = rkvdec_try_capture_fmt(file, priv, f);
	if (ret)
		return ret;

	ctx->decoded_fmt = *f;
	return 0;
}

static int rkvdec_s_output_fmt(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct rkvdec_ctx *ctx = file_to_rkvdec_ctx(file);
	struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
	const struct rkvdec_coded_fmt_desc *desc;
	struct v4l2_format *cap_fmt;
	struct vb2_queue *peer_vq, *vq;
	int ret;

	/*
	 * In order to support dynamic resolution change, the decoder admits
	 * a resolution change, as long as the pixelformat remains. Can't be
	 * done if streaming.
	 */
	vq = v4l2_m2m_get_vq(m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (vb2_is_streaming(vq) ||
	    (vb2_is_busy(vq) &&
	     f->fmt.pix_mp.pixelformat != ctx->coded_fmt.fmt.pix_mp.pixelformat))
		return -EBUSY;

	/*
	 * Since format change on the OUTPUT queue will reset the CAPTURE
	 * queue, we can't allow doing so when the CAPTURE queue has buffers
	 * allocated.
	 */
	peer_vq = v4l2_m2m_get_vq(m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (vb2_is_busy(peer_vq))
		return -EBUSY;

	ret = rkvdec_try_output_fmt(file, priv, f);
	if (ret)
		return ret;

	desc = rkvdec_find_coded_fmt_desc(ctx, f->fmt.pix_mp.pixelformat);
	if (!desc)
		return -EINVAL;
	ctx->coded_fmt_desc = desc;
	ctx->coded_fmt = *f;

	/*
	 * Current decoded format might have become invalid with newly
	 * selected codec, so reset it to default just to be safe and
	 * keep internal driver state sane. User is mandated to set
	 * the decoded format again after we return, so we don't need
	 * anything smarter.
	 *
	 * Note that this will propagates any size changes to the decoded format.
	 */
	ctx->image_fmt = RKVDEC_IMG_FMT_ANY;
	rkvdec_reset_decoded_fmt(ctx);

	/* Propagate colorspace information to capture. */
	cap_fmt = &ctx->decoded_fmt;
	cap_fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
	cap_fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
	cap_fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	cap_fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;

	/* Enable format specific queue features */
	vq->subsystem_flags |= desc->subsystem_flags;

	return 0;
}

static int rkvdec_g_output_fmt(struct file *file, void *priv,
			       struct v4l2_format *f)
{
	struct rkvdec_ctx *ctx = file_to_rkvdec_ctx(file);

	*f = ctx->coded_fmt;
	return 0;
}

static int rkvdec_g_capture_fmt(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct rkvdec_ctx *ctx = file_to_rkvdec_ctx(file);

	*f = ctx->decoded_fmt;
	return 0;
}

static int rkvdec_enum_output_fmt(struct file *file, void *priv,
				  struct v4l2_fmtdesc *f)
{
	struct rkvdec_ctx *ctx = file_to_rkvdec_ctx(file);
	const struct rkvdec_coded_fmt_desc *desc;

	desc = rkvdec_enum_coded_fmt_desc(ctx, f->index);
	if (!desc)
		return -EINVAL;

	f->pixelformat = desc->fourcc;
	return 0;
}

static int rkvdec_enum_capture_fmt(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	struct rkvdec_ctx *ctx = file_to_rkvdec_ctx(file);
	u32 fourcc;

	fourcc = rkvdec_enum_decoded_fmt(ctx, f->index, ctx->image_fmt);
	if (!fourcc)
		return -EINVAL;

	f->pixelformat = fourcc;
	return 0;
}

static const struct v4l2_ioctl_ops rkvdec_ioctl_ops = {
	.vidioc_querycap = rkvdec_querycap,
	.vidioc_enum_framesizes = rkvdec_enum_framesizes,

	.vidioc_try_fmt_vid_cap_mplane = rkvdec_try_capture_fmt,
	.vidioc_try_fmt_vid_out_mplane = rkvdec_try_output_fmt,
	.vidioc_s_fmt_vid_out_mplane = rkvdec_s_output_fmt,
	.vidioc_s_fmt_vid_cap_mplane = rkvdec_s_capture_fmt,
	.vidioc_g_fmt_vid_out_mplane = rkvdec_g_output_fmt,
	.vidioc_g_fmt_vid_cap_mplane = rkvdec_g_capture_fmt,
	.vidioc_enum_fmt_vid_out = rkvdec_enum_output_fmt,
	.vidioc_enum_fmt_vid_cap = rkvdec_enum_capture_fmt,

	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,

	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,

	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,

	.vidioc_decoder_cmd = v4l2_m2m_ioctl_stateless_decoder_cmd,
	.vidioc_try_decoder_cmd = v4l2_m2m_ioctl_stateless_try_decoder_cmd,
};

static int rkvdec_queue_setup(struct vb2_queue *vq, unsigned int *num_buffers,
			      unsigned int *num_planes, unsigned int sizes[],
			      struct device *alloc_devs[])
{
	struct rkvdec_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_format *f;
	unsigned int i;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		f = &ctx->coded_fmt;
	else
		f = &ctx->decoded_fmt;

	if (*num_planes) {
		if (*num_planes != f->fmt.pix_mp.num_planes)
			return -EINVAL;

		for (i = 0; i < f->fmt.pix_mp.num_planes; i++) {
			if (sizes[i] < f->fmt.pix_mp.plane_fmt[i].sizeimage)
				return -EINVAL;
		}
	} else {
		*num_planes = f->fmt.pix_mp.num_planes;
		for (i = 0; i < f->fmt.pix_mp.num_planes; i++)
			sizes[i] = f->fmt.pix_mp.plane_fmt[i].sizeimage;
	}

	return 0;
}

static int rkvdec_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct rkvdec_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_format *f;
	unsigned int i;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		f = &ctx->coded_fmt;
	else
		f = &ctx->decoded_fmt;

	for (i = 0; i < f->fmt.pix_mp.num_planes; ++i) {
		u32 sizeimage = f->fmt.pix_mp.plane_fmt[i].sizeimage;

		if (vb2_plane_size(vb, i) < sizeimage)
			return -EINVAL;
	}

	/*
	 * Buffer's bytesused must be written by driver for CAPTURE buffers.
	 * (for OUTPUT buffers, if userspace passes 0 bytesused, v4l2-core sets
	 * it to buffer length).
	 */
	if (V4L2_TYPE_IS_CAPTURE(vq->type))
		vb2_set_plane_payload(vb, 0, f->fmt.pix_mp.plane_fmt[0].sizeimage);

	return 0;
}

static void rkvdec_buf_queue(struct vb2_buffer *vb)
{
	struct rkvdec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int rkvdec_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;
	return 0;
}

static void rkvdec_buf_request_complete(struct vb2_buffer *vb)
{
	struct rkvdec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &ctx->ctrl_hdl);
}

static int rkvdec_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct rkvdec_ctx *ctx = vb2_get_drv_priv(q);
	const struct rkvdec_coded_fmt_desc *desc;
	const struct rkvdec_variant *variant = ctx->dev->variant;
	int ret;

	if (V4L2_TYPE_IS_CAPTURE(q->type))
		return 0;

	desc = ctx->coded_fmt_desc;
	if (WARN_ON(!desc))
		return -EINVAL;

	ret = rkvdec_allocate_rcb(ctx, variant->rcb_sizes, variant->num_rcb_sizes);
	if (ret)
		return ret;

	if (desc->ops->start) {
		ret = desc->ops->start(ctx);
		if (ret)
			goto err_ops_start;
	}

	return 0;

err_ops_start:
	rkvdec_free_rcb(ctx);

	return ret;
}

static void rkvdec_queue_cleanup(struct vb2_queue *vq, u32 state)
{
	struct rkvdec_ctx *ctx = vb2_get_drv_priv(vq);

	while (true) {
		struct vb2_v4l2_buffer *vbuf;

		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		if (!vbuf)
			break;

		v4l2_ctrl_request_complete(vbuf->vb2_buf.req_obj.req,
					   &ctx->ctrl_hdl);
		v4l2_m2m_buf_done(vbuf, state);
	}
}

static void rkvdec_stop_streaming(struct vb2_queue *q)
{
	struct rkvdec_ctx *ctx = vb2_get_drv_priv(q);

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		const struct rkvdec_coded_fmt_desc *desc = ctx->coded_fmt_desc;

		if (WARN_ON(!desc))
			return;

		if (desc->ops->stop)
			desc->ops->stop(ctx);

		rkvdec_free_rcb(ctx);
	}

	rkvdec_queue_cleanup(q, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops rkvdec_queue_ops = {
	.queue_setup = rkvdec_queue_setup,
	.buf_prepare = rkvdec_buf_prepare,
	.buf_queue = rkvdec_buf_queue,
	.buf_out_validate = rkvdec_buf_out_validate,
	.buf_request_complete = rkvdec_buf_request_complete,
	.start_streaming = rkvdec_start_streaming,
	.stop_streaming = rkvdec_stop_streaming,
};

static int rkvdec_request_validate(struct media_request *req)
{
	unsigned int count;

	count = vb2_request_buffer_cnt(req);
	if (!count)
		return -ENOENT;
	else if (count > 1)
		return -EINVAL;

	return vb2_request_validate(req);
}

static const struct media_device_ops rkvdec_media_ops = {
	.req_validate = rkvdec_request_validate,
	.req_queue = v4l2_m2m_request_queue,
};

static void rkvdec_job_finish_no_pm(struct rkvdec_ctx *ctx,
				    enum vb2_buffer_state result)
{
	if (ctx->coded_fmt_desc->ops->done) {
		struct vb2_v4l2_buffer *src_buf, *dst_buf;

		src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
		dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
		ctx->coded_fmt_desc->ops->done(ctx, src_buf, dst_buf, result);
	}

	v4l2_m2m_buf_done_and_job_finish(ctx->dev->m2m_dev, ctx->fh.m2m_ctx,
					 result);
}

static void rkvdec_job_finish(struct rkvdec_ctx *ctx,
			      enum vb2_buffer_state result)
{
	struct rkvdec_dev *rkvdec = ctx->dev;

	pm_runtime_put_autosuspend(rkvdec->dev);
	rkvdec_job_finish_no_pm(ctx, result);
}

void rkvdec_run_preamble(struct rkvdec_ctx *ctx, struct rkvdec_run *run)
{
	struct media_request *src_req;

	memset(run, 0, sizeof(*run));

	run->bufs.src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	run->bufs.dst = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	/* Apply request(s) controls if needed. */
	src_req = run->bufs.src->vb2_buf.req_obj.req;
	if (src_req)
		v4l2_ctrl_request_setup(src_req, &ctx->ctrl_hdl);

	v4l2_m2m_buf_copy_metadata(run->bufs.src, run->bufs.dst);
}

void rkvdec_run_postamble(struct rkvdec_ctx *ctx, struct rkvdec_run *run)
{
	struct media_request *src_req = run->bufs.src->vb2_buf.req_obj.req;

	if (src_req)
		v4l2_ctrl_request_complete(src_req, &ctx->ctrl_hdl);
}

void rkvdec_quirks_disable_qos(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	u32 reg;

	/* Set undocumented swreg_block_gating_e field */
	reg = readl(rkvdec->regs + RKVDEC_REG_QOS_CTRL);
	reg &= GENMASK(31, 16);
	reg |= 0xEFFF;
	writel(reg, rkvdec->regs + RKVDEC_REG_QOS_CTRL);
}

void rkvdec_memcpy_toio(void __iomem *dst, void *src, size_t len)
{
#ifdef CONFIG_ARM64
	__iowrite32_copy(dst, src, len / 4);
#else
	memcpy_toio(dst, src, len);
#endif
}

void rkvdec_schedule_watchdog(struct rkvdec_dev *rkvdec, u32 timeout_threshold)
{
	/* Set watchdog at 2 times the hardware timeout threshold */
	u32 watchdog_time;
	unsigned long axi_rate = clk_get_rate(rkvdec->axi_clk);

	if (axi_rate)
		watchdog_time = 2 * div_u64(1000 * (u64)timeout_threshold, axi_rate);
	else
		watchdog_time = 2000;

	schedule_delayed_work(&rkvdec->watchdog_work, msecs_to_jiffies(watchdog_time));
}

static void rkvdec_device_run(void *priv)
{
	struct rkvdec_ctx *ctx = priv;
	struct rkvdec_dev *rkvdec = ctx->dev;
	const struct rkvdec_coded_fmt_desc *desc = ctx->coded_fmt_desc;
	int ret;

	if (WARN_ON(!desc))
		return;

	ret = pm_runtime_resume_and_get(rkvdec->dev);
	if (ret < 0) {
		rkvdec_job_finish_no_pm(ctx, VB2_BUF_STATE_ERROR);
		return;
	}

	ret = desc->ops->run(ctx);
	if (ret)
		rkvdec_job_finish(ctx, VB2_BUF_STATE_ERROR);
}

static const struct v4l2_m2m_ops rkvdec_m2m_ops = {
	.device_run = rkvdec_device_run,
};

static int rkvdec_queue_init(void *priv,
			     struct vb2_queue *src_vq,
			     struct vb2_queue *dst_vq)
{
	struct rkvdec_ctx *ctx = priv;
	struct rkvdec_dev *rkvdec = ctx->dev;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->ops = &rkvdec_queue_ops;
	src_vq->mem_ops = &vb2_dma_contig_memops;

	/*
	 * Driver does mostly sequential access, so sacrifice TLB efficiency
	 * for faster allocation. Also, no CPU access on the source queue,
	 * so no kernel mapping needed.
	 */
	src_vq->dma_attrs = DMA_ATTR_ALLOC_SINGLE_PAGES |
			    DMA_ATTR_NO_KERNEL_MAPPING;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &rkvdec->vdev_lock;
	src_vq->dev = rkvdec->v4l2_dev.dev;
	src_vq->supports_requests = true;
	src_vq->requires_requests = true;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->bidirectional = true;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->dma_attrs = DMA_ATTR_ALLOC_SINGLE_PAGES |
			    DMA_ATTR_NO_KERNEL_MAPPING;
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &rkvdec_queue_ops;
	dst_vq->buf_struct_size = sizeof(struct rkvdec_decoded_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &rkvdec->vdev_lock;
	dst_vq->dev = rkvdec->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

static int rkvdec_add_ctrls(struct rkvdec_ctx *ctx,
			    const struct rkvdec_ctrls *ctrls)
{
	unsigned int i;

	for (i = 0; i < ctrls->num_ctrls; i++) {
		const struct v4l2_ctrl_config *cfg = &ctrls->ctrls[i].cfg;

		v4l2_ctrl_new_custom(&ctx->ctrl_hdl, cfg, ctx);
		if (ctx->ctrl_hdl.error)
			return ctx->ctrl_hdl.error;
	}

	return 0;
}

static int rkvdec_init_ctrls(struct rkvdec_ctx *ctx)
{
	const struct rkvdec_variant *variant = ctx->dev->variant;
	unsigned int i, nctrls = 0;
	int ret;

	for (i = 0; i < variant->num_coded_fmts; i++)
		nctrls += variant->coded_fmts[i].ctrls->num_ctrls;

	v4l2_ctrl_handler_init(&ctx->ctrl_hdl, nctrls);

	for (i = 0; i < variant->num_coded_fmts; i++) {
		ret = rkvdec_add_ctrls(ctx, variant->coded_fmts[i].ctrls);
		if (ret)
			goto err_free_handler;
	}

	ret = v4l2_ctrl_handler_setup(&ctx->ctrl_hdl);
	if (ret)
		goto err_free_handler;

	ctx->fh.ctrl_handler = &ctx->ctrl_hdl;
	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(&ctx->ctrl_hdl);
	return ret;
}

static int rkvdec_open(struct file *filp)
{
	struct rkvdec_dev *rkvdec = video_drvdata(filp);
	struct rkvdec_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = rkvdec;
	rkvdec_reset_coded_fmt(ctx);
	rkvdec_reset_decoded_fmt(ctx);
	v4l2_fh_init(&ctx->fh, video_devdata(filp));

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(rkvdec->m2m_dev, ctx,
					    rkvdec_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto err_free_ctx;
	}

	ret = rkvdec_init_ctrls(ctx);
	if (ret)
		goto err_cleanup_m2m_ctx;

	v4l2_fh_add(&ctx->fh, filp);

	return 0;

err_cleanup_m2m_ctx:
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);

err_free_ctx:
	kfree(ctx);
	return ret;
}

static int rkvdec_release(struct file *filp)
{
	struct rkvdec_ctx *ctx = file_to_rkvdec_ctx(filp);

	v4l2_fh_del(&ctx->fh, filp);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_ctrl_handler_free(&ctx->ctrl_hdl);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);

	return 0;
}

static const struct v4l2_file_operations rkvdec_fops = {
	.owner = THIS_MODULE,
	.open = rkvdec_open,
	.release = rkvdec_release,
	.poll = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = v4l2_m2m_fop_mmap,
};

static int rkvdec_v4l2_init(struct rkvdec_dev *rkvdec)
{
	int ret;

	ret = v4l2_device_register(rkvdec->dev, &rkvdec->v4l2_dev);
	if (ret) {
		dev_err(rkvdec->dev, "Failed to register V4L2 device\n");
		return ret;
	}

	rkvdec->m2m_dev = v4l2_m2m_init(&rkvdec_m2m_ops);
	if (IS_ERR(rkvdec->m2m_dev)) {
		v4l2_err(&rkvdec->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(rkvdec->m2m_dev);
		goto err_unregister_v4l2;
	}

	rkvdec->mdev.dev = rkvdec->dev;
	strscpy(rkvdec->mdev.model, "rkvdec", sizeof(rkvdec->mdev.model));
	strscpy(rkvdec->mdev.bus_info, "platform:rkvdec",
		sizeof(rkvdec->mdev.bus_info));
	media_device_init(&rkvdec->mdev);
	rkvdec->mdev.ops = &rkvdec_media_ops;
	rkvdec->v4l2_dev.mdev = &rkvdec->mdev;

	rkvdec->vdev.lock = &rkvdec->vdev_lock;
	rkvdec->vdev.v4l2_dev = &rkvdec->v4l2_dev;
	rkvdec->vdev.fops = &rkvdec_fops;
	rkvdec->vdev.release = video_device_release_empty;
	rkvdec->vdev.vfl_dir = VFL_DIR_M2M;
	rkvdec->vdev.device_caps = V4L2_CAP_STREAMING |
				   V4L2_CAP_VIDEO_M2M_MPLANE;
	rkvdec->vdev.ioctl_ops = &rkvdec_ioctl_ops;
	video_set_drvdata(&rkvdec->vdev, rkvdec);
	strscpy(rkvdec->vdev.name, "rkvdec", sizeof(rkvdec->vdev.name));

	ret = video_register_device(&rkvdec->vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(&rkvdec->v4l2_dev, "Failed to register video device\n");
		goto err_cleanup_mc;
	}

	ret = v4l2_m2m_register_media_controller(rkvdec->m2m_dev, &rkvdec->vdev,
						 MEDIA_ENT_F_PROC_VIDEO_DECODER);
	if (ret) {
		v4l2_err(&rkvdec->v4l2_dev,
			 "Failed to initialize V4L2 M2M media controller\n");
		goto err_unregister_vdev;
	}

	ret = media_device_register(&rkvdec->mdev);
	if (ret) {
		v4l2_err(&rkvdec->v4l2_dev, "Failed to register media device\n");
		goto err_unregister_mc;
	}

	return 0;

err_unregister_mc:
	v4l2_m2m_unregister_media_controller(rkvdec->m2m_dev);

err_unregister_vdev:
	video_unregister_device(&rkvdec->vdev);

err_cleanup_mc:
	media_device_cleanup(&rkvdec->mdev);
	v4l2_m2m_release(rkvdec->m2m_dev);

err_unregister_v4l2:
	v4l2_device_unregister(&rkvdec->v4l2_dev);
	return ret;
}

static void rkvdec_v4l2_cleanup(struct rkvdec_dev *rkvdec)
{
	media_device_unregister(&rkvdec->mdev);
	v4l2_m2m_unregister_media_controller(rkvdec->m2m_dev);
	video_unregister_device(&rkvdec->vdev);
	media_device_cleanup(&rkvdec->mdev);
	v4l2_m2m_release(rkvdec->m2m_dev);
	v4l2_device_unregister(&rkvdec->v4l2_dev);
}

static void rkvdec_iommu_restore(struct rkvdec_dev *rkvdec)
{
	if (rkvdec->empty_domain) {
		/*
		 * To rewrite mapping into the attached IOMMU core, attach a new empty domain that
		 * will program an empty table, then detach it to restore the default domain and
		 * all cached mappings.
		 * This is safely done in this interrupt handler to make sure no memory get mapped
		 * through the IOMMU while the empty domain is attached.
		 */
		iommu_attach_device(rkvdec->empty_domain, rkvdec->dev);
		iommu_detach_device(rkvdec->empty_domain, rkvdec->dev);
	}
}

static irqreturn_t rk3399_irq_handler(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	enum vb2_buffer_state state;
	u32 status;

	status = readl(rkvdec->regs + RKVDEC_REG_INTERRUPT);
	writel(0, rkvdec->regs + RKVDEC_REG_INTERRUPT);

	if (status & RKVDEC_RDY_STA) {
		state = VB2_BUF_STATE_DONE;
	} else {
		state = VB2_BUF_STATE_ERROR;
		if (status & RKVDEC_SOFTRESET_RDY)
			rkvdec_iommu_restore(rkvdec);
	}

	if (cancel_delayed_work(&rkvdec->watchdog_work))
		rkvdec_job_finish(ctx, state);

	return IRQ_HANDLED;
}

static irqreturn_t vdpu381_irq_handler(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	enum vb2_buffer_state state;
	bool need_reset = 0;
	u32 status;

	status = readl(rkvdec->regs + VDPU381_REG_STA_INT);
	writel(0, rkvdec->regs + VDPU381_REG_STA_INT);

	if (status & VDPU381_STA_INT_DEC_RDY_STA) {
		state = VB2_BUF_STATE_DONE;
	} else {
		state = VB2_BUF_STATE_ERROR;
		if (status & (VDPU381_STA_INT_SOFTRESET_RDY |
			      VDPU381_STA_INT_TIMEOUT |
			      VDPU381_STA_INT_ERROR))
			rkvdec_iommu_restore(rkvdec);
	}

	if (need_reset)
		rkvdec_iommu_restore(rkvdec);

	if (cancel_delayed_work(&rkvdec->watchdog_work))
		rkvdec_job_finish(ctx, state);

	return IRQ_HANDLED;
}

static irqreturn_t vdpu383_irq_handler(struct rkvdec_ctx *ctx)
{
	struct rkvdec_dev *rkvdec = ctx->dev;
	enum vb2_buffer_state state;
	bool need_reset = 0;
	u32 status;

	status = readl(rkvdec->link + VDPU383_LINK_STA_INT);
	writel(FIELD_PREP_WM16(VDPU383_STA_INT_ALL, 0), rkvdec->link + VDPU383_LINK_STA_INT);
	/* On vdpu383, the interrupts must be disabled */
	writel(FIELD_PREP_WM16(VDPU383_INT_EN_IRQ | VDPU383_INT_EN_LINE_IRQ, 0),
	       rkvdec->link + VDPU383_LINK_INT_EN);

	if (status & VDPU383_STA_INT_DEC_RDY_STA) {
		state = VB2_BUF_STATE_DONE;
	} else {
		state = VB2_BUF_STATE_ERROR;
		rkvdec_iommu_restore(rkvdec);
	}

	if (need_reset)
		rkvdec_iommu_restore(rkvdec);

	if (cancel_delayed_work(&rkvdec->watchdog_work))
		rkvdec_job_finish(ctx, state);

	return IRQ_HANDLED;
}

static irqreturn_t rkvdec_irq_handler(int irq, void *priv)
{
	struct rkvdec_dev *rkvdec = priv;
	struct rkvdec_ctx *ctx = v4l2_m2m_get_curr_priv(rkvdec->m2m_dev);
	const struct rkvdec_variant *variant = rkvdec->variant;

	return variant->ops->irq_handler(ctx);
}

/*
 * Flip one or more matrices along their main diagonal and flatten them
 * before writing it to the memory.
 * Convert:
 * ABCD         AEIM
 * EFGH     =>  BFJN     =>     AEIMBFJNCGKODHLP
 * IJKL         CGKO
 * MNOP         DHLP
 */
static void transpose_and_flatten_matrices(u8 *output, const u8 *input,
					   int matrices, int row_length)
{
	int i, j, row, x_offset, matrix_offset, rot_index, y_offset, matrix_size, new_value;

	matrix_size = row_length * row_length;
	for (i = 0; i < matrices; i++) {
		row = 0;
		x_offset = 0;
		matrix_offset = i * matrix_size;
		for (j = 0; j < matrix_size; j++) {
			y_offset = j - (row * row_length);
			rot_index = y_offset * row_length + x_offset;
			new_value = *(input + i * matrix_size + j);
			output[matrix_offset + rot_index] = new_value;
			if ((j + 1) % row_length == 0) {
				row += 1;
				x_offset += 1;
			}
		}
	}
}

/*
 * VDPU383 needs a specific order:
 * The 8x8 flatten matrix is based on 4x4 blocks.
 * Each 4x4 block is written separately in order.
 *
 * Base data    =>  Transposed    VDPU383 transposed
 *
 * ABCDEFGH         AIQYaiqy      AIQYBJRZ
 * IJKLMNOP         BJRZbjrz      CKS0DLT1
 * QRSTUVWX         CKS0cks6      aiqybjrz
 * YZ012345     =>  DLT1dlt7      cks6dlt7
 * abcdefgh         EMU2emu8      EMU2FNV3
 * ijklmnop         FNV3fnv9      GOW4HPX5
 * qrstuvwx         GOW4gow#      emu8fnv9
 * yz6789#$         HPX5hpx$      gow#hpx$
 *
 * As the function reads block of 4x4 it can be used for both 4x4 and 8x8 matrices.
 *
 */
static void vdpu383_flatten_matrices(u8 *output, const u8 *input, int matrices, int row_length)
{
	u8 block;
	int i, j, matrix_offset, matrix_size, new_value, input_idx, line_offset, block_offset;

	matrix_size = row_length * row_length;
	for (i = 0; i < matrices; i++) {
		matrix_offset = i * matrix_size;
		for (j = 0; j < matrix_size; j++) {
			block = j / 16;
			line_offset = (j % 16) / 4;
			block_offset = (block & 1) * 32 + (block & 2) * 2;
			input_idx = ((j % 4) * row_length) + line_offset + block_offset;

			new_value = *(input + i * matrix_size + input_idx);

			output[matrix_offset + j] = new_value;
		}
	}
}

static void rkvdec_watchdog_func(struct work_struct *work)
{
	struct rkvdec_dev *rkvdec;
	struct rkvdec_ctx *ctx;

	rkvdec = container_of(to_delayed_work(work), struct rkvdec_dev,
			      watchdog_work);
	ctx = v4l2_m2m_get_curr_priv(rkvdec->m2m_dev);
	if (ctx) {
		dev_err(rkvdec->dev, "Frame processing timed out!\n");
		writel(RKVDEC_IRQ_DIS, rkvdec->regs + RKVDEC_REG_INTERRUPT);
		rkvdec_job_finish(ctx, VB2_BUF_STATE_ERROR);
	}
}

/*
 * Some SoCs, like RK3588 have multiple identical VDPU cores, but the
 * kernel is currently missing support for multi-core handling. Exposing
 * separate devices for each core to userspace is bad, since that does
 * not allow scheduling tasks properly (and creates ABI). With this workaround
 * the driver will only probe for the first core and early exit for the other
 * cores. Once the driver gains multi-core support, the same technique
 * for detecting the first core can be used to cluster all cores together.
 */
static int rkvdec_disable_multicore(struct rkvdec_dev *rkvdec)
{
	struct device_node *node = NULL;
	const char *compatible;
	bool is_first_core;
	int ret;

	/* Intentionally ignores the fallback strings */
	ret = of_property_read_string(rkvdec->dev->of_node, "compatible", &compatible);
	if (ret)
		return ret;

	/* The first compatible and available node found is considered the main core */
	do {
		node = of_find_compatible_node(node, NULL, compatible);
		if (of_device_is_available(node))
			break;
	} while (node);

	if (!node)
		return -EINVAL;

	is_first_core = (rkvdec->dev->of_node == node);

	of_node_put(node);

	if (!is_first_core) {
		dev_info(rkvdec->dev, "missing multi-core support, ignoring this instance\n");
		return -ENODEV;
	}

	return 0;
}

static const struct rkvdec_variant_ops rk3399_variant_ops = {
	.irq_handler = rk3399_irq_handler,
	.colmv_size = rkvdec_colmv_size,
	.flatten_matrices = transpose_and_flatten_matrices,
};

static const struct rkvdec_variant rk3288_rkvdec_variant = {
	.num_regs = 68,
	.coded_fmts = rk3288_coded_fmts,
	.num_coded_fmts = ARRAY_SIZE(rk3288_coded_fmts),
	.ops = &rk3399_variant_ops,
	.has_single_reg_region = true,
};

static const struct rkvdec_variant rk3328_rkvdec_variant = {
	.num_regs = 109,
	.coded_fmts = rkvdec_coded_fmts,
	.num_coded_fmts = ARRAY_SIZE(rkvdec_coded_fmts),
	.ops = &rk3399_variant_ops,
	.has_single_reg_region = true,
	.quirks = RKVDEC_QUIRK_DISABLE_QOS,
};

static const struct rkvdec_variant rk3399_rkvdec_variant = {
	.num_regs = 78,
	.coded_fmts = rkvdec_coded_fmts,
	.num_coded_fmts = ARRAY_SIZE(rkvdec_coded_fmts),
	.ops = &rk3399_variant_ops,
	.has_single_reg_region = true,
};

static const struct rcb_size_info vdpu381_rcb_sizes[] = {
	{6,	PIC_WIDTH},	// intrar
	{1,	PIC_WIDTH},	// transdr (Is actually 0.4*pic_width)
	{1,	PIC_HEIGHT},	// transdc (Is actually 0.1*pic_height)
	{3,	PIC_WIDTH},	// streamdr
	{6,	PIC_WIDTH},	// interr
	{3,	PIC_HEIGHT},	// interc
	{22,	PIC_WIDTH},	// dblkr
	{6,	PIC_WIDTH},	// saor
	{11,	PIC_WIDTH},	// fbcr
	{67,	PIC_HEIGHT},	// filtc col
};

static const struct rkvdec_variant_ops vdpu381_variant_ops = {
	.irq_handler = vdpu381_irq_handler,
	.colmv_size = rkvdec_colmv_size,
	.flatten_matrices = transpose_and_flatten_matrices,
};

static const struct rkvdec_variant vdpu381_variant = {
	.coded_fmts = vdpu381_coded_fmts,
	.num_coded_fmts = ARRAY_SIZE(vdpu381_coded_fmts),
	.rcb_sizes = vdpu381_rcb_sizes,
	.num_rcb_sizes = ARRAY_SIZE(vdpu381_rcb_sizes),
	.ops = &vdpu381_variant_ops,
};

static const struct rcb_size_info vdpu383_rcb_sizes[] = {
	{6,	PIC_WIDTH},	// streamd
	{6,	PIC_WIDTH},	// streamd_tile
	{12,	PIC_WIDTH},	// inter
	{12,	PIC_WIDTH},	// inter_tile
	{16,	PIC_WIDTH},	// intra
	{10,	PIC_WIDTH},	// intra_tile
	{120,	PIC_WIDTH},	// filterd
	{120,	PIC_WIDTH},	// filterd_protect
	{120,	PIC_WIDTH},	// filterd_tile_row
	{180,	PIC_HEIGHT},	// filterd_tile_col
};

static const struct rkvdec_variant_ops vdpu383_variant_ops = {
	.irq_handler = vdpu383_irq_handler,
	.colmv_size = vdpu383_colmv_size,
	.flatten_matrices = vdpu383_flatten_matrices,
};

static const struct rkvdec_variant vdpu383_variant = {
	.coded_fmts = vdpu383_coded_fmts,
	.num_coded_fmts = ARRAY_SIZE(vdpu383_coded_fmts),
	.rcb_sizes = vdpu383_rcb_sizes,
	.num_rcb_sizes = ARRAY_SIZE(vdpu383_rcb_sizes),
	.ops = &vdpu383_variant_ops,
};

static const struct of_device_id of_rkvdec_match[] = {
	{
		.compatible = "rockchip,rk3288-vdec",
		.data = &rk3288_rkvdec_variant,
	},
	{
		.compatible = "rockchip,rk3328-vdec",
		.data = &rk3328_rkvdec_variant,
	},
	{
		.compatible = "rockchip,rk3399-vdec",
		.data = &rk3399_rkvdec_variant,
	},
	{
		.compatible = "rockchip,rk3588-vdec",
		.data = &vdpu381_variant,
	},
	{
		.compatible = "rockchip,rk3576-vdec",
		.data = &vdpu383_variant,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_rkvdec_match);

static int rkvdec_probe(struct platform_device *pdev)
{
	const struct rkvdec_variant *variant;
	struct rkvdec_dev *rkvdec;
	int ret, irq;

	variant = of_device_get_match_data(&pdev->dev);
	if (!variant)
		return -EINVAL;

	rkvdec = devm_kzalloc(&pdev->dev, sizeof(*rkvdec), GFP_KERNEL);
	if (!rkvdec)
		return -ENOMEM;

	platform_set_drvdata(pdev, rkvdec);
	rkvdec->dev = &pdev->dev;
	rkvdec->variant = variant;
	mutex_init(&rkvdec->vdev_lock);
	INIT_DELAYED_WORK(&rkvdec->watchdog_work, rkvdec_watchdog_func);

	ret = rkvdec_disable_multicore(rkvdec);
	if (ret)
		return ret;

	ret = devm_clk_bulk_get_all_enabled(&pdev->dev, &rkvdec->clocks);
	if (ret < 0)
		return ret;

	rkvdec->num_clocks = ret;
	rkvdec->axi_clk = devm_clk_get(&pdev->dev, "axi");

	if (rkvdec->variant->has_single_reg_region) {
		rkvdec->regs = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(rkvdec->regs))
			return PTR_ERR(rkvdec->regs);
	} else {
		rkvdec->regs = devm_platform_ioremap_resource_byname(pdev, "function");
		if (IS_ERR(rkvdec->regs))
			return PTR_ERR(rkvdec->regs);

		rkvdec->link = devm_platform_ioremap_resource_byname(pdev, "link");
		if (IS_ERR(rkvdec->link))
			return PTR_ERR(rkvdec->link);
	}

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Could not set DMA coherent mask.\n");
		return ret;
	}

	vb2_dma_contig_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return -ENXIO;

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					rkvdec_irq_handler, IRQF_ONESHOT,
					dev_name(&pdev->dev), rkvdec);
	if (ret) {
		dev_err(&pdev->dev, "Could not request vdec IRQ\n");
		return ret;
	}

	rkvdec->sram_pool = of_gen_pool_get(pdev->dev.of_node, "sram", 0);
	if (!rkvdec->sram_pool && rkvdec->variant->num_rcb_sizes > 0)
		dev_info(&pdev->dev, "No sram node, RCB will be stored in RAM\n");

	pm_runtime_set_autosuspend_delay(&pdev->dev, 100);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = rkvdec_v4l2_init(rkvdec);
	if (ret)
		goto err_disable_runtime_pm;

	rkvdec->iommu_domain = iommu_get_domain_for_dev(&pdev->dev);
	if (rkvdec->iommu_domain) {
		rkvdec->empty_domain = iommu_paging_domain_alloc(rkvdec->dev);

		if (IS_ERR(rkvdec->empty_domain)) {
			rkvdec->empty_domain = NULL;
			dev_warn(rkvdec->dev, "cannot alloc new empty domain\n");
		}
	}

	return 0;

err_disable_runtime_pm:
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	if (rkvdec->sram_pool)
		gen_pool_destroy(rkvdec->sram_pool);

	return ret;
}

static void rkvdec_remove(struct platform_device *pdev)
{
	struct rkvdec_dev *rkvdec = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&rkvdec->watchdog_work);

	rkvdec_v4l2_cleanup(rkvdec);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_dont_use_autosuspend(&pdev->dev);

	if (rkvdec->empty_domain)
		iommu_domain_free(rkvdec->empty_domain);
}

#ifdef CONFIG_PM
static int rkvdec_runtime_resume(struct device *dev)
{
	struct rkvdec_dev *rkvdec = dev_get_drvdata(dev);

	return clk_bulk_prepare_enable(rkvdec->num_clocks, rkvdec->clocks);
}

static int rkvdec_runtime_suspend(struct device *dev)
{
	struct rkvdec_dev *rkvdec = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(rkvdec->num_clocks, rkvdec->clocks);
	return 0;
}
#endif

static const struct dev_pm_ops rkvdec_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkvdec_runtime_suspend, rkvdec_runtime_resume, NULL)
};

static struct platform_driver rkvdec_driver = {
	.probe = rkvdec_probe,
	.remove = rkvdec_remove,
	.driver = {
		   .name = "rkvdec",
		   .of_match_table = of_rkvdec_match,
		   .pm = &rkvdec_pm_ops,
	},
};
module_platform_driver(rkvdec_driver);

MODULE_AUTHOR("Boris Brezillon <boris.brezillon@collabora.com>");
MODULE_DESCRIPTION("Rockchip Video Decoder driver");
MODULE_LICENSE("GPL v2");
