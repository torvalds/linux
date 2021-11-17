/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  cx18 buffer queues
 *
 *  Derived from ivtv-queue.h
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 */

#define CX18_DMA_UNMAPPED	((u32) -1)

/* cx18_buffer utility functions */

static inline void cx18_buf_sync_for_cpu(struct cx18_stream *s,
	struct cx18_buffer *buf)
{
	pci_dma_sync_single_for_cpu(s->cx->pci_dev, buf->dma_handle,
				s->buf_size, s->dma);
}

static inline void cx18_buf_sync_for_device(struct cx18_stream *s,
	struct cx18_buffer *buf)
{
	pci_dma_sync_single_for_device(s->cx->pci_dev, buf->dma_handle,
				s->buf_size, s->dma);
}

void _cx18_mdl_sync_for_device(struct cx18_stream *s, struct cx18_mdl *mdl);

static inline void cx18_mdl_sync_for_device(struct cx18_stream *s,
					    struct cx18_mdl *mdl)
{
	if (list_is_singular(&mdl->buf_list))
		cx18_buf_sync_for_device(s, list_first_entry(&mdl->buf_list,
							     struct cx18_buffer,
							     list));
	else
		_cx18_mdl_sync_for_device(s, mdl);
}

void cx18_buf_swap(struct cx18_buffer *buf);
void _cx18_mdl_swap(struct cx18_mdl *mdl);

static inline void cx18_mdl_swap(struct cx18_mdl *mdl)
{
	if (list_is_singular(&mdl->buf_list))
		cx18_buf_swap(list_first_entry(&mdl->buf_list,
					       struct cx18_buffer, list));
	else
		_cx18_mdl_swap(mdl);
}

/* cx18_queue utility functions */
struct cx18_queue *_cx18_enqueue(struct cx18_stream *s, struct cx18_mdl *mdl,
				 struct cx18_queue *q, int to_front);

static inline
struct cx18_queue *cx18_enqueue(struct cx18_stream *s, struct cx18_mdl *mdl,
				struct cx18_queue *q)
{
	return _cx18_enqueue(s, mdl, q, 0); /* FIFO */
}

static inline
struct cx18_queue *cx18_push(struct cx18_stream *s, struct cx18_mdl *mdl,
			     struct cx18_queue *q)
{
	return _cx18_enqueue(s, mdl, q, 1); /* LIFO */
}

void cx18_queue_init(struct cx18_queue *q);
struct cx18_mdl *cx18_dequeue(struct cx18_stream *s, struct cx18_queue *q);
struct cx18_mdl *cx18_queue_get_mdl(struct cx18_stream *s, u32 id,
	u32 bytesused);
void cx18_flush_queues(struct cx18_stream *s);

/* queue MDL reconfiguration helpers */
void cx18_unload_queues(struct cx18_stream *s);
void cx18_load_queues(struct cx18_stream *s);

/* cx18_stream utility functions */
int cx18_stream_alloc(struct cx18_stream *s);
void cx18_stream_free(struct cx18_stream *s);
