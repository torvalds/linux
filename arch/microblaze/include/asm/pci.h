/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * Based on powerpc version
 */

#ifndef __ASM_MICROBLAZE_PCI_H
#define __ASM_MICROBLAZE_PCI_H
#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/scatterlist.h>

#include <asm/io.h>
#include <asm/pci-bridge.h>

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		0x10000000

/*
 * Set this to 1 if you want the kernel to re-assign all PCI
 * bus numbers (don't do that on ppc64 yet !)
 */
#define pcibios_assign_all_busses()	0

extern int pci_domain_nr(struct pci_bus *bus);

/* Decide whether to display the domain number in /proc */
extern int pci_proc_domain(struct pci_bus *bus);

struct vm_area_struct;

/* Tell PCI code what kind of PCI resource mappings we support */
#define HAVE_PCI_MMAP			1
#define ARCH_GENERIC_PCI_MMAP_RESOURCE	1
#define arch_can_pci_mmap_io()		1

struct file;

/* This part of code was originally in xilinx-pci.h */
#ifdef CONFIG_PCI_XILINX
extern void __init xilinx_pci_init(void);
#else
static inline void __init xilinx_pci_init(void) { return; }
#endif

#endif	/* __KERNEL__ */
#endif /* __ASM_MICROBLAZE_PCI_H */
