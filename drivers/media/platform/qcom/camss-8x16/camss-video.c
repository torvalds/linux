/*
 * camss-video.c
 *
 * Qualcomm MSM Camera Subsystem - V4L2 device node
 *
 * Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015-2017 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/slab.h>
#include <media/media-entity.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf-core.h>
#include <media/videobuf2-dma-sg.h>

#include "camss-video.h"
#include "camss.h"

/*
 * struct camss_format_info - ISP media bus format information
 * @code: V4L2 media bus format code
 * @pixelformat: V4L2 pixel format FCC identifier
 * @bpp: Bits per pixel when stored in memory
 */
static const struct camss_format_info {
	u32 code;
	u32 pixelformat;
	unsigned int bpp;
} formats[] = {
	{ MEDIA_BUS_FMT_UYVY8_2X8, V4L2_PIX_FMT_UYVY, 16 },
	{ MEDIA_BUS_FMT_VYUY8_2X8, V4L2_PIX_FMT_VYUY, 16 },
	{ MEDIA_BUS_FMT_YUYV8_2X8, V4L2_PIX_FMT_YUYV, 16 },
	{ MEDIA_BUS_FMT_YVYU8_2X8, V4L2_PIX_FMT_YVYU, 16 },
	{ MEDIA_BUS_FMT_SBGGR8_1X8, V4L2_PIX_FMT_SBGGR8, 8 },
	{ MEDIA_BUS_FMT_SGBRG8_1X8, V4L2_PIX_FMT_SGBRG8, 8 },
	{ MEDIA_BUS_FMT_SGRBG8_1X8, V4L2_PIX_FMT_SGRBG8, 8 },
	{ MEDIA_BUS_FMT_SRGGB8_1X8, V4L2_PIX_FMT_SRGGB8, 8 },
	{ MEDIA_BUS_FMT_SBGGR10_1X10, V4L2_PIX_FMT_SBGGR10P, 10 },
	{ MEDIA_BUS_FMT_SGBRG10_1X10, V4L2_PIX_FMT_SGBRG10P, 10 },
	{ MEDIA_BUS_FMT_SGRBG10_1X10, V4L2_PIX_FMT_SGRBG10P, 10 },
	{ MEDIA_BUS_FMT_SRGGB10_1X10, V4L2_PIX_FMT_SRGGB10P, 10 },
	{ MEDIA_BUS_FMT_SBGGR12_1X12, V4L2_PIX_FMT_SBGGR12P, 12 },
	{ MEDIA_BUS_FMT_SGBRG12_1X12, V4L2_PIX_FMT_SGBRG12P, 12 },
	{ MEDIA_BUS_FMT_SGRBG12_1X12, V4L2_PIX_FMT_SGRBG12P, 12 },
	{ MEDIA_BUS_FMT_SRGGB12_1X12, V4L2_PIX_FMT_SRGGB12P, 12 }
};

/* -----------------------------------------------------------------------------
 * Helper functions
 */

/*
 * video_mbus_to_pix_mp - Convert v4l2_mbus_framefmt to v4l2_pix_format_mplane
 * @mbus: v4l2_mbus_framefmt format (input)
 * @pix: v4l2_pix_format_mplane format (output)
 *
 * Fill the output pix structure with information from the input mbus format.
 *
 * Return 0 on success or a negative error code otherwise
 */
static unsigned int video_mbus_to_pix_mp(const struct v4l2_mbus_framefmt *mbus,
					 struct v4l2_pix_format_mplane *pix)
{
	unsigned int i;
	u32 bytesperline;

	memset(pix, 0, sizeof(*pix));
	v4l2_fill_pix_format_mplane(pix, mbus);

	for (i = 0; i < ARRAY_SIZE(formats); ++i) {
		if (formats[i].code == mbus->code)
			break;
	}

	if (WARN_ON(i == ARRAY_SIZE(formats)))
		return -EINVAL;

	pix->pixelformat = formats[i].pixelformat;
	pix->num_planes = 1;
	bytesperline = pix->width * formats[i].bpp / 8;
	bytesperline = ALIGN(bytesperline, 8);
	pix->plane_fmt[0].bytesperline = bytesperline;
	pix->plane_fmt[0].sizeimage = bytesperline * pix->height;

	return 0;
}

static struct v4l2_subdev *video_remote_subdev(struct camss_video *video,
					       u32 *pad)
{
	struct media_pad *remote;

	remote = media_entity_remote_pad(&video->pad);

	if (!remote || !is_media_entity_v4l2_subdev(remote->entity))
		return NULL;

	if (pad)
		*pad = remote->index;

	return media_entity_to_v4l2_subdev(remote->entity);
}

static int video_get_subdev_format(struct camss_video *video,
				   struct v4l2_format *format)
{
	struct v4l2_subdev_format fmt;
	struct v4l2_subdev *subdev;
	u32 pad;
	int ret;

	subdev = video_remote_subdev(video, &pad);
	if (subdev == NULL)
		return -EPIPE;

	fmt.pad = pad;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;

	ret = v4l2_subdev_call(subdev, pad, get_fmt, NULL, &fmt);
	if (ret)
		return ret;

	format->type = video->type;
	return video_mbus_to_pix_mp(&fmt.format, &format->fmt.pix_mp);
}

/* -----------------------------------------------------------------------------
 * Video queue operations
 */

static int video_queue_setup(struct vb2_queue *q,
	unsigned int *num_buffers, unsigned int *num_planes,
	unsigned int sizes[], struct device *alloc_devs[])
{
	struct camss_video *video = vb2_get_drv_priv(q);

	if (*num_planes) {
		if (*num_planes != 1)
			return -EINVAL;

		if (sizes[0] < video->active_fmt.fmt.pix_mp.plane_fmt[0].sizeimage)
			return -EINVAL;

		return 0;
	}

	*num_planes = 1;

	sizes[0] = video->active_fmt.fmt.pix_mp.plane_fmt[0].sizeimage;

	return 0;
}

static int video_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct camss_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct camss_buffer *buffer = container_of(vbuf, struct camss_buffer,
						   vb);
	struct sg_table *sgt;

	if (video->active_fmt.fmt.pix_mp.plane_fmt[0].sizeimage >
							vb2_plane_size(vb, 0))
		return -EINVAL;

	vb2_set_plane_payload(vb, 0,
			video->active_fmt.fmt.pix_mp.plane_fmt[0].sizeimage);

	sgt = vb2_dma_sg_plane_desc(vb, 0);
	if (!sgt)
		return -EFAULT;

	buffer->addr = sg_dma_address(sgt->sgl);

	vbuf->field = V4L2_FIELD_NONE;

	return 0;
}

static void video_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct camss_video *video = vb2_get_drv_priv(vb->vb2_queue);
	struct camss_buffer *buffer = container_of(vbuf, struct camss_buffer,
						   vb);

	video->ops->queue_buffer(video, buffer);
}

static int video_check_format(struct camss_video *video)
{
	struct v4l2_pix_format_mplane *pix = &video->active_fmt.fmt.pix_mp;
	struct v4l2_format format;
	struct v4l2_pix_format_mplane *sd_pix = &format.fmt.pix_mp;
	int ret;

	ret = video_get_subdev_format(video, &format);
	if (ret < 0)
		return ret;

	if (pix->pixelformat != sd_pix->pixelformat ||
	    pix->height != sd_pix->height ||
	    pix->width != sd_pix->width ||
	    pix->num_planes != sd_pix->num_planes ||
	    pix->num_planes != 1 ||
	    pix->plane_fmt[0].bytesperline != sd_pix->plane_fmt[0].bytesperline ||
	    pix->plane_fmt[0].sizeimage != sd_pix->plane_fmt[0].sizeimage ||
	    pix->field != format.fmt.pix_mp.field)
		return -EPIPE;

	return 0;
}

static int video_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct camss_video *video = vb2_get_drv_priv(q);
	struct video_device *vdev = &video->vdev;
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	int ret;

	ret = media_pipeline_start(&vdev->entity, &video->pipe);
	if (ret < 0)
		return ret;

	ret = video_check_format(video);
	if (ret < 0)
		goto error;

	entity = &vdev->entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_pad(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		ret = v4l2_subdev_call(subdev, video, s_stream, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			goto error;
	}

	return 0;

error:
	media_pipeline_stop(&vdev->entity);

	video->ops->flush_buffers(video, VB2_BUF_STATE_QUEUED);

	return ret;
}

static void video_stop_streaming(struct vb2_queue *q)
{
	struct camss_video *video = vb2_get_drv_priv(q);
	struct video_device *vdev = &video->vdev;
	struct media_entity *entity;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	struct v4l2_subdev *subdev_vfe = NULL;

	entity = &vdev->entity;
	while (1) {
		pad = &entity->pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;

		pad = media_entity_remote_pad(pad);
		if (!pad || !is_media_entity_v4l2_subdev(pad->entity))
			break;

		entity = pad->entity;
		subdev = media_entity_to_v4l2_subdev(entity);

		if (strstr(subdev->name, "vfe")) {
			subdev_vfe = subdev;
		} else if (strstr(subdev->name, "ispif")) {
			v4l2_subdev_call(subdev, video, s_stream, 0);
			v4l2_subdev_call(subdev_vfe, video, s_stream, 0);
		} else {
			v4l2_subdev_call(subdev, video, s_stream, 0);
		}
	}

	media_pipeline_stop(&vdev->entity);

	video->ops->flush_buffers(video, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops msm_video_vb2_q_ops = {
	.queue_setup     = video_queue_setup,
	.wait_prepare    = vb2_ops_wait_prepare,
	.wait_finish     = vb2_ops_wait_finish,
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
	struct camss_video *video = video_drvdata(file);

	strlcpy(cap->driver, "qcom-camss", sizeof(cap->driver));
	strlcpy(cap->card, "Qualcomm Camera Subsystem", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(video->camss->dev));

	return 0;
}

static int video_enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct camss_video *video = video_drvdata(file);

	if (f->type != video->type)
		return -EINVAL;

	if (f->index >= ARRAY_SIZE(formats))
		return -EINVAL;

	f->pixelformat = formats[f->index].pixelformat;

	return 0;
}

static int video_g_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct camss_video *video = video_drvdata(file);

	*f = video->active_fmt;

	return 0;
}

static int __video_try_fmt(struct camss_video *video, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_mp;
	u32 width, height;
	u32 bpl;
	int j;

	pix_mp = &f->fmt.pix_mp;

	for (j = 0; j < ARRAY_SIZE(formats); j++)
		if (pix_mp->pixelformat == formats[j].pixelformat)
			break;

	if (j == ARRAY_SIZE(formats))
		j = 0; /* default format */

	width = pix_mp->width;
	height = pix_mp->height;

	memset(pix_mp, 0, sizeof(*pix_mp));

	pix_mp->pixelformat = formats[j].pixelformat;
	pix_mp->width = clamp_t(u32, width, 1, 8191);
	pix_mp->height = clamp_t(u32, height, 1, 8191);
	pix_mp->num_planes = 1;
	bpl = pix_mp->width * formats[j].bpp / 8;
	bpl = ALIGN(bpl, 8);
	pix_mp->plane_fmt[0].bytesperline = bpl;
	pix_mp->plane_fmt[0].sizeimage = bpl * pix_mp->height;

	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->colorspace = V4L2_COLORSPACE_SRGB;
	pix_mp->flags = 0;
	pix_mp->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(pix_mp->colorspace);
	pix_mp->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
					pix_mp->colorspace, pix_mp->ycbcr_enc);
	pix_mp->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(pix_mp->colorspace);

	return 0;
}

static int video_try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct camss_video *video = video_drvdata(file);

	return __video_try_fmt(video, f);
}

static int video_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct camss_video *video = video_drvdata(file);
	int ret;

	if (vb2_is_busy(&video->vb2_q))
		return -EBUSY;

	ret = __video_try_fmt(video, f);
	if (ret < 0)
		return ret;

	video->active_fmt = *f;

	return 0;
}

static int video_enum_input(struct file *file, void *fh,
			    struct v4l2_input *input)
{
	if (input->index > 0)
		return -EINVAL;

	strlcpy(input->name, "camera", sizeof(input->name));
	input->type = V4L2_INPUT_TYPE_CAMERA;

	return 0;
}

static int video_g_input(struct file *file, void *fh, unsigned int *input)
{
	*input = 0;

	return 0;
}

static int video_s_input(struct file *file, void *fh, unsigned int input)
{
	return input == 0 ? 0 : -EINVAL;
}

static const struct v4l2_ioctl_ops msm_vid_ioctl_ops = {
	.vidioc_querycap		= video_querycap,
	.vidioc_enum_fmt_vid_cap_mplane	= video_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane	= video_g_fmt,
	.vidioc_s_fmt_vid_cap_mplane	= video_s_fmt,
	.vidioc_try_fmt_vid_cap_mplane	= video_try_fmt,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	.vidioc_enum_input		= video_enum_input,
	.vidioc_g_input			= video_g_input,
	.vidioc_s_input			= video_s_input,
};

/* -----------------------------------------------------------------------------
 * V4L2 file operations
 */

static int video_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct camss_video *video = video_drvdata(file);
	struct v4l2_fh *vfh;
	int ret;

	mutex_lock(&video->lock);

	vfh = kzalloc(sizeof(*vfh), GFP_KERNEL);
	if (vfh == NULL) {
		ret = -ENOMEM;
		goto error_alloc;
	}

	v4l2_fh_init(vfh, vdev);
	v4l2_fh_add(vfh);

	file->private_data = vfh;

	ret = v4l2_pipeline_pm_use(&vdev->entity, 1);
	if (ret < 0) {
		dev_err(video->camss->dev, "Failed to power up pipeline: %d\n",
			ret);
		goto error_pm_use;
	}

	mutex_unlock(&video->lock);

	return 0;

error_pm_use:
	v4l2_fh_release(file);

error_alloc:
	mutex_unlock(&video->lock);

	return ret;
}

static int video_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);

	vb2_fop_release(file);

	v4l2_pipeline_pm_use(&vdev->entity, 0);

	file->private_data = NULL;

	return 0;
}

static const struct v4l2_file_operations msm_vid_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open           = video_open,
	.release        = video_release,
	.poll           = vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
	.read		= vb2_fop_read,
};

/* -----------------------------------------------------------------------------
 * CAMSS video core
 */

static void msm_video_release(struct video_device *vdev)
{
	struct camss_video *video = video_get_drvdata(vdev);

	media_entity_cleanup(&vdev->entity);

	mutex_destroy(&video->q_lock);
	mutex_destroy(&video->lock);

	if (atomic_dec_and_test(&video->camss->ref_count))
		camss_delete(video->camss);
}

/*
 * msm_video_init_format - Helper function to initialize format
 * @video: struct camss_video
 *
 * Initialize pad format with default value.
 *
 * Return 0 on success or a negative error code otherwise
 */
static int msm_video_init_format(struct camss_video *video)
{
	int ret;
	struct v4l2_format format = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.fmt.pix_mp = {
			.width = 1920,
			.height = 1080,
			.pixelformat = formats[0].pixelformat,
		},
	};

	ret = __video_try_fmt(video, &format);
	if (ret < 0)
		return ret;

	video->active_fmt = format;

	return 0;
}

/*
 * msm_video_register - Register a video device node
 * @video: struct camss_video
 * @v4l2_dev: V4L2 device
 * @name: name to be used for the video device node
 *
 * Initialize and register a video device node to a V4L2 device. Also
 * initialize the vb2 queue.
 *
 * Return 0 on success or a negative error code otherwise
 */

int msm_video_register(struct camss_video *video, struct v4l2_device *v4l2_dev,
		       const char *name)
{
	struct media_pad *pad = &video->pad;
	struct video_device *vdev;
	struct vb2_queue *q;
	int ret;

	vdev = &video->vdev;

	mutex_init(&video->q_lock);

	q = &video->vb2_q;
	q->drv_priv = video;
	q->mem_ops = &vb2_dma_sg_memops;
	q->ops = &msm_video_vb2_q_ops;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_DMABUF | VB2_MMAP | VB2_READ;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->buf_struct_size = sizeof(struct camss_buffer);
	q->dev = video->camss->dev;
	q->lock = &video->q_lock;
	ret = vb2_queue_init(q);
	if (ret < 0) {
		dev_err(v4l2_dev->dev, "Failed to init vb2 queue: %d\n", ret);
		goto error_vb2_init;
	}

	pad->flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vdev->entity, 1, pad);
	if (ret < 0) {
		dev_err(v4l2_dev->dev, "Failed to init video entity: %d\n",
			ret);
		goto error_media_init;
	}

	mutex_init(&video->lock);

	ret = msm_video_init_format(video);
	if (ret < 0) {
		dev_err(v4l2_dev->dev, "Failed to init format: %d\n", ret);
		goto error_video_register;
	}

	vdev->fops = &msm_vid_fops;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_STREAMING |
							V4L2_CAP_READWRITE;
	vdev->ioctl_ops = &msm_vid_ioctl_ops;
	vdev->release = msm_video_release;
	vdev->v4l2_dev = v4l2_dev;
	vdev->vfl_dir = VFL_DIR_RX;
	vdev->queue = &video->vb2_q;
	vdev->lock = &video->lock;
	strlcpy(vdev->name, name, sizeof(vdev->name));

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		dev_err(v4l2_dev->dev, "Failed to register video device: %d\n",
			ret);
		goto error_video_register;
	}

	video_set_drvdata(vdev, video);
	atomic_inc(&video->camss->ref_count);

	return 0;

error_video_register:
	media_entity_cleanup(&vdev->entity);
	mutex_destroy(&video->lock);
error_media_init:
	vb2_queue_release(&video->vb2_q);
error_vb2_init:
	mutex_destroy(&video->q_lock);

	return ret;
}

void msm_video_stop_streaming(struct camss_video *video)
{
	if (vb2_is_streaming(&video->vb2_q))
		vb2_queue_release(&video->vb2_q);
}

void msm_video_unregister(struct camss_video *video)
{
	atomic_inc(&video->camss->ref_count);
	video_unregister_device(&video->vdev);
	atomic_dec(&video->camss->ref_count);
}
