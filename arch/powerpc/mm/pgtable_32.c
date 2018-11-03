/*
 * This file contains the routines setting up the linux page tables.
 *  -- paulus
 *
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/memblock.h>
#include <linux/slab.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/fixmap.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/sections.h>

#include "mmu_decl.h"

unsigned long ioremap_bot;
EXPORT_SYMBOL(ioremap_bot);	/* aka VMALLOC_END */

extern char etext[], _stext[], _sinittext[], _einittext[];

__ref pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte;

	if (slab_is_available()) {
		pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_ZERO);
	} else {
		pte = __va(memblock_phys_alloc(PAGE_SIZE, PAGE_SIZE));
		if (pte)
			clear_page(pte);
	}
	return pte;
}

pgtable_t pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *ptepage;

	gfp_t flags = GFP_KERNEL | __GFP_ZERO | __GFP_ACCOUNT;

	ptepage = alloc_pages(flags, 0);
	if (!ptepage)
		return NULL;
	if (!pgtable_page_ctor(ptepage)) {
		__free_page(ptepage);
		return NULL;
	}
	return ptepage;
}

void __iomem *
ioremap(phys_addr_t addr, unsigned long size)
{
	pgprot_t prot = pgprot_noncached(PAGE_KERNEL);

	return __ioremap_caller(addr, size, prot, __builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap);

void __iomem *
ioremap_wc(phys_addr_t addr, unsigned long size)
{
	pgprot_t prot = pgprot_noncached_wc(PAGE_KERNEL);

	return __ioremap_caller(addr, size, prot, __builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_wc);

void __iomem *
ioremap_wt(phys_addr_t addr, unsigned long size)
{
	pgprot_t prot = pgprot_cached_wthru(PAGE_KERNEL);

	return __ioremap_caller(addr, size, prot, __builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_wt);

void __iomem *
ioremap_coherent(phys_addr_t addr, unsigned long size)
{
	pgprot_t prot = pgprot_cached(PAGE_KERNEL);

	return __ioremap_caller(addr, size, prot, __builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_coherent);

void __iomem *
ioremap_prot(phys_addr_t addr, unsigned long size, unsigned long flags)
{
	pte_t pte = __pte(flags);

	/* writeable implies dirty for kernel addresses */
	if (pte_write(pte))
		pte = pte_mkdirty(pte);

	/* we don't want to let _PAGE_USER and _PAGE_EXEC leak out */
	pte = pte_exprotect(pte);
	pte = pte_mkprivileged(pte);

	return __ioremap_caller(addr, size, pte_pgprot(pte), __builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_prot);

void __iomem *
__ioremap(phys_addr_t addr, unsigned long size, unsigned long flags)
{
	return __ioremap_caller(addr, size, __pgprot(flags), __builtin_return_address(0));
}

void __iomem *
__ioremap_caller(phys_addr_t addr, unsigned long size, pgprot_t prot, void *caller)
{
	unsigned long v, i;
	phys_addr_t p;
	int err;

	/*
	 * Choose an address to map it to.
	 * Once the vmalloc system is running, we use it.
	 * Before then, we use space going down from IOREMAP_TOP
	 * (ioremap_bot records where we're up to).
	 */
	p = addr & PAGE_MASK;
	size = PAGE_ALIGN(addr + size) - p;

	/*
	 * If the address lies within the first 16 MB, assume it's in ISA
	 * memory space
	 */
	if (p < 16*1024*1024)
		p += _ISA_MEM_BASE;

#ifndef CONFIG_CRASH_DUMP
	/*
	 * Don't allow anybody to remap normal RAM that we're using.
	 * mem_init() sets high_memory so only do the check after that.
	 */
	if (slab_is_available() && (p < virt_to_phys(high_memory)) &&
	    page_is_ram(__phys_to_pfn(p))) {
		printk("__ioremap(): phys addr 0x%llx is RAM lr %ps\n",
		       (unsigned long long)p, __builtin_return_address(0));
		return NULL;
	}
#endif

	if (size == 0)
		return NULL;

	/*
	 * Is it already mapped?  Perhaps overlapped by a previous
	 * mapping.
	 */
	v = p_block_mapped(p);
	if (v)
		goto out;

	if (slab_is_available()) {
		struct vm_struct *area;
		area = get_vm_area_caller(size, VM_IOREMAP, caller);
		if (area == 0)
			return NULL;
		area->phys_addr = p;
		v = (unsigned long) area->addr;
	} else {
		v = (ioremap_bot -= size);
	}

	/*
	 * Should check if it is a candidate for a BAT mapping
	 */

	err = 0;
	for (i = 0; i < size && err == 0; i += PAGE_SIZE)
		err = map_kernel_page(v + i, p + i, prot);
	if (err) {
		if (slab_is_available())
			vunmap((void *)v);
		return NULL;
	}

out:
	return (void __iomem *) (v + ((unsigned long)addr & ~PAGE_MASK));
}
EXPORT_SYMBOL(__ioremap);

void iounmap(volatile void __iomem *addr)
{
	/*
	 * If mapped by BATs then there is nothing to do.
	 * Calling vfree() generates a benign warning.
	 */
	if (v_block_mapped((unsigned long)addr))
		return;

	if (addr > high_memory && (unsigned long) addr < ioremap_bot)
		vunmap((void *) (PAGE_MASK & (unsigned long)addr));
}
EXPORT_SYMBOL(iounmap);

int map_kernel_page(unsigned long va, phys_addr_t pa, pgprot_t prot)
{
	pmd_t *pd;
	pte_t *pg;
	int err = -ENOMEM;

	/* Use upper 10 bits of VA to index the first level map */
	pd = pmd_offset(pud_offset(pgd_offset_k(va), va), va);
	/* Use middle 10 bits of VA to index the second-level map */
	pg = pte_alloc_kernel(pd, va);
	if (pg != 0) {
		err = 0;
		/* The PTE should never be already set nor present in the
		 * hash table
		 */
		BUG_ON((pte_present(*pg) | pte_hashpte(*pg)) && pgprot_val(prot));
		set_pte_at(&init_mm, va, pg, pfn_pte(pa >> PAGE_SHIFT, prot));
	}
	smp_wmb();
	return err;
}

/*
 * Map in a chunk of physical memory starting at start.
 */
static void __init __mapin_ram_chunk(unsigned long offset, unsigned long top)
{
	unsigned long v, s;
	phys_addr_t p;
	int ktext;

	s = offset;
	v = PAGE_OFFSET + s;
	p = memstart_addr + s;
	for (; s < top; s += PAGE_SIZE) {
		ktext = ((char *)v >= _stext && (char *)v < etext) ||
			((char *)v >= _sinittext && (char *)v < _einittext);
		map_kernel_page(v, p, ktext ? PAGE_KERNEL_TEXT : PAGE_KERNEL);
#ifdef CONFIG_PPC_STD_MMU_32
		if (ktext)
			hash_preload(&init_mm, v, false, 0x300);
#endif
		v += PAGE_SIZE;
		p += PAGE_SIZE;
	}
}

void __init mapin_ram(void)
{
	unsigned long s, top;

#ifndef CONFIG_WII
	top = total_lowmem;
	s = mmu_mapin_ram(top);
	__mapin_ram_chunk(s, top);
#else
	if (!wii_hole_size) {
		s = mmu_mapin_ram(total_lowmem);
		__mapin_ram_chunk(s, total_lowmem);
	} else {
		top = wii_hole_start;
		s = mmu_mapin_ram(top);
		__mapin_ram_chunk(s, top);

		top = memblock_end_of_DRAM();
		s = wii_mmu_mapin_mem2(top);
		__mapin_ram_chunk(s, top);
	}
#endif
}

/* Scan the real Linux page tables and return a PTE pointer for
 * a virtual address in a context.
 * Returns true (1) if PTE was found, zero otherwise.  The pointer to
 * the PTE pointer is unmodified if PTE is not found.
 */
static int
get_pteptr(struct mm_struct *mm, unsigned long addr, pte_t **ptep, pmd_t **pmdp)
{
        pgd_t	*pgd;
	pud_t	*pud;
        pmd_t	*pmd;
        pte_t	*pte;
        int     retval = 0;

        pgd = pgd_offset(mm, addr & PAGE_MASK);
        if (pgd) {
		pud = pud_offset(pgd, addr & PAGE_MASK);
		if (pud && pud_present(*pud)) {
			pmd = pmd_offset(pud, addr & PAGE_MASK);
			if (pmd_present(*pmd)) {
				pte = pte_offset_map(pmd, addr & PAGE_MASK);
				if (pte) {
					retval = 1;
					*ptep = pte;
					if (pmdp)
						*pmdp = pmd;
					/* XXX caller needs to do pte_unmap, yuck */
				}
			}
		}
        }
        return(retval);
}

static int __change_page_attr_noflush(struct page *page, pgprot_t prot)
{
	pte_t *kpte;
	pmd_t *kpmd;
	unsigned long address;

	BUG_ON(PageHighMem(page));
	address = (unsigned long)page_address(page);

	if (v_block_mapped(address))
		return 0;
	if (!get_pteptr(&init_mm, address, &kpte, &kpmd))
		return -EINVAL;
	__set_pte_at(&init_mm, address, kpte, mk_pte(page, prot), 0);
	pte_unmap(kpte);

	return 0;
}

/*
 * Change the page attributes of an page in the linear mapping.
 *
 * THIS DOES NOTHING WITH BAT MAPPINGS, DEBUG USE ONLY
 */
static int change_page_attr(struct page *page, int numpages, pgprot_t prot)
{
	int i, err = 0;
	unsigned long flags;
	struct page *start = page;

	local_irq_save(flags);
	for (i = 0; i < numpages; i++, page++) {
		err = __change_page_attr_noflush(page, prot);
		if (err)
			break;
	}
	wmb();
	local_irq_restore(flags);
	flush_tlb_kernel_range((unsigned long)page_address(start),
			       (unsigned long)page_address(page));
	return err;
}

void mark_initmem_nx(void)
{
	struct page *page = virt_to_page(_sinittext);
	unsigned long numpages = PFN_UP((unsigned long)_einittext) -
				 PFN_DOWN((unsigned long)_sinittext);

	change_page_attr(page, numpages, PAGE_KERNEL);
}

#ifdef CONFIG_STRICT_KERNEL_RWX
void mark_rodata_ro(void)
{
	struct page *page;
	unsigned long numpages;

	page = virt_to_page(_stext);
	numpages = PFN_UP((unsigned long)_etext) -
		   PFN_DOWN((unsigned long)_stext);

	change_page_attr(page, numpages, PAGE_KERNEL_ROX);
	/*
	 * mark .rodata as read only. Use __init_begin rather than __end_rodata
	 * to cover NOTES and EXCEPTION_TABLE.
	 */
	page = virt_to_page(__start_rodata);
	numpages = PFN_UP((unsigned long)__init_begin) -
		   PFN_DOWN((unsigned long)__start_rodata);

	change_page_attr(page, numpages, PAGE_KERNEL_RO);
}
#endif

#ifdef CONFIG_DEBUG_PAGEALLOC
void __kernel_map_pages(struct page *page, int numpages, int enable)
{
	if (PageHighMem(page))
		return;

	change_page_attr(page, numpages, enable ? PAGE_KERNEL : __pgprot(0));
}
#endif /* CONFIG_DEBUG_PAGEALLOC */
