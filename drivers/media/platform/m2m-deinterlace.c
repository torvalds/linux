/*
 * V4L2 deinterlacing support.
 *
 * Copyright (c) 2012 Vista Silicon S.L.
 * Javier Martin <javier.martin@vista-silicon.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>

#include <media/v4l2-mem2mem.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>

#define MEM2MEM_TEST_MODULE_NAME "mem2mem-deinterlace"

MODULE_DESCRIPTION("mem2mem device which supports deinterlacing using dmaengine");
MODULE_AUTHOR("Javier Martin <javier.martin@vista-silicon.com");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.0.1");

static bool debug;
module_param(debug, bool, 0644);

/* Flags that indicate a format can be used for capture/output */
#define MEM2MEM_CAPTURE	(1 << 0)
#define MEM2MEM_OUTPUT	(1 << 1)

#define MEM2MEM_NAME		"m2m-deinterlace"

#define dprintk(dev, fmt, arg...) \
	v4l2_dbg(1, debug, &dev->v4l2_dev, "%s: " fmt, __func__, ## arg)

struct deinterlace_fmt {
	char	*name;
	u32	fourcc;
	/* Types the format can be used for */
	u32	types;
};

static struct deinterlace_fmt formats[] = {
	{
		.name	= "YUV 4:2:0 Planar",
		.fourcc	= V4L2_PIX_FMT_YUV420,
		.types	= MEM2MEM_CAPTURE | MEM2MEM_OUTPUT,
	},
	{
		.name	= "YUYV 4:2:2",
		.fourcc	= V4L2_PIX_FMT_YUYV,
		.types	= MEM2MEM_CAPTURE | MEM2MEM_OUTPUT,
	},
};

#define NUM_FORMATS ARRAY_SIZE(formats)

/* Per-queue, driver-specific private data */
struct deinterlace_q_data {
	unsigned int		width;
	unsigned int		height;
	unsigned int		sizeimage;
	struct deinterlace_fmt	*fmt;
	enum v4l2_field		field;
};

enum {
	V4L2_M2M_SRC = 0,
	V4L2_M2M_DST = 1,
};

enum {
	YUV420_DMA_Y_ODD,
	YUV420_DMA_Y_EVEN,
	YUV420_DMA_U_ODD,
	YUV420_DMA_U_EVEN,
	YUV420_DMA_V_ODD,
	YUV420_DMA_V_EVEN,
	YUV420_DMA_Y_ODD_DOUBLING,
	YUV420_DMA_U_ODD_DOUBLING,
	YUV420_DMA_V_ODD_DOUBLING,
	YUYV_DMA_ODD,
	YUYV_DMA_EVEN,
	YUYV_DMA_EVEN_DOUBLING,
};

/* Source and destination queue data */
static struct deinterlace_q_data q_data[2];

static struct deinterlace_q_data *get_q_data(enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return &q_data[V4L2_M2M_SRC];
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &q_data[V4L2_M2M_DST];
	default:
		BUG();
	}
	return NULL;
}

static struct deinterlace_fmt *find_format(struct v4l2_format *f)
{
	struct deinterlace_fmt *fmt;
	unsigned int k;

	for (k = 0; k < NUM_FORMATS; k++) {
		fmt = &formats[k];
		if ((fmt->types & f->type) &&
			(fmt->fourcc == f->fmt.pix.pixelformat))
			break;
	}

	if (k == NUM_FORMATS)
		return NULL;

	return &formats[k];
}

struct deinterlace_dev {
	struct v4l2_device	v4l2_dev;
	struct video_device	vfd;

	atomic_t		busy;
	struct mutex		dev_mutex;
	spinlock_t		irqlock;

	struct dma_chan		*dma_chan;

	struct v4l2_m2m_dev	*m2m_dev;
};

struct deinterlace_ctx {
	struct deinterlace_dev	*dev;

	/* Abort requested by m2m */
	int			aborting;
	enum v4l2_colorspace	colorspace;
	dma_cookie_t		cookie;
	struct v4l2_m2m_ctx	*m2m_ctx;
	struct dma_interleaved_template *xt;
};

/*
 * mem2mem callbacks
 */
static int deinterlace_job_ready(void *priv)
{
	struct deinterlace_ctx *ctx = priv;
	struct deinterlace_dev *pcdev = ctx->dev;

	if ((v4l2_m2m_num_src_bufs_ready(ctx->m2m_ctx) > 0)
	    && (v4l2_m2m_num_dst_bufs_ready(ctx->m2m_ctx) > 0)
	    && (atomic_read(&ctx->dev->busy) == 0)) {
		dprintk(pcdev, "Task ready\n");
		return 1;
	}

	dprintk(pcdev, "Task not ready to run\n");

	return 0;
}

static void deinterlace_job_abort(void *priv)
{
	struct deinterlace_ctx *ctx = priv;
	struct deinterlace_dev *pcdev = ctx->dev;

	ctx->aborting = 1;

	dprintk(pcdev, "Aborting task\n");

	v4l2_m2m_job_finish(pcdev->m2m_dev, ctx->m2m_ctx);
}

static void dma_callback(void *data)
{
	struct deinterlace_ctx *curr_ctx = data;
	struct deinterlace_dev *pcdev = curr_ctx->dev;
	struct vb2_v4l2_buffer *src_vb, *dst_vb;

	atomic_set(&pcdev->busy, 0);

	src_vb = v4l2_m2m_src_buf_remove(curr_ctx->m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(curr_ctx->m2m_ctx);

	dst_vb->vb2_buf.timestamp = src_vb->vb2_buf.timestamp;
	dst_vb->flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	dst_vb->flags |=
		src_vb->flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	dst_vb->timecode = src_vb->timecode;

	v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_DONE);
	v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_DONE);

	v4l2_m2m_job_finish(pcdev->m2m_dev, curr_ctx->m2m_ctx);

	dprintk(pcdev, "dma transfers completed.\n");
}

static void deinterlace_issue_dma(struct deinterlace_ctx *ctx, int op,
				  int do_callback)
{
	struct deinterlace_q_data *s_q_data;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct deinterlace_dev *pcdev = ctx->dev;
	struct dma_chan *chan = pcdev->dma_chan;
	struct dma_device *dmadev = chan->device;
	struct dma_async_tx_descriptor *tx;
	unsigned int s_width, s_height;
	unsigned int s_size;
	dma_addr_t p_in, p_out;
	enum dma_ctrl_flags flags;

	src_buf = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);

	s_q_data = get_q_data(V4L2_BUF_TYPE_VIDEO_OUTPUT);
	s_width	= s_q_data->width;
	s_height = s_q_data->height;
	s_size = s_width * s_height;

	p_in = (dma_addr_t)vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	p_out = (dma_addr_t)vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf,
							  0);
	if (!p_in || !p_out) {
		v4l2_err(&pcdev->v4l2_dev,
			 "Acquiring kernel pointers to buffers failed\n");
		return;
	}

	switch (op) {
	case YUV420_DMA_Y_ODD:
		ctx->xt->numf = s_height / 2;
		ctx->xt->sgl[0].size = s_width;
		ctx->xt->sgl[0].icg = s_width;
		ctx->xt->src_start = p_in;
		ctx->xt->dst_start = p_out;
		break;
	case YUV420_DMA_Y_EVEN:
		ctx->xt->numf = s_height / 2;
		ctx->xt->sgl[0].size = s_width;
		ctx->xt->sgl[0].icg = s_width;
		ctx->xt->src_start = p_in + s_size / 2;
		ctx->xt->dst_start = p_out + s_width;
		break;
	case YUV420_DMA_U_ODD:
		ctx->xt->numf = s_height / 4;
		ctx->xt->sgl[0].size = s_width / 2;
		ctx->xt->sgl[0].icg = s_width / 2;
		ctx->xt->src_start = p_in + s_size;
		ctx->xt->dst_start = p_out + s_size;
		break;
	case YUV420_DMA_U_EVEN:
		ctx->xt->numf = s_height / 4;
		ctx->xt->sgl[0].size = s_width / 2;
		ctx->xt->sgl[0].icg = s_width / 2;
		ctx->xt->src_start = p_in + (9 * s_size) / 8;
		ctx->xt->dst_start = p_out + s_size + s_width / 2;
		break;
	case YUV420_DMA_V_ODD:
		ctx->xt->numf = s_height / 4;
		ctx->xt->sgl[0].size = s_width / 2;
		ctx->xt->sgl[0].icg = s_width / 2;
		ctx->xt->src_start = p_in + (5 * s_size) / 4;
		ctx->xt->dst_start = p_out + (5 * s_size) / 4;
		break;
	case YUV420_DMA_V_EVEN:
		ctx->xt->numf = s_height / 4;
		ctx->xt->sgl[0].size = s_width / 2;
		ctx->xt->sgl[0].icg = s_width / 2;
		ctx->xt->src_start = p_in + (11 * s_size) / 8;
		ctx->xt->dst_start = p_out + (5 * s_size) / 4 + s_width / 2;
		break;
	case YUV420_DMA_Y_ODD_DOUBLING:
		ctx->xt->numf = s_height / 2;
		ctx->xt->sgl[0].size = s_width;
		ctx->xt->sgl[0].icg = s_width;
		ctx->xt->src_start = p_in;
		ctx->xt->dst_start = p_out + s_width;
		break;
	case YUV420_DMA_U_ODD_DOUBLING:
		ctx->xt->numf = s_height / 4;
		ctx->xt->sgl[0].size = s_width / 2;
		ctx->xt->sgl[0].icg = s_width / 2;
		ctx->xt->src_start = p_in + s_size;
		ctx->xt->dst_start = p_out + s_size + s_width / 2;
		break;
	case YUV420_DMA_V_ODD_DOUBLING:
		ctx->xt->numf = s_height / 4;
		ctx->xt->sgl[0].size = s_width / 2;
		ctx->xt->sgl[0].icg = s_width / 2;
		ctx->xt->src_start = p_in + (5 * s_size) / 4;
		ctx->xt->dst_start = p_out + (5 * s_size) / 4 + s_width / 2;
		break;
	case YUYV_DMA_ODD:
		ctx->xt->numf = s_height / 2;
		ctx->xt->sgl[0].size = s_width * 2;
		ctx->xt->sgl[0].icg = s_width * 2;
		ctx->xt->src_start = p_in;
		ctx->xt->dst_start = p_out;
		break;
	case YUYV_DMA_EVEN:
		ctx->xt->numf = s_height / 2;
		ctx->xt->sgl[0].size = s_width * 2;
		ctx->xt->sgl[0].icg = s_width * 2;
		ctx->xt->src_start = p_in + s_size;
		ctx->xt->dst_start = p_out + s_width * 2;
		break;
	case YUYV_DMA_EVEN_DOUBLING:
	default:
		ctx->xt->numf = s_height / 2;
		ctx->xt->sgl[0].size = s_width * 2;
		ctx->xt->sgl[0].icg = s_width * 2;
		ctx->xt->src_start = p_in;
		ctx->xt->dst_start = p_out + s_width * 2;
		break;
	}

	/* Common parameters for al transfers */
	ctx->xt->frame_size = 1;
	ctx->xt->dir = DMA_MEM_TO_MEM;
	ctx->xt->src_sgl = false;
	ctx->xt->dst_sgl = true;
	flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;

	tx = dmadev->device_prep_interleaved_dma(chan, ctx->xt, flags);
	if (tx == NULL) {
		v4l2_warn(&pcdev->v4l2_dev, "DMA interleaved prep error\n");
		return;
	}

	if (do_callback) {
		tx->callback = dma_callback;
		tx->callback_param = ctx;
	}

	ctx->cookie = dmaengine_submit(tx);
	if (dma_submit_error(ctx->cookie)) {
		v4l2_warn(&pcdev->v4l2_dev,
			  "DMA submit error %d with src=0x%x dst=0x%x len=0x%x\n",
			  ctx->cookie, (unsigned)p_in, (unsigned)p_out,
			  s_size * 3/2);
		return;
	}

	dma_async_issue_pending(chan);
}

static void deinterlace_device_run(void *priv)
{
	struct deinterlace_ctx *ctx = priv;
	struct deinterlace_q_data *dst_q_data;

	atomic_set(&ctx->dev->busy, 1);

	dprintk(ctx->dev, "%s: DMA try issue.\n", __func__);

	dst_q_data = get_q_data(V4L2_BUF_TYPE_VIDEO_CAPTURE);

	/*
	 * 4 possible field conversions are possible at the moment:
	 *  V4L2_FIELD_SEQ_TB --> V4L2_FIELD_INTERLACED_TB:
	 *	two separate fields in the same input buffer are interlaced
	 *	in the output buffer using weaving. Top field comes first.
	 *  V4L2_FIELD_SEQ_TB --> V4L2_FIELD_NONE:
	 *	top field from the input buffer is copied to the output buffer
	 *	using line doubling. Bottom field from the input buffer is discarded.
	 * V4L2_FIELD_SEQ_BT --> V4L2_FIELD_INTERLACED_BT:
	 *	two separate fields in the same input buffer are interlaced
	 *	in the output buffer using weaving. Bottom field comes first.
	 * V4L2_FIELD_SEQ_BT --> V4L2_FIELD_NONE:
	 *	bottom field from the input buffer is copied to the output buffer
	 *	using line doubling. Top field from the input buffer is discarded.
	 */
	switch (dst_q_data->fmt->fourcc) {
	case V4L2_PIX_FMT_YUV420:
		switch (dst_q_data->field) {
		case V4L2_FIELD_INTERLACED_TB:
		case V4L2_FIELD_INTERLACED_BT:
			dprintk(ctx->dev, "%s: yuv420 interlaced tb.\n",
				__func__);
			deinterlace_issue_dma(ctx, YUV420_DMA_Y_ODD, 0);
			deinterlace_issue_dma(ctx, YUV420_DMA_Y_EVEN, 0);
			deinterlace_issue_dma(ctx, YUV420_DMA_U_ODD, 0);
			deinterlace_issue_dma(ctx, YUV420_DMA_U_EVEN, 0);
			deinterlace_issue_dma(ctx, YUV420_DMA_V_ODD, 0);
			deinterlace_issue_dma(ctx, YUV420_DMA_V_EVEN, 1);
			break;
		case V4L2_FIELD_NONE:
		default:
			dprintk(ctx->dev, "%s: yuv420 interlaced line doubling.\n",
				__func__);
			deinterlace_issue_dma(ctx, YUV420_DMA_Y_ODD, 0);
			deinterlace_issue_dma(ctx, YUV420_DMA_Y_ODD_DOUBLING, 0);
			deinterlace_issue_dma(ctx, YUV420_DMA_U_ODD, 0);
			deinterlace_issue_dma(ctx, YUV420_DMA_U_ODD_DOUBLING, 0);
			deinterlace_issue_dma(ctx, YUV420_DMA_V_ODD, 0);
			deinterlace_issue_dma(ctx, YUV420_DMA_V_ODD_DOUBLING, 1);
			break;
		}
		break;
	case V4L2_PIX_FMT_YUYV:
	default:
		switch (dst_q_data->field) {
		case V4L2_FIELD_INTERLACED_TB:
		case V4L2_FIELD_INTERLACED_BT:
			dprintk(ctx->dev, "%s: yuyv interlaced_tb.\n",
				__func__);
			deinterlace_issue_dma(ctx, YUYV_DMA_ODD, 0);
			deinterlace_issue_dma(ctx, YUYV_DMA_EVEN, 1);
			break;
		case V4L2_FIELD_NONE:
		default:
			dprintk(ctx->dev, "%s: yuyv interlaced line doubling.\n",
				__func__);
			deinterlace_issue_dma(ctx, YUYV_DMA_ODD, 0);
			deinterlace_issue_dma(ctx, YUYV_DMA_EVEN_DOUBLING, 1);
			break;
		}
		break;
	}

	dprintk(ctx->dev, "%s: DMA issue done.\n", __func__);
}

/*
 * video ioctls
 */
static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	strlcpy(cap->driver, MEM2MEM_NAME, sizeof(cap->driver));
	strlcpy(cap->card, MEM2MEM_NAME, sizeof(cap->card));
	strlcpy(cap->bus_info, MEM2MEM_NAME, sizeof(cap->card));
	/*
	 * This is only a mem-to-mem video device. The capture and output
	 * device capability flags are left only for backward compatibility
	 * and are scheduled for removal.
	 */
	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT |
			   V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int enum_fmt(struct v4l2_fmtdesc *f, u32 type)
{
	int i, num;
	struct deinterlace_fmt *fmt;

	num = 0;

	for (i = 0; i < NUM_FORMATS; ++i) {
		if (formats[i].types & type) {
			/* index-th format of type type found ? */
			if (num == f->index)
				break;
			/* Correct type but haven't reached our index yet,
			 * just increment per-type index */
			++num;
		}
	}

	if (i < NUM_FORMATS) {
		/* Format found */
		fmt = &formats[i];
		strlcpy(f->description, fmt->name, sizeof(f->description));
		f->pixelformat = fmt->fourcc;
		return 0;
	}

	/* Format not found */
	return -EINVAL;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	return enum_fmt(f, MEM2MEM_CAPTURE);
}

static int vidioc_enum_fmt_vid_out(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	return enum_fmt(f, MEM2MEM_OUTPUT);
}

static int vidioc_g_fmt(struct deinterlace_ctx *ctx, struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct deinterlace_q_data *q_data;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(f->type);

	f->fmt.pix.width	= q_data->width;
	f->fmt.pix.height	= q_data->height;
	f->fmt.pix.field	= q_data->field;
	f->fmt.pix.pixelformat	= q_data->fmt->fourcc;

	switch (q_data->fmt->fourcc) {
	case V4L2_PIX_FMT_YUV420:
		f->fmt.pix.bytesperline = q_data->width * 3 / 2;
		break;
	case V4L2_PIX_FMT_YUYV:
	default:
		f->fmt.pix.bytesperline = q_data->width * 2;
	}

	f->fmt.pix.sizeimage	= q_data->sizeimage;
	f->fmt.pix.colorspace	= ctx->colorspace;

	return 0;
}

static int vidioc_g_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	return vidioc_g_fmt(priv, f);
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	return vidioc_g_fmt(priv, f);
}

static int vidioc_try_fmt(struct v4l2_format *f, struct deinterlace_fmt *fmt)
{
	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_YUV420:
		f->fmt.pix.bytesperline = f->fmt.pix.width * 3 / 2;
		break;
	case V4L2_PIX_FMT_YUYV:
	default:
		f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	}
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct deinterlace_fmt *fmt;
	struct deinterlace_ctx *ctx = priv;

	fmt = find_format(f);
	if (!fmt || !(fmt->types & MEM2MEM_CAPTURE))
		f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;

	f->fmt.pix.colorspace = ctx->colorspace;

	if (f->fmt.pix.field != V4L2_FIELD_INTERLACED_TB &&
	    f->fmt.pix.field != V4L2_FIELD_INTERLACED_BT &&
	    f->fmt.pix.field != V4L2_FIELD_NONE)
		f->fmt.pix.field = V4L2_FIELD_INTERLACED_TB;

	return vidioc_try_fmt(f, fmt);
}

static int vidioc_try_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct deinterlace_fmt *fmt;

	fmt = find_format(f);
	if (!fmt || !(fmt->types & MEM2MEM_OUTPUT))
		f->fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;

	if (!f->fmt.pix.colorspace)
		f->fmt.pix.colorspace = V4L2_COLORSPACE_REC709;

	if (f->fmt.pix.field != V4L2_FIELD_SEQ_TB &&
	    f->fmt.pix.field != V4L2_FIELD_SEQ_BT)
		f->fmt.pix.field = V4L2_FIELD_SEQ_TB;

	return vidioc_try_fmt(f, fmt);
}

static int vidioc_s_fmt(struct deinterlace_ctx *ctx, struct v4l2_format *f)
{
	struct deinterlace_q_data *q_data;
	struct vb2_queue *vq;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(f->type);
	if (!q_data)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		v4l2_err(&ctx->dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	q_data->fmt = find_format(f);
	if (!q_data->fmt) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "Couldn't set format type %d, wxh: %dx%d. fmt: %d, field: %d\n",
			f->type, f->fmt.pix.width, f->fmt.pix.height,
			f->fmt.pix.pixelformat, f->fmt.pix.field);
		return -EINVAL;
	}

	q_data->width		= f->fmt.pix.width;
	q_data->height		= f->fmt.pix.height;
	q_data->field		= f->fmt.pix.field;

	switch (f->fmt.pix.pixelformat) {
	case V4L2_PIX_FMT_YUV420:
		f->fmt.pix.bytesperline = f->fmt.pix.width * 3 / 2;
		q_data->sizeimage = (q_data->width * q_data->height * 3) / 2;
		break;
	case V4L2_PIX_FMT_YUYV:
	default:
		f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
		q_data->sizeimage = q_data->width * q_data->height * 2;
	}

	dprintk(ctx->dev,
		"Setting format for type %d, wxh: %dx%d, fmt: %d, field: %d\n",
		f->type, q_data->width, q_data->height, q_data->fmt->fourcc,
		q_data->field);

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	int ret;

	ret = vidioc_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;
	return vidioc_s_fmt(priv, f);
}

static int vidioc_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct deinterlace_ctx *ctx = priv;
	int ret;

	ret = vidioc_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	ret = vidioc_s_fmt(priv, f);
	if (!ret)
		ctx->colorspace = f->fmt.pix.colorspace;

	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *reqbufs)
{
	struct deinterlace_ctx *ctx = priv;

	return v4l2_m2m_reqbufs(file, ctx->m2m_ctx, reqbufs);
}

static int vidioc_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct deinterlace_ctx *ctx = priv;

	return v4l2_m2m_querybuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct deinterlace_ctx *ctx = priv;

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct deinterlace_ctx *ctx = priv;

	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type type)
{
	struct deinterlace_q_data *s_q_data, *d_q_data;
	struct deinterlace_ctx *ctx = priv;

	s_q_data = get_q_data(V4L2_BUF_TYPE_VIDEO_OUTPUT);
	d_q_data = get_q_data(V4L2_BUF_TYPE_VIDEO_CAPTURE);

	/* Check that src and dst queues have the same pix format */
	if (s_q_data->fmt->fourcc != d_q_data->fmt->fourcc) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "src and dst formats don't match.\n");
		return -EINVAL;
	}

	/* Check that input and output deinterlacing types are compatible */
	switch (s_q_data->field) {
	case V4L2_FIELD_SEQ_BT:
		if (d_q_data->field != V4L2_FIELD_NONE &&
			d_q_data->field != V4L2_FIELD_INTERLACED_BT) {
			v4l2_err(&ctx->dev->v4l2_dev,
			 "src and dst field conversion [(%d)->(%d)] not supported.\n",
				s_q_data->field, d_q_data->field);
			return -EINVAL;
		}
		break;
	case V4L2_FIELD_SEQ_TB:
		if (d_q_data->field != V4L2_FIELD_NONE &&
			d_q_data->field != V4L2_FIELD_INTERLACED_TB) {
			v4l2_err(&ctx->dev->v4l2_dev,
			 "src and dst field conversion [(%d)->(%d)] not supported.\n",
				s_q_data->field, d_q_data->field);
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return v4l2_m2m_streamon(file, ctx->m2m_ctx, type);
}

static int vidioc_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct deinterlace_ctx *ctx = priv;

	return v4l2_m2m_streamoff(file, ctx->m2m_ctx, type);
}

static const struct v4l2_ioctl_ops deinterlace_ioctl_ops = {
	.vidioc_querycap	= vidioc_querycap,

	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	= vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	= vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= vidioc_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out = vidioc_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out	= vidioc_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out	= vidioc_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out	= vidioc_s_fmt_vid_out,

	.vidioc_reqbufs		= vidioc_reqbufs,
	.vidioc_querybuf	= vidioc_querybuf,

	.vidioc_qbuf		= vidioc_qbuf,
	.vidioc_dqbuf		= vidioc_dqbuf,

	.vidioc_streamon	= vidioc_streamon,
	.vidioc_streamoff	= vidioc_streamoff,
};


/*
 * Queue operations
 */
struct vb2_dc_conf {
	struct device           *dev;
};

static int deinterlace_queue_setup(struct vb2_queue *vq,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], struct device *alloc_devs[])
{
	struct deinterlace_ctx *ctx = vb2_get_drv_priv(vq);
	struct deinterlace_q_data *q_data;
	unsigned int size, count = *nbuffers;

	q_data = get_q_data(vq->type);

	switch (q_data->fmt->fourcc) {
	case V4L2_PIX_FMT_YUV420:
		size = q_data->width * q_data->height * 3 / 2;
		break;
	case V4L2_PIX_FMT_YUYV:
	default:
		size = q_data->width * q_data->height * 2;
	}

	*nplanes = 1;
	*nbuffers = count;
	sizes[0] = size;

	dprintk(ctx->dev, "get %d buffer(s) of size %d each.\n", count, size);

	return 0;
}

static int deinterlace_buf_prepare(struct vb2_buffer *vb)
{
	struct deinterlace_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct deinterlace_q_data *q_data;

	dprintk(ctx->dev, "type: %d\n", vb->vb2_queue->type);

	q_data = get_q_data(vb->vb2_queue->type);

	if (vb2_plane_size(vb, 0) < q_data->sizeimage) {
		dprintk(ctx->dev, "%s data will not fit into plane (%lu < %lu)\n",
			__func__, vb2_plane_size(vb, 0), (long)q_data->sizeimage);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, q_data->sizeimage);

	return 0;
}

static void deinterlace_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct deinterlace_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->m2m_ctx, vbuf);
}

static const struct vb2_ops deinterlace_qops = {
	.queue_setup	 = deinterlace_queue_setup,
	.buf_prepare	 = deinterlace_buf_prepare,
	.buf_queue	 = deinterlace_buf_queue,
	.wait_prepare	 = vb2_ops_wait_prepare,
	.wait_finish	 = vb2_ops_wait_finish,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct deinterlace_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &deinterlace_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->dev = ctx->dev->v4l2_dev.dev;
	src_vq->lock = &ctx->dev->dev_mutex;
	q_data[V4L2_M2M_SRC].fmt = &formats[0];
	q_data[V4L2_M2M_SRC].width = 640;
	q_data[V4L2_M2M_SRC].height = 480;
	q_data[V4L2_M2M_SRC].sizeimage = (640 * 480 * 3) / 2;
	q_data[V4L2_M2M_SRC].field = V4L2_FIELD_SEQ_TB;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &deinterlace_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->dev = ctx->dev->v4l2_dev.dev;
	dst_vq->lock = &ctx->dev->dev_mutex;
	q_data[V4L2_M2M_DST].fmt = &formats[0];
	q_data[V4L2_M2M_DST].width = 640;
	q_data[V4L2_M2M_DST].height = 480;
	q_data[V4L2_M2M_DST].sizeimage = (640 * 480 * 3) / 2;
	q_data[V4L2_M2M_SRC].field = V4L2_FIELD_INTERLACED_TB;

	return vb2_queue_init(dst_vq);
}

/*
 * File operations
 */
static int deinterlace_open(struct file *file)
{
	struct deinterlace_dev *pcdev = video_drvdata(file);
	struct deinterlace_ctx *ctx = NULL;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	file->private_data = ctx;
	ctx->dev = pcdev;

	ctx->m2m_ctx = v4l2_m2m_ctx_init(pcdev->m2m_dev, ctx, &queue_init);
	if (IS_ERR(ctx->m2m_ctx)) {
		int ret = PTR_ERR(ctx->m2m_ctx);

		kfree(ctx);
		return ret;
	}

	ctx->xt = kzalloc(sizeof(struct dma_interleaved_template) +
				sizeof(struct data_chunk), GFP_KERNEL);
	if (!ctx->xt) {
		kfree(ctx);
		return -ENOMEM;
	}

	ctx->colorspace = V4L2_COLORSPACE_REC709;

	dprintk(pcdev, "Created instance %p, m2m_ctx: %p\n", ctx, ctx->m2m_ctx);

	return 0;
}

static int deinterlace_release(struct file *file)
{
	struct deinterlace_dev *pcdev = video_drvdata(file);
	struct deinterlace_ctx *ctx = file->private_data;

	dprintk(pcdev, "Releasing instance %p\n", ctx);

	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	kfree(ctx->xt);
	kfree(ctx);

	return 0;
}

static __poll_t deinterlace_poll(struct file *file,
				 struct poll_table_struct *wait)
{
	struct deinterlace_ctx *ctx = file->private_data;
	__poll_t ret;

	mutex_lock(&ctx->dev->dev_mutex);
	ret = v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
	mutex_unlock(&ctx->dev->dev_mutex);

	return ret;
}

static int deinterlace_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct deinterlace_ctx *ctx = file->private_data;

	return v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
}

static const struct v4l2_file_operations deinterlace_fops = {
	.owner		= THIS_MODULE,
	.open		= deinterlace_open,
	.release	= deinterlace_release,
	.poll		= deinterlace_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= deinterlace_mmap,
};

static const struct video_device deinterlace_videodev = {
	.name		= MEM2MEM_NAME,
	.fops		= &deinterlace_fops,
	.ioctl_ops	= &deinterlace_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release_empty,
	.vfl_dir	= VFL_DIR_M2M,
};

static const struct v4l2_m2m_ops m2m_ops = {
	.device_run	= deinterlace_device_run,
	.job_ready	= deinterlace_job_ready,
	.job_abort	= deinterlace_job_abort,
};

static int deinterlace_probe(struct platform_device *pdev)
{
	struct deinterlace_dev *pcdev;
	struct video_device *vfd;
	dma_cap_mask_t mask;
	int ret = 0;

	pcdev = devm_kzalloc(&pdev->dev, sizeof(*pcdev), GFP_KERNEL);
	if (!pcdev)
		return -ENOMEM;

	spin_lock_init(&pcdev->irqlock);

	dma_cap_zero(mask);
	dma_cap_set(DMA_INTERLEAVE, mask);
	pcdev->dma_chan = dma_request_channel(mask, NULL, pcdev);
	if (!pcdev->dma_chan)
		return -ENODEV;

	if (!dma_has_cap(DMA_INTERLEAVE, pcdev->dma_chan->device->cap_mask)) {
		dev_err(&pdev->dev, "DMA does not support INTERLEAVE\n");
		ret = -ENODEV;
		goto rel_dma;
	}

	ret = v4l2_device_register(&pdev->dev, &pcdev->v4l2_dev);
	if (ret)
		goto rel_dma;

	atomic_set(&pcdev->busy, 0);
	mutex_init(&pcdev->dev_mutex);

	vfd = &pcdev->vfd;
	*vfd = deinterlace_videodev;
	vfd->lock = &pcdev->dev_mutex;
	vfd->v4l2_dev = &pcdev->v4l2_dev;

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, 0);
	if (ret) {
		v4l2_err(&pcdev->v4l2_dev, "Failed to register video device\n");
		goto unreg_dev;
	}

	video_set_drvdata(vfd, pcdev);
	v4l2_info(&pcdev->v4l2_dev, MEM2MEM_TEST_MODULE_NAME
			" Device registered as /dev/video%d\n", vfd->num);

	platform_set_drvdata(pdev, pcdev);

	pcdev->m2m_dev = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(pcdev->m2m_dev)) {
		v4l2_err(&pcdev->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(pcdev->m2m_dev);
		goto err_m2m;
	}

	return 0;

err_m2m:
	video_unregister_device(&pcdev->vfd);
unreg_dev:
	v4l2_device_unregister(&pcdev->v4l2_dev);
rel_dma:
	dma_release_channel(pcdev->dma_chan);

	return ret;
}

static int deinterlace_remove(struct platform_device *pdev)
{
	struct deinterlace_dev *pcdev = platform_get_drvdata(pdev);

	v4l2_info(&pcdev->v4l2_dev, "Removing " MEM2MEM_TEST_MODULE_NAME);
	v4l2_m2m_release(pcdev->m2m_dev);
	video_unregister_device(&pcdev->vfd);
	v4l2_device_unregister(&pcdev->v4l2_dev);
	dma_release_channel(pcdev->dma_chan);

	return 0;
}

static struct platform_driver deinterlace_pdrv = {
	.probe		= deinterlace_probe,
	.remove		= deinterlace_remove,
	.driver		= {
		.name	= MEM2MEM_NAME,
	},
};
module_platform_driver(deinterlace_pdrv);

