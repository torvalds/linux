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

#include <asm/pci-bridge.h>

/*
 * The hypervisor maps the entirety of CPA-space as bus addresses, so
 * bus addresses are physical addresses.  The networking and block
 * device layers use this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS     1

struct pci_controller *pci_bus_to_hose(int bus);
unsigned char __init common_swizzle(struct pci_dev *dev, unsigned char *pinp);
int __init tile_pci_init(void);
void pci_iounmap(struct pci_dev *dev, void __iomem *addr);
void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long max);
void __devinit pcibios_fixup_bus(struct pci_bus *bus);

int __devinit _tile_cfg_read(struct pci_controller *hose,
				    int bus,
				    int slot,
				    int function,
				    int offset,
				    int size,
				    u32 *val);
int __devinit _tile_cfg_write(struct pci_controller *hose,
				     int bus,
				     int slot,
				     int function,
				     int offset,
				     int size,
				     u32 val);

/*
 * These are used to to config reads and writes in the early stages of
 * setup before the driver infrastructure has been set up enough to be
 * able to do config reads and writes.
 */
#define early_cfg_read(where, size, value) \
	_tile_cfg_read(controller, \
		       current_bus, \
		       pci_slot, \
		       pci_fn, \
		       where, \
		       size, \
		       value)

#define early_cfg_write(where, size, value) \
	_tile_cfg_write(controller, \
		       current_bus, \
		       pci_slot, \
		       pci_fn, \
		       where, \
		       size, \
		       value)



#define PCICFG_BYTE	1
#define PCICFG_WORD	2
#define PCICFG_DWORD	4

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
 * I/O space is currently not supported.
 */

#define TILE_PCIE_LOWER_IO		0x0
#define TILE_PCIE_UPPER_IO		0x10000
#define TILE_PCIE_PCIE_IO_SIZE		0x0000FFFF

#define _PAGE_NO_CACHE		0
#define _PAGE_GUARDED		0


#define pcibios_assign_all_busses()    pci_assign_all_buses
extern int pci_assign_all_buses;

static inline void pcibios_set_master(struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
}

#define PCIBIOS_MIN_MEM		0
#define PCIBIOS_MIN_IO		TILE_PCIE_LOWER_IO

/*
 * This flag tells if the platform is TILEmpower that needs
 * special configuration for the PLX switch chip.
 */
extern int blade_pci;

/* implement the pci_ DMA API in terms of the generic device dma_ one */
#include <asm-generic/pci-dma-compat.h>

/* generic pci stuff */
#include <asm-generic/pci.h>

/* Use any cpu for PCI. */
#define cpumask_of_pcibus(bus) cpu_online_mask

#endif /* _ASM_TILE_PCI_H */
