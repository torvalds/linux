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
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
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
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>

#include "mmu_decl.h"

/*
 * Called when unmapping pages to flush entries from the TLB/hash table.
 */
void flush_hash_entry(struct mm_struct *mm, pte_t *ptep, unsigned long addr)
{
	unsigned long ptephys;

	if (Hash != 0) {
		ptephys = __pa(ptep) & PAGE_MASK;
		flush_hash_pages(mm->context.id, addr, ptephys, 1);
	}
}

/*
 * Called by ptep_set_access_flags, must flush on CPUs for which the
 * DSI handler can't just "fixup" the TLB on a write fault
 */
void flush_tlb_page_nohash(struct vm_area_struct *vma, unsigned long addr)
{
	if (Hash != 0)
		return;
	_tlbie(addr);
}

/*
 * Called at the end of a mmu_gather operation to make sure the
 * TLB flush is completely done.
 */
void tlb_flush(struct mmu_gather *tlb)
{
	if (Hash == 0) {
		/*
		 * 603 needs to flush the whole TLB here since
		 * it doesn't use a hash table.
		 */
		_tlbia();
	}
}

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
 * 750 SMP is a Bad Idea because the 750 doesn't broadcast all
 * the cache operations on the bus.  Hence we need to use an IPI
 * to get the other CPU(s) to invalidate their TLBs.
 */
#ifdef CONFIG_SMP_750
#define FINISH_FLUSH	smp_send_tlb_invalidate(0)
#else
#define FINISH_FLUSH	do { } while (0)
#endif

static void flush_range(struct mm_struct *mm, unsigned long start,
			unsigned long end)
{
	pmd_t *pmd;
	unsigned long pmd_end;
	int count;
	unsigned int ctx = mm->context.id;

	if (Hash == 0) {
		_tlbia();
		return;
	}
	start &= PAGE_MASK;
	if (start >= end)
		return;
	end = (end - 1) | ~PAGE_MASK;
	pmd = pmd_offset(pgd_offset(mm, start), start);
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

/*
 * Flush kernel TLB entries in the given range
 */
void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	flush_range(&init_mm, start, end);
	FINISH_FLUSH;
}

/*
 * Flush all the (user) entries for the address space described by mm.
 */
void flush_tlb_mm(struct mm_struct *mm)
{
	struct vm_area_struct *mp;

	if (Hash == 0) {
		_tlbia();
		return;
	}

	for (mp = mm->mmap; mp != NULL; mp = mp->vm_next)
		flush_range(mp->vm_mm, mp->vm_start, mp->vm_end);
	FINISH_FLUSH;
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	struct mm_struct *mm;
	pmd_t *pmd;

	if (Hash == 0) {
		_tlbie(vmaddr);
		return;
	}
	mm = (vmaddr < TASK_SIZE)? vma->vm_mm: &init_mm;
	pmd = pmd_offset(pgd_offset(mm, vmaddr), vmaddr);
	if (!pmd_none(*pmd))
		flush_hash_pages(mm->context.id, vmaddr, pmd_val(*pmd), 1);
	FINISH_FLUSH;
}

/*
 * For each address in the range, find the pte for the address
 * and check _PAGE_HASHPTE bit; if it is set, find and destroy
 * the corresponding HPTE.
 */
void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	flush_range(vma->vm_mm, start, end);
	FINISH_FLUSH;
}
