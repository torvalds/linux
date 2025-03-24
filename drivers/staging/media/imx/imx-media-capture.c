// SPDX-License-Identifier: GPL-2.0+
/*
 * Video Capture Subdev for Freescale i.MX5/6 SOC
 *
 * Copyright (c) 2012-2016 Mentor Graphics Inc.
 */
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-dma-contig.h>
#include <video/imx-ipu-v3.h>
#include <media/imx.h>
#include "imx-media.h"

#define IMX_CAPTURE_NAME "imx-capture"

struct capture_priv {
	struct imx_media_dev *md;		/* Media device */
	struct device *dev;			/* Physical device */

	struct imx_media_video_dev vdev;	/* Video device */
	struct media_pad vdev_pad;		/* Video device pad */

	struct v4l2_subdev *src_sd;		/* Source subdev */
	int src_sd_pad;				/* Source subdev pad */

	struct mutex mutex;			/* Protect vdev operations */

	struct vb2_queue q;			/* The videobuf2 queue */
	struct list_head ready_q;		/* List of queued buffers */
	spinlock_t q_lock;			/* Protect ready_q */

	struct v4l2_ctrl_handler ctrl_hdlr;	/* Controls inherited from subdevs */

	bool legacy_api;			/* Use the legacy (pre-MC) API */
};

#define to_capture_priv(v) container_of(v, struct capture_priv, vdev)

/* In bytes, per queue */
#define VID_MEM_LIMIT	SZ_64M

/* -----------------------------------------------------------------------------
 * MC-Centric Video IOCTLs
 */

static const struct imx_media_pixfmt *capture_find_format(u32 code, u32 fourcc)
{
	const struct imx_media_pixfmt *cc;

	cc = imx_media_find_ipu_format(code, PIXFMT_SEL_YUV_RGB);
	if (cc) {
		enum imx_pixfmt_sel fmt_sel = cc->cs == IPUV3_COLORSPACE_YUV
					    ? PIXFMT_SEL_YUV : PIXFMT_SEL_RGB;

		cc = imx_media_find_pixel_format(fourcc, fmt_sel);
		if (!cc) {
			imx_media_enum_pixel_formats(&fourcc, 0, fmt_sel, 0);
			cc = imx_media_find_pixel_format(fourcc, fmt_sel);
		}

		return cc;
	}

	return imx_media_find_mbus_format(code, PIXFMT_SEL_ANY);
}

static int capture_querycap(struct file *file, void *fh,
			    struct v4l2_capability *cap)
{
	struct capture_priv *priv = video_drvdata(file);

	strscpy(cap->driver, IMX_CAPTURE_NAME, sizeof(cap->driver));
	strscpy(cap->card, IMX_CAPTURE_NAME, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform:%s", dev_name(priv->dev));

	return 0;
}

static int capture_enum_fmt_vid_cap(struct file *file, void *fh,
				    struct v4l2_fmtdesc *f)
{
	return imx_media_enum_pixel_formats(&f->pixelformat, f->index,
					    PIXFMT_SEL_ANY, f->mbus_code);
}

static int capture_enum_framesizes(struct file *file, void *fh,
				   struct v4l2_frmsizeenum *fsize)
{
	const struct imx_media_pixfmt *cc;

	if (fsize->index > 0)
		return -EINVAL;

	cc = imx_media_find_pixel_format(fsize->pixel_format, PIXFMT_SEL_ANY);
	if (!cc)
		return -EINVAL;

	/*
	 * TODO: The constraints are hardware-specific and may depend on the
	 * pixel format. This should come from the driver using
	 * imx_media_capture.
	 */
	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width = 1;
	fsize->stepwise.max_width = 65535;
	fsize->stepwise.min_height = 1;
	fsize->stepwise.max_height = 65535;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.step_height = 1;

	return 0;
}

static int capture_g_fmt_vid_cap(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct capture_priv *priv = video_drvdata(file);

	f->fmt.pix = priv->vdev.fmt;

	return 0;
}

static const struct imx_media_pixfmt *
__capture_try_fmt(struct v4l2_pix_format *pixfmt, struct v4l2_rect *compose)
{
	struct v4l2_mbus_framefmt fmt_src;
	const struct imx_media_pixfmt *cc;

	/*
	 * Find the pixel format, default to the first supported format if not
	 * found.
	 */
	cc = imx_media_find_pixel_format(pixfmt->pixelformat, PIXFMT_SEL_ANY);
	if (!cc) {
		imx_media_enum_pixel_formats(&pixfmt->pixelformat, 0,
					     PIXFMT_SEL_ANY, 0);
		cc = imx_media_find_pixel_format(pixfmt->pixelformat,
						 PIXFMT_SEL_ANY);
	}

	/* Allow IDMAC interweave but enforce field order from source. */
	if (V4L2_FIELD_IS_INTERLACED(pixfmt->field)) {
		switch (pixfmt->field) {
		case V4L2_FIELD_SEQ_TB:
			pixfmt->field = V4L2_FIELD_INTERLACED_TB;
			break;
		case V4L2_FIELD_SEQ_BT:
			pixfmt->field = V4L2_FIELD_INTERLACED_BT;
			break;
		default:
			break;
		}
	}

	v4l2_fill_mbus_format(&fmt_src, pixfmt, 0);
	imx_media_mbus_fmt_to_pix_fmt(pixfmt, &fmt_src, cc);

	if (compose) {
		compose->width = fmt_src.width;
		compose->height = fmt_src.height;
	}

	return cc;
}

static int capture_try_fmt_vid_cap(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	__capture_try_fmt(&f->fmt.pix, NULL);
	return 0;
}

static int capture_s_fmt_vid_cap(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct capture_priv *priv = video_drvdata(file);
	const struct imx_media_pixfmt *cc;

	if (vb2_is_busy(&priv->q)) {
		dev_err(priv->dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	cc = __capture_try_fmt(&f->fmt.pix, &priv->vdev.compose);

	priv->vdev.cc = cc;
	priv->vdev.fmt = f->fmt.pix;

	return 0;
}

static int capture_g_selection(struct file *file, void *fh,
			       struct v4l2_selection *s)
{
	struct capture_priv *priv = video_drvdata(file);

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		/* The compose rectangle is fixed to the source format. */
		s->r = priv->vdev.compose;
		break;
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		/*
		 * The hardware writes with a configurable but fixed DMA burst
		 * size. If the source format width is not burst size aligned,
		 * the written frame contains padding to the right.
		 */
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = priv->vdev.fmt.width;
		s->r.height = priv->vdev.fmt.height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int capture_subscribe_event(struct v4l2_fh *fh,
				   const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_IMX_FRAME_INTERVAL_ERROR:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ioctl_ops capture_ioctl_ops = {
	.vidioc_querycap		= capture_querycap,

	.vidioc_enum_fmt_vid_cap	= capture_enum_fmt_vid_cap,
	.vidioc_enum_framesizes		= capture_enum_framesizes,

	.vidioc_g_fmt_vid_cap		= capture_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= capture_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= capture_s_fmt_vid_cap,

	.vidioc_g_selection		= capture_g_selection,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	.vidioc_subscribe_event		= capture_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/* -----------------------------------------------------------------------------
 * Legacy Video IOCTLs
 */

static int capture_legacy_enum_framesizes(struct file *file, void *fh,
					  struct v4l2_frmsizeenum *fsize)
{
	struct capture_priv *priv = video_drvdata(file);
	const struct imx_media_pixfmt *cc;
	struct v4l2_subdev_frame_size_enum fse = {
		.index = fsize->index,
		.pad = priv->src_sd_pad,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	cc = imx_media_find_pixel_format(fsize->pixel_format, PIXFMT_SEL_ANY);
	if (!cc)
		return -EINVAL;

	fse.code = cc->codes ? cc->codes[0] : 0;

	ret = v4l2_subdev_call(priv->src_sd, pad, enum_frame_size, NULL, &fse);
	if (ret)
		return ret;

	if (fse.min_width == fse.max_width &&
	    fse.min_height == fse.max_height) {
		fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		fsize->discrete.width = fse.min_width;
		fsize->discrete.height = fse.min_height;
	} else {
		fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
		fsize->stepwise.min_width = fse.min_width;
		fsize->stepwise.max_width = fse.max_width;
		fsize->stepwise.min_height = fse.min_height;
		fsize->stepwise.max_height = fse.max_height;
		fsize->stepwise.step_width = 1;
		fsize->stepwise.step_height = 1;
	}

	return 0;
}

static int capture_legacy_enum_frameintervals(struct file *file, void *fh,
					      struct v4l2_frmivalenum *fival)
{
	struct capture_priv *priv = video_drvdata(file);
	const struct imx_media_pixfmt *cc;
	struct v4l2_subdev_frame_interval_enum fie = {
		.index = fival->index,
		.pad = priv->src_sd_pad,
		.width = fival->width,
		.height = fival->height,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	cc = imx_media_find_pixel_format(fival->pixel_format, PIXFMT_SEL_ANY);
	if (!cc)
		return -EINVAL;

	fie.code = cc->codes ? cc->codes[0] : 0;

	ret = v4l2_subdev_call(priv->src_sd, pad, enum_frame_interval,
			       NULL, &fie);
	if (ret)
		return ret;

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete = fie.interval;

	return 0;
}

static int capture_legacy_enum_fmt_vid_cap(struct file *file, void *fh,
					   struct v4l2_fmtdesc *f)
{
	struct capture_priv *priv = video_drvdata(file);
	const struct imx_media_pixfmt *cc_src;
	struct v4l2_subdev_format fmt_src = {
		.pad = priv->src_sd_pad,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	u32 fourcc;
	int ret;

	ret = v4l2_subdev_call(priv->src_sd, pad, get_fmt, NULL, &fmt_src);
	if (ret) {
		dev_err(priv->dev, "failed to get src_sd format\n");
		return ret;
	}

	cc_src = imx_media_find_ipu_format(fmt_src.format.code,
					   PIXFMT_SEL_YUV_RGB);
	if (cc_src) {
		enum imx_pixfmt_sel fmt_sel =
			(cc_src->cs == IPUV3_COLORSPACE_YUV) ?
			PIXFMT_SEL_YUV : PIXFMT_SEL_RGB;

		ret = imx_media_enum_pixel_formats(&fourcc, f->index, fmt_sel,
						   0);
		if (ret)
			return ret;
	} else {
		cc_src = imx_media_find_mbus_format(fmt_src.format.code,
						    PIXFMT_SEL_ANY);
		if (WARN_ON(!cc_src))
			return -EINVAL;

		if (f->index != 0)
			return -EINVAL;
		fourcc = cc_src->fourcc;
	}

	f->pixelformat = fourcc;

	return 0;
}

static const struct imx_media_pixfmt *
__capture_legacy_try_fmt(struct capture_priv *priv,
			 struct v4l2_subdev_format *fmt_src,
			 struct v4l2_pix_format *pixfmt)
{
	const struct imx_media_pixfmt *cc;

	cc = capture_find_format(fmt_src->format.code, pixfmt->pixelformat);
	if (WARN_ON(!cc))
		return NULL;

	/* allow IDMAC interweave but enforce field order from source */
	if (V4L2_FIELD_IS_INTERLACED(pixfmt->field)) {
		switch (fmt_src->format.field) {
		case V4L2_FIELD_SEQ_TB:
			fmt_src->format.field = V4L2_FIELD_INTERLACED_TB;
			break;
		case V4L2_FIELD_SEQ_BT:
			fmt_src->format.field = V4L2_FIELD_INTERLACED_BT;
			break;
		default:
			break;
		}
	}

	imx_media_mbus_fmt_to_pix_fmt(pixfmt, &fmt_src->format, cc);

	return cc;
}

static int capture_legacy_try_fmt_vid_cap(struct file *file, void *fh,
					  struct v4l2_format *f)
{
	struct capture_priv *priv = video_drvdata(file);
	struct v4l2_subdev_format fmt_src = {
		.pad = priv->src_sd_pad,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	int ret;

	ret = v4l2_subdev_call(priv->src_sd, pad, get_fmt, NULL, &fmt_src);
	if (ret)
		return ret;

	if (!__capture_legacy_try_fmt(priv, &fmt_src, &f->fmt.pix))
		return -EINVAL;

	return 0;
}

static int capture_legacy_s_fmt_vid_cap(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct capture_priv *priv = video_drvdata(file);
	struct v4l2_subdev_format fmt_src = {
		.pad = priv->src_sd_pad,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	const struct imx_media_pixfmt *cc;
	int ret;

	if (vb2_is_busy(&priv->q)) {
		dev_err(priv->dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	ret = v4l2_subdev_call(priv->src_sd, pad, get_fmt, NULL, &fmt_src);
	if (ret)
		return ret;

	cc = __capture_legacy_try_fmt(priv, &fmt_src, &f->fmt.pix);
	if (!cc)
		return -EINVAL;

	priv->vdev.cc = cc;
	priv->vdev.fmt = f->fmt.pix;
	priv->vdev.compose.width = fmt_src.format.width;
	priv->vdev.compose.height = fmt_src.format.height;

	return 0;
}

static int capture_legacy_querystd(struct file *file, void *fh,
				   v4l2_std_id *std)
{
	struct capture_priv *priv = video_drvdata(file);

	return v4l2_subdev_call(priv->src_sd, video, querystd, std);
}

static int capture_legacy_g_std(struct file *file, void *fh, v4l2_std_id *std)
{
	struct capture_priv *priv = video_drvdata(file);

	return v4l2_subdev_call(priv->src_sd, video, g_std, std);
}

static int capture_legacy_s_std(struct file *file, void *fh, v4l2_std_id std)
{
	struct capture_priv *priv = video_drvdata(file);

	if (vb2_is_busy(&priv->q))
		return -EBUSY;

	return v4l2_subdev_call(priv->src_sd, video, s_std, std);
}

static int capture_legacy_g_parm(struct file *file, void *fh,
				 struct v4l2_streamparm *a)
{
	struct capture_priv *priv = video_drvdata(file);
	struct v4l2_subdev_frame_interval fi = {
		.pad = priv->src_sd_pad,
	};
	int ret;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	ret = v4l2_subdev_call_state_active(priv->src_sd, pad,
					    get_frame_interval, &fi);
	if (ret < 0)
		return ret;

	a->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	a->parm.capture.timeperframe = fi.interval;

	return 0;
}

static int capture_legacy_s_parm(struct file *file, void *fh,
				 struct v4l2_streamparm *a)
{
	struct capture_priv *priv = video_drvdata(file);
	struct v4l2_subdev_frame_interval fi = {
		.pad = priv->src_sd_pad,
	};
	int ret;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	fi.interval = a->parm.capture.timeperframe;
	ret = v4l2_subdev_call_state_active(priv->src_sd, pad,
					    set_frame_interval, &fi);
	if (ret < 0)
		return ret;

	a->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	a->parm.capture.timeperframe = fi.interval;

	return 0;
}

static int capture_legacy_subscribe_event(struct v4l2_fh *fh,
					  const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_IMX_FRAME_INTERVAL_ERROR:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ioctl_ops capture_legacy_ioctl_ops = {
	.vidioc_querycap		= capture_querycap,

	.vidioc_enum_framesizes		= capture_legacy_enum_framesizes,
	.vidioc_enum_frameintervals	= capture_legacy_enum_frameintervals,

	.vidioc_enum_fmt_vid_cap	= capture_legacy_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		= capture_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= capture_legacy_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		= capture_legacy_s_fmt_vid_cap,

	.vidioc_querystd		= capture_legacy_querystd,
	.vidioc_g_std			= capture_legacy_g_std,
	.vidioc_s_std			= capture_legacy_s_std,

	.vidioc_g_selection		= capture_g_selection,

	.vidioc_g_parm			= capture_legacy_g_parm,
	.vidioc_s_parm			= capture_legacy_s_parm,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,

	.vidioc_subscribe_event		= capture_legacy_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/* -----------------------------------------------------------------------------
 * Queue Operations
 */

static int capture_queue_setup(struct vb2_queue *vq,
			       unsigned int *nbuffers,
			       unsigned int *nplanes,
			       unsigned int sizes[],
			       struct device *alloc_devs[])
{
	struct capture_priv *priv = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix = &priv->vdev.fmt;
	unsigned int q_num_bufs = vb2_get_num_buffers(vq);
	unsigned int count = *nbuffers;

	if (vq->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (*nplanes) {
		if (*nplanes != 1 || sizes[0] < pix->sizeimage)
			return -EINVAL;
		count += q_num_bufs;
	}

	count = min_t(__u32, VID_MEM_LIMIT / pix->sizeimage, count);

	if (*nplanes)
		*nbuffers = (count < q_num_bufs) ? 0 :
			count - q_num_bufs;
	else
		*nbuffers = count;

	*nplanes = 1;
	sizes[0] = pix->sizeimage;

	return 0;
}

static int capture_buf_init(struct vb2_buffer *vb)
{
	struct imx_media_buffer *buf = to_imx_media_vb(vb);

	INIT_LIST_HEAD(&buf->list);

	return 0;
}

static int capture_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct capture_priv *priv = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix = &priv->vdev.fmt;

	if (vb2_plane_size(vb, 0) < pix->sizeimage) {
		dev_err(priv->dev,
			"data will not fit into plane (%lu < %lu)\n",
			vb2_plane_size(vb, 0), (long)pix->sizeimage);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, pix->sizeimage);

	return 0;
}

static void capture_buf_queue(struct vb2_buffer *vb)
{
	struct capture_priv *priv = vb2_get_drv_priv(vb->vb2_queue);
	struct imx_media_buffer *buf = to_imx_media_vb(vb);
	unsigned long flags;

	spin_lock_irqsave(&priv->q_lock, flags);

	list_add_tail(&buf->list, &priv->ready_q);

	spin_unlock_irqrestore(&priv->q_lock, flags);
}

static int capture_validate_fmt(struct capture_priv *priv)
{
	struct v4l2_subdev_format fmt_src = {
		.pad = priv->src_sd_pad,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	const struct imx_media_pixfmt *cc;
	int ret;

	/* Retrieve the media bus format on the source subdev. */
	ret = v4l2_subdev_call(priv->src_sd, pad, get_fmt, NULL, &fmt_src);
	if (ret)
		return ret;

	/*
	 * Verify that the media bus size matches the size set on the video
	 * node. It is sufficient to check the compose rectangle size without
	 * checking the rounded size from vdev.fmt, as the rounded size is
	 * derived directly from the compose rectangle size, and will thus
	 * always match if the compose rectangle matches.
	 */
	if (priv->vdev.compose.width != fmt_src.format.width ||
	    priv->vdev.compose.height != fmt_src.format.height)
		return -EPIPE;

	/*
	 * Verify that the media bus code is compatible with the pixel format
	 * set on the video node.
	 */
	cc = capture_find_format(fmt_src.format.code, 0);
	if (!cc || priv->vdev.cc->cs != cc->cs)
		return -EPIPE;

	return 0;
}

static int capture_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct capture_priv *priv = vb2_get_drv_priv(vq);
	struct imx_media_buffer *buf, *tmp;
	unsigned long flags;
	int ret;

	ret = capture_validate_fmt(priv);
	if (ret) {
		dev_err(priv->dev, "capture format not valid\n");
		goto return_bufs;
	}

	ret = imx_media_pipeline_set_stream(priv->md, &priv->src_sd->entity,
					    true);
	if (ret) {
		dev_err(priv->dev, "pipeline start failed with %d\n", ret);
		goto return_bufs;
	}

	return 0;

return_bufs:
	spin_lock_irqsave(&priv->q_lock, flags);
	list_for_each_entry_safe(buf, tmp, &priv->ready_q, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vbuf.vb2_buf, VB2_BUF_STATE_QUEUED);
	}
	spin_unlock_irqrestore(&priv->q_lock, flags);
	return ret;
}

static void capture_stop_streaming(struct vb2_queue *vq)
{
	struct capture_priv *priv = vb2_get_drv_priv(vq);
	struct imx_media_buffer *frame;
	struct imx_media_buffer *tmp;
	unsigned long flags;
	int ret;

	ret = imx_media_pipeline_set_stream(priv->md, &priv->src_sd->entity,
					    false);
	if (ret)
		dev_warn(priv->dev, "pipeline stop failed with %d\n", ret);

	/* release all active buffers */
	spin_lock_irqsave(&priv->q_lock, flags);
	list_for_each_entry_safe(frame, tmp, &priv->ready_q, list) {
		list_del(&frame->list);
		vb2_buffer_done(&frame->vbuf.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&priv->q_lock, flags);
}

static const struct vb2_ops capture_qops = {
	.queue_setup	 = capture_queue_setup,
	.buf_init        = capture_buf_init,
	.buf_prepare	 = capture_buf_prepare,
	.buf_queue	 = capture_buf_queue,
	.start_streaming = capture_start_streaming,
	.stop_streaming  = capture_stop_streaming,
};

/* -----------------------------------------------------------------------------
 * File Operations
 */

static int capture_open(struct file *file)
{
	struct capture_priv *priv = video_drvdata(file);
	struct video_device *vfd = priv->vdev.vfd;
	int ret;

	if (mutex_lock_interruptible(&priv->mutex))
		return -ERESTARTSYS;

	ret = v4l2_fh_open(file);
	if (ret) {
		dev_err(priv->dev, "v4l2_fh_open failed\n");
		goto out;
	}

	ret = v4l2_pipeline_pm_get(&vfd->entity);
	if (ret)
		v4l2_fh_release(file);

out:
	mutex_unlock(&priv->mutex);
	return ret;
}

static int capture_release(struct file *file)
{
	struct capture_priv *priv = video_drvdata(file);
	struct video_device *vfd = priv->vdev.vfd;
	struct vb2_queue *vq = &priv->q;

	mutex_lock(&priv->mutex);

	if (file->private_data == vq->owner) {
		vb2_queue_release(vq);
		vq->owner = NULL;
	}

	v4l2_pipeline_pm_put(&vfd->entity);

	v4l2_fh_release(file);
	mutex_unlock(&priv->mutex);
	return 0;
}

static const struct v4l2_file_operations capture_fops = {
	.owner		= THIS_MODULE,
	.open		= capture_open,
	.release	= capture_release,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
};

/* -----------------------------------------------------------------------------
 * Public API
 */

struct imx_media_buffer *
imx_media_capture_device_next_buf(struct imx_media_video_dev *vdev)
{
	struct capture_priv *priv = to_capture_priv(vdev);
	struct imx_media_buffer *buf = NULL;
	unsigned long flags;

	spin_lock_irqsave(&priv->q_lock, flags);

	/* get next queued buffer */
	if (!list_empty(&priv->ready_q)) {
		buf = list_entry(priv->ready_q.next, struct imx_media_buffer,
				 list);
		list_del(&buf->list);
	}

	spin_unlock_irqrestore(&priv->q_lock, flags);

	return buf;
}
EXPORT_SYMBOL_GPL(imx_media_capture_device_next_buf);

void imx_media_capture_device_error(struct imx_media_video_dev *vdev)
{
	struct capture_priv *priv = to_capture_priv(vdev);
	struct vb2_queue *vq = &priv->q;
	unsigned long flags;

	if (!vb2_is_streaming(vq))
		return;

	spin_lock_irqsave(&priv->q_lock, flags);
	vb2_queue_error(vq);
	spin_unlock_irqrestore(&priv->q_lock, flags);
}
EXPORT_SYMBOL_GPL(imx_media_capture_device_error);

static int capture_init_format(struct capture_priv *priv)
{
	struct v4l2_subdev_format fmt_src = {
		.pad = priv->src_sd_pad,
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};
	struct imx_media_video_dev *vdev = &priv->vdev;
	int ret;

	if (priv->legacy_api) {
		ret = v4l2_subdev_call(priv->src_sd, pad, get_fmt, NULL,
				       &fmt_src);
		if (ret) {
			dev_err(priv->dev, "failed to get source format\n");
			return ret;
		}
	} else {
		fmt_src.format.code = MEDIA_BUS_FMT_UYVY8_2X8;
		fmt_src.format.width = IMX_MEDIA_DEF_PIX_WIDTH;
		fmt_src.format.height = IMX_MEDIA_DEF_PIX_HEIGHT;
	}

	imx_media_mbus_fmt_to_pix_fmt(&vdev->fmt, &fmt_src.format, NULL);
	vdev->compose.width = fmt_src.format.width;
	vdev->compose.height = fmt_src.format.height;

	vdev->cc = imx_media_find_pixel_format(vdev->fmt.pixelformat,
					       PIXFMT_SEL_ANY);

	return 0;
}

int imx_media_capture_device_register(struct imx_media_video_dev *vdev,
				      u32 link_flags)
{
	struct capture_priv *priv = to_capture_priv(vdev);
	struct v4l2_subdev *sd = priv->src_sd;
	struct v4l2_device *v4l2_dev = sd->v4l2_dev;
	struct video_device *vfd = vdev->vfd;
	int ret;

	/* get media device */
	priv->md = container_of(v4l2_dev->mdev, struct imx_media_dev, md);

	vfd->v4l2_dev = v4l2_dev;

	/* Initialize the default format and compose rectangle. */
	ret = capture_init_format(priv);
	if (ret < 0)
		return ret;

	/* Register the video device. */
	ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(priv->dev, "Failed to register video device\n");
		return ret;
	}

	dev_info(priv->dev, "Registered %s as /dev/%s\n", vfd->name,
		 video_device_node_name(vfd));

	/* Create the link from the src_sd devnode pad to device node. */
	if (link_flags & MEDIA_LNK_FL_IMMUTABLE)
		link_flags |= MEDIA_LNK_FL_ENABLED;
	ret = media_create_pad_link(&sd->entity, priv->src_sd_pad,
				    &vfd->entity, 0, link_flags);
	if (ret) {
		dev_err(priv->dev, "failed to create link to device node\n");
		video_unregister_device(vfd);
		return ret;
	}

	/* Add vdev to the video devices list. */
	imx_media_add_video_device(priv->md, vdev);

	return 0;
}
EXPORT_SYMBOL_GPL(imx_media_capture_device_register);

void imx_media_capture_device_unregister(struct imx_media_video_dev *vdev)
{
	struct capture_priv *priv = to_capture_priv(vdev);
	struct video_device *vfd = priv->vdev.vfd;

	media_entity_cleanup(&vfd->entity);
	video_unregister_device(vfd);
}
EXPORT_SYMBOL_GPL(imx_media_capture_device_unregister);

struct imx_media_video_dev *
imx_media_capture_device_init(struct device *dev, struct v4l2_subdev *src_sd,
			      int pad, bool legacy_api)
{
	struct capture_priv *priv;
	struct video_device *vfd;
	struct vb2_queue *vq;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->src_sd = src_sd;
	priv->src_sd_pad = pad;
	priv->dev = dev;
	priv->legacy_api = legacy_api;

	mutex_init(&priv->mutex);
	INIT_LIST_HEAD(&priv->ready_q);
	spin_lock_init(&priv->q_lock);

	/* Allocate and initialize the video device. */
	vfd = video_device_alloc();
	if (!vfd)
		return ERR_PTR(-ENOMEM);

	vfd->fops = &capture_fops;
	vfd->ioctl_ops = legacy_api ? &capture_legacy_ioctl_ops
		       : &capture_ioctl_ops;
	vfd->minor = -1;
	vfd->release = video_device_release;
	vfd->vfl_dir = VFL_DIR_RX;
	vfd->tvnorms = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM;
	vfd->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING
			 | (!legacy_api ? V4L2_CAP_IO_MC : 0);
	vfd->lock = &priv->mutex;
	vfd->queue = &priv->q;

	snprintf(vfd->name, sizeof(vfd->name), "%s capture", src_sd->name);

	video_set_drvdata(vfd, priv);
	priv->vdev.vfd = vfd;
	INIT_LIST_HEAD(&priv->vdev.list);

	/* Initialize the video device pad. */
	priv->vdev_pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vfd->entity, 1, &priv->vdev_pad);
	if (ret) {
		video_device_release(vfd);
		return ERR_PTR(ret);
	}

	/* Initialize the vb2 queue. */
	vq = &priv->q;
	vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vq->io_modes = VB2_MMAP | VB2_DMABUF;
	vq->drv_priv = priv;
	vq->buf_struct_size = sizeof(struct imx_media_buffer);
	vq->ops = &capture_qops;
	vq->mem_ops = &vb2_dma_contig_memops;
	vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vq->lock = &priv->mutex;
	vq->min_queued_buffers = 2;
	vq->dev = priv->dev;

	ret = vb2_queue_init(vq);
	if (ret) {
		dev_err(priv->dev, "vb2_queue_init failed\n");
		video_device_release(vfd);
		return ERR_PTR(ret);
	}

	if (legacy_api) {
		/* Initialize the control handler. */
		v4l2_ctrl_handler_init(&priv->ctrl_hdlr, 0);
		vfd->ctrl_handler = &priv->ctrl_hdlr;
	}

	return &priv->vdev;
}
EXPORT_SYMBOL_GPL(imx_media_capture_device_init);

void imx_media_capture_device_remove(struct imx_media_video_dev *vdev)
{
	struct capture_priv *priv = to_capture_priv(vdev);

	v4l2_ctrl_handler_free(&priv->ctrl_hdlr);
}
EXPORT_SYMBOL_GPL(imx_media_capture_device_remove);

MODULE_DESCRIPTION("i.MX5/6 v4l2 video capture interface driver");
MODULE_AUTHOR("Steve Longerbeam <steve_longerbeam@mentor.com>");
MODULE_LICENSE("GPL");
