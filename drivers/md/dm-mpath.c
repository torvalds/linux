// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2003 Sistina Software Limited.
 * Copyright (C) 2004-2005 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include <linux/device-mapper.h>

#include "dm-rq.h"
#include "dm-bio-record.h"
#include "dm-path-selector.h"
#include "dm-uevent.h"

#include <linux/blkdev.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <scsi/scsi_dh.h>
#include <linux/atomic.h>
#include <linux/blk-mq.h>

static struct workqueue_struct *dm_mpath_wq;

#define DM_MSG_PREFIX "multipath"
#define DM_PG_INIT_DELAY_MSECS 2000
#define DM_PG_INIT_DELAY_DEFAULT ((unsigned int) -1)
#define QUEUE_IF_NO_PATH_TIMEOUT_DEFAULT 0

static unsigned long queue_if_no_path_timeout_secs = QUEUE_IF_NO_PATH_TIMEOUT_DEFAULT;

/* Path properties */
struct pgpath {
	struct list_head list;

	struct priority_group *pg;	/* Owning PG */
	unsigned int fail_count;		/* Cumulative failure count */

	struct dm_path path;
	struct delayed_work activate_path;

	bool is_active:1;		/* Path status */
};

#define path_to_pgpath(__pgp) container_of((__pgp), struct pgpath, path)

/*
 * Paths are grouped into Priority Groups and numbered from 1 upwards.
 * Each has a path selector which controls which path gets used.
 */
struct priority_group {
	struct list_head list;

	struct multipath *m;		/* Owning multipath instance */
	struct path_selector ps;

	unsigned int pg_num;		/* Reference number */
	unsigned int nr_pgpaths;		/* Number of paths in PG */
	struct list_head pgpaths;

	bool bypassed:1;		/* Temporarily bypass this PG? */
};

/* Multipath context */
struct multipath {
	unsigned long flags;		/* Multipath state flags */

	spinlock_t lock;
	enum dm_queue_mode queue_mode;

	struct pgpath *current_pgpath;
	struct priority_group *current_pg;
	struct priority_group *next_pg;	/* Switch to this PG if set */
	struct priority_group *last_probed_pg;

	atomic_t nr_valid_paths;	/* Total number of usable paths */
	unsigned int nr_priority_groups;
	struct list_head priority_groups;

	const char *hw_handler_name;
	char *hw_handler_params;
	wait_queue_head_t pg_init_wait;	/* Wait for pg_init completion */
	wait_queue_head_t probe_wait;   /* Wait for probing paths */
	unsigned int pg_init_retries;	/* Number of times to retry pg_init */
	unsigned int pg_init_delay_msecs;	/* Number of msecs before pg_init retry */
	atomic_t pg_init_in_progress;	/* Only one pg_init allowed at once */
	atomic_t pg_init_count;		/* Number of times pg_init called */

	struct mutex work_mutex;
	struct work_struct trigger_event;
	struct dm_target *ti;

	struct work_struct process_queued_bios;
	struct bio_list queued_bios;

	struct timer_list nopath_timer;	/* Timeout for queue_if_no_path */
	bool is_suspending;
};

/*
 * Context information attached to each io we process.
 */
struct dm_mpath_io {
	struct pgpath *pgpath;
	size_t nr_bytes;
	u64 start_time_ns;
};

typedef int (*action_fn) (struct pgpath *pgpath);

static struct workqueue_struct *kmultipathd, *kmpath_handlerd;
static void trigger_event(struct work_struct *work);
static void activate_or_offline_path(struct pgpath *pgpath);
static void activate_path_work(struct work_struct *work);
static void process_queued_bios(struct work_struct *work);
static void queue_if_no_path_timeout_work(struct timer_list *t);

/*
 *-----------------------------------------------
 * Multipath state flags.
 *-----------------------------------------------
 */
#define MPATHF_QUEUE_IO 0			/* Must we queue all I/O? */
#define MPATHF_QUEUE_IF_NO_PATH 1		/* Queue I/O if last path fails? */
#define MPATHF_SAVED_QUEUE_IF_NO_PATH 2		/* Saved state during suspension */
#define MPATHF_RETAIN_ATTACHED_HW_HANDLER 3	/* If there's already a hw_handler present, don't change it. */
#define MPATHF_PG_INIT_DISABLED 4		/* pg_init is not currently allowed */
#define MPATHF_PG_INIT_REQUIRED 5		/* pg_init needs calling? */
#define MPATHF_PG_INIT_DELAY_RETRY 6		/* Delay pg_init retry? */
#define MPATHF_DELAY_PG_SWITCH 7		/* Delay switching pg if it still has paths */
#define MPATHF_NEED_PG_SWITCH 8			/* Need to switch pgs after the delay has ended */

static bool mpath_double_check_test_bit(int MPATHF_bit, struct multipath *m)
{
	bool r = test_bit(MPATHF_bit, &m->flags);

	if (r) {
		unsigned long flags;

		spin_lock_irqsave(&m->lock, flags);
		r = test_bit(MPATHF_bit, &m->flags);
		spin_unlock_irqrestore(&m->lock, flags);
	}

	return r;
}

/*
 *-----------------------------------------------
 * Allocation routines
 *-----------------------------------------------
 */
static struct pgpath *alloc_pgpath(void)
{
	struct pgpath *pgpath = kzalloc(sizeof(*pgpath), GFP_KERNEL);

	if (!pgpath)
		return NULL;

	pgpath->is_active = true;

	return pgpath;
}

static void free_pgpath(struct pgpath *pgpath)
{
	kfree(pgpath);
}

static struct priority_group *alloc_priority_group(void)
{
	struct priority_group *pg;

	pg = kzalloc(sizeof(*pg), GFP_KERNEL);

	if (pg)
		INIT_LIST_HEAD(&pg->pgpaths);

	return pg;
}

static void free_pgpaths(struct list_head *pgpaths, struct dm_target *ti)
{
	struct pgpath *pgpath, *tmp;

	list_for_each_entry_safe(pgpath, tmp, pgpaths, list) {
		list_del(&pgpath->list);
		dm_put_device(ti, pgpath->path.dev);
		free_pgpath(pgpath);
	}
}

static void free_priority_group(struct priority_group *pg,
				struct dm_target *ti)
{
	struct path_selector *ps = &pg->ps;

	if (ps->type) {
		ps->type->destroy(ps);
		dm_put_path_selector(ps->type);
	}

	free_pgpaths(&pg->pgpaths, ti);
	kfree(pg);
}

static struct multipath *alloc_multipath(struct dm_target *ti)
{
	struct multipath *m;

	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (m) {
		INIT_LIST_HEAD(&m->priority_groups);
		spin_lock_init(&m->lock);
		atomic_set(&m->nr_valid_paths, 0);
		INIT_WORK(&m->trigger_event, trigger_event);
		mutex_init(&m->work_mutex);

		m->queue_mode = DM_TYPE_NONE;

		m->ti = ti;
		ti->private = m;

		timer_setup(&m->nopath_timer, queue_if_no_path_timeout_work, 0);
	}

	return m;
}

static int alloc_multipath_stage2(struct dm_target *ti, struct multipath *m)
{
	if (m->queue_mode == DM_TYPE_NONE) {
		m->queue_mode = DM_TYPE_REQUEST_BASED;
	} else if (m->queue_mode == DM_TYPE_BIO_BASED) {
		INIT_WORK(&m->process_queued_bios, process_queued_bios);
		/*
		 * bio-based doesn't support any direct scsi_dh management;
		 * it just discovers if a scsi_dh is attached.
		 */
		set_bit(MPATHF_RETAIN_ATTACHED_HW_HANDLER, &m->flags);
	}

	dm_table_set_type(ti->table, m->queue_mode);

	/*
	 * Init fields that are only used when a scsi_dh is attached
	 * - must do this unconditionally (really doesn't hurt non-SCSI uses)
	 */
	set_bit(MPATHF_QUEUE_IO, &m->flags);
	atomic_set(&m->pg_init_in_progress, 0);
	atomic_set(&m->pg_init_count, 0);
	m->pg_init_delay_msecs = DM_PG_INIT_DELAY_DEFAULT;
	init_waitqueue_head(&m->pg_init_wait);
	init_waitqueue_head(&m->probe_wait);

	return 0;
}

static void free_multipath(struct multipath *m)
{
	struct priority_group *pg, *tmp;

	list_for_each_entry_safe(pg, tmp, &m->priority_groups, list) {
		list_del(&pg->list);
		free_priority_group(pg, m->ti);
	}

	kfree(m->hw_handler_name);
	kfree(m->hw_handler_params);
	mutex_destroy(&m->work_mutex);
	kfree(m);
}

static struct dm_mpath_io *get_mpio(union map_info *info)
{
	return info->ptr;
}

static size_t multipath_per_bio_data_size(void)
{
	return sizeof(struct dm_mpath_io) + sizeof(struct dm_bio_details);
}

static struct dm_mpath_io *get_mpio_from_bio(struct bio *bio)
{
	return dm_per_bio_data(bio, multipath_per_bio_data_size());
}

static struct dm_bio_details *get_bio_details_from_mpio(struct dm_mpath_io *mpio)
{
	/* dm_bio_details is immediately after the dm_mpath_io in bio's per-bio-data */
	void *bio_details = mpio + 1;
	return bio_details;
}

static void multipath_init_per_bio_data(struct bio *bio, struct dm_mpath_io **mpio_p)
{
	struct dm_mpath_io *mpio = get_mpio_from_bio(bio);
	struct dm_bio_details *bio_details = get_bio_details_from_mpio(mpio);

	mpio->nr_bytes = bio->bi_iter.bi_size;
	mpio->pgpath = NULL;
	mpio->start_time_ns = 0;
	*mpio_p = mpio;

	dm_bio_record(bio_details, bio);
}

/*
 *-----------------------------------------------
 * Path selection
 *-----------------------------------------------
 */
static int __pg_init_all_paths(struct multipath *m)
{
	struct pgpath *pgpath;
	unsigned long pg_init_delay = 0;

	lockdep_assert_held(&m->lock);

	if (atomic_read(&m->pg_init_in_progress) || test_bit(MPATHF_PG_INIT_DISABLED, &m->flags))
		return 0;

	atomic_inc(&m->pg_init_count);
	clear_bit(MPATHF_PG_INIT_REQUIRED, &m->flags);

	/* Check here to reset pg_init_required */
	if (!m->current_pg)
		return 0;

	if (test_bit(MPATHF_PG_INIT_DELAY_RETRY, &m->flags))
		pg_init_delay = msecs_to_jiffies(m->pg_init_delay_msecs != DM_PG_INIT_DELAY_DEFAULT ?
						 m->pg_init_delay_msecs : DM_PG_INIT_DELAY_MSECS);
	list_for_each_entry(pgpath, &m->current_pg->pgpaths, list) {
		/* Skip failed paths */
		if (!pgpath->is_active)
			continue;
		if (queue_delayed_work(kmpath_handlerd, &pgpath->activate_path,
				       pg_init_delay))
			atomic_inc(&m->pg_init_in_progress);
	}
	return atomic_read(&m->pg_init_in_progress);
}

static int pg_init_all_paths(struct multipath *m)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);
	ret = __pg_init_all_paths(m);
	spin_unlock_irqrestore(&m->lock, flags);

	return ret;
}

static void __switch_pg(struct multipath *m, struct priority_group *pg)
{
	lockdep_assert_held(&m->lock);

	m->current_pg = pg;

	/* Must we initialise the PG first, and queue I/O till it's ready? */
	if (m->hw_handler_name) {
		set_bit(MPATHF_PG_INIT_REQUIRED, &m->flags);
		set_bit(MPATHF_QUEUE_IO, &m->flags);
	} else {
		clear_bit(MPATHF_PG_INIT_REQUIRED, &m->flags);
		clear_bit(MPATHF_QUEUE_IO, &m->flags);
	}

	atomic_set(&m->pg_init_count, 0);
}

static struct pgpath *choose_path_in_pg(struct multipath *m,
					struct priority_group *pg,
					size_t nr_bytes)
{
	unsigned long flags;
	struct dm_path *path;
	struct pgpath *pgpath;

	path = pg->ps.type->select_path(&pg->ps, nr_bytes);
	if (!path)
		return ERR_PTR(-ENXIO);

	pgpath = path_to_pgpath(path);

	if (unlikely(READ_ONCE(m->current_pg) != pg)) {
		/* Only update current_pgpath if pg changed */
		spin_lock_irqsave(&m->lock, flags);
		m->current_pgpath = pgpath;
		__switch_pg(m, pg);
		spin_unlock_irqrestore(&m->lock, flags);
	}

	return pgpath;
}

static struct pgpath *choose_pgpath(struct multipath *m, size_t nr_bytes)
{
	unsigned long flags;
	struct priority_group *pg;
	struct pgpath *pgpath;
	unsigned int bypassed = 1;

	if (!atomic_read(&m->nr_valid_paths)) {
		spin_lock_irqsave(&m->lock, flags);
		clear_bit(MPATHF_QUEUE_IO, &m->flags);
		spin_unlock_irqrestore(&m->lock, flags);
		goto failed;
	}

	/* Don't change PG until it has no remaining paths */
	pg = READ_ONCE(m->current_pg);
	if (pg) {
		pgpath = choose_path_in_pg(m, pg, nr_bytes);
		if (!IS_ERR_OR_NULL(pgpath))
			return pgpath;
	}

	/* Were we instructed to switch PG? */
	if (READ_ONCE(m->next_pg)) {
		spin_lock_irqsave(&m->lock, flags);
		pg = m->next_pg;
		if (!pg) {
			spin_unlock_irqrestore(&m->lock, flags);
			goto check_all_pgs;
		}
		m->next_pg = NULL;
		spin_unlock_irqrestore(&m->lock, flags);
		pgpath = choose_path_in_pg(m, pg, nr_bytes);
		if (!IS_ERR_OR_NULL(pgpath))
			return pgpath;
	}
check_all_pgs:
	/*
	 * Loop through priority groups until we find a valid path.
	 * First time we skip PGs marked 'bypassed'.
	 * Second time we only try the ones we skipped, but set
	 * pg_init_delay_retry so we do not hammer controllers.
	 */
	do {
		list_for_each_entry(pg, &m->priority_groups, list) {
			if (pg->bypassed == !!bypassed)
				continue;
			pgpath = choose_path_in_pg(m, pg, nr_bytes);
			if (!IS_ERR_OR_NULL(pgpath)) {
				if (!bypassed) {
					spin_lock_irqsave(&m->lock, flags);
					set_bit(MPATHF_PG_INIT_DELAY_RETRY, &m->flags);
					spin_unlock_irqrestore(&m->lock, flags);
				}
				return pgpath;
			}
		}
	} while (bypassed--);

failed:
	spin_lock_irqsave(&m->lock, flags);
	m->current_pgpath = NULL;
	m->current_pg = NULL;
	spin_unlock_irqrestore(&m->lock, flags);

	return NULL;
}

/*
 * dm_report_EIO() is a macro instead of a function to make pr_debug_ratelimited()
 * report the function name and line number of the function from which
 * it has been invoked.
 */
#define dm_report_EIO(m)						\
	DMDEBUG_LIMIT("%s: returning EIO; QIFNP = %d; SQIFNP = %d; DNFS = %d", \
		      dm_table_device_name((m)->ti->table),		\
		      test_bit(MPATHF_QUEUE_IF_NO_PATH, &(m)->flags),	\
		      test_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &(m)->flags), \
		      dm_noflush_suspending((m)->ti))

/*
 * Check whether bios must be queued in the device-mapper core rather
 * than here in the target.
 */
static bool __must_push_back(struct multipath *m)
{
	return dm_noflush_suspending(m->ti);
}

static bool must_push_back_rq(struct multipath *m)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&m->lock, flags);
	ret = (test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags) || __must_push_back(m));
	spin_unlock_irqrestore(&m->lock, flags);

	return ret;
}

/*
 * Map cloned requests (request-based multipath)
 */
static int multipath_clone_and_map(struct dm_target *ti, struct request *rq,
				   union map_info *map_context,
				   struct request **__clone)
{
	struct multipath *m = ti->private;
	size_t nr_bytes = blk_rq_bytes(rq);
	struct pgpath *pgpath;
	struct block_device *bdev;
	struct dm_mpath_io *mpio = get_mpio(map_context);
	struct request_queue *q;
	struct request *clone;

	/* Do we need to select a new pgpath? */
	pgpath = READ_ONCE(m->current_pgpath);
	if (!pgpath || !mpath_double_check_test_bit(MPATHF_QUEUE_IO, m))
		pgpath = choose_pgpath(m, nr_bytes);

	if (!pgpath) {
		if (must_push_back_rq(m))
			return DM_MAPIO_DELAY_REQUEUE;
		dm_report_EIO(m);	/* Failed */
		return DM_MAPIO_KILL;
	} else if (mpath_double_check_test_bit(MPATHF_QUEUE_IO, m) ||
		   mpath_double_check_test_bit(MPATHF_PG_INIT_REQUIRED, m)) {
		pg_init_all_paths(m);
		return DM_MAPIO_DELAY_REQUEUE;
	}

	mpio->pgpath = pgpath;
	mpio->nr_bytes = nr_bytes;

	bdev = pgpath->path.dev->bdev;
	q = bdev_get_queue(bdev);
	clone = blk_mq_alloc_request(q, rq->cmd_flags | REQ_NOMERGE,
			BLK_MQ_REQ_NOWAIT);
	if (IS_ERR(clone)) {
		/* EBUSY, ENODEV or EWOULDBLOCK: requeue */
		if (blk_queue_dying(q)) {
			atomic_inc(&m->pg_init_in_progress);
			activate_or_offline_path(pgpath);
			return DM_MAPIO_DELAY_REQUEUE;
		}

		/*
		 * blk-mq's SCHED_RESTART can cover this requeue, so we
		 * needn't deal with it by DELAY_REQUEUE. More importantly,
		 * we have to return DM_MAPIO_REQUEUE so that blk-mq can
		 * get the queue busy feedback (via BLK_STS_RESOURCE),
		 * otherwise I/O merging can suffer.
		 */
		return DM_MAPIO_REQUEUE;
	}
	clone->bio = clone->biotail = NULL;
	clone->cmd_flags |= REQ_FAILFAST_TRANSPORT;
	*__clone = clone;

	if (pgpath->pg->ps.type->start_io)
		pgpath->pg->ps.type->start_io(&pgpath->pg->ps,
					      &pgpath->path,
					      nr_bytes);
	return DM_MAPIO_REMAPPED;
}

static void multipath_release_clone(struct request *clone,
				    union map_info *map_context)
{
	if (unlikely(map_context)) {
		/*
		 * non-NULL map_context means caller is still map
		 * method; must undo multipath_clone_and_map()
		 */
		struct dm_mpath_io *mpio = get_mpio(map_context);
		struct pgpath *pgpath = mpio->pgpath;

		if (pgpath && pgpath->pg->ps.type->end_io)
			pgpath->pg->ps.type->end_io(&pgpath->pg->ps,
						    &pgpath->path,
						    mpio->nr_bytes,
						    clone->io_start_time_ns);
	}

	blk_mq_free_request(clone);
}

/*
 * Map cloned bios (bio-based multipath)
 */

static void __multipath_queue_bio(struct multipath *m, struct bio *bio)
{
	/* Queue for the daemon to resubmit */
	bio_list_add(&m->queued_bios, bio);
	if (!test_bit(MPATHF_QUEUE_IO, &m->flags))
		queue_work(kmultipathd, &m->process_queued_bios);
}

static void multipath_queue_bio(struct multipath *m, struct bio *bio)
{
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);
	__multipath_queue_bio(m, bio);
	spin_unlock_irqrestore(&m->lock, flags);
}

static struct pgpath *__map_bio(struct multipath *m, struct bio *bio)
{
	struct pgpath *pgpath;

	/* Do we need to select a new pgpath? */
	pgpath = READ_ONCE(m->current_pgpath);
	if (!pgpath || !mpath_double_check_test_bit(MPATHF_QUEUE_IO, m))
		pgpath = choose_pgpath(m, bio->bi_iter.bi_size);

	if (!pgpath) {
		spin_lock_irq(&m->lock);
		if (test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags)) {
			__multipath_queue_bio(m, bio);
			pgpath = ERR_PTR(-EAGAIN);
		}
		spin_unlock_irq(&m->lock);

	} else if (mpath_double_check_test_bit(MPATHF_QUEUE_IO, m) ||
		   mpath_double_check_test_bit(MPATHF_PG_INIT_REQUIRED, m)) {
		multipath_queue_bio(m, bio);
		pg_init_all_paths(m);
		return ERR_PTR(-EAGAIN);
	}

	return pgpath;
}

static int __multipath_map_bio(struct multipath *m, struct bio *bio,
			       struct dm_mpath_io *mpio)
{
	struct pgpath *pgpath = __map_bio(m, bio);

	if (IS_ERR(pgpath))
		return DM_MAPIO_SUBMITTED;

	if (!pgpath) {
		if (__must_push_back(m))
			return DM_MAPIO_REQUEUE;
		dm_report_EIO(m);
		return DM_MAPIO_KILL;
	}

	mpio->pgpath = pgpath;

	if (dm_ps_use_hr_timer(pgpath->pg->ps.type))
		mpio->start_time_ns = ktime_get_ns();

	bio->bi_status = 0;
	bio_set_dev(bio, pgpath->path.dev->bdev);
	bio->bi_opf |= REQ_FAILFAST_TRANSPORT;

	if (pgpath->pg->ps.type->start_io)
		pgpath->pg->ps.type->start_io(&pgpath->pg->ps,
					      &pgpath->path,
					      mpio->nr_bytes);
	return DM_MAPIO_REMAPPED;
}

static int multipath_map_bio(struct dm_target *ti, struct bio *bio)
{
	struct multipath *m = ti->private;
	struct dm_mpath_io *mpio = NULL;

	multipath_init_per_bio_data(bio, &mpio);
	return __multipath_map_bio(m, bio, mpio);
}

static void process_queued_io_list(struct multipath *m)
{
	if (m->queue_mode == DM_TYPE_REQUEST_BASED)
		dm_mq_kick_requeue_list(dm_table_get_md(m->ti->table));
	else if (m->queue_mode == DM_TYPE_BIO_BASED)
		queue_work(kmultipathd, &m->process_queued_bios);
}

static void process_queued_bios(struct work_struct *work)
{
	int r;
	struct bio *bio;
	struct bio_list bios;
	struct blk_plug plug;
	struct multipath *m =
		container_of(work, struct multipath, process_queued_bios);

	bio_list_init(&bios);

	spin_lock_irq(&m->lock);

	if (bio_list_empty(&m->queued_bios)) {
		spin_unlock_irq(&m->lock);
		return;
	}

	bio_list_merge_init(&bios, &m->queued_bios);

	spin_unlock_irq(&m->lock);

	blk_start_plug(&plug);
	while ((bio = bio_list_pop(&bios))) {
		struct dm_mpath_io *mpio = get_mpio_from_bio(bio);

		dm_bio_restore(get_bio_details_from_mpio(mpio), bio);
		r = __multipath_map_bio(m, bio, mpio);
		switch (r) {
		case DM_MAPIO_KILL:
			bio->bi_status = BLK_STS_IOERR;
			bio_endio(bio);
			break;
		case DM_MAPIO_REQUEUE:
			bio->bi_status = BLK_STS_DM_REQUEUE;
			bio_endio(bio);
			break;
		case DM_MAPIO_REMAPPED:
			submit_bio_noacct(bio);
			break;
		case DM_MAPIO_SUBMITTED:
			break;
		default:
			WARN_ONCE(true, "__multipath_map_bio() returned %d\n", r);
		}
	}
	blk_finish_plug(&plug);
}

/*
 * If we run out of usable paths, should we queue I/O or error it?
 */
static int queue_if_no_path(struct multipath *m, bool f_queue_if_no_path,
			    bool save_old_value, const char *caller)
{
	unsigned long flags;
	bool queue_if_no_path_bit, saved_queue_if_no_path_bit;
	const char *dm_dev_name = dm_table_device_name(m->ti->table);

	DMDEBUG("%s: %s caller=%s f_queue_if_no_path=%d save_old_value=%d",
		dm_dev_name, __func__, caller, f_queue_if_no_path, save_old_value);

	spin_lock_irqsave(&m->lock, flags);

	queue_if_no_path_bit = test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags);
	saved_queue_if_no_path_bit = test_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags);

	if (save_old_value) {
		if (unlikely(!queue_if_no_path_bit && saved_queue_if_no_path_bit)) {
			DMERR("%s: QIFNP disabled but saved as enabled, saving again loses state, not saving!",
			      dm_dev_name);
		} else
			assign_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags, queue_if_no_path_bit);
	} else if (!f_queue_if_no_path && saved_queue_if_no_path_bit) {
		/* due to "fail_if_no_path" message, need to honor it. */
		clear_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags);
	}
	assign_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags, f_queue_if_no_path);

	DMDEBUG("%s: after %s changes; QIFNP = %d; SQIFNP = %d; DNFS = %d",
		dm_dev_name, __func__,
		test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags),
		test_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags),
		dm_noflush_suspending(m->ti));

	spin_unlock_irqrestore(&m->lock, flags);

	if (!f_queue_if_no_path) {
		dm_table_run_md_queue_async(m->ti->table);
		process_queued_io_list(m);
	}

	return 0;
}

/*
 * If the queue_if_no_path timeout fires, turn off queue_if_no_path and
 * process any queued I/O.
 */
static void queue_if_no_path_timeout_work(struct timer_list *t)
{
	struct multipath *m = timer_container_of(m, t, nopath_timer);

	DMWARN("queue_if_no_path timeout on %s, failing queued IO",
	       dm_table_device_name(m->ti->table));
	queue_if_no_path(m, false, false, __func__);
}

/*
 * Enable the queue_if_no_path timeout if necessary.
 * Called with m->lock held.
 */
static void enable_nopath_timeout(struct multipath *m)
{
	unsigned long queue_if_no_path_timeout =
		READ_ONCE(queue_if_no_path_timeout_secs) * HZ;

	lockdep_assert_held(&m->lock);

	if (queue_if_no_path_timeout > 0 &&
	    atomic_read(&m->nr_valid_paths) == 0 &&
	    test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags)) {
		mod_timer(&m->nopath_timer,
			  jiffies + queue_if_no_path_timeout);
	}
}

static void disable_nopath_timeout(struct multipath *m)
{
	timer_delete_sync(&m->nopath_timer);
}

/*
 * An event is triggered whenever a path is taken out of use.
 * Includes path failure and PG bypass.
 */
static void trigger_event(struct work_struct *work)
{
	struct multipath *m =
		container_of(work, struct multipath, trigger_event);

	dm_table_event(m->ti->table);
}

/*
 *---------------------------------------------------------------
 * Constructor/argument parsing:
 * <#multipath feature args> [<arg>]*
 * <#hw_handler args> [hw_handler [<arg>]*]
 * <#priority groups>
 * <initial priority group>
 *     [<selector> <#selector args> [<arg>]*
 *      <#paths> <#per-path selector args>
 *         [<path> [<arg>]* ]+ ]+
 *---------------------------------------------------------------
 */
static int parse_path_selector(struct dm_arg_set *as, struct priority_group *pg,
			       struct dm_target *ti)
{
	int r;
	struct path_selector_type *pst;
	unsigned int ps_argc;

	static const struct dm_arg _args[] = {
		{0, 1024, "invalid number of path selector args"},
	};

	pst = dm_get_path_selector(dm_shift_arg(as));
	if (!pst) {
		ti->error = "unknown path selector type";
		return -EINVAL;
	}

	r = dm_read_arg_group(_args, as, &ps_argc, &ti->error);
	if (r) {
		dm_put_path_selector(pst);
		return -EINVAL;
	}

	r = pst->create(&pg->ps, ps_argc, as->argv);
	if (r) {
		dm_put_path_selector(pst);
		ti->error = "path selector constructor failed";
		return r;
	}

	pg->ps.type = pst;
	dm_consume_args(as, ps_argc);

	return 0;
}

static int setup_scsi_dh(struct block_device *bdev, struct multipath *m,
			 const char **attached_handler_name, char **error)
{
	struct request_queue *q = bdev_get_queue(bdev);
	int r;

	if (mpath_double_check_test_bit(MPATHF_RETAIN_ATTACHED_HW_HANDLER, m)) {
retain:
		if (*attached_handler_name) {
			/*
			 * Clear any hw_handler_params associated with a
			 * handler that isn't already attached.
			 */
			if (m->hw_handler_name && strcmp(*attached_handler_name, m->hw_handler_name)) {
				kfree(m->hw_handler_params);
				m->hw_handler_params = NULL;
			}

			/*
			 * Reset hw_handler_name to match the attached handler
			 *
			 * NB. This modifies the table line to show the actual
			 * handler instead of the original table passed in.
			 */
			kfree(m->hw_handler_name);
			m->hw_handler_name = *attached_handler_name;
			*attached_handler_name = NULL;
		}
	}

	if (m->hw_handler_name) {
		r = scsi_dh_attach(q, m->hw_handler_name);
		if (r == -EBUSY) {
			DMINFO("retaining handler on device %pg", bdev);
			goto retain;
		}
		if (r < 0) {
			*error = "error attaching hardware handler";
			return r;
		}

		if (m->hw_handler_params) {
			r = scsi_dh_set_params(q, m->hw_handler_params);
			if (r < 0) {
				*error = "unable to set hardware handler parameters";
				return r;
			}
		}
	}

	return 0;
}

static struct pgpath *parse_path(struct dm_arg_set *as, struct path_selector *ps,
				 struct dm_target *ti)
{
	int r;
	struct pgpath *p;
	struct multipath *m = ti->private;
	struct request_queue *q;
	const char *attached_handler_name = NULL;

	/* we need at least a path arg */
	if (as->argc < 1) {
		ti->error = "no device given";
		return ERR_PTR(-EINVAL);
	}

	p = alloc_pgpath();
	if (!p)
		return ERR_PTR(-ENOMEM);

	r = dm_get_device(ti, dm_shift_arg(as), dm_table_get_mode(ti->table),
			  &p->path.dev);
	if (r) {
		ti->error = "error getting device";
		goto bad;
	}

	q = bdev_get_queue(p->path.dev->bdev);
	attached_handler_name = scsi_dh_attached_handler_name(q, GFP_KERNEL);
	if (attached_handler_name || m->hw_handler_name) {
		INIT_DELAYED_WORK(&p->activate_path, activate_path_work);
		r = setup_scsi_dh(p->path.dev->bdev, m, &attached_handler_name, &ti->error);
		kfree(attached_handler_name);
		if (r) {
			dm_put_device(ti, p->path.dev);
			goto bad;
		}
	}

	r = ps->type->add_path(ps, &p->path, as->argc, as->argv, &ti->error);
	if (r) {
		dm_put_device(ti, p->path.dev);
		goto bad;
	}

	return p;
 bad:
	free_pgpath(p);
	return ERR_PTR(r);
}

static struct priority_group *parse_priority_group(struct dm_arg_set *as,
						   struct multipath *m)
{
	static const struct dm_arg _args[] = {
		{1, 1024, "invalid number of paths"},
		{0, 1024, "invalid number of selector args"}
	};

	int r;
	unsigned int i, nr_selector_args, nr_args;
	struct priority_group *pg;
	struct dm_target *ti = m->ti;

	if (as->argc < 2) {
		as->argc = 0;
		ti->error = "not enough priority group arguments";
		return ERR_PTR(-EINVAL);
	}

	pg = alloc_priority_group();
	if (!pg) {
		ti->error = "couldn't allocate priority group";
		return ERR_PTR(-ENOMEM);
	}
	pg->m = m;

	r = parse_path_selector(as, pg, ti);
	if (r)
		goto bad;

	/*
	 * read the paths
	 */
	r = dm_read_arg(_args, as, &pg->nr_pgpaths, &ti->error);
	if (r)
		goto bad;

	r = dm_read_arg(_args + 1, as, &nr_selector_args, &ti->error);
	if (r)
		goto bad;

	nr_args = 1 + nr_selector_args;
	for (i = 0; i < pg->nr_pgpaths; i++) {
		struct pgpath *pgpath;
		struct dm_arg_set path_args;

		if (as->argc < nr_args) {
			ti->error = "not enough path parameters";
			r = -EINVAL;
			goto bad;
		}

		path_args.argc = nr_args;
		path_args.argv = as->argv;

		pgpath = parse_path(&path_args, &pg->ps, ti);
		if (IS_ERR(pgpath)) {
			r = PTR_ERR(pgpath);
			goto bad;
		}

		pgpath->pg = pg;
		list_add_tail(&pgpath->list, &pg->pgpaths);
		dm_consume_args(as, nr_args);
	}

	return pg;

 bad:
	free_priority_group(pg, ti);
	return ERR_PTR(r);
}

static int parse_hw_handler(struct dm_arg_set *as, struct multipath *m)
{
	unsigned int hw_argc;
	int ret;
	struct dm_target *ti = m->ti;

	static const struct dm_arg _args[] = {
		{0, 1024, "invalid number of hardware handler args"},
	};

	if (dm_read_arg_group(_args, as, &hw_argc, &ti->error))
		return -EINVAL;

	if (!hw_argc)
		return 0;

	if (m->queue_mode == DM_TYPE_BIO_BASED) {
		dm_consume_args(as, hw_argc);
		DMERR("bio-based multipath doesn't allow hardware handler args");
		return 0;
	}

	m->hw_handler_name = kstrdup(dm_shift_arg(as), GFP_KERNEL);
	if (!m->hw_handler_name)
		return -EINVAL;

	if (hw_argc > 1) {
		char *p;
		int i, j, len = 4;

		for (i = 0; i <= hw_argc - 2; i++)
			len += strlen(as->argv[i]) + 1;
		p = m->hw_handler_params = kzalloc(len, GFP_KERNEL);
		if (!p) {
			ti->error = "memory allocation failed";
			ret = -ENOMEM;
			goto fail;
		}
		j = sprintf(p, "%d", hw_argc - 1);
		for (i = 0, p += j + 1; i <= hw_argc - 2; i++, p += j + 1)
			j = sprintf(p, "%s", as->argv[i]);
	}
	dm_consume_args(as, hw_argc - 1);

	return 0;
fail:
	kfree(m->hw_handler_name);
	m->hw_handler_name = NULL;
	return ret;
}

static int parse_features(struct dm_arg_set *as, struct multipath *m)
{
	int r;
	unsigned int argc;
	struct dm_target *ti = m->ti;
	const char *arg_name;

	static const struct dm_arg _args[] = {
		{0, 8, "invalid number of feature args"},
		{1, 50, "pg_init_retries must be between 1 and 50"},
		{0, 60000, "pg_init_delay_msecs must be between 0 and 60000"},
	};

	r = dm_read_arg_group(_args, as, &argc, &ti->error);
	if (r)
		return -EINVAL;

	if (!argc)
		return 0;

	do {
		arg_name = dm_shift_arg(as);
		argc--;

		if (!strcasecmp(arg_name, "queue_if_no_path")) {
			r = queue_if_no_path(m, true, false, __func__);
			continue;
		}

		if (!strcasecmp(arg_name, "retain_attached_hw_handler")) {
			set_bit(MPATHF_RETAIN_ATTACHED_HW_HANDLER, &m->flags);
			continue;
		}

		if (!strcasecmp(arg_name, "pg_init_retries") &&
		    (argc >= 1)) {
			r = dm_read_arg(_args + 1, as, &m->pg_init_retries, &ti->error);
			argc--;
			continue;
		}

		if (!strcasecmp(arg_name, "pg_init_delay_msecs") &&
		    (argc >= 1)) {
			r = dm_read_arg(_args + 2, as, &m->pg_init_delay_msecs, &ti->error);
			argc--;
			continue;
		}

		if (!strcasecmp(arg_name, "queue_mode") &&
		    (argc >= 1)) {
			const char *queue_mode_name = dm_shift_arg(as);

			if (!strcasecmp(queue_mode_name, "bio"))
				m->queue_mode = DM_TYPE_BIO_BASED;
			else if (!strcasecmp(queue_mode_name, "rq") ||
				 !strcasecmp(queue_mode_name, "mq"))
				m->queue_mode = DM_TYPE_REQUEST_BASED;
			else {
				ti->error = "Unknown 'queue_mode' requested";
				r = -EINVAL;
			}
			argc--;
			continue;
		}

		ti->error = "Unrecognised multipath feature request";
		r = -EINVAL;
	} while (argc && !r);

	return r;
}

static int multipath_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	/* target arguments */
	static const struct dm_arg _args[] = {
		{0, 1024, "invalid number of priority groups"},
		{0, 1024, "invalid initial priority group number"},
	};

	int r;
	struct multipath *m;
	struct dm_arg_set as;
	unsigned int pg_count = 0;
	unsigned int next_pg_num;

	as.argc = argc;
	as.argv = argv;

	m = alloc_multipath(ti);
	if (!m) {
		ti->error = "can't allocate multipath";
		return -EINVAL;
	}

	r = parse_features(&as, m);
	if (r)
		goto bad;

	r = alloc_multipath_stage2(ti, m);
	if (r)
		goto bad;

	r = parse_hw_handler(&as, m);
	if (r)
		goto bad;

	r = dm_read_arg(_args, &as, &m->nr_priority_groups, &ti->error);
	if (r)
		goto bad;

	r = dm_read_arg(_args + 1, &as, &next_pg_num, &ti->error);
	if (r)
		goto bad;

	if ((!m->nr_priority_groups && next_pg_num) ||
	    (m->nr_priority_groups && !next_pg_num)) {
		ti->error = "invalid initial priority group";
		r = -EINVAL;
		goto bad;
	}

	/* parse the priority groups */
	while (as.argc) {
		struct priority_group *pg;
		unsigned int nr_valid_paths = atomic_read(&m->nr_valid_paths);

		pg = parse_priority_group(&as, m);
		if (IS_ERR(pg)) {
			r = PTR_ERR(pg);
			goto bad;
		}

		nr_valid_paths += pg->nr_pgpaths;
		atomic_set(&m->nr_valid_paths, nr_valid_paths);

		list_add_tail(&pg->list, &m->priority_groups);
		pg_count++;
		pg->pg_num = pg_count;
		if (!--next_pg_num)
			m->next_pg = pg;
	}

	if (pg_count != m->nr_priority_groups) {
		ti->error = "priority group count mismatch";
		r = -EINVAL;
		goto bad;
	}

	spin_lock_irq(&m->lock);
	enable_nopath_timeout(m);
	spin_unlock_irq(&m->lock);

	ti->num_flush_bios = 1;
	ti->num_discard_bios = 1;
	ti->num_write_zeroes_bios = 1;
	if (m->queue_mode == DM_TYPE_BIO_BASED)
		ti->per_io_data_size = multipath_per_bio_data_size();
	else
		ti->per_io_data_size = sizeof(struct dm_mpath_io);

	return 0;

 bad:
	free_multipath(m);
	return r;
}

static void multipath_wait_for_pg_init_completion(struct multipath *m)
{
	DEFINE_WAIT(wait);

	while (1) {
		prepare_to_wait(&m->pg_init_wait, &wait, TASK_UNINTERRUPTIBLE);

		if (!atomic_read(&m->pg_init_in_progress))
			break;

		io_schedule();
	}
	finish_wait(&m->pg_init_wait, &wait);
}

static void flush_multipath_work(struct multipath *m)
{
	if (m->hw_handler_name) {
		if (!atomic_read(&m->pg_init_in_progress))
			goto skip;

		spin_lock_irq(&m->lock);
		if (atomic_read(&m->pg_init_in_progress) &&
		    !test_and_set_bit(MPATHF_PG_INIT_DISABLED, &m->flags)) {
			spin_unlock_irq(&m->lock);

			flush_workqueue(kmpath_handlerd);
			multipath_wait_for_pg_init_completion(m);

			spin_lock_irq(&m->lock);
			clear_bit(MPATHF_PG_INIT_DISABLED, &m->flags);
		}
		spin_unlock_irq(&m->lock);
	}
skip:
	if (m->queue_mode == DM_TYPE_BIO_BASED)
		flush_work(&m->process_queued_bios);
	flush_work(&m->trigger_event);
}

static void multipath_dtr(struct dm_target *ti)
{
	struct multipath *m = ti->private;

	disable_nopath_timeout(m);
	flush_multipath_work(m);
	free_multipath(m);
}

/*
 * Take a path out of use.
 */
static int fail_path(struct pgpath *pgpath)
{
	unsigned long flags;
	struct multipath *m = pgpath->pg->m;

	spin_lock_irqsave(&m->lock, flags);

	if (!pgpath->is_active)
		goto out;

	DMWARN("%s: Failing path %s.",
	       dm_table_device_name(m->ti->table),
	       pgpath->path.dev->name);

	pgpath->pg->ps.type->fail_path(&pgpath->pg->ps, &pgpath->path);
	pgpath->is_active = false;
	pgpath->fail_count++;

	atomic_dec(&m->nr_valid_paths);

	if (pgpath == m->current_pgpath)
		m->current_pgpath = NULL;

	dm_path_uevent(DM_UEVENT_PATH_FAILED, m->ti,
		       pgpath->path.dev->name, atomic_read(&m->nr_valid_paths));

	queue_work(dm_mpath_wq, &m->trigger_event);

	enable_nopath_timeout(m);

out:
	spin_unlock_irqrestore(&m->lock, flags);

	return 0;
}

/*
 * Reinstate a previously-failed path
 */
static int reinstate_path(struct pgpath *pgpath)
{
	int r = 0, run_queue = 0;
	struct multipath *m = pgpath->pg->m;
	unsigned int nr_valid_paths;

	spin_lock_irq(&m->lock);

	if (pgpath->is_active)
		goto out;

	DMWARN("%s: Reinstating path %s.",
	       dm_table_device_name(m->ti->table),
	       pgpath->path.dev->name);

	r = pgpath->pg->ps.type->reinstate_path(&pgpath->pg->ps, &pgpath->path);
	if (r)
		goto out;

	pgpath->is_active = true;

	nr_valid_paths = atomic_inc_return(&m->nr_valid_paths);
	if (nr_valid_paths == 1) {
		m->current_pgpath = NULL;
		run_queue = 1;
	} else if (m->hw_handler_name && (m->current_pg == pgpath->pg)) {
		if (queue_work(kmpath_handlerd, &pgpath->activate_path.work))
			atomic_inc(&m->pg_init_in_progress);
	}

	dm_path_uevent(DM_UEVENT_PATH_REINSTATED, m->ti,
		       pgpath->path.dev->name, nr_valid_paths);

	schedule_work(&m->trigger_event);

out:
	spin_unlock_irq(&m->lock);
	if (run_queue) {
		dm_table_run_md_queue_async(m->ti->table);
		process_queued_io_list(m);
	}

	if (pgpath->is_active)
		disable_nopath_timeout(m);

	return r;
}

/*
 * Fail or reinstate all paths that match the provided struct dm_dev.
 */
static int action_dev(struct multipath *m, dev_t dev, action_fn action)
{
	int r = -EINVAL;
	struct pgpath *pgpath;
	struct priority_group *pg;

	list_for_each_entry(pg, &m->priority_groups, list) {
		list_for_each_entry(pgpath, &pg->pgpaths, list) {
			if (pgpath->path.dev->bdev->bd_dev == dev)
				r = action(pgpath);
		}
	}

	return r;
}

/*
 * Temporarily try to avoid having to use the specified PG
 */
static void bypass_pg(struct multipath *m, struct priority_group *pg,
		      bool bypassed, bool can_be_delayed)
{
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);

	pg->bypassed = bypassed;
	if (can_be_delayed && test_bit(MPATHF_DELAY_PG_SWITCH, &m->flags))
		set_bit(MPATHF_NEED_PG_SWITCH, &m->flags);
	else {
		m->current_pgpath = NULL;
		m->current_pg = NULL;
	}

	spin_unlock_irqrestore(&m->lock, flags);

	schedule_work(&m->trigger_event);
}

/*
 * Switch to using the specified PG from the next I/O that gets mapped
 */
static int switch_pg_num(struct multipath *m, const char *pgstr)
{
	struct priority_group *pg;
	unsigned int pgnum;
	char dummy;

	if (!pgstr || (sscanf(pgstr, "%u%c", &pgnum, &dummy) != 1) || !pgnum ||
	    !m->nr_priority_groups || (pgnum > m->nr_priority_groups)) {
		DMWARN("invalid PG number supplied to %s", __func__);
		return -EINVAL;
	}

	spin_lock_irq(&m->lock);
	list_for_each_entry(pg, &m->priority_groups, list) {
		pg->bypassed = false;
		if (--pgnum)
			continue;

		if (test_bit(MPATHF_DELAY_PG_SWITCH, &m->flags))
			set_bit(MPATHF_NEED_PG_SWITCH, &m->flags);
		else {
			m->current_pgpath = NULL;
			m->current_pg = NULL;
		}
		m->next_pg = pg;
	}
	spin_unlock_irq(&m->lock);

	schedule_work(&m->trigger_event);
	return 0;
}

/*
 * Set/clear bypassed status of a PG.
 * PGs are numbered upwards from 1 in the order they were declared.
 */
static int bypass_pg_num(struct multipath *m, const char *pgstr, bool bypassed)
{
	struct priority_group *pg;
	unsigned int pgnum;
	char dummy;

	if (!pgstr || (sscanf(pgstr, "%u%c", &pgnum, &dummy) != 1) || !pgnum ||
	    !m->nr_priority_groups || (pgnum > m->nr_priority_groups)) {
		DMWARN("invalid PG number supplied to bypass_pg");
		return -EINVAL;
	}

	list_for_each_entry(pg, &m->priority_groups, list) {
		if (!--pgnum)
			break;
	}

	bypass_pg(m, pg, bypassed, true);
	return 0;
}

/*
 * Should we retry pg_init immediately?
 */
static bool pg_init_limit_reached(struct multipath *m, struct pgpath *pgpath)
{
	unsigned long flags;
	bool limit_reached = false;

	spin_lock_irqsave(&m->lock, flags);

	if (atomic_read(&m->pg_init_count) <= m->pg_init_retries &&
	    !test_bit(MPATHF_PG_INIT_DISABLED, &m->flags))
		set_bit(MPATHF_PG_INIT_REQUIRED, &m->flags);
	else
		limit_reached = true;

	spin_unlock_irqrestore(&m->lock, flags);

	return limit_reached;
}

static void pg_init_done(void *data, int errors)
{
	struct pgpath *pgpath = data;
	struct priority_group *pg = pgpath->pg;
	struct multipath *m = pg->m;
	unsigned long flags;
	bool delay_retry = false;

	/* device or driver problems */
	switch (errors) {
	case SCSI_DH_OK:
		break;
	case SCSI_DH_NOSYS:
		if (!m->hw_handler_name) {
			errors = 0;
			break;
		}
		DMERR("Could not failover the device: Handler scsi_dh_%s "
		      "Error %d.", m->hw_handler_name, errors);
		/*
		 * Fail path for now, so we do not ping pong
		 */
		fail_path(pgpath);
		break;
	case SCSI_DH_DEV_TEMP_BUSY:
		/*
		 * Probably doing something like FW upgrade on the
		 * controller so try the other pg.
		 */
		bypass_pg(m, pg, true, false);
		break;
	case SCSI_DH_RETRY:
		/* Wait before retrying. */
		delay_retry = true;
		fallthrough;
	case SCSI_DH_IMM_RETRY:
	case SCSI_DH_RES_TEMP_UNAVAIL:
		if (pg_init_limit_reached(m, pgpath))
			fail_path(pgpath);
		errors = 0;
		break;
	case SCSI_DH_DEV_OFFLINED:
	default:
		/*
		 * We probably do not want to fail the path for a device
		 * error, but this is what the old dm did. In future
		 * patches we can do more advanced handling.
		 */
		fail_path(pgpath);
	}

	spin_lock_irqsave(&m->lock, flags);
	if (errors) {
		if (pgpath == m->current_pgpath) {
			DMERR("Could not failover device. Error %d.", errors);
			m->current_pgpath = NULL;
			m->current_pg = NULL;
		}
	} else if (!test_bit(MPATHF_PG_INIT_REQUIRED, &m->flags))
		pg->bypassed = false;

	if (atomic_dec_return(&m->pg_init_in_progress) > 0)
		/* Activations of other paths are still on going */
		goto out;

	if (test_bit(MPATHF_PG_INIT_REQUIRED, &m->flags)) {
		if (delay_retry)
			set_bit(MPATHF_PG_INIT_DELAY_RETRY, &m->flags);
		else
			clear_bit(MPATHF_PG_INIT_DELAY_RETRY, &m->flags);

		if (__pg_init_all_paths(m))
			goto out;
	}
	clear_bit(MPATHF_QUEUE_IO, &m->flags);

	process_queued_io_list(m);

	/*
	 * Wake up any thread waiting to suspend.
	 */
	wake_up(&m->pg_init_wait);

out:
	spin_unlock_irqrestore(&m->lock, flags);
}

static void activate_or_offline_path(struct pgpath *pgpath)
{
	struct request_queue *q = bdev_get_queue(pgpath->path.dev->bdev);

	if (pgpath->is_active && !blk_queue_dying(q))
		scsi_dh_activate(q, pg_init_done, pgpath);
	else
		pg_init_done(pgpath, SCSI_DH_DEV_OFFLINED);
}

static void activate_path_work(struct work_struct *work)
{
	struct pgpath *pgpath =
		container_of(work, struct pgpath, activate_path.work);

	activate_or_offline_path(pgpath);
}

static int multipath_end_io(struct dm_target *ti, struct request *clone,
			    blk_status_t error, union map_info *map_context)
{
	struct dm_mpath_io *mpio = get_mpio(map_context);
	struct pgpath *pgpath = mpio->pgpath;
	int r = DM_ENDIO_DONE;

	/*
	 * We don't queue any clone request inside the multipath target
	 * during end I/O handling, since those clone requests don't have
	 * bio clones.  If we queue them inside the multipath target,
	 * we need to make bio clones, that requires memory allocation.
	 * (See drivers/md/dm-rq.c:end_clone_bio() about why the clone requests
	 *  don't have bio clones.)
	 * Instead of queueing the clone request here, we queue the original
	 * request into dm core, which will remake a clone request and
	 * clone bios for it and resubmit it later.
	 */
	if (error && blk_path_error(error)) {
		struct multipath *m = ti->private;

		if (error == BLK_STS_RESOURCE)
			r = DM_ENDIO_DELAY_REQUEUE;
		else
			r = DM_ENDIO_REQUEUE;

		if (pgpath)
			fail_path(pgpath);

		if (!atomic_read(&m->nr_valid_paths) &&
		    !must_push_back_rq(m)) {
			if (error == BLK_STS_IOERR)
				dm_report_EIO(m);
			/* complete with the original error */
			r = DM_ENDIO_DONE;
		}
	}

	if (pgpath) {
		struct path_selector *ps = &pgpath->pg->ps;

		if (ps->type->end_io)
			ps->type->end_io(ps, &pgpath->path, mpio->nr_bytes,
					 clone->io_start_time_ns);
	}

	return r;
}

static int multipath_end_io_bio(struct dm_target *ti, struct bio *clone,
				blk_status_t *error)
{
	struct multipath *m = ti->private;
	struct dm_mpath_io *mpio = get_mpio_from_bio(clone);
	struct pgpath *pgpath = mpio->pgpath;
	unsigned long flags;
	int r = DM_ENDIO_DONE;

	if (!*error || !blk_path_error(*error))
		goto done;

	if (pgpath)
		fail_path(pgpath);

	if (!atomic_read(&m->nr_valid_paths)) {
		spin_lock_irqsave(&m->lock, flags);
		if (!test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags)) {
			if (__must_push_back(m)) {
				r = DM_ENDIO_REQUEUE;
			} else {
				dm_report_EIO(m);
				*error = BLK_STS_IOERR;
			}
			spin_unlock_irqrestore(&m->lock, flags);
			goto done;
		}
		spin_unlock_irqrestore(&m->lock, flags);
	}

	multipath_queue_bio(m, clone);
	r = DM_ENDIO_INCOMPLETE;
done:
	if (pgpath) {
		struct path_selector *ps = &pgpath->pg->ps;

		if (ps->type->end_io)
			ps->type->end_io(ps, &pgpath->path, mpio->nr_bytes,
					 (mpio->start_time_ns ?:
					  dm_start_time_ns_from_clone(clone)));
	}

	return r;
}

/*
 * Suspend with flush can't complete until all the I/O is processed
 * so if the last path fails we must error any remaining I/O.
 * - Note that if the freeze_bdev fails while suspending, the
 *   queue_if_no_path state is lost - userspace should reset it.
 * Otherwise, during noflush suspend, queue_if_no_path will not change.
 */
static void multipath_presuspend(struct dm_target *ti)
{
	struct multipath *m = ti->private;

	spin_lock_irq(&m->lock);
	m->is_suspending = true;
	spin_unlock_irq(&m->lock);
	/* FIXME: bio-based shouldn't need to always disable queue_if_no_path */
	if (m->queue_mode == DM_TYPE_BIO_BASED || !dm_noflush_suspending(m->ti))
		queue_if_no_path(m, false, true, __func__);
}

static void multipath_postsuspend(struct dm_target *ti)
{
	struct multipath *m = ti->private;

	mutex_lock(&m->work_mutex);
	flush_multipath_work(m);
	mutex_unlock(&m->work_mutex);
}

/*
 * Restore the queue_if_no_path setting.
 */
static void multipath_resume(struct dm_target *ti)
{
	struct multipath *m = ti->private;

	spin_lock_irq(&m->lock);
	m->is_suspending = false;
	if (test_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags)) {
		set_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags);
		clear_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags);
	}

	DMDEBUG("%s: %s finished; QIFNP = %d; SQIFNP = %d",
		dm_table_device_name(m->ti->table), __func__,
		test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags),
		test_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags));

	spin_unlock_irq(&m->lock);
}

/*
 * Info output has the following format:
 * num_multipath_feature_args [multipath_feature_args]*
 * num_handler_status_args [handler_status_args]*
 * num_groups init_group_number
 *            [A|D|E num_ps_status_args [ps_status_args]*
 *             num_paths num_selector_args
 *             [path_dev A|F fail_count [selector_args]* ]+ ]+
 *
 * Table output has the following format (identical to the constructor string):
 * num_feature_args [features_args]*
 * num_handler_args hw_handler [hw_handler_args]*
 * num_groups init_group_number
 *     [priority selector-name num_ps_args [ps_args]*
 *      num_paths num_selector_args [path_dev [selector_args]* ]+ ]+
 */
static void multipath_status(struct dm_target *ti, status_type_t type,
			     unsigned int status_flags, char *result, unsigned int maxlen)
{
	int sz = 0, pg_counter, pgpath_counter;
	struct multipath *m = ti->private;
	struct priority_group *pg;
	struct pgpath *p;
	unsigned int pg_num;
	char state;

	spin_lock_irq(&m->lock);

	/* Features */
	if (type == STATUSTYPE_INFO)
		DMEMIT("2 %u %u ", test_bit(MPATHF_QUEUE_IO, &m->flags),
		       atomic_read(&m->pg_init_count));
	else {
		DMEMIT("%u ", test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags) +
			      (m->pg_init_retries > 0) * 2 +
			      (m->pg_init_delay_msecs != DM_PG_INIT_DELAY_DEFAULT) * 2 +
			      test_bit(MPATHF_RETAIN_ATTACHED_HW_HANDLER, &m->flags) +
			      (m->queue_mode != DM_TYPE_REQUEST_BASED) * 2);

		if (test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags))
			DMEMIT("queue_if_no_path ");
		if (m->pg_init_retries)
			DMEMIT("pg_init_retries %u ", m->pg_init_retries);
		if (m->pg_init_delay_msecs != DM_PG_INIT_DELAY_DEFAULT)
			DMEMIT("pg_init_delay_msecs %u ", m->pg_init_delay_msecs);
		if (test_bit(MPATHF_RETAIN_ATTACHED_HW_HANDLER, &m->flags))
			DMEMIT("retain_attached_hw_handler ");
		if (m->queue_mode != DM_TYPE_REQUEST_BASED) {
			switch (m->queue_mode) {
			case DM_TYPE_BIO_BASED:
				DMEMIT("queue_mode bio ");
				break;
			default:
				WARN_ON_ONCE(true);
				break;
			}
		}
	}

	if (!m->hw_handler_name || type == STATUSTYPE_INFO)
		DMEMIT("0 ");
	else
		DMEMIT("1 %s ", m->hw_handler_name);

	DMEMIT("%u ", m->nr_priority_groups);

	if (m->current_pg)
		pg_num = m->current_pg->pg_num;
	else if (m->next_pg)
		pg_num = m->next_pg->pg_num;
	else
		pg_num = (m->nr_priority_groups ? 1 : 0);

	DMEMIT("%u ", pg_num);

	switch (type) {
	case STATUSTYPE_INFO:
		list_for_each_entry(pg, &m->priority_groups, list) {
			if (pg->bypassed)
				state = 'D';	/* Disabled */
			else if (pg == m->current_pg)
				state = 'A';	/* Currently Active */
			else
				state = 'E';	/* Enabled */

			DMEMIT("%c ", state);

			if (pg->ps.type->status)
				sz += pg->ps.type->status(&pg->ps, NULL, type,
							  result + sz,
							  maxlen - sz);
			else
				DMEMIT("0 ");

			DMEMIT("%u %u ", pg->nr_pgpaths,
			       pg->ps.type->info_args);

			list_for_each_entry(p, &pg->pgpaths, list) {
				DMEMIT("%s %s %u ", p->path.dev->name,
				       p->is_active ? "A" : "F",
				       p->fail_count);
				if (pg->ps.type->status)
					sz += pg->ps.type->status(&pg->ps,
					      &p->path, type, result + sz,
					      maxlen - sz);
			}
		}
		break;

	case STATUSTYPE_TABLE:
		list_for_each_entry(pg, &m->priority_groups, list) {
			DMEMIT("%s ", pg->ps.type->name);

			if (pg->ps.type->status)
				sz += pg->ps.type->status(&pg->ps, NULL, type,
							  result + sz,
							  maxlen - sz);
			else
				DMEMIT("0 ");

			DMEMIT("%u %u ", pg->nr_pgpaths,
			       pg->ps.type->table_args);

			list_for_each_entry(p, &pg->pgpaths, list) {
				DMEMIT("%s ", p->path.dev->name);
				if (pg->ps.type->status)
					sz += pg->ps.type->status(&pg->ps,
					      &p->path, type, result + sz,
					      maxlen - sz);
			}
		}
		break;

	case STATUSTYPE_IMA:
		sz = 0; /*reset the result pointer*/

		DMEMIT_TARGET_NAME_VERSION(ti->type);
		DMEMIT(",nr_priority_groups=%u", m->nr_priority_groups);

		pg_counter = 0;
		list_for_each_entry(pg, &m->priority_groups, list) {
			if (pg->bypassed)
				state = 'D';	/* Disabled */
			else if (pg == m->current_pg)
				state = 'A';	/* Currently Active */
			else
				state = 'E';	/* Enabled */
			DMEMIT(",pg_state_%d=%c", pg_counter, state);
			DMEMIT(",nr_pgpaths_%d=%u", pg_counter, pg->nr_pgpaths);
			DMEMIT(",path_selector_name_%d=%s", pg_counter, pg->ps.type->name);

			pgpath_counter = 0;
			list_for_each_entry(p, &pg->pgpaths, list) {
				DMEMIT(",path_name_%d_%d=%s,is_active_%d_%d=%c,fail_count_%d_%d=%u",
				       pg_counter, pgpath_counter, p->path.dev->name,
				       pg_counter, pgpath_counter, p->is_active ? 'A' : 'F',
				       pg_counter, pgpath_counter, p->fail_count);
				if (pg->ps.type->status) {
					DMEMIT(",path_selector_status_%d_%d=",
					       pg_counter, pgpath_counter);
					sz += pg->ps.type->status(&pg->ps, &p->path,
								  type, result + sz,
								  maxlen - sz);
				}
				pgpath_counter++;
			}
			pg_counter++;
		}
		DMEMIT(";");
		break;
	}

	spin_unlock_irq(&m->lock);
}

static int multipath_message(struct dm_target *ti, unsigned int argc, char **argv,
			     char *result, unsigned int maxlen)
{
	int r = -EINVAL;
	dev_t dev;
	struct multipath *m = ti->private;
	action_fn action;

	mutex_lock(&m->work_mutex);

	if (dm_suspended(ti)) {
		r = -EBUSY;
		goto out;
	}

	if (argc == 1) {
		if (!strcasecmp(argv[0], "queue_if_no_path")) {
			r = queue_if_no_path(m, true, false, __func__);
			spin_lock_irq(&m->lock);
			enable_nopath_timeout(m);
			spin_unlock_irq(&m->lock);
			goto out;
		} else if (!strcasecmp(argv[0], "fail_if_no_path")) {
			r = queue_if_no_path(m, false, false, __func__);
			disable_nopath_timeout(m);
			goto out;
		}
	}

	if (argc != 2) {
		DMWARN("Invalid multipath message arguments. Expected 2 arguments, got %d.", argc);
		goto out;
	}

	if (!strcasecmp(argv[0], "disable_group")) {
		r = bypass_pg_num(m, argv[1], true);
		goto out;
	} else if (!strcasecmp(argv[0], "enable_group")) {
		r = bypass_pg_num(m, argv[1], false);
		goto out;
	} else if (!strcasecmp(argv[0], "switch_group")) {
		r = switch_pg_num(m, argv[1]);
		goto out;
	} else if (!strcasecmp(argv[0], "reinstate_path"))
		action = reinstate_path;
	else if (!strcasecmp(argv[0], "fail_path"))
		action = fail_path;
	else {
		DMWARN("Unrecognised multipath message received: %s", argv[0]);
		goto out;
	}

	r = dm_devt_from_path(argv[1], &dev);
	if (r) {
		DMWARN("message: error getting device %s",
		       argv[1]);
		goto out;
	}

	r = action_dev(m, dev, action);

out:
	mutex_unlock(&m->work_mutex);
	return r;
}

/*
 * Perform a minimal read from the given path to find out whether the
 * path still works.  If a path error occurs, fail it.
 */
static int probe_path(struct pgpath *pgpath)
{
	struct block_device *bdev = pgpath->path.dev->bdev;
	unsigned int read_size = bdev_logical_block_size(bdev);
	struct page *page;
	struct bio *bio;
	blk_status_t status;
	int r = 0;

	if (WARN_ON_ONCE(read_size > PAGE_SIZE))
		return -EINVAL;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	/* Perform a minimal read: Sector 0, length read_size */
	bio = bio_alloc(bdev, 1, REQ_OP_READ, GFP_KERNEL);
	if (!bio) {
		r = -ENOMEM;
		goto out;
	}

	bio->bi_iter.bi_sector = 0;
	__bio_add_page(bio, page, read_size, 0);
	submit_bio_wait(bio);
	status = bio->bi_status;
	bio_put(bio);

	if (status && blk_path_error(status))
		fail_path(pgpath);

out:
	__free_page(page);
	return r;
}

/*
 * Probe all active paths in current_pg to find out whether they still work.
 * Fail all paths that do not work.
 *
 * Return -ENOTCONN if no valid path is left (even outside of current_pg). We
 * cannot probe paths in other pgs without switching current_pg, so if valid
 * paths are only in different pgs, they may or may not work. Additionally
 * we should not probe paths in a pathgroup that is in the process of
 * Initializing. Userspace can submit a request and we'll switch and wait
 * for the pathgroup to be initialized. If the request fails, it may need to
 * probe again.
 */
static int probe_active_paths(struct multipath *m)
{
	struct pgpath *pgpath;
	struct priority_group *pg = NULL;
	int r = 0;

	spin_lock_irq(&m->lock);
	if (test_bit(MPATHF_DELAY_PG_SWITCH, &m->flags)) {
		wait_event_lock_irq(m->probe_wait,
				    !test_bit(MPATHF_DELAY_PG_SWITCH, &m->flags),
				    m->lock);
		/*
		 * if we waited because a probe was already in progress,
		 * and it probed the current active pathgroup, don't
		 * reprobe. Just return the number of valid paths
		 */
		if (m->current_pg == m->last_probed_pg)
			goto skip_probe;
	}
	if (!m->current_pg || m->is_suspending ||
	    test_bit(MPATHF_QUEUE_IO, &m->flags))
		goto skip_probe;
	set_bit(MPATHF_DELAY_PG_SWITCH, &m->flags);
	pg = m->last_probed_pg = m->current_pg;
	spin_unlock_irq(&m->lock);

	list_for_each_entry(pgpath, &pg->pgpaths, list) {
		if (pg != READ_ONCE(m->current_pg) ||
		    READ_ONCE(m->is_suspending))
			goto out;
		if (!pgpath->is_active)
			continue;

		r = probe_path(pgpath);
		if (r < 0)
			goto out;
	}

out:
	spin_lock_irq(&m->lock);
	clear_bit(MPATHF_DELAY_PG_SWITCH, &m->flags);
	if (test_and_clear_bit(MPATHF_NEED_PG_SWITCH, &m->flags)) {
		m->current_pgpath = NULL;
		m->current_pg = NULL;
	}
skip_probe:
	if (r == 0 && !atomic_read(&m->nr_valid_paths))
		r = -ENOTCONN;
	spin_unlock_irq(&m->lock);
	if (pg)
		wake_up(&m->probe_wait);
	return r;
}

static int multipath_prepare_ioctl(struct dm_target *ti,
				   struct block_device **bdev,
				   unsigned int cmd, unsigned long arg,
				   bool *forward)
{
	struct multipath *m = ti->private;
	struct pgpath *pgpath;
	int r;

	if (_IOC_TYPE(cmd) == DM_IOCTL) {
		*forward = false;
		switch (cmd) {
		case DM_MPATH_PROBE_PATHS:
			return probe_active_paths(m);
		default:
			return -ENOTTY;
		}
	}

	pgpath = READ_ONCE(m->current_pgpath);
	if (!pgpath || !mpath_double_check_test_bit(MPATHF_QUEUE_IO, m))
		pgpath = choose_pgpath(m, 0);

	if (pgpath) {
		if (!mpath_double_check_test_bit(MPATHF_QUEUE_IO, m)) {
			*bdev = pgpath->path.dev->bdev;
			r = 0;
		} else {
			/* pg_init has not started or completed */
			r = -ENOTCONN;
		}
	} else {
		/* No path is available */
		r = -EIO;
		spin_lock_irq(&m->lock);
		if (test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags))
			r = -ENOTCONN;
		spin_unlock_irq(&m->lock);
	}

	if (r == -ENOTCONN) {
		if (!READ_ONCE(m->current_pg)) {
			/* Path status changed, redo selection */
			(void) choose_pgpath(m, 0);
		}
		spin_lock_irq(&m->lock);
		if (test_bit(MPATHF_PG_INIT_REQUIRED, &m->flags))
			(void) __pg_init_all_paths(m);
		spin_unlock_irq(&m->lock);
		dm_table_run_md_queue_async(m->ti->table);
		process_queued_io_list(m);
	}

	/*
	 * Only pass ioctls through if the device sizes match exactly.
	 */
	if (!r && ti->len != bdev_nr_sectors((*bdev)))
		return 1;
	return r;
}

static int multipath_iterate_devices(struct dm_target *ti,
				     iterate_devices_callout_fn fn, void *data)
{
	struct multipath *m = ti->private;
	struct priority_group *pg;
	struct pgpath *p;
	int ret = 0;

	list_for_each_entry(pg, &m->priority_groups, list) {
		list_for_each_entry(p, &pg->pgpaths, list) {
			ret = fn(ti, p->path.dev, ti->begin, ti->len, data);
			if (ret)
				goto out;
		}
	}

out:
	return ret;
}

static int pgpath_busy(struct pgpath *pgpath)
{
	struct request_queue *q = bdev_get_queue(pgpath->path.dev->bdev);

	return blk_lld_busy(q);
}

/*
 * We return "busy", only when we can map I/Os but underlying devices
 * are busy (so even if we map I/Os now, the I/Os will wait on
 * the underlying queue).
 * In other words, if we want to kill I/Os or queue them inside us
 * due to map unavailability, we don't return "busy".  Otherwise,
 * dm core won't give us the I/Os and we can't do what we want.
 */
static int multipath_busy(struct dm_target *ti)
{
	bool busy = false, has_active = false;
	struct multipath *m = ti->private;
	struct priority_group *pg, *next_pg;
	struct pgpath *pgpath;

	/* pg_init in progress */
	if (atomic_read(&m->pg_init_in_progress))
		return true;

	/* no paths available, for blk-mq: rely on IO mapping to delay requeue */
	if (!atomic_read(&m->nr_valid_paths)) {
		unsigned long flags;

		spin_lock_irqsave(&m->lock, flags);
		if (test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags)) {
			spin_unlock_irqrestore(&m->lock, flags);
			return (m->queue_mode != DM_TYPE_REQUEST_BASED);
		}
		spin_unlock_irqrestore(&m->lock, flags);
	}

	/* Guess which priority_group will be used at next mapping time */
	pg = READ_ONCE(m->current_pg);
	next_pg = READ_ONCE(m->next_pg);
	if (unlikely(!READ_ONCE(m->current_pgpath) && next_pg))
		pg = next_pg;

	if (!pg) {
		/*
		 * We don't know which pg will be used at next mapping time.
		 * We don't call choose_pgpath() here to avoid to trigger
		 * pg_init just by busy checking.
		 * So we don't know whether underlying devices we will be using
		 * at next mapping time are busy or not. Just try mapping.
		 */
		return busy;
	}

	/*
	 * If there is one non-busy active path at least, the path selector
	 * will be able to select it. So we consider such a pg as not busy.
	 */
	busy = true;
	list_for_each_entry(pgpath, &pg->pgpaths, list) {
		if (pgpath->is_active) {
			has_active = true;
			if (!pgpath_busy(pgpath)) {
				busy = false;
				break;
			}
		}
	}

	if (!has_active) {
		/*
		 * No active path in this pg, so this pg won't be used and
		 * the current_pg will be changed at next mapping time.
		 * We need to try mapping to determine it.
		 */
		busy = false;
	}

	return busy;
}

/*
 *---------------------------------------------------------------
 * Module setup
 *---------------------------------------------------------------
 */
static struct target_type multipath_target = {
	.name = "multipath",
	.version = {1, 15, 0},
	.features = DM_TARGET_SINGLETON | DM_TARGET_IMMUTABLE |
		    DM_TARGET_PASSES_INTEGRITY,
	.module = THIS_MODULE,
	.ctr = multipath_ctr,
	.dtr = multipath_dtr,
	.clone_and_map_rq = multipath_clone_and_map,
	.release_clone_rq = multipath_release_clone,
	.rq_end_io = multipath_end_io,
	.map = multipath_map_bio,
	.end_io = multipath_end_io_bio,
	.presuspend = multipath_presuspend,
	.postsuspend = multipath_postsuspend,
	.resume = multipath_resume,
	.status = multipath_status,
	.message = multipath_message,
	.prepare_ioctl = multipath_prepare_ioctl,
	.iterate_devices = multipath_iterate_devices,
	.busy = multipath_busy,
};

static int __init dm_multipath_init(void)
{
	int r = -ENOMEM;

	kmultipathd = alloc_workqueue("kmpathd", WQ_MEM_RECLAIM, 0);
	if (!kmultipathd) {
		DMERR("failed to create workqueue kmpathd");
		goto bad_alloc_kmultipathd;
	}

	/*
	 * A separate workqueue is used to handle the device handlers
	 * to avoid overloading existing workqueue. Overloading the
	 * old workqueue would also create a bottleneck in the
	 * path of the storage hardware device activation.
	 */
	kmpath_handlerd = alloc_ordered_workqueue("kmpath_handlerd",
						  WQ_MEM_RECLAIM);
	if (!kmpath_handlerd) {
		DMERR("failed to create workqueue kmpath_handlerd");
		goto bad_alloc_kmpath_handlerd;
	}

	dm_mpath_wq = alloc_workqueue("dm_mpath_wq", 0, 0);
	if (!dm_mpath_wq) {
		DMERR("failed to create workqueue dm_mpath_wq");
		goto bad_alloc_dm_mpath_wq;
	}

	r = dm_register_target(&multipath_target);
	if (r < 0)
		goto bad_register_target;

	return 0;

bad_register_target:
	destroy_workqueue(dm_mpath_wq);
bad_alloc_dm_mpath_wq:
	destroy_workqueue(kmpath_handlerd);
bad_alloc_kmpath_handlerd:
	destroy_workqueue(kmultipathd);
bad_alloc_kmultipathd:
	return r;
}

static void __exit dm_multipath_exit(void)
{
	destroy_workqueue(dm_mpath_wq);
	destroy_workqueue(kmpath_handlerd);
	destroy_workqueue(kmultipathd);

	dm_unregister_target(&multipath_target);
}

module_init(dm_multipath_init);
module_exit(dm_multipath_exit);

module_param_named(queue_if_no_path_timeout_secs, queue_if_no_path_timeout_secs, ulong, 0644);
MODULE_PARM_DESC(queue_if_no_path_timeout_secs, "No available paths queue IO timeout in seconds");

MODULE_DESCRIPTION(DM_NAME " multipath target");
MODULE_AUTHOR("Sistina Software <dm-devel@lists.linux.dev>");
MODULE_LICENSE("GPL");
