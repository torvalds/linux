/*
 * External interrupt handling for AT32AP CPUs
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/random.h>

#include <asm/io.h>

#include <asm/arch/sm.h>

#include "sm.h"

static void eim_ack_irq(unsigned int irq)
{
	struct at32_sm *sm = get_irq_chip_data(irq);
	sm_writel(sm, EIM_ICR, 1 << (irq - sm->eim_first_irq));
}

static void eim_mask_irq(unsigned int irq)
{
	struct at32_sm *sm = get_irq_chip_data(irq);
	sm_writel(sm, EIM_IDR, 1 << (irq - sm->eim_first_irq));
}

static void eim_mask_ack_irq(unsigned int irq)
{
	struct at32_sm *sm = get_irq_chip_data(irq);
	sm_writel(sm, EIM_ICR, 1 << (irq - sm->eim_first_irq));
	sm_writel(sm, EIM_IDR, 1 << (irq - sm->eim_first_irq));
}

static void eim_unmask_irq(unsigned int irq)
{
	struct at32_sm *sm = get_irq_chip_data(irq);
	sm_writel(sm, EIM_IER, 1 << (irq - sm->eim_first_irq));
}

static int eim_set_irq_type(unsigned int irq, unsigned int flow_type)
{
	struct at32_sm *sm = get_irq_chip_data(irq);
	struct irq_desc *desc;
	unsigned int i = irq - sm->eim_first_irq;
	u32 mode, edge, level;
	unsigned long flags;
	int ret = 0;

	if (flow_type == IRQ_TYPE_NONE)
		flow_type = IRQ_TYPE_LEVEL_LOW;

	desc = &irq_desc[irq];
	desc->status &= ~(IRQ_TYPE_SENSE_MASK | IRQ_LEVEL);
	desc->status |= flow_type & IRQ_TYPE_SENSE_MASK;

	if (flow_type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH)) {
		desc->status |= IRQ_LEVEL;
		set_irq_handler(irq, handle_level_irq);
	} else {
		set_irq_handler(irq, handle_edge_irq);
	}

	spin_lock_irqsave(&sm->lock, flags);

	mode = sm_readl(sm, EIM_MODE);
	edge = sm_readl(sm, EIM_EDGE);
	level = sm_readl(sm, EIM_LEVEL);

	switch (flow_type) {
	case IRQ_TYPE_LEVEL_LOW:
		mode |= 1 << i;
		level &= ~(1 << i);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		mode |= 1 << i;
		level |= 1 << i;
		break;
	case IRQ_TYPE_EDGE_RISING:
		mode &= ~(1 << i);
		edge |= 1 << i;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		mode &= ~(1 << i);
		edge &= ~(1 << i);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	sm_writel(sm, EIM_MODE, mode);
	sm_writel(sm, EIM_EDGE, edge);
	sm_writel(sm, EIM_LEVEL, level);

	spin_unlock_irqrestore(&sm->lock, flags);

	return ret;
}

struct irq_chip eim_chip = {
	.name		= "eim",
	.ack		= eim_ack_irq,
	.mask		= eim_mask_irq,
	.mask_ack	= eim_mask_ack_irq,
	.unmask		= eim_unmask_irq,
	.set_type	= eim_set_irq_type,
};

static void demux_eim_irq(unsigned int irq, struct irq_desc *desc)
{
	struct at32_sm *sm = desc->handler_data;
	struct irq_desc *ext_desc;
	unsigned long status, pending;
	unsigned int i, ext_irq;

	spin_lock(&sm->lock);

	status = sm_readl(sm, EIM_ISR);
	pending = status & sm_readl(sm, EIM_IMR);

	while (pending) {
		i = fls(pending) - 1;
		pending &= ~(1 << i);

		ext_irq = i + sm->eim_first_irq;
		ext_desc = irq_desc + ext_irq;
		ext_desc->handle_irq(ext_irq, ext_desc);
	}

	spin_unlock(&sm->lock);
}

static int __init eim_init(void)
{
	struct at32_sm *sm = &system_manager;
	unsigned int i;
	unsigned int nr_irqs;
	unsigned int int_irq;
	u32 pattern;

	/*
	 * The EIM is really the same module as SM, so register
	 * mapping, etc. has been taken care of already.
	 */

	/*
	 * Find out how many interrupt lines that are actually
	 * implemented in hardware.
	 */
	sm_writel(sm, EIM_IDR, ~0UL);
	sm_writel(sm, EIM_MODE, ~0UL);
	pattern = sm_readl(sm, EIM_MODE);
	nr_irqs = fls(pattern);

	/* Trigger on falling edge unless overridden by driver */
	sm_writel(sm, EIM_MODE, 0UL);
	sm_writel(sm, EIM_EDGE, 0UL);

	sm->eim_chip = &eim_chip;

	for (i = 0; i < nr_irqs; i++) {
		set_irq_chip_and_handler(sm->eim_first_irq + i, &eim_chip,
					 handle_edge_irq);
		set_irq_chip_data(sm->eim_first_irq + i, sm);
	}

	int_irq = platform_get_irq_byname(sm->pdev, "eim");

	set_irq_chained_handler(int_irq, demux_eim_irq);
	set_irq_data(int_irq, sm);

	printk("EIM: External Interrupt Module at 0x%p, IRQ %u\n",
	       sm->regs, int_irq);
	printk("EIM: Handling %u external IRQs, starting with IRQ %u\n",
	       nr_irqs, sm->eim_first_irq);

	return 0;
}
arch_initcall(eim_init);
