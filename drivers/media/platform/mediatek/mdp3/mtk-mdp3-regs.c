// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#include <linux/math64.h>
#include <media/v4l2-common.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include "mtk-mdp3-core.h"
#include "mtk-mdp3-regs.h"
#include "mtk-mdp3-m2m.h"

/*
 * All 10-bit related formats are not added in the basic format list,
 * please add the corresponding format settings before use.
 */
static const struct mdp_format mdp_formats[] = {
	{
		.pixelformat	= V4L2_PIX_FMT_GREY,
		.mdp_color	= MDP_COLOR_GREY,
		.depth		= { 8 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_RGB565X,
		.mdp_color	= MDP_COLOR_BGR565,
		.depth		= { 16 },
		.row_depth	= { 16 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_RGB565,
		.mdp_color	= MDP_COLOR_RGB565,
		.depth		= { 16 },
		.row_depth	= { 16 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_RGB24,
		.mdp_color	= MDP_COLOR_RGB888,
		.depth		= { 24 },
		.row_depth	= { 24 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_BGR24,
		.mdp_color	= MDP_COLOR_BGR888,
		.depth		= { 24 },
		.row_depth	= { 24 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_ABGR32,
		.mdp_color	= MDP_COLOR_BGRA8888,
		.depth		= { 32 },
		.row_depth	= { 32 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_ARGB32,
		.mdp_color	= MDP_COLOR_ARGB8888,
		.depth		= { 32 },
		.row_depth	= { 32 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.mdp_color	= MDP_COLOR_UYVY,
		.depth		= { 16 },
		.row_depth	= { 16 },
		.num_planes	= 1,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_VYUY,
		.mdp_color	= MDP_COLOR_VYUY,
		.depth		= { 16 },
		.row_depth	= { 16 },
		.num_planes	= 1,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.mdp_color	= MDP_COLOR_YUYV,
		.depth		= { 16 },
		.row_depth	= { 16 },
		.num_planes	= 1,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YVYU,
		.mdp_color	= MDP_COLOR_YVYU,
		.depth		= { 16 },
		.row_depth	= { 16 },
		.num_planes	= 1,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YUV420,
		.mdp_color	= MDP_COLOR_I420,
		.depth		= { 12 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YVU420,
		.mdp_color	= MDP_COLOR_YV12,
		.depth		= { 12 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV12,
		.mdp_color	= MDP_COLOR_NV12,
		.depth		= { 12 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV21,
		.mdp_color	= MDP_COLOR_NV21,
		.depth		= { 12 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV16,
		.mdp_color	= MDP_COLOR_NV16,
		.depth		= { 16 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV61,
		.mdp_color	= MDP_COLOR_NV61,
		.depth		= { 16 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV24,
		.mdp_color	= MDP_COLOR_NV24,
		.depth		= { 24 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV42,
		.mdp_color	= MDP_COLOR_NV42,
		.depth		= { 24 },
		.row_depth	= { 8 },
		.num_planes	= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_MT21C,
		.mdp_color	= MDP_COLOR_420_BLK_UFO,
		.depth		= { 8, 4 },
		.row_depth	= { 8, 8 },
		.num_planes	= 2,
		.walign		= 4,
		.halign		= 5,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_MM21,
		.mdp_color	= MDP_COLOR_420_BLK,
		.depth		= { 8, 4 },
		.row_depth	= { 8, 8 },
		.num_planes	= 2,
		.walign		= 4,
		.halign		= 5,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV12M,
		.mdp_color	= MDP_COLOR_NV12,
		.depth		= { 8, 4 },
		.row_depth	= { 8, 8 },
		.num_planes	= 2,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV21M,
		.mdp_color	= MDP_COLOR_NV21,
		.depth		= { 8, 4 },
		.row_depth	= { 8, 8 },
		.num_planes	= 2,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV16M,
		.mdp_color	= MDP_COLOR_NV16,
		.depth		= { 8, 8 },
		.row_depth	= { 8, 8 },
		.num_planes	= 2,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_NV61M,
		.mdp_color	= MDP_COLOR_NV61,
		.depth		= { 8, 8 },
		.row_depth	= { 8, 8 },
		.num_planes	= 2,
		.walign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YUV420M,
		.mdp_color	= MDP_COLOR_I420,
		.depth		= { 8, 2, 2 },
		.row_depth	= { 8, 4, 4 },
		.num_planes	= 3,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}, {
		.pixelformat	= V4L2_PIX_FMT_YVU420M,
		.mdp_color	= MDP_COLOR_YV12,
		.depth		= { 8, 2, 2 },
		.row_depth	= { 8, 4, 4 },
		.num_planes	= 3,
		.walign		= 1,
		.halign		= 1,
		.flags		= MDP_FMT_FLAG_OUTPUT | MDP_FMT_FLAG_CAPTURE,
	}
};

static const struct mdp_limit mdp_def_limit = {
	.out_limit = {
		.wmin	= 16,
		.hmin	= 16,
		.wmax	= 8176,
		.hmax	= 8176,
	},
	.cap_limit = {
		.wmin	= 2,
		.hmin	= 2,
		.wmax	= 8176,
		.hmax	= 8176,
	},
	.h_scale_up_max = 32,
	.v_scale_up_max = 32,
	.h_scale_down_max = 20,
	.v_scale_down_max = 128,
};

static const struct mdp_format *mdp_find_fmt(u32 pixelformat, u32 type)
{
	u32 i, flag;

	flag = V4L2_TYPE_IS_OUTPUT(type) ? MDP_FMT_FLAG_OUTPUT :
					MDP_FMT_FLAG_CAPTURE;
	for (i = 0; i < ARRAY_SIZE(mdp_formats); ++i) {
		if (!(mdp_formats[i].flags & flag))
			continue;
		if (mdp_formats[i].pixelformat == pixelformat)
			return &mdp_formats[i];
	}
	return NULL;
}

static const struct mdp_format *mdp_find_fmt_by_index(u32 index, u32 type)
{
	u32 i, flag, num = 0;

	flag = V4L2_TYPE_IS_OUTPUT(type) ? MDP_FMT_FLAG_OUTPUT :
					MDP_FMT_FLAG_CAPTURE;
	for (i = 0; i < ARRAY_SIZE(mdp_formats); ++i) {
		if (!(mdp_formats[i].flags & flag))
			continue;
		if (index == num)
			return &mdp_formats[i];
		num++;
	}
	return NULL;
}

enum mdp_ycbcr_profile mdp_map_ycbcr_prof_mplane(struct v4l2_format *f,
						 u32 mdp_color)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;

	if (MDP_COLOR_IS_RGB(mdp_color))
		return MDP_YCBCR_PROFILE_FULL_BT601;

	switch (pix_mp->colorspace) {
	case V4L2_COLORSPACE_JPEG:
		return MDP_YCBCR_PROFILE_JPEG;
	case V4L2_COLORSPACE_REC709:
	case V4L2_COLORSPACE_DCI_P3:
		if (pix_mp->quantization == V4L2_QUANTIZATION_FULL_RANGE)
			return MDP_YCBCR_PROFILE_FULL_BT709;
		return MDP_YCBCR_PROFILE_BT709;
	case V4L2_COLORSPACE_BT2020:
		if (pix_mp->quantization == V4L2_QUANTIZATION_FULL_RANGE)
			return MDP_YCBCR_PROFILE_FULL_BT2020;
		return MDP_YCBCR_PROFILE_BT2020;
	default:
		if (pix_mp->quantization == V4L2_QUANTIZATION_FULL_RANGE)
			return MDP_YCBCR_PROFILE_FULL_BT601;
		return MDP_YCBCR_PROFILE_BT601;
	}
}

static void mdp_bound_align_image(u32 *w, u32 *h,
				  struct v4l2_frmsize_stepwise *s,
				  unsigned int salign)
{
	unsigned int org_w, org_h;

	org_w = *w;
	org_h = *h;
	v4l_bound_align_image(w, s->min_width, s->max_width, s->step_width,
			      h, s->min_height, s->max_height, s->step_height,
			      salign);

	s->min_width = org_w;
	s->min_height = org_h;
	v4l2_apply_frmsize_constraints(w, h, s);
}

static int mdp_clamp_align(s32 *x, int min, int max, unsigned int align)
{
	unsigned int mask;

	if (min < 0 || max < 0)
		return -ERANGE;

	/* Bits that must be zero to be aligned */
	mask = ~((1 << align) - 1);

	min = 0 ? 0 : ((min + ~mask) & mask);
	max = max & mask;
	if ((unsigned int)min > (unsigned int)max)
		return -ERANGE;

	/* Clamp to aligned min and max */
	*x = clamp(*x, min, max);

	/* Round to nearest aligned value */
	if (align)
		*x = (*x + (1 << (align - 1))) & mask;
	return 0;
}

int mdp_enum_fmt_mplane(struct v4l2_fmtdesc *f)
{
	const struct mdp_format *fmt;

	fmt = mdp_find_fmt_by_index(f->index, f->type);
	if (!fmt)
		return -EINVAL;

	f->pixelformat = fmt->pixelformat;
	return 0;
}

const struct mdp_format *mdp_try_fmt_mplane(struct v4l2_format *f,
					    struct mdp_frameparam *param,
					    u32 ctx_id)
{
	struct device *dev = &param->ctx->mdp_dev->pdev->dev;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	const struct mdp_format *fmt;
	const struct mdp_pix_limit *pix_limit;
	struct v4l2_frmsize_stepwise s;
	u32 org_w, org_h;
	unsigned int i;

	fmt = mdp_find_fmt(pix_mp->pixelformat, f->type);
	if (!fmt) {
		fmt = mdp_find_fmt_by_index(0, f->type);
		if (!fmt) {
			dev_dbg(dev, "%d: pixelformat %c%c%c%c invalid", ctx_id,
				(pix_mp->pixelformat & 0xff),
				(pix_mp->pixelformat >>  8) & 0xff,
				(pix_mp->pixelformat >> 16) & 0xff,
				(pix_mp->pixelformat >> 24) & 0xff);
			return NULL;
		}
	}

	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->flags = 0;
	pix_mp->pixelformat = fmt->pixelformat;
	if (V4L2_TYPE_IS_CAPTURE(f->type)) {
		pix_mp->colorspace = param->colorspace;
		pix_mp->xfer_func = param->xfer_func;
		pix_mp->ycbcr_enc = param->ycbcr_enc;
		pix_mp->quantization = param->quant;
	}

	pix_limit = V4L2_TYPE_IS_OUTPUT(f->type) ? &param->limit->out_limit :
						&param->limit->cap_limit;
	s.min_width = pix_limit->wmin;
	s.max_width = pix_limit->wmax;
	s.step_width = fmt->walign;
	s.min_height = pix_limit->hmin;
	s.max_height = pix_limit->hmax;
	s.step_height = fmt->halign;
	org_w = pix_mp->width;
	org_h = pix_mp->height;

	mdp_bound_align_image(&pix_mp->width, &pix_mp->height, &s, fmt->salign);
	if (org_w != pix_mp->width || org_h != pix_mp->height)
		dev_dbg(dev, "%d: size change: %ux%u to %ux%u", ctx_id,
			org_w, org_h, pix_mp->width, pix_mp->height);

	if (pix_mp->num_planes && pix_mp->num_planes != fmt->num_planes)
		dev_dbg(dev, "%d num of planes change: %u to %u", ctx_id,
			pix_mp->num_planes, fmt->num_planes);
	pix_mp->num_planes = fmt->num_planes;

	for (i = 0; i < pix_mp->num_planes; ++i) {
		u32 min_bpl = (pix_mp->width * fmt->row_depth[i]) >> 3;
		u32 max_bpl = (pix_limit->wmax * fmt->row_depth[i]) >> 3;
		u32 bpl = pix_mp->plane_fmt[i].bytesperline;
		u32 min_si, max_si;
		u32 si = pix_mp->plane_fmt[i].sizeimage;
		u64 di;

		bpl = clamp(bpl, min_bpl, max_bpl);
		pix_mp->plane_fmt[i].bytesperline = bpl;

		di = (u64)bpl * pix_mp->height * fmt->depth[i];
		min_si = (u32)div_u64(di, fmt->row_depth[i]);
		di = (u64)bpl * s.max_height * fmt->depth[i];
		max_si = (u32)div_u64(di, fmt->row_depth[i]);

		si = clamp(si, min_si, max_si);
		pix_mp->plane_fmt[i].sizeimage = si;

		dev_dbg(dev, "%d: p%u, bpl:%u [%u, %u], sizeimage:%u [%u, %u]",
			ctx_id, i, bpl, min_bpl, max_bpl, si, min_si, max_si);
	}

	return fmt;
}

static int mdp_clamp_start(s32 *x, int min, int max, unsigned int align,
			   u32 flags)
{
	if (flags & V4L2_SEL_FLAG_GE)
		max = *x;
	if (flags & V4L2_SEL_FLAG_LE)
		min = *x;
	return mdp_clamp_align(x, min, max, align);
}

static int mdp_clamp_end(s32 *x, int min, int max, unsigned int align,
			 u32 flags)
{
	if (flags & V4L2_SEL_FLAG_GE)
		min = *x;
	if (flags & V4L2_SEL_FLAG_LE)
		max = *x;
	return mdp_clamp_align(x, min, max, align);
}

int mdp_try_crop(struct mdp_m2m_ctx *ctx, struct v4l2_rect *r,
		 const struct v4l2_selection *s, struct mdp_frame *frame)
{
	struct device *dev = &ctx->mdp_dev->pdev->dev;
	s32 left, top, right, bottom;
	u32 framew, frameh, walign, halign;
	int ret;

	dev_dbg(dev, "%d target:%d, set:(%d,%d) %ux%u", ctx->id,
		s->target, s->r.left, s->r.top, s->r.width, s->r.height);

	left = s->r.left;
	top = s->r.top;
	right = s->r.left + s->r.width;
	bottom = s->r.top + s->r.height;
	framew = frame->format.fmt.pix_mp.width;
	frameh = frame->format.fmt.pix_mp.height;

	if (mdp_target_is_crop(s->target)) {
		walign = 1;
		halign = 1;
	} else {
		walign = frame->mdp_fmt->walign;
		halign = frame->mdp_fmt->halign;
	}

	dev_dbg(dev, "%d align:%u,%u, bound:%ux%u", ctx->id,
		walign, halign, framew, frameh);

	ret = mdp_clamp_start(&left, 0, right, walign, s->flags);
	if (ret)
		return ret;
	ret = mdp_clamp_start(&top, 0, bottom, halign, s->flags);
	if (ret)
		return ret;
	ret = mdp_clamp_end(&right, left, framew, walign, s->flags);
	if (ret)
		return ret;
	ret = mdp_clamp_end(&bottom, top, frameh, halign, s->flags);
	if (ret)
		return ret;

	r->left = left;
	r->top = top;
	r->width = right - left;
	r->height = bottom - top;

	dev_dbg(dev, "%d crop:(%d,%d) %ux%u", ctx->id,
		r->left, r->top, r->width, r->height);
	return 0;
}

int mdp_check_scaling_ratio(const struct v4l2_rect *crop,
			    const struct v4l2_rect *compose, s32 rotation,
	const struct mdp_limit *limit)
{
	u32 crop_w, crop_h, comp_w, comp_h;

	crop_w = crop->width;
	crop_h = crop->height;
	if (90 == rotation || 270 == rotation) {
		comp_w = compose->height;
		comp_h = compose->width;
	} else {
		comp_w = compose->width;
		comp_h = compose->height;
	}

	if ((crop_w / comp_w) > limit->h_scale_down_max ||
	    (crop_h / comp_h) > limit->v_scale_down_max ||
	    (comp_w / crop_w) > limit->h_scale_up_max ||
	    (comp_h / crop_h) > limit->v_scale_up_max)
		return -ERANGE;
	return 0;
}

/* Stride that is accepted by MDP HW */
static u32 mdp_fmt_get_stride(const struct mdp_format *fmt,
			      u32 bytesperline, unsigned int plane)
{
	enum mdp_color c = fmt->mdp_color;
	u32 stride;

	stride = (bytesperline * MDP_COLOR_BITS_PER_PIXEL(c))
		/ fmt->row_depth[0];
	if (plane == 0)
		return stride;
	if (plane < MDP_COLOR_GET_PLANE_COUNT(c)) {
		if (MDP_COLOR_IS_BLOCK_MODE(c))
			stride = stride / 2;
		return stride;
	}
	return 0;
}

/* Stride that is accepted by MDP HW of format with contiguous planes */
static u32 mdp_fmt_get_stride_contig(const struct mdp_format *fmt,
				     u32 pix_stride, unsigned int plane)
{
	enum mdp_color c = fmt->mdp_color;
	u32 stride = pix_stride;

	if (plane == 0)
		return stride;
	if (plane < MDP_COLOR_GET_PLANE_COUNT(c)) {
		stride = stride >> MDP_COLOR_GET_H_SUBSAMPLE(c);
		if (MDP_COLOR_IS_UV_COPLANE(c) && !MDP_COLOR_IS_BLOCK_MODE(c))
			stride = stride * 2;
		return stride;
	}
	return 0;
}

/* Plane size that is accepted by MDP HW */
static u32 mdp_fmt_get_plane_size(const struct mdp_format *fmt,
				  u32 stride, u32 height, unsigned int plane)
{
	enum mdp_color c = fmt->mdp_color;
	u32 bytesperline;

	bytesperline = (stride * fmt->row_depth[0])
		/ MDP_COLOR_BITS_PER_PIXEL(c);
	if (plane == 0)
		return bytesperline * height;
	if (plane < MDP_COLOR_GET_PLANE_COUNT(c)) {
		height = height >> MDP_COLOR_GET_V_SUBSAMPLE(c);
		if (MDP_COLOR_IS_BLOCK_MODE(c))
			bytesperline = bytesperline * 2;
		return bytesperline * height;
	}
	return 0;
}

static void mdp_prepare_buffer(struct img_image_buffer *b,
			       struct mdp_frame *frame, struct vb2_buffer *vb)
{
	struct v4l2_pix_format_mplane *pix_mp = &frame->format.fmt.pix_mp;
	unsigned int i;

	b->format.colorformat = frame->mdp_fmt->mdp_color;
	b->format.ycbcr_prof = frame->ycbcr_prof;
	for (i = 0; i < pix_mp->num_planes; ++i) {
		u32 stride = mdp_fmt_get_stride(frame->mdp_fmt,
			pix_mp->plane_fmt[i].bytesperline, i);

		b->format.plane_fmt[i].stride = stride;
		b->format.plane_fmt[i].size =
			mdp_fmt_get_plane_size(frame->mdp_fmt, stride,
					       pix_mp->height, i);
		b->iova[i] = vb2_dma_contig_plane_dma_addr(vb, i);
	}
	for (; i < MDP_COLOR_GET_PLANE_COUNT(b->format.colorformat); ++i) {
		u32 stride = mdp_fmt_get_stride_contig(frame->mdp_fmt,
			b->format.plane_fmt[0].stride, i);

		b->format.plane_fmt[i].stride = stride;
		b->format.plane_fmt[i].size =
			mdp_fmt_get_plane_size(frame->mdp_fmt, stride,
					       pix_mp->height, i);
		b->iova[i] = b->iova[i - 1] + b->format.plane_fmt[i - 1].size;
	}
	b->usage = frame->usage;
}

void mdp_set_src_config(struct img_input *in,
			struct mdp_frame *frame, struct vb2_buffer *vb)
{
	in->buffer.format.width = frame->format.fmt.pix_mp.width;
	in->buffer.format.height = frame->format.fmt.pix_mp.height;
	mdp_prepare_buffer(&in->buffer, frame, vb);
}

static u32 mdp_to_fixed(u32 *r, struct v4l2_fract *f)
{
	u32 q;

	if (f->denominator == 0) {
		*r = 0;
		return 0;
	}

	q = f->numerator / f->denominator;
	*r = div_u64(((u64)f->numerator - q * f->denominator) <<
		     IMG_SUBPIXEL_SHIFT, f->denominator);
	return q;
}

static void mdp_set_src_crop(struct img_crop *c, struct mdp_crop *crop)
{
	c->left = crop->c.left
		+ mdp_to_fixed(&c->left_subpix, &crop->left_subpix);
	c->top = crop->c.top
		+ mdp_to_fixed(&c->top_subpix, &crop->top_subpix);
	c->width = crop->c.width
		+ mdp_to_fixed(&c->width_subpix, &crop->width_subpix);
	c->height = crop->c.height
		+ mdp_to_fixed(&c->height_subpix, &crop->height_subpix);
}

static void mdp_set_orientation(struct img_output *out,
				s32 rotation, bool hflip, bool vflip)
{
	u8 flip = 0;

	if (hflip)
		flip ^= 1;
	if (vflip) {
		/*
		 * A vertical flip is equivalent to
		 * a 180-degree rotation with a horizontal flip
		 */
		rotation += 180;
		flip ^= 1;
	}

	out->rotation = rotation % 360;
	if (flip != 0)
		out->flags |= IMG_CTRL_FLAG_HFLIP;
	else
		out->flags &= ~IMG_CTRL_FLAG_HFLIP;
}

void mdp_set_dst_config(struct img_output *out,
			struct mdp_frame *frame, struct vb2_buffer *vb)
{
	out->buffer.format.width = frame->compose.width;
	out->buffer.format.height = frame->compose.height;
	mdp_prepare_buffer(&out->buffer, frame, vb);
	mdp_set_src_crop(&out->crop, &frame->crop);
	mdp_set_orientation(out, frame->rotation, frame->hflip, frame->vflip);
}

int mdp_frameparam_init(struct mdp_frameparam *param)
{
	struct mdp_frame *frame;

	if (!param)
		return -EINVAL;

	INIT_LIST_HEAD(&param->list);
	param->limit = &mdp_def_limit;
	param->type = MDP_STREAM_TYPE_BITBLT;

	frame = &param->output;
	frame->format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	frame->mdp_fmt = mdp_try_fmt_mplane(&frame->format, param, 0);
	frame->ycbcr_prof =
		mdp_map_ycbcr_prof_mplane(&frame->format,
					  frame->mdp_fmt->mdp_color);
	frame->usage = MDP_BUFFER_USAGE_HW_READ;

	param->num_captures = 1;
	frame = &param->captures[0];
	frame->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	frame->mdp_fmt = mdp_try_fmt_mplane(&frame->format, param, 0);
	frame->ycbcr_prof =
		mdp_map_ycbcr_prof_mplane(&frame->format,
					  frame->mdp_fmt->mdp_color);
	frame->usage = MDP_BUFFER_USAGE_MDP;
	frame->crop.c.width = param->output.format.fmt.pix_mp.width;
	frame->crop.c.height = param->output.format.fmt.pix_mp.height;
	frame->compose.width = frame->format.fmt.pix_mp.width;
	frame->compose.height = frame->format.fmt.pix_mp.height;

	return 0;
}
