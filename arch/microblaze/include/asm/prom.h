/*
 * Definitions for talking to the Open Firmware PROM on
 * Power Macintosh computers.
 *
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 * Updates for PPC64 by Peter Bergner & David Engebretsen, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/of.h>	/* linux/of.h gets to determine #include ordering */

#ifndef _ASM_MICROBLAZE_PROM_H
#define _ASM_MICROBLAZE_PROM_H
#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_fdt.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <asm/irq.h>
#include <asm/atomic.h>

#define HAVE_ARCH_DEVTREE_FIXUPS

/* Other Prototypes */
extern int early_uartlite_console(void);

#ifdef CONFIG_PCI
/*
 * PCI <-> OF matching functions
 * (XXX should these be here?)
 */
struct pci_bus;
struct pci_dev;
extern int pci_device_from_OF_node(struct device_node *node,
					u8 *bus, u8 *devfn);
extern struct device_node *pci_busdev_to_OF_node(struct pci_bus *bus,
							int devfn);
extern struct device_node *pci_device_to_OF_node(struct pci_dev *dev);
extern void pci_create_OF_bus_map(void);
#endif

/*
 * OF address retreival & translation
 */

/* Extract an address from a device, returns the region size and
 * the address space flags too. The PCI version uses a BAR number
 * instead of an absolute index
 */
extern const u32 *of_get_address(struct device_node *dev, int index,
			u64 *size, unsigned int *flags);
extern const u32 *of_get_pci_address(struct device_node *dev, int bar_no,
			u64 *size, unsigned int *flags);

extern int of_pci_address_to_resource(struct device_node *dev, int bar,
				struct resource *r);

#ifdef CONFIG_PCI
extern unsigned long pci_address_to_pio(phys_addr_t address);
#else
static inline unsigned long pci_address_to_pio(phys_addr_t address)
{
	return (unsigned long)-1;
}
#endif	/* CONFIG_PCI */

/* Parse the ibm,dma-window property of an OF node into the busno, phys and
 * size parameters.
 */
void of_parse_dma_window(struct device_node *dn, const void *dma_window_prop,
		unsigned long *busno, unsigned long *phys, unsigned long *size);

extern void kdump_move_device_tree(void);

/* CPU OF node matching */
struct device_node *of_get_cpu_node(int cpu, unsigned int *thread);

/* Get the MAC address */
extern const void *of_get_mac_address(struct device_node *np);

/**
 * of_irq_map_pci - Resolve the interrupt for a PCI device
 * @pdev:	the device whose interrupt is to be resolved
 * @out_irq:	structure of_irq filled by this function
 *
 * This function resolves the PCI interrupt for a given PCI device. If a
 * device-node exists for a given pci_dev, it will use normal OF tree
 * walking. If not, it will implement standard swizzling and walk up the
 * PCI tree until an device-node is found, at which point it will finish
 * resolving using the OF tree walking.
 */
struct pci_dev;
extern int of_irq_map_pci(struct pci_dev *pdev, struct of_irq *out_irq);

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_MICROBLAZE_PROM_H */
