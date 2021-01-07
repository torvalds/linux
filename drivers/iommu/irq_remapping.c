// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cpumask.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/msi.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/irqdomain.h>

#include <asm/hw_irq.h>
#include <asm/irq_remapping.h>
#include <asm/processor.h>
#include <asm/x86_init.h>
#include <asm/apic.h>
#include <asm/hpet.h>

#include "irq_remapping.h"

int irq_remapping_enabled;
int irq_remap_broken;
int disable_sourceid_checking;
int no_x2apic_optout;

int disable_irq_post = 0;

static int disable_irq_remap;
static struct irq_remap_ops *remap_ops;

static void irq_remapping_restore_boot_irq_mode(void)
{
	/*
	 * With interrupt-remapping, for now we will use virtual wire A
	 * mode, as virtual wire B is little complex (need to configure
	 * both IOAPIC RTE as well as interrupt-remapping table entry).
	 * As this gets called during crash dump, keep this simple for
	 * now.
	 */
	if (boot_cpu_has(X86_FEATURE_APIC) || apic_from_smp_config())
		disconnect_bsp_APIC(0);
}

static void __init irq_remapping_modify_x86_ops(void)
{
	x86_apic_ops.restore = irq_remapping_restore_boot_irq_mode;
}

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
		if (!strncmp(str, "on", 2)) {
			disable_irq_remap = 0;
			disable_irq_post = 0;
		} else if (!strncmp(str, "off", 3)) {
			disable_irq_remap = 1;
			disable_irq_post = 1;
		} else if (!strncmp(str, "nosid", 5))
			disable_sourceid_checking = 1;
		else if (!strncmp(str, "no_x2apic_optout", 16))
			no_x2apic_optout = 1;
		else if (!strncmp(str, "nopost", 6))
			disable_irq_post = 1;

		str += strcspn(str, ",");
		while (*str == ',')
			str++;
	}

	return 0;
}
early_param("intremap", setup_irqremap);

void set_irq_remapping_broken(void)
{
	irq_remap_broken = 1;
}

bool irq_remapping_cap(enum irq_remap_cap cap)
{
	if (!remap_ops || disable_irq_post)
		return false;

	return (remap_ops->capability & (1 << cap));
}
EXPORT_SYMBOL_GPL(irq_remapping_cap);

int __init irq_remapping_prepare(void)
{
	if (disable_irq_remap)
		return -ENOSYS;

	if (intel_irq_remap_ops.prepare() == 0)
		remap_ops = &intel_irq_remap_ops;
	else if (IS_ENABLED(CONFIG_AMD_IOMMU) &&
		 amd_iommu_irq_ops.prepare() == 0)
		remap_ops = &amd_iommu_irq_ops;
	else if (IS_ENABLED(CONFIG_HYPERV_IOMMU) &&
		 hyperv_irq_remap_ops.prepare() == 0)
		remap_ops = &hyperv_irq_remap_ops;
	else
		return -ENOSYS;

	return 0;
}

int __init irq_remapping_enable(void)
{
	int ret;

	if (!remap_ops->enable)
		return -ENODEV;

	ret = remap_ops->enable();

	if (irq_remapping_enabled)
		irq_remapping_modify_x86_ops();

	return ret;
}

void irq_remapping_disable(void)
{
	if (irq_remapping_enabled && remap_ops->disable)
		remap_ops->disable();
}

int irq_remapping_reenable(int mode)
{
	if (irq_remapping_enabled && remap_ops->reenable)
		return remap_ops->reenable(mode);

	return 0;
}

int __init irq_remap_enable_fault_handling(void)
{
	if (!irq_remapping_enabled)
		return 0;

	if (!remap_ops->enable_faulting)
		return -ENODEV;

	return remap_ops->enable_faulting();
}

void panic_if_irq_remap(const char *msg)
{
	if (irq_remapping_enabled)
		panic(msg);
}
