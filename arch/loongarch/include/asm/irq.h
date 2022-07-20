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

int get_ipi_irq(void);
int get_pmc_irq(void);
int get_timer_irq(void);
void spurious_interrupt(void);

#define NR_IRQS_LEGACY 16

#define arch_trigger_cpumask_backtrace arch_trigger_cpumask_backtrace
void arch_trigger_cpumask_backtrace(const struct cpumask *mask, bool exclude_self);

#define MAX_IO_PICS 2
#define NR_IRQS	(64 + (256 * MAX_IO_PICS))

struct acpi_vector_group {
	int node;
	int pci_segment;
	struct irq_domain *parent;
};
extern struct acpi_vector_group pch_group[MAX_IO_PICS];
extern struct acpi_vector_group msi_group[MAX_IO_PICS];

#define CORES_PER_EIO_NODE	4

#define LOONGSON_CPU_UART0_VEC		10 /* CPU UART0 */
#define LOONGSON_CPU_THSENS_VEC		14 /* CPU Thsens */
#define LOONGSON_CPU_HT0_VEC		16 /* CPU HT0 irq vector base number */
#define LOONGSON_CPU_HT1_VEC		24 /* CPU HT1 irq vector base number */

/* IRQ number definitions */
#define LOONGSON_LPC_IRQ_BASE		0
#define LOONGSON_LPC_LAST_IRQ		(LOONGSON_LPC_IRQ_BASE + 15)

#define LOONGSON_CPU_IRQ_BASE		16
#define LOONGSON_CPU_LAST_IRQ		(LOONGSON_CPU_IRQ_BASE + 14)

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

extern int find_pch_pic(u32 gsi);
extern int eiointc_get_node(int id);

static inline void eiointc_enable(void)
{
	uint64_t misc;

	misc = iocsr_read64(LOONGARCH_IOCSR_MISC_FUNC);
	misc |= IOCSR_MISC_FUNC_EXT_IOI_EN;
	iocsr_write64(misc, LOONGARCH_IOCSR_MISC_FUNC);
}

struct acpi_madt_lio_pic;
struct acpi_madt_eio_pic;
struct acpi_madt_ht_pic;
struct acpi_madt_bio_pic;
struct acpi_madt_msi_pic;
struct acpi_madt_lpc_pic;

struct irq_domain *loongarch_cpu_irq_init(void);

int liointc_acpi_init(struct irq_domain *parent,
					struct acpi_madt_lio_pic *acpi_liointc);
struct irq_domain *eiointc_acpi_init(struct irq_domain *parent,
					struct acpi_madt_eio_pic *acpi_eiointc);

struct irq_domain *htvec_acpi_init(struct irq_domain *parent,
					struct acpi_madt_ht_pic *acpi_htvec);
int pch_lpc_acpi_init(struct irq_domain *parent,
					struct acpi_madt_lpc_pic *acpi_pchlpc);
#if IS_ENABLED(CONFIG_LOONGSON_PCH_MSI)
int pch_msi_acpi_init(struct irq_domain *parent,
					struct acpi_madt_msi_pic *acpi_pchmsi);
#else
static inline int pch_msi_acpi_init(struct irq_domain *parent,
					struct acpi_madt_msi_pic *acpi_pchmsi)
{
	return 0;
}
#endif
int pch_pic_acpi_init(struct irq_domain *parent,
					struct acpi_madt_bio_pic *acpi_pchpic);
int find_pch_pic(u32 gsi);
struct fwnode_handle *get_pch_msi_handle(int pci_segment);

extern struct acpi_madt_lio_pic *acpi_liointc;
extern struct acpi_madt_eio_pic *acpi_eiointc[MAX_IO_PICS];

extern struct acpi_madt_ht_pic *acpi_htintc;
extern struct acpi_madt_lpc_pic *acpi_pchlpc;
extern struct acpi_madt_msi_pic *acpi_pchmsi[MAX_IO_PICS];
extern struct acpi_madt_bio_pic *acpi_pchpic[MAX_IO_PICS];

extern struct irq_domain *cpu_domain;
extern struct fwnode_handle *liointc_handle;
extern struct fwnode_handle *pch_lpc_handle;
extern struct fwnode_handle *pch_pic_handle[MAX_IO_PICS];

extern irqreturn_t loongson3_ipi_interrupt(int irq, void *dev);

#include <asm-generic/irq.h>

#endif /* _ASM_IRQ_H */
