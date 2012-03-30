#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>

#include "intr_remapping.h"

int intr_remapping_enabled;

int disable_intremap;
int disable_sourceid_checking;
int no_x2apic_optout;

static struct irq_remap_ops *remap_ops;

static __init int setup_nointremap(char *str)
{
	disable_intremap = 1;
	return 0;
}
early_param("nointremap", setup_nointremap);

static __init int setup_intremap(char *str)
{
	if (!str)
		return -EINVAL;

	while (*str) {
		if (!strncmp(str, "on", 2))
			disable_intremap = 0;
		else if (!strncmp(str, "off", 3))
			disable_intremap = 1;
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
early_param("intremap", setup_intremap);

void __init setup_intr_remapping(void)
{
	remap_ops = &intel_irq_remap_ops;
}

int intr_remapping_supported(void)
{
	if (disable_intremap)
		return 0;

	if (!remap_ops || !remap_ops->supported)
		return 0;

	return remap_ops->supported();
}

int __init intr_hardware_init(void)
{
	if (!remap_ops || !remap_ops->hardware_init)
		return -ENODEV;

	return remap_ops->hardware_init();
}

int __init intr_hardware_enable(void)
{
	if (!remap_ops || !remap_ops->hardware_enable)
		return -ENODEV;

	return remap_ops->hardware_enable();
}

void intr_hardware_disable(void)
{
	if (!remap_ops || !remap_ops->hardware_disable)
		return;

	remap_ops->hardware_disable();
}

int intr_hardware_reenable(int mode)
{
	if (!remap_ops || !remap_ops->hardware_reenable)
		return 0;

	return remap_ops->hardware_reenable(mode);
}

int __init intr_enable_fault_handling(void)
{
	if (!remap_ops || !remap_ops->enable_faulting)
		return -ENODEV;

	return remap_ops->enable_faulting();
}

int intr_setup_ioapic_entry(int irq,
			    struct IO_APIC_route_entry *entry,
			    unsigned int destination, int vector,
			    struct io_apic_irq_attr *attr)
{
	if (!remap_ops || !remap_ops->setup_ioapic_entry)
		return -ENODEV;

	return remap_ops->setup_ioapic_entry(irq, entry, destination,
					     vector, attr);
}

int intr_set_affinity(struct irq_data *data, const struct cpumask *mask,
		      bool force)
{
	if (!remap_ops || !remap_ops->set_affinity)
		return 0;

	return remap_ops->set_affinity(data, mask, force);
}

void intr_free_irq(int irq)
{
	if (!remap_ops || !remap_ops->free_irq)
		return;

	remap_ops->free_irq(irq);
}
