/*
 *	uvc_v4l2.c  --  USB Video Class Gadget driver
 *
 *	Copyright (C) 2009-2010
 *	    Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

#include <media/v4l2-dev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

#include "uvc.h"
#include "uvc_queue.h"

/* --------------------------------------------------------------------------
 * Requests handling
 */

static int
uvc_send_response(struct uvc_device *uvc, struct uvc_request_data *data)
{
	struct usb_composite_dev *cdev = uvc->func.config->cdev;
	struct usb_request *req = uvc->control_req;

	if (data->length < 0)
		return usb_ep_set_halt(cdev->gadget->ep0);

	req->length = min(uvc->event_length, data->length);
	req->zero = data->length < uvc->event_length;
	req->dma = DMA_ADDR_INVALID;

	memcpy(req->buf, data->data, data->length);

	return usb_ep_queue(cdev->gadget->ep0, req, GFP_KERNEL);
}

/* --------------------------------------------------------------------------
 * V4L2
 */

struct uvc_format
{
	u8 bpp;
	u32 fcc;
};

static struct uvc_format uvc_formats[] = {
	{ 16, V4L2_PIX_FMT_YUYV  },
	{ 0,  V4L2_PIX_FMT_MJPEG },
};

static int
uvc_v4l2_get_format(struct uvc_video *video, struct v4l2_format *fmt)
{
	fmt->fmt.pix.pixelformat = video->fcc;
	fmt->fmt.pix.width = video->width;
	fmt->fmt.pix.height = video->height;
	fmt->fmt.pix.field = V4L2_FIELD_NONE;
	fmt->fmt.pix.bytesperline = video->bpp * video->width / 8;
	fmt->fmt.pix.sizeimage = video->imagesize;
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	fmt->fmt.pix.priv = 0;

	return 0;
}

static int
uvc_v4l2_set_format(struct uvc_video *video, struct v4l2_format *fmt)
{
	struct uvc_format *format;
	unsigned int imagesize;
	unsigned int bpl;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(uvc_formats); ++i) {
		format = &uvc_formats[i];
		if (format->fcc == fmt->fmt.pix.pixelformat)
			break;
	}

	if (i == ARRAY_SIZE(uvc_formats)) {
		printk(KERN_INFO "Unsupported format 0x%08x.\n",
			fmt->fmt.pix.pixelformat);
		return -EINVAL;
	}

	bpl = format->bpp * fmt->fmt.pix.width / 8;
	imagesize = bpl ? bpl * fmt->fmt.pix.height : fmt->fmt.pix.sizeimage;

	video->fcc = format->fcc;
	video->bpp = format->bpp;
	video->width = fmt->fmt.pix.width;
	video->height = fmt->fmt.pix.height;
	video->imagesize = imagesize;

	fmt->fmt.pix.field = V4L2_FIELD_NONE;
	fmt->fmt.pix.bytesperline = bpl;
	fmt->fmt.pix.sizeimage = imagesize;
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	fmt->fmt.pix.priv = 0;

	return 0;
}

static int
uvc_v4l2_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_file_handle *handle;
	int ret;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (handle == NULL)
		return -ENOMEM;

	ret = v4l2_fh_init(&handle->vfh, vdev);
	if (ret < 0)
		goto error;

	ret = v4l2_event_init(&handle->vfh);
	if (ret < 0)
		goto error;

	ret = v4l2_event_alloc(&handle->vfh, 8);
	if (ret < 0)
		goto error;

	v4l2_fh_add(&handle->vfh);

	handle->device = &uvc->video;
	file->private_data = &handle->vfh;

	uvc_function_connect(uvc);
	return 0;

error:
	v4l2_fh_exit(&handle->vfh);
	return ret;
}

static int
uvc_v4l2_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_file_handle *handle = to_uvc_file_handle(file->private_data);
	struct uvc_video *video = handle->device;

	uvc_function_disconnect(uvc);

	uvc_video_enable(video, 0);
	mutex_lock(&video->queue.mutex);
	if (uvc_free_buffers(&video->queue) < 0)
		printk(KERN_ERR "uvc_v4l2_release: Unable to free "
				"buffers.\n");
	mutex_unlock(&video->queue.mutex);

	file->private_data = NULL;
	v4l2_fh_del(&handle->vfh);
	v4l2_fh_exit(&handle->vfh);
	kfree(handle);
	return 0;
}

static long
uvc_v4l2_do_ioctl(struct file *file, unsigned int cmd, void *arg)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_file_handle *handle = to_uvc_file_handle(file->private_data);
	struct usb_composite_dev *cdev = uvc->func.config->cdev;
	struct uvc_video *video = &uvc->video;
	int ret = 0;

	switch (cmd) {
	/* Query capabilities */
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;

		memset(cap, 0, sizeof *cap);
		strncpy(cap->driver, "g_uvc", sizeof(cap->driver));
		strncpy(cap->card, cdev->gadget->name, sizeof(cap->card));
		strncpy(cap->bus_info, dev_name(&cdev->gadget->dev),
			sizeof cap->bus_info);
		cap->version = DRIVER_VERSION_NUMBER;
		cap->capabilities = V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_STREAMING;
		break;
	}

	/* Get & Set format */
	case VIDIOC_G_FMT:
	{
		struct v4l2_format *fmt = arg;

		if (fmt->type != video->queue.type)
			return -EINVAL;

		return uvc_v4l2_get_format(video, fmt);
	}

	case VIDIOC_S_FMT:
	{
		struct v4l2_format *fmt = arg;

		if (fmt->type != video->queue.type)
			return -EINVAL;

		return uvc_v4l2_set_format(video, fmt);
	}

	/* Buffers & streaming */
	case VIDIOC_REQBUFS:
	{
		struct v4l2_requestbuffers *rb = arg;

		if (rb->type != video->queue.type ||
		    rb->memory != V4L2_MEMORY_MMAP)
			return -EINVAL;

		ret = uvc_alloc_buffers(&video->queue, rb->count,
					video->imagesize);
		if (ret < 0)
			return ret;

		rb->count = ret;
		ret = 0;
		break;
	}

	case VIDIOC_QUERYBUF:
	{
		struct v4l2_buffer *buf = arg;

		if (buf->type != video->queue.type)
			return -EINVAL;

		return uvc_query_buffer(&video->queue, buf);
	}

	case VIDIOC_QBUF:
		if ((ret = uvc_queue_buffer(&video->queue, arg)) < 0)
			return ret;

		return uvc_video_pump(video);

	case VIDIOC_DQBUF:
		return uvc_dequeue_buffer(&video->queue, arg,
			file->f_flags & O_NONBLOCK);

	case VIDIOC_STREAMON:
	{
		int *type = arg;

		if (*type != video->queue.type)
			return -EINVAL;

		return uvc_video_enable(video, 1);
	}

	case VIDIOC_STREAMOFF:
	{
		int *type = arg;

		if (*type != video->queue.type)
			return -EINVAL;

		return uvc_video_enable(video, 0);
	}

	/* Events */
        case VIDIOC_DQEVENT:
	{
		struct v4l2_event *event = arg;

		ret = v4l2_event_dequeue(&handle->vfh, event,
					 file->f_flags & O_NONBLOCK);
		if (ret == 0 && event->type == UVC_EVENT_SETUP) {
			struct uvc_event *uvc_event = (void *)&event->u.data;

			/* Tell the complete callback to generate an event for
			 * the next request that will be enqueued by
			 * uvc_event_write.
			 */
			uvc->event_setup_out =
				!(uvc_event->req.bRequestType & USB_DIR_IN);
			uvc->event_length = uvc_event->req.wLength;
		}

		return ret;
	}

	case VIDIOC_SUBSCRIBE_EVENT:
	{
		struct v4l2_event_subscription *sub = arg;

		if (sub->type < UVC_EVENT_FIRST || sub->type > UVC_EVENT_LAST)
			return -EINVAL;

		return v4l2_event_subscribe(&handle->vfh, arg);
	}

	case VIDIOC_UNSUBSCRIBE_EVENT:
		return v4l2_event_unsubscribe(&handle->vfh, arg);

	case UVCIOC_SEND_RESPONSE:
		ret = uvc_send_response(uvc, arg);
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return ret;
}

static long
uvc_v4l2_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return video_usercopy(file, cmd, arg, uvc_v4l2_do_ioctl);
}

static int
uvc_v4l2_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);

	return uvc_queue_mmap(&uvc->video.queue, vma);
}

static unsigned int
uvc_v4l2_poll(struct file *file, poll_table *wait)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_file_handle *handle = to_uvc_file_handle(file->private_data);
	unsigned int mask = 0;

	poll_wait(file, &handle->vfh.events->wait, wait);
	if (v4l2_event_pending(&handle->vfh))
		mask |= POLLPRI;

	mask |= uvc_queue_poll(&uvc->video.queue, file, wait);

	return mask;
}

static struct v4l2_file_operations uvc_v4l2_fops = {
	.owner		= THIS_MODULE,
	.open		= uvc_v4l2_open,
	.release	= uvc_v4l2_release,
	.ioctl		= uvc_v4l2_ioctl,
	.mmap		= uvc_v4l2_mmap,
	.poll		= uvc_v4l2_poll,
};

