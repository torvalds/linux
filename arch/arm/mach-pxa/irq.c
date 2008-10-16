/*
 *  linux/arch/arm/mach-pxa/irq.c
 *
 *  Generic PXA IRQ handling
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/sysdev.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <mach/pxa-regs.h>

#include "generic.h"

#define IRQ_BIT(n)	(((n) - PXA_IRQ(0)) & 0x1f)
#define _ICMR(n)	(*((((n) - PXA_IRQ(0)) & ~0x1f) ? &ICMR2 : &ICMR))
#define _ICLR(n)	(*((((n) - PXA_IRQ(0)) & ~0x1f) ? &ICLR2 : &ICLR))

/*
 * This is for peripheral IRQs internal to the PXA chip.
 */

static int pxa_internal_irq_nr;

static void pxa_mask_irq(unsigned int irq)
{
	_ICMR(irq) &= ~(1 << IRQ_BIT(irq));
}

static void pxa_unmask_irq(unsigned int irq)
{
	_ICMR(irq) |= 1 << IRQ_BIT(irq);
}

static struct irq_chip pxa_internal_irq_chip = {
	.name		= "SC",
	.ack		= pxa_mask_irq,
	.mask		= pxa_mask_irq,
	.unmask		= pxa_unmask_irq,
};

void __init pxa_init_irq(int irq_nr, set_wake_t fn)
{
	int irq;

	pxa_internal_irq_nr = irq_nr;

	for (irq = PXA_IRQ(0); irq < PXA_IRQ(irq_nr); irq += 32) {
		_ICMR(irq) = 0;	/* disable all IRQs */
		_ICLR(irq) = 0;	/* all IRQs are IRQ, not FIQ */
	}

	/* only unmasked interrupts kick us out of idle */
	ICCR = 1;

	for (irq = PXA_IRQ(0); irq < PXA_IRQ(irq_nr); irq++) {
		set_irq_chip(irq, &pxa_internal_irq_chip);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	pxa_internal_irq_chip.set_wake = fn;
}

#ifdef CONFIG_PM
static unsigned long saved_icmr[2];

static int pxa_irq_suspend(struct sys_device *dev, pm_message_t state)
{
	int i, irq = PXA_IRQ(0);

	for (i = 0; irq < PXA_IRQ(pxa_internal_irq_nr); i++, irq += 32) {
		saved_icmr[i] = _ICMR(irq);
		_ICMR(irq) = 0;
	}

	return 0;
}

static int pxa_irq_resume(struct sys_device *dev)
{
	int i, irq = PXA_IRQ(0);

	for (i = 0; irq < PXA_IRQ(pxa_internal_irq_nr); i++, irq += 32) {
		_ICMR(irq) = saved_icmr[i];
		_ICLR(irq) = 0;
	}

	ICCR = 1;
	return 0;
}
#else
#define pxa_irq_suspend		NULL
#define pxa_irq_resume		NULL
#endif

struct sysdev_class pxa_irq_sysclass = {
	.name		= "irq",
	.suspend	= pxa_irq_suspend,
	.resume		= pxa_irq_resume,
};

static int __init pxa_irq_init(void)
{
	return sysdev_class_register(&pxa_irq_sysclass);
}

core_initcall(pxa_irq_init);
