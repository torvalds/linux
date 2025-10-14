// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  cx18 System Control Block initialization
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@kernel.org>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 */

#include "cx18-driver.h"
#include "cx18-io.h"
#include "cx18-scb.h"

void cx18_init_scb(struct cx18 *cx)
{
	cx18_setup_page(cx, SCB_OFFSET);
	cx18_memset_io(cx, cx->scb, 0, 0x10000);

	cx18_writel(cx, IRQ_APU_TO_CPU,     &cx->scb->apu2cpu_irq);
	cx18_writel(cx, IRQ_CPU_TO_APU_ACK, &cx->scb->cpu2apu_irq_ack);
	cx18_writel(cx, IRQ_HPU_TO_CPU,     &cx->scb->hpu2cpu_irq);
	cx18_writel(cx, IRQ_CPU_TO_HPU_ACK, &cx->scb->cpu2hpu_irq_ack);
	cx18_writel(cx, IRQ_PPU_TO_CPU,     &cx->scb->ppu2cpu_irq);
	cx18_writel(cx, IRQ_CPU_TO_PPU_ACK, &cx->scb->cpu2ppu_irq_ack);
	cx18_writel(cx, IRQ_EPU_TO_CPU,     &cx->scb->epu2cpu_irq);
	cx18_writel(cx, IRQ_CPU_TO_EPU_ACK, &cx->scb->cpu2epu_irq_ack);

	cx18_writel(cx, IRQ_CPU_TO_APU,     &cx->scb->cpu2apu_irq);
	cx18_writel(cx, IRQ_APU_TO_CPU_ACK, &cx->scb->apu2cpu_irq_ack);
	cx18_writel(cx, IRQ_HPU_TO_APU,     &cx->scb->hpu2apu_irq);
	cx18_writel(cx, IRQ_APU_TO_HPU_ACK, &cx->scb->apu2hpu_irq_ack);
	cx18_writel(cx, IRQ_PPU_TO_APU,     &cx->scb->ppu2apu_irq);
	cx18_writel(cx, IRQ_APU_TO_PPU_ACK, &cx->scb->apu2ppu_irq_ack);
	cx18_writel(cx, IRQ_EPU_TO_APU,     &cx->scb->epu2apu_irq);
	cx18_writel(cx, IRQ_APU_TO_EPU_ACK, &cx->scb->apu2epu_irq_ack);

	cx18_writel(cx, IRQ_CPU_TO_HPU,     &cx->scb->cpu2hpu_irq);
	cx18_writel(cx, IRQ_HPU_TO_CPU_ACK, &cx->scb->hpu2cpu_irq_ack);
	cx18_writel(cx, IRQ_APU_TO_HPU,     &cx->scb->apu2hpu_irq);
	cx18_writel(cx, IRQ_HPU_TO_APU_ACK, &cx->scb->hpu2apu_irq_ack);
	cx18_writel(cx, IRQ_PPU_TO_HPU,     &cx->scb->ppu2hpu_irq);
	cx18_writel(cx, IRQ_HPU_TO_PPU_ACK, &cx->scb->hpu2ppu_irq_ack);
	cx18_writel(cx, IRQ_EPU_TO_HPU,     &cx->scb->epu2hpu_irq);
	cx18_writel(cx, IRQ_HPU_TO_EPU_ACK, &cx->scb->hpu2epu_irq_ack);

	cx18_writel(cx, IRQ_CPU_TO_PPU,     &cx->scb->cpu2ppu_irq);
	cx18_writel(cx, IRQ_PPU_TO_CPU_ACK, &cx->scb->ppu2cpu_irq_ack);
	cx18_writel(cx, IRQ_APU_TO_PPU,     &cx->scb->apu2ppu_irq);
	cx18_writel(cx, IRQ_PPU_TO_APU_ACK, &cx->scb->ppu2apu_irq_ack);
	cx18_writel(cx, IRQ_HPU_TO_PPU,     &cx->scb->hpu2ppu_irq);
	cx18_writel(cx, IRQ_PPU_TO_HPU_ACK, &cx->scb->ppu2hpu_irq_ack);
	cx18_writel(cx, IRQ_EPU_TO_PPU,     &cx->scb->epu2ppu_irq);
	cx18_writel(cx, IRQ_PPU_TO_EPU_ACK, &cx->scb->ppu2epu_irq_ack);

	cx18_writel(cx, IRQ_CPU_TO_EPU,     &cx->scb->cpu2epu_irq);
	cx18_writel(cx, IRQ_EPU_TO_CPU_ACK, &cx->scb->epu2cpu_irq_ack);
	cx18_writel(cx, IRQ_APU_TO_EPU,     &cx->scb->apu2epu_irq);
	cx18_writel(cx, IRQ_EPU_TO_APU_ACK, &cx->scb->epu2apu_irq_ack);
	cx18_writel(cx, IRQ_HPU_TO_EPU,     &cx->scb->hpu2epu_irq);
	cx18_writel(cx, IRQ_EPU_TO_HPU_ACK, &cx->scb->epu2hpu_irq_ack);
	cx18_writel(cx, IRQ_PPU_TO_EPU,     &cx->scb->ppu2epu_irq);
	cx18_writel(cx, IRQ_EPU_TO_PPU_ACK, &cx->scb->epu2ppu_irq_ack);

	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, apu2cpu_mb),
			&cx->scb->apu2cpu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, hpu2cpu_mb),
			&cx->scb->hpu2cpu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, ppu2cpu_mb),
			&cx->scb->ppu2cpu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, epu2cpu_mb),
			&cx->scb->epu2cpu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, cpu2apu_mb),
			&cx->scb->cpu2apu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, hpu2apu_mb),
			&cx->scb->hpu2apu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, ppu2apu_mb),
			&cx->scb->ppu2apu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, epu2apu_mb),
			&cx->scb->epu2apu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, cpu2hpu_mb),
			&cx->scb->cpu2hpu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, apu2hpu_mb),
			&cx->scb->apu2hpu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, ppu2hpu_mb),
			&cx->scb->ppu2hpu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, epu2hpu_mb),
			&cx->scb->epu2hpu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, cpu2ppu_mb),
			&cx->scb->cpu2ppu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, apu2ppu_mb),
			&cx->scb->apu2ppu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, hpu2ppu_mb),
			&cx->scb->hpu2ppu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, epu2ppu_mb),
			&cx->scb->epu2ppu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, cpu2epu_mb),
			&cx->scb->cpu2epu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, apu2epu_mb),
			&cx->scb->apu2epu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, hpu2epu_mb),
			&cx->scb->hpu2epu_mb_offset);
	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, ppu2epu_mb),
			&cx->scb->ppu2epu_mb_offset);

	cx18_writel(cx, SCB_OFFSET + offsetof(struct cx18_scb, cpu_state),
			&cx->scb->ipc_offset);

	cx18_writel(cx, 1, &cx->scb->epu_state);
}
