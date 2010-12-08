/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_PCI_H
#define _ASM_TILE_PCI_H

#include <linux/pci.h>

/*
 * Structure of a PCI controller (host bridge)
 */
struct pci_controller {
	int index;		/* PCI domain number */
	struct pci_bus *root_bus;

	int first_busno;
	int last_busno;

	int hv_cfg_fd[2];	/* config{0,1} fds for this PCIe controller */
	int hv_mem_fd;		/* fd to Hypervisor for MMIO operations */

	struct pci_ops *ops;

	int irq_base;		/* Base IRQ from the Hypervisor	*/
	int plx_gen1;		/* flag for PLX Gen 1 configuration */

	/* Address ranges that are routed to this controller/bridge. */
	struct resource mem_resources[3];
};

/*
 * The hypervisor maps the entirety of CPA-space as bus addresses, so
 * bus addresses are physical addresses.  The networking and block
 * device layers use this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS     1

int __init tile_pci_init(void);

void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long max);
static inline void pci_iounmap(struct pci_dev *dev, void __iomem *addr) {}

void __devinit pcibios_fixup_bus(struct pci_bus *bus);

#define	TILE_NUM_PCIE	2

#define pci_domain_nr(bus) (((struct pci_controller *)(bus)->sysdata)->index)

/*
 * This decides whether to display the domain number in /proc.
 */
static inline int pci_proc_domain(struct pci_bus *bus)
{
	return 1;
}

/*
 * pcibios_assign_all_busses() tells whether or not the bus numbers
 * should be reassigned, in case the BIOS didn't do it correctly, or
 * in case we don't have a BIOS and we want to let Linux do it.
 */
static inline int pcibios_assign_all_busses(void)
{
	return 1;
}

/*
 * No special bus mastering setup handling.
 */
static inline void pcibios_set_master(struct pci_dev *dev)
{
}

#define PCIBIOS_MIN_MEM		0
#define PCIBIOS_MIN_IO		0

/*
 * This flag tells if the platform is TILEmpower that needs
 * special configuration for the PLX switch chip.
 */
extern int tile_plx_gen1;

/* Use any cpu for PCI. */
#define cpumask_of_pcibus(bus) cpu_online_mask

/* implement the pci_ DMA API in terms of the generic device dma_ one */
#include <asm-generic/pci-dma-compat.h>

/* generic pci stuff */
#include <asm-generic/pci.h>

#endif /* _ASM_TILE_PCI_H */
