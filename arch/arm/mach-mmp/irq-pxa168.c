/*
 *  linux/arch/arm/mach-mmp/irq.c
 *
 *  Generic IRQ handling, GPIO IRQ demultiplexing, etc.
 *
 *  Author:	Bin Yang <bin.yang@marvell.com>
 *  Created:	Sep 30, 2008
 *  Copyright:	Marvell International Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <mach/regs-icu.h>

#include "common.h"

#define IRQ_ROUTE_TO_AP		(ICU_INT_CONF_AP_INT | ICU_INT_CONF_IRQ)

#define PRIORITY_DEFAULT	0x1
#define PRIORITY_NONE		0x0	/* means IRQ disabled */

static void icu_mask_irq(unsigned int irq)
{
	__raw_writel(PRIORITY_NONE, ICU_INT_CONF(irq));
}

static void icu_unmask_irq(unsigned int irq)
{
	__raw_writel(IRQ_ROUTE_TO_AP | PRIORITY_DEFAULT, ICU_INT_CONF(irq));
}

static struct irq_chip icu_irq_chip = {
	.name	= "icu_irq",
	.ack	= icu_mask_irq,
	.mask	= icu_mask_irq,
	.unmask	= icu_unmask_irq,
};

void __init icu_init_irq(void)
{
	int irq;

	for (irq = 0; irq < 64; irq++) {
		icu_mask_irq(irq);
		set_irq_chip(irq, &icu_irq_chip);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}
}
