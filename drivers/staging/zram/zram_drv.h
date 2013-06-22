/*
 * Compressed RAM block device
 *
 * Copyright (C) 2008, 2009, 2010  Nitin Gupta
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 *
 * Project home: http://compcache.googlecode.com
 */

#ifndef _ZRAM_DRV_H_
#define _ZRAM_DRV_H_

#include <linux/spinlock.h>
#include <linux/mutex.h>

#include "../zsmalloc/zsmalloc.h"

/*
 * Some arbitrary value. This is just to catch
 * invalid value for num_devices module parameter.
 */
static const unsigned max_num_devices = 32;

/*-- Configurable parameters */

/*
 * Pages that compress to size greater than this are stored
 * uncompressed in memory.
 */
static const size_t max_zpage_size = PAGE_SIZE / 4 * 3;

/*
 * NOTE: max_zpage_size must be less than or equal to:
 *   ZS_MAX_ALLOC_SIZE. Otherwise, zs_malloc() would
 * always return failure.
 */

/*-- End of configurable params */

#define SECTOR_SHIFT		9
#define SECTOR_SIZE		(1 << SECTOR_SHIFT)
#define SECTORS_PER_PAGE_SHIFT	(PAGE_SHIFT - SECTOR_SHIFT)
#define SECTORS_PER_PAGE	(1 << SECTORS_PER_PAGE_SHIFT)
#define ZRAM_LOGICAL_BLOCK_SHIFT 12
#define ZRAM_LOGICAL_BLOCK_SIZE	(1 << ZRAM_LOGICAL_BLOCK_SHIFT)
#define ZRAM_SECTOR_PER_LOGICAL_BLOCK	\
	(1 << (ZRAM_LOGICAL_BLOCK_SHIFT - SECTOR_SHIFT))

/* Flags for zram pages (table[page_no].flags) */
enum zram_pageflags {
	/* Page consists entirely of zeros */
	ZRAM_ZERO,

	__NR_ZRAM_PAGEFLAGS,
};

/*-- Data structures */

/* Allocated for each disk page */
struct table {
	unsigned long handle;
	u16 size;	/* object size (excluding header) */
	u8 count;	/* object ref count (not yet used) */
	u8 flags;
} __aligned(4);

/*
 * All 64bit fields should only be manipulated by 64bit atomic accessors.
 * All modifications to 32bit counter should be protected by zram->lock.
 */
struct zram_stats {
	atomic64_t compr_size;	/* compressed size of pages stored */
	atomic64_t num_reads;	/* failed + successful */
	atomic64_t num_writes;	/* --do-- */
	atomic64_t failed_reads;	/* should NEVER! happen */
	atomic64_t failed_writes;	/* can happen when memory is too low */
	atomic64_t invalid_io;	/* non-page-aligned I/O requests */
	atomic64_t notify_free;	/* no. of swap slot free notifications */
	u32 pages_zero;		/* no. of zero filled pages */
	u32 pages_stored;	/* no. of pages currently stored */
	u32 good_compress;	/* % of pages with compression ratio<=50% */
	u32 bad_compress;	/* % of pages with compression ratio>=75% */
};

struct zram_meta {
	void *compress_workmem;
	void *compress_buffer;
	struct table *table;
	struct zs_pool *mem_pool;
};

struct zram {
	struct zram_meta *meta;
	struct rw_semaphore lock; /* protect compression buffers, table,
				   * 32bit stat counters against concurrent
				   * notifications, reads and writes */
	struct request_queue *queue;
	struct gendisk *disk;
	int init_done;
	/* Prevent concurrent execution of device init, reset and R/W request */
	struct rw_semaphore init_lock;
	/*
	 * This is the limit on amount of *uncompressed* worth of data
	 * we can store in a disk.
	 */
	u64 disksize;	/* bytes */

	struct zram_stats stats;
};
#endif
