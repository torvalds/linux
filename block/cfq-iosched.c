/*
 *  CFQ, or complete fairness queueing, disk scheduler.
 *
 *  Based on ideas from a previously unfinished io
 *  scheduler (round robin per-process disk scheduling) and Andrea Arcangeli.
 *
 *  Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/jiffies.h>
#include <linux/rbtree.h>
#include <linux/ioprio.h>
#include <linux/blktrace_api.h>
#include <linux/blk-cgroup.h>
#include "blk.h"

/*
 * tunables
 */
/* max queue in one round of service */
static const int cfq_quantum = 8;
static const int cfq_fifo_expire[2] = { HZ / 4, HZ / 8 };
/* maximum backwards seek, in KiB */
static const int cfq_back_max = 16 * 1024;
/* penalty of a backwards seek */
static const int cfq_back_penalty = 2;
static const int cfq_slice_sync = HZ / 10;
static int cfq_slice_async = HZ / 25;
static const int cfq_slice_async_rq = 2;
static int cfq_slice_idle = HZ / 125;
static int cfq_group_idle = HZ / 125;
static const int cfq_target_latency = HZ * 3/10; /* 300 ms */
static const int cfq_hist_divisor = 4;

/*
 * offset from end of service tree
 */
#define CFQ_IDLE_DELAY		(HZ / 5)

/*
 * below this threshold, we consider thinktime immediate
 */
#define CFQ_MIN_TT		(2)

#define CFQ_SLICE_SCALE		(5)
#define CFQ_HW_QUEUE_MIN	(5)
#define CFQ_SERVICE_SHIFT       12

#define CFQQ_SEEK_THR		(sector_t)(8 * 100)
#define CFQQ_CLOSE_THR		(sector_t)(8 * 1024)
#define CFQQ_SECT_THR_NONROT	(sector_t)(2 * 32)
#define CFQQ_SEEKY(cfqq)	(hweight32(cfqq->seek_history) > 32/8)

#define RQ_CIC(rq)		icq_to_cic((rq)->elv.icq)
#define RQ_CFQQ(rq)		(struct cfq_queue *) ((rq)->elv.priv[0])
#define RQ_CFQG(rq)		(struct cfq_group *) ((rq)->elv.priv[1])

static struct kmem_cache *cfq_pool;

#define CFQ_PRIO_LISTS		IOPRIO_BE_NR
#define cfq_class_idle(cfqq)	((cfqq)->ioprio_class == IOPRIO_CLASS_IDLE)
#define cfq_class_rt(cfqq)	((cfqq)->ioprio_class == IOPRIO_CLASS_RT)

#define sample_valid(samples)	((samples) > 80)
#define rb_entry_cfqg(node)	rb_entry((node), struct cfq_group, rb_node)

/* blkio-related constants */
#define CFQ_WEIGHT_LEGACY_MIN	10
#define CFQ_WEIGHT_LEGACY_DFL	500
#define CFQ_WEIGHT_LEGACY_MAX	1000

struct cfq_ttime {
	unsigned long last_end_request;

	unsigned long ttime_total;
	unsigned long ttime_samples;
	unsigned long ttime_mean;
};

/*
 * Most of our rbtree usage is for sorting with min extraction, so
 * if we cache the leftmost node we don't have to walk down the tree
 * to find it. Idea borrowed from Ingo Molnars CFS scheduler. We should
 * move this into the elevator for the rq sorting as well.
 */
struct cfq_rb_root {
	struct rb_root rb;
	struct rb_node *left;
	unsigned count;
	u64 min_vdisktime;
	struct cfq_ttime ttime;
};
#define CFQ_RB_ROOT	(struct cfq_rb_root) { .rb = RB_ROOT, \
			.ttime = {.last_end_request = jiffies,},}

/*
 * Per process-grouping structure
 */
struct cfq_queue {
	/* reference count */
	int ref;
	/* various state flags, see below */
	unsigned int flags;
	/* parent cfq_data */
	struct cfq_data *cfqd;
	/* service_tree member */
	struct rb_node rb_node;
	/* service_tree key */
	unsigned long rb_key;
	/* prio tree member */
	struct rb_node p_node;
	/* prio tree root we belong to, if any */
	struct rb_root *p_root;
	/* sorted list of pending requests */
	struct rb_root sort_list;
	/* if fifo isn't expired, next request to serve */
	struct request *next_rq;
	/* requests queued in sort_list */
	int queued[2];
	/* currently allocated requests */
	int allocated[2];
	/* fifo list of requests in sort_list */
	struct list_head fifo;

	/* time when queue got scheduled in to dispatch first request. */
	unsigned long dispatch_start;
	unsigned int allocated_slice;
	unsigned int slice_dispatch;
	/* time when first request from queue completed and slice started. */
	unsigned long slice_start;
	unsigned long slice_end;
	long slice_resid;

	/* pending priority requests */
	int prio_pending;
	/* number of requests that are on the dispatch list or inside driver */
	int dispatched;

	/* io prio of this group */
	unsigned short ioprio, org_ioprio;
	unsigned short ioprio_class;

	pid_t pid;

	u32 seek_history;
	sector_t last_request_pos;

	struct cfq_rb_root *service_tree;
	struct cfq_queue *new_cfqq;
	struct cfq_group *cfqg;
	/* Number of sectors dispatched from queue in single dispatch round */
	unsigned long nr_sectors;
};

/*
 * First index in the service_trees.
 * IDLE is handled separately, so it has negative index
 */
enum wl_class_t {
	BE_WORKLOAD = 0,
	RT_WORKLOAD = 1,
	IDLE_WORKLOAD = 2,
	CFQ_PRIO_NR,
};

/*
 * Second index in the service_trees.
 */
enum wl_type_t {
	ASYNC_WORKLOAD = 0,
	SYNC_NOIDLE_WORKLOAD = 1,
	SYNC_WORKLOAD = 2
};

struct cfqg_stats {
#ifdef CONFIG_CFQ_GROUP_IOSCHED
	/* number of ios merged */
	struct blkg_rwstat		merged;
	/* total time spent on device in ns, may not be accurate w/ queueing */
	struct blkg_rwstat		service_time;
	/* total time spent waiting in scheduler queue in ns */
	struct blkg_rwstat		wait_time;
	/* number of IOs queued up */
	struct blkg_rwstat		queued;
	/* total disk time and nr sectors dispatched by this group */
	struct blkg_stat		time;
#ifdef CONFIG_DEBUG_BLK_CGROUP
	/* time not charged to this cgroup */
	struct blkg_stat		unaccounted_time;
	/* sum of number of ios queued across all samples */
	struct blkg_stat		avg_queue_size_sum;
	/* count of samples taken for average */
	struct blkg_stat		avg_queue_size_samples;
	/* how many times this group has been removed from service tree */
	struct blkg_stat		dequeue;
	/* total time spent waiting for it to be assigned a timeslice. */
	struct blkg_stat		group_wait_time;
	/* time spent idling for this blkcg_gq */
	struct blkg_stat		idle_time;
	/* total time with empty current active q with other requests queued */
	struct blkg_stat		empty_time;
	/* fields after this shouldn't be cleared on stat reset */
	uint64_t			start_group_wait_time;
	uint64_t			start_idle_time;
	uint64_t			start_empty_time;
	uint16_t			flags;
#endif	/* CONFIG_DEBUG_BLK_CGROUP */
#endif	/* CONFIG_CFQ_GROUP_IOSCHED */
};

/* Per-cgroup data */
struct cfq_group_data {
	/* must be the first member */
	struct blkcg_policy_data cpd;

	unsigned int weight;
	unsigned int leaf_weight;
};

/* This is per cgroup per device grouping structure */
struct cfq_group {
	/* must be the first member */
	struct blkg_policy_data pd;

	/* group service_tree member */
	struct rb_node rb_node;

	/* group service_tree key */
	u64 vdisktime;

	/*
	 * The number of active cfqgs and sum of their weights under this
	 * cfqg.  This covers this cfqg's leaf_weight and all children's
	 * weights, but does not cover weights of further descendants.
	 *
	 * If a cfqg is on the service tree, it's active.  An active cfqg
	 * also activates its parent and contributes to the children_weight
	 * of the parent.
	 */
	int nr_active;
	unsigned int children_weight;

	/*
	 * vfraction is the fraction of vdisktime that the tasks in this
	 * cfqg are entitled to.  This is determined by compounding the
	 * ratios walking up from this cfqg to the root.
	 *
	 * It is in fixed point w/ CFQ_SERVICE_SHIFT and the sum of all
	 * vfractions on a service tree is approximately 1.  The sum may
	 * deviate a bit due to rounding errors and fluctuations caused by
	 * cfqgs entering and leaving the service tree.
	 */
	unsigned int vfraction;

	/*
	 * There are two weights - (internal) weight is the weight of this
	 * cfqg against the sibling cfqgs.  leaf_weight is the wight of
	 * this cfqg against the child cfqgs.  For the root cfqg, both
	 * weights are kept in sync for backward compatibility.
	 */
	unsigned int weight;
	unsigned int new_weight;
	unsigned int dev_weight;

	unsigned int leaf_weight;
	unsigned int new_leaf_weight;
	unsigned int dev_leaf_weight;

	/* number of cfqq currently on this group */
	int nr_cfqq;

	/*
	 * Per group busy queues average. Useful for workload slice calc. We
	 * create the array for each prio class but at run time it is used
	 * only for RT and BE class and slot for IDLE class remains unused.
	 * This is primarily done to avoid confusion and a gcc warning.
	 */
	unsigned int busy_queues_avg[CFQ_PRIO_NR];
	/*
	 * rr lists of queues with requests. We maintain service trees for
	 * RT and BE classes. These trees are subdivided in subclasses
	 * of SYNC, SYNC_NOIDLE and ASYNC based on workload type. For IDLE
	 * class there is no subclassification and all the cfq queues go on
	 * a single tree service_tree_idle.
	 * Counts are embedded in the cfq_rb_root
	 */
	struct cfq_rb_root service_trees[2][3];
	struct cfq_rb_root service_tree_idle;

	unsigned long saved_wl_slice;
	enum wl_type_t saved_wl_type;
	enum wl_class_t saved_wl_class;

	/* number of requests that are on the dispatch list or inside driver */
	int dispatched;
	struct cfq_ttime ttime;
	struct cfqg_stats stats;	/* stats for this cfqg */

	/* async queue for each priority case */
	struct cfq_queue *async_cfqq[2][IOPRIO_BE_NR];
	struct cfq_queue *async_idle_cfqq;

};

struct cfq_io_cq {
	struct io_cq		icq;		/* must be the first member */
	struct cfq_queue	*cfqq[2];
	struct cfq_ttime	ttime;
	int			ioprio;		/* the current ioprio */
#ifdef CONFIG_CFQ_GROUP_IOSCHED
	uint64_t		blkcg_serial_nr; /* the current blkcg serial */
#endif
};

/*
 * Per block device queue structure
 */
struct cfq_data {
	struct request_queue *queue;
	/* Root service tree for cfq_groups */
	struct cfq_rb_root grp_service_tree;
	struct cfq_group *root_group;

	/*
	 * The priority currently being served
	 */
	enum wl_class_t serving_wl_class;
	enum wl_type_t serving_wl_type;
	unsigned long workload_expires;
	struct cfq_group *serving_group;

	/*
	 * Each priority tree is sorted by next_request position.  These
	 * trees are used when determining if two or more queues are
	 * interleaving requests (see cfq_close_cooperator).
	 */
	struct rb_root prio_trees[CFQ_PRIO_LISTS];

	unsigned int busy_queues;
	unsigned int busy_sync_queues;

	int rq_in_driver;
	int rq_in_flight[2];

	/*
	 * queue-depth detection
	 */
	int rq_queued;
	int hw_tag;
	/*
	 * hw_tag can be
	 * -1 => indeterminate, (cfq will behave as if NCQ is present, to allow better detection)
	 *  1 => NCQ is present (hw_tag_est_depth is the estimated max depth)
	 *  0 => no NCQ
	 */
	int hw_tag_est_depth;
	unsigned int hw_tag_samples;

	/*
	 * idle window management
	 */
	struct timer_list idle_slice_timer;
	struct work_struct unplug_work;

	struct cfq_queue *active_queue;
	struct cfq_io_cq *active_cic;

	sector_t last_position;

	/*
	 * tunables, see top of file
	 */
	unsigned int cfq_quantum;
	unsigned int cfq_fifo_expire[2];
	unsigned int cfq_back_penalty;
	unsigned int cfq_back_max;
	unsigned int cfq_slice[2];
	unsigned int cfq_slice_async_rq;
	unsigned int cfq_slice_idle;
	unsigned int cfq_group_idle;
	unsigned int cfq_latency;
	unsigned int cfq_target_latency;

	/*
	 * Fallback dummy cfqq for extreme OOM conditions
	 */
	struct cfq_queue oom_cfqq;

	unsigned long last_delayed_sync;
};

static struct cfq_group *cfq_get_next_cfqg(struct cfq_data *cfqd);
static void cfq_put_queue(struct cfq_queue *cfqq);

static struct cfq_rb_root *st_for(struct cfq_group *cfqg,
					    enum wl_class_t class,
					    enum wl_type_t type)
{
	if (!cfqg)
		return NULL;

	if (class == IDLE_WORKLOAD)
		return &cfqg->service_tree_idle;

	return &cfqg->service_trees[class][type];
}

enum cfqq_state_flags {
	CFQ_CFQQ_FLAG_on_rr = 0,	/* on round-robin busy list */
	CFQ_CFQQ_FLAG_wait_request,	/* waiting for a request */
	CFQ_CFQQ_FLAG_must_dispatch,	/* must be allowed a dispatch */
	CFQ_CFQQ_FLAG_must_alloc_slice,	/* per-slice must_alloc flag */
	CFQ_CFQQ_FLAG_fifo_expire,	/* FIFO checked in this slice */
	CFQ_CFQQ_FLAG_idle_window,	/* slice idling enabled */
	CFQ_CFQQ_FLAG_prio_changed,	/* task priority has changed */
	CFQ_CFQQ_FLAG_slice_new,	/* no requests dispatched in slice */
	CFQ_CFQQ_FLAG_sync,		/* synchronous queue */
	CFQ_CFQQ_FLAG_coop,		/* cfqq is shared */
	CFQ_CFQQ_FLAG_split_coop,	/* shared cfqq will be splitted */
	CFQ_CFQQ_FLAG_deep,		/* sync cfqq experienced large depth */
	CFQ_CFQQ_FLAG_wait_busy,	/* Waiting for next request */
};

#define CFQ_CFQQ_FNS(name)						\
static inline void cfq_mark_cfqq_##name(struct cfq_queue *cfqq)		\
{									\
	(cfqq)->flags |= (1 << CFQ_CFQQ_FLAG_##name);			\
}									\
static inline void cfq_clear_cfqq_##name(struct cfq_queue *cfqq)	\
{									\
	(cfqq)->flags &= ~(1 << CFQ_CFQQ_FLAG_##name);			\
}									\
static inline int cfq_cfqq_##name(const struct cfq_queue *cfqq)		\
{									\
	return ((cfqq)->flags & (1 << CFQ_CFQQ_FLAG_##name)) != 0;	\
}

CFQ_CFQQ_FNS(on_rr);
CFQ_CFQQ_FNS(wait_request);
CFQ_CFQQ_FNS(must_dispatch);
CFQ_CFQQ_FNS(must_alloc_slice);
CFQ_CFQQ_FNS(fifo_expire);
CFQ_CFQQ_FNS(idle_window);
CFQ_CFQQ_FNS(prio_changed);
CFQ_CFQQ_FNS(slice_new);
CFQ_CFQQ_FNS(sync);
CFQ_CFQQ_FNS(coop);
CFQ_CFQQ_FNS(split_coop);
CFQ_CFQQ_FNS(deep);
CFQ_CFQQ_FNS(wait_busy);
#undef CFQ_CFQQ_FNS

#if defined(CONFIG_CFQ_GROUP_IOSCHED) && defined(CONFIG_DEBUG_BLK_CGROUP)

/* cfqg stats flags */
enum cfqg_stats_flags {
	CFQG_stats_waiting = 0,
	CFQG_stats_idling,
	CFQG_stats_empty,
};

#define CFQG_FLAG_FNS(name)						\
static inline void cfqg_stats_mark_##name(struct cfqg_stats *stats)	\
{									\
	stats->flags |= (1 << CFQG_stats_##name);			\
}									\
static inline void cfqg_stats_clear_##name(struct cfqg_stats *stats)	\
{									\
	stats->flags &= ~(1 << CFQG_stats_##name);			\
}									\
static inline int cfqg_stats_##name(struct cfqg_stats *stats)		\
{									\
	return (stats->flags & (1 << CFQG_stats_##name)) != 0;		\
}									\

CFQG_FLAG_FNS(waiting)
CFQG_FLAG_FNS(idling)
CFQG_FLAG_FNS(empty)
#undef CFQG_FLAG_FNS

/* This should be called with the queue_lock held. */
static void cfqg_stats_update_group_wait_time(struct cfqg_stats *stats)
{
	unsigned long long now;

	if (!cfqg_stats_waiting(stats))
		return;

	now = sched_clock();
	if (time_after64(now, stats->start_group_wait_time))
		blkg_stat_add(&stats->group_wait_time,
			      now - stats->start_group_wait_time);
	cfqg_stats_clear_waiting(stats);
}

/* This should be called with the queue_lock held. */
static void cfqg_stats_set_start_group_wait_time(struct cfq_group *cfqg,
						 struct cfq_group *curr_cfqg)
{
	struct cfqg_stats *stats = &cfqg->stats;

	if (cfqg_stats_waiting(stats))
		return;
	if (cfqg == curr_cfqg)
		return;
	stats->start_group_wait_time = sched_clock();
	cfqg_stats_mark_waiting(stats);
}

/* This should be called with the queue_lock held. */
static void cfqg_stats_end_empty_time(struct cfqg_stats *stats)
{
	unsigned long long now;

	if (!cfqg_stats_empty(stats))
		return;

	now = sched_clock();
	if (time_after64(now, stats->start_empty_time))
		blkg_stat_add(&stats->empty_time,
			      now - stats->start_empty_time);
	cfqg_stats_clear_empty(stats);
}

static void cfqg_stats_update_dequeue(struct cfq_group *cfqg)
{
	blkg_stat_add(&cfqg->stats.dequeue, 1);
}

static void cfqg_stats_set_start_empty_time(struct cfq_group *cfqg)
{
	struct cfqg_stats *stats = &cfqg->stats;

	if (blkg_rwstat_total(&stats->queued))
		return;

	/*
	 * group is already marked empty. This can happen if cfqq got new
	 * request in parent group and moved to this group while being added
	 * to service tree. Just ignore the event and move on.
	 */
	if (cfqg_stats_empty(stats))
		return;

	stats->start_empty_time = sched_clock();
	cfqg_stats_mark_empty(stats);
}

static void cfqg_stats_update_idle_time(struct cfq_group *cfqg)
{
	struct cfqg_stats *stats = &cfqg->stats;

	if (cfqg_stats_idling(stats)) {
		unsigned long long now = sched_clock();

		if (time_after64(now, stats->start_idle_time))
			blkg_stat_add(&stats->idle_time,
				      now - stats->start_idle_time);
		cfqg_stats_clear_idling(stats);
	}
}

static void cfqg_stats_set_start_idle_time(struct cfq_group *cfqg)
{
	struct cfqg_stats *stats = &cfqg->stats;

	BUG_ON(cfqg_stats_idling(stats));

	stats->start_idle_time = sched_clock();
	cfqg_stats_mark_idling(stats);
}

static void cfqg_stats_update_avg_queue_size(struct cfq_group *cfqg)
{
	struct cfqg_stats *stats = &cfqg->stats;

	blkg_stat_add(&stats->avg_queue_size_sum,
		      blkg_rwstat_total(&stats->queued));
	blkg_stat_add(&stats->avg_queue_size_samples, 1);
	cfqg_stats_update_group_wait_time(stats);
}

#else	/* CONFIG_CFQ_GROUP_IOSCHED && CONFIG_DEBUG_BLK_CGROUP */

static inline void cfqg_stats_set_start_group_wait_time(struct cfq_group *cfqg, struct cfq_group *curr_cfqg) { }
static inline void cfqg_stats_end_empty_time(struct cfqg_stats *stats) { }
static inline void cfqg_stats_update_dequeue(struct cfq_group *cfqg) { }
static inline void cfqg_stats_set_start_empty_time(struct cfq_group *cfqg) { }
static inline void cfqg_stats_update_idle_time(struct cfq_group *cfqg) { }
static inline void cfqg_stats_set_start_idle_time(struct cfq_group *cfqg) { }
static inline void cfqg_stats_update_avg_queue_size(struct cfq_group *cfqg) { }

#endif	/* CONFIG_CFQ_GROUP_IOSCHED && CONFIG_DEBUG_BLK_CGROUP */

#ifdef CONFIG_CFQ_GROUP_IOSCHED

static inline struct cfq_group *pd_to_cfqg(struct blkg_policy_data *pd)
{
	return pd ? container_of(pd, struct cfq_group, pd) : NULL;
}

static struct cfq_group_data
*cpd_to_cfqgd(struct blkcg_policy_data *cpd)
{
	return cpd ? container_of(cpd, struct cfq_group_data, cpd) : NULL;
}

static inline struct blkcg_gq *cfqg_to_blkg(struct cfq_group *cfqg)
{
	return pd_to_blkg(&cfqg->pd);
}

static struct blkcg_policy blkcg_policy_cfq;

static inline struct cfq_group *blkg_to_cfqg(struct blkcg_gq *blkg)
{
	return pd_to_cfqg(blkg_to_pd(blkg, &blkcg_policy_cfq));
}

static struct cfq_group_data *blkcg_to_cfqgd(struct blkcg *blkcg)
{
	return cpd_to_cfqgd(blkcg_to_cpd(blkcg, &blkcg_policy_cfq));
}

static inline struct cfq_group *cfqg_parent(struct cfq_group *cfqg)
{
	struct blkcg_gq *pblkg = cfqg_to_blkg(cfqg)->parent;

	return pblkg ? blkg_to_cfqg(pblkg) : NULL;
}

static inline void cfqg_get(struct cfq_group *cfqg)
{
	return blkg_get(cfqg_to_blkg(cfqg));
}

static inline void cfqg_put(struct cfq_group *cfqg)
{
	return blkg_put(cfqg_to_blkg(cfqg));
}

#define cfq_log_cfqq(cfqd, cfqq, fmt, args...)	do {			\
	char __pbuf[128];						\
									\
	blkg_path(cfqg_to_blkg((cfqq)->cfqg), __pbuf, sizeof(__pbuf));	\
	blk_add_trace_msg((cfqd)->queue, "cfq%d%c%c %s " fmt, (cfqq)->pid, \
			cfq_cfqq_sync((cfqq)) ? 'S' : 'A',		\
			cfqq_type((cfqq)) == SYNC_NOIDLE_WORKLOAD ? 'N' : ' ',\
			  __pbuf, ##args);				\
} while (0)

#define cfq_log_cfqg(cfqd, cfqg, fmt, args...)	do {			\
	char __pbuf[128];						\
									\
	blkg_path(cfqg_to_blkg(cfqg), __pbuf, sizeof(__pbuf));		\
	blk_add_trace_msg((cfqd)->queue, "%s " fmt, __pbuf, ##args);	\
} while (0)

static inline void cfqg_stats_update_io_add(struct cfq_group *cfqg,
					    struct cfq_group *curr_cfqg, int rw)
{
	blkg_rwstat_add(&cfqg->stats.queued, rw, 1);
	cfqg_stats_end_empty_time(&cfqg->stats);
	cfqg_stats_set_start_group_wait_time(cfqg, curr_cfqg);
}

static inline void cfqg_stats_update_timeslice_used(struct cfq_group *cfqg,
			unsigned long time, unsigned long unaccounted_time)
{
	blkg_stat_add(&cfqg->stats.time, time);
#ifdef CONFIG_DEBUG_BLK_CGROUP
	blkg_stat_add(&cfqg->stats.unaccounted_time, unaccounted_time);
#endif
}

static inline void cfqg_stats_update_io_remove(struct cfq_group *cfqg, int rw)
{
	blkg_rwstat_add(&cfqg->stats.queued, rw, -1);
}

static inline void cfqg_stats_update_io_merged(struct cfq_group *cfqg, int rw)
{
	blkg_rwstat_add(&cfqg->stats.merged, rw, 1);
}

static inline void cfqg_stats_update_completion(struct cfq_group *cfqg,
			uint64_t start_time, uint64_t io_start_time, int rw)
{
	struct cfqg_stats *stats = &cfqg->stats;
	unsigned long long now = sched_clock();

	if (time_after64(now, io_start_time))
		blkg_rwstat_add(&stats->service_time, rw, now - io_start_time);
	if (time_after64(io_start_time, start_time))
		blkg_rwstat_add(&stats->wait_time, rw,
				io_start_time - start_time);
}

/* @stats = 0 */
static void cfqg_stats_reset(struct cfqg_stats *stats)
{
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
}

/* @to += @from */
static void cfqg_stats_add_aux(struct cfqg_stats *to, struct cfqg_stats *from)
{
	/* queued stats shouldn't be cleared */
	blkg_rwstat_add_aux(&to->merged, &from->merged);
	blkg_rwstat_add_aux(&to->service_time, &from->service_time);
	blkg_rwstat_add_aux(&to->wait_time, &from->wait_time);
	blkg_stat_add_aux(&from->time, &from->time);
#ifdef CONFIG_DEBUG_BLK_CGROUP
	blkg_stat_add_aux(&to->unaccounted_time, &from->unaccounted_time);
	blkg_stat_add_aux(&to->avg_queue_size_sum, &from->avg_queue_size_sum);
	blkg_stat_add_aux(&to->avg_queue_size_samples, &from->avg_queue_size_samples);
	blkg_stat_add_aux(&to->dequeue, &from->dequeue);
	blkg_stat_add_aux(&to->group_wait_time, &from->group_wait_time);
	blkg_stat_add_aux(&to->idle_time, &from->idle_time);
	blkg_stat_add_aux(&to->empty_time, &from->empty_time);
#endif
}

/*
 * Transfer @cfqg's stats to its parent's aux counts so that the ancestors'
 * recursive stats can still account for the amount used by this cfqg after
 * it's gone.
 */
static void cfqg_stats_xfer_dead(struct cfq_group *cfqg)
{
	struct cfq_group *parent = cfqg_parent(cfqg);

	lockdep_assert_held(cfqg_to_blkg(cfqg)->q->queue_lock);

	if (unlikely(!parent))
		return;

	cfqg_stats_add_aux(&parent->stats, &cfqg->stats);
	cfqg_stats_reset(&cfqg->stats);
}

#else	/* CONFIG_CFQ_GROUP_IOSCHED */

static inline struct cfq_group *cfqg_parent(struct cfq_group *cfqg) { return NULL; }
static inline void cfqg_get(struct cfq_group *cfqg) { }
static inline void cfqg_put(struct cfq_group *cfqg) { }

#define cfq_log_cfqq(cfqd, cfqq, fmt, args...)	\
	blk_add_trace_msg((cfqd)->queue, "cfq%d%c%c " fmt, (cfqq)->pid,	\
			cfq_cfqq_sync((cfqq)) ? 'S' : 'A',		\
			cfqq_type((cfqq)) == SYNC_NOIDLE_WORKLOAD ? 'N' : ' ',\
				##args)
#define cfq_log_cfqg(cfqd, cfqg, fmt, args...)		do {} while (0)

static inline void cfqg_stats_update_io_add(struct cfq_group *cfqg,
			struct cfq_group *curr_cfqg, int rw) { }
static inline void cfqg_stats_update_timeslice_used(struct cfq_group *cfqg,
			unsigned long time, unsigned long unaccounted_time) { }
static inline void cfqg_stats_update_io_remove(struct cfq_group *cfqg, int rw) { }
static inline void cfqg_stats_update_io_merged(struct cfq_group *cfqg, int rw) { }
static inline void cfqg_stats_update_completion(struct cfq_group *cfqg,
			uint64_t start_time, uint64_t io_start_time, int rw) { }

#endif	/* CONFIG_CFQ_GROUP_IOSCHED */

#define cfq_log(cfqd, fmt, args...)	\
	blk_add_trace_msg((cfqd)->queue, "cfq " fmt, ##args)

/* Traverses through cfq group service trees */
#define for_each_cfqg_st(cfqg, i, j, st) \
	for (i = 0; i <= IDLE_WORKLOAD; i++) \
		for (j = 0, st = i < IDLE_WORKLOAD ? &cfqg->service_trees[i][j]\
			: &cfqg->service_tree_idle; \
			(i < IDLE_WORKLOAD && j <= SYNC_WORKLOAD) || \
			(i == IDLE_WORKLOAD && j == 0); \
			j++, st = i < IDLE_WORKLOAD ? \
			&cfqg->service_trees[i][j]: NULL) \

static inline bool cfq_io_thinktime_big(struct cfq_data *cfqd,
	struct cfq_ttime *ttime, bool group_idle)
{
	unsigned long slice;
	if (!sample_valid(ttime->ttime_samples))
		return false;
	if (group_idle)
		slice = cfqd->cfq_group_idle;
	else
		slice = cfqd->cfq_slice_idle;
	return ttime->ttime_mean > slice;
}

static inline bool iops_mode(struct cfq_data *cfqd)
{
	/*
	 * If we are not idling on queues and it is a NCQ drive, parallel
	 * execution of requests is on and measuring time is not possible
	 * in most of the cases until and unless we drive shallower queue
	 * depths and that becomes a performance bottleneck. In such cases
	 * switch to start providing fairness in terms of number of IOs.
	 */
	if (!cfqd->cfq_slice_idle && cfqd->hw_tag)
		return true;
	else
		return false;
}

static inline enum wl_class_t cfqq_class(struct cfq_queue *cfqq)
{
	if (cfq_class_idle(cfqq))
		return IDLE_WORKLOAD;
	if (cfq_class_rt(cfqq))
		return RT_WORKLOAD;
	return BE_WORKLOAD;
}


static enum wl_type_t cfqq_type(struct cfq_queue *cfqq)
{
	if (!cfq_cfqq_sync(cfqq))
		return ASYNC_WORKLOAD;
	if (!cfq_cfqq_idle_window(cfqq))
		return SYNC_NOIDLE_WORKLOAD;
	return SYNC_WORKLOAD;
}

static inline int cfq_group_busy_queues_wl(enum wl_class_t wl_class,
					struct cfq_data *cfqd,
					struct cfq_group *cfqg)
{
	if (wl_class == IDLE_WORKLOAD)
		return cfqg->service_tree_idle.count;

	return cfqg->service_trees[wl_class][ASYNC_WORKLOAD].count +
		cfqg->service_trees[wl_class][SYNC_NOIDLE_WORKLOAD].count +
		cfqg->service_trees[wl_class][SYNC_WORKLOAD].count;
}

static inline int cfqg_busy_async_queues(struct cfq_data *cfqd,
					struct cfq_group *cfqg)
{
	return cfqg->service_trees[RT_WORKLOAD][ASYNC_WORKLOAD].count +
		cfqg->service_trees[BE_WORKLOAD][ASYNC_WORKLOAD].count;
}

static void cfq_dispatch_insert(struct request_queue *, struct request *);
static struct cfq_queue *cfq_get_queue(struct cfq_data *cfqd, bool is_sync,
				       struct cfq_io_cq *cic, struct bio *bio);

static inline struct cfq_io_cq *icq_to_cic(struct io_cq *icq)
{
	/* cic->icq is the first member, %NULL will convert to %NULL */
	return container_of(icq, struct cfq_io_cq, icq);
}

static inline struct cfq_io_cq *cfq_cic_lookup(struct cfq_data *cfqd,
					       struct io_context *ioc)
{
	if (ioc)
		return icq_to_cic(ioc_lookup_icq(ioc, cfqd->queue));
	return NULL;
}

static inline struct cfq_queue *cic_to_cfqq(struct cfq_io_cq *cic, bool is_sync)
{
	return cic->cfqq[is_sync];
}

static inline void cic_set_cfqq(struct cfq_io_cq *cic, struct cfq_queue *cfqq,
				bool is_sync)
{
	cic->cfqq[is_sync] = cfqq;
}

static inline struct cfq_data *cic_to_cfqd(struct cfq_io_cq *cic)
{
	return cic->icq.q->elevator->elevator_data;
}

/*
 * We regard a request as SYNC, if it's either a read or has the SYNC bit
 * set (in which case it could also be direct WRITE).
 */
static inline bool cfq_bio_sync(struct bio *bio)
{
	return bio_data_dir(bio) == READ || (bio->bi_rw & REQ_SYNC);
}

/*
 * scheduler run of queue, if there are requests pending and no one in the
 * driver that will restart queueing
 */
static inline void cfq_schedule_dispatch(struct cfq_data *cfqd)
{
	if (cfqd->busy_queues) {
		cfq_log(cfqd, "schedule dispatch");
		kblockd_schedule_work(&cfqd->unplug_work);
	}
}

/*
 * Scale schedule slice based on io priority. Use the sync time slice only
 * if a queue is marked sync and has sync io queued. A sync queue with async
 * io only, should not get full sync slice length.
 */
static inline int cfq_prio_slice(struct cfq_data *cfqd, bool sync,
				 unsigned short prio)
{
	const int base_slice = cfqd->cfq_slice[sync];

	WARN_ON(prio >= IOPRIO_BE_NR);

	return base_slice + (base_slice/CFQ_SLICE_SCALE * (4 - prio));
}

static inline int
cfq_prio_to_slice(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	return cfq_prio_slice(cfqd, cfq_cfqq_sync(cfqq), cfqq->ioprio);
}

/**
 * cfqg_scale_charge - scale disk time charge according to cfqg weight
 * @charge: disk time being charged
 * @vfraction: vfraction of the cfqg, fixed point w/ CFQ_SERVICE_SHIFT
 *
 * Scale @charge according to @vfraction, which is in range (0, 1].  The
 * scaling is inversely proportional.
 *
 * scaled = charge / vfraction
 *
 * The result is also in fixed point w/ CFQ_SERVICE_SHIFT.
 */
static inline u64 cfqg_scale_charge(unsigned long charge,
				    unsigned int vfraction)
{
	u64 c = charge << CFQ_SERVICE_SHIFT;	/* make it fixed point */

	/* charge / vfraction */
	c <<= CFQ_SERVICE_SHIFT;
	do_div(c, vfraction);
	return c;
}

static inline u64 max_vdisktime(u64 min_vdisktime, u64 vdisktime)
{
	s64 delta = (s64)(vdisktime - min_vdisktime);
	if (delta > 0)
		min_vdisktime = vdisktime;

	return min_vdisktime;
}

static inline u64 min_vdisktime(u64 min_vdisktime, u64 vdisktime)
{
	s64 delta = (s64)(vdisktime - min_vdisktime);
	if (delta < 0)
		min_vdisktime = vdisktime;

	return min_vdisktime;
}

static void update_min_vdisktime(struct cfq_rb_root *st)
{
	struct cfq_group *cfqg;

	if (st->left) {
		cfqg = rb_entry_cfqg(st->left);
		st->min_vdisktime = max_vdisktime(st->min_vdisktime,
						  cfqg->vdisktime);
	}
}

/*
 * get averaged number of queues of RT/BE priority.
 * average is updated, with a formula that gives more weight to higher numbers,
 * to quickly follows sudden increases and decrease slowly
 */

static inline unsigned cfq_group_get_avg_queues(struct cfq_data *cfqd,
					struct cfq_group *cfqg, bool rt)
{
	unsigned min_q, max_q;
	unsigned mult  = cfq_hist_divisor - 1;
	unsigned round = cfq_hist_divisor / 2;
	unsigned busy = cfq_group_busy_queues_wl(rt, cfqd, cfqg);

	min_q = min(cfqg->busy_queues_avg[rt], busy);
	max_q = max(cfqg->busy_queues_avg[rt], busy);
	cfqg->busy_queues_avg[rt] = (mult * max_q + min_q + round) /
		cfq_hist_divisor;
	return cfqg->busy_queues_avg[rt];
}

static inline unsigned
cfq_group_slice(struct cfq_data *cfqd, struct cfq_group *cfqg)
{
	return cfqd->cfq_target_latency * cfqg->vfraction >> CFQ_SERVICE_SHIFT;
}

static inline unsigned
cfq_scaled_cfqq_slice(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	unsigned slice = cfq_prio_to_slice(cfqd, cfqq);
	if (cfqd->cfq_latency) {
		/*
		 * interested queues (we consider only the ones with the same
		 * priority class in the cfq group)
		 */
		unsigned iq = cfq_group_get_avg_queues(cfqd, cfqq->cfqg,
						cfq_class_rt(cfqq));
		unsigned sync_slice = cfqd->cfq_slice[1];
		unsigned expect_latency = sync_slice * iq;
		unsigned group_slice = cfq_group_slice(cfqd, cfqq->cfqg);

		if (expect_latency > group_slice) {
			unsigned base_low_slice = 2 * cfqd->cfq_slice_idle;
			/* scale low_slice according to IO priority
			 * and sync vs async */
			unsigned low_slice =
				min(slice, base_low_slice * slice / sync_slice);
			/* the adapted slice value is scaled to fit all iqs
			 * into the target latency */
			slice = max(slice * group_slice / expect_latency,
				    low_slice);
		}
	}
	return slice;
}

static inline void
cfq_set_prio_slice(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	unsigned slice = cfq_scaled_cfqq_slice(cfqd, cfqq);

	cfqq->slice_start = jiffies;
	cfqq->slice_end = jiffies + slice;
	cfqq->allocated_slice = slice;
	cfq_log_cfqq(cfqd, cfqq, "set_slice=%lu", cfqq->slice_end - jiffies);
}

/*
 * We need to wrap this check in cfq_cfqq_slice_new(), since ->slice_end
 * isn't valid until the first request from the dispatch is activated
 * and the slice time set.
 */
static inline bool cfq_slice_used(struct cfq_queue *cfqq)
{
	if (cfq_cfqq_slice_new(cfqq))
		return false;
	if (time_before(jiffies, cfqq->slice_end))
		return false;

	return true;
}

/*
 * Lifted from AS - choose which of rq1 and rq2 that is best served now.
 * We choose the request that is closest to the head right now. Distance
 * behind the head is penalized and only allowed to a certain extent.
 */
static struct request *
cfq_choose_req(struct cfq_data *cfqd, struct request *rq1, struct request *rq2, sector_t last)
{
	sector_t s1, s2, d1 = 0, d2 = 0;
	unsigned long back_max;
#define CFQ_RQ1_WRAP	0x01 /* request 1 wraps */
#define CFQ_RQ2_WRAP	0x02 /* request 2 wraps */
	unsigned wrap = 0; /* bit mask: requests behind the disk head? */

	if (rq1 == NULL || rq1 == rq2)
		return rq2;
	if (rq2 == NULL)
		return rq1;

	if (rq_is_sync(rq1) != rq_is_sync(rq2))
		return rq_is_sync(rq1) ? rq1 : rq2;

	if ((rq1->cmd_flags ^ rq2->cmd_flags) & REQ_PRIO)
		return rq1->cmd_flags & REQ_PRIO ? rq1 : rq2;

	s1 = blk_rq_pos(rq1);
	s2 = blk_rq_pos(rq2);

	/*
	 * by definition, 1KiB is 2 sectors
	 */
	back_max = cfqd->cfq_back_max * 2;

	/*
	 * Strict one way elevator _except_ in the case where we allow
	 * short backward seeks which are biased as twice the cost of a
	 * similar forward seek.
	 */
	if (s1 >= last)
		d1 = s1 - last;
	else if (s1 + back_max >= last)
		d1 = (last - s1) * cfqd->cfq_back_penalty;
	else
		wrap |= CFQ_RQ1_WRAP;

	if (s2 >= last)
		d2 = s2 - last;
	else if (s2 + back_max >= last)
		d2 = (last - s2) * cfqd->cfq_back_penalty;
	else
		wrap |= CFQ_RQ2_WRAP;

	/* Found required data */

	/*
	 * By doing switch() on the bit mask "wrap" we avoid having to
	 * check two variables for all permutations: --> faster!
	 */
	switch (wrap) {
	case 0: /* common case for CFQ: rq1 and rq2 not wrapped */
		if (d1 < d2)
			return rq1;
		else if (d2 < d1)
			return rq2;
		else {
			if (s1 >= s2)
				return rq1;
			else
				return rq2;
		}

	case CFQ_RQ2_WRAP:
		return rq1;
	case CFQ_RQ1_WRAP:
		return rq2;
	case (CFQ_RQ1_WRAP|CFQ_RQ2_WRAP): /* both rqs wrapped */
	default:
		/*
		 * Since both rqs are wrapped,
		 * start with the one that's further behind head
		 * (--> only *one* back seek required),
		 * since back seek takes more time than forward.
		 */
		if (s1 <= s2)
			return rq1;
		else
			return rq2;
	}
}

/*
 * The below is leftmost cache rbtree addon
 */
static struct cfq_queue *cfq_rb_first(struct cfq_rb_root *root)
{
	/* Service tree is empty */
	if (!root->count)
		return NULL;

	if (!root->left)
		root->left = rb_first(&root->rb);

	if (root->left)
		return rb_entry(root->left, struct cfq_queue, rb_node);

	return NULL;
}

static struct cfq_group *cfq_rb_first_group(struct cfq_rb_root *root)
{
	if (!root->left)
		root->left = rb_first(&root->rb);

	if (root->left)
		return rb_entry_cfqg(root->left);

	return NULL;
}

static void rb_erase_init(struct rb_node *n, struct rb_root *root)
{
	rb_erase(n, root);
	RB_CLEAR_NODE(n);
}

static void cfq_rb_erase(struct rb_node *n, struct cfq_rb_root *root)
{
	if (root->left == n)
		root->left = NULL;
	rb_erase_init(n, &root->rb);
	--root->count;
}

/*
 * would be nice to take fifo expire time into account as well
 */
static struct request *
cfq_find_next_rq(struct cfq_data *cfqd, struct cfq_queue *cfqq,
		  struct request *last)
{
	struct rb_node *rbnext = rb_next(&last->rb_node);
	struct rb_node *rbprev = rb_prev(&last->rb_node);
	struct request *next = NULL, *prev = NULL;

	BUG_ON(RB_EMPTY_NODE(&last->rb_node));

	if (rbprev)
		prev = rb_entry_rq(rbprev);

	if (rbnext)
		next = rb_entry_rq(rbnext);
	else {
		rbnext = rb_first(&cfqq->sort_list);
		if (rbnext && rbnext != &last->rb_node)
			next = rb_entry_rq(rbnext);
	}

	return cfq_choose_req(cfqd, next, prev, blk_rq_pos(last));
}

static unsigned long cfq_slice_offset(struct cfq_data *cfqd,
				      struct cfq_queue *cfqq)
{
	/*
	 * just an approximation, should be ok.
	 */
	return (cfqq->cfqg->nr_cfqq - 1) * (cfq_prio_slice(cfqd, 1, 0) -
		       cfq_prio_slice(cfqd, cfq_cfqq_sync(cfqq), cfqq->ioprio));
}

static inline s64
cfqg_key(struct cfq_rb_root *st, struct cfq_group *cfqg)
{
	return cfqg->vdisktime - st->min_vdisktime;
}

static void
__cfq_group_service_tree_add(struct cfq_rb_root *st, struct cfq_group *cfqg)
{
	struct rb_node **node = &st->rb.rb_node;
	struct rb_node *parent = NULL;
	struct cfq_group *__cfqg;
	s64 key = cfqg_key(st, cfqg);
	int left = 1;

	while (*node != NULL) {
		parent = *node;
		__cfqg = rb_entry_cfqg(parent);

		if (key < cfqg_key(st, __cfqg))
			node = &parent->rb_left;
		else {
			node = &parent->rb_right;
			left = 0;
		}
	}

	if (left)
		st->left = &cfqg->rb_node;

	rb_link_node(&cfqg->rb_node, parent, node);
	rb_insert_color(&cfqg->rb_node, &st->rb);
}

/*
 * This has to be called only on activation of cfqg
 */
static void
cfq_update_group_weight(struct cfq_group *cfqg)
{
	if (cfqg->new_weight) {
		cfqg->weight = cfqg->new_weight;
		cfqg->new_weight = 0;
	}
}

static void
cfq_update_group_leaf_weight(struct cfq_group *cfqg)
{
	BUG_ON(!RB_EMPTY_NODE(&cfqg->rb_node));

	if (cfqg->new_leaf_weight) {
		cfqg->leaf_weight = cfqg->new_leaf_weight;
		cfqg->new_leaf_weight = 0;
	}
}

static void
cfq_group_service_tree_add(struct cfq_rb_root *st, struct cfq_group *cfqg)
{
	unsigned int vfr = 1 << CFQ_SERVICE_SHIFT;	/* start with 1 */
	struct cfq_group *pos = cfqg;
	struct cfq_group *parent;
	bool propagate;

	/* add to the service tree */
	BUG_ON(!RB_EMPTY_NODE(&cfqg->rb_node));

	/*
	 * Update leaf_weight.  We cannot update weight at this point
	 * because cfqg might already have been activated and is
	 * contributing its current weight to the parent's child_weight.
	 */
	cfq_update_group_leaf_weight(cfqg);
	__cfq_group_service_tree_add(st, cfqg);

	/*
	 * Activate @cfqg and calculate the portion of vfraction @cfqg is
	 * entitled to.  vfraction is calculated by walking the tree
	 * towards the root calculating the fraction it has at each level.
	 * The compounded ratio is how much vfraction @cfqg owns.
	 *
	 * Start with the proportion tasks in this cfqg has against active
	 * children cfqgs - its leaf_weight against children_weight.
	 */
	propagate = !pos->nr_active++;
	pos->children_weight += pos->leaf_weight;
	vfr = vfr * pos->leaf_weight / pos->children_weight;

	/*
	 * Compound ->weight walking up the tree.  Both activation and
	 * vfraction calculation are done in the same loop.  Propagation
	 * stops once an already activated node is met.  vfraction
	 * calculation should always continue to the root.
	 */
	while ((parent = cfqg_parent(pos))) {
		if (propagate) {
			cfq_update_group_weight(pos);
			propagate = !parent->nr_active++;
			parent->children_weight += pos->weight;
		}
		vfr = vfr * pos->weight / parent->children_weight;
		pos = parent;
	}

	cfqg->vfraction = max_t(unsigned, vfr, 1);
}

static void
cfq_group_notify_queue_add(struct cfq_data *cfqd, struct cfq_group *cfqg)
{
	struct cfq_rb_root *st = &cfqd->grp_service_tree;
	struct cfq_group *__cfqg;
	struct rb_node *n;

	cfqg->nr_cfqq++;
	if (!RB_EMPTY_NODE(&cfqg->rb_node))
		return;

	/*
	 * Currently put the group at the end. Later implement something
	 * so that groups get lesser vtime based on their weights, so that
	 * if group does not loose all if it was not continuously backlogged.
	 */
	n = rb_last(&st->rb);
	if (n) {
		__cfqg = rb_entry_cfqg(n);
		cfqg->vdisktime = __cfqg->vdisktime + CFQ_IDLE_DELAY;
	} else
		cfqg->vdisktime = st->min_vdisktime;
	cfq_group_service_tree_add(st, cfqg);
}

static void
cfq_group_service_tree_del(struct cfq_rb_root *st, struct cfq_group *cfqg)
{
	struct cfq_group *pos = cfqg;
	bool propagate;

	/*
	 * Undo activation from cfq_group_service_tree_add().  Deactivate
	 * @cfqg and propagate deactivation upwards.
	 */
	propagate = !--pos->nr_active;
	pos->children_weight -= pos->leaf_weight;

	while (propagate) {
		struct cfq_group *parent = cfqg_parent(pos);

		/* @pos has 0 nr_active at this point */
		WARN_ON_ONCE(pos->children_weight);
		pos->vfraction = 0;

		if (!parent)
			break;

		propagate = !--parent->nr_active;
		parent->children_weight -= pos->weight;
		pos = parent;
	}

	/* remove from the service tree */
	if (!RB_EMPTY_NODE(&cfqg->rb_node))
		cfq_rb_erase(&cfqg->rb_node, st);
}

static void
cfq_group_notify_queue_del(struct cfq_data *cfqd, struct cfq_group *cfqg)
{
	struct cfq_rb_root *st = &cfqd->grp_service_tree;

	BUG_ON(cfqg->nr_cfqq < 1);
	cfqg->nr_cfqq--;

	/* If there are other cfq queues under this group, don't delete it */
	if (cfqg->nr_cfqq)
		return;

	cfq_log_cfqg(cfqd, cfqg, "del_from_rr group");
	cfq_group_service_tree_del(st, cfqg);
	cfqg->saved_wl_slice = 0;
	cfqg_stats_update_dequeue(cfqg);
}

static inline unsigned int cfq_cfqq_slice_usage(struct cfq_queue *cfqq,
						unsigned int *unaccounted_time)
{
	unsigned int slice_used;

	/*
	 * Queue got expired before even a single request completed or
	 * got expired immediately after first request completion.
	 */
	if (!cfqq->slice_start || cfqq->slice_start == jiffies) {
		/*
		 * Also charge the seek time incurred to the group, otherwise
		 * if there are mutiple queues in the group, each can dispatch
		 * a single request on seeky media and cause lots of seek time
		 * and group will never know it.
		 */
		slice_used = max_t(unsigned, (jiffies - cfqq->dispatch_start),
					1);
	} else {
		slice_used = jiffies - cfqq->slice_start;
		if (slice_used > cfqq->allocated_slice) {
			*unaccounted_time = slice_used - cfqq->allocated_slice;
			slice_used = cfqq->allocated_slice;
		}
		if (time_after(cfqq->slice_start, cfqq->dispatch_start))
			*unaccounted_time += cfqq->slice_start -
					cfqq->dispatch_start;
	}

	return slice_used;
}

static void cfq_group_served(struct cfq_data *cfqd, struct cfq_group *cfqg,
				struct cfq_queue *cfqq)
{
	struct cfq_rb_root *st = &cfqd->grp_service_tree;
	unsigned int used_sl, charge, unaccounted_sl = 0;
	int nr_sync = cfqg->nr_cfqq - cfqg_busy_async_queues(cfqd, cfqg)
			- cfqg->service_tree_idle.count;
	unsigned int vfr;

	BUG_ON(nr_sync < 0);
	used_sl = charge = cfq_cfqq_slice_usage(cfqq, &unaccounted_sl);

	if (iops_mode(cfqd))
		charge = cfqq->slice_dispatch;
	else if (!cfq_cfqq_sync(cfqq) && !nr_sync)
		charge = cfqq->allocated_slice;

	/*
	 * Can't update vdisktime while on service tree and cfqg->vfraction
	 * is valid only while on it.  Cache vfr, leave the service tree,
	 * update vdisktime and go back on.  The re-addition to the tree
	 * will also update the weights as necessary.
	 */
	vfr = cfqg->vfraction;
	cfq_group_service_tree_del(st, cfqg);
	cfqg->vdisktime += cfqg_scale_charge(charge, vfr);
	cfq_group_service_tree_add(st, cfqg);

	/* This group is being expired. Save the context */
	if (time_after(cfqd->workload_expires, jiffies)) {
		cfqg->saved_wl_slice = cfqd->workload_expires
						- jiffies;
		cfqg->saved_wl_type = cfqd->serving_wl_type;
		cfqg->saved_wl_class = cfqd->serving_wl_class;
	} else
		cfqg->saved_wl_slice = 0;

	cfq_log_cfqg(cfqd, cfqg, "served: vt=%llu min_vt=%llu", cfqg->vdisktime,
					st->min_vdisktime);
	cfq_log_cfqq(cfqq->cfqd, cfqq,
		     "sl_used=%u disp=%u charge=%u iops=%u sect=%lu",
		     used_sl, cfqq->slice_dispatch, charge,
		     iops_mode(cfqd), cfqq->nr_sectors);
	cfqg_stats_update_timeslice_used(cfqg, used_sl, unaccounted_sl);
	cfqg_stats_set_start_empty_time(cfqg);
}

/**
 * cfq_init_cfqg_base - initialize base part of a cfq_group
 * @cfqg: cfq_group to initialize
 *
 * Initialize the base part which is used whether %CONFIG_CFQ_GROUP_IOSCHED
 * is enabled or not.
 */
static void cfq_init_cfqg_base(struct cfq_group *cfqg)
{
	struct cfq_rb_root *st;
	int i, j;

	for_each_cfqg_st(cfqg, i, j, st)
		*st = CFQ_RB_ROOT;
	RB_CLEAR_NODE(&cfqg->rb_node);

	cfqg->ttime.last_end_request = jiffies;
}

#ifdef CONFIG_CFQ_GROUP_IOSCHED
static int __cfq_set_weight(struct cgroup_subsys_state *css, u64 val,
			    bool on_dfl, bool reset_dev, bool is_leaf_weight);

static void cfqg_stats_exit(struct cfqg_stats *stats)
{
	blkg_rwstat_exit(&stats->merged);
	blkg_rwstat_exit(&stats->service_time);
	blkg_rwstat_exit(&stats->wait_time);
	blkg_rwstat_exit(&stats->queued);
	blkg_stat_exit(&stats->time);
#ifdef CONFIG_DEBUG_BLK_CGROUP
	blkg_stat_exit(&stats->unaccounted_time);
	blkg_stat_exit(&stats->avg_queue_size_sum);
	blkg_stat_exit(&stats->avg_queue_size_samples);
	blkg_stat_exit(&stats->dequeue);
	blkg_stat_exit(&stats->group_wait_time);
	blkg_stat_exit(&stats->idle_time);
	blkg_stat_exit(&stats->empty_time);
#endif
}

static int cfqg_stats_init(struct cfqg_stats *stats, gfp_t gfp)
{
	if (blkg_rwstat_init(&stats->merged, gfp) ||
	    blkg_rwstat_init(&stats->service_time, gfp) ||
	    blkg_rwstat_init(&stats->wait_time, gfp) ||
	    blkg_rwstat_init(&stats->queued, gfp) ||
	    blkg_stat_init(&stats->time, gfp))
		goto err;

#ifdef CONFIG_DEBUG_BLK_CGROUP
	if (blkg_stat_init(&stats->unaccounted_time, gfp) ||
	    blkg_stat_init(&stats->avg_queue_size_sum, gfp) ||
	    blkg_stat_init(&stats->avg_queue_size_samples, gfp) ||
	    blkg_stat_init(&stats->dequeue, gfp) ||
	    blkg_stat_init(&stats->group_wait_time, gfp) ||
	    blkg_stat_init(&stats->idle_time, gfp) ||
	    blkg_stat_init(&stats->empty_time, gfp))
		goto err;
#endif
	return 0;
err:
	cfqg_stats_exit(stats);
	return -ENOMEM;
}

static struct blkcg_policy_data *cfq_cpd_alloc(gfp_t gfp)
{
	struct cfq_group_data *cgd;

	cgd = kzalloc(sizeof(*cgd), gfp);
	if (!cgd)
		return NULL;
	return &cgd->cpd;
}

static void cfq_cpd_init(struct blkcg_policy_data *cpd)
{
	struct cfq_group_data *cgd = cpd_to_cfqgd(cpd);
	unsigned int weight = cgroup_subsys_on_dfl(io_cgrp_subsys) ?
			      CGROUP_WEIGHT_DFL : CFQ_WEIGHT_LEGACY_DFL;

	if (cpd_to_blkcg(cpd) == &blkcg_root)
		weight *= 2;

	cgd->weight = weight;
	cgd->leaf_weight = weight;
}

static void cfq_cpd_free(struct blkcg_policy_data *cpd)
{
	kfree(cpd_to_cfqgd(cpd));
}

static void cfq_cpd_bind(struct blkcg_policy_data *cpd)
{
	struct blkcg *blkcg = cpd_to_blkcg(cpd);
	bool on_dfl = cgroup_subsys_on_dfl(io_cgrp_subsys);
	unsigned int weight = on_dfl ? CGROUP_WEIGHT_DFL : CFQ_WEIGHT_LEGACY_DFL;

	if (blkcg == &blkcg_root)
		weight *= 2;

	WARN_ON_ONCE(__cfq_set_weight(&blkcg->css, weight, on_dfl, true, false));
	WARN_ON_ONCE(__cfq_set_weight(&blkcg->css, weight, on_dfl, true, true));
}

static struct blkg_policy_data *cfq_pd_alloc(gfp_t gfp, int node)
{
	struct cfq_group *cfqg;

	cfqg = kzalloc_node(sizeof(*cfqg), gfp, node);
	if (!cfqg)
		return NULL;

	cfq_init_cfqg_base(cfqg);
	if (cfqg_stats_init(&cfqg->stats, gfp)) {
		kfree(cfqg);
		return NULL;
	}

	return &cfqg->pd;
}

static void cfq_pd_init(struct blkg_policy_data *pd)
{
	struct cfq_group *cfqg = pd_to_cfqg(pd);
	struct cfq_group_data *cgd = blkcg_to_cfqgd(pd->blkg->blkcg);

	cfqg->weight = cgd->weight;
	cfqg->leaf_weight = cgd->leaf_weight;
}

static void cfq_pd_offline(struct blkg_policy_data *pd)
{
	struct cfq_group *cfqg = pd_to_cfqg(pd);
	int i;

	for (i = 0; i < IOPRIO_BE_NR; i++) {
		if (cfqg->async_cfqq[0][i])
			cfq_put_queue(cfqg->async_cfqq[0][i]);
		if (cfqg->async_cfqq[1][i])
			cfq_put_queue(cfqg->async_cfqq[1][i]);
	}

	if (cfqg->async_idle_cfqq)
		cfq_put_queue(cfqg->async_idle_cfqq);

	/*
	 * @blkg is going offline and will be ignored by
	 * blkg_[rw]stat_recursive_sum().  Transfer stats to the parent so
	 * that they don't get lost.  If IOs complete after this point, the
	 * stats for them will be lost.  Oh well...
	 */
	cfqg_stats_xfer_dead(cfqg);
}

static void cfq_pd_free(struct blkg_policy_data *pd)
{
	struct cfq_group *cfqg = pd_to_cfqg(pd);

	cfqg_stats_exit(&cfqg->stats);
	return kfree(cfqg);
}

static void cfq_pd_reset_stats(struct blkg_policy_data *pd)
{
	struct cfq_group *cfqg = pd_to_cfqg(pd);

	cfqg_stats_reset(&cfqg->stats);
}

static struct cfq_group *cfq_lookup_cfqg(struct cfq_data *cfqd,
					 struct blkcg *blkcg)
{
	struct blkcg_gq *blkg;

	blkg = blkg_lookup(blkcg, cfqd->queue);
	if (likely(blkg))
		return blkg_to_cfqg(blkg);
	return NULL;
}

static void cfq_link_cfqq_cfqg(struct cfq_queue *cfqq, struct cfq_group *cfqg)
{
	cfqq->cfqg = cfqg;
	/* cfqq reference on cfqg */
	cfqg_get(cfqg);
}

static u64 cfqg_prfill_weight_device(struct seq_file *sf,
				     struct blkg_policy_data *pd, int off)
{
	struct cfq_group *cfqg = pd_to_cfqg(pd);

	if (!cfqg->dev_weight)
		return 0;
	return __blkg_prfill_u64(sf, pd, cfqg->dev_weight);
}

static int cfqg_print_weight_device(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  cfqg_prfill_weight_device, &blkcg_policy_cfq,
			  0, false);
	return 0;
}

static u64 cfqg_prfill_leaf_weight_device(struct seq_file *sf,
					  struct blkg_policy_data *pd, int off)
{
	struct cfq_group *cfqg = pd_to_cfqg(pd);

	if (!cfqg->dev_leaf_weight)
		return 0;
	return __blkg_prfill_u64(sf, pd, cfqg->dev_leaf_weight);
}

static int cfqg_print_leaf_weight_device(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  cfqg_prfill_leaf_weight_device, &blkcg_policy_cfq,
			  0, false);
	return 0;
}

static int cfq_print_weight(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));
	struct cfq_group_data *cgd = blkcg_to_cfqgd(blkcg);
	unsigned int val = 0;

	if (cgd)
		val = cgd->weight;

	seq_printf(sf, "%u\n", val);
	return 0;
}

static int cfq_print_leaf_weight(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));
	struct cfq_group_data *cgd = blkcg_to_cfqgd(blkcg);
	unsigned int val = 0;

	if (cgd)
		val = cgd->leaf_weight;

	seq_printf(sf, "%u\n", val);
	return 0;
}

static ssize_t __cfqg_set_weight_device(struct kernfs_open_file *of,
					char *buf, size_t nbytes, loff_t off,
					bool on_dfl, bool is_leaf_weight)
{
	unsigned int min = on_dfl ? CGROUP_WEIGHT_MIN : CFQ_WEIGHT_LEGACY_MIN;
	unsigned int max = on_dfl ? CGROUP_WEIGHT_MAX : CFQ_WEIGHT_LEGACY_MAX;
	struct blkcg *blkcg = css_to_blkcg(of_css(of));
	struct blkg_conf_ctx ctx;
	struct cfq_group *cfqg;
	struct cfq_group_data *cfqgd;
	int ret;
	u64 v;

	ret = blkg_conf_prep(blkcg, &blkcg_policy_cfq, buf, &ctx);
	if (ret)
		return ret;

	if (sscanf(ctx.body, "%llu", &v) == 1) {
		/* require "default" on dfl */
		ret = -ERANGE;
		if (!v && on_dfl)
			goto out_finish;
	} else if (!strcmp(strim(ctx.body), "default")) {
		v = 0;
	} else {
		ret = -EINVAL;
		goto out_finish;
	}

	cfqg = blkg_to_cfqg(ctx.blkg);
	cfqgd = blkcg_to_cfqgd(blkcg);

	ret = -ERANGE;
	if (!v || (v >= min && v <= max)) {
		if (!is_leaf_weight) {
			cfqg->dev_weight = v;
			cfqg->new_weight = v ?: cfqgd->weight;
		} else {
			cfqg->dev_leaf_weight = v;
			cfqg->new_leaf_weight = v ?: cfqgd->leaf_weight;
		}
		ret = 0;
	}
out_finish:
	blkg_conf_finish(&ctx);
	return ret ?: nbytes;
}

static ssize_t cfqg_set_weight_device(struct kernfs_open_file *of,
				      char *buf, size_t nbytes, loff_t off)
{
	return __cfqg_set_weight_device(of, buf, nbytes, off, false, false);
}

static ssize_t cfqg_set_leaf_weight_device(struct kernfs_open_file *of,
					   char *buf, size_t nbytes, loff_t off)
{
	return __cfqg_set_weight_device(of, buf, nbytes, off, false, true);
}

static int __cfq_set_weight(struct cgroup_subsys_state *css, u64 val,
			    bool on_dfl, bool reset_dev, bool is_leaf_weight)
{
	unsigned int min = on_dfl ? CGROUP_WEIGHT_MIN : CFQ_WEIGHT_LEGACY_MIN;
	unsigned int max = on_dfl ? CGROUP_WEIGHT_MAX : CFQ_WEIGHT_LEGACY_MAX;
	struct blkcg *blkcg = css_to_blkcg(css);
	struct blkcg_gq *blkg;
	struct cfq_group_data *cfqgd;
	int ret = 0;

	if (val < min || val > max)
		return -ERANGE;

	spin_lock_irq(&blkcg->lock);
	cfqgd = blkcg_to_cfqgd(blkcg);
	if (!cfqgd) {
		ret = -EINVAL;
		goto out;
	}

	if (!is_leaf_weight)
		cfqgd->weight = val;
	else
		cfqgd->leaf_weight = val;

	hlist_for_each_entry(blkg, &blkcg->blkg_list, blkcg_node) {
		struct cfq_group *cfqg = blkg_to_cfqg(blkg);

		if (!cfqg)
			continue;

		if (!is_leaf_weight) {
			if (reset_dev)
				cfqg->dev_weight = 0;
			if (!cfqg->dev_weight)
				cfqg->new_weight = cfqgd->weight;
		} else {
			if (reset_dev)
				cfqg->dev_leaf_weight = 0;
			if (!cfqg->dev_leaf_weight)
				cfqg->new_leaf_weight = cfqgd->leaf_weight;
		}
	}

out:
	spin_unlock_irq(&blkcg->lock);
	return ret;
}

static int cfq_set_weight(struct cgroup_subsys_state *css, struct cftype *cft,
			  u64 val)
{
	return __cfq_set_weight(css, val, false, false, false);
}

static int cfq_set_leaf_weight(struct cgroup_subsys_state *css,
			       struct cftype *cft, u64 val)
{
	return __cfq_set_weight(css, val, false, false, true);
}

static int cfqg_print_stat(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)), blkg_prfill_stat,
			  &blkcg_policy_cfq, seq_cft(sf)->private, false);
	return 0;
}

static int cfqg_print_rwstat(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)), blkg_prfill_rwstat,
			  &blkcg_policy_cfq, seq_cft(sf)->private, true);
	return 0;
}

static u64 cfqg_prfill_stat_recursive(struct seq_file *sf,
				      struct blkg_policy_data *pd, int off)
{
	u64 sum = blkg_stat_recursive_sum(pd_to_blkg(pd),
					  &blkcg_policy_cfq, off);
	return __blkg_prfill_u64(sf, pd, sum);
}

static u64 cfqg_prfill_rwstat_recursive(struct seq_file *sf,
					struct blkg_policy_data *pd, int off)
{
	struct blkg_rwstat sum = blkg_rwstat_recursive_sum(pd_to_blkg(pd),
							&blkcg_policy_cfq, off);
	return __blkg_prfill_rwstat(sf, pd, &sum);
}

static int cfqg_print_stat_recursive(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  cfqg_prfill_stat_recursive, &blkcg_policy_cfq,
			  seq_cft(sf)->private, false);
	return 0;
}

static int cfqg_print_rwstat_recursive(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  cfqg_prfill_rwstat_recursive, &blkcg_policy_cfq,
			  seq_cft(sf)->private, true);
	return 0;
}

static u64 cfqg_prfill_sectors(struct seq_file *sf, struct blkg_policy_data *pd,
			       int off)
{
	u64 sum = blkg_rwstat_total(&pd->blkg->stat_bytes);

	return __blkg_prfill_u64(sf, pd, sum >> 9);
}

static int cfqg_print_stat_sectors(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  cfqg_prfill_sectors, &blkcg_policy_cfq, 0, false);
	return 0;
}

static u64 cfqg_prfill_sectors_recursive(struct seq_file *sf,
					 struct blkg_policy_data *pd, int off)
{
	struct blkg_rwstat tmp = blkg_rwstat_recursive_sum(pd->blkg, NULL,
					offsetof(struct blkcg_gq, stat_bytes));
	u64 sum = atomic64_read(&tmp.aux_cnt[BLKG_RWSTAT_READ]) +
		atomic64_read(&tmp.aux_cnt[BLKG_RWSTAT_WRITE]);

	return __blkg_prfill_u64(sf, pd, sum >> 9);
}

static int cfqg_print_stat_sectors_recursive(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  cfqg_prfill_sectors_recursive, &blkcg_policy_cfq, 0,
			  false);
	return 0;
}

#ifdef CONFIG_DEBUG_BLK_CGROUP
static u64 cfqg_prfill_avg_queue_size(struct seq_file *sf,
				      struct blkg_policy_data *pd, int off)
{
	struct cfq_group *cfqg = pd_to_cfqg(pd);
	u64 samples = blkg_stat_read(&cfqg->stats.avg_queue_size_samples);
	u64 v = 0;

	if (samples) {
		v = blkg_stat_read(&cfqg->stats.avg_queue_size_sum);
		v = div64_u64(v, samples);
	}
	__blkg_prfill_u64(sf, pd, v);
	return 0;
}

/* print avg_queue_size */
static int cfqg_print_avg_queue_size(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  cfqg_prfill_avg_queue_size, &blkcg_policy_cfq,
			  0, false);
	return 0;
}
#endif	/* CONFIG_DEBUG_BLK_CGROUP */

static struct cftype cfq_blkcg_legacy_files[] = {
	/* on root, weight is mapped to leaf_weight */
	{
		.name = "weight_device",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = cfqg_print_leaf_weight_device,
		.write = cfqg_set_leaf_weight_device,
	},
	{
		.name = "weight",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = cfq_print_leaf_weight,
		.write_u64 = cfq_set_leaf_weight,
	},

	/* no such mapping necessary for !roots */
	{
		.name = "weight_device",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = cfqg_print_weight_device,
		.write = cfqg_set_weight_device,
	},
	{
		.name = "weight",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = cfq_print_weight,
		.write_u64 = cfq_set_weight,
	},

	{
		.name = "leaf_weight_device",
		.seq_show = cfqg_print_leaf_weight_device,
		.write = cfqg_set_leaf_weight_device,
	},
	{
		.name = "leaf_weight",
		.seq_show = cfq_print_leaf_weight,
		.write_u64 = cfq_set_leaf_weight,
	},

	/* statistics, covers only the tasks in the cfqg */
	{
		.name = "time",
		.private = offsetof(struct cfq_group, stats.time),
		.seq_show = cfqg_print_stat,
	},
	{
		.name = "sectors",
		.seq_show = cfqg_print_stat_sectors,
	},
	{
		.name = "io_service_bytes",
		.private = (unsigned long)&blkcg_policy_cfq,
		.seq_show = blkg_print_stat_bytes,
	},
	{
		.name = "io_serviced",
		.private = (unsigned long)&blkcg_policy_cfq,
		.seq_show = blkg_print_stat_ios,
	},
	{
		.name = "io_service_time",
		.private = offsetof(struct cfq_group, stats.service_time),
		.seq_show = cfqg_print_rwstat,
	},
	{
		.name = "io_wait_time",
		.private = offsetof(struct cfq_group, stats.wait_time),
		.seq_show = cfqg_print_rwstat,
	},
	{
		.name = "io_merged",
		.private = offsetof(struct cfq_group, stats.merged),
		.seq_show = cfqg_print_rwstat,
	},
	{
		.name = "io_queued",
		.private = offsetof(struct cfq_group, stats.queued),
		.seq_show = cfqg_print_rwstat,
	},

	/* the same statictics which cover the cfqg and its descendants */
	{
		.name = "time_recursive",
		.private = offsetof(struct cfq_group, stats.time),
		.seq_show = cfqg_print_stat_recursive,
	},
	{
		.name = "sectors_recursive",
		.seq_show = cfqg_print_stat_sectors_recursive,
	},
	{
		.name = "io_service_bytes_recursive",
		.private = (unsigned long)&blkcg_policy_cfq,
		.seq_show = blkg_print_stat_bytes_recursive,
	},
	{
		.name = "io_serviced_recursive",
		.private = (unsigned long)&blkcg_policy_cfq,
		.seq_show = blkg_print_stat_ios_recursive,
	},
	{
		.name = "io_service_time_recursive",
		.private = offsetof(struct cfq_group, stats.service_time),
		.seq_show = cfqg_print_rwstat_recursive,
	},
	{
		.name = "io_wait_time_recursive",
		.private = offsetof(struct cfq_group, stats.wait_time),
		.seq_show = cfqg_print_rwstat_recursive,
	},
	{
		.name = "io_merged_recursive",
		.private = offsetof(struct cfq_group, stats.merged),
		.seq_show = cfqg_print_rwstat_recursive,
	},
	{
		.name = "io_queued_recursive",
		.private = offsetof(struct cfq_group, stats.queued),
		.seq_show = cfqg_print_rwstat_recursive,
	},
#ifdef CONFIG_DEBUG_BLK_CGROUP
	{
		.name = "avg_queue_size",
		.seq_show = cfqg_print_avg_queue_size,
	},
	{
		.name = "group_wait_time",
		.private = offsetof(struct cfq_group, stats.group_wait_time),
		.seq_show = cfqg_print_stat,
	},
	{
		.name = "idle_time",
		.private = offsetof(struct cfq_group, stats.idle_time),
		.seq_show = cfqg_print_stat,
	},
	{
		.name = "empty_time",
		.private = offsetof(struct cfq_group, stats.empty_time),
		.seq_show = cfqg_print_stat,
	},
	{
		.name = "dequeue",
		.private = offsetof(struct cfq_group, stats.dequeue),
		.seq_show = cfqg_print_stat,
	},
	{
		.name = "unaccounted_time",
		.private = offsetof(struct cfq_group, stats.unaccounted_time),
		.seq_show = cfqg_print_stat,
	},
#endif	/* CONFIG_DEBUG_BLK_CGROUP */
	{ }	/* terminate */
};

static int cfq_print_weight_on_dfl(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));
	struct cfq_group_data *cgd = blkcg_to_cfqgd(blkcg);

	seq_printf(sf, "default %u\n", cgd->weight);
	blkcg_print_blkgs(sf, blkcg, cfqg_prfill_weight_device,
			  &blkcg_policy_cfq, 0, false);
	return 0;
}

static ssize_t cfq_set_weight_on_dfl(struct kernfs_open_file *of,
				     char *buf, size_t nbytes, loff_t off)
{
	char *endp;
	int ret;
	u64 v;

	buf = strim(buf);

	/* "WEIGHT" or "default WEIGHT" sets the default weight */
	v = simple_strtoull(buf, &endp, 0);
	if (*endp == '\0' || sscanf(buf, "default %llu", &v) == 1) {
		ret = __cfq_set_weight(of_css(of), v, true, false, false);
		return ret ?: nbytes;
	}

	/* "MAJ:MIN WEIGHT" */
	return __cfqg_set_weight_device(of, buf, nbytes, off, true, false);
}

static struct cftype cfq_blkcg_files[] = {
	{
		.name = "weight",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = cfq_print_weight_on_dfl,
		.write = cfq_set_weight_on_dfl,
	},
	{ }	/* terminate */
};

#else /* GROUP_IOSCHED */
static struct cfq_group *cfq_lookup_cfqg(struct cfq_data *cfqd,
					 struct blkcg *blkcg)
{
	return cfqd->root_group;
}

static inline void
cfq_link_cfqq_cfqg(struct cfq_queue *cfqq, struct cfq_group *cfqg) {
	cfqq->cfqg = cfqg;
}

#endif /* GROUP_IOSCHED */

/*
 * The cfqd->service_trees holds all pending cfq_queue's that have
 * requests waiting to be processed. It is sorted in the order that
 * we will service the queues.
 */
static void cfq_service_tree_add(struct cfq_data *cfqd, struct cfq_queue *cfqq,
				 bool add_front)
{
	struct rb_node **p, *parent;
	struct cfq_queue *__cfqq;
	unsigned long rb_key;
	struct cfq_rb_root *st;
	int left;
	int new_cfqq = 1;

	st = st_for(cfqq->cfqg, cfqq_class(cfqq), cfqq_type(cfqq));
	if (cfq_class_idle(cfqq)) {
		rb_key = CFQ_IDLE_DELAY;
		parent = rb_last(&st->rb);
		if (parent && parent != &cfqq->rb_node) {
			__cfqq = rb_entry(parent, struct cfq_queue, rb_node);
			rb_key += __cfqq->rb_key;
		} else
			rb_key += jiffies;
	} else if (!add_front) {
		/*
		 * Get our rb key offset. Subtract any residual slice
		 * value carried from last service. A negative resid
		 * count indicates slice overrun, and this should position
		 * the next service time further away in the tree.
		 */
		rb_key = cfq_slice_offset(cfqd, cfqq) + jiffies;
		rb_key -= cfqq->slice_resid;
		cfqq->slice_resid = 0;
	} else {
		rb_key = -HZ;
		__cfqq = cfq_rb_first(st);
		rb_key += __cfqq ? __cfqq->rb_key : jiffies;
	}

	if (!RB_EMPTY_NODE(&cfqq->rb_node)) {
		new_cfqq = 0;
		/*
		 * same position, nothing more to do
		 */
		if (rb_key == cfqq->rb_key && cfqq->service_tree == st)
			return;

		cfq_rb_erase(&cfqq->rb_node, cfqq->service_tree);
		cfqq->service_tree = NULL;
	}

	left = 1;
	parent = NULL;
	cfqq->service_tree = st;
	p = &st->rb.rb_node;
	while (*p) {
		parent = *p;
		__cfqq = rb_entry(parent, struct cfq_queue, rb_node);

		/*
		 * sort by key, that represents service time.
		 */
		if (time_before(rb_key, __cfqq->rb_key))
			p = &parent->rb_left;
		else {
			p = &parent->rb_right;
			left = 0;
		}
	}

	if (left)
		st->left = &cfqq->rb_node;

	cfqq->rb_key = rb_key;
	rb_link_node(&cfqq->rb_node, parent, p);
	rb_insert_color(&cfqq->rb_node, &st->rb);
	st->count++;
	if (add_front || !new_cfqq)
		return;
	cfq_group_notify_queue_add(cfqd, cfqq->cfqg);
}

static struct cfq_queue *
cfq_prio_tree_lookup(struct cfq_data *cfqd, struct rb_root *root,
		     sector_t sector, struct rb_node **ret_parent,
		     struct rb_node ***rb_link)
{
	struct rb_node **p, *parent;
	struct cfq_queue *cfqq = NULL;

	parent = NULL;
	p = &root->rb_node;
	while (*p) {
		struct rb_node **n;

		parent = *p;
		cfqq = rb_entry(parent, struct cfq_queue, p_node);

		/*
		 * Sort strictly based on sector.  Smallest to the left,
		 * largest to the right.
		 */
		if (sector > blk_rq_pos(cfqq->next_rq))
			n = &(*p)->rb_right;
		else if (sector < blk_rq_pos(cfqq->next_rq))
			n = &(*p)->rb_left;
		else
			break;
		p = n;
		cfqq = NULL;
	}

	*ret_parent = parent;
	if (rb_link)
		*rb_link = p;
	return cfqq;
}

static void cfq_prio_tree_add(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	struct rb_node **p, *parent;
	struct cfq_queue *__cfqq;

	if (cfqq->p_root) {
		rb_erase(&cfqq->p_node, cfqq->p_root);
		cfqq->p_root = NULL;
	}

	if (cfq_class_idle(cfqq))
		return;
	if (!cfqq->next_rq)
		return;

	cfqq->p_root = &cfqd->prio_trees[cfqq->org_ioprio];
	__cfqq = cfq_prio_tree_lookup(cfqd, cfqq->p_root,
				      blk_rq_pos(cfqq->next_rq), &parent, &p);
	if (!__cfqq) {
		rb_link_node(&cfqq->p_node, parent, p);
		rb_insert_color(&cfqq->p_node, cfqq->p_root);
	} else
		cfqq->p_root = NULL;
}

/*
 * Update cfqq's position in the service tree.
 */
static void cfq_resort_rr_list(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	/*
	 * Resorting requires the cfqq to be on the RR list already.
	 */
	if (cfq_cfqq_on_rr(cfqq)) {
		cfq_service_tree_add(cfqd, cfqq, 0);
		cfq_prio_tree_add(cfqd, cfqq);
	}
}

/*
 * add to busy list of queues for service, trying to be fair in ordering
 * the pending list according to last request service
 */
static void cfq_add_cfqq_rr(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	cfq_log_cfqq(cfqd, cfqq, "add_to_rr");
	BUG_ON(cfq_cfqq_on_rr(cfqq));
	cfq_mark_cfqq_on_rr(cfqq);
	cfqd->busy_queues++;
	if (cfq_cfqq_sync(cfqq))
		cfqd->busy_sync_queues++;

	cfq_resort_rr_list(cfqd, cfqq);
}

/*
 * Called when the cfqq no longer has requests pending, remove it from
 * the service tree.
 */
static void cfq_del_cfqq_rr(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	cfq_log_cfqq(cfqd, cfqq, "del_from_rr");
	BUG_ON(!cfq_cfqq_on_rr(cfqq));
	cfq_clear_cfqq_on_rr(cfqq);

	if (!RB_EMPTY_NODE(&cfqq->rb_node)) {
		cfq_rb_erase(&cfqq->rb_node, cfqq->service_tree);
		cfqq->service_tree = NULL;
	}
	if (cfqq->p_root) {
		rb_erase(&cfqq->p_node, cfqq->p_root);
		cfqq->p_root = NULL;
	}

	cfq_group_notify_queue_del(cfqd, cfqq->cfqg);
	BUG_ON(!cfqd->busy_queues);
	cfqd->busy_queues--;
	if (cfq_cfqq_sync(cfqq))
		cfqd->busy_sync_queues--;
}

/*
 * rb tree support functions
 */
static void cfq_del_rq_rb(struct request *rq)
{
	struct cfq_queue *cfqq = RQ_CFQQ(rq);
	const int sync = rq_is_sync(rq);

	BUG_ON(!cfqq->queued[sync]);
	cfqq->queued[sync]--;

	elv_rb_del(&cfqq->sort_list, rq);

	if (cfq_cfqq_on_rr(cfqq) && RB_EMPTY_ROOT(&cfqq->sort_list)) {
		/*
		 * Queue will be deleted from service tree when we actually
		 * expire it later. Right now just remove it from prio tree
		 * as it is empty.
		 */
		if (cfqq->p_root) {
			rb_erase(&cfqq->p_node, cfqq->p_root);
			cfqq->p_root = NULL;
		}
	}
}

static void cfq_add_rq_rb(struct request *rq)
{
	struct cfq_queue *cfqq = RQ_CFQQ(rq);
	struct cfq_data *cfqd = cfqq->cfqd;
	struct request *prev;

	cfqq->queued[rq_is_sync(rq)]++;

	elv_rb_add(&cfqq->sort_list, rq);

	if (!cfq_cfqq_on_rr(cfqq))
		cfq_add_cfqq_rr(cfqd, cfqq);

	/*
	 * check if this request is a better next-serve candidate
	 */
	prev = cfqq->next_rq;
	cfqq->next_rq = cfq_choose_req(cfqd, cfqq->next_rq, rq, cfqd->last_position);

	/*
	 * adjust priority tree position, if ->next_rq changes
	 */
	if (prev != cfqq->next_rq)
		cfq_prio_tree_add(cfqd, cfqq);

	BUG_ON(!cfqq->next_rq);
}

static void cfq_reposition_rq_rb(struct cfq_queue *cfqq, struct request *rq)
{
	elv_rb_del(&cfqq->sort_list, rq);
	cfqq->queued[rq_is_sync(rq)]--;
	cfqg_stats_update_io_remove(RQ_CFQG(rq), rq->cmd_flags);
	cfq_add_rq_rb(rq);
	cfqg_stats_update_io_add(RQ_CFQG(rq), cfqq->cfqd->serving_group,
				 rq->cmd_flags);
}

static struct request *
cfq_find_rq_fmerge(struct cfq_data *cfqd, struct bio *bio)
{
	struct task_struct *tsk = current;
	struct cfq_io_cq *cic;
	struct cfq_queue *cfqq;

	cic = cfq_cic_lookup(cfqd, tsk->io_context);
	if (!cic)
		return NULL;

	cfqq = cic_to_cfqq(cic, cfq_bio_sync(bio));
	if (cfqq)
		return elv_rb_find(&cfqq->sort_list, bio_end_sector(bio));

	return NULL;
}

static void cfq_activate_request(struct request_queue *q, struct request *rq)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;

	cfqd->rq_in_driver++;
	cfq_log_cfqq(cfqd, RQ_CFQQ(rq), "activate rq, drv=%d",
						cfqd->rq_in_driver);

	cfqd->last_position = blk_rq_pos(rq) + blk_rq_sectors(rq);
}

static void cfq_deactivate_request(struct request_queue *q, struct request *rq)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;

	WARN_ON(!cfqd->rq_in_driver);
	cfqd->rq_in_driver--;
	cfq_log_cfqq(cfqd, RQ_CFQQ(rq), "deactivate rq, drv=%d",
						cfqd->rq_in_driver);
}

static void cfq_remove_request(struct request *rq)
{
	struct cfq_queue *cfqq = RQ_CFQQ(rq);

	if (cfqq->next_rq == rq)
		cfqq->next_rq = cfq_find_next_rq(cfqq->cfqd, cfqq, rq);

	list_del_init(&rq->queuelist);
	cfq_del_rq_rb(rq);

	cfqq->cfqd->rq_queued--;
	cfqg_stats_update_io_remove(RQ_CFQG(rq), rq->cmd_flags);
	if (rq->cmd_flags & REQ_PRIO) {
		WARN_ON(!cfqq->prio_pending);
		cfqq->prio_pending--;
	}
}

static int cfq_merge(struct request_queue *q, struct request **req,
		     struct bio *bio)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct request *__rq;

	__rq = cfq_find_rq_fmerge(cfqd, bio);
	if (__rq && elv_rq_merge_ok(__rq, bio)) {
		*req = __rq;
		return ELEVATOR_FRONT_MERGE;
	}

	return ELEVATOR_NO_MERGE;
}

static void cfq_merged_request(struct request_queue *q, struct request *req,
			       int type)
{
	if (type == ELEVATOR_FRONT_MERGE) {
		struct cfq_queue *cfqq = RQ_CFQQ(req);

		cfq_reposition_rq_rb(cfqq, req);
	}
}

static void cfq_bio_merged(struct request_queue *q, struct request *req,
				struct bio *bio)
{
	cfqg_stats_update_io_merged(RQ_CFQG(req), bio->bi_rw);
}

static void
cfq_merged_requests(struct request_queue *q, struct request *rq,
		    struct request *next)
{
	struct cfq_queue *cfqq = RQ_CFQQ(rq);
	struct cfq_data *cfqd = q->elevator->elevator_data;

	/*
	 * reposition in fifo if next is older than rq
	 */
	if (!list_empty(&rq->queuelist) && !list_empty(&next->queuelist) &&
	    time_before(next->fifo_time, rq->fifo_time) &&
	    cfqq == RQ_CFQQ(next)) {
		list_move(&rq->queuelist, &next->queuelist);
		rq->fifo_time = next->fifo_time;
	}

	if (cfqq->next_rq == next)
		cfqq->next_rq = rq;
	cfq_remove_request(next);
	cfqg_stats_update_io_merged(RQ_CFQG(rq), next->cmd_flags);

	cfqq = RQ_CFQQ(next);
	/*
	 * all requests of this queue are merged to other queues, delete it
	 * from the service tree. If it's the active_queue,
	 * cfq_dispatch_requests() will choose to expire it or do idle
	 */
	if (cfq_cfqq_on_rr(cfqq) && RB_EMPTY_ROOT(&cfqq->sort_list) &&
	    cfqq != cfqd->active_queue)
		cfq_del_cfqq_rr(cfqd, cfqq);
}

static int cfq_allow_merge(struct request_queue *q, struct request *rq,
			   struct bio *bio)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_io_cq *cic;
	struct cfq_queue *cfqq;

	/*
	 * Disallow merge of a sync bio into an async request.
	 */
	if (cfq_bio_sync(bio) && !rq_is_sync(rq))
		return false;

	/*
	 * Lookup the cfqq that this bio will be queued with and allow
	 * merge only if rq is queued there.
	 */
	cic = cfq_cic_lookup(cfqd, current->io_context);
	if (!cic)
		return false;

	cfqq = cic_to_cfqq(cic, cfq_bio_sync(bio));
	return cfqq == RQ_CFQQ(rq);
}

static inline void cfq_del_timer(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	del_timer(&cfqd->idle_slice_timer);
	cfqg_stats_update_idle_time(cfqq->cfqg);
}

static void __cfq_set_active_queue(struct cfq_data *cfqd,
				   struct cfq_queue *cfqq)
{
	if (cfqq) {
		cfq_log_cfqq(cfqd, cfqq, "set_active wl_class:%d wl_type:%d",
				cfqd->serving_wl_class, cfqd->serving_wl_type);
		cfqg_stats_update_avg_queue_size(cfqq->cfqg);
		cfqq->slice_start = 0;
		cfqq->dispatch_start = jiffies;
		cfqq->allocated_slice = 0;
		cfqq->slice_end = 0;
		cfqq->slice_dispatch = 0;
		cfqq->nr_sectors = 0;

		cfq_clear_cfqq_wait_request(cfqq);
		cfq_clear_cfqq_must_dispatch(cfqq);
		cfq_clear_cfqq_must_alloc_slice(cfqq);
		cfq_clear_cfqq_fifo_expire(cfqq);
		cfq_mark_cfqq_slice_new(cfqq);

		cfq_del_timer(cfqd, cfqq);
	}

	cfqd->active_queue = cfqq;
}

/*
 * current cfqq expired its slice (or was too idle), select new one
 */
static void
__cfq_slice_expired(struct cfq_data *cfqd, struct cfq_queue *cfqq,
		    bool timed_out)
{
	cfq_log_cfqq(cfqd, cfqq, "slice expired t=%d", timed_out);

	if (cfq_cfqq_wait_request(cfqq))
		cfq_del_timer(cfqd, cfqq);

	cfq_clear_cfqq_wait_request(cfqq);
	cfq_clear_cfqq_wait_busy(cfqq);

	/*
	 * If this cfqq is shared between multiple processes, check to
	 * make sure that those processes are still issuing I/Os within
	 * the mean seek distance.  If not, it may be time to break the
	 * queues apart again.
	 */
	if (cfq_cfqq_coop(cfqq) && CFQQ_SEEKY(cfqq))
		cfq_mark_cfqq_split_coop(cfqq);

	/*
	 * store what was left of this slice, if the queue idled/timed out
	 */
	if (timed_out) {
		if (cfq_cfqq_slice_new(cfqq))
			cfqq->slice_resid = cfq_scaled_cfqq_slice(cfqd, cfqq);
		else
			cfqq->slice_resid = cfqq->slice_end - jiffies;
		cfq_log_cfqq(cfqd, cfqq, "resid=%ld", cfqq->slice_resid);
	}

	cfq_group_served(cfqd, cfqq->cfqg, cfqq);

	if (cfq_cfqq_on_rr(cfqq) && RB_EMPTY_ROOT(&cfqq->sort_list))
		cfq_del_cfqq_rr(cfqd, cfqq);

	cfq_resort_rr_list(cfqd, cfqq);

	if (cfqq == cfqd->active_queue)
		cfqd->active_queue = NULL;

	if (cfqd->active_cic) {
		put_io_context(cfqd->active_cic->icq.ioc);
		cfqd->active_cic = NULL;
	}
}

static inline void cfq_slice_expired(struct cfq_data *cfqd, bool timed_out)
{
	struct cfq_queue *cfqq = cfqd->active_queue;

	if (cfqq)
		__cfq_slice_expired(cfqd, cfqq, timed_out);
}

/*
 * Get next queue for service. Unless we have a queue preemption,
 * we'll simply select the first cfqq in the service tree.
 */
static struct cfq_queue *cfq_get_next_queue(struct cfq_data *cfqd)
{
	struct cfq_rb_root *st = st_for(cfqd->serving_group,
			cfqd->serving_wl_class, cfqd->serving_wl_type);

	if (!cfqd->rq_queued)
		return NULL;

	/* There is nothing to dispatch */
	if (!st)
		return NULL;
	if (RB_EMPTY_ROOT(&st->rb))
		return NULL;
	return cfq_rb_first(st);
}

static struct cfq_queue *cfq_get_next_queue_forced(struct cfq_data *cfqd)
{
	struct cfq_group *cfqg;
	struct cfq_queue *cfqq;
	int i, j;
	struct cfq_rb_root *st;

	if (!cfqd->rq_queued)
		return NULL;

	cfqg = cfq_get_next_cfqg(cfqd);
	if (!cfqg)
		return NULL;

	for_each_cfqg_st(cfqg, i, j, st)
		if ((cfqq = cfq_rb_first(st)) != NULL)
			return cfqq;
	return NULL;
}

/*
 * Get and set a new active queue for service.
 */
static struct cfq_queue *cfq_set_active_queue(struct cfq_data *cfqd,
					      struct cfq_queue *cfqq)
{
	if (!cfqq)
		cfqq = cfq_get_next_queue(cfqd);

	__cfq_set_active_queue(cfqd, cfqq);
	return cfqq;
}

static inline sector_t cfq_dist_from_last(struct cfq_data *cfqd,
					  struct request *rq)
{
	if (blk_rq_pos(rq) >= cfqd->last_position)
		return blk_rq_pos(rq) - cfqd->last_position;
	else
		return cfqd->last_position - blk_rq_pos(rq);
}

static inline int cfq_rq_close(struct cfq_data *cfqd, struct cfq_queue *cfqq,
			       struct request *rq)
{
	return cfq_dist_from_last(cfqd, rq) <= CFQQ_CLOSE_THR;
}

static struct cfq_queue *cfqq_close(struct cfq_data *cfqd,
				    struct cfq_queue *cur_cfqq)
{
	struct rb_root *root = &cfqd->prio_trees[cur_cfqq->org_ioprio];
	struct rb_node *parent, *node;
	struct cfq_queue *__cfqq;
	sector_t sector = cfqd->last_position;

	if (RB_EMPTY_ROOT(root))
		return NULL;

	/*
	 * First, if we find a request starting at the end of the last
	 * request, choose it.
	 */
	__cfqq = cfq_prio_tree_lookup(cfqd, root, sector, &parent, NULL);
	if (__cfqq)
		return __cfqq;

	/*
	 * If the exact sector wasn't found, the parent of the NULL leaf
	 * will contain the closest sector.
	 */
	__cfqq = rb_entry(parent, struct cfq_queue, p_node);
	if (cfq_rq_close(cfqd, cur_cfqq, __cfqq->next_rq))
		return __cfqq;

	if (blk_rq_pos(__cfqq->next_rq) < sector)
		node = rb_next(&__cfqq->p_node);
	else
		node = rb_prev(&__cfqq->p_node);
	if (!node)
		return NULL;

	__cfqq = rb_entry(node, struct cfq_queue, p_node);
	if (cfq_rq_close(cfqd, cur_cfqq, __cfqq->next_rq))
		return __cfqq;

	return NULL;
}

/*
 * cfqd - obvious
 * cur_cfqq - passed in so that we don't decide that the current queue is
 * 	      closely cooperating with itself.
 *
 * So, basically we're assuming that that cur_cfqq has dispatched at least
 * one request, and that cfqd->last_position reflects a position on the disk
 * associated with the I/O issued by cur_cfqq.  I'm not sure this is a valid
 * assumption.
 */
static struct cfq_queue *cfq_close_cooperator(struct cfq_data *cfqd,
					      struct cfq_queue *cur_cfqq)
{
	struct cfq_queue *cfqq;

	if (cfq_class_idle(cur_cfqq))
		return NULL;
	if (!cfq_cfqq_sync(cur_cfqq))
		return NULL;
	if (CFQQ_SEEKY(cur_cfqq))
		return NULL;

	/*
	 * Don't search priority tree if it's the only queue in the group.
	 */
	if (cur_cfqq->cfqg->nr_cfqq == 1)
		return NULL;

	/*
	 * We should notice if some of the queues are cooperating, eg
	 * working closely on the same area of the disk. In that case,
	 * we can group them together and don't waste time idling.
	 */
	cfqq = cfqq_close(cfqd, cur_cfqq);
	if (!cfqq)
		return NULL;

	/* If new queue belongs to different cfq_group, don't choose it */
	if (cur_cfqq->cfqg != cfqq->cfqg)
		return NULL;

	/*
	 * It only makes sense to merge sync queues.
	 */
	if (!cfq_cfqq_sync(cfqq))
		return NULL;
	if (CFQQ_SEEKY(cfqq))
		return NULL;

	/*
	 * Do not merge queues of different priority classes
	 */
	if (cfq_class_rt(cfqq) != cfq_class_rt(cur_cfqq))
		return NULL;

	return cfqq;
}

/*
 * Determine whether we should enforce idle window for this queue.
 */

static bool cfq_should_idle(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	enum wl_class_t wl_class = cfqq_class(cfqq);
	struct cfq_rb_root *st = cfqq->service_tree;

	BUG_ON(!st);
	BUG_ON(!st->count);

	if (!cfqd->cfq_slice_idle)
		return false;

	/* We never do for idle class queues. */
	if (wl_class == IDLE_WORKLOAD)
		return false;

	/* We do for queues that were marked with idle window flag. */
	if (cfq_cfqq_idle_window(cfqq) &&
	   !(blk_queue_nonrot(cfqd->queue) && cfqd->hw_tag))
		return true;

	/*
	 * Otherwise, we do only if they are the last ones
	 * in their service tree.
	 */
	if (st->count == 1 && cfq_cfqq_sync(cfqq) &&
	   !cfq_io_thinktime_big(cfqd, &st->ttime, false))
		return true;
	cfq_log_cfqq(cfqd, cfqq, "Not idling. st->count:%d", st->count);
	return false;
}

static void cfq_arm_slice_timer(struct cfq_data *cfqd)
{
	struct cfq_queue *cfqq = cfqd->active_queue;
	struct cfq_io_cq *cic;
	unsigned long sl, group_idle = 0;

	/*
	 * SSD device without seek penalty, disable idling. But only do so
	 * for devices that support queuing, otherwise we still have a problem
	 * with sync vs async workloads.
	 */
	if (blk_queue_nonrot(cfqd->queue) && cfqd->hw_tag)
		return;

	WARN_ON(!RB_EMPTY_ROOT(&cfqq->sort_list));
	WARN_ON(cfq_cfqq_slice_new(cfqq));

	/*
	 * idle is disabled, either manually or by past process history
	 */
	if (!cfq_should_idle(cfqd, cfqq)) {
		/* no queue idling. Check for group idling */
		if (cfqd->cfq_group_idle)
			group_idle = cfqd->cfq_group_idle;
		else
			return;
	}

	/*
	 * still active requests from this queue, don't idle
	 */
	if (cfqq->dispatched)
		return;

	/*
	 * task has exited, don't wait
	 */
	cic = cfqd->active_cic;
	if (!cic || !atomic_read(&cic->icq.ioc->active_ref))
		return;

	/*
	 * If our average think time is larger than the remaining time
	 * slice, then don't idle. This avoids overrunning the allotted
	 * time slice.
	 */
	if (sample_valid(cic->ttime.ttime_samples) &&
	    (cfqq->slice_end - jiffies < cic->ttime.ttime_mean)) {
		cfq_log_cfqq(cfqd, cfqq, "Not idling. think_time:%lu",
			     cic->ttime.ttime_mean);
		return;
	}

	/* There are other queues in the group, don't do group idle */
	if (group_idle && cfqq->cfqg->nr_cfqq > 1)
		return;

	cfq_mark_cfqq_wait_request(cfqq);

	if (group_idle)
		sl = cfqd->cfq_group_idle;
	else
		sl = cfqd->cfq_slice_idle;

	mod_timer(&cfqd->idle_slice_timer, jiffies + sl);
	cfqg_stats_set_start_idle_time(cfqq->cfqg);
	cfq_log_cfqq(cfqd, cfqq, "arm_idle: %lu group_idle: %d", sl,
			group_idle ? 1 : 0);
}

/*
 * Move request from internal lists to the request queue dispatch list.
 */
static void cfq_dispatch_insert(struct request_queue *q, struct request *rq)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_queue *cfqq = RQ_CFQQ(rq);

	cfq_log_cfqq(cfqd, cfqq, "dispatch_insert");

	cfqq->next_rq = cfq_find_next_rq(cfqd, cfqq, rq);
	cfq_remove_request(rq);
	cfqq->dispatched++;
	(RQ_CFQG(rq))->dispatched++;
	elv_dispatch_sort(q, rq);

	cfqd->rq_in_flight[cfq_cfqq_sync(cfqq)]++;
	cfqq->nr_sectors += blk_rq_sectors(rq);
}

/*
 * return expired entry, or NULL to just start from scratch in rbtree
 */
static struct request *cfq_check_fifo(struct cfq_queue *cfqq)
{
	struct request *rq = NULL;

	if (cfq_cfqq_fifo_expire(cfqq))
		return NULL;

	cfq_mark_cfqq_fifo_expire(cfqq);

	if (list_empty(&cfqq->fifo))
		return NULL;

	rq = rq_entry_fifo(cfqq->fifo.next);
	if (time_before(jiffies, rq->fifo_time))
		rq = NULL;

	return rq;
}

static inline int
cfq_prio_to_maxrq(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	const int base_rq = cfqd->cfq_slice_async_rq;

	WARN_ON(cfqq->ioprio >= IOPRIO_BE_NR);

	return 2 * base_rq * (IOPRIO_BE_NR - cfqq->ioprio);
}

/*
 * Must be called with the queue_lock held.
 */
static int cfqq_process_refs(struct cfq_queue *cfqq)
{
	int process_refs, io_refs;

	io_refs = cfqq->allocated[READ] + cfqq->allocated[WRITE];
	process_refs = cfqq->ref - io_refs;
	BUG_ON(process_refs < 0);
	return process_refs;
}

static void cfq_setup_merge(struct cfq_queue *cfqq, struct cfq_queue *new_cfqq)
{
	int process_refs, new_process_refs;
	struct cfq_queue *__cfqq;

	/*
	 * If there are no process references on the new_cfqq, then it is
	 * unsafe to follow the ->new_cfqq chain as other cfqq's in the
	 * chain may have dropped their last reference (not just their
	 * last process reference).
	 */
	if (!cfqq_process_refs(new_cfqq))
		return;

	/* Avoid a circular list and skip interim queue merges */
	while ((__cfqq = new_cfqq->new_cfqq)) {
		if (__cfqq == cfqq)
			return;
		new_cfqq = __cfqq;
	}

	process_refs = cfqq_process_refs(cfqq);
	new_process_refs = cfqq_process_refs(new_cfqq);
	/*
	 * If the process for the cfqq has gone away, there is no
	 * sense in merging the queues.
	 */
	if (process_refs == 0 || new_process_refs == 0)
		return;

	/*
	 * Merge in the direction of the lesser amount of work.
	 */
	if (new_process_refs >= process_refs) {
		cfqq->new_cfqq = new_cfqq;
		new_cfqq->ref += process_refs;
	} else {
		new_cfqq->new_cfqq = cfqq;
		cfqq->ref += new_process_refs;
	}
}

static enum wl_type_t cfq_choose_wl_type(struct cfq_data *cfqd,
			struct cfq_group *cfqg, enum wl_class_t wl_class)
{
	struct cfq_queue *queue;
	int i;
	bool key_valid = false;
	unsigned long lowest_key = 0;
	enum wl_type_t cur_best = SYNC_NOIDLE_WORKLOAD;

	for (i = 0; i <= SYNC_WORKLOAD; ++i) {
		/* select the one with lowest rb_key */
		queue = cfq_rb_first(st_for(cfqg, wl_class, i));
		if (queue &&
		    (!key_valid || time_before(queue->rb_key, lowest_key))) {
			lowest_key = queue->rb_key;
			cur_best = i;
			key_valid = true;
		}
	}

	return cur_best;
}

static void
choose_wl_class_and_type(struct cfq_data *cfqd, struct cfq_group *cfqg)
{
	unsigned slice;
	unsigned count;
	struct cfq_rb_root *st;
	unsigned group_slice;
	enum wl_class_t original_class = cfqd->serving_wl_class;

	/* Choose next priority. RT > BE > IDLE */
	if (cfq_group_busy_queues_wl(RT_WORKLOAD, cfqd, cfqg))
		cfqd->serving_wl_class = RT_WORKLOAD;
	else if (cfq_group_busy_queues_wl(BE_WORKLOAD, cfqd, cfqg))
		cfqd->serving_wl_class = BE_WORKLOAD;
	else {
		cfqd->serving_wl_class = IDLE_WORKLOAD;
		cfqd->workload_expires = jiffies + 1;
		return;
	}

	if (original_class != cfqd->serving_wl_class)
		goto new_workload;

	/*
	 * For RT and BE, we have to choose also the type
	 * (SYNC, SYNC_NOIDLE, ASYNC), and to compute a workload
	 * expiration time
	 */
	st = st_for(cfqg, cfqd->serving_wl_class, cfqd->serving_wl_type);
	count = st->count;

	/*
	 * check workload expiration, and that we still have other queues ready
	 */
	if (count && !time_after(jiffies, cfqd->workload_expires))
		return;

new_workload:
	/* otherwise select new workload type */
	cfqd->serving_wl_type = cfq_choose_wl_type(cfqd, cfqg,
					cfqd->serving_wl_class);
	st = st_for(cfqg, cfqd->serving_wl_class, cfqd->serving_wl_type);
	count = st->count;

	/*
	 * the workload slice is computed as a fraction of target latency
	 * proportional to the number of queues in that workload, over
	 * all the queues in the same priority class
	 */
	group_slice = cfq_group_slice(cfqd, cfqg);

	slice = group_slice * count /
		max_t(unsigned, cfqg->busy_queues_avg[cfqd->serving_wl_class],
		      cfq_group_busy_queues_wl(cfqd->serving_wl_class, cfqd,
					cfqg));

	if (cfqd->serving_wl_type == ASYNC_WORKLOAD) {
		unsigned int tmp;

		/*
		 * Async queues are currently system wide. Just taking
		 * proportion of queues with-in same group will lead to higher
		 * async ratio system wide as generally root group is going
		 * to have higher weight. A more accurate thing would be to
		 * calculate system wide asnc/sync ratio.
		 */
		tmp = cfqd->cfq_target_latency *
			cfqg_busy_async_queues(cfqd, cfqg);
		tmp = tmp/cfqd->busy_queues;
		slice = min_t(unsigned, slice, tmp);

		/* async workload slice is scaled down according to
		 * the sync/async slice ratio. */
		slice = slice * cfqd->cfq_slice[0] / cfqd->cfq_slice[1];
	} else
		/* sync workload slice is at least 2 * cfq_slice_idle */
		slice = max(slice, 2 * cfqd->cfq_slice_idle);

	slice = max_t(unsigned, slice, CFQ_MIN_TT);
	cfq_log(cfqd, "workload slice:%d", slice);
	cfqd->workload_expires = jiffies + slice;
}

static struct cfq_group *cfq_get_next_cfqg(struct cfq_data *cfqd)
{
	struct cfq_rb_root *st = &cfqd->grp_service_tree;
	struct cfq_group *cfqg;

	if (RB_EMPTY_ROOT(&st->rb))
		return NULL;
	cfqg = cfq_rb_first_group(st);
	update_min_vdisktime(st);
	return cfqg;
}

static void cfq_choose_cfqg(struct cfq_data *cfqd)
{
	struct cfq_group *cfqg = cfq_get_next_cfqg(cfqd);

	cfqd->serving_group = cfqg;

	/* Restore the workload type data */
	if (cfqg->saved_wl_slice) {
		cfqd->workload_expires = jiffies + cfqg->saved_wl_slice;
		cfqd->serving_wl_type = cfqg->saved_wl_type;
		cfqd->serving_wl_class = cfqg->saved_wl_class;
	} else
		cfqd->workload_expires = jiffies - 1;

	choose_wl_class_and_type(cfqd, cfqg);
}

/*
 * Select a queue for service. If we have a current active queue,
 * check whether to continue servicing it, or retrieve and set a new one.
 */
static struct cfq_queue *cfq_select_queue(struct cfq_data *cfqd)
{
	struct cfq_queue *cfqq, *new_cfqq = NULL;

	cfqq = cfqd->active_queue;
	if (!cfqq)
		goto new_queue;

	if (!cfqd->rq_queued)
		return NULL;

	/*
	 * We were waiting for group to get backlogged. Expire the queue
	 */
	if (cfq_cfqq_wait_busy(cfqq) && !RB_EMPTY_ROOT(&cfqq->sort_list))
		goto expire;

	/*
	 * The active queue has run out of time, expire it and select new.
	 */
	if (cfq_slice_used(cfqq) && !cfq_cfqq_must_dispatch(cfqq)) {
		/*
		 * If slice had not expired at the completion of last request
		 * we might not have turned on wait_busy flag. Don't expire
		 * the queue yet. Allow the group to get backlogged.
		 *
		 * The very fact that we have used the slice, that means we
		 * have been idling all along on this queue and it should be
		 * ok to wait for this request to complete.
		 */
		if (cfqq->cfqg->nr_cfqq == 1 && RB_EMPTY_ROOT(&cfqq->sort_list)
		    && cfqq->dispatched && cfq_should_idle(cfqd, cfqq)) {
			cfqq = NULL;
			goto keep_queue;
		} else
			goto check_group_idle;
	}

	/*
	 * The active queue has requests and isn't expired, allow it to
	 * dispatch.
	 */
	if (!RB_EMPTY_ROOT(&cfqq->sort_list))
		goto keep_queue;

	/*
	 * If another queue has a request waiting within our mean seek
	 * distance, let it run.  The expire code will check for close
	 * cooperators and put the close queue at the front of the service
	 * tree.  If possible, merge the expiring queue with the new cfqq.
	 */
	new_cfqq = cfq_close_cooperator(cfqd, cfqq);
	if (new_cfqq) {
		if (!cfqq->new_cfqq)
			cfq_setup_merge(cfqq, new_cfqq);
		goto expire;
	}

	/*
	 * No requests pending. If the active queue still has requests in
	 * flight or is idling for a new request, allow either of these
	 * conditions to happen (or time out) before selecting a new queue.
	 */
	if (timer_pending(&cfqd->idle_slice_timer)) {
		cfqq = NULL;
		goto keep_queue;
	}

	/*
	 * This is a deep seek queue, but the device is much faster than
	 * the queue can deliver, don't idle
	 **/
	if (CFQQ_SEEKY(cfqq) && cfq_cfqq_idle_window(cfqq) &&
	    (cfq_cfqq_slice_new(cfqq) ||
	    (cfqq->slice_end - jiffies > jiffies - cfqq->slice_start))) {
		cfq_clear_cfqq_deep(cfqq);
		cfq_clear_cfqq_idle_window(cfqq);
	}

	if (cfqq->dispatched && cfq_should_idle(cfqd, cfqq)) {
		cfqq = NULL;
		goto keep_queue;
	}

	/*
	 * If group idle is enabled and there are requests dispatched from
	 * this group, wait for requests to complete.
	 */
check_group_idle:
	if (cfqd->cfq_group_idle && cfqq->cfqg->nr_cfqq == 1 &&
	    cfqq->cfqg->dispatched &&
	    !cfq_io_thinktime_big(cfqd, &cfqq->cfqg->ttime, true)) {
		cfqq = NULL;
		goto keep_queue;
	}

expire:
	cfq_slice_expired(cfqd, 0);
new_queue:
	/*
	 * Current queue expired. Check if we have to switch to a new
	 * service tree
	 */
	if (!new_cfqq)
		cfq_choose_cfqg(cfqd);

	cfqq = cfq_set_active_queue(cfqd, new_cfqq);
keep_queue:
	return cfqq;
}

static int __cfq_forced_dispatch_cfqq(struct cfq_queue *cfqq)
{
	int dispatched = 0;

	while (cfqq->next_rq) {
		cfq_dispatch_insert(cfqq->cfqd->queue, cfqq->next_rq);
		dispatched++;
	}

	BUG_ON(!list_empty(&cfqq->fifo));

	/* By default cfqq is not expired if it is empty. Do it explicitly */
	__cfq_slice_expired(cfqq->cfqd, cfqq, 0);
	return dispatched;
}

/*
 * Drain our current requests. Used for barriers and when switching
 * io schedulers on-the-fly.
 */
static int cfq_forced_dispatch(struct cfq_data *cfqd)
{
	struct cfq_queue *cfqq;
	int dispatched = 0;

	/* Expire the timeslice of the current active queue first */
	cfq_slice_expired(cfqd, 0);
	while ((cfqq = cfq_get_next_queue_forced(cfqd)) != NULL) {
		__cfq_set_active_queue(cfqd, cfqq);
		dispatched += __cfq_forced_dispatch_cfqq(cfqq);
	}

	BUG_ON(cfqd->busy_queues);

	cfq_log(cfqd, "forced_dispatch=%d", dispatched);
	return dispatched;
}

static inline bool cfq_slice_used_soon(struct cfq_data *cfqd,
	struct cfq_queue *cfqq)
{
	/* the queue hasn't finished any request, can't estimate */
	if (cfq_cfqq_slice_new(cfqq))
		return true;
	if (time_after(jiffies + cfqd->cfq_slice_idle * cfqq->dispatched,
		cfqq->slice_end))
		return true;

	return false;
}

static bool cfq_may_dispatch(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	unsigned int max_dispatch;

	if (cfq_cfqq_must_dispatch(cfqq))
		return true;

	/*
	 * Drain async requests before we start sync IO
	 */
	if (cfq_should_idle(cfqd, cfqq) && cfqd->rq_in_flight[BLK_RW_ASYNC])
		return false;

	/*
	 * If this is an async queue and we have sync IO in flight, let it wait
	 */
	if (cfqd->rq_in_flight[BLK_RW_SYNC] && !cfq_cfqq_sync(cfqq))
		return false;

	max_dispatch = max_t(unsigned int, cfqd->cfq_quantum / 2, 1);
	if (cfq_class_idle(cfqq))
		max_dispatch = 1;

	/*
	 * Does this cfqq already have too much IO in flight?
	 */
	if (cfqq->dispatched >= max_dispatch) {
		bool promote_sync = false;
		/*
		 * idle queue must always only have a single IO in flight
		 */
		if (cfq_class_idle(cfqq))
			return false;

		/*
		 * If there is only one sync queue
		 * we can ignore async queue here and give the sync
		 * queue no dispatch limit. The reason is a sync queue can
		 * preempt async queue, limiting the sync queue doesn't make
		 * sense. This is useful for aiostress test.
		 */
		if (cfq_cfqq_sync(cfqq) && cfqd->busy_sync_queues == 1)
			promote_sync = true;

		/*
		 * We have other queues, don't allow more IO from this one
		 */
		if (cfqd->busy_queues > 1 && cfq_slice_used_soon(cfqd, cfqq) &&
				!promote_sync)
			return false;

		/*
		 * Sole queue user, no limit
		 */
		if (cfqd->busy_queues == 1 || promote_sync)
			max_dispatch = -1;
		else
			/*
			 * Normally we start throttling cfqq when cfq_quantum/2
			 * requests have been dispatched. But we can drive
			 * deeper queue depths at the beginning of slice
			 * subjected to upper limit of cfq_quantum.
			 * */
			max_dispatch = cfqd->cfq_quantum;
	}

	/*
	 * Async queues must wait a bit before being allowed dispatch.
	 * We also ramp up the dispatch depth gradually for async IO,
	 * based on the last sync IO we serviced
	 */
	if (!cfq_cfqq_sync(cfqq) && cfqd->cfq_latency) {
		unsigned long last_sync = jiffies - cfqd->last_delayed_sync;
		unsigned int depth;

		depth = last_sync / cfqd->cfq_slice[1];
		if (!depth && !cfqq->dispatched)
			depth = 1;
		if (depth < max_dispatch)
			max_dispatch = depth;
	}

	/*
	 * If we're below the current max, allow a dispatch
	 */
	return cfqq->dispatched < max_dispatch;
}

/*
 * Dispatch a request from cfqq, moving them to the request queue
 * dispatch list.
 */
static bool cfq_dispatch_request(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	struct request *rq;

	BUG_ON(RB_EMPTY_ROOT(&cfqq->sort_list));

	rq = cfq_check_fifo(cfqq);
	if (rq)
		cfq_mark_cfqq_must_dispatch(cfqq);

	if (!cfq_may_dispatch(cfqd, cfqq))
		return false;

	/*
	 * follow expired path, else get first next available
	 */
	if (!rq)
		rq = cfqq->next_rq;
	else
		cfq_log_cfqq(cfqq->cfqd, cfqq, "fifo=%p", rq);

	/*
	 * insert request into driver dispatch list
	 */
	cfq_dispatch_insert(cfqd->queue, rq);

	if (!cfqd->active_cic) {
		struct cfq_io_cq *cic = RQ_CIC(rq);

		atomic_long_inc(&cic->icq.ioc->refcount);
		cfqd->active_cic = cic;
	}

	return true;
}

/*
 * Find the cfqq that we need to service and move a request from that to the
 * dispatch list
 */
static int cfq_dispatch_requests(struct request_queue *q, int force)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_queue *cfqq;

	if (!cfqd->busy_queues)
		return 0;

	if (unlikely(force))
		return cfq_forced_dispatch(cfqd);

	cfqq = cfq_select_queue(cfqd);
	if (!cfqq)
		return 0;

	/*
	 * Dispatch a request from this cfqq, if it is allowed
	 */
	if (!cfq_dispatch_request(cfqd, cfqq))
		return 0;

	cfqq->slice_dispatch++;
	cfq_clear_cfqq_must_dispatch(cfqq);

	/*
	 * expire an async queue immediately if it has used up its slice. idle
	 * queue always expire after 1 dispatch round.
	 */
	if (cfqd->busy_queues > 1 && ((!cfq_cfqq_sync(cfqq) &&
	    cfqq->slice_dispatch >= cfq_prio_to_maxrq(cfqd, cfqq)) ||
	    cfq_class_idle(cfqq))) {
		cfqq->slice_end = jiffies + 1;
		cfq_slice_expired(cfqd, 0);
	}

	cfq_log_cfqq(cfqd, cfqq, "dispatched a request");
	return 1;
}

/*
 * task holds one reference to the queue, dropped when task exits. each rq
 * in-flight on this queue also holds a reference, dropped when rq is freed.
 *
 * Each cfq queue took a reference on the parent group. Drop it now.
 * queue lock must be held here.
 */
static void cfq_put_queue(struct cfq_queue *cfqq)
{
	struct cfq_data *cfqd = cfqq->cfqd;
	struct cfq_group *cfqg;

	BUG_ON(cfqq->ref <= 0);

	cfqq->ref--;
	if (cfqq->ref)
		return;

	cfq_log_cfqq(cfqd, cfqq, "put_queue");
	BUG_ON(rb_first(&cfqq->sort_list));
	BUG_ON(cfqq->allocated[READ] + cfqq->allocated[WRITE]);
	cfqg = cfqq->cfqg;

	if (unlikely(cfqd->active_queue == cfqq)) {
		__cfq_slice_expired(cfqd, cfqq, 0);
		cfq_schedule_dispatch(cfqd);
	}

	BUG_ON(cfq_cfqq_on_rr(cfqq));
	kmem_cache_free(cfq_pool, cfqq);
	cfqg_put(cfqg);
}

static void cfq_put_cooperator(struct cfq_queue *cfqq)
{
	struct cfq_queue *__cfqq, *next;

	/*
	 * If this queue was scheduled to merge with another queue, be
	 * sure to drop the reference taken on that queue (and others in
	 * the merge chain).  See cfq_setup_merge and cfq_merge_cfqqs.
	 */
	__cfqq = cfqq->new_cfqq;
	while (__cfqq) {
		if (__cfqq == cfqq) {
			WARN(1, "cfqq->new_cfqq loop detected\n");
			break;
		}
		next = __cfqq->new_cfqq;
		cfq_put_queue(__cfqq);
		__cfqq = next;
	}
}

static void cfq_exit_cfqq(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	if (unlikely(cfqq == cfqd->active_queue)) {
		__cfq_slice_expired(cfqd, cfqq, 0);
		cfq_schedule_dispatch(cfqd);
	}

	cfq_put_cooperator(cfqq);

	cfq_put_queue(cfqq);
}

static void cfq_init_icq(struct io_cq *icq)
{
	struct cfq_io_cq *cic = icq_to_cic(icq);

	cic->ttime.last_end_request = jiffies;
}

static void cfq_exit_icq(struct io_cq *icq)
{
	struct cfq_io_cq *cic = icq_to_cic(icq);
	struct cfq_data *cfqd = cic_to_cfqd(cic);

	if (cic_to_cfqq(cic, false)) {
		cfq_exit_cfqq(cfqd, cic_to_cfqq(cic, false));
		cic_set_cfqq(cic, NULL, false);
	}

	if (cic_to_cfqq(cic, true)) {
		cfq_exit_cfqq(cfqd, cic_to_cfqq(cic, true));
		cic_set_cfqq(cic, NULL, true);
	}
}

static void cfq_init_prio_data(struct cfq_queue *cfqq, struct cfq_io_cq *cic)
{
	struct task_struct *tsk = current;
	int ioprio_class;

	if (!cfq_cfqq_prio_changed(cfqq))
		return;

	ioprio_class = IOPRIO_PRIO_CLASS(cic->ioprio);
	switch (ioprio_class) {
	default:
		printk(KERN_ERR "cfq: bad prio %x\n", ioprio_class);
	case IOPRIO_CLASS_NONE:
		/*
		 * no prio set, inherit CPU scheduling settings
		 */
		cfqq->ioprio = task_nice_ioprio(tsk);
		cfqq->ioprio_class = task_nice_ioclass(tsk);
		break;
	case IOPRIO_CLASS_RT:
		cfqq->ioprio = IOPRIO_PRIO_DATA(cic->ioprio);
		cfqq->ioprio_class = IOPRIO_CLASS_RT;
		break;
	case IOPRIO_CLASS_BE:
		cfqq->ioprio = IOPRIO_PRIO_DATA(cic->ioprio);
		cfqq->ioprio_class = IOPRIO_CLASS_BE;
		break;
	case IOPRIO_CLASS_IDLE:
		cfqq->ioprio_class = IOPRIO_CLASS_IDLE;
		cfqq->ioprio = 7;
		cfq_clear_cfqq_idle_window(cfqq);
		break;
	}

	/*
	 * keep track of original prio settings in case we have to temporarily
	 * elevate the priority of this queue
	 */
	cfqq->org_ioprio = cfqq->ioprio;
	cfq_clear_cfqq_prio_changed(cfqq);
}

static void check_ioprio_changed(struct cfq_io_cq *cic, struct bio *bio)
{
	int ioprio = cic->icq.ioc->ioprio;
	struct cfq_data *cfqd = cic_to_cfqd(cic);
	struct cfq_queue *cfqq;

	/*
	 * Check whether ioprio has changed.  The condition may trigger
	 * spuriously on a newly created cic but there's no harm.
	 */
	if (unlikely(!cfqd) || likely(cic->ioprio == ioprio))
		return;

	cfqq = cic_to_cfqq(cic, false);
	if (cfqq) {
		cfq_put_queue(cfqq);
		cfqq = cfq_get_queue(cfqd, BLK_RW_ASYNC, cic, bio);
		cic_set_cfqq(cic, cfqq, false);
	}

	cfqq = cic_to_cfqq(cic, true);
	if (cfqq)
		cfq_mark_cfqq_prio_changed(cfqq);

	cic->ioprio = ioprio;
}

static void cfq_init_cfqq(struct cfq_data *cfqd, struct cfq_queue *cfqq,
			  pid_t pid, bool is_sync)
{
	RB_CLEAR_NODE(&cfqq->rb_node);
	RB_CLEAR_NODE(&cfqq->p_node);
	INIT_LIST_HEAD(&cfqq->fifo);

	cfqq->ref = 0;
	cfqq->cfqd = cfqd;

	cfq_mark_cfqq_prio_changed(cfqq);

	if (is_sync) {
		if (!cfq_class_idle(cfqq))
			cfq_mark_cfqq_idle_window(cfqq);
		cfq_mark_cfqq_sync(cfqq);
	}
	cfqq->pid = pid;
}

#ifdef CONFIG_CFQ_GROUP_IOSCHED
static void check_blkcg_changed(struct cfq_io_cq *cic, struct bio *bio)
{
	struct cfq_data *cfqd = cic_to_cfqd(cic);
	struct cfq_queue *cfqq;
	uint64_t serial_nr;

	rcu_read_lock();
	serial_nr = bio_blkcg(bio)->css.serial_nr;
	rcu_read_unlock();

	/*
	 * Check whether blkcg has changed.  The condition may trigger
	 * spuriously on a newly created cic but there's no harm.
	 */
	if (unlikely(!cfqd) || likely(cic->blkcg_serial_nr == serial_nr))
		return;

	/*
	 * Drop reference to queues.  New queues will be assigned in new
	 * group upon arrival of fresh requests.
	 */
	cfqq = cic_to_cfqq(cic, false);
	if (cfqq) {
		cfq_log_cfqq(cfqd, cfqq, "changed cgroup");
		cic_set_cfqq(cic, NULL, false);
		cfq_put_queue(cfqq);
	}

	cfqq = cic_to_cfqq(cic, true);
	if (cfqq) {
		cfq_log_cfqq(cfqd, cfqq, "changed cgroup");
		cic_set_cfqq(cic, NULL, true);
		cfq_put_queue(cfqq);
	}

	cic->blkcg_serial_nr = serial_nr;
}
#else
static inline void check_blkcg_changed(struct cfq_io_cq *cic, struct bio *bio) { }
#endif  /* CONFIG_CFQ_GROUP_IOSCHED */

static struct cfq_queue **
cfq_async_queue_prio(struct cfq_group *cfqg, int ioprio_class, int ioprio)
{
	switch (ioprio_class) {
	case IOPRIO_CLASS_RT:
		return &cfqg->async_cfqq[0][ioprio];
	case IOPRIO_CLASS_NONE:
		ioprio = IOPRIO_NORM;
		/* fall through */
	case IOPRIO_CLASS_BE:
		return &cfqg->async_cfqq[1][ioprio];
	case IOPRIO_CLASS_IDLE:
		return &cfqg->async_idle_cfqq;
	default:
		BUG();
	}
}

static struct cfq_queue *
cfq_get_queue(struct cfq_data *cfqd, bool is_sync, struct cfq_io_cq *cic,
	      struct bio *bio)
{
	int ioprio_class = IOPRIO_PRIO_CLASS(cic->ioprio);
	int ioprio = IOPRIO_PRIO_DATA(cic->ioprio);
	struct cfq_queue **async_cfqq = NULL;
	struct cfq_queue *cfqq;
	struct cfq_group *cfqg;

	rcu_read_lock();
	cfqg = cfq_lookup_cfqg(cfqd, bio_blkcg(bio));
	if (!cfqg) {
		cfqq = &cfqd->oom_cfqq;
		goto out;
	}

	if (!is_sync) {
		if (!ioprio_valid(cic->ioprio)) {
			struct task_struct *tsk = current;
			ioprio = task_nice_ioprio(tsk);
			ioprio_class = task_nice_ioclass(tsk);
		}
		async_cfqq = cfq_async_queue_prio(cfqg, ioprio_class, ioprio);
		cfqq = *async_cfqq;
		if (cfqq)
			goto out;
	}

	cfqq = kmem_cache_alloc_node(cfq_pool, GFP_NOWAIT | __GFP_ZERO,
				     cfqd->queue->node);
	if (!cfqq) {
		cfqq = &cfqd->oom_cfqq;
		goto out;
	}

	cfq_init_cfqq(cfqd, cfqq, current->pid, is_sync);
	cfq_init_prio_data(cfqq, cic);
	cfq_link_cfqq_cfqg(cfqq, cfqg);
	cfq_log_cfqq(cfqd, cfqq, "alloced");

	if (async_cfqq) {
		/* a new async queue is created, pin and remember */
		cfqq->ref++;
		*async_cfqq = cfqq;
	}
out:
	cfqq->ref++;
	rcu_read_unlock();
	return cfqq;
}

static void
__cfq_update_io_thinktime(struct cfq_ttime *ttime, unsigned long slice_idle)
{
	unsigned long elapsed = jiffies - ttime->last_end_request;
	elapsed = min(elapsed, 2UL * slice_idle);

	ttime->ttime_samples = (7*ttime->ttime_samples + 256) / 8;
	ttime->ttime_total = (7*ttime->ttime_total + 256*elapsed) / 8;
	ttime->ttime_mean = (ttime->ttime_total + 128) / ttime->ttime_samples;
}

static void
cfq_update_io_thinktime(struct cfq_data *cfqd, struct cfq_queue *cfqq,
			struct cfq_io_cq *cic)
{
	if (cfq_cfqq_sync(cfqq)) {
		__cfq_update_io_thinktime(&cic->ttime, cfqd->cfq_slice_idle);
		__cfq_update_io_thinktime(&cfqq->service_tree->ttime,
			cfqd->cfq_slice_idle);
	}
#ifdef CONFIG_CFQ_GROUP_IOSCHED
	__cfq_update_io_thinktime(&cfqq->cfqg->ttime, cfqd->cfq_group_idle);
#endif
}

static void
cfq_update_io_seektime(struct cfq_data *cfqd, struct cfq_queue *cfqq,
		       struct request *rq)
{
	sector_t sdist = 0;
	sector_t n_sec = blk_rq_sectors(rq);
	if (cfqq->last_request_pos) {
		if (cfqq->last_request_pos < blk_rq_pos(rq))
			sdist = blk_rq_pos(rq) - cfqq->last_request_pos;
		else
			sdist = cfqq->last_request_pos - blk_rq_pos(rq);
	}

	cfqq->seek_history <<= 1;
	if (blk_queue_nonrot(cfqd->queue))
		cfqq->seek_history |= (n_sec < CFQQ_SECT_THR_NONROT);
	else
		cfqq->seek_history |= (sdist > CFQQ_SEEK_THR);
}

/*
 * Disable idle window if the process thinks too long or seeks so much that
 * it doesn't matter
 */
static void
cfq_update_idle_window(struct cfq_data *cfqd, struct cfq_queue *cfqq,
		       struct cfq_io_cq *cic)
{
	int old_idle, enable_idle;

	/*
	 * Don't idle for async or idle io prio class
	 */
	if (!cfq_cfqq_sync(cfqq) || cfq_class_idle(cfqq))
		return;

	enable_idle = old_idle = cfq_cfqq_idle_window(cfqq);

	if (cfqq->queued[0] + cfqq->queued[1] >= 4)
		cfq_mark_cfqq_deep(cfqq);

	if (cfqq->next_rq && (cfqq->next_rq->cmd_flags & REQ_NOIDLE))
		enable_idle = 0;
	else if (!atomic_read(&cic->icq.ioc->active_ref) ||
		 !cfqd->cfq_slice_idle ||
		 (!cfq_cfqq_deep(cfqq) && CFQQ_SEEKY(cfqq)))
		enable_idle = 0;
	else if (sample_valid(cic->ttime.ttime_samples)) {
		if (cic->ttime.ttime_mean > cfqd->cfq_slice_idle)
			enable_idle = 0;
		else
			enable_idle = 1;
	}

	if (old_idle != enable_idle) {
		cfq_log_cfqq(cfqd, cfqq, "idle=%d", enable_idle);
		if (enable_idle)
			cfq_mark_cfqq_idle_window(cfqq);
		else
			cfq_clear_cfqq_idle_window(cfqq);
	}
}

/*
 * Check if new_cfqq should preempt the currently active queue. Return 0 for
 * no or if we aren't sure, a 1 will cause a preempt.
 */
static bool
cfq_should_preempt(struct cfq_data *cfqd, struct cfq_queue *new_cfqq,
		   struct request *rq)
{
	struct cfq_queue *cfqq;

	cfqq = cfqd->active_queue;
	if (!cfqq)
		return false;

	if (cfq_class_idle(new_cfqq))
		return false;

	if (cfq_class_idle(cfqq))
		return true;

	/*
	 * Don't allow a non-RT request to preempt an ongoing RT cfqq timeslice.
	 */
	if (cfq_class_rt(cfqq) && !cfq_class_rt(new_cfqq))
		return false;

	/*
	 * if the new request is sync, but the currently running queue is
	 * not, let the sync request have priority.
	 */
	if (rq_is_sync(rq) && !cfq_cfqq_sync(cfqq) && !cfq_cfqq_must_dispatch(cfqq))
		return true;

	if (new_cfqq->cfqg != cfqq->cfqg)
		return false;

	if (cfq_slice_used(cfqq))
		return true;

	/* Allow preemption only if we are idling on sync-noidle tree */
	if (cfqd->serving_wl_type == SYNC_NOIDLE_WORKLOAD &&
	    cfqq_type(new_cfqq) == SYNC_NOIDLE_WORKLOAD &&
	    new_cfqq->service_tree->count == 2 &&
	    RB_EMPTY_ROOT(&cfqq->sort_list))
		return true;

	/*
	 * So both queues are sync. Let the new request get disk time if
	 * it's a metadata request and the current queue is doing regular IO.
	 */
	if ((rq->cmd_flags & REQ_PRIO) && !cfqq->prio_pending)
		return true;

	/*
	 * Allow an RT request to pre-empt an ongoing non-RT cfqq timeslice.
	 */
	if (cfq_class_rt(new_cfqq) && !cfq_class_rt(cfqq))
		return true;

	/* An idle queue should not be idle now for some reason */
	if (RB_EMPTY_ROOT(&cfqq->sort_list) && !cfq_should_idle(cfqd, cfqq))
		return true;

	if (!cfqd->active_cic || !cfq_cfqq_wait_request(cfqq))
		return false;

	/*
	 * if this request is as-good as one we would expect from the
	 * current cfqq, let it preempt
	 */
	if (cfq_rq_close(cfqd, cfqq, rq))
		return true;

	return false;
}

/*
 * cfqq preempts the active queue. if we allowed preempt with no slice left,
 * let it have half of its nominal slice.
 */
static void cfq_preempt_queue(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	enum wl_type_t old_type = cfqq_type(cfqd->active_queue);

	cfq_log_cfqq(cfqd, cfqq, "preempt");
	cfq_slice_expired(cfqd, 1);

	/*
	 * workload type is changed, don't save slice, otherwise preempt
	 * doesn't happen
	 */
	if (old_type != cfqq_type(cfqq))
		cfqq->cfqg->saved_wl_slice = 0;

	/*
	 * Put the new queue at the front of the of the current list,
	 * so we know that it will be selected next.
	 */
	BUG_ON(!cfq_cfqq_on_rr(cfqq));

	cfq_service_tree_add(cfqd, cfqq, 1);

	cfqq->slice_end = 0;
	cfq_mark_cfqq_slice_new(cfqq);
}

/*
 * Called when a new fs request (rq) is added (to cfqq). Check if there's
 * something we should do about it
 */
static void
cfq_rq_enqueued(struct cfq_data *cfqd, struct cfq_queue *cfqq,
		struct request *rq)
{
	struct cfq_io_cq *cic = RQ_CIC(rq);

	cfqd->rq_queued++;
	if (rq->cmd_flags & REQ_PRIO)
		cfqq->prio_pending++;

	cfq_update_io_thinktime(cfqd, cfqq, cic);
	cfq_update_io_seektime(cfqd, cfqq, rq);
	cfq_update_idle_window(cfqd, cfqq, cic);

	cfqq->last_request_pos = blk_rq_pos(rq) + blk_rq_sectors(rq);

	if (cfqq == cfqd->active_queue) {
		/*
		 * Remember that we saw a request from this process, but
		 * don't start queuing just yet. Otherwise we risk seeing lots
		 * of tiny requests, because we disrupt the normal plugging
		 * and merging. If the request is already larger than a single
		 * page, let it rip immediately. For that case we assume that
		 * merging is already done. Ditto for a busy system that
		 * has other work pending, don't risk delaying until the
		 * idle timer unplug to continue working.
		 */
		if (cfq_cfqq_wait_request(cfqq)) {
			if (blk_rq_bytes(rq) > PAGE_CACHE_SIZE ||
			    cfqd->busy_queues > 1) {
				cfq_del_timer(cfqd, cfqq);
				cfq_clear_cfqq_wait_request(cfqq);
				__blk_run_queue(cfqd->queue);
			} else {
				cfqg_stats_update_idle_time(cfqq->cfqg);
				cfq_mark_cfqq_must_dispatch(cfqq);
			}
		}
	} else if (cfq_should_preempt(cfqd, cfqq, rq)) {
		/*
		 * not the active queue - expire current slice if it is
		 * idle and has expired it's mean thinktime or this new queue
		 * has some old slice time left and is of higher priority or
		 * this new queue is RT and the current one is BE
		 */
		cfq_preempt_queue(cfqd, cfqq);
		__blk_run_queue(cfqd->queue);
	}
}

static void cfq_insert_request(struct request_queue *q, struct request *rq)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_queue *cfqq = RQ_CFQQ(rq);

	cfq_log_cfqq(cfqd, cfqq, "insert_request");
	cfq_init_prio_data(cfqq, RQ_CIC(rq));

	rq->fifo_time = jiffies + cfqd->cfq_fifo_expire[rq_is_sync(rq)];
	list_add_tail(&rq->queuelist, &cfqq->fifo);
	cfq_add_rq_rb(rq);
	cfqg_stats_update_io_add(RQ_CFQG(rq), cfqd->serving_group,
				 rq->cmd_flags);
	cfq_rq_enqueued(cfqd, cfqq, rq);
}

/*
 * Update hw_tag based on peak queue depth over 50 samples under
 * sufficient load.
 */
static void cfq_update_hw_tag(struct cfq_data *cfqd)
{
	struct cfq_queue *cfqq = cfqd->active_queue;

	if (cfqd->rq_in_driver > cfqd->hw_tag_est_depth)
		cfqd->hw_tag_est_depth = cfqd->rq_in_driver;

	if (cfqd->hw_tag == 1)
		return;

	if (cfqd->rq_queued <= CFQ_HW_QUEUE_MIN &&
	    cfqd->rq_in_driver <= CFQ_HW_QUEUE_MIN)
		return;

	/*
	 * If active queue hasn't enough requests and can idle, cfq might not
	 * dispatch sufficient requests to hardware. Don't zero hw_tag in this
	 * case
	 */
	if (cfqq && cfq_cfqq_idle_window(cfqq) &&
	    cfqq->dispatched + cfqq->queued[0] + cfqq->queued[1] <
	    CFQ_HW_QUEUE_MIN && cfqd->rq_in_driver < CFQ_HW_QUEUE_MIN)
		return;

	if (cfqd->hw_tag_samples++ < 50)
		return;

	if (cfqd->hw_tag_est_depth >= CFQ_HW_QUEUE_MIN)
		cfqd->hw_tag = 1;
	else
		cfqd->hw_tag = 0;
}

static bool cfq_should_wait_busy(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	struct cfq_io_cq *cic = cfqd->active_cic;

	/* If the queue already has requests, don't wait */
	if (!RB_EMPTY_ROOT(&cfqq->sort_list))
		return false;

	/* If there are other queues in the group, don't wait */
	if (cfqq->cfqg->nr_cfqq > 1)
		return false;

	/* the only queue in the group, but think time is big */
	if (cfq_io_thinktime_big(cfqd, &cfqq->cfqg->ttime, true))
		return false;

	if (cfq_slice_used(cfqq))
		return true;

	/* if slice left is less than think time, wait busy */
	if (cic && sample_valid(cic->ttime.ttime_samples)
	    && (cfqq->slice_end - jiffies < cic->ttime.ttime_mean))
		return true;

	/*
	 * If think times is less than a jiffy than ttime_mean=0 and above
	 * will not be true. It might happen that slice has not expired yet
	 * but will expire soon (4-5 ns) during select_queue(). To cover the
	 * case where think time is less than a jiffy, mark the queue wait
	 * busy if only 1 jiffy is left in the slice.
	 */
	if (cfqq->slice_end - jiffies == 1)
		return true;

	return false;
}

static void cfq_completed_request(struct request_queue *q, struct request *rq)
{
	struct cfq_queue *cfqq = RQ_CFQQ(rq);
	struct cfq_data *cfqd = cfqq->cfqd;
	const int sync = rq_is_sync(rq);
	unsigned long now;

	now = jiffies;
	cfq_log_cfqq(cfqd, cfqq, "complete rqnoidle %d",
		     !!(rq->cmd_flags & REQ_NOIDLE));

	cfq_update_hw_tag(cfqd);

	WARN_ON(!cfqd->rq_in_driver);
	WARN_ON(!cfqq->dispatched);
	cfqd->rq_in_driver--;
	cfqq->dispatched--;
	(RQ_CFQG(rq))->dispatched--;
	cfqg_stats_update_completion(cfqq->cfqg, rq_start_time_ns(rq),
				     rq_io_start_time_ns(rq), rq->cmd_flags);

	cfqd->rq_in_flight[cfq_cfqq_sync(cfqq)]--;

	if (sync) {
		struct cfq_rb_root *st;

		RQ_CIC(rq)->ttime.last_end_request = now;

		if (cfq_cfqq_on_rr(cfqq))
			st = cfqq->service_tree;
		else
			st = st_for(cfqq->cfqg, cfqq_class(cfqq),
					cfqq_type(cfqq));

		st->ttime.last_end_request = now;
		if (!time_after(rq->start_time + cfqd->cfq_fifo_expire[1], now))
			cfqd->last_delayed_sync = now;
	}

#ifdef CONFIG_CFQ_GROUP_IOSCHED
	cfqq->cfqg->ttime.last_end_request = now;
#endif

	/*
	 * If this is the active queue, check if it needs to be expired,
	 * or if we want to idle in case it has no pending requests.
	 */
	if (cfqd->active_queue == cfqq) {
		const bool cfqq_empty = RB_EMPTY_ROOT(&cfqq->sort_list);

		if (cfq_cfqq_slice_new(cfqq)) {
			cfq_set_prio_slice(cfqd, cfqq);
			cfq_clear_cfqq_slice_new(cfqq);
		}

		/*
		 * Should we wait for next request to come in before we expire
		 * the queue.
		 */
		if (cfq_should_wait_busy(cfqd, cfqq)) {
			unsigned long extend_sl = cfqd->cfq_slice_idle;
			if (!cfqd->cfq_slice_idle)
				extend_sl = cfqd->cfq_group_idle;
			cfqq->slice_end = jiffies + extend_sl;
			cfq_mark_cfqq_wait_busy(cfqq);
			cfq_log_cfqq(cfqd, cfqq, "will busy wait");
		}

		/*
		 * Idling is not enabled on:
		 * - expired queues
		 * - idle-priority queues
		 * - async queues
		 * - queues with still some requests queued
		 * - when there is a close cooperator
		 */
		if (cfq_slice_used(cfqq) || cfq_class_idle(cfqq))
			cfq_slice_expired(cfqd, 1);
		else if (sync && cfqq_empty &&
			 !cfq_close_cooperator(cfqd, cfqq)) {
			cfq_arm_slice_timer(cfqd);
		}
	}

	if (!cfqd->rq_in_driver)
		cfq_schedule_dispatch(cfqd);
}

static inline int __cfq_may_queue(struct cfq_queue *cfqq)
{
	if (cfq_cfqq_wait_request(cfqq) && !cfq_cfqq_must_alloc_slice(cfqq)) {
		cfq_mark_cfqq_must_alloc_slice(cfqq);
		return ELV_MQUEUE_MUST;
	}

	return ELV_MQUEUE_MAY;
}

static int cfq_may_queue(struct request_queue *q, int rw)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct task_struct *tsk = current;
	struct cfq_io_cq *cic;
	struct cfq_queue *cfqq;

	/*
	 * don't force setup of a queue from here, as a call to may_queue
	 * does not necessarily imply that a request actually will be queued.
	 * so just lookup a possibly existing queue, or return 'may queue'
	 * if that fails
	 */
	cic = cfq_cic_lookup(cfqd, tsk->io_context);
	if (!cic)
		return ELV_MQUEUE_MAY;

	cfqq = cic_to_cfqq(cic, rw_is_sync(rw));
	if (cfqq) {
		cfq_init_prio_data(cfqq, cic);

		return __cfq_may_queue(cfqq);
	}

	return ELV_MQUEUE_MAY;
}

/*
 * queue lock held here
 */
static void cfq_put_request(struct request *rq)
{
	struct cfq_queue *cfqq = RQ_CFQQ(rq);

	if (cfqq) {
		const int rw = rq_data_dir(rq);

		BUG_ON(!cfqq->allocated[rw]);
		cfqq->allocated[rw]--;

		/* Put down rq reference on cfqg */
		cfqg_put(RQ_CFQG(rq));
		rq->elv.priv[0] = NULL;
		rq->elv.priv[1] = NULL;

		cfq_put_queue(cfqq);
	}
}

static struct cfq_queue *
cfq_merge_cfqqs(struct cfq_data *cfqd, struct cfq_io_cq *cic,
		struct cfq_queue *cfqq)
{
	cfq_log_cfqq(cfqd, cfqq, "merging with queue %p", cfqq->new_cfqq);
	cic_set_cfqq(cic, cfqq->new_cfqq, 1);
	cfq_mark_cfqq_coop(cfqq->new_cfqq);
	cfq_put_queue(cfqq);
	return cic_to_cfqq(cic, 1);
}

/*
 * Returns NULL if a new cfqq should be allocated, or the old cfqq if this
 * was the last process referring to said cfqq.
 */
static struct cfq_queue *
split_cfqq(struct cfq_io_cq *cic, struct cfq_queue *cfqq)
{
	if (cfqq_process_refs(cfqq) == 1) {
		cfqq->pid = current->pid;
		cfq_clear_cfqq_coop(cfqq);
		cfq_clear_cfqq_split_coop(cfqq);
		return cfqq;
	}

	cic_set_cfqq(cic, NULL, 1);

	cfq_put_cooperator(cfqq);

	cfq_put_queue(cfqq);
	return NULL;
}
/*
 * Allocate cfq data structures associated with this request.
 */
static int
cfq_set_request(struct request_queue *q, struct request *rq, struct bio *bio,
		gfp_t gfp_mask)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_io_cq *cic = icq_to_cic(rq->elv.icq);
	const int rw = rq_data_dir(rq);
	const bool is_sync = rq_is_sync(rq);
	struct cfq_queue *cfqq;

	spin_lock_irq(q->queue_lock);

	check_ioprio_changed(cic, bio);
	check_blkcg_changed(cic, bio);
new_queue:
	cfqq = cic_to_cfqq(cic, is_sync);
	if (!cfqq || cfqq == &cfqd->oom_cfqq) {
		if (cfqq)
			cfq_put_queue(cfqq);
		cfqq = cfq_get_queue(cfqd, is_sync, cic, bio);
		cic_set_cfqq(cic, cfqq, is_sync);
	} else {
		/*
		 * If the queue was seeky for too long, break it apart.
		 */
		if (cfq_cfqq_coop(cfqq) && cfq_cfqq_split_coop(cfqq)) {
			cfq_log_cfqq(cfqd, cfqq, "breaking apart cfqq");
			cfqq = split_cfqq(cic, cfqq);
			if (!cfqq)
				goto new_queue;
		}

		/*
		 * Check to see if this queue is scheduled to merge with
		 * another, closely cooperating queue.  The merging of
		 * queues happens here as it must be done in process context.
		 * The reference on new_cfqq was taken in merge_cfqqs.
		 */
		if (cfqq->new_cfqq)
			cfqq = cfq_merge_cfqqs(cfqd, cic, cfqq);
	}

	cfqq->allocated[rw]++;

	cfqq->ref++;
	cfqg_get(cfqq->cfqg);
	rq->elv.priv[0] = cfqq;
	rq->elv.priv[1] = cfqq->cfqg;
	spin_unlock_irq(q->queue_lock);
	return 0;
}

static void cfq_kick_queue(struct work_struct *work)
{
	struct cfq_data *cfqd =
		container_of(work, struct cfq_data, unplug_work);
	struct request_queue *q = cfqd->queue;

	spin_lock_irq(q->queue_lock);
	__blk_run_queue(cfqd->queue);
	spin_unlock_irq(q->queue_lock);
}

/*
 * Timer running if the active_queue is currently idling inside its time slice
 */
static void cfq_idle_slice_timer(unsigned long data)
{
	struct cfq_data *cfqd = (struct cfq_data *) data;
	struct cfq_queue *cfqq;
	unsigned long flags;
	int timed_out = 1;

	cfq_log(cfqd, "idle timer fired");

	spin_lock_irqsave(cfqd->queue->queue_lock, flags);

	cfqq = cfqd->active_queue;
	if (cfqq) {
		timed_out = 0;

		/*
		 * We saw a request before the queue expired, let it through
		 */
		if (cfq_cfqq_must_dispatch(cfqq))
			goto out_kick;

		/*
		 * expired
		 */
		if (cfq_slice_used(cfqq))
			goto expire;

		/*
		 * only expire and reinvoke request handler, if there are
		 * other queues with pending requests
		 */
		if (!cfqd->busy_queues)
			goto out_cont;

		/*
		 * not expired and it has a request pending, let it dispatch
		 */
		if (!RB_EMPTY_ROOT(&cfqq->sort_list))
			goto out_kick;

		/*
		 * Queue depth flag is reset only when the idle didn't succeed
		 */
		cfq_clear_cfqq_deep(cfqq);
	}
expire:
	cfq_slice_expired(cfqd, timed_out);
out_kick:
	cfq_schedule_dispatch(cfqd);
out_cont:
	spin_unlock_irqrestore(cfqd->queue->queue_lock, flags);
}

static void cfq_shutdown_timer_wq(struct cfq_data *cfqd)
{
	del_timer_sync(&cfqd->idle_slice_timer);
	cancel_work_sync(&cfqd->unplug_work);
}

static void cfq_exit_queue(struct elevator_queue *e)
{
	struct cfq_data *cfqd = e->elevator_data;
	struct request_queue *q = cfqd->queue;

	cfq_shutdown_timer_wq(cfqd);

	spin_lock_irq(q->queue_lock);

	if (cfqd->active_queue)
		__cfq_slice_expired(cfqd, cfqd->active_queue, 0);

	spin_unlock_irq(q->queue_lock);

	cfq_shutdown_timer_wq(cfqd);

#ifdef CONFIG_CFQ_GROUP_IOSCHED
	blkcg_deactivate_policy(q, &blkcg_policy_cfq);
#else
	kfree(cfqd->root_group);
#endif
	kfree(cfqd);
}

static int cfq_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct cfq_data *cfqd;
	struct blkcg_gq *blkg __maybe_unused;
	int i, ret;
	struct elevator_queue *eq;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	cfqd = kzalloc_node(sizeof(*cfqd), GFP_KERNEL, q->node);
	if (!cfqd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	eq->elevator_data = cfqd;

	cfqd->queue = q;
	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);

	/* Init root service tree */
	cfqd->grp_service_tree = CFQ_RB_ROOT;

	/* Init root group and prefer root group over other groups by default */
#ifdef CONFIG_CFQ_GROUP_IOSCHED
	ret = blkcg_activate_policy(q, &blkcg_policy_cfq);
	if (ret)
		goto out_free;

	cfqd->root_group = blkg_to_cfqg(q->root_blkg);
#else
	ret = -ENOMEM;
	cfqd->root_group = kzalloc_node(sizeof(*cfqd->root_group),
					GFP_KERNEL, cfqd->queue->node);
	if (!cfqd->root_group)
		goto out_free;

	cfq_init_cfqg_base(cfqd->root_group);
	cfqd->root_group->weight = 2 * CFQ_WEIGHT_LEGACY_DFL;
	cfqd->root_group->leaf_weight = 2 * CFQ_WEIGHT_LEGACY_DFL;
#endif

	/*
	 * Not strictly needed (since RB_ROOT just clears the node and we
	 * zeroed cfqd on alloc), but better be safe in case someone decides
	 * to add magic to the rb code
	 */
	for (i = 0; i < CFQ_PRIO_LISTS; i++)
		cfqd->prio_trees[i] = RB_ROOT;

	/*
	 * Our fallback cfqq if cfq_get_queue() runs into OOM issues.
	 * Grab a permanent reference to it, so that the normal code flow
	 * will not attempt to free it.  oom_cfqq is linked to root_group
	 * but shouldn't hold a reference as it'll never be unlinked.  Lose
	 * the reference from linking right away.
	 */
	cfq_init_cfqq(cfqd, &cfqd->oom_cfqq, 1, 0);
	cfqd->oom_cfqq.ref++;

	spin_lock_irq(q->queue_lock);
	cfq_link_cfqq_cfqg(&cfqd->oom_cfqq, cfqd->root_group);
	cfqg_put(cfqd->root_group);
	spin_unlock_irq(q->queue_lock);

	init_timer(&cfqd->idle_slice_timer);
	cfqd->idle_slice_timer.function = cfq_idle_slice_timer;
	cfqd->idle_slice_timer.data = (unsigned long) cfqd;

	INIT_WORK(&cfqd->unplug_work, cfq_kick_queue);

	cfqd->cfq_quantum = cfq_quantum;
	cfqd->cfq_fifo_expire[0] = cfq_fifo_expire[0];
	cfqd->cfq_fifo_expire[1] = cfq_fifo_expire[1];
	cfqd->cfq_back_max = cfq_back_max;
	cfqd->cfq_back_penalty = cfq_back_penalty;
	cfqd->cfq_slice[0] = cfq_slice_async;
	cfqd->cfq_slice[1] = cfq_slice_sync;
	cfqd->cfq_target_latency = cfq_target_latency;
	cfqd->cfq_slice_async_rq = cfq_slice_async_rq;
	cfqd->cfq_slice_idle = cfq_slice_idle;
	cfqd->cfq_group_idle = cfq_group_idle;
	cfqd->cfq_latency = 1;
	cfqd->hw_tag = -1;
	/*
	 * we optimistically start assuming sync ops weren't delayed in last
	 * second, in order to have larger depth for async operations.
	 */
	cfqd->last_delayed_sync = jiffies - HZ;
	return 0;

out_free:
	kfree(cfqd);
	kobject_put(&eq->kobj);
	return ret;
}

static void cfq_registered_queue(struct request_queue *q)
{
	struct elevator_queue *e = q->elevator;
	struct cfq_data *cfqd = e->elevator_data;

	/*
	 * Default to IOPS mode with no idling for SSDs
	 */
	if (blk_queue_nonrot(q))
		cfqd->cfq_slice_idle = 0;
}

/*
 * sysfs parts below -->
 */
static ssize_t
cfq_var_show(unsigned int var, char *page)
{
	return sprintf(page, "%u\n", var);
}

static ssize_t
cfq_var_store(unsigned int *var, const char *page, size_t count)
{
	char *p = (char *) page;

	*var = simple_strtoul(p, &p, 10);
	return count;
}

#define SHOW_FUNCTION(__FUNC, __VAR, __CONV)				\
static ssize_t __FUNC(struct elevator_queue *e, char *page)		\
{									\
	struct cfq_data *cfqd = e->elevator_data;			\
	unsigned int __data = __VAR;					\
	if (__CONV)							\
		__data = jiffies_to_msecs(__data);			\
	return cfq_var_show(__data, (page));				\
}
SHOW_FUNCTION(cfq_quantum_show, cfqd->cfq_quantum, 0);
SHOW_FUNCTION(cfq_fifo_expire_sync_show, cfqd->cfq_fifo_expire[1], 1);
SHOW_FUNCTION(cfq_fifo_expire_async_show, cfqd->cfq_fifo_expire[0], 1);
SHOW_FUNCTION(cfq_back_seek_max_show, cfqd->cfq_back_max, 0);
SHOW_FUNCTION(cfq_back_seek_penalty_show, cfqd->cfq_back_penalty, 0);
SHOW_FUNCTION(cfq_slice_idle_show, cfqd->cfq_slice_idle, 1);
SHOW_FUNCTION(cfq_group_idle_show, cfqd->cfq_group_idle, 1);
SHOW_FUNCTION(cfq_slice_sync_show, cfqd->cfq_slice[1], 1);
SHOW_FUNCTION(cfq_slice_async_show, cfqd->cfq_slice[0], 1);
SHOW_FUNCTION(cfq_slice_async_rq_show, cfqd->cfq_slice_async_rq, 0);
SHOW_FUNCTION(cfq_low_latency_show, cfqd->cfq_latency, 0);
SHOW_FUNCTION(cfq_target_latency_show, cfqd->cfq_target_latency, 1);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, __CONV)			\
static ssize_t __FUNC(struct elevator_queue *e, const char *page, size_t count)	\
{									\
	struct cfq_data *cfqd = e->elevator_data;			\
	unsigned int __data;						\
	int ret = cfq_var_store(&__data, (page), count);		\
	if (__data < (MIN))						\
		__data = (MIN);						\
	else if (__data > (MAX))					\
		__data = (MAX);						\
	if (__CONV)							\
		*(__PTR) = msecs_to_jiffies(__data);			\
	else								\
		*(__PTR) = __data;					\
	return ret;							\
}
STORE_FUNCTION(cfq_quantum_store, &cfqd->cfq_quantum, 1, UINT_MAX, 0);
STORE_FUNCTION(cfq_fifo_expire_sync_store, &cfqd->cfq_fifo_expire[1], 1,
		UINT_MAX, 1);
STORE_FUNCTION(cfq_fifo_expire_async_store, &cfqd->cfq_fifo_expire[0], 1,
		UINT_MAX, 1);
STORE_FUNCTION(cfq_back_seek_max_store, &cfqd->cfq_back_max, 0, UINT_MAX, 0);
STORE_FUNCTION(cfq_back_seek_penalty_store, &cfqd->cfq_back_penalty, 1,
		UINT_MAX, 0);
STORE_FUNCTION(cfq_slice_idle_store, &cfqd->cfq_slice_idle, 0, UINT_MAX, 1);
STORE_FUNCTION(cfq_group_idle_store, &cfqd->cfq_group_idle, 0, UINT_MAX, 1);
STORE_FUNCTION(cfq_slice_sync_store, &cfqd->cfq_slice[1], 1, UINT_MAX, 1);
STORE_FUNCTION(cfq_slice_async_store, &cfqd->cfq_slice[0], 1, UINT_MAX, 1);
STORE_FUNCTION(cfq_slice_async_rq_store, &cfqd->cfq_slice_async_rq, 1,
		UINT_MAX, 0);
STORE_FUNCTION(cfq_low_latency_store, &cfqd->cfq_latency, 0, 1, 0);
STORE_FUNCTION(cfq_target_latency_store, &cfqd->cfq_target_latency, 1, UINT_MAX, 1);
#undef STORE_FUNCTION

#define CFQ_ATTR(name) \
	__ATTR(name, S_IRUGO|S_IWUSR, cfq_##name##_show, cfq_##name##_store)

static struct elv_fs_entry cfq_attrs[] = {
	CFQ_ATTR(quantum),
	CFQ_ATTR(fifo_expire_sync),
	CFQ_ATTR(fifo_expire_async),
	CFQ_ATTR(back_seek_max),
	CFQ_ATTR(back_seek_penalty),
	CFQ_ATTR(slice_sync),
	CFQ_ATTR(slice_async),
	CFQ_ATTR(slice_async_rq),
	CFQ_ATTR(slice_idle),
	CFQ_ATTR(group_idle),
	CFQ_ATTR(low_latency),
	CFQ_ATTR(target_latency),
	__ATTR_NULL
};

static struct elevator_type iosched_cfq = {
	.ops = {
		.elevator_merge_fn = 		cfq_merge,
		.elevator_merged_fn =		cfq_merged_request,
		.elevator_merge_req_fn =	cfq_merged_requests,
		.elevator_allow_merge_fn =	cfq_allow_merge,
		.elevator_bio_merged_fn =	cfq_bio_merged,
		.elevator_dispatch_fn =		cfq_dispatch_requests,
		.elevator_add_req_fn =		cfq_insert_request,
		.elevator_activate_req_fn =	cfq_activate_request,
		.elevator_deactivate_req_fn =	cfq_deactivate_request,
		.elevator_completed_req_fn =	cfq_completed_request,
		.elevator_former_req_fn =	elv_rb_former_request,
		.elevator_latter_req_fn =	elv_rb_latter_request,
		.elevator_init_icq_fn =		cfq_init_icq,
		.elevator_exit_icq_fn =		cfq_exit_icq,
		.elevator_set_req_fn =		cfq_set_request,
		.elevator_put_req_fn =		cfq_put_request,
		.elevator_may_queue_fn =	cfq_may_queue,
		.elevator_init_fn =		cfq_init_queue,
		.elevator_exit_fn =		cfq_exit_queue,
		.elevator_registered_fn =	cfq_registered_queue,
	},
	.icq_size	=	sizeof(struct cfq_io_cq),
	.icq_align	=	__alignof__(struct cfq_io_cq),
	.elevator_attrs =	cfq_attrs,
	.elevator_name	=	"cfq",
	.elevator_owner =	THIS_MODULE,
};

#ifdef CONFIG_CFQ_GROUP_IOSCHED
static struct blkcg_policy blkcg_policy_cfq = {
	.dfl_cftypes		= cfq_blkcg_files,
	.legacy_cftypes		= cfq_blkcg_legacy_files,

	.cpd_alloc_fn		= cfq_cpd_alloc,
	.cpd_init_fn		= cfq_cpd_init,
	.cpd_free_fn		= cfq_cpd_free,
	.cpd_bind_fn		= cfq_cpd_bind,

	.pd_alloc_fn		= cfq_pd_alloc,
	.pd_init_fn		= cfq_pd_init,
	.pd_offline_fn		= cfq_pd_offline,
	.pd_free_fn		= cfq_pd_free,
	.pd_reset_stats_fn	= cfq_pd_reset_stats,
};
#endif

static int __init cfq_init(void)
{
	int ret;

	/*
	 * could be 0 on HZ < 1000 setups
	 */
	if (!cfq_slice_async)
		cfq_slice_async = 1;
	if (!cfq_slice_idle)
		cfq_slice_idle = 1;

#ifdef CONFIG_CFQ_GROUP_IOSCHED
	if (!cfq_group_idle)
		cfq_group_idle = 1;

	ret = blkcg_policy_register(&blkcg_policy_cfq);
	if (ret)
		return ret;
#else
	cfq_group_idle = 0;
#endif

	ret = -ENOMEM;
	cfq_pool = KMEM_CACHE(cfq_queue, 0);
	if (!cfq_pool)
		goto err_pol_unreg;

	ret = elv_register(&iosched_cfq);
	if (ret)
		goto err_free_pool;

	return 0;

err_free_pool:
	kmem_cache_destroy(cfq_pool);
err_pol_unreg:
#ifdef CONFIG_CFQ_GROUP_IOSCHED
	blkcg_policy_unregister(&blkcg_policy_cfq);
#endif
	return ret;
}

static void __exit cfq_exit(void)
{
#ifdef CONFIG_CFQ_GROUP_IOSCHED
	blkcg_policy_unregister(&blkcg_policy_cfq);
#endif
	elv_unregister(&iosched_cfq);
	kmem_cache_destroy(cfq_pool);
}

module_init(cfq_init);
module_exit(cfq_exit);

MODULE_AUTHOR("Jens Axboe");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Completely Fair Queueing IO scheduler");
