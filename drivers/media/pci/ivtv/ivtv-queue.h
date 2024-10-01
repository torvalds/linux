/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    buffer queues.
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2004  Chris Kennedy <c@groovy.org>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

 */

#ifndef IVTV_QUEUE_H
#define IVTV_QUEUE_H

#define IVTV_DMA_UNMAPPED	((u32) -1)
#define SLICED_VBI_PIO 0

/* ivtv_buffer utility functions */

static inline int ivtv_might_use_pio(struct ivtv_stream *s)
{
	return s->dma == DMA_NONE || (SLICED_VBI_PIO && s->type == IVTV_ENC_STREAM_TYPE_VBI);
}

static inline int ivtv_use_pio(struct ivtv_stream *s)
{
	struct ivtv *itv = s->itv;

	return s->dma == DMA_NONE ||
	    (SLICED_VBI_PIO && s->type == IVTV_ENC_STREAM_TYPE_VBI && itv->vbi.sliced_in->service_set);
}

static inline int ivtv_might_use_dma(struct ivtv_stream *s)
{
	return s->dma != DMA_NONE;
}

static inline int ivtv_use_dma(struct ivtv_stream *s)
{
	return !ivtv_use_pio(s);
}

static inline void ivtv_buf_sync_for_cpu(struct ivtv_stream *s, struct ivtv_buffer *buf)
{
	if (ivtv_use_dma(s))
		dma_sync_single_for_cpu(&s->itv->pdev->dev, buf->dma_handle,
					s->buf_size + 256, s->dma);
}

static inline void ivtv_buf_sync_for_device(struct ivtv_stream *s, struct ivtv_buffer *buf)
{
	if (ivtv_use_dma(s))
		dma_sync_single_for_device(&s->itv->pdev->dev,
					   buf->dma_handle, s->buf_size + 256,
					   s->dma);
}

int ivtv_buf_copy_from_user(struct ivtv_stream *s, struct ivtv_buffer *buf, const char __user *src, int copybytes);
void ivtv_buf_swap(struct ivtv_buffer *buf);

/* ivtv_queue utility functions */
void ivtv_queue_init(struct ivtv_queue *q);
void ivtv_enqueue(struct ivtv_stream *s, struct ivtv_buffer *buf, struct ivtv_queue *q);
struct ivtv_buffer *ivtv_dequeue(struct ivtv_stream *s, struct ivtv_queue *q);
int ivtv_queue_move(struct ivtv_stream *s, struct ivtv_queue *from, struct ivtv_queue *steal,
		    struct ivtv_queue *to, int needed_bytes);
void ivtv_flush_queues(struct ivtv_stream *s);

/* ivtv_stream utility functions */
int ivtv_stream_alloc(struct ivtv_stream *s);
void ivtv_stream_free(struct ivtv_stream *s);

static inline void ivtv_stream_sync_for_cpu(struct ivtv_stream *s)
{
	if (ivtv_use_dma(s))
		dma_sync_single_for_cpu(&s->itv->pdev->dev, s->sg_handle,
					sizeof(struct ivtv_sg_element),
					DMA_TO_DEVICE);
}

static inline void ivtv_stream_sync_for_device(struct ivtv_stream *s)
{
	if (ivtv_use_dma(s))
		dma_sync_single_for_device(&s->itv->pdev->dev, s->sg_handle,
					   sizeof(struct ivtv_sg_element),
					   DMA_TO_DEVICE);
}

#endif
