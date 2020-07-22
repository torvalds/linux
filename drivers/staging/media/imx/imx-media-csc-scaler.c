// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * i.MX IPUv3 IC PP mem2mem CSC/Scaler driver
 *
 * Copyright (C) 2011 Pengutronix, Sascha Hauer
 * Copyright (C) 2018 Pengutronix, Philipp Zabel
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <video/imx-ipu-v3.h>
#include <video/imx-ipu-image-convert.h>

#include <media/media-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>

#include "imx-media.h"

#define fh_to_ctx(__fh)	container_of(__fh, struct ipu_csc_scaler_ctx, fh)

enum {
	V4L2_M2M_SRC = 0,
	V4L2_M2M_DST = 1,
};

struct ipu_csc_scaler_priv {
	struct imx_media_video_dev	vdev;

	struct v4l2_m2m_dev		*m2m_dev;
	struct device			*dev;

	struct imx_media_dev		*md;

	struct mutex			mutex;	/* mem2mem device mutex */
};

#define vdev_to_priv(v) container_of(v, struct ipu_csc_scaler_priv, vdev)

/* Per-queue, driver-specific private data */
struct ipu_csc_scaler_q_data {
	struct v4l2_pix_format		cur_fmt;
	struct v4l2_rect		rect;
};

struct ipu_csc_scaler_ctx {
	struct ipu_csc_scaler_priv	*priv;

	struct v4l2_fh			fh;
	struct ipu_csc_scaler_q_data	q_data[2];
	struct ipu_image_convert_ctx	*icc;

	struct v4l2_ctrl_handler	ctrl_hdlr;
	int				rotate;
	bool				hflip;
	bool				vflip;
	enum ipu_rotate_mode		rot_mode;
	unsigned int			sequence;
};

static struct ipu_csc_scaler_q_data *get_q_data(struct ipu_csc_scaler_ctx *ctx,
						enum v4l2_buf_type type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return &ctx->q_data[V4L2_M2M_SRC];
	else
		return &ctx->q_data[V4L2_M2M_DST];
}

/*
 * mem2mem callbacks
 */

static void job_abort(void *_ctx)
{
	struct ipu_csc_scaler_ctx *ctx = _ctx;

	if (ctx->icc)
		ipu_image_convert_abort(ctx->icc);
}

static void ipu_ic_pp_complete(struct ipu_image_convert_run *run, void *_ctx)
{
	struct ipu_csc_scaler_ctx *ctx = _ctx;
	struct ipu_csc_scaler_priv *priv = ctx->priv;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;

	src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

	v4l2_m2m_buf_copy_metadata(src_buf, dst_buf, true);

	src_buf->sequence = ctx->sequence++;
	dst_buf->sequence = src_buf->sequence;

	v4l2_m2m_buf_done(src_buf, run->status ? VB2_BUF_STATE_ERROR :
						 VB2_BUF_STATE_DONE);
	v4l2_m2m_buf_done(dst_buf, run->status ? VB2_BUF_STATE_ERROR :
						 VB2_BUF_STATE_DONE);

	v4l2_m2m_job_finish(priv->m2m_dev, ctx->fh.m2m_ctx);
	kfree(run);
}

static void device_run(void *_ctx)
{
	struct ipu_csc_scaler_ctx *ctx = _ctx;
	struct ipu_csc_scaler_priv *priv = ctx->priv;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct ipu_image_convert_run *run;
	int ret;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	run = kzalloc(sizeof(*run), GFP_KERNEL);
	if (!run)
		goto err;

	run->ctx = ctx->icc;
	run->in_phys = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	run->out_phys = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);

	ret = ipu_image_convert_queue(run);
	if (ret < 0) {
		v4l2_err(ctx->priv->vdev.vfd->v4l2_dev,
			 "%s: failed to queue: %d\n", __func__, ret);
		goto err;
	}

	return;

err:
	v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
	v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
	v4l2_m2m_job_finish(priv->m2m_dev, ctx->fh.m2m_ctx);
}

/*
 * Video ioctls
 */
static int ipu_csc_scaler_querycap(struct file *file, void *priv,
				   struct v4l2_capability *cap)
{
	strscpy(cap->driver, "imx-media-csc-scaler", sizeof(cap->driver));
	strscpy(cap->card, "imx-media-csc-scaler", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:imx-media-csc-scaler",
		sizeof(cap->bus_info));

	return 0;
}

static int ipu_csc_scaler_enum_fmt(struct file *file, void *fh,
				   struct v4l2_fmtdesc *f)
{
	u32 fourcc;
	int ret;

	ret = imx_media_enum_format(&fourcc, f->index, CS_SEL_ANY);
	if (ret)
		return ret;

	f->pixelformat = fourcc;

	return 0;
}

static int ipu_csc_scaler_g_fmt(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct ipu_csc_scaler_ctx *ctx = fh_to_ctx(priv);
	struct ipu_csc_scaler_q_data *q_data;

	q_data = get_q_data(ctx, f->type);

	f->fmt.pix = q_data->cur_fmt;

	return 0;
}

static int ipu_csc_scaler_try_fmt(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct ipu_csc_scaler_ctx *ctx = fh_to_ctx(priv);
	struct ipu_csc_scaler_q_data *q_data = get_q_data(ctx, f->type);
	struct ipu_image test_in, test_out;
	enum v4l2_field field;

	field = f->fmt.pix.field;
	if (field == V4L2_FIELD_ANY)
		field = V4L2_FIELD_NONE;
	else if (field != V4L2_FIELD_NONE)
		return -EINVAL;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		struct ipu_csc_scaler_q_data *q_data_in =
			get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);

		test_out.pix = f->fmt.pix;
		test_in.pix = q_data_in->cur_fmt;
	} else {
		struct ipu_csc_scaler_q_data *q_data_out =
			get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);

		test_in.pix = f->fmt.pix;
		test_out.pix = q_data_out->cur_fmt;
	}

	ipu_image_convert_adjust(&test_in, &test_out, ctx->rot_mode);

	f->fmt.pix = (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) ?
		test_out.pix : test_in.pix;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		f->fmt.pix.colorspace = q_data->cur_fmt.colorspace;
		f->fmt.pix.ycbcr_enc = q_data->cur_fmt.ycbcr_enc;
		f->fmt.pix.xfer_func = q_data->cur_fmt.xfer_func;
		f->fmt.pix.quantization = q_data->cur_fmt.quantization;
	} else if (f->fmt.pix.colorspace == V4L2_COLORSPACE_DEFAULT) {
		f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
		f->fmt.pix.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
		f->fmt.pix.xfer_func = V4L2_XFER_FUNC_DEFAULT;
		f->fmt.pix.quantization = V4L2_QUANTIZATION_DEFAULT;
	}

	return 0;
}

static int ipu_csc_scaler_s_fmt(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct ipu_csc_scaler_q_data *q_data;
	struct ipu_csc_scaler_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq;
	int ret;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_busy(vq)) {
		v4l2_err(ctx->priv->vdev.vfd->v4l2_dev, "%s: queue busy\n",
			 __func__);
		return -EBUSY;
	}

	q_data = get_q_data(ctx, f->type);

	ret = ipu_csc_scaler_try_fmt(file, priv, f);
	if (ret < 0)
		return ret;

	q_data->cur_fmt.width = f->fmt.pix.width;
	q_data->cur_fmt.height = f->fmt.pix.height;
	q_data->cur_fmt.pixelformat = f->fmt.pix.pixelformat;
	q_data->cur_fmt.field = f->fmt.pix.field;
	q_data->cur_fmt.bytesperline = f->fmt.pix.bytesperline;
	q_data->cur_fmt.sizeimage = f->fmt.pix.sizeimage;

	/* Reset cropping/composing rectangle */
	q_data->rect.left = 0;
	q_data->rect.top = 0;
	q_data->rect.width = q_data->cur_fmt.width;
	q_data->rect.height = q_data->cur_fmt.height;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		/* Set colorimetry on the output queue */
		q_data->cur_fmt.colorspace = f->fmt.pix.colorspace;
		q_data->cur_fmt.ycbcr_enc = f->fmt.pix.ycbcr_enc;
		q_data->cur_fmt.xfer_func = f->fmt.pix.xfer_func;
		q_data->cur_fmt.quantization = f->fmt.pix.quantization;
		/* Propagate colorimetry to the capture queue */
		q_data = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
		q_data->cur_fmt.colorspace = f->fmt.pix.colorspace;
		q_data->cur_fmt.ycbcr_enc = f->fmt.pix.ycbcr_enc;
		q_data->cur_fmt.xfer_func = f->fmt.pix.xfer_func;
		q_data->cur_fmt.quantization = f->fmt.pix.quantization;
	}

	/*
	 * TODO: Setting colorimetry on the capture queue is currently not
	 * supported by the V4L2 API
	 */

	return 0;
}

static int ipu_csc_scaler_g_selection(struct file *file, void *priv,
				      struct v4l2_selection *s)
{
	struct ipu_csc_scaler_ctx *ctx = fh_to_ctx(priv);
	struct ipu_csc_scaler_q_data *q_data;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		q_data = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
		break;
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		q_data = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
		break;
	default:
		return -EINVAL;
	}

	if (s->target == V4L2_SEL_TGT_CROP ||
	    s->target == V4L2_SEL_TGT_COMPOSE) {
		s->r = q_data->rect;
	} else {
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = q_data->cur_fmt.width;
		s->r.height = q_data->cur_fmt.height;
	}

	return 0;
}

static int ipu_csc_scaler_s_selection(struct file *file, void *priv,
				      struct v4l2_selection *s)
{
	struct ipu_csc_scaler_ctx *ctx = fh_to_ctx(priv);
	struct ipu_csc_scaler_q_data *q_data;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	q_data = get_q_data(ctx, s->type);

	/* The input's frame width to the IC must be a multiple of 8 pixels
	 * When performing resizing the frame width must be multiple of burst
	 * size - 8 or 16 pixels as defined by CB#_BURST_16 parameter.
	 */
	if (s->flags & V4L2_SEL_FLAG_GE)
		s->r.width = round_up(s->r.width, 8);
	if (s->flags & V4L2_SEL_FLAG_LE)
		s->r.width = round_down(s->r.width, 8);
	s->r.width = clamp_t(unsigned int, s->r.width, 8,
			     round_down(q_data->cur_fmt.width, 8));
	s->r.height = clamp_t(unsigned int, s->r.height, 1,
			      q_data->cur_fmt.height);
	s->r.left = clamp_t(unsigned int, s->r.left, 0,
			    q_data->cur_fmt.width - s->r.width);
	s->r.top = clamp_t(unsigned int, s->r.top, 0,
			   q_data->cur_fmt.height - s->r.height);

	/* V4L2_SEL_FLAG_KEEP_CONFIG is only valid for subdevices */
	q_data->rect = s->r;

	return 0;
}

static const struct v4l2_ioctl_ops ipu_csc_scaler_ioctl_ops = {
	.vidioc_querycap		= ipu_csc_scaler_querycap,

	.vidioc_enum_fmt_vid_cap	= ipu_csc_scaler_enum_fmt,
	.vidioc_g_fmt_vid_cap		= ipu_csc_scaler_g_fmt,
	.vidioc_try_fmt_vid_cap		= ipu_csc_scaler_try_fmt,
	.vidioc_s_fmt_vid_cap		= ipu_csc_scaler_s_fmt,

	.vidioc_enum_fmt_vid_out	= ipu_csc_scaler_enum_fmt,
	.vidioc_g_fmt_vid_out		= ipu_csc_scaler_g_fmt,
	.vidioc_try_fmt_vid_out		= ipu_csc_scaler_try_fmt,
	.vidioc_s_fmt_vid_out		= ipu_csc_scaler_s_fmt,

	.vidioc_g_selection		= ipu_csc_scaler_g_selection,
	.vidioc_s_selection		= ipu_csc_scaler_s_selection,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,

	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,

	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,

	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/*
 * Queue operations
 */

static int ipu_csc_scaler_queue_setup(struct vb2_queue *vq,
				      unsigned int *nbuffers,
				      unsigned int *nplanes,
				      unsigned int sizes[],
				      struct device *alloc_devs[])
{
	struct ipu_csc_scaler_ctx *ctx = vb2_get_drv_priv(vq);
	struct ipu_csc_scaler_q_data *q_data;
	unsigned int size, count = *nbuffers;

	q_data = get_q_data(ctx, vq->type);

	size = q_data->cur_fmt.sizeimage;

	*nbuffers = count;

	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = size;

	dev_dbg(ctx->priv->dev, "get %d buffer(s) of size %d each.\n",
		count, size);

	return 0;
}

static int ipu_csc_scaler_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct ipu_csc_scaler_ctx *ctx = vb2_get_drv_priv(vq);
	struct ipu_csc_scaler_q_data *q_data;
	unsigned long size;

	dev_dbg(ctx->priv->dev, "type: %d\n", vq->type);

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		if (vbuf->field == V4L2_FIELD_ANY)
			vbuf->field = V4L2_FIELD_NONE;
		if (vbuf->field != V4L2_FIELD_NONE) {
			dev_dbg(ctx->priv->dev, "%s: field isn't supported\n",
				__func__);
			return -EINVAL;
		}
	}

	q_data = get_q_data(ctx, vq->type);
	size = q_data->cur_fmt.sizeimage;

	if (vb2_plane_size(vb, 0) < size) {
		dev_dbg(ctx->priv->dev,
			"%s: data will not fit into plane (%lu < %lu)\n",
			__func__, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);

	return 0;
}

static void ipu_csc_scaler_buf_queue(struct vb2_buffer *vb)
{
	struct ipu_csc_scaler_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, to_vb2_v4l2_buffer(vb));
}

static void ipu_image_from_q_data(struct ipu_image *im,
				  struct ipu_csc_scaler_q_data *q_data)
{
	struct v4l2_pix_format *fmt = &q_data->cur_fmt;

	im->pix = *fmt;
	if (fmt->ycbcr_enc == V4L2_YCBCR_ENC_DEFAULT)
		im->pix.ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	if (fmt->quantization == V4L2_QUANTIZATION_DEFAULT)
		im->pix.ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
	im->rect = q_data->rect;
}

static int ipu_csc_scaler_start_streaming(struct vb2_queue *q,
					  unsigned int count)
{
	const enum ipu_ic_task ic_task = IC_TASK_POST_PROCESSOR;
	struct ipu_csc_scaler_ctx *ctx = vb2_get_drv_priv(q);
	struct ipu_csc_scaler_priv *priv = ctx->priv;
	struct ipu_soc *ipu = priv->md->ipu[0];
	struct ipu_csc_scaler_q_data *q_data;
	struct vb2_queue *other_q;
	struct ipu_image in, out;

	other_q = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
				  (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) ?
				  V4L2_BUF_TYPE_VIDEO_OUTPUT :
				  V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (!vb2_is_streaming(other_q))
		return 0;

	if (ctx->icc) {
		v4l2_warn(ctx->priv->vdev.vfd->v4l2_dev, "removing old ICC\n");
		ipu_image_convert_unprepare(ctx->icc);
	}

	q_data = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	ipu_image_from_q_data(&in, q_data);

	q_data = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	ipu_image_from_q_data(&out, q_data);

	ctx->icc = ipu_image_convert_prepare(ipu, ic_task, &in, &out,
					     ctx->rot_mode,
					     ipu_ic_pp_complete, ctx);
	if (IS_ERR(ctx->icc)) {
		struct vb2_v4l2_buffer *buf;
		int ret = PTR_ERR(ctx->icc);

		ctx->icc = NULL;
		v4l2_err(ctx->priv->vdev.vfd->v4l2_dev, "%s: error %d\n",
			 __func__, ret);
		while ((buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_QUEUED);
		while ((buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_QUEUED);
		return ret;
	}

	return 0;
}

static void ipu_csc_scaler_stop_streaming(struct vb2_queue *q)
{
	struct ipu_csc_scaler_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *buf;

	if (ctx->icc) {
		ipu_image_convert_unprepare(ctx->icc);
		ctx->icc = NULL;
	}

	ctx->sequence = 0;

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		while ((buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
	} else {
		while ((buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
	}
}

static const struct vb2_ops ipu_csc_scaler_qops = {
	.queue_setup		= ipu_csc_scaler_queue_setup,
	.buf_prepare		= ipu_csc_scaler_buf_prepare,
	.buf_queue		= ipu_csc_scaler_buf_queue,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
	.start_streaming	= ipu_csc_scaler_start_streaming,
	.stop_streaming		= ipu_csc_scaler_stop_streaming,
};

static int ipu_csc_scaler_queue_init(void *priv, struct vb2_queue *src_vq,
				     struct vb2_queue *dst_vq)
{
	struct ipu_csc_scaler_ctx *ctx = priv;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &ipu_csc_scaler_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->priv->mutex;
	src_vq->dev = ctx->priv->dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &ipu_csc_scaler_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->priv->mutex;
	dst_vq->dev = ctx->priv->dev;

	return vb2_queue_init(dst_vq);
}

static int ipu_csc_scaler_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ipu_csc_scaler_ctx *ctx = container_of(ctrl->handler,
						      struct ipu_csc_scaler_ctx,
						      ctrl_hdlr);
	enum ipu_rotate_mode rot_mode;
	int rotate;
	bool hflip, vflip;
	int ret = 0;

	rotate = ctx->rotate;
	hflip = ctx->hflip;
	vflip = ctx->vflip;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		hflip = ctrl->val;
		break;
	case V4L2_CID_VFLIP:
		vflip = ctrl->val;
		break;
	case V4L2_CID_ROTATE:
		rotate = ctrl->val;
		break;
	default:
		return -EINVAL;
	}

	ret = ipu_degrees_to_rot_mode(&rot_mode, rotate, hflip, vflip);
	if (ret)
		return ret;

	if (rot_mode != ctx->rot_mode) {
		struct v4l2_pix_format *in_fmt, *out_fmt;
		struct ipu_image test_in, test_out;

		in_fmt = &ctx->q_data[V4L2_M2M_SRC].cur_fmt;
		out_fmt = &ctx->q_data[V4L2_M2M_DST].cur_fmt;

		test_in.pix = *in_fmt;
		test_out.pix = *out_fmt;

		if (ipu_rot_mode_is_irt(rot_mode) !=
		    ipu_rot_mode_is_irt(ctx->rot_mode)) {
			/* Switch width & height to keep aspect ratio intact */
			test_out.pix.width = out_fmt->height;
			test_out.pix.height = out_fmt->width;
		}

		ipu_image_convert_adjust(&test_in, &test_out, ctx->rot_mode);

		/* Check if output format needs to be changed */
		if (test_in.pix.width != in_fmt->width ||
		    test_in.pix.height != in_fmt->height ||
		    test_in.pix.bytesperline != in_fmt->bytesperline ||
		    test_in.pix.sizeimage != in_fmt->sizeimage) {
			struct vb2_queue *out_q;

			out_q = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
						V4L2_BUF_TYPE_VIDEO_OUTPUT);
			if (vb2_is_busy(out_q))
				return -EBUSY;
		}

		/* Check if capture format needs to be changed */
		if (test_out.pix.width != out_fmt->width ||
		    test_out.pix.height != out_fmt->height ||
		    test_out.pix.bytesperline != out_fmt->bytesperline ||
		    test_out.pix.sizeimage != out_fmt->sizeimage) {
			struct vb2_queue *cap_q;

			cap_q = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
						V4L2_BUF_TYPE_VIDEO_CAPTURE);
			if (vb2_is_busy(cap_q))
				return -EBUSY;
		}

		*in_fmt = test_in.pix;
		*out_fmt = test_out.pix;

		ctx->rot_mode = rot_mode;
		ctx->rotate = rotate;
		ctx->hflip = hflip;
		ctx->vflip = vflip;
	}

	return 0;
}

static const struct v4l2_ctrl_ops ipu_csc_scaler_ctrl_ops = {
	.s_ctrl = ipu_csc_scaler_s_ctrl,
};

static int ipu_csc_scaler_init_controls(struct ipu_csc_scaler_ctx *ctx)
{
	struct v4l2_ctrl_handler *hdlr = &ctx->ctrl_hdlr;

	v4l2_ctrl_handler_init(hdlr, 3);

	v4l2_ctrl_new_std(hdlr, &ipu_csc_scaler_ctrl_ops, V4L2_CID_HFLIP,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(hdlr, &ipu_csc_scaler_ctrl_ops, V4L2_CID_VFLIP,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(hdlr, &ipu_csc_scaler_ctrl_ops, V4L2_CID_ROTATE,
			  0, 270, 90, 0);

	if (hdlr->error) {
		v4l2_ctrl_handler_free(hdlr);
		return hdlr->error;
	}

	v4l2_ctrl_handler_setup(hdlr);
	return 0;
}

#define DEFAULT_WIDTH	720
#define DEFAULT_HEIGHT	576
static const struct ipu_csc_scaler_q_data ipu_csc_scaler_q_data_default = {
	.cur_fmt = {
		.width = DEFAULT_WIDTH,
		.height = DEFAULT_HEIGHT,
		.pixelformat = V4L2_PIX_FMT_YUV420,
		.field = V4L2_FIELD_NONE,
		.bytesperline = DEFAULT_WIDTH,
		.sizeimage = DEFAULT_WIDTH * DEFAULT_HEIGHT * 3 / 2,
		.colorspace = V4L2_COLORSPACE_SRGB,
	},
	.rect = {
		.width = DEFAULT_WIDTH,
		.height = DEFAULT_HEIGHT,
	},
};

/*
 * File operations
 */
static int ipu_csc_scaler_open(struct file *file)
{
	struct ipu_csc_scaler_priv *priv = video_drvdata(file);
	struct ipu_csc_scaler_ctx *ctx = NULL;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->rot_mode = IPU_ROTATE_NONE;

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);
	ctx->priv = priv;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(priv->m2m_dev, ctx,
					    &ipu_csc_scaler_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto err_ctx;
	}

	ret = ipu_csc_scaler_init_controls(ctx);
	if (ret)
		goto err_ctrls;

	ctx->fh.ctrl_handler = &ctx->ctrl_hdlr;

	ctx->q_data[V4L2_M2M_SRC] = ipu_csc_scaler_q_data_default;
	ctx->q_data[V4L2_M2M_DST] = ipu_csc_scaler_q_data_default;

	dev_dbg(priv->dev, "Created instance %p, m2m_ctx: %p\n", ctx,
		ctx->fh.m2m_ctx);

	return 0;

err_ctrls:
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
err_ctx:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	return ret;
}

static int ipu_csc_scaler_release(struct file *file)
{
	struct ipu_csc_scaler_priv *priv = video_drvdata(file);
	struct ipu_csc_scaler_ctx *ctx = fh_to_ctx(file->private_data);

	dev_dbg(priv->dev, "Releasing instance %p\n", ctx);

	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);

	return 0;
}

static const struct v4l2_file_operations ipu_csc_scaler_fops = {
	.owner		= THIS_MODULE,
	.open		= ipu_csc_scaler_open,
	.release	= ipu_csc_scaler_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static struct v4l2_m2m_ops m2m_ops = {
	.device_run	= device_run,
	.job_abort	= job_abort,
};

static void ipu_csc_scaler_video_device_release(struct video_device *vdev)
{
	struct ipu_csc_scaler_priv *priv = video_get_drvdata(vdev);

	v4l2_m2m_release(priv->m2m_dev);
	video_device_release(vdev);
	kfree(priv);
}

static const struct video_device ipu_csc_scaler_videodev_template = {
	.name		= "ipu_ic_pp csc/scaler",
	.fops		= &ipu_csc_scaler_fops,
	.ioctl_ops	= &ipu_csc_scaler_ioctl_ops,
	.minor		= -1,
	.release	= ipu_csc_scaler_video_device_release,
	.vfl_dir	= VFL_DIR_M2M,
	.device_caps	= V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING,
};

int imx_media_csc_scaler_device_register(struct imx_media_video_dev *vdev)
{
	struct ipu_csc_scaler_priv *priv = vdev_to_priv(vdev);
	struct video_device *vfd = vdev->vfd;
	int ret;

	vfd->v4l2_dev = &priv->md->v4l2_dev;

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(vfd->v4l2_dev, "Failed to register video device\n");
		return ret;
	}

	v4l2_info(vfd->v4l2_dev, "Registered %s as /dev/%s\n", vfd->name,
		  video_device_node_name(vfd));

	return 0;
}

void imx_media_csc_scaler_device_unregister(struct imx_media_video_dev *vdev)
{
	struct ipu_csc_scaler_priv *priv = vdev_to_priv(vdev);
	struct video_device *vfd = priv->vdev.vfd;

	mutex_lock(&priv->mutex);

	video_unregister_device(vfd);

	mutex_unlock(&priv->mutex);
}

struct imx_media_video_dev *
imx_media_csc_scaler_device_init(struct imx_media_dev *md)
{
	struct ipu_csc_scaler_priv *priv;
	struct video_device *vfd;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->md = md;
	priv->dev = md->md.dev;

	mutex_init(&priv->mutex);

	vfd = video_device_alloc();
	if (!vfd) {
		ret = -ENOMEM;
		goto err_vfd;
	}

	*vfd = ipu_csc_scaler_videodev_template;
	vfd->lock = &priv->mutex;
	priv->vdev.vfd = vfd;

	INIT_LIST_HEAD(&priv->vdev.list);

	video_set_drvdata(vfd, priv);

	priv->m2m_dev = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(priv->m2m_dev)) {
		ret = PTR_ERR(priv->m2m_dev);
		v4l2_err(&md->v4l2_dev, "Failed to init mem2mem device: %d\n",
			 ret);
		goto err_m2m;
	}

	return &priv->vdev;

err_m2m:
	video_set_drvdata(vfd, NULL);
err_vfd:
	kfree(priv);
	return ERR_PTR(ret);
}

MODULE_DESCRIPTION("i.MX IPUv3 mem2mem scaler/CSC driver");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_LICENSE("GPL");
