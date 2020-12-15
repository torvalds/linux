// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright (c) 2020 MediaTek Inc.
 * Author Mark-PK Tsai <mark-pk.tsai@mediatek.com>
 */
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define INTC_MASK	0x0
#define INTC_EOI	0x20

struct mst_intc_chip_data {
	raw_spinlock_t	lock;
	unsigned int	irq_start, nr_irqs;
	void __iomem	*base;
	bool		no_eoi;
};

static void mst_set_irq(struct irq_data *d, u32 offset)
{
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	struct mst_intc_chip_data *cd = irq_data_get_irq_chip_data(d);
	u16 val, mask;
	unsigned long flags;

	mask = 1 << (hwirq % 16);
	offset += (hwirq / 16) * 4;

	raw_spin_lock_irqsave(&cd->lock, flags);
	val = readw_relaxed(cd->base + offset) | mask;
	writew_relaxed(val, cd->base + offset);
	raw_spin_unlock_irqrestore(&cd->lock, flags);
}

static void mst_clear_irq(struct irq_data *d, u32 offset)
{
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	struct mst_intc_chip_data *cd = irq_data_get_irq_chip_data(d);
	u16 val, mask;
	unsigned long flags;

	mask = 1 << (hwirq % 16);
	offset += (hwirq / 16) * 4;

	raw_spin_lock_irqsave(&cd->lock, flags);
	val = readw_relaxed(cd->base + offset) & ~mask;
	writew_relaxed(val, cd->base + offset);
	raw_spin_unlock_irqrestore(&cd->lock, flags);
}

static void mst_intc_mask_irq(struct irq_data *d)
{
	mst_set_irq(d, INTC_MASK);
	irq_chip_mask_parent(d);
}

static void mst_intc_unmask_irq(struct irq_data *d)
{
	mst_clear_irq(d, INTC_MASK);
	irq_chip_unmask_parent(d);
}

static void mst_intc_eoi_irq(struct irq_data *d)
{
	struct mst_intc_chip_data *cd = irq_data_get_irq_chip_data(d);

	if (!cd->no_eoi)
		mst_set_irq(d, INTC_EOI);

	irq_chip_eoi_parent(d);
}

static struct irq_chip mst_intc_chip = {
	.name			= "mst-intc",
	.irq_mask		= mst_intc_mask_irq,
	.irq_unmask		= mst_intc_unmask_irq,
	.irq_eoi		= mst_intc_eoi_irq,
	.irq_get_irqchip_state	= irq_chip_get_parent_state,
	.irq_set_irqchip_state	= irq_chip_set_parent_state,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_set_vcpu_affinity	= irq_chip_set_vcpu_affinity_parent,
	.irq_set_type		= irq_chip_set_type_parent,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.flags			= IRQCHIP_SET_TYPE_MASKED |
				  IRQCHIP_SKIP_SET_WAKE |
				  IRQCHIP_MASK_ON_SUSPEND,
};

static int mst_intc_domain_translate(struct irq_domain *d,
				     struct irq_fwspec *fwspec,
				     unsigned long *hwirq,
				     unsigned int *type)
{
	struct mst_intc_chip_data *cd = d->host_data;

	if (is_of_node(fwspec->fwnode)) {
		if (fwspec->param_count != 3)
			return -EINVAL;

		/* No PPI should point to this domain */
		if (fwspec->param[0] != 0)
			return -EINVAL;

		if (fwspec->param[1] >= cd->nr_irqs)
			return -EINVAL;

		*hwirq = fwspec->param[1];
		*type = fwspec->param[2] & IRQ_TYPE_SENSE_MASK;
		return 0;
	}

	return -EINVAL;
}

static int mst_intc_domain_alloc(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *data)
{
	int i;
	irq_hw_number_t hwirq;
	struct irq_fwspec parent_fwspec, *fwspec = data;
	struct mst_intc_chip_data *cd = domain->host_data;

	/* Not GIC compliant */
	if (fwspec->param_count != 3)
		return -EINVAL;

	/* No PPI should point to this domain */
	if (fwspec->param[0])
		return -EINVAL;

	hwirq = fwspec->param[1];
	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
					      &mst_intc_chip,
					      domain->host_data);

	parent_fwspec = *fwspec;
	parent_fwspec.fwnode = domain->parent->fwnode;
	parent_fwspec.param[1] = cd->irq_start + hwirq;
	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, &parent_fwspec);
}

static const struct irq_domain_ops mst_intc_domain_ops = {
	.translate	= mst_intc_domain_translate,
	.alloc		= mst_intc_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

static int __init mst_intc_of_init(struct device_node *dn,
				   struct device_node *parent)
{
	struct irq_domain *domain, *domain_parent;
	struct mst_intc_chip_data *cd;
	u32 irq_start, irq_end;

	domain_parent = irq_find_host(parent);
	if (!domain_parent) {
		pr_err("mst-intc: interrupt-parent not found\n");
		return -EINVAL;
	}

	if (of_property_read_u32_index(dn, "mstar,irqs-map-range", 0, &irq_start) ||
	    of_property_read_u32_index(dn, "mstar,irqs-map-range", 1, &irq_end))
		return -EINVAL;

	cd = kzalloc(sizeof(*cd), GFP_KERNEL);
	if (!cd)
		return -ENOMEM;

	cd->base = of_iomap(dn, 0);
	if (!cd->base) {
		kfree(cd);
		return -ENOMEM;
	}

	cd->no_eoi = of_property_read_bool(dn, "mstar,intc-no-eoi");
	raw_spin_lock_init(&cd->lock);
	cd->irq_start = irq_start;
	cd->nr_irqs = irq_end - irq_start + 1;
	domain = irq_domain_add_hierarchy(domain_parent, 0, cd->nr_irqs, dn,
					  &mst_intc_domain_ops, cd);
	if (!domain) {
		iounmap(cd->base);
		kfree(cd);
		return -ENOMEM;
	}

	return 0;
}

IRQCHIP_DECLARE(mst_intc, "mstar,mst-intc", mst_intc_of_init);
