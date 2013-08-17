/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Core file for Samsung EXYNOS Scaler driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-ioctl.h>
#include <mach/videonode.h>
#include <mach/exynos-scaler.h>
#include <plat/sysmmu.h>
#include <plat/cpu.h>

#include "scaler.h"

int sc_log_level;
module_param_named(sc_log_level, sc_log_level, uint, 0644);

static struct sc_fmt sc_formats[] = {
	{
		.name		= "RGB565",
		.pixelformat	= V4L2_PIX_FMT_RGB565,
		.num_planes	= 1,
		.num_comp	= 1,
		.bitperpixel	= { 16 },
		.color		= SC_COLOR_RGB,
	}, {
		.name		= "RGB1555",
		.pixelformat	= V4L2_PIX_FMT_RGB555X,
		.num_planes	= 1,
		.num_comp	= 1,
		.bitperpixel	= { 16 },
		.color		= SC_COLOR_RGB,
	}, {
		.name		= "ARGB4444",
		.pixelformat	= V4L2_PIX_FMT_RGB444,
		.num_planes	= 1,
		.num_comp	= 1,
		.bitperpixel	= { 16 },
		.color		= SC_COLOR_RGB,
	}, {
		.name		= "ARGB8888",
		.pixelformat	= V4L2_PIX_FMT_RGB32,
		.num_planes	= 1,
		.num_comp	= 1,
		.bitperpixel	= { 32 },
		.color		= SC_COLOR_RGB,
	}, {
		.name		= "BGRA8888",
		.pixelformat	= V4L2_PIX_FMT_BGR32,
		.num_planes	= 1,
		.num_comp	= 1,
		.bitperpixel	= { 32 },
		.color		= SC_COLOR_RGB,
	}, {
		.name		= "YUV 4:2:2 packed, YCbYCr",
		.pixelformat	= V4L2_PIX_FMT_YUYV,
		.num_planes	= 1,
		.num_comp	= 1,
		.bitperpixel	= { 16 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YUV 4:2:2 packed, CbYCrY",
		.pixelformat	= V4L2_PIX_FMT_UYVY,
		.num_planes	= 1,
		.num_comp	= 1,
		.bitperpixel	= { 16 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YUV 4:2:2 packed, YCrYCb",
		.pixelformat	= V4L2_PIX_FMT_YVYU,
		.num_planes	= 1,
		.num_comp	= 1,
		.bitperpixel	= { 16 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YUV 4:2:0 contiguous 2-planar, Y/CbCr",
		.pixelformat	= V4L2_PIX_FMT_NV12,
		.num_planes	= 1,
		.num_comp	= 2,
		.bitperpixel	= { 12 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YUV 4:2:0 contiguous 2-planar, Y/CrCb",
		.pixelformat	= V4L2_PIX_FMT_NV21,
		.num_planes	= 1,
		.num_comp	= 2,
		.bitperpixel	= { 12 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YUV 4:2:0 non-contiguous 2-planar, Y/CbCr",
		.pixelformat	= V4L2_PIX_FMT_NV12M,
		.num_planes	= 2,
		.num_comp	= 2,
		.bitperpixel	= { 8, 4 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YUV 4:2:0 non-contiguous 2-planar, Y/CrCb",
		.pixelformat	= V4L2_PIX_FMT_NV21M,
		.num_planes	= 2,
		.num_comp	= 2,
		.bitperpixel	= { 8, 4 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YUV 4:2:0 non-contiguous 2-planar, Y/CbCr, tiled",
		.pixelformat	= V4L2_PIX_FMT_NV12MT_16X16,
		.num_planes	= 2,
		.num_comp	= 2,
		.bitperpixel	= { 8, 4 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YUV 4:2:2 contiguous 2-planar, Y/CbCr",
		.pixelformat	= V4L2_PIX_FMT_NV16,
		.num_planes	= 1,
		.num_comp	= 2,
		.bitperpixel	= { 16 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YUV 4:2:2 contiguous 2-planar, Y/CrCb",
		.pixelformat	= V4L2_PIX_FMT_NV61,
		.num_planes	= 1,
		.num_comp	= 2,
		.bitperpixel	= { 16 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YUV 4:4:4 contiguous Y/CbCr",
		.pixelformat	= V4L2_PIX_FMT_NV24,
		.num_planes	= 1,
		.num_comp	= 2,
		.bitperpixel	= { 24 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YUV 4:4:4 contiguous Y/CrCb",
		.pixelformat	= V4L2_PIX_FMT_NV42,
		.num_planes	= 1,
		.num_comp	= 2,
		.bitperpixel	= { 24 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YUV 4:2:0 contiguous 3-planar, Y/Cb/Cr",
		.pixelformat	= V4L2_PIX_FMT_YUV420,
		.num_planes	= 1,
		.num_comp	= 3,
		.bitperpixel	= { 12 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YUV 4:2:0 non-contiguous 3-planar, Y/Cb/Cr",
		.pixelformat	= V4L2_PIX_FMT_YUV420M,
		.num_planes	= 3,
		.num_comp	= 3,
		.bitperpixel	= {8, 2, 2 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YVU 4:2:0 contiguous 3-planar, Y/Cb/Cr",
		.pixelformat	= V4L2_PIX_FMT_YVU420,
		.num_planes	= 1,
		.num_comp	= 3,
		.bitperpixel	= { 12 },
		.color		= SC_COLOR_YUV,
	}, {
		.name		= "YVU 4:2:0 non-contiguous 3-planar, Y/Cb/Cr",
		.pixelformat	= V4L2_PIX_FMT_YVU420M,
		.num_planes	= 3,
		.num_comp	= 3,
		.bitperpixel	= {8, 2, 2 },
		.color		= SC_COLOR_YUV,
	},
};

static struct sc_variant variant_5a = {
	.limit_input = {
		.min_w		= 16,
		.min_h		= 16,
		.max_w		= 16384,
		.max_h		= 16384,
		.align_w	= 0,
		.align_h	= 0,
	},
	.limit_output = {
		.min_w		= 4,
		.min_h		= 4,
		.max_w		= 4096,
		.max_h		= 4096,
		.align_w	= 0,
		.align_h	= 0,
	},
	.sc_up_max		= 16,
	.sc_down_max		= 4,
};

static struct sc_variant variant = {
	.limit_input = {
		.min_w		= 16,
		.min_h		= 16,
		.max_w		= 8192,
		.max_h		= 8192,
		.align_w	= 0,
		.align_h	= 0,
	},
	.limit_output = {
		.min_w		= 4,
		.min_h		= 4,
		.max_w		= 8192,
		.max_h		= 8192,
		.align_w	= 0,
		.align_h	= 0,
	},
	.sc_up_max		= 16,
	.sc_down_max		= 4,
};

/* Find the matches format */
static struct sc_fmt *sc_find_format(struct sc_dev *sc, struct v4l2_format *f)
{
	struct sc_fmt *sc_fmt;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sc_formats); ++i) {
		sc_fmt = &sc_formats[i];
		if (sc_fmt->pixelformat == f->fmt.pix_mp.pixelformat) {
			if (sc_ver_is_5a(sc) && (sc_fmt->num_planes == 3 ||
				sc_fmt->pixelformat == V4L2_PIX_FMT_NV12MT_16X16))
				return NULL;
			if (!V4L2_TYPE_IS_OUTPUT(f->type) &&
				sc_fmt->pixelformat == V4L2_PIX_FMT_NV12MT_16X16)
				return NULL;
			if (!sc_ver_is_5a(sc) &&
				sc_fmt->pixelformat == V4L2_PIX_FMT_BGR32)
				return NULL;
			else
				return &sc_formats[i];
		}
	}

	return NULL;
}

static int sc_v4l2_querycap(struct file *file, void *fh,
			     struct v4l2_capability *cap)
{
	strncpy(cap->driver, MODULE_NAME, sizeof(cap->driver) - 1);
	strncpy(cap->card, MODULE_NAME, sizeof(cap->card) - 1);

	cap->bus_info[0] = 0;
	cap->capabilities = V4L2_CAP_STREAMING |
		V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE;

	return 0;
}

static int sc_v4l2_enum_fmt_mplane(struct file *file, void *fh,
			     struct v4l2_fmtdesc *f)
{
	struct sc_fmt *sc_fmt;

	if (f->index >= ARRAY_SIZE(sc_formats))
		return -EINVAL;

	sc_fmt = &sc_formats[f->index];
	strncpy(f->description, sc_fmt->name, sizeof(f->description) - 1);
	f->pixelformat = sc_fmt->pixelformat;

	return 0;
}

static int sc_v4l2_g_fmt_mplane(struct file *file, void *fh,
			  struct v4l2_format *f)
{
	struct sc_ctx *ctx = fh_to_sc_ctx(fh);
	struct sc_fmt *sc_fmt;
	struct sc_frame *frame;
	struct v4l2_pix_format_mplane *pixm = &f->fmt.pix_mp;
	int i;

	frame = ctx_get_frame(ctx, f->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	sc_fmt = frame->sc_fmt;

	pixm->width		= frame->pix_mp.width;
	pixm->height		= frame->pix_mp.height;
	pixm->pixelformat	= frame->pix_mp.pixelformat;
	pixm->field		= V4L2_FIELD_NONE;
	pixm->num_planes	= frame->sc_fmt->num_planes;
	pixm->colorspace	= 0;

	for (i = 0; i < pixm->num_planes; ++i) {
		pixm->plane_fmt[i].bytesperline = (pixm->width *
				sc_fmt->bitperpixel[i]) >> 3;
		pixm->plane_fmt[i].sizeimage = pixm->plane_fmt[i].bytesperline
				* pixm->height;

		v4l2_dbg(1, sc_log_level, &ctx->sc_dev->m2m.v4l2_dev,
				"[%d] plane: bytesperline %d, sizeimage %d\n",
				i, pixm->plane_fmt[i].bytesperline,
				pixm->plane_fmt[i].sizeimage);
	}

	return 0;
}

static int sc_v4l2_try_fmt_mplane(struct file *file, void *fh,
			    struct v4l2_format *f)
{
	struct sc_ctx *ctx = fh_to_sc_ctx(fh);
	struct sc_fmt *sc_fmt;
	struct v4l2_pix_format_mplane *pixm = &f->fmt.pix_mp;
	struct sc_size_limit *limit;
	int i;

	if (!V4L2_TYPE_IS_MULTIPLANAR(f->type)) {
		v4l2_err(&ctx->sc_dev->m2m.v4l2_dev,
				"not supported v4l2 type\n");
		return -EINVAL;
	}

	sc_fmt = sc_find_format(ctx->sc_dev, f);
	if (!sc_fmt) {
		v4l2_err(&ctx->sc_dev->m2m.v4l2_dev,
				"not supported format type\n");
		return -EINVAL;
	}

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		limit = &ctx->sc_dev->variant->limit_input;
		/* rotation max source size is 4Kx4K */
		if (sc_ver_is_5a(ctx->sc_dev) &&
			(ctx->rotation == 90 || ctx->rotation == 270)) {
			limit->max_w = 4096;
			limit->max_h = 4096;
		}
	} else {
		limit = &ctx->sc_dev->variant->limit_output;
	}

	/*
	 * Y_SPAN - should even in interleaved YCbCr422
	 * C_SPAN - should even in YCbCr420 and YCbCr422
	 */
	if (sc_fmt_is_yuv422(sc_fmt->pixelformat) ||
			sc_fmt_is_yuv420(sc_fmt->pixelformat))
		limit->align_w = 1;

	/* Bound an image to have width and height in limit */
	v4l_bound_align_image(&pixm->width, limit->min_w, limit->max_w,
			limit->align_w, &pixm->height, limit->min_h,
			limit->max_h, limit->align_h, 0);

	pixm->num_planes = sc_fmt->num_planes;
	pixm->colorspace = 0;

	for (i = 0; i < pixm->num_planes; ++i) {
		pixm->plane_fmt[i].bytesperline = (pixm->width *
				sc_fmt->bitperpixel[i]) >> 3;
		pixm->plane_fmt[i].sizeimage = pixm->plane_fmt[i].bytesperline
				* pixm->height;

		v4l2_dbg(1, sc_log_level, &ctx->sc_dev->m2m.v4l2_dev,
				"[%d] plane: bytesperline %d, sizeimage %d\n",
				i, pixm->plane_fmt[i].bytesperline,
				pixm->plane_fmt[i].sizeimage);
	}

	return 0;
}

static int sc_v4l2_s_fmt_mplane(struct file *file, void *fh,
				 struct v4l2_format *f)

{
	struct sc_ctx *ctx = fh_to_sc_ctx(fh);
	struct vb2_queue *vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	struct sc_frame *frame;
	struct v4l2_pix_format_mplane *pixm = &f->fmt.pix_mp;
	int i, ret = 0;

	if (vb2_is_streaming(vq)) {
		v4l2_err(&ctx->sc_dev->m2m.v4l2_dev, "device is busy\n");
		return -EBUSY;
	}

	ret = sc_v4l2_try_fmt_mplane(file, fh, f);
	if (ret < 0)
		return ret;

	frame = ctx_get_frame(ctx, f->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	set_bit(CTX_PARAMS, &ctx->flags);

	frame->sc_fmt = sc_find_format(ctx->sc_dev, f);
	if (!frame->sc_fmt) {
		v4l2_err(&ctx->sc_dev->m2m.v4l2_dev,
				"not supported format values\n");
		return -EINVAL;
	}

	frame->pix_mp.pixelformat = pixm->pixelformat;
	frame->pix_mp.width	= pixm->width;
	frame->pix_mp.height	= pixm->height;

	/*
	 * Shouldn't call s_crop or g_crop before called g_fmt or s_fmt.
	 * Let's assume that we can keep the order.
	 */
	frame->crop.width	= pixm->width;
	frame->crop.height	= pixm->height;

	for (i = 0; i < frame->sc_fmt->num_planes; ++i)
		frame->bytesused[i] = (pixm->width * pixm->height *
				frame->sc_fmt->bitperpixel[i]) >> 3;

	return 0;
}

static int sc_v4l2_reqbufs(struct file *file, void *fh,
			    struct v4l2_requestbuffers *reqbufs)
{
	struct sc_ctx *ctx = fh_to_sc_ctx(fh);
	struct sc_dev *sc = ctx->sc_dev;

	sc->vb2->set_cacheable(sc->alloc_ctx, ctx->cacheable);

	return v4l2_m2m_reqbufs(file, ctx->m2m_ctx, reqbufs);
}

static int sc_v4l2_querybuf(struct file *file, void *fh,
			     struct v4l2_buffer *buf)
{
	struct sc_ctx *ctx = fh_to_sc_ctx(fh);
	return v4l2_m2m_querybuf(file, ctx->m2m_ctx, buf);
}

static int sc_v4l2_qbuf(struct file *file, void *fh,
			 struct v4l2_buffer *buf)
{
	struct sc_ctx *ctx = fh_to_sc_ctx(fh);

	if (sc_ver_is_5a(ctx->sc_dev)) {
		/*
		 * Exynos5410 scaler reads more than source image size
		 * when rotation and width size not aligned 64 bytes.
		 * To resolve this, increase plane length.
		 */
		if (V4L2_TYPE_IS_OUTPUT(buf->type) &&
				(ctx->rotation == 90 || ctx->rotation == 270)) {
			struct sc_frame *frame;
			frame = ctx_get_frame(ctx, buf->type);
			if (frame->pix_mp.width % 32) {
				int i;
				for (i = 0; i < buf->length; i++)
					buf->m.planes[i].length += 64;
				sc_dbg("increase plane length to 0x%x\n",
						buf->m.planes[0].length);
			}
		}
	}

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int sc_v4l2_dqbuf(struct file *file, void *fh,
			  struct v4l2_buffer *buf)
{
	struct sc_ctx *ctx = fh_to_sc_ctx(fh);
	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}

static int sc_v4l2_streamon(struct file *file, void *fh,
			     enum v4l2_buf_type type)
{
	struct sc_ctx *ctx = fh_to_sc_ctx(fh);
	return v4l2_m2m_streamon(file, ctx->m2m_ctx, type);
}

static int sc_v4l2_streamoff(struct file *file, void *fh,
			      enum v4l2_buf_type type)
{
	struct sc_ctx *ctx = fh_to_sc_ctx(fh);
	return v4l2_m2m_streamoff(file, ctx->m2m_ctx, type);
}

static int sc_v4l2_cropcap(struct file *file, void *fh,
			    struct v4l2_cropcap *cr)
{
	struct sc_ctx *ctx = fh_to_sc_ctx(fh);
	struct sc_frame *frame;

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

static int sc_v4l2_g_crop(struct file *file, void *fh, struct v4l2_crop *cr)
{
	struct sc_ctx *ctx = fh_to_sc_ctx(fh);
	struct sc_frame *frame;

	frame = ctx_get_frame(ctx, cr->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	cr->c = frame->crop;

	return 0;
}

static int sc_v4l2_s_crop(struct file *file, void *fh, struct v4l2_crop *cr)
{
	struct sc_ctx *ctx = fh_to_sc_ctx(fh);
	struct sc_dev *sc = ctx->sc_dev;
	struct v4l2_pix_format_mplane *pixm;
	struct sc_frame *frame;
	struct sc_size_limit *limit = NULL;
	int x_align = 0, y_align = 0;
	int i;

	frame = ctx_get_frame(ctx, cr->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	if (!test_bit(CTX_PARAMS, &ctx->flags)) {
		v4l2_err(&ctx->sc_dev->m2m.v4l2_dev,
				"color format is not set\n");
		return -EINVAL;
	}

	if (cr->c.left < 0 || cr->c.top < 0 ||
			cr->c.width < 0 || cr->c.height < 0) {
		v4l2_err(&ctx->sc_dev->m2m.v4l2_dev,
				"crop value is negative\n");
		return -EINVAL;
	}

	pixm = &frame->pix_mp;

	if (V4L2_TYPE_IS_OUTPUT(cr->type)) {
		limit = &sc->variant->limit_input;
		set_bit(CTX_SRC_FMT, &ctx->flags);
	} else {
		limit = &sc->variant->limit_output;
		set_bit(CTX_DST_FMT, &ctx->flags);
	}

	/* Check max scaling ratio */
	if (test_bit(CTX_SRC_FMT, &ctx->flags) &&
			test_bit(CTX_DST_FMT, &ctx->flags)) {
		int down_max, up_max, dw, dh;

		down_max = ctx->sc_dev->variant->sc_down_max;
		up_max = ctx->sc_dev->variant->sc_up_max;

		if (ctx->rotation == 90 || ctx->rotation == 270) {
			dw = cr->c.height;
			dh = cr->c.width;
		} else {
			dw = cr->c.width;
			dh = cr->c.height;
		}

		if (ctx->s_frame.crop.width * up_max < dw ||
			ctx->s_frame.crop.height * up_max < dh ||
			ctx->s_frame.crop.width > dw * down_max ||
			ctx->s_frame.crop.height > dh * down_max) {
			v4l2_err(&ctx->sc_dev->m2m.v4l2_dev,
				"Scaling ratio is out of range\n");
			return -EINVAL;
		}
	}

	if (sc_fmt_is_yuv422(frame->sc_fmt->pixelformat)) {
		limit->align_w = 1;
	} else if (sc_fmt_is_yuv420(frame->sc_fmt->pixelformat)) {
		limit->align_w = 1;
		limit->align_h = 1;
	}

	/* Bound an image to have crop width and height in limit */
	v4l_bound_align_image(&cr->c.width, limit->min_w, limit->max_w,
			limit->align_w, &cr->c.height, limit->min_h,
			limit->max_h, limit->align_h, 0);

	if (V4L2_TYPE_IS_OUTPUT(cr->type)) {
		if (sc_fmt_is_yuv422(frame->sc_fmt->pixelformat))
			x_align = 1;
	} else {
		if (sc_fmt_is_yuv422(frame->sc_fmt->pixelformat)) {
			x_align = 1;
		} else if (sc_fmt_is_yuv420(frame->sc_fmt->pixelformat)) {
			x_align = 1;
			y_align = 1;
		}
	}

	/* Bound an image to have crop position in limit */
	v4l_bound_align_image(&cr->c.left, 0, pixm->width - cr->c.width,
			x_align, &cr->c.top, 0, pixm->height - cr->c.height,
			y_align, 0);

	frame->crop = cr->c;

	for (i = 0; i < frame->sc_fmt->num_planes; ++i)
		frame->bytesused[i] = (cr->c.width * cr->c.height *
				frame->sc_fmt->bitperpixel[i]) >> 3;

	return 0;
}

static const struct v4l2_ioctl_ops sc_v4l2_ioctl_ops = {
	.vidioc_querycap		= sc_v4l2_querycap,

	.vidioc_enum_fmt_vid_cap_mplane	= sc_v4l2_enum_fmt_mplane,
	.vidioc_enum_fmt_vid_out_mplane	= sc_v4l2_enum_fmt_mplane,

	.vidioc_g_fmt_vid_cap_mplane	= sc_v4l2_g_fmt_mplane,
	.vidioc_g_fmt_vid_out_mplane	= sc_v4l2_g_fmt_mplane,

	.vidioc_try_fmt_vid_cap_mplane	= sc_v4l2_try_fmt_mplane,
	.vidioc_try_fmt_vid_out_mplane	= sc_v4l2_try_fmt_mplane,

	.vidioc_s_fmt_vid_cap_mplane	= sc_v4l2_s_fmt_mplane,
	.vidioc_s_fmt_vid_out_mplane	= sc_v4l2_s_fmt_mplane,

	.vidioc_reqbufs			= sc_v4l2_reqbufs,
	.vidioc_querybuf		= sc_v4l2_querybuf,

	.vidioc_qbuf			= sc_v4l2_qbuf,
	.vidioc_dqbuf			= sc_v4l2_dqbuf,

	.vidioc_streamon		= sc_v4l2_streamon,
	.vidioc_streamoff		= sc_v4l2_streamoff,

	.vidioc_g_crop			= sc_v4l2_g_crop,
	.vidioc_s_crop			= sc_v4l2_s_crop,
	.vidioc_cropcap			= sc_v4l2_cropcap
};

static int sc_ctx_stop_req(struct sc_ctx *ctx)
{
	struct sc_ctx *curr_ctx;
	struct sc_dev *sc = ctx->sc_dev;
	int ret = 0;

	curr_ctx = v4l2_m2m_get_curr_priv(sc->m2m.m2m_dev);
	if (!test_bit(CTX_RUN, &ctx->flags) || (curr_ctx != ctx))
		return 0;

	set_bit(CTX_ABORT, &ctx->flags);

	ret = wait_event_timeout(sc->wait,
			!test_bit(CTX_RUN, &ctx->flags), SC_TIMEOUT);

	/* TODO: How to handle case of timeout event */
	if (ret == 0) {
		dev_err(sc->dev, "device failed to stop request\n");
		ret = -EBUSY;
	}

	return ret;
}

static int sc_vb2_queue_setup(struct vb2_queue *vq,
		const struct v4l2_format *fmt, unsigned int *num_buffers,
		unsigned int *num_planes, unsigned int sizes[],
		void *allocators[])
{
	struct sc_ctx *ctx = vb2_get_drv_priv(vq);
	struct sc_frame *frame;
	int i;

	frame = ctx_get_frame(ctx, vq->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	/* Get number of planes from format_list in driver */
	*num_planes = frame->sc_fmt->num_planes;
	for (i = 0; i < frame->sc_fmt->num_planes; i++) {
		sizes[i] = (frame->pix_mp.width * frame->pix_mp.height *
				frame->sc_fmt->bitperpixel[i]) >> 3;
		allocators[i] = ctx->sc_dev->alloc_ctx;

		if (sc_ver_is_5a(ctx->sc_dev)) {
			/*
			 * Exynos5410 scaler reads more than source image size
			 * when width size is not aligned 64 bytes and rotation.
			 * To resolve this, increase plane length.
			 */
			if (V4L2_TYPE_IS_OUTPUT(vq->type) &&
				(ctx->rotation == 90 || ctx->rotation == 270)) {
				if (frame->pix_mp.width % 32) {
					sizes[i] += 64;
					sc_dbg("increase sizes[%d] to 0x%x\n",
							i, sizes[i]);
				}
			}
		}
	}
	vb2_queue_init(vq);

	return 0;
}

static int sc_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct sc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct sc_frame *frame;
	int i;

	frame = ctx_get_frame(ctx, vb->vb2_queue->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	if (!V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {
		for (i = 0; i < frame->sc_fmt->num_planes; i++)
			vb2_set_plane_payload(vb, i, frame->bytesused[i]);
	}

	return sc_buf_sync_prepare(vb);
}

static void sc_fence_work(struct work_struct *work)
{
	struct sc_ctx *ctx = container_of(work, struct sc_ctx, fence_work);
	struct v4l2_m2m_buffer *buffer;
	struct sync_fence *fence;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ctx->slock, flags);

	while (!list_empty(&ctx->fence_wait_list)) {
		buffer = list_first_entry(&ctx->fence_wait_list,
					  struct v4l2_m2m_buffer, wait);
		list_del(&buffer->wait);
		spin_unlock_irqrestore(&ctx->slock, flags);

		fence = buffer->vb.acquire_fence;
		if (fence) {
			buffer->vb.acquire_fence = NULL;
			ret = sync_fence_wait(fence, 1000);
			if (ret == -ETIME) {
				dev_warn(ctx->sc_dev->dev,
					"sync_fence_wait() timeout\n");
				ret = sync_fence_wait(fence, 10 * MSEC_PER_SEC);
			}
			if (ret)
				dev_warn(ctx->sc_dev->dev,
					"sync_fence_wait() error\n");
			sync_fence_put(fence);
		}

		if (ctx->m2m_ctx) {
			v4l2_m2m_buf_queue(ctx->m2m_ctx, &buffer->vb);
			v4l2_m2m_try_schedule(ctx->m2m_ctx);
		}

		spin_lock_irqsave(&ctx->slock, flags);
	}

	spin_unlock_irqrestore(&ctx->slock, flags);
}

static void sc_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct sc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_m2m_buffer *b =
		container_of(vb, struct v4l2_m2m_buffer, vb);
	struct sync_fence *fence;
	unsigned long flags;

	fence = vb->acquire_fence;
	if (fence) {
		spin_lock_irqsave(&ctx->slock, flags);
		list_add_tail(&b->wait, &ctx->fence_wait_list);
		spin_unlock_irqrestore(&ctx->slock, flags);

		queue_work(ctx->fence_wq, &ctx->fence_work);
	} else {
		if (ctx->m2m_ctx)
			v4l2_m2m_buf_queue(ctx->m2m_ctx, vb);
	}
}

static void sc_vb2_lock(struct vb2_queue *vq)
{
	struct sc_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_lock(&ctx->sc_dev->lock);
}

static void sc_vb2_unlock(struct vb2_queue *vq)
{
	struct sc_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_unlock(&ctx->sc_dev->lock);
}

static int sc_vb2_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct sc_ctx *ctx = vb2_get_drv_priv(vq);
	set_bit(CTX_STREAMING, &ctx->flags);

	return 0;
}

static int sc_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct sc_ctx *ctx = vb2_get_drv_priv(vq);
	int ret;

	ret = sc_ctx_stop_req(ctx);
	if (ret < 0)
		dev_err(ctx->sc_dev->dev, "wait timeout\n");

	clear_bit(CTX_STREAMING, &ctx->flags);

	return ret;
}

static struct vb2_ops sc_vb2_ops = {
	.queue_setup		 = sc_vb2_queue_setup,
	.buf_prepare		 = sc_vb2_buf_prepare,
	.buf_finish		 = sc_buf_sync_finish,
	.buf_queue		 = sc_vb2_buf_queue,
	.wait_finish		 = sc_vb2_lock,
	.wait_prepare		 = sc_vb2_unlock,
	.start_streaming	 = sc_vb2_start_streaming,
	.stop_streaming		 = sc_vb2_stop_streaming,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct sc_ctx *ctx = priv;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	src_vq->ops = &sc_vb2_ops;
	src_vq->mem_ops = ctx->sc_dev->vb2->ops;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dst_vq->ops = &sc_vb2_ops;
	dst_vq->mem_ops = ctx->sc_dev->vb2->ops;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);

	return vb2_queue_init(dst_vq);
}

static int sc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sc_ctx *ctx;

	sc_dbg("ctrl ID:%d, value:%d\n", ctrl->id, ctrl->val);
	ctx = container_of(ctrl->handler, struct sc_ctx, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		if (ctrl->val)
			ctx->flip |= SC_VFLIP;
		else
			ctx->flip &= ~SC_VFLIP;
		break;
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			ctx->flip |= SC_HFLIP;
		else
			ctx->flip &= ~SC_HFLIP;
		break;
	case V4L2_CID_ROTATE:
		ctx->rotation = ctrl->val;
		break;
	case V4L2_CID_CACHEABLE:
		ctx->cacheable = (bool)ctrl->val;
		break;
	case V4L2_CID_GLOBAL_ALPHA:
		ctx->g_alpha = ctrl->val;
		break;
	case V4L2_CID_2D_BLEND_OP:
		ctx->bl_op = ctrl->val;
		break;
	case V4L2_CID_2D_COLOR_FILL:
		ctx->color_fill = ctrl->val;
		break;
	case V4L2_CID_2D_FMT_PREMULTI:
		ctx->pre_multi = ctrl->val;
		break;
	case V4L2_CID_2D_DITH:
		ctx->dith = ctrl->val;
		break;
	case V4L2_CID_CSC_EQ:
		ctx->csc.csc_eq = ctrl->val;
		break;
	case V4L2_CID_CSC_RANGE:
		ctx->csc.csc_range = ctrl->val;
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops sc_ctrl_ops = {
	.s_ctrl = sc_s_ctrl,
};

static const struct v4l2_ctrl_config sc_custom_ctrl[] = {
	{
		.ops = &sc_ctrl_ops,
		.id = V4L2_CID_CACHEABLE,
		.name = "set cacheable",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 1,
		.def = true,
	}, {
		.ops = &sc_ctrl_ops,
		.id = V4L2_CID_GLOBAL_ALPHA,
		.name = "Set RGB alpha",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.step = 1,
		.max = 255,
		.def = 0,
	}, {
		.ops = &sc_ctrl_ops,
		.id = V4L2_CID_2D_BLEND_OP,
		.name = "set blend op",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.step = 1,
		.max = BL_OP_ADD,
		.def = 0,
	}, {
		.ops = &sc_ctrl_ops,
		.id = V4L2_CID_2D_COLOR_FILL,
		.name = "set color fill",
		.type = V4L2_CTRL_TYPE_BITMASK,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 0xffffffff,
		.def = 0,
	}, {
		.ops = &sc_ctrl_ops,
		.id = V4L2_CID_2D_DITH,
		.name = "set dithering",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 1,
		.def = false,
	}, {
		.ops = &sc_ctrl_ops,
		.id = V4L2_CID_2D_FMT_PREMULTI,
		.name = "set pre-multiplied format",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = 1,
		.def = false,
	}, {
		.ops = &sc_ctrl_ops,
		.id = V4L2_CID_CSC_EQ,
		.name = "Set CSC equation",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = SC_CSC_709,
		.def = SC_CSC_601,
	}, {
		.ops = &sc_ctrl_ops,
		.id = V4L2_CID_CSC_RANGE,
		.name = "Set CSC range",
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.flags = V4L2_CTRL_FLAG_SLIDER,
		.max = SC_CSC_WIDE,
		.def = SC_CSC_NARROW,
	}
};

static int sc_add_ctrls(struct sc_ctx *ctx)
{
	int i;

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, SC_MAX_CTRL_NUM);
	v4l2_ctrl_new_std(&ctx->ctrl_handler, &sc_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&ctx->ctrl_handler, &sc_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&ctx->ctrl_handler, &sc_ctrl_ops,
			V4L2_CID_ROTATE, 0, 270, 90, 0);

	for (i = 0; i < ARRAY_SIZE(sc_custom_ctrl); i++)
		v4l2_ctrl_new_custom(&ctx->ctrl_handler,
				&sc_custom_ctrl[i], NULL);
	if (ctx->ctrl_handler.error) {
		int err = ctx->ctrl_handler.error;
		v4l2_err(&ctx->sc_dev->m2m.v4l2_dev,
				"v4l2_ctrl_handler_init failed %d\n", err);
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		return err;
	}

	v4l2_ctrl_handler_setup(&ctx->ctrl_handler);

	return 0;
}

static int sc_open(struct file *file)
{
	struct sc_dev *sc = video_drvdata(file);
	struct sc_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx) {
		dev_err(sc->dev, "no memory for open context\n");
		return -ENOMEM;
	}

	atomic_inc(&sc->m2m.in_use);
	ctx->sc_dev = sc;

	v4l2_fh_init(&ctx->fh, sc->m2m.vfd);
	ret = sc_add_ctrls(ctx);
	if (ret)
		goto err_fh;

	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	/* Default color format */
	ctx->s_frame.sc_fmt = &sc_formats[0];
	ctx->d_frame.sc_fmt = &sc_formats[0];
	init_waitqueue_head(&sc->wait);
	spin_lock_init(&ctx->slock);

	INIT_LIST_HEAD(&ctx->fence_wait_list);
	INIT_WORK(&ctx->fence_work, sc_fence_work);
	ctx->fence_wq = create_singlethread_workqueue("sc_wq");
	if (&ctx->fence_wq == NULL) {
		dev_err(sc->dev, "failed to create work queue\n");
		goto err_wq;
	}

	/* Setup the device context for mem2mem mode. */
	ctx->m2m_ctx = v4l2_m2m_ctx_init(sc->m2m.m2m_dev, ctx, queue_init);
	if (IS_ERR(ctx->m2m_ctx)) {
		ret = -EINVAL;
		goto err_ctx;
	}

	return 0;

err_ctx:
	if (&ctx->fence_wq)
		destroy_workqueue(ctx->fence_wq);
err_wq:
	v4l2_fh_del(&ctx->fh);
err_fh:
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_fh_exit(&ctx->fh);
	atomic_dec(&sc->m2m.in_use);
	kfree(ctx);

	return ret;
}

static int sc_release(struct file *file)
{
	struct sc_ctx *ctx = fh_to_sc_ctx(file->private_data);
	struct sc_dev *sc = ctx->sc_dev;

	sc_dbg("refcnt= %d", atomic_read(&sc->m2m.in_use));

	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	atomic_dec(&sc->m2m.in_use);
	kfree(ctx);

	return 0;
}

static unsigned int sc_poll(struct file *file,
			     struct poll_table_struct *wait)
{
	struct sc_ctx *ctx = fh_to_sc_ctx(file->private_data);

	return v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
}

static int sc_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct sc_ctx *ctx = fh_to_sc_ctx(file->private_data);

	return v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
}

static const struct v4l2_file_operations sc_v4l2_fops = {
	.owner		= THIS_MODULE,
	.open		= sc_open,
	.release	= sc_release,
	.poll		= sc_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= sc_mmap,
};

static void sc_clock_gating(struct sc_dev *sc, enum sc_clk_status status)
{
	if (status == SC_CLK_ON) {
		atomic_inc(&sc->clk_cnt);
		clk_enable(sc->aclk);
		if (sc->pclk)
			clk_enable(sc->pclk);
	} else if (status == SC_CLK_OFF) {
		int clk_cnt = atomic_dec_return(&sc->clk_cnt);
		if (clk_cnt < 0) {
			dev_err(sc->dev, "scaler clock control is wrong!!\n");
			atomic_set(&sc->clk_cnt, 0);
		} else {
			clk_disable(sc->aclk);
			if (sc->pclk)
				clk_disable(sc->pclk);
		}
	}
}

static void sc_watchdog(unsigned long arg)
{
	struct sc_dev *sc = (struct sc_dev *)arg;
	struct sc_ctx *ctx;
	unsigned long flags;
	struct vb2_buffer *src_vb, *dst_vb;

	sc_dbg("timeout watchdog\n");
	if (atomic_read(&sc->wdt.cnt) >= SC_WDT_CNT) {
		sc_clock_gating(sc, SC_CLK_OFF);
		pm_runtime_put(sc->dev);

		sc_dbg("wakeup blocked process\n");
		atomic_set(&sc->wdt.cnt, 0);
		clear_bit(DEV_RUN, &sc->state);

		ctx = v4l2_m2m_get_curr_priv(sc->m2m.m2m_dev);
		if (!ctx || !ctx->m2m_ctx) {
			dev_err(sc->dev, "current ctx is NULL\n");
			return;
		}
		spin_lock_irqsave(&sc->slock, flags);
		clear_bit(CTX_RUN, &ctx->flags);
		src_vb = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		dst_vb = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);

		if (src_vb && dst_vb) {
			v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_ERROR);
			v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_ERROR);

			v4l2_m2m_job_finish(sc->m2m.m2m_dev, ctx->m2m_ctx);
		}
		spin_unlock_irqrestore(&sc->slock, flags);
		return;
	}

	if (test_bit(DEV_RUN, &sc->state)) {
		atomic_inc(&sc->wdt.cnt);
		dev_err(sc->dev, "scaler is still running\n");
		sc->wdt.timer.expires = jiffies + SC_TIMEOUT;
		add_timer(&sc->wdt.timer);
	} else {
		sc_dbg("scaler finished job\n");
	}
}

static irqreturn_t sc_irq_handler(int irq, void *priv)
{
	struct sc_dev *sc = priv;
	struct sc_ctx *ctx;
	struct vb2_buffer *src_vb, *dst_vb;
	int val;

	spin_lock(&sc->slock);

	clear_bit(DEV_RUN, &sc->state);

	if (timer_pending(&sc->wdt.timer))
		del_timer(&sc->wdt.timer);

	val = sc_hwget_int_status(sc);
	sc_hwset_int_clear(sc);

	sc_clock_gating(sc, SC_CLK_OFF);
	pm_runtime_put(sc->dev);

	ctx = v4l2_m2m_get_curr_priv(sc->m2m.m2m_dev);
	if (!ctx || !ctx->m2m_ctx) {
		dev_err(sc->dev, "current ctx is NULL\n");
		goto isr_unlock;
	}

	clear_bit(CTX_RUN, &ctx->flags);

	src_vb = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);

	if (src_vb && dst_vb) {
		if (val & SCALER_INT_STATUS_FRAME_END) {
			v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_DONE);
			v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_DONE);
		} else {
			dev_err(sc->dev, "illegal setting 0x%x err!!!\n", val);
			v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_ERROR);
			v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_ERROR);
		}

		if (test_bit(DEV_SUSPEND, &sc->state)) {
			sc_dbg("wake up blocked process by suspend\n");
			wake_up(&sc->wait);
		} else {
			v4l2_m2m_job_finish(sc->m2m.m2m_dev, ctx->m2m_ctx);
		}

		/* Wake up from CTX_ABORT state */
		if (test_and_clear_bit(CTX_ABORT, &ctx->flags))
			wake_up(&sc->wait);
	} else {
		dev_err(sc->dev, "failed to get the buffer done\n");
	}

isr_unlock:
	spin_unlock(&sc->slock);

	return IRQ_HANDLED;
}

static int sc_get_scale_filter(unsigned int ratio)
{
	int filter;

	if (ratio <= 65536)
		filter = 0;	/* 8:8 or zoom-in */
	else if (ratio <= 74898)
		filter = 1;	/* 8:7 zoom-out */
	else if (ratio <= 87381)
		filter = 2;	/* 8:6 zoom-out */
	else if (ratio <= 104857)
		filter = 3;	/* 8:5 zoom-out */
	else if (ratio <= 131072)
		filter = 4;	/* 8:4 zoom-out */
	else if (ratio <= 174762)
		filter = 5;	/* 8:3 zoom-out */
	else
		filter = 6;	/* 8:2 zoom-out */

	return filter;
}

static void sc_set_scale_coef(struct sc_dev *sc, unsigned int h_ratio,
				unsigned int v_ratio)
{
	int h_coef, v_coef;

	h_coef = sc_get_scale_filter(h_ratio);
	v_coef = sc_get_scale_filter(v_ratio);

	sc_hwset_hcoef(sc, h_coef);
	sc_hwset_vcoef(sc, v_coef);
}

#define SCALE_RATIO(x, y)	((65536 * x) / y)

static void sc_set_scale_ratio(struct sc_ctx *ctx)
{
	struct sc_frame *s_frame, *d_frame;
	unsigned int h_ratio, v_ratio;
	struct sc_dev *sc = ctx->sc_dev;

	s_frame = &ctx->s_frame;
	d_frame = &ctx->d_frame;

	if (ctx->rotation == 90 || ctx->rotation == 270) {
		h_ratio = SCALE_RATIO(s_frame->crop.height,
				d_frame->crop.width);
		v_ratio = SCALE_RATIO(s_frame->crop.width,
				d_frame->crop.height);
	} else {
		h_ratio = SCALE_RATIO(s_frame->crop.width,
				d_frame->crop.width);
		v_ratio = SCALE_RATIO(s_frame->crop.height,
				d_frame->crop.height);
	}

	sc_set_scale_coef(sc, h_ratio, v_ratio);

	sc_hwset_hratio(sc, h_ratio);
	sc_hwset_vratio(sc, v_ratio);
}

static void sc_get_bufaddr(struct sc_dev *sc, struct vb2_buffer *vb,
		struct sc_frame *frame, unsigned int *size)
{
	*size = frame->pix_mp.width * frame->pix_mp.height;
	frame->addr.y = sc->vb2->plane_addr(vb, 0);
	frame->addr.cb = 0;
	frame->addr.cr = 0;
	sc_dbg("vaddr 0x%x\n", (unsigned int)vb2_plane_vaddr(vb, 0));

	switch (frame->sc_fmt->num_comp) {
	case 1:
		*size = *size * (frame->sc_fmt->bitperpixel[0] >> 3);
		break;
	case 2:
		if (frame->sc_fmt->num_planes == 1)
			frame->addr.cb = frame->addr.y + *size;
		else if (frame->sc_fmt->num_planes == 2)
			frame->addr.cb = sc->vb2->plane_addr(vb, 1);
		break;
	case 3:
		if (frame->sc_fmt->num_planes == 1) {
			frame->addr.cb = frame->addr.y + *size;
			frame->addr.cr = frame->addr.cb + (*size >> 2);
		} else if (frame->sc_fmt->num_planes == 3) {
			frame->addr.cb = sc->vb2->plane_addr(vb, 1);
			frame->addr.cr = sc->vb2->plane_addr(vb, 2);
		} else {
			dev_err(sc->dev, "Please check the num of comp\n");
		}
		break;
	default:
		break;
	}

	if (frame->sc_fmt->pixelformat == V4L2_PIX_FMT_YVU420 ||
			frame->sc_fmt->pixelformat == V4L2_PIX_FMT_YVU420M) {
		u32 t_cb = frame->addr.cb;
		frame->addr.cb = frame->addr.cr;
		frame->addr.cr = t_cb;
	}

	sc_dbg("y addr 0x%x Cb 0x%x Cr 0x%x\n",
			frame->addr.y, frame->addr.cb, frame->addr.cr);
}

static void sc_set_sysmmu_pbuf(struct sc_ctx *ctx, unsigned int s_size,
				unsigned int d_size)
{
	unsigned int num_pbuf;
	struct sc_dev *sc = ctx->sc_dev;
	struct sc_frame *s_frame, *d_frame;
	struct sysmmu_prefbuf prebuf[sc_num_pbuf(sc)];

	memset(prebuf, 0, sizeof(prebuf));

	s_frame = &ctx->s_frame;
	d_frame = &ctx->d_frame;

	num_pbuf = sc_num_pbuf(sc);

	prebuf[0].base = s_frame->addr.y;
	prebuf[0].size = s_size;

	switch (s_frame->sc_fmt->num_comp) {
	case 1:
		prebuf[1].base = d_frame->addr.y;
		prebuf[1].size = d_size;
		if (d_frame->sc_fmt->num_comp == 1) {
			num_pbuf = 2;
		} else {
			if (sc_num_pbuf(sc) > 2) {
				prebuf[2].base = d_frame->addr.cb;
				/* TODO: re-calculate size in case of YUV420 */
				prebuf[2].size = d_size;
			}
		}
		break;
	case 2:
		prebuf[1].base = s_frame->addr.cb;
		prebuf[1].size = s_size;
		if (sc_num_pbuf(sc) > 2) {
			prebuf[2].base = d_frame->addr.y;
			prebuf[2].size = d_size;
		}
		break;
	case 3:
		prebuf[1].base = s_frame->addr.cb;
		prebuf[1].size = s_size;
		if (sc_num_pbuf(sc) > 2) {
			prebuf[2].base = s_frame->addr.cr;
			prebuf[2].size = s_size;
		}
		break;
	}

	exynos_sysmmu_set_pbuf(sc->dev, num_pbuf, prebuf);
}

static void sc_set_frame_addr(struct sc_ctx *ctx)
{
	struct vb2_buffer *src_vb, *dst_vb;
	struct sc_frame *s_frame, *d_frame;
	struct sc_dev *sc = ctx->sc_dev;
	unsigned int s_size, d_size;

	s_frame = &ctx->s_frame;
	d_frame = &ctx->d_frame;

	/* get source buffer address */
	src_vb = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	sc_get_bufaddr(sc, src_vb, s_frame, &s_size);

	/* get destination buffer address */
	dst_vb = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	sc_get_bufaddr(sc, dst_vb, d_frame, &d_size);

	/* set buffer base address */
	sc_hwset_src_addr(sc, &s_frame->addr);
	sc_hwset_dst_addr(sc, &d_frame->addr);

	/* set sysmmu prefetch buffer */
	sc_set_sysmmu_pbuf(ctx, s_size, d_size);
}

static void sc_set_csc_coef(struct sc_ctx *ctx)
{
	struct sc_frame *s_frame, *d_frame;
	struct sc_dev *sc;
	enum sc_csc_idx idx;

	sc = ctx->sc_dev;
	s_frame = &ctx->s_frame;
	d_frame = &ctx->d_frame;

	if (s_frame->sc_fmt->color == d_frame->sc_fmt->color)
		idx = NO_CSC;
	else if (sc_fmt_is_rgb(s_frame->sc_fmt->color))
		idx = CSC_R2Y;
	else
		idx = CSC_Y2R;

	sc_hwset_csc_coef(sc, idx, &ctx->csc);
}

static void sc_set_dithering(struct sc_ctx *ctx)
{
	struct sc_frame *s_frame, *d_frame;
	struct sc_dev *sc = ctx->sc_dev;
	unsigned int val = 0;

	if (sc_ver_is_5a(sc)) {
		s_frame = &ctx->s_frame;
		d_frame = &ctx->d_frame;

		if (s_frame->sc_fmt->pixelformat != d_frame->sc_fmt->pixelformat
			&& sc_fmt_is_rgb(d_frame->sc_fmt->color) && ctx->dith) {
			switch (d_frame->sc_fmt->pixelformat) {
			case V4L2_PIX_FMT_RGB32:
			case V4L2_PIX_FMT_BGR32:
				val = sc_dith_val(SC_DITH_8BIT, SC_DITH_8BIT,
						SC_DITH_8BIT);
				break;
			case V4L2_PIX_FMT_RGB565:
				val = sc_dith_val(SC_DITH_5BIT, SC_DITH_6BIT,
						SC_DITH_5BIT);
				break;
			case V4L2_PIX_FMT_RGB555X:
				val = sc_dith_val(SC_DITH_5BIT, SC_DITH_5BIT,
						SC_DITH_5BIT);
				break;
			case V4L2_PIX_FMT_RGB444:
				val = sc_dith_val(SC_DITH_4BIT, SC_DITH_4BIT,
						SC_DITH_4BIT);
				break;
			default:
				val = sc_dith_val(SC_DITH_8BIT, SC_DITH_8BIT,
						SC_DITH_8BIT);
				break;
			}
		}
	} else {
		if (ctx->dith)
			val = sc_dith_val(1, 1, 1);
	}

	sc_dbg("dither value is 0x%x\n", val);
	sc_hwset_dith(sc, val);
}

static void sc_m2m_device_run(void *priv)
{
	struct sc_ctx *ctx = priv;
	struct sc_dev *sc;
	struct sc_frame *s_frame, *d_frame;

	sc = ctx->sc_dev;

	if (test_bit(DEV_RUN, &sc->state)) {
		dev_err(sc->dev, "Scaler is already in progress\n");
		return;
	}

	if (test_bit(DEV_SUSPEND, &sc->state)) {
		dev_err(sc->dev, "Scaler is in suspend state\n");
		return;
	}

	if (test_bit(CTX_ABORT, &ctx->flags)) {
		dev_err(sc->dev, "aborted scaler device run\n");
		return;
	}

	if (in_irq())
		pm_runtime_get(sc->dev);
	else
		pm_runtime_get_sync(sc->dev);

	sc_clock_gating(sc, SC_CLK_ON);

	sc_hwset_soft_reset(sc);

	s_frame = &ctx->s_frame;
	d_frame = &ctx->d_frame;

	sc_hwset_src_image_format(sc, s_frame->sc_fmt->pixelformat);
	sc_hwset_dst_image_format(sc, d_frame->sc_fmt->pixelformat);
	if (ctx->pre_multi)
		sc_hwset_pre_multi_format(sc);

	sc_hwset_src_imgsize(sc, s_frame);
	sc_hwset_dst_imgsize(sc, d_frame);
	sc_hwset_src_crop(sc, &s_frame->crop);
	sc_hwset_dst_crop(sc, &d_frame->crop);

	sc_set_frame_addr(ctx);
	sc_set_csc_coef(ctx);
	sc_set_scale_ratio(ctx);
	sc_set_dithering(ctx);

	if (ctx->bl_op)
		sc_hwset_blend(sc, ctx->bl_op, ctx->pre_multi);
	if (ctx->color_fill)
		sc_hwset_color_fill(sc, ctx->color_fill);
	sc_hwset_flip_rotation(sc, ctx->flip, ctx->rotation);
	sc_hwset_int_en(sc, 1);

	sc->wdt.timer.expires = jiffies + SC_TIMEOUT;
	add_timer(&sc->wdt.timer);

	set_bit(DEV_RUN, &sc->state);
	set_bit(CTX_RUN, &ctx->flags);

	sc_hwset_start(sc);
}

static void sc_m2m_job_abort(void *priv)
{
	struct sc_ctx *ctx = priv;
	int ret;

	ret = sc_ctx_stop_req(ctx);
	if (ret < 0)
		dev_err(ctx->sc_dev->dev, "wait timeout\n");
}

static struct v4l2_m2m_ops sc_m2m_ops = {
	.device_run	= sc_m2m_device_run,
	.job_abort	= sc_m2m_job_abort,
};

static int sc_register_m2m_device(struct sc_dev *sc)
{
	struct v4l2_device *v4l2_dev;
	struct device *dev;
	struct video_device *vfd;
	int ret = 0;

	if (!sc)
		return -ENODEV;

	dev = sc->dev;
	v4l2_dev = &sc->m2m.v4l2_dev;

	snprintf(v4l2_dev->name, sizeof(v4l2_dev->name), "%s.m2m",
			MODULE_NAME);

	ret = v4l2_device_register(dev, v4l2_dev);
	if (ret) {
		dev_err(sc->dev, "failed to register v4l2 device\n");
		return ret;
	}

	vfd = video_device_alloc();
	if (!vfd) {
		dev_err(sc->dev, "failed to allocate video device\n");
		goto err_v4l2_dev;
	}

	vfd->fops	= &sc_v4l2_fops;
	vfd->ioctl_ops	= &sc_v4l2_ioctl_ops;
	vfd->release	= video_device_release;
	vfd->lock	= &sc->lock;
	snprintf(vfd->name, sizeof(vfd->name), "%s:m2m", MODULE_NAME);

	video_set_drvdata(vfd, sc);

	sc->m2m.vfd = vfd;
	sc->m2m.m2m_dev = v4l2_m2m_init(&sc_m2m_ops);
	if (IS_ERR(sc->m2m.m2m_dev)) {
		dev_err(sc->dev, "failed to initialize v4l2-m2m device\n");
		ret = PTR_ERR(sc->m2m.m2m_dev);
		goto err_dev_alloc;
	}

	ret = video_register_device(vfd, VFL_TYPE_GRABBER,
				EXYNOS_VIDEONODE_SCALER(sc->id));
	if (ret) {
		dev_err(sc->dev, "failed to register video device\n");
		goto err_m2m_dev;
	}

	return 0;

err_m2m_dev:
	v4l2_m2m_release(sc->m2m.m2m_dev);
err_dev_alloc:
	video_device_release(sc->m2m.vfd);
err_v4l2_dev:
	v4l2_device_unregister(v4l2_dev);

	return ret;
}

static int sc_suspend(struct device *dev)
{
	struct sc_dev *sc = dev_get_drvdata(dev);
	int ret;

	set_bit(DEV_SUSPEND, &sc->state);

	ret = wait_event_timeout(sc->wait,
			!test_bit(DEV_RUN, &sc->state), SC_TIMEOUT);
	if (ret == 0)
		dev_err(sc->dev, "wait timeout\n");

	return 0;
}

static int sc_resume(struct device *dev)
{
	struct sc_dev *sc = dev_get_drvdata(dev);

	clear_bit(DEV_SUSPEND, &sc->state);

	return 0;
}

static const struct dev_pm_ops sc_pm_ops = {
	.suspend		= sc_suspend,
	.resume			= sc_resume,
};

static int sc_probe(struct platform_device *pdev)
{
	struct exynos_scaler_platdata *pdata;
	struct sc_dev *sc;
	struct resource *res;
	int ret = 0;

	dev_info(&pdev->dev, "++%s\n", __func__);

	sc = devm_kzalloc(&pdev->dev, sizeof(struct sc_dev), GFP_KERNEL);
	if (!sc) {
		dev_err(&pdev->dev, "no memory for scaler device\n");
		return -ENOMEM;
	}

	sc->dev = &pdev->dev;
	sc->id = pdev->id;
	pdata = pdev->dev.platform_data;

	spin_lock_init(&sc->slock);
	mutex_init(&sc->lock);

	/* Get memory resource and map SFR region. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sc->regs = devm_request_and_ioremap(&pdev->dev, res);
	if (sc->regs == NULL) {
		dev_err(&pdev->dev, "failed to claim register region\n");
		return -ENOENT;
	}

	/* Get IRQ resource and register IRQ handler. */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get IRQ resource\n");
		return -ENXIO;
	}

	ret = devm_request_irq(&pdev->dev, res->start, sc_irq_handler, 0,
			pdev->name, sc);
	if (ret) {
		dev_err(&pdev->dev, "failed to install irq\n");
		return ret;
	}

	atomic_set(&sc->wdt.cnt, 0);
	setup_timer(&sc->wdt.timer, sc_watchdog, (unsigned long)sc);

	if (pdata->use_pclk) {
		sc->aclk = clk_get(sc->dev, "sc-aclk");
		if (IS_ERR(sc->aclk)) {
			dev_err(&pdev->dev, "failed to get aclk for scaler\n");
			return -ENXIO;
		}

		sc->pclk = clk_get(sc->dev, "sc-pclk");
		if (IS_ERR(sc->pclk)) {
			dev_err(&pdev->dev, "failed to get pclk for scaler\n");
			clk_put(sc->aclk);
			return -ENXIO;
		}
	} else {
		sc->aclk = clk_get(sc->dev, "mscl");
		if (IS_ERR(sc->aclk)) {
			dev_err(&pdev->dev, "failed to get clk for scaler\n");
			return -ENXIO;
		}
	}

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
	sc->vb2 = &sc_vb2_cma;
#elif defined(CONFIG_VIDEOBUF2_ION)
	sc->vb2 = &sc_vb2_ion;
#endif

	sc->alloc_ctx = sc->vb2->init(sc);
	if (IS_ERR_OR_NULL(sc->alloc_ctx)) {
		ret = PTR_ERR(sc->alloc_ctx);
		dev_err(&pdev->dev, "failed to alloc_ctx\n");
		goto err_clk;
	}

	sc->vb2->resume(sc->alloc_ctx);

	pm_runtime_enable(&pdev->dev);

	pm_runtime_get_sync(sc->dev);
	sc_clock_gating(sc, SC_CLK_ON);
	sc->ver = sc_hwget_version(sc);
	sc_clock_gating(sc, SC_CLK_OFF);
	pm_runtime_put_sync(sc->dev);

	if (sc_ver_is_5a(sc))
		sc->variant = &variant_5a;
	else
		sc->variant = &variant;

	platform_set_drvdata(pdev, sc);

	ret = sc_register_m2m_device(sc);
	if (ret) {
		dev_err(&pdev->dev, "failed to register m2m device\n");
		ret = -EPERM;
		goto err_clk;
	}

	dev_info(&pdev->dev, "scaler registered successfully\n");

	return 0;

err_clk:
	clk_put(sc->aclk);
	if (sc->pclk)
		clk_put(sc->pclk);

	return ret;
}

static int sc_remove(struct platform_device *pdev)
{
	struct sc_dev *sc = platform_get_drvdata(pdev);

	sc->vb2->suspend(sc->alloc_ctx);

	clk_put(sc->aclk);
	if (sc->pclk)
		clk_put(sc->pclk);

	if (timer_pending(&sc->wdt.timer))
		del_timer(&sc->wdt.timer);

	return 0;
}

static struct platform_driver sc_driver = {
	.probe		= sc_probe,
	.remove		= sc_remove,
	.driver = {
		.name	= MODULE_NAME,
		.owner	= THIS_MODULE,
		.pm	= &sc_pm_ops,
	}
};

module_platform_driver(sc_driver);

MODULE_AUTHOR("Sunyoung, Kang <sy0816.kang@samsung.com>");
MODULE_DESCRIPTION("EXYNOS m2m scaler driver");
MODULE_LICENSE("GPL");
