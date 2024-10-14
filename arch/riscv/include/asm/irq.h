/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#ifndef _ASM_RISCV_IRQ_H
#define _ASM_RISCV_IRQ_H

#include <linux/interrupt.h>
#include <linux/linkage.h>

#include <asm-generic/irq.h>

#define INVALID_CONTEXT UINT_MAX

#ifdef CONFIG_SMP
void arch_trigger_cpumask_backtrace(const cpumask_t *mask, int exclude_cpu);
#define arch_trigger_cpumask_backtrace arch_trigger_cpumask_backtrace
#endif

void riscv_set_intc_hwnode_fn(struct fwnode_handle *(*fn)(void));

struct fwnode_handle *riscv_get_intc_hwnode(void);

#ifdef CONFIG_ACPI

enum riscv_irqchip_type {
	ACPI_RISCV_IRQCHIP_INTC		= 0x00,
	ACPI_RISCV_IRQCHIP_IMSIC	= 0x01,
	ACPI_RISCV_IRQCHIP_PLIC		= 0x02,
	ACPI_RISCV_IRQCHIP_APLIC	= 0x03,
};

int riscv_acpi_get_gsi_info(struct fwnode_handle *fwnode, u32 *gsi_base,
			    u32 *id, u32 *nr_irqs, u32 *nr_idcs);
struct fwnode_handle *riscv_acpi_get_gsi_domain_id(u32 gsi);
unsigned long acpi_rintc_index_to_hartid(u32 index);
unsigned long acpi_rintc_ext_parent_to_hartid(unsigned int plic_id, unsigned int ctxt_idx);
unsigned int acpi_rintc_get_plic_nr_contexts(unsigned int plic_id);
unsigned int acpi_rintc_get_plic_context(unsigned int plic_id, unsigned int ctxt_idx);
int __init acpi_rintc_get_imsic_mmio_info(u32 index, struct resource *res);

#else
static inline int riscv_acpi_get_gsi_info(struct fwnode_handle *fwnode, u32 *gsi_base,
					  u32 *id, u32 *nr_irqs, u32 *nr_idcs)
{
	return 0;
}

static inline unsigned long acpi_rintc_index_to_hartid(u32 index)
{
	return INVALID_HARTID;
}

static inline unsigned long acpi_rintc_ext_parent_to_hartid(unsigned int plic_id,
							    unsigned int ctxt_idx)
{
	return INVALID_HARTID;
}

static inline unsigned int acpi_rintc_get_plic_nr_contexts(unsigned int plic_id)
{
	return INVALID_CONTEXT;
}

static inline unsigned int acpi_rintc_get_plic_context(unsigned int plic_id, unsigned int ctxt_idx)
{
	return INVALID_CONTEXT;
}

static inline int __init acpi_rintc_get_imsic_mmio_info(u32 index, struct resource *res)
{
	return 0;
}

#endif /* CONFIG_ACPI */

#endif /* _ASM_RISCV_IRQ_H */
