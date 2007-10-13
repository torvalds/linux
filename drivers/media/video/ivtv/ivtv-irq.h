/*
    interrupt handling
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

#ifndef IVTV_IRQ_H
#define IVTV_IRQ_H

#define IVTV_IRQ_ENC_START_CAP		(0x1 << 31)
#define IVTV_IRQ_ENC_EOS		(0x1 << 30)
#define IVTV_IRQ_ENC_VBI_CAP		(0x1 << 29)
#define IVTV_IRQ_ENC_VIM_RST		(0x1 << 28)
#define IVTV_IRQ_ENC_DMA_COMPLETE	(0x1 << 27)
#define IVTV_IRQ_ENC_PIO_COMPLETE	(0x1 << 25)
#define IVTV_IRQ_DEC_AUD_MODE_CHG	(0x1 << 24)
#define IVTV_IRQ_DEC_DATA_REQ		(0x1 << 22)
#define IVTV_IRQ_DEC_DMA_COMPLETE	(0x1 << 20)
#define IVTV_IRQ_DEC_VBI_RE_INSERT	(0x1 << 19)
#define IVTV_IRQ_DMA_ERR		(0x1 << 18)
#define IVTV_IRQ_DMA_WRITE		(0x1 << 17)
#define IVTV_IRQ_DMA_READ		(0x1 << 16)
#define IVTV_IRQ_DEC_VSYNC		(0x1 << 10)

/* IRQ Masks */
#define IVTV_IRQ_MASK_INIT (IVTV_IRQ_DMA_ERR|IVTV_IRQ_ENC_DMA_COMPLETE|\
		IVTV_IRQ_DMA_READ|IVTV_IRQ_ENC_PIO_COMPLETE)

#define IVTV_IRQ_MASK_CAPTURE (IVTV_IRQ_ENC_START_CAP | IVTV_IRQ_ENC_EOS)
#define IVTV_IRQ_MASK_DECODE  (IVTV_IRQ_DEC_DATA_REQ|IVTV_IRQ_DEC_AUD_MODE_CHG)

irqreturn_t ivtv_irq_handler(int irq, void *dev_id);

void ivtv_irq_work_handler(struct work_struct *work);
void ivtv_dma_stream_dec_prepare(struct ivtv_stream *s, u32 offset, int lock);
void ivtv_unfinished_dma(unsigned long arg);

#endif
