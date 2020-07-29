// SPDX-License-Identifier: GPL-2.0-only
/*
 * powerpc code to implement the kexec_file_load syscall
 *
 * Copyright (C) 2004  Adam Litke (agl@us.ibm.com)
 * Copyright (C) 2004  IBM Corp.
 * Copyright (C) 2004,2005  Milton D Miller II, IBM Corporation
 * Copyright (C) 2005  R Sharada (sharada@in.ibm.com)
 * Copyright (C) 2006  Mohan Kumar M (mohan@in.ibm.com)
 * Copyright (C) 2020  IBM Corporation
 *
 * Based on kexec-tools' kexec-ppc64.c, fs2dt.c.
 * Heavily modified for the kernel by
 * Hari Bathini, IBM Corporation.
 */

#define pr_fmt(fmt) "kexec ranges: " fmt

#include <linux/sort.h>
#include <linux/kexec.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <asm/sections.h>
#include <asm/kexec_ranges.h>

/**
 * get_max_nr_ranges - Get the max no. of ranges crash_mem structure
 *                     could hold, given the size allocated for it.
 * @size:              Allocation size of crash_mem structure.
 *
 * Returns the maximum no. of ranges.
 */
static inline unsigned int get_max_nr_ranges(size_t size)
{
	return ((size - sizeof(struct crash_mem)) /
		sizeof(struct crash_mem_range));
}

/**
 * get_mem_rngs_size - Get the allocated size of mem_rngs based on
 *                     max_nr_ranges and chunk size.
 * @mem_rngs:          Memory ranges.
 *
 * Returns the maximum size of @mem_rngs.
 */
static inline size_t get_mem_rngs_size(struct crash_mem *mem_rngs)
{
	size_t size;

	if (!mem_rngs)
		return 0;

	size = (sizeof(struct crash_mem) +
		(mem_rngs->max_nr_ranges * sizeof(struct crash_mem_range)));

	/*
	 * Memory is allocated in size multiple of MEM_RANGE_CHUNK_SZ.
	 * So, align to get the actual length.
	 */
	return ALIGN(size, MEM_RANGE_CHUNK_SZ);
}

/**
 * __add_mem_range - add a memory range to memory ranges list.
 * @mem_ranges:      Range list to add the memory range to.
 * @base:            Base address of the range to add.
 * @size:            Size of the memory range to add.
 *
 * (Re)allocates memory, if needed.
 *
 * Returns 0 on success, negative errno on error.
 */
static int __add_mem_range(struct crash_mem **mem_ranges, u64 base, u64 size)
{
	struct crash_mem *mem_rngs = *mem_ranges;

	if (!mem_rngs || (mem_rngs->nr_ranges == mem_rngs->max_nr_ranges)) {
		mem_rngs = realloc_mem_ranges(mem_ranges);
		if (!mem_rngs)
			return -ENOMEM;
	}

	mem_rngs->ranges[mem_rngs->nr_ranges].start = base;
	mem_rngs->ranges[mem_rngs->nr_ranges].end = base + size - 1;
	pr_debug("Added memory range [%#016llx - %#016llx] at index %d\n",
		 base, base + size - 1, mem_rngs->nr_ranges);
	mem_rngs->nr_ranges++;
	return 0;
}

/**
 * __merge_memory_ranges - Merges the given memory ranges list.
 * @mem_rngs:              Range list to merge.
 *
 * Assumes a sorted range list.
 *
 * Returns nothing.
 */
static void __merge_memory_ranges(struct crash_mem *mem_rngs)
{
	struct crash_mem_range *ranges;
	int i, idx;

	if (!mem_rngs)
		return;

	idx = 0;
	ranges = &(mem_rngs->ranges[0]);
	for (i = 1; i < mem_rngs->nr_ranges; i++) {
		if (ranges[i].start <= (ranges[i-1].end + 1))
			ranges[idx].end = ranges[i].end;
		else {
			idx++;
			if (i == idx)
				continue;

			ranges[idx] = ranges[i];
		}
	}
	mem_rngs->nr_ranges = idx + 1;
}

/* cmp_func_t callback to sort ranges with sort() */
static int rngcmp(const void *_x, const void *_y)
{
	const struct crash_mem_range *x = _x, *y = _y;

	if (x->start > y->start)
		return 1;
	if (x->start < y->start)
		return -1;
	return 0;
}

/**
 * sort_memory_ranges - Sorts the given memory ranges list.
 * @mem_rngs:           Range list to sort.
 * @merge:              If true, merge the list after sorting.
 *
 * Returns nothing.
 */
void sort_memory_ranges(struct crash_mem *mem_rngs, bool merge)
{
	int i;

	if (!mem_rngs)
		return;

	/* Sort the ranges in-place */
	sort(&(mem_rngs->ranges[0]), mem_rngs->nr_ranges,
	     sizeof(mem_rngs->ranges[0]), rngcmp, NULL);

	if (merge)
		__merge_memory_ranges(mem_rngs);

	/* For debugging purpose */
	pr_debug("Memory ranges:\n");
	for (i = 0; i < mem_rngs->nr_ranges; i++) {
		pr_debug("\t[%03d][%#016llx - %#016llx]\n", i,
			 mem_rngs->ranges[i].start,
			 mem_rngs->ranges[i].end);
	}
}

/**
 * realloc_mem_ranges - reallocate mem_ranges with size incremented
 *                      by MEM_RANGE_CHUNK_SZ. Frees up the old memory,
 *                      if memory allocation fails.
 * @mem_ranges:         Memory ranges to reallocate.
 *
 * Returns pointer to reallocated memory on success, NULL otherwise.
 */
struct crash_mem *realloc_mem_ranges(struct crash_mem **mem_ranges)
{
	struct crash_mem *mem_rngs = *mem_ranges;
	unsigned int nr_ranges;
	size_t size;

	size = get_mem_rngs_size(mem_rngs);
	nr_ranges = mem_rngs ? mem_rngs->nr_ranges : 0;

	size += MEM_RANGE_CHUNK_SZ;
	mem_rngs = krealloc(*mem_ranges, size, GFP_KERNEL);
	if (!mem_rngs) {
		kfree(*mem_ranges);
		*mem_ranges = NULL;
		return NULL;
	}

	mem_rngs->nr_ranges = nr_ranges;
	mem_rngs->max_nr_ranges = get_max_nr_ranges(size);
	*mem_ranges = mem_rngs;

	return mem_rngs;
}

/**
 * add_mem_range - Updates existing memory range, if there is an overlap.
 *                 Else, adds a new memory range.
 * @mem_ranges:    Range list to add the memory range to.
 * @base:          Base address of the range to add.
 * @size:          Size of the memory range to add.
 *
 * (Re)allocates memory, if needed.
 *
 * Returns 0 on success, negative errno on error.
 */
int add_mem_range(struct crash_mem **mem_ranges, u64 base, u64 size)
{
	struct crash_mem *mem_rngs = *mem_ranges;
	u64 mstart, mend, end;
	unsigned int i;

	if (!size)
		return 0;

	end = base + size - 1;

	if (!mem_rngs || !(mem_rngs->nr_ranges))
		return __add_mem_range(mem_ranges, base, size);

	for (i = 0; i < mem_rngs->nr_ranges; i++) {
		mstart = mem_rngs->ranges[i].start;
		mend = mem_rngs->ranges[i].end;
		if (base < mend && end > mstart) {
			if (base < mstart)
				mem_rngs->ranges[i].start = base;
			if (end > mend)
				mem_rngs->ranges[i].end = end;
			return 0;
		}
	}

	return __add_mem_range(mem_ranges, base, size);
}
