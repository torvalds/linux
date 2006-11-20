/* $Id: init.c,v 1.19 2004/02/21 04:42:16 kkojima Exp $
 *
 *  linux/arch/sh/mm/init.c
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *  Copyright (C) 2002, 2004  Paul Mundt
 *
 *  Based on linux/arch/i386/mm/init.c:
 *   Copyright (C) 1995  Linus Torvalds
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
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/tlb.h>
#include <asm/cacheflush.h>
#include <asm/cache.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);
pgd_t swapper_pg_dir[PTRS_PER_PGD];

/*
 * Cache of MMU context last used.
 */
unsigned long mmu_context_cache = NO_CONTEXT;

#ifdef CONFIG_MMU
/* It'd be good if these lines were in the standard header file. */
#define START_PFN	(NODE_DATA(0)->bdata->node_boot_start >> PAGE_SHIFT)
#define MAX_LOW_PFN	(NODE_DATA(0)->bdata->node_low_pfn)
#endif

void (*copy_page)(void *from, void *to);
void (*clear_page)(void *to);

void show_mem(void)
{
	int i, total = 0, reserved = 0;
	int shared = 0, cached = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (page_count(mem_map+i))
			shared += page_count(mem_map+i) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
}

static void set_pte_phys(unsigned long addr, unsigned long phys, pgprot_t prot)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = swapper_pg_dir + pgd_index(addr);
	if (pgd_none(*pgd)) {
		pgd_ERROR(*pgd);
		return;
	}

	pud = pud_offset(pgd, addr);
	if (pud_none(*pud)) {
		pmd = (pmd_t *)get_zeroed_page(GFP_ATOMIC);
		set_pud(pud, __pud(__pa(pmd) | _PAGE_TABLE));
		if (pmd != pmd_offset(pud, 0)) {
			pud_ERROR(*pud);
			return;
		}
	}

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd)) {
		pte = (pte_t *)get_zeroed_page(GFP_ATOMIC);
		set_pmd(pmd, __pmd(__pa(pte) | _PAGE_TABLE));
		if (pte != pte_offset_kernel(pmd, 0)) {
			pmd_ERROR(*pmd);
			return;
		}
	}

	pte = pte_offset_kernel(pmd, addr);
	if (!pte_none(*pte)) {
		pte_ERROR(*pte);
		return;
	}

	set_pte(pte, pfn_pte(phys >> PAGE_SHIFT, prot));

	__flush_tlb_page(get_asid(), addr);
}

/*
 * As a performance optimization, other platforms preserve the fixmap mapping
 * across a context switch, we don't presently do this, but this could be done
 * in a similar fashion as to the wired TLB interface that sh64 uses (by way
 * of the memorry mapped UTLB configuration) -- this unfortunately forces us to
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

/* References to section boundaries */

extern char _text, _etext, _edata, __bss_start, _end;
extern char __init_begin, __init_end;

/*
 * paging_init() sets up the page tables
 *
 * This routines also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = { 0, };

	/*
	 * Setup some defaults for the zone sizes.. these should be safe
	 * regardless of distcontiguous memory or MMU settings.
	 */
	zones_size[ZONE_DMA] = 0 >> PAGE_SHIFT;
	zones_size[ZONE_NORMAL] = __MEMORY_SIZE >> PAGE_SHIFT;
#ifdef CONFIG_HIGHMEM
	zones_size[ZONE_HIGHMEM] = 0 >> PAGE_SHIFT;
#endif

#ifdef CONFIG_MMU
	/*
	 * If we have an MMU, and want to be using it .. we need to adjust
	 * the zone sizes accordingly, in addition to turning it on.
	 */
	{
		unsigned long max_dma, low, start_pfn;
		pgd_t *pg_dir;
		int i;

		/* We don't need kernel mapping as hardware support that. */
		pg_dir = swapper_pg_dir;

		for (i = 0; i < PTRS_PER_PGD; i++)
			pgd_val(pg_dir[i]) = 0;

		/* Turn on the MMU */
		enable_mmu();

		/* Fixup the zone sizes */
		start_pfn = START_PFN;
		max_dma = virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;
		low = MAX_LOW_PFN;

		if (low < max_dma) {
			zones_size[ZONE_DMA] = low - start_pfn;
			zones_size[ZONE_NORMAL] = 0;
		} else {
			zones_size[ZONE_DMA] = max_dma - start_pfn;
			zones_size[ZONE_NORMAL] = low - max_dma;
		}
	}

#elif defined(CONFIG_CPU_SH3) || defined(CONFIG_CPU_SH4)
	/*
	 * If we don't have CONFIG_MMU set and the processor in question
	 * still has an MMU, care needs to be taken to make sure it doesn't
	 * stay on.. Since the boot loader could have potentially already
	 * turned it on, and we clearly don't want it, we simply turn it off.
	 *
	 * We don't need to do anything special for the zone sizes, since the
	 * default values that were already configured up above should be
	 * satisfactory.
	 */
	disable_mmu();
#endif
	NODE_DATA(0)->node_mem_map = NULL;
	free_area_init_node(0, NODE_DATA(0), zones_size, __MEMORY_START >> PAGE_SHIFT, 0);
}

static struct kcore_list kcore_mem, kcore_vmalloc;

void __init mem_init(void)
{
	extern unsigned long empty_zero_page[1024];
	int codesize, reservedpages, datasize, initsize;
	int tmp;
	extern unsigned long memory_start;

#ifdef CONFIG_MMU
	high_memory = (void *)__va(MAX_LOW_PFN * PAGE_SIZE);
#else
	extern unsigned long memory_end;

	high_memory = (void *)(memory_end & PAGE_MASK);
#endif

	max_mapnr = num_physpages = MAP_NR(high_memory) - MAP_NR(memory_start);

	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);
	__flush_wback_region(empty_zero_page, PAGE_SIZE);

	/*
	 * Setup wrappers for copy/clear_page(), these will get overridden
	 * later in the boot process if a better method is available.
	 */
#ifdef CONFIG_MMU
	copy_page = copy_page_slow;
	clear_page = clear_page_slow;
#else
	copy_page = copy_page_nommu;
	clear_page = clear_page_nommu;
#endif

	/* this will put all low memory onto the freelists */
	totalram_pages += free_all_bootmem_node(NODE_DATA(0));
	reservedpages = 0;
	for (tmp = 0; tmp < num_physpages; tmp++)
		/*
		 * Only count reserved RAM pages
		 */
		if (PageReserved(mem_map+tmp))
			reservedpages++;

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	kclist_add(&kcore_mem, __va(0), max_low_pfn << PAGE_SHIFT);
	kclist_add(&kcore_vmalloc, (void *)VMALLOC_START,
		   VMALLOC_END - VMALLOC_START);

	printk(KERN_INFO "Memory: %luk/%luk available (%dk kernel code, "
	       "%dk reserved, %dk data, %dk init)\n",
		(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
		max_mapnr << (PAGE_SHIFT-10),
		codesize >> 10,
		reservedpages << (PAGE_SHIFT-10),
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
	printk ("Freeing unused kernel memory: %dk freed\n", (&__init_end - &__init_begin) >> 10);
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
	printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
}
#endif

