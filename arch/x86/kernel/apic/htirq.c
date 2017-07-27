/*
 * Support Hypertransport IRQ
 *
 * Copyright (C) 1997, 1998, 1999, 2000, 2009 Ingo Molnar, Hajnalka Szabo
 *	Moved from arch/x86/kernel/apic/io_apic.c.
 * Jiang Liu <jiang.liu@linux.intel.com>
 *	Add support of hierarchical irqdomain
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/htirq.h>
#include <asm/irqdomain.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/hypertransport.h>

static struct irq_domain *htirq_domain;

/*
 * Hypertransport interrupt support
 */
static int
ht_set_affinity(struct irq_data *data, const struct cpumask *mask, bool force)
{
	struct irq_data *parent = data->parent_data;
	int ret;

	ret = parent->chip->irq_set_affinity(parent, mask, force);
	if (ret >= 0) {
		struct ht_irq_msg msg;
		struct irq_cfg *cfg = irqd_cfg(data);

		fetch_ht_irq_msg(data->irq, &msg);
		msg.address_lo &= ~(HT_IRQ_LOW_VECTOR_MASK |
				    HT_IRQ_LOW_DEST_ID_MASK);
		msg.address_lo |= HT_IRQ_LOW_VECTOR(cfg->vector) |
				  HT_IRQ_LOW_DEST_ID(cfg->dest_apicid);
		msg.address_hi &= ~(HT_IRQ_HIGH_DEST_ID_MASK);
		msg.address_hi |= HT_IRQ_HIGH_DEST_ID(cfg->dest_apicid);
		write_ht_irq_msg(data->irq, &msg);
	}

	return ret;
}

static struct irq_chip ht_irq_chip = {
	.name			= "PCI-HT",
	.irq_mask		= mask_ht_irq,
	.irq_unmask		= unmask_ht_irq,
	.irq_ack		= irq_chip_ack_parent,
	.irq_set_affinity	= ht_set_affinity,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.flags			= IRQCHIP_SKIP_SET_WAKE,
};

static int htirq_domain_alloc(struct irq_domain *domain, unsigned int virq,
			      unsigned int nr_irqs, void *arg)
{
	struct ht_irq_cfg *ht_cfg;
	struct irq_alloc_info *info = arg;
	struct pci_dev *dev;
	irq_hw_number_t hwirq;
	int ret;

	if (nr_irqs > 1 || !info)
		return -EINVAL;

	dev = info->ht_dev;
	hwirq = (info->ht_idx & 0xFF) |
		PCI_DEVID(dev->bus->number, dev->devfn) << 8 |
		(pci_domain_nr(dev->bus) & 0xFFFFFFFF) << 24;
	if (irq_find_mapping(domain, hwirq) > 0)
		return -EEXIST;

	ht_cfg = kmalloc(sizeof(*ht_cfg), GFP_KERNEL);
	if (!ht_cfg)
		return -ENOMEM;

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, info);
	if (ret < 0) {
		kfree(ht_cfg);
		return ret;
	}

	/* Initialize msg to a value that will never match the first write. */
	ht_cfg->msg.address_lo = 0xffffffff;
	ht_cfg->msg.address_hi = 0xffffffff;
	ht_cfg->dev = info->ht_dev;
	ht_cfg->update = info->ht_update;
	ht_cfg->pos = info->ht_pos;
	ht_cfg->idx = 0x10 + (info->ht_idx * 2);
	irq_domain_set_info(domain, virq, hwirq, &ht_irq_chip, ht_cfg,
			    handle_edge_irq, ht_cfg, "edge");

	return 0;
}

static void htirq_domain_free(struct irq_domain *domain, unsigned int virq,
			      unsigned int nr_irqs)
{
	struct irq_data *irq_data = irq_domain_get_irq_data(domain, virq);

	BUG_ON(nr_irqs != 1);
	kfree(irq_data->chip_data);
	irq_domain_free_irqs_top(domain, virq, nr_irqs);
}

static void htirq_domain_activate(struct irq_domain *domain,
				  struct irq_data *irq_data)
{
	struct ht_irq_msg msg;
	struct irq_cfg *cfg = irqd_cfg(irq_data);

	msg.address_hi = HT_IRQ_HIGH_DEST_ID(cfg->dest_apicid);
	msg.address_lo =
		HT_IRQ_LOW_BASE |
		HT_IRQ_LOW_DEST_ID(cfg->dest_apicid) |
		HT_IRQ_LOW_VECTOR(cfg->vector) |
		((apic->irq_dest_mode == 0) ?
			HT_IRQ_LOW_DM_PHYSICAL :
			HT_IRQ_LOW_DM_LOGICAL) |
		HT_IRQ_LOW_RQEOI_EDGE |
		((apic->irq_delivery_mode != dest_LowestPrio) ?
			HT_IRQ_LOW_MT_FIXED :
			HT_IRQ_LOW_MT_ARBITRATED) |
		HT_IRQ_LOW_IRQ_MASKED;
	write_ht_irq_msg(irq_data->irq, &msg);
}

static void htirq_domain_deactivate(struct irq_domain *domain,
				    struct irq_data *irq_data)
{
	struct ht_irq_msg msg;

	memset(&msg, 0, sizeof(msg));
	write_ht_irq_msg(irq_data->irq, &msg);
}

static const struct irq_domain_ops htirq_domain_ops = {
	.alloc		= htirq_domain_alloc,
	.free		= htirq_domain_free,
	.activate	= htirq_domain_activate,
	.deactivate	= htirq_domain_deactivate,
};

void __init arch_init_htirq_domain(struct irq_domain *parent)
{
	struct fwnode_handle *fn;

	if (disable_apic)
		return;

	fn = irq_domain_alloc_named_fwnode("PCI-HT");
	if (!fn)
		goto warn;

	htirq_domain = irq_domain_create_tree(fn, &htirq_domain_ops, NULL);
	irq_domain_free_fwnode(fn);
	if (!htirq_domain)
		goto warn;

	htirq_domain->parent = parent;
	return;

warn:
	pr_warn("Failed to initialize irqdomain for HTIRQ.\n");
}

int arch_setup_ht_irq(int idx, int pos, struct pci_dev *dev,
		      ht_irq_update_t *update)
{
	struct irq_alloc_info info;

	if (!htirq_domain)
		return -ENOSYS;

	init_irq_alloc_info(&info, NULL);
	info.ht_idx = idx;
	info.ht_pos = pos;
	info.ht_dev = dev;
	info.ht_update = update;

	return irq_domain_alloc_irqs(htirq_domain, 1, dev_to_node(&dev->dev),
				     &info);
}

void arch_teardown_ht_irq(unsigned int irq)
{
	irq_domain_free_irqs(irq, 1);
}
