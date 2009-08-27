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
#include <linux/lmb.h>
#include <linux/mm.h> /* mem_init */
#include <linux/initrd.h>
#include <linux/pagemap.h>
#include <linux/pfn.h>
#include <linux/swap.h>

#include <asm/page.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/tlb.h>

#ifndef CONFIG_MMU
unsigned int __page_offset;
EXPORT_SYMBOL(__page_offset);

#else
DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

int mem_init_done;
static int init_bootmem_done;
#endif /* CONFIG_MMU */

char *klimit = _end;

/*
 * Initialize the bootmem system and give it all the memory we
 * have available.
 */
unsigned long memory_start;
unsigned long memory_end; /* due to mm/nommu.c */
unsigned long memory_size;

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
static void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];

	/* Clean every zones */
	memset(zones_size, 0, sizeof(zones_size));

	/*
	 * old: we can DMA to/from any address.put all page into ZONE_DMA
	 * We use only ZONE_NORMAL
	 */
	zones_size[ZONE_NORMAL] = max_mapnr;

	free_area_init(zones_size);
}

void __init setup_memory(void)
{
	int i;
	unsigned long map_size;
#ifndef CONFIG_MMU
	u32 kernel_align_start, kernel_align_size;

	/* Find main memory where is the kernel */
	for (i = 0; i < lmb.memory.cnt; i++) {
		memory_start = (u32) lmb.memory.region[i].base;
		memory_end = (u32) lmb.memory.region[i].base
				+ (u32) lmb.memory.region[i].size;
		if ((memory_start <= (u32)_text) &&
					((u32)_text <= memory_end)) {
			memory_size = memory_end - memory_start;
			PAGE_OFFSET = memory_start;
			printk(KERN_INFO "%s: Main mem: 0x%x-0x%x, "
				"size 0x%08x\n", __func__, (u32) memory_start,
					(u32) memory_end, (u32) memory_size);
			break;
		}
	}

	if (!memory_start || !memory_end) {
		panic("%s: Missing memory setting 0x%08x-0x%08x\n",
			__func__, (u32) memory_start, (u32) memory_end);
	}

	/* reservation of region where is the kernel */
	kernel_align_start = PAGE_DOWN((u32)_text);
	/* ALIGN can be remove because _end in vmlinux.lds.S is align */
	kernel_align_size = PAGE_UP((u32)klimit) - kernel_align_start;
	lmb_reserve(kernel_align_start, kernel_align_size);
	printk(KERN_INFO "%s: kernel addr=0x%08x-0x%08x size=0x%08x\n",
		__func__, kernel_align_start, kernel_align_start
			+ kernel_align_size, kernel_align_size);

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
	max_pfn = max_low_pfn = memory_end >> PAGE_SHIFT;

	printk(KERN_INFO "%s: max_mapnr: %#lx\n", __func__, max_mapnr);
	printk(KERN_INFO "%s: min_low_pfn: %#lx\n", __func__, min_low_pfn);
	printk(KERN_INFO "%s: max_low_pfn: %#lx\n", __func__, max_low_pfn);

	/*
	 * Find an area to use for the bootmem bitmap.
	 * We look for the first area which is at least
	 * 128kB in length (128kB is enough for a bitmap
	 * for 4GB of memory, using 4kB pages), plus 1 page
	 * (in case the address isn't page-aligned).
	 */
#ifndef CONFIG_MMU
	map_size = init_bootmem_node(NODE_DATA(0), PFN_UP(TOPHYS((u32)klimit)),
					min_low_pfn, max_low_pfn);
#else
	map_size = init_bootmem_node(&contig_page_data,
		PFN_UP(TOPHYS((u32)klimit)), min_low_pfn, max_low_pfn);
#endif
	lmb_reserve(PFN_UP(TOPHYS((u32)klimit)) << PAGE_SHIFT, map_size);

	/* free bootmem is whole main memory */
	free_bootmem(memory_start, memory_size);

	/* reserve allocate blocks */
	for (i = 0; i < lmb.reserved.cnt; i++) {
		pr_debug("reserved %d - 0x%08x-0x%08x\n", i,
			(u32) lmb.reserved.region[i].base,
			(u32) lmb_size_bytes(&lmb.reserved, i));
		reserve_bootmem(lmb.reserved.region[i].base,
			lmb_size_bytes(&lmb.reserved, i) - 1, BOOTMEM_DEFAULT);
	}
#ifdef CONFIG_MMU
	init_bootmem_done = 1;
#endif
	paging_init();
}

void free_init_pages(char *what, unsigned long begin, unsigned long end)
{
	unsigned long addr;

	for (addr = begin; addr < end; addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		memset((void *)addr, 0xcc, PAGE_SIZE);
		free_page(addr);
		totalram_pages++;
	}
	printk(KERN_INFO "Freeing %s: %ldk freed\n", what, (end - begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	int pages = 0;
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		init_page_count(virt_to_page(start));
		free_page(start);
		totalram_pages++;
		pages++;
	}
	printk(KERN_NOTICE "Freeing initrd memory: %dk freed\n", pages);
}
#endif

void free_initmem(void)
{
	free_init_pages("unused kernel memory",
			(unsigned long)(&__init_begin),
			(unsigned long)(&__init_end));
}

/* FIXME from arch/powerpc/mm/mem.c*/
void show_mem(void)
{
	printk(KERN_NOTICE "%s\n", __func__);
}

void __init mem_init(void)
{
	high_memory = (void *)__va(memory_end);
	/* this will put all memory onto the freelists */
	totalram_pages += free_all_bootmem();

	printk(KERN_INFO "Memory: %luk/%luk available\n",
	       (unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
	       num_physpages << (PAGE_SHIFT-10));
#ifdef CONFIG_MMU
	mem_init_done = 1;
#endif
}

#ifndef CONFIG_MMU
/* Check against bounds of physical memory */
int ___range_ok(unsigned long addr, unsigned long size)
{
	return ((addr < memory_start) ||
		((addr + size) > memory_end));
}
EXPORT_SYMBOL(___range_ok);

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
			memory_end = memory_start + memory_size;
			lmb.memory.region[0].size = memory_size;
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

	if (!lmb.reserved.cnt) {
		printk(KERN_EMERG "Error memory count\n");
		machine_restart(NULL);
	}

	if ((u32) lmb.memory.region[0].size < 0x1000000) {
		printk(KERN_EMERG "Memory must be greater than 16MB\n");
		machine_restart(NULL);
	}
	/* Find main memory where the kernel is */
	memory_start = (u32) lmb.memory.region[0].base;
	memory_end = (u32) lmb.memory.region[0].base +
				(u32) lmb.memory.region[0].size;
	memory_size = memory_end - memory_start;

	mm_cmdline_setup(); /* FIXME parse args from command line - not used */

	/*
	 * Map out the kernel text/data/bss from the available physical
	 * memory.
	 */
	kstart = __pa(CONFIG_KERNEL_START); /* kernel start */
	/* kernel size */
	ksize = PAGE_ALIGN(((u32)_end - (u32)CONFIG_KERNEL_START));
	lmb_reserve(kstart, ksize);

#if defined(CONFIG_BLK_DEV_INITRD)
	/* Remove the init RAM disk from the available memory. */
/*	if (initrd_start) {
		mem_pieces_remove(&phys_avail, __pa(initrd_start),
				  initrd_end - initrd_start, 1);
	}*/
#endif /* CONFIG_BLK_DEV_INITRD */

	/* Initialize the MMU hardware */
	mmu_init_hw();

	/* Map in all of RAM starting at CONFIG_KERNEL_START */
	mapin_ram();

#ifdef HIGHMEM_START_BOOL
	ioremap_base = HIGHMEM_START;
#else
	ioremap_base = 0xfe000000UL;	/* for now, could be 0xfffff000 */
#endif /* CONFIG_HIGHMEM */
	ioremap_bot = ioremap_base;

	/* Initialize the context management stuff */
	mmu_context_init();
}

/* This is only called until mem_init is done. */
void __init *early_get_page(void)
{
	void *p;
	if (init_bootmem_done) {
		p = alloc_bootmem_pages(PAGE_SIZE);
	} else {
		/*
		 * Mem start + 32MB -> here is limit
		 * because of mem mapping from head.S
		 */
		p = __va(lmb_alloc_base(PAGE_SIZE, PAGE_SIZE,
					memory_start + 0x2000000));
	}
	return p;
}
#endif /* CONFIG_MMU */
