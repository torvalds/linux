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

#include <linux/memblock.h>
#include <linux/highmem.h>
#include <linux/suspend.h>
#include <linux/dma-direct.h>

#include <asm/machdep.h>
#include <asm/rtas.h>
#include <asm/kasan.h>
#include <asm/sparsemem.h>
#include <asm/svm.h>

#include <mm/mmu_decl.h>

unsigned long long memory_limit;
bool init_mem_is_free;

unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)] __page_aligned_bss;
EXPORT_SYMBOL(empty_zero_page);

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
static DEFINE_MUTEX(linear_mapping_mutex);

#ifdef CONFIG_NUMA
int memory_add_physaddr_to_nid(u64 start)
{
	return hot_add_scn_to_nid(start);
}
#endif

int __weak create_section_mapping(unsigned long start, unsigned long end,
				  int nid, pgprot_t prot)
{
	return -ENODEV;
}

int __weak remove_section_mapping(unsigned long start, unsigned long end)
{
	return -ENODEV;
}

int __ref arch_create_linear_mapping(int nid, u64 start, u64 size,
				     struct mhp_params *params)
{
	int rc;

	start = (unsigned long)__va(start);
	mutex_lock(&linear_mapping_mutex);
	rc = create_section_mapping(start, start + size, nid,
				    params->pgprot);
	mutex_unlock(&linear_mapping_mutex);
	if (rc) {
		pr_warn("Unable to create linear mapping for 0x%llx..0x%llx: %d\n",
			start, start + size, rc);
		return -EFAULT;
	}
	return 0;
}

void __ref arch_remove_linear_mapping(u64 start, u64 size)
{
	int ret;

	/* Remove htab bolted mappings for this section of memory */
	start = (unsigned long)__va(start);

	mutex_lock(&linear_mapping_mutex);
	ret = remove_section_mapping(start, start + size);
	mutex_unlock(&linear_mapping_mutex);
	if (ret)
		pr_warn("Unable to remove linear mapping for 0x%llx..0x%llx: %d\n",
			start, start + size, ret);

	/* Ensure all vmalloc mappings are flushed in case they also
	 * hit that section of memory
	 */
	vm_unmap_aliases();
}

int __ref arch_add_memory(int nid, u64 start, u64 size,
			  struct mhp_params *params)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;
	int rc;

	rc = arch_create_linear_mapping(nid, start, size, params);
	if (rc)
		return rc;
	rc = __add_pages(nid, start_pfn, nr_pages, params);
	if (rc)
		arch_remove_linear_mapping(start, size);
	return rc;
}

void __ref arch_remove_memory(int nid, u64 start, u64 size,
			      struct vmem_altmap *altmap)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;

	__remove_pages(start_pfn, nr_pages, altmap);
	arch_remove_linear_mapping(start, size);
}
#endif

#ifndef CONFIG_NUMA
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
	sparse_init();
}

/* mark pages that don't exist as nosave */
static int __init mark_nonram_nosave(void)
{
	unsigned long spfn, epfn, prev = 0;
	int i;

	for_each_mem_pfn_range(i, MAX_NUMNODES, &spfn, &epfn, NULL) {
		if (prev && prev < spfn)
			register_nosave_region(prev, spfn);

		prev = epfn;
	}

	return 0;
}
#else /* CONFIG_NUMA */
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

	free_area_init(max_zone_pfns);

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
	if (is_secure_guest())
		svm_swiotlb_init();
	else
		swiotlb_init(0);
#endif

	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE);
	set_max_mapnr(max_pfn);

	kasan_late_init();

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
#ifdef MODULES_VADDR
	pr_info("  * 0x%08lx..0x%08lx  : modules\n",
		MODULES_VADDR, MODULES_END);
#endif
#endif /* CONFIG_PPC32 */
}

void free_initmem(void)
{
	ppc_md.progress = ppc_printk_progress;
	mark_initmem_nx();
	init_mem_is_free = true;
	free_initmem_default(POISON_FREE_INITMEM);
}

/*
 * System memory should not be in /proc/iomem but various tools expect it
 * (eg kdump).
 */
static int __init add_system_ram_resources(void)
{
	phys_addr_t start, end;
	u64 i;

	for_each_mem_range(i, &start, &end) {
		struct resource *res;

		res = kzalloc(sizeof(struct resource), GFP_KERNEL);
		WARN_ON(!res);

		if (res) {
			res->name = "System RAM";
			res->start = start;
			/*
			 * In memblock, end points to the first byte after
			 * the range while in resourses, end points to the
			 * last byte in the range.
			 */
			res->end = end - 1;
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
