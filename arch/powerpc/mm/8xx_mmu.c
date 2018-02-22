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
#include <asm/fixmap.h>
#include <asm/code-patching.h>

#include "mmu_decl.h"

#define IMMR_SIZE (FIX_IMMR_SIZE << PAGE_SHIFT)

extern int __map_without_ltlbs;

static unsigned long block_mapped_ram;

/*
 * Return PA for this VA if it is in an area mapped with LTLBs.
 * Otherwise, returns 0
 */
phys_addr_t v_block_mapped(unsigned long va)
{
	unsigned long p = PHYS_IMMR_BASE;

	if (__map_without_ltlbs)
		return 0;
	if (va >= VIRT_IMMR_BASE && va < VIRT_IMMR_BASE + IMMR_SIZE)
		return p + va - VIRT_IMMR_BASE;
	if (va >= PAGE_OFFSET && va < PAGE_OFFSET + block_mapped_ram)
		return __pa(va);
	return 0;
}

/*
 * Return VA for a given PA mapped with LTLBs or 0 if not mapped
 */
unsigned long p_block_mapped(phys_addr_t pa)
{
	unsigned long p = PHYS_IMMR_BASE;

	if (__map_without_ltlbs)
		return 0;
	if (pa >= p && pa < p + IMMR_SIZE)
		return VIRT_IMMR_BASE + pa - p;
	if (pa < block_mapped_ram)
		return (unsigned long)__va(pa);
	return 0;
}

#define LARGE_PAGE_SIZE_8M	(1<<23)

/*
 * MMU_init_hw does the chip-specific initialization of the MMU hardware.
 */
void __init MMU_init_hw(void)
{
	/* PIN up to the 3 first 8Mb after IMMR in DTLB table */
#ifdef CONFIG_PIN_TLB_DATA
	unsigned long ctr = mfspr(SPRN_MD_CTR) & 0xfe000000;
	unsigned long flags = 0xf0 | MD_SPS16K | _PAGE_SHARED | _PAGE_DIRTY;
#ifdef CONFIG_PIN_TLB_IMMR
	int i = 29;
#else
	int i = 28;
#endif
	unsigned long addr = 0;
	unsigned long mem = total_lowmem;

	for (; i < 32 && mem >= LARGE_PAGE_SIZE_8M; i++) {
		mtspr(SPRN_MD_CTR, ctr | (i << 8));
		mtspr(SPRN_MD_EPN, (unsigned long)__va(addr) | MD_EVALID);
		mtspr(SPRN_MD_TWC, MD_PS8MEG | MD_SVALID);
		mtspr(SPRN_MD_RPN, addr | flags | _PAGE_PRESENT);
		addr += LARGE_PAGE_SIZE_8M;
		mem -= LARGE_PAGE_SIZE_8M;
	}
#endif
}

static void __init mmu_mapin_immr(void)
{
	unsigned long p = PHYS_IMMR_BASE;
	unsigned long v = VIRT_IMMR_BASE;
	unsigned long f = pgprot_val(PAGE_KERNEL_NCG);
	int offset;

	for (offset = 0; offset < IMMR_SIZE; offset += PAGE_SIZE)
		map_kernel_page(v + offset, p + offset, f);
}

/* Address of instructions to patch */
#ifndef CONFIG_PIN_TLB_IMMR
extern unsigned int DTLBMiss_jmp;
#endif
extern unsigned int DTLBMiss_cmp, FixupDAR_cmp;
#ifndef CONFIG_PIN_TLB_TEXT
extern unsigned int ITLBMiss_cmp;
#endif

static void __init mmu_patch_cmp_limit(unsigned int *addr, unsigned long mapped)
{
	unsigned int instr = *addr;

	instr &= 0xffff0000;
	instr |= (unsigned long)__va(mapped) >> 16;
	patch_instruction(addr, instr);
}

unsigned long __init mmu_mapin_ram(unsigned long top)
{
	unsigned long mapped;

	if (__map_without_ltlbs) {
		mapped = 0;
		mmu_mapin_immr();
#ifndef CONFIG_PIN_TLB_IMMR
		patch_instruction(&DTLBMiss_jmp, PPC_INST_NOP);
#endif
#ifndef CONFIG_PIN_TLB_TEXT
		mmu_patch_cmp_limit(&ITLBMiss_cmp, 0);
#endif
	} else {
		mapped = top & ~(LARGE_PAGE_SIZE_8M - 1);
	}

	mmu_patch_cmp_limit(&DTLBMiss_cmp, mapped);
	mmu_patch_cmp_limit(&FixupDAR_cmp, mapped);

	/* If the size of RAM is not an exact power of two, we may not
	 * have covered RAM in its entirety with 8 MiB
	 * pages. Consequently, restrict the top end of RAM currently
	 * allocable so that calls to the MEMBLOCK to allocate PTEs for "tail"
	 * coverage with normal-sized pages (or other reasons) do not
	 * attempt to allocate outside the allowed range.
	 */
	if (mapped)
		memblock_set_current_limit(mapped);

	block_mapped_ram = mapped;

	return mapped;
}

void __init setup_initial_memory_limit(phys_addr_t first_memblock_base,
				       phys_addr_t first_memblock_size)
{
	/* We don't currently support the first MEMBLOCK not mapping 0
	 * physical on those processors
	 */
	BUG_ON(first_memblock_base != 0);

	/* 8xx can only access 24MB at the moment */
	memblock_set_current_limit(min_t(u64, first_memblock_size, 0x01800000));
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
	mtspr(SPRN_M_CASID, id - 1);
	/* sync */
	mb();
}

void flush_instruction_cache(void)
{
	isync();
	mtspr(SPRN_IC_CST, IDC_INVALL);
	isync();
}
