/*
 * arch/arm/mach-ns9xxx/irq.c
 *
 * Copyright (C) 2006,2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/interrupt.h>
#include <asm/mach/irq.h>
#include <asm/mach-types.h>
#include <asm/arch-ns9xxx/regs-sys.h>
#include <asm/arch-ns9xxx/irqs.h>
#include <asm/arch-ns9xxx/board.h>

#include "generic.h"

static void ns9xxx_ack_irq_timer(unsigned int irq)
{
	u32 tc = SYS_TC(irq - IRQ_TIMER0);

	/*
	 * If the timer is programmed to halt on terminal count, the
	 * timer must be disabled before clearing the interrupt.
	 */
	if (REGGET(tc, SYS_TCx, REN) == 0) {
		REGSET(tc, SYS_TCx, TEN, DIS);
		SYS_TC(irq - IRQ_TIMER0) = tc;
	}

	REGSET(tc, SYS_TCx, INTC, SET);
	SYS_TC(irq - IRQ_TIMER0) = tc;

	REGSET(tc, SYS_TCx, INTC, UNSET);
	SYS_TC(irq - IRQ_TIMER0) = tc;
}

static void (*ns9xxx_ack_irq_functions[NR_IRQS])(unsigned int) = {
	[IRQ_TIMER0] = ns9xxx_ack_irq_timer,
	[IRQ_TIMER1] = ns9xxx_ack_irq_timer,
	[IRQ_TIMER2] = ns9xxx_ack_irq_timer,
	[IRQ_TIMER3] = ns9xxx_ack_irq_timer,
};

static void ns9xxx_mask_irq(unsigned int irq)
{
	/* XXX: better use cpp symbols */
	SYS_IC(irq / 4) &= ~(1 << (7 + 8 * (3 - (irq & 3))));
}

static void ns9xxx_ack_irq(unsigned int irq)
{
	if (!ns9xxx_ack_irq_functions[irq]) {
		printk(KERN_ERR "no ack function for irq %u\n", irq);
		BUG();
	}

	ns9xxx_ack_irq_functions[irq](irq);
	SYS_ISRADDR = 0;
}

static void ns9xxx_maskack_irq(unsigned int irq)
{
	ns9xxx_mask_irq(irq);
	ns9xxx_ack_irq(irq);
}

static void ns9xxx_unmask_irq(unsigned int irq)
{
	/* XXX: better use cpp symbols */
	SYS_IC(irq / 4) |= 1 << (7 + 8 * (3 - (irq & 3)));
}

static struct irq_chip ns9xxx_chip = {
	.ack		= ns9xxx_ack_irq,
	.mask		= ns9xxx_mask_irq,
	.mask_ack	= ns9xxx_maskack_irq,
	.unmask		= ns9xxx_unmask_irq,
};

void __init ns9xxx_init_irq(void)
{
	int i;

	/* disable all IRQs */
	for (i = 0; i < 8; ++i)
		SYS_IC(i) = (4 * i) << 24 | (4 * i + 1) << 16 |
			(4 * i + 2) << 8 | (4 * i + 3);

	/* simple interrupt prio table:
	 * prio(x) < prio(y) <=> x < y
	 */
	for (i = 0; i < 32; ++i)
		SYS_IVA(i) = i;

	for (i = IRQ_WATCHDOG; i <= IRQ_EXT3; ++i) {
		set_irq_chip(i, &ns9xxx_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}
}
