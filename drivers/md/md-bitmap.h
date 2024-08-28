/* SPDX-License-Identifier: GPL-2.0 */
/*
 * bitmap.h: Copyright (C) Peter T. Breuer (ptb@ot.uc3m.es) 2003
 *
 * additions: Copyright (C) 2003-2004, Paul Clements, SteelEye Technology, Inc.
 */
#ifndef BITMAP_H
#define BITMAP_H 1

#define BITMAP_MAGIC 0x6d746962

typedef __u16 bitmap_counter_t;
#define COUNTER_BITS 16
#define COUNTER_BIT_SHIFT 4
#define COUNTER_BYTE_SHIFT (COUNTER_BIT_SHIFT - 3)

#define NEEDED_MASK ((bitmap_counter_t) (1 << (COUNTER_BITS - 1)))
#define RESYNC_MASK ((bitmap_counter_t) (1 << (COUNTER_BITS - 2)))
#define COUNTER_MAX ((bitmap_counter_t) RESYNC_MASK - 1)

/* use these for bitmap->flags and bitmap->sb->state bit-fields */
enum bitmap_state {
	BITMAP_STALE	   = 1,  /* the bitmap file is out of date or had -EIO */
	BITMAP_WRITE_ERROR = 2, /* A write error has occurred */
	BITMAP_HOSTENDIAN  =15,
};

/* the superblock at the front of the bitmap file -- little endian */
typedef struct bitmap_super_s {
	__le32 magic;        /*  0  BITMAP_MAGIC */
	__le32 version;      /*  4  the bitmap major for now, could change... */
	__u8  uuid[16];      /*  8  128 bit uuid - must match md device uuid */
	__le64 events;       /* 24  event counter for the bitmap (1)*/
	__le64 events_cleared;/*32  event counter when last bit cleared (2) */
	__le64 sync_size;    /* 40  the size of the md device's sync range(3) */
	__le32 state;        /* 48  bitmap state information */
	__le32 chunksize;    /* 52  the bitmap chunk size in bytes */
	__le32 daemon_sleep; /* 56  seconds between disk flushes */
	__le32 write_behind; /* 60  number of outstanding write-behind writes */
	__le32 sectors_reserved; /* 64 number of 512-byte sectors that are
				  * reserved for the bitmap. */
	__le32 nodes;        /* 68 the maximum number of nodes in cluster. */
	__u8 cluster_name[64]; /* 72 cluster name to which this md belongs */
	__u8  pad[256 - 136]; /* set to zero */
} bitmap_super_t;

/* notes:
 * (1) This event counter is updated before the eventcounter in the md superblock
 *    When a bitmap is loaded, it is only accepted if this event counter is equal
 *    to, or one greater than, the event counter in the superblock.
 * (2) This event counter is updated when the other one is *if*and*only*if* the
 *    array is not degraded.  As bits are not cleared when the array is degraded,
 *    this represents the last time that any bits were cleared.
 *    If a device is being added that has an event count with this value or
 *    higher, it is accepted as conforming to the bitmap.
 * (3)This is the number of sectors represented by the bitmap, and is the range that
 *    resync happens across.  For raid1 and raid5/6 it is the size of individual
 *    devices.  For raid10 it is the size of the array.
 */

struct md_bitmap_stats {
	u64		events_cleared;
	int		behind_writes;
	bool		behind_wait;

	unsigned long	missing_pages;
	unsigned long	file_pages;
	unsigned long	sync_size;
	unsigned long	pages;
	struct file	*file;
};

struct bitmap_operations {
	bool (*enabled)(struct mddev *mddev);
	int (*create)(struct mddev *mddev, int slot);
	int (*resize)(struct mddev *mddev, sector_t blocks, int chunksize,
		      bool init);

	int (*load)(struct mddev *mddev);
	void (*destroy)(struct mddev *mddev);
	void (*flush)(struct mddev *mddev);
	void (*write_all)(struct mddev *mddev);
	void (*dirty_bits)(struct mddev *mddev, unsigned long s,
			   unsigned long e);
	void (*unplug)(struct mddev *mddev, bool sync);
	void (*daemon_work)(struct mddev *mddev);
	void (*wait_behind_writes)(struct mddev *mddev);

	int (*startwrite)(struct mddev *mddev, sector_t offset,
			  unsigned long sectors, bool behind);
	void (*endwrite)(struct mddev *mddev, sector_t offset,
			 unsigned long sectors, bool success, bool behind);
	bool (*start_sync)(struct mddev *mddev, sector_t offset,
			   sector_t *blocks, bool degraded);
	void (*end_sync)(struct mddev *mddev, sector_t offset, sector_t *blocks);
	void (*cond_end_sync)(struct mddev *mddev, sector_t sector, bool force);
	void (*close_sync)(struct mddev *mddev);

	void (*update_sb)(void *data);
	int (*get_stats)(void *data, struct md_bitmap_stats *stats);

	void (*sync_with_cluster)(struct mddev *mddev,
				  sector_t old_lo, sector_t old_hi,
				  sector_t new_lo, sector_t new_hi);
	void *(*get_from_slot)(struct mddev *mddev, int slot);
	int (*copy_from_slot)(struct mddev *mddev, int slot, sector_t *lo,
			      sector_t *hi, bool clear_bits);
	void (*set_pages)(void *data, unsigned long pages);
	void (*free)(void *data);
};

/* the bitmap API */
void mddev_set_bitmap_ops(struct mddev *mddev);

#endif
