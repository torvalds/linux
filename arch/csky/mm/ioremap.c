// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/export.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/io.h>

#include <asm/pgtable.h>

static void __iomem *__ioremap_caller(phys_addr_t addr, size_t size,
				      pgprot_t prot, void *caller)
{
	phys_addr_t last_addr;
	unsigned long offset, vaddr;
	struct vm_struct *area;

	last_addr = addr + size - 1;
	if (!size || last_addr < addr)
		return NULL;

	offset = addr & (~PAGE_MASK);
	addr &= PAGE_MASK;
	size = PAGE_ALIGN(size + offset);

	area = get_vm_area_caller(size, VM_IOREMAP, caller);
	if (!area)
		return NULL;

	vaddr = (unsigned long)area->addr;

	if (ioremap_page_range(vaddr, vaddr + size, addr, prot)) {
		free_vm_area(area);
		return NULL;
	}

	return (void __iomem *)(vaddr + offset);
}

void __iomem *__ioremap(phys_addr_t phys_addr, size_t size, pgprot_t prot)
{
	return __ioremap_caller(phys_addr, size, prot,
				__builtin_return_address(0));
}
EXPORT_SYMBOL(__ioremap);

void __iomem *ioremap_cache(phys_addr_t phys_addr, size_t size)
{
	return __ioremap_caller(phys_addr, size, PAGE_KERNEL,
				__builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_cache);

void iounmap(void __iomem *addr)
{
	vunmap((void *)((unsigned long)addr & PAGE_MASK));
}
EXPORT_SYMBOL(iounmap);

pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
			      unsigned long size, pgprot_t vma_prot)
{
	if (!pfn_valid(pfn)) {
		return pgprot_noncached(vma_prot);
	} else if (file->f_flags & O_SYNC) {
		return pgprot_writecombine(vma_prot);
	}

	return vma_prot;
}
EXPORT_SYMBOL(phys_mem_access_prot);
