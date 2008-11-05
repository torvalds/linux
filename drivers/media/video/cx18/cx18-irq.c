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
#include "cx18-io.h"
#include "cx18-firmware.h"
#include "cx18-fileops.h"
#include "cx18-queue.h"
#include "cx18-irq.h"
#include "cx18-ioctl.h"
#include "cx18-mailbox.h"
#include "cx18-vbi.h"
#include "cx18-scb.h"

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
		CX18_WARN("Got DMA done notification for unknown/inactive"
			  " handle %d\n", handle);
		mb->error = CXERR_NOT_OPEN;
		mb->cmd = 0;
		cx18_mb_ack(cx, mb);
		return;
	}

	off = mb->args[1];
	if (mb->args[2] != 1)
		CX18_WARN("Ack struct = %d for %s\n",
			mb->args[2], s->name);
	id = cx18_read_enc(cx, off);
	buf = cx18_queue_get_buf_irq(s, id, cx18_read_enc(cx, off + 4));
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
				cx18_read_enc(cx, off), s->name);
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
		cx18_setup_page(cx, mb->args[1]);
		cx18_memcpy_fromio(cx, str, cx->enc_mem + mb->args[1], 252);
		str[252] = 0;
	}
	cx18_mb_ack(cx, mb);
	CX18_DEBUG_INFO("%x %s\n", mb->args[0], str);
	p = strchr(str, '.');
	if (!test_bit(CX18_F_I_LOADED_FW, &cx->i_flags) && p && p > str)
		CX18_INFO("FW version: %s\n", p - 1);
}

static void epu_cmd(struct cx18 *cx, u32 sw1)
{
	struct cx18_mailbox mb;

	if (sw1 & IRQ_CPU_TO_EPU) {
		cx18_memcpy_fromio(cx, &mb, &cx->scb->cpu2epu_mb, sizeof(mb));
		mb.error = 0;

		switch (mb.cmd) {
		case CX18_EPU_DMA_DONE:
			epu_dma_done(cx, &mb);
			break;
		case CX18_EPU_DEBUG:
			epu_debug(cx, &mb);
			break;
		default:
			CX18_WARN("Unknown CPU_TO_EPU mailbox command %#08x\n",
				  mb.cmd);
			break;
		}
	}

	if (sw1 & IRQ_APU_TO_EPU) {
		cx18_memcpy_fromio(cx, &mb, &cx->scb->apu2epu_mb, sizeof(mb));
		CX18_WARN("Unknown APU_TO_EPU mailbox command %#08x\n", mb.cmd);
	}

	if (sw1 & IRQ_HPU_TO_EPU) {
		cx18_memcpy_fromio(cx, &mb, &cx->scb->hpu2epu_mb, sizeof(mb));
		CX18_WARN("Unknown HPU_TO_EPU mailbox command %#08x\n", mb.cmd);
	}
}

static void xpu_ack(struct cx18 *cx, u32 sw2)
{
	if (sw2 & IRQ_CPU_TO_EPU_ACK)
		wake_up(&cx->mb_cpu_waitq);
	if (sw2 & IRQ_APU_TO_EPU_ACK)
		wake_up(&cx->mb_apu_waitq);
	if (sw2 & IRQ_HPU_TO_EPU_ACK)
		wake_up(&cx->mb_hpu_waitq);
}

irqreturn_t cx18_irq_handler(int irq, void *dev_id)
{
	struct cx18 *cx = (struct cx18 *)dev_id;
	u32 sw1, sw1_mask;
	u32 sw2, sw2_mask;
	u32 hw2, hw2_mask;

	sw1_mask = cx18_read_reg(cx, SW1_INT_ENABLE_PCI);
	sw1 = cx18_read_reg(cx, SW1_INT_STATUS) & sw1_mask;
	sw2_mask = cx18_read_reg(cx, SW2_INT_ENABLE_PCI);
	sw2 = cx18_read_reg(cx, SW2_INT_STATUS) & sw2_mask;
	hw2_mask = cx18_read_reg(cx, HW2_INT_MASK5_PCI);
	hw2 = cx18_read_reg(cx, HW2_INT_CLR_STATUS) & hw2_mask;

	if (sw1)
		cx18_write_reg_expect(cx, sw1, SW1_INT_STATUS, ~sw1, sw1);
	if (sw2)
		cx18_write_reg_expect(cx, sw2, SW2_INT_STATUS, ~sw2, sw2);
	if (hw2)
		cx18_write_reg_expect(cx, hw2, HW2_INT_CLR_STATUS, ~hw2, hw2);

	if (sw1 || sw2 || hw2)
		CX18_DEBUG_HI_IRQ("SW1: %x  SW2: %x  HW2: %x\n", sw1, sw2, hw2);

	/* To do: interrupt-based I2C handling
	if (hw2 & (HW2_I2C1_INT|HW2_I2C2_INT)) {
	}
	*/

	if (sw2)
		xpu_ack(cx, sw2);

	if (sw1)
		epu_cmd(cx, sw1);

	return (sw1 || sw2 || hw2) ? IRQ_HANDLED : IRQ_NONE;
}
