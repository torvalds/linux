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
#include <asm/mshyperv.h>

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
	int ret;

	/* Return error If new irq affinity is out of ioapic_max_cpumask. */
	if (!cpumask_subset(mask, &ioapic_max_cpumask))
		return -EINVAL;

	ret = parent->chip->irq_set_affinity(parent, mask, force);
	if (ret < 0 || ret == IRQ_SET_MASK_OK_DONE)
		return ret;

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

static int hyperv_irq_remapping_select(struct irq_domain *d,
				       struct irq_fwspec *fwspec,
				       enum irq_domain_bus_token bus_token)
{
	/* Claim the only I/O APIC emulated by Hyper-V */
	return x86_fwspec_is_ioapic(fwspec);
}

static const struct irq_domain_ops hyperv_ir_domain_ops = {
	.select = hyperv_irq_remapping_select,
	.alloc = hyperv_irq_remapping_alloc,
	.free = hyperv_irq_remapping_free,
};

static const struct irq_domain_ops hyperv_root_ir_domain_ops;
static int __init hyperv_prepare_irq_remapping(void)
{
	struct fwnode_handle *fn;
	int i;
	const char *name;
	const struct irq_domain_ops *ops;

	if (!hypervisor_is_type(X86_HYPER_MS_HYPERV) ||
	    x86_init.hyper.msi_ext_dest_id() ||
	    !x2apic_supported())
		return -ENODEV;

	if (hv_root_partition) {
		name = "HYPERV-ROOT-IR";
		ops = &hyperv_root_ir_domain_ops;
	} else {
		name = "HYPERV-IR";
		ops = &hyperv_ir_domain_ops;
	}

	fn = irq_domain_alloc_named_id_fwnode(name, 0);
	if (!fn)
		return -ENOMEM;

	ioapic_ir_domain =
		irq_domain_create_hierarchy(arch_get_ir_parent_domain(),
				0, IOAPIC_REMAPPING_ENTRY, fn, ops, NULL);

	if (!ioapic_ir_domain) {
		irq_domain_free_fwnode(fn);
		return -ENOMEM;
	}

	if (hv_root_partition)
		return 0; /* The rest is only relevant to guests */

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

struct irq_remap_ops hyperv_irq_remap_ops = {
	.prepare		= hyperv_prepare_irq_remapping,
	.enable			= hyperv_enable_irq_remapping,
};

/* IRQ remapping domain when Linux runs as the root partition */
struct hyperv_root_ir_data {
	u8 ioapic_id;
	bool is_level;
	struct hv_interrupt_entry entry;
};

static void
hyperv_root_ir_compose_msi_msg(struct irq_data *irq_data, struct msi_msg *msg)
{
	u64 status;
	u32 vector;
	struct irq_cfg *cfg;
	int ioapic_id;
	struct cpumask *affinity;
	int cpu;
	struct hv_interrupt_entry entry;
	struct hyperv_root_ir_data *data = irq_data->chip_data;
	struct IO_APIC_route_entry e;

	cfg = irqd_cfg(irq_data);
	affinity = irq_data_get_effective_affinity_mask(irq_data);
	cpu = cpumask_first_and(affinity, cpu_online_mask);

	vector = cfg->vector;
	ioapic_id = data->ioapic_id;

	if (data->entry.source == HV_DEVICE_TYPE_IOAPIC
	    && data->entry.ioapic_rte.as_uint64) {
		entry = data->entry;

		status = hv_unmap_ioapic_interrupt(ioapic_id, &entry);

		if (status != HV_STATUS_SUCCESS)
			pr_debug("%s: unexpected unmap status %lld\n", __func__, status);

		data->entry.ioapic_rte.as_uint64 = 0;
		data->entry.source = 0; /* Invalid source */
	}


	status = hv_map_ioapic_interrupt(ioapic_id, data->is_level, cpu,
					vector, &entry);

	if (status != HV_STATUS_SUCCESS) {
		pr_err("%s: map hypercall failed, status %lld\n", __func__, status);
		return;
	}

	data->entry = entry;

	/* Turn it into an IO_APIC_route_entry, and generate MSI MSG. */
	e.w1 = entry.ioapic_rte.low_uint32;
	e.w2 = entry.ioapic_rte.high_uint32;

	memset(msg, 0, sizeof(*msg));
	msg->arch_data.vector = e.vector;
	msg->arch_data.delivery_mode = e.delivery_mode;
	msg->arch_addr_lo.dest_mode_logical = e.dest_mode_logical;
	msg->arch_addr_lo.dmar_format = e.ir_format;
	msg->arch_addr_lo.dmar_index_0_14 = e.ir_index_0_14;
}

static int hyperv_root_ir_set_affinity(struct irq_data *data,
		const struct cpumask *mask, bool force)
{
	struct irq_data *parent = data->parent_data;
	struct irq_cfg *cfg = irqd_cfg(data);
	int ret;

	ret = parent->chip->irq_set_affinity(parent, mask, force);
	if (ret < 0 || ret == IRQ_SET_MASK_OK_DONE)
		return ret;

	send_cleanup_vector(cfg);

	return 0;
}

static struct irq_chip hyperv_root_ir_chip = {
	.name			= "HYPERV-ROOT-IR",
	.irq_ack		= apic_ack_irq,
	.irq_set_affinity	= hyperv_root_ir_set_affinity,
	.irq_compose_msi_msg	= hyperv_root_ir_compose_msi_msg,
};

static int hyperv_root_irq_remapping_alloc(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs,
				     void *arg)
{
	struct irq_alloc_info *info = arg;
	struct irq_data *irq_data;
	struct hyperv_root_ir_data *data;
	int ret = 0;

	if (!info || info->type != X86_IRQ_ALLOC_TYPE_IOAPIC || nr_irqs > 1)
		return -EINVAL;

	ret = irq_domain_alloc_irqs_parent(domain, virq, nr_irqs, arg);
	if (ret < 0)
		return ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		irq_domain_free_irqs_common(domain, virq, nr_irqs);
		return -ENOMEM;
	}

	irq_data = irq_domain_get_irq_data(domain, virq);
	if (!irq_data) {
		kfree(data);
		irq_domain_free_irqs_common(domain, virq, nr_irqs);
		return -EINVAL;
	}

	data->ioapic_id = info->devid;
	data->is_level = info->ioapic.is_level;

	irq_data->chip = &hyperv_root_ir_chip;
	irq_data->chip_data = data;

	return 0;
}

static void hyperv_root_irq_remapping_free(struct irq_domain *domain,
				 unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *irq_data;
	struct hyperv_root_ir_data *data;
	struct hv_interrupt_entry *e;
	int i;

	for (i = 0; i < nr_irqs; i++) {
		irq_data = irq_domain_get_irq_data(domain, virq + i);

		if (irq_data && irq_data->chip_data) {
			data = irq_data->chip_data;
			e = &data->entry;

			if (e->source == HV_DEVICE_TYPE_IOAPIC
			      && e->ioapic_rte.as_uint64)
				hv_unmap_ioapic_interrupt(data->ioapic_id,
							&data->entry);

			kfree(data);
		}
	}

	irq_domain_free_irqs_common(domain, virq, nr_irqs);
}

static const struct irq_domain_ops hyperv_root_ir_domain_ops = {
	.select = hyperv_irq_remapping_select,
	.alloc = hyperv_root_irq_remapping_alloc,
	.free = hyperv_root_irq_remapping_free,
};

#endif
