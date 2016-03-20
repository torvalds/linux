/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Joe.C <yingjoe.chen@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

struct mtk_sysirq_chip_data {
	spinlock_t lock;
	void __iomem *intpol_base;
};

static int mtk_sysirq_set_type(struct irq_data *data, unsigned int type)
{
	irq_hw_number_t hwirq = data->hwirq;
	struct mtk_sysirq_chip_data *chip_data = data->chip_data;
	u32 offset, reg_index, value;
	unsigned long flags;
	int ret;

	offset = hwirq & 0x1f;
	reg_index = hwirq >> 5;

	spin_lock_irqsave(&chip_data->lock, flags);
	value = readl_relaxed(chip_data->intpol_base + reg_index * 4);
	if (type == IRQ_TYPE_LEVEL_LOW || type == IRQ_TYPE_EDGE_FALLING) {
		if (type == IRQ_TYPE_LEVEL_LOW)
			type = IRQ_TYPE_LEVEL_HIGH;
		else
			type = IRQ_TYPE_EDGE_RISING;
		value |= (1 << offset);
	} else {
		value &= ~(1 << offset);
	}
	writel(value, chip_data->intpol_base + reg_index * 4);

	data = data->parent_data;
	ret = data->chip->irq_set_type(data, type);
	spin_unlock_irqrestore(&chip_data->lock, flags);
	return ret;
}

static struct irq_chip mtk_sysirq_chip = {
	.name			= "MT_SYSIRQ",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_type		= mtk_sysirq_set_type,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
};

static int mtk_sysirq_domain_translate(struct irq_domain *d,
				       struct irq_fwspec *fwspec,
				       unsigned long *hwirq,
				       unsigned int *type)
{
	if (is_of_node(fwspec->fwnode)) {
		if (fwspec->param_count != 3)
			return -EINVAL;

		/* No PPI should point to this domain */
		if (fwspec->param[0] != 0)
			return -EINVAL;

		*hwirq = fwspec->param[1];
		*type = fwspec->param[2] & IRQ_TYPE_SENSE_MASK;
		return 0;
	}

	return -EINVAL;
}

static int mtk_sysirq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs, void *arg)
{
	int i;
	irq_hw_number_t hwirq;
	struct irq_fwspec *fwspec = arg;
	struct irq_fwspec gic_fwspec = *fwspec;

	if (fwspec->param_count != 3)
		return -EINVAL;

	/* sysirq doesn't support PPI */
	if (fwspec->param[0])
		return -EINVAL;

	hwirq = fwspec->param[1];
	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
					      &mtk_sysirq_chip,
					      domain->host_data);

	gic_fwspec.fwnode = domain->parent->fwnode;
	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, &gic_fwspec);
}

static const struct irq_domain_ops sysirq_domain_ops = {
	.translate	= mtk_sysirq_domain_translate,
	.alloc		= mtk_sysirq_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

static int __init mtk_sysirq_of_init(struct device_node *node,
				     struct device_node *parent)
{
	struct irq_domain *domain, *domain_parent;
	struct mtk_sysirq_chip_data *chip_data;
	int ret, size, intpol_num;
	struct resource res;

	domain_parent = irq_find_host(parent);
	if (!domain_parent) {
		pr_err("mtk_sysirq: interrupt-parent not found\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return ret;

	chip_data = kzalloc(sizeof(*chip_data), GFP_KERNEL);
	if (!chip_data)
		return -ENOMEM;

	size = resource_size(&res);
	intpol_num = size * 8;
	chip_data->intpol_base = ioremap(res.start, size);
	if (!chip_data->intpol_base) {
		pr_err("mtk_sysirq: unable to map sysirq register\n");
		ret = -ENXIO;
		goto out_free;
	}

	domain = irq_domain_add_hierarchy(domain_parent, 0, intpol_num, node,
					  &sysirq_domain_ops, chip_data);
	if (!domain) {
		ret = -ENOMEM;
		goto out_unmap;
	}
	spin_lock_init(&chip_data->lock);

	return 0;

out_unmap:
	iounmap(chip_data->intpol_base);
out_free:
	kfree(chip_data);
	return ret;
}
IRQCHIP_DECLARE(mtk_sysirq, "mediatek,mt6577-sysirq", mtk_sysirq_of_init);
