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
 *
 * Chris Zankel	<chris@zankel.net>
 * Joe Taylor	<joe@tensilica.com, joetylr@yahoo.com>
 * Marc Gauthier
 * Kevin Chea
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/bootmem.h>
#include <linux/gfp.h>
#include <linux/swap.h>
#include <linux/mman.h>
#include <linux/nodemask.h>
#include <linux/mm.h>

#include <asm/bootparam.h>
#include <asm/page.h>
#include <asm/sections.h>

/*
 * mem_reserve(start, end, must_exist)
 *
 * Reserve some memory from the memory pool.
 *
 * Parameters:
 *  start	Start of region,
 *  end		End of region,
 *  must_exist	Must exist in memory pool.
 *
 * Returns:
 *  0 (memory area couldn't be mapped)
 * -1 (success)
 */

int __init mem_reserve(unsigned long start, unsigned long end, int must_exist)
{
	int i;

	if (start == end)
		return 0;

	start = start & PAGE_MASK;
	end = PAGE_ALIGN(end);

	for (i = 0; i < sysmem.nr_banks; i++)
		if (start < sysmem.bank[i].end
		    && end >= sysmem.bank[i].start)
			break;

	if (i == sysmem.nr_banks) {
		if (must_exist)
			printk (KERN_WARNING "mem_reserve: [0x%0lx, 0x%0lx) "
				"not in any region!\n", start, end);
		return 0;
	}

	if (start > sysmem.bank[i].start) {
		if (end < sysmem.bank[i].end) {
			/* split entry */
			if (sysmem.nr_banks >= SYSMEM_BANKS_MAX)
				panic("meminfo overflow\n");
			sysmem.bank[sysmem.nr_banks].start = end;
			sysmem.bank[sysmem.nr_banks].end = sysmem.bank[i].end;
			sysmem.nr_banks++;
		}
		sysmem.bank[i].end = start;

	} else if (end < sysmem.bank[i].end) {
		sysmem.bank[i].start = end;

	} else {
		/* remove entry */
		sysmem.nr_banks--;
		sysmem.bank[i].start = sysmem.bank[sysmem.nr_banks].start;
		sysmem.bank[i].end   = sysmem.bank[sysmem.nr_banks].end;
	}
	return -1;
}


/*
 * Initialize the bootmem system and give it all low memory we have available.
 */

void __init bootmem_init(void)
{
	unsigned long pfn;
	unsigned long bootmap_start, bootmap_size;
	int i;

	max_low_pfn = max_pfn = 0;
	min_low_pfn = ~0;

	for (i=0; i < sysmem.nr_banks; i++) {
		pfn = PAGE_ALIGN(sysmem.bank[i].start) >> PAGE_SHIFT;
		if (pfn < min_low_pfn)
			min_low_pfn = pfn;
		pfn = PAGE_ALIGN(sysmem.bank[i].end - 1) >> PAGE_SHIFT;
		if (pfn > max_pfn)
			max_pfn = pfn;
	}

	if (min_low_pfn > max_pfn)
		panic("No memory found!\n");

	max_low_pfn = max_pfn < MAX_MEM_PFN >> PAGE_SHIFT ?
		max_pfn : MAX_MEM_PFN >> PAGE_SHIFT;

	/* Find an area to use for the bootmem bitmap. */

	bootmap_size = bootmem_bootmap_pages(max_low_pfn - min_low_pfn);
	bootmap_size <<= PAGE_SHIFT;
	bootmap_start = ~0;

	for (i=0; i<sysmem.nr_banks; i++)
		if (sysmem.bank[i].end - sysmem.bank[i].start >= bootmap_size) {
			bootmap_start = sysmem.bank[i].start;
			break;
		}

	if (bootmap_start == ~0UL)
		panic("Cannot find %ld bytes for bootmap\n", bootmap_size);

	/* Reserve the bootmem bitmap area */

	mem_reserve(bootmap_start, bootmap_start + bootmap_size, 1);
	bootmap_size = init_bootmem_node(NODE_DATA(0),
					 bootmap_start >> PAGE_SHIFT,
					 min_low_pfn,
					 max_low_pfn);

	/* Add all remaining memory pieces into the bootmem map */

	for (i = 0; i < sysmem.nr_banks; i++) {
		if (sysmem.bank[i].start >> PAGE_SHIFT < max_low_pfn) {
			unsigned long end = min(max_low_pfn << PAGE_SHIFT,
						sysmem.bank[i].end);
			free_bootmem(sysmem.bank[i].start,
				     end - sysmem.bank[i].start);
		}
	}

}


void __init zones_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];
	int i;

	/* All pages are DMA-able, so we put them all in the DMA zone. */

	zones_size[ZONE_DMA] = max_low_pfn - ARCH_PFN_OFFSET;
	for (i = 1; i < MAX_NR_ZONES; i++)
		zones_size[i] = 0;

#ifdef CONFIG_HIGHMEM
	zones_size[ZONE_HIGHMEM] = max_pfn - max_low_pfn;
#endif

	free_area_init_node(0, zones_size, ARCH_PFN_OFFSET, NULL);
}

/*
 * Initialize memory pages.
 */

void __init mem_init(void)
{
	max_mapnr = max_low_pfn - ARCH_PFN_OFFSET;
	high_memory = (void *) __va(max_low_pfn << PAGE_SHIFT);

#ifdef CONFIG_HIGHMEM
#error HIGHGMEM not implemented in init.c
#endif

	free_all_bootmem();

	mem_init_print_info(NULL);
}

#ifdef CONFIG_BLK_DEV_INITRD
extern int initrd_is_mapped;

void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (initrd_is_mapped)
		free_reserved_area((void *)start, (void *)end, -1, "initrd");
}
#endif

void free_initmem(void)
{
	free_initmem_default(-1);
}
