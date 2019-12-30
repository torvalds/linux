// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file contains the routines for initializing the MMU
 * on the 8xx series of chips.
 *  -- christophe
 *
 *  Derived from arch/powerpc/mm/40x_mmu.c:
 */

#include <linux/memblock.h>
#include <linux/mmu_context.h>
#include <asm/fixmap.h>
#include <asm/code-patching.h>

#include <mm/mmu_decl.h>

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
	if (IS_ENABLED(CONFIG_PIN_TLB_DATA)) {
		unsigned long ctr = mfspr(SPRN_MD_CTR) & 0xfe000000;
		unsigned long flags = 0xf0 | MD_SPS16K | _PAGE_SH | _PAGE_DIRTY;
		int i = IS_ENABLED(CONFIG_PIN_TLB_IMMR) ? 29 : 28;
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
	}
}

static void __init mmu_mapin_immr(void)
{
	unsigned long p = PHYS_IMMR_BASE;
	unsigned long v = VIRT_IMMR_BASE;
	int offset;

	for (offset = 0; offset < IMMR_SIZE; offset += PAGE_SIZE)
		map_kernel_page(v + offset, p + offset, PAGE_KERNEL_NCG);
}

static void mmu_patch_cmp_limit(s32 *site, unsigned long mapped)
{
	modify_instruction_site(site, 0xffff, (unsigned long)__va(mapped) >> 16);
}

static void mmu_patch_addis(s32 *site, long simm)
{
	unsigned int instr = *(unsigned int *)patch_site_addr(site);

	instr &= 0xffff0000;
	instr |= ((unsigned long)simm) >> 16;
	patch_instruction_site(site, instr);
}

static void mmu_mapin_ram_chunk(unsigned long offset, unsigned long top, pgprot_t prot)
{
	unsigned long s = offset;
	unsigned long v = PAGE_OFFSET + s;
	phys_addr_t p = memstart_addr + s;

	for (; s < top; s += PAGE_SIZE) {
		map_kernel_page(v, p, prot);
		v += PAGE_SIZE;
		p += PAGE_SIZE;
	}
}

unsigned long __init mmu_mapin_ram(unsigned long base, unsigned long top)
{
	unsigned long mapped;

	if (__map_without_ltlbs) {
		mapped = 0;
		mmu_mapin_immr();
		if (!IS_ENABLED(CONFIG_PIN_TLB_IMMR))
			patch_instruction_site(&patch__dtlbmiss_immr_jmp, PPC_INST_NOP);
		if (!IS_ENABLED(CONFIG_PIN_TLB_TEXT))
			mmu_patch_cmp_limit(&patch__itlbmiss_linmem_top, 0);
	} else {
		unsigned long einittext8 = ALIGN(__pa(_einittext), SZ_8M);

		mapped = top & ~(LARGE_PAGE_SIZE_8M - 1);
		if (!IS_ENABLED(CONFIG_PIN_TLB_TEXT))
			mmu_patch_cmp_limit(&patch__itlbmiss_linmem_top, einittext8);

		/*
		 * Populate page tables to:
		 * - have them appear in /sys/kernel/debug/kernel_page_tables
		 * - allow the BDI to find the pages when they are not PINNED
		 */
		mmu_mapin_ram_chunk(0, einittext8, PAGE_KERNEL_X);
		mmu_mapin_ram_chunk(einittext8, mapped, PAGE_KERNEL);
		mmu_mapin_immr();
	}

	mmu_patch_cmp_limit(&patch__dtlbmiss_linmem_top, mapped);
	mmu_patch_cmp_limit(&patch__fixupdar_linmem_top, mapped);

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

void mmu_mark_initmem_nx(void)
{
	if (IS_ENABLED(CONFIG_STRICT_KERNEL_RWX) && CONFIG_ETEXT_SHIFT < 23)
		mmu_patch_addis(&patch__itlbmiss_linmem_top8,
				-((long)_etext & ~(LARGE_PAGE_SIZE_8M - 1)));
	if (!IS_ENABLED(CONFIG_PIN_TLB_TEXT)) {
		unsigned long einittext8 = ALIGN(__pa(_einittext), SZ_8M);
		unsigned long etext8 = ALIGN(__pa(_etext), SZ_8M);
		unsigned long etext = __pa(_etext);

		mmu_patch_cmp_limit(&patch__itlbmiss_linmem_top, __pa(_etext));

		/* Update page tables for PTDUMP and BDI */
		mmu_mapin_ram_chunk(0, einittext8, __pgprot(0));
		if (IS_ENABLED(CONFIG_STRICT_KERNEL_RWX)) {
			mmu_mapin_ram_chunk(0, etext, PAGE_KERNEL_TEXT);
			mmu_mapin_ram_chunk(etext, einittext8, PAGE_KERNEL);
		} else {
			mmu_mapin_ram_chunk(0, etext8, PAGE_KERNEL_TEXT);
			mmu_mapin_ram_chunk(etext8, einittext8, PAGE_KERNEL);
		}
	}
}

#ifdef CONFIG_STRICT_KERNEL_RWX
void mmu_mark_rodata_ro(void)
{
	unsigned long sinittext = __pa(_sinittext);
	unsigned long etext = __pa(_etext);

	if (CONFIG_DATA_SHIFT < 23)
		mmu_patch_addis(&patch__dtlbmiss_romem_top8,
				-__pa(((unsigned long)_sinittext) &
				      ~(LARGE_PAGE_SIZE_8M - 1)));
	mmu_patch_addis(&patch__dtlbmiss_romem_top, -__pa(_sinittext));

	/* Update page tables for PTDUMP and BDI */
	mmu_mapin_ram_chunk(0, sinittext, __pgprot(0));
	mmu_mapin_ram_chunk(0, etext, PAGE_KERNEL_ROX);
	mmu_mapin_ram_chunk(etext, sinittext, PAGE_KERNEL_RO);
}
#endif

void __init setup_initial_memory_limit(phys_addr_t first_memblock_base,
				       phys_addr_t first_memblock_size)
{
	/* We don't currently support the first MEMBLOCK not mapping 0
	 * physical on those processors
	 */
	BUG_ON(first_memblock_base != 0);

	/* 8xx can only access 32MB at the moment */
	memblock_set_current_limit(min_t(u64, first_memblock_size, 0x02000000));
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

	/* Context switch the PTE pointer for the Abatron BDI2000.
	 * The PGDIR is passed as second argument.
	 */
	if (IS_ENABLED(CONFIG_BDI_SWITCH))
		abatron_pteptrs[1] = pgd;

	/* Register M_TWB will contain base address of level 1 table minus the
	 * lower part of the kernel PGDIR base address, so that all accesses to
	 * level 1 table are done relative to lower part of kernel PGDIR base
	 * address.
	 */
	mtspr(SPRN_M_TWB, __pa(pgd) - offset);

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

#ifdef CONFIG_PPC_KUEP
void __init setup_kuep(bool disabled)
{
	if (disabled)
		return;

	pr_info("Activating Kernel Userspace Execution Prevention\n");

	mtspr(SPRN_MI_AP, MI_APG_KUEP);
}
#endif

#ifdef CONFIG_PPC_KUAP
void __init setup_kuap(bool disabled)
{
	pr_info("Activating Kernel Userspace Access Protection\n");

	if (disabled)
		pr_warn("KUAP cannot be disabled yet on 8xx when compiled in\n");

	mtspr(SPRN_MD_AP, MD_APG_KUAP);
}
#endif
