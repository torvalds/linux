/*
 * generic helper functions for handling video4linux capture buffers
 *
 * (c) 2007 Mauro Carvalho Chehab, <mchehab@infradead.org>
 *
 * Highly based on video-buf written originally by:
 * (c) 2001,02 Gerd Knorr <kraxel@bytesex.org>
 * (c) 2006 Mauro Carvalho Chehab, <mchehab@infradead.org>
 * (c) 2006 Ted Walther and John Sokol
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <media/videobuf-core.h>

#define MAGIC_BUFFER 0x20070728
#define MAGIC_CHECK(is, should)						\
	do {								\
		if (unlikely((is) != (should))) {			\
			printk(KERN_ERR					\
				"magic mismatch: %x (expected %x)\n",	\
					is, should);			\
			BUG();						\
		}							\
	} while (0)

static int debug;
module_param(debug, int, 0644);

MODULE_DESCRIPTION("helper module to manage video4linux buffers");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@infradead.org>");
MODULE_LICENSE("GPL");

#define dprintk(level, fmt, arg...)					\
	do {								\
		if (debug >= level)					\
			printk(KERN_DEBUG "vbuf: " fmt, ## arg);	\
	} while (0)

/* --------------------------------------------------------------------- */

#define CALL(q, f, arg...)						\
	((q->int_ops->f) ? q->int_ops->f(arg) : 0)
#define CALLPTR(q, f, arg...)						\
	((q->int_ops->f) ? q->int_ops->f(arg) : NULL)

struct videobuf_buffer *videobuf_alloc_vb(struct videobuf_queue *q)
{
	struct videobuf_buffer *vb;

	BUG_ON(q->msize < sizeof(*vb));

	if (!q->int_ops || !q->int_ops->alloc_vb) {
		printk(KERN_ERR "No specific ops defined!\n");
		BUG();
	}

	vb = q->int_ops->alloc_vb(q->msize);
	if (NULL != vb) {
		init_waitqueue_head(&vb->done);
		vb->magic = MAGIC_BUFFER;
	}

	return vb;
}
EXPORT_SYMBOL_GPL(videobuf_alloc_vb);

static int state_neither_active_nor_queued(struct videobuf_queue *q,
					   struct videobuf_buffer *vb)
{
	unsigned long flags;
	bool rc;

	spin_lock_irqsave(q->irqlock, flags);
	rc = vb->state != VIDEOBUF_ACTIVE && vb->state != VIDEOBUF_QUEUED;
	spin_unlock_irqrestore(q->irqlock, flags);
	return rc;
};

int videobuf_waiton(struct videobuf_queue *q, struct videobuf_buffer *vb,
		int non_blocking, int intr)
{
	bool is_ext_locked;
	int ret = 0;

	MAGIC_CHECK(vb->magic, MAGIC_BUFFER);

	if (non_blocking) {
		if (state_neither_active_nor_queued(q, vb))
			return 0;
		return -EAGAIN;
	}

	is_ext_locked = q->ext_lock && mutex_is_locked(q->ext_lock);

	/* Release vdev lock to prevent this wait from blocking outside access to
	   the device. */
	if (is_ext_locked)
		mutex_unlock(q->ext_lock);
	if (intr)
		ret = wait_event_interruptible(vb->done,
					state_neither_active_nor_queued(q, vb));
	else
		wait_event(vb->done, state_neither_active_nor_queued(q, vb));
	/* Relock */
	if (is_ext_locked)
		mutex_lock(q->ext_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(videobuf_waiton);

int videobuf_iolock(struct videobuf_queue *q, struct videobuf_buffer *vb,
		    struct v4l2_framebuffer *fbuf)
{
	MAGIC_CHECK(vb->magic, MAGIC_BUFFER);
	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	return CALL(q, iolock, q, vb, fbuf);
}
EXPORT_SYMBOL_GPL(videobuf_iolock);

void *videobuf_queue_to_vaddr(struct videobuf_queue *q,
			      struct videobuf_buffer *buf)
{
	if (q->int_ops->vaddr)
		return q->int_ops->vaddr(buf);
	return NULL;
}
EXPORT_SYMBOL_GPL(videobuf_queue_to_vaddr);

/* --------------------------------------------------------------------- */


void videobuf_queue_core_init(struct videobuf_queue *q,
			 const struct videobuf_queue_ops *ops,
			 struct device *dev,
			 spinlock_t *irqlock,
			 enum v4l2_buf_type type,
			 enum v4l2_field field,
			 unsigned int msize,
			 void *priv,
			 struct videobuf_qtype_ops *int_ops,
			 struct mutex *ext_lock)
{
	BUG_ON(!q);
	memset(q, 0, sizeof(*q));
	q->irqlock   = irqlock;
	q->ext_lock  = ext_lock;
	q->dev       = dev;
	q->type      = type;
	q->field     = field;
	q->msize     = msize;
	q->ops       = ops;
	q->priv_data = priv;
	q->int_ops   = int_ops;

	/* All buffer operations are mandatory */
	BUG_ON(!q->ops->buf_setup);
	BUG_ON(!q->ops->buf_prepare);
	BUG_ON(!q->ops->buf_queue);
	BUG_ON(!q->ops->buf_release);

	/* Lock is mandatory for queue_cancel to work */
	BUG_ON(!irqlock);

	/* Having implementations for abstract methods are mandatory */
	BUG_ON(!q->int_ops);

	mutex_init(&q->vb_lock);
	init_waitqueue_head(&q->wait);
	INIT_LIST_HEAD(&q->stream);
}
EXPORT_SYMBOL_GPL(videobuf_queue_core_init);

/* Locking: Only usage in bttv unsafe find way to remove */
int videobuf_queue_is_busy(struct videobuf_queue *q)
{
	int i;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	if (q->streaming) {
		dprintk(1, "busy: streaming active\n");
		return 1;
	}
	if (q->reading) {
		dprintk(1, "busy: pending read #1\n");
		return 1;
	}
	if (q->read_buf) {
		dprintk(1, "busy: pending read #2\n");
		return 1;
	}
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		if (q->bufs[i]->map) {
			dprintk(1, "busy: buffer #%d mapped\n", i);
			return 1;
		}
		if (q->bufs[i]->state == VIDEOBUF_QUEUED) {
			dprintk(1, "busy: buffer #%d queued\n", i);
			return 1;
		}
		if (q->bufs[i]->state == VIDEOBUF_ACTIVE) {
			dprintk(1, "busy: buffer #%d avtive\n", i);
			return 1;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(videobuf_queue_is_busy);

/*
 * __videobuf_free() - free all the buffers and their control structures
 *
 * This function can only be called if streaming/reading is off, i.e. no buffers
 * are under control of the driver.
 */
/* Locking: Caller holds q->vb_lock */
static int __videobuf_free(struct videobuf_queue *q)
{
	int i;

	dprintk(1, "%s\n", __func__);
	if (!q)
		return 0;

	if (q->streaming || q->reading) {
		dprintk(1, "Cannot free buffers when streaming or reading\n");
		return -EBUSY;
	}

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	for (i = 0; i < VIDEO_MAX_FRAME; i++)
		if (q->bufs[i] && q->bufs[i]->map) {
			dprintk(1, "Cannot free mmapped buffers\n");
			return -EBUSY;
		}

	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		q->ops->buf_release(q, q->bufs[i]);
		kfree(q->bufs[i]);
		q->bufs[i] = NULL;
	}

	return 0;
}

/* Locking: Caller holds q->vb_lock */
void videobuf_queue_cancel(struct videobuf_queue *q)
{
	unsigned long flags = 0;
	int i;

	q->streaming = 0;
	q->reading  = 0;
	wake_up_interruptible_sync(&q->wait);

	/* remove queued buffers from list */
	spin_lock_irqsave(q->irqlock, flags);
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		if (q->bufs[i]->state == VIDEOBUF_QUEUED) {
			list_del(&q->bufs[i]->queue);
			q->bufs[i]->state = VIDEOBUF_ERROR;
			wake_up_all(&q->bufs[i]->done);
		}
	}
	spin_unlock_irqrestore(q->irqlock, flags);

	/* free all buffers + clear queue */
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		q->ops->buf_release(q, q->bufs[i]);
	}
	INIT_LIST_HEAD(&q->stream);
}
EXPORT_SYMBOL_GPL(videobuf_queue_cancel);

/* --------------------------------------------------------------------- */

/* Locking: Caller holds q->vb_lock */
enum v4l2_field videobuf_next_field(struct videobuf_queue *q)
{
	enum v4l2_field field = q->field;

	BUG_ON(V4L2_FIELD_ANY == field);

	if (V4L2_FIELD_ALTERNATE == field) {
		if (V4L2_FIELD_TOP == q->last) {
			field   = V4L2_FIELD_BOTTOM;
			q->last = V4L2_FIELD_BOTTOM;
		} else {
			field   = V4L2_FIELD_TOP;
			q->last = V4L2_FIELD_TOP;
		}
	}
	return field;
}
EXPORT_SYMBOL_GPL(videobuf_next_field);

/* Locking: Caller holds q->vb_lock */
static void videobuf_status(struct videobuf_queue *q, struct v4l2_buffer *b,
			    struct videobuf_buffer *vb, enum v4l2_buf_type type)
{
	MAGIC_CHECK(vb->magic, MAGIC_BUFFER);
	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	b->index    = vb->i;
	b->type     = type;

	b->memory   = vb->memory;
	switch (b->memory) {
	case V4L2_MEMORY_MMAP:
		b->m.offset  = vb->boff;
		b->length    = vb->bsize;
		break;
	case V4L2_MEMORY_USERPTR:
		b->m.userptr = vb->baddr;
		b->length    = vb->bsize;
		break;
	case V4L2_MEMORY_OVERLAY:
		b->m.offset  = vb->boff;
		break;
	case V4L2_MEMORY_DMABUF:
		/* DMABUF is not handled in videobuf framework */
		break;
	}

	b->flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	if (vb->map)
		b->flags |= V4L2_BUF_FLAG_MAPPED;

	switch (vb->state) {
	case VIDEOBUF_PREPARED:
	case VIDEOBUF_QUEUED:
	case VIDEOBUF_ACTIVE:
		b->flags |= V4L2_BUF_FLAG_QUEUED;
		break;
	case VIDEOBUF_ERROR:
		b->flags |= V4L2_BUF_FLAG_ERROR;
		/* fall through */
	case VIDEOBUF_DONE:
		b->flags |= V4L2_BUF_FLAG_DONE;
		break;
	case VIDEOBUF_NEEDS_INIT:
	case VIDEOBUF_IDLE:
		/* nothing */
		break;
	}

	b->field     = vb->field;
	b->timestamp = vb->ts;
	b->bytesused = vb->size;
	b->sequence  = vb->field_count >> 1;
}

int videobuf_mmap_free(struct videobuf_queue *q)
{
	int ret;
	videobuf_queue_lock(q);
	ret = __videobuf_free(q);
	videobuf_queue_unlock(q);
	return ret;
}
EXPORT_SYMBOL_GPL(videobuf_mmap_free);

/* Locking: Caller holds q->vb_lock */
int __videobuf_mmap_setup(struct videobuf_queue *q,
			unsigned int bcount, unsigned int bsize,
			enum v4l2_memory memory)
{
	unsigned int i;
	int err;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	err = __videobuf_free(q);
	if (0 != err)
		return err;

	/* Allocate and initialize buffers */
	for (i = 0; i < bcount; i++) {
		q->bufs[i] = videobuf_alloc_vb(q);

		if (NULL == q->bufs[i])
			break;

		q->bufs[i]->i      = i;
		q->bufs[i]->memory = memory;
		q->bufs[i]->bsize  = bsize;
		switch (memory) {
		case V4L2_MEMORY_MMAP:
			q->bufs[i]->boff = PAGE_ALIGN(bsize) * i;
			break;
		case V4L2_MEMORY_USERPTR:
		case V4L2_MEMORY_OVERLAY:
		case V4L2_MEMORY_DMABUF:
			/* nothing */
			break;
		}
	}

	if (!i)
		return -ENOMEM;

	dprintk(1, "mmap setup: %d buffers, %d bytes each\n", i, bsize);

	return i;
}
EXPORT_SYMBOL_GPL(__videobuf_mmap_setup);

int videobuf_mmap_setup(struct videobuf_queue *q,
			unsigned int bcount, unsigned int bsize,
			enum v4l2_memory memory)
{
	int ret;
	videobuf_queue_lock(q);
	ret = __videobuf_mmap_setup(q, bcount, bsize, memory);
	videobuf_queue_unlock(q);
	return ret;
}
EXPORT_SYMBOL_GPL(videobuf_mmap_setup);

int videobuf_reqbufs(struct videobuf_queue *q,
		 struct v4l2_requestbuffers *req)
{
	unsigned int size, count;
	int retval;

	if (req->memory != V4L2_MEMORY_MMAP     &&
	    req->memory != V4L2_MEMORY_USERPTR  &&
	    req->memory != V4L2_MEMORY_OVERLAY) {
		dprintk(1, "reqbufs: memory type invalid\n");
		return -EINVAL;
	}

	videobuf_queue_lock(q);
	if (req->type != q->type) {
		dprintk(1, "reqbufs: queue type invalid\n");
		retval = -EINVAL;
		goto done;
	}

	if (q->streaming) {
		dprintk(1, "reqbufs: streaming already exists\n");
		retval = -EBUSY;
		goto done;
	}
	if (!list_empty(&q->stream)) {
		dprintk(1, "reqbufs: stream running\n");
		retval = -EBUSY;
		goto done;
	}

	if (req->count == 0) {
		dprintk(1, "reqbufs: count invalid (%d)\n", req->count);
		retval = __videobuf_free(q);
		goto done;
	}

	count = req->count;
	if (count > VIDEO_MAX_FRAME)
		count = VIDEO_MAX_FRAME;
	size = 0;
	q->ops->buf_setup(q, &count, &size);
	dprintk(1, "reqbufs: bufs=%d, size=0x%x [%u pages total]\n",
		count, size,
		(unsigned int)((count * PAGE_ALIGN(size)) >> PAGE_SHIFT));

	retval = __videobuf_mmap_setup(q, count, size, req->memory);
	if (retval < 0) {
		dprintk(1, "reqbufs: mmap setup returned %d\n", retval);
		goto done;
	}

	req->count = retval;
	retval = 0;

 done:
	videobuf_queue_unlock(q);
	return retval;
}
EXPORT_SYMBOL_GPL(videobuf_reqbufs);

int videobuf_querybuf(struct videobuf_queue *q, struct v4l2_buffer *b)
{
	int ret = -EINVAL;

	videobuf_queue_lock(q);
	if (unlikely(b->type != q->type)) {
		dprintk(1, "querybuf: Wrong type.\n");
		goto done;
	}
	if (unlikely(b->index >= VIDEO_MAX_FRAME)) {
		dprintk(1, "querybuf: index out of range.\n");
		goto done;
	}
	if (unlikely(NULL == q->bufs[b->index])) {
		dprintk(1, "querybuf: buffer is null.\n");
		goto done;
	}

	videobuf_status(q, b, q->bufs[b->index], q->type);

	ret = 0;
done:
	videobuf_queue_unlock(q);
	return ret;
}
EXPORT_SYMBOL_GPL(videobuf_querybuf);

int videobuf_qbuf(struct videobuf_queue *q, struct v4l2_buffer *b)
{
	struct videobuf_buffer *buf;
	enum v4l2_field field;
	unsigned long flags = 0;
	int retval;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	if (b->memory == V4L2_MEMORY_MMAP)
		down_read(&current->mm->mmap_sem);

	videobuf_queue_lock(q);
	retval = -EBUSY;
	if (q->reading) {
		dprintk(1, "qbuf: Reading running...\n");
		goto done;
	}
	retval = -EINVAL;
	if (b->type != q->type) {
		dprintk(1, "qbuf: Wrong type.\n");
		goto done;
	}
	if (b->index >= VIDEO_MAX_FRAME) {
		dprintk(1, "qbuf: index out of range.\n");
		goto done;
	}
	buf = q->bufs[b->index];
	if (NULL == buf) {
		dprintk(1, "qbuf: buffer is null.\n");
		goto done;
	}
	MAGIC_CHECK(buf->magic, MAGIC_BUFFER);
	if (buf->memory != b->memory) {
		dprintk(1, "qbuf: memory type is wrong.\n");
		goto done;
	}
	if (buf->state != VIDEOBUF_NEEDS_INIT && buf->state != VIDEOBUF_IDLE) {
		dprintk(1, "qbuf: buffer is already queued or active.\n");
		goto done;
	}

	switch (b->memory) {
	case V4L2_MEMORY_MMAP:
		if (0 == buf->baddr) {
			dprintk(1, "qbuf: mmap requested but buffer addr is zero!\n");
			goto done;
		}
		if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT
		    || q->type == V4L2_BUF_TYPE_VBI_OUTPUT
		    || q->type == V4L2_BUF_TYPE_SLICED_VBI_OUTPUT
		    || q->type == V4L2_BUF_TYPE_SDR_OUTPUT) {
			buf->size = b->bytesused;
			buf->field = b->field;
			buf->ts = b->timestamp;
		}
		break;
	case V4L2_MEMORY_USERPTR:
		if (b->length < buf->bsize) {
			dprintk(1, "qbuf: buffer length is not enough\n");
			goto done;
		}
		if (VIDEOBUF_NEEDS_INIT != buf->state &&
		    buf->baddr != b->m.userptr)
			q->ops->buf_release(q, buf);
		buf->baddr = b->m.userptr;
		break;
	case V4L2_MEMORY_OVERLAY:
		buf->boff = b->m.offset;
		break;
	default:
		dprintk(1, "qbuf: wrong memory type\n");
		goto done;
	}

	dprintk(1, "qbuf: requesting next field\n");
	field = videobuf_next_field(q);
	retval = q->ops->buf_prepare(q, buf, field);
	if (0 != retval) {
		dprintk(1, "qbuf: buffer_prepare returned %d\n", retval);
		goto done;
	}

	list_add_tail(&buf->stream, &q->stream);
	if (q->streaming) {
		spin_lock_irqsave(q->irqlock, flags);
		q->ops->buf_queue(q, buf);
		spin_unlock_irqrestore(q->irqlock, flags);
	}
	dprintk(1, "qbuf: succeeded\n");
	retval = 0;
	wake_up_interruptible_sync(&q->wait);

done:
	videobuf_queue_unlock(q);

	if (b->memory == V4L2_MEMORY_MMAP)
		up_read(&current->mm->mmap_sem);

	return retval;
}
EXPORT_SYMBOL_GPL(videobuf_qbuf);

/* Locking: Caller holds q->vb_lock */
static int stream_next_buffer_check_queue(struct videobuf_queue *q, int noblock)
{
	int retval;

checks:
	if (!q->streaming) {
		dprintk(1, "next_buffer: Not streaming\n");
		retval = -EINVAL;
		goto done;
	}

	if (list_empty(&q->stream)) {
		if (noblock) {
			retval = -EAGAIN;
			dprintk(2, "next_buffer: no buffers to dequeue\n");
			goto done;
		} else {
			dprintk(2, "next_buffer: waiting on buffer\n");

			/* Drop lock to avoid deadlock with qbuf */
			videobuf_queue_unlock(q);

			/* Checking list_empty and streaming is safe without
			 * locks because we goto checks to validate while
			 * holding locks before proceeding */
			retval = wait_event_interruptible(q->wait,
				!list_empty(&q->stream) || !q->streaming);
			videobuf_queue_lock(q);

			if (retval)
				goto done;

			goto checks;
		}
	}

	retval = 0;

done:
	return retval;
}

/* Locking: Caller holds q->vb_lock */
static int stream_next_buffer(struct videobuf_queue *q,
			struct videobuf_buffer **vb, int nonblocking)
{
	int retval;
	struct videobuf_buffer *buf = NULL;

	retval = stream_next_buffer_check_queue(q, nonblocking);
	if (retval)
		goto done;

	buf = list_entry(q->stream.next, struct videobuf_buffer, stream);
	retval = videobuf_waiton(q, buf, nonblocking, 1);
	if (retval < 0)
		goto done;

	*vb = buf;
done:
	return retval;
}

int videobuf_dqbuf(struct videobuf_queue *q,
		   struct v4l2_buffer *b, int nonblocking)
{
	struct videobuf_buffer *buf = NULL;
	int retval;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	memset(b, 0, sizeof(*b));
	videobuf_queue_lock(q);

	retval = stream_next_buffer(q, &buf, nonblocking);
	if (retval < 0) {
		dprintk(1, "dqbuf: next_buffer error: %i\n", retval);
		goto done;
	}

	switch (buf->state) {
	case VIDEOBUF_ERROR:
		dprintk(1, "dqbuf: state is error\n");
		break;
	case VIDEOBUF_DONE:
		dprintk(1, "dqbuf: state is done\n");
		break;
	default:
		dprintk(1, "dqbuf: state invalid\n");
		retval = -EINVAL;
		goto done;
	}
	CALL(q, sync, q, buf);
	videobuf_status(q, b, buf, q->type);
	list_del(&buf->stream);
	buf->state = VIDEOBUF_IDLE;
	b->flags &= ~V4L2_BUF_FLAG_DONE;
done:
	videobuf_queue_unlock(q);
	return retval;
}
EXPORT_SYMBOL_GPL(videobuf_dqbuf);

int videobuf_streamon(struct videobuf_queue *q)
{
	struct videobuf_buffer *buf;
	unsigned long flags = 0;
	int retval;

	videobuf_queue_lock(q);
	retval = -EBUSY;
	if (q->reading)
		goto done;
	retval = 0;
	if (q->streaming)
		goto done;
	q->streaming = 1;
	spin_lock_irqsave(q->irqlock, flags);
	list_for_each_entry(buf, &q->stream, stream)
		if (buf->state == VIDEOBUF_PREPARED)
			q->ops->buf_queue(q, buf);
	spin_unlock_irqrestore(q->irqlock, flags);

	wake_up_interruptible_sync(&q->wait);
done:
	videobuf_queue_unlock(q);
	return retval;
}
EXPORT_SYMBOL_GPL(videobuf_streamon);

/* Locking: Caller holds q->vb_lock */
static int __videobuf_streamoff(struct videobuf_queue *q)
{
	if (!q->streaming)
		return -EINVAL;

	videobuf_queue_cancel(q);

	return 0;
}

int videobuf_streamoff(struct videobuf_queue *q)
{
	int retval;

	videobuf_queue_lock(q);
	retval = __videobuf_streamoff(q);
	videobuf_queue_unlock(q);

	return retval;
}
EXPORT_SYMBOL_GPL(videobuf_streamoff);

/* Locking: Caller holds q->vb_lock */
static ssize_t videobuf_read_zerocopy(struct videobuf_queue *q,
				      char __user *data,
				      size_t count, loff_t *ppos)
{
	enum v4l2_field field;
	unsigned long flags = 0;
	int retval;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	/* setup stuff */
	q->read_buf = videobuf_alloc_vb(q);
	if (NULL == q->read_buf)
		return -ENOMEM;

	q->read_buf->memory = V4L2_MEMORY_USERPTR;
	q->read_buf->baddr  = (unsigned long)data;
	q->read_buf->bsize  = count;

	field = videobuf_next_field(q);
	retval = q->ops->buf_prepare(q, q->read_buf, field);
	if (0 != retval)
		goto done;

	/* start capture & wait */
	spin_lock_irqsave(q->irqlock, flags);
	q->ops->buf_queue(q, q->read_buf);
	spin_unlock_irqrestore(q->irqlock, flags);
	retval = videobuf_waiton(q, q->read_buf, 0, 0);
	if (0 == retval) {
		CALL(q, sync, q, q->read_buf);
		if (VIDEOBUF_ERROR == q->read_buf->state)
			retval = -EIO;
		else
			retval = q->read_buf->size;
	}

done:
	/* cleanup */
	q->ops->buf_release(q, q->read_buf);
	kfree(q->read_buf);
	q->read_buf = NULL;
	return retval;
}

static int __videobuf_copy_to_user(struct videobuf_queue *q,
				   struct videobuf_buffer *buf,
				   char __user *data, size_t count,
				   int nonblocking)
{
	void *vaddr = CALLPTR(q, vaddr, buf);

	/* copy to userspace */
	if (count > buf->size - q->read_off)
		count = buf->size - q->read_off;

	if (copy_to_user(data, vaddr + q->read_off, count))
		return -EFAULT;

	return count;
}

static int __videobuf_copy_stream(struct videobuf_queue *q,
				  struct videobuf_buffer *buf,
				  char __user *data, size_t count, size_t pos,
				  int vbihack, int nonblocking)
{
	unsigned int *fc = CALLPTR(q, vaddr, buf);

	if (vbihack) {
		/* dirty, undocumented hack -- pass the frame counter
			* within the last four bytes of each vbi data block.
			* We need that one to maintain backward compatibility
			* to all vbi decoding software out there ... */
		fc += (buf->size >> 2) - 1;
		*fc = buf->field_count >> 1;
		dprintk(1, "vbihack: %d\n", *fc);
	}

	/* copy stuff using the common method */
	count = __videobuf_copy_to_user(q, buf, data, count, nonblocking);

	if ((count == -EFAULT) && (pos == 0))
		return -EFAULT;

	return count;
}

ssize_t videobuf_read_one(struct videobuf_queue *q,
			  char __user *data, size_t count, loff_t *ppos,
			  int nonblocking)
{
	enum v4l2_field field;
	unsigned long flags = 0;
	unsigned size = 0, nbufs = 1;
	int retval;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	videobuf_queue_lock(q);

	q->ops->buf_setup(q, &nbufs, &size);

	if (NULL == q->read_buf  &&
	    count >= size        &&
	    !nonblocking) {
		retval = videobuf_read_zerocopy(q, data, count, ppos);
		if (retval >= 0  ||  retval == -EIO)
			/* ok, all done */
			goto done;
		/* fallback to kernel bounce buffer on failures */
	}

	if (NULL == q->read_buf) {
		/* need to capture a new frame */
		retval = -ENOMEM;
		q->read_buf = videobuf_alloc_vb(q);

		dprintk(1, "video alloc=0x%p\n", q->read_buf);
		if (NULL == q->read_buf)
			goto done;
		q->read_buf->memory = V4L2_MEMORY_USERPTR;
		q->read_buf->bsize = count; /* preferred size */
		field = videobuf_next_field(q);
		retval = q->ops->buf_prepare(q, q->read_buf, field);

		if (0 != retval) {
			kfree(q->read_buf);
			q->read_buf = NULL;
			goto done;
		}

		spin_lock_irqsave(q->irqlock, flags);
		q->ops->buf_queue(q, q->read_buf);
		spin_unlock_irqrestore(q->irqlock, flags);

		q->read_off = 0;
	}

	/* wait until capture is done */
	retval = videobuf_waiton(q, q->read_buf, nonblocking, 1);
	if (0 != retval)
		goto done;

	CALL(q, sync, q, q->read_buf);

	if (VIDEOBUF_ERROR == q->read_buf->state) {
		/* catch I/O errors */
		q->ops->buf_release(q, q->read_buf);
		kfree(q->read_buf);
		q->read_buf = NULL;
		retval = -EIO;
		goto done;
	}

	/* Copy to userspace */
	retval = __videobuf_copy_to_user(q, q->read_buf, data, count, nonblocking);
	if (retval < 0)
		goto done;

	q->read_off += retval;
	if (q->read_off == q->read_buf->size) {
		/* all data copied, cleanup */
		q->ops->buf_release(q, q->read_buf);
		kfree(q->read_buf);
		q->read_buf = NULL;
	}

done:
	videobuf_queue_unlock(q);
	return retval;
}
EXPORT_SYMBOL_GPL(videobuf_read_one);

/* Locking: Caller holds q->vb_lock */
static int __videobuf_read_start(struct videobuf_queue *q)
{
	enum v4l2_field field;
	unsigned long flags = 0;
	unsigned int count = 0, size = 0;
	int err, i;

	q->ops->buf_setup(q, &count, &size);
	if (count < 2)
		count = 2;
	if (count > VIDEO_MAX_FRAME)
		count = VIDEO_MAX_FRAME;
	size = PAGE_ALIGN(size);

	err = __videobuf_mmap_setup(q, count, size, V4L2_MEMORY_USERPTR);
	if (err < 0)
		return err;

	count = err;

	for (i = 0; i < count; i++) {
		field = videobuf_next_field(q);
		err = q->ops->buf_prepare(q, q->bufs[i], field);
		if (err)
			return err;
		list_add_tail(&q->bufs[i]->stream, &q->stream);
	}
	spin_lock_irqsave(q->irqlock, flags);
	for (i = 0; i < count; i++)
		q->ops->buf_queue(q, q->bufs[i]);
	spin_unlock_irqrestore(q->irqlock, flags);
	q->reading = 1;
	return 0;
}

static void __videobuf_read_stop(struct videobuf_queue *q)
{
	int i;

	videobuf_queue_cancel(q);
	__videobuf_free(q);
	INIT_LIST_HEAD(&q->stream);
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		kfree(q->bufs[i]);
		q->bufs[i] = NULL;
	}
	q->read_buf = NULL;
}

int videobuf_read_start(struct videobuf_queue *q)
{
	int rc;

	videobuf_queue_lock(q);
	rc = __videobuf_read_start(q);
	videobuf_queue_unlock(q);

	return rc;
}
EXPORT_SYMBOL_GPL(videobuf_read_start);

void videobuf_read_stop(struct videobuf_queue *q)
{
	videobuf_queue_lock(q);
	__videobuf_read_stop(q);
	videobuf_queue_unlock(q);
}
EXPORT_SYMBOL_GPL(videobuf_read_stop);

void videobuf_stop(struct videobuf_queue *q)
{
	videobuf_queue_lock(q);

	if (q->streaming)
		__videobuf_streamoff(q);

	if (q->reading)
		__videobuf_read_stop(q);

	videobuf_queue_unlock(q);
}
EXPORT_SYMBOL_GPL(videobuf_stop);

ssize_t videobuf_read_stream(struct videobuf_queue *q,
			     char __user *data, size_t count, loff_t *ppos,
			     int vbihack, int nonblocking)
{
	int rc, retval;
	unsigned long flags = 0;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	dprintk(2, "%s\n", __func__);
	videobuf_queue_lock(q);
	retval = -EBUSY;
	if (q->streaming)
		goto done;
	if (!q->reading) {
		retval = __videobuf_read_start(q);
		if (retval < 0)
			goto done;
	}

	retval = 0;
	while (count > 0) {
		/* get / wait for data */
		if (NULL == q->read_buf) {
			q->read_buf = list_entry(q->stream.next,
						 struct videobuf_buffer,
						 stream);
			list_del(&q->read_buf->stream);
			q->read_off = 0;
		}
		rc = videobuf_waiton(q, q->read_buf, nonblocking, 1);
		if (rc < 0) {
			if (0 == retval)
				retval = rc;
			break;
		}

		if (q->read_buf->state == VIDEOBUF_DONE) {
			rc = __videobuf_copy_stream(q, q->read_buf, data + retval, count,
					retval, vbihack, nonblocking);
			if (rc < 0) {
				retval = rc;
				break;
			}
			retval      += rc;
			count       -= rc;
			q->read_off += rc;
		} else {
			/* some error */
			q->read_off = q->read_buf->size;
			if (0 == retval)
				retval = -EIO;
		}

		/* requeue buffer when done with copying */
		if (q->read_off == q->read_buf->size) {
			list_add_tail(&q->read_buf->stream,
				      &q->stream);
			spin_lock_irqsave(q->irqlock, flags);
			q->ops->buf_queue(q, q->read_buf);
			spin_unlock_irqrestore(q->irqlock, flags);
			q->read_buf = NULL;
		}
		if (retval < 0)
			break;
	}

done:
	videobuf_queue_unlock(q);
	return retval;
}
EXPORT_SYMBOL_GPL(videobuf_read_stream);

__poll_t videobuf_poll_stream(struct file *file,
				  struct videobuf_queue *q,
				  poll_table *wait)
{
	__poll_t req_events = poll_requested_events(wait);
	struct videobuf_buffer *buf = NULL;
	__poll_t rc = 0;

	videobuf_queue_lock(q);
	if (q->streaming) {
		if (!list_empty(&q->stream))
			buf = list_entry(q->stream.next,
					 struct videobuf_buffer, stream);
	} else if (req_events & (POLLIN | POLLRDNORM)) {
		if (!q->reading)
			__videobuf_read_start(q);
		if (!q->reading) {
			rc = POLLERR;
		} else if (NULL == q->read_buf) {
			q->read_buf = list_entry(q->stream.next,
						 struct videobuf_buffer,
						 stream);
			list_del(&q->read_buf->stream);
			q->read_off = 0;
		}
		buf = q->read_buf;
	}
	if (!buf)
		rc = POLLERR;

	if (0 == rc) {
		poll_wait(file, &buf->done, wait);
		if (buf->state == VIDEOBUF_DONE ||
		    buf->state == VIDEOBUF_ERROR) {
			switch (q->type) {
			case V4L2_BUF_TYPE_VIDEO_OUTPUT:
			case V4L2_BUF_TYPE_VBI_OUTPUT:
			case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
			case V4L2_BUF_TYPE_SDR_OUTPUT:
				rc = POLLOUT | POLLWRNORM;
				break;
			default:
				rc = POLLIN | POLLRDNORM;
				break;
			}
		}
	}
	videobuf_queue_unlock(q);
	return rc;
}
EXPORT_SYMBOL_GPL(videobuf_poll_stream);

int videobuf_mmap_mapper(struct videobuf_queue *q, struct vm_area_struct *vma)
{
	int rc = -EINVAL;
	int i;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	if (!(vma->vm_flags & VM_WRITE) || !(vma->vm_flags & VM_SHARED)) {
		dprintk(1, "mmap appl bug: PROT_WRITE and MAP_SHARED are required\n");
		return -EINVAL;
	}

	videobuf_queue_lock(q);
	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		struct videobuf_buffer *buf = q->bufs[i];

		if (buf && buf->memory == V4L2_MEMORY_MMAP &&
				buf->boff == (vma->vm_pgoff << PAGE_SHIFT)) {
			rc = CALL(q, mmap_mapper, q, buf, vma);
			break;
		}
	}
	videobuf_queue_unlock(q);

	return rc;
}
EXPORT_SYMBOL_GPL(videobuf_mmap_mapper);
