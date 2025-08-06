// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013 Altera Corporation
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2008 Thomas Chou <thomas@wytron.com.tw>
 *
 * based on irq.c from m68k which is:
 *
 * Copyright (C) 2007 Greg Ungerer <gerg@snapgear.com>
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/of.h>

static u32 ienable;

asmlinkage void do_IRQ(int hwirq, struct pt_regs *regs)
{
	struct pt_regs *oldregs = set_irq_regs(regs);

	irq_enter();
	generic_handle_domain_irq(NULL, hwirq);
	irq_exit();

	set_irq_regs(oldregs);
}

static void chip_unmask(struct irq_data *d)
{
	ienable |= (1 << d->hwirq);
	WRCTL(CTL_IENABLE, ienable);
}

static void chip_mask(struct irq_data *d)
{
	ienable &= ~(1 << d->hwirq);
	WRCTL(CTL_IENABLE, ienable);
}

static struct irq_chip m_irq_chip = {
	.name		= "NIOS2-INTC",
	.irq_unmask	= chip_unmask,
	.irq_mask	= chip_mask,
};

static int irq_map(struct irq_domain *h, unsigned int virq,
				irq_hw_number_t hw_irq_num)
{
	irq_set_chip_and_handler(virq, &m_irq_chip, handle_level_irq);

	return 0;
}

static const struct irq_domain_ops irq_ops = {
	.map	= irq_map,
	.xlate	= irq_domain_xlate_onecell,
};

void __init init_IRQ(void)
{
	struct irq_domain *domain;
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "altr,nios2-1.0");
	if (!node)
		node = of_find_compatible_node(NULL, NULL, "altr,nios2-1.1");

	BUG_ON(!node);

	domain = irq_domain_create_linear(of_fwnode_handle(node),
					  NIOS2_CPU_NR_IRQS, &irq_ops, NULL);
	BUG_ON(!domain);

	irq_set_default_domain(domain);
	of_node_put(node);
	/* Load the initial ienable value */
	ienable = RDCTL(CTL_IENABLE);
}
