/*
 * Common Block IO controller cgroup interface
 *
 * Based on ideas and code from CFQ, CFS and BFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2009 Vivek Goyal <vgoyal@redhat.com>
 * 	              Nauman Rafique <nauman@google.com>
 */
#include <linux/ioprio.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/genhd.h>
#include <linux/delay.h>
#include <linux/atomic.h>
#include "blk-cgroup.h"
#include "blk.h"

#define MAX_KEY_LEN 100

static DEFINE_MUTEX(blkcg_pol_mutex);
static DEFINE_MUTEX(all_q_mutex);
static LIST_HEAD(all_q_list);

struct blkio_cgroup blkio_root_cgroup = { .cfq_weight = 2 * CFQ_WEIGHT_DEFAULT };
EXPORT_SYMBOL_GPL(blkio_root_cgroup);

static struct blkio_policy_type *blkio_policy[BLKCG_MAX_POLS];

struct blkio_cgroup *cgroup_to_blkio_cgroup(struct cgroup *cgroup)
{
	return container_of(cgroup_subsys_state(cgroup, blkio_subsys_id),
			    struct blkio_cgroup, css);
}
EXPORT_SYMBOL_GPL(cgroup_to_blkio_cgroup);

static struct blkio_cgroup *task_blkio_cgroup(struct task_struct *tsk)
{
	return container_of(task_subsys_state(tsk, blkio_subsys_id),
			    struct blkio_cgroup, css);
}

struct blkio_cgroup *bio_blkio_cgroup(struct bio *bio)
{
	if (bio && bio->bi_css)
		return container_of(bio->bi_css, struct blkio_cgroup, css);
	return task_blkio_cgroup(current);
}
EXPORT_SYMBOL_GPL(bio_blkio_cgroup);

static bool blkcg_policy_enabled(struct request_queue *q,
				 const struct blkio_policy_type *pol)
{
	return pol && test_bit(pol->plid, q->blkcg_pols);
}

static size_t blkg_pd_size(const struct blkio_policy_type *pol)
{
	return sizeof(struct blkg_policy_data) + pol->pdata_size;
}

/**
 * blkg_free - free a blkg
 * @blkg: blkg to free
 *
 * Free @blkg which may be partially allocated.
 */
static void blkg_free(struct blkio_group *blkg)
{
	int i;

	if (!blkg)
		return;

	for (i = 0; i < BLKCG_MAX_POLS; i++) {
		struct blkio_policy_type *pol = blkio_policy[i];
		struct blkg_policy_data *pd = blkg->pd[i];

		if (!pd)
			continue;

		if (pol && pol->ops.blkio_exit_group_fn)
			pol->ops.blkio_exit_group_fn(blkg);

		kfree(pd);
	}

	kfree(blkg);
}

/**
 * blkg_alloc - allocate a blkg
 * @blkcg: block cgroup the new blkg is associated with
 * @q: request_queue the new blkg is associated with
 *
 * Allocate a new blkg assocating @blkcg and @q.
 */
static struct blkio_group *blkg_alloc(struct blkio_cgroup *blkcg,
				      struct request_queue *q)
{
	struct blkio_group *blkg;
	int i;

	/* alloc and init base part */
	blkg = kzalloc_node(sizeof(*blkg), GFP_ATOMIC, q->node);
	if (!blkg)
		return NULL;

	blkg->q = q;
	INIT_LIST_HEAD(&blkg->q_node);
	blkg->blkcg = blkcg;
	blkg->refcnt = 1;
	cgroup_path(blkcg->css.cgroup, blkg->path, sizeof(blkg->path));

	for (i = 0; i < BLKCG_MAX_POLS; i++) {
		struct blkio_policy_type *pol = blkio_policy[i];
		struct blkg_policy_data *pd;

		if (!blkcg_policy_enabled(q, pol))
			continue;

		/* alloc per-policy data and attach it to blkg */
		pd = kzalloc_node(blkg_pd_size(pol), GFP_ATOMIC, q->node);
		if (!pd) {
			blkg_free(blkg);
			return NULL;
		}

		blkg->pd[i] = pd;
		pd->blkg = blkg;
	}

	/* invoke per-policy init */
	for (i = 0; i < BLKCG_MAX_POLS; i++) {
		struct blkio_policy_type *pol = blkio_policy[i];

		if (blkcg_policy_enabled(blkg->q, pol))
			pol->ops.blkio_init_group_fn(blkg);
	}

	return blkg;
}

static struct blkio_group *__blkg_lookup(struct blkio_cgroup *blkcg,
					 struct request_queue *q)
{
	struct blkio_group *blkg;
	struct hlist_node *n;

	hlist_for_each_entry_rcu(blkg, n, &blkcg->blkg_list, blkcg_node)
		if (blkg->q == q)
			return blkg;
	return NULL;
}

/**
 * blkg_lookup - lookup blkg for the specified blkcg - q pair
 * @blkcg: blkcg of interest
 * @q: request_queue of interest
 *
 * Lookup blkg for the @blkcg - @q pair.  This function should be called
 * under RCU read lock and is guaranteed to return %NULL if @q is bypassing
 * - see blk_queue_bypass_start() for details.
 */
struct blkio_group *blkg_lookup(struct blkio_cgroup *blkcg,
				struct request_queue *q)
{
	WARN_ON_ONCE(!rcu_read_lock_held());

	if (unlikely(blk_queue_bypass(q)))
		return NULL;
	return __blkg_lookup(blkcg, q);
}
EXPORT_SYMBOL_GPL(blkg_lookup);

struct blkio_group *blkg_lookup_create(struct blkio_cgroup *blkcg,
				       struct request_queue *q,
				       bool for_root)
	__releases(q->queue_lock) __acquires(q->queue_lock)
{
	struct blkio_group *blkg;

	WARN_ON_ONCE(!rcu_read_lock_held());
	lockdep_assert_held(q->queue_lock);

	/*
	 * This could be the first entry point of blkcg implementation and
	 * we shouldn't allow anything to go through for a bypassing queue.
	 */
	if (unlikely(blk_queue_bypass(q)) && !for_root)
		return ERR_PTR(blk_queue_dead(q) ? -EINVAL : -EBUSY);

	blkg = __blkg_lookup(blkcg, q);
	if (blkg)
		return blkg;

	/* blkg holds a reference to blkcg */
	if (!css_tryget(&blkcg->css))
		return ERR_PTR(-EINVAL);

	/*
	 * Allocate and initialize.
	 */
	blkg = blkg_alloc(blkcg, q);

	/* did alloc fail? */
	if (unlikely(!blkg)) {
		blkg = ERR_PTR(-ENOMEM);
		goto out;
	}

	/* insert */
	spin_lock(&blkcg->lock);
	hlist_add_head_rcu(&blkg->blkcg_node, &blkcg->blkg_list);
	list_add(&blkg->q_node, &q->blkg_list);
	spin_unlock(&blkcg->lock);
out:
	return blkg;
}
EXPORT_SYMBOL_GPL(blkg_lookup_create);

static void blkg_destroy(struct blkio_group *blkg)
{
	struct request_queue *q = blkg->q;
	struct blkio_cgroup *blkcg = blkg->blkcg;

	lockdep_assert_held(q->queue_lock);
	lockdep_assert_held(&blkcg->lock);

	/* Something wrong if we are trying to remove same group twice */
	WARN_ON_ONCE(list_empty(&blkg->q_node));
	WARN_ON_ONCE(hlist_unhashed(&blkg->blkcg_node));
	list_del_init(&blkg->q_node);
	hlist_del_init_rcu(&blkg->blkcg_node);

	/*
	 * Put the reference taken at the time of creation so that when all
	 * queues are gone, group can be destroyed.
	 */
	blkg_put(blkg);
}

/**
 * blkg_destroy_all - destroy all blkgs associated with a request_queue
 * @q: request_queue of interest
 * @destroy_root: whether to destroy root blkg or not
 *
 * Destroy blkgs associated with @q.  If @destroy_root is %true, all are
 * destroyed; otherwise, root blkg is left alone.
 */
void blkg_destroy_all(struct request_queue *q, bool destroy_root)
{
	struct blkio_group *blkg, *n;

	spin_lock_irq(q->queue_lock);

	list_for_each_entry_safe(blkg, n, &q->blkg_list, q_node) {
		struct blkio_cgroup *blkcg = blkg->blkcg;

		/* skip root? */
		if (!destroy_root && blkg->blkcg == &blkio_root_cgroup)
			continue;

		spin_lock(&blkcg->lock);
		blkg_destroy(blkg);
		spin_unlock(&blkcg->lock);
	}

	spin_unlock_irq(q->queue_lock);
}
EXPORT_SYMBOL_GPL(blkg_destroy_all);

static void blkg_rcu_free(struct rcu_head *rcu_head)
{
	blkg_free(container_of(rcu_head, struct blkio_group, rcu_head));
}

void __blkg_release(struct blkio_group *blkg)
{
	/* release the extra blkcg reference this blkg has been holding */
	css_put(&blkg->blkcg->css);

	/*
	 * A group is freed in rcu manner. But having an rcu lock does not
	 * mean that one can access all the fields of blkg and assume these
	 * are valid. For example, don't try to follow throtl_data and
	 * request queue links.
	 *
	 * Having a reference to blkg under an rcu allows acess to only
	 * values local to groups like group stats and group rate limits
	 */
	call_rcu(&blkg->rcu_head, blkg_rcu_free);
}
EXPORT_SYMBOL_GPL(__blkg_release);

static int
blkiocg_reset_stats(struct cgroup *cgroup, struct cftype *cftype, u64 val)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgroup);
	struct blkio_group *blkg;
	struct hlist_node *n;
	int i;

	mutex_lock(&blkcg_pol_mutex);
	spin_lock_irq(&blkcg->lock);

	/*
	 * Note that stat reset is racy - it doesn't synchronize against
	 * stat updates.  This is a debug feature which shouldn't exist
	 * anyway.  If you get hit by a race, retry.
	 */
	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node) {
		for (i = 0; i < BLKCG_MAX_POLS; i++) {
			struct blkio_policy_type *pol = blkio_policy[i];

			if (blkcg_policy_enabled(blkg->q, pol) &&
			    pol->ops.blkio_reset_group_stats_fn)
				pol->ops.blkio_reset_group_stats_fn(blkg);
		}
	}

	spin_unlock_irq(&blkcg->lock);
	mutex_unlock(&blkcg_pol_mutex);
	return 0;
}

static const char *blkg_dev_name(struct blkio_group *blkg)
{
	/* some drivers (floppy) instantiate a queue w/o disk registered */
	if (blkg->q->backing_dev_info.dev)
		return dev_name(blkg->q->backing_dev_info.dev);
	return NULL;
}

/**
 * blkcg_print_blkgs - helper for printing per-blkg data
 * @sf: seq_file to print to
 * @blkcg: blkcg of interest
 * @prfill: fill function to print out a blkg
 * @pol: policy in question
 * @data: data to be passed to @prfill
 * @show_total: to print out sum of prfill return values or not
 *
 * This function invokes @prfill on each blkg of @blkcg if pd for the
 * policy specified by @pol exists.  @prfill is invoked with @sf, the
 * policy data and @data.  If @show_total is %true, the sum of the return
 * values from @prfill is printed with "Total" label at the end.
 *
 * This is to be used to construct print functions for
 * cftype->read_seq_string method.
 */
void blkcg_print_blkgs(struct seq_file *sf, struct blkio_cgroup *blkcg,
		       u64 (*prfill)(struct seq_file *, void *, int),
		       const struct blkio_policy_type *pol, int data,
		       bool show_total)
{
	struct blkio_group *blkg;
	struct hlist_node *n;
	u64 total = 0;

	spin_lock_irq(&blkcg->lock);
	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node)
		if (blkcg_policy_enabled(blkg->q, pol))
			total += prfill(sf, blkg->pd[pol->plid]->pdata, data);
	spin_unlock_irq(&blkcg->lock);

	if (show_total)
		seq_printf(sf, "Total %llu\n", (unsigned long long)total);
}
EXPORT_SYMBOL_GPL(blkcg_print_blkgs);

/**
 * __blkg_prfill_u64 - prfill helper for a single u64 value
 * @sf: seq_file to print to
 * @pdata: policy private data of interest
 * @v: value to print
 *
 * Print @v to @sf for the device assocaited with @pdata.
 */
u64 __blkg_prfill_u64(struct seq_file *sf, void *pdata, u64 v)
{
	const char *dname = blkg_dev_name(pdata_to_blkg(pdata));

	if (!dname)
		return 0;

	seq_printf(sf, "%s %llu\n", dname, (unsigned long long)v);
	return v;
}
EXPORT_SYMBOL_GPL(__blkg_prfill_u64);

/**
 * __blkg_prfill_rwstat - prfill helper for a blkg_rwstat
 * @sf: seq_file to print to
 * @pdata: policy private data of interest
 * @rwstat: rwstat to print
 *
 * Print @rwstat to @sf for the device assocaited with @pdata.
 */
u64 __blkg_prfill_rwstat(struct seq_file *sf, void *pdata,
			 const struct blkg_rwstat *rwstat)
{
	static const char *rwstr[] = {
		[BLKG_RWSTAT_READ]	= "Read",
		[BLKG_RWSTAT_WRITE]	= "Write",
		[BLKG_RWSTAT_SYNC]	= "Sync",
		[BLKG_RWSTAT_ASYNC]	= "Async",
	};
	const char *dname = blkg_dev_name(pdata_to_blkg(pdata));
	u64 v;
	int i;

	if (!dname)
		return 0;

	for (i = 0; i < BLKG_RWSTAT_NR; i++)
		seq_printf(sf, "%s %s %llu\n", dname, rwstr[i],
			   (unsigned long long)rwstat->cnt[i]);

	v = rwstat->cnt[BLKG_RWSTAT_READ] + rwstat->cnt[BLKG_RWSTAT_WRITE];
	seq_printf(sf, "%s Total %llu\n", dname, (unsigned long long)v);
	return v;
}

/**
 * blkg_prfill_stat - prfill callback for blkg_stat
 * @sf: seq_file to print to
 * @pdata: policy private data of interest
 * @off: offset to the blkg_stat in @pdata
 *
 * prfill callback for printing a blkg_stat.
 */
u64 blkg_prfill_stat(struct seq_file *sf, void *pdata, int off)
{
	return __blkg_prfill_u64(sf, pdata, blkg_stat_read(pdata + off));
}
EXPORT_SYMBOL_GPL(blkg_prfill_stat);

/**
 * blkg_prfill_rwstat - prfill callback for blkg_rwstat
 * @sf: seq_file to print to
 * @pdata: policy private data of interest
 * @off: offset to the blkg_rwstat in @pdata
 *
 * prfill callback for printing a blkg_rwstat.
 */
u64 blkg_prfill_rwstat(struct seq_file *sf, void *pdata, int off)
{
	struct blkg_rwstat rwstat = blkg_rwstat_read(pdata + off);

	return __blkg_prfill_rwstat(sf, pdata, &rwstat);
}
EXPORT_SYMBOL_GPL(blkg_prfill_rwstat);

/**
 * blkg_conf_prep - parse and prepare for per-blkg config update
 * @blkcg: target block cgroup
 * @pol: target policy
 * @input: input string
 * @ctx: blkg_conf_ctx to be filled
 *
 * Parse per-blkg config update from @input and initialize @ctx with the
 * result.  @ctx->blkg points to the blkg to be updated and @ctx->v the new
 * value.  This function returns with RCU read lock and queue lock held and
 * must be paired with blkg_conf_finish().
 */
int blkg_conf_prep(struct blkio_cgroup *blkcg,
		   const struct blkio_policy_type *pol, const char *input,
		   struct blkg_conf_ctx *ctx)
	__acquires(rcu) __acquires(disk->queue->queue_lock)
{
	struct gendisk *disk;
	struct blkio_group *blkg;
	unsigned int major, minor;
	unsigned long long v;
	int part, ret;

	if (sscanf(input, "%u:%u %llu", &major, &minor, &v) != 3)
		return -EINVAL;

	disk = get_gendisk(MKDEV(major, minor), &part);
	if (!disk || part)
		return -EINVAL;

	rcu_read_lock();
	spin_lock_irq(disk->queue->queue_lock);

	if (blkcg_policy_enabled(disk->queue, pol))
		blkg = blkg_lookup_create(blkcg, disk->queue, false);
	else
		blkg = ERR_PTR(-EINVAL);

	if (IS_ERR(blkg)) {
		ret = PTR_ERR(blkg);
		rcu_read_unlock();
		spin_unlock_irq(disk->queue->queue_lock);
		put_disk(disk);
		/*
		 * If queue was bypassing, we should retry.  Do so after a
		 * short msleep().  It isn't strictly necessary but queue
		 * can be bypassing for some time and it's always nice to
		 * avoid busy looping.
		 */
		if (ret == -EBUSY) {
			msleep(10);
			ret = restart_syscall();
		}
		return ret;
	}

	ctx->disk = disk;
	ctx->blkg = blkg;
	ctx->v = v;
	return 0;
}
EXPORT_SYMBOL_GPL(blkg_conf_prep);

/**
 * blkg_conf_finish - finish up per-blkg config update
 * @ctx: blkg_conf_ctx intiailized by blkg_conf_prep()
 *
 * Finish up after per-blkg config update.  This function must be paired
 * with blkg_conf_prep().
 */
void blkg_conf_finish(struct blkg_conf_ctx *ctx)
	__releases(ctx->disk->queue->queue_lock) __releases(rcu)
{
	spin_unlock_irq(ctx->disk->queue->queue_lock);
	rcu_read_unlock();
	put_disk(ctx->disk);
}
EXPORT_SYMBOL_GPL(blkg_conf_finish);

struct cftype blkio_files[] = {
	{
		.name = "reset_stats",
		.write_u64 = blkiocg_reset_stats,
	},
	{ }	/* terminate */
};

/**
 * blkiocg_pre_destroy - cgroup pre_destroy callback
 * @cgroup: cgroup of interest
 *
 * This function is called when @cgroup is about to go away and responsible
 * for shooting down all blkgs associated with @cgroup.  blkgs should be
 * removed while holding both q and blkcg locks.  As blkcg lock is nested
 * inside q lock, this function performs reverse double lock dancing.
 *
 * This is the blkcg counterpart of ioc_release_fn().
 */
static int blkiocg_pre_destroy(struct cgroup *cgroup)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgroup);

	spin_lock_irq(&blkcg->lock);

	while (!hlist_empty(&blkcg->blkg_list)) {
		struct blkio_group *blkg = hlist_entry(blkcg->blkg_list.first,
						struct blkio_group, blkcg_node);
		struct request_queue *q = blkg->q;

		if (spin_trylock(q->queue_lock)) {
			blkg_destroy(blkg);
			spin_unlock(q->queue_lock);
		} else {
			spin_unlock_irq(&blkcg->lock);
			cpu_relax();
			spin_lock_irq(&blkcg->lock);
		}
	}

	spin_unlock_irq(&blkcg->lock);
	return 0;
}

static void blkiocg_destroy(struct cgroup *cgroup)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgroup);

	if (blkcg != &blkio_root_cgroup)
		kfree(blkcg);
}

static struct cgroup_subsys_state *blkiocg_create(struct cgroup *cgroup)
{
	static atomic64_t id_seq = ATOMIC64_INIT(0);
	struct blkio_cgroup *blkcg;
	struct cgroup *parent = cgroup->parent;

	if (!parent) {
		blkcg = &blkio_root_cgroup;
		goto done;
	}

	blkcg = kzalloc(sizeof(*blkcg), GFP_KERNEL);
	if (!blkcg)
		return ERR_PTR(-ENOMEM);

	blkcg->cfq_weight = CFQ_WEIGHT_DEFAULT;
	blkcg->id = atomic64_inc_return(&id_seq); /* root is 0, start from 1 */
done:
	spin_lock_init(&blkcg->lock);
	INIT_HLIST_HEAD(&blkcg->blkg_list);

	return &blkcg->css;
}

/**
 * blkcg_init_queue - initialize blkcg part of request queue
 * @q: request_queue to initialize
 *
 * Called from blk_alloc_queue_node(). Responsible for initializing blkcg
 * part of new request_queue @q.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int blkcg_init_queue(struct request_queue *q)
{
	int ret;

	might_sleep();

	ret = blk_throtl_init(q);
	if (ret)
		return ret;

	mutex_lock(&all_q_mutex);
	INIT_LIST_HEAD(&q->all_q_node);
	list_add_tail(&q->all_q_node, &all_q_list);
	mutex_unlock(&all_q_mutex);

	return 0;
}

/**
 * blkcg_drain_queue - drain blkcg part of request_queue
 * @q: request_queue to drain
 *
 * Called from blk_drain_queue().  Responsible for draining blkcg part.
 */
void blkcg_drain_queue(struct request_queue *q)
{
	lockdep_assert_held(q->queue_lock);

	blk_throtl_drain(q);
}

/**
 * blkcg_exit_queue - exit and release blkcg part of request_queue
 * @q: request_queue being released
 *
 * Called from blk_release_queue().  Responsible for exiting blkcg part.
 */
void blkcg_exit_queue(struct request_queue *q)
{
	mutex_lock(&all_q_mutex);
	list_del_init(&q->all_q_node);
	mutex_unlock(&all_q_mutex);

	blkg_destroy_all(q, true);

	blk_throtl_exit(q);
}

/*
 * We cannot support shared io contexts, as we have no mean to support
 * two tasks with the same ioc in two different groups without major rework
 * of the main cic data structures.  For now we allow a task to change
 * its cgroup only if it's the only owner of its ioc.
 */
static int blkiocg_can_attach(struct cgroup *cgrp, struct cgroup_taskset *tset)
{
	struct task_struct *task;
	struct io_context *ioc;
	int ret = 0;

	/* task_lock() is needed to avoid races with exit_io_context() */
	cgroup_taskset_for_each(task, cgrp, tset) {
		task_lock(task);
		ioc = task->io_context;
		if (ioc && atomic_read(&ioc->nr_tasks) > 1)
			ret = -EINVAL;
		task_unlock(task);
		if (ret)
			break;
	}
	return ret;
}

struct cgroup_subsys blkio_subsys = {
	.name = "blkio",
	.create = blkiocg_create,
	.can_attach = blkiocg_can_attach,
	.pre_destroy = blkiocg_pre_destroy,
	.destroy = blkiocg_destroy,
	.subsys_id = blkio_subsys_id,
	.base_cftypes = blkio_files,
	.module = THIS_MODULE,
};
EXPORT_SYMBOL_GPL(blkio_subsys);

/**
 * blkcg_activate_policy - activate a blkcg policy on a request_queue
 * @q: request_queue of interest
 * @pol: blkcg policy to activate
 *
 * Activate @pol on @q.  Requires %GFP_KERNEL context.  @q goes through
 * bypass mode to populate its blkgs with policy_data for @pol.
 *
 * Activation happens with @q bypassed, so nobody would be accessing blkgs
 * from IO path.  Update of each blkg is protected by both queue and blkcg
 * locks so that holding either lock and testing blkcg_policy_enabled() is
 * always enough for dereferencing policy data.
 *
 * The caller is responsible for synchronizing [de]activations and policy
 * [un]registerations.  Returns 0 on success, -errno on failure.
 */
int blkcg_activate_policy(struct request_queue *q,
			  const struct blkio_policy_type *pol)
{
	LIST_HEAD(pds);
	struct blkio_group *blkg;
	struct blkg_policy_data *pd, *n;
	int cnt = 0, ret;

	if (blkcg_policy_enabled(q, pol))
		return 0;

	blk_queue_bypass_start(q);

	/* make sure the root blkg exists and count the existing blkgs */
	spin_lock_irq(q->queue_lock);

	rcu_read_lock();
	blkg = blkg_lookup_create(&blkio_root_cgroup, q, true);
	rcu_read_unlock();

	if (IS_ERR(blkg)) {
		ret = PTR_ERR(blkg);
		goto out_unlock;
	}
	q->root_blkg = blkg;

	list_for_each_entry(blkg, &q->blkg_list, q_node)
		cnt++;

	spin_unlock_irq(q->queue_lock);

	/* allocate policy_data for all existing blkgs */
	while (cnt--) {
		pd = kzalloc_node(blkg_pd_size(pol), GFP_KERNEL, q->node);
		if (!pd) {
			ret = -ENOMEM;
			goto out_free;
		}
		list_add_tail(&pd->alloc_node, &pds);
	}

	/*
	 * Install the allocated pds.  With @q bypassing, no new blkg
	 * should have been created while the queue lock was dropped.
	 */
	spin_lock_irq(q->queue_lock);

	list_for_each_entry(blkg, &q->blkg_list, q_node) {
		if (WARN_ON(list_empty(&pds))) {
			/* umm... this shouldn't happen, just abort */
			ret = -ENOMEM;
			goto out_unlock;
		}
		pd = list_first_entry(&pds, struct blkg_policy_data, alloc_node);
		list_del_init(&pd->alloc_node);

		/* grab blkcg lock too while installing @pd on @blkg */
		spin_lock(&blkg->blkcg->lock);

		blkg->pd[pol->plid] = pd;
		pd->blkg = blkg;
		pol->ops.blkio_init_group_fn(blkg);

		spin_unlock(&blkg->blkcg->lock);
	}

	__set_bit(pol->plid, q->blkcg_pols);
	ret = 0;
out_unlock:
	spin_unlock_irq(q->queue_lock);
out_free:
	blk_queue_bypass_end(q);
	list_for_each_entry_safe(pd, n, &pds, alloc_node)
		kfree(pd);
	return ret;
}
EXPORT_SYMBOL_GPL(blkcg_activate_policy);

/**
 * blkcg_deactivate_policy - deactivate a blkcg policy on a request_queue
 * @q: request_queue of interest
 * @pol: blkcg policy to deactivate
 *
 * Deactivate @pol on @q.  Follows the same synchronization rules as
 * blkcg_activate_policy().
 */
void blkcg_deactivate_policy(struct request_queue *q,
			     const struct blkio_policy_type *pol)
{
	struct blkio_group *blkg;

	if (!blkcg_policy_enabled(q, pol))
		return;

	blk_queue_bypass_start(q);
	spin_lock_irq(q->queue_lock);

	__clear_bit(pol->plid, q->blkcg_pols);

	list_for_each_entry(blkg, &q->blkg_list, q_node) {
		/* grab blkcg lock too while removing @pd from @blkg */
		spin_lock(&blkg->blkcg->lock);

		if (pol->ops.blkio_exit_group_fn)
			pol->ops.blkio_exit_group_fn(blkg);

		kfree(blkg->pd[pol->plid]);
		blkg->pd[pol->plid] = NULL;

		spin_unlock(&blkg->blkcg->lock);
	}

	spin_unlock_irq(q->queue_lock);
	blk_queue_bypass_end(q);
}
EXPORT_SYMBOL_GPL(blkcg_deactivate_policy);

/**
 * blkio_policy_register - register a blkcg policy
 * @blkiop: blkcg policy to register
 *
 * Register @blkiop with blkcg core.  Might sleep and @blkiop may be
 * modified on successful registration.  Returns 0 on success and -errno on
 * failure.
 */
int blkio_policy_register(struct blkio_policy_type *blkiop)
{
	int i, ret;

	mutex_lock(&blkcg_pol_mutex);

	/* find an empty slot */
	ret = -ENOSPC;
	for (i = 0; i < BLKCG_MAX_POLS; i++)
		if (!blkio_policy[i])
			break;
	if (i >= BLKCG_MAX_POLS)
		goto out_unlock;

	/* register and update blkgs */
	blkiop->plid = i;
	blkio_policy[i] = blkiop;

	/* everything is in place, add intf files for the new policy */
	if (blkiop->cftypes)
		WARN_ON(cgroup_add_cftypes(&blkio_subsys, blkiop->cftypes));
	ret = 0;
out_unlock:
	mutex_unlock(&blkcg_pol_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(blkio_policy_register);

/**
 * blkiop_policy_unregister - unregister a blkcg policy
 * @blkiop: blkcg policy to unregister
 *
 * Undo blkio_policy_register(@blkiop).  Might sleep.
 */
void blkio_policy_unregister(struct blkio_policy_type *blkiop)
{
	mutex_lock(&blkcg_pol_mutex);

	if (WARN_ON(blkio_policy[blkiop->plid] != blkiop))
		goto out_unlock;

	/* kill the intf files first */
	if (blkiop->cftypes)
		cgroup_rm_cftypes(&blkio_subsys, blkiop->cftypes);

	/* unregister and update blkgs */
	blkio_policy[blkiop->plid] = NULL;
out_unlock:
	mutex_unlock(&blkcg_pol_mutex);
}
EXPORT_SYMBOL_GPL(blkio_policy_unregister);
