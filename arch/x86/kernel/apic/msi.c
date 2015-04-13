/*
 * Support of MSI, HPET and DMAR interrupts.
 *
 * Copyright (C) 1997, 1998, 1999, 2000, 2009 Ingo Molnar, Hajnalka Szabo
 *	Moved from arch/x86/kernel/apic/io_apic.c.
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

void native_compose_msi_msg(struct pci_dev *pdev,
			    unsigned int irq, unsigned int dest,
			    struct msi_msg *msg, u8 hpet_id)
{
	struct irq_cfg *cfg = irq_cfg(irq);

	msg->address_hi = MSI_ADDR_BASE_HI;

	if (x2apic_enabled())
		msg->address_hi |= MSI_ADDR_EXT_DEST_ID(dest);

	msg->address_lo =
		MSI_ADDR_BASE_LO |
		((apic->irq_dest_mode == 0) ?
			MSI_ADDR_DEST_MODE_PHYSICAL :
			MSI_ADDR_DEST_MODE_LOGICAL) |
		((apic->irq_delivery_mode != dest_LowestPrio) ?
			MSI_ADDR_REDIRECTION_CPU :
			MSI_ADDR_REDIRECTION_LOWPRI) |
		MSI_ADDR_DEST_ID(dest);

	msg->data =
		MSI_DATA_TRIGGER_EDGE |
		MSI_DATA_LEVEL_ASSERT |
		((apic->irq_delivery_mode != dest_LowestPrio) ?
			MSI_DATA_DELIVERY_FIXED :
			MSI_DATA_DELIVERY_LOWPRI) |
		MSI_DATA_VECTOR(cfg->vector);
}

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
}

static int msi_compose_msg(struct pci_dev *pdev, unsigned int irq,
			   struct msi_msg *msg, u8 hpet_id)
{
	struct irq_cfg *cfg;
	int err;
	unsigned dest;

	if (disable_apic)
		return -ENXIO;

	cfg = irq_cfg(irq);
	err = assign_irq_vector(irq, cfg, apic->target_cpus());
	if (err)
		return err;

	err = apic->cpu_mask_to_apicid_and(cfg->domain,
					   apic->target_cpus(), &dest);
	if (err)
		return err;

	x86_msi.compose_msi_msg(pdev, irq, dest, msg, hpet_id);

	return 0;
}

static int
msi_set_affinity(struct irq_data *data, const struct cpumask *mask, bool force)
{
	struct irq_cfg *cfg = irqd_cfg(data);
	struct msi_msg msg;
	unsigned int dest;
	int ret;

	ret = apic_set_affinity(data, mask, &dest);
	if (ret)
		return ret;

	__get_cached_msi_msg(data->msi_desc, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID(dest);

	__pci_write_msi_msg(data->msi_desc, &msg);

	return IRQ_SET_MASK_OK_NOCOPY;
}

/*
 * IRQ Chip for MSI PCI/PCI-X/PCI-Express Devices,
 * which implement the MSI or MSI-X Capability Structure.
 */
static struct irq_chip msi_chip = {
	.name			= "PCI-MSI",
	.irq_unmask		= pci_msi_unmask_irq,
	.irq_mask		= pci_msi_mask_irq,
	.irq_ack		= apic_ack_edge,
	.irq_set_affinity	= msi_set_affinity,
	.irq_retrigger		= apic_retrigger_irq,
	.flags			= IRQCHIP_SKIP_SET_WAKE,
};

int setup_msi_irq(struct pci_dev *dev, struct msi_desc *msidesc,
		  unsigned int irq_base, unsigned int irq_offset)
{
	struct irq_chip *chip = &msi_chip;
	struct msi_msg msg;
	unsigned int irq = irq_base + irq_offset;
	int ret;

	ret = msi_compose_msg(dev, irq, &msg, -1);
	if (ret < 0)
		return ret;

	irq_set_msi_desc_off(irq_base, irq_offset, msidesc);

	/*
	 * MSI-X message is written per-IRQ, the offset is always 0.
	 * MSI message denotes a contiguous group of IRQs, written for 0th IRQ.
	 */
	if (!irq_offset)
		pci_write_msi_msg(irq, &msg);

	setup_remapped_irq(irq, irq_cfg(irq), chip);

	irq_set_chip_and_handler_name(irq, chip, handle_edge_irq, "edge");

	dev_dbg(&dev->dev, "irq %d for MSI/MSI-X\n", irq);

	return 0;
}

int native_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	struct msi_desc *msidesc;
	int irq, ret;

	/* Multiple MSI vectors only supported with interrupt remapping */
	if (type == PCI_CAP_ID_MSI && nvec > 1)
		return 1;

	list_for_each_entry(msidesc, &dev->msi_list, list) {
		irq = irq_domain_alloc_irqs(NULL, 1, NUMA_NO_NODE, NULL);
		if (irq <= 0)
			return -ENOSPC;

		ret = setup_msi_irq(dev, msidesc, irq, 0);
		if (ret < 0) {
			irq_domain_free_irqs(irq, 1);
			return ret;
		}

	}
	return 0;
}

void native_teardown_msi_irq(unsigned int irq)
{
	irq_domain_free_irqs(irq, 1);
}

#ifdef CONFIG_DMAR_TABLE
static int
dmar_msi_set_affinity(struct irq_data *data, const struct cpumask *mask,
		      bool force)
{
	struct irq_cfg *cfg = irqd_cfg(data);
	unsigned int dest, irq = data->irq;
	struct msi_msg msg;
	int ret;

	ret = apic_set_affinity(data, mask, &dest);
	if (ret)
		return ret;

	dmar_msi_read(irq, &msg);

	msg.data &= ~MSI_DATA_VECTOR_MASK;
	msg.data |= MSI_DATA_VECTOR(cfg->vector);
	msg.address_lo &= ~MSI_ADDR_DEST_ID_MASK;
	msg.address_lo |= MSI_ADDR_DEST_ID(dest);
	msg.address_hi = MSI_ADDR_BASE_HI | MSI_ADDR_EXT_DEST_ID(dest);

	dmar_msi_write(irq, &msg);

	return IRQ_SET_MASK_OK_NOCOPY;
}

static struct irq_chip dmar_msi_type = {
	.name			= "DMAR_MSI",
	.irq_unmask		= dmar_msi_unmask,
	.irq_mask		= dmar_msi_mask,
	.irq_ack		= apic_ack_edge,
	.irq_set_affinity	= dmar_msi_set_affinity,
	.irq_retrigger		= apic_retrigger_irq,
	.flags			= IRQCHIP_SKIP_SET_WAKE,
};

int arch_setup_dmar_msi(unsigned int irq)
{
	int ret;
	struct msi_msg msg;

	ret = msi_compose_msg(NULL, irq, &msg, -1);
	if (ret < 0)
		return ret;
	dmar_msi_write(irq, &msg);
	irq_set_chip_and_handler_name(irq, &dmar_msi_type, handle_edge_irq,
				      "edge");
	return 0;
}

int dmar_alloc_hwirq(void)
{
	return irq_domain_alloc_irqs(NULL, 1, NUMA_NO_NODE, NULL);
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

int default_setup_hpet_msi(unsigned int irq, unsigned int id)
{
	struct irq_chip *chip = &hpet_msi_controller;
	struct msi_msg msg;
	int ret;

	ret = msi_compose_msg(NULL, irq, &msg, id);
	if (ret < 0)
		return ret;

	hpet_msi_write(irq_get_handler_data(irq), &msg);
	irq_set_status_flags(irq, IRQ_MOVE_PCNTXT);
	setup_remapped_irq(irq, irq_cfg(irq), chip);

	irq_set_chip_and_handler_name(irq, chip, handle_edge_irq, "edge");
	return 0;
}

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
