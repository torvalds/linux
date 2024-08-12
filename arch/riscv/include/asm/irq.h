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

#else
static inline int riscv_acpi_get_gsi_info(struct fwnode_handle *fwnode, u32 *gsi_base,
					  u32 *id, u32 *nr_irqs, u32 *nr_idcs)
{
	return 0;
}

#endif /* CONFIG_ACPI */

#endif /* _ASM_RISCV_IRQ_H */
