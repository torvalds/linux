/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPARC_PCI_H
#define __SPARC_PCI_H

#ifdef __KERNEL__

#include <linux/dma-mapping.h>

/* Can be used to override the logic in pci_scan_bus for skipping
 * already-configured bus numbers - to be used for buggy BIOSes
 * or architectures with incomplete PCI setup by the loader.
 */
#define pcibios_assign_all_busses()	0

#define PCIBIOS_MIN_IO		0UL
#define PCIBIOS_MIN_MEM		0UL

#define PCI_IRQ_NONE		0xffffffff

#endif /* __KERNEL__ */

#ifndef CONFIG_LEON_PCI
/* generic pci stuff */
#include <asm-generic/pci.h>
#else
static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	return PCI_IRQ_NONE;
}
#endif

#endif /* __SPARC_PCI_H */
