// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip VPU codec driver
 *
 * Copyright (C) 2018 Collabora, Ltd.
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 *	Alpha Lin <Alpha.Lin@rock-chips.com>
 *	Jeffy Chen <jeffy.chen@rock-chips.com>
 *
 * Copyright 2018 Google LLC.
 *	Tomasz Figa <tfiga@chromium.org>
 *
 * Based on s5p-mfc driver by Samsung Electronics Co., Ltd.
 * Copyright (C) 2010-2011 Samsung Electronics Co., Ltd.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-sg.h>

#include "rockchip_vpu.h"
#include "rockchip_vpu_hw.h"
#include "rockchip_vpu_common.h"

/**
 * struct v4l2_format_info - information about a V4L2 format
 * @format: 4CC format identifier (V4L2_PIX_FMT_*)
 * @header_size: Size of header, optional and used by compressed formats
 * @num_planes: Number of planes (1 to 3)
 * @cpp: Number of bytes per pixel (per plane)
 * @hsub: Horizontal chroma subsampling factor
 * @vsub: Vertical chroma subsampling factor
 * @is_compressed: Is it a compressed format?
 * @multiplanar: Is it a multiplanar variant format? (e.g. NV12M)
 */
struct v4l2_format_info {
	u32 format;
	u32 header_size;
	u8 num_planes;
	u8 cpp[3];
	u8 hsub;
	u8 vsub;
	u8 is_compressed;
	u8 multiplanar;
};

static const struct v4l2_format_info *
v4l2_format_info(u32 format)
{
	static const struct v4l2_format_info formats[] = {
		{ .format = V4L2_PIX_FMT_YUV420M,	.num_planes = 3, .cpp = { 1, 1, 1 }, .hsub = 2, .vsub = 2, .multiplanar = 1 },
		{ .format = V4L2_PIX_FMT_NV12M,		.num_planes = 2, .cpp = { 1, 2, 0 }, .hsub = 2, .vsub = 2, .multiplanar = 1 },
		{ .format = V4L2_PIX_FMT_YUYV,		.num_planes = 1, .cpp = { 2, 0, 0 }, .hsub = 2, .vsub = 1 },
		{ .format = V4L2_PIX_FMT_UYVY,		.num_planes = 1, .cpp = { 2, 0, 0 }, .hsub = 2, .vsub = 1 },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(formats); ++i) {
		if (formats[i].format == format)
			return &formats[i];
	}

	vpu_err("Unsupported V4L 4CC format (%08x)\n", format);
	return NULL;
}

static void
fill_pixfmt_mp(struct v4l2_pix_format_mplane *pixfmt,
	       int pixelformat, int width, int height)
{
	const struct v4l2_format_info *info;
	struct v4l2_plane_pix_format *plane;
	int i;

	info = v4l2_format_info(pixelformat);
	if (!info)
		return;

	pixfmt->width = width;
	pixfmt->height = height;
	pixfmt->pixelformat = pixelformat;

	if (!info->multiplanar) {
		pixfmt->num_planes = 1;
		plane = &pixfmt->plane_fmt[0];
		plane->bytesperline = info->is_compressed ?
					0 : width * info->cpp[0];
		plane->sizeimage = info->header_size;
		for (i = 0; i < info->num_planes; i++) {
			unsigned int hsub = (i == 0) ? 1 : info->hsub;
			unsigned int vsub = (i == 0) ? 1 : info->vsub;

			plane->sizeimage += info->cpp[i] *
				DIV_ROUND_UP(width, hsub) *
				DIV_ROUND_UP(height, vsub);
		}
	} else {
		pixfmt->num_planes = info->num_planes;
		for (i = 0; i < info->num_planes; i++) {
			unsigned int hsub = (i == 0) ? 1 : info->hsub;
			unsigned int vsub = (i == 0) ? 1 : info->vsub;

			plane = &pixfmt->plane_fmt[i];
			plane->bytesperline =
				info->cpp[i] * DIV_ROUND_UP(width, hsub);
			plane->sizeimage =
				plane->bytesperline * DIV_ROUND_UP(height, vsub);
		}
	}
}

static const struct rockchip_vpu_fmt *
rockchip_vpu_find_format(struct rockchip_vpu_ctx *ctx, u32 fourcc)
{
	struct rockchip_vpu_dev *dev = ctx->dev;
	const struct rockchip_vpu_fmt *formats;
	unsigned int num_fmts, i;

	formats = dev->variant->enc_fmts;
	num_fmts = dev->variant->num_enc_fmts;
	for (i = 0; i < num_fmts; i++)
		if (formats[i].fourcc == fourcc)
			return &formats[i];
	return NULL;
}

static const struct rockchip_vpu_fmt *
rockchip_vpu_get_default_fmt(struct rockchip_vpu_ctx *ctx, bool bitstream)
{
	struct rockchip_vpu_dev *dev = ctx->dev;
	const struct rockchip_vpu_fmt *formats;
	unsigned int num_fmts, i;

	formats = dev->variant->enc_fmts;
	num_fmts = dev->variant->num_enc_fmts;
	for (i = 0; i < num_fmts; i++) {
		if (bitstream == (formats[i].codec_mode != RK_VPU_MODE_NONE))
			return &formats[i];
	}
	return NULL;
}

static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct rockchip_vpu_dev *vpu = video_drvdata(file);

	strscpy(cap->driver, vpu->dev->driver->name, sizeof(cap->driver));
	strscpy(cap->card, vpu->vfd_enc->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform: %s",
		 vpu->dev->driver->name);
	return 0;
}

static int vidioc_enum_framesizes(struct file *file, void *priv,
				  struct v4l2_frmsizeenum *fsize)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	const struct rockchip_vpu_fmt *fmt;

	if (fsize->index != 0) {
		vpu_debug(0, "invalid frame size index (expected 0, got %d)\n",
			  fsize->index);
		return -EINVAL;
	}

	fmt = rockchip_vpu_find_format(ctx, fsize->pixel_format);
	if (!fmt) {
		vpu_debug(0, "unsupported bitstream format (%08x)\n",
			  fsize->pixel_format);
		return -EINVAL;
	}

	/* This only makes sense for coded formats */
	if (fmt->codec_mode == RK_VPU_MODE_NONE)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise = fmt->frmsize;

	return 0;
}

static int vidioc_enum_fmt_vid_cap_mplane(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	struct rockchip_vpu_dev *dev = video_drvdata(file);
	const struct rockchip_vpu_fmt *fmt;
	const struct rockchip_vpu_fmt *formats;
	int num_fmts, i, j = 0;

	formats = dev->variant->enc_fmts;
	num_fmts = dev->variant->num_enc_fmts;
	for (i = 0; i < num_fmts; i++) {
		/* Skip uncompressed formats */
		if (formats[i].codec_mode == RK_VPU_MODE_NONE)
			continue;
		if (j == f->index) {
			fmt = &formats[i];
			f->pixelformat = fmt->fourcc;
			return 0;
		}
		++j;
	}
	return -EINVAL;
}

static int vidioc_enum_fmt_vid_out_mplane(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	struct rockchip_vpu_dev *dev = video_drvdata(file);
	const struct rockchip_vpu_fmt *formats;
	const struct rockchip_vpu_fmt *fmt;
	int num_fmts, i, j = 0;

	formats = dev->variant->enc_fmts;
	num_fmts = dev->variant->num_enc_fmts;
	for (i = 0; i < num_fmts; i++) {
		if (formats[i].codec_mode != RK_VPU_MODE_NONE)
			continue;
		if (j == f->index) {
			fmt = &formats[i];
			f->pixelformat = fmt->fourcc;
			return 0;
		}
		++j;
	}
	return -EINVAL;
}

static int vidioc_g_fmt_out_mplane(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);

	vpu_debug(4, "f->type = %d\n", f->type);

	*pix_mp = ctx->src_fmt;

	return 0;
}

static int vidioc_g_fmt_cap_mplane(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);

	vpu_debug(4, "f->type = %d\n", f->type);

	*pix_mp = ctx->dst_fmt;

	return 0;
}

static int
vidioc_try_fmt_cap_mplane(struct file *file, void *priv, struct v4l2_format *f)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	const struct rockchip_vpu_fmt *fmt;

	vpu_debug(4, "%c%c%c%c\n",
		  (pix_mp->pixelformat & 0x7f),
		  (pix_mp->pixelformat >> 8) & 0x7f,
		  (pix_mp->pixelformat >> 16) & 0x7f,
		  (pix_mp->pixelformat >> 24) & 0x7f);

	fmt = rockchip_vpu_find_format(ctx, pix_mp->pixelformat);
	if (!fmt) {
		fmt = rockchip_vpu_get_default_fmt(ctx, true);
		f->fmt.pix.pixelformat = fmt->fourcc;
	}

	pix_mp->num_planes = 1;
	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->width = clamp(pix_mp->width,
			      fmt->frmsize.min_width,
			      fmt->frmsize.max_width);
	pix_mp->height = clamp(pix_mp->height,
			       fmt->frmsize.min_height,
			       fmt->frmsize.max_height);
	/* Round up to macroblocks. */
	pix_mp->width = round_up(pix_mp->width, JPEG_MB_DIM);
	pix_mp->height = round_up(pix_mp->height, JPEG_MB_DIM);

	/*
	 * For compressed formats the application can specify
	 * sizeimage. If the application passes a zero sizeimage,
	 * let's default to the maximum frame size.
	 */
	if (!pix_mp->plane_fmt[0].sizeimage)
		pix_mp->plane_fmt[0].sizeimage = fmt->header_size +
			pix_mp->width * pix_mp->height * fmt->max_depth;
	memset(pix_mp->plane_fmt[0].reserved, 0,
	       sizeof(pix_mp->plane_fmt[0].reserved));
	return 0;
}

static int
vidioc_try_fmt_out_mplane(struct file *file, void *priv, struct v4l2_format *f)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	const struct rockchip_vpu_fmt *fmt;
	unsigned int width, height;
	int i;

	vpu_debug(4, "%c%c%c%c\n",
		  (pix_mp->pixelformat & 0x7f),
		  (pix_mp->pixelformat >> 8) & 0x7f,
		  (pix_mp->pixelformat >> 16) & 0x7f,
		  (pix_mp->pixelformat >> 24) & 0x7f);

	fmt = rockchip_vpu_find_format(ctx, pix_mp->pixelformat);
	if (!fmt) {
		fmt = rockchip_vpu_get_default_fmt(ctx, false);
		f->fmt.pix.pixelformat = fmt->fourcc;
	}

	pix_mp->field = V4L2_FIELD_NONE;
	width = clamp(pix_mp->width,
		      ctx->vpu_dst_fmt->frmsize.min_width,
		      ctx->vpu_dst_fmt->frmsize.max_width);
	height = clamp(pix_mp->height,
		       ctx->vpu_dst_fmt->frmsize.min_height,
		       ctx->vpu_dst_fmt->frmsize.max_height);
	/* Round up to macroblocks. */
	width = round_up(width, JPEG_MB_DIM);
	height = round_up(height, JPEG_MB_DIM);

	/* Fill remaining fields */
	fill_pixfmt_mp(pix_mp, fmt->fourcc, width, height);

	for (i = 0; i < pix_mp->num_planes; i++) {
		memset(pix_mp->plane_fmt[i].reserved, 0,
		       sizeof(pix_mp->plane_fmt[i].reserved));
	}
	return 0;
}

void rockchip_vpu_enc_reset_dst_fmt(struct rockchip_vpu_dev *vpu,
				    struct rockchip_vpu_ctx *ctx)
{
	struct v4l2_pix_format_mplane *fmt = &ctx->dst_fmt;

	ctx->vpu_dst_fmt = rockchip_vpu_get_default_fmt(ctx, true);

	memset(fmt, 0, sizeof(*fmt));

	fmt->num_planes = 1;
	fmt->width = clamp(fmt->width, ctx->vpu_dst_fmt->frmsize.min_width,
			   ctx->vpu_dst_fmt->frmsize.max_width);
	fmt->height = clamp(fmt->height, ctx->vpu_dst_fmt->frmsize.min_height,
			    ctx->vpu_dst_fmt->frmsize.max_height);
	fmt->pixelformat = ctx->vpu_dst_fmt->fourcc;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_JPEG,
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	fmt->plane_fmt[0].sizeimage = ctx->vpu_dst_fmt->header_size +
		fmt->width * fmt->height * ctx->vpu_dst_fmt->max_depth;
}

void rockchip_vpu_enc_reset_src_fmt(struct rockchip_vpu_dev *vpu,
				    struct rockchip_vpu_ctx *ctx)
{
	struct v4l2_pix_format_mplane *fmt = &ctx->src_fmt;
	unsigned int width, height;

	ctx->vpu_src_fmt = rockchip_vpu_get_default_fmt(ctx, false);

	memset(fmt, 0, sizeof(*fmt));

	width = clamp(fmt->width, ctx->vpu_dst_fmt->frmsize.min_width,
		      ctx->vpu_dst_fmt->frmsize.max_width);
	height = clamp(fmt->height, ctx->vpu_dst_fmt->frmsize.min_height,
		       ctx->vpu_dst_fmt->frmsize.max_height);
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_JPEG,
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	fill_pixfmt_mp(fmt, ctx->vpu_src_fmt->fourcc, width, height);
}

static int
vidioc_s_fmt_out_mplane(struct file *file, void *priv, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq;
	int ret;

	/* Change not allowed if queue is streaming. */
	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_streaming(vq))
		return -EBUSY;

	ret = vidioc_try_fmt_out_mplane(file, priv, f);
	if (ret)
		return ret;

	ctx->vpu_src_fmt = rockchip_vpu_find_format(ctx, pix_mp->pixelformat);
	ctx->src_fmt = *pix_mp;

	/* Propagate to the CAPTURE format */
	ctx->dst_fmt.colorspace = pix_mp->colorspace;
	ctx->dst_fmt.ycbcr_enc = pix_mp->ycbcr_enc;
	ctx->dst_fmt.xfer_func = pix_mp->xfer_func;
	ctx->dst_fmt.quantization = pix_mp->quantization;
	ctx->dst_fmt.width = pix_mp->width;
	ctx->dst_fmt.height = pix_mp->height;

	vpu_debug(0, "OUTPUT codec mode: %d\n", ctx->vpu_src_fmt->codec_mode);
	vpu_debug(0, "fmt - w: %d, h: %d, mb - w: %d, h: %d\n",
		  pix_mp->width, pix_mp->height,
		  JPEG_MB_WIDTH(pix_mp->width),
		  JPEG_MB_HEIGHT(pix_mp->height));
	return 0;
}

static int
vidioc_s_fmt_cap_mplane(struct file *file, void *priv, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	struct rockchip_vpu_dev *vpu = ctx->dev;
	struct vb2_queue *vq, *peer_vq;
	int ret;

	/* Change not allowed if queue is streaming. */
	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_streaming(vq))
		return -EBUSY;

	/*
	 * Since format change on the CAPTURE queue will reset
	 * the OUTPUT queue, we can't allow doing so
	 * when the OUTPUT queue has buffers allocated.
	 */
	peer_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
				  V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (vb2_is_busy(peer_vq) &&
	    (pix_mp->pixelformat != ctx->dst_fmt.pixelformat ||
	     pix_mp->height != ctx->dst_fmt.height ||
	     pix_mp->width != ctx->dst_fmt.width))
		return -EBUSY;

	ret = vidioc_try_fmt_cap_mplane(file, priv, f);
	if (ret)
		return ret;

	ctx->vpu_dst_fmt = rockchip_vpu_find_format(ctx, pix_mp->pixelformat);
	ctx->dst_fmt = *pix_mp;

	vpu_debug(0, "CAPTURE codec mode: %d\n", ctx->vpu_dst_fmt->codec_mode);
	vpu_debug(0, "fmt - w: %d, h: %d, mb - w: %d, h: %d\n",
		  pix_mp->width, pix_mp->height,
		  JPEG_MB_WIDTH(pix_mp->width),
		  JPEG_MB_HEIGHT(pix_mp->height));

	/*
	 * Current raw format might have become invalid with newly
	 * selected codec, so reset it to default just to be safe and
	 * keep internal driver state sane. User is mandated to set
	 * the raw format again after we return, so we don't need
	 * anything smarter.
	 */
	rockchip_vpu_enc_reset_src_fmt(vpu, ctx);
	return 0;
}

const struct v4l2_ioctl_ops rockchip_vpu_enc_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	.vidioc_enum_framesizes = vidioc_enum_framesizes,

	.vidioc_try_fmt_vid_cap_mplane = vidioc_try_fmt_cap_mplane,
	.vidioc_try_fmt_vid_out_mplane = vidioc_try_fmt_out_mplane,
	.vidioc_s_fmt_vid_out_mplane = vidioc_s_fmt_out_mplane,
	.vidioc_s_fmt_vid_cap_mplane = vidioc_s_fmt_cap_mplane,
	.vidioc_g_fmt_vid_out_mplane = vidioc_g_fmt_out_mplane,
	.vidioc_g_fmt_vid_cap_mplane = vidioc_g_fmt_cap_mplane,
	.vidioc_enum_fmt_vid_out_mplane = vidioc_enum_fmt_vid_out_mplane,
	.vidioc_enum_fmt_vid_cap_mplane = vidioc_enum_fmt_vid_cap_mplane,

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
};

static int
rockchip_vpu_queue_setup(struct vb2_queue *vq,
			 unsigned int *num_buffers,
			 unsigned int *num_planes,
			 unsigned int sizes[],
			 struct device *alloc_devs[])
{
	struct rockchip_vpu_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format_mplane *pixfmt;
	int i;

	switch (vq->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		pixfmt = &ctx->dst_fmt;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		pixfmt = &ctx->src_fmt;
		break;
	default:
		vpu_err("invalid queue type: %d\n", vq->type);
		return -EINVAL;
	}

	if (*num_planes) {
		if (*num_planes != pixfmt->num_planes)
			return -EINVAL;
		for (i = 0; i < pixfmt->num_planes; ++i)
			if (sizes[i] < pixfmt->plane_fmt[i].sizeimage)
				return -EINVAL;
		return 0;
	}

	*num_planes = pixfmt->num_planes;
	for (i = 0; i < pixfmt->num_planes; ++i)
		sizes[i] = pixfmt->plane_fmt[i].sizeimage;
	return 0;
}

static int rockchip_vpu_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vb2_queue *vq = vb->vb2_queue;
	struct rockchip_vpu_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format_mplane *pixfmt;
	unsigned int sz;
	int ret = 0;
	int i;

	switch (vq->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		pixfmt = &ctx->dst_fmt;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		pixfmt = &ctx->src_fmt;

		if (vbuf->field == V4L2_FIELD_ANY)
			vbuf->field = V4L2_FIELD_NONE;
		if (vbuf->field != V4L2_FIELD_NONE) {
			vpu_debug(4, "field %d not supported\n",
				  vbuf->field);
			return -EINVAL;
		}
		break;
	default:
		vpu_err("invalid queue type: %d\n", vq->type);
		return -EINVAL;
	}

	for (i = 0; i < pixfmt->num_planes; ++i) {
		sz = pixfmt->plane_fmt[i].sizeimage;
		vpu_debug(4, "plane %d size: %ld, sizeimage: %u\n",
			  i, vb2_plane_size(vb, i), sz);
		if (vb2_plane_size(vb, i) < sz) {
			vpu_err("plane %d is too small\n", i);
			ret = -EINVAL;
			break;
		}
	}

	return ret;
}

static void rockchip_vpu_buf_queue(struct vb2_buffer *vb)
{
	struct rockchip_vpu_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int rockchip_vpu_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct rockchip_vpu_ctx *ctx = vb2_get_drv_priv(q);
	enum rockchip_vpu_codec_mode codec_mode;

	if (V4L2_TYPE_IS_OUTPUT(q->type))
		ctx->sequence_out = 0;
	else
		ctx->sequence_cap = 0;

	/* Set codec_ops for the chosen destination format */
	codec_mode = ctx->vpu_dst_fmt->codec_mode;

	vpu_debug(4, "Codec mode = %d\n", codec_mode);
	ctx->codec_ops = &ctx->dev->variant->codec_ops[codec_mode];

	/* A bounce buffer is needed for the JPEG payload */
	if (!V4L2_TYPE_IS_OUTPUT(q->type)) {
		ctx->bounce_size = ctx->dst_fmt.plane_fmt[0].sizeimage -
				  ctx->vpu_dst_fmt->header_size;
		ctx->bounce_buf = dma_alloc_attrs(ctx->dev->dev,
						  ctx->bounce_size,
						  &ctx->bounce_dma_addr,
						  GFP_KERNEL,
						  DMA_ATTR_ALLOC_SINGLE_PAGES);
	}
	return 0;
}

static void rockchip_vpu_stop_streaming(struct vb2_queue *q)
{
	struct rockchip_vpu_ctx *ctx = vb2_get_drv_priv(q);

	if (!V4L2_TYPE_IS_OUTPUT(q->type))
		dma_free_attrs(ctx->dev->dev,
			       ctx->bounce_size,
			       ctx->bounce_buf,
			       ctx->bounce_dma_addr,
			       DMA_ATTR_ALLOC_SINGLE_PAGES);

	/*
	 * The mem2mem framework calls v4l2_m2m_cancel_job before
	 * .stop_streaming, so there isn't any job running and
	 * it is safe to return all the buffers.
	 */
	for (;;) {
		struct vb2_v4l2_buffer *vbuf;

		if (V4L2_TYPE_IS_OUTPUT(q->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (!vbuf)
			break;
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
	}
}

const struct vb2_ops rockchip_vpu_enc_queue_ops = {
	.queue_setup = rockchip_vpu_queue_setup,
	.buf_prepare = rockchip_vpu_buf_prepare,
	.buf_queue = rockchip_vpu_buf_queue,
	.start_streaming = rockchip_vpu_start_streaming,
	.stop_streaming = rockchip_vpu_stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};
