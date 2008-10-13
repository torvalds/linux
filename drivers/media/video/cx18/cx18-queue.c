/*
 *  cx18 buffer queues
 *
 *  Derived from ivtv-queue.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#include "cx18-driver.h"
#include "cx18-streams.h"
#include "cx18-queue.h"
#include "cx18-scb.h"

void cx18_buf_swap(struct cx18_buffer *buf)
{
	int i;

	for (i = 0; i < buf->bytesused; i += 4)
		swab32s((u32 *)(buf->buf + i));
}

void cx18_queue_init(struct cx18_queue *q)
{
	INIT_LIST_HEAD(&q->list);
	q->buffers = 0;
	q->length = 0;
	q->bytesused = 0;
}

void cx18_enqueue(struct cx18_stream *s, struct cx18_buffer *buf,
		struct cx18_queue *q)
{
	unsigned long flags = 0;

	/* clear the buffer if it is going to be enqueued to the free queue */
	if (q == &s->q_free) {
		buf->bytesused = 0;
		buf->readpos = 0;
		buf->b_flags = 0;
	}
	spin_lock_irqsave(&s->qlock, flags);
	list_add_tail(&buf->list, &q->list);
	q->buffers++;
	q->length += s->buf_size;
	q->bytesused += buf->bytesused - buf->readpos;
	spin_unlock_irqrestore(&s->qlock, flags);
}

struct cx18_buffer *cx18_dequeue(struct cx18_stream *s, struct cx18_queue *q)
{
	struct cx18_buffer *buf = NULL;
	unsigned long flags = 0;

	spin_lock_irqsave(&s->qlock, flags);
	if (!list_empty(&q->list)) {
		buf = list_entry(q->list.next, struct cx18_buffer, list);
		list_del_init(q->list.next);
		q->buffers--;
		q->length -= s->buf_size;
		q->bytesused -= buf->bytesused - buf->readpos;
	}
	spin_unlock_irqrestore(&s->qlock, flags);
	return buf;
}

struct cx18_buffer *cx18_queue_get_buf_irq(struct cx18_stream *s, u32 id,
	u32 bytesused)
{
	struct cx18 *cx = s->cx;
	struct list_head *p;

	spin_lock(&s->qlock);
	list_for_each(p, &s->q_free.list) {
		struct cx18_buffer *buf =
			list_entry(p, struct cx18_buffer, list);

		if (buf->id != id)
			continue;
		buf->bytesused = bytesused;
		/* the transport buffers are handled differently,
		   they are not moved to the full queue */
		if (s->type != CX18_ENC_STREAM_TYPE_TS) {
			s->q_free.buffers--;
			s->q_free.length -= s->buf_size;
			s->q_full.buffers++;
			s->q_full.length += s->buf_size;
			s->q_full.bytesused += buf->bytesused;
			list_move_tail(&buf->list, &s->q_full.list);
		}
		spin_unlock(&s->qlock);
		return buf;
	}
	spin_unlock(&s->qlock);
	CX18_ERR("Cannot find buffer %d for stream %s\n", id, s->name);
	return NULL;
}

/* Move all buffers of a queue to q_free, while flushing the buffers */
static void cx18_queue_flush(struct cx18_stream *s, struct cx18_queue *q)
{
	unsigned long flags;
	struct cx18_buffer *buf;

	if (q == &s->q_free)
		return;

	spin_lock_irqsave(&s->qlock, flags);
	while (!list_empty(&q->list)) {
		buf = list_entry(q->list.next, struct cx18_buffer, list);
		list_move_tail(q->list.next, &s->q_free.list);
		buf->bytesused = buf->readpos = buf->b_flags = 0;
		s->q_free.buffers++;
		s->q_free.length += s->buf_size;
	}
	cx18_queue_init(q);
	spin_unlock_irqrestore(&s->qlock, flags);
}

void cx18_flush_queues(struct cx18_stream *s)
{
	cx18_queue_flush(s, &s->q_io);
	cx18_queue_flush(s, &s->q_full);
}

int cx18_stream_alloc(struct cx18_stream *s)
{
	struct cx18 *cx = s->cx;
	int i;

	if (s->buffers == 0)
		return 0;

	CX18_DEBUG_INFO("Allocate %s stream: %d x %d buffers (%dkB total)\n",
		s->name, s->buffers, s->buf_size,
		s->buffers * s->buf_size / 1024);

	if (((char __iomem *)&cx->scb->cpu_mdl[cx->mdl_offset + s->buffers] -
				(char __iomem *)cx->scb) > SCB_RESERVED_SIZE) {
		unsigned bufsz = (((char __iomem *)cx->scb) + SCB_RESERVED_SIZE -
					((char __iomem *)cx->scb->cpu_mdl));

		CX18_ERR("Too many buffers, cannot fit in SCB area\n");
		CX18_ERR("Max buffers = %zd\n",
			bufsz / sizeof(struct cx18_mdl));
		return -ENOMEM;
	}

	s->mdl_offset = cx->mdl_offset;

	/* allocate stream buffers. Initially all buffers are in q_free. */
	for (i = 0; i < s->buffers; i++) {
		struct cx18_buffer *buf = kzalloc(sizeof(struct cx18_buffer),
						GFP_KERNEL|__GFP_NOWARN);

		if (buf == NULL)
			break;
		buf->buf = kmalloc(s->buf_size, GFP_KERNEL|__GFP_NOWARN);
		if (buf->buf == NULL) {
			kfree(buf);
			break;
		}
		buf->id = cx->buffer_id++;
		INIT_LIST_HEAD(&buf->list);
		buf->dma_handle = pci_map_single(s->cx->dev,
				buf->buf, s->buf_size, s->dma);
		cx18_buf_sync_for_cpu(s, buf);
		cx18_enqueue(s, buf, &s->q_free);
	}
	if (i == s->buffers) {
		cx->mdl_offset += s->buffers;
		return 0;
	}
	CX18_ERR("Couldn't allocate buffers for %s stream\n", s->name);
	cx18_stream_free(s);
	return -ENOMEM;
}

void cx18_stream_free(struct cx18_stream *s)
{
	struct cx18_buffer *buf;

	/* move all buffers to q_free */
	cx18_flush_queues(s);

	/* empty q_free */
	while ((buf = cx18_dequeue(s, &s->q_free))) {
		pci_unmap_single(s->cx->dev, buf->dma_handle,
				s->buf_size, s->dma);
		kfree(buf->buf);
		kfree(buf);
	}
}
