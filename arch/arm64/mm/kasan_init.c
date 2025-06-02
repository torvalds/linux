// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file contains kasan initialization code for ARM64.
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 */

#define pr_fmt(fmt) "kasan: " fmt
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/sched/task.h>
#include <linux/memblock.h>
#include <linux/start_kernel.h>
#include <linux/mm.h>

#include <asm/mmu_context.h>
#include <asm/kernel-pgtable.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)

static pgd_t tmp_pg_dir[PTRS_PER_PTE] __initdata __aligned(PAGE_SIZE);

/*
 * The p*d_populate functions call virt_to_phys implicitly so they can't be used
 * directly on kernel symbols (bm_p*d). All the early functions are called too
 * early to use lm_alias so __p*d_populate functions must be used to populate
 * with the physical address from __pa_symbol.
 */

static phys_addr_t __init kasan_alloc_zeroed_page(int node)
{
	void *p = memblock_alloc_try_nid(PAGE_SIZE, PAGE_SIZE,
					      __pa(MAX_DMA_ADDRESS),
					      MEMBLOCK_ALLOC_NOLEAKTRACE, node);
	if (!p)
		panic("%s: Failed to allocate %lu bytes align=0x%lx nid=%d from=%llx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE, node,
		      __pa(MAX_DMA_ADDRESS));

	return __pa(p);
}

static phys_addr_t __init kasan_alloc_raw_page(int node)
{
	void *p = memblock_alloc_try_nid_raw(PAGE_SIZE, PAGE_SIZE,
						__pa(MAX_DMA_ADDRESS),
						MEMBLOCK_ALLOC_NOLEAKTRACE,
						node);
	if (!p)
		panic("%s: Failed to allocate %lu bytes align=0x%lx nid=%d from=%llx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE, node,
		      __pa(MAX_DMA_ADDRESS));

	return __pa(p);
}

static pte_t *__init kasan_pte_offset(pmd_t *pmdp, unsigned long addr, int node,
				      bool early)
{
	if (pmd_none(READ_ONCE(*pmdp))) {
		phys_addr_t pte_phys = early ?
				__pa_symbol(kasan_early_shadow_pte)
					: kasan_alloc_zeroed_page(node);
		__pmd_populate(pmdp, pte_phys, PMD_TYPE_TABLE);
	}

	return early ? pte_offset_kimg(pmdp, addr)
		     : pte_offset_kernel(pmdp, addr);
}

static pmd_t *__init kasan_pmd_offset(pud_t *pudp, unsigned long addr, int node,
				      bool early)
{
	if (pud_none(READ_ONCE(*pudp))) {
		phys_addr_t pmd_phys = early ?
				__pa_symbol(kasan_early_shadow_pmd)
					: kasan_alloc_zeroed_page(node);
		__pud_populate(pudp, pmd_phys, PUD_TYPE_TABLE);
	}

	return early ? pmd_offset_kimg(pudp, addr) : pmd_offset(pudp, addr);
}

static pud_t *__init kasan_pud_offset(p4d_t *p4dp, unsigned long addr, int node,
				      bool early)
{
	if (p4d_none(READ_ONCE(*p4dp))) {
		phys_addr_t pud_phys = early ?
				__pa_symbol(kasan_early_shadow_pud)
					: kasan_alloc_zeroed_page(node);
		__p4d_populate(p4dp, pud_phys, P4D_TYPE_TABLE);
	}

	return early ? pud_offset_kimg(p4dp, addr) : pud_offset(p4dp, addr);
}

static p4d_t *__init kasan_p4d_offset(pgd_t *pgdp, unsigned long addr, int node,
				      bool early)
{
	if (pgd_none(READ_ONCE(*pgdp))) {
		phys_addr_t p4d_phys = early ?
				__pa_symbol(kasan_early_shadow_p4d)
					: kasan_alloc_zeroed_page(node);
		__pgd_populate(pgdp, p4d_phys, PGD_TYPE_TABLE);
	}

	return early ? p4d_offset_kimg(pgdp, addr) : p4d_offset(pgdp, addr);
}

static void __init kasan_pte_populate(pmd_t *pmdp, unsigned long addr,
				      unsigned long end, int node, bool early)
{
	unsigned long next;
	pte_t *ptep = kasan_pte_offset(pmdp, addr, node, early);

	do {
		phys_addr_t page_phys = early ?
				__pa_symbol(kasan_early_shadow_page)
					: kasan_alloc_raw_page(node);
		if (!early)
			memset(__va(page_phys), KASAN_SHADOW_INIT, PAGE_SIZE);
		next = addr + PAGE_SIZE;
		__set_pte(ptep, pfn_pte(__phys_to_pfn(page_phys), PAGE_KERNEL));
	} while (ptep++, addr = next, addr != end && pte_none(__ptep_get(ptep)));
}

static void __init kasan_pmd_populate(pud_t *pudp, unsigned long addr,
				      unsigned long end, int node, bool early)
{
	unsigned long next;
	pmd_t *pmdp = kasan_pmd_offset(pudp, addr, node, early);

	do {
		next = pmd_addr_end(addr, end);
		kasan_pte_populate(pmdp, addr, next, node, early);
	} while (pmdp++, addr = next, addr != end && pmd_none(READ_ONCE(*pmdp)));
}

static void __init kasan_pud_populate(p4d_t *p4dp, unsigned long addr,
				      unsigned long end, int node, bool early)
{
	unsigned long next;
	pud_t *pudp = kasan_pud_offset(p4dp, addr, node, early);

	do {
		next = pud_addr_end(addr, end);
		kasan_pmd_populate(pudp, addr, next, node, early);
	} while (pudp++, addr = next, addr != end && pud_none(READ_ONCE(*pudp)));
}

static void __init kasan_p4d_populate(pgd_t *pgdp, unsigned long addr,
				      unsigned long end, int node, bool early)
{
	unsigned long next;
	p4d_t *p4dp = kasan_p4d_offset(pgdp, addr, node, early);

	do {
		next = p4d_addr_end(addr, end);
		kasan_pud_populate(p4dp, addr, next, node, early);
	} while (p4dp++, addr = next, addr != end && p4d_none(READ_ONCE(*p4dp)));
}

static void __init kasan_pgd_populate(unsigned long addr, unsigned long end,
				      int node, bool early)
{
	unsigned long next;
	pgd_t *pgdp;

	pgdp = pgd_offset_k(addr);
	do {
		next = pgd_addr_end(addr, end);
		kasan_p4d_populate(pgdp, addr, next, node, early);
	} while (pgdp++, addr = next, addr != end);
}

#if defined(CONFIG_ARM64_64K_PAGES) || CONFIG_PGTABLE_LEVELS > 4
#define SHADOW_ALIGN	P4D_SIZE
#else
#define SHADOW_ALIGN	PUD_SIZE
#endif

/*
 * Return whether 'addr' is aligned to the size covered by a root level
 * descriptor.
 */
static bool __init root_level_aligned(u64 addr)
{
	int shift = (ARM64_HW_PGTABLE_LEVELS(vabits_actual) - 1) * PTDESC_TABLE_SHIFT;

	return (addr % (PAGE_SIZE << shift)) == 0;
}

/* The early shadow maps everything to a single page of zeroes */
asmlinkage void __init kasan_early_init(void)
{
	BUILD_BUG_ON(KASAN_SHADOW_OFFSET !=
		KASAN_SHADOW_END - (1UL << (64 - KASAN_SHADOW_SCALE_SHIFT)));
	BUILD_BUG_ON(!IS_ALIGNED(_KASAN_SHADOW_START(VA_BITS), SHADOW_ALIGN));
	BUILD_BUG_ON(!IS_ALIGNED(_KASAN_SHADOW_START(VA_BITS_MIN), SHADOW_ALIGN));
	BUILD_BUG_ON(!IS_ALIGNED(KASAN_SHADOW_END, SHADOW_ALIGN));

	if (!root_level_aligned(KASAN_SHADOW_START)) {
		/*
		 * The start address is misaligned, and so the next level table
		 * will be shared with the linear region. This can happen with
		 * 4 or 5 level paging, so install a generic pte_t[] as the
		 * next level. This prevents the kasan_pgd_populate call below
		 * from inserting an entry that refers to the shared KASAN zero
		 * shadow pud_t[]/p4d_t[], which could end up getting corrupted
		 * when the linear region is mapped.
		 */
		static pte_t tbl[PTRS_PER_PTE] __page_aligned_bss;
		pgd_t *pgdp = pgd_offset_k(KASAN_SHADOW_START);

		set_pgd(pgdp, __pgd(__pa_symbol(tbl) | PGD_TYPE_TABLE));
	}

	kasan_pgd_populate(KASAN_SHADOW_START, KASAN_SHADOW_END, NUMA_NO_NODE,
			   true);
}

/* Set up full kasan mappings, ensuring that the mapped pages are zeroed */
static void __init kasan_map_populate(unsigned long start, unsigned long end,
				      int node)
{
	kasan_pgd_populate(start & PAGE_MASK, PAGE_ALIGN(end), node, false);
}

/*
 * Return the descriptor index of 'addr' in the root level table
 */
static int __init root_level_idx(u64 addr)
{
	/*
	 * On 64k pages, the TTBR1 range root tables are extended for 52-bit
	 * virtual addressing, and TTBR1 will simply point to the pgd_t entry
	 * that covers the start of the 48-bit addressable VA space if LVA is
	 * not implemented. This means we need to index the table as usual,
	 * instead of masking off bits based on vabits_actual.
	 */
	u64 vabits = IS_ENABLED(CONFIG_ARM64_64K_PAGES) ? VA_BITS
							: vabits_actual;
	int shift = (ARM64_HW_PGTABLE_LEVELS(vabits) - 1) * PTDESC_TABLE_SHIFT;

	return (addr & ~_PAGE_OFFSET(vabits)) >> (shift + PAGE_SHIFT);
}

/*
 * Clone a next level table from swapper_pg_dir into tmp_pg_dir
 */
static void __init clone_next_level(u64 addr, pgd_t *tmp_pg_dir, pud_t *pud)
{
	int idx = root_level_idx(addr);
	pgd_t pgd = READ_ONCE(swapper_pg_dir[idx]);
	pud_t *pudp = (pud_t *)__phys_to_kimg(__pgd_to_phys(pgd));

	memcpy(pud, pudp, PAGE_SIZE);
	tmp_pg_dir[idx] = __pgd(__phys_to_pgd_val(__pa_symbol(pud)) |
				PUD_TYPE_TABLE);
}

/*
 * Return the descriptor index of 'addr' in the next level table
 */
static int __init next_level_idx(u64 addr)
{
	int shift = (ARM64_HW_PGTABLE_LEVELS(vabits_actual) - 2) * PTDESC_TABLE_SHIFT;

	return (addr >> (shift + PAGE_SHIFT)) % PTRS_PER_PTE;
}

/*
 * Dereference the table descriptor at 'pgd_idx' and clear the entries from
 * 'start' to 'end' (exclusive) from the table.
 */
static void __init clear_next_level(int pgd_idx, int start, int end)
{
	pgd_t pgd = READ_ONCE(swapper_pg_dir[pgd_idx]);
	pud_t *pudp = (pud_t *)__phys_to_kimg(__pgd_to_phys(pgd));

	memset(&pudp[start], 0, (end - start) * sizeof(pud_t));
}

static void __init clear_shadow(u64 start, u64 end)
{
	int l = root_level_idx(start), m = root_level_idx(end);

	if (!root_level_aligned(start))
		clear_next_level(l++, next_level_idx(start), PTRS_PER_PTE);
	if (!root_level_aligned(end))
		clear_next_level(m, 0, next_level_idx(end));
	memset(&swapper_pg_dir[l], 0, (m - l) * sizeof(pgd_t));
}

static void __init kasan_init_shadow(void)
{
	static pud_t pud[2][PTRS_PER_PUD] __initdata __aligned(PAGE_SIZE);
	u64 kimg_shadow_start, kimg_shadow_end;
	u64 mod_shadow_start;
	u64 vmalloc_shadow_end;
	phys_addr_t pa_start, pa_end;
	u64 i;

	kimg_shadow_start = (u64)kasan_mem_to_shadow(KERNEL_START) & PAGE_MASK;
	kimg_shadow_end = PAGE_ALIGN((u64)kasan_mem_to_shadow(KERNEL_END));

	mod_shadow_start = (u64)kasan_mem_to_shadow((void *)MODULES_VADDR);

	vmalloc_shadow_end = (u64)kasan_mem_to_shadow((void *)VMALLOC_END);

	/*
	 * We are going to perform proper setup of shadow memory.
	 * At first we should unmap early shadow (clear_pgds() call below).
	 * However, instrumented code couldn't execute without shadow memory.
	 * tmp_pg_dir used to keep early shadow mapped until full shadow
	 * setup will be finished.
	 */
	memcpy(tmp_pg_dir, swapper_pg_dir, sizeof(tmp_pg_dir));

	/*
	 * If the start or end address of the shadow region is not aligned to
	 * the root level size, we have to allocate a temporary next-level table
	 * in each case, clone the next level of descriptors, and install the
	 * table into tmp_pg_dir. Note that with 5 levels of paging, the next
	 * level will in fact be p4d_t, but that makes no difference in this
	 * case.
	 */
	if (!root_level_aligned(KASAN_SHADOW_START))
		clone_next_level(KASAN_SHADOW_START, tmp_pg_dir, pud[0]);
	if (!root_level_aligned(KASAN_SHADOW_END))
		clone_next_level(KASAN_SHADOW_END, tmp_pg_dir, pud[1]);
	dsb(ishst);
	cpu_replace_ttbr1(lm_alias(tmp_pg_dir));

	clear_shadow(KASAN_SHADOW_START, KASAN_SHADOW_END);

	kasan_map_populate(kimg_shadow_start, kimg_shadow_end,
			   early_pfn_to_nid(virt_to_pfn(lm_alias(KERNEL_START))));

	kasan_populate_early_shadow(kasan_mem_to_shadow((void *)PAGE_END),
				   (void *)mod_shadow_start);

	BUILD_BUG_ON(VMALLOC_START != MODULES_END);
	kasan_populate_early_shadow((void *)vmalloc_shadow_end,
				    (void *)KASAN_SHADOW_END);

	for_each_mem_range(i, &pa_start, &pa_end) {
		void *start = (void *)__phys_to_virt(pa_start);
		void *end = (void *)__phys_to_virt(pa_end);

		if (start >= end)
			break;

		kasan_map_populate((unsigned long)kasan_mem_to_shadow(start),
				   (unsigned long)kasan_mem_to_shadow(end),
				   early_pfn_to_nid(virt_to_pfn(start)));
	}

	/*
	 * KAsan may reuse the contents of kasan_early_shadow_pte directly,
	 * so we should make sure that it maps the zero page read-only.
	 */
	for (i = 0; i < PTRS_PER_PTE; i++)
		__set_pte(&kasan_early_shadow_pte[i],
			pfn_pte(sym_to_pfn(kasan_early_shadow_page),
				PAGE_KERNEL_RO));

	memset(kasan_early_shadow_page, KASAN_SHADOW_INIT, PAGE_SIZE);
	cpu_replace_ttbr1(lm_alias(swapper_pg_dir));
}

static void __init kasan_init_depth(void)
{
	init_task.kasan_depth = 0;
}

#ifdef CONFIG_KASAN_VMALLOC
void __init kasan_populate_early_vm_area_shadow(void *start, unsigned long size)
{
	unsigned long shadow_start, shadow_end;

	if (!is_vmalloc_or_module_addr(start))
		return;

	shadow_start = (unsigned long)kasan_mem_to_shadow(start);
	shadow_start = ALIGN_DOWN(shadow_start, PAGE_SIZE);
	shadow_end = (unsigned long)kasan_mem_to_shadow(start + size);
	shadow_end = ALIGN(shadow_end, PAGE_SIZE);
	kasan_map_populate(shadow_start, shadow_end, NUMA_NO_NODE);
}
#endif

void __init kasan_init(void)
{
	kasan_init_shadow();
	kasan_init_depth();
#if defined(CONFIG_KASAN_GENERIC)
	/*
	 * Generic KASAN is now fully initialized.
	 * Software and Hardware Tag-Based modes still require
	 * kasan_init_sw_tags() and kasan_init_hw_tags() correspondingly.
	 */
	pr_info("KernelAddressSanitizer initialized (generic)\n");
#endif
}

#endif /* CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS */
