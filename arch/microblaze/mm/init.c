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

unsigned int __page_offset;
/* EXPORT_SYMBOL(__page_offset); */

char *klimit = _end;

/*
 * Initialize the bootmem system and give it all the memory we
 * have available.
 */
unsigned int memory_start;
unsigned int memory_end; /* due to mm/nommu.c */
unsigned int memory_size;

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
static void __init paging_init(void)
{
	int i;
	unsigned long zones_size[MAX_NR_ZONES];

	/*
	 * old: we can DMA to/from any address.put all page into ZONE_DMA
	 * We use only ZONE_NORMAL
	 */
	zones_size[ZONE_NORMAL] = max_mapnr;

	/* every other zones are empty */
	for (i = 1; i < MAX_NR_ZONES; i++)
		zones_size[i] = 0;

	free_area_init(zones_size);
}

void __init setup_memory(void)
{
	int i;
	unsigned long map_size;
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
				"size 0x%08x\n", __func__, memory_start,
						memory_end, memory_size);
			break;
		}
	}

	if (!memory_start || !memory_end) {
		panic("%s: Missing memory setting 0x%08x-0x%08x\n",
			__func__, memory_start, memory_end);
	}

	/* reservation of region where is the kernel */
	kernel_align_start = PAGE_DOWN((u32)_text);
	/* ALIGN can be remove because _end in vmlinux.lds.S is align */
	kernel_align_size = PAGE_UP((u32)klimit) - kernel_align_start;
	lmb_reserve(kernel_align_start, kernel_align_size);
	printk(KERN_INFO "%s: kernel addr=0x%08x-0x%08x size=0x%08x\n",
		__func__, kernel_align_start, kernel_align_start
			+ kernel_align_size, kernel_align_size);

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
	map_size = init_bootmem_node(NODE_DATA(0), PFN_UP(TOPHYS((u32)_end)),
					min_low_pfn, max_low_pfn);

	lmb_reserve(PFN_UP(TOPHYS((u32)_end)) << PAGE_SHIFT, map_size);

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
}

/* Check against bounds of physical memory */
int ___range_ok(unsigned long addr, unsigned long size)
{
	return ((addr < memory_start) ||
		((addr + size) > memory_end));
}
