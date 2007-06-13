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
#include "ivtv-streams.h"
#include "ivtv-queue.h"
#include "ivtv-mailbox.h"

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

struct ivtv_buffer *ivtv_dequeue(struct ivtv_stream *s, struct ivtv_queue *q)
{
	struct ivtv_buffer *buf = NULL;
	unsigned long flags = 0;

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
		struct ivtv_queue *to, int clear, int full)
{
	struct ivtv_buffer *buf = list_entry(from->list.next, struct ivtv_buffer, list);

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
int ivtv_queue_move(struct ivtv_stream *s, struct ivtv_queue *from, struct ivtv_queue *steal,
		    struct ivtv_queue *to, int needed_bytes)
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
				rc++; 		/* keep track of 'stolen' buffers */
			ivtv_queue_move_buf(s, from, to, 1, 0);
		}
	}
	else {
		u32 old_bytesused = to->bytesused;

		while (to->bytesused - old_bytesused < needed_bytes) {
			if (list_empty(&from->list))
				from = steal;
			if (from == steal)
				rc++; 		/* keep track of 'stolen' buffers */
			ivtv_queue_move_buf(s, from, to, to_free, rc);
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
	int SGsize = sizeof(struct ivtv_SG_element) * s->buffers;
	int i;

	if (s->buffers == 0)
		return 0;

	IVTV_DEBUG_INFO("Allocate %s%s stream: %d x %d buffers (%dkB total)\n",
		s->dma != PCI_DMA_NONE ? "DMA " : "",
		s->name, s->buffers, s->buf_size, s->buffers * s->buf_size / 1024);

	if (ivtv_might_use_pio(s)) {
		s->PIOarray = (struct ivtv_SG_element *)kzalloc(SGsize, GFP_KERNEL);
		if (s->PIOarray == NULL) {
			IVTV_ERR("Could not allocate PIOarray for %s stream\n", s->name);
			return -ENOMEM;
		}
	}

	/* Allocate DMA SG Arrays */
	s->SGarray = (struct ivtv_SG_element *)kzalloc(SGsize, GFP_KERNEL);
	if (s->SGarray == NULL) {
		IVTV_ERR("Could not allocate SGarray for %s stream\n", s->name);
		if (ivtv_might_use_pio(s)) {
			kfree(s->PIOarray);
			s->PIOarray = NULL;
		}
		return -ENOMEM;
	}
	s->SG_length = 0;
	if (ivtv_might_use_dma(s)) {
		s->SG_handle = pci_map_single(itv->dev, s->SGarray, SGsize, s->dma);
		ivtv_stream_sync_for_cpu(s);
	}

	/* allocate stream buffers. Initially all buffers are in q_free. */
	for (i = 0; i < s->buffers; i++) {
		struct ivtv_buffer *buf = kzalloc(sizeof(struct ivtv_buffer), GFP_KERNEL);

		if (buf == NULL)
			break;
		buf->buf = kmalloc(s->buf_size + 256, GFP_KERNEL);
		if (buf->buf == NULL) {
			kfree(buf);
			break;
		}
		INIT_LIST_HEAD(&buf->list);
		if (ivtv_might_use_dma(s)) {
			buf->dma_handle = pci_map_single(s->itv->dev,
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
			pci_unmap_single(s->itv->dev, buf->dma_handle,
				s->buf_size + 256, s->dma);
		kfree(buf->buf);
		kfree(buf);
	}

	/* Free SG Array/Lists */
	if (s->SGarray != NULL) {
		if (s->SG_handle != IVTV_DMA_UNMAPPED) {
			pci_unmap_single(s->itv->dev, s->SG_handle,
				 sizeof(struct ivtv_SG_element) * s->buffers, PCI_DMA_TODEVICE);
			s->SG_handle = IVTV_DMA_UNMAPPED;
		}
		kfree(s->SGarray);
		kfree(s->PIOarray);
		s->PIOarray = NULL;
		s->SGarray = NULL;
		s->SG_length = 0;
	}
}
