/*
 * Copyright (C) 2007-2008 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/dma-map-ops.h>
#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h> /* mem_init */
#include <linux/initrd.h>
#include <linux/of_fdt.h>
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

EXPORT_SYMBOL(min_low_pfn);
EXPORT_SYMBOL(max_low_pfn);

#ifdef CONFIG_HIGHMEM
static void __init highmem_init(void)
{
	pr_debug("%x\n", (u32)PKMAP_BASE);
	map_page(PKMAP_BASE, 0, 0);	/* XXX gross */
	pkmap_page_table = virt_to_kpte(PKMAP_BASE);
}
#endif /* CONFIG_HIGHMEM */

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
static void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];
	int idx;

	/* Setup fixmaps */
	for (idx = 0; idx < __end_of_fixed_addresses; idx++)
		clear_fixmap(idx);

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
	free_area_init(zones_size);
}

void __init setup_memory(void)
{
	/*
	 * Kernel:
	 * start: base phys address of kernel - page align
	 * end: base phys address of kernel - page align
	 *
	 * min_low_pfn - the first page (mm/bootmem.c - node_boot_start)
	 * max_low_pfn
	 */

	/* memory start is from the kernel end (aligned) to higher addr */
	min_low_pfn = memory_start >> PAGE_SHIFT; /* minimum for allocation */
	max_low_pfn = ((u64)memory_start + (u64)lowmem_size) >> PAGE_SHIFT;
	max_pfn = ((u64)memory_start + (u64)memory_size) >> PAGE_SHIFT;

	pr_info("%s: min_low_pfn: %#lx\n", __func__, min_low_pfn);
	pr_info("%s: max_low_pfn: %#lx\n", __func__, max_low_pfn);
	pr_info("%s: max_pfn: %#lx\n", __func__, max_pfn);

	paging_init();
}

void __init mem_init(void)
{
	mem_init_done = 1;
}

int page_is_ram(unsigned long pfn)
{
	return pfn < max_low_pfn;
}

/*
 * Check for command-line options that affect what MMU_init will do.
 */
static void __init mm_cmdline_setup(void)
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
		memblock_reserve(__virt_to_phys(initrd_start), size);
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

	parse_early_param();

	early_init_fdt_scan_reserved_mem();

	/* CMA initialization */
	dma_contiguous_reserve(memory_start + lowmem_size - 1);

	memblock_dump_all();
}

static const pgprot_t protection_map[16] = {
	[VM_NONE]					= PAGE_NONE,
	[VM_READ]					= PAGE_READONLY_X,
	[VM_WRITE]					= PAGE_COPY,
	[VM_WRITE | VM_READ]				= PAGE_COPY_X,
	[VM_EXEC]					= PAGE_READONLY,
	[VM_EXEC | VM_READ]				= PAGE_READONLY_X,
	[VM_EXEC | VM_WRITE]				= PAGE_COPY,
	[VM_EXEC | VM_WRITE | VM_READ]			= PAGE_COPY_X,
	[VM_SHARED]					= PAGE_NONE,
	[VM_SHARED | VM_READ]				= PAGE_READONLY_X,
	[VM_SHARED | VM_WRITE]				= PAGE_SHARED,
	[VM_SHARED | VM_WRITE | VM_READ]		= PAGE_SHARED_X,
	[VM_SHARED | VM_EXEC]				= PAGE_READONLY,
	[VM_SHARED | VM_EXEC | VM_READ]			= PAGE_READONLY_X,
	[VM_SHARED | VM_EXEC | VM_WRITE]		= PAGE_SHARED,
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= PAGE_SHARED_X
};
DECLARE_VM_GET_PAGE_PROT
