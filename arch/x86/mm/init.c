#include <linux/gfp.h>
#include <linux/initrd.h>
#include <linux/ioport.h>
#include <linux/swap.h>
#include <linux/memblock.h>
#include <linux/bootmem.h>	/* for max_low_pfn */

#include <asm/cacheflush.h>
#include <asm/e820.h>
#include <asm/init.h>
#include <asm/page.h>
#include <asm/page_types.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/proto.h>
#include <asm/dma.h>		/* for MAX_DMA_PFN */

unsigned long __initdata pgt_buf_start;
unsigned long __meminitdata pgt_buf_end;
unsigned long __meminitdata pgt_buf_top;

int after_bootmem;

int direct_gbpages
#ifdef CONFIG_DIRECT_GBPAGES
				= 1
#endif
;

struct map_range {
	unsigned long start;
	unsigned long end;
	unsigned page_size_mask;
};

static int page_size_mask;

static void __init probe_page_size_mask(void)
{
#if !defined(CONFIG_DEBUG_PAGEALLOC) && !defined(CONFIG_KMEMCHECK)
	/*
	 * For CONFIG_DEBUG_PAGEALLOC, identity mapping will use small pages.
	 * This will simplify cpa(), which otherwise needs to support splitting
	 * large pages into small in interrupt context, etc.
	 */
	if (direct_gbpages)
		page_size_mask |= 1 << PG_LEVEL_1G;
	if (cpu_has_pse)
		page_size_mask |= 1 << PG_LEVEL_2M;
#endif

	/* Enable PSE if available */
	if (cpu_has_pse)
		set_in_cr4(X86_CR4_PSE);

	/* Enable PGE if available */
	if (cpu_has_pge) {
		set_in_cr4(X86_CR4_PGE);
		__supported_pte_mask |= _PAGE_GLOBAL;
	}
}
void __init native_pagetable_reserve(u64 start, u64 end)
{
	memblock_reserve(start, end - start);
}

#ifdef CONFIG_X86_32
#define NR_RANGE_MR 3
#else /* CONFIG_X86_64 */
#define NR_RANGE_MR 5
#endif

static int __meminit save_mr(struct map_range *mr, int nr_range,
			     unsigned long start_pfn, unsigned long end_pfn,
			     unsigned long page_size_mask)
{
	if (start_pfn < end_pfn) {
		if (nr_range >= NR_RANGE_MR)
			panic("run out of range for init_memory_mapping\n");
		mr[nr_range].start = start_pfn<<PAGE_SHIFT;
		mr[nr_range].end   = end_pfn<<PAGE_SHIFT;
		mr[nr_range].page_size_mask = page_size_mask;
		nr_range++;
	}

	return nr_range;
}

static int __meminit split_mem_range(struct map_range *mr, int nr_range,
				     unsigned long start,
				     unsigned long end)
{
	unsigned long start_pfn, end_pfn;
	unsigned long pos;
	int i;

	/* head if not big page alignment ? */
	start_pfn = start >> PAGE_SHIFT;
	pos = start_pfn << PAGE_SHIFT;
#ifdef CONFIG_X86_32
	/*
	 * Don't use a large page for the first 2/4MB of memory
	 * because there are often fixed size MTRRs in there
	 * and overlapping MTRRs into large pages can cause
	 * slowdowns.
	 */
	if (pos == 0)
		end_pfn = 1<<(PMD_SHIFT - PAGE_SHIFT);
	else
		end_pfn = ((pos + (PMD_SIZE - 1))>>PMD_SHIFT)
				 << (PMD_SHIFT - PAGE_SHIFT);
#else /* CONFIG_X86_64 */
	end_pfn = ((pos + (PMD_SIZE - 1)) >> PMD_SHIFT)
			<< (PMD_SHIFT - PAGE_SHIFT);
#endif
	if (end_pfn > (end >> PAGE_SHIFT))
		end_pfn = end >> PAGE_SHIFT;
	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn, 0);
		pos = end_pfn << PAGE_SHIFT;
	}

	/* big page (2M) range */
	start_pfn = ((pos + (PMD_SIZE - 1))>>PMD_SHIFT)
			 << (PMD_SHIFT - PAGE_SHIFT);
#ifdef CONFIG_X86_32
	end_pfn = (end>>PMD_SHIFT) << (PMD_SHIFT - PAGE_SHIFT);
#else /* CONFIG_X86_64 */
	end_pfn = ((pos + (PUD_SIZE - 1))>>PUD_SHIFT)
			 << (PUD_SHIFT - PAGE_SHIFT);
	if (end_pfn > ((end>>PMD_SHIFT)<<(PMD_SHIFT - PAGE_SHIFT)))
		end_pfn = ((end>>PMD_SHIFT)<<(PMD_SHIFT - PAGE_SHIFT));
#endif

	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn,
				page_size_mask & (1<<PG_LEVEL_2M));
		pos = end_pfn << PAGE_SHIFT;
	}

#ifdef CONFIG_X86_64
	/* big page (1G) range */
	start_pfn = ((pos + (PUD_SIZE - 1))>>PUD_SHIFT)
			 << (PUD_SHIFT - PAGE_SHIFT);
	end_pfn = (end >> PUD_SHIFT) << (PUD_SHIFT - PAGE_SHIFT);
	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn,
				page_size_mask &
				 ((1<<PG_LEVEL_2M)|(1<<PG_LEVEL_1G)));
		pos = end_pfn << PAGE_SHIFT;
	}

	/* tail is not big page (1G) alignment */
	start_pfn = ((pos + (PMD_SIZE - 1))>>PMD_SHIFT)
			 << (PMD_SHIFT - PAGE_SHIFT);
	end_pfn = (end >> PMD_SHIFT) << (PMD_SHIFT - PAGE_SHIFT);
	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn,
				page_size_mask & (1<<PG_LEVEL_2M));
		pos = end_pfn << PAGE_SHIFT;
	}
#endif

	/* tail is not big page (2M) alignment */
	start_pfn = pos>>PAGE_SHIFT;
	end_pfn = end>>PAGE_SHIFT;
	nr_range = save_mr(mr, nr_range, start_pfn, end_pfn, 0);

	/* try to merge same page size and continuous */
	for (i = 0; nr_range > 1 && i < nr_range - 1; i++) {
		unsigned long old_start;
		if (mr[i].end != mr[i+1].start ||
		    mr[i].page_size_mask != mr[i+1].page_size_mask)
			continue;
		/* move it */
		old_start = mr[i].start;
		memmove(&mr[i], &mr[i+1],
			(nr_range - 1 - i) * sizeof(struct map_range));
		mr[i--].start = old_start;
		nr_range--;
	}

	for (i = 0; i < nr_range; i++)
		printk(KERN_DEBUG " [mem %#010lx-%#010lx] page %s\n",
				mr[i].start, mr[i].end - 1,
			(mr[i].page_size_mask & (1<<PG_LEVEL_1G))?"1G":(
			 (mr[i].page_size_mask & (1<<PG_LEVEL_2M))?"2M":"4k"));

	return nr_range;
}

/*
 * First calculate space needed for kernel direct mapping page tables to cover
 * mr[0].start to mr[nr_range - 1].end, while accounting for possible 2M and 1GB
 * pages. Then find enough contiguous space for those page tables.
 */
static void __init find_early_table_space(unsigned long start, unsigned long end)
{
	int i;
	unsigned long puds = 0, pmds = 0, ptes = 0, tables;
	unsigned long good_end;
	phys_addr_t base;
	struct map_range mr[NR_RANGE_MR];
	int nr_range;

	memset(mr, 0, sizeof(mr));
	nr_range = 0;
	nr_range = split_mem_range(mr, nr_range, start, end);

	for (i = 0; i < nr_range; i++) {
		unsigned long range, extra;

		range = mr[i].end - mr[i].start;
		puds += (range + PUD_SIZE - 1) >> PUD_SHIFT;

		if (mr[i].page_size_mask & (1 << PG_LEVEL_1G)) {
			extra = range - ((range >> PUD_SHIFT) << PUD_SHIFT);
			pmds += (extra + PMD_SIZE - 1) >> PMD_SHIFT;
		} else {
			pmds += (range + PMD_SIZE - 1) >> PMD_SHIFT;
		}

		if (mr[i].page_size_mask & (1 << PG_LEVEL_2M)) {
			extra = range - ((range >> PMD_SHIFT) << PMD_SHIFT);
#ifdef CONFIG_X86_32
			extra += PMD_SIZE;
#endif
			ptes += (extra + PAGE_SIZE - 1) >> PAGE_SHIFT;
		} else {
			ptes += (range + PAGE_SIZE - 1) >> PAGE_SHIFT;
		}
	}

	tables = roundup(puds * sizeof(pud_t), PAGE_SIZE);
	tables += roundup(pmds * sizeof(pmd_t), PAGE_SIZE);
	tables += roundup(ptes * sizeof(pte_t), PAGE_SIZE);

#ifdef CONFIG_X86_32
	/* for fixmap */
	tables += roundup(__end_of_fixed_addresses * sizeof(pte_t), PAGE_SIZE);
	good_end = max_pfn_mapped << PAGE_SHIFT;
#endif

	base = memblock_find_in_range(start, good_end, tables, PAGE_SIZE);
	if (!base)
		panic("Cannot find space for the kernel page tables");

	pgt_buf_start = base >> PAGE_SHIFT;
	pgt_buf_end = pgt_buf_start;
	pgt_buf_top = pgt_buf_start + (tables >> PAGE_SHIFT);

	printk(KERN_DEBUG "kernel direct mapping tables up to %#lx @ [mem %#010lx-%#010lx]\n",
		mr[nr_range - 1].end - 1, pgt_buf_start << PAGE_SHIFT,
		(pgt_buf_top << PAGE_SHIFT) - 1);
}

/*
 * Setup the direct mapping of the physical memory at PAGE_OFFSET.
 * This runs before bootmem is initialized and gets pages directly from
 * the physical memory. To access them they are temporarily mapped.
 */
unsigned long __init_refok init_memory_mapping(unsigned long start,
					       unsigned long end)
{
	struct map_range mr[NR_RANGE_MR];
	unsigned long ret = 0;
	int nr_range, i;

	pr_info("init_memory_mapping: [mem %#010lx-%#010lx]\n",
	       start, end - 1);

	memset(mr, 0, sizeof(mr));
	nr_range = split_mem_range(mr, 0, start, end);

	for (i = 0; i < nr_range; i++)
		ret = kernel_physical_mapping_init(mr[i].start, mr[i].end,
						   mr[i].page_size_mask);

#ifdef CONFIG_X86_32
	early_ioremap_page_table_range_init();

	load_cr3(swapper_pg_dir);
#endif

	__flush_tlb_all();

	return ret >> PAGE_SHIFT;
}

void __init init_mem_mapping(void)
{
	probe_page_size_mask();

	/*
	 * Find space for the kernel direct mapping tables.
	 *
	 * Later we should allocate these tables in the local node of the
	 * memory mapped. Unfortunately this is done currently before the
	 * nodes are discovered.
	 */
#ifdef CONFIG_X86_64
	find_early_table_space(0, max_pfn<<PAGE_SHIFT);
#else
	find_early_table_space(0, max_low_pfn<<PAGE_SHIFT);
#endif
	max_low_pfn_mapped = init_memory_mapping(0, max_low_pfn<<PAGE_SHIFT);
	max_pfn_mapped = max_low_pfn_mapped;

#ifdef CONFIG_X86_64
	if (max_pfn > max_low_pfn) {
		max_pfn_mapped = init_memory_mapping(1UL<<32,
						     max_pfn<<PAGE_SHIFT);
		/* can we preseve max_low_pfn ?*/
		max_low_pfn = max_pfn;
	}
#endif
	/*
	 * Reserve the kernel pagetable pages we used (pgt_buf_start -
	 * pgt_buf_end) and free the other ones (pgt_buf_end - pgt_buf_top)
	 * so that they can be reused for other purposes.
	 *
	 * On native it just means calling memblock_reserve, on Xen it also
	 * means marking RW the pagetable pages that we allocated before
	 * but that haven't been used.
	 *
	 * In fact on xen we mark RO the whole range pgt_buf_start -
	 * pgt_buf_top, because we have to make sure that when
	 * init_memory_mapping reaches the pagetable pages area, it maps
	 * RO all the pagetable pages, including the ones that are beyond
	 * pgt_buf_end at that time.
	 */
	if (pgt_buf_end > pgt_buf_start)
		x86_init.mapping.pagetable_reserve(PFN_PHYS(pgt_buf_start),
				PFN_PHYS(pgt_buf_end));

	/* stop the wrong using */
	pgt_buf_top = 0;

	early_memtest(0, max_pfn_mapped << PAGE_SHIFT);
}

/*
 * devmem_is_allowed() checks to see if /dev/mem access to a certain address
 * is valid. The argument is a physical page number.
 *
 *
 * On x86, access has to be given to the first megabyte of ram because that area
 * contains bios code and data regions used by X and dosemu and similar apps.
 * Access has to be given to non-kernel-ram areas as well, these contain the PCI
 * mmio resources as well as potential bios/acpi data regions.
 */
int devmem_is_allowed(unsigned long pagenr)
{
	if (pagenr < 256)
		return 1;
	if (iomem_is_exclusive(pagenr << PAGE_SHIFT))
		return 0;
	if (!page_is_ram(pagenr))
		return 1;
	return 0;
}

void free_init_pages(char *what, unsigned long begin, unsigned long end)
{
	unsigned long addr;
	unsigned long begin_aligned, end_aligned;

	/* Make sure boundaries are page aligned */
	begin_aligned = PAGE_ALIGN(begin);
	end_aligned   = end & PAGE_MASK;

	if (WARN_ON(begin_aligned != begin || end_aligned != end)) {
		begin = begin_aligned;
		end   = end_aligned;
	}

	if (begin >= end)
		return;

	addr = begin;

	/*
	 * If debugging page accesses then do not free this memory but
	 * mark them not present - any buggy init-section access will
	 * create a kernel page fault:
	 */
#ifdef CONFIG_DEBUG_PAGEALLOC
	printk(KERN_INFO "debug: unmapping init [mem %#010lx-%#010lx]\n",
		begin, end - 1);
	set_memory_np(begin, (end - begin) >> PAGE_SHIFT);
#else
	/*
	 * We just marked the kernel text read only above, now that
	 * we are going to free part of that, we need to make that
	 * writeable and non-executable first.
	 */
	set_memory_nx(begin, (end - begin) >> PAGE_SHIFT);
	set_memory_rw(begin, (end - begin) >> PAGE_SHIFT);

	printk(KERN_INFO "Freeing %s: %luk freed\n", what, (end - begin) >> 10);

	for (; addr < end; addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		memset((void *)addr, POISON_FREE_INITMEM, PAGE_SIZE);
		free_page(addr);
		totalram_pages++;
	}
#endif
}

void free_initmem(void)
{
	free_init_pages("unused kernel memory",
			(unsigned long)(&__init_begin),
			(unsigned long)(&__init_end));
}

#ifdef CONFIG_BLK_DEV_INITRD
void __init free_initrd_mem(unsigned long start, unsigned long end)
{
	/*
	 * end could be not aligned, and We can not align that,
	 * decompresser could be confused by aligned initrd_end
	 * We already reserve the end partial page before in
	 *   - i386_start_kernel()
	 *   - x86_64_start_kernel()
	 *   - relocate_initrd()
	 * So here We can do PAGE_ALIGN() safely to get partial page to be freed
	 */
	free_init_pages("initrd memory", start, PAGE_ALIGN(end));
}
#endif

void __init zone_sizes_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));

#ifdef CONFIG_ZONE_DMA
	max_zone_pfns[ZONE_DMA]		= MAX_DMA_PFN;
#endif
#ifdef CONFIG_ZONE_DMA32
	max_zone_pfns[ZONE_DMA32]	= MAX_DMA32_PFN;
#endif
	max_zone_pfns[ZONE_NORMAL]	= max_low_pfn;
#ifdef CONFIG_HIGHMEM
	max_zone_pfns[ZONE_HIGHMEM]	= max_pfn;
#endif

	free_area_init_nodes(max_zone_pfns);
}

