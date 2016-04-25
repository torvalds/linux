/*
 *  drivers/irqchip/irq-crossbar.c
 *
 *  Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 *  Author: Sricharan R <r.sricharan@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>

#define IRQ_FREE	-1
#define IRQ_RESERVED	-2
#define IRQ_SKIP	-3
#define GIC_IRQ_START	32

/**
 * struct crossbar_device - crossbar device description
 * @lock: spinlock serializing access to @irq_map
 * @int_max: maximum number of supported interrupts
 * @safe_map: safe default value to initialize the crossbar
 * @max_crossbar_sources: Maximum number of crossbar sources
 * @irq_map: array of interrupts to crossbar number mapping
 * @crossbar_base: crossbar base address
 * @register_offsets: offsets for each irq number
 * @write: register write function pointer
 */
struct crossbar_device {
	raw_spinlock_t lock;
	uint int_max;
	uint safe_map;
	uint max_crossbar_sources;
	uint *irq_map;
	void __iomem *crossbar_base;
	int *register_offsets;
	void (*write)(int, int);
};

static struct crossbar_device *cb;

static void crossbar_writel(int irq_no, int cb_no)
{
	writel(cb_no, cb->crossbar_base + cb->register_offsets[irq_no]);
}

static void crossbar_writew(int irq_no, int cb_no)
{
	writew(cb_no, cb->crossbar_base + cb->register_offsets[irq_no]);
}

static void crossbar_writeb(int irq_no, int cb_no)
{
	writeb(cb_no, cb->crossbar_base + cb->register_offsets[irq_no]);
}

static struct irq_chip crossbar_chip = {
	.name			= "CBAR",
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_type		= irq_chip_set_type_parent,
	.flags			= IRQCHIP_MASK_ON_SUSPEND |
				  IRQCHIP_SKIP_SET_WAKE,
#ifdef CONFIG_SMP
	.irq_set_affinity	= irq_chip_set_affinity_parent,
#endif
};

static int allocate_gic_irq(struct irq_domain *domain, unsigned virq,
			    irq_hw_number_t hwirq)
{
	struct irq_fwspec fwspec;
	int i;
	int err;

	if (!irq_domain_get_of_node(domain->parent))
		return -EINVAL;

	raw_spin_lock(&cb->lock);
	for (i = cb->int_max - 1; i >= 0; i--) {
		if (cb->irq_map[i] == IRQ_FREE) {
			cb->irq_map[i] = hwirq;
			break;
		}
	}
	raw_spin_unlock(&cb->lock);

	if (i < 0)
		return -ENODEV;

	fwspec.fwnode = domain->parent->fwnode;
	fwspec.param_count = 3;
	fwspec.param[0] = 0;	/* SPI */
	fwspec.param[1] = i;
	fwspec.param[2] = IRQ_TYPE_LEVEL_HIGH;

	err = irq_domain_alloc_irqs_parent(domain, virq, 1, &fwspec);
	if (err)
		cb->irq_map[i] = IRQ_FREE;
	else
		cb->write(i, hwirq);

	return err;
}

static int crossbar_domain_alloc(struct irq_domain *d, unsigned int virq,
				 unsigned int nr_irqs, void *data)
{
	struct irq_fwspec *fwspec = data;
	irq_hw_number_t hwirq;
	int i;

	if (fwspec->param_count != 3)
		return -EINVAL;	/* Not GIC compliant */
	if (fwspec->param[0] != 0)
		return -EINVAL;	/* No PPI should point to this domain */

	hwirq = fwspec->param[1];
	if ((hwirq + nr_irqs) > cb->max_crossbar_sources)
		return -EINVAL;	/* Can't deal with this */

	for (i = 0; i < nr_irqs; i++) {
		int err = allocate_gic_irq(d, virq + i, hwirq + i);

		if (err)
			return err;

		irq_domain_set_hwirq_and_chip(d, virq + i, hwirq + i,
					      &crossbar_chip, NULL);
	}

	return 0;
}

/**
 * crossbar_domain_free - unmap/free a crossbar<->irq connection
 * @domain: domain of irq to unmap
 * @virq: virq number
 * @nr_irqs: number of irqs to free
 *
 * We do not maintain a use count of total number of map/unmap
 * calls for a particular irq to find out if a irq can be really
 * unmapped. This is because unmap is called during irq_dispose_mapping(irq),
 * after which irq is anyways unusable. So an explicit map has to be called
 * after that.
 */
static void crossbar_domain_free(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs)
{
	int i;

	raw_spin_lock(&cb->lock);
	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *d = irq_domain_get_irq_data(domain, virq + i);

		irq_domain_reset_irq_data(d);
		cb->irq_map[d->hwirq] = IRQ_FREE;
		cb->write(d->hwirq, cb->safe_map);
	}
	raw_spin_unlock(&cb->lock);
}

static int crossbar_domain_translate(struct irq_domain *d,
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
		*type = fwspec->param[2];
		return 0;
	}

	return -EINVAL;
}

static const struct irq_domain_ops crossbar_domain_ops = {
	.alloc		= crossbar_domain_alloc,
	.free		= crossbar_domain_free,
	.translate	= crossbar_domain_translate,
};

static int __init crossbar_of_init(struct device_node *node)
{
	int i, size, max = 0, reserved = 0, entry;
	const __be32 *irqsr;
	int ret = -ENOMEM;

	cb = kzalloc(sizeof(*cb), GFP_KERNEL);

	if (!cb)
		return ret;

	cb->crossbar_base = of_iomap(node, 0);
	if (!cb->crossbar_base)
		goto err_cb;

	of_property_read_u32(node, "ti,max-crossbar-sources",
			     &cb->max_crossbar_sources);
	if (!cb->max_crossbar_sources) {
		pr_err("missing 'ti,max-crossbar-sources' property\n");
		ret = -EINVAL;
		goto err_base;
	}

	of_property_read_u32(node, "ti,max-irqs", &max);
	if (!max) {
		pr_err("missing 'ti,max-irqs' property\n");
		ret = -EINVAL;
		goto err_base;
	}
	cb->irq_map = kcalloc(max, sizeof(int), GFP_KERNEL);
	if (!cb->irq_map)
		goto err_base;

	cb->int_max = max;

	for (i = 0; i < max; i++)
		cb->irq_map[i] = IRQ_FREE;

	/* Get and mark reserved irqs */
	irqsr = of_get_property(node, "ti,irqs-reserved", &size);
	if (irqsr) {
		size /= sizeof(__be32);

		for (i = 0; i < size; i++) {
			of_property_read_u32_index(node,
						   "ti,irqs-reserved",
						   i, &entry);
			if (entry >= max) {
				pr_err("Invalid reserved entry\n");
				ret = -EINVAL;
				goto err_irq_map;
			}
			cb->irq_map[entry] = IRQ_RESERVED;
		}
	}

	/* Skip irqs hardwired to bypass the crossbar */
	irqsr = of_get_property(node, "ti,irqs-skip", &size);
	if (irqsr) {
		size /= sizeof(__be32);

		for (i = 0; i < size; i++) {
			of_property_read_u32_index(node,
						   "ti,irqs-skip",
						   i, &entry);
			if (entry >= max) {
				pr_err("Invalid skip entry\n");
				ret = -EINVAL;
				goto err_irq_map;
			}
			cb->irq_map[entry] = IRQ_SKIP;
		}
	}


	cb->register_offsets = kcalloc(max, sizeof(int), GFP_KERNEL);
	if (!cb->register_offsets)
		goto err_irq_map;

	of_property_read_u32(node, "ti,reg-size", &size);

	switch (size) {
	case 1:
		cb->write = crossbar_writeb;
		break;
	case 2:
		cb->write = crossbar_writew;
		break;
	case 4:
		cb->write = crossbar_writel;
		break;
	default:
		pr_err("Invalid reg-size property\n");
		ret = -EINVAL;
		goto err_reg_offset;
		break;
	}

	/*
	 * Register offsets are not linear because of the
	 * reserved irqs. so find and store the offsets once.
	 */
	for (i = 0; i < max; i++) {
		if (cb->irq_map[i] == IRQ_RESERVED)
			continue;

		cb->register_offsets[i] = reserved;
		reserved += size;
	}

	of_property_read_u32(node, "ti,irqs-safe-map", &cb->safe_map);
	/* Initialize the crossbar with safe map to start with */
	for (i = 0; i < max; i++) {
		if (cb->irq_map[i] == IRQ_RESERVED ||
		    cb->irq_map[i] == IRQ_SKIP)
			continue;

		cb->write(i, cb->safe_map);
	}

	raw_spin_lock_init(&cb->lock);

	return 0;

err_reg_offset:
	kfree(cb->register_offsets);
err_irq_map:
	kfree(cb->irq_map);
err_base:
	iounmap(cb->crossbar_base);
err_cb:
	kfree(cb);

	cb = NULL;
	return ret;
}

static int __init irqcrossbar_init(struct device_node *node,
				   struct device_node *parent)
{
	struct irq_domain *parent_domain, *domain;
	int err;

	if (!parent) {
		pr_err("%s: no parent, giving up\n", node->full_name);
		return -ENODEV;
	}

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		pr_err("%s: unable to obtain parent domain\n", node->full_name);
		return -ENXIO;
	}

	err = crossbar_of_init(node);
	if (err)
		return err;

	domain = irq_domain_add_hierarchy(parent_domain, 0,
					  cb->max_crossbar_sources,
					  node, &crossbar_domain_ops,
					  NULL);
	if (!domain) {
		pr_err("%s: failed to allocated domain\n", node->full_name);
		return -ENOMEM;
	}

	return 0;
}

IRQCHIP_DECLARE(ti_irqcrossbar, "ti,irq-crossbar", irqcrossbar_init);
