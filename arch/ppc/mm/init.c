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
#include <asm/bootinfo.h>

#include "mem_pieces.h"
#include "mmu_decl.h"

#if defined(CONFIG_KERNEL_START_BOOL) || defined(CONFIG_LOWMEM_SIZE_BOOL)
/* The ammount of lowmem must be within 0xF0000000 - KERNELBASE. */
#if (CONFIG_LOWMEM_SIZE > (0xF0000000 - KERNELBASE))
#error "You must adjust CONFIG_LOWMEM_SIZE or CONFIG_START_KERNEL"
#endif
#endif
#define MAX_LOW_MEM	CONFIG_LOWMEM_SIZE

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

unsigned long total_memory;
unsigned long total_lowmem;

unsigned long ppc_memstart;
unsigned long ppc_memoffset = PAGE_OFFSET;

int mem_init_done;
int init_bootmem_done;
int boot_mapsize;
#ifdef CONFIG_PPC_PMAC
unsigned long agp_special_page;
#endif

extern char _end[];
extern char etext[], _stext[];
extern char __init_begin, __init_end;
extern char __prep_begin, __prep_end;
extern char __chrp_begin, __chrp_end;
extern char __pmac_begin, __pmac_end;
extern char __openfirmware_begin, __openfirmware_end;

#ifdef CONFIG_HIGHMEM
pte_t *kmap_pte;
pgprot_t kmap_prot;

EXPORT_SYMBOL(kmap_prot);
EXPORT_SYMBOL(kmap_pte);
#endif

void MMU_init(void);
void set_phys_avail(unsigned long total_ram);

/* XXX should be in current.h  -- paulus */
extern struct task_struct *current_set[NR_CPUS];

char *klimit = _end;
struct mem_pieces phys_avail;

/*
 * this tells the system to map all of ram with the segregs
 * (i.e. page tables) instead of the bats.
 * -- Cort
 */
int __map_without_bats;
int __map_without_ltlbs;

/* max amount of RAM to use */
unsigned long __max_memory;
/* max amount of low RAM to map in */
unsigned long __max_low_memory = MAX_LOW_MEM;

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0, cached = 0;
	int highmem = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageHighMem(mem_map+i))
			highmem++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (!page_count(mem_map+i))
			free++;
		else
			shared += page_count(mem_map+i) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d pages of HIGHMEM\n", highmem);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
}

/* Free up now-unused memory */
static void free_sec(unsigned long start, unsigned long end, const char *name)
{
	unsigned long cnt = 0;

	while (start < end) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		cnt++;
		start += PAGE_SIZE;
 	}
	if (cnt) {
		printk(" %ldk %s", cnt << (PAGE_SHIFT - 10), name);
		totalram_pages += cnt;
	}
}

void free_initmem(void)
{
#define FREESEC(TYPE) \
	free_sec((unsigned long)(&__ ## TYPE ## _begin), \
		 (unsigned long)(&__ ## TYPE ## _end), \
		 #TYPE);

	printk ("Freeing unused kernel memory:");
	FREESEC(init);
	if (_machine != _MACH_Pmac)
		FREESEC(pmac);
	if (_machine != _MACH_chrp)
		FREESEC(chrp);
	if (_machine != _MACH_prep)
		FREESEC(prep);
	if (!have_of)
		FREESEC(openfirmware);
 	printk("\n");
	ppc_md.progress = NULL;
#undef FREESEC
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);

	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		set_page_count(virt_to_page(start), 1);
		free_page(start);
		totalram_pages++;
	}
}
#endif

/*
 * Check for command-line options that affect what MMU_init will do.
 */
void MMU_setup(void)
{
	/* Check for nobats option (used in mapin_ram). */
	if (strstr(cmd_line, "nobats")) {
		__map_without_bats = 1;
	}

	if (strstr(cmd_line, "noltlbs")) {
		__map_without_ltlbs = 1;
	}

	/* Look for mem= option on command line */
	if (strstr(cmd_line, "mem=")) {
		char *p, *q;
		unsigned long maxmem = 0;

		for (q = cmd_line; (p = strstr(q, "mem=")) != 0; ) {
			q = p + 4;
			if (p > cmd_line && p[-1] != ' ')
				continue;
			maxmem = simple_strtoul(q, &q, 0);
			if (*q == 'k' || *q == 'K') {
				maxmem <<= 10;
				++q;
			} else if (*q == 'm' || *q == 'M') {
				maxmem <<= 20;
				++q;
			}
		}
		__max_memory = maxmem;
	}
}

/*
 * MMU_init sets up the basic memory mappings for the kernel,
 * including both RAM and possibly some I/O regions,
 * and sets up the page tables and the MMU hardware ready to go.
 */
void __init MMU_init(void)
{
	if (ppc_md.progress)
		ppc_md.progress("MMU:enter", 0x111);

	/* parse args from command line */
	MMU_setup();

	/*
	 * Figure out how much memory we have, how much
	 * is lowmem, and how much is highmem.  If we were
	 * passed the total memory size from the bootloader,
	 * just use it.
	 */
	if (boot_mem_size)
		total_memory = boot_mem_size;
	else
		total_memory = ppc_md.find_end_of_memory();

	if (__max_memory && total_memory > __max_memory)
		total_memory = __max_memory;
	total_lowmem = total_memory;
#ifdef CONFIG_FSL_BOOKE
	/* Freescale Book-E parts expect lowmem to be mapped by fixed TLB
	 * entries, so we need to adjust lowmem to match the amount we can map
	 * in the fixed entries */
	adjust_total_lowmem();
#endif /* CONFIG_FSL_BOOKE */
	if (total_lowmem > __max_low_memory) {
		total_lowmem = __max_low_memory;
#ifndef CONFIG_HIGHMEM
		total_memory = total_lowmem;
#endif /* CONFIG_HIGHMEM */
	}
	set_phys_avail(total_lowmem);

	/* Initialize the MMU hardware */
	if (ppc_md.progress)
		ppc_md.progress("MMU:hw init", 0x300);
	MMU_init_hw();

	/* Map in all of RAM starting at KERNELBASE */
	if (ppc_md.progress)
		ppc_md.progress("MMU:mapin", 0x301);
	mapin_ram();

#ifdef CONFIG_HIGHMEM
	ioremap_base = PKMAP_BASE;
#else
	ioremap_base = 0xfe000000UL;	/* for now, could be 0xfffff000 */
#endif /* CONFIG_HIGHMEM */
	ioremap_bot = ioremap_base;

	/* Map in I/O resources */
	if (ppc_md.progress)
		ppc_md.progress("MMU:setio", 0x302);
	if (ppc_md.setup_io_mappings)
		ppc_md.setup_io_mappings();

	/* Initialize the context management stuff */
	mmu_context_init();

	if (ppc_md.progress)
		ppc_md.progress("MMU:exit", 0x211);

#ifdef CONFIG_BOOTX_TEXT
	/* By default, we are no longer mapped */
       	boot_text_mapped = 0;
	/* Must be done last, or ppc_md.progress will die. */
	map_boot_text();
#endif
}

/* This is only called until mem_init is done. */
void __init *early_get_page(void)
{
	void *p;

	if (init_bootmem_done) {
		p = alloc_bootmem_pages(PAGE_SIZE);
	} else {
		p = mem_pieces_find(PAGE_SIZE, PAGE_SIZE);
	}
	return p;
}

/*
 * Initialize the bootmem system and give it all the memory we
 * have available.
 */
void __init do_init_bootmem(void)
{
	unsigned long start, size;
	int i;

	/*
	 * Find an area to use for the bootmem bitmap.
	 * We look for the first area which is at least
	 * 128kB in length (128kB is enough for a bitmap
	 * for 4GB of memory, using 4kB pages), plus 1 page
	 * (in case the address isn't page-aligned).
	 */
	start = 0;
	size = 0;
	for (i = 0; i < phys_avail.n_regions; ++i) {
		unsigned long a = phys_avail.regions[i].address;
		unsigned long s = phys_avail.regions[i].size;
		if (s <= size)
			continue;
		start = a;
		size = s;
		if (s >= 33 * PAGE_SIZE)
			break;
	}
	start = PAGE_ALIGN(start);

	min_low_pfn = start >> PAGE_SHIFT;
	max_low_pfn = (PPC_MEMSTART + total_lowmem) >> PAGE_SHIFT;
	max_pfn = (PPC_MEMSTART + total_memory) >> PAGE_SHIFT;
	boot_mapsize = init_bootmem_node(&contig_page_data, min_low_pfn,
					 PPC_MEMSTART >> PAGE_SHIFT,
					 max_low_pfn);

	/* remove the bootmem bitmap from the available memory */
	mem_pieces_remove(&phys_avail, start, boot_mapsize, 1);

	/* add everything in phys_avail into the bootmem map */
	for (i = 0; i < phys_avail.n_regions; ++i)
		free_bootmem(phys_avail.regions[i].address,
			     phys_avail.regions[i].size);

	init_bootmem_done = 1;
}

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES], i;

#ifdef CONFIG_HIGHMEM
	map_page(PKMAP_BASE, 0, 0);	/* XXX gross */
	pkmap_page_table = pte_offset_kernel(pmd_offset(pgd_offset_k
			(PKMAP_BASE), PKMAP_BASE), PKMAP_BASE);
	map_page(KMAP_FIX_BEGIN, 0, 0);	/* XXX gross */
	kmap_pte = pte_offset_kernel(pmd_offset(pgd_offset_k
			(KMAP_FIX_BEGIN), KMAP_FIX_BEGIN), KMAP_FIX_BEGIN);
	kmap_prot = PAGE_KERNEL;
#endif /* CONFIG_HIGHMEM */

	/*
	 * All pages are DMA-able so we put them all in the DMA zone.
	 */
	zones_size[ZONE_DMA] = total_lowmem >> PAGE_SHIFT;
	for (i = 1; i < MAX_NR_ZONES; i++)
		zones_size[i] = 0;

#ifdef CONFIG_HIGHMEM
	zones_size[ZONE_HIGHMEM] = (total_memory - total_lowmem) >> PAGE_SHIFT;
#endif /* CONFIG_HIGHMEM */

	free_area_init(zones_size);
}

void __init mem_init(void)
{
	unsigned long addr;
	int codepages = 0;
	int datapages = 0;
	int initpages = 0;
#ifdef CONFIG_HIGHMEM
	unsigned long highmem_mapnr;

	highmem_mapnr = total_lowmem >> PAGE_SHIFT;
#endif /* CONFIG_HIGHMEM */
	max_mapnr = total_memory >> PAGE_SHIFT;

	high_memory = (void *) __va(PPC_MEMSTART + total_lowmem);
	num_physpages = max_mapnr;	/* RAM is assumed contiguous */

	totalram_pages += free_all_bootmem();

#ifdef CONFIG_BLK_DEV_INITRD
	/* if we are booted from BootX with an initial ramdisk,
	   make sure the ramdisk pages aren't reserved. */
	if (initrd_start) {
		for (addr = initrd_start; addr < initrd_end; addr += PAGE_SIZE)
			ClearPageReserved(virt_to_page(addr));
	}
#endif /* CONFIG_BLK_DEV_INITRD */

#ifdef CONFIG_PPC_OF
	/* mark the RTAS pages as reserved */
	if ( rtas_data )
		for (addr = (ulong)__va(rtas_data);
		     addr < PAGE_ALIGN((ulong)__va(rtas_data)+rtas_size) ;
		     addr += PAGE_SIZE)
			SetPageReserved(virt_to_page(addr));
#endif
#ifdef CONFIG_PPC_PMAC
	if (agp_special_page)
		SetPageReserved(virt_to_page(agp_special_page));
#endif
	for (addr = PAGE_OFFSET; addr < (unsigned long)high_memory;
	     addr += PAGE_SIZE) {
		if (!PageReserved(virt_to_page(addr)))
			continue;
		if (addr < (ulong) etext)
			codepages++;
		else if (addr >= (unsigned long)&__init_begin
			 && addr < (unsigned long)&__init_end)
			initpages++;
		else if (addr < (ulong) klimit)
			datapages++;
	}

#ifdef CONFIG_HIGHMEM
	{
		unsigned long pfn;

		for (pfn = highmem_mapnr; pfn < max_mapnr; ++pfn) {
			struct page *page = mem_map + pfn;

			ClearPageReserved(page);
			set_page_count(page, 1);
			__free_page(page);
			totalhigh_pages++;
		}
		totalram_pages += totalhigh_pages;
	}
#endif /* CONFIG_HIGHMEM */

        printk("Memory: %luk available (%dk kernel code, %dk data, %dk init, %ldk highmem)\n",
	       (unsigned long)nr_free_pages()<< (PAGE_SHIFT-10),
	       codepages<< (PAGE_SHIFT-10), datapages<< (PAGE_SHIFT-10),
	       initpages<< (PAGE_SHIFT-10),
	       (unsigned long) (totalhigh_pages << (PAGE_SHIFT-10)));

#ifdef CONFIG_PPC_PMAC
	if (agp_special_page)
		printk(KERN_INFO "AGP special page: 0x%08lx\n", agp_special_page);
#endif

	mem_init_done = 1;
}

/*
 * Set phys_avail to the amount of physical memory,
 * less the kernel text/data/bss.
 */
void __init
set_phys_avail(unsigned long total_memory)
{
	unsigned long kstart, ksize;

	/*
	 * Initially, available physical memory is equivalent to all
	 * physical memory.
	 */

	phys_avail.regions[0].address = PPC_MEMSTART;
	phys_avail.regions[0].size = total_memory;
	phys_avail.n_regions = 1;

	/*
	 * Map out the kernel text/data/bss from the available physical
	 * memory.
	 */

	kstart = __pa(_stext);	/* should be 0 */
	ksize = PAGE_ALIGN(klimit - _stext);

	mem_pieces_remove(&phys_avail, kstart, ksize, 0);
	mem_pieces_remove(&phys_avail, 0, 0x4000, 0);

#if defined(CONFIG_BLK_DEV_INITRD)
	/* Remove the init RAM disk from the available memory. */
	if (initrd_start) {
		mem_pieces_remove(&phys_avail, __pa(initrd_start),
				  initrd_end - initrd_start, 1);
	}
#endif /* CONFIG_BLK_DEV_INITRD */
#ifdef CONFIG_PPC_OF
	/* remove the RTAS pages from the available memory */
	if (rtas_data)
		mem_pieces_remove(&phys_avail, rtas_data, rtas_size, 1);
#endif
#ifdef CONFIG_PPC_PMAC
	/* Because of some uninorth weirdness, we need a page of
	 * memory as high as possible (it must be outside of the
	 * bus address seen as the AGP aperture). It will be used
	 * by the r128 DRM driver
	 *
	 * FIXME: We need to make sure that page doesn't overlap any of the\
	 * above. This could be done by improving mem_pieces_find to be able
	 * to do a backward search from the end of the list.
	 */
	if (_machine == _MACH_Pmac && find_devices("uni-north-agp")) {
		agp_special_page = (total_memory - PAGE_SIZE);
		mem_pieces_remove(&phys_avail, agp_special_page, PAGE_SIZE, 0);
		agp_special_page = (unsigned long)__va(agp_special_page);
	}
#endif /* CONFIG_PPC_PMAC */
}

/* Mark some memory as reserved by removing it from phys_avail. */
void __init reserve_phys_mem(unsigned long start, unsigned long size)
{
	mem_pieces_remove(&phys_avail, start, size, 1);
}

/*
 * This is called when a page has been modified by the kernel.
 * It just marks the page as not i-cache clean.  We do the i-cache
 * flush later when the page is given to a user process, if necessary.
 */
void flush_dcache_page(struct page *page)
{
	clear_bit(PG_arch_1, &page->flags);
}

void flush_dcache_icache_page(struct page *page)
{
#ifdef CONFIG_BOOKE
	void *start = kmap_atomic(page, KM_PPC_SYNC_ICACHE);
	__flush_dcache_icache(start);
	kunmap_atomic(start, KM_PPC_SYNC_ICACHE);
#elif defined(CONFIG_8xx)
	/* On 8xx there is no need to kmap since highmem is not supported */
	__flush_dcache_icache(page_address(page)); 
#else
	__flush_dcache_icache_phys(page_to_pfn(page) << PAGE_SHIFT);
#endif

}
void clear_user_page(void *page, unsigned long vaddr, struct page *pg)
{
	clear_page(page);
	clear_bit(PG_arch_1, &pg->flags);
}

void copy_user_page(void *vto, void *vfrom, unsigned long vaddr,
		    struct page *pg)
{
	copy_page(vto, vfrom);
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

/*
 * This is called at the end of handling a user page fault, when the
 * fault has been handled by updating a PTE in the linux page tables.
 * We use it to preload an HPTE into the hash table corresponding to
 * the updated linux PTE.
 */
void update_mmu_cache(struct vm_area_struct *vma, unsigned long address,
		      pte_t pte)
{
	/* handle i-cache coherency */
	unsigned long pfn = pte_pfn(pte);

	if (pfn_valid(pfn)) {
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
	if (Hash != 0 && pte_young(pte)) {
		struct mm_struct *mm;
		pmd_t *pmd;

		mm = (address < TASK_SIZE)? vma->vm_mm: &init_mm;
		pmd = pmd_offset(pgd_offset(mm, address), address);
		if (!pmd_none(*pmd))
			add_hash_page(mm->context, address, pmd_val(*pmd));
	}
#endif
}

/*
 * This is called by /dev/mem to know if a given address has to
 * be mapped non-cacheable or not
 */
int page_is_ram(unsigned long pfn)
{
	unsigned long paddr = (pfn << PAGE_SHIFT);

	return paddr < __pa(high_memory);
}

pgprot_t phys_mem_access_prot(struct file *file, unsigned long addr,
			      unsigned long size, pgprot_t vma_prot)
{
	if (ppc_md.phys_mem_access_prot)
		return ppc_md.phys_mem_access_prot(file, addr, size, vma_prot);

	if (!page_is_ram(addr >> PAGE_SHIFT))
		vma_prot = __pgprot(pgprot_val(vma_prot)
				    | _PAGE_GUARDED | _PAGE_NO_CACHE);
	return vma_prot;
}
EXPORT_SYMBOL(phys_mem_access_prot);
