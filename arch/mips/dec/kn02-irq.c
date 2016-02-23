/*
 *	DECstation 5000/200 (KN02) Control and Status Register
 *	interrupts.
 *
 *	Copyright (c) 2002, 2003, 2005  Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/types.h>

#include <asm/dec/kn02.h>


/*
 * Bits 7:0 of the Control Register are write-only -- the
 * corresponding bits of the Status Register have a different
 * meaning.  Hence we use a cache.  It speeds up things a bit
 * as well.
 *
 * There is no default value -- it has to be initialized.
 */
u32 cached_kn02_csr;

static int kn02_irq_base;

static void unmask_kn02_irq(struct irq_data *d)
{
	volatile u32 *csr = (volatile u32 *)CKSEG1ADDR(KN02_SLOT_BASE +
						       KN02_CSR);

	cached_kn02_csr |= (1 << (d->irq - kn02_irq_base + 16));
	*csr = cached_kn02_csr;
}

static void mask_kn02_irq(struct irq_data *d)
{
	volatile u32 *csr = (volatile u32 *)CKSEG1ADDR(KN02_SLOT_BASE +
						       KN02_CSR);

	cached_kn02_csr &= ~(1 << (d->irq - kn02_irq_base + 16));
	*csr = cached_kn02_csr;
}

static void ack_kn02_irq(struct irq_data *d)
{
	mask_kn02_irq(d);
	iob();
}

static struct irq_chip kn02_irq_type = {
	.name = "KN02-CSR",
	.irq_ack = ack_kn02_irq,
	.irq_mask = mask_kn02_irq,
	.irq_mask_ack = ack_kn02_irq,
	.irq_unmask = unmask_kn02_irq,
};

void __init init_kn02_irqs(int base)
{
	volatile u32 *csr = (volatile u32 *)CKSEG1ADDR(KN02_SLOT_BASE +
						       KN02_CSR);
	int i;

	/* Mask interrupts. */
	cached_kn02_csr &= ~KN02_CSR_IOINTEN;
	*csr = cached_kn02_csr;
	iob();

	for (i = base; i < base + KN02_IRQ_LINES; i++)
		irq_set_chip_and_handler(i, &kn02_irq_type, handle_level_irq);

	kn02_irq_base = base;
}
