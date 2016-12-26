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
#include <linux/bootmem.h>
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
	struct cpuinfo_mips *c = &current_cpu_data;
	unsigned int lsize;

	/*
	 * Set PCI cacheline size to that of the highest level in the
	 * cache hierarchy.
	 */
	lsize = c->dcache.linesz;
	lsize = c->scache.linesz ? : lsize;
	lsize = c->tcache.linesz ? : lsize;

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
	*end = rsrc->start + size;
}

int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state, int write_combine)
{
	unsigned long prot;

	/*
	 * I/O space can be accessed via normal processor loads and stores on
	 * this platform but for now we elect not to do this and portable
	 * drivers should not do this anyway.
	 */
	if (mmap_state == pci_mmap_io)
		return -EINVAL;

	/*
	 * Ignore write-combine; for now only return uncached mappings.
	 */
	prot = pgprot_val(vma->vm_page_prot);
	prot = (prot & ~_CACHE_MASK) | _CACHE_UNCACHED;
	vma->vm_page_prot = __pgprot(prot);

	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
		vma->vm_end - vma->vm_start, vma->vm_page_prot);
}
