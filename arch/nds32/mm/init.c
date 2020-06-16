// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 1995-2005 Russell King
// Copyright (C) 2012 ARM Ltd.
// Copyright (C) 2013-2017 Andes Technology Corporation

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/mman.h>
#include <linux/nodemask.h>
#include <linux/initrd.h>
#include <linux/highmem.h>

#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/tlb.h>
#include <asm/page.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);
DEFINE_SPINLOCK(anon_alias_lock);
extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

/*
 * empty_zero_page is a special page that is used for
 * zero-initialized data and COW.
 */
struct page *empty_zero_page;
EXPORT_SYMBOL(empty_zero_page);

static void __init zone_sizes_init(void)
{
	unsigned long max_zone_pfn[MAX_NR_ZONES] = { 0 };

	max_zone_pfn[ZONE_NORMAL] = max_low_pfn;
#ifdef CONFIG_HIGHMEM
	max_zone_pfn[ZONE_HIGHMEM] = max_pfn;
#endif
	free_area_init(max_zone_pfn);

}

/*
 * Map all physical memory under high_memory into kernel's address space.
 *
 * This is explicitly coded for two-level page tables, so if you need
 * something else then this needs to change.
 */
static void __init map_ram(void)
{
	unsigned long v, p, e;
	pgd_t *pge;
	p4d_t *p4e;
	pud_t *pue;
	pmd_t *pme;
	pte_t *pte;
	/* These mark extents of read-only kernel pages...
	 * ...from vmlinux.lds.S
	 */

	p = (u32) memblock_start_of_DRAM() & PAGE_MASK;
	e = min((u32) memblock_end_of_DRAM(), (u32) __pa(high_memory));

	v = (u32) __va(p);
	pge = pgd_offset_k(v);

	while (p < e) {
		int j;
		p4e = p4d_offset(pge, v);
		pue = pud_offset(p4e, v);
		pme = pmd_offset(pue, v);

		if ((u32) pue != (u32) pge || (u32) pme != (u32) pge) {
			panic("%s: Kernel hardcoded for "
			      "two-level page tables", __func__);
		}

		/* Alloc one page for holding PTE's... */
		pte = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
		if (!pte)
			panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
			      __func__, PAGE_SIZE, PAGE_SIZE);
		set_pmd(pme, __pmd(__pa(pte) + _PAGE_KERNEL_TABLE));

		/* Fill the newly allocated page with PTE'S */
		for (j = 0; p < e && j < PTRS_PER_PTE;
		     v += PAGE_SIZE, p += PAGE_SIZE, j++, pte++) {
			/* Create mapping between p and v. */
			/* TODO: more fine grant for page access permission */
			set_pte(pte, __pte(p + pgprot_val(PAGE_KERNEL)));
		}

		pge++;
	}
}
static pmd_t *fixmap_pmd_p;
static void __init fixedrange_init(void)
{
	unsigned long vaddr;
	pmd_t *pmd;
#ifdef CONFIG_HIGHMEM
	pte_t *pte;
#endif /* CONFIG_HIGHMEM */

	/*
	 * Fixed mappings:
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1);
	pmd = pmd_off_k(vaddr);
	fixmap_pmd_p = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
	if (!fixmap_pmd_p)
		panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE);
	set_pmd(pmd, __pmd(__pa(fixmap_pmd_p) + _PAGE_KERNEL_TABLE));

#ifdef CONFIG_HIGHMEM
	/*
	 * Permanent kmaps:
	 */
	vaddr = PKMAP_BASE;

	pmd = pmd_off_k(vaddr);
	pte = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
	if (!pte)
		panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE);
	set_pmd(pmd, __pmd(__pa(pte) + _PAGE_KERNEL_TABLE));
	pkmap_page_table = pte;
#endif /* CONFIG_HIGHMEM */
}

/*
 * paging_init() sets up the page tables, initialises the zone memory
 * maps, and sets up the zero page, bad page and bad page tables.
 */
void __init paging_init(void)
{
	int i;
	void *zero_page;

	pr_info("Setting up paging and PTEs.\n");
	/* clear out the init_mm.pgd that will contain the kernel's mappings */
	for (i = 0; i < PTRS_PER_PGD; i++)
		swapper_pg_dir[i] = __pgd(1);

	map_ram();

	fixedrange_init();

	/* allocate space for empty_zero_page */
	zero_page = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
	if (!zero_page)
		panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE);
	zone_sizes_init();

	empty_zero_page = virt_to_page(zero_page);
	flush_dcache_page(empty_zero_page);
}

static inline void __init free_highmem(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long pfn;
	for (pfn = PFN_UP(__pa(high_memory)); pfn < max_pfn; pfn++) {
		phys_addr_t paddr = (phys_addr_t) pfn << PAGE_SHIFT;
		if (!memblock_is_reserved(paddr))
			free_highmem_page(pfn_to_page(pfn));
	}
#endif
}

static void __init set_max_mapnr_init(void)
{
	max_mapnr = max_pfn;
}

/*
 * mem_init() marks the free areas in the mem_map and tells us how much
 * memory is free.  This is done after various parts of the system have
 * claimed their memory after the kernel image.
 */
void __init mem_init(void)
{
	phys_addr_t memory_start = memblock_start_of_DRAM();
	BUG_ON(!mem_map);
	set_max_mapnr_init();

	free_highmem();

	/* this will put all low memory onto the freelists */
	memblock_free_all();
	mem_init_print_info(NULL);

	pr_info("virtual kernel memory layout:\n"
		"    fixmap  : 0x%08lx - 0x%08lx   (%4ld kB)\n"
#ifdef CONFIG_HIGHMEM
		"    pkmap   : 0x%08lx - 0x%08lx   (%4ld kB)\n"
#endif
		"    consist : 0x%08lx - 0x%08lx   (%4ld MB)\n"
		"    vmalloc : 0x%08lx - 0x%08lx   (%4ld MB)\n"
		"    lowmem  : 0x%08lx - 0x%08lx   (%4ld MB)\n"
		"      .init : 0x%08lx - 0x%08lx   (%4ld kB)\n"
		"      .data : 0x%08lx - 0x%08lx   (%4ld kB)\n"
		"      .text : 0x%08lx - 0x%08lx   (%4ld kB)\n",
		FIXADDR_START, FIXADDR_TOP, (FIXADDR_TOP - FIXADDR_START) >> 10,
#ifdef CONFIG_HIGHMEM
		PKMAP_BASE, PKMAP_BASE + LAST_PKMAP * PAGE_SIZE,
		(LAST_PKMAP * PAGE_SIZE) >> 10,
#endif
		CONSISTENT_BASE, CONSISTENT_END,
		((CONSISTENT_END) - (CONSISTENT_BASE)) >> 20, VMALLOC_START,
		(unsigned long)VMALLOC_END, (VMALLOC_END - VMALLOC_START) >> 20,
		(unsigned long)__va(memory_start), (unsigned long)high_memory,
		((unsigned long)high_memory -
		 (unsigned long)__va(memory_start)) >> 20,
		(unsigned long)&__init_begin, (unsigned long)&__init_end,
		((unsigned long)&__init_end -
		 (unsigned long)&__init_begin) >> 10, (unsigned long)&_etext,
		(unsigned long)&_edata,
		((unsigned long)&_edata - (unsigned long)&_etext) >> 10,
		(unsigned long)&_text, (unsigned long)&_etext,
		((unsigned long)&_etext - (unsigned long)&_text) >> 10);

	/*
	 * Check boundaries twice: Some fundamental inconsistencies can
	 * be detected at build time already.
	 */
#ifdef CONFIG_HIGHMEM
	BUILD_BUG_ON(PKMAP_BASE + LAST_PKMAP * PAGE_SIZE > FIXADDR_START);
	BUILD_BUG_ON((CONSISTENT_END) > PKMAP_BASE);
#endif
	BUILD_BUG_ON(VMALLOC_END > CONSISTENT_BASE);
	BUILD_BUG_ON(VMALLOC_START >= VMALLOC_END);

#ifdef CONFIG_HIGHMEM
	BUG_ON(PKMAP_BASE + LAST_PKMAP * PAGE_SIZE > FIXADDR_START);
	BUG_ON(CONSISTENT_END > PKMAP_BASE);
#endif
	BUG_ON(VMALLOC_END > CONSISTENT_BASE);
	BUG_ON(VMALLOC_START >= VMALLOC_END);
	BUG_ON((unsigned long)high_memory > VMALLOC_START);

	return;
}

void __set_fixmap(enum fixed_addresses idx,
			       phys_addr_t phys, pgprot_t flags)
{
	unsigned long addr = __fix_to_virt(idx);
	pte_t *pte;

	BUG_ON(idx <= FIX_HOLE || idx >= __end_of_fixed_addresses);

	pte = (pte_t *)&fixmap_pmd_p[pte_index(addr)];

	if (pgprot_val(flags)) {
		set_pte(pte, pfn_pte(phys >> PAGE_SHIFT, flags));
	} else {
		pte_clear(&init_mm, addr, pte);
		flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
	}
}
