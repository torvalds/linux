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

#define ICPU_CFG_INTR_DST_INTR_IDENT(_p, x) ((_p)->reg_off_ident + 0x4 * (x))
#define ICPU_CFG_INTR_INTR_TRIGGER(_p, x)   ((_p)->reg_off_trigger + 0x4 * (x))

#define FLAGS_HAS_TRIGGER	BIT(0)
#define FLAGS_NEED_INIT_ENABLE	BIT(1)

struct chip_props {
	u8 flags;
	u8 reg_off_sticky;
	u8 reg_off_ena;
	u8 reg_off_ena_clr;
	u8 reg_off_ena_set;
	u8 reg_off_ident;
	u8 reg_off_trigger;
	u8 reg_off_ena_irq0;
	u8 n_irq;
};

static struct chip_props ocelot_props = {
	.flags			= FLAGS_HAS_TRIGGER,
	.reg_off_sticky		= 0x10,
	.reg_off_ena		= 0x18,
	.reg_off_ena_clr	= 0x1c,
	.reg_off_ena_set	= 0x20,
	.reg_off_ident		= 0x38,
	.reg_off_trigger	= 0x5c,
	.n_irq			= 24,
};

static struct chip_props serval_props = {
	.flags			= FLAGS_HAS_TRIGGER,
	.reg_off_sticky		= 0xc,
	.reg_off_ena		= 0x14,
	.reg_off_ena_clr	= 0x18,
	.reg_off_ena_set	= 0x1c,
	.reg_off_ident		= 0x20,
	.reg_off_trigger	= 0x4,
	.n_irq			= 24,
};

static struct chip_props luton_props = {
	.flags			= FLAGS_NEED_INIT_ENABLE,
	.reg_off_sticky		= 0,
	.reg_off_ena		= 0x4,
	.reg_off_ena_clr	= 0x8,
	.reg_off_ena_set	= 0xc,
	.reg_off_ident		= 0x18,
	.reg_off_ena_irq0	= 0x14,
	.n_irq			= 28,
};

static struct chip_props jaguar2_props = {
	.flags			= FLAGS_HAS_TRIGGER,
	.reg_off_sticky		= 0x10,
	.reg_off_ena		= 0x18,
	.reg_off_ena_clr	= 0x1c,
	.reg_off_ena_set	= 0x20,
	.reg_off_ident		= 0x38,
	.reg_off_trigger	= 0x5c,
	.n_irq			= 29,
};

static void ocelot_irq_unmask(struct irq_data *data)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	struct irq_domain *d = data->domain;
	struct chip_props *p = d->host_data;
	struct irq_chip_type *ct = irq_data_get_chip_type(data);
	unsigned int mask = data->mask;
	u32 val;

	irq_gc_lock(gc);
	val = irq_reg_readl(gc, ICPU_CFG_INTR_INTR_TRIGGER(p, 0)) |
		irq_reg_readl(gc, ICPU_CFG_INTR_INTR_TRIGGER(p, 1));
	if (!(val & mask))
		irq_reg_writel(gc, mask, p->reg_off_sticky);

	*ct->mask_cache &= ~mask;
	irq_reg_writel(gc, mask, p->reg_off_ena_set);
	irq_gc_unlock(gc);
}

static void ocelot_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct irq_domain *d = irq_desc_get_handler_data(desc);
	struct chip_props *p = d->host_data;
	struct irq_chip_generic *gc = irq_get_domain_generic_chip(d, 0);
	u32 reg = irq_reg_readl(gc, ICPU_CFG_INTR_DST_INTR_IDENT(p, 0));

	chained_irq_enter(chip, desc);

	while (reg) {
		u32 hwirq = __fls(reg);

		generic_handle_domain_irq(d, hwirq);
		reg &= ~(BIT(hwirq));
	}

	chained_irq_exit(chip, desc);
}

static int __init vcoreiii_irq_init(struct device_node *node,
				    struct device_node *parent,
				    struct chip_props *p)
{
	struct irq_domain *domain;
	struct irq_chip_generic *gc;
	int parent_irq, ret;

	parent_irq = irq_of_parse_and_map(node, 0);
	if (!parent_irq)
		return -EINVAL;

	domain = irq_domain_add_linear(node, p->n_irq,
				       &irq_generic_chip_ops, NULL);
	if (!domain) {
		pr_err("%pOFn: unable to add irq domain\n", node);
		return -ENOMEM;
	}

	ret = irq_alloc_domain_generic_chips(domain, p->n_irq, 1,
					     "icpu", handle_level_irq,
					     0, 0, 0);
	if (ret) {
		pr_err("%pOFn: unable to alloc irq domain gc\n", node);
		goto err_domain_remove;
	}

	gc = irq_get_domain_generic_chip(domain, 0);
	gc->reg_base = of_iomap(node, 0);
	if (!gc->reg_base) {
		pr_err("%pOFn: unable to map resource\n", node);
		ret = -ENOMEM;
		goto err_gc_free;
	}

	gc->chip_types[0].chip.irq_ack = irq_gc_ack_set_bit;
	gc->chip_types[0].regs.ack = p->reg_off_sticky;
	if (p->flags & FLAGS_HAS_TRIGGER) {
		gc->chip_types[0].regs.mask = p->reg_off_ena_clr;
		gc->chip_types[0].chip.irq_unmask = ocelot_irq_unmask;
		gc->chip_types[0].chip.irq_mask = irq_gc_mask_set_bit;
	} else {
		gc->chip_types[0].regs.enable = p->reg_off_ena_set;
		gc->chip_types[0].regs.disable = p->reg_off_ena_clr;
		gc->chip_types[0].chip.irq_mask = irq_gc_mask_disable_reg;
		gc->chip_types[0].chip.irq_unmask = irq_gc_unmask_enable_reg;
	}

	/* Mask and ack all interrupts */
	irq_reg_writel(gc, 0, p->reg_off_ena);
	irq_reg_writel(gc, 0xffffffff, p->reg_off_sticky);

	/* Overall init */
	if (p->flags & FLAGS_NEED_INIT_ENABLE)
		irq_reg_writel(gc, BIT(0), p->reg_off_ena_irq0);

	domain->host_data = p;
	irq_set_chained_handler_and_data(parent_irq, ocelot_irq_handler,
					 domain);

	return 0;

err_gc_free:
	irq_free_generic_chip(gc);

err_domain_remove:
	irq_domain_remove(domain);

	return ret;
}

static int __init ocelot_irq_init(struct device_node *node,
				  struct device_node *parent)
{
	return vcoreiii_irq_init(node, parent, &ocelot_props);
}

IRQCHIP_DECLARE(ocelot_icpu, "mscc,ocelot-icpu-intr", ocelot_irq_init);

static int __init serval_irq_init(struct device_node *node,
				  struct device_node *parent)
{
	return vcoreiii_irq_init(node, parent, &serval_props);
}

IRQCHIP_DECLARE(serval_icpu, "mscc,serval-icpu-intr", serval_irq_init);

static int __init luton_irq_init(struct device_node *node,
				 struct device_node *parent)
{
	return vcoreiii_irq_init(node, parent, &luton_props);
}

IRQCHIP_DECLARE(luton_icpu, "mscc,luton-icpu-intr", luton_irq_init);

static int __init jaguar2_irq_init(struct device_node *node,
				   struct device_node *parent)
{
	return vcoreiii_irq_init(node, parent, &jaguar2_props);
}

IRQCHIP_DECLARE(jaguar2_icpu, "mscc,jaguar2-icpu-intr", jaguar2_irq_init);
