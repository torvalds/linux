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

#define IVTV_DMA_UNMAPPED	((u32) -1)

/* ivtv_buffer utility functions */
static inline void ivtv_buf_sync_for_cpu(struct ivtv_stream *s, struct ivtv_buffer *buf)
{
	if (s->dma != PCI_DMA_NONE)
		pci_dma_sync_single_for_cpu(s->itv->dev, buf->dma_handle,
				s->buf_size + 256, s->dma);
}

static inline void ivtv_buf_sync_for_device(struct ivtv_stream *s, struct ivtv_buffer *buf)
{
	if (s->dma != PCI_DMA_NONE)
		pci_dma_sync_single_for_device(s->itv->dev, buf->dma_handle,
				s->buf_size + 256, s->dma);
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
	pci_dma_sync_single_for_cpu(s->itv->dev, s->SG_handle,
		sizeof(struct ivtv_SG_element) * s->buffers, PCI_DMA_TODEVICE);
}

static inline void ivtv_stream_sync_for_device(struct ivtv_stream *s)
{
	pci_dma_sync_single_for_device(s->itv->dev, s->SG_handle,
		sizeof(struct ivtv_SG_element) * s->buffers, PCI_DMA_TODEVICE);
}
