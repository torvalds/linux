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

static void ns9xxx_mask_irq(unsigned int irq)
{
	/* XXX: better use cpp symbols */
	int prio = irq2prio(irq);
	u32 ic = __raw_readl(SYS_IC(prio / 4));
	ic &= ~(1 << (7 + 8 * (3 - (prio & 3))));
	__raw_writel(ic, SYS_IC(prio / 4));
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
	int prio = irq2prio(irq);
	u32 ic = __raw_readl(SYS_IC(prio / 4));
	ic |= 1 << (7 + 8 * (3 - (prio & 3)));
	__raw_writel(ic, SYS_IC(prio / 4));
}

static struct irq_chip ns9xxx_chip = {
	.ack		= ns9xxx_ack_irq,
	.mask		= ns9xxx_mask_irq,
	.mask_ack	= ns9xxx_maskack_irq,
	.unmask		= ns9xxx_unmask_irq,
};

#if 0
#define handle_irq handle_level_irq
#else
static void handle_prio_irq(unsigned int irq, struct irq_desc *desc)
{
	struct irqaction *action;
	irqreturn_t action_ret;

	spin_lock(&desc->lock);

	BUG_ON(desc->status & IRQ_INPROGRESS);

	desc->status &= ~(IRQ_REPLAY | IRQ_WAITING);
	kstat_incr_irqs_this_cpu(irq, desc);

	action = desc->action;
	if (unlikely(!action || (desc->status & IRQ_DISABLED)))
		goto out_mask;

	desc->status |= IRQ_INPROGRESS;
	spin_unlock(&desc->lock);

	action_ret = handle_IRQ_event(irq, action);

	/* XXX: There is no direct way to access noirqdebug, so check
	 * unconditionally for spurious irqs...
	 * Maybe this function should go to kernel/irq/chip.c? */
	note_interrupt(irq, desc, action_ret);

	spin_lock(&desc->lock);
	desc->status &= ~IRQ_INPROGRESS;

	if (desc->status & IRQ_DISABLED)
out_mask:
		desc->chip->mask(irq);

	/* ack unconditionally to unmask lower prio irqs */
	desc->chip->ack(irq);

	spin_unlock(&desc->lock);
}
#define handle_irq handle_prio_irq
#endif

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
		set_irq_chip(i, &ns9xxx_chip);
		set_irq_handler(i, handle_irq);
		set_irq_flags(i, IRQF_VALID);
	}
}
