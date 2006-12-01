/*
 *	linux/arch/mips/dec/ioasic-irq.c
 *
 *	DEC I/O ASIC interrupts.
 *
 *	Copyright (c) 2002, 2003  Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/types.h>

#include <asm/dec/ioasic.h>
#include <asm/dec/ioasic_addrs.h>
#include <asm/dec/ioasic_ints.h>


static int ioasic_irq_base;


static inline void unmask_ioasic_irq(unsigned int irq)
{
	u32 simr;

	simr = ioasic_read(IO_REG_SIMR);
	simr |= (1 << (irq - ioasic_irq_base));
	ioasic_write(IO_REG_SIMR, simr);
}

static inline void mask_ioasic_irq(unsigned int irq)
{
	u32 simr;

	simr = ioasic_read(IO_REG_SIMR);
	simr &= ~(1 << (irq - ioasic_irq_base));
	ioasic_write(IO_REG_SIMR, simr);
}

static inline void clear_ioasic_irq(unsigned int irq)
{
	u32 sir;

	sir = ~(1 << (irq - ioasic_irq_base));
	ioasic_write(IO_REG_SIR, sir);
}

static inline void ack_ioasic_irq(unsigned int irq)
{
	mask_ioasic_irq(irq);
	fast_iob();
}

static inline void end_ioasic_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		unmask_ioasic_irq(irq);
}

static struct irq_chip ioasic_irq_type = {
	.typename = "IO-ASIC",
	.ack = ack_ioasic_irq,
	.mask = mask_ioasic_irq,
	.mask_ack = ack_ioasic_irq,
	.unmask = unmask_ioasic_irq,
	.end = end_ioasic_irq,
};


#define unmask_ioasic_dma_irq unmask_ioasic_irq

#define mask_ioasic_dma_irq mask_ioasic_irq

#define ack_ioasic_dma_irq ack_ioasic_irq

static inline void end_ioasic_dma_irq(unsigned int irq)
{
	clear_ioasic_irq(irq);
	fast_iob();
	end_ioasic_irq(irq);
}

static struct irq_chip ioasic_dma_irq_type = {
	.typename = "IO-ASIC-DMA",
	.ack = ack_ioasic_dma_irq,
	.mask = mask_ioasic_dma_irq,
	.mask_ack = ack_ioasic_dma_irq,
	.unmask = unmask_ioasic_dma_irq,
	.end = end_ioasic_dma_irq,
};


void __init init_ioasic_irqs(int base)
{
	int i;

	/* Mask interrupts. */
	ioasic_write(IO_REG_SIMR, 0);
	fast_iob();

	for (i = base; i < base + IO_INR_DMA; i++)
		set_irq_chip_and_handler(i, &ioasic_irq_type,
					 handle_level_irq);
	for (; i < base + IO_IRQ_LINES; i++)
		set_irq_chip(i, &ioasic_dma_irq_type);

	ioasic_irq_base = base;
}
