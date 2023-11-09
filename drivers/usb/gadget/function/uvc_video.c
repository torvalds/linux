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
#include <asm/unaligned.h>

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
	struct uvc_device *uvc = container_of(video, struct uvc_device, video);
	struct usb_composite_dev *cdev = uvc->func.config->cdev;
	struct timespec64 ts = ns_to_timespec64(buf->buf.vb2_buf.timestamp);
	int pos = 2;

	data[1] = UVC_STREAM_EOH | video->fid;

	if (video->queue.buf_used == 0 && ts.tv_sec) {
		/* dwClockFrequency is 48 MHz */
		u32 pts = ((u64)ts.tv_sec * USEC_PER_SEC + ts.tv_nsec / NSEC_PER_USEC) * 48;

		data[1] |= UVC_STREAM_PTS;
		put_unaligned_le32(pts, &data[pos]);
		pos += 4;
	}

	if (cdev->gadget->ops->get_frame) {
		u32 sof, stc;

		sof = usb_gadget_frame_number(cdev->gadget);
		ktime_get_ts64(&ts);
		stc = ((u64)ts.tv_sec * USEC_PER_SEC + ts.tv_nsec / NSEC_PER_USEC) * 48;

		data[1] |= UVC_STREAM_SCR;
		put_unaligned_le32(stc, &data[pos]);
		put_unaligned_le16(sof, &data[pos+4]);
		pos += 6;
	}

	data[0] = pos;

	if (buf->bytesused - video->queue.buf_used <= len - pos)
		data[1] |= UVC_STREAM_EOF;

	return pos;
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
	struct uvc_request *ureq = req->context;
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
		list_del(&buf->queue);
		video->fid ^= UVC_STREAM_FID;
		ureq->last_buf = buf;

		video->payload_size = 0;
	}

	if (video->payload_size == video->max_payload_size ||
	    video->queue.flags & UVC_QUEUE_DROP_INCOMPLETE ||
	    buf->bytesused == video->queue.buf_used)
		video->payload_size = 0;
}

static void
uvc_video_encode_isoc_sg(struct usb_request *req, struct uvc_video *video,
		struct uvc_buffer *buf)
{
	unsigned int pending = buf->bytesused - video->queue.buf_used;
	struct uvc_request *ureq = req->context;
	struct scatterlist *sg, *iter;
	unsigned int len = video->req_size;
	unsigned int sg_left, part = 0;
	unsigned int i;
	int header_len;

	sg = ureq->sgt.sgl;
	sg_init_table(sg, ureq->sgt.nents);

	/* Init the header. */
	header_len = uvc_video_encode_header(video, buf, ureq->header,
				      video->req_size);
	sg_set_buf(sg, ureq->header, header_len);
	len -= header_len;

	if (pending <= len)
		len = pending;

	req->length = (len == pending) ?
		len + header_len : video->req_size;

	/* Init the pending sgs with payload */
	sg = sg_next(sg);

	for_each_sg(sg, iter, ureq->sgt.nents - 1, i) {
		if (!len || !buf->sg || !buf->sg->length)
			break;

		sg_left = buf->sg->length - buf->offset;
		part = min_t(unsigned int, len, sg_left);

		sg_set_page(iter, sg_page(buf->sg), part, buf->offset);

		if (part == sg_left) {
			buf->offset = 0;
			buf->sg = sg_next(buf->sg);
		} else {
			buf->offset += part;
		}
		len -= part;
	}

	/* Assign the video data with header. */
	req->buf = NULL;
	req->sg	= ureq->sgt.sgl;
	req->num_sgs = i + 1;

	req->length -= len;
	video->queue.buf_used += req->length - header_len;

	if (buf->bytesused == video->queue.buf_used || !buf->sg ||
			video->queue.flags & UVC_QUEUE_DROP_INCOMPLETE) {
		video->queue.buf_used = 0;
		buf->state = UVC_BUF_STATE_DONE;
		buf->offset = 0;
		list_del(&buf->queue);
		video->fid ^= UVC_STREAM_FID;
		ureq->last_buf = buf;
	}
}

static void
uvc_video_encode_isoc(struct usb_request *req, struct uvc_video *video,
		struct uvc_buffer *buf)
{
	void *mem = req->buf;
	struct uvc_request *ureq = req->context;
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

	if (buf->bytesused == video->queue.buf_used ||
			video->queue.flags & UVC_QUEUE_DROP_INCOMPLETE) {
		video->queue.buf_used = 0;
		buf->state = UVC_BUF_STATE_DONE;
		list_del(&buf->queue);
		video->fid ^= UVC_STREAM_FID;
		ureq->last_buf = buf;
	}
}

/* --------------------------------------------------------------------------
 * Request handling
 */

/*
 * Callers must take care to hold req_lock when this function may be called
 * from multiple threads. For example, when frames are streaming to the host.
 */
static void
uvc_video_free_request(struct uvc_request *ureq, struct usb_ep *ep)
{
	sg_free_table(&ureq->sgt);
	if (ureq->req && ep) {
		usb_ep_free_request(ep, ureq->req);
		ureq->req = NULL;
	}

	kfree(ureq->req_buffer);
	ureq->req_buffer = NULL;

	if (!list_empty(&ureq->list))
		list_del_init(&ureq->list);

	kfree(ureq);
}

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
	struct uvc_buffer *last_buf;
	unsigned long flags;

	spin_lock_irqsave(&video->req_lock, flags);
	if (!video->is_enabled) {
		/*
		 * When is_enabled is false, uvcg_video_disable() ensures
		 * that in-flight uvc_buffers are returned, so we can
		 * safely call free_request without worrying about
		 * last_buf.
		 */
		uvc_video_free_request(ureq, ep);
		spin_unlock_irqrestore(&video->req_lock, flags);
		return;
	}

	last_buf = ureq->last_buf;
	ureq->last_buf = NULL;
	spin_unlock_irqrestore(&video->req_lock, flags);

	switch (req->status) {
	case 0:
		break;

	case -EXDEV:
		uvcg_dbg(&video->uvc->func, "VS request missed xfer.\n");
		queue->flags |= UVC_QUEUE_DROP_INCOMPLETE;
		break;

	case -ESHUTDOWN:	/* disconnect from host. */
		uvcg_dbg(&video->uvc->func, "VS request cancelled.\n");
		uvcg_queue_cancel(queue, 1);
		break;

	default:
		uvcg_warn(&video->uvc->func,
			  "VS request completed with status %d.\n",
			  req->status);
		uvcg_queue_cancel(queue, 0);
	}

	if (last_buf) {
		spin_lock_irqsave(&queue->irqlock, flags);
		uvcg_complete_buffer(queue, last_buf);
		spin_unlock_irqrestore(&queue->irqlock, flags);
	}

	spin_lock_irqsave(&video->req_lock, flags);
	/*
	 * Video stream might have been disabled while we were
	 * processing the current usb_request. So make sure
	 * we're still streaming before queueing the usb_request
	 * back to req_free
	 */
	if (video->is_enabled) {
		list_add_tail(&req->list, &video->req_free);
		queue_work(video->async_wq, &video->pump);
	} else {
		uvc_video_free_request(ureq, ep);
	}
	spin_unlock_irqrestore(&video->req_lock, flags);
}

static int
uvc_video_free_requests(struct uvc_video *video)
{
	struct uvc_request *ureq, *temp;

	list_for_each_entry_safe(ureq, temp, &video->ureqs, list)
		uvc_video_free_request(ureq, video->ep);

	INIT_LIST_HEAD(&video->ureqs);
	INIT_LIST_HEAD(&video->req_free);
	video->req_size = 0;
	return 0;
}

static int
uvc_video_alloc_requests(struct uvc_video *video)
{
	struct uvc_request *ureq;
	unsigned int req_size;
	unsigned int i;
	int ret = -ENOMEM;

	BUG_ON(video->req_size);

	req_size = video->ep->maxpacket
		 * max_t(unsigned int, video->ep->maxburst, 1)
		 * (video->ep->mult);

	for (i = 0; i < video->uvc_num_requests; i++) {
		ureq = kzalloc(sizeof(struct uvc_request), GFP_KERNEL);
		if (ureq == NULL)
			goto error;

		INIT_LIST_HEAD(&ureq->list);

		list_add_tail(&ureq->list, &video->ureqs);

		ureq->req_buffer = kmalloc(req_size, GFP_KERNEL);
		if (ureq->req_buffer == NULL)
			goto error;

		ureq->req = usb_ep_alloc_request(video->ep, GFP_KERNEL);
		if (ureq->req == NULL)
			goto error;

		ureq->req->buf = ureq->req_buffer;
		ureq->req->length = 0;
		ureq->req->complete = uvc_video_complete;
		ureq->req->context = ureq;
		ureq->video = video;
		ureq->last_buf = NULL;

		list_add_tail(&ureq->req->list, &video->req_free);
		/* req_size/PAGE_SIZE + 1 for overruns and + 1 for header */
		sg_alloc_table(&ureq->sgt,
			       DIV_ROUND_UP(req_size - UVCG_REQUEST_HEADER_LEN,
					    PAGE_SIZE) + 2, GFP_KERNEL);
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
	/* video->max_payload_size is only set when using bulk transfer */
	bool is_bulk = video->max_payload_size;
	struct usb_request *req = NULL;
	struct uvc_buffer *buf;
	unsigned long flags;
	bool buf_done;
	int ret;

	while (true) {
		if (!video->ep->enabled)
			return;

		/*
		 * Check is_enabled and retrieve the first available USB
		 * request, protected by the request lock.
		 */
		spin_lock_irqsave(&video->req_lock, flags);
		if (!video->is_enabled || list_empty(&video->req_free)) {
			spin_unlock_irqrestore(&video->req_lock, flags);
			return;
		}
		req = list_first_entry(&video->req_free, struct usb_request,
					list);
		list_del(&req->list);
		spin_unlock_irqrestore(&video->req_lock, flags);

		/*
		 * Retrieve the first available video buffer and fill the
		 * request, protected by the video queue irqlock.
		 */
		spin_lock_irqsave(&queue->irqlock, flags);
		buf = uvcg_queue_head(queue);

		if (buf != NULL) {
			video->encode(req, video, buf);
			buf_done = buf->state == UVC_BUF_STATE_DONE;
		} else if (!(queue->flags & UVC_QUEUE_DISCONNECTED) && !is_bulk) {
			/*
			 * No video buffer available; the queue is still connected and
			 * we're transferring over ISOC. Queue a 0 length request to
			 * prevent missed ISOC transfers.
			 */
			req->length = 0;
			buf_done = false;
		} else {
			/*
			 * Either the queue has been disconnected or no video buffer
			 * available for bulk transfer. Either way, stop processing
			 * further.
			 */
			spin_unlock_irqrestore(&queue->irqlock, flags);
			break;
		}

		/*
		 * With USB3 handling more requests at a higher speed, we can't
		 * afford to generate an interrupt for every request. Decide to
		 * interrupt:
		 *
		 * - When no more requests are available in the free queue, as
		 *   this may be our last chance to refill the endpoint's
		 *   request queue.
		 *
		 * - When this is request is the last request for the video
		 *   buffer, as we want to start sending the next video buffer
		 *   ASAP in case it doesn't get started already in the next
		 *   iteration of this loop.
		 *
		 * - Four times over the length of the requests queue (as
		 *   indicated by video->uvc_num_requests), as a trade-off
		 *   between latency and interrupt load.
		 */
		if (list_empty(&video->req_free) || buf_done ||
		    !(video->req_int_count %
		       DIV_ROUND_UP(video->uvc_num_requests, 4))) {
			video->req_int_count = 0;
			req->no_interrupt = 0;
		} else {
			req->no_interrupt = 1;
		}

		/* Queue the USB request */
		ret = uvcg_video_ep_queue(video, req);
		spin_unlock_irqrestore(&queue->irqlock, flags);

		if (ret < 0) {
			uvcg_queue_cancel(queue, 0);
			break;
		}

		/* Endpoint now owns the request */
		req = NULL;
		video->req_int_count++;
	}

	if (!req)
		return;

	spin_lock_irqsave(&video->req_lock, flags);
	if (video->is_enabled)
		list_add_tail(&req->list, &video->req_free);
	else
		uvc_video_free_request(req->context, video->ep);
	spin_unlock_irqrestore(&video->req_lock, flags);
}

/*
 * Disable the video stream
 */
int
uvcg_video_disable(struct uvc_video *video)
{
	unsigned long flags;
	struct list_head inflight_bufs;
	struct usb_request *req, *temp;
	struct uvc_buffer *buf, *btemp;
	struct uvc_request *ureq, *utemp;

	if (video->ep == NULL) {
		uvcg_info(&video->uvc->func,
			  "Video disable failed, device is uninitialized.\n");
		return -ENODEV;
	}

	INIT_LIST_HEAD(&inflight_bufs);
	spin_lock_irqsave(&video->req_lock, flags);
	video->is_enabled = false;

	/*
	 * Remove any in-flight buffers from the uvc_requests
	 * because we want to return them before cancelling the
	 * queue. This ensures that we aren't stuck waiting for
	 * all complete callbacks to come through before disabling
	 * vb2 queue.
	 */
	list_for_each_entry(ureq, &video->ureqs, list) {
		if (ureq->last_buf) {
			list_add_tail(&ureq->last_buf->queue, &inflight_bufs);
			ureq->last_buf = NULL;
		}
	}
	spin_unlock_irqrestore(&video->req_lock, flags);

	cancel_work_sync(&video->pump);
	uvcg_queue_cancel(&video->queue, 0);

	spin_lock_irqsave(&video->req_lock, flags);
	/*
	 * Remove all uvc_requests from ureqs with list_del_init
	 * This lets uvc_video_free_request correctly identify
	 * if the uvc_request is attached to a list or not when freeing
	 * memory.
	 */
	list_for_each_entry_safe(ureq, utemp, &video->ureqs, list)
		list_del_init(&ureq->list);

	list_for_each_entry_safe(req, temp, &video->req_free, list) {
		list_del(&req->list);
		uvc_video_free_request(req->context, video->ep);
	}

	INIT_LIST_HEAD(&video->ureqs);
	INIT_LIST_HEAD(&video->req_free);
	video->req_size = 0;
	spin_unlock_irqrestore(&video->req_lock, flags);

	/*
	 * Return all the video buffers before disabling the queue.
	 */
	spin_lock_irqsave(&video->queue.irqlock, flags);
	list_for_each_entry_safe(buf, btemp, &inflight_bufs, queue) {
		list_del(&buf->queue);
		uvcg_complete_buffer(&video->queue, buf);
	}
	spin_unlock_irqrestore(&video->queue.irqlock, flags);

	uvcg_queue_enable(&video->queue, 0);
	return 0;
}

/*
 * Enable the video stream.
 */
int uvcg_video_enable(struct uvc_video *video)
{
	int ret;

	if (video->ep == NULL) {
		uvcg_info(&video->uvc->func,
			  "Video enable failed, device is uninitialized.\n");
		return -ENODEV;
	}

	/*
	 * Safe to access request related fields without req_lock because
	 * this is the only thread currently active, and no other
	 * request handling thread will become active until this function
	 * returns.
	 */
	video->is_enabled = true;

	if ((ret = uvcg_queue_enable(&video->queue, 1)) < 0)
		return ret;

	if ((ret = uvc_video_alloc_requests(video)) < 0)
		return ret;

	if (video->max_payload_size) {
		video->encode = uvc_video_encode_bulk;
		video->payload_size = 0;
	} else
		video->encode = video->queue.use_sg ?
			uvc_video_encode_isoc_sg : uvc_video_encode_isoc;

	video->req_int_count = 0;

	queue_work(video->async_wq, &video->pump);

	return ret;
}

/*
 * Initialize the UVC video stream.
 */
int uvcg_video_init(struct uvc_video *video, struct uvc_device *uvc)
{
	video->is_enabled = false;
	INIT_LIST_HEAD(&video->ureqs);
	INIT_LIST_HEAD(&video->req_free);
	spin_lock_init(&video->req_lock);
	INIT_WORK(&video->pump, uvcg_video_pump);

	/* Allocate a work queue for asynchronous video pump handler. */
	video->async_wq = alloc_workqueue("uvcgadget", WQ_UNBOUND | WQ_HIGHPRI, 0);
	if (!video->async_wq)
		return -EINVAL;

	video->uvc = uvc;
	video->fcc = V4L2_PIX_FMT_YUYV;
	video->bpp = 16;
	video->width = 320;
	video->height = 240;
	video->imagesize = 320 * 240 * 2;

	/* Initialize the video buffers queue. */
	uvcg_queue_init(&video->queue, uvc->v4l2_dev.dev->parent,
			V4L2_BUF_TYPE_VIDEO_OUTPUT, &video->mutex);
	return 0;
}
