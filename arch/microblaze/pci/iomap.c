// SPDX-License-Identifier: GPL-2.0
/*
 * ppc64 "iomap" interface implementation.
 *
 * (C) Copyright 2004 Linus Torvalds
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/io.h>
#include <asm/pci-bridge.h>

static DEFINE_SPINLOCK(hose_spinlock);
LIST_HEAD(hose_list);

unsigned long isa_io_base;
EXPORT_SYMBOL(isa_io_base);

static resource_size_t pcibios_io_size(const struct pci_controller *hose)
{
	return resource_size(&hose->io_resource);
}

int pcibios_vaddr_is_ioport(void __iomem *address)
{
	int ret = 0;
	struct pci_controller *hose;
	resource_size_t size;

	spin_lock(&hose_spinlock);
	list_for_each_entry(hose, &hose_list, list_node) {
		size = pcibios_io_size(hose);
		if (address >= hose->io_base_virt &&
		    address < (hose->io_base_virt + size)) {
			ret = 1;
			break;
		}
	}
	spin_unlock(&hose_spinlock);
	return ret;
}

/* Display the domain number in /proc */
int pci_proc_domain(struct pci_bus *bus)
{
	return pci_domain_nr(bus);
}

void pci_iounmap(struct pci_dev *dev, void __iomem *addr)
{
	if (isa_vaddr_is_ioport(addr))
		return;
	if (pcibios_vaddr_is_ioport(addr))
		return;
	iounmap(addr);
}
EXPORT_SYMBOL(pci_iounmap);
