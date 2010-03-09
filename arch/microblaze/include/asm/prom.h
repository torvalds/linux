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
#include <linux/of_fdt.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <asm/irq.h>
#include <asm/atomic.h>

#define HAVE_ARCH_DEVTREE_FIXUPS

/* Other Prototypes */
extern int early_uartlite_console(void);

/*
 * OF address retreival & translation
 */

/* Translate an OF address block into a CPU physical address
 */
extern u64 of_translate_address(struct device_node *np, const u32 *addr);

/* Extract an address from a device, returns the region size and
 * the address space flags too. The PCI version uses a BAR number
 * instead of an absolute index
 */
extern const u32 *of_get_address(struct device_node *dev, int index,
			u64 *size, unsigned int *flags);
extern const u32 *of_get_pci_address(struct device_node *dev, int bar_no,
			u64 *size, unsigned int *flags);

/* Get an address as a resource. Note that if your address is
 * a PIO address, the conversion will fail if the physical address
 * can't be internally converted to an IO token with
 * pci_address_to_pio(), that is because it's either called to early
 * or it can't be matched to any host bridge IO space
 */
extern int of_address_to_resource(struct device_node *dev, int index,
				struct resource *r);
extern int of_pci_address_to_resource(struct device_node *dev, int bar,
				struct resource *r);

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

/*
 * OF interrupt mapping
 */

/* This structure is returned when an interrupt is mapped. The controller
 * field needs to be put() after use
 */

#define OF_MAX_IRQ_SPEC		4 /* We handle specifiers of at most 4 cells */

struct of_irq {
	struct device_node *controller; /* Interrupt controller node */
	u32 size; /* Specifier size */
	u32 specifier[OF_MAX_IRQ_SPEC]; /* Specifier copy */
};

/**
 * of_irq_map_init - Initialize the irq remapper
 * @flags:	flags defining workarounds to enable
 *
 * Some machines have bugs in the device-tree which require certain workarounds
 * to be applied. Call this before any interrupt mapping attempts to enable
 * those workarounds.
 */
#define OF_IMAP_OLDWORLD_MAC	0x00000001
#define OF_IMAP_NO_PHANDLE	0x00000002

extern void of_irq_map_init(unsigned int flags);

/**
 * of_irq_map_raw - Low level interrupt tree parsing
 * @parent:	the device interrupt parent
 * @intspec:	interrupt specifier ("interrupts" property of the device)
 * @ointsize:	size of the passed in interrupt specifier
 * @addr:	address specifier (start of "reg" property of the device)
 * @out_irq:	structure of_irq filled by this function
 *
 * Returns 0 on success and a negative number on error
 *
 * This function is a low-level interrupt tree walking function. It
 * can be used to do a partial walk with synthetized reg and interrupts
 * properties, for example when resolving PCI interrupts when no device
 * node exist for the parent.
 *
 */

extern int of_irq_map_raw(struct device_node *parent, const u32 *intspec,
			u32 ointsize, const u32 *addr,
			struct of_irq *out_irq);

/**
 * of_irq_map_one - Resolve an interrupt for a device
 * @device:	the device whose interrupt is to be resolved
 * @index:	index of the interrupt to resolve
 * @out_irq:	structure of_irq filled by this function
 *
 * This function resolves an interrupt, walking the tree, for a given
 * device-tree node. It's the high level pendant to of_irq_map_raw().
 * It also implements the workarounds for OldWolrd Macs.
 */
extern int of_irq_map_one(struct device_node *device, int index,
			struct of_irq *out_irq);

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

extern int of_irq_to_resource(struct device_node *dev, int index,
			struct resource *r);

/**
 * of_iomap - Maps the memory mapped IO for a given device_node
 * @device:	the device whose io range will be mapped
 * @index:	index of the io range
 *
 * Returns a pointer to the mapped memory
 */
extern void __iomem *of_iomap(struct device_node *device, int index);

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_MICROBLAZE_PROM_H */
