/*
 *  Copyright (C) 2013-2014, Linaro Ltd.
 *	Author: Al Stone <al.stone@linaro.org>
 *	Author: Graeme Gregory <graeme.gregory@linaro.org>
 *	Author: Hanjun Guo <hanjun.guo@linaro.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation;
 */

#ifndef _ASM_ACPI_H
#define _ASM_ACPI_H

#include <linux/mm.h>
#include <linux/irqchip/arm-gic-acpi.h>

#include <asm/cputype.h>
#include <asm/psci.h>
#include <asm/smp_plat.h>

/* Macros for consistency checks of the GICC subtable of MADT */
#define ACPI_MADT_GICC_LENGTH	\
	(acpi_gbl_FADT.header.revision < 6 ? 76 : 80)

#define BAD_MADT_GICC_ENTRY(entry, end)						\
	(!(entry) || (unsigned long)(entry) + sizeof(*(entry)) > (end) ||	\
	 (entry)->header.length != ACPI_MADT_GICC_LENGTH)

/* Basic configuration for ACPI */
#ifdef	CONFIG_ACPI
/* ACPI table mapping after acpi_gbl_permanent_mmap is set */
static inline void __iomem *acpi_os_ioremap(acpi_physical_address phys,
					    acpi_size size)
{
	if (!page_is_ram(phys >> PAGE_SHIFT))
		return ioremap(phys, size);

	return ioremap_cache(phys, size);
}
#define acpi_os_ioremap acpi_os_ioremap

typedef u64 phys_cpuid_t;
#define PHYS_CPUID_INVALID INVALID_HWID

#define acpi_strict 1	/* No out-of-spec workarounds on ARM64 */
extern int acpi_disabled;
extern int acpi_noirq;
extern int acpi_pci_disabled;

static inline void disable_acpi(void)
{
	acpi_disabled = 1;
	acpi_pci_disabled = 1;
	acpi_noirq = 1;
}

static inline void enable_acpi(void)
{
	acpi_disabled = 0;
	acpi_pci_disabled = 0;
	acpi_noirq = 0;
}

/*
 * The ACPI processor driver for ACPI core code needs this macro
 * to find out this cpu was already mapped (mapping from CPU hardware
 * ID to CPU logical ID) or not.
 */
#define cpu_physical_id(cpu) cpu_logical_map(cpu)

/*
 * It's used from ACPI core in kdump to boot UP system with SMP kernel,
 * with this check the ACPI core will not override the CPU index
 * obtained from GICC with 0 and not print some error message as well.
 * Since MADT must provide at least one GICC structure for GIC
 * initialization, CPU will be always available in MADT on ARM64.
 */
static inline bool acpi_has_cpu_in_madt(void)
{
	return true;
}

static inline void arch_fix_phys_package_id(int num, u32 slot) { }
void __init acpi_init_cpus(void);

#else
static inline void acpi_init_cpus(void) { }
#endif /* CONFIG_ACPI */

static inline const char *acpi_get_enable_method(int cpu)
{
	return acpi_psci_present() ? "psci" : NULL;
}
#endif /*_ASM_ACPI_H*/
