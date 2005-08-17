/*
 * arch/cris/mm/ioremap.c
 *
 * Re-map IO memory to kernel address space so that we can access it.
 * Needed for memory-mapped I/O devices mapped outside our normal DRAM
 * window (that is, all memory-mapped I/O devices).
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 * CRIS-port by Axis Communications AB
 */

#include <linux/vmalloc.h>
#include <asm/io.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/arch/memmap.h>

extern inline void remap_area_pte(pte_t * pte, unsigned long address, unsigned long size,
	unsigned long phys_addr, pgprot_t prot)
{
	unsigned long end;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	if (address >= end)
		BUG();
	do {
		if (!pte_none(*pte)) {
			printk("remap_area_pte: page already exists\n");
			BUG();
		}
		set_pte(pte, mk_pte_phys(phys_addr, prot));
		address += PAGE_SIZE;
		phys_addr += PAGE_SIZE;
		pte++;
	} while (address && (address < end));
}

static inline int remap_area_pmd(pmd_t * pmd, unsigned long address, unsigned long size,
	unsigned long phys_addr, pgprot_t prot)
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
		pte_t * pte = pte_alloc_kernel(&init_mm, pmd, address);
		if (!pte)
			return -ENOMEM;
		remap_area_pte(pte, address, end - address, address + phys_addr, prot);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return 0;
}

static int remap_area_pages(unsigned long address, unsigned long phys_addr,
				 unsigned long size, pgprot_t prot)
{
	int error;
	pgd_t * dir;
	unsigned long end = address + size;

	phys_addr -= address;
	dir = pgd_offset(&init_mm, address);
	flush_cache_all();
	if (address >= end)
		BUG();
	spin_lock(&init_mm.page_table_lock);
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
				   phys_addr + address, prot))
			break;
		error = 0;
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (address && (address < end));
	spin_unlock(&init_mm.page_table_lock);
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
void __iomem * __ioremap_prot(unsigned long phys_addr, unsigned long size, pgprot_t prot)
{
	void __iomem * addr;
	struct vm_struct * area;
	unsigned long offset, last_addr;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr+1) - phys_addr;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	addr = (void __iomem *)area->addr;
	if (remap_area_pages((unsigned long) addr, phys_addr, size, prot)) {
		vfree((void __force *)addr);
		return NULL;
	}
	return (void __iomem *) (offset + (char __iomem *)addr);
}

void __iomem * __ioremap(unsigned long phys_addr, unsigned long size, unsigned long flags)
{
	return __ioremap_prot(phys_addr, size,
		              __pgprot(_PAGE_PRESENT | __READABLE |
				       __WRITEABLE | _PAGE_GLOBAL |
				       _PAGE_KERNEL | flags));
}

/**
 * ioremap_nocache     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * Must be freed with iounmap.
 */

void __iomem *ioremap_nocache (unsigned long phys_addr, unsigned long size)
{
        return __ioremap(phys_addr | MEM_NON_CACHEABLE, size, 0);
}

void iounmap(volatile void __iomem *addr)
{
	if (addr > high_memory)
		return vfree((void *) (PAGE_MASK & (unsigned long) addr));
}
