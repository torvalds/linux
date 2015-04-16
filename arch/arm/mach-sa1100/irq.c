/*
 * linux/arch/arm/mach-sa1100/irq.c
 *
 * Copyright (C) 1999-2001 Nicolas Pitre
 *
 * Generic IRQ handling for the SA11x0, GPIO 11-27 IRQ demultiplexing.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/ioport.h>
#include <linux/syscore_ops.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <asm/mach/irq.h>
#include <asm/exception.h>

#include "generic.h"


/*
 * We don't need to ACK IRQs on the SA1100 unless they're GPIOs
 * this is for internal IRQs i.e. from IRQ LCD to RTCAlrm.
 */
static void sa1100_mask_irq(struct irq_data *d)
{
	ICMR &= ~BIT(d->hwirq);
}

static void sa1100_unmask_irq(struct irq_data *d)
{
	ICMR |= BIT(d->hwirq);
}

/*
 * Apart form GPIOs, only the RTC alarm can be a wakeup event.
 */
static int sa1100_set_wake(struct irq_data *d, unsigned int on)
{
	if (BIT(d->hwirq) == IC_RTCAlrm) {
		if (on)
			PWER |= PWER_RTC;
		else
			PWER &= ~PWER_RTC;
		return 0;
	}
	return -EINVAL;
}

static struct irq_chip sa1100_normal_chip = {
	.name		= "SC",
	.irq_ack	= sa1100_mask_irq,
	.irq_mask	= sa1100_mask_irq,
	.irq_unmask	= sa1100_unmask_irq,
	.irq_set_wake	= sa1100_set_wake,
};

static int sa1100_normal_irqdomain_map(struct irq_domain *d,
		unsigned int irq, irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &sa1100_normal_chip,
				 handle_level_irq);
	set_irq_flags(irq, IRQF_VALID);

	return 0;
}

static struct irq_domain_ops sa1100_normal_irqdomain_ops = {
	.map = sa1100_normal_irqdomain_map,
	.xlate = irq_domain_xlate_onetwocell,
};

static struct irq_domain *sa1100_normal_irqdomain;

static struct resource irq_resource =
	DEFINE_RES_MEM_NAMED(0x90050000, SZ_64K, "irqs");

static struct sa1100irq_state {
	unsigned int	saved;
	unsigned int	icmr;
	unsigned int	iclr;
	unsigned int	iccr;
} sa1100irq_state;

static int sa1100irq_suspend(void)
{
	struct sa1100irq_state *st = &sa1100irq_state;

	st->saved = 1;
	st->icmr = ICMR;
	st->iclr = ICLR;
	st->iccr = ICCR;

	/*
	 * Disable all GPIO-based interrupts.
	 */
	ICMR &= ~(IC_GPIO11_27|IC_GPIO10|IC_GPIO9|IC_GPIO8|IC_GPIO7|
		  IC_GPIO6|IC_GPIO5|IC_GPIO4|IC_GPIO3|IC_GPIO2|
		  IC_GPIO1|IC_GPIO0);

	return 0;
}

static void sa1100irq_resume(void)
{
	struct sa1100irq_state *st = &sa1100irq_state;

	if (st->saved) {
		ICCR = st->iccr;
		ICLR = st->iclr;

		ICMR = st->icmr;
	}
}

static struct syscore_ops sa1100irq_syscore_ops = {
	.suspend	= sa1100irq_suspend,
	.resume		= sa1100irq_resume,
};

static int __init sa1100irq_init_devicefs(void)
{
	register_syscore_ops(&sa1100irq_syscore_ops);
	return 0;
}

device_initcall(sa1100irq_init_devicefs);

static asmlinkage void __exception_irq_entry
sa1100_handle_irq(struct pt_regs *regs)
{
	uint32_t icip, icmr, mask;

	do {
		icip = (ICIP);
		icmr = (ICMR);
		mask = icip & icmr;

		if (mask == 0)
			break;

		handle_domain_irq(sa1100_normal_irqdomain,
				ffs(mask) - 1, regs);
	} while (1);
}

void __init sa1100_init_irq(void)
{
	request_resource(&iomem_resource, &irq_resource);

	/* disable all IRQs */
	ICMR = 0;

	/* all IRQs are IRQ, not FIQ */
	ICLR = 0;

	/*
	 * Whatever the doc says, this has to be set for the wait-on-irq
	 * instruction to work... on a SA1100 rev 9 at least.
	 */
	ICCR = 1;

	sa1100_normal_irqdomain = irq_domain_add_simple(NULL,
			32, IRQ_GPIO0_SC,
			&sa1100_normal_irqdomain_ops, NULL);

	set_handle_irq(sa1100_handle_irq);

	sa1100_init_gpio();
}
