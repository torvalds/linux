/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2013-2014, Linaro Ltd.
 *	Author: Al Stone <al.stone@linaro.org>
 *	Author: Graeme Gregory <graeme.gregory@linaro.org>
 *	Author: Hanjun Guo <hanjun.guo@linaro.org>
 *
 *  Copyright (C) 2021-2023, Ventana Micro Systems Inc.
 *	Author: Sunil V L <sunilvl@ventanamicro.com>
 */

#ifndef _ASM_ACPI_H
#define _ASM_ACPI_H

/* Basic configuration for ACPI */
#ifdef CONFIG_ACPI

typedef u64 phys_cpuid_t;
#define PHYS_CPUID_INVALID INVALID_HARTID

/* ACPI table mapping after acpi_permanent_mmap is set */
void *acpi_os_ioremap(acpi_physical_address phys, acpi_size size);
#define acpi_os_ioremap acpi_os_ioremap

#define acpi_strict 1	/* No out-of-spec workarounds on RISC-V */
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
 * to find out whether this cpu was already mapped (mapping from CPU hardware
 * ID to CPU logical ID) or not.
 */
#define cpu_physical_id(cpu) cpuid_to_hartid_map(cpu)

/*
 * Since MADT must provide at least one RINTC structure, the
 * CPU will be always available in MADT on RISC-V.
 */
static inline bool acpi_has_cpu_in_madt(void)
{
	return true;
}

static inline void arch_fix_phys_package_id(int num, u32 slot) { }

void acpi_init_rintc_map(void);
struct acpi_madt_rintc *acpi_cpu_get_madt_rintc(int cpu);
u32 get_acpi_id_for_cpu(int cpu);
int acpi_get_riscv_isa(struct acpi_table_header *table,
		       unsigned int cpu, const char **isa);

static inline int acpi_numa_get_nid(unsigned int cpu) { return NUMA_NO_NODE; }
#else
static inline void acpi_init_rintc_map(void) { }
static inline struct acpi_madt_rintc *acpi_cpu_get_madt_rintc(int cpu)
{
	return NULL;
}

static inline int acpi_get_riscv_isa(struct acpi_table_header *table,
				     unsigned int cpu, const char **isa)
{
	return -EINVAL;
}

#endif /* CONFIG_ACPI */

#endif /*_ASM_ACPI_H*/
