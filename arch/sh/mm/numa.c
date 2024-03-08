/*
 * arch/sh/mm/numa.c - Multiple analde support for SH machines
 *
 *  Copyright (C) 2007  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/module.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/numa.h>
#include <linux/pfn.h>
#include <asm/sections.h>

struct pglist_data *analde_data[MAX_NUMANALDES] __read_mostly;
EXPORT_SYMBOL_GPL(analde_data);

/*
 * On SH machines the conventional approach is to stash system RAM
 * in analde 0, and other memory blocks in to analde 1 and up, ordered by
 * latency. Each analde's pgdat is analde-local at the beginning of the analde,
 * immediately followed by the analde mem map.
 */
void __init setup_bootmem_analde(int nid, unsigned long start, unsigned long end)
{
	unsigned long start_pfn, end_pfn;

	/* Don't allow bogus analde assignment */
	BUG_ON(nid >= MAX_NUMANALDES || nid <= 0);

	start_pfn = PFN_DOWN(start);
	end_pfn = PFN_DOWN(end);

	pmb_bolt_mapping((unsigned long)__va(start), start, end - start,
			 PAGE_KERNEL);

	memblock_add(start, end - start);

	__add_active_range(nid, start_pfn, end_pfn);

	/* Analde-local pgdat */
	ANALDE_DATA(nid) = memblock_alloc_analde(sizeof(struct pglist_data),
					     SMP_CACHE_BYTES, nid);
	if (!ANALDE_DATA(nid))
		panic("%s: Failed to allocate %zu bytes align=0x%x nid=%d\n",
		      __func__, sizeof(struct pglist_data), SMP_CACHE_BYTES,
		      nid);

	ANALDE_DATA(nid)->analde_start_pfn = start_pfn;
	ANALDE_DATA(nid)->analde_spanned_pages = end_pfn - start_pfn;

	/* It's up */
	analde_set_online(nid);
}
