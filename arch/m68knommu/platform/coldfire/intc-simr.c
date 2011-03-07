/*
 * intc-simr.c
 *
 * Interrupt controller code for the ColdFire 5208, 5207 & 532x parts.
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

/*
 *	There maybe one or two interrupt control units, each has 64
 *	interrupts. If there is no second unit then MCFINTC1_* defines
 *	will be 0 (and code for them optimized away).
 */

static void intc_irq_mask(struct irq_data *d)
{
	unsigned int irq = d->irq - MCFINT_VECBASE;

	if (MCFINTC1_SIMR && (irq > 64))
		__raw_writeb(irq - 64, MCFINTC1_SIMR);
	else
		__raw_writeb(irq, MCFINTC0_SIMR);
}

static void intc_irq_unmask(struct irq_data *d)
{
	unsigned int irq = d->irq - MCFINT_VECBASE;

	if (MCFINTC1_CIMR && (irq > 64))
		__raw_writeb(irq - 64, MCFINTC1_CIMR);
	else
		__raw_writeb(irq, MCFINTC0_CIMR);
}

static int intc_irq_set_type(struct irq_data *d, unsigned int type)
{
	unsigned int irq = d->irq - MCFINT_VECBASE;

	if (MCFINTC1_ICR0 && (irq > 64))
		__raw_writeb(5, MCFINTC1_ICR0 + irq - 64);
	else
		__raw_writeb(5, MCFINTC0_ICR0 + irq);
	return 0;
}

static struct irq_chip intc_irq_chip = {
	.name		= "CF-INTC",
	.irq_mask	= intc_irq_mask,
	.irq_unmask	= intc_irq_unmask,
	.irq_set_type	= intc_irq_set_type,
};

void __init init_IRQ(void)
{
	int irq, eirq;

	init_vectors();

	/* Mask all interrupt sources */
	__raw_writeb(0xff, MCFINTC0_SIMR);
	if (MCFINTC1_SIMR)
		__raw_writeb(0xff, MCFINTC1_SIMR);

	eirq = MCFINT_VECBASE + 64 + (MCFINTC1_ICR0 ? 64 : 0);
	for (irq = MCFINT_VECBASE; (irq < eirq); irq++) {
		set_irq_chip(irq, &intc_irq_chip);
		set_irq_type(irq, IRQ_TYPE_LEVEL_HIGH);
		set_irq_handler(irq, handle_level_irq);
	}
}

