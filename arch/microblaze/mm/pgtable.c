/*
 *  This file contains the routines setting up the linux page tables.
 *
 * Copyright (C) 2008 Michal Simek
 * Copyright (C) 2008 PetaLogix
 *
 *    Copyright (C) 2007 Xilinx, Inc.  All rights reserved.
 *
 *  Derived from arch/ppc/mm/pgtable.c:
 *    -- paulus
 *
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file COPYING in the main directory of this
 *  archive for more details.
 *
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/mm_types.h>
#include <linux/pgtable.h>
#include <linux/memblock.h>
#include <linux/kallsyms.h>

#include <asm/pgalloc.h>
#include <linux/io.h>
#include <asm/mmu.h>
#include <asm/sections.h>
#include <asm/fixmap.h>

unsigned long ioremap_base;
unsigned long ioremap_bot;
EXPORT_SYMBOL(ioremap_bot);

static void __iomem *__ioremap(phys_addr_t addr, unsigned long size,
		unsigned long flags)
{
	unsigned long v, i;
	phys_addr_t p;
	int err;

	/*
	 * Choose an address to map it to.
	 * Once the vmalloc system is running, we use it.
	 * Before then, we use space going down from ioremap_base
	 * (ioremap_bot records where we're up to).
	 */
	p = addr & PAGE_MASK;
	size = PAGE_ALIGN(addr + size) - p;

	/*
	 * Don't allow anybody to remap normal RAM that we're using.
	 * mem_init() sets high_memory so only do the check after that.
	 *
	 * However, allow remap of rootfs: TBD
	 */

	if (mem_init_done &&
		p >= memory_start && p < virt_to_phys(high_memory) &&
		!(p >= __virt_to_phys((phys_addr_t)__bss_stop) &&
		p < __virt_to_phys((phys_addr_t)__bss_stop))) {
		pr_warn("__ioremap(): phys addr "PTE_FMT" is RAM lr %ps\n",
			(unsigned long)p, __builtin_return_address(0));
		return NULL;
	}

	if (size == 0)
		return NULL;

	/*
	 * Is it already mapped? If the whole area is mapped then we're
	 * done, otherwise remap it since we want to keep the virt addrs for
	 * each request contiguous.
	 *
	 * We make the assumption here that if the bottom and top
	 * of the range we want are mapped then it's mapped to the
	 * same virt address (and this is contiguous).
	 *  -- Cort
	 */

	if (mem_init_done) {
		struct vm_struct *area;
		area = get_vm_area(size, VM_IOREMAP);
		if (area == NULL)
			return NULL;
		v = (unsigned long) area->addr;
	} else {
		v = (ioremap_bot -= size);
	}

	if ((flags & _PAGE_PRESENT) == 0)
		flags |= _PAGE_KERNEL;
	if (flags & _PAGE_NO_CACHE)
		flags |= _PAGE_GUARDED;

	err = 0;
	for (i = 0; i < size && err == 0; i += PAGE_SIZE)
		err = map_page(v + i, p + i, flags);
	if (err) {
		if (mem_init_done)
			vfree((void *)v);
		return NULL;
	}

	return (void __iomem *) (v + ((unsigned long)addr & ~PAGE_MASK));
}

void __iomem *ioremap(phys_addr_t addr, unsigned long size)
{
	return __ioremap(addr, size, _PAGE_NO_CACHE);
}
EXPORT_SYMBOL(ioremap);

void iounmap(volatile void __iomem *addr)
{
	if ((__force void *)addr > high_memory &&
					(unsigned long) addr < ioremap_bot)
		vfree((void *) (PAGE_MASK & (unsigned long) addr));
}
EXPORT_SYMBOL(iounmap);


int map_page(unsigned long va, phys_addr_t pa, int flags)
{
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pd;
	pte_t *pg;
	int err = -ENOMEM;

	/* Use upper 10 bits of VA to index the first level map */
	p4d = p4d_offset(pgd_offset_k(va), va);
	pud = pud_offset(p4d, va);
	pd = pmd_offset(pud, va);
	/* Use middle 10 bits of VA to index the second-level map */
	pg = pte_alloc_kernel(pd, va); /* from powerpc - pgtable.c */
	/* pg = pte_alloc_kernel(&init_mm, pd, va); */

	if (pg != NULL) {
		err = 0;
		set_pte_at(&init_mm, va, pg, pfn_pte(pa >> PAGE_SHIFT,
				__pgprot(flags)));
		if (unlikely(mem_init_done))
			_tlbie(va);
	}
	return err;
}

/*
 * Map in all of physical memory starting at CONFIG_KERNEL_START.
 */
void __init mapin_ram(void)
{
	unsigned long v, p, s, f;

	v = CONFIG_KERNEL_START;
	p = memory_start;
	for (s = 0; s < lowmem_size; s += PAGE_SIZE) {
		f = _PAGE_PRESENT | _PAGE_ACCESSED |
				_PAGE_SHARED | _PAGE_HWEXEC;
		if (!is_kernel_text(v))
			f |= _PAGE_WRENABLE;
		else
			/* On the MicroBlaze, no user access
			   forces R/W kernel access */
			f |= _PAGE_USER;
		map_page(v, p, f);
		v += PAGE_SIZE;
		p += PAGE_SIZE;
	}
}

/* is x a power of 2? */
#define is_power_of_2(x)	((x) != 0 && (((x) & ((x) - 1)) == 0))

/* Scan the real Linux page tables and return a PTE pointer for
 * a virtual address in a context.
 * Returns true (1) if PTE was found, zero otherwise.  The pointer to
 * the PTE pointer is unmodified if PTE is not found.
 */
static int get_pteptr(struct mm_struct *mm, unsigned long addr, pte_t **ptep)
{
	pgd_t	*pgd;
	p4d_t	*p4d;
	pud_t	*pud;
	pmd_t	*pmd;
	pte_t	*pte;
	int     retval = 0;

	pgd = pgd_offset(mm, addr & PAGE_MASK);
	if (pgd) {
		p4d = p4d_offset(pgd, addr & PAGE_MASK);
		pud = pud_offset(p4d, addr & PAGE_MASK);
		pmd = pmd_offset(pud, addr & PAGE_MASK);
		if (pmd_present(*pmd)) {
			pte = pte_offset_kernel(pmd, addr & PAGE_MASK);
			if (pte) {
				retval = 1;
				*ptep = pte;
			}
		}
	}
	return retval;
}

/* Find physical address for this virtual address.  Normally used by
 * I/O functions, but anyone can call it.
 */
unsigned long iopa(unsigned long addr)
{
	unsigned long pa;

	pte_t *pte;
	struct mm_struct *mm;

	/* Allow mapping of user addresses (within the thread)
	 * for DMA if necessary.
	 */
	if (addr < TASK_SIZE)
		mm = current->mm;
	else
		mm = &init_mm;

	pa = 0;
	if (get_pteptr(mm, addr, &pte))
		pa = (pte_val(*pte) & PAGE_MASK) | (addr & ~PAGE_MASK);

	return pa;
}

__ref pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	if (mem_init_done)
		return (pte_t *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
	else
		return memblock_alloc_try_nid(PAGE_SIZE, PAGE_SIZE,
					      MEMBLOCK_LOW_LIMIT,
					      memory_start + kernel_tlb,
					      NUMA_NO_NODE);
}

void __set_fixmap(enum fixed_addresses idx, phys_addr_t phys, pgprot_t flags)
{
	unsigned long address = __fix_to_virt(idx);

	if (idx >= __end_of_fixed_addresses)
		BUG();

	map_page(address, phys, pgprot_val(flags));
}
