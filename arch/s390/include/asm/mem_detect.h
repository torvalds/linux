/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_MEM_DETECT_H
#define _ASM_S390_MEM_DETECT_H

#include <linux/types.h>

enum mem_info_source {
	MEM_DETECT_NONE = 0,
	MEM_DETECT_SCLP_STOR_INFO,
	MEM_DETECT_DIAG260,
	MEM_DETECT_SCLP_READ_INFO,
	MEM_DETECT_BIN_SEARCH
};

struct mem_detect_block {
	u64 start;
	u64 end;
};

/*
 * Storage element id is defined as 1 byte (up to 256 storage elements).
 * In practise only storage element id 0 and 1 are used).
 * According to architecture one storage element could have as much as
 * 1020 subincrements. 255 mem_detect_blocks are embedded in mem_detect_info.
 * If more mem_detect_blocks are required, a block of memory from already
 * known mem_detect_block is taken (entries_extended points to it).
 */
#define MEM_INLINED_ENTRIES 255 /* (PAGE_SIZE - 16) / 16 */

struct mem_detect_info {
	u32 count;
	u8 info_source;
	struct mem_detect_block entries[MEM_INLINED_ENTRIES];
	struct mem_detect_block *entries_extended;
};
extern struct mem_detect_info mem_detect;

void add_mem_detect_block(u64 start, u64 end);

static inline int __get_mem_detect_block(u32 n, unsigned long *start,
					 unsigned long *end)
{
	if (n >= mem_detect.count) {
		*start = 0;
		*end = 0;
		return -1;
	}

	if (n < MEM_INLINED_ENTRIES) {
		*start = (unsigned long)mem_detect.entries[n].start;
		*end = (unsigned long)mem_detect.entries[n].end;
	} else {
		*start = (unsigned long)mem_detect.entries_extended[n - MEM_INLINED_ENTRIES].start;
		*end = (unsigned long)mem_detect.entries_extended[n - MEM_INLINED_ENTRIES].end;
	}
	return 0;
}

/**
 * for_each_mem_detect_block - early online memory range iterator
 * @i: an integer used as loop variable
 * @p_start: ptr to unsigned long for start address of the range
 * @p_end: ptr to unsigned long for end address of the range
 *
 * Walks over detected online memory ranges.
 */
#define for_each_mem_detect_block(i, p_start, p_end)			\
	for (i = 0, __get_mem_detect_block(i, p_start, p_end);		\
	     i < mem_detect.count;					\
	     i++, __get_mem_detect_block(i, p_start, p_end))

static inline void get_mem_detect_reserved(unsigned long *start,
					   unsigned long *size)
{
	*start = (unsigned long)mem_detect.entries_extended;
	if (mem_detect.count > MEM_INLINED_ENTRIES)
		*size = (mem_detect.count - MEM_INLINED_ENTRIES) * sizeof(struct mem_detect_block);
	else
		*size = 0;
}

#endif
