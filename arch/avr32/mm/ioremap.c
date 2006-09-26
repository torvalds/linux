/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/vmalloc.h>
#include <linux/module.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/addrspace.h>

static inline int remap_area_pte(pte_t *pte, unsigned long address,
				  unsigned long end, unsigned long phys_addr,
				  pgprot_t prot)
{
	unsigned long pfn;

	pfn = phys_addr >> PAGE_SHIFT;
	do {
		WARN_ON(!pte_none(*pte));

		set_pte(pte, pfn_pte(pfn, prot));
		address += PAGE_SIZE;
		pfn++;
		pte++;
	} while (address && (address < end));

	return 0;
}

static inline int remap_area_pmd(pmd_t *pmd, unsigned long address,
				 unsigned long end, unsigned long phys_addr,
				 pgprot_t prot)
{
	unsigned long next;

	phys_addr -= address;

	do {
		pte_t *pte = pte_alloc_kernel(pmd, address);
		if (!pte)
			return -ENOMEM;

		next = (address + PMD_SIZE) & PMD_MASK;
		if (remap_area_pte(pte, address, next,
				   address + phys_addr, prot))
			return -ENOMEM;

		address = next;
		pmd++;
	} while (address && (address < end));
	return 0;
}

static int remap_area_pud(pud_t *pud, unsigned long address,
			  unsigned long end, unsigned long phys_addr,
			  pgprot_t prot)
{
	unsigned long next;

	phys_addr -= address;

	do {
		pmd_t *pmd = pmd_alloc(&init_mm, pud, address);
		if (!pmd)
			return -ENOMEM;
		next = (address + PUD_SIZE) & PUD_MASK;
		if (remap_area_pmd(pmd, address, next,
				   phys_addr + address, prot))
			return -ENOMEM;

		address = next;
		pud++;
	} while (address && address < end);

	return 0;
}

static int remap_area_pages(unsigned long address, unsigned long phys_addr,
			    size_t size, pgprot_t prot)
{
	unsigned long end = address + size;
	unsigned long next;
	pgd_t *pgd;
	int err = 0;

	phys_addr -= address;

	pgd = pgd_offset_k(address);
	flush_cache_all();
	BUG_ON(address >= end);

	spin_lock(&init_mm.page_table_lock);
	do {
		pud_t *pud = pud_alloc(&init_mm, pgd, address);

		err = -ENOMEM;
		if (!pud)
			break;

		next = (address + PGDIR_SIZE) & PGDIR_MASK;
		if (next < address || next > end)
			next = end;
		err = remap_area_pud(pud, address, next,
				     phys_addr + address, prot);
		if (err)
			break;

		address = next;
		pgd++;
	} while (address && (address < end));

	spin_unlock(&init_mm.page_table_lock);
	flush_tlb_all();
	return err;
}

/*
 * Re-map an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access physical
 * memory directly.
 */
void __iomem *__ioremap(unsigned long phys_addr, size_t size,
			unsigned long flags)
{
	void *addr;
	struct vm_struct *area;
	unsigned long offset, last_addr;
	pgprot_t prot;

	/*
	 * Check if we can simply use the P4 segment. This area is
	 * uncacheable, so if caching/buffering is requested, we can't
	 * use it.
	 */
	if ((phys_addr >= P4SEG) && (flags == 0))
		return (void __iomem *)phys_addr;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * XXX: When mapping regular RAM, we'd better make damn sure
	 * it's never used for anything else.  But this is really the
	 * caller's responsibility...
	 */
	if (PHYSADDR(P2SEGADDR(phys_addr)) == phys_addr)
		return (void __iomem *)P2SEGADDR(phys_addr);

	/* Mappings have to be page-aligned */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr + 1) - phys_addr;

	prot = __pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY
			| _PAGE_ACCESSED | _PAGE_TYPE_SMALL | flags);

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	area->phys_addr = phys_addr;
	addr = area->addr;
	if (remap_area_pages((unsigned long)addr, phys_addr, size, prot)) {
		vunmap(addr);
		return NULL;
	}

	return (void __iomem *)(offset + (char *)addr);
}
EXPORT_SYMBOL(__ioremap);

void __iounmap(void __iomem *addr)
{
	struct vm_struct *p;

	if ((unsigned long)addr >= P4SEG)
		return;

	p = remove_vm_area((void *)(PAGE_MASK & (unsigned long __force)addr));
	if (unlikely(!p)) {
		printk (KERN_ERR "iounmap: bad address %p\n", addr);
		return;
	}

	kfree (p);
}
EXPORT_SYMBOL(__iounmap);
