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
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/memblock.h>
#include <linux/crash_core.h>
#include <asm/sections.h>
#include <asm/kexec_ranges.h>
#include <asm/crashdump-ppc64.h>

#if defined(CONFIG_KEXEC_FILE) || defined(CONFIG_CRASH_DUMP)
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
		sizeof(struct range));
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
		(mem_rngs->max_nr_ranges * sizeof(struct range)));

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
	struct range *ranges;
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
	const struct range *x = _x, *y = _y;

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

#endif /* CONFIG_KEXEC_FILE || CONFIG_CRASH_DUMP */

#ifdef CONFIG_KEXEC_FILE
/**
 * add_tce_mem_ranges - Adds tce-table range to the given memory ranges list.
 * @mem_ranges:         Range list to add the memory range(s) to.
 *
 * Returns 0 on success, negative errno on error.
 */
static int add_tce_mem_ranges(struct crash_mem **mem_ranges)
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
static int add_initrd_mem_range(struct crash_mem **mem_ranges)
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

/**
 * add_htab_mem_range - Adds htab range to the given memory ranges list,
 *                      if it exists
 * @mem_ranges:         Range list to add the memory range to.
 *
 * Returns 0 on success, negative errno on error.
 */
static int add_htab_mem_range(struct crash_mem **mem_ranges)
{

#ifdef CONFIG_PPC_64S_HASH_MMU
	if (!htab_address)
		return 0;

	return add_mem_range(mem_ranges, __pa(htab_address), htab_size_bytes);
#else
	return 0;
#endif
}

/**
 * add_kernel_mem_range - Adds kernel text region to the given
 *                        memory ranges list.
 * @mem_ranges:           Range list to add the memory range to.
 *
 * Returns 0 on success, negative errno on error.
 */
static int add_kernel_mem_range(struct crash_mem **mem_ranges)
{
	return add_mem_range(mem_ranges, 0, __pa(_end));
}
#endif /* CONFIG_KEXEC_FILE */

#if defined(CONFIG_KEXEC_FILE) || defined(CONFIG_CRASH_DUMP)
/**
 * add_rtas_mem_range - Adds RTAS region to the given memory ranges list.
 * @mem_ranges:         Range list to add the memory range to.
 *
 * Returns 0 on success, negative errno on error.
 */
static int add_rtas_mem_range(struct crash_mem **mem_ranges)
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
static int add_opal_mem_range(struct crash_mem **mem_ranges)
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
#endif /* CONFIG_KEXEC_FILE || CONFIG_CRASH_DUMP */

#ifdef CONFIG_KEXEC_FILE
/**
 * add_reserved_mem_ranges - Adds "/reserved-ranges" regions exported by f/w
 *                           to the given memory ranges list.
 * @mem_ranges:              Range list to add the memory ranges to.
 *
 * Returns 0 on success, negative errno on error.
 */
static int add_reserved_mem_ranges(struct crash_mem **mem_ranges)
{
	int n_mem_addr_cells, n_mem_size_cells, i, len, cells, ret = 0;
	struct device_node *root = of_find_node_by_path("/");
	const __be32 *prop;

	prop = of_get_property(root, "reserved-ranges", &len);
	n_mem_addr_cells = of_n_addr_cells(root);
	n_mem_size_cells = of_n_size_cells(root);
	of_node_put(root);
	if (!prop)
		return 0;

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

/**
 * get_reserved_memory_ranges - Get reserve memory ranges. This list includes
 *                              memory regions that should be added to the
 *                              memory reserve map to ensure the region is
 *                              protected from any mischief.
 * @mem_ranges:                 Range list to add the memory ranges to.
 *
 * Returns 0 on success, negative errno on error.
 */
int get_reserved_memory_ranges(struct crash_mem **mem_ranges)
{
	int ret;

	ret = add_rtas_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_tce_mem_ranges(mem_ranges);
	if (ret)
		goto out;

	ret = add_reserved_mem_ranges(mem_ranges);
out:
	if (ret)
		pr_err("Failed to setup reserved memory ranges\n");
	return ret;
}

/**
 * get_exclude_memory_ranges - Get exclude memory ranges. This list includes
 *                             regions like opal/rtas, tce-table, initrd,
 *                             kernel, htab which should be avoided while
 *                             setting up kexec load segments.
 * @mem_ranges:                Range list to add the memory ranges to.
 *
 * Returns 0 on success, negative errno on error.
 */
int get_exclude_memory_ranges(struct crash_mem **mem_ranges)
{
	int ret;

	ret = add_tce_mem_ranges(mem_ranges);
	if (ret)
		goto out;

	ret = add_initrd_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_htab_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_kernel_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_rtas_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_opal_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_reserved_mem_ranges(mem_ranges);
	if (ret)
		goto out;

	/* exclude memory ranges should be sorted for easy lookup */
	sort_memory_ranges(*mem_ranges, true);
out:
	if (ret)
		pr_err("Failed to setup exclude memory ranges\n");
	return ret;
}

#ifdef CONFIG_CRASH_DUMP
/**
 * get_usable_memory_ranges - Get usable memory ranges. This list includes
 *                            regions like crashkernel, opal/rtas & tce-table,
 *                            that kdump kernel could use.
 * @mem_ranges:               Range list to add the memory ranges to.
 *
 * Returns 0 on success, negative errno on error.
 */
int get_usable_memory_ranges(struct crash_mem **mem_ranges)
{
	int ret;

	/*
	 * Early boot failure observed on guests when low memory (first memory
	 * block?) is not added to usable memory. So, add [0, crashk_res.end]
	 * instead of [crashk_res.start, crashk_res.end] to workaround it.
	 * Also, crashed kernel's memory must be added to reserve map to
	 * avoid kdump kernel from using it.
	 */
	ret = add_mem_range(mem_ranges, 0, crashk_res.end + 1);
	if (ret)
		goto out;

	ret = add_rtas_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_opal_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_tce_mem_ranges(mem_ranges);
out:
	if (ret)
		pr_err("Failed to setup usable memory ranges\n");
	return ret;
}
#endif /* CONFIG_CRASH_DUMP */
#endif /* CONFIG_KEXEC_FILE */

#ifdef CONFIG_CRASH_DUMP
/**
 * get_crash_memory_ranges - Get crash memory ranges. This list includes
 *                           first/crashing kernel's memory regions that
 *                           would be exported via an elfcore.
 * @mem_ranges:              Range list to add the memory ranges to.
 *
 * Returns 0 on success, negative errno on error.
 */
int get_crash_memory_ranges(struct crash_mem **mem_ranges)
{
	phys_addr_t base, end;
	struct crash_mem *tmem;
	u64 i;
	int ret;

	for_each_mem_range(i, &base, &end) {
		u64 size = end - base;

		/* Skip backup memory region, which needs a separate entry */
		if (base == BACKUP_SRC_START) {
			if (size > BACKUP_SRC_SIZE) {
				base = BACKUP_SRC_END + 1;
				size -= BACKUP_SRC_SIZE;
			} else
				continue;
		}

		ret = add_mem_range(mem_ranges, base, size);
		if (ret)
			goto out;

		/* Try merging adjacent ranges before reallocation attempt */
		if ((*mem_ranges)->nr_ranges == (*mem_ranges)->max_nr_ranges)
			sort_memory_ranges(*mem_ranges, true);
	}

	/* Reallocate memory ranges if there is no space to split ranges */
	tmem = *mem_ranges;
	if (tmem && (tmem->nr_ranges == tmem->max_nr_ranges)) {
		tmem = realloc_mem_ranges(mem_ranges);
		if (!tmem)
			goto out;
	}

	/* Exclude crashkernel region */
	ret = crash_exclude_mem_range(tmem, crashk_res.start, crashk_res.end);
	if (ret)
		goto out;

	/*
	 * FIXME: For now, stay in parity with kexec-tools but if RTAS/OPAL
	 *        regions are exported to save their context at the time of
	 *        crash, they should actually be backed up just like the
	 *        first 64K bytes of memory.
	 */
	ret = add_rtas_mem_range(mem_ranges);
	if (ret)
		goto out;

	ret = add_opal_mem_range(mem_ranges);
	if (ret)
		goto out;

	/* create a separate program header for the backup region */
	ret = add_mem_range(mem_ranges, BACKUP_SRC_START, BACKUP_SRC_SIZE);
	if (ret)
		goto out;

	sort_memory_ranges(*mem_ranges, false);
out:
	if (ret)
		pr_err("Failed to setup crash memory ranges\n");
	return ret;
}

/**
 * remove_mem_range - Removes the given memory range from the range list.
 * @mem_ranges:    Range list to remove the memory range to.
 * @base:          Base address of the range to remove.
 * @size:          Size of the memory range to remove.
 *
 * (Re)allocates memory, if needed.
 *
 * Returns 0 on success, negative errno on error.
 */
int remove_mem_range(struct crash_mem **mem_ranges, u64 base, u64 size)
{
	u64 end;
	int ret = 0;
	unsigned int i;
	u64 mstart, mend;
	struct crash_mem *mem_rngs = *mem_ranges;

	if (!size)
		return 0;

	/*
	 * Memory range are stored as start and end address, use
	 * the same format to do remove operation.
	 */
	end = base + size - 1;

	for (i = 0; i < mem_rngs->nr_ranges; i++) {
		mstart = mem_rngs->ranges[i].start;
		mend = mem_rngs->ranges[i].end;

		/*
		 * Memory range to remove is not part of this range entry
		 * in the memory range list
		 */
		if (!(base >= mstart && end <= mend))
			continue;

		/*
		 * Memory range to remove is equivalent to this entry in the
		 * memory range list. Remove the range entry from the list.
		 */
		if (base == mstart && end == mend) {
			for (; i < mem_rngs->nr_ranges - 1; i++) {
				mem_rngs->ranges[i].start = mem_rngs->ranges[i+1].start;
				mem_rngs->ranges[i].end = mem_rngs->ranges[i+1].end;
			}
			mem_rngs->nr_ranges--;
			goto out;
		}
		/*
		 * Start address of the memory range to remove and the
		 * current memory range entry in the list is same. Just
		 * move the start address of the current memory range
		 * entry in the list to end + 1.
		 */
		else if (base == mstart) {
			mem_rngs->ranges[i].start = end + 1;
			goto out;
		}
		/*
		 * End address of the memory range to remove and the
		 * current memory range entry in the list is same.
		 * Just move the end address of the current memory
		 * range entry in the list to base - 1.
		 */
		else if (end == mend)  {
			mem_rngs->ranges[i].end = base - 1;
			goto out;
		}
		/*
		 * Memory range to remove is not at the edge of current
		 * memory range entry. Split the current memory entry into
		 * two half.
		 */
		else {
			mem_rngs->ranges[i].end = base - 1;
			size = mem_rngs->ranges[i].end - end;
			ret = add_mem_range(mem_ranges, end + 1, size);
		}
	}
out:
	return ret;
}
#endif /* CONFIG_CRASH_DUMP */
