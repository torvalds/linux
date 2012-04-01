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
#include <linux/seq_file.h>
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

static DEFINE_SPINLOCK(blkio_list_lock);
static LIST_HEAD(blkio_list);

static DEFINE_MUTEX(all_q_mutex);
static LIST_HEAD(all_q_list);

/* List of groups pending per cpu stats allocation */
static DEFINE_SPINLOCK(alloc_list_lock);
static LIST_HEAD(alloc_list);

static void blkio_stat_alloc_fn(struct work_struct *);
static DECLARE_DELAYED_WORK(blkio_stat_alloc_work, blkio_stat_alloc_fn);

struct blkio_cgroup blkio_root_cgroup = { .weight = 2*BLKIO_WEIGHT_DEFAULT };
EXPORT_SYMBOL_GPL(blkio_root_cgroup);

static struct blkio_policy_type *blkio_policy[BLKIO_NR_POLICIES];

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

static inline void blkio_update_group_weight(struct blkio_group *blkg,
					     int plid, unsigned int weight)
{
	struct blkio_policy_type *blkiop;

	list_for_each_entry(blkiop, &blkio_list, list) {
		/* If this policy does not own the blkg, do not send updates */
		if (blkiop->plid != plid)
			continue;
		if (blkiop->ops.blkio_update_group_weight_fn)
			blkiop->ops.blkio_update_group_weight_fn(blkg->q,
							blkg, weight);
	}
}

static inline void blkio_update_group_bps(struct blkio_group *blkg, int plid,
					  u64 bps, int rw)
{
	struct blkio_policy_type *blkiop;

	list_for_each_entry(blkiop, &blkio_list, list) {

		/* If this policy does not own the blkg, do not send updates */
		if (blkiop->plid != plid)
			continue;

		if (rw == READ && blkiop->ops.blkio_update_group_read_bps_fn)
			blkiop->ops.blkio_update_group_read_bps_fn(blkg->q,
								blkg, bps);

		if (rw == WRITE && blkiop->ops.blkio_update_group_write_bps_fn)
			blkiop->ops.blkio_update_group_write_bps_fn(blkg->q,
								blkg, bps);
	}
}

static inline void blkio_update_group_iops(struct blkio_group *blkg, int plid,
					   u64 iops, int rw)
{
	struct blkio_policy_type *blkiop;

	list_for_each_entry(blkiop, &blkio_list, list) {

		/* If this policy does not own the blkg, do not send updates */
		if (blkiop->plid != plid)
			continue;

		if (rw == READ && blkiop->ops.blkio_update_group_read_iops_fn)
			blkiop->ops.blkio_update_group_read_iops_fn(blkg->q,
								blkg, iops);

		if (rw == WRITE && blkiop->ops.blkio_update_group_write_iops_fn)
			blkiop->ops.blkio_update_group_write_iops_fn(blkg->q,
								blkg,iops);
	}
}

#ifdef CONFIG_DEBUG_BLK_CGROUP
/* This should be called with the queue_lock held. */
static void blkio_set_start_group_wait_time(struct blkio_group *blkg,
					    struct blkio_policy_type *pol,
					    struct blkio_group *curr_blkg)
{
	struct blkg_policy_data *pd = blkg->pd[pol->plid];

	if (blkio_blkg_waiting(&pd->stats))
		return;
	if (blkg == curr_blkg)
		return;
	pd->stats.start_group_wait_time = sched_clock();
	blkio_mark_blkg_waiting(&pd->stats);
}

/* This should be called with the queue_lock held. */
static void blkio_update_group_wait_time(struct blkio_group_stats *stats)
{
	unsigned long long now;

	if (!blkio_blkg_waiting(stats))
		return;

	now = sched_clock();
	if (time_after64(now, stats->start_group_wait_time))
		blkg_stat_add(&stats->group_wait_time,
			      now - stats->start_group_wait_time);
	blkio_clear_blkg_waiting(stats);
}

/* This should be called with the queue_lock held. */
static void blkio_end_empty_time(struct blkio_group_stats *stats)
{
	unsigned long long now;

	if (!blkio_blkg_empty(stats))
		return;

	now = sched_clock();
	if (time_after64(now, stats->start_empty_time))
		blkg_stat_add(&stats->empty_time,
			      now - stats->start_empty_time);
	blkio_clear_blkg_empty(stats);
}

void blkiocg_update_set_idle_time_stats(struct blkio_group *blkg,
					struct blkio_policy_type *pol)
{
	struct blkio_group_stats *stats = &blkg->pd[pol->plid]->stats;

	lockdep_assert_held(blkg->q->queue_lock);
	BUG_ON(blkio_blkg_idling(stats));

	stats->start_idle_time = sched_clock();
	blkio_mark_blkg_idling(stats);
}
EXPORT_SYMBOL_GPL(blkiocg_update_set_idle_time_stats);

void blkiocg_update_idle_time_stats(struct blkio_group *blkg,
				    struct blkio_policy_type *pol)
{
	struct blkio_group_stats *stats = &blkg->pd[pol->plid]->stats;

	lockdep_assert_held(blkg->q->queue_lock);

	if (blkio_blkg_idling(stats)) {
		unsigned long long now = sched_clock();

		if (time_after64(now, stats->start_idle_time))
			blkg_stat_add(&stats->idle_time,
				      now - stats->start_idle_time);
		blkio_clear_blkg_idling(stats);
	}
}
EXPORT_SYMBOL_GPL(blkiocg_update_idle_time_stats);

void blkiocg_update_avg_queue_size_stats(struct blkio_group *blkg,
					 struct blkio_policy_type *pol)
{
	struct blkio_group_stats *stats = &blkg->pd[pol->plid]->stats;

	lockdep_assert_held(blkg->q->queue_lock);

	blkg_stat_add(&stats->avg_queue_size_sum,
		      blkg_rwstat_sum(&stats->queued));
	blkg_stat_add(&stats->avg_queue_size_samples, 1);
	blkio_update_group_wait_time(stats);
}
EXPORT_SYMBOL_GPL(blkiocg_update_avg_queue_size_stats);

void blkiocg_set_start_empty_time(struct blkio_group *blkg,
				  struct blkio_policy_type *pol)
{
	struct blkio_group_stats *stats = &blkg->pd[pol->plid]->stats;

	lockdep_assert_held(blkg->q->queue_lock);

	if (blkg_rwstat_sum(&stats->queued))
		return;

	/*
	 * group is already marked empty. This can happen if cfqq got new
	 * request in parent group and moved to this group while being added
	 * to service tree. Just ignore the event and move on.
	 */
	if (blkio_blkg_empty(stats))
		return;

	stats->start_empty_time = sched_clock();
	blkio_mark_blkg_empty(stats);
}
EXPORT_SYMBOL_GPL(blkiocg_set_start_empty_time);

void blkiocg_update_dequeue_stats(struct blkio_group *blkg,
				  struct blkio_policy_type *pol,
				  unsigned long dequeue)
{
	struct blkg_policy_data *pd = blkg->pd[pol->plid];

	lockdep_assert_held(blkg->q->queue_lock);

	blkg_stat_add(&pd->stats.dequeue, dequeue);
}
EXPORT_SYMBOL_GPL(blkiocg_update_dequeue_stats);
#else
static inline void blkio_set_start_group_wait_time(struct blkio_group *blkg,
					struct blkio_policy_type *pol,
					struct blkio_group *curr_blkg) { }
static inline void blkio_end_empty_time(struct blkio_group_stats *stats) { }
#endif

void blkiocg_update_io_add_stats(struct blkio_group *blkg,
				 struct blkio_policy_type *pol,
				 struct blkio_group *curr_blkg, bool direction,
				 bool sync)
{
	struct blkio_group_stats *stats = &blkg->pd[pol->plid]->stats;
	int rw = (direction ? REQ_WRITE : 0) | (sync ? REQ_SYNC : 0);

	lockdep_assert_held(blkg->q->queue_lock);

	blkg_rwstat_add(&stats->queued, rw, 1);
	blkio_end_empty_time(stats);
	blkio_set_start_group_wait_time(blkg, pol, curr_blkg);
}
EXPORT_SYMBOL_GPL(blkiocg_update_io_add_stats);

void blkiocg_update_io_remove_stats(struct blkio_group *blkg,
				    struct blkio_policy_type *pol,
				    bool direction, bool sync)
{
	struct blkio_group_stats *stats = &blkg->pd[pol->plid]->stats;
	int rw = (direction ? REQ_WRITE : 0) | (sync ? REQ_SYNC : 0);

	lockdep_assert_held(blkg->q->queue_lock);

	blkg_rwstat_add(&stats->queued, rw, -1);
}
EXPORT_SYMBOL_GPL(blkiocg_update_io_remove_stats);

void blkiocg_update_timeslice_used(struct blkio_group *blkg,
				   struct blkio_policy_type *pol,
				   unsigned long time,
				   unsigned long unaccounted_time)
{
	struct blkio_group_stats *stats = &blkg->pd[pol->plid]->stats;

	lockdep_assert_held(blkg->q->queue_lock);

	blkg_stat_add(&stats->time, time);
#ifdef CONFIG_DEBUG_BLK_CGROUP
	blkg_stat_add(&stats->unaccounted_time, unaccounted_time);
#endif
}
EXPORT_SYMBOL_GPL(blkiocg_update_timeslice_used);

/*
 * should be called under rcu read lock or queue lock to make sure blkg pointer
 * is valid.
 */
void blkiocg_update_dispatch_stats(struct blkio_group *blkg,
				   struct blkio_policy_type *pol,
				   uint64_t bytes, bool direction, bool sync)
{
	int rw = (direction ? REQ_WRITE : 0) | (sync ? REQ_SYNC : 0);
	struct blkg_policy_data *pd = blkg->pd[pol->plid];
	struct blkio_group_stats_cpu *stats_cpu;
	unsigned long flags;

	/* If per cpu stats are not allocated yet, don't do any accounting. */
	if (pd->stats_cpu == NULL)
		return;

	/*
	 * Disabling interrupts to provide mutual exclusion between two
	 * writes on same cpu. It probably is not needed for 64bit. Not
	 * optimizing that case yet.
	 */
	local_irq_save(flags);

	stats_cpu = this_cpu_ptr(pd->stats_cpu);

	blkg_stat_add(&stats_cpu->sectors, bytes >> 9);
	blkg_rwstat_add(&stats_cpu->serviced, rw, 1);
	blkg_rwstat_add(&stats_cpu->service_bytes, rw, bytes);

	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_dispatch_stats);

void blkiocg_update_completion_stats(struct blkio_group *blkg,
				     struct blkio_policy_type *pol,
				     uint64_t start_time,
				     uint64_t io_start_time, bool direction,
				     bool sync)
{
	struct blkio_group_stats *stats = &blkg->pd[pol->plid]->stats;
	unsigned long long now = sched_clock();
	int rw = (direction ? REQ_WRITE : 0) | (sync ? REQ_SYNC : 0);

	lockdep_assert_held(blkg->q->queue_lock);

	if (time_after64(now, io_start_time))
		blkg_rwstat_add(&stats->service_time, rw, now - io_start_time);
	if (time_after64(io_start_time, start_time))
		blkg_rwstat_add(&stats->wait_time, rw,
				io_start_time - start_time);
}
EXPORT_SYMBOL_GPL(blkiocg_update_completion_stats);

/*  Merged stats are per cpu.  */
void blkiocg_update_io_merged_stats(struct blkio_group *blkg,
				    struct blkio_policy_type *pol,
				    bool direction, bool sync)
{
	struct blkio_group_stats *stats = &blkg->pd[pol->plid]->stats;
	int rw = (direction ? REQ_WRITE : 0) | (sync ? REQ_SYNC : 0);

	lockdep_assert_held(blkg->q->queue_lock);

	blkg_rwstat_add(&stats->merged, rw, 1);
}
EXPORT_SYMBOL_GPL(blkiocg_update_io_merged_stats);

/*
 * Worker for allocating per cpu stat for blk groups. This is scheduled on
 * the system_nrt_wq once there are some groups on the alloc_list waiting
 * for allocation.
 */
static void blkio_stat_alloc_fn(struct work_struct *work)
{
	static void *pcpu_stats[BLKIO_NR_POLICIES];
	struct delayed_work *dwork = to_delayed_work(work);
	struct blkio_group *blkg;
	int i;
	bool empty = false;

alloc_stats:
	for (i = 0; i < BLKIO_NR_POLICIES; i++) {
		if (pcpu_stats[i] != NULL)
			continue;

		pcpu_stats[i] = alloc_percpu(struct blkio_group_stats_cpu);

		/* Allocation failed. Try again after some time. */
		if (pcpu_stats[i] == NULL) {
			queue_delayed_work(system_nrt_wq, dwork,
						msecs_to_jiffies(10));
			return;
		}
	}

	spin_lock_irq(&blkio_list_lock);
	spin_lock(&alloc_list_lock);

	/* cgroup got deleted or queue exited. */
	if (!list_empty(&alloc_list)) {
		blkg = list_first_entry(&alloc_list, struct blkio_group,
						alloc_node);
		for (i = 0; i < BLKIO_NR_POLICIES; i++) {
			struct blkg_policy_data *pd = blkg->pd[i];

			if (blkio_policy[i] && pd && !pd->stats_cpu)
				swap(pd->stats_cpu, pcpu_stats[i]);
		}

		list_del_init(&blkg->alloc_node);
	}

	empty = list_empty(&alloc_list);

	spin_unlock(&alloc_list_lock);
	spin_unlock_irq(&blkio_list_lock);

	if (!empty)
		goto alloc_stats;
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

	for (i = 0; i < BLKIO_NR_POLICIES; i++) {
		struct blkg_policy_data *pd = blkg->pd[i];

		if (pd) {
			free_percpu(pd->stats_cpu);
			kfree(pd);
		}
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
	INIT_LIST_HEAD(&blkg->alloc_node);
	blkg->blkcg = blkcg;
	blkg->refcnt = 1;
	cgroup_path(blkcg->css.cgroup, blkg->path, sizeof(blkg->path));

	for (i = 0; i < BLKIO_NR_POLICIES; i++) {
		struct blkio_policy_type *pol = blkio_policy[i];
		struct blkg_policy_data *pd;

		if (!pol)
			continue;

		/* alloc per-policy data and attach it to blkg */
		pd = kzalloc_node(sizeof(*pd) + pol->pdata_size, GFP_ATOMIC,
				  q->node);
		if (!pd) {
			blkg_free(blkg);
			return NULL;
		}

		blkg->pd[i] = pd;
		pd->blkg = blkg;
	}

	/* invoke per-policy init */
	for (i = 0; i < BLKIO_NR_POLICIES; i++) {
		struct blkio_policy_type *pol = blkio_policy[i];

		if (pol)
			pol->ops.blkio_init_group_fn(blkg);
	}

	return blkg;
}

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
	 * The following can be removed if blkg lookup is guaranteed to
	 * fail on a bypassing queue.
	 */
	if (unlikely(blk_queue_bypass(q)) && !for_root)
		return ERR_PTR(blk_queue_dead(q) ? -EINVAL : -EBUSY);

	blkg = blkg_lookup(blkcg, q);
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

	spin_lock(&alloc_list_lock);
	list_add(&blkg->alloc_node, &alloc_list);
	/* Queue per cpu stat allocation from worker thread. */
	queue_delayed_work(system_nrt_wq, &blkio_stat_alloc_work, 0);
	spin_unlock(&alloc_list_lock);
out:
	return blkg;
}
EXPORT_SYMBOL_GPL(blkg_lookup_create);

/* called under rcu_read_lock(). */
struct blkio_group *blkg_lookup(struct blkio_cgroup *blkcg,
				struct request_queue *q)
{
	struct blkio_group *blkg;
	struct hlist_node *n;

	hlist_for_each_entry_rcu(blkg, n, &blkcg->blkg_list, blkcg_node)
		if (blkg->q == q)
			return blkg;
	return NULL;
}
EXPORT_SYMBOL_GPL(blkg_lookup);

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

	spin_lock(&alloc_list_lock);
	list_del_init(&blkg->alloc_node);
	spin_unlock(&alloc_list_lock);

	/*
	 * Put the reference taken at the time of creation so that when all
	 * queues are gone, group can be destroyed.
	 */
	blkg_put(blkg);
}

/*
 * XXX: This updates blkg policy data in-place for root blkg, which is
 * necessary across elevator switch and policy registration as root blkgs
 * aren't shot down.  This broken and racy implementation is temporary.
 * Eventually, blkg shoot down will be replaced by proper in-place update.
 */
void update_root_blkg_pd(struct request_queue *q, enum blkio_policy_id plid)
{
	struct blkio_policy_type *pol = blkio_policy[plid];
	struct blkio_group *blkg = blkg_lookup(&blkio_root_cgroup, q);
	struct blkg_policy_data *pd;

	if (!blkg)
		return;

	kfree(blkg->pd[plid]);
	blkg->pd[plid] = NULL;

	if (!pol)
		return;

	pd = kzalloc(sizeof(*pd) + pol->pdata_size, GFP_KERNEL);
	WARN_ON_ONCE(!pd);

	pd->stats_cpu = alloc_percpu(struct blkio_group_stats_cpu);
	WARN_ON_ONCE(!pd->stats_cpu);

	blkg->pd[plid] = pd;
	pd->blkg = blkg;
	pol->ops.blkio_init_group_fn(blkg);
}
EXPORT_SYMBOL_GPL(update_root_blkg_pd);

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

static void blkio_reset_stats_cpu(struct blkio_group *blkg, int plid)
{
	struct blkg_policy_data *pd = blkg->pd[plid];
	int cpu;

	if (pd->stats_cpu == NULL)
		return;

	for_each_possible_cpu(cpu) {
		struct blkio_group_stats_cpu *sc =
			per_cpu_ptr(pd->stats_cpu, cpu);

		blkg_rwstat_reset(&sc->service_bytes);
		blkg_rwstat_reset(&sc->serviced);
		blkg_stat_reset(&sc->sectors);
	}
}

static int
blkiocg_reset_stats(struct cgroup *cgroup, struct cftype *cftype, u64 val)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgroup);
	struct blkio_group *blkg;
	struct hlist_node *n;

	spin_lock(&blkio_list_lock);
	spin_lock_irq(&blkcg->lock);

	/*
	 * Note that stat reset is racy - it doesn't synchronize against
	 * stat updates.  This is a debug feature which shouldn't exist
	 * anyway.  If you get hit by a race, retry.
	 */
	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node) {
		struct blkio_policy_type *pol;

		list_for_each_entry(pol, &blkio_list, list) {
			struct blkg_policy_data *pd = blkg->pd[pol->plid];
			struct blkio_group_stats *stats = &pd->stats;

			/* queued stats shouldn't be cleared */
			blkg_rwstat_reset(&stats->merged);
			blkg_rwstat_reset(&stats->service_time);
			blkg_rwstat_reset(&stats->wait_time);
			blkg_stat_reset(&stats->time);
#ifdef CONFIG_DEBUG_BLK_CGROUP
			blkg_stat_reset(&stats->unaccounted_time);
			blkg_stat_reset(&stats->avg_queue_size_sum);
			blkg_stat_reset(&stats->avg_queue_size_samples);
			blkg_stat_reset(&stats->dequeue);
			blkg_stat_reset(&stats->group_wait_time);
			blkg_stat_reset(&stats->idle_time);
			blkg_stat_reset(&stats->empty_time);
#endif
			blkio_reset_stats_cpu(blkg, pol->plid);
		}
	}

	spin_unlock_irq(&blkcg->lock);
	spin_unlock(&blkio_list_lock);
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
static void blkcg_print_blkgs(struct seq_file *sf, struct blkio_cgroup *blkcg,
			      u64 (*prfill)(struct seq_file *,
					    struct blkg_policy_data *, int),
			      int pol, int data, bool show_total)
{
	struct blkio_group *blkg;
	struct hlist_node *n;
	u64 total = 0;

	spin_lock_irq(&blkcg->lock);
	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node)
		if (blkg->pd[pol])
			total += prfill(sf, blkg->pd[pol], data);
	spin_unlock_irq(&blkcg->lock);

	if (show_total)
		seq_printf(sf, "Total %llu\n", (unsigned long long)total);
}

/**
 * __blkg_prfill_u64 - prfill helper for a single u64 value
 * @sf: seq_file to print to
 * @pd: policy data of interest
 * @v: value to print
 *
 * Print @v to @sf for the device assocaited with @pd.
 */
static u64 __blkg_prfill_u64(struct seq_file *sf, struct blkg_policy_data *pd,
			     u64 v)
{
	const char *dname = blkg_dev_name(pd->blkg);

	if (!dname)
		return 0;

	seq_printf(sf, "%s %llu\n", dname, (unsigned long long)v);
	return v;
}

/**
 * __blkg_prfill_rwstat - prfill helper for a blkg_rwstat
 * @sf: seq_file to print to
 * @pd: policy data of interest
 * @rwstat: rwstat to print
 *
 * Print @rwstat to @sf for the device assocaited with @pd.
 */
static u64 __blkg_prfill_rwstat(struct seq_file *sf,
				struct blkg_policy_data *pd,
				const struct blkg_rwstat *rwstat)
{
	static const char *rwstr[] = {
		[BLKG_RWSTAT_READ]	= "Read",
		[BLKG_RWSTAT_WRITE]	= "Write",
		[BLKG_RWSTAT_SYNC]	= "Sync",
		[BLKG_RWSTAT_ASYNC]	= "Async",
	};
	const char *dname = blkg_dev_name(pd->blkg);
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

static u64 blkg_prfill_stat(struct seq_file *sf, struct blkg_policy_data *pd,
			    int off)
{
	return __blkg_prfill_u64(sf, pd,
				 blkg_stat_read((void *)&pd->stats + off));
}

static u64 blkg_prfill_rwstat(struct seq_file *sf, struct blkg_policy_data *pd,
			      int off)
{
	struct blkg_rwstat rwstat = blkg_rwstat_read((void *)&pd->stats + off);

	return __blkg_prfill_rwstat(sf, pd, &rwstat);
}

/* print blkg_stat specified by BLKCG_STAT_PRIV() */
static int blkcg_print_stat(struct cgroup *cgrp, struct cftype *cft,
			    struct seq_file *sf)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgrp);

	blkcg_print_blkgs(sf, blkcg, blkg_prfill_stat,
			  BLKCG_STAT_POL(cft->private),
			  BLKCG_STAT_OFF(cft->private), false);
	return 0;
}

/* print blkg_rwstat specified by BLKCG_STAT_PRIV() */
static int blkcg_print_rwstat(struct cgroup *cgrp, struct cftype *cft,
			      struct seq_file *sf)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgrp);

	blkcg_print_blkgs(sf, blkcg, blkg_prfill_rwstat,
			  BLKCG_STAT_POL(cft->private),
			  BLKCG_STAT_OFF(cft->private), true);
	return 0;
}

static u64 blkg_prfill_cpu_stat(struct seq_file *sf,
				struct blkg_policy_data *pd, int off)
{
	u64 v = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		struct blkio_group_stats_cpu *sc =
			per_cpu_ptr(pd->stats_cpu, cpu);

		v += blkg_stat_read((void *)sc + off);
	}

	return __blkg_prfill_u64(sf, pd, v);
}

static u64 blkg_prfill_cpu_rwstat(struct seq_file *sf,
				  struct blkg_policy_data *pd, int off)
{
	struct blkg_rwstat rwstat = { }, tmp;
	int i, cpu;

	for_each_possible_cpu(cpu) {
		struct blkio_group_stats_cpu *sc =
			per_cpu_ptr(pd->stats_cpu, cpu);

		tmp = blkg_rwstat_read((void *)sc + off);
		for (i = 0; i < BLKG_RWSTAT_NR; i++)
			rwstat.cnt[i] += tmp.cnt[i];
	}

	return __blkg_prfill_rwstat(sf, pd, &rwstat);
}

/* print per-cpu blkg_stat specified by BLKCG_STAT_PRIV() */
static int blkcg_print_cpu_stat(struct cgroup *cgrp, struct cftype *cft,
				struct seq_file *sf)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgrp);

	blkcg_print_blkgs(sf, blkcg, blkg_prfill_cpu_stat,
			  BLKCG_STAT_POL(cft->private),
			  BLKCG_STAT_OFF(cft->private), false);
	return 0;
}

/* print per-cpu blkg_rwstat specified by BLKCG_STAT_PRIV() */
static int blkcg_print_cpu_rwstat(struct cgroup *cgrp, struct cftype *cft,
				  struct seq_file *sf)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgrp);

	blkcg_print_blkgs(sf, blkcg, blkg_prfill_cpu_rwstat,
			  BLKCG_STAT_POL(cft->private),
			  BLKCG_STAT_OFF(cft->private), true);
	return 0;
}

#ifdef CONFIG_DEBUG_BLK_CGROUP
static u64 blkg_prfill_avg_queue_size(struct seq_file *sf,
				      struct blkg_policy_data *pd, int off)
{
	u64 samples = blkg_stat_read(&pd->stats.avg_queue_size_samples);
	u64 v = 0;

	if (samples) {
		v = blkg_stat_read(&pd->stats.avg_queue_size_sum);
		do_div(v, samples);
	}
	__blkg_prfill_u64(sf, pd, v);
	return 0;
}

/* print avg_queue_size */
static int blkcg_print_avg_queue_size(struct cgroup *cgrp, struct cftype *cft,
				      struct seq_file *sf)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgrp);

	blkcg_print_blkgs(sf, blkcg, blkg_prfill_avg_queue_size,
			  BLKIO_POLICY_PROP, 0, false);
	return 0;
}
#endif	/* CONFIG_DEBUG_BLK_CGROUP */

struct blkg_conf_ctx {
	struct gendisk		*disk;
	struct blkio_group	*blkg;
	u64			v;
};

/**
 * blkg_conf_prep - parse and prepare for per-blkg config update
 * @blkcg: target block cgroup
 * @input: input string
 * @ctx: blkg_conf_ctx to be filled
 *
 * Parse per-blkg config update from @input and initialize @ctx with the
 * result.  @ctx->blkg points to the blkg to be updated and @ctx->v the new
 * value.  This function returns with RCU read locked and must be paired
 * with blkg_conf_finish().
 */
static int blkg_conf_prep(struct blkio_cgroup *blkcg, const char *input,
			  struct blkg_conf_ctx *ctx)
	__acquires(rcu)
{
	struct gendisk *disk;
	struct blkio_group *blkg;
	char *buf, *s[4], *p, *major_s, *minor_s;
	unsigned long major, minor;
	int i = 0, ret = -EINVAL;
	int part;
	dev_t dev;
	u64 temp;

	buf = kstrdup(input, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memset(s, 0, sizeof(s));

	while ((p = strsep(&buf, " ")) != NULL) {
		if (!*p)
			continue;

		s[i++] = p;

		/* Prevent from inputing too many things */
		if (i == 3)
			break;
	}

	if (i != 2)
		goto out;

	p = strsep(&s[0], ":");
	if (p != NULL)
		major_s = p;
	else
		goto out;

	minor_s = s[0];
	if (!minor_s)
		goto out;

	if (strict_strtoul(major_s, 10, &major))
		goto out;

	if (strict_strtoul(minor_s, 10, &minor))
		goto out;

	dev = MKDEV(major, minor);

	if (strict_strtoull(s[1], 10, &temp))
		goto out;

	disk = get_gendisk(dev, &part);
	if (!disk || part)
		goto out;

	rcu_read_lock();

	spin_lock_irq(disk->queue->queue_lock);
	blkg = blkg_lookup_create(blkcg, disk->queue, false);
	spin_unlock_irq(disk->queue->queue_lock);

	if (IS_ERR(blkg)) {
		ret = PTR_ERR(blkg);
		rcu_read_unlock();
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
		goto out;
	}

	ctx->disk = disk;
	ctx->blkg = blkg;
	ctx->v = temp;
	ret = 0;
out:
	kfree(buf);
	return ret;
}

/**
 * blkg_conf_finish - finish up per-blkg config update
 * @ctx: blkg_conf_ctx intiailized by blkg_conf_prep()
 *
 * Finish up after per-blkg config update.  This function must be paired
 * with blkg_conf_prep().
 */
static void blkg_conf_finish(struct blkg_conf_ctx *ctx)
	__releases(rcu)
{
	rcu_read_unlock();
	put_disk(ctx->disk);
}

/* for propio conf */
static u64 blkg_prfill_weight_device(struct seq_file *sf,
				     struct blkg_policy_data *pd, int off)
{
	if (!pd->conf.weight)
		return 0;
	return __blkg_prfill_u64(sf, pd, pd->conf.weight);
}

static int blkcg_print_weight_device(struct cgroup *cgrp, struct cftype *cft,
				     struct seq_file *sf)
{
	blkcg_print_blkgs(sf, cgroup_to_blkio_cgroup(cgrp),
			  blkg_prfill_weight_device, BLKIO_POLICY_PROP, 0,
			  false);
	return 0;
}

static int blkcg_print_weight(struct cgroup *cgrp, struct cftype *cft,
			      struct seq_file *sf)
{
	seq_printf(sf, "%u\n", cgroup_to_blkio_cgroup(cgrp)->weight);
	return 0;
}

static int blkcg_set_weight_device(struct cgroup *cgrp, struct cftype *cft,
				   const char *buf)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgrp);
	struct blkg_policy_data *pd;
	struct blkg_conf_ctx ctx;
	int ret;

	ret = blkg_conf_prep(blkcg, buf, &ctx);
	if (ret)
		return ret;

	ret = -EINVAL;
	pd = ctx.blkg->pd[BLKIO_POLICY_PROP];
	if (pd && (!ctx.v || (ctx.v >= BLKIO_WEIGHT_MIN &&
			      ctx.v <= BLKIO_WEIGHT_MAX))) {
		pd->conf.weight = ctx.v;
		blkio_update_group_weight(ctx.blkg, BLKIO_POLICY_PROP,
					  ctx.v ?: blkcg->weight);
		ret = 0;
	}

	blkg_conf_finish(&ctx);
	return ret;
}

static int blkcg_set_weight(struct cgroup *cgrp, struct cftype *cft, u64 val)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgrp);
	struct blkio_group *blkg;
	struct hlist_node *n;

	if (val < BLKIO_WEIGHT_MIN || val > BLKIO_WEIGHT_MAX)
		return -EINVAL;

	spin_lock(&blkio_list_lock);
	spin_lock_irq(&blkcg->lock);
	blkcg->weight = (unsigned int)val;

	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node) {
		struct blkg_policy_data *pd = blkg->pd[BLKIO_POLICY_PROP];

		if (pd && !pd->conf.weight)
			blkio_update_group_weight(blkg, BLKIO_POLICY_PROP,
						  blkcg->weight);
	}

	spin_unlock_irq(&blkcg->lock);
	spin_unlock(&blkio_list_lock);
	return 0;
}

/* for blk-throttle conf */
#ifdef CONFIG_BLK_DEV_THROTTLING
static u64 blkg_prfill_conf_u64(struct seq_file *sf,
				struct blkg_policy_data *pd, int off)
{
	u64 v = *(u64 *)((void *)&pd->conf + off);

	if (!v)
		return 0;
	return __blkg_prfill_u64(sf, pd, v);
}

static int blkcg_print_conf_u64(struct cgroup *cgrp, struct cftype *cft,
				struct seq_file *sf)
{
	blkcg_print_blkgs(sf, cgroup_to_blkio_cgroup(cgrp),
			  blkg_prfill_conf_u64, BLKIO_POLICY_THROTL,
			  cft->private, false);
	return 0;
}

static int blkcg_set_conf_u64(struct cgroup *cgrp, struct cftype *cft,
			      const char *buf, int rw,
			      void (*update)(struct blkio_group *, int, u64, int))
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgrp);
	struct blkg_policy_data *pd;
	struct blkg_conf_ctx ctx;
	int ret;

	ret = blkg_conf_prep(blkcg, buf, &ctx);
	if (ret)
		return ret;

	ret = -EINVAL;
	pd = ctx.blkg->pd[BLKIO_POLICY_THROTL];
	if (pd) {
		*(u64 *)((void *)&pd->conf + cft->private) = ctx.v;
		update(ctx.blkg, BLKIO_POLICY_THROTL, ctx.v ?: -1, rw);
		ret = 0;
	}

	blkg_conf_finish(&ctx);
	return ret;
}

static int blkcg_set_conf_bps_r(struct cgroup *cgrp, struct cftype *cft,
				const char *buf)
{
	return blkcg_set_conf_u64(cgrp, cft, buf, READ, blkio_update_group_bps);
}

static int blkcg_set_conf_bps_w(struct cgroup *cgrp, struct cftype *cft,
				const char *buf)
{
	return blkcg_set_conf_u64(cgrp, cft, buf, WRITE, blkio_update_group_bps);
}

static int blkcg_set_conf_iops_r(struct cgroup *cgrp, struct cftype *cft,
				 const char *buf)
{
	return blkcg_set_conf_u64(cgrp, cft, buf, READ, blkio_update_group_iops);
}

static int blkcg_set_conf_iops_w(struct cgroup *cgrp, struct cftype *cft,
				 const char *buf)
{
	return blkcg_set_conf_u64(cgrp, cft, buf, WRITE, blkio_update_group_iops);
}
#endif

struct cftype blkio_files[] = {
	{
		.name = "weight_device",
		.read_seq_string = blkcg_print_weight_device,
		.write_string = blkcg_set_weight_device,
		.max_write_len = 256,
	},
	{
		.name = "weight",
		.read_seq_string = blkcg_print_weight,
		.write_u64 = blkcg_set_weight,
	},
	{
		.name = "time",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_PROP,
				offsetof(struct blkio_group_stats, time)),
		.read_seq_string = blkcg_print_stat,
	},
	{
		.name = "sectors",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_PROP,
				offsetof(struct blkio_group_stats_cpu, sectors)),
		.read_seq_string = blkcg_print_cpu_stat,
	},
	{
		.name = "io_service_bytes",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_PROP,
				offsetof(struct blkio_group_stats_cpu, service_bytes)),
		.read_seq_string = blkcg_print_cpu_rwstat,
	},
	{
		.name = "io_serviced",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_PROP,
				offsetof(struct blkio_group_stats_cpu, serviced)),
		.read_seq_string = blkcg_print_cpu_rwstat,
	},
	{
		.name = "io_service_time",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_PROP,
				offsetof(struct blkio_group_stats, service_time)),
		.read_seq_string = blkcg_print_rwstat,
	},
	{
		.name = "io_wait_time",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_PROP,
				offsetof(struct blkio_group_stats, wait_time)),
		.read_seq_string = blkcg_print_rwstat,
	},
	{
		.name = "io_merged",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_PROP,
				offsetof(struct blkio_group_stats, merged)),
		.read_seq_string = blkcg_print_rwstat,
	},
	{
		.name = "io_queued",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_PROP,
				offsetof(struct blkio_group_stats, queued)),
		.read_seq_string = blkcg_print_rwstat,
	},
	{
		.name = "reset_stats",
		.write_u64 = blkiocg_reset_stats,
	},
#ifdef CONFIG_BLK_DEV_THROTTLING
	{
		.name = "throttle.read_bps_device",
		.private = offsetof(struct blkio_group_conf, bps[READ]),
		.read_seq_string = blkcg_print_conf_u64,
		.write_string = blkcg_set_conf_bps_r,
		.max_write_len = 256,
	},

	{
		.name = "throttle.write_bps_device",
		.private = offsetof(struct blkio_group_conf, bps[WRITE]),
		.read_seq_string = blkcg_print_conf_u64,
		.write_string = blkcg_set_conf_bps_w,
		.max_write_len = 256,
	},

	{
		.name = "throttle.read_iops_device",
		.private = offsetof(struct blkio_group_conf, iops[READ]),
		.read_seq_string = blkcg_print_conf_u64,
		.write_string = blkcg_set_conf_iops_r,
		.max_write_len = 256,
	},

	{
		.name = "throttle.write_iops_device",
		.private = offsetof(struct blkio_group_conf, iops[WRITE]),
		.read_seq_string = blkcg_print_conf_u64,
		.write_string = blkcg_set_conf_iops_w,
		.max_write_len = 256,
	},
	{
		.name = "throttle.io_service_bytes",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_THROTL,
				offsetof(struct blkio_group_stats_cpu, service_bytes)),
		.read_seq_string = blkcg_print_cpu_rwstat,
	},
	{
		.name = "throttle.io_serviced",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_THROTL,
				offsetof(struct blkio_group_stats_cpu, serviced)),
		.read_seq_string = blkcg_print_cpu_rwstat,
	},
#endif /* CONFIG_BLK_DEV_THROTTLING */

#ifdef CONFIG_DEBUG_BLK_CGROUP
	{
		.name = "avg_queue_size",
		.read_seq_string = blkcg_print_avg_queue_size,
	},
	{
		.name = "group_wait_time",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_PROP,
				offsetof(struct blkio_group_stats, group_wait_time)),
		.read_seq_string = blkcg_print_stat,
	},
	{
		.name = "idle_time",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_PROP,
				offsetof(struct blkio_group_stats, idle_time)),
		.read_seq_string = blkcg_print_stat,
	},
	{
		.name = "empty_time",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_PROP,
				offsetof(struct blkio_group_stats, empty_time)),
		.read_seq_string = blkcg_print_stat,
	},
	{
		.name = "dequeue",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_PROP,
				offsetof(struct blkio_group_stats, dequeue)),
		.read_seq_string = blkcg_print_stat,
	},
	{
		.name = "unaccounted_time",
		.private = BLKCG_STAT_PRIV(BLKIO_POLICY_PROP,
				offsetof(struct blkio_group_stats, unaccounted_time)),
		.read_seq_string = blkcg_print_stat,
	},
#endif
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

	blkcg->weight = BLKIO_WEIGHT_DEFAULT;
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

static void blkcg_bypass_start(void)
	__acquires(&all_q_mutex)
{
	struct request_queue *q;

	mutex_lock(&all_q_mutex);

	list_for_each_entry(q, &all_q_list, all_q_node) {
		blk_queue_bypass_start(q);
		blkg_destroy_all(q, false);
	}
}

static void blkcg_bypass_end(void)
	__releases(&all_q_mutex)
{
	struct request_queue *q;

	list_for_each_entry(q, &all_q_list, all_q_node)
		blk_queue_bypass_end(q);

	mutex_unlock(&all_q_mutex);
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

void blkio_policy_register(struct blkio_policy_type *blkiop)
{
	struct request_queue *q;

	blkcg_bypass_start();
	spin_lock(&blkio_list_lock);

	BUG_ON(blkio_policy[blkiop->plid]);
	blkio_policy[blkiop->plid] = blkiop;
	list_add_tail(&blkiop->list, &blkio_list);

	spin_unlock(&blkio_list_lock);
	list_for_each_entry(q, &all_q_list, all_q_node)
		update_root_blkg_pd(q, blkiop->plid);
	blkcg_bypass_end();
}
EXPORT_SYMBOL_GPL(blkio_policy_register);

void blkio_policy_unregister(struct blkio_policy_type *blkiop)
{
	struct request_queue *q;

	blkcg_bypass_start();
	spin_lock(&blkio_list_lock);

	BUG_ON(blkio_policy[blkiop->plid] != blkiop);
	blkio_policy[blkiop->plid] = NULL;
	list_del_init(&blkiop->list);

	spin_unlock(&blkio_list_lock);
	list_for_each_entry(q, &all_q_list, all_q_node)
		update_root_blkg_pd(q, blkiop->plid);
	blkcg_bypass_end();
}
EXPORT_SYMBOL_GPL(blkio_policy_unregister);
