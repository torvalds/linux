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
#include "blk-cgroup.h"
#include "blk.h"

#define MAX_KEY_LEN 100

static DEFINE_SPINLOCK(blkio_list_lock);
static LIST_HEAD(blkio_list);

static DEFINE_MUTEX(all_q_mutex);
static LIST_HEAD(all_q_list);

struct blkio_cgroup blkio_root_cgroup = { .weight = 2*BLKIO_WEIGHT_DEFAULT };
EXPORT_SYMBOL_GPL(blkio_root_cgroup);

static struct blkio_policy_type *blkio_policy[BLKIO_NR_POLICIES];

static struct cgroup_subsys_state *blkiocg_create(struct cgroup_subsys *,
						  struct cgroup *);
static int blkiocg_can_attach(struct cgroup_subsys *, struct cgroup *,
			      struct cgroup_taskset *);
static void blkiocg_attach(struct cgroup_subsys *, struct cgroup *,
			   struct cgroup_taskset *);
static int blkiocg_pre_destroy(struct cgroup_subsys *, struct cgroup *);
static void blkiocg_destroy(struct cgroup_subsys *, struct cgroup *);
static int blkiocg_populate(struct cgroup_subsys *, struct cgroup *);

/* for encoding cft->private value on file */
#define BLKIOFILE_PRIVATE(x, val)	(((x) << 16) | (val))
/* What policy owns the file, proportional or throttle */
#define BLKIOFILE_POLICY(val)		(((val) >> 16) & 0xffff)
#define BLKIOFILE_ATTR(val)		((val) & 0xffff)

struct cgroup_subsys blkio_subsys = {
	.name = "blkio",
	.create = blkiocg_create,
	.can_attach = blkiocg_can_attach,
	.attach = blkiocg_attach,
	.pre_destroy = blkiocg_pre_destroy,
	.destroy = blkiocg_destroy,
	.populate = blkiocg_populate,
	.subsys_id = blkio_subsys_id,
	.module = THIS_MODULE,
};
EXPORT_SYMBOL_GPL(blkio_subsys);

struct blkio_cgroup *cgroup_to_blkio_cgroup(struct cgroup *cgroup)
{
	return container_of(cgroup_subsys_state(cgroup, blkio_subsys_id),
			    struct blkio_cgroup, css);
}
EXPORT_SYMBOL_GPL(cgroup_to_blkio_cgroup);

struct blkio_cgroup *task_blkio_cgroup(struct task_struct *tsk)
{
	return container_of(task_subsys_state(tsk, blkio_subsys_id),
			    struct blkio_cgroup, css);
}
EXPORT_SYMBOL_GPL(task_blkio_cgroup);

static inline void
blkio_update_group_weight(struct blkio_group *blkg, unsigned int weight)
{
	struct blkio_policy_type *blkiop;

	list_for_each_entry(blkiop, &blkio_list, list) {
		/* If this policy does not own the blkg, do not send updates */
		if (blkiop->plid != blkg->plid)
			continue;
		if (blkiop->ops.blkio_update_group_weight_fn)
			blkiop->ops.blkio_update_group_weight_fn(blkg->q,
							blkg, weight);
	}
}

static inline void blkio_update_group_bps(struct blkio_group *blkg, u64 bps,
				int fileid)
{
	struct blkio_policy_type *blkiop;

	list_for_each_entry(blkiop, &blkio_list, list) {

		/* If this policy does not own the blkg, do not send updates */
		if (blkiop->plid != blkg->plid)
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
			unsigned int iops, int fileid)
{
	struct blkio_policy_type *blkiop;

	list_for_each_entry(blkiop, &blkio_list, list) {

		/* If this policy does not own the blkg, do not send updates */
		if (blkiop->plid != blkg->plid)
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
 * This should be called with the blkg->stats_lock held.
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
 * This should be called with the blkg->stats_lock held.
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
/* This should be called with the blkg->stats_lock held. */
static void blkio_set_start_group_wait_time(struct blkio_group *blkg,
						struct blkio_group *curr_blkg)
{
	if (blkio_blkg_waiting(&blkg->stats))
		return;
	if (blkg == curr_blkg)
		return;
	blkg->stats.start_group_wait_time = sched_clock();
	blkio_mark_blkg_waiting(&blkg->stats);
}

/* This should be called with the blkg->stats_lock held. */
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

/* This should be called with the blkg->stats_lock held. */
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

void blkiocg_update_set_idle_time_stats(struct blkio_group *blkg)
{
	unsigned long flags;

	spin_lock_irqsave(&blkg->stats_lock, flags);
	BUG_ON(blkio_blkg_idling(&blkg->stats));
	blkg->stats.start_idle_time = sched_clock();
	blkio_mark_blkg_idling(&blkg->stats);
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_set_idle_time_stats);

void blkiocg_update_idle_time_stats(struct blkio_group *blkg)
{
	unsigned long flags;
	unsigned long long now;
	struct blkio_group_stats *stats;

	spin_lock_irqsave(&blkg->stats_lock, flags);
	stats = &blkg->stats;
	if (blkio_blkg_idling(stats)) {
		now = sched_clock();
		if (time_after64(now, stats->start_idle_time))
			stats->idle_time += now - stats->start_idle_time;
		blkio_clear_blkg_idling(stats);
	}
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_idle_time_stats);

void blkiocg_update_avg_queue_size_stats(struct blkio_group *blkg)
{
	unsigned long flags;
	struct blkio_group_stats *stats;

	spin_lock_irqsave(&blkg->stats_lock, flags);
	stats = &blkg->stats;
	stats->avg_queue_size_sum +=
			stats->stat_arr[BLKIO_STAT_QUEUED][BLKIO_STAT_READ] +
			stats->stat_arr[BLKIO_STAT_QUEUED][BLKIO_STAT_WRITE];
	stats->avg_queue_size_samples++;
	blkio_update_group_wait_time(stats);
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_avg_queue_size_stats);

void blkiocg_set_start_empty_time(struct blkio_group *blkg)
{
	unsigned long flags;
	struct blkio_group_stats *stats;

	spin_lock_irqsave(&blkg->stats_lock, flags);
	stats = &blkg->stats;

	if (stats->stat_arr[BLKIO_STAT_QUEUED][BLKIO_STAT_READ] ||
			stats->stat_arr[BLKIO_STAT_QUEUED][BLKIO_STAT_WRITE]) {
		spin_unlock_irqrestore(&blkg->stats_lock, flags);
		return;
	}

	/*
	 * group is already marked empty. This can happen if cfqq got new
	 * request in parent group and moved to this group while being added
	 * to service tree. Just ignore the event and move on.
	 */
	if(blkio_blkg_empty(stats)) {
		spin_unlock_irqrestore(&blkg->stats_lock, flags);
		return;
	}

	stats->start_empty_time = sched_clock();
	blkio_mark_blkg_empty(stats);
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_set_start_empty_time);

void blkiocg_update_dequeue_stats(struct blkio_group *blkg,
			unsigned long dequeue)
{
	blkg->stats.dequeue += dequeue;
}
EXPORT_SYMBOL_GPL(blkiocg_update_dequeue_stats);
#else
static inline void blkio_set_start_group_wait_time(struct blkio_group *blkg,
					struct blkio_group *curr_blkg) {}
static inline void blkio_end_empty_time(struct blkio_group_stats *stats) {}
#endif

void blkiocg_update_io_add_stats(struct blkio_group *blkg,
			struct blkio_group *curr_blkg, bool direction,
			bool sync)
{
	unsigned long flags;

	spin_lock_irqsave(&blkg->stats_lock, flags);
	blkio_add_stat(blkg->stats.stat_arr[BLKIO_STAT_QUEUED], 1, direction,
			sync);
	blkio_end_empty_time(&blkg->stats);
	blkio_set_start_group_wait_time(blkg, curr_blkg);
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_io_add_stats);

void blkiocg_update_io_remove_stats(struct blkio_group *blkg,
						bool direction, bool sync)
{
	unsigned long flags;

	spin_lock_irqsave(&blkg->stats_lock, flags);
	blkio_check_and_dec_stat(blkg->stats.stat_arr[BLKIO_STAT_QUEUED],
					direction, sync);
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_io_remove_stats);

void blkiocg_update_timeslice_used(struct blkio_group *blkg, unsigned long time,
				unsigned long unaccounted_time)
{
	unsigned long flags;

	spin_lock_irqsave(&blkg->stats_lock, flags);
	blkg->stats.time += time;
#ifdef CONFIG_DEBUG_BLK_CGROUP
	blkg->stats.unaccounted_time += unaccounted_time;
#endif
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_timeslice_used);

/*
 * should be called under rcu read lock or queue lock to make sure blkg pointer
 * is valid.
 */
void blkiocg_update_dispatch_stats(struct blkio_group *blkg,
				uint64_t bytes, bool direction, bool sync)
{
	struct blkio_group_stats_cpu *stats_cpu;
	unsigned long flags;

	/*
	 * Disabling interrupts to provide mutual exclusion between two
	 * writes on same cpu. It probably is not needed for 64bit. Not
	 * optimizing that case yet.
	 */
	local_irq_save(flags);

	stats_cpu = this_cpu_ptr(blkg->stats_cpu);

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
	uint64_t start_time, uint64_t io_start_time, bool direction, bool sync)
{
	struct blkio_group_stats *stats;
	unsigned long flags;
	unsigned long long now = sched_clock();

	spin_lock_irqsave(&blkg->stats_lock, flags);
	stats = &blkg->stats;
	if (time_after64(now, io_start_time))
		blkio_add_stat(stats->stat_arr[BLKIO_STAT_SERVICE_TIME],
				now - io_start_time, direction, sync);
	if (time_after64(io_start_time, start_time))
		blkio_add_stat(stats->stat_arr[BLKIO_STAT_WAIT_TIME],
				io_start_time - start_time, direction, sync);
	spin_unlock_irqrestore(&blkg->stats_lock, flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_completion_stats);

/*  Merged stats are per cpu.  */
void blkiocg_update_io_merged_stats(struct blkio_group *blkg, bool direction,
					bool sync)
{
	struct blkio_group_stats_cpu *stats_cpu;
	unsigned long flags;

	/*
	 * Disabling interrupts to provide mutual exclusion between two
	 * writes on same cpu. It probably is not needed for 64bit. Not
	 * optimizing that case yet.
	 */
	local_irq_save(flags);

	stats_cpu = this_cpu_ptr(blkg->stats_cpu);

	u64_stats_update_begin(&stats_cpu->syncp);
	blkio_add_stat(stats_cpu->stat_arr_cpu[BLKIO_STAT_CPU_MERGED], 1,
				direction, sync);
	u64_stats_update_end(&stats_cpu->syncp);
	local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(blkiocg_update_io_merged_stats);

struct blkio_group *blkg_lookup_create(struct blkio_cgroup *blkcg,
				       struct request_queue *q,
				       enum blkio_policy_id plid,
				       bool for_root)
	__releases(q->queue_lock) __acquires(q->queue_lock)
{
	struct blkio_policy_type *pol = blkio_policy[plid];
	struct blkio_group *blkg, *new_blkg;

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

	blkg = blkg_lookup(blkcg, q, plid);
	if (blkg)
		return blkg;

	/* blkg holds a reference to blkcg */
	if (!css_tryget(&blkcg->css))
		return ERR_PTR(-EINVAL);

	/*
	 * Allocate and initialize.
	 *
	 * FIXME: The following is broken.  Percpu memory allocation
	 * requires %GFP_KERNEL context and can't be performed from IO
	 * path.  Allocation here should inherently be atomic and the
	 * following lock dancing can be removed once the broken percpu
	 * allocation is fixed.
	 */
	spin_unlock_irq(q->queue_lock);
	rcu_read_unlock();

	new_blkg = pol->ops.blkio_alloc_group_fn(q, blkcg);
	if (new_blkg) {
		new_blkg->stats_cpu = alloc_percpu(struct blkio_group_stats_cpu);

		spin_lock_init(&new_blkg->stats_lock);
		rcu_assign_pointer(new_blkg->q, q);
		new_blkg->blkcg = blkcg;
		new_blkg->plid = plid;
		cgroup_path(blkcg->css.cgroup, new_blkg->path,
			    sizeof(new_blkg->path));
	} else {
		css_put(&blkcg->css);
	}

	rcu_read_lock();
	spin_lock_irq(q->queue_lock);

	/* did bypass get turned on inbetween? */
	if (unlikely(blk_queue_bypass(q)) && !for_root) {
		blkg = ERR_PTR(blk_queue_dead(q) ? -EINVAL : -EBUSY);
		goto out;
	}

	/* did someone beat us to it? */
	blkg = blkg_lookup(blkcg, q, plid);
	if (unlikely(blkg))
		goto out;

	/* did alloc fail? */
	if (unlikely(!new_blkg || !new_blkg->stats_cpu)) {
		blkg = ERR_PTR(-ENOMEM);
		goto out;
	}

	/* insert */
	spin_lock(&blkcg->lock);
	swap(blkg, new_blkg);
	hlist_add_head_rcu(&blkg->blkcg_node, &blkcg->blkg_list);
	pol->ops.blkio_link_group_fn(q, blkg);
	spin_unlock(&blkcg->lock);
out:
	if (new_blkg) {
		free_percpu(new_blkg->stats_cpu);
		kfree(new_blkg);
		css_put(&blkcg->css);
	}
	return blkg;
}
EXPORT_SYMBOL_GPL(blkg_lookup_create);

static void __blkiocg_del_blkio_group(struct blkio_group *blkg)
{
	hlist_del_init_rcu(&blkg->blkcg_node);
}

/*
 * returns 0 if blkio_group was still on cgroup list. Otherwise returns 1
 * indicating that blk_group was unhashed by the time we got to it.
 */
int blkiocg_del_blkio_group(struct blkio_group *blkg)
{
	struct blkio_cgroup *blkcg = blkg->blkcg;
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&blkcg->lock, flags);
	if (!hlist_unhashed(&blkg->blkcg_node)) {
		__blkiocg_del_blkio_group(blkg);
		ret = 0;
	}
	spin_unlock_irqrestore(&blkcg->lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(blkiocg_del_blkio_group);

/* called under rcu_read_lock(). */
struct blkio_group *blkg_lookup(struct blkio_cgroup *blkcg,
				struct request_queue *q,
				enum blkio_policy_id plid)
{
	struct blkio_group *blkg;
	struct hlist_node *n;

	hlist_for_each_entry_rcu(blkg, n, &blkcg->blkg_list, blkcg_node)
		if (blkg->q == q && blkg->plid == plid)
			return blkg;
	return NULL;
}
EXPORT_SYMBOL_GPL(blkg_lookup);

void blkg_destroy_all(struct request_queue *q)
{
	struct blkio_policy_type *pol;

	while (true) {
		bool done = true;

		spin_lock(&blkio_list_lock);
		spin_lock_irq(q->queue_lock);

		/*
		 * clear_queue_fn() might return with non-empty group list
		 * if it raced cgroup removal and lost.  cgroup removal is
		 * guaranteed to make forward progress and retrying after a
		 * while is enough.  This ugliness is scheduled to be
		 * removed after locking update.
		 */
		list_for_each_entry(pol, &blkio_list, list)
			if (!pol->ops.blkio_clear_queue_fn(q))
				done = false;

		spin_unlock_irq(q->queue_lock);
		spin_unlock(&blkio_list_lock);

		if (done)
			break;

		msleep(10);	/* just some random duration I like */
	}
}

static void blkio_reset_stats_cpu(struct blkio_group *blkg)
{
	struct blkio_group_stats_cpu *stats_cpu;
	int i, j, k;
	/*
	 * Note: On 64 bit arch this should not be an issue. This has the
	 * possibility of returning some inconsistent value on 32bit arch
	 * as 64bit update on 32bit is non atomic. Taking care of this
	 * corner case makes code very complicated, like sending IPIs to
	 * cpus, taking care of stats of offline cpus etc.
	 *
	 * reset stats is anyway more of a debug feature and this sounds a
	 * corner case. So I am not complicating the code yet until and
	 * unless this becomes a real issue.
	 */
	for_each_possible_cpu(i) {
		stats_cpu = per_cpu_ptr(blkg->stats_cpu, i);
		stats_cpu->sectors = 0;
		for(j = 0; j < BLKIO_STAT_CPU_NR; j++)
			for (k = 0; k < BLKIO_STAT_TOTAL; k++)
				stats_cpu->stat_arr_cpu[j][k] = 0;
	}
}

static int
blkiocg_reset_stats(struct cgroup *cgroup, struct cftype *cftype, u64 val)
{
	struct blkio_cgroup *blkcg;
	struct blkio_group *blkg;
	struct blkio_group_stats *stats;
	struct hlist_node *n;
	uint64_t queued[BLKIO_STAT_TOTAL];
	int i;
#ifdef CONFIG_DEBUG_BLK_CGROUP
	bool idling, waiting, empty;
	unsigned long long now = sched_clock();
#endif

	blkcg = cgroup_to_blkio_cgroup(cgroup);
	spin_lock_irq(&blkcg->lock);
	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node) {
		spin_lock(&blkg->stats_lock);
		stats = &blkg->stats;
#ifdef CONFIG_DEBUG_BLK_CGROUP
		idling = blkio_blkg_idling(stats);
		waiting = blkio_blkg_waiting(stats);
		empty = blkio_blkg_empty(stats);
#endif
		for (i = 0; i < BLKIO_STAT_TOTAL; i++)
			queued[i] = stats->stat_arr[BLKIO_STAT_QUEUED][i];
		memset(stats, 0, sizeof(struct blkio_group_stats));
		for (i = 0; i < BLKIO_STAT_TOTAL; i++)
			stats->stat_arr[BLKIO_STAT_QUEUED][i] = queued[i];
#ifdef CONFIG_DEBUG_BLK_CGROUP
		if (idling) {
			blkio_mark_blkg_idling(stats);
			stats->start_idle_time = now;
		}
		if (waiting) {
			blkio_mark_blkg_waiting(stats);
			stats->start_group_wait_time = now;
		}
		if (empty) {
			blkio_mark_blkg_empty(stats);
			stats->start_empty_time = now;
		}
#endif
		spin_unlock(&blkg->stats_lock);

		/* Reset Per cpu stats which don't take blkg->stats_lock */
		blkio_reset_stats_cpu(blkg);
	}

	spin_unlock_irq(&blkcg->lock);
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

static uint64_t blkio_fill_stat(char *str, int chars_left, uint64_t val,
				struct cgroup_map_cb *cb, const char *dname)
{
	blkio_get_key_name(0, dname, str, chars_left, true);
	cb->fill(cb, str, val);
	return val;
}


static uint64_t blkio_read_stat_cpu(struct blkio_group *blkg,
			enum stat_type_cpu type, enum stat_sub_type sub_type)
{
	int cpu;
	struct blkio_group_stats_cpu *stats_cpu;
	u64 val = 0, tval;

	for_each_possible_cpu(cpu) {
		unsigned int start;
		stats_cpu  = per_cpu_ptr(blkg->stats_cpu, cpu);

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

static uint64_t blkio_get_stat_cpu(struct blkio_group *blkg,
				   struct cgroup_map_cb *cb, const char *dname,
				   enum stat_type_cpu type)
{
	uint64_t disk_total, val;
	char key_str[MAX_KEY_LEN];
	enum stat_sub_type sub_type;

	if (type == BLKIO_STAT_CPU_SECTORS) {
		val = blkio_read_stat_cpu(blkg, type, 0);
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1, val, cb,
				       dname);
	}

	for (sub_type = BLKIO_STAT_READ; sub_type < BLKIO_STAT_TOTAL;
			sub_type++) {
		blkio_get_key_name(sub_type, dname, key_str, MAX_KEY_LEN,
				   false);
		val = blkio_read_stat_cpu(blkg, type, sub_type);
		cb->fill(cb, key_str, val);
	}

	disk_total = blkio_read_stat_cpu(blkg, type, BLKIO_STAT_READ) +
			blkio_read_stat_cpu(blkg, type, BLKIO_STAT_WRITE);

	blkio_get_key_name(BLKIO_STAT_TOTAL, dname, key_str, MAX_KEY_LEN,
			   false);
	cb->fill(cb, key_str, disk_total);
	return disk_total;
}

/* This should be called with blkg->stats_lock held */
static uint64_t blkio_get_stat(struct blkio_group *blkg,
			       struct cgroup_map_cb *cb, const char *dname,
			       enum stat_type type)
{
	uint64_t disk_total;
	char key_str[MAX_KEY_LEN];
	enum stat_sub_type sub_type;

	if (type == BLKIO_STAT_TIME)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
					blkg->stats.time, cb, dname);
#ifdef CONFIG_DEBUG_BLK_CGROUP
	if (type == BLKIO_STAT_UNACCOUNTED_TIME)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
				       blkg->stats.unaccounted_time, cb, dname);
	if (type == BLKIO_STAT_AVG_QUEUE_SIZE) {
		uint64_t sum = blkg->stats.avg_queue_size_sum;
		uint64_t samples = blkg->stats.avg_queue_size_samples;
		if (samples)
			do_div(sum, samples);
		else
			sum = 0;
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
				       sum, cb, dname);
	}
	if (type == BLKIO_STAT_GROUP_WAIT_TIME)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
				       blkg->stats.group_wait_time, cb, dname);
	if (type == BLKIO_STAT_IDLE_TIME)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
				       blkg->stats.idle_time, cb, dname);
	if (type == BLKIO_STAT_EMPTY_TIME)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
				       blkg->stats.empty_time, cb, dname);
	if (type == BLKIO_STAT_DEQUEUE)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
				       blkg->stats.dequeue, cb, dname);
#endif

	for (sub_type = BLKIO_STAT_READ; sub_type < BLKIO_STAT_TOTAL;
			sub_type++) {
		blkio_get_key_name(sub_type, dname, key_str, MAX_KEY_LEN,
				   false);
		cb->fill(cb, key_str, blkg->stats.stat_arr[type][sub_type]);
	}
	disk_total = blkg->stats.stat_arr[type][BLKIO_STAT_READ] +
			blkg->stats.stat_arr[type][BLKIO_STAT_WRITE];
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
	blkg = blkg_lookup_create(blkcg, disk->queue, plid, false);
	spin_unlock_irq(disk->queue->queue_lock);

	if (IS_ERR(blkg)) {
		ret = PTR_ERR(blkg);
		goto out_unlock;
	}

	switch (plid) {
	case BLKIO_POLICY_PROP:
		if ((temp < BLKIO_WEIGHT_MIN && temp > 0) ||
		     temp > BLKIO_WEIGHT_MAX)
			goto out_unlock;

		blkg->conf.weight = temp;
		blkio_update_group_weight(blkg, temp ?: blkcg->weight);
		break;
	case BLKIO_POLICY_THROTL:
		switch(fileid) {
		case BLKIO_THROTL_read_bps_device:
			blkg->conf.bps[READ] = temp;
			blkio_update_group_bps(blkg, temp ?: -1, fileid);
			break;
		case BLKIO_THROTL_write_bps_device:
			blkg->conf.bps[WRITE] = temp;
			blkio_update_group_bps(blkg, temp ?: -1, fileid);
			break;
		case BLKIO_THROTL_read_iops_device:
			if (temp > THROTL_IOPS_MAX)
				goto out_unlock;
			blkg->conf.iops[READ] = temp;
			blkio_update_group_iops(blkg, temp ?: -1, fileid);
			break;
		case BLKIO_THROTL_write_iops_device:
			if (temp > THROTL_IOPS_MAX)
				goto out_unlock;
			blkg->conf.iops[WRITE] = temp;
			blkio_update_group_iops(blkg, temp ?: -1, fileid);
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
	const char *dname = blkg_dev_name(blkg);
	int fileid = BLKIOFILE_ATTR(cft->private);
	int rw = WRITE;

	if (!dname)
		return;

	switch (blkg->plid) {
		case BLKIO_POLICY_PROP:
			if (blkg->conf.weight)
				seq_printf(m, "%s\t%u\n",
					   dname, blkg->conf.weight);
			break;
		case BLKIO_POLICY_THROTL:
			switch (fileid) {
			case BLKIO_THROTL_read_bps_device:
				rw = READ;
			case BLKIO_THROTL_write_bps_device:
				if (blkg->conf.bps[rw])
					seq_printf(m, "%s\t%llu\n",
						   dname, blkg->conf.bps[rw]);
				break;
			case BLKIO_THROTL_read_iops_device:
				rw = READ;
			case BLKIO_THROTL_write_iops_device:
				if (blkg->conf.iops[rw])
					seq_printf(m, "%s\t%u\n",
						   dname, blkg->conf.iops[rw]);
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
		if (BLKIOFILE_POLICY(cft->private) == blkg->plid)
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

	rcu_read_lock();
	hlist_for_each_entry_rcu(blkg, n, &blkcg->blkg_list, blkcg_node) {
		const char *dname = blkg_dev_name(blkg);

		if (!dname || BLKIOFILE_POLICY(cft->private) != blkg->plid)
			continue;
		if (pcpu)
			cgroup_total += blkio_get_stat_cpu(blkg, cb, dname,
							   type);
		else {
			spin_lock_irq(&blkg->stats_lock);
			cgroup_total += blkio_get_stat(blkg, cb, dname, type);
			spin_unlock_irq(&blkg->stats_lock);
		}
	}
	if (show_total)
		cb->fill(cb, "Total", cgroup_total);
	rcu_read_unlock();
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
						BLKIO_STAT_CPU_MERGED, 1, 1);
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

	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node)
		if (blkg->plid == plid && !blkg->conf.weight)
			blkio_update_group_weight(blkg, blkcg->weight);

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
};

static int blkiocg_populate(struct cgroup_subsys *subsys, struct cgroup *cgroup)
{
	return cgroup_add_files(cgroup, subsys, blkio_files,
				ARRAY_SIZE(blkio_files));
}

static int blkiocg_pre_destroy(struct cgroup_subsys *subsys,
			       struct cgroup *cgroup)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgroup);
	unsigned long flags;
	struct blkio_group *blkg;
	struct request_queue *q;
	struct blkio_policy_type *blkiop;

	rcu_read_lock();

	do {
		spin_lock_irqsave(&blkcg->lock, flags);

		if (hlist_empty(&blkcg->blkg_list)) {
			spin_unlock_irqrestore(&blkcg->lock, flags);
			break;
		}

		blkg = hlist_entry(blkcg->blkg_list.first, struct blkio_group,
					blkcg_node);
		q = rcu_dereference(blkg->q);
		__blkiocg_del_blkio_group(blkg);

		spin_unlock_irqrestore(&blkcg->lock, flags);

		/*
		 * This blkio_group is being unlinked as associated cgroup is
		 * going away. Let all the IO controlling policies know about
		 * this event.
		 */
		spin_lock(&blkio_list_lock);
		list_for_each_entry(blkiop, &blkio_list, list) {
			if (blkiop->plid != blkg->plid)
				continue;
			blkiop->ops.blkio_unlink_group_fn(q, blkg);
		}
		spin_unlock(&blkio_list_lock);
	} while (1);

	rcu_read_unlock();

	return 0;
}

static void blkiocg_destroy(struct cgroup_subsys *subsys, struct cgroup *cgroup)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgroup);

	if (blkcg != &blkio_root_cgroup)
		kfree(blkcg);
}

static struct cgroup_subsys_state *
blkiocg_create(struct cgroup_subsys *subsys, struct cgroup *cgroup)
{
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

	blk_throtl_exit(q);
}

/*
 * We cannot support shared io contexts, as we have no mean to support
 * two tasks with the same ioc in two different groups without major rework
 * of the main cic data structures.  For now we allow a task to change
 * its cgroup only if it's the only owner of its ioc.
 */
static int blkiocg_can_attach(struct cgroup_subsys *ss, struct cgroup *cgrp,
			      struct cgroup_taskset *tset)
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

static void blkiocg_attach(struct cgroup_subsys *ss, struct cgroup *cgrp,
			   struct cgroup_taskset *tset)
{
	struct task_struct *task;
	struct io_context *ioc;

	cgroup_taskset_for_each(task, cgrp, tset) {
		/* we don't lose anything even if ioc allocation fails */
		ioc = get_task_io_context(task, GFP_ATOMIC, NUMA_NO_NODE);
		if (ioc) {
			ioc_cgroup_changed(ioc);
			put_io_context(ioc);
		}
	}
}

static void blkcg_bypass_start(void)
	__acquires(&all_q_mutex)
{
	struct request_queue *q;

	mutex_lock(&all_q_mutex);

	list_for_each_entry(q, &all_q_list, all_q_node) {
		blk_queue_bypass_start(q);
		blkg_destroy_all(q);
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

void blkio_policy_register(struct blkio_policy_type *blkiop)
{
	blkcg_bypass_start();
	spin_lock(&blkio_list_lock);

	BUG_ON(blkio_policy[blkiop->plid]);
	blkio_policy[blkiop->plid] = blkiop;
	list_add_tail(&blkiop->list, &blkio_list);

	spin_unlock(&blkio_list_lock);
	blkcg_bypass_end();
}
EXPORT_SYMBOL_GPL(blkio_policy_register);

void blkio_policy_unregister(struct blkio_policy_type *blkiop)
{
	blkcg_bypass_start();
	spin_lock(&blkio_list_lock);

	BUG_ON(blkio_policy[blkiop->plid] != blkiop);
	blkio_policy[blkiop->plid] = NULL;
	list_del_init(&blkiop->list);

	spin_unlock(&blkio_list_lock);
	blkcg_bypass_end();
}
EXPORT_SYMBOL_GPL(blkio_policy_unregister);
