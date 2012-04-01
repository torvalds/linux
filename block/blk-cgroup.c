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

/* for encoding cft->private value on file */
#define BLKIOFILE_PRIVATE(x, val)	(((x) << 16) | (val))
/* What policy owns the file, proportional or throttle */
#define BLKIOFILE_POLICY(val)		(((val) >> 16) & 0xffff)
#define BLKIOFILE_ATTR(val)		((val) & 0xffff)

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
					  u64 bps, int fileid)
{
	struct blkio_policy_type *blkiop;

	list_for_each_entry(blkiop, &blkio_list, list) {

		/* If this policy does not own the blkg, do not send updates */
		if (blkiop->plid != plid)
			continue;

		if (fileid == BLKIO_THROTL_read_bps_device
		    && blkiop->ops.blkio_update_group_read_bps_fn)
			blkiop->ops.blkio_update_group_read_bps_fn(blkg->q,
								blkg, bps);

		if (fileid == BLKIO_THROTL_write_bps_device
		    && blkiop->ops.blkio_update_group_write_bps_fn)
			blkiop->ops.blkio_update_group_write_bps_fn(blkg->q,
								blkg, bps);
	}
}

static inline void blkio_update_group_iops(struct blkio_group *blkg,
					   int plid, unsigned int iops,
					   int fileid)
{
	struct blkio_policy_type *blkiop;

	list_for_each_entry(blkiop, &blkio_list, list) {

		/* If this policy does not own the blkg, do not send updates */
		if (blkiop->plid != plid)
			continue;

		if (fileid == BLKIO_THROTL_read_iops_device
		    && blkiop->ops.blkio_update_group_read_iops_fn)
			blkiop->ops.blkio_update_group_read_iops_fn(blkg->q,
								blkg, iops);

		if (fileid == BLKIO_THROTL_write_iops_device
		    && blkiop->ops.blkio_update_group_write_iops_fn)
			blkiop->ops.blkio_update_group_write_iops_fn(blkg->q,
								blkg,iops);
	}
}

/*
 * Add to the appropriate stat variable depending on the request type.
 * This should be called with queue_lock held.
 */
static void blkio_add_stat(uint64_t *stat, uint64_t add, bool direction,
				bool sync)
{
	if (direction)
		stat[BLKIO_STAT_WRITE] += add;
	else
		stat[BLKIO_STAT_READ] += add;
	if (sync)
		stat[BLKIO_STAT_SYNC] += add;
	else
		stat[BLKIO_STAT_ASYNC] += add;
}

/*
 * Decrements the appropriate stat variable if non-zero depending on the
 * request type. Panics on value being zero.
 * This should be called with the queue_lock held.
 */
static void blkio_check_and_dec_stat(uint64_t *stat, bool direction, bool sync)
{
	if (direction) {
		BUG_ON(stat[BLKIO_STAT_WRITE] == 0);
		stat[BLKIO_STAT_WRITE]--;
	} else {
		BUG_ON(stat[BLKIO_STAT_READ] == 0);
		stat[BLKIO_STAT_READ]--;
	}
	if (sync) {
		BUG_ON(stat[BLKIO_STAT_SYNC] == 0);
		stat[BLKIO_STAT_SYNC]--;
	} else {
		BUG_ON(stat[BLKIO_STAT_ASYNC] == 0);
		stat[BLKIO_STAT_ASYNC]--;
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
		stats->group_wait_time += now - stats->start_group_wait_time;
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
		stats->empty_time += now - stats->start_empty_time;
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

		if (time_after64(now, stats->start_idle_time)) {
			u64_stats_update_begin(&stats->syncp);
			stats->idle_time += now - stats->start_idle_time;
			u64_stats_update_end(&stats->syncp);
		}
		blkio_clear_blkg_idling(stats);
	}
}
EXPORT_SYMBOL_GPL(blkiocg_update_idle_time_stats);

void blkiocg_update_avg_queue_size_stats(struct blkio_group *blkg,
					 struct blkio_policy_type *pol)
{
	struct blkio_group_stats *stats = &blkg->pd[pol->plid]->stats;

	lockdep_assert_held(blkg->q->queue_lock);

	u64_stats_update_begin(&stats->syncp);
	stats->avg_queue_size_sum +=
			stats->stat_arr[BLKIO_STAT_QUEUED][BLKIO_STAT_READ] +
			stats->stat_arr[BLKIO_STAT_QUEUED][BLKIO_STAT_WRITE];
	stats->avg_queue_size_samples++;
	blkio_update_group_wait_time(stats);
	u64_stats_update_end(&stats->syncp);
}
EXPORT_SYMBOL_GPL(blkiocg_update_avg_queue_size_stats);

void blkiocg_set_start_empty_time(struct blkio_group *blkg,
				  struct blkio_policy_type *pol)
{
	struct blkio_group_stats *stats = &blkg->pd[pol->plid]->stats;

	lockdep_assert_held(blkg->q->queue_lock);

	if (stats->stat_arr[BLKIO_STAT_QUEUED][BLKIO_STAT_READ] ||
			stats->stat_arr[BLKIO_STAT_QUEUED][BLKIO_STAT_WRITE])
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

	pd->stats.dequeue += dequeue;
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

	lockdep_assert_held(blkg->q->queue_lock);

	u64_stats_update_begin(&stats->syncp);
	blkio_add_stat(stats->stat_arr[BLKIO_STAT_QUEUED], 1, direction, sync);
	blkio_end_empty_time(stats);
	u64_stats_update_end(&stats->syncp);

	blkio_set_start_group_wait_time(blkg, pol, curr_blkg);
}
EXPORT_SYMBOL_GPL(blkiocg_update_io_add_stats);

void blkiocg_update_io_remove_stats(struct blkio_group *blkg,
				    struct blkio_policy_type *pol,
				    bool direction, bool sync)
{
	struct blkio_group_stats *stats = &blkg->pd[pol->plid]->stats;

	lockdep_assert_held(blkg->q->queue_lock);

	u64_stats_update_begin(&stats->syncp);
	blkio_check_and_dec_stat(stats->stat_arr[BLKIO_STAT_QUEUED], direction,
				 sync);
	u64_stats_update_end(&stats->syncp);
}
EXPORT_SYMBOL_GPL(blkiocg_update_io_remove_stats);

void blkiocg_update_timeslice_used(struct blkio_group *blkg,
				   struct blkio_policy_type *pol,
				   unsigned long time,
				   unsigned long unaccounted_time)
{
	struct blkio_group_stats *stats = &blkg->pd[pol->plid]->stats;

	lockdep_assert_held(blkg->q->queue_lock);

	u64_stats_update_begin(&stats->syncp);
	stats->time += time;
#ifdef CONFIG_DEBUG_BLK_CGROUP
	stats->unaccounted_time += unaccounted_time;
#endif
	u64_stats_update_end(&stats->syncp);
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

	u64_stats_update_begin(&stats_cpu->syncp);
	stats_cpu->sectors += bytes >> 9;
	blkio_add_stat(stats_cpu->stat_arr_cpu[BLKIO_STAT_CPU_SERVICED],
			1, direction, sync);
	blkio_add_stat(stats_cpu->stat_arr_cpu[BLKIO_STAT_CPU_SERVICE_BYTES],
			bytes, direction, sync);
	u64_stats_update_end(&stats_cpu->syncp);
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

	lockdep_assert_held(blkg->q->queue_lock);

	u64_stats_update_begin(&stats->syncp);
	if (time_after64(now, io_start_time))
		blkio_add_stat(stats->stat_arr[BLKIO_STAT_SERVICE_TIME],
				now - io_start_time, direction, sync);
	if (time_after64(io_start_time, start_time))
		blkio_add_stat(stats->stat_arr[BLKIO_STAT_WAIT_TIME],
				io_start_time - start_time, direction, sync);
	u64_stats_update_end(&stats->syncp);
}
EXPORT_SYMBOL_GPL(blkiocg_update_completion_stats);

/*  Merged stats are per cpu.  */
void blkiocg_update_io_merged_stats(struct blkio_group *blkg,
				    struct blkio_policy_type *pol,
				    bool direction, bool sync)
{
	struct blkio_group_stats *stats = &blkg->pd[pol->plid]->stats;

	lockdep_assert_held(blkg->q->queue_lock);

	u64_stats_update_begin(&stats->syncp);
	blkio_add_stat(stats->stat_arr[BLKIO_STAT_MERGED], 1, direction, sync);
	u64_stats_update_end(&stats->syncp);
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

		sc->sectors = 0;
		memset(sc->stat_arr_cpu, 0, sizeof(sc->stat_arr_cpu));
	}
}

static int
blkiocg_reset_stats(struct cgroup *cgroup, struct cftype *cftype, u64 val)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgroup);
	struct blkio_group *blkg;
	struct hlist_node *n;
	int i;

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
			for (i = 0; i < ARRAY_SIZE(stats->stat_arr); i++)
				if (i != BLKIO_STAT_QUEUED)
					memset(stats->stat_arr[i], 0,
					       sizeof(stats->stat_arr[i]));
			stats->time = 0;
#ifdef CONFIG_DEBUG_BLK_CGROUP
			memset((void *)stats + BLKG_STATS_DEBUG_CLEAR_START, 0,
			       BLKG_STATS_DEBUG_CLEAR_SIZE);
#endif
			blkio_reset_stats_cpu(blkg, pol->plid);
		}
	}

	spin_unlock_irq(&blkcg->lock);
	spin_unlock(&blkio_list_lock);
	return 0;
}

static void blkio_get_key_name(enum stat_sub_type type, const char *dname,
			       char *str, int chars_left, bool diskname_only)
{
	snprintf(str, chars_left, "%s", dname);
	chars_left -= strlen(str);
	if (chars_left <= 0) {
		printk(KERN_WARNING
			"Possibly incorrect cgroup stat display format");
		return;
	}
	if (diskname_only)
		return;
	switch (type) {
	case BLKIO_STAT_READ:
		strlcat(str, " Read", chars_left);
		break;
	case BLKIO_STAT_WRITE:
		strlcat(str, " Write", chars_left);
		break;
	case BLKIO_STAT_SYNC:
		strlcat(str, " Sync", chars_left);
		break;
	case BLKIO_STAT_ASYNC:
		strlcat(str, " Async", chars_left);
		break;
	case BLKIO_STAT_TOTAL:
		strlcat(str, " Total", chars_left);
		break;
	default:
		strlcat(str, " Invalid", chars_left);
	}
}

static uint64_t blkio_read_stat_cpu(struct blkio_group *blkg, int plid,
			enum stat_type_cpu type, enum stat_sub_type sub_type)
{
	struct blkg_policy_data *pd = blkg->pd[plid];
	int cpu;
	struct blkio_group_stats_cpu *stats_cpu;
	u64 val = 0, tval;

	if (pd->stats_cpu == NULL)
		return val;

	for_each_possible_cpu(cpu) {
		unsigned int start;
		stats_cpu = per_cpu_ptr(pd->stats_cpu, cpu);

		do {
			start = u64_stats_fetch_begin(&stats_cpu->syncp);
			if (type == BLKIO_STAT_CPU_SECTORS)
				tval = stats_cpu->sectors;
			else
				tval = stats_cpu->stat_arr_cpu[type][sub_type];
		} while(u64_stats_fetch_retry(&stats_cpu->syncp, start));

		val += tval;
	}

	return val;
}

static uint64_t blkio_get_stat_cpu(struct blkio_group *blkg, int plid,
				   struct cgroup_map_cb *cb, const char *dname,
				   enum stat_type_cpu type)
{
	uint64_t disk_total, val;
	char key_str[MAX_KEY_LEN];
	enum stat_sub_type sub_type;

	if (type == BLKIO_STAT_CPU_SECTORS) {
		val = blkio_read_stat_cpu(blkg, plid, type, 0);
		blkio_get_key_name(0, dname, key_str, MAX_KEY_LEN, true);
		cb->fill(cb, key_str, val);
		return val;
	}

	for (sub_type = BLKIO_STAT_READ; sub_type < BLKIO_STAT_TOTAL;
			sub_type++) {
		blkio_get_key_name(sub_type, dname, key_str, MAX_KEY_LEN,
				   false);
		val = blkio_read_stat_cpu(blkg, plid, type, sub_type);
		cb->fill(cb, key_str, val);
	}

	disk_total = blkio_read_stat_cpu(blkg, plid, type, BLKIO_STAT_READ) +
		blkio_read_stat_cpu(blkg, plid, type, BLKIO_STAT_WRITE);

	blkio_get_key_name(BLKIO_STAT_TOTAL, dname, key_str, MAX_KEY_LEN,
			   false);
	cb->fill(cb, key_str, disk_total);
	return disk_total;
}

static uint64_t blkio_get_stat(struct blkio_group *blkg, int plid,
			       struct cgroup_map_cb *cb, const char *dname,
			       enum stat_type type)
{
	struct blkio_group_stats *stats = &blkg->pd[plid]->stats;
	uint64_t v = 0, disk_total = 0;
	char key_str[MAX_KEY_LEN];
	unsigned int sync_start;
	int st;

	if (type >= BLKIO_STAT_ARR_NR) {
		do {
			sync_start = u64_stats_fetch_begin(&stats->syncp);
			switch (type) {
			case BLKIO_STAT_TIME:
				v = stats->time;
				break;
#ifdef CONFIG_DEBUG_BLK_CGROUP
			case BLKIO_STAT_UNACCOUNTED_TIME:
				v = stats->unaccounted_time;
				break;
			case BLKIO_STAT_AVG_QUEUE_SIZE: {
				uint64_t samples = stats->avg_queue_size_samples;

				if (samples) {
					v = stats->avg_queue_size_sum;
					do_div(v, samples);
				}
				break;
			}
			case BLKIO_STAT_IDLE_TIME:
				v = stats->idle_time;
				break;
			case BLKIO_STAT_EMPTY_TIME:
				v = stats->empty_time;
				break;
			case BLKIO_STAT_DEQUEUE:
				v = stats->dequeue;
				break;
			case BLKIO_STAT_GROUP_WAIT_TIME:
				v = stats->group_wait_time;
				break;
#endif
			default:
				WARN_ON_ONCE(1);
			}
		} while (u64_stats_fetch_retry(&stats->syncp, sync_start));

		blkio_get_key_name(0, dname, key_str, MAX_KEY_LEN, true);
		cb->fill(cb, key_str, v);
		return v;
	}

	for (st = BLKIO_STAT_READ; st < BLKIO_STAT_TOTAL; st++) {
		do {
			sync_start = u64_stats_fetch_begin(&stats->syncp);
			v = stats->stat_arr[type][st];
		} while (u64_stats_fetch_retry(&stats->syncp, sync_start));

		blkio_get_key_name(st, dname, key_str, MAX_KEY_LEN, false);
		cb->fill(cb, key_str, v);
		if (st == BLKIO_STAT_READ || st == BLKIO_STAT_WRITE)
			disk_total += v;
	}

	blkio_get_key_name(BLKIO_STAT_TOTAL, dname, key_str, MAX_KEY_LEN,
			   false);
	cb->fill(cb, key_str, disk_total);
	return disk_total;
}

static int blkio_policy_parse_and_set(char *buf, enum blkio_policy_id plid,
				      int fileid, struct blkio_cgroup *blkcg)
{
	struct gendisk *disk = NULL;
	struct blkio_group *blkg = NULL;
	struct blkg_policy_data *pd;
	char *s[4], *p, *major_s = NULL, *minor_s = NULL;
	unsigned long major, minor;
	int i = 0, ret = -EINVAL;
	int part;
	dev_t dev;
	u64 temp;

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
		goto out_unlock;
	}

	pd = blkg->pd[plid];

	switch (plid) {
	case BLKIO_POLICY_PROP:
		if ((temp < BLKIO_WEIGHT_MIN && temp > 0) ||
		     temp > BLKIO_WEIGHT_MAX)
			goto out_unlock;

		pd->conf.weight = temp;
		blkio_update_group_weight(blkg, plid, temp ?: blkcg->weight);
		break;
	case BLKIO_POLICY_THROTL:
		switch(fileid) {
		case BLKIO_THROTL_read_bps_device:
			pd->conf.bps[READ] = temp;
			blkio_update_group_bps(blkg, plid, temp ?: -1, fileid);
			break;
		case BLKIO_THROTL_write_bps_device:
			pd->conf.bps[WRITE] = temp;
			blkio_update_group_bps(blkg, plid, temp ?: -1, fileid);
			break;
		case BLKIO_THROTL_read_iops_device:
			if (temp > THROTL_IOPS_MAX)
				goto out_unlock;
			pd->conf.iops[READ] = temp;
			blkio_update_group_iops(blkg, plid, temp ?: -1, fileid);
			break;
		case BLKIO_THROTL_write_iops_device:
			if (temp > THROTL_IOPS_MAX)
				goto out_unlock;
			pd->conf.iops[WRITE] = temp;
			blkio_update_group_iops(blkg, plid, temp ?: -1, fileid);
			break;
		}
		break;
	default:
		BUG();
	}
	ret = 0;
out_unlock:
	rcu_read_unlock();
out:
	put_disk(disk);

	/*
	 * If queue was bypassing, we should retry.  Do so after a short
	 * msleep().  It isn't strictly necessary but queue can be
	 * bypassing for some time and it's always nice to avoid busy
	 * looping.
	 */
	if (ret == -EBUSY) {
		msleep(10);
		return restart_syscall();
	}
	return ret;
}

static int blkiocg_file_write(struct cgroup *cgrp, struct cftype *cft,
 				       const char *buffer)
{
	int ret = 0;
	char *buf;
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgrp);
	enum blkio_policy_id plid = BLKIOFILE_POLICY(cft->private);
	int fileid = BLKIOFILE_ATTR(cft->private);

	buf = kstrdup(buffer, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = blkio_policy_parse_and_set(buf, plid, fileid, blkcg);
	kfree(buf);
	return ret;
}

static const char *blkg_dev_name(struct blkio_group *blkg)
{
	/* some drivers (floppy) instantiate a queue w/o disk registered */
	if (blkg->q->backing_dev_info.dev)
		return dev_name(blkg->q->backing_dev_info.dev);
	return NULL;
}

static void blkio_print_group_conf(struct cftype *cft, struct blkio_group *blkg,
				   struct seq_file *m)
{
	int plid = BLKIOFILE_POLICY(cft->private);
	int fileid = BLKIOFILE_ATTR(cft->private);
	struct blkg_policy_data *pd = blkg->pd[plid];
	const char *dname = blkg_dev_name(blkg);
	int rw = WRITE;

	if (!dname)
		return;

	switch (plid) {
		case BLKIO_POLICY_PROP:
			if (pd->conf.weight)
				seq_printf(m, "%s\t%u\n",
					   dname, pd->conf.weight);
			break;
		case BLKIO_POLICY_THROTL:
			switch (fileid) {
			case BLKIO_THROTL_read_bps_device:
				rw = READ;
			case BLKIO_THROTL_write_bps_device:
				if (pd->conf.bps[rw])
					seq_printf(m, "%s\t%llu\n",
						   dname, pd->conf.bps[rw]);
				break;
			case BLKIO_THROTL_read_iops_device:
				rw = READ;
			case BLKIO_THROTL_write_iops_device:
				if (pd->conf.iops[rw])
					seq_printf(m, "%s\t%u\n",
						   dname, pd->conf.iops[rw]);
				break;
			}
			break;
		default:
			BUG();
	}
}

/* cgroup files which read their data from policy nodes end up here */
static void blkio_read_conf(struct cftype *cft, struct blkio_cgroup *blkcg,
			    struct seq_file *m)
{
	struct blkio_group *blkg;
	struct hlist_node *n;

	spin_lock_irq(&blkcg->lock);
	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node)
		blkio_print_group_conf(cft, blkg, m);
	spin_unlock_irq(&blkcg->lock);
}

static int blkiocg_file_read(struct cgroup *cgrp, struct cftype *cft,
				struct seq_file *m)
{
	struct blkio_cgroup *blkcg;
	enum blkio_policy_id plid = BLKIOFILE_POLICY(cft->private);
	int name = BLKIOFILE_ATTR(cft->private);

	blkcg = cgroup_to_blkio_cgroup(cgrp);

	switch(plid) {
	case BLKIO_POLICY_PROP:
		switch(name) {
		case BLKIO_PROP_weight_device:
			blkio_read_conf(cft, blkcg, m);
			return 0;
		default:
			BUG();
		}
		break;
	case BLKIO_POLICY_THROTL:
		switch(name){
		case BLKIO_THROTL_read_bps_device:
		case BLKIO_THROTL_write_bps_device:
		case BLKIO_THROTL_read_iops_device:
		case BLKIO_THROTL_write_iops_device:
			blkio_read_conf(cft, blkcg, m);
			return 0;
		default:
			BUG();
		}
		break;
	default:
		BUG();
	}

	return 0;
}

static int blkio_read_blkg_stats(struct blkio_cgroup *blkcg,
		struct cftype *cft, struct cgroup_map_cb *cb,
		enum stat_type type, bool show_total, bool pcpu)
{
	struct blkio_group *blkg;
	struct hlist_node *n;
	uint64_t cgroup_total = 0;

	spin_lock_irq(&blkcg->lock);

	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node) {
		const char *dname = blkg_dev_name(blkg);
		int plid = BLKIOFILE_POLICY(cft->private);

		if (!dname)
			continue;
		if (pcpu)
			cgroup_total += blkio_get_stat_cpu(blkg, plid,
							   cb, dname, type);
		else
			cgroup_total += blkio_get_stat(blkg, plid,
						       cb, dname, type);
	}
	if (show_total)
		cb->fill(cb, "Total", cgroup_total);

	spin_unlock_irq(&blkcg->lock);
	return 0;
}

/* All map kind of cgroup file get serviced by this function */
static int blkiocg_file_read_map(struct cgroup *cgrp, struct cftype *cft,
				struct cgroup_map_cb *cb)
{
	struct blkio_cgroup *blkcg;
	enum blkio_policy_id plid = BLKIOFILE_POLICY(cft->private);
	int name = BLKIOFILE_ATTR(cft->private);

	blkcg = cgroup_to_blkio_cgroup(cgrp);

	switch(plid) {
	case BLKIO_POLICY_PROP:
		switch(name) {
		case BLKIO_PROP_time:
			return blkio_read_blkg_stats(blkcg, cft, cb,
						BLKIO_STAT_TIME, 0, 0);
		case BLKIO_PROP_sectors:
			return blkio_read_blkg_stats(blkcg, cft, cb,
						BLKIO_STAT_CPU_SECTORS, 0, 1);
		case BLKIO_PROP_io_service_bytes:
			return blkio_read_blkg_stats(blkcg, cft, cb,
					BLKIO_STAT_CPU_SERVICE_BYTES, 1, 1);
		case BLKIO_PROP_io_serviced:
			return blkio_read_blkg_stats(blkcg, cft, cb,
						BLKIO_STAT_CPU_SERVICED, 1, 1);
		case BLKIO_PROP_io_service_time:
			return blkio_read_blkg_stats(blkcg, cft, cb,
						BLKIO_STAT_SERVICE_TIME, 1, 0);
		case BLKIO_PROP_io_wait_time:
			return blkio_read_blkg_stats(blkcg, cft, cb,
						BLKIO_STAT_WAIT_TIME, 1, 0);
		case BLKIO_PROP_io_merged:
			return blkio_read_blkg_stats(blkcg, cft, cb,
						BLKIO_STAT_MERGED, 1, 0);
		case BLKIO_PROP_io_queued:
			return blkio_read_blkg_stats(blkcg, cft, cb,
						BLKIO_STAT_QUEUED, 1, 0);
#ifdef CONFIG_DEBUG_BLK_CGROUP
		case BLKIO_PROP_unaccounted_time:
			return blkio_read_blkg_stats(blkcg, cft, cb,
					BLKIO_STAT_UNACCOUNTED_TIME, 0, 0);
		case BLKIO_PROP_dequeue:
			return blkio_read_blkg_stats(blkcg, cft, cb,
						BLKIO_STAT_DEQUEUE, 0, 0);
		case BLKIO_PROP_avg_queue_size:
			return blkio_read_blkg_stats(blkcg, cft, cb,
					BLKIO_STAT_AVG_QUEUE_SIZE, 0, 0);
		case BLKIO_PROP_group_wait_time:
			return blkio_read_blkg_stats(blkcg, cft, cb,
					BLKIO_STAT_GROUP_WAIT_TIME, 0, 0);
		case BLKIO_PROP_idle_time:
			return blkio_read_blkg_stats(blkcg, cft, cb,
						BLKIO_STAT_IDLE_TIME, 0, 0);
		case BLKIO_PROP_empty_time:
			return blkio_read_blkg_stats(blkcg, cft, cb,
						BLKIO_STAT_EMPTY_TIME, 0, 0);
#endif
		default:
			BUG();
		}
		break;
	case BLKIO_POLICY_THROTL:
		switch(name){
		case BLKIO_THROTL_io_service_bytes:
			return blkio_read_blkg_stats(blkcg, cft, cb,
						BLKIO_STAT_CPU_SERVICE_BYTES, 1, 1);
		case BLKIO_THROTL_io_serviced:
			return blkio_read_blkg_stats(blkcg, cft, cb,
						BLKIO_STAT_CPU_SERVICED, 1, 1);
		default:
			BUG();
		}
		break;
	default:
		BUG();
	}

	return 0;
}

static int blkio_weight_write(struct blkio_cgroup *blkcg, int plid, u64 val)
{
	struct blkio_group *blkg;
	struct hlist_node *n;

	if (val < BLKIO_WEIGHT_MIN || val > BLKIO_WEIGHT_MAX)
		return -EINVAL;

	spin_lock(&blkio_list_lock);
	spin_lock_irq(&blkcg->lock);
	blkcg->weight = (unsigned int)val;

	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node) {
		struct blkg_policy_data *pd = blkg->pd[plid];

		if (!pd->conf.weight)
			blkio_update_group_weight(blkg, plid, blkcg->weight);
	}

	spin_unlock_irq(&blkcg->lock);
	spin_unlock(&blkio_list_lock);
	return 0;
}

static u64 blkiocg_file_read_u64 (struct cgroup *cgrp, struct cftype *cft) {
	struct blkio_cgroup *blkcg;
	enum blkio_policy_id plid = BLKIOFILE_POLICY(cft->private);
	int name = BLKIOFILE_ATTR(cft->private);

	blkcg = cgroup_to_blkio_cgroup(cgrp);

	switch(plid) {
	case BLKIO_POLICY_PROP:
		switch(name) {
		case BLKIO_PROP_weight:
			return (u64)blkcg->weight;
		}
		break;
	default:
		BUG();
	}
	return 0;
}

static int
blkiocg_file_write_u64(struct cgroup *cgrp, struct cftype *cft, u64 val)
{
	struct blkio_cgroup *blkcg;
	enum blkio_policy_id plid = BLKIOFILE_POLICY(cft->private);
	int name = BLKIOFILE_ATTR(cft->private);

	blkcg = cgroup_to_blkio_cgroup(cgrp);

	switch(plid) {
	case BLKIO_POLICY_PROP:
		switch(name) {
		case BLKIO_PROP_weight:
			return blkio_weight_write(blkcg, plid, val);
		}
		break;
	default:
		BUG();
	}

	return 0;
}

struct cftype blkio_files[] = {
	{
		.name = "weight_device",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_weight_device),
		.read_seq_string = blkiocg_file_read,
		.write_string = blkiocg_file_write,
		.max_write_len = 256,
	},
	{
		.name = "weight",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_weight),
		.read_u64 = blkiocg_file_read_u64,
		.write_u64 = blkiocg_file_write_u64,
	},
	{
		.name = "time",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_time),
		.read_map = blkiocg_file_read_map,
	},
	{
		.name = "sectors",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_sectors),
		.read_map = blkiocg_file_read_map,
	},
	{
		.name = "io_service_bytes",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_io_service_bytes),
		.read_map = blkiocg_file_read_map,
	},
	{
		.name = "io_serviced",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_io_serviced),
		.read_map = blkiocg_file_read_map,
	},
	{
		.name = "io_service_time",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_io_service_time),
		.read_map = blkiocg_file_read_map,
	},
	{
		.name = "io_wait_time",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_io_wait_time),
		.read_map = blkiocg_file_read_map,
	},
	{
		.name = "io_merged",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_io_merged),
		.read_map = blkiocg_file_read_map,
	},
	{
		.name = "io_queued",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_io_queued),
		.read_map = blkiocg_file_read_map,
	},
	{
		.name = "reset_stats",
		.write_u64 = blkiocg_reset_stats,
	},
#ifdef CONFIG_BLK_DEV_THROTTLING
	{
		.name = "throttle.read_bps_device",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_THROTL,
				BLKIO_THROTL_read_bps_device),
		.read_seq_string = blkiocg_file_read,
		.write_string = blkiocg_file_write,
		.max_write_len = 256,
	},

	{
		.name = "throttle.write_bps_device",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_THROTL,
				BLKIO_THROTL_write_bps_device),
		.read_seq_string = blkiocg_file_read,
		.write_string = blkiocg_file_write,
		.max_write_len = 256,
	},

	{
		.name = "throttle.read_iops_device",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_THROTL,
				BLKIO_THROTL_read_iops_device),
		.read_seq_string = blkiocg_file_read,
		.write_string = blkiocg_file_write,
		.max_write_len = 256,
	},

	{
		.name = "throttle.write_iops_device",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_THROTL,
				BLKIO_THROTL_write_iops_device),
		.read_seq_string = blkiocg_file_read,
		.write_string = blkiocg_file_write,
		.max_write_len = 256,
	},
	{
		.name = "throttle.io_service_bytes",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_THROTL,
				BLKIO_THROTL_io_service_bytes),
		.read_map = blkiocg_file_read_map,
	},
	{
		.name = "throttle.io_serviced",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_THROTL,
				BLKIO_THROTL_io_serviced),
		.read_map = blkiocg_file_read_map,
	},
#endif /* CONFIG_BLK_DEV_THROTTLING */

#ifdef CONFIG_DEBUG_BLK_CGROUP
	{
		.name = "avg_queue_size",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_avg_queue_size),
		.read_map = blkiocg_file_read_map,
	},
	{
		.name = "group_wait_time",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_group_wait_time),
		.read_map = blkiocg_file_read_map,
	},
	{
		.name = "idle_time",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_idle_time),
		.read_map = blkiocg_file_read_map,
	},
	{
		.name = "empty_time",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_empty_time),
		.read_map = blkiocg_file_read_map,
	},
	{
		.name = "dequeue",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_dequeue),
		.read_map = blkiocg_file_read_map,
	},
	{
		.name = "unaccounted_time",
		.private = BLKIOFILE_PRIVATE(BLKIO_POLICY_PROP,
				BLKIO_PROP_unaccounted_time),
		.read_map = blkiocg_file_read_map,
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
