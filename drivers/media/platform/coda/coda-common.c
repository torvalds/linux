/*
 * Coda multi-standard codec IP
 *
 * Copyright (C) 2012 Vista Silicon S.L.
 *    Javier Martin, <javier.martin@vista-silicon.com>
 *    Xavier Duret
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kfifo.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/of.h>
#include <linux/platform_data/coda.h>
#include <linux/reset.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-vmalloc.h>

#include "coda.h"

#define CODA_NAME		"coda"

#define CODADX6_MAX_INSTANCES	4
#define CODA_MAX_FORMATS	4

#define CODA_ISRAM_SIZE	(2048 * 2)

#define MIN_W 176
#define MIN_H 144

#define S_ALIGN		1 /* multiple of 2 */
#define W_ALIGN		1 /* multiple of 2 */
#define H_ALIGN		1 /* multiple of 2 */

#define fh_to_ctx(__fh)	container_of(__fh, struct coda_ctx, fh)

int coda_debug;
module_param(coda_debug, int, 0644);
MODULE_PARM_DESC(coda_debug, "Debug level (0-2)");

struct coda_fmt {
	char *name;
	u32 fourcc;
};

void coda_write(struct coda_dev *dev, u32 data, u32 reg)
{
	v4l2_dbg(2, coda_debug, &dev->v4l2_dev,
		 "%s: data=0x%x, reg=0x%x\n", __func__, data, reg);
	writel(data, dev->regs_base + reg);
}

unsigned int coda_read(struct coda_dev *dev, u32 reg)
{
	u32 data;

	data = readl(dev->regs_base + reg);
	v4l2_dbg(2, coda_debug, &dev->v4l2_dev,
		 "%s: data=0x%x, reg=0x%x\n", __func__, data, reg);
	return data;
}

void coda_write_base(struct coda_ctx *ctx, struct coda_q_data *q_data,
		     struct vb2_buffer *buf, unsigned int reg_y)
{
	u32 base_y = vb2_dma_contig_plane_dma_addr(buf, 0);
	u32 base_cb, base_cr;

	switch (q_data->fourcc) {
	case V4L2_PIX_FMT_YVU420:
		/* Switch Cb and Cr for YVU420 format */
		base_cr = base_y + q_data->bytesperline * q_data->height;
		base_cb = base_cr + q_data->bytesperline * q_data->height / 4;
		break;
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_NV12:
	default:
		base_cb = base_y + q_data->bytesperline * q_data->height;
		base_cr = base_cb + q_data->bytesperline * q_data->height / 4;
		break;
	case V4L2_PIX_FMT_YUV422P:
		base_cb = base_y + q_data->bytesperline * q_data->height;
		base_cr = base_cb + q_data->bytesperline * q_data->height / 2;
	}

	coda_write(ctx->dev, base_y, reg_y);
	coda_write(ctx->dev, base_cb, reg_y + 4);
	coda_write(ctx->dev, base_cr, reg_y + 8);
}

/*
 * Array of all formats supported by any version of Coda:
 */
static const struct coda_fmt coda_formats[] = {
	{
		.name = "YUV 4:2:0 Planar, YCbCr",
		.fourcc = V4L2_PIX_FMT_YUV420,
	},
	{
		.name = "YUV 4:2:0 Planar, YCrCb",
		.fourcc = V4L2_PIX_FMT_YVU420,
	},
	{
		.name = "YUV 4:2:0 Partial interleaved Y/CbCr",
		.fourcc = V4L2_PIX_FMT_NV12,
	},
	{
		.name = "YUV 4:2:2 Planar, YCbCr",
		.fourcc = V4L2_PIX_FMT_YUV422P,
	},
	{
		.name = "H264 Encoded Stream",
		.fourcc = V4L2_PIX_FMT_H264,
	},
	{
		.name = "MPEG4 Encoded Stream",
		.fourcc = V4L2_PIX_FMT_MPEG4,
	},
	{
		.name = "JPEG Encoded Images",
		.fourcc = V4L2_PIX_FMT_JPEG,
	},
};

#define CODA_CODEC(mode, src_fourcc, dst_fourcc, max_w, max_h) \
	{ mode, src_fourcc, dst_fourcc, max_w, max_h }

/*
 * Arrays of codecs supported by each given version of Coda:
 *  i.MX27 -> codadx6
 *  i.MX5x -> coda7
 *  i.MX6  -> coda960
 * Use V4L2_PIX_FMT_YUV420 as placeholder for all supported YUV 4:2:0 variants
 */
static const struct coda_codec codadx6_codecs[] = {
	CODA_CODEC(CODADX6_MODE_ENCODE_H264, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_H264,  720, 576),
	CODA_CODEC(CODADX6_MODE_ENCODE_MP4,  V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_MPEG4, 720, 576),
};

static const struct coda_codec coda7_codecs[] = {
	CODA_CODEC(CODA7_MODE_ENCODE_H264, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_H264,   1280, 720),
	CODA_CODEC(CODA7_MODE_ENCODE_MP4,  V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_MPEG4,  1280, 720),
	CODA_CODEC(CODA7_MODE_ENCODE_MJPG, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_JPEG,   8192, 8192),
	CODA_CODEC(CODA7_MODE_DECODE_H264, V4L2_PIX_FMT_H264,   V4L2_PIX_FMT_YUV420, 1920, 1088),
	CODA_CODEC(CODA7_MODE_DECODE_MP4,  V4L2_PIX_FMT_MPEG4,  V4L2_PIX_FMT_YUV420, 1920, 1088),
	CODA_CODEC(CODA7_MODE_DECODE_MJPG, V4L2_PIX_FMT_JPEG,   V4L2_PIX_FMT_YUV420, 8192, 8192),
};

static const struct coda_codec coda9_codecs[] = {
	CODA_CODEC(CODA9_MODE_ENCODE_H264, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_H264,   1920, 1088),
	CODA_CODEC(CODA9_MODE_ENCODE_MP4,  V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_MPEG4,  1920, 1088),
	CODA_CODEC(CODA9_MODE_DECODE_H264, V4L2_PIX_FMT_H264,   V4L2_PIX_FMT_YUV420, 1920, 1088),
	CODA_CODEC(CODA9_MODE_DECODE_MP4,  V4L2_PIX_FMT_MPEG4,  V4L2_PIX_FMT_YUV420, 1920, 1088),
};

struct coda_video_device {
	const char *name;
	enum coda_inst_type type;
	const struct coda_context_ops *ops;
	bool direct;
	u32 src_formats[CODA_MAX_FORMATS];
	u32 dst_formats[CODA_MAX_FORMATS];
};

static const struct coda_video_device coda_bit_encoder = {
	.name = "coda-encoder",
	.type = CODA_INST_ENCODER,
	.ops = &coda_bit_encode_ops,
	.src_formats = {
		V4L2_PIX_FMT_YUV420,
		V4L2_PIX_FMT_YVU420,
		V4L2_PIX_FMT_NV12,
	},
	.dst_formats = {
		V4L2_PIX_FMT_H264,
		V4L2_PIX_FMT_MPEG4,
	},
};

static const struct coda_video_device coda_bit_jpeg_encoder = {
	.name = "coda-jpeg-encoder",
	.type = CODA_INST_ENCODER,
	.ops = &coda_bit_encode_ops,
	.src_formats = {
		V4L2_PIX_FMT_YUV420,
		V4L2_PIX_FMT_YVU420,
		V4L2_PIX_FMT_NV12,
		V4L2_PIX_FMT_YUV422P,
	},
	.dst_formats = {
		V4L2_PIX_FMT_JPEG,
	},
};

static const struct coda_video_device coda_bit_decoder = {
	.name = "coda-decoder",
	.type = CODA_INST_DECODER,
	.ops = &coda_bit_decode_ops,
	.src_formats = {
		V4L2_PIX_FMT_H264,
		V4L2_PIX_FMT_MPEG4,
	},
	.dst_formats = {
		V4L2_PIX_FMT_YUV420,
		V4L2_PIX_FMT_YVU420,
		V4L2_PIX_FMT_NV12,
	},
};

static const struct coda_video_device coda_bit_jpeg_decoder = {
	.name = "coda-jpeg-decoder",
	.type = CODA_INST_DECODER,
	.ops = &coda_bit_decode_ops,
	.src_formats = {
		V4L2_PIX_FMT_JPEG,
	},
	.dst_formats = {
		V4L2_PIX_FMT_YUV420,
		V4L2_PIX_FMT_YVU420,
		V4L2_PIX_FMT_NV12,
		V4L2_PIX_FMT_YUV422P,
	},
};

static const struct coda_video_device *codadx6_video_devices[] = {
	&coda_bit_encoder,
};

static const struct coda_video_device *coda7_video_devices[] = {
	&coda_bit_jpeg_encoder,
	&coda_bit_jpeg_decoder,
	&coda_bit_encoder,
	&coda_bit_decoder,
};

static const struct coda_video_device *coda9_video_devices[] = {
	&coda_bit_encoder,
	&coda_bit_decoder,
};

static bool coda_format_is_yuv(u32 fourcc)
{
	switch (fourcc) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_YUV422P:
		return true;
	default:
		return false;
	}
}

static const char *coda_format_name(u32 fourcc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(coda_formats); i++) {
		if (coda_formats[i].fourcc == fourcc)
			return coda_formats[i].name;
	}

	return NULL;
}

/*
 * Normalize all supported YUV 4:2:0 formats to the value used in the codec
 * tables.
 */
static u32 coda_format_normalize_yuv(u32 fourcc)
{
	return coda_format_is_yuv(fourcc) ? V4L2_PIX_FMT_YUV420 : fourcc;
}

static const struct coda_codec *coda_find_codec(struct coda_dev *dev,
						int src_fourcc, int dst_fourcc)
{
	const struct coda_codec *codecs = dev->devtype->codecs;
	int num_codecs = dev->devtype->num_codecs;
	int k;

	src_fourcc = coda_format_normalize_yuv(src_fourcc);
	dst_fourcc = coda_format_normalize_yuv(dst_fourcc);
	if (src_fourcc == dst_fourcc)
		return NULL;

	for (k = 0; k < num_codecs; k++) {
		if (codecs[k].src_fourcc == src_fourcc &&
		    codecs[k].dst_fourcc == dst_fourcc)
			break;
	}

	if (k == num_codecs)
		return NULL;

	return &codecs[k];
}

static void coda_get_max_dimensions(struct coda_dev *dev,
				    const struct coda_codec *codec,
				    int *max_w, int *max_h)
{
	const struct coda_codec *codecs = dev->devtype->codecs;
	int num_codecs = dev->devtype->num_codecs;
	unsigned int w, h;
	int k;

	if (codec) {
		w = codec->max_w;
		h = codec->max_h;
	} else {
		for (k = 0, w = 0, h = 0; k < num_codecs; k++) {
			w = max(w, codecs[k].max_w);
			h = max(h, codecs[k].max_h);
		}
	}

	if (max_w)
		*max_w = w;
	if (max_h)
		*max_h = h;
}

const struct coda_video_device *to_coda_video_device(struct video_device *vdev)
{
	struct coda_dev *dev = video_get_drvdata(vdev);
	unsigned int i = vdev - dev->vfd;

	if (i >= dev->devtype->num_vdevs)
		return NULL;

	return dev->devtype->vdevs[i];
}

const char *coda_product_name(int product)
{
	static char buf[9];

	switch (product) {
	case CODA_DX6:
		return "CodaDx6";
	case CODA_7541:
		return "CODA7541";
	case CODA_960:
		return "CODA960";
	default:
		snprintf(buf, sizeof(buf), "(0x%04x)", product);
		return buf;
	}
}

/*
 * V4L2 ioctl() operations.
 */
static int coda_querycap(struct file *file, void *priv,
			 struct v4l2_capability *cap)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);

	strlcpy(cap->driver, CODA_NAME, sizeof(cap->driver));
	strlcpy(cap->card, coda_product_name(ctx->dev->devtype->product),
		sizeof(cap->card));
	strlcpy(cap->bus_info, "platform:" CODA_NAME, sizeof(cap->bus_info));
	cap->device_caps = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int coda_enum_fmt(struct file *file, void *priv,
			 struct v4l2_fmtdesc *f)
{
	struct video_device *vdev = video_devdata(file);
	const struct coda_video_device *cvd = to_coda_video_device(vdev);
	const u32 *formats;
	const char *name;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		formats = cvd->src_formats;
	else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		formats = cvd->dst_formats;
	else
		return -EINVAL;

	if (f->index >= CODA_MAX_FORMATS || formats[f->index] == 0)
		return -EINVAL;

	name = coda_format_name(formats[f->index]);
	strlcpy(f->description, name, sizeof(f->description));
	f->pixelformat = formats[f->index];
	if (!coda_format_is_yuv(formats[f->index]))
		f->flags |= V4L2_FMT_FLAG_COMPRESSED;

	return 0;
}

static int coda_g_fmt(struct file *file, void *priv,
		      struct v4l2_format *f)
{
	struct coda_q_data *q_data;
	struct coda_ctx *ctx = fh_to_ctx(priv);

	q_data = get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	f->fmt.pix.field	= V4L2_FIELD_NONE;
	f->fmt.pix.pixelformat	= q_data->fourcc;
	f->fmt.pix.width	= q_data->width;
	f->fmt.pix.height	= q_data->height;
	f->fmt.pix.bytesperline = q_data->bytesperline;

	f->fmt.pix.sizeimage	= q_data->sizeimage;
	if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_JPEG)
		f->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	else
		f->fmt.pix.colorspace = ctx->colorspace;

	return 0;
}

static int coda_try_pixelformat(struct coda_ctx *ctx, struct v4l2_format *f)
{
	struct coda_q_data *q_data;
	const u32 *formats;
	int i;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		formats = ctx->cvd->src_formats;
	else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		formats = ctx->cvd->dst_formats;
	else
		return -EINVAL;

	for (i = 0; i < CODA_MAX_FORMATS; i++) {
		if (formats[i] == f->fmt.pix.pixelformat) {
			f->fmt.pix.pixelformat = formats[i];
			return 0;
		}
	}

	/* Fall back to currently set pixelformat */
	q_data = get_q_data(ctx, f->type);
	f->fmt.pix.pixelformat = q_data->fourcc;

	return 0;
}

static unsigned int coda_estimate_sizeimage(struct coda_ctx *ctx, u32 sizeimage,
					    u32 width, u32 height)
{
	/*
	 * This is a rough estimate for sensible compressed buffer
	 * sizes (between 1 and 16 bits per pixel). This could be
	 * improved by better format specific worst case estimates.
	 */
	return round_up(clamp(sizeimage, width * height / 8,
					 width * height * 2), PAGE_SIZE);
}

static int coda_try_fmt(struct coda_ctx *ctx, const struct coda_codec *codec,
			struct v4l2_format *f)
{
	struct coda_dev *dev = ctx->dev;
	unsigned int max_w, max_h;
	enum v4l2_field field;

	field = f->fmt.pix.field;
	if (field == V4L2_FIELD_ANY)
		field = V4L2_FIELD_NONE;
	else if (V4L2_FIELD_NONE != field)
		return -EINVAL;

	/* V4L2 specification suggests the driver corrects the format struct
	 * if any of the dimensions is unsupported */
	f->fmt.pix.field = field;

	coda_get_max_dimensions(dev, codec, &max_w, &max_h);
	v4l_bound_align_image(&f->fmt.pix.width, MIN_W, max_w, W_ALIGN,
			      &f->fmt.pix.height, MIN_H, max_h, H_ALIGN,
			      S_ALIGN);

	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
	case V4L2_PIX_FMT_NV12:
		/*
		 * Frame stride must be at least multiple of 8,
		 * but multiple of 16 for h.264 or JPEG 4:2:x
		 */
		f->fmt.pix.bytesperline = round_up(f->fmt.pix.width, 16);
		f->fmt.pix.sizeimage = f->fmt.pix.bytesperline *
					f->fmt.pix.height * 3 / 2;
		break;
	case V4L2_PIX_FMT_YUV422P:
		f->fmt.pix.bytesperline = round_up(f->fmt.pix.width, 16);
		f->fmt.pix.sizeimage = f->fmt.pix.bytesperline *
					f->fmt.pix.height * 2;
		break;
	case V4L2_PIX_FMT_JPEG:
		f->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
		/* fallthrough */
	case V4L2_PIX_FMT_H264:
	case V4L2_PIX_FMT_MPEG4:
		f->fmt.pix.bytesperline = 0;
		f->fmt.pix.sizeimage = coda_estimate_sizeimage(ctx,
							f->fmt.pix.sizeimage,
							f->fmt.pix.width,
							f->fmt.pix.height);
		break;
	default:
		BUG();
	}

	return 0;
}

static int coda_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	const struct coda_q_data *q_data_src;
	const struct coda_codec *codec;
	struct vb2_queue *src_vq;
	int ret;

	ret = coda_try_pixelformat(ctx, f);
	if (ret < 0)
		return ret;

	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);

	/*
	 * If the source format is already fixed, only allow the same output
	 * resolution
	 */
	src_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	if (vb2_is_streaming(src_vq)) {
		f->fmt.pix.width = q_data_src->width;
		f->fmt.pix.height = q_data_src->height;
	}

	f->fmt.pix.colorspace = ctx->colorspace;

	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	codec = coda_find_codec(ctx->dev, q_data_src->fourcc,
				f->fmt.pix.pixelformat);
	if (!codec)
		return -EINVAL;

	ret = coda_try_fmt(ctx, codec, f);
	if (ret < 0)
		return ret;

	/* The h.264 decoder only returns complete 16x16 macroblocks */
	if (codec && codec->src_fourcc == V4L2_PIX_FMT_H264) {
		f->fmt.pix.width = f->fmt.pix.width;
		f->fmt.pix.height = round_up(f->fmt.pix.height, 16);
		f->fmt.pix.bytesperline = round_up(f->fmt.pix.width, 16);
		f->fmt.pix.sizeimage = f->fmt.pix.bytesperline *
				       f->fmt.pix.height * 3 / 2;
	}

	return 0;
}

static int coda_try_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	struct coda_dev *dev = ctx->dev;
	const struct coda_q_data *q_data_dst;
	const struct coda_codec *codec;
	int ret;

	ret = coda_try_pixelformat(ctx, f);
	if (ret < 0)
		return ret;

	switch (f->fmt.pix.colorspace) {
	case V4L2_COLORSPACE_REC709:
	case V4L2_COLORSPACE_JPEG:
		break;
	default:
		if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_JPEG)
			f->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
		else
			f->fmt.pix.colorspace = V4L2_COLORSPACE_REC709;
	}

	q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	codec = coda_find_codec(dev, f->fmt.pix.pixelformat, q_data_dst->fourcc);

	return coda_try_fmt(ctx, codec, f);
}

static int coda_s_fmt(struct coda_ctx *ctx, struct v4l2_format *f)
{
	struct coda_q_data *q_data;
	struct vb2_queue *vq;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		v4l2_err(&ctx->dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	q_data->fourcc = f->fmt.pix.pixelformat;
	q_data->width = f->fmt.pix.width;
	q_data->height = f->fmt.pix.height;
	q_data->bytesperline = f->fmt.pix.bytesperline;
	q_data->sizeimage = f->fmt.pix.sizeimage;
	q_data->rect.left = 0;
	q_data->rect.top = 0;
	q_data->rect.width = f->fmt.pix.width;
	q_data->rect.height = f->fmt.pix.height;

	v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
		"Setting format for type %d, wxh: %dx%d, fmt: %d\n",
		f->type, q_data->width, q_data->height, q_data->fourcc);

	return 0;
}

static int coda_s_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	int ret;

	ret = coda_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	return coda_s_fmt(ctx, f);
}

static int coda_s_fmt_vid_out(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_format f_cap;
	int ret;

	ret = coda_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	ret = coda_s_fmt(ctx, f);
	if (ret)
		return ret;

	ctx->colorspace = f->fmt.pix.colorspace;

	memset(&f_cap, 0, sizeof(f_cap));
	f_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	coda_g_fmt(file, priv, &f_cap);
	f_cap.fmt.pix.width = f->fmt.pix.width;
	f_cap.fmt.pix.height = f->fmt.pix.height;

	ret = coda_try_fmt_vid_cap(file, priv, &f_cap);
	if (ret)
		return ret;

	return coda_s_fmt(ctx, &f_cap);
}

static int coda_reqbufs(struct file *file, void *priv,
			struct v4l2_requestbuffers *rb)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);
	int ret;

	ret = v4l2_m2m_reqbufs(file, ctx->fh.m2m_ctx, rb);
	if (ret)
		return ret;

	/*
	 * Allow to allocate instance specific per-context buffers, such as
	 * bitstream ringbuffer, slice buffer, work buffer, etc. if needed.
	 */
	if (rb->type == V4L2_BUF_TYPE_VIDEO_OUTPUT && ctx->ops->reqbufs)
		return ctx->ops->reqbufs(ctx, rb);

	return 0;
}

static int coda_qbuf(struct file *file, void *priv,
		     struct v4l2_buffer *buf)
{
	struct coda_ctx *ctx = fh_to_ctx(priv);

	return v4l2_m2m_qbuf(file, ctx->fh.m2m_ctx, buf);
}

static bool coda_buf_is_end_of_stream(struct coda_ctx *ctx,
				      struct vb2_buffer *buf)
{
	struct vb2_queue *src_vq;

	src_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);

	return ((ctx->bit_stream_param & CODA_BIT_STREAM_END_FLAG) &&
		(buf->v4l2_buf.sequence == (ctx->qsequence - 1)));
}

void coda_m2m_buf_done(struct coda_ctx *ctx, struct vb2_buffer *buf,
		       enum vb2_buffer_state state)
{
	const struct v4l2_event eos_event = {
		.type = V4L2_EVENT_EOS
	};

	if (coda_buf_is_end_of_stream(ctx, buf)) {
		buf->v4l2_buf.flags |= V4L2_BUF_FLAG_LAST;

		v4l2_event_queue_fh(&ctx->fh, &eos_event);
	}

	v4l2_m2m_buf_done(buf, state);
}

static int coda_g_selection(struct file *file, void *fh,
			    struct v4l2_selection *s)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);
	struct coda_q_data *q_data;
	struct v4l2_rect r, *rsel;

	q_data = get_q_data(ctx, s->type);
	if (!q_data)
		return -EINVAL;

	r.left = 0;
	r.top = 0;
	r.width = q_data->width;
	r.height = q_data->height;
	rsel = &q_data->rect;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		rsel = &r;
		/* fallthrough */
	case V4L2_SEL_TGT_CROP:
		if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		rsel = &r;
		/* fallthrough */
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	s->r = *rsel;

	return 0;
}

static int coda_try_decoder_cmd(struct file *file, void *fh,
				struct v4l2_decoder_cmd *dc)
{
	if (dc->cmd != V4L2_DEC_CMD_STOP)
		return -EINVAL;

	if (dc->flags & V4L2_DEC_CMD_STOP_TO_BLACK)
		return -EINVAL;

	if (!(dc->flags & V4L2_DEC_CMD_STOP_IMMEDIATELY) && (dc->stop.pts != 0))
		return -EINVAL;

	return 0;
}

static int coda_decoder_cmd(struct file *file, void *fh,
			    struct v4l2_decoder_cmd *dc)
{
	struct coda_ctx *ctx = fh_to_ctx(fh);
	int ret;

	ret = coda_try_decoder_cmd(file, fh, dc);
	if (ret < 0)
		return ret;

	/* Ignore decoder stop command silently in encoder context */
	if (ctx->inst_type != CODA_INST_DECODER)
		return 0;

	/* Set the stream-end flag on this context */
	coda_bit_stream_end_flag(ctx);
	ctx->hold = false;
	v4l2_m2m_try_schedule(ctx->fh.m2m_ctx);

	return 0;
}

static int coda_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

static const struct v4l2_ioctl_ops coda_ioctl_ops = {
	.vidioc_querycap	= coda_querycap,

	.vidioc_enum_fmt_vid_cap = coda_enum_fmt,
	.vidioc_g_fmt_vid_cap	= coda_g_fmt,
	.vidioc_try_fmt_vid_cap	= coda_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= coda_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out = coda_enum_fmt,
	.vidioc_g_fmt_vid_out	= coda_g_fmt,
	.vidioc_try_fmt_vid_out	= coda_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out	= coda_s_fmt_vid_out,

	.vidioc_reqbufs		= coda_reqbufs,
	.vidioc_querybuf	= v4l2_m2m_ioctl_querybuf,

	.vidioc_qbuf		= coda_qbuf,
	.vidioc_expbuf		= v4l2_m2m_ioctl_expbuf,
	.vidioc_dqbuf		= v4l2_m2m_ioctl_dqbuf,
	.vidioc_create_bufs	= v4l2_m2m_ioctl_create_bufs,

	.vidioc_streamon	= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff	= v4l2_m2m_ioctl_streamoff,

	.vidioc_g_selection	= coda_g_selection,

	.vidioc_try_decoder_cmd	= coda_try_decoder_cmd,
	.vidioc_decoder_cmd	= coda_decoder_cmd,

	.vidioc_subscribe_event = coda_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

void coda_set_gdi_regs(struct coda_ctx *ctx)
{
	struct gdi_tiled_map *tiled_map = &ctx->tiled_map;
	struct coda_dev *dev = ctx->dev;
	int i;

	for (i = 0; i < 16; i++)
		coda_write(dev, tiled_map->xy2ca_map[i],
				CODA9_GDI_XY2_CAS_0 + 4 * i);
	for (i = 0; i < 4; i++)
		coda_write(dev, tiled_map->xy2ba_map[i],
				CODA9_GDI_XY2_BA_0 + 4 * i);
	for (i = 0; i < 16; i++)
		coda_write(dev, tiled_map->xy2ra_map[i],
				CODA9_GDI_XY2_RAS_0 + 4 * i);
	coda_write(dev, tiled_map->xy2rbc_config, CODA9_GDI_XY2_RBC_CONFIG);
	for (i = 0; i < 32; i++)
		coda_write(dev, tiled_map->rbc2axi_map[i],
				CODA9_GDI_RBC2_AXI_0 + 4 * i);
}

/*
 * Mem-to-mem operations.
 */

static void coda_device_run(void *m2m_priv)
{
	struct coda_ctx *ctx = m2m_priv;
	struct coda_dev *dev = ctx->dev;

	queue_work(dev->workqueue, &ctx->pic_run_work);
}

static void coda_pic_run_work(struct work_struct *work)
{
	struct coda_ctx *ctx = container_of(work, struct coda_ctx, pic_run_work);
	struct coda_dev *dev = ctx->dev;
	int ret;

	mutex_lock(&ctx->buffer_mutex);
	mutex_lock(&dev->coda_mutex);

	ret = ctx->ops->prepare_run(ctx);
	if (ret < 0 && ctx->inst_type == CODA_INST_DECODER) {
		mutex_unlock(&dev->coda_mutex);
		mutex_unlock(&ctx->buffer_mutex);
		/* job_finish scheduled by prepare_decode */
		return;
	}

	if (!wait_for_completion_timeout(&ctx->completion,
					 msecs_to_jiffies(1000))) {
		dev_err(&dev->plat_dev->dev, "CODA PIC_RUN timeout\n");

		ctx->hold = true;

		coda_hw_reset(ctx);
	} else if (!ctx->aborting) {
		ctx->ops->finish_run(ctx);
	}

	if ((ctx->aborting || (!ctx->streamon_cap && !ctx->streamon_out)) &&
	    ctx->ops->seq_end_work)
		queue_work(dev->workqueue, &ctx->seq_end_work);

	mutex_unlock(&dev->coda_mutex);
	mutex_unlock(&ctx->buffer_mutex);

	v4l2_m2m_job_finish(ctx->dev->m2m_dev, ctx->fh.m2m_ctx);
}

static int coda_job_ready(void *m2m_priv)
{
	struct coda_ctx *ctx = m2m_priv;

	/*
	 * For both 'P' and 'key' frame cases 1 picture
	 * and 1 frame are needed. In the decoder case,
	 * the compressed frame can be in the bitstream.
	 */
	if (!v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx) &&
	    ctx->inst_type != CODA_INST_DECODER) {
		v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
			 "not ready: not enough video buffers.\n");
		return 0;
	}

	if (!v4l2_m2m_num_dst_bufs_ready(ctx->fh.m2m_ctx)) {
		v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
			 "not ready: not enough video capture buffers.\n");
		return 0;
	}

	if (ctx->inst_type == CODA_INST_DECODER && ctx->use_bit) {
		struct list_head *meta;
		bool stream_end;
		int num_metas;
		int src_bufs;

		if (ctx->hold && !v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx)) {
			v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
				 "%d: not ready: on hold for more buffers.\n",
				 ctx->idx);
			return 0;
		}

		stream_end = ctx->bit_stream_param &
			     CODA_BIT_STREAM_END_FLAG;

		num_metas = 0;
		list_for_each(meta, &ctx->buffer_meta_list)
			num_metas++;

		src_bufs = v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx);

		if (!stream_end && (num_metas + src_bufs) < 2) {
			v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
				 "%d: not ready: need 2 buffers available (%d, %d)\n",
				 ctx->idx, num_metas, src_bufs);
			return 0;
		}


		if (!v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx) &&
		    !stream_end && (coda_get_bitstream_payload(ctx) < 512)) {
			v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
				 "%d: not ready: not enough bitstream data (%d).\n",
				 ctx->idx, coda_get_bitstream_payload(ctx));
			return 0;
		}
	}

	if (ctx->aborting) {
		v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
			 "not ready: aborting\n");
		return 0;
	}

	v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
			"job ready\n");
	return 1;
}

static void coda_job_abort(void *priv)
{
	struct coda_ctx *ctx = priv;

	ctx->aborting = 1;

	v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
		 "Aborting task\n");
}

static void coda_lock(void *m2m_priv)
{
	struct coda_ctx *ctx = m2m_priv;
	struct coda_dev *pcdev = ctx->dev;

	mutex_lock(&pcdev->dev_mutex);
}

static void coda_unlock(void *m2m_priv)
{
	struct coda_ctx *ctx = m2m_priv;
	struct coda_dev *pcdev = ctx->dev;

	mutex_unlock(&pcdev->dev_mutex);
}

static const struct v4l2_m2m_ops coda_m2m_ops = {
	.device_run	= coda_device_run,
	.job_ready	= coda_job_ready,
	.job_abort	= coda_job_abort,
	.lock		= coda_lock,
	.unlock		= coda_unlock,
};

static void coda_set_tiled_map_type(struct coda_ctx *ctx, int tiled_map_type)
{
	struct gdi_tiled_map *tiled_map = &ctx->tiled_map;
	int luma_map, chro_map, i;

	memset(tiled_map, 0, sizeof(*tiled_map));

	luma_map = 64;
	chro_map = 64;
	tiled_map->map_type = tiled_map_type;
	for (i = 0; i < 16; i++)
		tiled_map->xy2ca_map[i] = luma_map << 8 | chro_map;
	for (i = 0; i < 4; i++)
		tiled_map->xy2ba_map[i] = luma_map << 8 | chro_map;
	for (i = 0; i < 16; i++)
		tiled_map->xy2ra_map[i] = luma_map << 8 | chro_map;

	if (tiled_map_type == GDI_LINEAR_FRAME_MAP) {
		tiled_map->xy2rbc_config = 0;
	} else {
		dev_err(&ctx->dev->plat_dev->dev, "invalid map type: %d\n",
			tiled_map_type);
		return;
	}
}

static void set_default_params(struct coda_ctx *ctx)
{
	unsigned int max_w, max_h, usize, csize;

	ctx->codec = coda_find_codec(ctx->dev, ctx->cvd->src_formats[0],
				     ctx->cvd->dst_formats[0]);
	max_w = min(ctx->codec->max_w, 1920U);
	max_h = min(ctx->codec->max_h, 1088U);
	usize = max_w * max_h * 3 / 2;
	csize = coda_estimate_sizeimage(ctx, usize, max_w, max_h);

	ctx->params.codec_mode = ctx->codec->mode;
	ctx->colorspace = V4L2_COLORSPACE_REC709;
	ctx->params.framerate = 30;

	/* Default formats for output and input queues */
	ctx->q_data[V4L2_M2M_SRC].fourcc = ctx->codec->src_fourcc;
	ctx->q_data[V4L2_M2M_DST].fourcc = ctx->codec->dst_fourcc;
	ctx->q_data[V4L2_M2M_SRC].width = max_w;
	ctx->q_data[V4L2_M2M_SRC].height = max_h;
	ctx->q_data[V4L2_M2M_DST].width = max_w;
	ctx->q_data[V4L2_M2M_DST].height = max_h;
	if (ctx->codec->src_fourcc == V4L2_PIX_FMT_YUV420) {
		ctx->q_data[V4L2_M2M_SRC].bytesperline = max_w;
		ctx->q_data[V4L2_M2M_SRC].sizeimage = usize;
		ctx->q_data[V4L2_M2M_DST].bytesperline = 0;
		ctx->q_data[V4L2_M2M_DST].sizeimage = csize;
	} else {
		ctx->q_data[V4L2_M2M_SRC].bytesperline = 0;
		ctx->q_data[V4L2_M2M_SRC].sizeimage = csize;
		ctx->q_data[V4L2_M2M_DST].bytesperline = max_w;
		ctx->q_data[V4L2_M2M_DST].sizeimage = usize;
	}
	ctx->q_data[V4L2_M2M_SRC].rect.width = max_w;
	ctx->q_data[V4L2_M2M_SRC].rect.height = max_h;
	ctx->q_data[V4L2_M2M_DST].rect.width = max_w;
	ctx->q_data[V4L2_M2M_DST].rect.height = max_h;

	if (ctx->dev->devtype->product == CODA_960)
		coda_set_tiled_map_type(ctx, GDI_LINEAR_FRAME_MAP);
}

/*
 * Queue operations
 */
static int coda_queue_setup(struct vb2_queue *vq,
				const struct v4l2_format *fmt,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], void *alloc_ctxs[])
{
	struct coda_ctx *ctx = vb2_get_drv_priv(vq);
	struct coda_q_data *q_data;
	unsigned int size;

	q_data = get_q_data(ctx, vq->type);
	size = q_data->sizeimage;

	*nplanes = 1;
	sizes[0] = size;

	/* Set to vb2-dma-contig allocator context, ignored by vb2-vmalloc */
	alloc_ctxs[0] = ctx->dev->alloc_ctx;

	v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
		 "get %d buffer(s) of size %d each.\n", *nbuffers, size);

	return 0;
}

static int coda_buf_prepare(struct vb2_buffer *vb)
{
	struct coda_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct coda_q_data *q_data;

	q_data = get_q_data(ctx, vb->vb2_queue->type);

	if (vb2_plane_size(vb, 0) < q_data->sizeimage) {
		v4l2_warn(&ctx->dev->v4l2_dev,
			  "%s data will not fit into plane (%lu < %lu)\n",
			  __func__, vb2_plane_size(vb, 0),
			  (long)q_data->sizeimage);
		return -EINVAL;
	}

	return 0;
}

static void coda_buf_queue(struct vb2_buffer *vb)
{
	struct coda_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_queue *vq = vb->vb2_queue;
	struct coda_q_data *q_data;

	q_data = get_q_data(ctx, vb->vb2_queue->type);

	/*
	 * In the decoder case, immediately try to copy the buffer into the
	 * bitstream ringbuffer and mark it as ready to be dequeued.
	 */
	if (ctx->bitstream.size && vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		/*
		 * For backwards compatibility, queuing an empty buffer marks
		 * the stream end
		 */
		if (vb2_get_plane_payload(vb, 0) == 0)
			coda_bit_stream_end_flag(ctx);
		mutex_lock(&ctx->bitstream_mutex);
		v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vb);
		if (vb2_is_streaming(vb->vb2_queue))
			coda_fill_bitstream(ctx, true);
		mutex_unlock(&ctx->bitstream_mutex);
	} else {
		v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vb);
	}
}

int coda_alloc_aux_buf(struct coda_dev *dev, struct coda_aux_buf *buf,
		       size_t size, const char *name, struct dentry *parent)
{
	buf->vaddr = dma_alloc_coherent(&dev->plat_dev->dev, size, &buf->paddr,
					GFP_KERNEL);
	if (!buf->vaddr) {
		v4l2_err(&dev->v4l2_dev,
			 "Failed to allocate %s buffer of size %u\n",
			 name, size);
		return -ENOMEM;
	}

	buf->size = size;

	if (name && parent) {
		buf->blob.data = buf->vaddr;
		buf->blob.size = size;
		buf->dentry = debugfs_create_blob(name, 0644, parent,
						  &buf->blob);
		if (!buf->dentry)
			dev_warn(&dev->plat_dev->dev,
				 "failed to create debugfs entry %s\n", name);
	}

	return 0;
}

void coda_free_aux_buf(struct coda_dev *dev,
		       struct coda_aux_buf *buf)
{
	if (buf->vaddr) {
		dma_free_coherent(&dev->plat_dev->dev, buf->size,
				  buf->vaddr, buf->paddr);
		buf->vaddr = NULL;
		buf->size = 0;
		debugfs_remove(buf->dentry);
		buf->dentry = NULL;
	}
}

static int coda_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct coda_ctx *ctx = vb2_get_drv_priv(q);
	struct v4l2_device *v4l2_dev = &ctx->dev->v4l2_dev;
	struct coda_q_data *q_data_src, *q_data_dst;
	struct vb2_buffer *buf;
	int ret = 0;

	q_data_src = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		if (q_data_src->fourcc == V4L2_PIX_FMT_H264 ||
		    (q_data_src->fourcc == V4L2_PIX_FMT_JPEG &&
		     ctx->dev->devtype->product == CODA_7541)) {
			/* copy the buffers that were queued before streamon */
			mutex_lock(&ctx->bitstream_mutex);
			coda_fill_bitstream(ctx, false);
			mutex_unlock(&ctx->bitstream_mutex);

			if (coda_get_bitstream_payload(ctx) < 512) {
				ret = -EINVAL;
				goto err;
			}
		} else {
			if (count < 1) {
				ret = -EINVAL;
				goto err;
			}
		}

		ctx->streamon_out = 1;
	} else {
		if (count < 1) {
			ret = -EINVAL;
			goto err;
		}

		ctx->streamon_cap = 1;
	}

	/* Don't start the coda unless both queues are on */
	if (!(ctx->streamon_out & ctx->streamon_cap))
		return 0;

	q_data_dst = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if ((q_data_src->width != q_data_dst->width &&
	     round_up(q_data_src->width, 16) != q_data_dst->width) ||
	    (q_data_src->height != q_data_dst->height &&
	     round_up(q_data_src->height, 16) != q_data_dst->height)) {
		v4l2_err(v4l2_dev, "can't convert %dx%d to %dx%d\n",
			 q_data_src->width, q_data_src->height,
			 q_data_dst->width, q_data_dst->height);
		ret = -EINVAL;
		goto err;
	}

	/* Allow BIT decoder device_run with no new buffers queued */
	if (ctx->inst_type == CODA_INST_DECODER && ctx->use_bit)
		v4l2_m2m_set_src_buffered(ctx->fh.m2m_ctx, true);

	ctx->gopcounter = ctx->params.gop_size - 1;

	ctx->codec = coda_find_codec(ctx->dev, q_data_src->fourcc,
				     q_data_dst->fourcc);
	if (!ctx->codec) {
		v4l2_err(v4l2_dev, "couldn't tell instance type.\n");
		ret = -EINVAL;
		goto err;
	}

	if (q_data_dst->fourcc == V4L2_PIX_FMT_JPEG)
		ctx->params.gop_size = 1;
	ctx->gopcounter = ctx->params.gop_size - 1;

	ret = ctx->ops->start_streaming(ctx);
	if (ctx->inst_type == CODA_INST_DECODER) {
		if (ret == -EAGAIN)
			return 0;
		else if (ret < 0)
			goto err;
	}

	ctx->initialized = 1;
	return ret;

err:
	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		while ((buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_QUEUED);
	} else {
		while ((buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_QUEUED);
	}
	return ret;
}

static void coda_stop_streaming(struct vb2_queue *q)
{
	struct coda_ctx *ctx = vb2_get_drv_priv(q);
	struct coda_dev *dev = ctx->dev;
	struct vb2_buffer *buf;
	bool stop;

	stop = ctx->streamon_out && ctx->streamon_cap;

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		v4l2_dbg(1, coda_debug, &dev->v4l2_dev,
			 "%s: output\n", __func__);
		ctx->streamon_out = 0;

		coda_bit_stream_end_flag(ctx);

		ctx->qsequence = 0;

		while ((buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
	} else {
		v4l2_dbg(1, coda_debug, &dev->v4l2_dev,
			 "%s: capture\n", __func__);
		ctx->streamon_cap = 0;

		ctx->osequence = 0;
		ctx->sequence_offset = 0;

		while ((buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
	}

	if (stop) {
		struct coda_buffer_meta *meta;

		if (ctx->ops->seq_end_work) {
			queue_work(dev->workqueue, &ctx->seq_end_work);
			flush_work(&ctx->seq_end_work);
		}
		mutex_lock(&ctx->bitstream_mutex);
		while (!list_empty(&ctx->buffer_meta_list)) {
			meta = list_first_entry(&ctx->buffer_meta_list,
						struct coda_buffer_meta, list);
			list_del(&meta->list);
			kfree(meta);
		}
		mutex_unlock(&ctx->bitstream_mutex);
		kfifo_init(&ctx->bitstream_fifo,
			ctx->bitstream.vaddr, ctx->bitstream.size);
		ctx->initialized = 0;
		ctx->runcounter = 0;
		ctx->aborting = 0;
	}
}

static const struct vb2_ops coda_qops = {
	.queue_setup		= coda_queue_setup,
	.buf_prepare		= coda_buf_prepare,
	.buf_queue		= coda_buf_queue,
	.start_streaming	= coda_start_streaming,
	.stop_streaming		= coda_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

static int coda_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct coda_ctx *ctx =
			container_of(ctrl->handler, struct coda_ctx, ctrls);

	v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
		 "s_ctrl: id = %d, val = %d\n", ctrl->id, ctrl->val);

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			ctx->params.rot_mode |= CODA_MIR_HOR;
		else
			ctx->params.rot_mode &= ~CODA_MIR_HOR;
		break;
	case V4L2_CID_VFLIP:
		if (ctrl->val)
			ctx->params.rot_mode |= CODA_MIR_VER;
		else
			ctx->params.rot_mode &= ~CODA_MIR_VER;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		ctx->params.bitrate = ctrl->val / 1000;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		ctx->params.gop_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:
		ctx->params.h264_intra_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP:
		ctx->params.h264_inter_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
		ctx->params.h264_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
		ctx->params.h264_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA:
		ctx->params.h264_deblk_alpha = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA:
		ctx->params.h264_deblk_beta = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:
		ctx->params.h264_deblk_enabled = (ctrl->val ==
				V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_ENABLED);
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP:
		ctx->params.mpeg4_intra_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP:
		ctx->params.mpeg4_inter_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE:
		ctx->params.slice_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB:
		ctx->params.slice_max_mb = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES:
		ctx->params.slice_max_bits = ctrl->val * 8;
		break;
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
		break;
	case V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB:
		ctx->params.intra_refresh = ctrl->val;
		break;
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		coda_set_jpeg_compression_quality(ctx, ctrl->val);
		break;
	case V4L2_CID_JPEG_RESTART_INTERVAL:
		ctx->params.jpeg_restart_interval = ctrl->val;
		break;
	default:
		v4l2_dbg(1, coda_debug, &ctx->dev->v4l2_dev,
			"Invalid control, id=%d, val=%d\n",
			ctrl->id, ctrl->val);
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops coda_ctrl_ops = {
	.s_ctrl = coda_s_ctrl,
};

static void coda_encode_ctrls(struct coda_ctx *ctx)
{
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_BITRATE, 0, 32767000, 1000, 0);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_GOP_SIZE, 1, 60, 1, 16);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, 0, 51, 1, 25);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP, 0, 51, 1, 25);
	if (ctx->dev->devtype->product != CODA_960) {
		v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
			V4L2_CID_MPEG_VIDEO_H264_MIN_QP, 0, 51, 1, 12);
	}
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_MAX_QP, 0, 51, 1, 51);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA, 0, 15, 1, 0);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA, 0, 15, 1, 0);
	v4l2_ctrl_new_std_menu(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE,
		V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED, 0x0,
		V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_ENABLED);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP, 1, 31, 1, 2);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP, 1, 31, 1, 2);
	v4l2_ctrl_new_std_menu(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE,
		V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_BYTES, 0x0,
		V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB, 1, 0x3fffffff, 1, 1);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES, 1, 0x3fffffff, 1,
		500);
	v4l2_ctrl_new_std_menu(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_HEADER_MODE,
		V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		(1 << V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE),
		V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB, 0,
		1920 * 1088 / 256, 1, 0);
}

static void coda_jpeg_encode_ctrls(struct coda_ctx *ctx)
{
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_JPEG_COMPRESSION_QUALITY, 5, 100, 1, 50);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_JPEG_RESTART_INTERVAL, 0, 100, 1, 0);
}

static int coda_ctrls_setup(struct coda_ctx *ctx)
{
	v4l2_ctrl_handler_init(&ctx->ctrls, 2);

	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&ctx->ctrls, &coda_ctrl_ops,
		V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (ctx->inst_type == CODA_INST_ENCODER) {
		if (ctx->cvd->dst_formats[0] == V4L2_PIX_FMT_JPEG)
			coda_jpeg_encode_ctrls(ctx);
		else
			coda_encode_ctrls(ctx);
	}

	if (ctx->ctrls.error) {
		v4l2_err(&ctx->dev->v4l2_dev,
			"control initialization error (%d)",
			ctx->ctrls.error);
		return -EINVAL;
	}

	return v4l2_ctrl_handler_setup(&ctx->ctrls);
}

static int coda_queue_init(struct coda_ctx *ctx, struct vb2_queue *vq)
{
	vq->drv_priv = ctx;
	vq->ops = &coda_qops;
	vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	vq->lock = &ctx->dev->dev_mutex;
	/* One way to indicate end-of-stream for coda is to set the
	 * bytesused == 0. However by default videobuf2 handles bytesused
	 * equal to 0 as a special case and changes its value to the size
	 * of the buffer. Set the allow_zero_bytesused flag, so
	 * that videobuf2 will keep the value of bytesused intact.
	 */
	vq->allow_zero_bytesused = 1;

	return vb2_queue_init(vq);
}

int coda_encoder_queue_init(void *priv, struct vb2_queue *src_vq,
			    struct vb2_queue *dst_vq)
{
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	src_vq->mem_ops = &vb2_dma_contig_memops;

	ret = coda_queue_init(priv, src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	dst_vq->mem_ops = &vb2_dma_contig_memops;

	return coda_queue_init(priv, dst_vq);
}

int coda_decoder_queue_init(void *priv, struct vb2_queue *src_vq,
			    struct vb2_queue *dst_vq)
{
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_DMABUF | VB2_MMAP | VB2_USERPTR;
	src_vq->mem_ops = &vb2_vmalloc_memops;

	ret = coda_queue_init(priv, src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_DMABUF | VB2_MMAP;
	dst_vq->mem_ops = &vb2_dma_contig_memops;

	return coda_queue_init(priv, dst_vq);
}

static int coda_next_free_instance(struct coda_dev *dev)
{
	int idx = ffz(dev->instance_mask);

	if ((idx < 0) ||
	    (dev->devtype->product == CODA_DX6 && idx > CODADX6_MAX_INSTANCES))
		return -EBUSY;

	return idx;
}

/*
 * File operations
 */

static int coda_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct coda_dev *dev = video_get_drvdata(vdev);
	struct coda_ctx *ctx = NULL;
	char *name;
	int ret;
	int idx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	idx = coda_next_free_instance(dev);
	if (idx < 0) {
		ret = idx;
		goto err_coda_max;
	}
	set_bit(idx, &dev->instance_mask);

	name = kasprintf(GFP_KERNEL, "context%d", idx);
	if (!name) {
		ret = -ENOMEM;
		goto err_coda_name_init;
	}

	ctx->debugfs_entry = debugfs_create_dir(name, dev->debugfs_root);
	kfree(name);

	ctx->cvd = to_coda_video_device(vdev);
	ctx->inst_type = ctx->cvd->type;
	ctx->ops = ctx->cvd->ops;
	ctx->use_bit = !ctx->cvd->direct;
	init_completion(&ctx->completion);
	INIT_WORK(&ctx->pic_run_work, coda_pic_run_work);
	if (ctx->ops->seq_end_work)
		INIT_WORK(&ctx->seq_end_work, ctx->ops->seq_end_work);
	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);
	ctx->dev = dev;
	ctx->idx = idx;
	switch (dev->devtype->product) {
	case CODA_960:
		ctx->frame_mem_ctrl = 1 << 12;
		/* fallthrough */
	case CODA_7541:
		ctx->reg_idx = 0;
		break;
	default:
		ctx->reg_idx = idx;
	}

	/* Power up and upload firmware if necessary */
	ret = pm_runtime_get_sync(&dev->plat_dev->dev);
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev, "failed to power up: %d\n", ret);
		goto err_pm_get;
	}

	ret = clk_prepare_enable(dev->clk_per);
	if (ret)
		goto err_clk_per;

	ret = clk_prepare_enable(dev->clk_ahb);
	if (ret)
		goto err_clk_ahb;

	set_default_params(ctx);
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx,
					    ctx->ops->queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);

		v4l2_err(&dev->v4l2_dev, "%s return error (%d)\n",
			 __func__, ret);
		goto err_ctx_init;
	}

	ret = coda_ctrls_setup(ctx);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "failed to setup coda controls\n");
		goto err_ctrls_setup;
	}

	ctx->fh.ctrl_handler = &ctx->ctrls;

	mutex_init(&ctx->bitstream_mutex);
	mutex_init(&ctx->buffer_mutex);
	INIT_LIST_HEAD(&ctx->buffer_meta_list);

	coda_lock(ctx);
	list_add(&ctx->list, &dev->instances);
	coda_unlock(ctx);

	v4l2_dbg(1, coda_debug, &dev->v4l2_dev, "Created instance %d (%p)\n",
		 ctx->idx, ctx);

	return 0;

err_ctrls_setup:
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
err_ctx_init:
	clk_disable_unprepare(dev->clk_ahb);
err_clk_ahb:
	clk_disable_unprepare(dev->clk_per);
err_clk_per:
	pm_runtime_put_sync(&dev->plat_dev->dev);
err_pm_get:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	clear_bit(ctx->idx, &dev->instance_mask);
err_coda_name_init:
err_coda_max:
	kfree(ctx);
	return ret;
}

static int coda_release(struct file *file)
{
	struct coda_dev *dev = video_drvdata(file);
	struct coda_ctx *ctx = fh_to_ctx(file->private_data);

	v4l2_dbg(1, coda_debug, &dev->v4l2_dev, "Releasing instance %p\n",
		 ctx);

	if (ctx->inst_type == CODA_INST_DECODER && ctx->use_bit)
		coda_bit_stream_end_flag(ctx);

	/* If this instance is running, call .job_abort and wait for it to end */
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);

	/* In case the instance was not running, we still need to call SEQ_END */
	if (ctx->initialized && ctx->ops->seq_end_work) {
		queue_work(dev->workqueue, &ctx->seq_end_work);
		flush_work(&ctx->seq_end_work);
	}

	coda_lock(ctx);
	list_del(&ctx->list);
	coda_unlock(ctx);

	if (ctx->dev->devtype->product == CODA_DX6)
		coda_free_aux_buf(dev, &ctx->workbuf);

	v4l2_ctrl_handler_free(&ctx->ctrls);
	clk_disable_unprepare(dev->clk_ahb);
	clk_disable_unprepare(dev->clk_per);
	pm_runtime_put_sync(&dev->plat_dev->dev);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	clear_bit(ctx->idx, &dev->instance_mask);
	if (ctx->ops->release)
		ctx->ops->release(ctx);
	debugfs_remove_recursive(ctx->debugfs_entry);
	kfree(ctx);

	return 0;
}

static const struct v4l2_file_operations coda_fops = {
	.owner		= THIS_MODULE,
	.open		= coda_open,
	.release	= coda_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static int coda_hw_init(struct coda_dev *dev)
{
	u32 data;
	u16 *p;
	int i, ret;

	ret = clk_prepare_enable(dev->clk_per);
	if (ret)
		goto err_clk_per;

	ret = clk_prepare_enable(dev->clk_ahb);
	if (ret)
		goto err_clk_ahb;

	if (dev->rstc)
		reset_control_reset(dev->rstc);

	/*
	 * Copy the first CODA_ISRAM_SIZE in the internal SRAM.
	 * The 16-bit chars in the code buffer are in memory access
	 * order, re-sort them to CODA order for register download.
	 * Data in this SRAM survives a reboot.
	 */
	p = (u16 *)dev->codebuf.vaddr;
	if (dev->devtype->product == CODA_DX6) {
		for (i = 0; i < (CODA_ISRAM_SIZE / 2); i++)  {
			data = CODA_DOWN_ADDRESS_SET(i) |
				CODA_DOWN_DATA_SET(p[i ^ 1]);
			coda_write(dev, data, CODA_REG_BIT_CODE_DOWN);
		}
	} else {
		for (i = 0; i < (CODA_ISRAM_SIZE / 2); i++) {
			data = CODA_DOWN_ADDRESS_SET(i) |
				CODA_DOWN_DATA_SET(p[round_down(i, 4) +
							3 - (i % 4)]);
			coda_write(dev, data, CODA_REG_BIT_CODE_DOWN);
		}
	}

	/* Clear registers */
	for (i = 0; i < 64; i++)
		coda_write(dev, 0, CODA_REG_BIT_CODE_BUF_ADDR + i * 4);

	/* Tell the BIT where to find everything it needs */
	if (dev->devtype->product == CODA_960 ||
	    dev->devtype->product == CODA_7541) {
		coda_write(dev, dev->tempbuf.paddr,
				CODA_REG_BIT_TEMP_BUF_ADDR);
		coda_write(dev, 0, CODA_REG_BIT_BIT_STREAM_PARAM);
	} else {
		coda_write(dev, dev->workbuf.paddr,
			      CODA_REG_BIT_WORK_BUF_ADDR);
	}
	coda_write(dev, dev->codebuf.paddr,
		      CODA_REG_BIT_CODE_BUF_ADDR);
	coda_write(dev, 0, CODA_REG_BIT_CODE_RUN);

	/* Set default values */
	switch (dev->devtype->product) {
	case CODA_DX6:
		coda_write(dev, CODADX6_STREAM_BUF_PIC_FLUSH,
			   CODA_REG_BIT_STREAM_CTRL);
		break;
	default:
		coda_write(dev, CODA7_STREAM_BUF_PIC_FLUSH,
			   CODA_REG_BIT_STREAM_CTRL);
	}
	if (dev->devtype->product == CODA_960)
		coda_write(dev, 1 << 12, CODA_REG_BIT_FRAME_MEM_CTRL);
	else
		coda_write(dev, 0, CODA_REG_BIT_FRAME_MEM_CTRL);

	if (dev->devtype->product != CODA_DX6)
		coda_write(dev, 0, CODA7_REG_BIT_AXI_SRAM_USE);

	coda_write(dev, CODA_INT_INTERRUPT_ENABLE,
		      CODA_REG_BIT_INT_ENABLE);

	/* Reset VPU and start processor */
	data = coda_read(dev, CODA_REG_BIT_CODE_RESET);
	data |= CODA_REG_RESET_ENABLE;
	coda_write(dev, data, CODA_REG_BIT_CODE_RESET);
	udelay(10);
	data &= ~CODA_REG_RESET_ENABLE;
	coda_write(dev, data, CODA_REG_BIT_CODE_RESET);
	coda_write(dev, CODA_REG_RUN_ENABLE, CODA_REG_BIT_CODE_RUN);

	clk_disable_unprepare(dev->clk_ahb);
	clk_disable_unprepare(dev->clk_per);

	return 0;

err_clk_ahb:
	clk_disable_unprepare(dev->clk_per);
err_clk_per:
	return ret;
}

static int coda_register_device(struct coda_dev *dev, int i)
{
	struct video_device *vfd = &dev->vfd[i];

	if (i >= dev->devtype->num_vdevs)
		return -EINVAL;

	strlcpy(vfd->name, dev->devtype->vdevs[i]->name, sizeof(vfd->name));
	vfd->fops	= &coda_fops;
	vfd->ioctl_ops	= &coda_ioctl_ops;
	vfd->release	= video_device_release_empty,
	vfd->lock	= &dev->dev_mutex;
	vfd->v4l2_dev	= &dev->v4l2_dev;
	vfd->vfl_dir	= VFL_DIR_M2M;
	video_set_drvdata(vfd, dev);

	/* Not applicable, use the selection API instead */
	v4l2_disable_ioctl(vfd, VIDIOC_CROPCAP);
	v4l2_disable_ioctl(vfd, VIDIOC_G_CROP);
	v4l2_disable_ioctl(vfd, VIDIOC_S_CROP);

	return video_register_device(vfd, VFL_TYPE_GRABBER, 0);
}

static void coda_fw_callback(const struct firmware *fw, void *context)
{
	struct coda_dev *dev = context;
	struct platform_device *pdev = dev->plat_dev;
	int i, ret;

	if (!fw) {
		v4l2_err(&dev->v4l2_dev, "firmware request failed\n");
		goto put_pm;
	}

	/* allocate auxiliary per-device code buffer for the BIT processor */
	ret = coda_alloc_aux_buf(dev, &dev->codebuf, fw->size, "codebuf",
				 dev->debugfs_root);
	if (ret < 0)
		goto put_pm;

	/* Copy the whole firmware image to the code buffer */
	memcpy(dev->codebuf.vaddr, fw->data, fw->size);
	release_firmware(fw);

	ret = coda_hw_init(dev);
	if (ret < 0) {
		v4l2_err(&dev->v4l2_dev, "HW initialization failed\n");
		goto put_pm;
	}

	ret = coda_check_firmware(dev);
	if (ret < 0)
		goto put_pm;

	dev->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(dev->alloc_ctx)) {
		v4l2_err(&dev->v4l2_dev, "Failed to alloc vb2 context\n");
		goto put_pm;
	}

	dev->m2m_dev = v4l2_m2m_init(&coda_m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem device\n");
		goto rel_ctx;
	}

	for (i = 0; i < dev->devtype->num_vdevs; i++) {
		ret = coda_register_device(dev, i);
		if (ret) {
			v4l2_err(&dev->v4l2_dev,
				 "Failed to register %s video device: %d\n",
				 dev->devtype->vdevs[i]->name, ret);
			goto rel_vfd;
		}
	}

	v4l2_info(&dev->v4l2_dev, "codec registered as /dev/video[%d-%d]\n",
		  dev->vfd[0].num, dev->vfd[i - 1].num);

	pm_runtime_put_sync(&pdev->dev);
	return;

rel_vfd:
	while (--i >= 0)
		video_unregister_device(&dev->vfd[i]);
	v4l2_m2m_release(dev->m2m_dev);
rel_ctx:
	vb2_dma_contig_cleanup_ctx(dev->alloc_ctx);
put_pm:
	pm_runtime_put_sync(&pdev->dev);
}

static int coda_firmware_request(struct coda_dev *dev)
{
	char *fw = dev->devtype->firmware;

	dev_dbg(&dev->plat_dev->dev, "requesting firmware '%s' for %s\n", fw,
		coda_product_name(dev->devtype->product));

	return request_firmware_nowait(THIS_MODULE, true,
		fw, &dev->plat_dev->dev, GFP_KERNEL, dev, coda_fw_callback);
}

enum coda_platform {
	CODA_IMX27,
	CODA_IMX53,
	CODA_IMX6Q,
	CODA_IMX6DL,
};

static const struct coda_devtype coda_devdata[] = {
	[CODA_IMX27] = {
		.firmware     = "v4l-codadx6-imx27.bin",
		.product      = CODA_DX6,
		.codecs       = codadx6_codecs,
		.num_codecs   = ARRAY_SIZE(codadx6_codecs),
		.vdevs        = codadx6_video_devices,
		.num_vdevs    = ARRAY_SIZE(codadx6_video_devices),
		.workbuf_size = 288 * 1024 + FMO_SLICE_SAVE_BUF_SIZE * 8 * 1024,
		.iram_size    = 0xb000,
	},
	[CODA_IMX53] = {
		.firmware     = "v4l-coda7541-imx53.bin",
		.product      = CODA_7541,
		.codecs       = coda7_codecs,
		.num_codecs   = ARRAY_SIZE(coda7_codecs),
		.vdevs        = coda7_video_devices,
		.num_vdevs    = ARRAY_SIZE(coda7_video_devices),
		.workbuf_size = 128 * 1024,
		.tempbuf_size = 304 * 1024,
		.iram_size    = 0x14000,
	},
	[CODA_IMX6Q] = {
		.firmware     = "v4l-coda960-imx6q.bin",
		.product      = CODA_960,
		.codecs       = coda9_codecs,
		.num_codecs   = ARRAY_SIZE(coda9_codecs),
		.vdevs        = coda9_video_devices,
		.num_vdevs    = ARRAY_SIZE(coda9_video_devices),
		.workbuf_size = 80 * 1024,
		.tempbuf_size = 204 * 1024,
		.iram_size    = 0x21000,
	},
	[CODA_IMX6DL] = {
		.firmware     = "v4l-coda960-imx6dl.bin",
		.product      = CODA_960,
		.codecs       = coda9_codecs,
		.num_codecs   = ARRAY_SIZE(coda9_codecs),
		.vdevs        = coda9_video_devices,
		.num_vdevs    = ARRAY_SIZE(coda9_video_devices),
		.workbuf_size = 80 * 1024,
		.tempbuf_size = 204 * 1024,
		.iram_size    = 0x20000,
	},
};

static struct platform_device_id coda_platform_ids[] = {
	{ .name = "coda-imx27", .driver_data = CODA_IMX27 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, coda_platform_ids);

#ifdef CONFIG_OF
static const struct of_device_id coda_dt_ids[] = {
	{ .compatible = "fsl,imx27-vpu", .data = &coda_devdata[CODA_IMX27] },
	{ .compatible = "fsl,imx53-vpu", .data = &coda_devdata[CODA_IMX53] },
	{ .compatible = "fsl,imx6q-vpu", .data = &coda_devdata[CODA_IMX6Q] },
	{ .compatible = "fsl,imx6dl-vpu", .data = &coda_devdata[CODA_IMX6DL] },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, coda_dt_ids);
#endif

static int coda_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(of_match_ptr(coda_dt_ids), &pdev->dev);
	const struct platform_device_id *pdev_id;
	struct coda_platform_data *pdata = pdev->dev.platform_data;
	struct device_node *np = pdev->dev.of_node;
	struct gen_pool *pool;
	struct coda_dev *dev;
	struct resource *res;
	int ret, irq;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	pdev_id = of_id ? of_id->data : platform_get_device_id(pdev);

	if (of_id) {
		dev->devtype = of_id->data;
	} else if (pdev_id) {
		dev->devtype = &coda_devdata[pdev_id->driver_data];
	} else {
		ret = -EINVAL;
		goto err_v4l2_register;
	}

	spin_lock_init(&dev->irqlock);
	INIT_LIST_HEAD(&dev->instances);

	dev->plat_dev = pdev;
	dev->clk_per = devm_clk_get(&pdev->dev, "per");
	if (IS_ERR(dev->clk_per)) {
		dev_err(&pdev->dev, "Could not get per clock\n");
		return PTR_ERR(dev->clk_per);
	}

	dev->clk_ahb = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(dev->clk_ahb)) {
		dev_err(&pdev->dev, "Could not get ahb clock\n");
		return PTR_ERR(dev->clk_ahb);
	}

	/* Get  memory for physical registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev->regs_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->regs_base))
		return PTR_ERR(dev->regs_base);

	/* IRQ */
	irq = platform_get_irq_byname(pdev, "bit");
	if (irq < 0)
		irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get irq resource\n");
		return irq;
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL, coda_irq_handler,
			IRQF_ONESHOT, dev_name(&pdev->dev), dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request irq: %d\n", ret);
		return ret;
	}

	dev->rstc = devm_reset_control_get_optional(&pdev->dev, NULL);
	if (IS_ERR(dev->rstc)) {
		ret = PTR_ERR(dev->rstc);
		if (ret == -ENOENT || ret == -ENOSYS) {
			dev->rstc = NULL;
		} else {
			dev_err(&pdev->dev, "failed get reset control: %d\n",
				ret);
			return ret;
		}
	}

	/* Get IRAM pool from device tree or platform data */
	pool = of_gen_pool_get(np, "iram", 0);
	if (!pool && pdata)
		pool = gen_pool_get(pdata->iram_dev);
	if (!pool) {
		dev_err(&pdev->dev, "iram pool not available\n");
		return -ENOMEM;
	}
	dev->iram_pool = pool;

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		return ret;

	mutex_init(&dev->dev_mutex);
	mutex_init(&dev->coda_mutex);

	dev->debugfs_root = debugfs_create_dir("coda", NULL);
	if (!dev->debugfs_root)
		dev_warn(&pdev->dev, "failed to create debugfs root\n");

	/* allocate auxiliary per-device buffers for the BIT processor */
	if (dev->devtype->product == CODA_DX6) {
		ret = coda_alloc_aux_buf(dev, &dev->workbuf,
					 dev->devtype->workbuf_size, "workbuf",
					 dev->debugfs_root);
		if (ret < 0)
			goto err_v4l2_register;
	}

	if (dev->devtype->tempbuf_size) {
		ret = coda_alloc_aux_buf(dev, &dev->tempbuf,
					 dev->devtype->tempbuf_size, "tempbuf",
					 dev->debugfs_root);
		if (ret < 0)
			goto err_v4l2_register;
	}

	dev->iram.size = dev->devtype->iram_size;
	dev->iram.vaddr = gen_pool_dma_alloc(dev->iram_pool, dev->iram.size,
					     &dev->iram.paddr);
	if (!dev->iram.vaddr) {
		dev_warn(&pdev->dev, "unable to alloc iram\n");
	} else {
		memset(dev->iram.vaddr, 0, dev->iram.size);
		dev->iram.blob.data = dev->iram.vaddr;
		dev->iram.blob.size = dev->iram.size;
		dev->iram.dentry = debugfs_create_blob("iram", 0644,
						       dev->debugfs_root,
						       &dev->iram.blob);
	}

	dev->workqueue = alloc_workqueue("coda", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!dev->workqueue) {
		dev_err(&pdev->dev, "unable to alloc workqueue\n");
		ret = -ENOMEM;
		goto err_v4l2_register;
	}

	platform_set_drvdata(pdev, dev);

	/*
	 * Start activated so we can directly call coda_hw_init in
	 * coda_fw_callback regardless of whether CONFIG_PM is
	 * enabled or whether the device is associated with a PM domain.
	 */
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return coda_firmware_request(dev);

err_v4l2_register:
	v4l2_device_unregister(&dev->v4l2_dev);
	return ret;
}

static int coda_remove(struct platform_device *pdev)
{
	struct coda_dev *dev = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->vfd); i++) {
		if (video_get_drvdata(&dev->vfd[i]))
			video_unregister_device(&dev->vfd[i]);
	}
	if (dev->m2m_dev)
		v4l2_m2m_release(dev->m2m_dev);
	pm_runtime_disable(&pdev->dev);
	if (dev->alloc_ctx)
		vb2_dma_contig_cleanup_ctx(dev->alloc_ctx);
	v4l2_device_unregister(&dev->v4l2_dev);
	destroy_workqueue(dev->workqueue);
	if (dev->iram.vaddr)
		gen_pool_free(dev->iram_pool, (unsigned long)dev->iram.vaddr,
			      dev->iram.size);
	coda_free_aux_buf(dev, &dev->codebuf);
	coda_free_aux_buf(dev, &dev->tempbuf);
	coda_free_aux_buf(dev, &dev->workbuf);
	debugfs_remove_recursive(dev->debugfs_root);
	return 0;
}

#ifdef CONFIG_PM
static int coda_runtime_resume(struct device *dev)
{
	struct coda_dev *cdev = dev_get_drvdata(dev);
	int ret = 0;

	if (dev->pm_domain && cdev->codebuf.vaddr) {
		ret = coda_hw_init(cdev);
		if (ret)
			v4l2_err(&cdev->v4l2_dev, "HW initialization failed\n");
	}

	return ret;
}
#endif

static const struct dev_pm_ops coda_pm_ops = {
	SET_RUNTIME_PM_OPS(NULL, coda_runtime_resume, NULL)
};

static struct platform_driver coda_driver = {
	.probe	= coda_probe,
	.remove	= coda_remove,
	.driver	= {
		.name	= CODA_NAME,
		.of_match_table = of_match_ptr(coda_dt_ids),
		.pm	= &coda_pm_ops,
	},
	.id_table = coda_platform_ids,
};

module_platform_driver(coda_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Javier Martin <javier.martin@vista-silicon.com>");
MODULE_DESCRIPTION("Coda multi-standard codec V4L2 driver");
