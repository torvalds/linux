/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2013-2014, Linaro Ltd.
 *	Author: Al Stone <al.stone@linaro.org>
 *	Author: Graeme Gregory <graeme.gregory@linaro.org>
 *	Author: Hanjun Guo <hanjun.guo@linaro.org>
 */

#ifndef _ASM_ACPI_H
#define _ASM_ACPI_H

#include <linux/efi.h>
#include <linux/memblock.h>
#include <linux/psci.h>
#include <linux/stddef.h>

#include <asm/cputype.h>
#include <asm/io.h>
#include <asm/ptrace.h>
#include <asm/smp_plat.h>
#include <asm/tlbflush.h>

/* Macros for consistency checks of the GICC subtable of MADT */

/*
 * MADT GICC minimum length refers to the MADT GICC structure table length as
 * defined in the earliest ACPI version supported on arm64, ie ACPI 5.1.
 *
 * The efficiency_class member was added to the
 * struct acpi_madt_generic_interrupt to represent the MADT GICC structure
 * "Processor Power Efficiency Class" field, added in ACPI 6.0 whose offset
 * is therefore used to delimit the MADT GICC structure minimum length
 * appropriately.
 */
#define ACPI_MADT_GICC_MIN_LENGTH   offsetof(  \
	struct acpi_madt_generic_interrupt, efficiency_class)

#define BAD_MADT_GICC_ENTRY(entry, end)					\
	(!(entry) || (entry)->header.length < ACPI_MADT_GICC_MIN_LENGTH || \
	(unsigned long)(entry) + (entry)->header.length > (end))

#define ACPI_MADT_GICC_SPE  (offsetof(struct acpi_madt_generic_interrupt, \
	spe_interrupt) + sizeof(u16))

#define ACPI_MADT_GICC_TRBE  (offsetof(struct acpi_madt_generic_interrupt, \
	trbe_interrupt) + sizeof(u16))

/* Basic configuration for ACPI */
#ifdef	CONFIG_ACPI
pgprot_t __acpi_get_mem_attribute(phys_addr_t addr);

/* ACPI table mapping after acpi_permanent_mmap is set */
void __iomem *acpi_os_ioremap(acpi_physical_address phys, acpi_size size);
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

struct acpi_madt_generic_interrupt *acpi_cpu_get_madt_gicc(int cpu);
static inline u32 get_acpi_id_for_cpu(unsigned int cpu)
{
	return	acpi_cpu_get_madt_gicc(cpu)->uid;
}

static inline void arch_fix_phys_package_id(int num, u32 slot) { }
void __init acpi_init_cpus(void);
int apei_claim_sea(struct pt_regs *regs);
#else
static inline void acpi_init_cpus(void) { }
static inline int apei_claim_sea(struct pt_regs *regs) { return -ENOENT; }
#endif /* CONFIG_ACPI */

#ifdef CONFIG_ARM64_ACPI_PARKING_PROTOCOL
bool acpi_parking_protocol_valid(int cpu);
void __init
acpi_set_mailbox_entry(int cpu, struct acpi_madt_generic_interrupt *processor);
#else
static inline bool acpi_parking_protocol_valid(int cpu) { return false; }
static inline void
acpi_set_mailbox_entry(int cpu, struct acpi_madt_generic_interrupt *processor)
{}
#endif

static inline const char *acpi_get_enable_method(int cpu)
{
	if (acpi_psci_present())
		return "psci";

	if (acpi_parking_protocol_valid(cpu))
		return "parking-protocol";

	return NULL;
}

#ifdef	CONFIG_ACPI_APEI
/*
 * acpi_disable_cmcff is used in drivers/acpi/apei/hest.c for disabling
 * IA-32 Architecture Corrected Machine Check (CMC) Firmware-First mode
 * with a kernel command line parameter "acpi=nocmcoff". But we don't
 * have this IA-32 specific feature on ARM64, this definition is only
 * for compatibility.
 */
#define acpi_disable_cmcff 1
static inline pgprot_t arch_apei_get_mem_attribute(phys_addr_t addr)
{
	return __acpi_get_mem_attribute(addr);
}
#endif /* CONFIG_ACPI_APEI */

#ifdef CONFIG_ACPI_NUMA
int arm64_acpi_numa_init(void);
int acpi_numa_get_nid(unsigned int cpu);
void acpi_map_cpus_to_nodes(void);
#else
static inline int arm64_acpi_numa_init(void) { return -ENOSYS; }
static inline int acpi_numa_get_nid(unsigned int cpu) { return NUMA_NO_NODE; }
static inline void acpi_map_cpus_to_nodes(void) { }
#endif /* CONFIG_ACPI_NUMA */

#define ACPI_TABLE_UPGRADE_MAX_PHYS MEMBLOCK_ALLOC_ACCESSIBLE

#endif /*_ASM_ACPI_H*/
