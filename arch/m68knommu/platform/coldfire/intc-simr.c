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
	if ((irq >= MCFINT_VECBASE) && (irq <= MCFINT_VECBASE + 63))
		__raw_writeb(irq - MCFINT_VECBASE, MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_SIMR);
}

static void intc_irq_unmask(unsigned int irq)
{
	if ((irq >= MCFINT_VECBASE) && (irq <= MCFINT_VECBASE + 63))
		__raw_writeb(irq - MCFINT_VECBASE, MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_CIMR);
}

static int intc_irq_set_type(unsigned int irq, unsigned int type)
{
	if ((irq >= MCFINT_VECBASE) && (irq <= MCFINT_VECBASE + 63))
		__raw_writeb(5, MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_ICR0 + irq - MCFINT_VECBASE);
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

	for (irq = 0; (irq < NR_IRQS); irq++) {
		irq_desc[irq].status = IRQ_DISABLED;
		irq_desc[irq].action = NULL;
		irq_desc[irq].depth = 1;
		irq_desc[irq].chip = &intc_irq_chip;
		intc_irq_set_type(irq, 0);
	}
}

