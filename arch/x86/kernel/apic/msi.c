// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support of MSI, HPET and DMAR interrupts.
 *
 * Copyright (C) 1997, 1998, 1999, 2000, 2009 Ingo Molnar, Hajnalka Szabo
 *	Moved from arch/x86/kernel/apic/io_apic.c.
 * Jiang Liu <jiang.liu@linux.intel.com>
 *	Convert to hierarchical irqdomain
 */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/dmar.h>
#include <linux/hpet.h>
#include <linux/msi.h>
#include <asm/irqdomain.h>
#include <asm/hpet.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/irq_remapping.h>
#include <asm/xen/hypervisor.h>

struct irq_domain *x86_pci_msi_default_domain __ro_after_init;

static void irq_msi_update_msg(struct irq_data *irqd, struct irq_cfg *cfg)
{
	struct msi_msg msg[2] = { [1] = { }, };

	__irq_msi_compose_msg(cfg, msg, false);
	irq_data_get_irq_chip(irqd)->irq_write_msi_msg(irqd, msg);
}

static int
msi_set_affinity(struct irq_data *irqd, const struct cpumask *mask, bool force)
{
	struct irq_cfg old_cfg, *cfg = irqd_cfg(irqd);
	struct irq_data *parent = irqd->parent_data;
	unsigned int cpu;
	int ret;

	/* Save the current configuration */
	cpu = cpumask_first(irq_data_get_effective_affinity_mask(irqd));
	old_cfg = *cfg;

	/* Allocate a new target vector */
	ret = parent->chip->irq_set_affinity(parent, mask, force);
	if (ret < 0 || ret == IRQ_SET_MASK_OK_DONE)
		return ret;

	/*
	 * For non-maskable and non-remapped MSI interrupts the migration
	 * to a different destination CPU and a different vector has to be
	 * done careful to handle the possible stray interrupt which can be
	 * caused by the non-atomic update of the address/data pair.
	 *
	 * Direct update is possible when:
	 * - The MSI is maskable (remapped MSI does not use this code path).
	 *   The reservation mode bit is set in this case.
	 * - The new vector is the same as the old vector
	 * - The old vector is MANAGED_IRQ_SHUTDOWN_VECTOR (interrupt starts up)
	 * - The interrupt is not yet started up
	 * - The new destination CPU is the same as the old destination CPU
	 */
	if (!irqd_can_reserve(irqd) ||
	    cfg->vector == old_cfg.vector ||
	    old_cfg.vector == MANAGED_IRQ_SHUTDOWN_VECTOR ||
	    !irqd_is_started(irqd) ||
	    cfg->dest_apicid == old_cfg.dest_apicid) {
		irq_msi_update_msg(irqd, cfg);
		return ret;
	}

	/*
	 * Paranoia: Validate that the interrupt target is the local
	 * CPU.
	 */
	if (WARN_ON_ONCE(cpu != smp_processor_id())) {
		irq_msi_update_msg(irqd, cfg);
		return ret;
	}

	/*
	 * Redirect the interrupt to the new vector on the current CPU
	 * first. This might cause a spurious interrupt on this vector if
	 * the device raises an interrupt right between this update and the
	 * update to the final destination CPU.
	 *
	 * If the vector is in use then the installed device handler will
	 * denote it as spurious which is no harm as this is a rare event
	 * and interrupt handlers have to cope with spurious interrupts
	 * anyway. If the vector is unused, then it is marked so it won't
	 * trigger the 'No irq handler for vector' warning in
	 * common_interrupt().
	 *
	 * This requires to hold vector lock to prevent concurrent updates to
	 * the affected vector.
	 */
	lock_vector_lock();

	/*
	 * Mark the new target vector on the local CPU if it is currently
	 * unused. Reuse the VECTOR_RETRIGGERED state which is also used in
	 * the CPU hotplug path for a similar purpose. This cannot be
	 * undone here as the current CPU has interrupts disabled and
	 * cannot handle the interrupt before the whole set_affinity()
	 * section is done. In the CPU unplug case, the current CPU is
	 * about to vanish and will not handle any interrupts anymore. The
	 * vector is cleaned up when the CPU comes online again.
	 */
	if (IS_ERR_OR_NULL(this_cpu_read(vector_irq[cfg->vector])))
		this_cpu_write(vector_irq[cfg->vector], VECTOR_RETRIGGERED);

	/* Redirect it to the new vector on the local CPU temporarily */
	old_cfg.vector = cfg->vector;
	irq_msi_update_msg(irqd, &old_cfg);

	/* Now transition it to the target CPU */
	irq_msi_update_msg(irqd, cfg);

	/*
	 * All interrupts after this point are now targeted at the new
	 * vector/CPU.
	 *
	 * Drop vector lock before testing whether the temporary assignment
	 * to the local CPU was hit by an interrupt raised in the device,
	 * because the retrigger function acquires vector lock again.
	 */
	unlock_vector_lock();

	/*
	 * Check whether the transition raced with a device interrupt and
	 * is pending in the local APICs IRR. It is safe to do this outside
	 * of vector lock as the irq_desc::lock of this interrupt is still
	 * held and interrupts are disabled: The check is not accessing the
	 * underlying vector store. It's just checking the local APIC's
	 * IRR.
	 */
	if (lapic_vector_set_in_irr(cfg->vector))
		irq_data_get_irq_chip(irqd)->irq_retrigger(irqd);

	return ret;
}

/**
 * pci_dev_has_default_msi_parent_domain - Check whether the device has the default
 *					   MSI parent domain associated
 * @dev:	Pointer to the PCI device
 */
bool pci_dev_has_default_msi_parent_domain(struct pci_dev *dev)
{
	struct irq_domain *domain = dev_get_msi_domain(&dev->dev);

	if (!domain)
		domain = dev_get_msi_domain(&dev->bus->dev);
	if (!domain)
		return false;

	return domain == x86_vector_domain;
}

/**
 * x86_msi_prepare - Setup of msi_alloc_info_t for allocations
 * @domain:	The domain for which this setup happens
 * @dev:	The device for which interrupts are allocated
 * @nvec:	The number of vectors to allocate
 * @alloc:	The allocation info structure to initialize
 *
 * This function is to be used for all types of MSI domains above the x86
 * vector domain and any intermediates. It is always invoked from the
 * top level interrupt domain. The domain specific allocation
 * functionality is determined via the @domain's bus token which allows to
 * map the X86 specific allocation type.
 */
static int x86_msi_prepare(struct irq_domain *domain, struct device *dev,
			   int nvec, msi_alloc_info_t *alloc)
{
	struct msi_domain_info *info = domain->host_data;

	init_irq_alloc_info(alloc, NULL);

	switch (info->bus_token) {
	case DOMAIN_BUS_PCI_DEVICE_MSI:
		alloc->type = X86_IRQ_ALLOC_TYPE_PCI_MSI;
		return 0;
	case DOMAIN_BUS_PCI_DEVICE_MSIX:
		alloc->type = X86_IRQ_ALLOC_TYPE_PCI_MSIX;
		return 0;
	default:
		return -EINVAL;
	}
}

/**
 * x86_init_dev_msi_info - Domain info setup for MSI domains
 * @dev:		The device for which the domain should be created
 * @domain:		The (root) domain providing this callback
 * @real_parent:	The real parent domain of the to initialize domain
 * @info:		The domain info for the to initialize domain
 *
 * This function is to be used for all types of MSI domains above the x86
 * vector domain and any intermediates. The domain specific functionality
 * is determined via the @real_parent.
 */
static bool x86_init_dev_msi_info(struct device *dev, struct irq_domain *domain,
				  struct irq_domain *real_parent, struct msi_domain_info *info)
{
	const struct msi_parent_ops *pops = real_parent->msi_parent_ops;

	/* MSI parent domain specific settings */
	switch (real_parent->bus_token) {
	case DOMAIN_BUS_ANY:
		/* Only the vector domain can have the ANY token */
		if (WARN_ON_ONCE(domain != real_parent))
			return false;
		info->chip->irq_set_affinity = msi_set_affinity;
		break;
	case DOMAIN_BUS_DMAR:
	case DOMAIN_BUS_AMDVI:
		break;
	default:
		WARN_ON_ONCE(1);
		return false;
	}

	/* Is the target supported? */
	switch(info->bus_token) {
	case DOMAIN_BUS_PCI_DEVICE_MSI:
	case DOMAIN_BUS_PCI_DEVICE_MSIX:
		break;
	default:
		WARN_ON_ONCE(1);
		return false;
	}

	/*
	 * Mask out the domain specific MSI feature flags which are not
	 * supported by the real parent.
	 */
	info->flags			&= pops->supported_flags;
	/* Enforce the required flags */
	info->flags			|= X86_VECTOR_MSI_FLAGS_REQUIRED;

	/* This is always invoked from the top level MSI domain! */
	info->ops->msi_prepare		= x86_msi_prepare;

	info->chip->irq_ack		= irq_chip_ack_parent;
	info->chip->irq_retrigger	= irq_chip_retrigger_hierarchy;
	info->chip->flags		|= IRQCHIP_SKIP_SET_WAKE |
					   IRQCHIP_AFFINITY_PRE_STARTUP;

	info->handler			= handle_edge_irq;
	info->handler_name		= "edge";

	return true;
}

static const struct msi_parent_ops x86_vector_msi_parent_ops = {
	.supported_flags	= X86_VECTOR_MSI_FLAGS_SUPPORTED,
	.init_dev_msi_info	= x86_init_dev_msi_info,
};

struct irq_domain * __init native_create_pci_msi_domain(void)
{
	if (apic_is_disabled)
		return NULL;

	x86_vector_domain->flags |= IRQ_DOMAIN_FLAG_MSI_PARENT;
	x86_vector_domain->msi_parent_ops = &x86_vector_msi_parent_ops;
	return x86_vector_domain;
}

void __init x86_create_pci_msi_domain(void)
{
	x86_pci_msi_default_domain = x86_init.irqs.create_pci_msi_domain();
}

/* Keep around for hyperV */
int pci_msi_prepare(struct irq_domain *domain, struct device *dev, int nvec,
		    msi_alloc_info_t *arg)
{
	init_irq_alloc_info(arg, NULL);

	if (to_pci_dev(dev)->msix_enabled)
		arg->type = X86_IRQ_ALLOC_TYPE_PCI_MSIX;
	else
		arg->type = X86_IRQ_ALLOC_TYPE_PCI_MSI;
	return 0;
}
EXPORT_SYMBOL_GPL(pci_msi_prepare);

#ifdef CONFIG_DMAR_TABLE
/*
 * The Intel IOMMU (ab)uses the high bits of the MSI address to contain the
 * high bits of the destination APIC ID. This can't be done in the general
 * case for MSIs as it would be targeting real memory above 4GiB not the
 * APIC.
 */
static void dmar_msi_compose_msg(struct irq_data *data, struct msi_msg *msg)
{
	__irq_msi_compose_msg(irqd_cfg(data), msg, true);
}

static void dmar_msi_write_msg(struct irq_data *data, struct msi_msg *msg)
{
	dmar_msi_write(data->irq, msg);
}

static struct irq_chip dmar_msi_controller = {
	.name			= "DMAR-MSI",
	.irq_unmask		= dmar_msi_unmask,
	.irq_mask		= dmar_msi_mask,
	.irq_ack		= irq_chip_ack_parent,
	.irq_set_affinity	= msi_domain_set_affinity,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_compose_msi_msg	= dmar_msi_compose_msg,
	.irq_write_msi_msg	= dmar_msi_write_msg,
	.flags			= IRQCHIP_SKIP_SET_WAKE |
				  IRQCHIP_AFFINITY_PRE_STARTUP,
};

static int dmar_msi_init(struct irq_domain *domain,
			 struct msi_domain_info *info, unsigned int virq,
			 irq_hw_number_t hwirq, msi_alloc_info_t *arg)
{
	irq_domain_set_info(domain, virq, arg->devid, info->chip, NULL,
			    handle_edge_irq, arg->data, "edge");

	return 0;
}

static struct msi_domain_ops dmar_msi_domain_ops = {
	.msi_init	= dmar_msi_init,
};

static struct msi_domain_info dmar_msi_domain_info = {
	.ops		= &dmar_msi_domain_ops,
	.chip		= &dmar_msi_controller,
	.flags		= MSI_FLAG_USE_DEF_DOM_OPS,
};

static struct irq_domain *dmar_get_irq_domain(void)
{
	static struct irq_domain *dmar_domain;
	static DEFINE_MUTEX(dmar_lock);
	struct fwnode_handle *fn;

	mutex_lock(&dmar_lock);
	if (dmar_domain)
		goto out;

	fn = irq_domain_alloc_named_fwnode("DMAR-MSI");
	if (fn) {
		dmar_domain = msi_create_irq_domain(fn, &dmar_msi_domain_info,
						    x86_vector_domain);
		if (!dmar_domain)
			irq_domain_free_fwnode(fn);
	}
out:
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
	info.devid = id;
	info.hwirq = id;
	info.data = arg;

	return irq_domain_alloc_irqs(domain, 1, node, &info);
}

void dmar_free_hwirq(int irq)
{
	irq_domain_free_irqs(irq, 1);
}
#endif

bool arch_restore_msi_irqs(struct pci_dev *dev)
{
	return xen_initdom_restore_msi(dev);
}
