/*
 * Copyright (C) 2012 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * This header file contains the interface of the interrupt remapping code to
 * the x86 interrupt management code.
 */

#ifndef __X86_IRQ_REMAPPING_H
#define __X86_IRQ_REMAPPING_H

#include <linux/irqdomain.h>
#include <asm/hw_irq.h>
#include <asm/io_apic.h>

struct IO_APIC_route_entry;
struct io_apic_irq_attr;
struct irq_chip;
struct msi_msg;
struct pci_dev;
struct irq_cfg;
struct irq_alloc_info;

#ifdef CONFIG_IRQ_REMAP

extern void set_irq_remapping_broken(void);
extern int irq_remapping_prepare(void);
extern int irq_remapping_enable(void);
extern void irq_remapping_disable(void);
extern int irq_remapping_reenable(int);
extern int irq_remap_enable_fault_handling(void);
extern int setup_ioapic_remapped_entry(int irq,
				       struct IO_APIC_route_entry *entry,
				       unsigned int destination,
				       int vector,
				       struct io_apic_irq_attr *attr);
extern void free_remapped_irq(int irq);
extern void panic_if_irq_remap(const char *msg);
extern bool setup_remapped_irq(int irq,
			       struct irq_cfg *cfg,
			       struct irq_chip *chip);

void irq_remap_modify_chip_defaults(struct irq_chip *chip);

extern struct irq_domain *
irq_remapping_get_ir_irq_domain(struct irq_alloc_info *info);
extern struct irq_domain *
irq_remapping_get_irq_domain(struct irq_alloc_info *info);

/* Create PCI MSI/MSIx irqdomain, use @parent as the parent irqdomain. */
extern struct irq_domain *arch_create_msi_irq_domain(struct irq_domain *parent);

/* Get parent irqdomain for interrupt remapping irqdomain */
static inline struct irq_domain *arch_get_ir_parent_domain(void)
{
	return x86_vector_domain;
}

#else  /* CONFIG_IRQ_REMAP */

static inline void set_irq_remapping_broken(void) { }
static inline int irq_remapping_prepare(void) { return -ENODEV; }
static inline int irq_remapping_enable(void) { return -ENODEV; }
static inline void irq_remapping_disable(void) { }
static inline int irq_remapping_reenable(int eim) { return -ENODEV; }
static inline int irq_remap_enable_fault_handling(void) { return -ENODEV; }
static inline int setup_ioapic_remapped_entry(int irq,
					      struct IO_APIC_route_entry *entry,
					      unsigned int destination,
					      int vector,
					      struct io_apic_irq_attr *attr)
{
	return -ENODEV;
}
static inline void free_remapped_irq(int irq) { }

static inline void panic_if_irq_remap(const char *msg)
{
}

static inline void irq_remap_modify_chip_defaults(struct irq_chip *chip)
{
}

static inline bool setup_remapped_irq(int irq,
				      struct irq_cfg *cfg,
				      struct irq_chip *chip)
{
	return false;
}

static inline struct irq_domain *
irq_remapping_get_ir_irq_domain(struct irq_alloc_info *info)
{
	return NULL;
}

static inline struct irq_domain *
irq_remapping_get_irq_domain(struct irq_alloc_info *info)
{
	return NULL;
}

#endif /* CONFIG_IRQ_REMAP */
#endif /* __X86_IRQ_REMAPPING_H */
