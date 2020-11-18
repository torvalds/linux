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

/**
 * add_tce_mem_ranges - Adds tce-table range to the given memory ranges list.
 * @mem_ranges:         Range list to add the memory range(s) to.
 *
 * Returns 0 on success, negative errno on error.
 */
int add_tce_mem_ranges(struct crash_mem **mem_ranges)
{
	struct device_node *dn = NULL;
	int ret = 0;

	for_each_node_by_type(dn, "pci") {
		u64 base;
		u32 size;

		ret = of_property_read_u64(dn, "linux,tce-base", &base);
		ret |= of_property_read_u32(dn, "linux,tce-size", &size);
		if (ret) {
			/*
			 * It is ok to have pci nodes without tce. So, ignore
			 * property does not exist error.
			 */
			if (ret == -EINVAL) {
				ret = 0;
				continue;
			}
			break;
		}

		ret = add_mem_range(mem_ranges, base, size);
		if (ret)
			break;
	}

	of_node_put(dn);
	return ret;
}

/**
 * add_initrd_mem_range - Adds initrd range to the given memory ranges list,
 *                        if the initrd was retained.
 * @mem_ranges:           Range list to add the memory range to.
 *
 * Returns 0 on success, negative errno on error.
 */
int add_initrd_mem_range(struct crash_mem **mem_ranges)
{
	u64 base, end;
	int ret;

	/* This range means something, only if initrd was retained */
	if (!strstr(saved_command_line, "retain_initrd"))
		return 0;

	ret = of_property_read_u64(of_chosen, "linux,initrd-start", &base);
	ret |= of_property_read_u64(of_chosen, "linux,initrd-end", &end);
	if (!ret)
		ret = add_mem_range(mem_ranges, base, end - base + 1);

	return ret;
}

#ifdef CONFIG_PPC_BOOK3S_64
/**
 * add_htab_mem_range - Adds htab range to the given memory ranges list,
 *                      if it exists
 * @mem_ranges:         Range list to add the memory range to.
 *
 * Returns 0 on success, negative errno on error.
 */
int add_htab_mem_range(struct crash_mem **mem_ranges)
{
	if (!htab_address)
		return 0;

	return add_mem_range(mem_ranges, __pa(htab_address), htab_size_bytes);
}
#endif

/**
 * add_kernel_mem_range - Adds kernel text region to the given
 *                        memory ranges list.
 * @mem_ranges:           Range list to add the memory range to.
 *
 * Returns 0 on success, negative errno on error.
 */
int add_kernel_mem_range(struct crash_mem **mem_ranges)
{
	return add_mem_range(mem_ranges, 0, __pa(_end));
}

/**
 * add_rtas_mem_range - Adds RTAS region to the given memory ranges list.
 * @mem_ranges:         Range list to add the memory range to.
 *
 * Returns 0 on success, negative errno on error.
 */
int add_rtas_mem_range(struct crash_mem **mem_ranges)
{
	struct device_node *dn;
	u32 base, size;
	int ret = 0;

	dn = of_find_node_by_path("/rtas");
	if (!dn)
		return 0;

	ret = of_property_read_u32(dn, "linux,rtas-base", &base);
	ret |= of_property_read_u32(dn, "rtas-size", &size);
	if (!ret)
		ret = add_mem_range(mem_ranges, base, size);

	of_node_put(dn);
	return ret;
}

/**
 * add_opal_mem_range - Adds OPAL region to the given memory ranges list.
 * @mem_ranges:         Range list to add the memory range to.
 *
 * Returns 0 on success, negative errno on error.
 */
int add_opal_mem_range(struct crash_mem **mem_ranges)
{
	struct device_node *dn;
	u64 base, size;
	int ret;

	dn = of_find_node_by_path("/ibm,opal");
	if (!dn)
		return 0;

	ret = of_property_read_u64(dn, "opal-base-address", &base);
	ret |= of_property_read_u64(dn, "opal-runtime-size", &size);
	if (!ret)
		ret = add_mem_range(mem_ranges, base, size);

	of_node_put(dn);
	return ret;
}

/**
 * add_reserved_mem_ranges - Adds "/reserved-ranges" regions exported by f/w
 *                           to the given memory ranges list.
 * @mem_ranges:              Range list to add the memory ranges to.
 *
 * Returns 0 on success, negative errno on error.
 */
int add_reserved_mem_ranges(struct crash_mem **mem_ranges)
{
	int n_mem_addr_cells, n_mem_size_cells, i, len, cells, ret = 0;
	const __be32 *prop;

	prop = of_get_property(of_root, "reserved-ranges", &len);
	if (!prop)
		return 0;

	n_mem_addr_cells = of_n_addr_cells(of_root);
	n_mem_size_cells = of_n_size_cells(of_root);
	cells = n_mem_addr_cells + n_mem_size_cells;

	/* Each reserved range is an (address,size) pair */
	for (i = 0; i < (len / (sizeof(u32) * cells)); i++) {
		u64 base, size;

		base = of_read_number(prop + (i * cells), n_mem_addr_cells);
		size = of_read_number(prop + (i * cells) + n_mem_addr_cells,
				      n_mem_size_cells);

		ret = add_mem_range(mem_ranges, base, size);
		if (ret)
			break;
	}

	return ret;
}
