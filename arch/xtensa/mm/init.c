/*
 * arch/xtensa/mm/init.c
 *
 * Derived from MIPS, PPC.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 * Copyright (C) 2014 - 2016 Cadence Design Systems Inc.
 *
 * Chris Zankel	<chris@zankel.net>
 * Joe Taylor	<joe@tensilica.com, joetylr@yahoo.com>
 * Marc Gauthier
 * Kevin Chea
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/memblock.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/mman.h>
#include <linux/nodemask.h>
#include <linux/mm.h>
#include <linux/of_fdt.h>
#include <linux/dma-map-ops.h>

#include <asm/bootparam.h>
#include <asm/page.h>
#include <asm/sections.h>
#include <asm/sysmem.h>

/*
 * Initialize the bootmem system and give it all low memory we have available.
 */

void __init bootmem_init(void)
{
	/* Reserve all memory below PHYS_OFFSET, as memory
	 * accounting doesn't work for pages below that address.
	 *
	 * If PHYS_OFFSET is zero reserve page at address 0:
	 * successfull allocations should never return NULL.
	 */
	memblock_reserve(0, PHYS_OFFSET ? PHYS_OFFSET : 1);

	early_init_fdt_scan_reserved_mem();

	if (!memblock_phys_mem_size())
		panic("No memory found!\n");

	min_low_pfn = PFN_UP(memblock_start_of_DRAM());
	min_low_pfn = max(min_low_pfn, PFN_UP(PHYS_OFFSET));
	max_pfn = PFN_DOWN(memblock_end_of_DRAM());
	max_low_pfn = min(max_pfn, MAX_LOW_PFN);

	early_memtest((phys_addr_t)min_low_pfn << PAGE_SHIFT,
		      (phys_addr_t)max_low_pfn << PAGE_SHIFT);

	memblock_set_current_limit(PFN_PHYS(max_low_pfn));
	dma_contiguous_reserve(PFN_PHYS(max_low_pfn));

	memblock_dump_all();
}

static void __init print_vm_layout(void)
{
	pr_info("virtual kernel memory layout:\n"
#ifdef CONFIG_KASAN
		"    kasan   : 0x%08lx - 0x%08lx  (%5lu MB)\n"
#endif
#ifdef CONFIG_MMU
		"    vmalloc : 0x%08lx - 0x%08lx  (%5lu MB)\n"
#endif
#ifdef CONFIG_HIGHMEM
		"    pkmap   : 0x%08lx - 0x%08lx  (%5lu kB)\n"
		"    fixmap  : 0x%08lx - 0x%08lx  (%5lu kB)\n"
#endif
		"    lowmem  : 0x%08lx - 0x%08lx  (%5lu MB)\n"
		"    .text   : 0x%08lx - 0x%08lx  (%5lu kB)\n"
		"    .rodata : 0x%08lx - 0x%08lx  (%5lu kB)\n"
		"    .data   : 0x%08lx - 0x%08lx  (%5lu kB)\n"
		"    .init   : 0x%08lx - 0x%08lx  (%5lu kB)\n"
		"    .bss    : 0x%08lx - 0x%08lx  (%5lu kB)\n",
#ifdef CONFIG_KASAN
		KASAN_SHADOW_START, KASAN_SHADOW_START + KASAN_SHADOW_SIZE,
		KASAN_SHADOW_SIZE >> 20,
#endif
#ifdef CONFIG_MMU
		VMALLOC_START, VMALLOC_END,
		(VMALLOC_END - VMALLOC_START) >> 20,
#ifdef CONFIG_HIGHMEM
		PKMAP_BASE, PKMAP_BASE + LAST_PKMAP * PAGE_SIZE,
		(LAST_PKMAP*PAGE_SIZE) >> 10,
		FIXADDR_START, FIXADDR_END,
		(FIXADDR_END - FIXADDR_START) >> 10,
#endif
		PAGE_OFFSET, PAGE_OFFSET +
		(max_low_pfn - min_low_pfn) * PAGE_SIZE,
#else
		min_low_pfn * PAGE_SIZE, max_low_pfn * PAGE_SIZE,
#endif
		((max_low_pfn - min_low_pfn) * PAGE_SIZE) >> 20,
		(unsigned long)_text, (unsigned long)_etext,
		(unsigned long)(_etext - _text) >> 10,
		(unsigned long)__start_rodata, (unsigned long)__end_rodata,
		(unsigned long)(__end_rodata - __start_rodata) >> 10,
		(unsigned long)_sdata, (unsigned long)_edata,
		(unsigned long)(_edata - _sdata) >> 10,
		(unsigned long)__init_begin, (unsigned long)__init_end,
		(unsigned long)(__init_end - __init_begin) >> 10,
		(unsigned long)__bss_start, (unsigned long)__bss_stop,
		(unsigned long)(__bss_stop - __bss_start) >> 10);
}

void __init zones_init(void)
{
	/* All pages are DMA-able, so we put them all in the DMA zone. */
	unsigned long max_zone_pfn[MAX_NR_ZONES] = {
		[ZONE_NORMAL] = max_low_pfn,
#ifdef CONFIG_HIGHMEM
		[ZONE_HIGHMEM] = max_pfn,
#endif
	};
	free_area_init(max_zone_pfn);
	print_vm_layout();
}

static void __init free_highpages(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long max_low = max_low_pfn;
	phys_addr_t range_start, range_end;
	u64 i;

	/* set highmem page free */
	for_each_free_mem_range(i, NUMA_NO_NODE, MEMBLOCK_NONE,
				&range_start, &range_end, NULL) {
		unsigned long start = PFN_UP(range_start);
		unsigned long end = PFN_DOWN(range_end);

		/* Ignore complete lowmem entries */
		if (end <= max_low)
			continue;

		/* Truncate partial highmem entries */
		if (start < max_low)
			start = max_low;

		for (; start < end; start++)
			free_highmem_page(pfn_to_page(start));
	}
#endif
}

/*
 * Initialize memory pages.
 */

void __init mem_init(void)
{
	free_highpages();

	max_mapnr = max_pfn - ARCH_PFN_OFFSET;
	high_memory = (void *)__va(max_low_pfn << PAGE_SHIFT);

	memblock_free_all();
}

static void __init parse_memmap_one(char *p)
{
	char *oldp;
	unsigned long start_at, mem_size;

	if (!p)
		return;

	oldp = p;
	mem_size = memparse(p, &p);
	if (p == oldp)
		return;

	switch (*p) {
	case '@':
		start_at = memparse(p + 1, &p);
		memblock_add(start_at, mem_size);
		break;

	case '$':
		start_at = memparse(p + 1, &p);
		memblock_reserve(start_at, mem_size);
		break;

	case 0:
		memblock_reserve(mem_size, -mem_size);
		break;

	default:
		pr_warn("Unrecognized memmap syntax: %s\n", p);
		break;
	}
}

static int __init parse_memmap_opt(char *str)
{
	while (str) {
		char *k = strchr(str, ',');

		if (k)
			*k++ = 0;

		parse_memmap_one(str);
		str = k;
	}

	return 0;
}
early_param("memmap", parse_memmap_opt);

#ifdef CONFIG_MMU
static const pgprot_t protection_map[16] = {
	[VM_NONE]					= PAGE_NONE,
	[VM_READ]					= PAGE_READONLY,
	[VM_WRITE]					= PAGE_COPY,
	[VM_WRITE | VM_READ]				= PAGE_COPY,
	[VM_EXEC]					= PAGE_READONLY_EXEC,
	[VM_EXEC | VM_READ]				= PAGE_READONLY_EXEC,
	[VM_EXEC | VM_WRITE]				= PAGE_COPY_EXEC,
	[VM_EXEC | VM_WRITE | VM_READ]			= PAGE_COPY_EXEC,
	[VM_SHARED]					= PAGE_NONE,
	[VM_SHARED | VM_READ]				= PAGE_READONLY,
	[VM_SHARED | VM_WRITE]				= PAGE_SHARED,
	[VM_SHARED | VM_WRITE | VM_READ]		= PAGE_SHARED,
	[VM_SHARED | VM_EXEC]				= PAGE_READONLY_EXEC,
	[VM_SHARED | VM_EXEC | VM_READ]			= PAGE_READONLY_EXEC,
	[VM_SHARED | VM_EXEC | VM_WRITE]		= PAGE_SHARED_EXEC,
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= PAGE_SHARED_EXEC
};
DECLARE_VM_GET_PAGE_PROT
#endif
