/*
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/libnvdimm.h>
#include <linux/badblocks.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/ctype.h>
#include <linux/ndctl.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/io.h>
#include "nd-core.h"
#include "nd.h"

void badrange_init(struct badrange *badrange)
{
	INIT_LIST_HEAD(&badrange->list);
	spin_lock_init(&badrange->lock);
}
EXPORT_SYMBOL_GPL(badrange_init);

static void append_badrange_entry(struct badrange *badrange,
		struct badrange_entry *bre, u64 addr, u64 length)
{
	lockdep_assert_held(&badrange->lock);
	bre->start = addr;
	bre->length = length;
	list_add_tail(&bre->list, &badrange->list);
}

static int alloc_and_append_badrange_entry(struct badrange *badrange,
		u64 addr, u64 length, gfp_t flags)
{
	struct badrange_entry *bre;

	bre = kzalloc(sizeof(*bre), flags);
	if (!bre)
		return -ENOMEM;

	append_badrange_entry(badrange, bre, addr, length);
	return 0;
}

static int add_badrange(struct badrange *badrange, u64 addr, u64 length)
{
	struct badrange_entry *bre, *bre_new;

	spin_unlock(&badrange->lock);
	bre_new = kzalloc(sizeof(*bre_new), GFP_KERNEL);
	spin_lock(&badrange->lock);

	if (list_empty(&badrange->list)) {
		if (!bre_new)
			return -ENOMEM;
		append_badrange_entry(badrange, bre_new, addr, length);
		return 0;
	}

	/*
	 * There is a chance this is a duplicate, check for those first.
	 * This will be the common case as ARS_STATUS returns all known
	 * errors in the SPA space, and we can't query it per region
	 */
	list_for_each_entry(bre, &badrange->list, list)
		if (bre->start == addr) {
			/* If length has changed, update this list entry */
			if (bre->length != length)
				bre->length = length;
			kfree(bre_new);
			return 0;
		}

	/*
	 * If not a duplicate or a simple length update, add the entry as is,
	 * as any overlapping ranges will get resolved when the list is consumed
	 * and converted to badblocks
	 */
	if (!bre_new)
		return -ENOMEM;
	append_badrange_entry(badrange, bre_new, addr, length);

	return 0;
}

int badrange_add(struct badrange *badrange, u64 addr, u64 length)
{
	int rc;

	spin_lock(&badrange->lock);
	rc = add_badrange(badrange, addr, length);
	spin_unlock(&badrange->lock);

	return rc;
}
EXPORT_SYMBOL_GPL(badrange_add);

void badrange_forget(struct badrange *badrange, phys_addr_t start,
		unsigned int len)
{
	struct list_head *badrange_list = &badrange->list;
	u64 clr_end = start + len - 1;
	struct badrange_entry *bre, *next;

	spin_lock(&badrange->lock);

	/*
	 * [start, clr_end] is the badrange interval being cleared.
	 * [bre->start, bre_end] is the badrange_list entry we're comparing
	 * the above interval against. The badrange list entry may need
	 * to be modified (update either start or length), deleted, or
	 * split into two based on the overlap characteristics
	 */

	list_for_each_entry_safe(bre, next, badrange_list, list) {
		u64 bre_end = bre->start + bre->length - 1;

		/* Skip intervals with no intersection */
		if (bre_end < start)
			continue;
		if (bre->start >  clr_end)
			continue;
		/* Delete completely overlapped badrange entries */
		if ((bre->start >= start) && (bre_end <= clr_end)) {
			list_del(&bre->list);
			kfree(bre);
			continue;
		}
		/* Adjust start point of partially cleared entries */
		if ((start <= bre->start) && (clr_end > bre->start)) {
			bre->length -= clr_end - bre->start + 1;
			bre->start = clr_end + 1;
			continue;
		}
		/* Adjust bre->length for partial clearing at the tail end */
		if ((bre->start < start) && (bre_end <= clr_end)) {
			/* bre->start remains the same */
			bre->length = start - bre->start;
			continue;
		}
		/*
		 * If clearing in the middle of an entry, we split it into
		 * two by modifying the current entry to represent one half of
		 * the split, and adding a new entry for the second half.
		 */
		if ((bre->start < start) && (bre_end > clr_end)) {
			u64 new_start = clr_end + 1;
			u64 new_len = bre_end - new_start + 1;

			/* Add new entry covering the right half */
			alloc_and_append_badrange_entry(badrange, new_start,
					new_len, GFP_NOWAIT);
			/* Adjust this entry to cover the left half */
			bre->length = start - bre->start;
			continue;
		}
	}
	spin_unlock(&badrange->lock);
}
EXPORT_SYMBOL_GPL(badrange_forget);

static void set_badblock(struct badblocks *bb, sector_t s, int num)
{
	dev_dbg(bb->dev, "Found a bad range (0x%llx, 0x%llx)\n",
			(u64) s * 512, (u64) num * 512);
	/* this isn't an error as the hardware will still throw an exception */
	if (badblocks_set(bb, s, num, 1))
		dev_info_once(bb->dev, "%s: failed for sector %llx\n",
				__func__, (u64) s);
}

/**
 * __add_badblock_range() - Convert a physical address range to bad sectors
 * @bb:		badblocks instance to populate
 * @ns_offset:	namespace offset where the error range begins (in bytes)
 * @len:	number of bytes of badrange to be added
 *
 * This assumes that the range provided with (ns_offset, len) is within
 * the bounds of physical addresses for this namespace, i.e. lies in the
 * interval [ns_start, ns_start + ns_size)
 */
static void __add_badblock_range(struct badblocks *bb, u64 ns_offset, u64 len)
{
	const unsigned int sector_size = 512;
	sector_t start_sector, end_sector;
	u64 num_sectors;
	u32 rem;

	start_sector = div_u64(ns_offset, sector_size);
	end_sector = div_u64_rem(ns_offset + len, sector_size, &rem);
	if (rem)
		end_sector++;
	num_sectors = end_sector - start_sector;

	if (unlikely(num_sectors > (u64)INT_MAX)) {
		u64 remaining = num_sectors;
		sector_t s = start_sector;

		while (remaining) {
			int done = min_t(u64, remaining, INT_MAX);

			set_badblock(bb, s, done);
			remaining -= done;
			s += done;
		}
	} else
		set_badblock(bb, start_sector, num_sectors);
}

static void badblocks_populate(struct badrange *badrange,
		struct badblocks *bb, const struct resource *res)
{
	struct badrange_entry *bre;

	if (list_empty(&badrange->list))
		return;

	list_for_each_entry(bre, &badrange->list, list) {
		u64 bre_end = bre->start + bre->length - 1;

		/* Discard intervals with no intersection */
		if (bre_end < res->start)
			continue;
		if (bre->start >  res->end)
			continue;
		/* Deal with any overlap after start of the namespace */
		if (bre->start >= res->start) {
			u64 start = bre->start;
			u64 len;

			if (bre_end <= res->end)
				len = bre->length;
			else
				len = res->start + resource_size(res)
					- bre->start;
			__add_badblock_range(bb, start - res->start, len);
			continue;
		}
		/*
		 * Deal with overlap for badrange starting before
		 * the namespace.
		 */
		if (bre->start < res->start) {
			u64 len;

			if (bre_end < res->end)
				len = bre->start + bre->length - res->start;
			else
				len = resource_size(res);
			__add_badblock_range(bb, 0, len);
		}
	}
}

/**
 * nvdimm_badblocks_populate() - Convert a list of badranges to badblocks
 * @region: parent region of the range to interrogate
 * @bb: badblocks instance to populate
 * @res: resource range to consider
 *
 * The badrange list generated during bus initialization may contain
 * multiple, possibly overlapping physical address ranges.  Compare each
 * of these ranges to the resource range currently being initialized,
 * and add badblocks entries for all matching sub-ranges
 */
void nvdimm_badblocks_populate(struct nd_region *nd_region,
		struct badblocks *bb, const struct resource *res)
{
	struct nvdimm_bus *nvdimm_bus;

	if (!is_memory(&nd_region->dev)) {
		dev_WARN_ONCE(&nd_region->dev, 1,
				"%s only valid for pmem regions\n", __func__);
		return;
	}
	nvdimm_bus = walk_to_nvdimm_bus(&nd_region->dev);

	nvdimm_bus_lock(&nvdimm_bus->dev);
	badblocks_populate(&nvdimm_bus->badrange, bb, res);
	nvdimm_bus_unlock(&nvdimm_bus->dev);
}
EXPORT_SYMBOL_GPL(nvdimm_badblocks_populate);
