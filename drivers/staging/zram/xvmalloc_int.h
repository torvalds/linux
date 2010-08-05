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
#define XV_ALIGN_SHIFT	2
#define XV_ALIGN	(1 << XV_ALIGN_SHIFT)
#define XV_ALIGN_MASK	(XV_ALIGN - 1)

/* This must be greater than sizeof(link_free) */
#define XV_MIN_ALLOC_SIZE	32
#define XV_MAX_ALLOC_SIZE	(PAGE_SIZE - XV_ALIGN)

/* Free lists are separated by FL_DELTA bytes */
#define FL_DELTA_SHIFT	3
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
	spinlock_t lock;

	struct freelist_entry freelist[NUM_FREE_LISTS];

	/* stats */
	u64 total_pages;
};

#endif
