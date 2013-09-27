/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/of.h>	/* linux/of.h gets to determine #include ordering */

#ifndef _ASM_OPENRISC_PROM_H
#define _ASM_OPENRISC_PROM_H
#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <asm/irq.h>
#include <linux/irqdomain.h>
#include <linux/atomic.h>
#include <linux/of_irq.h>
#include <linux/of_fdt.h>
#include <linux/of_address.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#define HAVE_ARCH_DEVTREE_FIXUPS

/* Other Prototypes */
extern int early_uartlite_console(void);

/* Parse the ibm,dma-window property of an OF node into the busno, phys and
 * size parameters.
 */
void of_parse_dma_window(struct device_node *dn, const void *dma_window_prop,
		unsigned long *busno, unsigned long *phys, unsigned long *size);

extern void kdump_move_device_tree(void);

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
#endif /* _ASM_OPENRISC_PROM_H */
