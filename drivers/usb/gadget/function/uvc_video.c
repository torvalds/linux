/*
 *	uvc_video.c  --  USB Video Class Gadget driver
 *
 *	Copyright (C) 2009-2010
 *	    Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/video.h>

#include <media/v4l2-dev.h>

#include "uvc.h"
#include "uvc_queue.h"
#include "uvc_video.h"

/* --------------------------------------------------------------------------
 * Video codecs
 */

static int
uvc_video_encode_header(struct uvc_video *video, struct uvc_buffer *buf,
		u8 *data, int len)
{
	data[0] = 2;
	data[1] = UVC_STREAM_EOH | video->fid;

	if (buf->bytesused - video->queue.buf_used <= len - 2)
		data[1] |= UVC_STREAM_EOF;

	return 2;
}

static int
uvc_video_encode_data(struct uvc_video *video, struct uvc_buffer *buf,
		u8 *data, int len)
{
	struct uvc_video_queue *queue = &video->queue;
	unsigned int nbytes;
	void *mem;

	/* Copy video data to the USB buffer. */
	mem = buf->mem + queue->buf_used;
	nbytes = min((unsigned int)len, buf->bytesused - queue->buf_used);

	memcpy(data, mem, nbytes);
	queue->buf_used += nbytes;

	return nbytes;
}

static void
uvc_video_encode_bulk(struct usb_request *req, struct uvc_video *video,
		struct uvc_buffer *buf)
{
	void *mem = req->buf;
	int len = video->req_size;
	int ret;

	/* Add a header at the beginning of the payload. */
	if (video->payload_size == 0) {
		ret = uvc_video_encode_header(video, buf, mem, len);
		video->payload_size += ret;
		mem += ret;
		len -= ret;
	}

	/* Process video data. */
	len = min((int)(video->max_payload_size - video->payload_size), len);
	ret = uvc_video_encode_data(video, buf, mem, len);

	video->payload_size += ret;
	len -= ret;

	req->length = video->req_size - len;
	req->zero = video->payload_size == video->max_payload_size;

	if (buf->bytesused == video->queue.buf_used) {
		video->queue.buf_used = 0;
		buf->state = UVC_BUF_STATE_DONE;
		uvcg_queue_next_buffer(&video->queue, buf);
		video->fid ^= UVC_STREAM_FID;

		video->payload_size = 0;
	}

	if (video->payload_size == video->max_payload_size ||
	    buf->bytesused == video->queue.buf_used)
		video->payload_size = 0;
}

static void
uvc_video_encode_isoc(struct usb_request *req, struct uvc_video *video,
		struct uvc_buffer *buf)
{
	void *mem = req->buf;
	int len = video->req_size;
	int ret;

	/* Add the header. */
	ret = uvc_video_encode_header(video, buf, mem, len);
	mem += ret;
	len -= ret;

	/* Process video data. */
	ret = uvc_video_encode_data(video, buf, mem, len);
	len -= ret;

	req->length = video->req_size - len;

	if (buf->bytesused == video->queue.buf_used) {
		video->queue.buf_used = 0;
		buf->state = UVC_BUF_STATE_DONE;
		uvcg_queue_next_buffer(&video->queue, buf);
		video->fid ^= UVC_STREAM_FID;
	}
}

/* --------------------------------------------------------------------------
 * Request handling
 */

/*
 * I somehow feel that synchronisation won't be easy to achieve here. We have
 * three events that control USB requests submission:
 *
 * - USB request completion: the completion handler will resubmit the request
 *   if a video buffer is available.
 *
 * - USB interface setting selection: in response to a SET_INTERFACE request,
 *   the handler will start streaming if a video buffer is available and if
 *   video is not currently streaming.
 *
 * - V4L2 buffer queueing: the driver will start streaming if video is not
 *   currently streaming.
 *
 * Race conditions between those 3 events might lead to deadlocks or other
 * nasty side effects.
 *
 * The "video currently streaming" condition can't be detected by the irqqueue
 * being empty, as a request can still be in flight. A separate "queue paused"
 * flag is thus needed.
 *
 * The paused flag will be set when we try to retrieve the irqqueue head if the
 * queue is empty, and cleared when we queue a buffer.
 *
 * The USB request completion handler will get the buffer at the irqqueue head
 * under protection of the queue spinlock. If the queue is empty, the streaming
 * paused flag will be set. Right after releasing the spinlock a userspace
 * application can queue a buffer. The flag will then cleared, and the ioctl
 * handler will restart the video stream.
 */
static void
uvc_video_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct uvc_video *video = req->context;
	struct uvc_video_queue *queue = &video->queue;
	struct uvc_buffer *buf;
	unsigned long flags;
	int ret;

	switch (req->status) {
	case 0:
		break;

	case -ESHUTDOWN:	/* disconnect from host. */
		printk(KERN_DEBUG "VS request cancelled.\n");
		uvcg_queue_cancel(queue, 1);
		goto requeue;

	default:
		printk(KERN_INFO "VS request completed with status %d.\n",
			req->status);
		uvcg_queue_cancel(queue, 0);
		goto requeue;
	}

	spin_lock_irqsave(&video->queue.irqlock, flags);
	buf = uvcg_queue_head(&video->queue);
	if (buf == NULL) {
		spin_unlock_irqrestore(&video->queue.irqlock, flags);
		goto requeue;
	}

	video->encode(req, video, buf);

	if ((ret = usb_ep_queue(ep, req, GFP_ATOMIC)) < 0) {
		printk(KERN_INFO "Failed to queue request (%d).\n", ret);
		usb_ep_set_halt(ep);
		spin_unlock_irqrestore(&video->queue.irqlock, flags);
		uvcg_queue_cancel(queue, 0);
		goto requeue;
	}
	spin_unlock_irqrestore(&video->queue.irqlock, flags);

	return;

requeue:
	spin_lock_irqsave(&video->req_lock, flags);
	list_add_tail(&req->list, &video->req_free);
	spin_unlock_irqrestore(&video->req_lock, flags);
}

static int
uvc_video_free_requests(struct uvc_video *video)
{
	unsigned int i;

	for (i = 0; i < UVC_NUM_REQUESTS; ++i) {
		if (video->req[i]) {
			usb_ep_free_request(video->ep, video->req[i]);
			video->req[i] = NULL;
		}

		if (video->req_buffer[i]) {
			kfree(video->req_buffer[i]);
			video->req_buffer[i] = NULL;
		}
	}

	INIT_LIST_HEAD(&video->req_free);
	video->req_size = 0;
	return 0;
}

static int
uvc_video_alloc_requests(struct uvc_video *video)
{
	unsigned int req_size;
	unsigned int i;
	int ret = -ENOMEM;

	BUG_ON(video->req_size);

	req_size = video->ep->maxpacket
		 * max_t(unsigned int, video->ep->maxburst, 1)
		 * (video->ep->mult + 1);

	for (i = 0; i < UVC_NUM_REQUESTS; ++i) {
		video->req_buffer[i] = kmalloc(req_size, GFP_KERNEL);
		if (video->req_buffer[i] == NULL)
			goto error;

		video->req[i] = usb_ep_alloc_request(video->ep, GFP_KERNEL);
		if (video->req[i] == NULL)
			goto error;

		video->req[i]->buf = video->req_buffer[i];
		video->req[i]->length = 0;
		video->req[i]->complete = uvc_video_complete;
		video->req[i]->context = video;

		list_add_tail(&video->req[i]->list, &video->req_free);
	}

	video->req_size = req_size;

	return 0;

error:
	uvc_video_free_requests(video);
	return ret;
}

/* --------------------------------------------------------------------------
 * Video streaming
 */

/*
 * uvcg_video_pump - Pump video data into the USB requests
 *
 * This function fills the available USB requests (listed in req_free) with
 * video data from the queued buffers.
 */
int uvcg_video_pump(struct uvc_video *video)
{
	struct uvc_video_queue *queue = &video->queue;
	struct usb_request *req;
	struct uvc_buffer *buf;
	unsigned long flags;
	int ret;

	/* FIXME TODO Race between uvcg_video_pump and requests completion
	 * handler ???
	 */

	while (1) {
		/* Retrieve the first available USB request, protected by the
		 * request lock.
		 */
		spin_lock_irqsave(&video->req_lock, flags);
		if (list_empty(&video->req_free)) {
			spin_unlock_irqrestore(&video->req_lock, flags);
			return 0;
		}
		req = list_first_entry(&video->req_free, struct usb_request,
					list);
		list_del(&req->list);
		spin_unlock_irqrestore(&video->req_lock, flags);

		/* Retrieve the first available video buffer and fill the
		 * request, protected by the video queue irqlock.
		 */
		spin_lock_irqsave(&queue->irqlock, flags);
		buf = uvcg_queue_head(queue);
		if (buf == NULL) {
			spin_unlock_irqrestore(&queue->irqlock, flags);
			break;
		}

		video->encode(req, video, buf);

		/* Queue the USB request */
		ret = usb_ep_queue(video->ep, req, GFP_ATOMIC);
		if (ret < 0) {
			printk(KERN_INFO "Failed to queue request (%d)\n", ret);
			usb_ep_set_halt(video->ep);
			spin_unlock_irqrestore(&queue->irqlock, flags);
			uvcg_queue_cancel(queue, 0);
			break;
		}
		spin_unlock_irqrestore(&queue->irqlock, flags);
	}

	spin_lock_irqsave(&video->req_lock, flags);
	list_add_tail(&req->list, &video->req_free);
	spin_unlock_irqrestore(&video->req_lock, flags);
	return 0;
}

/*
 * Enable or disable the video stream.
 */
int uvcg_video_enable(struct uvc_video *video, int enable)
{
	unsigned int i;
	int ret;

	if (video->ep == NULL) {
		printk(KERN_INFO "Video enable failed, device is "
			"uninitialized.\n");
		return -ENODEV;
	}

	if (!enable) {
		for (i = 0; i < UVC_NUM_REQUESTS; ++i)
			if (video->req[i])
				usb_ep_dequeue(video->ep, video->req[i]);

		uvc_video_free_requests(video);
		uvcg_queue_enable(&video->queue, 0);
		return 0;
	}

	if ((ret = uvcg_queue_enable(&video->queue, 1)) < 0)
		return ret;

	if ((ret = uvc_video_alloc_requests(video)) < 0)
		return ret;

	if (video->max_payload_size) {
		video->encode = uvc_video_encode_bulk;
		video->payload_size = 0;
	} else
		video->encode = uvc_video_encode_isoc;

	return uvcg_video_pump(video);
}

/*
 * Initialize the UVC video stream.
 */
int uvcg_video_init(struct uvc_video *video)
{
	INIT_LIST_HEAD(&video->req_free);
	spin_lock_init(&video->req_lock);

	video->fcc = V4L2_PIX_FMT_YUYV;
	video->bpp = 16;
	video->width = 320;
	video->height = 240;
	video->imagesize = 320 * 240 * 2;

	/* Initialize the video buffers queue. */
	uvcg_queue_init(&video->queue, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	return 0;
}

