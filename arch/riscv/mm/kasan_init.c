// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Andes Technology Corporation

#include <linux/pfn.h>
#include <linux/init_task.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/fixmap.h>
#include <asm/pgalloc.h>

/*
 * Kasan shadow region must lie at a fixed address across sv39, sv48 and sv57
 * which is right before the kernel.
 *
 * For sv39, the region is aligned on PGDIR_SIZE so we only need to populate
 * the page global directory with kasan_early_shadow_pmd.
 *
 * For sv48 and sv57, the region is not aligned on PGDIR_SIZE so the mapping
 * must be divided as follows:
 * - the first PGD entry, although incomplete, is populated with
 *   kasan_early_shadow_pud/p4d
 * - the PGD entries in the middle are populated with kasan_early_shadow_pud/p4d
 * - the last PGD entry is shared with the kernel mapping so populated at the
 *   lower levels pud/p4d
 *
 * In addition, when shallow populating a kasan region (for example vmalloc),
 * this region may also not be aligned on PGDIR size, so we must go down to the
 * pud level too.
 */

extern pgd_t early_pg_dir[PTRS_PER_PGD];

static void __init kasan_populate_pte(pmd_t *pmd, unsigned long vaddr, unsigned long end)
{
	phys_addr_t phys_addr;
	pte_t *ptep, *base_pte;

	if (pmd_none(*pmd))
		base_pte = memblock_alloc(PTRS_PER_PTE * sizeof(pte_t), PAGE_SIZE);
	else
		base_pte = (pte_t *)pmd_page_vaddr(*pmd);

	ptep = base_pte + pte_index(vaddr);

	do {
		if (pte_none(*ptep)) {
			phys_addr = memblock_phys_alloc(PAGE_SIZE, PAGE_SIZE);
			set_pte(ptep, pfn_pte(PFN_DOWN(phys_addr), PAGE_KERNEL));
		}
	} while (ptep++, vaddr += PAGE_SIZE, vaddr != end);

	set_pmd(pmd, pfn_pmd(PFN_DOWN(__pa(base_pte)), PAGE_TABLE));
}

static void __init kasan_populate_pmd(pud_t *pud, unsigned long vaddr, unsigned long end)
{
	phys_addr_t phys_addr;
	pmd_t *pmdp, *base_pmd;
	unsigned long next;

	if (pud_none(*pud)) {
		base_pmd = memblock_alloc(PTRS_PER_PMD * sizeof(pmd_t), PAGE_SIZE);
	} else {
		base_pmd = (pmd_t *)pud_pgtable(*pud);
		if (base_pmd == lm_alias(kasan_early_shadow_pmd))
			base_pmd = memblock_alloc(PTRS_PER_PMD * sizeof(pmd_t), PAGE_SIZE);
	}

	pmdp = base_pmd + pmd_index(vaddr);

	do {
		next = pmd_addr_end(vaddr, end);

		if (pmd_none(*pmdp) && IS_ALIGNED(vaddr, PMD_SIZE) && (next - vaddr) >= PMD_SIZE) {
			phys_addr = memblock_phys_alloc(PMD_SIZE, PMD_SIZE);
			if (phys_addr) {
				set_pmd(pmdp, pfn_pmd(PFN_DOWN(phys_addr), PAGE_KERNEL));
				continue;
			}
		}

		kasan_populate_pte(pmdp, vaddr, next);
	} while (pmdp++, vaddr = next, vaddr != end);

	/*
	 * Wait for the whole PGD to be populated before setting the PGD in
	 * the page table, otherwise, if we did set the PGD before populating
	 * it entirely, memblock could allocate a page at a physical address
	 * where KASAN is not populated yet and then we'd get a page fault.
	 */
	set_pud(pud, pfn_pud(PFN_DOWN(__pa(base_pmd)), PAGE_TABLE));
}

static void __init kasan_populate_pud(pgd_t *pgd,
				      unsigned long vaddr, unsigned long end,
				      bool early)
{
	phys_addr_t phys_addr;
	pud_t *pudp, *base_pud;
	unsigned long next;

	if (early) {
		/*
		 * We can't use pgd_page_vaddr here as it would return a linear
		 * mapping address but it is not mapped yet, but when populating
		 * early_pg_dir, we need the physical address and when populating
		 * swapper_pg_dir, we need the kernel virtual address so use
		 * pt_ops facility.
		 */
		base_pud = pt_ops.get_pud_virt(pfn_to_phys(_pgd_pfn(*pgd)));
	} else {
		base_pud = (pud_t *)pgd_page_vaddr(*pgd);
		if (base_pud == lm_alias(kasan_early_shadow_pud))
			base_pud = memblock_alloc(PTRS_PER_PUD * sizeof(pud_t), PAGE_SIZE);
	}

	pudp = base_pud + pud_index(vaddr);

	do {
		next = pud_addr_end(vaddr, end);

		if (pud_none(*pudp) && IS_ALIGNED(vaddr, PUD_SIZE) && (next - vaddr) >= PUD_SIZE) {
			if (early) {
				phys_addr = __pa(((uintptr_t)kasan_early_shadow_pmd));
				set_pud(pudp, pfn_pud(PFN_DOWN(phys_addr), PAGE_TABLE));
				continue;
			} else {
				phys_addr = memblock_phys_alloc(PUD_SIZE, PUD_SIZE);
				if (phys_addr) {
					set_pud(pudp, pfn_pud(PFN_DOWN(phys_addr), PAGE_KERNEL));
					continue;
				}
			}
		}

		kasan_populate_pmd(pudp, vaddr, next);
	} while (pudp++, vaddr = next, vaddr != end);

	/*
	 * Wait for the whole PGD to be populated before setting the PGD in
	 * the page table, otherwise, if we did set the PGD before populating
	 * it entirely, memblock could allocate a page at a physical address
	 * where KASAN is not populated yet and then we'd get a page fault.
	 */
	if (!early)
		set_pgd(pgd, pfn_pgd(PFN_DOWN(__pa(base_pud)), PAGE_TABLE));
}

#define kasan_early_shadow_pgd_next			(pgtable_l4_enabled ?	\
				(uintptr_t)kasan_early_shadow_pud :		\
				(uintptr_t)kasan_early_shadow_pmd)
#define kasan_populate_pgd_next(pgdp, vaddr, next, early)			\
		(pgtable_l4_enabled ?						\
			kasan_populate_pud(pgdp, vaddr, next, early) :		\
			kasan_populate_pmd((pud_t *)pgdp, vaddr, next))

static void __init kasan_populate_pgd(pgd_t *pgdp,
				      unsigned long vaddr, unsigned long end,
				      bool early)
{
	phys_addr_t phys_addr;
	unsigned long next;

	do {
		next = pgd_addr_end(vaddr, end);

		if (IS_ALIGNED(vaddr, PGDIR_SIZE) && (next - vaddr) >= PGDIR_SIZE) {
			if (early) {
				phys_addr = __pa((uintptr_t)kasan_early_shadow_pgd_next);
				set_pgd(pgdp, pfn_pgd(PFN_DOWN(phys_addr), PAGE_TABLE));
				continue;
			} else if (pgd_page_vaddr(*pgdp) ==
				   (unsigned long)lm_alias(kasan_early_shadow_pgd_next)) {
				/*
				 * pgdp can't be none since kasan_early_init
				 * initialized all KASAN shadow region with
				 * kasan_early_shadow_pud: if this is still the
				 * case, that means we can try to allocate a
				 * hugepage as a replacement.
				 */
				phys_addr = memblock_phys_alloc(PGDIR_SIZE, PGDIR_SIZE);
				if (phys_addr) {
					set_pgd(pgdp, pfn_pgd(PFN_DOWN(phys_addr), PAGE_KERNEL));
					continue;
				}
			}
		}

		kasan_populate_pgd_next(pgdp, vaddr, next, early);
	} while (pgdp++, vaddr = next, vaddr != end);
}

asmlinkage void __init kasan_early_init(void)
{
	uintptr_t i;

	BUILD_BUG_ON(KASAN_SHADOW_OFFSET !=
		KASAN_SHADOW_END - (1UL << (64 - KASAN_SHADOW_SCALE_SHIFT)));

	for (i = 0; i < PTRS_PER_PTE; ++i)
		set_pte(kasan_early_shadow_pte + i,
			mk_pte(virt_to_page(kasan_early_shadow_page),
			       PAGE_KERNEL));

	for (i = 0; i < PTRS_PER_PMD; ++i)
		set_pmd(kasan_early_shadow_pmd + i,
			pfn_pmd(PFN_DOWN
				(__pa((uintptr_t)kasan_early_shadow_pte)),
				PAGE_TABLE));

	if (pgtable_l4_enabled) {
		for (i = 0; i < PTRS_PER_PUD; ++i)
			set_pud(kasan_early_shadow_pud + i,
				pfn_pud(PFN_DOWN
					(__pa(((uintptr_t)kasan_early_shadow_pmd))),
					PAGE_TABLE));
	}

	kasan_populate_pgd(early_pg_dir + pgd_index(KASAN_SHADOW_START),
			   KASAN_SHADOW_START, KASAN_SHADOW_END, true);

	local_flush_tlb_all();
}

void __init kasan_swapper_init(void)
{
	kasan_populate_pgd(pgd_offset_k(KASAN_SHADOW_START),
			   KASAN_SHADOW_START, KASAN_SHADOW_END, true);

	local_flush_tlb_all();
}

static void __init kasan_populate(void *start, void *end)
{
	unsigned long vaddr = (unsigned long)start & PAGE_MASK;
	unsigned long vend = PAGE_ALIGN((unsigned long)end);

	kasan_populate_pgd(pgd_offset_k(vaddr), vaddr, vend, false);

	local_flush_tlb_all();
	memset(start, KASAN_SHADOW_INIT, end - start);
}

static void __init kasan_shallow_populate_pud(pgd_t *pgdp,
					      unsigned long vaddr, unsigned long end,
					      bool kasan_populate)
{
	unsigned long next;
	pud_t *pudp, *base_pud;
	pmd_t *base_pmd;
	bool is_kasan_pmd;

	base_pud = (pud_t *)pgd_page_vaddr(*pgdp);
	pudp = base_pud + pud_index(vaddr);

	if (kasan_populate)
		memcpy(base_pud, (void *)kasan_early_shadow_pgd_next,
		       sizeof(pud_t) * PTRS_PER_PUD);

	do {
		next = pud_addr_end(vaddr, end);
		is_kasan_pmd = (pud_pgtable(*pudp) == lm_alias(kasan_early_shadow_pmd));

		if (is_kasan_pmd) {
			base_pmd = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
			set_pud(pudp, pfn_pud(PFN_DOWN(__pa(base_pmd)), PAGE_TABLE));
		}
	} while (pudp++, vaddr = next, vaddr != end);
}

static void __init kasan_shallow_populate_pgd(unsigned long vaddr, unsigned long end)
{
	unsigned long next;
	void *p;
	pgd_t *pgd_k = pgd_offset_k(vaddr);
	bool is_kasan_pgd_next;

	do {
		next = pgd_addr_end(vaddr, end);
		is_kasan_pgd_next = (pgd_page_vaddr(*pgd_k) ==
				     (unsigned long)lm_alias(kasan_early_shadow_pgd_next));

		if (is_kasan_pgd_next) {
			p = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
			set_pgd(pgd_k, pfn_pgd(PFN_DOWN(__pa(p)), PAGE_TABLE));
		}

		if (IS_ALIGNED(vaddr, PGDIR_SIZE) && (next - vaddr) >= PGDIR_SIZE)
			continue;

		kasan_shallow_populate_pud(pgd_k, vaddr, next, is_kasan_pgd_next);
	} while (pgd_k++, vaddr = next, vaddr != end);
}

static void __init kasan_shallow_populate(void *start, void *end)
{
	unsigned long vaddr = (unsigned long)start & PAGE_MASK;
	unsigned long vend = PAGE_ALIGN((unsigned long)end);

	kasan_shallow_populate_pgd(vaddr, vend);
	local_flush_tlb_all();
}

void __init kasan_init(void)
{
	phys_addr_t p_start, p_end;
	u64 i;

	if (IS_ENABLED(CONFIG_KASAN_VMALLOC))
		kasan_shallow_populate(
			(void *)kasan_mem_to_shadow((void *)VMALLOC_START),
			(void *)kasan_mem_to_shadow((void *)VMALLOC_END));

	/* Populate the linear mapping */
	for_each_mem_range(i, &p_start, &p_end) {
		void *start = (void *)__va(p_start);
		void *end = (void *)__va(p_end);

		if (start >= end)
			break;

		kasan_populate(kasan_mem_to_shadow(start), kasan_mem_to_shadow(end));
	}

	/* Populate kernel, BPF, modules mapping */
	kasan_populate(kasan_mem_to_shadow((const void *)MODULES_VADDR),
		       kasan_mem_to_shadow((const void *)MODULES_VADDR + SZ_2G));

	for (i = 0; i < PTRS_PER_PTE; i++)
		set_pte(&kasan_early_shadow_pte[i],
			mk_pte(virt_to_page(kasan_early_shadow_page),
			       __pgprot(_PAGE_PRESENT | _PAGE_READ |
					_PAGE_ACCESSED)));

	memset(kasan_early_shadow_page, KASAN_SHADOW_INIT, PAGE_SIZE);
	init_task.kasan_depth = 0;
}
