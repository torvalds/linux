#include <linux/of.h>	/* linux/of.h gets to determine #include ordering */
#ifndef _POWERPC_PROM_H
#define _POWERPC_PROM_H
#ifdef __KERNEL__

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
#include <linux/types.h>
#include <asm/irq.h>
#include <linux/atomic.h>

#define HAVE_ARCH_DEVTREE_FIXUPS

/*
 * OF address retreival & translation
 */

/* Translate a DMA address from device space to CPU space */
extern u64 of_translate_dma_address(struct device_node *dev,
				    const __be32 *in_addr);

#ifdef CONFIG_PCI
extern unsigned long pci_address_to_pio(phys_addr_t address);
#define pci_address_to_pio pci_address_to_pio
#endif	/* CONFIG_PCI */

/* Parse the ibm,dma-window property of an OF node into the busno, phys and
 * size parameters.
 */
void of_parse_dma_window(struct device_node *dn, const void *dma_window_prop,
		unsigned long *busno, unsigned long *phys, unsigned long *size);

extern void kdump_move_device_tree(void);

/* CPU OF node matching */
struct device_node *of_get_cpu_node(int cpu, unsigned int *thread);

/* cache lookup */
struct device_node *of_find_next_cache_node(struct device_node *np);

#ifdef CONFIG_NUMA
extern int of_node_to_nid(struct device_node *device);
#else
static inline int of_node_to_nid(struct device_node *device) { return 0; }
#endif
#define of_node_to_nid of_node_to_nid

extern void of_instantiate_rtc(void);

/* These includes are put at the bottom because they may contain things
 * that are overridden by this file.  Ideally they shouldn't be included
 * by this file, but there are a bunch of .c files that currently depend
 * on it.  Eventually they will be cleaned up. */
#include <linux/of_fdt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#endif /* __KERNEL__ */
#endif /* _POWERPC_PROM_H */
