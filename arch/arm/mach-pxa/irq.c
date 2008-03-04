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

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/arch/pxa-regs.h>

#include "generic.h"


/*
 * This is for peripheral IRQs internal to the PXA chip.
 */

static void pxa_mask_low_irq(unsigned int irq)
{
	ICMR &= ~(1 << irq);
}

static void pxa_unmask_low_irq(unsigned int irq)
{
	ICMR |= (1 << irq);
}

static struct irq_chip pxa_internal_chip_low = {
	.name		= "SC",
	.ack		= pxa_mask_low_irq,
	.mask		= pxa_mask_low_irq,
	.unmask		= pxa_unmask_low_irq,
};

void __init pxa_init_irq_low(void)
{
	int irq;

	/* disable all IRQs */
	ICMR = 0;

	/* all IRQs are IRQ, not FIQ */
	ICLR = 0;

	/* only unmasked interrupts kick us out of idle */
	ICCR = 1;

	for (irq = PXA_IRQ(0); irq <= PXA_IRQ(31); irq++) {
		set_irq_chip(irq, &pxa_internal_chip_low);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}
}

#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)

/*
 * This is for the second set of internal IRQs as found on the PXA27x.
 */

static void pxa_mask_high_irq(unsigned int irq)
{
	ICMR2 &= ~(1 << (irq - 32));
}

static void pxa_unmask_high_irq(unsigned int irq)
{
	ICMR2 |= (1 << (irq - 32));
}

static struct irq_chip pxa_internal_chip_high = {
	.name		= "SC-hi",
	.ack		= pxa_mask_high_irq,
	.mask		= pxa_mask_high_irq,
	.unmask		= pxa_unmask_high_irq,
};

void __init pxa_init_irq_high(void)
{
	int irq;

	ICMR2 = 0;
	ICLR2 = 0;

	for (irq = PXA_IRQ(32); irq < PXA_IRQ(64); irq++) {
		set_irq_chip(irq, &pxa_internal_chip_high);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}
}
#endif

void __init pxa_init_irq_set_wake(int (*set_wake)(unsigned int, unsigned int))
{
	pxa_internal_chip_low.set_wake = set_wake;
#ifdef CONFIG_PXA27x
	pxa_internal_chip_high.set_wake = set_wake;
#endif
	pxa_init_gpio_set_wake(set_wake);
}

#ifdef CONFIG_PM
static unsigned long saved_icmr[2];

static int pxa_irq_suspend(struct sys_device *dev, pm_message_t state)
{
	switch (dev->id) {
	case 0:
		saved_icmr[0] = ICMR;
		ICMR = 0;
		break;
#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)
	case 1:
		saved_icmr[1] = ICMR2;
		ICMR2 = 0;
		break;
#endif
	default:
		return -EINVAL;
	}

	return 0;
}

static int pxa_irq_resume(struct sys_device *dev)
{
	switch (dev->id) {
	case 0:
		ICMR = saved_icmr[0];
		ICLR = 0;
		ICCR = 1;
		break;
#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)
	case 1:
		ICMR2 = saved_icmr[1];
		ICLR2 = 0;
		break;
#endif
	default:
		return -EINVAL;
	}

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
