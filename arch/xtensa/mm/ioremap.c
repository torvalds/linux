/*
 * ioremap implementation.
 *
 * Copyright (C) 2015 Cadence Design Systems Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/vmalloc.h>
#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/pgtable.h>

static void __iomem *xtensa_ioremap(unsigned long paddr, unsigned long size,
				    pgprot_t prot)
{
	unsigned long offset = paddr & ~PAGE_MASK;
	unsigned long pfn = __phys_to_pfn(paddr);
	struct vm_struct *area;
	unsigned long vaddr;
	int err;

	paddr &= PAGE_MASK;

	WARN_ON(pfn_valid(pfn));

	size = PAGE_ALIGN(offset + size);

	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;

	vaddr = (unsigned long)area->addr;
	area->phys_addr = paddr;

	err = ioremap_page_range(vaddr, vaddr + size, paddr, prot);

	if (err) {
		vunmap((void *)vaddr);
		return NULL;
	}

	flush_cache_vmap(vaddr, vaddr + size);
	return (void __iomem *)(offset + vaddr);
}

void __iomem *xtensa_ioremap_nocache(unsigned long addr, unsigned long size)
{
	return xtensa_ioremap(addr, size, pgprot_noncached(PAGE_KERNEL));
}
EXPORT_SYMBOL(xtensa_ioremap_nocache);

void __iomem *xtensa_ioremap_cache(unsigned long addr, unsigned long size)
{
	return xtensa_ioremap(addr, size, PAGE_KERNEL);
}
EXPORT_SYMBOL(xtensa_ioremap_cache);

void xtensa_iounmap(volatile void __iomem *io_addr)
{
	void *addr = (void *)(PAGE_MASK & (unsigned long)io_addr);

	vunmap(addr);
}
EXPORT_SYMBOL(xtensa_iounmap);
