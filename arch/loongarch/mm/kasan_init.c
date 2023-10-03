// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */
#define pr_fmt(fmt) "kasan: " fmt
#include <linux/kasan.h>
#include <linux/memblock.h>
#include <linux/sched/task.h>

#include <asm/tlbflush.h>
#include <asm/pgalloc.h>
#include <asm-generic/sections.h>

static pgd_t kasan_pg_dir[PTRS_PER_PGD] __initdata __aligned(PAGE_SIZE);

#ifdef __PAGETABLE_PUD_FOLDED
#define __p4d_none(early, p4d) (0)
#else
#define __p4d_none(early, p4d) (early ? (p4d_val(p4d) == 0) : \
(__pa(p4d_val(p4d)) == (unsigned long)__pa(kasan_early_shadow_pud)))
#endif

#ifdef __PAGETABLE_PMD_FOLDED
#define __pud_none(early, pud) (0)
#else
#define __pud_none(early, pud) (early ? (pud_val(pud) == 0) : \
(__pa(pud_val(pud)) == (unsigned long)__pa(kasan_early_shadow_pmd)))
#endif

#define __pmd_none(early, pmd) (early ? (pmd_val(pmd) == 0) : \
(__pa(pmd_val(pmd)) == (unsigned long)__pa(kasan_early_shadow_pte)))

#define __pte_none(early, pte) (early ? pte_none(pte) : \
((pte_val(pte) & _PFN_MASK) == (unsigned long)__pa(kasan_early_shadow_page)))

bool kasan_early_stage = true;

void *kasan_mem_to_shadow(const void *addr)
{
	if (!kasan_arch_is_ready()) {
		return (void *)(kasan_early_shadow_page);
	} else {
		unsigned long maddr = (unsigned long)addr;
		unsigned long xrange = (maddr >> XRANGE_SHIFT) & 0xffff;
		unsigned long offset = 0;

		maddr &= XRANGE_SHADOW_MASK;
		switch (xrange) {
		case XKPRANGE_CC_SEG:
			offset = XKPRANGE_CC_SHADOW_OFFSET;
			break;
		case XKPRANGE_UC_SEG:
			offset = XKPRANGE_UC_SHADOW_OFFSET;
			break;
		case XKVRANGE_VC_SEG:
			offset = XKVRANGE_VC_SHADOW_OFFSET;
			break;
		default:
			WARN_ON(1);
			return NULL;
		}

		return (void *)((maddr >> KASAN_SHADOW_SCALE_SHIFT) + offset);
	}
}

const void *kasan_shadow_to_mem(const void *shadow_addr)
{
	unsigned long addr = (unsigned long)shadow_addr;

	if (unlikely(addr > KASAN_SHADOW_END) ||
		unlikely(addr < KASAN_SHADOW_START)) {
		WARN_ON(1);
		return NULL;
	}

	if (addr >= XKVRANGE_VC_SHADOW_OFFSET)
		return (void *)(((addr - XKVRANGE_VC_SHADOW_OFFSET) << KASAN_SHADOW_SCALE_SHIFT) + XKVRANGE_VC_START);
	else if (addr >= XKPRANGE_UC_SHADOW_OFFSET)
		return (void *)(((addr - XKPRANGE_UC_SHADOW_OFFSET) << KASAN_SHADOW_SCALE_SHIFT) + XKPRANGE_UC_START);
	else if (addr >= XKPRANGE_CC_SHADOW_OFFSET)
		return (void *)(((addr - XKPRANGE_CC_SHADOW_OFFSET) << KASAN_SHADOW_SCALE_SHIFT) + XKPRANGE_CC_START);
	else {
		WARN_ON(1);
		return NULL;
	}
}

/*
 * Alloc memory for shadow memory page table.
 */
static phys_addr_t __init kasan_alloc_zeroed_page(int node)
{
	void *p = memblock_alloc_try_nid(PAGE_SIZE, PAGE_SIZE,
					__pa(MAX_DMA_ADDRESS), MEMBLOCK_ALLOC_ACCESSIBLE, node);
	if (!p)
		panic("%s: Failed to allocate %lu bytes align=0x%lx nid=%d from=%llx\n",
			__func__, PAGE_SIZE, PAGE_SIZE, node, __pa(MAX_DMA_ADDRESS));

	return __pa(p);
}

static pte_t *__init kasan_pte_offset(pmd_t *pmdp, unsigned long addr, int node, bool early)
{
	if (__pmd_none(early, READ_ONCE(*pmdp))) {
		phys_addr_t pte_phys = early ?
				__pa_symbol(kasan_early_shadow_pte) : kasan_alloc_zeroed_page(node);
		if (!early)
			memcpy(__va(pte_phys), kasan_early_shadow_pte, sizeof(kasan_early_shadow_pte));
		pmd_populate_kernel(NULL, pmdp, (pte_t *)__va(pte_phys));
	}

	return pte_offset_kernel(pmdp, addr);
}

static pmd_t *__init kasan_pmd_offset(pud_t *pudp, unsigned long addr, int node, bool early)
{
	if (__pud_none(early, READ_ONCE(*pudp))) {
		phys_addr_t pmd_phys = early ?
				__pa_symbol(kasan_early_shadow_pmd) : kasan_alloc_zeroed_page(node);
		if (!early)
			memcpy(__va(pmd_phys), kasan_early_shadow_pmd, sizeof(kasan_early_shadow_pmd));
		pud_populate(&init_mm, pudp, (pmd_t *)__va(pmd_phys));
	}

	return pmd_offset(pudp, addr);
}

static pud_t *__init kasan_pud_offset(p4d_t *p4dp, unsigned long addr, int node, bool early)
{
	if (__p4d_none(early, READ_ONCE(*p4dp))) {
		phys_addr_t pud_phys = early ?
			__pa_symbol(kasan_early_shadow_pud) : kasan_alloc_zeroed_page(node);
		if (!early)
			memcpy(__va(pud_phys), kasan_early_shadow_pud, sizeof(kasan_early_shadow_pud));
		p4d_populate(&init_mm, p4dp, (pud_t *)__va(pud_phys));
	}

	return pud_offset(p4dp, addr);
}

static void __init kasan_pte_populate(pmd_t *pmdp, unsigned long addr,
				      unsigned long end, int node, bool early)
{
	unsigned long next;
	pte_t *ptep = kasan_pte_offset(pmdp, addr, node, early);

	do {
		phys_addr_t page_phys = early ?
					__pa_symbol(kasan_early_shadow_page)
					      : kasan_alloc_zeroed_page(node);
		next = addr + PAGE_SIZE;
		set_pte(ptep, pfn_pte(__phys_to_pfn(page_phys), PAGE_KERNEL));
	} while (ptep++, addr = next, addr != end && __pte_none(early, READ_ONCE(*ptep)));
}

static void __init kasan_pmd_populate(pud_t *pudp, unsigned long addr,
				      unsigned long end, int node, bool early)
{
	unsigned long next;
	pmd_t *pmdp = kasan_pmd_offset(pudp, addr, node, early);

	do {
		next = pmd_addr_end(addr, end);
		kasan_pte_populate(pmdp, addr, next, node, early);
	} while (pmdp++, addr = next, addr != end && __pmd_none(early, READ_ONCE(*pmdp)));
}

static void __init kasan_pud_populate(p4d_t *p4dp, unsigned long addr,
					    unsigned long end, int node, bool early)
{
	unsigned long next;
	pud_t *pudp = kasan_pud_offset(p4dp, addr, node, early);

	do {
		next = pud_addr_end(addr, end);
		kasan_pmd_populate(pudp, addr, next, node, early);
	} while (pudp++, addr = next, addr != end);
}

static void __init kasan_p4d_populate(pgd_t *pgdp, unsigned long addr,
					    unsigned long end, int node, bool early)
{
	unsigned long next;
	p4d_t *p4dp = p4d_offset(pgdp, addr);

	do {
		next = p4d_addr_end(addr, end);
		kasan_pud_populate(p4dp, addr, next, node, early);
	} while (p4dp++, addr = next, addr != end);
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

/* Set up full kasan mappings, ensuring that the mapped pages are zeroed */
static void __init kasan_map_populate(unsigned long start, unsigned long end,
				      int node)
{
	kasan_pgd_populate(start & PAGE_MASK, PAGE_ALIGN(end), node, false);
}

asmlinkage void __init kasan_early_init(void)
{
	BUILD_BUG_ON(!IS_ALIGNED(KASAN_SHADOW_START, PGDIR_SIZE));
	BUILD_BUG_ON(!IS_ALIGNED(KASAN_SHADOW_END, PGDIR_SIZE));
}

static inline void kasan_set_pgd(pgd_t *pgdp, pgd_t pgdval)
{
	WRITE_ONCE(*pgdp, pgdval);
}

static void __init clear_pgds(unsigned long start, unsigned long end)
{
	/*
	 * Remove references to kasan page tables from
	 * swapper_pg_dir. pgd_clear() can't be used
	 * here because it's nop on 2,3-level pagetable setups
	 */
	for (; start < end; start += PGDIR_SIZE)
		kasan_set_pgd((pgd_t *)pgd_offset_k(start), __pgd(0));
}

void __init kasan_init(void)
{
	u64 i;
	phys_addr_t pa_start, pa_end;

	/*
	 * PGD was populated as invalid_pmd_table or invalid_pud_table
	 * in pagetable_init() which depends on how many levels of page
	 * table you are using, but we had to clean the gpd of kasan
	 * shadow memory, as the pgd value is none-zero.
	 * The assertion pgd_none is going to be false and the formal populate
	 * afterwards is not going to create any new pgd at all.
	 */
	memcpy(kasan_pg_dir, swapper_pg_dir, sizeof(kasan_pg_dir));
	csr_write64(__pa_symbol(kasan_pg_dir), LOONGARCH_CSR_PGDH);
	local_flush_tlb_all();

	clear_pgds(KASAN_SHADOW_START, KASAN_SHADOW_END);

	/* Maps everything to a single page of zeroes */
	kasan_pgd_populate(KASAN_SHADOW_START, KASAN_SHADOW_END, NUMA_NO_NODE, true);

	kasan_populate_early_shadow(kasan_mem_to_shadow((void *)VMALLOC_START),
					kasan_mem_to_shadow((void *)KFENCE_AREA_END));

	kasan_early_stage = false;

	/* Populate the linear mapping */
	for_each_mem_range(i, &pa_start, &pa_end) {
		void *start = (void *)phys_to_virt(pa_start);
		void *end   = (void *)phys_to_virt(pa_end);

		if (start >= end)
			break;

		kasan_map_populate((unsigned long)kasan_mem_to_shadow(start),
			(unsigned long)kasan_mem_to_shadow(end), NUMA_NO_NODE);
	}

	/* Populate modules mapping */
	kasan_map_populate((unsigned long)kasan_mem_to_shadow((void *)MODULES_VADDR),
		(unsigned long)kasan_mem_to_shadow((void *)MODULES_END), NUMA_NO_NODE);
	/*
	 * KAsan may reuse the contents of kasan_early_shadow_pte directly, so we
	 * should make sure that it maps the zero page read-only.
	 */
	for (i = 0; i < PTRS_PER_PTE; i++)
		set_pte(&kasan_early_shadow_pte[i],
			pfn_pte(__phys_to_pfn(__pa_symbol(kasan_early_shadow_page)), PAGE_KERNEL_RO));

	memset(kasan_early_shadow_page, 0, PAGE_SIZE);
	csr_write64(__pa_symbol(swapper_pg_dir), LOONGARCH_CSR_PGDH);
	local_flush_tlb_all();

	/* At this point kasan is fully initialized. Enable error messages */
	init_task.kasan_depth = 0;
	pr_info("KernelAddressSanitizer initialized.\n");
}
