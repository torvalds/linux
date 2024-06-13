// SPDX-License-Identifier: GPL-2.0
/*
 * ppc64 "iomap" interface implementation.
 *
 * (C) Copyright 2004 Linus Torvalds
 */
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm/isa-bridge.h>

void __iomem *ioport_map(unsigned long port, unsigned int len)
{
	return (void __iomem *) (port + _IO_BASE);
}
EXPORT_SYMBOL(ioport_map);

#ifdef CONFIG_PCI
void pci_iounmap(struct pci_dev *dev, void __iomem *addr)
{
	if (isa_vaddr_is_ioport(addr))
		return;
	if (pcibios_vaddr_is_ioport(addr))
		return;
	iounmap(addr);
}

EXPORT_SYMBOL(pci_iounmap);
#endif /* CONFIG_PCI */
