/* linux/drivers/media/video/exynos/rotator/rotator-core.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Core file for Samsung Exynos Image Rotator driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/version.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <mach/videonode.h>

#include "rotator.h"

module_param_named(log_level, log_level, uint, 0644);

static struct rot_fmt rot_formats[] = {
	{
		.name		= "RGB565",
		.pixelformat	= V4L2_PIX_FMT_RGB565,
		.num_planes	= 1,
		.nr_comp	= 1,
		.bitperpixel	= { 16 },
	}, {
		.name		= "XRGB-8888, 32 bps",
		.pixelformat	= V4L2_PIX_FMT_RGB32,
		.num_planes	= 1,
		.nr_comp	= 1,
		.bitperpixel	= { 32 },
	}, {
		.name		= "YUV 4:2:2 packed, YCbYCr",
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.num_planes	= 1,
		.nr_comp	= 1,
		.bitperpixel	= { 16 },
	}, {
		.name		= "YUV 4:2:0 non-contiguous 2-planar, Y/CbCr",
		.pixelformat	= V4L2_PIX_FMT_NV12M,
		.num_planes	= 2,
		.nr_comp	= 2,
		.bitperpixel	= { 8, 4 },
	}, {
		.name		= "YUV 4:2:0 non-contiguous 3-planar, Y/Cb/Cr",
		.pixelformat	= V4L2_PIX_FMT_YUV420M,
		.num_planes	= 3,
		.nr_comp	= 3,
		.bitperpixel	= { 8, 2, 2 },
	},
};

static struct v4l2_queryctrl rot_ctrls[] = {
	{
		.id		= V4L2_CID_HFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Horizontal flip",
		.minimum	= 0,
		.maximum	= 1,
		.default_value	= 0,
	}, {
		.id		= V4L2_CID_VFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Vertical flip",
		.minimum	= 0,
		.maximum	= 1,
		.default_value	= 0,
	}, {
		.id		= V4L2_CID_ROTATE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Rotation",
		.minimum	= 0,
		.maximum	= 270,
		.step		= 90,
		.default_value	= 0,
	}, {
		.id		= V4L2_CID_CACHEABLE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Enable cache configuration",
		.minimum	= 0,
		.maximum	= 1,
		.default_value	= 1,
	},
};

/* Find the matches format */
struct rot_fmt *rot_find_format(struct v4l2_format *f)
{
	struct rot_fmt *rot_fmt;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rot_formats); ++i) {
		rot_fmt = &rot_formats[i];
		if (rot_fmt->pixelformat == f->fmt.pix_mp.pixelformat)
			return &rot_formats[i];
	}

	return NULL;
}

static struct v4l2_queryctrl *rot_find_ctrl(int id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rot_ctrls); ++i) {
		if (id == rot_ctrls[i].id)
			return &rot_ctrls[i];
	}

	return NULL;
}

void rot_bound_align_image(struct rot_ctx *ctx, struct rot_fmt *rot_fmt,
			   u32 *width, u32 *height)
{
	struct exynos_rot_variant *variant = ctx->rot_dev->variant;
	struct exynos_rot_size_limit *limit = NULL;

	switch (rot_fmt->pixelformat) {
	case V4L2_PIX_FMT_YUV420M:
		limit = &variant->limit_yuv420_3p;
		break;
	case V4L2_PIX_FMT_NV12M:
		limit = &variant->limit_yuv420_2p;
		break;
	case V4L2_PIX_FMT_YUYV:
		limit = &variant->limit_yuv422;
		break;
	case V4L2_PIX_FMT_RGB565:
		limit =	&variant->limit_rgb565;
		break;
	case V4L2_PIX_FMT_RGB32:
		limit = &variant->limit_rgb888;
		break;
	default:
		break;
	}

	/* Bound an image to have width and height in limit */
	v4l_bound_align_image(width, limit->min_x, limit->max_x,
			limit->align, height, limit->min_y,
			limit->max_y, limit->align, 0);
}

void rot_adjust_pixminfo(struct rot_ctx *ctx, struct rot_frame *frame,
			 struct v4l2_pix_format_mplane *pixm)
{
	struct rot_frame *rot_frame;

	if (frame == &ctx->s_frame) {
		if (test_bit(CTX_DST, &ctx->flags)) {
			rot_frame = &ctx->d_frame;
			pixm->pixelformat = rot_frame->rot_fmt->pixelformat;
		}
		set_bit(CTX_SRC, &ctx->flags);
	} else if (frame == &ctx->d_frame) {
		if (test_bit(CTX_SRC, &ctx->flags)) {
			rot_frame = &ctx->s_frame;
			pixm->pixelformat = rot_frame->rot_fmt->pixelformat;
		}
		set_bit(CTX_DST, &ctx->flags);
	}
}

void rot_adjust_cropinfo(struct rot_ctx *ctx, struct rot_frame *frame,
			 struct v4l2_rect *crop)
{
	struct rot_frame *rot_frame;

	if (frame == &ctx->s_frame) {
		if (test_bit(CTX_DST, &ctx->flags)) {
			rot_frame = &ctx->d_frame;
			crop->width  = rot_frame->crop.height;
			crop->height = rot_frame->crop.width;
		}
		set_bit(CTX_SRC, &ctx->flags);
	} else if (frame == &ctx->d_frame) {
		if (test_bit(CTX_SRC, &ctx->flags)) {
			rot_frame = &ctx->s_frame;
			crop->width  = rot_frame->crop.height;
			crop->height = rot_frame->crop.width;
		}
		set_bit(CTX_DST, &ctx->flags);
	}
}

static int rot_v4l2_querycap(struct file *file, void *priv,
			     struct v4l2_capability *cap)
{
	struct rot_ctx *ctx = file->private_data;
	struct rot_dev *rot = ctx->rot_dev;

	strncpy(cap->driver, rot->pdev->name, sizeof(cap->driver) - 1);
	strncpy(cap->card, rot->pdev->name, sizeof(cap->card) - 1);

	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(MAJOR_VERSION,
				      MINOR_VERSION,
				      RELEASE_VERSION);
	cap->capabilities = V4L2_CAP_STREAMING |
		V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE;

	return 0;
}

int rot_v4l2_enum_fmt_mplane(struct file *file, void *priv,
			     struct v4l2_fmtdesc *f)
{
	struct rot_fmt *rot_fmt;

	if (f->index >= ARRAY_SIZE(rot_formats)) {
		rot_err("invalid number of format\n");
		return -EINVAL;
	}

	rot_fmt = &rot_formats[f->index];
	strncpy(f->description, rot_fmt->name, sizeof(f->description) - 1);
	f->pixelformat = rot_fmt->pixelformat;

	return 0;
}

int rot_v4l2_g_fmt_mplane(struct file *file, void *priv,
			  struct v4l2_format *f)
{
	struct rot_ctx *ctx = priv;
	struct rot_fmt *rot_fmt;
	struct rot_frame *frame;
	struct v4l2_pix_format_mplane *pixm = &f->fmt.pix_mp;
	int i;

	frame = ctx_get_frame(ctx, f->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	rot_fmt = frame->rot_fmt;

	pixm->width		= frame->pix_mp.width;
	pixm->height		= frame->pix_mp.height;
	pixm->pixelformat	= frame->pix_mp.pixelformat;
	pixm->field		= V4L2_FIELD_NONE;
	pixm->num_planes	= frame->rot_fmt->num_planes;
	pixm->colorspace	= 0;

	for (i = 0; i < pixm->num_planes; ++i) {
		pixm->plane_fmt[i].bytesperline = (pixm->width *
				rot_fmt->bitperpixel[i]) >> 3;
		pixm->plane_fmt[i].sizeimage = pixm->plane_fmt[i].bytesperline
				* pixm->height;

		rot_dbg("[%d] plane: bytesperline %d, sizeimage %d\n", i,
				pixm->plane_fmt[i].bytesperline,
				pixm->plane_fmt[i].sizeimage);
	}

	return 0;
}

int rot_v4l2_try_fmt_mplane(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct rot_ctx *ctx = priv;
	struct rot_fmt *rot_fmt;
	struct v4l2_pix_format_mplane *pixm = &f->fmt.pix_mp;
	int i;

	if (!V4L2_TYPE_IS_MULTIPLANAR(f->type)) {
		rot_err("not supported format type\n");
		return -EINVAL;
	}

	rot_fmt = rot_find_format(f);
	if (!rot_fmt) {
		rot_err("not supported format values\n");
		return -EINVAL;
	}

	rot_bound_align_image(ctx, rot_fmt, &pixm->width, &pixm->height);

	pixm->num_planes = rot_fmt->num_planes;
	pixm->colorspace = 0;

	for (i = 0; i < pixm->num_planes; ++i) {
		pixm->plane_fmt[i].bytesperline = (pixm->width *
				rot_fmt->bitperpixel[i]) >> 3;
		pixm->plane_fmt[i].sizeimage = pixm->plane_fmt[i].bytesperline
				* pixm->height;

		rot_dbg("[%d] plane: bytesperline %d, sizeimage %d\n", i,
				pixm->plane_fmt[i].bytesperline,
				pixm->plane_fmt[i].sizeimage);
	}

	return 0;
}

static int rot_v4l2_s_fmt_mplane(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct rot_ctx *ctx = priv;
	struct vb2_queue *vq;
	struct rot_frame *frame;
	struct v4l2_pix_format_mplane *pixm = &f->fmt.pix_mp;
	int i, ret = 0;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);

	if (vb2_is_streaming(vq)) {
		rot_err("device is busy\n");
		return -EBUSY;
	}

	ret = rot_v4l2_try_fmt_mplane(file, priv, f);
	if (ret < 0)
		return ret;

	frame = ctx_get_frame(ctx, f->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	set_bit(CTX_PARAMS, &ctx->flags);

	frame->rot_fmt = rot_find_format(f);
	if (!frame->rot_fmt) {
		rot_err("not supported format values\n");
		return -EINVAL;
	}

	rot_adjust_pixminfo(ctx, frame, pixm);

	frame->pix_mp.pixelformat = pixm->pixelformat;
	frame->pix_mp.width	= pixm->width;
	frame->pix_mp.height	= pixm->height;

	/*
	 * Shouldn't call s_crop or g_crop before called g_fmt or s_fmt.
	 * Let's assume that we can keep the order.
	 */
	frame->crop.width	= pixm->width;
	frame->crop.height	= pixm->height;

	for (i = 0; i < frame->rot_fmt->num_planes; ++i)
		frame->bytesused[i] = (pixm->width * pixm->height *
				frame->rot_fmt->bitperpixel[i]) >> 3;

	return 0;
}

static int rot_v4l2_reqbufs(struct file *file, void *priv,
			    struct v4l2_requestbuffers *reqbufs)
{
	struct rot_ctx *ctx = priv;
	struct rot_dev *rot = ctx->rot_dev;
	struct rot_frame *frame;

	frame = ctx_get_frame(ctx, reqbufs->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	if (frame == &ctx->s_frame)
		clear_bit(CTX_SRC, &ctx->flags);
	else if (frame == &ctx->d_frame)
		clear_bit(CTX_DST, &ctx->flags);

	frame->cacheable = ctx->cacheable;
	rot->vb2->set_cacheable(rot->alloc_ctx, frame->cacheable);

	return v4l2_m2m_reqbufs(file, ctx->m2m_ctx, reqbufs);
}

static int rot_v4l2_querybuf(struct file *file, void *priv,
			     struct v4l2_buffer *buf)
{
	struct rot_ctx *ctx = priv;
	return v4l2_m2m_querybuf(file, ctx->m2m_ctx, buf);
}

static int rot_v4l2_qbuf(struct file *file, void *priv,
			 struct v4l2_buffer *buf)
{
	struct rot_ctx *ctx = priv;
	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int rot_v4l2_dqbuf(struct file *file, void *priv,
			  struct v4l2_buffer *buf)
{
	struct rot_ctx *ctx = priv;
	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}

static int rot_v4l2_streamon(struct file *file, void *priv,
			     enum v4l2_buf_type type)
{
	struct rot_ctx *ctx = priv;
	return v4l2_m2m_streamon(file, ctx->m2m_ctx, type);
}

static int rot_v4l2_streamoff(struct file *file, void *priv,
			      enum v4l2_buf_type type)
{
	struct rot_ctx *ctx = priv;
	return v4l2_m2m_streamoff(file, ctx->m2m_ctx, type);
}

int rot_v4l2_queryctrl(struct file *file, void *priv,
		       struct v4l2_queryctrl *qc)
{
	struct v4l2_queryctrl *c;

	c = rot_find_ctrl(qc->id);
	if (!c) {
		rot_err("not supported control id\n");
		return -EINVAL;
	}
	*qc = *c;

	return 0;
}

int rot_v4l2_g_ctrl(struct file *file, void *priv,
		    struct v4l2_control *ctrl)
{
	struct rot_ctx *ctx = priv;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		ctrl->value = (ROT_VFLIP & ctx->flip) ? 1 : 0;
		break;
	case V4L2_CID_HFLIP:
		ctrl->value = (ROT_HFLIP & ctx->flip) ? 1 : 0;
		break;
	case V4L2_CID_ROTATE:
		ctrl->value = ctx->rotation;
		break;
	case V4L2_CID_CACHEABLE:
		ctrl->value = (int)ctx->cacheable;
		break;
	default:
		rot_err("invalid control id\n");
		return -EINVAL;
	}

	return 0;
}

int rot_check_ctrl_val(struct rot_ctx *ctx,  struct v4l2_control *ctrl)
{
	struct v4l2_queryctrl *c;

	c = rot_find_ctrl(ctrl->id);
	if (!c) {
		rot_err("not supported control id\n");
		return -EINVAL;
	}

	if (ctrl->value < c->minimum || ctrl->value > c->maximum
		|| ((c->step != 0) && (ctrl->value % c->step != 0))) {
		rot_err("not supported control value\n");
		return -ERANGE;
	}

	return 0;
}

int rot_v4l2_s_ctrl(struct file *file, void *priv,
		    struct v4l2_control *ctrl)
{
	struct rot_ctx *ctx = file->private_data;
	int ret;

	ret = rot_check_ctrl_val(ctx, ctrl);
	if (ret)
		return ret;

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		if (ctrl->value)
			ctx->flip |= ROT_VFLIP;
		else
			ctx->flip &= ~ROT_VFLIP;
		break;
	case V4L2_CID_HFLIP:
		if (ctrl->value)
			ctx->flip |= ROT_HFLIP;
		else
			ctx->flip &= ~ROT_HFLIP;
		break;
	case V4L2_CID_ROTATE:
		ctx->rotation = ctrl->value;
		break;
	case V4L2_CID_CACHEABLE:
		ctx->cacheable = (bool)ctrl->value;
		break;
	default:
		rot_err("invalid control id\n");
		return -EINVAL;
	}

	return 0;
}

static int rot_v4l2_cropcap(struct file *file, void *fh,
			    struct v4l2_cropcap *cr)
{
	struct rot_ctx *ctx = fh;
	struct rot_frame *frame;

	frame = ctx_get_frame(ctx, cr->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	cr->bounds.left		= 0;
	cr->bounds.top		= 0;
	cr->bounds.width	= frame->pix_mp.width;
	cr->bounds.height	= frame->pix_mp.height;
	cr->defrect		= cr->bounds;

	return 0;
}

static int rot_v4l2_g_crop(struct file *file, void *fh, struct v4l2_crop *cr)
{
	struct rot_ctx *ctx = fh;
	struct rot_frame *frame;

	frame = ctx_get_frame(ctx, cr->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	cr->c = frame->crop;

	return 0;
}

static int rot_v4l2_s_crop(struct file *file, void *fh, struct v4l2_crop *cr)
{
	struct rot_ctx *ctx = fh;
	struct rot_frame *frame;
	struct v4l2_pix_format_mplane *pixm;
	int i;

	frame = ctx_get_frame(ctx, cr->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	if (!test_bit(CTX_PARAMS, &ctx->flags)) {
		rot_err("color format is not set\n");
		return -EINVAL;
	}

	if (cr->c.left < 0 || cr->c.top < 0 ||
			cr->c.width < 0 || cr->c.height < 0) {
		rot_err("crop value is negative\n");
		return -EINVAL;
	}

	pixm = &frame->pix_mp;
	rot_adjust_cropinfo(ctx, frame, &cr->c);
	rot_bound_align_image(ctx, frame->rot_fmt, &cr->c.width, &cr->c.height);

	/* Adjust left/top if cropping rectangle is out of bounds */
	if (cr->c.left + cr->c.width > pixm->width) {
		rot_warn("out of bound left cropping size:left %d, width %d\n",
				cr->c.left, cr->c.width);
		cr->c.left = pixm->width - cr->c.width;
	}
	if (cr->c.top + cr->c.height > pixm->height) {
		rot_warn("out of bound top cropping size:top %d, height %d\n",
				cr->c.top, cr->c.height);
		cr->c.top = pixm->height - cr->c.height;
	}

	frame->crop = cr->c;

	for (i = 0; i < frame->rot_fmt->num_planes; ++i)
		frame->bytesused[i] = (cr->c.width * cr->c.height *
				frame->rot_fmt->bitperpixel[i]) >> 3;

	return 0;
}

static const struct v4l2_ioctl_ops rot_v4l2_ioctl_ops = {
	.vidioc_querycap		= rot_v4l2_querycap,

	.vidioc_enum_fmt_vid_cap_mplane	= rot_v4l2_enum_fmt_mplane,
	.vidioc_enum_fmt_vid_out_mplane	= rot_v4l2_enum_fmt_mplane,

	.vidioc_g_fmt_vid_cap_mplane	= rot_v4l2_g_fmt_mplane,
	.vidioc_g_fmt_vid_out_mplane	= rot_v4l2_g_fmt_mplane,

	.vidioc_try_fmt_vid_cap_mplane	= rot_v4l2_try_fmt_mplane,
	.vidioc_try_fmt_vid_out_mplane	= rot_v4l2_try_fmt_mplane,

	.vidioc_s_fmt_vid_cap_mplane	= rot_v4l2_s_fmt_mplane,
	.vidioc_s_fmt_vid_out_mplane	= rot_v4l2_s_fmt_mplane,

	.vidioc_reqbufs			= rot_v4l2_reqbufs,
	.vidioc_querybuf		= rot_v4l2_querybuf,

	.vidioc_qbuf			= rot_v4l2_qbuf,
	.vidioc_dqbuf			= rot_v4l2_dqbuf,

	.vidioc_streamon		= rot_v4l2_streamon,
	.vidioc_streamoff		= rot_v4l2_streamoff,

	.vidioc_queryctrl		= rot_v4l2_queryctrl,
	.vidioc_g_ctrl			= rot_v4l2_g_ctrl,
	.vidioc_s_ctrl			= rot_v4l2_s_ctrl,

	.vidioc_g_crop			= rot_v4l2_g_crop,
	.vidioc_s_crop			= rot_v4l2_s_crop,
	.vidioc_cropcap			= rot_v4l2_cropcap
};

static int rot_ctx_stop_req(struct rot_ctx *ctx)
{
	struct rot_ctx *curr_ctx;
	struct rot_dev *rot = ctx->rot_dev;
	int ret = 0;

	curr_ctx = v4l2_m2m_get_curr_priv(rot->m2m.m2m_dev);
	if (!test_bit(CTX_RUN, &ctx->flags) || (curr_ctx != ctx))
		return 0;

	set_bit(CTX_ABORT, &ctx->flags);

	ret = wait_event_timeout(rot->irq.wait,
			!test_bit(CTX_RUN, &ctx->flags), ROT_TIMEOUT);

	/* TODO: How to handle case of timeout event */
	if (!ret) {
		rot_err("device failed to stop request\n");
		ret = -EBUSY;
	}

	return ret;
}

static int rot_vb2_queue_setup(struct vb2_queue *vq, unsigned int *num_buffers,
			       unsigned int *num_planes, unsigned long sizes[],
			       void *alloc_ctxs[])
{
	struct rot_ctx *ctx = vb2_get_drv_priv(vq);
	struct rot_frame *frame;
	int i;

	frame = ctx_get_frame(ctx, vq->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	/* Get number of planes from format_list in driver */
	*num_planes = frame->rot_fmt->num_planes;
	for (i = 0; i < frame->rot_fmt->num_planes; i++) {
		sizes[i] = (frame->pix_mp.width * frame->pix_mp.height *
				frame->rot_fmt->bitperpixel[i]) >> 3;
		alloc_ctxs[i] = ctx->rot_dev->alloc_ctx;
	}

	return 0;
}

static int rot_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct rot_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct rot_frame *frame;
	int i;

	frame = ctx_get_frame(ctx, vb->vb2_queue->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	if (!V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {
		for (i = 0; i < frame->rot_fmt->num_planes; i++)
			vb2_set_plane_payload(vb, i, frame->bytesused[i]);
	}

	if (frame->cacheable)
		ctx->rot_dev->vb2->cache_flush(vb, frame->rot_fmt->num_planes);

	return 0;
}

static void rot_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct rot_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (ctx->m2m_ctx)
		v4l2_m2m_buf_queue(ctx->m2m_ctx, vb);
}

static void rot_vb2_lock(struct vb2_queue *vq)
{
	struct rot_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_lock(&ctx->rot_dev->lock);
}

static void rot_vb2_unlock(struct vb2_queue *vq)
{
	struct rot_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_unlock(&ctx->rot_dev->lock);
}

static int rot_vb2_start_streaming(struct vb2_queue *vq)
{
	struct rot_ctx *ctx = vb2_get_drv_priv(vq);
	set_bit(CTX_STREAMING, &ctx->flags);

	return 0;
}

static int rot_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct rot_ctx *ctx = vb2_get_drv_priv(vq);
	struct rot_dev *rot = ctx->rot_dev;
	int ret;

	ret = rot_ctx_stop_req(ctx);
	if (ret < 0)
		rot_err("wait timeout\n");

	clear_bit(CTX_STREAMING, &ctx->flags);
	v4l2_m2m_get_next_job(rot->m2m.m2m_dev, ctx->m2m_ctx);

	return 0;
}

struct vb2_ops rot_vb2_ops = {
	.queue_setup		 = rot_vb2_queue_setup,
	.buf_prepare		 = rot_vb2_buf_prepare,
	.buf_queue		 = rot_vb2_buf_queue,
	.wait_finish		 = rot_vb2_lock,
	.wait_prepare		 = rot_vb2_unlock,
	.start_streaming	 = rot_vb2_start_streaming,
	.stop_streaming		 = rot_vb2_stop_streaming,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct rot_ctx *ctx = priv;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	src_vq->ops = &rot_vb2_ops;
	src_vq->mem_ops = ctx->rot_dev->vb2->ops;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	dst_vq->ops = &rot_vb2_ops;
	dst_vq->mem_ops = ctx->rot_dev->vb2->ops;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);

	return vb2_queue_init(dst_vq);
}

static int rot_open(struct file *file)
{
	struct rot_dev *rot = video_drvdata(file);
	struct rot_ctx *ctx = NULL;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx) {
		rot_err("no memory for open context\n");
		return -ENOMEM;
	}

	atomic_inc(&rot->m2m.in_use);

	file->private_data = ctx;
	ctx->rot_dev = rot;
	/* Default color format */
	ctx->s_frame.rot_fmt = &rot_formats[0];
	ctx->d_frame.rot_fmt = &rot_formats[0];
	spin_lock_init(&ctx->slock);

	/* Setup the device context for mem2mem mode. */
	ctx->m2m_ctx = v4l2_m2m_ctx_init(rot->m2m.m2m_dev, ctx, queue_init);
	if (IS_ERR(ctx->m2m_ctx)) {
		kfree(ctx);
		atomic_dec(&rot->m2m.in_use);
		return PTR_ERR(ctx->m2m_ctx);
	}

	return 0;
}

static int rot_release(struct file *file)
{
	struct rot_ctx *ctx = file->private_data;
	struct rot_dev *rot = ctx->rot_dev;

	rot_dbg("refcnt= %d", atomic_read(&rot->m2m.in_use));

	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	kfree(ctx);

	atomic_dec(&rot->m2m.in_use);

	return 0;
}

static unsigned int rot_poll(struct file *file,
			     struct poll_table_struct *wait)
{
	struct rot_ctx *ctx = file->private_data;

	return v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
}

static int rot_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct rot_ctx *ctx = file->private_data;

	return v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
}

static const struct v4l2_file_operations rot_v4l2_fops = {
	.owner		= THIS_MODULE,
	.open		= rot_open,
	.release	= rot_release,
	.poll		= rot_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= rot_mmap,
};

void rot_work(struct work_struct *work)
{
	struct rot_dev *rot = container_of(work, struct rot_dev, ws);
	struct rot_ctx *ctx;
	unsigned long flags;
	struct vb2_buffer *src_vb, *dst_vb;

	spin_lock_irqsave(&rot->slock, flags);

	if (atomic_read(&rot->wdt.cnt) >= ROT_WDT_CNT) {
		rot_dbg("wakeup blocked process\n");
		ctx = v4l2_m2m_get_curr_priv(rot->m2m.m2m_dev);
		if (!ctx || !ctx->m2m_ctx) {
			rot_err("current ctx is NULL\n");
			goto wq_unlock;
		}
		src_vb = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		dst_vb = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);

		if (src_vb && dst_vb) {
			v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_ERROR);
			v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_ERROR);

			v4l2_m2m_job_finish(rot->m2m.m2m_dev, ctx->m2m_ctx);
		}
		rot->m2m.ctx = NULL;
		atomic_set(&rot->wdt.cnt, 0);
		clear_bit(DEV_RUN, &rot->state);
		clear_bit(CTX_RUN, &ctx->flags);
	}

wq_unlock:
	spin_unlock_irqrestore(&rot->slock, flags);

	pm_runtime_put(&rot->pdev->dev);
}

void rot_watchdog(unsigned long arg)
{
	struct rot_dev *rot = (struct rot_dev *)arg;

	rot_dbg("timeout watchdog\n");
	if (test_bit(DEV_RUN, &rot->state)) {
		atomic_inc(&rot->wdt.cnt);
		rot_err("rotator is still running\n");
		rot->wdt.timer.expires = jiffies + ROT_TIMEOUT;
		add_timer(&rot->wdt.timer);
	} else {
		rot_dbg("rotator finished job\n");
	}

	if (atomic_read(&rot->wdt.cnt) >= ROT_WDT_CNT)
		queue_work(rot->wq, &rot->ws);
}

static irqreturn_t rot_irq_handler(int irq, void *priv)
{
	struct rot_dev *rot = priv;
	struct rot_ctx *ctx;
	struct vb2_buffer *src_vb, *dst_vb;
	unsigned int irq_src;

	spin_lock(&rot->slock);

	clear_bit(DEV_RUN, &rot->state);
	if (timer_pending(&rot->wdt.timer))
		del_timer(&rot->wdt.timer);

	rot_hwget_irq_src(rot, &irq_src);
	rot_hwset_irq_clear(rot, &irq_src);

	if (irq_src != ISR_PEND_DONE) {
		rot_err("####################\n");
		rot_err("set SFR illegally\n");
		rot_err("maybe the result is wrong\n");
		rot_err("####################\n");
		rot_dump_register(rot);
	}

	ctx = v4l2_m2m_get_curr_priv(rot->m2m.m2m_dev);
	if (!ctx || !ctx->m2m_ctx) {
		rot_err("current ctx is NULL\n");
		goto isr_unlock;
	}

	clear_bit(CTX_RUN, &ctx->flags);
	rot->m2m.ctx = NULL;

	src_vb = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);

	if (src_vb && dst_vb) {
		v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_DONE);
		v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_DONE);

		if (test_bit(DEV_SUSPEND, &rot->state)) {
			rot_dbg("wake up blocked process by suspend\n");
			wake_up(&rot->irq.wait);
		} else {
			v4l2_m2m_job_finish(rot->m2m.m2m_dev, ctx->m2m_ctx);
		}

		/* Wake up from CTX_ABORT state */
		if (test_and_clear_bit(CTX_ABORT, &ctx->flags))
			wake_up(&rot->irq.wait);

		queue_work(rot->wq, &rot->ws);
	} else {
		rot_err("failed to get the buffer done\n");
	}

isr_unlock:
	spin_unlock(&rot->slock);

	return IRQ_HANDLED;
}

void rot_get_bufaddr(struct rot_dev *rot, struct vb2_buffer *vb,
		     struct rot_frame *frame, struct rot_addr *addr)
{
	unsigned int pix_size;

	pix_size = frame->pix_mp.width * frame->pix_mp.height;

	addr->y = rot->vb2->plane_addr(vb, 0);
	addr->cb = 0;
	addr->cr = 0;

	switch (frame->rot_fmt->nr_comp) {
	case 2:
		if (frame->rot_fmt->num_planes == 1)
			addr->cb = addr->y + pix_size;
		else if (frame->rot_fmt->num_planes == 2)
			addr->cb = rot->vb2->plane_addr(vb, 1);
		break;
	case 3:
		if (frame->rot_fmt->num_planes == 3) {
			addr->cb = rot->vb2->plane_addr(vb, 1);
			addr->cr = rot->vb2->plane_addr(vb, 2);
		}
		break;
	default:
		break;
	}
}

void rot_set_frame_addr(struct rot_ctx *ctx)
{
	struct vb2_buffer *vb;
	struct rot_frame *s_frame, *d_frame;
	struct rot_dev *rot = ctx->rot_dev;

	s_frame = &ctx->s_frame;
	d_frame = &ctx->d_frame;

	/* set source buffer address */
	vb = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	rot_get_bufaddr(rot, vb, s_frame, &s_frame->addr);

	rot_hwset_src_addr(rot, s_frame->addr.y, ROT_ADDR_Y);
	rot_hwset_src_addr(rot, s_frame->addr.cb, ROT_ADDR_CB);
	rot_hwset_src_addr(rot, s_frame->addr.cr, ROT_ADDR_CR);

	/* set destination buffer address */
	vb = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	rot_get_bufaddr(rot, vb, d_frame, &d_frame->addr);

	rot_hwset_dst_addr(rot, d_frame->addr.y, ROT_ADDR_Y);
	rot_hwset_dst_addr(rot, d_frame->addr.cb, ROT_ADDR_CB);
	rot_hwset_dst_addr(rot, d_frame->addr.cr, ROT_ADDR_CR);
}

void rot_mapping_flip(struct rot_ctx *ctx, u32 *degree, u32 *flip)
{
	*degree = ctx->rotation;
	*flip = ctx->flip;

	if (ctx->flip == (ROT_VFLIP | ROT_HFLIP)) {
		*flip = ROT_NOFLIP;
		switch (ctx->rotation) {
		case 0:
			*degree = 180;
			break;
		case 90:
			*degree = 270;
			break;
		case 180:
			*degree = 0;
			break;
		case 270:
			*degree = 90;
			break;
		}
	}
}

static void rot_m2m_device_run(void *priv)
{
	struct rot_ctx *ctx = priv;
	struct rot_frame *s_frame, *d_frame;
	struct rot_dev *rot;
	unsigned long flags, tmp;
	u32 degree = 0, flip = 0;

	spin_lock_irqsave(&ctx->slock, flags);

	rot = ctx->rot_dev;

	if (test_bit(DEV_RUN, &rot->state)) {
		rot_err("Rotate is already in progress\n");
		goto run_unlock;
	}

	if (test_bit(DEV_SUSPEND, &rot->state)) {
		rot_err("Rotate is in suspend state\n");
		goto run_unlock;
	}

	if (test_bit(CTX_ABORT, &ctx->flags)) {
		rot_dbg("aborted rot device run\n");
		goto run_unlock;
	}

	pm_runtime_get_sync(&ctx->rot_dev->pdev->dev);

	if (rot->m2m.ctx != ctx)
		rot->m2m.ctx = ctx;

	s_frame = &ctx->s_frame;
	d_frame = &ctx->d_frame;

	/* Configuration rotator registers */
	rot_hwset_image_format(rot, s_frame->rot_fmt->pixelformat);
	rot_mapping_flip(ctx, &degree, &flip);
	rot_hwset_flip(rot, flip);
	rot_hwset_rotation(rot, degree);

	if (ctx->rotation == 90 || ctx->rotation == 270) {
		tmp                     = d_frame->pix_mp.height;
		d_frame->pix_mp.height  = d_frame->pix_mp.width;
		d_frame->pix_mp.width   = tmp;
	}

	rot_hwset_src_imgsize(rot, s_frame);
	rot_hwset_dst_imgsize(rot, d_frame);

	rot_hwset_src_crop(rot, &s_frame->crop);
	rot_hwset_dst_crop(rot, &d_frame->crop);

	rot_set_frame_addr(ctx);

	/* Enable rotator interrupt */
	rot_hwset_irq_frame_done(rot, 1);
	rot_hwset_irq_illegal_config(rot, 1);

	set_bit(DEV_RUN, &rot->state);
	set_bit(CTX_RUN, &ctx->flags);

	/* Start rotate operation */
	rot_hwset_start(rot);

	/* Start watchdog timer */
	rot->wdt.timer.expires = jiffies + ROT_TIMEOUT;
	if (timer_pending(&rot->wdt.timer) == 0)
		add_timer(&rot->wdt.timer);
	else
		mod_timer(&rot->wdt.timer, rot->wdt.timer.expires);

run_unlock:
	spin_unlock_irqrestore(&ctx->slock, flags);
}

static void rot_m2m_job_abort(void *priv)
{
	struct rot_ctx *ctx = priv;
	struct rot_dev *rot = ctx->rot_dev;
	int ret;

	ret = rot_ctx_stop_req(ctx);
	if (ret < 0)
		rot_err("wait timeout\n");

	v4l2_m2m_get_next_job(rot->m2m.m2m_dev, ctx->m2m_ctx);
}

static struct v4l2_m2m_ops rot_m2m_ops = {
	.device_run	= rot_m2m_device_run,
	.job_abort	= rot_m2m_job_abort,
};

static int rot_register_m2m_device(struct rot_dev *rot)
{
	struct v4l2_device *v4l2_dev;
	struct platform_device *pdev;
	struct video_device *vfd;
	int ret = 0;

	if (!rot)
		return -ENODEV;

	pdev = rot->pdev;
	v4l2_dev = &rot->m2m.v4l2_dev;

	/* Set name to "device name.m2m" if it is empty */
	if (!v4l2_dev->name[0])
		snprintf(v4l2_dev->name, sizeof(v4l2_dev->name),
			"%s.m2m", dev_name(&pdev->dev));

	ret = v4l2_device_register(&pdev->dev, v4l2_dev);
	if (ret) {
		rot_err("failed to register v4l2 device\n");
		return ret;
	}

	vfd = video_device_alloc();
	if (!vfd) {
		rot_err("failed to allocate video device\n");
		goto err_v4l2_dev;
	}

	vfd->fops	= &rot_v4l2_fops;
	vfd->ioctl_ops	= &rot_v4l2_ioctl_ops;
	vfd->release	= video_device_release;

	video_set_drvdata(vfd, rot);
	platform_set_drvdata(pdev, rot);

	rot->m2m.vfd = vfd;
	rot->m2m.m2m_dev = v4l2_m2m_init(&rot_m2m_ops);
	if (IS_ERR(rot->m2m.m2m_dev)) {
		rot_err("failed to initialize v4l2-m2m device\n");
		ret = PTR_ERR(rot->m2m.m2m_dev);
		goto err_dev_alloc;
	}

	ret = video_register_device(vfd, VFL_TYPE_GRABBER,
						EXYNOS_VIDEONODE_ROTATOR);
	if (ret) {
		rot_err("failed to register video device\n");
		goto err_m2m_dev;
	}

	return 0;

err_m2m_dev:
	v4l2_m2m_release(rot->m2m.m2m_dev);
err_dev_alloc:
	video_device_release(rot->m2m.vfd);
err_v4l2_dev:
	v4l2_device_unregister(v4l2_dev);

	return ret;
}

static int rot_suspend(struct device *dev)
{
	struct platform_device *pdev;
	struct rot_dev *rot;
	struct rot_ctx *ctx;
	int ret = 0;

	pdev = to_platform_device(dev);
	rot = (struct rot_dev *)platform_get_drvdata(pdev);

	set_bit(DEV_SUSPEND, &rot->state);

	ret = wait_event_timeout(rot->irq.wait,
		!test_bit(DEV_RUN, &rot->state),
		ROT_TIMEOUT);
	if (!ret)
		rot_err("wait timeout\n");

	ctx = rot->m2m.ctx;
	if (ctx != NULL)
		set_bit(CTX_SUSPEND, &ctx->flags);

	return ret;
}

static int rot_resume(struct device *dev)
{
	struct platform_device *pdev;
	struct rot_dev *rot;
	struct rot_ctx *ctx;

	pdev = to_platform_device(dev);
	rot = (struct rot_dev *)platform_get_drvdata(pdev);

	clear_bit(DEV_SUSPEND, &rot->state);

	ctx = rot->m2m.ctx;
	if (ctx != NULL) {
		clear_bit(CTX_SUSPEND, &ctx->flags);
		rot->m2m.ctx = NULL;
		v4l2_m2m_job_finish(rot->m2m.m2m_dev, ctx->m2m_ctx);
	}

	return 0;
}

static int rot_runtime_suspend(struct device *dev)
{
	struct rot_dev *rot;
	struct platform_device *pdev;

	pdev = to_platform_device(dev);
	rot = (struct rot_dev *)platform_get_drvdata(pdev);

	rot->vb2->suspend(rot->alloc_ctx);

	clk_disable(rot->clock);

	return 0;
}

static int rot_runtime_resume(struct device *dev)
{
	struct rot_dev *rot;
	struct platform_device *pdev;

	pdev = to_platform_device(dev);
	rot = (struct rot_dev *)platform_get_drvdata(pdev);

	clk_enable(rot->clock);

	rot->vb2->resume(rot->alloc_ctx);

	return 0;
}

static const struct dev_pm_ops rot_pm_ops = {
	.suspend		= rot_suspend,
	.resume			= rot_resume,
	.runtime_suspend	= rot_runtime_suspend,
	.runtime_resume		= rot_runtime_resume,
};

static int rot_probe(struct platform_device *pdev)
{
	struct exynos_rot_driverdata *drv_data;
	struct rot_dev *rot;
	struct resource *res;
	int variant_num, ret = 0;

	printk(KERN_INFO "++%s\n", __func__);

	drv_data = (struct exynos_rot_driverdata *)
			platform_get_device_id(pdev)->driver_data;

	if (pdev->id >= drv_data->nr_dev) {
		pr_err("Invalid platform device id\n");
		return -EINVAL;
	}

	rot = kzalloc(sizeof(struct rot_dev), GFP_KERNEL);
	if (!rot) {
		pr_err("no memory for rotator device\n");
		return -ENOMEM;
	}

	rot->pdev = pdev;
	rot->id = pdev->id;
	variant_num = (rot->id < 0) ? 0 : rot->id;
	rot->variant = drv_data->variant[variant_num];

	spin_lock_init(&rot->slock);

	/* Get memory resource and map SFR region. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		rot_err("failed to find the registers\n");
		ret = -ENOENT;
		goto err_dev;
	}

	rot->regs_res = request_mem_region(res->start, resource_size(res),
			dev_name(&pdev->dev));
	if (!rot->regs_res) {
		rot_err("failed to claim register region\n");
		ret = -ENOENT;
		goto err_dev;
	}

	rot->regs = ioremap(res->start, resource_size(res));
	if (!rot->regs) {
		rot_err("failed to map register\n");
		ret = -ENXIO;
		goto err_req_region;
	}

	/* Get IRQ resource and register IRQ handler. */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		rot_err("failed to get IRQ resource\n");
		ret = -ENXIO;
		goto err_ioremap;
	}
	rot->irq.num = res->start;

	ret = request_irq(rot->irq.num, rot_irq_handler, 0,
			pdev->name, rot);
	if (ret) {
		rot_err("failed to install irq(%d)\n", ret);
		goto err_ioremap;
	}

	rot->wq = create_singlethread_workqueue(MODULE_NAME);
	if (rot->wq == NULL) {
		rot_err("failed to create workqueue for rotator\n");
		goto err_irq;
	}
	INIT_WORK(&rot->ws, rot_work);

	atomic_set(&rot->wdt.cnt, 0);
	setup_timer(&rot->wdt.timer, rot_watchdog, (unsigned long)rot);

	rot->clock = clk_get(&rot->pdev->dev, "rotator");
	if (IS_ERR(rot->clock))
		goto err_wq;

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
	rot->vb2 = &rot_vb2_cma;
#elif defined(CONFIG_VIDEOBUF2_ION)
	rot->vb2 = &rot_vb2_ion;
#endif

	rot->alloc_ctx = rot->vb2->init(rot);
	ret = rot_register_m2m_device(rot);
	if (ret)
		goto err_irq;

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_enable(&pdev->dev);
#else
	rot_runtime_resume(&pdev->dev);
#endif

	rot_info("rotator registered successfully\n");
	printk(KERN_INFO "--%s\n", __func__);

	return 0;

err_wq:
	destroy_workqueue(rot->wq);
err_irq:
	free_irq(rot->irq.num, rot);
err_ioremap:
	iounmap(rot->regs);
err_req_region:
	release_mem_region(rot->regs_res->start,
			resource_size(rot->regs_res));
err_dev:
	kfree(rot);

	return ret;
}

static int rot_remove(struct platform_device *pdev)
{
	struct rot_dev *rot = (struct rot_dev *)platform_get_drvdata(pdev);

	free_irq(rot->irq.num, rot);
	clk_put(rot->clock);
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_disable(&pdev->dev);
#else
	rot_runtime_suspend(&pdev->dev);
#endif

	if (timer_pending(&rot->wdt.timer))
		del_timer(&rot->wdt.timer);

	destroy_workqueue(rot->wq);
	iounmap(rot->regs);
	release_mem_region(rot->regs_res->start,
			resource_size(rot->regs_res));

	kfree(rot);

	return 0;
}

struct exynos_rot_variant rot_variant_exynos = {
	.limit_rgb565 = {
		.min_x = 16,
		.min_y = 16,
		.max_x = SZ_16K,
		.max_y = SZ_16K,
		.align = 2,
	},
	.limit_rgb888 = {
		.min_x = 8,
		.min_y = 8,
		.max_x = SZ_8K,
		.max_y = SZ_8K,
		.align = 2,
	},
	.limit_yuv422 = {
		.min_x = 16,
		.min_y = 16,
		.max_x = SZ_16K,
		.max_y = SZ_16K,
		.align = 2,
	},
	.limit_yuv420_2p = {
		.min_x = 32,
		.min_y = 32,
		.max_x = SZ_32K,
		.max_y = SZ_32K,
		.align = 3,
	},
	.limit_yuv420_3p = {
		.min_x = 64,
		.min_y = 32,
		.max_x = SZ_32K,
		.max_y = SZ_32K,
		.align = 4,
	},
};

static struct exynos_rot_driverdata rot_drvdata_exynos = {
	.variant = {
		[0] = &rot_variant_exynos,
	},
	.nr_dev = 1,
};

static struct platform_device_id rot_driver_ids[] = {
	{
		.name		= "exynos-rot",
		.driver_data	= (unsigned long)&rot_drvdata_exynos,
	},
	{},
};

static struct platform_driver rot_driver = {
	.probe		= rot_probe,
	.remove		= rot_remove,
	.id_table	= rot_driver_ids,
	.driver = {
		.name	= MODULE_NAME,
		.owner	= THIS_MODULE,
		.pm	= &rot_pm_ops,
	}
};

static int __init rot_init(void)
{
	int ret = platform_driver_register(&rot_driver);
	if (ret)
		pr_err("platform driver register failed\n");
	return ret;
}

static void __exit rot_exit(void)
{
	platform_driver_unregister(&rot_driver);
}

module_init(rot_init);
module_exit(rot_exit);

MODULE_AUTHOR("Sunyoung, Kang <sy0816.kang@samsung.com>");
MODULE_AUTHOR("Ayoung, Sim <a.sim@samsung.com>");
MODULE_DESCRIPTION("Exynos Image Rotator driver");
MODULE_LICENSE("GPL");
