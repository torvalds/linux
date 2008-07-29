/*
 *  cx18 buffer queues
 *
 *  Derived from ivtv-queue.h
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

#define CX18_DMA_UNMAPPED	((u32) -1)

/* cx18_buffer utility functions */

static inline void cx18_buf_sync_for_cpu(struct cx18_stream *s,
	struct cx18_buffer *buf)
{
	pci_dma_sync_single_for_cpu(s->cx->dev, buf->dma_handle,
				s->buf_size, s->dma);
}

static inline void cx18_buf_sync_for_device(struct cx18_stream *s,
	struct cx18_buffer *buf)
{
	pci_dma_sync_single_for_device(s->cx->dev, buf->dma_handle,
				s->buf_size, s->dma);
}

void cx18_buf_swap(struct cx18_buffer *buf);

/* cx18_queue utility functions */
void cx18_queue_init(struct cx18_queue *q);
void cx18_enqueue(struct cx18_stream *s, struct cx18_buffer *buf,
	struct cx18_queue *q);
struct cx18_buffer *cx18_dequeue(struct cx18_stream *s, struct cx18_queue *q);
struct cx18_buffer *cx18_queue_find_buf(struct cx18_stream *s, u32 id,
	u32 bytesused);
void cx18_flush_queues(struct cx18_stream *s);

/* cx18_stream utility functions */
int cx18_stream_alloc(struct cx18_stream *s);
void cx18_stream_free(struct cx18_stream *s);
