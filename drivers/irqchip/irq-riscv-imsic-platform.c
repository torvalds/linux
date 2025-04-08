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

#include "irq-msi-lib.h"
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

static void imsic_irq_ack(struct irq_data *d)
{
	irq_move_irq(d);
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
	struct imsic_vector tmp_vec;

	/*
	 * Requirements for the downstream irqdomains (or devices):
	 *
	 * 1) Downstream irqdomains (or devices) with atomic MSI update can
	 *    happily do imsic_irq_set_affinity() in the process-context on
	 *    any CPU so the irqchip of such irqdomains must not set the
	 *    IRQCHIP_MOVE_DEFERRED flag.
	 *
	 * 2) Downstream irqdomains (or devices) with non-atomic MSI update
	 *    must use imsic_irq_set_affinity() in nterrupt-context upon
	 *    the next device interrupt so the irqchip of such irqdomains
	 *    must set the IRQCHIP_MOVE_DEFERRED flag.
	 */

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
	new_vec = imsic_vector_alloc(old_vec->irq, mask_val);
	if (!new_vec)
		return -ENOSPC;

	/*
	 * Device having non-atomic MSI update might see an intermediate
	 * state when changing target IMSIC vector from one CPU to another.
	 *
	 * To avoid losing interrupt to such intermediate state, do the
	 * following (just like x86 APIC):
	 *
	 * 1) First write a temporary IMSIC vector to the device which
	 * has MSI address same as the old IMSIC vector but MSI data
	 * matches the new IMSIC vector.
	 *
	 * 2) Next write the new IMSIC vector to the device.
	 *
	 * Based on the above, __imsic_local_sync() must check pending
	 * status of both old MSI data and new MSI data on the old CPU.
	 */
	if (!irq_can_move_in_process_context(d) &&
	    new_vec->local_id != old_vec->local_id) {
		/* Setup temporary vector */
		tmp_vec.cpu = old_vec->cpu;
		tmp_vec.local_id = new_vec->local_id;

		/* Point device to the temporary vector */
		imsic_msi_update_msg(irq_get_irq_data(d->irq), &tmp_vec);
	}

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

static void imsic_irq_force_complete_move(struct irq_data *d)
{
	struct imsic_vector *mvec, *vec = irq_data_get_irq_chip_data(d);
	unsigned int cpu = smp_processor_id();

	if (WARN_ON(!vec))
		return;

	/* Do nothing if there is no in-flight move */
	mvec = imsic_vector_get_move(vec);
	if (!mvec)
		return;

	/* Do nothing if the old IMSIC vector does not belong to current CPU */
	if (mvec->cpu != cpu)
		return;

	/*
	 * The best we can do is force cleanup the old IMSIC vector.
	 *
	 * The challenges over here are same as x86 vector domain so
	 * refer to the comments in irq_force_complete_move() function
	 * implemented at arch/x86/kernel/apic/vector.c.
	 */

	/* Force cleanup in-flight move */
	pr_info("IRQ fixup: irq %d move in progress, old vector cpu %d local_id %d\n",
		d->irq, mvec->cpu, mvec->local_id);
	imsic_vector_force_move_cleanup(vec);
}
#endif

static struct irq_chip imsic_irq_base_chip = {
	.name				= "IMSIC",
	.irq_mask			= imsic_irq_mask,
	.irq_unmask			= imsic_irq_unmask,
#ifdef CONFIG_SMP
	.irq_set_affinity		= imsic_irq_set_affinity,
	.irq_force_complete_move	= imsic_irq_force_complete_move,
#endif
	.irq_retrigger			= imsic_irq_retrigger,
	.irq_ack			= imsic_irq_ack,
	.irq_compose_msi_msg		= imsic_irq_compose_msg,
	.flags				= IRQCHIP_SKIP_SET_WAKE | IRQCHIP_MASK_ON_SUSPEND,
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
			    handle_edge_irq, NULL, NULL);
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
	.select		= msi_lib_irq_domain_select,
#ifdef CONFIG_GENERIC_IRQ_DEBUGFS
	.debug_show	= imsic_irq_debug_show,
#endif
};

static bool imsic_init_dev_msi_info(struct device *dev, struct irq_domain *domain,
				    struct irq_domain *real_parent, struct msi_domain_info *info)
{
	if (!msi_lib_init_dev_msi_info(dev, domain, real_parent, info))
		return false;

	switch (info->bus_token) {
	case DOMAIN_BUS_PCI_DEVICE_MSI:
	case DOMAIN_BUS_PCI_DEVICE_MSIX:
		info->chip->flags |= IRQCHIP_MOVE_DEFERRED;
		break;
	default:
		break;
	}

	return true;
}

static const struct msi_parent_ops imsic_msi_parent_ops = {
	.supported_flags	= MSI_GENERIC_FLAGS_MASK |
				  MSI_FLAG_PCI_MSIX,
	.required_flags		= MSI_FLAG_USE_DEF_DOM_OPS |
				  MSI_FLAG_USE_DEF_CHIP_OPS |
				  MSI_FLAG_PCI_MSI_MASK_PARENT,
	.chip_flags		= MSI_CHIP_FLAG_SET_ACK,
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
