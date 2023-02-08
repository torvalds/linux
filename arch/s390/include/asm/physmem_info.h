/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_MEM_DETECT_H
#define _ASM_S390_MEM_DETECT_H

#include <linux/types.h>

enum physmem_info_source {
	MEM_DETECT_NONE = 0,
	MEM_DETECT_SCLP_STOR_INFO,
	MEM_DETECT_DIAG260,
	MEM_DETECT_SCLP_READ_INFO,
	MEM_DETECT_BIN_SEARCH
};

struct physmem_range {
	u64 start;
	u64 end;
};

/*
 * Storage element id is defined as 1 byte (up to 256 storage elements).
 * In practise only storage element id 0 and 1 are used).
 * According to architecture one storage element could have as much as
 * 1020 subincrements. 255 physmem_ranges are embedded in physmem_info.
 * If more physmem_ranges are required, a block of memory from already
 * known physmem_range is taken (online_extended points to it).
 */
#define MEM_INLINED_ENTRIES 255 /* (PAGE_SIZE - 16) / 16 */

struct physmem_info {
	u32 range_count;
	u8 info_source;
	unsigned long usable;
	struct physmem_range online[MEM_INLINED_ENTRIES];
	struct physmem_range *online_extended;
};

extern struct physmem_info physmem_info;

void add_physmem_online_range(u64 start, u64 end);

static inline int __get_physmem_range(u32 n, unsigned long *start,
				      unsigned long *end, bool respect_usable_limit)
{
	if (n >= physmem_info.range_count) {
		*start = 0;
		*end = 0;
		return -1;
	}

	if (n < MEM_INLINED_ENTRIES) {
		*start = (unsigned long)physmem_info.online[n].start;
		*end = (unsigned long)physmem_info.online[n].end;
	} else {
		*start = (unsigned long)physmem_info.online_extended[n - MEM_INLINED_ENTRIES].start;
		*end = (unsigned long)physmem_info.online_extended[n - MEM_INLINED_ENTRIES].end;
	}

	if (respect_usable_limit && physmem_info.usable) {
		if (*start >= physmem_info.usable)
			return -1;
		if (*end > physmem_info.usable)
			*end = physmem_info.usable;
	}
	return 0;
}

/**
 * for_each_physmem_usable_range - early online memory range iterator
 * @i: an integer used as loop variable
 * @p_start: ptr to unsigned long for start address of the range
 * @p_end: ptr to unsigned long for end address of the range
 *
 * Walks over detected online memory ranges below usable limit.
 */
#define for_each_physmem_usable_range(i, p_start, p_end)		\
	for (i = 0; !__get_physmem_range(i, p_start, p_end, true); i++)

/* Walks over all detected online memory ranges disregarding usable limit. */
#define for_each_physmem_online_range(i, p_start, p_end)		\
	for (i = 0; !__get_physmem_range(i, p_start, p_end, false); i++)

static inline unsigned long get_physmem_usable_total(void)
{
	unsigned long start, end, total = 0;
	int i;

	for_each_physmem_usable_range(i, &start, &end)
		total += end - start;

	return total;
}

static inline void get_physmem_reserved(unsigned long *start, unsigned long *size)
{
	*start = (unsigned long)physmem_info.online_extended;
	if (physmem_info.range_count > MEM_INLINED_ENTRIES)
		*size = (physmem_info.range_count - MEM_INLINED_ENTRIES) *
			sizeof(struct physmem_range);
	else
		*size = 0;
}

static inline unsigned long get_physmem_usable_end(void)
{
	unsigned long start;
	unsigned long end;

	if (physmem_info.usable)
		return physmem_info.usable;
	if (physmem_info.range_count) {
		__get_physmem_range(physmem_info.range_count - 1, &start, &end, false);
		return end;
	}
	return 0;
}

#endif
