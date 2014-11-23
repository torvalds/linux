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

#include <linux/export.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/gfp.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/initrd.h>
#include <linux/pagemap.h>
#include <linux/suspend.h>
#include <linux/memblock.h>
#include <linux/hugetlb.h>
#include <linux/slab.h>

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
#include <asm/swiotlb.h>
#include <asm/rtas.h>

#include "mmu_decl.h"

#ifndef CPU_FTR_COHERENT_ICACHE
#define CPU_FTR_COHERENT_ICACHE	0	/* XXX for now */
#define CPU_FTR_NOEXECUTE	0
#endif

int init_bootmem_done;
int mem_init_done;
unsigned long long memory_limit;

#ifdef CONFIG_HIGHMEM
pte_t *kmap_pte;
EXPORT_SYMBOL(kmap_pte);
pgprot_t kmap_prot;
EXPORT_SYMBOL(kmap_prot);

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
	struct memblock_region *reg;

	for_each_memblock(memory, reg)
		if (paddr >= reg->base && paddr < (reg->base + reg->size))
			return 1;
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
	if (create_section_mapping(start, start + size))
		return -EINVAL;

	/* this should work for most non-highmem platforms */
	zone = pgdata->node_zones +
		zone_for_memory(nid, start, size, 0);

	return __add_pages(nid, zone, start_pfn, nr_pages);
}

#ifdef CONFIG_MEMORY_HOTREMOVE
int arch_remove_memory(u64 start, u64 size)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;
	struct zone *zone;
	int ret;

	zone = page_zone(pfn_to_page(start_pfn));
	ret = __remove_pages(zone, start_pfn, nr_pages);
	if (!ret && (ppc_md.remove_memory))
		ret = ppc_md.remove_memory(start, size);

	return ret;
}
#endif
#endif /* CONFIG_MEMORY_HOTPLUG */

/*
 * walk_memory_resource() needs to make sure there is no holes in a given
 * memory range.  PPC64 does not maintain the memory layout in /proc/iomem.
 * Instead it maintains it in memblock.memory structures.  Walk through the
 * memory regions, find holes and callback for contiguous regions.
 */
int
walk_system_ram_range(unsigned long start_pfn, unsigned long nr_pages,
		void *arg, int (*func)(unsigned long, unsigned long, void *))
{
	struct memblock_region *reg;
	unsigned long end_pfn = start_pfn + nr_pages;
	unsigned long tstart, tend;
	int ret = -1;

	for_each_memblock(memory, reg) {
		tstart = max(start_pfn, memblock_region_memory_base_pfn(reg));
		tend = min(end_pfn, memblock_region_memory_end_pfn(reg));
		if (tstart >= tend)
			continue;
		ret = (*func)(tstart, tend - tstart, arg);
		if (ret)
			break;
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
	unsigned long start, bootmap_pages;
	unsigned long total_pages;
	struct memblock_region *reg;
	int boot_mapsize;

	max_low_pfn = max_pfn = memblock_end_of_DRAM() >> PAGE_SHIFT;
	total_pages = (memblock_end_of_DRAM() - memstart_addr) >> PAGE_SHIFT;
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

	start = memblock_alloc(bootmap_pages << PAGE_SHIFT, PAGE_SIZE);

	min_low_pfn = MEMORY_START >> PAGE_SHIFT;
	boot_mapsize = init_bootmem_node(NODE_DATA(0), start >> PAGE_SHIFT, min_low_pfn, max_low_pfn);

	/* Place all memblock_regions in the same node and merge contiguous
	 * memblock_regions
	 */
	memblock_set_node(0, (phys_addr_t)ULLONG_MAX, &memblock.memory, 0);

	/* Add all physical memory to the bootmem map, mark each area
	 * present.
	 */
#ifdef CONFIG_HIGHMEM
	free_bootmem_with_active_regions(0, lowmem_end_addr >> PAGE_SHIFT);

	/* reserve the sections we're already using */
	for_each_memblock(reserved, reg) {
		unsigned long top = reg->base + reg->size - 1;
		if (top < lowmem_end_addr)
			reserve_bootmem(reg->base, reg->size, BOOTMEM_DEFAULT);
		else if (reg->base < lowmem_end_addr) {
			unsigned long trunc_size = lowmem_end_addr - reg->base;
			reserve_bootmem(reg->base, trunc_size, BOOTMEM_DEFAULT);
		}
	}
#else
	free_bootmem_with_active_regions(0, max_pfn);

	/* reserve the sections we're already using */
	for_each_memblock(reserved, reg)
		reserve_bootmem(reg->base, reg->size, BOOTMEM_DEFAULT);
#endif
	/* XXX need to clip this if using highmem? */
	sparse_memory_present_with_active_regions(0);

	init_bootmem_done = 1;
}

/* mark pages that don't exist as nosave */
static int __init mark_nonram_nosave(void)
{
	struct memblock_region *reg, *prev = NULL;

	for_each_memblock(memory, reg) {
		if (prev &&
		    memblock_region_memory_end_pfn(prev) < memblock_region_memory_base_pfn(reg))
			register_nosave_region(memblock_region_memory_end_pfn(prev),
					       memblock_region_memory_base_pfn(reg));
		prev = reg;
	}
	return 0;
}
#else /* CONFIG_NEED_MULTIPLE_NODES */
static int __init mark_nonram_nosave(void)
{
	return 0;
}
#endif

static bool zone_limits_final;

static unsigned long max_zone_pfns[MAX_NR_ZONES] = {
	[0 ... MAX_NR_ZONES - 1] = ~0UL
};

/*
 * Restrict the specified zone and all more restrictive zones
 * to be below the specified pfn.  May not be called after
 * paging_init().
 */
void __init limit_zone_pfn(enum zone_type zone, unsigned long pfn_limit)
{
	int i;

	if (WARN_ON(zone_limits_final))
		return;

	for (i = zone; i >= 0; i--) {
		if (max_zone_pfns[i] > pfn_limit)
			max_zone_pfns[i] = pfn_limit;
	}
}

/*
 * Find the least restrictive zone that is entirely below the
 * specified pfn limit.  Returns < 0 if no suitable zone is found.
 *
 * pfn_limit must be u64 because it can exceed 32 bits even on 32-bit
 * systems -- the DMA limit can be higher than any possible real pfn.
 */
int dma_pfn_limit_to_zone(u64 pfn_limit)
{
	enum zone_type top_zone = ZONE_NORMAL;
	int i;

#ifdef CONFIG_HIGHMEM
	top_zone = ZONE_HIGHMEM;
#endif

	for (i = top_zone; i >= 0; i--) {
		if (max_zone_pfns[i] <= pfn_limit)
			return i;
	}

	return -EPERM;
}

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
void __init paging_init(void)
{
	unsigned long long total_ram = memblock_phys_mem_size();
	phys_addr_t top_of_ram = memblock_end_of_DRAM();
	enum zone_type top_zone;

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

	printk(KERN_DEBUG "Top of RAM: 0x%llx, Total RAM: 0x%llx\n",
	       (unsigned long long)top_of_ram, total_ram);
	printk(KERN_DEBUG "Memory hole size: %ldMB\n",
	       (long int)((top_of_ram - total_ram) >> 20));

#ifdef CONFIG_HIGHMEM
	top_zone = ZONE_HIGHMEM;
	limit_zone_pfn(ZONE_NORMAL, lowmem_end_addr >> PAGE_SHIFT);
#else
	top_zone = ZONE_NORMAL;
#endif

	limit_zone_pfn(top_zone, top_of_ram >> PAGE_SHIFT);
	zone_limits_final = true;
	free_area_init_nodes(max_zone_pfns);

	mark_nonram_nosave();
}

static void __init register_page_bootmem_info(void)
{
	int i;

	for_each_online_node(i)
		register_page_bootmem_info_node(NODE_DATA(i));
}

void __init mem_init(void)
{
	/*
	 * book3s is limited to 16 page sizes due to encoding this in
	 * a 4-bit field for slices.
	 */
	BUILD_BUG_ON(MMU_PAGE_COUNT > 16);

#ifdef CONFIG_SWIOTLB
	swiotlb_init(0);
#endif

	register_page_bootmem_info();
	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);
	set_max_mapnr(max_pfn);
	free_all_bootmem();

#ifdef CONFIG_HIGHMEM
	{
		unsigned long pfn, highmem_mapnr;

		highmem_mapnr = lowmem_end_addr >> PAGE_SHIFT;
		for (pfn = highmem_mapnr; pfn < max_mapnr; ++pfn) {
			phys_addr_t paddr = (phys_addr_t)pfn << PAGE_SHIFT;
			struct page *page = pfn_to_page(pfn);
			if (!memblock_is_reserved(paddr))
				free_highmem_page(page);
		}
	}
#endif /* CONFIG_HIGHMEM */

#if defined(CONFIG_PPC_FSL_BOOK3E) && !defined(CONFIG_SMP)
	/*
	 * If smp is enabled, next_tlbcam_idx is initialized in the cpu up
	 * functions.... do it here for the non-smp case.
	 */
	per_cpu(next_tlbcam_idx, smp_processor_id()) =
		(mfspr(SPRN_TLB1CFG) & TLBnCFG_N_ENTRY) - 1;
#endif

	mem_init_print_info(NULL);
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

void free_initmem(void)
{
	ppc_md.progress = ppc_printk_progress;
	free_initmem_default(POISON_FREE_INITMEM);
}

#ifdef CONFIG_BLK_DEV_INITRD
void __init free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, -1, "initrd");
}
#endif

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
#ifdef CONFIG_HUGETLB_PAGE
	if (PageCompound(page)) {
		flush_dcache_icache_hugepage(page);
		return;
	}
#endif
#ifdef CONFIG_BOOKE
	{
		void *start = kmap_atomic(page);
		__flush_dcache_icache(start);
		kunmap_atomic(start);
	}
#elif defined(CONFIG_8xx) || defined(CONFIG_PPC64)
	/* On 8xx there is no need to kmap since highmem is not supported */
	__flush_dcache_icache(page_address(page)); 
#else
	__flush_dcache_icache_phys(page_to_pfn(page) << PAGE_SHIFT);
#endif
}
EXPORT_SYMBOL(flush_dcache_icache_page);

void clear_user_page(void *page, unsigned long vaddr, struct page *pg)
{
	clear_page(page);

	/*
	 * We shouldn't have to do this, but some versions of glibc
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
		      pte_t *ptep)
{
#ifdef CONFIG_PPC_STD_MMU
	/*
	 * We don't need to worry about _PAGE_PRESENT here because we are
	 * called with either mm->page_table_lock held or ptl lock held
	 */
	unsigned long access = 0, trap;

	/* We only want HPTEs for linux PTEs that have _PAGE_ACCESSED set */
	if (!pte_young(*ptep) || address >= TASK_SIZE)
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
#if (defined(CONFIG_PPC_BOOK3E_64) || defined(CONFIG_PPC_FSL_BOOK3E)) \
	&& defined(CONFIG_HUGETLB_PAGE)
	if (is_vm_hugetlb_page(vma))
		book3e_hugetlb_preload(vma, address, *ptep);
#endif
}

/*
 * System memory should not be in /proc/iomem but various tools expect it
 * (eg kdump).
 */
static int __init add_system_ram_resources(void)
{
	struct memblock_region *reg;

	for_each_memblock(memory, reg) {
		struct resource *res;
		unsigned long base = reg->base;
		unsigned long size = reg->size;

		res = kzalloc(sizeof(struct resource), GFP_KERNEL);
		WARN_ON(!res);

		if (res) {
			res->name = "System RAM";
			res->start = base;
			res->end = base + size - 1;
			res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
			WARN_ON(request_resource(&iomem_resource, res) < 0);
		}
	}

	return 0;
}
subsys_initcall(add_system_ram_resources);

#ifdef CONFIG_STRICT_DEVMEM
/*
 * devmem_is_allowed(): check to see if /dev/mem access to a certain address
 * is valid. The argument is a physical page number.
 *
 * Access has to be given to non-kernel-ram areas as well, these contain the
 * PCI mmio resources as well as potential bios/acpi data regions.
 */
int devmem_is_allowed(unsigned long pfn)
{
	if (iomem_is_exclusive(pfn << PAGE_SHIFT))
		return 0;
	if (!page_is_ram(pfn))
		return 1;
	if (page_is_rtas_user_buf(pfn))
		return 1;
	return 0;
}
#endif /* CONFIG_STRICT_DEVMEM */
