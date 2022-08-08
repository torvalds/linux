/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_UM_PCI_H
#define __ASM_UM_PCI_H
#include <linux/types.h>
#include <asm/io.h>

#define PCIBIOS_MIN_IO		0
#define PCIBIOS_MIN_MEM		0

#define pcibios_assign_all_busses() 1

extern int isa_dma_bridge_buggy;

#ifdef CONFIG_PCI
static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	/* no legacy IRQs */
	return -ENODEV;
}
#endif

#ifdef CONFIG_PCI_DOMAINS
static inline int pci_proc_domain(struct pci_bus *bus)
{
	/* always show the domain in /proc */
	return 1;
}
#endif  /* CONFIG_PCI */

#ifdef CONFIG_PCI_MSI_IRQ_DOMAIN
/*
 * This is a bit of an annoying hack, and it assumes we only have
 * the virt-pci (if anything). Which is true, but still.
 */
void *pci_root_bus_fwnode(struct pci_bus *bus);
#define pci_root_bus_fwnode	pci_root_bus_fwnode
#endif

#endif  /* __ASM_UM_PCI_H */
