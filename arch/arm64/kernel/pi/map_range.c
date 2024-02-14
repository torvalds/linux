// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2023 Google LLC
// Author: Ard Biesheuvel <ardb@google.com>

#include <linux/types.h>
#include <linux/sizes.h>

#include <asm/memory.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>

#include "pi.h"

/**
 * map_range - Map a contiguous range of physical pages into virtual memory
 *
 * @pte:		Address of physical pointer to array of pages to
 *			allocate page tables from
 * @start:		Virtual address of the start of the range
 * @end:		Virtual address of the end of the range (exclusive)
 * @pa:			Physical address of the start of the range
 * @prot:		Access permissions of the range
 * @level:		Translation level for the mapping
 * @tbl:		The level @level page table to create the mappings in
 * @may_use_cont:	Whether the use of the contiguous attribute is allowed
 * @va_offset:		Offset between a physical page and its current mapping
 * 			in the VA space
 */
void __init map_range(u64 *pte, u64 start, u64 end, u64 pa, pgprot_t prot,
		      int level, pte_t *tbl, bool may_use_cont, u64 va_offset)
{
	u64 cmask = (level == 3) ? CONT_PTE_SIZE - 1 : U64_MAX;
	u64 protval = pgprot_val(prot) & ~PTE_TYPE_MASK;
	int lshift = (3 - level) * (PAGE_SHIFT - 3);
	u64 lmask = (PAGE_SIZE << lshift) - 1;

	start	&= PAGE_MASK;
	pa	&= PAGE_MASK;

	/* Advance tbl to the entry that covers start */
	tbl += (start >> (lshift + PAGE_SHIFT)) % PTRS_PER_PTE;

	/*
	 * Set the right block/page bits for this level unless we are
	 * clearing the mapping
	 */
	if (protval)
		protval |= (level < 3) ? PMD_TYPE_SECT : PTE_TYPE_PAGE;

	while (start < end) {
		u64 next = min((start | lmask) + 1, PAGE_ALIGN(end));

		if (level < 3 && (start | next | pa) & lmask) {
			/*
			 * This chunk needs a finer grained mapping. Create a
			 * table mapping if necessary and recurse.
			 */
			if (pte_none(*tbl)) {
				*tbl = __pte(__phys_to_pte_val(*pte) |
					     PMD_TYPE_TABLE | PMD_TABLE_UXN);
				*pte += PTRS_PER_PTE * sizeof(pte_t);
			}
			map_range(pte, start, next, pa, prot, level + 1,
				  (pte_t *)(__pte_to_phys(*tbl) + va_offset),
				  may_use_cont, va_offset);
		} else {
			/*
			 * Start a contiguous range if start and pa are
			 * suitably aligned
			 */
			if (((start | pa) & cmask) == 0 && may_use_cont)
				protval |= PTE_CONT;

			/*
			 * Clear the contiguous attribute if the remaining
			 * range does not cover a contiguous block
			 */
			if ((end & ~cmask) <= start)
				protval &= ~PTE_CONT;

			/* Put down a block or page mapping */
			*tbl = __pte(__phys_to_pte_val(pa) | protval);
		}
		pa += next - start;
		start = next;
		tbl++;
	}
}

asmlinkage u64 __init create_init_idmap(pgd_t *pg_dir, pteval_t clrmask)
{
	u64 ptep = (u64)pg_dir + PAGE_SIZE;
	pgprot_t text_prot = PAGE_KERNEL_ROX;
	pgprot_t data_prot = PAGE_KERNEL;

	pgprot_val(text_prot) &= ~clrmask;
	pgprot_val(data_prot) &= ~clrmask;

	map_range(&ptep, (u64)_stext, (u64)__initdata_begin, (u64)_stext,
		  text_prot, IDMAP_ROOT_LEVEL, (pte_t *)pg_dir, false, 0);
	map_range(&ptep, (u64)__initdata_begin, (u64)_end, (u64)__initdata_begin,
		  data_prot, IDMAP_ROOT_LEVEL, (pte_t *)pg_dir, false, 0);

	return ptep;
}
