/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/cache.h>

void __iomem *ioremap(unsigned long paddr, unsigned long size)
{
	unsigned long vaddr;
	struct vm_struct *area;
	unsigned long off, end;
	const pgprot_t prot = PAGE_KERNEL_NO_CACHE;

	/* Don't allow wraparound or zero size */
	end = paddr + size - 1;
	if (!size || (end < paddr))
		return NULL;

	/* If the region is h/w uncached, nothing special needed */
	if (paddr >= ARC_UNCACHED_ADDR_SPACE)
		return (void __iomem *)paddr;

	/* An early platform driver might end up here */
	if (!slab_is_available())
		return NULL;

	/* Mappings have to be page-aligned, page-sized */
	off = paddr & ~PAGE_MASK;
	paddr &= PAGE_MASK;
	size = PAGE_ALIGN(end + 1) - paddr;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;

	area->phys_addr = paddr;
	vaddr = (unsigned long)area->addr;
	if (ioremap_page_range(vaddr, vaddr + size, paddr, prot)) {
		vfree(area->addr);
		return NULL;
	}

	return (void __iomem *)(off + (char __iomem *)vaddr);
}
EXPORT_SYMBOL(ioremap);

void iounmap(const void __iomem *addr)
{
	if (addr >= (void __force __iomem *)ARC_UNCACHED_ADDR_SPACE)
		return;

	vfree((void *)(PAGE_MASK & (unsigned long __force)addr));
}
EXPORT_SYMBOL(iounmap);
