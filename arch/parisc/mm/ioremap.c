/*
 * arch/parisc/mm/ioremap.c
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 * (C) Copyright 2001 Helge Deller <deller@gmx.de>
 * (C) Copyright 2005 Kyle McMartin <kyle@parisc-linux.org>
 */

#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <asm/io.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

static inline void 
remap_area_pte(pte_t *pte, unsigned long address, unsigned long size,
	       unsigned long phys_addr, unsigned long flags)
{
	unsigned long end, pfn;
	pgprot_t pgprot = __pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY |
				   _PAGE_ACCESSED | flags);

	address &= ~PMD_MASK;

	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;

	BUG_ON(address >= end);

	pfn = phys_addr >> PAGE_SHIFT;
	do {
		BUG_ON(!pte_none(*pte));

		set_pte(pte, pfn_pte(pfn, pgprot));

		address += PAGE_SIZE;
		pfn++;
		pte++;
	} while (address && (address < end));
}

static inline int 
remap_area_pmd(pmd_t *pmd, unsigned long address, unsigned long size,
	       unsigned long phys_addr, unsigned long flags)
{
	unsigned long end;

	address &= ~PGDIR_MASK;

	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;

	BUG_ON(address >= end);

	phys_addr -= address;
	do {
		pte_t *pte = pte_alloc_kernel(pmd, address);
		if (!pte)
			return -ENOMEM;

		remap_area_pte(pte, address, end - address, 
			       address + phys_addr, flags);

		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));

	return 0;
}

static int 
remap_area_pages(unsigned long address, unsigned long phys_addr,
		 unsigned long size, unsigned long flags)
{
	pgd_t *dir;
	int error = 0;
	unsigned long end = address + size;

	BUG_ON(address >= end);

	phys_addr -= address;
	dir = pgd_offset_k(address);

	flush_cache_all();

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

/*
 * Generic mapping function (not visible outside):
 */

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
void __iomem * __ioremap(unsigned long phys_addr, unsigned long size, unsigned long flags)
{
	void *addr;
	struct vm_struct *area;
	unsigned long offset, last_addr;

#ifdef CONFIG_EISA
	unsigned long end = phys_addr + size - 1;
	/* Support EISA addresses */
	if ((phys_addr >= 0x00080000 && end < 0x000fffff) ||
	    (phys_addr >= 0x00500000 && end < 0x03bfffff)) {
		phys_addr |= F_EXTEND(0xfc000000);
	}
#endif

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * Don't allow anybody to remap normal RAM that we're using..
	 */
	if (phys_addr < virt_to_phys(high_memory)) {
		char *t_addr, *t_end;
		struct page *page;

		t_addr = __va(phys_addr);
		t_end = t_addr + (size - 1);
	   
		for (page = virt_to_page(t_addr); 
		     page <= virt_to_page(t_end); page++) {
			if(!PageReserved(page))
				return NULL;
		}
	}

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr) - phys_addr;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;

	addr = area->addr;
	if (remap_area_pages((unsigned long) addr, phys_addr, size, flags)) {
		vfree(addr);
		return NULL;
	}

	return (void __iomem *) (offset + (char *)addr);
}
EXPORT_SYMBOL(__ioremap);

void iounmap(void __iomem *addr)
{
	if (addr > high_memory)
		return vfree((void *) (PAGE_MASK & (unsigned long __force) addr));
}
EXPORT_SYMBOL(iounmap);
