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

void pcibios_config_init(void);
struct pci_bus * pcibios_scan_root(int bus);

void pcibios_set_master(struct pci_dev *dev);
struct irq_routing_table *pcibios_get_irq_routing_table(void);
int pcibios_set_irq_routing(struct pci_dev *dev, int pin, int irq);

/* Dynamic DMA mapping stuff.
 * i386 has everything mapped statically.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <asm/io.h>

struct pci_dev;

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
