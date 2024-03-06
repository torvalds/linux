// SPDX-License-Identifier: GPL-2.0
/*
 * stf_video.c
 *
 * StarFive Camera Subsystem - V4L2 device node
 *
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 */

#include <linux/pm_runtime.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-contig.h>

#include "stf-camss.h"
#include "stf-video.h"

/* -----------------------------------------------------------------------------
 * Helper functions
 */

static inline struct stfcamss_buffer *
to_stfcamss_buffer(struct vb2_v4l2_buffer *vbuf)
{
	return container_of(vbuf, struct stfcamss_buffer, vb);
}

static const struct stfcamss_format_info *
video_g_fi_by_index(struct stfcamss_video *video, int index)
{
	if (index >= video->nformats)
		return NULL;

	return &video->formats[index];
}

static const struct stfcamss_format_info *
video_g_fi_by_mcode(struct stfcamss_video *video, u32 mcode)
{
	unsigned int i;

	for (i = 0; i < video->nformats; i++) {
		if (video->formats[i].code == mcode)
			return &video->formats[i];
	}

	return NULL;
}

static const struct stfcamss_format_info *
video_g_fi_by_pfmt(struct stfcamss_video *video, u32 pixelformat)
{
	unsigned int i;

	for (i = 0; i < video->nformats; i++) {
		if (video->formats[i].pixelformat == pixelformat)
			return &video->formats[i];
	}

	return NULL;
}

static int __video_try_fmt(struct stfcamss_video *video, struct v4l2_format *f)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;
	const struct stfcamss_format_info *fi;
	u32 width, height;
	u32 bpl;
	unsigned int i;

	fi = video_g_fi_by_pfmt(video, pix->pixelformat);
	if (!fi)
		fi = &video->formats[0]; /* default format */

	width = pix->width;
	height = pix->height;

	memset(pix, 0, sizeof(*pix));

	pix->pixelformat = fi->pixelformat;
	pix->width = clamp_t(u32, width, STFCAMSS_FRAME_MIN_WIDTH,
			     STFCAMSS_FRAME_MAX_WIDTH);
	pix->height = clamp_t(u32, height, STFCAMSS_FRAME_MIN_HEIGHT,
			      STFCAMSS_FRAME_MAX_HEIGHT);
	bpl = pix->width * fi->bpp / 8;
	bpl = ALIGN(bpl, video->bpl_alignment);
	pix->bytesperline = bpl;

	for (i = 0; i < fi->planes; ++i)
		pix->sizeimage += bpl * pix->height / fi->vsub[i];

	pix->field = V4L2_FIELD_NONE;
	pix->colorspace = V4L2_COLORSPACE_SRGB;
	pix->flags = 0;
	pix->ycbcr_enc =
		V4L2_MAP_YCBCR_ENC_DEFAULT(pix->colorspace);
	pix->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
							  pix->colorspace,
							  pix->ycbcr_enc);
	pix->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(pix->colorspace);

	return 0;
}

static int stf_video_init_format(struct stfcamss_video *video)
{
	int ret;
	struct v4l2_format format = {
		.type = video->type,
		.fmt.pix = {
			.width = 1920,
			.height = 1080,
			.pixelformat = V4L2_PIX_FMT_NV12,
		},
	};

	ret = __video_try_fmt(video, &format);

	if (ret < 0)
		return ret;

	video->active_fmt = format;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Video queue operations
 */

static int video_queue_setup(struct vb2_queue *q,
			     unsigned int *num_buffers,
			     unsigned int *num_planes,
			     unsigned int sizes[],
			     struct device *alloc_devs[])
{
	struct stfcamss_video *video = vb2_get_drv_priv(q);
	const struct v4l2_pix_format *format = &video->active_fmt.fmt.pix;

	if (*num_planes) {
		if (*num_planes != 1)
			return -EINVAL;

		if (sizes[0] < format->sizeimage)
			return -EINVAL;
	} else {
		*num_planes = 1;
		sizes[0] = format->sizeimage;
	}

	if (!sizes[0]) {
		dev_dbg(video->stfcamss->dev,
			"%s: error size is zero.\n", __func__);
		return -EINVAL;
	}

	dev_dbg(video->stfcamss->dev, "planes = %d, size = %d\n",
		*num_planes, sizes[0]);

	return 0;
}

static int video_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct stfcamss_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct stfcamss_buffer *buffer = to_stfcamss_buffer(vbuf);
	const struct v4l2_pix_format *fmt = &video->active_fmt.fmt.pix;
	dma_addr_t *paddr;

	paddr = vb2_plane_cookie(vb, 0);
	buffer->addr[0] = *paddr;

	if (fmt->pixelformat == V4L2_PIX_FMT_NV12)
		buffer->addr[1] =
			buffer->addr[0] + fmt->bytesperline * fmt->height;

	return 0;
}

static int video_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct stfcamss_video *video = vb2_get_drv_priv(vb->vb2_queue);
	const struct v4l2_pix_format *fmt = &video->active_fmt.fmt.pix;

	if (fmt->sizeimage > vb2_plane_size(vb, 0)) {
		dev_dbg(video->stfcamss->dev,
			"sizeimage = %u, plane size = %u\n",
			fmt->sizeimage, (unsigned int)vb2_plane_size(vb, 0));
		return -EINVAL;
	}
	vb2_set_plane_payload(vb, 0, fmt->sizeimage);

	vbuf->field = V4L2_FIELD_NONE;

	return 0;
}

static void video_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct stfcamss_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct stfcamss_buffer *buffer = to_stfcamss_buffer(vbuf);

	video->ops->queue_buffer(video, buffer);
}

static int video_get_subdev_format(struct stfcamss_video *video,
				   struct v4l2_subdev_format *fmt)
{
	struct v4l2_subdev *subdev;
	struct media_pad *pad;
	struct media_entity *entity;
	int ret;

	entity = &video->vdev.entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_pad_remote_pad_first(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		fmt->pad = pad->index;

		ret = v4l2_subdev_call_state_active(subdev, pad, get_fmt, fmt);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;
		else if (!ret)
			break;
	}

	return 0;
}

static int stf_video_check_format(struct stfcamss_video *video)
{
	struct v4l2_pix_format *pix = &video->active_fmt.fmt.pix;
	const struct stfcamss_format_info *fi;
	int ret;
	struct v4l2_subdev_format sd_fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	ret = video_get_subdev_format(video, &sd_fmt);
	if (ret < 0)
		return ret;

	fi = video_g_fi_by_mcode(video, sd_fmt.format.code);
	if (!fi)
		return -EINVAL;

	if (pix->pixelformat != fi->pixelformat ||
	    pix->height != sd_fmt.format.height ||
	    pix->width != sd_fmt.format.width ||
	    pix->field != sd_fmt.format.field)
		return -EPIPE;

	return 0;
}

static int video_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct stfcamss_video *video = vb2_get_drv_priv(q);
	struct video_device *vdev = &video->vdev;
	int ret;

	ret = video_device_pipeline_start(vdev, &video->stfcamss->pipe);
	if (ret < 0) {
		dev_err(video->stfcamss->dev,
			"Failed to media_pipeline_start: %d\n", ret);
		goto err_ret_buffers;
	}

	ret = pm_runtime_resume_and_get(video->stfcamss->dev);
	if (ret < 0) {
		dev_err(video->stfcamss->dev, "power up failed %d\n", ret);
		goto err_pipeline_stop;
	}

	video->ops->start_streaming(video);

	ret = v4l2_subdev_call(video->source_subdev, video, s_stream, true);
	if (ret) {
		dev_err(video->stfcamss->dev, "stream on failed\n");
		goto err_pm_put;
	}

	return 0;

err_pm_put:
	pm_runtime_put(video->stfcamss->dev);
err_pipeline_stop:
	video_device_pipeline_stop(vdev);
err_ret_buffers:
	video->ops->flush_buffers(video, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void video_stop_streaming(struct vb2_queue *q)
{
	struct stfcamss_video *video = vb2_get_drv_priv(q);
	struct video_device *vdev = &video->vdev;

	video->ops->stop_streaming(video);

	v4l2_subdev_call(video->source_subdev, video, s_stream, false);

	pm_runtime_put(video->stfcamss->dev);

	video_device_pipeline_stop(vdev);
	video->ops->flush_buffers(video, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops stf_video_vb2_q_ops = {
	.queue_setup     = video_queue_setup,
	.wait_prepare    = vb2_ops_wait_prepare,
	.wait_finish     = vb2_ops_wait_finish,
	.buf_init        = video_buf_init,
	.buf_prepare     = video_buf_prepare,
	.buf_queue       = video_buf_queue,
	.start_streaming = video_start_streaming,
	.stop_streaming  = video_stop_streaming,
};

/* -----------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int video_querycap(struct file *file, void *fh,
			  struct v4l2_capability *cap)
{
	strscpy(cap->driver, "starfive-camss", sizeof(cap->driver));
	strscpy(cap->card, "Starfive Camera Subsystem", sizeof(cap->card));

	return 0;
}

static int video_enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct stfcamss_video *video = video_drvdata(file);
	const struct stfcamss_format_info *fi;

	if (f->index >= video->nformats)
		return -EINVAL;

	if (f->mbus_code) {
		/* Each entry in formats[] table has unique mbus_code */
		if (f->index > 0)
			return -EINVAL;

		fi = video_g_fi_by_mcode(video, f->mbus_code);
	} else {
		fi = video_g_fi_by_index(video, f->index);
	}

	if (!fi)
		return -EINVAL;

	f->pixelformat = fi->pixelformat;

	return 0;
}

static int video_enum_framesizes(struct file *file, void *fh,
				 struct v4l2_frmsizeenum *fsize)
{
	struct stfcamss_video *video = video_drvdata(file);
	unsigned int i;

	if (fsize->index)
		return -EINVAL;

	for (i = 0; i < video->nformats; i++) {
		if (video->formats[i].pixelformat == fsize->pixel_format)
			break;
	}

	if (i == video->nformats)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width = STFCAMSS_FRAME_MIN_WIDTH;
	fsize->stepwise.max_width = STFCAMSS_FRAME_MAX_WIDTH;
	fsize->stepwise.min_height = STFCAMSS_FRAME_MIN_HEIGHT;
	fsize->stepwise.max_height = STFCAMSS_FRAME_MAX_HEIGHT;
	fsize->stepwise.step_width = 1;
	fsize->stepwise.step_height = 1;

	return 0;
}

static int video_g_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct stfcamss_video *video = video_drvdata(file);

	*f = video->active_fmt;

	return 0;
}

static int video_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct stfcamss_video *video = video_drvdata(file);
	int ret;

	if (vb2_is_busy(&video->vb2_q))
		return -EBUSY;

	ret = __video_try_fmt(video, f);
	if (ret < 0)
		return ret;

	video->active_fmt = *f;

	return 0;
}

static int video_try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct stfcamss_video *video = video_drvdata(file);

	return __video_try_fmt(video, f);
}

static const struct v4l2_ioctl_ops stf_vid_ioctl_ops = {
	.vidioc_querycap                = video_querycap,
	.vidioc_enum_fmt_vid_cap        = video_enum_fmt,
	.vidioc_enum_framesizes         = video_enum_framesizes,
	.vidioc_g_fmt_vid_cap           = video_g_fmt,
	.vidioc_s_fmt_vid_cap           = video_s_fmt,
	.vidioc_try_fmt_vid_cap         = video_try_fmt,
	.vidioc_reqbufs                 = vb2_ioctl_reqbufs,
	.vidioc_querybuf                = vb2_ioctl_querybuf,
	.vidioc_qbuf                    = vb2_ioctl_qbuf,
	.vidioc_expbuf                  = vb2_ioctl_expbuf,
	.vidioc_dqbuf                   = vb2_ioctl_dqbuf,
	.vidioc_create_bufs             = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf             = vb2_ioctl_prepare_buf,
	.vidioc_streamon                = vb2_ioctl_streamon,
	.vidioc_streamoff               = vb2_ioctl_streamoff,
};

/* -----------------------------------------------------------------------------
 * V4L2 file operations
 */

static const struct v4l2_file_operations stf_vid_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open           = v4l2_fh_open,
	.release        = vb2_fop_release,
	.poll           = vb2_fop_poll,
	.mmap           = vb2_fop_mmap,
	.read           = vb2_fop_read,
};

/* -----------------------------------------------------------------------------
 * STFCAMSS video core
 */

static int stf_link_validate(struct media_link *link)
{
	struct video_device *vdev =
		media_entity_to_video_device(link->sink->entity);
	struct stfcamss_video *video = video_get_drvdata(vdev);
	int ret;

	ret = stf_video_check_format(video);

	return ret;
}

static const struct media_entity_operations stf_media_ops = {
	.link_validate = stf_link_validate,
};

static void stf_video_release(struct video_device *vdev)
{
	struct stfcamss_video *video = video_get_drvdata(vdev);

	media_entity_cleanup(&vdev->entity);

	mutex_destroy(&video->q_lock);
	mutex_destroy(&video->lock);
}

int stf_video_register(struct stfcamss_video *video,
		       struct v4l2_device *v4l2_dev, const char *name)
{
	struct video_device *vdev = &video->vdev;
	struct vb2_queue *q;
	struct media_pad *pad = &video->pad;
	int ret;

	mutex_init(&video->q_lock);
	mutex_init(&video->lock);

	q = &video->vb2_q;
	q->drv_priv = video;
	q->mem_ops = &vb2_dma_contig_memops;
	q->ops = &stf_video_vb2_q_ops;
	q->type = video->type;
	q->io_modes = VB2_DMABUF | VB2_MMAP;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->buf_struct_size = sizeof(struct stfcamss_buffer);
	q->dev = video->stfcamss->dev;
	q->lock = &video->q_lock;
	q->min_queued_buffers = STFCAMSS_MIN_BUFFERS;
	ret = vb2_queue_init(q);
	if (ret < 0) {
		dev_err(video->stfcamss->dev,
			"Failed to init vb2 queue: %d\n", ret);
		goto err_mutex_destroy;
	}

	pad->flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vdev->entity, 1, pad);
	if (ret < 0) {
		dev_err(video->stfcamss->dev,
			"Failed to init video entity: %d\n", ret);
		goto err_mutex_destroy;
	}

	ret = stf_video_init_format(video);
	if (ret < 0) {
		dev_err(video->stfcamss->dev,
			"Failed to init format: %d\n", ret);
		goto err_media_cleanup;
	}

	vdev->fops = &stf_vid_fops;
	vdev->ioctl_ops = &stf_vid_ioctl_ops;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
	vdev->entity.ops = &stf_media_ops;
	vdev->vfl_dir = VFL_DIR_RX;
	vdev->release = stf_video_release;
	vdev->v4l2_dev = v4l2_dev;
	vdev->queue = &video->vb2_q;
	vdev->lock = &video->lock;
	strscpy(vdev->name, name, sizeof(vdev->name));

	video_set_drvdata(vdev, video);

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		dev_err(video->stfcamss->dev,
			"Failed to register video device: %d\n", ret);
		goto err_media_cleanup;
	}

	return 0;

err_media_cleanup:
	media_entity_cleanup(&vdev->entity);
err_mutex_destroy:
	mutex_destroy(&video->lock);
	mutex_destroy(&video->q_lock);
	return ret;
}

void stf_video_unregister(struct stfcamss_video *video)
{
	vb2_video_unregister_device(&video->vdev);
}
