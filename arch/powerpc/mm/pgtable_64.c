// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  This file contains pgtable related functions for 64-bit machines.
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

#include <asm/page.h>
#include <asm/prom.h>
#include <asm/mmu_context.h>
#include <asm/mmu.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/tlb.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/firmware.h>
#include <asm/dma.h>

#include <mm/mmu_decl.h>


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
unsigned long __vmalloc_start;
EXPORT_SYMBOL(__vmalloc_start);
unsigned long __vmalloc_end;
EXPORT_SYMBOL(__vmalloc_end);
unsigned long __kernel_io_start;
EXPORT_SYMBOL(__kernel_io_start);
unsigned long __kernel_io_end;
struct page *vmemmap;
EXPORT_SYMBOL(vmemmap);
unsigned long __pte_frag_nr;
EXPORT_SYMBOL(__pte_frag_nr);
unsigned long __pte_frag_size_shift;
EXPORT_SYMBOL(__pte_frag_size_shift);
#endif

#ifndef __PAGETABLE_PUD_FOLDED
/* 4 level page table */
struct page *p4d_page(p4d_t p4d)
{
	if (p4d_is_leaf(p4d)) {
		VM_WARN_ON(!p4d_huge(p4d));
		return pte_page(p4d_pte(p4d));
	}
	return virt_to_page(p4d_pgtable(p4d));
}
#endif

struct page *pud_page(pud_t pud)
{
	if (pud_is_leaf(pud)) {
		VM_WARN_ON(!pud_huge(pud));
		return pte_page(pud_pte(pud));
	}
	return virt_to_page(pud_pgtable(pud));
}

/*
 * For hugepage we have pfn in the pmd, we use PTE_RPN_SHIFT bits for flags
 * For PTE page, we have a PTE_FRAG_SIZE (4K) aligned virtual address.
 */
struct page *pmd_page(pmd_t pmd)
{
	if (pmd_is_leaf(pmd)) {
		VM_WARN_ON(!(pmd_large(pmd) || pmd_huge(pmd)));
		return pte_page(pmd_pte(pmd));
	}
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

	// mark_initmem_nx() should have already run by now
	ptdump_check_wx();
}

void mark_initmem_nx(void)
{
	if (radix_enabled())
		radix__mark_initmem_nx();
	else
		hash__mark_initmem_nx();
}
#endif
