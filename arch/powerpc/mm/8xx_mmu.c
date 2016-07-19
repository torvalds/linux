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

void setup_initial_memory_limit(phys_addr_t first_memblock_base,
				phys_addr_t first_memblock_size)
{
	/* We don't currently support the first MEMBLOCK not mapping 0
	 * physical on those processors
	 */
	BUG_ON(first_memblock_base != 0);

#ifdef CONFIG_PIN_TLB
	/* 8xx can only access 24MB at the moment */
	memblock_set_current_limit(min_t(u64, first_memblock_size, 0x01800000));
#else
	/* 8xx can only access 8MB at the moment */
	memblock_set_current_limit(min_t(u64, first_memblock_size, 0x00800000));
#endif
}

/*
 * Set up to use a given MMU context.
 * id is context number, pgd is PGD pointer.
 *
 * We place the physical address of the new task page directory loaded
 * into the MMU base register, and set the ASID compare register with
 * the new "context."
 */
void set_context(unsigned long id, pgd_t *pgd)
{
	s16 offset = (s16)(__pa(swapper_pg_dir));

#ifdef CONFIG_BDI_SWITCH
	pgd_t	**ptr = *(pgd_t ***)(KERNELBASE + 0xf0);

	/* Context switch the PTE pointer for the Abatron BDI2000.
	 * The PGDIR is passed as second argument.
	 */
	*(ptr + 1) = pgd;
#endif

	/* Register M_TW will contain base address of level 1 table minus the
	 * lower part of the kernel PGDIR base address, so that all accesses to
	 * level 1 table are done relative to lower part of kernel PGDIR base
	 * address.
	 */
	mtspr(SPRN_M_TW, __pa(pgd) - offset);

	/* Update context */
	mtspr(SPRN_M_CASID, id);
	/* sync */
	mb();
}

void flush_instruction_cache(void)
{
	isync();
	mtspr(SPRN_IC_CST, IDC_INVALL);
	isync();
}
