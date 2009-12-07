/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
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
#include <linux/suspend.h>
#include <linux/lmb.h>

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
#include <asm/sections.h>
#include <asm/sparsemem.h>
#include <asm/vdso.h>
#include <asm/fixmap.h>

#include "mmu_decl.h"

#ifndef CPU_FTR_COHERENT_ICACHE
#define CPU_FTR_COHERENT_ICACHE	0	/* XXX for now */
#define CPU_FTR_NOEXECUTE	0
#endif

int init_bootmem_done;
int mem_init_done;
phys_addr_t memory_limit;

#ifdef CONFIG_HIGHMEM
pte_t *kmap_pte;
pgprot_t kmap_prot;

EXPORT_SYMBOL(kmap_prot);
EXPORT_SYMBOL(kmap_pte);

static inline pte_t *virt_to_kpte(unsigned long vaddr)
{
	return pte_offset_kernel(pmd_offset(pud_offset(pgd_offset_k(vaddr),
			vaddr), vaddr), vaddr);
}
#endif

int page_is_ram(unsigned long pfn)
{
#ifndef CONFIG_PPC64	/* XXX for now */
	return pfn < max_pfn;
#else
	unsigned long paddr = (pfn << PAGE_SHIFT);
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

pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
			      unsigned long size, pgprot_t vma_prot)
{
	if (ppc_md.phys_mem_access_prot)
		return ppc_md.phys_mem_access_prot(file, pfn, size, vma_prot);

	if (!page_is_ram(pfn))
		vma_prot = pgprot_noncached(vma_prot);

	return vma_prot;
}
EXPORT_SYMBOL(phys_mem_access_prot);

#ifdef CONFIG_MEMORY_HOTPLUG

#ifdef CONFIG_NUMA
int memory_add_physaddr_to_nid(u64 start)
{
	return hot_add_scn_to_nid(start);
}
#endif

int arch_add_memory(int nid, u64 start, u64 size)
{
	struct pglist_data *pgdata;
	struct zone *zone;
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;

	pgdata = NODE_DATA(nid);

	start = (unsigned long)__va(start);
	create_section_mapping(start, start + size);

	/* this should work for most non-highmem platforms */
	zone = pgdata->node_zones;

	return __add_pages(nid, zone, start_pfn, nr_pages);
}
#endif /* CONFIG_MEMORY_HOTPLUG */

/*
 * walk_memory_resource() needs to make sure there is no holes in a given
 * memory range.  PPC64 does not maintain the memory layout in /proc/iomem.
 * Instead it maintains it in lmb.memory structures.  Walk through the
 * memory regions, find holes and callback for contiguous regions.
 */
int
walk_system_ram_range(unsigned long start_pfn, unsigned long nr_pages,
		void *arg, int (*func)(unsigned long, unsigned long, void *))
{
	struct lmb_property res;
	unsigned long pfn, len;
	u64 end;
	int ret = -1;

	res.base = (u64) start_pfn << PAGE_SHIFT;
	res.size = (u64) nr_pages << PAGE_SHIFT;

	end = res.base + res.size - 1;
	while ((res.base < end) && (lmb_find(&res) >= 0)) {
		pfn = (unsigned long)(res.base >> PAGE_SHIFT);
		len = (unsigned long)(res.size >> PAGE_SHIFT);
		ret = (*func)(pfn, len, arg);
		if (ret)
			break;
		res.base += (res.size + 1);
		res.size = (end - res.base + 1);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(walk_system_ram_range);

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

	max_low_pfn = max_pfn = lmb_end_of_DRAM() >> PAGE_SHIFT;
	total_pages = (lmb_end_of_DRAM() - memstart_addr) >> PAGE_SHIFT;
#ifdef CONFIG_HIGHMEM
	total_pages = total_lowmem >> PAGE_SHIFT;
	max_low_pfn = lowmem_end_addr >> PAGE_SHIFT;
#endif

	/*
	 * Find an area to use for the bootmem bitmap.  Calculate the size of
	 * bitmap required as (Total Memory) / PAGE_SIZE / BITS_PER_BYTE.
	 * Add 1 additional page in case the address isn't page-aligned.
	 */
	bootmap_pages = bootmem_bootmap_pages(total_pages);

	start = lmb_alloc(bootmap_pages << PAGE_SHIFT, PAGE_SIZE);

	min_low_pfn = MEMORY_START >> PAGE_SHIFT;
	boot_mapsize = init_bootmem_node(NODE_DATA(0), start >> PAGE_SHIFT, min_low_pfn, max_low_pfn);

	/* Add active regions with valid PFNs */
	for (i = 0; i < lmb.memory.cnt; i++) {
		unsigned long start_pfn, end_pfn;
		start_pfn = lmb.memory.region[i].base >> PAGE_SHIFT;
		end_pfn = start_pfn + lmb_size_pages(&lmb.memory, i);
		add_active_range(0, start_pfn, end_pfn);
	}

	/* Add all physical memory to the bootmem map, mark each area
	 * present.
	 */
#ifdef CONFIG_HIGHMEM
	free_bootmem_with_active_regions(0, lowmem_end_addr >> PAGE_SHIFT);

	/* reserve the sections we're already using */
	for (i = 0; i < lmb.reserved.cnt; i++) {
		unsigned long addr = lmb.reserved.region[i].base +
				     lmb_size_bytes(&lmb.reserved, i) - 1;
		if (addr < lowmem_end_addr)
			reserve_bootmem(lmb.reserved.region[i].base,
					lmb_size_bytes(&lmb.reserved, i),
					BOOTMEM_DEFAULT);
		else if (lmb.reserved.region[i].base < lowmem_end_addr) {
			unsigned long adjusted_size = lowmem_end_addr -
				      lmb.reserved.region[i].base;
			reserve_bootmem(lmb.reserved.region[i].base,
					adjusted_size, BOOTMEM_DEFAULT);
		}
	}
#else
	free_bootmem_with_active_regions(0, max_pfn);

	/* reserve the sections we're already using */
	for (i = 0; i < lmb.reserved.cnt; i++)
		reserve_bootmem(lmb.reserved.region[i].base,
				lmb_size_bytes(&lmb.reserved, i),
				BOOTMEM_DEFAULT);

#endif
	/* XXX need to clip this if using highmem? */
	sparse_memory_present_with_active_regions(0);

	init_bootmem_done = 1;
}

/* mark pages that don't exist as nosave */
static int __init mark_nonram_nosave(void)
{
	unsigned long lmb_next_region_start_pfn,
		      lmb_region_max_pfn;
	int i;

	for (i = 0; i < lmb.memory.cnt - 1; i++) {
		lmb_region_max_pfn =
			(lmb.memory.region[i].base >> PAGE_SHIFT) +
			(lmb.memory.region[i].size >> PAGE_SHIFT);
		lmb_next_region_start_pfn =
			lmb.memory.region[i+1].base >> PAGE_SHIFT;

		if (lmb_region_max_pfn < lmb_next_region_start_pfn)
			register_nosave_region(lmb_region_max_pfn,
					       lmb_next_region_start_pfn);
	}

	return 0;
}

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
void __init paging_init(void)
{
	unsigned long total_ram = lmb_phys_mem_size();
	phys_addr_t top_of_ram = lmb_end_of_DRAM();
	unsigned long max_zone_pfns[MAX_NR_ZONES];

#ifdef CONFIG_PPC32
	unsigned long v = __fix_to_virt(__end_of_fixed_addresses - 1);
	unsigned long end = __fix_to_virt(FIX_HOLE);

	for (; v < end; v += PAGE_SIZE)
		map_page(v, 0, 0); /* XXX gross */
#endif

#ifdef CONFIG_HIGHMEM
	map_page(PKMAP_BASE, 0, 0);	/* XXX gross */
	pkmap_page_table = virt_to_kpte(PKMAP_BASE);

	kmap_pte = virt_to_kpte(__fix_to_virt(FIX_KMAP_BEGIN));
	kmap_prot = PAGE_KERNEL;
#endif /* CONFIG_HIGHMEM */

	printk(KERN_DEBUG "Top of RAM: 0x%llx, Total RAM: 0x%lx\n",
	       (unsigned long long)top_of_ram, total_ram);
	printk(KERN_DEBUG "Memory hole size: %ldMB\n",
	       (long int)((top_of_ram - total_ram) >> 20));
	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
#ifdef CONFIG_HIGHMEM
	max_zone_pfns[ZONE_DMA] = lowmem_end_addr >> PAGE_SHIFT;
	max_zone_pfns[ZONE_HIGHMEM] = top_of_ram >> PAGE_SHIFT;
#else
	max_zone_pfns[ZONE_DMA] = top_of_ram >> PAGE_SHIFT;
#endif
	free_area_init_nodes(max_zone_pfns);

	mark_nonram_nosave();
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
			printk("freeing bootmem node %d\n", nid);
			totalram_pages +=
				free_all_bootmem_node(NODE_DATA(nid));
		}
	}
#else
	max_mapnr = max_pfn;
	totalram_pages += free_all_bootmem();
#endif
	for_each_online_pgdat(pgdat) {
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

		highmem_mapnr = lowmem_end_addr >> PAGE_SHIFT;
		for (pfn = highmem_mapnr; pfn < max_mapnr; ++pfn) {
			struct page *page = pfn_to_page(pfn);
			if (lmb_is_reserved(pfn << PAGE_SHIFT))
				continue;
			ClearPageReserved(page);
			init_page_count(page);
			__free_page(page);
			totalhigh_pages++;
			reservedpages--;
		}
		totalram_pages += totalhigh_pages;
		printk(KERN_DEBUG "High memory: %luk\n",
		       totalhigh_pages << (PAGE_SHIFT-10));
	}
#endif /* CONFIG_HIGHMEM */

	printk(KERN_INFO "Memory: %luk/%luk available (%luk kernel code, "
	       "%luk reserved, %luk data, %luk bss, %luk init)\n",
		nr_free_pages() << (PAGE_SHIFT-10),
		num_physpages << (PAGE_SHIFT-10),
		codesize >> 10,
		reservedpages << (PAGE_SHIFT-10),
		datasize >> 10,
		bsssize >> 10,
		initsize >> 10);

#ifdef CONFIG_PPC32
	pr_info("Kernel virtual memory layout:\n");
	pr_info("  * 0x%08lx..0x%08lx  : fixmap\n", FIXADDR_START, FIXADDR_TOP);
#ifdef CONFIG_HIGHMEM
	pr_info("  * 0x%08lx..0x%08lx  : highmem PTEs\n",
		PKMAP_BASE, PKMAP_ADDR(LAST_PKMAP));
#endif /* CONFIG_HIGHMEM */
#ifdef CONFIG_NOT_COHERENT_CACHE
	pr_info("  * 0x%08lx..0x%08lx  : consistent mem\n",
		IOREMAP_TOP, IOREMAP_TOP + CONFIG_CONSISTENT_SIZE);
#endif /* CONFIG_NOT_COHERENT_CACHE */
	pr_info("  * 0x%08lx..0x%08lx  : early ioremap\n",
		ioremap_bot, IOREMAP_TOP);
	pr_info("  * 0x%08lx..0x%08lx  : vmalloc & ioremap\n",
		VMALLOC_START, VMALLOC_END);
#endif /* CONFIG_PPC32 */

	mem_init_done = 1;
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

	/*
	 * We shouldnt have to do this, but some versions of glibc
	 * require it (ld.so assumes zero filled pages are icache clean)
	 * - Anton
	 */
	flush_dcache_page(pg);
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

	flush_dcache_page(pg);
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
