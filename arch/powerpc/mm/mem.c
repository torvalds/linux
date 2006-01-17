/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
 *  PPC44x/36-bit changes by Matt Porter (mporter@mvista.com)
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/initrd.h>
#include <linux/pagemap.h>

#include <asm/pgalloc.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/btext.h>
#include <asm/tlb.h>
#include <asm/prom.h>
#include <asm/lmb.h>
#include <asm/sections.h>
#include <asm/vdso.h>

#include "mmu_decl.h"

#ifndef CPU_FTR_COHERENT_ICACHE
#define CPU_FTR_COHERENT_ICACHE	0	/* XXX for now */
#define CPU_FTR_NOEXECUTE	0
#endif

int init_bootmem_done;
int mem_init_done;
unsigned long memory_limit;

extern void hash_preload(struct mm_struct *mm, unsigned long ea,
			 unsigned long access, unsigned long trap);

/*
 * This is called by /dev/mem to know if a given address has to
 * be mapped non-cacheable or not
 */
int page_is_ram(unsigned long pfn)
{
	unsigned long paddr = (pfn << PAGE_SHIFT);

#ifndef CONFIG_PPC64	/* XXX for now */
	return paddr < __pa(high_memory);
#else
	int i;
	for (i=0; i < lmb.memory.cnt; i++) {
		unsigned long base;

		base = lmb.memory.region[i].base;

		if ((paddr >= base) &&
			(paddr < (base + lmb.memory.region[i].size))) {
			return 1;
		}
	}

	return 0;
#endif
}
EXPORT_SYMBOL(page_is_ram);

pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
			      unsigned long size, pgprot_t vma_prot)
{
	if (ppc_md.phys_mem_access_prot)
		return ppc_md.phys_mem_access_prot(file, pfn, size, vma_prot);

	if (!page_is_ram(pfn))
		vma_prot = __pgprot(pgprot_val(vma_prot)
				    | _PAGE_GUARDED | _PAGE_NO_CACHE);
	return vma_prot;
}
EXPORT_SYMBOL(phys_mem_access_prot);

#ifdef CONFIG_MEMORY_HOTPLUG

void online_page(struct page *page)
{
	ClearPageReserved(page);
	set_page_count(page, 0);
	free_cold_page(page);
	totalram_pages++;
	num_physpages++;
}

int __devinit add_memory(u64 start, u64 size)
{
	struct pglist_data *pgdata;
	struct zone *zone;
	int nid;
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;

	nid = hot_add_scn_to_nid(start);
	pgdata = NODE_DATA(nid);

	start = __va(start);
	create_section_mapping(start, start + size);

	/* this should work for most non-highmem platforms */
	zone = pgdata->node_zones;

	return __add_pages(zone, start_pfn, nr_pages);

	return 0;
}

/*
 * First pass at this code will check to determine if the remove
 * request is within the RMO.  Do not allow removal within the RMO.
 */
int __devinit remove_memory(u64 start, u64 size)
{
	struct zone *zone;
	unsigned long start_pfn, end_pfn, nr_pages;

	start_pfn = start >> PAGE_SHIFT;
	nr_pages = size >> PAGE_SHIFT;
	end_pfn = start_pfn + nr_pages;

	printk("%s(): Attempting to remove memoy in range "
			"%lx to %lx\n", __func__, start, start+size);
	/*
	 * check for range within RMO
	 */
	zone = page_zone(pfn_to_page(start_pfn));

	printk("%s(): memory will be removed from "
			"the %s zone\n", __func__, zone->name);

	/*
	 * not handling removing memory ranges that
	 * overlap multiple zones yet
	 */
	if (end_pfn > (zone->zone_start_pfn + zone->spanned_pages))
		goto overlap;

	/* make sure it is NOT in RMO */
	if ((start < lmb.rmo_size) || ((start+size) < lmb.rmo_size)) {
		printk("%s(): range to be removed must NOT be in RMO!\n",
			__func__);
		goto in_rmo;
	}

	return __remove_pages(zone, start_pfn, nr_pages);

overlap:
	printk("%s(): memory range to be removed overlaps "
		"multiple zones!!!\n", __func__);
in_rmo:
	return -1;
}
#endif /* CONFIG_MEMORY_HOTPLUG */

void show_mem(void)
{
	unsigned long total = 0, reserved = 0;
	unsigned long shared = 0, cached = 0;
	unsigned long highmem = 0;
	struct page *page;
	pg_data_t *pgdat;
	unsigned long i;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	for_each_pgdat(pgdat) {
		unsigned long flags;
		pgdat_resize_lock(pgdat, &flags);
		for (i = 0; i < pgdat->node_spanned_pages; i++) {
			if (!pfn_valid(pgdat->node_start_pfn + i))
				continue;
			page = pgdat_page_nr(pgdat, i);
			total++;
			if (PageHighMem(page))
				highmem++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (page_count(page))
				shared += page_count(page) - 1;
		}
		pgdat_resize_unlock(pgdat, &flags);
	}
	printk("%ld pages of RAM\n", total);
#ifdef CONFIG_HIGHMEM
	printk("%ld pages of HIGHMEM\n", highmem);
#endif
	printk("%ld reserved pages\n", reserved);
	printk("%ld pages shared\n", shared);
	printk("%ld pages swap cached\n", cached);
}

/*
 * Initialize the bootmem system and give it all the memory we
 * have available.  If we are using highmem, we only put the
 * lowmem into the bootmem system.
 */
#ifndef CONFIG_NEED_MULTIPLE_NODES
void __init do_init_bootmem(void)
{
	unsigned long i;
	unsigned long start, bootmap_pages;
	unsigned long total_pages;
	int boot_mapsize;

	max_pfn = total_pages = lmb_end_of_DRAM() >> PAGE_SHIFT;
#ifdef CONFIG_HIGHMEM
	total_pages = total_lowmem >> PAGE_SHIFT;
#endif

	/*
	 * Find an area to use for the bootmem bitmap.  Calculate the size of
	 * bitmap required as (Total Memory) / PAGE_SIZE / BITS_PER_BYTE.
	 * Add 1 additional page in case the address isn't page-aligned.
	 */
	bootmap_pages = bootmem_bootmap_pages(total_pages);

	start = lmb_alloc(bootmap_pages << PAGE_SHIFT, PAGE_SIZE);
	BUG_ON(!start);

	boot_mapsize = init_bootmem(start >> PAGE_SHIFT, total_pages);

	/* Add all physical memory to the bootmem map, mark each area
	 * present.
	 */
	for (i = 0; i < lmb.memory.cnt; i++) {
		unsigned long base = lmb.memory.region[i].base;
		unsigned long size = lmb_size_bytes(&lmb.memory, i);
#ifdef CONFIG_HIGHMEM
		if (base >= total_lowmem)
			continue;
		if (base + size > total_lowmem)
			size = total_lowmem - base;
#endif
		free_bootmem(base, size);
	}

	/* reserve the sections we're already using */
	for (i = 0; i < lmb.reserved.cnt; i++)
		reserve_bootmem(lmb.reserved.region[i].base,
				lmb_size_bytes(&lmb.reserved, i));

	/* XXX need to clip this if using highmem? */
	for (i = 0; i < lmb.memory.cnt; i++)
		memory_present(0, lmb_start_pfn(&lmb.memory, i),
			       lmb_end_pfn(&lmb.memory, i));
	init_bootmem_done = 1;
}

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];
	unsigned long zholes_size[MAX_NR_ZONES];
	unsigned long total_ram = lmb_phys_mem_size();
	unsigned long top_of_ram = lmb_end_of_DRAM();

#ifdef CONFIG_HIGHMEM
	map_page(PKMAP_BASE, 0, 0);	/* XXX gross */
	pkmap_page_table = pte_offset_kernel(pmd_offset(pgd_offset_k
			(PKMAP_BASE), PKMAP_BASE), PKMAP_BASE);
	map_page(KMAP_FIX_BEGIN, 0, 0);	/* XXX gross */
	kmap_pte = pte_offset_kernel(pmd_offset(pgd_offset_k
			(KMAP_FIX_BEGIN), KMAP_FIX_BEGIN), KMAP_FIX_BEGIN);
	kmap_prot = PAGE_KERNEL;
#endif /* CONFIG_HIGHMEM */

	printk(KERN_INFO "Top of RAM: 0x%lx, Total RAM: 0x%lx\n",
	       top_of_ram, total_ram);
	printk(KERN_INFO "Memory hole size: %ldMB\n",
	       (top_of_ram - total_ram) >> 20);
	/*
	 * All pages are DMA-able so we put them all in the DMA zone.
	 */
	memset(zones_size, 0, sizeof(zones_size));
	memset(zholes_size, 0, sizeof(zholes_size));

	zones_size[ZONE_DMA] = top_of_ram >> PAGE_SHIFT;
	zholes_size[ZONE_DMA] = (top_of_ram - total_ram) >> PAGE_SHIFT;

#ifdef CONFIG_HIGHMEM
	zones_size[ZONE_DMA] = total_lowmem >> PAGE_SHIFT;
	zones_size[ZONE_HIGHMEM] = (total_memory - total_lowmem) >> PAGE_SHIFT;
	zholes_size[ZONE_HIGHMEM] = (top_of_ram - total_ram) >> PAGE_SHIFT;
#else
	zones_size[ZONE_DMA] = top_of_ram >> PAGE_SHIFT;
	zholes_size[ZONE_DMA] = (top_of_ram - total_ram) >> PAGE_SHIFT;
#endif /* CONFIG_HIGHMEM */

	free_area_init_node(0, NODE_DATA(0), zones_size,
			    __pa(PAGE_OFFSET) >> PAGE_SHIFT, zholes_size);
}
#endif /* ! CONFIG_NEED_MULTIPLE_NODES */

void __init mem_init(void)
{
#ifdef CONFIG_NEED_MULTIPLE_NODES
	int nid;
#endif
	pg_data_t *pgdat;
	unsigned long i;
	struct page *page;
	unsigned long reservedpages = 0, codesize, initsize, datasize, bsssize;

	num_physpages = lmb.memory.size >> PAGE_SHIFT;
	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);

#ifdef CONFIG_NEED_MULTIPLE_NODES
        for_each_online_node(nid) {
		if (NODE_DATA(nid)->node_spanned_pages != 0) {
			printk("freeing bootmem node %x\n", nid);
			totalram_pages +=
				free_all_bootmem_node(NODE_DATA(nid));
		}
	}
#else
	max_mapnr = max_pfn;
	totalram_pages += free_all_bootmem();
#endif
	for_each_pgdat(pgdat) {
		for (i = 0; i < pgdat->node_spanned_pages; i++) {
			if (!pfn_valid(pgdat->node_start_pfn + i))
				continue;
			page = pgdat_page_nr(pgdat, i);
			if (PageReserved(page))
				reservedpages++;
		}
	}

	codesize = (unsigned long)&_sdata - (unsigned long)&_stext;
	datasize = (unsigned long)&_edata - (unsigned long)&_sdata;
	initsize = (unsigned long)&__init_end - (unsigned long)&__init_begin;
	bsssize = (unsigned long)&__bss_stop - (unsigned long)&__bss_start;

#ifdef CONFIG_HIGHMEM
	{
		unsigned long pfn, highmem_mapnr;

		highmem_mapnr = total_lowmem >> PAGE_SHIFT;
		for (pfn = highmem_mapnr; pfn < max_mapnr; ++pfn) {
			struct page *page = pfn_to_page(pfn);

			ClearPageReserved(page);
			set_page_count(page, 1);
			__free_page(page);
			totalhigh_pages++;
		}
		totalram_pages += totalhigh_pages;
		printk(KERN_INFO "High memory: %luk\n",
		       totalhigh_pages << (PAGE_SHIFT-10));
	}
#endif /* CONFIG_HIGHMEM */

	printk(KERN_INFO "Memory: %luk/%luk available (%luk kernel code, "
	       "%luk reserved, %luk data, %luk bss, %luk init)\n",
		(unsigned long)nr_free_pages() << (PAGE_SHIFT-10),
		num_physpages << (PAGE_SHIFT-10),
		codesize >> 10,
		reservedpages << (PAGE_SHIFT-10),
		datasize >> 10,
		bsssize >> 10,
		initsize >> 10);

	mem_init_done = 1;

	/* Initialize the vDSO */
	vdso_init();
}

/*
 * This is called when a page has been modified by the kernel.
 * It just marks the page as not i-cache clean.  We do the i-cache
 * flush later when the page is given to a user process, if necessary.
 */
void flush_dcache_page(struct page *page)
{
	if (cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		return;
	/* avoid an atomic op if possible */
	if (test_bit(PG_arch_1, &page->flags))
		clear_bit(PG_arch_1, &page->flags);
}
EXPORT_SYMBOL(flush_dcache_page);

void flush_dcache_icache_page(struct page *page)
{
#ifdef CONFIG_BOOKE
	void *start = kmap_atomic(page, KM_PPC_SYNC_ICACHE);
	__flush_dcache_icache(start);
	kunmap_atomic(start, KM_PPC_SYNC_ICACHE);
#elif defined(CONFIG_8xx) || defined(CONFIG_PPC64)
	/* On 8xx there is no need to kmap since highmem is not supported */
	__flush_dcache_icache(page_address(page)); 
#else
	__flush_dcache_icache_phys(page_to_pfn(page) << PAGE_SHIFT);
#endif

}
void clear_user_page(void *page, unsigned long vaddr, struct page *pg)
{
	clear_page(page);

	if (cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		return;
	/*
	 * We shouldnt have to do this, but some versions of glibc
	 * require it (ld.so assumes zero filled pages are icache clean)
	 * - Anton
	 */

	/* avoid an atomic op if possible */
	if (test_bit(PG_arch_1, &pg->flags))
		clear_bit(PG_arch_1, &pg->flags);
}
EXPORT_SYMBOL(clear_user_page);

void copy_user_page(void *vto, void *vfrom, unsigned long vaddr,
		    struct page *pg)
{
	copy_page(vto, vfrom);

	/*
	 * We should be able to use the following optimisation, however
	 * there are two problems.
	 * Firstly a bug in some versions of binutils meant PLT sections
	 * were not marked executable.
	 * Secondly the first word in the GOT section is blrl, used
	 * to establish the GOT address. Until recently the GOT was
	 * not marked executable.
	 * - Anton
	 */
#if 0
	if (!vma->vm_file && ((vma->vm_flags & VM_EXEC) == 0))
		return;
#endif

	if (cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		return;

	/* avoid an atomic op if possible */
	if (test_bit(PG_arch_1, &pg->flags))
		clear_bit(PG_arch_1, &pg->flags);
}

void flush_icache_user_range(struct vm_area_struct *vma, struct page *page,
			     unsigned long addr, int len)
{
	unsigned long maddr;

	maddr = (unsigned long) kmap(page) + (addr & ~PAGE_MASK);
	flush_icache_range(maddr, maddr + len);
	kunmap(page);
}
EXPORT_SYMBOL(flush_icache_user_range);

/*
 * This is called at the end of handling a user page fault, when the
 * fault has been handled by updating a PTE in the linux page tables.
 * We use it to preload an HPTE into the hash table corresponding to
 * the updated linux PTE.
 * 
 * This must always be called with the pte lock held.
 */
void update_mmu_cache(struct vm_area_struct *vma, unsigned long address,
		      pte_t pte)
{
#ifdef CONFIG_PPC_STD_MMU
	unsigned long access = 0, trap;
#endif
	unsigned long pfn = pte_pfn(pte);

	/* handle i-cache coherency */
	if (!cpu_has_feature(CPU_FTR_COHERENT_ICACHE) &&
	    !cpu_has_feature(CPU_FTR_NOEXECUTE) &&
	    pfn_valid(pfn)) {
		struct page *page = pfn_to_page(pfn);
		if (!PageReserved(page)
		    && !test_bit(PG_arch_1, &page->flags)) {
			if (vma->vm_mm == current->active_mm) {
#ifdef CONFIG_8xx
			/* On 8xx, cache control instructions (particularly 
		 	 * "dcbst" from flush_dcache_icache) fault as write 
			 * operation if there is an unpopulated TLB entry 
			 * for the address in question. To workaround that, 
			 * we invalidate the TLB here, thus avoiding dcbst 
			 * misbehaviour.
			 */
				_tlbie(address);
#endif
				__flush_dcache_icache((void *) address);
			} else
				flush_dcache_icache_page(page);
			set_bit(PG_arch_1, &page->flags);
		}
	}

#ifdef CONFIG_PPC_STD_MMU
	/* We only want HPTEs for linux PTEs that have _PAGE_ACCESSED set */
	if (!pte_young(pte) || address >= TASK_SIZE)
		return;

	/* We try to figure out if we are coming from an instruction
	 * access fault and pass that down to __hash_page so we avoid
	 * double-faulting on execution of fresh text. We have to test
	 * for regs NULL since init will get here first thing at boot
	 *
	 * We also avoid filling the hash if not coming from a fault
	 */
	if (current->thread.regs == NULL)
		return;
	trap = TRAP(current->thread.regs);
	if (trap == 0x400)
		access |= _PAGE_EXEC;
	else if (trap != 0x300)
		return;
	hash_preload(vma->vm_mm, address, access, trap);
#endif /* CONFIG_PPC_STD_MMU */
}
