// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file contains the routines for TLB flushing.
 * On machines where the MMU uses a hash table to store virtual to
 * physical translations, these routines flush entries from the
 * hash table also.
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
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/export.h>

#include <asm/tlbflush.h>
#include <asm/tlb.h>

#include <mm/mmu_decl.h>

/*
 * TLB flushing:
 *
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes kernel pages
 *
 * since the hardware hash table functions as an extension of the
 * tlb as far as the linux tables are concerned, flush it too.
 *    -- Cort
 */

/*
 * For each address in the range, find the pte for the address
 * and check _PAGE_HASHPTE bit; if it is set, find and destroy
 * the corresponding HPTE.
 */
void hash__flush_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	pmd_t *pmd;
	unsigned long pmd_end;
	int count;
	unsigned int ctx = mm->context.id;

	start &= PAGE_MASK;
	if (start >= end)
		return;
	end = (end - 1) | ~PAGE_MASK;
	pmd = pmd_off(mm, start);
	for (;;) {
		pmd_end = ((start + PGDIR_SIZE) & PGDIR_MASK) - 1;
		if (pmd_end > end)
			pmd_end = end;
		if (!pmd_none(*pmd)) {
			count = ((pmd_end - start) >> PAGE_SHIFT) + 1;
			flush_hash_pages(ctx, start, pmd_val(*pmd), count);
		}
		if (pmd_end == end)
			break;
		start = pmd_end + 1;
		++pmd;
	}
}
EXPORT_SYMBOL(hash__flush_range);

/*
 * Flush all the (user) entries for the address space described by mm.
 */
void hash__flush_tlb_mm(struct mm_struct *mm)
{
	struct vm_area_struct *mp;

	/*
	 * It is safe to go down the mm's list of vmas when called
	 * from dup_mmap, holding mmap_lock.  It would also be safe from
	 * unmap_region or exit_mmap, but not from vmtruncate on SMP -
	 * but it seems dup_mmap is the only SMP case which gets here.
	 */
	for (mp = mm->mmap; mp != NULL; mp = mp->vm_next)
		hash__flush_range(mp->vm_mm, mp->vm_start, mp->vm_end);
}
EXPORT_SYMBOL(hash__flush_tlb_mm);

void hash__flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	struct mm_struct *mm;
	pmd_t *pmd;

	mm = (vmaddr < TASK_SIZE)? vma->vm_mm: &init_mm;
	pmd = pmd_off(mm, vmaddr);
	if (!pmd_none(*pmd))
		flush_hash_pages(mm->context.id, vmaddr, pmd_val(*pmd), 1);
}
EXPORT_SYMBOL(hash__flush_tlb_page);
