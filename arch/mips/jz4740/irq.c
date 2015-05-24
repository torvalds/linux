/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform IRQ support
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General	 Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/of_irq.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <asm/io.h>

#include <asm/mach-jz4740/base.h>
#include <asm/mach-jz4740/irq.h>

#include "irq.h"

#include "../../drivers/irqchip/irqchip.h"

struct ingenic_intc_data {
	void __iomem *base;
};

#define JZ_REG_INTC_STATUS	0x00
#define JZ_REG_INTC_MASK	0x04
#define JZ_REG_INTC_SET_MASK	0x08
#define JZ_REG_INTC_CLEAR_MASK	0x0c
#define JZ_REG_INTC_PENDING	0x10

static irqreturn_t jz4740_cascade(int irq, void *data)
{
	struct ingenic_intc_data *intc = irq_get_handler_data(irq);
	uint32_t irq_reg;

	irq_reg = readl(intc->base + JZ_REG_INTC_PENDING);

	if (irq_reg)
		generic_handle_irq(__fls(irq_reg) + JZ4740_IRQ_BASE);

	return IRQ_HANDLED;
}

static void jz4740_irq_set_mask(struct irq_chip_generic *gc, uint32_t mask)
{
	struct irq_chip_regs *regs = &gc->chip_types->regs;

	writel(mask, gc->reg_base + regs->enable);
	writel(~mask, gc->reg_base + regs->disable);
}

void jz4740_irq_suspend(struct irq_data *data)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	jz4740_irq_set_mask(gc, gc->wake_active);
}

void jz4740_irq_resume(struct irq_data *data)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	jz4740_irq_set_mask(gc, gc->mask_cache);
}

static struct irqaction jz4740_cascade_action = {
	.handler = jz4740_cascade,
	.name = "JZ4740 cascade interrupt",
};

static int __init jz4740_intc_of_init(struct device_node *node,
	struct device_node *parent)
{
	struct ingenic_intc_data *intc;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	struct irq_domain *domain;
	int parent_irq, err = 0;

	intc = kzalloc(sizeof(*intc), GFP_KERNEL);
	if (!intc) {
		err = -ENOMEM;
		goto out_err;
	}

	parent_irq = irq_of_parse_and_map(node, 0);
	if (!parent_irq) {
		err = -EINVAL;
		goto out_free;
	}

	err = irq_set_handler_data(parent_irq, intc);
	if (err)
		goto out_unmap_irq;

	intc->base = ioremap(JZ4740_INTC_BASE_ADDR, 0x14);

	/* Mask all irqs */
	writel(0xffffffff, intc->base + JZ_REG_INTC_SET_MASK);

	gc = irq_alloc_generic_chip("INTC", 1, JZ4740_IRQ_BASE, intc->base,
		handle_level_irq);

	gc->wake_enabled = IRQ_MSK(32);

	ct = gc->chip_types;
	ct->regs.enable = JZ_REG_INTC_CLEAR_MASK;
	ct->regs.disable = JZ_REG_INTC_SET_MASK;
	ct->chip.irq_unmask = irq_gc_unmask_enable_reg;
	ct->chip.irq_mask = irq_gc_mask_disable_reg;
	ct->chip.irq_mask_ack = irq_gc_mask_disable_reg;
	ct->chip.irq_set_wake = irq_gc_set_wake;
	ct->chip.irq_suspend = jz4740_irq_suspend;
	ct->chip.irq_resume = jz4740_irq_resume;

	irq_setup_generic_chip(gc, IRQ_MSK(32), 0, 0, IRQ_NOPROBE | IRQ_LEVEL);

	domain = irq_domain_add_legacy(node, num_chips * 32, JZ4740_IRQ_BASE, 0,
				       &irq_domain_simple_ops, NULL);
	if (!domain)
		pr_warn("unable to register IRQ domain\n");

	setup_irq(parent_irq, &jz4740_cascade_action);
	return 0;

out_unmap_irq:
	irq_dispose_mapping(parent_irq);
out_free:
	kfree(intc);
out_err:
	return err;
}
IRQCHIP_DECLARE(jz4740_intc, "ingenic,jz4740-intc", jz4740_intc_of_init);
