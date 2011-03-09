/*
 * arch/arm/plat-spear/shirq.c
 *
 * SPEAr platform shared irq layer source file
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <plat/shirq.h>

struct spear_shirq *shirq;
static DEFINE_SPINLOCK(lock);

static void shirq_irq_mask(struct irq_data *d)
{
	struct spear_shirq *shirq = irq_data_get_irq_chip_data(d);
	u32 val, id = d->irq - shirq->dev_config[0].virq;
	unsigned long flags;

	if ((shirq->regs.enb_reg == -1) || shirq->dev_config[id].enb_mask == -1)
		return;

	spin_lock_irqsave(&lock, flags);
	val = readl(shirq->regs.base + shirq->regs.enb_reg);
	if (shirq->regs.reset_to_enb)
		val |= shirq->dev_config[id].enb_mask;
	else
		val &= ~(shirq->dev_config[id].enb_mask);
	writel(val, shirq->regs.base + shirq->regs.enb_reg);
	spin_unlock_irqrestore(&lock, flags);
}

static void shirq_irq_unmask(struct irq_data *d)
{
	struct spear_shirq *shirq = irq_data_get_irq_chip_data(d);
	u32 val, id = d->irq - shirq->dev_config[0].virq;
	unsigned long flags;

	if ((shirq->regs.enb_reg == -1) || shirq->dev_config[id].enb_mask == -1)
		return;

	spin_lock_irqsave(&lock, flags);
	val = readl(shirq->regs.base + shirq->regs.enb_reg);
	if (shirq->regs.reset_to_enb)
		val &= ~(shirq->dev_config[id].enb_mask);
	else
		val |= shirq->dev_config[id].enb_mask;
	writel(val, shirq->regs.base + shirq->regs.enb_reg);
	spin_unlock_irqrestore(&lock, flags);
}

static struct irq_chip shirq_chip = {
	.name		= "spear_shirq",
	.irq_ack	= shirq_irq_mask,
	.irq_mask	= shirq_irq_mask,
	.irq_unmask	= shirq_irq_unmask,
};

static void shirq_handler(unsigned irq, struct irq_desc *desc)
{
	u32 i, val, mask;
	struct spear_shirq *shirq = get_irq_data(irq);

	desc->irq_data.chip->irq_ack(&desc->irq_data);
	while ((val = readl(shirq->regs.base + shirq->regs.status_reg) &
				shirq->regs.status_reg_mask)) {
		for (i = 0; (i < shirq->dev_count) && val; i++) {
			if (!(shirq->dev_config[i].status_mask & val))
				continue;

			generic_handle_irq(shirq->dev_config[i].virq);

			/* clear interrupt */
			val &= ~shirq->dev_config[i].status_mask;
			if ((shirq->regs.clear_reg == -1) ||
					shirq->dev_config[i].clear_mask == -1)
				continue;
			mask = readl(shirq->regs.base + shirq->regs.clear_reg);
			if (shirq->regs.reset_to_clear)
				mask &= ~shirq->dev_config[i].clear_mask;
			else
				mask |= shirq->dev_config[i].clear_mask;
			writel(mask, shirq->regs.base + shirq->regs.clear_reg);
		}
	}
	desc->irq_data.chip->irq_unmask(&desc->irq_data);
}

int spear_shirq_register(struct spear_shirq *shirq)
{
	int i;

	if (!shirq || !shirq->dev_config || !shirq->regs.base)
		return -EFAULT;

	if (!shirq->dev_count)
		return -EINVAL;

	set_irq_chained_handler(shirq->irq, shirq_handler);
	for (i = 0; i < shirq->dev_count; i++) {
		set_irq_chip(shirq->dev_config[i].virq, &shirq_chip);
		set_irq_handler(shirq->dev_config[i].virq, handle_simple_irq);
		set_irq_flags(shirq->dev_config[i].virq, IRQF_VALID);
		set_irq_chip_data(shirq->dev_config[i].virq, shirq);
	}

	set_irq_data(shirq->irq, shirq);
	return 0;
}
