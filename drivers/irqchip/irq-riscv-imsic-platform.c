// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 */

#define pr_fmt(fmt) "riscv-imsic: " fmt
#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/smp.h>

#include "irq-riscv-imsic-state.h"

static bool imsic_cpu_page_phys(unsigned int cpu, unsigned int guest_index,
				phys_addr_t *out_msi_pa)
{
	struct imsic_global_config *global;
	struct imsic_local_config *local;

	global = &imsic->global;
	local = per_cpu_ptr(global->local, cpu);

	if (BIT(global->guest_index_bits) <= guest_index)
		return false;

	if (out_msi_pa)
		*out_msi_pa = local->msi_pa + (guest_index * IMSIC_MMIO_PAGE_SZ);

	return true;
}

static void imsic_irq_mask(struct irq_data *d)
{
	imsic_vector_mask(irq_data_get_irq_chip_data(d));
}

static void imsic_irq_unmask(struct irq_data *d)
{
	imsic_vector_unmask(irq_data_get_irq_chip_data(d));
}

static int imsic_irq_retrigger(struct irq_data *d)
{
	struct imsic_vector *vec = irq_data_get_irq_chip_data(d);
	struct imsic_local_config *local;

	if (WARN_ON(!vec))
		return -ENOENT;

	local = per_cpu_ptr(imsic->global.local, vec->cpu);
	writel_relaxed(vec->local_id, local->msi_va);
	return 0;
}

static void imsic_irq_compose_vector_msg(struct imsic_vector *vec, struct msi_msg *msg)
{
	phys_addr_t msi_addr;

	if (WARN_ON(!vec))
		return;

	if (WARN_ON(!imsic_cpu_page_phys(vec->cpu, 0, &msi_addr)))
		return;

	msg->address_hi = upper_32_bits(msi_addr);
	msg->address_lo = lower_32_bits(msi_addr);
	msg->data = vec->local_id;
}

static void imsic_irq_compose_msg(struct irq_data *d, struct msi_msg *msg)
{
	imsic_irq_compose_vector_msg(irq_data_get_irq_chip_data(d), msg);
}

#ifdef CONFIG_SMP
static void imsic_msi_update_msg(struct irq_data *d, struct imsic_vector *vec)
{
	struct msi_msg msg = { };

	imsic_irq_compose_vector_msg(vec, &msg);
	irq_data_get_irq_chip(d)->irq_write_msi_msg(d, &msg);
}

static int imsic_irq_set_affinity(struct irq_data *d, const struct cpumask *mask_val,
				  bool force)
{
	struct imsic_vector *old_vec, *new_vec;

	old_vec = irq_data_get_irq_chip_data(d);
	if (WARN_ON(!old_vec))
		return -ENOENT;

	/* If old vector cpu belongs to the target cpumask then do nothing */
	if (cpumask_test_cpu(old_vec->cpu, mask_val))
		return IRQ_SET_MASK_OK_DONE;

	/* If move is already in-flight then return failure */
	if (imsic_vector_get_move(old_vec))
		return -EBUSY;

	/* Get a new vector on the desired set of CPUs */
	new_vec = imsic_vector_alloc(old_vec->hwirq, mask_val);
	if (!new_vec)
		return -ENOSPC;

	/* Point device to the new vector */
	imsic_msi_update_msg(irq_get_irq_data(d->irq), new_vec);

	/* Update irq descriptors with the new vector */
	d->chip_data = new_vec;

	/* Update effective affinity */
	irq_data_update_effective_affinity(d, cpumask_of(new_vec->cpu));

	/* Move state of the old vector to the new vector */
	imsic_vector_move(old_vec, new_vec);

	return IRQ_SET_MASK_OK_DONE;
}
#endif

static struct irq_chip imsic_irq_base_chip = {
	.name			= "IMSIC",
	.irq_mask		= imsic_irq_mask,
	.irq_unmask		= imsic_irq_unmask,
#ifdef CONFIG_SMP
	.irq_set_affinity	= imsic_irq_set_affinity,
#endif
	.irq_retrigger		= imsic_irq_retrigger,
	.irq_compose_msi_msg	= imsic_irq_compose_msg,
	.flags			= IRQCHIP_SKIP_SET_WAKE |
				  IRQCHIP_MASK_ON_SUSPEND,
};

static int imsic_irq_domain_alloc(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs, void *args)
{
	struct imsic_vector *vec;

	/* Multi-MSI is not supported yet. */
	if (nr_irqs > 1)
		return -EOPNOTSUPP;

	vec = imsic_vector_alloc(virq, cpu_online_mask);
	if (!vec)
		return -ENOSPC;

	irq_domain_set_info(domain, virq, virq, &imsic_irq_base_chip, vec,
			    handle_simple_irq, NULL, NULL);
	irq_set_noprobe(virq);
	irq_set_affinity(virq, cpu_online_mask);
	irq_data_update_effective_affinity(irq_get_irq_data(virq), cpumask_of(vec->cpu));

	return 0;
}

static void imsic_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				  unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);

	imsic_vector_free(irq_data_get_irq_chip_data(d));
	irq_domain_free_irqs_parent(domain, virq, nr_irqs);
}

static int imsic_irq_domain_select(struct irq_domain *domain, struct irq_fwspec *fwspec,
				   enum irq_domain_bus_token bus_token)
{
	const struct msi_parent_ops *ops = domain->msi_parent_ops;
	u32 busmask = BIT(bus_token);

	if (fwspec->fwnode != domain->fwnode || fwspec->param_count != 0)
		return 0;

	/* Handle pure domain searches */
	if (bus_token == ops->bus_select_token)
		return 1;

	return !!(ops->bus_select_mask & busmask);
}

#ifdef CONFIG_GENERIC_IRQ_DEBUGFS
static void imsic_irq_debug_show(struct seq_file *m, struct irq_domain *d,
				 struct irq_data *irqd, int ind)
{
	if (!irqd) {
		imsic_vector_debug_show_summary(m, ind);
		return;
	}

	imsic_vector_debug_show(m, irq_data_get_irq_chip_data(irqd), ind);
}
#endif

static const struct irq_domain_ops imsic_base_domain_ops = {
	.alloc		= imsic_irq_domain_alloc,
	.free		= imsic_irq_domain_free,
	.select		= imsic_irq_domain_select,
#ifdef CONFIG_GENERIC_IRQ_DEBUGFS
	.debug_show	= imsic_irq_debug_show,
#endif
};

#ifdef CONFIG_RISCV_IMSIC_PCI

static void imsic_pci_mask_irq(struct irq_data *d)
{
	pci_msi_mask_irq(d);
	irq_chip_mask_parent(d);
}

static void imsic_pci_unmask_irq(struct irq_data *d)
{
	irq_chip_unmask_parent(d);
	pci_msi_unmask_irq(d);
}

#define MATCH_PCI_MSI		BIT(DOMAIN_BUS_PCI_MSI)

#else

#define MATCH_PCI_MSI		0

#endif

static bool imsic_init_dev_msi_info(struct device *dev,
				    struct irq_domain *domain,
				    struct irq_domain *real_parent,
				    struct msi_domain_info *info)
{
	const struct msi_parent_ops *pops = real_parent->msi_parent_ops;

	/* MSI parent domain specific settings */
	switch (real_parent->bus_token) {
	case DOMAIN_BUS_NEXUS:
		if (WARN_ON_ONCE(domain != real_parent))
			return false;
#ifdef CONFIG_SMP
		info->chip->irq_set_affinity = irq_chip_set_affinity_parent;
#endif
		break;
	default:
		WARN_ON_ONCE(1);
		return false;
	}

	/* Is the target supported? */
	switch (info->bus_token) {
#ifdef CONFIG_RISCV_IMSIC_PCI
	case DOMAIN_BUS_PCI_DEVICE_MSI:
	case DOMAIN_BUS_PCI_DEVICE_MSIX:
		info->chip->irq_mask = imsic_pci_mask_irq;
		info->chip->irq_unmask = imsic_pci_unmask_irq;
		break;
#endif
	case DOMAIN_BUS_DEVICE_MSI:
		/*
		 * Per-device MSI should never have any MSI feature bits
		 * set. It's sole purpose is to create a dumb interrupt
		 * chip which has a device specific irq_write_msi_msg()
		 * callback.
		 */
		if (WARN_ON_ONCE(info->flags))
			return false;

		/* Core managed MSI descriptors */
		info->flags |= MSI_FLAG_ALLOC_SIMPLE_MSI_DESCS |
			       MSI_FLAG_FREE_MSI_DESCS;
		break;
	case DOMAIN_BUS_WIRED_TO_MSI:
		break;
	default:
		WARN_ON_ONCE(1);
		return false;
	}

	/* Use hierarchial chip operations re-trigger */
	info->chip->irq_retrigger = irq_chip_retrigger_hierarchy;

	/*
	 * Mask out the domain specific MSI feature flags which are not
	 * supported by the real parent.
	 */
	info->flags &= pops->supported_flags;

	/* Enforce the required flags */
	info->flags |= pops->required_flags;

	return true;
}

#define MATCH_PLATFORM_MSI		BIT(DOMAIN_BUS_PLATFORM_MSI)

static const struct msi_parent_ops imsic_msi_parent_ops = {
	.supported_flags	= MSI_GENERIC_FLAGS_MASK |
				  MSI_FLAG_PCI_MSIX,
	.required_flags		= MSI_FLAG_USE_DEF_DOM_OPS |
				  MSI_FLAG_USE_DEF_CHIP_OPS,
	.bus_select_token	= DOMAIN_BUS_NEXUS,
	.bus_select_mask	= MATCH_PCI_MSI | MATCH_PLATFORM_MSI,
	.init_dev_msi_info	= imsic_init_dev_msi_info,
};

int imsic_irqdomain_init(void)
{
	struct imsic_global_config *global;

	if (!imsic || !imsic->fwnode) {
		pr_err("early driver not probed\n");
		return -ENODEV;
	}

	if (imsic->base_domain) {
		pr_err("%pfwP: irq domain already created\n", imsic->fwnode);
		return -ENODEV;
	}

	/* Create Base IRQ domain */
	imsic->base_domain = irq_domain_create_tree(imsic->fwnode,
						    &imsic_base_domain_ops, imsic);
	if (!imsic->base_domain) {
		pr_err("%pfwP: failed to create IMSIC base domain\n", imsic->fwnode);
		return -ENOMEM;
	}
	imsic->base_domain->flags |= IRQ_DOMAIN_FLAG_MSI_PARENT;
	imsic->base_domain->msi_parent_ops = &imsic_msi_parent_ops;

	irq_domain_update_bus_token(imsic->base_domain, DOMAIN_BUS_NEXUS);

	global = &imsic->global;
	pr_info("%pfwP:  hart-index-bits: %d,  guest-index-bits: %d\n",
		imsic->fwnode, global->hart_index_bits, global->guest_index_bits);
	pr_info("%pfwP: group-index-bits: %d, group-index-shift: %d\n",
		imsic->fwnode, global->group_index_bits, global->group_index_shift);
	pr_info("%pfwP: per-CPU IDs %d at base address %pa\n",
		imsic->fwnode, global->nr_ids, &global->base_addr);
	pr_info("%pfwP: total %d interrupts available\n",
		imsic->fwnode, num_possible_cpus() * (global->nr_ids - 1));

	return 0;
}

static int imsic_platform_probe_common(struct fwnode_handle *fwnode)
{
	if (imsic && imsic->fwnode != fwnode) {
		pr_err("%pfwP: fwnode mismatch\n", fwnode);
		return -ENODEV;
	}

	return imsic_irqdomain_init();
}

static int imsic_platform_dt_probe(struct platform_device *pdev)
{
	return imsic_platform_probe_common(pdev->dev.fwnode);
}

#ifdef CONFIG_ACPI

/*
 *  On ACPI based systems, PCI enumeration happens early during boot in
 *  acpi_scan_init(). PCI enumeration expects MSI domain setup before
 *  it calls pci_set_msi_domain(). Hence, unlike in DT where
 *  imsic-platform drive probe happens late during boot, ACPI based
 *  systems need to setup the MSI domain early.
 */
int imsic_platform_acpi_probe(struct fwnode_handle *fwnode)
{
	return imsic_platform_probe_common(fwnode);
}

#endif

static const struct of_device_id imsic_platform_match[] = {
	{ .compatible = "riscv,imsics" },
	{}
};

static struct platform_driver imsic_platform_driver = {
	.driver = {
		.name		= "riscv-imsic",
		.of_match_table	= imsic_platform_match,
	},
	.probe = imsic_platform_dt_probe,
};
builtin_platform_driver(imsic_platform_driver);
