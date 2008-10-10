/*
 * Copyright (C) 2003 Sistina Software Limited.
 * Copyright (C) 2004-2005 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include "dm-path-selector.h"
#include "dm-bio-list.h"
#include "dm-bio-record.h"
#include "dm-uevent.h"

#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include <scsi/scsi_dh.h>
#include <asm/atomic.h>

#define DM_MSG_PREFIX "multipath"
#define MESG_STR(x) x, sizeof(x)

/* Path properties */
struct pgpath {
	struct list_head list;

	struct priority_group *pg;	/* Owning PG */
	unsigned is_active;		/* Path status */
	unsigned fail_count;		/* Cumulative failure count */

	struct dm_path path;
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
	unsigned bypassed;		/* Temporarily bypass this PG? */

	unsigned nr_pgpaths;		/* Number of paths in PG */
	struct list_head pgpaths;
};

/* Multipath context */
struct multipath {
	struct list_head list;
	struct dm_target *ti;

	spinlock_t lock;

	const char *hw_handler_name;
	struct work_struct activate_path;
	struct pgpath *pgpath_to_activate;
	unsigned nr_priority_groups;
	struct list_head priority_groups;
	unsigned pg_init_required;	/* pg_init needs calling? */
	unsigned pg_init_in_progress;	/* Only one pg_init allowed at once */

	unsigned nr_valid_paths;	/* Total number of usable paths */
	struct pgpath *current_pgpath;
	struct priority_group *current_pg;
	struct priority_group *next_pg;	/* Switch to this PG if set */
	unsigned repeat_count;		/* I/Os left before calling PS again */

	unsigned queue_io;		/* Must we queue all I/O? */
	unsigned queue_if_no_path;	/* Queue I/O if last path fails? */
	unsigned saved_queue_if_no_path;/* Saved state during suspension */
	unsigned pg_init_retries;	/* Number of times to retry pg_init */
	unsigned pg_init_count;		/* Number of times pg_init called */

	struct work_struct process_queued_ios;
	struct bio_list queued_ios;
	unsigned queue_size;

	struct work_struct trigger_event;

	/*
	 * We must use a mempool of dm_mpath_io structs so that we
	 * can resubmit bios on error.
	 */
	mempool_t *mpio_pool;
};

/*
 * Context information attached to each bio we process.
 */
struct dm_mpath_io {
	struct pgpath *pgpath;
	struct dm_bio_details details;
};

typedef int (*action_fn) (struct pgpath *pgpath);

#define MIN_IOS 256	/* Mempool size */

static struct kmem_cache *_mpio_cache;

static struct workqueue_struct *kmultipathd, *kmpath_handlerd;
static void process_queued_ios(struct work_struct *work);
static void trigger_event(struct work_struct *work);
static void activate_path(struct work_struct *work);


/*-----------------------------------------------
 * Allocation routines
 *-----------------------------------------------*/

static struct pgpath *alloc_pgpath(void)
{
	struct pgpath *pgpath = kzalloc(sizeof(*pgpath), GFP_KERNEL);

	if (pgpath)
		pgpath->is_active = 1;

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
	unsigned long flags;
	struct pgpath *pgpath, *tmp;
	struct multipath *m = ti->private;

	list_for_each_entry_safe(pgpath, tmp, pgpaths, list) {
		list_del(&pgpath->list);
		if (m->hw_handler_name)
			scsi_dh_detach(bdev_get_queue(pgpath->path.dev->bdev));
		dm_put_device(ti, pgpath->path.dev);
		spin_lock_irqsave(&m->lock, flags);
		if (m->pgpath_to_activate == pgpath)
			m->pgpath_to_activate = NULL;
		spin_unlock_irqrestore(&m->lock, flags);
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
		m->queue_io = 1;
		INIT_WORK(&m->process_queued_ios, process_queued_ios);
		INIT_WORK(&m->trigger_event, trigger_event);
		INIT_WORK(&m->activate_path, activate_path);
		m->mpio_pool = mempool_create_slab_pool(MIN_IOS, _mpio_cache);
		if (!m->mpio_pool) {
			kfree(m);
			return NULL;
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
	mempool_destroy(m->mpio_pool);
	kfree(m);
}


/*-----------------------------------------------
 * Path selection
 *-----------------------------------------------*/

static void __switch_pg(struct multipath *m, struct pgpath *pgpath)
{
	m->current_pg = pgpath->pg;

	/* Must we initialise the PG first, and queue I/O till it's ready? */
	if (m->hw_handler_name) {
		m->pg_init_required = 1;
		m->queue_io = 1;
	} else {
		m->pg_init_required = 0;
		m->queue_io = 0;
	}

	m->pg_init_count = 0;
}

static int __choose_path_in_pg(struct multipath *m, struct priority_group *pg)
{
	struct dm_path *path;

	path = pg->ps.type->select_path(&pg->ps, &m->repeat_count);
	if (!path)
		return -ENXIO;

	m->current_pgpath = path_to_pgpath(path);

	if (m->current_pg != pg)
		__switch_pg(m, m->current_pgpath);

	return 0;
}

static void __choose_pgpath(struct multipath *m)
{
	struct priority_group *pg;
	unsigned bypassed = 1;

	if (!m->nr_valid_paths)
		goto failed;

	/* Were we instructed to switch PG? */
	if (m->next_pg) {
		pg = m->next_pg;
		m->next_pg = NULL;
		if (!__choose_path_in_pg(m, pg))
			return;
	}

	/* Don't change PG until it has no remaining paths */
	if (m->current_pg && !__choose_path_in_pg(m, m->current_pg))
		return;

	/*
	 * Loop through priority groups until we find a valid path.
	 * First time we skip PGs marked 'bypassed'.
	 * Second time we only try the ones we skipped.
	 */
	do {
		list_for_each_entry(pg, &m->priority_groups, list) {
			if (pg->bypassed == bypassed)
				continue;
			if (!__choose_path_in_pg(m, pg))
				return;
		}
	} while (bypassed--);

failed:
	m->current_pgpath = NULL;
	m->current_pg = NULL;
}

/*
 * Check whether bios must be queued in the device-mapper core rather
 * than here in the target.
 *
 * m->lock must be held on entry.
 *
 * If m->queue_if_no_path and m->saved_queue_if_no_path hold the
 * same value then we are not between multipath_presuspend()
 * and multipath_resume() calls and we have no need to check
 * for the DMF_NOFLUSH_SUSPENDING flag.
 */
static int __must_push_back(struct multipath *m)
{
	return (m->queue_if_no_path != m->saved_queue_if_no_path &&
		dm_noflush_suspending(m->ti));
}

static int map_io(struct multipath *m, struct bio *bio,
		  struct dm_mpath_io *mpio, unsigned was_queued)
{
	int r = DM_MAPIO_REMAPPED;
	unsigned long flags;
	struct pgpath *pgpath;

	spin_lock_irqsave(&m->lock, flags);

	/* Do we need to select a new pgpath? */
	if (!m->current_pgpath ||
	    (!m->queue_io && (m->repeat_count && --m->repeat_count == 0)))
		__choose_pgpath(m);

	pgpath = m->current_pgpath;

	if (was_queued)
		m->queue_size--;

	if ((pgpath && m->queue_io) ||
	    (!pgpath && m->queue_if_no_path)) {
		/* Queue for the daemon to resubmit */
		bio_list_add(&m->queued_ios, bio);
		m->queue_size++;
		if ((m->pg_init_required && !m->pg_init_in_progress) ||
		    !m->queue_io)
			queue_work(kmultipathd, &m->process_queued_ios);
		pgpath = NULL;
		r = DM_MAPIO_SUBMITTED;
	} else if (pgpath)
		bio->bi_bdev = pgpath->path.dev->bdev;
	else if (__must_push_back(m))
		r = DM_MAPIO_REQUEUE;
	else
		r = -EIO;	/* Failed */

	mpio->pgpath = pgpath;

	spin_unlock_irqrestore(&m->lock, flags);

	return r;
}

/*
 * If we run out of usable paths, should we queue I/O or error it?
 */
static int queue_if_no_path(struct multipath *m, unsigned queue_if_no_path,
			    unsigned save_old_value)
{
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);

	if (save_old_value)
		m->saved_queue_if_no_path = m->queue_if_no_path;
	else
		m->saved_queue_if_no_path = queue_if_no_path;
	m->queue_if_no_path = queue_if_no_path;
	if (!m->queue_if_no_path && m->queue_size)
		queue_work(kmultipathd, &m->process_queued_ios);

	spin_unlock_irqrestore(&m->lock, flags);

	return 0;
}

/*-----------------------------------------------------------------
 * The multipath daemon is responsible for resubmitting queued ios.
 *---------------------------------------------------------------*/

static void dispatch_queued_ios(struct multipath *m)
{
	int r;
	unsigned long flags;
	struct bio *bio = NULL, *next;
	struct dm_mpath_io *mpio;
	union map_info *info;

	spin_lock_irqsave(&m->lock, flags);
	bio = bio_list_get(&m->queued_ios);
	spin_unlock_irqrestore(&m->lock, flags);

	while (bio) {
		next = bio->bi_next;
		bio->bi_next = NULL;

		info = dm_get_mapinfo(bio);
		mpio = info->ptr;

		r = map_io(m, bio, mpio, 1);
		if (r < 0)
			bio_endio(bio, r);
		else if (r == DM_MAPIO_REMAPPED)
			generic_make_request(bio);
		else if (r == DM_MAPIO_REQUEUE)
			bio_endio(bio, -EIO);

		bio = next;
	}
}

static void process_queued_ios(struct work_struct *work)
{
	struct multipath *m =
		container_of(work, struct multipath, process_queued_ios);
	struct pgpath *pgpath = NULL;
	unsigned init_required = 0, must_queue = 1;
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);

	if (!m->queue_size)
		goto out;

	if (!m->current_pgpath)
		__choose_pgpath(m);

	pgpath = m->current_pgpath;
	m->pgpath_to_activate = m->current_pgpath;

	if ((pgpath && !m->queue_io) ||
	    (!pgpath && !m->queue_if_no_path))
		must_queue = 0;

	if (m->pg_init_required && !m->pg_init_in_progress) {
		m->pg_init_count++;
		m->pg_init_required = 0;
		m->pg_init_in_progress = 1;
		init_required = 1;
	}

out:
	spin_unlock_irqrestore(&m->lock, flags);

	if (init_required)
		queue_work(kmpath_handlerd, &m->activate_path);

	if (!must_queue)
		dispatch_queued_ios(m);
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
struct param {
	unsigned min;
	unsigned max;
	char *error;
};

static int read_param(struct param *param, char *str, unsigned *v, char **error)
{
	if (!str ||
	    (sscanf(str, "%u", v) != 1) ||
	    (*v < param->min) ||
	    (*v > param->max)) {
		*error = param->error;
		return -EINVAL;
	}

	return 0;
}

struct arg_set {
	unsigned argc;
	char **argv;
};

static char *shift(struct arg_set *as)
{
	char *r;

	if (as->argc) {
		as->argc--;
		r = *as->argv;
		as->argv++;
		return r;
	}

	return NULL;
}

static void consume(struct arg_set *as, unsigned n)
{
	BUG_ON (as->argc < n);
	as->argc -= n;
	as->argv += n;
}

static int parse_path_selector(struct arg_set *as, struct priority_group *pg,
			       struct dm_target *ti)
{
	int r;
	struct path_selector_type *pst;
	unsigned ps_argc;

	static struct param _params[] = {
		{0, 1024, "invalid number of path selector args"},
	};

	pst = dm_get_path_selector(shift(as));
	if (!pst) {
		ti->error = "unknown path selector type";
		return -EINVAL;
	}

	r = read_param(_params, shift(as), &ps_argc, &ti->error);
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
	consume(as, ps_argc);

	return 0;
}

static struct pgpath *parse_path(struct arg_set *as, struct path_selector *ps,
			       struct dm_target *ti)
{
	int r;
	struct pgpath *p;
	struct multipath *m = ti->private;

	/* we need at least a path arg */
	if (as->argc < 1) {
		ti->error = "no device given";
		return ERR_PTR(-EINVAL);
	}

	p = alloc_pgpath();
	if (!p)
		return ERR_PTR(-ENOMEM);

	r = dm_get_device(ti, shift(as), ti->begin, ti->len,
			  dm_table_get_mode(ti->table), &p->path.dev);
	if (r) {
		ti->error = "error getting device";
		goto bad;
	}

	if (m->hw_handler_name) {
		r = scsi_dh_attach(bdev_get_queue(p->path.dev->bdev),
				   m->hw_handler_name);
		if (r < 0) {
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

static struct priority_group *parse_priority_group(struct arg_set *as,
						   struct multipath *m)
{
	static struct param _params[] = {
		{1, 1024, "invalid number of paths"},
		{0, 1024, "invalid number of selector args"}
	};

	int r;
	unsigned i, nr_selector_args, nr_params;
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
	r = read_param(_params, shift(as), &pg->nr_pgpaths, &ti->error);
	if (r)
		goto bad;

	r = read_param(_params + 1, shift(as), &nr_selector_args, &ti->error);
	if (r)
		goto bad;

	nr_params = 1 + nr_selector_args;
	for (i = 0; i < pg->nr_pgpaths; i++) {
		struct pgpath *pgpath;
		struct arg_set path_args;

		if (as->argc < nr_params) {
			ti->error = "not enough path parameters";
			goto bad;
		}

		path_args.argc = nr_params;
		path_args.argv = as->argv;

		pgpath = parse_path(&path_args, &pg->ps, ti);
		if (IS_ERR(pgpath)) {
			r = PTR_ERR(pgpath);
			goto bad;
		}

		pgpath->pg = pg;
		list_add_tail(&pgpath->list, &pg->pgpaths);
		consume(as, nr_params);
	}

	return pg;

 bad:
	free_priority_group(pg, ti);
	return ERR_PTR(r);
}

static int parse_hw_handler(struct arg_set *as, struct multipath *m)
{
	unsigned hw_argc;
	struct dm_target *ti = m->ti;

	static struct param _params[] = {
		{0, 1024, "invalid number of hardware handler args"},
	};

	if (read_param(_params, shift(as), &hw_argc, &ti->error))
		return -EINVAL;

	if (!hw_argc)
		return 0;

	m->hw_handler_name = kstrdup(shift(as), GFP_KERNEL);
	request_module("scsi_dh_%s", m->hw_handler_name);
	if (scsi_dh_handler_exist(m->hw_handler_name) == 0) {
		ti->error = "unknown hardware handler type";
		kfree(m->hw_handler_name);
		m->hw_handler_name = NULL;
		return -EINVAL;
	}
	consume(as, hw_argc - 1);

	return 0;
}

static int parse_features(struct arg_set *as, struct multipath *m)
{
	int r;
	unsigned argc;
	struct dm_target *ti = m->ti;
	const char *param_name;

	static struct param _params[] = {
		{0, 3, "invalid number of feature args"},
		{1, 50, "pg_init_retries must be between 1 and 50"},
	};

	r = read_param(_params, shift(as), &argc, &ti->error);
	if (r)
		return -EINVAL;

	if (!argc)
		return 0;

	do {
		param_name = shift(as);
		argc--;

		if (!strnicmp(param_name, MESG_STR("queue_if_no_path"))) {
			r = queue_if_no_path(m, 1, 0);
			continue;
		}

		if (!strnicmp(param_name, MESG_STR("pg_init_retries")) &&
		    (argc >= 1)) {
			r = read_param(_params + 1, shift(as),
				       &m->pg_init_retries, &ti->error);
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
	/* target parameters */
	static struct param _params[] = {
		{1, 1024, "invalid number of priority groups"},
		{1, 1024, "invalid initial priority group number"},
	};

	int r;
	struct multipath *m;
	struct arg_set as;
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

	r = parse_hw_handler(&as, m);
	if (r)
		goto bad;

	r = read_param(_params, shift(&as), &m->nr_priority_groups, &ti->error);
	if (r)
		goto bad;

	r = read_param(_params + 1, shift(&as), &next_pg_num, &ti->error);
	if (r)
		goto bad;

	/* parse the priority groups */
	while (as.argc) {
		struct priority_group *pg;

		pg = parse_priority_group(&as, m);
		if (IS_ERR(pg)) {
			r = PTR_ERR(pg);
			goto bad;
		}

		m->nr_valid_paths += pg->nr_pgpaths;
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

	return 0;

 bad:
	free_multipath(m);
	return r;
}

static void multipath_dtr(struct dm_target *ti)
{
	struct multipath *m = (struct multipath *) ti->private;

	flush_workqueue(kmpath_handlerd);
	flush_workqueue(kmultipathd);
	free_multipath(m);
}

/*
 * Map bios, recording original fields for later in case we have to resubmit
 */
static int multipath_map(struct dm_target *ti, struct bio *bio,
			 union map_info *map_context)
{
	int r;
	struct dm_mpath_io *mpio;
	struct multipath *m = (struct multipath *) ti->private;

	mpio = mempool_alloc(m->mpio_pool, GFP_NOIO);
	dm_bio_record(&mpio->details, bio);

	map_context->ptr = mpio;
	bio->bi_rw |= (1 << BIO_RW_FAILFAST);
	r = map_io(m, bio, mpio, 0);
	if (r < 0 || r == DM_MAPIO_REQUEUE)
		mempool_free(mpio, m->mpio_pool);

	return r;
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
	pgpath->is_active = 0;
	pgpath->fail_count++;

	m->nr_valid_paths--;

	if (pgpath == m->current_pgpath)
		m->current_pgpath = NULL;

	dm_path_uevent(DM_UEVENT_PATH_FAILED, m->ti,
		      pgpath->path.dev->name, m->nr_valid_paths);

	queue_work(kmultipathd, &m->trigger_event);

out:
	spin_unlock_irqrestore(&m->lock, flags);

	return 0;
}

/*
 * Reinstate a previously-failed path
 */
static int reinstate_path(struct pgpath *pgpath)
{
	int r = 0;
	unsigned long flags;
	struct multipath *m = pgpath->pg->m;

	spin_lock_irqsave(&m->lock, flags);

	if (pgpath->is_active)
		goto out;

	if (!pgpath->pg->ps.type->reinstate_path) {
		DMWARN("Reinstate path not supported by path selector %s",
		       pgpath->pg->ps.type->name);
		r = -EINVAL;
		goto out;
	}

	r = pgpath->pg->ps.type->reinstate_path(&pgpath->pg->ps, &pgpath->path);
	if (r)
		goto out;

	pgpath->is_active = 1;

	m->current_pgpath = NULL;
	if (!m->nr_valid_paths++ && m->queue_size)
		queue_work(kmultipathd, &m->process_queued_ios);

	dm_path_uevent(DM_UEVENT_PATH_REINSTATED, m->ti,
		      pgpath->path.dev->name, m->nr_valid_paths);

	queue_work(kmultipathd, &m->trigger_event);

out:
	spin_unlock_irqrestore(&m->lock, flags);

	return r;
}

/*
 * Fail or reinstate all paths that match the provided struct dm_dev.
 */
static int action_dev(struct multipath *m, struct dm_dev *dev,
		      action_fn action)
{
	int r = 0;
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
		      int bypassed)
{
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);

	pg->bypassed = bypassed;
	m->current_pgpath = NULL;
	m->current_pg = NULL;

	spin_unlock_irqrestore(&m->lock, flags);

	queue_work(kmultipathd, &m->trigger_event);
}

/*
 * Switch to using the specified PG from the next I/O that gets mapped
 */
static int switch_pg_num(struct multipath *m, const char *pgstr)
{
	struct priority_group *pg;
	unsigned pgnum;
	unsigned long flags;

	if (!pgstr || (sscanf(pgstr, "%u", &pgnum) != 1) || !pgnum ||
	    (pgnum > m->nr_priority_groups)) {
		DMWARN("invalid PG number supplied to switch_pg_num");
		return -EINVAL;
	}

	spin_lock_irqsave(&m->lock, flags);
	list_for_each_entry(pg, &m->priority_groups, list) {
		pg->bypassed = 0;
		if (--pgnum)
			continue;

		m->current_pgpath = NULL;
		m->current_pg = NULL;
		m->next_pg = pg;
	}
	spin_unlock_irqrestore(&m->lock, flags);

	queue_work(kmultipathd, &m->trigger_event);
	return 0;
}

/*
 * Set/clear bypassed status of a PG.
 * PGs are numbered upwards from 1 in the order they were declared.
 */
static int bypass_pg_num(struct multipath *m, const char *pgstr, int bypassed)
{
	struct priority_group *pg;
	unsigned pgnum;

	if (!pgstr || (sscanf(pgstr, "%u", &pgnum) != 1) || !pgnum ||
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
static int pg_init_limit_reached(struct multipath *m, struct pgpath *pgpath)
{
	unsigned long flags;
	int limit_reached = 0;

	spin_lock_irqsave(&m->lock, flags);

	if (m->pg_init_count <= m->pg_init_retries)
		m->pg_init_required = 1;
	else
		limit_reached = 1;

	spin_unlock_irqrestore(&m->lock, flags);

	return limit_reached;
}

static void pg_init_done(struct dm_path *path, int errors)
{
	struct pgpath *pgpath = path_to_pgpath(path);
	struct priority_group *pg = pgpath->pg;
	struct multipath *m = pg->m;
	unsigned long flags;

	/* device or driver problems */
	switch (errors) {
	case SCSI_DH_OK:
		break;
	case SCSI_DH_NOSYS:
		if (!m->hw_handler_name) {
			errors = 0;
			break;
		}
		DMERR("Cannot failover device because scsi_dh_%s was not "
		      "loaded.", m->hw_handler_name);
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
		bypass_pg(m, pg, 1);
		break;
	/* TODO: For SCSI_DH_RETRY we should wait a couple seconds */
	case SCSI_DH_RETRY:
	case SCSI_DH_IMM_RETRY:
	case SCSI_DH_RES_TEMP_UNAVAIL:
		if (pg_init_limit_reached(m, pgpath))
			fail_path(pgpath);
		errors = 0;
		break;
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
		DMERR("Could not failover device. Error %d.", errors);
		m->current_pgpath = NULL;
		m->current_pg = NULL;
	} else if (!m->pg_init_required) {
		m->queue_io = 0;
		pg->bypassed = 0;
	}

	m->pg_init_in_progress = 0;
	queue_work(kmultipathd, &m->process_queued_ios);
	spin_unlock_irqrestore(&m->lock, flags);
}

static void activate_path(struct work_struct *work)
{
	int ret;
	struct multipath *m =
		container_of(work, struct multipath, activate_path);
	struct dm_path *path;
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);
	path = &m->pgpath_to_activate->path;
	m->pgpath_to_activate = NULL;
	spin_unlock_irqrestore(&m->lock, flags);
	if (!path)
		return;
	ret = scsi_dh_activate(bdev_get_queue(path->dev->bdev));
	pg_init_done(path, ret);
}

/*
 * end_io handling
 */
static int do_end_io(struct multipath *m, struct bio *bio,
		     int error, struct dm_mpath_io *mpio)
{
	unsigned long flags;

	if (!error)
		return 0;	/* I/O complete */

	if ((error == -EWOULDBLOCK) && bio_rw_ahead(bio))
		return error;

	if (error == -EOPNOTSUPP)
		return error;

	spin_lock_irqsave(&m->lock, flags);
	if (!m->nr_valid_paths) {
		if (__must_push_back(m)) {
			spin_unlock_irqrestore(&m->lock, flags);
			return DM_ENDIO_REQUEUE;
		} else if (!m->queue_if_no_path) {
			spin_unlock_irqrestore(&m->lock, flags);
			return -EIO;
		} else {
			spin_unlock_irqrestore(&m->lock, flags);
			goto requeue;
		}
	}
	spin_unlock_irqrestore(&m->lock, flags);

	if (mpio->pgpath)
		fail_path(mpio->pgpath);

      requeue:
	dm_bio_restore(&mpio->details, bio);

	/* queue for the daemon to resubmit or fail */
	spin_lock_irqsave(&m->lock, flags);
	bio_list_add(&m->queued_ios, bio);
	m->queue_size++;
	if (!m->queue_io)
		queue_work(kmultipathd, &m->process_queued_ios);
	spin_unlock_irqrestore(&m->lock, flags);

	return DM_ENDIO_INCOMPLETE;	/* io not complete */
}

static int multipath_end_io(struct dm_target *ti, struct bio *bio,
			    int error, union map_info *map_context)
{
	struct multipath *m = ti->private;
	struct dm_mpath_io *mpio = map_context->ptr;
	struct pgpath *pgpath = mpio->pgpath;
	struct path_selector *ps;
	int r;

	r  = do_end_io(m, bio, error, mpio);
	if (pgpath) {
		ps = &pgpath->pg->ps;
		if (ps->type->end_io)
			ps->type->end_io(ps, &pgpath->path);
	}
	if (r != DM_ENDIO_INCOMPLETE)
		mempool_free(mpio, m->mpio_pool);

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
	struct multipath *m = (struct multipath *) ti->private;

	queue_if_no_path(m, 0, 1);
}

/*
 * Restore the queue_if_no_path setting.
 */
static void multipath_resume(struct dm_target *ti)
{
	struct multipath *m = (struct multipath *) ti->private;
	unsigned long flags;

	spin_lock_irqsave(&m->lock, flags);
	m->queue_if_no_path = m->saved_queue_if_no_path;
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
static int multipath_status(struct dm_target *ti, status_type_t type,
			    char *result, unsigned int maxlen)
{
	int sz = 0;
	unsigned long flags;
	struct multipath *m = (struct multipath *) ti->private;
	struct priority_group *pg;
	struct pgpath *p;
	unsigned pg_num;
	char state;

	spin_lock_irqsave(&m->lock, flags);

	/* Features */
	if (type == STATUSTYPE_INFO)
		DMEMIT("2 %u %u ", m->queue_size, m->pg_init_count);
	else {
		DMEMIT("%u ", m->queue_if_no_path +
			      (m->pg_init_retries > 0) * 2);
		if (m->queue_if_no_path)
			DMEMIT("queue_if_no_path ");
		if (m->pg_init_retries)
			DMEMIT("pg_init_retries %u ", m->pg_init_retries);
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
			pg_num = 1;

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

	return 0;
}

static int multipath_message(struct dm_target *ti, unsigned argc, char **argv)
{
	int r;
	struct dm_dev *dev;
	struct multipath *m = (struct multipath *) ti->private;
	action_fn action;

	if (argc == 1) {
		if (!strnicmp(argv[0], MESG_STR("queue_if_no_path")))
			return queue_if_no_path(m, 1, 0);
		else if (!strnicmp(argv[0], MESG_STR("fail_if_no_path")))
			return queue_if_no_path(m, 0, 0);
	}

	if (argc != 2)
		goto error;

	if (!strnicmp(argv[0], MESG_STR("disable_group")))
		return bypass_pg_num(m, argv[1], 1);
	else if (!strnicmp(argv[0], MESG_STR("enable_group")))
		return bypass_pg_num(m, argv[1], 0);
	else if (!strnicmp(argv[0], MESG_STR("switch_group")))
		return switch_pg_num(m, argv[1]);
	else if (!strnicmp(argv[0], MESG_STR("reinstate_path")))
		action = reinstate_path;
	else if (!strnicmp(argv[0], MESG_STR("fail_path")))
		action = fail_path;
	else
		goto error;

	r = dm_get_device(ti, argv[1], ti->begin, ti->len,
			  dm_table_get_mode(ti->table), &dev);
	if (r) {
		DMWARN("message: error getting device %s",
		       argv[1]);
		return -EINVAL;
	}

	r = action_dev(m, dev, action);

	dm_put_device(ti, dev);

	return r;

error:
	DMWARN("Unrecognised multipath message received.");
	return -EINVAL;
}

static int multipath_ioctl(struct dm_target *ti, struct inode *inode,
			   struct file *filp, unsigned int cmd,
			   unsigned long arg)
{
	struct multipath *m = (struct multipath *) ti->private;
	struct block_device *bdev = NULL;
	unsigned long flags;
	struct file fake_file = {};
	struct dentry fake_dentry = {};
	int r = 0;

	fake_file.f_path.dentry = &fake_dentry;

	spin_lock_irqsave(&m->lock, flags);

	if (!m->current_pgpath)
		__choose_pgpath(m);

	if (m->current_pgpath) {
		bdev = m->current_pgpath->path.dev->bdev;
		fake_dentry.d_inode = bdev->bd_inode;
		fake_file.f_mode = m->current_pgpath->path.dev->mode;
	}

	if (m->queue_io)
		r = -EAGAIN;
	else if (!bdev)
		r = -EIO;

	spin_unlock_irqrestore(&m->lock, flags);

	return r ? : blkdev_driver_ioctl(bdev->bd_inode, &fake_file,
					 bdev->bd_disk, cmd, arg);
}

/*-----------------------------------------------------------------
 * Module setup
 *---------------------------------------------------------------*/
static struct target_type multipath_target = {
	.name = "multipath",
	.version = {1, 0, 5},
	.module = THIS_MODULE,
	.ctr = multipath_ctr,
	.dtr = multipath_dtr,
	.map = multipath_map,
	.end_io = multipath_end_io,
	.presuspend = multipath_presuspend,
	.resume = multipath_resume,
	.status = multipath_status,
	.message = multipath_message,
	.ioctl  = multipath_ioctl,
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
		kmem_cache_destroy(_mpio_cache);
		return -EINVAL;
	}

	kmultipathd = create_workqueue("kmpathd");
	if (!kmultipathd) {
		DMERR("failed to create workqueue kmpathd");
		dm_unregister_target(&multipath_target);
		kmem_cache_destroy(_mpio_cache);
		return -ENOMEM;
	}

	/*
	 * A separate workqueue is used to handle the device handlers
	 * to avoid overloading existing workqueue. Overloading the
	 * old workqueue would also create a bottleneck in the
	 * path of the storage hardware device activation.
	 */
	kmpath_handlerd = create_singlethread_workqueue("kmpath_handlerd");
	if (!kmpath_handlerd) {
		DMERR("failed to create workqueue kmpath_handlerd");
		destroy_workqueue(kmultipathd);
		dm_unregister_target(&multipath_target);
		kmem_cache_destroy(_mpio_cache);
		return -ENOMEM;
	}

	DMINFO("version %u.%u.%u loaded",
	       multipath_target.version[0], multipath_target.version[1],
	       multipath_target.version[2]);

	return r;
}

static void __exit dm_multipath_exit(void)
{
	int r;

	destroy_workqueue(kmpath_handlerd);
	destroy_workqueue(kmultipathd);

	r = dm_unregister_target(&multipath_target);
	if (r < 0)
		DMERR("target unregister failed %d", r);
	kmem_cache_destroy(_mpio_cache);
}

module_init(dm_multipath_init);
module_exit(dm_multipath_exit);

MODULE_DESCRIPTION(DM_NAME " multipath target");
MODULE_AUTHOR("Sistina Software <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
