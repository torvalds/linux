/*
 * Budget Fair Queueing (BFQ) I/O scheduler.
 *
 * Based on ideas and code from CFQ:
 * Copyright (C) 2003 Jens Axboe <axboe@kernel.dk>
 *
 * Copyright (C) 2008 Fabio Checconi <fabio@gandalf.sssup.it>
 *		      Paolo Valente <paolo.valente@unimore.it>
 *
 * Copyright (C) 2010 Paolo Valente <paolo.valente@unimore.it>
 *                    Arianna Avanzini <avanzini@google.com>
 *
 * Copyright (C) 2017 Paolo Valente <paolo.valente@linaro.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * BFQ is a proportional-share I/O scheduler, with some extra
 * low-latency capabilities. BFQ also supports full hierarchical
 * scheduling through cgroups. Next paragraphs provide an introduction
 * on BFQ inner workings. Details on BFQ benefits, usage and
 * limitations can be found in Documentation/block/bfq-iosched.txt.
 *
 * BFQ is a proportional-share storage-I/O scheduling algorithm based
 * on the slice-by-slice service scheme of CFQ. But BFQ assigns
 * budgets, measured in number of sectors, to processes instead of
 * time slices. The device is not granted to the in-service process
 * for a given time slice, but until it has exhausted its assigned
 * budget. This change from the time to the service domain enables BFQ
 * to distribute the device throughput among processes as desired,
 * without any distortion due to throughput fluctuations, or to device
 * internal queueing. BFQ uses an ad hoc internal scheduler, called
 * B-WF2Q+, to schedule processes according to their budgets. More
 * precisely, BFQ schedules queues associated with processes. Each
 * process/queue is assigned a user-configurable weight, and B-WF2Q+
 * guarantees that each queue receives a fraction of the throughput
 * proportional to its weight. Thanks to the accurate policy of
 * B-WF2Q+, BFQ can afford to assign high budgets to I/O-bound
 * processes issuing sequential requests (to boost the throughput),
 * and yet guarantee a low latency to interactive and soft real-time
 * applications.
 *
 * In particular, to provide these low-latency guarantees, BFQ
 * explicitly privileges the I/O of two classes of time-sensitive
 * applications: interactive and soft real-time. This feature enables
 * BFQ to provide applications in these classes with a very low
 * latency. Finally, BFQ also features additional heuristics for
 * preserving both a low latency and a high throughput on NCQ-capable,
 * rotational or flash-based devices, and to get the job done quickly
 * for applications consisting in many I/O-bound processes.
 *
 * BFQ is described in [1], where also a reference to the initial, more
 * theoretical paper on BFQ can be found. The interested reader can find
 * in the latter paper full details on the main algorithm, as well as
 * formulas of the guarantees and formal proofs of all the properties.
 * With respect to the version of BFQ presented in these papers, this
 * implementation adds a few more heuristics, such as the one that
 * guarantees a low latency to soft real-time applications, and a
 * hierarchical extension based on H-WF2Q+.
 *
 * B-WF2Q+ is based on WF2Q+, which is described in [2], together with
 * H-WF2Q+, while the augmented tree used here to implement B-WF2Q+
 * with O(log N) complexity derives from the one introduced with EEVDF
 * in [3].
 *
 * [1] P. Valente, A. Avanzini, "Evolution of the BFQ Storage I/O
 *     Scheduler", Proceedings of the First Workshop on Mobile System
 *     Technologies (MST-2015), May 2015.
 *     http://algogroup.unimore.it/people/paolo/disk_sched/mst-2015.pdf
 *
 * [2] Jon C.R. Bennett and H. Zhang, "Hierarchical Packet Fair Queueing
 *     Algorithms", IEEE/ACM Transactions on Networking, 5(5):675-689,
 *     Oct 1997.
 *
 * http://www.cs.cmu.edu/~hzhang/papers/TON-97-Oct.ps.gz
 *
 * [3] I. Stoica and H. Abdel-Wahab, "Earliest Eligible Virtual Deadline
 *     First: A Flexible and Accurate Mechanism for Proportional Share
 *     Resource Allocation", technical report.
 *
 * http://www.cs.berkeley.edu/~istoica/papers/eevdf-tr-95.pdf
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/cgroup.h>
#include <linux/elevator.h>
#include <linux/ktime.h>
#include <linux/rbtree.h>
#include <linux/ioprio.h>
#include <linux/sbitmap.h>
#include <linux/delay.h>

#include "blk.h"
#include "blk-mq.h"
#include "blk-mq-tag.h"
#include "blk-mq-sched.h"
#include <linux/blktrace_api.h>
#include <linux/hrtimer.h>
#include <linux/blk-cgroup.h>

#define BFQ_IOPRIO_CLASSES	3
#define BFQ_CL_IDLE_TIMEOUT	(HZ/5)

#define BFQ_MIN_WEIGHT			1
#define BFQ_MAX_WEIGHT			1000
#define BFQ_WEIGHT_CONVERSION_COEFF	10

#define BFQ_DEFAULT_QUEUE_IOPRIO	4

#define BFQ_WEIGHT_LEGACY_DFL	100
#define BFQ_DEFAULT_GRP_IOPRIO	0
#define BFQ_DEFAULT_GRP_CLASS	IOPRIO_CLASS_BE

/*
 * Soft real-time applications are extremely more latency sensitive
 * than interactive ones. Over-raise the weight of the former to
 * privilege them against the latter.
 */
#define BFQ_SOFTRT_WEIGHT_FACTOR	100

struct bfq_entity;

/**
 * struct bfq_service_tree - per ioprio_class service tree.
 *
 * Each service tree represents a B-WF2Q+ scheduler on its own.  Each
 * ioprio_class has its own independent scheduler, and so its own
 * bfq_service_tree.  All the fields are protected by the queue lock
 * of the containing bfqd.
 */
struct bfq_service_tree {
	/* tree for active entities (i.e., those backlogged) */
	struct rb_root active;
	/* tree for idle entities (i.e., not backlogged, with V <= F_i)*/
	struct rb_root idle;

	/* idle entity with minimum F_i */
	struct bfq_entity *first_idle;
	/* idle entity with maximum F_i */
	struct bfq_entity *last_idle;

	/* scheduler virtual time */
	u64 vtime;
	/* scheduler weight sum; active and idle entities contribute to it */
	unsigned long wsum;
};

/**
 * struct bfq_sched_data - multi-class scheduler.
 *
 * bfq_sched_data is the basic scheduler queue.  It supports three
 * ioprio_classes, and can be used either as a toplevel queue or as an
 * intermediate queue on a hierarchical setup.  @next_in_service
 * points to the active entity of the sched_data service trees that
 * will be scheduled next. It is used to reduce the number of steps
 * needed for each hierarchical-schedule update.
 *
 * The supported ioprio_classes are the same as in CFQ, in descending
 * priority order, IOPRIO_CLASS_RT, IOPRIO_CLASS_BE, IOPRIO_CLASS_IDLE.
 * Requests from higher priority queues are served before all the
 * requests from lower priority queues; among requests of the same
 * queue requests are served according to B-WF2Q+.
 * All the fields are protected by the queue lock of the containing bfqd.
 */
struct bfq_sched_data {
	/* entity in service */
	struct bfq_entity *in_service_entity;
	/* head-of-line entity (see comments above) */
	struct bfq_entity *next_in_service;
	/* array of service trees, one per ioprio_class */
	struct bfq_service_tree service_tree[BFQ_IOPRIO_CLASSES];
	/* last time CLASS_IDLE was served */
	unsigned long bfq_class_idle_last_service;

};

/**
 * struct bfq_weight_counter - counter of the number of all active entities
 *                             with a given weight.
 */
struct bfq_weight_counter {
	unsigned int weight; /* weight of the entities this counter refers to */
	unsigned int num_active; /* nr of active entities with this weight */
	/*
	 * Weights tree member (see bfq_data's @queue_weights_tree and
	 * @group_weights_tree)
	 */
	struct rb_node weights_node;
};

/**
 * struct bfq_entity - schedulable entity.
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
	/* service_tree member */
	struct rb_node rb_node;
	/* pointer to the weight counter associated with this entity */
	struct bfq_weight_counter *weight_counter;

	/*
	 * Flag, true if the entity is on a tree (either the active or
	 * the idle one of its service_tree) or is in service.
	 */
	bool on_st;

	/* B-WF2Q+ start and finish timestamps [sectors/weight] */
	u64 start, finish;

	/* tree the entity is enqueued into; %NULL if not on a tree */
	struct rb_root *tree;

	/*
	 * minimum start time of the (active) subtree rooted at this
	 * entity; used for O(log N) lookups into active trees
	 */
	u64 min_start;

	/* amount of service received during the last service slot */
	int service;

	/* budget, used also to calculate F_i: F_i = S_i + @budget / @weight */
	int budget;

	/* weight of the queue */
	int weight;
	/* next weight if a change is in progress */
	int new_weight;

	/* original weight, used to implement weight boosting */
	int orig_weight;

	/* parent entity, for hierarchical scheduling */
	struct bfq_entity *parent;

	/*
	 * For non-leaf nodes in the hierarchy, the associated
	 * scheduler queue, %NULL on leaf nodes.
	 */
	struct bfq_sched_data *my_sched_data;
	/* the scheduler queue this entity belongs to */
	struct bfq_sched_data *sched_data;

	/* flag, set to request a weight, ioprio or ioprio_class change  */
	int prio_changed;
};

struct bfq_group;

/**
 * struct bfq_ttime - per process thinktime stats.
 */
struct bfq_ttime {
	/* completion time of the last request */
	u64 last_end_request;

	/* total process thinktime */
	u64 ttime_total;
	/* number of thinktime samples */
	unsigned long ttime_samples;
	/* average process thinktime */
	u64 ttime_mean;
};

/**
 * struct bfq_queue - leaf schedulable entity.
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
	/* reference counter */
	int ref;
	/* parent bfq_data */
	struct bfq_data *bfqd;

	/* current ioprio and ioprio class */
	unsigned short ioprio, ioprio_class;
	/* next ioprio and ioprio class if a change is in progress */
	unsigned short new_ioprio, new_ioprio_class;

	/*
	 * Shared bfq_queue if queue is cooperating with one or more
	 * other queues.
	 */
	struct bfq_queue *new_bfqq;
	/* request-position tree member (see bfq_group's @rq_pos_tree) */
	struct rb_node pos_node;
	/* request-position tree root (see bfq_group's @rq_pos_tree) */
	struct rb_root *pos_root;

	/* sorted list of pending requests */
	struct rb_root sort_list;
	/* if fifo isn't expired, next request to serve */
	struct request *next_rq;
	/* number of sync and async requests queued */
	int queued[2];
	/* number of requests currently allocated */
	int allocated;
	/* number of pending metadata requests */
	int meta_pending;
	/* fifo list of requests in sort_list */
	struct list_head fifo;

	/* entity representing this queue in the scheduler */
	struct bfq_entity entity;

	/* maximum budget allowed from the feedback mechanism */
	int max_budget;
	/* budget expiration (in jiffies) */
	unsigned long budget_timeout;

	/* number of requests on the dispatch list or inside driver */
	int dispatched;

	/* status flags */
	unsigned long flags;

	/* node for active/idle bfqq list inside parent bfqd */
	struct list_head bfqq_list;

	/* associated @bfq_ttime struct */
	struct bfq_ttime ttime;

	/* bit vector: a 1 for each seeky requests in history */
	u32 seek_history;

	/* node for the device's burst list */
	struct hlist_node burst_list_node;

	/* position of the last request enqueued */
	sector_t last_request_pos;

	/* Number of consecutive pairs of request completion and
	 * arrival, such that the queue becomes idle after the
	 * completion, but the next request arrives within an idle
	 * time slice; used only if the queue's IO_bound flag has been
	 * cleared.
	 */
	unsigned int requests_within_timer;

	/* pid of the process owning the queue, used for logging purposes */
	pid_t pid;

	/*
	 * Pointer to the bfq_io_cq owning the bfq_queue, set to %NULL
	 * if the queue is shared.
	 */
	struct bfq_io_cq *bic;

	/* current maximum weight-raising time for this queue */
	unsigned long wr_cur_max_time;
	/*
	 * Minimum time instant such that, only if a new request is
	 * enqueued after this time instant in an idle @bfq_queue with
	 * no outstanding requests, then the task associated with the
	 * queue it is deemed as soft real-time (see the comments on
	 * the function bfq_bfqq_softrt_next_start())
	 */
	unsigned long soft_rt_next_start;
	/*
	 * Start time of the current weight-raising period if
	 * the @bfq-queue is being weight-raised, otherwise
	 * finish time of the last weight-raising period.
	 */
	unsigned long last_wr_start_finish;
	/* factor by which the weight of this queue is multiplied */
	unsigned int wr_coeff;
	/*
	 * Time of the last transition of the @bfq_queue from idle to
	 * backlogged.
	 */
	unsigned long last_idle_bklogged;
	/*
	 * Cumulative service received from the @bfq_queue since the
	 * last transition from idle to backlogged.
	 */
	unsigned long service_from_backlogged;

	/*
	 * Value of wr start time when switching to soft rt
	 */
	unsigned long wr_start_at_switch_to_srt;

	unsigned long split_time; /* time of last split */
};

/**
 * struct bfq_io_cq - per (request_queue, io_context) structure.
 */
struct bfq_io_cq {
	/* associated io_cq structure */
	struct io_cq icq; /* must be the first member */
	/* array of two process queues, the sync and the async */
	struct bfq_queue *bfqq[2];
	/* per (request_queue, blkcg) ioprio */
	int ioprio;
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	uint64_t blkcg_serial_nr; /* the current blkcg serial */
#endif
	/*
	 * Snapshot of the idle window before merging; taken to
	 * remember this value while the queue is merged, so as to be
	 * able to restore it in case of split.
	 */
	bool saved_idle_window;
	/*
	 * Same purpose as the previous two fields for the I/O bound
	 * classification of a queue.
	 */
	bool saved_IO_bound;

	/*
	 * Same purpose as the previous fields for the value of the
	 * field keeping the queue's belonging to a large burst
	 */
	bool saved_in_large_burst;
	/*
	 * True if the queue belonged to a burst list before its merge
	 * with another cooperating queue.
	 */
	bool was_in_burst_list;

	/*
	 * Similar to previous fields: save wr information.
	 */
	unsigned long saved_wr_coeff;
	unsigned long saved_last_wr_start_finish;
	unsigned long saved_wr_start_at_switch_to_srt;
	unsigned int saved_wr_cur_max_time;
	struct bfq_ttime saved_ttime;
};

enum bfq_device_speed {
	BFQ_BFQD_FAST,
	BFQ_BFQD_SLOW,
};

/**
 * struct bfq_data - per-device data structure.
 *
 * All the fields are protected by @lock.
 */
struct bfq_data {
	/* device request queue */
	struct request_queue *queue;
	/* dispatch queue */
	struct list_head dispatch;

	/* root bfq_group for the device */
	struct bfq_group *root_group;

	/*
	 * rbtree of weight counters of @bfq_queues, sorted by
	 * weight. Used to keep track of whether all @bfq_queues have
	 * the same weight. The tree contains one counter for each
	 * distinct weight associated to some active and not
	 * weight-raised @bfq_queue (see the comments to the functions
	 * bfq_weights_tree_[add|remove] for further details).
	 */
	struct rb_root queue_weights_tree;
	/*
	 * rbtree of non-queue @bfq_entity weight counters, sorted by
	 * weight. Used to keep track of whether all @bfq_groups have
	 * the same weight. The tree contains one counter for each
	 * distinct weight associated to some active @bfq_group (see
	 * the comments to the functions bfq_weights_tree_[add|remove]
	 * for further details).
	 */
	struct rb_root group_weights_tree;

	/*
	 * Number of bfq_queues containing requests (including the
	 * queue in service, even if it is idling).
	 */
	int busy_queues;
	/* number of weight-raised busy @bfq_queues */
	int wr_busy_queues;
	/* number of queued requests */
	int queued;
	/* number of requests dispatched and waiting for completion */
	int rq_in_driver;

	/*
	 * Maximum number of requests in driver in the last
	 * @hw_tag_samples completed requests.
	 */
	int max_rq_in_driver;
	/* number of samples used to calculate hw_tag */
	int hw_tag_samples;
	/* flag set to one if the driver is showing a queueing behavior */
	int hw_tag;

	/* number of budgets assigned */
	int budgets_assigned;

	/*
	 * Timer set when idling (waiting) for the next request from
	 * the queue in service.
	 */
	struct hrtimer idle_slice_timer;

	/* bfq_queue in service */
	struct bfq_queue *in_service_queue;

	/* on-disk position of the last served request */
	sector_t last_position;

	/* time of last request completion (ns) */
	u64 last_completion;

	/* time of first rq dispatch in current observation interval (ns) */
	u64 first_dispatch;
	/* time of last rq dispatch in current observation interval (ns) */
	u64 last_dispatch;

	/* beginning of the last budget */
	ktime_t last_budget_start;
	/* beginning of the last idle slice */
	ktime_t last_idling_start;

	/* number of samples in current observation interval */
	int peak_rate_samples;
	/* num of samples of seq dispatches in current observation interval */
	u32 sequential_samples;
	/* total num of sectors transferred in current observation interval */
	u64 tot_sectors_dispatched;
	/* max rq size seen during current observation interval (sectors) */
	u32 last_rq_max_size;
	/* time elapsed from first dispatch in current observ. interval (us) */
	u64 delta_from_first;
	/*
	 * Current estimate of the device peak rate, measured in
	 * [BFQ_RATE_SHIFT * sectors/usec]. The left-shift by
	 * BFQ_RATE_SHIFT is performed to increase precision in
	 * fixed-point calculations.
	 */
	u32 peak_rate;

	/* maximum budget allotted to a bfq_queue before rescheduling */
	int bfq_max_budget;

	/* list of all the bfq_queues active on the device */
	struct list_head active_list;
	/* list of all the bfq_queues idle on the device */
	struct list_head idle_list;

	/*
	 * Timeout for async/sync requests; when it fires, requests
	 * are served in fifo order.
	 */
	u64 bfq_fifo_expire[2];
	/* weight of backward seeks wrt forward ones */
	unsigned int bfq_back_penalty;
	/* maximum allowed backward seek */
	unsigned int bfq_back_max;
	/* maximum idling time */
	u32 bfq_slice_idle;

	/* user-configured max budget value (0 for auto-tuning) */
	int bfq_user_max_budget;
	/*
	 * Timeout for bfq_queues to consume their budget; used to
	 * prevent seeky queues from imposing long latencies to
	 * sequential or quasi-sequential ones (this also implies that
	 * seeky queues cannot receive guarantees in the service
	 * domain; after a timeout they are charged for the time they
	 * have been in service, to preserve fairness among them, but
	 * without service-domain guarantees).
	 */
	unsigned int bfq_timeout;

	/*
	 * Number of consecutive requests that must be issued within
	 * the idle time slice to set again idling to a queue which
	 * was marked as non-I/O-bound (see the definition of the
	 * IO_bound flag for further details).
	 */
	unsigned int bfq_requests_within_timer;

	/*
	 * Force device idling whenever needed to provide accurate
	 * service guarantees, without caring about throughput
	 * issues. CAVEAT: this may even increase latencies, in case
	 * of useless idling for processes that did stop doing I/O.
	 */
	bool strict_guarantees;

	/*
	 * Last time at which a queue entered the current burst of
	 * queues being activated shortly after each other; for more
	 * details about this and the following parameters related to
	 * a burst of activations, see the comments on the function
	 * bfq_handle_burst.
	 */
	unsigned long last_ins_in_burst;
	/*
	 * Reference time interval used to decide whether a queue has
	 * been activated shortly after @last_ins_in_burst.
	 */
	unsigned long bfq_burst_interval;
	/* number of queues in the current burst of queue activations */
	int burst_size;

	/* common parent entity for the queues in the burst */
	struct bfq_entity *burst_parent_entity;
	/* Maximum burst size above which the current queue-activation
	 * burst is deemed as 'large'.
	 */
	unsigned long bfq_large_burst_thresh;
	/* true if a large queue-activation burst is in progress */
	bool large_burst;
	/*
	 * Head of the burst list (as for the above fields, more
	 * details in the comments on the function bfq_handle_burst).
	 */
	struct hlist_head burst_list;

	/* if set to true, low-latency heuristics are enabled */
	bool low_latency;
	/*
	 * Maximum factor by which the weight of a weight-raised queue
	 * is multiplied.
	 */
	unsigned int bfq_wr_coeff;
	/* maximum duration of a weight-raising period (jiffies) */
	unsigned int bfq_wr_max_time;

	/* Maximum weight-raising duration for soft real-time processes */
	unsigned int bfq_wr_rt_max_time;
	/*
	 * Minimum idle period after which weight-raising may be
	 * reactivated for a queue (in jiffies).
	 */
	unsigned int bfq_wr_min_idle_time;
	/*
	 * Minimum period between request arrivals after which
	 * weight-raising may be reactivated for an already busy async
	 * queue (in jiffies).
	 */
	unsigned long bfq_wr_min_inter_arr_async;

	/* Max service-rate for a soft real-time queue, in sectors/sec */
	unsigned int bfq_wr_max_softrt_rate;
	/*
	 * Cached value of the product R*T, used for computing the
	 * maximum duration of weight raising automatically.
	 */
	u64 RT_prod;
	/* device-speed class for the low-latency heuristic */
	enum bfq_device_speed device_speed;

	/* fallback dummy bfqq for extreme OOM conditions */
	struct bfq_queue oom_bfqq;

	spinlock_t lock;

	/*
	 * bic associated with the task issuing current bio for
	 * merging. This and the next field are used as a support to
	 * be able to perform the bic lookup, needed by bio-merge
	 * functions, before the scheduler lock is taken, and thus
	 * avoid taking the request-queue lock while the scheduler
	 * lock is being held.
	 */
	struct bfq_io_cq *bio_bic;
	/* bfqq associated with the task issuing current bio for merging */
	struct bfq_queue *bio_bfqq;
};

enum bfqq_state_flags {
	BFQQF_just_created = 0,	/* queue just allocated */
	BFQQF_busy,		/* has requests or is in service */
	BFQQF_wait_request,	/* waiting for a request */
	BFQQF_non_blocking_wait_rq, /*
				     * waiting for a request
				     * without idling the device
				     */
	BFQQF_fifo_expire,	/* FIFO checked in this slice */
	BFQQF_idle_window,	/* slice idling enabled */
	BFQQF_sync,		/* synchronous queue */
	BFQQF_IO_bound,		/*
				 * bfqq has timed-out at least once
				 * having consumed at most 2/10 of
				 * its budget
				 */
	BFQQF_in_large_burst,	/*
				 * bfqq activated in a large burst,
				 * see comments to bfq_handle_burst.
				 */
	BFQQF_softrt_update,	/*
				 * may need softrt-next-start
				 * update
				 */
	BFQQF_coop,		/* bfqq is shared */
	BFQQF_split_coop	/* shared bfqq will be split */
};

#define BFQ_BFQQ_FNS(name)						\
static void bfq_mark_bfqq_##name(struct bfq_queue *bfqq)		\
{									\
	__set_bit(BFQQF_##name, &(bfqq)->flags);			\
}									\
static void bfq_clear_bfqq_##name(struct bfq_queue *bfqq)		\
{									\
	__clear_bit(BFQQF_##name, &(bfqq)->flags);		\
}									\
static int bfq_bfqq_##name(const struct bfq_queue *bfqq)		\
{									\
	return test_bit(BFQQF_##name, &(bfqq)->flags);		\
}

BFQ_BFQQ_FNS(just_created);
BFQ_BFQQ_FNS(busy);
BFQ_BFQQ_FNS(wait_request);
BFQ_BFQQ_FNS(non_blocking_wait_rq);
BFQ_BFQQ_FNS(fifo_expire);
BFQ_BFQQ_FNS(idle_window);
BFQ_BFQQ_FNS(sync);
BFQ_BFQQ_FNS(IO_bound);
BFQ_BFQQ_FNS(in_large_burst);
BFQ_BFQQ_FNS(coop);
BFQ_BFQQ_FNS(split_coop);
BFQ_BFQQ_FNS(softrt_update);
#undef BFQ_BFQQ_FNS

/* Logging facilities. */
#ifdef CONFIG_BFQ_GROUP_IOSCHED
static struct bfq_group *bfqq_group(struct bfq_queue *bfqq);
static struct blkcg_gq *bfqg_to_blkg(struct bfq_group *bfqg);

#define bfq_log_bfqq(bfqd, bfqq, fmt, args...)	do {			\
	char __pbuf[128];						\
									\
	blkg_path(bfqg_to_blkg(bfqq_group(bfqq)), __pbuf, sizeof(__pbuf)); \
	blk_add_trace_msg((bfqd)->queue, "bfq%d%c %s " fmt, (bfqq)->pid, \
			bfq_bfqq_sync((bfqq)) ? 'S' : 'A',		\
			  __pbuf, ##args);				\
} while (0)

#define bfq_log_bfqg(bfqd, bfqg, fmt, args...)	do {			\
	char __pbuf[128];						\
									\
	blkg_path(bfqg_to_blkg(bfqg), __pbuf, sizeof(__pbuf));		\
	blk_add_trace_msg((bfqd)->queue, "%s " fmt, __pbuf, ##args);	\
} while (0)

#else /* CONFIG_BFQ_GROUP_IOSCHED */

#define bfq_log_bfqq(bfqd, bfqq, fmt, args...)	\
	blk_add_trace_msg((bfqd)->queue, "bfq%d%c " fmt, (bfqq)->pid,	\
			bfq_bfqq_sync((bfqq)) ? 'S' : 'A',		\
				##args)
#define bfq_log_bfqg(bfqd, bfqg, fmt, args...)		do {} while (0)

#endif /* CONFIG_BFQ_GROUP_IOSCHED */

#define bfq_log(bfqd, fmt, args...) \
	blk_add_trace_msg((bfqd)->queue, "bfq " fmt, ##args)

/* Expiration reasons. */
enum bfqq_expiration {
	BFQQE_TOO_IDLE = 0,		/*
					 * queue has been idling for
					 * too long
					 */
	BFQQE_BUDGET_TIMEOUT,	/* budget took too long to be used */
	BFQQE_BUDGET_EXHAUSTED,	/* budget consumed */
	BFQQE_NO_MORE_REQUESTS,	/* the queue has no more requests */
	BFQQE_PREEMPTED		/* preemption in progress */
};

struct bfqg_stats {
#ifdef CONFIG_BFQ_GROUP_IOSCHED
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
#endif	/* CONFIG_BFQ_GROUP_IOSCHED */
};

#ifdef CONFIG_BFQ_GROUP_IOSCHED

/*
 * struct bfq_group_data - per-blkcg storage for the blkio subsystem.
 *
 * @ps: @blkcg_policy_storage that this structure inherits
 * @weight: weight of the bfq_group
 */
struct bfq_group_data {
	/* must be the first member */
	struct blkcg_policy_data pd;

	unsigned int weight;
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
 * @stats: stats for this bfqg.
 * @active_entities: number of active entities belonging to the group;
 *                   unused for the root group. Used to know whether there
 *                   are groups with more than one active @bfq_entity
 *                   (see the comments to the function
 *                   bfq_bfqq_may_idle()).
 * @rq_pos_tree: rbtree sorted by next_request position, used when
 *               determining if two or more queues have interleaving
 *               requests (see bfq_find_close_cooperator()).
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

	struct rb_root rq_pos_tree;

	struct bfqg_stats stats;
};

#else
struct bfq_group {
	struct bfq_sched_data sched_data;

	struct bfq_queue *async_bfqq[2][IOPRIO_BE_NR];
	struct bfq_queue *async_idle_bfqq;

	struct rb_root rq_pos_tree;
};
#endif

static struct bfq_queue *bfq_entity_to_bfqq(struct bfq_entity *entity);

static unsigned int bfq_class_idx(struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	return bfqq ? bfqq->ioprio_class - 1 :
		BFQ_DEFAULT_GRP_CLASS - 1;
}

static struct bfq_service_tree *
bfq_entity_service_tree(struct bfq_entity *entity)
{
	struct bfq_sched_data *sched_data = entity->sched_data;
	unsigned int idx = bfq_class_idx(entity);

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

#ifdef CONFIG_BFQ_GROUP_IOSCHED

static struct bfq_group *bfq_bfqq_to_bfqg(struct bfq_queue *bfqq)
{
	struct bfq_entity *group_entity = bfqq->entity.parent;

	if (!group_entity)
		group_entity = &bfqq->bfqd->root_group->entity;

	return container_of(group_entity, struct bfq_group, entity);
}

#else

static struct bfq_group *bfq_bfqq_to_bfqg(struct bfq_queue *bfqq)
{
	return bfqq->bfqd->root_group;
}

#endif

static void bfq_check_ioprio_change(struct bfq_io_cq *bic, struct bio *bio);
static void bfq_put_queue(struct bfq_queue *bfqq);
static struct bfq_queue *bfq_get_queue(struct bfq_data *bfqd,
				       struct bio *bio, bool is_sync,
				       struct bfq_io_cq *bic);
static void bfq_end_wr_async_queues(struct bfq_data *bfqd,
				    struct bfq_group *bfqg);
static void bfq_put_async_queues(struct bfq_data *bfqd, struct bfq_group *bfqg);
static void bfq_exit_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq);

/* Expiration time of sync (0) and async (1) requests, in ns. */
static const u64 bfq_fifo_expire[2] = { NSEC_PER_SEC / 4, NSEC_PER_SEC / 8 };

/* Maximum backwards seek (magic number lifted from CFQ), in KiB. */
static const int bfq_back_max = 16 * 1024;

/* Penalty of a backwards seek, in number of sectors. */
static const int bfq_back_penalty = 2;

/* Idling period duration, in ns. */
static u64 bfq_slice_idle = NSEC_PER_SEC / 125;

/* Minimum number of assigned budgets for which stats are safe to compute. */
static const int bfq_stats_min_budgets = 194;

/* Default maximum budget values, in sectors and number of requests. */
static const int bfq_default_max_budget = 16 * 1024;

/*
 * Async to sync throughput distribution is controlled as follows:
 * when an async request is served, the entity is charged the number
 * of sectors of the request, multiplied by the factor below
 */
static const int bfq_async_charge_factor = 10;

/* Default timeout values, in jiffies, approximating CFQ defaults. */
static const int bfq_timeout = HZ / 8;

static struct kmem_cache *bfq_pool;

/* Below this threshold (in ns), we consider thinktime immediate. */
#define BFQ_MIN_TT		(2 * NSEC_PER_MSEC)

/* hw_tag detection: parallel requests threshold and min samples needed. */
#define BFQ_HW_QUEUE_THRESHOLD	4
#define BFQ_HW_QUEUE_SAMPLES	32

#define BFQQ_SEEK_THR		(sector_t)(8 * 100)
#define BFQQ_SECT_THR_NONROT	(sector_t)(2 * 32)
#define BFQQ_CLOSE_THR		(sector_t)(8 * 1024)
#define BFQQ_SEEKY(bfqq)	(hweight32(bfqq->seek_history) > 32/8)

/* Min number of samples required to perform peak-rate update */
#define BFQ_RATE_MIN_SAMPLES	32
/* Min observation time interval required to perform a peak-rate update (ns) */
#define BFQ_RATE_MIN_INTERVAL	(300*NSEC_PER_MSEC)
/* Target observation time interval for a peak-rate update (ns) */
#define BFQ_RATE_REF_INTERVAL	NSEC_PER_SEC

/* Shift used for peak rate fixed precision calculations. */
#define BFQ_RATE_SHIFT		16

/*
 * By default, BFQ computes the duration of the weight raising for
 * interactive applications automatically, using the following formula:
 * duration = (R / r) * T, where r is the peak rate of the device, and
 * R and T are two reference parameters.
 * In particular, R is the peak rate of the reference device (see below),
 * and T is a reference time: given the systems that are likely to be
 * installed on the reference device according to its speed class, T is
 * about the maximum time needed, under BFQ and while reading two files in
 * parallel, to load typical large applications on these systems.
 * In practice, the slower/faster the device at hand is, the more/less it
 * takes to load applications with respect to the reference device.
 * Accordingly, the longer/shorter BFQ grants weight raising to interactive
 * applications.
 *
 * BFQ uses four different reference pairs (R, T), depending on:
 * . whether the device is rotational or non-rotational;
 * . whether the device is slow, such as old or portable HDDs, as well as
 *   SD cards, or fast, such as newer HDDs and SSDs.
 *
 * The device's speed class is dynamically (re)detected in
 * bfq_update_peak_rate() every time the estimated peak rate is updated.
 *
 * In the following definitions, R_slow[0]/R_fast[0] and
 * T_slow[0]/T_fast[0] are the reference values for a slow/fast
 * rotational device, whereas R_slow[1]/R_fast[1] and
 * T_slow[1]/T_fast[1] are the reference values for a slow/fast
 * non-rotational device. Finally, device_speed_thresh are the
 * thresholds used to switch between speed classes. The reference
 * rates are not the actual peak rates of the devices used as a
 * reference, but slightly lower values. The reason for using these
 * slightly lower values is that the peak-rate estimator tends to
 * yield slightly lower values than the actual peak rate (it can yield
 * the actual peak rate only if there is only one process doing I/O,
 * and the process does sequential I/O).
 *
 * Both the reference peak rates and the thresholds are measured in
 * sectors/usec, left-shifted by BFQ_RATE_SHIFT.
 */
static int R_slow[2] = {1000, 10700};
static int R_fast[2] = {14000, 33000};
/*
 * To improve readability, a conversion function is used to initialize the
 * following arrays, which entails that they can be initialized only in a
 * function.
 */
static int T_slow[2];
static int T_fast[2];
static int device_speed_thresh[2];

#define BFQ_SERVICE_TREE_INIT	((struct bfq_service_tree)		\
				{ RB_ROOT, RB_ROOT, NULL, NULL, 0, 0 })

#define RQ_BIC(rq)		((struct bfq_io_cq *) (rq)->elv.priv[0])
#define RQ_BFQQ(rq)		((rq)->elv.priv[1])

/**
 * icq_to_bic - convert iocontext queue structure to bfq_io_cq.
 * @icq: the iocontext queue.
 */
static struct bfq_io_cq *icq_to_bic(struct io_cq *icq)
{
	/* bic->icq is the first member, %NULL will convert to %NULL */
	return container_of(icq, struct bfq_io_cq, icq);
}

/**
 * bfq_bic_lookup - search into @ioc a bic associated to @bfqd.
 * @bfqd: the lookup key.
 * @ioc: the io_context of the process doing I/O.
 * @q: the request queue.
 */
static struct bfq_io_cq *bfq_bic_lookup(struct bfq_data *bfqd,
					struct io_context *ioc,
					struct request_queue *q)
{
	if (ioc) {
		unsigned long flags;
		struct bfq_io_cq *icq;

		spin_lock_irqsave(q->queue_lock, flags);
		icq = icq_to_bic(ioc_lookup_icq(ioc, q));
		spin_unlock_irqrestore(q->queue_lock, flags);

		return icq;
	}

	return NULL;
}

/*
 * Scheduler run of queue, if there are requests pending and no one in the
 * driver that will restart queueing.
 */
static void bfq_schedule_dispatch(struct bfq_data *bfqd)
{
	if (bfqd->queued != 0) {
		bfq_log(bfqd, "schedule dispatch");
		blk_mq_run_hw_queues(bfqd->queue, true);
	}
}

/**
 * bfq_gt - compare two timestamps.
 * @a: first ts.
 * @b: second ts.
 *
 * Return @a > @b, dealing with wrapping correctly.
 */
static int bfq_gt(u64 a, u64 b)
{
	return (s64)(a - b) > 0;
}

static struct bfq_entity *bfq_root_active_entity(struct rb_root *tree)
{
	struct rb_node *node = tree->rb_node;

	return rb_entry(node, struct bfq_entity, rb_node);
}

static struct bfq_entity *bfq_lookup_next_entity(struct bfq_sched_data *sd);

static bool bfq_update_parent_budget(struct bfq_entity *next_in_service);

/**
 * bfq_update_next_in_service - update sd->next_in_service
 * @sd: sched_data for which to perform the update.
 * @new_entity: if not NULL, pointer to the entity whose activation,
 *		requeueing or repositionig triggered the invocation of
 *		this function.
 *
 * This function is called to update sd->next_in_service, which, in
 * its turn, may change as a consequence of the insertion or
 * extraction of an entity into/from one of the active trees of
 * sd. These insertions/extractions occur as a consequence of
 * activations/deactivations of entities, with some activations being
 * 'true' activations, and other activations being requeueings (i.e.,
 * implementing the second, requeueing phase of the mechanism used to
 * reposition an entity in its active tree; see comments on
 * __bfq_activate_entity and __bfq_requeue_entity for details). In
 * both the last two activation sub-cases, new_entity points to the
 * just activated or requeued entity.
 *
 * Returns true if sd->next_in_service changes in such a way that
 * entity->parent may become the next_in_service for its parent
 * entity.
 */
static bool bfq_update_next_in_service(struct bfq_sched_data *sd,
				       struct bfq_entity *new_entity)
{
	struct bfq_entity *next_in_service = sd->next_in_service;
	bool parent_sched_may_change = false;

	/*
	 * If this update is triggered by the activation, requeueing
	 * or repositiong of an entity that does not coincide with
	 * sd->next_in_service, then a full lookup in the active tree
	 * can be avoided. In fact, it is enough to check whether the
	 * just-modified entity has a higher priority than
	 * sd->next_in_service, or, even if it has the same priority
	 * as sd->next_in_service, is eligible and has a lower virtual
	 * finish time than sd->next_in_service. If this compound
	 * condition holds, then the new entity becomes the new
	 * next_in_service. Otherwise no change is needed.
	 */
	if (new_entity && new_entity != sd->next_in_service) {
		/*
		 * Flag used to decide whether to replace
		 * sd->next_in_service with new_entity. Tentatively
		 * set to true, and left as true if
		 * sd->next_in_service is NULL.
		 */
		bool replace_next = true;

		/*
		 * If there is already a next_in_service candidate
		 * entity, then compare class priorities or timestamps
		 * to decide whether to replace sd->service_tree with
		 * new_entity.
		 */
		if (next_in_service) {
			unsigned int new_entity_class_idx =
				bfq_class_idx(new_entity);
			struct bfq_service_tree *st =
				sd->service_tree + new_entity_class_idx;

			/*
			 * For efficiency, evaluate the most likely
			 * sub-condition first.
			 */
			replace_next =
				(new_entity_class_idx ==
				 bfq_class_idx(next_in_service)
				 &&
				 !bfq_gt(new_entity->start, st->vtime)
				 &&
				 bfq_gt(next_in_service->finish,
					new_entity->finish))
				||
				new_entity_class_idx <
				bfq_class_idx(next_in_service);
		}

		if (replace_next)
			next_in_service = new_entity;
	} else /* invoked because of a deactivation: lookup needed */
		next_in_service = bfq_lookup_next_entity(sd);

	if (next_in_service) {
		parent_sched_may_change = !sd->next_in_service ||
			bfq_update_parent_budget(next_in_service);
	}

	sd->next_in_service = next_in_service;

	if (!next_in_service)
		return parent_sched_may_change;

	return parent_sched_may_change;
}

#ifdef CONFIG_BFQ_GROUP_IOSCHED
/* both next loops stop at one of the child entities of the root group */
#define for_each_entity(entity)	\
	for (; entity ; entity = entity->parent)

/*
 * For each iteration, compute parent in advance, so as to be safe if
 * entity is deallocated during the iteration. Such a deallocation may
 * happen as a consequence of a bfq_put_queue that frees the bfq_queue
 * containing entity.
 */
#define for_each_entity_safe(entity, parent) \
	for (; entity && ({ parent = entity->parent; 1; }); entity = parent)

/*
 * Returns true if this budget changes may let next_in_service->parent
 * become the next_in_service entity for its parent entity.
 */
static bool bfq_update_parent_budget(struct bfq_entity *next_in_service)
{
	struct bfq_entity *bfqg_entity;
	struct bfq_group *bfqg;
	struct bfq_sched_data *group_sd;
	bool ret = false;

	group_sd = next_in_service->sched_data;

	bfqg = container_of(group_sd, struct bfq_group, sched_data);
	/*
	 * bfq_group's my_entity field is not NULL only if the group
	 * is not the root group. We must not touch the root entity
	 * as it must never become an in-service entity.
	 */
	bfqg_entity = bfqg->my_entity;
	if (bfqg_entity) {
		if (bfqg_entity->budget > next_in_service->budget)
			ret = true;
		bfqg_entity->budget = next_in_service->budget;
	}

	return ret;
}

/*
 * This function tells whether entity stops being a candidate for next
 * service, according to the following logic.
 *
 * This function is invoked for an entity that is about to be set in
 * service. If such an entity is a queue, then the entity is no longer
 * a candidate for next service (i.e, a candidate entity to serve
 * after the in-service entity is expired). The function then returns
 * true.
 *
 * In contrast, the entity could stil be a candidate for next service
 * if it is not a queue, and has more than one child. In fact, even if
 * one of its children is about to be set in service, other children
 * may still be the next to serve. As a consequence, a non-queue
 * entity is not a candidate for next-service only if it has only one
 * child. And only if this condition holds, then the function returns
 * true for a non-queue entity.
 */
static bool bfq_no_longer_next_in_service(struct bfq_entity *entity)
{
	struct bfq_group *bfqg;

	if (bfq_entity_to_bfqq(entity))
		return true;

	bfqg = container_of(entity, struct bfq_group, entity);

	if (bfqg->active_entities == 1)
		return true;

	return false;
}

#else /* CONFIG_BFQ_GROUP_IOSCHED */
/*
 * Next two macros are fake loops when cgroups support is not
 * enabled. I fact, in such a case, there is only one level to go up
 * (to reach the root group).
 */
#define for_each_entity(entity)	\
	for (; entity ; entity = NULL)

#define for_each_entity_safe(entity, parent) \
	for (parent = NULL; entity ; entity = parent)

static bool bfq_update_parent_budget(struct bfq_entity *next_in_service)
{
	return false;
}

static bool bfq_no_longer_next_in_service(struct bfq_entity *entity)
{
	return true;
}

#endif /* CONFIG_BFQ_GROUP_IOSCHED */

/*
 * Shift for timestamp calculations.  This actually limits the maximum
 * service allowed in one timestamp delta (small shift values increase it),
 * the maximum total weight that can be used for the queues in the system
 * (big shift values increase it), and the period of virtual time
 * wraparounds.
 */
#define WFQ_SERVICE_SHIFT	22

static struct bfq_queue *bfq_entity_to_bfqq(struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = NULL;

	if (!entity->my_sched_data)
		bfqq = container_of(entity, struct bfq_queue, entity);

	return bfqq;
}


/**
 * bfq_delta - map service into the virtual time domain.
 * @service: amount of service.
 * @weight: scale factor (weight of an entity or weight sum).
 */
static u64 bfq_delta(unsigned long service, unsigned long weight)
{
	u64 d = (u64)service << WFQ_SERVICE_SHIFT;

	do_div(d, weight);
	return d;
}

/**
 * bfq_calc_finish - assign the finish time to an entity.
 * @entity: the entity to act upon.
 * @service: the service to be charged to the entity.
 */
static void bfq_calc_finish(struct bfq_entity *entity, unsigned long service)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	entity->finish = entity->start +
		bfq_delta(service, entity->weight);

	if (bfqq) {
		bfq_log_bfqq(bfqq->bfqd, bfqq,
			"calc_finish: serv %lu, w %d",
			service, entity->weight);
		bfq_log_bfqq(bfqq->bfqd, bfqq,
			"calc_finish: start %llu, finish %llu, delta %llu",
			entity->start, entity->finish,
			bfq_delta(service, entity->weight));
	}
}

/**
 * bfq_entity_of - get an entity from a node.
 * @node: the node field of the entity.
 *
 * Convert a node pointer to the relative entity.  This is used only
 * to simplify the logic of some functions and not as the generic
 * conversion mechanism because, e.g., in the tree walking functions,
 * the check for a %NULL value would be redundant.
 */
static struct bfq_entity *bfq_entity_of(struct rb_node *node)
{
	struct bfq_entity *entity = NULL;

	if (node)
		entity = rb_entry(node, struct bfq_entity, rb_node);

	return entity;
}

/**
 * bfq_extract - remove an entity from a tree.
 * @root: the tree root.
 * @entity: the entity to remove.
 */
static void bfq_extract(struct rb_root *root, struct bfq_entity *entity)
{
	entity->tree = NULL;
	rb_erase(&entity->rb_node, root);
}

/**
 * bfq_idle_extract - extract an entity from the idle tree.
 * @st: the service tree of the owning @entity.
 * @entity: the entity being removed.
 */
static void bfq_idle_extract(struct bfq_service_tree *st,
			     struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);
	struct rb_node *next;

	if (entity == st->first_idle) {
		next = rb_next(&entity->rb_node);
		st->first_idle = bfq_entity_of(next);
	}

	if (entity == st->last_idle) {
		next = rb_prev(&entity->rb_node);
		st->last_idle = bfq_entity_of(next);
	}

	bfq_extract(&st->idle, entity);

	if (bfqq)
		list_del(&bfqq->bfqq_list);
}

/**
 * bfq_insert - generic tree insertion.
 * @root: tree root.
 * @entity: entity to insert.
 *
 * This is used for the idle and the active tree, since they are both
 * ordered by finish time.
 */
static void bfq_insert(struct rb_root *root, struct bfq_entity *entity)
{
	struct bfq_entity *entry;
	struct rb_node **node = &root->rb_node;
	struct rb_node *parent = NULL;

	while (*node) {
		parent = *node;
		entry = rb_entry(parent, struct bfq_entity, rb_node);

		if (bfq_gt(entry->finish, entity->finish))
			node = &parent->rb_left;
		else
			node = &parent->rb_right;
	}

	rb_link_node(&entity->rb_node, parent, node);
	rb_insert_color(&entity->rb_node, root);

	entity->tree = root;
}

/**
 * bfq_update_min - update the min_start field of a entity.
 * @entity: the entity to update.
 * @node: one of its children.
 *
 * This function is called when @entity may store an invalid value for
 * min_start due to updates to the active tree.  The function  assumes
 * that the subtree rooted at @node (which may be its left or its right
 * child) has a valid min_start value.
 */
static void bfq_update_min(struct bfq_entity *entity, struct rb_node *node)
{
	struct bfq_entity *child;

	if (node) {
		child = rb_entry(node, struct bfq_entity, rb_node);
		if (bfq_gt(entity->min_start, child->min_start))
			entity->min_start = child->min_start;
	}
}

/**
 * bfq_update_active_node - recalculate min_start.
 * @node: the node to update.
 *
 * @node may have changed position or one of its children may have moved,
 * this function updates its min_start value.  The left and right subtrees
 * are assumed to hold a correct min_start value.
 */
static void bfq_update_active_node(struct rb_node *node)
{
	struct bfq_entity *entity = rb_entry(node, struct bfq_entity, rb_node);

	entity->min_start = entity->start;
	bfq_update_min(entity, node->rb_right);
	bfq_update_min(entity, node->rb_left);
}

/**
 * bfq_update_active_tree - update min_start for the whole active tree.
 * @node: the starting node.
 *
 * @node must be the deepest modified node after an update.  This function
 * updates its min_start using the values held by its children, assuming
 * that they did not change, and then updates all the nodes that may have
 * changed in the path to the root.  The only nodes that may have changed
 * are the ones in the path or their siblings.
 */
static void bfq_update_active_tree(struct rb_node *node)
{
	struct rb_node *parent;

up:
	bfq_update_active_node(node);

	parent = rb_parent(node);
	if (!parent)
		return;

	if (node == parent->rb_left && parent->rb_right)
		bfq_update_active_node(parent->rb_right);
	else if (parent->rb_left)
		bfq_update_active_node(parent->rb_left);

	node = parent;
	goto up;
}

static void bfq_weights_tree_add(struct bfq_data *bfqd,
				 struct bfq_entity *entity,
				 struct rb_root *root);

static void bfq_weights_tree_remove(struct bfq_data *bfqd,
				    struct bfq_entity *entity,
				    struct rb_root *root);


/**
 * bfq_active_insert - insert an entity in the active tree of its
 *                     group/device.
 * @st: the service tree of the entity.
 * @entity: the entity being inserted.
 *
 * The active tree is ordered by finish time, but an extra key is kept
 * per each node, containing the minimum value for the start times of
 * its children (and the node itself), so it's possible to search for
 * the eligible node with the lowest finish time in logarithmic time.
 */
static void bfq_active_insert(struct bfq_service_tree *st,
			      struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);
	struct rb_node *node = &entity->rb_node;
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	struct bfq_sched_data *sd = NULL;
	struct bfq_group *bfqg = NULL;
	struct bfq_data *bfqd = NULL;
#endif

	bfq_insert(&st->active, entity);

	if (node->rb_left)
		node = node->rb_left;
	else if (node->rb_right)
		node = node->rb_right;

	bfq_update_active_tree(node);

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	sd = entity->sched_data;
	bfqg = container_of(sd, struct bfq_group, sched_data);
	bfqd = (struct bfq_data *)bfqg->bfqd;
#endif
	if (bfqq)
		list_add(&bfqq->bfqq_list, &bfqq->bfqd->active_list);
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	else /* bfq_group */
		bfq_weights_tree_add(bfqd, entity, &bfqd->group_weights_tree);

	if (bfqg != bfqd->root_group)
		bfqg->active_entities++;
#endif
}

/**
 * bfq_ioprio_to_weight - calc a weight from an ioprio.
 * @ioprio: the ioprio value to convert.
 */
static unsigned short bfq_ioprio_to_weight(int ioprio)
{
	return (IOPRIO_BE_NR - ioprio) * BFQ_WEIGHT_CONVERSION_COEFF;
}

/**
 * bfq_weight_to_ioprio - calc an ioprio from a weight.
 * @weight: the weight value to convert.
 *
 * To preserve as much as possible the old only-ioprio user interface,
 * 0 is used as an escape ioprio value for weights (numerically) equal or
 * larger than IOPRIO_BE_NR * BFQ_WEIGHT_CONVERSION_COEFF.
 */
static unsigned short bfq_weight_to_ioprio(int weight)
{
	return max_t(int, 0,
		     IOPRIO_BE_NR * BFQ_WEIGHT_CONVERSION_COEFF - weight);
}

static void bfq_get_entity(struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	if (bfqq) {
		bfqq->ref++;
		bfq_log_bfqq(bfqq->bfqd, bfqq, "get_entity: %p %d",
			     bfqq, bfqq->ref);
	}
}

/**
 * bfq_find_deepest - find the deepest node that an extraction can modify.
 * @node: the node being removed.
 *
 * Do the first step of an extraction in an rb tree, looking for the
 * node that will replace @node, and returning the deepest node that
 * the following modifications to the tree can touch.  If @node is the
 * last node in the tree return %NULL.
 */
static struct rb_node *bfq_find_deepest(struct rb_node *node)
{
	struct rb_node *deepest;

	if (!node->rb_right && !node->rb_left)
		deepest = rb_parent(node);
	else if (!node->rb_right)
		deepest = node->rb_left;
	else if (!node->rb_left)
		deepest = node->rb_right;
	else {
		deepest = rb_next(node);
		if (deepest->rb_right)
			deepest = deepest->rb_right;
		else if (rb_parent(deepest) != node)
			deepest = rb_parent(deepest);
	}

	return deepest;
}

/**
 * bfq_active_extract - remove an entity from the active tree.
 * @st: the service_tree containing the tree.
 * @entity: the entity being removed.
 */
static void bfq_active_extract(struct bfq_service_tree *st,
			       struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);
	struct rb_node *node;
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	struct bfq_sched_data *sd = NULL;
	struct bfq_group *bfqg = NULL;
	struct bfq_data *bfqd = NULL;
#endif

	node = bfq_find_deepest(&entity->rb_node);
	bfq_extract(&st->active, entity);

	if (node)
		bfq_update_active_tree(node);

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	sd = entity->sched_data;
	bfqg = container_of(sd, struct bfq_group, sched_data);
	bfqd = (struct bfq_data *)bfqg->bfqd;
#endif
	if (bfqq)
		list_del(&bfqq->bfqq_list);
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	else /* bfq_group */
		bfq_weights_tree_remove(bfqd, entity,
					&bfqd->group_weights_tree);

	if (bfqg != bfqd->root_group)
		bfqg->active_entities--;
#endif
}

/**
 * bfq_idle_insert - insert an entity into the idle tree.
 * @st: the service tree containing the tree.
 * @entity: the entity to insert.
 */
static void bfq_idle_insert(struct bfq_service_tree *st,
			    struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);
	struct bfq_entity *first_idle = st->first_idle;
	struct bfq_entity *last_idle = st->last_idle;

	if (!first_idle || bfq_gt(first_idle->finish, entity->finish))
		st->first_idle = entity;
	if (!last_idle || bfq_gt(entity->finish, last_idle->finish))
		st->last_idle = entity;

	bfq_insert(&st->idle, entity);

	if (bfqq)
		list_add(&bfqq->bfqq_list, &bfqq->bfqd->idle_list);
}

/**
 * bfq_forget_entity - do not consider entity any longer for scheduling
 * @st: the service tree.
 * @entity: the entity being removed.
 * @is_in_service: true if entity is currently the in-service entity.
 *
 * Forget everything about @entity. In addition, if entity represents
 * a queue, and the latter is not in service, then release the service
 * reference to the queue (the one taken through bfq_get_entity). In
 * fact, in this case, there is really no more service reference to
 * the queue, as the latter is also outside any service tree. If,
 * instead, the queue is in service, then __bfq_bfqd_reset_in_service
 * will take care of putting the reference when the queue finally
 * stops being served.
 */
static void bfq_forget_entity(struct bfq_service_tree *st,
			      struct bfq_entity *entity,
			      bool is_in_service)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	entity->on_st = false;
	st->wsum -= entity->weight;
	if (bfqq && !is_in_service)
		bfq_put_queue(bfqq);
}

/**
 * bfq_put_idle_entity - release the idle tree ref of an entity.
 * @st: service tree for the entity.
 * @entity: the entity being released.
 */
static void bfq_put_idle_entity(struct bfq_service_tree *st,
				struct bfq_entity *entity)
{
	bfq_idle_extract(st, entity);
	bfq_forget_entity(st, entity,
			  entity == entity->sched_data->in_service_entity);
}

/**
 * bfq_forget_idle - update the idle tree if necessary.
 * @st: the service tree to act upon.
 *
 * To preserve the global O(log N) complexity we only remove one entry here;
 * as the idle tree will not grow indefinitely this can be done safely.
 */
static void bfq_forget_idle(struct bfq_service_tree *st)
{
	struct bfq_entity *first_idle = st->first_idle;
	struct bfq_entity *last_idle = st->last_idle;

	if (RB_EMPTY_ROOT(&st->active) && last_idle &&
	    !bfq_gt(last_idle->finish, st->vtime)) {
		/*
		 * Forget the whole idle tree, increasing the vtime past
		 * the last finish time of idle entities.
		 */
		st->vtime = last_idle->finish;
	}

	if (first_idle && !bfq_gt(first_idle->finish, st->vtime))
		bfq_put_idle_entity(st, first_idle);
}

static struct bfq_service_tree *
__bfq_entity_update_weight_prio(struct bfq_service_tree *old_st,
				struct bfq_entity *entity)
{
	struct bfq_service_tree *new_st = old_st;

	if (entity->prio_changed) {
		struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);
		unsigned int prev_weight, new_weight;
		struct bfq_data *bfqd = NULL;
		struct rb_root *root;
#ifdef CONFIG_BFQ_GROUP_IOSCHED
		struct bfq_sched_data *sd;
		struct bfq_group *bfqg;
#endif

		if (bfqq)
			bfqd = bfqq->bfqd;
#ifdef CONFIG_BFQ_GROUP_IOSCHED
		else {
			sd = entity->my_sched_data;
			bfqg = container_of(sd, struct bfq_group, sched_data);
			bfqd = (struct bfq_data *)bfqg->bfqd;
		}
#endif

		old_st->wsum -= entity->weight;

		if (entity->new_weight != entity->orig_weight) {
			if (entity->new_weight < BFQ_MIN_WEIGHT ||
			    entity->new_weight > BFQ_MAX_WEIGHT) {
				pr_crit("update_weight_prio: new_weight %d\n",
					entity->new_weight);
				if (entity->new_weight < BFQ_MIN_WEIGHT)
					entity->new_weight = BFQ_MIN_WEIGHT;
				else
					entity->new_weight = BFQ_MAX_WEIGHT;
			}
			entity->orig_weight = entity->new_weight;
			if (bfqq)
				bfqq->ioprio =
				  bfq_weight_to_ioprio(entity->orig_weight);
		}

		if (bfqq)
			bfqq->ioprio_class = bfqq->new_ioprio_class;
		entity->prio_changed = 0;

		/*
		 * NOTE: here we may be changing the weight too early,
		 * this will cause unfairness.  The correct approach
		 * would have required additional complexity to defer
		 * weight changes to the proper time instants (i.e.,
		 * when entity->finish <= old_st->vtime).
		 */
		new_st = bfq_entity_service_tree(entity);

		prev_weight = entity->weight;
		new_weight = entity->orig_weight *
			     (bfqq ? bfqq->wr_coeff : 1);
		/*
		 * If the weight of the entity changes, remove the entity
		 * from its old weight counter (if there is a counter
		 * associated with the entity), and add it to the counter
		 * associated with its new weight.
		 */
		if (prev_weight != new_weight) {
			root = bfqq ? &bfqd->queue_weights_tree :
				      &bfqd->group_weights_tree;
			bfq_weights_tree_remove(bfqd, entity, root);
		}
		entity->weight = new_weight;
		/*
		 * Add the entity to its weights tree only if it is
		 * not associated with a weight-raised queue.
		 */
		if (prev_weight != new_weight &&
		    (bfqq ? bfqq->wr_coeff == 1 : 1))
			/* If we get here, root has been initialized. */
			bfq_weights_tree_add(bfqd, entity, root);

		new_st->wsum += entity->weight;

		if (new_st != old_st)
			entity->start = new_st->vtime;
	}

	return new_st;
}

static void bfqg_stats_set_start_empty_time(struct bfq_group *bfqg);
static struct bfq_group *bfqq_group(struct bfq_queue *bfqq);

/**
 * bfq_bfqq_served - update the scheduler status after selection for
 *                   service.
 * @bfqq: the queue being served.
 * @served: bytes to transfer.
 *
 * NOTE: this can be optimized, as the timestamps of upper level entities
 * are synchronized every time a new bfqq is selected for service.  By now,
 * we keep it to better check consistency.
 */
static void bfq_bfqq_served(struct bfq_queue *bfqq, int served)
{
	struct bfq_entity *entity = &bfqq->entity;
	struct bfq_service_tree *st;

	for_each_entity(entity) {
		st = bfq_entity_service_tree(entity);

		entity->service += served;

		st->vtime += bfq_delta(served, st->wsum);
		bfq_forget_idle(st);
	}
	bfqg_stats_set_start_empty_time(bfqq_group(bfqq));
	bfq_log_bfqq(bfqq->bfqd, bfqq, "bfqq_served %d secs", served);
}

/**
 * bfq_bfqq_charge_time - charge an amount of service equivalent to the length
 *			  of the time interval during which bfqq has been in
 *			  service.
 * @bfqd: the device
 * @bfqq: the queue that needs a service update.
 * @time_ms: the amount of time during which the queue has received service
 *
 * If a queue does not consume its budget fast enough, then providing
 * the queue with service fairness may impair throughput, more or less
 * severely. For this reason, queues that consume their budget slowly
 * are provided with time fairness instead of service fairness. This
 * goal is achieved through the BFQ scheduling engine, even if such an
 * engine works in the service, and not in the time domain. The trick
 * is charging these queues with an inflated amount of service, equal
 * to the amount of service that they would have received during their
 * service slot if they had been fast, i.e., if their requests had
 * been dispatched at a rate equal to the estimated peak rate.
 *
 * It is worth noting that time fairness can cause important
 * distortions in terms of bandwidth distribution, on devices with
 * internal queueing. The reason is that I/O requests dispatched
 * during the service slot of a queue may be served after that service
 * slot is finished, and may have a total processing time loosely
 * correlated with the duration of the service slot. This is
 * especially true for short service slots.
 */
static void bfq_bfqq_charge_time(struct bfq_data *bfqd, struct bfq_queue *bfqq,
				 unsigned long time_ms)
{
	struct bfq_entity *entity = &bfqq->entity;
	int tot_serv_to_charge = entity->service;
	unsigned int timeout_ms = jiffies_to_msecs(bfq_timeout);

	if (time_ms > 0 && time_ms < timeout_ms)
		tot_serv_to_charge =
			(bfqd->bfq_max_budget * time_ms) / timeout_ms;

	if (tot_serv_to_charge < entity->service)
		tot_serv_to_charge = entity->service;

	/* Increase budget to avoid inconsistencies */
	if (tot_serv_to_charge > entity->budget)
		entity->budget = tot_serv_to_charge;

	bfq_bfqq_served(bfqq,
			max_t(int, 0, tot_serv_to_charge - entity->service));
}

static void bfq_update_fin_time_enqueue(struct bfq_entity *entity,
					struct bfq_service_tree *st,
					bool backshifted)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	st = __bfq_entity_update_weight_prio(st, entity);
	bfq_calc_finish(entity, entity->budget);

	/*
	 * If some queues enjoy backshifting for a while, then their
	 * (virtual) finish timestamps may happen to become lower and
	 * lower than the system virtual time.	In particular, if
	 * these queues often happen to be idle for short time
	 * periods, and during such time periods other queues with
	 * higher timestamps happen to be busy, then the backshifted
	 * timestamps of the former queues can become much lower than
	 * the system virtual time. In fact, to serve the queues with
	 * higher timestamps while the ones with lower timestamps are
	 * idle, the system virtual time may be pushed-up to much
	 * higher values than the finish timestamps of the idle
	 * queues. As a consequence, the finish timestamps of all new
	 * or newly activated queues may end up being much larger than
	 * those of lucky queues with backshifted timestamps. The
	 * latter queues may then monopolize the device for a lot of
	 * time. This would simply break service guarantees.
	 *
	 * To reduce this problem, push up a little bit the
	 * backshifted timestamps of the queue associated with this
	 * entity (only a queue can happen to have the backshifted
	 * flag set): just enough to let the finish timestamp of the
	 * queue be equal to the current value of the system virtual
	 * time. This may introduce a little unfairness among queues
	 * with backshifted timestamps, but it does not break
	 * worst-case fairness guarantees.
	 *
	 * As a special case, if bfqq is weight-raised, push up
	 * timestamps much less, to keep very low the probability that
	 * this push up causes the backshifted finish timestamps of
	 * weight-raised queues to become higher than the backshifted
	 * finish timestamps of non weight-raised queues.
	 */
	if (backshifted && bfq_gt(st->vtime, entity->finish)) {
		unsigned long delta = st->vtime - entity->finish;

		if (bfqq)
			delta /= bfqq->wr_coeff;

		entity->start += delta;
		entity->finish += delta;
	}

	bfq_active_insert(st, entity);
}

/**
 * __bfq_activate_entity - handle activation of entity.
 * @entity: the entity being activated.
 * @non_blocking_wait_rq: true if entity was waiting for a request
 *
 * Called for a 'true' activation, i.e., if entity is not active and
 * one of its children receives a new request.
 *
 * Basically, this function updates the timestamps of entity and
 * inserts entity into its active tree, ater possible extracting it
 * from its idle tree.
 */
static void __bfq_activate_entity(struct bfq_entity *entity,
				  bool non_blocking_wait_rq)
{
	struct bfq_service_tree *st = bfq_entity_service_tree(entity);
	bool backshifted = false;
	unsigned long long min_vstart;

	/* See comments on bfq_fqq_update_budg_for_activation */
	if (non_blocking_wait_rq && bfq_gt(st->vtime, entity->finish)) {
		backshifted = true;
		min_vstart = entity->finish;
	} else
		min_vstart = st->vtime;

	if (entity->tree == &st->idle) {
		/*
		 * Must be on the idle tree, bfq_idle_extract() will
		 * check for that.
		 */
		bfq_idle_extract(st, entity);
		entity->start = bfq_gt(min_vstart, entity->finish) ?
			min_vstart : entity->finish;
	} else {
		/*
		 * The finish time of the entity may be invalid, and
		 * it is in the past for sure, otherwise the queue
		 * would have been on the idle tree.
		 */
		entity->start = min_vstart;
		st->wsum += entity->weight;
		/*
		 * entity is about to be inserted into a service tree,
		 * and then set in service: get a reference to make
		 * sure entity does not disappear until it is no
		 * longer in service or scheduled for service.
		 */
		bfq_get_entity(entity);

		entity->on_st = true;
	}

	bfq_update_fin_time_enqueue(entity, st, backshifted);
}

/**
 * __bfq_requeue_entity - handle requeueing or repositioning of an entity.
 * @entity: the entity being requeued or repositioned.
 *
 * Requeueing is needed if this entity stops being served, which
 * happens if a leaf descendant entity has expired. On the other hand,
 * repositioning is needed if the next_inservice_entity for the child
 * entity has changed. See the comments inside the function for
 * details.
 *
 * Basically, this function: 1) removes entity from its active tree if
 * present there, 2) updates the timestamps of entity and 3) inserts
 * entity back into its active tree (in the new, right position for
 * the new values of the timestamps).
 */
static void __bfq_requeue_entity(struct bfq_entity *entity)
{
	struct bfq_sched_data *sd = entity->sched_data;
	struct bfq_service_tree *st = bfq_entity_service_tree(entity);

	if (entity == sd->in_service_entity) {
		/*
		 * We are requeueing the current in-service entity,
		 * which may have to be done for one of the following
		 * reasons:
		 * - entity represents the in-service queue, and the
		 *   in-service queue is being requeued after an
		 *   expiration;
		 * - entity represents a group, and its budget has
		 *   changed because one of its child entities has
		 *   just been either activated or requeued for some
		 *   reason; the timestamps of the entity need then to
		 *   be updated, and the entity needs to be enqueued
		 *   or repositioned accordingly.
		 *
		 * In particular, before requeueing, the start time of
		 * the entity must be moved forward to account for the
		 * service that the entity has received while in
		 * service. This is done by the next instructions. The
		 * finish time will then be updated according to this
		 * new value of the start time, and to the budget of
		 * the entity.
		 */
		bfq_calc_finish(entity, entity->service);
		entity->start = entity->finish;
		/*
		 * In addition, if the entity had more than one child
		 * when set in service, then was not extracted from
		 * the active tree. This implies that the position of
		 * the entity in the active tree may need to be
		 * changed now, because we have just updated the start
		 * time of the entity, and we will update its finish
		 * time in a moment (the requeueing is then, more
		 * precisely, a repositioning in this case). To
		 * implement this repositioning, we: 1) dequeue the
		 * entity here, 2) update the finish time and
		 * requeue the entity according to the new
		 * timestamps below.
		 */
		if (entity->tree)
			bfq_active_extract(st, entity);
	} else { /* The entity is already active, and not in service */
		/*
		 * In this case, this function gets called only if the
		 * next_in_service entity below this entity has
		 * changed, and this change has caused the budget of
		 * this entity to change, which, finally implies that
		 * the finish time of this entity must be
		 * updated. Such an update may cause the scheduling,
		 * i.e., the position in the active tree, of this
		 * entity to change. We handle this change by: 1)
		 * dequeueing the entity here, 2) updating the finish
		 * time and requeueing the entity according to the new
		 * timestamps below. This is the same approach as the
		 * non-extracted-entity sub-case above.
		 */
		bfq_active_extract(st, entity);
	}

	bfq_update_fin_time_enqueue(entity, st, false);
}

static void __bfq_activate_requeue_entity(struct bfq_entity *entity,
					  struct bfq_sched_data *sd,
					  bool non_blocking_wait_rq)
{
	struct bfq_service_tree *st = bfq_entity_service_tree(entity);

	if (sd->in_service_entity == entity || entity->tree == &st->active)
		 /*
		  * in service or already queued on the active tree,
		  * requeue or reposition
		  */
		__bfq_requeue_entity(entity);
	else
		/*
		 * Not in service and not queued on its active tree:
		 * the activity is idle and this is a true activation.
		 */
		__bfq_activate_entity(entity, non_blocking_wait_rq);
}


/**
 * bfq_activate_entity - activate or requeue an entity representing a bfq_queue,
 *			 and activate, requeue or reposition all ancestors
 *			 for which such an update becomes necessary.
 * @entity: the entity to activate.
 * @non_blocking_wait_rq: true if this entity was waiting for a request
 * @requeue: true if this is a requeue, which implies that bfqq is
 *	     being expired; thus ALL its ancestors stop being served and must
 *	     therefore be requeued
 */
static void bfq_activate_requeue_entity(struct bfq_entity *entity,
					bool non_blocking_wait_rq,
					bool requeue)
{
	struct bfq_sched_data *sd;

	for_each_entity(entity) {
		sd = entity->sched_data;
		__bfq_activate_requeue_entity(entity, sd, non_blocking_wait_rq);

		if (!bfq_update_next_in_service(sd, entity) && !requeue)
			break;
	}
}

/**
 * __bfq_deactivate_entity - deactivate an entity from its service tree.
 * @entity: the entity to deactivate.
 * @ins_into_idle_tree: if false, the entity will not be put into the
 *			idle tree.
 *
 * Deactivates an entity, independently from its previous state.  Must
 * be invoked only if entity is on a service tree. Extracts the entity
 * from that tree, and if necessary and allowed, puts it on the idle
 * tree.
 */
static bool __bfq_deactivate_entity(struct bfq_entity *entity,
				    bool ins_into_idle_tree)
{
	struct bfq_sched_data *sd = entity->sched_data;
	struct bfq_service_tree *st = bfq_entity_service_tree(entity);
	int is_in_service = entity == sd->in_service_entity;

	if (!entity->on_st) /* entity never activated, or already inactive */
		return false;

	if (is_in_service)
		bfq_calc_finish(entity, entity->service);

	if (entity->tree == &st->active)
		bfq_active_extract(st, entity);
	else if (!is_in_service && entity->tree == &st->idle)
		bfq_idle_extract(st, entity);

	if (!ins_into_idle_tree || !bfq_gt(entity->finish, st->vtime))
		bfq_forget_entity(st, entity, is_in_service);
	else
		bfq_idle_insert(st, entity);

	return true;
}

/**
 * bfq_deactivate_entity - deactivate an entity representing a bfq_queue.
 * @entity: the entity to deactivate.
 * @ins_into_idle_tree: true if the entity can be put on the idle tree
 */
static void bfq_deactivate_entity(struct bfq_entity *entity,
				  bool ins_into_idle_tree,
				  bool expiration)
{
	struct bfq_sched_data *sd;
	struct bfq_entity *parent = NULL;

	for_each_entity_safe(entity, parent) {
		sd = entity->sched_data;

		if (!__bfq_deactivate_entity(entity, ins_into_idle_tree)) {
			/*
			 * entity is not in any tree any more, so
			 * this deactivation is a no-op, and there is
			 * nothing to change for upper-level entities
			 * (in case of expiration, this can never
			 * happen).
			 */
			return;
		}

		if (sd->next_in_service == entity)
			/*
			 * entity was the next_in_service entity,
			 * then, since entity has just been
			 * deactivated, a new one must be found.
			 */
			bfq_update_next_in_service(sd, NULL);

		if (sd->next_in_service)
			/*
			 * The parent entity is still backlogged,
			 * because next_in_service is not NULL. So, no
			 * further upwards deactivation must be
			 * performed.  Yet, next_in_service has
			 * changed.  Then the schedule does need to be
			 * updated upwards.
			 */
			break;

		/*
		 * If we get here, then the parent is no more
		 * backlogged and we need to propagate the
		 * deactivation upwards. Thus let the loop go on.
		 */

		/*
		 * Also let parent be queued into the idle tree on
		 * deactivation, to preserve service guarantees, and
		 * assuming that who invoked this function does not
		 * need parent entities too to be removed completely.
		 */
		ins_into_idle_tree = true;
	}

	/*
	 * If the deactivation loop is fully executed, then there are
	 * no more entities to touch and next loop is not executed at
	 * all. Otherwise, requeue remaining entities if they are
	 * about to stop receiving service, or reposition them if this
	 * is not the case.
	 */
	entity = parent;
	for_each_entity(entity) {
		/*
		 * Invoke __bfq_requeue_entity on entity, even if
		 * already active, to requeue/reposition it in the
		 * active tree (because sd->next_in_service has
		 * changed)
		 */
		__bfq_requeue_entity(entity);

		sd = entity->sched_data;
		if (!bfq_update_next_in_service(sd, entity) &&
		    !expiration)
			/*
			 * next_in_service unchanged or not causing
			 * any change in entity->parent->sd, and no
			 * requeueing needed for expiration: stop
			 * here.
			 */
			break;
	}
}

/**
 * bfq_calc_vtime_jump - compute the value to which the vtime should jump,
 *                       if needed, to have at least one entity eligible.
 * @st: the service tree to act upon.
 *
 * Assumes that st is not empty.
 */
static u64 bfq_calc_vtime_jump(struct bfq_service_tree *st)
{
	struct bfq_entity *root_entity = bfq_root_active_entity(&st->active);

	if (bfq_gt(root_entity->min_start, st->vtime))
		return root_entity->min_start;

	return st->vtime;
}

static void bfq_update_vtime(struct bfq_service_tree *st, u64 new_value)
{
	if (new_value > st->vtime) {
		st->vtime = new_value;
		bfq_forget_idle(st);
	}
}

/**
 * bfq_first_active_entity - find the eligible entity with
 *                           the smallest finish time
 * @st: the service tree to select from.
 * @vtime: the system virtual to use as a reference for eligibility
 *
 * This function searches the first schedulable entity, starting from the
 * root of the tree and going on the left every time on this side there is
 * a subtree with at least one eligible (start >= vtime) entity. The path on
 * the right is followed only if a) the left subtree contains no eligible
 * entities and b) no eligible entity has been found yet.
 */
static struct bfq_entity *bfq_first_active_entity(struct bfq_service_tree *st,
						  u64 vtime)
{
	struct bfq_entity *entry, *first = NULL;
	struct rb_node *node = st->active.rb_node;

	while (node) {
		entry = rb_entry(node, struct bfq_entity, rb_node);
left:
		if (!bfq_gt(entry->start, vtime))
			first = entry;

		if (node->rb_left) {
			entry = rb_entry(node->rb_left,
					 struct bfq_entity, rb_node);
			if (!bfq_gt(entry->min_start, vtime)) {
				node = node->rb_left;
				goto left;
			}
		}
		if (first)
			break;
		node = node->rb_right;
	}

	return first;
}

/**
 * __bfq_lookup_next_entity - return the first eligible entity in @st.
 * @st: the service tree.
 *
 * If there is no in-service entity for the sched_data st belongs to,
 * then return the entity that will be set in service if:
 * 1) the parent entity this st belongs to is set in service;
 * 2) no entity belonging to such parent entity undergoes a state change
 * that would influence the timestamps of the entity (e.g., becomes idle,
 * becomes backlogged, changes its budget, ...).
 *
 * In this first case, update the virtual time in @st too (see the
 * comments on this update inside the function).
 *
 * In constrast, if there is an in-service entity, then return the
 * entity that would be set in service if not only the above
 * conditions, but also the next one held true: the currently
 * in-service entity, on expiration,
 * 1) gets a finish time equal to the current one, or
 * 2) is not eligible any more, or
 * 3) is idle.
 */
static struct bfq_entity *
__bfq_lookup_next_entity(struct bfq_service_tree *st, bool in_service)
{
	struct bfq_entity *entity;
	u64 new_vtime;

	if (RB_EMPTY_ROOT(&st->active))
		return NULL;

	/*
	 * Get the value of the system virtual time for which at
	 * least one entity is eligible.
	 */
	new_vtime = bfq_calc_vtime_jump(st);

	/*
	 * If there is no in-service entity for the sched_data this
	 * active tree belongs to, then push the system virtual time
	 * up to the value that guarantees that at least one entity is
	 * eligible. If, instead, there is an in-service entity, then
	 * do not make any such update, because there is already an
	 * eligible entity, namely the in-service one (even if the
	 * entity is not on st, because it was extracted when set in
	 * service).
	 */
	if (!in_service)
		bfq_update_vtime(st, new_vtime);

	entity = bfq_first_active_entity(st, new_vtime);

	return entity;
}

/**
 * bfq_lookup_next_entity - return the first eligible entity in @sd.
 * @sd: the sched_data.
 *
 * This function is invoked when there has been a change in the trees
 * for sd, and we need know what is the new next entity after this
 * change.
 */
static struct bfq_entity *bfq_lookup_next_entity(struct bfq_sched_data *sd)
{
	struct bfq_service_tree *st = sd->service_tree;
	struct bfq_service_tree *idle_class_st = st + (BFQ_IOPRIO_CLASSES - 1);
	struct bfq_entity *entity = NULL;
	int class_idx = 0;

	/*
	 * Choose from idle class, if needed to guarantee a minimum
	 * bandwidth to this class (and if there is some active entity
	 * in idle class). This should also mitigate
	 * priority-inversion problems in case a low priority task is
	 * holding file system resources.
	 */
	if (time_is_before_jiffies(sd->bfq_class_idle_last_service +
				   BFQ_CL_IDLE_TIMEOUT)) {
		if (!RB_EMPTY_ROOT(&idle_class_st->active))
			class_idx = BFQ_IOPRIO_CLASSES - 1;
		/* About to be served if backlogged, or not yet backlogged */
		sd->bfq_class_idle_last_service = jiffies;
	}

	/*
	 * Find the next entity to serve for the highest-priority
	 * class, unless the idle class needs to be served.
	 */
	for (; class_idx < BFQ_IOPRIO_CLASSES; class_idx++) {
		entity = __bfq_lookup_next_entity(st + class_idx,
						  sd->in_service_entity);

		if (entity)
			break;
	}

	if (!entity)
		return NULL;

	return entity;
}

static bool next_queue_may_preempt(struct bfq_data *bfqd)
{
	struct bfq_sched_data *sd = &bfqd->root_group->sched_data;

	return sd->next_in_service != sd->in_service_entity;
}

/*
 * Get next queue for service.
 */
static struct bfq_queue *bfq_get_next_queue(struct bfq_data *bfqd)
{
	struct bfq_entity *entity = NULL;
	struct bfq_sched_data *sd;
	struct bfq_queue *bfqq;

	if (bfqd->busy_queues == 0)
		return NULL;

	/*
	 * Traverse the path from the root to the leaf entity to
	 * serve. Set in service all the entities visited along the
	 * way.
	 */
	sd = &bfqd->root_group->sched_data;
	for (; sd ; sd = entity->my_sched_data) {
		/*
		 * WARNING. We are about to set the in-service entity
		 * to sd->next_in_service, i.e., to the (cached) value
		 * returned by bfq_lookup_next_entity(sd) the last
		 * time it was invoked, i.e., the last time when the
		 * service order in sd changed as a consequence of the
		 * activation or deactivation of an entity. In this
		 * respect, if we execute bfq_lookup_next_entity(sd)
		 * in this very moment, it may, although with low
		 * probability, yield a different entity than that
		 * pointed to by sd->next_in_service. This rare event
		 * happens in case there was no CLASS_IDLE entity to
		 * serve for sd when bfq_lookup_next_entity(sd) was
		 * invoked for the last time, while there is now one
		 * such entity.
		 *
		 * If the above event happens, then the scheduling of
		 * such entity in CLASS_IDLE is postponed until the
		 * service of the sd->next_in_service entity
		 * finishes. In fact, when the latter is expired,
		 * bfq_lookup_next_entity(sd) gets called again,
		 * exactly to update sd->next_in_service.
		 */

		/* Make next_in_service entity become in_service_entity */
		entity = sd->next_in_service;
		sd->in_service_entity = entity;

		/*
		 * Reset the accumulator of the amount of service that
		 * the entity is about to receive.
		 */
		entity->service = 0;

		/*
		 * If entity is no longer a candidate for next
		 * service, then we extract it from its active tree,
		 * for the following reason. To further boost the
		 * throughput in some special case, BFQ needs to know
		 * which is the next candidate entity to serve, while
		 * there is already an entity in service. In this
		 * respect, to make it easy to compute/update the next
		 * candidate entity to serve after the current
		 * candidate has been set in service, there is a case
		 * where it is necessary to extract the current
		 * candidate from its service tree. Such a case is
		 * when the entity just set in service cannot be also
		 * a candidate for next service. Details about when
		 * this conditions holds are reported in the comments
		 * on the function bfq_no_longer_next_in_service()
		 * invoked below.
		 */
		if (bfq_no_longer_next_in_service(entity))
			bfq_active_extract(bfq_entity_service_tree(entity),
					   entity);

		/*
		 * For the same reason why we may have just extracted
		 * entity from its active tree, we may need to update
		 * next_in_service for the sched_data of entity too,
		 * regardless of whether entity has been extracted.
		 * In fact, even if entity has not been extracted, a
		 * descendant entity may get extracted. Such an event
		 * would cause a change in next_in_service for the
		 * level of the descendant entity, and thus possibly
		 * back to upper levels.
		 *
		 * We cannot perform the resulting needed update
		 * before the end of this loop, because, to know which
		 * is the correct next-to-serve candidate entity for
		 * each level, we need first to find the leaf entity
		 * to set in service. In fact, only after we know
		 * which is the next-to-serve leaf entity, we can
		 * discover whether the parent entity of the leaf
		 * entity becomes the next-to-serve, and so on.
		 */

	}

	bfqq = bfq_entity_to_bfqq(entity);

	/*
	 * We can finally update all next-to-serve entities along the
	 * path from the leaf entity just set in service to the root.
	 */
	for_each_entity(entity) {
		struct bfq_sched_data *sd = entity->sched_data;

		if (!bfq_update_next_in_service(sd, NULL))
			break;
	}

	return bfqq;
}

static void __bfq_bfqd_reset_in_service(struct bfq_data *bfqd)
{
	struct bfq_queue *in_serv_bfqq = bfqd->in_service_queue;
	struct bfq_entity *in_serv_entity = &in_serv_bfqq->entity;
	struct bfq_entity *entity = in_serv_entity;

	bfq_clear_bfqq_wait_request(in_serv_bfqq);
	hrtimer_try_to_cancel(&bfqd->idle_slice_timer);
	bfqd->in_service_queue = NULL;

	/*
	 * When this function is called, all in-service entities have
	 * been properly deactivated or requeued, so we can safely
	 * execute the final step: reset in_service_entity along the
	 * path from entity to the root.
	 */
	for_each_entity(entity)
		entity->sched_data->in_service_entity = NULL;

	/*
	 * in_serv_entity is no longer in service, so, if it is in no
	 * service tree either, then release the service reference to
	 * the queue it represents (taken with bfq_get_entity).
	 */
	if (!in_serv_entity->on_st)
		bfq_put_queue(in_serv_bfqq);
}

static void bfq_deactivate_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq,
				bool ins_into_idle_tree, bool expiration)
{
	struct bfq_entity *entity = &bfqq->entity;

	bfq_deactivate_entity(entity, ins_into_idle_tree, expiration);
}

static void bfq_activate_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;

	bfq_activate_requeue_entity(entity, bfq_bfqq_non_blocking_wait_rq(bfqq),
				    false);
	bfq_clear_bfqq_non_blocking_wait_rq(bfqq);
}

static void bfq_requeue_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;

	bfq_activate_requeue_entity(entity, false,
				    bfqq == bfqd->in_service_queue);
}

static void bfqg_stats_update_dequeue(struct bfq_group *bfqg);

/*
 * Called when the bfqq no longer has requests pending, remove it from
 * the service tree. As a special case, it can be invoked during an
 * expiration.
 */
static void bfq_del_bfqq_busy(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			      bool expiration)
{
	bfq_log_bfqq(bfqd, bfqq, "del from busy");

	bfq_clear_bfqq_busy(bfqq);

	bfqd->busy_queues--;

	if (!bfqq->dispatched)
		bfq_weights_tree_remove(bfqd, &bfqq->entity,
					&bfqd->queue_weights_tree);

	if (bfqq->wr_coeff > 1)
		bfqd->wr_busy_queues--;

	bfqg_stats_update_dequeue(bfqq_group(bfqq));

	bfq_deactivate_bfqq(bfqd, bfqq, true, expiration);
}

/*
 * Called when an inactive queue receives a new request.
 */
static void bfq_add_bfqq_busy(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	bfq_log_bfqq(bfqd, bfqq, "add to busy");

	bfq_activate_bfqq(bfqd, bfqq);

	bfq_mark_bfqq_busy(bfqq);
	bfqd->busy_queues++;

	if (!bfqq->dispatched)
		if (bfqq->wr_coeff == 1)
			bfq_weights_tree_add(bfqd, &bfqq->entity,
					     &bfqd->queue_weights_tree);

	if (bfqq->wr_coeff > 1)
		bfqd->wr_busy_queues++;
}

#ifdef CONFIG_BFQ_GROUP_IOSCHED

/* bfqg stats flags */
enum bfqg_stats_flags {
	BFQG_stats_waiting = 0,
	BFQG_stats_idling,
	BFQG_stats_empty,
};

#define BFQG_FLAG_FNS(name)						\
static void bfqg_stats_mark_##name(struct bfqg_stats *stats)	\
{									\
	stats->flags |= (1 << BFQG_stats_##name);			\
}									\
static void bfqg_stats_clear_##name(struct bfqg_stats *stats)	\
{									\
	stats->flags &= ~(1 << BFQG_stats_##name);			\
}									\
static int bfqg_stats_##name(struct bfqg_stats *stats)		\
{									\
	return (stats->flags & (1 << BFQG_stats_##name)) != 0;		\
}									\

BFQG_FLAG_FNS(waiting)
BFQG_FLAG_FNS(idling)
BFQG_FLAG_FNS(empty)
#undef BFQG_FLAG_FNS

/* This should be called with the queue_lock held. */
static void bfqg_stats_update_group_wait_time(struct bfqg_stats *stats)
{
	unsigned long long now;

	if (!bfqg_stats_waiting(stats))
		return;

	now = sched_clock();
	if (time_after64(now, stats->start_group_wait_time))
		blkg_stat_add(&stats->group_wait_time,
			      now - stats->start_group_wait_time);
	bfqg_stats_clear_waiting(stats);
}

/* This should be called with the queue_lock held. */
static void bfqg_stats_set_start_group_wait_time(struct bfq_group *bfqg,
						 struct bfq_group *curr_bfqg)
{
	struct bfqg_stats *stats = &bfqg->stats;

	if (bfqg_stats_waiting(stats))
		return;
	if (bfqg == curr_bfqg)
		return;
	stats->start_group_wait_time = sched_clock();
	bfqg_stats_mark_waiting(stats);
}

/* This should be called with the queue_lock held. */
static void bfqg_stats_end_empty_time(struct bfqg_stats *stats)
{
	unsigned long long now;

	if (!bfqg_stats_empty(stats))
		return;

	now = sched_clock();
	if (time_after64(now, stats->start_empty_time))
		blkg_stat_add(&stats->empty_time,
			      now - stats->start_empty_time);
	bfqg_stats_clear_empty(stats);
}

static void bfqg_stats_update_dequeue(struct bfq_group *bfqg)
{
	blkg_stat_add(&bfqg->stats.dequeue, 1);
}

static void bfqg_stats_set_start_empty_time(struct bfq_group *bfqg)
{
	struct bfqg_stats *stats = &bfqg->stats;

	if (blkg_rwstat_total(&stats->queued))
		return;

	/*
	 * group is already marked empty. This can happen if bfqq got new
	 * request in parent group and moved to this group while being added
	 * to service tree. Just ignore the event and move on.
	 */
	if (bfqg_stats_empty(stats))
		return;

	stats->start_empty_time = sched_clock();
	bfqg_stats_mark_empty(stats);
}

static void bfqg_stats_update_idle_time(struct bfq_group *bfqg)
{
	struct bfqg_stats *stats = &bfqg->stats;

	if (bfqg_stats_idling(stats)) {
		unsigned long long now = sched_clock();

		if (time_after64(now, stats->start_idle_time))
			blkg_stat_add(&stats->idle_time,
				      now - stats->start_idle_time);
		bfqg_stats_clear_idling(stats);
	}
}

static void bfqg_stats_set_start_idle_time(struct bfq_group *bfqg)
{
	struct bfqg_stats *stats = &bfqg->stats;

	stats->start_idle_time = sched_clock();
	bfqg_stats_mark_idling(stats);
}

static void bfqg_stats_update_avg_queue_size(struct bfq_group *bfqg)
{
	struct bfqg_stats *stats = &bfqg->stats;

	blkg_stat_add(&stats->avg_queue_size_sum,
		      blkg_rwstat_total(&stats->queued));
	blkg_stat_add(&stats->avg_queue_size_samples, 1);
	bfqg_stats_update_group_wait_time(stats);
}

/*
 * blk-cgroup policy-related handlers
 * The following functions help in converting between blk-cgroup
 * internal structures and BFQ-specific structures.
 */

static struct bfq_group *pd_to_bfqg(struct blkg_policy_data *pd)
{
	return pd ? container_of(pd, struct bfq_group, pd) : NULL;
}

static struct blkcg_gq *bfqg_to_blkg(struct bfq_group *bfqg)
{
	return pd_to_blkg(&bfqg->pd);
}

static struct blkcg_policy blkcg_policy_bfq;

static struct bfq_group *blkg_to_bfqg(struct blkcg_gq *blkg)
{
	return pd_to_bfqg(blkg_to_pd(blkg, &blkcg_policy_bfq));
}

/*
 * bfq_group handlers
 * The following functions help in navigating the bfq_group hierarchy
 * by allowing to find the parent of a bfq_group or the bfq_group
 * associated to a bfq_queue.
 */

static struct bfq_group *bfqg_parent(struct bfq_group *bfqg)
{
	struct blkcg_gq *pblkg = bfqg_to_blkg(bfqg)->parent;

	return pblkg ? blkg_to_bfqg(pblkg) : NULL;
}

static struct bfq_group *bfqq_group(struct bfq_queue *bfqq)
{
	struct bfq_entity *group_entity = bfqq->entity.parent;

	return group_entity ? container_of(group_entity, struct bfq_group,
					   entity) :
			      bfqq->bfqd->root_group;
}

/*
 * The following two functions handle get and put of a bfq_group by
 * wrapping the related blk-cgroup hooks.
 */

static void bfqg_get(struct bfq_group *bfqg)
{
	return blkg_get(bfqg_to_blkg(bfqg));
}

static void bfqg_put(struct bfq_group *bfqg)
{
	return blkg_put(bfqg_to_blkg(bfqg));
}

static void bfqg_stats_update_io_add(struct bfq_group *bfqg,
				     struct bfq_queue *bfqq,
				     unsigned int op)
{
	blkg_rwstat_add(&bfqg->stats.queued, op, 1);
	bfqg_stats_end_empty_time(&bfqg->stats);
	if (!(bfqq == ((struct bfq_data *)bfqg->bfqd)->in_service_queue))
		bfqg_stats_set_start_group_wait_time(bfqg, bfqq_group(bfqq));
}

static void bfqg_stats_update_io_remove(struct bfq_group *bfqg, unsigned int op)
{
	blkg_rwstat_add(&bfqg->stats.queued, op, -1);
}

static void bfqg_stats_update_io_merged(struct bfq_group *bfqg, unsigned int op)
{
	blkg_rwstat_add(&bfqg->stats.merged, op, 1);
}

static void bfqg_stats_update_completion(struct bfq_group *bfqg,
			uint64_t start_time, uint64_t io_start_time,
			unsigned int op)
{
	struct bfqg_stats *stats = &bfqg->stats;
	unsigned long long now = sched_clock();

	if (time_after64(now, io_start_time))
		blkg_rwstat_add(&stats->service_time, op,
				now - io_start_time);
	if (time_after64(io_start_time, start_time))
		blkg_rwstat_add(&stats->wait_time, op,
				io_start_time - start_time);
}

/* @stats = 0 */
static void bfqg_stats_reset(struct bfqg_stats *stats)
{
	/* queued stats shouldn't be cleared */
	blkg_rwstat_reset(&stats->merged);
	blkg_rwstat_reset(&stats->service_time);
	blkg_rwstat_reset(&stats->wait_time);
	blkg_stat_reset(&stats->time);
	blkg_stat_reset(&stats->avg_queue_size_sum);
	blkg_stat_reset(&stats->avg_queue_size_samples);
	blkg_stat_reset(&stats->dequeue);
	blkg_stat_reset(&stats->group_wait_time);
	blkg_stat_reset(&stats->idle_time);
	blkg_stat_reset(&stats->empty_time);
}

/* @to += @from */
static void bfqg_stats_add_aux(struct bfqg_stats *to, struct bfqg_stats *from)
{
	if (!to || !from)
		return;

	/* queued stats shouldn't be cleared */
	blkg_rwstat_add_aux(&to->merged, &from->merged);
	blkg_rwstat_add_aux(&to->service_time, &from->service_time);
	blkg_rwstat_add_aux(&to->wait_time, &from->wait_time);
	blkg_stat_add_aux(&from->time, &from->time);
	blkg_stat_add_aux(&to->avg_queue_size_sum, &from->avg_queue_size_sum);
	blkg_stat_add_aux(&to->avg_queue_size_samples,
			  &from->avg_queue_size_samples);
	blkg_stat_add_aux(&to->dequeue, &from->dequeue);
	blkg_stat_add_aux(&to->group_wait_time, &from->group_wait_time);
	blkg_stat_add_aux(&to->idle_time, &from->idle_time);
	blkg_stat_add_aux(&to->empty_time, &from->empty_time);
}

/*
 * Transfer @bfqg's stats to its parent's aux counts so that the ancestors'
 * recursive stats can still account for the amount used by this bfqg after
 * it's gone.
 */
static void bfqg_stats_xfer_dead(struct bfq_group *bfqg)
{
	struct bfq_group *parent;

	if (!bfqg) /* root_group */
		return;

	parent = bfqg_parent(bfqg);

	lockdep_assert_held(bfqg_to_blkg(bfqg)->q->queue_lock);

	if (unlikely(!parent))
		return;

	bfqg_stats_add_aux(&parent->stats, &bfqg->stats);
	bfqg_stats_reset(&bfqg->stats);
}

static void bfq_init_entity(struct bfq_entity *entity,
			    struct bfq_group *bfqg)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	entity->weight = entity->new_weight;
	entity->orig_weight = entity->new_weight;
	if (bfqq) {
		bfqq->ioprio = bfqq->new_ioprio;
		bfqq->ioprio_class = bfqq->new_ioprio_class;
		bfqg_get(bfqg);
	}
	entity->parent = bfqg->my_entity; /* NULL for root group */
	entity->sched_data = &bfqg->sched_data;
}

static void bfqg_stats_exit(struct bfqg_stats *stats)
{
	blkg_rwstat_exit(&stats->merged);
	blkg_rwstat_exit(&stats->service_time);
	blkg_rwstat_exit(&stats->wait_time);
	blkg_rwstat_exit(&stats->queued);
	blkg_stat_exit(&stats->time);
	blkg_stat_exit(&stats->avg_queue_size_sum);
	blkg_stat_exit(&stats->avg_queue_size_samples);
	blkg_stat_exit(&stats->dequeue);
	blkg_stat_exit(&stats->group_wait_time);
	blkg_stat_exit(&stats->idle_time);
	blkg_stat_exit(&stats->empty_time);
}

static int bfqg_stats_init(struct bfqg_stats *stats, gfp_t gfp)
{
	if (blkg_rwstat_init(&stats->merged, gfp) ||
	    blkg_rwstat_init(&stats->service_time, gfp) ||
	    blkg_rwstat_init(&stats->wait_time, gfp) ||
	    blkg_rwstat_init(&stats->queued, gfp) ||
	    blkg_stat_init(&stats->time, gfp) ||
	    blkg_stat_init(&stats->avg_queue_size_sum, gfp) ||
	    blkg_stat_init(&stats->avg_queue_size_samples, gfp) ||
	    blkg_stat_init(&stats->dequeue, gfp) ||
	    blkg_stat_init(&stats->group_wait_time, gfp) ||
	    blkg_stat_init(&stats->idle_time, gfp) ||
	    blkg_stat_init(&stats->empty_time, gfp)) {
		bfqg_stats_exit(stats);
		return -ENOMEM;
	}

	return 0;
}

static struct bfq_group_data *cpd_to_bfqgd(struct blkcg_policy_data *cpd)
{
	return cpd ? container_of(cpd, struct bfq_group_data, pd) : NULL;
}

static struct bfq_group_data *blkcg_to_bfqgd(struct blkcg *blkcg)
{
	return cpd_to_bfqgd(blkcg_to_cpd(blkcg, &blkcg_policy_bfq));
}

static struct blkcg_policy_data *bfq_cpd_alloc(gfp_t gfp)
{
	struct bfq_group_data *bgd;

	bgd = kzalloc(sizeof(*bgd), gfp);
	if (!bgd)
		return NULL;
	return &bgd->pd;
}

static void bfq_cpd_init(struct blkcg_policy_data *cpd)
{
	struct bfq_group_data *d = cpd_to_bfqgd(cpd);

	d->weight = cgroup_subsys_on_dfl(io_cgrp_subsys) ?
		CGROUP_WEIGHT_DFL : BFQ_WEIGHT_LEGACY_DFL;
}

static void bfq_cpd_free(struct blkcg_policy_data *cpd)
{
	kfree(cpd_to_bfqgd(cpd));
}

static struct blkg_policy_data *bfq_pd_alloc(gfp_t gfp, int node)
{
	struct bfq_group *bfqg;

	bfqg = kzalloc_node(sizeof(*bfqg), gfp, node);
	if (!bfqg)
		return NULL;

	if (bfqg_stats_init(&bfqg->stats, gfp)) {
		kfree(bfqg);
		return NULL;
	}

	return &bfqg->pd;
}

static void bfq_pd_init(struct blkg_policy_data *pd)
{
	struct blkcg_gq *blkg = pd_to_blkg(pd);
	struct bfq_group *bfqg = blkg_to_bfqg(blkg);
	struct bfq_data *bfqd = blkg->q->elevator->elevator_data;
	struct bfq_entity *entity = &bfqg->entity;
	struct bfq_group_data *d = blkcg_to_bfqgd(blkg->blkcg);

	entity->orig_weight = entity->weight = entity->new_weight = d->weight;
	entity->my_sched_data = &bfqg->sched_data;
	bfqg->my_entity = entity; /*
				   * the root_group's will be set to NULL
				   * in bfq_init_queue()
				   */
	bfqg->bfqd = bfqd;
	bfqg->active_entities = 0;
	bfqg->rq_pos_tree = RB_ROOT;
}

static void bfq_pd_free(struct blkg_policy_data *pd)
{
	struct bfq_group *bfqg = pd_to_bfqg(pd);

	bfqg_stats_exit(&bfqg->stats);
	return kfree(bfqg);
}

static void bfq_pd_reset_stats(struct blkg_policy_data *pd)
{
	struct bfq_group *bfqg = pd_to_bfqg(pd);

	bfqg_stats_reset(&bfqg->stats);
}

static void bfq_group_set_parent(struct bfq_group *bfqg,
					struct bfq_group *parent)
{
	struct bfq_entity *entity;

	entity = &bfqg->entity;
	entity->parent = parent->my_entity;
	entity->sched_data = &parent->sched_data;
}

static struct bfq_group *bfq_lookup_bfqg(struct bfq_data *bfqd,
					 struct blkcg *blkcg)
{
	struct blkcg_gq *blkg;

	blkg = blkg_lookup(blkcg, bfqd->queue);
	if (likely(blkg))
		return blkg_to_bfqg(blkg);
	return NULL;
}

static struct bfq_group *bfq_find_set_group(struct bfq_data *bfqd,
					    struct blkcg *blkcg)
{
	struct bfq_group *bfqg, *parent;
	struct bfq_entity *entity;

	bfqg = bfq_lookup_bfqg(bfqd, blkcg);

	if (unlikely(!bfqg))
		return NULL;

	/*
	 * Update chain of bfq_groups as we might be handling a leaf group
	 * which, along with some of its relatives, has not been hooked yet
	 * to the private hierarchy of BFQ.
	 */
	entity = &bfqg->entity;
	for_each_entity(entity) {
		bfqg = container_of(entity, struct bfq_group, entity);
		if (bfqg != bfqd->root_group) {
			parent = bfqg_parent(bfqg);
			if (!parent)
				parent = bfqd->root_group;
			bfq_group_set_parent(bfqg, parent);
		}
	}

	return bfqg;
}

static void bfq_pos_tree_add_move(struct bfq_data *bfqd,
				  struct bfq_queue *bfqq);
static void bfq_bfqq_expire(struct bfq_data *bfqd,
			    struct bfq_queue *bfqq,
			    bool compensate,
			    enum bfqq_expiration reason);

/**
 * bfq_bfqq_move - migrate @bfqq to @bfqg.
 * @bfqd: queue descriptor.
 * @bfqq: the queue to move.
 * @bfqg: the group to move to.
 *
 * Move @bfqq to @bfqg, deactivating it from its old group and reactivating
 * it on the new one.  Avoid putting the entity on the old group idle tree.
 *
 * Must be called under the queue lock; the cgroup owning @bfqg must
 * not disappear (by now this just means that we are called under
 * rcu_read_lock()).
 */
static void bfq_bfqq_move(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			  struct bfq_group *bfqg)
{
	struct bfq_entity *entity = &bfqq->entity;

	/* If bfqq is empty, then bfq_bfqq_expire also invokes
	 * bfq_del_bfqq_busy, thereby removing bfqq and its entity
	 * from data structures related to current group. Otherwise we
	 * need to remove bfqq explicitly with bfq_deactivate_bfqq, as
	 * we do below.
	 */
	if (bfqq == bfqd->in_service_queue)
		bfq_bfqq_expire(bfqd, bfqd->in_service_queue,
				false, BFQQE_PREEMPTED);

	if (bfq_bfqq_busy(bfqq))
		bfq_deactivate_bfqq(bfqd, bfqq, false, false);
	else if (entity->on_st)
		bfq_put_idle_entity(bfq_entity_service_tree(entity), entity);
	bfqg_put(bfqq_group(bfqq));

	/*
	 * Here we use a reference to bfqg.  We don't need a refcounter
	 * as the cgroup reference will not be dropped, so that its
	 * destroy() callback will not be invoked.
	 */
	entity->parent = bfqg->my_entity;
	entity->sched_data = &bfqg->sched_data;
	bfqg_get(bfqg);

	if (bfq_bfqq_busy(bfqq)) {
		bfq_pos_tree_add_move(bfqd, bfqq);
		bfq_activate_bfqq(bfqd, bfqq);
	}

	if (!bfqd->in_service_queue && !bfqd->rq_in_driver)
		bfq_schedule_dispatch(bfqd);
}

/**
 * __bfq_bic_change_cgroup - move @bic to @cgroup.
 * @bfqd: the queue descriptor.
 * @bic: the bic to move.
 * @blkcg: the blk-cgroup to move to.
 *
 * Move bic to blkcg, assuming that bfqd->queue is locked; the caller
 * has to make sure that the reference to cgroup is valid across the call.
 *
 * NOTE: an alternative approach might have been to store the current
 * cgroup in bfqq and getting a reference to it, reducing the lookup
 * time here, at the price of slightly more complex code.
 */
static struct bfq_group *__bfq_bic_change_cgroup(struct bfq_data *bfqd,
						struct bfq_io_cq *bic,
						struct blkcg *blkcg)
{
	struct bfq_queue *async_bfqq = bic_to_bfqq(bic, 0);
	struct bfq_queue *sync_bfqq = bic_to_bfqq(bic, 1);
	struct bfq_group *bfqg;
	struct bfq_entity *entity;

	bfqg = bfq_find_set_group(bfqd, blkcg);

	if (unlikely(!bfqg))
		bfqg = bfqd->root_group;

	if (async_bfqq) {
		entity = &async_bfqq->entity;

		if (entity->sched_data != &bfqg->sched_data) {
			bic_set_bfqq(bic, NULL, 0);
			bfq_log_bfqq(bfqd, async_bfqq,
				     "bic_change_group: %p %d",
				     async_bfqq, async_bfqq->ref);
			bfq_put_queue(async_bfqq);
		}
	}

	if (sync_bfqq) {
		entity = &sync_bfqq->entity;
		if (entity->sched_data != &bfqg->sched_data)
			bfq_bfqq_move(bfqd, sync_bfqq, bfqg);
	}

	return bfqg;
}

static void bfq_bic_update_cgroup(struct bfq_io_cq *bic, struct bio *bio)
{
	struct bfq_data *bfqd = bic_to_bfqd(bic);
	struct bfq_group *bfqg = NULL;
	uint64_t serial_nr;

	rcu_read_lock();
	serial_nr = bio_blkcg(bio)->css.serial_nr;

	/*
	 * Check whether blkcg has changed.  The condition may trigger
	 * spuriously on a newly created cic but there's no harm.
	 */
	if (unlikely(!bfqd) || likely(bic->blkcg_serial_nr == serial_nr))
		goto out;

	bfqg = __bfq_bic_change_cgroup(bfqd, bic, bio_blkcg(bio));
	bic->blkcg_serial_nr = serial_nr;
out:
	rcu_read_unlock();
}

/**
 * bfq_flush_idle_tree - deactivate any entity on the idle tree of @st.
 * @st: the service tree being flushed.
 */
static void bfq_flush_idle_tree(struct bfq_service_tree *st)
{
	struct bfq_entity *entity = st->first_idle;

	for (; entity ; entity = st->first_idle)
		__bfq_deactivate_entity(entity, false);
}

/**
 * bfq_reparent_leaf_entity - move leaf entity to the root_group.
 * @bfqd: the device data structure with the root group.
 * @entity: the entity to move.
 */
static void bfq_reparent_leaf_entity(struct bfq_data *bfqd,
				     struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	bfq_bfqq_move(bfqd, bfqq, bfqd->root_group);
}

/**
 * bfq_reparent_active_entities - move to the root group all active
 *                                entities.
 * @bfqd: the device data structure with the root group.
 * @bfqg: the group to move from.
 * @st: the service tree with the entities.
 *
 * Needs queue_lock to be taken and reference to be valid over the call.
 */
static void bfq_reparent_active_entities(struct bfq_data *bfqd,
					 struct bfq_group *bfqg,
					 struct bfq_service_tree *st)
{
	struct rb_root *active = &st->active;
	struct bfq_entity *entity = NULL;

	if (!RB_EMPTY_ROOT(&st->active))
		entity = bfq_entity_of(rb_first(active));

	for (; entity ; entity = bfq_entity_of(rb_first(active)))
		bfq_reparent_leaf_entity(bfqd, entity);

	if (bfqg->sched_data.in_service_entity)
		bfq_reparent_leaf_entity(bfqd,
			bfqg->sched_data.in_service_entity);
}

/**
 * bfq_pd_offline - deactivate the entity associated with @pd,
 *		    and reparent its children entities.
 * @pd: descriptor of the policy going offline.
 *
 * blkio already grabs the queue_lock for us, so no need to use
 * RCU-based magic
 */
static void bfq_pd_offline(struct blkg_policy_data *pd)
{
	struct bfq_service_tree *st;
	struct bfq_group *bfqg = pd_to_bfqg(pd);
	struct bfq_data *bfqd = bfqg->bfqd;
	struct bfq_entity *entity = bfqg->my_entity;
	unsigned long flags;
	int i;

	if (!entity) /* root group */
		return;

	spin_lock_irqsave(&bfqd->lock, flags);
	/*
	 * Empty all service_trees belonging to this group before
	 * deactivating the group itself.
	 */
	for (i = 0; i < BFQ_IOPRIO_CLASSES; i++) {
		st = bfqg->sched_data.service_tree + i;

		/*
		 * The idle tree may still contain bfq_queues belonging
		 * to exited task because they never migrated to a different
		 * cgroup from the one being destroyed now.  No one else
		 * can access them so it's safe to act without any lock.
		 */
		bfq_flush_idle_tree(st);

		/*
		 * It may happen that some queues are still active
		 * (busy) upon group destruction (if the corresponding
		 * processes have been forced to terminate). We move
		 * all the leaf entities corresponding to these queues
		 * to the root_group.
		 * Also, it may happen that the group has an entity
		 * in service, which is disconnected from the active
		 * tree: it must be moved, too.
		 * There is no need to put the sync queues, as the
		 * scheduler has taken no reference.
		 */
		bfq_reparent_active_entities(bfqd, bfqg, st);
	}

	__bfq_deactivate_entity(entity, false);
	bfq_put_async_queues(bfqd, bfqg);

	spin_unlock_irqrestore(&bfqd->lock, flags);
	/*
	 * @blkg is going offline and will be ignored by
	 * blkg_[rw]stat_recursive_sum().  Transfer stats to the parent so
	 * that they don't get lost.  If IOs complete after this point, the
	 * stats for them will be lost.  Oh well...
	 */
	bfqg_stats_xfer_dead(bfqg);
}

static void bfq_end_wr_async(struct bfq_data *bfqd)
{
	struct blkcg_gq *blkg;

	list_for_each_entry(blkg, &bfqd->queue->blkg_list, q_node) {
		struct bfq_group *bfqg = blkg_to_bfqg(blkg);

		bfq_end_wr_async_queues(bfqd, bfqg);
	}
	bfq_end_wr_async_queues(bfqd, bfqd->root_group);
}

static int bfq_io_show_weight(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));
	struct bfq_group_data *bfqgd = blkcg_to_bfqgd(blkcg);
	unsigned int val = 0;

	if (bfqgd)
		val = bfqgd->weight;

	seq_printf(sf, "%u\n", val);

	return 0;
}

static int bfq_io_set_weight_legacy(struct cgroup_subsys_state *css,
				    struct cftype *cftype,
				    u64 val)
{
	struct blkcg *blkcg = css_to_blkcg(css);
	struct bfq_group_data *bfqgd = blkcg_to_bfqgd(blkcg);
	struct blkcg_gq *blkg;
	int ret = -ERANGE;

	if (val < BFQ_MIN_WEIGHT || val > BFQ_MAX_WEIGHT)
		return ret;

	ret = 0;
	spin_lock_irq(&blkcg->lock);
	bfqgd->weight = (unsigned short)val;
	hlist_for_each_entry(blkg, &blkcg->blkg_list, blkcg_node) {
		struct bfq_group *bfqg = blkg_to_bfqg(blkg);

		if (!bfqg)
			continue;
		/*
		 * Setting the prio_changed flag of the entity
		 * to 1 with new_weight == weight would re-set
		 * the value of the weight to its ioprio mapping.
		 * Set the flag only if necessary.
		 */
		if ((unsigned short)val != bfqg->entity.new_weight) {
			bfqg->entity.new_weight = (unsigned short)val;
			/*
			 * Make sure that the above new value has been
			 * stored in bfqg->entity.new_weight before
			 * setting the prio_changed flag. In fact,
			 * this flag may be read asynchronously (in
			 * critical sections protected by a different
			 * lock than that held here), and finding this
			 * flag set may cause the execution of the code
			 * for updating parameters whose value may
			 * depend also on bfqg->entity.new_weight (in
			 * __bfq_entity_update_weight_prio).
			 * This barrier makes sure that the new value
			 * of bfqg->entity.new_weight is correctly
			 * seen in that code.
			 */
			smp_wmb();
			bfqg->entity.prio_changed = 1;
		}
	}
	spin_unlock_irq(&blkcg->lock);

	return ret;
}

static ssize_t bfq_io_set_weight(struct kernfs_open_file *of,
				 char *buf, size_t nbytes,
				 loff_t off)
{
	u64 weight;
	/* First unsigned long found in the file is used */
	int ret = kstrtoull(strim(buf), 0, &weight);

	if (ret)
		return ret;

	return bfq_io_set_weight_legacy(of_css(of), NULL, weight);
}

static int bfqg_print_stat(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)), blkg_prfill_stat,
			  &blkcg_policy_bfq, seq_cft(sf)->private, false);
	return 0;
}

static int bfqg_print_rwstat(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)), blkg_prfill_rwstat,
			  &blkcg_policy_bfq, seq_cft(sf)->private, true);
	return 0;
}

static u64 bfqg_prfill_stat_recursive(struct seq_file *sf,
				      struct blkg_policy_data *pd, int off)
{
	u64 sum = blkg_stat_recursive_sum(pd_to_blkg(pd),
					  &blkcg_policy_bfq, off);
	return __blkg_prfill_u64(sf, pd, sum);
}

static u64 bfqg_prfill_rwstat_recursive(struct seq_file *sf,
					struct blkg_policy_data *pd, int off)
{
	struct blkg_rwstat sum = blkg_rwstat_recursive_sum(pd_to_blkg(pd),
							   &blkcg_policy_bfq,
							   off);
	return __blkg_prfill_rwstat(sf, pd, &sum);
}

static int bfqg_print_stat_recursive(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  bfqg_prfill_stat_recursive, &blkcg_policy_bfq,
			  seq_cft(sf)->private, false);
	return 0;
}

static int bfqg_print_rwstat_recursive(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  bfqg_prfill_rwstat_recursive, &blkcg_policy_bfq,
			  seq_cft(sf)->private, true);
	return 0;
}

static u64 bfqg_prfill_sectors(struct seq_file *sf, struct blkg_policy_data *pd,
			       int off)
{
	u64 sum = blkg_rwstat_total(&pd->blkg->stat_bytes);

	return __blkg_prfill_u64(sf, pd, sum >> 9);
}

static int bfqg_print_stat_sectors(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  bfqg_prfill_sectors, &blkcg_policy_bfq, 0, false);
	return 0;
}

static u64 bfqg_prfill_sectors_recursive(struct seq_file *sf,
					 struct blkg_policy_data *pd, int off)
{
	struct blkg_rwstat tmp = blkg_rwstat_recursive_sum(pd->blkg, NULL,
					offsetof(struct blkcg_gq, stat_bytes));
	u64 sum = atomic64_read(&tmp.aux_cnt[BLKG_RWSTAT_READ]) +
		atomic64_read(&tmp.aux_cnt[BLKG_RWSTAT_WRITE]);

	return __blkg_prfill_u64(sf, pd, sum >> 9);
}

static int bfqg_print_stat_sectors_recursive(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  bfqg_prfill_sectors_recursive, &blkcg_policy_bfq, 0,
			  false);
	return 0;
}

static u64 bfqg_prfill_avg_queue_size(struct seq_file *sf,
				      struct blkg_policy_data *pd, int off)
{
	struct bfq_group *bfqg = pd_to_bfqg(pd);
	u64 samples = blkg_stat_read(&bfqg->stats.avg_queue_size_samples);
	u64 v = 0;

	if (samples) {
		v = blkg_stat_read(&bfqg->stats.avg_queue_size_sum);
		v = div64_u64(v, samples);
	}
	__blkg_prfill_u64(sf, pd, v);
	return 0;
}

/* print avg_queue_size */
static int bfqg_print_avg_queue_size(struct seq_file *sf, void *v)
{
	blkcg_print_blkgs(sf, css_to_blkcg(seq_css(sf)),
			  bfqg_prfill_avg_queue_size, &blkcg_policy_bfq,
			  0, false);
	return 0;
}

static struct bfq_group *
bfq_create_group_hierarchy(struct bfq_data *bfqd, int node)
{
	int ret;

	ret = blkcg_activate_policy(bfqd->queue, &blkcg_policy_bfq);
	if (ret)
		return NULL;

	return blkg_to_bfqg(bfqd->queue->root_blkg);
}

static struct cftype bfq_blkcg_legacy_files[] = {
	{
		.name = "bfq.weight",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = bfq_io_show_weight,
		.write_u64 = bfq_io_set_weight_legacy,
	},

	/* statistics, covers only the tasks in the bfqg */
	{
		.name = "bfq.time",
		.private = offsetof(struct bfq_group, stats.time),
		.seq_show = bfqg_print_stat,
	},
	{
		.name = "bfq.sectors",
		.seq_show = bfqg_print_stat_sectors,
	},
	{
		.name = "bfq.io_service_bytes",
		.private = (unsigned long)&blkcg_policy_bfq,
		.seq_show = blkg_print_stat_bytes,
	},
	{
		.name = "bfq.io_serviced",
		.private = (unsigned long)&blkcg_policy_bfq,
		.seq_show = blkg_print_stat_ios,
	},
	{
		.name = "bfq.io_service_time",
		.private = offsetof(struct bfq_group, stats.service_time),
		.seq_show = bfqg_print_rwstat,
	},
	{
		.name = "bfq.io_wait_time",
		.private = offsetof(struct bfq_group, stats.wait_time),
		.seq_show = bfqg_print_rwstat,
	},
	{
		.name = "bfq.io_merged",
		.private = offsetof(struct bfq_group, stats.merged),
		.seq_show = bfqg_print_rwstat,
	},
	{
		.name = "bfq.io_queued",
		.private = offsetof(struct bfq_group, stats.queued),
		.seq_show = bfqg_print_rwstat,
	},

	/* the same statictics which cover the bfqg and its descendants */
	{
		.name = "bfq.time_recursive",
		.private = offsetof(struct bfq_group, stats.time),
		.seq_show = bfqg_print_stat_recursive,
	},
	{
		.name = "bfq.sectors_recursive",
		.seq_show = bfqg_print_stat_sectors_recursive,
	},
	{
		.name = "bfq.io_service_bytes_recursive",
		.private = (unsigned long)&blkcg_policy_bfq,
		.seq_show = blkg_print_stat_bytes_recursive,
	},
	{
		.name = "bfq.io_serviced_recursive",
		.private = (unsigned long)&blkcg_policy_bfq,
		.seq_show = blkg_print_stat_ios_recursive,
	},
	{
		.name = "bfq.io_service_time_recursive",
		.private = offsetof(struct bfq_group, stats.service_time),
		.seq_show = bfqg_print_rwstat_recursive,
	},
	{
		.name = "bfq.io_wait_time_recursive",
		.private = offsetof(struct bfq_group, stats.wait_time),
		.seq_show = bfqg_print_rwstat_recursive,
	},
	{
		.name = "bfq.io_merged_recursive",
		.private = offsetof(struct bfq_group, stats.merged),
		.seq_show = bfqg_print_rwstat_recursive,
	},
	{
		.name = "bfq.io_queued_recursive",
		.private = offsetof(struct bfq_group, stats.queued),
		.seq_show = bfqg_print_rwstat_recursive,
	},
	{
		.name = "bfq.avg_queue_size",
		.seq_show = bfqg_print_avg_queue_size,
	},
	{
		.name = "bfq.group_wait_time",
		.private = offsetof(struct bfq_group, stats.group_wait_time),
		.seq_show = bfqg_print_stat,
	},
	{
		.name = "bfq.idle_time",
		.private = offsetof(struct bfq_group, stats.idle_time),
		.seq_show = bfqg_print_stat,
	},
	{
		.name = "bfq.empty_time",
		.private = offsetof(struct bfq_group, stats.empty_time),
		.seq_show = bfqg_print_stat,
	},
	{
		.name = "bfq.dequeue",
		.private = offsetof(struct bfq_group, stats.dequeue),
		.seq_show = bfqg_print_stat,
	},
	{ }	/* terminate */
};

static struct cftype bfq_blkg_files[] = {
	{
		.name = "bfq.weight",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = bfq_io_show_weight,
		.write = bfq_io_set_weight,
	},
	{} /* terminate */
};

#else	/* CONFIG_BFQ_GROUP_IOSCHED */

static inline void bfqg_stats_update_io_add(struct bfq_group *bfqg,
			struct bfq_queue *bfqq, unsigned int op) { }
static inline void
bfqg_stats_update_io_remove(struct bfq_group *bfqg, unsigned int op) { }
static inline void
bfqg_stats_update_io_merged(struct bfq_group *bfqg, unsigned int op) { }
static inline void bfqg_stats_update_completion(struct bfq_group *bfqg,
			uint64_t start_time, uint64_t io_start_time,
			unsigned int op) { }
static inline void
bfqg_stats_set_start_group_wait_time(struct bfq_group *bfqg,
				     struct bfq_group *curr_bfqg) { }
static inline void bfqg_stats_end_empty_time(struct bfqg_stats *stats) { }
static inline void bfqg_stats_update_dequeue(struct bfq_group *bfqg) { }
static inline void bfqg_stats_set_start_empty_time(struct bfq_group *bfqg) { }
static inline void bfqg_stats_update_idle_time(struct bfq_group *bfqg) { }
static inline void bfqg_stats_set_start_idle_time(struct bfq_group *bfqg) { }
static inline void bfqg_stats_update_avg_queue_size(struct bfq_group *bfqg) { }

static void bfq_bfqq_move(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			  struct bfq_group *bfqg) {}

static void bfq_init_entity(struct bfq_entity *entity,
			    struct bfq_group *bfqg)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	entity->weight = entity->new_weight;
	entity->orig_weight = entity->new_weight;
	if (bfqq) {
		bfqq->ioprio = bfqq->new_ioprio;
		bfqq->ioprio_class = bfqq->new_ioprio_class;
	}
	entity->sched_data = &bfqg->sched_data;
}

static void bfq_bic_update_cgroup(struct bfq_io_cq *bic, struct bio *bio) {}

static void bfq_end_wr_async(struct bfq_data *bfqd)
{
	bfq_end_wr_async_queues(bfqd, bfqd->root_group);
}

static struct bfq_group *bfq_find_set_group(struct bfq_data *bfqd,
					    struct blkcg *blkcg)
{
	return bfqd->root_group;
}

static struct bfq_group *bfqq_group(struct bfq_queue *bfqq)
{
	return bfqq->bfqd->root_group;
}

static struct bfq_group *bfq_create_group_hierarchy(struct bfq_data *bfqd,
						    int node)
{
	struct bfq_group *bfqg;
	int i;

	bfqg = kmalloc_node(sizeof(*bfqg), GFP_KERNEL | __GFP_ZERO, node);
	if (!bfqg)
		return NULL;

	for (i = 0; i < BFQ_IOPRIO_CLASSES; i++)
		bfqg->sched_data.service_tree[i] = BFQ_SERVICE_TREE_INIT;

	return bfqg;
}
#endif	/* CONFIG_BFQ_GROUP_IOSCHED */

#define bfq_class_idle(bfqq)	((bfqq)->ioprio_class == IOPRIO_CLASS_IDLE)
#define bfq_class_rt(bfqq)	((bfqq)->ioprio_class == IOPRIO_CLASS_RT)

#define bfq_sample_valid(samples)	((samples) > 80)

/*
 * Lifted from AS - choose which of rq1 and rq2 that is best served now.
 * We choose the request that is closesr to the head right now.  Distance
 * behind the head is penalized and only allowed to a certain extent.
 */
static struct request *bfq_choose_req(struct bfq_data *bfqd,
				      struct request *rq1,
				      struct request *rq2,
				      sector_t last)
{
	sector_t s1, s2, d1 = 0, d2 = 0;
	unsigned long back_max;
#define BFQ_RQ1_WRAP	0x01 /* request 1 wraps */
#define BFQ_RQ2_WRAP	0x02 /* request 2 wraps */
	unsigned int wrap = 0; /* bit mask: requests behind the disk head? */

	if (!rq1 || rq1 == rq2)
		return rq2;
	if (!rq2)
		return rq1;

	if (rq_is_sync(rq1) && !rq_is_sync(rq2))
		return rq1;
	else if (rq_is_sync(rq2) && !rq_is_sync(rq1))
		return rq2;
	if ((rq1->cmd_flags & REQ_META) && !(rq2->cmd_flags & REQ_META))
		return rq1;
	else if ((rq2->cmd_flags & REQ_META) && !(rq1->cmd_flags & REQ_META))
		return rq2;

	s1 = blk_rq_pos(rq1);
	s2 = blk_rq_pos(rq2);

	/*
	 * By definition, 1KiB is 2 sectors.
	 */
	back_max = bfqd->bfq_back_max * 2;

	/*
	 * Strict one way elevator _except_ in the case where we allow
	 * short backward seeks which are biased as twice the cost of a
	 * similar forward seek.
	 */
	if (s1 >= last)
		d1 = s1 - last;
	else if (s1 + back_max >= last)
		d1 = (last - s1) * bfqd->bfq_back_penalty;
	else
		wrap |= BFQ_RQ1_WRAP;

	if (s2 >= last)
		d2 = s2 - last;
	else if (s2 + back_max >= last)
		d2 = (last - s2) * bfqd->bfq_back_penalty;
	else
		wrap |= BFQ_RQ2_WRAP;

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

		if (s1 >= s2)
			return rq1;
		else
			return rq2;

	case BFQ_RQ2_WRAP:
		return rq1;
	case BFQ_RQ1_WRAP:
		return rq2;
	case BFQ_RQ1_WRAP|BFQ_RQ2_WRAP: /* both rqs wrapped */
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

static struct bfq_queue *
bfq_rq_pos_tree_lookup(struct bfq_data *bfqd, struct rb_root *root,
		     sector_t sector, struct rb_node **ret_parent,
		     struct rb_node ***rb_link)
{
	struct rb_node **p, *parent;
	struct bfq_queue *bfqq = NULL;

	parent = NULL;
	p = &root->rb_node;
	while (*p) {
		struct rb_node **n;

		parent = *p;
		bfqq = rb_entry(parent, struct bfq_queue, pos_node);

		/*
		 * Sort strictly based on sector. Smallest to the left,
		 * largest to the right.
		 */
		if (sector > blk_rq_pos(bfqq->next_rq))
			n = &(*p)->rb_right;
		else if (sector < blk_rq_pos(bfqq->next_rq))
			n = &(*p)->rb_left;
		else
			break;
		p = n;
		bfqq = NULL;
	}

	*ret_parent = parent;
	if (rb_link)
		*rb_link = p;

	bfq_log(bfqd, "rq_pos_tree_lookup %llu: returning %d",
		(unsigned long long)sector,
		bfqq ? bfqq->pid : 0);

	return bfqq;
}

static void bfq_pos_tree_add_move(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct rb_node **p, *parent;
	struct bfq_queue *__bfqq;

	if (bfqq->pos_root) {
		rb_erase(&bfqq->pos_node, bfqq->pos_root);
		bfqq->pos_root = NULL;
	}

	if (bfq_class_idle(bfqq))
		return;
	if (!bfqq->next_rq)
		return;

	bfqq->pos_root = &bfq_bfqq_to_bfqg(bfqq)->rq_pos_tree;
	__bfqq = bfq_rq_pos_tree_lookup(bfqd, bfqq->pos_root,
			blk_rq_pos(bfqq->next_rq), &parent, &p);
	if (!__bfqq) {
		rb_link_node(&bfqq->pos_node, parent, p);
		rb_insert_color(&bfqq->pos_node, bfqq->pos_root);
	} else
		bfqq->pos_root = NULL;
}

/*
 * Tell whether there are active queues or groups with differentiated weights.
 */
static bool bfq_differentiated_weights(struct bfq_data *bfqd)
{
	/*
	 * For weights to differ, at least one of the trees must contain
	 * at least two nodes.
	 */
	return (!RB_EMPTY_ROOT(&bfqd->queue_weights_tree) &&
		(bfqd->queue_weights_tree.rb_node->rb_left ||
		 bfqd->queue_weights_tree.rb_node->rb_right)
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	       ) ||
	       (!RB_EMPTY_ROOT(&bfqd->group_weights_tree) &&
		(bfqd->group_weights_tree.rb_node->rb_left ||
		 bfqd->group_weights_tree.rb_node->rb_right)
#endif
	       );
}

/*
 * The following function returns true if every queue must receive the
 * same share of the throughput (this condition is used when deciding
 * whether idling may be disabled, see the comments in the function
 * bfq_bfqq_may_idle()).
 *
 * Such a scenario occurs when:
 * 1) all active queues have the same weight,
 * 2) all active groups at the same level in the groups tree have the same
 *    weight,
 * 3) all active groups at the same level in the groups tree have the same
 *    number of children.
 *
 * Unfortunately, keeping the necessary state for evaluating exactly the
 * above symmetry conditions would be quite complex and time-consuming.
 * Therefore this function evaluates, instead, the following stronger
 * sub-conditions, for which it is much easier to maintain the needed
 * state:
 * 1) all active queues have the same weight,
 * 2) all active groups have the same weight,
 * 3) all active groups have at most one active child each.
 * In particular, the last two conditions are always true if hierarchical
 * support and the cgroups interface are not enabled, thus no state needs
 * to be maintained in this case.
 */
static bool bfq_symmetric_scenario(struct bfq_data *bfqd)
{
	return !bfq_differentiated_weights(bfqd);
}

/*
 * If the weight-counter tree passed as input contains no counter for
 * the weight of the input entity, then add that counter; otherwise just
 * increment the existing counter.
 *
 * Note that weight-counter trees contain few nodes in mostly symmetric
 * scenarios. For example, if all queues have the same weight, then the
 * weight-counter tree for the queues may contain at most one node.
 * This holds even if low_latency is on, because weight-raised queues
 * are not inserted in the tree.
 * In most scenarios, the rate at which nodes are created/destroyed
 * should be low too.
 */
static void bfq_weights_tree_add(struct bfq_data *bfqd,
				 struct bfq_entity *entity,
				 struct rb_root *root)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	/*
	 * Do not insert if the entity is already associated with a
	 * counter, which happens if:
	 *   1) the entity is associated with a queue,
	 *   2) a request arrival has caused the queue to become both
	 *      non-weight-raised, and hence change its weight, and
	 *      backlogged; in this respect, each of the two events
	 *      causes an invocation of this function,
	 *   3) this is the invocation of this function caused by the
	 *      second event. This second invocation is actually useless,
	 *      and we handle this fact by exiting immediately. More
	 *      efficient or clearer solutions might possibly be adopted.
	 */
	if (entity->weight_counter)
		return;

	while (*new) {
		struct bfq_weight_counter *__counter = container_of(*new,
						struct bfq_weight_counter,
						weights_node);
		parent = *new;

		if (entity->weight == __counter->weight) {
			entity->weight_counter = __counter;
			goto inc_counter;
		}
		if (entity->weight < __counter->weight)
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
	}

	entity->weight_counter = kzalloc(sizeof(struct bfq_weight_counter),
					 GFP_ATOMIC);

	/*
	 * In the unlucky event of an allocation failure, we just
	 * exit. This will cause the weight of entity to not be
	 * considered in bfq_differentiated_weights, which, in its
	 * turn, causes the scenario to be deemed wrongly symmetric in
	 * case entity's weight would have been the only weight making
	 * the scenario asymmetric. On the bright side, no unbalance
	 * will however occur when entity becomes inactive again (the
	 * invocation of this function is triggered by an activation
	 * of entity). In fact, bfq_weights_tree_remove does nothing
	 * if !entity->weight_counter.
	 */
	if (unlikely(!entity->weight_counter))
		return;

	entity->weight_counter->weight = entity->weight;
	rb_link_node(&entity->weight_counter->weights_node, parent, new);
	rb_insert_color(&entity->weight_counter->weights_node, root);

inc_counter:
	entity->weight_counter->num_active++;
}

/*
 * Decrement the weight counter associated with the entity, and, if the
 * counter reaches 0, remove the counter from the tree.
 * See the comments to the function bfq_weights_tree_add() for considerations
 * about overhead.
 */
static void bfq_weights_tree_remove(struct bfq_data *bfqd,
				    struct bfq_entity *entity,
				    struct rb_root *root)
{
	if (!entity->weight_counter)
		return;

	entity->weight_counter->num_active--;
	if (entity->weight_counter->num_active > 0)
		goto reset_entity_pointer;

	rb_erase(&entity->weight_counter->weights_node, root);
	kfree(entity->weight_counter);

reset_entity_pointer:
	entity->weight_counter = NULL;
}

/*
 * Return expired entry, or NULL to just start from scratch in rbtree.
 */
static struct request *bfq_check_fifo(struct bfq_queue *bfqq,
				      struct request *last)
{
	struct request *rq;

	if (bfq_bfqq_fifo_expire(bfqq))
		return NULL;

	bfq_mark_bfqq_fifo_expire(bfqq);

	rq = rq_entry_fifo(bfqq->fifo.next);

	if (rq == last || ktime_get_ns() < rq->fifo_time)
		return NULL;

	bfq_log_bfqq(bfqq->bfqd, bfqq, "check_fifo: returned %p", rq);
	return rq;
}

static struct request *bfq_find_next_rq(struct bfq_data *bfqd,
					struct bfq_queue *bfqq,
					struct request *last)
{
	struct rb_node *rbnext = rb_next(&last->rb_node);
	struct rb_node *rbprev = rb_prev(&last->rb_node);
	struct request *next, *prev = NULL;

	/* Follow expired path, else get first next available. */
	next = bfq_check_fifo(bfqq, last);
	if (next)
		return next;

	if (rbprev)
		prev = rb_entry_rq(rbprev);

	if (rbnext)
		next = rb_entry_rq(rbnext);
	else {
		rbnext = rb_first(&bfqq->sort_list);
		if (rbnext && rbnext != &last->rb_node)
			next = rb_entry_rq(rbnext);
	}

	return bfq_choose_req(bfqd, next, prev, blk_rq_pos(last));
}

/* see the definition of bfq_async_charge_factor for details */
static unsigned long bfq_serv_to_charge(struct request *rq,
					struct bfq_queue *bfqq)
{
	if (bfq_bfqq_sync(bfqq) || bfqq->wr_coeff > 1)
		return blk_rq_sectors(rq);

	/*
	 * If there are no weight-raised queues, then amplify service
	 * by just the async charge factor; otherwise amplify service
	 * by twice the async charge factor, to further reduce latency
	 * for weight-raised queues.
	 */
	if (bfqq->bfqd->wr_busy_queues == 0)
		return blk_rq_sectors(rq) * bfq_async_charge_factor;

	return blk_rq_sectors(rq) * 2 * bfq_async_charge_factor;
}

/**
 * bfq_updated_next_req - update the queue after a new next_rq selection.
 * @bfqd: the device data the queue belongs to.
 * @bfqq: the queue to update.
 *
 * If the first request of a queue changes we make sure that the queue
 * has enough budget to serve at least its first request (if the
 * request has grown).  We do this because if the queue has not enough
 * budget for its first request, it has to go through two dispatch
 * rounds to actually get it dispatched.
 */
static void bfq_updated_next_req(struct bfq_data *bfqd,
				 struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;
	struct request *next_rq = bfqq->next_rq;
	unsigned long new_budget;

	if (!next_rq)
		return;

	if (bfqq == bfqd->in_service_queue)
		/*
		 * In order not to break guarantees, budgets cannot be
		 * changed after an entity has been selected.
		 */
		return;

	new_budget = max_t(unsigned long, bfqq->max_budget,
			   bfq_serv_to_charge(next_rq, bfqq));
	if (entity->budget != new_budget) {
		entity->budget = new_budget;
		bfq_log_bfqq(bfqd, bfqq, "updated next rq: new budget %lu",
					 new_budget);
		bfq_requeue_bfqq(bfqd, bfqq);
	}
}

static void
bfq_bfqq_resume_state(struct bfq_queue *bfqq, struct bfq_io_cq *bic)
{
	if (bic->saved_idle_window)
		bfq_mark_bfqq_idle_window(bfqq);
	else
		bfq_clear_bfqq_idle_window(bfqq);

	if (bic->saved_IO_bound)
		bfq_mark_bfqq_IO_bound(bfqq);
	else
		bfq_clear_bfqq_IO_bound(bfqq);

	bfqq->ttime = bic->saved_ttime;
	bfqq->wr_coeff = bic->saved_wr_coeff;
	bfqq->wr_start_at_switch_to_srt = bic->saved_wr_start_at_switch_to_srt;
	bfqq->last_wr_start_finish = bic->saved_last_wr_start_finish;
	bfqq->wr_cur_max_time = bic->saved_wr_cur_max_time;

	if (bfqq->wr_coeff > 1 && (bfq_bfqq_in_large_burst(bfqq) ||
	    time_is_before_jiffies(bfqq->last_wr_start_finish +
				   bfqq->wr_cur_max_time))) {
		bfq_log_bfqq(bfqq->bfqd, bfqq,
		    "resume state: switching off wr");

		bfqq->wr_coeff = 1;
	}

	/* make sure weight will be updated, however we got here */
	bfqq->entity.prio_changed = 1;
}

static int bfqq_process_refs(struct bfq_queue *bfqq)
{
	return bfqq->ref - bfqq->allocated - bfqq->entity.on_st;
}

/* Empty burst list and add just bfqq (see comments on bfq_handle_burst) */
static void bfq_reset_burst_list(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct bfq_queue *item;
	struct hlist_node *n;

	hlist_for_each_entry_safe(item, n, &bfqd->burst_list, burst_list_node)
		hlist_del_init(&item->burst_list_node);
	hlist_add_head(&bfqq->burst_list_node, &bfqd->burst_list);
	bfqd->burst_size = 1;
	bfqd->burst_parent_entity = bfqq->entity.parent;
}

/* Add bfqq to the list of queues in current burst (see bfq_handle_burst) */
static void bfq_add_to_burst(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	/* Increment burst size to take into account also bfqq */
	bfqd->burst_size++;

	if (bfqd->burst_size == bfqd->bfq_large_burst_thresh) {
		struct bfq_queue *pos, *bfqq_item;
		struct hlist_node *n;

		/*
		 * Enough queues have been activated shortly after each
		 * other to consider this burst as large.
		 */
		bfqd->large_burst = true;

		/*
		 * We can now mark all queues in the burst list as
		 * belonging to a large burst.
		 */
		hlist_for_each_entry(bfqq_item, &bfqd->burst_list,
				     burst_list_node)
			bfq_mark_bfqq_in_large_burst(bfqq_item);
		bfq_mark_bfqq_in_large_burst(bfqq);

		/*
		 * From now on, and until the current burst finishes, any
		 * new queue being activated shortly after the last queue
		 * was inserted in the burst can be immediately marked as
		 * belonging to a large burst. So the burst list is not
		 * needed any more. Remove it.
		 */
		hlist_for_each_entry_safe(pos, n, &bfqd->burst_list,
					  burst_list_node)
			hlist_del_init(&pos->burst_list_node);
	} else /*
		* Burst not yet large: add bfqq to the burst list. Do
		* not increment the ref counter for bfqq, because bfqq
		* is removed from the burst list before freeing bfqq
		* in put_queue.
		*/
		hlist_add_head(&bfqq->burst_list_node, &bfqd->burst_list);
}

/*
 * If many queues belonging to the same group happen to be created
 * shortly after each other, then the processes associated with these
 * queues have typically a common goal. In particular, bursts of queue
 * creations are usually caused by services or applications that spawn
 * many parallel threads/processes. Examples are systemd during boot,
 * or git grep. To help these processes get their job done as soon as
 * possible, it is usually better to not grant either weight-raising
 * or device idling to their queues.
 *
 * In this comment we describe, firstly, the reasons why this fact
 * holds, and, secondly, the next function, which implements the main
 * steps needed to properly mark these queues so that they can then be
 * treated in a different way.
 *
 * The above services or applications benefit mostly from a high
 * throughput: the quicker the requests of the activated queues are
 * cumulatively served, the sooner the target job of these queues gets
 * completed. As a consequence, weight-raising any of these queues,
 * which also implies idling the device for it, is almost always
 * counterproductive. In most cases it just lowers throughput.
 *
 * On the other hand, a burst of queue creations may be caused also by
 * the start of an application that does not consist of a lot of
 * parallel I/O-bound threads. In fact, with a complex application,
 * several short processes may need to be executed to start-up the
 * application. In this respect, to start an application as quickly as
 * possible, the best thing to do is in any case to privilege the I/O
 * related to the application with respect to all other
 * I/O. Therefore, the best strategy to start as quickly as possible
 * an application that causes a burst of queue creations is to
 * weight-raise all the queues created during the burst. This is the
 * exact opposite of the best strategy for the other type of bursts.
 *
 * In the end, to take the best action for each of the two cases, the
 * two types of bursts need to be distinguished. Fortunately, this
 * seems relatively easy, by looking at the sizes of the bursts. In
 * particular, we found a threshold such that only bursts with a
 * larger size than that threshold are apparently caused by
 * services or commands such as systemd or git grep. For brevity,
 * hereafter we call just 'large' these bursts. BFQ *does not*
 * weight-raise queues whose creation occurs in a large burst. In
 * addition, for each of these queues BFQ performs or does not perform
 * idling depending on which choice boosts the throughput more. The
 * exact choice depends on the device and request pattern at
 * hand.
 *
 * Unfortunately, false positives may occur while an interactive task
 * is starting (e.g., an application is being started). The
 * consequence is that the queues associated with the task do not
 * enjoy weight raising as expected. Fortunately these false positives
 * are very rare. They typically occur if some service happens to
 * start doing I/O exactly when the interactive task starts.
 *
 * Turning back to the next function, it implements all the steps
 * needed to detect the occurrence of a large burst and to properly
 * mark all the queues belonging to it (so that they can then be
 * treated in a different way). This goal is achieved by maintaining a
 * "burst list" that holds, temporarily, the queues that belong to the
 * burst in progress. The list is then used to mark these queues as
 * belonging to a large burst if the burst does become large. The main
 * steps are the following.
 *
 * . when the very first queue is created, the queue is inserted into the
 *   list (as it could be the first queue in a possible burst)
 *
 * . if the current burst has not yet become large, and a queue Q that does
 *   not yet belong to the burst is activated shortly after the last time
 *   at which a new queue entered the burst list, then the function appends
 *   Q to the burst list
 *
 * . if, as a consequence of the previous step, the burst size reaches
 *   the large-burst threshold, then
 *
 *     . all the queues in the burst list are marked as belonging to a
 *       large burst
 *
 *     . the burst list is deleted; in fact, the burst list already served
 *       its purpose (keeping temporarily track of the queues in a burst,
 *       so as to be able to mark them as belonging to a large burst in the
 *       previous sub-step), and now is not needed any more
 *
 *     . the device enters a large-burst mode
 *
 * . if a queue Q that does not belong to the burst is created while
 *   the device is in large-burst mode and shortly after the last time
 *   at which a queue either entered the burst list or was marked as
 *   belonging to the current large burst, then Q is immediately marked
 *   as belonging to a large burst.
 *
 * . if a queue Q that does not belong to the burst is created a while
 *   later, i.e., not shortly after, than the last time at which a queue
 *   either entered the burst list or was marked as belonging to the
 *   current large burst, then the current burst is deemed as finished and:
 *
 *        . the large-burst mode is reset if set
 *
 *        . the burst list is emptied
 *
 *        . Q is inserted in the burst list, as Q may be the first queue
 *          in a possible new burst (then the burst list contains just Q
 *          after this step).
 */
static void bfq_handle_burst(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	/*
	 * If bfqq is already in the burst list or is part of a large
	 * burst, or finally has just been split, then there is
	 * nothing else to do.
	 */
	if (!hlist_unhashed(&bfqq->burst_list_node) ||
	    bfq_bfqq_in_large_burst(bfqq) ||
	    time_is_after_eq_jiffies(bfqq->split_time +
				     msecs_to_jiffies(10)))
		return;

	/*
	 * If bfqq's creation happens late enough, or bfqq belongs to
	 * a different group than the burst group, then the current
	 * burst is finished, and related data structures must be
	 * reset.
	 *
	 * In this respect, consider the special case where bfqq is
	 * the very first queue created after BFQ is selected for this
	 * device. In this case, last_ins_in_burst and
	 * burst_parent_entity are not yet significant when we get
	 * here. But it is easy to verify that, whether or not the
	 * following condition is true, bfqq will end up being
	 * inserted into the burst list. In particular the list will
	 * happen to contain only bfqq. And this is exactly what has
	 * to happen, as bfqq may be the first queue of the first
	 * burst.
	 */
	if (time_is_before_jiffies(bfqd->last_ins_in_burst +
	    bfqd->bfq_burst_interval) ||
	    bfqq->entity.parent != bfqd->burst_parent_entity) {
		bfqd->large_burst = false;
		bfq_reset_burst_list(bfqd, bfqq);
		goto end;
	}

	/*
	 * If we get here, then bfqq is being activated shortly after the
	 * last queue. So, if the current burst is also large, we can mark
	 * bfqq as belonging to this large burst immediately.
	 */
	if (bfqd->large_burst) {
		bfq_mark_bfqq_in_large_burst(bfqq);
		goto end;
	}

	/*
	 * If we get here, then a large-burst state has not yet been
	 * reached, but bfqq is being activated shortly after the last
	 * queue. Then we add bfqq to the burst.
	 */
	bfq_add_to_burst(bfqd, bfqq);
end:
	/*
	 * At this point, bfqq either has been added to the current
	 * burst or has caused the current burst to terminate and a
	 * possible new burst to start. In particular, in the second
	 * case, bfqq has become the first queue in the possible new
	 * burst.  In both cases last_ins_in_burst needs to be moved
	 * forward.
	 */
	bfqd->last_ins_in_burst = jiffies;
}

static int bfq_bfqq_budget_left(struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;

	return entity->budget - entity->service;
}

/*
 * If enough samples have been computed, return the current max budget
 * stored in bfqd, which is dynamically updated according to the
 * estimated disk peak rate; otherwise return the default max budget
 */
static int bfq_max_budget(struct bfq_data *bfqd)
{
	if (bfqd->budgets_assigned < bfq_stats_min_budgets)
		return bfq_default_max_budget;
	else
		return bfqd->bfq_max_budget;
}

/*
 * Return min budget, which is a fraction of the current or default
 * max budget (trying with 1/32)
 */
static int bfq_min_budget(struct bfq_data *bfqd)
{
	if (bfqd->budgets_assigned < bfq_stats_min_budgets)
		return bfq_default_max_budget / 32;
	else
		return bfqd->bfq_max_budget / 32;
}

static void bfq_bfqq_expire(struct bfq_data *bfqd,
			    struct bfq_queue *bfqq,
			    bool compensate,
			    enum bfqq_expiration reason);

/*
 * The next function, invoked after the input queue bfqq switches from
 * idle to busy, updates the budget of bfqq. The function also tells
 * whether the in-service queue should be expired, by returning
 * true. The purpose of expiring the in-service queue is to give bfqq
 * the chance to possibly preempt the in-service queue, and the reason
 * for preempting the in-service queue is to achieve one of the two
 * goals below.
 *
 * 1. Guarantee to bfqq its reserved bandwidth even if bfqq has
 * expired because it has remained idle. In particular, bfqq may have
 * expired for one of the following two reasons:
 *
 * - BFQQE_NO_MORE_REQUESTS bfqq did not enjoy any device idling
 *   and did not make it to issue a new request before its last
 *   request was served;
 *
 * - BFQQE_TOO_IDLE bfqq did enjoy device idling, but did not issue
 *   a new request before the expiration of the idling-time.
 *
 * Even if bfqq has expired for one of the above reasons, the process
 * associated with the queue may be however issuing requests greedily,
 * and thus be sensitive to the bandwidth it receives (bfqq may have
 * remained idle for other reasons: CPU high load, bfqq not enjoying
 * idling, I/O throttling somewhere in the path from the process to
 * the I/O scheduler, ...). But if, after every expiration for one of
 * the above two reasons, bfqq has to wait for the service of at least
 * one full budget of another queue before being served again, then
 * bfqq is likely to get a much lower bandwidth or resource time than
 * its reserved ones. To address this issue, two countermeasures need
 * to be taken.
 *
 * First, the budget and the timestamps of bfqq need to be updated in
 * a special way on bfqq reactivation: they need to be updated as if
 * bfqq did not remain idle and did not expire. In fact, if they are
 * computed as if bfqq expired and remained idle until reactivation,
 * then the process associated with bfqq is treated as if, instead of
 * being greedy, it stopped issuing requests when bfqq remained idle,
 * and restarts issuing requests only on this reactivation. In other
 * words, the scheduler does not help the process recover the "service
 * hole" between bfqq expiration and reactivation. As a consequence,
 * the process receives a lower bandwidth than its reserved one. In
 * contrast, to recover this hole, the budget must be updated as if
 * bfqq was not expired at all before this reactivation, i.e., it must
 * be set to the value of the remaining budget when bfqq was
 * expired. Along the same line, timestamps need to be assigned the
 * value they had the last time bfqq was selected for service, i.e.,
 * before last expiration. Thus timestamps need to be back-shifted
 * with respect to their normal computation (see [1] for more details
 * on this tricky aspect).
 *
 * Secondly, to allow the process to recover the hole, the in-service
 * queue must be expired too, to give bfqq the chance to preempt it
 * immediately. In fact, if bfqq has to wait for a full budget of the
 * in-service queue to be completed, then it may become impossible to
 * let the process recover the hole, even if the back-shifted
 * timestamps of bfqq are lower than those of the in-service queue. If
 * this happens for most or all of the holes, then the process may not
 * receive its reserved bandwidth. In this respect, it is worth noting
 * that, being the service of outstanding requests unpreemptible, a
 * little fraction of the holes may however be unrecoverable, thereby
 * causing a little loss of bandwidth.
 *
 * The last important point is detecting whether bfqq does need this
 * bandwidth recovery. In this respect, the next function deems the
 * process associated with bfqq greedy, and thus allows it to recover
 * the hole, if: 1) the process is waiting for the arrival of a new
 * request (which implies that bfqq expired for one of the above two
 * reasons), and 2) such a request has arrived soon. The first
 * condition is controlled through the flag non_blocking_wait_rq,
 * while the second through the flag arrived_in_time. If both
 * conditions hold, then the function computes the budget in the
 * above-described special way, and signals that the in-service queue
 * should be expired. Timestamp back-shifting is done later in
 * __bfq_activate_entity.
 *
 * 2. Reduce latency. Even if timestamps are not backshifted to let
 * the process associated with bfqq recover a service hole, bfqq may
 * however happen to have, after being (re)activated, a lower finish
 * timestamp than the in-service queue.	 That is, the next budget of
 * bfqq may have to be completed before the one of the in-service
 * queue. If this is the case, then preempting the in-service queue
 * allows this goal to be achieved, apart from the unpreemptible,
 * outstanding requests mentioned above.
 *
 * Unfortunately, regardless of which of the above two goals one wants
 * to achieve, service trees need first to be updated to know whether
 * the in-service queue must be preempted. To have service trees
 * correctly updated, the in-service queue must be expired and
 * rescheduled, and bfqq must be scheduled too. This is one of the
 * most costly operations (in future versions, the scheduling
 * mechanism may be re-designed in such a way to make it possible to
 * know whether preemption is needed without needing to update service
 * trees). In addition, queue preemptions almost always cause random
 * I/O, and thus loss of throughput. Because of these facts, the next
 * function adopts the following simple scheme to avoid both costly
 * operations and too frequent preemptions: it requests the expiration
 * of the in-service queue (unconditionally) only for queues that need
 * to recover a hole, or that either are weight-raised or deserve to
 * be weight-raised.
 */
static bool bfq_bfqq_update_budg_for_activation(struct bfq_data *bfqd,
						struct bfq_queue *bfqq,
						bool arrived_in_time,
						bool wr_or_deserves_wr)
{
	struct bfq_entity *entity = &bfqq->entity;

	if (bfq_bfqq_non_blocking_wait_rq(bfqq) && arrived_in_time) {
		/*
		 * We do not clear the flag non_blocking_wait_rq here, as
		 * the latter is used in bfq_activate_bfqq to signal
		 * that timestamps need to be back-shifted (and is
		 * cleared right after).
		 */

		/*
		 * In next assignment we rely on that either
		 * entity->service or entity->budget are not updated
		 * on expiration if bfqq is empty (see
		 * __bfq_bfqq_recalc_budget). Thus both quantities
		 * remain unchanged after such an expiration, and the
		 * following statement therefore assigns to
		 * entity->budget the remaining budget on such an
		 * expiration. For clarity, entity->service is not
		 * updated on expiration in any case, and, in normal
		 * operation, is reset only when bfqq is selected for
		 * service (see bfq_get_next_queue).
		 */
		entity->budget = min_t(unsigned long,
				       bfq_bfqq_budget_left(bfqq),
				       bfqq->max_budget);

		return true;
	}

	entity->budget = max_t(unsigned long, bfqq->max_budget,
			       bfq_serv_to_charge(bfqq->next_rq, bfqq));
	bfq_clear_bfqq_non_blocking_wait_rq(bfqq);
	return wr_or_deserves_wr;
}

static unsigned int bfq_wr_duration(struct bfq_data *bfqd)
{
	u64 dur;

	if (bfqd->bfq_wr_max_time > 0)
		return bfqd->bfq_wr_max_time;

	dur = bfqd->RT_prod;
	do_div(dur, bfqd->peak_rate);

	/*
	 * Limit duration between 3 and 13 seconds. Tests show that
	 * higher values than 13 seconds often yield the opposite of
	 * the desired result, i.e., worsen responsiveness by letting
	 * non-interactive and non-soft-real-time applications
	 * preserve weight raising for a too long time interval.
	 *
	 * On the other end, lower values than 3 seconds make it
	 * difficult for most interactive tasks to complete their jobs
	 * before weight-raising finishes.
	 */
	if (dur > msecs_to_jiffies(13000))
		dur = msecs_to_jiffies(13000);
	else if (dur < msecs_to_jiffies(3000))
		dur = msecs_to_jiffies(3000);

	return dur;
}

static void bfq_update_bfqq_wr_on_rq_arrival(struct bfq_data *bfqd,
					     struct bfq_queue *bfqq,
					     unsigned int old_wr_coeff,
					     bool wr_or_deserves_wr,
					     bool interactive,
					     bool in_burst,
					     bool soft_rt)
{
	if (old_wr_coeff == 1 && wr_or_deserves_wr) {
		/* start a weight-raising period */
		if (interactive) {
			bfqq->wr_coeff = bfqd->bfq_wr_coeff;
			bfqq->wr_cur_max_time = bfq_wr_duration(bfqd);
		} else {
			bfqq->wr_start_at_switch_to_srt = jiffies;
			bfqq->wr_coeff = bfqd->bfq_wr_coeff *
				BFQ_SOFTRT_WEIGHT_FACTOR;
			bfqq->wr_cur_max_time =
				bfqd->bfq_wr_rt_max_time;
		}

		/*
		 * If needed, further reduce budget to make sure it is
		 * close to bfqq's backlog, so as to reduce the
		 * scheduling-error component due to a too large
		 * budget. Do not care about throughput consequences,
		 * but only about latency. Finally, do not assign a
		 * too small budget either, to avoid increasing
		 * latency by causing too frequent expirations.
		 */
		bfqq->entity.budget = min_t(unsigned long,
					    bfqq->entity.budget,
					    2 * bfq_min_budget(bfqd));
	} else if (old_wr_coeff > 1) {
		if (interactive) { /* update wr coeff and duration */
			bfqq->wr_coeff = bfqd->bfq_wr_coeff;
			bfqq->wr_cur_max_time = bfq_wr_duration(bfqd);
		} else if (in_burst)
			bfqq->wr_coeff = 1;
		else if (soft_rt) {
			/*
			 * The application is now or still meeting the
			 * requirements for being deemed soft rt.  We
			 * can then correctly and safely (re)charge
			 * the weight-raising duration for the
			 * application with the weight-raising
			 * duration for soft rt applications.
			 *
			 * In particular, doing this recharge now, i.e.,
			 * before the weight-raising period for the
			 * application finishes, reduces the probability
			 * of the following negative scenario:
			 * 1) the weight of a soft rt application is
			 *    raised at startup (as for any newly
			 *    created application),
			 * 2) since the application is not interactive,
			 *    at a certain time weight-raising is
			 *    stopped for the application,
			 * 3) at that time the application happens to
			 *    still have pending requests, and hence
			 *    is destined to not have a chance to be
			 *    deemed soft rt before these requests are
			 *    completed (see the comments to the
			 *    function bfq_bfqq_softrt_next_start()
			 *    for details on soft rt detection),
			 * 4) these pending requests experience a high
			 *    latency because the application is not
			 *    weight-raised while they are pending.
			 */
			if (bfqq->wr_cur_max_time !=
				bfqd->bfq_wr_rt_max_time) {
				bfqq->wr_start_at_switch_to_srt =
					bfqq->last_wr_start_finish;

				bfqq->wr_cur_max_time =
					bfqd->bfq_wr_rt_max_time;
				bfqq->wr_coeff = bfqd->bfq_wr_coeff *
					BFQ_SOFTRT_WEIGHT_FACTOR;
			}
			bfqq->last_wr_start_finish = jiffies;
		}
	}
}

static bool bfq_bfqq_idle_for_long_time(struct bfq_data *bfqd,
					struct bfq_queue *bfqq)
{
	return bfqq->dispatched == 0 &&
		time_is_before_jiffies(
			bfqq->budget_timeout +
			bfqd->bfq_wr_min_idle_time);
}

static void bfq_bfqq_handle_idle_busy_switch(struct bfq_data *bfqd,
					     struct bfq_queue *bfqq,
					     int old_wr_coeff,
					     struct request *rq,
					     bool *interactive)
{
	bool soft_rt, in_burst,	wr_or_deserves_wr,
		bfqq_wants_to_preempt,
		idle_for_long_time = bfq_bfqq_idle_for_long_time(bfqd, bfqq),
		/*
		 * See the comments on
		 * bfq_bfqq_update_budg_for_activation for
		 * details on the usage of the next variable.
		 */
		arrived_in_time =  ktime_get_ns() <=
			bfqq->ttime.last_end_request +
			bfqd->bfq_slice_idle * 3;

	bfqg_stats_update_io_add(bfqq_group(RQ_BFQQ(rq)), bfqq, rq->cmd_flags);

	/*
	 * bfqq deserves to be weight-raised if:
	 * - it is sync,
	 * - it does not belong to a large burst,
	 * - it has been idle for enough time or is soft real-time,
	 * - is linked to a bfq_io_cq (it is not shared in any sense).
	 */
	in_burst = bfq_bfqq_in_large_burst(bfqq);
	soft_rt = bfqd->bfq_wr_max_softrt_rate > 0 &&
		!in_burst &&
		time_is_before_jiffies(bfqq->soft_rt_next_start);
	*interactive = !in_burst && idle_for_long_time;
	wr_or_deserves_wr = bfqd->low_latency &&
		(bfqq->wr_coeff > 1 ||
		 (bfq_bfqq_sync(bfqq) &&
		  bfqq->bic && (*interactive || soft_rt)));

	/*
	 * Using the last flag, update budget and check whether bfqq
	 * may want to preempt the in-service queue.
	 */
	bfqq_wants_to_preempt =
		bfq_bfqq_update_budg_for_activation(bfqd, bfqq,
						    arrived_in_time,
						    wr_or_deserves_wr);

	/*
	 * If bfqq happened to be activated in a burst, but has been
	 * idle for much more than an interactive queue, then we
	 * assume that, in the overall I/O initiated in the burst, the
	 * I/O associated with bfqq is finished. So bfqq does not need
	 * to be treated as a queue belonging to a burst
	 * anymore. Accordingly, we reset bfqq's in_large_burst flag
	 * if set, and remove bfqq from the burst list if it's
	 * there. We do not decrement burst_size, because the fact
	 * that bfqq does not need to belong to the burst list any
	 * more does not invalidate the fact that bfqq was created in
	 * a burst.
	 */
	if (likely(!bfq_bfqq_just_created(bfqq)) &&
	    idle_for_long_time &&
	    time_is_before_jiffies(
		    bfqq->budget_timeout +
		    msecs_to_jiffies(10000))) {
		hlist_del_init(&bfqq->burst_list_node);
		bfq_clear_bfqq_in_large_burst(bfqq);
	}

	bfq_clear_bfqq_just_created(bfqq);


	if (!bfq_bfqq_IO_bound(bfqq)) {
		if (arrived_in_time) {
			bfqq->requests_within_timer++;
			if (bfqq->requests_within_timer >=
			    bfqd->bfq_requests_within_timer)
				bfq_mark_bfqq_IO_bound(bfqq);
		} else
			bfqq->requests_within_timer = 0;
	}

	if (bfqd->low_latency) {
		if (unlikely(time_is_after_jiffies(bfqq->split_time)))
			/* wraparound */
			bfqq->split_time =
				jiffies - bfqd->bfq_wr_min_idle_time - 1;

		if (time_is_before_jiffies(bfqq->split_time +
					   bfqd->bfq_wr_min_idle_time)) {
			bfq_update_bfqq_wr_on_rq_arrival(bfqd, bfqq,
							 old_wr_coeff,
							 wr_or_deserves_wr,
							 *interactive,
							 in_burst,
							 soft_rt);

			if (old_wr_coeff != bfqq->wr_coeff)
				bfqq->entity.prio_changed = 1;
		}
	}

	bfqq->last_idle_bklogged = jiffies;
	bfqq->service_from_backlogged = 0;
	bfq_clear_bfqq_softrt_update(bfqq);

	bfq_add_bfqq_busy(bfqd, bfqq);

	/*
	 * Expire in-service queue only if preemption may be needed
	 * for guarantees. In this respect, the function
	 * next_queue_may_preempt just checks a simple, necessary
	 * condition, and not a sufficient condition based on
	 * timestamps. In fact, for the latter condition to be
	 * evaluated, timestamps would need first to be updated, and
	 * this operation is quite costly (see the comments on the
	 * function bfq_bfqq_update_budg_for_activation).
	 */
	if (bfqd->in_service_queue && bfqq_wants_to_preempt &&
	    bfqd->in_service_queue->wr_coeff < bfqq->wr_coeff &&
	    next_queue_may_preempt(bfqd))
		bfq_bfqq_expire(bfqd, bfqd->in_service_queue,
				false, BFQQE_PREEMPTED);
}

static void bfq_add_request(struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);
	struct bfq_data *bfqd = bfqq->bfqd;
	struct request *next_rq, *prev;
	unsigned int old_wr_coeff = bfqq->wr_coeff;
	bool interactive = false;

	bfq_log_bfqq(bfqd, bfqq, "add_request %d", rq_is_sync(rq));
	bfqq->queued[rq_is_sync(rq)]++;
	bfqd->queued++;

	elv_rb_add(&bfqq->sort_list, rq);

	/*
	 * Check if this request is a better next-serve candidate.
	 */
	prev = bfqq->next_rq;
	next_rq = bfq_choose_req(bfqd, bfqq->next_rq, rq, bfqd->last_position);
	bfqq->next_rq = next_rq;

	/*
	 * Adjust priority tree position, if next_rq changes.
	 */
	if (prev != bfqq->next_rq)
		bfq_pos_tree_add_move(bfqd, bfqq);

	if (!bfq_bfqq_busy(bfqq)) /* switching to busy ... */
		bfq_bfqq_handle_idle_busy_switch(bfqd, bfqq, old_wr_coeff,
						 rq, &interactive);
	else {
		if (bfqd->low_latency && old_wr_coeff == 1 && !rq_is_sync(rq) &&
		    time_is_before_jiffies(
				bfqq->last_wr_start_finish +
				bfqd->bfq_wr_min_inter_arr_async)) {
			bfqq->wr_coeff = bfqd->bfq_wr_coeff;
			bfqq->wr_cur_max_time = bfq_wr_duration(bfqd);

			bfqd->wr_busy_queues++;
			bfqq->entity.prio_changed = 1;
		}
		if (prev != bfqq->next_rq)
			bfq_updated_next_req(bfqd, bfqq);
	}

	/*
	 * Assign jiffies to last_wr_start_finish in the following
	 * cases:
	 *
	 * . if bfqq is not going to be weight-raised, because, for
	 *   non weight-raised queues, last_wr_start_finish stores the
	 *   arrival time of the last request; as of now, this piece
	 *   of information is used only for deciding whether to
	 *   weight-raise async queues
	 *
	 * . if bfqq is not weight-raised, because, if bfqq is now
	 *   switching to weight-raised, then last_wr_start_finish
	 *   stores the time when weight-raising starts
	 *
	 * . if bfqq is interactive, because, regardless of whether
	 *   bfqq is currently weight-raised, the weight-raising
	 *   period must start or restart (this case is considered
	 *   separately because it is not detected by the above
	 *   conditions, if bfqq is already weight-raised)
	 *
	 * last_wr_start_finish has to be updated also if bfqq is soft
	 * real-time, because the weight-raising period is constantly
	 * restarted on idle-to-busy transitions for these queues, but
	 * this is already done in bfq_bfqq_handle_idle_busy_switch if
	 * needed.
	 */
	if (bfqd->low_latency &&
		(old_wr_coeff == 1 || bfqq->wr_coeff == 1 || interactive))
		bfqq->last_wr_start_finish = jiffies;
}

static struct request *bfq_find_rq_fmerge(struct bfq_data *bfqd,
					  struct bio *bio,
					  struct request_queue *q)
{
	struct bfq_queue *bfqq = bfqd->bio_bfqq;


	if (bfqq)
		return elv_rb_find(&bfqq->sort_list, bio_end_sector(bio));

	return NULL;
}

static sector_t get_sdist(sector_t last_pos, struct request *rq)
{
	if (last_pos)
		return abs(blk_rq_pos(rq) - last_pos);

	return 0;
}

#if 0 /* Still not clear if we can do without next two functions */
static void bfq_activate_request(struct request_queue *q, struct request *rq)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;

	bfqd->rq_in_driver++;
}

static void bfq_deactivate_request(struct request_queue *q, struct request *rq)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;

	bfqd->rq_in_driver--;
}
#endif

static void bfq_remove_request(struct request_queue *q,
			       struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);
	struct bfq_data *bfqd = bfqq->bfqd;
	const int sync = rq_is_sync(rq);

	if (bfqq->next_rq == rq) {
		bfqq->next_rq = bfq_find_next_rq(bfqd, bfqq, rq);
		bfq_updated_next_req(bfqd, bfqq);
	}

	if (rq->queuelist.prev != &rq->queuelist)
		list_del_init(&rq->queuelist);
	bfqq->queued[sync]--;
	bfqd->queued--;
	elv_rb_del(&bfqq->sort_list, rq);

	elv_rqhash_del(q, rq);
	if (q->last_merge == rq)
		q->last_merge = NULL;

	if (RB_EMPTY_ROOT(&bfqq->sort_list)) {
		bfqq->next_rq = NULL;

		if (bfq_bfqq_busy(bfqq) && bfqq != bfqd->in_service_queue) {
			bfq_del_bfqq_busy(bfqd, bfqq, false);
			/*
			 * bfqq emptied. In normal operation, when
			 * bfqq is empty, bfqq->entity.service and
			 * bfqq->entity.budget must contain,
			 * respectively, the service received and the
			 * budget used last time bfqq emptied. These
			 * facts do not hold in this case, as at least
			 * this last removal occurred while bfqq is
			 * not in service. To avoid inconsistencies,
			 * reset both bfqq->entity.service and
			 * bfqq->entity.budget, if bfqq has still a
			 * process that may issue I/O requests to it.
			 */
			bfqq->entity.budget = bfqq->entity.service = 0;
		}

		/*
		 * Remove queue from request-position tree as it is empty.
		 */
		if (bfqq->pos_root) {
			rb_erase(&bfqq->pos_node, bfqq->pos_root);
			bfqq->pos_root = NULL;
		}
	}

	if (rq->cmd_flags & REQ_META)
		bfqq->meta_pending--;

	bfqg_stats_update_io_remove(bfqq_group(bfqq), rq->cmd_flags);
}

static bool bfq_bio_merge(struct blk_mq_hw_ctx *hctx, struct bio *bio)
{
	struct request_queue *q = hctx->queue;
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct request *free = NULL;
	/*
	 * bfq_bic_lookup grabs the queue_lock: invoke it now and
	 * store its return value for later use, to avoid nesting
	 * queue_lock inside the bfqd->lock. We assume that the bic
	 * returned by bfq_bic_lookup does not go away before
	 * bfqd->lock is taken.
	 */
	struct bfq_io_cq *bic = bfq_bic_lookup(bfqd, current->io_context, q);
	bool ret;

	spin_lock_irq(&bfqd->lock);

	if (bic)
		bfqd->bio_bfqq = bic_to_bfqq(bic, op_is_sync(bio->bi_opf));
	else
		bfqd->bio_bfqq = NULL;
	bfqd->bio_bic = bic;

	ret = blk_mq_sched_try_merge(q, bio, &free);

	if (free)
		blk_mq_free_request(free);
	spin_unlock_irq(&bfqd->lock);

	return ret;
}

static int bfq_request_merge(struct request_queue *q, struct request **req,
			     struct bio *bio)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct request *__rq;

	__rq = bfq_find_rq_fmerge(bfqd, bio, q);
	if (__rq && elv_bio_merge_ok(__rq, bio)) {
		*req = __rq;
		return ELEVATOR_FRONT_MERGE;
	}

	return ELEVATOR_NO_MERGE;
}

static void bfq_request_merged(struct request_queue *q, struct request *req,
			       enum elv_merge type)
{
	if (type == ELEVATOR_FRONT_MERGE &&
	    rb_prev(&req->rb_node) &&
	    blk_rq_pos(req) <
	    blk_rq_pos(container_of(rb_prev(&req->rb_node),
				    struct request, rb_node))) {
		struct bfq_queue *bfqq = RQ_BFQQ(req);
		struct bfq_data *bfqd = bfqq->bfqd;
		struct request *prev, *next_rq;

		/* Reposition request in its sort_list */
		elv_rb_del(&bfqq->sort_list, req);
		elv_rb_add(&bfqq->sort_list, req);

		/* Choose next request to be served for bfqq */
		prev = bfqq->next_rq;
		next_rq = bfq_choose_req(bfqd, bfqq->next_rq, req,
					 bfqd->last_position);
		bfqq->next_rq = next_rq;
		/*
		 * If next_rq changes, update both the queue's budget to
		 * fit the new request and the queue's position in its
		 * rq_pos_tree.
		 */
		if (prev != bfqq->next_rq) {
			bfq_updated_next_req(bfqd, bfqq);
			bfq_pos_tree_add_move(bfqd, bfqq);
		}
	}
}

static void bfq_requests_merged(struct request_queue *q, struct request *rq,
				struct request *next)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq), *next_bfqq = RQ_BFQQ(next);

	if (!RB_EMPTY_NODE(&rq->rb_node))
		goto end;
	spin_lock_irq(&bfqq->bfqd->lock);

	/*
	 * If next and rq belong to the same bfq_queue and next is older
	 * than rq, then reposition rq in the fifo (by substituting next
	 * with rq). Otherwise, if next and rq belong to different
	 * bfq_queues, never reposition rq: in fact, we would have to
	 * reposition it with respect to next's position in its own fifo,
	 * which would most certainly be too expensive with respect to
	 * the benefits.
	 */
	if (bfqq == next_bfqq &&
	    !list_empty(&rq->queuelist) && !list_empty(&next->queuelist) &&
	    next->fifo_time < rq->fifo_time) {
		list_del_init(&rq->queuelist);
		list_replace_init(&next->queuelist, &rq->queuelist);
		rq->fifo_time = next->fifo_time;
	}

	if (bfqq->next_rq == next)
		bfqq->next_rq = rq;

	bfq_remove_request(q, next);

	spin_unlock_irq(&bfqq->bfqd->lock);
end:
	bfqg_stats_update_io_merged(bfqq_group(bfqq), next->cmd_flags);
}

/* Must be called with bfqq != NULL */
static void bfq_bfqq_end_wr(struct bfq_queue *bfqq)
{
	if (bfq_bfqq_busy(bfqq))
		bfqq->bfqd->wr_busy_queues--;
	bfqq->wr_coeff = 1;
	bfqq->wr_cur_max_time = 0;
	bfqq->last_wr_start_finish = jiffies;
	/*
	 * Trigger a weight change on the next invocation of
	 * __bfq_entity_update_weight_prio.
	 */
	bfqq->entity.prio_changed = 1;
}

static void bfq_end_wr_async_queues(struct bfq_data *bfqd,
				    struct bfq_group *bfqg)
{
	int i, j;

	for (i = 0; i < 2; i++)
		for (j = 0; j < IOPRIO_BE_NR; j++)
			if (bfqg->async_bfqq[i][j])
				bfq_bfqq_end_wr(bfqg->async_bfqq[i][j]);
	if (bfqg->async_idle_bfqq)
		bfq_bfqq_end_wr(bfqg->async_idle_bfqq);
}

static void bfq_end_wr(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq;

	spin_lock_irq(&bfqd->lock);

	list_for_each_entry(bfqq, &bfqd->active_list, bfqq_list)
		bfq_bfqq_end_wr(bfqq);
	list_for_each_entry(bfqq, &bfqd->idle_list, bfqq_list)
		bfq_bfqq_end_wr(bfqq);
	bfq_end_wr_async(bfqd);

	spin_unlock_irq(&bfqd->lock);
}

static sector_t bfq_io_struct_pos(void *io_struct, bool request)
{
	if (request)
		return blk_rq_pos(io_struct);
	else
		return ((struct bio *)io_struct)->bi_iter.bi_sector;
}

static int bfq_rq_close_to_sector(void *io_struct, bool request,
				  sector_t sector)
{
	return abs(bfq_io_struct_pos(io_struct, request) - sector) <=
	       BFQQ_CLOSE_THR;
}

static struct bfq_queue *bfqq_find_close(struct bfq_data *bfqd,
					 struct bfq_queue *bfqq,
					 sector_t sector)
{
	struct rb_root *root = &bfq_bfqq_to_bfqg(bfqq)->rq_pos_tree;
	struct rb_node *parent, *node;
	struct bfq_queue *__bfqq;

	if (RB_EMPTY_ROOT(root))
		return NULL;

	/*
	 * First, if we find a request starting at the end of the last
	 * request, choose it.
	 */
	__bfqq = bfq_rq_pos_tree_lookup(bfqd, root, sector, &parent, NULL);
	if (__bfqq)
		return __bfqq;

	/*
	 * If the exact sector wasn't found, the parent of the NULL leaf
	 * will contain the closest sector (rq_pos_tree sorted by
	 * next_request position).
	 */
	__bfqq = rb_entry(parent, struct bfq_queue, pos_node);
	if (bfq_rq_close_to_sector(__bfqq->next_rq, true, sector))
		return __bfqq;

	if (blk_rq_pos(__bfqq->next_rq) < sector)
		node = rb_next(&__bfqq->pos_node);
	else
		node = rb_prev(&__bfqq->pos_node);
	if (!node)
		return NULL;

	__bfqq = rb_entry(node, struct bfq_queue, pos_node);
	if (bfq_rq_close_to_sector(__bfqq->next_rq, true, sector))
		return __bfqq;

	return NULL;
}

static struct bfq_queue *bfq_find_close_cooperator(struct bfq_data *bfqd,
						   struct bfq_queue *cur_bfqq,
						   sector_t sector)
{
	struct bfq_queue *bfqq;

	/*
	 * We shall notice if some of the queues are cooperating,
	 * e.g., working closely on the same area of the device. In
	 * that case, we can group them together and: 1) don't waste
	 * time idling, and 2) serve the union of their requests in
	 * the best possible order for throughput.
	 */
	bfqq = bfqq_find_close(bfqd, cur_bfqq, sector);
	if (!bfqq || bfqq == cur_bfqq)
		return NULL;

	return bfqq;
}

static struct bfq_queue *
bfq_setup_merge(struct bfq_queue *bfqq, struct bfq_queue *new_bfqq)
{
	int process_refs, new_process_refs;
	struct bfq_queue *__bfqq;

	/*
	 * If there are no process references on the new_bfqq, then it is
	 * unsafe to follow the ->new_bfqq chain as other bfqq's in the chain
	 * may have dropped their last reference (not just their last process
	 * reference).
	 */
	if (!bfqq_process_refs(new_bfqq))
		return NULL;

	/* Avoid a circular list and skip interim queue merges. */
	while ((__bfqq = new_bfqq->new_bfqq)) {
		if (__bfqq == bfqq)
			return NULL;
		new_bfqq = __bfqq;
	}

	process_refs = bfqq_process_refs(bfqq);
	new_process_refs = bfqq_process_refs(new_bfqq);
	/*
	 * If the process for the bfqq has gone away, there is no
	 * sense in merging the queues.
	 */
	if (process_refs == 0 || new_process_refs == 0)
		return NULL;

	bfq_log_bfqq(bfqq->bfqd, bfqq, "scheduling merge with queue %d",
		new_bfqq->pid);

	/*
	 * Merging is just a redirection: the requests of the process
	 * owning one of the two queues are redirected to the other queue.
	 * The latter queue, in its turn, is set as shared if this is the
	 * first time that the requests of some process are redirected to
	 * it.
	 *
	 * We redirect bfqq to new_bfqq and not the opposite, because
	 * we are in the context of the process owning bfqq, thus we
	 * have the io_cq of this process. So we can immediately
	 * configure this io_cq to redirect the requests of the
	 * process to new_bfqq. In contrast, the io_cq of new_bfqq is
	 * not available any more (new_bfqq->bic == NULL).
	 *
	 * Anyway, even in case new_bfqq coincides with the in-service
	 * queue, redirecting requests the in-service queue is the
	 * best option, as we feed the in-service queue with new
	 * requests close to the last request served and, by doing so,
	 * are likely to increase the throughput.
	 */
	bfqq->new_bfqq = new_bfqq;
	new_bfqq->ref += process_refs;
	return new_bfqq;
}

static bool bfq_may_be_close_cooperator(struct bfq_queue *bfqq,
					struct bfq_queue *new_bfqq)
{
	if (bfq_class_idle(bfqq) || bfq_class_idle(new_bfqq) ||
	    (bfqq->ioprio_class != new_bfqq->ioprio_class))
		return false;

	/*
	 * If either of the queues has already been detected as seeky,
	 * then merging it with the other queue is unlikely to lead to
	 * sequential I/O.
	 */
	if (BFQQ_SEEKY(bfqq) || BFQQ_SEEKY(new_bfqq))
		return false;

	/*
	 * Interleaved I/O is known to be done by (some) applications
	 * only for reads, so it does not make sense to merge async
	 * queues.
	 */
	if (!bfq_bfqq_sync(bfqq) || !bfq_bfqq_sync(new_bfqq))
		return false;

	return true;
}

/*
 * If this function returns true, then bfqq cannot be merged. The idea
 * is that true cooperation happens very early after processes start
 * to do I/O. Usually, late cooperations are just accidental false
 * positives. In case bfqq is weight-raised, such false positives
 * would evidently degrade latency guarantees for bfqq.
 */
static bool wr_from_too_long(struct bfq_queue *bfqq)
{
	return bfqq->wr_coeff > 1 &&
		time_is_before_jiffies(bfqq->last_wr_start_finish +
				       msecs_to_jiffies(100));
}

/*
 * Attempt to schedule a merge of bfqq with the currently in-service
 * queue or with a close queue among the scheduled queues.  Return
 * NULL if no merge was scheduled, a pointer to the shared bfq_queue
 * structure otherwise.
 *
 * The OOM queue is not allowed to participate to cooperation: in fact, since
 * the requests temporarily redirected to the OOM queue could be redirected
 * again to dedicated queues at any time, the state needed to correctly
 * handle merging with the OOM queue would be quite complex and expensive
 * to maintain. Besides, in such a critical condition as an out of memory,
 * the benefits of queue merging may be little relevant, or even negligible.
 *
 * Weight-raised queues can be merged only if their weight-raising
 * period has just started. In fact cooperating processes are usually
 * started together. Thus, with this filter we avoid false positives
 * that would jeopardize low-latency guarantees.
 *
 * WARNING: queue merging may impair fairness among non-weight raised
 * queues, for at least two reasons: 1) the original weight of a
 * merged queue may change during the merged state, 2) even being the
 * weight the same, a merged queue may be bloated with many more
 * requests than the ones produced by its originally-associated
 * process.
 */
static struct bfq_queue *
bfq_setup_cooperator(struct bfq_data *bfqd, struct bfq_queue *bfqq,
		     void *io_struct, bool request)
{
	struct bfq_queue *in_service_bfqq, *new_bfqq;

	if (bfqq->new_bfqq)
		return bfqq->new_bfqq;

	if (!io_struct ||
	    wr_from_too_long(bfqq) ||
	    unlikely(bfqq == &bfqd->oom_bfqq))
		return NULL;

	/* If there is only one backlogged queue, don't search. */
	if (bfqd->busy_queues == 1)
		return NULL;

	in_service_bfqq = bfqd->in_service_queue;

	if (!in_service_bfqq || in_service_bfqq == bfqq
	    || wr_from_too_long(in_service_bfqq) ||
	    unlikely(in_service_bfqq == &bfqd->oom_bfqq))
		goto check_scheduled;

	if (bfq_rq_close_to_sector(io_struct, request, bfqd->last_position) &&
	    bfqq->entity.parent == in_service_bfqq->entity.parent &&
	    bfq_may_be_close_cooperator(bfqq, in_service_bfqq)) {
		new_bfqq = bfq_setup_merge(bfqq, in_service_bfqq);
		if (new_bfqq)
			return new_bfqq;
	}
	/*
	 * Check whether there is a cooperator among currently scheduled
	 * queues. The only thing we need is that the bio/request is not
	 * NULL, as we need it to establish whether a cooperator exists.
	 */
check_scheduled:
	new_bfqq = bfq_find_close_cooperator(bfqd, bfqq,
			bfq_io_struct_pos(io_struct, request));

	if (new_bfqq && !wr_from_too_long(new_bfqq) &&
	    likely(new_bfqq != &bfqd->oom_bfqq) &&
	    bfq_may_be_close_cooperator(bfqq, new_bfqq))
		return bfq_setup_merge(bfqq, new_bfqq);

	return NULL;
}

static void bfq_bfqq_save_state(struct bfq_queue *bfqq)
{
	struct bfq_io_cq *bic = bfqq->bic;

	/*
	 * If !bfqq->bic, the queue is already shared or its requests
	 * have already been redirected to a shared queue; both idle window
	 * and weight raising state have already been saved. Do nothing.
	 */
	if (!bic)
		return;

	bic->saved_ttime = bfqq->ttime;
	bic->saved_idle_window = bfq_bfqq_idle_window(bfqq);
	bic->saved_IO_bound = bfq_bfqq_IO_bound(bfqq);
	bic->saved_in_large_burst = bfq_bfqq_in_large_burst(bfqq);
	bic->was_in_burst_list = !hlist_unhashed(&bfqq->burst_list_node);
	bic->saved_wr_coeff = bfqq->wr_coeff;
	bic->saved_wr_start_at_switch_to_srt = bfqq->wr_start_at_switch_to_srt;
	bic->saved_last_wr_start_finish = bfqq->last_wr_start_finish;
	bic->saved_wr_cur_max_time = bfqq->wr_cur_max_time;
}

static void
bfq_merge_bfqqs(struct bfq_data *bfqd, struct bfq_io_cq *bic,
		struct bfq_queue *bfqq, struct bfq_queue *new_bfqq)
{
	bfq_log_bfqq(bfqd, bfqq, "merging with queue %lu",
		(unsigned long)new_bfqq->pid);
	/* Save weight raising and idle window of the merged queues */
	bfq_bfqq_save_state(bfqq);
	bfq_bfqq_save_state(new_bfqq);
	if (bfq_bfqq_IO_bound(bfqq))
		bfq_mark_bfqq_IO_bound(new_bfqq);
	bfq_clear_bfqq_IO_bound(bfqq);

	/*
	 * If bfqq is weight-raised, then let new_bfqq inherit
	 * weight-raising. To reduce false positives, neglect the case
	 * where bfqq has just been created, but has not yet made it
	 * to be weight-raised (which may happen because EQM may merge
	 * bfqq even before bfq_add_request is executed for the first
	 * time for bfqq). Handling this case would however be very
	 * easy, thanks to the flag just_created.
	 */
	if (new_bfqq->wr_coeff == 1 && bfqq->wr_coeff > 1) {
		new_bfqq->wr_coeff = bfqq->wr_coeff;
		new_bfqq->wr_cur_max_time = bfqq->wr_cur_max_time;
		new_bfqq->last_wr_start_finish = bfqq->last_wr_start_finish;
		new_bfqq->wr_start_at_switch_to_srt =
			bfqq->wr_start_at_switch_to_srt;
		if (bfq_bfqq_busy(new_bfqq))
			bfqd->wr_busy_queues++;
		new_bfqq->entity.prio_changed = 1;
	}

	if (bfqq->wr_coeff > 1) { /* bfqq has given its wr to new_bfqq */
		bfqq->wr_coeff = 1;
		bfqq->entity.prio_changed = 1;
		if (bfq_bfqq_busy(bfqq))
			bfqd->wr_busy_queues--;
	}

	bfq_log_bfqq(bfqd, new_bfqq, "merge_bfqqs: wr_busy %d",
		     bfqd->wr_busy_queues);

	/*
	 * Merge queues (that is, let bic redirect its requests to new_bfqq)
	 */
	bic_set_bfqq(bic, new_bfqq, 1);
	bfq_mark_bfqq_coop(new_bfqq);
	/*
	 * new_bfqq now belongs to at least two bics (it is a shared queue):
	 * set new_bfqq->bic to NULL. bfqq either:
	 * - does not belong to any bic any more, and hence bfqq->bic must
	 *   be set to NULL, or
	 * - is a queue whose owning bics have already been redirected to a
	 *   different queue, hence the queue is destined to not belong to
	 *   any bic soon and bfqq->bic is already NULL (therefore the next
	 *   assignment causes no harm).
	 */
	new_bfqq->bic = NULL;
	bfqq->bic = NULL;
	/* release process reference to bfqq */
	bfq_put_queue(bfqq);
}

static bool bfq_allow_bio_merge(struct request_queue *q, struct request *rq,
				struct bio *bio)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	bool is_sync = op_is_sync(bio->bi_opf);
	struct bfq_queue *bfqq = bfqd->bio_bfqq, *new_bfqq;

	/*
	 * Disallow merge of a sync bio into an async request.
	 */
	if (is_sync && !rq_is_sync(rq))
		return false;

	/*
	 * Lookup the bfqq that this bio will be queued with. Allow
	 * merge only if rq is queued there.
	 */
	if (!bfqq)
		return false;

	/*
	 * We take advantage of this function to perform an early merge
	 * of the queues of possible cooperating processes.
	 */
	new_bfqq = bfq_setup_cooperator(bfqd, bfqq, bio, false);
	if (new_bfqq) {
		/*
		 * bic still points to bfqq, then it has not yet been
		 * redirected to some other bfq_queue, and a queue
		 * merge beween bfqq and new_bfqq can be safely
		 * fulfillled, i.e., bic can be redirected to new_bfqq
		 * and bfqq can be put.
		 */
		bfq_merge_bfqqs(bfqd, bfqd->bio_bic, bfqq,
				new_bfqq);
		/*
		 * If we get here, bio will be queued into new_queue,
		 * so use new_bfqq to decide whether bio and rq can be
		 * merged.
		 */
		bfqq = new_bfqq;

		/*
		 * Change also bqfd->bio_bfqq, as
		 * bfqd->bio_bic now points to new_bfqq, and
		 * this function may be invoked again (and then may
		 * use again bqfd->bio_bfqq).
		 */
		bfqd->bio_bfqq = bfqq;
	}

	return bfqq == RQ_BFQQ(rq);
}

/*
 * Set the maximum time for the in-service queue to consume its
 * budget. This prevents seeky processes from lowering the throughput.
 * In practice, a time-slice service scheme is used with seeky
 * processes.
 */
static void bfq_set_budget_timeout(struct bfq_data *bfqd,
				   struct bfq_queue *bfqq)
{
	unsigned int timeout_coeff;

	if (bfqq->wr_cur_max_time == bfqd->bfq_wr_rt_max_time)
		timeout_coeff = 1;
	else
		timeout_coeff = bfqq->entity.weight / bfqq->entity.orig_weight;

	bfqd->last_budget_start = ktime_get();

	bfqq->budget_timeout = jiffies +
		bfqd->bfq_timeout * timeout_coeff;
}

static void __bfq_set_in_service_queue(struct bfq_data *bfqd,
				       struct bfq_queue *bfqq)
{
	if (bfqq) {
		bfqg_stats_update_avg_queue_size(bfqq_group(bfqq));
		bfq_clear_bfqq_fifo_expire(bfqq);

		bfqd->budgets_assigned = (bfqd->budgets_assigned * 7 + 256) / 8;

		if (time_is_before_jiffies(bfqq->last_wr_start_finish) &&
		    bfqq->wr_coeff > 1 &&
		    bfqq->wr_cur_max_time == bfqd->bfq_wr_rt_max_time &&
		    time_is_before_jiffies(bfqq->budget_timeout)) {
			/*
			 * For soft real-time queues, move the start
			 * of the weight-raising period forward by the
			 * time the queue has not received any
			 * service. Otherwise, a relatively long
			 * service delay is likely to cause the
			 * weight-raising period of the queue to end,
			 * because of the short duration of the
			 * weight-raising period of a soft real-time
			 * queue.  It is worth noting that this move
			 * is not so dangerous for the other queues,
			 * because soft real-time queues are not
			 * greedy.
			 *
			 * To not add a further variable, we use the
			 * overloaded field budget_timeout to
			 * determine for how long the queue has not
			 * received service, i.e., how much time has
			 * elapsed since the queue expired. However,
			 * this is a little imprecise, because
			 * budget_timeout is set to jiffies if bfqq
			 * not only expires, but also remains with no
			 * request.
			 */
			if (time_after(bfqq->budget_timeout,
				       bfqq->last_wr_start_finish))
				bfqq->last_wr_start_finish +=
					jiffies - bfqq->budget_timeout;
			else
				bfqq->last_wr_start_finish = jiffies;
		}

		bfq_set_budget_timeout(bfqd, bfqq);
		bfq_log_bfqq(bfqd, bfqq,
			     "set_in_service_queue, cur-budget = %d",
			     bfqq->entity.budget);
	}

	bfqd->in_service_queue = bfqq;
}

/*
 * Get and set a new queue for service.
 */
static struct bfq_queue *bfq_set_in_service_queue(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq = bfq_get_next_queue(bfqd);

	__bfq_set_in_service_queue(bfqd, bfqq);
	return bfqq;
}

static void bfq_arm_slice_timer(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq = bfqd->in_service_queue;
	u32 sl;

	bfq_mark_bfqq_wait_request(bfqq);

	/*
	 * We don't want to idle for seeks, but we do want to allow
	 * fair distribution of slice time for a process doing back-to-back
	 * seeks. So allow a little bit of time for him to submit a new rq.
	 */
	sl = bfqd->bfq_slice_idle;
	/*
	 * Unless the queue is being weight-raised or the scenario is
	 * asymmetric, grant only minimum idle time if the queue
	 * is seeky. A long idling is preserved for a weight-raised
	 * queue, or, more in general, in an asymmetric scenario,
	 * because a long idling is needed for guaranteeing to a queue
	 * its reserved share of the throughput (in particular, it is
	 * needed if the queue has a higher weight than some other
	 * queue).
	 */
	if (BFQQ_SEEKY(bfqq) && bfqq->wr_coeff == 1 &&
	    bfq_symmetric_scenario(bfqd))
		sl = min_t(u64, sl, BFQ_MIN_TT);

	bfqd->last_idling_start = ktime_get();
	hrtimer_start(&bfqd->idle_slice_timer, ns_to_ktime(sl),
		      HRTIMER_MODE_REL);
	bfqg_stats_set_start_idle_time(bfqq_group(bfqq));
}

/*
 * In autotuning mode, max_budget is dynamically recomputed as the
 * amount of sectors transferred in timeout at the estimated peak
 * rate. This enables BFQ to utilize a full timeslice with a full
 * budget, even if the in-service queue is served at peak rate. And
 * this maximises throughput with sequential workloads.
 */
static unsigned long bfq_calc_max_budget(struct bfq_data *bfqd)
{
	return (u64)bfqd->peak_rate * USEC_PER_MSEC *
		jiffies_to_msecs(bfqd->bfq_timeout)>>BFQ_RATE_SHIFT;
}

/*
 * Update parameters related to throughput and responsiveness, as a
 * function of the estimated peak rate. See comments on
 * bfq_calc_max_budget(), and on T_slow and T_fast arrays.
 */
static void update_thr_responsiveness_params(struct bfq_data *bfqd)
{
	int dev_type = blk_queue_nonrot(bfqd->queue);

	if (bfqd->bfq_user_max_budget == 0)
		bfqd->bfq_max_budget =
			bfq_calc_max_budget(bfqd);

	if (bfqd->device_speed == BFQ_BFQD_FAST &&
	    bfqd->peak_rate < device_speed_thresh[dev_type]) {
		bfqd->device_speed = BFQ_BFQD_SLOW;
		bfqd->RT_prod = R_slow[dev_type] *
			T_slow[dev_type];
	} else if (bfqd->device_speed == BFQ_BFQD_SLOW &&
		   bfqd->peak_rate > device_speed_thresh[dev_type]) {
		bfqd->device_speed = BFQ_BFQD_FAST;
		bfqd->RT_prod = R_fast[dev_type] *
			T_fast[dev_type];
	}

	bfq_log(bfqd,
"dev_type %s dev_speed_class = %s (%llu sects/sec), thresh %llu setcs/sec",
		dev_type == 0 ? "ROT" : "NONROT",
		bfqd->device_speed == BFQ_BFQD_FAST ? "FAST" : "SLOW",
		bfqd->device_speed == BFQ_BFQD_FAST ?
		(USEC_PER_SEC*(u64)R_fast[dev_type])>>BFQ_RATE_SHIFT :
		(USEC_PER_SEC*(u64)R_slow[dev_type])>>BFQ_RATE_SHIFT,
		(USEC_PER_SEC*(u64)device_speed_thresh[dev_type])>>
		BFQ_RATE_SHIFT);
}

static void bfq_reset_rate_computation(struct bfq_data *bfqd,
				       struct request *rq)
{
	if (rq != NULL) { /* new rq dispatch now, reset accordingly */
		bfqd->last_dispatch = bfqd->first_dispatch = ktime_get_ns();
		bfqd->peak_rate_samples = 1;
		bfqd->sequential_samples = 0;
		bfqd->tot_sectors_dispatched = bfqd->last_rq_max_size =
			blk_rq_sectors(rq);
	} else /* no new rq dispatched, just reset the number of samples */
		bfqd->peak_rate_samples = 0; /* full re-init on next disp. */

	bfq_log(bfqd,
		"reset_rate_computation at end, sample %u/%u tot_sects %llu",
		bfqd->peak_rate_samples, bfqd->sequential_samples,
		bfqd->tot_sectors_dispatched);
}

static void bfq_update_rate_reset(struct bfq_data *bfqd, struct request *rq)
{
	u32 rate, weight, divisor;

	/*
	 * For the convergence property to hold (see comments on
	 * bfq_update_peak_rate()) and for the assessment to be
	 * reliable, a minimum number of samples must be present, and
	 * a minimum amount of time must have elapsed. If not so, do
	 * not compute new rate. Just reset parameters, to get ready
	 * for a new evaluation attempt.
	 */
	if (bfqd->peak_rate_samples < BFQ_RATE_MIN_SAMPLES ||
	    bfqd->delta_from_first < BFQ_RATE_MIN_INTERVAL)
		goto reset_computation;

	/*
	 * If a new request completion has occurred after last
	 * dispatch, then, to approximate the rate at which requests
	 * have been served by the device, it is more precise to
	 * extend the observation interval to the last completion.
	 */
	bfqd->delta_from_first =
		max_t(u64, bfqd->delta_from_first,
		      bfqd->last_completion - bfqd->first_dispatch);

	/*
	 * Rate computed in sects/usec, and not sects/nsec, for
	 * precision issues.
	 */
	rate = div64_ul(bfqd->tot_sectors_dispatched<<BFQ_RATE_SHIFT,
			div_u64(bfqd->delta_from_first, NSEC_PER_USEC));

	/*
	 * Peak rate not updated if:
	 * - the percentage of sequential dispatches is below 3/4 of the
	 *   total, and rate is below the current estimated peak rate
	 * - rate is unreasonably high (> 20M sectors/sec)
	 */
	if ((bfqd->sequential_samples < (3 * bfqd->peak_rate_samples)>>2 &&
	     rate <= bfqd->peak_rate) ||
		rate > 20<<BFQ_RATE_SHIFT)
		goto reset_computation;

	/*
	 * We have to update the peak rate, at last! To this purpose,
	 * we use a low-pass filter. We compute the smoothing constant
	 * of the filter as a function of the 'weight' of the new
	 * measured rate.
	 *
	 * As can be seen in next formulas, we define this weight as a
	 * quantity proportional to how sequential the workload is,
	 * and to how long the observation time interval is.
	 *
	 * The weight runs from 0 to 8. The maximum value of the
	 * weight, 8, yields the minimum value for the smoothing
	 * constant. At this minimum value for the smoothing constant,
	 * the measured rate contributes for half of the next value of
	 * the estimated peak rate.
	 *
	 * So, the first step is to compute the weight as a function
	 * of how sequential the workload is. Note that the weight
	 * cannot reach 9, because bfqd->sequential_samples cannot
	 * become equal to bfqd->peak_rate_samples, which, in its
	 * turn, holds true because bfqd->sequential_samples is not
	 * incremented for the first sample.
	 */
	weight = (9 * bfqd->sequential_samples) / bfqd->peak_rate_samples;

	/*
	 * Second step: further refine the weight as a function of the
	 * duration of the observation interval.
	 */
	weight = min_t(u32, 8,
		       div_u64(weight * bfqd->delta_from_first,
			       BFQ_RATE_REF_INTERVAL));

	/*
	 * Divisor ranging from 10, for minimum weight, to 2, for
	 * maximum weight.
	 */
	divisor = 10 - weight;

	/*
	 * Finally, update peak rate:
	 *
	 * peak_rate = peak_rate * (divisor-1) / divisor  +  rate / divisor
	 */
	bfqd->peak_rate *= divisor-1;
	bfqd->peak_rate /= divisor;
	rate /= divisor; /* smoothing constant alpha = 1/divisor */

	bfqd->peak_rate += rate;
	update_thr_responsiveness_params(bfqd);

reset_computation:
	bfq_reset_rate_computation(bfqd, rq);
}

/*
 * Update the read/write peak rate (the main quantity used for
 * auto-tuning, see update_thr_responsiveness_params()).
 *
 * It is not trivial to estimate the peak rate (correctly): because of
 * the presence of sw and hw queues between the scheduler and the
 * device components that finally serve I/O requests, it is hard to
 * say exactly when a given dispatched request is served inside the
 * device, and for how long. As a consequence, it is hard to know
 * precisely at what rate a given set of requests is actually served
 * by the device.
 *
 * On the opposite end, the dispatch time of any request is trivially
 * available, and, from this piece of information, the "dispatch rate"
 * of requests can be immediately computed. So, the idea in the next
 * function is to use what is known, namely request dispatch times
 * (plus, when useful, request completion times), to estimate what is
 * unknown, namely in-device request service rate.
 *
 * The main issue is that, because of the above facts, the rate at
 * which a certain set of requests is dispatched over a certain time
 * interval can vary greatly with respect to the rate at which the
 * same requests are then served. But, since the size of any
 * intermediate queue is limited, and the service scheme is lossless
 * (no request is silently dropped), the following obvious convergence
 * property holds: the number of requests dispatched MUST become
 * closer and closer to the number of requests completed as the
 * observation interval grows. This is the key property used in
 * the next function to estimate the peak service rate as a function
 * of the observed dispatch rate. The function assumes to be invoked
 * on every request dispatch.
 */
static void bfq_update_peak_rate(struct bfq_data *bfqd, struct request *rq)
{
	u64 now_ns = ktime_get_ns();

	if (bfqd->peak_rate_samples == 0) { /* first dispatch */
		bfq_log(bfqd, "update_peak_rate: goto reset, samples %d",
			bfqd->peak_rate_samples);
		bfq_reset_rate_computation(bfqd, rq);
		goto update_last_values; /* will add one sample */
	}

	/*
	 * Device idle for very long: the observation interval lasting
	 * up to this dispatch cannot be a valid observation interval
	 * for computing a new peak rate (similarly to the late-
	 * completion event in bfq_completed_request()). Go to
	 * update_rate_and_reset to have the following three steps
	 * taken:
	 * - close the observation interval at the last (previous)
	 *   request dispatch or completion
	 * - compute rate, if possible, for that observation interval
	 * - start a new observation interval with this dispatch
	 */
	if (now_ns - bfqd->last_dispatch > 100*NSEC_PER_MSEC &&
	    bfqd->rq_in_driver == 0)
		goto update_rate_and_reset;

	/* Update sampling information */
	bfqd->peak_rate_samples++;

	if ((bfqd->rq_in_driver > 0 ||
		now_ns - bfqd->last_completion < BFQ_MIN_TT)
	     && get_sdist(bfqd->last_position, rq) < BFQQ_SEEK_THR)
		bfqd->sequential_samples++;

	bfqd->tot_sectors_dispatched += blk_rq_sectors(rq);

	/* Reset max observed rq size every 32 dispatches */
	if (likely(bfqd->peak_rate_samples % 32))
		bfqd->last_rq_max_size =
			max_t(u32, blk_rq_sectors(rq), bfqd->last_rq_max_size);
	else
		bfqd->last_rq_max_size = blk_rq_sectors(rq);

	bfqd->delta_from_first = now_ns - bfqd->first_dispatch;

	/* Target observation interval not yet reached, go on sampling */
	if (bfqd->delta_from_first < BFQ_RATE_REF_INTERVAL)
		goto update_last_values;

update_rate_and_reset:
	bfq_update_rate_reset(bfqd, rq);
update_last_values:
	bfqd->last_position = blk_rq_pos(rq) + blk_rq_sectors(rq);
	bfqd->last_dispatch = now_ns;
}

/*
 * Remove request from internal lists.
 */
static void bfq_dispatch_remove(struct request_queue *q, struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);

	/*
	 * For consistency, the next instruction should have been
	 * executed after removing the request from the queue and
	 * dispatching it.  We execute instead this instruction before
	 * bfq_remove_request() (and hence introduce a temporary
	 * inconsistency), for efficiency.  In fact, should this
	 * dispatch occur for a non in-service bfqq, this anticipated
	 * increment prevents two counters related to bfqq->dispatched
	 * from risking to be, first, uselessly decremented, and then
	 * incremented again when the (new) value of bfqq->dispatched
	 * happens to be taken into account.
	 */
	bfqq->dispatched++;
	bfq_update_peak_rate(q->elevator->elevator_data, rq);

	bfq_remove_request(q, rq);
}

static void __bfq_bfqq_expire(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	/*
	 * If this bfqq is shared between multiple processes, check
	 * to make sure that those processes are still issuing I/Os
	 * within the mean seek distance. If not, it may be time to
	 * break the queues apart again.
	 */
	if (bfq_bfqq_coop(bfqq) && BFQQ_SEEKY(bfqq))
		bfq_mark_bfqq_split_coop(bfqq);

	if (RB_EMPTY_ROOT(&bfqq->sort_list)) {
		if (bfqq->dispatched == 0)
			/*
			 * Overloading budget_timeout field to store
			 * the time at which the queue remains with no
			 * backlog and no outstanding request; used by
			 * the weight-raising mechanism.
			 */
			bfqq->budget_timeout = jiffies;

		bfq_del_bfqq_busy(bfqd, bfqq, true);
	} else {
		bfq_requeue_bfqq(bfqd, bfqq);
		/*
		 * Resort priority tree of potential close cooperators.
		 */
		bfq_pos_tree_add_move(bfqd, bfqq);
	}

	/*
	 * All in-service entities must have been properly deactivated
	 * or requeued before executing the next function, which
	 * resets all in-service entites as no more in service.
	 */
	__bfq_bfqd_reset_in_service(bfqd);
}

/**
 * __bfq_bfqq_recalc_budget - try to adapt the budget to the @bfqq behavior.
 * @bfqd: device data.
 * @bfqq: queue to update.
 * @reason: reason for expiration.
 *
 * Handle the feedback on @bfqq budget at queue expiration.
 * See the body for detailed comments.
 */
static void __bfq_bfqq_recalc_budget(struct bfq_data *bfqd,
				     struct bfq_queue *bfqq,
				     enum bfqq_expiration reason)
{
	struct request *next_rq;
	int budget, min_budget;

	min_budget = bfq_min_budget(bfqd);

	if (bfqq->wr_coeff == 1)
		budget = bfqq->max_budget;
	else /*
	      * Use a constant, low budget for weight-raised queues,
	      * to help achieve a low latency. Keep it slightly higher
	      * than the minimum possible budget, to cause a little
	      * bit fewer expirations.
	      */
		budget = 2 * min_budget;

	bfq_log_bfqq(bfqd, bfqq, "recalc_budg: last budg %d, budg left %d",
		bfqq->entity.budget, bfq_bfqq_budget_left(bfqq));
	bfq_log_bfqq(bfqd, bfqq, "recalc_budg: last max_budg %d, min budg %d",
		budget, bfq_min_budget(bfqd));
	bfq_log_bfqq(bfqd, bfqq, "recalc_budg: sync %d, seeky %d",
		bfq_bfqq_sync(bfqq), BFQQ_SEEKY(bfqd->in_service_queue));

	if (bfq_bfqq_sync(bfqq) && bfqq->wr_coeff == 1) {
		switch (reason) {
		/*
		 * Caveat: in all the following cases we trade latency
		 * for throughput.
		 */
		case BFQQE_TOO_IDLE:
			/*
			 * This is the only case where we may reduce
			 * the budget: if there is no request of the
			 * process still waiting for completion, then
			 * we assume (tentatively) that the timer has
			 * expired because the batch of requests of
			 * the process could have been served with a
			 * smaller budget.  Hence, betting that
			 * process will behave in the same way when it
			 * becomes backlogged again, we reduce its
			 * next budget.  As long as we guess right,
			 * this budget cut reduces the latency
			 * experienced by the process.
			 *
			 * However, if there are still outstanding
			 * requests, then the process may have not yet
			 * issued its next request just because it is
			 * still waiting for the completion of some of
			 * the still outstanding ones.  So in this
			 * subcase we do not reduce its budget, on the
			 * contrary we increase it to possibly boost
			 * the throughput, as discussed in the
			 * comments to the BUDGET_TIMEOUT case.
			 */
			if (bfqq->dispatched > 0) /* still outstanding reqs */
				budget = min(budget * 2, bfqd->bfq_max_budget);
			else {
				if (budget > 5 * min_budget)
					budget -= 4 * min_budget;
				else
					budget = min_budget;
			}
			break;
		case BFQQE_BUDGET_TIMEOUT:
			/*
			 * We double the budget here because it gives
			 * the chance to boost the throughput if this
			 * is not a seeky process (and has bumped into
			 * this timeout because of, e.g., ZBR).
			 */
			budget = min(budget * 2, bfqd->bfq_max_budget);
			break;
		case BFQQE_BUDGET_EXHAUSTED:
			/*
			 * The process still has backlog, and did not
			 * let either the budget timeout or the disk
			 * idling timeout expire. Hence it is not
			 * seeky, has a short thinktime and may be
			 * happy with a higher budget too. So
			 * definitely increase the budget of this good
			 * candidate to boost the disk throughput.
			 */
			budget = min(budget * 4, bfqd->bfq_max_budget);
			break;
		case BFQQE_NO_MORE_REQUESTS:
			/*
			 * For queues that expire for this reason, it
			 * is particularly important to keep the
			 * budget close to the actual service they
			 * need. Doing so reduces the timestamp
			 * misalignment problem described in the
			 * comments in the body of
			 * __bfq_activate_entity. In fact, suppose
			 * that a queue systematically expires for
			 * BFQQE_NO_MORE_REQUESTS and presents a
			 * new request in time to enjoy timestamp
			 * back-shifting. The larger the budget of the
			 * queue is with respect to the service the
			 * queue actually requests in each service
			 * slot, the more times the queue can be
			 * reactivated with the same virtual finish
			 * time. It follows that, even if this finish
			 * time is pushed to the system virtual time
			 * to reduce the consequent timestamp
			 * misalignment, the queue unjustly enjoys for
			 * many re-activations a lower finish time
			 * than all newly activated queues.
			 *
			 * The service needed by bfqq is measured
			 * quite precisely by bfqq->entity.service.
			 * Since bfqq does not enjoy device idling,
			 * bfqq->entity.service is equal to the number
			 * of sectors that the process associated with
			 * bfqq requested to read/write before waiting
			 * for request completions, or blocking for
			 * other reasons.
			 */
			budget = max_t(int, bfqq->entity.service, min_budget);
			break;
		default:
			return;
		}
	} else if (!bfq_bfqq_sync(bfqq)) {
		/*
		 * Async queues get always the maximum possible
		 * budget, as for them we do not care about latency
		 * (in addition, their ability to dispatch is limited
		 * by the charging factor).
		 */
		budget = bfqd->bfq_max_budget;
	}

	bfqq->max_budget = budget;

	if (bfqd->budgets_assigned >= bfq_stats_min_budgets &&
	    !bfqd->bfq_user_max_budget)
		bfqq->max_budget = min(bfqq->max_budget, bfqd->bfq_max_budget);

	/*
	 * If there is still backlog, then assign a new budget, making
	 * sure that it is large enough for the next request.  Since
	 * the finish time of bfqq must be kept in sync with the
	 * budget, be sure to call __bfq_bfqq_expire() *after* this
	 * update.
	 *
	 * If there is no backlog, then no need to update the budget;
	 * it will be updated on the arrival of a new request.
	 */
	next_rq = bfqq->next_rq;
	if (next_rq)
		bfqq->entity.budget = max_t(unsigned long, bfqq->max_budget,
					    bfq_serv_to_charge(next_rq, bfqq));

	bfq_log_bfqq(bfqd, bfqq, "head sect: %u, new budget %d",
			next_rq ? blk_rq_sectors(next_rq) : 0,
			bfqq->entity.budget);
}

/*
 * Return true if the process associated with bfqq is "slow". The slow
 * flag is used, in addition to the budget timeout, to reduce the
 * amount of service provided to seeky processes, and thus reduce
 * their chances to lower the throughput. More details in the comments
 * on the function bfq_bfqq_expire().
 *
 * An important observation is in order: as discussed in the comments
 * on the function bfq_update_peak_rate(), with devices with internal
 * queues, it is hard if ever possible to know when and for how long
 * an I/O request is processed by the device (apart from the trivial
 * I/O pattern where a new request is dispatched only after the
 * previous one has been completed). This makes it hard to evaluate
 * the real rate at which the I/O requests of each bfq_queue are
 * served.  In fact, for an I/O scheduler like BFQ, serving a
 * bfq_queue means just dispatching its requests during its service
 * slot (i.e., until the budget of the queue is exhausted, or the
 * queue remains idle, or, finally, a timeout fires). But, during the
 * service slot of a bfq_queue, around 100 ms at most, the device may
 * be even still processing requests of bfq_queues served in previous
 * service slots. On the opposite end, the requests of the in-service
 * bfq_queue may be completed after the service slot of the queue
 * finishes.
 *
 * Anyway, unless more sophisticated solutions are used
 * (where possible), the sum of the sizes of the requests dispatched
 * during the service slot of a bfq_queue is probably the only
 * approximation available for the service received by the bfq_queue
 * during its service slot. And this sum is the quantity used in this
 * function to evaluate the I/O speed of a process.
 */
static bool bfq_bfqq_is_slow(struct bfq_data *bfqd, struct bfq_queue *bfqq,
				 bool compensate, enum bfqq_expiration reason,
				 unsigned long *delta_ms)
{
	ktime_t delta_ktime;
	u32 delta_usecs;
	bool slow = BFQQ_SEEKY(bfqq); /* if delta too short, use seekyness */

	if (!bfq_bfqq_sync(bfqq))
		return false;

	if (compensate)
		delta_ktime = bfqd->last_idling_start;
	else
		delta_ktime = ktime_get();
	delta_ktime = ktime_sub(delta_ktime, bfqd->last_budget_start);
	delta_usecs = ktime_to_us(delta_ktime);

	/* don't use too short time intervals */
	if (delta_usecs < 1000) {
		if (blk_queue_nonrot(bfqd->queue))
			 /*
			  * give same worst-case guarantees as idling
			  * for seeky
			  */
			*delta_ms = BFQ_MIN_TT / NSEC_PER_MSEC;
		else /* charge at least one seek */
			*delta_ms = bfq_slice_idle / NSEC_PER_MSEC;

		return slow;
	}

	*delta_ms = delta_usecs / USEC_PER_MSEC;

	/*
	 * Use only long (> 20ms) intervals to filter out excessive
	 * spikes in service rate estimation.
	 */
	if (delta_usecs > 20000) {
		/*
		 * Caveat for rotational devices: processes doing I/O
		 * in the slower disk zones tend to be slow(er) even
		 * if not seeky. In this respect, the estimated peak
		 * rate is likely to be an average over the disk
		 * surface. Accordingly, to not be too harsh with
		 * unlucky processes, a process is deemed slow only if
		 * its rate has been lower than half of the estimated
		 * peak rate.
		 */
		slow = bfqq->entity.service < bfqd->bfq_max_budget / 2;
	}

	bfq_log_bfqq(bfqd, bfqq, "bfq_bfqq_is_slow: slow %d", slow);

	return slow;
}

/*
 * To be deemed as soft real-time, an application must meet two
 * requirements. First, the application must not require an average
 * bandwidth higher than the approximate bandwidth required to playback or
 * record a compressed high-definition video.
 * The next function is invoked on the completion of the last request of a
 * batch, to compute the next-start time instant, soft_rt_next_start, such
 * that, if the next request of the application does not arrive before
 * soft_rt_next_start, then the above requirement on the bandwidth is met.
 *
 * The second requirement is that the request pattern of the application is
 * isochronous, i.e., that, after issuing a request or a batch of requests,
 * the application stops issuing new requests until all its pending requests
 * have been completed. After that, the application may issue a new batch,
 * and so on.
 * For this reason the next function is invoked to compute
 * soft_rt_next_start only for applications that meet this requirement,
 * whereas soft_rt_next_start is set to infinity for applications that do
 * not.
 *
 * Unfortunately, even a greedy application may happen to behave in an
 * isochronous way if the CPU load is high. In fact, the application may
 * stop issuing requests while the CPUs are busy serving other processes,
 * then restart, then stop again for a while, and so on. In addition, if
 * the disk achieves a low enough throughput with the request pattern
 * issued by the application (e.g., because the request pattern is random
 * and/or the device is slow), then the application may meet the above
 * bandwidth requirement too. To prevent such a greedy application to be
 * deemed as soft real-time, a further rule is used in the computation of
 * soft_rt_next_start: soft_rt_next_start must be higher than the current
 * time plus the maximum time for which the arrival of a request is waited
 * for when a sync queue becomes idle, namely bfqd->bfq_slice_idle.
 * This filters out greedy applications, as the latter issue instead their
 * next request as soon as possible after the last one has been completed
 * (in contrast, when a batch of requests is completed, a soft real-time
 * application spends some time processing data).
 *
 * Unfortunately, the last filter may easily generate false positives if
 * only bfqd->bfq_slice_idle is used as a reference time interval and one
 * or both the following cases occur:
 * 1) HZ is so low that the duration of a jiffy is comparable to or higher
 *    than bfqd->bfq_slice_idle. This happens, e.g., on slow devices with
 *    HZ=100.
 * 2) jiffies, instead of increasing at a constant rate, may stop increasing
 *    for a while, then suddenly 'jump' by several units to recover the lost
 *    increments. This seems to happen, e.g., inside virtual machines.
 * To address this issue, we do not use as a reference time interval just
 * bfqd->bfq_slice_idle, but bfqd->bfq_slice_idle plus a few jiffies. In
 * particular we add the minimum number of jiffies for which the filter
 * seems to be quite precise also in embedded systems and KVM/QEMU virtual
 * machines.
 */
static unsigned long bfq_bfqq_softrt_next_start(struct bfq_data *bfqd,
						struct bfq_queue *bfqq)
{
	return max(bfqq->last_idle_bklogged +
		   HZ * bfqq->service_from_backlogged /
		   bfqd->bfq_wr_max_softrt_rate,
		   jiffies + nsecs_to_jiffies(bfqq->bfqd->bfq_slice_idle) + 4);
}

/*
 * Return the farthest future time instant according to jiffies
 * macros.
 */
static unsigned long bfq_greatest_from_now(void)
{
	return jiffies + MAX_JIFFY_OFFSET;
}

/*
 * Return the farthest past time instant according to jiffies
 * macros.
 */
static unsigned long bfq_smallest_from_now(void)
{
	return jiffies - MAX_JIFFY_OFFSET;
}

/**
 * bfq_bfqq_expire - expire a queue.
 * @bfqd: device owning the queue.
 * @bfqq: the queue to expire.
 * @compensate: if true, compensate for the time spent idling.
 * @reason: the reason causing the expiration.
 *
 * If the process associated with bfqq does slow I/O (e.g., because it
 * issues random requests), we charge bfqq with the time it has been
 * in service instead of the service it has received (see
 * bfq_bfqq_charge_time for details on how this goal is achieved). As
 * a consequence, bfqq will typically get higher timestamps upon
 * reactivation, and hence it will be rescheduled as if it had
 * received more service than what it has actually received. In the
 * end, bfqq receives less service in proportion to how slowly its
 * associated process consumes its budgets (and hence how seriously it
 * tends to lower the throughput). In addition, this time-charging
 * strategy guarantees time fairness among slow processes. In
 * contrast, if the process associated with bfqq is not slow, we
 * charge bfqq exactly with the service it has received.
 *
 * Charging time to the first type of queues and the exact service to
 * the other has the effect of using the WF2Q+ policy to schedule the
 * former on a timeslice basis, without violating service domain
 * guarantees among the latter.
 */
static void bfq_bfqq_expire(struct bfq_data *bfqd,
			    struct bfq_queue *bfqq,
			    bool compensate,
			    enum bfqq_expiration reason)
{
	bool slow;
	unsigned long delta = 0;
	struct bfq_entity *entity = &bfqq->entity;
	int ref;

	/*
	 * Check whether the process is slow (see bfq_bfqq_is_slow).
	 */
	slow = bfq_bfqq_is_slow(bfqd, bfqq, compensate, reason, &delta);

	/*
	 * Increase service_from_backlogged before next statement,
	 * because the possible next invocation of
	 * bfq_bfqq_charge_time would likely inflate
	 * entity->service. In contrast, service_from_backlogged must
	 * contain real service, to enable the soft real-time
	 * heuristic to correctly compute the bandwidth consumed by
	 * bfqq.
	 */
	bfqq->service_from_backlogged += entity->service;

	/*
	 * As above explained, charge slow (typically seeky) and
	 * timed-out queues with the time and not the service
	 * received, to favor sequential workloads.
	 *
	 * Processes doing I/O in the slower disk zones will tend to
	 * be slow(er) even if not seeky. Therefore, since the
	 * estimated peak rate is actually an average over the disk
	 * surface, these processes may timeout just for bad luck. To
	 * avoid punishing them, do not charge time to processes that
	 * succeeded in consuming at least 2/3 of their budget. This
	 * allows BFQ to preserve enough elasticity to still perform
	 * bandwidth, and not time, distribution with little unlucky
	 * or quasi-sequential processes.
	 */
	if (bfqq->wr_coeff == 1 &&
	    (slow ||
	     (reason == BFQQE_BUDGET_TIMEOUT &&
	      bfq_bfqq_budget_left(bfqq) >=  entity->budget / 3)))
		bfq_bfqq_charge_time(bfqd, bfqq, delta);

	if (reason == BFQQE_TOO_IDLE &&
	    entity->service <= 2 * entity->budget / 10)
		bfq_clear_bfqq_IO_bound(bfqq);

	if (bfqd->low_latency && bfqq->wr_coeff == 1)
		bfqq->last_wr_start_finish = jiffies;

	if (bfqd->low_latency && bfqd->bfq_wr_max_softrt_rate > 0 &&
	    RB_EMPTY_ROOT(&bfqq->sort_list)) {
		/*
		 * If we get here, and there are no outstanding
		 * requests, then the request pattern is isochronous
		 * (see the comments on the function
		 * bfq_bfqq_softrt_next_start()). Thus we can compute
		 * soft_rt_next_start. If, instead, the queue still
		 * has outstanding requests, then we have to wait for
		 * the completion of all the outstanding requests to
		 * discover whether the request pattern is actually
		 * isochronous.
		 */
		if (bfqq->dispatched == 0)
			bfqq->soft_rt_next_start =
				bfq_bfqq_softrt_next_start(bfqd, bfqq);
		else {
			/*
			 * The application is still waiting for the
			 * completion of one or more requests:
			 * prevent it from possibly being incorrectly
			 * deemed as soft real-time by setting its
			 * soft_rt_next_start to infinity. In fact,
			 * without this assignment, the application
			 * would be incorrectly deemed as soft
			 * real-time if:
			 * 1) it issued a new request before the
			 *    completion of all its in-flight
			 *    requests, and
			 * 2) at that time, its soft_rt_next_start
			 *    happened to be in the past.
			 */
			bfqq->soft_rt_next_start =
				bfq_greatest_from_now();
			/*
			 * Schedule an update of soft_rt_next_start to when
			 * the task may be discovered to be isochronous.
			 */
			bfq_mark_bfqq_softrt_update(bfqq);
		}
	}

	bfq_log_bfqq(bfqd, bfqq,
		"expire (%d, slow %d, num_disp %d, idle_win %d)", reason,
		slow, bfqq->dispatched, bfq_bfqq_idle_window(bfqq));

	/*
	 * Increase, decrease or leave budget unchanged according to
	 * reason.
	 */
	__bfq_bfqq_recalc_budget(bfqd, bfqq, reason);
	ref = bfqq->ref;
	__bfq_bfqq_expire(bfqd, bfqq);

	/* mark bfqq as waiting a request only if a bic still points to it */
	if (ref > 1 && !bfq_bfqq_busy(bfqq) &&
	    reason != BFQQE_BUDGET_TIMEOUT &&
	    reason != BFQQE_BUDGET_EXHAUSTED)
		bfq_mark_bfqq_non_blocking_wait_rq(bfqq);
}

/*
 * Budget timeout is not implemented through a dedicated timer, but
 * just checked on request arrivals and completions, as well as on
 * idle timer expirations.
 */
static bool bfq_bfqq_budget_timeout(struct bfq_queue *bfqq)
{
	return time_is_before_eq_jiffies(bfqq->budget_timeout);
}

/*
 * If we expire a queue that is actively waiting (i.e., with the
 * device idled) for the arrival of a new request, then we may incur
 * the timestamp misalignment problem described in the body of the
 * function __bfq_activate_entity. Hence we return true only if this
 * condition does not hold, or if the queue is slow enough to deserve
 * only to be kicked off for preserving a high throughput.
 */
static bool bfq_may_expire_for_budg_timeout(struct bfq_queue *bfqq)
{
	bfq_log_bfqq(bfqq->bfqd, bfqq,
		"may_budget_timeout: wait_request %d left %d timeout %d",
		bfq_bfqq_wait_request(bfqq),
			bfq_bfqq_budget_left(bfqq) >=  bfqq->entity.budget / 3,
		bfq_bfqq_budget_timeout(bfqq));

	return (!bfq_bfqq_wait_request(bfqq) ||
		bfq_bfqq_budget_left(bfqq) >=  bfqq->entity.budget / 3)
		&&
		bfq_bfqq_budget_timeout(bfqq);
}

/*
 * For a queue that becomes empty, device idling is allowed only if
 * this function returns true for the queue. As a consequence, since
 * device idling plays a critical role in both throughput boosting and
 * service guarantees, the return value of this function plays a
 * critical role in both these aspects as well.
 *
 * In a nutshell, this function returns true only if idling is
 * beneficial for throughput or, even if detrimental for throughput,
 * idling is however necessary to preserve service guarantees (low
 * latency, desired throughput distribution, ...). In particular, on
 * NCQ-capable devices, this function tries to return false, so as to
 * help keep the drives' internal queues full, whenever this helps the
 * device boost the throughput without causing any service-guarantee
 * issue.
 *
 * In more detail, the return value of this function is obtained by,
 * first, computing a number of boolean variables that take into
 * account throughput and service-guarantee issues, and, then,
 * combining these variables in a logical expression. Most of the
 * issues taken into account are not trivial. We discuss these issues
 * individually while introducing the variables.
 */
static bool bfq_bfqq_may_idle(struct bfq_queue *bfqq)
{
	struct bfq_data *bfqd = bfqq->bfqd;
	bool idling_boosts_thr, idling_boosts_thr_without_issues,
		idling_needed_for_service_guarantees,
		asymmetric_scenario;

	if (bfqd->strict_guarantees)
		return true;

	/*
	 * The next variable takes into account the cases where idling
	 * boosts the throughput.
	 *
	 * The value of the variable is computed considering, first, that
	 * idling is virtually always beneficial for the throughput if:
	 * (a) the device is not NCQ-capable, or
	 * (b) regardless of the presence of NCQ, the device is rotational
	 *     and the request pattern for bfqq is I/O-bound and sequential.
	 *
	 * Secondly, and in contrast to the above item (b), idling an
	 * NCQ-capable flash-based device would not boost the
	 * throughput even with sequential I/O; rather it would lower
	 * the throughput in proportion to how fast the device
	 * is. Accordingly, the next variable is true if any of the
	 * above conditions (a) and (b) is true, and, in particular,
	 * happens to be false if bfqd is an NCQ-capable flash-based
	 * device.
	 */
	idling_boosts_thr = !bfqd->hw_tag ||
		(!blk_queue_nonrot(bfqd->queue) && bfq_bfqq_IO_bound(bfqq) &&
		 bfq_bfqq_idle_window(bfqq));

	/*
	 * The value of the next variable,
	 * idling_boosts_thr_without_issues, is equal to that of
	 * idling_boosts_thr, unless a special case holds. In this
	 * special case, described below, idling may cause problems to
	 * weight-raised queues.
	 *
	 * When the request pool is saturated (e.g., in the presence
	 * of write hogs), if the processes associated with
	 * non-weight-raised queues ask for requests at a lower rate,
	 * then processes associated with weight-raised queues have a
	 * higher probability to get a request from the pool
	 * immediately (or at least soon) when they need one. Thus
	 * they have a higher probability to actually get a fraction
	 * of the device throughput proportional to their high
	 * weight. This is especially true with NCQ-capable drives,
	 * which enqueue several requests in advance, and further
	 * reorder internally-queued requests.
	 *
	 * For this reason, we force to false the value of
	 * idling_boosts_thr_without_issues if there are weight-raised
	 * busy queues. In this case, and if bfqq is not weight-raised,
	 * this guarantees that the device is not idled for bfqq (if,
	 * instead, bfqq is weight-raised, then idling will be
	 * guaranteed by another variable, see below). Combined with
	 * the timestamping rules of BFQ (see [1] for details), this
	 * behavior causes bfqq, and hence any sync non-weight-raised
	 * queue, to get a lower number of requests served, and thus
	 * to ask for a lower number of requests from the request
	 * pool, before the busy weight-raised queues get served
	 * again. This often mitigates starvation problems in the
	 * presence of heavy write workloads and NCQ, thereby
	 * guaranteeing a higher application and system responsiveness
	 * in these hostile scenarios.
	 */
	idling_boosts_thr_without_issues = idling_boosts_thr &&
		bfqd->wr_busy_queues == 0;

	/*
	 * There is then a case where idling must be performed not
	 * for throughput concerns, but to preserve service
	 * guarantees.
	 *
	 * To introduce this case, we can note that allowing the drive
	 * to enqueue more than one request at a time, and hence
	 * delegating de facto final scheduling decisions to the
	 * drive's internal scheduler, entails loss of control on the
	 * actual request service order. In particular, the critical
	 * situation is when requests from different processes happen
	 * to be present, at the same time, in the internal queue(s)
	 * of the drive. In such a situation, the drive, by deciding
	 * the service order of the internally-queued requests, does
	 * determine also the actual throughput distribution among
	 * these processes. But the drive typically has no notion or
	 * concern about per-process throughput distribution, and
	 * makes its decisions only on a per-request basis. Therefore,
	 * the service distribution enforced by the drive's internal
	 * scheduler is likely to coincide with the desired
	 * device-throughput distribution only in a completely
	 * symmetric scenario where:
	 * (i)  each of these processes must get the same throughput as
	 *      the others;
	 * (ii) all these processes have the same I/O pattern
		(either sequential or random).
	 * In fact, in such a scenario, the drive will tend to treat
	 * the requests of each of these processes in about the same
	 * way as the requests of the others, and thus to provide
	 * each of these processes with about the same throughput
	 * (which is exactly the desired throughput distribution). In
	 * contrast, in any asymmetric scenario, device idling is
	 * certainly needed to guarantee that bfqq receives its
	 * assigned fraction of the device throughput (see [1] for
	 * details).
	 *
	 * We address this issue by controlling, actually, only the
	 * symmetry sub-condition (i), i.e., provided that
	 * sub-condition (i) holds, idling is not performed,
	 * regardless of whether sub-condition (ii) holds. In other
	 * words, only if sub-condition (i) holds, then idling is
	 * allowed, and the device tends to be prevented from queueing
	 * many requests, possibly of several processes. The reason
	 * for not controlling also sub-condition (ii) is that we
	 * exploit preemption to preserve guarantees in case of
	 * symmetric scenarios, even if (ii) does not hold, as
	 * explained in the next two paragraphs.
	 *
	 * Even if a queue, say Q, is expired when it remains idle, Q
	 * can still preempt the new in-service queue if the next
	 * request of Q arrives soon (see the comments on
	 * bfq_bfqq_update_budg_for_activation). If all queues and
	 * groups have the same weight, this form of preemption,
	 * combined with the hole-recovery heuristic described in the
	 * comments on function bfq_bfqq_update_budg_for_activation,
	 * are enough to preserve a correct bandwidth distribution in
	 * the mid term, even without idling. In fact, even if not
	 * idling allows the internal queues of the device to contain
	 * many requests, and thus to reorder requests, we can rather
	 * safely assume that the internal scheduler still preserves a
	 * minimum of mid-term fairness. The motivation for using
	 * preemption instead of idling is that, by not idling,
	 * service guarantees are preserved without minimally
	 * sacrificing throughput. In other words, both a high
	 * throughput and its desired distribution are obtained.
	 *
	 * More precisely, this preemption-based, idleless approach
	 * provides fairness in terms of IOPS, and not sectors per
	 * second. This can be seen with a simple example. Suppose
	 * that there are two queues with the same weight, but that
	 * the first queue receives requests of 8 sectors, while the
	 * second queue receives requests of 1024 sectors. In
	 * addition, suppose that each of the two queues contains at
	 * most one request at a time, which implies that each queue
	 * always remains idle after it is served. Finally, after
	 * remaining idle, each queue receives very quickly a new
	 * request. It follows that the two queues are served
	 * alternatively, preempting each other if needed. This
	 * implies that, although both queues have the same weight,
	 * the queue with large requests receives a service that is
	 * 1024/8 times as high as the service received by the other
	 * queue.
	 *
	 * On the other hand, device idling is performed, and thus
	 * pure sector-domain guarantees are provided, for the
	 * following queues, which are likely to need stronger
	 * throughput guarantees: weight-raised queues, and queues
	 * with a higher weight than other queues. When such queues
	 * are active, sub-condition (i) is false, which triggers
	 * device idling.
	 *
	 * According to the above considerations, the next variable is
	 * true (only) if sub-condition (i) holds. To compute the
	 * value of this variable, we not only use the return value of
	 * the function bfq_symmetric_scenario(), but also check
	 * whether bfqq is being weight-raised, because
	 * bfq_symmetric_scenario() does not take into account also
	 * weight-raised queues (see comments on
	 * bfq_weights_tree_add()).
	 *
	 * As a side note, it is worth considering that the above
	 * device-idling countermeasures may however fail in the
	 * following unlucky scenario: if idling is (correctly)
	 * disabled in a time period during which all symmetry
	 * sub-conditions hold, and hence the device is allowed to
	 * enqueue many requests, but at some later point in time some
	 * sub-condition stops to hold, then it may become impossible
	 * to let requests be served in the desired order until all
	 * the requests already queued in the device have been served.
	 */
	asymmetric_scenario = bfqq->wr_coeff > 1 ||
		!bfq_symmetric_scenario(bfqd);

	/*
	 * Finally, there is a case where maximizing throughput is the
	 * best choice even if it may cause unfairness toward
	 * bfqq. Such a case is when bfqq became active in a burst of
	 * queue activations. Queues that became active during a large
	 * burst benefit only from throughput, as discussed in the
	 * comments on bfq_handle_burst. Thus, if bfqq became active
	 * in a burst and not idling the device maximizes throughput,
	 * then the device must no be idled, because not idling the
	 * device provides bfqq and all other queues in the burst with
	 * maximum benefit. Combining this and the above case, we can
	 * now establish when idling is actually needed to preserve
	 * service guarantees.
	 */
	idling_needed_for_service_guarantees =
		asymmetric_scenario && !bfq_bfqq_in_large_burst(bfqq);

	/*
	 * We have now all the components we need to compute the return
	 * value of the function, which is true only if both the following
	 * conditions hold:
	 * 1) bfqq is sync, because idling make sense only for sync queues;
	 * 2) idling either boosts the throughput (without issues), or
	 *    is necessary to preserve service guarantees.
	 */
	return bfq_bfqq_sync(bfqq) &&
		(idling_boosts_thr_without_issues ||
		 idling_needed_for_service_guarantees);
}

/*
 * If the in-service queue is empty but the function bfq_bfqq_may_idle
 * returns true, then:
 * 1) the queue must remain in service and cannot be expired, and
 * 2) the device must be idled to wait for the possible arrival of a new
 *    request for the queue.
 * See the comments on the function bfq_bfqq_may_idle for the reasons
 * why performing device idling is the best choice to boost the throughput
 * and preserve service guarantees when bfq_bfqq_may_idle itself
 * returns true.
 */
static bool bfq_bfqq_must_idle(struct bfq_queue *bfqq)
{
	struct bfq_data *bfqd = bfqq->bfqd;

	return RB_EMPTY_ROOT(&bfqq->sort_list) && bfqd->bfq_slice_idle != 0 &&
	       bfq_bfqq_may_idle(bfqq);
}

/*
 * Select a queue for service.  If we have a current queue in service,
 * check whether to continue servicing it, or retrieve and set a new one.
 */
static struct bfq_queue *bfq_select_queue(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq;
	struct request *next_rq;
	enum bfqq_expiration reason = BFQQE_BUDGET_TIMEOUT;

	bfqq = bfqd->in_service_queue;
	if (!bfqq)
		goto new_queue;

	bfq_log_bfqq(bfqd, bfqq, "select_queue: already in-service queue");

	if (bfq_may_expire_for_budg_timeout(bfqq) &&
	    !bfq_bfqq_wait_request(bfqq) &&
	    !bfq_bfqq_must_idle(bfqq))
		goto expire;

check_queue:
	/*
	 * This loop is rarely executed more than once. Even when it
	 * happens, it is much more convenient to re-execute this loop
	 * than to return NULL and trigger a new dispatch to get a
	 * request served.
	 */
	next_rq = bfqq->next_rq;
	/*
	 * If bfqq has requests queued and it has enough budget left to
	 * serve them, keep the queue, otherwise expire it.
	 */
	if (next_rq) {
		if (bfq_serv_to_charge(next_rq, bfqq) >
			bfq_bfqq_budget_left(bfqq)) {
			/*
			 * Expire the queue for budget exhaustion,
			 * which makes sure that the next budget is
			 * enough to serve the next request, even if
			 * it comes from the fifo expired path.
			 */
			reason = BFQQE_BUDGET_EXHAUSTED;
			goto expire;
		} else {
			/*
			 * The idle timer may be pending because we may
			 * not disable disk idling even when a new request
			 * arrives.
			 */
			if (bfq_bfqq_wait_request(bfqq)) {
				/*
				 * If we get here: 1) at least a new request
				 * has arrived but we have not disabled the
				 * timer because the request was too small,
				 * 2) then the block layer has unplugged
				 * the device, causing the dispatch to be
				 * invoked.
				 *
				 * Since the device is unplugged, now the
				 * requests are probably large enough to
				 * provide a reasonable throughput.
				 * So we disable idling.
				 */
				bfq_clear_bfqq_wait_request(bfqq);
				hrtimer_try_to_cancel(&bfqd->idle_slice_timer);
				bfqg_stats_update_idle_time(bfqq_group(bfqq));
			}
			goto keep_queue;
		}
	}

	/*
	 * No requests pending. However, if the in-service queue is idling
	 * for a new request, or has requests waiting for a completion and
	 * may idle after their completion, then keep it anyway.
	 */
	if (bfq_bfqq_wait_request(bfqq) ||
	    (bfqq->dispatched != 0 && bfq_bfqq_may_idle(bfqq))) {
		bfqq = NULL;
		goto keep_queue;
	}

	reason = BFQQE_NO_MORE_REQUESTS;
expire:
	bfq_bfqq_expire(bfqd, bfqq, false, reason);
new_queue:
	bfqq = bfq_set_in_service_queue(bfqd);
	if (bfqq) {
		bfq_log_bfqq(bfqd, bfqq, "select_queue: checking new queue");
		goto check_queue;
	}
keep_queue:
	if (bfqq)
		bfq_log_bfqq(bfqd, bfqq, "select_queue: returned this queue");
	else
		bfq_log(bfqd, "select_queue: no queue returned");

	return bfqq;
}

static void bfq_update_wr_data(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;

	if (bfqq->wr_coeff > 1) { /* queue is being weight-raised */
		bfq_log_bfqq(bfqd, bfqq,
			"raising period dur %u/%u msec, old coeff %u, w %d(%d)",
			jiffies_to_msecs(jiffies - bfqq->last_wr_start_finish),
			jiffies_to_msecs(bfqq->wr_cur_max_time),
			bfqq->wr_coeff,
			bfqq->entity.weight, bfqq->entity.orig_weight);

		if (entity->prio_changed)
			bfq_log_bfqq(bfqd, bfqq, "WARN: pending prio change");

		/*
		 * If the queue was activated in a burst, or too much
		 * time has elapsed from the beginning of this
		 * weight-raising period, then end weight raising.
		 */
		if (bfq_bfqq_in_large_burst(bfqq))
			bfq_bfqq_end_wr(bfqq);
		else if (time_is_before_jiffies(bfqq->last_wr_start_finish +
						bfqq->wr_cur_max_time)) {
			if (bfqq->wr_cur_max_time != bfqd->bfq_wr_rt_max_time ||
			time_is_before_jiffies(bfqq->wr_start_at_switch_to_srt +
					       bfq_wr_duration(bfqd)))
				bfq_bfqq_end_wr(bfqq);
			else {
				/* switch back to interactive wr */
				bfqq->wr_coeff = bfqd->bfq_wr_coeff;
				bfqq->wr_cur_max_time = bfq_wr_duration(bfqd);
				bfqq->last_wr_start_finish =
					bfqq->wr_start_at_switch_to_srt;
				bfqq->entity.prio_changed = 1;
			}
		}
	}
	/* Update weight both if it must be raised and if it must be lowered */
	if ((entity->weight > entity->orig_weight) != (bfqq->wr_coeff > 1))
		__bfq_entity_update_weight_prio(
			bfq_entity_service_tree(entity),
			entity);
}

/*
 * Dispatch next request from bfqq.
 */
static struct request *bfq_dispatch_rq_from_bfqq(struct bfq_data *bfqd,
						 struct bfq_queue *bfqq)
{
	struct request *rq = bfqq->next_rq;
	unsigned long service_to_charge;

	service_to_charge = bfq_serv_to_charge(rq, bfqq);

	bfq_bfqq_served(bfqq, service_to_charge);

	bfq_dispatch_remove(bfqd->queue, rq);

	/*
	 * If weight raising has to terminate for bfqq, then next
	 * function causes an immediate update of bfqq's weight,
	 * without waiting for next activation. As a consequence, on
	 * expiration, bfqq will be timestamped as if has never been
	 * weight-raised during this service slot, even if it has
	 * received part or even most of the service as a
	 * weight-raised queue. This inflates bfqq's timestamps, which
	 * is beneficial, as bfqq is then more willing to leave the
	 * device immediately to possible other weight-raised queues.
	 */
	bfq_update_wr_data(bfqd, bfqq);

	/*
	 * Expire bfqq, pretending that its budget expired, if bfqq
	 * belongs to CLASS_IDLE and other queues are waiting for
	 * service.
	 */
	if (bfqd->busy_queues > 1 && bfq_class_idle(bfqq))
		goto expire;

	return rq;

expire:
	bfq_bfqq_expire(bfqd, bfqq, false, BFQQE_BUDGET_EXHAUSTED);
	return rq;
}

static bool bfq_has_work(struct blk_mq_hw_ctx *hctx)
{
	struct bfq_data *bfqd = hctx->queue->elevator->elevator_data;

	/*
	 * Avoiding lock: a race on bfqd->busy_queues should cause at
	 * most a call to dispatch for nothing
	 */
	return !list_empty_careful(&bfqd->dispatch) ||
		bfqd->busy_queues > 0;
}

static struct request *__bfq_dispatch_request(struct blk_mq_hw_ctx *hctx)
{
	struct bfq_data *bfqd = hctx->queue->elevator->elevator_data;
	struct request *rq = NULL;
	struct bfq_queue *bfqq = NULL;

	if (!list_empty(&bfqd->dispatch)) {
		rq = list_first_entry(&bfqd->dispatch, struct request,
				      queuelist);
		list_del_init(&rq->queuelist);

		bfqq = RQ_BFQQ(rq);

		if (bfqq) {
			/*
			 * Increment counters here, because this
			 * dispatch does not follow the standard
			 * dispatch flow (where counters are
			 * incremented)
			 */
			bfqq->dispatched++;

			goto inc_in_driver_start_rq;
		}

		/*
		 * We exploit the put_rq_private hook to decrement
		 * rq_in_driver, but put_rq_private will not be
		 * invoked on this request. So, to avoid unbalance,
		 * just start this request, without incrementing
		 * rq_in_driver. As a negative consequence,
		 * rq_in_driver is deceptively lower than it should be
		 * while this request is in service. This may cause
		 * bfq_schedule_dispatch to be invoked uselessly.
		 *
		 * As for implementing an exact solution, the
		 * put_request hook, if defined, is probably invoked
		 * also on this request. So, by exploiting this hook,
		 * we could 1) increment rq_in_driver here, and 2)
		 * decrement it in put_request. Such a solution would
		 * let the value of the counter be always accurate,
		 * but it would entail using an extra interface
		 * function. This cost seems higher than the benefit,
		 * being the frequency of non-elevator-private
		 * requests very low.
		 */
		goto start_rq;
	}

	bfq_log(bfqd, "dispatch requests: %d busy queues", bfqd->busy_queues);

	if (bfqd->busy_queues == 0)
		goto exit;

	/*
	 * Force device to serve one request at a time if
	 * strict_guarantees is true. Forcing this service scheme is
	 * currently the ONLY way to guarantee that the request
	 * service order enforced by the scheduler is respected by a
	 * queueing device. Otherwise the device is free even to make
	 * some unlucky request wait for as long as the device
	 * wishes.
	 *
	 * Of course, serving one request at at time may cause loss of
	 * throughput.
	 */
	if (bfqd->strict_guarantees && bfqd->rq_in_driver > 0)
		goto exit;

	bfqq = bfq_select_queue(bfqd);
	if (!bfqq)
		goto exit;

	rq = bfq_dispatch_rq_from_bfqq(bfqd, bfqq);

	if (rq) {
inc_in_driver_start_rq:
		bfqd->rq_in_driver++;
start_rq:
		rq->rq_flags |= RQF_STARTED;
	}
exit:
	return rq;
}

static struct request *bfq_dispatch_request(struct blk_mq_hw_ctx *hctx)
{
	struct bfq_data *bfqd = hctx->queue->elevator->elevator_data;
	struct request *rq;

	spin_lock_irq(&bfqd->lock);

	rq = __bfq_dispatch_request(hctx);
	spin_unlock_irq(&bfqd->lock);

	return rq;
}

/*
 * Task holds one reference to the queue, dropped when task exits.  Each rq
 * in-flight on this queue also holds a reference, dropped when rq is freed.
 *
 * Scheduler lock must be held here. Recall not to use bfqq after calling
 * this function on it.
 */
static void bfq_put_queue(struct bfq_queue *bfqq)
{
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	struct bfq_group *bfqg = bfqq_group(bfqq);
#endif

	if (bfqq->bfqd)
		bfq_log_bfqq(bfqq->bfqd, bfqq, "put_queue: %p %d",
			     bfqq, bfqq->ref);

	bfqq->ref--;
	if (bfqq->ref)
		return;

	if (bfq_bfqq_sync(bfqq))
		/*
		 * The fact that this queue is being destroyed does not
		 * invalidate the fact that this queue may have been
		 * activated during the current burst. As a consequence,
		 * although the queue does not exist anymore, and hence
		 * needs to be removed from the burst list if there,
		 * the burst size has not to be decremented.
		 */
		hlist_del_init(&bfqq->burst_list_node);

	kmem_cache_free(bfq_pool, bfqq);
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	bfqg_put(bfqg);
#endif
}

static void bfq_put_cooperator(struct bfq_queue *bfqq)
{
	struct bfq_queue *__bfqq, *next;

	/*
	 * If this queue was scheduled to merge with another queue, be
	 * sure to drop the reference taken on that queue (and others in
	 * the merge chain). See bfq_setup_merge and bfq_merge_bfqqs.
	 */
	__bfqq = bfqq->new_bfqq;
	while (__bfqq) {
		if (__bfqq == bfqq)
			break;
		next = __bfqq->new_bfqq;
		bfq_put_queue(__bfqq);
		__bfqq = next;
	}
}

static void bfq_exit_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	if (bfqq == bfqd->in_service_queue) {
		__bfq_bfqq_expire(bfqd, bfqq);
		bfq_schedule_dispatch(bfqd);
	}

	bfq_log_bfqq(bfqd, bfqq, "exit_bfqq: %p, %d", bfqq, bfqq->ref);

	bfq_put_cooperator(bfqq);

	bfq_put_queue(bfqq); /* release process reference */
}

static void bfq_exit_icq_bfqq(struct bfq_io_cq *bic, bool is_sync)
{
	struct bfq_queue *bfqq = bic_to_bfqq(bic, is_sync);
	struct bfq_data *bfqd;

	if (bfqq)
		bfqd = bfqq->bfqd; /* NULL if scheduler already exited */

	if (bfqq && bfqd) {
		unsigned long flags;

		spin_lock_irqsave(&bfqd->lock, flags);
		bfq_exit_bfqq(bfqd, bfqq);
		bic_set_bfqq(bic, NULL, is_sync);
		spin_unlock_irqrestore(&bfqd->lock, flags);
	}
}

static void bfq_exit_icq(struct io_cq *icq)
{
	struct bfq_io_cq *bic = icq_to_bic(icq);

	bfq_exit_icq_bfqq(bic, true);
	bfq_exit_icq_bfqq(bic, false);
}

/*
 * Update the entity prio values; note that the new values will not
 * be used until the next (re)activation.
 */
static void
bfq_set_next_ioprio_data(struct bfq_queue *bfqq, struct bfq_io_cq *bic)
{
	struct task_struct *tsk = current;
	int ioprio_class;
	struct bfq_data *bfqd = bfqq->bfqd;

	if (!bfqd)
		return;

	ioprio_class = IOPRIO_PRIO_CLASS(bic->ioprio);
	switch (ioprio_class) {
	default:
		dev_err(bfqq->bfqd->queue->backing_dev_info->dev,
			"bfq: bad prio class %d\n", ioprio_class);
	case IOPRIO_CLASS_NONE:
		/*
		 * No prio set, inherit CPU scheduling settings.
		 */
		bfqq->new_ioprio = task_nice_ioprio(tsk);
		bfqq->new_ioprio_class = task_nice_ioclass(tsk);
		break;
	case IOPRIO_CLASS_RT:
		bfqq->new_ioprio = IOPRIO_PRIO_DATA(bic->ioprio);
		bfqq->new_ioprio_class = IOPRIO_CLASS_RT;
		break;
	case IOPRIO_CLASS_BE:
		bfqq->new_ioprio = IOPRIO_PRIO_DATA(bic->ioprio);
		bfqq->new_ioprio_class = IOPRIO_CLASS_BE;
		break;
	case IOPRIO_CLASS_IDLE:
		bfqq->new_ioprio_class = IOPRIO_CLASS_IDLE;
		bfqq->new_ioprio = 7;
		bfq_clear_bfqq_idle_window(bfqq);
		break;
	}

	if (bfqq->new_ioprio >= IOPRIO_BE_NR) {
		pr_crit("bfq_set_next_ioprio_data: new_ioprio %d\n",
			bfqq->new_ioprio);
		bfqq->new_ioprio = IOPRIO_BE_NR;
	}

	bfqq->entity.new_weight = bfq_ioprio_to_weight(bfqq->new_ioprio);
	bfqq->entity.prio_changed = 1;
}

static void bfq_check_ioprio_change(struct bfq_io_cq *bic, struct bio *bio)
{
	struct bfq_data *bfqd = bic_to_bfqd(bic);
	struct bfq_queue *bfqq;
	int ioprio = bic->icq.ioc->ioprio;

	/*
	 * This condition may trigger on a newly created bic, be sure to
	 * drop the lock before returning.
	 */
	if (unlikely(!bfqd) || likely(bic->ioprio == ioprio))
		return;

	bic->ioprio = ioprio;

	bfqq = bic_to_bfqq(bic, false);
	if (bfqq) {
		/* release process reference on this queue */
		bfq_put_queue(bfqq);
		bfqq = bfq_get_queue(bfqd, bio, BLK_RW_ASYNC, bic);
		bic_set_bfqq(bic, bfqq, false);
	}

	bfqq = bic_to_bfqq(bic, true);
	if (bfqq)
		bfq_set_next_ioprio_data(bfqq, bic);
}

static void bfq_init_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			  struct bfq_io_cq *bic, pid_t pid, int is_sync)
{
	RB_CLEAR_NODE(&bfqq->entity.rb_node);
	INIT_LIST_HEAD(&bfqq->fifo);
	INIT_HLIST_NODE(&bfqq->burst_list_node);

	bfqq->ref = 0;
	bfqq->bfqd = bfqd;

	if (bic)
		bfq_set_next_ioprio_data(bfqq, bic);

	if (is_sync) {
		if (!bfq_class_idle(bfqq))
			bfq_mark_bfqq_idle_window(bfqq);
		bfq_mark_bfqq_sync(bfqq);
		bfq_mark_bfqq_just_created(bfqq);
	} else
		bfq_clear_bfqq_sync(bfqq);

	/* set end request to minus infinity from now */
	bfqq->ttime.last_end_request = ktime_get_ns() + 1;

	bfq_mark_bfqq_IO_bound(bfqq);

	bfqq->pid = pid;

	/* Tentative initial value to trade off between thr and lat */
	bfqq->max_budget = (2 * bfq_max_budget(bfqd)) / 3;
	bfqq->budget_timeout = bfq_smallest_from_now();

	bfqq->wr_coeff = 1;
	bfqq->last_wr_start_finish = jiffies;
	bfqq->wr_start_at_switch_to_srt = bfq_smallest_from_now();
	bfqq->split_time = bfq_smallest_from_now();

	/*
	 * Set to the value for which bfqq will not be deemed as
	 * soft rt when it becomes backlogged.
	 */
	bfqq->soft_rt_next_start = bfq_greatest_from_now();

	/* first request is almost certainly seeky */
	bfqq->seek_history = 1;
}

static struct bfq_queue **bfq_async_queue_prio(struct bfq_data *bfqd,
					       struct bfq_group *bfqg,
					       int ioprio_class, int ioprio)
{
	switch (ioprio_class) {
	case IOPRIO_CLASS_RT:
		return &bfqg->async_bfqq[0][ioprio];
	case IOPRIO_CLASS_NONE:
		ioprio = IOPRIO_NORM;
		/* fall through */
	case IOPRIO_CLASS_BE:
		return &bfqg->async_bfqq[1][ioprio];
	case IOPRIO_CLASS_IDLE:
		return &bfqg->async_idle_bfqq;
	default:
		return NULL;
	}
}

static struct bfq_queue *bfq_get_queue(struct bfq_data *bfqd,
				       struct bio *bio, bool is_sync,
				       struct bfq_io_cq *bic)
{
	const int ioprio = IOPRIO_PRIO_DATA(bic->ioprio);
	const int ioprio_class = IOPRIO_PRIO_CLASS(bic->ioprio);
	struct bfq_queue **async_bfqq = NULL;
	struct bfq_queue *bfqq;
	struct bfq_group *bfqg;

	rcu_read_lock();

	bfqg = bfq_find_set_group(bfqd, bio_blkcg(bio));
	if (!bfqg) {
		bfqq = &bfqd->oom_bfqq;
		goto out;
	}

	if (!is_sync) {
		async_bfqq = bfq_async_queue_prio(bfqd, bfqg, ioprio_class,
						  ioprio);
		bfqq = *async_bfqq;
		if (bfqq)
			goto out;
	}

	bfqq = kmem_cache_alloc_node(bfq_pool,
				     GFP_NOWAIT | __GFP_ZERO | __GFP_NOWARN,
				     bfqd->queue->node);

	if (bfqq) {
		bfq_init_bfqq(bfqd, bfqq, bic, current->pid,
			      is_sync);
		bfq_init_entity(&bfqq->entity, bfqg);
		bfq_log_bfqq(bfqd, bfqq, "allocated");
	} else {
		bfqq = &bfqd->oom_bfqq;
		bfq_log_bfqq(bfqd, bfqq, "using oom bfqq");
		goto out;
	}

	/*
	 * Pin the queue now that it's allocated, scheduler exit will
	 * prune it.
	 */
	if (async_bfqq) {
		bfqq->ref++; /*
			      * Extra group reference, w.r.t. sync
			      * queue. This extra reference is removed
			      * only if bfqq->bfqg disappears, to
			      * guarantee that this queue is not freed
			      * until its group goes away.
			      */
		bfq_log_bfqq(bfqd, bfqq, "get_queue, bfqq not in async: %p, %d",
			     bfqq, bfqq->ref);
		*async_bfqq = bfqq;
	}

out:
	bfqq->ref++; /* get a process reference to this queue */
	bfq_log_bfqq(bfqd, bfqq, "get_queue, at end: %p, %d", bfqq, bfqq->ref);
	rcu_read_unlock();
	return bfqq;
}

static void bfq_update_io_thinktime(struct bfq_data *bfqd,
				    struct bfq_queue *bfqq)
{
	struct bfq_ttime *ttime = &bfqq->ttime;
	u64 elapsed = ktime_get_ns() - bfqq->ttime.last_end_request;

	elapsed = min_t(u64, elapsed, 2ULL * bfqd->bfq_slice_idle);

	ttime->ttime_samples = (7*bfqq->ttime.ttime_samples + 256) / 8;
	ttime->ttime_total = div_u64(7*ttime->ttime_total + 256*elapsed,  8);
	ttime->ttime_mean = div64_ul(ttime->ttime_total + 128,
				     ttime->ttime_samples);
}

static void
bfq_update_io_seektime(struct bfq_data *bfqd, struct bfq_queue *bfqq,
		       struct request *rq)
{
	bfqq->seek_history <<= 1;
	bfqq->seek_history |=
		get_sdist(bfqq->last_request_pos, rq) > BFQQ_SEEK_THR &&
		(!blk_queue_nonrot(bfqd->queue) ||
		 blk_rq_sectors(rq) < BFQQ_SECT_THR_NONROT);
}

/*
 * Disable idle window if the process thinks too long or seeks so much that
 * it doesn't matter.
 */
static void bfq_update_idle_window(struct bfq_data *bfqd,
				   struct bfq_queue *bfqq,
				   struct bfq_io_cq *bic)
{
	int enable_idle;

	/* Don't idle for async or idle io prio class. */
	if (!bfq_bfqq_sync(bfqq) || bfq_class_idle(bfqq))
		return;

	/* Idle window just restored, statistics are meaningless. */
	if (time_is_after_eq_jiffies(bfqq->split_time +
				     bfqd->bfq_wr_min_idle_time))
		return;

	enable_idle = bfq_bfqq_idle_window(bfqq);

	if (atomic_read(&bic->icq.ioc->active_ref) == 0 ||
	    bfqd->bfq_slice_idle == 0 ||
		(bfqd->hw_tag && BFQQ_SEEKY(bfqq) &&
			bfqq->wr_coeff == 1))
		enable_idle = 0;
	else if (bfq_sample_valid(bfqq->ttime.ttime_samples)) {
		if (bfqq->ttime.ttime_mean > bfqd->bfq_slice_idle &&
			bfqq->wr_coeff == 1)
			enable_idle = 0;
		else
			enable_idle = 1;
	}
	bfq_log_bfqq(bfqd, bfqq, "update_idle_window: enable_idle %d",
		enable_idle);

	if (enable_idle)
		bfq_mark_bfqq_idle_window(bfqq);
	else
		bfq_clear_bfqq_idle_window(bfqq);
}

/*
 * Called when a new fs request (rq) is added to bfqq.  Check if there's
 * something we should do about it.
 */
static void bfq_rq_enqueued(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			    struct request *rq)
{
	struct bfq_io_cq *bic = RQ_BIC(rq);

	if (rq->cmd_flags & REQ_META)
		bfqq->meta_pending++;

	bfq_update_io_thinktime(bfqd, bfqq);
	bfq_update_io_seektime(bfqd, bfqq, rq);
	if (bfqq->entity.service > bfq_max_budget(bfqd) / 8 ||
	    !BFQQ_SEEKY(bfqq))
		bfq_update_idle_window(bfqd, bfqq, bic);

	bfq_log_bfqq(bfqd, bfqq,
		     "rq_enqueued: idle_window=%d (seeky %d)",
		     bfq_bfqq_idle_window(bfqq), BFQQ_SEEKY(bfqq));

	bfqq->last_request_pos = blk_rq_pos(rq) + blk_rq_sectors(rq);

	if (bfqq == bfqd->in_service_queue && bfq_bfqq_wait_request(bfqq)) {
		bool small_req = bfqq->queued[rq_is_sync(rq)] == 1 &&
				 blk_rq_sectors(rq) < 32;
		bool budget_timeout = bfq_bfqq_budget_timeout(bfqq);

		/*
		 * There is just this request queued: if the request
		 * is small and the queue is not to be expired, then
		 * just exit.
		 *
		 * In this way, if the device is being idled to wait
		 * for a new request from the in-service queue, we
		 * avoid unplugging the device and committing the
		 * device to serve just a small request. On the
		 * contrary, we wait for the block layer to decide
		 * when to unplug the device: hopefully, new requests
		 * will be merged to this one quickly, then the device
		 * will be unplugged and larger requests will be
		 * dispatched.
		 */
		if (small_req && !budget_timeout)
			return;

		/*
		 * A large enough request arrived, or the queue is to
		 * be expired: in both cases disk idling is to be
		 * stopped, so clear wait_request flag and reset
		 * timer.
		 */
		bfq_clear_bfqq_wait_request(bfqq);
		hrtimer_try_to_cancel(&bfqd->idle_slice_timer);
		bfqg_stats_update_idle_time(bfqq_group(bfqq));

		/*
		 * The queue is not empty, because a new request just
		 * arrived. Hence we can safely expire the queue, in
		 * case of budget timeout, without risking that the
		 * timestamps of the queue are not updated correctly.
		 * See [1] for more details.
		 */
		if (budget_timeout)
			bfq_bfqq_expire(bfqd, bfqq, false,
					BFQQE_BUDGET_TIMEOUT);
	}
}

static void __bfq_insert_request(struct bfq_data *bfqd, struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq),
		*new_bfqq = bfq_setup_cooperator(bfqd, bfqq, rq, true);

	if (new_bfqq) {
		if (bic_to_bfqq(RQ_BIC(rq), 1) != bfqq)
			new_bfqq = bic_to_bfqq(RQ_BIC(rq), 1);
		/*
		 * Release the request's reference to the old bfqq
		 * and make sure one is taken to the shared queue.
		 */
		new_bfqq->allocated++;
		bfqq->allocated--;
		new_bfqq->ref++;
		bfq_clear_bfqq_just_created(bfqq);
		/*
		 * If the bic associated with the process
		 * issuing this request still points to bfqq
		 * (and thus has not been already redirected
		 * to new_bfqq or even some other bfq_queue),
		 * then complete the merge and redirect it to
		 * new_bfqq.
		 */
		if (bic_to_bfqq(RQ_BIC(rq), 1) == bfqq)
			bfq_merge_bfqqs(bfqd, RQ_BIC(rq),
					bfqq, new_bfqq);
		/*
		 * rq is about to be enqueued into new_bfqq,
		 * release rq reference on bfqq
		 */
		bfq_put_queue(bfqq);
		rq->elv.priv[1] = new_bfqq;
		bfqq = new_bfqq;
	}

	bfq_add_request(rq);

	rq->fifo_time = ktime_get_ns() + bfqd->bfq_fifo_expire[rq_is_sync(rq)];
	list_add_tail(&rq->queuelist, &bfqq->fifo);

	bfq_rq_enqueued(bfqd, bfqq, rq);
}

static void bfq_insert_request(struct blk_mq_hw_ctx *hctx, struct request *rq,
			       bool at_head)
{
	struct request_queue *q = hctx->queue;
	struct bfq_data *bfqd = q->elevator->elevator_data;

	spin_lock_irq(&bfqd->lock);
	if (blk_mq_sched_try_insert_merge(q, rq)) {
		spin_unlock_irq(&bfqd->lock);
		return;
	}

	spin_unlock_irq(&bfqd->lock);

	blk_mq_sched_request_inserted(rq);

	spin_lock_irq(&bfqd->lock);
	if (at_head || blk_rq_is_passthrough(rq)) {
		if (at_head)
			list_add(&rq->queuelist, &bfqd->dispatch);
		else
			list_add_tail(&rq->queuelist, &bfqd->dispatch);
	} else {
		__bfq_insert_request(bfqd, rq);

		if (rq_mergeable(rq)) {
			elv_rqhash_add(q, rq);
			if (!q->last_merge)
				q->last_merge = rq;
		}
	}

	spin_unlock_irq(&bfqd->lock);
}

static void bfq_insert_requests(struct blk_mq_hw_ctx *hctx,
				struct list_head *list, bool at_head)
{
	while (!list_empty(list)) {
		struct request *rq;

		rq = list_first_entry(list, struct request, queuelist);
		list_del_init(&rq->queuelist);
		bfq_insert_request(hctx, rq, at_head);
	}
}

static void bfq_update_hw_tag(struct bfq_data *bfqd)
{
	bfqd->max_rq_in_driver = max_t(int, bfqd->max_rq_in_driver,
				       bfqd->rq_in_driver);

	if (bfqd->hw_tag == 1)
		return;

	/*
	 * This sample is valid if the number of outstanding requests
	 * is large enough to allow a queueing behavior.  Note that the
	 * sum is not exact, as it's not taking into account deactivated
	 * requests.
	 */
	if (bfqd->rq_in_driver + bfqd->queued < BFQ_HW_QUEUE_THRESHOLD)
		return;

	if (bfqd->hw_tag_samples++ < BFQ_HW_QUEUE_SAMPLES)
		return;

	bfqd->hw_tag = bfqd->max_rq_in_driver > BFQ_HW_QUEUE_THRESHOLD;
	bfqd->max_rq_in_driver = 0;
	bfqd->hw_tag_samples = 0;
}

static void bfq_completed_request(struct bfq_queue *bfqq, struct bfq_data *bfqd)
{
	u64 now_ns;
	u32 delta_us;

	bfq_update_hw_tag(bfqd);

	bfqd->rq_in_driver--;
	bfqq->dispatched--;

	if (!bfqq->dispatched && !bfq_bfqq_busy(bfqq)) {
		/*
		 * Set budget_timeout (which we overload to store the
		 * time at which the queue remains with no backlog and
		 * no outstanding request; used by the weight-raising
		 * mechanism).
		 */
		bfqq->budget_timeout = jiffies;

		bfq_weights_tree_remove(bfqd, &bfqq->entity,
					&bfqd->queue_weights_tree);
	}

	now_ns = ktime_get_ns();

	bfqq->ttime.last_end_request = now_ns;

	/*
	 * Using us instead of ns, to get a reasonable precision in
	 * computing rate in next check.
	 */
	delta_us = div_u64(now_ns - bfqd->last_completion, NSEC_PER_USEC);

	/*
	 * If the request took rather long to complete, and, according
	 * to the maximum request size recorded, this completion latency
	 * implies that the request was certainly served at a very low
	 * rate (less than 1M sectors/sec), then the whole observation
	 * interval that lasts up to this time instant cannot be a
	 * valid time interval for computing a new peak rate.  Invoke
	 * bfq_update_rate_reset to have the following three steps
	 * taken:
	 * - close the observation interval at the last (previous)
	 *   request dispatch or completion
	 * - compute rate, if possible, for that observation interval
	 * - reset to zero samples, which will trigger a proper
	 *   re-initialization of the observation interval on next
	 *   dispatch
	 */
	if (delta_us > BFQ_MIN_TT/NSEC_PER_USEC &&
	   (bfqd->last_rq_max_size<<BFQ_RATE_SHIFT)/delta_us <
			1UL<<(BFQ_RATE_SHIFT - 10))
		bfq_update_rate_reset(bfqd, NULL);
	bfqd->last_completion = now_ns;

	/*
	 * If we are waiting to discover whether the request pattern
	 * of the task associated with the queue is actually
	 * isochronous, and both requisites for this condition to hold
	 * are now satisfied, then compute soft_rt_next_start (see the
	 * comments on the function bfq_bfqq_softrt_next_start()). We
	 * schedule this delayed check when bfqq expires, if it still
	 * has in-flight requests.
	 */
	if (bfq_bfqq_softrt_update(bfqq) && bfqq->dispatched == 0 &&
	    RB_EMPTY_ROOT(&bfqq->sort_list))
		bfqq->soft_rt_next_start =
			bfq_bfqq_softrt_next_start(bfqd, bfqq);

	/*
	 * If this is the in-service queue, check if it needs to be expired,
	 * or if we want to idle in case it has no pending requests.
	 */
	if (bfqd->in_service_queue == bfqq) {
		if (bfqq->dispatched == 0 && bfq_bfqq_must_idle(bfqq)) {
			bfq_arm_slice_timer(bfqd);
			return;
		} else if (bfq_may_expire_for_budg_timeout(bfqq))
			bfq_bfqq_expire(bfqd, bfqq, false,
					BFQQE_BUDGET_TIMEOUT);
		else if (RB_EMPTY_ROOT(&bfqq->sort_list) &&
			 (bfqq->dispatched == 0 ||
			  !bfq_bfqq_may_idle(bfqq)))
			bfq_bfqq_expire(bfqd, bfqq, false,
					BFQQE_NO_MORE_REQUESTS);
	}
}

static void bfq_put_rq_priv_body(struct bfq_queue *bfqq)
{
	bfqq->allocated--;

	bfq_put_queue(bfqq);
}

static void bfq_put_rq_private(struct request_queue *q, struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);
	struct bfq_data *bfqd = bfqq->bfqd;

	if (rq->rq_flags & RQF_STARTED)
		bfqg_stats_update_completion(bfqq_group(bfqq),
					     rq_start_time_ns(rq),
					     rq_io_start_time_ns(rq),
					     rq->cmd_flags);

	if (likely(rq->rq_flags & RQF_STARTED)) {
		unsigned long flags;

		spin_lock_irqsave(&bfqd->lock, flags);

		bfq_completed_request(bfqq, bfqd);
		bfq_put_rq_priv_body(bfqq);

		spin_unlock_irqrestore(&bfqd->lock, flags);
	} else {
		/*
		 * Request rq may be still/already in the scheduler,
		 * in which case we need to remove it. And we cannot
		 * defer such a check and removal, to avoid
		 * inconsistencies in the time interval from the end
		 * of this function to the start of the deferred work.
		 * This situation seems to occur only in process
		 * context, as a consequence of a merge. In the
		 * current version of the code, this implies that the
		 * lock is held.
		 */

		if (!RB_EMPTY_NODE(&rq->rb_node))
			bfq_remove_request(q, rq);
		bfq_put_rq_priv_body(bfqq);
	}

	rq->elv.priv[0] = NULL;
	rq->elv.priv[1] = NULL;
}

/*
 * Returns NULL if a new bfqq should be allocated, or the old bfqq if this
 * was the last process referring to that bfqq.
 */
static struct bfq_queue *
bfq_split_bfqq(struct bfq_io_cq *bic, struct bfq_queue *bfqq)
{
	bfq_log_bfqq(bfqq->bfqd, bfqq, "splitting queue");

	if (bfqq_process_refs(bfqq) == 1) {
		bfqq->pid = current->pid;
		bfq_clear_bfqq_coop(bfqq);
		bfq_clear_bfqq_split_coop(bfqq);
		return bfqq;
	}

	bic_set_bfqq(bic, NULL, 1);

	bfq_put_cooperator(bfqq);

	bfq_put_queue(bfqq);
	return NULL;
}

static struct bfq_queue *bfq_get_bfqq_handle_split(struct bfq_data *bfqd,
						   struct bfq_io_cq *bic,
						   struct bio *bio,
						   bool split, bool is_sync,
						   bool *new_queue)
{
	struct bfq_queue *bfqq = bic_to_bfqq(bic, is_sync);

	if (likely(bfqq && bfqq != &bfqd->oom_bfqq))
		return bfqq;

	if (new_queue)
		*new_queue = true;

	if (bfqq)
		bfq_put_queue(bfqq);
	bfqq = bfq_get_queue(bfqd, bio, is_sync, bic);

	bic_set_bfqq(bic, bfqq, is_sync);
	if (split && is_sync) {
		if ((bic->was_in_burst_list && bfqd->large_burst) ||
		    bic->saved_in_large_burst)
			bfq_mark_bfqq_in_large_burst(bfqq);
		else {
			bfq_clear_bfqq_in_large_burst(bfqq);
			if (bic->was_in_burst_list)
				hlist_add_head(&bfqq->burst_list_node,
					       &bfqd->burst_list);
		}
		bfqq->split_time = jiffies;
	}

	return bfqq;
}

/*
 * Allocate bfq data structures associated with this request.
 */
static int bfq_get_rq_private(struct request_queue *q, struct request *rq,
			      struct bio *bio)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct bfq_io_cq *bic = icq_to_bic(rq->elv.icq);
	const int is_sync = rq_is_sync(rq);
	struct bfq_queue *bfqq;
	bool new_queue = false;
	bool split = false;

	spin_lock_irq(&bfqd->lock);

	bfq_check_ioprio_change(bic, bio);

	if (!bic)
		goto queue_fail;

	bfq_bic_update_cgroup(bic, bio);

	bfqq = bfq_get_bfqq_handle_split(bfqd, bic, bio, false, is_sync,
					 &new_queue);

	if (likely(!new_queue)) {
		/* If the queue was seeky for too long, break it apart. */
		if (bfq_bfqq_coop(bfqq) && bfq_bfqq_split_coop(bfqq)) {
			bfq_log_bfqq(bfqd, bfqq, "breaking apart bfqq");

			/* Update bic before losing reference to bfqq */
			if (bfq_bfqq_in_large_burst(bfqq))
				bic->saved_in_large_burst = true;

			bfqq = bfq_split_bfqq(bic, bfqq);
			split = true;

			if (!bfqq)
				bfqq = bfq_get_bfqq_handle_split(bfqd, bic, bio,
								 true, is_sync,
								 NULL);
		}
	}

	bfqq->allocated++;
	bfqq->ref++;
	bfq_log_bfqq(bfqd, bfqq, "get_request %p: bfqq %p, %d",
		     rq, bfqq, bfqq->ref);

	rq->elv.priv[0] = bic;
	rq->elv.priv[1] = bfqq;

	/*
	 * If a bfq_queue has only one process reference, it is owned
	 * by only this bic: we can then set bfqq->bic = bic. in
	 * addition, if the queue has also just been split, we have to
	 * resume its state.
	 */
	if (likely(bfqq != &bfqd->oom_bfqq) && bfqq_process_refs(bfqq) == 1) {
		bfqq->bic = bic;
		if (split) {
			/*
			 * The queue has just been split from a shared
			 * queue: restore the idle window and the
			 * possible weight raising period.
			 */
			bfq_bfqq_resume_state(bfqq, bic);
		}
	}

	if (unlikely(bfq_bfqq_just_created(bfqq)))
		bfq_handle_burst(bfqd, bfqq);

	spin_unlock_irq(&bfqd->lock);

	return 0;

queue_fail:
	spin_unlock_irq(&bfqd->lock);

	return 1;
}

static void bfq_idle_slice_timer_body(struct bfq_queue *bfqq)
{
	struct bfq_data *bfqd = bfqq->bfqd;
	enum bfqq_expiration reason;
	unsigned long flags;

	spin_lock_irqsave(&bfqd->lock, flags);
	bfq_clear_bfqq_wait_request(bfqq);

	if (bfqq != bfqd->in_service_queue) {
		spin_unlock_irqrestore(&bfqd->lock, flags);
		return;
	}

	if (bfq_bfqq_budget_timeout(bfqq))
		/*
		 * Also here the queue can be safely expired
		 * for budget timeout without wasting
		 * guarantees
		 */
		reason = BFQQE_BUDGET_TIMEOUT;
	else if (bfqq->queued[0] == 0 && bfqq->queued[1] == 0)
		/*
		 * The queue may not be empty upon timer expiration,
		 * because we may not disable the timer when the
		 * first request of the in-service queue arrives
		 * during disk idling.
		 */
		reason = BFQQE_TOO_IDLE;
	else
		goto schedule_dispatch;

	bfq_bfqq_expire(bfqd, bfqq, true, reason);

schedule_dispatch:
	spin_unlock_irqrestore(&bfqd->lock, flags);
	bfq_schedule_dispatch(bfqd);
}

/*
 * Handler of the expiration of the timer running if the in-service queue
 * is idling inside its time slice.
 */
static enum hrtimer_restart bfq_idle_slice_timer(struct hrtimer *timer)
{
	struct bfq_data *bfqd = container_of(timer, struct bfq_data,
					     idle_slice_timer);
	struct bfq_queue *bfqq = bfqd->in_service_queue;

	/*
	 * Theoretical race here: the in-service queue can be NULL or
	 * different from the queue that was idling if a new request
	 * arrives for the current queue and there is a full dispatch
	 * cycle that changes the in-service queue.  This can hardly
	 * happen, but in the worst case we just expire a queue too
	 * early.
	 */
	if (bfqq)
		bfq_idle_slice_timer_body(bfqq);

	return HRTIMER_NORESTART;
}

static void __bfq_put_async_bfqq(struct bfq_data *bfqd,
				 struct bfq_queue **bfqq_ptr)
{
	struct bfq_queue *bfqq = *bfqq_ptr;

	bfq_log(bfqd, "put_async_bfqq: %p", bfqq);
	if (bfqq) {
		bfq_bfqq_move(bfqd, bfqq, bfqd->root_group);

		bfq_log_bfqq(bfqd, bfqq, "put_async_bfqq: putting %p, %d",
			     bfqq, bfqq->ref);
		bfq_put_queue(bfqq);
		*bfqq_ptr = NULL;
	}
}

/*
 * Release all the bfqg references to its async queues.  If we are
 * deallocating the group these queues may still contain requests, so
 * we reparent them to the root cgroup (i.e., the only one that will
 * exist for sure until all the requests on a device are gone).
 */
static void bfq_put_async_queues(struct bfq_data *bfqd, struct bfq_group *bfqg)
{
	int i, j;

	for (i = 0; i < 2; i++)
		for (j = 0; j < IOPRIO_BE_NR; j++)
			__bfq_put_async_bfqq(bfqd, &bfqg->async_bfqq[i][j]);

	__bfq_put_async_bfqq(bfqd, &bfqg->async_idle_bfqq);
}

static void bfq_exit_queue(struct elevator_queue *e)
{
	struct bfq_data *bfqd = e->elevator_data;
	struct bfq_queue *bfqq, *n;

	hrtimer_cancel(&bfqd->idle_slice_timer);

	spin_lock_irq(&bfqd->lock);
	list_for_each_entry_safe(bfqq, n, &bfqd->idle_list, bfqq_list)
		bfq_deactivate_bfqq(bfqd, bfqq, false, false);
	spin_unlock_irq(&bfqd->lock);

	hrtimer_cancel(&bfqd->idle_slice_timer);

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	blkcg_deactivate_policy(bfqd->queue, &blkcg_policy_bfq);
#else
	spin_lock_irq(&bfqd->lock);
	bfq_put_async_queues(bfqd, bfqd->root_group);
	kfree(bfqd->root_group);
	spin_unlock_irq(&bfqd->lock);
#endif

	kfree(bfqd);
}

static void bfq_init_root_group(struct bfq_group *root_group,
				struct bfq_data *bfqd)
{
	int i;

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	root_group->entity.parent = NULL;
	root_group->my_entity = NULL;
	root_group->bfqd = bfqd;
#endif
	root_group->rq_pos_tree = RB_ROOT;
	for (i = 0; i < BFQ_IOPRIO_CLASSES; i++)
		root_group->sched_data.service_tree[i] = BFQ_SERVICE_TREE_INIT;
	root_group->sched_data.bfq_class_idle_last_service = jiffies;
}

static int bfq_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct bfq_data *bfqd;
	struct elevator_queue *eq;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	bfqd = kzalloc_node(sizeof(*bfqd), GFP_KERNEL, q->node);
	if (!bfqd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	eq->elevator_data = bfqd;

	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);

	/*
	 * Our fallback bfqq if bfq_find_alloc_queue() runs into OOM issues.
	 * Grab a permanent reference to it, so that the normal code flow
	 * will not attempt to free it.
	 */
	bfq_init_bfqq(bfqd, &bfqd->oom_bfqq, NULL, 1, 0);
	bfqd->oom_bfqq.ref++;
	bfqd->oom_bfqq.new_ioprio = BFQ_DEFAULT_QUEUE_IOPRIO;
	bfqd->oom_bfqq.new_ioprio_class = IOPRIO_CLASS_BE;
	bfqd->oom_bfqq.entity.new_weight =
		bfq_ioprio_to_weight(bfqd->oom_bfqq.new_ioprio);

	/* oom_bfqq does not participate to bursts */
	bfq_clear_bfqq_just_created(&bfqd->oom_bfqq);

	/*
	 * Trigger weight initialization, according to ioprio, at the
	 * oom_bfqq's first activation. The oom_bfqq's ioprio and ioprio
	 * class won't be changed any more.
	 */
	bfqd->oom_bfqq.entity.prio_changed = 1;

	bfqd->queue = q;

	INIT_LIST_HEAD(&bfqd->dispatch);

	hrtimer_init(&bfqd->idle_slice_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	bfqd->idle_slice_timer.function = bfq_idle_slice_timer;

	bfqd->queue_weights_tree = RB_ROOT;
	bfqd->group_weights_tree = RB_ROOT;

	INIT_LIST_HEAD(&bfqd->active_list);
	INIT_LIST_HEAD(&bfqd->idle_list);
	INIT_HLIST_HEAD(&bfqd->burst_list);

	bfqd->hw_tag = -1;

	bfqd->bfq_max_budget = bfq_default_max_budget;

	bfqd->bfq_fifo_expire[0] = bfq_fifo_expire[0];
	bfqd->bfq_fifo_expire[1] = bfq_fifo_expire[1];
	bfqd->bfq_back_max = bfq_back_max;
	bfqd->bfq_back_penalty = bfq_back_penalty;
	bfqd->bfq_slice_idle = bfq_slice_idle;
	bfqd->bfq_timeout = bfq_timeout;

	bfqd->bfq_requests_within_timer = 120;

	bfqd->bfq_large_burst_thresh = 8;
	bfqd->bfq_burst_interval = msecs_to_jiffies(180);

	bfqd->low_latency = true;

	/*
	 * Trade-off between responsiveness and fairness.
	 */
	bfqd->bfq_wr_coeff = 30;
	bfqd->bfq_wr_rt_max_time = msecs_to_jiffies(300);
	bfqd->bfq_wr_max_time = 0;
	bfqd->bfq_wr_min_idle_time = msecs_to_jiffies(2000);
	bfqd->bfq_wr_min_inter_arr_async = msecs_to_jiffies(500);
	bfqd->bfq_wr_max_softrt_rate = 7000; /*
					      * Approximate rate required
					      * to playback or record a
					      * high-definition compressed
					      * video.
					      */
	bfqd->wr_busy_queues = 0;

	/*
	 * Begin by assuming, optimistically, that the device is a
	 * high-speed one, and that its peak rate is equal to 2/3 of
	 * the highest reference rate.
	 */
	bfqd->RT_prod = R_fast[blk_queue_nonrot(bfqd->queue)] *
			T_fast[blk_queue_nonrot(bfqd->queue)];
	bfqd->peak_rate = R_fast[blk_queue_nonrot(bfqd->queue)] * 2 / 3;
	bfqd->device_speed = BFQ_BFQD_FAST;

	spin_lock_init(&bfqd->lock);

	/*
	 * The invocation of the next bfq_create_group_hierarchy
	 * function is the head of a chain of function calls
	 * (bfq_create_group_hierarchy->blkcg_activate_policy->
	 * blk_mq_freeze_queue) that may lead to the invocation of the
	 * has_work hook function. For this reason,
	 * bfq_create_group_hierarchy is invoked only after all
	 * scheduler data has been initialized, apart from the fields
	 * that can be initialized only after invoking
	 * bfq_create_group_hierarchy. This, in particular, enables
	 * has_work to correctly return false. Of course, to avoid
	 * other inconsistencies, the blk-mq stack must then refrain
	 * from invoking further scheduler hooks before this init
	 * function is finished.
	 */
	bfqd->root_group = bfq_create_group_hierarchy(bfqd, q->node);
	if (!bfqd->root_group)
		goto out_free;
	bfq_init_root_group(bfqd->root_group, bfqd);
	bfq_init_entity(&bfqd->oom_bfqq.entity, bfqd->root_group);


	return 0;

out_free:
	kfree(bfqd);
	kobject_put(&eq->kobj);
	return -ENOMEM;
}

static void bfq_slab_kill(void)
{
	kmem_cache_destroy(bfq_pool);
}

static int __init bfq_slab_setup(void)
{
	bfq_pool = KMEM_CACHE(bfq_queue, 0);
	if (!bfq_pool)
		return -ENOMEM;
	return 0;
}

static ssize_t bfq_var_show(unsigned int var, char *page)
{
	return sprintf(page, "%u\n", var);
}

static ssize_t bfq_var_store(unsigned long *var, const char *page,
			     size_t count)
{
	unsigned long new_val;
	int ret = kstrtoul(page, 10, &new_val);

	if (ret == 0)
		*var = new_val;

	return count;
}

#define SHOW_FUNCTION(__FUNC, __VAR, __CONV)				\
static ssize_t __FUNC(struct elevator_queue *e, char *page)		\
{									\
	struct bfq_data *bfqd = e->elevator_data;			\
	u64 __data = __VAR;						\
	if (__CONV == 1)						\
		__data = jiffies_to_msecs(__data);			\
	else if (__CONV == 2)						\
		__data = div_u64(__data, NSEC_PER_MSEC);		\
	return bfq_var_show(__data, (page));				\
}
SHOW_FUNCTION(bfq_fifo_expire_sync_show, bfqd->bfq_fifo_expire[1], 2);
SHOW_FUNCTION(bfq_fifo_expire_async_show, bfqd->bfq_fifo_expire[0], 2);
SHOW_FUNCTION(bfq_back_seek_max_show, bfqd->bfq_back_max, 0);
SHOW_FUNCTION(bfq_back_seek_penalty_show, bfqd->bfq_back_penalty, 0);
SHOW_FUNCTION(bfq_slice_idle_show, bfqd->bfq_slice_idle, 2);
SHOW_FUNCTION(bfq_max_budget_show, bfqd->bfq_user_max_budget, 0);
SHOW_FUNCTION(bfq_timeout_sync_show, bfqd->bfq_timeout, 1);
SHOW_FUNCTION(bfq_strict_guarantees_show, bfqd->strict_guarantees, 0);
SHOW_FUNCTION(bfq_low_latency_show, bfqd->low_latency, 0);
#undef SHOW_FUNCTION

#define USEC_SHOW_FUNCTION(__FUNC, __VAR)				\
static ssize_t __FUNC(struct elevator_queue *e, char *page)		\
{									\
	struct bfq_data *bfqd = e->elevator_data;			\
	u64 __data = __VAR;						\
	__data = div_u64(__data, NSEC_PER_USEC);			\
	return bfq_var_show(__data, (page));				\
}
USEC_SHOW_FUNCTION(bfq_slice_idle_us_show, bfqd->bfq_slice_idle);
#undef USEC_SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR, MIN, MAX, __CONV)			\
static ssize_t								\
__FUNC(struct elevator_queue *e, const char *page, size_t count)	\
{									\
	struct bfq_data *bfqd = e->elevator_data;			\
	unsigned long uninitialized_var(__data);			\
	int ret = bfq_var_store(&__data, (page), count);		\
	if (__data < (MIN))						\
		__data = (MIN);						\
	else if (__data > (MAX))					\
		__data = (MAX);						\
	if (__CONV == 1)						\
		*(__PTR) = msecs_to_jiffies(__data);			\
	else if (__CONV == 2)						\
		*(__PTR) = (u64)__data * NSEC_PER_MSEC;			\
	else								\
		*(__PTR) = __data;					\
	return ret;							\
}
STORE_FUNCTION(bfq_fifo_expire_sync_store, &bfqd->bfq_fifo_expire[1], 1,
		INT_MAX, 2);
STORE_FUNCTION(bfq_fifo_expire_async_store, &bfqd->bfq_fifo_expire[0], 1,
		INT_MAX, 2);
STORE_FUNCTION(bfq_back_seek_max_store, &bfqd->bfq_back_max, 0, INT_MAX, 0);
STORE_FUNCTION(bfq_back_seek_penalty_store, &bfqd->bfq_back_penalty, 1,
		INT_MAX, 0);
STORE_FUNCTION(bfq_slice_idle_store, &bfqd->bfq_slice_idle, 0, INT_MAX, 2);
#undef STORE_FUNCTION

#define USEC_STORE_FUNCTION(__FUNC, __PTR, MIN, MAX)			\
static ssize_t __FUNC(struct elevator_queue *e, const char *page, size_t count)\
{									\
	struct bfq_data *bfqd = e->elevator_data;			\
	unsigned long uninitialized_var(__data);			\
	int ret = bfq_var_store(&__data, (page), count);		\
	if (__data < (MIN))						\
		__data = (MIN);						\
	else if (__data > (MAX))					\
		__data = (MAX);						\
	*(__PTR) = (u64)__data * NSEC_PER_USEC;				\
	return ret;							\
}
USEC_STORE_FUNCTION(bfq_slice_idle_us_store, &bfqd->bfq_slice_idle, 0,
		    UINT_MAX);
#undef USEC_STORE_FUNCTION

static ssize_t bfq_max_budget_store(struct elevator_queue *e,
				    const char *page, size_t count)
{
	struct bfq_data *bfqd = e->elevator_data;
	unsigned long uninitialized_var(__data);
	int ret = bfq_var_store(&__data, (page), count);

	if (__data == 0)
		bfqd->bfq_max_budget = bfq_calc_max_budget(bfqd);
	else {
		if (__data > INT_MAX)
			__data = INT_MAX;
		bfqd->bfq_max_budget = __data;
	}

	bfqd->bfq_user_max_budget = __data;

	return ret;
}

/*
 * Leaving this name to preserve name compatibility with cfq
 * parameters, but this timeout is used for both sync and async.
 */
static ssize_t bfq_timeout_sync_store(struct elevator_queue *e,
				      const char *page, size_t count)
{
	struct bfq_data *bfqd = e->elevator_data;
	unsigned long uninitialized_var(__data);
	int ret = bfq_var_store(&__data, (page), count);

	if (__data < 1)
		__data = 1;
	else if (__data > INT_MAX)
		__data = INT_MAX;

	bfqd->bfq_timeout = msecs_to_jiffies(__data);
	if (bfqd->bfq_user_max_budget == 0)
		bfqd->bfq_max_budget = bfq_calc_max_budget(bfqd);

	return ret;
}

static ssize_t bfq_strict_guarantees_store(struct elevator_queue *e,
				     const char *page, size_t count)
{
	struct bfq_data *bfqd = e->elevator_data;
	unsigned long uninitialized_var(__data);
	int ret = bfq_var_store(&__data, (page), count);

	if (__data > 1)
		__data = 1;
	if (!bfqd->strict_guarantees && __data == 1
	    && bfqd->bfq_slice_idle < 8 * NSEC_PER_MSEC)
		bfqd->bfq_slice_idle = 8 * NSEC_PER_MSEC;

	bfqd->strict_guarantees = __data;

	return ret;
}

static ssize_t bfq_low_latency_store(struct elevator_queue *e,
				     const char *page, size_t count)
{
	struct bfq_data *bfqd = e->elevator_data;
	unsigned long uninitialized_var(__data);
	int ret = bfq_var_store(&__data, (page), count);

	if (__data > 1)
		__data = 1;
	if (__data == 0 && bfqd->low_latency != 0)
		bfq_end_wr(bfqd);
	bfqd->low_latency = __data;

	return ret;
}

#define BFQ_ATTR(name) \
	__ATTR(name, 0644, bfq_##name##_show, bfq_##name##_store)

static struct elv_fs_entry bfq_attrs[] = {
	BFQ_ATTR(fifo_expire_sync),
	BFQ_ATTR(fifo_expire_async),
	BFQ_ATTR(back_seek_max),
	BFQ_ATTR(back_seek_penalty),
	BFQ_ATTR(slice_idle),
	BFQ_ATTR(slice_idle_us),
	BFQ_ATTR(max_budget),
	BFQ_ATTR(timeout_sync),
	BFQ_ATTR(strict_guarantees),
	BFQ_ATTR(low_latency),
	__ATTR_NULL
};

static struct elevator_type iosched_bfq_mq = {
	.ops.mq = {
		.get_rq_priv		= bfq_get_rq_private,
		.put_rq_priv		= bfq_put_rq_private,
		.exit_icq		= bfq_exit_icq,
		.insert_requests	= bfq_insert_requests,
		.dispatch_request	= bfq_dispatch_request,
		.next_request		= elv_rb_latter_request,
		.former_request		= elv_rb_former_request,
		.allow_merge		= bfq_allow_bio_merge,
		.bio_merge		= bfq_bio_merge,
		.request_merge		= bfq_request_merge,
		.requests_merged	= bfq_requests_merged,
		.request_merged		= bfq_request_merged,
		.has_work		= bfq_has_work,
		.init_sched		= bfq_init_queue,
		.exit_sched		= bfq_exit_queue,
	},

	.uses_mq =		true,
	.icq_size =		sizeof(struct bfq_io_cq),
	.icq_align =		__alignof__(struct bfq_io_cq),
	.elevator_attrs =	bfq_attrs,
	.elevator_name =	"bfq",
	.elevator_owner =	THIS_MODULE,
};

#ifdef CONFIG_BFQ_GROUP_IOSCHED
static struct blkcg_policy blkcg_policy_bfq = {
	.dfl_cftypes		= bfq_blkg_files,
	.legacy_cftypes		= bfq_blkcg_legacy_files,

	.cpd_alloc_fn		= bfq_cpd_alloc,
	.cpd_init_fn		= bfq_cpd_init,
	.cpd_bind_fn	        = bfq_cpd_init,
	.cpd_free_fn		= bfq_cpd_free,

	.pd_alloc_fn		= bfq_pd_alloc,
	.pd_init_fn		= bfq_pd_init,
	.pd_offline_fn		= bfq_pd_offline,
	.pd_free_fn		= bfq_pd_free,
	.pd_reset_stats_fn	= bfq_pd_reset_stats,
};
#endif

static int __init bfq_init(void)
{
	int ret;

#ifdef CONFIG_BFQ_GROUP_IOSCHED
	ret = blkcg_policy_register(&blkcg_policy_bfq);
	if (ret)
		return ret;
#endif

	ret = -ENOMEM;
	if (bfq_slab_setup())
		goto err_pol_unreg;

	/*
	 * Times to load large popular applications for the typical
	 * systems installed on the reference devices (see the
	 * comments before the definitions of the next two
	 * arrays). Actually, we use slightly slower values, as the
	 * estimated peak rate tends to be smaller than the actual
	 * peak rate.  The reason for this last fact is that estimates
	 * are computed over much shorter time intervals than the long
	 * intervals typically used for benchmarking. Why? First, to
	 * adapt more quickly to variations. Second, because an I/O
	 * scheduler cannot rely on a peak-rate-evaluation workload to
	 * be run for a long time.
	 */
	T_slow[0] = msecs_to_jiffies(3500); /* actually 4 sec */
	T_slow[1] = msecs_to_jiffies(6000); /* actually 6.5 sec */
	T_fast[0] = msecs_to_jiffies(7000); /* actually 8 sec */
	T_fast[1] = msecs_to_jiffies(2500); /* actually 3 sec */

	/*
	 * Thresholds that determine the switch between speed classes
	 * (see the comments before the definition of the array
	 * device_speed_thresh). These thresholds are biased towards
	 * transitions to the fast class. This is safer than the
	 * opposite bias. In fact, a wrong transition to the slow
	 * class results in short weight-raising periods, because the
	 * speed of the device then tends to be higher that the
	 * reference peak rate. On the opposite end, a wrong
	 * transition to the fast class tends to increase
	 * weight-raising periods, because of the opposite reason.
	 */
	device_speed_thresh[0] = (4 * R_slow[0]) / 3;
	device_speed_thresh[1] = (4 * R_slow[1]) / 3;

	ret = elv_register(&iosched_bfq_mq);
	if (ret)
		goto err_pol_unreg;

	return 0;

err_pol_unreg:
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	blkcg_policy_unregister(&blkcg_policy_bfq);
#endif
	return ret;
}

static void __exit bfq_exit(void)
{
	elv_unregister(&iosched_bfq_mq);
#ifdef CONFIG_BFQ_GROUP_IOSCHED
	blkcg_policy_unregister(&blkcg_policy_bfq);
#endif
	bfq_slab_kill();
}

module_init(bfq_init);
module_exit(bfq_exit);

MODULE_AUTHOR("Paolo Valente");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MQ Budget Fair Queueing I/O Scheduler");
