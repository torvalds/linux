/*
 * intc-simr.c
 *
 * (C) Copyright 2009, Greg Ungerer <gerg@snapgear.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/traps.h>

static void intc_irq_mask(unsigned int irq)
{
	if (irq >= MCFINT_VECBASE) {
		if (irq < MCFINT_VECBASE + 64)
			__raw_writeb(irq - MCFINT_VECBASE, MCFINTC0_SIMR);
		else if ((irq < MCFINT_VECBASE + 128) && MCFINTC1_SIMR)
			__raw_writeb(irq - MCFINT_VECBASE - 64, MCFINTC1_SIMR);
	}
}

static void intc_irq_unmask(unsigned int irq)
{
	if (irq >= MCFINT_VECBASE) {
		if (irq < MCFINT_VECBASE + 64)
			__raw_writeb(irq - MCFINT_VECBASE, MCFINTC0_CIMR);
		else if ((irq < MCFINT_VECBASE + 128) && MCFINTC1_CIMR)
			__raw_writeb(irq - MCFINT_VECBASE - 64, MCFINTC1_CIMR);
	}
}

static int intc_irq_set_type(unsigned int irq, unsigned int type)
{
	if (irq >= MCFINT_VECBASE) {
		if (irq < MCFINT_VECBASE + 64)
			__raw_writeb(5, MCFINTC0_ICR0 + irq - MCFINT_VECBASE);
		else if ((irq < MCFINT_VECBASE) && MCFINTC1_ICR0)
			__raw_writeb(5, MCFINTC1_ICR0 + irq - MCFINT_VECBASE - 64);
	}
	return 0;
}

static struct irq_chip intc_irq_chip = {
	.name		= "CF-INTC",
	.mask		= intc_irq_mask,
	.unmask		= intc_irq_unmask,
	.set_type	= intc_irq_set_type,
};

void __init init_IRQ(void)
{
	int irq;

	init_vectors();

	/* Mask all interrupt sources */
	__raw_writeb(0xff, MCFINTC0_SIMR);
	if (MCFINTC1_SIMR)
		__raw_writeb(0xff, MCFINTC1_SIMR);

	for (irq = 0; (irq < NR_IRQS); irq++) {
		irq_desc[irq].status = IRQ_DISABLED;
		irq_desc[irq].action = NULL;
		irq_desc[irq].depth = 1;
		irq_desc[irq].chip = &intc_irq_chip;
		intc_irq_set_type(irq, 0);
	}
}

