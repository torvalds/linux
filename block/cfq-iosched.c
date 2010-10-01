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
#include "cfq.h"

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

#define RQ_CIC(rq)		\
	((struct cfq_io_context *) (rq)->elevator_private)
#define RQ_CFQQ(rq)		(struct cfq_queue *) ((rq)->elevator_private2)
#define RQ_CFQG(rq)		(struct cfq_group *) ((rq)->elevator_private3)

static struct kmem_cache *cfq_pool;
static struct kmem_cache *cfq_ioc_pool;

static DEFINE_PER_CPU(unsigned long, cfq_ioc_count);
static struct completion *ioc_gone;
static DEFINE_SPINLOCK(ioc_gone_lock);

static DEFINE_SPINLOCK(cic_index_lock);
static DEFINE_IDA(cic_index_ida);

#define CFQ_PRIO_LISTS		IOPRIO_BE_NR
#define cfq_class_idle(cfqq)	((cfqq)->ioprio_class == IOPRIO_CLASS_IDLE)
#define cfq_class_rt(cfqq)	((cfqq)->ioprio_class == IOPRIO_CLASS_RT)

#define sample_valid(samples)	((samples) > 80)
#define rb_entry_cfqg(node)	rb_entry((node), struct cfq_group, rb_node)

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
	unsigned total_weight;
	u64 min_vdisktime;
	struct rb_node *active;
};
#define CFQ_RB_ROOT	(struct cfq_rb_root) { .rb = RB_ROOT, .left = NULL, \
			.count = 0, .min_vdisktime = 0, }

/*
 * Per process-grouping structure
 */
struct cfq_queue {
	/* reference count */
	atomic_t ref;
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

	/* pending metadata requests */
	int meta_pending;
	/* number of requests that are on the dispatch list or inside driver */
	int dispatched;

	/* io prio of this group */
	unsigned short ioprio, org_ioprio;
	unsigned short ioprio_class, org_ioprio_class;

	pid_t pid;

	u32 seek_history;
	sector_t last_request_pos;

	struct cfq_rb_root *service_tree;
	struct cfq_queue *new_cfqq;
	struct cfq_group *cfqg;
	struct cfq_group *orig_cfqg;
};

/*
 * First index in the service_trees.
 * IDLE is handled separately, so it has negative index
 */
enum wl_prio_t {
	BE_WORKLOAD = 0,
	RT_WORKLOAD = 1,
	IDLE_WORKLOAD = 2,
};

/*
 * Second index in the service_trees.
 */
enum wl_type_t {
	ASYNC_WORKLOAD = 0,
	SYNC_NOIDLE_WORKLOAD = 1,
	SYNC_WORKLOAD = 2
};

/* This is per cgroup per device grouping structure */
struct cfq_group {
	/* group service_tree member */
	struct rb_node rb_node;

	/* group service_tree key */
	u64 vdisktime;
	unsigned int weight;
	bool on_st;

	/* number of cfqq currently on this group */
	int nr_cfqq;

	/* Per group busy queus average. Useful for workload slice calc. */
	unsigned int busy_queues_avg[2];
	/*
	 * rr lists of queues with requests, onle rr for each priority class.
	 * Counts are embedded in the cfq_rb_root
	 */
	struct cfq_rb_root service_trees[2][3];
	struct cfq_rb_root service_tree_idle;

	unsigned long saved_workload_slice;
	enum wl_type_t saved_workload;
	enum wl_prio_t saved_serving_prio;
	struct blkio_group blkg;
#ifdef CONFIG_CFQ_GROUP_IOSCHED
	struct hlist_node cfqd_node;
	atomic_t ref;
#endif
};

/*
 * Per block device queue structure
 */
struct cfq_data {
	struct request_queue *queue;
	/* Root service tree for cfq_groups */
	struct cfq_rb_root grp_service_tree;
	struct cfq_group root_group;

	/*
	 * The priority currently being served
	 */
	enum wl_prio_t serving_prio;
	enum wl_type_t serving_type;
	unsigned long workload_expires;
	struct cfq_group *serving_group;

	/*
	 * Each priority tree is sorted by next_request position.  These
	 * trees are used when determining if two or more queues are
	 * interleaving requests (see cfq_close_cooperator).
	 */
	struct rb_root prio_trees[CFQ_PRIO_LISTS];

	unsigned int busy_queues;

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
	struct cfq_io_context *active_cic;

	/*
	 * async queue for each priority case
	 */
	struct cfq_queue *async_cfqq[2][IOPRIO_BE_NR];
	struct cfq_queue *async_idle_cfqq;

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
	unsigned int cfq_latency;
	unsigned int cfq_group_isolation;

	unsigned int cic_index;
	struct list_head cic_list;

	/*
	 * Fallback dummy cfqq for extreme OOM conditions
	 */
	struct cfq_queue oom_cfqq;

	unsigned long last_delayed_sync;

	/* List of cfq groups being managed on this device*/
	struct hlist_head cfqg_list;
	struct rcu_head rcu;
};

static struct cfq_group *cfq_get_next_cfqg(struct cfq_data *cfqd);

static struct cfq_rb_root *service_tree_for(struct cfq_group *cfqg,
					    enum wl_prio_t prio,
					    enum wl_type_t type)
{
	if (!cfqg)
		return NULL;

	if (prio == IDLE_WORKLOAD)
		return &cfqg->service_tree_idle;

	return &cfqg->service_trees[prio][type];
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

#ifdef CONFIG_CFQ_GROUP_IOSCHED
#define cfq_log_cfqq(cfqd, cfqq, fmt, args...)	\
	blk_add_trace_msg((cfqd)->queue, "cfq%d%c %s " fmt, (cfqq)->pid, \
			cfq_cfqq_sync((cfqq)) ? 'S' : 'A', \
			blkg_path(&(cfqq)->cfqg->blkg), ##args);

#define cfq_log_cfqg(cfqd, cfqg, fmt, args...)				\
	blk_add_trace_msg((cfqd)->queue, "%s " fmt,			\
				blkg_path(&(cfqg)->blkg), ##args);      \

#else
#define cfq_log_cfqq(cfqd, cfqq, fmt, args...)	\
	blk_add_trace_msg((cfqd)->queue, "cfq%d " fmt, (cfqq)->pid, ##args)
#define cfq_log_cfqg(cfqd, cfqg, fmt, args...)		do {} while (0);
#endif
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


static inline enum wl_prio_t cfqq_prio(struct cfq_queue *cfqq)
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

static inline int cfq_group_busy_queues_wl(enum wl_prio_t wl,
					struct cfq_data *cfqd,
					struct cfq_group *cfqg)
{
	if (wl == IDLE_WORKLOAD)
		return cfqg->service_tree_idle.count;

	return cfqg->service_trees[wl][ASYNC_WORKLOAD].count
		+ cfqg->service_trees[wl][SYNC_NOIDLE_WORKLOAD].count
		+ cfqg->service_trees[wl][SYNC_WORKLOAD].count;
}

static inline int cfqg_busy_async_queues(struct cfq_data *cfqd,
					struct cfq_group *cfqg)
{
	return cfqg->service_trees[RT_WORKLOAD][ASYNC_WORKLOAD].count
		+ cfqg->service_trees[BE_WORKLOAD][ASYNC_WORKLOAD].count;
}

static void cfq_dispatch_insert(struct request_queue *, struct request *);
static struct cfq_queue *cfq_get_queue(struct cfq_data *, bool,
				       struct io_context *, gfp_t);
static struct cfq_io_context *cfq_cic_lookup(struct cfq_data *,
						struct io_context *);

static inline struct cfq_queue *cic_to_cfqq(struct cfq_io_context *cic,
					    bool is_sync)
{
	return cic->cfqq[is_sync];
}

static inline void cic_set_cfqq(struct cfq_io_context *cic,
				struct cfq_queue *cfqq, bool is_sync)
{
	cic->cfqq[is_sync] = cfqq;
}

#define CIC_DEAD_KEY	1ul
#define CIC_DEAD_INDEX_SHIFT	1

static inline void *cfqd_dead_key(struct cfq_data *cfqd)
{
	return (void *)(cfqd->cic_index << CIC_DEAD_INDEX_SHIFT | CIC_DEAD_KEY);
}

static inline struct cfq_data *cic_to_cfqd(struct cfq_io_context *cic)
{
	struct cfq_data *cfqd = cic->key;

	if (unlikely((unsigned long) cfqd & CIC_DEAD_KEY))
		return NULL;

	return cfqd;
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
		kblockd_schedule_work(cfqd->queue, &cfqd->unplug_work);
	}
}

static int cfq_queue_empty(struct request_queue *q)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;

	return !cfqd->rq_queued;
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

static inline u64 cfq_scale_slice(unsigned long delta, struct cfq_group *cfqg)
{
	u64 d = delta << CFQ_SERVICE_SHIFT;

	d = d * BLKIO_WEIGHT_DEFAULT;
	do_div(d, cfqg->weight);
	return d;
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
	u64 vdisktime = st->min_vdisktime;
	struct cfq_group *cfqg;

	if (st->active) {
		cfqg = rb_entry_cfqg(st->active);
		vdisktime = cfqg->vdisktime;
	}

	if (st->left) {
		cfqg = rb_entry_cfqg(st->left);
		vdisktime = min_vdisktime(vdisktime, cfqg->vdisktime);
	}

	st->min_vdisktime = max_vdisktime(st->min_vdisktime, vdisktime);
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
	struct cfq_rb_root *st = &cfqd->grp_service_tree;

	return cfq_target_latency * cfqg->weight / st->total_weight;
}

static inline void
cfq_set_prio_slice(struct cfq_data *cfqd, struct cfq_queue *cfqq)
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
		return 0;
	if (time_before(jiffies, cfqq->slice_end))
		return 0;

	return 1;
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

	if (rq_is_sync(rq1) && !rq_is_sync(rq2))
		return rq1;
	else if (rq_is_sync(rq2) && !rq_is_sync(rq1))
		return rq2;
	if ((rq1->cmd_flags & REQ_META) && !(rq2->cmd_flags & REQ_META))
		return rq1;
	else if ((rq2->cmd_flags & REQ_META) &&
		 !(rq1->cmd_flags & REQ_META))
		return rq2;

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

static void
cfq_group_service_tree_add(struct cfq_data *cfqd, struct cfq_group *cfqg)
{
	struct cfq_rb_root *st = &cfqd->grp_service_tree;
	struct cfq_group *__cfqg;
	struct rb_node *n;

	cfqg->nr_cfqq++;
	if (cfqg->on_st)
		return;

	/*
	 * Currently put the group at the end. Later implement something
	 * so that groups get lesser vtime based on their weights, so that
	 * if group does not loose all if it was not continously backlogged.
	 */
	n = rb_last(&st->rb);
	if (n) {
		__cfqg = rb_entry_cfqg(n);
		cfqg->vdisktime = __cfqg->vdisktime + CFQ_IDLE_DELAY;
	} else
		cfqg->vdisktime = st->min_vdisktime;

	__cfq_group_service_tree_add(st, cfqg);
	cfqg->on_st = true;
	st->total_weight += cfqg->weight;
}

static void
cfq_group_service_tree_del(struct cfq_data *cfqd, struct cfq_group *cfqg)
{
	struct cfq_rb_root *st = &cfqd->grp_service_tree;

	if (st->active == &cfqg->rb_node)
		st->active = NULL;

	BUG_ON(cfqg->nr_cfqq < 1);
	cfqg->nr_cfqq--;

	/* If there are other cfq queues under this group, don't delete it */
	if (cfqg->nr_cfqq)
		return;

	cfq_log_cfqg(cfqd, cfqg, "del_from_rr group");
	cfqg->on_st = false;
	st->total_weight -= cfqg->weight;
	if (!RB_EMPTY_NODE(&cfqg->rb_node))
		cfq_rb_erase(&cfqg->rb_node, st);
	cfqg->saved_workload_slice = 0;
	cfq_blkiocg_update_dequeue_stats(&cfqg->blkg, 1);
}

static inline unsigned int cfq_cfqq_slice_usage(struct cfq_queue *cfqq)
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
		if (slice_used > cfqq->allocated_slice)
			slice_used = cfqq->allocated_slice;
	}

	cfq_log_cfqq(cfqq->cfqd, cfqq, "sl_used=%u", slice_used);
	return slice_used;
}

static void cfq_group_served(struct cfq_data *cfqd, struct cfq_group *cfqg,
				struct cfq_queue *cfqq)
{
	struct cfq_rb_root *st = &cfqd->grp_service_tree;
	unsigned int used_sl, charge_sl;
	int nr_sync = cfqg->nr_cfqq - cfqg_busy_async_queues(cfqd, cfqg)
			- cfqg->service_tree_idle.count;

	BUG_ON(nr_sync < 0);
	used_sl = charge_sl = cfq_cfqq_slice_usage(cfqq);

	if (!cfq_cfqq_sync(cfqq) && !nr_sync)
		charge_sl = cfqq->allocated_slice;

	/* Can't update vdisktime while group is on service tree */
	cfq_rb_erase(&cfqg->rb_node, st);
	cfqg->vdisktime += cfq_scale_slice(charge_sl, cfqg);
	__cfq_group_service_tree_add(st, cfqg);

	/* This group is being expired. Save the context */
	if (time_after(cfqd->workload_expires, jiffies)) {
		cfqg->saved_workload_slice = cfqd->workload_expires
						- jiffies;
		cfqg->saved_workload = cfqd->serving_type;
		cfqg->saved_serving_prio = cfqd->serving_prio;
	} else
		cfqg->saved_workload_slice = 0;

	cfq_log_cfqg(cfqd, cfqg, "served: vt=%llu min_vt=%llu", cfqg->vdisktime,
					st->min_vdisktime);
	cfq_blkiocg_update_timeslice_used(&cfqg->blkg, used_sl);
	cfq_blkiocg_set_start_empty_time(&cfqg->blkg);
}

#ifdef CONFIG_CFQ_GROUP_IOSCHED
static inline struct cfq_group *cfqg_of_blkg(struct blkio_group *blkg)
{
	if (blkg)
		return container_of(blkg, struct cfq_group, blkg);
	return NULL;
}

void cfq_update_blkio_group_weight(void *key, struct blkio_group *blkg,
					unsigned int weight)
{
	cfqg_of_blkg(blkg)->weight = weight;
}

static struct cfq_group *
cfq_find_alloc_cfqg(struct cfq_data *cfqd, struct cgroup *cgroup, int create)
{
	struct blkio_cgroup *blkcg = cgroup_to_blkio_cgroup(cgroup);
	struct cfq_group *cfqg = NULL;
	void *key = cfqd;
	int i, j;
	struct cfq_rb_root *st;
	struct backing_dev_info *bdi = &cfqd->queue->backing_dev_info;
	unsigned int major, minor;

	cfqg = cfqg_of_blkg(blkiocg_lookup_group(blkcg, key));
	if (cfqg && !cfqg->blkg.dev && bdi->dev && dev_name(bdi->dev)) {
		sscanf(dev_name(bdi->dev), "%u:%u", &major, &minor);
		cfqg->blkg.dev = MKDEV(major, minor);
		goto done;
	}
	if (cfqg || !create)
		goto done;

	cfqg = kzalloc_node(sizeof(*cfqg), GFP_ATOMIC, cfqd->queue->node);
	if (!cfqg)
		goto done;

	for_each_cfqg_st(cfqg, i, j, st)
		*st = CFQ_RB_ROOT;
	RB_CLEAR_NODE(&cfqg->rb_node);

	/*
	 * Take the initial reference that will be released on destroy
	 * This can be thought of a joint reference by cgroup and
	 * elevator which will be dropped by either elevator exit
	 * or cgroup deletion path depending on who is exiting first.
	 */
	atomic_set(&cfqg->ref, 1);

	/* Add group onto cgroup list */
	sscanf(dev_name(bdi->dev), "%u:%u", &major, &minor);
	cfq_blkiocg_add_blkio_group(blkcg, &cfqg->blkg, (void *)cfqd,
					MKDEV(major, minor));
	cfqg->weight = blkcg_get_weight(blkcg, cfqg->blkg.dev);

	/* Add group on cfqd list */
	hlist_add_head(&cfqg->cfqd_node, &cfqd->cfqg_list);

done:
	return cfqg;
}

/*
 * Search for the cfq group current task belongs to. If create = 1, then also
 * create the cfq group if it does not exist. request_queue lock must be held.
 */
static struct cfq_group *cfq_get_cfqg(struct cfq_data *cfqd, int create)
{
	struct cgroup *cgroup;
	struct cfq_group *cfqg = NULL;

	rcu_read_lock();
	cgroup = task_cgroup(current, blkio_subsys_id);
	cfqg = cfq_find_alloc_cfqg(cfqd, cgroup, create);
	if (!cfqg && create)
		cfqg = &cfqd->root_group;
	rcu_read_unlock();
	return cfqg;
}

static inline struct cfq_group *cfq_ref_get_cfqg(struct cfq_group *cfqg)
{
	atomic_inc(&cfqg->ref);
	return cfqg;
}

static void cfq_link_cfqq_cfqg(struct cfq_queue *cfqq, struct cfq_group *cfqg)
{
	/* Currently, all async queues are mapped to root group */
	if (!cfq_cfqq_sync(cfqq))
		cfqg = &cfqq->cfqd->root_group;

	cfqq->cfqg = cfqg;
	/* cfqq reference on cfqg */
	atomic_inc(&cfqq->cfqg->ref);
}

static void cfq_put_cfqg(struct cfq_group *cfqg)
{
	struct cfq_rb_root *st;
	int i, j;

	BUG_ON(atomic_read(&cfqg->ref) <= 0);
	if (!atomic_dec_and_test(&cfqg->ref))
		return;
	for_each_cfqg_st(cfqg, i, j, st)
		BUG_ON(!RB_EMPTY_ROOT(&st->rb) || st->active != NULL);
	kfree(cfqg);
}

static void cfq_destroy_cfqg(struct cfq_data *cfqd, struct cfq_group *cfqg)
{
	/* Something wrong if we are trying to remove same group twice */
	BUG_ON(hlist_unhashed(&cfqg->cfqd_node));

	hlist_del_init(&cfqg->cfqd_node);

	/*
	 * Put the reference taken at the time of creation so that when all
	 * queues are gone, group can be destroyed.
	 */
	cfq_put_cfqg(cfqg);
}

static void cfq_release_cfq_groups(struct cfq_data *cfqd)
{
	struct hlist_node *pos, *n;
	struct cfq_group *cfqg;

	hlist_for_each_entry_safe(cfqg, pos, n, &cfqd->cfqg_list, cfqd_node) {
		/*
		 * If cgroup removal path got to blk_group first and removed
		 * it from cgroup list, then it will take care of destroying
		 * cfqg also.
		 */
		if (!cfq_blkiocg_del_blkio_group(&cfqg->blkg))
			cfq_destroy_cfqg(cfqd, cfqg);
	}
}

/*
 * Blk cgroup controller notification saying that blkio_group object is being
 * delinked as associated cgroup object is going away. That also means that
 * no new IO will come in this group. So get rid of this group as soon as
 * any pending IO in the group is finished.
 *
 * This function is called under rcu_read_lock(). key is the rcu protected
 * pointer. That means "key" is a valid cfq_data pointer as long as we are rcu
 * read lock.
 *
 * "key" was fetched from blkio_group under blkio_cgroup->lock. That means
 * it should not be NULL as even if elevator was exiting, cgroup deltion
 * path got to it first.
 */
void cfq_unlink_blkio_group(void *key, struct blkio_group *blkg)
{
	unsigned long  flags;
	struct cfq_data *cfqd = key;

	spin_lock_irqsave(cfqd->queue->queue_lock, flags);
	cfq_destroy_cfqg(cfqd, cfqg_of_blkg(blkg));
	spin_unlock_irqrestore(cfqd->queue->queue_lock, flags);
}

#else /* GROUP_IOSCHED */
static struct cfq_group *cfq_get_cfqg(struct cfq_data *cfqd, int create)
{
	return &cfqd->root_group;
}

static inline struct cfq_group *cfq_ref_get_cfqg(struct cfq_group *cfqg)
{
	return cfqg;
}

static inline void
cfq_link_cfqq_cfqg(struct cfq_queue *cfqq, struct cfq_group *cfqg) {
	cfqq->cfqg = cfqg;
}

static void cfq_release_cfq_groups(struct cfq_data *cfqd) {}
static inline void cfq_put_cfqg(struct cfq_group *cfqg) {}

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
	struct cfq_rb_root *service_tree;
	int left;
	int new_cfqq = 1;
	int group_changed = 0;

#ifdef CONFIG_CFQ_GROUP_IOSCHED
	if (!cfqd->cfq_group_isolation
	    && cfqq_type(cfqq) == SYNC_NOIDLE_WORKLOAD
	    && cfqq->cfqg && cfqq->cfqg != &cfqd->root_group) {
		/* Move this cfq to root group */
		cfq_log_cfqq(cfqd, cfqq, "moving to root group");
		if (!RB_EMPTY_NODE(&cfqq->rb_node))
			cfq_group_service_tree_del(cfqd, cfqq->cfqg);
		cfqq->orig_cfqg = cfqq->cfqg;
		cfqq->cfqg = &cfqd->root_group;
		atomic_inc(&cfqd->root_group.ref);
		group_changed = 1;
	} else if (!cfqd->cfq_group_isolation
		   && cfqq_type(cfqq) == SYNC_WORKLOAD && cfqq->orig_cfqg) {
		/* cfqq is sequential now needs to go to its original group */
		BUG_ON(cfqq->cfqg != &cfqd->root_group);
		if (!RB_EMPTY_NODE(&cfqq->rb_node))
			cfq_group_service_tree_del(cfqd, cfqq->cfqg);
		cfq_put_cfqg(cfqq->cfqg);
		cfqq->cfqg = cfqq->orig_cfqg;
		cfqq->orig_cfqg = NULL;
		group_changed = 1;
		cfq_log_cfqq(cfqd, cfqq, "moved to origin group");
	}
#endif

	service_tree = service_tree_for(cfqq->cfqg, cfqq_prio(cfqq),
						cfqq_type(cfqq));
	if (cfq_class_idle(cfqq)) {
		rb_key = CFQ_IDLE_DELAY;
		parent = rb_last(&service_tree->rb);
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
		__cfqq = cfq_rb_first(service_tree);
		rb_key += __cfqq ? __cfqq->rb_key : jiffies;
	}

	if (!RB_EMPTY_NODE(&cfqq->rb_node)) {
		new_cfqq = 0;
		/*
		 * same position, nothing more to do
		 */
		if (rb_key == cfqq->rb_key &&
		    cfqq->service_tree == service_tree)
			return;

		cfq_rb_erase(&cfqq->rb_node, cfqq->service_tree);
		cfqq->service_tree = NULL;
	}

	left = 1;
	parent = NULL;
	cfqq->service_tree = service_tree;
	p = &service_tree->rb.rb_node;
	while (*p) {
		struct rb_node **n;

		parent = *p;
		__cfqq = rb_entry(parent, struct cfq_queue, rb_node);

		/*
		 * sort by key, that represents service time.
		 */
		if (time_before(rb_key, __cfqq->rb_key))
			n = &(*p)->rb_left;
		else {
			n = &(*p)->rb_right;
			left = 0;
		}

		p = n;
	}

	if (left)
		service_tree->left = &cfqq->rb_node;

	cfqq->rb_key = rb_key;
	rb_link_node(&cfqq->rb_node, parent, p);
	rb_insert_color(&cfqq->rb_node, &service_tree->rb);
	service_tree->count++;
	if ((add_front || !new_cfqq) && !group_changed)
		return;
	cfq_group_service_tree_add(cfqd, cfqq->cfqg);
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

	cfq_group_service_tree_del(cfqd, cfqq->cfqg);
	BUG_ON(!cfqd->busy_queues);
	cfqd->busy_queues--;
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
	struct request *__alias, *prev;

	cfqq->queued[rq_is_sync(rq)]++;

	/*
	 * looks a little odd, but the first insert might return an alias.
	 * if that happens, put the alias on the dispatch list
	 */
	while ((__alias = elv_rb_add(&cfqq->sort_list, rq)) != NULL)
		cfq_dispatch_insert(cfqd->queue, __alias);

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
	cfq_blkiocg_update_io_remove_stats(&(RQ_CFQG(rq))->blkg,
					rq_data_dir(rq), rq_is_sync(rq));
	cfq_add_rq_rb(rq);
	cfq_blkiocg_update_io_add_stats(&(RQ_CFQG(rq))->blkg,
			&cfqq->cfqd->serving_group->blkg, rq_data_dir(rq),
			rq_is_sync(rq));
}

static struct request *
cfq_find_rq_fmerge(struct cfq_data *cfqd, struct bio *bio)
{
	struct task_struct *tsk = current;
	struct cfq_io_context *cic;
	struct cfq_queue *cfqq;

	cic = cfq_cic_lookup(cfqd, tsk->io_context);
	if (!cic)
		return NULL;

	cfqq = cic_to_cfqq(cic, cfq_bio_sync(bio));
	if (cfqq) {
		sector_t sector = bio->bi_sector + bio_sectors(bio);

		return elv_rb_find(&cfqq->sort_list, sector);
	}

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
	cfq_blkiocg_update_io_remove_stats(&(RQ_CFQG(rq))->blkg,
					rq_data_dir(rq), rq_is_sync(rq));
	if (rq->cmd_flags & REQ_META) {
		WARN_ON(!cfqq->meta_pending);
		cfqq->meta_pending--;
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
	cfq_blkiocg_update_io_merged_stats(&(RQ_CFQG(req))->blkg,
					bio_data_dir(bio), cfq_bio_sync(bio));
}

static void
cfq_merged_requests(struct request_queue *q, struct request *rq,
		    struct request *next)
{
	struct cfq_queue *cfqq = RQ_CFQQ(rq);
	/*
	 * reposition in fifo if next is older than rq
	 */
	if (!list_empty(&rq->queuelist) && !list_empty(&next->queuelist) &&
	    time_before(rq_fifo_time(next), rq_fifo_time(rq))) {
		list_move(&rq->queuelist, &next->queuelist);
		rq_set_fifo_time(rq, rq_fifo_time(next));
	}

	if (cfqq->next_rq == next)
		cfqq->next_rq = rq;
	cfq_remove_request(next);
	cfq_blkiocg_update_io_merged_stats(&(RQ_CFQG(rq))->blkg,
					rq_data_dir(next), rq_is_sync(next));
}

static int cfq_allow_merge(struct request_queue *q, struct request *rq,
			   struct bio *bio)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_io_context *cic;
	struct cfq_queue *cfqq;

	/*
	 * Disallow merge of a sync bio into an async request.
	 */
	if (cfq_bio_sync(bio) && !rq_is_sync(rq))
		return false;

	/*
	 * Lookup the cfqq that this bio will be queued with. Allow
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
	cfq_blkiocg_update_idle_time_stats(&cfqq->cfqg->blkg);
}

static void __cfq_set_active_queue(struct cfq_data *cfqd,
				   struct cfq_queue *cfqq)
{
	if (cfqq) {
		cfq_log_cfqq(cfqd, cfqq, "set_active wl_prio:%d wl_type:%d",
				cfqd->serving_prio, cfqd->serving_type);
		cfq_blkiocg_update_avg_queue_size_stats(&cfqq->cfqg->blkg);
		cfqq->slice_start = 0;
		cfqq->dispatch_start = jiffies;
		cfqq->allocated_slice = 0;
		cfqq->slice_end = 0;
		cfqq->slice_dispatch = 0;

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
	if (timed_out && !cfq_cfqq_slice_new(cfqq)) {
		cfqq->slice_resid = cfqq->slice_end - jiffies;
		cfq_log_cfqq(cfqd, cfqq, "resid=%ld", cfqq->slice_resid);
	}

	cfq_group_served(cfqd, cfqq->cfqg, cfqq);

	if (cfq_cfqq_on_rr(cfqq) && RB_EMPTY_ROOT(&cfqq->sort_list))
		cfq_del_cfqq_rr(cfqd, cfqq);

	cfq_resort_rr_list(cfqd, cfqq);

	if (cfqq == cfqd->active_queue)
		cfqd->active_queue = NULL;

	if (&cfqq->cfqg->rb_node == cfqd->grp_service_tree.active)
		cfqd->grp_service_tree.active = NULL;

	if (cfqd->active_cic) {
		put_io_context(cfqd->active_cic->ioc);
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
	struct cfq_rb_root *service_tree =
		service_tree_for(cfqd->serving_group, cfqd->serving_prio,
					cfqd->serving_type);

	if (!cfqd->rq_queued)
		return NULL;

	/* There is nothing to dispatch */
	if (!service_tree)
		return NULL;
	if (RB_EMPTY_ROOT(&service_tree->rb))
		return NULL;
	return cfq_rb_first(service_tree);
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
	enum wl_prio_t prio = cfqq_prio(cfqq);
	struct cfq_rb_root *service_tree = cfqq->service_tree;

	BUG_ON(!service_tree);
	BUG_ON(!service_tree->count);

	/* We never do for idle class queues. */
	if (prio == IDLE_WORKLOAD)
		return false;

	/* We do for queues that were marked with idle window flag. */
	if (cfq_cfqq_idle_window(cfqq) &&
	   !(blk_queue_nonrot(cfqd->queue) && cfqd->hw_tag))
		return true;

	/*
	 * Otherwise, we do only if they are the last ones
	 * in their service tree.
	 */
	if (service_tree->count == 1 && cfq_cfqq_sync(cfqq))
		return 1;
	cfq_log_cfqq(cfqd, cfqq, "Not idling. st->count:%d",
			service_tree->count);
	return 0;
}

static void cfq_arm_slice_timer(struct cfq_data *cfqd)
{
	struct cfq_queue *cfqq = cfqd->active_queue;
	struct cfq_io_context *cic;
	unsigned long sl;

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
	if (!cfqd->cfq_slice_idle || !cfq_should_idle(cfqd, cfqq))
		return;

	/*
	 * still active requests from this queue, don't idle
	 */
	if (cfqq->dispatched)
		return;

	/*
	 * task has exited, don't wait
	 */
	cic = cfqd->active_cic;
	if (!cic || !atomic_read(&cic->ioc->nr_tasks))
		return;

	/*
	 * If our average think time is larger than the remaining time
	 * slice, then don't idle. This avoids overrunning the allotted
	 * time slice.
	 */
	if (sample_valid(cic->ttime_samples) &&
	    (cfqq->slice_end - jiffies < cic->ttime_mean)) {
		cfq_log_cfqq(cfqd, cfqq, "Not idling. think_time:%d",
				cic->ttime_mean);
		return;
	}

	cfq_mark_cfqq_wait_request(cfqq);

	sl = cfqd->cfq_slice_idle;

	mod_timer(&cfqd->idle_slice_timer, jiffies + sl);
	cfq_blkiocg_update_set_idle_time_stats(&cfqq->cfqg->blkg);
	cfq_log_cfqq(cfqd, cfqq, "arm_idle: %lu", sl);
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
	elv_dispatch_sort(q, rq);

	cfqd->rq_in_flight[cfq_cfqq_sync(cfqq)]++;
	cfq_blkiocg_update_dispatch_stats(&cfqq->cfqg->blkg, blk_rq_bytes(rq),
					rq_data_dir(rq), rq_is_sync(rq));
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
	if (time_before(jiffies, rq_fifo_time(rq)))
		rq = NULL;

	cfq_log_cfqq(cfqq->cfqd, cfqq, "fifo=%p", rq);
	return rq;
}

static inline int
cfq_prio_to_maxrq(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	const int base_rq = cfqd->cfq_slice_async_rq;

	WARN_ON(cfqq->ioprio >= IOPRIO_BE_NR);

	return 2 * (base_rq + base_rq * (CFQ_PRIO_LISTS - 1 - cfqq->ioprio));
}

/*
 * Must be called with the queue_lock held.
 */
static int cfqq_process_refs(struct cfq_queue *cfqq)
{
	int process_refs, io_refs;

	io_refs = cfqq->allocated[READ] + cfqq->allocated[WRITE];
	process_refs = atomic_read(&cfqq->ref) - io_refs;
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
		atomic_add(process_refs, &new_cfqq->ref);
	} else {
		new_cfqq->new_cfqq = cfqq;
		atomic_add(new_process_refs, &cfqq->ref);
	}
}

static enum wl_type_t cfq_choose_wl(struct cfq_data *cfqd,
				struct cfq_group *cfqg, enum wl_prio_t prio)
{
	struct cfq_queue *queue;
	int i;
	bool key_valid = false;
	unsigned long lowest_key = 0;
	enum wl_type_t cur_best = SYNC_NOIDLE_WORKLOAD;

	for (i = 0; i <= SYNC_WORKLOAD; ++i) {
		/* select the one with lowest rb_key */
		queue = cfq_rb_first(service_tree_for(cfqg, prio, i));
		if (queue &&
		    (!key_valid || time_before(queue->rb_key, lowest_key))) {
			lowest_key = queue->rb_key;
			cur_best = i;
			key_valid = true;
		}
	}

	return cur_best;
}

static void choose_service_tree(struct cfq_data *cfqd, struct cfq_group *cfqg)
{
	unsigned slice;
	unsigned count;
	struct cfq_rb_root *st;
	unsigned group_slice;

	if (!cfqg) {
		cfqd->serving_prio = IDLE_WORKLOAD;
		cfqd->workload_expires = jiffies + 1;
		return;
	}

	/* Choose next priority. RT > BE > IDLE */
	if (cfq_group_busy_queues_wl(RT_WORKLOAD, cfqd, cfqg))
		cfqd->serving_prio = RT_WORKLOAD;
	else if (cfq_group_busy_queues_wl(BE_WORKLOAD, cfqd, cfqg))
		cfqd->serving_prio = BE_WORKLOAD;
	else {
		cfqd->serving_prio = IDLE_WORKLOAD;
		cfqd->workload_expires = jiffies + 1;
		return;
	}

	/*
	 * For RT and BE, we have to choose also the type
	 * (SYNC, SYNC_NOIDLE, ASYNC), and to compute a workload
	 * expiration time
	 */
	st = service_tree_for(cfqg, cfqd->serving_prio, cfqd->serving_type);
	count = st->count;

	/*
	 * check workload expiration, and that we still have other queues ready
	 */
	if (count && !time_after(jiffies, cfqd->workload_expires))
		return;

	/* otherwise select new workload type */
	cfqd->serving_type =
		cfq_choose_wl(cfqd, cfqg, cfqd->serving_prio);
	st = service_tree_for(cfqg, cfqd->serving_prio, cfqd->serving_type);
	count = st->count;

	/*
	 * the workload slice is computed as a fraction of target latency
	 * proportional to the number of queues in that workload, over
	 * all the queues in the same priority class
	 */
	group_slice = cfq_group_slice(cfqd, cfqg);

	slice = group_slice * count /
		max_t(unsigned, cfqg->busy_queues_avg[cfqd->serving_prio],
		      cfq_group_busy_queues_wl(cfqd->serving_prio, cfqd, cfqg));

	if (cfqd->serving_type == ASYNC_WORKLOAD) {
		unsigned int tmp;

		/*
		 * Async queues are currently system wide. Just taking
		 * proportion of queues with-in same group will lead to higher
		 * async ratio system wide as generally root group is going
		 * to have higher weight. A more accurate thing would be to
		 * calculate system wide asnc/sync ratio.
		 */
		tmp = cfq_target_latency * cfqg_busy_async_queues(cfqd, cfqg);
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
	st->active = &cfqg->rb_node;
	update_min_vdisktime(st);
	return cfqg;
}

static void cfq_choose_cfqg(struct cfq_data *cfqd)
{
	struct cfq_group *cfqg = cfq_get_next_cfqg(cfqd);

	cfqd->serving_group = cfqg;

	/* Restore the workload type data */
	if (cfqg->saved_workload_slice) {
		cfqd->workload_expires = jiffies + cfqg->saved_workload_slice;
		cfqd->serving_type = cfqg->saved_workload;
		cfqd->serving_prio = cfqg->saved_serving_prio;
	} else
		cfqd->workload_expires = jiffies - 1;

	choose_service_tree(cfqd, cfqg);
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
			goto expire;
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
	if (timer_pending(&cfqd->idle_slice_timer) ||
	    (cfqq->dispatched && cfq_should_idle(cfqd, cfqq))) {
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
		return 1;
	if (time_after(jiffies + cfqd->cfq_slice_idle * cfqq->dispatched,
		cfqq->slice_end))
		return 1;

	return 0;
}

static bool cfq_may_dispatch(struct cfq_data *cfqd, struct cfq_queue *cfqq)
{
	unsigned int max_dispatch;

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
		/*
		 * idle queue must always only have a single IO in flight
		 */
		if (cfq_class_idle(cfqq))
			return false;

		/*
		 * We have other queues, don't allow more IO from this one
		 */
		if (cfqd->busy_queues > 1 && cfq_slice_used_soon(cfqd, cfqq))
			return false;

		/*
		 * Sole queue user, no limit
		 */
		if (cfqd->busy_queues == 1)
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

	if (!cfq_may_dispatch(cfqd, cfqq))
		return false;

	/*
	 * follow expired path, else get first next available
	 */
	rq = cfq_check_fifo(cfqq);
	if (!rq)
		rq = cfqq->next_rq;

	/*
	 * insert request into driver dispatch list
	 */
	cfq_dispatch_insert(cfqd->queue, rq);

	if (!cfqd->active_cic) {
		struct cfq_io_context *cic = RQ_CIC(rq);

		atomic_long_inc(&cic->ioc->refcount);
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
	struct cfq_group *cfqg, *orig_cfqg;

	BUG_ON(atomic_read(&cfqq->ref) <= 0);

	if (!atomic_dec_and_test(&cfqq->ref))
		return;

	cfq_log_cfqq(cfqd, cfqq, "put_queue");
	BUG_ON(rb_first(&cfqq->sort_list));
	BUG_ON(cfqq->allocated[READ] + cfqq->allocated[WRITE]);
	cfqg = cfqq->cfqg;
	orig_cfqg = cfqq->orig_cfqg;

	if (unlikely(cfqd->active_queue == cfqq)) {
		__cfq_slice_expired(cfqd, cfqq, 0);
		cfq_schedule_dispatch(cfqd);
	}

	BUG_ON(cfq_cfqq_on_rr(cfqq));
	kmem_cache_free(cfq_pool, cfqq);
	cfq_put_cfqg(cfqg);
	if (orig_cfqg)
		cfq_put_cfqg(orig_cfqg);
}

/*
 * Must always be called with the rcu_read_lock() held
 */
static void
__call_for_each_cic(struct io_context *ioc,
		    void (*func)(struct io_context *, struct cfq_io_context *))
{
	struct cfq_io_context *cic;
	struct hlist_node *n;

	hlist_for_each_entry_rcu(cic, n, &ioc->cic_list, cic_list)
		func(ioc, cic);
}

/*
 * Call func for each cic attached to this ioc.
 */
static void
call_for_each_cic(struct io_context *ioc,
		  void (*func)(struct io_context *, struct cfq_io_context *))
{
	rcu_read_lock();
	__call_for_each_cic(ioc, func);
	rcu_read_unlock();
}

static void cfq_cic_free_rcu(struct rcu_head *head)
{
	struct cfq_io_context *cic;

	cic = container_of(head, struct cfq_io_context, rcu_head);

	kmem_cache_free(cfq_ioc_pool, cic);
	elv_ioc_count_dec(cfq_ioc_count);

	if (ioc_gone) {
		/*
		 * CFQ scheduler is exiting, grab exit lock and check
		 * the pending io context count. If it hits zero,
		 * complete ioc_gone and set it back to NULL
		 */
		spin_lock(&ioc_gone_lock);
		if (ioc_gone && !elv_ioc_count_read(cfq_ioc_count)) {
			complete(ioc_gone);
			ioc_gone = NULL;
		}
		spin_unlock(&ioc_gone_lock);
	}
}

static void cfq_cic_free(struct cfq_io_context *cic)
{
	call_rcu(&cic->rcu_head, cfq_cic_free_rcu);
}

static void cic_free_func(struct io_context *ioc, struct cfq_io_context *cic)
{
	unsigned long flags;
	unsigned long dead_key = (unsigned long) cic->key;

	BUG_ON(!(dead_key & CIC_DEAD_KEY));

	spin_lock_irqsave(&ioc->lock, flags);
	radix_tree_delete(&ioc->radix_root, dead_key >> CIC_DEAD_INDEX_SHIFT);
	hlist_del_rcu(&cic->cic_list);
	spin_unlock_irqrestore(&ioc->lock, flags);

	cfq_cic_free(cic);
}

/*
 * Must be called with rcu_read_lock() held or preemption otherwise disabled.
 * Only two callers of this - ->dtor() which is called with the rcu_read_lock(),
 * and ->trim() which is called with the task lock held
 */
static void cfq_free_io_context(struct io_context *ioc)
{
	/*
	 * ioc->refcount is zero here, or we are called from elv_unregister(),
	 * so no more cic's are allowed to be linked into this ioc.  So it
	 * should be ok to iterate over the known list, we will see all cic's
	 * since no new ones are added.
	 */
	__call_for_each_cic(ioc, cic_free_func);
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

static void __cfq_exit_single_io_context(struct cfq_data *cfqd,
					 struct cfq_io_context *cic)
{
	struct io_context *ioc = cic->ioc;

	list_del_init(&cic->queue_list);

	/*
	 * Make sure dead mark is seen for dead queues
	 */
	smp_wmb();
	cic->key = cfqd_dead_key(cfqd);

	if (ioc->ioc_data == cic)
		rcu_assign_pointer(ioc->ioc_data, NULL);

	if (cic->cfqq[BLK_RW_ASYNC]) {
		cfq_exit_cfqq(cfqd, cic->cfqq[BLK_RW_ASYNC]);
		cic->cfqq[BLK_RW_ASYNC] = NULL;
	}

	if (cic->cfqq[BLK_RW_SYNC]) {
		cfq_exit_cfqq(cfqd, cic->cfqq[BLK_RW_SYNC]);
		cic->cfqq[BLK_RW_SYNC] = NULL;
	}
}

static void cfq_exit_single_io_context(struct io_context *ioc,
				       struct cfq_io_context *cic)
{
	struct cfq_data *cfqd = cic_to_cfqd(cic);

	if (cfqd) {
		struct request_queue *q = cfqd->queue;
		unsigned long flags;

		spin_lock_irqsave(q->queue_lock, flags);

		/*
		 * Ensure we get a fresh copy of the ->key to prevent
		 * race between exiting task and queue
		 */
		smp_read_barrier_depends();
		if (cic->key == cfqd)
			__cfq_exit_single_io_context(cfqd, cic);

		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}

/*
 * The process that ioc belongs to has exited, we need to clean up
 * and put the internal structures we have that belongs to that process.
 */
static void cfq_exit_io_context(struct io_context *ioc)
{
	call_for_each_cic(ioc, cfq_exit_single_io_context);
}

static struct cfq_io_context *
cfq_alloc_io_context(struct cfq_data *cfqd, gfp_t gfp_mask)
{
	struct cfq_io_context *cic;

	cic = kmem_cache_alloc_node(cfq_ioc_pool, gfp_mask | __GFP_ZERO,
							cfqd->queue->node);
	if (cic) {
		cic->last_end_request = jiffies;
		INIT_LIST_HEAD(&cic->queue_list);
		INIT_HLIST_NODE(&cic->cic_list);
		cic->dtor = cfq_free_io_context;
		cic->exit = cfq_exit_io_context;
		elv_ioc_count_inc(cfq_ioc_count);
	}

	return cic;
}

static void cfq_init_prio_data(struct cfq_queue *cfqq, struct io_context *ioc)
{
	struct task_struct *tsk = current;
	int ioprio_class;

	if (!cfq_cfqq_prio_changed(cfqq))
		return;

	ioprio_class = IOPRIO_PRIO_CLASS(ioc->ioprio);
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
		cfqq->ioprio = task_ioprio(ioc);
		cfqq->ioprio_class = IOPRIO_CLASS_RT;
		break;
	case IOPRIO_CLASS_BE:
		cfqq->ioprio = task_ioprio(ioc);
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
	cfqq->org_ioprio_class = cfqq->ioprio_class;
	cfq_clear_cfqq_prio_changed(cfqq);
}

static void changed_ioprio(struct io_context *ioc, struct cfq_io_context *cic)
{
	struct cfq_data *cfqd = cic_to_cfqd(cic);
	struct cfq_queue *cfqq;
	unsigned long flags;

	if (unlikely(!cfqd))
		return;

	spin_lock_irqsave(cfqd->queue->queue_lock, flags);

	cfqq = cic->cfqq[BLK_RW_ASYNC];
	if (cfqq) {
		struct cfq_queue *new_cfqq;
		new_cfqq = cfq_get_queue(cfqd, BLK_RW_ASYNC, cic->ioc,
						GFP_ATOMIC);
		if (new_cfqq) {
			cic->cfqq[BLK_RW_ASYNC] = new_cfqq;
			cfq_put_queue(cfqq);
		}
	}

	cfqq = cic->cfqq[BLK_RW_SYNC];
	if (cfqq)
		cfq_mark_cfqq_prio_changed(cfqq);

	spin_unlock_irqrestore(cfqd->queue->queue_lock, flags);
}

static void cfq_ioc_set_ioprio(struct io_context *ioc)
{
	call_for_each_cic(ioc, changed_ioprio);
	ioc->ioprio_changed = 0;
}

static void cfq_init_cfqq(struct cfq_data *cfqd, struct cfq_queue *cfqq,
			  pid_t pid, bool is_sync)
{
	RB_CLEAR_NODE(&cfqq->rb_node);
	RB_CLEAR_NODE(&cfqq->p_node);
	INIT_LIST_HEAD(&cfqq->fifo);

	atomic_set(&cfqq->ref, 0);
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
static void changed_cgroup(struct io_context *ioc, struct cfq_io_context *cic)
{
	struct cfq_queue *sync_cfqq = cic_to_cfqq(cic, 1);
	struct cfq_data *cfqd = cic_to_cfqd(cic);
	unsigned long flags;
	struct request_queue *q;

	if (unlikely(!cfqd))
		return;

	q = cfqd->queue;

	spin_lock_irqsave(q->queue_lock, flags);

	if (sync_cfqq) {
		/*
		 * Drop reference to sync queue. A new sync queue will be
		 * assigned in new group upon arrival of a fresh request.
		 */
		cfq_log_cfqq(cfqd, sync_cfqq, "changed cgroup");
		cic_set_cfqq(cic, NULL, 1);
		cfq_put_queue(sync_cfqq);
	}

	spin_unlock_irqrestore(q->queue_lock, flags);
}

static void cfq_ioc_set_cgroup(struct io_context *ioc)
{
	call_for_each_cic(ioc, changed_cgroup);
	ioc->cgroup_changed = 0;
}
#endif  /* CONFIG_CFQ_GROUP_IOSCHED */

static struct cfq_queue *
cfq_find_alloc_queue(struct cfq_data *cfqd, bool is_sync,
		     struct io_context *ioc, gfp_t gfp_mask)
{
	struct cfq_queue *cfqq, *new_cfqq = NULL;
	struct cfq_io_context *cic;
	struct cfq_group *cfqg;

retry:
	cfqg = cfq_get_cfqg(cfqd, 1);
	cic = cfq_cic_lookup(cfqd, ioc);
	/* cic always exists here */
	cfqq = cic_to_cfqq(cic, is_sync);

	/*
	 * Always try a new alloc if we fell back to the OOM cfqq
	 * originally, since it should just be a temporary situation.
	 */
	if (!cfqq || cfqq == &cfqd->oom_cfqq) {
		cfqq = NULL;
		if (new_cfqq) {
			cfqq = new_cfqq;
			new_cfqq = NULL;
		} else if (gfp_mask & __GFP_WAIT) {
			spin_unlock_irq(cfqd->queue->queue_lock);
			new_cfqq = kmem_cache_alloc_node(cfq_pool,
					gfp_mask | __GFP_ZERO,
					cfqd->queue->node);
			spin_lock_irq(cfqd->queue->queue_lock);
			if (new_cfqq)
				goto retry;
		} else {
			cfqq = kmem_cache_alloc_node(cfq_pool,
					gfp_mask | __GFP_ZERO,
					cfqd->queue->node);
		}

		if (cfqq) {
			cfq_init_cfqq(cfqd, cfqq, current->pid, is_sync);
			cfq_init_prio_data(cfqq, ioc);
			cfq_link_cfqq_cfqg(cfqq, cfqg);
			cfq_log_cfqq(cfqd, cfqq, "alloced");
		} else
			cfqq = &cfqd->oom_cfqq;
	}

	if (new_cfqq)
		kmem_cache_free(cfq_pool, new_cfqq);

	return cfqq;
}

static struct cfq_queue **
cfq_async_queue_prio(struct cfq_data *cfqd, int ioprio_class, int ioprio)
{
	switch (ioprio_class) {
	case IOPRIO_CLASS_RT:
		return &cfqd->async_cfqq[0][ioprio];
	case IOPRIO_CLASS_BE:
		return &cfqd->async_cfqq[1][ioprio];
	case IOPRIO_CLASS_IDLE:
		return &cfqd->async_idle_cfqq;
	default:
		BUG();
	}
}

static struct cfq_queue *
cfq_get_queue(struct cfq_data *cfqd, bool is_sync, struct io_context *ioc,
	      gfp_t gfp_mask)
{
	const int ioprio = task_ioprio(ioc);
	const int ioprio_class = task_ioprio_class(ioc);
	struct cfq_queue **async_cfqq = NULL;
	struct cfq_queue *cfqq = NULL;

	if (!is_sync) {
		async_cfqq = cfq_async_queue_prio(cfqd, ioprio_class, ioprio);
		cfqq = *async_cfqq;
	}

	if (!cfqq)
		cfqq = cfq_find_alloc_queue(cfqd, is_sync, ioc, gfp_mask);

	/*
	 * pin the queue now that it's allocated, scheduler exit will prune it
	 */
	if (!is_sync && !(*async_cfqq)) {
		atomic_inc(&cfqq->ref);
		*async_cfqq = cfqq;
	}

	atomic_inc(&cfqq->ref);
	return cfqq;
}

/*
 * We drop cfq io contexts lazily, so we may find a dead one.
 */
static void
cfq_drop_dead_cic(struct cfq_data *cfqd, struct io_context *ioc,
		  struct cfq_io_context *cic)
{
	unsigned long flags;

	WARN_ON(!list_empty(&cic->queue_list));
	BUG_ON(cic->key != cfqd_dead_key(cfqd));

	spin_lock_irqsave(&ioc->lock, flags);

	BUG_ON(ioc->ioc_data == cic);

	radix_tree_delete(&ioc->radix_root, cfqd->cic_index);
	hlist_del_rcu(&cic->cic_list);
	spin_unlock_irqrestore(&ioc->lock, flags);

	cfq_cic_free(cic);
}

static struct cfq_io_context *
cfq_cic_lookup(struct cfq_data *cfqd, struct io_context *ioc)
{
	struct cfq_io_context *cic;
	unsigned long flags;

	if (unlikely(!ioc))
		return NULL;

	rcu_read_lock();

	/*
	 * we maintain a last-hit cache, to avoid browsing over the tree
	 */
	cic = rcu_dereference(ioc->ioc_data);
	if (cic && cic->key == cfqd) {
		rcu_read_unlock();
		return cic;
	}

	do {
		cic = radix_tree_lookup(&ioc->radix_root, cfqd->cic_index);
		rcu_read_unlock();
		if (!cic)
			break;
		if (unlikely(cic->key != cfqd)) {
			cfq_drop_dead_cic(cfqd, ioc, cic);
			rcu_read_lock();
			continue;
		}

		spin_lock_irqsave(&ioc->lock, flags);
		rcu_assign_pointer(ioc->ioc_data, cic);
		spin_unlock_irqrestore(&ioc->lock, flags);
		break;
	} while (1);

	return cic;
}

/*
 * Add cic into ioc, using cfqd as the search key. This enables us to lookup
 * the process specific cfq io context when entered from the block layer.
 * Also adds the cic to a per-cfqd list, used when this queue is removed.
 */
static int cfq_cic_link(struct cfq_data *cfqd, struct io_context *ioc,
			struct cfq_io_context *cic, gfp_t gfp_mask)
{
	unsigned long flags;
	int ret;

	ret = radix_tree_preload(gfp_mask);
	if (!ret) {
		cic->ioc = ioc;
		cic->key = cfqd;

		spin_lock_irqsave(&ioc->lock, flags);
		ret = radix_tree_insert(&ioc->radix_root,
						cfqd->cic_index, cic);
		if (!ret)
			hlist_add_head_rcu(&cic->cic_list, &ioc->cic_list);
		spin_unlock_irqrestore(&ioc->lock, flags);

		radix_tree_preload_end();

		if (!ret) {
			spin_lock_irqsave(cfqd->queue->queue_lock, flags);
			list_add(&cic->queue_list, &cfqd->cic_list);
			spin_unlock_irqrestore(cfqd->queue->queue_lock, flags);
		}
	}

	if (ret)
		printk(KERN_ERR "cfq: cic link failed!\n");

	return ret;
}

/*
 * Setup general io context and cfq io context. There can be several cfq
 * io contexts per general io context, if this process is doing io to more
 * than one device managed by cfq.
 */
static struct cfq_io_context *
cfq_get_io_context(struct cfq_data *cfqd, gfp_t gfp_mask)
{
	struct io_context *ioc = NULL;
	struct cfq_io_context *cic;

	might_sleep_if(gfp_mask & __GFP_WAIT);

	ioc = get_io_context(gfp_mask, cfqd->queue->node);
	if (!ioc)
		return NULL;

	cic = cfq_cic_lookup(cfqd, ioc);
	if (cic)
		goto out;

	cic = cfq_alloc_io_context(cfqd, gfp_mask);
	if (cic == NULL)
		goto err;

	if (cfq_cic_link(cfqd, ioc, cic, gfp_mask))
		goto err_free;

out:
	smp_read_barrier_depends();
	if (unlikely(ioc->ioprio_changed))
		cfq_ioc_set_ioprio(ioc);

#ifdef CONFIG_CFQ_GROUP_IOSCHED
	if (unlikely(ioc->cgroup_changed))
		cfq_ioc_set_cgroup(ioc);
#endif
	return cic;
err_free:
	cfq_cic_free(cic);
err:
	put_io_context(ioc);
	return NULL;
}

static void
cfq_update_io_thinktime(struct cfq_data *cfqd, struct cfq_io_context *cic)
{
	unsigned long elapsed = jiffies - cic->last_end_request;
	unsigned long ttime = min(elapsed, 2UL * cfqd->cfq_slice_idle);

	cic->ttime_samples = (7*cic->ttime_samples + 256) / 8;
	cic->ttime_total = (7*cic->ttime_total + 256*ttime) / 8;
	cic->ttime_mean = (cic->ttime_total + 128) / cic->ttime_samples;
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
		       struct cfq_io_context *cic)
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
	else if (!atomic_read(&cic->ioc->nr_tasks) || !cfqd->cfq_slice_idle ||
	    (!cfq_cfqq_deep(cfqq) && CFQQ_SEEKY(cfqq)))
		enable_idle = 0;
	else if (sample_valid(cic->ttime_samples)) {
		if (cic->ttime_mean > cfqd->cfq_slice_idle)
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
	if (rq_is_sync(rq) && !cfq_cfqq_sync(cfqq))
		return true;

	if (new_cfqq->cfqg != cfqq->cfqg)
		return false;

	if (cfq_slice_used(cfqq))
		return true;

	/* Allow preemption only if we are idling on sync-noidle tree */
	if (cfqd->serving_type == SYNC_NOIDLE_WORKLOAD &&
	    cfqq_type(new_cfqq) == SYNC_NOIDLE_WORKLOAD &&
	    new_cfqq->service_tree->count == 2 &&
	    RB_EMPTY_ROOT(&cfqq->sort_list))
		return true;

	/*
	 * So both queues are sync. Let the new request get disk time if
	 * it's a metadata request and the current queue is doing regular IO.
	 */
	if ((rq->cmd_flags & REQ_META) && !cfqq->meta_pending)
		return true;

	/*
	 * Allow an RT request to pre-empt an ongoing non-RT cfqq timeslice.
	 */
	if (cfq_class_rt(new_cfqq) && !cfq_class_rt(cfqq))
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
	cfq_log_cfqq(cfqd, cfqq, "preempt");
	cfq_slice_expired(cfqd, 1);

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
	struct cfq_io_context *cic = RQ_CIC(rq);

	cfqd->rq_queued++;
	if (rq->cmd_flags & REQ_META)
		cfqq->meta_pending++;

	cfq_update_io_thinktime(cfqd, cic);
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
				cfq_blkiocg_update_idle_time_stats(
						&cfqq->cfqg->blkg);
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
	cfq_init_prio_data(cfqq, RQ_CIC(rq)->ioc);

	rq_set_fifo_time(rq, jiffies + cfqd->cfq_fifo_expire[rq_is_sync(rq)]);
	list_add_tail(&rq->queuelist, &cfqq->fifo);
	cfq_add_rq_rb(rq);
	cfq_blkiocg_update_io_add_stats(&(RQ_CFQG(rq))->blkg,
			&cfqd->serving_group->blkg, rq_data_dir(rq),
			rq_is_sync(rq));
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
	struct cfq_io_context *cic = cfqd->active_cic;

	/* If there are other queues in the group, don't wait */
	if (cfqq->cfqg->nr_cfqq > 1)
		return false;

	if (cfq_slice_used(cfqq))
		return true;

	/* if slice left is less than think time, wait busy */
	if (cic && sample_valid(cic->ttime_samples)
	    && (cfqq->slice_end - jiffies < cic->ttime_mean))
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
	cfq_blkiocg_update_completion_stats(&cfqq->cfqg->blkg,
			rq_start_time_ns(rq), rq_io_start_time_ns(rq),
			rq_data_dir(rq), rq_is_sync(rq));

	cfqd->rq_in_flight[cfq_cfqq_sync(cfqq)]--;

	if (sync) {
		RQ_CIC(rq)->last_end_request = now;
		if (!time_after(rq->start_time + cfqd->cfq_fifo_expire[1], now))
			cfqd->last_delayed_sync = now;
	}

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
			cfqq->slice_end = jiffies + cfqd->cfq_slice_idle;
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

/*
 * we temporarily boost lower priority queues if they are holding fs exclusive
 * resources. they are boosted to normal prio (CLASS_BE/4)
 */
static void cfq_prio_boost(struct cfq_queue *cfqq)
{
	if (has_fs_excl()) {
		/*
		 * boost idle prio on transactions that would lock out other
		 * users of the filesystem
		 */
		if (cfq_class_idle(cfqq))
			cfqq->ioprio_class = IOPRIO_CLASS_BE;
		if (cfqq->ioprio > IOPRIO_NORM)
			cfqq->ioprio = IOPRIO_NORM;
	} else {
		/*
		 * unboost the queue (if needed)
		 */
		cfqq->ioprio_class = cfqq->org_ioprio_class;
		cfqq->ioprio = cfqq->org_ioprio;
	}
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
	struct cfq_io_context *cic;
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
		cfq_init_prio_data(cfqq, cic->ioc);
		cfq_prio_boost(cfqq);

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

		put_io_context(RQ_CIC(rq)->ioc);

		rq->elevator_private = NULL;
		rq->elevator_private2 = NULL;

		/* Put down rq reference on cfqg */
		cfq_put_cfqg(RQ_CFQG(rq));
		rq->elevator_private3 = NULL;

		cfq_put_queue(cfqq);
	}
}

static struct cfq_queue *
cfq_merge_cfqqs(struct cfq_data *cfqd, struct cfq_io_context *cic,
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
split_cfqq(struct cfq_io_context *cic, struct cfq_queue *cfqq)
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
cfq_set_request(struct request_queue *q, struct request *rq, gfp_t gfp_mask)
{
	struct cfq_data *cfqd = q->elevator->elevator_data;
	struct cfq_io_context *cic;
	const int rw = rq_data_dir(rq);
	const bool is_sync = rq_is_sync(rq);
	struct cfq_queue *cfqq;
	unsigned long flags;

	might_sleep_if(gfp_mask & __GFP_WAIT);

	cic = cfq_get_io_context(cfqd, gfp_mask);

	spin_lock_irqsave(q->queue_lock, flags);

	if (!cic)
		goto queue_fail;

new_queue:
	cfqq = cic_to_cfqq(cic, is_sync);
	if (!cfqq || cfqq == &cfqd->oom_cfqq) {
		cfqq = cfq_get_queue(cfqd, is_sync, cic->ioc, gfp_mask);
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
	atomic_inc(&cfqq->ref);

	spin_unlock_irqrestore(q->queue_lock, flags);

	rq->elevator_private = cic;
	rq->elevator_private2 = cfqq;
	rq->elevator_private3 = cfq_ref_get_cfqg(cfqq->cfqg);
	return 0;

queue_fail:
	if (cic)
		put_io_context(cic->ioc);

	cfq_schedule_dispatch(cfqd);
	spin_unlock_irqrestore(q->queue_lock, flags);
	cfq_log(cfqd, "set_request fail");
	return 1;
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

static void cfq_put_async_queues(struct cfq_data *cfqd)
{
	int i;

	for (i = 0; i < IOPRIO_BE_NR; i++) {
		if (cfqd->async_cfqq[0][i])
			cfq_put_queue(cfqd->async_cfqq[0][i]);
		if (cfqd->async_cfqq[1][i])
			cfq_put_queue(cfqd->async_cfqq[1][i]);
	}

	if (cfqd->async_idle_cfqq)
		cfq_put_queue(cfqd->async_idle_cfqq);
}

static void cfq_cfqd_free(struct rcu_head *head)
{
	kfree(container_of(head, struct cfq_data, rcu));
}

static void cfq_exit_queue(struct elevator_queue *e)
{
	struct cfq_data *cfqd = e->elevator_data;
	struct request_queue *q = cfqd->queue;

	cfq_shutdown_timer_wq(cfqd);

	spin_lock_irq(q->queue_lock);

	if (cfqd->active_queue)
		__cfq_slice_expired(cfqd, cfqd->active_queue, 0);

	while (!list_empty(&cfqd->cic_list)) {
		struct cfq_io_context *cic = list_entry(cfqd->cic_list.next,
							struct cfq_io_context,
							queue_list);

		__cfq_exit_single_io_context(cfqd, cic);
	}

	cfq_put_async_queues(cfqd);
	cfq_release_cfq_groups(cfqd);
	cfq_blkiocg_del_blkio_group(&cfqd->root_group.blkg);

	spin_unlock_irq(q->queue_lock);

	cfq_shutdown_timer_wq(cfqd);

	spin_lock(&cic_index_lock);
	ida_remove(&cic_index_ida, cfqd->cic_index);
	spin_unlock(&cic_index_lock);

	/* Wait for cfqg->blkg->key accessors to exit their grace periods. */
	call_rcu(&cfqd->rcu, cfq_cfqd_free);
}

static int cfq_alloc_cic_index(void)
{
	int index, error;

	do {
		if (!ida_pre_get(&cic_index_ida, GFP_KERNEL))
			return -ENOMEM;

		spin_lock(&cic_index_lock);
		error = ida_get_new(&cic_index_ida, &index);
		spin_unlock(&cic_index_lock);
		if (error && error != -EAGAIN)
			return error;
	} while (error);

	return index;
}

static void *cfq_init_queue(struct request_queue *q)
{
	struct cfq_data *cfqd;
	int i, j;
	struct cfq_group *cfqg;
	struct cfq_rb_root *st;

	i = cfq_alloc_cic_index();
	if (i < 0)
		return NULL;

	cfqd = kmalloc_node(sizeof(*cfqd), GFP_KERNEL | __GFP_ZERO, q->node);
	if (!cfqd)
		return NULL;

	cfqd->cic_index = i;

	/* Init root service tree */
	cfqd->grp_service_tree = CFQ_RB_ROOT;

	/* Init root group */
	cfqg = &cfqd->root_group;
	for_each_cfqg_st(cfqg, i, j, st)
		*st = CFQ_RB_ROOT;
	RB_CLEAR_NODE(&cfqg->rb_node);

	/* Give preference to root group over other groups */
	cfqg->weight = 2*BLKIO_WEIGHT_DEFAULT;

#ifdef CONFIG_CFQ_GROUP_IOSCHED
	/*
	 * Take a reference to root group which we never drop. This is just
	 * to make sure that cfq_put_cfqg() does not try to kfree root group
	 */
	atomic_set(&cfqg->ref, 1);
	rcu_read_lock();
	cfq_blkiocg_add_blkio_group(&blkio_root_cgroup, &cfqg->blkg,
					(void *)cfqd, 0);
	rcu_read_unlock();
#endif
	/*
	 * Not strictly needed (since RB_ROOT just clears the node and we
	 * zeroed cfqd on alloc), but better be safe in case someone decides
	 * to add magic to the rb code
	 */
	for (i = 0; i < CFQ_PRIO_LISTS; i++)
		cfqd->prio_trees[i] = RB_ROOT;

	/*
	 * Our fallback cfqq if cfq_find_alloc_queue() runs into OOM issues.
	 * Grab a permanent reference to it, so that the normal code flow
	 * will not attempt to free it.
	 */
	cfq_init_cfqq(cfqd, &cfqd->oom_cfqq, 1, 0);
	atomic_inc(&cfqd->oom_cfqq.ref);
	cfq_link_cfqq_cfqg(&cfqd->oom_cfqq, &cfqd->root_group);

	INIT_LIST_HEAD(&cfqd->cic_list);

	cfqd->queue = q;

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
	cfqd->cfq_slice_async_rq = cfq_slice_async_rq;
	cfqd->cfq_slice_idle = cfq_slice_idle;
	cfqd->cfq_latency = 1;
	cfqd->cfq_group_isolation = 0;
	cfqd->hw_tag = -1;
	/*
	 * we optimistically start assuming sync ops weren't delayed in last
	 * second, in order to have larger depth for async operations.
	 */
	cfqd->last_delayed_sync = jiffies - HZ;
	return cfqd;
}

static void cfq_slab_kill(void)
{
	/*
	 * Caller already ensured that pending RCU callbacks are completed,
	 * so we should have no busy allocations at this point.
	 */
	if (cfq_pool)
		kmem_cache_destroy(cfq_pool);
	if (cfq_ioc_pool)
		kmem_cache_destroy(cfq_ioc_pool);
}

static int __init cfq_slab_setup(void)
{
	cfq_pool = KMEM_CACHE(cfq_queue, 0);
	if (!cfq_pool)
		goto fail;

	cfq_ioc_pool = KMEM_CACHE(cfq_io_context, 0);
	if (!cfq_ioc_pool)
		goto fail;

	return 0;
fail:
	cfq_slab_kill();
	return -ENOMEM;
}

/*
 * sysfs parts below -->
 */
static ssize_t
cfq_var_show(unsigned int var, char *page)
{
	return sprintf(page, "%d\n", var);
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
SHOW_FUNCTION(cfq_slice_sync_show, cfqd->cfq_slice[1], 1);
SHOW_FUNCTION(cfq_slice_async_show, cfqd->cfq_slice[0], 1);
SHOW_FUNCTION(cfq_slice_async_rq_show, cfqd->cfq_slice_async_rq, 0);
SHOW_FUNCTION(cfq_low_latency_show, cfqd->cfq_latency, 0);
SHOW_FUNCTION(cfq_group_isolation_show, cfqd->cfq_group_isolation, 0);
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
STORE_FUNCTION(cfq_slice_sync_store, &cfqd->cfq_slice[1], 1, UINT_MAX, 1);
STORE_FUNCTION(cfq_slice_async_store, &cfqd->cfq_slice[0], 1, UINT_MAX, 1);
STORE_FUNCTION(cfq_slice_async_rq_store, &cfqd->cfq_slice_async_rq, 1,
		UINT_MAX, 0);
STORE_FUNCTION(cfq_low_latency_store, &cfqd->cfq_latency, 0, 1, 0);
STORE_FUNCTION(cfq_group_isolation_store, &cfqd->cfq_group_isolation, 0, 1, 0);
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
	CFQ_ATTR(low_latency),
	CFQ_ATTR(group_isolation),
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
		.elevator_queue_empty_fn =	cfq_queue_empty,
		.elevator_completed_req_fn =	cfq_completed_request,
		.elevator_former_req_fn =	elv_rb_former_request,
		.elevator_latter_req_fn =	elv_rb_latter_request,
		.elevator_set_req_fn =		cfq_set_request,
		.elevator_put_req_fn =		cfq_put_request,
		.elevator_may_queue_fn =	cfq_may_queue,
		.elevator_init_fn =		cfq_init_queue,
		.elevator_exit_fn =		cfq_exit_queue,
		.trim =				cfq_free_io_context,
	},
	.elevator_attrs =	cfq_attrs,
	.elevator_name =	"cfq",
	.elevator_owner =	THIS_MODULE,
};

#ifdef CONFIG_CFQ_GROUP_IOSCHED
static struct blkio_policy_type blkio_policy_cfq = {
	.ops = {
		.blkio_unlink_group_fn =	cfq_unlink_blkio_group,
		.blkio_update_group_weight_fn =	cfq_update_blkio_group_weight,
	},
	.plid = BLKIO_POLICY_PROP,
};
#else
static struct blkio_policy_type blkio_policy_cfq;
#endif

static int __init cfq_init(void)
{
	/*
	 * could be 0 on HZ < 1000 setups
	 */
	if (!cfq_slice_async)
		cfq_slice_async = 1;
	if (!cfq_slice_idle)
		cfq_slice_idle = 1;

	if (cfq_slab_setup())
		return -ENOMEM;

	elv_register(&iosched_cfq);
	blkio_policy_register(&blkio_policy_cfq);

	return 0;
}

static void __exit cfq_exit(void)
{
	DECLARE_COMPLETION_ONSTACK(all_gone);
	blkio_policy_unregister(&blkio_policy_cfq);
	elv_unregister(&iosched_cfq);
	ioc_gone = &all_gone;
	/* ioc_gone's update must be visible before reading ioc_count */
	smp_wmb();

	/*
	 * this also protects us from entering cfq_slab_kill() with
	 * pending RCU callbacks
	 */
	if (elv_ioc_count_read(cfq_ioc_count))
		wait_for_completion(&all_gone);
	ida_destroy(&cic_index_ida);
	cfq_slab_kill();
}

module_init(cfq_init);
module_exit(cfq_exit);

MODULE_AUTHOR("Jens Axboe");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Completely Fair Queueing IO scheduler");
