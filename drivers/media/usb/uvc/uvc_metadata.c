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
#include <linux/usb/uvc.h>
#include <linux/videodev2.h>

#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

#include "uvcvideo.h"

/* -----------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int uvc_meta_v4l2_querycap(struct file *file, void *priv,
				  struct v4l2_capability *cap)
{
	struct v4l2_fh *vfh = file_to_v4l2_fh(file);
	struct uvc_streaming *stream = video_get_drvdata(vfh->vdev);
	struct uvc_video_chain *chain = stream->chain;

	strscpy(cap->driver, "uvcvideo", sizeof(cap->driver));
	strscpy(cap->card, stream->dev->name, sizeof(cap->card));
	usb_make_path(stream->dev->udev, cap->bus_info, sizeof(cap->bus_info));
	cap->capabilities = V4L2_CAP_DEVICE_CAPS | V4L2_CAP_STREAMING
			  | chain->caps;

	return 0;
}

static int uvc_meta_v4l2_get_format(struct file *file, void *priv,
				    struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file_to_v4l2_fh(file);
	struct uvc_streaming *stream = video_get_drvdata(vfh->vdev);
	struct v4l2_meta_format *fmt = &format->fmt.meta;

	if (format->type != vfh->vdev->queue->type)
		return -EINVAL;

	fmt->dataformat = stream->meta.format;
	fmt->buffersize = UVC_METADATA_BUF_SIZE;

	return 0;
}

static int uvc_meta_v4l2_try_format(struct file *file, void *priv,
				    struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file_to_v4l2_fh(file);
	struct uvc_streaming *stream = video_get_drvdata(vfh->vdev);
	struct uvc_device *dev = stream->dev;
	struct v4l2_meta_format *fmt = &format->fmt.meta;
	u32 fmeta = V4L2_META_FMT_UVC;

	if (format->type != vfh->vdev->queue->type)
		return -EINVAL;

	for (unsigned int i = 0; i < dev->nmeta_formats; i++) {
		if (dev->meta_formats[i] == fmt->dataformat) {
			fmeta = fmt->dataformat;
			break;
		}
	}

	memset(fmt, 0, sizeof(*fmt));

	fmt->dataformat = fmeta;
	fmt->buffersize = UVC_METADATA_BUF_SIZE;

	return 0;
}

static int uvc_meta_v4l2_set_format(struct file *file, void *priv,
				    struct v4l2_format *format)
{
	struct v4l2_fh *vfh = file_to_v4l2_fh(file);
	struct uvc_streaming *stream = video_get_drvdata(vfh->vdev);
	struct v4l2_meta_format *fmt = &format->fmt.meta;
	int ret;

	ret = uvc_meta_v4l2_try_format(file, priv, format);
	if (ret < 0)
		return ret;

	/*
	 * We could in principle switch at any time, also during streaming.
	 * Metadata buffers would still be perfectly parseable, but it's more
	 * consistent and cleaner to disallow that.
	 */
	if (vb2_is_busy(&stream->meta.queue.queue))
		return -EBUSY;

	stream->meta.format = fmt->dataformat;

	return 0;
}

static int uvc_meta_v4l2_enum_formats(struct file *file, void *priv,
				      struct v4l2_fmtdesc *fdesc)
{
	struct v4l2_fh *vfh = file_to_v4l2_fh(file);
	struct uvc_streaming *stream = video_get_drvdata(vfh->vdev);
	struct uvc_device *dev = stream->dev;

	if (fdesc->type != vfh->vdev->queue->type)
		return -EINVAL;

	if (fdesc->index >= dev->nmeta_formats)
		return -EINVAL;

	fdesc->pixelformat = dev->meta_formats[fdesc->index];

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

static struct uvc_entity *uvc_meta_find_msxu(struct uvc_device *dev)
{
	static const u8 uvc_msxu_guid[16] = UVC_GUID_MSXU_1_5;
	struct uvc_entity *entity;

	list_for_each_entry(entity, &dev->entities, list) {
		if (!memcmp(entity->guid, uvc_msxu_guid, sizeof(entity->guid)))
			return entity;
	}

	return NULL;
}

static int uvc_meta_detect_msxu(struct uvc_device *dev)
{
	u32 *data __free(kfree) = NULL;
	struct uvc_entity *entity;
	int ret;

	entity = uvc_meta_find_msxu(dev);
	if (!entity)
		return 0;

	/*
	 * USB requires buffers aligned in a special way, simplest way is to
	 * make sure that query_ctrl will work is to kmalloc() them.
	 */
	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/*
	 * Check if the metadata is already enabled, or if the device always
	 * returns metadata.
	 */
	ret = uvc_query_ctrl(dev, UVC_GET_CUR, entity->id, dev->intfnum,
			     UVC_MSXU_CONTROL_METADATA, data, sizeof(*data));
	if (ret)
		return 0;

	if (*data) {
		dev->quirks |= UVC_QUIRK_MSXU_META;
		return 0;
	}

	/*
	 * Set the value of UVC_MSXU_CONTROL_METADATA to the value reported by
	 * GET_MAX to enable production of MSXU metadata. The GET_MAX request
	 * reports the maximum size of the metadata, if its value is 0 then MSXU
	 * metadata is not supported. For more information, see
	 * https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/uvc-extensions-1-5#2229-metadata-control
	 */
	ret = uvc_query_ctrl(dev, UVC_GET_MAX, entity->id, dev->intfnum,
			     UVC_MSXU_CONTROL_METADATA, data, sizeof(*data));
	if (ret || !*data)
		return 0;

	/*
	 * If we can set UVC_MSXU_CONTROL_METADATA, the device will report
	 * metadata.
	 */
	ret = uvc_query_ctrl(dev, UVC_SET_CUR, entity->id, dev->intfnum,
			     UVC_MSXU_CONTROL_METADATA, data, sizeof(*data));
	if (!ret)
		dev->quirks |= UVC_QUIRK_MSXU_META;

	return 0;
}

int uvc_meta_register(struct uvc_streaming *stream)
{
	struct uvc_device *dev = stream->dev;
	struct uvc_video_queue *queue = &stream->meta.queue;

	stream->meta.format = V4L2_META_FMT_UVC;

	return uvc_register_video_device(dev, stream, queue,
					 V4L2_BUF_TYPE_META_CAPTURE,
					 &uvc_meta_fops, &uvc_meta_ioctl_ops);
}

int uvc_meta_init(struct uvc_device *dev)
{
	unsigned int i = 0;
	int ret;

	ret = uvc_meta_detect_msxu(dev);
	if (ret)
		return ret;

	dev->meta_formats[i++] = V4L2_META_FMT_UVC;

	if (dev->info->meta_format &&
	    !WARN_ON(dev->info->meta_format == V4L2_META_FMT_UVC))
		dev->meta_formats[i++] = dev->info->meta_format;

	if (dev->quirks & UVC_QUIRK_MSXU_META &&
	    !WARN_ON(dev->info->meta_format == V4L2_META_FMT_UVC_MSXU_1_5))
		dev->meta_formats[i++] = V4L2_META_FMT_UVC_MSXU_1_5;

	 /* IMPORTANT: for new meta-formats update UVC_MAX_META_DATA_FORMATS. */
	dev->nmeta_formats = i;

	return 0;
}
