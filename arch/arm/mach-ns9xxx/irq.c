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
#include <linux/kernel_stat.h>
#include <linux/io.h>
#include <asm/mach/irq.h>
#include <mach/regs-sys-common.h>
#include <mach/irqs.h>
#include <mach/board.h>

#include "generic.h"

/* simple interrupt prio table: prio(x) < prio(y) <=> x < y */
#define irq2prio(i) (i)
#define prio2irq(p) (p)

static void ns9xxx_mask_irq(struct irq_data *d)
{
	/* XXX: better use cpp symbols */
	int prio = irq2prio(d->irq);
	u32 ic = __raw_readl(SYS_IC(prio / 4));
	ic &= ~(1 << (7 + 8 * (3 - (prio & 3))));
	__raw_writel(ic, SYS_IC(prio / 4));
}

static void ns9xxx_eoi_irq(struct irq_data *d)
{
	__raw_writel(0, SYS_ISRADDR);
}

static void ns9xxx_unmask_irq(struct irq_data *d)
{
	/* XXX: better use cpp symbols */
	int prio = irq2prio(d->irq);
	u32 ic = __raw_readl(SYS_IC(prio / 4));
	ic |= 1 << (7 + 8 * (3 - (prio & 3)));
	__raw_writel(ic, SYS_IC(prio / 4));
}

static struct irq_chip ns9xxx_chip = {
	.irq_eoi	= ns9xxx_eoi_irq,
	.irq_mask	= ns9xxx_mask_irq,
	.irq_unmask	= ns9xxx_unmask_irq,
};

void __init ns9xxx_init_irq(void)
{
	int i;

	/* disable all IRQs */
	for (i = 0; i < 8; ++i)
		__raw_writel(prio2irq(4 * i) << 24 |
				prio2irq(4 * i + 1) << 16 |
				prio2irq(4 * i + 2) << 8 |
				prio2irq(4 * i + 3),
				SYS_IC(i));

	for (i = 0; i < 32; ++i)
		__raw_writel(prio2irq(i), SYS_IVA(i));

	for (i = 0; i <= 31; ++i) {
		irq_set_chip_and_handler(i, &ns9xxx_chip, handle_fasteoi_irq);
		set_irq_flags(i, IRQF_VALID);
		irq_set_status_flags(i, IRQ_LEVEL);
	}
}
