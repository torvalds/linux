/*
 * Copyright (C) 2007-2008 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/mm.h> /* mem_init */
#include <linux/initrd.h>
#include <linux/pagemap.h>
#include <linux/pfn.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/export.h>

#include <asm/page.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/tlb.h>
#include <asm/fixmap.h>

/* Use for MMU and noMMU because of PCI generic code */
int mem_init_done;

#ifndef CONFIG_MMU
unsigned int __page_offset;
EXPORT_SYMBOL(__page_offset);

#else
static int init_bootmem_done;
#endif /* CONFIG_MMU */

char *klimit = _end;

/*
 * Initialize the bootmem system and give it all the memory we
 * have available.
 */
unsigned long memory_start;
EXPORT_SYMBOL(memory_start);
unsigned long memory_size;
EXPORT_SYMBOL(memory_size);
unsigned long lowmem_size;

#ifdef CONFIG_HIGHMEM
pte_t *kmap_pte;
EXPORT_SYMBOL(kmap_pte);
pgprot_t kmap_prot;
EXPORT_SYMBOL(kmap_prot);

static inline pte_t *virt_to_kpte(unsigned long vaddr)
{
	return pte_offset_kernel(pmd_offset(pgd_offset_k(vaddr),
			vaddr), vaddr);
}

static void __init highmem_init(void)
{
	pr_debug("%x\n", (u32)PKMAP_BASE);
	map_page(PKMAP_BASE, 0, 0);	/* XXX gross */
	pkmap_page_table = virt_to_kpte(PKMAP_BASE);

	kmap_pte = virt_to_kpte(__fix_to_virt(FIX_KMAP_BEGIN));
	kmap_prot = PAGE_KERNEL;
}

static unsigned long highmem_setup(void)
{
	unsigned long pfn;
	unsigned long reservedpages = 0;

	for (pfn = max_low_pfn; pfn < max_pfn; ++pfn) {
		struct page *page = pfn_to_page(pfn);

		/* FIXME not sure about */
		if (memblock_is_reserved(pfn << PAGE_SHIFT))
			continue;
		free_highmem_page(page);
		reservedpages++;
	}
	pr_info("High memory: %luk\n",
					totalhigh_pages << (PAGE_SHIFT-10));

	return reservedpages;
}
#endif /* CONFIG_HIGHMEM */

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
static void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];
#ifdef CONFIG_MMU
	int idx;

	/* Setup fixmaps */
	for (idx = 0; idx < __end_of_fixed_addresses; idx++)
		clear_fixmap(idx);
#endif

	/* Clean every zones */
	memset(zones_size, 0, sizeof(zones_size));

#ifdef CONFIG_HIGHMEM
	highmem_init();

	zones_size[ZONE_DMA] = max_low_pfn;
	zones_size[ZONE_HIGHMEM] = max_pfn;
#else
	zones_size[ZONE_DMA] = max_pfn;
#endif

	/* We don't have holes in memory map */
	free_area_init_nodes(zones_size);
}

void __init setup_memory(void)
{
	unsigned long map_size;
	struct memblock_region *reg;

#ifndef CONFIG_MMU
	u32 kernel_align_start, kernel_align_size;

	/* Find main memory where is the kernel */
	for_each_memblock(memory, reg) {
		memory_start = (u32)reg->base;
		lowmem_size = reg->size;
		if ((memory_start <= (u32)_text) &&
			((u32)_text <= (memory_start + lowmem_size - 1))) {
			memory_size = lowmem_size;
			PAGE_OFFSET = memory_start;
			pr_info("%s: Main mem: 0x%x, size 0x%08x\n",
				__func__, (u32) memory_start,
					(u32) memory_size);
			break;
		}
	}

	if (!memory_start || !memory_size) {
		panic("%s: Missing memory setting 0x%08x, size=0x%08x\n",
			__func__, (u32) memory_start, (u32) memory_size);
	}

	/* reservation of region where is the kernel */
	kernel_align_start = PAGE_DOWN((u32)_text);
	/* ALIGN can be remove because _end in vmlinux.lds.S is align */
	kernel_align_size = PAGE_UP((u32)klimit) - kernel_align_start;
	pr_info("%s: kernel addr:0x%08x-0x%08x size=0x%08x\n",
		__func__, kernel_align_start, kernel_align_start
			+ kernel_align_size, kernel_align_size);
	memblock_reserve(kernel_align_start, kernel_align_size);
#endif
	/*
	 * Kernel:
	 * start: base phys address of kernel - page align
	 * end: base phys address of kernel - page align
	 *
	 * min_low_pfn - the first page (mm/bootmem.c - node_boot_start)
	 * max_low_pfn
	 * max_mapnr - the first unused page (mm/bootmem.c - node_low_pfn)
	 * num_physpages - number of all pages
	 */

	/* memory start is from the kernel end (aligned) to higher addr */
	min_low_pfn = memory_start >> PAGE_SHIFT; /* minimum for allocation */
	/* RAM is assumed contiguous */
	num_physpages = max_mapnr = memory_size >> PAGE_SHIFT;
	max_low_pfn = ((u64)memory_start + (u64)lowmem_size) >> PAGE_SHIFT;
	max_pfn = ((u64)memory_start + (u64)memory_size) >> PAGE_SHIFT;

	pr_info("%s: max_mapnr: %#lx\n", __func__, max_mapnr);
	pr_info("%s: min_low_pfn: %#lx\n", __func__, min_low_pfn);
	pr_info("%s: max_low_pfn: %#lx\n", __func__, max_low_pfn);
	pr_info("%s: max_pfn: %#lx\n", __func__, max_pfn);

	/*
	 * Find an area to use for the bootmem bitmap.
	 * We look for the first area which is at least
	 * 128kB in length (128kB is enough for a bitmap
	 * for 4GB of memory, using 4kB pages), plus 1 page
	 * (in case the address isn't page-aligned).
	 */
	map_size = init_bootmem_node(NODE_DATA(0),
		PFN_UP(TOPHYS((u32)klimit)), min_low_pfn, max_low_pfn);
	memblock_reserve(PFN_UP(TOPHYS((u32)klimit)) << PAGE_SHIFT, map_size);

	/* Add active regions with valid PFNs */
	for_each_memblock(memory, reg) {
		unsigned long start_pfn, end_pfn;

		start_pfn = memblock_region_memory_base_pfn(reg);
		end_pfn = memblock_region_memory_end_pfn(reg);
		memblock_set_node(start_pfn << PAGE_SHIFT,
					(end_pfn - start_pfn) << PAGE_SHIFT, 0);
	}

	/* free bootmem is whole main memory */
	free_bootmem_with_active_regions(0, max_low_pfn);

	/* reserve allocate blocks */
	for_each_memblock(reserved, reg) {
		unsigned long top = reg->base + reg->size - 1;

		pr_debug("reserved - 0x%08x-0x%08x, %lx, %lx\n",
			 (u32) reg->base, (u32) reg->size, top,
						memory_start + lowmem_size - 1);

		if (top <= (memory_start + lowmem_size - 1)) {
			reserve_bootmem(reg->base, reg->size, BOOTMEM_DEFAULT);
		} else if (reg->base < (memory_start + lowmem_size - 1)) {
			unsigned long trunc_size = memory_start + lowmem_size -
								reg->base;
			reserve_bootmem(reg->base, trunc_size, BOOTMEM_DEFAULT);
		}
	}

	/* XXX need to clip this if using highmem? */
	sparse_memory_present_with_active_regions(0);

#ifdef CONFIG_MMU
	init_bootmem_done = 1;
#endif
	paging_init();
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, 0, "initrd");
}
#endif

void free_initmem(void)
{
	free_initmem_default(0);
}

void __init mem_init(void)
{
	pg_data_t *pgdat;
	unsigned long reservedpages = 0, codesize, initsize, datasize, bsssize;

	high_memory = (void *)__va(memory_start + lowmem_size - 1);

	/* this will put all memory onto the freelists */
	totalram_pages += free_all_bootmem();

	for_each_online_pgdat(pgdat) {
		unsigned long i;
		struct page *page;

		for (i = 0; i < pgdat->node_spanned_pages; i++) {
			if (!pfn_valid(pgdat->node_start_pfn + i))
				continue;
			page = pgdat_page_nr(pgdat, i);
			if (PageReserved(page))
				reservedpages++;
		}
	}

#ifdef CONFIG_HIGHMEM
	reservedpages -= highmem_setup();
#endif

	codesize = (unsigned long)&_sdata - (unsigned long)&_stext;
	datasize = (unsigned long)&_edata - (unsigned long)&_sdata;
	initsize = (unsigned long)&__init_end - (unsigned long)&__init_begin;
	bsssize = (unsigned long)&__bss_stop - (unsigned long)&__bss_start;

	pr_info("Memory: %luk/%luk available (%luk kernel code, ",
		nr_free_pages() << (PAGE_SHIFT-10),
		num_physpages << (PAGE_SHIFT-10),
		codesize >> 10);
	pr_cont("%luk reserved, %luk data, %luk bss, %luk init)\n",
		reservedpages << (PAGE_SHIFT-10),
		datasize >> 10,
		bsssize >> 10,
		initsize >> 10);

#ifdef CONFIG_MMU
	pr_info("Kernel virtual memory layout:\n");
	pr_info("  * 0x%08lx..0x%08lx  : fixmap\n", FIXADDR_START, FIXADDR_TOP);
#ifdef CONFIG_HIGHMEM
	pr_info("  * 0x%08lx..0x%08lx  : highmem PTEs\n",
		PKMAP_BASE, PKMAP_ADDR(LAST_PKMAP));
#endif /* CONFIG_HIGHMEM */
	pr_info("  * 0x%08lx..0x%08lx  : early ioremap\n",
		ioremap_bot, ioremap_base);
	pr_info("  * 0x%08lx..0x%08lx  : vmalloc & ioremap\n",
		(unsigned long)VMALLOC_START, VMALLOC_END);
#endif
	mem_init_done = 1;
}

#ifndef CONFIG_MMU
int page_is_ram(unsigned long pfn)
{
	return __range_ok(pfn, 0);
}
#else
int page_is_ram(unsigned long pfn)
{
	return pfn < max_low_pfn;
}

/*
 * Check for command-line options that affect what MMU_init will do.
 */
static void mm_cmdline_setup(void)
{
	unsigned long maxmem = 0;
	char *p = cmd_line;

	/* Look for mem= option on command line */
	p = strstr(cmd_line, "mem=");
	if (p) {
		p += 4;
		maxmem = memparse(p, &p);
		if (maxmem && memory_size > maxmem) {
			memory_size = maxmem;
			memblock.memory.regions[0].size = memory_size;
		}
	}
}

/*
 * MMU_init_hw does the chip-specific initialization of the MMU hardware.
 */
static void __init mmu_init_hw(void)
{
	/*
	 * The Zone Protection Register (ZPR) defines how protection will
	 * be applied to every page which is a member of a given zone. At
	 * present, we utilize only two of the zones.
	 * The zone index bits (of ZSEL) in the PTE are used for software
	 * indicators, except the LSB.  For user access, zone 1 is used,
	 * for kernel access, zone 0 is used.  We set all but zone 1
	 * to zero, allowing only kernel access as indicated in the PTE.
	 * For zone 1, we set a 01 binary (a value of 10 will not work)
	 * to allow user access as indicated in the PTE.  This also allows
	 * kernel access as indicated in the PTE.
	 */
	__asm__ __volatile__ ("ori r11, r0, 0x10000000;" \
			"mts rzpr, r11;"
			: : : "r11");
}

/*
 * MMU_init sets up the basic memory mappings for the kernel,
 * including both RAM and possibly some I/O regions,
 * and sets up the page tables and the MMU hardware ready to go.
 */

/* called from head.S */
asmlinkage void __init mmu_init(void)
{
	unsigned int kstart, ksize;

	if (!memblock.reserved.cnt) {
		pr_emerg("Error memory count\n");
		machine_restart(NULL);
	}

	if ((u32) memblock.memory.regions[0].size < 0x400000) {
		pr_emerg("Memory must be greater than 4MB\n");
		machine_restart(NULL);
	}

	if ((u32) memblock.memory.regions[0].size < kernel_tlb) {
		pr_emerg("Kernel size is greater than memory node\n");
		machine_restart(NULL);
	}

	/* Find main memory where the kernel is */
	memory_start = (u32) memblock.memory.regions[0].base;
	lowmem_size = memory_size = (u32) memblock.memory.regions[0].size;

	if (lowmem_size > CONFIG_LOWMEM_SIZE) {
		lowmem_size = CONFIG_LOWMEM_SIZE;
#ifndef CONFIG_HIGHMEM
		memory_size = lowmem_size;
#endif
	}

	mm_cmdline_setup(); /* FIXME parse args from command line - not used */

	/*
	 * Map out the kernel text/data/bss from the available physical
	 * memory.
	 */
	kstart = __pa(CONFIG_KERNEL_START); /* kernel start */
	/* kernel size */
	ksize = PAGE_ALIGN(((u32)_end - (u32)CONFIG_KERNEL_START));
	memblock_reserve(kstart, ksize);

#if defined(CONFIG_BLK_DEV_INITRD)
	/* Remove the init RAM disk from the available memory. */
	if (initrd_start) {
		unsigned long size;
		size = initrd_end - initrd_start;
		memblock_reserve(virt_to_phys(initrd_start), size);
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	/* Initialize the MMU hardware */
	mmu_init_hw();

	/* Map in all of RAM starting at CONFIG_KERNEL_START */
	mapin_ram();

	/* Extend vmalloc and ioremap area as big as possible */
#ifdef CONFIG_HIGHMEM
	ioremap_base = ioremap_bot = PKMAP_BASE;
#else
	ioremap_base = ioremap_bot = FIXADDR_START;
#endif

	/* Initialize the context management stuff */
	mmu_context_init();

	/* Shortly after that, the entire linear mapping will be available */
	/* This will also cause that unflatten device tree will be allocated
	 * inside 768MB limit */
	memblock_set_current_limit(memory_start + lowmem_size - 1);
}

/* This is only called until mem_init is done. */
void __init *early_get_page(void)
{
	void *p;
	if (init_bootmem_done) {
		p = alloc_bootmem_pages(PAGE_SIZE);
	} else {
		/*
		 * Mem start + kernel_tlb -> here is limit
		 * because of mem mapping from head.S
		 */
		p = __va(memblock_alloc_base(PAGE_SIZE, PAGE_SIZE,
					memory_start + kernel_tlb));
	}
	return p;
}

#endif /* CONFIG_MMU */

void * __init_refok alloc_maybe_bootmem(size_t size, gfp_t mask)
{
	if (mem_init_done)
		return kmalloc(size, mask);
	else
		return alloc_bootmem(size);
}

void * __init_refok zalloc_maybe_bootmem(size_t size, gfp_t mask)
{
	void *p;

	if (mem_init_done)
		p = kzalloc(size, mask);
	else {
		p = alloc_bootmem(size);
		if (p)
			memset(p, 0, size);
	}
	return p;
}
