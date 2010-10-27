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

#ifndef _ASM_TILE_PCI_BRIDGE_H
#define _ASM_TILE_PCI_BRIDGE_H

#include <linux/ioport.h>
#include <linux/pci.h>

struct device_node;
struct pci_controller;

/*
 * pci_io_base returns the memory address at which you can access
 * the I/O space for PCI bus number `bus' (or NULL on error).
 */
extern void __iomem *pci_bus_io_base(unsigned int bus);
extern unsigned long pci_bus_io_base_phys(unsigned int bus);
extern unsigned long pci_bus_mem_base_phys(unsigned int bus);

/* Allocate a new PCI host bridge structure */
extern struct pci_controller *pcibios_alloc_controller(void);

/* Helper function for setting up resources */
extern void pci_init_resource(struct resource *res, unsigned long start,
			      unsigned long end, int flags, char *name);

/* Get the PCI host controller for a bus */
extern struct pci_controller *pci_bus_to_hose(int bus);

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

static inline struct pci_controller *pci_bus_to_host(struct pci_bus *bus)
{
	return bus->sysdata;
}

extern void setup_indirect_pci_nomap(struct pci_controller *hose,
			       void __iomem *cfg_addr, void __iomem *cfg_data);
extern void setup_indirect_pci(struct pci_controller *hose,
			       u32 cfg_addr, u32 cfg_data);
extern void setup_grackle(struct pci_controller *hose);

extern unsigned char common_swizzle(struct pci_dev *, unsigned char *);

/*
 *   The following code swizzles for exactly one bridge.  The routine
 *   common_swizzle below handles multiple bridges.  But there are a
 *   some boards that don't follow the PCI spec's suggestion so we
 *   break this piece out separately.
 */
static inline unsigned char bridge_swizzle(unsigned char pin,
		unsigned char idsel)
{
	return (((pin-1) + idsel) % 4) + 1;
}

/*
 * The following macro is used to lookup irqs in a standard table
 * format for those PPC systems that do not already have PCI
 * interrupts properly routed.
 */
/* FIXME - double check this */
#define PCI_IRQ_TABLE_LOOKUP ({ \
	long _ctl_ = -1; \
	if (idsel >= min_idsel && idsel <= max_idsel && pin <= irqs_per_slot) \
		_ctl_ = pci_irq_table[idsel - min_idsel][pin-1]; \
	_ctl_; \
})

/*
 * Scan the buses below a given PCI host bridge and assign suitable
 * resources to all devices found.
 */
extern int pciauto_bus_scan(struct pci_controller *, int);

#ifdef CONFIG_PCI
extern unsigned long pci_address_to_pio(phys_addr_t address);
#else
static inline unsigned long pci_address_to_pio(phys_addr_t address)
{
	return (unsigned long)-1;
}
#endif

#endif /* _ASM_TILE_PCI_BRIDGE_H */
