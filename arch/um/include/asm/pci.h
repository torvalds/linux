/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_UM_PCI_H
#define __ASM_UM_PCI_H
#include <linux/types.h>
#include <asm/io.h>

/* Generic PCI */
#include <asm-generic/pci.h>

#ifdef CONFIG_PCI_MSI_IRQ_DOMAIN
/*
 * This is a bit of an annoying hack, and it assumes we only have
 * the virt-pci (if anything). Which is true, but still.
 */
void *pci_root_bus_fwnode(struct pci_bus *bus);
#define pci_root_bus_fwnode	pci_root_bus_fwnode
#endif

#endif  /* __ASM_UM_PCI_H */
