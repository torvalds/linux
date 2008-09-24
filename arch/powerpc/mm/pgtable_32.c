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

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/fixmap.h>
#include <asm/io.h>

#include "mmu_decl.h"

unsigned long ioremap_base;
unsigned long ioremap_bot;
EXPORT_SYMBOL(ioremap_bot);	/* aka VMALLOC_END */

#if defined(CONFIG_6xx) || defined(CONFIG_POWER3)
#define HAVE_BATS	1
#endif

#if defined(CONFIG_FSL_BOOKE)
#define HAVE_TLBCAM	1
#endif

extern char etext[], _stext[];

#ifdef CONFIG_SMP
extern void hash_page_sync(void);
#endif

#ifdef HAVE_BATS
extern phys_addr_t v_mapped_by_bats(unsigned long va);
extern unsigned long p_mapped_by_bats(phys_addr_t pa);
void setbat(int index, unsigned long virt, phys_addr_t phys,
	    unsigned int size, int flags);

#else /* !HAVE_BATS */
#define v_mapped_by_bats(x)	(0UL)
#define p_mapped_by_bats(x)	(0UL)
#endif /* HAVE_BATS */

#ifdef HAVE_TLBCAM
extern unsigned int tlbcam_index;
extern unsigned long v_mapped_by_tlbcam(unsigned long va);
extern unsigned long p_mapped_by_tlbcam(unsigned long pa);
#else /* !HAVE_TLBCAM */
#define v_mapped_by_tlbcam(x)	(0UL)
#define p_mapped_by_tlbcam(x)	(0UL)
#endif /* HAVE_TLBCAM */

#ifdef CONFIG_PTE_64BIT
/* Some processors use an 8kB pgdir because they have 8-byte Linux PTEs. */
#define PGDIR_ORDER	1
#else
#define PGDIR_ORDER	0
#endif

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *ret;

	ret = (pgd_t *)__get_free_pages(GFP_KERNEL|__GFP_ZERO, PGDIR_ORDER);
	return ret;
}

void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	free_pages((unsigned long)pgd, PGDIR_ORDER);
}

__init_refok pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte;
	extern int mem_init_done;
	extern void *early_get_page(void);

	if (mem_init_done) {
		pte = (pte_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO);
	} else {
		pte = (pte_t *)early_get_page();
		if (pte)
			clear_page(pte);
	}
	return pte;
}

pgtable_t pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *ptepage;

#ifdef CONFIG_HIGHPTE
	gfp_t flags = GFP_KERNEL | __GFP_HIGHMEM | __GFP_REPEAT | __GFP_ZERO;
#else
	gfp_t flags = GFP_KERNEL | __GFP_REPEAT | __GFP_ZERO;
#endif

	ptepage = alloc_pages(flags, 0);
	if (!ptepage)
		return NULL;
	pgtable_page_ctor(ptepage);
	return ptepage;
}

void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
#ifdef CONFIG_SMP
	hash_page_sync();
#endif
	free_page((unsigned long)pte);
}

void pte_free(struct mm_struct *mm, pgtable_t ptepage)
{
#ifdef CONFIG_SMP
	hash_page_sync();
#endif
	pgtable_page_dtor(ptepage);
	__free_page(ptepage);
}

void __iomem *
ioremap(phys_addr_t addr, unsigned long size)
{
	return __ioremap(addr, size, _PAGE_NO_CACHE | _PAGE_GUARDED);
}
EXPORT_SYMBOL(ioremap);

void __iomem *
ioremap_flags(phys_addr_t addr, unsigned long size, unsigned long flags)
{
	/* writeable implies dirty for kernel addresses */
	if (flags & _PAGE_RW)
		flags |= _PAGE_DIRTY | _PAGE_HWWRITE;

	/* we don't want to let _PAGE_USER and _PAGE_EXEC leak out */
	flags &= ~(_PAGE_USER | _PAGE_EXEC | _PAGE_HWEXEC);

	return __ioremap(addr, size, flags);
}
EXPORT_SYMBOL(ioremap_flags);

void __iomem *
__ioremap(phys_addr_t addr, unsigned long size, unsigned long flags)
{
	unsigned long v, i;
	phys_addr_t p;
	int err;

	/* Make sure we have the base flags */
	if ((flags & _PAGE_PRESENT) == 0)
		flags |= _PAGE_KERNEL;

	/* Non-cacheable page cannot be coherent */
	if (flags & _PAGE_NO_CACHE)
		flags &= ~_PAGE_COHERENT;

	/*
	 * Choose an address to map it to.
	 * Once the vmalloc system is running, we use it.
	 * Before then, we use space going down from ioremap_base
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

	/*
	 * Don't allow anybody to remap normal RAM that we're using.
	 * mem_init() sets high_memory so only do the check after that.
	 */
	if (mem_init_done && (p < virt_to_phys(high_memory))) {
		printk("__ioremap(): phys addr 0x%llx is RAM lr %p\n",
		       (unsigned long long)p, __builtin_return_address(0));
		return NULL;
	}

	if (size == 0)
		return NULL;

	/*
	 * Is it already mapped?  Perhaps overlapped by a previous
	 * BAT mapping.  If the whole area is mapped then we're done,
	 * otherwise remap it since we want to keep the virt addrs for
	 * each request contiguous.
	 *
	 * We make the assumption here that if the bottom and top
	 * of the range we want are mapped then it's mapped to the
	 * same virt address (and this is contiguous).
	 *  -- Cort
	 */
	if ((v = p_mapped_by_bats(p)) /*&& p_mapped_by_bats(p+size-1)*/ )
		goto out;

	if ((v = p_mapped_by_tlbcam(p)))
		goto out;

	if (mem_init_done) {
		struct vm_struct *area;
		area = get_vm_area(size, VM_IOREMAP);
		if (area == 0)
			return NULL;
		v = (unsigned long) area->addr;
	} else {
		v = (ioremap_bot -= size);
	}

	/*
	 * Should check if it is a candidate for a BAT mapping
	 */

	err = 0;
	for (i = 0; i < size && err == 0; i += PAGE_SIZE)
		err = map_page(v+i, p+i, flags);
	if (err) {
		if (mem_init_done)
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
	if (v_mapped_by_bats((unsigned long)addr)) return;

	if (addr > high_memory && (unsigned long) addr < ioremap_bot)
		vunmap((void *) (PAGE_MASK & (unsigned long)addr));
}
EXPORT_SYMBOL(iounmap);

int map_page(unsigned long va, phys_addr_t pa, int flags)
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
		BUG_ON(pte_val(*pg) & (_PAGE_PRESENT | _PAGE_HASHPTE));
		set_pte_at(&init_mm, va, pg, pfn_pte(pa >> PAGE_SHIFT,
						     __pgprot(flags)));
	}
	return err;
}

/*
 * Map in a big chunk of physical memory starting at KERNELBASE.
 */
void __init mapin_ram(void)
{
	unsigned long v, s, f;
	phys_addr_t p;
	int ktext;

	s = mmu_mapin_ram();
	v = KERNELBASE + s;
	p = memstart_addr + s;
	for (; s < total_lowmem; s += PAGE_SIZE) {
		ktext = ((char *) v >= _stext && (char *) v < etext);
		f = ktext ?_PAGE_RAM_TEXT : _PAGE_RAM;
		map_page(v, p, f);
#ifdef CONFIG_PPC_STD_MMU_32
		if (ktext)
			hash_preload(&init_mm, v, 0, 0x300);
#endif
		v += PAGE_SIZE;
		p += PAGE_SIZE;
	}
}

/* Scan the real Linux page tables and return a PTE pointer for
 * a virtual address in a context.
 * Returns true (1) if PTE was found, zero otherwise.  The pointer to
 * the PTE pointer is unmodified if PTE is not found.
 */
int
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

#ifdef CONFIG_DEBUG_PAGEALLOC

static int __change_page_attr(struct page *page, pgprot_t prot)
{
	pte_t *kpte;
	pmd_t *kpmd;
	unsigned long address;

	BUG_ON(PageHighMem(page));
	address = (unsigned long)page_address(page);

	if (v_mapped_by_bats(address) || v_mapped_by_tlbcam(address))
		return 0;
	if (!get_pteptr(&init_mm, address, &kpte, &kpmd))
		return -EINVAL;
	set_pte_at(&init_mm, address, kpte, mk_pte(page, prot));
	wmb();
	flush_HPTE(0, address, pmd_val(*kpmd));
	pte_unmap(kpte);

	return 0;
}

/*
 * Change the page attributes of an page in the linear mapping.
 *
 * THIS CONFLICTS WITH BAT MAPPINGS, DEBUG USE ONLY
 */
static int change_page_attr(struct page *page, int numpages, pgprot_t prot)
{
	int i, err = 0;
	unsigned long flags;

	local_irq_save(flags);
	for (i = 0; i < numpages; i++, page++) {
		err = __change_page_attr(page, prot);
		if (err)
			break;
	}
	local_irq_restore(flags);
	return err;
}


void kernel_map_pages(struct page *page, int numpages, int enable)
{
	if (PageHighMem(page))
		return;

	change_page_attr(page, numpages, enable ? PAGE_KERNEL : __pgprot(0));
}
#endif /* CONFIG_DEBUG_PAGEALLOC */

static int fixmaps;
unsigned long FIXADDR_TOP = 0xfffff000;
EXPORT_SYMBOL(FIXADDR_TOP);

void __set_fixmap (enum fixed_addresses idx, phys_addr_t phys, pgprot_t flags)
{
	unsigned long address = __fix_to_virt(idx);

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}

	map_page(address, phys, pgprot_val(flags));
	fixmaps++;
}

void __this_fixmap_does_not_exist(void)
{
	WARN_ON(1);
}
