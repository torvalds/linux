// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqchip/irq-partition-percpu.h>
#include <linux/irqdomain.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

struct partition_desc {
	int				nr_parts;
	struct partition_affinity	*parts;
	struct irq_domain		*domain;
	struct irq_desc			*chained_desc;
	unsigned long			*bitmap;
	struct irq_domain_ops		ops;
};

static bool partition_check_cpu(struct partition_desc *part,
				unsigned int cpu, unsigned int hwirq)
{
	return cpumask_test_cpu(cpu, &part->parts[hwirq].mask);
}

static void partition_irq_mask(struct irq_data *d)
{
	struct partition_desc *part = irq_data_get_irq_chip_data(d);
	struct irq_chip *chip = irq_desc_get_chip(part->chained_desc);
	struct irq_data *data = irq_desc_get_irq_data(part->chained_desc);

	if (partition_check_cpu(part, smp_processor_id(), d->hwirq) &&
	    chip->irq_mask)
		chip->irq_mask(data);
}

static void partition_irq_unmask(struct irq_data *d)
{
	struct partition_desc *part = irq_data_get_irq_chip_data(d);
	struct irq_chip *chip = irq_desc_get_chip(part->chained_desc);
	struct irq_data *data = irq_desc_get_irq_data(part->chained_desc);

	if (partition_check_cpu(part, smp_processor_id(), d->hwirq) &&
	    chip->irq_unmask)
		chip->irq_unmask(data);
}

static int partition_irq_set_irqchip_state(struct irq_data *d,
					   enum irqchip_irq_state which,
					   bool val)
{
	struct partition_desc *part = irq_data_get_irq_chip_data(d);
	struct irq_chip *chip = irq_desc_get_chip(part->chained_desc);
	struct irq_data *data = irq_desc_get_irq_data(part->chained_desc);

	if (partition_check_cpu(part, smp_processor_id(), d->hwirq) &&
	    chip->irq_set_irqchip_state)
		return chip->irq_set_irqchip_state(data, which, val);

	return -EINVAL;
}

static int partition_irq_get_irqchip_state(struct irq_data *d,
					   enum irqchip_irq_state which,
					   bool *val)
{
	struct partition_desc *part = irq_data_get_irq_chip_data(d);
	struct irq_chip *chip = irq_desc_get_chip(part->chained_desc);
	struct irq_data *data = irq_desc_get_irq_data(part->chained_desc);

	if (partition_check_cpu(part, smp_processor_id(), d->hwirq) &&
	    chip->irq_get_irqchip_state)
		return chip->irq_get_irqchip_state(data, which, val);

	return -EINVAL;
}

static int partition_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct partition_desc *part = irq_data_get_irq_chip_data(d);
	struct irq_chip *chip = irq_desc_get_chip(part->chained_desc);
	struct irq_data *data = irq_desc_get_irq_data(part->chained_desc);

	if (chip->irq_set_type)
		return chip->irq_set_type(data, type);

	return -EINVAL;
}

static void partition_irq_print_chip(struct irq_data *d, struct seq_file *p)
{
	struct partition_desc *part = irq_data_get_irq_chip_data(d);
	struct irq_chip *chip = irq_desc_get_chip(part->chained_desc);
	struct irq_data *data = irq_desc_get_irq_data(part->chained_desc);

	seq_printf(p, "%5s-%lu", chip->name, data->hwirq);
}

static struct irq_chip partition_irq_chip = {
	.irq_mask		= partition_irq_mask,
	.irq_unmask		= partition_irq_unmask,
	.irq_set_type		= partition_irq_set_type,
	.irq_get_irqchip_state	= partition_irq_get_irqchip_state,
	.irq_set_irqchip_state	= partition_irq_set_irqchip_state,
	.irq_print_chip		= partition_irq_print_chip,
};

static void partition_handle_irq(struct irq_desc *desc)
{
	struct partition_desc *part = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int cpu = smp_processor_id();
	int hwirq;

	chained_irq_enter(chip, desc);

	for_each_set_bit(hwirq, part->bitmap, part->nr_parts) {
		if (partition_check_cpu(part, cpu, hwirq))
			break;
	}

	if (unlikely(hwirq == part->nr_parts))
		handle_bad_irq(desc);
	else
		generic_handle_domain_irq(part->domain, hwirq);

	chained_irq_exit(chip, desc);
}

static int partition_domain_alloc(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs, void *arg)
{
	int ret;
	irq_hw_number_t hwirq;
	unsigned int type;
	struct irq_fwspec *fwspec = arg;
	struct partition_desc *part;

	BUG_ON(nr_irqs != 1);
	ret = domain->ops->translate(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	part = domain->host_data;

	set_bit(hwirq, part->bitmap);
	irq_set_chained_handler_and_data(irq_desc_get_irq(part->chained_desc),
					 partition_handle_irq, part);
	irq_set_percpu_devid_partition(virq, &part->parts[hwirq].mask);
	irq_domain_set_info(domain, virq, hwirq, &partition_irq_chip, part,
			    handle_percpu_devid_irq, NULL, NULL);
	irq_set_status_flags(virq, IRQ_NOAUTOEN);

	return 0;
}

static void partition_domain_free(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs)
{
	struct irq_data *d;

	BUG_ON(nr_irqs != 1);

	d = irq_domain_get_irq_data(domain, virq);
	irq_set_handler(virq, NULL);
	irq_domain_reset_irq_data(d);
}

int partition_translate_id(struct partition_desc *desc, void *partition_id)
{
	struct partition_affinity *part = NULL;
	int i;

	for (i = 0; i < desc->nr_parts; i++) {
		if (desc->parts[i].partition_id == partition_id) {
			part = &desc->parts[i];
			break;
		}
	}

	if (WARN_ON(!part)) {
		pr_err("Failed to find partition\n");
		return -EINVAL;
	}

	return i;
}

struct partition_desc *partition_create_desc(struct fwnode_handle *fwnode,
					     struct partition_affinity *parts,
					     int nr_parts,
					     int chained_irq,
					     const struct irq_domain_ops *ops)
{
	struct partition_desc *desc;
	struct irq_domain *d;

	BUG_ON(!ops->select || !ops->translate);

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	desc->ops = *ops;
	desc->ops.free = partition_domain_free;
	desc->ops.alloc = partition_domain_alloc;

	d = irq_domain_create_linear(fwnode, nr_parts, &desc->ops, desc);
	if (!d)
		goto out;
	desc->domain = d;

	desc->bitmap = bitmap_zalloc(nr_parts, GFP_KERNEL);
	if (WARN_ON(!desc->bitmap))
		goto out;

	desc->chained_desc = irq_to_desc(chained_irq);
	desc->nr_parts = nr_parts;
	desc->parts = parts;

	return desc;
out:
	if (d)
		irq_domain_remove(d);
	kfree(desc);

	return NULL;
}

struct irq_domain *partition_get_domain(struct partition_desc *dsc)
{
	if (dsc)
		return dsc->domain;

	return NULL;
}
