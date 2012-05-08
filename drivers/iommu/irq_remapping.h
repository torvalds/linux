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
 * This header file contains stuff that is shared between different interrupt
 * remapping drivers but with no need to be visible outside of the IOMMU layer.
 */

#ifndef __IRQ_REMAPPING_H
#define __IRQ_REMAPPING_H

#ifdef CONFIG_IRQ_REMAP

struct IO_APIC_route_entry;
struct io_apic_irq_attr;
struct irq_data;
struct cpumask;
struct pci_dev;
struct msi_msg;

extern int disable_irq_remap;
extern int disable_sourceid_checking;
extern int no_x2apic_optout;

struct irq_remap_ops {
	/* Check whether Interrupt Remapping is supported */
	int (*supported)(void);

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

	/* IO-APIC setup routine */
	int (*setup_ioapic_entry)(int irq, struct IO_APIC_route_entry *,
				  unsigned int, int,
				  struct io_apic_irq_attr *);

#ifdef CONFIG_SMP
	/* Set the CPU affinity of a remapped interrupt */
	int (*set_affinity)(struct irq_data *data, const struct cpumask *mask,
			    bool force);
#endif

	/* Free an IRQ */
	int (*free_irq)(int);

	/* Create MSI msg to use for interrupt remapping */
	void (*compose_msi_msg)(struct pci_dev *,
				unsigned int, unsigned int,
				struct msi_msg *, u8);

	/* Allocate remapping resources for MSI */
	int (*msi_alloc_irq)(struct pci_dev *, int, int);

	/* Setup the remapped MSI irq */
	int (*msi_setup_irq)(struct pci_dev *, unsigned int, int, int);

	/* Setup interrupt remapping for an HPET MSI */
	int (*setup_hpet_msi)(unsigned int, unsigned int);
};

extern struct irq_remap_ops intel_irq_remap_ops;

#endif /* CONFIG_IRQ_REMAP */

#endif /* __IRQ_REMAPPING_H */
