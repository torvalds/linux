/*
 * Compressed RAM block device
 *
 * Copyright (C) 2008, 2009, 2010  Nitin Gupta
 *               2012, 2013 Minchan Kim
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 *
 */

#ifndef _ZRAM_DRV_H_
#define _ZRAM_DRV_H_

#include <linux/rwsem.h>
#include <linux/zsmalloc.h>
#include <linux/crypto.h>

#include "zcomp.h"

#define SECTOR_SHIFT		9
#define SECTORS_PER_PAGE_SHIFT	(PAGE_SHIFT - SECTOR_SHIFT)
#define SECTORS_PER_PAGE	(1 << SECTORS_PER_PAGE_SHIFT)
#define ZRAM_LOGICAL_BLOCK_SHIFT 12
#define ZRAM_LOGICAL_BLOCK_SIZE	(1 << ZRAM_LOGICAL_BLOCK_SHIFT)
#define ZRAM_SECTOR_PER_LOGICAL_BLOCK	\
	(1 << (ZRAM_LOGICAL_BLOCK_SHIFT - SECTOR_SHIFT))


/*
 * The lower ZRAM_FLAG_SHIFT bits of table.value is for
 * object size (excluding header), the higher bits is for
 * zram_pageflags.
 *
 * zram is mainly used for memory efficiency so we want to keep memory
 * footprint small so we can squeeze size and flags into a field.
 * The lower ZRAM_FLAG_SHIFT bits is for object size (excluding header),
 * the higher bits is for zram_pageflags.
 */
#define ZRAM_FLAG_SHIFT 24

/* Flags for zram pages (table[page_no].value) */
enum zram_pageflags {
	/* Page consists the same element */
	ZRAM_SAME = ZRAM_FLAG_SHIFT,
	ZRAM_ACCESS,	/* page is now accessed */
	ZRAM_WB,	/* page is stored on backing_device */

	__NR_ZRAM_PAGEFLAGS,
};

/*-- Data structures */

/* Allocated for each disk page */
struct zram_table_entry {
	union {
		unsigned long handle;
		unsigned long element;
	};
	unsigned long value;
};

struct zram_stats {
	atomic64_t compr_data_size;	/* compressed size of pages stored */
	atomic64_t num_reads;	/* failed + successful */
	atomic64_t num_writes;	/* --do-- */
	atomic64_t failed_reads;	/* can happen when memory is too low */
	atomic64_t failed_writes;	/* can happen when memory is too low */
	atomic64_t invalid_io;	/* non-page-aligned I/O requests */
	atomic64_t notify_free;	/* no. of swap slot free notifications */
	atomic64_t same_pages;		/* no. of same element filled pages */
	atomic64_t pages_stored;	/* no. of pages currently stored */
	atomic_long_t max_used_pages;	/* no. of maximum pages stored */
	atomic64_t writestall;		/* no. of write slow paths */
};

struct zram {
	struct zram_table_entry *table;
	struct zs_pool *mem_pool;
	struct zcomp *comp;
	struct gendisk *disk;
	/* Prevent concurrent execution of device init */
	struct rw_semaphore init_lock;
	/*
	 * the number of pages zram can consume for storing compressed data
	 */
	unsigned long limit_pages;

	struct zram_stats stats;
	/*
	 * This is the limit on amount of *uncompressed* worth of data
	 * we can store in a disk.
	 */
	u64 disksize;	/* bytes */
	char compressor[CRYPTO_MAX_ALG_NAME];
	/*
	 * zram is claimed so open request will be failed
	 */
	bool claim; /* Protected by bdev->bd_mutex */
#ifdef CONFIG_ZRAM_WRITEBACK
	struct file *backing_dev;
	struct block_device *bdev;
	unsigned int old_block_size;
	unsigned long *bitmap;
	unsigned long nr_pages;
	spinlock_t bitmap_lock;
#endif
};
#endif
