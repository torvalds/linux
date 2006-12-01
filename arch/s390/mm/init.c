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

extern unsigned long __initdata zholes_size[];
/*
 * paging_init() sets up the page tables
 */

#ifndef CONFIG_64BIT
void __init paging_init(void)
{
        pgd_t * pg_dir;
        pte_t * pg_table;
        pte_t   pte;
	int     i;
        unsigned long tmp;
        unsigned long pfn = 0;
        unsigned long pgdir_k = (__pa(swapper_pg_dir) & PAGE_MASK) | _KERNSEG_TABLE;
        static const int ssm_mask = 0x04000000L;
	unsigned long ro_start_pfn, ro_end_pfn;
	unsigned long zones_size[MAX_NR_ZONES];

	ro_start_pfn = PFN_DOWN((unsigned long)&__start_rodata);
	ro_end_pfn = PFN_UP((unsigned long)&__end_rodata);

	memset(zones_size, 0, sizeof(zones_size));
	zones_size[ZONE_DMA] = max_low_pfn;
	free_area_init_node(0, &contig_page_data, zones_size,
			    __pa(PAGE_OFFSET) >> PAGE_SHIFT,
			    zholes_size);

	/* unmap whole virtual address space */
	
        pg_dir = swapper_pg_dir;

	for (i = 0; i < PTRS_PER_PGD; i++)
		pmd_clear((pmd_t *) pg_dir++);
		
	/*
	 * map whole physical memory to virtual memory (identity mapping) 
	 */

        pg_dir = swapper_pg_dir;

        while (pfn < max_low_pfn) {
                /*
                 * pg_table is physical at this point
                 */
		pg_table = (pte_t *) alloc_bootmem_pages(PAGE_SIZE);

		pmd_populate_kernel(&init_mm, (pmd_t *) pg_dir, pg_table);
                pg_dir++;

                for (tmp = 0 ; tmp < PTRS_PER_PTE ; tmp++,pg_table++) {
			if (pfn >= ro_start_pfn && pfn < ro_end_pfn)
				pte = pfn_pte(pfn, __pgprot(_PAGE_RO));
			else
				pte = pfn_pte(pfn, PAGE_KERNEL);
                        if (pfn >= max_low_pfn)
				pte_val(pte) = _PAGE_TYPE_EMPTY;
			set_pte(pg_table, pte);
                        pfn++;
                }
        }

	S390_lowcore.kernel_asce = pgdir_k;

        /* enable virtual mapping in kernel mode */
	__ctl_load(pgdir_k, 1, 1);
	__ctl_load(pgdir_k, 7, 7);
	__ctl_load(pgdir_k, 13, 13);
	__raw_local_irq_ssm(ssm_mask);

        local_flush_tlb();
}

#else /* CONFIG_64BIT */

void __init paging_init(void)
{
        pgd_t * pg_dir;
	pmd_t * pm_dir;
        pte_t * pt_dir;
        pte_t   pte;
	int     i,j,k;
        unsigned long pfn = 0;
        unsigned long pgdir_k = (__pa(swapper_pg_dir) & PAGE_MASK) |
          _KERN_REGION_TABLE;
	static const int ssm_mask = 0x04000000L;
	unsigned long zones_size[MAX_NR_ZONES];
	unsigned long dma_pfn, high_pfn;
	unsigned long ro_start_pfn, ro_end_pfn;

	memset(zones_size, 0, sizeof(zones_size));
	dma_pfn = MAX_DMA_ADDRESS >> PAGE_SHIFT;
	high_pfn = max_low_pfn;
	ro_start_pfn = PFN_DOWN((unsigned long)&__start_rodata);
	ro_end_pfn = PFN_UP((unsigned long)&__end_rodata);

	if (dma_pfn > high_pfn)
		zones_size[ZONE_DMA] = high_pfn;
	else {
		zones_size[ZONE_DMA] = dma_pfn;
		zones_size[ZONE_NORMAL] = high_pfn - dma_pfn;
	}

	/* Initialize mem_map[].  */
	free_area_init_node(0, &contig_page_data, zones_size,
			    __pa(PAGE_OFFSET) >> PAGE_SHIFT, zholes_size);

	/*
	 * map whole physical memory to virtual memory (identity mapping) 
	 */

        pg_dir = swapper_pg_dir;
	
        for (i = 0 ; i < PTRS_PER_PGD ; i++,pg_dir++) {
	
                if (pfn >= max_low_pfn) {
                        pgd_clear(pg_dir);
                        continue;
                }          
        
		pm_dir = (pmd_t *) alloc_bootmem_pages(PAGE_SIZE * 4);
                pgd_populate(&init_mm, pg_dir, pm_dir);

                for (j = 0 ; j < PTRS_PER_PMD ; j++,pm_dir++) {
                        if (pfn >= max_low_pfn) {
                                pmd_clear(pm_dir);
                                continue; 
                        }          
                        
			pt_dir = (pte_t *) alloc_bootmem_pages(PAGE_SIZE);
                        pmd_populate_kernel(&init_mm, pm_dir, pt_dir);
	
                        for (k = 0 ; k < PTRS_PER_PTE ; k++,pt_dir++) {
				if (pfn >= ro_start_pfn && pfn < ro_end_pfn)
					pte = pfn_pte(pfn, __pgprot(_PAGE_RO));
				else
					pte = pfn_pte(pfn, PAGE_KERNEL);
				if (pfn >= max_low_pfn)
					pte_val(pte) = _PAGE_TYPE_EMPTY;
                                set_pte(pt_dir, pte);
                                pfn++;
                        }
                }
        }

	S390_lowcore.kernel_asce = pgdir_k;

        /* enable virtual mapping in kernel mode */
	__ctl_load(pgdir_k, 1, 1);
	__ctl_load(pgdir_k, 7, 7);
	__ctl_load(pgdir_k, 13, 13);
	__raw_local_irq_ssm(ssm_mask);

        local_flush_tlb();
}
#endif /* CONFIG_64BIT */

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
