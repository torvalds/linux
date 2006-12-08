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

void diag10(unsigned long addr)
{
        if (addr >= 0x7ff00000)
                return;
	asm volatile(
#ifdef CONFIG_64BIT
		"	sam31\n"
		"	diag	%0,%0,0x10\n"
		"0:	sam64\n"
#else
		"	diag	%0,%0,0x10\n"
		"0:\n"
#endif
		EX_TABLE(0b,0b)
		: : "a" (addr));
}

void show_mem(void)
{
        int i, total = 0, reserved = 0;
        int shared = 0, cached = 0;
	struct page *page;

        printk("Mem-info:\n");
        show_free_areas();
        printk("Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
        i = max_mapnr;
        while (i-- > 0) {
		if (!pfn_valid(i))
			continue;
		page = pfn_to_page(i);
                total++;
		if (PageReserved(page))
                        reserved++;
		else if (PageSwapCache(page))
                        cached++;
		else if (page_count(page))
			shared += page_count(page) - 1;
        }
        printk("%d pages of RAM\n",total);
        printk("%d reserved pages\n",reserved);
        printk("%d pages shared\n",shared);
        printk("%d pages swap cached\n",cached);
}

static void __init setup_ro_region(void)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	pte_t new_pte;
	unsigned long address, end;

	address = ((unsigned long)&__start_rodata) & PAGE_MASK;
	end = PFN_ALIGN((unsigned long)&__end_rodata);

	for (; address < end; address += PAGE_SIZE) {
		pgd = pgd_offset_k(address);
		pmd = pmd_offset(pgd, address);
		pte = pte_offset_kernel(pmd, address);
		new_pte = mk_pte_phys(address, __pgprot(_PAGE_RO));
		set_pte(pte, new_pte);
	}
}

extern void vmem_map_init(void);

/*
 * paging_init() sets up the page tables
 */
void __init paging_init(void)
{
	pgd_t *pg_dir;
	int i;
	unsigned long pgdir_k;
	static const int ssm_mask = 0x04000000L;
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	pg_dir = swapper_pg_dir;
	
#ifdef CONFIG_64BIT
	pgdir_k = (__pa(swapper_pg_dir) & PAGE_MASK) | _KERN_REGION_TABLE;
	for (i = 0; i < PTRS_PER_PGD; i++)
		pgd_clear(pg_dir + i);
#else
	pgdir_k = (__pa(swapper_pg_dir) & PAGE_MASK) | _KERNSEG_TABLE;
	for (i = 0; i < PTRS_PER_PGD; i++)
		pmd_clear((pmd_t *)(pg_dir + i));
#endif
	vmem_map_init();
	setup_ro_region();

	S390_lowcore.kernel_asce = pgdir_k;

        /* enable virtual mapping in kernel mode */
	__ctl_load(pgdir_k, 1, 1);
	__ctl_load(pgdir_k, 7, 7);
	__ctl_load(pgdir_k, 13, 13);
	__raw_local_irq_ssm(ssm_mask);

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
	max_zone_pfns[ZONE_DMA] = PFN_DOWN(MAX_DMA_ADDRESS);
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
	       (unsigned long)&__start_rodata,
	       PFN_ALIGN((unsigned long)&__end_rodata) - 1);
	printk("Virtual memmap size: %ldk\n",
	       (max_pfn * sizeof(struct page)) >> 10);
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
