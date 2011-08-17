#ifndef _RAID10_H
#define _RAID10_H

typedef struct mirror_info mirror_info_t;

struct mirror_info {
	mdk_rdev_t	*rdev;
	sector_t	head_position;
	int		recovery_disabled;	/* matches
						 * mddev->recovery_disabled
						 * when we shouldn't try
						 * recovering this device.
						 */
};

typedef struct r10bio_s r10bio_t;

struct r10_private_data_s {
	mddev_t			*mddev;
	mirror_info_t		*mirrors;
	int			raid_disks;
	spinlock_t		device_lock;

	/* geometry */
	int			near_copies;  /* number of copies laid out raid0 style */
	int 			far_copies;   /* number of copies laid out
					       * at large strides across drives
					       */
	int			far_offset;   /* far_copies are offset by 1 stripe
					       * instead of many
					       */
	int			copies;	      /* near_copies * far_copies.
					       * must be <= raid_disks
					       */
	sector_t		stride;	      /* distance between far copies.
					       * This is size / far_copies unless
					       * far_offset, in which case it is
					       * 1 stripe.
					       */

	sector_t		dev_sectors;  /* temp copy of mddev->dev_sectors */

	int chunk_shift; /* shift from chunks to sectors */
	sector_t chunk_mask;

	struct list_head	retry_list;
	/* queue pending writes and submit them on unplug */
	struct bio_list		pending_bio_list;


	spinlock_t		resync_lock;
	int nr_pending;
	int nr_waiting;
	int nr_queued;
	int barrier;
	sector_t		next_resync;
	int			fullsync;  /* set to 1 if a full sync is needed,
					    * (fresh device added).
					    * Cleared when a sync completes.
					    */

	wait_queue_head_t	wait_barrier;

	mempool_t *r10bio_pool;
	mempool_t *r10buf_pool;
	struct page		*tmppage;

	/* When taking over an array from a different personality, we store
	 * the new thread here until we fully activate the array.
	 */
	struct mdk_thread_s	*thread;
};

typedef struct r10_private_data_s conf_t;

/*
 * this is our 'private' RAID10 bio.
 *
 * it contains information about what kind of IO operations were started
 * for this RAID10 operation, and about their status:
 */

struct r10bio_s {
	atomic_t		remaining; /* 'have we finished' count,
					    * used from IRQ handlers
					    */
	sector_t		sector;	/* virtual sector number */
	int			sectors;
	unsigned long		state;
	mddev_t			*mddev;
	/*
	 * original bio going to /dev/mdx
	 */
	struct bio		*master_bio;
	/*
	 * if the IO is in READ direction, then this is where we read
	 */
	int			read_slot;

	struct list_head	retry_list;
	/*
	 * if the IO is in WRITE direction, then multiple bios are used,
	 * one for each copy.
	 * When resyncing we also use one for each copy.
	 * When reconstructing, we use 2 bios, one for read, one for write.
	 * We choose the number when they are allocated.
	 */
	struct {
		struct bio		*bio;
		sector_t addr;
		int devnum;
	} devs[0];
};

/* when we get a read error on a read-only array, we redirect to another
 * device without failing the first device, or trying to over-write to
 * correct the read error.  To keep track of bad blocks on a per-bio
 * level, we store IO_BLOCKED in the appropriate 'bios' pointer
 */
#define IO_BLOCKED ((struct bio*)1)
/* When we successfully write to a known bad-block, we need to remove the
 * bad-block marking which must be done from process context.  So we record
 * the success by setting devs[n].bio to IO_MADE_GOOD
 */
#define IO_MADE_GOOD ((struct bio *)2)

#define BIO_SPECIAL(bio) ((unsigned long)bio <= 2)

/* bits for r10bio.state */
#define	R10BIO_Uptodate	0
#define	R10BIO_IsSync	1
#define	R10BIO_IsRecover 2
#define	R10BIO_Degraded 3
/* Set ReadError on bios that experience a read error
 * so that raid10d knows what to do with them.
 */
#define	R10BIO_ReadError 4
/* If a write for this request means we can clear some
 * known-bad-block records, we set this flag.
 */
#define	R10BIO_MadeGood 5
#define	R10BIO_WriteError 6
#endif
