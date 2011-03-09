/*
 * Memory-to-memory device framework for Video for Linux 2 and videobuf.
 *
 * Helper functions for devices that use videobuf buffers for both their
 * source and destination.
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <p.osciak@samsung.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <media/videobuf-core.h>
#include <media/v4l2-mem2mem.h>

MODULE_DESCRIPTION("Mem to mem device framework for videobuf");
MODULE_AUTHOR("Pawel Osciak, <p.osciak@samsung.com>");
MODULE_LICENSE("GPL");

static bool debug;
module_param(debug, bool, 0644);

#define dprintk(fmt, arg...)						\
	do {								\
		if (debug)						\
			printk(KERN_DEBUG "%s: " fmt, __func__, ## arg);\
	} while (0)


/* Instance is already queued on the job_queue */
#define TRANS_QUEUED		(1 << 0)
/* Instance is currently running in hardware */
#define TRANS_RUNNING		(1 << 1)


/* Offset base for buffers on the destination queue - used to distinguish
 * between source and destination buffers when mmapping - they receive the same
 * offsets but for different queues */
#define DST_QUEUE_OFF_BASE	(1 << 30)


/**
 * struct v4l2_m2m_dev - per-device context
 * @curr_ctx:		currently running instance
 * @job_queue:		instances queued to run
 * @job_spinlock:	protects job_queue
 * @m2m_ops:		driver callbacks
 */
struct v4l2_m2m_dev {
	struct v4l2_m2m_ctx	*curr_ctx;

	struct list_head	job_queue;
	spinlock_t		job_spinlock;

	struct v4l2_m2m_ops	*m2m_ops;
};

static struct v4l2_m2m_queue_ctx *get_queue_ctx(struct v4l2_m2m_ctx *m2m_ctx,
						enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &m2m_ctx->cap_q_ctx;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return &m2m_ctx->out_q_ctx;
	default:
		printk(KERN_ERR "Invalid buffer type\n");
		return NULL;
	}
}

/**
 * v4l2_m2m_get_vq() - return videobuf_queue for the given type
 */
struct videobuf_queue *v4l2_m2m_get_vq(struct v4l2_m2m_ctx *m2m_ctx,
				       enum v4l2_buf_type type)
{
	struct v4l2_m2m_queue_ctx *q_ctx;

	q_ctx = get_queue_ctx(m2m_ctx, type);
	if (!q_ctx)
		return NULL;

	return &q_ctx->q;
}
EXPORT_SYMBOL(v4l2_m2m_get_vq);

/**
 * v4l2_m2m_next_buf() - return next buffer from the list of ready buffers
 */
void *v4l2_m2m_next_buf(struct v4l2_m2m_ctx *m2m_ctx, enum v4l2_buf_type type)
{
	struct v4l2_m2m_queue_ctx *q_ctx;
	struct videobuf_buffer *vb = NULL;
	unsigned long flags;

	q_ctx = get_queue_ctx(m2m_ctx, type);
	if (!q_ctx)
		return NULL;

	spin_lock_irqsave(q_ctx->q.irqlock, flags);

	if (list_empty(&q_ctx->rdy_queue))
		goto end;

	vb = list_entry(q_ctx->rdy_queue.next, struct videobuf_buffer, queue);
	vb->state = VIDEOBUF_ACTIVE;

end:
	spin_unlock_irqrestore(q_ctx->q.irqlock, flags);
	return vb;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_next_buf);

/**
 * v4l2_m2m_buf_remove() - take off a buffer from the list of ready buffers and
 * return it
 */
void *v4l2_m2m_buf_remove(struct v4l2_m2m_ctx *m2m_ctx, enum v4l2_buf_type type)
{
	struct v4l2_m2m_queue_ctx *q_ctx;
	struct videobuf_buffer *vb = NULL;
	unsigned long flags;

	q_ctx = get_queue_ctx(m2m_ctx, type);
	if (!q_ctx)
		return NULL;

	spin_lock_irqsave(q_ctx->q.irqlock, flags);
	if (!list_empty(&q_ctx->rdy_queue)) {
		vb = list_entry(q_ctx->rdy_queue.next, struct videobuf_buffer,
				queue);
		list_del(&vb->queue);
		q_ctx->num_rdy--;
	}
	spin_unlock_irqrestore(q_ctx->q.irqlock, flags);

	return vb;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_buf_remove);

/*
 * Scheduling handlers
 */

/**
 * v4l2_m2m_get_curr_priv() - return driver private data for the currently
 * running instance or NULL if no instance is running
 */
void *v4l2_m2m_get_curr_priv(struct v4l2_m2m_dev *m2m_dev)
{
	unsigned long flags;
	void *ret = NULL;

	spin_lock_irqsave(&m2m_dev->job_spinlock, flags);
	if (m2m_dev->curr_ctx)
		ret = m2m_dev->curr_ctx->priv;
	spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);

	return ret;
}
EXPORT_SYMBOL(v4l2_m2m_get_curr_priv);

/**
 * v4l2_m2m_try_run() - select next job to perform and run it if possible
 *
 * Get next transaction (if present) from the waiting jobs list and run it.
 */
static void v4l2_m2m_try_run(struct v4l2_m2m_dev *m2m_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&m2m_dev->job_spinlock, flags);
	if (NULL != m2m_dev->curr_ctx) {
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);
		dprintk("Another instance is running, won't run now\n");
		return;
	}

	if (list_empty(&m2m_dev->job_queue)) {
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);
		dprintk("No job pending\n");
		return;
	}

	m2m_dev->curr_ctx = list_entry(m2m_dev->job_queue.next,
				   struct v4l2_m2m_ctx, queue);
	m2m_dev->curr_ctx->job_flags |= TRANS_RUNNING;
	spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);

	m2m_dev->m2m_ops->device_run(m2m_dev->curr_ctx->priv);
}

/**
 * v4l2_m2m_try_schedule() - check whether an instance is ready to be added to
 * the pending job queue and add it if so.
 * @m2m_ctx:	m2m context assigned to the instance to be checked
 *
 * There are three basic requirements an instance has to meet to be able to run:
 * 1) at least one source buffer has to be queued,
 * 2) at least one destination buffer has to be queued,
 * 3) streaming has to be on.
 *
 * There may also be additional, custom requirements. In such case the driver
 * should supply a custom callback (job_ready in v4l2_m2m_ops) that should
 * return 1 if the instance is ready.
 * An example of the above could be an instance that requires more than one
 * src/dst buffer per transaction.
 */
static void v4l2_m2m_try_schedule(struct v4l2_m2m_ctx *m2m_ctx)
{
	struct v4l2_m2m_dev *m2m_dev;
	unsigned long flags_job, flags;

	m2m_dev = m2m_ctx->m2m_dev;
	dprintk("Trying to schedule a job for m2m_ctx: %p\n", m2m_ctx);

	if (!m2m_ctx->out_q_ctx.q.streaming
	    || !m2m_ctx->cap_q_ctx.q.streaming) {
		dprintk("Streaming needs to be on for both queues\n");
		return;
	}

	spin_lock_irqsave(&m2m_dev->job_spinlock, flags_job);
	if (m2m_ctx->job_flags & TRANS_QUEUED) {
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags_job);
		dprintk("On job queue already\n");
		return;
	}

	spin_lock_irqsave(m2m_ctx->out_q_ctx.q.irqlock, flags);
	if (list_empty(&m2m_ctx->out_q_ctx.rdy_queue)) {
		spin_unlock_irqrestore(m2m_ctx->out_q_ctx.q.irqlock, flags);
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags_job);
		dprintk("No input buffers available\n");
		return;
	}
	if (list_empty(&m2m_ctx->cap_q_ctx.rdy_queue)) {
		spin_unlock_irqrestore(m2m_ctx->out_q_ctx.q.irqlock, flags);
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags_job);
		dprintk("No output buffers available\n");
		return;
	}
	spin_unlock_irqrestore(m2m_ctx->out_q_ctx.q.irqlock, flags);

	if (m2m_dev->m2m_ops->job_ready
		&& (!m2m_dev->m2m_ops->job_ready(m2m_ctx->priv))) {
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags_job);
		dprintk("Driver not ready\n");
		return;
	}

	list_add_tail(&m2m_ctx->queue, &m2m_dev->job_queue);
	m2m_ctx->job_flags |= TRANS_QUEUED;

	spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags_job);

	v4l2_m2m_try_run(m2m_dev);
}

/**
 * v4l2_m2m_job_finish() - inform the framework that a job has been finished
 * and have it clean up
 *
 * Called by a driver to yield back the device after it has finished with it.
 * Should be called as soon as possible after reaching a state which allows
 * other instances to take control of the device.
 *
 * This function has to be called only after device_run() callback has been
 * called on the driver. To prevent recursion, it should not be called directly
 * from the device_run() callback though.
 */
void v4l2_m2m_job_finish(struct v4l2_m2m_dev *m2m_dev,
			 struct v4l2_m2m_ctx *m2m_ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&m2m_dev->job_spinlock, flags);
	if (!m2m_dev->curr_ctx || m2m_dev->curr_ctx != m2m_ctx) {
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);
		dprintk("Called by an instance not currently running\n");
		return;
	}

	list_del(&m2m_dev->curr_ctx->queue);
	m2m_dev->curr_ctx->job_flags &= ~(TRANS_QUEUED | TRANS_RUNNING);
	m2m_dev->curr_ctx = NULL;

	spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);

	/* This instance might have more buffers ready, but since we do not
	 * allow more than one job on the job_queue per instance, each has
	 * to be scheduled separately after the previous one finishes. */
	v4l2_m2m_try_schedule(m2m_ctx);
	v4l2_m2m_try_run(m2m_dev);
}
EXPORT_SYMBOL(v4l2_m2m_job_finish);

/**
 * v4l2_m2m_reqbufs() - multi-queue-aware REQBUFS multiplexer
 */
int v4l2_m2m_reqbufs(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		     struct v4l2_requestbuffers *reqbufs)
{
	struct videobuf_queue *vq;

	vq = v4l2_m2m_get_vq(m2m_ctx, reqbufs->type);
	return videobuf_reqbufs(vq, reqbufs);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_reqbufs);

/**
 * v4l2_m2m_querybuf() - multi-queue-aware QUERYBUF multiplexer
 *
 * See v4l2_m2m_mmap() documentation for details.
 */
int v4l2_m2m_querybuf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		      struct v4l2_buffer *buf)
{
	struct videobuf_queue *vq;
	int ret;

	vq = v4l2_m2m_get_vq(m2m_ctx, buf->type);
	ret = videobuf_querybuf(vq, buf);

	if (buf->memory == V4L2_MEMORY_MMAP
	    && vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		buf->m.offset += DST_QUEUE_OFF_BASE;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_querybuf);

/**
 * v4l2_m2m_qbuf() - enqueue a source or destination buffer, depending on
 * the type
 */
int v4l2_m2m_qbuf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		  struct v4l2_buffer *buf)
{
	struct videobuf_queue *vq;
	int ret;

	vq = v4l2_m2m_get_vq(m2m_ctx, buf->type);
	ret = videobuf_qbuf(vq, buf);
	if (!ret)
		v4l2_m2m_try_schedule(m2m_ctx);

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_qbuf);

/**
 * v4l2_m2m_dqbuf() - dequeue a source or destination buffer, depending on
 * the type
 */
int v4l2_m2m_dqbuf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		   struct v4l2_buffer *buf)
{
	struct videobuf_queue *vq;

	vq = v4l2_m2m_get_vq(m2m_ctx, buf->type);
	return videobuf_dqbuf(vq, buf, file->f_flags & O_NONBLOCK);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_dqbuf);

/**
 * v4l2_m2m_streamon() - turn on streaming for a video queue
 */
int v4l2_m2m_streamon(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		      enum v4l2_buf_type type)
{
	struct videobuf_queue *vq;
	int ret;

	vq = v4l2_m2m_get_vq(m2m_ctx, type);
	ret = videobuf_streamon(vq);
	if (!ret)
		v4l2_m2m_try_schedule(m2m_ctx);

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_streamon);

/**
 * v4l2_m2m_streamoff() - turn off streaming for a video queue
 */
int v4l2_m2m_streamoff(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		       enum v4l2_buf_type type)
{
	struct videobuf_queue *vq;

	vq = v4l2_m2m_get_vq(m2m_ctx, type);
	return videobuf_streamoff(vq);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_streamoff);

/**
 * v4l2_m2m_poll() - poll replacement, for destination buffers only
 *
 * Call from the driver's poll() function. Will poll both queues. If a buffer
 * is available to dequeue (with dqbuf) from the source queue, this will
 * indicate that a non-blocking write can be performed, while read will be
 * returned in case of the destination queue.
 */
unsigned int v4l2_m2m_poll(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
			   struct poll_table_struct *wait)
{
	struct videobuf_queue *src_q, *dst_q;
	struct videobuf_buffer *src_vb = NULL, *dst_vb = NULL;
	unsigned int rc = 0;

	src_q = v4l2_m2m_get_src_vq(m2m_ctx);
	dst_q = v4l2_m2m_get_dst_vq(m2m_ctx);

	videobuf_queue_lock(src_q);
	videobuf_queue_lock(dst_q);

	if (src_q->streaming && !list_empty(&src_q->stream))
		src_vb = list_first_entry(&src_q->stream,
					  struct videobuf_buffer, stream);
	if (dst_q->streaming && !list_empty(&dst_q->stream))
		dst_vb = list_first_entry(&dst_q->stream,
					  struct videobuf_buffer, stream);

	if (!src_vb && !dst_vb) {
		rc = POLLERR;
		goto end;
	}

	if (src_vb) {
		poll_wait(file, &src_vb->done, wait);
		if (src_vb->state == VIDEOBUF_DONE
		    || src_vb->state == VIDEOBUF_ERROR)
			rc |= POLLOUT | POLLWRNORM;
	}
	if (dst_vb) {
		poll_wait(file, &dst_vb->done, wait);
		if (dst_vb->state == VIDEOBUF_DONE
		    || dst_vb->state == VIDEOBUF_ERROR)
			rc |= POLLIN | POLLRDNORM;
	}

end:
	videobuf_queue_unlock(dst_q);
	videobuf_queue_unlock(src_q);
	return rc;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_poll);

/**
 * v4l2_m2m_mmap() - source and destination queues-aware mmap multiplexer
 *
 * Call from driver's mmap() function. Will handle mmap() for both queues
 * seamlessly for videobuffer, which will receive normal per-queue offsets and
 * proper videobuf queue pointers. The differentiation is made outside videobuf
 * by adding a predefined offset to buffers from one of the queues and
 * subtracting it before passing it back to videobuf. Only drivers (and
 * thus applications) receive modified offsets.
 */
int v4l2_m2m_mmap(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
			 struct vm_area_struct *vma)
{
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	struct videobuf_queue *vq;

	if (offset < DST_QUEUE_OFF_BASE) {
		vq = v4l2_m2m_get_src_vq(m2m_ctx);
	} else {
		vq = v4l2_m2m_get_dst_vq(m2m_ctx);
		vma->vm_pgoff -= (DST_QUEUE_OFF_BASE >> PAGE_SHIFT);
	}

	return videobuf_mmap_mapper(vq, vma);
}
EXPORT_SYMBOL(v4l2_m2m_mmap);

/**
 * v4l2_m2m_init() - initialize per-driver m2m data
 *
 * Usually called from driver's probe() function.
 */
struct v4l2_m2m_dev *v4l2_m2m_init(struct v4l2_m2m_ops *m2m_ops)
{
	struct v4l2_m2m_dev *m2m_dev;

	if (!m2m_ops)
		return ERR_PTR(-EINVAL);

	BUG_ON(!m2m_ops->device_run);
	BUG_ON(!m2m_ops->job_abort);

	m2m_dev = kzalloc(sizeof *m2m_dev, GFP_KERNEL);
	if (!m2m_dev)
		return ERR_PTR(-ENOMEM);

	m2m_dev->curr_ctx = NULL;
	m2m_dev->m2m_ops = m2m_ops;
	INIT_LIST_HEAD(&m2m_dev->job_queue);
	spin_lock_init(&m2m_dev->job_spinlock);

	return m2m_dev;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_init);

/**
 * v4l2_m2m_release() - cleans up and frees a m2m_dev structure
 *
 * Usually called from driver's remove() function.
 */
void v4l2_m2m_release(struct v4l2_m2m_dev *m2m_dev)
{
	kfree(m2m_dev);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_release);

/**
 * v4l2_m2m_ctx_init() - allocate and initialize a m2m context
 * @priv - driver's instance private data
 * @m2m_dev - a previously initialized m2m_dev struct
 * @vq_init - a callback for queue type-specific initialization function to be
 * used for initializing videobuf_queues
 *
 * Usually called from driver's open() function.
 */
struct v4l2_m2m_ctx *v4l2_m2m_ctx_init(void *priv, struct v4l2_m2m_dev *m2m_dev,
			void (*vq_init)(void *priv, struct videobuf_queue *,
					enum v4l2_buf_type))
{
	struct v4l2_m2m_ctx *m2m_ctx;
	struct v4l2_m2m_queue_ctx *out_q_ctx, *cap_q_ctx;

	if (!vq_init)
		return ERR_PTR(-EINVAL);

	m2m_ctx = kzalloc(sizeof *m2m_ctx, GFP_KERNEL);
	if (!m2m_ctx)
		return ERR_PTR(-ENOMEM);

	m2m_ctx->priv = priv;
	m2m_ctx->m2m_dev = m2m_dev;

	out_q_ctx = get_queue_ctx(m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	cap_q_ctx = get_queue_ctx(m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);

	INIT_LIST_HEAD(&out_q_ctx->rdy_queue);
	INIT_LIST_HEAD(&cap_q_ctx->rdy_queue);

	INIT_LIST_HEAD(&m2m_ctx->queue);

	vq_init(priv, &out_q_ctx->q, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	vq_init(priv, &cap_q_ctx->q, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	out_q_ctx->q.priv_data = cap_q_ctx->q.priv_data = priv;

	return m2m_ctx;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_ctx_init);

/**
 * v4l2_m2m_ctx_release() - release m2m context
 *
 * Usually called from driver's release() function.
 */
void v4l2_m2m_ctx_release(struct v4l2_m2m_ctx *m2m_ctx)
{
	struct v4l2_m2m_dev *m2m_dev;
	struct videobuf_buffer *vb;
	unsigned long flags;

	m2m_dev = m2m_ctx->m2m_dev;

	spin_lock_irqsave(&m2m_dev->job_spinlock, flags);
	if (m2m_ctx->job_flags & TRANS_RUNNING) {
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);
		m2m_dev->m2m_ops->job_abort(m2m_ctx->priv);
		dprintk("m2m_ctx %p running, will wait to complete", m2m_ctx);
		vb = v4l2_m2m_next_dst_buf(m2m_ctx);
		BUG_ON(NULL == vb);
		wait_event(vb->done, vb->state != VIDEOBUF_ACTIVE
				     && vb->state != VIDEOBUF_QUEUED);
	} else if (m2m_ctx->job_flags & TRANS_QUEUED) {
		list_del(&m2m_ctx->queue);
		m2m_ctx->job_flags &= ~(TRANS_QUEUED | TRANS_RUNNING);
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);
		dprintk("m2m_ctx: %p had been on queue and was removed\n",
			m2m_ctx);
	} else {
		/* Do nothing, was not on queue/running */
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);
	}

	videobuf_stop(&m2m_ctx->cap_q_ctx.q);
	videobuf_stop(&m2m_ctx->out_q_ctx.q);

	videobuf_mmap_free(&m2m_ctx->cap_q_ctx.q);
	videobuf_mmap_free(&m2m_ctx->out_q_ctx.q);

	kfree(m2m_ctx);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_ctx_release);

/**
 * v4l2_m2m_buf_queue() - add a buffer to the proper ready buffers list.
 *
 * Call from buf_queue(), videobuf_queue_ops callback.
 *
 * Locking: Caller holds q->irqlock (taken by videobuf before calling buf_queue
 * callback in the driver).
 */
void v4l2_m2m_buf_queue(struct v4l2_m2m_ctx *m2m_ctx, struct videobuf_queue *vq,
			struct videobuf_buffer *vb)
{
	struct v4l2_m2m_queue_ctx *q_ctx;

	q_ctx = get_queue_ctx(m2m_ctx, vq->type);
	if (!q_ctx)
		return;

	list_add_tail(&vb->queue, &q_ctx->rdy_queue);
	q_ctx->num_rdy++;

	vb->state = VIDEOBUF_QUEUED;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_buf_queue);

