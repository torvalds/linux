/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    interrupt handling
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2004  Chris Kennedy <c@groovy.org>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

 */

#ifndef IVTV_IRQ_H
#define IVTV_IRQ_H

#define IVTV_IRQ_ENC_START_CAP		BIT(31)
#define IVTV_IRQ_ENC_EOS		BIT(30)
#define IVTV_IRQ_ENC_VBI_CAP		BIT(29)
#define IVTV_IRQ_ENC_VIM_RST		BIT(28)
#define IVTV_IRQ_ENC_DMA_COMPLETE	BIT(27)
#define IVTV_IRQ_ENC_PIO_COMPLETE	BIT(25)
#define IVTV_IRQ_DEC_AUD_MODE_CHG	BIT(24)
#define IVTV_IRQ_DEC_DATA_REQ		BIT(22)
#define IVTV_IRQ_DEC_DMA_COMPLETE	BIT(20)
#define IVTV_IRQ_DEC_VBI_RE_INSERT	BIT(19)
#define IVTV_IRQ_DMA_ERR		BIT(18)
#define IVTV_IRQ_DMA_WRITE		BIT(17)
#define IVTV_IRQ_DMA_READ		BIT(16)
#define IVTV_IRQ_DEC_VSYNC		BIT(10)

/* IRQ Masks */
#define IVTV_IRQ_MASK_INIT (IVTV_IRQ_DMA_ERR|IVTV_IRQ_ENC_DMA_COMPLETE|\
		IVTV_IRQ_DMA_READ|IVTV_IRQ_ENC_PIO_COMPLETE)

#define IVTV_IRQ_MASK_CAPTURE (IVTV_IRQ_ENC_START_CAP | IVTV_IRQ_ENC_EOS)
#define IVTV_IRQ_MASK_DECODE  (IVTV_IRQ_DEC_DATA_REQ|IVTV_IRQ_DEC_AUD_MODE_CHG)

irqreturn_t ivtv_irq_handler(int irq, void *dev_id);

void ivtv_irq_work_handler(struct kthread_work *work);
void ivtv_dma_stream_dec_prepare(struct ivtv_stream *s, u32 offset, int lock);
void ivtv_unfinished_dma(struct timer_list *t);

#endif
