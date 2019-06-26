/*
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Copyright (C) 2003, 04, 11 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2011 Wind River Systems,
 *   written by Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/of_address.h>

#include <asm/cpu-info.h>

unsigned long PCIBIOS_MIN_IO;
EXPORT_SYMBOL(PCIBIOS_MIN_IO);

unsigned long PCIBIOS_MIN_MEM;
EXPORT_SYMBOL(PCIBIOS_MIN_MEM);

static int __init pcibios_set_cache_line_size(void)
{
	unsigned int lsize;

	/*
	 * Set PCI cacheline size to that of the highest level in the
	 * cache hierarchy.
	 */
	lsize = cpu_dcache_line_size();
	lsize = cpu_scache_line_size() ? : lsize;
	lsize = cpu_tcache_line_size() ? : lsize;

	BUG_ON(!lsize);

	pci_dfl_cache_line_size = lsize >> 2;

	pr_debug("PCI: pci_cache_line_size set to %d bytes\n", lsize);
	return 0;
}
arch_initcall(pcibios_set_cache_line_size);

void pci_resource_to_user(const struct pci_dev *dev, int bar,
			  const struct resource *rsrc, resource_size_t *start,
			  resource_size_t *end)
{
	phys_addr_t size = resource_size(rsrc);

	*start = fixup_bigphys_addr(rsrc->start, size);
	*end = rsrc->start + size - 1;
}
