/*
 *  linux/arch/m32r/mm/ioremap.c
 *
 *  Copyright (c) 2001, 2002  Hiroyuki Kondo
 *
 *  Taken from mips version.
 *    (C) Copyright 1995 1996 Linus Torvalds
 *    (C) Copyright 2001 Ralf Baechle
 */

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <linux/module.h>
#include <asm/addrspace.h>
#include <asm/byteorder.h>

#include <linux/vmalloc.h>
#include <asm/io.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

static inline void
remap_area_pte(pte_t * pte, unsigned long address, unsigned long size,
	       unsigned long phys_addr, unsigned long flags)
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
	pfn = phys_addr >> PAGE_SHIFT;
	do {
		if (!pte_none(*pte)) {
			printk("remap_area_pte: page already exists\n");
			BUG();
		}
		set_pte(pte, pfn_pte(pfn, pgprot));
		address += PAGE_SIZE;
		pfn++;
		pte++;
	} while (address && (address < end));
}

static inline int
remap_area_pmd(pmd_t * pmd, unsigned long address, unsigned long size,
	       unsigned long phys_addr, unsigned long flags)
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
		pte_t * pte = pte_alloc_kernel(pmd, address);
		if (!pte)
			return -ENOMEM;
		remap_area_pte(pte, address, end - address, address + phys_addr, flags);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return 0;
}

static int
remap_area_pages(unsigned long address, unsigned long phys_addr,
		 unsigned long size, unsigned long flags)
{
	int error;
	pgd_t * dir;
	unsigned long end = address + size;

	phys_addr -= address;
	dir = pgd_offset(&init_mm, address);
	flush_cache_all();
	if (address >= end)
		BUG();
	do {
		pmd_t *pmd;
		pmd = pmd_alloc(&init_mm, dir, address);
		error = -ENOMEM;
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

/*
 * Generic mapping function (not visible outside):
 */

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */

#define IS_LOW512(addr) (!((unsigned long)(addr) & ~0x1fffffffUL))

void __iomem *
__ioremap(unsigned long phys_addr, unsigned long size, unsigned long flags)
{
	void __iomem * addr;
	struct vm_struct * area;
	unsigned long offset, last_addr;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * Map objects in the low 512mb of address space using KSEG1, otherwise
	 * map using page tables.
	 */
	if (IS_LOW512(phys_addr) && IS_LOW512(phys_addr + size - 1))
		return (void *) KSEG1ADDR(phys_addr);

	/*
	 * Don't allow anybody to remap normal RAM that we're using..
	 */
	if (phys_addr < virt_to_phys(high_memory)) {
		char *t_addr, *t_end;
		struct page *page;

		t_addr = __va(phys_addr);
		t_end = t_addr + (size - 1);

		for(page = virt_to_page(t_addr); page <= virt_to_page(t_end); page++)
			if(!PageReserved(page))
				return NULL;
	}

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr + 1) - phys_addr;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	area->phys_addr = phys_addr;
	addr = (void __iomem *) area->addr;
	if (remap_area_pages((unsigned long)addr, phys_addr, size, flags)) {
		vunmap((void __force *) addr);
		return NULL;
	}

	return (void __iomem *) (offset + (char __iomem *)addr);
}

#define IS_KSEG1(addr) (((unsigned long)(addr) & ~0x1fffffffUL) == KSEG1)

void iounmap(volatile void __iomem *addr)
{
	if (!IS_KSEG1(addr))
		vfree((void *) (PAGE_MASK & (unsigned long) addr));
}

