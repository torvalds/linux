/*
 * Copyright (C) 2007-2008 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/kernel.h>
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

static void highmem_setup(void)
{
	unsigned long pfn;

	for (pfn = max_low_pfn; pfn < max_pfn; ++pfn) {
		struct page *page = pfn_to_page(pfn);

		/* FIXME not sure about */
		if (!memblock_is_reserved(pfn << PAGE_SHIFT))
			free_highmem_page(page);
	}
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
	 */

	/* memory start is from the kernel end (aligned) to higher addr */
	min_low_pfn = memory_start >> PAGE_SHIFT; /* minimum for allocation */
	/* RAM is assumed contiguous */
	max_mapnr = memory_size >> PAGE_SHIFT;
	max_low_pfn = ((u64)memory_start + (u64)lowmem_size) >> PAGE_SHIFT;
	max_pfn = ((u64)memory_start + (u64)memory_size) >> PAGE_SHIFT;

	pr_info("%s: max_mapnr: %#lx\n", __func__, max_mapnr);
	pr_info("%s: min_low_pfn: %#lx\n", __func__, min_low_pfn);
	pr_info("%s: max_low_pfn: %#lx\n", __func__, max_low_pfn);
	pr_info("%s: max_pfn: %#lx\n", __func__, max_pfn);

	/* Add active regions with valid PFNs */
	for_each_memblock(memory, reg) {
		unsigned long start_pfn, end_pfn;

		start_pfn = memblock_region_memory_base_pfn(reg);
		end_pfn = memblock_region_memory_end_pfn(reg);
		memblock_set_node(start_pfn << PAGE_SHIFT,
				  (end_pfn - start_pfn) << PAGE_SHIFT,
				  &memblock.memory, 0);
	}

	/* XXX need to clip this if using highmem? */
	sparse_memory_present_with_active_regions(0);

	paging_init();
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, -1, "initrd");
}
#endif

void free_initmem(void)
{
	free_initmem_default(-1);
}

void __init mem_init(void)
{
	high_memory = (void *)__va(memory_start + lowmem_size - 1);

	/* this will put all memory onto the freelists */
	memblock_free_all();
#ifdef CONFIG_HIGHMEM
	highmem_setup();
#endif

	mem_init_print_info(NULL);
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
}

/* This is only called until mem_init is done. */
void __init *early_get_page(void)
{
	/*
	 * Mem start + kernel_tlb -> here is limit
	 * because of mem mapping from head.S
	 */
	return __va(memblock_alloc_base(PAGE_SIZE, PAGE_SIZE,
				memory_start + kernel_tlb));
}

#endif /* CONFIG_MMU */

void * __ref zalloc_maybe_bootmem(size_t size, gfp_t mask)
{
	void *p;

	if (mem_init_done)
		p = kzalloc(size, mask);
	else {
		p = memblock_alloc(size, 0);
		if (p)
			memset(p, 0, size);
	}
	return p;
}
