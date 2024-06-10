/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_MEM_DETECT_H
#define _ASM_S390_MEM_DETECT_H

#include <linux/types.h>
#include <asm/page.h>

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

enum reserved_range_type {
	RR_DECOMPRESSOR,
	RR_INITRD,
	RR_VMLINUX,
	RR_AMODE31,
	RR_IPLREPORT,
	RR_CERT_COMP_LIST,
	RR_MEM_DETECT_EXTENDED,
	RR_VMEM,
	RR_MAX
};

struct reserved_range {
	unsigned long start;
	unsigned long end;
	struct reserved_range *chain;
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
	struct reserved_range reserved[RR_MAX];
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

static inline const char *get_physmem_info_source(void)
{
	switch (physmem_info.info_source) {
	case MEM_DETECT_SCLP_STOR_INFO:
		return "sclp storage info";
	case MEM_DETECT_DIAG260:
		return "diag260";
	case MEM_DETECT_SCLP_READ_INFO:
		return "sclp read info";
	case MEM_DETECT_BIN_SEARCH:
		return "binary search";
	}
	return "none";
}

#define RR_TYPE_NAME(t) case RR_ ## t: return #t
static inline const char *get_rr_type_name(enum reserved_range_type t)
{
	switch (t) {
	RR_TYPE_NAME(DECOMPRESSOR);
	RR_TYPE_NAME(INITRD);
	RR_TYPE_NAME(VMLINUX);
	RR_TYPE_NAME(AMODE31);
	RR_TYPE_NAME(IPLREPORT);
	RR_TYPE_NAME(CERT_COMP_LIST);
	RR_TYPE_NAME(MEM_DETECT_EXTENDED);
	RR_TYPE_NAME(VMEM);
	default:
		return "UNKNOWN";
	}
}

#define for_each_physmem_reserved_type_range(t, range, p_start, p_end)				\
	for (range = &physmem_info.reserved[t], *p_start = range->start, *p_end = range->end;	\
	     range && range->end; range = range->chain ? __va(range->chain) : NULL,		\
	     *p_start = range ? range->start : 0, *p_end = range ? range->end : 0)

static inline struct reserved_range *__physmem_reserved_next(enum reserved_range_type *t,
							     struct reserved_range *range)
{
	if (!range) {
		range = &physmem_info.reserved[*t];
		if (range->end)
			return range;
	}
	if (range->chain)
		return __va(range->chain);
	while (++*t < RR_MAX) {
		range = &physmem_info.reserved[*t];
		if (range->end)
			return range;
	}
	return NULL;
}

#define for_each_physmem_reserved_range(t, range, p_start, p_end)			\
	for (t = 0, range = __physmem_reserved_next(&t, NULL),			\
	    *p_start = range ? range->start : 0, *p_end = range ? range->end : 0;	\
	     range; range = __physmem_reserved_next(&t, range),			\
	    *p_start = range ? range->start : 0, *p_end = range ? range->end : 0)

static inline unsigned long get_physmem_reserved(enum reserved_range_type type,
						 unsigned long *addr, unsigned long *size)
{
	*addr = physmem_info.reserved[type].start;
	*size = physmem_info.reserved[type].end - physmem_info.reserved[type].start;
	return *size;
}

#define AMODE31_START	(physmem_info.reserved[RR_AMODE31].start)
#define AMODE31_END	(physmem_info.reserved[RR_AMODE31].end)

#endif
