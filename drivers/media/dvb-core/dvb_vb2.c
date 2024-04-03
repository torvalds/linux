// SPDX-License-Identifier: GPL-2.0
/*
 * dvb-vb2.c - dvb-vb2
 *
 * Copyright (C) 2015 Samsung Electronics
 *
 * Author: jh1009.sung@samsung.com
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>

#include <media/dvbdev.h>
#include <media/dvb_vb2.h>

#define DVB_V2_MAX_SIZE		(4096 * 188)

static int vb2_debug;
module_param(vb2_debug, int, 0644);

#define dprintk(level, fmt, arg...)					      \
	do {								      \
		if (vb2_debug >= level)					      \
			pr_info("vb2: %s: " fmt, __func__, ## arg); \
	} while (0)

static int _queue_setup(struct vb2_queue *vq,
			unsigned int *nbuffers, unsigned int *nplanes,
			unsigned int sizes[], struct device *alloc_devs[])
{
	struct dvb_vb2_ctx *ctx = vb2_get_drv_priv(vq);

	ctx->buf_cnt = *nbuffers;
	*nplanes = 1;
	sizes[0] = ctx->buf_siz;

	/*
	 * videobuf2-vmalloc allocator is context-less so no need to set
	 * alloc_ctxs array.
	 */

	dprintk(3, "[%s] count=%d, size=%d\n", ctx->name,
		*nbuffers, sizes[0]);

	return 0;
}

static int _buffer_prepare(struct vb2_buffer *vb)
{
	struct dvb_vb2_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	unsigned long size = ctx->buf_siz;

	if (vb2_plane_size(vb, 0) < size) {
		dprintk(1, "[%s] data will not fit into plane (%lu < %lu)\n",
			ctx->name, vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);
	dprintk(3, "[%s]\n", ctx->name);

	return 0;
}

static void _buffer_queue(struct vb2_buffer *vb)
{
	struct dvb_vb2_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct dvb_buffer *buf = container_of(vb, struct dvb_buffer, vb);
	unsigned long flags = 0;

	spin_lock_irqsave(&ctx->slock, flags);
	list_add_tail(&buf->list, &ctx->dvb_q);
	spin_unlock_irqrestore(&ctx->slock, flags);

	dprintk(3, "[%s]\n", ctx->name);
}

static int _start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct dvb_vb2_ctx *ctx = vb2_get_drv_priv(vq);

	dprintk(3, "[%s] count=%d\n", ctx->name, count);
	return 0;
}

static void _stop_streaming(struct vb2_queue *vq)
{
	struct dvb_vb2_ctx *ctx = vb2_get_drv_priv(vq);
	struct dvb_buffer *buf;
	unsigned long flags = 0;

	dprintk(3, "[%s]\n", ctx->name);

	spin_lock_irqsave(&ctx->slock, flags);
	while (!list_empty(&ctx->dvb_q)) {
		buf = list_entry(ctx->dvb_q.next,
				 struct dvb_buffer, list);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&ctx->slock, flags);
}

static void _dmxdev_lock(struct vb2_queue *vq)
{
	struct dvb_vb2_ctx *ctx = vb2_get_drv_priv(vq);

	mutex_lock(&ctx->mutex);
	dprintk(3, "[%s]\n", ctx->name);
}

static void _dmxdev_unlock(struct vb2_queue *vq)
{
	struct dvb_vb2_ctx *ctx = vb2_get_drv_priv(vq);

	if (mutex_is_locked(&ctx->mutex))
		mutex_unlock(&ctx->mutex);
	dprintk(3, "[%s]\n", ctx->name);
}

static const struct vb2_ops dvb_vb2_qops = {
	.queue_setup		= _queue_setup,
	.buf_prepare		= _buffer_prepare,
	.buf_queue		= _buffer_queue,
	.start_streaming	= _start_streaming,
	.stop_streaming		= _stop_streaming,
	.wait_prepare		= _dmxdev_unlock,
	.wait_finish		= _dmxdev_lock,
};

static void _fill_dmx_buffer(struct vb2_buffer *vb, void *pb)
{
	struct dvb_vb2_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct dmx_buffer *b = pb;

	b->index = vb->index;
	b->length = vb->planes[0].length;
	b->bytesused = vb->planes[0].bytesused;
	b->offset = vb->planes[0].m.offset;
	dprintk(3, "[%s]\n", ctx->name);
}

static int _fill_vb2_buffer(struct vb2_buffer *vb, struct vb2_plane *planes)
{
	struct dvb_vb2_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	planes[0].bytesused = 0;
	dprintk(3, "[%s]\n", ctx->name);

	return 0;
}

static const struct vb2_buf_ops dvb_vb2_buf_ops = {
	.fill_user_buffer	= _fill_dmx_buffer,
	.fill_vb2_buffer	= _fill_vb2_buffer,
};

/*
 * Videobuf operations
 */
int dvb_vb2_init(struct dvb_vb2_ctx *ctx, const char *name, int nonblocking)
{
	struct vb2_queue *q = &ctx->vb_q;
	int ret;

	memset(ctx, 0, sizeof(struct dvb_vb2_ctx));
	q->type = DVB_BUF_TYPE_CAPTURE;
	/**only mmap is supported currently*/
	q->io_modes = VB2_MMAP;
	q->drv_priv = ctx;
	q->buf_struct_size = sizeof(struct dvb_buffer);
	q->min_queued_buffers = 1;
	q->ops = &dvb_vb2_qops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->buf_ops = &dvb_vb2_buf_ops;
	ret = vb2_core_queue_init(q);
	if (ret) {
		ctx->state = DVB_VB2_STATE_NONE;
		dprintk(1, "[%s] errno=%d\n", ctx->name, ret);
		return ret;
	}

	mutex_init(&ctx->mutex);
	spin_lock_init(&ctx->slock);
	INIT_LIST_HEAD(&ctx->dvb_q);

	strscpy(ctx->name, name, DVB_VB2_NAME_MAX);
	ctx->nonblocking = nonblocking;
	ctx->state = DVB_VB2_STATE_INIT;

	dprintk(3, "[%s]\n", ctx->name);

	return 0;
}

int dvb_vb2_release(struct dvb_vb2_ctx *ctx)
{
	struct vb2_queue *q = (struct vb2_queue *)&ctx->vb_q;

	if (ctx->state & DVB_VB2_STATE_INIT)
		vb2_core_queue_release(q);

	ctx->state = DVB_VB2_STATE_NONE;
	dprintk(3, "[%s]\n", ctx->name);

	return 0;
}

int dvb_vb2_stream_on(struct dvb_vb2_ctx *ctx)
{
	struct vb2_queue *q = &ctx->vb_q;
	int ret;

	ret = vb2_core_streamon(q, q->type);
	if (ret) {
		ctx->state = DVB_VB2_STATE_NONE;
		dprintk(1, "[%s] errno=%d\n", ctx->name, ret);
		return ret;
	}
	ctx->state |= DVB_VB2_STATE_STREAMON;
	dprintk(3, "[%s]\n", ctx->name);

	return 0;
}

int dvb_vb2_stream_off(struct dvb_vb2_ctx *ctx)
{
	struct vb2_queue *q = (struct vb2_queue *)&ctx->vb_q;
	int ret;

	ctx->state &= ~DVB_VB2_STATE_STREAMON;
	ret = vb2_core_streamoff(q, q->type);
	if (ret) {
		ctx->state = DVB_VB2_STATE_NONE;
		dprintk(1, "[%s] errno=%d\n", ctx->name, ret);
		return ret;
	}
	dprintk(3, "[%s]\n", ctx->name);

	return 0;
}

int dvb_vb2_is_streaming(struct dvb_vb2_ctx *ctx)
{
	return (ctx->state & DVB_VB2_STATE_STREAMON);
}

int dvb_vb2_fill_buffer(struct dvb_vb2_ctx *ctx,
			const unsigned char *src, int len,
			enum dmx_buffer_flags *buffer_flags)
{
	unsigned long flags = 0;
	void *vbuf = NULL;
	int todo = len;
	unsigned char *psrc = (unsigned char *)src;
	int ll = 0;

	/*
	 * normal case: This func is called twice from demux driver
	 * one with valid src pointer, second time with NULL pointer
	 */
	if (!src || !len)
		return 0;
	spin_lock_irqsave(&ctx->slock, flags);
	if (buffer_flags && *buffer_flags) {
		ctx->flags |= *buffer_flags;
		*buffer_flags = 0;
	}
	while (todo) {
		if (!ctx->buf) {
			if (list_empty(&ctx->dvb_q)) {
				dprintk(3, "[%s] Buffer overflow!!!\n",
					ctx->name);
				break;
			}

			ctx->buf = list_entry(ctx->dvb_q.next,
					      struct dvb_buffer, list);
			ctx->remain = vb2_plane_size(&ctx->buf->vb, 0);
			ctx->offset = 0;
		}

		if (!dvb_vb2_is_streaming(ctx)) {
			vb2_buffer_done(&ctx->buf->vb, VB2_BUF_STATE_ERROR);
			list_del(&ctx->buf->list);
			ctx->buf = NULL;
			break;
		}

		/* Fill buffer */
		ll = min(todo, ctx->remain);
		vbuf = vb2_plane_vaddr(&ctx->buf->vb, 0);
		memcpy(vbuf + ctx->offset, psrc, ll);
		todo -= ll;
		psrc += ll;

		ctx->remain -= ll;
		ctx->offset += ll;

		if (ctx->remain == 0) {
			vb2_buffer_done(&ctx->buf->vb, VB2_BUF_STATE_DONE);
			list_del(&ctx->buf->list);
			ctx->buf = NULL;
		}
	}

	if (ctx->nonblocking && ctx->buf) {
		vb2_set_plane_payload(&ctx->buf->vb, 0, ll);
		vb2_buffer_done(&ctx->buf->vb, VB2_BUF_STATE_DONE);
		list_del(&ctx->buf->list);
		ctx->buf = NULL;
	}
	spin_unlock_irqrestore(&ctx->slock, flags);

	if (todo)
		dprintk(1, "[%s] %d bytes are dropped.\n", ctx->name, todo);
	else
		dprintk(3, "[%s]\n", ctx->name);

	dprintk(3, "[%s] %d bytes are copied\n", ctx->name, len - todo);
	return (len - todo);
}

int dvb_vb2_reqbufs(struct dvb_vb2_ctx *ctx, struct dmx_requestbuffers *req)
{
	int ret;

	/* Adjust size to a sane value */
	if (req->size > DVB_V2_MAX_SIZE)
		req->size = DVB_V2_MAX_SIZE;

	/* FIXME: round req->size to a 188 or 204 multiple */

	ctx->buf_siz = req->size;
	ctx->buf_cnt = req->count;
	ret = vb2_core_reqbufs(&ctx->vb_q, VB2_MEMORY_MMAP, 0, &req->count);
	if (ret) {
		ctx->state = DVB_VB2_STATE_NONE;
		dprintk(1, "[%s] count=%d size=%d errno=%d\n", ctx->name,
			ctx->buf_cnt, ctx->buf_siz, ret);
		return ret;
	}
	ctx->state |= DVB_VB2_STATE_REQBUFS;
	dprintk(3, "[%s] count=%d size=%d\n", ctx->name,
		ctx->buf_cnt, ctx->buf_siz);

	return 0;
}

int dvb_vb2_querybuf(struct dvb_vb2_ctx *ctx, struct dmx_buffer *b)
{
	struct vb2_queue *q = &ctx->vb_q;
	struct vb2_buffer *vb2 = vb2_get_buffer(q, b->index);

	if (!vb2) {
		dprintk(1, "[%s] invalid buffer index\n", ctx->name);
		return -EINVAL;
	}
	vb2_core_querybuf(&ctx->vb_q, vb2, b);
	dprintk(3, "[%s] index=%d\n", ctx->name, b->index);
	return 0;
}

int dvb_vb2_expbuf(struct dvb_vb2_ctx *ctx, struct dmx_exportbuffer *exp)
{
	struct vb2_queue *q = &ctx->vb_q;
	int ret;

	ret = vb2_core_expbuf(&ctx->vb_q, &exp->fd, q->type, q->bufs[exp->index],
			      0, exp->flags);
	if (ret) {
		dprintk(1, "[%s] index=%d errno=%d\n", ctx->name,
			exp->index, ret);
		return ret;
	}
	dprintk(3, "[%s] index=%d fd=%d\n", ctx->name, exp->index, exp->fd);

	return 0;
}

int dvb_vb2_qbuf(struct dvb_vb2_ctx *ctx, struct dmx_buffer *b)
{
	struct vb2_queue *q = &ctx->vb_q;
	struct vb2_buffer *vb2 = vb2_get_buffer(q, b->index);
	int ret;

	if (!vb2) {
		dprintk(1, "[%s] invalid buffer index\n", ctx->name);
		return -EINVAL;
	}
	ret = vb2_core_qbuf(&ctx->vb_q, vb2, b, NULL);
	if (ret) {
		dprintk(1, "[%s] index=%d errno=%d\n", ctx->name,
			b->index, ret);
		return ret;
	}
	dprintk(5, "[%s] index=%d\n", ctx->name, b->index);

	return 0;
}

int dvb_vb2_dqbuf(struct dvb_vb2_ctx *ctx, struct dmx_buffer *b)
{
	unsigned long flags;
	int ret;

	ret = vb2_core_dqbuf(&ctx->vb_q, &b->index, b, ctx->nonblocking);
	if (ret) {
		dprintk(1, "[%s] errno=%d\n", ctx->name, ret);
		return ret;
	}

	spin_lock_irqsave(&ctx->slock, flags);
	b->count = ctx->count++;
	b->flags = ctx->flags;
	ctx->flags = 0;
	spin_unlock_irqrestore(&ctx->slock, flags);

	dprintk(5, "[%s] index=%d, count=%d, flags=%d\n",
		ctx->name, b->index, ctx->count, b->flags);


	return 0;
}

int dvb_vb2_mmap(struct dvb_vb2_ctx *ctx, struct vm_area_struct *vma)
{
	int ret;

	ret = vb2_mmap(&ctx->vb_q, vma);
	if (ret) {
		dprintk(1, "[%s] errno=%d\n", ctx->name, ret);
		return ret;
	}
	dprintk(3, "[%s] ret=%d\n", ctx->name, ret);

	return 0;
}

__poll_t dvb_vb2_poll(struct dvb_vb2_ctx *ctx, struct file *file,
		      poll_table *wait)
{
	dprintk(3, "[%s]\n", ctx->name);
	return vb2_core_poll(&ctx->vb_q, file, wait);
}

