/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <linux/irqdomain.h>
#include <linux/irqreturn.h>

#define IRQ_STACK_SIZE			THREAD_SIZE
#define IRQ_STACK_START			(IRQ_STACK_SIZE - 16)

DECLARE_PER_CPU(unsigned long, irq_stack);

/*
 * The highest address on the IRQ stack contains a dummy frame which is
 * structured as follows:
 *
 *   top ------------
 *       | task sp  | <- irq_stack[cpu] + IRQ_STACK_START
 *       ------------
 *       |          | <- First frame of IRQ context
 *       ------------
 *
 * task sp holds a copy of the task stack pointer where the struct pt_regs
 * from exception entry can be found.
 */

static inline bool on_irq_stack(int cpu, unsigned long sp)
{
	unsigned long low = per_cpu(irq_stack, cpu);
	unsigned long high = low + IRQ_STACK_SIZE;

	return (low <= sp && sp <= high);
}

void spurious_interrupt(void);

#define NR_IRQS_LEGACY 16

/*
 * 256 Vectors Mapping for AVECINTC:
 *
 * 0 - 15: Mapping classic IPs, e.g. IP0-12.
 * 16 - 255: Mapping vectors for external IRQ.
 *
 */
#define NR_VECTORS		256
#define NR_LEGACY_VECTORS	16
#define IRQ_MATRIX_BITS		NR_VECTORS

#define arch_trigger_cpumask_backtrace arch_trigger_cpumask_backtrace
void arch_trigger_cpumask_backtrace(const struct cpumask *mask, int exclude_cpu);

#define MAX_IO_PICS 8
#define NR_IRQS	(64 + NR_VECTORS * (NR_CPUS + MAX_IO_PICS))

struct acpi_vector_group {
	int node;
	int pci_segment;
	struct irq_domain *parent;
};
extern struct acpi_vector_group pch_group[MAX_IO_PICS];
extern struct acpi_vector_group msi_group[MAX_IO_PICS];

#define CORES_PER_EIO_NODE	4
#define CORES_PER_VEIO_NODE	256

#define LOONGSON_CPU_UART0_VEC		10 /* CPU UART0 */
#define LOONGSON_CPU_THSENS_VEC		14 /* CPU Thsens */
#define LOONGSON_CPU_HT0_VEC		16 /* CPU HT0 irq vector base number */
#define LOONGSON_CPU_HT1_VEC		24 /* CPU HT1 irq vector base number */

/* IRQ number definitions */
#define LOONGSON_LPC_IRQ_BASE		0
#define LOONGSON_LPC_LAST_IRQ		(LOONGSON_LPC_IRQ_BASE + 15)

#define LOONGSON_CPU_IRQ_BASE		16
#define LOONGSON_CPU_LAST_IRQ		(LOONGSON_CPU_IRQ_BASE + 15)

#define LOONGSON_PCH_IRQ_BASE		64
#define LOONGSON_PCH_ACPI_IRQ		(LOONGSON_PCH_IRQ_BASE + 47)
#define LOONGSON_PCH_LAST_IRQ		(LOONGSON_PCH_IRQ_BASE + 64 - 1)

#define LOONGSON_MSI_IRQ_BASE		(LOONGSON_PCH_IRQ_BASE + 64)
#define LOONGSON_MSI_LAST_IRQ		(LOONGSON_PCH_IRQ_BASE + 256 - 1)

#define GSI_MIN_LPC_IRQ		LOONGSON_LPC_IRQ_BASE
#define GSI_MAX_LPC_IRQ		(LOONGSON_LPC_IRQ_BASE + 16 - 1)
#define GSI_MIN_CPU_IRQ		LOONGSON_CPU_IRQ_BASE
#define GSI_MAX_CPU_IRQ		(LOONGSON_CPU_IRQ_BASE + 48 - 1)
#define GSI_MIN_PCH_IRQ		LOONGSON_PCH_IRQ_BASE
#define GSI_MAX_PCH_IRQ		(LOONGSON_PCH_IRQ_BASE + 256 - 1)

struct acpi_madt_lio_pic;
struct acpi_madt_eio_pic;
struct acpi_madt_ht_pic;
struct acpi_madt_bio_pic;
struct acpi_madt_msi_pic;
struct acpi_madt_lpc_pic;

void complete_irq_moving(void);

struct fwnode_handle *get_pch_msi_handle(int pci_segment);

extern struct acpi_madt_lio_pic *acpi_liointc;
extern struct acpi_madt_eio_pic *acpi_eiointc[MAX_IO_PICS];

extern struct acpi_madt_ht_pic *acpi_htintc;
extern struct acpi_madt_lpc_pic *acpi_pchlpc;
extern struct acpi_madt_msi_pic *acpi_pchmsi[MAX_IO_PICS];
extern struct acpi_madt_bio_pic *acpi_pchpic[MAX_IO_PICS];

extern struct fwnode_handle *cpuintc_handle;
extern struct fwnode_handle *liointc_handle;
extern struct fwnode_handle *pch_lpc_handle;
extern struct fwnode_handle *pch_pic_handle[MAX_IO_PICS];

static inline int get_percpu_irq(int vector)
{
	struct irq_domain *d;

	d = irq_find_matching_fwnode(cpuintc_handle, DOMAIN_BUS_ANY);
	if (d)
		return irq_create_mapping(d, vector);

	return -EINVAL;
}

#include <asm-generic/irq.h>

#endif /* _ASM_IRQ_H */
