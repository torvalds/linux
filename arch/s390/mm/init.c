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
#include <linux/memblock.h>
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
	unsigned int order;
	struct page *page;
	int i;

	/* Latest machines require a mapping granularity of 512KB */
	order = 7;

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
	if (VMALLOC_END > (1UL << 42)) {
		asce_bits = _ASCE_TYPE_REGION2 | _ASCE_TABLE_LENGTH;
		pgd_type = _REGION2_ENTRY_EMPTY;
	} else {
		asce_bits = _ASCE_TYPE_REGION3 | _ASCE_TABLE_LENGTH;
		pgd_type = _REGION3_ENTRY_EMPTY;
	}
	init_mm.context.asce = (__pa(init_mm.pgd) & PAGE_MASK) | asce_bits;
	S390_lowcore.kernel_asce = init_mm.context.asce;
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

	set_max_mapnr(max_low_pfn);
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
int arch_add_memory(int nid, u64 start, u64 size, bool for_device)
{
	unsigned long normal_end_pfn = PFN_DOWN(memblock_end_of_DRAM());
	unsigned long dma_end_pfn = PFN_DOWN(MAX_DMA_ADDRESS);
	unsigned long start_pfn = PFN_DOWN(start);
	unsigned long size_pages = PFN_DOWN(size);
	unsigned long nr_pages;
	int rc, zone_enum;

	rc = vmem_add_mapping(start, size);
	if (rc)
		return rc;

	while (size_pages > 0) {
		if (start_pfn < dma_end_pfn) {
			nr_pages = (start_pfn + size_pages > dma_end_pfn) ?
				   dma_end_pfn - start_pfn : size_pages;
			zone_enum = ZONE_DMA;
		} else if (start_pfn < normal_end_pfn) {
			nr_pages = (start_pfn + size_pages > normal_end_pfn) ?
				   normal_end_pfn - start_pfn : size_pages;
			zone_enum = ZONE_NORMAL;
		} else {
			nr_pages = size_pages;
			zone_enum = ZONE_MOVABLE;
		}
		rc = __add_pages(nid, NODE_DATA(nid)->node_zones + zone_enum,
				 start_pfn, size_pages);
		if (rc)
			break;
		start_pfn += nr_pages;
		size_pages -= nr_pages;
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
	return max_t(unsigned long, MIN_MEMORY_BLOCK_SIZE, sclp.rzm);
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
