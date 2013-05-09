/*
 * BFQ-v7r11 for 4.4.0: data structures and common functions prototypes.
 *
 * Based on ideas and code from CFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2010 Paolo Valente <paolo.valente@unimore.it>
 */

#ifndef _BFQ_H
#define _BFQ_H

#include <linux/blktrace_api.h>
#include <linux/hrtimer.h>
#include <linux/ioprio.h>
#include <linux/rbtree.h>
#include <linux/blk-cgroup.h>

#define BFQ_IOPRIO_CLASSES	3
#define BFQ_CL_IDLE_TIMEOUT	(HZ/5)

#define BFQ_MIN_WEIGHT			1
#define BFQ_MAX_WEIGHT			1000
#define BFQ_WEIGHT_CONVERSION_COEFF	10

#define BFQ_DEFAULT_QUEUE_IOPRIO	4

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
 * @in_service_entity: entity in service.
 * @next_in_service: head-of-the-line entity in the scheduler.
 * @service_tree: array of service trees, one per ioprio_class.
 *
 * bfq_sched_data is the basic scheduler queue.  It supports three
 * ioprio_classes, and can be used either as a toplevel queue or as
 * an intermediate queue on a hierarchical setup.
 * @next_in_service points to the active entity of the sched_data
 * service trees that will be scheduled next.
 *
 * The supported ioprio_classes are the same as in CFQ, in descending
 * priority order, IOPRIO_CLASS_RT, IOPRIO_CLASS_BE, IOPRIO_CLASS_IDLE.
 * Requests from higher priority queues are served before all the
 * requests from lower priority queues; among requests of the same
 * queue requests are served according to B-WF2Q+.
 * All the fields are protected by the queue lock of the containing bfqd.
 */
struct bfq_sched_data {
	struct bfq_entity *in_service_entity;
	struct bfq_entity *next_in_service;
	struct bfq_service_tree service_tree[BFQ_IOPRIO_CLASSES];
};

/**
 * struct bfq_weight_counter - counter of the number of all active entities
 *                             with a given weight.
 * @weight: weight of the entities that this counter refers to.
 * @num_active: number of active entities with this weight.
 * @weights_node: weights tree member (see bfq_data's @queue_weights_tree
 *                and @group_weights_tree).
 */
struct bfq_weight_counter {
	short int weight;
	unsigned int num_active;
	struct rb_node weights_node;
};

/**
 * struct bfq_entity - schedulable entity.
 * @rb_node: service_tree member.
 * @weight_counter: pointer to the weight counter associated with this entity.
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
 * @prio_changed: flag, true when the user requested a weight, ioprio or
 *		  ioprio_class change.
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
 * new_* fields, then setting the @prio_changed flag.  As soon as
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
	struct bfq_weight_counter *weight_counter;

	int on_st;

	u64 finish;
	u64 start;

	struct rb_root *tree;

	u64 min_start;

	int service, budget;
	unsigned short weight, new_weight;
	unsigned short orig_weight;

	struct bfq_entity *parent;

	struct bfq_sched_data *my_sched_data;
	struct bfq_sched_data *sched_data;

	int prio_changed;
};

struct bfq_group;

/**
 * struct bfq_queue - leaf schedulable entity.
 * @ref: reference counter.
 * @bfqd: parent bfq_data.
 * @new_ioprio: when an ioprio change is requested, the new ioprio value.
 * @ioprio_class: the ioprio_class in use.
 * @new_ioprio_class: when an ioprio_class change is requested, the new
 *                    ioprio_class value.
 * @new_bfqq: shared bfq_queue if queue is cooperating with
 *           one or more other queues.
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
 * @flags: status flags.
 * @bfqq_list: node for active/idle bfqq list inside our bfqd.
 * @burst_list_node: node for the device's burst list.
 * @seek_samples: number of seeks sampled
 * @seek_total: sum of the distances of the seeks sampled
 * @seek_mean: mean seek distance
 * @last_request_pos: position of the last request enqueued
 * @requests_within_timer: number of consecutive pairs of request completion
 *                         and arrival, such that the queue becomes idle
 *                         after the completion, but the next request arrives
 *                         within an idle time slice; used only if the queue's
 *                         IO_bound has been cleared.
 * @pid: pid of the process owning the queue, used for logging purposes.
 * @last_wr_start_finish: start time of the current weight-raising period if
 *                        the @bfq-queue is being weight-raised, otherwise
 *                        finish time of the last weight-raising period
 * @wr_cur_max_time: current max raising time for this queue
 * @soft_rt_next_start: minimum time instant such that, only if a new
 *                      request is enqueued after this time instant in an
 *                      idle @bfq_queue with no outstanding requests, then
 *                      the task associated with the queue it is deemed as
 *                      soft real-time (see the comments to the function
 *                      bfq_bfqq_softrt_next_start())
 * @last_idle_bklogged: time of the last transition of the @bfq_queue from
 *                      idle to backlogged
 * @service_from_backlogged: cumulative service received from the @bfq_queue
 *                           since the last transition from idle to
 *                           backlogged
 * @bic: pointer to the bfq_io_cq owning the bfq_queue, set to %NULL if the
 *	 queue is shared
 *
 * A bfq_queue is a leaf request queue; it can be associated with an
 * io_context or more, if it  is  async or shared  between  cooperating
 * processes. @cgroup holds a reference to the cgroup, to be sure that it
 * does not disappear while a bfqq still references it (mostly to avoid
 * races between request issuing and task migration followed by cgroup
 * destruction).
 * All the fields are protected by the queue lock of the containing bfqd.
 */
struct bfq_queue {
	atomic_t ref;
	struct bfq_data *bfqd;

	unsigned short ioprio, new_ioprio;
	unsigned short ioprio_class, new_ioprio_class;

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

	int max_budget;
	unsigned long budget_timeout;

	int dispatched;

	unsigned int flags;

	struct list_head bfqq_list;

	struct hlist_node burst_list_node;

	unsigned int seek_samples;
	u64 seek_total;
	sector_t seek_mean;
	sector_t last_request_pos;

	unsigned int requests_within_timer;

	pid_t pid;
	struct bfq_io_cq *bic;

	/* weight-raising fields */
	unsigned long wr_cur_max_time;
	unsigned long soft_rt_next_start;
	unsigned long last_wr_start_finish;
	unsigned int wr_coeff;
	unsigned long last_idle_bklogged;
	unsigned long service_from_backlogged;
};

/**
 * struct bfq_ttime - per process thinktime stats.
 * @ttime_total: total process thinktime
 * @ttime_samples: number of thinktime samples
 * @ttime_mean: average process thinktime
 */
struct bfq_ttime {
	unsigned long last_end_request;

	unsigned long ttime_total;
	unsigned long ttime_samples;
	unsigned long ttime_mean;
};

/**
 * struct bfq_io_cq - per (request_queue, io_context) structure.
 * @icq: associated io_cq structure
 * @bfqq: array of two process queues, the sync and the async
 * @ttime: associated @bfq_ttime struct
 * @ioprio: per (request_queue, blkcg) ioprio.
 * @blkcg_id: id of the blkcg the related io_cq belongs to.
 */
struct bfq_io_cq {
	struct io_cq icq; /* must be the first member */
	struct bfq_queue *bfqq[2];
	struct bfq_ttime ttime;
	int ioprio;

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	uint64_t blkcg_id; /* the current blkcg ID */
#endif
};

enum bfq_device_speed {
	BFQ_BFQD_FAST,
	BFQ_BFQD_SLOW,
};

/**
 * struct bfq_data - per device data structure.
 * @queue: request queue for the managed device.
 * @root_group: root bfq_group for the device.
 * @active_numerous_groups: number of bfq_groups containing more than one
 *                          active @bfq_entity.
 * @queue_weights_tree: rbtree of weight counters of @bfq_queues, sorted by
 *                      weight. Used to keep track of whether all @bfq_queues
 *                     have the same weight. The tree contains one counter
 *                     for each distinct weight associated to some active
 *                     and not weight-raised @bfq_queue (see the comments to
 *                      the functions bfq_weights_tree_[add|remove] for
 *                     further details).
 * @group_weights_tree: rbtree of non-queue @bfq_entity weight counters, sorted
 *                      by weight. Used to keep track of whether all
 *                     @bfq_groups have the same weight. The tree contains
 *                     one counter for each distinct weight associated to
 *                     some active @bfq_group (see the comments to the
 *                     functions bfq_weights_tree_[add|remove] for further
 *                     details).
 * @busy_queues: number of bfq_queues containing requests (including the
 *		 queue in service, even if it is idling).
 * @busy_in_flight_queues: number of @bfq_queues containing pending or
 *                         in-flight requests, plus the @bfq_queue in
 *                         service, even if idle but waiting for the
 *                         possible arrival of its next sync request. This
 *                         field is updated only if the device is rotational,
 *                         but used only if the device is also NCQ-capable.
 *                         The reason why the field is updated also for non-
 *                         NCQ-capable rotational devices is related to the
 *                         fact that the value of @hw_tag may be set also
 *                         later than when busy_in_flight_queues may need to
 *                         be incremented for the first time(s). Taking also
 *                         this possibility into account, to avoid unbalanced
 *                         increments/decrements, would imply more overhead
 *                         than just updating busy_in_flight_queues
 *                         regardless of the value of @hw_tag.
 * @const_seeky_busy_in_flight_queues: number of constantly-seeky @bfq_queues
 *                                     (that is, seeky queues that expired
 *                                     for budget timeout at least once)
 *                                     containing pending or in-flight
 *                                     requests, including the in-service
 *                                     @bfq_queue if constantly seeky. This
 *                                     field is updated only if the device
 *                                     is rotational, but used only if the
 *                                     device is also NCQ-capable (see the
 *                                     comments to @busy_in_flight_queues).
 * @wr_busy_queues: number of weight-raised busy @bfq_queues.
 * @queued: number of queued requests.
 * @rq_in_driver: number of requests dispatched and waiting for completion.
 * @sync_flight: number of sync requests in the driver.
 * @max_rq_in_driver: max number of reqs in driver in the last
 *                    @hw_tag_samples completed requests.
 * @hw_tag_samples: nr of samples used to calculate hw_tag.
 * @hw_tag: flag set to one if the driver is showing a queueing behavior.
 * @budgets_assigned: number of budgets assigned.
 * @idle_slice_timer: timer set when idling for the next sequential request
 *                    from the queue in service.
 * @unplug_work: delayed work to restart dispatching on the request queue.
 * @in_service_queue: bfq_queue in service.
 * @in_service_bic: bfq_io_cq (bic) associated with the @in_service_queue.
 * @last_position: on-disk position of the last served request.
 * @last_budget_start: beginning of the last budget.
 * @last_idling_start: beginning of the last idle slice.
 * @peak_rate: peak transfer rate observed for a budget.
 * @peak_rate_samples: number of samples used to calculate @peak_rate.
 * @bfq_max_budget: maximum budget allotted to a bfq_queue before
 *                  rescheduling.
 * @active_list: list of all the bfq_queues active on the device.
 * @idle_list: list of all the bfq_queues idle on the device.
 * @bfq_fifo_expire: timeout for async/sync requests; when it expires
 *                   requests are served in fifo order.
 * @bfq_back_penalty: weight of backward seeks wrt forward ones.
 * @bfq_back_max: maximum allowed backward seek.
 * @bfq_slice_idle: maximum idling time.
 * @bfq_user_max_budget: user-configured max budget value
 *                       (0 for auto-tuning).
 * @bfq_max_budget_async_rq: maximum budget (in nr of requests) allotted to
 *                           async queues.
 * @bfq_timeout: timeout for bfq_queues to consume their budget; used to
 *               to prevent seeky queues to impose long latencies to well
 *               behaved ones (this also implies that seeky queues cannot
 *               receive guarantees in the service domain; after a timeout
 *               they are charged for the whole allocated budget, to try
 *               to preserve a behavior reasonably fair among them, but
 *               without service-domain guarantees).
 * @bfq_coop_thresh: number of queue merges after which a @bfq_queue is
 *                   no more granted any weight-raising.
 * @bfq_failed_cooperations: number of consecutive failed cooperation
 *                           chances after which weight-raising is restored
 *                           to a queue subject to more than bfq_coop_thresh
 *                           queue merges.
 * @bfq_requests_within_timer: number of consecutive requests that must be
 *                             issued within the idle time slice to set
 *                             again idling to a queue which was marked as
 *                             non-I/O-bound (see the definition of the
 *                             IO_bound flag for further details).
 * @last_ins_in_burst: last time at which a queue entered the current
 *                     burst of queues being activated shortly after
 *                     each other; for more details about this and the
 *                     following parameters related to a burst of
 *                     activations, see the comments to the function
 *                     @bfq_handle_burst.
 * @bfq_burst_interval: reference time interval used to decide whether a
 *                      queue has been activated shortly after
 *                      @last_ins_in_burst.
 * @burst_size: number of queues in the current burst of queue activations.
 * @bfq_large_burst_thresh: maximum burst size above which the current
 * 			    queue-activation burst is deemed as 'large'.
 * @large_burst: true if a large queue-activation burst is in progress.
 * @burst_list: head of the burst list (as for the above fields, more details
 * 		in the comments to the function bfq_handle_burst).
 * @low_latency: if set to true, low-latency heuristics are enabled.
 * @bfq_wr_coeff: maximum factor by which the weight of a weight-raised
 *                queue is multiplied.
 * @bfq_wr_max_time: maximum duration of a weight-raising period (jiffies).
 * @bfq_wr_rt_max_time: maximum duration for soft real-time processes.
 * @bfq_wr_min_idle_time: minimum idle period after which weight-raising
 *			  may be reactivated for a queue (in jiffies).
 * @bfq_wr_min_inter_arr_async: minimum period between request arrivals
 *				after which weight-raising may be
 *				reactivated for an already busy queue
 *				(in jiffies).
 * @bfq_wr_max_softrt_rate: max service-rate for a soft real-time queue,
 *			    sectors per seconds.
 * @RT_prod: cached value of the product R*T used for computing the maximum
 *	     duration of the weight raising automatically.
 * @device_speed: device-speed class for the low-latency heuristic.
 * @oom_bfqq: fallback dummy bfqq for extreme OOM conditions.
 *
 * All the fields are protected by the @queue lock.
 */
struct bfq_data {
	struct request_queue *queue;

	struct bfq_group *root_group;

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	int active_numerous_groups;
#endif

	struct rb_root queue_weights_tree;
	struct rb_root group_weights_tree;

	int busy_queues;
	int busy_in_flight_queues;
	int const_seeky_busy_in_flight_queues;
	int wr_busy_queues;
	int queued;
	int rq_in_driver;
	int sync_flight;

	int max_rq_in_driver;
	int hw_tag_samples;
	int hw_tag;

	int budgets_assigned;

	struct timer_list idle_slice_timer;
	struct work_struct unplug_work;

	struct bfq_queue *in_service_queue;
	struct bfq_io_cq *in_service_bic;

	sector_t last_position;

	ktime_t last_budget_start;
	ktime_t last_idling_start;
	int peak_rate_samples;
	u64 peak_rate;
	int bfq_max_budget;

	struct list_head active_list;
	struct list_head idle_list;

	unsigned int bfq_fifo_expire[2];
	unsigned int bfq_back_penalty;
	unsigned int bfq_back_max;
	unsigned int bfq_slice_idle;
	u64 bfq_class_idle_last_service;

	int bfq_user_max_budget;
	int bfq_max_budget_async_rq;
	unsigned int bfq_timeout[2];

	unsigned int bfq_coop_thresh;
	unsigned int bfq_failed_cooperations;
	unsigned int bfq_requests_within_timer;

	unsigned long last_ins_in_burst;
	unsigned long bfq_burst_interval;
	int burst_size;
	unsigned long bfq_large_burst_thresh;
	bool large_burst;
	struct hlist_head burst_list;

	bool low_latency;

	/* parameters of the low_latency heuristics */
	unsigned int bfq_wr_coeff;
	unsigned int bfq_wr_max_time;
	unsigned int bfq_wr_rt_max_time;
	unsigned int bfq_wr_min_idle_time;
	unsigned long bfq_wr_min_inter_arr_async;
	unsigned int bfq_wr_max_softrt_rate;
	u64 RT_prod;
	enum bfq_device_speed device_speed;

	struct bfq_queue oom_bfqq;
};

enum bfqq_state_flags {
	BFQ_BFQQ_FLAG_busy = 0,		/* has requests or is in service */
	BFQ_BFQQ_FLAG_wait_request,	/* waiting for a request */
	BFQ_BFQQ_FLAG_must_alloc,	/* must be allowed rq alloc */
	BFQ_BFQQ_FLAG_fifo_expire,	/* FIFO checked in this slice */
	BFQ_BFQQ_FLAG_idle_window,	/* slice idling enabled */
	BFQ_BFQQ_FLAG_sync,		/* synchronous queue */
	BFQ_BFQQ_FLAG_budget_new,	/* no completion with this budget */
	BFQ_BFQQ_FLAG_IO_bound,		/*
					 * bfqq has timed-out at least once
					 * having consumed at most 2/10 of
					 * its budget
					 */
	BFQ_BFQQ_FLAG_in_large_burst,	/*
					 * bfqq activated in a large burst,
					 * see comments to bfq_handle_burst.
					 */
	BFQ_BFQQ_FLAG_constantly_seeky,	/*
					 * bfqq has proved to be slow and
					 * seeky until budget timeout
					 */
	BFQ_BFQQ_FLAG_softrt_update,	/*
					 * may need softrt-next-start
					 * update
					 */
};

#define BFQ_BFQQ_FNS(name)						\
static void bfq_mark_bfqq_##name(struct bfq_queue *bfqq)		\
{									\
	(bfqq)->flags |= (1 << BFQ_BFQQ_FLAG_##name);			\
}									\
static void bfq_clear_bfqq_##name(struct bfq_queue *bfqq)		\
{									\
	(bfqq)->flags &= ~(1 << BFQ_BFQQ_FLAG_##name);			\
}									\
static int bfq_bfqq_##name(const struct bfq_queue *bfqq)		\
{									\
	return ((bfqq)->flags & (1 << BFQ_BFQQ_FLAG_##name)) != 0;	\
}

BFQ_BFQQ_FNS(busy);
BFQ_BFQQ_FNS(wait_request);
BFQ_BFQQ_FNS(must_alloc);
BFQ_BFQQ_FNS(fifo_expire);
BFQ_BFQQ_FNS(idle_window);
BFQ_BFQQ_FNS(sync);
BFQ_BFQQ_FNS(budget_new);
BFQ_BFQQ_FNS(IO_bound);
BFQ_BFQQ_FNS(in_large_burst);
BFQ_BFQQ_FNS(constantly_seeky);
BFQ_BFQQ_FNS(softrt_update);
#undef BFQ_BFQQ_FNS

/* Logging facilities. */
#define bfq_log_bfqq(bfqd, bfqq, fmt, args...) \
	blk_add_trace_msg((bfqd)->queue, "bfq%d " fmt, (bfqq)->pid, ##args)

#define bfq_log(bfqd, fmt, args...) \
	blk_add_trace_msg((bfqd)->queue, "bfq " fmt, ##args)

/* Expiration reasons. */
enum bfqq_expiration {
	BFQ_BFQQ_TOO_IDLE = 0,		/*
					 * queue has been idling for
					 * too long
					 */
	BFQ_BFQQ_BUDGET_TIMEOUT,	/* budget took too long to be used */
	BFQ_BFQQ_BUDGET_EXHAUSTED,	/* budget consumed */
	BFQ_BFQQ_NO_MORE_REQUESTS,	/* the queue has no more requests */
};

#ifdef CONFIG_BFQ_GROUP_IOSCHED

struct bfqg_stats {
	/* total bytes transferred */
	struct blkg_rwstat		service_bytes;
	/* total IOs serviced, post merge */
	struct blkg_rwstat		serviced;
	/* number of ios merged */
	struct blkg_rwstat		merged;
	/* total time spent on device in ns, may not be accurate w/ queueing */
	struct blkg_rwstat		service_time;
	/* total time spent waiting in scheduler queue in ns */
	struct blkg_rwstat		wait_time;
	/* number of IOs queued up */
	struct blkg_rwstat		queued;
	/* total sectors transferred */
	struct blkg_stat		sectors;
	/* total disk time and nr sectors dispatched by this group */
	struct blkg_stat		time;
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
};

/*
 * struct bfq_group_data - per-blkcg storage for the blkio subsystem.
 *
 * @ps: @blkcg_policy_storage that this structure inherits
 * @weight: weight of the bfq_group
 */
struct bfq_group_data {
	/* must be the first member */
	struct blkcg_policy_data pd;

	unsigned short weight;
};

/**
 * struct bfq_group - per (device, cgroup) data structure.
 * @entity: schedulable entity to insert into the parent group sched_data.
 * @sched_data: own sched_data, to contain child entities (they may be
 *              both bfq_queues and bfq_groups).
 * @bfqd: the bfq_data for the device this group acts upon.
 * @async_bfqq: array of async queues for all the tasks belonging to
 *              the group, one queue per ioprio value per ioprio_class,
 *              except for the idle class that has only one queue.
 * @async_idle_bfqq: async queue for the idle class (ioprio is ignored).
 * @my_entity: pointer to @entity, %NULL for the toplevel group; used
 *             to avoid too many special cases during group creation/
 *             migration.
 * @active_entities: number of active entities belonging to the group;
 *                   unused for the root group. Used to know whether there
 *                   are groups with more than one active @bfq_entity
 *                   (see the comments to the function
 *                   bfq_bfqq_must_not_expire()).
 *
 * Each (device, cgroup) pair has its own bfq_group, i.e., for each cgroup
 * there is a set of bfq_groups, each one collecting the lower-level
 * entities belonging to the group that are acting on the same device.
 *
 * Locking works as follows:
 *    o @bfqd is protected by the queue lock, RCU is used to access it
 *      from the readers.
 *    o All the other fields are protected by the @bfqd queue lock.
 */
struct bfq_group {
	/* must be the first member */
	struct blkg_policy_data pd;

	struct bfq_entity entity;
	struct bfq_sched_data sched_data;

	void *bfqd;

	struct bfq_queue *async_bfqq[2][IOPRIO_BE_NR];
	struct bfq_queue *async_idle_bfqq;

	struct bfq_entity *my_entity;

	int active_entities;

	struct bfqg_stats stats;
	struct bfqg_stats dead_stats;	/* stats pushed from dead children */
};

#else
struct bfq_group {
	struct bfq_sched_data sched_data;

	struct bfq_queue *async_bfqq[2][IOPRIO_BE_NR];
	struct bfq_queue *async_idle_bfqq;
};
#endif

static struct bfq_queue *bfq_entity_to_bfqq(struct bfq_entity *entity);

static struct bfq_service_tree *
bfq_entity_service_tree(struct bfq_entity *entity)
{
	struct bfq_sched_data *sched_data = entity->sched_data;
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);
	unsigned int idx = bfqq ? bfqq->ioprio_class - 1 :
				  BFQ_DEFAULT_GRP_CLASS;

	BUG_ON(idx >= BFQ_IOPRIO_CLASSES);
	BUG_ON(sched_data == NULL);

	return sched_data->service_tree + idx;
}

static struct bfq_queue *bic_to_bfqq(struct bfq_io_cq *bic, bool is_sync)
{
	return bic->bfqq[is_sync];
}

static void bic_set_bfqq(struct bfq_io_cq *bic, struct bfq_queue *bfqq,
			 bool is_sync)
{
	bic->bfqq[is_sync] = bfqq;
}

static struct bfq_data *bic_to_bfqd(struct bfq_io_cq *bic)
{
	return bic->icq.q->elevator->elevator_data;
}

/**
 * bfq_get_bfqd_locked - get a lock to a bfqd using a RCU protected pointer.
 * @ptr: a pointer to a bfqd.
 * @flags: storage for the flags to be saved.
 *
 * This function allows bfqg->bfqd to be protected by the
 * queue lock of the bfqd they reference; the pointer is dereferenced
 * under RCU, so the storage for bfqd is assured to be safe as long
 * as the RCU read side critical section does not end.  After the
 * bfqd->queue->queue_lock is taken the pointer is rechecked, to be
 * sure that no other writer accessed it.  If we raced with a writer,
 * the function returns NULL, with the queue unlocked, otherwise it
 * returns the dereferenced pointer, with the queue locked.
 */
static struct bfq_data *bfq_get_bfqd_locked(void **ptr, unsigned long *flags)
{
	struct bfq_data *bfqd;

	rcu_read_lock();
	bfqd = rcu_dereference(*(struct bfq_data **)ptr);

	if (bfqd != NULL) {
		spin_lock_irqsave(bfqd->queue->queue_lock, *flags);
		if (ptr == NULL)
			printk(KERN_CRIT "get_bfqd_locked pointer NULL\n");
		else if (*ptr == bfqd)
			goto out;
		spin_unlock_irqrestore(bfqd->queue->queue_lock, *flags);
	}

	bfqd = NULL;
out:
	rcu_read_unlock();
	return bfqd;
}

static void bfq_put_bfqd_unlock(struct bfq_data *bfqd, unsigned long *flags)
{
	spin_unlock_irqrestore(bfqd->queue->queue_lock, *flags);
}

static void bfq_check_ioprio_change(struct bfq_io_cq *bic, struct bio *bio);
static void bfq_put_queue(struct bfq_queue *bfqq);
static void bfq_dispatch_insert(struct request_queue *q, struct request *rq);
static struct bfq_queue *bfq_get_queue(struct bfq_data *bfqd,
				       struct bio *bio, int is_sync,
				       struct bfq_io_cq *bic, gfp_t gfp_mask);
static void bfq_end_wr_async_queues(struct bfq_data *bfqd,
				    struct bfq_group *bfqg);
static void bfq_put_async_queues(struct bfq_data *bfqd, struct bfq_group *bfqg);
static void bfq_exit_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq);

#endif /* _BFQ_H */
