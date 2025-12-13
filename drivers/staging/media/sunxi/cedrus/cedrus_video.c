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

#include <linux/pm_runtime.h>

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
#define CEDRUS_MAX_WIDTH	4096U
#define CEDRUS_MAX_HEIGHT	2304U

static struct cedrus_format cedrus_formats[] = {
	{
		.pixelformat	= V4L2_PIX_FMT_MPEG2_SLICE,
		.directions	= CEDRUS_DECODE_SRC,
		.capabilities	= CEDRUS_CAPABILITY_MPEG2_DEC,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_H264_SLICE,
		.directions	= CEDRUS_DECODE_SRC,
		.capabilities	= CEDRUS_CAPABILITY_H264_DEC,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_HEVC_SLICE,
		.directions	= CEDRUS_DECODE_SRC,
		.capabilities	= CEDRUS_CAPABILITY_H265_DEC,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_VP8_FRAME,
		.directions	= CEDRUS_DECODE_SRC,
		.capabilities	= CEDRUS_CAPABILITY_VP8_DEC,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_NV12,
		.directions	= CEDRUS_DECODE_DST,
		.capabilities	= CEDRUS_CAPABILITY_UNTILED,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_NV12_32L32,
		.directions	= CEDRUS_DECODE_DST,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_NV21,
		.directions	= CEDRUS_DECODE_DST,
		.capabilities	= CEDRUS_CAPABILITY_UNTILED,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_YUV420,
		.directions	= CEDRUS_DECODE_DST,
		.capabilities	= CEDRUS_CAPABILITY_UNTILED,
	},
	{
		.pixelformat	= V4L2_PIX_FMT_YVU420,
		.directions	= CEDRUS_DECODE_DST,
		.capabilities	= CEDRUS_CAPABILITY_UNTILED,
	},
};

#define CEDRUS_FORMATS_COUNT	ARRAY_SIZE(cedrus_formats)

static struct cedrus_format *cedrus_find_format(struct cedrus_ctx *ctx,
						u32 pixelformat, u32 directions)
{
	struct cedrus_format *first_valid_fmt = NULL;
	struct cedrus_format *fmt;
	unsigned int i;

	for (i = 0; i < CEDRUS_FORMATS_COUNT; i++) {
		fmt = &cedrus_formats[i];

		if (!cedrus_is_capable(ctx, fmt->capabilities) ||
		    !(fmt->directions & directions))
			continue;

		if (fmt->pixelformat == pixelformat)
			break;

		if (!first_valid_fmt)
			first_valid_fmt = fmt;
	}

	if (i == CEDRUS_FORMATS_COUNT)
		return first_valid_fmt;

	return &cedrus_formats[i];
}

void cedrus_prepare_format(struct v4l2_pix_format *pix_fmt)
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
	case V4L2_PIX_FMT_H264_SLICE:
	case V4L2_PIX_FMT_HEVC_SLICE:
	case V4L2_PIX_FMT_VP8_FRAME:
		/* Zero bytes per line for encoded source. */
		bytesperline = 0;
		/* Choose some minimum size since this can't be 0 */
		sizeimage = max_t(u32, SZ_1K, sizeimage);
		break;

	case V4L2_PIX_FMT_NV12_32L32:
		/* 32-aligned stride. */
		bytesperline = ALIGN(width, 32);

		/* 32-aligned height. */
		height = ALIGN(height, 32);

		/* Luma plane size. */
		sizeimage = bytesperline * height;

		/* Chroma plane size. */
		sizeimage += bytesperline * ALIGN(height, 64) / 2;

		break;

	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
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
	unsigned int i, index;

	/* Index among formats that match the requested direction. */
	index = 0;

	for (i = 0; i < CEDRUS_FORMATS_COUNT; i++) {
		if (!cedrus_is_capable(ctx, cedrus_formats[i].capabilities))
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

	f->fmt.pix = ctx->dst_fmt;
	return 0;
}

static int cedrus_g_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cedrus_ctx *ctx = cedrus_file2ctx(file);

	f->fmt.pix = ctx->src_fmt;
	return 0;
}

static int cedrus_try_fmt_vid_cap_p(struct cedrus_ctx *ctx,
				    struct v4l2_pix_format *pix_fmt)
{
	struct cedrus_format *fmt =
		cedrus_find_format(ctx, pix_fmt->pixelformat,
				   CEDRUS_DECODE_DST);

	if (!fmt)
		return -EINVAL;

	pix_fmt->pixelformat = fmt->pixelformat;
	pix_fmt->width = ctx->src_fmt.width;
	pix_fmt->height = ctx->src_fmt.height;
	cedrus_prepare_format(pix_fmt);

	if (ctx->current_codec->extra_cap_size)
		pix_fmt->sizeimage +=
			ctx->current_codec->extra_cap_size(ctx, pix_fmt);

	return 0;
}

static int cedrus_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	return cedrus_try_fmt_vid_cap_p(cedrus_file2ctx(file), &f->fmt.pix);
}

static int cedrus_try_fmt_vid_out_p(struct cedrus_ctx *ctx,
				    struct v4l2_pix_format *pix_fmt)
{
	struct cedrus_format *fmt =
		cedrus_find_format(ctx, pix_fmt->pixelformat,
				   CEDRUS_DECODE_SRC);

	if (!fmt)
		return -EINVAL;

	pix_fmt->pixelformat = fmt->pixelformat;
	cedrus_prepare_format(pix_fmt);

	return 0;
}

static int cedrus_try_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	return cedrus_try_fmt_vid_out_p(cedrus_file2ctx(file), &f->fmt.pix);
}

static int cedrus_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cedrus_ctx *ctx = cedrus_file2ctx(file);
	struct vb2_queue *vq;
	int ret;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_busy(vq))
		return -EBUSY;

	ret = cedrus_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	ctx->dst_fmt = f->fmt.pix;

	return 0;
}

void cedrus_reset_cap_format(struct cedrus_ctx *ctx)
{
	ctx->dst_fmt.pixelformat = 0;
	cedrus_try_fmt_vid_cap_p(ctx, &ctx->dst_fmt);
}

static int cedrus_s_fmt_vid_out_p(struct cedrus_ctx *ctx,
				  struct v4l2_pix_format *pix_fmt)
{
	struct vb2_queue *vq;
	int ret;

	ret = cedrus_try_fmt_vid_out_p(ctx, pix_fmt);
	if (ret)
		return ret;

	ctx->src_fmt = *pix_fmt;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);

	switch (ctx->src_fmt.pixelformat) {
	case V4L2_PIX_FMT_H264_SLICE:
	case V4L2_PIX_FMT_HEVC_SLICE:
		vq->subsystem_flags |=
			VB2_V4L2_FL_SUPPORTS_M2M_HOLD_CAPTURE_BUF;
		break;
	default:
		vq->subsystem_flags &=
			~VB2_V4L2_FL_SUPPORTS_M2M_HOLD_CAPTURE_BUF;
		break;
	}

	switch (ctx->src_fmt.pixelformat) {
	case V4L2_PIX_FMT_MPEG2_SLICE:
		ctx->current_codec = &cedrus_dec_ops_mpeg2;
		break;
	case V4L2_PIX_FMT_H264_SLICE:
		ctx->current_codec = &cedrus_dec_ops_h264;
		break;
	case V4L2_PIX_FMT_HEVC_SLICE:
		ctx->current_codec = &cedrus_dec_ops_h265;
		break;
	case V4L2_PIX_FMT_VP8_FRAME:
		ctx->current_codec = &cedrus_dec_ops_vp8;
		break;
	}

	/* Propagate format information to capture. */
	ctx->dst_fmt.colorspace = pix_fmt->colorspace;
	ctx->dst_fmt.xfer_func = pix_fmt->xfer_func;
	ctx->dst_fmt.ycbcr_enc = pix_fmt->ycbcr_enc;
	ctx->dst_fmt.quantization = pix_fmt->quantization;
	cedrus_reset_cap_format(ctx);

	return 0;
}

void cedrus_reset_out_format(struct cedrus_ctx *ctx)
{
	ctx->src_fmt.pixelformat = 0;
	cedrus_s_fmt_vid_out_p(ctx, &ctx->src_fmt);
}

static int cedrus_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cedrus_ctx *ctx = cedrus_file2ctx(file);
	struct vb2_queue *vq;
	struct vb2_queue *peer_vq;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	/*
	 * In order to support dynamic resolution change,
	 * the decoder admits a resolution change, as long
	 * as the pixelformat remains. Can't be done if streaming.
	 */
	if (vb2_is_streaming(vq) || (vb2_is_busy(vq) &&
	    f->fmt.pix.pixelformat != ctx->src_fmt.pixelformat))
		return -EBUSY;
	/*
	 * Since format change on the OUTPUT queue will reset
	 * the CAPTURE queue, we can't allow doing so
	 * when the CAPTURE queue has buffers allocated.
	 */
	peer_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
				  V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (vb2_is_busy(peer_vq))
		return -EBUSY;

	return cedrus_s_fmt_vid_out_p(cedrus_file2ctx(file), &f->fmt.pix);
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

	.vidioc_try_decoder_cmd		= v4l2_m2m_ioctl_stateless_try_decoder_cmd,
	.vidioc_decoder_cmd		= v4l2_m2m_ioctl_stateless_decoder_cmd,

	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

static int cedrus_queue_setup(struct vb2_queue *vq, unsigned int *nbufs,
			      unsigned int *nplanes, unsigned int sizes[],
			      struct device *alloc_devs[])
{
	struct cedrus_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix_fmt;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		pix_fmt = &ctx->src_fmt;
	else
		pix_fmt = &ctx->dst_fmt;

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

	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		if (!vbuf)
			return;

		v4l2_ctrl_request_complete(vbuf->vb2_buf.req_obj.req,
					   &ctx->hdl);
		v4l2_m2m_buf_done(vbuf, state);
	}
}

static int cedrus_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;
	return 0;
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

	/*
	 * Buffer's bytesused must be written by driver for CAPTURE buffers.
	 * (for OUTPUT buffers, if userspace passes 0 bytesused, v4l2-core sets
	 * it to buffer length).
	 */
	if (V4L2_TYPE_IS_CAPTURE(vq->type))
		vb2_set_plane_payload(vb, 0, pix_fmt->sizeimage);

	return 0;
}

static int cedrus_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct cedrus_ctx *ctx = vb2_get_drv_priv(vq);
	struct cedrus_dev *dev = ctx->dev;
	int ret = 0;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		ret = pm_runtime_resume_and_get(dev->dev);
		if (ret < 0)
			goto err_cleanup;

		if (ctx->current_codec->start) {
			ret = ctx->current_codec->start(ctx);
			if (ret)
				goto err_pm;
		}
	}

	return 0;

err_pm:
	pm_runtime_put(dev->dev);
err_cleanup:
	cedrus_queue_cleanup(vq, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void cedrus_stop_streaming(struct vb2_queue *vq)
{
	struct cedrus_ctx *ctx = vb2_get_drv_priv(vq);
	struct cedrus_dev *dev = ctx->dev;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		if (ctx->current_codec->stop)
			ctx->current_codec->stop(ctx);

		pm_runtime_put(dev->dev);
	}

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

static const struct vb2_ops cedrus_qops = {
	.queue_setup		= cedrus_queue_setup,
	.buf_prepare		= cedrus_buf_prepare,
	.buf_queue		= cedrus_buf_queue,
	.buf_out_validate	= cedrus_buf_out_validate,
	.buf_request_complete	= cedrus_buf_request_complete,
	.start_streaming	= cedrus_start_streaming,
	.stop_streaming		= cedrus_stop_streaming,
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
	src_vq->ops = &cedrus_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->dev->dev_mutex;
	src_vq->dev = ctx->dev->dev;
	src_vq->supports_requests = true;
	src_vq->requires_requests = true;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct cedrus_buffer);
	dst_vq->ops = &cedrus_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->dev->dev_mutex;
	dst_vq->dev = ctx->dev->dev;

	return vb2_queue_init(dst_vq);
}
