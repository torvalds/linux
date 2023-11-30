// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Shaohua Li <shli@fb.com>
 * Copyright (C) 2016 Song Liu <songliubraving@fb.com>
 */
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/raid/md_p.h>
#include <linux/crc32c.h>
#include <linux/random.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include "md.h"
#include "raid5.h"
#include "md-bitmap.h"
#include "raid5-log.h"

/*
 * metadata/data stored in disk with 4k size unit (a block) regardless
 * underneath hardware sector size. only works with PAGE_SIZE == 4096
 */
#define BLOCK_SECTORS (8)
#define BLOCK_SECTOR_SHIFT (3)

/*
 * log->max_free_space is min(1/4 disk size, 10G reclaimable space).
 *
 * In write through mode, the reclaim runs every log->max_free_space.
 * This can prevent the recovery scans for too long
 */
#define RECLAIM_MAX_FREE_SPACE (10 * 1024 * 1024 * 2) /* sector */
#define RECLAIM_MAX_FREE_SPACE_SHIFT (2)

/* wake up reclaim thread periodically */
#define R5C_RECLAIM_WAKEUP_INTERVAL (30 * HZ)
/* start flush with these full stripes */
#define R5C_FULL_STRIPE_FLUSH_BATCH(conf) (conf->max_nr_stripes / 4)
/* reclaim stripes in groups */
#define R5C_RECLAIM_STRIPE_GROUP (NR_STRIPE_HASH_LOCKS * 2)

/*
 * We only need 2 bios per I/O unit to make progress, but ensure we
 * have a few more available to not get too tight.
 */
#define R5L_POOL_SIZE	4

static char *r5c_journal_mode_str[] = {"write-through",
				       "write-back"};
/*
 * raid5 cache state machine
 *
 * With the RAID cache, each stripe works in two phases:
 *	- caching phase
 *	- writing-out phase
 *
 * These two phases are controlled by bit STRIPE_R5C_CACHING:
 *   if STRIPE_R5C_CACHING == 0, the stripe is in writing-out phase
 *   if STRIPE_R5C_CACHING == 1, the stripe is in caching phase
 *
 * When there is no journal, or the journal is in write-through mode,
 * the stripe is always in writing-out phase.
 *
 * For write-back journal, the stripe is sent to caching phase on write
 * (r5c_try_caching_write). r5c_make_stripe_write_out() kicks off
 * the write-out phase by clearing STRIPE_R5C_CACHING.
 *
 * Stripes in caching phase do not write the raid disks. Instead, all
 * writes are committed from the log device. Therefore, a stripe in
 * caching phase handles writes as:
 *	- write to log device
 *	- return IO
 *
 * Stripes in writing-out phase handle writes as:
 *	- calculate parity
 *	- write pending data and parity to journal
 *	- write data and parity to raid disks
 *	- return IO for pending writes
 */

struct r5l_log {
	struct md_rdev *rdev;

	u32 uuid_checksum;

	sector_t device_size;		/* log device size, round to
					 * BLOCK_SECTORS */
	sector_t max_free_space;	/* reclaim run if free space is at
					 * this size */

	sector_t last_checkpoint;	/* log tail. where recovery scan
					 * starts from */
	u64 last_cp_seq;		/* log tail sequence */

	sector_t log_start;		/* log head. where new data appends */
	u64 seq;			/* log head sequence */

	sector_t next_checkpoint;

	struct mutex io_mutex;
	struct r5l_io_unit *current_io;	/* current io_unit accepting new data */

	spinlock_t io_list_lock;
	struct list_head running_ios;	/* io_units which are still running,
					 * and have not yet been completely
					 * written to the log */
	struct list_head io_end_ios;	/* io_units which have been completely
					 * written to the log but not yet written
					 * to the RAID */
	struct list_head flushing_ios;	/* io_units which are waiting for log
					 * cache flush */
	struct list_head finished_ios;	/* io_units which settle down in log disk */
	struct bio flush_bio;

	struct list_head no_mem_stripes;   /* pending stripes, -ENOMEM */

	struct kmem_cache *io_kc;
	mempool_t io_pool;
	struct bio_set bs;
	mempool_t meta_pool;

	struct md_thread *reclaim_thread;
	unsigned long reclaim_target;	/* number of space that need to be
					 * reclaimed.  if it's 0, reclaim spaces
					 * used by io_units which are in
					 * IO_UNIT_STRIPE_END state (eg, reclaim
					 * doesn't wait for specific io_unit
					 * switching to IO_UNIT_STRIPE_END
					 * state) */
	wait_queue_head_t iounit_wait;

	struct list_head no_space_stripes; /* pending stripes, log has no space */
	spinlock_t no_space_stripes_lock;

	bool need_cache_flush;

	/* for r5c_cache */
	enum r5c_journal_mode r5c_journal_mode;

	/* all stripes in r5cache, in the order of seq at sh->log_start */
	struct list_head stripe_in_journal_list;

	spinlock_t stripe_in_journal_lock;
	atomic_t stripe_in_journal_count;

	/* to submit async io_units, to fulfill ordering of flush */
	struct work_struct deferred_io_work;
	/* to disable write back during in degraded mode */
	struct work_struct disable_writeback_work;

	/* to for chunk_aligned_read in writeback mode, details below */
	spinlock_t tree_lock;
	struct radix_tree_root big_stripe_tree;
};

/*
 * Enable chunk_aligned_read() with write back cache.
 *
 * Each chunk may contain more than one stripe (for example, a 256kB
 * chunk contains 64 4kB-page, so this chunk contain 64 stripes). For
 * chunk_aligned_read, these stripes are grouped into one "big_stripe".
 * For each big_stripe, we count how many stripes of this big_stripe
 * are in the write back cache. These data are tracked in a radix tree
 * (big_stripe_tree). We use radix_tree item pointer as the counter.
 * r5c_tree_index() is used to calculate keys for the radix tree.
 *
 * chunk_aligned_read() calls r5c_big_stripe_cached() to look up
 * big_stripe of each chunk in the tree. If this big_stripe is in the
 * tree, chunk_aligned_read() aborts. This look up is protected by
 * rcu_read_lock().
 *
 * It is necessary to remember whether a stripe is counted in
 * big_stripe_tree. Instead of adding new flag, we reuses existing flags:
 * STRIPE_R5C_PARTIAL_STRIPE and STRIPE_R5C_FULL_STRIPE. If either of these
 * two flags are set, the stripe is counted in big_stripe_tree. This
 * requires moving set_bit(STRIPE_R5C_PARTIAL_STRIPE) to
 * r5c_try_caching_write(); and moving clear_bit of
 * STRIPE_R5C_PARTIAL_STRIPE and STRIPE_R5C_FULL_STRIPE to
 * r5c_finish_stripe_write_out().
 */

/*
 * radix tree requests lowest 2 bits of data pointer to be 2b'00.
 * So it is necessary to left shift the counter by 2 bits before using it
 * as data pointer of the tree.
 */
#define R5C_RADIX_COUNT_SHIFT 2

/*
 * calculate key for big_stripe_tree
 *
 * sect: align_bi->bi_iter.bi_sector or sh->sector
 */
static inline sector_t r5c_tree_index(struct r5conf *conf,
				      sector_t sect)
{
	sector_div(sect, conf->chunk_sectors);
	return sect;
}

/*
 * an IO range starts from a meta data block and end at the next meta data
 * block. The io unit's the meta data block tracks data/parity followed it. io
 * unit is written to log disk with normal write, as we always flush log disk
 * first and then start move data to raid disks, there is no requirement to
 * write io unit with FLUSH/FUA
 */
struct r5l_io_unit {
	struct r5l_log *log;

	struct page *meta_page;	/* store meta block */
	int meta_offset;	/* current offset in meta_page */

	struct bio *current_bio;/* current_bio accepting new data */

	atomic_t pending_stripe;/* how many stripes not flushed to raid */
	u64 seq;		/* seq number of the metablock */
	sector_t log_start;	/* where the io_unit starts */
	sector_t log_end;	/* where the io_unit ends */
	struct list_head log_sibling; /* log->running_ios */
	struct list_head stripe_list; /* stripes added to the io_unit */

	int state;
	bool need_split_bio;
	struct bio *split_bio;

	unsigned int has_flush:1;		/* include flush request */
	unsigned int has_fua:1;			/* include fua request */
	unsigned int has_null_flush:1;		/* include null flush request */
	unsigned int has_flush_payload:1;	/* include flush payload  */
	/*
	 * io isn't sent yet, flush/fua request can only be submitted till it's
	 * the first IO in running_ios list
	 */
	unsigned int io_deferred:1;

	struct bio_list flush_barriers;   /* size == 0 flush bios */
};

/* r5l_io_unit state */
enum r5l_io_unit_state {
	IO_UNIT_RUNNING = 0,	/* accepting new IO */
	IO_UNIT_IO_START = 1,	/* io_unit bio start writing to log,
				 * don't accepting new bio */
	IO_UNIT_IO_END = 2,	/* io_unit bio finish writing to log */
	IO_UNIT_STRIPE_END = 3,	/* stripes data finished writing to raid */
};

bool r5c_is_writeback(struct r5l_log *log)
{
	return (log != NULL &&
		log->r5c_journal_mode == R5C_JOURNAL_MODE_WRITE_BACK);
}

static sector_t r5l_ring_add(struct r5l_log *log, sector_t start, sector_t inc)
{
	start += inc;
	if (start >= log->device_size)
		start = start - log->device_size;
	return start;
}

static sector_t r5l_ring_distance(struct r5l_log *log, sector_t start,
				  sector_t end)
{
	if (end >= start)
		return end - start;
	else
		return end + log->device_size - start;
}

static bool r5l_has_free_space(struct r5l_log *log, sector_t size)
{
	sector_t used_size;

	used_size = r5l_ring_distance(log, log->last_checkpoint,
					log->log_start);

	return log->device_size > used_size + size;
}

static void __r5l_set_io_unit_state(struct r5l_io_unit *io,
				    enum r5l_io_unit_state state)
{
	if (WARN_ON(io->state >= state))
		return;
	io->state = state;
}

static void
r5c_return_dev_pending_writes(struct r5conf *conf, struct r5dev *dev)
{
	struct bio *wbi, *wbi2;

	wbi = dev->written;
	dev->written = NULL;
	while (wbi && wbi->bi_iter.bi_sector <
	       dev->sector + RAID5_STRIPE_SECTORS(conf)) {
		wbi2 = r5_next_bio(conf, wbi, dev->sector);
		md_write_end(conf->mddev);
		bio_endio(wbi);
		wbi = wbi2;
	}
}

void r5c_handle_cached_data_endio(struct r5conf *conf,
				  struct stripe_head *sh, int disks)
{
	int i;

	for (i = sh->disks; i--; ) {
		if (sh->dev[i].written) {
			set_bit(R5_UPTODATE, &sh->dev[i].flags);
			r5c_return_dev_pending_writes(conf, &sh->dev[i]);
			md_bitmap_endwrite(conf->mddev->bitmap, sh->sector,
					   RAID5_STRIPE_SECTORS(conf),
					   !test_bit(STRIPE_DEGRADED, &sh->state),
					   0);
		}
	}
}

void r5l_wake_reclaim(struct r5l_log *log, sector_t space);

/* Check whether we should flush some stripes to free up stripe cache */
void r5c_check_stripe_cache_usage(struct r5conf *conf)
{
	int total_cached;

	if (!r5c_is_writeback(conf->log))
		return;

	total_cached = atomic_read(&conf->r5c_cached_partial_stripes) +
		atomic_read(&conf->r5c_cached_full_stripes);

	/*
	 * The following condition is true for either of the following:
	 *   - stripe cache pressure high:
	 *          total_cached > 3/4 min_nr_stripes ||
	 *          empty_inactive_list_nr > 0
	 *   - stripe cache pressure moderate:
	 *          total_cached > 1/2 min_nr_stripes
	 */
	if (total_cached > conf->min_nr_stripes * 1 / 2 ||
	    atomic_read(&conf->empty_inactive_list_nr) > 0)
		r5l_wake_reclaim(conf->log, 0);
}

/*
 * flush cache when there are R5C_FULL_STRIPE_FLUSH_BATCH or more full
 * stripes in the cache
 */
void r5c_check_cached_full_stripe(struct r5conf *conf)
{
	if (!r5c_is_writeback(conf->log))
		return;

	/*
	 * wake up reclaim for R5C_FULL_STRIPE_FLUSH_BATCH cached stripes
	 * or a full stripe (chunk size / 4k stripes).
	 */
	if (atomic_read(&conf->r5c_cached_full_stripes) >=
	    min(R5C_FULL_STRIPE_FLUSH_BATCH(conf),
		conf->chunk_sectors >> RAID5_STRIPE_SHIFT(conf)))
		r5l_wake_reclaim(conf->log, 0);
}

/*
 * Total log space (in sectors) needed to flush all data in cache
 *
 * To avoid deadlock due to log space, it is necessary to reserve log
 * space to flush critical stripes (stripes that occupying log space near
 * last_checkpoint). This function helps check how much log space is
 * required to flush all cached stripes.
 *
 * To reduce log space requirements, two mechanisms are used to give cache
 * flush higher priorities:
 *    1. In handle_stripe_dirtying() and schedule_reconstruction(),
 *       stripes ALREADY in journal can be flushed w/o pending writes;
 *    2. In r5l_write_stripe() and r5c_cache_data(), stripes NOT in journal
 *       can be delayed (r5l_add_no_space_stripe).
 *
 * In cache flush, the stripe goes through 1 and then 2. For a stripe that
 * already passed 1, flushing it requires at most (conf->max_degraded + 1)
 * pages of journal space. For stripes that has not passed 1, flushing it
 * requires (conf->raid_disks + 1) pages of journal space. There are at
 * most (conf->group_cnt + 1) stripe that passed 1. So total journal space
 * required to flush all cached stripes (in pages) is:
 *
 *     (stripe_in_journal_count - group_cnt - 1) * (max_degraded + 1) +
 *     (group_cnt + 1) * (raid_disks + 1)
 * or
 *     (stripe_in_journal_count) * (max_degraded + 1) +
 *     (group_cnt + 1) * (raid_disks - max_degraded)
 */
static sector_t r5c_log_required_to_flush_cache(struct r5conf *conf)
{
	struct r5l_log *log = conf->log;

	if (!r5c_is_writeback(log))
		return 0;

	return BLOCK_SECTORS *
		((conf->max_degraded + 1) * atomic_read(&log->stripe_in_journal_count) +
		 (conf->raid_disks - conf->max_degraded) * (conf->group_cnt + 1));
}

/*
 * evaluate log space usage and update R5C_LOG_TIGHT and R5C_LOG_CRITICAL
 *
 * R5C_LOG_TIGHT is set when free space on the log device is less than 3x of
 * reclaim_required_space. R5C_LOG_CRITICAL is set when free space on the log
 * device is less than 2x of reclaim_required_space.
 */
static inline void r5c_update_log_state(struct r5l_log *log)
{
	struct r5conf *conf = log->rdev->mddev->private;
	sector_t free_space;
	sector_t reclaim_space;
	bool wake_reclaim = false;

	if (!r5c_is_writeback(log))
		return;

	free_space = r5l_ring_distance(log, log->log_start,
				       log->last_checkpoint);
	reclaim_space = r5c_log_required_to_flush_cache(conf);
	if (free_space < 2 * reclaim_space)
		set_bit(R5C_LOG_CRITICAL, &conf->cache_state);
	else {
		if (test_bit(R5C_LOG_CRITICAL, &conf->cache_state))
			wake_reclaim = true;
		clear_bit(R5C_LOG_CRITICAL, &conf->cache_state);
	}
	if (free_space < 3 * reclaim_space)
		set_bit(R5C_LOG_TIGHT, &conf->cache_state);
	else
		clear_bit(R5C_LOG_TIGHT, &conf->cache_state);

	if (wake_reclaim)
		r5l_wake_reclaim(log, 0);
}

/*
 * Put the stripe into writing-out phase by clearing STRIPE_R5C_CACHING.
 * This function should only be called in write-back mode.
 */
void r5c_make_stripe_write_out(struct stripe_head *sh)
{
	struct r5conf *conf = sh->raid_conf;
	struct r5l_log *log = conf->log;

	BUG_ON(!r5c_is_writeback(log));

	WARN_ON(!test_bit(STRIPE_R5C_CACHING, &sh->state));
	clear_bit(STRIPE_R5C_CACHING, &sh->state);

	if (!test_and_set_bit(STRIPE_PREREAD_ACTIVE, &sh->state))
		atomic_inc(&conf->preread_active_stripes);
}

static void r5c_handle_data_cached(struct stripe_head *sh)
{
	int i;

	for (i = sh->disks; i--; )
		if (test_and_clear_bit(R5_Wantwrite, &sh->dev[i].flags)) {
			set_bit(R5_InJournal, &sh->dev[i].flags);
			clear_bit(R5_LOCKED, &sh->dev[i].flags);
		}
	clear_bit(STRIPE_LOG_TRAPPED, &sh->state);
}

/*
 * this journal write must contain full parity,
 * it may also contain some data pages
 */
static void r5c_handle_parity_cached(struct stripe_head *sh)
{
	int i;

	for (i = sh->disks; i--; )
		if (test_bit(R5_InJournal, &sh->dev[i].flags))
			set_bit(R5_Wantwrite, &sh->dev[i].flags);
}

/*
 * Setting proper flags after writing (or flushing) data and/or parity to the
 * log device. This is called from r5l_log_endio() or r5l_log_flush_endio().
 */
static void r5c_finish_cache_stripe(struct stripe_head *sh)
{
	struct r5l_log *log = sh->raid_conf->log;

	if (log->r5c_journal_mode == R5C_JOURNAL_MODE_WRITE_THROUGH) {
		BUG_ON(test_bit(STRIPE_R5C_CACHING, &sh->state));
		/*
		 * Set R5_InJournal for parity dev[pd_idx]. This means
		 * all data AND parity in the journal. For RAID 6, it is
		 * NOT necessary to set the flag for dev[qd_idx], as the
		 * two parities are written out together.
		 */
		set_bit(R5_InJournal, &sh->dev[sh->pd_idx].flags);
	} else if (test_bit(STRIPE_R5C_CACHING, &sh->state)) {
		r5c_handle_data_cached(sh);
	} else {
		r5c_handle_parity_cached(sh);
		set_bit(R5_InJournal, &sh->dev[sh->pd_idx].flags);
	}
}

static void r5l_io_run_stripes(struct r5l_io_unit *io)
{
	struct stripe_head *sh, *next;

	list_for_each_entry_safe(sh, next, &io->stripe_list, log_list) {
		list_del_init(&sh->log_list);

		r5c_finish_cache_stripe(sh);

		set_bit(STRIPE_HANDLE, &sh->state);
		raid5_release_stripe(sh);
	}
}

static void r5l_log_run_stripes(struct r5l_log *log)
{
	struct r5l_io_unit *io, *next;

	lockdep_assert_held(&log->io_list_lock);

	list_for_each_entry_safe(io, next, &log->running_ios, log_sibling) {
		/* don't change list order */
		if (io->state < IO_UNIT_IO_END)
			break;

		list_move_tail(&io->log_sibling, &log->finished_ios);
		r5l_io_run_stripes(io);
	}
}

static void r5l_move_to_end_ios(struct r5l_log *log)
{
	struct r5l_io_unit *io, *next;

	lockdep_assert_held(&log->io_list_lock);

	list_for_each_entry_safe(io, next, &log->running_ios, log_sibling) {
		/* don't change list order */
		if (io->state < IO_UNIT_IO_END)
			break;
		list_move_tail(&io->log_sibling, &log->io_end_ios);
	}
}

static void __r5l_stripe_write_finished(struct r5l_io_unit *io);
static void r5l_log_endio(struct bio *bio)
{
	struct r5l_io_unit *io = bio->bi_private;
	struct r5l_io_unit *io_deferred;
	struct r5l_log *log = io->log;
	unsigned long flags;
	bool has_null_flush;
	bool has_flush_payload;

	if (bio->bi_status)
		md_error(log->rdev->mddev, log->rdev);

	bio_put(bio);
	mempool_free(io->meta_page, &log->meta_pool);

	spin_lock_irqsave(&log->io_list_lock, flags);
	__r5l_set_io_unit_state(io, IO_UNIT_IO_END);

	/*
	 * if the io doesn't not have null_flush or flush payload,
	 * it is not safe to access it after releasing io_list_lock.
	 * Therefore, it is necessary to check the condition with
	 * the lock held.
	 */
	has_null_flush = io->has_null_flush;
	has_flush_payload = io->has_flush_payload;

	if (log->need_cache_flush && !list_empty(&io->stripe_list))
		r5l_move_to_end_ios(log);
	else
		r5l_log_run_stripes(log);
	if (!list_empty(&log->running_ios)) {
		/*
		 * FLUSH/FUA io_unit is deferred because of ordering, now we
		 * can dispatch it
		 */
		io_deferred = list_first_entry(&log->running_ios,
					       struct r5l_io_unit, log_sibling);
		if (io_deferred->io_deferred)
			schedule_work(&log->deferred_io_work);
	}

	spin_unlock_irqrestore(&log->io_list_lock, flags);

	if (log->need_cache_flush)
		md_wakeup_thread(log->rdev->mddev->thread);

	/* finish flush only io_unit and PAYLOAD_FLUSH only io_unit */
	if (has_null_flush) {
		struct bio *bi;

		WARN_ON(bio_list_empty(&io->flush_barriers));
		while ((bi = bio_list_pop(&io->flush_barriers)) != NULL) {
			bio_endio(bi);
			if (atomic_dec_and_test(&io->pending_stripe)) {
				__r5l_stripe_write_finished(io);
				return;
			}
		}
	}
	/* decrease pending_stripe for flush payload */
	if (has_flush_payload)
		if (atomic_dec_and_test(&io->pending_stripe))
			__r5l_stripe_write_finished(io);
}

static void r5l_do_submit_io(struct r5l_log *log, struct r5l_io_unit *io)
{
	unsigned long flags;

	spin_lock_irqsave(&log->io_list_lock, flags);
	__r5l_set_io_unit_state(io, IO_UNIT_IO_START);
	spin_unlock_irqrestore(&log->io_list_lock, flags);

	/*
	 * In case of journal device failures, submit_bio will get error
	 * and calls endio, then active stripes will continue write
	 * process. Therefore, it is not necessary to check Faulty bit
	 * of journal device here.
	 *
	 * We can't check split_bio after current_bio is submitted. If
	 * io->split_bio is null, after current_bio is submitted, current_bio
	 * might already be completed and the io_unit is freed. We submit
	 * split_bio first to avoid the issue.
	 */
	if (io->split_bio) {
		if (io->has_flush)
			io->split_bio->bi_opf |= REQ_PREFLUSH;
		if (io->has_fua)
			io->split_bio->bi_opf |= REQ_FUA;
		submit_bio(io->split_bio);
	}

	if (io->has_flush)
		io->current_bio->bi_opf |= REQ_PREFLUSH;
	if (io->has_fua)
		io->current_bio->bi_opf |= REQ_FUA;
	submit_bio(io->current_bio);
}

/* deferred io_unit will be dispatched here */
static void r5l_submit_io_async(struct work_struct *work)
{
	struct r5l_log *log = container_of(work, struct r5l_log,
					   deferred_io_work);
	struct r5l_io_unit *io = NULL;
	unsigned long flags;

	spin_lock_irqsave(&log->io_list_lock, flags);
	if (!list_empty(&log->running_ios)) {
		io = list_first_entry(&log->running_ios, struct r5l_io_unit,
				      log_sibling);
		if (!io->io_deferred)
			io = NULL;
		else
			io->io_deferred = 0;
	}
	spin_unlock_irqrestore(&log->io_list_lock, flags);
	if (io)
		r5l_do_submit_io(log, io);
}

static void r5c_disable_writeback_async(struct work_struct *work)
{
	struct r5l_log *log = container_of(work, struct r5l_log,
					   disable_writeback_work);
	struct mddev *mddev = log->rdev->mddev;
	struct r5conf *conf = mddev->private;
	int locked = 0;

	if (log->r5c_journal_mode == R5C_JOURNAL_MODE_WRITE_THROUGH)
		return;
	pr_info("md/raid:%s: Disabling writeback cache for degraded array.\n",
		mdname(mddev));

	/* wait superblock change before suspend */
	wait_event(mddev->sb_wait,
		   conf->log == NULL ||
		   (!test_bit(MD_SB_CHANGE_PENDING, &mddev->sb_flags) &&
		    (locked = mddev_trylock(mddev))));
	if (locked) {
		mddev_suspend(mddev);
		log->r5c_journal_mode = R5C_JOURNAL_MODE_WRITE_THROUGH;
		mddev_resume(mddev);
		mddev_unlock(mddev);
	}
}

static void r5l_submit_current_io(struct r5l_log *log)
{
	struct r5l_io_unit *io = log->current_io;
	struct r5l_meta_block *block;
	unsigned long flags;
	u32 crc;
	bool do_submit = true;

	if (!io)
		return;

	block = page_address(io->meta_page);
	block->meta_size = cpu_to_le32(io->meta_offset);
	crc = crc32c_le(log->uuid_checksum, block, PAGE_SIZE);
	block->checksum = cpu_to_le32(crc);

	log->current_io = NULL;
	spin_lock_irqsave(&log->io_list_lock, flags);
	if (io->has_flush || io->has_fua) {
		if (io != list_first_entry(&log->running_ios,
					   struct r5l_io_unit, log_sibling)) {
			io->io_deferred = 1;
			do_submit = false;
		}
	}
	spin_unlock_irqrestore(&log->io_list_lock, flags);
	if (do_submit)
		r5l_do_submit_io(log, io);
}

static struct bio *r5l_bio_alloc(struct r5l_log *log)
{
	struct bio *bio = bio_alloc_bioset(log->rdev->bdev, BIO_MAX_VECS,
					   REQ_OP_WRITE, GFP_NOIO, &log->bs);

	bio->bi_iter.bi_sector = log->rdev->data_offset + log->log_start;

	return bio;
}

static void r5_reserve_log_entry(struct r5l_log *log, struct r5l_io_unit *io)
{
	log->log_start = r5l_ring_add(log, log->log_start, BLOCK_SECTORS);

	r5c_update_log_state(log);
	/*
	 * If we filled up the log device start from the beginning again,
	 * which will require a new bio.
	 *
	 * Note: for this to work properly the log size needs to me a multiple
	 * of BLOCK_SECTORS.
	 */
	if (log->log_start == 0)
		io->need_split_bio = true;

	io->log_end = log->log_start;
}

static struct r5l_io_unit *r5l_new_meta(struct r5l_log *log)
{
	struct r5l_io_unit *io;
	struct r5l_meta_block *block;

	io = mempool_alloc(&log->io_pool, GFP_ATOMIC);
	if (!io)
		return NULL;
	memset(io, 0, sizeof(*io));

	io->log = log;
	INIT_LIST_HEAD(&io->log_sibling);
	INIT_LIST_HEAD(&io->stripe_list);
	bio_list_init(&io->flush_barriers);
	io->state = IO_UNIT_RUNNING;

	io->meta_page = mempool_alloc(&log->meta_pool, GFP_NOIO);
	block = page_address(io->meta_page);
	clear_page(block);
	block->magic = cpu_to_le32(R5LOG_MAGIC);
	block->version = R5LOG_VERSION;
	block->seq = cpu_to_le64(log->seq);
	block->position = cpu_to_le64(log->log_start);

	io->log_start = log->log_start;
	io->meta_offset = sizeof(struct r5l_meta_block);
	io->seq = log->seq++;

	io->current_bio = r5l_bio_alloc(log);
	io->current_bio->bi_end_io = r5l_log_endio;
	io->current_bio->bi_private = io;
	bio_add_page(io->current_bio, io->meta_page, PAGE_SIZE, 0);

	r5_reserve_log_entry(log, io);

	spin_lock_irq(&log->io_list_lock);
	list_add_tail(&io->log_sibling, &log->running_ios);
	spin_unlock_irq(&log->io_list_lock);

	return io;
}

static int r5l_get_meta(struct r5l_log *log, unsigned int payload_size)
{
	if (log->current_io &&
	    log->current_io->meta_offset + payload_size > PAGE_SIZE)
		r5l_submit_current_io(log);

	if (!log->current_io) {
		log->current_io = r5l_new_meta(log);
		if (!log->current_io)
			return -ENOMEM;
	}

	return 0;
}

static void r5l_append_payload_meta(struct r5l_log *log, u16 type,
				    sector_t location,
				    u32 checksum1, u32 checksum2,
				    bool checksum2_valid)
{
	struct r5l_io_unit *io = log->current_io;
	struct r5l_payload_data_parity *payload;

	payload = page_address(io->meta_page) + io->meta_offset;
	payload->header.type = cpu_to_le16(type);
	payload->header.flags = cpu_to_le16(0);
	payload->size = cpu_to_le32((1 + !!checksum2_valid) <<
				    (PAGE_SHIFT - 9));
	payload->location = cpu_to_le64(location);
	payload->checksum[0] = cpu_to_le32(checksum1);
	if (checksum2_valid)
		payload->checksum[1] = cpu_to_le32(checksum2);

	io->meta_offset += sizeof(struct r5l_payload_data_parity) +
		sizeof(__le32) * (1 + !!checksum2_valid);
}

static void r5l_append_payload_page(struct r5l_log *log, struct page *page)
{
	struct r5l_io_unit *io = log->current_io;

	if (io->need_split_bio) {
		BUG_ON(io->split_bio);
		io->split_bio = io->current_bio;
		io->current_bio = r5l_bio_alloc(log);
		bio_chain(io->current_bio, io->split_bio);
		io->need_split_bio = false;
	}

	if (!bio_add_page(io->current_bio, page, PAGE_SIZE, 0))
		BUG();

	r5_reserve_log_entry(log, io);
}

static void r5l_append_flush_payload(struct r5l_log *log, sector_t sect)
{
	struct mddev *mddev = log->rdev->mddev;
	struct r5conf *conf = mddev->private;
	struct r5l_io_unit *io;
	struct r5l_payload_flush *payload;
	int meta_size;

	/*
	 * payload_flush requires extra writes to the journal.
	 * To avoid handling the extra IO in quiesce, just skip
	 * flush_payload
	 */
	if (conf->quiesce)
		return;

	mutex_lock(&log->io_mutex);
	meta_size = sizeof(struct r5l_payload_flush) + sizeof(__le64);

	if (r5l_get_meta(log, meta_size)) {
		mutex_unlock(&log->io_mutex);
		return;
	}

	/* current implementation is one stripe per flush payload */
	io = log->current_io;
	payload = page_address(io->meta_page) + io->meta_offset;
	payload->header.type = cpu_to_le16(R5LOG_PAYLOAD_FLUSH);
	payload->header.flags = cpu_to_le16(0);
	payload->size = cpu_to_le32(sizeof(__le64));
	payload->flush_stripes[0] = cpu_to_le64(sect);
	io->meta_offset += meta_size;
	/* multiple flush payloads count as one pending_stripe */
	if (!io->has_flush_payload) {
		io->has_flush_payload = 1;
		atomic_inc(&io->pending_stripe);
	}
	mutex_unlock(&log->io_mutex);
}

static int r5l_log_stripe(struct r5l_log *log, struct stripe_head *sh,
			   int data_pages, int parity_pages)
{
	int i;
	int meta_size;
	int ret;
	struct r5l_io_unit *io;

	meta_size =
		((sizeof(struct r5l_payload_data_parity) + sizeof(__le32))
		 * data_pages) +
		sizeof(struct r5l_payload_data_parity) +
		sizeof(__le32) * parity_pages;

	ret = r5l_get_meta(log, meta_size);
	if (ret)
		return ret;

	io = log->current_io;

	if (test_and_clear_bit(STRIPE_R5C_PREFLUSH, &sh->state))
		io->has_flush = 1;

	for (i = 0; i < sh->disks; i++) {
		if (!test_bit(R5_Wantwrite, &sh->dev[i].flags) ||
		    test_bit(R5_InJournal, &sh->dev[i].flags))
			continue;
		if (i == sh->pd_idx || i == sh->qd_idx)
			continue;
		if (test_bit(R5_WantFUA, &sh->dev[i].flags) &&
		    log->r5c_journal_mode == R5C_JOURNAL_MODE_WRITE_BACK) {
			io->has_fua = 1;
			/*
			 * we need to flush journal to make sure recovery can
			 * reach the data with fua flag
			 */
			io->has_flush = 1;
		}
		r5l_append_payload_meta(log, R5LOG_PAYLOAD_DATA,
					raid5_compute_blocknr(sh, i, 0),
					sh->dev[i].log_checksum, 0, false);
		r5l_append_payload_page(log, sh->dev[i].page);
	}

	if (parity_pages == 2) {
		r5l_append_payload_meta(log, R5LOG_PAYLOAD_PARITY,
					sh->sector, sh->dev[sh->pd_idx].log_checksum,
					sh->dev[sh->qd_idx].log_checksum, true);
		r5l_append_payload_page(log, sh->dev[sh->pd_idx].page);
		r5l_append_payload_page(log, sh->dev[sh->qd_idx].page);
	} else if (parity_pages == 1) {
		r5l_append_payload_meta(log, R5LOG_PAYLOAD_PARITY,
					sh->sector, sh->dev[sh->pd_idx].log_checksum,
					0, false);
		r5l_append_payload_page(log, sh->dev[sh->pd_idx].page);
	} else  /* Just writing data, not parity, in caching phase */
		BUG_ON(parity_pages != 0);

	list_add_tail(&sh->log_list, &io->stripe_list);
	atomic_inc(&io->pending_stripe);
	sh->log_io = io;

	if (log->r5c_journal_mode == R5C_JOURNAL_MODE_WRITE_THROUGH)
		return 0;

	if (sh->log_start == MaxSector) {
		BUG_ON(!list_empty(&sh->r5c));
		sh->log_start = io->log_start;
		spin_lock_irq(&log->stripe_in_journal_lock);
		list_add_tail(&sh->r5c,
			      &log->stripe_in_journal_list);
		spin_unlock_irq(&log->stripe_in_journal_lock);
		atomic_inc(&log->stripe_in_journal_count);
	}
	return 0;
}

/* add stripe to no_space_stripes, and then wake up reclaim */
static inline void r5l_add_no_space_stripe(struct r5l_log *log,
					   struct stripe_head *sh)
{
	spin_lock(&log->no_space_stripes_lock);
	list_add_tail(&sh->log_list, &log->no_space_stripes);
	spin_unlock(&log->no_space_stripes_lock);
}

/*
 * running in raid5d, where reclaim could wait for raid5d too (when it flushes
 * data from log to raid disks), so we shouldn't wait for reclaim here
 */
int r5l_write_stripe(struct r5l_log *log, struct stripe_head *sh)
{
	struct r5conf *conf = sh->raid_conf;
	int write_disks = 0;
	int data_pages, parity_pages;
	int reserve;
	int i;
	int ret = 0;
	bool wake_reclaim = false;

	if (!log)
		return -EAGAIN;
	/* Don't support stripe batch */
	if (sh->log_io || !test_bit(R5_Wantwrite, &sh->dev[sh->pd_idx].flags) ||
	    test_bit(STRIPE_SYNCING, &sh->state)) {
		/* the stripe is written to log, we start writing it to raid */
		clear_bit(STRIPE_LOG_TRAPPED, &sh->state);
		return -EAGAIN;
	}

	WARN_ON(test_bit(STRIPE_R5C_CACHING, &sh->state));

	for (i = 0; i < sh->disks; i++) {
		void *addr;

		if (!test_bit(R5_Wantwrite, &sh->dev[i].flags) ||
		    test_bit(R5_InJournal, &sh->dev[i].flags))
			continue;

		write_disks++;
		/* checksum is already calculated in last run */
		if (test_bit(STRIPE_LOG_TRAPPED, &sh->state))
			continue;
		addr = kmap_atomic(sh->dev[i].page);
		sh->dev[i].log_checksum = crc32c_le(log->uuid_checksum,
						    addr, PAGE_SIZE);
		kunmap_atomic(addr);
	}
	parity_pages = 1 + !!(sh->qd_idx >= 0);
	data_pages = write_disks - parity_pages;

	set_bit(STRIPE_LOG_TRAPPED, &sh->state);
	/*
	 * The stripe must enter state machine again to finish the write, so
	 * don't delay.
	 */
	clear_bit(STRIPE_DELAYED, &sh->state);
	atomic_inc(&sh->count);

	mutex_lock(&log->io_mutex);
	/* meta + data */
	reserve = (1 + write_disks) << (PAGE_SHIFT - 9);

	if (log->r5c_journal_mode == R5C_JOURNAL_MODE_WRITE_THROUGH) {
		if (!r5l_has_free_space(log, reserve)) {
			r5l_add_no_space_stripe(log, sh);
			wake_reclaim = true;
		} else {
			ret = r5l_log_stripe(log, sh, data_pages, parity_pages);
			if (ret) {
				spin_lock_irq(&log->io_list_lock);
				list_add_tail(&sh->log_list,
					      &log->no_mem_stripes);
				spin_unlock_irq(&log->io_list_lock);
			}
		}
	} else {  /* R5C_JOURNAL_MODE_WRITE_BACK */
		/*
		 * log space critical, do not process stripes that are
		 * not in cache yet (sh->log_start == MaxSector).
		 */
		if (test_bit(R5C_LOG_CRITICAL, &conf->cache_state) &&
		    sh->log_start == MaxSector) {
			r5l_add_no_space_stripe(log, sh);
			wake_reclaim = true;
			reserve = 0;
		} else if (!r5l_has_free_space(log, reserve)) {
			if (sh->log_start == log->last_checkpoint)
				BUG();
			else
				r5l_add_no_space_stripe(log, sh);
		} else {
			ret = r5l_log_stripe(log, sh, data_pages, parity_pages);
			if (ret) {
				spin_lock_irq(&log->io_list_lock);
				list_add_tail(&sh->log_list,
					      &log->no_mem_stripes);
				spin_unlock_irq(&log->io_list_lock);
			}
		}
	}

	mutex_unlock(&log->io_mutex);
	if (wake_reclaim)
		r5l_wake_reclaim(log, reserve);
	return 0;
}

void r5l_write_stripe_run(struct r5l_log *log)
{
	if (!log)
		return;
	mutex_lock(&log->io_mutex);
	r5l_submit_current_io(log);
	mutex_unlock(&log->io_mutex);
}

int r5l_handle_flush_request(struct r5l_log *log, struct bio *bio)
{
	if (log->r5c_journal_mode == R5C_JOURNAL_MODE_WRITE_THROUGH) {
		/*
		 * in write through (journal only)
		 * we flush log disk cache first, then write stripe data to
		 * raid disks. So if bio is finished, the log disk cache is
		 * flushed already. The recovery guarantees we can recovery
		 * the bio from log disk, so we don't need to flush again
		 */
		if (bio->bi_iter.bi_size == 0) {
			bio_endio(bio);
			return 0;
		}
		bio->bi_opf &= ~REQ_PREFLUSH;
	} else {
		/* write back (with cache) */
		if (bio->bi_iter.bi_size == 0) {
			mutex_lock(&log->io_mutex);
			r5l_get_meta(log, 0);
			bio_list_add(&log->current_io->flush_barriers, bio);
			log->current_io->has_flush = 1;
			log->current_io->has_null_flush = 1;
			atomic_inc(&log->current_io->pending_stripe);
			r5l_submit_current_io(log);
			mutex_unlock(&log->io_mutex);
			return 0;
		}
	}
	return -EAGAIN;
}

/* This will run after log space is reclaimed */
static void r5l_run_no_space_stripes(struct r5l_log *log)
{
	struct stripe_head *sh;

	spin_lock(&log->no_space_stripes_lock);
	while (!list_empty(&log->no_space_stripes)) {
		sh = list_first_entry(&log->no_space_stripes,
				      struct stripe_head, log_list);
		list_del_init(&sh->log_list);
		set_bit(STRIPE_HANDLE, &sh->state);
		raid5_release_stripe(sh);
	}
	spin_unlock(&log->no_space_stripes_lock);
}

/*
 * calculate new last_checkpoint
 * for write through mode, returns log->next_checkpoint
 * for write back, returns log_start of first sh in stripe_in_journal_list
 */
static sector_t r5c_calculate_new_cp(struct r5conf *conf)
{
	struct stripe_head *sh;
	struct r5l_log *log = conf->log;
	sector_t new_cp;
	unsigned long flags;

	if (log->r5c_journal_mode == R5C_JOURNAL_MODE_WRITE_THROUGH)
		return log->next_checkpoint;

	spin_lock_irqsave(&log->stripe_in_journal_lock, flags);
	if (list_empty(&conf->log->stripe_in_journal_list)) {
		/* all stripes flushed */
		spin_unlock_irqrestore(&log->stripe_in_journal_lock, flags);
		return log->next_checkpoint;
	}
	sh = list_first_entry(&conf->log->stripe_in_journal_list,
			      struct stripe_head, r5c);
	new_cp = sh->log_start;
	spin_unlock_irqrestore(&log->stripe_in_journal_lock, flags);
	return new_cp;
}

static sector_t r5l_reclaimable_space(struct r5l_log *log)
{
	struct r5conf *conf = log->rdev->mddev->private;

	return r5l_ring_distance(log, log->last_checkpoint,
				 r5c_calculate_new_cp(conf));
}

static void r5l_run_no_mem_stripe(struct r5l_log *log)
{
	struct stripe_head *sh;

	lockdep_assert_held(&log->io_list_lock);

	if (!list_empty(&log->no_mem_stripes)) {
		sh = list_first_entry(&log->no_mem_stripes,
				      struct stripe_head, log_list);
		list_del_init(&sh->log_list);
		set_bit(STRIPE_HANDLE, &sh->state);
		raid5_release_stripe(sh);
	}
}

static bool r5l_complete_finished_ios(struct r5l_log *log)
{
	struct r5l_io_unit *io, *next;
	bool found = false;

	lockdep_assert_held(&log->io_list_lock);

	list_for_each_entry_safe(io, next, &log->finished_ios, log_sibling) {
		/* don't change list order */
		if (io->state < IO_UNIT_STRIPE_END)
			break;

		log->next_checkpoint = io->log_start;

		list_del(&io->log_sibling);
		mempool_free(io, &log->io_pool);
		r5l_run_no_mem_stripe(log);

		found = true;
	}

	return found;
}

static void __r5l_stripe_write_finished(struct r5l_io_unit *io)
{
	struct r5l_log *log = io->log;
	struct r5conf *conf = log->rdev->mddev->private;
	unsigned long flags;

	spin_lock_irqsave(&log->io_list_lock, flags);
	__r5l_set_io_unit_state(io, IO_UNIT_STRIPE_END);

	if (!r5l_complete_finished_ios(log)) {
		spin_unlock_irqrestore(&log->io_list_lock, flags);
		return;
	}

	if (r5l_reclaimable_space(log) > log->max_free_space ||
	    test_bit(R5C_LOG_TIGHT, &conf->cache_state))
		r5l_wake_reclaim(log, 0);

	spin_unlock_irqrestore(&log->io_list_lock, flags);
	wake_up(&log->iounit_wait);
}

void r5l_stripe_write_finished(struct stripe_head *sh)
{
	struct r5l_io_unit *io;

	io = sh->log_io;
	sh->log_io = NULL;

	if (io && atomic_dec_and_test(&io->pending_stripe))
		__r5l_stripe_write_finished(io);
}

static void r5l_log_flush_endio(struct bio *bio)
{
	struct r5l_log *log = container_of(bio, struct r5l_log,
		flush_bio);
	unsigned long flags;
	struct r5l_io_unit *io;

	if (bio->bi_status)
		md_error(log->rdev->mddev, log->rdev);
	bio_uninit(bio);

	spin_lock_irqsave(&log->io_list_lock, flags);
	list_for_each_entry(io, &log->flushing_ios, log_sibling)
		r5l_io_run_stripes(io);
	list_splice_tail_init(&log->flushing_ios, &log->finished_ios);
	spin_unlock_irqrestore(&log->io_list_lock, flags);
}

/*
 * Starting dispatch IO to raid.
 * io_unit(meta) consists of a log. There is one situation we want to avoid. A
 * broken meta in the middle of a log causes recovery can't find meta at the
 * head of log. If operations require meta at the head persistent in log, we
 * must make sure meta before it persistent in log too. A case is:
 *
 * stripe data/parity is in log, we start write stripe to raid disks. stripe
 * data/parity must be persistent in log before we do the write to raid disks.
 *
 * The solution is we restrictly maintain io_unit list order. In this case, we
 * only write stripes of an io_unit to raid disks till the io_unit is the first
 * one whose data/parity is in log.
 */
void r5l_flush_stripe_to_raid(struct r5l_log *log)
{
	bool do_flush;

	if (!log || !log->need_cache_flush)
		return;

	spin_lock_irq(&log->io_list_lock);
	/* flush bio is running */
	if (!list_empty(&log->flushing_ios)) {
		spin_unlock_irq(&log->io_list_lock);
		return;
	}
	list_splice_tail_init(&log->io_end_ios, &log->flushing_ios);
	do_flush = !list_empty(&log->flushing_ios);
	spin_unlock_irq(&log->io_list_lock);

	if (!do_flush)
		return;
	bio_init(&log->flush_bio, log->rdev->bdev, NULL, 0,
		  REQ_OP_WRITE | REQ_PREFLUSH);
	log->flush_bio.bi_end_io = r5l_log_flush_endio;
	submit_bio(&log->flush_bio);
}

static void r5l_write_super(struct r5l_log *log, sector_t cp);
static void r5l_write_super_and_discard_space(struct r5l_log *log,
	sector_t end)
{
	struct block_device *bdev = log->rdev->bdev;
	struct mddev *mddev;

	r5l_write_super(log, end);

	if (!bdev_max_discard_sectors(bdev))
		return;

	mddev = log->rdev->mddev;
	/*
	 * Discard could zero data, so before discard we must make sure
	 * superblock is updated to new log tail. Updating superblock (either
	 * directly call md_update_sb() or depend on md thread) must hold
	 * reconfig mutex. On the other hand, raid5_quiesce is called with
	 * reconfig_mutex hold. The first step of raid5_quiesce() is waiting
	 * for all IO finish, hence waiting for reclaim thread, while reclaim
	 * thread is calling this function and waiting for reconfig mutex. So
	 * there is a deadlock. We workaround this issue with a trylock.
	 * FIXME: we could miss discard if we can't take reconfig mutex
	 */
	set_mask_bits(&mddev->sb_flags, 0,
		BIT(MD_SB_CHANGE_DEVS) | BIT(MD_SB_CHANGE_PENDING));
	if (!mddev_trylock(mddev))
		return;
	md_update_sb(mddev, 1);
	mddev_unlock(mddev);

	/* discard IO error really doesn't matter, ignore it */
	if (log->last_checkpoint < end) {
		blkdev_issue_discard(bdev,
				log->last_checkpoint + log->rdev->data_offset,
				end - log->last_checkpoint, GFP_NOIO);
	} else {
		blkdev_issue_discard(bdev,
				log->last_checkpoint + log->rdev->data_offset,
				log->device_size - log->last_checkpoint,
				GFP_NOIO);
		blkdev_issue_discard(bdev, log->rdev->data_offset, end,
				GFP_NOIO);
	}
}

/*
 * r5c_flush_stripe moves stripe from cached list to handle_list. When called,
 * the stripe must be on r5c_cached_full_stripes or r5c_cached_partial_stripes.
 *
 * must hold conf->device_lock
 */
static void r5c_flush_stripe(struct r5conf *conf, struct stripe_head *sh)
{
	BUG_ON(list_empty(&sh->lru));
	BUG_ON(!test_bit(STRIPE_R5C_CACHING, &sh->state));
	BUG_ON(test_bit(STRIPE_HANDLE, &sh->state));

	/*
	 * The stripe is not ON_RELEASE_LIST, so it is safe to call
	 * raid5_release_stripe() while holding conf->device_lock
	 */
	BUG_ON(test_bit(STRIPE_ON_RELEASE_LIST, &sh->state));
	lockdep_assert_held(&conf->device_lock);

	list_del_init(&sh->lru);
	atomic_inc(&sh->count);

	set_bit(STRIPE_HANDLE, &sh->state);
	atomic_inc(&conf->active_stripes);
	r5c_make_stripe_write_out(sh);

	if (test_bit(STRIPE_R5C_PARTIAL_STRIPE, &sh->state))
		atomic_inc(&conf->r5c_flushing_partial_stripes);
	else
		atomic_inc(&conf->r5c_flushing_full_stripes);
	raid5_release_stripe(sh);
}

/*
 * if num == 0, flush all full stripes
 * if num > 0, flush all full stripes. If less than num full stripes are
 *             flushed, flush some partial stripes until totally num stripes are
 *             flushed or there is no more cached stripes.
 */
void r5c_flush_cache(struct r5conf *conf, int num)
{
	int count;
	struct stripe_head *sh, *next;

	lockdep_assert_held(&conf->device_lock);
	if (!conf->log)
		return;

	count = 0;
	list_for_each_entry_safe(sh, next, &conf->r5c_full_stripe_list, lru) {
		r5c_flush_stripe(conf, sh);
		count++;
	}

	if (count >= num)
		return;
	list_for_each_entry_safe(sh, next,
				 &conf->r5c_partial_stripe_list, lru) {
		r5c_flush_stripe(conf, sh);
		if (++count >= num)
			break;
	}
}

static void r5c_do_reclaim(struct r5conf *conf)
{
	struct r5l_log *log = conf->log;
	struct stripe_head *sh;
	int count = 0;
	unsigned long flags;
	int total_cached;
	int stripes_to_flush;
	int flushing_partial, flushing_full;

	if (!r5c_is_writeback(log))
		return;

	flushing_partial = atomic_read(&conf->r5c_flushing_partial_stripes);
	flushing_full = atomic_read(&conf->r5c_flushing_full_stripes);
	total_cached = atomic_read(&conf->r5c_cached_partial_stripes) +
		atomic_read(&conf->r5c_cached_full_stripes) -
		flushing_full - flushing_partial;

	if (total_cached > conf->min_nr_stripes * 3 / 4 ||
	    atomic_read(&conf->empty_inactive_list_nr) > 0)
		/*
		 * if stripe cache pressure high, flush all full stripes and
		 * some partial stripes
		 */
		stripes_to_flush = R5C_RECLAIM_STRIPE_GROUP;
	else if (total_cached > conf->min_nr_stripes * 1 / 2 ||
		 atomic_read(&conf->r5c_cached_full_stripes) - flushing_full >
		 R5C_FULL_STRIPE_FLUSH_BATCH(conf))
		/*
		 * if stripe cache pressure moderate, or if there is many full
		 * stripes,flush all full stripes
		 */
		stripes_to_flush = 0;
	else
		/* no need to flush */
		stripes_to_flush = -1;

	if (stripes_to_flush >= 0) {
		spin_lock_irqsave(&conf->device_lock, flags);
		r5c_flush_cache(conf, stripes_to_flush);
		spin_unlock_irqrestore(&conf->device_lock, flags);
	}

	/* if log space is tight, flush stripes on stripe_in_journal_list */
	if (test_bit(R5C_LOG_TIGHT, &conf->cache_state)) {
		spin_lock_irqsave(&log->stripe_in_journal_lock, flags);
		spin_lock(&conf->device_lock);
		list_for_each_entry(sh, &log->stripe_in_journal_list, r5c) {
			/*
			 * stripes on stripe_in_journal_list could be in any
			 * state of the stripe_cache state machine. In this
			 * case, we only want to flush stripe on
			 * r5c_cached_full/partial_stripes. The following
			 * condition makes sure the stripe is on one of the
			 * two lists.
			 */
			if (!list_empty(&sh->lru) &&
			    !test_bit(STRIPE_HANDLE, &sh->state) &&
			    atomic_read(&sh->count) == 0) {
				r5c_flush_stripe(conf, sh);
				if (count++ >= R5C_RECLAIM_STRIPE_GROUP)
					break;
			}
		}
		spin_unlock(&conf->device_lock);
		spin_unlock_irqrestore(&log->stripe_in_journal_lock, flags);
	}

	if (!test_bit(R5C_LOG_CRITICAL, &conf->cache_state))
		r5l_run_no_space_stripes(log);

	md_wakeup_thread(conf->mddev->thread);
}

static void r5l_do_reclaim(struct r5l_log *log)
{
	struct r5conf *conf = log->rdev->mddev->private;
	sector_t reclaim_target = xchg(&log->reclaim_target, 0);
	sector_t reclaimable;
	sector_t next_checkpoint;
	bool write_super;

	spin_lock_irq(&log->io_list_lock);
	write_super = r5l_reclaimable_space(log) > log->max_free_space ||
		reclaim_target != 0 || !list_empty(&log->no_space_stripes);
	/*
	 * move proper io_unit to reclaim list. We should not change the order.
	 * reclaimable/unreclaimable io_unit can be mixed in the list, we
	 * shouldn't reuse space of an unreclaimable io_unit
	 */
	while (1) {
		reclaimable = r5l_reclaimable_space(log);
		if (reclaimable >= reclaim_target ||
		    (list_empty(&log->running_ios) &&
		     list_empty(&log->io_end_ios) &&
		     list_empty(&log->flushing_ios) &&
		     list_empty(&log->finished_ios)))
			break;

		md_wakeup_thread(log->rdev->mddev->thread);
		wait_event_lock_irq(log->iounit_wait,
				    r5l_reclaimable_space(log) > reclaimable,
				    log->io_list_lock);
	}

	next_checkpoint = r5c_calculate_new_cp(conf);
	spin_unlock_irq(&log->io_list_lock);

	if (reclaimable == 0 || !write_super)
		return;

	/*
	 * write_super will flush cache of each raid disk. We must write super
	 * here, because the log area might be reused soon and we don't want to
	 * confuse recovery
	 */
	r5l_write_super_and_discard_space(log, next_checkpoint);

	mutex_lock(&log->io_mutex);
	log->last_checkpoint = next_checkpoint;
	r5c_update_log_state(log);
	mutex_unlock(&log->io_mutex);

	r5l_run_no_space_stripes(log);
}

static void r5l_reclaim_thread(struct md_thread *thread)
{
	struct mddev *mddev = thread->mddev;
	struct r5conf *conf = mddev->private;
	struct r5l_log *log = conf->log;

	if (!log)
		return;
	r5c_do_reclaim(conf);
	r5l_do_reclaim(log);
}

void r5l_wake_reclaim(struct r5l_log *log, sector_t space)
{
	unsigned long target;
	unsigned long new = (unsigned long)space; /* overflow in theory */

	if (!log)
		return;
	do {
		target = log->reclaim_target;
		if (new < target)
			return;
	} while (cmpxchg(&log->reclaim_target, target, new) != target);
	md_wakeup_thread(log->reclaim_thread);
}

void r5l_quiesce(struct r5l_log *log, int quiesce)
{
	struct mddev *mddev;

	if (quiesce) {
		/* make sure r5l_write_super_and_discard_space exits */
		mddev = log->rdev->mddev;
		wake_up(&mddev->sb_wait);
		kthread_park(log->reclaim_thread->tsk);
		r5l_wake_reclaim(log, MaxSector);
		r5l_do_reclaim(log);
	} else
		kthread_unpark(log->reclaim_thread->tsk);
}

bool r5l_log_disk_error(struct r5conf *conf)
{
	struct r5l_log *log = conf->log;

	/* don't allow write if journal disk is missing */
	if (!log)
		return test_bit(MD_HAS_JOURNAL, &conf->mddev->flags);
	else
		return test_bit(Faulty, &log->rdev->flags);
}

#define R5L_RECOVERY_PAGE_POOL_SIZE 256

struct r5l_recovery_ctx {
	struct page *meta_page;		/* current meta */
	sector_t meta_total_blocks;	/* total size of current meta and data */
	sector_t pos;			/* recovery position */
	u64 seq;			/* recovery position seq */
	int data_parity_stripes;	/* number of data_parity stripes */
	int data_only_stripes;		/* number of data_only stripes */
	struct list_head cached_list;

	/*
	 * read ahead page pool (ra_pool)
	 * in recovery, log is read sequentially. It is not efficient to
	 * read every page with sync_page_io(). The read ahead page pool
	 * reads multiple pages with one IO, so further log read can
	 * just copy data from the pool.
	 */
	struct page *ra_pool[R5L_RECOVERY_PAGE_POOL_SIZE];
	struct bio_vec ra_bvec[R5L_RECOVERY_PAGE_POOL_SIZE];
	sector_t pool_offset;	/* offset of first page in the pool */
	int total_pages;	/* total allocated pages */
	int valid_pages;	/* pages with valid data */
};

static int r5l_recovery_allocate_ra_pool(struct r5l_log *log,
					    struct r5l_recovery_ctx *ctx)
{
	struct page *page;

	ctx->valid_pages = 0;
	ctx->total_pages = 0;
	while (ctx->total_pages < R5L_RECOVERY_PAGE_POOL_SIZE) {
		page = alloc_page(GFP_KERNEL);

		if (!page)
			break;
		ctx->ra_pool[ctx->total_pages] = page;
		ctx->total_pages += 1;
	}

	if (ctx->total_pages == 0)
		return -ENOMEM;

	ctx->pool_offset = 0;
	return 0;
}

static void r5l_recovery_free_ra_pool(struct r5l_log *log,
					struct r5l_recovery_ctx *ctx)
{
	int i;

	for (i = 0; i < ctx->total_pages; ++i)
		put_page(ctx->ra_pool[i]);
}

/*
 * fetch ctx->valid_pages pages from offset
 * In normal cases, ctx->valid_pages == ctx->total_pages after the call.
 * However, if the offset is close to the end of the journal device,
 * ctx->valid_pages could be smaller than ctx->total_pages
 */
static int r5l_recovery_fetch_ra_pool(struct r5l_log *log,
				      struct r5l_recovery_ctx *ctx,
				      sector_t offset)
{
	struct bio bio;
	int ret;

	bio_init(&bio, log->rdev->bdev, ctx->ra_bvec,
		 R5L_RECOVERY_PAGE_POOL_SIZE, REQ_OP_READ);
	bio.bi_iter.bi_sector = log->rdev->data_offset + offset;

	ctx->valid_pages = 0;
	ctx->pool_offset = offset;

	while (ctx->valid_pages < ctx->total_pages) {
		__bio_add_page(&bio, ctx->ra_pool[ctx->valid_pages], PAGE_SIZE,
			       0);
		ctx->valid_pages += 1;

		offset = r5l_ring_add(log, offset, BLOCK_SECTORS);

		if (offset == 0)  /* reached end of the device */
			break;
	}

	ret = submit_bio_wait(&bio);
	bio_uninit(&bio);
	return ret;
}

/*
 * try read a page from the read ahead page pool, if the page is not in the
 * pool, call r5l_recovery_fetch_ra_pool
 */
static int r5l_recovery_read_page(struct r5l_log *log,
				  struct r5l_recovery_ctx *ctx,
				  struct page *page,
				  sector_t offset)
{
	int ret;

	if (offset < ctx->pool_offset ||
	    offset >= ctx->pool_offset + ctx->valid_pages * BLOCK_SECTORS) {
		ret = r5l_recovery_fetch_ra_pool(log, ctx, offset);
		if (ret)
			return ret;
	}

	BUG_ON(offset < ctx->pool_offset ||
	       offset >= ctx->pool_offset + ctx->valid_pages * BLOCK_SECTORS);

	memcpy(page_address(page),
	       page_address(ctx->ra_pool[(offset - ctx->pool_offset) >>
					 BLOCK_SECTOR_SHIFT]),
	       PAGE_SIZE);
	return 0;
}

static int r5l_recovery_read_meta_block(struct r5l_log *log,
					struct r5l_recovery_ctx *ctx)
{
	struct page *page = ctx->meta_page;
	struct r5l_meta_block *mb;
	u32 crc, stored_crc;
	int ret;

	ret = r5l_recovery_read_page(log, ctx, page, ctx->pos);
	if (ret != 0)
		return ret;

	mb = page_address(page);
	stored_crc = le32_to_cpu(mb->checksum);
	mb->checksum = 0;

	if (le32_to_cpu(mb->magic) != R5LOG_MAGIC ||
	    le64_to_cpu(mb->seq) != ctx->seq ||
	    mb->version != R5LOG_VERSION ||
	    le64_to_cpu(mb->position) != ctx->pos)
		return -EINVAL;

	crc = crc32c_le(log->uuid_checksum, mb, PAGE_SIZE);
	if (stored_crc != crc)
		return -EINVAL;

	if (le32_to_cpu(mb->meta_size) > PAGE_SIZE)
		return -EINVAL;

	ctx->meta_total_blocks = BLOCK_SECTORS;

	return 0;
}

static void
r5l_recovery_create_empty_meta_block(struct r5l_log *log,
				     struct page *page,
				     sector_t pos, u64 seq)
{
	struct r5l_meta_block *mb;

	mb = page_address(page);
	clear_page(mb);
	mb->magic = cpu_to_le32(R5LOG_MAGIC);
	mb->version = R5LOG_VERSION;
	mb->meta_size = cpu_to_le32(sizeof(struct r5l_meta_block));
	mb->seq = cpu_to_le64(seq);
	mb->position = cpu_to_le64(pos);
}

static int r5l_log_write_empty_meta_block(struct r5l_log *log, sector_t pos,
					  u64 seq)
{
	struct page *page;
	struct r5l_meta_block *mb;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	r5l_recovery_create_empty_meta_block(log, page, pos, seq);
	mb = page_address(page);
	mb->checksum = cpu_to_le32(crc32c_le(log->uuid_checksum,
					     mb, PAGE_SIZE));
	if (!sync_page_io(log->rdev, pos, PAGE_SIZE, page, REQ_OP_WRITE |
			  REQ_SYNC | REQ_FUA, false)) {
		__free_page(page);
		return -EIO;
	}
	__free_page(page);
	return 0;
}

/*
 * r5l_recovery_load_data and r5l_recovery_load_parity uses flag R5_Wantwrite
 * to mark valid (potentially not flushed) data in the journal.
 *
 * We already verified checksum in r5l_recovery_verify_data_checksum_for_mb,
 * so there should not be any mismatch here.
 */
static void r5l_recovery_load_data(struct r5l_log *log,
				   struct stripe_head *sh,
				   struct r5l_recovery_ctx *ctx,
				   struct r5l_payload_data_parity *payload,
				   sector_t log_offset)
{
	struct mddev *mddev = log->rdev->mddev;
	struct r5conf *conf = mddev->private;
	int dd_idx;

	raid5_compute_sector(conf,
			     le64_to_cpu(payload->location), 0,
			     &dd_idx, sh);
	r5l_recovery_read_page(log, ctx, sh->dev[dd_idx].page, log_offset);
	sh->dev[dd_idx].log_checksum =
		le32_to_cpu(payload->checksum[0]);
	ctx->meta_total_blocks += BLOCK_SECTORS;

	set_bit(R5_Wantwrite, &sh->dev[dd_idx].flags);
	set_bit(STRIPE_R5C_CACHING, &sh->state);
}

static void r5l_recovery_load_parity(struct r5l_log *log,
				     struct stripe_head *sh,
				     struct r5l_recovery_ctx *ctx,
				     struct r5l_payload_data_parity *payload,
				     sector_t log_offset)
{
	struct mddev *mddev = log->rdev->mddev;
	struct r5conf *conf = mddev->private;

	ctx->meta_total_blocks += BLOCK_SECTORS * conf->max_degraded;
	r5l_recovery_read_page(log, ctx, sh->dev[sh->pd_idx].page, log_offset);
	sh->dev[sh->pd_idx].log_checksum =
		le32_to_cpu(payload->checksum[0]);
	set_bit(R5_Wantwrite, &sh->dev[sh->pd_idx].flags);

	if (sh->qd_idx >= 0) {
		r5l_recovery_read_page(
			log, ctx, sh->dev[sh->qd_idx].page,
			r5l_ring_add(log, log_offset, BLOCK_SECTORS));
		sh->dev[sh->qd_idx].log_checksum =
			le32_to_cpu(payload->checksum[1]);
		set_bit(R5_Wantwrite, &sh->dev[sh->qd_idx].flags);
	}
	clear_bit(STRIPE_R5C_CACHING, &sh->state);
}

static void r5l_recovery_reset_stripe(struct stripe_head *sh)
{
	int i;

	sh->state = 0;
	sh->log_start = MaxSector;
	for (i = sh->disks; i--; )
		sh->dev[i].flags = 0;
}

static void
r5l_recovery_replay_one_stripe(struct r5conf *conf,
			       struct stripe_head *sh,
			       struct r5l_recovery_ctx *ctx)
{
	struct md_rdev *rdev, *rrdev;
	int disk_index;
	int data_count = 0;

	for (disk_index = 0; disk_index < sh->disks; disk_index++) {
		if (!test_bit(R5_Wantwrite, &sh->dev[disk_index].flags))
			continue;
		if (disk_index == sh->qd_idx || disk_index == sh->pd_idx)
			continue;
		data_count++;
	}

	/*
	 * stripes that only have parity must have been flushed
	 * before the crash that we are now recovering from, so
	 * there is nothing more to recovery.
	 */
	if (data_count == 0)
		goto out;

	for (disk_index = 0; disk_index < sh->disks; disk_index++) {
		if (!test_bit(R5_Wantwrite, &sh->dev[disk_index].flags))
			continue;

		/* in case device is broken */
		rcu_read_lock();
		rdev = rcu_dereference(conf->disks[disk_index].rdev);
		if (rdev) {
			atomic_inc(&rdev->nr_pending);
			rcu_read_unlock();
			sync_page_io(rdev, sh->sector, PAGE_SIZE,
				     sh->dev[disk_index].page, REQ_OP_WRITE,
				     false);
			rdev_dec_pending(rdev, rdev->mddev);
			rcu_read_lock();
		}
		rrdev = rcu_dereference(conf->disks[disk_index].replacement);
		if (rrdev) {
			atomic_inc(&rrdev->nr_pending);
			rcu_read_unlock();
			sync_page_io(rrdev, sh->sector, PAGE_SIZE,
				     sh->dev[disk_index].page, REQ_OP_WRITE,
				     false);
			rdev_dec_pending(rrdev, rrdev->mddev);
			rcu_read_lock();
		}
		rcu_read_unlock();
	}
	ctx->data_parity_stripes++;
out:
	r5l_recovery_reset_stripe(sh);
}

static struct stripe_head *
r5c_recovery_alloc_stripe(
		struct r5conf *conf,
		sector_t stripe_sect,
		int noblock)
{
	struct stripe_head *sh;

	sh = raid5_get_active_stripe(conf, NULL, stripe_sect,
				     noblock ? R5_GAS_NOBLOCK : 0);
	if (!sh)
		return NULL;  /* no more stripe available */

	r5l_recovery_reset_stripe(sh);

	return sh;
}

static struct stripe_head *
r5c_recovery_lookup_stripe(struct list_head *list, sector_t sect)
{
	struct stripe_head *sh;

	list_for_each_entry(sh, list, lru)
		if (sh->sector == sect)
			return sh;
	return NULL;
}

static void
r5c_recovery_drop_stripes(struct list_head *cached_stripe_list,
			  struct r5l_recovery_ctx *ctx)
{
	struct stripe_head *sh, *next;

	list_for_each_entry_safe(sh, next, cached_stripe_list, lru) {
		r5l_recovery_reset_stripe(sh);
		list_del_init(&sh->lru);
		raid5_release_stripe(sh);
	}
}

static void
r5c_recovery_replay_stripes(struct list_head *cached_stripe_list,
			    struct r5l_recovery_ctx *ctx)
{
	struct stripe_head *sh, *next;

	list_for_each_entry_safe(sh, next, cached_stripe_list, lru)
		if (!test_bit(STRIPE_R5C_CACHING, &sh->state)) {
			r5l_recovery_replay_one_stripe(sh->raid_conf, sh, ctx);
			list_del_init(&sh->lru);
			raid5_release_stripe(sh);
		}
}

/* if matches return 0; otherwise return -EINVAL */
static int
r5l_recovery_verify_data_checksum(struct r5l_log *log,
				  struct r5l_recovery_ctx *ctx,
				  struct page *page,
				  sector_t log_offset, __le32 log_checksum)
{
	void *addr;
	u32 checksum;

	r5l_recovery_read_page(log, ctx, page, log_offset);
	addr = kmap_atomic(page);
	checksum = crc32c_le(log->uuid_checksum, addr, PAGE_SIZE);
	kunmap_atomic(addr);
	return (le32_to_cpu(log_checksum) == checksum) ? 0 : -EINVAL;
}

/*
 * before loading data to stripe cache, we need verify checksum for all data,
 * if there is mismatch for any data page, we drop all data in the mata block
 */
static int
r5l_recovery_verify_data_checksum_for_mb(struct r5l_log *log,
					 struct r5l_recovery_ctx *ctx)
{
	struct mddev *mddev = log->rdev->mddev;
	struct r5conf *conf = mddev->private;
	struct r5l_meta_block *mb = page_address(ctx->meta_page);
	sector_t mb_offset = sizeof(struct r5l_meta_block);
	sector_t log_offset = r5l_ring_add(log, ctx->pos, BLOCK_SECTORS);
	struct page *page;
	struct r5l_payload_data_parity *payload;
	struct r5l_payload_flush *payload_flush;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	while (mb_offset < le32_to_cpu(mb->meta_size)) {
		payload = (void *)mb + mb_offset;
		payload_flush = (void *)mb + mb_offset;

		if (le16_to_cpu(payload->header.type) == R5LOG_PAYLOAD_DATA) {
			if (r5l_recovery_verify_data_checksum(
				    log, ctx, page, log_offset,
				    payload->checksum[0]) < 0)
				goto mismatch;
		} else if (le16_to_cpu(payload->header.type) == R5LOG_PAYLOAD_PARITY) {
			if (r5l_recovery_verify_data_checksum(
				    log, ctx, page, log_offset,
				    payload->checksum[0]) < 0)
				goto mismatch;
			if (conf->max_degraded == 2 && /* q for RAID 6 */
			    r5l_recovery_verify_data_checksum(
				    log, ctx, page,
				    r5l_ring_add(log, log_offset,
						 BLOCK_SECTORS),
				    payload->checksum[1]) < 0)
				goto mismatch;
		} else if (le16_to_cpu(payload->header.type) == R5LOG_PAYLOAD_FLUSH) {
			/* nothing to do for R5LOG_PAYLOAD_FLUSH here */
		} else /* not R5LOG_PAYLOAD_DATA/PARITY/FLUSH */
			goto mismatch;

		if (le16_to_cpu(payload->header.type) == R5LOG_PAYLOAD_FLUSH) {
			mb_offset += sizeof(struct r5l_payload_flush) +
				le32_to_cpu(payload_flush->size);
		} else {
			/* DATA or PARITY payload */
			log_offset = r5l_ring_add(log, log_offset,
						  le32_to_cpu(payload->size));
			mb_offset += sizeof(struct r5l_payload_data_parity) +
				sizeof(__le32) *
				(le32_to_cpu(payload->size) >> (PAGE_SHIFT - 9));
		}

	}

	put_page(page);
	return 0;

mismatch:
	put_page(page);
	return -EINVAL;
}

/*
 * Analyze all data/parity pages in one meta block
 * Returns:
 * 0 for success
 * -EINVAL for unknown playload type
 * -EAGAIN for checksum mismatch of data page
 * -ENOMEM for run out of memory (alloc_page failed or run out of stripes)
 */
static int
r5c_recovery_analyze_meta_block(struct r5l_log *log,
				struct r5l_recovery_ctx *ctx,
				struct list_head *cached_stripe_list)
{
	struct mddev *mddev = log->rdev->mddev;
	struct r5conf *conf = mddev->private;
	struct r5l_meta_block *mb;
	struct r5l_payload_data_parity *payload;
	struct r5l_payload_flush *payload_flush;
	int mb_offset;
	sector_t log_offset;
	sector_t stripe_sect;
	struct stripe_head *sh;
	int ret;

	/*
	 * for mismatch in data blocks, we will drop all data in this mb, but
	 * we will still read next mb for other data with FLUSH flag, as
	 * io_unit could finish out of order.
	 */
	ret = r5l_recovery_verify_data_checksum_for_mb(log, ctx);
	if (ret == -EINVAL)
		return -EAGAIN;
	else if (ret)
		return ret;   /* -ENOMEM duo to alloc_page() failed */

	mb = page_address(ctx->meta_page);
	mb_offset = sizeof(struct r5l_meta_block);
	log_offset = r5l_ring_add(log, ctx->pos, BLOCK_SECTORS);

	while (mb_offset < le32_to_cpu(mb->meta_size)) {
		int dd;

		payload = (void *)mb + mb_offset;
		payload_flush = (void *)mb + mb_offset;

		if (le16_to_cpu(payload->header.type) == R5LOG_PAYLOAD_FLUSH) {
			int i, count;

			count = le32_to_cpu(payload_flush->size) / sizeof(__le64);
			for (i = 0; i < count; ++i) {
				stripe_sect = le64_to_cpu(payload_flush->flush_stripes[i]);
				sh = r5c_recovery_lookup_stripe(cached_stripe_list,
								stripe_sect);
				if (sh) {
					WARN_ON(test_bit(STRIPE_R5C_CACHING, &sh->state));
					r5l_recovery_reset_stripe(sh);
					list_del_init(&sh->lru);
					raid5_release_stripe(sh);
				}
			}

			mb_offset += sizeof(struct r5l_payload_flush) +
				le32_to_cpu(payload_flush->size);
			continue;
		}

		/* DATA or PARITY payload */
		stripe_sect = (le16_to_cpu(payload->header.type) == R5LOG_PAYLOAD_DATA) ?
			raid5_compute_sector(
				conf, le64_to_cpu(payload->location), 0, &dd,
				NULL)
			: le64_to_cpu(payload->location);

		sh = r5c_recovery_lookup_stripe(cached_stripe_list,
						stripe_sect);

		if (!sh) {
			sh = r5c_recovery_alloc_stripe(conf, stripe_sect, 1);
			/*
			 * cannot get stripe from raid5_get_active_stripe
			 * try replay some stripes
			 */
			if (!sh) {
				r5c_recovery_replay_stripes(
					cached_stripe_list, ctx);
				sh = r5c_recovery_alloc_stripe(
					conf, stripe_sect, 1);
			}
			if (!sh) {
				int new_size = conf->min_nr_stripes * 2;
				pr_debug("md/raid:%s: Increasing stripe cache size to %d to recovery data on journal.\n",
					mdname(mddev),
					new_size);
				ret = raid5_set_cache_size(mddev, new_size);
				if (conf->min_nr_stripes <= new_size / 2) {
					pr_err("md/raid:%s: Cannot increase cache size, ret=%d, new_size=%d, min_nr_stripes=%d, max_nr_stripes=%d\n",
						mdname(mddev),
						ret,
						new_size,
						conf->min_nr_stripes,
						conf->max_nr_stripes);
					return -ENOMEM;
				}
				sh = r5c_recovery_alloc_stripe(
					conf, stripe_sect, 0);
			}
			if (!sh) {
				pr_err("md/raid:%s: Cannot get enough stripes due to memory pressure. Recovery failed.\n",
					mdname(mddev));
				return -ENOMEM;
			}
			list_add_tail(&sh->lru, cached_stripe_list);
		}

		if (le16_to_cpu(payload->header.type) == R5LOG_PAYLOAD_DATA) {
			if (!test_bit(STRIPE_R5C_CACHING, &sh->state) &&
			    test_bit(R5_Wantwrite, &sh->dev[sh->pd_idx].flags)) {
				r5l_recovery_replay_one_stripe(conf, sh, ctx);
				list_move_tail(&sh->lru, cached_stripe_list);
			}
			r5l_recovery_load_data(log, sh, ctx, payload,
					       log_offset);
		} else if (le16_to_cpu(payload->header.type) == R5LOG_PAYLOAD_PARITY)
			r5l_recovery_load_parity(log, sh, ctx, payload,
						 log_offset);
		else
			return -EINVAL;

		log_offset = r5l_ring_add(log, log_offset,
					  le32_to_cpu(payload->size));

		mb_offset += sizeof(struct r5l_payload_data_parity) +
			sizeof(__le32) *
			(le32_to_cpu(payload->size) >> (PAGE_SHIFT - 9));
	}

	return 0;
}

/*
 * Load the stripe into cache. The stripe will be written out later by
 * the stripe cache state machine.
 */
static void r5c_recovery_load_one_stripe(struct r5l_log *log,
					 struct stripe_head *sh)
{
	struct r5dev *dev;
	int i;

	for (i = sh->disks; i--; ) {
		dev = sh->dev + i;
		if (test_and_clear_bit(R5_Wantwrite, &dev->flags)) {
			set_bit(R5_InJournal, &dev->flags);
			set_bit(R5_UPTODATE, &dev->flags);
		}
	}
}

/*
 * Scan through the log for all to-be-flushed data
 *
 * For stripes with data and parity, namely Data-Parity stripe
 * (STRIPE_R5C_CACHING == 0), we simply replay all the writes.
 *
 * For stripes with only data, namely Data-Only stripe
 * (STRIPE_R5C_CACHING == 1), we load them to stripe cache state machine.
 *
 * For a stripe, if we see data after parity, we should discard all previous
 * data and parity for this stripe, as these data are already flushed to
 * the array.
 *
 * At the end of the scan, we return the new journal_tail, which points to
 * first data-only stripe on the journal device, or next invalid meta block.
 */
static int r5c_recovery_flush_log(struct r5l_log *log,
				  struct r5l_recovery_ctx *ctx)
{
	struct stripe_head *sh;
	int ret = 0;

	/* scan through the log */
	while (1) {
		if (r5l_recovery_read_meta_block(log, ctx))
			break;

		ret = r5c_recovery_analyze_meta_block(log, ctx,
						      &ctx->cached_list);
		/*
		 * -EAGAIN means mismatch in data block, in this case, we still
		 * try scan the next metablock
		 */
		if (ret && ret != -EAGAIN)
			break;   /* ret == -EINVAL or -ENOMEM */
		ctx->seq++;
		ctx->pos = r5l_ring_add(log, ctx->pos, ctx->meta_total_blocks);
	}

	if (ret == -ENOMEM) {
		r5c_recovery_drop_stripes(&ctx->cached_list, ctx);
		return ret;
	}

	/* replay data-parity stripes */
	r5c_recovery_replay_stripes(&ctx->cached_list, ctx);

	/* load data-only stripes to stripe cache */
	list_for_each_entry(sh, &ctx->cached_list, lru) {
		WARN_ON(!test_bit(STRIPE_R5C_CACHING, &sh->state));
		r5c_recovery_load_one_stripe(log, sh);
		ctx->data_only_stripes++;
	}

	return 0;
}

/*
 * we did a recovery. Now ctx.pos points to an invalid meta block. New
 * log will start here. but we can't let superblock point to last valid
 * meta block. The log might looks like:
 * | meta 1| meta 2| meta 3|
 * meta 1 is valid, meta 2 is invalid. meta 3 could be valid. If
 * superblock points to meta 1, we write a new valid meta 2n.  if crash
 * happens again, new recovery will start from meta 1. Since meta 2n is
 * valid now, recovery will think meta 3 is valid, which is wrong.
 * The solution is we create a new meta in meta2 with its seq == meta
 * 1's seq + 10000 and let superblock points to meta2. The same recovery
 * will not think meta 3 is a valid meta, because its seq doesn't match
 */

/*
 * Before recovery, the log looks like the following
 *
 *   ---------------------------------------------
 *   |           valid log        | invalid log  |
 *   ---------------------------------------------
 *   ^
 *   |- log->last_checkpoint
 *   |- log->last_cp_seq
 *
 * Now we scan through the log until we see invalid entry
 *
 *   ---------------------------------------------
 *   |           valid log        | invalid log  |
 *   ---------------------------------------------
 *   ^                            ^
 *   |- log->last_checkpoint      |- ctx->pos
 *   |- log->last_cp_seq          |- ctx->seq
 *
 * From this point, we need to increase seq number by 10 to avoid
 * confusing next recovery.
 *
 *   ---------------------------------------------
 *   |           valid log        | invalid log  |
 *   ---------------------------------------------
 *   ^                              ^
 *   |- log->last_checkpoint        |- ctx->pos+1
 *   |- log->last_cp_seq            |- ctx->seq+10001
 *
 * However, it is not safe to start the state machine yet, because data only
 * parities are not yet secured in RAID. To save these data only parities, we
 * rewrite them from seq+11.
 *
 *   -----------------------------------------------------------------
 *   |           valid log        | data only stripes | invalid log  |
 *   -----------------------------------------------------------------
 *   ^                                                ^
 *   |- log->last_checkpoint                          |- ctx->pos+n
 *   |- log->last_cp_seq                              |- ctx->seq+10000+n
 *
 * If failure happens again during this process, the recovery can safe start
 * again from log->last_checkpoint.
 *
 * Once data only stripes are rewritten to journal, we move log_tail
 *
 *   -----------------------------------------------------------------
 *   |     old log        |    data only stripes    | invalid log  |
 *   -----------------------------------------------------------------
 *                        ^                         ^
 *                        |- log->last_checkpoint   |- ctx->pos+n
 *                        |- log->last_cp_seq       |- ctx->seq+10000+n
 *
 * Then we can safely start the state machine. If failure happens from this
 * point on, the recovery will start from new log->last_checkpoint.
 */
static int
r5c_recovery_rewrite_data_only_stripes(struct r5l_log *log,
				       struct r5l_recovery_ctx *ctx)
{
	struct stripe_head *sh;
	struct mddev *mddev = log->rdev->mddev;
	struct page *page;
	sector_t next_checkpoint = MaxSector;

	page = alloc_page(GFP_KERNEL);
	if (!page) {
		pr_err("md/raid:%s: cannot allocate memory to rewrite data only stripes\n",
		       mdname(mddev));
		return -ENOMEM;
	}

	WARN_ON(list_empty(&ctx->cached_list));

	list_for_each_entry(sh, &ctx->cached_list, lru) {
		struct r5l_meta_block *mb;
		int i;
		int offset;
		sector_t write_pos;

		WARN_ON(!test_bit(STRIPE_R5C_CACHING, &sh->state));
		r5l_recovery_create_empty_meta_block(log, page,
						     ctx->pos, ctx->seq);
		mb = page_address(page);
		offset = le32_to_cpu(mb->meta_size);
		write_pos = r5l_ring_add(log, ctx->pos, BLOCK_SECTORS);

		for (i = sh->disks; i--; ) {
			struct r5dev *dev = &sh->dev[i];
			struct r5l_payload_data_parity *payload;
			void *addr;

			if (test_bit(R5_InJournal, &dev->flags)) {
				payload = (void *)mb + offset;
				payload->header.type = cpu_to_le16(
					R5LOG_PAYLOAD_DATA);
				payload->size = cpu_to_le32(BLOCK_SECTORS);
				payload->location = cpu_to_le64(
					raid5_compute_blocknr(sh, i, 0));
				addr = kmap_atomic(dev->page);
				payload->checksum[0] = cpu_to_le32(
					crc32c_le(log->uuid_checksum, addr,
						  PAGE_SIZE));
				kunmap_atomic(addr);
				sync_page_io(log->rdev, write_pos, PAGE_SIZE,
					     dev->page, REQ_OP_WRITE, false);
				write_pos = r5l_ring_add(log, write_pos,
							 BLOCK_SECTORS);
				offset += sizeof(__le32) +
					sizeof(struct r5l_payload_data_parity);

			}
		}
		mb->meta_size = cpu_to_le32(offset);
		mb->checksum = cpu_to_le32(crc32c_le(log->uuid_checksum,
						     mb, PAGE_SIZE));
		sync_page_io(log->rdev, ctx->pos, PAGE_SIZE, page,
			     REQ_OP_WRITE | REQ_SYNC | REQ_FUA, false);
		sh->log_start = ctx->pos;
		list_add_tail(&sh->r5c, &log->stripe_in_journal_list);
		atomic_inc(&log->stripe_in_journal_count);
		ctx->pos = write_pos;
		ctx->seq += 1;
		next_checkpoint = sh->log_start;
	}
	log->next_checkpoint = next_checkpoint;
	__free_page(page);
	return 0;
}

static void r5c_recovery_flush_data_only_stripes(struct r5l_log *log,
						 struct r5l_recovery_ctx *ctx)
{
	struct mddev *mddev = log->rdev->mddev;
	struct r5conf *conf = mddev->private;
	struct stripe_head *sh, *next;
	bool cleared_pending = false;

	if (ctx->data_only_stripes == 0)
		return;

	if (test_bit(MD_SB_CHANGE_PENDING, &mddev->sb_flags)) {
		cleared_pending = true;
		clear_bit(MD_SB_CHANGE_PENDING, &mddev->sb_flags);
	}
	log->r5c_journal_mode = R5C_JOURNAL_MODE_WRITE_BACK;

	list_for_each_entry_safe(sh, next, &ctx->cached_list, lru) {
		r5c_make_stripe_write_out(sh);
		set_bit(STRIPE_HANDLE, &sh->state);
		list_del_init(&sh->lru);
		raid5_release_stripe(sh);
	}

	/* reuse conf->wait_for_quiescent in recovery */
	wait_event(conf->wait_for_quiescent,
		   atomic_read(&conf->active_stripes) == 0);

	log->r5c_journal_mode = R5C_JOURNAL_MODE_WRITE_THROUGH;
	if (cleared_pending)
		set_bit(MD_SB_CHANGE_PENDING, &mddev->sb_flags);
}

static int r5l_recovery_log(struct r5l_log *log)
{
	struct mddev *mddev = log->rdev->mddev;
	struct r5l_recovery_ctx *ctx;
	int ret;
	sector_t pos;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->pos = log->last_checkpoint;
	ctx->seq = log->last_cp_seq;
	INIT_LIST_HEAD(&ctx->cached_list);
	ctx->meta_page = alloc_page(GFP_KERNEL);

	if (!ctx->meta_page) {
		ret =  -ENOMEM;
		goto meta_page;
	}

	if (r5l_recovery_allocate_ra_pool(log, ctx) != 0) {
		ret = -ENOMEM;
		goto ra_pool;
	}

	ret = r5c_recovery_flush_log(log, ctx);

	if (ret)
		goto error;

	pos = ctx->pos;
	ctx->seq += 10000;

	if ((ctx->data_only_stripes == 0) && (ctx->data_parity_stripes == 0))
		pr_info("md/raid:%s: starting from clean shutdown\n",
			 mdname(mddev));
	else
		pr_info("md/raid:%s: recovering %d data-only stripes and %d data-parity stripes\n",
			 mdname(mddev), ctx->data_only_stripes,
			 ctx->data_parity_stripes);

	if (ctx->data_only_stripes == 0) {
		log->next_checkpoint = ctx->pos;
		r5l_log_write_empty_meta_block(log, ctx->pos, ctx->seq++);
		ctx->pos = r5l_ring_add(log, ctx->pos, BLOCK_SECTORS);
	} else if (r5c_recovery_rewrite_data_only_stripes(log, ctx)) {
		pr_err("md/raid:%s: failed to rewrite stripes to journal\n",
		       mdname(mddev));
		ret =  -EIO;
		goto error;
	}

	log->log_start = ctx->pos;
	log->seq = ctx->seq;
	log->last_checkpoint = pos;
	r5l_write_super(log, pos);

	r5c_recovery_flush_data_only_stripes(log, ctx);
	ret = 0;
error:
	r5l_recovery_free_ra_pool(log, ctx);
ra_pool:
	__free_page(ctx->meta_page);
meta_page:
	kfree(ctx);
	return ret;
}

static void r5l_write_super(struct r5l_log *log, sector_t cp)
{
	struct mddev *mddev = log->rdev->mddev;

	log->rdev->journal_tail = cp;
	set_bit(MD_SB_CHANGE_DEVS, &mddev->sb_flags);
}

static ssize_t r5c_journal_mode_show(struct mddev *mddev, char *page)
{
	struct r5conf *conf;
	int ret;

	ret = mddev_lock(mddev);
	if (ret)
		return ret;

	conf = mddev->private;
	if (!conf || !conf->log)
		goto out_unlock;

	switch (conf->log->r5c_journal_mode) {
	case R5C_JOURNAL_MODE_WRITE_THROUGH:
		ret = snprintf(
			page, PAGE_SIZE, "[%s] %s\n",
			r5c_journal_mode_str[R5C_JOURNAL_MODE_WRITE_THROUGH],
			r5c_journal_mode_str[R5C_JOURNAL_MODE_WRITE_BACK]);
		break;
	case R5C_JOURNAL_MODE_WRITE_BACK:
		ret = snprintf(
			page, PAGE_SIZE, "%s [%s]\n",
			r5c_journal_mode_str[R5C_JOURNAL_MODE_WRITE_THROUGH],
			r5c_journal_mode_str[R5C_JOURNAL_MODE_WRITE_BACK]);
		break;
	default:
		ret = 0;
	}

out_unlock:
	mddev_unlock(mddev);
	return ret;
}

/*
 * Set journal cache mode on @mddev (external API initially needed by dm-raid).
 *
 * @mode as defined in 'enum r5c_journal_mode'.
 *
 */
int r5c_journal_mode_set(struct mddev *mddev, int mode)
{
	struct r5conf *conf;

	if (mode < R5C_JOURNAL_MODE_WRITE_THROUGH ||
	    mode > R5C_JOURNAL_MODE_WRITE_BACK)
		return -EINVAL;

	conf = mddev->private;
	if (!conf || !conf->log)
		return -ENODEV;

	if (raid5_calc_degraded(conf) > 0 &&
	    mode == R5C_JOURNAL_MODE_WRITE_BACK)
		return -EINVAL;

	mddev_suspend(mddev);
	conf->log->r5c_journal_mode = mode;
	mddev_resume(mddev);

	pr_debug("md/raid:%s: setting r5c cache mode to %d: %s\n",
		 mdname(mddev), mode, r5c_journal_mode_str[mode]);
	return 0;
}
EXPORT_SYMBOL(r5c_journal_mode_set);

static ssize_t r5c_journal_mode_store(struct mddev *mddev,
				      const char *page, size_t length)
{
	int mode = ARRAY_SIZE(r5c_journal_mode_str);
	size_t len = length;
	int ret;

	if (len < 2)
		return -EINVAL;

	if (page[len - 1] == '\n')
		len--;

	while (mode--)
		if (strlen(r5c_journal_mode_str[mode]) == len &&
		    !strncmp(page, r5c_journal_mode_str[mode], len))
			break;
	ret = mddev_lock(mddev);
	if (ret)
		return ret;
	ret = r5c_journal_mode_set(mddev, mode);
	mddev_unlock(mddev);
	return ret ?: length;
}

struct md_sysfs_entry
r5c_journal_mode = __ATTR(journal_mode, 0644,
			  r5c_journal_mode_show, r5c_journal_mode_store);

/*
 * Try handle write operation in caching phase. This function should only
 * be called in write-back mode.
 *
 * If all outstanding writes can be handled in caching phase, returns 0
 * If writes requires write-out phase, call r5c_make_stripe_write_out()
 * and returns -EAGAIN
 */
int r5c_try_caching_write(struct r5conf *conf,
			  struct stripe_head *sh,
			  struct stripe_head_state *s,
			  int disks)
{
	struct r5l_log *log = conf->log;
	int i;
	struct r5dev *dev;
	int to_cache = 0;
	void __rcu **pslot;
	sector_t tree_index;
	int ret;
	uintptr_t refcount;

	BUG_ON(!r5c_is_writeback(log));

	if (!test_bit(STRIPE_R5C_CACHING, &sh->state)) {
		/*
		 * There are two different scenarios here:
		 *  1. The stripe has some data cached, and it is sent to
		 *     write-out phase for reclaim
		 *  2. The stripe is clean, and this is the first write
		 *
		 * For 1, return -EAGAIN, so we continue with
		 * handle_stripe_dirtying().
		 *
		 * For 2, set STRIPE_R5C_CACHING and continue with caching
		 * write.
		 */

		/* case 1: anything injournal or anything in written */
		if (s->injournal > 0 || s->written > 0)
			return -EAGAIN;
		/* case 2 */
		set_bit(STRIPE_R5C_CACHING, &sh->state);
	}

	/*
	 * When run in degraded mode, array is set to write-through mode.
	 * This check helps drain pending write safely in the transition to
	 * write-through mode.
	 *
	 * When a stripe is syncing, the write is also handled in write
	 * through mode.
	 */
	if (s->failed || test_bit(STRIPE_SYNCING, &sh->state)) {
		r5c_make_stripe_write_out(sh);
		return -EAGAIN;
	}

	for (i = disks; i--; ) {
		dev = &sh->dev[i];
		/* if non-overwrite, use writing-out phase */
		if (dev->towrite && !test_bit(R5_OVERWRITE, &dev->flags) &&
		    !test_bit(R5_InJournal, &dev->flags)) {
			r5c_make_stripe_write_out(sh);
			return -EAGAIN;
		}
	}

	/* if the stripe is not counted in big_stripe_tree, add it now */
	if (!test_bit(STRIPE_R5C_PARTIAL_STRIPE, &sh->state) &&
	    !test_bit(STRIPE_R5C_FULL_STRIPE, &sh->state)) {
		tree_index = r5c_tree_index(conf, sh->sector);
		spin_lock(&log->tree_lock);
		pslot = radix_tree_lookup_slot(&log->big_stripe_tree,
					       tree_index);
		if (pslot) {
			refcount = (uintptr_t)radix_tree_deref_slot_protected(
				pslot, &log->tree_lock) >>
				R5C_RADIX_COUNT_SHIFT;
			radix_tree_replace_slot(
				&log->big_stripe_tree, pslot,
				(void *)((refcount + 1) << R5C_RADIX_COUNT_SHIFT));
		} else {
			/*
			 * this radix_tree_insert can fail safely, so no
			 * need to call radix_tree_preload()
			 */
			ret = radix_tree_insert(
				&log->big_stripe_tree, tree_index,
				(void *)(1 << R5C_RADIX_COUNT_SHIFT));
			if (ret) {
				spin_unlock(&log->tree_lock);
				r5c_make_stripe_write_out(sh);
				return -EAGAIN;
			}
		}
		spin_unlock(&log->tree_lock);

		/*
		 * set STRIPE_R5C_PARTIAL_STRIPE, this shows the stripe is
		 * counted in the radix tree
		 */
		set_bit(STRIPE_R5C_PARTIAL_STRIPE, &sh->state);
		atomic_inc(&conf->r5c_cached_partial_stripes);
	}

	for (i = disks; i--; ) {
		dev = &sh->dev[i];
		if (dev->towrite) {
			set_bit(R5_Wantwrite, &dev->flags);
			set_bit(R5_Wantdrain, &dev->flags);
			set_bit(R5_LOCKED, &dev->flags);
			to_cache++;
		}
	}

	if (to_cache) {
		set_bit(STRIPE_OP_BIODRAIN, &s->ops_request);
		/*
		 * set STRIPE_LOG_TRAPPED, which triggers r5c_cache_data()
		 * in ops_run_io(). STRIPE_LOG_TRAPPED will be cleared in
		 * r5c_handle_data_cached()
		 */
		set_bit(STRIPE_LOG_TRAPPED, &sh->state);
	}

	return 0;
}

/*
 * free extra pages (orig_page) we allocated for prexor
 */
void r5c_release_extra_page(struct stripe_head *sh)
{
	struct r5conf *conf = sh->raid_conf;
	int i;
	bool using_disk_info_extra_page;

	using_disk_info_extra_page =
		sh->dev[0].orig_page == conf->disks[0].extra_page;

	for (i = sh->disks; i--; )
		if (sh->dev[i].page != sh->dev[i].orig_page) {
			struct page *p = sh->dev[i].orig_page;

			sh->dev[i].orig_page = sh->dev[i].page;
			clear_bit(R5_OrigPageUPTDODATE, &sh->dev[i].flags);

			if (!using_disk_info_extra_page)
				put_page(p);
		}

	if (using_disk_info_extra_page) {
		clear_bit(R5C_EXTRA_PAGE_IN_USE, &conf->cache_state);
		md_wakeup_thread(conf->mddev->thread);
	}
}

void r5c_use_extra_page(struct stripe_head *sh)
{
	struct r5conf *conf = sh->raid_conf;
	int i;
	struct r5dev *dev;

	for (i = sh->disks; i--; ) {
		dev = &sh->dev[i];
		if (dev->orig_page != dev->page)
			put_page(dev->orig_page);
		dev->orig_page = conf->disks[i].extra_page;
	}
}

/*
 * clean up the stripe (clear R5_InJournal for dev[pd_idx] etc.) after the
 * stripe is committed to RAID disks.
 */
void r5c_finish_stripe_write_out(struct r5conf *conf,
				 struct stripe_head *sh,
				 struct stripe_head_state *s)
{
	struct r5l_log *log = conf->log;
	int i;
	int do_wakeup = 0;
	sector_t tree_index;
	void __rcu **pslot;
	uintptr_t refcount;

	if (!log || !test_bit(R5_InJournal, &sh->dev[sh->pd_idx].flags))
		return;

	WARN_ON(test_bit(STRIPE_R5C_CACHING, &sh->state));
	clear_bit(R5_InJournal, &sh->dev[sh->pd_idx].flags);

	if (log->r5c_journal_mode == R5C_JOURNAL_MODE_WRITE_THROUGH)
		return;

	for (i = sh->disks; i--; ) {
		clear_bit(R5_InJournal, &sh->dev[i].flags);
		if (test_and_clear_bit(R5_Overlap, &sh->dev[i].flags))
			do_wakeup = 1;
	}

	/*
	 * analyse_stripe() runs before r5c_finish_stripe_write_out(),
	 * We updated R5_InJournal, so we also update s->injournal.
	 */
	s->injournal = 0;

	if (test_and_clear_bit(STRIPE_FULL_WRITE, &sh->state))
		if (atomic_dec_and_test(&conf->pending_full_writes))
			md_wakeup_thread(conf->mddev->thread);

	if (do_wakeup)
		wake_up(&conf->wait_for_overlap);

	spin_lock_irq(&log->stripe_in_journal_lock);
	list_del_init(&sh->r5c);
	spin_unlock_irq(&log->stripe_in_journal_lock);
	sh->log_start = MaxSector;

	atomic_dec(&log->stripe_in_journal_count);
	r5c_update_log_state(log);

	/* stop counting this stripe in big_stripe_tree */
	if (test_bit(STRIPE_R5C_PARTIAL_STRIPE, &sh->state) ||
	    test_bit(STRIPE_R5C_FULL_STRIPE, &sh->state)) {
		tree_index = r5c_tree_index(conf, sh->sector);
		spin_lock(&log->tree_lock);
		pslot = radix_tree_lookup_slot(&log->big_stripe_tree,
					       tree_index);
		BUG_ON(pslot == NULL);
		refcount = (uintptr_t)radix_tree_deref_slot_protected(
			pslot, &log->tree_lock) >>
			R5C_RADIX_COUNT_SHIFT;
		if (refcount == 1)
			radix_tree_delete(&log->big_stripe_tree, tree_index);
		else
			radix_tree_replace_slot(
				&log->big_stripe_tree, pslot,
				(void *)((refcount - 1) << R5C_RADIX_COUNT_SHIFT));
		spin_unlock(&log->tree_lock);
	}

	if (test_and_clear_bit(STRIPE_R5C_PARTIAL_STRIPE, &sh->state)) {
		BUG_ON(atomic_read(&conf->r5c_cached_partial_stripes) == 0);
		atomic_dec(&conf->r5c_flushing_partial_stripes);
		atomic_dec(&conf->r5c_cached_partial_stripes);
	}

	if (test_and_clear_bit(STRIPE_R5C_FULL_STRIPE, &sh->state)) {
		BUG_ON(atomic_read(&conf->r5c_cached_full_stripes) == 0);
		atomic_dec(&conf->r5c_flushing_full_stripes);
		atomic_dec(&conf->r5c_cached_full_stripes);
	}

	r5l_append_flush_payload(log, sh->sector);
	/* stripe is flused to raid disks, we can do resync now */
	if (test_bit(STRIPE_SYNC_REQUESTED, &sh->state))
		set_bit(STRIPE_HANDLE, &sh->state);
}

int r5c_cache_data(struct r5l_log *log, struct stripe_head *sh)
{
	struct r5conf *conf = sh->raid_conf;
	int pages = 0;
	int reserve;
	int i;
	int ret = 0;

	BUG_ON(!log);

	for (i = 0; i < sh->disks; i++) {
		void *addr;

		if (!test_bit(R5_Wantwrite, &sh->dev[i].flags))
			continue;
		addr = kmap_atomic(sh->dev[i].page);
		sh->dev[i].log_checksum = crc32c_le(log->uuid_checksum,
						    addr, PAGE_SIZE);
		kunmap_atomic(addr);
		pages++;
	}
	WARN_ON(pages == 0);

	/*
	 * The stripe must enter state machine again to call endio, so
	 * don't delay.
	 */
	clear_bit(STRIPE_DELAYED, &sh->state);
	atomic_inc(&sh->count);

	mutex_lock(&log->io_mutex);
	/* meta + data */
	reserve = (1 + pages) << (PAGE_SHIFT - 9);

	if (test_bit(R5C_LOG_CRITICAL, &conf->cache_state) &&
	    sh->log_start == MaxSector)
		r5l_add_no_space_stripe(log, sh);
	else if (!r5l_has_free_space(log, reserve)) {
		if (sh->log_start == log->last_checkpoint)
			BUG();
		else
			r5l_add_no_space_stripe(log, sh);
	} else {
		ret = r5l_log_stripe(log, sh, pages, 0);
		if (ret) {
			spin_lock_irq(&log->io_list_lock);
			list_add_tail(&sh->log_list, &log->no_mem_stripes);
			spin_unlock_irq(&log->io_list_lock);
		}
	}

	mutex_unlock(&log->io_mutex);
	return 0;
}

/* check whether this big stripe is in write back cache. */
bool r5c_big_stripe_cached(struct r5conf *conf, sector_t sect)
{
	struct r5l_log *log = conf->log;
	sector_t tree_index;
	void *slot;

	if (!log)
		return false;

	WARN_ON_ONCE(!rcu_read_lock_held());
	tree_index = r5c_tree_index(conf, sect);
	slot = radix_tree_lookup(&log->big_stripe_tree, tree_index);
	return slot != NULL;
}

static int r5l_load_log(struct r5l_log *log)
{
	struct md_rdev *rdev = log->rdev;
	struct page *page;
	struct r5l_meta_block *mb;
	sector_t cp = log->rdev->journal_tail;
	u32 stored_crc, expected_crc;
	bool create_super = false;
	int ret = 0;

	/* Make sure it's valid */
	if (cp >= rdev->sectors || round_down(cp, BLOCK_SECTORS) != cp)
		cp = 0;
	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	if (!sync_page_io(rdev, cp, PAGE_SIZE, page, REQ_OP_READ, false)) {
		ret = -EIO;
		goto ioerr;
	}
	mb = page_address(page);

	if (le32_to_cpu(mb->magic) != R5LOG_MAGIC ||
	    mb->version != R5LOG_VERSION) {
		create_super = true;
		goto create;
	}
	stored_crc = le32_to_cpu(mb->checksum);
	mb->checksum = 0;
	expected_crc = crc32c_le(log->uuid_checksum, mb, PAGE_SIZE);
	if (stored_crc != expected_crc) {
		create_super = true;
		goto create;
	}
	if (le64_to_cpu(mb->position) != cp) {
		create_super = true;
		goto create;
	}
create:
	if (create_super) {
		log->last_cp_seq = get_random_u32();
		cp = 0;
		r5l_log_write_empty_meta_block(log, cp, log->last_cp_seq);
		/*
		 * Make sure super points to correct address. Log might have
		 * data very soon. If super hasn't correct log tail address,
		 * recovery can't find the log
		 */
		r5l_write_super(log, cp);
	} else
		log->last_cp_seq = le64_to_cpu(mb->seq);

	log->device_size = round_down(rdev->sectors, BLOCK_SECTORS);
	log->max_free_space = log->device_size >> RECLAIM_MAX_FREE_SPACE_SHIFT;
	if (log->max_free_space > RECLAIM_MAX_FREE_SPACE)
		log->max_free_space = RECLAIM_MAX_FREE_SPACE;
	log->last_checkpoint = cp;

	__free_page(page);

	if (create_super) {
		log->log_start = r5l_ring_add(log, cp, BLOCK_SECTORS);
		log->seq = log->last_cp_seq + 1;
		log->next_checkpoint = cp;
	} else
		ret = r5l_recovery_log(log);

	r5c_update_log_state(log);
	return ret;
ioerr:
	__free_page(page);
	return ret;
}

int r5l_start(struct r5l_log *log)
{
	int ret;

	if (!log)
		return 0;

	ret = r5l_load_log(log);
	if (ret) {
		struct mddev *mddev = log->rdev->mddev;
		struct r5conf *conf = mddev->private;

		r5l_exit_log(conf);
	}
	return ret;
}

void r5c_update_on_rdev_error(struct mddev *mddev, struct md_rdev *rdev)
{
	struct r5conf *conf = mddev->private;
	struct r5l_log *log = conf->log;

	if (!log)
		return;

	if ((raid5_calc_degraded(conf) > 0 ||
	     test_bit(Journal, &rdev->flags)) &&
	    conf->log->r5c_journal_mode == R5C_JOURNAL_MODE_WRITE_BACK)
		schedule_work(&log->disable_writeback_work);
}

int r5l_init_log(struct r5conf *conf, struct md_rdev *rdev)
{
	struct request_queue *q = bdev_get_queue(rdev->bdev);
	struct r5l_log *log;
	int ret;

	pr_debug("md/raid:%s: using device %pg as journal\n",
		 mdname(conf->mddev), rdev->bdev);

	if (PAGE_SIZE != 4096)
		return -EINVAL;

	/*
	 * The PAGE_SIZE must be big enough to hold 1 r5l_meta_block and
	 * raid_disks r5l_payload_data_parity.
	 *
	 * Write journal and cache does not work for very big array
	 * (raid_disks > 203)
	 */
	if (sizeof(struct r5l_meta_block) +
	    ((sizeof(struct r5l_payload_data_parity) + sizeof(__le32)) *
	     conf->raid_disks) > PAGE_SIZE) {
		pr_err("md/raid:%s: write journal/cache doesn't work for array with %d disks\n",
		       mdname(conf->mddev), conf->raid_disks);
		return -EINVAL;
	}

	log = kzalloc(sizeof(*log), GFP_KERNEL);
	if (!log)
		return -ENOMEM;
	log->rdev = rdev;

	log->need_cache_flush = test_bit(QUEUE_FLAG_WC, &q->queue_flags) != 0;

	log->uuid_checksum = crc32c_le(~0, rdev->mddev->uuid,
				       sizeof(rdev->mddev->uuid));

	mutex_init(&log->io_mutex);

	spin_lock_init(&log->io_list_lock);
	INIT_LIST_HEAD(&log->running_ios);
	INIT_LIST_HEAD(&log->io_end_ios);
	INIT_LIST_HEAD(&log->flushing_ios);
	INIT_LIST_HEAD(&log->finished_ios);

	log->io_kc = KMEM_CACHE(r5l_io_unit, 0);
	if (!log->io_kc)
		goto io_kc;

	ret = mempool_init_slab_pool(&log->io_pool, R5L_POOL_SIZE, log->io_kc);
	if (ret)
		goto io_pool;

	ret = bioset_init(&log->bs, R5L_POOL_SIZE, 0, BIOSET_NEED_BVECS);
	if (ret)
		goto io_bs;

	ret = mempool_init_page_pool(&log->meta_pool, R5L_POOL_SIZE, 0);
	if (ret)
		goto out_mempool;

	spin_lock_init(&log->tree_lock);
	INIT_RADIX_TREE(&log->big_stripe_tree, GFP_NOWAIT | __GFP_NOWARN);

	log->reclaim_thread = md_register_thread(r5l_reclaim_thread,
						 log->rdev->mddev, "reclaim");
	if (!log->reclaim_thread)
		goto reclaim_thread;
	log->reclaim_thread->timeout = R5C_RECLAIM_WAKEUP_INTERVAL;

	init_waitqueue_head(&log->iounit_wait);

	INIT_LIST_HEAD(&log->no_mem_stripes);

	INIT_LIST_HEAD(&log->no_space_stripes);
	spin_lock_init(&log->no_space_stripes_lock);

	INIT_WORK(&log->deferred_io_work, r5l_submit_io_async);
	INIT_WORK(&log->disable_writeback_work, r5c_disable_writeback_async);

	log->r5c_journal_mode = R5C_JOURNAL_MODE_WRITE_THROUGH;
	INIT_LIST_HEAD(&log->stripe_in_journal_list);
	spin_lock_init(&log->stripe_in_journal_lock);
	atomic_set(&log->stripe_in_journal_count, 0);

	conf->log = log;

	set_bit(MD_HAS_JOURNAL, &conf->mddev->flags);
	return 0;

reclaim_thread:
	mempool_exit(&log->meta_pool);
out_mempool:
	bioset_exit(&log->bs);
io_bs:
	mempool_exit(&log->io_pool);
io_pool:
	kmem_cache_destroy(log->io_kc);
io_kc:
	kfree(log);
	return -EINVAL;
}

void r5l_exit_log(struct r5conf *conf)
{
	struct r5l_log *log = conf->log;

	md_unregister_thread(&log->reclaim_thread);

	/*
	 * 'reconfig_mutex' is held by caller, set 'confg->log' to NULL to
	 * ensure disable_writeback_work wakes up and exits.
	 */
	conf->log = NULL;
	wake_up(&conf->mddev->sb_wait);
	flush_work(&log->disable_writeback_work);

	mempool_exit(&log->meta_pool);
	bioset_exit(&log->bs);
	mempool_exit(&log->io_pool);
	kmem_cache_destroy(log->io_kc);
	kfree(log);
}
