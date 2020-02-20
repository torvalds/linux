// SPDX-License-Identifier: GPL-2.0-or-later
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
#include <linux/memblock.h>
#include <linux/highmem.h>
#include <linux/initrd.h>
#include <linux/pagemap.h>
#include <linux/suspend.h>
#include <linux/hugetlb.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/memremap.h>
#include <linux/dma-direct.h>

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

#include <mm/mmu_decl.h>

#ifndef CPU_FTR_COHERENT_ICACHE
#define CPU_FTR_COHERENT_ICACHE	0	/* XXX for now */
#define CPU_FTR_NOEXECUTE	0
#endif

unsigned long long memory_limit;
bool init_mem_is_free;

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

int __weak create_section_mapping(unsigned long start, unsigned long end, int nid)
{
	return -ENODEV;
}

int __weak remove_section_mapping(unsigned long start, unsigned long end)
{
	return -ENODEV;
}

#define FLUSH_CHUNK_SIZE SZ_1G
/**
 * flush_dcache_range_chunked(): Write any modified data cache blocks out to
 * memory and invalidate them, in chunks of up to FLUSH_CHUNK_SIZE
 * Does not invalidate the corresponding instruction cache blocks.
 *
 * @start: the start address
 * @stop: the stop address (exclusive)
 * @chunk: the max size of the chunks
 */
static void flush_dcache_range_chunked(unsigned long start, unsigned long stop,
				       unsigned long chunk)
{
	unsigned long i;

	for (i = start; i < stop; i += chunk) {
		flush_dcache_range(i, min(stop, i + chunk));
		cond_resched();
	}
}

int __ref arch_add_memory(int nid, u64 start, u64 size,
			struct mhp_restrictions *restrictions)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;
	int rc;

	resize_hpt_for_hotplug(memblock_phys_mem_size());

	start = (unsigned long)__va(start);
	rc = create_section_mapping(start, start + size, nid);
	if (rc) {
		pr_warn("Unable to create mapping for hot added memory 0x%llx..0x%llx: %d\n",
			start, start + size, rc);
		return -EFAULT;
	}

	return __add_pages(nid, start_pfn, nr_pages, restrictions);
}

void __ref arch_remove_memory(int nid, u64 start, u64 size,
			     struct vmem_altmap *altmap)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;
	int ret;

	__remove_pages(start_pfn, nr_pages, altmap);

	/* Remove htab bolted mappings for this section of memory */
	start = (unsigned long)__va(start);
	flush_dcache_range_chunked(start, start + size, FLUSH_CHUNK_SIZE);

	ret = remove_section_mapping(start, start + size);
	WARN_ON_ONCE(ret);

	/* Ensure all vmalloc mappings are flushed in case they also
	 * hit that section of memory
	 */
	vm_unmap_aliases();

	if (resize_hpt_for_hotplug(memblock_phys_mem_size()) == -ENOSPC)
		pr_warn("Hash collision while resizing HPT\n");
}
#endif

#ifndef CONFIG_NEED_MULTIPLE_NODES
void __init mem_topology_setup(void)
{
	max_low_pfn = max_pfn = memblock_end_of_DRAM() >> PAGE_SHIFT;
	min_low_pfn = MEMORY_START >> PAGE_SHIFT;
#ifdef CONFIG_HIGHMEM
	max_low_pfn = lowmem_end_addr >> PAGE_SHIFT;
#endif

	/* Place all memblock_regions in the same node and merge contiguous
	 * memblock_regions
	 */
	memblock_set_node(0, PHYS_ADDR_MAX, &memblock.memory, 0);
}

void __init initmem_init(void)
{
	/* XXX need to clip this if using highmem? */
	sparse_memory_present_with_active_regions(0);
	sparse_init();
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

/*
 * Zones usage:
 *
 * We setup ZONE_DMA to be 31-bits on all platforms and ZONE_NORMAL to be
 * everything else. GFP_DMA32 page allocations automatically fall back to
 * ZONE_DMA.
 *
 * By using 31-bit unconditionally, we can exploit zone_dma_bits to inform the
 * generic DMA mapping code.  32-bit only devices (if not handled by an IOMMU
 * anyway) will take a first dip into ZONE_NORMAL and get otherwise served by
 * ZONE_DMA.
 */
static unsigned long max_zone_pfns[MAX_NR_ZONES];

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
void __init paging_init(void)
{
	unsigned long long total_ram = memblock_phys_mem_size();
	phys_addr_t top_of_ram = memblock_end_of_DRAM();

#ifdef CONFIG_HIGHMEM
	unsigned long v = __fix_to_virt(FIX_KMAP_END);
	unsigned long end = __fix_to_virt(FIX_KMAP_BEGIN);

	for (; v < end; v += PAGE_SIZE)
		map_kernel_page(v, 0, __pgprot(0)); /* XXX gross */

	map_kernel_page(PKMAP_BASE, 0, __pgprot(0));	/* XXX gross */
	pkmap_page_table = virt_to_kpte(PKMAP_BASE);

	kmap_pte = virt_to_kpte(__fix_to_virt(FIX_KMAP_BEGIN));
	kmap_prot = PAGE_KERNEL;
#endif /* CONFIG_HIGHMEM */

	printk(KERN_DEBUG "Top of RAM: 0x%llx, Total RAM: 0x%llx\n",
	       (unsigned long long)top_of_ram, total_ram);
	printk(KERN_DEBUG "Memory hole size: %ldMB\n",
	       (long int)((top_of_ram - total_ram) >> 20));

	/*
	 * Allow 30-bit DMA for very limited Broadcom wifi chips on many
	 * powerbooks.
	 */
	if (IS_ENABLED(CONFIG_PPC32))
		zone_dma_bits = 30;
	else
		zone_dma_bits = 31;

#ifdef CONFIG_ZONE_DMA
	max_zone_pfns[ZONE_DMA]	= min(max_low_pfn,
				      1UL << (zone_dma_bits - PAGE_SHIFT));
#endif
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
#ifdef CONFIG_HIGHMEM
	max_zone_pfns[ZONE_HIGHMEM] = max_pfn;
#endif

	free_area_init_nodes(max_zone_pfns);

	mark_nonram_nosave();
}

void __init mem_init(void)
{
	/*
	 * book3s is limited to 16 page sizes due to encoding this in
	 * a 4-bit field for slices.
	 */
	BUILD_BUG_ON(MMU_PAGE_COUNT > 16);

#ifdef CONFIG_SWIOTLB
	/*
	 * Some platforms (e.g. 85xx) limit DMA-able memory way below
	 * 4G. We force memblock to bottom-up mode to ensure that the
	 * memory allocated in swiotlb_init() is DMA-able.
	 * As it's the last memblock allocation, no need to reset it
	 * back to to-down.
	 */
	memblock_set_bottom_up(true);
	swiotlb_init(0);
#endif

	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);
	set_max_mapnr(max_pfn);
	memblock_free_all();

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
#ifdef CONFIG_KASAN
	pr_info("  * 0x%08lx..0x%08lx  : kasan shadow mem\n",
		KASAN_SHADOW_START, KASAN_SHADOW_END);
#endif
	pr_info("  * 0x%08lx..0x%08lx  : fixmap\n", FIXADDR_START, FIXADDR_TOP);
#ifdef CONFIG_HIGHMEM
	pr_info("  * 0x%08lx..0x%08lx  : highmem PTEs\n",
		PKMAP_BASE, PKMAP_ADDR(LAST_PKMAP));
#endif /* CONFIG_HIGHMEM */
	if (ioremap_bot != IOREMAP_TOP)
		pr_info("  * 0x%08lx..0x%08lx  : early ioremap\n",
			ioremap_bot, IOREMAP_TOP);
	pr_info("  * 0x%08lx..0x%08lx  : vmalloc & ioremap\n",
		VMALLOC_START, VMALLOC_END);
#endif /* CONFIG_PPC32 */
}

void free_initmem(void)
{
	ppc_md.progress = ppc_printk_progress;
	mark_initmem_nx();
	init_mem_is_free = true;
	free_initmem_default(POISON_FREE_INITMEM);
}

/**
 * flush_coherent_icache() - if a CPU has a coherent icache, flush it
 * @addr: The base address to use (can be any valid address, the whole cache will be flushed)
 * Return true if the cache was flushed, false otherwise
 */
static inline bool flush_coherent_icache(unsigned long addr)
{
	/*
	 * For a snooping icache, we still need a dummy icbi to purge all the
	 * prefetched instructions from the ifetch buffers. We also need a sync
	 * before the icbi to order the the actual stores to memory that might
	 * have modified instructions with the icbi.
	 */
	if (cpu_has_feature(CPU_FTR_COHERENT_ICACHE)) {
		mb(); /* sync */
		icbi((void *)addr);
		mb(); /* sync */
		isync();
		return true;
	}

	return false;
}

/**
 * invalidate_icache_range() - Flush the icache by issuing icbi across an address range
 * @start: the start address
 * @stop: the stop address (exclusive)
 */
static void invalidate_icache_range(unsigned long start, unsigned long stop)
{
	unsigned long shift = l1_icache_shift();
	unsigned long bytes = l1_icache_bytes();
	char *addr = (char *)(start & ~(bytes - 1));
	unsigned long size = stop - (unsigned long)addr + (bytes - 1);
	unsigned long i;

	for (i = 0; i < size >> shift; i++, addr += bytes)
		icbi(addr);

	mb(); /* sync */
	isync();
}

/**
 * flush_icache_range: Write any modified data cache blocks out to memory
 * and invalidate the corresponding blocks in the instruction cache
 *
 * Generic code will call this after writing memory, before executing from it.
 *
 * @start: the start address
 * @stop: the stop address (exclusive)
 */
void flush_icache_range(unsigned long start, unsigned long stop)
{
	if (flush_coherent_icache(start))
		return;

	clean_dcache_range(start, stop);

	if (IS_ENABLED(CONFIG_44x)) {
		/*
		 * Flash invalidate on 44x because we are passed kmapped
		 * addresses and this doesn't work for userspace pages due to
		 * the virtually tagged icache.
		 */
		iccci((void *)start);
		mb(); /* sync */
		isync();
	} else
		invalidate_icache_range(start, stop);
}
EXPORT_SYMBOL(flush_icache_range);

#if !defined(CONFIG_PPC_8xx) && !defined(CONFIG_PPC64)
/**
 * flush_dcache_icache_phys() - Flush a page by it's physical address
 * @physaddr: the physical address of the page
 */
static void flush_dcache_icache_phys(unsigned long physaddr)
{
	unsigned long bytes = l1_dcache_bytes();
	unsigned long nb = PAGE_SIZE / bytes;
	unsigned long addr = physaddr & PAGE_MASK;
	unsigned long msr, msr0;
	unsigned long loop1 = addr, loop2 = addr;

	msr0 = mfmsr();
	msr = msr0 & ~MSR_DR;
	/*
	 * This must remain as ASM to prevent potential memory accesses
	 * while the data MMU is disabled
	 */
	asm volatile(
		"   mtctr %2;\n"
		"   mtmsr %3;\n"
		"   isync;\n"
		"0: dcbst   0, %0;\n"
		"   addi    %0, %0, %4;\n"
		"   bdnz    0b;\n"
		"   sync;\n"
		"   mtctr %2;\n"
		"1: icbi    0, %1;\n"
		"   addi    %1, %1, %4;\n"
		"   bdnz    1b;\n"
		"   sync;\n"
		"   mtmsr %5;\n"
		"   isync;\n"
		: "+&r" (loop1), "+&r" (loop2)
		: "r" (nb), "r" (msr), "i" (bytes), "r" (msr0)
		: "ctr", "memory");
}
#endif // !defined(CONFIG_PPC_8xx) && !defined(CONFIG_PPC64)

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
#if defined(CONFIG_PPC_8xx) || defined(CONFIG_PPC64)
	/* On 8xx there is no need to kmap since highmem is not supported */
	__flush_dcache_icache(page_address(page));
#else
	if (IS_ENABLED(CONFIG_BOOKE) || sizeof(phys_addr_t) > sizeof(void *)) {
		void *start = kmap_atomic(page);
		__flush_dcache_icache(start);
		kunmap_atomic(start);
	} else {
		unsigned long addr = page_to_pfn(page) << PAGE_SHIFT;

		if (flush_coherent_icache(addr))
			return;
		flush_dcache_icache_phys(addr);
	}
#endif
}
EXPORT_SYMBOL(flush_dcache_icache_page);

/**
 * __flush_dcache_icache(): Flush a particular page from the data cache to RAM.
 * Note: this is necessary because the instruction cache does *not*
 * snoop from the data cache.
 *
 * @page: the address of the page to flush
 */
void __flush_dcache_icache(void *p)
{
	unsigned long addr = (unsigned long)p;

	if (flush_coherent_icache(addr))
		return;

	clean_dcache_range(addr, addr + PAGE_SIZE);

	/*
	 * We don't flush the icache on 44x. Those have a virtual icache and we
	 * don't have access to the virtual address here (it's not the page
	 * vaddr but where it's mapped in user space). The flushing of the
	 * icache on these is handled elsewhere, when a change in the address
	 * space occurs, before returning to user space.
	 */

	if (cpu_has_feature(MMU_FTR_TYPE_44x))
		return;

	invalidate_icache_range(addr, addr + PAGE_SIZE);
}

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
			res->flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
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
	if (page_is_rtas_user_buf(pfn))
		return 1;
	if (iomem_is_exclusive(PFN_PHYS(pfn)))
		return 0;
	if (!page_is_ram(pfn))
		return 1;
	return 0;
}
#endif /* CONFIG_STRICT_DEVMEM */

/*
 * This is defined in kernel/resource.c but only powerpc needs to export it, for
 * the EHEA driver. Drop this when drivers/net/ethernet/ibm/ehea is removed.
 */
EXPORT_SYMBOL_GPL(walk_system_ram_range);
