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
#include <linux/hugetlb.h>
#include <asm/fixmap.h>
#include <asm/code-patching.h>
#include <asm/inst.h>
#include <asm/pgalloc.h>

#include <mm/mmu_decl.h>

#define IMMR_SIZE (FIX_IMMR_SIZE << PAGE_SHIFT)

extern int __map_without_ltlbs;

static unsigned long block_mapped_ram;

/*
 * Return PA for this VA if it is in an area mapped with LTLBs or fixmap.
 * Otherwise, returns 0
 */
phys_addr_t v_block_mapped(unsigned long va)
{
	unsigned long p = PHYS_IMMR_BASE;

	if (va >= VIRT_IMMR_BASE && va < VIRT_IMMR_BASE + IMMR_SIZE)
		return p + va - VIRT_IMMR_BASE;
	if (__map_without_ltlbs)
		return 0;
	if (va >= PAGE_OFFSET && va < PAGE_OFFSET + block_mapped_ram)
		return __pa(va);
	return 0;
}

/*
 * Return VA for a given PA mapped with LTLBs or fixmap
 * Return 0 if not mapped
 */
unsigned long p_block_mapped(phys_addr_t pa)
{
	unsigned long p = PHYS_IMMR_BASE;

	if (pa >= p && pa < p + IMMR_SIZE)
		return VIRT_IMMR_BASE + pa - p;
	if (__map_without_ltlbs)
		return 0;
	if (pa < block_mapped_ram)
		return (unsigned long)__va(pa);
	return 0;
}

static pte_t __init *early_hugepd_alloc_kernel(hugepd_t *pmdp, unsigned long va)
{
	if (hpd_val(*pmdp) == 0) {
		pte_t *ptep = memblock_alloc(sizeof(pte_basic_t), SZ_4K);

		if (!ptep)
			return NULL;

		hugepd_populate_kernel((hugepd_t *)pmdp, ptep, PAGE_SHIFT_8M);
		hugepd_populate_kernel((hugepd_t *)pmdp + 1, ptep, PAGE_SHIFT_8M);
	}
	return hugepte_offset(*(hugepd_t *)pmdp, va, PGDIR_SHIFT);
}

static int __ref __early_map_kernel_hugepage(unsigned long va, phys_addr_t pa,
					     pgprot_t prot, int psize, bool new)
{
	pmd_t *pmdp = pmd_ptr_k(va);
	pte_t *ptep;

	if (WARN_ON(psize != MMU_PAGE_512K && psize != MMU_PAGE_8M))
		return -EINVAL;

	if (new) {
		if (WARN_ON(slab_is_available()))
			return -EINVAL;

		if (psize == MMU_PAGE_512K)
			ptep = early_pte_alloc_kernel(pmdp, va);
		else
			ptep = early_hugepd_alloc_kernel((hugepd_t *)pmdp, va);
	} else {
		if (psize == MMU_PAGE_512K)
			ptep = pte_offset_kernel(pmdp, va);
		else
			ptep = hugepte_offset(*(hugepd_t *)pmdp, va, PGDIR_SHIFT);
	}

	if (WARN_ON(!ptep))
		return -ENOMEM;

	/* The PTE should never be already present */
	if (new && WARN_ON(pte_present(*ptep) && pgprot_val(prot)))
		return -EINVAL;

	set_huge_pte_at(&init_mm, va, ptep, pte_mkhuge(pfn_pte(pa >> PAGE_SHIFT, prot)));

	return 0;
}

/*
 * MMU_init_hw does the chip-specific initialization of the MMU hardware.
 */
void __init MMU_init_hw(void)
{
}

static bool immr_is_mapped __initdata;

void __init mmu_mapin_immr(void)
{
	if (immr_is_mapped)
		return;

	immr_is_mapped = true;

	__early_map_kernel_hugepage(VIRT_IMMR_BASE, PHYS_IMMR_BASE,
				    PAGE_KERNEL_NCG, MMU_PAGE_512K, true);
}

unsigned long __init mmu_mapin_ram(unsigned long base, unsigned long top)
{
	mmu_mapin_immr();

	return 0;
}

void mmu_mark_initmem_nx(void)
{
}

#ifdef CONFIG_STRICT_KERNEL_RWX
void mmu_mark_rodata_ro(void)
{
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
	memblock_set_current_limit(min_t(u64, first_memblock_size, SZ_32M));
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
