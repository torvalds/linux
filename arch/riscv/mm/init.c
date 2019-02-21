/*
 * Copyright (C) 2012 Regents of the University of California
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/initrd.h>
#include <linux/swap.h>
#include <linux/sizes.h>
#include <linux/of_fdt.h>

#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/io.h>

static void __init zone_sizes_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES] = { 0, };

#ifdef CONFIG_ZONE_DMA32
	max_zone_pfns[ZONE_DMA32] = PFN_DOWN(min(4UL * SZ_1G,
			(unsigned long) PFN_PHYS(max_low_pfn)));
#endif
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;

	free_area_init_nodes(max_zone_pfns);
}

void setup_zero_page(void)
{
	memset((void *)empty_zero_page, 0, PAGE_SIZE);
}

void __init paging_init(void)
{
	setup_zero_page();
	local_flush_tlb_all();
	zone_sizes_init();
}

void __init mem_init(void)
{
#ifdef CONFIG_FLATMEM
	BUG_ON(!mem_map);
#endif /* CONFIG_FLATMEM */

	high_memory = (void *)(__va(PFN_PHYS(max_low_pfn)));
	memblock_free_all();

	mem_init_print_info(NULL);
}

void free_initmem(void)
{
	free_initmem_default(0);
}

#ifdef CONFIG_BLK_DEV_INITRD
static void __init setup_initrd(void)
{
	unsigned long size;

	if (initrd_start >= initrd_end) {
		pr_info("initrd not found or empty");
		goto disable;
	}
	if (__pa(initrd_end) > PFN_PHYS(max_low_pfn)) {
		pr_err("initrd extends beyond end of memory");
		goto disable;
	}

	size = initrd_end - initrd_start;
	memblock_reserve(__pa(initrd_start), size);
	initrd_below_start_ok = 1;

	pr_info("Initial ramdisk at: 0x%p (%lu bytes)\n",
		(void *)(initrd_start), size);
	return;
disable:
	pr_cont(" - disabling initrd\n");
	initrd_start = 0;
	initrd_end = 0;
}

void free_initrd_mem(unsigned long start, unsigned long end)
{
}
#endif /* CONFIG_BLK_DEV_INITRD */

void __init setup_bootmem(void)
{
	struct memblock_region *reg;
	phys_addr_t mem_size = 0;

	/* Find the memory region containing the kernel */
	for_each_memblock(memory, reg) {
		phys_addr_t vmlinux_end = __pa(_end);
		phys_addr_t end = reg->base + reg->size;

		if (reg->base <= vmlinux_end && vmlinux_end <= end) {
			/*
			 * Reserve from the start of the region to the end of
			 * the kernel
			 */
			memblock_reserve(reg->base, vmlinux_end - reg->base);
			mem_size = min(reg->size, (phys_addr_t)-PAGE_OFFSET);
		}
	}
	BUG_ON(mem_size == 0);

	set_max_mapnr(PFN_DOWN(mem_size));
	max_low_pfn = PFN_DOWN(memblock_end_of_DRAM());

#ifdef CONFIG_BLK_DEV_INITRD
	setup_initrd();
#endif /* CONFIG_BLK_DEV_INITRD */

	early_init_fdt_reserve_self();
	early_init_fdt_scan_reserved_mem();
	memblock_allow_resize();
	memblock_dump_all();

	for_each_memblock(memory, reg) {
		unsigned long start_pfn = memblock_region_memory_base_pfn(reg);
		unsigned long end_pfn = memblock_region_memory_end_pfn(reg);

		memblock_set_node(PFN_PHYS(start_pfn),
				  PFN_PHYS(end_pfn - start_pfn),
				  &memblock.memory, 0);
	}
}
