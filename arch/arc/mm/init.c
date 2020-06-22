// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/initrd.h>
#endif
#include <linux/of_fdt.h>
#include <linux/swap.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/arcregs.h>

pgd_t swapper_pg_dir[PTRS_PER_PGD] __aligned(PAGE_SIZE);
char empty_zero_page[PAGE_SIZE] __aligned(PAGE_SIZE);
EXPORT_SYMBOL(empty_zero_page);

static const unsigned long low_mem_start = CONFIG_LINUX_RAM_BASE;
static unsigned long low_mem_sz;

#ifdef CONFIG_HIGHMEM
static unsigned long min_high_pfn, max_high_pfn;
static u64 high_mem_start;
static u64 high_mem_sz;
#endif

#ifdef CONFIG_DISCONTIGMEM
struct pglist_data node_data[MAX_NUMNODES] __read_mostly;
EXPORT_SYMBOL(node_data);
#endif

long __init arc_get_mem_sz(void)
{
	return low_mem_sz;
}

/* User can over-ride above with "mem=nnn[KkMm]" in cmdline */
static int __init setup_mem_sz(char *str)
{
	low_mem_sz = memparse(str, NULL) & PAGE_MASK;

	/* early console might not be setup yet - it will show up later */
	pr_info("\"mem=%s\": mem sz set to %ldM\n", str, TO_MB(low_mem_sz));

	return 0;
}
early_param("mem", setup_mem_sz);

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	int in_use = 0;

	if (!low_mem_sz) {
		if (base != low_mem_start)
			panic("CONFIG_LINUX_RAM_BASE != DT memory { }");

		low_mem_sz = size;
		in_use = 1;
		memblock_add_node(base, size, 0);
	} else {
#ifdef CONFIG_HIGHMEM
		high_mem_start = base;
		high_mem_sz = size;
		in_use = 1;
		memblock_add_node(base, size, 1);
#endif
	}

	pr_info("Memory @ %llx [%lldM] %s\n",
		base, TO_MB(size), !in_use ? "Not used":"");
}

bool arch_has_descending_max_zone_pfns(void)
{
	return !IS_ENABLED(CONFIG_ARC_HAS_PAE40);
}

/*
 * First memory setup routine called from setup_arch()
 * 1. setup swapper's mm @init_mm
 * 2. Count the pages we have and setup bootmem allocator
 * 3. zone setup
 */
void __init setup_arch_memory(void)
{
	unsigned long max_zone_pfn[MAX_NR_ZONES] = { 0 };

	init_mm.start_code = (unsigned long)_text;
	init_mm.end_code = (unsigned long)_etext;
	init_mm.end_data = (unsigned long)_edata;
	init_mm.brk = (unsigned long)_end;

	/* first page of system - kernel .vector starts here */
	min_low_pfn = ARCH_PFN_OFFSET;

	/* Last usable page of low mem */
	max_low_pfn = max_pfn = PFN_DOWN(low_mem_start + low_mem_sz);

#ifdef CONFIG_FLATMEM
	/* pfn_valid() uses this */
	max_mapnr = max_low_pfn - min_low_pfn;
#endif

	/*------------- bootmem allocator setup -----------------------*/

	/*
	 * seed the bootmem allocator after any DT memory node parsing or
	 * "mem=xxx" cmdline overrides have potentially updated @arc_mem_sz
	 *
	 * Only low mem is added, otherwise we have crashes when allocating
	 * mem_map[] itself. NO_BOOTMEM allocates mem_map[] at the end of
	 * avail memory, ending in highmem with a > 32-bit address. However
	 * it then tries to memset it with a truncaed 32-bit handle, causing
	 * the crash
	 */

	memblock_reserve(CONFIG_LINUX_LINK_BASE,
			 __pa(_end) - CONFIG_LINUX_LINK_BASE);

#ifdef CONFIG_BLK_DEV_INITRD
	if (phys_initrd_size) {
		memblock_reserve(phys_initrd_start, phys_initrd_size);
		initrd_start = (unsigned long)__va(phys_initrd_start);
		initrd_end = initrd_start + phys_initrd_size;
	}
#endif

	early_init_fdt_reserve_self();
	early_init_fdt_scan_reserved_mem();

	memblock_dump_all();

	/*----------------- node/zones setup --------------------------*/
	max_zone_pfn[ZONE_NORMAL] = max_low_pfn;

#ifdef CONFIG_HIGHMEM
	/*
	 * Populate a new node with highmem
	 *
	 * On ARC (w/o PAE) HIGHMEM addresses are actually smaller (0 based)
	 * than addresses in normal ala low memory (0x8000_0000 based).
	 * Even with PAE, the huge peripheral space hole would waste a lot of
	 * mem with single mem_map[]. This warrants a mem_map per region design.
	 * Thus HIGHMEM on ARC is imlemented with DISCONTIGMEM.
	 *
	 * DISCONTIGMEM in turns requires multiple nodes. node 0 above is
	 * populated with normal memory zone while node 1 only has highmem
	 */
	node_set_online(1);

	min_high_pfn = PFN_DOWN(high_mem_start);
	max_high_pfn = PFN_DOWN(high_mem_start + high_mem_sz);

	max_zone_pfn[ZONE_HIGHMEM] = max_high_pfn;

	high_memory = (void *)(min_high_pfn << PAGE_SHIFT);
	kmap_init();
#endif

	free_area_init(max_zone_pfn);
}

/*
 * mem_init - initializes memory
 *
 * Frees up bootmem
 * Calculates and displays memory available/used
 */
void __init mem_init(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long tmp;

	reset_all_zones_managed_pages();
	for (tmp = min_high_pfn; tmp < max_high_pfn; tmp++)
		free_highmem_page(pfn_to_page(tmp));
#endif

	memblock_free_all();
	mem_init_print_info(NULL);
}
