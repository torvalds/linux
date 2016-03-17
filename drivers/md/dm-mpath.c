/*
 * Copyright (C) 2003 Sistina Software Limited.
 * Copyright (C) 2004-2005 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include <linux/device-mapper.h>

#include "dm.h"
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
	struct list_head list;
	struct dm_target *ti;

	const char *hw_handler_name;
	char *hw_handler_params;

	spinlock_t lock;

	unsigned nr_priority_groups;
	struct list_head priority_groups;

	wait_queue_head_t pg_init_wait;	/* Wait for pg_init completion */

	struct pgpath *current_pgpath;
	struct priority_group *current_pg;
	struct priority_group *next_pg;	/* Switch to this PG if set */

	unsigned long flags;		/* Multipath state flags */

	unsigned pg_init_retries;	/* Number of times to retry pg_init */
	unsigned pg_init_delay_msecs;	/* Number of msecs before pg_init retry */

	atomic_t nr_valid_paths;	/* Total number of usable paths */
	atomic_t pg_init_in_progress;	/* Only one pg_init allowed at once */
	atomic_t pg_init_count;		/* Number of times pg_init called */

	/*
	 * We must use a mempool of dm_mpath_io structs so that we
	 * can resubmit bios on error.
	 */
	mempool_t *mpio_pool;

	struct mutex work_mutex;
	struct work_struct trigger_event;
};

/*
 * Context information attached to each bio we process.
 */
struct dm_mpath_io {
	struct pgpath *pgpath;
	size_t nr_bytes;
};

typedef int (*action_fn) (struct pgpath *pgpath);

static struct kmem_cache *_mpio_cache;

static struct workqueue_struct *kmultipathd, *kmpath_handlerd;
static void trigger_event(struct work_struct *work);
static void activate_path(struct work_struct *work);

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

	if (pgpath) {
		pgpath->is_active = true;
		INIT_DELAYED_WORK(&pgpath->activate_path, activate_path);
	}

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

static struct multipath *alloc_multipath(struct dm_target *ti, bool use_blk_mq)
{
	struct multipath *m;

	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (m) {
		INIT_LIST_HEAD(&m->priority_groups);
		spin_lock_init(&m->lock);
		set_bit(MPATHF_QUEUE_IO, &m->flags);
		atomic_set(&m->nr_valid_paths, 0);
		atomic_set(&m->pg_init_in_progress, 0);
		atomic_set(&m->pg_init_count, 0);
		m->pg_init_delay_msecs = DM_PG_INIT_DELAY_DEFAULT;
		INIT_WORK(&m->trigger_event, trigger_event);
		init_waitqueue_head(&m->pg_init_wait);
		mutex_init(&m->work_mutex);

		m->mpio_pool = NULL;
		if (!use_blk_mq) {
			unsigned min_ios = dm_get_reserved_rq_based_ios();

			m->mpio_pool = mempool_create_slab_pool(min_ios, _mpio_cache);
			if (!m->mpio_pool) {
				kfree(m);
				return NULL;
			}
		}

		m->ti = ti;
		ti->private = m;
	}

	return m;
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
	mempool_destroy(m->mpio_pool);
	kfree(m);
}

static struct dm_mpath_io *get_mpio(union map_info *info)
{
	return info->ptr;
}

static struct dm_mpath_io *set_mpio(struct multipath *m, union map_info *info)
{
	struct dm_mpath_io *mpio;

	if (!m->mpio_pool) {
		/* Use blk-mq pdu memory requested via per_io_data_size */
		mpio = get_mpio(info);
		memset(mpio, 0, sizeof(*mpio));
		return mpio;
	}

	mpio = mempool_alloc(m->mpio_pool, GFP_ATOMIC);
	if (!mpio)
		return NULL;

	memset(mpio, 0, sizeof(*mpio));
	info->ptr = mpio;

	return mpio;
}

static void clear_request_fn_mpio(struct multipath *m, union map_info *info)
{
	/* Only needed for non blk-mq (.request_fn) multipath */
	if (m->mpio_pool) {
		struct dm_mpath_io *mpio = info->ptr;

		info->ptr = NULL;
		mempool_free(mpio, m->mpio_pool);
	}
}

/*-----------------------------------------------
 * Path selection
 *-----------------------------------------------*/

static int __pg_init_all_paths(struct multipath *m)
{
	struct pgpath *pgpath;
	unsigned long pg_init_delay = 0;

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
	int r;
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);
	r = __pg_init_all_paths(m);
	spin_unlock_irqrestore(&m->lock, flags);

	return r;
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

	if (unlikely(lockless_dereference(m->current_pg) != pg)) {
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
	bool bypassed = true;

	if (!atomic_read(&m->nr_valid_paths)) {
		clear_bit(MPATHF_QUEUE_IO, &m->flags);
		goto failed;
	}

	/* Were we instructed to switch PG? */
	if (lockless_dereference(m->next_pg)) {
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
	pg = lockless_dereference(m->current_pg);
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
			if (pg->bypassed == bypassed)
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
 * Check whether bios must be queued in the device-mapper core rather
 * than here in the target.
 *
 * If m->queue_if_no_path and m->saved_queue_if_no_path hold the
 * same value then we are not between multipath_presuspend()
 * and multipath_resume() calls and we have no need to check
 * for the DMF_NOFLUSH_SUSPENDING flag.
 */
static int must_push_back(struct multipath *m)
{
	return (test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags) ||
		((test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags) !=
		  test_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags)) &&
		 dm_noflush_suspending(m->ti)));
}

/*
 * Map cloned requests
 */
static int __multipath_map(struct dm_target *ti, struct request *clone,
			   union map_info *map_context,
			   struct request *rq, struct request **__clone)
{
	struct multipath *m = ti->private;
	int r = DM_MAPIO_REQUEUE;
	size_t nr_bytes = clone ? blk_rq_bytes(clone) : blk_rq_bytes(rq);
	struct pgpath *pgpath;
	struct block_device *bdev;
	struct dm_mpath_io *mpio;

	/* Do we need to select a new pgpath? */
	pgpath = lockless_dereference(m->current_pgpath);
	if (!pgpath || !test_bit(MPATHF_QUEUE_IO, &m->flags))
		pgpath = choose_pgpath(m, nr_bytes);

	if (!pgpath) {
		if (!must_push_back(m))
			r = -EIO;	/* Failed */
		return r;
	} else if (test_bit(MPATHF_QUEUE_IO, &m->flags) ||
		   test_bit(MPATHF_PG_INIT_REQUIRED, &m->flags)) {
		pg_init_all_paths(m);
		return r;
	}

	mpio = set_mpio(m, map_context);
	if (!mpio)
		/* ENOMEM, requeue */
		return r;

	mpio->pgpath = pgpath;
	mpio->nr_bytes = nr_bytes;

	bdev = pgpath->path.dev->bdev;

	if (clone) {
		/*
		 * Old request-based interface: allocated clone is passed in.
		 * Used by: .request_fn stacked on .request_fn path(s).
		 */
		clone->q = bdev_get_queue(bdev);
		clone->rq_disk = bdev->bd_disk;
		clone->cmd_flags |= REQ_FAILFAST_TRANSPORT;
	} else {
		/*
		 * blk-mq request-based interface; used by both:
		 * .request_fn stacked on blk-mq path(s) and
		 * blk-mq stacked on blk-mq path(s).
		 */
		*__clone = blk_mq_alloc_request(bdev_get_queue(bdev),
						rq_data_dir(rq), BLK_MQ_REQ_NOWAIT);
		if (IS_ERR(*__clone)) {
			/* ENOMEM, requeue */
			clear_request_fn_mpio(m, map_context);
			return r;
		}
		(*__clone)->bio = (*__clone)->biotail = NULL;
		(*__clone)->rq_disk = bdev->bd_disk;
		(*__clone)->cmd_flags |= REQ_FAILFAST_TRANSPORT;
	}

	if (pgpath->pg->ps.type->start_io)
		pgpath->pg->ps.type->start_io(&pgpath->pg->ps,
					      &pgpath->path,
					      nr_bytes);
	return DM_MAPIO_REMAPPED;
}

static int multipath_map(struct dm_target *ti, struct request *clone,
			 union map_info *map_context)
{
	return __multipath_map(ti, clone, map_context, NULL, NULL);
}

static int multipath_clone_and_map(struct dm_target *ti, struct request *rq,
				   union map_info *map_context,
				   struct request **clone)
{
	return __multipath_map(ti, NULL, map_context, rq, clone);
}

static void multipath_release_clone(struct request *clone)
{
	blk_mq_free_request(clone);
}

/*
 * If we run out of usable paths, should we queue I/O or error it?
 */
static int queue_if_no_path(struct multipath *m, bool queue_if_no_path,
			    bool save_old_value)
{
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);

	if (save_old_value) {
		if (test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags))
			set_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags);
		else
			clear_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags);
	} else {
		if (queue_if_no_path)
			set_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags);
		else
			clear_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags);
	}
	if (queue_if_no_path)
		set_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags);
	else
		clear_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags);

	spin_unlock_irqrestore(&m->lock, flags);

	if (!queue_if_no_path)
		dm_table_run_md_queue_async(m->ti->table);

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

	static struct dm_arg _args[] = {
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

static struct pgpath *parse_path(struct dm_arg_set *as, struct path_selector *ps,
			       struct dm_target *ti)
{
	int r;
	struct pgpath *p;
	struct multipath *m = ti->private;
	struct request_queue *q = NULL;
	const char *attached_handler_name;

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

	if (test_bit(MPATHF_RETAIN_ATTACHED_HW_HANDLER, &m->flags) || m->hw_handler_name)
		q = bdev_get_queue(p->path.dev->bdev);

	if (test_bit(MPATHF_RETAIN_ATTACHED_HW_HANDLER, &m->flags)) {
retain:
		attached_handler_name = scsi_dh_attached_handler_name(q, GFP_KERNEL);
		if (attached_handler_name) {
			/*
			 * Reset hw_handler_name to match the attached handler
			 * and clear any hw_handler_params associated with the
			 * ignored handler.
			 *
			 * NB. This modifies the table line to show the actual
			 * handler instead of the original table passed in.
			 */
			kfree(m->hw_handler_name);
			m->hw_handler_name = attached_handler_name;

			kfree(m->hw_handler_params);
			m->hw_handler_params = NULL;
		}
	}

	if (m->hw_handler_name) {
		r = scsi_dh_attach(q, m->hw_handler_name);
		if (r == -EBUSY) {
			char b[BDEVNAME_SIZE];

			printk(KERN_INFO "dm-mpath: retaining handler on device %s\n",
				bdevname(p->path.dev->bdev, b));
			goto retain;
		}
		if (r < 0) {
			ti->error = "error attaching hardware handler";
			dm_put_device(ti, p->path.dev);
			goto bad;
		}

		if (m->hw_handler_params) {
			r = scsi_dh_set_params(q, m->hw_handler_params);
			if (r < 0) {
				ti->error = "unable to set hardware "
							"handler parameters";
				dm_put_device(ti, p->path.dev);
				goto bad;
			}
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
	static struct dm_arg _args[] = {
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

	static struct dm_arg _args[] = {
		{0, 1024, "invalid number of hardware handler args"},
	};

	if (dm_read_arg_group(_args, as, &hw_argc, &ti->error))
		return -EINVAL;

	if (!hw_argc)
		return 0;

	m->hw_handler_name = kstrdup(dm_shift_arg(as), GFP_KERNEL);

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

	static struct dm_arg _args[] = {
		{0, 6, "invalid number of feature args"},
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

		ti->error = "Unrecognised multipath feature request";
		r = -EINVAL;
	} while (argc && !r);

	return r;
}

static int multipath_ctr(struct dm_target *ti, unsigned int argc,
			 char **argv)
{
	/* target arguments */
	static struct dm_arg _args[] = {
		{0, 1024, "invalid number of priority groups"},
		{0, 1024, "invalid initial priority group number"},
	};

	int r;
	struct multipath *m;
	struct dm_arg_set as;
	unsigned pg_count = 0;
	unsigned next_pg_num;
	bool use_blk_mq = dm_use_blk_mq(dm_table_get_md(ti->table));

	as.argc = argc;
	as.argv = argv;

	m = alloc_multipath(ti, use_blk_mq);
	if (!m) {
		ti->error = "can't allocate multipath";
		return -EINVAL;
	}

	r = parse_features(&as, m);
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
	if (use_blk_mq)
		ti->per_io_data_size = sizeof(struct dm_mpath_io);

	return 0;

 bad:
	free_multipath(m);
	return r;
}

static void multipath_wait_for_pg_init_completion(struct multipath *m)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&m->pg_init_wait, &wait);

	while (1) {
		set_current_state(TASK_UNINTERRUPTIBLE);

		if (!atomic_read(&m->pg_init_in_progress))
			break;

		io_schedule();
	}
	set_current_state(TASK_RUNNING);

	remove_wait_queue(&m->pg_init_wait, &wait);
}

static void flush_multipath_work(struct multipath *m)
{
	set_bit(MPATHF_PG_INIT_DISABLED, &m->flags);
	smp_mb__after_atomic();

	flush_workqueue(kmpath_handlerd);
	multipath_wait_for_pg_init_completion(m);
	flush_workqueue(kmultipathd);
	flush_work(&m->trigger_event);

	clear_bit(MPATHF_PG_INIT_DISABLED, &m->flags);
	smp_mb__after_atomic();
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
	if (run_queue)
		dm_table_run_md_queue_async(m->ti->table);

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
	    (pgnum > m->nr_priority_groups)) {
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
	    (pgnum > m->nr_priority_groups)) {
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

	/*
	 * Wake up any thread waiting to suspend.
	 */
	wake_up(&m->pg_init_wait);

out:
	spin_unlock_irqrestore(&m->lock, flags);
}

static void activate_path(struct work_struct *work)
{
	struct pgpath *pgpath =
		container_of(work, struct pgpath, activate_path.work);

	if (pgpath->is_active)
		scsi_dh_activate(bdev_get_queue(pgpath->path.dev->bdev),
				 pg_init_done, pgpath);
	else
		pg_init_done(pgpath, SCSI_DH_DEV_OFFLINED);
}

static int noretry_error(int error)
{
	switch (error) {
	case -EOPNOTSUPP:
	case -EREMOTEIO:
	case -EILSEQ:
	case -ENODATA:
	case -ENOSPC:
		return 1;
	}

	/* Anything else could be a path failure, so should be retried */
	return 0;
}

/*
 * end_io handling
 */
static int do_end_io(struct multipath *m, struct request *clone,
		     int error, struct dm_mpath_io *mpio)
{
	/*
	 * We don't queue any clone request inside the multipath target
	 * during end I/O handling, since those clone requests don't have
	 * bio clones.  If we queue them inside the multipath target,
	 * we need to make bio clones, that requires memory allocation.
	 * (See drivers/md/dm.c:end_clone_bio() about why the clone requests
	 *  don't have bio clones.)
	 * Instead of queueing the clone request here, we queue the original
	 * request into dm core, which will remake a clone request and
	 * clone bios for it and resubmit it later.
	 */
	int r = DM_ENDIO_REQUEUE;

	if (!error && !clone->errors)
		return 0;	/* I/O complete */

	if (noretry_error(error))
		return error;

	if (mpio->pgpath)
		fail_path(mpio->pgpath);

	if (!atomic_read(&m->nr_valid_paths)) {
		if (!test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags)) {
			if (!must_push_back(m))
				r = -EIO;
		} else {
			if (error == -EBADE)
				r = error;
		}
	}

	return r;
}

static int multipath_end_io(struct dm_target *ti, struct request *clone,
			    int error, union map_info *map_context)
{
	struct multipath *m = ti->private;
	struct dm_mpath_io *mpio = get_mpio(map_context);
	struct pgpath *pgpath;
	struct path_selector *ps;
	int r;

	BUG_ON(!mpio);

	r = do_end_io(m, clone, error, mpio);
	pgpath = mpio->pgpath;
	if (pgpath) {
		ps = &pgpath->pg->ps;
		if (ps->type->end_io)
			ps->type->end_io(ps, &pgpath->path, mpio->nr_bytes);
	}
	clear_request_fn_mpio(m, map_context);

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

	if (test_bit(MPATHF_SAVED_QUEUE_IF_NO_PATH, &m->flags))
		set_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags);
	else
		clear_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags);
	smp_mb__after_atomic();
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
			      test_bit(MPATHF_RETAIN_ATTACHED_HW_HANDLER, &m->flags));
		if (test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags))
			DMEMIT("queue_if_no_path ");
		if (m->pg_init_retries)
			DMEMIT("pg_init_retries %u ", m->pg_init_retries);
		if (m->pg_init_delay_msecs != DM_PG_INIT_DELAY_DEFAULT)
			DMEMIT("pg_init_delay_msecs %u ", m->pg_init_delay_msecs);
		if (test_bit(MPATHF_RETAIN_ATTACHED_HW_HANDLER, &m->flags))
			DMEMIT("retain_attached_hw_handler ");
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

static int multipath_message(struct dm_target *ti, unsigned argc, char **argv)
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
		struct block_device **bdev, fmode_t *mode)
{
	struct multipath *m = ti->private;
	struct pgpath *current_pgpath;
	int r;

	current_pgpath = lockless_dereference(m->current_pgpath);
	if (!current_pgpath)
		current_pgpath = choose_pgpath(m, 0);

	if (current_pgpath) {
		if (!test_bit(MPATHF_QUEUE_IO, &m->flags)) {
			*bdev = current_pgpath->path.dev->bdev;
			*mode = current_pgpath->path.dev->mode;
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
		if (!lockless_dereference(m->current_pg)) {
			/* Path status changed, redo selection */
			(void) choose_pgpath(m, 0);
		}
		if (test_bit(MPATHF_PG_INIT_REQUIRED, &m->flags))
			pg_init_all_paths(m);
		dm_table_run_md_queue_async(m->ti->table);
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

	/* pg_init in progress or no paths available */
	if (atomic_read(&m->pg_init_in_progress) ||
	    (!atomic_read(&m->nr_valid_paths) && test_bit(MPATHF_QUEUE_IF_NO_PATH, &m->flags)))
		return true;

	/* Guess which priority_group will be used at next mapping time */
	pg = lockless_dereference(m->current_pg);
	next_pg = lockless_dereference(m->next_pg);
	if (unlikely(!lockless_dereference(m->current_pgpath) && next_pg))
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
	.version = {1, 11, 0},
	.features = DM_TARGET_SINGLETON | DM_TARGET_IMMUTABLE,
	.module = THIS_MODULE,
	.ctr = multipath_ctr,
	.dtr = multipath_dtr,
	.map_rq = multipath_map,
	.clone_and_map_rq = multipath_clone_and_map,
	.release_clone_rq = multipath_release_clone,
	.rq_end_io = multipath_end_io,
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

	/* allocate a slab for the dm_ios */
	_mpio_cache = KMEM_CACHE(dm_mpath_io, 0);
	if (!_mpio_cache)
		return -ENOMEM;

	r = dm_register_target(&multipath_target);
	if (r < 0) {
		DMERR("register failed %d", r);
		r = -EINVAL;
		goto bad_register_target;
	}

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

	DMINFO("version %u.%u.%u loaded",
	       multipath_target.version[0], multipath_target.version[1],
	       multipath_target.version[2]);

	return 0;

bad_alloc_kmpath_handlerd:
	destroy_workqueue(kmultipathd);
bad_alloc_kmultipathd:
	dm_unregister_target(&multipath_target);
bad_register_target:
	kmem_cache_destroy(_mpio_cache);

	return r;
}

static void __exit dm_multipath_exit(void)
{
	destroy_workqueue(kmpath_handlerd);
	destroy_workqueue(kmultipathd);

	dm_unregister_target(&multipath_target);
	kmem_cache_destroy(_mpio_cache);
}

module_init(dm_multipath_init);
module_exit(dm_multipath_exit);

MODULE_DESCRIPTION(DM_NAME " multipath target");
MODULE_AUTHOR("Sistina Software <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
