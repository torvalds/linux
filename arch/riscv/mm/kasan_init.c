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
 * For sv48 and sv57, the region start is aligned on PGDIR_SIZE whereas the end
 * region is not and then we have to go down to the PUD level.
 */

static pgd_t tmp_pg_dir[PTRS_PER_PGD] __page_aligned_bss;
static p4d_t tmp_p4d[PTRS_PER_P4D] __page_aligned_bss;
static pud_t tmp_pud[PTRS_PER_PUD] __page_aligned_bss;

static void __init kasan_populate_pte(pmd_t *pmd, unsigned long vaddr, unsigned long end)
{
	phys_addr_t phys_addr;
	pte_t *ptep, *p;

	if (pmd_none(pmdp_get(pmd))) {
		p = memblock_alloc(PTRS_PER_PTE * sizeof(pte_t), PAGE_SIZE);
		set_pmd(pmd, pfn_pmd(PFN_DOWN(__pa(p)), PAGE_TABLE));
	}

	ptep = pte_offset_kernel(pmd, vaddr);

	do {
		if (pte_none(ptep_get(ptep))) {
			phys_addr = memblock_phys_alloc(PAGE_SIZE, PAGE_SIZE);
			set_pte(ptep, pfn_pte(PFN_DOWN(phys_addr), PAGE_KERNEL));
			memset(__va(phys_addr), KASAN_SHADOW_INIT, PAGE_SIZE);
		}
	} while (ptep++, vaddr += PAGE_SIZE, vaddr != end);
}

static void __init kasan_populate_pmd(pud_t *pud, unsigned long vaddr, unsigned long end)
{
	phys_addr_t phys_addr;
	pmd_t *pmdp, *p;
	unsigned long next;

	if (pud_none(pudp_get(pud))) {
		p = memblock_alloc(PTRS_PER_PMD * sizeof(pmd_t), PAGE_SIZE);
		set_pud(pud, pfn_pud(PFN_DOWN(__pa(p)), PAGE_TABLE));
	}

	pmdp = pmd_offset(pud, vaddr);

	do {
		next = pmd_addr_end(vaddr, end);

		if (pmd_none(pmdp_get(pmdp)) && IS_ALIGNED(vaddr, PMD_SIZE) &&
		    (next - vaddr) >= PMD_SIZE) {
			phys_addr = memblock_phys_alloc(PMD_SIZE, PMD_SIZE);
			if (phys_addr) {
				set_pmd(pmdp, pfn_pmd(PFN_DOWN(phys_addr), PAGE_KERNEL));
				memset(__va(phys_addr), KASAN_SHADOW_INIT, PMD_SIZE);
				continue;
			}
		}

		kasan_populate_pte(pmdp, vaddr, next);
	} while (pmdp++, vaddr = next, vaddr != end);
}

static void __init kasan_populate_pud(p4d_t *p4d,
				      unsigned long vaddr, unsigned long end)
{
	phys_addr_t phys_addr;
	pud_t *pudp, *p;
	unsigned long next;

	if (p4d_none(p4dp_get(p4d))) {
		p = memblock_alloc(PTRS_PER_PUD * sizeof(pud_t), PAGE_SIZE);
		set_p4d(p4d, pfn_p4d(PFN_DOWN(__pa(p)), PAGE_TABLE));
	}

	pudp = pud_offset(p4d, vaddr);

	do {
		next = pud_addr_end(vaddr, end);

		if (pud_none(pudp_get(pudp)) && IS_ALIGNED(vaddr, PUD_SIZE) &&
		    (next - vaddr) >= PUD_SIZE) {
			phys_addr = memblock_phys_alloc(PUD_SIZE, PUD_SIZE);
			if (phys_addr) {
				set_pud(pudp, pfn_pud(PFN_DOWN(phys_addr), PAGE_KERNEL));
				memset(__va(phys_addr), KASAN_SHADOW_INIT, PUD_SIZE);
				continue;
			}
		}

		kasan_populate_pmd(pudp, vaddr, next);
	} while (pudp++, vaddr = next, vaddr != end);
}

static void __init kasan_populate_p4d(pgd_t *pgd,
				      unsigned long vaddr, unsigned long end)
{
	phys_addr_t phys_addr;
	p4d_t *p4dp, *p;
	unsigned long next;

	if (pgd_none(pgdp_get(pgd))) {
		p = memblock_alloc(PTRS_PER_P4D * sizeof(p4d_t), PAGE_SIZE);
		set_pgd(pgd, pfn_pgd(PFN_DOWN(__pa(p)), PAGE_TABLE));
	}

	p4dp = p4d_offset(pgd, vaddr);

	do {
		next = p4d_addr_end(vaddr, end);

		if (p4d_none(p4dp_get(p4dp)) && IS_ALIGNED(vaddr, P4D_SIZE) &&
		    (next - vaddr) >= P4D_SIZE) {
			phys_addr = memblock_phys_alloc(P4D_SIZE, P4D_SIZE);
			if (phys_addr) {
				set_p4d(p4dp, pfn_p4d(PFN_DOWN(phys_addr), PAGE_KERNEL));
				memset(__va(phys_addr), KASAN_SHADOW_INIT, P4D_SIZE);
				continue;
			}
		}

		kasan_populate_pud(p4dp, vaddr, next);
	} while (p4dp++, vaddr = next, vaddr != end);
}

static void __init kasan_populate_pgd(pgd_t *pgdp,
				      unsigned long vaddr, unsigned long end)
{
	phys_addr_t phys_addr;
	unsigned long next;

	do {
		next = pgd_addr_end(vaddr, end);

		if (pgd_none(pgdp_get(pgdp)) && IS_ALIGNED(vaddr, PGDIR_SIZE) &&
		    (next - vaddr) >= PGDIR_SIZE) {
			phys_addr = memblock_phys_alloc(PGDIR_SIZE, PGDIR_SIZE);
			if (phys_addr) {
				set_pgd(pgdp, pfn_pgd(PFN_DOWN(phys_addr), PAGE_KERNEL));
				memset(__va(phys_addr), KASAN_SHADOW_INIT, PGDIR_SIZE);
				continue;
			}
		}

		kasan_populate_p4d(pgdp, vaddr, next);
	} while (pgdp++, vaddr = next, vaddr != end);
}

static void __init kasan_early_clear_pud(p4d_t *p4dp,
					 unsigned long vaddr, unsigned long end)
{
	pud_t *pudp, *base_pud;
	unsigned long next;

	if (!pgtable_l4_enabled) {
		pudp = (pud_t *)p4dp;
	} else {
		base_pud = pt_ops.get_pud_virt(pfn_to_phys(_p4d_pfn(p4dp_get(p4dp))));
		pudp = base_pud + pud_index(vaddr);
	}

	do {
		next = pud_addr_end(vaddr, end);

		if (IS_ALIGNED(vaddr, PUD_SIZE) && (next - vaddr) >= PUD_SIZE) {
			pud_clear(pudp);
			continue;
		}

		BUG();
	} while (pudp++, vaddr = next, vaddr != end);
}

static void __init kasan_early_clear_p4d(pgd_t *pgdp,
					 unsigned long vaddr, unsigned long end)
{
	p4d_t *p4dp, *base_p4d;
	unsigned long next;

	if (!pgtable_l5_enabled) {
		p4dp = (p4d_t *)pgdp;
	} else {
		base_p4d = pt_ops.get_p4d_virt(pfn_to_phys(_pgd_pfn(pgdp_get(pgdp))));
		p4dp = base_p4d + p4d_index(vaddr);
	}

	do {
		next = p4d_addr_end(vaddr, end);

		if (pgtable_l4_enabled && IS_ALIGNED(vaddr, P4D_SIZE) &&
		    (next - vaddr) >= P4D_SIZE) {
			p4d_clear(p4dp);
			continue;
		}

		kasan_early_clear_pud(p4dp, vaddr, next);
	} while (p4dp++, vaddr = next, vaddr != end);
}

static void __init kasan_early_clear_pgd(pgd_t *pgdp,
					 unsigned long vaddr, unsigned long end)
{
	unsigned long next;

	do {
		next = pgd_addr_end(vaddr, end);

		if (pgtable_l5_enabled && IS_ALIGNED(vaddr, PGDIR_SIZE) &&
		    (next - vaddr) >= PGDIR_SIZE) {
			pgd_clear(pgdp);
			continue;
		}

		kasan_early_clear_p4d(pgdp, vaddr, next);
	} while (pgdp++, vaddr = next, vaddr != end);
}

static void __init kasan_early_populate_pud(p4d_t *p4dp,
					    unsigned long vaddr,
					    unsigned long end)
{
	pud_t *pudp, *base_pud;
	phys_addr_t phys_addr;
	unsigned long next;

	if (!pgtable_l4_enabled) {
		pudp = (pud_t *)p4dp;
	} else {
		base_pud = pt_ops.get_pud_virt(pfn_to_phys(_p4d_pfn(p4dp_get(p4dp))));
		pudp = base_pud + pud_index(vaddr);
	}

	do {
		next = pud_addr_end(vaddr, end);

		if (pud_none(pudp_get(pudp)) && IS_ALIGNED(vaddr, PUD_SIZE) &&
		    (next - vaddr) >= PUD_SIZE) {
			phys_addr = __pa((uintptr_t)kasan_early_shadow_pmd);
			set_pud(pudp, pfn_pud(PFN_DOWN(phys_addr), PAGE_TABLE));
			continue;
		}

		BUG();
	} while (pudp++, vaddr = next, vaddr != end);
}

static void __init kasan_early_populate_p4d(pgd_t *pgdp,
					    unsigned long vaddr,
					    unsigned long end)
{
	p4d_t *p4dp, *base_p4d;
	phys_addr_t phys_addr;
	unsigned long next;

	/*
	 * We can't use pgd_page_vaddr here as it would return a linear
	 * mapping address but it is not mapped yet, but when populating
	 * early_pg_dir, we need the physical address and when populating
	 * swapper_pg_dir, we need the kernel virtual address so use
	 * pt_ops facility.
	 * Note that this test is then completely equivalent to
	 * p4dp = p4d_offset(pgdp, vaddr)
	 */
	if (!pgtable_l5_enabled) {
		p4dp = (p4d_t *)pgdp;
	} else {
		base_p4d = pt_ops.get_p4d_virt(pfn_to_phys(_pgd_pfn(pgdp_get(pgdp))));
		p4dp = base_p4d + p4d_index(vaddr);
	}

	do {
		next = p4d_addr_end(vaddr, end);

		if (p4d_none(p4dp_get(p4dp)) && IS_ALIGNED(vaddr, P4D_SIZE) &&
		    (next - vaddr) >= P4D_SIZE) {
			phys_addr = __pa((uintptr_t)kasan_early_shadow_pud);
			set_p4d(p4dp, pfn_p4d(PFN_DOWN(phys_addr), PAGE_TABLE));
			continue;
		}

		kasan_early_populate_pud(p4dp, vaddr, next);
	} while (p4dp++, vaddr = next, vaddr != end);
}

static void __init kasan_early_populate_pgd(pgd_t *pgdp,
					    unsigned long vaddr,
					    unsigned long end)
{
	phys_addr_t phys_addr;
	unsigned long next;

	do {
		next = pgd_addr_end(vaddr, end);

		if (pgd_none(pgdp_get(pgdp)) && IS_ALIGNED(vaddr, PGDIR_SIZE) &&
		    (next - vaddr) >= PGDIR_SIZE) {
			phys_addr = __pa((uintptr_t)kasan_early_shadow_p4d);
			set_pgd(pgdp, pfn_pgd(PFN_DOWN(phys_addr), PAGE_TABLE));
			continue;
		}

		kasan_early_populate_p4d(pgdp, vaddr, next);
	} while (pgdp++, vaddr = next, vaddr != end);
}

asmlinkage void __init kasan_early_init(void)
{
	uintptr_t i;

	BUILD_BUG_ON(KASAN_SHADOW_OFFSET !=
		KASAN_SHADOW_END - (1UL << (64 - KASAN_SHADOW_SCALE_SHIFT)));

	for (i = 0; i < PTRS_PER_PTE; ++i)
		set_pte(kasan_early_shadow_pte + i,
			pfn_pte(virt_to_pfn(kasan_early_shadow_page), PAGE_KERNEL));

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

	if (pgtable_l5_enabled) {
		for (i = 0; i < PTRS_PER_P4D; ++i)
			set_p4d(kasan_early_shadow_p4d + i,
				pfn_p4d(PFN_DOWN
					(__pa(((uintptr_t)kasan_early_shadow_pud))),
					PAGE_TABLE));
	}

	kasan_early_populate_pgd(early_pg_dir + pgd_index(KASAN_SHADOW_START),
				 KASAN_SHADOW_START, KASAN_SHADOW_END);

	local_flush_tlb_all();
}

void __init kasan_swapper_init(void)
{
	kasan_early_populate_pgd(pgd_offset_k(KASAN_SHADOW_START),
				 KASAN_SHADOW_START, KASAN_SHADOW_END);

	local_flush_tlb_all();
}

static void __init kasan_populate(void *start, void *end)
{
	unsigned long vaddr = (unsigned long)start & PAGE_MASK;
	unsigned long vend = PAGE_ALIGN((unsigned long)end);

	kasan_populate_pgd(pgd_offset_k(vaddr), vaddr, vend);
}

static void __init kasan_shallow_populate_pud(p4d_t *p4d,
					      unsigned long vaddr, unsigned long end)
{
	unsigned long next;
	void *p;
	pud_t *pud_k = pud_offset(p4d, vaddr);

	do {
		next = pud_addr_end(vaddr, end);

		if (pud_none(pudp_get(pud_k))) {
			p = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
			set_pud(pud_k, pfn_pud(PFN_DOWN(__pa(p)), PAGE_TABLE));
			continue;
		}

		BUG();
	} while (pud_k++, vaddr = next, vaddr != end);
}

static void __init kasan_shallow_populate_p4d(pgd_t *pgd,
					      unsigned long vaddr, unsigned long end)
{
	unsigned long next;
	void *p;
	p4d_t *p4d_k = p4d_offset(pgd, vaddr);

	do {
		next = p4d_addr_end(vaddr, end);

		if (p4d_none(p4dp_get(p4d_k))) {
			p = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
			set_p4d(p4d_k, pfn_p4d(PFN_DOWN(__pa(p)), PAGE_TABLE));
			continue;
		}

		kasan_shallow_populate_pud(p4d_k, vaddr, end);
	} while (p4d_k++, vaddr = next, vaddr != end);
}

static void __init kasan_shallow_populate_pgd(unsigned long vaddr, unsigned long end)
{
	unsigned long next;
	void *p;
	pgd_t *pgd_k = pgd_offset_k(vaddr);

	do {
		next = pgd_addr_end(vaddr, end);

		if (pgd_none(pgdp_get(pgd_k))) {
			p = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
			set_pgd(pgd_k, pfn_pgd(PFN_DOWN(__pa(p)), PAGE_TABLE));
			continue;
		}

		kasan_shallow_populate_p4d(pgd_k, vaddr, next);
	} while (pgd_k++, vaddr = next, vaddr != end);
}

static void __init kasan_shallow_populate(void *start, void *end)
{
	unsigned long vaddr = (unsigned long)start & PAGE_MASK;
	unsigned long vend = PAGE_ALIGN((unsigned long)end);

	kasan_shallow_populate_pgd(vaddr, vend);
}

static void __init create_tmp_mapping(void)
{
	void *ptr;
	p4d_t *base_p4d;

	/*
	 * We need to clean the early mapping: this is hard to achieve "in-place",
	 * so install a temporary mapping like arm64 and x86 do.
	 */
	memcpy(tmp_pg_dir, swapper_pg_dir, sizeof(pgd_t) * PTRS_PER_PGD);

	/* Copy the last p4d since it is shared with the kernel mapping. */
	if (pgtable_l5_enabled) {
		ptr = (p4d_t *)pgd_page_vaddr(pgdp_get(pgd_offset_k(KASAN_SHADOW_END)));
		memcpy(tmp_p4d, ptr, sizeof(p4d_t) * PTRS_PER_P4D);
		set_pgd(&tmp_pg_dir[pgd_index(KASAN_SHADOW_END)],
			pfn_pgd(PFN_DOWN(__pa(tmp_p4d)), PAGE_TABLE));
		base_p4d = tmp_p4d;
	} else {
		base_p4d = (p4d_t *)tmp_pg_dir;
	}

	/* Copy the last pud since it is shared with the kernel mapping. */
	if (pgtable_l4_enabled) {
		ptr = (pud_t *)p4d_page_vaddr(p4dp_get(base_p4d + p4d_index(KASAN_SHADOW_END)));
		memcpy(tmp_pud, ptr, sizeof(pud_t) * PTRS_PER_PUD);
		set_p4d(&base_p4d[p4d_index(KASAN_SHADOW_END)],
			pfn_p4d(PFN_DOWN(__pa(tmp_pud)), PAGE_TABLE));
	}
}

void __init kasan_init(void)
{
	phys_addr_t p_start, p_end;
	u64 i;

	create_tmp_mapping();
	csr_write(CSR_SATP, PFN_DOWN(__pa(tmp_pg_dir)) | satp_mode);

	kasan_early_clear_pgd(pgd_offset_k(KASAN_SHADOW_START),
			      KASAN_SHADOW_START, KASAN_SHADOW_END);

	kasan_populate_early_shadow((void *)kasan_mem_to_shadow((void *)FIXADDR_START),
				    (void *)kasan_mem_to_shadow((void *)VMALLOC_START));

	if (IS_ENABLED(CONFIG_KASAN_VMALLOC)) {
		kasan_shallow_populate(
			(void *)kasan_mem_to_shadow((void *)VMALLOC_START),
			(void *)kasan_mem_to_shadow((void *)VMALLOC_END));
		/* Shallow populate modules and BPF which are vmalloc-allocated */
		kasan_shallow_populate(
			(void *)kasan_mem_to_shadow((void *)MODULES_VADDR),
			(void *)kasan_mem_to_shadow((void *)MODULES_END));
	} else {
		kasan_populate_early_shadow((void *)kasan_mem_to_shadow((void *)VMALLOC_START),
					    (void *)kasan_mem_to_shadow((void *)VMALLOC_END));
	}

	/* Populate the linear mapping */
	for_each_mem_range(i, &p_start, &p_end) {
		void *start = (void *)__va(p_start);
		void *end = (void *)__va(p_end);

		if (start >= end)
			break;

		kasan_populate(kasan_mem_to_shadow(start), kasan_mem_to_shadow(end));
	}

	/* Populate kernel */
	kasan_populate(kasan_mem_to_shadow((const void *)MODULES_END),
		       kasan_mem_to_shadow((const void *)MODULES_VADDR + SZ_2G));

	for (i = 0; i < PTRS_PER_PTE; i++)
		set_pte(&kasan_early_shadow_pte[i],
			mk_pte(virt_to_page(kasan_early_shadow_page),
			       __pgprot(_PAGE_PRESENT | _PAGE_READ |
					_PAGE_ACCESSED)));

	memset(kasan_early_shadow_page, KASAN_SHADOW_INIT, PAGE_SIZE);
	init_task.kasan_depth = 0;

	csr_write(CSR_SATP, PFN_DOWN(__pa(swapper_pg_dir)) | satp_mode);
	local_flush_tlb_all();
}
