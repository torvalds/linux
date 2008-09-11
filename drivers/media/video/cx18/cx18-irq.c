/*
 *  cx18 interrupt handling
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
#include "cx18-firmware.h"
#include "cx18-fileops.h"
#include "cx18-queue.h"
#include "cx18-irq.h"
#include "cx18-ioctl.h"
#include "cx18-mailbox.h"
#include "cx18-vbi.h"
#include "cx18-scb.h"

#define DMA_MAGIC_COOKIE 0x000001fe

static void epu_dma_done(struct cx18 *cx, struct cx18_mailbox *mb)
{
	u32 handle = mb->args[0];
	struct cx18_stream *s = NULL;
	struct cx18_buffer *buf;
	u32 off;
	int i;
	int id;

	for (i = 0; i < CX18_MAX_STREAMS; i++) {
		s = &cx->streams[i];
		if ((handle == s->handle) && (s->dvb.enabled))
			break;
		if (s->v4l2dev && handle == s->handle)
			break;
	}
	if (i == CX18_MAX_STREAMS) {
		CX18_WARN("DMA done for unknown handle %d for stream %s\n",
			handle, s->name);
		mb->error = CXERR_NOT_OPEN;
		mb->cmd = 0;
		cx18_mb_ack(cx, mb);
		return;
	}

	off = mb->args[1];
	if (mb->args[2] != 1)
		CX18_WARN("Ack struct = %d for %s\n",
			mb->args[2], s->name);
	id = read_enc(off);
	buf = cx18_queue_get_buf_irq(s, id, read_enc(off + 4));
	CX18_DEBUG_HI_DMA("DMA DONE for %s (buffer %d)\n", s->name, id);
	if (buf) {
		cx18_buf_sync_for_cpu(s, buf);
		if (s->type == CX18_ENC_STREAM_TYPE_TS && s->dvb.enabled) {
			/* process the buffer here */
			CX18_DEBUG_HI_DMA("TS recv and sent bytesused=%d\n",
					buf->bytesused);

			dvb_dmx_swfilter(&s->dvb.demux, buf->buf,
					buf->bytesused);

			cx18_buf_sync_for_device(s, buf);
			cx18_vapi(cx, CX18_CPU_DE_SET_MDL, 5, s->handle,
			    (void __iomem *)&cx->scb->cpu_mdl[buf->id] - cx->enc_mem,
			    1, buf->id, s->buf_size);
		} else
			set_bit(CX18_F_B_NEED_BUF_SWAP, &buf->b_flags);
	} else {
		CX18_WARN("Could not find buf %d for stream %s\n",
				read_enc(off), s->name);
	}
	mb->error = 0;
	mb->cmd = 0;
	cx18_mb_ack(cx, mb);
	wake_up(&cx->dma_waitq);
	if (s->id != -1)
		wake_up(&s->waitq);
}

static void epu_debug(struct cx18 *cx, struct cx18_mailbox *mb)
{
	char str[256] = { 0 };
	char *p;

	if (mb->args[1]) {
		setup_page(mb->args[1]);
		memcpy_fromio(str, cx->enc_mem + mb->args[1], 252);
		str[252] = 0;
	}
	cx18_mb_ack(cx, mb);
	CX18_DEBUG_INFO("%x %s\n", mb->args[0], str);
	p = strchr(str, '.');
	if (!test_bit(CX18_F_I_LOADED_FW, &cx->i_flags) && p && p > str)
		CX18_INFO("FW version: %s\n", p - 1);
}

static void hpu_cmd(struct cx18 *cx, u32 sw1)
{
	struct cx18_mailbox mb;

	if (sw1 & IRQ_CPU_TO_EPU) {
		memcpy_fromio(&mb, &cx->scb->cpu2epu_mb, sizeof(mb));
		mb.error = 0;

		switch (mb.cmd) {
		case CX18_EPU_DMA_DONE:
			epu_dma_done(cx, &mb);
			break;
		case CX18_EPU_DEBUG:
			epu_debug(cx, &mb);
			break;
		default:
			CX18_WARN("Unexpected mailbox command %08x\n", mb.cmd);
			break;
		}
	}
	if (sw1 & (IRQ_APU_TO_EPU | IRQ_HPU_TO_EPU))
		CX18_WARN("Unexpected interrupt %08x\n", sw1);
}

irqreturn_t cx18_irq_handler(int irq, void *dev_id)
{
	struct cx18 *cx = (struct cx18 *)dev_id;
	u32 sw1, sw1_mask;
	u32 sw2, sw2_mask;
	u32 hw2, hw2_mask;

	spin_lock(&cx->dma_reg_lock);

	hw2_mask = read_reg(HW2_INT_MASK5_PCI);
	hw2 = read_reg(HW2_INT_CLR_STATUS) & hw2_mask;
	sw2_mask = read_reg(SW2_INT_ENABLE_PCI) | IRQ_EPU_TO_HPU_ACK;
	sw2 = read_reg(SW2_INT_STATUS) & sw2_mask;
	sw1_mask = read_reg(SW1_INT_ENABLE_PCI) | IRQ_EPU_TO_HPU;
	sw1 = read_reg(SW1_INT_STATUS) & sw1_mask;

	write_reg(sw2&sw2_mask, SW2_INT_STATUS);
	write_reg(sw1&sw1_mask, SW1_INT_STATUS);
	write_reg(hw2&hw2_mask, HW2_INT_CLR_STATUS);

	if (sw1 || sw2 || hw2)
		CX18_DEBUG_HI_IRQ("SW1: %x  SW2: %x  HW2: %x\n", sw1, sw2, hw2);

	/* To do: interrupt-based I2C handling
	if (hw2 & 0x00c00000) {
	}
	*/

	if (sw2) {
		if (sw2 & (readl(&cx->scb->cpu2hpu_irq_ack) |
			   readl(&cx->scb->cpu2epu_irq_ack)))
			wake_up(&cx->mb_cpu_waitq);
		if (sw2 & (readl(&cx->scb->apu2hpu_irq_ack) |
			   readl(&cx->scb->apu2epu_irq_ack)))
			wake_up(&cx->mb_apu_waitq);
		if (sw2 & readl(&cx->scb->epu2hpu_irq_ack))
			wake_up(&cx->mb_epu_waitq);
		if (sw2 & readl(&cx->scb->hpu2epu_irq_ack))
			wake_up(&cx->mb_hpu_waitq);
	}

	if (sw1)
		hpu_cmd(cx, sw1);
	spin_unlock(&cx->dma_reg_lock);

	return (hw2 | sw1 | sw2) ? IRQ_HANDLED : IRQ_NONE;
}
