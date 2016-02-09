/*
 * This file contains the routines for initializing the MMU
 * on the 8xx series of chips.
 *  -- christophe
 *
 *  Derived from arch/powerpc/mm/40x_mmu.c:
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/memblock.h>

#include "mmu_decl.h"

extern int __map_without_ltlbs;
/*
 * MMU_init_hw does the chip-specific initialization of the MMU hardware.
 */
void __init MMU_init_hw(void)
{
	/* Nothing to do for the time being but keep it similar to other PPC */
}

#define LARGE_PAGE_SIZE_4M	(1<<22)
#define LARGE_PAGE_SIZE_8M	(1<<23)
#define LARGE_PAGE_SIZE_64M	(1<<26)

unsigned long __init mmu_mapin_ram(unsigned long top)
{
	unsigned long v, s, mapped;
	phys_addr_t p;

	v = KERNELBASE;
	p = 0;
	s = top;

	if (__map_without_ltlbs)
		return 0;

#ifdef CONFIG_PPC_4K_PAGES
	while (s >= LARGE_PAGE_SIZE_8M) {
		pmd_t *pmdp;
		unsigned long val = p | MD_PS8MEG;

		pmdp = pmd_offset(pud_offset(pgd_offset_k(v), v), v);
		*pmdp++ = __pmd(val);
		*pmdp++ = __pmd(val + LARGE_PAGE_SIZE_4M);

		v += LARGE_PAGE_SIZE_8M;
		p += LARGE_PAGE_SIZE_8M;
		s -= LARGE_PAGE_SIZE_8M;
	}
#else /* CONFIG_PPC_16K_PAGES */
	while (s >= LARGE_PAGE_SIZE_64M) {
		pmd_t *pmdp;
		unsigned long val = p | MD_PS8MEG;

		pmdp = pmd_offset(pud_offset(pgd_offset_k(v), v), v);
		*pmdp++ = __pmd(val);

		v += LARGE_PAGE_SIZE_64M;
		p += LARGE_PAGE_SIZE_64M;
		s -= LARGE_PAGE_SIZE_64M;
	}
#endif

	mapped = top - s;

	/* If the size of RAM is not an exact power of two, we may not
	 * have covered RAM in its entirety with 8 MiB
	 * pages. Consequently, restrict the top end of RAM currently
	 * allocable so that calls to the MEMBLOCK to allocate PTEs for "tail"
	 * coverage with normal-sized pages (or other reasons) do not
	 * attempt to allocate outside the allowed range.
	 */
	memblock_set_current_limit(mapped);

	return mapped;
}
