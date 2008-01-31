/*
 * Procedures for maintaining information about logical memory blocks.
 *
 * Peter Bergner, IBM Corp.	June 2001.
 * Copyright (C) 2001 Peter Bergner.
 * 
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <asm/types.h>
#include <asm/page.h>
#include <asm/prom.h>
#include <asm/lmb.h>
#ifdef CONFIG_PPC32
#include "mmu_decl.h"		/* for __max_low_memory */
#endif

#undef DEBUG

#ifdef DEBUG
#include <asm/udbg.h>
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

#define LMB_ALLOC_ANYWHERE	0

struct lmb lmb;

void lmb_dump_all(void)
{
#ifdef DEBUG
	unsigned long i;

	DBG("lmb_dump_all:\n");
	DBG("    memory.cnt		  = 0x%lx\n", lmb.memory.cnt);
	DBG("    memory.size		  = 0x%lx\n", lmb.memory.size);
	for (i=0; i < lmb.memory.cnt ;i++) {
		DBG("    memory.region[0x%x].base       = 0x%lx\n",
			    i, lmb.memory.region[i].base);
		DBG("		      .size     = 0x%lx\n",
			    lmb.memory.region[i].size);
	}

	DBG("\n    reserved.cnt	  = 0x%lx\n", lmb.reserved.cnt);
	DBG("    reserved.size	  = 0x%lx\n", lmb.reserved.size);
	for (i=0; i < lmb.reserved.cnt ;i++) {
		DBG("    reserved.region[0x%x].base       = 0x%lx\n",
			    i, lmb.reserved.region[i].base);
		DBG("		      .size     = 0x%lx\n",
			    lmb.reserved.region[i].size);
	}
#endif /* DEBUG */
}

static unsigned long __init lmb_addrs_overlap(unsigned long base1,
		unsigned long size1, unsigned long base2, unsigned long size2)
{
	return ((base1 < (base2+size2)) && (base2 < (base1+size1)));
}

static long __init lmb_addrs_adjacent(unsigned long base1, unsigned long size1,
		unsigned long base2, unsigned long size2)
{
	if (base2 == base1 + size1)
		return 1;
	else if (base1 == base2 + size2)
		return -1;

	return 0;
}

static long __init lmb_regions_adjacent(struct lmb_region *rgn,
		unsigned long r1, unsigned long r2)
{
	unsigned long base1 = rgn->region[r1].base;
	unsigned long size1 = rgn->region[r1].size;
	unsigned long base2 = rgn->region[r2].base;
	unsigned long size2 = rgn->region[r2].size;

	return lmb_addrs_adjacent(base1, size1, base2, size2);
}

static void __init lmb_remove_region(struct lmb_region *rgn, unsigned long r)
{
	unsigned long i;

	for (i = r; i < rgn->cnt - 1; i++) {
		rgn->region[i].base = rgn->region[i + 1].base;
		rgn->region[i].size = rgn->region[i + 1].size;
	}
	rgn->cnt--;
}

/* Assumption: base addr of region 1 < base addr of region 2 */
static void __init lmb_coalesce_regions(struct lmb_region *rgn,
		unsigned long r1, unsigned long r2)
{
	rgn->region[r1].size += rgn->region[r2].size;
	lmb_remove_region(rgn, r2);
}

/* This routine called with relocation disabled. */
void __init lmb_init(void)
{
	/* Create a dummy zero size LMB which will get coalesced away later.
	 * This simplifies the lmb_add() code below...
	 */
	lmb.memory.region[0].base = 0;
	lmb.memory.region[0].size = 0;
	lmb.memory.cnt = 1;

	/* Ditto. */
	lmb.reserved.region[0].base = 0;
	lmb.reserved.region[0].size = 0;
	lmb.reserved.cnt = 1;
}

/* This routine may be called with relocation disabled. */
void __init lmb_analyze(void)
{
	int i;

	lmb.memory.size = 0;

	for (i = 0; i < lmb.memory.cnt; i++)
		lmb.memory.size += lmb.memory.region[i].size;
}

/* This routine called with relocation disabled. */
static long __init lmb_add_region(struct lmb_region *rgn, unsigned long base,
				  unsigned long size)
{
	unsigned long coalesced = 0;
	long adjacent, i;

	/* First try and coalesce this LMB with another. */
	for (i=0; i < rgn->cnt; i++) {
		unsigned long rgnbase = rgn->region[i].base;
		unsigned long rgnsize = rgn->region[i].size;

		if ((rgnbase == base) && (rgnsize == size))
			/* Already have this region, so we're done */
			return 0;

		adjacent = lmb_addrs_adjacent(base,size,rgnbase,rgnsize);
		if ( adjacent > 0 ) {
			rgn->region[i].base -= size;
			rgn->region[i].size += size;
			coalesced++;
			break;
		}
		else if ( adjacent < 0 ) {
			rgn->region[i].size += size;
			coalesced++;
			break;
		}
	}

	if ((i < rgn->cnt-1) && lmb_regions_adjacent(rgn, i, i+1) ) {
		lmb_coalesce_regions(rgn, i, i+1);
		coalesced++;
	}

	if (coalesced)
		return coalesced;
	if (rgn->cnt >= MAX_LMB_REGIONS)
		return -1;

	/* Couldn't coalesce the LMB, so add it to the sorted table. */
	for (i = rgn->cnt-1; i >= 0; i--) {
		if (base < rgn->region[i].base) {
			rgn->region[i+1].base = rgn->region[i].base;
			rgn->region[i+1].size = rgn->region[i].size;
		} else {
			rgn->region[i+1].base = base;
			rgn->region[i+1].size = size;
			break;
		}
	}
	rgn->cnt++;

	return 0;
}

/* This routine may be called with relocation disabled. */
long __init lmb_add(unsigned long base, unsigned long size)
{
	struct lmb_region *_rgn = &(lmb.memory);

	/* On pSeries LPAR systems, the first LMB is our RMO region. */
	if (base == 0)
		lmb.rmo_size = size;

	return lmb_add_region(_rgn, base, size);

}

long __init lmb_reserve(unsigned long base, unsigned long size)
{
	struct lmb_region *_rgn = &(lmb.reserved);

	BUG_ON(0 == size);

	return lmb_add_region(_rgn, base, size);
}

long __init lmb_overlaps_region(struct lmb_region *rgn, unsigned long base,
				unsigned long size)
{
	unsigned long i;

	for (i=0; i < rgn->cnt; i++) {
		unsigned long rgnbase = rgn->region[i].base;
		unsigned long rgnsize = rgn->region[i].size;
		if ( lmb_addrs_overlap(base,size,rgnbase,rgnsize) ) {
			break;
		}
	}

	return (i < rgn->cnt) ? i : -1;
}

unsigned long __init lmb_alloc(unsigned long size, unsigned long align)
{
	return lmb_alloc_base(size, align, LMB_ALLOC_ANYWHERE);
}

unsigned long __init lmb_alloc_base(unsigned long size, unsigned long align,
				    unsigned long max_addr)
{
	unsigned long alloc;

	alloc = __lmb_alloc_base(size, align, max_addr);

	if (alloc == 0)
		panic("ERROR: Failed to allocate 0x%lx bytes below 0x%lx.\n",
				size, max_addr);

	return alloc;
}

unsigned long __init __lmb_alloc_base(unsigned long size, unsigned long align,
				    unsigned long max_addr)
{
	long i, j;
	unsigned long base = 0;

	BUG_ON(0 == size);

#ifdef CONFIG_PPC32
	/* On 32-bit, make sure we allocate lowmem */
	if (max_addr == LMB_ALLOC_ANYWHERE)
		max_addr = __max_low_memory;
#endif
	for (i = lmb.memory.cnt-1; i >= 0; i--) {
		unsigned long lmbbase = lmb.memory.region[i].base;
		unsigned long lmbsize = lmb.memory.region[i].size;

		if (max_addr == LMB_ALLOC_ANYWHERE)
			base = _ALIGN_DOWN(lmbbase + lmbsize - size, align);
		else if (lmbbase < max_addr) {
			base = min(lmbbase + lmbsize, max_addr);
			base = _ALIGN_DOWN(base - size, align);
		} else
			continue;

		while ((lmbbase <= base) &&
		       ((j = lmb_overlaps_region(&lmb.reserved, base, size)) >= 0) )
			base = _ALIGN_DOWN(lmb.reserved.region[j].base - size,
					   align);

		if ((base != 0) && (lmbbase <= base))
			break;
	}

	if (i < 0)
		return 0;

	lmb_add_region(&lmb.reserved, base, size);

	return base;
}

/* You must call lmb_analyze() before this. */
unsigned long __init lmb_phys_mem_size(void)
{
	return lmb.memory.size;
}

unsigned long __init lmb_end_of_DRAM(void)
{
	int idx = lmb.memory.cnt - 1;

	return (lmb.memory.region[idx].base + lmb.memory.region[idx].size);
}

/* You must call lmb_analyze() after this. */
void __init lmb_enforce_memory_limit(unsigned long memory_limit)
{
	unsigned long i, limit;
	struct lmb_property *p;

	if (! memory_limit)
		return;

	/* Truncate the lmb regions to satisfy the memory limit. */
	limit = memory_limit;
	for (i = 0; i < lmb.memory.cnt; i++) {
		if (limit > lmb.memory.region[i].size) {
			limit -= lmb.memory.region[i].size;
			continue;
		}

		lmb.memory.region[i].size = limit;
		lmb.memory.cnt = i + 1;
		break;
	}

	if (lmb.memory.region[0].size < lmb.rmo_size)
		lmb.rmo_size = lmb.memory.region[0].size;

	/* And truncate any reserves above the limit also. */
	for (i = 0; i < lmb.reserved.cnt; i++) {
		p = &lmb.reserved.region[i];

		if (p->base > memory_limit)
			p->size = 0;
		else if ((p->base + p->size) > memory_limit)
			p->size = memory_limit - p->base;

		if (p->size == 0) {
			lmb_remove_region(&lmb.reserved, i);
			i--;
		}
	}
}

int __init lmb_is_reserved(unsigned long addr)
{
	int i;

	for (i = 0; i < lmb.reserved.cnt; i++) {
		unsigned long upper = lmb.reserved.region[i].base +
				      lmb.reserved.region[i].size - 1;
		if ((addr >= lmb.reserved.region[i].base) && (addr <= upper))
			return 1;
	}
	return 0;
}
