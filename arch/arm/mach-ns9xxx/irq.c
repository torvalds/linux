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
#include <asm/io.h>
#include <asm/mach/irq.h>
#include <asm/mach-types.h>
#include <asm/arch-ns9xxx/regs-sys.h>
#include <asm/arch-ns9xxx/irqs.h>
#include <asm/arch-ns9xxx/board.h>

#include "generic.h"

static void ns9xxx_mask_irq(unsigned int irq)
{
	/* XXX: better use cpp symbols */
	u32 ic = __raw_readl(SYS_IC(irq / 4));
	ic &= ~(1 << (7 + 8 * (3 - (irq & 3))));
	__raw_writel(ic, SYS_IC(irq / 4));
}

static void ns9xxx_ack_irq(unsigned int irq)
{
	__raw_writel(0, SYS_ISRADDR);
}

static void ns9xxx_maskack_irq(unsigned int irq)
{
	ns9xxx_mask_irq(irq);
	ns9xxx_ack_irq(irq);
}

static void ns9xxx_unmask_irq(unsigned int irq)
{
	/* XXX: better use cpp symbols */
	u32 ic = __raw_readl(SYS_IC(irq / 4));
	ic |= 1 << (7 + 8 * (3 - (irq & 3)));
	__raw_writel(ic, SYS_IC(irq / 4));
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
		__raw_writel((4 * i) << 24 | (4 * i + 1) << 16 |
				(4 * i + 2) << 8 | (4 * i + 3), SYS_IC(i));

	/* simple interrupt prio table:
	 * prio(x) < prio(y) <=> x < y
	 */
	for (i = 0; i < 32; ++i)
		__raw_writel(i, SYS_IVA(i));

	for (i = IRQ_WATCHDOG; i <= IRQ_EXT3; ++i) {
		set_irq_chip(i, &ns9xxx_chip);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}
}
