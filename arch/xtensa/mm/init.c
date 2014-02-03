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
 * Copyright (C) 2014 Cadence Design Systems Inc.
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
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/mman.h>
#include <linux/nodemask.h>
#include <linux/mm.h>

#include <asm/bootparam.h>
#include <asm/page.h>
#include <asm/sections.h>
#include <asm/sysmem.h>

struct sysmem_info sysmem __initdata;

static void __init sysmem_dump(void)
{
	unsigned i;

	pr_debug("Sysmem:\n");
	for (i = 0; i < sysmem.nr_banks; ++i)
		pr_debug("  0x%08lx - 0x%08lx (%ldK)\n",
			 sysmem.bank[i].start, sysmem.bank[i].end,
			 (sysmem.bank[i].end - sysmem.bank[i].start) >> 10);
}

/*
 * Find bank with maximal .start such that bank.start <= start
 */
static inline struct meminfo * __init find_bank(unsigned long start)
{
	unsigned i;
	struct meminfo *it = NULL;

	for (i = 0; i < sysmem.nr_banks; ++i)
		if (sysmem.bank[i].start <= start)
			it = sysmem.bank + i;
		else
			break;
	return it;
}

/*
 * Move all memory banks starting at 'from' to a new place at 'to',
 * adjust nr_banks accordingly.
 * Both 'from' and 'to' must be inside the sysmem.bank.
 *
 * Returns: 0 (success), -ENOMEM (not enough space in the sysmem.bank).
 */
static int __init move_banks(struct meminfo *to, struct meminfo *from)
{
	unsigned n = sysmem.nr_banks - (from - sysmem.bank);

	if (to > from && to - from + sysmem.nr_banks > SYSMEM_BANKS_MAX)
		return -ENOMEM;
	if (to != from)
		memmove(to, from, n * sizeof(struct meminfo));
	sysmem.nr_banks += to - from;
	return 0;
}

/*
 * Add new bank to sysmem. Resulting sysmem is the union of bytes of the
 * original sysmem and the new bank.
 *
 * Returns: 0 (success), < 0 (error)
 */
int __init add_sysmem_bank(unsigned long start, unsigned long end)
{
	unsigned i;
	struct meminfo *it = NULL;
	unsigned long sz;
	unsigned long bank_sz = 0;

	if (start == end ||
	    (start < end) != (PAGE_ALIGN(start) < (end & PAGE_MASK))) {
		pr_warn("Ignoring small memory bank 0x%08lx size: %ld bytes\n",
			start, end - start);
		return -EINVAL;
	}

	start = PAGE_ALIGN(start);
	end &= PAGE_MASK;
	sz = end - start;

	it = find_bank(start);

	if (it)
		bank_sz = it->end - it->start;

	if (it && bank_sz >= start - it->start) {
		if (end - it->start > bank_sz)
			it->end = end;
		else
			return 0;
	} else {
		if (!it)
			it = sysmem.bank;
		else
			++it;

		if (it - sysmem.bank < sysmem.nr_banks &&
		    it->start - start <= sz) {
			it->start = start;
			if (it->end - it->start < sz)
				it->end = end;
			else
				return 0;
		} else {
			if (move_banks(it + 1, it) < 0) {
				pr_warn("Ignoring memory bank 0x%08lx size %ld bytes\n",
					start, end - start);
				return -EINVAL;
			}
			it->start = start;
			it->end = end;
			return 0;
		}
	}
	sz = it->end - it->start;
	for (i = it + 1 - sysmem.bank; i < sysmem.nr_banks; ++i)
		if (sysmem.bank[i].start - it->start <= sz) {
			if (sz < sysmem.bank[i].end - it->start)
				it->end = sysmem.bank[i].end;
		} else {
			break;
		}

	move_banks(it + 1, sysmem.bank + i);
	return 0;
}

/*
 * mem_reserve(start, end, must_exist)
 *
 * Reserve some memory from the memory pool.
 * If must_exist is set and a part of the region being reserved does not exist
 * memory map is not altered.
 *
 * Parameters:
 *  start	Start of region,
 *  end		End of region,
 *  must_exist	Must exist in memory pool.
 *
 * Returns:
 *  0 (success)
 *  < 0 (error)
 */

int __init mem_reserve(unsigned long start, unsigned long end, int must_exist)
{
	struct meminfo *it;
	struct meminfo *rm = NULL;
	unsigned long sz;
	unsigned long bank_sz = 0;

	start = start & PAGE_MASK;
	end = PAGE_ALIGN(end);
	sz = end - start;
	if (!sz)
		return -EINVAL;

	it = find_bank(start);

	if (it)
		bank_sz = it->end - it->start;

	if ((!it || end - it->start > bank_sz) && must_exist) {
		pr_warn("mem_reserve: [0x%0lx, 0x%0lx) not in any region!\n",
			start, end);
		return -EINVAL;
	}

	if (it && start - it->start < bank_sz) {
		if (start == it->start) {
			if (end - it->start < bank_sz) {
				it->start = end;
				return 0;
			} else {
				rm = it;
			}
		} else {
			it->end = start;
			if (end - it->start < bank_sz)
				return add_sysmem_bank(end,
						       it->start + bank_sz);
			++it;
		}
	}

	if (!it)
		it = sysmem.bank;

	for (; it < sysmem.bank + sysmem.nr_banks; ++it) {
		if (it->end - start <= sz) {
			if (!rm)
				rm = it;
		} else {
			if (it->start - start < sz)
				it->start = end;
			break;
		}
	}

	if (rm)
		move_banks(rm, it);

	return 0;
}


/*
 * Initialize the bootmem system and give it all low memory we have available.
 */

void __init bootmem_init(void)
{
	unsigned long pfn;
	unsigned long bootmap_start, bootmap_size;
	int i;

	sysmem_dump();
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
	/* All pages are DMA-able, so we put them all in the DMA zone. */
	unsigned long zones_size[MAX_NR_ZONES] = {
		[ZONE_DMA] = max_low_pfn - ARCH_PFN_OFFSET,
#ifdef CONFIG_HIGHMEM
		[ZONE_HIGHMEM] = max_pfn - max_low_pfn,
#endif
	};
	free_area_init_node(0, zones_size, ARCH_PFN_OFFSET, NULL);
}

/*
 * Initialize memory pages.
 */

void __init mem_init(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long tmp;

	reset_all_zones_managed_pages();
	for (tmp = max_low_pfn; tmp < max_pfn; tmp++)
		free_highmem_page(pfn_to_page(tmp));
#endif

	max_mapnr = max_pfn - ARCH_PFN_OFFSET;
	high_memory = (void *)__va(max_low_pfn << PAGE_SHIFT);

	free_all_bootmem();

	mem_init_print_info(NULL);
	pr_info("virtual kernel memory layout:\n"
#ifdef CONFIG_HIGHMEM
		"    pkmap   : 0x%08lx - 0x%08lx  (%5lu kB)\n"
		"    fixmap  : 0x%08lx - 0x%08lx  (%5lu kB)\n"
#endif
		"    vmalloc : 0x%08x - 0x%08x  (%5u MB)\n"
		"    lowmem  : 0x%08x - 0x%08lx  (%5lu MB)\n",
#ifdef CONFIG_HIGHMEM
		PKMAP_BASE, PKMAP_BASE + LAST_PKMAP * PAGE_SIZE,
		(LAST_PKMAP*PAGE_SIZE) >> 10,
		FIXADDR_START, FIXADDR_TOP,
		(FIXADDR_TOP - FIXADDR_START) >> 10,
#endif
		VMALLOC_START, VMALLOC_END,
		(VMALLOC_END - VMALLOC_START) >> 20,
		PAGE_OFFSET, PAGE_OFFSET +
		(max_low_pfn - min_low_pfn) * PAGE_SIZE,
		((max_low_pfn - min_low_pfn) * PAGE_SIZE) >> 20);
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
		add_sysmem_bank(start_at, start_at + mem_size);
		break;

	case '$':
		start_at = memparse(p + 1, &p);
		mem_reserve(start_at, start_at + mem_size, 0);
		break;

	case 0:
		mem_reserve(mem_size, 0, 0);
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
