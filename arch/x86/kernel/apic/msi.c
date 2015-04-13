/*
 * Support of MSI, HPET and DMAR interrupts.
 *
 * Copyright (C) 1997, 1998, 1999, 2000, 2009 Ingo Molnar, Hajnalka Szabo
 *	Moved from arch/x86/kernel/apic/io_apic.c.
 * Jiang Liu <jiang.liu@linux.intel.com>
 *	Convert to hierarchical irqdomain
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/dmar.h>
#include <linux/hpet.h>
#include <linux/msi.h>
#include <linux/irqdomain.h>
#include <asm/msidef.h>
#include <asm/hpet.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/irq_remapping.h>

static struct irq_domain *msi_default_domain;

static void irq_msi_compose_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct irq_cfg *cfg = irqd_cfg(data);

	msg->address_hi = MSI_ADDR_BASE_HI;

	if (x2apic_enabled())
		msg->address_hi |= MSI_ADDR_EXT_DEST_ID(cfg->dest_apicid);

	msg->address_lo =
		MSI_ADDR_BASE_LO |
		((apic->irq_dest_mode == 0) ?
			MSI_ADDR_DEST_MODE_PHYSICAL :
			MSI_ADDR_DEST_MODE_LOGICAL) |
		((apic->irq_delivery_mode != dest_LowestPrio) ?
			MSI_ADDR_REDIRECTION_CPU :
			MSI_ADDR_REDIRECTION_LOWPRI) |
		MSI_ADDR_DEST_ID(cfg->dest_apicid);

	msg->data =
		MSI_DATA_TRIGGER_EDGE |
		MSI_DATA_LEVEL_ASSERT |
		((apic->irq_delivery_mode != dest_LowestPrio) ?
			MSI_DATA_DELIVERY_FIXED :
			MSI_DATA_DELIVERY_LOWPRI) |
		MSI_DATA_VECTOR(cfg->vector);
}

static void msi_update_msg(struct msi_msg *msg, struct irq_data *irq_data)
{
	struct irq_cfg *cfg = irqd_cfg(irq_data);

	msg->data &= ~MSI_DATA_VECTOR_MASK;
	msg->data |= MSI_DATA_VECTOR(cfg->vector);
	msg->address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg->address_lo |= MSI_ADDR_DEST_ID(cfg->dest_apicid);
	if (x2apic_enabled())
		msg->address_hi = MSI_ADDR_BASE_HI |
				  MSI_ADDR_EXT_DEST_ID(cfg->dest_apicid);
}

/*
 * IRQ Chip for MSI PCI/PCI-X/PCI-Express Devices,
 * which implement the MSI or MSI-X Capability Structure.
 */
static struct irq_chip pci_msi_controller = {
	.name			= "PCI-MSI",
	.irq_unmask		= pci_msi_unmask_irq,
	.irq_mask		= pci_msi_mask_irq,
	.irq_ack		= irq_chip_ack_parent,
	.irq_set_affinity	= msi_domain_set_affinity,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_print_chip		= irq_remapping_print_chip,
	.irq_compose_msi_msg	= irq_msi_compose_msg,
	.irq_write_msi_msg	= pci_msi_domain_write_msg,
	.flags			= IRQCHIP_SKIP_SET_WAKE,
};

int native_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct irq_domain *domain;
	struct irq_alloc_info info;

	init_irq_alloc_info(&info, NULL);
	info.type = X86_IRQ_ALLOC_TYPE_MSI;
	info.msi_dev = dev;

	domain = irq_remapping_get_irq_domain(&info);
	if (domain == NULL)
		domain = msi_default_domain;
	if (domain == NULL)
		return -ENOSYS;

	return pci_msi_domain_alloc_irqs(domain, dev, nvec, type);
}

void native_teardown_msi_irq(unsigned int irq)
{
	irq_domain_free_irqs(irq, 1);
}

static irq_hw_number_t pci_msi_get_hwirq(struct msi_domain_info *info,
					 msi_alloc_info_t *arg)
{
	return arg->msi_hwirq;
}

static int pci_msi_prepare(struct irq_domain *domain, struct device *dev,
			   int nvec, msi_alloc_info_t *arg)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct msi_desc *desc = first_pci_msi_entry(pdev);

	init_irq_alloc_info(arg, NULL);
	arg->msi_dev = pdev;
	if (desc->msi_attrib.is_msix) {
		arg->type = X86_IRQ_ALLOC_TYPE_MSIX;
	} else {
		arg->type = X86_IRQ_ALLOC_TYPE_MSI;
		arg->flags |= X86_IRQ_ALLOC_CONTIGUOUS_VECTORS;
	}

	return 0;
}

static void pci_msi_set_desc(msi_alloc_info_t *arg, struct msi_desc *desc)
{
	arg->msi_hwirq = pci_msi_domain_calc_hwirq(arg->msi_dev, desc);
}

static struct msi_domain_ops pci_msi_domain_ops = {
	.get_hwirq	= pci_msi_get_hwirq,
	.msi_prepare	= pci_msi_prepare,
	.set_desc	= pci_msi_set_desc,
};

static struct msi_domain_info pci_msi_domain_info = {
	.flags		= MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
			  MSI_FLAG_MULTI_PCI_MSI | MSI_FLAG_PCI_MSIX,
	.ops		= &pci_msi_domain_ops,
	.chip		= &pci_msi_controller,
	.handler	= handle_edge_irq,
	.handler_name	= "edge",
};

void arch_init_msi_domain(struct irq_domain *parent)
{
	if (disable_apic)
		return;

	msi_default_domain = pci_msi_create_irq_domain(NULL,
					&pci_msi_domain_info, parent);
	if (!msi_default_domain)
		pr_warn("failed to initialize irqdomain for MSI/MSI-x.\n");
}

#ifdef CONFIG_IRQ_REMAP
struct irq_domain *arch_create_msi_irq_domain(struct irq_domain *parent)
{
	return msi_create_irq_domain(NULL, &pci_msi_domain_info, parent);
}
#endif

#ifdef CONFIG_DMAR_TABLE
static int
dmar_msi_set_affinity(struct irq_data *data, const struct cpumask *mask,
		      bool force)
{
	struct irq_data *parent = data->parent_data;
	struct msi_msg msg;
	int ret;

	ret = parent->chip->irq_set_affinity(parent, mask, force);
	if (ret >= 0) {
		dmar_msi_read(data->irq, &msg);
		msi_update_msg(&msg, data);
		dmar_msi_write(data->irq, &msg);
	}

	return ret;
}

static struct irq_chip dmar_msi_controller = {
	.name			= "DMAR_MSI",
	.irq_unmask		= dmar_msi_unmask,
	.irq_mask		= dmar_msi_mask,
	.irq_ack		= irq_chip_ack_parent,
	.irq_set_affinity	= dmar_msi_set_affinity,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_compose_msi_msg	= irq_msi_compose_msg,
	.flags			= IRQCHIP_SKIP_SET_WAKE,
};

static int dmar_domain_alloc(struct irq_domain *domain, unsigned int virq,
			     unsigned int nr_irqs, void *arg)
{
	struct irq_alloc_info *info = arg;
	int ret;

	if (nr_irqs > 1 || !info || info->type != X86_IRQ_ALLOC_TYPE_DMAR)
		return -EINVAL;
	if (irq_find_mapping(domain, info->dmar_id)) {
		pr_warn("IRQ for DMAR%d already exists.\n", info->dmar_id);
		return -EEXIST;
	}

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, arg);
	if (ret >= 0) {
		irq_domain_set_hwirq_and_chip(domain, virq, info->dmar_id,
					      &dmar_msi_controller, NULL);
		irq_set_handler_data(virq, info->dmar_data);
		__irq_set_handler(virq, handle_edge_irq, 0, "edge");
	}

	return ret;
}

static void dmar_domain_free(struct irq_domain *domain, unsigned int virq,
			     unsigned int nr_irqs)
{
	BUG_ON(nr_irqs > 1);
	irq_domain_free_irqs_top(domain, virq, nr_irqs);
}

static void dmar_domain_activate(struct irq_domain *domain,
				 struct irq_data *irq_data)
{
	struct msi_msg msg;

	BUG_ON(irq_chip_compose_msi_msg(irq_data, &msg));
	dmar_msi_write(irq_data->irq, &msg);
}

static void dmar_domain_deactivate(struct irq_domain *domain,
				   struct irq_data *irq_data)
{
	struct msi_msg msg;

	memset(&msg, 0, sizeof(msg));
	dmar_msi_write(irq_data->irq, &msg);
}

static struct irq_domain_ops dmar_domain_ops = {
	.alloc = dmar_domain_alloc,
	.free = dmar_domain_free,
	.activate = dmar_domain_activate,
	.deactivate = dmar_domain_deactivate,
};

static struct irq_domain *dmar_get_irq_domain(void)
{
	static struct irq_domain *dmar_domain;
	static DEFINE_MUTEX(dmar_lock);

	mutex_lock(&dmar_lock);
	if (dmar_domain == NULL) {
		dmar_domain = irq_domain_add_tree(NULL, &dmar_domain_ops, NULL);
		if (dmar_domain)
			dmar_domain->parent = x86_vector_domain;
	}
	mutex_unlock(&dmar_lock);

	return dmar_domain;
}

int dmar_alloc_hwirq(int id, int node, void *arg)
{
	struct irq_domain *domain = dmar_get_irq_domain();
	struct irq_alloc_info info;

	if (!domain)
		return -1;

	init_irq_alloc_info(&info, NULL);
	info.type = X86_IRQ_ALLOC_TYPE_DMAR;
	info.dmar_id = id;
	info.dmar_data = arg;

	return irq_domain_alloc_irqs(domain, 1, node, &info);
}

void dmar_free_hwirq(int irq)
{
	irq_domain_free_irqs(irq, 1);
}
#endif

/*
 * MSI message composition
 */
#ifdef CONFIG_HPET_TIMER
static inline int hpet_dev_id(struct irq_domain *domain)
{
	return (int)(long)domain->host_data;
}

static int hpet_msi_set_affinity(struct irq_data *data,
				 const struct cpumask *mask, bool force)
{
	struct irq_data *parent = data->parent_data;
	struct msi_msg msg;
	int ret;

	ret = parent->chip->irq_set_affinity(parent, mask, force);
	if (ret >= 0 && ret != IRQ_SET_MASK_OK_DONE) {
		hpet_msi_read(data->handler_data, &msg);
		msi_update_msg(&msg, data);
		hpet_msi_write(data->handler_data, &msg);
	}

	return ret;
}

static struct irq_chip hpet_msi_controller = {
	.name = "HPET_MSI",
	.irq_unmask = hpet_msi_unmask,
	.irq_mask = hpet_msi_mask,
	.irq_ack = irq_chip_ack_parent,
	.irq_set_affinity = hpet_msi_set_affinity,
	.irq_retrigger = irq_chip_retrigger_hierarchy,
	.irq_print_chip = irq_remapping_print_chip,
	.irq_compose_msi_msg = irq_msi_compose_msg,
	.flags = IRQCHIP_SKIP_SET_WAKE,
};

static int hpet_domain_alloc(struct irq_domain *domain, unsigned int virq,
			     unsigned int nr_irqs, void *arg)
{
	struct irq_alloc_info *info = arg;
	int ret;

	if (nr_irqs > 1 || !info || info->type != X86_IRQ_ALLOC_TYPE_HPET)
		return -EINVAL;
	if (irq_find_mapping(domain, info->hpet_index)) {
		pr_warn("IRQ for HPET%d already exists.\n", info->hpet_index);
		return -EEXIST;
	}

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, arg);
	if (ret >= 0) {
		irq_set_status_flags(virq, IRQ_MOVE_PCNTXT);
		irq_domain_set_hwirq_and_chip(domain, virq, info->hpet_index,
					      &hpet_msi_controller, NULL);
		irq_set_handler_data(virq, info->hpet_data);
		__irq_set_handler(virq, handle_edge_irq, 0, "edge");
	}

	return ret;
}

static void hpet_domain_free(struct irq_domain *domain, unsigned int virq,
			     unsigned int nr_irqs)
{
	BUG_ON(nr_irqs > 1);
	irq_clear_status_flags(virq, IRQ_MOVE_PCNTXT);
	irq_domain_free_irqs_top(domain, virq, nr_irqs);
}

static void hpet_domain_activate(struct irq_domain *domain,
				struct irq_data *irq_data)
{
	struct msi_msg msg;

	BUG_ON(irq_chip_compose_msi_msg(irq_data, &msg));
	hpet_msi_write(irq_get_handler_data(irq_data->irq), &msg);
}

static void hpet_domain_deactivate(struct irq_domain *domain,
				  struct irq_data *irq_data)
{
	struct msi_msg msg;

	memset(&msg, 0, sizeof(msg));
	hpet_msi_write(irq_get_handler_data(irq_data->irq), &msg);
}

static struct irq_domain_ops hpet_domain_ops = {
	.alloc = hpet_domain_alloc,
	.free = hpet_domain_free,
	.activate = hpet_domain_activate,
	.deactivate = hpet_domain_deactivate,
};

struct irq_domain *hpet_create_irq_domain(int hpet_id)
{
	struct irq_domain *parent;
	struct irq_alloc_info info;

	if (x86_vector_domain == NULL)
		return NULL;

	init_irq_alloc_info(&info, NULL);
	info.type = X86_IRQ_ALLOC_TYPE_HPET;
	info.hpet_id = hpet_id;
	parent = irq_remapping_get_ir_irq_domain(&info);
	if (parent == NULL)
		parent = x86_vector_domain;

	return irq_domain_add_hierarchy(parent, 0, 0, NULL, &hpet_domain_ops,
					(void *)(long)hpet_id);
}

int hpet_assign_irq(struct irq_domain *domain, struct hpet_dev *dev,
		    int dev_num)
{
	struct irq_alloc_info info;

	init_irq_alloc_info(&info, NULL);
	info.type = X86_IRQ_ALLOC_TYPE_HPET;
	info.hpet_data = dev;
	info.hpet_id = hpet_dev_id(domain);
	info.hpet_index = dev_num;

	return irq_domain_alloc_irqs(domain, 1, NUMA_NO_NODE, NULL);
}
#endif
