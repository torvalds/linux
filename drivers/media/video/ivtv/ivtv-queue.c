/*
    buffer queues.
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2004  Chris Kennedy <c@groovy.org>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ivtv-driver.h"
#include "ivtv-queue.h"

int ivtv_buf_copy_from_user(struct ivtv_stream *s, struct ivtv_buffer *buf, const char __user *src, int copybytes)
{
	if (s->buf_size - buf->bytesused < copybytes)
		copybytes = s->buf_size - buf->bytesused;
	if (copy_from_user(buf->buf + buf->bytesused, src, copybytes)) {
		return -EFAULT;
	}
	buf->bytesused += copybytes;
	return copybytes;
}

void ivtv_buf_swap(struct ivtv_buffer *buf)
{
	int i;

	for (i = 0; i < buf->bytesused; i += 4)
		swab32s((u32 *)(buf->buf + i));
}

void ivtv_queue_init(struct ivtv_queue *q)
{
	INIT_LIST_HEAD(&q->list);
	q->buffers = 0;
	q->length = 0;
	q->bytesused = 0;
}

void ivtv_enqueue(struct ivtv_stream *s, struct ivtv_buffer *buf, struct ivtv_queue *q)
{
	unsigned long flags;

	/* clear the buffer if it is going to be enqueued to the free queue */
	if (q == &s->q_free) {
		buf->bytesused = 0;
		buf->readpos = 0;
		buf->b_flags = 0;
		buf->dma_xfer_cnt = 0;
	}
	spin_lock_irqsave(&s->qlock, flags);
	list_add_tail(&buf->list, &q->list);
	q->buffers++;
	q->length += s->buf_size;
	q->bytesused += buf->bytesused - buf->readpos;
	spin_unlock_irqrestore(&s->qlock, flags);
}

struct ivtv_buffer *ivtv_dequeue(struct ivtv_stream *s, struct ivtv_queue *q)
{
	struct ivtv_buffer *buf = NULL;
	unsigned long flags;

	spin_lock_irqsave(&s->qlock, flags);
	if (!list_empty(&q->list)) {
		buf = list_entry(q->list.next, struct ivtv_buffer, list);
		list_del_init(q->list.next);
		q->buffers--;
		q->length -= s->buf_size;
		q->bytesused -= buf->bytesused - buf->readpos;
	}
	spin_unlock_irqrestore(&s->qlock, flags);
	return buf;
}

static void ivtv_queue_move_buf(struct ivtv_stream *s, struct ivtv_queue *from,
		struct ivtv_queue *to, int clear)
{
	struct ivtv_buffer *buf = list_entry(from->list.next, struct ivtv_buffer, list);

	list_move_tail(from->list.next, &to->list);
	from->buffers--;
	from->length -= s->buf_size;
	from->bytesused -= buf->bytesused - buf->readpos;
	/* special handling for q_free */
	if (clear)
		buf->bytesused = buf->readpos = buf->b_flags = buf->dma_xfer_cnt = 0;
	to->buffers++;
	to->length += s->buf_size;
	to->bytesused += buf->bytesused - buf->readpos;
}

/* Move 'needed_bytes' worth of buffers from queue 'from' into queue 'to'.
   If 'needed_bytes' == 0, then move all buffers from 'from' into 'to'.
   If 'steal' != NULL, then buffers may also taken from that queue if
   needed, but only if 'from' is the free queue.

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
int ivtv_queue_move(struct ivtv_stream *s, struct ivtv_queue *from, struct ivtv_queue *steal,
		    struct ivtv_queue *to, int needed_bytes)
{
	unsigned long flags;
	int rc = 0;
	int from_free = from == &s->q_free;
	int to_free = to == &s->q_free;
	int bytes_available, bytes_steal;

	spin_lock_irqsave(&s->qlock, flags);
	if (needed_bytes == 0) {
		from_free = 1;
		needed_bytes = from->length;
	}

	bytes_available = from_free ? from->length : from->bytesused;
	bytes_steal = (from_free && steal) ? steal->length : 0;

	if (bytes_available + bytes_steal < needed_bytes) {
		spin_unlock_irqrestore(&s->qlock, flags);
		return -ENOMEM;
	}
	while (bytes_available < needed_bytes) {
		struct ivtv_buffer *buf = list_entry(steal->list.prev, struct ivtv_buffer, list);
		u16 dma_xfer_cnt = buf->dma_xfer_cnt;

		/* move buffers from the tail of the 'steal' queue to the tail of the
		   'from' queue. Always copy all the buffers with the same dma_xfer_cnt
		   value, this ensures that you do not end up with partial frame data
		   if one frame is stored in multiple buffers. */
		while (dma_xfer_cnt == buf->dma_xfer_cnt) {
			list_move_tail(steal->list.prev, &from->list);
			rc++;
			steal->buffers--;
			steal->length -= s->buf_size;
			steal->bytesused -= buf->bytesused - buf->readpos;
			buf->bytesused = buf->readpos = buf->b_flags = buf->dma_xfer_cnt = 0;
			from->buffers++;
			from->length += s->buf_size;
			bytes_available += s->buf_size;
			if (list_empty(&steal->list))
				break;
			buf = list_entry(steal->list.prev, struct ivtv_buffer, list);
		}
	}
	if (from_free) {
		u32 old_length = to->length;

		while (to->length - old_length < needed_bytes) {
			ivtv_queue_move_buf(s, from, to, 1);
		}
	}
	else {
		u32 old_bytesused = to->bytesused;

		while (to->bytesused - old_bytesused < needed_bytes) {
			ivtv_queue_move_buf(s, from, to, to_free);
		}
	}
	spin_unlock_irqrestore(&s->qlock, flags);
	return rc;
}

void ivtv_flush_queues(struct ivtv_stream *s)
{
	ivtv_queue_move(s, &s->q_io, NULL, &s->q_free, 0);
	ivtv_queue_move(s, &s->q_full, NULL, &s->q_free, 0);
	ivtv_queue_move(s, &s->q_dma, NULL, &s->q_free, 0);
	ivtv_queue_move(s, &s->q_predma, NULL, &s->q_free, 0);
}

int ivtv_stream_alloc(struct ivtv_stream *s)
{
	struct ivtv *itv = s->itv;
	int SGsize = sizeof(struct ivtv_sg_host_element) * s->buffers;
	int i;

	if (s->buffers == 0)
		return 0;

	IVTV_DEBUG_INFO("Allocate %s%s stream: %d x %d buffers (%dkB total)\n",
		s->dma != PCI_DMA_NONE ? "DMA " : "",
		s->name, s->buffers, s->buf_size, s->buffers * s->buf_size / 1024);

	s->sg_pending = kzalloc(SGsize, GFP_KERNEL|__GFP_NOWARN);
	if (s->sg_pending == NULL) {
		IVTV_ERR("Could not allocate sg_pending for %s stream\n", s->name);
		return -ENOMEM;
	}
	s->sg_pending_size = 0;

	s->sg_processing = kzalloc(SGsize, GFP_KERNEL|__GFP_NOWARN);
	if (s->sg_processing == NULL) {
		IVTV_ERR("Could not allocate sg_processing for %s stream\n", s->name);
		kfree(s->sg_pending);
		s->sg_pending = NULL;
		return -ENOMEM;
	}
	s->sg_processing_size = 0;

	s->sg_dma = kzalloc(sizeof(struct ivtv_sg_element),
					GFP_KERNEL|__GFP_NOWARN);
	if (s->sg_dma == NULL) {
		IVTV_ERR("Could not allocate sg_dma for %s stream\n", s->name);
		kfree(s->sg_pending);
		s->sg_pending = NULL;
		kfree(s->sg_processing);
		s->sg_processing = NULL;
		return -ENOMEM;
	}
	if (ivtv_might_use_dma(s)) {
		s->sg_handle = pci_map_single(itv->pdev, s->sg_dma, sizeof(struct ivtv_sg_element), s->dma);
		ivtv_stream_sync_for_cpu(s);
	}

	/* allocate stream buffers. Initially all buffers are in q_free. */
	for (i = 0; i < s->buffers; i++) {
		struct ivtv_buffer *buf = kzalloc(sizeof(struct ivtv_buffer),
						GFP_KERNEL|__GFP_NOWARN);

		if (buf == NULL)
			break;
		buf->buf = kmalloc(s->buf_size + 256, GFP_KERNEL|__GFP_NOWARN);
		if (buf->buf == NULL) {
			kfree(buf);
			break;
		}
		INIT_LIST_HEAD(&buf->list);
		if (ivtv_might_use_dma(s)) {
			buf->dma_handle = pci_map_single(s->itv->pdev,
				buf->buf, s->buf_size + 256, s->dma);
			ivtv_buf_sync_for_cpu(s, buf);
		}
		ivtv_enqueue(s, buf, &s->q_free);
	}
	if (i == s->buffers)
		return 0;
	IVTV_ERR("Couldn't allocate buffers for %s stream\n", s->name);
	ivtv_stream_free(s);
	return -ENOMEM;
}

void ivtv_stream_free(struct ivtv_stream *s)
{
	struct ivtv_buffer *buf;

	/* move all buffers to q_free */
	ivtv_flush_queues(s);

	/* empty q_free */
	while ((buf = ivtv_dequeue(s, &s->q_free))) {
		if (ivtv_might_use_dma(s))
			pci_unmap_single(s->itv->pdev, buf->dma_handle,
				s->buf_size + 256, s->dma);
		kfree(buf->buf);
		kfree(buf);
	}

	/* Free SG Array/Lists */
	if (s->sg_dma != NULL) {
		if (s->sg_handle != IVTV_DMA_UNMAPPED) {
			pci_unmap_single(s->itv->pdev, s->sg_handle,
				 sizeof(struct ivtv_sg_element), PCI_DMA_TODEVICE);
			s->sg_handle = IVTV_DMA_UNMAPPED;
		}
		kfree(s->sg_pending);
		kfree(s->sg_processing);
		kfree(s->sg_dma);
		s->sg_pending = NULL;
		s->sg_processing = NULL;
		s->sg_dma = NULL;
		s->sg_pending_size = 0;
		s->sg_processing_size = 0;
	}
}
