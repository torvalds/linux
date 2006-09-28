/*
 * System Manager driver for AT32AP CPUs
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
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/spinlock.h>

#include <asm/intc.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <asm/arch/sm.h>

#include "sm.h"

#define SM_EIM_IRQ_RESOURCE	1
#define SM_PM_IRQ_RESOURCE	2
#define SM_RTC_IRQ_RESOURCE	3

#define to_eim(irqc) container_of(irqc, struct at32_sm, irqc)

struct at32_sm system_manager;

int __init at32_sm_init(void)
{
	struct resource *regs;
	struct at32_sm *sm = &system_manager;
	int ret = -ENXIO;

	regs = platform_get_resource(&at32_sm_device, IORESOURCE_MEM, 0);
	if (!regs)
		goto fail;

	spin_lock_init(&sm->lock);
	sm->pdev = &at32_sm_device;

	ret = -ENOMEM;
	sm->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!sm->regs)
		goto fail;

	return 0;

fail:
	printk(KERN_ERR "Failed to initialize System Manager: %d\n", ret);
	return ret;
}

/*
 * External Interrupt Module (EIM).
 *
 * EIM gets level- or edge-triggered interrupts of either polarity
 * from the outside and converts it to active-high level-triggered
 * interrupts that the internal interrupt controller can handle. EIM
 * also provides masking/unmasking of interrupts, as well as
 * acknowledging of edge-triggered interrupts.
 */

static irqreturn_t spurious_eim_interrupt(int irq, void *dev_id,
					  struct pt_regs *regs)
{
	printk(KERN_WARNING "Spurious EIM interrupt %d\n", irq);
	disable_irq(irq);
	return IRQ_NONE;
}

static struct irqaction eim_spurious_action = {
	.handler = spurious_eim_interrupt,
};

static irqreturn_t eim_handle_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	struct irq_controller * irqc = dev_id;
	struct at32_sm *sm = to_eim(irqc);
	unsigned long pending;

	/*
	 * No need to disable interrupts globally.  The interrupt
	 * level relevant to this group must be masked all the time,
	 * so we know that this particular EIM instance will not be
	 * re-entered.
	 */
	spin_lock(&sm->lock);

	pending = intc_get_pending(sm->irqc.irq_group);
	if (unlikely(!pending)) {
		printk(KERN_ERR "EIM (group %u): No interrupts pending!\n",
		       sm->irqc.irq_group);
		goto unlock;
	}

	do {
		struct irqaction *action;
		unsigned int i;

		i = fls(pending) - 1;
		pending &= ~(1 << i);
		action = sm->action[i];

		/* Acknowledge the interrupt */
		sm_writel(sm, EIM_ICR, 1 << i);

		spin_unlock(&sm->lock);

		if (action->flags & SA_INTERRUPT)
			local_irq_disable();
		action->handler(sm->irqc.first_irq + i, action->dev_id, regs);
		local_irq_enable();
		spin_lock(&sm->lock);
		if (action->flags & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(sm->irqc.first_irq + i);
	} while (pending);

unlock:
	spin_unlock(&sm->lock);
	return IRQ_HANDLED;
}

static void eim_mask(struct irq_controller *irqc, unsigned int irq)
{
	struct at32_sm *sm = to_eim(irqc);
	unsigned int i;

	i = irq - sm->irqc.first_irq;
	sm_writel(sm, EIM_IDR, 1 << i);
}

static void eim_unmask(struct irq_controller *irqc, unsigned int irq)
{
	struct at32_sm *sm = to_eim(irqc);
	unsigned int i;

	i = irq - sm->irqc.first_irq;
	sm_writel(sm, EIM_IER, 1 << i);
}

static int eim_setup(struct irq_controller *irqc, unsigned int irq,
		struct irqaction *action)
{
	struct at32_sm *sm = to_eim(irqc);
	sm->action[irq - sm->irqc.first_irq] = action;
	/* Acknowledge earlier interrupts */
	sm_writel(sm, EIM_ICR, (1<<(irq - sm->irqc.first_irq)));
	eim_unmask(irqc, irq);
	return 0;
}

static void eim_free(struct irq_controller *irqc, unsigned int irq,
		void *dev)
{
	struct at32_sm *sm = to_eim(irqc);
	eim_mask(irqc, irq);
	sm->action[irq - sm->irqc.first_irq] = &eim_spurious_action;
}

static int eim_set_type(struct irq_controller *irqc, unsigned int irq,
			unsigned int type)
{
	struct at32_sm *sm = to_eim(irqc);
	unsigned long flags;
	u32 value, pattern;

	spin_lock_irqsave(&sm->lock, flags);

	pattern = 1 << (irq - sm->irqc.first_irq);

	value = sm_readl(sm, EIM_MODE);
	if (type & IRQ_TYPE_LEVEL)
		value |= pattern;
	else
		value &= ~pattern;
	sm_writel(sm, EIM_MODE, value);
	value = sm_readl(sm, EIM_EDGE);
	if (type & IRQ_EDGE_RISING)
		value |= pattern;
	else
		value &= ~pattern;
	sm_writel(sm, EIM_EDGE, value);
	value = sm_readl(sm, EIM_LEVEL);
	if (type & IRQ_LEVEL_HIGH)
		value |= pattern;
	else
		value &= ~pattern;
	sm_writel(sm, EIM_LEVEL, value);

	spin_unlock_irqrestore(&sm->lock, flags);

	return 0;
}

static unsigned int eim_get_type(struct irq_controller *irqc,
				 unsigned int irq)
{
	struct at32_sm *sm = to_eim(irqc);
	unsigned long flags;
	unsigned int type = 0;
	u32 mode, edge, level, pattern;

	pattern = 1 << (irq - sm->irqc.first_irq);

	spin_lock_irqsave(&sm->lock, flags);
	mode = sm_readl(sm, EIM_MODE);
	edge = sm_readl(sm, EIM_EDGE);
	level = sm_readl(sm, EIM_LEVEL);
	spin_unlock_irqrestore(&sm->lock, flags);

	if (mode & pattern)
		type |= IRQ_TYPE_LEVEL;
	if (edge & pattern)
		type |= IRQ_EDGE_RISING;
	if (level & pattern)
		type |= IRQ_LEVEL_HIGH;

	return type;
}

static struct irq_controller_class eim_irq_class = {
	.typename	= "EIM",
	.handle		= eim_handle_irq,
	.setup		= eim_setup,
	.free		= eim_free,
	.mask		= eim_mask,
	.unmask		= eim_unmask,
	.set_type	= eim_set_type,
	.get_type	= eim_get_type,
};

static int __init eim_init(void)
{
	struct at32_sm *sm = &system_manager;
	unsigned int i;
	u32 pattern;
	int ret;

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
	sm->irqc.nr_irqs = fls(pattern);

	ret = -ENOMEM;
	sm->action = kmalloc(sizeof(*sm->action) * sm->irqc.nr_irqs,
			     GFP_KERNEL);
	if (!sm->action)
		goto out;

	for (i = 0; i < sm->irqc.nr_irqs; i++)
		sm->action[i] = &eim_spurious_action;

	spin_lock_init(&sm->lock);
	sm->irqc.irq_group = sm->pdev->resource[SM_EIM_IRQ_RESOURCE].start;
	sm->irqc.class = &eim_irq_class;

	ret = intc_register_controller(&sm->irqc);
	if (ret < 0)
		goto out_free_actions;

	printk("EIM: External Interrupt Module at 0x%p, IRQ group %u\n",
	       sm->regs, sm->irqc.irq_group);
	printk("EIM: Handling %u external IRQs, starting with IRQ%u\n",
	       sm->irqc.nr_irqs, sm->irqc.first_irq);

	return 0;

out_free_actions:
	kfree(sm->action);
out:
	return ret;
}
arch_initcall(eim_init);
