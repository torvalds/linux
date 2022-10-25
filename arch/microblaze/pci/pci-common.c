// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Contains common pci routines for ALL ppc platform
 * (based on pci_32.c and pci_64.c)
 *
 * Port for PPC64 David Engebretsen, IBM Corp.
 * Contains common pci routines for ppc64 platform, pSeries and iSeries brands.
 *
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 *   Rework, based on alpha PCI code.
 *
 * Common pmac/prep/chrp pci routines. -- Cort
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/shmem_fs.h>
#include <linux/list.h>
#include <linux/syscalls.h>
#include <linux/irq.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/export.h>

#include <asm/processor.h>
#include <linux/io.h>
#include <asm/pci-bridge.h>
#include <asm/byteorder.h>

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
