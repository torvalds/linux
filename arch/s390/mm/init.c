/*
 *  arch/s390/mm/init.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
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
#include <linux/pfn.h>
#include <linux/poison.h>
#include <linux/initrd.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/dma.h>
#include <asm/lowcore.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

pgd_t swapper_pg_dir[PTRS_PER_PGD] __attribute__((__aligned__(PAGE_SIZE)));

char  empty_zero_page[PAGE_SIZE] __attribute__((__aligned__(PAGE_SIZE)));
EXPORT_SYMBOL(empty_zero_page);

/*
 * paging_init() sets up the page tables
 */
void __init paging_init(void)
{
	static const int ssm_mask = 0x04000000L;
	unsigned long max_zone_pfns[MAX_NR_ZONES];
	unsigned long pgd_type;

	init_mm.pgd = swapper_pg_dir;
	S390_lowcore.kernel_asce = __pa(init_mm.pgd) & PAGE_MASK;
#ifdef CONFIG_64BIT
	/* A three level page table (4TB) is enough for the kernel space. */
	S390_lowcore.kernel_asce |= _ASCE_TYPE_REGION3 | _ASCE_TABLE_LENGTH;
	pgd_type = _REGION3_ENTRY_EMPTY;
#else
	S390_lowcore.kernel_asce |= _ASCE_TABLE_LENGTH;
	pgd_type = _SEGMENT_ENTRY_EMPTY;
#endif
	clear_table((unsigned long *) init_mm.pgd, pgd_type,
		    sizeof(unsigned long)*2048);
	vmem_map_init();

        /* enable virtual mapping in kernel mode */
	__ctl_load(S390_lowcore.kernel_asce, 1, 1);
	__ctl_load(S390_lowcore.kernel_asce, 7, 7);
	__ctl_load(S390_lowcore.kernel_asce, 13, 13);
	__raw_local_irq_ssm(ssm_mask);

	sparse_memory_present_with_active_regions(MAX_NUMNODES);
	sparse_init();
	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
#ifdef CONFIG_ZONE_DMA
	max_zone_pfns[ZONE_DMA] = PFN_DOWN(MAX_DMA_ADDRESS);
#endif
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
	free_area_init_nodes(max_zone_pfns);
}

void __init mem_init(void)
{
	unsigned long codesize, reservedpages, datasize, initsize;

        max_mapnr = num_physpages = max_low_pfn;
        high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);

        /* clear the zero-page */
        memset(empty_zero_page, 0, PAGE_SIZE);

	/* Setup guest page hinting */
	cmma_init();

	/* this will put all low memory onto the freelists */
	totalram_pages += free_all_bootmem();

	reservedpages = 0;

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;
        printk("Memory: %luk/%luk available (%ldk kernel code, %ldk reserved, %ldk data, %ldk init)\n",
                (unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
                max_mapnr << (PAGE_SHIFT-10),
                codesize >> 10,
                reservedpages << (PAGE_SHIFT-10),
                datasize >>10,
                initsize >> 10);
	printk("Write protected kernel read-only data: %#lx - %#lx\n",
	       (unsigned long)&_stext,
	       PFN_ALIGN((unsigned long)&_eshared) - 1);
}

#ifdef CONFIG_DEBUG_PAGEALLOC
void kernel_map_pages(struct page *page, int numpages, int enable)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long address;
	int i;

	for (i = 0; i < numpages; i++) {
		address = page_to_phys(page + i);
		pgd = pgd_offset_k(address);
		pud = pud_offset(pgd, address);
		pmd = pmd_offset(pud, address);
		pte = pte_offset_kernel(pmd, address);
		if (!enable) {
			ptep_invalidate(&init_mm, address, pte);
			continue;
		}
		*pte = mk_pte_phys(address, __pgprot(_PAGE_TYPE_RW));
		/* Flush cpu write queue. */
		mb();
	}
}
#endif

void free_initmem(void)
{
        unsigned long addr;

        addr = (unsigned long)(&__init_begin);
        for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		memset((void *)addr, POISON_FREE_INITMEM, PAGE_SIZE);
		free_page(addr);
		totalram_pages++;
        }
        printk ("Freeing unused kernel memory: %ldk freed\n",
		((unsigned long)&__init_end - (unsigned long)&__init_begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
        if (start < end)
                printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
        for (; start < end; start += PAGE_SIZE) {
                ClearPageReserved(virt_to_page(start));
                init_page_count(virt_to_page(start));
                free_page(start);
                totalram_pages++;
        }
}
#endif

#ifdef CONFIG_MEMORY_HOTPLUG
int arch_add_memory(int nid, u64 start, u64 size)
{
	struct pglist_data *pgdat;
	struct zone *zone;
	int rc;

	pgdat = NODE_DATA(nid);
	zone = pgdat->node_zones + ZONE_MOVABLE;
	rc = vmem_add_mapping(start, size);
	if (rc)
		return rc;
	rc = __add_pages(nid, zone, PFN_DOWN(start), PFN_DOWN(size));
	if (rc)
		vmem_remove_mapping(start, size);
	return rc;
}
#endif /* CONFIG_MEMORY_HOTPLUG */
