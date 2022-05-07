// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/sh/mm/init.c
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *  Copyright (C) 2002 - 2011  Paul Mundt
 *
 *  Based on linux/arch/i386/mm/init.c:
 *   Copyright (C) 1995  Linus Torvalds
 */
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/memblock.h>
#include <linux/proc_fs.h>
#include <linux/pagemap.h>
#include <linux/percpu.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <asm/mmu_context.h>
#include <asm/mmzone.h>
#include <asm/kexec.h>
#include <asm/tlb.h>
#include <asm/cacheflush.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/cache.h>
#include <asm/pgalloc.h>
#include <linux/sizes.h>
#include "ioremap.h"

pgd_t swapper_pg_dir[PTRS_PER_PGD];

void __init generic_mem_init(void)
{
	memblock_add(__MEMORY_START, __MEMORY_SIZE);
}

void __init __weak plat_mem_setup(void)
{
	/* Nothing to see here, move along. */
}

#ifdef CONFIG_MMU
static pte_t *__get_pte_phys(unsigned long addr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset_k(addr);
	if (pgd_none(*pgd)) {
		pgd_ERROR(*pgd);
		return NULL;
	}

	p4d = p4d_alloc(NULL, pgd, addr);
	if (unlikely(!p4d)) {
		p4d_ERROR(*p4d);
		return NULL;
	}

	pud = pud_alloc(NULL, p4d, addr);
	if (unlikely(!pud)) {
		pud_ERROR(*pud);
		return NULL;
	}

	pmd = pmd_alloc(NULL, pud, addr);
	if (unlikely(!pmd)) {
		pmd_ERROR(*pmd);
		return NULL;
	}

	return pte_offset_kernel(pmd, addr);
}

static void set_pte_phys(unsigned long addr, unsigned long phys, pgprot_t prot)
{
	pte_t *pte;

	pte = __get_pte_phys(addr);
	if (!pte_none(*pte)) {
		pte_ERROR(*pte);
		return;
	}

	set_pte(pte, pfn_pte(phys >> PAGE_SHIFT, prot));
	local_flush_tlb_one(get_asid(), addr);

	if (pgprot_val(prot) & _PAGE_WIRED)
		tlb_wire_entry(NULL, addr, *pte);
}

static void clear_pte_phys(unsigned long addr, pgprot_t prot)
{
	pte_t *pte;

	pte = __get_pte_phys(addr);

	if (pgprot_val(prot) & _PAGE_WIRED)
		tlb_unwire_entry();

	set_pte(pte, pfn_pte(0, __pgprot(0)));
	local_flush_tlb_one(get_asid(), addr);
}

void __set_fixmap(enum fixed_addresses idx, unsigned long phys, pgprot_t prot)
{
	unsigned long address = __fix_to_virt(idx);

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}

	set_pte_phys(address, phys, prot);
}

void __clear_fixmap(enum fixed_addresses idx, pgprot_t prot)
{
	unsigned long address = __fix_to_virt(idx);

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}

	clear_pte_phys(address, prot);
}

static pmd_t * __init one_md_table_init(pud_t *pud)
{
	if (pud_none(*pud)) {
		pmd_t *pmd;

		pmd = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
		if (!pmd)
			panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
			      __func__, PAGE_SIZE, PAGE_SIZE);
		pud_populate(&init_mm, pud, pmd);
		BUG_ON(pmd != pmd_offset(pud, 0));
	}

	return pmd_offset(pud, 0);
}

static pte_t * __init one_page_table_init(pmd_t *pmd)
{
	if (pmd_none(*pmd)) {
		pte_t *pte;

		pte = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
		if (!pte)
			panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
			      __func__, PAGE_SIZE, PAGE_SIZE);
		pmd_populate_kernel(&init_mm, pmd, pte);
		BUG_ON(pte != pte_offset_kernel(pmd, 0));
	}

	return pte_offset_kernel(pmd, 0);
}

static pte_t * __init page_table_kmap_check(pte_t *pte, pmd_t *pmd,
					    unsigned long vaddr, pte_t *lastpte)
{
	return pte;
}

void __init page_table_range_init(unsigned long start, unsigned long end,
					 pgd_t *pgd_base)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;
	int i, j, k;
	unsigned long vaddr;

	vaddr = start;
	i = pgd_index(vaddr);
	j = pud_index(vaddr);
	k = pmd_index(vaddr);
	pgd = pgd_base + i;

	for ( ; (i < PTRS_PER_PGD) && (vaddr != end); pgd++, i++) {
		pud = (pud_t *)pgd;
		for ( ; (j < PTRS_PER_PUD) && (vaddr != end); pud++, j++) {
			pmd = one_md_table_init(pud);
#ifndef __PAGETABLE_PMD_FOLDED
			pmd += k;
#endif
			for (; (k < PTRS_PER_PMD) && (vaddr != end); pmd++, k++) {
				pte = page_table_kmap_check(one_page_table_init(pmd),
							    pmd, vaddr, pte);
				vaddr += PMD_SIZE;
			}
			k = 0;
		}
		j = 0;
	}
}
#endif	/* CONFIG_MMU */

void __init allocate_pgdat(unsigned int nid)
{
	unsigned long start_pfn, end_pfn;

	get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);

#ifdef CONFIG_NEED_MULTIPLE_NODES
	NODE_DATA(nid) = memblock_alloc_try_nid(
				sizeof(struct pglist_data),
				SMP_CACHE_BYTES, MEMBLOCK_LOW_LIMIT,
				MEMBLOCK_ALLOC_ACCESSIBLE, nid);
	if (!NODE_DATA(nid))
		panic("Can't allocate pgdat for node %d\n", nid);
#endif

	NODE_DATA(nid)->node_start_pfn = start_pfn;
	NODE_DATA(nid)->node_spanned_pages = end_pfn - start_pfn;
}

static void __init do_init_bootmem(void)
{
	unsigned long start_pfn, end_pfn;
	int i;

	/* Add active regions with valid PFNs. */
	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, NULL)
		__add_active_range(0, start_pfn, end_pfn);

	/* All of system RAM sits in node 0 for the non-NUMA case */
	allocate_pgdat(0);
	node_set_online(0);

	plat_mem_setup();

	sparse_init();
}

static void __init early_reserve_mem(void)
{
	unsigned long start_pfn;
	u32 zero_base = (u32)__MEMORY_START + (u32)PHYSICAL_OFFSET;
	u32 start = zero_base + (u32)CONFIG_ZERO_PAGE_OFFSET;

	/*
	 * Partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	start_pfn = PFN_UP(__pa(_end));

	/*
	 * Reserve the kernel text and Reserve the bootmem bitmap. We do
	 * this in two steps (first step was init_bootmem()), because
	 * this catches the (definitely buggy) case of us accidentally
	 * initializing the bootmem allocator with an invalid RAM area.
	 */
	memblock_reserve(start, (PFN_PHYS(start_pfn) + PAGE_SIZE - 1) - start);

	/*
	 * Reserve physical pages below CONFIG_ZERO_PAGE_OFFSET.
	 */
	if (CONFIG_ZERO_PAGE_OFFSET != 0)
		memblock_reserve(zero_base, CONFIG_ZERO_PAGE_OFFSET);

	/*
	 * Handle additional early reservations
	 */
	check_for_initrd();
	reserve_crashkernel();
}

void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];
	unsigned long vaddr, end;

	sh_mv.mv_mem_init();

	early_reserve_mem();

	/*
	 * Once the early reservations are out of the way, give the
	 * platforms a chance to kick out some memory.
	 */
	if (sh_mv.mv_mem_reserve)
		sh_mv.mv_mem_reserve();

	memblock_enforce_memory_limit(memory_limit);
	memblock_allow_resize();

	memblock_dump_all();

	/*
	 * Determine low and high memory ranges:
	 */
	max_low_pfn = max_pfn = memblock_end_of_DRAM() >> PAGE_SHIFT;
	min_low_pfn = __MEMORY_START >> PAGE_SHIFT;

	nodes_clear(node_online_map);

	memory_start = (unsigned long)__va(__MEMORY_START);
	memory_end = memory_start + (memory_limit ?: memblock_phys_mem_size());

	uncached_init();
	pmb_init();
	do_init_bootmem();
	ioremap_fixed_init();

	/* We don't need to map the kernel through the TLB, as
	 * it is permanatly mapped using P1. So clear the
	 * entire pgd. */
	memset(swapper_pg_dir, 0, sizeof(swapper_pg_dir));

	/* Set an initial value for the MMU.TTB so we don't have to
	 * check for a null value. */
	set_TTB(swapper_pg_dir);

	/*
	 * Populate the relevant portions of swapper_pg_dir so that
	 * we can use the fixmap entries without calling kmalloc.
	 * pte's will be filled in by __set_fixmap().
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	end = (FIXADDR_TOP + PMD_SIZE - 1) & PMD_MASK;
	page_table_range_init(vaddr, end, swapper_pg_dir);

	kmap_coherent_init();

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
	free_area_init(max_zone_pfns);
}

unsigned int mem_init_done = 0;

void __init mem_init(void)
{
	pg_data_t *pgdat;

	high_memory = NULL;
	for_each_online_pgdat(pgdat)
		high_memory = max_t(void *, high_memory,
				    __va(pgdat_end_pfn(pgdat) << PAGE_SHIFT));

	memblock_free_all();

	/* Set this up early, so we can take care of the zero page */
	cpu_cache_init();

	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);
	__flush_wback_region(empty_zero_page, PAGE_SIZE);

	vsyscall_init();

	mem_init_print_info(NULL);
	pr_info("virtual kernel memory layout:\n"
		"    fixmap  : 0x%08lx - 0x%08lx   (%4ld kB)\n"
#ifdef CONFIG_HIGHMEM
		"    pkmap   : 0x%08lx - 0x%08lx   (%4ld kB)\n"
#endif
		"    vmalloc : 0x%08lx - 0x%08lx   (%4ld MB)\n"
		"    lowmem  : 0x%08lx - 0x%08lx   (%4ld MB) (cached)\n"
#ifdef CONFIG_UNCACHED_MAPPING
		"            : 0x%08lx - 0x%08lx   (%4ld MB) (uncached)\n"
#endif
		"      .init : 0x%08lx - 0x%08lx   (%4ld kB)\n"
		"      .data : 0x%08lx - 0x%08lx   (%4ld kB)\n"
		"      .text : 0x%08lx - 0x%08lx   (%4ld kB)\n",
		FIXADDR_START, FIXADDR_TOP,
		(FIXADDR_TOP - FIXADDR_START) >> 10,

#ifdef CONFIG_HIGHMEM
		PKMAP_BASE, PKMAP_BASE+LAST_PKMAP*PAGE_SIZE,
		(LAST_PKMAP*PAGE_SIZE) >> 10,
#endif

		(unsigned long)VMALLOC_START, VMALLOC_END,
		(VMALLOC_END - VMALLOC_START) >> 20,

		(unsigned long)memory_start, (unsigned long)high_memory,
		((unsigned long)high_memory - (unsigned long)memory_start) >> 20,

#ifdef CONFIG_UNCACHED_MAPPING
		uncached_start, uncached_end, uncached_size >> 20,
#endif

		(unsigned long)&__init_begin, (unsigned long)&__init_end,
		((unsigned long)&__init_end -
		 (unsigned long)&__init_begin) >> 10,

		(unsigned long)&_etext, (unsigned long)&_edata,
		((unsigned long)&_edata - (unsigned long)&_etext) >> 10,

		(unsigned long)&_text, (unsigned long)&_etext,
		((unsigned long)&_etext - (unsigned long)&_text) >> 10);

	mem_init_done = 1;
}

#ifdef CONFIG_MEMORY_HOTPLUG
int arch_add_memory(int nid, u64 start, u64 size,
		    struct mhp_params *params)
{
	unsigned long start_pfn = PFN_DOWN(start);
	unsigned long nr_pages = size >> PAGE_SHIFT;
	int ret;

	if (WARN_ON_ONCE(params->pgprot.pgprot != PAGE_KERNEL.pgprot))
		return -EINVAL;

	/* We only have ZONE_NORMAL, so this is easy.. */
	ret = __add_pages(nid, start_pfn, nr_pages, params);
	if (unlikely(ret))
		printk("%s: Failed, __add_pages() == %d\n", __func__, ret);

	return ret;
}

void arch_remove_memory(int nid, u64 start, u64 size,
			struct vmem_altmap *altmap)
{
	unsigned long start_pfn = PFN_DOWN(start);
	unsigned long nr_pages = size >> PAGE_SHIFT;

	__remove_pages(start_pfn, nr_pages, altmap);
}
#endif /* CONFIG_MEMORY_HOTPLUG */
