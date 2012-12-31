/*
 * BFQ-v4 for 3.0: data structures and common functions prototypes.
 *
 * Based on ideas and code from CFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 */

#ifndef _BFQ_H
#define _BFQ_H

#include <linux/blktrace_api.h>
#include <linux/hrtimer.h>
#include <linux/ioprio.h>
#include <linux/rbtree.h>

#define BFQ_IOPRIO_CLASSES	3
#define BFQ_CL_IDLE_TIMEOUT	HZ/5

#define BFQ_MIN_WEIGHT	1
#define BFQ_MAX_WEIGHT	1000

#define BFQ_DEFAULT_GRP_WEIGHT	10
#define BFQ_DEFAULT_GRP_IOPRIO	0
#define BFQ_DEFAULT_GRP_CLASS	IOPRIO_CLASS_BE

struct bfq_entity;

/**
 * struct bfq_service_tree - per ioprio_class service tree.
 * @active: tree for active entities (i.e., those backlogged).
 * @idle: tree for idle entities (i.e., those not backlogged, with V <= F_i).
 * @first_idle: idle entity with minimum F_i.
 * @last_idle: idle entity with maximum F_i.
 * @vtime: scheduler virtual time.
 * @wsum: scheduler weight sum; active and idle entities contribute to it.
 *
 * Each service tree represents a B-WF2Q+ scheduler on its own.  Each
 * ioprio_class has its own independent scheduler, and so its own
 * bfq_service_tree.  All the fields are protected by the queue lock
 * of the containing bfqd.
 */
struct bfq_service_tree {
	struct rb_root active;
	struct rb_root idle;

	struct bfq_entity *first_idle;
	struct bfq_entity *last_idle;

	u64 vtime;
	unsigned long wsum;
};

/**
 * struct bfq_sched_data - multi-class scheduler.
 * @active_entity: entity under service.
 * @next_active: head-of-the-line entity in the scheduler.
 * @service_tree: array of service trees, one per ioprio_class.
 *
 * bfq_sched_data is the basic scheduler queue.  It supports three
 * ioprio_classes, and can be used either as a toplevel queue or as
 * an intermediate queue on a hierarchical setup.
 * @next_active points to the active entity of the sched_data service
 * trees that will be scheduled next.
 *
 * The supported ioprio_classes are the same as in CFQ, in descending
 * priority order, IOPRIO_CLASS_RT, IOPRIO_CLASS_BE, IOPRIO_CLASS_IDLE.
 * Requests from higher priority queues are served before all the
 * requests from lower priority queues; among requests of the same
 * queue requests are served according to B-WF2Q+.
 * All the fields are protected by the queue lock of the containing bfqd.
 */
struct bfq_sched_data {
	struct bfq_entity *active_entity;
	struct bfq_entity *next_active;
	struct bfq_service_tree service_tree[BFQ_IOPRIO_CLASSES];
};

/**
 * struct bfq_entity - schedulable entity.
 * @rb_node: service_tree member.
 * @on_st: flag, true if the entity is on a tree (either the active or
 *         the idle one of its service_tree).
 * @finish: B-WF2Q+ finish timestamp (aka F_i).
 * @start: B-WF2Q+ start timestamp (aka S_i).
 * @tree: tree the entity is enqueued into; %NULL if not on a tree.
 * @min_start: minimum start time of the (active) subtree rooted at
 *             this entity; used for O(log N) lookups into active trees.
 * @service: service received during the last round of service.
 * @budget: budget used to calculate F_i; F_i = S_i + @budget / @weight.
 * @weight: weight of the queue
 * @parent: parent entity, for hierarchical scheduling.
 * @my_sched_data: for non-leaf nodes in the cgroup hierarchy, the
 *                 associated scheduler queue, %NULL on leaf nodes.
 * @sched_data: the scheduler queue this entity belongs to.
 * @ioprio: the ioprio in use.
 * @new_weight: when a weight change is requested, the new weight value.
 * @orig_weight: original weight, used to implement weight boosting
 * @new_ioprio: when an ioprio change is requested, the new ioprio value.
 * @ioprio_class: the ioprio_class in use.
 * @new_ioprio_class: when an ioprio_class change is requested, the new
 *                    ioprio_class value.
 * @ioprio_changed: flag, true when the user requested a weight, ioprio or
 *                  ioprio_class change.
 *
 * A bfq_entity is used to represent either a bfq_queue (leaf node in the
 * cgroup hierarchy) or a bfq_group into the upper level scheduler.  Each
 * entity belongs to the sched_data of the parent group in the cgroup
 * hierarchy.  Non-leaf entities have also their own sched_data, stored
 * in @my_sched_data.
 *
 * Each entity stores independently its priority values; this would
 * allow different weights on different devices, but this
 * functionality is not exported to userspace by now.  Priorities and
 * weights are updated lazily, first storing the new values into the
 * new_* fields, then setting the @ioprio_changed flag.  As soon as
 * there is a transition in the entity state that allows the priority
 * update to take place the effective and the requested priority
 * values are synchronized.
 *
 * Unless cgroups are used, the weight value is calculated from the
 * ioprio to export the same interface as CFQ.  When dealing with
 * ``well-behaved'' queues (i.e., queues that do not spend too much
 * time to consume their budget and have true sequential behavior, and
 * when there are no external factors breaking anticipation) the
 * relative weights at each level of the cgroups hierarchy should be
 * guaranteed.  All the fields are protected by the queue lock of the
 * containing bfqd.
 */
struct bfq_entity {
	struct rb_node rb_node;

	int on_st;

	u64 finish;
	u64 start;

	struct rb_root *tree;

	u64 min_start;

	unsigned long service, budget;
	unsigned short weight, new_weight;
	unsigned short orig_weight;

	struct bfq_entity *parent;

	struct bfq_sched_data *my_sched_data;
	struct bfq_sched_data *sched_data;

	unsigned short ioprio, new_ioprio;
	unsigned short ioprio_class, new_ioprio_class;

	int ioprio_changed;
};

struct bfq_group;

/**
 * struct bfq_queue - leaf schedulable entity.
 * @ref: reference counter.
 * @bfqd: parent bfq_data.
 * @new_bfqq: shared bfq_queue if queue is cooperating with
 *           one or more other queues.
 * @pos_node: request-position tree member (see bfq_data's @rq_pos_tree).
 * @pos_root: request-position tree root (see bfq_data's @rq_pos_tree).
 * @sort_list: sorted list of pending requests.
 * @next_rq: if fifo isn't expired, next request to serve.
 * @queued: nr of requests queued in @sort_list.
 * @allocated: currently allocated requests.
 * @meta_pending: pending metadata requests.
 * @fifo: fifo list of requests in sort_list.
 * @entity: entity representing this queue in the scheduler.
 * @max_budget: maximum budget allowed from the feedback mechanism.
 * @budget_timeout: budget expiration (in jiffies).
 * @dispatched: number of requests on the dispatch list or inside driver.
 * @org_ioprio: saved ioprio during boosted periods.
 * @org_ioprio_class: saved ioprio_class during boosted periods.
 * @flags: status flags.
 * @bfqq_list: node for active/idle bfqq list inside our bfqd.
 * @seek_samples: number of seeks sampled
 * @seek_total: sum of the distances of the seeks sampled
 * @seek_mean: mean seek distance
 * @last_request_pos: position of the last request enqueued
 * @pid: pid of the process owning the queue, used for logging purposes.
 * @last_rais_start_time: last (idle -> weight-raised) transition attempt
 * @raising_cur_max_time: current max raising time for this queue
 *
 * A bfq_queue is a leaf request queue; it can be associated to an io_context
 * or more (if it is an async one).  @cgroup holds a reference to the
 * cgroup, to be sure that it does not disappear while a bfqq still
 * references it (mostly to avoid races between request issuing and task
 * migration followed by cgroup distruction).
 * All the fields are protected by the queue lock of the containing bfqd.
 */
struct bfq_queue {
	atomic_t ref;
	struct bfq_data *bfqd;

	/* fields for cooperating queues handling */
	struct bfq_queue *new_bfqq;
	struct rb_node pos_node;
	struct rb_root *pos_root;

	struct rb_root sort_list;
	struct request *next_rq;
	int queued[2];
	int allocated[2];
	int meta_pending;
	struct list_head fifo;

	struct bfq_entity entity;

	unsigned long max_budget;
	unsigned long budget_timeout;

	int dispatched;

	unsigned short org_ioprio;
	unsigned short org_ioprio_class;

	unsigned int flags;

	struct list_head bfqq_list;

	unsigned int seek_samples;
	u64 seek_total;
	sector_t seek_mean;
	sector_t last_request_pos;

	pid_t pid;

	/* weight-raising fields */
	unsigned int raising_cur_max_time;
	u64 last_rais_start_finish, soft_rt_next_start;
	unsigned int raising_coeff;
};

/**
 * struct bfq_data - per device data structure.
 * @queue: request queue for the managed device.
 * @root_group: root bfq_group for the device.
 * @rq_pos_tree: rbtree sorted by next_request position,
 *		used when determining if two or more queues
 *		have interleaving requests (see bfq_close_cooperator).
 * @busy_queues: number of bfq_queues containing requests (including the
 *		 queue under service, even if it is idling).
 * @queued: number of queued requests.
 * @rq_in_driver: number of requests dispatched and waiting for completion.
 * @sync_flight: number of sync requests in the driver.
 * @max_rq_in_driver: max number of reqs in driver in the last @hw_tag_samples
 *		      completed requests .
 * @hw_tag_samples: nr of samples used to calculate hw_tag.
 * @hw_tag: flag set to one if the driver is showing a queueing behavior.
 * @budgets_assigned: number of budgets assigned.
 * @idle_slice_timer: timer set when idling for the next sequential request
 *                    from the queue under service.
 * @unplug_work: delayed work to restart dispatching on the request queue.
 * @active_queue: bfq_queue under service.
 * @active_cic: cfq_io_context (cic) associated with the @active_queue.
 * @last_position: on-disk position of the last served request.
 * @last_budget_start: beginning of the last budget.
 * @last_idling_start: beginning of the last idle slice.
 * @peak_rate: peak transfer rate observed for a budget.
 * @peak_rate_samples: number of samples used to calculate @peak_rate.
 * @bfq_max_budget: maximum budget allotted to a bfq_queue before rescheduling.
 * @cic_index: use small consequent indexes as radix tree keys to reduce depth
 * @cic_list: list of all the cics active on the bfq_data device.
 * @group_list: list of all the bfq_groups active on the device.
 * @active_list: list of all the bfq_queues active on the device.
 * @idle_list: list of all the bfq_queues idle on the device.
 * @bfq_quantum: max number of requests dispatched per dispatch round.
 * @bfq_fifo_expire: timeout for async/sync requests; when it expires
 *                   requests are served in fifo order.
 * @bfq_back_penalty: weight of backward seeks wrt forward ones.
 * @bfq_back_max: maximum allowed backward seek.
 * @bfq_slice_idle: maximum idling time.
 * @bfq_user_max_budget: user-configured max budget value (0 for auto-tuning).
 * @bfq_max_budget_async_rq: maximum budget (in nr of requests) allotted to
 *                           async queues.
 * @bfq_timeout: timeout for bfq_queues to consume their budget; used to
 *               to prevent seeky queues to impose long latencies to well
 *               behaved ones (this also implies that seeky queues cannot
 *               receive guarantees in the service domain; after a timeout
 *               they are charged for the whole allocated budget, to try
 *               to preserve a behavior reasonably fair among them, but
 *               without service-domain guarantees).
 * @bfq_raising_coeff: Maximum factor by which the weight of a boosted
 *                            queue is multiplied
 * @bfq_raising_max_time: maximum duration of a weight-raising period (jiffies)
 * @bfq_raising_rt_max_time: maximum duration for soft real-time processes
 * @bfq_raising_min_idle_time: minimum idle period after which weight-raising
 *			       may be reactivated for a queue (in jiffies)
 * @bfq_raising_min_inter_arr_async: minimum period between request arrivals
 *                                   after which weight-raising may be
 *                                   reactivated for an already busy queue
 *                                   (in jiffies)
 * @bfq_raising_max_softrt_rate: max service-rate for a soft real-time queue,
 *			         sectors per seconds
 * @oom_bfqq: fallback dummy bfqq for extreme OOM conditions
 *
 * All the fields are protected by the @queue lock.
 */
struct bfq_data {
	struct request_queue *queue;

	struct bfq_group *root_group;

	struct rb_root rq_pos_tree;

	int busy_queues;
	int queued;
	int rq_in_driver;
	int sync_flight;

	int max_rq_in_driver;
	int hw_tag_samples;
	int hw_tag;

	int budgets_assigned;

	struct timer_list idle_slice_timer;
	struct work_struct unplug_work;

	struct bfq_queue *active_queue;
	struct cfq_io_context *active_cic;

	sector_t last_position;

	ktime_t last_budget_start;
	ktime_t last_idling_start;
	int peak_rate_samples;
	u64 peak_rate;
	unsigned long bfq_max_budget;

	unsigned int cic_index;
	struct list_head cic_list;
	struct hlist_head group_list;
	struct list_head active_list;
	struct list_head idle_list;

	unsigned int bfq_quantum;
	unsigned int bfq_fifo_expire[2];
	unsigned int bfq_back_penalty;
	unsigned int bfq_back_max;
	unsigned int bfq_slice_idle;
	u64 bfq_class_idle_last_service;

	unsigned int bfq_user_max_budget;
	unsigned int bfq_max_budget_async_rq;
	unsigned int bfq_timeout[2];

	bool low_latency;

	/* parameters of the low_latency heuristics */
	unsigned int bfq_raising_coeff;
	unsigned int bfq_raising_max_time;
	unsigned int bfq_raising_rt_max_time;
	unsigned int bfq_raising_min_idle_time;
	unsigned int bfq_raising_min_inter_arr_async;
	unsigned int bfq_raising_max_softrt_rate;

	struct bfq_queue oom_bfqq;
};

enum bfqq_state_flags {
	BFQ_BFQQ_FLAG_busy = 0,		/* has requests or is under service */
	BFQ_BFQQ_FLAG_wait_request,	/* waiting for a request */
	BFQ_BFQQ_FLAG_must_alloc,	/* must be allowed rq alloc */
	BFQ_BFQQ_FLAG_fifo_expire,	/* FIFO checked in this slice */
	BFQ_BFQQ_FLAG_idle_window,	/* slice idling enabled */
	BFQ_BFQQ_FLAG_prio_changed,	/* task priority has changed */
	BFQ_BFQQ_FLAG_sync,		/* synchronous queue */
	BFQ_BFQQ_FLAG_budget_new,	/* no completion with this budget */
	BFQ_BFQQ_FLAG_coop,		/* bfqq is shared */
	BFQ_BFQQ_FLAG_split_coop,	/* shared bfqq will be splitted */
	BFQ_BFQQ_FLAG_some_coop_idle,   /* some cooperator is inactive */
};

#define BFQ_BFQQ_FNS(name)						\
static inline void bfq_mark_bfqq_##name(struct bfq_queue *bfqq)		\
{									\
	(bfqq)->flags |= (1 << BFQ_BFQQ_FLAG_##name);			\
}									\
static inline void bfq_clear_bfqq_##name(struct bfq_queue *bfqq)	\
{									\
	(bfqq)->flags &= ~(1 << BFQ_BFQQ_FLAG_##name);			\
}									\
static inline int bfq_bfqq_##name(const struct bfq_queue *bfqq)		\
{									\
	return ((bfqq)->flags & (1 << BFQ_BFQQ_FLAG_##name)) != 0;	\
}

BFQ_BFQQ_FNS(busy);
BFQ_BFQQ_FNS(wait_request);
BFQ_BFQQ_FNS(must_alloc);
BFQ_BFQQ_FNS(fifo_expire);
BFQ_BFQQ_FNS(idle_window);
BFQ_BFQQ_FNS(prio_changed);
BFQ_BFQQ_FNS(sync);
BFQ_BFQQ_FNS(budget_new);
BFQ_BFQQ_FNS(coop);
BFQ_BFQQ_FNS(split_coop);
BFQ_BFQQ_FNS(some_coop_idle);
#undef BFQ_BFQQ_FNS

/* Logging facilities. */
#define bfq_log_bfqq(bfqd, bfqq, fmt, args...) \
	blk_add_trace_msg((bfqd)->queue, "bfq%d " fmt, (bfqq)->pid, ##args)

#define bfq_log(bfqd, fmt, args...) \
	blk_add_trace_msg((bfqd)->queue, "bfq " fmt, ##args)

/* Expiration reasons. */
enum bfqq_expiration {
	BFQ_BFQQ_TOO_IDLE = 0,		/* queue has been idling for too long */
	BFQ_BFQQ_BUDGET_TIMEOUT,	/* budget took too long to be used */
	BFQ_BFQQ_BUDGET_EXHAUSTED,	/* budget consumed */
	BFQ_BFQQ_NO_MORE_REQUESTS,	/* the queue has no more requests */
};

#ifdef CONFIG_CGROUP_BFQIO
/**
 * struct bfq_group - per (device, cgroup) data structure.
 * @entity: schedulable entity to insert into the parent group sched_data.
 * @sched_data: own sched_data, to contain child entities (they may be
 *              both bfq_queues and bfq_groups).
 * @group_node: node to be inserted into the bfqio_cgroup->group_data
 *              list of the containing cgroup's bfqio_cgroup.
 * @bfqd_node: node to be inserted into the @bfqd->group_list list
 *             of the groups active on the same device; used for cleanup.
 * @bfqd: the bfq_data for the device this group acts upon.
 * @async_bfqq: array of async queues for all the tasks belonging to
 *              the group, one queue per ioprio value per ioprio_class,
 *              except for the idle class that has only one queue.
 * @async_idle_bfqq: async queue for the idle class (ioprio is ignored).
 * @my_entity: pointer to @entity, %NULL for the toplevel group; used
 *             to avoid too many special cases during group creation/migration.
 *
 * Each (device, cgroup) pair has its own bfq_group, i.e., for each cgroup
 * there is a set of bfq_groups, each one collecting the lower-level
 * entities belonging to the group that are acting on the same device.
 *
 * Locking works as follows:
 *    o @group_node is protected by the bfqio_cgroup lock, and is accessed
 *      via RCU from its readers.
 *    o @bfqd is protected by the queue lock, RCU is used to access it
 *      from the readers.
 *    o All the other fields are protected by the @bfqd queue lock.
 */
struct bfq_group {
	struct bfq_entity entity;
	struct bfq_sched_data sched_data;

	struct hlist_node group_node;
	struct hlist_node bfqd_node;

	void *bfqd;

	struct bfq_queue *async_bfqq[2][IOPRIO_BE_NR];
	struct bfq_queue *async_idle_bfqq;

	struct bfq_entity *my_entity;
};

/**
 * struct bfqio_cgroup - bfq cgroup data structure.
 * @css: subsystem state for bfq in the containing cgroup.
 * @weight: cgroup weight.
 * @ioprio: cgroup ioprio.
 * @ioprio_class: cgroup ioprio_class.
 * @lock: spinlock that protects @ioprio, @ioprio_class and @group_data.
 * @group_data: list containing the bfq_group belonging to this cgroup.
 *
 * @group_data is accessed using RCU, with @lock protecting the updates,
 * @ioprio and @ioprio_class are protected by @lock.
 */
struct bfqio_cgroup {
	struct cgroup_subsys_state css;

	unsigned short weight, ioprio, ioprio_class;

	spinlock_t lock;
	struct hlist_head group_data;
};
#else
struct bfq_group {
	struct bfq_sched_data sched_data;

	struct bfq_queue *async_bfqq[2][IOPRIO_BE_NR];
	struct bfq_queue *async_idle_bfqq;
};
#endif

static inline struct bfq_service_tree *
bfq_entity_service_tree(struct bfq_entity *entity)
{
	struct bfq_sched_data *sched_data = entity->sched_data;
	unsigned int idx = entity->ioprio_class - 1;

	BUG_ON(idx >= BFQ_IOPRIO_CLASSES);
	BUG_ON(sched_data == NULL);

	return sched_data->service_tree + idx;
}

static inline struct bfq_queue *cic_to_bfqq(struct cfq_io_context *cic,
					    int is_sync)
{
	return cic->cfqq[!!is_sync];
}

static inline void cic_set_bfqq(struct cfq_io_context *cic,
				struct bfq_queue *bfqq, int is_sync)
{
	cic->cfqq[!!is_sync] = bfqq;
}

static inline void call_for_each_cic(struct io_context *ioc,
				     void (*func)(struct io_context *,
				     struct cfq_io_context *))
{
	struct cfq_io_context *cic;
	struct hlist_node *n;

	rcu_read_lock();
	hlist_for_each_entry_rcu(cic, n, &ioc->bfq_cic_list, cic_list)
		func(ioc, cic);
	rcu_read_unlock();
}

#define CIC_DEAD_KEY    1ul
#define CIC_DEAD_INDEX_SHIFT    1

static inline void *bfqd_dead_key(struct bfq_data *bfqd)
{
	return (void *)(bfqd->cic_index << CIC_DEAD_INDEX_SHIFT | CIC_DEAD_KEY);
}

/**
 * bfq_get_bfqd_locked - get a lock to a bfqd using a RCU protected pointer.
 * @ptr: a pointer to a bfqd.
 * @flags: storage for the flags to be saved.
 *
 * This function allows cic->key and bfqg->bfqd to be protected by the
 * queue lock of the bfqd they reference; the pointer is dereferenced
 * under RCU, so the storage for bfqd is assured to be safe as long
 * as the RCU read side critical section does not end.  After the
 * bfqd->queue->queue_lock is taken the pointer is rechecked, to be
 * sure that no other writer accessed it.  If we raced with a writer,
 * the function returns NULL, with the queue unlocked, otherwise it
 * returns the dereferenced pointer, with the queue locked.
 */
static inline struct bfq_data *bfq_get_bfqd_locked(void **ptr,
						   unsigned long *flags)
{
	struct bfq_data *bfqd;

	rcu_read_lock();
	bfqd = rcu_dereference(*(struct bfq_data **)ptr);

	if (bfqd != NULL && !((unsigned long) bfqd & CIC_DEAD_KEY)) {
		spin_lock_irqsave(bfqd->queue->queue_lock, *flags);
		if (*ptr == bfqd)
			goto out;
		spin_unlock_irqrestore(bfqd->queue->queue_lock, *flags);
	}

	bfqd = NULL;
out:
	rcu_read_unlock();
	return bfqd;
}

static inline void bfq_put_bfqd_unlock(struct bfq_data *bfqd,
				       unsigned long *flags)
{
	spin_unlock_irqrestore(bfqd->queue->queue_lock, *flags);
}

static void bfq_changed_ioprio(struct io_context *ioc,
			       struct cfq_io_context *cic);
static void bfq_put_queue(struct bfq_queue *bfqq);
static void bfq_dispatch_insert(struct request_queue *q, struct request *rq);
static struct bfq_queue *bfq_get_queue(struct bfq_data *bfqd,
				       struct bfq_group *bfqg, int is_sync,
				       struct io_context *ioc, gfp_t gfp_mask);
static void bfq_put_async_queues(struct bfq_data *bfqd, struct bfq_group *bfqg);
static void bfq_exit_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq);
#endif
