/*
 * linux/arch/sh/mm/init.c
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *  Copyright (C) 2002 - 2007  Paul Mundt
 *
 *  Based on linux/arch/i386/mm/init.c:
 *   Copyright (C) 1995  Linus Torvalds
 */
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/proc_fs.h>
#include <linux/pagemap.h>
#include <linux/percpu.h>
#include <linux/io.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>
#include <asm/cacheflush.h>
#include <asm/sections.h>
#include <asm/cache.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);
pgd_t swapper_pg_dir[PTRS_PER_PGD];
unsigned long cached_to_uncached = 0;

void show_mem(void)
{
	int total = 0, reserved = 0, free = 0;
	int shared = 0, cached = 0, slab = 0;
	pg_data_t *pgdat;

	printk("Mem-info:\n");
	show_free_areas();

	for_each_online_pgdat(pgdat) {
		unsigned long flags, i;

		pgdat_resize_lock(pgdat, &flags);
		for (i = 0; i < pgdat->node_spanned_pages; i++) {
			struct page *page = pgdat_page_nr(pgdat, i);
			total++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (PageSlab(page))
				slab++;
			else if (!page_count(page))
				free++;
			else
				shared += page_count(page) - 1;
		}
		pgdat_resize_unlock(pgdat, &flags);
	}

	printk("Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	printk("%d pages of RAM\n", total);
	printk("%d free pages\n", free);
	printk("%d reserved pages\n", reserved);
	printk("%d slab pages\n", slab);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n", cached);
	printk(KERN_INFO "Total of %ld pages in page table cache\n",
	       quicklist_total_size());
}

#ifdef CONFIG_MMU
static void set_pte_phys(unsigned long addr, unsigned long phys, pgprot_t prot)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset_k(addr);
	if (pgd_none(*pgd)) {
		pgd_ERROR(*pgd);
		return;
	}

	pud = pud_alloc(NULL, pgd, addr);
	if (unlikely(!pud)) {
		pud_ERROR(*pud);
		return;
	}

	pmd = pmd_alloc(NULL, pud, addr);
	if (unlikely(!pmd)) {
		pmd_ERROR(*pmd);
		return;
	}

	pte = pte_offset_kernel(pmd, addr);
	if (!pte_none(*pte)) {
		pte_ERROR(*pte);
		return;
	}

	set_pte(pte, pfn_pte(phys >> PAGE_SHIFT, prot));

	if (cached_to_uncached)
		flush_tlb_one(get_asid(), addr);
}

/*
 * As a performance optimization, other platforms preserve the fixmap mapping
 * across a context switch, we don't presently do this, but this could be done
 * in a similar fashion as to the wired TLB interface that sh64 uses (by way
 * of the memory mapped UTLB configuration) -- this unfortunately forces us to
 * give up a TLB entry for each mapping we want to preserve. While this may be
 * viable for a small number of fixmaps, it's not particularly useful for
 * everything and needs to be carefully evaluated. (ie, we may want this for
 * the vsyscall page).
 *
 * XXX: Perhaps add a _PAGE_WIRED flag or something similar that we can pass
 * in at __set_fixmap() time to determine the appropriate behavior to follow.
 *
 *					 -- PFM.
 */
void __set_fixmap(enum fixed_addresses idx, unsigned long phys, pgprot_t prot)
{
	unsigned long address = __fix_to_virt(idx);

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}

	set_pte_phys(address, phys, prot);
}

void __init page_table_range_init(unsigned long start, unsigned long end,
					 pgd_t *pgd_base)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	int pgd_idx;
	unsigned long vaddr;

	vaddr = start & PMD_MASK;
	end = (end + PMD_SIZE - 1) & PMD_MASK;
	pgd_idx = pgd_index(vaddr);
	pgd = pgd_base + pgd_idx;

	for ( ; (pgd_idx < PTRS_PER_PGD) && (vaddr != end); pgd++, pgd_idx++) {
		BUG_ON(pgd_none(*pgd));
		pud = pud_offset(pgd, 0);
		BUG_ON(pud_none(*pud));
		pmd = pmd_offset(pud, 0);

		if (!pmd_present(*pmd)) {
			pte_t *pte_table;
			pte_table = (pte_t *)alloc_bootmem_low_pages(PAGE_SIZE);
			memset(pte_table, 0, PAGE_SIZE);
			pmd_populate_kernel(&init_mm, pmd, pte_table);
		}

		vaddr += PMD_SIZE;
	}
}
#endif	/* CONFIG_MMU */

/*
 * paging_init() sets up the page tables
 */
void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];
	int nid;

	/* We don't need to map the kernel through the TLB, as
	 * it is permanatly mapped using P1. So clear the
	 * entire pgd. */
	memset(swapper_pg_dir, 0, sizeof(swapper_pg_dir));

	/* Set an initial value for the MMU.TTB so we don't have to
	 * check for a null value. */
	set_TTB(swapper_pg_dir);

	/* Populate the relevant portions of swapper_pg_dir so that
	 * we can use the fixmap entries without calling kmalloc.
	 * pte's will be filled in by __set_fixmap(). */
	page_table_range_init(FIXADDR_START, FIXADDR_TOP, swapper_pg_dir);

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));

	for_each_online_node(nid) {
		pg_data_t *pgdat = NODE_DATA(nid);
		unsigned long low, start_pfn;

		start_pfn = pgdat->bdata->node_boot_start >> PAGE_SHIFT;
		low = pgdat->bdata->node_low_pfn;

		if (max_zone_pfns[ZONE_NORMAL] < low)
			max_zone_pfns[ZONE_NORMAL] = low;

		printk("Node %u: start_pfn = 0x%lx, low = 0x%lx\n",
		       nid, start_pfn, low);
	}

	free_area_init_nodes(max_zone_pfns);

#ifdef CONFIG_SUPERH32
	/* Set up the uncached fixmap */
	set_fixmap_nocache(FIX_UNCACHED, __pa(&__uncached_start));

#ifdef CONFIG_29BIT
	/*
	 * Handle trivial transitions between cached and uncached
	 * segments, making use of the 1:1 mapping relationship in
	 * 512MB lowmem.
	 */
	cached_to_uncached = P2SEG - P1SEG;
#endif
#endif
}

static struct kcore_list kcore_mem, kcore_vmalloc;
int after_bootmem = 0;

void __init mem_init(void)
{
	int codesize, datasize, initsize;
	int nid;

	num_physpages = 0;
	high_memory = NULL;

	for_each_online_node(nid) {
		pg_data_t *pgdat = NODE_DATA(nid);
		unsigned long node_pages = 0;
		void *node_high_memory;

		num_physpages += pgdat->node_present_pages;

		if (pgdat->node_spanned_pages)
			node_pages = free_all_bootmem_node(pgdat);

		totalram_pages += node_pages;

		node_high_memory = (void *)__va((pgdat->node_start_pfn +
						 pgdat->node_spanned_pages) <<
						 PAGE_SHIFT);
		if (node_high_memory > high_memory)
			high_memory = node_high_memory;
	}

	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);
	__flush_wback_region(empty_zero_page, PAGE_SIZE);

	after_bootmem = 1;

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	kclist_add(&kcore_mem, __va(0), max_low_pfn << PAGE_SHIFT);
	kclist_add(&kcore_vmalloc, (void *)VMALLOC_START,
		   VMALLOC_END - VMALLOC_START);

	printk(KERN_INFO "Memory: %luk/%luk available (%dk kernel code, "
	       "%dk data, %dk init)\n",
		(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
		num_physpages << (PAGE_SHIFT-10),
		codesize >> 10,
		datasize >> 10,
		initsize >> 10);

	p3_cache_init();

	/* Initialize the vDSO */
	vsyscall_init();
}

void free_initmem(void)
{
	unsigned long addr;

	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		free_page(addr);
		totalram_pages++;
	}
	printk("Freeing unused kernel memory: %ldk freed\n",
	       ((unsigned long)&__init_end -
	        (unsigned long)&__init_begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	unsigned long p;
	for (p = start; p < end; p += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(p));
		init_page_count(virt_to_page(p));
		free_page(p);
		totalram_pages++;
	}
	printk("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
}
#endif

#ifdef CONFIG_MEMORY_HOTPLUG
int arch_add_memory(int nid, u64 start, u64 size)
{
	pg_data_t *pgdat;
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;
	int ret;

	pgdat = NODE_DATA(nid);

	/* We only have ZONE_NORMAL, so this is easy.. */
	ret = __add_pages(pgdat->node_zones + ZONE_NORMAL, start_pfn, nr_pages);
	if (unlikely(ret))
		printk("%s: Failed, __add_pages() == %d\n", __func__, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(arch_add_memory);

#ifdef CONFIG_NUMA
int memory_add_physaddr_to_nid(u64 addr)
{
	/* Node 0 for now.. */
	return 0;
}
EXPORT_SYMBOL_GPL(memory_add_physaddr_to_nid);
#endif
#endif
