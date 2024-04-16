// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      uvc_metadata.c  --  USB Video Class driver - Metadata handling
 *
 *      Copyright (C) 2016
 *          Guennadi Liakhovetski (guennadi.liakhovetski@intel.com)
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/videodev2.h>

#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

#include "uvcvideo.h"

/* -----------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int uvc_meta_v4l2_querycap(struct file *file, void *fh,
				  struct v4l2_capability *cap)
{
	struct v4l2_fh *vfh = file->private_data;
	struct uvc_streaming *stream = video_get_drvdata(vfh->vdev);
	struct uvc_video_chain *chain = stream->chain;

	strscpy(cap->driver, "uvcvideo", sizeof(cap->driver));
	strscpy(cap->card, stream->dev->name, sizeof(cap->card));
	usb_make_path(stream->dev->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->capabilities = V4L2_CAP_DEVICE_CAPS | V4L2_CAP_STREAMING
			  | chain->caps;

	return 0;
}

static int uvc_meta_v4l2_get_format(struct file *file, void *fh,
				    struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct uvc_streaming *stream = video_get_drvdata(vfh->vdev);
	struct v4l2_meta_format *fmt = &format->fmt.meta;

	if (format->type != vfh->vdev->queue->type)
		return -EINVAL;

	memset(fmt, 0, sizeof(*fmt));

	fmt->dataformat = stream->meta.format;
	fmt->buffersize = UVC_METADATA_BUF_SIZE;

	return 0;
}

static int uvc_meta_v4l2_try_format(struct file *file, void *fh,
				    struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct uvc_streaming *stream = video_get_drvdata(vfh->vdev);
	struct uvc_device *dev = stream->dev;
	struct v4l2_meta_format *fmt = &format->fmt.meta;
	u32 fmeta = fmt->dataformat;

	if (format->type != vfh->vdev->queue->type)
		return -EINVAL;

	memset(fmt, 0, sizeof(*fmt));

	fmt->dataformat = fmeta == dev->info->meta_format
			? fmeta : V4L2_META_FMT_UVC;
	fmt->buffersize = UVC_METADATA_BUF_SIZE;

	return 0;
}

static int uvc_meta_v4l2_set_format(struct file *file, void *fh,
				    struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file->private_data;
	struct uvc_streaming *stream = video_get_drvdata(vfh->vdev);
	struct v4l2_meta_format *fmt = &format->fmt.meta;
	int ret;

	ret = uvc_meta_v4l2_try_format(file, fh, format);
	if (ret < 0)
		return ret;

	/*
	 * We could in principle switch at any time, also during streaming.
	 * Metadata buffers would still be perfectly parseable, but it's more
	 * consistent and cleaner to disallow that.
	 */
	mutex_lock(&stream->mutex);

	if (uvc_queue_allocated(&stream->queue))
		ret = -EBUSY;
	else
		stream->meta.format = fmt->dataformat;

	mutex_unlock(&stream->mutex);

	return ret;
}

static int uvc_meta_v4l2_enum_formats(struct file *file, void *fh,
				      struct v4l2_fmtdesc *fdesc)
{
	struct v4l2_fh *vfh = file->private_data;
	struct uvc_streaming *stream = video_get_drvdata(vfh->vdev);
	struct uvc_device *dev = stream->dev;
	u32 index = fdesc->index;

	if (fdesc->type != vfh->vdev->queue->type ||
	    index > 1U || (index && !dev->info->meta_format))
		return -EINVAL;

	memset(fdesc, 0, sizeof(*fdesc));

	fdesc->type = vfh->vdev->queue->type;
	fdesc->index = index;
	fdesc->pixelformat = index ? dev->info->meta_format : V4L2_META_FMT_UVC;

	return 0;
}

static const struct v4l2_ioctl_ops uvc_meta_ioctl_ops = {
	.vidioc_querycap		= uvc_meta_v4l2_querycap,
	.vidioc_g_fmt_meta_cap		= uvc_meta_v4l2_get_format,
	.vidioc_s_fmt_meta_cap		= uvc_meta_v4l2_set_format,
	.vidioc_try_fmt_meta_cap	= uvc_meta_v4l2_try_format,
	.vidioc_enum_fmt_meta_cap	= uvc_meta_v4l2_enum_formats,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

/* -----------------------------------------------------------------------------
 * V4L2 File Operations
 */

static const struct v4l2_file_operations uvc_meta_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
};

int uvc_meta_register(struct uvc_streaming *stream)
{
	struct uvc_device *dev = stream->dev;
	struct video_device *vdev = &stream->meta.vdev;
	struct uvc_video_queue *queue = &stream->meta.queue;

	stream->meta.format = V4L2_META_FMT_UVC;

	/*
	 * The video interface queue uses manual locking and thus does not set
	 * the queue pointer. Set it manually here.
	 */
	vdev->queue = &queue->queue;

	return uvc_register_video_device(dev, stream, vdev, queue,
					 V4L2_BUF_TYPE_META_CAPTURE,
					 &uvc_meta_fops, &uvc_meta_ioctl_ops);
}
