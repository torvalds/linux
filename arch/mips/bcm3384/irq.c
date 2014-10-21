/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Partially based on arch/mips/ralink/irq.c
 *
 * Copyright (C) 2009 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2013 John Crispin <blogic@openwrt.org>
 * Copyright (C) 2014 Kevin Cernekee <cernekee@gmail.com>
 */

#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <asm/bmips.h>
#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>

/* INTC register offsets */
#define INTC_REG_ENABLE		0x00
#define INTC_REG_STATUS		0x04

#define MAX_WORDS		2
#define IRQS_PER_WORD		32

struct bcm3384_intc {
	int			n_words;
	void __iomem		*reg[MAX_WORDS];
	u32			enable[MAX_WORDS];
	spinlock_t		lock;
};

static void bcm3384_intc_irq_unmask(struct irq_data *d)
{
	struct bcm3384_intc *priv = d->domain->host_data;
	unsigned long flags;
	int idx = d->hwirq / IRQS_PER_WORD;
	int bit = d->hwirq % IRQS_PER_WORD;

	spin_lock_irqsave(&priv->lock, flags);
	priv->enable[idx] |= BIT(bit);
	__raw_writel(priv->enable[idx], priv->reg[idx] + INTC_REG_ENABLE);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void bcm3384_intc_irq_mask(struct irq_data *d)
{
	struct bcm3384_intc *priv = d->domain->host_data;
	unsigned long flags;
	int idx = d->hwirq / IRQS_PER_WORD;
	int bit = d->hwirq % IRQS_PER_WORD;

	spin_lock_irqsave(&priv->lock, flags);
	priv->enable[idx] &= ~BIT(bit);
	__raw_writel(priv->enable[idx], priv->reg[idx] + INTC_REG_ENABLE);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static struct irq_chip bcm3384_intc_irq_chip = {
	.name		= "INTC",
	.irq_unmask	= bcm3384_intc_irq_unmask,
	.irq_mask	= bcm3384_intc_irq_mask,
	.irq_mask_ack	= bcm3384_intc_irq_mask,
};

unsigned int get_c0_compare_int(void)
{
	return CP0_LEGACY_COMPARE_IRQ;
}

static void bcm3384_intc_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct irq_domain *domain = irq_get_handler_data(irq);
	struct bcm3384_intc *priv = domain->host_data;
	unsigned long flags;
	unsigned int idx;

	for (idx = 0; idx < priv->n_words; idx++) {
		unsigned long pending;
		int hwirq;

		spin_lock_irqsave(&priv->lock, flags);
		pending = __raw_readl(priv->reg[idx] + INTC_REG_STATUS) &
			  priv->enable[idx];
		spin_unlock_irqrestore(&priv->lock, flags);

		for_each_set_bit(hwirq, &pending, IRQS_PER_WORD) {
			generic_handle_irq(irq_find_mapping(domain,
					   hwirq + idx * IRQS_PER_WORD));
		}
	}
}

asmlinkage void plat_irq_dispatch(void)
{
	unsigned long pending =
		(read_c0_status() & read_c0_cause() & ST0_IM) >> STATUSB_IP0;
	int bit;

	for_each_set_bit(bit, &pending, 8)
		do_IRQ(MIPS_CPU_IRQ_BASE + bit);
}

static int intc_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &bcm3384_intc_irq_chip, handle_level_irq);
	return 0;
}

static const struct irq_domain_ops irq_domain_ops = {
	.xlate = irq_domain_xlate_onecell,
	.map = intc_map,
};

static int __init ioremap_one_pair(struct bcm3384_intc *priv,
				   struct device_node *node,
				   int idx)
{
	struct resource res;

	if (of_address_to_resource(node, idx, &res))
		return 0;

	if (request_mem_region(res.start, resource_size(&res),
			       res.name) < 0)
		pr_err("Failed to request INTC register region\n");

	priv->reg[idx] = ioremap_nocache(res.start, resource_size(&res));
	if (!priv->reg[idx])
		panic("Failed to ioremap INTC register range");

	/* start up with everything masked before we hook the parent IRQ */
	__raw_writel(0, priv->reg[idx] + INTC_REG_ENABLE);
	priv->enable[idx] = 0;

	return IRQS_PER_WORD;
}

static int __init intc_of_init(struct device_node *node,
			       struct device_node *parent)
{
	struct irq_domain *domain;
	unsigned int parent_irq, n_irqs = 0;
	struct bcm3384_intc *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		panic("Failed to allocate bcm3384_intc struct");

	spin_lock_init(&priv->lock);

	parent_irq = irq_of_parse_and_map(node, 0);
	if (!parent_irq)
		panic("Failed to get INTC IRQ");

	n_irqs += ioremap_one_pair(priv, node, 0);
	n_irqs += ioremap_one_pair(priv, node, 1);

	if (!n_irqs)
		panic("Failed to map INTC registers");

	priv->n_words = n_irqs / IRQS_PER_WORD;
	domain = irq_domain_add_linear(node, n_irqs, &irq_domain_ops, priv);
	if (!domain)
		panic("Failed to add irqdomain");

	irq_set_chained_handler(parent_irq, bcm3384_intc_irq_handler);
	irq_set_handler_data(parent_irq, domain);

	return 0;
}

static struct of_device_id of_irq_ids[] __initdata = {
	{ .compatible = "mti,cpu-interrupt-controller",
	  .data = mips_cpu_intc_init },
	{ .compatible = "brcm,bcm3384-intc",
	  .data = intc_of_init },
	{},
};

void __init arch_init_irq(void)
{
	bmips_tp1_irqs = 0;
	of_irq_init(of_irq_ids);
}
