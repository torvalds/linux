/*
 *  Copyright (C) 2005,2006,2007,2008,2009,2010 Imagination Technologies
 *
 */

#include <linux/export.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/pagemap.h>
#include <linux/percpu.h>
#include <linux/memblock.h>
#include <linux/initrd.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/sections.h>
#include <asm/tlb.h>
#include <asm/user_gateway.h>
#include <asm/mmzone.h>
#include <asm/fixmap.h>

unsigned long pfn_base;
EXPORT_SYMBOL(pfn_base);

pgd_t swapper_pg_dir[PTRS_PER_PGD] __page_aligned_data;

unsigned long empty_zero_page;
EXPORT_SYMBOL(empty_zero_page);

extern char __user_gateway_start;
extern char __user_gateway_end;

void *gateway_page;

/*
 * Insert the gateway page into a set of page tables, creating the
 * page tables if necessary.
 */
static void insert_gateway_page(pgd_t *pgd, unsigned long address)
{
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	BUG_ON(!pgd_present(*pgd));

	pud = pud_offset(pgd, address);
	BUG_ON(!pud_present(*pud));

	pmd = pmd_offset(pud, address);
	if (!pmd_present(*pmd)) {
		pte = alloc_bootmem_pages(PAGE_SIZE);
		set_pmd(pmd, __pmd(_PAGE_TABLE | __pa(pte)));
	}

	pte = pte_offset_kernel(pmd, address);
	set_pte(pte, pfn_pte(__pa(gateway_page) >> PAGE_SHIFT, PAGE_READONLY));
}

/* Alloc and map a page in a known location accessible to userspace. */
static void __init user_gateway_init(void)
{
	unsigned long address = USER_GATEWAY_PAGE;
	int offset = pgd_index(address);
	pgd_t *pgd;

	gateway_page = alloc_bootmem_pages(PAGE_SIZE);

	pgd = swapper_pg_dir + offset;
	insert_gateway_page(pgd, address);

#ifdef CONFIG_METAG_META12
	/*
	 * Insert the gateway page into our current page tables even
	 * though we've already inserted it into our reference page
	 * table (swapper_pg_dir). This is because with a META1 mmu we
	 * copy just the user address range and not the gateway page
	 * entry on context switch, see switch_mmu().
	 */
	pgd = (pgd_t *)mmu_get_base() + offset;
	insert_gateway_page(pgd, address);
#endif /* CONFIG_METAG_META12 */

	BUG_ON((&__user_gateway_end - &__user_gateway_start) > PAGE_SIZE);

	gateway_page += (address & ~PAGE_MASK);

	memcpy(gateway_page, &__user_gateway_start,
	       &__user_gateway_end - &__user_gateway_start);

	/*
	 * We don't need to flush the TLB here, there should be no mapping
	 * present at boot for this address and only valid mappings are in
	 * the TLB (apart from on Meta 1.x, but those cached invalid
	 * mappings should be impossible to hit here).
	 *
	 * We don't flush the code cache here even though we have written
	 * code through the data cache and they may not be coherent. At
	 * this point we assume there is no stale data in the code cache
	 * for this address so there is no need to flush.
	 */
}

static void __init allocate_pgdat(unsigned int nid)
{
	unsigned long start_pfn, end_pfn;
#ifdef CONFIG_NEED_MULTIPLE_NODES
	unsigned long phys;
#endif

	get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);

#ifdef CONFIG_NEED_MULTIPLE_NODES
	phys = __memblock_alloc_base(sizeof(struct pglist_data),
				SMP_CACHE_BYTES, end_pfn << PAGE_SHIFT);
	/* Retry with all of system memory */
	if (!phys)
		phys = __memblock_alloc_base(sizeof(struct pglist_data),
					     SMP_CACHE_BYTES,
					     memblock_end_of_DRAM());
	if (!phys)
		panic("Can't allocate pgdat for node %d\n", nid);

	NODE_DATA(nid) = __va(phys);
	memset(NODE_DATA(nid), 0, sizeof(struct pglist_data));

	NODE_DATA(nid)->bdata = &bootmem_node_data[nid];
#endif

	NODE_DATA(nid)->node_start_pfn = start_pfn;
	NODE_DATA(nid)->node_spanned_pages = end_pfn - start_pfn;
}

static void __init bootmem_init_one_node(unsigned int nid)
{
	unsigned long total_pages, paddr;
	unsigned long end_pfn;
	struct pglist_data *p;

	p = NODE_DATA(nid);

	/* Nothing to do.. */
	if (!p->node_spanned_pages)
		return;

	end_pfn = p->node_start_pfn + p->node_spanned_pages;
#ifdef CONFIG_HIGHMEM
	if (end_pfn > max_low_pfn)
		end_pfn = max_low_pfn;
#endif

	total_pages = bootmem_bootmap_pages(end_pfn - p->node_start_pfn);

	paddr = memblock_alloc(total_pages << PAGE_SHIFT, PAGE_SIZE);
	if (!paddr)
		panic("Can't allocate bootmap for nid[%d]\n", nid);

	init_bootmem_node(p, paddr >> PAGE_SHIFT, p->node_start_pfn, end_pfn);

	free_bootmem_with_active_regions(nid, end_pfn);

	/*
	 * XXX Handle initial reservations for the system memory node
	 * only for the moment, we'll refactor this later for handling
	 * reservations in other nodes.
	 */
	if (nid == 0) {
		struct memblock_region *reg;

		/* Reserve the sections we're already using. */
		for_each_memblock(reserved, reg) {
			unsigned long size = reg->size;

#ifdef CONFIG_HIGHMEM
			/* ...but not highmem */
			if (PFN_DOWN(reg->base) >= highstart_pfn)
				continue;

			if (PFN_UP(reg->base + size) > highstart_pfn)
				size = (highstart_pfn - PFN_DOWN(reg->base))
				       << PAGE_SHIFT;
#endif

			reserve_bootmem(reg->base, size, BOOTMEM_DEFAULT);
		}
	}

	sparse_memory_present_with_active_regions(nid);
}

static void __init do_init_bootmem(void)
{
	struct memblock_region *reg;
	int i;

	/* Add active regions with valid PFNs. */
	for_each_memblock(memory, reg) {
		unsigned long start_pfn, end_pfn;
		start_pfn = memblock_region_memory_base_pfn(reg);
		end_pfn = memblock_region_memory_end_pfn(reg);
		memblock_set_node(PFN_PHYS(start_pfn),
				  PFN_PHYS(end_pfn - start_pfn), 0);
	}

	/* All of system RAM sits in node 0 for the non-NUMA case */
	allocate_pgdat(0);
	node_set_online(0);

	soc_mem_setup();

	for_each_online_node(i)
		bootmem_init_one_node(i);

	sparse_init();
}

extern char _heap_start[];

static void __init init_and_reserve_mem(void)
{
	unsigned long start_pfn, heap_start;
	u64 base = min_low_pfn << PAGE_SHIFT;
	u64 size = (max_low_pfn << PAGE_SHIFT) - base;

	heap_start = (unsigned long) &_heap_start;

	memblock_add(base, size);

	/*
	 * Partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	start_pfn = PFN_UP(__pa(heap_start));

	/*
	 * Reserve the kernel text.
	 */
	memblock_reserve(base, (PFN_PHYS(start_pfn) + PAGE_SIZE - 1) - base);

#ifdef CONFIG_HIGHMEM
	/*
	 * Add & reserve highmem, so page structures are initialised.
	 */
	base = highstart_pfn << PAGE_SHIFT;
	size = (highend_pfn << PAGE_SHIFT) - base;
	if (size) {
		memblock_add(base, size);
		memblock_reserve(base, size);
	}
#endif
}

#ifdef CONFIG_HIGHMEM
/*
 * Ensure we have allocated page tables in swapper_pg_dir for the
 * fixed mappings range from 'start' to 'end'.
 */
static void __init allocate_pgtables(unsigned long start, unsigned long end)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	int i, j;
	unsigned long vaddr;

	vaddr = start;
	i = pgd_index(vaddr);
	j = pmd_index(vaddr);
	pgd = swapper_pg_dir + i;

	for ( ; (i < PTRS_PER_PGD) && (vaddr != end); pgd++, i++) {
		pmd = (pmd_t *)pgd;
		for (; (j < PTRS_PER_PMD) && (vaddr != end); pmd++, j++) {
			vaddr += PMD_SIZE;

			if (!pmd_none(*pmd))
				continue;

			pte = (pte_t *)alloc_bootmem_low_pages(PAGE_SIZE);
			pmd_populate_kernel(&init_mm, pmd, pte);
		}
		j = 0;
	}
}

static void __init fixedrange_init(void)
{
	unsigned long vaddr, end;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	/*
	 * Fixed mappings:
	 */
	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	end = (FIXADDR_TOP + PMD_SIZE - 1) & PMD_MASK;
	allocate_pgtables(vaddr, end);

	/*
	 * Permanent kmaps:
	 */
	vaddr = PKMAP_BASE;
	allocate_pgtables(vaddr, vaddr + PAGE_SIZE*LAST_PKMAP);

	pgd = swapper_pg_dir + pgd_index(vaddr);
	pud = pud_offset(pgd, vaddr);
	pmd = pmd_offset(pud, vaddr);
	pte = pte_offset_kernel(pmd, vaddr);
	pkmap_page_table = pte;
}
#endif /* CONFIG_HIGHMEM */

/*
 * paging_init() continues the virtual memory environment setup which
 * was begun by the code in arch/metag/kernel/setup.c.
 */
void __init paging_init(unsigned long mem_end)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];
	int nid;

	init_and_reserve_mem();

	memblock_allow_resize();

	memblock_dump_all();

	nodes_clear(node_online_map);

	init_new_context(&init_task, &init_mm);

	memset(swapper_pg_dir, 0, sizeof(swapper_pg_dir));

	do_init_bootmem();
	mmu_init(mem_end);

#ifdef CONFIG_HIGHMEM
	fixedrange_init();
	kmap_init();
#endif

	/* Initialize the zero page to a bootmem page, already zeroed. */
	empty_zero_page = (unsigned long)alloc_bootmem_pages(PAGE_SIZE);

	user_gateway_init();

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));

	for_each_online_node(nid) {
		pg_data_t *pgdat = NODE_DATA(nid);
		unsigned long low, start_pfn;

		start_pfn = pgdat->bdata->node_min_pfn;
		low = pgdat->bdata->node_low_pfn;

		if (max_zone_pfns[ZONE_NORMAL] < low)
			max_zone_pfns[ZONE_NORMAL] = low;

#ifdef CONFIG_HIGHMEM
		max_zone_pfns[ZONE_HIGHMEM] = highend_pfn;
#endif
		pr_info("Node %u: start_pfn = 0x%lx, low = 0x%lx\n",
			nid, start_pfn, low);
	}

	free_area_init_nodes(max_zone_pfns);
}

void __init mem_init(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long tmp;

	/*
	 * Explicitly reset zone->managed_pages because highmem pages are
	 * freed before calling free_all_bootmem();
	 */
	reset_all_zones_managed_pages();
	for (tmp = highstart_pfn; tmp < highend_pfn; tmp++)
		free_highmem_page(pfn_to_page(tmp));
#endif /* CONFIG_HIGHMEM */

	free_all_bootmem();
	mem_init_print_info(NULL);
	show_mem(0);
}

void free_initmem(void)
{
	free_initmem_default(POISON_FREE_INITMEM);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, POISON_FREE_INITMEM,
			   "initrd");
}
#endif
