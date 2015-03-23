/*
 *  S390 version
 *    Copyright IBM Corp. 1999
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1995  Linus Torvalds
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>
#include <linux/memory.h>
#include <linux/pfn.h>
#include <linux/poison.h>
#include <linux/initrd.h>
#include <linux/export.h>
#include <linux/gfp.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/dma.h>
#include <asm/lowcore.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/ctl_reg.h>
#include <asm/sclp.h>

pgd_t swapper_pg_dir[PTRS_PER_PGD] __attribute__((__aligned__(PAGE_SIZE)));

unsigned long empty_zero_page, zero_page_mask;
EXPORT_SYMBOL(empty_zero_page);
EXPORT_SYMBOL(zero_page_mask);

static void __init setup_zero_pages(void)
{
	struct cpuid cpu_id;
	unsigned int order;
	struct page *page;
	int i;

	get_cpu_id(&cpu_id);
	switch (cpu_id.machine) {
	case 0x9672:	/* g5 */
	case 0x2064:	/* z900 */
	case 0x2066:	/* z900 */
	case 0x2084:	/* z990 */
	case 0x2086:	/* z990 */
	case 0x2094:	/* z9-109 */
	case 0x2096:	/* z9-109 */
		order = 0;
		break;
	case 0x2097:	/* z10 */
	case 0x2098:	/* z10 */
	case 0x2817:	/* z196 */
	case 0x2818:	/* z196 */
		order = 2;
		break;
	case 0x2827:	/* zEC12 */
	case 0x2828:	/* zEC12 */
		order = 5;
		break;
	case 0x2964:	/* z13 */
	default:
		order = 7;
		break;
	}
	/* Limit number of empty zero pages for small memory sizes */
	while (order > 2 && (totalram_pages >> 10) < (1UL << order))
		order--;

	empty_zero_page = __get_free_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!empty_zero_page)
		panic("Out of memory in setup_zero_pages");

	page = virt_to_page((void *) empty_zero_page);
	split_page(page, order);
	for (i = 1 << order; i > 0; i--) {
		mark_page_reserved(page);
		page++;
	}

	zero_page_mask = ((PAGE_SIZE << order) - 1) & PAGE_MASK;
}

/*
 * paging_init() sets up the page tables
 */
void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];
	unsigned long pgd_type, asce_bits;

	init_mm.pgd = swapper_pg_dir;
#ifdef CONFIG_64BIT
	if (VMALLOC_END > (1UL << 42)) {
		asce_bits = _ASCE_TYPE_REGION2 | _ASCE_TABLE_LENGTH;
		pgd_type = _REGION2_ENTRY_EMPTY;
	} else {
		asce_bits = _ASCE_TYPE_REGION3 | _ASCE_TABLE_LENGTH;
		pgd_type = _REGION3_ENTRY_EMPTY;
	}
#else
	asce_bits = _ASCE_TABLE_LENGTH;
	pgd_type = _SEGMENT_ENTRY_EMPTY;
#endif
	S390_lowcore.kernel_asce = (__pa(init_mm.pgd) & PAGE_MASK) | asce_bits;
	clear_table((unsigned long *) init_mm.pgd, pgd_type,
		    sizeof(unsigned long)*2048);
	vmem_map_init();

        /* enable virtual mapping in kernel mode */
	__ctl_load(S390_lowcore.kernel_asce, 1, 1);
	__ctl_load(S390_lowcore.kernel_asce, 7, 7);
	__ctl_load(S390_lowcore.kernel_asce, 13, 13);
	arch_local_irq_restore(4UL << (BITS_PER_LONG - 8));

	sparse_memory_present_with_active_regions(MAX_NUMNODES);
	sparse_init();
	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
	max_zone_pfns[ZONE_DMA] = PFN_DOWN(MAX_DMA_ADDRESS);
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
	free_area_init_nodes(max_zone_pfns);
}

void __init mem_init(void)
{
	if (MACHINE_HAS_TLB_LC)
		cpumask_set_cpu(0, &init_mm.context.cpu_attach_mask);
	cpumask_set_cpu(0, mm_cpumask(&init_mm));
	atomic_set(&init_mm.context.attach_count, 1);

        max_mapnr = max_low_pfn;
        high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);

	/* Setup guest page hinting */
	cmma_init();

	/* this will put all low memory onto the freelists */
	free_all_bootmem();
	setup_zero_pages();	/* Setup zeroed pages. */

	mem_init_print_info(NULL);
	printk("Write protected kernel read-only data: %#lx - %#lx\n",
	       (unsigned long)&_stext,
	       PFN_ALIGN((unsigned long)&_eshared) - 1);
}

void free_initmem(void)
{
	free_initmem_default(POISON_FREE_INITMEM);
}

#ifdef CONFIG_BLK_DEV_INITRD
void __init free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, POISON_FREE_INITMEM,
			   "initrd");
}
#endif

#ifdef CONFIG_MEMORY_HOTPLUG
int arch_add_memory(int nid, u64 start, u64 size)
{
	unsigned long zone_start_pfn, zone_end_pfn, nr_pages;
	unsigned long start_pfn = PFN_DOWN(start);
	unsigned long size_pages = PFN_DOWN(size);
	struct zone *zone;
	int rc;

	rc = vmem_add_mapping(start, size);
	if (rc)
		return rc;
	for_each_zone(zone) {
		if (zone_idx(zone) != ZONE_MOVABLE) {
			/* Add range within existing zone limits */
			zone_start_pfn = zone->zone_start_pfn;
			zone_end_pfn = zone->zone_start_pfn +
				       zone->spanned_pages;
		} else {
			/* Add remaining range to ZONE_MOVABLE */
			zone_start_pfn = start_pfn;
			zone_end_pfn = start_pfn + size_pages;
		}
		if (start_pfn < zone_start_pfn || start_pfn >= zone_end_pfn)
			continue;
		nr_pages = (start_pfn + size_pages > zone_end_pfn) ?
			   zone_end_pfn - start_pfn : size_pages;
		rc = __add_pages(nid, zone, start_pfn, nr_pages);
		if (rc)
			break;
		start_pfn += nr_pages;
		size_pages -= nr_pages;
		if (!size_pages)
			break;
	}
	if (rc)
		vmem_remove_mapping(start, size);
	return rc;
}

unsigned long memory_block_size_bytes(void)
{
	/*
	 * Make sure the memory block size is always greater
	 * or equal than the memory increment size.
	 */
	return max_t(unsigned long, MIN_MEMORY_BLOCK_SIZE, sclp_get_rzm());
}

#ifdef CONFIG_MEMORY_HOTREMOVE
int arch_remove_memory(u64 start, u64 size)
{
	/*
	 * There is no hardware or firmware interface which could trigger a
	 * hot memory remove on s390. So there is nothing that needs to be
	 * implemented.
	 */
	return -EBUSY;
}
#endif
#endif /* CONFIG_MEMORY_HOTPLUG */
