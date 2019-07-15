// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015-2016 Vladimir Zapolskiy <vz@mleia.com>
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <asm/exception.h>

#define LPC32XX_INTC_MASK		0x00
#define LPC32XX_INTC_RAW		0x04
#define LPC32XX_INTC_STAT		0x08
#define LPC32XX_INTC_POL		0x0C
#define LPC32XX_INTC_TYPE		0x10
#define LPC32XX_INTC_FIQ		0x14

#define NR_LPC32XX_IC_IRQS		32

struct lpc32xx_irq_chip {
	void __iomem *base;
	struct irq_domain *domain;
	struct irq_chip chip;
};

static struct lpc32xx_irq_chip *lpc32xx_mic_irqc;

static inline u32 lpc32xx_ic_read(struct lpc32xx_irq_chip *ic, u32 reg)
{
	return readl_relaxed(ic->base + reg);
}

static inline void lpc32xx_ic_write(struct lpc32xx_irq_chip *ic,
				    u32 reg, u32 val)
{
	writel_relaxed(val, ic->base + reg);
}

static void lpc32xx_irq_mask(struct irq_data *d)
{
	struct lpc32xx_irq_chip *ic = irq_data_get_irq_chip_data(d);
	u32 val, mask = BIT(d->hwirq);

	val = lpc32xx_ic_read(ic, LPC32XX_INTC_MASK) & ~mask;
	lpc32xx_ic_write(ic, LPC32XX_INTC_MASK, val);
}

static void lpc32xx_irq_unmask(struct irq_data *d)
{
	struct lpc32xx_irq_chip *ic = irq_data_get_irq_chip_data(d);
	u32 val, mask = BIT(d->hwirq);

	val = lpc32xx_ic_read(ic, LPC32XX_INTC_MASK) | mask;
	lpc32xx_ic_write(ic, LPC32XX_INTC_MASK, val);
}

static void lpc32xx_irq_ack(struct irq_data *d)
{
	struct lpc32xx_irq_chip *ic = irq_data_get_irq_chip_data(d);
	u32 mask = BIT(d->hwirq);

	lpc32xx_ic_write(ic, LPC32XX_INTC_RAW, mask);
}

static int lpc32xx_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct lpc32xx_irq_chip *ic = irq_data_get_irq_chip_data(d);
	u32 val, mask = BIT(d->hwirq);
	bool high, edge;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		edge = true;
		high = true;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		edge = true;
		high = false;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		edge = false;
		high = true;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		edge = false;
		high = false;
		break;
	default:
		pr_info("unsupported irq type %d\n", type);
		return -EINVAL;
	}

	irqd_set_trigger_type(d, type);

	val = lpc32xx_ic_read(ic, LPC32XX_INTC_POL);
	if (high)
		val |= mask;
	else
		val &= ~mask;
	lpc32xx_ic_write(ic, LPC32XX_INTC_POL, val);

	val = lpc32xx_ic_read(ic, LPC32XX_INTC_TYPE);
	if (edge) {
		val |= mask;
		irq_set_handler_locked(d, handle_edge_irq);
	} else {
		val &= ~mask;
		irq_set_handler_locked(d, handle_level_irq);
	}
	lpc32xx_ic_write(ic, LPC32XX_INTC_TYPE, val);

	return 0;
}

static void __exception_irq_entry lpc32xx_handle_irq(struct pt_regs *regs)
{
	struct lpc32xx_irq_chip *ic = lpc32xx_mic_irqc;
	u32 hwirq = lpc32xx_ic_read(ic, LPC32XX_INTC_STAT), irq;

	while (hwirq) {
		irq = __ffs(hwirq);
		hwirq &= ~BIT(irq);
		handle_domain_irq(lpc32xx_mic_irqc->domain, irq, regs);
	}
}

static void lpc32xx_sic_handler(struct irq_desc *desc)
{
	struct lpc32xx_irq_chip *ic = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u32 hwirq = lpc32xx_ic_read(ic, LPC32XX_INTC_STAT), irq;

	chained_irq_enter(chip, desc);

	while (hwirq) {
		irq = __ffs(hwirq);
		hwirq &= ~BIT(irq);
		generic_handle_irq(irq_find_mapping(ic->domain, irq));
	}

	chained_irq_exit(chip, desc);
}

static int lpc32xx_irq_domain_map(struct irq_domain *id, unsigned int virq,
				  irq_hw_number_t hw)
{
	struct lpc32xx_irq_chip *ic = id->host_data;

	irq_set_chip_data(virq, ic);
	irq_set_chip_and_handler(virq, &ic->chip, handle_level_irq);
	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_noprobe(virq);

	return 0;
}

static void lpc32xx_irq_domain_unmap(struct irq_domain *id, unsigned int virq)
{
	irq_set_chip_and_handler(virq, NULL, NULL);
}

static const struct irq_domain_ops lpc32xx_irq_domain_ops = {
	.map    = lpc32xx_irq_domain_map,
	.unmap	= lpc32xx_irq_domain_unmap,
	.xlate  = irq_domain_xlate_twocell,
};

static int __init lpc32xx_of_ic_init(struct device_node *node,
				     struct device_node *parent)
{
	struct lpc32xx_irq_chip *irqc;
	bool is_mic = of_device_is_compatible(node, "nxp,lpc3220-mic");
	const __be32 *reg = of_get_property(node, "reg", NULL);
	u32 parent_irq, i, addr = reg ? be32_to_cpu(*reg) : 0;

	irqc = kzalloc(sizeof(*irqc), GFP_KERNEL);
	if (!irqc)
		return -ENOMEM;

	irqc->base = of_iomap(node, 0);
	if (!irqc->base) {
		pr_err("%pOF: unable to map registers\n", node);
		kfree(irqc);
		return -EINVAL;
	}

	irqc->chip.irq_ack = lpc32xx_irq_ack;
	irqc->chip.irq_mask = lpc32xx_irq_mask;
	irqc->chip.irq_unmask = lpc32xx_irq_unmask;
	irqc->chip.irq_set_type = lpc32xx_irq_set_type;
	if (is_mic)
		irqc->chip.name = kasprintf(GFP_KERNEL, "%08x.mic", addr);
	else
		irqc->chip.name = kasprintf(GFP_KERNEL, "%08x.sic", addr);

	irqc->domain = irq_domain_add_linear(node, NR_LPC32XX_IC_IRQS,
					     &lpc32xx_irq_domain_ops, irqc);
	if (!irqc->domain) {
		pr_err("unable to add irq domain\n");
		iounmap(irqc->base);
		kfree(irqc->chip.name);
		kfree(irqc);
		return -ENODEV;
	}

	if (is_mic) {
		lpc32xx_mic_irqc = irqc;
		set_handle_irq(lpc32xx_handle_irq);
	} else {
		for (i = 0; i < of_irq_count(node); i++) {
			parent_irq = irq_of_parse_and_map(node, i);
			if (parent_irq)
				irq_set_chained_handler_and_data(parent_irq,
						 lpc32xx_sic_handler, irqc);
		}
	}

	lpc32xx_ic_write(irqc, LPC32XX_INTC_MASK, 0x00);
	lpc32xx_ic_write(irqc, LPC32XX_INTC_POL,  0x00);
	lpc32xx_ic_write(irqc, LPC32XX_INTC_TYPE, 0x00);

	return 0;
}

IRQCHIP_DECLARE(nxp_lpc32xx_mic, "nxp,lpc3220-mic", lpc32xx_of_ic_init);
IRQCHIP_DECLARE(nxp_lpc32xx_sic, "nxp,lpc3220-sic", lpc32xx_of_ic_init);
