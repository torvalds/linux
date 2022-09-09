// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	DEC I/O ASIC interrupts.
 *
 *	Copyright (c) 2002, 2003, 2013  Maciej W. Rozycki
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/types.h>

#include <asm/dec/ioasic.h>
#include <asm/dec/ioasic_addrs.h>
#include <asm/dec/ioasic_ints.h>

static int ioasic_irq_base;

static void unmask_ioasic_irq(struct irq_data *d)
{
	u32 simr;

	simr = ioasic_read(IO_REG_SIMR);
	simr |= (1 << (d->irq - ioasic_irq_base));
	ioasic_write(IO_REG_SIMR, simr);
}

static void mask_ioasic_irq(struct irq_data *d)
{
	u32 simr;

	simr = ioasic_read(IO_REG_SIMR);
	simr &= ~(1 << (d->irq - ioasic_irq_base));
	ioasic_write(IO_REG_SIMR, simr);
}

static void ack_ioasic_irq(struct irq_data *d)
{
	mask_ioasic_irq(d);
	fast_iob();
}

static struct irq_chip ioasic_irq_type = {
	.name = "IO-ASIC",
	.irq_ack = ack_ioasic_irq,
	.irq_mask = mask_ioasic_irq,
	.irq_mask_ack = ack_ioasic_irq,
	.irq_unmask = unmask_ioasic_irq,
};

static void clear_ioasic_dma_irq(struct irq_data *d)
{
	u32 sir;

	sir = ~(1 << (d->irq - ioasic_irq_base));
	ioasic_write(IO_REG_SIR, sir);
	fast_iob();
}

static struct irq_chip ioasic_dma_irq_type = {
	.name = "IO-ASIC-DMA",
	.irq_ack = clear_ioasic_dma_irq,
	.irq_mask = mask_ioasic_irq,
	.irq_unmask = unmask_ioasic_irq,
	.irq_eoi = clear_ioasic_dma_irq,
};

/*
 * I/O ASIC implements two kinds of DMA interrupts, informational and
 * error interrupts.
 *
 * The former do not stop DMA and should be cleared as soon as possible
 * so that if they retrigger before the handler has completed, usually as
 * a side effect of actions taken by the handler, then they are reissued.
 * These use the `handle_edge_irq' handler that clears the request right
 * away.
 *
 * The latter stop DMA and do not resume it until the interrupt has been
 * cleared.  This cannot be done until after a corrective action has been
 * taken and this also means they will not retrigger.  Therefore they use
 * the `handle_fasteoi_irq' handler that only clears the request on the
 * way out.  Because MIPS processor interrupt inputs, one of which the I/O
 * ASIC is cascaded to, are level-triggered it is recommended that error
 * DMA interrupt action handlers are registered with the IRQF_ONESHOT flag
 * set so that they are run with the interrupt line masked.
 *
 * This mask has `1' bits in the positions of informational interrupts.
 */
#define IO_IRQ_DMA_INFO							\
	(IO_IRQ_MASK(IO_INR_SCC0A_RXDMA) |				\
	 IO_IRQ_MASK(IO_INR_SCC1A_RXDMA) |				\
	 IO_IRQ_MASK(IO_INR_ISDN_TXDMA) |				\
	 IO_IRQ_MASK(IO_INR_ISDN_RXDMA) |				\
	 IO_IRQ_MASK(IO_INR_ASC_DMA))

void __init init_ioasic_irqs(int base)
{
	int i;

	/* Mask interrupts. */
	ioasic_write(IO_REG_SIMR, 0);
	fast_iob();

	for (i = base; i < base + IO_INR_DMA; i++)
		irq_set_chip_and_handler(i, &ioasic_irq_type,
					 handle_level_irq);
	for (; i < base + IO_IRQ_LINES; i++)
		irq_set_chip_and_handler(i, &ioasic_dma_irq_type,
					 1 << (i - base) & IO_IRQ_DMA_INFO ?
					 handle_edge_irq : handle_fasteoi_irq);

	ioasic_irq_base = base;
}
