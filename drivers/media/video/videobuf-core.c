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
#define MAGIC_CHECK(is, should) do {					   \
	if (unlikely((is) != (should))) {				   \
	printk(KERN_ERR "magic mismatch: %x (expected %x)\n", is, should); \
	BUG(); } } while (0)

static int debug;
module_param(debug, int, 0644);

MODULE_DESCRIPTION("helper module to manage video4linux buffers");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@infradead.org>");
MODULE_LICENSE("GPL");

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_DEBUG "vbuf: " fmt , ## arg); } while (0)

/* --------------------------------------------------------------------- */

#define CALL(q, f, arg...)						\
	((q->int_ops->f) ? q->int_ops->f(arg) : 0)

void *videobuf_alloc(struct videobuf_queue *q)
{
	struct videobuf_buffer *vb;

	BUG_ON(q->msize < sizeof(*vb));

	if (!q->int_ops || !q->int_ops->alloc) {
		printk(KERN_ERR "No specific ops defined!\n");
		BUG();
	}

	vb = q->int_ops->alloc(q->msize);

	if (NULL != vb) {
		init_waitqueue_head(&vb->done);
		vb->magic     = MAGIC_BUFFER;
	}

	return vb;
}

#define WAITON_CONDITION (vb->state != VIDEOBUF_ACTIVE &&\
				vb->state != VIDEOBUF_QUEUED)
int videobuf_waiton(struct videobuf_buffer *vb, int non_blocking, int intr)
{
	MAGIC_CHECK(vb->magic, MAGIC_BUFFER);

	if (non_blocking) {
		if (WAITON_CONDITION)
			return 0;
		else
			return -EAGAIN;
	}

	if (intr)
		return wait_event_interruptible(vb->done, WAITON_CONDITION);
	else
		wait_event(vb->done, WAITON_CONDITION);

	return 0;
}

int videobuf_iolock(struct videobuf_queue *q, struct videobuf_buffer *vb,
		    struct v4l2_framebuffer *fbuf)
{
	MAGIC_CHECK(vb->magic, MAGIC_BUFFER);
	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	return CALL(q, iolock, q, vb, fbuf);
}

void *videobuf_queue_to_vmalloc (struct videobuf_queue *q,
			   struct videobuf_buffer *buf)
{
	if (q->int_ops->vmalloc)
		return q->int_ops->vmalloc(buf);
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(videobuf_queue_to_vmalloc);

/* --------------------------------------------------------------------- */


void videobuf_queue_core_init(struct videobuf_queue *q,
			 const struct videobuf_queue_ops *ops,
			 struct device *dev,
			 spinlock_t *irqlock,
			 enum v4l2_buf_type type,
			 enum v4l2_field field,
			 unsigned int msize,
			 void *priv,
			 struct videobuf_qtype_ops *int_ops)
{
	BUG_ON(!q);
	memset(q, 0, sizeof(*q));
	q->irqlock   = irqlock;
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
	}

	b->flags    = 0;
	if (vb->map)
		b->flags |= V4L2_BUF_FLAG_MAPPED;

	switch (vb->state) {
	case VIDEOBUF_PREPARED:
	case VIDEOBUF_QUEUED:
	case VIDEOBUF_ACTIVE:
		b->flags |= V4L2_BUF_FLAG_QUEUED;
		break;
	case VIDEOBUF_DONE:
	case VIDEOBUF_ERROR:
		b->flags |= V4L2_BUF_FLAG_DONE;
		break;
	case VIDEOBUF_NEEDS_INIT:
	case VIDEOBUF_IDLE:
		/* nothing */
		break;
	}

	if (vb->input != UNSET) {
		b->flags |= V4L2_BUF_FLAG_INPUT;
		b->input  = vb->input;
	}

	b->field     = vb->field;
	b->timestamp = vb->ts;
	b->bytesused = vb->size;
	b->sequence  = vb->field_count >> 1;
}

/* Locking: Caller holds q->vb_lock */
static int __videobuf_mmap_free(struct videobuf_queue *q)
{
	int i;
	int rc;

	if (!q)
		return 0;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);


	rc  = CALL(q, mmap_free, q);

	q->is_mmapped = 0;

	if (rc < 0)
		return rc;

	for (i = 0; i < VIDEO_MAX_FRAME; i++) {
		if (NULL == q->bufs[i])
			continue;
		q->ops->buf_release(q, q->bufs[i]);
		kfree(q->bufs[i]);
		q->bufs[i] = NULL;
	}

	return rc;
}

int videobuf_mmap_free(struct videobuf_queue *q)
{
	int ret;
	mutex_lock(&q->vb_lock);
	ret = __videobuf_mmap_free(q);
	mutex_unlock(&q->vb_lock);
	return ret;
}

/* Locking: Caller holds q->vb_lock */
int __videobuf_mmap_setup(struct videobuf_queue *q,
			unsigned int bcount, unsigned int bsize,
			enum v4l2_memory memory)
{
	unsigned int i;
	int err;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	err = __videobuf_mmap_free(q);
	if (0 != err)
		return err;

	/* Allocate and initialize buffers */
	for (i = 0; i < bcount; i++) {
		q->bufs[i] = videobuf_alloc(q);

		if (q->bufs[i] == NULL)
			break;

		q->bufs[i]->i      = i;
		q->bufs[i]->input  = UNSET;
		q->bufs[i]->memory = memory;
		q->bufs[i]->bsize  = bsize;
		switch (memory) {
		case V4L2_MEMORY_MMAP:
			q->bufs[i]->boff = PAGE_ALIGN(bsize) * i;
			break;
		case V4L2_MEMORY_USERPTR:
		case V4L2_MEMORY_OVERLAY:
			/* nothing */
			break;
		}
	}

	if (!i)
		return -ENOMEM;

	dprintk(1, "mmap setup: %d buffers, %d bytes each\n",
		i, bsize);

	return i;
}

int videobuf_mmap_setup(struct videobuf_queue *q,
			unsigned int bcount, unsigned int bsize,
			enum v4l2_memory memory)
{
	int ret;
	mutex_lock(&q->vb_lock);
	ret = __videobuf_mmap_setup(q, bcount, bsize, memory);
	mutex_unlock(&q->vb_lock);
	return ret;
}

int videobuf_reqbufs(struct videobuf_queue *q,
		 struct v4l2_requestbuffers *req)
{
	unsigned int size, count;
	int retval;

	if (req->count < 1) {
		dprintk(1, "reqbufs: count invalid (%d)\n", req->count);
		return -EINVAL;
	}

	if (req->memory != V4L2_MEMORY_MMAP     &&
	    req->memory != V4L2_MEMORY_USERPTR  &&
	    req->memory != V4L2_MEMORY_OVERLAY) {
		dprintk(1, "reqbufs: memory type invalid\n");
		return -EINVAL;
	}

	mutex_lock(&q->vb_lock);
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

	count = req->count;
	if (count > VIDEO_MAX_FRAME)
		count = VIDEO_MAX_FRAME;
	size = 0;
	q->ops->buf_setup(q, &count, &size);
	dprintk(1, "reqbufs: bufs=%d, size=0x%x [%u pages total]\n",
		count, size,
		(unsigned int)((count*PAGE_ALIGN(size))>>PAGE_SHIFT) );

	retval = __videobuf_mmap_setup(q, count, size, req->memory);
	if (retval < 0) {
		dprintk(1, "reqbufs: mmap setup returned %d\n", retval);
		goto done;
	}

	req->count = retval;
	retval = 0;

 done:
	mutex_unlock(&q->vb_lock);
	return retval;
}

int videobuf_querybuf(struct videobuf_queue *q, struct v4l2_buffer *b)
{
	int ret = -EINVAL;

	mutex_lock(&q->vb_lock);
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
	mutex_unlock(&q->vb_lock);
	return ret;
}

int videobuf_qbuf(struct videobuf_queue *q,
	      struct v4l2_buffer *b)
{
	struct videobuf_buffer *buf;
	enum v4l2_field field;
	unsigned long flags = 0;
	int retval;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	if (b->memory == V4L2_MEMORY_MMAP)
		down_read(&current->mm->mmap_sem);

	mutex_lock(&q->vb_lock);
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

	if (b->flags & V4L2_BUF_FLAG_INPUT) {
		if (b->input >= q->inputs) {
			dprintk(1, "qbuf: wrong input.\n");
			goto done;
		}
		buf->input = b->input;
	} else {
		buf->input = UNSET;
	}

	switch (b->memory) {
	case V4L2_MEMORY_MMAP:
		if (0 == buf->baddr) {
			dprintk(1, "qbuf: mmap requested "
				   "but buffer addr is zero!\n");
			goto done;
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
	dprintk(1, "qbuf: succeded\n");
	retval = 0;
	wake_up_interruptible_sync(&q->wait);

 done:
	mutex_unlock(&q->vb_lock);

	if (b->memory == V4L2_MEMORY_MMAP)
		up_read(&current->mm->mmap_sem);

	return retval;
}


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
			mutex_unlock(&q->vb_lock);

			/* Checking list_empty and streaming is safe without
			 * locks because we goto checks to validate while
			 * holding locks before proceeding */
			retval = wait_event_interruptible(q->wait,
				!list_empty(&q->stream) || !q->streaming);
			mutex_lock(&q->vb_lock);

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
	retval = videobuf_waiton(buf, nonblocking, 1);
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

	mutex_lock(&q->vb_lock);

	retval = stream_next_buffer(q, &buf, nonblocking);
	if (retval < 0) {
		dprintk(1, "dqbuf: next_buffer error: %i\n", retval);
		goto done;
	}

	switch (buf->state) {
	case VIDEOBUF_ERROR:
		dprintk(1, "dqbuf: state is error\n");
		retval = -EIO;
		CALL(q, sync, q, buf);
		buf->state = VIDEOBUF_IDLE;
		break;
	case VIDEOBUF_DONE:
		dprintk(1, "dqbuf: state is done\n");
		CALL(q, sync, q, buf);
		buf->state = VIDEOBUF_IDLE;
		break;
	default:
		dprintk(1, "dqbuf: state invalid\n");
		retval = -EINVAL;
		goto done;
	}
	list_del(&buf->stream);
	memset(b, 0, sizeof(*b));
	videobuf_status(q, b, buf, q->type);

 done:
	mutex_unlock(&q->vb_lock);
	return retval;
}

int videobuf_streamon(struct videobuf_queue *q)
{
	struct videobuf_buffer *buf;
	unsigned long flags = 0;
	int retval;

	mutex_lock(&q->vb_lock);
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
	mutex_unlock(&q->vb_lock);
	return retval;
}

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

	mutex_lock(&q->vb_lock);
	retval = __videobuf_streamoff(q);
	mutex_unlock(&q->vb_lock);

	return retval;
}

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
	q->read_buf = videobuf_alloc(q);
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
	retval = videobuf_waiton(q->read_buf, 0, 0);
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

ssize_t videobuf_read_one(struct videobuf_queue *q,
			  char __user *data, size_t count, loff_t *ppos,
			  int nonblocking)
{
	enum v4l2_field field;
	unsigned long flags = 0;
	unsigned size = 0, nbufs = 1;
	int retval;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	mutex_lock(&q->vb_lock);

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
		q->read_buf = videobuf_alloc(q);

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
	retval = videobuf_waiton(q->read_buf, nonblocking, 1);
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
	retval = CALL(q, video_copy_to_user, q, data, count, nonblocking);
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
	mutex_unlock(&q->vb_lock);
	return retval;
}

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
	__videobuf_mmap_free(q);
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

	mutex_lock(&q->vb_lock);
	rc = __videobuf_read_start(q);
	mutex_unlock(&q->vb_lock);

	return rc;
}

void videobuf_read_stop(struct videobuf_queue *q)
{
	mutex_lock(&q->vb_lock);
	__videobuf_read_stop(q);
	mutex_unlock(&q->vb_lock);
}

void videobuf_stop(struct videobuf_queue *q)
{
	mutex_lock(&q->vb_lock);

	if (q->streaming)
		__videobuf_streamoff(q);

	if (q->reading)
		__videobuf_read_stop(q);

	mutex_unlock(&q->vb_lock);
}


ssize_t videobuf_read_stream(struct videobuf_queue *q,
			     char __user *data, size_t count, loff_t *ppos,
			     int vbihack, int nonblocking)
{
	int rc, retval;
	unsigned long flags = 0;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	dprintk(2, "%s\n", __func__);
	mutex_lock(&q->vb_lock);
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
		rc = videobuf_waiton(q->read_buf, nonblocking, 1);
		if (rc < 0) {
			if (0 == retval)
				retval = rc;
			break;
		}

		if (q->read_buf->state == VIDEOBUF_DONE) {
			rc = CALL(q, copy_stream, q, data + retval, count,
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
	mutex_unlock(&q->vb_lock);
	return retval;
}

unsigned int videobuf_poll_stream(struct file *file,
				  struct videobuf_queue *q,
				  poll_table *wait)
{
	struct videobuf_buffer *buf = NULL;
	unsigned int rc = 0;

	mutex_lock(&q->vb_lock);
	if (q->streaming) {
		if (!list_empty(&q->stream))
			buf = list_entry(q->stream.next,
					 struct videobuf_buffer, stream);
	} else {
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
		    buf->state == VIDEOBUF_ERROR)
			rc = POLLIN|POLLRDNORM;
	}
	mutex_unlock(&q->vb_lock);
	return rc;
}

int videobuf_mmap_mapper(struct videobuf_queue *q,
			 struct vm_area_struct *vma)
{
	int retval;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	mutex_lock(&q->vb_lock);
	retval = CALL(q, mmap_mapper, q, vma);
	q->is_mmapped = 1;
	mutex_unlock(&q->vb_lock);

	return retval;
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
int videobuf_cgmbuf(struct videobuf_queue *q,
		    struct video_mbuf *mbuf, int count)
{
	struct v4l2_requestbuffers req;
	int rc, i;

	MAGIC_CHECK(q->int_ops->magic, MAGIC_QTYPE_OPS);

	memset(&req, 0, sizeof(req));
	req.type   = q->type;
	req.count  = count;
	req.memory = V4L2_MEMORY_MMAP;
	rc = videobuf_reqbufs(q, &req);
	if (rc < 0)
		return rc;

	mbuf->frames = req.count;
	mbuf->size   = 0;
	for (i = 0; i < mbuf->frames; i++) {
		mbuf->offsets[i]  = q->bufs[i]->boff;
		mbuf->size       += PAGE_ALIGN(q->bufs[i]->bsize);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(videobuf_cgmbuf);
#endif

/* --------------------------------------------------------------------- */

EXPORT_SYMBOL_GPL(videobuf_waiton);
EXPORT_SYMBOL_GPL(videobuf_iolock);

EXPORT_SYMBOL_GPL(videobuf_alloc);

EXPORT_SYMBOL_GPL(videobuf_queue_core_init);
EXPORT_SYMBOL_GPL(videobuf_queue_cancel);
EXPORT_SYMBOL_GPL(videobuf_queue_is_busy);

EXPORT_SYMBOL_GPL(videobuf_next_field);
EXPORT_SYMBOL_GPL(videobuf_reqbufs);
EXPORT_SYMBOL_GPL(videobuf_querybuf);
EXPORT_SYMBOL_GPL(videobuf_qbuf);
EXPORT_SYMBOL_GPL(videobuf_dqbuf);
EXPORT_SYMBOL_GPL(videobuf_streamon);
EXPORT_SYMBOL_GPL(videobuf_streamoff);

EXPORT_SYMBOL_GPL(videobuf_read_start);
EXPORT_SYMBOL_GPL(videobuf_read_stop);
EXPORT_SYMBOL_GPL(videobuf_stop);
EXPORT_SYMBOL_GPL(videobuf_read_stream);
EXPORT_SYMBOL_GPL(videobuf_read_one);
EXPORT_SYMBOL_GPL(videobuf_poll_stream);

EXPORT_SYMBOL_GPL(__videobuf_mmap_setup);
EXPORT_SYMBOL_GPL(videobuf_mmap_setup);
EXPORT_SYMBOL_GPL(videobuf_mmap_free);
EXPORT_SYMBOL_GPL(videobuf_mmap_mapper);
