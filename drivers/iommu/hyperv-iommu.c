// SPDX-License-Identifier: GPL-2.0

/*
 * Hyper-V stub IOMMU driver.
 *
 * Copyright (C) 2019, Microsoft, Inc.
 *
 * Author : Lan Tianyu <Tianyu.Lan@microsoft.com>
 */

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/iommu.h>
#include <linux/module.h>

#include <asm/apic.h>
#include <asm/cpu.h>
#include <asm/hw_irq.h>
#include <asm/io_apic.h>
#include <asm/irq_remapping.h>
#include <asm/hypervisor.h>

#include "irq_remapping.h"

#ifdef CONFIG_IRQ_REMAP

/*
 * According 82093AA IO-APIC spec , IO APIC has a 24-entry Interrupt
 * Redirection Table. Hyper-V exposes one single IO-APIC and so define
 * 24 IO APIC remmapping entries.
 */
#define IOAPIC_REMAPPING_ENTRY 24

static cpumask_t ioapic_max_cpumask = { CPU_BITS_NONE };
static struct irq_domain *ioapic_ir_domain;

static int hyperv_ir_set_affinity(struct irq_data *data,
		const struct cpumask *mask, bool force)
{
	struct irq_data *parent = data->parent_data;
	struct irq_cfg *cfg = irqd_cfg(data);
	struct IO_APIC_route_entry *entry;
	int ret;

	/* Return error If new irq affinity is out of ioapic_max_cpumask. */
	if (!cpumask_subset(mask, &ioapic_max_cpumask))
		return -EINVAL;

	ret = parent->chip->irq_set_affinity(parent, mask, force);
	if (ret < 0 || ret == IRQ_SET_MASK_OK_DONE)
		return ret;

	entry = data->chip_data;
	entry->dest = cfg->dest_apicid;
	entry->vector = cfg->vector;
	send_cleanup_vector(cfg);

	return 0;
}

static struct irq_chip hyperv_ir_chip = {
	.name			= "HYPERV-IR",
	.irq_ack		= apic_ack_irq,
	.irq_set_affinity	= hyperv_ir_set_affinity,
};

static int hyperv_irq_remapping_alloc(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs,
				     void *arg)
{
	struct irq_alloc_info *info = arg;
	struct irq_data *irq_data;
	struct irq_desc *desc;
	int ret = 0;

	if (!info || info->type != X86_IRQ_ALLOC_TYPE_IOAPIC || nr_irqs > 1)
		return -EINVAL;

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, arg);
	if (ret < 0)
		return ret;

	irq_data = irq_domain_get_irq_data(domain, virq);
	if (!irq_data) {
		irq_domain_free_irqs_common(domain, virq, nr_irqs);
		return -EINVAL;
	}

	irq_data->chip = &hyperv_ir_chip;

	/*
	 * If there is interrupt remapping function of IOMMU, setting irq
	 * affinity only needs to change IRTE of IOMMU. But Hyper-V doesn't
	 * support interrupt remapping function, setting irq affinity of IO-APIC
	 * interrupts still needs to change IO-APIC registers. But ioapic_
	 * configure_entry() will ignore value of cfg->vector and cfg->
	 * dest_apicid when IO-APIC's parent irq domain is not the vector
	 * domain.(See ioapic_configure_entry()) In order to setting vector
	 * and dest_apicid to IO-APIC register, IO-APIC entry pointer is saved
	 * in the chip_data and hyperv_irq_remapping_activate()/hyperv_ir_set_
	 * affinity() set vector and dest_apicid directly into IO-APIC entry.
	 */
	irq_data->chip_data = info->ioapic_entry;

	/*
	 * Hypver-V IO APIC irq affinity should be in the scope of
	 * ioapic_max_cpumask because no irq remapping support.
	 */
	desc = irq_data_to_desc(irq_data);
	cpumask_copy(desc->irq_common_data.affinity, &ioapic_max_cpumask);

	return 0;
}

static void hyperv_irq_remapping_free(struct irq_domain *domain,
				 unsigned int virq, unsigned int nr_irqs)
{
	irq_domain_free_irqs_common(domain, virq, nr_irqs);
}

static int hyperv_irq_remapping_activate(struct irq_domain *domain,
			  struct irq_data *irq_data, bool reserve)
{
	struct irq_cfg *cfg = irqd_cfg(irq_data);
	struct IO_APIC_route_entry *entry = irq_data->chip_data;

	entry->dest = cfg->dest_apicid;
	entry->vector = cfg->vector;

	return 0;
}

static struct irq_domain_ops hyperv_ir_domain_ops = {
	.alloc = hyperv_irq_remapping_alloc,
	.free = hyperv_irq_remapping_free,
	.activate = hyperv_irq_remapping_activate,
};

static int __init hyperv_prepare_irq_remapping(void)
{
	struct fwnode_handle *fn;
	int i;

	if (!hypervisor_is_type(X86_HYPER_MS_HYPERV) ||
	    !x2apic_supported())
		return -ENODEV;

	fn = irq_domain_alloc_named_id_fwnode("HYPERV-IR", 0);
	if (!fn)
		return -ENOMEM;

	ioapic_ir_domain =
		irq_domain_create_hierarchy(arch_get_ir_parent_domain(),
				0, IOAPIC_REMAPPING_ENTRY, fn,
				&hyperv_ir_domain_ops, NULL);

	irq_domain_free_fwnode(fn);

	/*
	 * Hyper-V doesn't provide irq remapping function for
	 * IO-APIC and so IO-APIC only accepts 8-bit APIC ID.
	 * Cpu's APIC ID is read from ACPI MADT table and APIC IDs
	 * in the MADT table on Hyper-v are sorted monotonic increasingly.
	 * APIC ID reflects cpu topology. There maybe some APIC ID
	 * gaps when cpu number in a socket is not power of two. Prepare
	 * max cpu affinity for IOAPIC irqs. Scan cpu 0-255 and set cpu
	 * into ioapic_max_cpumask if its APIC ID is less than 256.
	 */
	for (i = min_t(unsigned int, num_possible_cpus() - 1, 255); i >= 0; i--)
		if (cpu_physical_id(i) < 256)
			cpumask_set_cpu(i, &ioapic_max_cpumask);

	return 0;
}

static int __init hyperv_enable_irq_remapping(void)
{
	return IRQ_REMAP_X2APIC_MODE;
}

static struct irq_domain *hyperv_get_ir_irq_domain(struct irq_alloc_info *info)
{
	if (info->type == X86_IRQ_ALLOC_TYPE_IOAPIC)
		return ioapic_ir_domain;
	else
		return NULL;
}

struct irq_remap_ops hyperv_irq_remap_ops = {
	.prepare		= hyperv_prepare_irq_remapping,
	.enable			= hyperv_enable_irq_remapping,
	.get_ir_irq_domain	= hyperv_get_ir_irq_domain,
};

#endif
