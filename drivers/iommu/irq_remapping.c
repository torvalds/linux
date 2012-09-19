#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/cpumask.h>
#include <linux/errno.h>
#include <linux/msi.h>

#include <asm/hw_irq.h>
#include <asm/irq_remapping.h>

#include "irq_remapping.h"

int irq_remapping_enabled;

int disable_irq_remap;
int disable_sourceid_checking;
int no_x2apic_optout;

static struct irq_remap_ops *remap_ops;

static __init int setup_nointremap(char *str)
{
	disable_irq_remap = 1;
	return 0;
}
early_param("nointremap", setup_nointremap);

static __init int setup_irqremap(char *str)
{
	if (!str)
		return -EINVAL;

	while (*str) {
		if (!strncmp(str, "on", 2))
			disable_irq_remap = 0;
		else if (!strncmp(str, "off", 3))
			disable_irq_remap = 1;
		else if (!strncmp(str, "nosid", 5))
			disable_sourceid_checking = 1;
		else if (!strncmp(str, "no_x2apic_optout", 16))
			no_x2apic_optout = 1;

		str += strcspn(str, ",");
		while (*str == ',')
			str++;
	}

	return 0;
}
early_param("intremap", setup_irqremap);

void __init setup_irq_remapping_ops(void)
{
	remap_ops = &intel_irq_remap_ops;
}

int irq_remapping_supported(void)
{
	if (disable_irq_remap)
		return 0;

	if (!remap_ops || !remap_ops->supported)
		return 0;

	return remap_ops->supported();
}

int __init irq_remapping_prepare(void)
{
	if (!remap_ops || !remap_ops->prepare)
		return -ENODEV;

	return remap_ops->prepare();
}

int __init irq_remapping_enable(void)
{
	if (!remap_ops || !remap_ops->enable)
		return -ENODEV;

	return remap_ops->enable();
}

void irq_remapping_disable(void)
{
	if (!remap_ops || !remap_ops->disable)
		return;

	remap_ops->disable();
}

int irq_remapping_reenable(int mode)
{
	if (!remap_ops || !remap_ops->reenable)
		return 0;

	return remap_ops->reenable(mode);
}

int __init irq_remap_enable_fault_handling(void)
{
	if (!remap_ops || !remap_ops->enable_faulting)
		return -ENODEV;

	return remap_ops->enable_faulting();
}

int setup_ioapic_remapped_entry(int irq,
				struct IO_APIC_route_entry *entry,
				unsigned int destination, int vector,
				struct io_apic_irq_attr *attr)
{
	if (!remap_ops || !remap_ops->setup_ioapic_entry)
		return -ENODEV;

	return remap_ops->setup_ioapic_entry(irq, entry, destination,
					     vector, attr);
}

int set_remapped_irq_affinity(struct irq_data *data, const struct cpumask *mask,
			      bool force)
{
	if (!config_enabled(CONFIG_SMP) || !remap_ops ||
	    !remap_ops->set_affinity)
		return 0;

	return remap_ops->set_affinity(data, mask, force);
}

void free_remapped_irq(int irq)
{
	if (!remap_ops || !remap_ops->free_irq)
		return;

	remap_ops->free_irq(irq);
}

void compose_remapped_msi_msg(struct pci_dev *pdev,
			      unsigned int irq, unsigned int dest,
			      struct msi_msg *msg, u8 hpet_id)
{
	if (!remap_ops || !remap_ops->compose_msi_msg)
		return;

	remap_ops->compose_msi_msg(pdev, irq, dest, msg, hpet_id);
}

int msi_alloc_remapped_irq(struct pci_dev *pdev, int irq, int nvec)
{
	if (!remap_ops || !remap_ops->msi_alloc_irq)
		return -ENODEV;

	return remap_ops->msi_alloc_irq(pdev, irq, nvec);
}

int msi_setup_remapped_irq(struct pci_dev *pdev, unsigned int irq,
			   int index, int sub_handle)
{
	if (!remap_ops || !remap_ops->msi_setup_irq)
		return -ENODEV;

	return remap_ops->msi_setup_irq(pdev, irq, index, sub_handle);
}

int setup_hpet_msi_remapped(unsigned int irq, unsigned int id)
{
	if (!remap_ops || !remap_ops->setup_hpet_msi)
		return -ENODEV;

	return remap_ops->setup_hpet_msi(irq, id);
}
