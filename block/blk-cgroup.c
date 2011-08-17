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
#include "blk-cgroup.h"
#include <linux/genhd.h>

#define MAX_KEY_LEN 100

static DEFINE_SPINLOCK(blkio_list_lock);
static LIST_HEAD(blkio_list);

struct blkio_cgroup blkio_root_cgroup = { .weight = 2*BLKIO_WEIGHT_DEFAULT };
EXPORT_SYMBOL_GPL(blkio_root_cgroup);

static struct cgroup_subsys_state *blkiocg_create(struct cgroup_subsys *,
						  struct cgroup *);
static int blkiocg_can_attach_task(struct cgroup *, struct task_struct *);
static void blkiocg_attach_task(struct cgroup *, struct task_struct *);
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
	.can_attach_task = blkiocg_can_attach_task,
	.attach_task = blkiocg_attach_task,
	.destroy = blkiocg_destroy,
	.populate = blkiocg_populate,
#ifdef CONFIG_BLK_CGROUP
	/* note: blkio_subsys_id is otherwise defined in blk-cgroup.h */
	.subsys_id = blkio_subsys_id,
#endif
	.use_id = 1,
	.module = THIS_MODULE,
};
EXPORT_SYMBOL_GPL(blkio_subsys);

static inline void blkio_policy_insert_node(struct blkio_cgroup *blkcg,
					    struct blkio_policy_node *pn)
{
	list_add(&pn->node, &blkcg->policy_list);
}

static inline bool cftype_blkg_same_policy(struct cftype *cft,
			struct blkio_group *blkg)
{
	enum blkio_policy_id plid = BLKIOFILE_POLICY(cft->private);

	if (blkg->plid == plid)
		return 1;

	return 0;
}

/* Determines if policy node matches cgroup file being accessed */
static inline bool pn_matches_cftype(struct cftype *cft,
			struct blkio_policy_node *pn)
{
	enum blkio_policy_id plid = BLKIOFILE_POLICY(cft->private);
	int fileid = BLKIOFILE_ATTR(cft->private);

	return (plid == pn->plid && fileid == pn->fileid);
}

/* Must be called with blkcg->lock held */
static inline void blkio_policy_delete_node(struct blkio_policy_node *pn)
{
	list_del(&pn->node);
}

/* Must be called with blkcg->lock held */
static struct blkio_policy_node *
blkio_policy_search_node(const struct blkio_cgroup *blkcg, dev_t dev,
		enum blkio_policy_id plid, int fileid)
{
	struct blkio_policy_node *pn;

	list_for_each_entry(pn, &blkcg->policy_list, node) {
		if (pn->dev == dev && pn->plid == plid && pn->fileid == fileid)
			return pn;
	}

	return NULL;
}

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
			blkiop->ops.blkio_update_group_weight_fn(blkg->key,
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
			blkiop->ops.blkio_update_group_read_bps_fn(blkg->key,
								blkg, bps);

		if (fileid == BLKIO_THROTL_write_bps_device
		    && blkiop->ops.blkio_update_group_write_bps_fn)
			blkiop->ops.blkio_update_group_write_bps_fn(blkg->key,
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
			blkiop->ops.blkio_update_group_read_iops_fn(blkg->key,
								blkg, iops);

		if (fileid == BLKIO_THROTL_write_iops_device
		    && blkiop->ops.blkio_update_group_write_iops_fn)
			blkiop->ops.blkio_update_group_write_iops_fn(blkg->key,
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

/*
 * This function allocates the per cpu stats for blkio_group. Should be called
 * from sleepable context as alloc_per_cpu() requires that.
 */
int blkio_alloc_blkg_stats(struct blkio_group *blkg)
{
	/* Allocate memory for per cpu stats */
	blkg->stats_cpu = alloc_percpu(struct blkio_group_stats_cpu);
	if (!blkg->stats_cpu)
		return -ENOMEM;
	return 0;
}
EXPORT_SYMBOL_GPL(blkio_alloc_blkg_stats);

void blkiocg_add_blkio_group(struct blkio_cgroup *blkcg,
		struct blkio_group *blkg, void *key, dev_t dev,
		enum blkio_policy_id plid)
{
	unsigned long flags;

	spin_lock_irqsave(&blkcg->lock, flags);
	spin_lock_init(&blkg->stats_lock);
	rcu_assign_pointer(blkg->key, key);
	blkg->blkcg_id = css_id(&blkcg->css);
	hlist_add_head_rcu(&blkg->blkcg_node, &blkcg->blkg_list);
	blkg->plid = plid;
	spin_unlock_irqrestore(&blkcg->lock, flags);
	/* Need to take css reference ? */
	cgroup_path(blkcg->css.cgroup, blkg->path, sizeof(blkg->path));
	blkg->dev = dev;
}
EXPORT_SYMBOL_GPL(blkiocg_add_blkio_group);

static void __blkiocg_del_blkio_group(struct blkio_group *blkg)
{
	hlist_del_init_rcu(&blkg->blkcg_node);
	blkg->blkcg_id = 0;
}

/*
 * returns 0 if blkio_group was still on cgroup list. Otherwise returns 1
 * indicating that blk_group was unhashed by the time we got to it.
 */
int blkiocg_del_blkio_group(struct blkio_group *blkg)
{
	struct blkio_cgroup *blkcg;
	unsigned long flags;
	struct cgroup_subsys_state *css;
	int ret = 1;

	rcu_read_lock();
	css = css_lookup(&blkio_subsys, blkg->blkcg_id);
	if (css) {
		blkcg = container_of(css, struct blkio_cgroup, css);
		spin_lock_irqsave(&blkcg->lock, flags);
		if (!hlist_unhashed(&blkg->blkcg_node)) {
			__blkiocg_del_blkio_group(blkg);
			ret = 0;
		}
		spin_unlock_irqrestore(&blkcg->lock, flags);
	}

	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(blkiocg_del_blkio_group);

/* called under rcu_read_lock(). */
struct blkio_group *blkiocg_lookup_group(struct blkio_cgroup *blkcg, void *key)
{
	struct blkio_group *blkg;
	struct hlist_node *n;
	void *__key;

	hlist_for_each_entry_rcu(blkg, n, &blkcg->blkg_list, blkcg_node) {
		__key = blkg->key;
		if (__key == key)
			return blkg;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(blkiocg_lookup_group);

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

static void blkio_get_key_name(enum stat_sub_type type, dev_t dev, char *str,
				int chars_left, bool diskname_only)
{
	snprintf(str, chars_left, "%d:%d", MAJOR(dev), MINOR(dev));
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
				struct cgroup_map_cb *cb, dev_t dev)
{
	blkio_get_key_name(0, dev, str, chars_left, true);
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
		struct cgroup_map_cb *cb, dev_t dev, enum stat_type_cpu type)
{
	uint64_t disk_total, val;
	char key_str[MAX_KEY_LEN];
	enum stat_sub_type sub_type;

	if (type == BLKIO_STAT_CPU_SECTORS) {
		val = blkio_read_stat_cpu(blkg, type, 0);
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1, val, cb, dev);
	}

	for (sub_type = BLKIO_STAT_READ; sub_type < BLKIO_STAT_TOTAL;
			sub_type++) {
		blkio_get_key_name(sub_type, dev, key_str, MAX_KEY_LEN, false);
		val = blkio_read_stat_cpu(blkg, type, sub_type);
		cb->fill(cb, key_str, val);
	}

	disk_total = blkio_read_stat_cpu(blkg, type, BLKIO_STAT_READ) +
			blkio_read_stat_cpu(blkg, type, BLKIO_STAT_WRITE);

	blkio_get_key_name(BLKIO_STAT_TOTAL, dev, key_str, MAX_KEY_LEN, false);
	cb->fill(cb, key_str, disk_total);
	return disk_total;
}

/* This should be called with blkg->stats_lock held */
static uint64_t blkio_get_stat(struct blkio_group *blkg,
		struct cgroup_map_cb *cb, dev_t dev, enum stat_type type)
{
	uint64_t disk_total;
	char key_str[MAX_KEY_LEN];
	enum stat_sub_type sub_type;

	if (type == BLKIO_STAT_TIME)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
					blkg->stats.time, cb, dev);
#ifdef CONFIG_DEBUG_BLK_CGROUP
	if (type == BLKIO_STAT_UNACCOUNTED_TIME)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
					blkg->stats.unaccounted_time, cb, dev);
	if (type == BLKIO_STAT_AVG_QUEUE_SIZE) {
		uint64_t sum = blkg->stats.avg_queue_size_sum;
		uint64_t samples = blkg->stats.avg_queue_size_samples;
		if (samples)
			do_div(sum, samples);
		else
			sum = 0;
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1, sum, cb, dev);
	}
	if (type == BLKIO_STAT_GROUP_WAIT_TIME)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
					blkg->stats.group_wait_time, cb, dev);
	if (type == BLKIO_STAT_IDLE_TIME)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
					blkg->stats.idle_time, cb, dev);
	if (type == BLKIO_STAT_EMPTY_TIME)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
					blkg->stats.empty_time, cb, dev);
	if (type == BLKIO_STAT_DEQUEUE)
		return blkio_fill_stat(key_str, MAX_KEY_LEN - 1,
					blkg->stats.dequeue, cb, dev);
#endif

	for (sub_type = BLKIO_STAT_READ; sub_type < BLKIO_STAT_TOTAL;
			sub_type++) {
		blkio_get_key_name(sub_type, dev, key_str, MAX_KEY_LEN, false);
		cb->fill(cb, key_str, blkg->stats.stat_arr[type][sub_type]);
	}
	disk_total = blkg->stats.stat_arr[type][BLKIO_STAT_READ] +
			blkg->stats.stat_arr[type][BLKIO_STAT_WRITE];
	blkio_get_key_name(BLKIO_STAT_TOTAL, dev, key_str, MAX_KEY_LEN, false);
	cb->fill(cb, key_str, disk_total);
	return disk_total;
}

static int blkio_check_dev_num(dev_t dev)
{
	int part = 0;
	struct gendisk *disk;

	disk = get_gendisk(dev, &part);
	if (!disk || part)
		return -ENODEV;

	return 0;
}

static int blkio_policy_parse_and_set(char *buf,
	struct blkio_policy_node *newpn, enum blkio_policy_id plid, int fileid)
{
	char *s[4], *p, *major_s = NULL, *minor_s = NULL;
	int ret;
	unsigned long major, minor, temp;
	int i = 0;
	dev_t dev;
	u64 bps, iops;

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
		return -EINVAL;

	p = strsep(&s[0], ":");
	if (p != NULL)
		major_s = p;
	else
		return -EINVAL;

	minor_s = s[0];
	if (!minor_s)
		return -EINVAL;

	ret = strict_strtoul(major_s, 10, &major);
	if (ret)
		return -EINVAL;

	ret = strict_strtoul(minor_s, 10, &minor);
	if (ret)
		return -EINVAL;

	dev = MKDEV(major, minor);

	ret = blkio_check_dev_num(dev);
	if (ret)
		return ret;

	newpn->dev = dev;

	if (s[1] == NULL)
		return -EINVAL;

	switch (plid) {
	case BLKIO_POLICY_PROP:
		ret = strict_strtoul(s[1], 10, &temp);
		if (ret || (temp < BLKIO_WEIGHT_MIN && temp > 0) ||
			temp > BLKIO_WEIGHT_MAX)
			return -EINVAL;

		newpn->plid = plid;
		newpn->fileid = fileid;
		newpn->val.weight = temp;
		break;
	case BLKIO_POLICY_THROTL:
		switch(fileid) {
		case BLKIO_THROTL_read_bps_device:
		case BLKIO_THROTL_write_bps_device:
			ret = strict_strtoull(s[1], 10, &bps);
			if (ret)
				return -EINVAL;

			newpn->plid = plid;
			newpn->fileid = fileid;
			newpn->val.bps = bps;
			break;
		case BLKIO_THROTL_read_iops_device:
		case BLKIO_THROTL_write_iops_device:
			ret = strict_strtoull(s[1], 10, &iops);
			if (ret)
				return -EINVAL;

			if (iops > THROTL_IOPS_MAX)
				return -EINVAL;

			newpn->plid = plid;
			newpn->fileid = fileid;
			newpn->val.iops = (unsigned int)iops;
			break;
		}
		break;
	default:
		BUG();
	}

	return 0;
}

unsigned int blkcg_get_weight(struct blkio_cgroup *blkcg,
			      dev_t dev)
{
	struct blkio_policy_node *pn;

	pn = blkio_policy_search_node(blkcg, dev, BLKIO_POLICY_PROP,
				BLKIO_PROP_weight_device);
	if (pn)
		return pn->val.weight;
	else
		return blkcg->weight;
}
EXPORT_SYMBOL_GPL(blkcg_get_weight);

uint64_t blkcg_get_read_bps(struct blkio_cgroup *blkcg, dev_t dev)
{
	struct blkio_policy_node *pn;

	pn = blkio_policy_search_node(blkcg, dev, BLKIO_POLICY_THROTL,
				BLKIO_THROTL_read_bps_device);
	if (pn)
		return pn->val.bps;
	else
		return -1;
}

uint64_t blkcg_get_write_bps(struct blkio_cgroup *blkcg, dev_t dev)
{
	struct blkio_policy_node *pn;
	pn = blkio_policy_search_node(blkcg, dev, BLKIO_POLICY_THROTL,
				BLKIO_THROTL_write_bps_device);
	if (pn)
		return pn->val.bps;
	else
		return -1;
}

unsigned int blkcg_get_read_iops(struct blkio_cgroup *blkcg, dev_t dev)
{
	struct blkio_policy_node *pn;

	pn = blkio_policy_search_node(blkcg, dev, BLKIO_POLICY_THROTL,
				BLKIO_THROTL_read_iops_device);
	if (pn)
		return pn->val.iops;
	else
		return -1;
}

unsigned int blkcg_get_write_iops(struct blkio_cgroup *blkcg, dev_t dev)
{
	struct blkio_policy_node *pn;
	pn = blkio_policy_search_node(blkcg, dev, BLKIO_POLICY_THROTL,
				BLKIO_THROTL_write_iops_device);
	if (pn)
		return pn->val.iops;
	else
		return -1;
}

/* Checks whether user asked for deleting a policy rule */
static bool blkio_delete_rule_command(struct blkio_policy_node *pn)
{
	switch(pn->plid) {
	case BLKIO_POLICY_PROP:
		if (pn->val.weight == 0)
			return 1;
		break;
	case BLKIO_POLICY_THROTL:
		switch(pn->fileid) {
		case BLKIO_THROTL_read_bps_device:
		case BLKIO_THROTL_write_bps_device:
			if (pn->val.bps == 0)
				return 1;
			break;
		case BLKIO_THROTL_read_iops_device:
		case BLKIO_THROTL_write_iops_device:
			if (pn->val.iops == 0)
				return 1;
		}
		break;
	default:
		BUG();
	}

	return 0;
}

static void blkio_update_policy_rule(struct blkio_policy_node *oldpn,
					struct blkio_policy_node *newpn)
{
	switch(oldpn->plid) {
	case BLKIO_POLICY_PROP:
		oldpn->val.weight = newpn->val.weight;
		break;
	case BLKIO_POLICY_THROTL:
		switch(newpn->fileid) {
		case BLKIO_THROTL_read_bps_device:
		case BLKIO_THROTL_write_bps_device:
			oldpn->val.bps = newpn->val.bps;
			break;
		case BLKIO_THROTL_read_iops_device:
		case BLKIO_THROTL_write_iops_device:
			oldpn->val.iops = newpn->val.iops;
		}
		break;
	default:
		BUG();
	}
}

/*
 * Some rules/values in blkg have changed. Propagate those to respective
 * policies.
 */
static void blkio_update_blkg_policy(struct blkio_cgroup *blkcg,
		struct blkio_group *blkg, struct blkio_policy_node *pn)
{
	unsigned int weight, iops;
	u64 bps;

	switch(pn->plid) {
	case BLKIO_POLICY_PROP:
		weight = pn->val.weight ? pn->val.weight :
				blkcg->weight;
		blkio_update_group_weight(blkg, weight);
		break;
	case BLKIO_POLICY_THROTL:
		switch(pn->fileid) {
		case BLKIO_THROTL_read_bps_device:
		case BLKIO_THROTL_write_bps_device:
			bps = pn->val.bps ? pn->val.bps : (-1);
			blkio_update_group_bps(blkg, bps, pn->fileid);
			break;
		case BLKIO_THROTL_read_iops_device:
		case BLKIO_THROTL_write_iops_device:
			iops = pn->val.iops ? pn->val.iops : (-1);
			blkio_update_group_iops(blkg, iops, pn->fileid);
			break;
		}
		break;
	default:
		BUG();
	}
}

/*
 * A policy node rule has been updated. Propagate this update to all the
 * block groups which might be affected by this update.
 */
static void blkio_update_policy_node_blkg(struct blkio_cgroup *blkcg,
				struct blkio_policy_node *pn)
{
	struct blkio_group *blkg;
	struct hlist_node *n;

	spin_lock(&blkio_list_lock);
	spin_lock_irq(&blkcg->lock);

	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node) {
		if (pn->dev != blkg->dev || pn->plid != blkg->plid)
			continue;
		blkio_update_blkg_policy(blkcg, blkg, pn);
	}

	spin_unlock_irq(&blkcg->lock);
	spin_unlock(&blkio_list_lock);
}

static int blkiocg_file_write(struct cgroup *cgrp, struct cftype *cft,
 				       const char *buffer)
{
	int ret = 0;
	char *buf;
	struct blkio_policy_node *newpn, *pn;
	struct blkio_cgroup *blkcg;
	int keep_newpn = 0;
	enum blkio_policy_id plid = BLKIOFILE_POLICY(cft->private);
	int fileid = BLKIOFILE_ATTR(cft->private);

	buf = kstrdup(buffer, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	newpn = kzalloc(sizeof(*newpn), GFP_KERNEL);
	if (!newpn) {
		ret = -ENOMEM;
		goto free_buf;
	}

	ret = blkio_policy_parse_and_set(buf, newpn, plid, fileid);
	if (ret)
		goto free_newpn;

	blkcg = cgroup_to_blkio_cgroup(cgrp);

	spin_lock_irq(&blkcg->lock);

	pn = blkio_policy_search_node(blkcg, newpn->dev, plid, fileid);
	if (!pn) {
		if (!blkio_delete_rule_command(newpn)) {
			blkio_policy_insert_node(blkcg, newpn);
			keep_newpn = 1;
		}
		spin_unlock_irq(&blkcg->lock);
		goto update_io_group;
	}

	if (blkio_delete_rule_command(newpn)) {
		blkio_policy_delete_node(pn);
		spin_unlock_irq(&blkcg->lock);
		goto update_io_group;
	}
	spin_unlock_irq(&blkcg->lock);

	blkio_update_policy_rule(pn, newpn);

update_io_group:
	blkio_update_policy_node_blkg(blkcg, newpn);

free_newpn:
	if (!keep_newpn)
		kfree(newpn);
free_buf:
	kfree(buf);
	return ret;
}

static void
blkio_print_policy_node(struct seq_file *m, struct blkio_policy_node *pn)
{
	switch(pn->plid) {
		case BLKIO_POLICY_PROP:
			if (pn->fileid == BLKIO_PROP_weight_device)
				seq_printf(m, "%u:%u\t%u\n", MAJOR(pn->dev),
					MINOR(pn->dev), pn->val.weight);
			break;
		case BLKIO_POLICY_THROTL:
			switch(pn->fileid) {
			case BLKIO_THROTL_read_bps_device:
			case BLKIO_THROTL_write_bps_device:
				seq_printf(m, "%u:%u\t%llu\n", MAJOR(pn->dev),
					MINOR(pn->dev), pn->val.bps);
				break;
			case BLKIO_THROTL_read_iops_device:
			case BLKIO_THROTL_write_iops_device:
				seq_printf(m, "%u:%u\t%u\n", MAJOR(pn->dev),
					MINOR(pn->dev), pn->val.iops);
				break;
			}
			break;
		default:
			BUG();
	}
}

/* cgroup files which read their data from policy nodes end up here */
static void blkio_read_policy_node_files(struct cftype *cft,
			struct blkio_cgroup *blkcg, struct seq_file *m)
{
	struct blkio_policy_node *pn;

	if (!list_empty(&blkcg->policy_list)) {
		spin_lock_irq(&blkcg->lock);
		list_for_each_entry(pn, &blkcg->policy_list, node) {
			if (!pn_matches_cftype(cft, pn))
				continue;
			blkio_print_policy_node(m, pn);
		}
		spin_unlock_irq(&blkcg->lock);
	}
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
			blkio_read_policy_node_files(cft, blkcg, m);
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
			blkio_read_policy_node_files(cft, blkcg, m);
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
		if (blkg->dev) {
			if (!cftype_blkg_same_policy(cft, blkg))
				continue;
			if (pcpu)
				cgroup_total += blkio_get_stat_cpu(blkg, cb,
						blkg->dev, type);
			else {
				spin_lock_irq(&blkg->stats_lock);
				cgroup_total += blkio_get_stat(blkg, cb,
						blkg->dev, type);
				spin_unlock_irq(&blkg->stats_lock);
			}
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

static int blkio_weight_write(struct blkio_cgroup *blkcg, u64 val)
{
	struct blkio_group *blkg;
	struct hlist_node *n;
	struct blkio_policy_node *pn;

	if (val < BLKIO_WEIGHT_MIN || val > BLKIO_WEIGHT_MAX)
		return -EINVAL;

	spin_lock(&blkio_list_lock);
	spin_lock_irq(&blkcg->lock);
	blkcg->weight = (unsigned int)val;

	hlist_for_each_entry(blkg, n, &blkcg->blkg_list, blkcg_node) {
		pn = blkio_policy_search_node(blkcg, blkg->dev,
				BLKIO_POLICY_PROP, BLKIO_PROP_weight_device);
		if (pn)
			continue;

		blkio_update_group_weight(blkg, blkcg->weight);
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
			return blkio_weight_write(blkcg, val);
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

static void blkiocg_destroy(struct cgroup_subsys *subsys, struct cgroup *cgroup)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgroup);
	unsigned long flags;
	struct blkio_group *blkg;
	void *key;
	struct blkio_policy_type *blkiop;
	struct blkio_policy_node *pn, *pntmp;

	rcu_read_lock();
	do {
		spin_lock_irqsave(&blkcg->lock, flags);

		if (hlist_empty(&blkcg->blkg_list)) {
			spin_unlock_irqrestore(&blkcg->lock, flags);
			break;
		}

		blkg = hlist_entry(blkcg->blkg_list.first, struct blkio_group,
					blkcg_node);
		key = rcu_dereference(blkg->key);
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
			blkiop->ops.blkio_unlink_group_fn(key, blkg);
		}
		spin_unlock(&blkio_list_lock);
	} while (1);

	list_for_each_entry_safe(pn, pntmp, &blkcg->policy_list, node) {
		blkio_policy_delete_node(pn);
		kfree(pn);
	}

	free_css_id(&blkio_subsys, &blkcg->css);
	rcu_read_unlock();
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

	INIT_LIST_HEAD(&blkcg->policy_list);
	return &blkcg->css;
}

/*
 * We cannot support shared io contexts, as we have no mean to support
 * two tasks with the same ioc in two different groups without major rework
 * of the main cic data structures.  For now we allow a task to change
 * its cgroup only if it's the only owner of its ioc.
 */
static int blkiocg_can_attach_task(struct cgroup *cgrp, struct task_struct *tsk)
{
	struct io_context *ioc;
	int ret = 0;

	/* task_lock() is needed to avoid races with exit_io_context() */
	task_lock(tsk);
	ioc = tsk->io_context;
	if (ioc && atomic_read(&ioc->nr_tasks) > 1)
		ret = -EINVAL;
	task_unlock(tsk);

	return ret;
}

static void blkiocg_attach_task(struct cgroup *cgrp, struct task_struct *tsk)
{
	struct io_context *ioc;

	task_lock(tsk);
	ioc = tsk->io_context;
	if (ioc)
		ioc->cgroup_changed = 1;
	task_unlock(tsk);
}

void blkio_policy_register(struct blkio_policy_type *blkiop)
{
	spin_lock(&blkio_list_lock);
	list_add_tail(&blkiop->list, &blkio_list);
	spin_unlock(&blkio_list_lock);
}
EXPORT_SYMBOL_GPL(blkio_policy_register);

void blkio_policy_unregister(struct blkio_policy_type *blkiop)
{
	spin_lock(&blkio_list_lock);
	list_del_init(&blkiop->list);
	spin_unlock(&blkio_list_lock);
}
EXPORT_SYMBOL_GPL(blkio_policy_unregister);

static int __init init_cgroup_blkio(void)
{
	return cgroup_load_subsys(&blkio_subsys);
}

static void __exit exit_cgroup_blkio(void)
{
	cgroup_unload_subsys(&blkio_subsys);
}

module_init(init_cgroup_blkio);
module_exit(exit_cgroup_blkio);
MODULE_LICENSE("GPL");
