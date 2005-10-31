/*
 *  arch/s390/mm/ioremap.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *
 *  Derived from "arch/i386/mm/extable.c"
 *    (C) Copyright 1995 1996 Linus Torvalds
 *
 * Re-map IO memory to kernel address space so that we can access it.
 * This is needed for high PCI addresses that aren't mapped in the
 * 640k-1MB IO memory area on PC's
 */

#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <asm/pgalloc.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

static inline void remap_area_pte(pte_t * pte, unsigned long address, unsigned long size,
        unsigned long phys_addr, unsigned long flags)
{
        unsigned long end;
        unsigned long pfn;

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
                set_pte(pte, pfn_pte(pfn, __pgprot(flags)));
                address += PAGE_SIZE;
                pfn++;
                pte++;
        } while (address && (address < end));
}

static inline int remap_area_pmd(pmd_t * pmd, unsigned long address, unsigned long size,
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

static int remap_area_pages(unsigned long address, unsigned long phys_addr,
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
	return 0;
}

/*
 * Generic mapping function (not visible outside):
 */

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 */
void * __ioremap(unsigned long phys_addr, unsigned long size, unsigned long flags)
{
	void * addr;
	struct vm_struct * area;

	if (phys_addr < virt_to_phys(high_memory))
		return phys_to_virt(phys_addr);
	if (phys_addr & ~PAGE_MASK)
		return NULL;
	size = PAGE_ALIGN(size);
	if (!size || size > phys_addr + size)
		return NULL;
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	addr = area->addr;
	if (remap_area_pages((unsigned long) addr, phys_addr, size, flags)) {
		vfree(addr);
		return NULL;
	}
	return addr;
}

void iounmap(void *addr)
{
	if (addr > high_memory)
		vfree(addr);
}
