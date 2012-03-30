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

#ifndef __X86_INTR_REMAPPING_H
#define __X86_INTR_REMAPPING_H

#ifdef CONFIG_IRQ_REMAP

struct IO_APIC_route_entry;
struct io_apic_irq_attr;
struct pci_dev;

extern int intr_remapping_enabled;

extern void setup_intr_remapping(void);
extern int intr_remapping_supported(void);
extern int intr_hardware_init(void);
extern int intr_hardware_enable(void);
extern void intr_hardware_disable(void);
extern int intr_hardware_reenable(int);
extern int intr_enable_fault_handling(void);
extern int intr_setup_ioapic_entry(int irq,
				   struct IO_APIC_route_entry *entry,
				   unsigned int destination, int vector,
				   struct io_apic_irq_attr *attr);
extern int intr_set_affinity(struct irq_data *data,
			     const struct cpumask *mask,
			     bool force);
extern void intr_free_irq(int irq);
extern void intr_compose_msi_msg(struct pci_dev *pdev,
				 unsigned int irq, unsigned int dest,
				 struct msi_msg *msg, u8 hpet_id);
extern int intr_msi_alloc_irq(struct pci_dev *pdev, int irq, int nvec);
extern int intr_msi_setup_irq(struct pci_dev *pdev, unsigned int irq,
			      int index, int sub_handle);
extern int intr_setup_hpet_msi(unsigned int irq, unsigned int id);

#else  /* CONFIG_IRQ_REMAP */

#define intr_remapping_enabled	0

static inline void setup_intr_remapping(void) { }
static inline int intr_remapping_supported(void) { return 0; }
static inline int intr_hardware_init(void) { return -ENODEV; }
static inline int intr_hardware_enable(void) { return -ENODEV; }
static inline void intr_hardware_disable(void) { }
static inline int intr_hardware_reenable(int eim) { return -ENODEV; }
static inline int intr_enable_fault_handling(void) { return -ENODEV; }
static inline int intr_setup_ioapic_entry(int irq,
					  struct IO_APIC_route_entry *entry,
					  unsigned int destination, int vector,
					  struct io_apic_irq_attr *attr)
{
	return -ENODEV;
}
static inline int intr_set_affinity(struct irq_data *data,
				    const struct cpumask *mask,
				    bool force)
{
	return 0;
}
static inline void intr_free_irq(int irq) { }
static inline void intr_compose_msi_msg(struct pci_dev *pdev,
					unsigned int irq, unsigned int dest,
					struct msi_msg *msg, u8 hpet_id)
{
}
static inline int intr_msi_alloc_irq(struct pci_dev *pdev, int irq, int nvec)
{
	return -ENODEV;
}
static inline int intr_msi_setup_irq(struct pci_dev *pdev, unsigned int irq,
				     int index, int sub_handle)
{
	return -ENODEV;
}
static inline int intr_setup_hpet_msi(unsigned int irq, unsigned int id)
{
	return -ENODEV;
}
#endif /* CONFIG_IRQ_REMAP */

#endif /* __X86_INTR_REMAPPING_H */
