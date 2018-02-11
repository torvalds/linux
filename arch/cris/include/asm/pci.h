/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_CRIS_PCI_H
#define __ASM_CRIS_PCI_H


#ifdef __KERNEL__
#include <linux/mm.h>		/* for struct page */

/* Can be used to override the logic in pci_scan_bus for skipping
   already-configured bus numbers - to be used for buggy BIOSes
   or architectures with incomplete PCI setup by the loader */

#define pcibios_assign_all_busses(void) 1

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		0x10000000

#define PCIBIOS_MIN_CARDBUS_IO	0x4000

/* Dynamic DMA mapping stuff.
 * i386 has everything mapped statically.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <asm/io.h>

/* The PCI address space does equal the physical memory
 * address space.  The networking and block device layers use
 * this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS	(1)

#define HAVE_PCI_MMAP
#define ARCH_GENERIC_PCI_MMAP_RESOURCE

#endif /* __KERNEL__ */

/* generic pci stuff */
#include <asm-generic/pci.h>

#endif /* __ASM_CRIS_PCI_H */
