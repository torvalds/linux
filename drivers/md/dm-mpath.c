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
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <scsi/scsi_dh.h>
#include <linux/atomic.h>
#include <linux/blk-mq.h>

#define DM_MSG_PREFIX "multipath"
#define DM_PG_INIT_DELAY_MSECS 2000
#define DM_PG_INIT_DELAY_DEFAULT ((unsigned) -1)

/* Path properties */
struct pgpath {
	struct list_head list;

	struct priority_group *pg;	/* Owning PG */
	unsigned fail_count;		/* Cumulative failure count */

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

	unsigned pg_num;		/* Reference number */
	unsigned nr_pgpaths;		/* Number of paths in PG */
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

	atomic_t nr_valid_paths;	/* Total number of usable paths */
	unsigned nr_priority_groups;
	struct list_head priority_groups;

	const char *hw_handler_name;
	char *hw_handler_params;
	wait_queue_head_t pg_init_wait;	/* Wait for pg_init completion */
	unsigned pg_init_retries;	/* Number of times to retry pg_init */
	unsigned pg_init_delay_msecs;	/* Number of msecs before pg_init retry */
	atomic_t pg_init_in_progress;	/* Only one pg_init allowed at once */
	atomic_t pg_init_count;		/* Number of times pg_init called */

	struct mutex work_mutex;
	struct work_struct trigger_event;
	struct dm_target *ti;

	struct work_struct process_queued_bios;
	struct bio_list queued_bios;
};

/*
 * Context information attached to each io we process.
 */
struct dm_mpath_io {
	struct pgpath *pgpath;
	size_t nr_bytes;
};

typedef int (*action_fn) (struct pgpath *pgpath);

static struct workqueue_struct *kmultipathd, *kmpath_handlerd;
static void trigger_event(struct work_struct *work);
static void activate_or_offline_path(struct pgpath *pgpath);
static void activate_path_work(struct work_struct *work);
static void process_queued_bios(struct work_struct *work);

/*-----------------------------------------------
 * Multipath state flags.
 *-----------------------------------------------*/

#define MPATHF_QUEUE_IO 0			/* Must we queue all I/O? */
#define MPATHF_QUEUE_IF_NO_PATH 1		/* Queue I/O if last path fails? */
#define MPATHF_SAVED_QUEUE_IF_NO_PATH 2		/* Saved state during suspension */
#define MPATHF_RETAIN_ATTACHED_HW_HANDLER 3	/* If there's already a hw_handler present, don't change it. */
#define MPATHF_PG_INIT_DISABLED 4		/* pg_init is not currently allowed */
#define MPATHF_PG_INIT_REQUIRED 5		/* pg_init needs calling? */
#define MPATHF_PG_INIT_DELAY_RETRY 6		/* Delay pg_init retry? */

/*-----------------------------------------------
 * Allocation routines
 *-----------------------------------------------*/

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
	*mpio_p = mpio;

	dm_bio_record(bio_details, bio);
}

/*-----------------------------------------------
 * Path selection
 *-----------------------------------------------*/

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
	unsigned bypassed = 1;

	if (!atomic_read(&m->nr_valid_paths)) {
		clear_bit(MPATHF_QUEUE_IO, &m->flags);
		goto failed;
	}

	/* Were we instructed to switch PG? */
	if (READ_ONCE(m->next_pg)) {
		spin_lock_irqsave(&m->lock, flags);
		pg = m->next_pg;
		if (!pg) {
			spin_unlock_irqrestore(&m->lock, flags);
			goto check_current_pg;
		}
		m->next_pg = NULL;
		spin_unlock_irqrestore(&m->lock, flags);
		pgpath = choose_path_in_pg(m, pg, nr_bytes);
		if (!IS_ERR_OR_NULL(pgpath))
			return pgpath;
	}

	/* Don't change PG until it has no remaining paths */
check_current_pg:
	pg = READ_ONCE(m->current_pg);
	if (pg) {
		pgpath = choose_path_in_pg(m, pg, nr_bytes);
		if (!IS_ERR_OR_NULL(pgpath))
			return pgpath;
	}

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
				if (!bypassed)
					set_bit(MPATHF_PG_INIT_DELAY_RETRY, &m->flags);
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
 * dm_report_EIO() is a macro instead of a function to make pr_debug()
 * report the function name and line number of the function from which
 * it has been invoked.
 */
#define dm_report_EIO(m)						\
do {									\
	struct mapped_device *md = dm_table_get_md((m)->ti->table);	\
									\
	pr_debug("%s: returning EIO; QIFNP = %d; SQIFNP = %d; DNFS = %d\n", \
		 dm_device_name(md),					\
		 test_bit(MPATHF_QUEUE_IF_NO_PATH, &(m)->flags),	\
		 test_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &(m)->flags),	\
		 dm_noflush_suspending((m)->ti));			\
} while (0)

/*
 * Check whether bios must be queued in the device-mapper core rather
 * than here in the target.
 *
 * If MPATHF_QUEUE_IF_NO_PATH and MPATHF_SAVED_QUEUE_IF_NO_PATH hold
 * the same value then we are not between multipath_presuspend()
 * and multipath_resume() calls and we have no need to check
 * for the DMF_NOFLUSH_SUSPENDING flag.
 */
static bool __must_push_back(struct multipath *m, unsigned long flags)
{
	return ((test_bit(MPATHF_QUEUE_IF_NO_PATH, &flags) !=
		 test_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &flags)) &&
		dm_noflush_suspending(m->ti));
}

/*
 * Following functions use READ_ONCE to get atomic access to
 * all m->flags to avoid taking spinlock
 */
static bool must_push_back_rq(struct multipath *m)
{
	unsigned long flags = READ_ONCE(m->flags);
	return test_bit(MPATHF_QUEUE_IF_NO_PATH, &flags) || __must_push_back(m, flags);
}

static bool must_push_back_bio(struct multipath *m)
{
	unsigned long flags = READ_ONCE(m->flags);
	return __must_push_back(m, flags);
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
	if (!pgpath || !test_bit(MPATHF_QUEUE_IO, &m->flags))
		pgpath = choose_pgpath(m, nr_bytes);

	if (!pgpath) {
		if (must_push_back_rq(m))
			return DM_MAPIO_DELAY_REQUEUE;
		dm_report_EIO(m);	/* Failed */
		return DM_MAPIO_KILL;
	} else if (test_bit(MPATHF_QUEUE_IO, &m->flags) ||
		   test_bit(MPATHF_PG_INIT_REQUIRED, &m->flags)) {
		pg_init_all_paths(m);
		return DM_MAPIO_DELAY_REQUEUE;
	}

	mpio->pgpath = pgpath;
	mpio->nr_bytes = nr_bytes;

	bdev = pgpath->path.dev->bdev;
	q = bdev_get_queue(bdev);
	clone = blk_get_request(q, rq->cmd_flags | REQ_NOMERGE,
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
	clone->rq_disk = bdev->bd_disk;
	clone->cmd_flags |= REQ_FAILFAST_TRANSPORT;
	*__clone = clone;

	if (pgpath->pg->ps.type->start_io)
		pgpath->pg->ps.type->start_io(&pgpath->pg->ps,
					      &pgpath->path,
					      nr_bytes);
	return DM_MAPIO_REMAPPED;
}

static void multipath_release_clone(struct request *clone)
{
	blk_put_request(clone);
}

/*
 * Map cloned bios (bio-based multipath)
 */

static struct pgpath *__map_bio(struct multipath *m, struct bio *bio)
{
	struct pgpath *pgpath;
	unsigned long flags;
	bool queue_io;

	/* Do we need to select a new pgpath? */
	pgpath = READ_ONCE(m->current_pgpath);
	queue_io = test_bit(MPATHF_QUEUE_IO, &m->flags);
	if (!pgpath || !queue_io)
		pgpath = choose_pgpath(m, bio->bi_iter.bi_size);

	if ((pgpath && queue_io) ||
	    (!pgpath && test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags))) {
		/* Queue for the daemon to resubmit */
		spin_lock_irqsave(&m->lock, flags);
		bio_list_add(&m->queued_bios, bio);
		spin_unlock_irqrestore(&m->lock, flags);

		/* PG_INIT_REQUIRED cannot be set without QUEUE_IO */
		if (queue_io || test_bit(MPATHF_PG_INIT_REQUIRED, &m->flags))
			pg_init_all_paths(m);
		else if (!queue_io)
			queue_work(kmultipathd, &m->process_queued_bios);

		return ERR_PTR(-EAGAIN);
	}

	return pgpath;
}

static struct pgpath *__map_bio_fast(struct multipath *m, struct bio *bio)
{
	struct pgpath *pgpath;
	unsigned long flags;

	/* Do we need to select a new pgpath? */
	/*
	 * FIXME: currently only switching path if no path (due to failure, etc)
	 * - which negates the point of using a path selector
	 */
	pgpath = READ_ONCE(m->current_pgpath);
	if (!pgpath)
		pgpath = choose_pgpath(m, bio->bi_iter.bi_size);

	if (!pgpath) {
		if (test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags)) {
			/* Queue for the daemon to resubmit */
			spin_lock_irqsave(&m->lock, flags);
			bio_list_add(&m->queued_bios, bio);
			spin_unlock_irqrestore(&m->lock, flags);
			queue_work(kmultipathd, &m->process_queued_bios);

			return ERR_PTR(-EAGAIN);
		}
		return NULL;
	}

	return pgpath;
}

static int __multipath_map_bio(struct multipath *m, struct bio *bio,
			       struct dm_mpath_io *mpio)
{
	struct pgpath *pgpath;

	if (!m->hw_handler_name)
		pgpath = __map_bio_fast(m, bio);
	else
		pgpath = __map_bio(m, bio);

	if (IS_ERR(pgpath))
		return DM_MAPIO_SUBMITTED;

	if (!pgpath) {
		if (must_push_back_bio(m))
			return DM_MAPIO_REQUEUE;
		dm_report_EIO(m);
		return DM_MAPIO_KILL;
	}

	mpio->pgpath = pgpath;

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
	unsigned long flags;
	struct bio *bio;
	struct bio_list bios;
	struct blk_plug plug;
	struct multipath *m =
		container_of(work, struct multipath, process_queued_bios);

	bio_list_init(&bios);

	spin_lock_irqsave(&m->lock, flags);

	if (bio_list_empty(&m->queued_bios)) {
		spin_unlock_irqrestore(&m->lock, flags);
		return;
	}

	bio_list_merge(&bios, &m->queued_bios);
	bio_list_init(&m->queued_bios);

	spin_unlock_irqrestore(&m->lock, flags);

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
			generic_make_request(bio);
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
static int queue_if_no_path(struct multipath *m, bool queue_if_no_path,
			    bool save_old_value)
{
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);
	assign_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags,
		   (save_old_value && test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags)) ||
		   (!save_old_value && queue_if_no_path));
	assign_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags, queue_if_no_path);
	spin_unlock_irqrestore(&m->lock, flags);

	if (!queue_if_no_path) {
		dm_table_run_md_queue_async(m->ti->table);
		process_queued_io_list(m);
	}

	return 0;
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

/*-----------------------------------------------------------------
 * Constructor/argument parsing:
 * <#multipath feature args> [<arg>]*
 * <#hw_handler args> [hw_handler [<arg>]*]
 * <#priority groups>
 * <initial priority group>
 *     [<selector> <#selector args> [<arg>]*
 *      <#paths> <#per-path selector args>
 *         [<path> [<arg>]* ]+ ]+
 *---------------------------------------------------------------*/
static int parse_path_selector(struct dm_arg_set *as, struct priority_group *pg,
			       struct dm_target *ti)
{
	int r;
	struct path_selector_type *pst;
	unsigned ps_argc;

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

	if (test_bit(MPATHF_RETAIN_ATTACHED_HW_HANDLER, &m->flags)) {
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
			char b[BDEVNAME_SIZE];

			printk(KERN_INFO "dm-mpath: retaining handler on device %s\n",
			       bdevname(bdev, b));
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
	kfree(attached_handler_name);
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
	unsigned i, nr_selector_args, nr_args;
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
	unsigned hw_argc;
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
		for (i = 0, p+=j+1; i <= hw_argc - 2; i++, p+=j+1)
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
	unsigned argc;
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
			r = queue_if_no_path(m, true, false);
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

static int multipath_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
	/* target arguments */
	static const struct dm_arg _args[] = {
		{0, 1024, "invalid number of priority groups"},
		{0, 1024, "invalid initial priority group number"},
	};

	int r;
	struct multipath *m;
	struct dm_arg_set as;
	unsigned pg_count = 0;
	unsigned next_pg_num;

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
		unsigned nr_valid_paths = atomic_read(&m->nr_valid_paths);

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

	ti->num_flush_bios = 1;
	ti->num_discard_bios = 1;
	ti->num_write_same_bios = 1;
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
		set_bit(MPATHF_PG_INIT_DISABLED, &m->flags);
		smp_mb__after_atomic();

		flush_workqueue(kmpath_handlerd);
		multipath_wait_for_pg_init_completion(m);

		clear_bit(MPATHF_PG_INIT_DISABLED, &m->flags);
		smp_mb__after_atomic();
	}

	flush_workqueue(kmultipathd);
	flush_work(&m->trigger_event);
}

static void multipath_dtr(struct dm_target *ti)
{
	struct multipath *m = ti->private;

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

	DMWARN("Failing path %s.", pgpath->path.dev->name);

	pgpath->pg->ps.type->fail_path(&pgpath->pg->ps, &pgpath->path);
	pgpath->is_active = false;
	pgpath->fail_count++;

	atomic_dec(&m->nr_valid_paths);

	if (pgpath == m->current_pgpath)
		m->current_pgpath = NULL;

	dm_path_uevent(DM_UEVENT_PATH_FAILED, m->ti,
		       pgpath->path.dev->name, atomic_read(&m->nr_valid_paths));

	schedule_work(&m->trigger_event);

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
	unsigned long flags;
	struct multipath *m = pgpath->pg->m;
	unsigned nr_valid_paths;

	spin_lock_irqsave(&m->lock, flags);

	if (pgpath->is_active)
		goto out;

	DMWARN("Reinstating path %s.", pgpath->path.dev->name);

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
	spin_unlock_irqrestore(&m->lock, flags);
	if (run_queue) {
		dm_table_run_md_queue_async(m->ti->table);
		process_queued_io_list(m);
	}

	return r;
}

/*
 * Fail or reinstate all paths that match the provided struct dm_dev.
 */
static int action_dev(struct multipath *m, struct dm_dev *dev,
		      action_fn action)
{
	int r = -EINVAL;
	struct pgpath *pgpath;
	struct priority_group *pg;

	list_for_each_entry(pg, &m->priority_groups, list) {
		list_for_each_entry(pgpath, &pg->pgpaths, list) {
			if (pgpath->path.dev == dev)
				r = action(pgpath);
		}
	}

	return r;
}

/*
 * Temporarily try to avoid having to use the specified PG
 */
static void bypass_pg(struct multipath *m, struct priority_group *pg,
		      bool bypassed)
{
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);

	pg->bypassed = bypassed;
	m->current_pgpath = NULL;
	m->current_pg = NULL;

	spin_unlock_irqrestore(&m->lock, flags);

	schedule_work(&m->trigger_event);
}

/*
 * Switch to using the specified PG from the next I/O that gets mapped
 */
static int switch_pg_num(struct multipath *m, const char *pgstr)
{
	struct priority_group *pg;
	unsigned pgnum;
	unsigned long flags;
	char dummy;

	if (!pgstr || (sscanf(pgstr, "%u%c", &pgnum, &dummy) != 1) || !pgnum ||
	    !m->nr_priority_groups || (pgnum > m->nr_priority_groups)) {
		DMWARN("invalid PG number supplied to switch_pg_num");
		return -EINVAL;
	}

	spin_lock_irqsave(&m->lock, flags);
	list_for_each_entry(pg, &m->priority_groups, list) {
		pg->bypassed = false;
		if (--pgnum)
			continue;

		m->current_pgpath = NULL;
		m->current_pg = NULL;
		m->next_pg = pg;
	}
	spin_unlock_irqrestore(&m->lock, flags);

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
	unsigned pgnum;
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

	bypass_pg(m, pg, bypassed);
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
		bypass_pg(m, pg, true);
		break;
	case SCSI_DH_RETRY:
		/* Wait before retrying. */
		delay_retry = 1;
		/* fall through */
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

		if (atomic_read(&m->nr_valid_paths) == 0 &&
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
			ps->type->end_io(ps, &pgpath->path, mpio->nr_bytes);
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

	if (atomic_read(&m->nr_valid_paths) == 0 &&
	    !test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags)) {
		if (must_push_back_bio(m)) {
			r = DM_ENDIO_REQUEUE;
		} else {
			dm_report_EIO(m);
			*error = BLK_STS_IOERR;
		}
		goto done;
	}

	spin_lock_irqsave(&m->lock, flags);
	bio_list_add(&m->queued_bios, clone);
	spin_unlock_irqrestore(&m->lock, flags);
	if (!test_bit(MPATHF_QUEUE_IO, &m->flags))
		queue_work(kmultipathd, &m->process_queued_bios);

	r = DM_ENDIO_INCOMPLETE;
done:
	if (pgpath) {
		struct path_selector *ps = &pgpath->pg->ps;

		if (ps->type->end_io)
			ps->type->end_io(ps, &pgpath->path, mpio->nr_bytes);
	}

	return r;
}

/*
 * Suspend can't complete until all the I/O is processed so if
 * the last path fails we must error any remaining I/O.
 * Note that if the freeze_bdev fails while suspending, the
 * queue_if_no_path state is lost - userspace should reset it.
 */
static void multipath_presuspend(struct dm_target *ti)
{
	struct multipath *m = ti->private;

	queue_if_no_path(m, false, true);
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
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);
	assign_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags,
		   test_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags));
	spin_unlock_irqrestore(&m->lock, flags);
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
			     unsigned status_flags, char *result, unsigned maxlen)
{
	int sz = 0;
	unsigned long flags;
	struct multipath *m = ti->private;
	struct priority_group *pg;
	struct pgpath *p;
	unsigned pg_num;
	char state;

	spin_lock_irqsave(&m->lock, flags);

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
			switch(m->queue_mode) {
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

	if (m->next_pg)
		pg_num = m->next_pg->pg_num;
	else if (m->current_pg)
		pg_num = m->current_pg->pg_num;
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
	}

	spin_unlock_irqrestore(&m->lock, flags);
}

static int multipath_message(struct dm_target *ti, unsigned argc, char **argv,
			     char *result, unsigned maxlen)
{
	int r = -EINVAL;
	struct dm_dev *dev;
	struct multipath *m = ti->private;
	action_fn action;

	mutex_lock(&m->work_mutex);

	if (dm_suspended(ti)) {
		r = -EBUSY;
		goto out;
	}

	if (argc == 1) {
		if (!strcasecmp(argv[0], "queue_if_no_path")) {
			r = queue_if_no_path(m, true, false);
			goto out;
		} else if (!strcasecmp(argv[0], "fail_if_no_path")) {
			r = queue_if_no_path(m, false, false);
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

	r = dm_get_device(ti, argv[1], dm_table_get_mode(ti->table), &dev);
	if (r) {
		DMWARN("message: error getting device %s",
		       argv[1]);
		goto out;
	}

	r = action_dev(m, dev, action);

	dm_put_device(ti, dev);

out:
	mutex_unlock(&m->work_mutex);
	return r;
}

static int multipath_prepare_ioctl(struct dm_target *ti,
				   struct block_device **bdev)
{
	struct multipath *m = ti->private;
	struct pgpath *current_pgpath;
	int r;

	current_pgpath = READ_ONCE(m->current_pgpath);
	if (!current_pgpath)
		current_pgpath = choose_pgpath(m, 0);

	if (current_pgpath) {
		if (!test_bit(MPATHF_QUEUE_IO, &m->flags)) {
			*bdev = current_pgpath->path.dev->bdev;
			r = 0;
		} else {
			/* pg_init has not started or completed */
			r = -ENOTCONN;
		}
	} else {
		/* No path is available */
		if (test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags))
			r = -ENOTCONN;
		else
			r = -EIO;
	}

	if (r == -ENOTCONN) {
		if (!READ_ONCE(m->current_pg)) {
			/* Path status changed, redo selection */
			(void) choose_pgpath(m, 0);
		}
		if (test_bit(MPATHF_PG_INIT_REQUIRED, &m->flags))
			pg_init_all_paths(m);
		dm_table_run_md_queue_async(m->ti->table);
		process_queued_io_list(m);
	}

	/*
	 * Only pass ioctls through if the device sizes match exactly.
	 */
	if (!r && ti->len != i_size_read((*bdev)->bd_inode) >> SECTOR_SHIFT)
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
	if (!atomic_read(&m->nr_valid_paths) && test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags))
		return (m->queue_mode != DM_TYPE_REQUEST_BASED);

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

/*-----------------------------------------------------------------
 * Module setup
 *---------------------------------------------------------------*/
static struct target_type multipath_target = {
	.name = "multipath",
	.version = {1, 13, 0},
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
	int r;

	kmultipathd = alloc_workqueue("kmpathd", WQ_MEM_RECLAIM, 0);
	if (!kmultipathd) {
		DMERR("failed to create workqueue kmpathd");
		r = -ENOMEM;
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
		r = -ENOMEM;
		goto bad_alloc_kmpath_handlerd;
	}

	r = dm_register_target(&multipath_target);
	if (r < 0) {
		DMERR("request-based register failed %d", r);
		r = -EINVAL;
		goto bad_register_target;
	}

	return 0;

bad_register_target:
	destroy_workqueue(kmpath_handlerd);
bad_alloc_kmpath_handlerd:
	destroy_workqueue(kmultipathd);
bad_alloc_kmultipathd:
	return r;
}

static void __exit dm_multipath_exit(void)
{
	destroy_workqueue(kmpath_handlerd);
	destroy_workqueue(kmultipathd);

	dm_unregister_target(&multipath_target);
}

module_init(dm_multipath_init);
module_exit(dm_multipath_exit);

MODULE_DESCRIPTION(DM_NAME " multipath target");
MODULE_AUTHOR("Sistina Software <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
