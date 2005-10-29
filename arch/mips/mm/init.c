/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2000 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
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
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/swap.h>

#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/cpu.h>
#include <asm/dma.h>
#include <asm/mmu_context.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

unsigned long highstart_pfn, highend_pfn;

/*
 * We have up to 8 empty zeroed pages so we can map one of the right colour
 * when needed.  This is necessary only on R4000 / R4400 SC and MC versions
 * where we have to avoid VCED / VECI exceptions for good performance at
 * any price.  Since page is never written to after the initialization we
 * don't have to care about aliases on other CPUs.
 */
unsigned long empty_zero_page, zero_page_mask;

/*
 * Not static inline because used by IP27 special magic initialization code
 */
unsigned long setup_zero_pages(void)
{
	unsigned long order, size;
	struct page *page;

	if (cpu_has_vce)
		order = 3;
	else
		order = 0;

	empty_zero_page = __get_free_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!empty_zero_page)
		panic("Oh boy, that early out of memory?");

	page = virt_to_page(empty_zero_page);
	while (page < virt_to_page(empty_zero_page + (PAGE_SIZE << order))) {
		set_bit(PG_reserved, &page->flags);
		set_page_count(page, 0);
		page++;
	}

	size = PAGE_SIZE << order;
	zero_page_mask = (size - 1) & PAGE_MASK;

	return 1UL << order;
}

#ifdef CONFIG_HIGHMEM
pte_t *kmap_pte;
pgprot_t kmap_prot;

#define kmap_get_fixmap_pte(vaddr)					\
	pte_offset_kernel(pmd_offset(pud_offset(pgd_offset_k(vaddr), (vaddr)), (vaddr)), (vaddr))

static void __init kmap_init(void)
{
	unsigned long kmap_vstart;

	/* cache the first kmap pte */
	kmap_vstart = __fix_to_virt(FIX_KMAP_BEGIN);
	kmap_pte = kmap_get_fixmap_pte(kmap_vstart);

	kmap_prot = PAGE_KERNEL;
}

#ifdef CONFIG_32BIT
void __init fixrange_init(unsigned long start, unsigned long end,
	pgd_t *pgd_base)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	int i, j, k;
	unsigned long vaddr;

	vaddr = start;
	i = __pgd_offset(vaddr);
	j = __pud_offset(vaddr);
	k = __pmd_offset(vaddr);
	pgd = pgd_base + i;

	for ( ; (i < PTRS_PER_PGD) && (vaddr != end); pgd++, i++) {
		pud = (pud_t *)pgd;
		for ( ; (j < PTRS_PER_PUD) && (vaddr != end); pud++, j++) {
			pmd = (pmd_t *)pud;
			for (; (k < PTRS_PER_PMD) && (vaddr != end); pmd++, k++) {
				if (pmd_none(*pmd)) {
					pte = (pte_t *) alloc_bootmem_low_pages(PAGE_SIZE);
					set_pmd(pmd, __pmd(pte));
					if (pte != pte_offset_kernel(pmd, 0))
						BUG();
				}
				vaddr += PMD_SIZE;
			}
			k = 0;
		}
		j = 0;
	}
}
#endif /* CONFIG_32BIT */
#endif /* CONFIG_HIGHMEM */

#ifndef CONFIG_NEED_MULTIPLE_NODES
extern void pagetable_init(void);

void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = {0, 0, 0};
	unsigned long max_dma, high, low;

	pagetable_init();

#ifdef CONFIG_HIGHMEM
	kmap_init();
#endif

	max_dma = virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;
	low = max_low_pfn;
	high = highend_pfn;

#ifdef CONFIG_ISA
	if (low < max_dma)
		zones_size[ZONE_DMA] = low;
	else {
		zones_size[ZONE_DMA] = max_dma;
		zones_size[ZONE_NORMAL] = low - max_dma;
	}
#else
	zones_size[ZONE_DMA] = low;
#endif
#ifdef CONFIG_HIGHMEM
	if (cpu_has_dc_aliases) {
		printk(KERN_WARNING "This processor doesn't support highmem.");
		if (high - low)
			printk(" %ldk highmem ignored", high - low);
		printk("\n");
	} else
		zones_size[ZONE_HIGHMEM] = high - low;
#endif

	free_area_init(zones_size);
}

#define PFN_UP(x)	(((x) + PAGE_SIZE - 1) >> PAGE_SHIFT)
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)

static inline int page_is_ram(unsigned long pagenr)
{
	int i;

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long addr, end;

		if (boot_mem_map.map[i].type != BOOT_MEM_RAM)
			/* not usable memory */
			continue;

		addr = PFN_UP(boot_mem_map.map[i].addr);
		end = PFN_DOWN(boot_mem_map.map[i].addr +
			       boot_mem_map.map[i].size);

		if (pagenr >= addr && pagenr < end)
			return 1;
	}

	return 0;
}

void __init mem_init(void)
{
	unsigned long codesize, reservedpages, datasize, initsize;
	unsigned long tmp, ram;

#ifdef CONFIG_HIGHMEM
#ifdef CONFIG_DISCONTIGMEM
#error "CONFIG_HIGHMEM and CONFIG_DISCONTIGMEM dont work together yet"
#endif
	max_mapnr = num_physpages = highend_pfn;
#else
	max_mapnr = num_physpages = max_low_pfn;
#endif
	high_memory = (void *) __va(max_low_pfn << PAGE_SHIFT);

	totalram_pages += free_all_bootmem();
	totalram_pages -= setup_zero_pages();	/* Setup zeroed pages.  */

	reservedpages = ram = 0;
	for (tmp = 0; tmp < max_low_pfn; tmp++)
		if (page_is_ram(tmp)) {
			ram++;
			if (PageReserved(mem_map+tmp))
				reservedpages++;
		}

#ifdef CONFIG_HIGHMEM
	for (tmp = highstart_pfn; tmp < highend_pfn; tmp++) {
		struct page *page = mem_map + tmp;

		if (!page_is_ram(tmp)) {
			SetPageReserved(page);
			continue;
		}
		ClearPageReserved(page);
#ifdef CONFIG_LIMITED_DMA
		set_page_address(page, lowmem_page_address(page));
#endif
		set_page_count(page, 1);
		__free_page(page);
		totalhigh_pages++;
	}
	totalram_pages += totalhigh_pages;
#endif

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	printk(KERN_INFO "Memory: %luk/%luk available (%ldk kernel code, "
	       "%ldk reserved, %ldk data, %ldk init, %ldk highmem)\n",
	       (unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
	       ram << (PAGE_SHIFT-10),
	       codesize >> 10,
	       reservedpages << (PAGE_SHIFT-10),
	       datasize >> 10,
	       initsize >> 10,
	       (unsigned long) (totalhigh_pages << (PAGE_SHIFT-10)));
}
#endif /* !CONFIG_NEED_MULTIPLE_NODES */

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
#ifdef CONFIG_64BIT
	/* Switch from KSEG0 to XKPHYS addresses */
	start = (unsigned long)phys_to_virt(CPHYSADDR(start));
	end = (unsigned long)phys_to_virt(CPHYSADDR(end));
#endif
	if (start < end)
		printk(KERN_INFO "Freeing initrd memory: %ldk freed\n",
		       (end - start) >> 10);

	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		totalram_pages++;
	}
}
#endif

extern unsigned long prom_free_prom_memory(void);

void free_initmem(void)
{
	unsigned long addr, page, freed;

	freed = prom_free_prom_memory();

	addr = (unsigned long) &__init_begin;
	while (addr < (unsigned long) &__init_end) {
#ifdef CONFIG_64BIT
		page = PAGE_OFFSET | CPHYSADDR(addr);
#else
		page = addr;
#endif
		ClearPageReserved(virt_to_page(page));
		set_page_count(virt_to_page(page), 1);
		free_page(page);
		totalram_pages++;
		freed += PAGE_SIZE;
		addr += PAGE_SIZE;
	}
	printk(KERN_INFO "Freeing unused kernel memory: %ldk freed\n",
	       freed >> 10);
}
