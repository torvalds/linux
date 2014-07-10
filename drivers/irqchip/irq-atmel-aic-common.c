/*
 * Atmel AT91 common AIC (Advanced Interrupt Controller) code shared by
 * irq-atmel-aic and irq-atmel-aic5 drivers
 *
 *  Copyright (C) 2004 SAN People
 *  Copyright (C) 2004 ATMEL
 *  Copyright (C) Rick Bronson
 *  Copyright (C) 2014 Free Electrons
 *
 *  Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include "irq-atmel-aic-common.h"

#define AT91_AIC_PRIOR			GENMASK(2, 0)
#define AT91_AIC_IRQ_MIN_PRIORITY	0
#define AT91_AIC_IRQ_MAX_PRIORITY	7

#define AT91_AIC_SRCTYPE		GENMASK(7, 6)
#define AT91_AIC_SRCTYPE_LOW		(0 << 5)
#define AT91_AIC_SRCTYPE_FALLING	(1 << 5)
#define AT91_AIC_SRCTYPE_HIGH		(2 << 5)
#define AT91_AIC_SRCTYPE_RISING		(3 << 5)

struct aic_chip_data {
	u32 ext_irqs;
};

static void aic_common_shutdown(struct irq_data *d)
{
	struct irq_chip_type *ct = irq_data_get_chip_type(d);

	ct->chip.irq_mask(d);
}

int aic_common_set_type(struct irq_data *d, unsigned type, unsigned *val)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct aic_chip_data *aic = gc->private;
	unsigned aic_type;

	switch (type) {
	case IRQ_TYPE_LEVEL_HIGH:
		aic_type = AT91_AIC_SRCTYPE_HIGH;
		break;
	case IRQ_TYPE_EDGE_RISING:
		aic_type = AT91_AIC_SRCTYPE_RISING;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		if (!(d->mask & aic->ext_irqs))
			return -EINVAL;

		aic_type = AT91_AIC_SRCTYPE_LOW;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		if (!(d->mask & aic->ext_irqs))
			return -EINVAL;

		aic_type = AT91_AIC_SRCTYPE_FALLING;
		break;
	default:
		return -EINVAL;
	}

	*val &= AT91_AIC_SRCTYPE;
	*val |= aic_type;

	return 0;
}

int aic_common_set_priority(int priority, unsigned *val)
{
	if (priority < AT91_AIC_IRQ_MIN_PRIORITY ||
	    priority > AT91_AIC_IRQ_MAX_PRIORITY)
		return -EINVAL;

	*val &= AT91_AIC_PRIOR;
	*val |= priority;

	return 0;
}

int aic_common_irq_domain_xlate(struct irq_domain *d,
				struct device_node *ctrlr,
				const u32 *intspec,
				unsigned int intsize,
				irq_hw_number_t *out_hwirq,
				unsigned int *out_type)
{
	if (WARN_ON(intsize < 3))
		return -EINVAL;

	if (WARN_ON((intspec[2] < AT91_AIC_IRQ_MIN_PRIORITY) ||
		    (intspec[2] > AT91_AIC_IRQ_MAX_PRIORITY)))
		return -EINVAL;

	*out_hwirq = intspec[0];
	*out_type = intspec[1] & IRQ_TYPE_SENSE_MASK;

	return 0;
}

static void __init aic_common_ext_irq_of_init(struct irq_domain *domain)
{
	struct device_node *node = domain->of_node;
	struct irq_chip_generic *gc;
	struct aic_chip_data *aic;
	struct property *prop;
	const __be32 *p;
	u32 hwirq;

	gc = irq_get_domain_generic_chip(domain, 0);

	aic = gc->private;
	aic->ext_irqs |= 1;

	of_property_for_each_u32(node, "atmel,external-irqs", prop, p, hwirq) {
		gc = irq_get_domain_generic_chip(domain, hwirq);
		if (!gc) {
			pr_warn("AIC: external irq %d >= %d skip it\n",
				hwirq, domain->revmap_size);
			continue;
		}

		aic = gc->private;
		aic->ext_irqs |= (1 << (hwirq % 32));
	}
}

void __init aic_common_irq_fixup(const struct of_device_id *matches)
{
	struct device_node *root = of_find_node_by_path("/");
	const struct of_device_id *match;

	if (!root)
		return;

	match = of_match_node(matches, root);
	of_node_put(root);

	if (match) {
		void (*fixup)(struct device_node *) = match->data;
		fixup(root);
	}

	of_node_put(root);
}

struct irq_domain *__init aic_common_of_init(struct device_node *node,
					     const struct irq_domain_ops *ops,
					     const char *name, int nirqs)
{
	struct irq_chip_generic *gc;
	struct irq_domain *domain;
	struct aic_chip_data *aic;
	void __iomem *reg_base;
	int nchips;
	int ret;
	int i;

	nchips = DIV_ROUND_UP(nirqs, 32);

	reg_base = of_iomap(node, 0);
	if (!reg_base)
		return ERR_PTR(-ENOMEM);

	aic = kcalloc(nchips, sizeof(*aic), GFP_KERNEL);
	if (!aic) {
		ret = -ENOMEM;
		goto err_iounmap;
	}

	domain = irq_domain_add_linear(node, nchips * 32, ops, aic);
	if (!domain) {
		ret = -ENOMEM;
		goto err_free_aic;
	}

	ret = irq_alloc_domain_generic_chips(domain, 32, 1, name,
					     handle_level_irq, 0, 0,
					     IRQCHIP_SKIP_SET_WAKE);
	if (ret)
		goto err_domain_remove;

	for (i = 0; i < nchips; i++) {
		gc = irq_get_domain_generic_chip(domain, i * 32);

		gc->reg_base = reg_base;

		gc->unused = 0;
		gc->wake_enabled = ~0;
		gc->chip_types[0].type = IRQ_TYPE_SENSE_MASK;
		gc->chip_types[0].handler = handle_fasteoi_irq;
		gc->chip_types[0].chip.irq_eoi = irq_gc_eoi;
		gc->chip_types[0].chip.irq_set_wake = irq_gc_set_wake;
		gc->chip_types[0].chip.irq_shutdown = aic_common_shutdown;
		gc->private = &aic[i];
	}

	aic_common_ext_irq_of_init(domain);

	return domain;

err_domain_remove:
	irq_domain_remove(domain);

err_free_aic:
	kfree(aic);

err_iounmap:
	iounmap(reg_base);

	return ERR_PTR(ret);
}
