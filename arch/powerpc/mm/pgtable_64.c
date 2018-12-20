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

#include "mmu_decl.h"


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
unsigned long __pud_cache_index;
EXPORT_SYMBOL(__pud_cache_index);
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
unsigned long __kernel_io_start;
EXPORT_SYMBOL(__kernel_io_start);
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
void __iomem *__ioremap_at(phys_addr_t pa, void *ea, unsigned long size, pgprot_t prot)
{
	unsigned long i;

	/* We don't support the 4K PFN hack with ioremap */
	if (pgprot_val(prot) & H_PAGE_4K_PFN)
		return NULL;

	WARN_ON(pa & ~PAGE_MASK);
	WARN_ON(((unsigned long)ea) & ~PAGE_MASK);
	WARN_ON(size & ~PAGE_MASK);

	for (i = 0; i < size; i += PAGE_SIZE)
		if (map_kernel_page((unsigned long)ea + i, pa + i, prot))
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
				pgprot_t prot, void *caller)
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
		ret = __ioremap_at(paligned, area->addr, size, prot);
		if (!ret)
			vunmap(area->addr);
	} else {
		ret = __ioremap_at(paligned, (void *)ioremap_bot, size, prot);
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
	return __ioremap_caller(addr, size, __pgprot(flags), __builtin_return_address(0));
}

void __iomem * ioremap(phys_addr_t addr, unsigned long size)
{
	pgprot_t prot = pgprot_noncached(PAGE_KERNEL);
	void *caller = __builtin_return_address(0);

	if (ppc_md.ioremap)
		return ppc_md.ioremap(addr, size, prot, caller);
	return __ioremap_caller(addr, size, prot, caller);
}

void __iomem * ioremap_wc(phys_addr_t addr, unsigned long size)
{
	pgprot_t prot = pgprot_noncached_wc(PAGE_KERNEL);
	void *caller = __builtin_return_address(0);

	if (ppc_md.ioremap)
		return ppc_md.ioremap(addr, size, prot, caller);
	return __ioremap_caller(addr, size, prot, caller);
}

void __iomem *ioremap_coherent(phys_addr_t addr, unsigned long size)
{
	pgprot_t prot = pgprot_cached(PAGE_KERNEL);
	void *caller = __builtin_return_address(0);

	if (ppc_md.ioremap)
		return ppc_md.ioremap(addr, size, prot, caller);
	return __ioremap_caller(addr, size, prot, caller);
}

void __iomem * ioremap_prot(phys_addr_t addr, unsigned long size,
			     unsigned long flags)
{
	pte_t pte = __pte(flags);
	void *caller = __builtin_return_address(0);

	/* writeable implies dirty for kernel addresses */
	if (pte_write(pte))
		pte = pte_mkdirty(pte);

	/* we don't want to let _PAGE_EXEC leak out */
	pte = pte_exprotect(pte);
	/*
	 * Force kernel mapping.
	 */
	pte = pte_mkprivileged(pte);

	if (ppc_md.ioremap)
		return ppc_md.ioremap(addr, size, pte_pgprot(pte), caller);
	return __ioremap_caller(addr, size, pte_pgprot(pte), caller);
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
	if (pmd_large(pmd) || pmd_huge(pmd) || pmd_devmap(pmd))
		return pte_page(pmd_pte(pmd));
	return virt_to_page(pmd_page_vaddr(pmd));
}

#ifdef CONFIG_STRICT_KERNEL_RWX
void mark_rodata_ro(void)
{
	if (!mmu_has_feature(MMU_FTR_KERNEL_RO)) {
		pr_warn("Warning: Unable to mark rodata read only on this CPU.\n");
		return;
	}

	if (radix_enabled())
		radix__mark_rodata_ro();
	else
		hash__mark_rodata_ro();
}

void mark_initmem_nx(void)
{
	if (radix_enabled())
		radix__mark_initmem_nx();
	else
		hash__mark_initmem_nx();
}
#endif
