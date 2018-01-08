/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RAID1_H
#define _RAID1_H

/*
 * each barrier unit size is 64MB fow now
 * note: it must be larger than RESYNC_DEPTH
 */
#define BARRIER_UNIT_SECTOR_BITS	17
#define BARRIER_UNIT_SECTOR_SIZE	(1<<17)
/*
 * In struct r1conf, the following members are related to I/O barrier
 * buckets,
 *	atomic_t	*nr_pending;
 *	atomic_t	*nr_waiting;
 *	atomic_t	*nr_queued;
 *	atomic_t	*barrier;
 * Each of them points to array of atomic_t variables, each array is
 * designed to have BARRIER_BUCKETS_NR elements and occupy a single
 * memory page. The data width of atomic_t variables is 4 bytes, equal
 * to 1<<(ilog2(sizeof(atomic_t))), BARRIER_BUCKETS_NR_BITS is defined
 * as (PAGE_SHIFT - ilog2(sizeof(int))) to make sure an array of
 * atomic_t variables with BARRIER_BUCKETS_NR elements just exactly
 * occupies a single memory page.
 */
#define BARRIER_BUCKETS_NR_BITS		(PAGE_SHIFT - ilog2(sizeof(atomic_t)))
#define BARRIER_BUCKETS_NR		(1<<BARRIER_BUCKETS_NR_BITS)

struct raid1_info {
	struct md_rdev	*rdev;
	sector_t	head_position;

	/* When choose the best device for a read (read_balance())
	 * we try to keep sequential reads one the same device
	 */
	sector_t	next_seq_sect;
	sector_t	seq_start;
};

/*
 * memory pools need a pointer to the mddev, so they can force an unplug
 * when memory is tight, and a count of the number of drives that the
 * pool was allocated for, so they know how much to allocate and free.
 * mddev->raid_disks cannot be used, as it can change while a pool is active
 * These two datums are stored in a kmalloced struct.
 * The 'raid_disks' here is twice the raid_disks in r1conf.
 * This allows space for each 'real' device can have a replacement in the
 * second half of the array.
 */

struct pool_info {
	struct mddev *mddev;
	int	raid_disks;
};

struct r1conf {
	struct mddev		*mddev;
	struct raid1_info	*mirrors;	/* twice 'raid_disks' to
						 * allow for replacements.
						 */
	int			raid_disks;

	spinlock_t		device_lock;

	/* list of 'struct r1bio' that need to be processed by raid1d,
	 * whether to retry a read, writeout a resync or recovery
	 * block, or anything else.
	 */
	struct list_head	retry_list;
	/* A separate list of r1bio which just need raid_end_bio_io called.
	 * This mustn't happen for writes which had any errors if the superblock
	 * needs to be written.
	 */
	struct list_head	bio_end_io_list;

	/* queue pending writes to be submitted on unplug */
	struct bio_list		pending_bio_list;
	int			pending_count;

	/* for use when syncing mirrors:
	 * We don't allow both normal IO and resync/recovery IO at
	 * the same time - resync/recovery can only happen when there
	 * is no other IO.  So when either is active, the other has to wait.
	 * See more details description in raid1.c near raise_barrier().
	 */
	wait_queue_head_t	wait_barrier;
	spinlock_t		resync_lock;
	atomic_t		nr_sync_pending;
	atomic_t		*nr_pending;
	atomic_t		*nr_waiting;
	atomic_t		*nr_queued;
	atomic_t		*barrier;
	int			array_frozen;

	/* Set to 1 if a full sync is needed, (fresh device added).
	 * Cleared when a sync completes.
	 */
	int			fullsync;

	/* When the same as mddev->recovery_disabled we don't allow
	 * recovery to be attempted as we expect a read error.
	 */
	int			recovery_disabled;

	/* poolinfo contains information about the content of the
	 * mempools - it changes when the array grows or shrinks
	 */
	struct pool_info	*poolinfo;
	mempool_t		*r1bio_pool;
	mempool_t		*r1buf_pool;

	struct bio_set		*bio_split;

	/* temporary buffer to synchronous IO when attempting to repair
	 * a read error.
	 */
	struct page		*tmppage;

	/* When taking over an array from a different personality, we store
	 * the new thread here until we fully activate the array.
	 */
	struct md_thread	*thread;

	/* Keep track of cluster resync window to send to other
	 * nodes.
	 */
	sector_t		cluster_sync_low;
	sector_t		cluster_sync_high;

};

/*
 * this is our 'private' RAID1 bio.
 *
 * it contains information about what kind of IO operations were started
 * for this RAID1 operation, and about their status:
 */

struct r1bio {
	atomic_t		remaining; /* 'have we finished' count,
					    * used from IRQ handlers
					    */
	atomic_t		behind_remaining; /* number of write-behind ios remaining
						 * in this BehindIO request
						 */
	sector_t		sector;
	int			sectors;
	unsigned long		state;
	struct mddev		*mddev;
	/*
	 * original bio going to /dev/mdx
	 */
	struct bio		*master_bio;
	/*
	 * if the IO is in READ direction, then this is where we read
	 */
	int			read_disk;

	struct list_head	retry_list;

	/*
	 * When R1BIO_BehindIO is set, we store pages for write behind
	 * in behind_master_bio.
	 */
	struct bio		*behind_master_bio;

	/*
	 * if the IO is in WRITE direction, then multiple bios are used.
	 * We choose the number when they are allocated.
	 */
	struct bio		*bios[0];
	/* DO NOT PUT ANY NEW FIELDS HERE - bios array is contiguously alloced*/
};

/* bits for r1bio.state */
enum r1bio_state {
	R1BIO_Uptodate,
	R1BIO_IsSync,
	R1BIO_Degraded,
	R1BIO_BehindIO,
/* Set ReadError on bios that experience a readerror so that
 * raid1d knows what to do with them.
 */
	R1BIO_ReadError,
/* For write-behind requests, we call bi_end_io when
 * the last non-write-behind device completes, providing
 * any write was successful.  Otherwise we call when
 * any write-behind write succeeds, otherwise we call
 * with failure when last write completes (and all failed).
 * Record that bi_end_io was called with this flag...
 */
	R1BIO_Returned,
/* If a write for this request means we can clear some
 * known-bad-block records, we set this flag
 */
	R1BIO_MadeGood,
	R1BIO_WriteError,
	R1BIO_FailFast,
};

static inline int sector_to_idx(sector_t sector)
{
	return hash_long(sector >> BARRIER_UNIT_SECTOR_BITS,
			 BARRIER_BUCKETS_NR_BITS);
}
#endif
