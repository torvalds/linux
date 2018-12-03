// SPDX-License-Identifier: GPL-2.0
/*
 * Cedrus VPU driver
 *
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 * Copyright (C) 2018 Bootlin
 *
 * Based on the vim2m driver, that is:
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <pawel@osciak.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 */

#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>

#include "cedrus.h"
#include "cedrus_video.h"
#include "cedrus_dec.h"
#include "cedrus_hw.h"

#define CEDRUS_DECODE_SRC	BIT(0)
#define CEDRUS_DECODE_DST	BIT(1)

#define CEDRUS_MIN_WIDTH	16U
#define CEDRUS_MIN_HEIGHT	16U
#define CEDRUS_MAX_WIDTH	3840U
#define CEDRUS_MAX_HEIGHT	2160U

static struct cedrus_format cedrus_formats[] = {
	{
		.pixelformat	= V4L2_PIX_FMT_MPEG2_SLICE,
		.directions	= CEDRUS_DECODE_SRC,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_SUNXI_TILED_NV12,
		.directions	= CEDRUS_DECODE_DST,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_NV12,
		.directions	= CEDRUS_DECODE_DST,
		.capabilities	= CEDRUS_CAPABILITY_UNTILED,
	},
};

#define CEDRUS_FORMATS_COUNT	ARRAY_SIZE(cedrus_formats)

static inline struct cedrus_ctx *cedrus_file2ctx(struct file *file)
{
	return container_of(file->private_data, struct cedrus_ctx, fh);
}

static struct cedrus_format *cedrus_find_format(u32 pixelformat, u32 directions,
						unsigned int capabilities)
{
	struct cedrus_format *fmt;
	unsigned int i;

	for (i = 0; i < CEDRUS_FORMATS_COUNT; i++) {
		fmt = &cedrus_formats[i];

		if (fmt->capabilities && (fmt->capabilities & capabilities) !=
		    fmt->capabilities)
			continue;

		if (fmt->pixelformat == pixelformat &&
		    (fmt->directions & directions) != 0)
			break;
	}

	if (i == CEDRUS_FORMATS_COUNT)
		return NULL;

	return &cedrus_formats[i];
}

static bool cedrus_check_format(u32 pixelformat, u32 directions,
				unsigned int capabilities)
{
	return cedrus_find_format(pixelformat, directions, capabilities);
}

static void cedrus_prepare_format(struct v4l2_pix_format *pix_fmt)
{
	unsigned int width = pix_fmt->width;
	unsigned int height = pix_fmt->height;
	unsigned int sizeimage = pix_fmt->sizeimage;
	unsigned int bytesperline = pix_fmt->bytesperline;

	pix_fmt->field = V4L2_FIELD_NONE;

	/* Limit to hardware min/max. */
	width = clamp(width, CEDRUS_MIN_WIDTH, CEDRUS_MAX_WIDTH);
	height = clamp(height, CEDRUS_MIN_HEIGHT, CEDRUS_MAX_HEIGHT);

	switch (pix_fmt->pixelformat) {
	case V4L2_PIX_FMT_MPEG2_SLICE:
		/* Zero bytes per line for encoded source. */
		bytesperline = 0;

		break;

	case V4L2_PIX_FMT_SUNXI_TILED_NV12:
		/* 32-aligned stride. */
		bytesperline = ALIGN(width, 32);

		/* 32-aligned height. */
		height = ALIGN(height, 32);

		/* Luma plane size. */
		sizeimage = bytesperline * height;

		/* Chroma plane size. */
		sizeimage += bytesperline * height / 2;

		break;

	case V4L2_PIX_FMT_NV12:
		/* 16-aligned stride. */
		bytesperline = ALIGN(width, 16);

		/* 16-aligned height. */
		height = ALIGN(height, 16);

		/* Luma plane size. */
		sizeimage = bytesperline * height;

		/* Chroma plane size. */
		sizeimage += bytesperline * height / 2;

		break;
	}

	pix_fmt->width = width;
	pix_fmt->height = height;

	pix_fmt->bytesperline = bytesperline;
	pix_fmt->sizeimage = sizeimage;
}

static int cedrus_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	strscpy(cap->driver, CEDRUS_NAME, sizeof(cap->driver));
	strscpy(cap->card, CEDRUS_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", CEDRUS_NAME);

	return 0;
}

static int cedrus_enum_fmt(struct file *file, struct v4l2_fmtdesc *f,
			   u32 direction)
{
	struct cedrus_ctx *ctx = cedrus_file2ctx(file);
	struct cedrus_dev *dev = ctx->dev;
	unsigned int capabilities = dev->capabilities;
	struct cedrus_format *fmt;
	unsigned int i, index;

	/* Index among formats that match the requested direction. */
	index = 0;

	for (i = 0; i < CEDRUS_FORMATS_COUNT; i++) {
		fmt = &cedrus_formats[i];

		if (fmt->capabilities && (fmt->capabilities & capabilities) !=
		    fmt->capabilities)
			continue;

		if (!(cedrus_formats[i].directions & direction))
			continue;

		if (index == f->index)
			break;

		index++;
	}

	/* Matched format. */
	if (i < CEDRUS_FORMATS_COUNT) {
		f->pixelformat = cedrus_formats[i].pixelformat;

		return 0;
	}

	return -EINVAL;
}

static int cedrus_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	return cedrus_enum_fmt(file, f, CEDRUS_DECODE_DST);
}

static int cedrus_enum_fmt_vid_out(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	return cedrus_enum_fmt(file, f, CEDRUS_DECODE_SRC);
}

static int cedrus_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cedrus_ctx *ctx = cedrus_file2ctx(file);

	/* Fall back to dummy default by lack of hardware configuration. */
	if (!ctx->dst_fmt.width || !ctx->dst_fmt.height) {
		f->fmt.pix.pixelformat = V4L2_PIX_FMT_SUNXI_TILED_NV12;
		cedrus_prepare_format(&f->fmt.pix);

		return 0;
	}

	f->fmt.pix = ctx->dst_fmt;

	return 0;
}

static int cedrus_g_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cedrus_ctx *ctx = cedrus_file2ctx(file);

	/* Fall back to dummy default by lack of hardware configuration. */
	if (!ctx->dst_fmt.width || !ctx->dst_fmt.height) {
		f->fmt.pix.pixelformat = V4L2_PIX_FMT_MPEG2_SLICE;
		f->fmt.pix.sizeimage = SZ_1K;
		cedrus_prepare_format(&f->fmt.pix);

		return 0;
	}

	f->fmt.pix = ctx->src_fmt;

	return 0;
}

static int cedrus_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct cedrus_ctx *ctx = cedrus_file2ctx(file);
	struct cedrus_dev *dev = ctx->dev;
	struct v4l2_pix_format *pix_fmt = &f->fmt.pix;

	if (!cedrus_check_format(pix_fmt->pixelformat, CEDRUS_DECODE_DST,
				 dev->capabilities))
		return -EINVAL;

	cedrus_prepare_format(pix_fmt);

	return 0;
}

static int cedrus_try_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct cedrus_ctx *ctx = cedrus_file2ctx(file);
	struct cedrus_dev *dev = ctx->dev;
	struct v4l2_pix_format *pix_fmt = &f->fmt.pix;

	if (!cedrus_check_format(pix_fmt->pixelformat, CEDRUS_DECODE_SRC,
				 dev->capabilities))
		return -EINVAL;

	/* Source image size has to be provided by userspace. */
	if (pix_fmt->sizeimage == 0)
		return -EINVAL;

	cedrus_prepare_format(pix_fmt);

	return 0;
}

static int cedrus_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cedrus_ctx *ctx = cedrus_file2ctx(file);
	struct cedrus_dev *dev = ctx->dev;
	int ret;

	ret = cedrus_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	ctx->dst_fmt = f->fmt.pix;

	cedrus_dst_format_set(dev, &ctx->dst_fmt);

	return 0;
}

static int cedrus_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cedrus_ctx *ctx = cedrus_file2ctx(file);
	int ret;

	ret = cedrus_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	ctx->src_fmt = f->fmt.pix;

	/* Propagate colorspace information to capture. */
	ctx->dst_fmt.colorspace = f->fmt.pix.colorspace;
	ctx->dst_fmt.xfer_func = f->fmt.pix.xfer_func;
	ctx->dst_fmt.ycbcr_enc = f->fmt.pix.ycbcr_enc;
	ctx->dst_fmt.quantization = f->fmt.pix.quantization;

	return 0;
}

const struct v4l2_ioctl_ops cedrus_ioctl_ops = {
	.vidioc_querycap		= cedrus_querycap,

	.vidioc_enum_fmt_vid_cap	= cedrus_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= cedrus_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= cedrus_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= cedrus_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out	= cedrus_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out		= cedrus_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out		= cedrus_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out		= cedrus_s_fmt_vid_out,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,

	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static int cedrus_queue_setup(struct vb2_queue *vq, unsigned int *nbufs,
			      unsigned int *nplanes, unsigned int sizes[],
			      struct device *alloc_devs[])
{
	struct cedrus_ctx *ctx = vb2_get_drv_priv(vq);
	struct cedrus_dev *dev = ctx->dev;
	struct v4l2_pix_format *pix_fmt;
	u32 directions;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		directions = CEDRUS_DECODE_SRC;
		pix_fmt = &ctx->src_fmt;
	} else {
		directions = CEDRUS_DECODE_DST;
		pix_fmt = &ctx->dst_fmt;
	}

	if (!cedrus_check_format(pix_fmt->pixelformat, directions,
				 dev->capabilities))
		return -EINVAL;

	if (*nplanes) {
		if (sizes[0] < pix_fmt->sizeimage)
			return -EINVAL;
	} else {
		sizes[0] = pix_fmt->sizeimage;
		*nplanes = 1;
	}

	return 0;
}

static void cedrus_queue_cleanup(struct vb2_queue *vq, u32 state)
{
	struct cedrus_ctx *ctx = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *vbuf;
	unsigned long flags;

	for (;;) {
		spin_lock_irqsave(&ctx->dev->irq_lock, flags);

		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		spin_unlock_irqrestore(&ctx->dev->irq_lock, flags);

		if (!vbuf)
			return;

		v4l2_ctrl_request_complete(vbuf->vb2_buf.req_obj.req,
					   &ctx->hdl);
		v4l2_m2m_buf_done(vbuf, state);
	}
}

static int cedrus_buf_init(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct cedrus_ctx *ctx = vb2_get_drv_priv(vq);

	if (!V4L2_TYPE_IS_OUTPUT(vq->type))
		ctx->dst_bufs[vb->index] = vb;

	return 0;
}

static void cedrus_buf_cleanup(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct cedrus_ctx *ctx = vb2_get_drv_priv(vq);

	if (!V4L2_TYPE_IS_OUTPUT(vq->type))
		ctx->dst_bufs[vb->index] = NULL;
}

static int cedrus_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct cedrus_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix_fmt;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		pix_fmt = &ctx->src_fmt;
	else
		pix_fmt = &ctx->dst_fmt;

	if (vb2_plane_size(vb, 0) < pix_fmt->sizeimage)
		return -EINVAL;

	vb2_set_plane_payload(vb, 0, pix_fmt->sizeimage);

	return 0;
}

static int cedrus_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct cedrus_ctx *ctx = vb2_get_drv_priv(vq);
	struct cedrus_dev *dev = ctx->dev;
	int ret = 0;

	switch (ctx->src_fmt.pixelformat) {
	case V4L2_PIX_FMT_MPEG2_SLICE:
		ctx->current_codec = CEDRUS_CODEC_MPEG2;
		break;

	default:
		return -EINVAL;
	}

	if (V4L2_TYPE_IS_OUTPUT(vq->type) &&
	    dev->dec_ops[ctx->current_codec]->start)
		ret = dev->dec_ops[ctx->current_codec]->start(ctx);

	if (ret)
		cedrus_queue_cleanup(vq, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void cedrus_stop_streaming(struct vb2_queue *vq)
{
	struct cedrus_ctx *ctx = vb2_get_drv_priv(vq);
	struct cedrus_dev *dev = ctx->dev;

	if (V4L2_TYPE_IS_OUTPUT(vq->type) &&
	    dev->dec_ops[ctx->current_codec]->stop)
		dev->dec_ops[ctx->current_codec]->stop(ctx);

	cedrus_queue_cleanup(vq, VB2_BUF_STATE_ERROR);
}

static void cedrus_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct cedrus_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static void cedrus_buf_request_complete(struct vb2_buffer *vb)
{
	struct cedrus_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &ctx->hdl);
}

static struct vb2_ops cedrus_qops = {
	.queue_setup		= cedrus_queue_setup,
	.buf_prepare		= cedrus_buf_prepare,
	.buf_init		= cedrus_buf_init,
	.buf_cleanup		= cedrus_buf_cleanup,
	.buf_queue		= cedrus_buf_queue,
	.buf_request_complete	= cedrus_buf_request_complete,
	.start_streaming	= cedrus_start_streaming,
	.stop_streaming		= cedrus_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

int cedrus_queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct cedrus_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct cedrus_buffer);
	src_vq->min_buffers_needed = 1;
	src_vq->ops = &cedrus_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->dev->dev_mutex;
	src_vq->dev = ctx->dev->dev;
	src_vq->supports_requests = true;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct cedrus_buffer);
	dst_vq->min_buffers_needed = 1;
	dst_vq->ops = &cedrus_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->dev->dev_mutex;
	dst_vq->dev = ctx->dev->dev;

	return vb2_queue_init(dst_vq);
}
