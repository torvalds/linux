/*
 *  This file contains ioremap and related functions for 64-bit machines.
 *
 *  Derived from arch/ppc64/mm/init.c
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@samba.org)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Dave Engebretsen <engebret@us.ibm.com>
 *      Rework for PPC64 port.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/hugetlb.h>

#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/tlb.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/firmware.h>
#include <asm/dma.h>
#include <asm/powernv.h>

#include "mmu_decl.h"

#ifdef CONFIG_PPC_STD_MMU_64
#if TASK_SIZE_USER64 > (1UL << (ESID_BITS + SID_SHIFT))
#error TASK_SIZE_USER64 exceeds user VSID range
#endif
#endif

#ifdef CONFIG_PPC_BOOK3S_64
/*
 * partition table and process table for ISA 3.0
 */
struct prtb_entry *process_tb;
struct patb_entry *partition_tb;
/*
 * page table size
 */
unsigned long __pte_index_size;
EXPORT_SYMBOL(__pte_index_size);
unsigned long __pmd_index_size;
EXPORT_SYMBOL(__pmd_index_size);
unsigned long __pud_index_size;
EXPORT_SYMBOL(__pud_index_size);
unsigned long __pgd_index_size;
EXPORT_SYMBOL(__pgd_index_size);
unsigned long __pmd_cache_index;
EXPORT_SYMBOL(__pmd_cache_index);
unsigned long __pte_table_size;
EXPORT_SYMBOL(__pte_table_size);
unsigned long __pmd_table_size;
EXPORT_SYMBOL(__pmd_table_size);
unsigned long __pud_table_size;
EXPORT_SYMBOL(__pud_table_size);
unsigned long __pgd_table_size;
EXPORT_SYMBOL(__pgd_table_size);
unsigned long __pmd_val_bits;
EXPORT_SYMBOL(__pmd_val_bits);
unsigned long __pud_val_bits;
EXPORT_SYMBOL(__pud_val_bits);
unsigned long __pgd_val_bits;
EXPORT_SYMBOL(__pgd_val_bits);
unsigned long __kernel_virt_start;
EXPORT_SYMBOL(__kernel_virt_start);
unsigned long __kernel_virt_size;
EXPORT_SYMBOL(__kernel_virt_size);
unsigned long __vmalloc_start;
EXPORT_SYMBOL(__vmalloc_start);
unsigned long __vmalloc_end;
EXPORT_SYMBOL(__vmalloc_end);
struct page *vmemmap;
EXPORT_SYMBOL(vmemmap);
unsigned long __pte_frag_nr;
EXPORT_SYMBOL(__pte_frag_nr);
unsigned long __pte_frag_size_shift;
EXPORT_SYMBOL(__pte_frag_size_shift);
unsigned long ioremap_bot;
#else /* !CONFIG_PPC_BOOK3S_64 */
unsigned long ioremap_bot = IOREMAP_BASE;
#endif

/**
 * __ioremap_at - Low level function to establish the page tables
 *                for an IO mapping
 */
void __iomem * __ioremap_at(phys_addr_t pa, void *ea, unsigned long size,
			    unsigned long flags)
{
	unsigned long i;

	/* Make sure we have the base flags */
	if ((flags & _PAGE_PRESENT) == 0)
		flags |= pgprot_val(PAGE_KERNEL);

	/* We don't support the 4K PFN hack with ioremap */
	if (flags & H_PAGE_4K_PFN)
		return NULL;

	WARN_ON(pa & ~PAGE_MASK);
	WARN_ON(((unsigned long)ea) & ~PAGE_MASK);
	WARN_ON(size & ~PAGE_MASK);

	for (i = 0; i < size; i += PAGE_SIZE)
		if (map_kernel_page((unsigned long)ea+i, pa+i, flags))
			return NULL;

	return (void __iomem *)ea;
}

/**
 * __iounmap_from - Low level function to tear down the page tables
 *                  for an IO mapping. This is used for mappings that
 *                  are manipulated manually, like partial unmapping of
 *                  PCI IOs or ISA space.
 */
void __iounmap_at(void *ea, unsigned long size)
{
	WARN_ON(((unsigned long)ea) & ~PAGE_MASK);
	WARN_ON(size & ~PAGE_MASK);

	unmap_kernel_range((unsigned long)ea, size);
}

void __iomem * __ioremap_caller(phys_addr_t addr, unsigned long size,
				unsigned long flags, void *caller)
{
	phys_addr_t paligned;
	void __iomem *ret;

	/*
	 * Choose an address to map it to.
	 * Once the imalloc system is running, we use it.
	 * Before that, we map using addresses going
	 * up from ioremap_bot.  imalloc will use
	 * the addresses from ioremap_bot through
	 * IMALLOC_END
	 * 
	 */
	paligned = addr & PAGE_MASK;
	size = PAGE_ALIGN(addr + size) - paligned;

	if ((size == 0) || (paligned == 0))
		return NULL;

	if (slab_is_available()) {
		struct vm_struct *area;

		area = __get_vm_area_caller(size, VM_IOREMAP,
					    ioremap_bot, IOREMAP_END,
					    caller);
		if (area == NULL)
			return NULL;

		area->phys_addr = paligned;
		ret = __ioremap_at(paligned, area->addr, size, flags);
		if (!ret)
			vunmap(area->addr);
	} else {
		ret = __ioremap_at(paligned, (void *)ioremap_bot, size, flags);
		if (ret)
			ioremap_bot += size;
	}

	if (ret)
		ret += addr & ~PAGE_MASK;
	return ret;
}

void __iomem * __ioremap(phys_addr_t addr, unsigned long size,
			 unsigned long flags)
{
	return __ioremap_caller(addr, size, flags, __builtin_return_address(0));
}

void __iomem * ioremap(phys_addr_t addr, unsigned long size)
{
	unsigned long flags = pgprot_val(pgprot_noncached(__pgprot(0)));
	void *caller = __builtin_return_address(0);

	if (ppc_md.ioremap)
		return ppc_md.ioremap(addr, size, flags, caller);
	return __ioremap_caller(addr, size, flags, caller);
}

void __iomem * ioremap_wc(phys_addr_t addr, unsigned long size)
{
	unsigned long flags = pgprot_val(pgprot_noncached_wc(__pgprot(0)));
	void *caller = __builtin_return_address(0);

	if (ppc_md.ioremap)
		return ppc_md.ioremap(addr, size, flags, caller);
	return __ioremap_caller(addr, size, flags, caller);
}

void __iomem * ioremap_prot(phys_addr_t addr, unsigned long size,
			     unsigned long flags)
{
	void *caller = __builtin_return_address(0);

	/* writeable implies dirty for kernel addresses */
	if (flags & _PAGE_WRITE)
		flags |= _PAGE_DIRTY;

	/* we don't want to let _PAGE_EXEC leak out */
	flags &= ~_PAGE_EXEC;
	/*
	 * Force kernel mapping.
	 */
#if defined(CONFIG_PPC_BOOK3S_64)
	flags |= _PAGE_PRIVILEGED;
#else
	flags &= ~_PAGE_USER;
#endif


#ifdef _PAGE_BAP_SR
	/* _PAGE_USER contains _PAGE_BAP_SR on BookE using the new PTE format
	 * which means that we just cleared supervisor access... oops ;-) This
	 * restores it
	 */
	flags |= _PAGE_BAP_SR;
#endif

	if (ppc_md.ioremap)
		return ppc_md.ioremap(addr, size, flags, caller);
	return __ioremap_caller(addr, size, flags, caller);
}


/*  
 * Unmap an IO region and remove it from imalloc'd list.
 * Access to IO memory should be serialized by driver.
 */
void __iounmap(volatile void __iomem *token)
{
	void *addr;

	if (!slab_is_available())
		return;
	
	addr = (void *) ((unsigned long __force)
			 PCI_FIX_ADDR(token) & PAGE_MASK);
	if ((unsigned long)addr < ioremap_bot) {
		printk(KERN_WARNING "Attempt to iounmap early bolted mapping"
		       " at 0x%p\n", addr);
		return;
	}
	vunmap(addr);
}

void iounmap(volatile void __iomem *token)
{
	if (ppc_md.iounmap)
		ppc_md.iounmap(token);
	else
		__iounmap(token);
}

EXPORT_SYMBOL(ioremap);
EXPORT_SYMBOL(ioremap_wc);
EXPORT_SYMBOL(ioremap_prot);
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(__ioremap_at);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(__iounmap);
EXPORT_SYMBOL(__iounmap_at);

#ifndef __PAGETABLE_PUD_FOLDED
/* 4 level page table */
struct page *pgd_page(pgd_t pgd)
{
	if (pgd_huge(pgd))
		return pte_page(pgd_pte(pgd));
	return virt_to_page(pgd_page_vaddr(pgd));
}
#endif

struct page *pud_page(pud_t pud)
{
	if (pud_huge(pud))
		return pte_page(pud_pte(pud));
	return virt_to_page(pud_page_vaddr(pud));
}

/*
 * For hugepage we have pfn in the pmd, we use PTE_RPN_SHIFT bits for flags
 * For PTE page, we have a PTE_FRAG_SIZE (4K) aligned virtual address.
 */
struct page *pmd_page(pmd_t pmd)
{
	if (pmd_trans_huge(pmd) || pmd_huge(pmd))
		return pte_page(pmd_pte(pmd));
	return virt_to_page(pmd_page_vaddr(pmd));
}

#ifdef CONFIG_PPC_64K_PAGES
static pte_t *get_from_cache(struct mm_struct *mm)
{
	void *pte_frag, *ret;

	spin_lock(&mm->page_table_lock);
	ret = mm->context.pte_frag;
	if (ret) {
		pte_frag = ret + PTE_FRAG_SIZE;
		/*
		 * If we have taken up all the fragments mark PTE page NULL
		 */
		if (((unsigned long)pte_frag & ~PAGE_MASK) == 0)
			pte_frag = NULL;
		mm->context.pte_frag = pte_frag;
	}
	spin_unlock(&mm->page_table_lock);
	return (pte_t *)ret;
}

static pte_t *__alloc_for_cache(struct mm_struct *mm, int kernel)
{
	void *ret = NULL;
	struct page *page = alloc_page(GFP_KERNEL | __GFP_NOTRACK | __GFP_ZERO);
	if (!page)
		return NULL;
	if (!kernel && !pgtable_page_ctor(page)) {
		__free_page(page);
		return NULL;
	}

	ret = page_address(page);
	spin_lock(&mm->page_table_lock);
	/*
	 * If we find pgtable_page set, we return
	 * the allocated page with single fragement
	 * count.
	 */
	if (likely(!mm->context.pte_frag)) {
		set_page_count(page, PTE_FRAG_NR);
		mm->context.pte_frag = ret + PTE_FRAG_SIZE;
	}
	spin_unlock(&mm->page_table_lock);

	return (pte_t *)ret;
}

pte_t *pte_fragment_alloc(struct mm_struct *mm, unsigned long vmaddr, int kernel)
{
	pte_t *pte;

	pte = get_from_cache(mm);
	if (pte)
		return pte;

	return __alloc_for_cache(mm, kernel);
}
#endif /* CONFIG_PPC_64K_PAGES */

void pte_fragment_free(unsigned long *table, int kernel)
{
	struct page *page = virt_to_page(table);
	if (put_page_testzero(page)) {
		if (!kernel)
			pgtable_page_dtor(page);
		free_hot_cold_page(page, 0);
	}
}

#ifdef CONFIG_SMP
void pgtable_free_tlb(struct mmu_gather *tlb, void *table, int shift)
{
	unsigned long pgf = (unsigned long)table;

	BUG_ON(shift > MAX_PGTABLE_INDEX_SIZE);
	pgf |= shift;
	tlb_remove_table(tlb, (void *)pgf);
}

void __tlb_remove_table(void *_table)
{
	void *table = (void *)((unsigned long)_table & ~MAX_PGTABLE_INDEX_SIZE);
	unsigned shift = (unsigned long)_table & MAX_PGTABLE_INDEX_SIZE;

	if (!shift)
		/* PTE page needs special handling */
		pte_fragment_free(table, 0);
	else {
		BUG_ON(shift > MAX_PGTABLE_INDEX_SIZE);
		kmem_cache_free(PGT_CACHE(shift), table);
	}
}
#else
void pgtable_free_tlb(struct mmu_gather *tlb, void *table, int shift)
{
	if (!shift) {
		/* PTE page needs special handling */
		pte_fragment_free(table, 0);
	} else {
		BUG_ON(shift > MAX_PGTABLE_INDEX_SIZE);
		kmem_cache_free(PGT_CACHE(shift), table);
	}
}
#endif

#ifdef CONFIG_PPC_BOOK3S_64
void __init mmu_partition_table_init(void)
{
	unsigned long patb_size = 1UL << PATB_SIZE_SHIFT;
	unsigned long ptcr;

	BUILD_BUG_ON_MSG((PATB_SIZE_SHIFT > 36), "Partition table size too large.");
	partition_tb = __va(memblock_alloc_base(patb_size, patb_size,
						MEMBLOCK_ALLOC_ANYWHERE));

	/* Initialize the Partition Table with no entries */
	memset((void *)partition_tb, 0, patb_size);

	/*
	 * update partition table control register,
	 * 64 K size.
	 */
	ptcr = __pa(partition_tb) | (PATB_SIZE_SHIFT - 12);
	mtspr(SPRN_PTCR, ptcr);
	powernv_set_nmmu_ptcr(ptcr);
}

void mmu_partition_table_set_entry(unsigned int lpid, unsigned long dw0,
				   unsigned long dw1)
{
	unsigned long old = be64_to_cpu(partition_tb[lpid].patb0);

	partition_tb[lpid].patb0 = cpu_to_be64(dw0);
	partition_tb[lpid].patb1 = cpu_to_be64(dw1);

	/*
	 * Global flush of TLBs and partition table caches for this lpid.
	 * The type of flush (hash or radix) depends on what the previous
	 * use of this partition ID was, not the new use.
	 */
	asm volatile("ptesync" : : : "memory");
	if (old & PATB_HR)
		asm volatile(PPC_TLBIE_5(%0,%1,2,0,1) : :
			     "r" (TLBIEL_INVAL_SET_LPID), "r" (lpid));
	else
		asm volatile(PPC_TLBIE_5(%0,%1,2,0,0) : :
			     "r" (TLBIEL_INVAL_SET_LPID), "r" (lpid));
	asm volatile("eieio; tlbsync; ptesync" : : : "memory");
}
EXPORT_SYMBOL_GPL(mmu_partition_table_set_entry);
#endif /* CONFIG_PPC_BOOK3S_64 */
