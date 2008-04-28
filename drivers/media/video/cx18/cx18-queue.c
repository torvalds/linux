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

int cx18_buf_copy_from_user(struct cx18_stream *s, struct cx18_buffer *buf,
		const char __user *src, int copybytes)
{
	if (s->buf_size - buf->bytesused < copybytes)
		copybytes = s->buf_size - buf->bytesused;
	if (copy_from_user(buf->buf + buf->bytesused, src, copybytes))
		return -EFAULT;
	buf->bytesused += copybytes;
	return copybytes;
}

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

struct cx18_buffer *cx18_queue_find_buf(struct cx18_stream *s, u32 id,
	u32 bytesused)
{
	struct cx18 *cx = s->cx;
	struct list_head *p;

	list_for_each(p, &s->q_free.list) {
		struct cx18_buffer *buf =
			list_entry(p, struct cx18_buffer, list);

		if (buf->id != id)
			continue;
		buf->bytesused = bytesused;
		/* the transport buffers are handled differently,
		   so there is no need to move them to the full queue */
		if (s->type == CX18_ENC_STREAM_TYPE_TS)
			return buf;
		s->q_free.buffers--;
		s->q_free.length -= s->buf_size;
		s->q_full.buffers++;
		s->q_full.length += s->buf_size;
		s->q_full.bytesused += buf->bytesused;
		list_move_tail(&buf->list, &s->q_full.list);
		return buf;
	}
	CX18_ERR("Cannot find buffer %d for stream %s\n", id, s->name);
	return NULL;
}

static void cx18_queue_move_buf(struct cx18_stream *s, struct cx18_queue *from,
		struct cx18_queue *to, int clear, int full)
{
	struct cx18_buffer *buf =
		list_entry(from->list.next, struct cx18_buffer, list);

	list_move_tail(from->list.next, &to->list);
	from->buffers--;
	from->length -= s->buf_size;
	from->bytesused -= buf->bytesused - buf->readpos;
	/* special handling for q_free */
	if (clear)
		buf->bytesused = buf->readpos = buf->b_flags = 0;
	else if (full) {
		/* special handling for stolen buffers, assume
		   all bytes are used. */
		buf->bytesused = s->buf_size;
		buf->readpos = buf->b_flags = 0;
	}
	to->buffers++;
	to->length += s->buf_size;
	to->bytesused += buf->bytesused - buf->readpos;
}

/* Move 'needed_bytes' worth of buffers from queue 'from' into queue 'to'.
   If 'needed_bytes' == 0, then move all buffers from 'from' into 'to'.
   If 'steal' != NULL, then buffers may also taken from that queue if
   needed.

   The buffer is automatically cleared if it goes to the free queue. It is
   also cleared if buffers need to be taken from the 'steal' queue and
   the 'from' queue is the free queue.

   When 'from' is q_free, then needed_bytes is compared to the total
   available buffer length, otherwise needed_bytes is compared to the
   bytesused value. For the 'steal' queue the total available buffer
   length is always used.

   -ENOMEM is returned if the buffers could not be obtained, 0 if all
   buffers where obtained from the 'from' list and if non-zero then
   the number of stolen buffers is returned. */
int cx18_queue_move(struct cx18_stream *s, struct cx18_queue *from,
	struct cx18_queue *steal, struct cx18_queue *to, int needed_bytes)
{
	unsigned long flags;
	int rc = 0;
	int from_free = from == &s->q_free;
	int to_free = to == &s->q_free;
	int bytes_available;

	spin_lock_irqsave(&s->qlock, flags);
	if (needed_bytes == 0) {
		from_free = 1;
		needed_bytes = from->length;
	}

	bytes_available = from_free ? from->length : from->bytesused;
	bytes_available += steal ? steal->length : 0;

	if (bytes_available < needed_bytes) {
		spin_unlock_irqrestore(&s->qlock, flags);
		return -ENOMEM;
	}
	if (from_free) {
		u32 old_length = to->length;

		while (to->length - old_length < needed_bytes) {
			if (list_empty(&from->list))
				from = steal;
			if (from == steal)
				rc++; 	/* keep track of 'stolen' buffers */
			cx18_queue_move_buf(s, from, to, 1, 0);
		}
	} else {
		u32 old_bytesused = to->bytesused;

		while (to->bytesused - old_bytesused < needed_bytes) {
			if (list_empty(&from->list))
				from = steal;
			if (from == steal)
				rc++; 	/* keep track of 'stolen' buffers */
			cx18_queue_move_buf(s, from, to, to_free, rc);
		}
	}
	spin_unlock_irqrestore(&s->qlock, flags);
	return rc;
}

void cx18_flush_queues(struct cx18_stream *s)
{
	cx18_queue_move(s, &s->q_io, NULL, &s->q_free, 0);
	cx18_queue_move(s, &s->q_full, NULL, &s->q_free, 0);
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

	if (((char *)&cx->scb->cpu_mdl[cx->mdl_offset + s->buffers] -
				(char *)cx->scb) > SCB_RESERVED_SIZE) {
		unsigned bufsz = (((char *)cx->scb) + SCB_RESERVED_SIZE -
					((char *)cx->scb->cpu_mdl));

		CX18_ERR("Too many buffers, cannot fit in SCB area\n");
		CX18_ERR("Max buffers = %zd\n",
			bufsz / sizeof(struct cx18_mdl));
		return -ENOMEM;
	}

	s->mdl_offset = cx->mdl_offset;

	/* allocate stream buffers. Initially all buffers are in q_free. */
	for (i = 0; i < s->buffers; i++) {
		struct cx18_buffer *buf =
			kzalloc(sizeof(struct cx18_buffer), GFP_KERNEL);

		if (buf == NULL)
			break;
		buf->buf = kmalloc(s->buf_size, GFP_KERNEL);
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
