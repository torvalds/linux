/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RAID5_H
#define _RAID5_H

#include <linux/raid/xor.h>
#include <linux/dmaengine.h>
#include <linux/local_lock.h>

/*
 *
 * Each stripe contains one buffer per device.  Each buffer can be in
 * one of a number of states stored in "flags".  Changes between
 * these states happen *almost* exclusively under the protection of the
 * STRIPE_ACTIVE flag.  Some very specific changes can happen in bi_end_io, and
 * these are not protected by STRIPE_ACTIVE.
 *
 * The flag bits that are used to represent these states are:
 *   R5_UPTODATE and R5_LOCKED
 *
 * State Empty == !UPTODATE, !LOCK
 *        We have no data, and there is no active request
 * State Want == !UPTODATE, LOCK
 *        A read request is being submitted for this block
 * State Dirty == UPTODATE, LOCK
 *        Some new data is in this buffer, and it is being written out
 * State Clean == UPTODATE, !LOCK
 *        We have valid data which is the same as on disc
 *
 * The possible state transitions are:
 *
 *  Empty -> Want   - on read or write to get old data for  parity calc
 *  Empty -> Dirty  - on compute_parity to satisfy write/sync request.
 *  Empty -> Clean  - on compute_block when computing a block for failed drive
 *  Want  -> Empty  - on failed read
 *  Want  -> Clean  - on successful completion of read request
 *  Dirty -> Clean  - on successful completion of write request
 *  Dirty -> Clean  - on failed write
 *  Clean -> Dirty  - on compute_parity to satisfy write/sync (RECONSTRUCT or RMW)
 *
 * The Want->Empty, Want->Clean, Dirty->Clean, transitions
 * all happen in b_end_io at interrupt time.
 * Each sets the Uptodate bit before releasing the Lock bit.
 * This leaves one multi-stage transition:
 *    Want->Dirty->Clean
 * This is safe because thinking that a Clean buffer is actually dirty
 * will at worst delay some action, and the stripe will be scheduled
 * for attention after the transition is complete.
 *
 * There is one possibility that is not covered by these states.  That
 * is if one drive has failed and there is a spare being rebuilt.  We
 * can't distinguish between a clean block that has been generated
 * from parity calculations, and a clean block that has been
 * successfully written to the spare ( or to parity when resyncing).
 * To distinguish these states we have a stripe bit STRIPE_INSYNC that
 * is set whenever a write is scheduled to the spare, or to the parity
 * disc if there is no spare.  A sync request clears this bit, and
 * when we find it set with no buffers locked, we know the sync is
 * complete.
 *
 * Buffers for the md device that arrive via make_request are attached
 * to the appropriate stripe in one of two lists linked on b_reqnext.
 * One list (bh_read) for read requests, one (bh_write) for write.
 * There should never be more than one buffer on the two lists
 * together, but we are not guaranteed of that so we allow for more.
 *
 * If a buffer is on the read list when the associated cache buffer is
 * Uptodate, the data is copied into the read buffer and it's b_end_io
 * routine is called.  This may happen in the end_request routine only
 * if the buffer has just successfully been read.  end_request should
 * remove the buffers from the list and then set the Uptodate bit on
 * the buffer.  Other threads may do this only if they first check
 * that the Uptodate bit is set.  Once they have checked that they may
 * take buffers off the read queue.
 *
 * When a buffer on the write list is committed for write it is copied
 * into the cache buffer, which is then marked dirty, and moved onto a
 * third list, the written list (bh_written).  Once both the parity
 * block and the cached buffer are successfully written, any buffer on
 * a written list can be returned with b_end_io.
 *
 * The write list and read list both act as fifos.  The read list,
 * write list and written list are protected by the device_lock.
 * The device_lock is only for list manipulations and will only be
 * held for a very short time.  It can be claimed from interrupts.
 *
 *
 * Stripes in the stripe cache can be on one of two lists (or on
 * neither).  The "inactive_list" contains stripes which are not
 * currently being used for any request.  They can freely be reused
 * for another stripe.  The "handle_list" contains stripes that need
 * to be handled in some way.  Both of these are fifo queues.  Each
 * stripe is also (potentially) linked to a hash bucket in the hash
 * table so that it can be found by sector number.  Stripes that are
 * not hashed must be on the inactive_list, and will normally be at
 * the front.  All stripes start life this way.
 *
 * The inactive_list, handle_list and hash bucket lists are all protected by the
 * device_lock.
 *  - stripes have a reference counter. If count==0, they are on a list.
 *  - If a stripe might need handling, STRIPE_HANDLE is set.
 *  - When refcount reaches zero, then if STRIPE_HANDLE it is put on
 *    handle_list else inactive_list
 *
 * This, combined with the fact that STRIPE_HANDLE is only ever
 * cleared while a stripe has a non-zero count means that if the
 * refcount is 0 and STRIPE_HANDLE is set, then it is on the
 * handle_list and if recount is 0 and STRIPE_HANDLE is not set, then
 * the stripe is on inactive_list.
 *
 * The possible transitions are:
 *  activate an unhashed/inactive stripe (get_active_stripe())
 *     lockdev check-hash unlink-stripe cnt++ clean-stripe hash-stripe unlockdev
 *  activate a hashed, possibly active stripe (get_active_stripe())
 *     lockdev check-hash if(!cnt++)unlink-stripe unlockdev
 *  attach a request to an active stripe (add_stripe_bh())
 *     lockdev attach-buffer unlockdev
 *  handle a stripe (handle_stripe())
 *     setSTRIPE_ACTIVE,  clrSTRIPE_HANDLE ...
 *		(lockdev check-buffers unlockdev) ..
 *		change-state ..
 *		record io/ops needed clearSTRIPE_ACTIVE schedule io/ops
 *  release an active stripe (release_stripe())
 *     lockdev if (!--cnt) { if  STRIPE_HANDLE, add to handle_list else add to inactive-list } unlockdev
 *
 * The refcount counts each thread that have activated the stripe,
 * plus raid5d if it is handling it, plus one for each active request
 * on a cached buffer, and plus one if the stripe is undergoing stripe
 * operations.
 *
 * The stripe operations are:
 * -copying data between the stripe cache and user application buffers
 * -computing blocks to save a disk access, or to recover a missing block
 * -updating the parity on a write operation (reconstruct write and
 *  read-modify-write)
 * -checking parity correctness
 * -running i/o to disk
 * These operations are carried out by raid5_run_ops which uses the async_tx
 * api to (optionally) offload operations to dedicated hardware engines.
 * When requesting an operation handle_stripe sets the pending bit for the
 * operation and increments the count.  raid5_run_ops is then run whenever
 * the count is non-zero.
 * There are some critical dependencies between the operations that prevent some
 * from being requested while another is in flight.
 * 1/ Parity check operations destroy the in cache version of the parity block,
 *    so we prevent parity dependent operations like writes and compute_blocks
 *    from starting while a check is in progress.  Some dma engines can perform
 *    the check without damaging the parity block, in these cases the parity
 *    block is re-marked up to date (assuming the check was successful) and is
 *    not re-read from disk.
 * 2/ When a write operation is requested we immediately lock the affected
 *    blocks, and mark them as not up to date.  This causes new read requests
 *    to be held off, as well as parity checks and compute block operations.
 * 3/ Once a compute block operation has been requested handle_stripe treats
 *    that block as if it is up to date.  raid5_run_ops guaruntees that any
 *    operation that is dependent on the compute block result is initiated after
 *    the compute block completes.
 */

/*
 * Operations state - intermediate states that are visible outside of
 *   STRIPE_ACTIVE.
 * In general _idle indicates nothing is running, _run indicates a data
 * processing operation is active, and _result means the data processing result
 * is stable and can be acted upon.  For simple operations like biofill and
 * compute that only have an _idle and _run state they are indicated with
 * sh->state flags (STRIPE_BIOFILL_RUN and STRIPE_COMPUTE_RUN)
 */
/**
 * enum check_states - handles syncing / repairing a stripe
 * @check_state_idle - check operations are quiesced
 * @check_state_run - check operation is running
 * @check_state_result - set outside lock when check result is valid
 * @check_state_compute_run - check failed and we are repairing
 * @check_state_compute_result - set outside lock when compute result is valid
 */
enum check_states {
	check_state_idle = 0,
	check_state_run, /* xor parity check */
	check_state_run_q, /* q-parity check */
	check_state_run_pq, /* pq dual parity check */
	check_state_check_result,
	check_state_compute_run, /* parity repair */
	check_state_compute_result,
};

/**
 * enum reconstruct_states - handles writing or expanding a stripe
 */
enum reconstruct_states {
	reconstruct_state_idle = 0,
	reconstruct_state_prexor_drain_run,	/* prexor-write */
	reconstruct_state_drain_run,		/* write */
	reconstruct_state_run,			/* expand */
	reconstruct_state_prexor_drain_result,
	reconstruct_state_drain_result,
	reconstruct_state_result,
};

#define DEFAULT_STRIPE_SIZE	4096
struct stripe_head {
	struct hlist_node	hash;
	struct list_head	lru;	      /* inactive_list or handle_list */
	struct llist_node	release_list;
	struct r5conf		*raid_conf;
	short			generation;	/* increments with every
						 * reshape */
	sector_t		sector;		/* sector of this row */
	short			pd_idx;		/* parity disk index */
	short			qd_idx;		/* 'Q' disk index for raid6 */
	short			ddf_layout;/* use DDF ordering to calculate Q */
	short			hash_lock_index;
	unsigned long		state;		/* state flags */
	atomic_t		count;	      /* nr of active thread/requests */
	int			bm_seq;	/* sequence number for bitmap flushes */
	int			disks;		/* disks in stripe */
	int			overwrite_disks; /* total overwrite disks in stripe,
						  * this is only checked when stripe
						  * has STRIPE_BATCH_READY
						  */
	enum check_states	check_state;
	enum reconstruct_states reconstruct_state;
	spinlock_t		stripe_lock;
	int			cpu;
	struct r5worker_group	*group;

	struct stripe_head	*batch_head; /* protected by stripe lock */
	spinlock_t		batch_lock; /* only header's lock is useful */
	struct list_head	batch_list; /* protected by head's batch lock*/

	union {
		struct r5l_io_unit	*log_io;
		struct ppl_io_unit	*ppl_io;
	};

	struct list_head	log_list;
	sector_t		log_start; /* first meta block on the journal */
	struct list_head	r5c; /* for r5c_cache->stripe_in_journal */

	struct page		*ppl_page; /* partial parity of this stripe */
	/**
	 * struct stripe_operations
	 * @target - STRIPE_OP_COMPUTE_BLK target
	 * @target2 - 2nd compute target in the raid6 case
	 * @zero_sum_result - P and Q verification flags
	 * @request - async service request flags for raid_run_ops
	 */
	struct stripe_operations {
		int 		     target, target2;
		enum sum_check_flags zero_sum_result;
	} ops;

#if PAGE_SIZE != DEFAULT_STRIPE_SIZE
	/* These pages will be used by bios in dev[i] */
	struct page	**pages;
	int	nr_pages;	/* page array size */
	int	stripes_per_page;
#endif
	struct r5dev {
		/* rreq and rvec are used for the replacement device when
		 * writing data to both devices.
		 */
		struct bio	req, rreq;
		struct bio_vec	vec, rvec;
		struct page	*page, *orig_page;
		unsigned int    offset;     /* offset of the page */
		struct bio	*toread, *read, *towrite, *written;
		sector_t	sector;			/* sector of this page */
		unsigned long	flags;
		u32		log_checksum;
		unsigned short	write_hint;
	} dev[1]; /* allocated with extra space depending of RAID geometry */
};

/* stripe_head_state - collects and tracks the dynamic state of a stripe_head
 *     for handle_stripe.
 */
struct stripe_head_state {
	/* 'syncing' means that we need to read all devices, either
	 * to check/correct parity, or to reconstruct a missing device.
	 * 'replacing' means we are replacing one or more drives and
	 * the source is valid at this point so we don't need to
	 * read all devices, just the replacement targets.
	 */
	int syncing, expanding, expanded, replacing;
	int locked, uptodate, to_read, to_write, failed, written;
	int to_fill, compute, req_compute, non_overwrite;
	int injournal, just_cached;
	int failed_num[2];
	int p_failed, q_failed;
	int dec_preread_active;
	unsigned long ops_request;

	struct md_rdev *blocked_rdev;
	int handle_bad_blocks;
	int log_failed;
	int waiting_extra_page;
};

/* Flags for struct r5dev.flags */
enum r5dev_flags {
	R5_UPTODATE,	/* page contains current data */
	R5_LOCKED,	/* IO has been submitted on "req" */
	R5_DOUBLE_LOCKED,/* Cannot clear R5_LOCKED until 2 writes complete */
	R5_OVERWRITE,	/* towrite covers whole page */
/* and some that are internal to handle_stripe */
	R5_Insync,	/* rdev && rdev->in_sync at start */
	R5_Wantread,	/* want to schedule a read */
	R5_Wantwrite,
	R5_Overlap,	/* There is a pending overlapping request
			 * on this block */
	R5_ReadNoMerge, /* prevent bio from merging in block-layer */
	R5_ReadError,	/* seen a read error here recently */
	R5_ReWrite,	/* have tried to over-write the readerror */

	R5_Expanded,	/* This block now has post-expand data */
	R5_Wantcompute,	/* compute_block in progress treat as
			 * uptodate
			 */
	R5_Wantfill,	/* dev->toread contains a bio that needs
			 * filling
			 */
	R5_Wantdrain,	/* dev->towrite needs to be drained */
	R5_WantFUA,	/* Write should be FUA */
	R5_SyncIO,	/* The IO is sync */
	R5_WriteError,	/* got a write error - need to record it */
	R5_MadeGood,	/* A bad block has been fixed by writing to it */
	R5_ReadRepl,	/* Will/did read from replacement rather than orig */
	R5_MadeGoodRepl,/* A bad block on the replacement device has been
			 * fixed by writing to it */
	R5_NeedReplace,	/* This device has a replacement which is not
			 * up-to-date at this stripe. */
	R5_WantReplace, /* We need to update the replacement, we have read
			 * data in, and now is a good time to write it out.
			 */
	R5_Discard,	/* Discard the stripe */
	R5_SkipCopy,	/* Don't copy data from bio to stripe cache */
	R5_InJournal,	/* data being written is in the journal device.
			 * if R5_InJournal is set for parity pd_idx, all the
			 * data and parity being written are in the journal
			 * device
			 */
	R5_OrigPageUPTDODATE,	/* with write back cache, we read old data into
				 * dev->orig_page for prexor. When this flag is
				 * set, orig_page contains latest data in the
				 * raid disk.
				 */
};

/*
 * Stripe state
 */
enum {
	STRIPE_ACTIVE,
	STRIPE_HANDLE,
	STRIPE_SYNC_REQUESTED,
	STRIPE_SYNCING,
	STRIPE_INSYNC,
	STRIPE_REPLACED,
	STRIPE_PREREAD_ACTIVE,
	STRIPE_DELAYED,
	STRIPE_DEGRADED,
	STRIPE_BIT_DELAY,
	STRIPE_EXPANDING,
	STRIPE_EXPAND_SOURCE,
	STRIPE_EXPAND_READY,
	STRIPE_IO_STARTED,	/* do not count towards 'bypass_count' */
	STRIPE_FULL_WRITE,	/* all blocks are set to be overwritten */
	STRIPE_BIOFILL_RUN,
	STRIPE_COMPUTE_RUN,
	STRIPE_ON_UNPLUG_LIST,
	STRIPE_DISCARD,
	STRIPE_ON_RELEASE_LIST,
	STRIPE_BATCH_READY,
	STRIPE_BATCH_ERR,
	STRIPE_BITMAP_PENDING,	/* Being added to bitmap, don't add
				 * to batch yet.
				 */
	STRIPE_LOG_TRAPPED,	/* trapped into log (see raid5-cache.c)
				 * this bit is used in two scenarios:
				 *
				 * 1. write-out phase
				 *  set in first entry of r5l_write_stripe
				 *  clear in second entry of r5l_write_stripe
				 *  used to bypass logic in handle_stripe
				 *
				 * 2. caching phase
				 *  set in r5c_try_caching_write()
				 *  clear when journal write is done
				 *  used to initiate r5c_cache_data()
				 *  also used to bypass logic in handle_stripe
				 */
	STRIPE_R5C_CACHING,	/* the stripe is in caching phase
				 * see more detail in the raid5-cache.c
				 */
	STRIPE_R5C_PARTIAL_STRIPE,	/* in r5c cache (to-be/being handled or
					 * in conf->r5c_partial_stripe_list)
					 */
	STRIPE_R5C_FULL_STRIPE,	/* in r5c cache (to-be/being handled or
				 * in conf->r5c_full_stripe_list)
				 */
	STRIPE_R5C_PREFLUSH,	/* need to flush journal device */
};

#define STRIPE_EXPAND_SYNC_FLAGS \
	((1 << STRIPE_EXPAND_SOURCE) |\
	(1 << STRIPE_EXPAND_READY) |\
	(1 << STRIPE_EXPANDING) |\
	(1 << STRIPE_SYNC_REQUESTED))
/*
 * Operation request flags
 */
enum {
	STRIPE_OP_BIOFILL,
	STRIPE_OP_COMPUTE_BLK,
	STRIPE_OP_PREXOR,
	STRIPE_OP_BIODRAIN,
	STRIPE_OP_RECONSTRUCT,
	STRIPE_OP_CHECK,
	STRIPE_OP_PARTIAL_PARITY,
};

/*
 * RAID parity calculation preferences
 */
enum {
	PARITY_DISABLE_RMW = 0,
	PARITY_ENABLE_RMW,
	PARITY_PREFER_RMW,
};

/*
 * Pages requested from set_syndrome_sources()
 */
enum {
	SYNDROME_SRC_ALL,
	SYNDROME_SRC_WANT_DRAIN,
	SYNDROME_SRC_WRITTEN,
};
/*
 * Plugging:
 *
 * To improve write throughput, we need to delay the handling of some
 * stripes until there has been a chance that several write requests
 * for the one stripe have all been collected.
 * In particular, any write request that would require pre-reading
 * is put on a "delayed" queue until there are no stripes currently
 * in a pre-read phase.  Further, if the "delayed" queue is empty when
 * a stripe is put on it then we "plug" the queue and do not process it
 * until an unplug call is made. (the unplug_io_fn() is called).
 *
 * When preread is initiated on a stripe, we set PREREAD_ACTIVE and add
 * it to the count of prereading stripes.
 * When write is initiated, or the stripe refcnt == 0 (just in case) we
 * clear the PREREAD_ACTIVE flag and decrement the count
 * Whenever the 'handle' queue is empty and the device is not plugged, we
 * move any strips from delayed to handle and clear the DELAYED flag and set
 * PREREAD_ACTIVE.
 * In stripe_handle, if we find pre-reading is necessary, we do it if
 * PREREAD_ACTIVE is set, else we set DELAYED which will send it to the delayed queue.
 * HANDLE gets cleared if stripe_handle leaves nothing locked.
 */

/* Note: disk_info.rdev can be set to NULL asynchronously by raid5_remove_disk.
 * There are three safe ways to access disk_info.rdev.
 * 1/ when holding mddev->reconfig_mutex
 * 2/ when resync/recovery/reshape is known to be happening - i.e. in code that
 *    is called as part of performing resync/recovery/reshape.
 * 3/ while holding rcu_read_lock(), use rcu_dereference to get the pointer
 *    and if it is non-NULL, increment rdev->nr_pending before dropping the RCU
 *    lock.
 * When .rdev is set to NULL, the nr_pending count checked again and if
 * it has been incremented, the pointer is put back in .rdev.
 */

struct disk_info {
	struct md_rdev	*rdev, *replacement;
	struct page	*extra_page; /* extra page to use in prexor */
};

/*
 * Stripe cache
 */

#define NR_STRIPES		256

#if PAGE_SIZE == DEFAULT_STRIPE_SIZE
#define STRIPE_SIZE		PAGE_SIZE
#define STRIPE_SHIFT		(PAGE_SHIFT - 9)
#define STRIPE_SECTORS		(STRIPE_SIZE>>9)
#endif

#define	IO_THRESHOLD		1
#define BYPASS_THRESHOLD	1
#define NR_HASH			(PAGE_SIZE / sizeof(struct hlist_head))
#define HASH_MASK		(NR_HASH - 1)
#define MAX_STRIPE_BATCH	8

/* NOTE NR_STRIPE_HASH_LOCKS must remain below 64.
 * This is because we sometimes take all the spinlocks
 * and creating that much locking depth can cause
 * problems.
 */
#define NR_STRIPE_HASH_LOCKS 8
#define STRIPE_HASH_LOCKS_MASK (NR_STRIPE_HASH_LOCKS - 1)

struct r5worker {
	struct work_struct work;
	struct r5worker_group *group;
	struct list_head temp_inactive_list[NR_STRIPE_HASH_LOCKS];
	bool working;
};

struct r5worker_group {
	struct list_head handle_list;
	struct list_head loprio_list;
	struct r5conf *conf;
	struct r5worker *workers;
	int stripes_cnt;
};

/*
 * r5c journal modes of the array: write-back or write-through.
 * write-through mode has identical behavior as existing log only
 * implementation.
 */
enum r5c_journal_mode {
	R5C_JOURNAL_MODE_WRITE_THROUGH = 0,
	R5C_JOURNAL_MODE_WRITE_BACK = 1,
};

enum r5_cache_state {
	R5_INACTIVE_BLOCKED,	/* release of inactive stripes blocked,
				 * waiting for 25% to be free
				 */
	R5_ALLOC_MORE,		/* It might help to allocate another
				 * stripe.
				 */
	R5_DID_ALLOC,		/* A stripe was allocated, don't allocate
				 * more until at least one has been
				 * released.  This avoids flooding
				 * the cache.
				 */
	R5C_LOG_TIGHT,		/* log device space tight, need to
				 * prioritize stripes at last_checkpoint
				 */
	R5C_LOG_CRITICAL,	/* log device is running out of space,
				 * only process stripes that are already
				 * occupying the log
				 */
	R5C_EXTRA_PAGE_IN_USE,	/* a stripe is using disk_info.extra_page
				 * for prexor
				 */
};

#define PENDING_IO_MAX 512
#define PENDING_IO_ONE_FLUSH 128
struct r5pending_data {
	struct list_head sibling;
	sector_t sector; /* stripe sector */
	struct bio_list bios;
};

struct r5conf {
	struct hlist_head	*stripe_hashtbl;
	/* only protect corresponding hash list and inactive_list */
	spinlock_t		hash_locks[NR_STRIPE_HASH_LOCKS];
	struct mddev		*mddev;
	int			chunk_sectors;
	int			level, algorithm, rmw_level;
	int			max_degraded;
	int			raid_disks;
	int			max_nr_stripes;
	int			min_nr_stripes;
#if PAGE_SIZE != DEFAULT_STRIPE_SIZE
	unsigned long	stripe_size;
	unsigned int	stripe_shift;
	unsigned long	stripe_sectors;
#endif

	/* reshape_progress is the leading edge of a 'reshape'
	 * It has value MaxSector when no reshape is happening
	 * If delta_disks < 0, it is the last sector we started work on,
	 * else is it the next sector to work on.
	 */
	sector_t		reshape_progress;
	/* reshape_safe is the trailing edge of a reshape.  We know that
	 * before (or after) this address, all reshape has completed.
	 */
	sector_t		reshape_safe;
	int			previous_raid_disks;
	int			prev_chunk_sectors;
	int			prev_algo;
	short			generation; /* increments with every reshape */
	seqcount_spinlock_t	gen_lock;	/* lock against generation changes */
	unsigned long		reshape_checkpoint; /* Time we last updated
						     * metadata */
	long long		min_offset_diff; /* minimum difference between
						  * data_offset and
						  * new_data_offset across all
						  * devices.  May be negative,
						  * but is closest to zero.
						  */

	struct list_head	handle_list; /* stripes needing handling */
	struct list_head	loprio_list; /* low priority stripes */
	struct list_head	hold_list; /* preread ready stripes */
	struct list_head	delayed_list; /* stripes that have plugged requests */
	struct list_head	bitmap_list; /* stripes delaying awaiting bitmap update */
	struct bio		*retry_read_aligned; /* currently retrying aligned bios   */
	unsigned int		retry_read_offset; /* sector offset into retry_read_aligned */
	struct bio		*retry_read_aligned_list; /* aligned bios retry list  */
	atomic_t		preread_active_stripes; /* stripes with scheduled io */
	atomic_t		active_aligned_reads;
	atomic_t		pending_full_writes; /* full write backlog */
	int			bypass_count; /* bypassed prereads */
	int			bypass_threshold; /* preread nice */
	int			skip_copy; /* Don't copy data from bio to stripe cache */
	struct list_head	*last_hold; /* detect hold_list promotions */

	atomic_t		reshape_stripes; /* stripes with pending writes for reshape */
	/* unfortunately we need two cache names as we temporarily have
	 * two caches.
	 */
	int			active_name;
	char			cache_name[2][32];
	struct kmem_cache	*slab_cache; /* for allocating stripes */
	struct mutex		cache_size_mutex; /* Protect changes to cache size */

	int			seq_flush, seq_write;
	int			quiesce;

	int			fullsync;  /* set to 1 if a full sync is needed,
					    * (fresh device added).
					    * Cleared when a sync completes.
					    */
	int			recovery_disabled;
	/* per cpu variables */
	struct raid5_percpu {
		struct page	*spare_page; /* Used when checking P/Q in raid6 */
		void		*scribble;  /* space for constructing buffer
					     * lists and performing address
					     * conversions
					     */
		int             scribble_obj_size;
		local_lock_t    lock;
	} __percpu *percpu;
	int scribble_disks;
	int scribble_sectors;
	struct hlist_node node;

	/*
	 * Free stripes pool
	 */
	atomic_t		active_stripes;
	struct list_head	inactive_list[NR_STRIPE_HASH_LOCKS];

	atomic_t		r5c_cached_full_stripes;
	struct list_head	r5c_full_stripe_list;
	atomic_t		r5c_cached_partial_stripes;
	struct list_head	r5c_partial_stripe_list;
	atomic_t		r5c_flushing_full_stripes;
	atomic_t		r5c_flushing_partial_stripes;

	atomic_t		empty_inactive_list_nr;
	struct llist_head	released_stripes;
	wait_queue_head_t	wait_for_quiescent;
	wait_queue_head_t	wait_for_stripe;
	wait_queue_head_t	wait_for_overlap;
	unsigned long		cache_state;
	struct shrinker		shrinker;
	int			pool_size; /* number of disks in stripeheads in pool */
	spinlock_t		device_lock;
	struct disk_info	*disks;
	struct bio_set		bio_split;

	/* When taking over an array from a different personality, we store
	 * the new thread here until we fully activate the array.
	 */
	struct md_thread	*thread;
	struct list_head	temp_inactive_list[NR_STRIPE_HASH_LOCKS];
	struct r5worker_group	*worker_groups;
	int			group_cnt;
	int			worker_cnt_per_group;
	struct r5l_log		*log;
	void			*log_private;

	spinlock_t		pending_bios_lock;
	bool			batch_bio_dispatch;
	struct r5pending_data	*pending_data;
	struct list_head	free_list;
	struct list_head	pending_list;
	int			pending_data_cnt;
	struct r5pending_data	*next_pending_data;
};

#if PAGE_SIZE == DEFAULT_STRIPE_SIZE
#define RAID5_STRIPE_SIZE(conf)	STRIPE_SIZE
#define RAID5_STRIPE_SHIFT(conf)	STRIPE_SHIFT
#define RAID5_STRIPE_SECTORS(conf)	STRIPE_SECTORS
#else
#define RAID5_STRIPE_SIZE(conf)	((conf)->stripe_size)
#define RAID5_STRIPE_SHIFT(conf)	((conf)->stripe_shift)
#define RAID5_STRIPE_SECTORS(conf)	((conf)->stripe_sectors)
#endif

/* bio's attached to a stripe+device for I/O are linked together in bi_sector
 * order without overlap.  There may be several bio's per stripe+device, and
 * a bio could span several devices.
 * When walking this list for a particular stripe+device, we must never proceed
 * beyond a bio that extends past this device, as the next bio might no longer
 * be valid.
 * This function is used to determine the 'next' bio in the list, given the
 * sector of the current stripe+device
 */
static inline struct bio *r5_next_bio(struct r5conf *conf, struct bio *bio, sector_t sector)
{
	if (bio_end_sector(bio) < sector + RAID5_STRIPE_SECTORS(conf))
		return bio->bi_next;
	else
		return NULL;
}

/*
 * Our supported algorithms
 */
#define ALGORITHM_LEFT_ASYMMETRIC	0 /* Rotating Parity N with Data Restart */
#define ALGORITHM_RIGHT_ASYMMETRIC	1 /* Rotating Parity 0 with Data Restart */
#define ALGORITHM_LEFT_SYMMETRIC	2 /* Rotating Parity N with Data Continuation */
#define ALGORITHM_RIGHT_SYMMETRIC	3 /* Rotating Parity 0 with Data Continuation */

/* Define non-rotating (raid4) algorithms.  These allow
 * conversion of raid4 to raid5.
 */
#define ALGORITHM_PARITY_0		4 /* P or P,Q are initial devices */
#define ALGORITHM_PARITY_N		5 /* P or P,Q are final devices. */

/* DDF RAID6 layouts differ from md/raid6 layouts in two ways.
 * Firstly, the exact positioning of the parity block is slightly
 * different between the 'LEFT_*' modes of md and the "_N_*" modes
 * of DDF.
 * Secondly, or order of datablocks over which the Q syndrome is computed
 * is different.
 * Consequently we have different layouts for DDF/raid6 than md/raid6.
 * These layouts are from the DDFv1.2 spec.
 * Interestingly DDFv1.2-Errata-A does not specify N_CONTINUE but
 * leaves RLQ=3 as 'Vendor Specific'
 */

#define ALGORITHM_ROTATING_ZERO_RESTART	8 /* DDF PRL=6 RLQ=1 */
#define ALGORITHM_ROTATING_N_RESTART	9 /* DDF PRL=6 RLQ=2 */
#define ALGORITHM_ROTATING_N_CONTINUE	10 /*DDF PRL=6 RLQ=3 */

/* For every RAID5 algorithm we define a RAID6 algorithm
 * with exactly the same layout for data and parity, and
 * with the Q block always on the last device (N-1).
 * This allows trivial conversion from RAID5 to RAID6
 */
#define ALGORITHM_LEFT_ASYMMETRIC_6	16
#define ALGORITHM_RIGHT_ASYMMETRIC_6	17
#define ALGORITHM_LEFT_SYMMETRIC_6	18
#define ALGORITHM_RIGHT_SYMMETRIC_6	19
#define ALGORITHM_PARITY_0_6		20
#define ALGORITHM_PARITY_N_6		ALGORITHM_PARITY_N

static inline int algorithm_valid_raid5(int layout)
{
	return (layout >= 0) &&
		(layout <= 5);
}
static inline int algorithm_valid_raid6(int layout)
{
	return (layout >= 0 && layout <= 5)
		||
		(layout >= 8 && layout <= 10)
		||
		(layout >= 16 && layout <= 20);
}

static inline int algorithm_is_DDF(int layout)
{
	return layout >= 8 && layout <= 10;
}

#if PAGE_SIZE != DEFAULT_STRIPE_SIZE
/*
 * Return offset of the corresponding page for r5dev.
 */
static inline int raid5_get_page_offset(struct stripe_head *sh, int disk_idx)
{
	return (disk_idx % sh->stripes_per_page) * RAID5_STRIPE_SIZE(sh->raid_conf);
}

/*
 * Return corresponding page address for r5dev.
 */
static inline struct page *
raid5_get_dev_page(struct stripe_head *sh, int disk_idx)
{
	return sh->pages[disk_idx / sh->stripes_per_page];
}
#endif

extern void md_raid5_kick_device(struct r5conf *conf);
extern int raid5_set_cache_size(struct mddev *mddev, int size);
extern sector_t raid5_compute_blocknr(struct stripe_head *sh, int i, int previous);
extern void raid5_release_stripe(struct stripe_head *sh);
extern sector_t raid5_compute_sector(struct r5conf *conf, sector_t r_sector,
				     int previous, int *dd_idx,
				     struct stripe_head *sh);
extern struct stripe_head *
raid5_get_active_stripe(struct r5conf *conf, sector_t sector,
			int previous, int noblock, int noquiesce);
extern int raid5_calc_degraded(struct r5conf *conf);
extern int r5c_journal_mode_set(struct mddev *mddev, int journal_mode);
#endif
