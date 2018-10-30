// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Microsemi Ocelot IRQ controller driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */
#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/interrupt.h>

#define ICPU_CFG_INTR_INTR_STICKY	0x10
#define ICPU_CFG_INTR_INTR_ENA		0x18
#define ICPU_CFG_INTR_INTR_ENA_CLR	0x1c
#define ICPU_CFG_INTR_INTR_ENA_SET	0x20
#define ICPU_CFG_INTR_DST_INTR_IDENT(x)	(0x38 + 0x4 * (x))
#define ICPU_CFG_INTR_INTR_TRIGGER(x)	(0x5c + 0x4 * (x))

#define OCELOT_NR_IRQ 24

static void ocelot_irq_unmask(struct irq_data *data)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	struct irq_chip_type *ct = irq_data_get_chip_type(data);
	unsigned int mask = data->mask;
	u32 val;

	irq_gc_lock(gc);
	val = irq_reg_readl(gc, ICPU_CFG_INTR_INTR_TRIGGER(0)) |
	      irq_reg_readl(gc, ICPU_CFG_INTR_INTR_TRIGGER(1));
	if (!(val & mask))
		irq_reg_writel(gc, mask, ICPU_CFG_INTR_INTR_STICKY);

	*ct->mask_cache &= ~mask;
	irq_reg_writel(gc, mask, ICPU_CFG_INTR_INTR_ENA_SET);
	irq_gc_unlock(gc);
}

static void ocelot_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_domain *d = irq_desc_get_handler_data(desc);
	struct irq_chip_generic *gc = irq_get_domain_generic_chip(d, 0);
	u32 reg = irq_reg_readl(gc, ICPU_CFG_INTR_DST_INTR_IDENT(0));

	chained_irq_enter(chip, desc);

	while (reg) {
		u32 hwirq = __fls(reg);

		generic_handle_irq(irq_find_mapping(d, hwirq));
		reg &= ~(BIT(hwirq));
	}

	chained_irq_exit(chip, desc);
}

static int __init ocelot_irq_init(struct device_node *node,
				  struct device_node *parent)
{
	struct irq_domain *domain;
	struct irq_chip_generic *gc;
	int parent_irq, ret;

	parent_irq = irq_of_parse_and_map(node, 0);
	if (!parent_irq)
		return -EINVAL;

	domain = irq_domain_add_linear(node, OCELOT_NR_IRQ,
				       &irq_generic_chip_ops, NULL);
	if (!domain) {
		pr_err("%s: unable to add irq domain\n", node->name);
		return -ENOMEM;
	}

	ret = irq_alloc_domain_generic_chips(domain, OCELOT_NR_IRQ, 1,
					     "icpu", handle_level_irq,
					     0, 0, 0);
	if (ret) {
		pr_err("%s: unable to alloc irq domain gc\n", node->name);
		goto err_domain_remove;
	}

	gc = irq_get_domain_generic_chip(domain, 0);
	gc->reg_base = of_iomap(node, 0);
	if (!gc->reg_base) {
		pr_err("%s: unable to map resource\n", node->name);
		ret = -ENOMEM;
		goto err_gc_free;
	}

	gc->chip_types[0].regs.ack = ICPU_CFG_INTR_INTR_STICKY;
	gc->chip_types[0].regs.mask = ICPU_CFG_INTR_INTR_ENA_CLR;
	gc->chip_types[0].chip.irq_ack = irq_gc_ack_set_bit;
	gc->chip_types[0].chip.irq_mask = irq_gc_mask_set_bit;
	gc->chip_types[0].chip.irq_unmask = ocelot_irq_unmask;

	/* Mask and ack all interrupts */
	irq_reg_writel(gc, 0, ICPU_CFG_INTR_INTR_ENA);
	irq_reg_writel(gc, 0xffffffff, ICPU_CFG_INTR_INTR_STICKY);

	irq_set_chained_handler_and_data(parent_irq, ocelot_irq_handler,
					 domain);

	return 0;

err_gc_free:
	irq_free_generic_chip(gc);

err_domain_remove:
	irq_domain_remove(domain);

	return ret;
}
IRQCHIP_DECLARE(ocelot_icpu, "mscc,ocelot-icpu-intr", ocelot_irq_init);
