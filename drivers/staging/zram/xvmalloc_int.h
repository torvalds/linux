/*
 * xvmalloc memory allocator
 *
 * Copyright (C) 2008, 2009, 2010  Nitin Gupta
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 */

#ifndef _XV_MALLOC_INT_H_
#define _XV_MALLOC_INT_H_

#include <linux/kernel.h>
#include <linux/types.h>

/* User configurable params */

/* Must be power of two */
#ifdef CONFIG_64BIT
#define XV_ALIGN_SHIFT 3
#else
#define XV_ALIGN_SHIFT	2
#endif
#define XV_ALIGN	(1 << XV_ALIGN_SHIFT)
#define XV_ALIGN_MASK	(XV_ALIGN - 1)

/* This must be greater than sizeof(link_free) */
#define XV_MIN_ALLOC_SIZE	32
#define XV_MAX_ALLOC_SIZE	(PAGE_SIZE - XV_ALIGN)

/*
 * Free lists are separated by FL_DELTA bytes
 * This value is 3 for 4k pages and 4 for 64k pages, for any
 * other page size, a conservative (PAGE_SHIFT - 9) is used.
 */
#if PAGE_SHIFT == 16
#define FL_DELTA_SHIFT 4
#else
#define FL_DELTA_SHIFT (PAGE_SHIFT - 9)
#endif
#define FL_DELTA	(1 << FL_DELTA_SHIFT)
#define FL_DELTA_MASK	(FL_DELTA - 1)
#define NUM_FREE_LISTS	((XV_MAX_ALLOC_SIZE - XV_MIN_ALLOC_SIZE) \
				/ FL_DELTA + 1)

#define MAX_FLI		DIV_ROUND_UP(NUM_FREE_LISTS, BITS_PER_LONG)

/* End of user params */

enum blockflags {
	BLOCK_FREE,
	PREV_FREE,
	__NR_BLOCKFLAGS,
};

#define FLAGS_MASK	XV_ALIGN_MASK
#define PREV_MASK	(~FLAGS_MASK)

struct freelist_entry {
	struct page *page;
	u16 offset;
	u16 pad;
};

struct link_free {
	struct page *prev_page;
	struct page *next_page;
	u16 prev_offset;
	u16 next_offset;
};

struct block_header {
	union {
		/* This common header must be XV_ALIGN bytes */
		u8 common[XV_ALIGN];
		struct {
			u16 size;
			u16 prev;
		};
	};
	struct link_free link;
};

struct xv_pool {
	ulong flbitmap;
	ulong slbitmap[MAX_FLI];
	u64 total_pages;	/* stats */
	struct freelist_entry freelist[NUM_FREE_LISTS];
	spinlock_t lock;
};

#endif
