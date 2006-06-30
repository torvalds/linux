/*
 *    Copyright (c) 1996 Paul Mackerras <paulus@cs.anu.edu.au>
 *      Changes to accommodate Power Macintoshes.
 *    Cort Dougan <cort@cs.nmt.edu>
 *      Rewrites.
 *    Grant Erickson <grant@lcse.umn.edu>
 *      General rework and split from mm/init.c.
 *
 *    Module name: mem_pieces.c
 *
 *    Description:
 *      Routines and data structures for manipulating and representing
 *      phyiscal memory extents (i.e. address/length pairs).
 *
 */

#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <asm/page.h>

#include "mem_pieces.h"

extern struct mem_pieces phys_avail;

static void mem_pieces_print(struct mem_pieces *);

/*
 * Scan a region for a piece of a given size with the required alignment.
 */
void __init *
mem_pieces_find(unsigned int size, unsigned int align)
{
	int i;
	unsigned a, e;
	struct mem_pieces *mp = &phys_avail;

	for (i = 0; i < mp->n_regions; ++i) {
		a = mp->regions[i].address;
		e = a + mp->regions[i].size;
		a = (a + align - 1) & -align;
		if (a + size <= e) {
			mem_pieces_remove(mp, a, size, 1);
			return (void *) __va(a);
		}
	}
	panic("Couldn't find %u bytes at %u alignment\n", size, align);

	return NULL;
}

/*
 * Remove some memory from an array of pieces
 */
void __init
mem_pieces_remove(struct mem_pieces *mp, unsigned int start, unsigned int size,
		  int must_exist)
{
	int i, j;
	unsigned int end, rs, re;
	struct reg_property *rp;

	end = start + size;
	for (i = 0, rp = mp->regions; i < mp->n_regions; ++i, ++rp) {
		if (end > rp->address && start < rp->address + rp->size)
			break;
	}
	if (i >= mp->n_regions) {
		if (must_exist)
			printk("mem_pieces_remove: [%x,%x) not in any region\n",
			       start, end);
		return;
	}
	for (; i < mp->n_regions && end > rp->address; ++i, ++rp) {
		rs = rp->address;
		re = rs + rp->size;
		if (must_exist && (start < rs || end > re)) {
			printk("mem_pieces_remove: bad overlap [%x,%x) with",
			       start, end);
			mem_pieces_print(mp);
			must_exist = 0;
		}
		if (start > rs) {
			rp->size = start - rs;
			if (end < re) {
				/* need to split this entry */
				if (mp->n_regions >= MEM_PIECES_MAX)
					panic("eek... mem_pieces overflow");
				for (j = mp->n_regions; j > i + 1; --j)
					mp->regions[j] = mp->regions[j-1];
				++mp->n_regions;
				rp[1].address = end;
				rp[1].size = re - end;
			}
		} else {
			if (end < re) {
				rp->address = end;
				rp->size = re - end;
			} else {
				/* need to delete this entry */
				for (j = i; j < mp->n_regions - 1; ++j)
					mp->regions[j] = mp->regions[j+1];
				--mp->n_regions;
				--i;
				--rp;
			}
		}
	}
}

static void __init
mem_pieces_print(struct mem_pieces *mp)
{
	int i;

	for (i = 0; i < mp->n_regions; ++i)
		printk(" [%x, %x)", mp->regions[i].address,
		       mp->regions[i].address + mp->regions[i].size);
	printk("\n");
}

void __init
mem_pieces_sort(struct mem_pieces *mp)
{
	unsigned long a, s;
	int i, j;

	for (i = 1; i < mp->n_regions; ++i) {
		a = mp->regions[i].address;
		s = mp->regions[i].size;
		for (j = i - 1; j >= 0; --j) {
			if (a >= mp->regions[j].address)
				break;
			mp->regions[j+1] = mp->regions[j];
		}
		mp->regions[j+1].address = a;
		mp->regions[j+1].size = s;
	}
}

void __init
mem_pieces_coalesce(struct mem_pieces *mp)
{
	unsigned long a, s, ns;
	int i, j, d;

	d = 0;
	for (i = 0; i < mp->n_regions; i = j) {
		a = mp->regions[i].address;
		s = mp->regions[i].size;
		for (j = i + 1; j < mp->n_regions
			     && mp->regions[j].address - a <= s; ++j) {
			ns = mp->regions[j].address + mp->regions[j].size - a;
			if (ns > s)
				s = ns;
		}
		mp->regions[d].address = a;
		mp->regions[d].size = s;
		++d;
	}
	mp->n_regions = d;
}
