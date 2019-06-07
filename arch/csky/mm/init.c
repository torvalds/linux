// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/bug.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/pagemap.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/memblock.h>
#include <linux/swap.h>
#include <linux/proc_fs.h>
#include <linux/pfn.h>

#include <asm/setup.h>
#include <asm/cachectl.h>
#include <asm/dma.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/sections.h>
#include <asm/tlb.h>

pgd_t swapper_pg_dir[PTRS_PER_PGD] __page_aligned_bss;
pte_t invalid_pte_table[PTRS_PER_PTE] __page_aligned_bss;
unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)]
						__page_aligned_bss;
EXPORT_SYMBOL(empty_zero_page);

void __init mem_init(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long tmp;

	max_mapnr = highend_pfn;
#else
	max_mapnr = max_low_pfn;
#endif
	high_memory = (void *) __va(max_low_pfn << PAGE_SHIFT);

	memblock_free_all();

#ifdef CONFIG_HIGHMEM
	for (tmp = highstart_pfn; tmp < highend_pfn; tmp++) {
		struct page *page = pfn_to_page(tmp);

		/* FIXME not sure about */
		if (!memblock_is_reserved(tmp << PAGE_SHIFT))
			free_highmem_page(page);
	}
#endif
	mem_init_print_info(NULL);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (start < end)
		pr_info("Freeing initrd memory: %ldk freed\n",
			(end - start) >> 10);

	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		init_page_count(virt_to_page(start));
		free_page(start);
		totalram_pages_inc();
	}
}
#endif

extern char __init_begin[], __init_end[];

void free_initmem(void)
{
	unsigned long addr;

	addr = (unsigned long) &__init_begin;

	while (addr < (unsigned long) &__init_end) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		free_page(addr);
		totalram_pages_inc();
		addr += PAGE_SIZE;
	}

	pr_info("Freeing unused kernel memory: %dk freed\n",
	((unsigned int)&__init_end - (unsigned int)&__init_begin) >> 10);
}

void pgd_init(unsigned long *p)
{
	int i;

	for (i = 0; i < PTRS_PER_PGD; i++)
		p[i] = __pa(invalid_pte_table);
}

void __init pre_mmu_init(void)
{
	/*
	 * Setup page-table and enable TLB-hardrefill
	 */
	flush_tlb_all();
	pgd_init((unsigned long *)swapper_pg_dir);
	TLBMISS_HANDLER_SETUP_PGD(swapper_pg_dir);
	TLBMISS_HANDLER_SETUP_PGD_KERNEL(swapper_pg_dir);

	asid_cache(smp_processor_id()) = ASID_FIRST_VERSION;

	/* Setup page mask to 4k */
	write_mmu_pagemask(0);
}
