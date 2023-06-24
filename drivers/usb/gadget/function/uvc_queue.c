// SPDX-License-Identifier: GPL-2.0+
/*
 *	uvc_queue.c  --  USB Video Class driver - Buffers management
 *
 *	Copyright (C) 2005-2010
 *	    Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/videodev2.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>

#include <media/v4l2-common.h>
#include <media/videobuf2-dma-sg.h>
#include <media/videobuf2-vmalloc.h>

#include "uvc.h"

/* ------------------------------------------------------------------------
 * Video buffers queue management.
 *
 * Video queues is initialized by uvcg_queue_init(). The function performs
 * basic initialization of the uvc_video_queue struct and never fails.
 *
 * Video buffers are managed by videobuf2. The driver uses a mutex to protect
 * the videobuf2 queue operations by serializing calls to videobuf2 and a
 * spinlock to protect the IRQ queue that holds the buffers to be processed by
 * the driver.
 */

/* -----------------------------------------------------------------------------
 * videobuf2 queue operations
 */

static int uvc_queue_setup(struct vb2_queue *vq,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], struct device *alloc_devs[])
{
	struct uvc_video_queue *queue = vb2_get_drv_priv(vq);
	struct uvc_video *video = container_of(queue, struct uvc_video, queue);
	unsigned int req_size;
	unsigned int nreq;

	if (*nbuffers > UVC_MAX_VIDEO_BUFFERS)
		*nbuffers = UVC_MAX_VIDEO_BUFFERS;

	*nplanes = 1;

	sizes[0] = video->imagesize;

	req_size = video->ep->maxpacket
		 * max_t(unsigned int, video->ep->maxburst, 1)
		 * (video->ep->mult);

	/* We divide by two, to increase the chance to run
	 * into fewer requests for smaller framesizes.
	 */
	nreq = DIV_ROUND_UP(DIV_ROUND_UP(sizes[0], 2), req_size);
	nreq = clamp(nreq, 4U, 64U);
	video->uvc_num_requests = nreq;

	return 0;
}

static int uvc_buffer_prepare(struct vb2_buffer *vb)
{
	struct uvc_video_queue *queue = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct uvc_buffer *buf = container_of(vbuf, struct uvc_buffer, buf);

	if (vb->type == V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    vb2_get_plane_payload(vb, 0) > vb2_plane_size(vb, 0)) {
		uvc_trace(UVC_TRACE_CAPTURE, "[E] Bytes used out of bounds.\n");
		return -EINVAL;
	}

	if (unlikely(queue->flags & UVC_QUEUE_DISCONNECTED))
		return -ENODEV;

	buf->state = UVC_BUF_STATE_QUEUED;
	if (queue->use_sg) {
		buf->sgt = vb2_dma_sg_plane_desc(vb, 0);
		buf->sg = buf->sgt->sgl;
	} else {
		buf->mem = vb2_plane_vaddr(vb, 0);
	}
	buf->length = vb2_plane_size(vb, 0);
	if (vb->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		buf->bytesused = 0;
	else
		buf->bytesused = vb2_get_plane_payload(vb, 0);

	return 0;
}

static void uvc_buffer_queue(struct vb2_buffer *vb)
{
	struct uvc_video_queue *queue = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct uvc_buffer *buf = container_of(vbuf, struct uvc_buffer, buf);
	unsigned long flags;

	spin_lock_irqsave(&queue->irqlock, flags);

	if (likely(!(queue->flags & UVC_QUEUE_DISCONNECTED))) {
		list_add_tail(&buf->queue, &queue->irqqueue);
	} else {
		/*
		 * If the device is disconnected return the buffer to userspace
		 * directly. The next QBUF call will fail with -ENODEV.
		 */
		buf->state = UVC_BUF_STATE_ERROR;
		vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
	}

	spin_unlock_irqrestore(&queue->irqlock, flags);
}

static const struct vb2_ops uvc_queue_qops = {
	.queue_setup = uvc_queue_setup,
	.buf_prepare = uvc_buffer_prepare,
	.buf_queue = uvc_buffer_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

int uvcg_queue_init(struct uvc_video_queue *queue, struct device *dev, enum v4l2_buf_type type,
		    struct mutex *lock)
{
	struct uvc_video *video = container_of(queue, struct uvc_video, queue);
	struct usb_composite_dev *cdev = video->uvc->func.config->cdev;
	int ret;

	queue->queue.type = type;
	queue->queue.io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	queue->queue.drv_priv = queue;
	queue->queue.buf_struct_size = sizeof(struct uvc_buffer);
	queue->queue.ops = &uvc_queue_qops;
	queue->queue.lock = lock;
	if (cdev->gadget->sg_supported) {
		queue->queue.mem_ops = &vb2_dma_sg_memops;
		queue->use_sg = 1;
	} else {
		queue->queue.mem_ops = &vb2_vmalloc_memops;
	}

	queue->queue.timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY
				     | V4L2_BUF_FLAG_TSTAMP_SRC_EOF;
	queue->queue.dev = dev;

	ret = vb2_queue_init(&queue->queue);
	if (ret)
		return ret;

	spin_lock_init(&queue->irqlock);
	INIT_LIST_HEAD(&queue->irqqueue);
	queue->flags = 0;

	return 0;
}

/*
 * Free the video buffers.
 */
void uvcg_free_buffers(struct uvc_video_queue *queue)
{
	vb2_queue_release(&queue->queue);
}

/*
 * Allocate the video buffers.
 */
int uvcg_alloc_buffers(struct uvc_video_queue *queue,
			      struct v4l2_requestbuffers *rb)
{
	int ret;

	ret = vb2_reqbufs(&queue->queue, rb);

	return ret ? ret : rb->count;
}

int uvcg_query_buffer(struct uvc_video_queue *queue, struct v4l2_buffer *buf)
{
	return vb2_querybuf(&queue->queue, buf);
}

int uvcg_queue_buffer(struct uvc_video_queue *queue, struct v4l2_buffer *buf)
{
	return vb2_qbuf(&queue->queue, NULL, buf);
}

/*
 * Dequeue a video buffer. If nonblocking is false, block until a buffer is
 * available.
 */
int uvcg_dequeue_buffer(struct uvc_video_queue *queue, struct v4l2_buffer *buf,
			int nonblocking)
{
	return vb2_dqbuf(&queue->queue, buf, nonblocking);
}

/*
 * Poll the video queue.
 *
 * This function implements video queue polling and is intended to be used by
 * the device poll handler.
 */
__poll_t uvcg_queue_poll(struct uvc_video_queue *queue, struct file *file,
			     poll_table *wait)
{
	return vb2_poll(&queue->queue, file, wait);
}

int uvcg_queue_mmap(struct uvc_video_queue *queue, struct vm_area_struct *vma)
{
	return vb2_mmap(&queue->queue, vma);
}

#ifndef CONFIG_MMU
/*
 * Get unmapped area.
 *
 * NO-MMU arch need this function to make mmap() work correctly.
 */
unsigned long uvcg_queue_get_unmapped_area(struct uvc_video_queue *queue,
					   unsigned long pgoff)
{
	return vb2_get_unmapped_area(&queue->queue, 0, 0, pgoff, 0);
}
#endif

/*
 * Cancel the video buffers queue.
 *
 * Cancelling the queue marks all buffers on the irq queue as erroneous,
 * wakes them up and removes them from the queue.
 *
 * If the disconnect parameter is set, further calls to uvc_queue_buffer will
 * fail with -ENODEV.
 *
 * This function acquires the irq spinlock and can be called from interrupt
 * context.
 */
void uvcg_queue_cancel(struct uvc_video_queue *queue, int disconnect)
{
	struct uvc_buffer *buf;
	unsigned long flags;

	spin_lock_irqsave(&queue->irqlock, flags);
	while (!list_empty(&queue->irqqueue)) {
		buf = list_first_entry(&queue->irqqueue, struct uvc_buffer,
				       queue);
		list_del(&buf->queue);
		buf->state = UVC_BUF_STATE_ERROR;
		vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	queue->buf_used = 0;

	/*
	 * This must be protected by the irqlock spinlock to avoid race
	 * conditions between uvc_queue_buffer and the disconnection event that
	 * could result in an interruptible wait in uvc_dequeue_buffer. Do not
	 * blindly replace this logic by checking for the UVC_DEV_DISCONNECTED
	 * state outside the queue code.
	 */
	if (disconnect)
		queue->flags |= UVC_QUEUE_DISCONNECTED;
	spin_unlock_irqrestore(&queue->irqlock, flags);
}

/*
 * Enable or disable the video buffers queue.
 *
 * The queue must be enabled before starting video acquisition and must be
 * disabled after stopping it. This ensures that the video buffers queue
 * state can be properly initialized before buffers are accessed from the
 * interrupt handler.
 *
 * Enabling the video queue initializes parameters (such as sequence number,
 * sync pattern, ...). If the queue is already enabled, return -EBUSY.
 *
 * Disabling the video queue cancels the queue and removes all buffers from
 * the main queue.
 *
 * This function can't be called from interrupt context. Use
 * uvcg_queue_cancel() instead.
 */
int uvcg_queue_enable(struct uvc_video_queue *queue, int enable)
{
	unsigned long flags;
	int ret = 0;

	if (enable) {
		ret = vb2_streamon(&queue->queue, queue->queue.type);
		if (ret < 0)
			return ret;

		queue->sequence = 0;
		queue->buf_used = 0;
		queue->flags &= ~UVC_QUEUE_DROP_INCOMPLETE;
	} else {
		ret = vb2_streamoff(&queue->queue, queue->queue.type);
		if (ret < 0)
			return ret;

		spin_lock_irqsave(&queue->irqlock, flags);
		INIT_LIST_HEAD(&queue->irqqueue);

		/*
		 * FIXME: We need to clear the DISCONNECTED flag to ensure that
		 * applications will be able to queue buffers for the next
		 * streaming run. However, clearing it here doesn't guarantee
		 * that the device will be reconnected in the meantime.
		 */
		queue->flags &= ~UVC_QUEUE_DISCONNECTED;
		spin_unlock_irqrestore(&queue->irqlock, flags);
	}

	return ret;
}

/* called with &queue_irqlock held.. */
void uvcg_complete_buffer(struct uvc_video_queue *queue,
					  struct uvc_buffer *buf)
{
	if (queue->flags & UVC_QUEUE_DROP_INCOMPLETE) {
		queue->flags &= ~UVC_QUEUE_DROP_INCOMPLETE;
		buf->state = UVC_BUF_STATE_ERROR;
		vb2_set_plane_payload(&buf->buf.vb2_buf, 0, 0);
		vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_ERROR);
		return;
	}

	buf->buf.field = V4L2_FIELD_NONE;
	buf->buf.sequence = queue->sequence++;
	buf->buf.vb2_buf.timestamp = ktime_get_ns();

	vb2_set_plane_payload(&buf->buf.vb2_buf, 0, buf->bytesused);
	vb2_buffer_done(&buf->buf.vb2_buf, VB2_BUF_STATE_DONE);
}

struct uvc_buffer *uvcg_queue_head(struct uvc_video_queue *queue)
{
	struct uvc_buffer *buf = NULL;

	if (!list_empty(&queue->irqqueue))
		buf = list_first_entry(&queue->irqqueue, struct uvc_buffer,
				       queue);

	return buf;
}

