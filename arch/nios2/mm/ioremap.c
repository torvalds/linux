/*
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009 Wind River Systems Inc
 *   Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/export.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

static inline void remap_area_pte(pte_t *pte, unsigned long address,
				unsigned long size, unsigned long phys_addr,
				unsigned long flags)
{
	unsigned long end;
	unsigned long pfn;
	pgprot_t pgprot = __pgprot(_PAGE_GLOBAL | _PAGE_PRESENT | _PAGE_READ
				| _PAGE_WRITE | flags);

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	if (address >= end)
		BUG();
	pfn = PFN_DOWN(phys_addr);
	do {
		if (!pte_none(*pte)) {
			pr_err("remap_area_pte: page already exists\n");
			BUG();
		}
		set_pte(pte, pfn_pte(pfn, pgprot));
		address += PAGE_SIZE;
		pfn++;
		pte++;
	} while (address && (address < end));
}

static inline int remap_area_pmd(pmd_t *pmd, unsigned long address,
				unsigned long size, unsigned long phys_addr,
				unsigned long flags)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	phys_addr -= address;
	if (address >= end)
		BUG();
	do {
		pte_t *pte = pte_alloc_kernel(pmd, address);

		if (!pte)
			return -ENOMEM;
		remap_area_pte(pte, address, end - address, address + phys_addr,
			flags);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return 0;
}

static int remap_area_pages(unsigned long address, unsigned long phys_addr,
				unsigned long size, unsigned long flags)
{
	int error;
	pgd_t *dir;
	unsigned long end = address + size;

	phys_addr -= address;
	dir = pgd_offset(&init_mm, address);
	flush_cache_all();
	if (address >= end)
		BUG();
	do {
		pud_t *pud;
		pmd_t *pmd;

		error = -ENOMEM;
		pud = pud_alloc(&init_mm, dir, address);
		if (!pud)
			break;
		pmd = pmd_alloc(&init_mm, pud, address);
		if (!pmd)
			break;
		if (remap_area_pmd(pmd, address, end - address,
			phys_addr + address, flags))
			break;
		error = 0;
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (address && (address < end));
	flush_tlb_all();
	return error;
}

#define IS_MAPPABLE_UNCACHEABLE(addr) (addr < 0x20000000UL)

/*
 * Map some physical address range into the kernel address space.
 */
void __iomem *__ioremap(unsigned long phys_addr, unsigned long size,
			unsigned long cacheflag)
{
	struct vm_struct *area;
	unsigned long offset;
	unsigned long last_addr;
	void *addr;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;

	if (!size || last_addr < phys_addr)
		return NULL;

	/* Don't allow anybody to remap normal RAM that we're using */
	if (phys_addr > PHYS_OFFSET && phys_addr < virt_to_phys(high_memory)) {
		char *t_addr, *t_end;
		struct page *page;

		t_addr = __va(phys_addr);
		t_end = t_addr + (size - 1);
		for (page = virt_to_page(t_addr);
			page <= virt_to_page(t_end); page++)
			if (!PageReserved(page))
				return NULL;
	}

	/*
	 * Map uncached objects in the low part of address space to
	 * CONFIG_NIOS2_IO_REGION_BASE
	 */
	if (IS_MAPPABLE_UNCACHEABLE(phys_addr) &&
	    IS_MAPPABLE_UNCACHEABLE(last_addr) &&
	    !(cacheflag & _PAGE_CACHED))
		return (void __iomem *)(CONFIG_NIOS2_IO_REGION_BASE + phys_addr);

	/* Mappings have to be page-aligned */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr + 1) - phys_addr;

	/* Ok, go for it */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	addr = area->addr;
	if (remap_area_pages((unsigned long) addr, phys_addr, size,
		cacheflag)) {
		vunmap(addr);
		return NULL;
	}
	return (void __iomem *) (offset + (char *)addr);
}
EXPORT_SYMBOL(__ioremap);

/*
 * __iounmap unmaps nearly everything, so be careful
 * it doesn't free currently pointer/page tables anymore but it
 * wasn't used anyway and might be added later.
 */
void __iounmap(void __iomem *addr)
{
	struct vm_struct *p;

	if ((unsigned long) addr > CONFIG_NIOS2_IO_REGION_BASE)
		return;

	p = remove_vm_area((void *) (PAGE_MASK & (unsigned long __force) addr));
	if (!p)
		pr_err("iounmap: bad address %p\n", addr);
	kfree(p);
}
EXPORT_SYMBOL(__iounmap);
