// SPDX-License-Identifier: GPL-2.0+
/*
 *	uvc_video.c  --  USB Video Class Gadget driver
 *
 *	Copyright (C) 2009-2010
 *	    Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/video.h>
#include <linux/pm_qos.h>

#include <media/v4l2-dev.h>

#include "uvc.h"
#include "uvc_queue.h"
#include "uvc_video.h"
#include "u_uvc.h"

#if defined(CONFIG_ARCH_ROCKCHIP) && defined(CONFIG_NO_GKI)
static bool uvc_using_zero_copy(struct uvc_video *video)
{
	struct uvc_device *uvc = container_of(video, struct uvc_device, video);
	struct f_uvc_opts *opts = fi_to_f_uvc_opts(uvc->func.fi);

	if (opts && opts->uvc_zero_copy && video->fcc != V4L2_PIX_FMT_YUYV)
		return true;
	else
		return false;
}

static void uvc_wait_req_complete(struct uvc_video *video, struct uvc_request *ureq)
{
	unsigned long flags;
	struct usb_request *req;
	int ret;

	spin_lock_irqsave(&video->req_lock, flags);

	list_for_each_entry(req, &video->req_free, list) {
		if (req == ureq->req)
			break;
	}

	if (req != ureq->req) {
		reinit_completion(&ureq->req_done);

		spin_unlock_irqrestore(&video->req_lock, flags);
		ret = wait_for_completion_timeout(&ureq->req_done,
						  msecs_to_jiffies(500));
		if (ret == 0)
			uvcg_warn(&video->uvc->func,
				  "timed out waiting for req done\n");
		return;
	}

	spin_unlock_irqrestore(&video->req_lock, flags);
}
#else
static inline bool uvc_using_zero_copy(struct uvc_video *video)
{
	return false;
}

static inline void uvc_wait_req_complete(struct uvc_video *video, struct uvc_request *ureq)
{ }
#endif

/* --------------------------------------------------------------------------
 * Video codecs
 */

static int
uvc_video_encode_header(struct uvc_video *video, struct uvc_buffer *buf,
		u8 *data, int len)
{
	if (uvc_using_zero_copy(video)) {
		u8 *mem;

		mem = buf->mem + video->queue.buf_used +
		      (video->queue.buf_used / (video->req_size - 2)) * 2;

		mem[0] = 2;
		mem[1] = UVC_STREAM_EOH | video->fid;
		if (buf->bytesused - video->queue.buf_used <= len - 2)
			mem[1] |= UVC_STREAM_EOF;

		return 2;
	}

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

	if (!uvc_using_zero_copy(video))
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
		req->zero = 1;
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

	if (uvc_using_zero_copy(video))
		req->buf = buf->mem + video->queue.buf_used +
			   (video->queue.buf_used / (video->req_size - 2)) * 2;

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

static int uvcg_video_ep_queue(struct uvc_video *video, struct usb_request *req)
{
	int ret;

	ret = usb_ep_queue(video->ep, req, GFP_ATOMIC);
	if (ret < 0) {
		uvcg_err(&video->uvc->func, "Failed to queue request (%d).\n",
			 ret);

		/* If the endpoint is disabled the descriptor may be NULL. */
		if (video->ep->desc) {
			/* Isochronous endpoints can't be halted. */
			if (usb_endpoint_xfer_bulk(video->ep->desc))
				usb_ep_set_halt(video->ep);
		}
	}

	return ret;
}

static void
uvc_video_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct uvc_request *ureq = req->context;
	struct uvc_video *video = ureq->video;
	struct uvc_video_queue *queue = &video->queue;
	struct uvc_device *uvc = video->uvc;
	unsigned long flags;

	switch (req->status) {
	case 0:
		break;

	case -ESHUTDOWN:	/* disconnect from host. */
		uvcg_dbg(&video->uvc->func, "VS request cancelled.\n");
		uvcg_queue_cancel(queue, 1);
		break;

	default:
		uvcg_info(&video->uvc->func,
			  "VS request completed with status %d.\n",
			  req->status);
		uvcg_queue_cancel(queue, 0);
	}

	spin_lock_irqsave(&video->req_lock, flags);
	list_add_tail(&req->list, &video->req_free);
#if defined(CONFIG_ARCH_ROCKCHIP) && defined(CONFIG_NO_GKI)
	complete(&ureq->req_done);
#endif
	spin_unlock_irqrestore(&video->req_lock, flags);

	if (uvc->state == UVC_STATE_STREAMING)
		schedule_work(&video->pump);
}

static int
uvc_video_free_requests(struct uvc_video *video)
{
	unsigned int i;

	if (video->ureq) {
		for (i = 0; i < video->uvc_num_requests; ++i) {
			if (video->ureq[i].req) {
				uvc_wait_req_complete(video, &video->ureq[i]);
				usb_ep_free_request(video->ep, video->ureq[i].req);
				video->ureq[i].req = NULL;
			}

			if (video->ureq[i].req_buffer) {
				kfree(video->ureq[i].req_buffer);
				video->ureq[i].req_buffer = NULL;
			}
		}

		kfree(video->ureq);
		video->ureq = NULL;
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

	if (!usb_endpoint_xfer_bulk(video->ep->desc)) {
		req_size = video->ep->maxpacket
			 * max_t(unsigned int, video->ep->maxburst, 1)
			 * (video->ep->mult);
	} else {
		req_size = video->ep->maxpacket
			 * max_t(unsigned int, video->ep->maxburst, 1);
	}

	video->ureq = kcalloc(video->uvc_num_requests, sizeof(struct uvc_request), GFP_KERNEL);
	if (video->ureq == NULL)
		return -ENOMEM;

	for (i = 0; i < video->uvc_num_requests; ++i) {
		video->ureq[i].req_buffer = kmalloc(req_size, GFP_KERNEL);
		if (video->ureq[i].req_buffer == NULL)
			goto error;

		video->ureq[i].req = usb_ep_alloc_request(video->ep, GFP_KERNEL);
		if (video->ureq[i].req == NULL)
			goto error;

		video->ureq[i].req->buf = video->ureq[i].req_buffer;
		video->ureq[i].req->length = 0;
		video->ureq[i].req->complete = uvc_video_complete;
		video->ureq[i].req->context = &video->ureq[i];
		video->ureq[i].video = video;

#if defined(CONFIG_ARCH_ROCKCHIP) && defined(CONFIG_NO_GKI)
		init_completion(&video->ureq[i].req_done);
#endif
		list_add_tail(&video->ureq[i].req->list, &video->req_free);
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
static void uvcg_video_pump(struct work_struct *work)
{
	struct uvc_video *video = container_of(work, struct uvc_video, pump);
	struct uvc_video_queue *queue = &video->queue;
	struct usb_request *req = NULL;
	struct uvc_buffer *buf;
	unsigned long flags;
	int ret;

	while (video->ep->enabled) {
		/* Retrieve the first available USB request, protected by the
		 * request lock.
		 */
		spin_lock_irqsave(&video->req_lock, flags);
		if (list_empty(&video->req_free)) {
			spin_unlock_irqrestore(&video->req_lock, flags);
			return;
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
		ret = uvcg_video_ep_queue(video, req);
		spin_unlock_irqrestore(&queue->irqlock, flags);

		if (ret < 0) {
			uvcg_queue_cancel(queue, 0);
			break;
		}

		/* Endpoint now owns the request */
		req = NULL;
	}

	if (!req)
		return;

	spin_lock_irqsave(&video->req_lock, flags);
	list_add_tail(&req->list, &video->req_free);
	spin_unlock_irqrestore(&video->req_lock, flags);
	return;
}

/*
 * Enable or disable the video stream.
 */
int uvcg_video_enable(struct uvc_video *video, int enable)
{
	unsigned int i;
	int ret;
	struct uvc_device *uvc;
	struct f_uvc_opts *opts;

	if (video->ep == NULL) {
		uvcg_info(&video->uvc->func,
			  "Video enable failed, device is uninitialized.\n");
		return -ENODEV;
	}

	uvc = container_of(video, struct uvc_device, video);
	opts = fi_to_f_uvc_opts(uvc->func.fi);

	if (!enable) {
		cancel_work_sync(&video->pump);
		uvcg_queue_cancel(&video->queue, 0);

		for (i = 0; i < video->uvc_num_requests; ++i)
			if (video->ureq && video->ureq[i].req)
				usb_ep_dequeue(video->ep, video->ureq[i].req);

		uvc_video_free_requests(video);
		uvcg_queue_enable(&video->queue, 0);
		if (cpu_latency_qos_request_active(&uvc->pm_qos))
			cpu_latency_qos_remove_request(&uvc->pm_qos);
		return 0;
	}

	cpu_latency_qos_add_request(&uvc->pm_qos, opts->pm_qos_latency);
	if ((ret = uvcg_queue_enable(&video->queue, 1)) < 0)
		return ret;

	if ((ret = uvc_video_alloc_requests(video)) < 0)
		return ret;

	if (video->max_payload_size) {
		video->encode = uvc_video_encode_bulk;
		video->payload_size = 0;
	} else
		video->encode = uvc_video_encode_isoc;

	schedule_work(&video->pump);

	return ret;
}

/*
 * Initialize the UVC video stream.
 */
int uvcg_video_init(struct uvc_video *video, struct uvc_device *uvc)
{
	INIT_LIST_HEAD(&video->req_free);
	spin_lock_init(&video->req_lock);
	INIT_WORK(&video->pump, uvcg_video_pump);

	video->uvc = uvc;
	video->fcc = V4L2_PIX_FMT_YUYV;
	video->bpp = 16;
	video->width = 320;
	video->height = 240;
	video->imagesize = 320 * 240 * 2;

	/* Initialize the video buffers queue. */
	uvcg_queue_init(&video->queue, V4L2_BUF_TYPE_VIDEO_OUTPUT,
			&video->mutex);
	return 0;
}

