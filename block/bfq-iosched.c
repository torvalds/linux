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

#define BFQ_DEFAULT_GRP_WEIGHT	10
#define BFQ_DEFAULT_GRP_IOPRIO	0
#define BFQ_DEFAULT_GRP_CLASS	IOPRIO_CLASS_BE

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
	/* entity in service */
	struct bfq_entity *in_service_entity;
	/* head-of-the-line entity in the scheduler */
	struct bfq_entity *next_in_service;
	/* array of service trees, one per ioprio_class */
	struct bfq_service_tree service_tree[BFQ_IOPRIO_CLASSES];
};

/**
 * struct bfq_entity - schedulable entity.
 *
 * A bfq_entity is used to represent a bfq_queue (leaf node in the upper
 * level scheduler). Each entity belongs to the sched_data of the parent
 * group hierarchy. Non-leaf entities have also their own sched_data,
 * stored in @my_sched_data.
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
 * The weight value is calculated from the ioprio to export the same
 * interface as CFQ.  When dealing with  ``well-behaved'' queues (i.e.,
 * queues that do not spend too much time to consume their budget
 * and have true sequential behavior, and when there are no external
 * factors breaking anticipation) the relative weights at each level
 * of the hierarchy should be guaranteed.  All the fields are
 * protected by the queue lock of the containing bfqd.
 */
struct bfq_entity {
	/* service_tree member */
	struct rb_node rb_node;

	/*
	 * flag, true if the entity is on a tree (either the active or
	 * the idle one of its service_tree).
	 */
	int on_st;

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
 * io_context or more, if it is async.
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

	/* root @bfq_sched_data for the device */
	struct bfq_sched_data sched_data;

	/*
	 * Number of bfq_queues containing requests (including the
	 * queue in service, even if it is idling).
	 */
	int busy_queues;
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
	/* bfq_io_cq (bic) associated with the @in_service_queue */
	struct bfq_io_cq *in_service_bic;

	/* on-disk position of the last served request */
	sector_t last_position;

	/* beginning of the last budget */
	ktime_t last_budget_start;
	/* beginning of the last idle slice */
	ktime_t last_idling_start;
	/* number of samples used to calculate @peak_rate */
	int peak_rate_samples;
	/*
	 * Peak read/write rate, observed during the service of a
	 * budget [BFQ_RATE_SHIFT * sectors/usec]. The value is
	 * left-shifted by BFQ_RATE_SHIFT to increase precision in
	 * fixed-point calculations.
	 */
	u64 peak_rate;
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
	/* last time CLASS_IDLE was served */
	u64 bfq_class_idle_last_service;

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
	BFQQF_busy = 0,		/* has requests or is in service */
	BFQQF_wait_request,	/* waiting for a request */
	BFQQF_non_blocking_wait_rq, /*
				     * waiting for a request
				     * without idling the device
				     */
	BFQQF_fifo_expire,	/* FIFO checked in this slice */
	BFQQF_idle_window,	/* slice idling enabled */
	BFQQF_sync,		/* synchronous queue */
	BFQQF_budget_new,	/* no completion with this budget */
	BFQQF_IO_bound,		/*
				 * bfqq has timed-out at least once
				 * having consumed at most 2/10 of
				 * its budget
				 */
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

BFQ_BFQQ_FNS(busy);
BFQ_BFQQ_FNS(wait_request);
BFQ_BFQQ_FNS(non_blocking_wait_rq);
BFQ_BFQQ_FNS(fifo_expire);
BFQ_BFQQ_FNS(idle_window);
BFQ_BFQQ_FNS(sync);
BFQ_BFQQ_FNS(budget_new);
BFQ_BFQQ_FNS(IO_bound);
#undef BFQ_BFQQ_FNS

/* Logging facilities. */
#define bfq_log_bfqq(bfqd, bfqq, fmt, args...) \
	blk_add_trace_msg((bfqd)->queue, "bfq%d " fmt, (bfqq)->pid, ##args)

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

static struct bfq_queue *bfq_entity_to_bfqq(struct bfq_entity *entity);

static struct bfq_service_tree *
bfq_entity_service_tree(struct bfq_entity *entity)
{
	struct bfq_sched_data *sched_data = entity->sched_data;
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);
	unsigned int idx = bfqq ? bfqq->ioprio_class - 1 :
				  BFQ_DEFAULT_GRP_CLASS - 1;

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

static void bfq_check_ioprio_change(struct bfq_io_cq *bic, struct bio *bio);
static void bfq_put_queue(struct bfq_queue *bfqq);
static struct bfq_queue *bfq_get_queue(struct bfq_data *bfqd,
				       struct bio *bio, bool is_sync,
				       struct bfq_io_cq *bic);
static void bfq_exit_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq);

/*
 * Array of async queues for all the processes, one queue
 * per ioprio value per ioprio_class.
 */
struct bfq_queue *async_bfqq[2][IOPRIO_BE_NR];
/* Async queue for the idle class (ioprio is ignored) */
struct bfq_queue *async_idle_bfqq;

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

/* Default timeout values, in jiffies, approximating CFQ defaults. */
static const int bfq_timeout = HZ / 8;

static struct kmem_cache *bfq_pool;

/* Below this threshold (in ms), we consider thinktime immediate. */
#define BFQ_MIN_TT		(2 * NSEC_PER_MSEC)

/* hw_tag detection: parallel requests threshold and min samples needed. */
#define BFQ_HW_QUEUE_THRESHOLD	4
#define BFQ_HW_QUEUE_SAMPLES	32

#define BFQQ_SEEK_THR		(sector_t)(8 * 100)
#define BFQQ_SECT_THR_NONROT	(sector_t)(2 * 32)
#define BFQQ_CLOSE_THR		(sector_t)(8 * 1024)
#define BFQQ_SEEKY(bfqq)	(hweight32(bfqq->seek_history) > 32/8)

/* Budget feedback step. */
#define BFQ_BUDGET_STEP         128

/* Min samples used for peak rate estimation (for autotuning). */
#define BFQ_PEAK_RATE_SAMPLES	32

/* Shift used for peak rate fixed precision calculations. */
#define BFQ_RATE_SHIFT		16

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
 * Next two macros are just fake loops for the moment. They will
 * become true loops in the cgroups-enabled variant of the code. Such
 * a variant, in its turn, will be introduced by next commit.
 */
#define for_each_entity(entity)	\
	for (; entity ; entity = NULL)

#define for_each_entity_safe(entity, parent) \
	for (parent = NULL; entity ; entity = parent)

static int bfq_update_next_in_service(struct bfq_sched_data *sd)
{
	return 0;
}

static void bfq_check_next_in_service(struct bfq_sched_data *sd,
				      struct bfq_entity *entity)
{
}

static void bfq_update_budget(struct bfq_entity *next_in_service)
{
}

/*
 * Shift for timestamp calculations.  This actually limits the maximum
 * service allowed in one timestamp delta (small shift values increase it),
 * the maximum total weight that can be used for the queues in the system
 * (big shift values increase it), and the period of virtual time
 * wraparounds.
 */
#define WFQ_SERVICE_SHIFT	22

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

	bfq_insert(&st->active, entity);

	if (node->rb_left)
		node = node->rb_left;
	else if (node->rb_right)
		node = node->rb_right;

	bfq_update_active_tree(node);

	if (bfqq)
		list_add(&bfqq->bfqq_list, &bfqq->bfqd->active_list);
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

	node = bfq_find_deepest(&entity->rb_node);
	bfq_extract(&st->active, entity);

	if (node)
		bfq_update_active_tree(node);

	if (bfqq)
		list_del(&bfqq->bfqq_list);
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

	entity->on_st = 0;
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
		unsigned short prev_weight, new_weight;
		struct bfq_data *bfqd = NULL;

		if (bfqq)
			bfqd = bfqq->bfqd;

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
		new_weight = entity->orig_weight;
		entity->weight = new_weight;

		new_st->wsum += entity->weight;

		if (new_st != old_st)
			entity->start = new_st->vtime;
	}

	return new_st;
}

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
	bfq_log_bfqq(bfqq->bfqd, bfqq, "bfqq_served %d secs", served);
}

/**
 * bfq_bfqq_charge_full_budget - set the service to the entity budget.
 * @bfqq: the queue that needs a service update.
 *
 * When it's not possible to be fair in the service domain, because
 * a queue is not consuming its budget fast enough (the meaning of
 * fast depends on the timeout parameter), we charge it a full
 * budget.  In this way we should obtain a sort of time-domain
 * fairness among all the seeky/slow queues.
 */
static void bfq_bfqq_charge_full_budget(struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;

	bfq_log_bfqq(bfqq->bfqd, bfqq, "charge_full_budget");

	bfq_bfqq_served(bfqq, entity->budget - entity->service);
}

/**
 * __bfq_activate_entity - activate an entity.
 * @entity: the entity being activated.
 * @non_blocking_wait_rq: true if this entity was waiting for a request
 *
 * Called whenever an entity is activated, i.e., it is not active and one
 * of its children receives a new request, or has to be reactivated due to
 * budget exhaustion.  It uses the current budget of the entity (and the
 * service received if @entity is active) of the queue to calculate its
 * timestamps.
 */
static void __bfq_activate_entity(struct bfq_entity *entity,
				  bool non_blocking_wait_rq)
{
	struct bfq_sched_data *sd = entity->sched_data;
	struct bfq_service_tree *st = bfq_entity_service_tree(entity);
	bool backshifted = false;

	if (entity == sd->in_service_entity) {
		/*
		 * If we are requeueing the current entity we have
		 * to take care of not charging to it service it has
		 * not received.
		 */
		bfq_calc_finish(entity, entity->service);
		entity->start = entity->finish;
		sd->in_service_entity = NULL;
	} else if (entity->tree == &st->active) {
		/*
		 * Requeueing an entity due to a change of some
		 * next_in_service entity below it.  We reuse the
		 * old start time.
		 */
		bfq_active_extract(st, entity);
	} else {
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

			entity->on_st = 1;
		}
	}

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
	 */
	if (backshifted && bfq_gt(st->vtime, entity->finish)) {
		unsigned long delta = st->vtime - entity->finish;

		entity->start += delta;
		entity->finish += delta;
	}

	bfq_active_insert(st, entity);
}

/**
 * bfq_activate_entity - activate an entity and its ancestors if necessary.
 * @entity: the entity to activate.
 * @non_blocking_wait_rq: true if this entity was waiting for a request
 *
 * Activate @entity and all the entities on the path from it to the root.
 */
static void bfq_activate_entity(struct bfq_entity *entity,
				bool non_blocking_wait_rq)
{
	struct bfq_sched_data *sd;

	for_each_entity(entity) {
		__bfq_activate_entity(entity, non_blocking_wait_rq);

		sd = entity->sched_data;
		if (!bfq_update_next_in_service(sd))
			/*
			 * No need to propagate the activation to the
			 * upper entities, as they will be updated when
			 * the in-service entity is rescheduled.
			 */
			break;
	}
}

/**
 * __bfq_deactivate_entity - deactivate an entity from its service tree.
 * @entity: the entity to deactivate.
 * @requeue: if false, the entity will not be put into the idle tree.
 *
 * Deactivate an entity, independently from its previous state.  If the
 * entity was not on a service tree just return, otherwise if it is on
 * any scheduler tree, extract it from that tree, and if necessary
 * and if the caller did not specify @requeue, put it on the idle tree.
 *
 * Return %1 if the caller should update the entity hierarchy, i.e.,
 * if the entity was in service or if it was the next_in_service for
 * its sched_data; return %0 otherwise.
 */
static int __bfq_deactivate_entity(struct bfq_entity *entity, int requeue)
{
	struct bfq_sched_data *sd = entity->sched_data;
	struct bfq_service_tree *st = bfq_entity_service_tree(entity);
	int is_in_service = entity == sd->in_service_entity;
	int ret = 0;

	if (!entity->on_st)
		return 0;

	if (is_in_service) {
		bfq_calc_finish(entity, entity->service);
		sd->in_service_entity = NULL;
	} else if (entity->tree == &st->active)
		bfq_active_extract(st, entity);
	else if (entity->tree == &st->idle)
		bfq_idle_extract(st, entity);

	if (is_in_service || sd->next_in_service == entity)
		ret = bfq_update_next_in_service(sd);

	if (!requeue || !bfq_gt(entity->finish, st->vtime))
		bfq_forget_entity(st, entity, is_in_service);
	else
		bfq_idle_insert(st, entity);

	return ret;
}

/**
 * bfq_deactivate_entity - deactivate an entity.
 * @entity: the entity to deactivate.
 * @requeue: true if the entity can be put on the idle tree
 */
static void bfq_deactivate_entity(struct bfq_entity *entity, int requeue)
{
	struct bfq_sched_data *sd;
	struct bfq_entity *parent = NULL;

	for_each_entity_safe(entity, parent) {
		sd = entity->sched_data;

		if (!__bfq_deactivate_entity(entity, requeue))
			/*
			 * The parent entity is still backlogged, and
			 * we don't need to update it as it is still
			 * in service.
			 */
			break;

		if (sd->next_in_service)
			/*
			 * The parent entity is still backlogged and
			 * the budgets on the path towards the root
			 * need to be updated.
			 */
			goto update;

		/*
		 * If we get here, then the parent is no more backlogged and
		 * we want to propagate the deactivation upwards.
		 */
		requeue = 1;
	}

	return;

update:
	entity = parent;
	for_each_entity(entity) {
		__bfq_activate_entity(entity, false);

		sd = entity->sched_data;
		if (!bfq_update_next_in_service(sd))
			break;
	}
}

/**
 * bfq_update_vtime - update vtime if necessary.
 * @st: the service tree to act upon.
 *
 * If necessary update the service tree vtime to have at least one
 * eligible entity, skipping to its start time.  Assumes that the
 * active tree of the device is not empty.
 *
 * NOTE: this hierarchical implementation updates vtimes quite often,
 * we may end up with reactivated processes getting timestamps after a
 * vtime skip done because we needed a ->first_active entity on some
 * intermediate node.
 */
static void bfq_update_vtime(struct bfq_service_tree *st)
{
	struct bfq_entity *entry;
	struct rb_node *node = st->active.rb_node;

	entry = rb_entry(node, struct bfq_entity, rb_node);
	if (bfq_gt(entry->min_start, st->vtime)) {
		st->vtime = entry->min_start;
		bfq_forget_idle(st);
	}
}

/**
 * bfq_first_active_entity - find the eligible entity with
 *                           the smallest finish time
 * @st: the service tree to select from.
 *
 * This function searches the first schedulable entity, starting from the
 * root of the tree and going on the left every time on this side there is
 * a subtree with at least one eligible (start >= vtime) entity. The path on
 * the right is followed only if a) the left subtree contains no eligible
 * entities and b) no eligible entity has been found yet.
 */
static struct bfq_entity *bfq_first_active_entity(struct bfq_service_tree *st)
{
	struct bfq_entity *entry, *first = NULL;
	struct rb_node *node = st->active.rb_node;

	while (node) {
		entry = rb_entry(node, struct bfq_entity, rb_node);
left:
		if (!bfq_gt(entry->start, st->vtime))
			first = entry;

		if (node->rb_left) {
			entry = rb_entry(node->rb_left,
					 struct bfq_entity, rb_node);
			if (!bfq_gt(entry->min_start, st->vtime)) {
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
 * Update the virtual time in @st and return the first eligible entity
 * it contains.
 */
static struct bfq_entity *__bfq_lookup_next_entity(struct bfq_service_tree *st,
						   bool force)
{
	struct bfq_entity *entity, *new_next_in_service = NULL;

	if (RB_EMPTY_ROOT(&st->active))
		return NULL;

	bfq_update_vtime(st);
	entity = bfq_first_active_entity(st);

	/*
	 * If the chosen entity does not match with the sched_data's
	 * next_in_service and we are forcedly serving the IDLE priority
	 * class tree, bubble up budget update.
	 */
	if (unlikely(force && entity != entity->sched_data->next_in_service)) {
		new_next_in_service = entity;
		for_each_entity(new_next_in_service)
			bfq_update_budget(new_next_in_service);
	}

	return entity;
}

/**
 * bfq_lookup_next_entity - return the first eligible entity in @sd.
 * @sd: the sched_data.
 * @extract: if true the returned entity will be also extracted from @sd.
 *
 * NOTE: since we cache the next_in_service entity at each level of the
 * hierarchy, the complexity of the lookup can be decreased with
 * absolutely no effort just returning the cached next_in_service value;
 * we prefer to do full lookups to test the consistency of the data
 * structures.
 */
static struct bfq_entity *bfq_lookup_next_entity(struct bfq_sched_data *sd,
						 int extract,
						 struct bfq_data *bfqd)
{
	struct bfq_service_tree *st = sd->service_tree;
	struct bfq_entity *entity;
	int i = 0;

	/*
	 * Choose from idle class, if needed to guarantee a minimum
	 * bandwidth to this class. This should also mitigate
	 * priority-inversion problems in case a low priority task is
	 * holding file system resources.
	 */
	if (bfqd &&
	    jiffies - bfqd->bfq_class_idle_last_service >
	    BFQ_CL_IDLE_TIMEOUT) {
		entity = __bfq_lookup_next_entity(st + BFQ_IOPRIO_CLASSES - 1,
						  true);
		if (entity) {
			i = BFQ_IOPRIO_CLASSES - 1;
			bfqd->bfq_class_idle_last_service = jiffies;
			sd->next_in_service = entity;
		}
	}
	for (; i < BFQ_IOPRIO_CLASSES; i++) {
		entity = __bfq_lookup_next_entity(st + i, false);
		if (entity) {
			if (extract) {
				bfq_check_next_in_service(sd, entity);
				bfq_active_extract(st + i, entity);
				sd->in_service_entity = entity;
				sd->next_in_service = NULL;
			}
			break;
		}
	}

	return entity;
}

static bool next_queue_may_preempt(struct bfq_data *bfqd)
{
	struct bfq_sched_data *sd = &bfqd->sched_data;

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

	sd = &bfqd->sched_data;
	for (; sd ; sd = entity->my_sched_data) {
		entity = bfq_lookup_next_entity(sd, 1, bfqd);
		entity->service = 0;
	}

	bfqq = bfq_entity_to_bfqq(entity);

	return bfqq;
}

static void __bfq_bfqd_reset_in_service(struct bfq_data *bfqd)
{
	struct bfq_queue *in_serv_bfqq = bfqd->in_service_queue;
	struct bfq_entity *in_serv_entity = &in_serv_bfqq->entity;

	if (bfqd->in_service_bic) {
		put_io_context(bfqd->in_service_bic->icq.ioc);
		bfqd->in_service_bic = NULL;
	}

	bfq_clear_bfqq_wait_request(in_serv_bfqq);
	hrtimer_try_to_cancel(&bfqd->idle_slice_timer);
	bfqd->in_service_queue = NULL;

	/*
	 * in_serv_entity is no longer in service, so, if it is in no
	 * service tree either, then release the service reference to
	 * the queue it represents (taken with bfq_get_entity).
	 */
	if (!in_serv_entity->on_st)
		bfq_put_queue(in_serv_bfqq);
}

static void bfq_deactivate_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq,
				int requeue)
{
	struct bfq_entity *entity = &bfqq->entity;

	bfq_deactivate_entity(entity, requeue);
}

static void bfq_activate_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	struct bfq_entity *entity = &bfqq->entity;

	bfq_activate_entity(entity, bfq_bfqq_non_blocking_wait_rq(bfqq));
	bfq_clear_bfqq_non_blocking_wait_rq(bfqq);
}

/*
 * Called when the bfqq no longer has requests pending, remove it from
 * the service tree.
 */
static void bfq_del_bfqq_busy(struct bfq_data *bfqd, struct bfq_queue *bfqq,
			      int requeue)
{
	bfq_log_bfqq(bfqd, bfqq, "del from busy");

	bfq_clear_bfqq_busy(bfqq);

	bfqd->busy_queues--;

	bfq_deactivate_bfqq(bfqd, bfqq, requeue);
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
}

static void bfq_init_entity(struct bfq_entity *entity)
{
	struct bfq_queue *bfqq = bfq_entity_to_bfqq(entity);

	entity->weight = entity->new_weight;
	entity->orig_weight = entity->new_weight;

	bfqq->ioprio = bfqq->new_ioprio;
	bfqq->ioprio_class = bfqq->new_ioprio_class;

	entity->sched_data = &bfqq->bfqd->sched_data;
}

#define bfq_class_idle(bfqq)	((bfqq)->ioprio_class == IOPRIO_CLASS_IDLE)
#define bfq_class_rt(bfqq)	((bfqq)->ioprio_class == IOPRIO_CLASS_RT)

#define bfq_sample_valid(samples)	((samples) > 80)

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

static unsigned long bfq_serv_to_charge(struct request *rq,
					struct bfq_queue *bfqq)
{
	return blk_rq_sectors(rq);
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
		bfq_activate_bfqq(bfqd, bfqq);
	}
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
 * for preempting the in-service queue is to achieve the following
 * goal: guarantee to bfqq its reserved bandwidth even if bfqq has
 * expired because it has remained idle.
 *
 * In particular, bfqq may have expired for one of the following two
 * reasons:
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
 */
static bool bfq_bfqq_update_budg_for_activation(struct bfq_data *bfqd,
						struct bfq_queue *bfqq,
						bool arrived_in_time)
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
	return false;
}

static void bfq_bfqq_handle_idle_busy_switch(struct bfq_data *bfqd,
					     struct bfq_queue *bfqq,
					     struct request *rq)
{
	bool bfqq_wants_to_preempt,
		/*
		 * See the comments on
		 * bfq_bfqq_update_budg_for_activation for
		 * details on the usage of the next variable.
		 */
		arrived_in_time =  ktime_get_ns() <=
			bfqq->ttime.last_end_request +
			bfqd->bfq_slice_idle * 3;

	/*
	 * Update budget and check whether bfqq may want to preempt
	 * the in-service queue.
	 */
	bfqq_wants_to_preempt =
		bfq_bfqq_update_budg_for_activation(bfqd, bfqq,
						    arrived_in_time);

	if (!bfq_bfqq_IO_bound(bfqq)) {
		if (arrived_in_time) {
			bfqq->requests_within_timer++;
			if (bfqq->requests_within_timer >=
			    bfqd->bfq_requests_within_timer)
				bfq_mark_bfqq_IO_bound(bfqq);
		} else
			bfqq->requests_within_timer = 0;
	}

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
	    next_queue_may_preempt(bfqd))
		bfq_bfqq_expire(bfqd, bfqd->in_service_queue,
				false, BFQQE_PREEMPTED);
}

static void bfq_add_request(struct request *rq)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq);
	struct bfq_data *bfqd = bfqq->bfqd;
	struct request *next_rq, *prev;

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

	if (!bfq_bfqq_busy(bfqq)) /* switching to busy ... */
		bfq_bfqq_handle_idle_busy_switch(bfqd, bfqq, rq);
	else if (prev != bfqq->next_rq)
		bfq_updated_next_req(bfqd, bfqq);
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

#if 0 /* Still not clear if we can do without next two functions */
static void bfq_activate_request(struct request_queue *q, struct request *rq)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;

	bfqd->rq_in_driver++;
	bfqd->last_position = blk_rq_pos(rq) + blk_rq_sectors(rq);
	bfq_log(bfqd, "activate_request: new bfqd->last_position %llu",
		(unsigned long long)bfqd->last_position);
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
			bfq_del_bfqq_busy(bfqd, bfqq, 1);
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
	}

	if (rq->cmd_flags & REQ_META)
		bfqq->meta_pending--;
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
		 * If next_rq changes, update the queue's budget to fit
		 * the new request.
		 */
		if (prev != bfqq->next_rq)
			bfq_updated_next_req(bfqd, bfqq);
	}
}

static void bfq_requests_merged(struct request_queue *q, struct request *rq,
				struct request *next)
{
	struct bfq_queue *bfqq = RQ_BFQQ(rq), *next_bfqq = RQ_BFQQ(next);

	if (!RB_EMPTY_NODE(&rq->rb_node))
		return;
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
}

static bool bfq_allow_bio_merge(struct request_queue *q, struct request *rq,
				struct bio *bio)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	bool is_sync = op_is_sync(bio->bi_opf);
	struct bfq_queue *bfqq = bfqd->bio_bfqq;

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

	return bfqq == RQ_BFQQ(rq);
}

static void __bfq_set_in_service_queue(struct bfq_data *bfqd,
				       struct bfq_queue *bfqq)
{
	if (bfqq) {
		bfq_mark_bfqq_budget_new(bfqq);
		bfq_clear_bfqq_fifo_expire(bfqq);

		bfqd->budgets_assigned = (bfqd->budgets_assigned * 7 + 256) / 8;

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

/*
 * bfq_default_budget - return the default budget for @bfqq on @bfqd.
 * @bfqd: the device descriptor.
 * @bfqq: the queue to consider.
 *
 * We use 3/4 of the @bfqd maximum budget as the default value
 * for the max_budget field of the queues.  This lets the feedback
 * mechanism to start from some middle ground, then the behavior
 * of the process will drive the heuristics towards high values, if
 * it behaves as a greedy sequential reader, or towards small values
 * if it shows a more intermittent behavior.
 */
static unsigned long bfq_default_budget(struct bfq_data *bfqd,
					struct bfq_queue *bfqq)
{
	unsigned long budget;

	/*
	 * When we need an estimate of the peak rate we need to avoid
	 * to give budgets that are too short due to previous
	 * measurements.  So, in the first 10 assignments use a
	 * ``safe'' budget value. For such first assignment the value
	 * of bfqd->budgets_assigned happens to be lower than 194.
	 * See __bfq_set_in_service_queue for the formula by which
	 * this field is computed.
	 */
	if (bfqd->budgets_assigned < 194 && bfqd->bfq_user_max_budget == 0)
		budget = bfq_default_max_budget;
	else
		budget = bfqd->bfq_max_budget;

	return budget - budget / 4;
}

static void bfq_arm_slice_timer(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq = bfqd->in_service_queue;
	struct bfq_io_cq *bic;
	u32 sl;

	/* Processes have exited, don't wait. */
	bic = bfqd->in_service_bic;
	if (!bic || atomic_read(&bic->icq.ioc->active_ref) == 0)
		return;

	bfq_mark_bfqq_wait_request(bfqq);

	/*
	 * We don't want to idle for seeks, but we do want to allow
	 * fair distribution of slice time for a process doing back-to-back
	 * seeks. So allow a little bit of time for him to submit a new rq.
	 */
	sl = bfqd->bfq_slice_idle;
	/*
	 * Grant only minimum idle time if the queue is seeky.
	 */
	if (BFQQ_SEEKY(bfqq))
		sl = min_t(u64, sl, BFQ_MIN_TT);

	bfqd->last_idling_start = ktime_get();
	hrtimer_start(&bfqd->idle_slice_timer, ns_to_ktime(sl),
		      HRTIMER_MODE_REL);
}

/*
 * Set the maximum time for the in-service queue to consume its
 * budget. This prevents seeky processes from lowering the disk
 * throughput (always guaranteed with a time slice scheme as in CFQ).
 */
static void bfq_set_budget_timeout(struct bfq_data *bfqd)
{
	struct bfq_queue *bfqq = bfqd->in_service_queue;
	unsigned int timeout_coeff = bfqq->entity.weight /
				     bfqq->entity.orig_weight;

	bfqd->last_budget_start = ktime_get();

	bfq_clear_bfqq_budget_new(bfqq);
	bfqq->budget_timeout = jiffies +
		bfqd->bfq_timeout * timeout_coeff;

	bfq_log_bfqq(bfqd, bfqq, "set budget_timeout %u",
		jiffies_to_msecs(bfqd->bfq_timeout * timeout_coeff));
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

	bfq_remove_request(q, rq);
}

static void __bfq_bfqq_expire(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	__bfq_bfqd_reset_in_service(bfqd);

	if (RB_EMPTY_ROOT(&bfqq->sort_list))
		bfq_del_bfqq_busy(bfqd, bfqq, 1);
	else
		bfq_activate_bfqq(bfqd, bfqq);
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

	budget = bfqq->max_budget;
	min_budget = bfq_min_budget(bfqd);

	bfq_log_bfqq(bfqd, bfqq, "recalc_budg: last budg %d, budg left %d",
		bfqq->entity.budget, bfq_bfqq_budget_left(bfqq));
	bfq_log_bfqq(bfqd, bfqq, "recalc_budg: last max_budg %d, min budg %d",
		budget, bfq_min_budget(bfqd));
	bfq_log_bfqq(bfqd, bfqq, "recalc_budg: sync %d, seeky %d",
		bfq_bfqq_sync(bfqq), BFQQ_SEEKY(bfqd->in_service_queue));

	if (bfq_bfqq_sync(bfqq)) {
		switch (reason) {
		/*
		 * Caveat: in all the following cases we trade latency
		 * for throughput.
		 */
		case BFQQE_TOO_IDLE:
			if (budget > min_budget + BFQ_BUDGET_STEP)
				budget -= BFQ_BUDGET_STEP;
			else
				budget = min_budget;
			break;
		case BFQQE_BUDGET_TIMEOUT:
			budget = bfq_default_budget(bfqd, bfqq);
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
			budget = min(budget + 8 * BFQ_BUDGET_STEP,
				     bfqd->bfq_max_budget);
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
	} else {
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

static unsigned long bfq_calc_max_budget(u64 peak_rate, u64 timeout)
{
	unsigned long max_budget;

	/*
	 * The max_budget calculated when autotuning is equal to the
	 * amount of sectors transferred in timeout at the estimated
	 * peak rate. To get this value, peak_rate is, first,
	 * multiplied by 1000, because timeout is measured in ms,
	 * while peak_rate is measured in sectors/usecs. Then the
	 * result of this multiplication is right-shifted by
	 * BFQ_RATE_SHIFT, because peak_rate is equal to the value of
	 * the peak rate left-shifted by BFQ_RATE_SHIFT.
	 */
	max_budget = (unsigned long)(peak_rate * 1000 *
				     timeout >> BFQ_RATE_SHIFT);

	return max_budget;
}

/*
 * In addition to updating the peak rate, checks whether the process
 * is "slow", and returns 1 if so. This slow flag is used, in addition
 * to the budget timeout, to reduce the amount of service provided to
 * seeky processes, and hence reduce their chances to lower the
 * throughput. See the code for more details.
 */
static bool bfq_update_peak_rate(struct bfq_data *bfqd, struct bfq_queue *bfqq,
				 bool compensate)
{
	u64 bw, usecs, expected, timeout;
	ktime_t delta;
	int update = 0;

	if (!bfq_bfqq_sync(bfqq) || bfq_bfqq_budget_new(bfqq))
		return false;

	if (compensate)
		delta = bfqd->last_idling_start;
	else
		delta = ktime_get();
	delta = ktime_sub(delta, bfqd->last_budget_start);
	usecs = ktime_to_us(delta);

	/* don't use too short time intervals */
	if (usecs < 1000)
		return false;

	/*
	 * Calculate the bandwidth for the last slice.  We use a 64 bit
	 * value to store the peak rate, in sectors per usec in fixed
	 * point math.  We do so to have enough precision in the estimate
	 * and to avoid overflows.
	 */
	bw = (u64)bfqq->entity.service << BFQ_RATE_SHIFT;
	do_div(bw, (unsigned long)usecs);

	timeout = jiffies_to_msecs(bfqd->bfq_timeout);

	/*
	 * Use only long (> 20ms) intervals to filter out spikes for
	 * the peak rate estimation.
	 */
	if (usecs > 20000) {
		if (bw > bfqd->peak_rate) {
			bfqd->peak_rate = bw;
			update = 1;
			bfq_log(bfqd, "new peak_rate=%llu", bw);
		}

		update |= bfqd->peak_rate_samples == BFQ_PEAK_RATE_SAMPLES - 1;

		if (bfqd->peak_rate_samples < BFQ_PEAK_RATE_SAMPLES)
			bfqd->peak_rate_samples++;

		if (bfqd->peak_rate_samples == BFQ_PEAK_RATE_SAMPLES &&
		    update && bfqd->bfq_user_max_budget == 0) {
			bfqd->bfq_max_budget =
				bfq_calc_max_budget(bfqd->peak_rate,
						    timeout);
			bfq_log(bfqd, "new max_budget=%d",
				bfqd->bfq_max_budget);
		}
	}

	/*
	 * A process is considered ``slow'' (i.e., seeky, so that we
	 * cannot treat it fairly in the service domain, as it would
	 * slow down too much the other processes) if, when a slice
	 * ends for whatever reason, it has received service at a
	 * rate that would not be high enough to complete the budget
	 * before the budget timeout expiration.
	 */
	expected = bw * 1000 * timeout >> BFQ_RATE_SHIFT;

	/*
	 * Caveat: processes doing IO in the slower disk zones will
	 * tend to be slow(er) even if not seeky. And the estimated
	 * peak rate will actually be an average over the disk
	 * surface. Hence, to not be too harsh with unlucky processes,
	 * we keep a budget/3 margin of safety before declaring a
	 * process slow.
	 */
	return expected > (4 * bfqq->entity.budget) / 3;
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
 *
 * If the process associated with the queue is slow (i.e., seeky), or
 * in case of budget timeout, or, finally, if it is async, we
 * artificially charge it an entire budget (independently of the
 * actual service it received). As a consequence, the queue will get
 * higher timestamps than the correct ones upon reactivation, and
 * hence it will be rescheduled as if it had received more service
 * than what it actually received. In the end, this class of processes
 * will receive less service in proportion to how slowly they consume
 * their budgets (and hence how seriously they tend to lower the
 * throughput).
 *
 * In contrast, when a queue expires because it has been idling for
 * too much or because it exhausted its budget, we do not touch the
 * amount of service it has received. Hence when the queue will be
 * reactivated and its timestamps updated, the latter will be in sync
 * with the actual service received by the queue until expiration.
 *
 * Charging a full budget to the first type of queues and the exact
 * service to the others has the effect of using the WF2Q+ policy to
 * schedule the former on a timeslice basis, without violating the
 * service domain guarantees of the latter.
 */
static void bfq_bfqq_expire(struct bfq_data *bfqd,
			    struct bfq_queue *bfqq,
			    bool compensate,
			    enum bfqq_expiration reason)
{
	bool slow;
	int ref;

	/*
	 * Update device peak rate for autotuning and check whether the
	 * process is slow (see bfq_update_peak_rate).
	 */
	slow = bfq_update_peak_rate(bfqd, bfqq, compensate);

	/*
	 * As above explained, 'punish' slow (i.e., seeky), timed-out
	 * and async queues, to favor sequential sync workloads.
	 */
	if (slow || reason == BFQQE_BUDGET_TIMEOUT)
		bfq_bfqq_charge_full_budget(bfqq);

	if (reason == BFQQE_TOO_IDLE &&
	    bfqq->entity.service <= 2 * bfqq->entity.budget / 10)
		bfq_clear_bfqq_IO_bound(bfqq);

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
	if (bfq_bfqq_budget_new(bfqq) ||
	    time_is_after_jiffies(bfqq->budget_timeout))
		return false;
	return true;
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
 * this function returns true for the queue. And this function returns
 * true only if idling is beneficial for throughput.
 */
static bool bfq_bfqq_may_idle(struct bfq_queue *bfqq)
{
	struct bfq_data *bfqd = bfqq->bfqd;
	bool idling_boosts_thr;

	if (bfqd->strict_guarantees)
		return true;

	/*
	 * The value of the next variable is computed considering that
	 * idling is usually beneficial for the throughput if:
	 * (a) the device is not NCQ-capable, or
	 * (b) regardless of the presence of NCQ, the request pattern
	 *     for bfqq is I/O-bound (possible throughput losses
	 *     caused by granting idling to seeky queues are mitigated
	 *     by the fact that, in all scenarios where boosting
	 *     throughput is the best thing to do, i.e., in all
	 *     symmetric scenarios, only a minimal idle time is
	 *     allowed to seeky queues).
	 */
	idling_boosts_thr = !bfqd->hw_tag || bfq_bfqq_IO_bound(bfqq);

	/*
	 * We have now the components we need to compute the return
	 * value of the function, which is true only if both the
	 * following conditions hold:
	 * 1) bfqq is sync, because idling make sense only for sync queues;
	 * 2) idling boosts the throughput.
	 */
	return bfq_bfqq_sync(bfqq) && idling_boosts_thr;
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

	if (!bfqd->in_service_bic) {
		atomic_long_inc(&RQ_BIC(rq)->icq.ioc->refcount);
		bfqd->in_service_bic = RQ_BIC(rq);
	}

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
	if (bfqq->bfqd)
		bfq_log_bfqq(bfqq->bfqd, bfqq, "put_queue: %p %d",
			     bfqq, bfqq->ref);

	bfqq->ref--;
	if (bfqq->ref)
		return;

	kmem_cache_free(bfq_pool, bfqq);
}

static void bfq_exit_bfqq(struct bfq_data *bfqd, struct bfq_queue *bfqq)
{
	if (bfqq == bfqd->in_service_queue) {
		__bfq_bfqq_expire(bfqd, bfqq);
		bfq_schedule_dispatch(bfqd);
	}

	bfq_log_bfqq(bfqd, bfqq, "exit_bfqq: %p, %d", bfqq, bfqq->ref);

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
		spin_unlock_irq(&bfqd->lock);
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

	bfqq->ref = 0;
	bfqq->bfqd = bfqd;

	if (bic)
		bfq_set_next_ioprio_data(bfqq, bic);

	if (is_sync) {
		if (!bfq_class_idle(bfqq))
			bfq_mark_bfqq_idle_window(bfqq);
		bfq_mark_bfqq_sync(bfqq);
	} else
		bfq_clear_bfqq_sync(bfqq);

	/* set end request to minus infinity from now */
	bfqq->ttime.last_end_request = ktime_get_ns() + 1;

	bfq_mark_bfqq_IO_bound(bfqq);

	bfqq->pid = pid;

	/* Tentative initial value to trade off between thr and lat */
	bfqq->max_budget = bfq_default_budget(bfqd, bfqq);
	bfqq->budget_timeout = bfq_smallest_from_now();
	bfqq->pid = pid;

	/* first request is almost certainly seeky */
	bfqq->seek_history = 1;
}

static struct bfq_queue **bfq_async_queue_prio(struct bfq_data *bfqd,
					       int ioprio_class, int ioprio)
{
	switch (ioprio_class) {
	case IOPRIO_CLASS_RT:
		return &async_bfqq[0][ioprio];
	case IOPRIO_CLASS_NONE:
		ioprio = IOPRIO_NORM;
		/* fall through */
	case IOPRIO_CLASS_BE:
		return &async_bfqq[1][ioprio];
	case IOPRIO_CLASS_IDLE:
		return &async_idle_bfqq;
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

	rcu_read_lock();

	if (!is_sync) {
		async_bfqq = bfq_async_queue_prio(bfqd, ioprio_class,
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
		bfq_init_entity(&bfqq->entity);
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
		bfqq->ref++;
		bfq_log_bfqq(bfqd, bfqq,
			     "get_queue, bfqq not in async: %p, %d",
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
	sector_t sdist = 0;

	if (bfqq->last_request_pos) {
		if (bfqq->last_request_pos < blk_rq_pos(rq))
			sdist = blk_rq_pos(rq) - bfqq->last_request_pos;
		else
			sdist = bfqq->last_request_pos - blk_rq_pos(rq);
	}

	bfqq->seek_history <<= 1;
	bfqq->seek_history |= sdist > BFQQ_SEEK_THR &&
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

	enable_idle = bfq_bfqq_idle_window(bfqq);

	if (atomic_read(&bic->icq.ioc->active_ref) == 0 ||
	    bfqd->bfq_slice_idle == 0 ||
		(bfqd->hw_tag && BFQQ_SEEKY(bfqq)))
		enable_idle = 0;
	else if (bfq_sample_valid(bfqq->ttime.ttime_samples)) {
		if (bfqq->ttime.ttime_mean > bfqd->bfq_slice_idle)
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
	struct bfq_queue *bfqq = RQ_BFQQ(rq);

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
	bfq_update_hw_tag(bfqd);

	bfqd->rq_in_driver--;
	bfqq->dispatched--;

	bfqq->ttime.last_end_request = ktime_get_ns();

	/*
	 * If this is the in-service queue, check if it needs to be expired,
	 * or if we want to idle in case it has no pending requests.
	 */
	if (bfqd->in_service_queue == bfqq) {
		if (bfq_bfqq_budget_new(bfqq))
			bfq_set_budget_timeout(bfqd);

		if (bfq_bfqq_must_idle(bfqq)) {
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
 * Allocate bfq data structures associated with this request.
 */
static int bfq_get_rq_private(struct request_queue *q, struct request *rq,
			      struct bio *bio)
{
	struct bfq_data *bfqd = q->elevator->elevator_data;
	struct bfq_io_cq *bic = icq_to_bic(rq->elv.icq);
	const int is_sync = rq_is_sync(rq);
	struct bfq_queue *bfqq;

	spin_lock_irq(&bfqd->lock);

	bfq_check_ioprio_change(bic, bio);

	if (!bic)
		goto queue_fail;

	bfqq = bic_to_bfqq(bic, is_sync);
	if (!bfqq || bfqq == &bfqd->oom_bfqq) {
		if (bfqq)
			bfq_put_queue(bfqq);
		bfqq = bfq_get_queue(bfqd, bio, is_sync, bic);
		bic_set_bfqq(bic, bfqq, is_sync);
	}

	bfqq->allocated++;
	bfqq->ref++;
	bfq_log_bfqq(bfqd, bfqq, "get_request %p: bfqq %p, %d",
		     rq, bfqq, bfqq->ref);

	rq->elv.priv[0] = bic;
	rq->elv.priv[1] = bfqq;

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
		bfq_log_bfqq(bfqd, bfqq, "put_async_bfqq: putting %p, %d",
			     bfqq, bfqq->ref);
		bfq_put_queue(bfqq);
		*bfqq_ptr = NULL;
	}
}

/*
 * Release the extra reference of the async queues as the device
 * goes away.
 */
static void bfq_put_async_queues(struct bfq_data *bfqd)
{
	int i, j;

	for (i = 0; i < 2; i++)
		for (j = 0; j < IOPRIO_BE_NR; j++)
			__bfq_put_async_bfqq(bfqd, &async_bfqq[i][j]);

	__bfq_put_async_bfqq(bfqd, &async_idle_bfqq);
}

static void bfq_exit_queue(struct elevator_queue *e)
{
	struct bfq_data *bfqd = e->elevator_data;
	struct bfq_queue *bfqq, *n;

	hrtimer_cancel(&bfqd->idle_slice_timer);

	spin_lock_irq(&bfqd->lock);
	list_for_each_entry_safe(bfqq, n, &bfqd->idle_list, bfqq_list)
		bfq_deactivate_bfqq(bfqd, bfqq, false);
	bfq_put_async_queues(bfqd);
	spin_unlock_irq(&bfqd->lock);

	hrtimer_cancel(&bfqd->idle_slice_timer);

	kfree(bfqd);
}

static int bfq_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct bfq_data *bfqd;
	struct elevator_queue *eq;
	int i;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	bfqd = kzalloc_node(sizeof(*bfqd), GFP_KERNEL, q->node);
	if (!bfqd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	eq->elevator_data = bfqd;

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
	/*
	 * Trigger weight initialization, according to ioprio, at the
	 * oom_bfqq's first activation. The oom_bfqq's ioprio and ioprio
	 * class won't be changed any more.
	 */
	bfqd->oom_bfqq.entity.prio_changed = 1;

	bfqd->queue = q;

	for (i = 0; i < BFQ_IOPRIO_CLASSES; i++)
		bfqd->sched_data.service_tree[i] = BFQ_SERVICE_TREE_INIT;

	hrtimer_init(&bfqd->idle_slice_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	bfqd->idle_slice_timer.function = bfq_idle_slice_timer;

	INIT_LIST_HEAD(&bfqd->active_list);
	INIT_LIST_HEAD(&bfqd->idle_list);

	bfqd->hw_tag = -1;

	bfqd->bfq_max_budget = bfq_default_max_budget;

	bfqd->bfq_fifo_expire[0] = bfq_fifo_expire[0];
	bfqd->bfq_fifo_expire[1] = bfq_fifo_expire[1];
	bfqd->bfq_back_max = bfq_back_max;
	bfqd->bfq_back_penalty = bfq_back_penalty;
	bfqd->bfq_slice_idle = bfq_slice_idle;
	bfqd->bfq_class_idle_last_service = 0;
	bfqd->bfq_timeout = bfq_timeout;

	bfqd->bfq_requests_within_timer = 120;

	spin_lock_init(&bfqd->lock);
	INIT_LIST_HEAD(&bfqd->dispatch);

	q->elevator = eq;

	return 0;
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

static unsigned long bfq_estimated_max_budget(struct bfq_data *bfqd)
{
	u64 timeout = jiffies_to_msecs(bfqd->bfq_timeout);

	if (bfqd->peak_rate_samples >= BFQ_PEAK_RATE_SAMPLES)
		return bfq_calc_max_budget(bfqd->peak_rate, timeout);
	else
		return bfq_default_max_budget;
}

static ssize_t bfq_max_budget_store(struct elevator_queue *e,
				    const char *page, size_t count)
{
	struct bfq_data *bfqd = e->elevator_data;
	unsigned long uninitialized_var(__data);
	int ret = bfq_var_store(&__data, (page), count);

	if (__data == 0)
		bfqd->bfq_max_budget = bfq_estimated_max_budget(bfqd);
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
		bfqd->bfq_max_budget = bfq_estimated_max_budget(bfqd);

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

static int __init bfq_init(void)
{
	int ret;

	ret = -ENOMEM;
	if (bfq_slab_setup())
		goto err_pol_unreg;

	ret = elv_register(&iosched_bfq_mq);
	if (ret)
		goto err_pol_unreg;

	return 0;

err_pol_unreg:
	return ret;
}

static void __exit bfq_exit(void)
{
	elv_unregister(&iosched_bfq_mq);
	bfq_slab_kill();
}

module_init(bfq_init);
module_exit(bfq_exit);

MODULE_AUTHOR("Paolo Valente");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MQ Budget Fair Queueing I/O Scheduler");
