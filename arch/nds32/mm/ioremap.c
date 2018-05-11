// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <asm/pgtable.h>

void __iomem *ioremap(phys_addr_t phys_addr, size_t size);

static void __iomem *__ioremap_caller(phys_addr_t phys_addr, size_t size,
				      void *caller)
{
	struct vm_struct *area;
	unsigned long addr, offset, last_addr;
	pgprot_t prot;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr + 1) - phys_addr;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area_caller(size, VM_IOREMAP, caller);
	if (!area)
		return NULL;

	area->phys_addr = phys_addr;
	addr = (unsigned long)area->addr;
	prot = __pgprot(_PAGE_V | _PAGE_M_KRW | _PAGE_D |
			_PAGE_G | _PAGE_C_DEV);
	if (ioremap_page_range(addr, addr + size, phys_addr, prot)) {
		vunmap((void *)addr);
		return NULL;
	}
	return (__force void __iomem *)(offset + (char *)addr);

}

void __iomem *ioremap(phys_addr_t phys_addr, size_t size)
{
	return __ioremap_caller(phys_addr, size,
				__builtin_return_address(0));
}

EXPORT_SYMBOL(ioremap);

void iounmap(volatile void __iomem * addr)
{
	vunmap((void *)(PAGE_MASK & (unsigned long)addr));
}

EXPORT_SYMBOL(iounmap);
