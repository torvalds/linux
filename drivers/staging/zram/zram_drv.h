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

#include "xvmalloc.h"

/*
 * Some arbitrary value. This is just to catch
 * invalid value for num_devices module parameter.
 */
static const unsigned max_num_devices = 32;

/*
 * Stored at beginning of each compressed object.
 *
 * It stores back-reference to table entry which points to this
 * object. This is required to support memory defragmentation.
 */
struct zobj_header {
#if 0
	u32 table_idx;
#endif
};

/*-- Configurable parameters */

/* Default zram disk size: 25% of total RAM */
static const unsigned default_disksize_perc_ram = 25;

/*
 * Pages that compress to size greater than this are stored
 * uncompressed in memory.
 */
static const unsigned max_zpage_size = PAGE_SIZE / 4 * 3;

/*
 * NOTE: max_zpage_size must be less than or equal to:
 *   XV_MAX_ALLOC_SIZE - sizeof(struct zobj_header)
 * otherwise, xv_malloc() would always return failure.
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
	/* Page is stored uncompressed */
	ZRAM_UNCOMPRESSED,

	/* Page consists entirely of zeros */
	ZRAM_ZERO,

	__NR_ZRAM_PAGEFLAGS,
};

/*-- Data structures */

/* Allocated for each disk page */
struct table {
	struct page *page;
	u16 offset;
	u8 count;	/* object ref count (not yet used) */
	u8 flags;
} __attribute__((aligned(4)));

struct zram_stats {
	u64 compr_size;		/* compressed size of pages stored */
	u64 num_reads;		/* failed + successful */
	u64 num_writes;		/* --do-- */
	u64 failed_reads;	/* should NEVER! happen */
	u64 failed_writes;	/* can happen when memory is too low */
	u64 invalid_io;		/* non-page-aligned I/O requests */
	u64 notify_free;	/* no. of swap slot free notifications */
	u32 pages_zero;		/* no. of zero filled pages */
	u32 pages_stored;	/* no. of pages currently stored */
	u32 good_compress;	/* % of pages with compression ratio<=50% */
	u32 pages_expand;	/* % of incompressible pages */
};

struct zram {
	struct xv_pool *mem_pool;
	void *compress_workmem;
	void *compress_buffer;
	struct table *table;
	spinlock_t stat64_lock;	/* protect 64-bit stats */
	struct rw_semaphore lock; /* protect compression buffers and table
				   * against concurrent read and writes */
	struct request_queue *queue;
	struct gendisk *disk;
	int init_done;
	/* Prevent concurrent execution of device init and reset */
	struct mutex init_lock;
	/*
	 * This is the limit on amount of *uncompressed* worth of data
	 * we can store in a disk.
	 */
	u64 disksize;	/* bytes */

	struct zram_stats stats;
};

extern struct zram *devices;
extern unsigned int num_devices;
#ifdef CONFIG_SYSFS
extern struct attribute_group zram_disk_attr_group;
#endif

extern int zram_init_device(struct zram *zram);
extern void zram_reset_device(struct zram *zram);

#endif
