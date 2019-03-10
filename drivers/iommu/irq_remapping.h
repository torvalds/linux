/*
 * Copyright (C) 2012 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <jroedel@suse.de>
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
 * This header file contains stuff that is shared between different interrupt
 * remapping drivers but with no need to be visible outside of the IOMMU layer.
 */

#ifndef __IRQ_REMAPPING_H
#define __IRQ_REMAPPING_H

#ifdef CONFIG_IRQ_REMAP

struct irq_data;
struct msi_msg;
struct irq_domain;
struct irq_alloc_info;

extern int irq_remap_broken;
extern int disable_sourceid_checking;
extern int no_x2apic_optout;
extern int irq_remapping_enabled;

extern int disable_irq_post;

struct irq_remap_ops {
	/* The supported capabilities */
	int capability;

	/* Initializes hardware and makes it ready for remapping interrupts */
	int  (*prepare)(void);

	/* Enables the remapping hardware */
	int  (*enable)(void);

	/* Disables the remapping hardware */
	void (*disable)(void);

	/* Reenables the remapping hardware */
	int  (*reenable)(int);

	/* Enable fault handling */
	int  (*enable_faulting)(void);

	/* Get the irqdomain associated the IOMMU device */
	struct irq_domain *(*get_ir_irq_domain)(struct irq_alloc_info *);

	/* Get the MSI irqdomain associated with the IOMMU device */
	struct irq_domain *(*get_irq_domain)(struct irq_alloc_info *);
};

extern struct irq_remap_ops intel_irq_remap_ops;
extern struct irq_remap_ops amd_iommu_irq_ops;
extern struct irq_remap_ops hyperv_irq_remap_ops;

#else  /* CONFIG_IRQ_REMAP */

#define irq_remapping_enabled 0
#define irq_remap_broken      0
#define disable_irq_post      1

#endif /* CONFIG_IRQ_REMAP */

#endif /* __IRQ_REMAPPING_H */
